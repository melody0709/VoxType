#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tab_speech_engine.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "ui_strings.h"

#include <windowsx.h>
#include <array>

namespace ui_tab {

namespace {

struct BackendOption {
    const wchar_t* id;
    const wchar_t* label;
    bool primarySupported;
    bool fallbackSupported;
};

constexpr int kBackendOptionCount = 8;
constexpr DWORD_PTR kDisabledBackendItem = static_cast<DWORD_PTR>(-1);

// 下拉项文案随界面语言切换，只能在填充时从文案表取，不得缓存进静态存储。
std::array<BackendOption, kBackendOptionCount> MakeBackendOptions() {
    const auto& text = ui_strings::UiText();
    return {{
        {L"local", text.speechBackendLocal, true, true},
        {L"volcengine", text.speechBackendVolc, true, false},
        {L"baidu", text.speechBackendBaidu, true, true},
        {L"qwen", text.speechBackendQwen, true, true},
        {L"mimo", text.speechBackendMimo, true, true},
        {L"mai", text.speechBackendMai, true, true},
        {L"doubao_ime", text.speechBackendDoubao, true, true},
        {L"qwen_free", text.speechBackendQwenFree, true, true},
    }};
}

bool BackendSupported(const BackendOption& option, bool fallback) {
    return fallback ? option.fallbackSupported : option.primarySupported;
}

void PopulateBackendCombo(HWND combo, const std::wstring& selectedBackend, bool fallback) {
    if (!combo) return;
    ComboBox_ResetContent(combo);
    const auto options = MakeBackendOptions();

    int selectedIndex = -1;
    if (fallback) {
        int item = ComboBox_AddString(combo, ui_strings::UiText().speechFallbackDisabled);
        ComboBox_SetItemData(combo, item, kDisabledBackendItem);
        if (selectedBackend.empty() || selectedBackend == L"none") {
            selectedIndex = item;
        }
    }

    for (int i = 0; i < kBackendOptionCount; ++i) {
        const BackendOption& option = options[i];
        if (!BackendSupported(option, fallback)) continue;
        int item = ComboBox_AddString(combo, option.label);
        ComboBox_SetItemData(combo, item, static_cast<DWORD_PTR>(i));
        if (selectedBackend == option.id) {
            selectedIndex = item;
        }
    }

    if (selectedIndex < 0) selectedIndex = 0;
    ComboBox_SetCurSel(combo, selectedIndex);
}

std::wstring BackendIdFromCombo(HWND combo, bool fallback) {
    if (!combo) return fallback ? L"none" : L"local";
    const int sel = ComboBox_GetCurSel(combo);
    if (sel < 0) return fallback ? L"none" : L"local";
    const DWORD_PTR data = ComboBox_GetItemData(combo, sel);
    if (fallback && data == kDisabledBackendItem) return L"none";
    const auto options = MakeBackendOptions();
    if (data < static_cast<DWORD_PTR>(kBackendOptionCount)) {
        const BackendOption& option = options[static_cast<int>(data)];
        if (BackendSupported(option, fallback)) return option.id;
    }
    return fallback ? L"none" : L"local";
}

} // namespace

TabSpeechEngine::TabSpeechEngine()
    : m_panels{ &m_local, &m_volc, &m_baidu, &m_qwen, &m_mimo, &m_doubao, &m_qwenFree, &m_mai } {}

void TabSpeechEngine::ShowBackendPanel(HWND parent, const std::wstring& backendId) {
    for (auto* panel : m_panels) {
        panel->Show(panel->Id() == backendId);
    }
    if (m_hintControl) {
        ShowWindow(m_hintControl, backendId == L"local" ? SW_HIDE : SW_SHOW);
    }
    if (parent) InvalidateRect(parent, nullptr, TRUE);
}

void TabSpeechEngine::CreateControls(HWND parent) {
    m_controls.clear();
    const auto& text = ui_strings::UiText();
    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(0)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), text.speechAsrBackendLabel);
    AddEngineControl(control);
    AddEngineControl(CreateCombo(parent, IDC_ASR_BACKEND, S(UiStyle::InputLeft), S(UiStyle::RowInputY(0)), S(UiStyle::PrimaryBackendComboW), S(UiStyle::ComboH)));
    control = CreateLabel(parent, S(UiStyle::FallbackLabelX), S(UiStyle::RowLabelY(0)), S(68), S(UiStyle::LabelH), text.speechFallbackLabel);
    AddEngineControl(control);
    AddEngineControl(CreateCombo(parent, IDC_ASR_FALLBACK_BACKEND, S(UiStyle::FallbackComboX), S(UiStyle::RowInputY(0)), S(UiStyle::FallbackComboW), S(UiStyle::ComboH)));

    for (auto* panel : m_panels) {
        panel->CreateControls(parent);
    }

    m_hintControl = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::CloudAsrHintY), S(UiStyle::CloudAsrHintW), S(UiStyle::LabelH),
                               text.speechCloudAsrHint);
    AddEngineControl(m_hintControl);
}

void TabSpeechEngine::DestroyControls() {
    for (auto* panel : m_panels) {
        panel->DestroyControls();
    }
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
    m_hintControl = nullptr;
}

void TabSpeechEngine::Show(bool visible) {
    for (HWND c : m_controls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
    if (visible) {
        HWND parent = m_controls.empty() ? nullptr : GetParent(m_controls[0]);
        ShowBackendPanel(parent, BackendIdFromCombo(GetDlgItem(parent, IDC_ASR_BACKEND), false));
    } else {
        for (auto* panel : m_panels) {
            panel->Show(false);
        }
        if (m_hintControl) ShowWindow(m_hintControl, SW_HIDE);
    }
}

void TabSpeechEngine::LoadControls(HWND parent, const Config& cfg) {
    PopulateBackendCombo(GetDlgItem(parent, IDC_ASR_BACKEND), cfg.asrBackend, false);
    PopulateBackendCombo(GetDlgItem(parent, IDC_ASR_FALLBACK_BACKEND), cfg.fallbackAsrBackend, true);

    for (auto* panel : m_panels) {
        panel->LoadControls(parent, cfg);
    }

    ShowBackendPanel(parent, BackendIdFromCombo(GetDlgItem(parent, IDC_ASR_BACKEND), false));
}

void TabSpeechEngine::SaveControls(HWND parent, Config& cfg) {
    cfg.asrBackend = BackendIdFromCombo(GetDlgItem(parent, IDC_ASR_BACKEND), false);
    cfg.fallbackAsrBackend = BackendIdFromCombo(GetDlgItem(parent, IDC_ASR_FALLBACK_BACKEND), true);
    if (cfg.fallbackAsrBackend == cfg.asrBackend) {
        cfg.fallbackAsrBackend = L"none";
    }

    for (auto* panel : m_panels) {
        panel->SaveControls(parent, cfg);
    }
}

bool TabSpeechEngine::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    if (controlId == IDC_ASR_BACKEND && notifyCode == CBN_SELCHANGE) {
        const std::wstring primary = BackendIdFromCombo(GetDlgItem(parent, IDC_ASR_BACKEND), false);
        const std::wstring fallback = BackendIdFromCombo(GetDlgItem(parent, IDC_ASR_FALLBACK_BACKEND), true);
        if (fallback == primary) {
            PopulateBackendCombo(GetDlgItem(parent, IDC_ASR_FALLBACK_BACKEND), L"none", true);
            SetStatus(parent, ui_strings::UiText().speechFallbackMatchesStatus);
        }
        ShowBackendPanel(parent, primary);
        return true;
    }
    for (auto* panel : m_panels) {
        if (panel->HandleCommand(parent, notifyCode, controlId, control)) return true;
    }
    return false;
}

bool TabSpeechEngine::HandleMessage(HWND parent, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (m_qwenFree.HandleMessage(parent, msg, wParam, lParam)) return true;
    if (m_local.HandleMessage(parent, msg, wParam, lParam)) return true;
    return false;
}

} // namespace ui_tab
