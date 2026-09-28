#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <uiautomation.h>
#include <oleacc.h>

#include <string>
#include <set>
#include <memory>
#include <mutex>
#include <thread>
#include <future>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <limits>

#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "oleacc.lib")
#pragma comment(lib, "oleaut32.lib")

struct InputContextResult {
    int successLayer = -1;
    std::wstring windowTitle;
    std::wstring inputFieldText;
    double elapsedMs = 0.0;
    bool timedOut = false;
    std::string failReason;
    int textLength = 0;
    std::string controlType;
    std::string focusWindowClass;
    bool isPassword = false;
};

extern InputContextResult g_inputContextResult;
extern std::mutex g_inputContextMutex;

namespace input_context {

inline const char* LayerName(int layer) {
    switch (layer) {
        case -1: return "NONE";
        case 0:  return "L0_TITLE";
        case 1:  return "L1_WM_GETTEXT";
        case 2:  return "L2_UIA_VALUE";
        case 3:  return "L2.5_TEXTPATTERN";
        case 4:  return "L2.7_MSAA";
        case 5:  return "L3_TEXTPATTERN2";
        default: return "UNKNOWN";
    }
}

inline std::wstring TruncateForDisplay(const std::wstring& text, size_t maxLen = 80) {
    if (text.size() <= maxLen) return text;
    return L"..." + text.substr(text.size() - maxLen + 3);
}

inline bool IsHighSurrogate(wchar_t ch) {
    return ch >= 0xD800 && ch <= 0xDBFF;
}

inline bool IsLowSurrogate(wchar_t ch) {
    return ch >= 0xDC00 && ch <= 0xDFFF;
}

// `std::wstring` is UTF-16 on Windows.  UI Automation reports character
// counts in a way that is usually compatible with UTF-16 code units, but a
// suffix/prefix cut must still avoid returning a lone surrogate.  Keeping
// these helpers here lets all provider-specific limits share the same safe
// boundary behavior.
inline std::wstring TakeFirstN(const std::wstring& text, size_t n) {
    if (text.size() <= n) return text;
    size_t end = n;
    if (end > 0 && end < text.size() && IsHighSurrogate(text[end - 1]) &&
        IsLowSurrogate(text[end])) {
        --end;
    }
    return text.substr(0, end);
}

inline std::wstring TakeLastN(const std::wstring& text, size_t n) {
    if (text.size() <= n) return text;
    size_t start = text.size() - n;
    if (start > 0 && start < text.size() && IsLowSurrogate(text[start]) &&
        IsHighSurrogate(text[start - 1])) {
        ++start;
    }
    return text.substr(start);
}

inline std::mutex s_triggeredWindowsMutex;
inline std::set<HWND> s_triggeredWindows;

inline void EnsureAccessibilityTree(HWND hwnd) {
    if (!hwnd) return;
    {
        std::lock_guard<std::mutex> lock(s_triggeredWindowsMutex);
        if (s_triggeredWindows.count(hwnd)) return;
        s_triggeredWindows.insert(hwnd);
    }
    SendMessageW(hwnd, WM_GETOBJECT, 0, (LPARAM)0xFFFFFFFC);
}

inline std::wstring GetForegroundWindowTitle() {
    HWND fgWnd = GetForegroundWindow();
    if (!fgWnd) return L"";
    wchar_t title[256] = {};
    GetWindowTextW(fgWnd, title, 256);
    return title;
}

inline std::string GetControlTypeName(CONTROLTYPEID ct) {
    switch (ct) {
        case UIA_EditControlTypeId: return "Edit";
        case UIA_DocumentControlTypeId: return "Document";
        case UIA_TextControlTypeId: return "Text";
        case UIA_WindowControlTypeId: return "Window";
        case UIA_PaneControlTypeId: return "Pane";
        case UIA_CustomControlTypeId: return "Custom";
        default: return "Type" + std::to_string(ct);
    }
}

// Three states for the UIA password property: only an explicit boolean false
// means "not a password control". A failed or undecidable read must never be
// mistaken for it, or the fail-closed probe would call an unknown control Safe.
enum : int {
    kPasswordQueryFailed = -1,
    kPasswordQueryNotPassword = 0,
    kPasswordQueryPassword = 1,
};

// The payload signal on its own: an explicit TRUE means the control IS a
// password field. Used by the text reader, which must stop on that signal
// whatever status the call reported.
inline bool PasswordPayloadIsTrue(const VARIANT& value) {
    return value.vt == VT_BOOL && value.boolVal == VARIANT_TRUE;
}

// Pure mapping of the raw property read; unit-tested instead of trusted. Stricter
// than the payload rule above: only a canonical value returned with S_OK may
// decide, so the fail-closed probe never reads an undecidable answer as Safe.
inline int PasswordQueryState(HRESULT hr, const VARIANT& value) {
    if (hr != S_OK) return kPasswordQueryFailed;
    if (value.vt != VT_BOOL) return kPasswordQueryFailed;
    if (PasswordPayloadIsTrue(value)) return kPasswordQueryPassword;
    if (value.boolVal == VARIANT_FALSE) return kPasswordQueryNotPassword;
    return kPasswordQueryFailed;
}

inline int ReadPasswordQueryState(IUIAutomationElement* pElement) {
    if (!pElement) return kPasswordQueryFailed;
    VARIANT var;
    VariantInit(&var);
    const HRESULT hr = pElement->GetCurrentPropertyValue(UIA_IsPasswordPropertyId, &var);
    const int state = PasswordQueryState(hr, var);
    // Initialized before the call and cleared on every path: a failed read leaves
    // the VARIANT untouched, and clearing an uninitialized one is undefined.
    VariantClear(&var);
    return state;
}

inline bool IsPasswordElement(IUIAutomationElement* pElement) {
    // Reader rule: only an explicit TRUE payload stops ordinary field extraction,
    // so an undecidable read never blocks text capture in controls whose UIA
    // property is temporarily unavailable. The status is deliberately not
    // consulted: a contradictory one (S_FALSE with a TRUE payload) must not turn a
    // password signal back into "keep reading". The sensitive-focus probe uses
    // ReadPasswordQueryState() instead, whose rule is stricter still.
    if (!pElement) return false;
    VARIANT var;
    VariantInit(&var);
    pElement->GetCurrentPropertyValue(UIA_IsPasswordPropertyId, &var);
    const bool password = PasswordPayloadIsTrue(var);
    VariantClear(&var);
    return password;
}

inline bool TryGetValueText(IUIAutomationElement* pElement,
                            size_t maxTextCharacters,
                            InputContextResult& result) {
    VARIANT varValue;
    VariantInit(&varValue);
    HRESULT hr = pElement->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &varValue);
    if (SUCCEEDED(hr) && varValue.vt == VT_BSTR && varValue.bstrVal && varValue.bstrVal[0] != L'\0') {
        std::wstring text = varValue.bstrVal;
        VariantClear(&varValue);
        result.textLength = static_cast<int>(text.size());
        result.inputFieldText = TakeLastN(text, maxTextCharacters);
        result.successLayer = 2;
        return true;
    }
    VariantClear(&varValue);
    return false;
}

inline bool TryTextPatternFromPoint(IUIAutomationElement* pElement, POINT pt,
                                    size_t maxTextCharacters,
                                    InputContextResult& result) {
    IUIAutomationTextPattern* pTP = nullptr;
    HRESULT hr = pElement->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pTP));
    if (FAILED(hr) || !pTP) return false;

    IUIAutomationTextRange* pRange = nullptr;
    hr = pTP->RangeFromPoint(pt, &pRange);
    if (SUCCEEDED(hr) && pRange) {
        int moved = 0;
        pRange->Move(TextUnit_Character, -100, &moved);
        BSTR text = nullptr;
        const int readCount = static_cast<int>((std::min)(
            maxTextCharacters, static_cast<size_t>((std::numeric_limits<int>::max)())));
        pRange->GetText(readCount, &text);
        pRange->Release();
        pTP->Release();

        if (text && text[0] != L'\0') {
            std::wstring wtext = text;
            SysFreeString(text);
            result.textLength = static_cast<int>(wtext.size());
            result.inputFieldText = TakeLastN(wtext, maxTextCharacters);
            result.successLayer = 3;
            return true;
        }
        if (text) SysFreeString(text);
        return false;
    }
    pTP->Release();
    return false;
}

inline bool TryTextPatternVisibleRanges(IUIAutomationElement* pElement,
                                        size_t maxTextCharacters,
                                        InputContextResult& result) {
    IUIAutomationTextPattern* pTP = nullptr;
    HRESULT hr = pElement->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pTP));
    if (FAILED(hr) || !pTP) return false;

    IUIAutomationTextRangeArray* pRanges = nullptr;
    hr = pTP->GetVisibleRanges(&pRanges);
    pTP->Release();
    if (FAILED(hr) || !pRanges) return false;

    int count = 0;
    pRanges->get_Length(&count);
    if (count == 0) {
        pRanges->Release();
        return false;
    }

    std::wstring allText;
    for (int i = 0; i < count; i++) {
        IUIAutomationTextRange* pRange = nullptr;
        pRanges->GetElement(i, &pRange);
        if (!pRange) continue;
        BSTR text = nullptr;
        const size_t readLimit = (std::max)(maxTextCharacters, static_cast<size_t>(1));
        const int readCount = static_cast<int>((std::min)(
            readLimit, static_cast<size_t>((std::numeric_limits<int>::max)())));
        pRange->GetText(readCount, &text);
        if (text) {
            allText += text;
            SysFreeString(text);
        }
        pRange->Release();
        if (allText.size() >= maxTextCharacters) break;
    }
    pRanges->Release();

    if (allText.empty()) return false;
    result.textLength = static_cast<int>(allText.size());
    result.inputFieldText = TakeLastN(allText, maxTextCharacters);
    result.successLayer = 3;
    return true;
}

inline bool TryTextPattern2Caret(IUIAutomationElement* pElement,
                                 size_t maxTextCharacters,
                                 InputContextResult& result) {
    IUIAutomationTextPattern2* pTP2 = nullptr;
    HRESULT hr = pElement->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&pTP2));
    if (FAILED(hr) || !pTP2) return false;

    IUIAutomationTextRange* pCaretRange = nullptr;
    BOOL isActive = FALSE;
    hr = pTP2->GetCaretRange(&isActive, &pCaretRange);
    if (FAILED(hr) || !pCaretRange) {
        pTP2->Release();
        return false;
    }

    int moved = 0;
    pCaretRange->Move(TextUnit_Character, -100, &moved);
    BSTR text = nullptr;
    const int readCount = static_cast<int>((std::min)(
        maxTextCharacters, static_cast<size_t>((std::numeric_limits<int>::max)())));
    pCaretRange->GetText(readCount, &text);
    pCaretRange->Release();
    pTP2->Release();

    if (text && text[0] != L'\0') {
        std::wstring wtext = text;
        SysFreeString(text);
        result.textLength = static_cast<int>(wtext.size());
        result.inputFieldText = TakeLastN(wtext, maxTextCharacters);
        result.successLayer = 5;
        return true;
    }
    if (text) SysFreeString(text);
    return false;
}

inline bool TryReadFromElement(IUIAutomationElement* pElement, POINT pt, bool hasPt,
                               size_t maxTextCharacters,
                               InputContextResult& result) {
    if (!pElement) return false;

    // Check the password flag before ANY read pattern: TextPattern, ValuePattern
    // and the caret-based readers can all return the very text this check
    // protects, so detecting it afterwards is too late.
    if (IsPasswordElement(pElement)) {
        result.isPassword = true;
        result.failReason = "PASSWORD";
        result.inputFieldText.clear();
        return false;
    }

    CONTROLTYPEID ct = 0;
    pElement->get_CurrentControlType(&ct);
    result.controlType = GetControlTypeName(ct);

    if (TryGetValueText(pElement, maxTextCharacters, result)) return true;

    if (hasPt && TryTextPatternFromPoint(pElement, pt, maxTextCharacters, result)) return true;

    if (TryTextPatternVisibleRanges(pElement, maxTextCharacters, result)) return true;

    if (TryTextPattern2Caret(pElement, maxTextCharacters, result)) return true;

    return false;
}

inline bool TryWalkParentsForText(IUIAutomation* pAutomation,
                                   IUIAutomationElement* pStart,
                                   POINT pt, bool hasPt,
                                   size_t maxTextCharacters,
                                   InputContextResult& result) {
    if (IsPasswordElement(pStart)) {
        result.isPassword = true;
        result.inputFieldText.clear();
        return false;
    }

    IUIAutomationTreeWalker* pWalker = nullptr;
    HRESULT hr = pAutomation->get_ControlViewWalker(&pWalker);
    if (FAILED(hr) || !pWalker) return false;

    IUIAutomationElement* pCurrent = pStart;
    pCurrent->AddRef();

    for (int depth = 0; depth < 8; depth++) {
        IUIAutomationElement* pParent = nullptr;
        hr = pWalker->GetParentElement(pCurrent, &pParent);
        pCurrent->Release();
        pCurrent = nullptr;

        if (FAILED(hr) || !pParent) break;

        CONTROLTYPEID parentCt = 0;
        pParent->get_CurrentControlType(&parentCt);

        if (parentCt == UIA_WindowControlTypeId) {
            pParent->Release();
            pCurrent = nullptr;
            break;
        }

        if (TryReadFromElement(pParent, pt, hasPt, maxTextCharacters, result)) {
            pParent->Release();
            pWalker->Release();
            return true;
        }

        if (result.isPassword) {
            // A password control appeared in the ancestor chain: stop walking so
            // no higher-level container can hand back its text.
            pParent->Release();
            break;
        }

        pCurrent = pParent;
    }

    if (pCurrent) pCurrent->Release();
    pWalker->Release();
    return false;
}

inline InputContextResult ReadInputFieldTextUIA(const std::wstring& windowTitle,
                                                HWND fgWnd,
                                                size_t maxTextCharacters = 200) {
    InputContextResult result;
    result.windowTitle = windowTitle;

    if (!fgWnd) {
        result.failReason = "NO_FGWND";
        return result;
    }

    DWORD fgTid = GetWindowThreadProcessId(fgWnd, nullptr);

    GUITHREADINFO guiInfo = {};
    guiInfo.cbSize = sizeof(guiInfo);
    POINT caretScreenPt = {};
    bool hasCaretPt = false;
    HWND hwndFocus = nullptr;

    if (GetGUIThreadInfo(fgTid, &guiInfo)) {
        hwndFocus = guiInfo.hwndFocus;
        if (guiInfo.hwndCaret) {
            caretScreenPt.x = guiInfo.rcCaret.left;
            caretScreenPt.y = guiInfo.rcCaret.top;
            ClientToScreen(guiInfo.hwndCaret, &caretScreenPt);
            hasCaretPt = true;
        }
    }

    if (hwndFocus) {
        wchar_t className[256] = {};
        GetClassNameW(hwndFocus, className, 256);
        char classNameUtf8[512] = {};
        WideCharToMultiByte(CP_UTF8, 0, className, -1, classNameUtf8, 512, nullptr, nullptr);
        result.focusWindowClass = classNameUtf8;

        if (_wcsicmp(className, L"Edit") == 0) {
            // A password edit answers WM_GETTEXT with its real contents, so the
            // style must be checked before this fast path reads anything.
            if ((GetWindowLongW(hwndFocus, GWL_STYLE) & ES_PASSWORD) != 0) {
                result.isPassword = true;
                result.failReason = "PASSWORD";
                return result;
            }
            wchar_t buf[4096] = {};
            LRESULT sent = SendMessageTimeoutW(hwndFocus, WM_GETTEXT, 4095,
                                               (LPARAM)buf, SMTO_ABORTIFHUNG, 500, nullptr);
            if (sent != 0 && buf[0] != L'\0') {
                std::wstring text = buf;
                result.textLength = static_cast<int>(text.size());
                result.inputFieldText = TakeLastN(text, maxTextCharacters);
                result.successLayer = 1;
                return result;
            }
        }
    }

    EnsureAccessibilityTree(fgWnd);

    IUIAutomation* pAutomation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                                  (void**)&pAutomation);
    if (FAILED(hr) || !pAutomation) {
        result.failReason = "UIA_CREATE_FAILED";
        return result;
    }

    POINT uiaPt = caretScreenPt;
    if (!hasCaretPt) {
        RECT fgRect = {};
        GetWindowRect(fgWnd, &fgRect);
        uiaPt.x = (fgRect.left + fgRect.right) / 2;
        uiaPt.y = fgRect.top + (fgRect.bottom - fgRect.top) * 2 / 3;
    }

    IUIAutomationElement* pFocused = nullptr;
    hr = pAutomation->GetFocusedElement(&pFocused);
    if (SUCCEEDED(hr) && pFocused) {
        if (TryReadFromElement(pFocused, uiaPt, hasCaretPt, maxTextCharacters, result)) {
            pFocused->Release();
            pAutomation->Release();
            return result;
        }
        if (result.isPassword) {
            pFocused->Release();
            pAutomation->Release();
            return result;
        }

        if (TryWalkParentsForText(pAutomation, pFocused, uiaPt, hasCaretPt,
                                  maxTextCharacters, result)) {
            pFocused->Release();
            pAutomation->Release();
            return result;
        }
        pFocused->Release();
    }

    if (result.isPassword) {
        // The focused control is a password field: terminate the whole chain.
        // The from-point element and the MSAA fallback below can both return the
        // password text, and the caller only needs to know "not readable".
        result.inputFieldText.clear();
        pAutomation->Release();
        return result;
    }

    IUIAutomationElement* pFromPt = nullptr;
    hr = pAutomation->ElementFromPoint(uiaPt, &pFromPt);
    if (SUCCEEDED(hr) && pFromPt) {
        if (TryReadFromElement(pFromPt, uiaPt, hasCaretPt, maxTextCharacters, result)) {
            pFromPt->Release();
            pAutomation->Release();
            return result;
        }
        if (result.isPassword) {
            pFromPt->Release();
            pAutomation->Release();
            return result;
        }

        if (TryWalkParentsForText(pAutomation, pFromPt, uiaPt, hasCaretPt,
                                  maxTextCharacters, result)) {
            pFromPt->Release();
            pAutomation->Release();
            return result;
        }
        pFromPt->Release();
    }

    if (result.isPassword) {
        result.inputFieldText.clear();
        pAutomation->Release();
        return result;
    }

    if (hwndFocus) {
        IAccessible* pAcc = nullptr;
        hr = AccessibleObjectFromWindow(hwndFocus, OBJID_WINDOW,
                                        IID_IAccessible, (void**)&pAcc);
        if (SUCCEEDED(hr) && pAcc) {
            VARIANT varSelf;
            varSelf.vt = VT_I4;
            varSelf.lVal = CHILDID_SELF;
            BSTR value = nullptr;
            hr = pAcc->get_accValue(varSelf, &value);
            if (SUCCEEDED(hr) && value && value[0] != L'\0') {
                std::wstring text = value;
                result.textLength = static_cast<int>(text.size());
                result.inputFieldText = TakeLastN(text, maxTextCharacters);
                result.successLayer = 4;
                SysFreeString(value);
                pAcc->Release();
                pAutomation->Release();
                return result;
            }
            if (value) SysFreeString(value);
            pAcc->Release();
        }
    }

    pAutomation->Release();
    result.failReason = "UIA_EMPTY";
    return result;
}

// One flag per UI Automation operation: the field reader and the sensitive-focus
// probe run next to each other (the probe is started first so its query lands as
// close to recording start as possible), and each flag is released by its own
// worker thread when it really finishes.
inline std::atomic<bool> s_uiaReadRunning{false};
inline std::atomic<bool> s_uiaProbeRunning{false};

// True when the control is the classic Win32 password edit. Cheap and
// synchronous (no UI Automation), so it is safe on the recording-start path. It
// takes the focus window instead of re-querying it: the probe answers this from
// the very same synchronous snapshot it uses as its cross-check anchor, so the
// two cannot disagree.
inline bool IsWin32PasswordEditWindow(HWND window) {
    if (!window) return false;
    wchar_t className[64] = {};
    GetClassNameW(window, className, 64);
    if (_wcsicmp(className, L"Edit") != 0) return false;
    return (GetWindowLongW(window, GWL_STYLE) & ES_PASSWORD) != 0;
}

// Answers of the sensitive-focus probe. Only kSensitiveProbeSafe lets the shared
// recognition history keep the transcript; every other code is "unknown" and is
// treated as sensitive (see MustSkipHistoryForFocus). The unknown codes are
// carried into the log so a missing history entry stays diagnosable.
inline constexpr int kSensitiveProbeSensitive = 1;
inline constexpr int kSensitiveProbeSafe = 2;
inline constexpr int kSensitiveProbeUnknownNotStarted = 0;
inline constexpr int kSensitiveProbeUnknownNoProbe = 10;
inline constexpr int kSensitiveProbeUnknownProbeBusy = 11;
inline constexpr int kSensitiveProbeUnknownUiaUnavailable = 12;
inline constexpr int kSensitiveProbeUnknownNoFocusedElement = 13;
inline constexpr int kSensitiveProbeUnknownNoHandle = 14;
inline constexpr int kSensitiveProbeUnknownFocusMoved = 15;
inline constexpr int kSensitiveProbeUnknownNoStartFocus = 16;
inline constexpr int kSensitiveProbeUnknownPasswordQuery = 17;

// Focus identity of the moment recording starts, readable synchronously: the
// foreground window and the control it reports as focused. It must be captured
// BEFORE the focused-field read (which can wait for its UI Automation timeout),
// because the probe is asynchronous and has to prove that the element it
// inspects still belongs to the window that had the focus back then.
struct SensitiveFocusAnchor {
    HWND foreground = nullptr;
    HWND focus = nullptr;
};

inline SensitiveFocusAnchor CaptureSensitiveFocusAnchor() {
    SensitiveFocusAnchor anchor;
    anchor.foreground = GetForegroundWindow();
    if (!anchor.foreground) return anchor;
    GUITHREADINFO guiInfo = {};
    guiInfo.cbSize = sizeof(guiInfo);
    if (GetGUIThreadInfo(GetWindowThreadProcessId(anchor.foreground, nullptr), &guiInfo)) {
        anchor.focus = guiInfo.hwndFocus;
    }
    return anchor;
}

// True when an element's window still belongs to the window that was focused
// when recording started. HWND level only: a switch between two elements inside
// one window (a browser password field and a normal field share the render
// widget) cannot be distinguished here. That residual gap is exactly why the
// probe is started before the focused-field read — this is a cross-check, not a
// start-of-recording element snapshot.
inline bool MatchesSensitiveFocusAnchor(HWND elementWindow,
                                        const SensitiveFocusAnchor& anchor) {
    // Without both halves of the anchor there is nothing to compare against, so
    // the asynchronous query can never be accepted as Safe.
    if (!elementWindow || !anchor.foreground || !anchor.focus) return false;
    if (elementWindow == anchor.foreground || elementWindow == anchor.focus) return true;
    const HWND elementRoot = GetAncestor(elementWindow, GA_ROOT);
    return elementRoot != nullptr && elementRoot == GetAncestor(anchor.foreground, GA_ROOT);
}

// Pure mapping of the synchronous recording-start snapshot. kSensitiveProbeUnknownNotStarted
// is the only answer that means "now run the UI Automation worker"; every other
// value is final.
inline int InitialSensitiveProbeAnswer(bool hasForegroundWindow,
                                       bool hasFocusedControl,
                                       bool win32PasswordEdit) {
    // The classic Win32 password edit answers without UI Automation and cannot
    // move to another control unnoticed, so it needs no cross-check.
    if (win32PasswordEdit) return kSensitiveProbeSensitive;
    if (!hasForegroundWindow) return kSensitiveProbeUnknownNoFocusedElement;
    // A window that reports no focused control gives the cross-check nothing to
    // compare with, so an asynchronous "not a password" answer must not be
    // trusted as Safe.
    if (!hasFocusedControl) return kSensitiveProbeUnknownNoStartFocus;
    return kSensitiveProbeUnknownNotStarted;
}

// Pure mapping of the UI Automation result into a probe answer:
//   - an explicit password hit is always Sensitive (it is a password control even
//     if it is no longer inside the anchored window);
//   - a failed or undecidable property read is Unknown, never Safe;
//   - "not a password" becomes Safe only for an element that still belongs to the
//     window that had the focus when recording started.
inline int SensitiveProbeAnswerFromUia(int passwordState,
                                       bool hasWindowHandle,
                                       bool anchorMatched) {
    if (passwordState == kPasswordQueryPassword) return kSensitiveProbeSensitive;
    if (passwordState != kPasswordQueryNotPassword) return kSensitiveProbeUnknownPasswordQuery;
    if (!hasWindowHandle) return kSensitiveProbeUnknownNoHandle;
    return anchorMatched ? kSensitiveProbeSafe : kSensitiveProbeUnknownFocusMoved;
}

// Starts a detached UI Automation probe and returns a handle that settles to
// Sensitive, Safe, or one of the unknown codes. Callers start it when recording
// starts and read the handle when the transcript arrives, so the probe stays off
// the recording latency path.
//
// It has its own single-flight flag (separate from the field reader) because it
// runs before the focused-field read, and both guards are released by their own
// worker thread.
inline std::shared_ptr<std::atomic<int>> BeginSensitiveFocusProbe() {
    auto state = std::make_shared<std::atomic<int>>(kSensitiveProbeUnknownNotStarted);
    const SensitiveFocusAnchor anchor = CaptureSensitiveFocusAnchor();

    const int initial = InitialSensitiveProbeAnswer(
        anchor.foreground != nullptr, anchor.focus != nullptr,
        IsWin32PasswordEditWindow(anchor.focus));
    if (initial != kSensitiveProbeUnknownNotStarted) {
        state->store(initial, std::memory_order_relaxed);
        return state;
    }
    if (s_uiaProbeRunning.exchange(true)) {
        state->store(kSensitiveProbeUnknownProbeBusy, std::memory_order_relaxed);
        return state;
    }

    std::thread worker([state, anchor]() {
        int answer = kSensitiveProbeUnknownUiaUnavailable;
        if (SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) {
            IUIAutomation* pAutomation = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                           CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                                           (void**)&pAutomation)) && pAutomation) {
                IUIAutomationElement* pFocused = nullptr;
                if (SUCCEEDED(pAutomation->GetFocusedElement(&pFocused)) && pFocused) {
                    // The password property is read first: an explicit hit is
                    // conclusive on its own and needs no window handle at all.
                    const int passwordState = ReadPasswordQueryState(pFocused);
                    bool hasWindowHandle = false;
                    bool anchorMatched = false;
                    if (passwordState == kPasswordQueryNotPassword) {
                        UIA_HWND nativeWindow = nullptr;
                        if (SUCCEEDED(pFocused->get_CurrentNativeWindowHandle(&nativeWindow)) &&
                            nativeWindow != nullptr) {
                            hasWindowHandle = true;
                            anchorMatched = MatchesSensitiveFocusAnchor(
                                reinterpret_cast<HWND>(nativeWindow), anchor);
                        }
                    }
                    answer = SensitiveProbeAnswerFromUia(passwordState, hasWindowHandle,
                                                         anchorMatched);
                    pFocused->Release();
                } else {
                    answer = kSensitiveProbeUnknownNoFocusedElement;
                }
                pAutomation->Release();
            }
            CoUninitialize();
        }
        state->store(answer, std::memory_order_relaxed);
        // Released by the worker when it really finishes, never by a caller that
        // stopped waiting: a detached worker can still be reading after a
        // timeout, and clearing the slot there allowed overlapping UIA threads.
        s_uiaProbeRunning.store(false, std::memory_order_relaxed);
    });
    worker.detach();
    return state;
}

// Reads a probe answer without waiting; a missing probe is "unknown".
inline int SensitiveFocusAnswer(const std::shared_ptr<std::atomic<int>>& state) {
    return state ? state->load(std::memory_order_relaxed) : kSensitiveProbeUnknownNoProbe;
}

// Stable name for the diagnostic log; never null.
inline const char* SensitiveFocusAnswerName(int answer) {
    switch (answer) {
    case kSensitiveProbeSensitive: return "sensitive";
    case kSensitiveProbeSafe: return "safe";
    case kSensitiveProbeUnknownProbeBusy: return "unknown_probe_busy";
    case kSensitiveProbeUnknownUiaUnavailable: return "unknown_uia_unavailable";
    case kSensitiveProbeUnknownNoFocusedElement: return "unknown_no_focused_element";
    case kSensitiveProbeUnknownNoHandle: return "unknown_no_handle";
    case kSensitiveProbeUnknownFocusMoved: return "unknown_focus_moved";
    case kSensitiveProbeUnknownNoProbe: return "unknown_no_probe";
    case kSensitiveProbeUnknownNoStartFocus: return "unknown_no_start_focus";
    case kSensitiveProbeUnknownPasswordQuery: return "unknown_password_query";
    default: return "unknown_not_settled";
    }
}

// Fail-closed mapping for the shared recognition history: only an explicit Safe
// answer lets a transcript be recorded. An unverified focus — or no probe at all
// — counts as sensitive, because leaving the transcript in an uploadable history
// is the higher risk; the loss only affects later context enhancement, since the
// transcript itself is still delivered to the focused window. The skip is logged
// with reason=focus_unknown so missing history in terminals/games/odd toolkits
// stays diagnosable.
inline bool MustSkipHistoryForFocus(const std::shared_ptr<std::atomic<int>>& state) {
    return SensitiveFocusAnswer(state) != kSensitiveProbeSafe;
}

inline InputContextResult GetInputFieldContext(size_t maxTextCharacters = 200) {
    InputContextResult result;
    HWND fgWnd = GetForegroundWindow();
    result.windowTitle = GetForegroundWindowTitle();

    if (s_uiaReadRunning.exchange(true)) {
        result.failReason = "UIA_BUSY";
        return result;
    }

    auto promise = std::make_shared<std::promise<InputContextResult>>();
    auto future = promise->get_future();

    std::wstring wt = result.windowTitle;
    std::thread worker([promise, wt, fgWnd, maxTextCharacters]() {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        InputContextResult r = ReadInputFieldTextUIA(wt, fgWnd, maxTextCharacters);
        CoUninitialize();
        promise->set_value(std::move(r));
        // Released by the worker when it really finishes: a timed-out caller must
        // not free the slot while this thread is still reading, which used to
        // allow overlapping UI Automation threads.
        s_uiaReadRunning.store(false, std::memory_order_relaxed);
    });
    worker.detach();

    if (future.wait_for(std::chrono::milliseconds(200)) == std::future_status::timeout) {
        result.timedOut = true;
        result.failReason = "TIMEOUT";
        return result;
    }

    InputContextResult uiaResult = future.get();
    // Final privacy net: whatever a fallback layer produced, a password control
    // must never yield text (see ReadInputFieldTextUIA for the containment).
    if (uiaResult.isPassword) uiaResult.inputFieldText.clear();
    return uiaResult;
}

}
