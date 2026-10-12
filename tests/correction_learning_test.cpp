// Offline regression: the local learning loop (P2).
//
// Three contracts are pinned here:
//   1. correction_extractor accepts exactly the "local textual replacement"
//      shape and refuses insertions, deletions, whitespace churn, punctuation
//      churn and far-apart edits (a wrong pair would teach a wrong habit);
//   2. learning_store persists JSONL, reinforces repeats (weight 4 -> 5, never
//      50) and evicts by (hits, lastMs); the *merge* into the runtime vocabulary
//      uses the lowest weight instead, so a learned word can never outrank
//      explicit vocabulary (see MergeLearnedEntries);
//   3. injection_ledger is single-slot, monotonic and single-use.

#include "correction_extractor.h"
#include "injection_ledger.h"
#include "learning_store.h"
#include "vocabulary_manager.h"

#include <windows.h>

#include <utility>

#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Expect(bool condition, const char* description) {
    if (condition) return;
    std::cerr << "FAIL: " << description << '\n';
    ++g_failures;
}

struct ExtractCase {
    const wchar_t* injected;
    const wchar_t* observed;
    bool accepted;
    const wchar_t* expectedOriginal;
    const wchar_t* expectedCorrected;
};

const ExtractCase kExtractCases[] = {
    // Local replacement inside a longer sentence.
    {L"请打开这里办办理医保业务", L"请打开浙里办办理医保业务", true, L"这", L"浙"},
    // Identical text is not a correction.
    {L"今天下雨了", L"今天下雨了", false, L"", L""},
    // Pure insertion (the user kept typing) and pure deletion are refused.
    {L"今天下雨了", L"今天下雨了。", false, L"", L""},
    {L"测试文本", L"测试", false, L"", L""},
    // Too short to learn from.
    {L"嗯", L"恩", false, L"", L""},
    // Whitespace churn inside the pair.
    {L"你好\t世界", L"你好 世界", false, L"", L""},
    // Punctuation-only edit is noise, not a learned word.
    {L"你好，世界", L"你好。世界", false, L"", L""},
    // The edit must stay small relative to the injected text.
    {L"abcdefghijklmnopqrstuvwx", L"zyxwvutsrqponmlkjihgfedc", false, L"", L""},
    // Length ratio cap (observed far longer than injected).
    {L"确定", L"确定一下再确认一遍", false, L"", L""},
    // Multi-character replacement still yields the trimmed middle.
    {L"我们昨天开会讨论了新方案", L"我们昨天复盘讨论了新方案", true, L"开会", L"复盘"},
    // A pair that spans a line break is refused.
    {L"abcde", L"abc\nde", false, L"", L""},
    // The injection itself must be single-line.
    {L"你好\t世界", L"你好世界", false, L"", L""},
};

// The document-level entry point is what the injection reflection actually
// calls: the read-back is a whole document, not the injected sentence.
void RunDocumentCases() {
    correction_extractor::CorrectionPair pair;
    size_t position = 0;

    // Injection embedded in a document, corrected in the middle.
    const std::wstring injected = L"请打开这里办办理医保业务";
    const std::wstring document =
        L"会议记录\n" + std::wstring(200, L'前') + L"\n" + L"请打开浙里办办理医保业务，谢谢\n" +
        std::wstring(200, L'后');
    Expect(correction_extractor::ExtractFromDocument(injected, document, pair, {}, &position),
           "a long multi-line document still yields a pair");
    Expect(pair.original == L"这" && pair.corrected == L"浙",
           "the pair is the differing middle, not the document");
    Expect(position > 0, "the match position is reported");

    // The exact injected text is gone because the user edited inside it; the
    // head anchor must still find the region.
    const std::wstring editedInside = L"会议记录\n请打开浙里办办理医保业务。";
    Expect(correction_extractor::ExtractFromDocument(injected, editedInside, pair),
           "a head anchor finds the region when the text was edited inside");
    Expect(pair.original == L"这" && pair.corrected == L"浙",
           "the anchored window still yields the trimmed pair");

    // The injection is not in the document at all (caret moved elsewhere).
    Expect(!correction_extractor::ExtractFromDocument(
               injected, std::wstring(L"这里完全是别的内容，与注入无关。"), pair),
           "an unrelated document is not learned from");

    // Too short to anchor safely.
    Expect(!correction_extractor::ExtractFromDocument(L"好", L"好", pair),
           "a one-character injection is never learned");

    // Line endings normalized by the host application are retried on a
    // collapsed copy, so a CRLF document still yields the pair.
    const std::wstring crlfDocument = L"记录\r\n请打开浙里办办理医保业务\r\n";
    Expect(correction_extractor::ExtractFromDocument(injected, crlfDocument, pair),
           "a CRLF document is matched after collapsing line breaks");
    Expect(pair.original == L"这" && pair.corrected == L"浙",
           "the collapsed match still yields the trimmed pair");
}

std::wstring TempStorePath() {
    wchar_t dir[MAX_PATH] = {};
    const DWORD length = GetTempPathW(MAX_PATH, dir);
    std::wstring path = length ? std::wstring(dir, length) : std::wstring(L".\\");
    path += L"voxtype_learning_store_test.json";
    return path;
}

void RunExtractorCases() {
    for (const ExtractCase& c : kExtractCases) {
        correction_extractor::CorrectionPair pair;
        const bool accepted = correction_extractor::Extract(c.injected, c.observed, pair);
        if (accepted != c.accepted) {
            std::cerr << "FAIL: extraction acceptance mismatch for a table case\n";
            ++g_failures;
            continue;
        }
        if (!accepted) continue;
        if (pair.original != c.expectedOriginal || pair.corrected != c.expectedCorrected) {
            std::cerr << "FAIL: extraction pair mismatch\n";
            ++g_failures;
        }
    }
}

void RunStoreCases() {
    const std::wstring path = TempStorePath();
    learning_store::SetPathOverrideForTests(path);
    learning_store::LoadingPolicy policy;
    policy.maxPairs = 2;
    policy.maxPairChars = 48;

    learning_store::Clear();
    Expect(learning_store::Count(policy) == 0, "store starts empty");

    Expect(learning_store::Add(L"这里", L"浙里", 1000, policy), "first pair is stored");
    Expect(learning_store::Count(policy) == 1, "store holds one pair");

    Expect(learning_store::Add(L"这里", L"浙里", 2000, policy), "repeat is reinforced");
    std::vector<learning_store::Entry> entries = learning_store::Load(policy);
    Expect(entries.size() == 1 && entries[0].hits == 2, "repeat increments hits");
    Expect(entries[0].weight == 4, "weight stays 4 below the repeat threshold");

    learning_store::Add(L"这里", L"浙里", 3000, policy);  // hits = 3
    entries = learning_store::Load(policy);
    Expect(!entries.empty() && entries[0].weight == 5, "weight promotes to 5 after 3 hits");
    Expect(!entries.empty() && entries[0].weight != 50, "learned words never reach the super-hotword tier");

    learning_store::Add(L"开会", L"复盘", 4000, policy);      // hits = 1
    learning_store::Add(L"明天", L"昨日", 5000, policy);      // hits = 1, newest
    entries = learning_store::Load(policy);
    Expect(entries.size() == 2, "eviction keeps maxPairs entries");
    Expect(!entries.empty() && entries[0].original == L"这里",
           "the most reinforced pair survives eviction");
    // Sorted by hits desc then lastMs desc: [这里(3 hits), 明天(5000)] wins the
    // last slot over 开会(4000), which is the pair that gets evicted.
    Expect(entries.size() == 2 && entries[1].original == L"明天",
           "eviction keeps the newest single-hit pair and drops the older one");

    Expect(!learning_store::Add(std::wstring(60, L'x'), L"y", 6000, policy),
           "an over-long pair is refused");

    learning_store::Clear();
    Expect(learning_store::Count(policy) == 0, "clear removes all pairs");
    DeleteFileW(path.c_str());
    learning_store::SetPathOverrideForTests(std::wstring());
}

void RunLedgerCases() {
    injection_ledger::Record record;
    injection_ledger::Invalidate();
    Expect(!injection_ledger::Snapshot(record), "empty ledger has no record");

    const uint64_t first = injection_ledger::NextSequence();
    const uint64_t second = injection_ledger::NextSequence();
    Expect(second > first, "sequences are monotonic");

    injection_ledger::Record entry;
    entry.injectedText = L"今天下雨了";
    entry.method = injection_ledger::InjectionMethod::ClipboardPaste;
    entry.targetWindow = reinterpret_cast<HWND>(0x1234);
    entry.tickMs = GetTickCount();
    entry.sensitive = true;
    const uint64_t sequence = injection_ledger::RecordInjection(std::move(entry));
    Expect(sequence != 0, "record receives a sequence");

    injection_ledger::Record observed;
    Expect(injection_ledger::Snapshot(observed), "record is readable");
    Expect(observed.injectedText == L"今天下雨了", "record keeps the injected text");
    Expect(observed.sensitive, "record keeps the sensitive flag");
    Expect(std::string(injection_ledger::MethodLogName(observed.method)) == "clipboard",
           "method log name is stable");

    injection_ledger::Consume();
    Expect(!injection_ledger::Snapshot(observed), "consumed record is not reported again");

    injection_ledger::Record second_entry;
    second_entry.injectedText = L"再来一次";
    injection_ledger::RecordInjection(std::move(second_entry));
    injection_ledger::Invalidate();
    Expect(!injection_ledger::Snapshot(observed), "invalidate drops the record");
}

// The learned words must extend the runtime lists without ever touching the
// super-hotword tier or duplicating an explicit entry.
void RunMergeCases() {
    const vocabulary_manager::VocabularyList entries = {{L"gittag", 50}, {L"VoxType", 4}};
    const std::vector<std::pair<std::wstring, int>> learned = {
        {L"gittag", 5},   // already explicit -> never duplicated
        {L"浙里办", 5},
        {L"复盘", 4},
    };

    const auto merged = vocabulary_manager::MergeLearnedEntries(entries, learned, 2);
    Expect(merged.size() == 4, "learned merge appends unseen words only");
    Expect(merged[0].word == L"gittag" && merged[0].weight == 50,
           "explicit super hotword keeps its weight");
    Expect(merged[2].word == L"浙里办" && merged[2].weight == vocabulary_manager::kLearnedVocabularyWeight,
           "a learned word merges at the lowest weight so it never outranks explicit vocabulary");
    Expect(merged[3].word == L"复盘", "learned order follows the store order");

    const auto capped = vocabulary_manager::MergeLearnedEntries(entries, learned, 1);
    Expect(capped.size() == 3, "maxExtra caps the learned tail");

    const std::vector<std::pair<std::wstring, int>> super = {{L"新词", 50}};
    const auto clamped = vocabulary_manager::MergeLearnedEntries({}, super, 4);
    Expect(clamped.size() == 1 &&
               clamped[0].weight == vocabulary_manager::kLearnedVocabularyWeight &&
               !vocabulary_manager::IsSuperHotword(clamped[0].weight),
           "a learned word never lands in the super-hotword tier");

    Expect(vocabulary_manager::MergeLearnedEntries(entries, {}, 4).size() == entries.size(),
           "no learned words leaves the list untouched");
}

} // namespace

int main() {
    RunExtractorCases();
    RunDocumentCases();
    RunStoreCases();
    RunLedgerCases();
    RunMergeCases();
    if (g_failures == 0) {
        std::cout << "correction_learning_test: all cases passed\n";
        return 0;
    }
    std::cerr << "correction_learning_test: " << g_failures << " failure(s)\n";
    return 1;
}
