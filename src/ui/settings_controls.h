#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include "hotkey.h"

namespace UiStyle {
extern float Scale;
}

extern HWND g_settingsWindow;

void UpdateUiScale(HWND hwnd);
void UpdateUiScaleForDpi(UINT dpi);
RECT GetWorkAreaForWindow(HWND hwnd);
int S(int px);
void HandleSettingsDpiChanged(HWND hwnd, WPARAM wParam, LPARAM lParam);
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

HINSTANCE GetParentInstance(HWND parent);
std::wstring GetControlText(HWND hwnd, int id, size_t capacity = 1024);
inline std::wstring QwenControlText(HWND hwnd, int id, size_t capacity = 1024) {
    return GetControlText(hwnd, id, capacity);
}

extern std::atomic<uint64_t> g_sharedTestGeneration;
constexpr UINT kSharedTestResultMessage = WM_APP + 10;
void PostSharedTestResult(HWND hwnd, uint64_t generation, bool ok, std::wstring message);
