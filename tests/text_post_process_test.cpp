// Offline regression: local voice text cleanup (P3).
//
// The punctuation sets, the trailing-keep set and the AutoSpace classes were
// decompiled from the official Windows IME (evidence E-016/E-017). Those sets
// are the contract: a looser "iswpunct"-style predicate silently rewrites user
// text ("1,000" -> "1 000", "50%" -> "50"), so every boundary is pinned here.
//
// Maintenance rule: when a mode, a predicate range or the bypass changes, add
// the new case to the tables below instead of editing an existing expectation.

#include "text_post_process.h"

#include "config_store.h"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void Expect(bool condition, const char* description) {
    if (condition) return;
    std::cerr << "FAIL: " << description << '\n';
    ++g_failures;
}

void ExpectText(const std::wstring& actual,
                const std::wstring& expected,
                const char* description) {
    if (actual == expected) return;
    std::cerr << "FAIL: " << description << "\n  expected=";
    std::wcerr << expected;
    std::cerr << "\n  actual  =";
    std::wcerr << actual;
    std::cerr << '\n';
    ++g_failures;
}

struct PunctCase {
    char32_t cp;
    bool punctuation;
};

// Boundary table: each range start/end plus the neighbours that must stay out.
const PunctCase kPunctCases[] = {
    {0x21, true},  {0x23, true},  {0x24, false}, {0x2A, true},  {0x2B, false},
    {0x2C, true},  {0x30, false}, {0x3A, true},  {0x3D, false}, {0x40, true},
    {0x5B, true},  {0x5E, false}, {0x5F, true},  {0x60, false}, {0x7B, true},
    {0x7D, true},  {0x7E, false}, {0xA0, false}, {0xA1, true},  {0xB6, true},
    {0xBF, true},  {0xC0, false}, {0x200F, false}, {0x2010, true}, {0x2027, true},
    {0x2028, false}, {0x202F, false}, {0x2030, true}, {0x205E, true}, {0x205F, false},
    {0x3000, false}, {0x3001, true}, {0x3003, true}, {0x3004, false}, {0x3008, true},
    {0x3011, true}, {0x3012, false}, {0x3014, true}, {0x301F, true}, {0x3020, false},
    {0x30FB, true}, {0xFF01, true}, {0xFF0B, false}, {0xFF0C, true}, {0xFF1D, false},
    {0xFF1F, true}, {0xFF3E, false}, {0xFF3F, true}, {0xFF40, false}, {0xFF5F, true},
    {0xFF65, true}, {0xFF66, false},
};

struct KeepCase {
    char32_t cp;
    bool keep;
};

// Trailing-keep table: quotes and paired brackets must survive both modes.
const KeepCase kKeepCases[] = {
    {0x22, true},  {0x27, true},  {0x28, true},  {0x29, true},  {0x7B, true},
    {0x7D, true},  {0x7C, false}, {0xAB, true},  {0xBB, true},  {0x300A, true},
    {0x300B, true}, {0x3001, false}, {0x2018, true}, {0x2019, true}, {0x201C, true},
    {0x201D, true}, {0x201E, false}, {0x2039, true}, {0x203A, true}, {0x2045, true},
    {0x2046, true}, {0xFF02, true}, {0xFF01, false}, {0xFF5B, true}, {0xFF5D, true},
    {0xFF62, true}, {0xFF63, true}, {0xFF60, true},
};

struct ClassCase {
    char32_t cp;
    int cls;
};

const ClassCase kClassCases[] = {
    {0x3400, 1},   {0x4DBF, 1},   {0x4DC0, 0},   {0x4E00, 1},   {0x9FFF, 1},
    {0xA000, 0},   {0xF900, 1},   {0xF9FF, 1},   {0xFA00, 0},   {0x20000, 1},
    {0x2FA1F, 1},  {0x2FA20, 0},  {0x30, 3},     {0x39, 3},     {0x2F, 0},
    {0x41, 2},     {0x5A, 2},     {0x61, 2},     {0x7A, 2},     {0x7B, 0},
    {0xE9, 0},     {0xFF21, 0},   {0xFF10, 0},   {0x4E2D, 1},
};

struct TextCase {
    const wchar_t* input;
    text_post_process::Options options;
    const wchar_t* expected;
};

const text_post_process::Options kKeepAll{};
const text_post_process::Options kSpaceReplace{text_post_process::PunctMode::SpaceReplace, false};
const text_post_process::Options kRemoveTrailing{text_post_process::PunctMode::RemoveTrailing, false};
const text_post_process::Options kSpacingOnly{text_post_process::PunctMode::KeepAll, true};
const text_post_process::Options kSpaceReplaceAndSpacing{text_post_process::PunctMode::SpaceReplace, true};

const TextCase kTextCases[] = {
    // keep_all is a no-op, with or without spacing off.
    {L"你好，世界。", kKeepAll, L"你好，世界。"},
    {L"", kKeepAll, L""},

    // space_replace: punctuation becomes a space, numbers are protected.
    {L"你好，世界", kSpaceReplace, L"你好 世界"},
    {L"你好。", kSpaceReplace, L"你好"},
    {L"1,000", kSpaceReplace, L"1,000"},
    {L"3.14", kSpaceReplace, L"3.14"},
    {L"50%", kSpaceReplace, L"50%"},
    {L"温度25℃。", kSpaceReplace, L"温度25℃"},
    {L"他说《好》", kSpaceReplace, L"他说《好》"},
    {L"a.b", kSpaceReplace, L"a b"},
    {L"你好！！", kSpaceReplace, L"你好 "},
    {L"１，０００", kSpaceReplace, L"１，０００"},

    // remove_trailing: exactly one trailing punctuation per line.
    {L"你好。\n世界！", kRemoveTrailing, L"你好\n世界"},
    {L"你好。\r\n世界", kRemoveTrailing, L"你好\r\n世界"},
    {L"你好。\n", kRemoveTrailing, L"你好\n"},
    {L"50%", kRemoveTrailing, L"50%"},
    {L"测试.", kRemoveTrailing, L"测试"},
    {L"他说：“好”", kRemoveTrailing, L"他说：“好”"},
    {L"你好。。", kRemoveTrailing, L"你好。"},

    // The digits + punctuation bypass survives every mode.
    {L"100%!", kSpaceReplace, L"100%"},

    // Inner spacing only; the host-text leading/trailing rules are out of scope.
    {L"测试A", kSpacingOnly, L"测试 A"},
    {L"A1", kSpacingOnly, L"A1"},
    {L"中文123", kSpacingOnly, L"中文 123"},
    {L"hello 世界", kSpacingOnly, L"hello 世界"},
    {L"中文。A", kSpacingOnly, L"中文。A"},
    {L"测试\U00020000A", kSpacingOnly, L"测试\U00020000 A"},

    // Punctuation first, spacing second (official pipeline order).
    {L"中文。A", kSpaceReplaceAndSpacing, L"中文 A"},
    {L"中文，ABC", kSpaceReplaceAndSpacing, L"中文 ABC"},
};

void RunPredicateTables() {
    for (const PunctCase& c : kPunctCases) {
        const bool actual = text_post_process::code_set::IsPostProcessPunctuation(c.cp);
        if (actual == c.punctuation) continue;
        std::cerr << "FAIL: IsPostProcessPunctuation(U+" << std::hex << static_cast<unsigned>(c.cp)
                  << ") expected=" << (c.punctuation ? 1 : 0) << " actual=" << (actual ? 1 : 0) << std::dec
                  << '\n';
        ++g_failures;
    }
    for (const KeepCase& c : kKeepCases) {
        const bool actual = text_post_process::code_set::IsTrailingKeepPunctuation(c.cp);
        if (actual == c.keep) continue;
        std::cerr << "FAIL: IsTrailingKeepPunctuation(U+" << std::hex << static_cast<unsigned>(c.cp)
                  << ") expected=" << (c.keep ? 1 : 0) << " actual=" << (actual ? 1 : 0) << std::dec
                  << '\n';
        ++g_failures;
    }
    for (const ClassCase& c : kClassCases) {
        const int actual = text_post_process::code_set::AutoSpaceClass(c.cp);
        if (actual == c.cls) continue;
        std::cerr << "FAIL: AutoSpaceClass(U+" << std::hex << static_cast<unsigned>(c.cp) << ") expected=" << std::dec
                  << c.cls << " actual=" << actual << '\n';
        ++g_failures;
    }
    Expect(!text_post_process::code_set::IsAutoSpaceWhitespace(0x0B), "0x0B is not an AutoSpace whitespace");
    Expect(text_post_process::code_set::IsAutoSpaceWhitespace(0x3000), "0x3000 is an AutoSpace whitespace");
    Expect(text_post_process::code_set::IsTrimmedTrailingSpace(0xA0), "0xA0 is trimmed at the tail");
    Expect(!text_post_process::code_set::IsTrimmedTrailingSpace(0x0A), "0x0A is not trimmed at the tail");
}

void RunTextTables() {
    for (const TextCase& c : kTextCases) {
        ExpectText(text_post_process::Apply(c.input, c.options), c.expected, "Apply() table case");
    }
}

// The shared text path runs for every ASR backend, so the default configuration
// must be a byte-for-byte no-op. This is the guard against a future default flip.
void RunDefaultConfigIsInert() {
    Config fresh;
    const wchar_t* samples[] = {
        L"开放时间，早上 9 点至下午 5 点。",
        L"1,000 元的 50% 是 500 元",
        L"温度25℃。",
        L"hello 世界 ABC 123",
        L"",
        L"第一行。\n第二行！\n",
        L"他说：“好”",
    };
    Expect(fresh.voicePunctMode == L"keep_all" && !fresh.voiceSpacingEnabled,
           "defaults stay keep_all + spacing off");
    // The switches that make the app read the focused document (and, for context
    // upload / learned hotwords, send document-derived text to a service) must
    // ship off: enabling them is the user's explicit choice.
    Expect(!fresh.asrLearningEnabled && !fresh.asrUndoEnabled,
           "learning and undo capture ship disabled");
    Expect(!fresh.doubaoContextEnabled, "doubao session context upload ships disabled");
    for (const wchar_t* sample : samples) {
        const std::wstring input(sample);
        if (text_post_process::ApplyVoicePostProcess(input, fresh) == input) continue;
        std::cerr << "FAIL: default config modified a transcript\n";
        ++g_failures;
    }
    Expect(text_post_process::ApplyVoicePostProcess(L"你好，世界。", fresh) == L"你好，世界。",
           "default config never rewrites punctuation");
}

void RunModeParsing() {
    Expect(text_post_process::PunctModeFromString(L"keep_all") == text_post_process::PunctMode::KeepAll,
           "keep_all parses");
    Expect(text_post_process::PunctModeFromString(L"space_replace") == text_post_process::PunctMode::SpaceReplace,
           "space_replace parses");
    Expect(text_post_process::PunctModeFromString(L"remove_trailing") == text_post_process::PunctMode::RemoveTrailing,
           "remove_trailing parses");
    Expect(text_post_process::PunctModeFromString(L"nonsense") == text_post_process::PunctMode::KeepAll,
           "invalid mode falls back to keep_all");
    Expect(text_post_process::PunctModeFromString(L"") == text_post_process::PunctMode::KeepAll,
           "empty mode falls back to keep_all");
    Expect(std::string(text_post_process::PunctModeLogName(text_post_process::PunctMode::RemoveTrailing)) ==
               "remove_trailing",
           "log name matches the persisted value");
}

} // namespace

int main() {
    RunPredicateTables();
    RunTextTables();
    RunDefaultConfigIsInert();
    RunModeParsing();
    if (g_failures == 0) {
        std::cout << "text_post_process_test: all cases passed\n";
        return 0;
    }
    std::cerr << "text_post_process_test: " << g_failures << " failure(s)\n";
    return 1;
}
