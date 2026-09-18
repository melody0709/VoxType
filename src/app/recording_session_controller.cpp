#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "recording_session_controller.h"
#include "asr_attempt_manager.h"
#include "hud_pagination.h"
#include "audio_capture.h"
#include "audio_chunk_sink.h"
#include "wasapi_capture.h"
#include "audio_diagnostics.h"
#include "asr_diagnostics.h"
#include "asr_runtime_log.h"
#include "cloud_asr_common.h"
#include "doubao_ime_streaming_session.h"
#include "qwen_streaming_session.h"
#include "qwen_audio_streaming_session.h"
#include "qwen_free_streaming_session.h"
#include "volcengine_streaming_session.h"
#include "volcengine_asr.h"
#include "hud.h"
#include "selection_context.h"
#include "ui_utils.h"
#include "engine_local.h"
#include "app_state.h"
#include "app_messages.h"
#include "asr_metrics.h"

#include <audioclient.h>
#include <algorithm>
#include <vector>

std::unique_ptr<IStreamingAsrSession> g_activeStreamingSession;


static ULONGLONG g_sessionStartTick = 0;
static double g_recordingMs = 0.0;
static bool s_wasapiUsed = false;
static std::wstring s_wasapiDeviceName;
static UINT32 s_wasapiNativeRate = 0;

static bool g_capturePendingOnly = false;
static bool g_stopDelayRestoreCapsLock = false;
static bool g_stopDelayPending = false;
static bool g_stopDelayHeldForRepress = false;
static bool g_captureConfigStale = false;

bool GetWasapiUsed() { return s_wasapiUsed; }
const std::wstring& GetWasapiDeviceName() { return s_wasapiDeviceName; }
UINT32 GetWasapiNativeRate() { return s_wasapiNativeRate; }
double GetRecordingMs() { return g_recordingMs; }
ULONGLONG GetSessionStartTick() { return g_sessionStartTick; }

bool IsCapturePendingOnly() { return g_capturePendingOnly; }
bool IsStopDelayPending() { return g_stopDelayPending; }
void SetStopDelayPending(bool pending) { g_stopDelayPending = pending; }
bool IsStopDelayRestoreCapsLock() { return g_stopDelayRestoreCapsLock; }
void SetStopDelayRestoreCapsLock(bool restore) { g_stopDelayRestoreCapsLock = restore; }
bool IsStopDelayHeldForRepress() { return g_stopDelayHeldForRepress; }
void SetStopDelayHeldForRepress(bool held) { g_stopDelayHeldForRepress = held; }
void SetCaptureConfigStale(bool stale) { g_captureConfigStale = stale; }

struct StreamingPartialHudCallbackContext {
    const wchar_t* statusLine = nullptr;
    uint64_t attemptId = 0;
};

static StreamingPartialHudCallbackContext g_qwenPartialHudContext{L"Listening... Qwen ASR"};
static StreamingPartialHudCallbackContext g_doubaoImePartialHudContext{L"Listening... Doubao IME"};
static StreamingPartialHudCallbackContext g_volcenginePartialHudContext{L"Listening... Volcano Engine"};
static StreamingPartialHudCallbackContext g_qwenFreePartialHudContext{L"Listening... Qwen IME (Free)"};

void StreamingPartialHudCallback(const std::wstring& text, bool, void* userData) {
    if (text.empty()) return;
    const auto* ctx = static_cast<const StreamingPartialHudCallbackContext*>(userData);
    auto* msg = new HudUpdateWithOptionsMessage;
    msg->attemptId = ctx ? ctx->attemptId : 0;
    msg->statusLine = (ctx && ctx->statusLine) ? ctx->statusLine : L"Listening...";
    msg->text = text;
    msg->maxWidthDip = kStreamingPartialHudMaxWidthDip;
    msg->maxScreenWidthFraction = kStreamingPartialHudMaxScreenFraction;
    msg->maxLines = kStreamingPartialHudMaxLines;
    msg->fixedLines = 0;
    msg->streamingPartial = true;
    if (!PostMessageW(g_mainWindow, kHudUpdateWithOptionsMessage, 0,
                      reinterpret_cast<LPARAM>(msg))) {
        delete msg;
    }
}

std::unique_ptr<IStreamingAsrSession> TakeActiveStreamingSession() {
    EnterCriticalSection(&g_streamingSessionCs);
    SetActiveAudioChunkSink(nullptr);
    auto session = std::move(g_activeStreamingSession);
    LeaveCriticalSection(&g_streamingSessionCs);
    return session;
}

bool HasActiveStreamingSession() {
    EnterCriticalSection(&g_streamingSessionCs);
    const bool hasSession = (g_activeStreamingSession != nullptr);
    LeaveCriticalSection(&g_streamingSessionCs);
    return hasSession;
}

void AbortAndResetActiveStreamingSession() {
    auto session = TakeActiveStreamingSession();
    if (session) {
        session->Abort();
    }
}

void ResetStreamingVadTrimmerState() {
    EnterCriticalSection(&g_streamingSessionCs);
    g_streamingVadTrimmer.reset();
    g_streamingVadReady = false;
    LeaveCriticalSection(&g_streamingSessionCs);
    g_vadDetectedVoice.store(false);
}

bool StartStreamingVadTrimmerForCloud(const Config& config,
                                     const wchar_t* debugPrefix,
                                     bool markReady) {
    ResetStreamingVadTrimmerState();
    if (!config.enableVad) return false;

    auto trimmer = std::make_unique<StreamingVadTrimmer>();
    std::wstring error;
    const bool ok = trimmer->Start(config, g_asrEngine, &error);
    if (!ok) {
        VolcDebugLog("%ls: VAD trim disabled (error_wlen=%zu)", debugPrefix, error.size());
        return false;
    }

    EnterCriticalSection(&g_streamingSessionCs);
    g_streamingVadTrimmer = std::move(trimmer);
    g_streamingVadReady = markReady;
    LeaveCriticalSection(&g_streamingSessionCs);
    VolcDebugLog("%ls: VAD trim active (%ls)", debugPrefix, config.vadModel.c_str());
    return true;
}

void FinishStreamingVadTrimmer() {
    EnterCriticalSection(&g_streamingSessionCs);
    if (g_streamingVadTrimmer && g_streamingVadTrimmer->IsActive()) {
        g_streamingVadTrimmer->Finish();
    }
    LeaveCriticalSection(&g_streamingSessionCs);
}

bool StreamingVadTrimSawNoSpeech() {
    EnterCriticalSection(&g_streamingSessionCs);
    const bool sawNoSpeech = g_streamingVadTrimmer &&
                             g_streamingVadTrimmer->IsActive() &&
                             !g_streamingVadTrimmer->DetectedSpeech();
    LeaveCriticalSection(&g_streamingSessionCs);
    return sawNoSpeech;
}

bool GetStreamingVadTrimStats(StreamingVadTrimStats& stats) {
    EnterCriticalSection(&g_streamingSessionCs);
    bool active = false;
    if (g_streamingVadTrimmer && g_streamingVadTrimmer->IsActive()) {
        stats = g_streamingVadTrimmer->Stats();
        active = true;
    }
    LeaveCriticalSection(&g_streamingSessionCs);
    return active;
}

void ActivateStreamingSession(std::unique_ptr<IStreamingAsrSession> session,
                             bool useVadTrimmer) {
    if (!session) return;

    EnterCriticalSection(&g_audioLock);
    EnterCriticalSection(&g_streamingSessionCs);

    if (!g_audioData.empty()) {
        if (useVadTrimmer && g_streamingVadTrimmer && g_streamingVadTrimmer->IsActive()) {
            std::vector<std::vector<BYTE>> streamingOutputs;
            g_streamingVadTrimmer->ProcessPcm16(g_audioData.data(), g_audioData.size(),
                                                streamingOutputs);
            for (const auto& chunk : streamingOutputs) {
                if (!chunk.empty() && !session->EnqueuePcmChunk(chunk.data(), chunk.size())) {
                    asr_runtime_log::Write(
                        "event=streaming_initial_audio_enqueue_failed pcm_bytes=%zu",
                        chunk.size());
                }
            }
        } else if (!session->EnqueuePcmChunk(g_audioData.data(), g_audioData.size())) {
            asr_runtime_log::Write(
                "event=streaming_initial_audio_enqueue_failed pcm_bytes=%zu",
                g_audioData.size());
        }
    }

    SetActiveAudioChunkSink(session.get());
    g_activeStreamingSession = std::move(session);
    g_streamingVadReady = useVadTrimmer && g_streamingVadTrimmer &&
                          g_streamingVadTrimmer->IsActive();
    LeaveCriticalSection(&g_streamingSessionCs);
    LeaveCriticalSection(&g_audioLock);
}

void StartStreamingWatchdog(const wchar_t* listeningText) {
    ShowHud(listeningText);
    DWORD watchdogMs = 18000;
    EnterCriticalSection(&g_streamingSessionCs);
    if (g_activeStreamingSession) {
        watchdogMs = g_activeStreamingSession->CurrentWatchdogMs();
    }
    LeaveCriticalSection(&g_streamingSessionCs);
    SetTimer(g_mainWindow, kStreamingWatchdogTimer, watchdogMs, nullptr);
}

static std::wstring AudioCaptureFailureHudText(bool wasapi, DWORD code) {
    if (wasapi && (code == static_cast<DWORD>(AUDCLNT_E_DEVICE_INVALIDATED) ||
                   code == static_cast<DWORD>(AUDCLNT_E_SERVICE_NOT_RUNNING))) {
        return L"Microphone disconnected";
    }
    return L"Microphone capture failed";
}

static void FinishAudioCaptureFailure(uint64_t generation,
                                      DWORD code,
                                      bool wasapi,
                                      uint64_t attemptId,
                                      double recordingMs,
                                      const std::vector<BYTE>& pcm) {
    g_audioCaptureFailurePending.store(false, std::memory_order_release);
    SetLastPcmBytes(pcm.size());
    CompleteActiveAttemptRecording(attemptId, &pcm, recordingMs, pcm.size());
    const Config attemptConfig = ActiveAsrAttemptConfig();
    audio_diagnostics::StageMetadata stage =
        asr_diagnostics::MakeStageMetadata(attemptConfig, L"capture_failure");
    audio_diagnostics::StageTerminal terminal;
    terminal.terminal = "capture_failure";
    terminal.reason = wasapi ? "wasapi_runtime_failure" : "wavein_runtime_failure";
    terminal.providerCode = std::to_string(static_cast<unsigned long>(code));
    audio_diagnostics::CompleteStageIfMissing(attemptId, stage, terminal);
    audio_diagnostics::FinalResult finalResult;
    finalResult.kind = audio_diagnostics::FinalKind::CaptureFailure;
    finalResult.reason = terminal.reason;
    finalResult.terminal = terminal.terminal;
    audio_diagnostics::FinalizeAttempt(attemptId, finalResult);
    CancelActiveAsrAttempt(attemptId, true);
    MarkActiveAttemptFinalHandled(attemptId);
    KillTimer(g_mainWindow, kStreamingWatchdogTimer);
    AbortAndResetActiveStreamingSession();
    ResetStreamingVadTrimmerState();

    const std::wstring hudText = AudioCaptureFailureHudText(wasapi, code);
    asr_runtime_log::Write(
        "event=capture_runtime_failed generation=%llu attempt=%llu source=%s code=0x%08lx pcm_bytes=%zu",
        static_cast<unsigned long long>(generation),
        static_cast<unsigned long long>(attemptId),
        wasapi ? "wasapi" : "wavein",
        static_cast<unsigned long>(code),
        pcm.size());
    ShowHud(hudText);
    if (g_hudWindow) SetTimer(g_hudWindow, kHudHideTimer, 2200, nullptr);
}

void HandleAudioCaptureFailure(uint64_t generation,
                               DWORD code,
                               bool wasapi) {
    const uint64_t currentGeneration =
        g_audioCaptureGeneration.load(std::memory_order_acquire);
    if (generation == 0 || generation != currentGeneration) {
        asr_runtime_log::Write(
            "event=capture_runtime_failed_stale generation=%llu current=%llu source=%s code=0x%08lx recording=%d",
            static_cast<unsigned long long>(generation),
            static_cast<unsigned long long>(currentGeneration),
            wasapi ? "wasapi" : "wavein",
            static_cast<unsigned long>(code),
            g_recording ? 1 : 0);
        return;
    }

    if (!g_recording) {
        if (g_capturePendingOnly || g_captureSuppressed.load(std::memory_order_acquire)) {
            const bool wasPending = g_capturePendingOnly;
            const bool wasKeepAlive = g_captureSuppressed.load(std::memory_order_acquire);
            g_capturePendingOnly = false;
            g_captureConfigStale = false;
            asr_runtime_log::Write(
                "event=capture_idle_failed generation=%llu source=%s code=0x%08lx pending=%d keepalive=%d",
                static_cast<unsigned long long>(generation),
                wasapi ? "wasapi" : "wavein",
                static_cast<unsigned long>(code),
                wasPending ? 1 : 0,
                wasKeepAlive ? 1 : 0);
            CloseAudioCapture();
            return;
        }
        asr_runtime_log::Write(
            "event=capture_runtime_failed_stale generation=%llu current=%llu source=%s code=0x%08lx recording=%d",
            static_cast<unsigned long long>(generation),
            static_cast<unsigned long long>(currentGeneration),
            wasapi ? "wasapi" : "wavein",
            static_cast<unsigned long>(code),
            g_recording ? 1 : 0);
        return;
    }

    const uint64_t attemptId = ActiveAsrAttemptId();
    const double recordingMs = static_cast<double>(GetTickCount64() - g_sessionStartTick);
    g_recording = false;

    const std::vector<BYTE> pcm = StopAudioCapture();
    g_captureConfigStale = false;
    CloseAudioCapture();
    FinishAudioCaptureFailure(generation, code, wasapi, attemptId, recordingMs, pcm);
}

void BeginCaptureOnly() {
    if (g_recording) {
        if (g_stopDelayPending) {
            KillTimer(g_mainWindow, kRecordingStopDelayTimer);
            g_stopDelayPending = false;
            g_stopDelayHeldForRepress = true;
            g_stopDelayRestoreCapsLock = false;
        }
        return;
    }
    if (g_capturePendingOnly) return;
    std::wstring error;
    AudioCaptureStartFailure captureFailure;
    if (!StartAudioCapture(error, &captureFailure)) {
        asr_runtime_log::Write(
            "event=capture_pending_start_failed capture_backends=%s terminal_backend=%s phase=%s code=%lu",
            WideToUtf8(captureFailure.attemptedBackends).c_str(),
            WideToUtf8(captureFailure.terminalBackend).c_str(),
            WideToUtf8(captureFailure.phase).c_str(),
            static_cast<unsigned long>(captureFailure.code));
        return;
    }
    g_capturePendingOnly = true;
    g_sessionStartTick = GetTickCount64();
}

void DiscardPendingCapture() {
    if (!g_capturePendingOnly) {
        if (g_stopDelayHeldForRepress) {
            g_stopDelayHeldForRepress = false;
            g_stopDelayPending = true;
            SetTimer(g_mainWindow, kRecordingStopDelayTimer, kRecordingStopDelayMs, nullptr);
        }
        return;
    }
    g_capturePendingOnly = false;
    StopAudioCapture();
    if (g_captureConfigStale) {
        g_captureConfigStale = false;
        CloseAudioCapture();
    }
    audio_diagnostics::CancelCapture();
    asr_runtime_log::Write("event=capture_pending_discarded");
}

void StartRecordingSession() {
    if (g_hudWindow) KillTimer(g_hudWindow, kHudHideTimer);
    KillTimer(g_mainWindow, kRecordingStopDelayTimer);
    g_stopDelayPending = false;
    g_stopDelayHeldForRepress = false;
    g_stopDelayRestoreCapsLock = false;
    if (g_recording) return;

    const bool resumePendingCapture = g_capturePendingOnly;
    g_capturePendingOnly = false;

    g_hudIsRefining = false;
    g_hudHasSpoken = false;
    g_vadDetectedVoice.store(false);
    std::wstring name = AsrBackendDisplayName(g_config);
    ShowHud(L"Listening... " + name);

    const uint64_t supersededAttemptId = ActiveAsrAttemptId();
    CancelActiveAsrAttempt(supersededAttemptId, true);
    auto supersededStreamingSession = TakeActiveStreamingSession();
    ResetStreamingVadTrimmerState();

    if (!resumePendingCapture) {
        std::wstring error;
        AudioCaptureStartFailure captureFailure;
        if (!StartAudioCapture(error, &captureFailure)) {
            if (supersededStreamingSession) {
                supersededStreamingSession->Abort();
                supersededStreamingSession.reset();
            }
            const uint64_t attemptId = RecordCaptureStartFailure(
                g_config, captureFailure);
            asr_runtime_log::Write(
                "event=capture_start_failed attempt=%llu primary=%s capture_backends=%s terminal_backend=%s phase=%s code=%lu error_chars=%zu",
                static_cast<unsigned long long>(attemptId),
                AsrBackendLogName(g_config.asrBackend),
                WideToUtf8(captureFailure.attemptedBackends).c_str(),
                WideToUtf8(captureFailure.terminalBackend).c_str(),
                WideToUtf8(captureFailure.phase).c_str(),
                static_cast<unsigned long>(captureFailure.code),
                error.size());
            ShowHud(error);
            if (g_hudWindow) SetTimer(g_hudWindow, kHudHideTimer, 1800, nullptr);
            return;
        }
        g_sessionStartTick = GetTickCount64();
    }
    g_recording = true;

    StartHudRecordingAnimation();
    s_wasapiUsed = g_wasapiCapture.IsInitialized();
    if (s_wasapiUsed) {
        s_wasapiDeviceName = g_wasapiCapture.GetDeviceName();
        s_wasapiNativeRate = g_wasapiCapture.GetNativeSampleRate();
    }

    SelectionContext selection;
    if (g_config.asrBackend == L"qwen_free" && g_config.qwenFreeRewriteEnabled) {
        selection = selection_context::Capture();
        if (selection.Usable()) {
            asr_runtime_log::Write(
                "event=selection_capture ok=1 chars=%zu uia=%d clipboard=%d",
                selection.selectedText.size(),
                selection.capturedWithUiAutomation ? 1 : 0,
                selection.capturedWithClipboard ? 1 : 0);
        } else {
            asr_runtime_log::Write(
                "event=selection_capture ok=0 reason=%s",
                selection.error.empty() ? "unknown" : selection.error.c_str());
        }
    }

    const uint64_t attemptId = BeginAsrAttempt(g_config, selection);
    if (supersededStreamingSession) {
        supersededStreamingSession->Abort();
        supersededStreamingSession.reset();
    }
    const Config attemptConfig = ActiveAsrAttemptConfig();

    if (attemptConfig.asrBackend == L"qwen" && IsStreamingCloudBackend(attemptConfig)) {
        auto session = (attemptConfig.qwenModel == L"qwen-audio-3.0-asr-flash-streaming")
            ? CreateQwenAudioStreamingSession(attemptConfig, g_mainWindow, RefineWithLlmAsync, GetLastRawAsrTextPtr())
            : CreateQwenStreamingSession(attemptConfig, g_mainWindow, RefineWithLlmAsync, GetLastRawAsrTextPtr());
        g_qwenPartialHudContext.attemptId = attemptId;
        session->SetPartialCallback(StreamingPartialHudCallback, &g_qwenPartialHudContext);
        session->SetFinalCallback(StreamingFinalCallback, reinterpret_cast<void*>(static_cast<UINT_PTR>(attemptId)));

        std::wstring startError;
        if (!session->Start(startError)) {
            HandleStreamingSessionStartFailure(
                attemptId,
                attemptConfig,
                startError.empty() ? L"Qwen ASR error: session start failed" : std::move(startError));
            return;
        }

        StartStreamingVadTrimmerForCloud(attemptConfig, L"Qwen thread", false);
        ActivateStreamingSession(std::move(session), /*useVadTrimmer=*/true);

        StartStreamingWatchdog(L"Listening... Qwen ASR");
        return;
    }

    if (attemptConfig.asrBackend == L"doubao_ime") {
        auto session = CreateDoubaoImeStreamingSession(attemptConfig, g_mainWindow, RefineWithLlmAsync, GetLastRawAsrTextPtr());
        g_doubaoImePartialHudContext.attemptId = attemptId;
        session->SetPartialCallback(StreamingPartialHudCallback, &g_doubaoImePartialHudContext);
        session->SetFinalCallback(StreamingFinalCallback, reinterpret_cast<void*>(static_cast<UINT_PTR>(attemptId)));

        std::wstring startError;
        if (!session->Start(startError)) {
            HandleStreamingSessionStartFailure(
                attemptId,
                attemptConfig,
                startError.empty() ? L"Doubao IME ASR error: session start failed" : std::move(startError));
            return;
        }

        ActivateStreamingSession(std::move(session), /*useVadTrimmer=*/false);

        StartStreamingWatchdog(L"Listening... Doubao IME");
        return;
    }

    if (attemptConfig.asrBackend == L"qwen_free") {
        auto session = CreateQwenFreeStreamingSession(
            attemptConfig, g_mainWindow, RefineWithLlmAsync, GetLastRawAsrTextPtr(), selection);
        g_qwenFreePartialHudContext.attemptId = attemptId;
        session->SetPartialCallback(StreamingPartialHudCallback, &g_qwenFreePartialHudContext);
        session->SetFinalCallback(StreamingFinalCallback, reinterpret_cast<void*>(static_cast<UINT_PTR>(attemptId)));

        std::wstring startError;
        if (!session->Start(startError)) {
            HandleStreamingSessionStartFailure(
                attemptId,
                attemptConfig,
                startError.empty()
                    ? L"Qwen IME ASR error: session start failed"
                    : std::move(startError));
            return;
        }

        ResetStreamingVadTrimmerState();
        ActivateStreamingSession(std::move(session), /*useVadTrimmer=*/false);

        StartStreamingWatchdog(L"Listening... Qwen IME (Free)");
        return;
    }

    if (attemptConfig.asrBackend == L"volcengine") {
        VolcengineResetForNewSession();

        auto session = CreateVolcengineStreamingSession(attemptConfig, g_mainWindow, RefineWithLlmAsync, GetLastRawAsrTextPtr());
        g_volcenginePartialHudContext.attemptId = attemptId;
        session->SetPartialCallback(StreamingPartialHudCallback, &g_volcenginePartialHudContext);
        session->SetFinalCallback(StreamingFinalCallback, reinterpret_cast<void*>(static_cast<UINT_PTR>(attemptId)));

        std::wstring startError;
        if (!session->Start(startError)) {
            HandleStreamingSessionStartFailure(
                attemptId,
                attemptConfig,
                startError.empty() ? L"VolcEngine error: session start failed" : std::move(startError));
            return;
        }

        StartStreamingVadTrimmerForCloud(attemptConfig, L"Volc thread", false);
        ActivateStreamingSession(std::move(session), /*useVadTrimmer=*/true);

        StartStreamingWatchdog(L"Listening... Volcano Engine");
        return;
    }

    g_streamingVadReady = false;
    g_streamingVadSamples.clear();
    if (attemptConfig.asrBackend == L"local" && attemptConfig.enableVad) {
        const int threads = ResolveThreads(attemptConfig.threads);
        g_asrEngine.Lock();
        bool ok = g_asrEngine.EnsureVadForConfig(attemptConfig, threads);
        if (ok) {
            if (attemptConfig.vadModel == L"firered") {
                g_asrEngine.fireRedVad->Reset();
            } else {
                g_asrEngine.vad->Reset();
            }
        }
        g_asrEngine.Unlock();
        if (ok) {
            EnterCriticalSection(&g_audioLock);
            g_asrEngine.Lock();
            if (!g_audioData.empty()) {
                const int16_t* pcm16 = reinterpret_cast<const int16_t*>(g_audioData.data());
                const size_t frames = g_audioData.size() / sizeof(int16_t);
                std::vector<float> floatBuf(frames);
                for (size_t i = 0; i < frames; ++i) {
                    floatBuf[i] = static_cast<float>(pcm16[i]) / 32768.0f;
                }
                if (attemptConfig.vadModel == L"firered") {
                    g_asrEngine.fireRedVad->Process(floatBuf);
                } else {
                    g_asrEngine.vad->AcceptWaveform(floatBuf.data(), static_cast<int32_t>(floatBuf.size()));
                }
            }
            g_streamingVadReady = true;
            g_asrEngine.Unlock();
            LeaveCriticalSection(&g_audioLock);
        }
    }
}

void StopRecordingSession() {
    if (!g_recording) return;
    if (g_audioCaptureFailurePending.load(std::memory_order_acquire)) {
        HandleAudioCaptureFailure(
            g_audioCaptureGeneration.load(std::memory_order_acquire),
            g_audioCaptureFailureCode.load(std::memory_order_acquire),
            g_audioCaptureFailureWasapi.load(std::memory_order_acquire));
        return;
    }
    g_recording = false;
    g_recordingMs = static_cast<double>(GetTickCount64() - g_sessionStartTick);
    g_vadMs = 0.0;
    g_asrDecodeMs = 0.0;
    g_punctMs = 0.0;
    g_cloudApiMs = 0.0;
    g_llmMs = 0.0;
    {
        std::lock_guard<std::mutex> lk(g_vadMetricsMutex);
        g_vadModelName.clear();
    }
    g_vadTrimmedSamples = 0;
    ClearLastRawAsrText();
    const uint64_t attemptId = ActiveAsrAttemptId();
    const Config recordingConfig = ActiveAsrAttemptConfig();
    const bool hasStreamingSession = HasActiveStreamingSession();
    const uint64_t captureGeneration =
        g_audioCaptureGeneration.load(std::memory_order_acquire);
    const std::vector<BYTE> pcm = StopAudioCapture();
    if (g_audioCaptureFailurePending.load(std::memory_order_acquire)) {
        g_captureConfigStale = false;
        CloseAudioCapture();
        FinishAudioCaptureFailure(
            captureGeneration,
            g_audioCaptureFailureCode.load(std::memory_order_acquire),
            g_audioCaptureFailureWasapi.load(std::memory_order_acquire),
            attemptId,
            g_recordingMs,
            pcm);
        return;
    }
    if (g_captureConfigStale) {
        g_captureConfigStale = false;
        CloseAudioCapture();
    }

    if (IsStreamingCloudBackend(recordingConfig) && hasStreamingSession) {
        std::unique_ptr<AsrAttemptFinalMessage> deferredFinal =
            CompleteActiveAttemptRecording(attemptId, &pcm,
                                            g_recordingMs, pcm.size());
        SetLastPcmBytes(pcm.size());
        if (pcm.size() < 8000) {
            KillTimer(g_mainWindow, kStreamingWatchdogTimer);
            asr_diagnostics::CompleteIfMissingFromText(
                recordingConfig, L"Too short", "too_short");
            audio_diagnostics::FinalizeAttempt(
                attemptId, asr_diagnostics::FinalFromText(L"Too short"));
            MarkActiveAttemptFinalHandled(attemptId);
            AbortAndResetActiveStreamingSession();
            asr_runtime_log::Write("event=attempt_skipped attempt=%llu kind=too_short pcm_bytes=%zu",
                                   static_cast<unsigned long long>(attemptId), pcm.size());
            ShowHud(L"Too short");
            SetTimer(g_hudWindow, kHudHideTimer, 1200, nullptr);
            return;
        }
        FinishStreamingVadTrimmer();
        StreamingVadTrimStats stats = {};
        if (GetStreamingVadTrimStats(stats)) {
            audio_diagnostics::StageMetadata stage =
                asr_diagnostics::MakeStageMetadata(recordingConfig);
            stage.vadActive = stats.active;
            stage.vadDetectedSpeech = stats.detectedSpeech;
            stage.vadModel = stats.modelName;
            stage.vadInputBytes = stats.rawBytes;
            stage.vadOutputBytes = stats.outputBytes;
            audio_diagnostics::UpdateStageMetadata(attemptId, stage);
        }
        if (StreamingVadTrimSawNoSpeech()) {
            KillTimer(g_mainWindow, kStreamingWatchdogTimer);
            audio_diagnostics::StageMetadata stage =
                asr_diagnostics::MakeStageMetadata(recordingConfig, L"local_vad_no_speech");
            stage.vadActive = true;
            stage.vadDetectedSpeech = false;
            audio_diagnostics::StageTerminal terminal;
            terminal.terminal = "local_vad_no_speech";
            terminal.reason = "no_speech";
            audio_diagnostics::CompleteStageIfMissing(attemptId, stage, terminal);
            audio_diagnostics::FinalizeAttempt(
                attemptId, asr_diagnostics::FinalFromText(L"No speech detected"));
            MarkActiveAttemptFinalHandled(attemptId);
            AbortAndResetActiveStreamingSession();
            asr_runtime_log::Write("event=attempt_skipped attempt=%llu kind=no_speech pcm_bytes=%zu",
                                   static_cast<unsigned long long>(attemptId), pcm.size());
            ShowHud(L"No speech detected");
            SetTimer(g_hudWindow, kHudHideTimer, 1500, nullptr);
            return;
        }
        if (deferredFinal) {
            KillTimer(g_mainWindow, kStreamingWatchdogTimer);
            HandleAsrAttemptFinal(*deferredFinal);
            return;
        }
        DWORD finalizeTimeout = IsFallbackAsrEnabled(recordingConfig)
            ? ComputeCloudAsrStreamingFinalWaitMs(g_recordingMs, pcm.size())
            : ComputeCloudAsrLegacyFinalizeTimeoutMs(g_recordingMs, pcm.size());
        EnterCriticalSection(&g_streamingSessionCs);
        if (g_activeStreamingSession) {
            g_activeStreamingSession->StopInput(g_recordingMs, pcm.size());
            finalizeTimeout = g_activeStreamingSession->CurrentWatchdogMs();
        }
        LeaveCriticalSection(&g_streamingSessionCs);
        KillTimer(g_mainWindow, kStreamingWatchdogTimer);
        SetTimer(g_mainWindow, kStreamingWatchdogTimer, finalizeTimeout, nullptr);
        if (recordingConfig.asrBackend == L"volcengine") {
            VolcDebugLog("Volc watchdog: finalize timeout reset to %ums (recording=%.0fms, pcm=%zu)",
                         finalizeTimeout, g_recordingMs, pcm.size());
        }
        ShowHud(L"Recognizing... " + AsrBackendDisplayName(recordingConfig));
        return;
    }

    CompleteActiveAttemptRecording(attemptId, &pcm, g_recordingMs, pcm.size());
    if (pcm.size() < 8000) {
        asr_diagnostics::CompleteIfMissingFromText(
            recordingConfig, L"Too short", "too_short");
        audio_diagnostics::FinalizeAttempt(
            attemptId, asr_diagnostics::FinalFromText(L"Too short"));
        MarkActiveAttemptFinalHandled(attemptId);
        asr_runtime_log::Write("event=attempt_skipped attempt=%llu kind=too_short pcm_bytes=%zu",
                               static_cast<unsigned long long>(attemptId), pcm.size());
        ShowHud(L"Too short");
        SetTimer(g_hudWindow, kHudHideTimer, 1200, nullptr);
        return;
    }

    if (g_streamingVadReady) {
        HiResTimer tVad;
        g_asrEngine.Lock();
        if (recordingConfig.vadModel == L"firered") {
            g_asrEngine.fireRedVad->Flush();
            auto samples = PcmToFloat(pcm);
            auto concat = g_asrEngine.fireRedVad->GetConcatenatedSamples(samples);
            if (!concat.empty()) {
                g_streamingVadSamples = std::move(concat);
            }
        } else {
            g_asrEngine.vad->Flush();
            while (!g_asrEngine.vad->IsEmpty()) {
                auto seg = g_asrEngine.vad->Front();
                g_streamingVadSamples.insert(g_streamingVadSamples.end(),
                    seg.samples.begin(), seg.samples.end());
                g_asrEngine.vad->Pop();
            }
        }
        g_asrEngine.Unlock();
        double ms = tVad.ElapsedMs();
        if (recordingConfig.enableDebugMode) {
            g_vadMs = ms;
            std::lock_guard<std::mutex> lk(g_vadMetricsMutex);
            g_vadModelName = (recordingConfig.vadModel == L"firered")
                ? L"FireRed" : L"Silero";
        }
        g_streamingVadReady = false;

        if (g_streamingVadSamples.empty()) {
            audio_diagnostics::StageMetadata stage =
                asr_diagnostics::MakeStageMetadata(recordingConfig, L"local_vad_no_speech");
            stage.vadActive = true;
            stage.vadDetectedSpeech = false;
            audio_diagnostics::StageTerminal terminal;
            terminal.terminal = "local_vad_no_speech";
            terminal.reason = "no_speech";
            audio_diagnostics::CompleteStageIfMissing(attemptId, stage, terminal);
            audio_diagnostics::FinalizeAttempt(
                attemptId, asr_diagnostics::FinalFromText(L"No speech detected"));
            MarkActiveAttemptFinalHandled(attemptId);
            asr_runtime_log::Write("event=attempt_skipped attempt=%llu kind=no_speech pcm_bytes=%zu",
                                   static_cast<unsigned long long>(attemptId), pcm.size());
            ShowHud(L"No speech detected");
            SetTimer(g_hudWindow, kHudHideTimer, 1500, nullptr);
            return;
        }
    }

    std::wstring name = AsrBackendDisplayName(recordingConfig);
    ShowHud(L"Recognizing... " + name);
    RecognizeAsync(pcm, attemptId, recordingConfig);
}
