#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>

extern HINSTANCE g_instance;
extern HWND g_mainWindow;
extern HICON g_appIcon;
extern std::atomic<float> g_audioLevel;
extern std::atomic<bool> g_vadDetectedVoice;
extern std::atomic<bool> g_enableDebugMode;
extern CRITICAL_SECTION g_streamingSessionCs;

