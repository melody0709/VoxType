#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "settings_tab_base.h"

namespace ui_tab {

class TabGeneral : public ISettingsTab {
public:
    void CreateControls(HWND parent) override;
    void DestroyControls() override;
    void Show(bool visible) override;
    void LoadControls(HWND parent, const Config& cfg) override;
    void SaveControls(HWND parent, Config& cfg) override;
    bool HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) override;

    bool SaveStartupRegistration(HWND parent);

private:
    void AddGeneralControl(HWND hwnd) { if (hwnd) m_controls.push_back(hwnd); }
    std::vector<HWND> m_controls;
};

void RefreshStartupRegistrationControl(HWND hwnd, bool reportError);
bool SaveStartupRegistrationControl(HWND hwnd);
void OpenDiagnosticAudioFolder(HWND hwnd);
void DeleteDiagnosticAudioFiles(HWND hwnd);

} // namespace ui_tab
