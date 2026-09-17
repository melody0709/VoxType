#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "text_injector.h"

#include <imm.h>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "imm32.lib")

namespace platform {

namespace {

struct ImeStateGuard {
    HWND  targetWnd = nullptr;
    HIMC  hIMC      = nullptr;
    DWORD savedConv = 0;
    DWORD savedSent = 0;
    BOOL  savedOpen = FALSE;
    bool  active    = false;

    bool Disable() {
        targetWnd = GetForegroundWindow();
        if (!targetWnd) return false;

        HWND focused = GetFocus();
        if (focused && IsChild(targetWnd, focused)) {
            targetWnd = focused;
        }

        hIMC = ImmGetContext(targetWnd);
        if (!hIMC) return false;

        ImmGetConversionStatus(hIMC, &savedConv, &savedSent);
        savedOpen = ImmGetOpenStatus(hIMC);

        if (savedConv == IME_CMODE_ALPHANUMERIC && !savedOpen) {
            ImmReleaseContext(targetWnd, hIMC);
            hIMC = nullptr;
            return false;
        }

        ImmSetOpenStatus(hIMC, FALSE);
        ImmSetConversionStatus(hIMC, IME_CMODE_ALPHANUMERIC, 0);
        Sleep(15);
        active = true;
        return true;
    }

    void Restore() {
        if (!active || !hIMC) return;
        ImmSetConversionStatus(hIMC, savedConv, savedSent);
        ImmSetOpenStatus(hIMC, savedOpen);
        if (targetWnd) ImmReleaseContext(targetWnd, hIMC);
        hIMC = nullptr;
        active = false;
    }

    ~ImeStateGuard() { Restore(); }
};

}  // namespace

void SetClipboardText(const std::wstring& text) {
    if (text.empty() || !OpenClipboard(nullptr)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        void* ptr = GlobalLock(mem);
        if (ptr) {
            memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
            mem = nullptr;
        }
        if (mem) GlobalFree(mem);
    }
    CloseClipboard();
}

void SendCtrlV() {
    INPUT inputs[4] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
}

void SendUnicodeText(const std::wstring& text) {
    if (text.empty()) return;
    for (wchar_t ch : text) {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = ch;
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wScan = ch;
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
        Sleep(1);
    }
}

void PasteTextImeAware(const std::wstring& text, bool forceUnicodeInput) {
    if (text.empty()) return;

    if (forceUnicodeInput) {
        SendUnicodeText(text);
        return;
    }

    HWND focus = GetFocus();
    if (focus) {
        SetClipboardText(text);
        DWORD_PTR result = 0;
        SendMessageTimeoutW(focus, WM_PASTE, 0, 0, SMTO_ABORTIFHUNG, 2000, &result);
        return;
    }

    HWND fg = GetForegroundWindow();
    if (!fg) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    wchar_t processName[MAX_PATH] = {};
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        DWORD size = MAX_PATH;
        QueryFullProcessImageNameW(hProc, 0, processName, &size);
        CloseHandle(hProc);
    }

    bool isWeChat = wcsstr(processName, L"WeChat") || wcsstr(processName, L"wechat") ||
                    wcsstr(processName, L"Weixin") || wcsstr(processName, L"weixin");
    if (!isWeChat) {
        SetClipboardText(text);
        ImeStateGuard guard;
        guard.Disable();
        SendCtrlV();
        return;
    }

    for (wchar_t ch : text) {
        PostMessageW(fg, WM_CHAR, ch, 0);
        Sleep(1);
    }
}

bool ReplaceSelectionTextImeAware(const SelectionContext& selection,
                                  const std::wstring& text,
                                  std::wstring* error) {
    if (error) error->clear();
    if (text.empty()) {
        if (error) *error = L"empty replacement";
        return false;
    }
    if (!selection.Usable()) {
        if (error) *error = L"selection target unavailable";
        return false;
    }
    if (!selection_context::IsSameForegroundWindow(selection)) {
        if (error) *error = L"selection target is no longer foreground";
        return false;
    }
    if (!selection_context::VerifyCurrentSelection(selection)) {
        if (error) *error = L"selection changed while recognizing";
        return false;
    }

    if (!IsWindow(selection.focusWindow)) {
        if (error) *error = L"selection focus control unavailable";
        return false;
    }
    HWND focus = selection.focusWindow;

    DWORD currentProcessId = 0;
    GetWindowThreadProcessId(selection.targetWindow, &currentProcessId);
    if (selection.processId != 0 && currentProcessId != selection.processId) {
        if (error) *error = L"selection target process changed";
        return false;
    }
    if (selection_context::TopLevelWindow(focus) != selection.targetWindow) {
        if (error) *error = L"selection focus control changed window";
        return false;
    }

    GUITHREADINFO guiInfo = {};
    guiInfo.cbSize = sizeof(guiInfo);
    const DWORD targetThread = GetWindowThreadProcessId(selection.targetWindow, nullptr);
    if (GetGUIThreadInfo(targetThread, &guiInfo) &&
        selection.focusWindow && guiInfo.hwndFocus &&
        guiInfo.hwndFocus != selection.focusWindow) {
        if (error) *error = L"focus changed while recognizing";
        return false;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(selection.targetWindow, &pid);
    wchar_t processName[MAX_PATH] = {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process) {
        DWORD size = MAX_PATH;
        QueryFullProcessImageNameW(process, 0, processName, &size);
        CloseHandle(process);
    }
    const bool isWeChat = wcsstr(processName, L"WeChat") ||
                          wcsstr(processName, L"wechat") ||
                          wcsstr(processName, L"Weixin") ||
                          wcsstr(processName, L"weixin");
    if (isWeChat) {
        for (wchar_t ch : text) {
            if (!PostMessageW(focus, WM_CHAR, ch, 0)) {
                if (error) *error = L"WeChat WM_CHAR failed";
                return false;
            }
            Sleep(1);
        }
        return true;
    }

    selection_context::ClipboardSnapshot previousClipboard;
    if (!previousClipboard.Readable()) {
        if (error) *error = L"clipboard snapshot unavailable";
        return false;
    }
    if (!selection_context::SetClipboardText(text)) {
        if (error) *error = L"clipboard unavailable";
        return false;
    }

    DWORD_PTR result = 0;
    if (SendMessageTimeoutW(focus, WM_PASTE, 0, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK,
                            2000, &result)) {
        return true;
    }

    ImeStateGuard guard;
    guard.Disable();
    SendCtrlV();
    Sleep(100);
    return true;
}

bool IsCapsLockOn() {
    return (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
}

void SendCapsLockTap() {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CAPITAL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_CAPITAL;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

void RestoreCapsLockState(bool wasOn) {
    if (IsCapsLockOn() != wasOn) {
        SendCapsLockTap();
    }
}

}  // namespace platform
