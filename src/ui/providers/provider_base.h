#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "config_store.h"

namespace ui_provider {

class ICloudProviderPanel {
public:
    virtual ~ICloudProviderPanel() = default;
    virtual void CreateControls(HWND parent) = 0;
    virtual void DestroyControls() = 0;
    virtual void Show(bool visible) = 0;
    virtual void LoadControls(HWND parent, const Config& cfg) = 0;
    virtual void SaveControls(HWND parent, Config& cfg) = 0;
    virtual bool HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) { return false; }
};

} // namespace ui_provider
