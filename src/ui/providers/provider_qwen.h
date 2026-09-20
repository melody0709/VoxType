#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_base.h"
#include "qwen_settings_helper.h"

namespace ui_provider {

void ShowQwenSubControls(HWND hwnd);

class ProviderQwen : public ICloudProviderPanel {
public:
    ~ProviderQwen() override { m_languageHintsHint = nullptr; }
    void CreateControls(HWND parent) override;
    void DestroyControls() override;
    void Show(bool visible) override;
    void LoadControls(HWND parent, const Config& cfg) override;
    void SaveControls(HWND parent, Config& cfg) override;
    bool HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) override;
    void UpdateSubControls(HWND parent);
    void ShowSubControls(HWND parent);

private:
    void AddQwenControl(HWND hwnd) { if (hwnd) m_controls.push_back(hwnd); }
    void AddQwenAudio3Control(HWND hwnd) { if (hwnd) m_audio3Controls.push_back(hwnd); }
    void AddQwenAudioStreamingOnlyControl(HWND hwnd) { if (hwnd) m_streamingOnlyControls.push_back(hwnd); }

    std::vector<HWND> m_controls;
    std::vector<HWND> m_audio3Controls;
    std::vector<HWND> m_streamingOnlyControls;
    bool m_keyVisible = false;
    QwenProfileState m_profileState;
    HWND m_languageHintsHint = nullptr;
};

} // namespace ui_provider
