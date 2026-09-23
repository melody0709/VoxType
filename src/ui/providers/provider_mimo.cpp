#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_mimo.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "asr_probe_service.h"
#include "utils.h"

#include <windowsx.h>

namespace ui_provider {

namespace {
constexpr wchar_t kMimoDefaultBaseUrl[] = L"https://api.xiaomimimo.com/v1";
constexpr wchar_t kMimoDefaultModel[] = L"mimo-v2.5-asr";

struct MimoUrlPreset {
    const wchar_t* label;
    const wchar_t* url;
};

constexpr MimoUrlPreset kMimoUrlPresets[] = {
    {L"Default API", L"https://api.xiaomimimo.com/v1"},
    {L"Token Plan (CN)", L"https://token-plan-cn.xiaomimimo.com/v1"},
    {L"Token Plan (AMS)", L"https://token-plan-ams.xiaomimimo.com/v1"},
    {L"Custom", L""},
};
constexpr int kMimoUrlPresetCount = static_cast<int>(sizeof(kMimoUrlPresets) / sizeof(kMimoUrlPresets[0]));
constexpr int kCustomPresetIndex = kMimoUrlPresetCount - 1;

int MatchUrlPresetIndex(const std::wstring& url) {
    for (int i = 0; i < kCustomPresetIndex; ++i) {
        if (url == kMimoUrlPresets[i].url) {
            return i;
        }
    }
    return kCustomPresetIndex;
}

} // namespace

void ProviderMimo::CreateControls(HWND parent) {
    m_extraControls.clear();
    ui_form::LayoutCursor cursor(UiStyle::RowInputY(1), UiStyle::RowHeight);

    m_binder.AddPasswordRow(parent, cursor, IDC_MIMO_API_KEY, IDC_MIMO_SHOW_KEY, L"API Key", &Config::mimoApiKey, 330);

    const int y = cursor.NextRowY();
    HWND lblUrl = CreateLabel(parent, S(UiStyle::ContentLeft), S(y + UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Base URL");
    m_extraControls.push_back(lblUrl);

    HWND comboPreset = CreateCombo(parent, IDC_MIMO_BASE_URL_PRESET, S(UiStyle::InputLeft), S(y), S(UiStyle::MimoPresetComboW), S(UiStyle::ComboH));
    for (int i = 0; i < kMimoUrlPresetCount; ++i) {
        ComboBox_AddString(comboPreset, kMimoUrlPresets[i].label);
    }
    m_extraControls.push_back(comboPreset);

    HWND editUrl = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                   S(UiStyle::InputLeft + UiStyle::MimoPresetComboW + UiStyle::MimoUrlEditGap), S(y), S(UiStyle::MimoUrlEditW), S(UiStyle::EditH),
                                   parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MIMO_BASE_URL)),
                                   GetParentInstance(parent), nullptr);
    ApplyUiFont(editUrl);
    m_extraControls.push_back(editUrl);

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
    std::wstring url = cfg.mimoBaseUrl.empty() ? kMimoDefaultBaseUrl : cfg.mimoBaseUrl;
    SetWindowTextW(GetDlgItem(parent, IDC_MIMO_BASE_URL), url.c_str());
    int presetIdx = MatchUrlPresetIndex(url);
    ComboBox_SetCurSel(GetDlgItem(parent, IDC_MIMO_BASE_URL_PRESET), presetIdx);
    if (cfg.mimoModel.empty()) SetWindowTextW(GetDlgItem(parent, IDC_MIMO_MODEL), kMimoDefaultModel);
}

void ProviderMimo::SaveControls(HWND parent, Config& cfg) {
    m_binder.SaveToConfig(parent, cfg);
    wchar_t buf[512] = {};
    GetWindowTextW(GetDlgItem(parent, IDC_MIMO_BASE_URL), buf, 512);
    std::wstring url = Trim(std::wstring(buf));
    if (url.empty()) url = kMimoDefaultBaseUrl;
    cfg.mimoBaseUrl = url;
    if (cfg.mimoModel.empty()) cfg.mimoModel = kMimoDefaultModel;
}

bool ProviderMimo::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    if (m_binder.HandlePasswordToggle(parent, controlId)) return true;

    if (controlId == IDC_MIMO_BASE_URL_PRESET && notifyCode == CBN_SELCHANGE) {
        int sel = ComboBox_GetCurSel(control);
        if (sel >= 0 && sel < kCustomPresetIndex) {
            SetWindowTextW(GetDlgItem(parent, IDC_MIMO_BASE_URL), kMimoUrlPresets[sel].url);
        }
        return true;
    }

    if (controlId == IDC_MIMO_BASE_URL && notifyCode == EN_CHANGE) {
        wchar_t buf[512] = {};
        GetWindowTextW(control, buf, 512);
        std::wstring text = Trim(std::wstring(buf));
        int presetIdx = MatchUrlPresetIndex(text);
        HWND combo = GetDlgItem(parent, IDC_MIMO_BASE_URL_PRESET);
        if (combo && ComboBox_GetCurSel(combo) != presetIdx) {
            ComboBox_SetCurSel(combo, presetIdx);
        }
        return true;
    }

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
