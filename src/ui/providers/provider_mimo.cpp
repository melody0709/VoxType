#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_mimo.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "ui_strings.h"
#include "asr_probe_service.h"
#include "utils.h"

#include <windowsx.h>

namespace ui_provider {

namespace {
constexpr wchar_t kMimoDefaultBaseUrl[] = L"https://api.xiaomimimo.com/v1";
constexpr wchar_t kMimoDefaultModel[] = L"mimo-v2.5-asr";

// 预设 URL 的标签走 ui_strings 双语表（运行时取值），这里只保留匹配用的 URL。
constexpr const wchar_t* kMimoPresetUrls[] = {
    L"https://api.xiaomimimo.com/v1",
    L"https://token-plan-cn.xiaomimimo.com/v1",
    L"https://token-plan-ams.xiaomimimo.com/v1",
    L"",
};
constexpr int kMimoUrlPresetCount = static_cast<int>(sizeof(kMimoPresetUrls) / sizeof(kMimoPresetUrls[0]));
constexpr int kCustomPresetIndex = kMimoUrlPresetCount - 1;

const wchar_t* MimoPresetLabel(const ui_strings::UiStrings& text, int index) {
    switch (index) {
    case 0: return text.mimoPresetDefault;
    case 1: return text.mimoPresetTokenCn;
    case 2: return text.mimoPresetTokenAms;
    default: return text.mimoPresetCustom;
    }
}

int MatchUrlPresetIndex(const std::wstring& url) {
    for (int i = 0; i < kCustomPresetIndex; ++i) {
        if (url == kMimoPresetUrls[i]) {
            return i;
        }
    }
    return kCustomPresetIndex;
}

} // namespace

void ProviderMimo::CreateControls(HWND parent) {
    m_extraControls.clear();
    const auto& text = ui_strings::UiText();
    ui_form::LayoutCursor cursor(UiStyle::RowInputY(1), UiStyle::RowHeight);

    m_binder.AddPasswordRow(parent, cursor, IDC_MIMO_API_KEY, IDC_MIMO_SHOW_KEY, text.mimoApiKey, &Config::mimoApiKey, 330);

    const int y = cursor.NextRowY();
    HWND lblUrl = CreateLabel(parent, S(UiStyle::ContentLeft), S(y + UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), text.mimoBaseUrl);
    m_extraControls.push_back(lblUrl);

    HWND comboPreset = CreateCombo(parent, IDC_MIMO_BASE_URL_PRESET, S(UiStyle::InputLeft), S(y), S(UiStyle::MimoPresetComboW), S(UiStyle::ComboH));
    for (int i = 0; i < kMimoUrlPresetCount; ++i) {
        ComboBox_AddString(comboPreset, MimoPresetLabel(text, i));
    }
    m_extraControls.push_back(comboPreset);

    HWND editUrl = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                   S(UiStyle::InputLeft + UiStyle::MimoPresetComboW + UiStyle::MimoUrlEditGap), S(y), S(UiStyle::MimoUrlEditW), S(UiStyle::EditH),
                                   parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MIMO_BASE_URL)),
                                   GetParentInstance(parent), nullptr);
    ApplyUiFont(editUrl);
    m_extraControls.push_back(editUrl);

    m_binder.AddEditRow(parent, cursor, IDC_MIMO_MODEL, text.mimoModel, &Config::mimoModel, 330);

    const std::pair<std::wstring, std::wstring> langOpts[] = {
        {L"auto", text.mimoLangAuto},
        {L"zh", text.mimoLangChinese},
        {L"en", text.mimoLangEnglish},
        {L"ja", text.mimoLangJapanese},
        {L"ko", text.mimoLangKorean},
        {L"yue", text.mimoLangCantonese},
    };
    m_binder.AddComboRow(parent, cursor, IDC_MIMO_LANGUAGE, text.mimoLanguage, &Config::mimoLanguage, langOpts);

    HWND btnTest = CreateButton(parent, IDC_MIMO_TEST, S(500), S(UiStyle::RowInputY(5)), S(UiStyle::ActionBtnW), S(UiStyle::ActionBtnH), text.mimoTestConnection);
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
            SetWindowTextW(GetDlgItem(parent, IDC_MIMO_BASE_URL), kMimoPresetUrls[sel]);
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
        SetStatus(parent, ui_strings::UiText().mimoTesting);
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
