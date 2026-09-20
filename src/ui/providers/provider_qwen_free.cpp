#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_qwen_free.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "asr_probe_service.h"

#include <windowsx.h>
#include <shlobj.h>
#include <memory>
#include <thread>
#include <atomic>

namespace ui_provider {

namespace {

std::atomic<uint64_t> g_qwenFreeStatusGeneration{0};
std::atomic<uint64_t> g_qwenFreeTestGeneration{0};

struct QwenFreeStatusMessage {
    uint64_t generation = 0;
    std::wstring text;
};

struct QwenFreeTestMessage {
    uint64_t generation = 0;
    asr_probe::ProbeResult result;
};

} // namespace

void RefreshQwenFreeStatus(HWND hwnd) {
    HWND status = GetDlgItem(hwnd, IDC_QWEN_FREE_STATUS);
    if (!status) return;

    SetWindowTextW(status, L"Checking Qwen device identity...");
    const uint64_t generation =
        g_qwenFreeStatusGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::wstring utdidOverride = g_config.qwenFreeUtdidOverride;
    const std::wstring shellPath = g_config.qwenFreeShellPath;
    std::thread([hwnd, generation, utdidOverride, shellPath]() {
        auto* message = new QwenFreeStatusMessage;
        message->generation = generation;
        message->text = asr_probe::GetQwenFreeStatusText(utdidOverride, shellPath);
        if (!PostMessageW(hwnd, kQwenFreeStatusResultMessage, 0,
                          reinterpret_cast<LPARAM>(message))) {
            delete message;
        }
    }).detach();
}

void CancelQwenFreeTests() {
    g_qwenFreeStatusGeneration.fetch_add(1, std::memory_order_relaxed);
    g_qwenFreeTestGeneration.fetch_add(1, std::memory_order_relaxed);
}

void ProviderQwenFree::RefreshStatus(HWND parent) {
    RefreshQwenFreeStatus(parent);
}

void ProviderQwenFree::CreateControls(HWND parent) {
    m_controls.clear();
    // Row 0: Test Connection
    m_controls.push_back(CreateButton(
        parent, IDC_QWEN_FREE_TEST, S(500), S(UiStyle::RowInputY(0)),
        S(UiStyle::ActionBtnW), S(UiStyle::ActionBtnH), L"Test Connection"));

    // Row 1: Shell path label + edit + browse
    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(1)),
                               S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Shell Path");
    m_controls.push_back(control);

    HWND qwenFreePath = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        S(UiStyle::InputLeft), S(UiStyle::RowInputY(1)),
        S(UiStyle::QwenFreeShellPathW), S(UiStyle::EditH),
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_FREE_SHELL_PATH)),
        GetParentInstance(parent), nullptr);
    ApplyUiFont(qwenFreePath);
    m_controls.push_back(qwenFreePath);

    m_controls.push_back(CreateButton(
        parent, IDC_QWEN_FREE_BROWSE,
        S(UiStyle::InputLeft + UiStyle::QwenFreeShellPathW + UiStyle::QwenFreeShellBrowseGap),
        S(UiStyle::RowInputY(1)) - S(1), S(UiStyle::SmallBtnW), S(UiStyle::BtnH),
        L"Browse..."));

    // Row 2: Status
    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(2)),
                          S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Status");
    m_controls.push_back(control);

    HWND qwenFreeStatus = CreateWindowW(
        L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        S(UiStyle::InputLeft), S(UiStyle::RowInputY(2)) + S(4),
        S(500), S(UiStyle::LabelH), parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_FREE_STATUS)),
        GetParentInstance(parent), nullptr);
    ApplyUiFont(qwenFreeStatus);
    m_controls.push_back(qwenFreeStatus);

    // Row 3: Qwen VoiceInputWrite options
    HWND qwenPolish = CreateCheckBox(
        parent, IDC_QWEN_FREE_POLISH,
        S(UiStyle::ContentLeft), S(UiStyle::RowInputY(3)),
        S(UiStyle::QwenFreeOptionCheckW), S(UiStyle::CheckH), L"Polish (auto)");
    m_controls.push_back(qwenPolish);

    HWND qwenPunct = CreateCheckBox(
        parent, IDC_QWEN_FREE_PUNCT,
        S(UiStyle::ContentLeft + UiStyle::QwenFreeOptionCheckW + UiStyle::QwenFreeOptionGap),
        S(UiStyle::RowInputY(3)), S(UiStyle::QwenFreeOptionCheckW),
        S(UiStyle::CheckH), L"Punctuation included");
    EnableWindow(qwenPunct, FALSE);
    m_controls.push_back(qwenPunct);

    HWND qwenCorrect = CreateCheckBox(
        parent, IDC_QWEN_FREE_CORRECT,
        S(UiStyle::ContentLeft + (UiStyle::QwenFreeOptionCheckW + UiStyle::QwenFreeOptionGap) * 2),
        S(UiStyle::RowInputY(3)), S(UiStyle::QwenFreeOptionCheckW),
        S(UiStyle::CheckH), L"Correction included");
    EnableWindow(qwenCorrect, FALSE);
    m_controls.push_back(qwenCorrect);

    // Row 4: More options
    HWND qwenRewrite = CreateCheckBox(
        parent, IDC_QWEN_FREE_REWRITE,
        S(UiStyle::ContentLeft), S(UiStyle::RowInputY(4)),
        S(UiStyle::QwenFreeRewriteCheckW), S(UiStyle::CheckH),
        L"Rewrite selection (experimental)");
    Button_SetCheck(qwenRewrite, BST_UNCHECKED);
    EnableWindow(qwenRewrite, FALSE);
    m_controls.push_back(qwenRewrite);

    HWND qwenDebug = CreateCheckBox(
        parent, IDC_QWEN_FREE_DEBUG,
        S(UiStyle::ContentLeft + UiStyle::QwenFreeRewriteCheckW + UiStyle::QwenFreeOptionGap),
        S(UiStyle::RowInputY(4)), S(UiStyle::QwenFreeDebugCheckW),
        S(UiStyle::CheckH), L"Debug log");
    m_controls.push_back(qwenDebug);

    // Row 5: Hint label
    control = CreateLabel(
        parent, S(UiStyle::InputLeft), S(UiStyle::RowInputY(5)), S(560), S(UiStyle::LabelH),
        L"VoiceInputWrite bundles punctuation and correction; only Polish is configurable. Selection rewrite is temporarily disabled.");
    m_controls.push_back(control);
}

void ProviderQwenFree::DestroyControls() {
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
}

void ProviderQwenFree::Show(bool visible) {
    for (HWND c : m_controls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
}

void ProviderQwenFree::LoadControls(HWND parent, const Config& cfg) {
    Button_SetCheck(GetDlgItem(parent, IDC_QWEN_FREE_POLISH),
                    cfg.qwenFreePolishEnabled ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(parent, IDC_QWEN_FREE_PUNCT),
                    cfg.qwenFreePunctEnabled ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(parent, IDC_QWEN_FREE_CORRECT),
                    cfg.qwenFreeCorrectEnabled ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(parent, IDC_QWEN_FREE_REWRITE),
                    cfg.qwenFreeRewriteEnabled ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(parent, IDC_QWEN_FREE_DEBUG),
                    cfg.qwenFreeDebugLog ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(GetDlgItem(parent, IDC_QWEN_FREE_SHELL_PATH), cfg.qwenFreeShellPath.c_str());
    RefreshQwenFreeStatus(parent);
}

void ProviderQwenFree::SaveControls(HWND parent, Config& cfg) {
    cfg.qwenFreePolishEnabled =
        Button_GetCheck(GetDlgItem(parent, IDC_QWEN_FREE_POLISH)) == BST_CHECKED;
    cfg.qwenFreePunctEnabled = cfg.qwenFreePolishEnabled;
    cfg.qwenFreeCorrectEnabled = cfg.qwenFreePolishEnabled;
    cfg.qwenFreeRewriteEnabled = false;
    cfg.qwenFreeDebugLog =
        Button_GetCheck(GetDlgItem(parent, IDC_QWEN_FREE_DEBUG)) == BST_CHECKED;
    cfg.qwenFreeShellPath = GetControlText(parent, IDC_QWEN_FREE_SHELL_PATH, 1024);
}

bool ProviderQwenFree::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    switch (controlId) {
    case IDC_QWEN_FREE_POLISH: {
        const bool enabled =
            Button_GetCheck(GetDlgItem(parent, IDC_QWEN_FREE_POLISH)) == BST_CHECKED;
        Button_SetCheck(GetDlgItem(parent, IDC_QWEN_FREE_PUNCT),
                        enabled ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(GetDlgItem(parent, IDC_QWEN_FREE_CORRECT),
                        enabled ? BST_CHECKED : BST_UNCHECKED);
        return true;
    }
    case IDC_QWEN_FREE_BROWSE: {
        wchar_t buffer[MAX_PATH] = {};
        BROWSEINFOW bi{};
        bi.hwndOwner = parent;
        bi.pszDisplayName = buffer;
        bi.lpszTitle = L"Select Qianwen IME Shell Directory";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
            wchar_t pathBuffer[MAX_PATH] = {};
            if (SHGetPathFromIDListW(pidl, pathBuffer)) {
                SetWindowTextW(GetDlgItem(parent, IDC_QWEN_FREE_SHELL_PATH), pathBuffer);
            }
            CoTaskMemFree(pidl);
        }
        return true;
    }
    case IDC_QWEN_FREE_TEST: {
        Config snap = g_config;
        snap.qwenFreeShellPath = GetControlText(parent, IDC_QWEN_FREE_SHELL_PATH, 1024);
        snap.qwenFreePolishEnabled =
            Button_GetCheck(GetDlgItem(parent, IDC_QWEN_FREE_POLISH)) == BST_CHECKED;
        snap.qwenFreeRewriteEnabled =
            Button_GetCheck(GetDlgItem(parent, IDC_QWEN_FREE_REWRITE)) == BST_CHECKED;

        EnableWindow(GetDlgItem(parent, IDC_QWEN_FREE_TEST), FALSE);
        const bool llmRequired = snap.qwenFreePolishEnabled || snap.qwenFreeRewriteEnabled;
        SetStatus(parent, llmRequired
            ? L"Testing Qianwen IME (Free) UTDID, ASR and LLM..."
            : L"Testing Qianwen IME (Free) UTDID and ASR...");
        const uint64_t generation =
            g_qwenFreeTestGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
        asr_probe::ProbeRequest req{ L"qwen_free", snap };
        if (auto* svc = asr_probe::GetProbeService()) {
            svc->ProbeAsync(req, [parent, generation](const asr_probe::ProbeResult& result) {
                auto* msg = new QwenFreeTestMessage{ generation, result };
                if (!PostMessageW(parent, kQwenFreeTestResultMessage, 0, reinterpret_cast<LPARAM>(msg))) {
                    delete msg;
                }
            });
        }
        return true;
    }
    default:
        return false;
    }
}

bool ProviderQwenFree::HandleMessage(HWND parent, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == kQwenFreeTestResultMessage) {
        std::unique_ptr<QwenFreeTestMessage> m(reinterpret_cast<QwenFreeTestMessage*>(lParam));
        if (!m || m->generation != g_qwenFreeTestGeneration.load(std::memory_order_relaxed)) {
            return true;
        }
        EnableWindow(GetDlgItem(parent, IDC_QWEN_FREE_TEST), TRUE);
        RefreshQwenFreeStatus(parent);
        if (m->result.ok) {
            SetStatus(parent, m->result.message);
        } else {
            const std::wstring detail = !m->result.message.empty()
                ? m->result.message
                : L"Qwen IME ASR connection failed.";
            const bool llmFailure = m->result.asrOk && !m->result.llmOk;
            SetStatus(parent, llmFailure
                ? L"Connection failed: Qwen IME (Free) LLM"
                : L"Connection failed: Qwen IME (Free) ASR");
            MessageBoxW(parent, detail.c_str(), L"Connection Test Failed", MB_ICONERROR | MB_OK);
        }
        return true;
    }
    if (msg == kQwenFreeStatusResultMessage) {
        std::unique_ptr<QwenFreeStatusMessage> m(reinterpret_cast<QwenFreeStatusMessage*>(lParam));
        if (m && m->generation == g_qwenFreeStatusGeneration.load(std::memory_order_relaxed)) {
            HWND status = GetDlgItem(parent, IDC_QWEN_FREE_STATUS);
            if (status) SetWindowTextW(status, m->text.c_str());
        }
        return true;
    }
    return false;
}

} // namespace ui_provider
