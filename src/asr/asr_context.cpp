#include "asr_context.h"

#include "input_context.h"
#include "utils.h"

#include <algorithm>

namespace asr_context {

std::wstring NormalizeTurn(const std::wstring& text, size_t maxCharacters) {
    std::wstring trimmed = Trim(text);
    if (trimmed.empty()) return {};
    if (trimmed.size() <= maxCharacters) return trimmed;
    return input_context::TakeLastN(trimmed, maxCharacters);
}

std::vector<std::wstring> BuildHistoryTurns(
    const std::vector<std::wstring>& chronologicalHistory,
    size_t maxRounds,
    size_t maxCharacters) {
    std::vector<std::wstring> turns;
    if (maxRounds == 0 || chronologicalHistory.empty()) return turns;

    const size_t first = chronologicalHistory.size() > maxRounds
        ? chronologicalHistory.size() - maxRounds
        : 0;
    turns.reserve(chronologicalHistory.size() - first);
    for (size_t i = first; i < chronologicalHistory.size(); ++i) {
        std::wstring turn = NormalizeTurn(chronologicalHistory[i], maxCharacters);
        if (turn.empty()) continue;
        turns.push_back(std::move(turn));
    }
    return turns;
}

std::vector<std::wstring> ClampTurns(const std::vector<std::wstring>& turns,
                                     size_t maxTurns,
                                     size_t maxCharacters) {
    std::vector<std::wstring> normalized;
    if (maxTurns == 0) return normalized;
    normalized.reserve((std::min)(turns.size(), maxTurns));
    // Walk from the newest turn backwards so the window is filled with the most
    // recent turns, then restore the oldest-first order the provider expects.
    for (size_t i = turns.size(); i-- > 0 && normalized.size() < maxTurns;) {
        std::wstring turn = NormalizeTurn(turns[i], maxCharacters);
        if (turn.empty()) continue;
        normalized.push_back(std::move(turn));
    }
    std::reverse(normalized.begin(), normalized.end());
    return normalized;
}

bool ShouldRecordHistory(bool usableText, bool sensitiveFocus) {
    return usableText && !sensitiveFocus;
}

} // namespace asr_context
