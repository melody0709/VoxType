#include "asr_dispatcher.h"

#include "asr_context.h"
#include "asr_diagnostics.h"
#include "asr_history.h"
#include "asr_result.h"
#include "asr_runtime_log.h"
#include "input_context.h"
#include "app_state.h"
#include "app_messages.h"

#include <utility>

void DispatchAsrFinalText(HWND targetWindow,
                          std::wstring text,
                          const Config& config,
                          AsrLlmRefineFn refineFn,
                          std::wstring* lastRawAsrText,
                          const AsrFinalMetadata& metadata) {
    text = NormalizeAsrText(std::move(text));
    // Single recording point for the shared recognition history. Every backend
    // funnels its final transcript through here, so recording anywhere else
    // would double-count (Volcano Engine used to keep a private queue). The
    // shared policy keeps no-speech placeholders and operational prefixes out,
    // and excludes a recording that started in a sensitive control — at WRITE
    // time, because this history can be uploaded later by any context consumer
    // (Qwen multi-turn, Volcano Engine dialog_ctx) once the user enables it.
    //
    // The focus answer is fail-closed: only an explicit "safe" probe permits
    // recording, and an unverified focus is reported as reason=focus_unknown so
    // a missing history entry can be explained from the log.
    const bool sensitiveFocus = input_context::MustSkipHistoryForFocus(config.asrSensitiveProbe);
    const bool usableText = IsUsableAsrTextForContext(text);
    if (asr_context::ShouldRecordHistory(usableText, sensitiveFocus)) {
        asr_history::Add(text);
    } else if (usableText && sensitiveFocus) {
        const int answer = input_context::SensitiveFocusAnswer(config.asrSensitiveProbe);
        asr_runtime_log::Write("event=asr_history_skipped reason=%s answer=%s",
                               answer == input_context::kSensitiveProbeSensitive
                                   ? "sensitive_focus"
                                   : "focus_unknown",
                               input_context::SensitiveFocusAnswerName(answer));
    }
    if (metadata.attemptId != 0) {
        audio_diagnostics::FinalizeAttempt(
            metadata.attemptId, asr_diagnostics::FinalFromText(text));
    }
    const bool needLlm = !metadata.bundledPostProcessApplied &&
                         refineFn && ShouldRunLlmRefine(config, text);

    auto* msg = new AsrFinalMessage;
    msg->attemptId = metadata.attemptId;
    msg->allowCancelledAttempt = metadata.allowCancelledAttempt;
    msg->bundledPostProcessApplied = metadata.bundledPostProcessApplied;
    msg->text = text;
    msg->resultConfig = config;
    msg->usedFallback = metadata.usedFallback;
    msg->primaryBackend = metadata.primaryBackend;
    msg->primaryError = metadata.primaryError;
    msg->fallbackBackend = metadata.fallbackBackend;
    msg->selection = metadata.selection;

    if (needLlm) {
        SetLastRawAsrText(text);
        if (lastRawAsrText) {
            *lastRawAsrText = text;
        }
        AsrFinalMessage llmInput = *msg;
        if (!PostMessageW(targetWindow, kAsrResultMessage, 1,
                          reinterpret_cast<LPARAM>(msg))) {
            delete msg;
            return;
        }
        refineFn(llmInput);
        return;
    }

    if (!PostMessageW(targetWindow, kAsrResultMessage, 0,
                      reinterpret_cast<LPARAM>(msg))) {
        delete msg;
    }
}
