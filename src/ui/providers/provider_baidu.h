#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_base.h"
#include "form_builder.h"

namespace ui_provider {

class ProviderBaidu : public ICloudProviderPanel {
public:
    void CreateControls(HWND parent) override;
    void DestroyControls() override;
    void Show(bool visible) override;
    void LoadControls(HWND parent, const Config& cfg) override;
    void SaveControls(HWND parent, Config& cfg) override;
    bool HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) override;

private:
    ui_form::FormBinder m_binder;
    std::vector<HWND> m_extraControls;
};

} // namespace ui_provider
