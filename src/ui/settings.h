#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

#include "settings_controls.h"
#include "settings_dialogs.h"

void SetStatus(HWND hwnd, const std::wstring& text);

void AddRecognitionControl(HWND hwnd);
void AddShortcutControl(HWND hwnd);
void AddLlmControl(HWND hwnd);
void AddPromptControl(HWND hwnd);
void AddCloudAsrControl(HWND hwnd);
void AddBaiduControl(HWND hwnd);
void AddVolcengineControl(HWND hwnd);
void ShowCloudSubPage(HWND hwnd, int providerIdx);
void ShowSettingsPage(HWND hwnd, int page);
void LayoutSettingsWindow(HWND hwnd);
void HideSettingsWindow(HWND hwnd);

void RefreshProviderDropdown(HWND hwnd);
void LoadSettingsControls(HWND hwnd);
void SaveSettingsControls(HWND hwnd);
void TestLlmConnection(HWND hwnd);
void DeleteProviderFromStore(const std::wstring& name);

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowSettingsWindow(HWND owner);
