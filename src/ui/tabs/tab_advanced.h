#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "settings_tab_base.h"

namespace ui_tab {

class TabAdvanced : public ISettingsTab {
public:
    void CreateControls(HWND parent) override;
    void DestroyControls() override;
    void Show(bool visible) override;
    void LoadControls(HWND parent, const Config& cfg) override;
    void SaveControls(HWND parent, Config& cfg) override;
    bool HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) override;

    void UpdateVadSubGroup(HWND parent);
    void ShowVadSubGroup(int vadModelIdx);

private:
    void AddAdvancedControl(HWND hwnd) { if (hwnd) m_controls.push_back(hwnd); }
    void AddVadFireredControl(HWND hwnd) { if (hwnd) m_vadFireredControls.push_back(hwnd); }

    std::vector<HWND> m_controls;
    std::vector<HWND> m_vadFireredControls;
};

void OpenDiagnosticAudioFolder(HWND hwnd);
void DeleteDiagnosticAudioFiles(HWND hwnd);

} // namespace ui_tab
