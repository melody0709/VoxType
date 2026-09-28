#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Shared recognition history for context enhancement.
//
// Every backend that finishes an utterance contributes its final transcript to
// this single session-wide ring buffer, so the context features of different
// providers (Qwen `input.context`, Volcano Engine `dialog_ctx`) read the same
// data instead of each keeping a private queue. Recording happens once per
// recognition inside the shared final-text path, which is what keeps the two
// consumers from double-recording or drifting apart.
//
// Storage only: text filtering (rejecting no-speech/error/watchdog strings) and
// the per-provider round budget stay with the callers, because both live in the
// ASR layer while this module is Core.
namespace asr_history {

// Deepest history any provider can ask for. Volcano Engine documents 20 rounds
// as the upper bound of its dialog context, so nothing older ever needs to be
// retained.
inline constexpr size_t kMaxRetainedRounds = 20;

// Appends a final transcript, evicting the oldest round past the cap.
void Add(std::wstring_view text);

// Chronological copy (oldest first) of the retained rounds.
std::vector<std::wstring> Snapshot();

// Number of retained rounds.
size_t Size();

// Drops every retained round.
void Clear();

} // namespace asr_history
