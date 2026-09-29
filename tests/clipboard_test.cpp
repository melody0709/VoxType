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
    return testForeground ? testForeground : ::GetForegroundWindow();
}

BOOL TestGetGUIThreadInfo(DWORD threadId, GUITHREADINFO* info) {
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

    DestroyWindow(owner);
    assert(SetThreadDesktop(oldDesktop));
    assert(SetProcessWindowStation(oldStation));
    CloseDesktop(desktop);
    CloseWindowStation(station);
    std::puts("[PASS] Clipboard rollback, diagnostics throttling, pending paste, sequence race and Unicode rewrite fallback (isolated station)");
}
