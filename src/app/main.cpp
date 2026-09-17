#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "globals.h"
#include "main_window.h"
#include "asr_attempt_manager.h"
#include "recording_session_controller.h"
#include "hud_pagination.h"
#include "config_store.h"
#include "engine_local.h"
#include "path_service.h"
#include "hud.h"
#include "audio_diagnostics.h"
#include "wasapi_capture.h"
#include "input_context.h"
#include "volcengine_asr.h"
#include "volcengine_streaming_session.h"

#include <thread>
#include <commctrl.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

HINSTANCE g_instance = nullptr;
HWND g_mainWindow = nullptr;
HWND g_settingsWindow = nullptr;
HWND g_hudWindow = nullptr;
HHOOK g_keyboardHook = nullptr;
HICON g_appIcon = nullptr;
HFONT g_uiFont = nullptr;
HFONT g_titleFont = nullptr;
HFONT g_sectionFont = nullptr;
HBRUSH g_settingsBgBrush = nullptr;
HBRUSH g_cardBrush = nullptr;
HBRUSH g_controlBgBrush = nullptr;
ID2D1Factory* g_d2dFactory = nullptr;
IDWriteFactory* g_dwriteFactory = nullptr;
ID2D1HwndRenderTarget* g_hudRenderTarget = nullptr;
ID2D1SolidColorBrush* g_hudBrush = nullptr;
ID2D1LinearGradientBrush* g_hudBarGradientRec = nullptr;
ID2D1LinearGradientBrush* g_hudBarGradientIdle = nullptr;
ID2D1GradientStopCollection* g_hudBarGradientStopsRec = nullptr;
ID2D1GradientStopCollection* g_hudBarGradientStopsIdle = nullptr;
IDWriteTextFormat* g_hudTextFormat = nullptr;
Config g_config;
std::atomic<bool> g_enableDebugMode{false};
bool g_recording = false;
UINT g_activeHotkeyKey = 0;
bool g_capsLockHotkeyPending = false;
bool g_capsLockLongPressActive = false;
bool g_capsLockWasOn = false;
std::wstring g_hudText = L"Ready";
HWAVEIN g_waveIn = nullptr;
WAVEHDR g_waveHeaders[8] = {};
std::vector<std::vector<BYTE>> g_waveBuffers;
std::vector<BYTE> g_audioData;
CRITICAL_SECTION g_audioLock;
std::atomic<bool> g_captureActive{false};
std::atomic<bool> g_captureSuppressed{false};
std::atomic<uint64_t> g_audioCaptureGeneration{0};
std::atomic<bool> g_audioCaptureFailurePending{false};
std::atomic<DWORD> g_audioCaptureFailureCode{0};
std::atomic<bool> g_audioCaptureFailureWasapi{false};
std::atomic<float> g_audioLevel{0.0f};
float g_hudSmoothedLevel = 0.0f;
bool g_hudHasSpoken = false;
std::atomic<bool> g_vadDetectedVoice{false};
WasapiCapture g_wasapiCapture;
std::vector<HWND> g_recognitionControls;
std::vector<HWND> g_generalControls;
std::vector<HWND> g_llmControls;
std::vector<HWND> g_promptControls;
std::vector<HWND> g_cloudAsrControls;
std::vector<HWND> g_baiduControls;
std::vector<HWND> g_volcengineControls;
std::vector<HWND> g_qwenControls;
std::vector<HWND> g_qwenAudio3Controls;
std::vector<HWND> g_qwenAudioStreamingOnlyControls;
std::vector<HWND> g_mimoControls;
std::vector<HWND> g_maiControls;
std::vector<HWND> g_maiOpenRouterControls;
std::vector<HWND> g_maiAzureControls;
std::vector<HWND> g_doubaoImeControls;
std::vector<HWND> g_qwenFreeControls;
std::vector<HWND> g_vadFireredControls;
std::vector<HWND> g_vadSileroControls;
bool g_hudIsRefining = false;
bool g_llmKeyVisible = false;
bool g_baiduKeyVisible = false;
bool g_baiduApiKeyVisible = false;
bool g_volcKeyVisible = false;
bool g_qwenKeyVisible = false;
bool g_mimoKeyVisible = false;
bool g_maiOpenRouterKeyVisible = false;
bool g_maiAzureKeyVisible = false;
std::unique_ptr<IStreamingAsrSession> g_activeStreamingSession;
std::unique_ptr<StreamingVadTrimmer> g_streamingVadTrimmer;
CRITICAL_SECTION g_streamingSessionCs;
AsrEngine g_asrEngine;
int g_cloudProviderIdx = 0;
HWND g_cloudAsrHintControl = nullptr;
InputContextResult g_inputContextResult;
std::mutex g_inputContextMutex;

std::atomic<double> g_vadMs{0.0};
std::atomic<double> g_asrDecodeMs{0.0};
std::atomic<double> g_punctMs{0.0};
std::atomic<double> g_cloudApiMs{0.0};
std::atomic<double> g_llmMs{0.0};
std::wstring g_vadModelName;
std::mutex g_vadMetricsMutex;
std::atomic<size_t> g_vadTrimmedSamples{0};
std::vector<float> g_streamingVadSamples;
std::atomic<bool> g_streamingVadReady{false};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\VoxType.SingleInstance");
    if (!mutex) {
        MessageBoxW(nullptr, L"Failed to create single-instance mutex.", kAppName, MB_OK | MB_ICONERROR);
        return 0;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"VoxType is already running.", kAppName, MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 0;
    }
    g_instance = instance;
    SetActiveVadDetector(&g_asrEngine);
    SetTaskbarCreatedMessage(RegisterWindowMessageW(L"TaskbarCreated"));
    InitializeCriticalSection(&g_audioLock);
    InitializeCriticalSection(&g_streamingSessionCs);
    InitCommonControls();
    g_appIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!g_appIcon) {
        g_appIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    CreateUiResources();

    LoadConfig(g_config);
    g_enableDebugMode = g_config.enableDebugMode;
    audio_diagnostics::SetLogCallback(WriteDiagnosticAudioRuntimeLog);

    SetDefaultHudLineCounter([](const std::wstring& statusLine, const std::wstring& body) -> uint32_t {
        return HudWrappedLineCount(BuildStreamingPartialHudText(statusLine, body),
                                   kStreamingPartialHudMaxWidthDip,
                                   kStreamingPartialHudMaxScreenFraction);
    });

    if (g_config.enableDebugMode) {
        DebugModeOpenConsole();
    }

    if (!RegisterWindowClasses()) {
        MessageBoxW(nullptr, L"Failed to register window classes.", kAppName, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        DeleteUiResources();
        DeleteCriticalSection(&g_audioLock);
        DeleteCriticalSection(&g_streamingSessionCs);
        return 1;
    }

    g_mainWindow = CreateWindowExW(
        0,
        kMainClass,
        kAppName,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        320,
        240,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!g_mainWindow) {
        MessageBoxW(nullptr, L"Failed to create main window.", kAppName, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        DeleteUiResources();
        DeleteCriticalSection(&g_audioLock);
        DeleteCriticalSection(&g_streamingSessionCs);
        return 1;
    }

    if (g_config.asrBackend == L"local" || g_config.fallbackAsrBackend == L"local") {
        Config localConfig = g_config;
        localConfig.asrBackend = L"local";
        const std::wstring modelDir = localConfig.modelDir.empty() ? DefaultModelDir(localConfig.modelId) : localConfig.modelDir;
        if (ModelDirExists(modelDir)) {
            const Config cfg = localConfig;
            std::thread([cfg]() {
                PreloadAsrEngine(cfg);
                PostMessageW(g_mainWindow, kPreloadDoneMessage, 0, 0);
            }).detach();
        }
    }

    if (g_config.asrBackend == L"volcengine" && !g_config.volcApiKey.empty()) {
        std::thread([]() {
            VolcenginePrewarmConnection();
        }).detach();
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    VolcengineClosePersistentConnection();
    audio_diagnostics::WaitForPendingWrites(INFINITE);

    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    DeleteUiResources();
    DeleteCriticalSection(&g_audioLock);
    DeleteCriticalSection(&g_streamingSessionCs);
    return static_cast<int>(msg.wParam);
}
