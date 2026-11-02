#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "settings.h"
#include "config_store.h"
#include "app_state.h"
#include "app_messages.h"
#include "ui_types.h"
#include "ui_strings.h"
#include "ui_theme.h"
#include "ui_utils.h"
#include "asr_probe_service.h"

#include "tab_general.h"
#include "tab_speech_engine.h"
#include "tab_vocabulary.h"
#include "tab_llm.h"
#include "tab_advanced.h"
#include "provider_qwen_free.h"

#include <commctrl.h>
#include <windowsx.h>
#include <memory>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

float UiStyle::Scale = 1.0f;

namespace {

ui_tab::TabGeneral s_tabGeneral;
ui_tab::TabSpeechEngine s_tabSpeechEngine;
ui_tab::TabVocabulary s_tabVocabulary;
ui_tab::TabLlm s_tabLlm;
ui_tab::TabAdvanced s_tabAdvanced;

constexpr size_t kTabCount = 5;
ui_tab::ISettingsTab* const s_tabs[kTabCount] = {
    &s_tabGeneral, &s_tabSpeechEngine, &s_tabVocabulary, &s_tabLlm, &s_tabAdvanced
};

} // namespace

void ShowSettingsPage(HWND hwnd, int page) {
    for (size_t i = 0; i < kTabCount; ++i) s_tabs[i]->Show(static_cast<int>(i) == page);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void LoadSettingsControls(HWND hwnd) {
    for (auto* t : s_tabs) t->LoadControls(hwnd, g_config);
    SetStatus(hwnd, ui_strings::UiText().statusReady);
}

void SaveSettingsControls(HWND hwnd) {
    if (!s_tabGeneral.SaveStartupRegistration(hwnd)) return;

    const std::wstring oldQwenApiKey = g_config.qwenApiKey;
    const std::wstring oldQwenStreamingBaseUrl = g_config.qwenAudioStreamingBaseUrl;
    const std::wstring oldQwenModel = g_config.qwenModel;

    for (auto* t : s_tabs) t->SaveControls(hwnd, g_config);

    const bool fallbackAdjusted = (g_config.fallbackAsrBackend == g_config.asrBackend);
    if (fallbackAdjusted) {
        g_config.fallbackAsrBackend = L"none";
    }

    const bool qwenStreamingIdentityChanged =
        oldQwenApiKey != g_config.qwenApiKey ||
        Trim(oldQwenStreamingBaseUrl) != Trim(g_config.qwenAudioStreamingBaseUrl) ||
        oldQwenModel != g_config.qwenModel;
    if (qwenStreamingIdentityChanged) {
        asr_probe::InvalidateQwenStreamingConnections();
    }

    SaveConfig(g_config);
    // 语言变更的生效方式是重建整个 Settings 窗口（控件与 Tab 名全部换表）。先切表再取
    // 状态文案，重建后的新窗口补写它；否则新窗口 WM_CREATE 的 "Ready." 会盖掉保存反馈。
    const bool languageChanged = ui_strings::ApplyUiLanguageFromConfig(g_config.uiLanguage);
    if (languageChanged) {
        PostMessageW(hwnd, kUiLanguageReapplyMessage, fallbackAdjusted ? 1 : 0, 0);
    } else {
        SetStatus(hwnd, fallbackAdjusted
            ? ui_strings::UiText().statusSavedFallbackDisabled
            : ui_strings::UiText().statusSaved);
    }
    PostMessageW(g_mainWindow, kReloadMessage, 0, 0);
}

void CloseSettingsWindow(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wParam), &rc, ui_theme::SettingsBgBrush());
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, ui_theme::SettingsBgBrush());

        HPEN line = CreatePen(PS_SOLID, 1, UiStyle::DividerColor);
        HGDIOBJ oldPen = SelectObject(hdc, line);
        const int footerHeight = S(UiStyle::FooterHeight);
        const int footerTop = (rc.bottom - footerHeight > S(UiStyle::FooterMinTop)) ? rc.bottom - footerHeight : S(UiStyle::FooterMinTop);
        const int margin = S(24);
        MoveToEx(hdc, margin, footerTop, nullptr);
        LineTo(hdc, rc.right - margin, footerTop);
        SelectObject(hdc, oldPen);
        DeleteObject(line);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        LayoutSettingsWindow(hwnd);
        return 0;
    case WM_CTLCOLORDLG:
        return reinterpret_cast<LRESULT>(ui_theme::SettingsBgBrush());
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND ctl = reinterpret_cast<HWND>(lParam);
        if (IsSettingsHint(ctl)) {
            SetTextColor(hdc, UiStyle::HintTextColor);
        } else {
            SetTextColor(hdc, UiStyle::TextColor);
        }
        SetBkColor(hdc, UiStyle::BgColor);
        return reinterpret_cast<LRESULT>(ui_theme::SettingsBgBrush());
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, UiStyle::InputTextColor);
        SetBkColor(hdc, UiStyle::ControlBgColor);
        return reinterpret_cast<LRESULT>(ui_theme::ControlBgBrush());
    }
    case WM_CREATE: {
        g_instance = reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance;
        UpdateUiScale(hwnd);

        HWND tab = CreateWindowW(WC_TABCONTROLW, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
                                 S(UiStyle::Margin), S(16), S(786), S(330), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SETTINGS_TAB)), g_instance, nullptr);
        ApplyUiFont(tab);
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        for (auto* name : {ui_strings::UiText().tabGeneral, ui_strings::UiText().tabSpeechEngine,
                           ui_strings::UiText().tabVocabulary, ui_strings::UiText().tabLlm,
                           ui_strings::UiText().tabAdvanced}) {
            item.pszText = const_cast<LPWSTR>(name);
            TabCtrl_InsertItem(tab, 100, &item);
        }

        for (auto* t : s_tabs) t->CreateControls(hwnd);

        HWND status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
                                    S(UiStyle::Margin) * 2, S(UiStyle::FooterMinTop) + S(21), S(520), S(UiStyle::LabelH), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)), g_instance, nullptr);
        ApplyUiFont(status);
        CreateButton(hwnd, IDC_SAVE, S(626), S(UiStyle::FooterMinTop) + S(21), S(UiStyle::FooterBtnW), S(UiStyle::ActionBtnH), ui_strings::UiText().footerSave);
        CreateButton(hwnd, IDC_CANCEL, S(726), S(UiStyle::FooterMinTop) + S(21), S(UiStyle::FooterBtnW), S(UiStyle::ActionBtnH), ui_strings::UiText().footerClose);

        LoadSettingsControls(hwnd);
        ShowSettingsPage(hwnd, 0);
        LayoutSettingsWindow(hwnd);
        return 0;
    }
    case WM_COMMAND: {
        const WORD controlId = LOWORD(wParam);
        const WORD notifyCode = HIWORD(wParam);
        HWND controlHwnd = reinterpret_cast<HWND>(lParam);

        if (controlId == IDC_SAVE || controlId == IDOK) {
            SaveSettingsControls(hwnd);
            return 0;
        }
        if (controlId == IDC_CANCEL || controlId == IDCANCEL) {
            CloseSettingsWindow(hwnd);
            return 0;
        }

        const int curPage = TabCtrl_GetCurSel(GetDlgItem(hwnd, IDC_SETTINGS_TAB));
        if (curPage >= 0 && curPage < static_cast<int>(kTabCount) &&
            s_tabs[curPage]->HandleCommand(hwnd, notifyCode, controlId, controlHwnd)) return 0;
        for (size_t i = 0; i < kTabCount; ++i) {
            if (static_cast<int>(i) != curPage && s_tabs[i]->HandleCommand(hwnd, notifyCode, controlId, controlHwnd)) return 0;
        }

        return 0;
    }
    case WM_NOTIFY: {
        NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
        if (hdr && hdr->idFrom == IDC_SETTINGS_TAB && hdr->code == TCN_SELCHANGE) {
            ShowSettingsPage(hwnd, TabCtrl_GetCurSel(GetDlgItem(hwnd, IDC_SETTINGS_TAB)));
            // Switching pages hides the previous page's controls. Do not rely on the
            // tab control taking focus from the click: if the focused control just
            // became invisible (e.g. the shortcut field, which suspends the global
            // hotkey listener while it holds focus), move focus to the tab so the
            // listener resumes and the hotkey keeps working on the newly shown page.
            HWND focus = GetFocus();
            if (focus && IsChild(hwnd, focus) && !IsWindowVisible(focus)) {
                SetFocus(GetDlgItem(hwnd, IDC_SETTINGS_TAB));
            }
            return 0;
        }
        break;
    }
    case kSharedTestResultMessage: {
        std::unique_ptr<std::wstring> msg(reinterpret_cast<std::wstring*>(lParam));
        const uint64_t generation = static_cast<uint64_t>(wParam) >> 1;
        if (generation != g_sharedTestGeneration.load(std::memory_order_relaxed)) {
            return 0;
        }
        const bool ok = (static_cast<uint64_t>(wParam) & 1) == 0;
        if (ok) {
            SetStatus(hwnd, msg ? msg->c_str() : ui_strings::UiText().testOk);
        } else {
            std::wstring shortMsg = ui_strings::UiText().testFailedShort;
            if (msg) {
                size_t nl = msg->find(L'\n');
                shortMsg = std::wstring(ui_strings::UiText().testFailedPrefix) + (nl != std::wstring::npos ? msg->substr(0, nl) : *msg);
                MessageBoxW(hwnd, msg->c_str(), ui_strings::UiText().testFailedTitle, MB_ICONERROR | MB_OK);
            }
            SetStatus(hwnd, shortMsg.c_str());
        }
        return 0;
    }
    case kDoubaoImeSettingsCredentialsMessage: {
        const uint64_t generation = static_cast<uint64_t>(wParam);
        std::unique_ptr<asr_probe::DoubaoCredentials> creds(
            reinterpret_cast<asr_probe::DoubaoCredentials*>(lParam));
        if (generation != g_sharedTestGeneration.load(std::memory_order_relaxed)) {
            return 0;
        }
        EnableWindow(GetDlgItem(hwnd, IDC_DOUBAO_IME_TEST), TRUE);
        if (creds) {
            g_config.doubaoImeDeviceId = creds->deviceId;
            g_config.doubaoImeCdid = creds->cdid;
            g_config.doubaoImeToken = creds->token;
            SaveConfig(g_config);
        }
        ui_provider::RefreshDoubaoImeStatus(hwnd);
        return 0;
    }
    case kDoubaoImeSettingsRefreshMessage:
        ui_provider::RefreshDoubaoImeStatus(hwnd);
        return 0;
    case kUiLanguageReapplyMessage:
        RebuildSettingsWindowForLanguage(hwnd, wParam != 0);
        return 0;
    case WM_DPICHANGED: {
        const int curPage = TabCtrl_GetCurSel(GetDlgItem(hwnd, IDC_SETTINGS_TAB));
        const bool startupChecked = Button_GetCheck(GetDlgItem(hwnd, IDC_START_WITH_WINDOWS)) == BST_CHECKED;

        HWND focused = GetFocus();
        const int focusedId = focused ? GetDlgCtrlID(focused) : 0;
        DWORD selStart = 0, selEnd = 0;
        if (focused && focusedId > 0) {
            SendMessageW(focused, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
        }

        struct SavedEdit { int id; std::wstring text; };
        std::vector<SavedEdit> savedEdits;
        EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
            wchar_t cls[32] = {};
            if (GetClassNameW(child, cls, static_cast<int>(std::size(cls))) > 0 && _wcsicmp(cls, L"Edit") == 0) {
                const int id = GetDlgCtrlID(child);
                if (id > 0 && id != IDC_HOTKEY) {
                    const int len = GetWindowTextLengthW(child);
                    std::wstring buf(len, L'\0');
                    if (len > 0) GetWindowTextW(child, buf.data(), len + 1);
                    reinterpret_cast<std::vector<SavedEdit>*>(lp)->push_back({id, std::move(buf)});
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&savedEdits));

        Config draft = g_config;
        for (auto* t : s_tabs) t->SaveControls(hwnd, draft);

        HandleSettingsDpiChanged(hwnd, wParam, lParam);

        for (auto* t : s_tabs) t->DestroyControls();
        for (auto* t : s_tabs) t->CreateControls(hwnd);
        for (auto* t : s_tabs) t->LoadControls(hwnd, draft);

        for (const auto& edit : savedEdits) {
            HWND hEdit = GetDlgItem(hwnd, edit.id);
            if (hEdit) SetWindowTextW(hEdit, edit.text.c_str());
        }

        Button_SetCheck(GetDlgItem(hwnd, IDC_START_WITH_WINDOWS), startupChecked ? BST_CHECKED : BST_UNCHECKED);
        ShowSettingsPage(hwnd, curPage >= 0 ? curPage : 0);

        if (focusedId > 0) {
            HWND newFocus = GetDlgItem(hwnd, focusedId);
            if (newFocus) {
                SetFocus(newFocus);
                SendMessageW(newFocus, EM_SETSEL, selStart, selEnd);
            }
        }
        return 0;
    }
    case WM_DESTROY:
        g_sharedTestGeneration.fetch_add(1, std::memory_order_relaxed);
        ui_provider::CancelQwenFreeTests();
        for (auto* t : s_tabs) t->DestroyControls();
        if (g_settingsWindow == hwnd) g_settingsWindow = nullptr;
        // The listener stays installed for the process lifetime; only make sure no
        // destroy path can leave it suspended.
        SetHotkeyListenerSuspended(false);
        return 0;
    case WM_CLOSE:
        CloseSettingsWindow(hwnd);
        return 0;
    default:
        for (auto* t : s_tabs) {
            if (t->HandleMessage(hwnd, msg, wParam, lParam)) {
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowSettingsWindow(HWND owner) {
    // 已保存的界面语言在创建前应用：重启后首次打开也按配置语言渲染。
    ui_strings::ApplyUiLanguageFromConfig(g_config.uiLanguage);
    bool maximized = false;
    if (!g_settingsWindow) {
        RECT frame = {};
        DefaultSettingsWindowRect(owner, frame);
        g_settingsWindow = CreateWindowExW(
            WS_EX_APPWINDOW, kSettingsClass, ui_strings::UiText().settingsTitle,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
            frame.left, frame.top, frame.right - frame.left, frame.bottom - frame.top,
            owner, nullptr, g_instance, nullptr);
        // 语言切换重建：恢复销毁前的摆放（坐标换算与最大化态分开处理）。
        maximized = ApplyCapturedSettingsPlacement(g_settingsWindow, frame);
    }
    if (g_settingsWindow) {
        ui_tab::RefreshStartupRegistrationControl(g_settingsWindow, true);
        ShowWindow(g_settingsWindow, maximized ? SW_SHOWMAXIMIZED : SW_SHOW);
        SetForegroundWindow(g_settingsWindow);
    }
}

bool ProcessSettingsDialogMessage(MSG* msg) {
    if (!g_settingsWindow || !IsWindow(g_settingsWindow) || !IsWindowVisible(g_settingsWindow)) return false;
    return IsDialogMessageW(g_settingsWindow, msg) != FALSE;
}
