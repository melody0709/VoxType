#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tab_cloud_asr.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"

#include <windowsx.h>

namespace ui_tab {

namespace {

constexpr int kMaiCloudProviderIndex = 6;

} // namespace

TabCloudAsr::TabCloudAsr() = default;

void TabCloudAsr::CreateControls(HWND parent) {
    m_controls.clear();
    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(0)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Cloud Provider");
    AddCloudAsrControl(control);
    AddCloudAsrControl(CreateCombo(parent, IDC_CLOUD_PROVIDER, S(UiStyle::InputLeft), S(UiStyle::RowInputY(0)), S(UiStyle::ComboW), S(UiStyle::ComboH)));

    m_volc.CreateControls(parent);
    m_baidu.CreateControls(parent);
    m_qwen.CreateControls(parent);
    m_mimo.CreateControls(parent);
    m_doubao.CreateControls(parent);
    m_qwenFree.CreateControls(parent);
    m_mai.CreateControls(parent);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::CloudAsrHintY), S(UiStyle::CloudAsrHintW), S(UiStyle::LabelH),
                          L"Cloud ASR sends audio to remote servers. Keys are encrypted with DPAPI locally.");
    AddCloudAsrControl(control);
    m_hintControl = control;
}

void TabCloudAsr::DestroyControls() {
    m_volc.DestroyControls();
    m_baidu.DestroyControls();
    m_qwen.DestroyControls();
    m_mimo.DestroyControls();
    m_doubao.DestroyControls();
    m_qwenFree.DestroyControls();
    m_mai.DestroyControls();
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
    m_hintControl = nullptr;
}

void TabCloudAsr::ShowCloudSubPage(HWND hwnd, int providerIdx) {
    m_cloudProviderIdx = providerIdx;
    m_volc.Show(providerIdx == 0);
    m_baidu.Show(providerIdx == 1);
    m_qwen.Show(providerIdx == 2);
    m_mimo.Show(providerIdx == 3);
    m_doubao.Show(providerIdx == 4);
    m_qwenFree.Show(providerIdx == 5);
    m_mai.Show(providerIdx == kMaiCloudProviderIndex);
    ui_provider::ShowMaiApiSubPage(hwnd);
}

void TabCloudAsr::Show(bool visible) {
    for (HWND c : m_controls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
    if (visible) {
        ShowCloudSubPage(m_controls.empty() ? nullptr : GetParent(m_controls[0]), m_cloudProviderIdx);
    } else {
        m_volc.Show(false);
        m_baidu.Show(false);
        m_qwen.Show(false);
        m_mimo.Show(false);
        m_doubao.Show(false);
        m_qwenFree.Show(false);
        m_mai.Show(false);
    }
}

void TabCloudAsr::LoadControls(HWND parent, const Config& cfg) {
    HWND cloudProviderCombo = GetDlgItem(parent, IDC_CLOUD_PROVIDER);
    if (cloudProviderCombo) {
        ComboBox_ResetContent(cloudProviderCombo);
        ComboBox_AddString(cloudProviderCombo, L"Volcano Engine (Doubao)");
        ComboBox_AddString(cloudProviderCombo, L"Baidu Cloud");
        ComboBox_AddString(cloudProviderCombo, L"Qwen ASR (DashScope)");
        ComboBox_AddString(cloudProviderCombo, L"MiMo ASR (Xiaomi)");
        ComboBox_AddString(cloudProviderCombo, L"Doubao IME (Free)");
        ComboBox_AddString(cloudProviderCombo, L"Qwen IME (Free)");
        ComboBox_AddString(cloudProviderCombo, L"Microsoft MAI Transcribe 2");

        int cloudIdx = 0;
        if (cfg.cloudProvider == L"baidu") cloudIdx = 1;
        else if (cfg.cloudProvider == L"qwen") cloudIdx = 2;
        else if (cfg.cloudProvider == L"mimo") cloudIdx = 3;
        else if (cfg.cloudProvider == L"doubao_ime") cloudIdx = 4;
        else if (cfg.cloudProvider == L"qwen_free") cloudIdx = 5;
        else if (cfg.cloudProvider == L"mai") cloudIdx = kMaiCloudProviderIndex;
        ComboBox_SetCurSel(cloudProviderCombo, cloudIdx);
        m_cloudProviderIdx = cloudIdx;
    }

    m_volc.LoadControls(parent, cfg);
    m_baidu.LoadControls(parent, cfg);
    m_qwen.LoadControls(parent, cfg);
    m_mimo.LoadControls(parent, cfg);
    m_doubao.LoadControls(parent, cfg);
    m_qwenFree.LoadControls(parent, cfg);
    m_mai.LoadControls(parent, cfg);

    ShowCloudSubPage(parent, m_cloudProviderIdx);
}

void TabCloudAsr::SaveControls(HWND parent, Config& cfg) {
    int cloudIdx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_CLOUD_PROVIDER));
    if (cloudIdx == 0) cfg.cloudProvider = L"volcengine";
    else if (cloudIdx == 1) cfg.cloudProvider = L"baidu";
    else if (cloudIdx == 2) cfg.cloudProvider = L"qwen";
    else if (cloudIdx == 3) cfg.cloudProvider = L"mimo";
    else if (cloudIdx == 4) cfg.cloudProvider = L"doubao_ime";
    else if (cloudIdx == 5) cfg.cloudProvider = L"qwen_free";
    else if (cloudIdx == kMaiCloudProviderIndex) cfg.cloudProvider = L"mai";

    m_volc.SaveControls(parent, cfg);
    m_baidu.SaveControls(parent, cfg);
    m_qwen.SaveControls(parent, cfg);
    m_mimo.SaveControls(parent, cfg);
    m_doubao.SaveControls(parent, cfg);
    m_qwenFree.SaveControls(parent, cfg);
    m_mai.SaveControls(parent, cfg);
}

bool TabCloudAsr::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    if (controlId == IDC_CLOUD_PROVIDER && notifyCode == CBN_SELCHANGE) {
        int idx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_CLOUD_PROVIDER));
        ShowCloudSubPage(parent, idx);
        return true;
    }
    if (m_volc.HandleCommand(parent, notifyCode, controlId, control)) return true;
    if (m_baidu.HandleCommand(parent, notifyCode, controlId, control)) return true;
    if (m_qwen.HandleCommand(parent, notifyCode, controlId, control)) return true;
    if (m_mimo.HandleCommand(parent, notifyCode, controlId, control)) return true;
    if (m_doubao.HandleCommand(parent, notifyCode, controlId, control)) return true;
    if (m_qwenFree.HandleCommand(parent, notifyCode, controlId, control)) return true;
    if (m_mai.HandleCommand(parent, notifyCode, controlId, control)) return true;
    return false;
}

bool TabCloudAsr::HandleMessage(HWND parent, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (m_qwenFree.HandleMessage(parent, msg, wParam, lParam)) return true;
    return false;
}

} // namespace ui_tab
