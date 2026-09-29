#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <uiautomation.h>
#include <oleauto.h>
#include <objidl.h>
#include <shlobj_core.h>

#include <algorithm>
#include <cwctype>
#include <cstring>
#include <string>

#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

// A snapshot of the text selection that existed when a recognition attempt
// started.  The HWNDs are deliberately kept with the text so a delayed cloud
// result cannot be pasted into whichever application happens to be focused
// later.
struct SelectionContext {
    bool hasSelection = false;
    bool capturedWithUiAutomation = false;
    bool capturedWithWin32 = false;
    std::wstring selectedText;
    HWND targetWindow = nullptr;  // top-level foreground window
    HWND focusWindow = nullptr;   // focused editor/control, when available
    DWORD processId = 0;
    DWORD focusThreadId = 0;
    std::string error;

    bool HasCapturedSelection() const {
        return hasSelection && !selectedText.empty();
    }

    bool Usable() const {
        return HasCapturedSelection() && IsWindow(targetWindow);
    }
};

namespace selection_context {

constexpr size_t kMaxSelectionChars = 200000;

inline HWND TopLevelWindow(HWND hwnd) {
    if (!hwnd) return nullptr;
    HWND root = GetAncestor(hwnd, GA_ROOT);
    return root ? root : hwnd;
}

inline bool HasNonWhitespace(const std::wstring& text) {
    return std::any_of(text.begin(), text.end(), [](wchar_t ch) {
        return !iswspace(ch);
    });
}

inline bool ReadClipboardText(std::wstring& out) {
    out.clear();
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (!OpenClipboard(nullptr)) {
            Sleep(5);
            continue;
        }
        HANDLE handle = GetClipboardData(CF_UNICODETEXT);
        if (handle) {
            const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle));
            if (text) {
                out.assign(text);
                GlobalUnlock(handle);
            }
        }
        CloseClipboard();
        return handle != nullptr;
    }
    return false;
}

// Materialize every offered format before touching the clipboard. Holding the
// OleGetClipboard proxy alone does not keep non-OLE clipboard data alive.
class ClipboardSnapshot {
public:
    ClipboardSnapshot() {
        const HRESULT hr = OleInitialize(nullptr);
        shouldUninitialize_ = SUCCEEDED(hr);
        if (FAILED(hr) && hr != S_FALSE) return;
        if (OpenClipboard(nullptr)) {
            wasEmpty_ = CountClipboardFormats() == 0;
            CloseClipboard();
            if (wasEmpty_) {
                readable_ = true;
                return;
            }
        }
        IDataObject* source = nullptr;
        if (SUCCEEDED(OleGetClipboard(&source)) && source) {
            IEnumFORMATETC* formats = nullptr;
            if (SUCCEEDED(SHCreateDataObject(nullptr, 0, nullptr, nullptr,
                                             IID_PPV_ARGS(&dataObject_))) &&
                SUCCEEDED(source->EnumFormatEtc(DATADIR_GET, &formats)) && formats) {
                readable_ = true;
                FORMATETC format = {};
                HRESULT next;
                size_t copiedFormats = 0;
                while ((next = formats->Next(1, &format, nullptr)) == S_OK) {
                    STGMEDIUM medium = {};
                    const HRESULT got = source->GetData(&format, &medium);
                    if (SUCCEEDED(got)) format.tymed = medium.tymed;
                    const bool copied = SUCCEEDED(got) &&
                        SUCCEEDED(dataObject_->SetData(&format, &medium, TRUE));
                    if (SUCCEEDED(got) && !copied) ReleaseStgMedium(&medium);
                    CoTaskMemFree(format.ptd);
                    format = {};
                    if (!copied) {
                        readable_ = false;
                        break;
                    }
                    ++copiedFormats;
                }
                if (FAILED(next) || copiedFormats == 0) readable_ = false;
            }
            if (formats) formats->Release();
            source->Release();
        }
        if (!readable_ && OpenClipboard(nullptr)) {
            wasEmpty_ = CountClipboardFormats() == 0;
            readable_ = wasEmpty_;
            CloseClipboard();
        }
    }

    ~ClipboardSnapshot() {
        if (dataObject_) dataObject_->Release();
        if (shouldUninitialize_) OleUninitialize();
    }

    ClipboardSnapshot(const ClipboardSnapshot&) = delete;
    ClipboardSnapshot& operator=(const ClipboardSnapshot&) = delete;

    bool Readable() const { return readable_; }
    bool FlushPending() const { return flushPending_; }
    bool IsCurrentOleClipboard() const {
        return flushPending_ && dataObject_ && OleIsCurrentClipboard(dataObject_) == S_OK;
    }

    // Abandons clipboard restoration so that the destructor does not overwrite
    // user clipboard contents when sequence check fails or snapshot is superseded.
    void Dismiss() noexcept { dismissed_ = true; }

    bool Restore() {
        if (dismissed_) return true;
        if (restored_) return true;
        if (!readable_) {
            restored_ = true;
            return true;
        }
        if (wasEmpty_) {
            if (!OpenClipboard(nullptr)) {
                return false;
            }
            const bool emptied = EmptyClipboard() != FALSE;
            CloseClipboard();
            if (!emptied) return false;
            restored_ = true;
            return true;
        }
        if (!dataObject_) {
            restored_ = true;
            return true;
        }
        if (!flushPending_) {
            if (FAILED(OleSetClipboard(dataObject_))) return false;
            flushPending_ = true;
        }
        // Materialize delayed formats before releasing the original IDataObject.
        if (FAILED(OleFlushClipboard())) return false;
        restored_ = true;
        return true;
    }

private:
    IDataObject* dataObject_ = nullptr;
    bool readable_ = false;
    bool restored_ = false;
    bool dismissed_ = false;
    bool flushPending_ = false;
    bool wasEmpty_ = false;
    bool shouldUninitialize_ = false;
};

struct ClipboardWriteResult {
    bool written = false;
    bool changed = false;
    DWORD sequence = 0;
};

inline ClipboardWriteResult SetClipboardText(const std::wstring& text, HWND ownerWnd,
                                            DWORD expectedSequence,
                                            bool excludeFromHistory = true) {
    ClipboardWriteResult result;
    if (!ownerWnd || !expectedSequence || text.empty() ||
        text.size() > SIZE_MAX / sizeof(wchar_t) - 1) return result;

    const UINT cfIgnore = excludeFromHistory
        ? RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing") : 0;
    if (excludeFromHistory && !cfIgnore) return result;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    HGLOBAL ignore = excludeFromHistory ? GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD)) : nullptr;
    if (!memory || (excludeFromHistory && !ignore)) {
        if (memory) GlobalFree(memory);
        if (ignore) GlobalFree(ignore);
        return result;
    }
    void* destination = GlobalLock(memory);
    DWORD* ignoreValue = ignore ? static_cast<DWORD*>(GlobalLock(ignore)) : nullptr;
    if (!destination || (ignore && !ignoreValue)) {
        if (destination) GlobalUnlock(memory);
        if (ignoreValue) GlobalUnlock(ignore);
        GlobalFree(memory);
        GlobalFree(ignore);
        return result;
    }
    memcpy(destination, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    if (ignoreValue) *ignoreValue = 0;
    GlobalUnlock(memory);
    if (ignore) GlobalUnlock(ignore);

    for (int attempt = 0; attempt < 5; ++attempt) {
        if (!OpenClipboard(ownerWnd)) {
            Sleep(5);
            continue;
        }
        if (GetClipboardSequenceNumber() != expectedSequence) {
            CloseClipboard();
            break;
        }
        if (!EmptyClipboard()) {
            CloseClipboard();
            Sleep(5);
            continue;
        }
        result.changed = true;
        bool markerReady = !excludeFromHistory;
        if (excludeFromHistory && SetClipboardData(cfIgnore, ignore)) {
            ignore = nullptr;
            markerReady = true;
        }
        if (markerReady && SetClipboardData(CF_UNICODETEXT, memory)) {
            memory = nullptr;
            result.written = true;
        }
        // Protected writes install the exclusion before the transcript.
        if (!result.written) EmptyClipboard();
        CloseClipboard();
        // Windows can synthesize formats and advance the sequence at close.
        // A pre-close number would reject our own successful write. If another
        // application copied at close, do not submit a paste of its contents.
        if (GetClipboardOwner() == ownerWnd) {
            result.sequence = GetClipboardSequenceNumber();
        } else {
            result.written = false;
        }
        break;
    }
    if (memory) GlobalFree(memory);
    if (ignore) GlobalFree(ignore);
    return result;
}

inline bool IsSelectionPasswordReadable(HRESULT queryResult, BOOL isPassword) {
    return queryResult == S_OK && isPassword == FALSE;
}

inline bool TryReadSelectionWithUiAutomation(HWND focusWindow,
                                             std::wstring& selectedText,
                                             std::string& error) {
    selectedText.clear();
    error.clear();

    HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        error = "COM_INIT_FAILED";
        return false;
    }

    IUIAutomation* automation = nullptr;
    IUIAutomationElement* focused = nullptr;
    IUIAutomationTextPattern* textPattern = nullptr;
    IUIAutomationTextRangeArray* ranges = nullptr;
    bool ok = false;

    do {
        HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                       CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&automation));
        if (FAILED(hr) || !automation) {
            error = "UIA_CREATE_FAILED";
            break;
        }
        hr = automation->GetFocusedElement(&focused);
        if (FAILED(hr) || !focused) {
            error = "UIA_NO_FOCUS";
            break;
        }
        BOOL isPassword = FALSE;
        const HRESULT passwordResult = focused->get_CurrentIsPassword(&isPassword);
        if (!IsSelectionPasswordReadable(passwordResult, isPassword)) {
            error = isPassword ? "PASSWORD_SELECTION" : "PASSWORD_QUERY_FAILED";
            break;
        }

        DWORD focusProcessId = 0;
        if (!focusWindow || !IsWindow(focusWindow) ||
            !GetWindowThreadProcessId(focusWindow, &focusProcessId)) {
            error = "UIA_FOCUS_UNAVAILABLE";
            break;
        }
        int uiaProcessId = 0;
        if (focused->get_CurrentProcessId(&uiaProcessId) != S_OK ||
            uiaProcessId <= 0 ||
            static_cast<DWORD>(uiaProcessId) != focusProcessId) {
            error = "UIA_PROCESS_MISMATCH";
            break;
        }

        UIA_HWND nativeWindowValue = nullptr;
        focused->get_CurrentNativeWindowHandle(&nativeWindowValue);
        HWND nativeWindow = static_cast<HWND>(nativeWindowValue);
        if (focusWindow && nativeWindow &&
            TopLevelWindow(nativeWindow) != TopLevelWindow(focusWindow)) {
            error = "UIA_FOCUS_MISMATCH";
            break;
        }

        hr = focused->GetCurrentPatternAs(UIA_TextPatternId,
                                          IID_PPV_ARGS(&textPattern));
        if (FAILED(hr) || !textPattern) {
            error = "UIA_TEXT_PATTERN_UNAVAILABLE";
            break;
        }
        hr = textPattern->GetSelection(&ranges);
        if (FAILED(hr) || !ranges) {
            error = "UIA_SELECTION_UNAVAILABLE";
            break;
        }

        int count = 0;
        ranges->get_Length(&count);
        for (int index = 0; index < count; ++index) {
            IUIAutomationTextRange* range = nullptr;
            if (FAILED(ranges->GetElement(index, &range)) || !range) continue;
            BSTR text = nullptr;
            const int remaining = static_cast<int>(kMaxSelectionChars - selectedText.size() + 1);
            if (SUCCEEDED(range->GetText(remaining, &text)) && text) {
                selectedText.append(text, SysStringLen(text));
                SysFreeString(text);
            }
            range->Release();
            if (selectedText.size() > kMaxSelectionChars) break;
        }

        if (selectedText.size() > kMaxSelectionChars) {
            selectedText.clear();
            error = "SELECTION_TOO_LARGE";
            break;
        }
        if (!HasNonWhitespace(selectedText)) {
            selectedText.clear();
            error = "NO_NONWHITESPACE_SELECTION";
            break;
        }
        ok = true;
    } while (false);

    if (ranges) ranges->Release();
    if (textPattern) textPattern->Release();
    if (focused) focused->Release();
    if (automation) automation->Release();
    if (shouldUninitialize) CoUninitialize();
    return ok;
}

inline bool TryReadSelectionWithWin32(HWND focusWindow,
                                      std::wstring& selectedText,
                                      std::string& error) {
    selectedText.clear();
    error = "WIN32_SELECTION_UNAVAILABLE";
    wchar_t className[64] = {};
    if (!focusWindow || !GetClassNameW(focusWindow, className, 64) ||
        (_wcsicmp(className, L"Edit") != 0 &&
         _wcsnicmp(className, L"RichEdit", 8) != 0)) return false;
    if (GetWindowLongPtrW(focusWindow, GWL_STYLE) & ES_PASSWORD) {
        error = "PASSWORD_SELECTION";
        return false;
    }
    DWORD start = 0;
    DWORD end = 0;
    DWORD_PTR ignored = 0;
    DWORD_PTR length = 0;
    constexpr UINT flags = SMTO_ABORTIFHUNG | SMTO_BLOCK;
    if (!SendMessageTimeoutW(focusWindow, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
                             reinterpret_cast<LPARAM>(&end), flags, 500, &ignored) ||
        start >= end ||
        !SendMessageTimeoutW(focusWindow, WM_GETTEXTLENGTH, 0, 0, flags, 500, &length) ||
        length > kMaxSelectionChars || end > length) return false;
    std::wstring text(length + 1, L'\0');
    DWORD_PTR copied = 0;
    if (!SendMessageTimeoutW(focusWindow, WM_GETTEXT, text.size(),
                             reinterpret_cast<LPARAM>(text.data()), flags, 500, &copied) ||
        end > copied) return false;
    selectedText = text.substr(start, end - start);
    if (!HasNonWhitespace(selectedText)) {
        selectedText.clear();
        return false;
    }
    error.clear();
    return true;
}

inline SelectionContext Capture() {
    SelectionContext result;
    HWND foreground = GetForegroundWindow();
    result.targetWindow = TopLevelWindow(foreground);
    if (!result.targetWindow) {
        result.error = "NO_FOREGROUND_WINDOW";
        return result;
    }
    result.focusThreadId = GetWindowThreadProcessId(result.targetWindow,
                                                    &result.processId);

    GUITHREADINFO guiInfo = {};
    guiInfo.cbSize = sizeof(guiInfo);
    if (GetGUIThreadInfo(result.focusThreadId, &guiInfo) && guiInfo.hwndFocus) {
        result.focusWindow = guiInfo.hwndFocus;
    } else {
        result.focusWindow = foreground;
    }

    std::string uiaError;
    if (TryReadSelectionWithUiAutomation(result.focusWindow,
                                         result.selectedText, uiaError)) {
        if (TopLevelWindow(GetForegroundWindow()) != result.targetWindow) {
            result.selectedText.clear();
            result.error = "FOREGROUND_CHANGED";
            return result;
        }
        result.hasSelection = true;
        result.capturedWithUiAutomation = true;
        return result;
    }

    if (uiaError == "PASSWORD_SELECTION" || uiaError == "PASSWORD_QUERY_FAILED") {
        result.error = uiaError;
        return result;
    }
    std::string win32Error;
    if (TryReadSelectionWithWin32(result.focusWindow,
                                  result.selectedText, win32Error)) {
        if (TopLevelWindow(GetForegroundWindow()) != result.targetWindow) {
            result.selectedText.clear();
            result.error = "FOREGROUND_CHANGED";
            return result;
        }
        result.hasSelection = true;
        result.capturedWithWin32 = true;
        return result;
    }

    result.error = win32Error.empty() ? uiaError : win32Error;
    if (result.error.empty()) result.error = "SELECTION_NOT_AVAILABLE";
    return result;
}

inline bool IsSameForegroundWindow(const SelectionContext& context) {
    if (!context.Usable()) return false;
    return TopLevelWindow(GetForegroundWindow()) == context.targetWindow;
}

inline bool VerifyCurrentUiAutomationSelection(const SelectionContext& context) {
    if (!context.capturedWithUiAutomation) return true;
    std::wstring current;
    std::string error;
    if (!TryReadSelectionWithUiAutomation(context.focusWindow, current, error)) {
        return false;
    }
    return current == context.selectedText;
}

inline bool VerifyCurrentWin32Selection(const SelectionContext& context) {
    if (!context.capturedWithWin32) return true;
    if (!context.Usable()) return false;

    std::wstring current;
    std::string error;
    if (!TryReadSelectionWithWin32(context.focusWindow, current, error)) {
        return false;
    }
    return current == context.selectedText;
}

inline bool VerifyCurrentSelection(const SelectionContext& context) {
    return VerifyCurrentUiAutomationSelection(context) &&
           VerifyCurrentWin32Selection(context);
}

} // namespace selection_context
