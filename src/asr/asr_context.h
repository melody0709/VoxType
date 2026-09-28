#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Context-enhancement assembly shared by the cloud backends.
//
// The provider budgets are documented per turn (Qwen: at most 5 turns of 400
// characters each), and a dictation tool cares about the text closest to the
// caret, so every turn is trimmed from the tail before it is sent. Trimming
// here also means the provider never has to truncate, which would otherwise
// drop exactly the words that matter.
namespace asr_context {

// The provider retains at most five context messages per request, so the focused
// field turn (when it carries text) must reserve one of them.
inline constexpr size_t kMaxContextTurns = 5;

// Turns capped like the field turn, with the oldest turns dropped first so that
// the most recent rounds survive the round budget.
std::vector<std::wstring> BuildHistoryTurns(
    const std::vector<std::wstring>& chronologicalHistory,
    size_t maxRounds,
    size_t maxCharacters);

// Normalizes every turn (trim + tail cap) and keeps only the newest maxTurns,
// because the provider silently ignores everything older than its message
// window. Used by both request builders as a second line of defence.
std::vector<std::wstring> ClampTurns(const std::vector<std::wstring>& turns,
                                     size_t maxTurns,
                                     size_t maxCharacters);

// Trims and tail-caps a single context turn; empty when it carries no text.
std::wstring NormalizeTurn(const std::wstring& text, size_t maxCharacters);

// Single policy point for the shared recognition history, which every
// context-enhancement consumer reads and which can be uploaded to the cloud:
// usable text is recorded only when the recording did not start in a sensitive
// control (password field). `usableText` is the caller's classification
// (IsUsableAsrTextForContext) so this module stays free of the classifier's
// link dependencies.
//
// The check has to live at write time, not at request time — otherwise a
// transcript dictated while every context switch was off would still be sitting
// in the history when the user turns one on later, or when a different backend
// reads the same history.
bool ShouldRecordHistory(bool usableText, bool sensitiveFocus);

} // namespace asr_context
