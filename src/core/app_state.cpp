#include "app_state.h"
#include "input_context.h"

HINSTANCE g_instance = nullptr;
HWND g_mainWindow = nullptr;
HICON g_appIcon = nullptr;
std::atomic<float> g_audioLevel{0.0f};
std::atomic<bool> g_vadDetectedVoice{false};
std::atomic<bool> g_enableDebugMode{false};
std::atomic<bool> g_recording{false};
CRITICAL_SECTION g_streamingSessionCs;

InputContextResult g_inputContextResult;
std::mutex g_inputContextMutex;

static std::mutex s_lastRawAsrTextMutex;
static std::wstring s_lastRawAsrText;

void SetLastRawAsrText(const std::wstring& text) {
    std::lock_guard<std::mutex> lock(s_lastRawAsrTextMutex);
    s_lastRawAsrText = text;
}

std::wstring GetLastRawAsrText() {
    std::lock_guard<std::mutex> lock(s_lastRawAsrTextMutex);
    return s_lastRawAsrText;
}

void ClearLastRawAsrText() {
    std::lock_guard<std::mutex> lock(s_lastRawAsrTextMutex);
    s_lastRawAsrText.clear();
}

