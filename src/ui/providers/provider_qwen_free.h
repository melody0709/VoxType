#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_base.h"

namespace ui_provider {

constexpr UINT kQwenFreeTestResultMessage = WM_APP + 5;
constexpr UINT kQwenFreeStatusResultMessage = WM_APP + 6;

class ProviderQwenFree : public ICloudProviderPanel {
public:
    const wchar_t* Id() const override { return L"qwen_free"; }
    void CreateControls(HWND parent) override;
    void DestroyControls() override;
    void Show(bool visible) override;
    void LoadControls(HWND parent, const Config& cfg) override;
    void SaveControls(HWND parent, Config& cfg) override;
    bool HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) override;

    bool HandleMessage(HWND parent, UINT msg, WPARAM wParam, LPARAM lParam);
    void RefreshStatus(HWND parent);

private:
    void AddQwenFreeControl(HWND hwnd) { if (hwnd) m_controls.push_back(hwnd); }

    std::vector<HWND> m_controls;
};

void RefreshQwenFreeStatus(HWND hwnd);
void CancelQwenFreeTests();

} // namespace ui_provider
