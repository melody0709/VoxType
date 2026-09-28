#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <string_view>
#include <vector>
#include <expected>
#include <cstdint>

namespace vocabulary_manager {

// The shared vocabulary uses the provider-documented ordinary scale plus one
// "super" marker:
//   * 1..5   ordinary preference; Qwen documents 4 as the recommended start.
//   * 50     super hotword (Qwen-specific); at most 50 per request.
// Every other provider maps this scale into its own numeric space, so the
// default must stay on the ordinary end: a bare word must never silently
// consume one of the 50 super slots.
inline constexpr int kOrdinaryWeightDefault = 4;
inline constexpr int kSuperHotwordWeight = 50;
inline constexpr size_t kQwenMaxSuperHotwords = 50;
inline constexpr size_t kMaxVocabularyEntries = 2000;

struct VocabularyEntry {
    std::wstring word;
    int weight = kOrdinaryWeightDefault;
};

using VocabularyList = std::vector<VocabularyEntry>;

// True when the entry must be sent as a Qwen super hotword (weight 50).
// Single source of truth: the Qwen transpiler, the LLM section order and the
// Vocabulary tab status line all read this predicate.
bool IsSuperHotword(int weight);

// Per-provider weight mapping. The shared weight is the source of truth and
// every mapping must stay inside the provider's documented value space.
// Volcano Engine has no mapping on purpose: its big-model API documents inline
// hotwords as `{"word": "..."}` and states that the big model has no weight
// concept, so there the weight only decides the list order.
int WeightToQwen(int weight);
float WeightToSherpaScore(int weight);

// Validation
bool IsCommentKey(std::wstring_view key);
bool IsValidTerm(std::wstring_view term);
bool ValidateVocabulary(std::wstring_view text, std::wstring* error = nullptr);

// Parsing & Serialization
std::expected<VocabularyList, std::wstring> ParseVocabularyJson(std::wstring_view jsonStr);
std::expected<VocabularyList, std::wstring> ParseVocabularyLines(std::wstring_view text);
std::expected<VocabularyList, std::wstring> ParseVocabularyText(std::wstring_view text);

std::string FormatVocabularyJson(const VocabularyList& entries, bool pretty = true);

// Qwen immediate vocabulary. Entries are ordered by weight (descending, file
// order preserved on ties) before the 50-super / 2000-entry caps apply, so a
// high-weight word can no longer be demoted or dropped just because it was
// written at the end of the file.
std::string TranspileToQwenJson(const VocabularyList& entries);

// Volcano Engine corpus.context. `history` is passed in chronological order
// (oldest first) and emitted newest-first, which is what the provider
// documents for `context_data`; the focused input field text is the current
// turn and therefore stays first. Inline hotwords carry no weight field (the
// provider denies a weight concept) and are emitted weight-ordered instead: the
// server keeps the front of the list and drops the tail once the mode's token
// budget is exceeded, so the ordering is what protects the important words.
std::wstring BuildVolcengineContextJson(
    const VocabularyList& entries,
    const std::wstring& inputFieldText = L"",
    const std::vector<std::wstring>& history = {});

// Renders the vocabulary as a plain word list for context enhancement, keeping
// the highest-weight terms first and never exceeding maxCharacters.
std::wstring BuildVocabularyWordList(const VocabularyList& entries,
                                     size_t maxCharacters);

// Not wired into the local engine yet (kept for the offline hotwords.txt path).
std::string TranspileToSherpaHotwords(const VocabularyList& entries);

// File I/O
std::wstring GetVocabularyFilePath();
bool EnsureVocabularyFileTemplate(const std::wstring& path = L"");
std::expected<std::wstring, std::wstring> ReadVocabularyFile(const std::wstring& path = L"");
std::expected<void, std::wstring> WriteVocabularyFile(const std::wstring& content, const std::wstring& path = L"");

// Upper bound on the entries sent to the LLM. The vocabulary is user data of
// unbounded size, so it cannot be appended to every request in full.
constexpr size_t kLlmVocabularyMaxEntries = 200;

// Renders the vocabulary as the LLM system 【用户词表】 section, highest weight
// first, or an empty string when there is nothing to protect.
//
// The section only protects spellings that already appear in the transcript: it
// must never make the model rewrite a recognised variant into a vocabulary word
// ("git tag" must not become "gittag").
std::wstring BuildLlmVocabularySection(const VocabularyList& entries);

// Convenience Accessors
VocabularyList GetEffectiveVocabularyEntries(const std::wstring& fallbackConfigVocab = L"");
std::wstring GetEffectiveQwenVocabulary(const std::wstring& fallbackConfigVocab = L"");

} // namespace vocabulary_manager
