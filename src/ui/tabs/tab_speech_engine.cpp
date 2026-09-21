#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tab_speech_engine.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"

#include <windowsx.h>

namespace ui_tab {

namespace {

struct BackendOption {
    const wchar_t* id;
    const wchar_t* label;
    bool primarySupported;
    bool fallbackSupported;
};

constexpr BackendOption kBackendOptions[] = {
    {L"local", L"Local (sherpa-onnx)", true, true},
    {L"volcengine", L"Volcano Engine", true, false},
    {L"baidu", L"Baidu Cloud", true, true},
    {L"qwen", L"Qwen ASR", true, true},
    {L"mimo", L"MiMo ASR", true, true},
    {L"mai", L"Microsoft MAI Transcribe 2", true, true},
    {L"doubao_ime", L"Doubao IME (Free)", true, true},
    {L"qwen_free", L"Qwen IME (Free)", true, true},
};

constexpr int kBackendOptionCount = static_cast<int>(sizeof(kBackendOptions) / sizeof(kBackendOptions[0]));
constexpr DWORD_PTR kDisabledBackendItem = static_cast<DWORD_PTR>(-1);

bool BackendSupported(const BackendOption& option, bool fallback) {
    return fallback ? option.fallbackSupported : option.primarySupported;
}

void PopulateBackendCombo(HWND combo, const std::wstring& selectedBackend, bool fallback) {
    if (!combo) return;
    ComboBox_ResetContent(combo);

    int selectedIndex = -1;
    if (fallback) {
        int item = ComboBox_AddString(combo, L"Disabled");
        ComboBox_SetItemData(combo, item, kDisabledBackendItem);
        if (selectedBackend.empty() || selectedBackend == L"none") {
            selectedIndex = item;
        }
    }

    for (int i = 0; i < kBackendOptionCount; ++i) {
        const BackendOption& option = kBackendOptions[i];
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
    if (data < static_cast<DWORD_PTR>(kBackendOptionCount)) {
        const BackendOption& option = kBackendOptions[static_cast<int>(data)];
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
    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(0)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"ASR Backend");
    AddEngineControl(control);
    AddEngineControl(CreateCombo(parent, IDC_ASR_BACKEND, S(UiStyle::InputLeft), S(UiStyle::RowInputY(0)), S(UiStyle::PrimaryBackendComboW), S(UiStyle::ComboH)));
    control = CreateLabel(parent, S(UiStyle::FallbackLabelX), S(UiStyle::RowLabelY(0)), S(68), S(UiStyle::LabelH), L"Fallback");
    AddEngineControl(control);
    AddEngineControl(CreateCombo(parent, IDC_ASR_FALLBACK_BACKEND, S(UiStyle::FallbackComboX), S(UiStyle::RowInputY(0)), S(UiStyle::FallbackComboW), S(UiStyle::ComboH)));

    for (auto* panel : m_panels) {
        panel->CreateControls(parent);
    }

    m_hintControl = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::CloudAsrHintY), S(UiStyle::CloudAsrHintW), S(UiStyle::LabelH),
                               L"Cloud ASR sends audio to remote servers. Keys are encrypted with DPAPI locally.");
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
            SetStatus(parent, L"Fallback disabled because it matches ASR Backend.");
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
