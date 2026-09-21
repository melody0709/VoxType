#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "settings_tab_base.h"
#include "provider_local.h"
#include "provider_baidu.h"
#include "provider_volcengine.h"
#include "provider_qwen.h"
#include "provider_mimo.h"
#include "provider_doubao.h"
#include "provider_qwen_free.h"
#include "provider_mai.h"

#include <array>

namespace ui_tab {

// Tab 2: the single place that owns the ASR Backend / Fallback selectors (Row 0)
// and swaps in the matching provider panel below them. Row 0 is created here once
// and never by a provider panel, so every GetDlgItem(IDC_ASR_BACKEND) resolves to
// exactly one control.
class TabSpeechEngine : public ISettingsTab {
public:
    TabSpeechEngine();
    ~TabSpeechEngine() override = default;

    void CreateControls(HWND parent) override;
    void DestroyControls() override;
    void Show(bool visible) override;
    void LoadControls(HWND parent, const Config& cfg) override;
    void SaveControls(HWND parent, Config& cfg) override;
    bool HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) override;
    bool HandleMessage(HWND parent, UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    void AddEngineControl(HWND hwnd) { if (hwnd) m_controls.push_back(hwnd); }
    void ShowBackendPanel(HWND parent, const std::wstring& backendId);

    std::vector<HWND> m_controls;
    HWND m_hintControl = nullptr;

    ui_provider::LocalProviderPanel m_local;
    ui_provider::ProviderVolcengine m_volc;
    ui_provider::ProviderBaidu m_baidu;
    ui_provider::ProviderQwen m_qwen;
    ui_provider::ProviderMimo m_mimo;
    ui_provider::ProviderDoubao m_doubao;
    ui_provider::ProviderQwenFree m_qwenFree;
    ui_provider::ProviderMai m_mai;
    std::array<ui_provider::ICloudProviderPanel*, 8> m_panels;
};

} // namespace ui_tab
