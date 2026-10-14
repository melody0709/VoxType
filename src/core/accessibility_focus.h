#pragma once

#include "browser_target.h"
#include <uiautomation.h>

namespace accessibility_focus {

// Resolve only the captured HWND. Global focus and screen-point queries can
// activate Chrome accessibility if the user switches apps while a worker runs.
inline HRESULT ElementFromWindow(IUIAutomation* automation, HWND anchor,
                                  IUIAutomationElement** element) {
    if (!element) return E_POINTER;
    *element = nullptr;
    if (!automation || !IsWindow(anchor) || browser_target::IdentifyTopLevel(anchor).IsBrowser()) {
        return E_ACCESSDENIED;
    }
    return automation->ElementFromHandle(anchor, element);
}

inline HRESULT FocusedElementWithinWindow(IUIAutomation* automation, HWND anchor,
                                           IUIAutomationElement** focused) {
    if (!focused) return E_POINTER;
    *focused = nullptr;
    IUIAutomationElement* root = nullptr;
    HRESULT hr = ElementFromWindow(automation, anchor, &root);
    if (FAILED(hr) || !root) return FAILED(hr) ? hr : E_FAIL;
    BOOL hasFocus = FALSE;
    if (root->get_CurrentHasKeyboardFocus(&hasFocus) == S_OK && hasFocus) {
        *focused = root;
        return S_OK;
    }
    VARIANT value{};
    value.vt = VT_BOOL;
    value.boolVal = VARIANT_TRUE;
    IUIAutomationCondition* condition = nullptr;
    hr = automation->CreatePropertyCondition(UIA_HasKeyboardFocusPropertyId, value, &condition);
    if (SUCCEEDED(hr) && condition) {
        hr = root->FindFirst(TreeScope_Descendants, condition, focused);
        condition->Release();
    }
    root->Release();
    return hr;
}

} // namespace accessibility_focus
