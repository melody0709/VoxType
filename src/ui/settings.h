#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

#include "settings_controls.h"
#include "settings_dialogs.h"

extern HWND g_settingsWindow;

void SetStatus(HWND hwnd, const std::wstring& text);

void ShowSettingsPage(HWND hwnd, int page);
void LayoutSettingsWindow(HWND hwnd);
void HideSettingsWindow(HWND hwnd);

void LoadSettingsControls(HWND hwnd);
void SaveSettingsControls(HWND hwnd);

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowSettingsWindow(HWND owner);
bool ProcessSettingsDialogMessage(MSG* msg);
