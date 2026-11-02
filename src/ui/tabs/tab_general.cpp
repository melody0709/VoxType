#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tab_general.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "ui_strings.h"
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

// Presentation order of the voice-cleanup combo is fixed by PunctModeValueFromIndex
// below. The persisted values live in voice_text_mode (core layer) so the ASR
// post-processor parses exactly these strings without ui ever including the asr
// layer; the visible labels come from the language table at creation time.

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
    std::wstring result = length && message ? std::wstring(message, length) : ui_strings::UiText().unknownWin32Error;
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
    const auto& text = ui_strings::UiText();
    const auto browserState = [&text](size_t connected, size_t paired,
                                      browser_bridge_registration::HostState registered) {
        if (connected) return text.bridgeConnected;
        if (paired) return text.bridgePairedOffline;
        return registered == browser_bridge_registration::HostState::Ready ? text.bridgeNotPaired : text.bridgeSetupNeeded;
    };
    const std::wstring statusText = connection.error.empty() ? ui_strings::Format(text.bridgeStatusFormat,
        browserState(connection.chrome, connection.pairedChrome, state.chrome),
        browserState(connection.edge, connection.pairedEdge, state.edge),
        connection.legacy ? text.bridgeLegacySuffix : L"") :
        std::wstring(text.bridgeUnavailablePrefix) + Utf8ToWide(connection.error);
    SetWindowTextW(statusLabel, statusText.c_str());
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
        SetStatus(parent, ui_strings::Format(ui_strings::UiText().shellOpenFailed,
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
            SetStatus(hwnd, ui_strings::Format(ui_strings::UiText().startupReadError,
                                               startup.error, DescribeWin32Error(startup.error)));
        }
        return;
    }

    Button_SetCheck(checkbox, startup.registered ? BST_CHECKED : BST_UNCHECKED);
    if (reportError && startup.registered && !startup.pointsToCurrentExecutable) {
        SetStatus(hwnd, ui_strings::UiText().startupMovedCopy);
    }
}

bool SaveStartupRegistrationControl(HWND hwnd) {
    const bool enable = Button_GetCheck(GetDlgItem(hwnd, IDC_START_WITH_WINDOWS)) == BST_CHECKED;
    const DWORD error = SetVoxTypeStartupRegistration(enable);
    if (error == ERROR_SUCCESS) return true;

    const std::wstring detail = ui_strings::Format(ui_strings::UiText().startupWriteError,
                                                   error, DescribeWin32Error(error));
    SetStatus(hwnd, detail);
    MessageBoxW(hwnd, detail.c_str(), ui_strings::UiText().settingsTitle, MB_OK | MB_ICONERROR);
    return false;
}

void TabGeneral::CreateControls(HWND parent) {
    m_parent = parent;
    m_controls.clear();
    const auto& text = ui_strings::UiText();
    const int shortcutY = S(UiStyle::GeneralShortcutStartupGroupY);
    HWND shortcutGroup = CreateWindowW(L"BUTTON", text.generalShortcutStartupGroupTitle, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                       S(UiStyle::GeneralGroupX), shortcutY, S(UiStyle::GeneralGroupW), S(UiStyle::GeneralShortcutStartupGroupH), parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(shortcutGroup);
    AddGeneralControl(shortcutGroup);

    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), shortcutY + S(UiStyle::GeneralShortcutLabelOffsetY), S(UiStyle::LabelWidth), S(UiStyle::LabelH), text.generalHoldHotkey);
    AddGeneralControl(control);
    AddGeneralControl(CreateHotkeyEdit(parent, IDC_HOTKEY, S(UiStyle::InputLeft), shortcutY + S(UiStyle::GeneralShortcutRowOffsetY), S(300), S(UiStyle::HotkeyEditH), CurrentConfiguredHotkey()));
    control = CreateLabel(parent, S(UiStyle::ContentLeft), shortcutY + S(UiStyle::GeneralShortcutHintOffsetY),
                          S(UiStyle::GeneralGroupHintW), S(UiStyle::LabelH), text.generalShortcutHint);
    AddGeneralControl(control);

    // 合并组第三行：开机启动勾选（原独立 Startup 组迁入；Portable 提示已删）。
    HWND startupCheckbox = CreateWindowW(L"BUTTON", text.generalStartupCheck, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                         S(UiStyle::ContentLeft), shortcutY + S(UiStyle::GeneralStartupCheckInShortcutOffsetY), S(UiStyle::GeneralStartupCheckInShortcutW), S(UiStyle::CheckH), parent,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_START_WITH_WINDOWS)), GetParentInstance(parent), nullptr);
    ApplyUiFont(startupCheckbox);
    AddGeneralControl(startupCheckbox);

    const int inputY = S(UiStyle::GeneralInputGroupY);
    HWND inputGroup = CreateWindowW(L"BUTTON", text.generalInputPreviewGroupTitle, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                    S(UiStyle::GeneralGroupX), inputY, S(UiStyle::GeneralGroupW), S(UiStyle::GeneralInputGroupH), parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(inputGroup);
    AddGeneralControl(inputGroup);
    HWND partialCheckbox = CreateWindowW(L"BUTTON", text.generalPartialResult, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                         S(UiStyle::ContentLeft), inputY + S(UiStyle::GeneralInputCheckOffsetY), S(UiStyle::GeneralInputCheckW), S(UiStyle::CheckH), parent,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PARTIAL)), GetParentInstance(parent), nullptr);
    ApplyUiFont(partialCheckbox);
    AddGeneralControl(partialCheckbox);

    HWND fastPasteCheckbox = CreateWindowW(L"BUTTON", text.generalFastPaste, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                  S(UiStyle::ContentLeft + UiStyle::GeneralRestoreClipboardOffsetX), inputY + S(UiStyle::GeneralInputCheckOffsetY), S(UiStyle::GeneralRestoreClipboardW), S(UiStyle::CheckH), parent,
                                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RESTORE_CLIPBOARD)), GetParentInstance(parent), nullptr);
    ApplyUiFont(fastPasteCheckbox);
    AddGeneralControl(fastPasteCheckbox);

    // Row 2 of the same group: local voice-cleanup controls (P3).
    const int cleanupY = inputY + S(UiStyle::GeneralCleanupRowOffsetY);
    control = CreateLabel(parent, S(UiStyle::ContentLeft), cleanupY + S(UiStyle::LabelYOffset),
                          S(UiStyle::GeneralCleanupLabelW), S(UiStyle::LabelH), text.generalPunctuationLabel);
    AddGeneralControl(control);
    HWND punctCombo = CreateCombo(parent, IDC_VOICE_PUNCT_MODE,
                                  S(UiStyle::ContentLeft + UiStyle::GeneralCleanupComboOffsetX), cleanupY,
                                  S(UiStyle::GeneralCleanupComboW), S(UiStyle::ComboH));
    for (const wchar_t* label : {text.generalPunctKeepAll, text.generalPunctSpaceReplace, text.generalPunctNoEnd}) {
        ComboBox_AddString(punctCombo, label);
    }
    ComboBox_SetCurSel(punctCombo, 0);
    AddGeneralControl(punctCombo);

    HWND spacingCheckbox = CreateCheckBox(parent,
                                          IDC_VOICE_SPACING,
                                          S(UiStyle::ContentLeft + UiStyle::GeneralCleanupCheckOffsetX), cleanupY,
                                          S(UiStyle::GeneralCleanupCheckW), S(UiStyle::CheckH),
                                          text.generalSpacing);
    AddGeneralControl(spacingCheckbox);

    // Row 3 of the same group: local learning loop (P2).
    const int learningY = inputY + S(UiStyle::GeneralLearningRowOffsetY);
    HWND learningCheckbox = CreateCheckBox(parent,
                                           IDC_LEARNING_ENABLED,
                                           S(UiStyle::ContentLeft), learningY,
                                           S(UiStyle::GeneralLearningCheckW), S(UiStyle::CheckH),
                                           text.generalLearning);
    AddGeneralControl(learningCheckbox);
    HWND clearLearningBtn = CreateButton(parent,
                                         IDC_LEARNING_CLEAR,
                                         S(UiStyle::ContentLeft + UiStyle::GeneralLearningClearOffsetX), learningY,
                                         S(UiStyle::GeneralLearningClearW), S(UiStyle::ActionBtnH),
                                         text.generalClearLearned);
    AddGeneralControl(clearLearningBtn);

    const int browserY = S(UiStyle::GeneralBrowserGroupY);
    HWND browserGroup = CreateWindowW(L"BUTTON", text.generalBrowserGroupTitle, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                      S(UiStyle::GeneralGroupX), browserY, S(UiStyle::GeneralGroupW), S(UiStyle::GeneralBrowserGroupH), parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(browserGroup);
    AddGeneralControl(browserGroup);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), browserY + S(UiStyle::GeneralBrowserHintOffsetY), S(UiStyle::GeneralBrowserHintW), S(UiStyle::LabelH),
                          text.generalBrowserHint);
    AddGeneralControl(control);

    HWND statusLabel = CreateWindowW(L"STATIC", text.generalBrowserStatusChecking, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
                                     S(UiStyle::ContentLeft), browserY + S(UiStyle::GeneralBrowserStatusOffsetY), S(UiStyle::GeneralBrowserStatusW), S(UiStyle::LabelH),
                                     parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BROWSER_STATUS)), GetParentInstance(parent), nullptr);
    ApplyUiFont(statusLabel);
    AddGeneralControl(statusLabel);

    const int browserActionsY = browserY + S(UiStyle::GeneralBrowserActionsOffsetY);
    HWND regBtn = CreateButton(parent, IDC_BROWSER_REGISTER_BRIDGE, S(UiStyle::ContentLeft), browserActionsY,
                               S(UiStyle::GeneralBrowserBtnW), S(UiStyle::ActionBtnH), text.generalBrowserPairRefresh);
    AddGeneralControl(regBtn);

    const int folderBtnX = UiStyle::ContentLeft + UiStyle::GeneralBrowserBtnW + UiStyle::GeneralBrowserButtonGap;
    HWND folderBtn = CreateButton(parent, IDC_BROWSER_OPEN_FOLDER, S(folderBtnX), browserActionsY,
                                  S(UiStyle::GeneralBrowserFolderBtnW), S(UiStyle::ActionBtnH), text.generalBrowserOpenFolder);
    AddGeneralControl(folderBtn);

    const int guideBtnX = folderBtnX + UiStyle::GeneralBrowserFolderBtnW + UiStyle::GeneralBrowserButtonGap;
    HWND guideBtn = CreateButton(parent, IDC_BROWSER_SETUP_GUIDE, S(guideBtnX), browserActionsY,
                                 S(UiStyle::GeneralBrowserGuideBtnW), S(UiStyle::ActionBtnH), text.generalBrowserSetupGuide);
    AddGeneralControl(guideBtn);
    const int revokeBtnX = guideBtnX + UiStyle::GeneralBrowserGuideBtnW + UiStyle::GeneralBrowserButtonGap;
    AddGeneralControl(CreateButton(parent, IDC_BROWSER_REVOKE, S(revokeBtnX), browserActionsY,
        S(UiStyle::GeneralBrowserRevokeBtnW), S(UiStyle::ActionBtnH), text.generalBrowserUnpair));

    // Language 组：界面语言下拉（保存后重建窗口生效）。
    const int languageY = S(UiStyle::GeneralLanguageGroupY);
    HWND languageGroup = CreateWindowW(L"BUTTON", text.generalLanguageGroupTitle, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                       S(UiStyle::GeneralGroupX), languageY, S(UiStyle::GeneralGroupW), S(UiStyle::GeneralLanguageGroupH), parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(languageGroup);
    AddGeneralControl(languageGroup);
    control = CreateLabel(parent, S(UiStyle::ContentLeft), languageY + S(UiStyle::GeneralLanguageRowOffsetY + UiStyle::LabelYOffset),
                          S(UiStyle::LabelWidth), S(UiStyle::LabelH), text.generalLanguageLabel);
    AddGeneralControl(control);
    HWND languageCombo = CreateCombo(parent, IDC_UI_LANGUAGE,
                                     S(UiStyle::InputLeft), languageY + S(UiStyle::GeneralLanguageRowOffsetY),
                                     S(UiStyle::ComboW), S(UiStyle::ComboH));
    // 语言选择器永远显示自称，不受当前界面语言影响。
    for (const wchar_t* label : {L"Follow system / 跟随系统", L"English", L"中文"}) {
        ComboBox_AddString(languageCombo, label);
    }
    ComboBox_SetCurSel(languageCombo, 0);
    AddGeneralControl(languageCombo);
    control = CreateLabel(parent, S(UiStyle::ContentLeft), languageY + S(UiStyle::GeneralLanguageHintOffsetY),
                          S(UiStyle::GeneralGroupHintW), S(UiStyle::LabelH), text.generalLanguageHint);
    AddGeneralControl(control);
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
    // 下拉项顺序 Follow system/English/中文 与 UiLanguage 枚举序一致（Auto/En/Zh），
    // 映射复用 ParseUiLanguage/UiLanguageCode，由 tests/ui_strings_test.cpp 钉住。
    ComboBox_SetCurSel(GetDlgItem(parent, IDC_UI_LANGUAGE),
                       static_cast<int>(ui_strings::ParseUiLanguage(cfg.uiLanguage)));
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
    const int languageIndex = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_UI_LANGUAGE));
    cfg.uiLanguage = ui_strings::UiLanguageCode(static_cast<ui_strings::UiLanguage>(languageIndex));
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
            SetStatus(parent, ui_strings::UiText().browserPairStatus);
            MessageBoxW(parent,
                        ui_strings::UiText().browserPairGuide,
                        ui_strings::UiText().browserIntegrationTitle, MB_OK | MB_ICONINFORMATION);
        } else {
            SetStatus(parent, std::wstring(ui_strings::UiText().browserRegistrationFailed) + registered.error());
            MessageBoxW(parent, registered.error().c_str(), ui_strings::UiText().browserIntegrationTitle,
                        MB_OK | MB_ICONERROR);
        }
        return true;
    }
    if (controlId == IDC_BROWSER_REVOKE) {
        if (MessageBoxW(parent, ui_strings::UiText().browserRevokeConfirm,
            ui_strings::UiText().browserIntegrationTitle, MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return true;
        const auto revoked = browser_bridge::Bridge::Instance().RevokePairings();
        RefreshBrowserBridgeControl(parent);
        SetStatus(parent, revoked ? ui_strings::UiText().browserPairingsRevoked
                                  : std::wstring(ui_strings::UiText().browserRevokeFailed) + Utf8ToWide(revoked.error()));
        return true;
    }
    if (controlId == IDC_BROWSER_OPEN_FOLDER) {
        OpenWithShell(parent, browser_bridge_registration::ExtensionDir(),
                      ui_strings::UiText().openedExtensionFolder,
                      ui_strings::UiText().extensionFolderMissing);
        return true;
    }
    if (controlId == IDC_BROWSER_SETUP_GUIDE) {
        OpenWithShell(parent,
                      browser_bridge_registration::ExtensionDir() + L"\\README.md",
                      ui_strings::UiText().openedSetupGuide,
                      ui_strings::UiText().setupGuideMissing);
        return true;
    }
    if (controlId != IDC_LEARNING_CLEAR) return false;
    if (MessageBoxW(parent,
                    ui_strings::UiText().learningClearConfirm,
                    ui_strings::UiText().settingsTitle, MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
        return true;
    }
    if (!learning_store::Clear()) {
        SetStatus(parent, ui_strings::UiText().learningClearFailed);
        return true;
    }
    // Drop the pending reflection before reporting success. A worker that already
    // snapshotted the injection still calls learning_store::Add() when its message
    // reaches this window, and Add re-reads the (now empty) file, so without this
    // the pair would be written straight back and the status line above would be
    // a lie. The whole window is reflectionDelayMs (2.5 s by default), so
    // "dictate, then clear" hits it reliably.
    injection_ledger::Invalidate();
    SetStatus(parent, ui_strings::UiText().learningCleared);
    return true;
}

bool TabGeneral::HandleMessage(HWND parent, UINT msg, WPARAM wParam, LPARAM) {
    if (msg != WM_TIMER || wParam != reinterpret_cast<UINT_PTR>(this)) return false;
    RefreshBrowserBridgeControl(parent);
    return true;
}

} // namespace ui_tab
