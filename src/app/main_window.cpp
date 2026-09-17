#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "main_window.h"
#include "asr_attempt_manager.h"
#include "recording_session_controller.h"
#include "hud_pagination.h"
#include "config_store.h"
#include "audio_capture.h"
#include "audio_diagnostics.h"
#include "asr_diagnostics.h"
#include "asr_runtime_log.h"
#include "cloud_asr_common.h"
#include "asr_dispatcher.h"
#include "doubao_ime_asr.h"
#include "volcengine_asr.h"
#include "volcengine_streaming_session.h"
#include "hud.h"
#include "hotkey.h"
#include "settings.h"
#include "input_context.h"
#include "ui_utils.h"
#include "engine_local.h"
#include "path_service.h"

#include <thread>
#include <cstdio>
#include <algorithm>

static UINT g_taskbarCreatedMessage = 0;
static bool s_debugConsoleOpen = false;
static StreamingPartialHudState s_streamingPartialHudState;

void SetTaskbarCreatedMessage(UINT msg) {
    g_taskbarCreatedMessage = msg;
}

UINT GetTaskbarCreatedMessage() {
    return g_taskbarCreatedMessage;
}

static bool ShouldPreloadLocalAsr(const Config& config) {
    return config.asrBackend == L"local" || config.fallbackAsrBackend == L"local";
}

static Config LocalPreloadConfig(const Config& config) {
    Config localConfig = config;
    localConfig.asrBackend = L"local";
    return localConfig;
}

void DebugModeOpenConsole() {
    if (s_debugConsoleOpen) return;
    if (!AllocConsole()) return;
    s_debugConsoleOpen = true;
    FILE* dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
    SetConsoleTitleW(L"VoxType Debug Console");
    printf("\n--- Debug mode enabled ---\n\n");
}

void DebugModeCloseConsole() {
    if (!s_debugConsoleOpen) return;
    s_debugConsoleOpen = false;
    FreeConsole();
}

static void DebugPrintHeader(double recMs, size_t pcmBytes) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    printf("\n-- %02d:%02d:%02d  Rec %.1fs(%zuKB) --",
           st.wHour, st.wMinute, st.wSecond,
           recMs / 1000.0, (pcmBytes > 0 ? pcmBytes : static_cast<size_t>(recMs * 32)) / 1024);
    if (GetWasapiUsed()) {
        printf(" WASAPI %ukHz->16kHz (%ls)", GetWasapiNativeRate() / 1000, GetWasapiDeviceName().c_str());
    } else {
        printf(" waveIn 16kHz (default)");
    }
    printf("\n");
}

static void DebugPrintTextLine(const wchar_t* prefix, const std::wstring& text) {
    if (text.empty()) return;
    std::wstring oneLine = text;
    for (auto& c : oneLine) if (c == L'\n' || c == L'\r') c = L' ';
    std::wstring line = std::wstring(L"  ") + prefix + L": \"" + oneLine + L"\"\n";
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    WriteConsoleW(hOut, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
}

static void DebugPrintInputContext() {
    if (g_config.volcEnableInputContext || g_config.qwenEnableInputContext) {
        InputContextResult ic;
        {
            std::lock_guard<std::mutex> lk(g_inputContextMutex);
            ic = g_inputContextResult;
        }
        printf("  Context: %s %.0fms", input_context::LayerName(ic.successLayer), ic.elapsedMs);
        if (!ic.focusWindowClass.empty())
            printf(" class=%s", ic.focusWindowClass.c_str());
        if (!ic.controlType.empty())
            printf(" uia=%s", ic.controlType.c_str());
        if (ic.inputFieldText.empty()) {
            printf(" [%ls]\n", input_context::TruncateForDisplay(ic.windowTitle).c_str());
        } else {
            printf(" len=%d\n", ic.textLength);
            printf("  ContextText: \"");
            DWORD written = 0;
            WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), ic.inputFieldText.c_str(), (DWORD)ic.inputFieldText.size(), &written, nullptr);
            printf("\"\n");
        }
        if (ic.successLayer < 0 && !ic.failReason.empty())
            printf("  ContextFail: %s\n", ic.failReason.c_str());
    } else if (g_config.volcEnableContext) {
        printf("  Context: HISTORY %zu rounds\n", VolcengineRecognitionHistorySize());
    }
}

static void DebugPrintVadTrimLine(size_t rawBytes, size_t trimmedSamples) {
    if (trimmedSamples == 0) return;
    if (rawBytes == 0) rawBytes = static_cast<size_t>(GetRecordingMs() * 32.0);
    const size_t trimBytes = trimmedSamples * sizeof(int16_t);
    const double trimMs = trimmedSamples / 16.0;
    printf("  VAD trim: %.1fs/%zuKB -> %.1fs/%zuKB (%.0f%%)\n",
           GetRecordingMs() / 1000.0, rawBytes / 1024,
           trimMs / 1000.0, trimBytes / 1024,
           rawBytes > 0 ? 100.0 * trimBytes / rawBytes : 0.0);
}

static std::wstring VadModelNameSnapshot() {
    std::lock_guard<std::mutex> lk(g_vadMetricsMutex);
    return g_vadModelName;
}

static void DebugPrintCloudVadTrim(const Config& config) {
    if (IsStreamingCloudBackend(config) &&
        g_streamingVadTrimmer && g_streamingVadTrimmer->IsActive()) {
        StreamingVadTrimStats stats = g_streamingVadTrimmer->Stats();
        size_t rawBytes = stats.rawBytes > 0 ? stats.rawBytes : static_cast<size_t>(GetRecordingMs() * 32.0);
        if (GetLastPcmBytes() > 0) rawBytes = GetLastPcmBytes();
        if (stats.outputBytes > 0) {
            double sentMs = stats.outputBytes / 32.0;
            printf("  VAD trim: %.1fs/%zuKB -> %.1fs/%zuKB (%.0f%%)\n",
                   GetRecordingMs() / 1000.0, rawBytes / 1024,
                   sentMs / 1000.0, stats.outputBytes / 1024,
                   rawBytes > 0 ? 100.0 * stats.outputBytes / rawBytes : 0.0);
        } else {
            printf("  VAD trim: %.1fs/%zuKB raw, sent 0KB\n",
                   GetRecordingMs() / 1000.0, rawBytes / 1024);
        }
        return;
    }

    const double vadMs = g_vadMs.load(std::memory_order_relaxed);
    const size_t trimmedSamples = g_vadTrimmedSamples.load(std::memory_order_relaxed);
    if ((config.asrBackend == L"baidu" || config.asrBackend == L"qwen" || config.asrBackend == L"mimo") &&
        vadMs > 0 && trimmedSamples > 0) {
        size_t rawBytes = GetLastPcmBytes() > 0 ? GetLastPcmBytes()
            : static_cast<size_t>(GetRecordingMs() * 32.0);
        DebugPrintVadTrimLine(rawBytes, trimmedSamples);
    }
}

static void DebugPrintBatchVadTrim() {
    const double vadMs = g_vadMs.load(std::memory_order_relaxed);
    const size_t trimmedSamples = g_vadTrimmedSamples.load(std::memory_order_relaxed);
    if (vadMs > 0 && trimmedSamples > 0) {
        size_t rawBytes = GetLastPcmBytes() > 0 ? GetLastPcmBytes()
            : static_cast<size_t>(GetRecordingMs() * 32.0);
        DebugPrintVadTrimLine(rawBytes, trimmedSamples);
    }
}

void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, ID_TRAY_VERSION, APP_VERSION_WSTR);
    AppendMenuW(menu, MF_STRING, ID_TRAY_SETTINGS, L"Settings...");
    AppendMenuW(menu, MF_STRING, ID_TRAY_RELOAD, L"Reload ASR Engine");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_config.enableDebugMode ? MF_CHECKED : 0),
                ID_TRAY_DEBUG_MODE, L"Debug Mode");
    AppendMenuW(menu, MF_STRING | (g_config.forceUnicodeInput ? MF_CHECKED : 0),
                ID_TRAY_FORCE_UNICODE, L"Force Unicode Input");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_QUIT, L"Quit");
    SetForegroundWindow(hwnd);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    if (pt.y > work.bottom) pt.y = work.bottom;
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_taskbarCreatedMessage != 0 && msg == g_taskbarCreatedMessage) {
        AddTrayIcon(hwnd);
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        AddTrayIcon(hwnd);
        InstallKeyboardHook();
        return 0;
    case kTrayMessage:
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
            ShowTrayMenu(hwnd);
        } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            ShowSettingsWindow(hwnd);
        }
        return 0;
    case kReloadMessage:
        g_enableDebugMode = g_config.enableDebugMode;
        g_asrEngine.Reload();
        if (!g_recording && !IsCapturePendingOnly()) {
            CloseAudioCapture();
        } else {
            SetCaptureConfigStale(true);
        }
        if (ShouldPreloadLocalAsr(g_config)) {
            const Config cfg = LocalPreloadConfig(g_config);
            std::thread([cfg]() {
                PreloadAsrEngine(cfg);
                PostMessageW(g_mainWindow, kPreloadDoneMessage, 0, 0);
            }).detach();
        }
        return 0;
    case kPreloadDoneMessage:
        return 0;
    case kHudUpdateMessage: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
        const uint64_t attemptId = static_cast<uint64_t>(wParam);
        if (attemptId != 0 && !IsActiveAsrAttempt(attemptId, true)) return 0;
        if (text) ShowHud(*text);
        return 0;
    }
    case kHotkeyRecordingMessage:
        if (wParam == kHotkeyCaptureBegin) {
            BeginCaptureOnly();
        } else if (wParam == kHotkeyCaptureDiscard) {
            DiscardPendingCapture();
        } else if (wParam == kHotkeyRecordingStart) {
            s_streamingPartialHudState = {};
            StartRecordingSession();
        } else if (wParam == kHotkeyCapsLockRecordingStop) {
            SetStopDelayRestoreCapsLock(true);
            SetStopDelayPending(true);
            SetTimer(g_mainWindow, kRecordingStopDelayTimer, kRecordingStopDelayMs, nullptr);
        } else if (wParam == kHotkeyRecordingStop) {
            SetStopDelayRestoreCapsLock(false);
            SetStopDelayPending(true);
            SetTimer(g_mainWindow, kRecordingStopDelayTimer, kRecordingStopDelayMs, nullptr);
        }
        return 0;
    case kAudioCaptureErrorMessage:
        HandleAudioCaptureFailure(static_cast<uint64_t>(wParam),
                                  static_cast<DWORD>(lParam),
                                  true);
        return 0;
    case kWaveInCaptureErrorMessage:
        HandleAudioCaptureFailure(static_cast<uint64_t>(wParam),
                                  static_cast<DWORD>(lParam),
                                  false);
        return 0;
    case kHudUpdateWithOptionsMessage: {
        std::unique_ptr<HudUpdateWithOptionsMessage> msg(
            reinterpret_cast<HudUpdateWithOptionsMessage*>(lParam));
        if (msg && (msg->attemptId == 0 || IsActiveAsrAttempt(msg->attemptId))) {
            const std::wstring text = msg->streamingPartial
                ? FormatStreamingPartialHudText(s_streamingPartialHudState, msg->statusLine, msg->text)
                : msg->text;
            const int fixedLines = msg->streamingPartial && s_streamingPartialHudState.fixedHeightMode
                ? kStreamingPartialHudMaxLines
                : msg->fixedLines;
            ShowHudConstrained(text,
                               msg->maxWidthDip,
                               msg->maxScreenWidthFraction,
                               msg->maxLines,
                               fixedLines);
        }
        return 0;
    }
    case kDoubaoImeCredentialsMessage: {
        std::unique_ptr<doubao_ime_asr::CredentialsUpdateMessage> update(
            reinterpret_cast<doubao_ime_asr::CredentialsUpdateMessage*>(lParam));
        if (update) {
            if (update->clear) {
                g_config.doubaoImeDeviceId.clear();
                g_config.doubaoImeCdid.clear();
                g_config.doubaoImeToken.clear();
            } else {
                g_config.doubaoImeDeviceId = update->credentials.deviceId;
                g_config.doubaoImeCdid = update->credentials.cdid;
                g_config.doubaoImeToken = update->credentials.token;
            }
            SaveConfig(g_config);
            if (g_settingsWindow && IsWindow(g_settingsWindow)) {
                PostMessageW(g_settingsWindow, kDoubaoImeSettingsRefreshMessage, 0, 0);
            }
        }
        return 0;
    }
    case kAsrAttemptFinalMessage: {
        std::unique_ptr<AsrAttemptFinalMessage> result(
            reinterpret_cast<AsrAttemptFinalMessage*>(lParam));
        if (result) {
            HandleAsrAttemptFinal(*result);
        }
        return 0;
    }
    case kAsrResultMessage: {
        std::unique_ptr<AsrFinalMessage> result(reinterpret_cast<AsrFinalMessage*>(lParam));
        if (result && !ShouldAcceptFinalMessage(
                result->attemptId, result->allowCancelledAttempt)) {
            return 0;
        }
        KillTimer(g_mainWindow, kStreamingWatchdogTimer);
        const Config resultConfig = result ? result->resultConfig : g_config;
        const std::wstring text = NormalizeAsrText(result ? result->text : L"ASR failed");
        const bool isRewriteFailure = text.rfind(L"Qwen IME rewrite failed:", 0) == 0;
        const bool isError = IsOperationalAsrError(text) || isRewriteFailure;
        const bool hasSelectionRewrite = result &&
            resultConfig.asrBackend == L"qwen_free" &&
            resultConfig.qwenFreeRewriteEnabled &&
            result->selection.HasCapturedSelection() && !isRewriteFailure;
        if (wParam == 1 && !text.empty() && !isError) {
            g_hudIsRefining = true;
            ShowHud(L"Refining...");
        } else {
            g_hudIsRefining = false;
            ShowHud(text);
            if (g_hudWindow) {
                const bool isNoSpeech = (text == L"No speech detected");
                UINT hideMs = isError ? 2200 : (isNoSpeech ? 1500 : 200);
                SetTimer(g_hudWindow, kHudHideTimer, hideMs, nullptr);
            }
            if (!text.empty() && text != L"No speech detected" && !isError) {
                VolcDebugLog("DeliverFinalText: starting (text=%u chars, selection_rewrite=%d)",
                             (unsigned)text.size(), hasSelectionRewrite ? 1 : 0);
                HiResTimer tPaste;
                bool delivered = true;
                if (hasSelectionRewrite) {
                    std::wstring replaceError;
                    delivered = ReplaceSelectionTextImeAware(
                        result->selection, text, &replaceError);
                    if (!delivered) {
                        VolcDebugLog("ReplaceSelectionTextImeAware: skipped (%ls)",
                                     replaceError.c_str());
                        ShowHud(L"Rewrite skipped: " + replaceError);
                        if (g_hudWindow) SetTimer(g_hudWindow, kHudHideTimer, 2200, nullptr);
                    }
                } else {
                    PasteTextImeAware(text);
                }
                double pasteMs = tPaste.ElapsedMs();
                VolcDebugLog("DeliverFinalText: done (%.0fms, delivered=%d)",
                             pasteMs, delivered ? 1 : 0);

                if (resultConfig.enableDebugMode && delivered) {
                    DebugPrintHeader(GetRecordingMs(), GetLastPcmBytes());

                    DebugPrintInputContext();

                    if (result && result->usedFallback && !result->primaryError.empty()) {
                        DebugPrintTextLine(L"Primary failed", result->primaryError);
                    }

                    const double vadMs = g_vadMs.load(std::memory_order_relaxed);
                    const double asrDecodeMs = g_asrDecodeMs.load(std::memory_order_relaxed);
                    const double punctMs = g_punctMs.load(std::memory_order_relaxed);
                    const double cloudApiMs = g_cloudApiMs.load(std::memory_order_relaxed);
                    const size_t trimmedSamples =
                        g_vadTrimmedSamples.load(std::memory_order_relaxed);
                    const std::wstring vadModelName = VadModelNameSnapshot();

                    if (resultConfig.asrBackend == L"local") {
                        printf("  Pipeline: ");
                        if (vadMs > 0) printf("VAD(%ls) %.0f | ", vadModelName.c_str(), vadMs);
                        printf("ASR %.0f", asrDecodeMs);
                        if (punctMs > 0) printf(" | Punct %.0f", punctMs);
                        printf(" | Paste %.0f = Total %.0fms\n", pasteMs,
                               vadMs + asrDecodeMs + punctMs + pasteMs);
                        if (vadMs > 0 && trimmedSamples > 0) {
                            size_t rawBytes = GetLastPcmBytes() > 0 ? GetLastPcmBytes()
                                : static_cast<size_t>(GetRecordingMs() * 32.0);
                            DebugPrintVadTrimLine(rawBytes, trimmedSamples);
                        }
                    } else {
                        const char* backend = AsrBackendDebugName(resultConfig.asrBackend);
                        printf("  Pipeline: %s %.0f | Paste %.0f = Total %.0fms\n",
                               backend, cloudApiMs, pasteMs, cloudApiMs + pasteMs);
                        if (result && result->usedFallback) {
                            DebugPrintBatchVadTrim();
                        } else {
                            DebugPrintCloudVadTrim(resultConfig);
                        }
                    }

                    DebugPrintTextLine(L"OK", text);
                }
            } else if (isError) {
                const AsrResultClassification classification = ClassifyAsrResult(text);
                VolcDebugLog("DeliverFinalText: skipped operational error (reason=%s)",
                             AsrFailureReasonDebugName(classification.reason));
            }
        }
        return 0;
    }
    case kLlmResultMessage: {
        std::unique_ptr<LlmFinalMessage> result(reinterpret_cast<LlmFinalMessage*>(lParam));
        if (result && !ShouldAcceptFinalMessage(
                result->attemptId, result->allowCancelledAttempt)) {
            return 0;
        }
        const Config resultConfig = result ? result->resultConfig : g_config;
        const std::wstring text = result ? result->text : L"LLM failed";
        g_hudIsRefining = false;
        ShowHud(text);
        if (!text.empty() && text.rfind(L"LLM failed:", 0) != 0) {
            HiResTimer tPaste;
            const bool hasSelectionRewrite = result &&
                resultConfig.asrBackend == L"qwen_free" &&
                resultConfig.qwenFreeRewriteEnabled &&
                result->selection.HasCapturedSelection();
            bool delivered = true;
            if (hasSelectionRewrite) {
                std::wstring replaceError;
                delivered = ReplaceSelectionTextImeAware(
                    result->selection, text, &replaceError);
                if (!delivered) {
                    VolcDebugLog("ReplaceSelectionTextImeAware (LLM): skipped (%ls)",
                                 replaceError.c_str());
                    ShowHud(L"Rewrite skipped: " + replaceError);
                    if (g_hudWindow) SetTimer(g_hudWindow, kHudHideTimer, 2200, nullptr);
                }
            } else {
                PasteTextImeAware(text);
            }
            double pasteMs = tPaste.ElapsedMs();

            if (resultConfig.enableDebugMode && delivered) {
                DebugPrintHeader(GetRecordingMs(), GetLastPcmBytes());

                DebugPrintInputContext();

                if (result && result->usedFallback && !result->primaryError.empty()) {
                    DebugPrintTextLine(L"Primary failed", result->primaryError);
                }

                const double vadMs = g_vadMs.load(std::memory_order_relaxed);
                const double asrDecodeMs = g_asrDecodeMs.load(std::memory_order_relaxed);
                const double punctMs = g_punctMs.load(std::memory_order_relaxed);
                const double cloudApiMs = g_cloudApiMs.load(std::memory_order_relaxed);
                const double llmMs = g_llmMs.load(std::memory_order_relaxed);
                const std::wstring vadModelName = VadModelNameSnapshot();

                if (resultConfig.asrBackend == L"local") {
                    printf("  Pipeline: ");
                    if (vadMs > 0) printf("VAD(%ls) %.0f | ", vadModelName.c_str(), vadMs);
                    printf("ASR %.0f", asrDecodeMs);
                    if (punctMs > 0) printf(" | Punct %.0f", punctMs);
                    printf(" | LLM %.0f | Paste %.0f = Total %.0fms\n",
                           llmMs, pasteMs,
                           vadMs + asrDecodeMs + punctMs + llmMs + pasteMs);
                } else {
                    const char* backend = AsrBackendDebugName(resultConfig.asrBackend);
                    printf("  Pipeline: %s %.0f | LLM %.0f | Paste %.0f = Total %.0fms\n",
                           backend, cloudApiMs, llmMs, pasteMs,
                           cloudApiMs + llmMs + pasteMs);
                    if (result && result->usedFallback) {
                        DebugPrintBatchVadTrim();
                    } else {
                        DebugPrintCloudVadTrim(resultConfig);
                    }
                }

                DebugPrintTextLine(L"ASR", result ? result->rawAsrText : GetLastRawAsrText());
                DebugPrintTextLine(L"LLM", text);
            }
        }
        if (g_hudWindow) {
            SetTimer(g_hudWindow, kHudHideTimer, 200, nullptr);
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == kCapsLockLongPressTimer) {
            KillTimer(hwnd, kCapsLockLongPressTimer);
            ActivateCapsLockLongPress();
            return 0;
        }
        if (wParam == kRecordingStopDelayTimer) {
            KillTimer(hwnd, kRecordingStopDelayTimer);
            SetStopDelayPending(false);
            StopRecordingSession();
            if (IsStopDelayRestoreCapsLock()) {
                SetStopDelayRestoreCapsLock(false);
                RestoreCapsLockState();
            }
            return 0;
        }
        if (wParam == kMicKeepAliveTimer) {
            KillTimer(hwnd, kMicKeepAliveTimer);
            if (!g_recording && !IsCapturePendingOnly()) {
                CloseAudioCapture();
            }
            return 0;
        }
        if (wParam == kStreamingWatchdogTimer) {
            KillTimer(hwnd, kStreamingWatchdogTimer);
            if (g_recording) {
                DWORD watchdogMs = 18000;
                DWORD recordingLimitMs = 0;
                EnterCriticalSection(&g_streamingSessionCs);
                if (g_activeStreamingSession) {
                    watchdogMs = g_activeStreamingSession->CurrentWatchdogMs();
                    recordingLimitMs = g_activeStreamingSession->MaxRecordingMs();
                }
                LeaveCriticalSection(&g_streamingSessionCs);
                if (recordingLimitMs > 0) {
                    const ULONGLONG elapsedMs = GetTickCount64() - GetSessionStartTick();
                    if (elapsedMs >= recordingLimitMs) {
                        const uint64_t attemptId = ActiveAsrAttemptId();
                        const Config attemptConfig = ActiveAsrAttemptConfig();
                        asr_runtime_log::Write(
                            "event=recording_limit_reached attempt=%llu backend=%s model=%s elapsed_ms=%llu limit_ms=%lu",
                            static_cast<unsigned long long>(attemptId),
                            AsrBackendLogName(attemptConfig.asrBackend),
                            WideToUtf8(attemptConfig.qwenModel).c_str(),
                            static_cast<unsigned long long>(elapsedMs),
                            static_cast<unsigned long>(recordingLimitMs));
                        StopRecordingSession();
                        return 0;
                    }
                    const DWORD remainingMs = static_cast<DWORD>(recordingLimitMs - elapsedMs);
                    watchdogMs = (std::min)(watchdogMs, (std::max<DWORD>)(remainingMs, 1));
                }
                SetTimer(hwnd, kStreamingWatchdogTimer, watchdogMs, nullptr);
                return 0;
            }
            auto session = TakeActiveStreamingSession();
            if (session && session->IsRunning()) {
                const uint64_t attemptId = ActiveAsrAttemptId();
                const Config primaryConfig = ActiveAsrAttemptConfig();
                std::wstring providerName = session->ProviderName();
                CancelActiveAsrAttempt(attemptId, false);
                session->Abort();
                std::wstring timeoutText = std::wstring(providerName) + L" error: timeout";
                if (providerName == L"Volcano Engine") {
                    timeoutText = L"ASR failed: VolcEngine timeout";
                }
                auto* msg = new AsrAttemptFinalMessage;
                msg->attemptId = attemptId;
                msg->primaryConfig = primaryConfig;
                msg->text = std::move(timeoutText);
                msg->source = AsrAttemptFinalSource::Watchdog;
                msg->allowCancelledAttempt = true;
                if (!PostMessageW(hwnd, kAsrAttemptFinalMessage, 0,
                                  reinterpret_cast<LPARAM>(msg))) {
                    delete msg;
                }
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_SETTINGS:
            ShowSettingsWindow(hwnd);
            return 0;
        case ID_TRAY_RELOAD:
            PostMessageW(hwnd, kReloadMessage, 0, 0);
            return 0;
        case ID_TRAY_QUIT:
            DestroyWindow(hwnd);
            return 0;
        case ID_TRAY_DEBUG_MODE:
            g_config.enableDebugMode = !g_config.enableDebugMode;
            g_enableDebugMode = g_config.enableDebugMode;
            if (g_config.enableDebugMode) DebugModeOpenConsole();
            else DebugModeCloseConsole();
            SaveConfig(g_config);
            return 0;
        case ID_TRAY_FORCE_UNICODE:
            g_config.forceUnicodeInput = !g_config.forceUnicodeInput;
            SaveConfig(g_config);
            return 0;
        default:
            break;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kStreamingWatchdogTimer);
        KillTimer(hwnd, kRecordingStopDelayTimer);
        KillTimer(hwnd, kMicKeepAliveTimer);
        {
            const uint64_t attemptId = ActiveAsrAttemptId();
            CancelActiveAsrAttempt(attemptId, true);
            MarkActiveAttemptFinalHandled(attemptId);
            audio_diagnostics::DiscardAttempt(attemptId, "application_exit");
        }
        AbortAndResetActiveStreamingSession();
        VolcengineForceAbortAndCloseAll();
        CloseAudioCapture();
        UninstallKeyboardHook();
        RemoveTrayIcon(hwnd);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

bool RegisterWindowClasses() {
    WNDCLASSEXW mainClass = { sizeof(mainClass) };
    mainClass.lpfnWndProc = MainWndProc;
    mainClass.hInstance = g_instance;
    mainClass.lpszClassName = kMainClass;
    mainClass.hIcon = g_appIcon;
    mainClass.hIconSm = g_appIcon;
    mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&mainClass)) return false;

    WNDCLASSEXW settingsClass = { sizeof(settingsClass) };
    settingsClass.lpfnWndProc = SettingsWndProc;
    settingsClass.hInstance = g_instance;
    settingsClass.lpszClassName = kSettingsClass;
    settingsClass.hIcon = g_appIcon;
    settingsClass.hIconSm = g_appIcon;
    settingsClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    settingsClass.hbrBackground = g_settingsBgBrush;
    if (!RegisterClassExW(&settingsClass)) return false;

    WNDCLASSEXW hudClass = { sizeof(hudClass) };
    hudClass.lpfnWndProc = HudWndProc;
    hudClass.hInstance = g_instance;
    hudClass.lpszClassName = kHudClass;
    hudClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    hudClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    if (!RegisterClassExW(&hudClass)) return false;

    WNDCLASSEXW hotkeyClass = { sizeof(hotkeyClass) };
    hotkeyClass.lpfnWndProc = HotkeyEditWndProc;
    hotkeyClass.hInstance = g_instance;
    hotkeyClass.lpszClassName = kHotkeyEditClass;
    hotkeyClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    hotkeyClass.hbrBackground = g_controlBgBrush;
    return RegisterClassExW(&hotkeyClass) != 0;
}
