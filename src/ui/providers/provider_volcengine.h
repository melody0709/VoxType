#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_base.h"
#include "settings_dialogs.h"

namespace ui_provider {

class ProviderVolcengine : public ICloudProviderPanel {
public:
    const wchar_t* Id() const override { return L"volcengine"; }
    void CreateControls(HWND parent) override;
    void DestroyControls() override;
    void Show(bool visible) override;
    void LoadControls(HWND parent, const Config& cfg) override;
    void SaveControls(HWND parent, Config& cfg) override;
    bool HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) override;

private:
    void AddVolcControl(HWND hwnd) { if (hwnd) m_controls.push_back(hwnd); }
    std::wstring CurrentMode(HWND parent) const;
    std::wstring CurrentResourceId(HWND parent) const;
    std::wstring CurrentLanguage(HWND parent) const;
    void SyncAdvancedFromConfig(const Config& cfg);

    std::vector<HWND> m_controls;
    bool m_keyVisible = false;
    // Low-frequency tuning lives in the [Advanced...] dialog; the values are kept
    // here between dialog sessions and written back to Config on save.
    VolcAdvancedDialogData m_advanced;
};

} // namespace ui_provider
