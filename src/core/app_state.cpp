#include "app_state.h"
#include "input_context.h"

HINSTANCE g_instance = nullptr;
HWND g_mainWindow = nullptr;
HICON g_appIcon = nullptr;
std::atomic<float> g_audioLevel{0.0f};
std::atomic<bool> g_vadDetectedVoice{false};
std::atomic<bool> g_enableDebugMode{false};
CRITICAL_SECTION g_streamingSessionCs;

InputContextResult g_inputContextResult;
std::mutex g_inputContextMutex;

