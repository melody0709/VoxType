#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tab_llm.h"
#include "settings_controls.h"
#include "settings_dialogs.h"
#include "settings.h"
#include "ui_types.h"
#include "config_store.h"
#include "path_service.h"
#include "llm_refine.h"

#include <windowsx.h>
#include <shellapi.h>
#include <thread>

namespace ui_tab {

namespace {

void DeleteProviderFromStore(const std::wstring& name) {
    std::string json = llm::WideToUtf8(g_config.llmProvidersJson);
    std::string key = llm::WideToUtf8(name);
    if (llm::RemoveJsonObjectMember(json, key)) {
        g_config.llmProvidersJson = llm::Utf8ToWide(json);
    }
}

} // namespace

void RefreshProviderDropdown(HWND hwnd) {
    HWND combo = GetDlgItem(hwnd, IDC_LLM_PROVIDER);
    const std::wstring current = g_config.llmProvider;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < llm::kProviderPresetCount; ++i) {
        ComboBox_AddString(combo, llm::kProviderPresets[i].name);
    }
    bool currentListed = FindPresetIndex(current) >= 0;
    std::string provJson = llm::WideToUtf8(g_config.llmProvidersJson);
    std::vector<std::string> providerNames;
    if (llm::GetJsonObjectMemberNames(provJson, providerNames)) {
        for (const std::string& name : providerNames) {
            const std::wstring wideName = llm::Utf8ToWide(name);
            bool isPreset = false;
            for (int i = 0; i < llm::kProviderPresetCount; ++i) {
                if (name == llm::WideToUtf8(llm::kProviderPresets[i].name)) { isPreset = true; break; }
            }
            if (!isPreset) {
                ComboBox_AddString(combo, wideName.c_str());
            }
            if (wideName == current) currentListed = true;
        }
    }
    if (!current.empty() && !currentListed) {
        ComboBox_AddString(combo, current.c_str());
    }
    int sel = 0;
    int count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; ++i) {
        wchar_t buf[128] = {};
        ComboBox_GetLBText(combo, i, buf);
        if (current == buf) { sel = i; break; }
    }
    ComboBox_SetCurSel(combo, sel);
    bool isPresetSel = FindPresetIndex(ComboText(combo)) >= 0;
    HWND delBtn = GetDlgItem(hwnd, IDC_LLM_PROVIDER_DEL);
    if (delBtn) EnableWindow(delBtn, !isPresetSel);
    HWND resetBtn = GetDlgItem(hwnd, IDC_LLM_EXTRA_RESET);
    if (resetBtn) EnableWindow(resetBtn, isPresetSel);
}

void TestLlmConnection(HWND hwnd) {
    wchar_t endpoint[512] = {};
    GetWindowTextW(GetDlgItem(hwnd, IDC_LLM_ENDPOINT), endpoint, 512);
    wchar_t apiKey[512] = {};
    GetWindowTextW(GetDlgItem(hwnd, IDC_LLM_KEY), apiKey, 512);
    wchar_t model[256] = {};
    GetWindowTextW(GetDlgItem(hwnd, IDC_LLM_MODEL), model, 256);
    wchar_t extraParams[1024] = {};
    GetWindowTextW(GetDlgItem(hwnd, IDC_LLM_EXTRA), extraParams, 1024);

    if (llm::Trim(endpoint).empty() || llm::Trim(apiKey).empty() || llm::Trim(model).empty()) {
        SetStatus(hwnd, L"Please fill in all LLM fields.");
        return;
    }

    SetStatus(hwnd, L"Testing connection...");
    llm::RequestConfig cfg;
    cfg.endpoint = endpoint;
    cfg.apiKey = apiKey;
    cfg.model = model;
    cfg.extraParams = extraParams;
    const uint64_t testGen = g_sharedTestGeneration.fetch_add(1) + 1;
    std::thread([hwnd, cfg, testGen]() {
        llm::TestResult result = llm::TestConnection(cfg);
        PostSharedTestResult(hwnd, testGen, result.ok, std::move(result.message));
    }).detach();
}

void TabLlm::StoreVisibleProvider(HWND hwnd, Config& cfg) {
    if (cfg.llmProvider.empty()) return;
    wchar_t endpoint[512] = {};
    wchar_t apiKey[512] = {};
    wchar_t model[256] = {};
    wchar_t extraParams[1024] = {};
    GetWindowTextW(GetDlgItem(hwnd, IDC_LLM_ENDPOINT), endpoint, 512);
    GetWindowTextW(GetDlgItem(hwnd, IDC_LLM_KEY), apiKey, 512);
    GetWindowTextW(GetDlgItem(hwnd, IDC_LLM_MODEL), model, 256);
    GetWindowTextW(GetDlgItem(hwnd, IDC_LLM_EXTRA), extraParams, 1024);
    cfg.llmEndpoint = endpoint;
    cfg.llmApiKey = apiKey;
    cfg.llmModel = model;
    cfg.llmExtraParams = extraParams;
    SaveCurrentProvider(cfg);
}

void TabLlm::RefreshProviderList(HWND parent) {
    RefreshProviderDropdown(parent);
}

void TabLlm::UpdateControlEnableState(HWND parent) {
    const bool masterEnabled = Button_GetCheck(GetDlgItem(parent, IDC_LLM_ENABLE)) == BST_CHECKED;

    const std::wstring prov = ComboText(GetDlgItem(parent, IDC_LLM_PROVIDER));
    const bool isPreset = FindPresetIndex(prov) >= 0;

    EnableWindow(GetDlgItem(parent, IDC_LLM_PROVIDER), masterEnabled);
    EnableWindow(GetDlgItem(parent, IDC_LLM_PROVIDER_ADD), masterEnabled);
    EnableWindow(GetDlgItem(parent, IDC_LLM_PROVIDER_DEL), masterEnabled && !isPreset);
    EnableWindow(GetDlgItem(parent, IDC_LLM_ENDPOINT), masterEnabled);
    EnableWindow(GetDlgItem(parent, IDC_LLM_KEY), masterEnabled);
    EnableWindow(GetDlgItem(parent, IDC_LLM_SHOW_KEY), masterEnabled);
    EnableWindow(GetDlgItem(parent, IDC_LLM_MODEL), masterEnabled);
    EnableWindow(GetDlgItem(parent, IDC_LLM_EXTRA), masterEnabled);
    EnableWindow(GetDlgItem(parent, IDC_LLM_EXTRA_RESET), masterEnabled && isPreset);
    EnableWindow(GetDlgItem(parent, IDC_LLM_PRESET_COMBO), masterEnabled);
    EnableWindow(GetDlgItem(parent, IDC_LLM_MANAGE_PROMPT), masterEnabled);
    EnableWindow(GetDlgItem(parent, IDC_LLM_TEST), masterEnabled);
    EnableWindow(GetDlgItem(parent, IDC_LLM_DEBUG), masterEnabled);
    EnableWindow(GetDlgItem(parent, IDC_LLM_OPEN_LOG), masterEnabled);
}

void TabLlm::CreateControls(HWND parent) {
    m_controls.clear();

    HWND enableCheck = CreateWindowW(
        L"BUTTON", L"Enable LLM Refinement",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        S(UiStyle::ContentLeft), S(UiStyle::RowInputY(0)),
        S(320), S(UiStyle::CheckH),
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LLM_ENABLE)),
        GetParentInstance(parent), nullptr);
    ApplyUiFont(enableCheck);
    AddLlmControl(enableCheck);

    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(1)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Provider");
    AddLlmControl(control);
    AddLlmControl(CreateCombo(parent, IDC_LLM_PROVIDER, S(UiStyle::InputLeft), S(UiStyle::RowInputY(1)), S(480), S(400)));
    AddLlmControl(CreateButton(parent, IDC_LLM_PROVIDER_ADD, S(UiStyle::SideBtnX), S(UiStyle::RowInputY(1)) - S(1), S(44), S(UiStyle::BtnH), L"+"));
    AddLlmControl(CreateButton(parent, IDC_LLM_PROVIDER_DEL, S(UiStyle::SideBtnX) + S(48), S(UiStyle::RowInputY(1)) - S(1), S(44), S(UiStyle::BtnH), L"\u2212"));

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(2)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"API Base URL");
    AddLlmControl(control);
    HWND llmEndpoint = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                       S(UiStyle::InputLeft), S(UiStyle::RowInputY(2)), S(UiStyle::InputWFull), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LLM_ENDPOINT)), GetParentInstance(parent), nullptr);
    ApplyUiFont(llmEndpoint);
    AddLlmControl(llmEndpoint);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(3)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"API Key");
    AddLlmControl(control);
    HWND llmKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                                  S(UiStyle::InputLeft), S(UiStyle::RowInputY(3)), S(UiStyle::InputW), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LLM_KEY)), GetParentInstance(parent), nullptr);
    ApplyUiFont(llmKey);
    AddLlmControl(llmKey);
    AddLlmControl(CreateButton(parent, IDC_LLM_SHOW_KEY, S(UiStyle::SideBtnX), S(UiStyle::RowInputY(3)) - S(1), S(UiStyle::SideBtnW), S(UiStyle::BtnH), L"Show"));

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(4)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Model");
    AddLlmControl(control);
    HWND llmModel = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                    S(UiStyle::InputLeft), S(UiStyle::RowInputY(4)), S(UiStyle::InputWFull), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LLM_MODEL)), GetParentInstance(parent), nullptr);
    ApplyUiFont(llmModel);
    AddLlmControl(llmModel);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(5)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Extra Params");
    AddLlmControl(control);
    HWND llmExtra = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                    S(UiStyle::InputLeft), S(UiStyle::RowInputY(5)), S(UiStyle::InputW), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LLM_EXTRA)), GetParentInstance(parent), nullptr);
    ApplyUiFont(llmExtra);
    AddLlmControl(llmExtra);
    AddLlmControl(CreateButton(parent, IDC_LLM_EXTRA_RESET, S(UiStyle::SideBtnX), S(UiStyle::RowInputY(5)) - S(1), S(UiStyle::SideBtnW), S(UiStyle::BtnH), L"Reset"));
    {
        HWND hint = CreateWindowW(L"STATIC",
            L"JSON object fields merged into the request body; outer braces are optional.",
            WS_CHILD | WS_VISIBLE, S(UiStyle::InputLeft), S(UiStyle::RowInputY(5)) + S(UiStyle::EditH) + S(2), S(UiStyle::InputW), S(20), parent, nullptr, GetParentInstance(parent), nullptr);
        ApplyUiFont(hint);
        MarkSettingsHint(hint);
        AddLlmControl(hint);
    }

    const int promptRowY = S(UiStyle::RowInputY(6)) + S(6);
    const int promptLabelY = S(UiStyle::RowLabelY(6)) + S(6);
    control = CreateLabel(parent, S(UiStyle::ContentLeft), promptLabelY, S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Prompt");
    AddLlmControl(control);
    AddLlmControl(CreateCombo(parent, IDC_LLM_PRESET_COMBO, S(UiStyle::InputLeft), promptRowY, S(320), S(200)));
    AddLlmControl(CreateButton(parent, IDC_LLM_MANAGE_PROMPT, S(UiStyle::InputLeft) + S(332), promptRowY - S(1), S(136), S(UiStyle::BtnH), L"Manage..."));

    const int actionRowY = S(UiStyle::RowInputY(7)) + S(6);
    AddLlmControl(CreateButton(parent, IDC_LLM_TEST, S(UiStyle::InputLeft), actionRowY, S(UiStyle::ActionBtnW), S(UiStyle::ActionBtnH), L"Test Connection"));
    HWND llmDebug = CreateWindowW(L"BUTTON", L"Log refine before/after", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                  S(348), actionRowY + S(6), S(210), S(UiStyle::CheckH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LLM_DEBUG)), GetParentInstance(parent), nullptr);
    ApplyUiFont(llmDebug);
    AddLlmControl(llmDebug);
    AddLlmControl(CreateButton(parent, IDC_LLM_OPEN_LOG, S(576), actionRowY, S(150), S(UiStyle::ActionBtnH), L"Open Log Folder"));
}

void TabLlm::DestroyControls() {
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
}

void TabLlm::Show(bool visible) {
    for (HWND c : m_controls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
    if (!visible) {
        HWND parent = m_controls.empty() ? nullptr : GetParent(m_controls[0]);
        if (parent) {
            m_keyVisible = false;
            HWND showKeyBtn = GetDlgItem(parent, IDC_LLM_SHOW_KEY);
            if (showKeyBtn) SetWindowTextW(showKeyBtn, L"Show");
            HWND keyEdit = GetDlgItem(parent, IDC_LLM_KEY);
            if (keyEdit) {
                SendMessageW(keyEdit, EM_SETPASSWORDCHAR, L'\u25CF', 0);
                InvalidateRect(keyEdit, nullptr, TRUE);
            }
        }
    }
}

void TabLlm::LoadControls(HWND parent, const Config& cfg) {
    Button_SetCheck(GetDlgItem(parent, IDC_LLM_ENABLE), cfg.enableLlm ? BST_CHECKED : BST_UNCHECKED);

    RefreshProviderDropdown(parent);

    SetWindowTextW(GetDlgItem(parent, IDC_LLM_ENDPOINT), cfg.llmEndpoint.c_str());
    SetWindowTextW(GetDlgItem(parent, IDC_LLM_KEY), cfg.llmApiKey.c_str());
    SetWindowTextW(GetDlgItem(parent, IDC_LLM_MODEL), cfg.llmModel.c_str());
    SetWindowTextW(GetDlgItem(parent, IDC_LLM_EXTRA), cfg.llmExtraParams.c_str());

    m_keyVisible = false;
    HWND showKeyBtn = GetDlgItem(parent, IDC_LLM_SHOW_KEY);
    if (showKeyBtn) SetWindowTextW(showKeyBtn, L"Show");
    HWND keyEdit = GetDlgItem(parent, IDC_LLM_KEY);
    if (keyEdit) SendMessageW(keyEdit, EM_SETPASSWORDCHAR, L'\u25CF', 0);

    m_currentPrompt = cfg.llmPrompt;
    if (m_currentPrompt.empty()) {
        m_currentPrompt = llm::kPromptPresets[0].prompt;
    }

    HWND presetCombo = GetDlgItem(parent, IDC_LLM_PRESET_COMBO);
    if (presetCombo) {
        ComboBox_ResetContent(presetCombo);
        for (int i = 0; i < llm::kPromptPresetCount; ++i) {
            ComboBox_AddString(presetCombo, llm::kPromptPresets[i].name);
        }
        ComboBox_AddString(presetCombo, L"Custom");

        int matchedPreset = -1;
        for (int i = 0; i < llm::kPromptPresetCount; ++i) {
            if (m_currentPrompt == llm::kPromptPresets[i].prompt) {
                matchedPreset = i;
                break;
            }
        }
        if (matchedPreset >= 0) {
            ComboBox_SetCurSel(presetCombo, matchedPreset);
            m_customPromptBackup.clear();
        } else {
            ComboBox_SetCurSel(presetCombo, llm::kPromptPresetCount);
            m_customPromptBackup = m_currentPrompt;
        }
    }

    Button_SetCheck(GetDlgItem(parent, IDC_LLM_DEBUG), cfg.enableLlmDebug ? BST_CHECKED : BST_UNCHECKED);

    UpdateControlEnableState(parent);
}

void TabLlm::SaveControls(HWND parent, Config& cfg) {
    cfg.enableLlm = (Button_GetCheck(GetDlgItem(parent, IDC_LLM_ENABLE)) == BST_CHECKED);
    StoreVisibleProvider(parent, cfg);
    cfg.enableLlmDebug = (Button_GetCheck(GetDlgItem(parent, IDC_LLM_DEBUG)) == BST_CHECKED);
    cfg.llmPrompt = m_currentPrompt;
    cfg.llmPromptPreset = llm::PromptPresetIdForText(m_currentPrompt);
    cfg.llmPromptPresetVersion = llm::kPromptPresetVersion;
}

bool TabLlm::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    switch (controlId) {
    case IDC_LLM_ENABLE:
        if (notifyCode == BN_CLICKED) {
            UpdateControlEnableState(parent);
            return true;
        }
        return false;
    case IDC_LLM_PROVIDER:
        if (notifyCode == CBN_SELCHANGE) {
            StoreVisibleProvider(parent, g_config);
            std::wstring prov = ComboText(GetDlgItem(parent, IDC_LLM_PROVIDER));
            if (!prov.empty()) {
                g_config.llmProvider = prov;
                int pi = FindPresetIndex(prov);
                if (pi >= 0) {
                    ApplyPreset(g_config, pi);
                } else {
                    g_config.llmEndpoint.clear();
                    g_config.llmApiKey.clear();
                    g_config.llmModel.clear();
                    g_config.llmExtraParams.clear();
                    LoadProviderFromStore(g_config, prov);
                }
                SetWindowTextW(GetDlgItem(parent, IDC_LLM_ENDPOINT), g_config.llmEndpoint.c_str());
                SetWindowTextW(GetDlgItem(parent, IDC_LLM_KEY), g_config.llmApiKey.c_str());
                SetWindowTextW(GetDlgItem(parent, IDC_LLM_MODEL), g_config.llmModel.c_str());
                SetWindowTextW(GetDlgItem(parent, IDC_LLM_EXTRA), g_config.llmExtraParams.c_str());
                UpdateControlEnableState(parent);
            }
            return true;
        }
        return false;
    case IDC_LLM_PRESET_COMBO:
        if (notifyCode == CBN_SELCHANGE) {
            HWND combo = GetDlgItem(parent, IDC_LLM_PRESET_COMBO);
            int sel = ComboBox_GetCurSel(combo);
            if (sel >= 0 && sel < llm::kPromptPresetCount) {
                if (m_customPromptBackup.empty()) {
                    m_customPromptBackup = m_currentPrompt;
                }
                m_currentPrompt = llm::kPromptPresets[sel].prompt;
            } else if (sel == llm::kPromptPresetCount) {
                if (m_customPromptBackup.empty()) {
                    m_customPromptBackup = m_currentPrompt;
                } else {
                    m_currentPrompt = m_customPromptBackup;
                }
            }
            return true;
        }
        return false;
    case IDC_LLM_MANAGE_PROMPT: {
        std::wstring prompt = m_currentPrompt;
        if (ShowPromptManageDialog(parent, prompt, &m_customPromptBackup)) {
            m_currentPrompt = prompt;
            HWND combo = GetDlgItem(parent, IDC_LLM_PRESET_COMBO);
            if (combo) {
                int matched = -1;
                for (int i = 0; i < llm::kPromptPresetCount; ++i) {
                    if (m_currentPrompt == llm::kPromptPresets[i].prompt) {
                        matched = i;
                        break;
                    }
                }
                if (matched >= 0) {
                    ComboBox_SetCurSel(combo, matched);
                } else {
                    ComboBox_SetCurSel(combo, llm::kPromptPresetCount);
                    m_customPromptBackup = m_currentPrompt;
                }
            }
        }
        return true;
    }
    case IDC_LLM_PROVIDER_ADD: {
        std::wstring name;
        if (ShowInputDialog(parent, L"Add Provider", name)) {
            bool exists = FindPresetIndex(name) >= 0;
            if (!exists) {
                std::string json = llm::WideToUtf8(g_config.llmProvidersJson);
                std::string key = llm::WideToUtf8(name);
                std::string stored;
                exists = llm::GetJsonObjectMemberRaw(json, key, stored);
            }
            if (exists) {
                SetStatus(parent, L"Provider name already exists.");
            } else {
                StoreVisibleProvider(parent);
                g_config.llmEndpoint.clear();
                g_config.llmApiKey.clear();
                g_config.llmModel.clear();
                g_config.llmExtraParams.clear();
                g_config.llmProvider = name;
                SaveCurrentProvider(g_config);
                RefreshProviderDropdown(parent);
                SetWindowTextW(GetDlgItem(parent, IDC_LLM_ENDPOINT), L"");
                SetWindowTextW(GetDlgItem(parent, IDC_LLM_KEY), L"");
                SetWindowTextW(GetDlgItem(parent, IDC_LLM_MODEL), L"");
                SetWindowTextW(GetDlgItem(parent, IDC_LLM_EXTRA), L"");
                UpdateControlEnableState(parent);
                SetStatus(parent, (L"Added provider: " + name).c_str());
            }
        }
        return true;
    }
    case IDC_LLM_PROVIDER_DEL: {
        std::wstring prov = ComboText(GetDlgItem(parent, IDC_LLM_PROVIDER));
        if (prov.empty() || FindPresetIndex(prov) >= 0) return true;
        DeleteProviderFromStore(prov);
        ApplyPreset(g_config, 0);
        RefreshProviderDropdown(parent);
        SetWindowTextW(GetDlgItem(parent, IDC_LLM_ENDPOINT), g_config.llmEndpoint.c_str());
        SetWindowTextW(GetDlgItem(parent, IDC_LLM_KEY), g_config.llmApiKey.c_str());
        SetWindowTextW(GetDlgItem(parent, IDC_LLM_MODEL), g_config.llmModel.c_str());
        SetWindowTextW(GetDlgItem(parent, IDC_LLM_EXTRA), g_config.llmExtraParams.c_str());
        UpdateControlEnableState(parent);
        SetStatus(parent, (L"Deleted provider: " + prov).c_str());
        return true;
    }
    case IDC_LLM_SHOW_KEY: {
        m_keyVisible = !m_keyVisible;
        HWND keyEdit = GetDlgItem(parent, IDC_LLM_KEY);
        if (keyEdit) {
            SendMessageW(keyEdit, EM_SETPASSWORDCHAR, m_keyVisible ? 0 : L'\u25CF', 0);
            InvalidateRect(keyEdit, nullptr, TRUE);
        }
        HWND btn = GetDlgItem(parent, IDC_LLM_SHOW_KEY);
        if (btn) SetWindowTextW(btn, m_keyVisible ? L"Hide" : L"Show");
        return true;
    }
    case IDC_LLM_TEST:
        TestLlmConnection(parent);
        return true;
    case IDC_LLM_EXTRA_RESET: {
        int pi = FindPresetIndex(g_config.llmProvider);
        if (pi >= 0) {
            SetWindowTextW(GetDlgItem(parent, IDC_LLM_EXTRA), llm::kProviderPresets[pi].extraParams);
        }
        return true;
    }
    case IDC_LLM_OPEN_LOG: {
        const std::wstring logDir = LogDir();
        HINSTANCE res = ShellExecuteW(parent, L"open", logDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(res) <= 32) {
            SetStatus(parent, L"Unable to open log folder.");
        } else {
            SetStatus(parent, L"Opened log folder.");
        }
        return true;
    }
    default:
        return false;
    }
}

} // namespace ui_tab
