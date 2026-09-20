#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "app_state.h"
#include "app_messages.h"
#include "resource.h"
#include "main_window.h"
#include "asr_attempt_manager.h"
#include "recording_session_controller.h"
#include "hud_pagination.h"
#include "config_store.h"
#include "engine_local.h"
#include "path_service.h"
#include "hud.h"
#include "hotkey.h"
#include "audio_capture.h"
#include "audio_diagnostics.h"
#include "wasapi_capture.h"
#include "input_context.h"
#include "volcengine_asr.h"
#include "volcengine_streaming_session.h"
#include "asr_probe_service_impl.h"

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

bool ProcessSettingsDialogMessage(MSG* msg);

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
    asr_probe::InitializeAsrProbeService();
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

    if (g_config.enableDebugMode) DebugModeOpenConsole();

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

    SetHotkeyTargetWindow(g_mainWindow);

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
        if (ProcessSettingsDialogMessage(&msg)) continue;
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
