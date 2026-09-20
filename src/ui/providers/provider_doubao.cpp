#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_doubao.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "asr_probe_service.h"
#include "app_messages.h"

namespace ui_provider {

namespace {

std::wstring DoubaoImeCredentialStatusText() {
    if (g_config.doubaoImeDeviceId.empty()) {
        return L"Not registered. First use or Test Connection will register automatically.";
    }
    std::wstring id = g_config.doubaoImeDeviceId;
    if (id.size() > 22) {
        id = id.substr(0, 10) + L"..." + id.substr(id.size() - 8);
    }
    return L"Registered device: " + id;
}

} // namespace

void RefreshDoubaoImeStatus(HWND hwnd) {
    HWND status = GetDlgItem(hwnd, IDC_DOUBAO_IME_STATUS);
    if (status) {
        SetWindowTextW(status, DoubaoImeCredentialStatusText().c_str());
    }
}

void ProviderDoubao::RefreshStatus(HWND parent) {
    RefreshDoubaoImeStatus(parent);
}

void ProviderDoubao::CreateControls(HWND parent) {
    m_controls.clear();
    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(1)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Credentials");
    m_controls.push_back(control);

    HWND doubaoStatus = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_WORDELLIPSIS,
        S(UiStyle::InputLeft), S(UiStyle::RowInputY(1)), S(580), S(UiStyle::EditH),
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DOUBAO_IME_STATUS)),
        GetParentInstance(parent), nullptr);
    ApplyUiFont(doubaoStatus);
    m_controls.push_back(doubaoStatus);

    HWND btnTest = CreateButton(parent, IDC_DOUBAO_IME_TEST, S(500), S(UiStyle::RowInputY(0)), S(UiStyle::ActionBtnW), S(UiStyle::ActionBtnH), L"Test Connection");
    m_controls.push_back(btnTest);

    HWND btnReset = CreateButton(parent, IDC_DOUBAO_IME_RESET, S(UiStyle::InputLeft), S(UiStyle::RowInputY(2)), S(170), S(UiStyle::ActionBtnH), L"Reset Credentials");
    m_controls.push_back(btnReset);

    HWND hint = CreateLabel(parent, S(UiStyle::InputLeft), S(UiStyle::RowInputY(3)), S(560), S(UiStyle::LabelH),
                            L"Experimental unofficial Doubao IME endpoint. No API key is required.");
    m_controls.push_back(hint);
}

void ProviderDoubao::DestroyControls() {
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
}

void ProviderDoubao::Show(bool visible) {
    for (HWND c : m_controls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
}

void ProviderDoubao::LoadControls(HWND parent, const Config& cfg) {
    RefreshDoubaoImeStatus(parent);
}

void ProviderDoubao::SaveControls(HWND parent, Config& cfg) {
    // Runtime tokens and device id are managed directly in config
}

bool ProviderDoubao::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    switch (controlId) {
    case IDC_DOUBAO_IME_TEST: {
        EnableWindow(GetDlgItem(parent, IDC_DOUBAO_IME_TEST), FALSE);
        SetStatus(parent, L"Testing Doubao IME ASR connection...");
        const uint64_t testGen = g_sharedTestGeneration.fetch_add(1) + 1;
        asr_probe::ProbeRequest req{ L"doubao_ime", g_config };
        if (auto* svc = asr_probe::GetProbeService()) {
            svc->ProbeAsync(req, [parent, testGen](const asr_probe::ProbeResult& result) {
                PostSharedTestResult(parent, testGen, result.ok, result.message);
                asr_probe::DoubaoCredentials* creds = nullptr;
                if (result.credentialsChanged) {
                    creds = new asr_probe::DoubaoCredentials(result.credentials);
                }
                if (!PostMessageW(parent, kDoubaoImeSettingsCredentialsMessage, static_cast<WPARAM>(testGen), reinterpret_cast<LPARAM>(creds))) {
                    delete creds;
                }
            });
        }
        return true;
    }
    case IDC_DOUBAO_IME_RESET:
        g_sharedTestGeneration.fetch_add(1, std::memory_order_relaxed);
        g_config.doubaoImeDeviceId.clear();
        g_config.doubaoImeCdid.clear();
        g_config.doubaoImeToken.clear();
        SaveConfig(g_config);
        RefreshDoubaoImeStatus(parent);
        EnableWindow(GetDlgItem(parent, IDC_DOUBAO_IME_TEST), TRUE);
        SetStatus(parent, L"Doubao IME credentials reset.");
        return true;
    default:
        return false;
    }
}

} // namespace ui_provider
