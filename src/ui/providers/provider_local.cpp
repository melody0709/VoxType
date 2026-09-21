#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "provider_local.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "path_service.h"

#include <windowsx.h>
#include <thread>
#include <algorithm>
#include <memory>

namespace ui_provider {

void LocalProviderPanel::CreateControls(HWND parent) {
    m_controls.clear();

    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(1)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"ASR model");
    AddLocalControl(control);
    AddLocalControl(CreateCombo(parent, IDC_MODEL, S(UiStyle::InputLeft), S(UiStyle::RowInputY(1)), S(330), S(180)));
    AddLocalControl(CreateButton(parent, IDC_DOWNLOAD_MODELS, S(UiStyle::InputLeft) + S(350), S(UiStyle::RowInputY(1)) - S(1), S(220), S(UiStyle::BtnH), L"Download Local Model"));

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(2)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Model folder");
    AddLocalControl(control);
    HWND modelDir = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                    S(UiStyle::InputLeft), S(UiStyle::RowInputY(2)), S(UiStyle::InputW), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MODEL_DIR)), GetParentInstance(parent), nullptr);
    ApplyUiFont(modelDir);
    AddLocalControl(modelDir);
    AddLocalControl(CreateButton(parent, IDC_BROWSE, S(UiStyle::SideBtnX), S(UiStyle::RowInputY(2)) - S(1), S(UiStyle::SideBtnW), S(UiStyle::BtnH), L"Browse..."));

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(3)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Threads");
    AddLocalControl(control);
    AddLocalControl(CreateCombo(parent, IDC_THREADS, S(UiStyle::InputLeft), S(UiStyle::RowInputY(3)), S(130), S(UiStyle::ComboH)));

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(4)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Punctuation");
    AddLocalControl(control);
    AddLocalControl(CreateCombo(parent, IDC_POSTPROCESS, S(UiStyle::InputLeft), S(UiStyle::RowInputY(4)), S(UiStyle::ComboW), S(UiStyle::ComboH)));
}

void LocalProviderPanel::DestroyControls() {
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
}

void LocalProviderPanel::Show(bool visible) {
    for (HWND c : m_controls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
}

void LocalProviderPanel::LoadControls(HWND parent, const Config& cfg) {
    HWND model = GetDlgItem(parent, IDC_MODEL);
    if (model) {
        ComboBox_ResetContent(model);
        ComboBox_AddString(model, L"FireRedASR2 CTC");
        ComboBox_AddString(model, L"FireRedASR2 AED");
        ComboBox_AddString(model, L"SenseVoiceSmall");
        ComboBox_SetCurSel(model, ModelIndex(cfg.modelId));
    }

    SetWindowTextW(GetDlgItem(parent, IDC_MODEL_DIR), cfg.modelDir.c_str());

    HWND threads = GetDlgItem(parent, IDC_THREADS);
    if (threads) {
        ComboBox_ResetContent(threads);
        int physical = std::thread::hardware_concurrency();
        if (physical < 1) physical = 4;
        int auto_threads = (std::min)(8, physical);
        std::wstring auto_label = L"auto (" + std::to_wstring(auto_threads) + L")";
        ComboBox_AddString(threads, auto_label.c_str());
        for (int i = 1; i <= 8; i++)
            ComboBox_AddString(threads, std::to_wstring(i).c_str());
        int threadIndex = 0;
        if (cfg.threads == L"auto") threadIndex = 0;
        else {
            int val = _wtoi(cfg.threads.c_str());
            if (val >= 1 && val <= 8) threadIndex = val;
        }
        ComboBox_SetCurSel(threads, threadIndex);
    }

    // Config::postprocess carries itn (default) / punct / llm / auto / none; the
    // offline punctuation model is loaded for itn/punct/llm/auto (engine_local.cpp).
    // Only three values are exposed here, and any other value must survive a save
    // verbatim: never silently rewrite an unknown mode as none/auto.
    HWND post = GetDlgItem(parent, IDC_POSTPROCESS);
    if (post) {
        ComboBox_ResetContent(post);
        ComboBox_AddString(post, L"Auto punctuate");
        ComboBox_AddString(post, L"ITN only");
        ComboBox_AddString(post, L"Disabled");
        int sel = 0;
        if (cfg.postprocess == L"itn") sel = 1;
        else if (cfg.postprocess == L"none") sel = 2;
        else if (cfg.postprocess != L"auto") sel = ComboBox_AddString(post, cfg.postprocess.c_str());
        ComboBox_SetCurSel(post, sel);
    }
}

void LocalProviderPanel::SaveControls(HWND parent, Config& cfg) {
    int sel = ComboBox_GetCurSel(GetDlgItem(parent, IDC_MODEL));
    cfg.modelId = ModelIdFromIndex(sel);

    wchar_t modelDir[MAX_PATH] = {};
    GetWindowTextW(GetDlgItem(parent, IDC_MODEL_DIR), modelDir, MAX_PATH);
    cfg.modelDir = modelDir;

    int threadSel = ComboBox_GetCurSel(GetDlgItem(parent, IDC_THREADS));
    cfg.threads = threadSel == 0 ? L"auto" : std::to_wstring(threadSel);

    HWND post = GetDlgItem(parent, IDC_POSTPROCESS);
    const int postSel = ComboBox_GetCurSel(post);
    if (postSel == 0) cfg.postprocess = L"auto";
    else if (postSel == 1) cfg.postprocess = L"itn";
    else if (postSel == 2) cfg.postprocess = L"none";
    else if (postSel > 2 && post && ComboBox_GetLBTextLen(post, postSel) < 64) {
        wchar_t rawPost[64] = {};
        ComboBox_GetLBText(post, postSel, rawPost);
        cfg.postprocess = rawPost;
    }
}

bool LocalProviderPanel::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    (void)notifyCode;
    (void)control;
    switch (controlId) {
    case IDC_BROWSE:
        BrowseModelDirectory(parent);
        return true;
    case IDC_DOWNLOAD_MODELS: {
        int ret = MessageBoxW(parent,
            L"Download ASR models? (~2.2GB)\n\n"
            L"This will open a PowerShell window.",
            L"Download Models",
            MB_YESNO | MB_ICONQUESTION);
        if (ret == IDYES) {
            if (RunModelDownloader(parent)) {
                EnableWindow(GetDlgItem(parent, IDC_DOWNLOAD_MODELS), FALSE);
                SetStatus(parent, L"Downloading... close PowerShell window when done.");
            }
        }
        return true;
    }
    default:
        return false;
    }
}

// Model downloads post WM_APP + 20 to the settings window; the completion handler
// lives with the controls it updates (IDC_MODEL_DIR / IDC_DOWNLOAD_MODELS), and both
// keep their parent and control ids so GetDlgItem keeps resolving.
bool LocalProviderPanel::HandleMessage(HWND parent, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    if (msg != WM_APP + 20) return false;
    EnableWindow(GetDlgItem(parent, IDC_DOWNLOAD_MODELS), TRUE);
    if (lParam) {
        std::unique_ptr<std::wstring> dir(reinterpret_cast<std::wstring*>(lParam));
        std::wstring newDir = std::move(*dir);
        SetWindowTextW(GetDlgItem(parent, IDC_MODEL_DIR), newDir.c_str());
        g_config.modelDir = newDir;
        SetStatus(parent, L"Download complete");
        MessageBoxW(parent, L"Download complete!", L"Success", MB_OK | MB_ICONINFORMATION);
    } else {
        SetStatus(parent, L"Download failed or model directory not found");
    }
    return true;
}

} // namespace ui_provider
