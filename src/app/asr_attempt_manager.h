#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "audio_capture.h"
#include "asr_result.h"
#include "asr_session.h"
#include "asr_dispatcher.h"
#include "doubao_ime_asr.h"
#include "selection_context.h"
#include <memory>
#include <string>
#include <vector>

enum class AsrAttemptFinalSource {
    Callback,
    Watchdog,
    SessionStart,
};

const char* AsrAttemptFinalSourceName(AsrAttemptFinalSource source);

struct AsrAttemptFinalMessage {
    uint64_t attemptId = 0;
    Config primaryConfig;
    std::wstring text;
    AsrAttemptFinalSource source = AsrAttemptFinalSource::Callback;
    bool allowCancelledAttempt = false;
    bool bundledPostProcessApplied = false;
    SelectionContext selection;
};

bool IsStreamingCloudBackend(const Config& config);

void WriteDiagnosticAudioRuntimeLog(const std::string& line);
void WriteLlmLog(const std::wstring& asrText, const std::wstring& llmText);
void RefineWithLlmAsync(const AsrFinalMessage& finalMessage);

void ApplyBatchResultMetrics(const Config& config, const AsrSessionResult& result);
void PostDoubaoImeCredentialsUpdate(const doubao_ime_asr::Credentials& credentials, bool clear);
void ApplyAsrSessionSideEffects(const AsrSessionResult& result);

void RecognizeAsync(const std::vector<BYTE>& pcm, uint64_t attemptId, Config config);

uint64_t BeginAsrAttempt(const Config& config, SelectionContext selection = {});
uint64_t RecordCaptureStartFailure(const Config& config, const AudioCaptureStartFailure& failure);

uint64_t ActiveAsrAttemptId();
Config ActiveAsrAttemptConfig();
bool IsActiveAsrAttempt(uint64_t attemptId, bool allowCancelled = false);
bool ShouldAcceptFinalMessage(uint64_t attemptId, bool allowCancelled = false);
void CancelActiveAsrAttempt(uint64_t attemptId, bool invalidate);

std::unique_ptr<AsrAttemptFinalMessage> CompleteActiveAttemptRecording(
    uint64_t attemptId,
    const std::vector<BYTE>* pcm,
    double recordingMs,
    size_t pcmBytes);

void MarkActiveAttemptFinalHandled(uint64_t attemptId);
void HandleAsrAttemptFinal(AsrAttemptFinalMessage& msg);
void HandleStreamingSessionStartFailure(uint64_t attemptId, const Config& primaryConfig, std::wstring errorText);

void StreamingFinalCallback(std::wstring text, const Config& config, bool bundledPostProcessApplied, void* userData);

std::wstring* GetLastRawAsrTextPtr();

size_t GetLastPcmBytes();
void SetLastPcmBytes(size_t bytes);
