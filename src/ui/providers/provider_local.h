#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_base.h"

namespace ui_provider {

class LocalProviderPanel : public ICloudProviderPanel {
public:
    const wchar_t* Id() const override { return L"local"; }
    void CreateControls(HWND parent) override;
    void DestroyControls() override;
    void Show(bool visible) override;
    void LoadControls(HWND parent, const Config& cfg) override;
    void SaveControls(HWND parent, Config& cfg) override;
    bool HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) override;

    bool HandleMessage(HWND parent, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void AddLocalControl(HWND hwnd) { if (hwnd) m_controls.push_back(hwnd); }

    std::vector<HWND> m_controls;
};

} // namespace ui_provider
