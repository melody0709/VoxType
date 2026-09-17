#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "settings_controls.h"
#include "hotkey.h"
#include "ui_utils.h"

#include <commctrl.h>
#include <windowsx.h>
#include <shlobj.h>
#include <vector>

HWND g_settingsWindow = nullptr;
std::vector<HWND> g_recognitionControls;
std::vector<HWND> g_generalControls;
std::vector<HWND> g_llmControls;
std::vector<HWND> g_promptControls;
std::vector<HWND> g_cloudAsrControls;
std::vector<HWND> g_baiduControls;
std::vector<HWND> g_volcengineControls;
std::vector<HWND> g_qwenControls;
std::vector<HWND> g_qwenAudio3Controls;
std::vector<HWND> g_qwenAudioStreamingOnlyControls;
std::vector<HWND> g_mimoControls;
std::vector<HWND> g_maiControls;
std::vector<HWND> g_maiOpenRouterControls;
std::vector<HWND> g_maiAzureControls;
std::vector<HWND> g_doubaoImeControls;
std::vector<HWND> g_qwenFreeControls;
std::vector<HWND> g_vadFireredControls;
std::vector<HWND> g_vadSileroControls;
bool g_llmKeyVisible = false;
bool g_baiduKeyVisible = false;
bool g_baiduApiKeyVisible = false;
bool g_volcKeyVisible = false;
bool g_qwenKeyVisible = false;
bool g_mimoKeyVisible = false;
bool g_maiOpenRouterKeyVisible = false;
bool g_maiAzureKeyVisible = false;
int g_cloudProviderIdx = 0;
HWND g_cloudAsrHintControl = nullptr;

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

HINSTANCE GetParentInstance(HWND parent) {
    if (parent) {
        HINSTANCE h = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
        if (h) return h;
    }
    return GetModuleHandleW(nullptr);
}

}  // namespace

void UpdateUiScale(HWND hwnd) {
    UiStyle::Scale = DpiScaleForWindow(hwnd) * 96.0f / 144.0f;
}

int S(int px) {
    return DipToPx(static_cast<float>(px), UiStyle::Scale);
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
    HWND hwnd = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(hwnd);
    return hwnd;
}

HWND CreateHint(HWND parent, int x, int y, int w, int h, const wchar_t* text) {
    HWND hwnd = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                              x, y, w, h, parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(hwnd);
    MarkSettingsHint(hwnd);
    return hwnd;
}

HWND CreateCombo(HWND parent, int id, int x, int y, int w, int h) {
    HWND hwnd = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                              x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetParentInstance(parent), nullptr);
    ApplyUiFont(hwnd);
    return hwnd;
}

HWND CreateButton(HWND parent, int id, int x, int y, int w, int h, const wchar_t* text) {
    HWND hwnd = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
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
