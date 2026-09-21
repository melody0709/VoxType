#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_volcengine.h"
#include "settings_controls.h"
#include "settings_dialogs.h"
#include "settings.h"
#include "ui_types.h"
#include "ui_utils.h"
#include "asr_probe_service.h"

#include <windowsx.h>

namespace ui_provider {

namespace {

struct VolcMapping {
    int comboIdx; const wchar_t* resourceId;
};
constexpr VolcMapping kVolcResources[] = {
    {0, L"volc.seedasr.sauc.duration"},
    {1, L"volc.seedasr.sauc.concurrent"},
    {2, L"volc.bigasr.sauc.duration"},
    {3, L"volc.bigasr.sauc.concurrent"},
};
constexpr const wchar_t* kVolcLanguages[] = {
    L"", L"en-US", L"ja-JP", L"ko-KR", L"fr-FR",
    L"de-DE", L"es-MX", L"pt-BR", L"id-ID",
};

bool ValidateVolcAdvancedData(VolcAdvancedDialogData& data, std::wstring& error) {
    if (_wtoi(data.endWindowSize.c_str()) <= 0) {
        error = L"end_window_size must be a positive number of milliseconds.";
        return false;
    }
    if (_wtoi(data.forceToSpeechTime.c_str()) < 0) {
        error = L"force_to_speech_time must be zero or a positive number of milliseconds.";
        return false;
    }
    if (data.enableContext) {
        const int history = _wtoi(data.contextHistory.c_str());
        if (history < 1 || history > 20) {
            error = L"History turns must be between 1 and 20.";
            return false;
        }
    }
    return true;
}

} // namespace

void ProviderVolcengine::CreateControls(HWND parent) {
    m_controls.clear();
    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::VolcKeyY) + S(UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"API Key (X-Api-Key)");
    AddVolcControl(control);
    HWND volcApiKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                                      S(UiStyle::InputLeft), S(UiStyle::VolcKeyY), S(UiStyle::VolcKeyEditW), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_API_KEY)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcApiKey);
    AddVolcControl(volcApiKey);
    AddVolcControl(CreateButton(parent, IDC_VOLC_SHOW_KEY, S(UiStyle::QwenShowBtnX), S(UiStyle::VolcKeyY), S(UiStyle::QwenShowBtnW), S(UiStyle::EditH), L"Show"));

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::VolcModelY) + S(UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Model");
    AddVolcControl(control);
    AddVolcControl(CreateCombo(parent, IDC_VOLC_RESOURCE, S(UiStyle::InputLeft), S(UiStyle::VolcModelY), S(UiStyle::VolcModelComboW), S(UiStyle::ComboH)));
    AddVolcControl(CreateButton(parent, IDC_VOLC_OPEN_LOG, S(UiStyle::VolcLogBtnX), S(UiStyle::VolcModelY), S(UiStyle::VolcLogBtnW), S(UiStyle::ActionBtnH), L"Open log"));

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::VolcModeY) + S(UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"ASR Mode");
    AddVolcControl(control);
    AddVolcControl(CreateCombo(parent, IDC_VOLC_MODE, S(UiStyle::InputLeft), S(UiStyle::VolcModeY), S(UiStyle::VolcModeComboW), S(UiStyle::ComboH)));

    control = CreateLabel(parent, S(UiStyle::VolcLanguageLabelX), S(UiStyle::VolcModeY) + S(UiStyle::LabelYOffset), S(UiStyle::VolcLanguageLabelW), S(UiStyle::LabelH), L"Language");
    AddVolcControl(control);
    AddVolcControl(CreateCombo(parent, IDC_VOLC_LANGUAGE, S(UiStyle::VolcLanguageComboX), S(UiStyle::VolcModeY), S(UiStyle::VolcLanguageComboW), S(UiStyle::ComboH)));

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::VolcContextY) + S(UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Context & Vocab");
    AddVolcControl(control);
    HWND volcReuseVocab = CreateWindowW(L"BUTTON", L"Reuse common vocabulary (vocabulary.json)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                        S(UiStyle::InputLeft), S(UiStyle::VolcContextY), S(UiStyle::VolcReuseVocabW), S(UiStyle::CheckH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_REUSE_VOCABULARY)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcReuseVocab);
    AddVolcControl(volcReuseVocab);
    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::VolcInputContextY) + S(UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Input context");
    AddVolcControl(control);
    HWND volcEnableInputContext = CreateWindowW(L"BUTTON", L"Use focused input field text as context", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                S(UiStyle::VolcInputContextX), S(UiStyle::VolcInputContextY), S(UiStyle::VolcInputContextW), S(UiStyle::CheckH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_ENABLE_INPUT_CONTEXT)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcEnableInputContext);
    AddVolcControl(volcEnableInputContext);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::VolcActionY) + S(UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Advanced");
    AddVolcControl(control);
    AddVolcControl(CreateButton(parent, IDC_VOLC_ADVANCED, S(UiStyle::InputLeft), S(UiStyle::VolcActionY), S(UiStyle::VolcAdvancedBtnW), S(UiStyle::ActionBtnH), L"Advanced..."));
    AddVolcControl(CreateButton(parent, IDC_VOLC_TEST, S(UiStyle::VolcTestBtnX), S(UiStyle::VolcActionY), S(UiStyle::VolcTestBtnW), S(UiStyle::ActionBtnH), L"Test Connection"));

    control = CreateHint(parent, S(UiStyle::InputLeft), S(UiStyle::VolcHintY), S(UiStyle::QwenHintW), S(UiStyle::QwenHintH),
                         L"Hotwords, correction tables, DDC, end-window, JSON.");
    AddVolcControl(control);
}

void ProviderVolcengine::DestroyControls() {
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
}

void ProviderVolcengine::Show(bool visible) {
    for (HWND c : m_controls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
    if (!visible) {
        HWND parent = m_controls.empty() ? nullptr : GetParent(m_controls[0]);
        if (parent) {
            m_keyVisible = false;
            HWND showVolcBtn = GetDlgItem(parent, IDC_VOLC_SHOW_KEY);
            if (showVolcBtn) SetWindowTextW(showVolcBtn, L"Show");
            HWND volcKeyEdit = GetDlgItem(parent, IDC_VOLC_API_KEY);
            if (volcKeyEdit) {
                SendMessageW(volcKeyEdit, EM_SETPASSWORDCHAR, L'\u25CF', 0);
                InvalidateRect(volcKeyEdit, nullptr, TRUE);
            }
        }
    }
}

std::wstring ProviderVolcengine::CurrentMode(HWND parent) const {
    const int modeIdx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_VOLC_MODE));
    if (modeIdx == 1) return L"bigmodel_async";
    if (modeIdx == 2) return L"bigmodel";
    return L"bigmodel_nostream";
}

std::wstring ProviderVolcengine::CurrentResourceId(HWND parent) const {
    const int resIdx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_VOLC_RESOURCE));
    if (resIdx >= 0 && resIdx < 4) return kVolcResources[resIdx].resourceId;
    return kVolcResources[0].resourceId;
}

std::wstring ProviderVolcengine::CurrentLanguage(HWND parent) const {
    const int langIdx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_VOLC_LANGUAGE));
    if (langIdx >= 0 && langIdx < 9) return kVolcLanguages[langIdx];
    return L"";
}

void ProviderVolcengine::SyncAdvancedFromConfig(const Config& cfg) {
    m_advanced.hotwordsId = cfg.volcHotwordsId;
    m_advanced.hotwordsName = cfg.volcHotwordsName;
    m_advanced.correctTableId = cfg.volcCorrectTableId;
    m_advanced.correctTableName = cfg.volcCorrectTableName;
    m_advanced.enableContext = cfg.volcEnableContext;
    m_advanced.contextHistory = std::to_wstring(cfg.volcContextHistory);
    m_advanced.endWindowSize = std::to_wstring(cfg.volcEndWindowSize);
    m_advanced.forceToSpeechTime = std::to_wstring(cfg.volcForceToSpeechTime);
    m_advanced.enableDdc = cfg.volcEnableDdc;
    m_advanced.enableNonstream = cfg.volcEnableNonstream;
    m_advanced.enableMusicFc = cfg.volcEnableMusicFc;
    m_advanced.enablePoiFc = cfg.volcEnablePoiFc;
    m_advanced.extraParams = cfg.volcExtraParams;
}

void ProviderVolcengine::LoadControls(HWND parent, const Config& cfg) {
    m_keyVisible = false;
    HWND showVolcBtn = GetDlgItem(parent, IDC_VOLC_SHOW_KEY);
    if (showVolcBtn) SetWindowTextW(showVolcBtn, L"Show");
    HWND volcKeyEdit = GetDlgItem(parent, IDC_VOLC_API_KEY);
    if (volcKeyEdit) SendMessageW(volcKeyEdit, EM_SETPASSWORDCHAR, L'\u25CF', 0);

    SetWindowTextW(GetDlgItem(parent, IDC_VOLC_API_KEY), cfg.volcApiKey.c_str());

    HWND volcModeCombo = GetDlgItem(parent, IDC_VOLC_MODE);
    if (volcModeCombo) {
        ComboBox_ResetContent(volcModeCombo);
        ComboBox_AddString(volcModeCombo, L"bigmodel_nostream");
        ComboBox_AddString(volcModeCombo, L"bigmodel_async");
        ComboBox_AddString(volcModeCombo, L"bigmodel");
        int modeIdx = 0;
        if (cfg.volcMode == L"bigmodel_async") modeIdx = 1;
        else if (cfg.volcMode == L"bigmodel") modeIdx = 2;
        ComboBox_SetCurSel(volcModeCombo, modeIdx);
    }

    HWND volcResCombo = GetDlgItem(parent, IDC_VOLC_RESOURCE);
    if (volcResCombo) {
        ComboBox_ResetContent(volcResCombo);
        ComboBox_AddString(volcResCombo, L"Seed-ASR 2.0 (duration)");
        ComboBox_AddString(volcResCombo, L"Seed-ASR 2.0 (concurrent)");
        ComboBox_AddString(volcResCombo, L"BigASR 1.0 (duration)");
        ComboBox_AddString(volcResCombo, L"BigASR 1.0 (concurrent)");
        int resIdx = 0;
        if (cfg.volcResourceId == L"volc.seedasr.sauc.concurrent") resIdx = 1;
        else if (cfg.volcResourceId == L"volc.bigasr.sauc.duration") resIdx = 2;
        else if (cfg.volcResourceId == L"volc.bigasr.sauc.concurrent") resIdx = 3;
        ComboBox_SetCurSel(volcResCombo, resIdx);
    }

    HWND volcLangCombo = GetDlgItem(parent, IDC_VOLC_LANGUAGE);
    if (volcLangCombo) {
        ComboBox_ResetContent(volcLangCombo);
        ComboBox_AddString(volcLangCombo, L"Auto (Chinese+English+Dialects)");
        ComboBox_AddString(volcLangCombo, L"English (en-US)");
        ComboBox_AddString(volcLangCombo, L"Japanese (ja-JP)");
        ComboBox_AddString(volcLangCombo, L"Korean (ko-KR)");
        ComboBox_AddString(volcLangCombo, L"French (fr-FR)");
        ComboBox_AddString(volcLangCombo, L"German (de-DE)");
        ComboBox_AddString(volcLangCombo, L"Spanish (es-MX)");
        ComboBox_AddString(volcLangCombo, L"Portuguese (pt-BR)");
        ComboBox_AddString(volcLangCombo, L"Indonesian (id-ID)");
        int langIdx = 0;
        if (cfg.volcLanguage == L"en-US") langIdx = 1;
        else if (cfg.volcLanguage == L"ja-JP") langIdx = 2;
        else if (cfg.volcLanguage == L"ko-KR") langIdx = 3;
        else if (cfg.volcLanguage == L"fr-FR") langIdx = 4;
        else if (cfg.volcLanguage == L"de-DE") langIdx = 5;
        else if (cfg.volcLanguage == L"es-MX") langIdx = 6;
        else if (cfg.volcLanguage == L"pt-BR") langIdx = 7;
        else if (cfg.volcLanguage == L"id-ID") langIdx = 8;
        ComboBox_SetCurSel(volcLangCombo, langIdx);
        int modeIdx = ComboBox_GetCurSel(volcModeCombo);
        EnableWindow(volcLangCombo, modeIdx == 0);
    }

    Button_SetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_INPUT_CONTEXT), cfg.volcEnableInputContext ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(parent, IDC_VOLC_REUSE_VOCABULARY), cfg.volcEnableReuseVocabulary ? BST_CHECKED : BST_UNCHECKED);

    SyncAdvancedFromConfig(cfg);
    m_advanced.mode = cfg.volcMode;
}

void ProviderVolcengine::SaveControls(HWND parent, Config& cfg) {
    cfg.volcApiKey = GetControlText(parent, IDC_VOLC_API_KEY, 256);
    cfg.volcMode = CurrentMode(parent);
    cfg.volcResourceId = CurrentResourceId(parent);
    cfg.volcLanguage = CurrentLanguage(parent);
    cfg.volcEnableInputContext = Button_GetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_INPUT_CONTEXT)) == BST_CHECKED;
    cfg.volcEnableReuseVocabulary = Button_GetCheck(GetDlgItem(parent, IDC_VOLC_REUSE_VOCABULARY)) == BST_CHECKED;

    m_advanced.mode = cfg.volcMode;
    cfg.volcHotwordsId = m_advanced.hotwordsId;
    cfg.volcHotwordsName = m_advanced.hotwordsName;
    cfg.volcCorrectTableId = m_advanced.correctTableId;
    cfg.volcCorrectTableName = m_advanced.correctTableName;
    cfg.volcEnableContext = m_advanced.enableContext;
    int parsed = _wtoi(m_advanced.contextHistory.c_str());
    cfg.volcContextHistory = (parsed >= 1 && parsed <= 20) ? parsed : 3;
    parsed = _wtoi(m_advanced.endWindowSize.c_str());
    cfg.volcEndWindowSize = parsed > 0 ? parsed : 800;
    parsed = _wtoi(m_advanced.forceToSpeechTime.c_str());
    cfg.volcForceToSpeechTime = parsed >= 1 ? parsed : 0;
    cfg.volcEnableDdc = m_advanced.enableDdc;
    cfg.volcEnableNonstream = m_advanced.enableNonstream;
    cfg.volcEnableMusicFc = m_advanced.enableMusicFc;
    cfg.volcEnablePoiFc = m_advanced.enablePoiFc;
    cfg.volcExtraParams = m_advanced.extraParams;
}

bool ProviderVolcengine::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    switch (controlId) {
    case IDC_VOLC_MODE:
        if (notifyCode == CBN_SELCHANGE) {
            const int modeIdx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_VOLC_MODE));
            HWND langCombo = GetDlgItem(parent, IDC_VOLC_LANGUAGE);
            if (langCombo) EnableWindow(langCombo, modeIdx == 0);
            return true;
        }
        return false;
    case IDC_VOLC_SHOW_KEY: {
        m_keyVisible = !m_keyVisible;
        HWND keyEdit = GetDlgItem(parent, IDC_VOLC_API_KEY);
        if (keyEdit) {
            SendMessageW(keyEdit, EM_SETPASSWORDCHAR, m_keyVisible ? 0 : L'\u25CF', 0);
            InvalidateRect(keyEdit, nullptr, TRUE);
        }
        HWND btn = GetDlgItem(parent, IDC_VOLC_SHOW_KEY);
        if (btn) SetWindowTextW(btn, m_keyVisible ? L"Hide" : L"Show");
        return true;
    }
    case IDC_VOLC_OPEN_LOG:
        OpenAsrDebugLog(parent, L"volc_asr_debug.log");
        return true;
    case IDC_VOLC_ADVANCED: {
        m_advanced.mode = CurrentMode(parent);
        VolcAdvancedDialogData edited = m_advanced;
        if (ShowVolcAdvancedDialog(parent, edited, ValidateVolcAdvancedData)) {
            m_advanced = edited;
            SetStatus(parent, L"Volcano Engine advanced settings updated. Click Save to apply.");
        }
        return true;
    }
    case IDC_VOLC_TEST: {
        Config snap = g_config;
        snap.volcApiKey = GetControlText(parent, IDC_VOLC_API_KEY, 256);
        snap.volcMode = CurrentMode(parent);
        snap.volcResourceId = CurrentResourceId(parent);
        snap.volcLanguage = CurrentLanguage(parent);
        snap.volcEnableInputContext = Button_GetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_INPUT_CONTEXT)) == BST_CHECKED;
        snap.volcEnableReuseVocabulary = Button_GetCheck(GetDlgItem(parent, IDC_VOLC_REUSE_VOCABULARY)) == BST_CHECKED;
        snap.volcHotwordsId = m_advanced.hotwordsId;
        snap.volcHotwordsName = m_advanced.hotwordsName;
        snap.volcCorrectTableId = m_advanced.correctTableId;
        snap.volcCorrectTableName = m_advanced.correctTableName;
        snap.volcEnableContext = m_advanced.enableContext;
        {
            const int parsed = _wtoi(m_advanced.contextHistory.c_str());
            snap.volcContextHistory = (parsed >= 1 && parsed <= 20) ? parsed : 3;
        }
        {
            const int parsed = _wtoi(m_advanced.endWindowSize.c_str());
            snap.volcEndWindowSize = parsed > 0 ? parsed : 800;
        }
        {
            const int parsed = _wtoi(m_advanced.forceToSpeechTime.c_str());
            snap.volcForceToSpeechTime = parsed >= 1 ? parsed : 0;
        }
        snap.volcEnableDdc = m_advanced.enableDdc;
        snap.volcEnableNonstream = m_advanced.enableNonstream;
        snap.volcEnableMusicFc = m_advanced.enableMusicFc;
        snap.volcEnablePoiFc = m_advanced.enablePoiFc;
        snap.volcExtraParams = m_advanced.extraParams;

        SetStatus(parent, L"Testing Volcano Engine ASR connection...");
        const uint64_t testGen = g_sharedTestGeneration.fetch_add(1) + 1;
        asr_probe::ProbeRequest req{ L"volcengine", snap };
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
