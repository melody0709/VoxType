#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "settings_controls.h"
#include "settings.h"
#include "ui_theme.h"
#include "hotkey.h"
#include "ui_utils.h"
#include "ui_strings.h"

#include <commctrl.h>
#include <windowsx.h>
#include <shlobj.h>
#include <array>
#include <vector>

HWND g_settingsWindow = nullptr;

namespace {

// 语言切换重建窗口时的一次性摆放快照（重建前写入，新窗口创建后取走并清空）。
// 必须整份保存 WINDOWPLACEMENT：它的 rcNormalPosition 是**工作区坐标**（不含任务栏），
// 直接塞进 CreateWindowExW（屏幕坐标）会在任务栏位于顶部/左侧时让窗口每次漂移；
// SetWindowPlacement 内部完成换算，所以恢复只能走它。
WINDOWPLACEMENT s_restorePlacement = {};
bool s_hasRestorePlacement = false;

void CapturePlacement(HWND hwnd) {
    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (!hwnd || !GetWindowPlacement(hwnd, &placement)) return;
    s_restorePlacement = placement;
    s_hasRestorePlacement = true;
}

} // namespace

// Settings 主窗口的动态布局：Tab 区与底栏按钮随客户区尺寸走，不依赖固定窗口高。
// （原先在 settings.cpp，为给语言切换接线腾出行数预算移入本模块。）
void LayoutSettingsWindow(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int margin = S(UiStyle::Margin);
    const int footerHeight = S(UiStyle::FooterHeight);
    const int footerTop = (rc.bottom - footerHeight > S(UiStyle::FooterMinTop)) ? rc.bottom - footerHeight : S(UiStyle::FooterMinTop);
    const int tabBottom = footerTop - S(4);
    HWND tab = GetDlgItem(hwnd, IDC_SETTINGS_TAB);
    if (tab) {
        MoveWindow(tab, margin, S(12), rc.right - margin * 2, tabBottom - S(12), TRUE);
    }
    const int availableFooterH = rc.bottom - footerTop;
    const int btnY = footerTop + (availableFooterH - S(UiStyle::ActionBtnH)) / 2;
    HWND status = GetDlgItem(hwnd, IDC_STATUS);
    if (status) {
        MoveWindow(status, margin, btnY + S(4), rc.right - margin * 2 - S(300), S(28), TRUE);
    }
    HWND save = GetDlgItem(hwnd, IDC_SAVE), close = GetDlgItem(hwnd, IDC_CANCEL);
    if (save) MoveWindow(save, rc.right - margin - S(192), btnY, S(UiStyle::FooterBtnW), S(UiStyle::ActionBtnH), TRUE);
    if (close) MoveWindow(close, rc.right - margin - S(UiStyle::FooterBtnW), btnY, S(UiStyle::FooterBtnW), S(UiStyle::ActionBtnH), TRUE);
}

void DefaultSettingsWindowRect(HWND owner, RECT& frame) {
    UpdateUiScale(owner);
    const int w = S(UiStyle::SettingsWindowW);
    const int h = S(UiStyle::SettingsWindowH);
    const RECT work = GetWorkAreaForWindow(owner);
    frame.left = work.left + (work.right - work.left - w) / 2;
    frame.top = work.top + (work.bottom - work.top - h) / 2;
    frame.right = frame.left + w;
    frame.bottom = frame.top + h;
}

bool ApplyCapturedSettingsPlacement(HWND hwnd, const RECT& fallbackFrame) {
    if (!s_hasRestorePlacement || !hwnd) return false;
    s_hasRestorePlacement = false;
    const bool maximized = s_restorePlacement.showCmd == SW_SHOWMAXIMIZED;
    WINDOWPLACEMENT placement = s_restorePlacement;
    placement.length = sizeof(placement);
    // 位置/尺寸交给系统换算（工作区坐标 → 屏幕坐标）；最大化态留给调用方的 ShowWindow，
    // 两条路径互不覆盖。
    placement.showCmd = SW_SHOWNORMAL;
    SetWindowPlacement(hwnd, &placement);
    // 旧位置可能落在已断开的显示器上：用窗口矩形（屏幕坐标，隐藏窗口也有效）判定，
    // 不在任何显示器上就回落到居中，别把窗口创建到屏幕外。
    RECT placed = {};
    GetWindowRect(hwnd, &placed);
    if (!MonitorFromRect(&placed, MONITOR_DEFAULTTONULL)) {
        SetWindowPos(hwnd, nullptr, fallbackFrame.left, fallbackFrame.top,
                     fallbackFrame.right - fallbackFrame.left,
                     fallbackFrame.bottom - fallbackFrame.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return maximized;
}

void RebuildSettingsWindowForLanguage(HWND hwnd, bool fallbackAdjusted) {
    HWND owner = GetWindow(hwnd, GW_OWNER);
    CapturePlacement(hwnd);
    if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
    ShowSettingsWindow(owner);
    if (g_settingsWindow) {
        // 新窗口 WM_CREATE 已写过 Ready.，这里补回保存反馈（表已是新语言）。
        SetStatus(g_settingsWindow, fallbackAdjusted
            ? ui_strings::UiText().statusSavedFallbackDisabled
            : ui_strings::UiText().statusSaved);
    }
}

void SetStatus(HWND hwnd, const std::wstring& text) {
    HWND status = GetDlgItem(hwnd, IDC_STATUS);
    if (!status && g_settingsWindow) {
        status = GetDlgItem(g_settingsWindow, IDC_STATUS);
    }
    if (status) {
        SetWindowTextW(status, text.c_str());
    }
}

namespace {

constexpr wchar_t kSettingsHintProperty[] = L"VoxType.SettingsHint";

struct QwenLanguageOption {
    const wchar_t* label;
    const wchar_t* code;
};

// 语言显示名随界面语言变化，禁止 static 缓存 UiText()，每次调用现取；
// code 是配置值（参与 cfg.qwenLanguage 比较），保持原样字面量。
std::array<QwenLanguageOption, 28> QwenLanguages() {
    const auto& text = ui_strings::UiText();
    return {{
        {text.ctlQwenLangAuto, L""},
        {text.ctlQwenLangZh, L"zh"},
        {text.ctlQwenLangYue, L"yue"},
        {text.ctlQwenLangEn, L"en"},
        {text.ctlQwenLangJa, L"ja"},
        {text.ctlQwenLangDe, L"de"},
        {text.ctlQwenLangKo, L"ko"},
        {text.ctlQwenLangRu, L"ru"},
        {text.ctlQwenLangFr, L"fr"},
        {text.ctlQwenLangPt, L"pt"},
        {text.ctlQwenLangAr, L"ar"},
        {text.ctlQwenLangIt, L"it"},
        {text.ctlQwenLangEs, L"es"},
        {text.ctlQwenLangHi, L"hi"},
        {text.ctlQwenLangId, L"id"},
        {text.ctlQwenLangTh, L"th"},
        {text.ctlQwenLangTr, L"tr"},
        {text.ctlQwenLangUk, L"uk"},
        {text.ctlQwenLangVi, L"vi"},
        {text.ctlQwenLangCs, L"cs"},
        {text.ctlQwenLangDa, L"da"},
        {text.ctlQwenLangFil, L"fil"},
        {text.ctlQwenLangFi, L"fi"},
        {text.ctlQwenLangIs, L"is"},
        {text.ctlQwenLangMs, L"ms"},
        {text.ctlQwenLangNo, L"no"},
        {text.ctlQwenLangPl, L"pl"},
        {text.ctlQwenLangSv, L"sv"},
    }};
}

}  // namespace

void UpdateUiScaleForDpi(UINT dpi) {
    if (dpi == 0) dpi = 96;
    UiStyle::Scale = static_cast<float>(dpi) / 144.0f;
}

void UpdateUiScale(HWND hwnd) {
    UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
    if (dpi == 0) dpi = GetDpiForSystem();
    UpdateUiScaleForDpi(dpi);
}

RECT GetWorkAreaForWindow(HWND hwnd) {
    RECT work = {};
    HMONITOR hMon = MonitorFromWindow(hwnd ? hwnd : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(hMon, &mi)) {
        work = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }
    return work;
}

int S(int px) {
    return DipToPx(static_cast<float>(px), UiStyle::Scale);
}

void HandleSettingsDpiChanged(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    const UINT newDpi = HIWORD(wParam);
    UpdateUiScaleForDpi(newDpi);
    const int targetW = S(UiStyle::SettingsWindowW);
    const int targetH = S(UiStyle::SettingsWindowH);
    RECT* suggested = reinterpret_cast<RECT*>(lParam);
    if (suggested) {
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     targetW, targetH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        SetWindowPos(hwnd, nullptr, 0, 0, targetW, targetH,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    HFONT newFont = ui_theme::UiFontForDpi(newDpi);
    EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(newFont));
    LayoutSettingsWindow(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void MarkSettingsHint(HWND hwnd) {
    if (hwnd) SetPropW(hwnd, kSettingsHintProperty, reinterpret_cast<HANDLE>(1));
}

bool IsSettingsHint(HWND hwnd) {
    return hwnd && GetPropW(hwnd, kSettingsHintProperty) != nullptr;
}

void OpenAsrDebugLog(HWND hwnd, const wchar_t* fileName) {
    if (!fileName || !*fileName) return;
    const auto& text = ui_strings::UiText();
    wchar_t tempPath[MAX_PATH] = {};
    const DWORD length = GetTempPathW(MAX_PATH, tempPath);
    if (length == 0 || length >= MAX_PATH) {
        MessageBoxW(hwnd, text.ctlTempLogDirFailed, text.ctlOpenLogTitle, MB_OK | MB_ICONERROR);
        return;
    }
    std::wstring path(tempPath, length);
    path += fileName;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        MessageBoxW(hwnd, text.ctlDebugLogFileFailed, text.ctlOpenLogTitle, MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(file);
    const HINSTANCE result = ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(hwnd, text.ctlNoAppForLogFile, text.ctlOpenLogTitle, MB_OK | MB_ICONERROR);
    }
}

HWND CreateLabel(HWND parent, int x, int y, int w, int h, const wchar_t* text) {
    HWND hwnd = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, x, y, w, h, parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(hwnd);
    return hwnd;
}

HWND CreateHint(HWND parent, int x, int y, int w, int h, const wchar_t* text) {
    HWND hwnd = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                              x, y, w, h, parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(hwnd);
    MarkSettingsHint(hwnd);
    return hwnd;
}

HWND CreateCombo(HWND parent, int id, int x, int y, int w, int h) {
    HWND hwnd = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                              x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetParentInstance(parent), nullptr);
    ApplyUiFont(hwnd);
    return hwnd;
}

HWND CreateButton(HWND parent, int id, int x, int y, int w, int h, const wchar_t* text) {
    HWND hwnd = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                              x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetParentInstance(parent), nullptr);
    ApplyUiFont(hwnd);
    return hwnd;
}

HWND CreateCheckBox(HWND parent, int id, int x, int y, int w, int h, const wchar_t* text) {
    HWND hwnd = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                              x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetParentInstance(parent), nullptr);
    ApplyUiFont(hwnd);
    return hwnd;
}

std::wstring ComboText(HWND combo) {
    int sel = ComboBox_GetCurSel(combo);
    if (sel < 0) return {};
    wchar_t buf[256] = {};
    ComboBox_GetLBText(combo, sel, buf);
    return buf;
}

void BrowseModelDirectory(HWND hwnd) {
    BROWSEINFOW bi = {};
    bi.hwndOwner = hwnd;
    bi.lpszTitle = ui_strings::UiText().ctlSelectModelDir;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH] = {};
    constexpr int kModelDirId = 2002;
    if (SHGetPathFromIDListW(pidl, path)) {
        SetWindowTextW(GetDlgItem(hwnd, kModelDirId), path);
    }
    CoTaskMemFree(pidl);
}

int QwenLanguageIndexFromCode(const std::wstring& code) {
    const auto langs = QwenLanguages();
    for (int i = 0; i < static_cast<int>(langs.size()); ++i) {
        if (code == langs[i].code) return i;
    }
    return 0;
}

const wchar_t* QwenLanguageCodeFromIndex(int index) {
    const auto langs = QwenLanguages();
    if (index >= 0 && index < static_cast<int>(langs.size())) {
        return langs[index].code;
    }
    return L"";
}

void PopulateQwenLanguageCombo(HWND combo) {
    if (!combo) return;
    for (const auto& lang : QwenLanguages()) {
        ComboBox_AddString(combo, lang.label);
    }
}

HINSTANCE GetParentInstance(HWND parent) {
    if (parent) {
        HINSTANCE h = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
        if (h) return h;
    }
    return GetModuleHandleW(nullptr);
}

std::wstring GetControlText(HWND hwnd, int id, size_t capacity) {
    HWND control = GetDlgItem(hwnd, id);
    if (!control) return {};
    const int textLength = GetWindowTextLengthW(control);
    if (textLength <= 0) return {};
    size_t allocSize = static_cast<size_t>(textLength) + 1;
    if (capacity > allocSize) {
        allocSize = capacity;
    }
    std::wstring value(allocSize, L'\0');
    const int length = GetWindowTextW(control, value.data(), static_cast<int>(value.size()));
    if (length <= 0) return {};
    value.resize(static_cast<size_t>(length));
    return value;
}

std::atomic<uint64_t> g_sharedTestGeneration{0};

void PostSharedTestResult(HWND hwnd, uint64_t generation, bool ok,
                          std::wstring message) {
    auto* payload = new std::wstring(std::move(message));
    const WPARAM packed = static_cast<WPARAM>(
        (generation << 1) | (ok ? 0ULL : 1ULL));
    if (!PostMessageW(hwnd, kSharedTestResultMessage, packed,
                      reinterpret_cast<LPARAM>(payload))) {
        delete payload;
    }
}
