#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_baidu.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "asr_probe_service.h"

#include <windowsx.h>

namespace ui_provider {

void ProviderBaidu::CreateControls(HWND parent) {
    m_extraControls.clear();
    ui_form::LayoutCursor cursor(UiStyle::RowInputY(1), UiStyle::RowHeight);

    m_binder.AddPasswordRow(parent, cursor, IDC_BAIDU_API_KEY, IDC_BAIDU_SHOW_API_KEY, L"API Key", &Config::baiduApiKey, 330);
    m_binder.AddPasswordRow(parent, cursor, IDC_BAIDU_SECRET_KEY, IDC_BAIDU_SHOW_KEY, L"Secret Key", &Config::baiduSecretKey, 330);

    const std::pair<int, std::wstring> pidOpts[] = {
        {1537, L"Mandarin (1537)"},
        {1737, L"English (1737)"},
        {1637, L"Cantonese (1637)"},
        {1837, L"Sichuanese (1837)"},
    };
    m_binder.AddComboRow(parent, cursor, IDC_BAIDU_DEV_PID, L"Language Model", &Config::baiduDevPid, pidOpts, UiStyle::ComboW);

    HWND btnTest = CreateButton(parent, IDC_BAIDU_TEST, S(500), S(UiStyle::RowInputY(4)), S(UiStyle::ActionBtnW), S(UiStyle::ActionBtnH), L"Test Connection");
    m_extraControls.push_back(btnTest);
}

void ProviderBaidu::DestroyControls() {
    m_binder.DestroyControls();
    for (HWND c : m_extraControls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_extraControls.clear();
}

void ProviderBaidu::Show(bool visible) {
    m_binder.ShowAll(visible);
    for (HWND c : m_extraControls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
}

void ProviderBaidu::LoadControls(HWND parent, const Config& cfg) {
    m_binder.LoadFromConfig(parent, cfg);
}

void ProviderBaidu::SaveControls(HWND parent, Config& cfg) {
    m_binder.SaveToConfig(parent, cfg);
}

bool ProviderBaidu::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    if (m_binder.HandlePasswordToggle(parent, controlId)) return true;

    if (controlId == IDC_BAIDU_TEST) {
        Config snap = g_config;
        SaveControls(parent, snap);
        SetStatus(parent, L"Testing Baidu ASR connection...");
        const uint64_t testGen = g_sharedTestGeneration.fetch_add(1) + 1;
        asr_probe::ProbeRequest req{ L"baidu", snap };
        if (auto* svc = asr_probe::GetProbeService()) {
            svc->ProbeAsync(req, [parent, testGen](const asr_probe::ProbeResult& result) {
                PostSharedTestResult(parent, testGen, result.ok, result.message);
            });
        }
        return true;
    }
    return false;
}

} // namespace ui_provider
