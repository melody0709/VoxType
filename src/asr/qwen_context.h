#pragma once

#include "input_context.h"

#include <string>

namespace qwen_context {

inline constexpr size_t kMaxContextCharacters = 400;

// The provider documents a per-context text budget of 400 characters.  The
// shared input-field reader already keeps the tail of the focused control;
// keep the final provider payload bounded as a second line of defence.
inline std::wstring SanitizeText(const InputContextResult& result,
                                 size_t maxCharacters = kMaxContextCharacters) {
    if (result.isPassword || result.timedOut || result.inputFieldText.empty()) {
        return {};
    }
    if (result.inputFieldText.size() <= maxCharacters) {
        return result.inputFieldText;
    }
    // Tail-first is deliberate and uniform across every context turn: the text
    // closest to the caret carries the most signal for a dictation tool, and
    // truncating here means the provider never has to drop the words that
    // matter.  (The provider's own rule truncates from the end, which would
    // remove exactly this part of the text.)
    return input_context::TakeLastN(result.inputFieldText, maxCharacters);
}

inline std::wstring CaptureInputFieldText(InputContextResult* diagnostics = nullptr) {
    InputContextResult result = input_context::GetInputFieldContext(kMaxContextCharacters);
    if (diagnostics) *diagnostics = result;
    return SanitizeText(result);
}

} // namespace qwen_context
