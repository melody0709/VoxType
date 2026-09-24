#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "settings_controls.h"
#include "settings.h"
#include "ui_theme.h"
#include "hotkey.h"
#include "ui_utils.h"

#include <commctrl.h>
#include <windowsx.h>
#include <shlobj.h>
#include <vector>

HWND g_settingsWindow = nullptr;

namespace {

constexpr wchar_t kSettingsHintProperty[] = L"VoxType.SettingsHint";

struct QwenLanguageOption {
    const wchar_t* label;
    const wchar_t* code;
};

constexpr QwenLanguageOption kQwenLanguages[] = {
    {L"Auto", L""},
    {L"Chinese (zh)", L"zh"},
    {L"Cantonese (yue)", L"yue"},
    {L"English (en)", L"en"},
    {L"Japanese (ja)", L"ja"},
    {L"German (de)", L"de"},
    {L"Korean (ko)", L"ko"},
    {L"Russian (ru)", L"ru"},
    {L"French (fr)", L"fr"},
    {L"Portuguese (pt)", L"pt"},
    {L"Arabic (ar)", L"ar"},
    {L"Italian (it)", L"it"},
    {L"Spanish (es)", L"es"},
    {L"Hindi (hi)", L"hi"},
    {L"Indonesian (id)", L"id"},
    {L"Thai (th)", L"th"},
    {L"Turkish (tr)", L"tr"},
    {L"Ukrainian (uk)", L"uk"},
    {L"Vietnamese (vi)", L"vi"},
    {L"Czech (cs)", L"cs"},
    {L"Danish (da)", L"da"},
    {L"Filipino (fil)", L"fil"},
    {L"Finnish (fi)", L"fi"},
    {L"Icelandic (is)", L"is"},
    {L"Malay (ms)", L"ms"},
    {L"Norwegian (no)", L"no"},
    {L"Polish (pl)", L"pl"},
    {L"Swedish (sv)", L"sv"},
};

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
    wchar_t tempPath[MAX_PATH] = {};
    const DWORD length = GetTempPathW(MAX_PATH, tempPath);
    if (length == 0 || length >= MAX_PATH) {
        MessageBoxW(hwnd, L"Unable to resolve the temporary log directory.", L"Open log", MB_OK | MB_ICONERROR);
        return;
    }
    std::wstring path(tempPath, length);
    path += fileName;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        MessageBoxW(hwnd, L"Unable to create or open the debug log file.", L"Open log", MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(file);
    const HINSTANCE result = ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(hwnd, L"No application is associated with the debug log file.", L"Open log", MB_OK | MB_ICONERROR);
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
    bi.lpszTitle = L"Select model directory";
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
    constexpr int count = static_cast<int>(sizeof(kQwenLanguages) / sizeof(kQwenLanguages[0]));
    for (int i = 0; i < count; ++i) {
        if (code == kQwenLanguages[i].code) return i;
    }
    return 0;
}

const wchar_t* QwenLanguageCodeFromIndex(int index) {
    constexpr int count = static_cast<int>(sizeof(kQwenLanguages) / sizeof(kQwenLanguages[0]));
    if (index >= 0 && index < count) {
        return kQwenLanguages[index].code;
    }
    return L"";
}

void PopulateQwenLanguageCombo(HWND combo) {
    if (!combo) return;
    for (const auto& lang : kQwenLanguages) {
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
