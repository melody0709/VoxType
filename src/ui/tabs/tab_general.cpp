#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tab_general.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "hotkey.h"
#include "injection_ledger.h"
#include "learning_store.h"
#include "startup_registration.h"
#include "browser_bridge_registration.h"
#include "browser_bridge.h"

#include <windowsx.h>
#include <shellapi.h>

#include <format>

namespace ui_tab {

namespace {

// Presentation order of the voice-cleanup combo. The persisted values live in
// voice_text_mode (core layer) so the ASR post-processor parses exactly these
// strings without ui ever including the asr layer.
const wchar_t* const kPunctModeLabels[] = {
    L"Keep all punctuation",
    L"Replace with spaces",
    L"No end punctuation",
};

std::wstring PunctModeValueFromIndex(int index) {
    switch (index) {
    case 1: return voice_text_mode::kSpaceReplace;
    case 2: return voice_text_mode::kRemoveTrailing;
    default: return voice_text_mode::kKeepAll;
    }
}

int PunctModeIndexFromValue(const std::wstring& value) {
    if (value == voice_text_mode::kSpaceReplace) return 1;
    if (value == voice_text_mode::kRemoveTrailing) return 2;
    return 0;
}

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

void RefreshBrowserBridgeControl(HWND parent) {
    HWND statusLabel = GetDlgItem(parent, IDC_BROWSER_STATUS);
    if (!statusLabel) return;

    const browser_bridge_registration::BridgeState state =
        browser_bridge_registration::QueryBridgeState();
    const auto connection = browser_bridge::Bridge::Instance().Status();
    const auto browserState = [](size_t connected, size_t paired,
                                  browser_bridge_registration::HostState registered) {
        if (connected) return L"connected";
        if (paired) return L"paired (offline)";
        return registered == browser_bridge_registration::HostState::Ready ? L"not paired" : L"setup needed";
    };
    const std::wstring text = connection.error.empty() ? std::format(L"Chrome: {} / Edge: {}{}",
        browserState(connection.chrome, connection.pairedChrome, state.chrome),
        browserState(connection.edge, connection.pairedEdge, state.edge),
        connection.legacy ? L" (legacy host active)" : L"") :
        L"Browser connection unavailable: " + Utf8ToWide(connection.error);
    SetWindowTextW(statusLabel, text.c_str());
}

// Opens a file or folder and reports the real outcome. The "Opened ..." status
// must not be printed when the shell refused, which is the normal case for a
// development run whose runtime payload is not installed yet.
void OpenWithShell(HWND parent, const std::wstring& target, const wchar_t* successText,
                   const wchar_t* missingText) {
    if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
        SetStatus(parent, missingText);
        return;
    }
    const HINSTANCE result = ShellExecuteW(parent, L"open", target.c_str(), nullptr, nullptr,
                                           SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        SetStatus(parent, std::format(L"Could not open {} (shell error {}).",
                                      target, reinterpret_cast<INT_PTR>(result)));
        return;
    }
    SetStatus(parent, successText);
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
    m_parent = parent;
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

    HWND fastPasteCheckbox = CreateWindowW(L"BUTTON", L"Fast paste + clipboard history", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                  S(UiStyle::ContentLeft + UiStyle::GeneralRestoreClipboardOffsetX), inputY + S(UiStyle::GeneralInputCheckOffsetY), S(UiStyle::GeneralRestoreClipboardW), S(UiStyle::CheckH), parent,
                                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RESTORE_CLIPBOARD)), GetParentInstance(parent), nullptr);
    ApplyUiFont(fastPasteCheckbox);
    AddGeneralControl(fastPasteCheckbox);

    // Row 2 of the same group: local voice-cleanup controls (P3).
    const int cleanupY = inputY + S(UiStyle::GeneralCleanupRowOffsetY);
    control = CreateLabel(parent, S(UiStyle::ContentLeft), cleanupY + S(UiStyle::LabelYOffset),
                          S(UiStyle::GeneralCleanupLabelW), S(UiStyle::LabelH), L"Punctuation");
    AddGeneralControl(control);
    HWND punctCombo = CreateCombo(parent, IDC_VOICE_PUNCT_MODE,
                                  S(UiStyle::ContentLeft + UiStyle::GeneralCleanupComboOffsetX), cleanupY,
                                  S(UiStyle::GeneralCleanupComboW), S(UiStyle::ComboH));
    for (const wchar_t* label : kPunctModeLabels) {
        ComboBox_AddString(punctCombo, label);
    }
    ComboBox_SetCurSel(punctCombo, 0);
    AddGeneralControl(punctCombo);

    HWND spacingCheckbox = CreateCheckBox(parent,
                                          IDC_VOICE_SPACING,
                                          S(UiStyle::ContentLeft + UiStyle::GeneralCleanupCheckOffsetX), cleanupY,
                                          S(UiStyle::GeneralCleanupCheckW), S(UiStyle::CheckH),
                                          L"Space between Chinese & English");
    AddGeneralControl(spacingCheckbox);

    // Row 3 of the same group: local learning loop (P2).
    const int learningY = inputY + S(UiStyle::GeneralLearningRowOffsetY);
    HWND learningCheckbox = CreateCheckBox(parent,
                                           IDC_LEARNING_ENABLED,
                                           S(UiStyle::ContentLeft), learningY,
                                           S(UiStyle::GeneralLearningCheckW), S(UiStyle::CheckH),
                                           L"Learn from manual corrections (cloud sync)");
    AddGeneralControl(learningCheckbox);
    HWND clearLearningBtn = CreateButton(parent,
                                         IDC_LEARNING_CLEAR,
                                         S(UiStyle::ContentLeft + UiStyle::GeneralLearningClearOffsetX), learningY,
                                         S(UiStyle::GeneralLearningClearW), S(UiStyle::ActionBtnH),
                                         L"Clear learned data");
    AddGeneralControl(clearLearningBtn);

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

    const int browserY = S(UiStyle::GeneralBrowserGroupY);
    HWND browserGroup = CreateWindowW(L"BUTTON", L"Browser integration (Chrome / Edge)", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                      S(UiStyle::GeneralGroupX), browserY, S(UiStyle::GeneralGroupW), S(UiStyle::GeneralBrowserGroupH), parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(browserGroup);
    AddGeneralControl(browserGroup);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), browserY + S(UiStyle::GeneralBrowserHintOffsetY), S(UiStyle::GeneralBrowserHintW), S(UiStyle::LabelH),
                          L"Load the companion extension, then click its icon to pair with VoxType.");
    AddGeneralControl(control);

    HWND statusLabel = CreateWindowW(L"STATIC", L"Bridge: checking...", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
                                     S(UiStyle::ContentLeft), browserY + S(UiStyle::GeneralBrowserStatusOffsetY), S(UiStyle::GeneralBrowserStatusW), S(UiStyle::LabelH),
                                     parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BROWSER_STATUS)), GetParentInstance(parent), nullptr);
    ApplyUiFont(statusLabel);
    AddGeneralControl(statusLabel);

    const int browserActionsY = browserY + S(UiStyle::GeneralBrowserActionsOffsetY);
    HWND regBtn = CreateButton(parent, IDC_BROWSER_REGISTER_BRIDGE, S(UiStyle::ContentLeft), browserActionsY,
                               S(UiStyle::GeneralBrowserBtnW), S(UiStyle::ActionBtnH), L"Pair / Refresh");
    AddGeneralControl(regBtn);

    const int folderBtnX = UiStyle::ContentLeft + UiStyle::GeneralBrowserBtnW + UiStyle::GeneralBrowserButtonGap;
    HWND folderBtn = CreateButton(parent, IDC_BROWSER_OPEN_FOLDER, S(folderBtnX), browserActionsY,
                                  S(UiStyle::GeneralBrowserFolderBtnW), S(UiStyle::ActionBtnH), L"Open Extension Folder");
    AddGeneralControl(folderBtn);

    const int guideBtnX = folderBtnX + UiStyle::GeneralBrowserFolderBtnW + UiStyle::GeneralBrowserButtonGap;
    HWND guideBtn = CreateButton(parent, IDC_BROWSER_SETUP_GUIDE, S(guideBtnX), browserActionsY,
                                 S(UiStyle::GeneralBrowserGuideBtnW), S(UiStyle::ActionBtnH), L"Setup Guide");
    AddGeneralControl(guideBtn);
    const int revokeBtnX = guideBtnX + UiStyle::GeneralBrowserGuideBtnW + UiStyle::GeneralBrowserButtonGap;
    AddGeneralControl(CreateButton(parent, IDC_BROWSER_REVOKE, S(revokeBtnX), browserActionsY,
        S(UiStyle::GeneralBrowserRevokeBtnW), S(UiStyle::ActionBtnH), L"Unpair all"));
}

void TabGeneral::DestroyControls() {
    if (m_parent) KillTimer(m_parent, reinterpret_cast<UINT_PTR>(this));
    m_parent = nullptr;
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
}

void TabGeneral::Show(bool visible) {
    if (m_parent) {
        if (visible) {
            RefreshBrowserBridgeControl(m_parent);
            SetTimer(m_parent, reinterpret_cast<UINT_PTR>(this), 1000, nullptr);
        } else KillTimer(m_parent, reinterpret_cast<UINT_PTR>(this));
    }
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
    RefreshBrowserBridgeControl(parent);
    Button_SetCheck(GetDlgItem(parent, IDC_PARTIAL), cfg.enablePartial ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(parent, IDC_RESTORE_CLIPBOARD), cfg.restoreClipboardAfterPaste ? BST_UNCHECKED : BST_CHECKED);
    ComboBox_SetCurSel(GetDlgItem(parent, IDC_VOICE_PUNCT_MODE), PunctModeIndexFromValue(cfg.voicePunctMode));
    Button_SetCheck(GetDlgItem(parent, IDC_VOICE_SPACING), cfg.voiceSpacingEnabled ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(parent, IDC_LEARNING_ENABLED), cfg.asrLearningEnabled ? BST_CHECKED : BST_UNCHECKED);
}

void TabGeneral::SaveControls(HWND parent, Config& cfg) {
    HWND hwnd = parent;
    HotkeyConfig hotkey = GetHotkeyFromEdit(parent, IDC_HOTKEY);
    cfg.hotkey = HotkeyToString(hotkey);

    cfg.enablePartial = Button_GetCheck(GetDlgItem(hwnd, IDC_PARTIAL)) == BST_CHECKED;
    cfg.restoreClipboardAfterPaste = Button_GetCheck(GetDlgItem(hwnd, IDC_RESTORE_CLIPBOARD)) != BST_CHECKED;
    cfg.asrLearningEnabled = Button_GetCheck(GetDlgItem(hwnd, IDC_LEARNING_ENABLED)) == BST_CHECKED;
    const int punctIndex = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_VOICE_PUNCT_MODE));
    cfg.voicePunctMode = PunctModeValueFromIndex(punctIndex);
    cfg.voiceSpacingEnabled = Button_GetCheck(GetDlgItem(hwnd, IDC_VOICE_SPACING)) == BST_CHECKED;
}

bool TabGeneral::SaveStartupRegistration(HWND parent) {
    HWND hwnd = parent;
    return SaveStartupRegistrationControl(hwnd);
}

bool TabGeneral::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    (void)notifyCode;
    (void)control;
    if (controlId == IDC_BROWSER_REGISTER_BRIDGE) {
        const std::wstring manifest = browser_bridge_registration::ManifestPath();
        const auto state = browser_bridge_registration::QueryBridgeState();
        const bool ready = state.chrome == browser_bridge_registration::HostState::Ready &&
                           state.edge == browser_bridge_registration::HostState::Ready;
        const auto registered = ready ? std::expected<void, std::wstring>{} :
            browser_bridge_registration::RegisterHost(manifest);
        if (registered) {
            RefreshBrowserBridgeControl(parent);
            SetStatus(parent, L"Click the browser extension icon to pair. Normal connections use WebSocket.");
            MessageBoxW(parent,
                        L"Pairing is ready for Chrome and Edge.\n\n"
                        L"If the companion extension is not loaded yet, open chrome://extensions "
                        L"(or edge://extensions), enable Developer mode, and use 'Load unpacked' to "
                        L"select the extensions\\browser-bridge folder. Reload an older extension.\n\n"
                        L"Keep VoxType running and click the extension icon once in each browser profile "
                        L"to pair. The temporary pairing host exits; subsequent connections use WebSocket. "
                        L"Use Unpair all to revoke credentials. Connection status refreshes here each second.",
                        L"VoxType Browser Integration", MB_OK | MB_ICONINFORMATION);
        } else {
            SetStatus(parent, L"Registration failed: " + registered.error());
            MessageBoxW(parent, registered.error().c_str(), L"VoxType Browser Integration",
                        MB_OK | MB_ICONERROR);
        }
        return true;
    }
    if (controlId == IDC_BROWSER_REVOKE) {
        if (MessageBoxW(parent, L"Revoke all browser pairings for this VoxType installation?\n"
            L"Each browser profile will need its extension icon clicked to pair again.",
            L"VoxType Browser Integration", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return true;
        const auto revoked = browser_bridge::Bridge::Instance().RevokePairings();
        RefreshBrowserBridgeControl(parent);
        SetStatus(parent, revoked ? L"Browser pairings revoked." : L"Could not revoke pairings: " + Utf8ToWide(revoked.error()));
        return true;
    }
    if (controlId == IDC_BROWSER_OPEN_FOLDER) {
        OpenWithShell(parent, browser_bridge_registration::ExtensionDir(),
                      L"Opened extensions\\browser-bridge folder.",
                      L"the companion extension folder is missing; run build.bat to install the "
                      L"runtime payload.");
        return true;
    }
    if (controlId == IDC_BROWSER_SETUP_GUIDE) {
        OpenWithShell(parent,
                      browser_bridge_registration::ExtensionDir() + L"\\README.md",
                      L"Opened browser companion setup guide.",
                      L"the companion setup guide is missing; run build.bat to install the runtime "
                      L"payload.");
        return true;
    }
    if (controlId != IDC_LEARNING_CLEAR) return false;
    if (MessageBoxW(parent,
                    L"Delete every correction learned on this PC? The vocabulary file is not touched.",
                    L"VoxType Settings", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
        return true;
    }
    if (!learning_store::Clear()) {
        SetStatus(parent, L"Learned data could not be deleted (file is in use).");
        return true;
    }
    // Drop the pending reflection before reporting success. A worker that already
    // snapshotted the injection still calls learning_store::Add() when its message
    // reaches this window, and Add re-reads the (now empty) file, so without this
    // the pair would be written straight back and the status line above would be
    // a lie. The whole window is reflectionDelayMs (2.5 s by default), so
    // "dictate, then clear" hits it reliably.
    injection_ledger::Invalidate();
    SetStatus(parent, L"Learned corrections deleted.");
    return true;
}

bool TabGeneral::HandleMessage(HWND parent, UINT msg, WPARAM wParam, LPARAM) {
    if (msg != WM_TIMER || wParam != reinterpret_cast<UINT_PTR>(this)) return false;
    RefreshBrowserBridgeControl(parent);
    return true;
}

} // namespace ui_tab
