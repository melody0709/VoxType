#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>

#include <string>

extern HINSTANCE g_instance;
extern HWND g_mainWindow;
extern HICON g_appIcon;
extern std::atomic<float> g_audioLevel;
extern std::atomic<bool> g_vadDetectedVoice;
extern std::atomic<bool> g_enableDebugMode;
extern std::atomic<bool> g_recording;
extern CRITICAL_SECTION g_streamingSessionCs;

void SetLastRawAsrText(const std::wstring& text);
std::wstring GetLastRawAsrText();
void ClearLastRawAsrText();

