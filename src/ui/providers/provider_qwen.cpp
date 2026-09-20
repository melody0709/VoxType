#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_qwen.h"
#include "qwen_settings_helper.h"
#include "settings_controls.h"
#include "settings_dialogs.h"
#include "settings.h"
#include "ui_types.h"
#include "ui_utils.h"
#include "asr_probe_service.h"
#include "vocabulary_manager.h"

#include <windowsx.h>
#include <algorithm>
#include <format>

namespace ui_provider {

namespace {

ProviderQwen* s_qwenInstance = nullptr;

} // namespace

void ShowQwenSubControls(HWND hwnd) {
    if (s_qwenInstance) {
        s_qwenInstance->ShowSubControls(hwnd);
    }
}

void ProviderQwen::UpdateSubControls(HWND parent) {
    ShowSubControls(parent);
}

void ProviderQwen::ShowSubControls(HWND hwnd) {
    const std::wstring selectedModel = QwenModelFromControl(hwnd);
    const bool isAudio = IsQwenAudioHttpModel(selectedModel) || IsQwenAudioStreamingModel(selectedModel);
    const bool isStreaming = IsQwenAudioStreamingModel(selectedModel);
    for (HWND control : m_audio3Controls) {
        ShowWindow(control, isAudio ? SW_SHOW : SW_HIDE);
    }
    for (HWND control : m_streamingOnlyControls) {
        ShowWindow(control, isStreaming ? SW_SHOW : SW_HIDE);
    }
}

void ProviderQwen::CreateControls(HWND parent) {
    s_qwenInstance = this;
    m_controls.clear();
    m_audio3Controls.clear();
    m_streamingOnlyControls.clear();

    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(1)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"API Key");
    AddQwenControl(control);
    HWND qwenApiKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                                      S(UiStyle::InputLeft), S(UiStyle::RowInputY(1)), S(330), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_API_KEY)), GetParentInstance(parent), nullptr);
    ApplyUiFont(qwenApiKey);
    AddQwenControl(qwenApiKey);
    HWND btnShow = CreateButton(parent, IDC_QWEN_SHOW_KEY, S(UiStyle::SmallBtnX), S(UiStyle::RowInputY(1)) - S(1), S(UiStyle::SmallBtnW), S(UiStyle::BtnH), L"Show");
    AddQwenControl(btnShow);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(2)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Base URL");
    AddQwenControl(control);
    HWND qwenBaseUrl = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                       S(UiStyle::InputLeft), S(UiStyle::RowInputY(2)), S(UiStyle::InputWFull), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_BASE_URL)), GetParentInstance(parent), nullptr);
    ApplyUiFont(qwenBaseUrl);
    AddQwenControl(qwenBaseUrl);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(3)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Model");
    AddQwenControl(control);
    HWND qwenModel = CreateCombo(parent, IDC_QWEN_MODEL, S(UiStyle::InputLeft), S(UiStyle::RowInputY(3)), S(420), S(UiStyle::ComboH));
    AddQwenControl(qwenModel);
    HWND btnLog = CreateButton(parent, IDC_QWEN_OPEN_LOG, S(620), S(UiStyle::RowInputY(3)), S(110), S(UiStyle::ActionBtnH), L"Open log");
    AddQwenControl(btnLog);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::QwenLanguageY) + S(UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Fallback language");
    AddQwenControl(control);
    HWND qwenLang = CreateCombo(parent, IDC_QWEN_LANGUAGE, S(UiStyle::InputLeft), S(UiStyle::QwenLanguageY), S(UiStyle::ComboW), S(UiStyle::ComboH));
    AddQwenControl(qwenLang);
    control = CreateHint(parent, S(UiStyle::InputLeft), S(UiStyle::QwenLanguageHintY),
                         S(UiStyle::InputWFull), S(UiStyle::QwenHint2LineH),
                         L"Used only when Language hints is blank. Audio 3 sends no hint when both are Auto.");
    AddQwenControl(control);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::QwenChunkY) + S(UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Chunk ms");
    AddQwenControl(control);
    HWND qwenChunkMs = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                       S(UiStyle::InputLeft), S(UiStyle::QwenChunkY), S(80), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_CHUNK_MS)), GetParentInstance(parent), nullptr);
    ApplyUiFont(qwenChunkMs);
    AddQwenControl(qwenChunkMs);

    HWND qwenInputContext = CreateCheckBox(parent, IDC_QWEN_INPUT_CONTEXT,
                                            S(300), S(UiStyle::QwenChunkY), S(300), S(UiStyle::CheckH),
                                            L"Use focused field text as ASR context");
    AddQwenAudio3Control(qwenInputContext);
    control = CreateHint(parent, S(UiStyle::InputLeft), S(UiStyle::QwenChunkHintY),
                         S(UiStyle::InputWFull), S(UiStyle::QwenHint2LineH),
                         L"100–300 ms recommended. At record start, up to 400 characters are sent to the cloud.");
    AddQwenControl(control);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::QwenLanguageHintsY) + S(UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Language hints");
    AddQwenAudio3Control(control);
    HWND qwenHints = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     S(UiStyle::InputLeft), S(UiStyle::QwenLanguageHintsY), S(UiStyle::InputW), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_LANGUAGE_HINTS)), GetParentInstance(parent), nullptr);
    ApplyUiFont(qwenHints);
    AddQwenAudio3Control(qwenHints);
    HWND btnResetHints = CreateButton(parent, IDC_QWEN_LANGUAGE_HINTS_RESET,
                                      S(UiStyle::SideBtnX), S(UiStyle::QwenLanguageHintsY) - S(1),
                                      S(UiStyle::SideBtnW), S(UiStyle::BtnH), L"Reset");
    AddQwenAudio3Control(btnResetHints);
    control = CreateHint(parent, S(UiStyle::InputLeft), S(UiStyle::QwenLanguageHintsHintY),
                         S(UiStyle::InputWFull), S(UiStyle::QwenHint2LineH),
                         L"Effective language: Auto. Non-empty hints override Fallback language; blank = Auto.");
    m_languageHintsHint = control;
    AddQwenAudio3Control(control);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::QwenAdvancedButtonY) + S(UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Advanced");
    AddQwenAudio3Control(control);
    HWND btnAdv = CreateButton(parent, IDC_QWEN_ADVANCED,
                               S(UiStyle::InputLeft), S(UiStyle::QwenAdvancedButtonY),
                               S(UiStyle::ActionBtnW), S(UiStyle::ActionBtnH), L"Advanced...");
    AddQwenAudio3Control(btnAdv);
    control = CreateHint(parent, S(UiStyle::InputLeft), S(UiStyle::QwenAdvancedHintY),
                         S(UiStyle::InputWFull), S(UiStyle::QwenHint2LineH),
                         L"Hotwords, context refresh, sensitive-word filtering, punctuation and VAD thresholds.");
    AddQwenAudio3Control(control);

    // Hidden edit/checkbox controls for Advanced parameters to hold data in main window:
    HWND qwenVocabId = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, parent,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_VOCABULARY_ID)), GetParentInstance(parent), nullptr);
    HWND qwenVocabulary = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | ES_MULTILINE, 0, 0, 0, 0, parent,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_VOCABULARY)), GetParentInstance(parent), nullptr);
    HWND qwenSemantic = CreateWindowW(L"BUTTON", nullptr, WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 0, 0, parent,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_SEMANTIC_PUNCTUATION)), GetParentInstance(parent), nullptr);
    HWND qwenSilence = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, 0, 0, 0, 0, parent,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_MAX_SENTENCE_SILENCE)), GetParentInstance(parent), nullptr);
    HWND qwenMulti = CreateWindowW(L"BUTTON", nullptr, WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 0, 0, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_MULTI_THRESHOLD)), GetParentInstance(parent), nullptr);
    HWND qwenHeartbeat = CreateWindowW(L"BUTTON", nullptr, WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 0, 0, parent,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_HEARTBEAT)), GetParentInstance(parent), nullptr);
    HWND qwenNoiseEnable = CreateWindowW(L"BUTTON", nullptr, WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 0, 0, parent,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_SPEECH_NOISE_ENABLE)), GetParentInstance(parent), nullptr);
    HWND qwenNoise = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, parent,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_SPEECH_NOISE_THRESHOLD)), GetParentInstance(parent), nullptr);
    HWND qwenContinue = CreateWindowW(L"BUTTON", nullptr, WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 0, 0, parent,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_CONTINUE_CONTEXT)), GetParentInstance(parent), nullptr);
    HWND qwenSpecialReplace = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | ES_MULTILINE, 0, 0, 0, 0, parent,
                                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_SPECIAL_REPLACE)), GetParentInstance(parent), nullptr);
    HWND qwenSpecialEmpty = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | ES_MULTILINE, 0, 0, 0, 0, parent,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_SPECIAL_EMPTY)), GetParentInstance(parent), nullptr);
    HWND qwenSystemFilter = CreateWindowW(L"BUTTON", nullptr, WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 0, 0, parent,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_SYSTEM_FILTER)), GetParentInstance(parent), nullptr);
    ApplyUiFont(qwenVocabId); ApplyUiFont(qwenVocabulary); ApplyUiFont(qwenSemantic);
    ApplyUiFont(qwenSilence); ApplyUiFont(qwenMulti); ApplyUiFont(qwenHeartbeat);
    ApplyUiFont(qwenNoiseEnable); ApplyUiFont(qwenNoise); ApplyUiFont(qwenContinue);
    ApplyUiFont(qwenSpecialReplace); ApplyUiFont(qwenSpecialEmpty); ApplyUiFont(qwenSystemFilter);

    HWND btnTest = CreateButton(parent, IDC_QWEN_TEST, S(500), S(UiStyle::RowInputY(0)), S(UiStyle::ActionBtnW), S(UiStyle::ActionBtnH), L"Test Connection");
    AddQwenControl(btnTest);
}

void ProviderQwen::DestroyControls() {
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
    m_audio3Controls.clear();
    m_streamingOnlyControls.clear();
    m_languageHintsHint = nullptr;
}

void ProviderQwen::Show(bool visible) {
    for (HWND c : m_controls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
    if (visible) {
        ShowSubControls(m_controls.empty() ? nullptr : GetParent(m_controls[0]));
    } else {
        for (HWND c : m_audio3Controls) ShowWindow(c, SW_HIDE);
        for (HWND c : m_streamingOnlyControls) ShowWindow(c, SW_HIDE);
        HWND parent = m_controls.empty() ? nullptr : GetParent(m_controls[0]);
        if (parent) {
            m_keyVisible = false;
            HWND showQwenBtn = GetDlgItem(parent, IDC_QWEN_SHOW_KEY);
            if (showQwenBtn) SetWindowTextW(showQwenBtn, L"Show");
            HWND qwenKeyEdit = GetDlgItem(parent, IDC_QWEN_API_KEY);
            if (qwenKeyEdit) {
                SendMessageW(qwenKeyEdit, EM_SETPASSWORDCHAR, L'\u25CF', 0);
                InvalidateRect(qwenKeyEdit, nullptr, TRUE);
            }
        }
    }
}

void ProviderQwen::LoadControls(HWND parent, const Config& cfg) {
    m_keyVisible = false;
    HWND showQwenBtn = GetDlgItem(parent, IDC_QWEN_SHOW_KEY);
    if (showQwenBtn) SetWindowTextW(showQwenBtn, L"Show");
    HWND qwenKeyEdit = GetDlgItem(parent, IDC_QWEN_API_KEY);
    if (qwenKeyEdit) SendMessageW(qwenKeyEdit, EM_SETPASSWORDCHAR, L'\u25CF', 0);

    SetWindowTextW(GetDlgItem(parent, IDC_QWEN_API_KEY), cfg.qwenApiKey.c_str());

    m_profileState.uiLegacyUrl = cfg.qwenBaseUrl;
    m_profileState.uiHttpUrl = cfg.qwenHttpBaseUrl;
    m_profileState.uiAudioStreamingUrl = cfg.qwenAudioStreamingBaseUrl;
    m_profileState.uiModel = cfg.qwenModel;

    HWND qwenModelCombo = GetDlgItem(parent, IDC_QWEN_MODEL);
    if (qwenModelCombo) {
        ComboBox_ResetContent(qwenModelCombo);
        ComboBox_AddString(qwenModelCombo, L"qwen-audio-3.0-asr-flash-streaming");
        ComboBox_AddString(qwenModelCombo, L"qwen-audio-3.0-asr-flash");
        ComboBox_AddString(qwenModelCombo, L"qwen3-asr-flash-realtime");
        int idx = ComboBox_FindStringExact(qwenModelCombo, -1, cfg.qwenModel.c_str());
        if (idx == CB_ERR) idx = 0;
        ComboBox_SetCurSel(qwenModelCombo, idx);
    }

    HWND qwenLangCombo = GetDlgItem(parent, IDC_QWEN_LANGUAGE);
    if (qwenLangCombo) {
        PopulateQwenLanguageCombo(qwenLangCombo);
        ComboBox_SetCurSel(qwenLangCombo, QwenLanguageIndexFromCode(cfg.qwenLanguage));
    }

    SetWindowTextW(GetDlgItem(parent, IDC_QWEN_LANGUAGE_HINTS), cfg.qwenLanguageHints.c_str());
    SetWindowTextW(GetDlgItem(parent, IDC_QWEN_CHUNK_MS), std::to_wstring(cfg.qwenChunkMs).c_str());
    Button_SetCheck(GetDlgItem(parent, IDC_QWEN_INPUT_CONTEXT), cfg.qwenEnableInputContext ? BST_CHECKED : BST_UNCHECKED);

    SetWindowTextW(GetDlgItem(parent, IDC_QWEN_VOCABULARY_ID), cfg.qwenVocabularyId.c_str());
    std::wstring vocabText = cfg.qwenVocabulary;
    auto fileRes = vocabulary_manager::ReadVocabularyFile();
    if (fileRes && !fileRes->empty()) {
        vocabText = *fileRes;
    }
    SetWindowTextW(GetDlgItem(parent, IDC_QWEN_VOCABULARY), vocabText.c_str());
    Button_SetCheck(GetDlgItem(parent, IDC_QWEN_SEMANTIC_PUNCTUATION), cfg.qwenSemanticPunctuation ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(GetDlgItem(parent, IDC_QWEN_MAX_SENTENCE_SILENCE),
                   cfg.qwenMaxSentenceSilenceMs > 0 ? std::to_wstring(cfg.qwenMaxSentenceSilenceMs).c_str() : L"");
    Button_SetCheck(GetDlgItem(parent, IDC_QWEN_MULTI_THRESHOLD), cfg.qwenMultiThresholdMode ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(parent, IDC_QWEN_HEARTBEAT), cfg.qwenHeartbeat ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(parent, IDC_QWEN_SPEECH_NOISE_ENABLE), cfg.qwenSpeechNoiseThresholdEnabled ? BST_CHECKED : BST_UNCHECKED);
    if (cfg.qwenSpeechNoiseThreshold >= 0.0f) {
        std::wstring s = std::format(L"{}", cfg.qwenSpeechNoiseThreshold);
        SetWindowTextW(GetDlgItem(parent, IDC_QWEN_SPEECH_NOISE_THRESHOLD), s.c_str());
    } else {
        SetWindowTextW(GetDlgItem(parent, IDC_QWEN_SPEECH_NOISE_THRESHOLD), L"");
    }
    Button_SetCheck(GetDlgItem(parent, IDC_QWEN_CONTINUE_CONTEXT), cfg.qwenEnableContinueContext ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(GetDlgItem(parent, IDC_QWEN_SPECIAL_REPLACE), cfg.qwenSpecialWordReplaceList.c_str());
    SetWindowTextW(GetDlgItem(parent, IDC_QWEN_SPECIAL_EMPTY), cfg.qwenSpecialWordEmptyList.c_str());
    Button_SetCheck(GetDlgItem(parent, IDC_QWEN_SYSTEM_FILTER), cfg.qwenSystemReservedFilter ? BST_CHECKED : BST_UNCHECKED);

    ApplyQwenModelProfile(parent, cfg.qwenModel, false, m_profileState, m_languageHintsHint);
}

void ProviderQwen::SaveControls(HWND parent, Config& cfg) {
    StoreQwenProfileUrl(parent, m_profileState.uiModel, m_profileState);
    cfg.qwenApiKey = QwenControlText(parent, IDC_QWEN_API_KEY, 1024);
    cfg.qwenModel = QwenModelFromControl(parent);
    cfg.qwenBaseUrl = m_profileState.uiLegacyUrl.empty() ? kQwenDefaultBaseUrl : m_profileState.uiLegacyUrl;
    cfg.qwenHttpBaseUrl = m_profileState.uiHttpUrl;
    cfg.qwenAudioStreamingBaseUrl = m_profileState.uiAudioStreamingUrl;

    const std::wstring currentUrl = QwenControlText(parent, IDC_QWEN_BASE_URL, 2048);
    if (IsQwenAudioHttpModel(cfg.qwenModel)) {
        cfg.qwenHttpBaseUrl = currentUrl;
    } else if (IsQwenAudioStreamingModel(cfg.qwenModel)) {
        cfg.qwenAudioStreamingBaseUrl = currentUrl;
    } else {
        cfg.qwenBaseUrl = currentUrl;
    }

    cfg.qwenLanguage = QwenLanguageCodeFromIndex(
        ComboBox_GetCurSel(GetDlgItem(parent, IDC_QWEN_LANGUAGE)));
    cfg.qwenLanguageHints = NormalizeQwenLanguageHints(
        QwenControlText(parent, IDC_QWEN_LANGUAGE_HINTS, 1024));

    wchar_t chunkBuf[32] = {};
    GetWindowTextW(GetDlgItem(parent, IDC_QWEN_CHUNK_MS), chunkBuf, 32);
    cfg.qwenChunkMs = std::clamp(_wtoi(chunkBuf), 20, 1000);

    cfg.qwenEnableInputContext = Button_GetCheck(GetDlgItem(parent, IDC_QWEN_INPUT_CONTEXT)) == BST_CHECKED;

    cfg.qwenVocabularyId = QwenControlText(parent, IDC_QWEN_VOCABULARY_ID, 512);
    cfg.qwenVocabulary = QwenControlText(parent, IDC_QWEN_VOCABULARY);
    cfg.qwenSemanticPunctuation = Button_GetCheck(GetDlgItem(parent, IDC_QWEN_SEMANTIC_PUNCTUATION)) == BST_CHECKED;

    wchar_t silenceBuf[32] = {};
    GetWindowTextW(GetDlgItem(parent, IDC_QWEN_MAX_SENTENCE_SILENCE), silenceBuf, 32);
    cfg.qwenMaxSentenceSilenceMs = std::clamp(_wtoi(silenceBuf), 200, 6000);

    cfg.qwenMultiThresholdMode = !cfg.qwenSemanticPunctuation && Button_GetCheck(GetDlgItem(parent, IDC_QWEN_MULTI_THRESHOLD)) == BST_CHECKED;
    cfg.qwenHeartbeat = Button_GetCheck(GetDlgItem(parent, IDC_QWEN_HEARTBEAT)) == BST_CHECKED;
    cfg.qwenSpeechNoiseThresholdEnabled = Button_GetCheck(GetDlgItem(parent, IDC_QWEN_SPEECH_NOISE_ENABLE)) == BST_CHECKED;

    wchar_t noiseBuf[32] = {};
    GetWindowTextW(GetDlgItem(parent, IDC_QWEN_SPEECH_NOISE_THRESHOLD), noiseBuf, 32);
    cfg.qwenSpeechNoiseThreshold = std::clamp(static_cast<float>(_wtof(noiseBuf)), -1.0f, 1.0f);

    cfg.qwenEnableContinueContext = cfg.qwenEnableInputContext && Button_GetCheck(GetDlgItem(parent, IDC_QWEN_CONTINUE_CONTEXT)) == BST_CHECKED;
    cfg.qwenSpecialWordReplaceList = QwenControlText(parent, IDC_QWEN_SPECIAL_REPLACE);
    cfg.qwenSpecialWordEmptyList = QwenControlText(parent, IDC_QWEN_SPECIAL_EMPTY);
    cfg.qwenSystemReservedFilter = Button_GetCheck(GetDlgItem(parent, IDC_QWEN_SYSTEM_FILTER)) == BST_CHECKED;
}

bool ProviderQwen::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    switch (controlId) {
    case IDC_QWEN_SHOW_KEY: {
        m_keyVisible = !m_keyVisible;
        HWND keyEdit = GetDlgItem(parent, IDC_QWEN_API_KEY);
        if (keyEdit) {
            SendMessageW(keyEdit, EM_SETPASSWORDCHAR, m_keyVisible ? 0 : L'\u25CF', 0);
            InvalidateRect(keyEdit, nullptr, TRUE);
        }
        HWND btn = GetDlgItem(parent, IDC_QWEN_SHOW_KEY);
        if (btn) SetWindowTextW(btn, m_keyVisible ? L"Hide" : L"Show");
        return true;
    }
    case IDC_QWEN_OPEN_LOG:
        OpenAsrDebugLog(parent, L"qwen_asr_debug.log");
        return true;
    case IDC_QWEN_MODEL:
        if (notifyCode == CBN_SELCHANGE || notifyCode == CBN_EDITCHANGE) {
            ApplyQwenModelProfile(parent, QwenModelFromControl(parent), false, m_profileState, m_languageHintsHint);
            return true;
        }
        return false;
    case IDC_QWEN_LANGUAGE:
        if (notifyCode == CBN_SELCHANGE) {
            UpdateQwenLanguageEffectiveHint(parent, m_languageHintsHint);
            return true;
        }
        return false;
    case IDC_QWEN_LANGUAGE_HINTS_RESET:
        SetWindowTextW(GetDlgItem(parent, IDC_QWEN_LANGUAGE_HINTS), L"zh,en,yue");
        UpdateQwenLanguageEffectiveHint(parent, m_languageHintsHint);
        SetStatus(parent, L"Language hints reset to zh,en,yue. Click Save to apply.");
        return true;
    case IDC_QWEN_ADVANCED:
        EditQwenAdvancedSettings(parent, m_profileState, m_languageHintsHint);
        return true;
    case IDC_QWEN_TEST: {
        const std::wstring selectedModel = QwenModelFromControl(parent);
        Config snap = g_config;
        snap.qwenModel = selectedModel;
        snap.qwenApiKey = QwenControlText(parent, IDC_QWEN_API_KEY, 1024);
        const std::wstring enteredUrl = QwenControlText(parent, IDC_QWEN_BASE_URL, 2048);
        if (IsQwenAudioHttpModel(selectedModel)) {
            snap.qwenHttpBaseUrl = enteredUrl;
        } else if (IsQwenAudioStreamingModel(selectedModel)) {
            snap.qwenAudioStreamingBaseUrl = enteredUrl;
        } else {
            snap.qwenBaseUrl = enteredUrl;
        }
        snap.qwenLanguage = QwenLanguageCodeFromIndex(
            ComboBox_GetCurSel(GetDlgItem(parent, IDC_QWEN_LANGUAGE)));
        snap.qwenLanguageHints = NormalizeQwenLanguageHints(
            QwenControlText(parent, IDC_QWEN_LANGUAGE_HINTS, 1024));
        snap.qwenVocabularyId = QwenControlText(parent, IDC_QWEN_VOCABULARY_ID);
        snap.qwenVocabulary = QwenControlText(parent, IDC_QWEN_VOCABULARY);
        snap.qwenSemanticPunctuation = Button_GetCheck(GetDlgItem(parent, IDC_QWEN_SEMANTIC_PUNCTUATION)) == BST_CHECKED;
        snap.qwenMultiThresholdMode = Button_GetCheck(GetDlgItem(parent, IDC_QWEN_MULTI_THRESHOLD)) == BST_CHECKED;
        snap.qwenHeartbeat = Button_GetCheck(GetDlgItem(parent, IDC_QWEN_HEARTBEAT)) == BST_CHECKED;
        snap.qwenSpeechNoiseThresholdEnabled = Button_GetCheck(GetDlgItem(parent, IDC_QWEN_SPEECH_NOISE_ENABLE)) == BST_CHECKED;
        snap.qwenSpeechNoiseThreshold = std::clamp(static_cast<float>(_wtof(QwenControlText(parent, IDC_QWEN_SPEECH_NOISE_THRESHOLD).c_str())), -1.0f, 1.0f);
        snap.qwenMaxSentenceSilenceMs = std::clamp(_wtoi(QwenControlText(parent, IDC_QWEN_MAX_SENTENCE_SILENCE).c_str()), 200, 6000);
        snap.qwenSpecialWordReplaceList = QwenControlText(parent, IDC_QWEN_SPECIAL_REPLACE);
        snap.qwenSpecialWordEmptyList = QwenControlText(parent, IDC_QWEN_SPECIAL_EMPTY);
        snap.qwenSystemReservedFilter = Button_GetCheck(GetDlgItem(parent, IDC_QWEN_SYSTEM_FILTER)) == BST_CHECKED;

        std::wstring err;
        if (IsQwenAudioStreamingModel(selectedModel)) {
            if (!ValidateQwenEndpoint(snap.qwenAudioStreamingBaseUrl, L"wss", L"/api-ws/v1/inference", err)) {
                SetStatus(parent, err);
                MessageBoxW(parent, err.c_str(), L"Connection Test Failed", MB_ICONERROR | MB_OK);
                return true;
            }
        } else if (IsQwenAudioHttpModel(selectedModel)) {
            if (!ValidateQwenEndpoint(snap.qwenHttpBaseUrl, L"https", L"/api/v1/services/aigc/multimodal-generation/generation", err)) {
                SetStatus(parent, err);
                MessageBoxW(parent, err.c_str(), L"Connection Test Failed", MB_ICONERROR | MB_OK);
                return true;
            }
        } else {
            if (!ValidateQwenEndpoint(snap.qwenBaseUrl, L"wss", L"/api-ws/v1/realtime", err)) {
                std::wstring wsErr;
                if (!ValidateQwenEndpoint(snap.qwenBaseUrl, L"ws", L"/api-ws/v1/realtime", wsErr)) {
                    SetStatus(parent, err);
                    MessageBoxW(parent, err.c_str(), L"Connection Test Failed", MB_ICONERROR | MB_OK);
                    return true;
                }
            }
        }
        if (!ValidateQwenHints(QwenControlText(parent, IDC_QWEN_LANGUAGE_HINTS, 1024), err)) {
            SetStatus(parent, err);
            MessageBoxW(parent, err.c_str(), L"Connection Test Failed", MB_ICONERROR | MB_OK);
            return true;
        }

        SetStatus(parent, L"Testing Qwen ASR connection...");
        const uint64_t testGen = g_sharedTestGeneration.fetch_add(1) + 1;
        asr_probe::ProbeRequest req{ L"qwen", snap };
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
