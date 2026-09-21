#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tab_general.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "hotkey.h"
#include "startup_registration.h"

#include <windowsx.h>

namespace ui_tab {

namespace {

std::wstring DescribeWin32Error(DWORD error) {
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result = length && message ? std::wstring(message, length) : L"Unknown Windows error";
    if (message) LocalFree(message);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result;
}

} // namespace

void RefreshStartupRegistrationControl(HWND hwnd, bool reportError) {
    const StartupRegistrationState startup = QueryVoxTypeStartupRegistration();
    HWND checkbox = GetDlgItem(hwnd, IDC_START_WITH_WINDOWS);
    if (!checkbox) return;

    if (!startup.Succeeded()) {
        Button_SetCheck(checkbox, BST_UNCHECKED);
        if (reportError) {
            SetStatus(hwnd, L"Could not read Windows startup setting (" +
                std::to_wstring(startup.error) + L"): " + DescribeWin32Error(startup.error));
        }
        return;
    }

    Button_SetCheck(checkbox, startup.registered ? BST_CHECKED : BST_UNCHECKED);
    if (reportError && startup.registered && !startup.pointsToCurrentExecutable) {
        SetStatus(hwnd, L"Startup is enabled for a moved copy. Save will update it to this VoxType folder.");
    }
}

bool SaveStartupRegistrationControl(HWND hwnd) {
    const bool enable = Button_GetCheck(GetDlgItem(hwnd, IDC_START_WITH_WINDOWS)) == BST_CHECKED;
    const DWORD error = SetVoxTypeStartupRegistration(enable);
    if (error == ERROR_SUCCESS) return true;

    const std::wstring detail = L"Windows startup setting was not changed (" +
        std::to_wstring(error) + L"): " + DescribeWin32Error(error);
    SetStatus(hwnd, detail);
    MessageBoxW(hwnd, detail.c_str(), L"VoxType Settings", MB_OK | MB_ICONERROR);
    return false;
}

void TabGeneral::CreateControls(HWND parent) {
    m_controls.clear();
    const int shortcutY = S(UiStyle::GeneralShortcutGroupY);
    HWND shortcutGroup = CreateWindowW(L"BUTTON", L"Shortcut", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                       S(UiStyle::GeneralGroupX), shortcutY, S(UiStyle::GeneralGroupW), S(UiStyle::GeneralShortcutGroupH), parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(shortcutGroup);
    AddGeneralControl(shortcutGroup);

    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), shortcutY + S(28), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Hold hotkey");
    AddGeneralControl(control);
    AddGeneralControl(CreateHotkeyEdit(parent, IDC_HOTKEY, S(UiStyle::InputLeft), shortcutY + S(20), S(300), S(UiStyle::HotkeyEditH), CurrentConfiguredHotkey()));
    control = CreateLabel(parent, S(UiStyle::ContentLeft), shortcutY + S(68), S(740), S(UiStyle::LabelH), L"Click the field, then press the key or key combination to use while recording.");
    AddGeneralControl(control);
    control = CreateLabel(parent, S(UiStyle::ContentLeft), shortcutY + S(98), S(740), S(UiStyle::LabelH), L"Esc cancels recording a shortcut. Backspace/Delete clears it.");
    AddGeneralControl(control);

    const int inputY = S(UiStyle::GeneralInputGroupY);
    HWND inputGroup = CreateWindowW(L"BUTTON", L"Input preview", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                    S(UiStyle::GeneralGroupX), inputY, S(UiStyle::GeneralGroupW), S(UiStyle::GeneralInputGroupH), parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(inputGroup);
    AddGeneralControl(inputGroup);
    HWND partialCheckbox = CreateWindowW(L"BUTTON", L"Partial result", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                         S(UiStyle::ContentLeft), inputY + S(UiStyle::GeneralInputCheckOffsetY), S(UiStyle::GeneralInputCheckW), S(UiStyle::CheckH), parent,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PARTIAL)), GetParentInstance(parent), nullptr);
    ApplyUiFont(partialCheckbox);
    AddGeneralControl(partialCheckbox);

    const int startupY = S(UiStyle::GeneralStartupGroupY);
    HWND startupGroup = CreateWindowW(L"BUTTON", L"Startup", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                      S(UiStyle::GeneralGroupX), startupY, S(UiStyle::GeneralGroupW), S(UiStyle::GeneralStartupGroupH), parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(startupGroup);
    AddGeneralControl(startupGroup);
    HWND startupCheckbox = CreateWindowW(L"BUTTON", L"Start VoxType when I sign in to Windows", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                         S(UiStyle::ContentLeft), startupY + S(UiStyle::GeneralStartupCheckOffsetY), S(UiStyle::GeneralStartupCheckW), S(UiStyle::CheckH), parent,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_START_WITH_WINDOWS)), GetParentInstance(parent), nullptr);
    ApplyUiFont(startupCheckbox);
    AddGeneralControl(startupCheckbox);
    control = CreateLabel(parent, S(UiStyle::ContentLeft), startupY + S(UiStyle::GeneralStartupHintOffsetY), S(UiStyle::GeneralStartupHintW), S(UiStyle::QwenHint2LineH),
                          L"Uses your Windows account startup list. Moving a Portable copy is corrected when you save.");
    AddGeneralControl(control);
}

void TabGeneral::DestroyControls() {
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
}

void TabGeneral::Show(bool visible) {
    for (HWND c : m_controls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
}

void TabGeneral::LoadControls(HWND parent, const Config& cfg) {
    HWND hotkeyEdit = GetDlgItem(parent, IDC_HOTKEY);
    auto* hotkeyState = hotkeyEdit ? reinterpret_cast<HotkeyEditState*>(GetWindowLongPtrW(hotkeyEdit, GWLP_USERDATA)) : nullptr;
    if (hotkeyState) {
        hotkeyState->hotkey = ConfiguredHotkeyOrDefault(cfg.hotkey);
        hotkeyState->original = hotkeyState->hotkey;
        InvalidateRect(hotkeyEdit, nullptr, TRUE);
    }

    RefreshStartupRegistrationControl(g_settingsWindow ? g_settingsWindow : parent, true);
    Button_SetCheck(GetDlgItem(parent, IDC_PARTIAL), cfg.enablePartial ? BST_CHECKED : BST_UNCHECKED);
}

void TabGeneral::SaveControls(HWND parent, Config& cfg) {
    HWND hwnd = parent;
    HotkeyConfig hotkey = GetHotkeyFromEdit(parent, IDC_HOTKEY);
    cfg.hotkey = HotkeyToString(hotkey);

    cfg.enablePartial = Button_GetCheck(GetDlgItem(hwnd, IDC_PARTIAL)) == BST_CHECKED;
}

bool TabGeneral::SaveStartupRegistration(HWND parent) {
    HWND hwnd = parent;
    return SaveStartupRegistrationControl(hwnd);
}

} // namespace ui_tab
