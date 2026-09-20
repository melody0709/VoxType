#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tab_general.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "hotkey.h"
#include "startup_registration.h"
#include "asr_probe_service.h"

#include <windowsx.h>
#include <shellapi.h>

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

std::wstring DiagnosticAudioModeFromControl(HWND hwnd) {
    const int index = ComboBox_GetCurSel(
        GetDlgItem(hwnd, IDC_DIAGNOSTIC_AUDIO_MODE));
    if (index == 1) return L"failures";
    if (index == 2) return L"all";
    return L"off";
}

void UpdateDiagnosticAudioHint(HWND hwnd) {
    HWND hint = GetDlgItem(hwnd, IDC_DIAGNOSTIC_AUDIO_HINT);
    if (!hint) return;
    const std::wstring mode = DiagnosticAudioModeFromControl(hwnd);
    const wchar_t* text = mode == L"all"
        ? L"Privacy: every utterance is saved as a playable WAV on this PC. Old files are removed automatically."
        : (mode == L"failures"
            ? L"Only diagnostic failures are saved on this PC. Old files are removed automatically."
            : L"Audio recording diagnostics are off. No diagnostic WAV files are saved.");
    SetWindowTextW(hint, text);
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

void OpenDiagnosticAudioFolder(HWND hwnd) {
    std::wstring error;
    if (!asr_probe::EnsureDiagnosticAudioDir(&error)) {
        MessageBoxW(hwnd, error.c_str(), L"Recording diagnostics",
                    MB_OK | MB_ICONERROR);
        return;
    }
    const std::wstring directory = asr_probe::GetDiagnosticAudioDir();
    const HINSTANCE result = ShellExecuteW(
        hwnd, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(hwnd, L"Unable to open the recordings folder.",
                    L"Recording diagnostics", MB_OK | MB_ICONERROR);
    }
}

void DeleteDiagnosticAudioFiles(HWND hwnd) {
    const int choice = MessageBoxW(
        hwnd,
        L"Delete all recordings and JSON manifests managed by VoxType?\n\n"
        L"Unknown files in the folder will be preserved.",
        L"Delete saved recordings", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (choice != IDYES) return;

    size_t deletedGroups = 0;
    std::wstring error;
    if (!asr_probe::DeleteDiagnosticManagedRecordings(&deletedGroups, &error)) {
        MessageBoxW(hwnd, error.c_str(), L"Delete saved recordings",
                    MB_OK | MB_ICONERROR);
        return;
    }
    SetStatus(hwnd, L"Deleted " + std::to_wstring(deletedGroups) +
                    L" managed recording group(s). Unknown files were preserved.");
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
    control = CreateLabel(parent, S(UiStyle::ContentLeft), startupY + S(UiStyle::GeneralStartupHintOffsetY), S(UiStyle::GeneralStartupHintW), S(UiStyle::LabelH),
                          L"Uses your Windows account startup list. Moving a Portable copy is corrected when you save.");
    AddGeneralControl(control);

    const int diagnosticsY = S(UiStyle::GeneralDiagnosticsGroupY);
    HWND diagnosticsGroup = CreateWindowW(
        L"BUTTON", L"Diagnostics", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        S(UiStyle::GeneralGroupX), diagnosticsY,
        S(UiStyle::GeneralGroupW), S(UiStyle::GeneralDiagnosticsGroupH),
        parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(diagnosticsGroup);
    AddGeneralControl(diagnosticsGroup);

    control = CreateLabel(
        parent, S(UiStyle::ContentLeft),
        diagnosticsY + S(UiStyle::GeneralDiagnosticsModeOffsetY + UiStyle::LabelYOffset),
        S(UiStyle::GeneralDiagnosticsModeLabelW), S(UiStyle::LabelH),
        L"Recording diagnostics");
    AddGeneralControl(control);
    AddGeneralControl(CreateCombo(
        parent, IDC_DIAGNOSTIC_AUDIO_MODE, S(UiStyle::InputLeft),
        diagnosticsY + S(UiStyle::GeneralDiagnosticsModeOffsetY),
        S(UiStyle::GeneralDiagnosticsModeW), S(UiStyle::ComboH)));

    const int diagnosticActionsY =
        diagnosticsY + S(UiStyle::GeneralDiagnosticsActionsOffsetY);
    AddGeneralControl(CreateButton(
        parent, IDC_DIAGNOSTIC_AUDIO_OPEN_FOLDER, S(UiStyle::ContentLeft),
        diagnosticActionsY, S(UiStyle::GeneralDiagnosticsOpenButtonW),
        S(UiStyle::ActionBtnH), L"Open recordings folder"));
    AddGeneralControl(CreateButton(
        parent, IDC_DIAGNOSTIC_AUDIO_DELETE,
        S(UiStyle::ContentLeft + UiStyle::GeneralDiagnosticsOpenButtonW +
          UiStyle::GeneralDiagnosticsButtonGap),
        diagnosticActionsY, S(UiStyle::GeneralDiagnosticsDeleteButtonW),
        S(UiStyle::ActionBtnH), L"Delete saved recordings..."));

    control = CreateHint(
        parent, S(UiStyle::ContentLeft),
        diagnosticsY + S(UiStyle::GeneralDiagnosticsHintOffsetY),
        S(UiStyle::GeneralDiagnosticsHintW), S(UiStyle::LabelH), L"");
    SetWindowLongPtrW(control, GWLP_ID, IDC_DIAGNOSTIC_AUDIO_HINT);
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

    HWND audioDiagCombo = GetDlgItem(parent, IDC_DIAGNOSTIC_AUDIO_MODE);
    if (audioDiagCombo) {
        ComboBox_ResetContent(audioDiagCombo);
        ComboBox_AddString(audioDiagCombo, L"Off (default)");
        ComboBox_AddString(audioDiagCombo, L"Save failed requests only");
        ComboBox_AddString(audioDiagCombo, L"Save all utterances (debug)");
        const std::wstring mode = asr_probe::NormalizeDiagnosticAudioMode(cfg.diagnosticAudioMode);
        int sel = 0;
        if (mode == L"failures") sel = 1;
        else if (mode == L"all") sel = 2;
        ComboBox_SetCurSel(audioDiagCombo, sel);
        UpdateDiagnosticAudioHint(parent);
    }
}

void TabGeneral::SaveControls(HWND parent, Config& cfg) {
    HWND hwnd = parent;
    HotkeyConfig hotkey = GetHotkeyFromEdit(parent, IDC_HOTKEY);
    cfg.hotkey = HotkeyToString(hotkey);

    cfg.diagnosticAudioMode = DiagnosticAudioModeFromControl(hwnd);
}

bool TabGeneral::SaveStartupRegistration(HWND parent) {
    HWND hwnd = parent;
    return SaveStartupRegistrationControl(hwnd);
}

bool TabGeneral::HandleCommand(HWND hwnd, WORD notifyCode, WORD controlId, HWND control) {
    switch (controlId) {
    case IDC_DIAGNOSTIC_AUDIO_MODE:
        if (notifyCode == CBN_SELCHANGE) {
            UpdateDiagnosticAudioHint(hwnd);
            return true;
        }
        return false;
    case IDC_DIAGNOSTIC_AUDIO_OPEN_FOLDER:
        OpenDiagnosticAudioFolder(hwnd);
        return true;
    case IDC_DIAGNOSTIC_AUDIO_DELETE:
        DeleteDiagnosticAudioFiles(hwnd);
        return true;
    default:
        return false;
    }
}

} // namespace ui_tab
