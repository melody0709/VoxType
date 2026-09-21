#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_mimo.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "asr_probe_service.h"

#include <windowsx.h>

namespace ui_provider {

namespace {
constexpr wchar_t kMimoDefaultBaseUrl[] = L"https://token-plan-ams.xiaomimimo.com/v1";
constexpr wchar_t kMimoDefaultModel[] = L"mimo-v2.5-asr";
} // namespace

void ProviderMimo::CreateControls(HWND parent) {
    m_extraControls.clear();
    ui_form::LayoutCursor cursor(UiStyle::RowInputY(1), UiStyle::RowHeight);

    m_binder.AddPasswordRow(parent, cursor, IDC_MIMO_API_KEY, IDC_MIMO_SHOW_KEY, L"API Key", &Config::mimoApiKey, 330);
    m_binder.AddEditRow(parent, cursor, IDC_MIMO_BASE_URL, L"Base URL", &Config::mimoBaseUrl, 480);
    m_binder.AddEditRow(parent, cursor, IDC_MIMO_MODEL, L"Model", &Config::mimoModel, 330);

    const std::pair<std::wstring, std::wstring> langOpts[] = {
        {L"auto", L"Auto (auto)"},
        {L"zh", L"Chinese (zh)"},
        {L"en", L"English (en)"},
        {L"ja", L"Japanese (ja)"},
        {L"ko", L"Korean (ko)"},
        {L"yue", L"Cantonese (yue)"},
    };
    m_binder.AddComboRow(parent, cursor, IDC_MIMO_LANGUAGE, L"Language", &Config::mimoLanguage, langOpts);

    HWND btnTest = CreateButton(parent, IDC_MIMO_TEST, S(500), S(UiStyle::RowInputY(5)), S(UiStyle::ActionBtnW), S(UiStyle::ActionBtnH), L"Test Connection");
    m_extraControls.push_back(btnTest);
}

void ProviderMimo::DestroyControls() {
    m_binder.DestroyControls();
    for (HWND c : m_extraControls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_extraControls.clear();
}

void ProviderMimo::Show(bool visible) {
    m_binder.ShowAll(visible);
    for (HWND c : m_extraControls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
}

void ProviderMimo::LoadControls(HWND parent, const Config& cfg) {
    m_binder.LoadFromConfig(parent, cfg);
    if (cfg.mimoBaseUrl.empty()) SetWindowTextW(GetDlgItem(parent, IDC_MIMO_BASE_URL), kMimoDefaultBaseUrl);
    if (cfg.mimoModel.empty()) SetWindowTextW(GetDlgItem(parent, IDC_MIMO_MODEL), kMimoDefaultModel);
}

void ProviderMimo::SaveControls(HWND parent, Config& cfg) {
    m_binder.SaveToConfig(parent, cfg);
    if (cfg.mimoBaseUrl.empty()) cfg.mimoBaseUrl = kMimoDefaultBaseUrl;
    if (cfg.mimoModel.empty()) cfg.mimoModel = kMimoDefaultModel;
}

bool ProviderMimo::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    if (m_binder.HandlePasswordToggle(parent, controlId)) return true;

    if (controlId == IDC_MIMO_TEST) {
        Config snap = g_config;
        SaveControls(parent, snap);
        SetStatus(parent, L"Testing MiMo ASR connection...");
        const uint64_t testGen = g_sharedTestGeneration.fetch_add(1) + 1;
        asr_probe::ProbeRequest req{ L"mimo", snap };
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
