#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

#include "ui_types.h"
#include "app_messages.h"

struct HotkeyConfig {
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool win = false;
    UINT key = VK_CAPITAL;

    bool IsEmpty() const { return key == 0; }
};

struct HotkeyEditState {
    HotkeyConfig hotkey;
    HotkeyConfig original;
    bool capturing = false;
};


bool IsModifierKey(UINT vk);
UINT NormalizedKeyFromWParam(WPARAM wParam);
UINT NormalizedKeyFromKeyMessage(WPARAM wParam, LPARAM lParam);
std::wstring KeyName(UINT key);
std::wstring HotkeyToString(const HotkeyConfig& hotkey);
HotkeyConfig HotkeyFromString(const std::wstring& text);
HotkeyConfig CurrentConfiguredHotkey();
bool ModifiersMatch(const HotkeyConfig& hotkey);

void ResetCapsLockHotkeyState();
void StartCapsLockHotkeyPress();
void ActivateCapsLockLongPress();
void FinishCapsLockHotkeyPress();
bool IsCapsLockOn();
void SendCapsLockTap();

UINT GetActiveHotkeyKey();
void SetActiveHotkeyKey(UINT key);
bool WasCapsLockOn();
void SetCapsLockWasOn(bool wasOn);

void SetHotkeyTargetWindow(HWND hwnd);
HWND GetHotkeyTargetWindow();
void SetDefaultUiFont(HFONT font);

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam);
void InstallKeyboardHook();
void UninstallKeyboardHook();

void ApplyUiFont(HWND hwnd, HFONT font = nullptr);
LRESULT CALLBACK HotkeyEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
HWND CreateHotkeyEdit(HWND parent, int id, int x, int y, int w, int h, const HotkeyConfig& initial);
HotkeyConfig GetHotkeyFromEdit(HWND parent, int id);
