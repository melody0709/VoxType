#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "settings_tab_base.h"
#include "provider_baidu.h"
#include "provider_volcengine.h"
#include "provider_qwen.h"
#include "provider_mimo.h"
#include "provider_doubao.h"
#include "provider_qwen_free.h"
#include "provider_mai.h"

#include <memory>

namespace ui_tab {

class TabCloudAsr : public ISettingsTab {
public:
    TabCloudAsr();
    ~TabCloudAsr() override = default;

    void CreateControls(HWND parent) override;
    void DestroyControls() override;
    void Show(bool visible) override;
    void LoadControls(HWND parent, const Config& cfg) override;
    void SaveControls(HWND parent, Config& cfg) override;
    bool HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) override;

    void ShowCloudSubPage(HWND parent, int providerIdx);
    bool HandleMessage(HWND parent, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void AddCloudAsrControl(HWND hwnd) { if (hwnd) m_controls.push_back(hwnd); }

    std::vector<HWND> m_controls;
    HWND m_hintControl = nullptr;
    int m_cloudProviderIdx = 0;

    ui_provider::ProviderVolcengine m_volc;
    ui_provider::ProviderBaidu m_baidu;
    ui_provider::ProviderQwen m_qwen;
    ui_provider::ProviderMimo m_mimo;
    ui_provider::ProviderDoubao m_doubao;
    ui_provider::ProviderQwenFree m_qwenFree;
    ui_provider::ProviderMai m_mai;
};

} // namespace ui_tab
