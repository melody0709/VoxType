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
void CloseSettingsWindow(HWND hwnd);

// 语言切换以"销毁 + 重建 Settings 窗口"生效：重建沿用原窗口摆放，并在新窗口上补写
// 保存状态（新窗口 WM_CREATE 的 Ready. 会盖掉保存反馈）。
void RebuildSettingsWindowForLanguage(HWND hwnd, bool fallbackAdjusted);
// 正常创建时的窗口矩形：按 owner 工作区居中，并同步 UiStyle::Scale。
void DefaultSettingsWindowRect(HWND owner, RECT& frame);
// 重建后恢复销毁前的摆放：整份 WINDOWPLACEMENT 交给 SetWindowPlacement（它的
// rcNormalPosition 是**工作区坐标**，不能直接喂给 CreateWindowExW）。
// 返回 true 表示原窗口最大化，调用方应以 SW_SHOWMAXIMIZED 显示。
bool ApplyCapturedSettingsPlacement(HWND hwnd, const RECT& fallbackFrame);

void LoadSettingsControls(HWND hwnd);
void SaveSettingsControls(HWND hwnd);

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowSettingsWindow(HWND owner);
bool ProcessSettingsDialogMessage(MSG* msg);
