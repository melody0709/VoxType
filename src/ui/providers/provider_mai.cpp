#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_mai.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "ui_utils.h"
#include "asr_probe_service.h"

#include <windowsx.h>
#include <winhttp.h>

namespace ui_provider {

namespace {

constexpr int kMaiCloudProviderIndex = 6;

struct MaiLanguageOption {
    const wchar_t* label;
    const wchar_t* code;
};

constexpr MaiLanguageOption kMaiLanguages[] = {
    {L"Auto", L"auto"},
    {L"Chinese (zh)", L"zh"},
    {L"English (en)", L"en"},
    {L"Japanese (ja)", L"ja"},
    {L"Korean (ko)", L"ko"},
    {L"Cantonese (yue)", L"yue"},
};

const wchar_t* MaiLanguageCodeFromIndex(int index) {
    constexpr int count = static_cast<int>(sizeof(kMaiLanguages) / sizeof(kMaiLanguages[0]));
    return index >= 0 && index < count ? kMaiLanguages[index].code : L"auto";
}

bool ValidateAzureEndpoint(const std::wstring& endpoint, std::wstring& error) {
    std::wstring url = Trim(endpoint);
    if (url.empty()) {
        error = L"Azure Endpoint cannot be empty.";
        return false;
    }
    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        error = L"invalid Azure Endpoint";
        return false;
    }
    std::wstring scheme(parts.lpszScheme, parts.dwSchemeLength);
    if (scheme != L"https" && scheme != L"http") {
        error = L"Azure Endpoint must start with https:// or http://";
        return false;
    }
    if (parts.dwHostNameLength == 0) {
        error = L"Azure Endpoint host cannot be empty.";
        return false;
    }
    return true;
}

static ProviderMai* s_maiInstance = nullptr;

} // namespace

void ShowMaiApiSubPage(HWND hwnd) {
    if (s_maiInstance) {
        s_maiInstance->ShowSubPage(hwnd);
    }
}

void ProviderMai::ShowSubPage(HWND hwnd) {
    HWND apiProviderCombo = GetDlgItem(hwnd, IDC_MAI_API_PROVIDER);
    const bool visible = apiProviderCombo && IsWindowVisible(apiProviderCombo);
    const bool azure = ComboBox_GetCurSel(apiProviderCombo) == 1;
    for (HWND control : m_openRouterControls) {
        ShowWindow(control, visible && !azure ? SW_SHOW : SW_HIDE);
    }
    for (HWND control : m_azureControls) {
        ShowWindow(control, visible && azure ? SW_SHOW : SW_HIDE);
    }
    HWND hint = GetDlgItem(hwnd, IDC_MAI_HINT);
    if (hint) {
        SetWindowTextW(hint, azure
            ? L"Azure Fast Transcription sends the complete WAV after key release. Final text only; no partial."
            : L"OpenRouter sends the complete recording after key release. Final text only; no partial.");
    }
}

void ProviderMai::UpdateSubPage(HWND parent) {
    ShowSubPage(parent);
}

void ProviderMai::CreateControls(HWND hwnd) {
    s_maiInstance = this;
    m_controls.clear();
    m_openRouterControls.clear();
    m_azureControls.clear();
    HWND parent = hwnd;
    HWND control = CreateLabel(hwnd, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(1)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"API");
    AddMaiControl(control);
    AddMaiControl(CreateCombo(hwnd, IDC_MAI_API_PROVIDER,
                              S(UiStyle::InputLeft), S(UiStyle::RowInputY(1)),
                              S(UiStyle::ComboW), S(UiStyle::ComboH)));

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(2)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"API Key");
    AddMaiOpenRouterControl(control);
    HWND maiOpenRouterApiKey = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
        S(UiStyle::InputLeft), S(UiStyle::RowInputY(2)), S(330), S(UiStyle::EditH),
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAI_OPENROUTER_API_KEY)),
        GetParentInstance(parent), nullptr);
    ApplyUiFont(maiOpenRouterApiKey);
    AddMaiOpenRouterControl(maiOpenRouterApiKey);
    AddMaiOpenRouterControl(CreateButton(
        parent, IDC_MAI_SHOW_OPENROUTER_KEY, S(UiStyle::SmallBtnX),
        S(UiStyle::RowInputY(2)) - S(1), S(UiStyle::SmallBtnW), S(UiStyle::BtnH),
        L"Show"));

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(2)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Endpoint");
    AddMaiAzureControl(control);
    HWND maiAzureEndpoint = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        S(UiStyle::InputLeft), S(UiStyle::RowInputY(2)), S(UiStyle::InputWFull), S(UiStyle::EditH),
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAI_AZURE_ENDPOINT)),
        GetParentInstance(parent), nullptr);
    ApplyUiFont(maiAzureEndpoint);
    AddMaiAzureControl(maiAzureEndpoint);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(3)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"API Key");
    AddMaiAzureControl(control);
    HWND maiAzureApiKey = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
        S(UiStyle::InputLeft), S(UiStyle::RowInputY(3)), S(330), S(UiStyle::EditH),
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAI_AZURE_API_KEY)),
        GetParentInstance(parent), nullptr);
    ApplyUiFont(maiAzureApiKey);
    AddMaiAzureControl(maiAzureApiKey);
    AddMaiAzureControl(CreateButton(
        parent, IDC_MAI_SHOW_AZURE_KEY, S(UiStyle::SmallBtnX),
        S(UiStyle::RowInputY(3)) - S(1), S(UiStyle::SmallBtnW), S(UiStyle::BtnH),
        L"Show"));

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(4)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Language");
    AddMaiControl(control);
    AddMaiControl(CreateCombo(parent, IDC_MAI_LANGUAGE,
                              S(UiStyle::InputLeft), S(UiStyle::RowInputY(4)),
                              S(UiStyle::ComboW), S(UiStyle::ComboH)));

    HWND maiHint = CreateHint(parent, S(UiStyle::ContentLeft), S(UiStyle::RowInputY(5)),
                              S(UiStyle::InputWFull), S(UiStyle::QwenHint2LineH), L"");
    AddMaiControl(maiHint);
    SetWindowLongPtrW(maiHint, GWLP_ID, IDC_MAI_HINT);

    AddMaiControl(CreateButton(
        parent, IDC_MAI_TEST, S(500), S(UiStyle::RowInputY(0)),
        S(UiStyle::ActionBtnW), S(UiStyle::ActionBtnH), L"Test Connection"));
}

void ProviderMai::DestroyControls() {
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
    m_openRouterControls.clear();
    m_azureControls.clear();
}

void ProviderMai::Show(bool visible) {
    for (HWND c : m_controls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
    if (visible) {
        ShowSubPage(m_controls.empty() ? nullptr : GetParent(m_controls[0]));
    } else {
        for (HWND c : m_openRouterControls) ShowWindow(c, SW_HIDE);
        for (HWND c : m_azureControls) ShowWindow(c, SW_HIDE);
        HWND parent = m_controls.empty() ? nullptr : GetParent(m_controls[0]);
        if (parent) {
            m_openRouterKeyVisible = false;
            HWND showMaiOpenRouter = GetDlgItem(parent, IDC_MAI_SHOW_OPENROUTER_KEY);
            if (showMaiOpenRouter) SetWindowTextW(showMaiOpenRouter, L"Show");
            HWND maiOpenRouterKey = GetDlgItem(parent, IDC_MAI_OPENROUTER_API_KEY);
            if (maiOpenRouterKey) {
                SendMessageW(maiOpenRouterKey, EM_SETPASSWORDCHAR, L'\u25CF', 0);
                InvalidateRect(maiOpenRouterKey, nullptr, TRUE);
            }
            m_azureKeyVisible = false;
            HWND showMaiAzure = GetDlgItem(parent, IDC_MAI_SHOW_AZURE_KEY);
            if (showMaiAzure) SetWindowTextW(showMaiAzure, L"Show");
            HWND maiAzureKey = GetDlgItem(parent, IDC_MAI_AZURE_API_KEY);
            if (maiAzureKey) {
                SendMessageW(maiAzureKey, EM_SETPASSWORDCHAR, L'\u25CF', 0);
                InvalidateRect(maiAzureKey, nullptr, TRUE);
            }
        }
    }
}

void ProviderMai::LoadControls(HWND parent, const Config& cfg) {
    m_openRouterKeyVisible = false;
    HWND showMaiOpenRouter = GetDlgItem(parent, IDC_MAI_SHOW_OPENROUTER_KEY);
    if (showMaiOpenRouter) SetWindowTextW(showMaiOpenRouter, L"Show");
    HWND maiOpenRouterKey = GetDlgItem(parent, IDC_MAI_OPENROUTER_API_KEY);
    if (maiOpenRouterKey) SendMessageW(maiOpenRouterKey, EM_SETPASSWORDCHAR, L'\u25CF', 0);

    m_azureKeyVisible = false;
    HWND showMaiAzure = GetDlgItem(parent, IDC_MAI_SHOW_AZURE_KEY);
    if (showMaiAzure) SetWindowTextW(showMaiAzure, L"Show");
    HWND maiAzureKey = GetDlgItem(parent, IDC_MAI_AZURE_API_KEY);
    if (maiAzureKey) SendMessageW(maiAzureKey, EM_SETPASSWORDCHAR, L'\u25CF', 0);

    SetWindowTextW(GetDlgItem(parent, IDC_MAI_OPENROUTER_API_KEY), cfg.maiOpenRouterApiKey.c_str());
    SetWindowTextW(GetDlgItem(parent, IDC_MAI_AZURE_ENDPOINT), cfg.maiAzureEndpoint.c_str());
    SetWindowTextW(GetDlgItem(parent, IDC_MAI_AZURE_API_KEY), cfg.maiAzureApiKey.c_str());

    HWND maiApiProvider = GetDlgItem(parent, IDC_MAI_API_PROVIDER);
    if (maiApiProvider) {
        ComboBox_ResetContent(maiApiProvider);
        ComboBox_AddString(maiApiProvider, L"OpenRouter");
        ComboBox_AddString(maiApiProvider, L"Azure Fast Transcription");
        ComboBox_SetCurSel(maiApiProvider, cfg.maiApiProvider == L"azure" ? 1 : 0);
    }

    HWND maiLanguage = GetDlgItem(parent, IDC_MAI_LANGUAGE);
    if (maiLanguage) {
        ComboBox_ResetContent(maiLanguage);
        for (const auto& opt : kMaiLanguages) {
            ComboBox_AddString(maiLanguage, opt.label);
        }
        int sel = 0;
        for (size_t i = 0; i < sizeof(kMaiLanguages) / sizeof(kMaiLanguages[0]); ++i) {
            if (cfg.maiLanguage == kMaiLanguages[i].code) {
                sel = static_cast<int>(i);
                break;
            }
        }
        ComboBox_SetCurSel(maiLanguage, sel);
    }

    ShowMaiApiSubPage(parent);
}

void ProviderMai::SaveControls(HWND parent, Config& cfg) {
    cfg.maiApiProvider =
        ComboBox_GetCurSel(GetDlgItem(parent, IDC_MAI_API_PROVIDER)) == 1
            ? L"azure"
            : L"openrouter";
    cfg.maiOpenRouterApiKey = QwenControlText(parent, IDC_MAI_OPENROUTER_API_KEY, 1024);
    cfg.maiAzureEndpoint = Trim(QwenControlText(parent, IDC_MAI_AZURE_ENDPOINT, 2048));
    cfg.maiAzureApiKey = QwenControlText(parent, IDC_MAI_AZURE_API_KEY, 1024);
    cfg.maiLanguage = MaiLanguageCodeFromIndex(
        ComboBox_GetCurSel(GetDlgItem(parent, IDC_MAI_LANGUAGE)));
}

bool ProviderMai::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    switch (controlId) {
    case IDC_MAI_API_PROVIDER:
        if (notifyCode == CBN_SELCHANGE) {
            ShowMaiApiSubPage(parent);
            return true;
        }
        return false;
    case IDC_MAI_SHOW_OPENROUTER_KEY: {
        m_openRouterKeyVisible = !m_openRouterKeyVisible;
        HWND key = GetDlgItem(parent, IDC_MAI_OPENROUTER_API_KEY);
        if (key) {
            SendMessageW(key, EM_SETPASSWORDCHAR, m_openRouterKeyVisible ? 0 : L'\u25CF', 0);
            InvalidateRect(key, nullptr, TRUE);
        }
        HWND button = GetDlgItem(parent, IDC_MAI_SHOW_OPENROUTER_KEY);
        if (button) {
            SetWindowTextW(button, m_openRouterKeyVisible ? L"Hide" : L"Show");
        }
        return true;
    }
    case IDC_MAI_SHOW_AZURE_KEY: {
        m_azureKeyVisible = !m_azureKeyVisible;
        HWND key = GetDlgItem(parent, IDC_MAI_AZURE_API_KEY);
        if (key) {
            SendMessageW(key, EM_SETPASSWORDCHAR, m_azureKeyVisible ? 0 : L'\u25CF', 0);
            InvalidateRect(key, nullptr, TRUE);
        }
        HWND button = GetDlgItem(parent, IDC_MAI_SHOW_AZURE_KEY);
        if (button) {
            SetWindowTextW(button, m_azureKeyVisible ? L"Hide" : L"Show");
        }
        return true;
    }
    case IDC_MAI_TEST: {
        const int providerSel = ComboBox_GetCurSel(GetDlgItem(parent, IDC_MAI_API_PROVIDER));
        const bool azure = providerSel == 1;
        Config snap = g_config;
        snap.maiApiProvider = azure ? L"azure" : L"openrouter";
        snap.maiOpenRouterApiKey = QwenControlText(parent, IDC_MAI_OPENROUTER_API_KEY, 1024);
        snap.maiAzureEndpoint = Trim(QwenControlText(parent, IDC_MAI_AZURE_ENDPOINT, 2048));
        snap.maiAzureApiKey = QwenControlText(parent, IDC_MAI_AZURE_API_KEY, 1024);
        snap.maiLanguage = MaiLanguageCodeFromIndex(
            ComboBox_GetCurSel(GetDlgItem(parent, IDC_MAI_LANGUAGE)));

        if (azure) {
            std::wstring error;
            if (!ValidateAzureEndpoint(snap.maiAzureEndpoint, error)) {
                SetStatus(parent, error);
                return true;
            }
        }
        SetStatus(parent,
            azure ? L"Testing MAI Azure Fast Transcription connection..."
                  : L"Testing MAI OpenRouter connection...");

        const uint64_t testGen = g_sharedTestGeneration.fetch_add(1) + 1;
        asr_probe::ProbeRequest req{ L"mai", snap };
        if (auto* svc = asr_probe::GetProbeService()) {
            svc->ProbeAsync(req, [parent, testGen](const asr_probe::ProbeResult& result) {
                PostSharedTestResult(parent, testGen, result.ok, result.message);
            });
        }
        return true;
    }
    default:
        return false;
    }
}

} // namespace ui_provider
