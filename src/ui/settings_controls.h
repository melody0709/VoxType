#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace UiStyle {
extern float Scale;
}

extern HWND g_settingsWindow;
extern std::vector<HWND> g_recognitionControls;
extern std::vector<HWND> g_generalControls;
extern std::vector<HWND> g_llmControls;
extern std::vector<HWND> g_promptControls;
extern std::vector<HWND> g_cloudAsrControls;
extern std::vector<HWND> g_baiduControls;
extern std::vector<HWND> g_volcengineControls;
extern std::vector<HWND> g_qwenControls;
extern std::vector<HWND> g_qwenAudio3Controls;
extern std::vector<HWND> g_qwenAudioStreamingOnlyControls;
extern std::vector<HWND> g_mimoControls;
extern std::vector<HWND> g_maiControls;
extern std::vector<HWND> g_maiOpenRouterControls;
extern std::vector<HWND> g_maiAzureControls;
extern std::vector<HWND> g_doubaoImeControls;
extern std::vector<HWND> g_qwenFreeControls;
extern std::vector<HWND> g_vadFireredControls;
extern std::vector<HWND> g_vadSileroControls;
extern bool g_llmKeyVisible;
extern bool g_baiduKeyVisible;
extern bool g_baiduApiKeyVisible;
extern bool g_volcKeyVisible;
extern bool g_qwenKeyVisible;
extern bool g_mimoKeyVisible;
extern bool g_maiOpenRouterKeyVisible;
extern bool g_maiAzureKeyVisible;
extern int g_cloudProviderIdx;
extern HWND g_cloudAsrHintControl;

void UpdateUiScale(HWND hwnd);
int S(int px);
void MarkSettingsHint(HWND hwnd);
bool IsSettingsHint(HWND hwnd);
void OpenAsrDebugLog(HWND hwnd, const wchar_t* fileName);

HWND CreateLabel(HWND parent, int x, int y, int w, int h, const wchar_t* text);
HWND CreateHint(HWND parent, int x, int y, int w, int h, const wchar_t* text);
HWND CreateCombo(HWND parent, int id, int x, int y, int w, int h);
HWND CreateButton(HWND parent, int id, int x, int y, int w, int h, const wchar_t* text);
HWND CreateCheckBox(HWND parent, int id, int x, int y, int w, int h, const wchar_t* text);
std::wstring ComboText(HWND combo);
void BrowseModelDirectory(HWND hwnd);

int QwenLanguageIndexFromCode(const std::wstring& code);
const wchar_t* QwenLanguageCodeFromIndex(int index);
void PopulateQwenLanguageCombo(HWND combo);
