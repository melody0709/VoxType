#ifdef NDEBUG
#undef NDEBUG
#endif
#define NOMINMAX
#include <windows.h>
#include <ole2.h>
#include <imm.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <thread>

namespace {
bool failEmpty = false;
bool failFlush = false;
UINT failFormat = 0;
UINT exclusionFormat = 0;
int emptyCalls = 0;
HWND testForeground = nullptr;
HWND testFocus = nullptr;
HWND switchForegroundTarget = nullptr;
HWND switchFocusTarget = nullptr;
int switchAfterGetForegroundCount = 0;
bool forceNullForeground = false;
DWORD fakeTargetPid = 0;
bool denyTargetProcessOpen = false;
UINT unicodeCharacters = 0;
UINT pasteShortcuts = 0;
ULONGLONG testTickOffset = 0;
bool copyOnClose = false;
HWND externalCopyOwner = nullptr;
bool expectExclusion = true;
bool liveInput = false;
bool imeOpen = true;
DWORD imeConversion = IME_CMODE_NATIVE;
DWORD imeSentence = 0;
UINT imeDisableCalls = 0;
UINT imeRestoreCalls = 0;

HIMC TestImmGetContext(HWND hwnd) {
    return liveInput ? ::ImmGetContext(hwnd) : reinterpret_cast<HIMC>(1);
}
BOOL TestImmGetConversionStatus(HIMC context, LPDWORD conversion, LPDWORD sentence) {
    if (liveInput) return ::ImmGetConversionStatus(context, conversion, sentence);
    *conversion = imeConversion;
    *sentence = imeSentence;
    return TRUE;
}
BOOL TestImmGetOpenStatus(HIMC context) {
    return liveInput ? ::ImmGetOpenStatus(context) : imeOpen;
}
BOOL TestImmSetOpenStatus(HIMC context, BOOL open) {
    if (liveInput) return ::ImmSetOpenStatus(context, open);
    imeOpen = open != FALSE;
    if (imeOpen) ++imeRestoreCalls;
    else ++imeDisableCalls;
    return TRUE;
}
BOOL TestImmSetConversionStatus(HIMC context, DWORD conversion, DWORD sentence) {
    if (liveInput) return ::ImmSetConversionStatus(context, conversion, sentence);
    imeConversion = conversion;
    imeSentence = sentence;
    return TRUE;
}
BOOL TestImmReleaseContext(HWND hwnd, HIMC context) {
    return liveInput ? ::ImmReleaseContext(hwnd, context) : TRUE;
}

HWND TestGetForegroundWindow() {
    if (switchAfterGetForegroundCount > 0 && --switchAfterGetForegroundCount == 0) {
        testForeground = switchForegroundTarget;
        testFocus = switchFocusTarget;
        switchForegroundTarget = nullptr;
        switchFocusTarget = nullptr;
    }
    if (forceNullForeground) return nullptr;
    return testForeground ? testForeground : ::GetForegroundWindow();
}

DWORD TestGetWindowThreadProcessId(HWND hwnd, LPDWORD outPid) {
    DWORD realPid = 0;
    const DWORD tid = ::GetWindowThreadProcessId(hwnd, &realPid);
    if (outPid) {
        *outPid = (fakeTargetPid != 0) ? fakeTargetPid : realPid;
    }
    return tid;
}

HANDLE TestOpenProcess(DWORD desiredAccess, BOOL inheritHandle, DWORD pid) {
    if (denyTargetProcessOpen && fakeTargetPid != 0 && pid == fakeTargetPid) {
        SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }
    return ::OpenProcess(desiredAccess, inheritHandle, pid);
}

BOOL TestGetGUIThreadInfo(DWORD threadId, GUITHREADINFO* info) {
    if (forceNullForeground) return FALSE;
    if (!testForeground) return ::GetGUIThreadInfo(threadId, info);
    info->hwndFocus = testFocus;
    info->hwndActive = testForeground;
    return TRUE;
}

UINT TestSendInput(UINT count, INPUT* inputs, int) {
    if (liveInput) {
        using SendInputFn = UINT (WINAPI*)(UINT, LPINPUT, int);
        const auto send = reinterpret_cast<SendInputFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SendInput"));
        assert(send);
        return send(count, inputs, sizeof(INPUT));
    }
    // Never inject into the interactive desktop. Send text to our private Edit.
    assert(testFocus && IsWindow(testFocus));
    for (UINT i = 0; i < count; ++i) {
        if (inputs[i].ki.dwFlags & KEYEVENTF_UNICODE) {
            if (!(inputs[i].ki.dwFlags & KEYEVENTF_KEYUP)) {
                SendMessageW(testFocus, WM_CHAR, inputs[i].ki.wScan, 0);
                ++unicodeCharacters;
            }
        } else if (inputs[i].ki.wVk == 'V') {
            if (!(inputs[i].ki.dwFlags & KEYEVENTF_KEYUP)) {
                assert(!imeOpen);
                ++pasteShortcuts;
                SendMessageW(testFocus, WM_PASTE, 0, 0);
            }
        } else {
            assert(inputs[i].ki.wVk == VK_CONTROL);
        }
    }
    return count;
}

ULONGLONG TestGetTickCount64() { return ::GetTickCount64() + testTickOffset; }

BOOL TestCloseClipboard() {
    const BOOL result = ::CloseClipboard();
    if (copyOnClose) {
        copyOnClose = false;
        // Simulate an external copy immediately after releasing the lock.
        assert(externalCopyOwner && ::OpenClipboard(externalCopyOwner) && ::EmptyClipboard());
        constexpr wchar_t external[] = L"external copy";
        HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, sizeof(external));
        assert(data);
        void* target = GlobalLock(data);
        assert(target);
        memcpy(target, external, sizeof(external));
        GlobalUnlock(data);
        assert(::SetClipboardData(CF_UNICODETEXT, data));
        ::CloseClipboard();
    }
    return result;
}

BOOL TestEmptyClipboard() {
    ++emptyCalls;
    return failEmpty ? FALSE : ::EmptyClipboard();
}

HANDLE TestSetClipboardData(UINT format, HANDLE data) {
    if (format == CF_UNICODETEXT && expectExclusion) {
        assert(::GetClipboardData(exclusionFormat));
    }
    return format == failFormat ? nullptr : ::SetClipboardData(format, data);
}

HRESULT TestOleFlushClipboard() {
    return failFlush ? CLIPBRD_E_CANT_OPEN : ::OleFlushClipboard();
}
} // namespace

// Exercise the production implementation with real APIs, injecting only the
// two write failures that must preserve the previous contents.
#define EmptyClipboard TestEmptyClipboard
#define SetClipboardData TestSetClipboardData
#define OleFlushClipboard TestOleFlushClipboard
#define CloseClipboard TestCloseClipboard
#define GetForegroundWindow TestGetForegroundWindow
#include "selection_context.h"
#undef EmptyClipboard
#undef SetClipboardData
#undef OleFlushClipboard
#undef CloseClipboard
#undef GetForegroundWindow
#include "config_store.h"
#include "text_injector.h"

Config g_config;
HWND g_mainWindow = nullptr;
namespace debug_log { void Write(const char*, ...) {} }
// Compile the injector in this translation unit so the failure hooks above
// produce a single definition of each inline clipboard function.
#define GetForegroundWindow TestGetForegroundWindow
#define GetWindowThreadProcessId TestGetWindowThreadProcessId
#define OpenProcess TestOpenProcess
#define GetGUIThreadInfo TestGetGUIThreadInfo
#define SendInput TestSendInput
#define GetTickCount64 TestGetTickCount64
#define ImmGetContext TestImmGetContext
#define ImmGetConversionStatus TestImmGetConversionStatus
#define ImmGetOpenStatus TestImmGetOpenStatus
#define ImmSetOpenStatus TestImmSetOpenStatus
#define ImmSetConversionStatus TestImmSetConversionStatus
#define ImmReleaseContext TestImmReleaseContext
#include "../src/platform/text_injector.cpp"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef OpenProcess
#undef GetGUIThreadInfo
#undef SendInput
#undef GetTickCount64
#undef ImmGetContext
#undef ImmGetConversionStatus
#undef ImmGetOpenStatus
#undef ImmSetOpenStatus
#undef ImmSetConversionStatus
#undef ImmReleaseContext

int main(int argc, char** argv) {
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    if (argc == 2 && std::strcmp(argv[1], "--live-snapshot-probe") == 0) {
        const DWORD before = GetClipboardSequenceNumber();
        selection_context::ClipboardSnapshot snapshot;
        std::printf("live_clipboard_snapshot_readable=%d sequence_changed=%d\n",
                    snapshot.Readable() ? 1 : 0,
                    GetClipboardSequenceNumber() != before ? 1 : 0);
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--live-paste-probe") == 0) {
        HWND owner = CreateWindowExW(0, L"STATIC", L"ClipboardTestOwner", 0,
                                     0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
        assert(owner);
        g_mainWindow = owner;
        liveInput = true;
        expectExclusion = false;
        const bool delivered = platform::PasteTextImeAware(L"VoxType live paste probe", false);
        std::wstring clipboardText;
        const bool clipboardReadable = selection_context::ReadClipboardText(clipboardText);
        std::printf("live_paste_delivered=%d clipboard_readable=%d clipboard_chars=%zu\n",
                    delivered ? 1 : 0, clipboardReadable ? 1 : 0,
                    clipboardText.size());
        DestroyWindow(owner);
        return delivered ? 0 : 1;
    }
    assert(selection_context::IsSelectionPasswordReadable(S_OK, FALSE));
    assert(!selection_context::IsSelectionPasswordReadable(S_OK, TRUE));
    assert(!selection_context::IsSelectionPasswordReadable(E_FAIL, FALSE));
    assert(!selection_context::IsSelectionPasswordReadable(S_FALSE, FALSE));
    // The clipboard belongs to a window station. Use a private station and
    // desktop so these tests never read or replace the user's clipboard.
    const auto oldStation = GetProcessWindowStation();
    const auto oldDesktop = GetThreadDesktop(GetCurrentThreadId());
    const auto station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    assert(station && SetProcessWindowStation(station));
    const auto desktop = CreateDesktopW(L"ClipboardTest", nullptr, nullptr, 0,
                                         GENERIC_ALL, nullptr);
    assert(desktop && SetThreadDesktop(desktop));
    HWND owner = CreateWindowExW(0, L"STATIC", L"ClipboardTest", 0,
                                  0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
    assert(owner);
    g_mainWindow = owner;
    exclusionFormat = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    const UINT extraFormat = RegisterClipboardFormatW(L"VoxType.ClipboardTest.Extra");

    assert(OpenClipboard(owner) && EmptyClipboard());
    CloseClipboard();
    {
        selection_context::ClipboardSnapshot emptySnapshot;
        assert(emptySnapshot.Readable());
        assert(selection_context::SetClipboardText(L"temporary", owner,
                                                   GetClipboardSequenceNumber()).written);
        assert(emptySnapshot.Restore());
        assert(OpenClipboard(owner) && CountClipboardFormats() == 0);
        CloseClipboard();
    }
    auto original = selection_context::SetClipboardText(L"original", owner,
                                                        GetClipboardSequenceNumber());
    assert(original.written);
    assert(OpenClipboard(owner));
    HGLOBAL extra = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    assert(extra);
    auto* value = static_cast<DWORD*>(GlobalLock(extra));
    assert(value);
    *value = 0x12345678;
    GlobalUnlock(extra);
    assert(SetClipboardData(extraFormat, extra));
    CloseClipboard();

    for (UINT failure : {exclusionFormat, UINT(CF_UNICODETEXT), UINT(0)}) {
        selection_context::ClipboardSnapshot snapshot;
        assert(snapshot.Readable());
        failFormat = failure;
        auto write = selection_context::SetClipboardText(L"temporary", owner,
                                                         GetClipboardSequenceNumber());
        failFormat = 0;
        assert(write.changed && write.written == (failure == 0));
        assert(snapshot.Restore());
        std::wstring text;
        assert(selection_context::ReadClipboardText(text) && text == L"original");
        assert(OpenClipboard(owner));
        auto restoredExtra = GetClipboardData(extraFormat);
        assert(restoredExtra);
        auto* restoredValue = static_cast<DWORD*>(GlobalLock(restoredExtra));
        assert(restoredValue && *restoredValue == 0x12345678);
        GlobalUnlock(restoredExtra);
        CloseClipboard();
    }

    const auto sequence = GetClipboardSequenceNumber();
    auto write = selection_context::SetClipboardText(L"temporary", owner, sequence + 1);
    assert(!write.written && !write.changed && GetClipboardSequenceNumber() == sequence);
    const int before = emptyCalls;
    write = selection_context::SetClipboardText(L"temporary", nullptr, sequence);
    assert(!write.changed && emptyCalls == before);
    failEmpty = true;
    write = selection_context::SetClipboardText(L"temporary", owner, sequence);
    failEmpty = false;
    assert(!write.written && !write.changed && GetClipboardSequenceNumber() == sequence);
    std::wstring text;
    assert(selection_context::ReadClipboardText(text) && text == L"original");

    {
        selection_context::ClipboardSnapshot snapshot;
        assert(snapshot.Readable());
        assert(selection_context::SetClipboardText(L"temporary", owner,
                                                   GetClipboardSequenceNumber()).written);
        failFlush = true;
        assert(!snapshot.Restore() && snapshot.FlushPending() && snapshot.IsCurrentOleClipboard());
        failFlush = false;
        assert(snapshot.Restore());
        assert(selection_context::ReadClipboardText(text) && text == L"original");
    }

    HWND edit = CreateWindowExW(0, L"EDIT", L"abc selected xyz", WS_CHILD | ES_AUTOHSCROLL,
                                 0, 0, 100, 30, owner, nullptr, nullptr, nullptr);
    assert(edit);
    SendMessageW(edit, EM_SETSEL, 4, 12);
    const auto beforeSelection = GetClipboardSequenceNumber();
    std::string error;
    assert(selection_context::TryReadSelectionWithWin32(edit, text, error) && text == L"selected");
    assert(GetClipboardSequenceNumber() == beforeSelection);
    assert(platform::PasteViaClipboard(edit, L"replacement", true));
    wchar_t editText[64] = {};
    GetWindowTextW(edit, editText, 64);
    assert(std::wstring(editText) == L"abc replacement xyz");
    assert(selection_context::ReadClipboardText(text) && text == L"original");
    // A synchronous paste can complete while restoration is contended.
    failFlush = true;
    assert(platform::PasteViaClipboard(edit, L"retry", true));
    MSG diagnostic = {};
    assert(PeekMessageW(&diagnostic, owner, kClipboardDiagnosticMessage,
                       kClipboardDiagnosticMessage, PM_REMOVE));
    assert(diagnostic.wParam == kClipboardRestoreFailed && diagnostic.lParam == 1);
    platform::OnClipboardRestoreTimer();
    assert(!PeekMessageW(&diagnostic, owner, kClipboardDiagnosticMessage,
                        kClipboardDiagnosticMessage, PM_REMOVE));
    testTickOffset = 60000;
    platform::OnClipboardRestoreTimer();
    assert(PeekMessageW(&diagnostic, owner, kClipboardDiagnosticMessage,
                       kClipboardDiagnosticMessage, PM_REMOVE));
    assert(diagnostic.wParam == kClipboardRestoreFailed && diagnostic.lParam >= 3);
    failFlush = false;
    platform::OnClipboardRestoreTimer();
    testTickOffset = 0;
    assert(selection_context::ReadClipboardText(text) && text == L"original");

    SetWindowTextW(edit, L"abc selected xyz");
    SendMessageW(edit, EM_SETSEL, 4, 12);
    SelectionContext selection;
    selection.hasSelection = selection.capturedWithWin32 = true;
    selection.selectedText = L"selected";
    selection.targetWindow = owner;
    selection.focusWindow = edit;
    GetWindowThreadProcessId(owner, &selection.processId);
    testForeground = owner;
    testFocus = edit;
    failEmpty = true;
    std::wstring replacementError;
    assert(platform::ReplaceSelectionTextImeAware(selection, L"Unicode", &replacementError));
    failEmpty = false;
    GetWindowTextW(edit, editText, 64);
    assert(std::wstring(editText) == L"abc Unicode xyz" && unicodeCharacters == 7);
    assert(selection_context::ReadClipboardText(text) && text == L"original");
    copyOnClose = true;
    externalCopyOwner = CreateWindowExW(0, L"STATIC", L"ExternalCopy", 0,
                                         0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
    assert(externalCopyOwner);
    write = selection_context::SetClipboardText(L"temporary", owner, GetClipboardSequenceNumber());
    assert(!write.written && write.changed && write.sequence == 0);
    assert(selection_context::ReadClipboardText(text) && text == L"external copy");
    DestroyWindow(externalCopyOwner);
    externalCopyOwner = nullptr;
    testForeground = owner;
    testFocus = edit;
    expectExclusion = false;
    SendMessageW(edit, EM_SETSEL, 0, -1);
    assert(platform::PasteViaClipboard(edit, L"leave clipboard", false));
    assert(imeOpen && imeConversion == IME_CMODE_NATIVE &&
           imeDisableCalls == 1 && imeRestoreCalls == 1);
    assert(selection_context::ReadClipboardText(text) && text == L"leave clipboard");
    assert(OpenClipboard(owner));
    assert(GetClipboardData(exclusionFormat) == nullptr);
    CloseClipboard();
    assert(unicodeCharacters == 7);
    expectExclusion = true;
    testForeground = testFocus = nullptr;
    SetWindowLongPtrW(edit, GWL_STYLE, GetWindowLongPtrW(edit, GWL_STYLE) | ES_PASSWORD);
    assert(!selection_context::TryReadSelectionWithWin32(edit, text, error) && text.empty());
    DestroyWindow(edit);
    platform::CleanupClipboardOnExit();

    assert(!g_config.restoreClipboardAfterPaste);
    testForeground = testFocus = owner;  // A nonstandard focused control.
    expectExclusion = false;
    const UINT beforeShortcuts = pasteShortcuts;
    const UINT beforeUnicode = unicodeCharacters;
    assert(platform::PasteTextImeAware(L"fast path"));
    assert(imeOpen && imeDisableCalls == 2 && imeRestoreCalls == 2);
    assert(pasteShortcuts == beforeShortcuts + 1 && unicodeCharacters == beforeUnicode);
    assert(selection_context::ReadClipboardText(text) && text == L"fast path");
    assert(OpenClipboard(owner) && !GetClipboardData(exclusionFormat));
    CloseClipboard();
    expectExclusion = true;
    g_config.restoreClipboardAfterPaste = true;
    assert(platform::PasteTextImeAware(L"protected"));
    assert(pasteShortcuts == beforeShortcuts + 1 && unicodeCharacters == beforeUnicode + 9);
    assert(selection_context::ReadClipboardText(text) && text == L"fast path");
    g_config.restoreClipboardAfterPaste = false;
    testForeground = testFocus = nullptr;

    assert(selection_context::SetClipboardText(L"original", owner,
                                               GetClipboardSequenceNumber()).written);
    HANDLE consume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(consume);
    std::promise<HWND> ready;
    auto target = ready.get_future();
    std::jthread worker([&](std::stop_token stop) {
        assert(SetThreadDesktop(desktop));
        HWND peerEdit = CreateWindowExW(0, L"EDIT", L"", ES_AUTOHSCROLL,
                                         0, 0, 500, 30, nullptr, nullptr, nullptr, nullptr);
        assert(peerEdit);
        ready.set_value(peerEdit);
        WaitForSingleObject(consume, INFINITE);
        while (!stop.stop_requested()) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(1);
        }
        DestroyWindow(peerEdit);
    });
    HWND peerEdit = target.get();
    assert(platform::PasteViaClipboard(peerEdit, L"first", true));
    assert(!platform::PasteViaClipboard(peerEdit, L"second", true));
    assert(selection_context::ReadClipboardText(text) && text == L"first");
    testTickOffset = 11000;
    platform::OnClipboardRestoreTimer();
    assert(PeekMessageW(&diagnostic, owner, kClipboardDiagnosticMessage,
                       kClipboardDiagnosticMessage, PM_REMOVE));
    assert(diagnostic.wParam == kClipboardPasteStillPending && diagnostic.lParam >= 10000);
    platform::OnClipboardRestoreTimer();
    assert(!PeekMessageW(&diagnostic, owner, kClipboardDiagnosticMessage,
                        kClipboardDiagnosticMessage, PM_REMOVE));
    // A prior paste queued for the worker must also block selection rewriting.
    selection.targetWindow = peerEdit;
    selection.focusWindow = peerEdit;
    selection.capturedWithWin32 = false;
    testForeground = testFocus = peerEdit;
    const UINT beforePendingUnicode = unicodeCharacters;
    assert(!platform::ReplaceSelectionTextImeAware(selection, L"second", &replacementError));
    assert(replacementError == L"previous clipboard paste is still pending" &&
           unicodeCharacters == beforePendingUnicode);
    testForeground = testFocus = nullptr;
    testTickOffset = 0;
    SetEvent(consume);
    for (int wait = 0; wait < 1000; ++wait) {
        MSG message;
        PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
        if (selection_context::ReadClipboardText(text) && text == L"original") break;
        Sleep(1);
    }
    assert(text == L"original");
    GetWindowTextW(peerEdit, editText, 64);
    assert(std::wstring(editText) == L"first");
    worker.request_stop();
    worker.join();
    CloseHandle(consume);
    platform::CleanupClipboardOnExit();

    // --- Pre-injection target guard, t2->t3 window-switch guard & clipboard fallback ---
    auto registerClassIfNeeded = [](const wchar_t* className) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = className;
        RegisterClassW(&wc);
    };
    const wchar_t* testClasses[] = {
        L"Progman", L"WorkerW", L"SysListView32", L"Shell_TrayWnd",
        L"CabinetWClass", L"DirectUIHWND", L"VoxType.Hud",
        L"VoxType.Main", L"VoxType.Settings"
    };
    for (const wchar_t* cls : testClasses) {
        registerClassIfNeeded(cls);
    }

    // 1. NoForegroundWindow (null foreground & minimized foreground)
    {
        g_config.restoreClipboardAfterPaste = false;
        expectExclusion = false;
        InjectionContext ctx;
        ctx.sourceText = L"no fg text";
        ctx.copyToClipboardOnFailure = true;
        forceNullForeground = true;
        const InjectionOutcome outNull = platform::PasteTextImeAwareWithOutcome(L"no fg text", false, &ctx);
        assert(!outNull.delivered && outNull.copiedToClipboard &&
               outNull.reason == InjectionFailureReason::NoForegroundWindow);
        assert(selection_context::ReadClipboardText(text) && text == L"no fg text");
        forceNullForeground = false;

        HWND minimizedWin = CreateWindowExW(0, L"EDIT", L"", WS_MINIMIZE | ES_AUTOHSCROLL,
                                            0, 0, 200, 30, nullptr, nullptr, nullptr, nullptr);
        assert(minimizedWin);
        testForeground = testFocus = minimizedWin;
        const InjectionOutcome outMin = platform::PasteTextImeAwareWithOutcome(L"min text", false, &ctx);
        assert(!outMin.delivered && outMin.copiedToClipboard &&
               outMin.reason == InjectionFailureReason::NoForegroundWindow);
        assert(selection_context::ReadClipboardText(text) && text == L"min text");
        testForeground = testFocus = nullptr;
        DestroyWindow(minimizedWin);
    }

    // 2. NonEditableShellSurface (desktop, taskbar, Explorer view, VoxType HUD) vs Explorer rename Edit
    {
        HWND progman = CreateWindowExW(0, L"Progman", L"Desktop", 0, 0, 0, 100, 100, nullptr, nullptr, nullptr, nullptr);
        HWND workerW = CreateWindowExW(0, L"WorkerW", L"", 0, 0, 0, 100, 100, nullptr, nullptr, nullptr, nullptr);
        HWND desktopList = CreateWindowExW(0, L"SysListView32", L"", WS_CHILD, 0, 0, 100, 100, workerW, nullptr, nullptr, nullptr);
        HWND taskbar = CreateWindowExW(0, L"Shell_TrayWnd", L"", 0, 0, 0, 100, 40, nullptr, nullptr, nullptr, nullptr);
        HWND explorer = CreateWindowExW(0, L"CabinetWClass", L"Explorer", 0, 0, 0, 200, 200, nullptr, nullptr, nullptr, nullptr);
        HWND explorerView = CreateWindowExW(0, L"DirectUIHWND", L"", WS_CHILD, 0, 0, 100, 100, explorer, nullptr, nullptr, nullptr);
        HWND explorerRename = CreateWindowExW(0, L"EDIT", L"", WS_CHILD, 0, 0, 100, 24, explorer, nullptr, nullptr, nullptr);
        HWND voxHud = CreateWindowExW(0, L"VoxType.Hud", L"", 0, 0, 0, 100, 40, nullptr, nullptr, nullptr, nullptr);
        HWND voxMain = CreateWindowExW(0, L"VoxType.Main", L"", 0, 0, 0, 100, 40, nullptr, nullptr, nullptr, nullptr);
        HWND voxSettings = CreateWindowExW(0, L"VoxType.Settings", L"Settings", 0, 0, 0, 200, 200, nullptr, nullptr, nullptr, nullptr);
        HWND voxSettingsEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD, 0, 0, 100, 24, voxSettings, nullptr, nullptr, nullptr);
        assert(progman && workerW && desktopList && taskbar && explorer && explorerView && explorerRename && voxHud && voxMain && voxSettings && voxSettingsEdit);

        InjectionContext ctx;
        ctx.copyToClipboardOnFailure = true;

        g_config.restoreClipboardAfterPaste = false;
        expectExclusion = false;
        testForeground = progman;
        testFocus = progman;
        auto out = platform::PasteTextImeAwareWithOutcome(L"desktop fallback", false, &ctx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::NonEditableShellSurface);
        assert(selection_context::ReadClipboardText(text) && text == L"desktop fallback");
        assert(OpenClipboard(owner) && !GetClipboardData(exclusionFormat));
        CloseClipboard();

        testForeground = workerW;
        testFocus = desktopList;
        out = platform::PasteTextImeAwareWithOutcome(L"workerw fallback", false, &ctx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::NonEditableShellSurface);
        assert(selection_context::ReadClipboardText(text) && text == L"workerw fallback");

        testForeground = taskbar;
        testFocus = taskbar;
        out = platform::PasteTextImeAwareWithOutcome(L"taskbar fallback", false, &ctx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::NonEditableShellSurface);

        testForeground = voxHud;
        testFocus = voxHud;
        out = platform::PasteTextImeAwareWithOutcome(L"hud fallback", false, &ctx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::NonEditableShellSurface);

        testForeground = voxMain;
        testFocus = voxMain;
        out = platform::PasteTextImeAwareWithOutcome(L"main fallback", false, &ctx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::NonEditableShellSurface);
        assert(selection_context::ReadClipboardText(text) && text == L"main fallback");

        // VoxType.Settings IS allowed when focused.
        testForeground = voxSettings;
        testFocus = voxSettingsEdit;
        out = platform::PasteTextImeAwareWithOutcome(L"settings ok", false, &ctx);
        assert(out.delivered && !out.copiedToClipboard &&
               out.reason == InjectionFailureReason::None);
        wchar_t settingsBuf[64] = {};
        GetWindowTextW(voxSettingsEdit, settingsBuf, 64);
        assert(std::wstring(settingsBuf) == L"settings ok");

        // Protected mode: clipboard fallback MUST mark CloudClipboardMonitor exclusion.
        g_config.restoreClipboardAfterPaste = true;
        expectExclusion = true;
        testForeground = explorer;
        testFocus = explorerView;
        out = platform::PasteTextImeAwareWithOutcome(L"explorer protected", false, &ctx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::NonEditableShellSurface);
        assert(selection_context::ReadClipboardText(text) && text == L"explorer protected");
        assert(OpenClipboard(owner) && GetClipboardData(exclusionFormat) != nullptr);
        CloseClipboard();

        // Explorer inline file rename Edit IS editable and MUST deliver!
        testForeground = explorer;
        testFocus = explorerRename;
        out = platform::PasteTextImeAwareWithOutcome(L"renamed", false, &ctx);
        assert(out.delivered && !out.copiedToClipboard &&
               out.reason == InjectionFailureReason::None);
        MSG renameMsg;
        while (PeekMessageW(&renameMsg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&renameMsg);
            DispatchMessageW(&renameMsg);
        }
        wchar_t renameBuf[64] = {};
        GetWindowTextW(explorerRename, renameBuf, 64);
        assert(std::wstring(renameBuf) == L"renamed");
        platform::CleanupClipboardOnExit();

        testForeground = testFocus = nullptr;
        g_config.restoreClipboardAfterPaste = false;
        expectExclusion = false;
        DestroyWindow(voxSettings);
        DestroyWindow(voxMain);
        DestroyWindow(voxHud);
        DestroyWindow(explorer);
        DestroyWindow(taskbar);
        DestroyWindow(workerW);
        DestroyWindow(progman);
    }

    // 3. DisabledOrReadOnlyControl (ES_READONLY Edit and WS_DISABLED Edit)
    {
        HWND readOnlyEdit = CreateWindowExW(0, L"EDIT", L"readonly", ES_READONLY | ES_AUTOHSCROLL,
                                            0, 0, 200, 30, nullptr, nullptr, nullptr, nullptr);
        HWND disabledEdit = CreateWindowExW(0, L"EDIT", L"disabled", WS_DISABLED | ES_AUTOHSCROLL,
                                            0, 0, 200, 30, nullptr, nullptr, nullptr, nullptr);
        assert(readOnlyEdit && disabledEdit);

        InjectionContext ctx;
        ctx.copyToClipboardOnFailure = true;
        testForeground = testFocus = readOnlyEdit;
        auto out = platform::PasteTextImeAwareWithOutcome(L"ro fallback", false, &ctx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::DisabledOrReadOnlyControl);
        assert(selection_context::ReadClipboardText(text) && text == L"ro fallback");

        testForeground = testFocus = disabledEdit;
        out = platform::PasteTextImeAwareWithOutcome(L"disabled fallback", false, &ctx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::DisabledOrReadOnlyControl);
        assert(selection_context::ReadClipboardText(text) && text == L"disabled fallback");

        testForeground = testFocus = nullptr;
        DestroyWindow(disabledEdit);
        DestroyWindow(readOnlyEdit);
    }

    // 4. UIPI blocked (pure integrity comparison + mocked OpenProcess ERROR_ACCESS_DENIED)
    {
        assert(platform::IsHigherIntegrityBlocked(SECURITY_MANDATORY_HIGH_RID,
                                                  SECURITY_MANDATORY_MEDIUM_RID, false));
        assert(!platform::IsHigherIntegrityBlocked(SECURITY_MANDATORY_HIGH_RID,
                                                   SECURITY_MANDATORY_MEDIUM_RID, true));
        assert(!platform::IsHigherIntegrityBlocked(SECURITY_MANDATORY_MEDIUM_RID,
                                                   SECURITY_MANDATORY_MEDIUM_RID, false));

        HWND adminEdit = CreateWindowExW(0, L"EDIT", L"", ES_AUTOHSCROLL,
                                         0, 0, 200, 30, nullptr, nullptr, nullptr, nullptr);
        assert(adminEdit);
        testForeground = testFocus = adminEdit;
        fakeTargetPid = 999999;
        denyTargetProcessOpen = true;
        InjectionContext ctx;
        ctx.copyToClipboardOnFailure = true;
        const auto out = platform::PasteTextImeAwareWithOutcome(L"uipi fallback", false, &ctx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::BlockedByUipi);
        assert(selection_context::ReadClipboardText(text) && text == L"uipi fallback");
        denyTargetProcessOpen = false;
        fakeTargetPid = 0;
        testForeground = testFocus = nullptr;
        DestroyWindow(adminEdit);
    }

    // 5. WindowSwitchedAfterStop (different root, destroyed root, PID mismatch, and toggle off)
    {
        HWND winA = CreateWindowExW(0, L"EDIT", L"", ES_AUTOHSCROLL,
                                    0, 0, 200, 30, nullptr, nullptr, nullptr, nullptr);
        HWND winB = CreateWindowExW(0, L"EDIT", L"", ES_AUTOHSCROLL,
                                    0, 0, 200, 30, nullptr, nullptr, nullptr, nullptr);
        assert(winA && winB);

        DWORD pidA = 0;
        GetWindowThreadProcessId(winA, &pidA);
        InjectionContext ctx;
        ctx.stopRootWindow = winA;
        ctx.stopRootProcessId = pidA;
        ctx.fallbackOnWindowSwitch = true;
        ctx.copyToClipboardOnFailure = true;

        testForeground = testFocus = winB;
        auto out = platform::PasteTextImeAwareWithOutcome(L"switched fallback", false, &ctx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::WindowSwitchedAfterStop);
        assert(selection_context::ReadClipboardText(text) && text == L"switched fallback");

        // Same HWND but PID changed (HWND reuse simulation) must also block!
        ctx.stopRootWindow = winB;
        ctx.stopRootProcessId = pidA + 1;
        out = platform::PasteTextImeAwareWithOutcome(L"pid reuse fallback", false, &ctx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::WindowSwitchedAfterStop);
        assert(selection_context::ReadClipboardText(text) && text == L"pid reuse fallback");

        // Destroyed stopRootWindow must also block cleanly via IsWindow check!
        DestroyWindow(winA);
        ctx.stopRootWindow = winA;
        ctx.stopRootProcessId = pidA;
        out = platform::PasteTextImeAwareWithOutcome(L"closed anchor fallback", false, &ctx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::WindowSwitchedAfterStop);

        // When fallbackOnWindowSwitch == false, delivery proceeds to winB.
        ctx.fallbackOnWindowSwitch = false;
        const UINT beforeSwitchShortcuts = pasteShortcuts;
        out = platform::PasteTextImeAwareWithOutcome(L"ok", false, &ctx);
        assert(out.delivered && !out.copiedToClipboard &&
               out.reason == InjectionFailureReason::None);
        assert(pasteShortcuts == beforeSwitchShortcuts + 1);

        testForeground = testFocus = nullptr;
        DestroyWindow(winB);
    }

    // 6. Pending paste: normal completion, external clipboard change, and shutdown.
    for (int mode = 0; mode < 3; ++mode) {
        g_config.restoreClipboardAfterPaste = true;
        expectExclusion = true;
        assert(selection_context::SetClipboardText(L"preexisting", owner,
                                                   GetClipboardSequenceNumber()).written);
        HANDLE restoreConsume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE pasteConsumed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        assert(restoreConsume && pasteConsumed);
        std::promise<HWND> restoreReady;
        auto restoreTarget = restoreReady.get_future();
        std::jthread restoreWorker([&](std::stop_token stop) {
            assert(SetThreadDesktop(desktop));
            HWND peer = CreateWindowExW(0, L"EDIT", L"", ES_AUTOHSCROLL,
                                        0, 0, 300, 30, nullptr, nullptr, nullptr, nullptr);
            assert(peer);
            restoreReady.set_value(peer);
            WaitForSingleObject(restoreConsume, INFINITE);
            while (!stop.stop_requested()) {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                if (GetWindowTextLengthW(peer) > 0) SetEvent(pasteConsumed);
                Sleep(1);
            }
            DestroyWindow(peer);
        });
        HWND peer = restoreTarget.get();
        assert(platform::PasteViaClipboard(peer, L"transient paste", true));
        HWND otherOwner = CreateWindowExW(0, L"STATIC", L"ExternalCopy", 0,
                                           0, 0, 10, 10, nullptr, nullptr, nullptr, nullptr);
        assert(otherOwner);
        if (mode == 1) {
            assert(selection_context::SetClipboardText(L"external copy", otherOwner,
                                                       GetClipboardSequenceNumber()).written);
            // Ownership loss must not clear the in-flight paste guard.
            platform::OnClipboardRestoreTimer();
        }
        // While peer has not yet pumped WM_PASTE, fallback MUST NOT overwrite the clipboard.
        assert(platform::CopyTextToClipboardFallback(L"fallback survives restore", true, false, nullptr, false)
               == ClipboardCopyStatus::Pending);
        InjectionContext pendingContext;
        forceNullForeground = true;
        const auto pendingOutcome = platform::PasteTextImeAwareWithOutcome(
            L"fallback survives restore", false, &pendingContext);
        forceNullForeground = false;
        assert(!pendingOutcome.delivered && !pendingOutcome.copiedToClipboard &&
               pendingOutcome.clipboardCopyPending);
        const std::wstring offeredText = mode == 1 ? L"external copy" : L"transient paste";
        assert(selection_context::ReadClipboardText(text) && text == offeredText);
        if (mode == 2) {
            platform::CleanupClipboardOnExit();
            assert(selection_context::ReadClipboardText(text) && text == offeredText);
        }

        // Unpause peer and pump SendMessageCallbackW completion on our main thread.
        SetEvent(restoreConsume);
        bool completionNotified = false;
        const ULONGLONG waitStart = ::GetTickCount64();
        while (::GetTickCount64() - waitStart < 2000) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == kClipboardDiagnosticMessage &&
                    msg.wParam == (mode == 0 ? kClipboardFallbackCopied : kClipboardFallbackCancelled)) {
                    completionNotified = true;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (WaitForSingleObject(pasteConsumed, 0) == WAIT_OBJECT_0 &&
                (mode == 2 || completionNotified)) {
                break;
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        }
        assert(WaitForSingleObject(pasteConsumed, 0) == WAIT_OBJECT_0);
        assert(mode == 2 || completionNotified);
        const std::wstring finalClipboard = mode == 0 ? L"fallback survives restore" : offeredText;
        assert(selection_context::ReadClipboardText(text) && text == finalClipboard);
        wchar_t peerBuf[128] = {};
        GetWindowTextW(peer, peerBuf, static_cast<int>(std::size(peerBuf)));
        assert(std::wstring_view(peerBuf) == offeredText);

        // Restore timer firing later MUST keep fallback text instead of rolling back "preexisting".
        testTickOffset = 15000;
        platform::OnClipboardRestoreTimer();
        testTickOffset = 0;
        assert(selection_context::ReadClipboardText(text) && text == finalClipboard);

        // Case 6b: once WM_PASTE has already completed and restore is only retrying via timer,
        // fallback writes immediately without deferring.
        failEmpty = true;
        assert(selection_context::SetClipboardText(L"preexisting2", owner,
                                                   GetClipboardSequenceNumber()).written == false);
        failEmpty = false;
        assert(selection_context::SetClipboardText(L"preexisting2", owner,
                                                   GetClipboardSequenceNumber()).written);
        assert(platform::PasteViaClipboard(peer, L"transient 2", true));
        const ULONGLONG waitStart2 = ::GetTickCount64();
        while (::GetTickCount64() - waitStart2 < 2000) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (selection_context::ReadClipboardText(text) && text == L"preexisting2") {
                break;
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        }
        assert(platform::CopyTextToClipboardFallback(L"immediate fallback 2", true, false, nullptr, false)
               == ClipboardCopyStatus::Copied);
        assert(selection_context::ReadClipboardText(text) && text == L"immediate fallback 2");

        restoreWorker.request_stop();
        restoreWorker.join();
        CloseHandle(restoreConsume);
        CloseHandle(pasteConsumed);
        DestroyWindow(otherOwner);
        g_config.restoreClipboardAfterPaste = false;
        expectExclusion = false;
        platform::CleanupClipboardOnExit();
    }

    // 7. Fail-open privacy rule vs explicit password skip, copyToClipboardOnFailure=false, and rewrite fallback
    {
        assert(selection_context::SetClipboardText(L"unchanged", owner,
                                                   GetClipboardSequenceNumber()).written);
        HWND switchTarget = CreateWindowExW(0, L"EDIT", L"", ES_AUTOHSCROLL,
                                            0, 0, 200, 30, nullptr, nullptr, nullptr, nullptr);
        HWND pwdEdit = CreateWindowExW(0, L"EDIT", L"", ES_PASSWORD | ES_READONLY | ES_AUTOHSCROLL,
                                       0, 0, 200, 30, nullptr, nullptr, nullptr, nullptr);
        assert(switchTarget && pwdEdit);

        // Unknown probe (confirmedSensitiveAtStart == false) MUST still copy on failure.
        InjectionContext unknownCtx;
        unknownCtx.confirmedSensitiveAtStart = false;
        unknownCtx.copyToClipboardOnFailure = true;
        forceNullForeground = true;
        auto out = platform::PasteTextImeAwareWithOutcome(L"unknown probe text", false, &unknownCtx);
        assert(!out.delivered && out.copiedToClipboard && !out.skippedDueToPassword);
        assert(selection_context::ReadClipboardText(text) && text == L"unknown probe text");
        forceNullForeground = false;

        // Confirmed password at t0 MUST skip clipboard fallback and preserve previous clipboard.
        assert(selection_context::SetClipboardText(L"unchanged", owner,
                                                   GetClipboardSequenceNumber()).written);
        InjectionContext pwdStartCtx;
        pwdStartCtx.confirmedSensitiveAtStart = true;
        pwdStartCtx.copyToClipboardOnFailure = true;
        forceNullForeground = true;
        out = platform::PasteTextImeAwareWithOutcome(L"secret123", false, &pwdStartCtx);
        assert(!out.delivered && !out.copiedToClipboard && out.skippedDueToPassword);
        assert(selection_context::ReadClipboardText(text) && text == L"unchanged");
        forceNullForeground = false;

        // Current Win32 ES_PASSWORD control at t3 MUST also skip clipboard fallback.
        InjectionContext normalCtx;
        normalCtx.copyToClipboardOnFailure = true;
        testForeground = testFocus = pwdEdit;
        out = platform::PasteTextImeAwareWithOutcome(L"secret456", false, &normalCtx);
        assert(!out.delivered && !out.copiedToClipboard && out.skippedDueToPassword &&
               out.reason == InjectionFailureReason::DisabledOrReadOnlyControl);
        assert(selection_context::ReadClipboardText(text) && text == L"unchanged");

        // copyToClipboardOnFailure == false MUST NOT touch clipboard.
        InjectionContext disabledCopyCtx;
        disabledCopyCtx.copyToClipboardOnFailure = false;
        testForeground = testFocus = pwdEdit;
        out = platform::PasteTextImeAwareWithOutcome(L"no copy pwd", false, &disabledCopyCtx);
        assert(!out.delivered && !out.copiedToClipboard && out.skippedDueToPassword);
        testForeground = testFocus = nullptr;
        forceNullForeground = true;
        out = platform::PasteTextImeAwareWithOutcome(L"no copy", false, &disabledCopyCtx);
        assert(!out.delivered && !out.copiedToClipboard && !out.skippedDueToPassword);
        assert(selection_context::ReadClipboardText(text) && text == L"unchanged");
        forceNullForeground = false;

        // ReplaceSelectionTextImeAwareWithOutcome copies rewritten text on skip/switch/readonly.
        SelectionContext sel;
        sel.targetWindow = pwdEdit;
        sel.focusWindow = pwdEdit;
        sel.selectedText = L"old";
        sel.capturedWithWin32 = true;
        testForeground = testFocus = switchTarget;
        InjectionContext rewriteCtx;
        rewriteCtx.copyToClipboardOnFailure = true;
        out = platform::ReplaceSelectionTextImeAwareWithOutcome(sel, L"rewritten text", &rewriteCtx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::SelectionRewriteSkipped);
        assert(selection_context::ReadClipboardText(text) && text == L"rewritten text");

        // Read-only Edit control during rewrite MUST trigger DisabledOrReadOnlyControl & clipboard fallback
        // (and skip clipboard on ES_PASSWORD).
        HWND roEdit = CreateWindowExW(0, L"EDIT", L"selected", ES_READONLY | ES_AUTOHSCROLL,
                                      0, 0, 200, 30, nullptr, nullptr, nullptr, nullptr);
        assert(roEdit);
        SendMessageW(roEdit, EM_SETSEL, 0, 8);
        SelectionContext roSel;
        roSel.hasSelection = roSel.capturedWithWin32 = true;
        roSel.selectedText = L"selected";
        roSel.targetWindow = roEdit;
        roSel.focusWindow = roEdit;
        GetWindowThreadProcessId(roEdit, &roSel.processId);
        testForeground = testFocus = roEdit;
        out = platform::ReplaceSelectionTextImeAwareWithOutcome(roSel, L"ro rewrite fallback", &rewriteCtx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::DisabledOrReadOnlyControl);
        assert(selection_context::ReadClipboardText(text) && text == L"ro rewrite fallback");

        // WindowSwitchedAfterStop on rewrite path (switched root + toggle).
        DWORD roPid = 0;
        GetWindowThreadProcessId(roEdit, &roPid);
        rewriteCtx.stopRootWindow = switchTarget;
        rewriteCtx.stopRootProcessId = roPid;
        rewriteCtx.fallbackOnWindowSwitch = true;
        out = platform::ReplaceSelectionTextImeAwareWithOutcome(roSel, L"switched rewrite", &rewriteCtx);
        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::WindowSwitchedAfterStop);
        assert(selection_context::ReadClipboardText(text) && text == L"switched rewrite");
        DestroyWindow(roEdit);

        testForeground = testFocus = nullptr;
        DestroyWindow(pwdEdit);
        DestroyWindow(switchTarget);
    }

    // 8. SendUnicodeText target binding & foreground switch during capturePreSelection
    {
        HWND winA = CreateWindowExW(0, L"EDIT", L"", ES_AUTOHSCROLL,
                                    0, 0, 200, 30, nullptr, nullptr, nullptr, nullptr);
        HWND winB = CreateWindowExW(0, L"EDIT", L"", ES_AUTOHSCROLL,
                                    0, 0, 200, 30, nullptr, nullptr, nullptr, nullptr);
        assert(winA && winB);

        // Direct mismatch check: expectedForeground must stop SendUnicodeText before typing.
        testForeground = testFocus = winB;
        assert(!platform::SendUnicodeText(L"wrong target", winA));
        wchar_t winBBuf[128] = {};
        GetWindowTextW(winB, winBBuf, static_cast<int>(std::size(winBBuf)));
        assert(winBBuf[0] == L'\0');

        // Foreground switch during capturePreSelection MUST abort forceUnicodeInput delivery
        // and fall back to clipboard instead of typing into winB.
        assert(selection_context::SetClipboardText(L"before capture switch", owner,
                                                   GetClipboardSequenceNumber()).written);
        DWORD pidA = 0;
        GetWindowThreadProcessId(winA, &pidA);
        InjectionContext captureSwitchCtx;
        captureSwitchCtx.capturePreSelection = true;
        captureSwitchCtx.copyToClipboardOnFailure = true;
        captureSwitchCtx.fallbackOnWindowSwitch = true;
        captureSwitchCtx.stopRootWindow = winA;
        captureSwitchCtx.stopRootProcessId = pidA;

        testForeground = testFocus = winA;
        switchForegroundTarget = winB;
        switchFocusTarget = winB;
        switchAfterGetForegroundCount = 2;
        auto out = platform::PasteTextImeAwareWithOutcome(
            L"after capture switch", true, &captureSwitchCtx);
        switchAfterGetForegroundCount = 0;
        switchForegroundTarget = switchFocusTarget = nullptr;

        assert(!out.delivered && out.copiedToClipboard &&
               out.reason == InjectionFailureReason::WindowSwitchedAfterStop);
        assert(selection_context::ReadClipboardText(text) && text == L"after capture switch");
        GetWindowTextW(winB, winBBuf, static_cast<int>(std::size(winBBuf)));
        assert(winBBuf[0] == L'\0');

        // Selection rewrite must bind both Unicode routes to the captured window.
        HWND customFocus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD,
                                           0, 0, 20, 20, winA, nullptr, nullptr, nullptr);
        assert(customFocus);
        for (bool standardControl : {true, false}) {
            SetWindowTextW(winA, L"selected");
            SendMessageW(winA, EM_SETSEL, 0, 8);
            SelectionContext rewriteSelection;
            rewriteSelection.hasSelection = true;
            rewriteSelection.capturedWithWin32 = standardControl;
            rewriteSelection.selectedText = L"selected";
            rewriteSelection.targetWindow = winA;
            rewriteSelection.focusWindow = standardControl ? winA : customFocus;
            GetWindowThreadProcessId(winA, &rewriteSelection.processId);
            testForeground = winA;
            testFocus = rewriteSelection.focusWindow;
            switchForegroundTarget = switchFocusTarget = winB;
            switchAfterGetForegroundCount = 4;
            g_config.restoreClipboardAfterPaste = true;
            expectExclusion = true;
            failEmpty = true;
            std::wstring rewriteError;
            const UINT beforeRewriteUnicode = unicodeCharacters;
            assert(!platform::ReplaceSelectionTextImeAware(
                rewriteSelection, L"wrong rewrite", &rewriteError));
            assert(unicodeCharacters == beforeRewriteUnicode);
            failEmpty = false;
            switchAfterGetForegroundCount = 0;
            switchForegroundTarget = switchFocusTarget = nullptr;
            GetWindowTextW(winB, winBBuf, static_cast<int>(std::size(winBBuf)));
            assert(winBBuf[0] == L'\0');
        }
        g_config.restoreClipboardAfterPaste = false;
        expectExclusion = false;

        testForeground = testFocus = nullptr;
        DestroyWindow(winB);
        DestroyWindow(winA);
    }

    for (const wchar_t* cls : testClasses) {
        UnregisterClassW(cls, GetModuleHandleW(nullptr));
    }
    platform::CleanupClipboardOnExit();

    DestroyWindow(owner);
    assert(SetThreadDesktop(oldDesktop));
    assert(SetProcessWindowStation(oldStation));
    CloseDesktop(desktop);
    CloseWindowStation(station);
    std::puts("[PASS] Clipboard rollback, diagnostics throttling, pending paste, sequence race and Unicode rewrite fallback (isolated station)");
}
