#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_base.h"

namespace ui_provider {

class ProviderVolcengine : public ICloudProviderPanel {
public:
    void CreateControls(HWND parent) override;
    void DestroyControls() override;
    void Show(bool visible) override;
    void LoadControls(HWND parent, const Config& cfg) override;
    void SaveControls(HWND parent, Config& cfg) override;
    bool HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) override;

private:
    void AddVolcControl(HWND hwnd) { if (hwnd) m_controls.push_back(hwnd); }

    std::vector<HWND> m_controls;
    bool m_keyVisible = false;
};

} // namespace ui_provider
