#pragma once

// Local text post-processing for final ASR transcripts (P3 of the Doubao
// pipeline plan).
//
// Two independent features, both applied to the *final* text only and only when
// the caller already decided the text is usable:
//
//   1. Punctuation mode. `keep_all` (default) never touches the text.
//      `space_replace` / `remove_trailing` mirror the official Windows IME
//      behaviour that was decompiled into evidence E-016/E-017: the exact
//      punctuation set, the "keep the trailing quote/bracket" set, the
//      digit-adjacency exceptions (1,000 / 50% / ℃) and the "digits and
//      punctuation only" bypass.
//   2. Optional CJK/ASCII spacing (official `AutoSpace`). Only the *inner*
//      boundary rule is implemented: a space is inserted between two adjacent
//      code points whose classes differ. The official leading/trailing rules
//      depend on the host text before/after the caret, which this process does
//      not read, so they are intentionally not emulated.
//
// The module is a pure function over text plus explicit options, which keeps it
// unit-testable without touching Config, Windows or the network.

#include <string>
#include <string_view>

struct Config;

namespace text_post_process {

enum class PunctMode {
    KeepAll,        // "keep_all" - default, no change
    SpaceReplace,   // "space_replace" - punctuation becomes a space
    RemoveTrailing, // "remove_trailing" - one trailing punctuation per line
};

struct Options {
    PunctMode punctMode = PunctMode::KeepAll;
    bool spacing = false;
};

// Parses the persisted config value; unknown/empty values fall back to KeepAll
// (matching the official "invalid punctuation mode, reset to keep_all").
PunctMode PunctModeFromString(std::wstring_view value);

// Stable ASCII name for runtime logs and tests.
const char* PunctModeLogName(PunctMode mode);

// Config-facing entry point used by the ASR dispatcher.
std::wstring ApplyVoicePostProcess(std::wstring text, const Config& config);

// Explicit-options entry point (unit tests, future callers).
std::wstring Apply(std::wstring_view text, const Options& options);

// Table-testable predicates and helpers. The code point sets come from the
// official predicates FUN_140775890 (punctuation) and FUN_1407757d0
// (trailing-keep punctuation). See evidence E-017 sections A1/A2/A7.
namespace code_set {

bool IsPostProcessPunctuation(char32_t cp);
bool IsTrailingKeepPunctuation(char32_t cp);
bool IsPostProcessDigit(char32_t cp);
bool IsTrimmedTrailingSpace(char32_t cp);
bool IsAutoSpaceWhitespace(char32_t cp);

// AutoSpace class: 0 none/symbol, 1 CJK, 2 ASCII letter, 3 ASCII digit.
int AutoSpaceClass(char32_t cp);

std::u32string Decode(std::wstring_view text);
std::wstring Encode(std::u32string_view text);

} // namespace code_set

namespace detail {

bool BypassesPunctuationPostProcess(const std::u32string& cps);
std::wstring ApplyPunctuationMode(const std::u32string& cps, PunctMode mode);
std::wstring ApplyInnerSpacing(const std::u32string& cps);

} // namespace detail

} // namespace text_post_process
