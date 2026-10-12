#pragma once

#include "doubao_ime_asr.h"
#include "config_store.h"
#include "asr_history.h"
#include "doubao_ime_context.h"

inline doubao_ime_asr::DoubaoImeConfig BuildDoubaoImeConfigFromConfig(const Config& config) {
    doubao_ime_asr::DoubaoImeConfig dcfg;
    dcfg.deviceId = config.doubaoImeDeviceId;
    dcfg.cdid = config.doubaoImeCdid;
    dcfg.token = config.doubaoImeToken;
    dcfg.sampleRate = 16000;
    dcfg.channels = 1;
    dcfg.frameMs = 20;
    dcfg.enablePunctuation = true;
    dcfg.diagnosticAttemptId = config.asrAttemptId;
    dcfg.diagnosticStageKind = config.asrDiagnosticStageKind;
    dcfg.diagnosticStageIndex = config.asrDiagnosticStageIndex;
    if (config.doubaoContextEnabled) {
        // Opt-in context enhancement (P1). The history is already write-time
        // filtered for sensitive focus, so nothing is re-checked here.
        dcfg.contextBase64 = doubao_ime_context::BuildAsrContextBase64(
            asr_history::Snapshot(), config);
    }
    return dcfg;
}
