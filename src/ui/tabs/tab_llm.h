#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "settings_tab_base.h"

namespace ui_tab {

class TabLlm : public ISettingsTab {
public:
    void CreateControls(HWND parent) override;
    void DestroyControls() override;
    void Show(bool visible) override;
    void LoadControls(HWND parent, const Config& cfg) override;
    void SaveControls(HWND parent, Config& cfg) override;
    bool HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) override;

    void RefreshProviderList(HWND parent);
    void StoreVisibleProvider(HWND parent, Config& cfg = g_config);

private:
    void AddLlmControl(HWND hwnd) { if (hwnd) m_controls.push_back(hwnd); }

    std::vector<HWND> m_controls;
    bool m_keyVisible = false;
};

void RefreshProviderDropdown(HWND hwnd);
void TestLlmConnection(HWND hwnd);

} // namespace ui_tab
