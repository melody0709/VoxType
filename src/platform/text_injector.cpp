#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "text_injector.h"
#include "selection_context.h"
#include "config_store.h"
#include "app_messages.h"
#include "app_state.h"
#include "debug_logger.h"
#include "injection_ledger.h"

#include <imm.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "advapi32.lib")

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
static bool ReadWindowClassName(HWND hwnd, wchar_t (&buffer)[64]) {
    buffer[0] = L'\0';
    return hwnd && GetClassNameW(hwnd, buffer, 64) > 0;
}

static bool IsEditOrRichEditClass(const wchar_t* cls) {
    if (!cls || cls[0] == L'\0') return false;
    return _wcsicmp(cls, L"Edit") == 0 ||
           _wcsnicmp(cls, L"RichEdit", 8) == 0;
}

static bool IsStandardEditControl(HWND hwnd) {
    wchar_t cls[64] = {};
    if (!ReadWindowClassName(hwnd, cls)) return false;
    return IsEditOrRichEditClass(cls) ||
           _wcsicmp(cls, L"Scintilla") == 0;
}

static bool IsWin32PasswordControl(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    wchar_t cls[64] = {};
    if (!ReadWindowClassName(hwnd, cls)) return false;
    if (!IsEditOrRichEditClass(cls)) return false;
    return (GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_PASSWORD) != 0;
}

static bool IsWin32ReadOnlyEditControl(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    wchar_t cls[64] = {};
    if (!ReadWindowClassName(hwnd, cls)) return false;
    if (!IsEditOrRichEditClass(cls)) return false;
    return (GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_READONLY) != 0;
}

struct ScopedWin32Handle {
    HANDLE handle = nullptr;
    ScopedWin32Handle() = default;
    explicit ScopedWin32Handle(HANDLE h) : handle(h) {}
    ~ScopedWin32Handle() {
        if (handle) CloseHandle(handle);
    }
    ScopedWin32Handle(const ScopedWin32Handle&) = delete;
    ScopedWin32Handle& operator=(const ScopedWin32Handle&) = delete;
};

static bool IsHigherIntegrityBlocked(DWORD targetIntegrityLevel,
                                     DWORD currentIntegrityLevel,
                                     bool currentHasUiAccess) {
    return targetIntegrityLevel > currentIntegrityLevel && !currentHasUiAccess;
}

static bool QueryTokenIntegrityLevel(HANDLE token, DWORD& outIntegrityLevel) {
    DWORD needed = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &needed);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed == 0) {
        return false;
    }
    std::vector<BYTE> buffer(needed);
    if (!GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), needed, &needed)) {
        return false;
    }
    const auto* mandatoryLabel = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data());
    if (!mandatoryLabel || !mandatoryLabel->Label.Sid) {
        return false;
    }
    const UCHAR subAuthCount = *GetSidSubAuthorityCount(mandatoryLabel->Label.Sid);
    if (subAuthCount == 0) {
        return false;
    }
    outIntegrityLevel = *GetSidSubAuthority(mandatoryLabel->Label.Sid, subAuthCount - 1);
    return true;
}

static bool IsBlockedByUipi(HWND targetWindow) {
    if (!targetWindow) return false;
    DWORD targetPid = 0;
    GetWindowThreadProcessId(targetWindow, &targetPid);
    if (targetPid == 0 || targetPid == GetCurrentProcessId()) {
        return false;
    }
    ScopedWin32Handle targetProcess(
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, targetPid));
    if (!targetProcess.handle) {
        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    HANDLE rawTargetToken = nullptr;
    if (!OpenProcessToken(targetProcess.handle, TOKEN_QUERY, &rawTargetToken)) {
        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    ScopedWin32Handle targetToken(rawTargetToken);

    HANDLE rawSelfToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawSelfToken)) {
        return false;
    }
    ScopedWin32Handle selfToken(rawSelfToken);

    DWORD targetIl = 0;
    DWORD selfIl = 0;
    if (!QueryTokenIntegrityLevel(targetToken.handle, targetIl) ||
        !QueryTokenIntegrityLevel(selfToken.handle, selfIl)) {
        return false;
    }
    DWORD uiAccess = 0;
    DWORD uiAccessLen = sizeof(uiAccess);
    const bool hasUiAccess =
        GetTokenInformation(selfToken.handle, TokenUIAccess, &uiAccess,
                            sizeof(uiAccess), &uiAccessLen) &&
        uiAccess != 0;
    return IsHigherIntegrityBlocked(targetIl, selfIl, hasUiAccess);
}

static bool IsShellNonEditableSurface(HWND currentRoot, HWND fg, HWND focus) {
    wchar_t rootCls[64] = {};
    wchar_t fgCls[64] = {};
    wchar_t focusCls[64] = {};
    ReadWindowClassName(currentRoot, rootCls);
    ReadWindowClassName(fg, fgCls);
    ReadWindowClassName(focus, focusCls);

    if (_wcsicmp(rootCls, kMainClass) == 0 || _wcsicmp(rootCls, kHudClass) == 0 ||
        _wcsicmp(fgCls, kMainClass) == 0 || _wcsicmp(fgCls, kHudClass) == 0) {
        return true;
    }

    const bool focusIsStandardEdit = focus && IsStandardEditControl(focus);

    // Desktop (Progman / WorkerW): allow only if renaming an icon (Edit).
    if (_wcsicmp(rootCls, L"Progman") == 0) {
        return !focusIsStandardEdit;
    }
    if (_wcsicmp(rootCls, L"WorkerW") == 0) {
        if (focusIsStandardEdit) return false;
        return _wcsicmp(focusCls, L"WorkerW") == 0 ||
               _wcsicmp(focusCls, L"SHELLDLL_DefView") == 0 ||
               _wcsicmp(focusCls, L"SysListView32") == 0;
    }

    // Taskbar & system switchers / lock screen.
    if (_wcsicmp(rootCls, L"Shell_TrayWnd") == 0 ||
        _wcsicmp(rootCls, L"Shell_SecondaryTrayWnd") == 0) {
        return !focusIsStandardEdit;
    }
    if (_wcsicmp(rootCls, L"NotifyIconOverflowWindow") == 0 ||
        _wcsicmp(rootCls, L"TaskListThumbnailWnd") == 0 ||
        _wcsicmp(rootCls, L"MultitaskingViewFrame") == 0 ||
        _wcsicmp(rootCls, L"TaskSwitcherWnd") == 0 ||
        _wcsicmp(rootCls, L"ForegroundStaging") == 0 ||
        _wcsicmp(rootCls, L"XamlExplorerHostPopupWindow") == 0 ||
        _wcsicmp(rootCls, L"LockApp") == 0) {
        return true;
    }

    // Explorer non-editable views (renaming a file uses Edit -> allowed).
    if (_wcsicmp(rootCls, L"CabinetWClass") == 0) {
        if (focusIsStandardEdit) return false;
        return _wcsicmp(focusCls, L"CabinetWClass") == 0 ||
               _wcsicmp(focusCls, L"DirectUIHWND") == 0 ||
               _wcsicmp(focusCls, L"SysListView32") == 0 ||
               _wcsicmp(focusCls, L"SysTreeView32") == 0 ||
               _wcsicmp(focusCls, L"SHELLDLL_DefView") == 0 ||
               _wcsicmp(focusCls, L"ToolbarWindow32") == 0 ||
               _wcsicmp(focusCls, L"ReBarWindow32") == 0;
    }

    return false;
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

struct PendingFallbackCopy {
    bool active = false;
    std::wstring text;
    bool excludeFromHistory = true;
};

static std::unique_ptr<selection_context::ClipboardSnapshot> s_pendingClipboardRestore;
static PendingFallbackCopy s_pendingFallbackCopy;
static DWORD s_expectedClipboardSeq = 0;
static bool s_clipboardPastePending = false;
static bool s_restoreAfterPaste = true;
static ULONG_PTR s_clipboardPasteToken = 0;
static UINT s_clipboardRestoreFailures = 0;
static ULONGLONG s_lastClipboardRestoreLogTick = 0;
static ULONGLONG s_clipboardPasteStartTick = 0;
static ULONGLONG s_lastClipboardPasteLogTick = 0;
static bool s_clipboardPasteWaitLogged = false;

bool IsPendingClipboardStillOurs() {
    if (s_pendingClipboardRestore && s_pendingClipboardRestore->FlushPending()) {
        return s_pendingClipboardRestore->IsCurrentOleClipboard();
    }
    return s_expectedClipboardSeq != 0 &&
           GetClipboardOwner() == g_mainWindow &&
           GetClipboardSequenceNumber() == s_expectedClipboardSeq;
}

bool WriteClipboardFallbackTextImmediate(const std::wstring& text, bool excludeFromHistory) {
    if (!g_mainWindow || text.empty()) return false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const DWORD seq = GetClipboardSequenceNumber();
        if (!seq) return false;
        const auto write = selection_context::SetClipboardText(
            text, g_mainWindow, seq, excludeFromHistory);
        if (write.written) return true;
        // SetClipboardText already retries OpenClipboard contention internally;
        // only re-attempt once with a refreshed sequence if another app bumped it.
        if (GetClipboardSequenceNumber() == seq) break;
    }
    return false;
}

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
    // Clipboard ownership can change without consuming the queued WM_PASTE.
    if (s_clipboardPastePending) return false;
    if (!s_pendingClipboardRestore && !s_pendingFallbackCopy.active) return true;
    const bool stillOurs = IsPendingClipboardStillOurs();
    if (!stillOurs) {
        if (s_pendingFallbackCopy.active && g_mainWindow) {
            PostMessageW(g_mainWindow, kClipboardDiagnosticMessage, kClipboardFallbackCancelled, 0);
        }
        if (s_pendingClipboardRestore) {
            s_pendingClipboardRestore->Dismiss();
            s_pendingClipboardRestore.reset();
        }
        s_pendingFallbackCopy = {};
        s_clipboardPastePending = false;
        ++s_clipboardPasteToken;
        s_clipboardRestoreFailures = 0;
    } else if (s_pendingFallbackCopy.active) {
        if (!WriteClipboardFallbackTextImmediate(s_pendingFallbackCopy.text,
                                                s_pendingFallbackCopy.excludeFromHistory)) {
            ReportClipboardRestoreFailure();
            return false;
        }
        if (s_pendingClipboardRestore) {
            s_pendingClipboardRestore->Dismiss();
            s_pendingClipboardRestore.reset();
        }
        s_pendingFallbackCopy = {};
        s_clipboardRestoreFailures = 0;
        if (g_mainWindow) {
            PostMessageW(g_mainWindow, kClipboardDiagnosticMessage, kClipboardFallbackCopied, 0);
        }
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
    if (!s_restoreAfterPaste && !s_pendingFallbackCopy.active && s_pendingClipboardRestore) {
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
    if (s_clipboardPastePending) {
        // Exiting cannot cancel another thread's WM_PASTE; leave its data untouched.
        if (s_pendingClipboardRestore) {
            s_pendingClipboardRestore->Dismiss();
            s_pendingClipboardRestore.reset();
        }
        s_pendingFallbackCopy = {};
        s_clipboardPastePending = false;
        ++s_clipboardPasteToken;
        return;
    }
    s_clipboardPastePending = false;
    ++s_clipboardPasteToken;
    if (!s_restoreAfterPaste && !s_pendingFallbackCopy.active && s_pendingClipboardRestore) {
        s_pendingClipboardRestore->Dismiss();
        s_pendingClipboardRestore.reset();
    }
    for (int attempt = 0;
         attempt < 5 && (s_pendingClipboardRestore || s_pendingFallbackCopy.active);
         ++attempt) {
        if (RestorePendingClipboard()) break;
        Sleep(50);
    }
    s_pendingFallbackCopy = {};
    if (s_pendingClipboardRestore) {
        debug_log::Write("[Clipboard] Restore failed during shutdown");
        s_pendingClipboardRestore->Dismiss();
        s_pendingClipboardRestore.reset();
    }
}

namespace {

// True while a read-back worker is sleeping/reading, so repeated injections
// cannot accumulate threads (see StartReflectionWorker).
std::atomic<bool> s_reflectionInFlight{false};

// One-shot read-back worker for the learning loop. It only observes; every
// policy decision (learn or not) happens in the app layer when the message is
// handled, so a failure here can never change what is typed.
void StartReflectionWorker(uint64_t sequence, DWORD reflectionDelayMs) {
    if (reflectionDelayMs == 0 || !g_mainWindow) return;
    // At most one read-back in flight. Pressing the hotkey again before the delay
    // elapses must not pile up detached workers: each one sleeps through the delay
    // and then reads the focused document, so a rapid sequence of injections would
    // otherwise leave a thread per press queued behind the same window.
    bool expected = false;
    if (!s_reflectionInFlight.compare_exchange_strong(expected, true)) return;
    std::thread([sequence, reflectionDelayMs]() {
        struct InFlightReset {
            ~InFlightReset() { s_reflectionInFlight.store(false); }
        } inFlightReset;
        std::this_thread::sleep_for(std::chrono::milliseconds(reflectionDelayMs));
        injection_ledger::Record record;
        if (!injection_ledger::Snapshot(record) || record.sequence != sequence) return;

        auto* reflection = new InjectionReflection();
        reflection->sequence = sequence;
        if (record.sensitive) {
            // A sensitive injection is never read back: the document may be a
            // password field and the observation must not reach any store.
            reflection->reason = "sensitive";
        } else if (selection_context::TopLevelWindow(GetForegroundWindow()) !=
                   record.targetWindow) {
            reflection->reason = "foreground_changed";
        } else {
            std::wstring observed;
            std::string error;
            HWND focus = record.focusWindow ? record.focusWindow : GetForegroundWindow();
            if (selection_context::TryReadFocusedDocumentText(focus, observed, error)) {
                reflection->readSucceeded = true;
                reflection->observedText = std::move(observed);
            } else {
                reflection->reason = "read_failed";
            }
        }

        if (!g_mainWindow ||
            !PostMessageW(g_mainWindow, kInjectionReflectionMessage,
                          static_cast<WPARAM>(sequence), reinterpret_cast<LPARAM>(reflection))) {
            delete reflection;
        }
    }).detach();
}

// Records the successful injection so the undo/organize/learning consumers can
// line up with what the host application now contains.
void RecordCompletedInjection(const std::wstring& text,
                              injection_ledger::InjectionMethod method,
                              HWND foreground,
                              HWND focusWindow,
                              bool hasPreSelection,
                              const std::wstring& preSelectionText,
                              const platform::InjectionContext* context,
                              DWORD reflectionDelayMs) {
    injection_ledger::Record record;
    record.injectedText = text;
    record.method = method;
    record.targetWindow = selection_context::TopLevelWindow(foreground);
    record.focusWindow = focusWindow;
    record.tickMs = GetTickCount();
    record.hasPreSelection = hasPreSelection;
    record.preSelectionText = preSelectionText;
    if (context) {
        record.sourceText = context->sourceText;
        record.sensitive = context->sensitive;
        record.llmApplied = context->llmApplied;
    }
    const uint64_t sequence = injection_ledger::RecordInjection(std::move(record));
    StartReflectionWorker(sequence, reflectionDelayMs);
}

}  // namespace

bool SendUnicodeText(const std::wstring& text, HWND expectedForeground) {
    const HWND fg = GetForegroundWindow();
    if (!text.empty() && !fg) return false;
    if (expectedForeground != nullptr && fg != expectedForeground) return false;
    const HWND target = expectedForeground ? expectedForeground : fg;
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

const char* InjectionFailureReasonName(InjectionFailureReason reason) {
    switch (reason) {
    case InjectionFailureReason::None: return "none";
    case InjectionFailureReason::NoForegroundWindow: return "no_foreground_window";
    case InjectionFailureReason::BlockedByUipi: return "blocked_by_uipi";
    case InjectionFailureReason::NonEditableShellSurface: return "non_editable_shell_surface";
    case InjectionFailureReason::DisabledOrReadOnlyControl: return "disabled_or_readonly_control";
    case InjectionFailureReason::WindowSwitchedAfterStop: return "window_switched_after_stop";
    case InjectionFailureReason::SelectionRewriteSkipped: return "selection_rewrite_skipped";
    case InjectionFailureReason::DeliveryFailed: return "delivery_failed";
    }
    return "unknown";
}

void InvalidatePendingClipboardRestore() {
    s_clipboardPastePending = false;
    ++s_clipboardPasteToken;
    s_pendingFallbackCopy = {};
    if (s_pendingClipboardRestore) {
        s_pendingClipboardRestore->Dismiss();
        s_pendingClipboardRestore.reset();
    }
    s_clipboardRestoreFailures = 0;
    if (g_mainWindow) KillTimer(g_mainWindow, kClipboardRestoreTimer);
}

bool IsCurrentFocusWin32Password() {
    HWND fg = GetForegroundWindow();
    if (!fg || !IsWindow(fg)) return false;
    if (IsWin32PasswordControl(fg)) return true;
    HWND focus = GetFocusedWindowForForeground(fg);
    return focus != fg && IsWin32PasswordControl(focus);
}

ClipboardCopyStatus CopyTextToClipboardFallback(const std::wstring& text,
                                 bool excludeFromHistory,
                                 bool confirmedSensitiveAtStart,
                                 bool* skippedDueToPassword,
                                 bool checkCurrentFocusPassword) {
    if (skippedDueToPassword) *skippedDueToPassword = false;
    if (text.empty()) return ClipboardCopyStatus::Failed;
    if (confirmedSensitiveAtStart ||
        (checkCurrentFocusPassword && IsCurrentFocusWin32Password())) {
        if (skippedDueToPassword) *skippedDueToPassword = true;
        return ClipboardCopyStatus::Failed;
    }
    if (!g_mainWindow) return ClipboardCopyStatus::Failed;
    if (s_clipboardPastePending) {
        s_pendingFallbackCopy.active = true;
        s_pendingFallbackCopy.text = text;
        s_pendingFallbackCopy.excludeFromHistory = excludeFromHistory;
        return ClipboardCopyStatus::Pending;
    }
    InvalidatePendingClipboardRestore();
    return WriteClipboardFallbackTextImmediate(text, excludeFromHistory)
        ? ClipboardCopyStatus::Copied : ClipboardCopyStatus::Failed;
}

namespace {

struct TargetAssessment {
    InjectionFailureReason failure = InjectionFailureReason::None;
    HWND fg = nullptr;
    HWND currentRoot = nullptr;
    HWND focus = nullptr;
};

static bool HasWindowSwitchedAfterStop(const InjectionContext* context, HWND currentRoot) {
    if (!context || !context->fallbackOnWindowSwitch || context->stopRootWindow == nullptr) {
        return false;
    }
    if (!IsWindow(context->stopRootWindow) || currentRoot != context->stopRootWindow) {
        return true;
    }
    if (context->stopRootProcessId != 0) {
        DWORD currentRootPid = 0;
        GetWindowThreadProcessId(currentRoot, &currentRootPid);
        if (currentRootPid != context->stopRootProcessId) {
            return true;
        }
    }
    return false;
}

TargetAssessment AssessInjectionTarget(const InjectionContext* context) {
    TargetAssessment assessment;
    HWND fg = GetForegroundWindow();
    if (!fg || !IsWindow(fg) || IsIconic(fg)) {
        assessment.failure = InjectionFailureReason::NoForegroundWindow;
        return assessment;
    }
    HWND currentRoot = selection_context::TopLevelWindow(fg);
    if (!currentRoot || !IsWindow(currentRoot) || IsIconic(currentRoot)) {
        assessment.failure = InjectionFailureReason::NoForegroundWindow;
        return assessment;
    }

    assessment.fg = fg;
    assessment.currentRoot = currentRoot;

    if (HasWindowSwitchedAfterStop(context, currentRoot)) {
        assessment.failure = InjectionFailureReason::WindowSwitchedAfterStop;
        return assessment;
    }

    HWND focus = GetFocusedWindowForForeground(fg);
    assessment.focus = focus;

    if (IsShellNonEditableSurface(currentRoot, fg, focus)) {
        assessment.failure = InjectionFailureReason::NonEditableShellSurface;
        return assessment;
    }

    if (IsBlockedByUipi(fg) || (focus && focus != fg && IsBlockedByUipi(focus))) {
        assessment.failure = InjectionFailureReason::BlockedByUipi;
        return assessment;
    }

    if (!IsWindowEnabled(currentRoot) || !IsWindowEnabled(fg) ||
        (focus && IsWindow(focus) && !IsWindowEnabled(focus)) ||
        IsWin32ReadOnlyEditControl(focus)) {
        assessment.failure = InjectionFailureReason::DisabledOrReadOnlyControl;
        return assessment;
    }

    return assessment;
}

void PopulateFallbackOutcome(InjectionOutcome& outcome,
                             const std::wstring& text,
                             const InjectionContext* context) {
    const bool shouldCopy = context ? context->copyToClipboardOnFailure
                                    : g_config.copyToClipboardOnFailure;
    const bool confirmedSensitiveAtStart =
        context ? context->confirmedSensitiveAtStart : false;
    if (!shouldCopy) {
        if (confirmedSensitiveAtStart || IsCurrentFocusWin32Password()) {
            outcome.skippedDueToPassword = true;
        }
        return;
    }
    const ClipboardCopyStatus copyStatus = CopyTextToClipboardFallback(
        text,
        /*excludeFromHistory=*/g_config.restoreClipboardAfterPaste,
        confirmedSensitiveAtStart,
        &outcome.skippedDueToPassword,
        /*checkCurrentFocusPassword=*/true);
    outcome.copiedToClipboard = copyStatus == ClipboardCopyStatus::Copied;
    outcome.clipboardCopyPending = copyStatus == ClipboardCopyStatus::Pending;
}

}  // namespace

InjectionOutcome PasteTextImeAwareWithOutcome(const std::wstring& text,
                                              bool forceUnicodeInput,
                                              const InjectionContext* context) {
    InjectionOutcome outcome;
    if (text.empty()) {
        outcome.delivered = true;
        return outcome;
    }

    const TargetAssessment target = AssessInjectionTarget(context);
    if (target.failure != InjectionFailureReason::None) {
        outcome.reason = target.failure;
        PopulateFallbackOutcome(outcome, text, context);
        return outcome;
    }

    const DWORD reflectionDelayMs = context ? context->reflectionDelayMs : 0;
    bool hasPreSelection = false;
    std::wstring preSelectionText;
    if (context && context->capturePreSelection) {
        // Must run before the text lands: the paste replaces the selection.
        SelectionContext selection = selection_context::Capture();
        if (selection.Usable() && !selection.selectedText.empty()) {
            hasPreSelection = true;
            preSelectionText = selection.selectedText;
        }
    }
    const auto finish = [&](injection_ledger::InjectionMethod method, HWND foreground,
                            HWND focusWindow) {
        RecordCompletedInjection(text, method, foreground, focusWindow, hasPreSelection,
                                 preSelectionText, context, reflectionDelayMs);
        outcome.delivered = true;
        outcome.reason = InjectionFailureReason::None;
        return outcome;
    };
    const auto failDelivery = [&](InjectionFailureReason defaultReason) {
        HWND nowFg = GetForegroundWindow();
        if (!nowFg || !IsWindow(nowFg) || IsIconic(nowFg)) {
            outcome.reason = InjectionFailureReason::NoForegroundWindow;
        } else {
            HWND nowRoot = selection_context::TopLevelWindow(nowFg);
            if (HasWindowSwitchedAfterStop(context, nowRoot)) {
                outcome.reason = InjectionFailureReason::WindowSwitchedAfterStop;
            } else {
                outcome.reason = defaultReason;
            }
        }
        PopulateFallbackOutcome(outcome, text, context);
        return outcome;
    };

    if (GetForegroundWindow() != target.fg) {
        return failDelivery(InjectionFailureReason::DeliveryFailed);
    }

    if (forceUnicodeInput) {
        if (!SendUnicodeText(text, target.fg)) {
            return failDelivery(InjectionFailureReason::DeliveryFailed);
        }
        return finish(injection_ledger::InjectionMethod::UnicodeInput, target.fg, target.focus);
    }

    HWND fg = target.fg;
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
            if (GetForegroundWindow() != fg) {
                return failDelivery(InjectionFailureReason::DeliveryFailed);
            }
            if (!PostMessageW(fg, WM_CHAR, ch, 0)) {
                return failDelivery(InjectionFailureReason::DeliveryFailed);
            }
            Sleep(1);
        }
        return finish(injection_ledger::InjectionMethod::WmChar, fg, nullptr);
    }

    HWND focus = target.focus ? target.focus : GetFocusedWindowForForeground(fg);
    if (!g_config.restoreClipboardAfterPaste) {
        bool shortcutAttempted = false;
        if (PasteViaClipboard(focus, text, false, &shortcutAttempted)) {
            return finish(injection_ledger::InjectionMethod::ClipboardPaste, fg, focus);
        }
        if (shortcutAttempted || GetForegroundWindow() != fg) {
            return failDelivery(InjectionFailureReason::DeliveryFailed);
        }
        if (!SendUnicodeText(text, fg)) {
            return failDelivery(InjectionFailureReason::DeliveryFailed);
        }
        return finish(injection_ledger::InjectionMethod::UnicodeInput, fg, focus);
    }
    // There is no acknowledgement that an asynchronous Ctrl+V has consumed
    // the clipboard. Protected mode uses Unicode for nonstandard controls.
    if (!IsStandardEditControl(focus)) {
        if (!SendUnicodeText(text, fg)) {
            return failDelivery(InjectionFailureReason::DeliveryFailed);
        }
        return finish(injection_ledger::InjectionMethod::UnicodeInput, fg, focus);
    }
    if (PasteViaClipboard(focus, text, true)) {
        return finish(injection_ledger::InjectionMethod::ClipboardPaste, fg, focus);
    }
    if (GetForegroundWindow() != fg) {
        return failDelivery(InjectionFailureReason::DeliveryFailed);
    }
    if (!SendUnicodeText(text, fg)) {
        return failDelivery(InjectionFailureReason::DeliveryFailed);
    }
    return finish(injection_ledger::InjectionMethod::UnicodeInput, fg, focus);
}

bool PasteTextImeAware(const std::wstring& text,
                       bool forceUnicodeInput,
                       const InjectionContext* context) {
    return PasteTextImeAwareWithOutcome(text, forceUnicodeInput, context).delivered;
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
        if (!SendUnicodeText(text, selection.targetWindow)) {
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
        if (!SendUnicodeText(text, selection.targetWindow)) {
            if (error) *error = L"Unicode replacement failed";
            return false;
        }
    }
    return true;
}

InjectionOutcome ReplaceSelectionTextImeAwareWithOutcome(const SelectionContext& selection,
                                                         const std::wstring& text,
                                                         const InjectionContext* context) {
    InjectionOutcome outcome;
    if (text.empty()) {
        outcome.delivered = true;
        return outcome;
    }

    const TargetAssessment target = AssessInjectionTarget(context);
    if (target.failure != InjectionFailureReason::None) {
        outcome.reason = target.failure;
        PopulateFallbackOutcome(outcome, text, context);
        return outcome;
    }

    std::wstring replaceError;
    if (ReplaceSelectionTextImeAware(selection, text, &replaceError)) {
        outcome.delivered = true;
        outcome.reason = InjectionFailureReason::None;
        return outcome;
    }

    outcome.reason = InjectionFailureReason::SelectionRewriteSkipped;
    outcome.detail = std::move(replaceError);
    PopulateFallbackOutcome(outcome, text, context);
    return outcome;
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
