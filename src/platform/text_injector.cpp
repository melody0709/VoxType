#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "text_injector.h"
#include "selection_context.h"
#include "config_store.h"
#include "app_messages.h"
#include "app_state.h"
#include "debug_logger.h"

#include <imm.h>
#include <memory>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "imm32.lib")

namespace platform {

namespace {

// Preserve the shortcut behavior used before the clipboard refactor. Some
// targets route Ctrl+V through the active IME rather than the editor.
struct ImeStateGuard {
    HWND targetWnd = nullptr;
    HIMC context = nullptr;
    DWORD conversion = 0;
    DWORD sentence = 0;
    BOOL wasOpen = FALSE;
    bool changed = false;

    void Disable() {
        targetWnd = GetForegroundWindow();
        if (!targetWnd) return;
        context = ImmGetContext(targetWnd);
        if (!context) return;
        if (!ImmGetConversionStatus(context, &conversion, &sentence)) {
            ImmReleaseContext(targetWnd, context);
            context = nullptr;
            return;
        }
        wasOpen = ImmGetOpenStatus(context);
        if (conversion == IME_CMODE_ALPHANUMERIC && !wasOpen) return;
        ImmSetOpenStatus(context, FALSE);
        ImmSetConversionStatus(context, IME_CMODE_ALPHANUMERIC, 0);
        Sleep(15);
        changed = true;
    }

    ~ImeStateGuard() {
        if (!context) return;
        if (changed) {
            ImmSetConversionStatus(context, conversion, sentence);
            ImmSetOpenStatus(context, wasOpen);
        }
        ImmReleaseContext(targetWnd, context);
    }
};

// Clipboard paste is also supported by Scintilla; the native selection reader
// in selection_context.h deliberately supports only Edit/RichEdit (UTF-16).
static bool IsStandardEditControl(HWND hwnd) {
    if (!hwnd) return false;
    wchar_t cls[64] = {};
    if (GetClassNameW(hwnd, cls, 64) <= 0) return false;
    return _wcsicmp(cls, L"Edit") == 0 ||
           _wcsnicmp(cls, L"RichEdit", 8) == 0 ||
           _wcsicmp(cls, L"Scintilla") == 0;
}

static bool SendCtrlV() {
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
    if (SendInput(4, inputs, sizeof(INPUT)) == 4) return true;
    INPUT release[2] = {inputs[2], inputs[3]};
    SendInput(2, release, sizeof(INPUT));
    return false;
}

static HWND GetFocusedWindowForForeground(HWND fg) {
    if (!fg) return nullptr;
    DWORD threadId = GetWindowThreadProcessId(fg, nullptr);
    GUITHREADINFO gti = { sizeof(gti) };
    if (GetGUIThreadInfo(threadId, &gti) && gti.hwndFocus) {
        return gti.hwndFocus;
    }
    return fg;
}

static std::unique_ptr<selection_context::ClipboardSnapshot> s_pendingClipboardRestore;
static DWORD s_expectedClipboardSeq = 0;
static bool s_clipboardPastePending = false;
static bool s_restoreAfterPaste = true;
static ULONG_PTR s_clipboardPasteToken = 0;
static UINT s_clipboardRestoreFailures = 0;
static ULONGLONG s_lastClipboardRestoreLogTick = 0;
static ULONGLONG s_clipboardPasteStartTick = 0;
static ULONGLONG s_lastClipboardPasteLogTick = 0;
static bool s_clipboardPasteWaitLogged = false;

void ReportClipboardRestoreFailure() {
    const ULONGLONG now = GetTickCount64();
    if (s_clipboardRestoreFailures < UINT_MAX) ++s_clipboardRestoreFailures;
    if (s_clipboardRestoreFailures == 1 || now - s_lastClipboardRestoreLogTick >= 60000) {
        s_lastClipboardRestoreLogTick = now;
        PostMessageW(g_mainWindow, kClipboardDiagnosticMessage, kClipboardRestoreFailed,
                     static_cast<LPARAM>(s_clipboardRestoreFailures));
    }
}

bool RestorePendingClipboard() {
    if (!s_pendingClipboardRestore) return true;
    const bool stillOurs = s_pendingClipboardRestore->FlushPending()
        ? s_pendingClipboardRestore->IsCurrentOleClipboard()
        : GetClipboardOwner() == g_mainWindow &&
          GetClipboardSequenceNumber() == s_expectedClipboardSeq;
    if (!stillOurs) {
        s_pendingClipboardRestore->Dismiss();
        s_pendingClipboardRestore.reset();
        s_clipboardPastePending = false;
        ++s_clipboardPasteToken;
        s_clipboardRestoreFailures = 0;
    } else if (s_clipboardPastePending) {
        return false;
    } else if (!s_pendingClipboardRestore->Restore()) {
        ReportClipboardRestoreFailure();
        return false;
    } else {
        s_pendingClipboardRestore.reset();
        s_clipboardRestoreFailures = 0;
    }
    if (g_mainWindow) KillTimer(g_mainWindow, kClipboardRestoreTimer);
    return true;
}

void CALLBACK ClipboardPasteComplete(HWND, UINT, ULONG_PTR token, LRESULT) {
    if (token != s_clipboardPasteToken) return;
    s_clipboardPastePending = false;
    if (!s_restoreAfterPaste && s_pendingClipboardRestore) {
        s_pendingClipboardRestore->Dismiss();
        s_pendingClipboardRestore.reset();
        s_clipboardRestoreFailures = 0;
        if (g_mainWindow) KillTimer(g_mainWindow, kClipboardRestoreTimer);
        return;
    }
    if (!RestorePendingClipboard() && g_mainWindow)
        SetTimer(g_mainWindow, kClipboardRestoreTimer, 50, nullptr);
}

void RestoreOrRetry(std::unique_ptr<selection_context::ClipboardSnapshot> snapshot,
                    DWORD expectedSequence) {
    if (!snapshot) return;
    if (!expectedSequence || GetClipboardOwner() != g_mainWindow ||
        GetClipboardSequenceNumber() != expectedSequence) {
        snapshot->Dismiss();
        return;
    }
    if (snapshot->Restore()) return;
    if (snapshot->FlushPending() && !snapshot->IsCurrentOleClipboard()) {
        snapshot->Dismiss();
        return;
    }
    s_clipboardRestoreFailures = 0;
    ReportClipboardRestoreFailure();
    s_restoreAfterPaste = true;
    s_expectedClipboardSeq = GetClipboardSequenceNumber();
    s_pendingClipboardRestore = std::move(snapshot);
    if (g_mainWindow) SetTimer(g_mainWindow, kClipboardRestoreTimer, 50, nullptr);
}

}  // namespace

void OnClipboardRestoreTimer() {
    if (RestorePendingClipboard()) return;
    if (s_clipboardPastePending) {
        const ULONGLONG now = GetTickCount64();
        if (now - s_clipboardPasteStartTick >= 10000 &&
            (!s_clipboardPasteWaitLogged || now - s_lastClipboardPasteLogTick >= 60000)) {
            s_clipboardPasteWaitLogged = true;
            s_lastClipboardPasteLogTick = now;
            PostMessageW(g_mainWindow, kClipboardDiagnosticMessage, kClipboardPasteStillPending,
                         static_cast<LPARAM>(now - s_clipboardPasteStartTick));
        }
    }
    if (g_mainWindow)
        SetTimer(g_mainWindow, kClipboardRestoreTimer, s_clipboardPastePending ? 1000 : 250, nullptr);
}

void CleanupClipboardOnExit() {
    if (g_mainWindow) KillTimer(g_mainWindow, kClipboardRestoreTimer);
    s_clipboardPastePending = false;
    ++s_clipboardPasteToken;
    if (!s_restoreAfterPaste && s_pendingClipboardRestore) {
        s_pendingClipboardRestore->Dismiss();
        s_pendingClipboardRestore.reset();
    }
    for (int attempt = 0; attempt < 5 && s_pendingClipboardRestore; ++attempt) {
        if (RestorePendingClipboard()) break;
        Sleep(50);
    }
    if (s_pendingClipboardRestore) {
        debug_log::Write("[Clipboard] Restore failed during shutdown");
        s_pendingClipboardRestore->Dismiss();
        s_pendingClipboardRestore.reset();
    }
}

bool SendUnicodeText(const std::wstring& text) {
    const HWND target = GetForegroundWindow();
    if (!text.empty() && !target) return false;
    for (wchar_t ch : text) {
        if (GetForegroundWindow() != target) return false;
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = ch;
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wScan = ch;
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        const UINT sent = SendInput(2, inputs, sizeof(INPUT));
        if (sent != 2) {
            if (sent == 1) SendInput(1, &inputs[1], sizeof(INPUT));
            return false;
        }
        Sleep(1);
    }
    return true;
}

bool PasteViaClipboard(HWND focus, const std::wstring& text, bool restoreClipboard,
                       bool* shortcutAttempted = nullptr) {
    if (shortcutAttempted) *shortcutAttempted = false;
    if (!g_mainWindow || !RestorePendingClipboard()) return false;
    const DWORD originalSequence = GetClipboardSequenceNumber();
    if (!originalSequence) return false;

    // Fast mode deliberately follows the legacy delivery contract: write the
    // transcript once and paste it as a block. Do not enumerate/materialize
    // every existing clipboard format first. Rich, delayed-rendered, or
    // application-private formats are allowed to be present; requiring a
    // complete rollback snapshot here made ordinary browser pastes fall back
    // to slow Unicode input before the write was even attempted.
    if (!restoreClipboard) {
        const auto write = selection_context::SetClipboardText(
            text, g_mainWindow, originalSequence, false);
        if (!write.written) return false;
        if (GetForegroundWindow() != selection_context::TopLevelWindow(focus) ||
            GetClipboardOwner() != g_mainWindow ||
            GetClipboardSequenceNumber() != write.sequence) {
            return false;
        }
        ImeStateGuard ime;
        ime.Disable();
        // Switching the IME can yield to the target's message loop. Recheck
        // before submitting a shortcut that might paste into another window.
        if (GetForegroundWindow() != selection_context::TopLevelWindow(focus) ||
            GetClipboardOwner() != g_mainWindow ||
            GetClipboardSequenceNumber() != write.sequence) return false;
        if (shortcutAttempted) *shortcutAttempted = true;
        return SendCtrlV();
    }

    auto snapshot = std::make_unique<selection_context::ClipboardSnapshot>();
    if (!snapshot->Readable() || GetClipboardSequenceNumber() != originalSequence) {
        snapshot->Dismiss();
        return false;
    }

    const auto write = selection_context::SetClipboardText(
        text, g_mainWindow, originalSequence, restoreClipboard);
    if (!write.written) {
        if (write.changed) RestoreOrRetry(std::move(snapshot), write.sequence);
        else snapshot->Dismiss();
        return false;
    }

    s_expectedClipboardSeq = write.sequence;
    s_pendingClipboardRestore = std::move(snapshot);
    s_clipboardPastePending = true;
    s_restoreAfterPaste = restoreClipboard;
    s_clipboardRestoreFailures = 0;
    s_clipboardPasteStartTick = GetTickCount64();
    s_clipboardPasteWaitLogged = false;
    const ULONG_PTR token = ++s_clipboardPasteToken;
    SetTimer(g_mainWindow, kClipboardRestoreTimer, 10000, nullptr);
    if (!SendMessageCallbackW(focus, WM_PASTE, 0, 0, ClipboardPasteComplete, token)) {
        s_clipboardPastePending = false;
        s_restoreAfterPaste = true;
        if (!RestorePendingClipboard() && g_mainWindow)
            SetTimer(g_mainWindow, kClipboardRestoreTimer, 50, nullptr);
        return false;
    }
    return true;
}

bool PasteTextImeAware(const std::wstring& text, bool forceUnicodeInput) {
    if (text.empty()) return true;

    if (forceUnicodeInput) {
        return SendUnicodeText(text);
    }

    HWND fg = GetForegroundWindow();
    if (!fg) return false;

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
    if (isWeChat) {
        for (wchar_t ch : text) {
            if (GetForegroundWindow() != fg) return false;
            if (!PostMessageW(fg, WM_CHAR, ch, 0)) return false;
            Sleep(1);
        }
        return true;
    }

    HWND focus = GetFocusedWindowForForeground(fg);
    if (!g_config.restoreClipboardAfterPaste) {
        bool shortcutAttempted = false;
        if (PasteViaClipboard(focus, text, false, &shortcutAttempted)) return true;
        return !shortcutAttempted && GetForegroundWindow() == fg && SendUnicodeText(text);
    }
    // There is no acknowledgement that an asynchronous Ctrl+V has consumed
    // the clipboard. Protected mode uses Unicode for nonstandard controls.
    if (!IsStandardEditControl(focus)) {
        return SendUnicodeText(text);
    }
    return PasteViaClipboard(focus, text, true) ||
           (GetForegroundWindow() == fg && SendUnicodeText(text));
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
    if (!selection_context::IsSameForegroundWindow(selection)) {
        if (error) *error = L"selection target is no longer foreground";
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
            if (!selection_context::IsSameForegroundWindow(selection)) {
                if (error) *error = L"selection target is no longer foreground";
                return false;
            }
            if (!PostMessageW(focus, WM_CHAR, ch, 0)) {
                if (error) *error = L"WeChat WM_CHAR failed";
                return false;
            }
            Sleep(1);
        }
        return true;
    }

    if (g_config.restoreClipboardAfterPaste && !IsStandardEditControl(focus)) {
        if (!SendUnicodeText(text)) {
            if (error) *error = L"Unicode input failed";
            return false;
        }
        return true;
    }
    bool shortcutAttempted = false;
    if (!PasteViaClipboard(focus, text, g_config.restoreClipboardAfterPaste,
                           &shortcutAttempted)) {
        if (shortcutAttempted) {
            if (error) *error = L"paste shortcut failed";
            return false;
        }
        // A previously queued paste can still change this selection. Typing
        // a replacement now would race that paste; fail without a second edit.
        if (s_clipboardPastePending) {
            if (error) *error = L"previous clipboard paste is still pending";
            return false;
        }
        if (!selection_context::VerifyCurrentSelection(selection) ||
            !selection_context::IsSameForegroundWindow(selection) ||
            GetFocusedWindowForForeground(selection.targetWindow) != focus) {
            if (error) *error = L"selection or focus changed before Unicode fallback";
            return false;
        }
        if (!SendUnicodeText(text)) {
            if (error) *error = L"Unicode replacement failed";
            return false;
        }
    }
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
