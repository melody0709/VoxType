#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_volcengine.h"
#include "settings_controls.h"
#include "settings_dialogs.h"
#include "settings.h"
#include "ui_types.h"
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

} // namespace

void ProviderVolcengine::CreateControls(HWND parent) {
    m_controls.clear();
    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(1)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"API Key (X-Api-Key)");
    m_controls.push_back(control);
    HWND volcApiKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                                      S(UiStyle::InputLeft), S(UiStyle::RowInputY(1)), S(330), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_API_KEY)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcApiKey);
    m_controls.push_back(volcApiKey);
    HWND btnShow = CreateButton(parent, IDC_VOLC_SHOW_KEY, S(UiStyle::SmallBtnX), S(UiStyle::RowInputY(1)) - S(1), S(UiStyle::SmallBtnW), S(UiStyle::BtnH), L"Show");
    m_controls.push_back(btnShow);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(2)), S(130), S(UiStyle::LabelH), L"Model");
    m_controls.push_back(control);
    HWND comboRes = CreateCombo(parent, IDC_VOLC_RESOURCE, S(UiStyle::InputLeft), S(UiStyle::RowInputY(2)), S(480), S(UiStyle::ComboH));
    m_controls.push_back(comboRes);
    HWND btnLog = CreateButton(parent, IDC_VOLC_OPEN_LOG, S(680), S(UiStyle::RowInputY(2)), S(100), S(UiStyle::ActionBtnH), L"Open log");
    m_controls.push_back(btnLog);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(3)), S(130), S(UiStyle::LabelH), L"ASR Mode");
    m_controls.push_back(control);
    HWND comboMode = CreateCombo(parent, IDC_VOLC_MODE, S(UiStyle::InputLeft), S(UiStyle::RowInputY(3)), S(220), S(UiStyle::ComboH));
    m_controls.push_back(comboMode);

    control = CreateLabel(parent, S(420), S(UiStyle::RowLabelY(3)), S(80), S(UiStyle::LabelH), L"Language");
    m_controls.push_back(control);
    HWND comboLang = CreateCombo(parent, IDC_VOLC_LANGUAGE, S(505), S(UiStyle::RowInputY(3)), S(240), S(UiStyle::ComboH));
    m_controls.push_back(comboLang);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowInputY(4)) + S(2), S(UiStyle::LabelWidth) + S(10), S(UiStyle::LabelH), L"end_window_size");
    m_controls.push_back(control);
    HWND volcEndWindow = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                         S(UiStyle::InputLeft) + S(10), S(UiStyle::RowInputY(4)), S(80), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_END_WINDOW_SIZE)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcEndWindow);
    m_controls.push_back(volcEndWindow);
    control = CreateLabel(parent, S(UiStyle::InputLeft) + S(96), S(UiStyle::RowInputY(4)) + S(2), S(30), S(UiStyle::LabelH), L"ms");
    m_controls.push_back(control);

    control = CreateLabel(parent, S(360), S(UiStyle::RowInputY(4)) + S(2), S(180), S(UiStyle::LabelH), L"force_to_speech_time");
    m_controls.push_back(control);
    HWND volcForceSpeech = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                           S(550), S(UiStyle::RowInputY(4)), S(60), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_FORCE_TO_SPEECH_TIME)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcForceSpeech);
    m_controls.push_back(volcForceSpeech);
    control = CreateLabel(parent, S(618), S(UiStyle::RowInputY(4)) + S(2), S(30), S(UiStyle::LabelH), L"ms");
    m_controls.push_back(control);

    HWND volcDdc = CreateWindowW(L"BUTTON", L"enable_ddc", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                 S(UiStyle::ContentLeft), S(UiStyle::RowInputY(5)) + S(6), S(120), S(UiStyle::CheckH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_ENABLE_DDC)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcDdc);
    m_controls.push_back(volcDdc);

    HWND volcNonstream = CreateWindowW(L"BUTTON", L"enable_nonstream", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                       S(195), S(UiStyle::RowInputY(5)) + S(6), S(170), S(UiStyle::CheckH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_ENABLE_NONSTREAM)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcNonstream);
    m_controls.push_back(volcNonstream);

    HWND volcMusicFc = CreateWindowW(L"BUTTON", L"enable_music_fc", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                     S(395), S(UiStyle::RowInputY(5)) + S(6), S(150), S(UiStyle::CheckH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_ENABLE_MUSIC_FC)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcMusicFc);
    m_controls.push_back(volcMusicFc);

    HWND volcPoiFc = CreateWindowW(L"BUTTON", L"enable_poi_fc", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                   S(575), S(UiStyle::RowInputY(5)) + S(6), S(130), S(UiStyle::CheckH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_ENABLE_POI_FC)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcPoiFc);
    m_controls.push_back(volcPoiFc);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowInputY(6)) + S(2), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Extra Params");
    m_controls.push_back(control);
    HWND btnExtra = CreateButton(parent, IDC_VOLC_EXTRA_PARAMS, S(UiStyle::InputLeft), S(UiStyle::RowInputY(6)), S(UiStyle::ActionBtnW), S(UiStyle::ActionBtnH), L"Edit Params");
    m_controls.push_back(btnExtra);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowInputY(7)) + S(2), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Hotwords ID");
    m_controls.push_back(control);
    HWND volcHotwordsId = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                          S(UiStyle::InputLeft), S(UiStyle::RowInputY(7)), S(260), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_HOTWORDS_ID)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcHotwordsId);
    m_controls.push_back(volcHotwordsId);
    control = CreateLabel(parent, S(462), S(UiStyle::RowInputY(7)) + S(2), S(48), S(UiStyle::LabelH), L"Name");
    m_controls.push_back(control);
    HWND volcHotwordsName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                            S(542), S(UiStyle::RowInputY(7)), S(230), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_HOTWORDS_NAME)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcHotwordsName);
    m_controls.push_back(volcHotwordsName);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowInputY(8)) + S(2), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Correct ID");
    m_controls.push_back(control);
    HWND volcCorrectTableId = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                               S(UiStyle::InputLeft), S(UiStyle::RowInputY(8)), S(260), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_CORRECT_TABLE_ID)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcCorrectTableId);
    m_controls.push_back(volcCorrectTableId);
    control = CreateLabel(parent, S(462), S(UiStyle::RowInputY(8)) + S(2), S(48), S(UiStyle::LabelH), L"Name");
    m_controls.push_back(control);
    HWND volcCorrectTableName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                                 S(542), S(UiStyle::RowInputY(8)), S(230), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_CORRECT_TABLE_NAME)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcCorrectTableName);
    m_controls.push_back(volcCorrectTableName);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowInputY(9)) + S(2), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Context");
    m_controls.push_back(control);

    HWND volcEnableContext = CreateWindowW(L"BUTTON", L"Use history as context", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                           S(UiStyle::InputLeft), S(UiStyle::RowInputY(9)) + S(6), S(210), S(UiStyle::CheckH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_ENABLE_CONTEXT)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcEnableContext);
    m_controls.push_back(volcEnableContext);

    HWND volcContextHistory = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                              S(UiStyle::InputLeft) + S(220), S(UiStyle::RowInputY(9)), S(44), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_CONTEXT_HISTORY)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcContextHistory);
    m_controls.push_back(volcContextHistory);
    control = CreateLabel(parent, S(UiStyle::InputLeft) + S(270), S(UiStyle::RowInputY(9)) + S(2), S(80), S(UiStyle::LabelH), L"history");
    m_controls.push_back(control);

    HWND volcEnableInputContext = CreateWindowW(L"BUTTON", L"Read input field context", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                 S(542), S(UiStyle::RowInputY(9)) + S(6), S(280), S(UiStyle::CheckH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_ENABLE_INPUT_CONTEXT)), GetParentInstance(parent), nullptr);
    ApplyUiFont(volcEnableInputContext);
    m_controls.push_back(volcEnableInputContext);

    HWND btnTest = CreateButton(parent, IDC_VOLC_TEST, S(500), S(UiStyle::RowInputY(0)), S(UiStyle::ActionBtnW), S(UiStyle::ActionBtnH), L"Test Connection");
    m_controls.push_back(btnTest);
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

    Button_SetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_NONSTREAM), cfg.volcEnableNonstream ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(parent, IDC_VOLC_ENABLE_NONSTREAM), cfg.volcMode == L"bigmodel_async");
    {
        bool fcEnabled = (cfg.volcMode == L"bigmodel_nostream") ||
            (cfg.volcMode == L"bigmodel_async" && cfg.volcEnableNonstream);
        EnableWindow(GetDlgItem(parent, IDC_VOLC_ENABLE_MUSIC_FC), fcEnabled);
        EnableWindow(GetDlgItem(parent, IDC_VOLC_ENABLE_POI_FC), fcEnabled);
    }
    Button_SetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_DDC), cfg.volcEnableDdc ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_MUSIC_FC), cfg.volcEnableMusicFc ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_POI_FC), cfg.volcEnablePoiFc ? BST_CHECKED : BST_UNCHECKED);
    {
        wchar_t ew[32] = {};
        _itow_s(cfg.volcEndWindowSize, ew, 10);
        SetWindowTextW(GetDlgItem(parent, IDC_VOLC_END_WINDOW_SIZE), ew);
    }
    {
        wchar_t ft[32] = {};
        _itow_s(cfg.volcForceToSpeechTime, ft, 10);
        SetWindowTextW(GetDlgItem(parent, IDC_VOLC_FORCE_TO_SPEECH_TIME), ft);
    }

    SetWindowTextW(GetDlgItem(parent, IDC_VOLC_HOTWORDS_ID), cfg.volcHotwordsId.c_str());
    SetWindowTextW(GetDlgItem(parent, IDC_VOLC_HOTWORDS_NAME), cfg.volcHotwordsName.c_str());
    SetWindowTextW(GetDlgItem(parent, IDC_VOLC_CORRECT_TABLE_ID), cfg.volcCorrectTableId.c_str());
    SetWindowTextW(GetDlgItem(parent, IDC_VOLC_CORRECT_TABLE_NAME), cfg.volcCorrectTableName.c_str());

    Button_SetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_CONTEXT), cfg.volcEnableContext ? BST_CHECKED : BST_UNCHECKED);
    {
        wchar_t ch[32] = {};
        _itow_s(cfg.volcContextHistory, ch, 10);
        SetWindowTextW(GetDlgItem(parent, IDC_VOLC_CONTEXT_HISTORY), ch);
    }
    Button_SetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_INPUT_CONTEXT), cfg.volcEnableInputContext ? BST_CHECKED : BST_UNCHECKED);
}

void ProviderVolcengine::SaveControls(HWND parent, Config& cfg) {
    wchar_t volcApiKey[256] = {};
    GetWindowTextW(GetDlgItem(parent, IDC_VOLC_API_KEY), volcApiKey, 256);
    cfg.volcApiKey = volcApiKey;

    int modeIdx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_VOLC_MODE));
    if (modeIdx == 1) cfg.volcMode = L"bigmodel_async";
    else if (modeIdx == 2) cfg.volcMode = L"bigmodel";
    else cfg.volcMode = L"bigmodel_nostream";

    int resIdx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_VOLC_RESOURCE));
    if (resIdx >= 0 && resIdx < 4) cfg.volcResourceId = kVolcResources[resIdx].resourceId;

    int langIdx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_VOLC_LANGUAGE));
    if (langIdx >= 0 && langIdx < 9) cfg.volcLanguage = kVolcLanguages[langIdx];

    cfg.volcEnableNonstream = Button_GetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_NONSTREAM)) == BST_CHECKED;
    cfg.volcEnableDdc = Button_GetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_DDC)) == BST_CHECKED;
    cfg.volcEnableMusicFc = Button_GetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_MUSIC_FC)) == BST_CHECKED;
    cfg.volcEnablePoiFc = Button_GetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_POI_FC)) == BST_CHECKED;

    wchar_t ew[32] = {};
    GetWindowTextW(GetDlgItem(parent, IDC_VOLC_END_WINDOW_SIZE), ew, 32);
    int parsed = _wtoi(ew);
    cfg.volcEndWindowSize = parsed > 0 ? parsed : 800;

    wchar_t ft[32] = {};
    GetWindowTextW(GetDlgItem(parent, IDC_VOLC_FORCE_TO_SPEECH_TIME), ft, 32);
    parsed = _wtoi(ft);
    cfg.volcForceToSpeechTime = parsed >= 1 ? parsed : 0;

    wchar_t hw[512] = {};
    GetWindowTextW(GetDlgItem(parent, IDC_VOLC_HOTWORDS_ID), hw, 512);
    cfg.volcHotwordsId = hw;

    GetWindowTextW(GetDlgItem(parent, IDC_VOLC_HOTWORDS_NAME), hw, 512);
    cfg.volcHotwordsName = hw;

    wchar_t ct[512] = {};
    GetWindowTextW(GetDlgItem(parent, IDC_VOLC_CORRECT_TABLE_ID), ct, 512);
    cfg.volcCorrectTableId = ct;

    GetWindowTextW(GetDlgItem(parent, IDC_VOLC_CORRECT_TABLE_NAME), ct, 512);
    cfg.volcCorrectTableName = ct;

    cfg.volcEnableContext = Button_GetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_CONTEXT)) == BST_CHECKED;
    wchar_t ch[32] = {};
    GetWindowTextW(GetDlgItem(parent, IDC_VOLC_CONTEXT_HISTORY), ch, 32);
    parsed = _wtoi(ch);
    cfg.volcContextHistory = (parsed >= 1 && parsed <= 20) ? parsed : 3;

    cfg.volcEnableInputContext = Button_GetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_INPUT_CONTEXT)) == BST_CHECKED;
}

bool ProviderVolcengine::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    switch (controlId) {
    case IDC_VOLC_MODE:
        if (notifyCode == CBN_SELCHANGE) {
            int modeIdx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_VOLC_MODE));
            HWND langCombo = GetDlgItem(parent, IDC_VOLC_LANGUAGE);
            if (langCombo) EnableWindow(langCombo, modeIdx == 0);
            EnableWindow(GetDlgItem(parent, IDC_VOLC_ENABLE_NONSTREAM), modeIdx == 1);
            {
                bool fcEnabled = (modeIdx == 0) ||
                    (modeIdx == 1 && Button_GetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_NONSTREAM)) == BST_CHECKED);
                EnableWindow(GetDlgItem(parent, IDC_VOLC_ENABLE_MUSIC_FC), fcEnabled);
                EnableWindow(GetDlgItem(parent, IDC_VOLC_ENABLE_POI_FC), fcEnabled);
            }
            return true;
        }
        return false;
    case IDC_VOLC_ENABLE_NONSTREAM: {
        int modeIdx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_VOLC_MODE));
        bool fcEnabled = (modeIdx == 0) ||
            (modeIdx == 1 && Button_GetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_NONSTREAM)) == BST_CHECKED);
        EnableWindow(GetDlgItem(parent, IDC_VOLC_ENABLE_MUSIC_FC), fcEnabled);
        EnableWindow(GetDlgItem(parent, IDC_VOLC_ENABLE_POI_FC), fcEnabled);
        return true;
    }
    case IDC_VOLC_EXTRA_PARAMS: {
        std::wstring params = g_config.volcExtraParams;
        if (ShowVolcExtraDialog(parent, params)) {
            g_config.volcExtraParams = params;
        }
        return true;
    }
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
    case IDC_VOLC_TEST: {
        Config snap = g_config;
        wchar_t tmp[256] = {};
        GetWindowTextW(GetDlgItem(parent, IDC_VOLC_API_KEY), tmp, 256);
        snap.volcApiKey = tmp;
        int modeIdx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_VOLC_MODE));
        if (modeIdx == 1) snap.volcMode = L"bigmodel_async";
        else if (modeIdx == 2) snap.volcMode = L"bigmodel";
        else snap.volcMode = L"bigmodel_nostream";
        int resIdx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_VOLC_RESOURCE));
        if (resIdx >= 0 && resIdx < 4) snap.volcResourceId = kVolcResources[resIdx].resourceId;
        int langIdx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_VOLC_LANGUAGE));
        if (langIdx >= 0 && langIdx < 9) snap.volcLanguage = kVolcLanguages[langIdx];
        snap.volcEnableNonstream = Button_GetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_NONSTREAM)) == BST_CHECKED;
        snap.volcEnableDdc = Button_GetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_DDC)) == BST_CHECKED;
        snap.volcEnableMusicFc = Button_GetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_MUSIC_FC)) == BST_CHECKED;
        snap.volcEnablePoiFc = Button_GetCheck(GetDlgItem(parent, IDC_VOLC_ENABLE_POI_FC)) == BST_CHECKED;
        {
            wchar_t value[32] = {};
            GetWindowTextW(GetDlgItem(parent, IDC_VOLC_END_WINDOW_SIZE), value, 32);
            const int parsedVal = _wtoi(value);
            snap.volcEndWindowSize = parsedVal > 0 ? parsedVal : 800;
        }
        {
            wchar_t value[32] = {};
            GetWindowTextW(GetDlgItem(parent, IDC_VOLC_FORCE_TO_SPEECH_TIME), value, 32);
            const int parsedVal = _wtoi(value);
            snap.volcForceToSpeechTime = parsedVal >= 1 ? parsedVal : 0;
        }
        snap.volcExtraParams = g_config.volcExtraParams;
        {
            wchar_t hw[512] = {};
            GetWindowTextW(GetDlgItem(parent, IDC_VOLC_HOTWORDS_ID), hw, 512);
            snap.volcHotwordsId = hw;
        }
        {
            wchar_t hw[512] = {};
            GetWindowTextW(GetDlgItem(parent, IDC_VOLC_HOTWORDS_NAME), hw, 512);
            snap.volcHotwordsName = hw;
        }
        {
            wchar_t ct[512] = {};
            GetWindowTextW(GetDlgItem(parent, IDC_VOLC_CORRECT_TABLE_ID), ct, 512);
            snap.volcCorrectTableId = ct;
        }
        {
            wchar_t ct[512] = {};
            GetWindowTextW(GetDlgItem(parent, IDC_VOLC_CORRECT_TABLE_NAME), ct, 512);
            snap.volcCorrectTableName = ct;
        }
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
