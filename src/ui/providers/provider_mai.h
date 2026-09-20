#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_base.h"

namespace ui_provider {

class ProviderMai : public ICloudProviderPanel {
public:
    void CreateControls(HWND parent) override;
    void DestroyControls() override;
    void Show(bool visible) override;
    void LoadControls(HWND parent, const Config& cfg) override;
    void SaveControls(HWND parent, Config& cfg) override;
    bool HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) override;

    void UpdateSubPage(HWND parent);
    void ShowSubPage(HWND parent);

private:
    void AddMaiControl(HWND hwnd) { if (hwnd) m_controls.push_back(hwnd); }
    void AddMaiOpenRouterControl(HWND hwnd) { if (hwnd) m_openRouterControls.push_back(hwnd); }
    void AddMaiAzureControl(HWND hwnd) { if (hwnd) m_azureControls.push_back(hwnd); }

    std::vector<HWND> m_controls;
    std::vector<HWND> m_openRouterControls;
    std::vector<HWND> m_azureControls;
    bool m_openRouterKeyVisible = false;
    bool m_azureKeyVisible = false;
};

void ShowMaiApiSubPage(HWND hwnd);

} // namespace ui_provider
