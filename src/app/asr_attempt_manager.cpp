#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "asr_attempt_manager.h"
#include "recording_session_controller.h"
#include "path_service.h"
#include "audio_diagnostics.h"
#include "asr_diagnostics.h"
#include "asr_runtime_log.h"
#include "cloud_asr_common.h"
#include "asr_dispatcher.h"
#include "doubao_ime_asr.h"
#include "doubao_ime_streaming_session.h"
#include "qwen_streaming_session.h"
#include "qwen_audio_streaming_session.h"
#include "qwen_free_streaming_session.h"
#include "qwen_audio_profile.h"
#include "qwen_context.h"
#include "llm_refine.h"
#include "volcengine_streaming_session.h"
#include "volcengine_asr.h"
#include "hud.h"
#include "app_state.h"
#include "app_messages.h"
#include "asr_metrics.h"
#include "engine_local.h"

#include <fstream>
#include <sstream>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <utility>
#include <vector>
#include <cstdint>
#include <cstddef>

static std::atomic<size_t> s_lastPcmBytes{0};

std::wstring* GetLastRawAsrTextPtr() {
    return nullptr;
}

size_t GetLastPcmBytes() {
    return s_lastPcmBytes.load(std::memory_order_relaxed);
}

void SetLastPcmBytes(size_t bytes) {
    s_lastPcmBytes.store(bytes, std::memory_order_relaxed);
}

bool IsStreamingCloudBackend(const Config& config) {
    if (config.asrBackend == L"qwen" &&
        config.qwenModel == L"qwen-audio-3.0-asr-flash") {
        return false;
    }
    return config.asrBackend == L"qwen" || config.asrBackend == L"volcengine" ||
           config.asrBackend == L"doubao_ime" || config.asrBackend == L"qwen_free";
}

void WriteDiagnosticAudioRuntimeLog(const std::string& line) {
    asr_runtime_log::Write("%s", line.c_str());
}

void WriteLlmLog(const std::wstring& asrText, const std::wstring& llmText) {
    const std::wstring logDir = LogDir();

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t dateStr[16];
    swprintf_s(dateStr, L"%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
    std::wstring logPath = logDir + L"\\llm_refine_" + dateStr + L".log";

    wchar_t timeStr[32];
    swprintf_s(timeStr, L"[%04d-%02d-%02d %02d:%02d:%02d]", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::ofstream file(logPath, std::ios::app | std::ios::binary);
    if (!file) return;
    std::string ts = llm::WideToUtf8(timeStr);
    std::string asr = llm::WideToUtf8(asrText);
    std::string llmS = llm::WideToUtf8(llmText);
    file << ts << "\n"
         << "[ASR]  " << asr << "\n"
         << "[LLM]  " << llmS << "\n"
         << "---\n";
    file.flush();
}

void RefineWithLlmAsync(const AsrFinalMessage& finalMessage) {
    const std::wstring asrText = finalMessage.text;
    const Config config = finalMessage.resultConfig;
    llm::RequestConfig cfg;
    cfg.endpoint = config.llmEndpoint;
    cfg.apiKey = config.llmApiKey;
    cfg.model = config.llmModel;
    cfg.systemPrompt = config.llmPrompt;
    cfg.extraParams = config.llmExtraParams;
    bool debug = config.enableLlmDebug;
    std::thread([asrText, cfg, debug, finalMessage]() {
        HiResTimer tLlm;
        llm::RefineResult refined = llm::Refine(asrText, cfg);
        g_llmMs = tLlm.ElapsedMs();
        if (debug) {
            WriteLlmLog(asrText, refined.rawLlmText);
        }
        auto* msg = new LlmFinalMessage;
        msg->attemptId = finalMessage.attemptId;
        msg->allowCancelledAttempt = finalMessage.allowCancelledAttempt;
        msg->bundledPostProcessApplied = finalMessage.bundledPostProcessApplied;
        msg->text = std::move(refined.text);
        msg->rawAsrText = asrText;
        msg->resultConfig = finalMessage.resultConfig;
        msg->usedFallback = finalMessage.usedFallback;
        msg->primaryBackend = finalMessage.primaryBackend;
        msg->primaryError = finalMessage.primaryError;
        msg->fallbackBackend = finalMessage.fallbackBackend;
        msg->selection = finalMessage.selection;
        if (!PostMessageW(g_mainWindow, kLlmResultMessage, 0, reinterpret_cast<LPARAM>(msg))) {
            delete msg;
        }
    }).detach();
}

void ApplyBatchResultMetrics(const Config& config, const AsrSessionResult& result) {
    s_lastPcmBytes = result.pcmBytes;
    if (result.isStreaming ||
        result.backend == AsrSessionBackend::BaiduBatch ||
        result.backend == AsrSessionBackend::QwenRealtimeBatch ||
        result.backend == AsrSessionBackend::QwenAudioBatch ||
        result.backend == AsrSessionBackend::MimoBatch ||
        result.backend == AsrSessionBackend::MaiBatch ||
        result.backend == AsrSessionBackend::DoubaoImeRecorded) {
        g_cloudApiMs = result.cloudApiMs;
    }
    if (config.enableDebugMode && result.vadTrimmedSamples > 0) {
        g_vadTrimmedSamples = result.vadTrimmedSamples;
        g_vadMs = result.vadMs;
        std::lock_guard<std::mutex> lk(g_vadMetricsMutex);
        g_vadModelName = result.vadModelName;
    }
}

void PostDoubaoImeCredentialsUpdate(const doubao_ime_asr::Credentials& credentials, bool clear) {
    auto* update = new doubao_ime_asr::CredentialsUpdateMessage;
    update->credentials = credentials;
    update->clear = clear;
    if (!PostMessageW(g_mainWindow, kDoubaoImeCredentialsMessage, 0,
                      reinterpret_cast<LPARAM>(update))) {
        delete update;
    }
}

void ApplyAsrSessionSideEffects(const AsrSessionResult& result) {
    if (result.doubaoImeClearCredentials) {
        PostDoubaoImeCredentialsUpdate({}, true);
    }
    if (result.doubaoImeCredentialsChanged) {
        doubao_ime_asr::Credentials credentials;
        credentials.deviceId = result.doubaoImeDeviceId;
        credentials.cdid = result.doubaoImeCdid;
        credentials.token = result.doubaoImeToken;
        PostDoubaoImeCredentialsUpdate(credentials, false);
    }
}

struct OneShotStreamingResult {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool bundledPostProcessApplied = false;
    std::wstring text;
};

static void CaptureOneShotStreamingFinal(std::wstring text,
                                        const Config& /*config*/,
                                        bool bundledPostProcessApplied,
                                        void* userData) {
    auto* result = static_cast<OneShotStreamingResult*>(userData);
    if (!result) return;
    {
        std::lock_guard<std::mutex> lock(result->mutex);
        if (result->done) return;
        result->text = std::move(text);
        result->bundledPostProcessApplied = bundledPostProcessApplied;
        result->done = true;
    }
    result->cv.notify_one();
}

static std::unique_ptr<IStreamingAsrSession> CreateStreamingSessionForOneShot(
    const Config& config) {
    if (config.asrBackend == L"qwen") {
        if (config.qwenModel == L"qwen-audio-3.0-asr-flash-streaming") {
            return CreateQwenAudioStreamingSession(config, g_mainWindow, nullptr, nullptr);
        }
        return CreateQwenStreamingSession(config, g_mainWindow, nullptr, nullptr);
    }
    if (config.asrBackend == L"doubao_ime") {
        return CreateDoubaoImeStreamingSession(config, g_mainWindow, nullptr, nullptr);
    }
    if (config.asrBackend == L"qwen_free") {
        return CreateQwenFreeStreamingSession(config, g_mainWindow, nullptr, nullptr, {});
    }
    if (config.asrBackend == L"volcengine") {
        VolcengineResetForNewSession();
        return CreateVolcengineStreamingSession(config, g_mainWindow, nullptr, nullptr);
    }
    return nullptr;
}

static AsrSessionResult RunStreamingAsrOnce(const Config& config,
                                           const std::vector<BYTE>& pcm) {
    AsrSessionResult result;
    result.providerName = AsrBackendDisplayName(config);
    result.pcmBytes = pcm.size();
    result.isStreaming = true;

    OneShotStreamingResult collected;
    auto session = CreateStreamingSessionForOneShot(config);
    if (!session) {
        result.text = L"ASR failed: streaming backend is not available";
        asr_diagnostics::CompleteIfMissingFromText(config, result.text);
        return result;
    }

    session->SetFinalCallback(CaptureOneShotStreamingFinal, &collected);
    const ULONGLONG startedTick = GetTickCount64();
    std::wstring startError;
    if (!session->Start(startError)) {
        result.text = startError.empty()
            ? L"ASR failed: streaming session start failed"
            : std::move(startError);
        session->Abort();
        result.cloudApiMs = static_cast<double>(GetTickCount64() - startedTick);
        asr_diagnostics::CompleteIfMissingFromText(
            config, result.text, "session_start_failed", result.cloudApiMs);
        return result;
    }

    if (!pcm.empty() && !session->EnqueuePcmChunk(pcm.data(), pcm.size())) {
        result.text = L"ASR failed: streaming fallback audio enqueue failed";
        session->Abort();
        result.cloudApiMs = static_cast<double>(GetTickCount64() - startedTick);
        asr_diagnostics::CompleteIfMissingFromText(
            config, result.text, "audio_enqueue_failed", result.cloudApiMs);
        return result;
    }

    session->StopInput(static_cast<double>(pcm.size()) / 32.0, pcm.size());
    DWORD waitMs = session->CurrentWatchdogMs();
    waitMs = (std::max<DWORD>)(1000, (std::min<DWORD>)(waitMs, 60000));
    bool completed = false;
    {
        std::unique_lock<std::mutex> lock(collected.mutex);
        completed = collected.cv.wait_for(
            lock, std::chrono::milliseconds(waitMs),
            [&collected]() { return collected.done; });
        if (completed) {
            result.text = collected.text;
            result.bundledPostProcessApplied = collected.bundledPostProcessApplied;
        }
    }

    if (!completed) {
        result.text = L"ASR failed: streaming fallback timeout";
    }
    session->Abort();
    result.cloudApiMs = static_cast<double>(GetTickCount64() - startedTick);
    asr_diagnostics::CompleteIfMissingFromText(
        config, result.text, "provider_final", result.cloudApiMs);
    return result;
}

static AsrSessionResult RunBatchAsrOnce(const Config& config,
                                        const std::vector<BYTE>& pcm,
                                        std::vector<float>&& localStreamingVadSamples) {
    AsrSessionResult result;
    result.providerName = AsrBackendDisplayName(config);
    result.pcmBytes = pcm.size();

    auto session = CreateBatchAsrSession(config, g_asrEngine, std::move(localStreamingVadSamples));
    std::wstring startError;
    if (!session || !session->Start(startError)) {
        result.text = startError.empty() ? L"ASR failed: session start failed" : startError;
        asr_diagnostics::CompleteIfMissingFromText(
            config, result.text, "session_start_failed");
        return result;
    }

    session->EnqueuePcmChunk(pcm.data(), pcm.size());
    result = session->Finish();
    asr_diagnostics::CompleteIfMissingFromText(
        config, result.text, "provider_final", result.cloudApiMs);
    return result;
}

static AsrSessionResult RunConfiguredAsrOnce(
    const Config& config,
    const std::vector<BYTE>& pcm,
    std::vector<float>&& localStreamingVadSamples) {
    if (IsStreamingCloudBackend(config)) {
        return RunStreamingAsrOnce(config, pcm);
    }
    return RunBatchAsrOnce(config, pcm, std::move(localStreamingVadSamples));
}

static void PostFallbackHud(uint64_t attemptId, const Config& fallbackConfig) {
    auto* text = new std::wstring(L"Fallback... " + AsrBackendDisplayName(fallbackConfig));
    if (!PostMessageW(g_mainWindow, kHudUpdateMessage,
                      static_cast<WPARAM>(attemptId),
                      reinterpret_cast<LPARAM>(text))) {
        delete text;
    }
}

static unsigned long long ElapsedSinceTick(ULONGLONG startTick) {
    if (startTick == 0) return 0;
    return static_cast<unsigned long long>(GetTickCount64() - startTick);
}

static void LogAsrResultEvent(const char* event,
                              uint64_t attemptId,
                              const std::wstring& backend,
                              const AsrResultClassification& classification,
                              const char* source,
                              ULONGLONG startTick,
                              bool accepted) {
    asr_runtime_log::Write(
        "event=%s attempt=%llu backend=%s kind=%s reason=%s source=%s elapsed_ms=%llu accepted=%d",
        event,
        static_cast<unsigned long long>(attemptId),
        AsrBackendLogName(backend),
        AsrResultKindDebugName(classification.kind),
        AsrFailureReasonDebugName(classification.reason),
        source,
        ElapsedSinceTick(startTick),
        accepted ? 1 : 0);
}

void RecognizeAsync(const std::vector<BYTE>& pcm,
                    uint64_t attemptId,
                    Config config) {
    std::vector<float> localStreamingVadSamples;
    if (config.asrBackend == L"local") {
        localStreamingVadSamples = std::move(g_streamingVadSamples);
        g_streamingVadSamples.clear();
    }

    std::thread([attemptId, config, pcm, localStreamingVadSamples = std::move(localStreamingVadSamples)]() mutable {
        const ULONGLONG primaryStartTick = GetTickCount64();
        AsrSessionResult selectedResult = RunBatchAsrOnce(config, pcm, std::move(localStreamingVadSamples));
        Config resultConfig = config;
        AsrFinalMetadata metadata;
        metadata.attemptId = attemptId;

        const AsrResultClassification primaryClassification = ClassifyAsrResult(selectedResult.text);
        const bool primaryAccepted = ShouldAcceptFinalMessage(attemptId);
        LogAsrResultEvent("primary_final", attemptId, config.asrBackend,
                          primaryClassification, "batch", primaryStartTick, primaryAccepted);
        if (!primaryAccepted) {
            asr_runtime_log::Write("event=primary_discarded_stale attempt=%llu source=batch",
                                   static_cast<unsigned long long>(attemptId));
            return;
        }

        if (ShouldRunFallback(config, selectedResult.text, false, false)) {
            Config fallbackConfig = BuildFallbackConfig(config);
            asr_runtime_log::Write(
                "event=fallback_start attempt=%llu primary=%s fallback=%s reason=%s pcm_bytes=%zu",
                static_cast<unsigned long long>(attemptId),
                AsrBackendLogName(config.asrBackend),
                AsrBackendLogName(fallbackConfig.asrBackend),
                AsrFailureReasonDebugName(primaryClassification.reason),
                pcm.size());
            PostFallbackHud(attemptId, fallbackConfig);
            const ULONGLONG fallbackStartTick = GetTickCount64();
            AsrSessionResult fallbackResult = RunConfiguredAsrOnce(fallbackConfig, pcm, {});
            metadata.usedFallback = true;
            metadata.primaryBackend = config.asrBackend;
            metadata.primaryError = NormalizeAsrText(selectedResult.text);
            metadata.fallbackBackend = fallbackConfig.asrBackend;
            resultConfig = fallbackConfig;

            const AsrResultClassification fallbackClassification = ClassifyAsrResult(fallbackResult.text);
            const bool fallbackAccepted = ShouldAcceptFinalMessage(attemptId);
            LogAsrResultEvent("fallback_final", attemptId, fallbackConfig.asrBackend,
                              fallbackClassification, "batch", fallbackStartTick, fallbackAccepted);
            if (!fallbackAccepted) {
                asr_runtime_log::Write("event=fallback_discarded_stale attempt=%llu stage=completed",
                                       static_cast<unsigned long long>(attemptId));
                return;
            }

            if (fallbackClassification.kind == AsrResultKind::OperationalError) {
                selectedResult = fallbackResult;
                selectedResult.text = L"Fallback failed: " + AsrBackendDisplayName(fallbackConfig);
            } else {
                selectedResult = std::move(fallbackResult);
            }
        } else if (IsFallbackAsrEnabled(config) &&
                   primaryClassification.kind == AsrResultKind::OperationalError) {
            asr_runtime_log::Write(
                "event=fallback_suppressed attempt=%llu reason=non_retryable_error",
                static_cast<unsigned long long>(attemptId));
        }

        if (!ShouldAcceptFinalMessage(attemptId)) {
            asr_runtime_log::Write("event=final_dispatch_discarded_stale attempt=%llu",
                                   static_cast<unsigned long long>(attemptId));
            return;
        }
        ApplyAsrSessionSideEffects(selectedResult);
        ApplyBatchResultMetrics(resultConfig, selectedResult);
        DispatchAsrFinalText(g_mainWindow, selectedResult.text, resultConfig,
                             RefineWithLlmAsync, GetLastRawAsrTextPtr(), metadata);
    }).detach();
}

const char* AsrAttemptFinalSourceName(AsrAttemptFinalSource source) {
    switch (source) {
    case AsrAttemptFinalSource::Callback: return "callback";
    case AsrAttemptFinalSource::Watchdog: return "watchdog";
    case AsrAttemptFinalSource::SessionStart: return "session_start";
    }
    return "unknown";
}

struct RecognitionAttemptContext {
    uint64_t id = 0;
    Config primaryConfig;
    std::shared_ptr<const std::vector<BYTE>> pcm;
    ULONGLONG startedTick = 0;
    ULONGLONG stoppedTick = 0;
    bool recordingStopped = false;
    bool finalHandled = false;
    bool cancelled = false;
    bool invalidated = false;
    bool fallbackStarted = false;
    bool hasDeferredFinal = false;
    std::wstring deferredFinalText;
    Config deferredFinalConfig;
    AsrAttemptFinalSource deferredFinalSource = AsrAttemptFinalSource::Callback;
    bool deferredAllowCancelledAttempt = false;
    bool deferredBundledPostProcessApplied = false;
    SelectionContext selection;
};

static std::atomic<uint64_t> g_asrAttemptSeq{0};
static std::mutex g_asrAttemptMutex;
static RecognitionAttemptContext g_activeAttempt;

uint64_t BeginAsrAttempt(const Config& config, SelectionContext selection) {
    const uint64_t attemptId = ++g_asrAttemptSeq;
    Config attemptConfig = config;
    attemptConfig.asrAttemptId = attemptId;
    if (config.asrBackend == L"qwen" && config.qwenEnableInputContext &&
        (config.qwenModel == qwen_audio_profile::kHttpModel ||
         config.qwenModel == qwen_audio_profile::kStreamingModel)) {
        attemptConfig.qwenInputContextSnapshotCaptured = true;
        {
            std::lock_guard<std::mutex> lk(g_inputContextMutex);
            attemptConfig.qwenInputContextSnapshot =
                qwen_context::CaptureInputFieldText(&g_inputContextResult);
            asr_runtime_log::Write(
                "event=input_context_snapshot captured=%d chars=%zu layer=%d timed_out=%d password=%d",
                attemptConfig.qwenInputContextSnapshot.empty() ? 0 : 1,
                attemptConfig.qwenInputContextSnapshot.size(),
                g_inputContextResult.successLayer,
                g_inputContextResult.timedOut ? 1 : 0,
                g_inputContextResult.isPassword ? 1 : 0);
        }
    }
    const audio_diagnostics::AttemptMetadata diagnosticMetadata =
        asr_diagnostics::MakeAttemptMetadata(attemptConfig);
    uint64_t supersededAttemptId = 0;
    {
        std::lock_guard<std::mutex> lock(g_asrAttemptMutex);
        supersededAttemptId = g_activeAttempt.id;
        g_activeAttempt = {};
        g_activeAttempt.id = attemptId;
        g_activeAttempt.primaryConfig = std::move(attemptConfig);
        g_activeAttempt.selection = std::move(selection);
        g_activeAttempt.startedTick = GetTickCount64();
    }
    if (supersededAttemptId != 0 && supersededAttemptId != attemptId) {
        audio_diagnostics::DiscardAttempt(supersededAttemptId, "superseded");
    }
    audio_diagnostics::BeginAttempt(diagnosticMetadata);
    asr_runtime_log::Write(
        "event=attempt_start attempt=%llu primary=%s fallback=%s",
        static_cast<unsigned long long>(attemptId),
        AsrBackendLogName(config.asrBackend),
        IsFallbackAsrEnabled(config) ? AsrBackendLogName(config.fallbackAsrBackend) : "none");
    return attemptId;
}

uint64_t RecordCaptureStartFailure(
    const Config& config,
    const AudioCaptureStartFailure& failure) {
    const uint64_t attemptId = ++g_asrAttemptSeq;
    Config attemptConfig = config;
    attemptConfig.asrAttemptId = attemptId;
    audio_diagnostics::BeginAttempt(
        asr_diagnostics::MakeAttemptMetadata(attemptConfig));

    audio_diagnostics::CaptureSnapshot capture;
    capture.device.backend = failure.terminalBackend.empty()
        ? config.audioBackend : failure.terminalBackend;
    capture.device.deviceName = failure.deviceName;
    capture.device.deviceId = failure.deviceId;
    capture.device.usedDefaultDevice = failure.usedDefaultDevice;
    capture.device.nativeSampleRate = failure.nativeSampleRate;
    capture.device.nativeChannels = failure.nativeChannels;
    capture.device.nativeBitsPerSample = failure.nativeBitsPerSample;
    capture.device.nativeIsFloat = failure.nativeIsFloat;
    audio_diagnostics::AttachCapture(
        attemptId,
        std::make_shared<const std::vector<BYTE>>(),
        capture);

    audio_diagnostics::StageMetadata stage;
    stage.kind = audio_diagnostics::StageKind::Primary;
    stage.backend = L"audio_capture";
    stage.transport = failure.attemptedBackends.empty()
        ? capture.device.backend : failure.attemptedBackends;
    stage.reason = L"capture_start_" +
        (failure.phase.empty() ? std::wstring(L"unknown") : failure.phase);

    audio_diagnostics::StageTerminal terminal;
    terminal.terminal = "capture_start_failure";
    terminal.reason = WideToUtf8(stage.reason);
    terminal.providerCode = std::to_string(
        static_cast<unsigned long>(failure.code));
    terminal.errorCategory = "capture";
    audio_diagnostics::CompleteStageIfMissing(attemptId, stage, terminal);

    audio_diagnostics::FinalResult finalResult;
    finalResult.kind = audio_diagnostics::FinalKind::CaptureFailure;
    finalResult.reason = terminal.reason;
    finalResult.terminal = terminal.terminal;
    audio_diagnostics::FinalizeAttempt(attemptId, finalResult);
    return attemptId;
}

uint64_t ActiveAsrAttemptId() {
    std::lock_guard<std::mutex> lock(g_asrAttemptMutex);
    return g_activeAttempt.id;
}

Config ActiveAsrAttemptConfig() {
    std::lock_guard<std::mutex> lock(g_asrAttemptMutex);
    return g_activeAttempt.primaryConfig;
}

bool IsActiveAsrAttempt(uint64_t attemptId, bool allowCancelled) {
    std::lock_guard<std::mutex> lock(g_asrAttemptMutex);
    if (attemptId == 0 || g_activeAttempt.id != attemptId ||
        g_activeAttempt.invalidated) {
        return false;
    }
    return allowCancelled || !g_activeAttempt.cancelled;
}

bool ShouldAcceptFinalMessage(uint64_t attemptId, bool allowCancelled) {
    return attemptId == 0 || IsActiveAsrAttempt(attemptId, allowCancelled);
}

void CancelActiveAsrAttempt(uint64_t attemptId, bool invalidate) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_asrAttemptMutex);
        if (attemptId != 0 && g_activeAttempt.id == attemptId) {
            g_activeAttempt.cancelled = true;
            if (invalidate) g_activeAttempt.invalidated = true;
            changed = true;
        }
    }
    if (changed) {
        asr_runtime_log::Write(
            "event=attempt_cancel attempt=%llu invalidate=%d",
            static_cast<unsigned long long>(attemptId), invalidate ? 1 : 0);
    }
}

std::unique_ptr<AsrAttemptFinalMessage> CompleteActiveAttemptRecording(
    uint64_t attemptId,
    const std::vector<BYTE>* pcm,
    double recordingMs,
    size_t pcmBytes) {
    std::unique_ptr<AsrAttemptFinalMessage> deferred;
    std::shared_ptr<const std::vector<BYTE>> capturedPcm;
    bool completed = false;
    {
        std::lock_guard<std::mutex> lock(g_asrAttemptMutex);
        if (g_activeAttempt.id == attemptId && !g_activeAttempt.recordingStopped) {
            g_activeAttempt.recordingStopped = true;
            g_activeAttempt.stoppedTick = GetTickCount64();
            if (pcm) {
                capturedPcm = std::make_shared<const std::vector<BYTE>>(*pcm);
                g_activeAttempt.pcm = capturedPcm;
            }
            if (g_activeAttempt.hasDeferredFinal) {
                deferred = std::make_unique<AsrAttemptFinalMessage>();
                deferred->attemptId = attemptId;
                deferred->primaryConfig = g_activeAttempt.deferredFinalConfig;
                deferred->text = std::move(g_activeAttempt.deferredFinalText);
                deferred->source = g_activeAttempt.deferredFinalSource;
                deferred->allowCancelledAttempt =
                    g_activeAttempt.deferredAllowCancelledAttempt;
                deferred->bundledPostProcessApplied =
                    g_activeAttempt.deferredBundledPostProcessApplied;
                deferred->selection = g_activeAttempt.selection;
                g_activeAttempt.hasDeferredFinal = false;
                g_activeAttempt.deferredAllowCancelledAttempt = false;
                g_activeAttempt.deferredBundledPostProcessApplied = false;
            }
            completed = true;
        }
    }
    if (completed) {
        const audio_diagnostics::CaptureSnapshot capture =
            audio_diagnostics::FreezeCapture(recordingMs, pcmBytes);
        audio_diagnostics::AttachCapture(attemptId, capturedPcm, capture);
        const std::string deviceId = WideToUtf8(capture.device.deviceId);
        const std::string deviceHash = deviceId.empty()
            ? std::string()
            : audio_diagnostics::Sha256Hex(
                reinterpret_cast<const BYTE*>(deviceId.data()), deviceId.size());
        asr_runtime_log::Write(
            "event=capture_summary attempt=%llu source=%s device_hash=%s default_device=%d native_rate=%lu native_channels=%u native_bits=%u native_float=%d recording_ms=%.0f native_frames=%llu output_samples=%llu pcm_bytes=%zu deferred_primary=%d rms_dbfs=%.2f peak_dbfs=%.2f zero_ratio=%.6f near_silent_ratio=%.6f clipping_ratio=%.6f silent_packets=%llu silent_frames=%llu discontinuities=%llu max_callback_gap_ms=%.1f first_non_silent_delay_ms=%.1f downmix_rms_dbfs=%.2f",
            static_cast<unsigned long long>(attemptId),
            WideToUtf8(capture.device.backend).c_str(),
            deviceHash.empty() ? "unavailable" : deviceHash.c_str(),
            capture.device.usedDefaultDevice ? 1 : 0,
            static_cast<unsigned long>(capture.device.nativeSampleRate),
            static_cast<unsigned>(capture.device.nativeChannels),
            static_cast<unsigned>(capture.device.nativeBitsPerSample),
            capture.device.nativeIsFloat ? 1 : 0,
            recordingMs,
            static_cast<unsigned long long>(capture.nativeFrames),
            static_cast<unsigned long long>(capture.outputSamples),
            pcmBytes,
            deferred ? 1 : 0,
            capture.output.rmsDbfs,
            capture.output.peakDbfs,
            capture.output.zeroRatio,
            capture.output.nearSilentRatio,
            capture.output.clippingRatio,
            static_cast<unsigned long long>(capture.silentPackets),
            static_cast<unsigned long long>(capture.silentFrames),
            static_cast<unsigned long long>(capture.discontinuities),
            capture.maxCallbackGapMs,
            capture.firstNonSilentDelayMs,
            capture.downmixRmsDbfs);
    }
    return deferred;
}

void MarkActiveAttemptFinalHandled(uint64_t attemptId) {
    std::lock_guard<std::mutex> lock(g_asrAttemptMutex);
    if (g_activeAttempt.id == attemptId) {
        g_activeAttempt.finalHandled = true;
        g_activeAttempt.cancelled = true;
        g_activeAttempt.pcm.reset();
        g_activeAttempt.hasDeferredFinal = false;
        g_activeAttempt.deferredFinalText.clear();
        g_activeAttempt.deferredFinalConfig = {};
        g_activeAttempt.deferredAllowCancelledAttempt = false;
        g_activeAttempt.deferredBundledPostProcessApplied = false;
    }
}

enum class AttemptFinalDisposition {
    Ready,
    DeferredUntilStop,
    Rejected,
};

static AttemptFinalDisposition PrepareAttemptFinal(
    const AsrAttemptFinalMessage& message,
    Config& primaryConfig,
    Config& resultConfig,
    std::shared_ptr<const std::vector<BYTE>>& pcm,
    bool& fallbackAlreadyStarted,
    ULONGLONG& elapsedStartTick,
    SelectionContext& selection) {
    std::lock_guard<std::mutex> lock(g_asrAttemptMutex);
    if (message.attemptId == 0 || g_activeAttempt.id != message.attemptId ||
        g_activeAttempt.finalHandled || g_activeAttempt.invalidated ||
        (g_activeAttempt.cancelled && !message.allowCancelledAttempt)) {
        return AttemptFinalDisposition::Rejected;
    }
    if (!g_activeAttempt.recordingStopped) {
        if (g_activeAttempt.hasDeferredFinal) {
            return AttemptFinalDisposition::Rejected;
        }
        g_activeAttempt.hasDeferredFinal = true;
        g_activeAttempt.deferredFinalText = message.text;
        g_activeAttempt.deferredFinalConfig = message.primaryConfig;
        g_activeAttempt.deferredFinalSource = message.source;
        g_activeAttempt.deferredAllowCancelledAttempt =
            message.allowCancelledAttempt;
        g_activeAttempt.deferredBundledPostProcessApplied =
            message.bundledPostProcessApplied;
        return AttemptFinalDisposition::DeferredUntilStop;
    }
    g_activeAttempt.finalHandled = true;
    primaryConfig = g_activeAttempt.primaryConfig;
    resultConfig = message.primaryConfig;
    pcm = g_activeAttempt.pcm;
    g_activeAttempt.pcm.reset();
    fallbackAlreadyStarted = g_activeAttempt.fallbackStarted;
    selection = g_activeAttempt.selection;
    elapsedStartTick = g_activeAttempt.stoppedTick != 0
        ? g_activeAttempt.stoppedTick
        : g_activeAttempt.startedTick;
    return AttemptFinalDisposition::Ready;
}

static bool TryMarkAttemptFallbackStarted(uint64_t attemptId, bool allowCancelled = false) {
    std::lock_guard<std::mutex> lock(g_asrAttemptMutex);
    if (g_activeAttempt.id == attemptId && g_activeAttempt.finalHandled &&
        !g_activeAttempt.invalidated &&
        (allowCancelled || !g_activeAttempt.cancelled) &&
        !g_activeAttempt.fallbackStarted) {
        g_activeAttempt.fallbackStarted = true;
        return true;
    }
    return false;
}

void StreamingFinalCallback(std::wstring text,
                            const Config& config,
                            bool bundledPostProcessApplied,
                            void* userData) {
    auto* msg = new AsrAttemptFinalMessage;
    msg->attemptId = static_cast<uint64_t>(reinterpret_cast<UINT_PTR>(userData));
    msg->primaryConfig = config;
    msg->text = std::move(text);
    msg->bundledPostProcessApplied = bundledPostProcessApplied;
    if (!PostMessageW(g_mainWindow, kAsrAttemptFinalMessage, 0, reinterpret_cast<LPARAM>(msg))) {
        delete msg;
    }
}

static void DispatchStreamingFallbackAsync(uint64_t attemptId,
                                           Config primaryConfig,
                                           std::shared_ptr<const std::vector<BYTE>> pcm,
                                           std::wstring primaryError,
                                           bool allowCancelledAttempt = false) {
    if (!IsActiveAsrAttempt(attemptId, allowCancelledAttempt)) {
        asr_runtime_log::Write("event=fallback_discarded_stale attempt=%llu stage=before_start",
                               static_cast<unsigned long long>(attemptId));
        return;
    }
    Config fallbackConfig = BuildFallbackConfig(primaryConfig);
    if (!TryMarkAttemptFallbackStarted(attemptId, allowCancelledAttempt)) {
        asr_runtime_log::Write("event=fallback_discarded_stale attempt=%llu stage=claim",
                               static_cast<unsigned long long>(attemptId));
        return;
    }
    const AsrResultClassification primaryClassification = ClassifyAsrResult(primaryError);
    asr_runtime_log::Write(
        "event=fallback_start attempt=%llu primary=%s fallback=%s reason=%s pcm_bytes=%zu",
        static_cast<unsigned long long>(attemptId),
        AsrBackendLogName(primaryConfig.asrBackend),
        AsrBackendLogName(fallbackConfig.asrBackend),
        AsrFailureReasonDebugName(primaryClassification.reason),
        pcm ? pcm->size() : 0);
    PostFallbackHud(attemptId, fallbackConfig);
    std::thread([attemptId, primaryConfig, fallbackConfig, pcm,
                 primaryError = std::move(primaryError),
                 allowCancelledAttempt]() {
        if (!IsActiveAsrAttempt(attemptId, allowCancelledAttempt)) {
            asr_runtime_log::Write("event=fallback_discarded_stale attempt=%llu stage=worker_start",
                                   static_cast<unsigned long long>(attemptId));
            return;
        }
        const ULONGLONG fallbackStartTick = GetTickCount64();
        AsrSessionResult selectedResult = RunConfiguredAsrOnce(fallbackConfig, *pcm, {});
        AsrFinalMetadata metadata;
        metadata.attemptId = attemptId;
        metadata.allowCancelledAttempt = allowCancelledAttempt;
        metadata.bundledPostProcessApplied =
            selectedResult.bundledPostProcessApplied;
        metadata.usedFallback = true;
        metadata.primaryBackend = primaryConfig.asrBackend;
        metadata.primaryError = NormalizeAsrText(primaryError);
        metadata.fallbackBackend = fallbackConfig.asrBackend;

        const AsrResultClassification fallbackClassification = ClassifyAsrResult(selectedResult.text);
        const bool fallbackAccepted = IsActiveAsrAttempt(
            attemptId, allowCancelledAttempt);
        LogAsrResultEvent("fallback_final", attemptId, fallbackConfig.asrBackend,
                          fallbackClassification, "streaming_fallback",
                          fallbackStartTick, fallbackAccepted);
        if (!fallbackAccepted) {
            asr_runtime_log::Write("event=fallback_discarded_stale attempt=%llu stage=completed",
                                   static_cast<unsigned long long>(attemptId));
            return;
        }

        if (fallbackClassification.kind == AsrResultKind::OperationalError) {
            selectedResult.text = L"Fallback failed: " + AsrBackendDisplayName(fallbackConfig);
        }

        if (!IsActiveAsrAttempt(attemptId, allowCancelledAttempt)) {
            asr_runtime_log::Write("event=fallback_discarded_stale attempt=%llu stage=dispatch",
                                   static_cast<unsigned long long>(attemptId));
            return;
        }
        ApplyAsrSessionSideEffects(selectedResult);
        ApplyBatchResultMetrics(fallbackConfig, selectedResult);
        DispatchAsrFinalText(g_mainWindow, selectedResult.text, fallbackConfig,
                             RefineWithLlmAsync, GetLastRawAsrTextPtr(), metadata);
    }).detach();
}

void HandleAsrAttemptFinal(AsrAttemptFinalMessage& msg) {
    Config primaryConfig = msg.primaryConfig;
    Config resultConfig = msg.primaryConfig;
    std::shared_ptr<const std::vector<BYTE>> pcm;
    bool fallbackAlreadyStarted = false;
    ULONGLONG elapsedStartTick = 0;
    SelectionContext selection;
    const bool allowCancelledAttempt = msg.allowCancelledAttempt;
    const AttemptFinalDisposition disposition = PrepareAttemptFinal(
        msg, primaryConfig, resultConfig, pcm,
        fallbackAlreadyStarted, elapsedStartTick, selection);
    if (disposition == AttemptFinalDisposition::DeferredUntilStop) {
        asr_runtime_log::Write(
            "event=primary_final_deferred attempt=%llu source=%s reason=recording_active",
            static_cast<unsigned long long>(msg.attemptId),
            AsrAttemptFinalSourceName(msg.source));
        return;
    }
    if (disposition == AttemptFinalDisposition::Rejected) {
        asr_runtime_log::Write(
            "event=primary_final_discarded attempt=%llu source=%s reason=stale_or_duplicate",
            static_cast<unsigned long long>(msg.attemptId),
            AsrAttemptFinalSourceName(msg.source));
        return;
    }

    auto finishedSession = TakeActiveStreamingSession();
    if (finishedSession) {
        finishedSession->Abort();
        finishedSession.reset();
    }

    const std::wstring primaryText = NormalizeAsrText(msg.text);
    const double primaryElapsedMs =
        static_cast<double>(ElapsedSinceTick(elapsedStartTick));
    if (msg.source == AsrAttemptFinalSource::Watchdog) {
        asr_diagnostics::CompleteFromText(
            primaryConfig, primaryText, "watchdog_final", primaryElapsedMs);
    } else {
        asr_diagnostics::CompleteIfMissingFromText(
            primaryConfig, primaryText, "provider_final", primaryElapsedMs);
    }
    const AsrResultClassification primaryClassification = ClassifyAsrResult(primaryText);
    LogAsrResultEvent("primary_final", msg.attemptId, primaryConfig.asrBackend,
                      primaryClassification, AsrAttemptFinalSourceName(msg.source),
                      elapsedStartTick, true);
    const bool selectionRewriteRequested =
        primaryConfig.asrBackend == L"qwen_free" &&
        primaryConfig.qwenFreeRewriteEnabled &&
        selection.HasCapturedSelection();
    const bool automaticQwenUtdidFallback =
        !fallbackAlreadyStarted &&
        !selectionRewriteRequested &&
        primaryConfig.asrBackend == L"qwen_free" &&
        primaryClassification.kind == AsrResultKind::OperationalError &&
        primaryText.find(L"UTDID acquisition failed") != std::wstring::npos &&
        !primaryConfig.qwenApiKey.empty();

    if (automaticQwenUtdidFallback && pcm && pcm->size() >= 8000) {
        Config automaticFallbackConfig = primaryConfig;
        automaticFallbackConfig.fallbackAsrBackend = L"qwen";
        DispatchStreamingFallbackAsync(
            msg.attemptId, automaticFallbackConfig, pcm, primaryText,
            allowCancelledAttempt);
        return;
    }
    if (automaticQwenUtdidFallback) {
        asr_runtime_log::Write(
            "event=fallback_not_started attempt=%llu fallback=qwen reason=%s pcm_bytes=%zu",
            static_cast<unsigned long long>(msg.attemptId),
            pcm ? "pcm_too_short" : "pcm_unavailable",
            pcm ? pcm->size() : 0);
    }

    const bool canFallback = !selectionRewriteRequested &&
        ShouldRunFallback(primaryConfig, primaryText, fallbackAlreadyStarted, false);
    if (selectionRewriteRequested && !automaticQwenUtdidFallback &&
        primaryClassification.kind == AsrResultKind::OperationalError) {
        asr_runtime_log::Write(
            "event=fallback_suppressed attempt=%llu reason=selection_rewrite_safety",
            static_cast<unsigned long long>(msg.attemptId));
    } else if (!canFallback && !fallbackAlreadyStarted && !selectionRewriteRequested &&
               IsFallbackAsrEnabled(primaryConfig) &&
               primaryClassification.kind == AsrResultKind::OperationalError) {
        asr_runtime_log::Write(
            "event=fallback_suppressed attempt=%llu reason=non_retryable_error",
            static_cast<unsigned long long>(msg.attemptId));
    }
    if (canFallback && pcm && pcm->size() >= 8000) {
        DispatchStreamingFallbackAsync(msg.attemptId, primaryConfig, pcm,
                                        primaryText, allowCancelledAttempt);
        return;
    }
    if (canFallback) {
        asr_runtime_log::Write(
            "event=fallback_not_started attempt=%llu fallback=%s reason=%s pcm_bytes=%zu",
            static_cast<unsigned long long>(msg.attemptId),
            AsrBackendLogName(primaryConfig.fallbackAsrBackend),
            pcm ? "pcm_too_short" : "pcm_unavailable",
            pcm ? pcm->size() : 0);
    }

    AsrFinalMetadata metadata;
    metadata.attemptId = msg.attemptId;
    metadata.allowCancelledAttempt = allowCancelledAttempt;
    metadata.bundledPostProcessApplied = msg.bundledPostProcessApplied;
    metadata.selection = std::move(selection);
    DispatchAsrFinalText(g_mainWindow, primaryText, resultConfig,
                         RefineWithLlmAsync, GetLastRawAsrTextPtr(), metadata);
}

void HandleStreamingSessionStartFailure(uint64_t attemptId,
                                       const Config& primaryConfig,
                                       std::wstring errorText) {
    const std::vector<BYTE> pcm = StopAudioCapture();
    g_recording = false;
    double recordingMs = static_cast<double>(GetTickCount64() - GetSessionStartTick());
    SetLastPcmBytes(pcm.size());
    CompleteActiveAttemptRecording(attemptId, &pcm, recordingMs, pcm.size());

    AsrAttemptFinalMessage message;
    message.attemptId = attemptId;
    message.primaryConfig = primaryConfig;
    message.text = std::move(errorText);
    message.source = AsrAttemptFinalSource::SessionStart;
    HandleAsrAttemptFinal(message);
}
