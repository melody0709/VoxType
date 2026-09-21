#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tab_recognition.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "path_service.h"

#include <windowsx.h>
#include <thread>
#include <algorithm>

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

void TabRecognition::ShowVadSubGroup(int vadModelIdx) {
    for (HWND c : m_vadFireredControls) ShowWindow(c, vadModelIdx == 1 ? SW_SHOW : SW_HIDE);
    for (HWND c : m_vadSileroControls) ShowWindow(c, vadModelIdx == 0 ? SW_SHOW : SW_HIDE);
}

void TabRecognition::UpdateVadSubGroup(HWND parent) {
    int vadModelIndex = 0;
    if (g_config.vadModel == L"firered") vadModelIndex = 1;
    HWND vadModelCombo = GetDlgItem(parent, IDC_VAD_MODEL);
    if (vadModelCombo) vadModelIndex = ComboBox_GetCurSel(vadModelCombo);
    ShowVadSubGroup(vadModelIndex);
}

void TabRecognition::CreateControls(HWND parent) {
    m_controls.clear();
    m_vadFireredControls.clear();
    m_vadSileroControls.clear();
    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(0)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"ASR Backend");
    AddRecognitionControl(control);
    AddRecognitionControl(CreateCombo(parent, IDC_ASR_BACKEND, S(UiStyle::InputLeft), S(UiStyle::RowInputY(0)), S(UiStyle::PrimaryBackendComboW), S(UiStyle::ComboH)));
    control = CreateLabel(parent, S(UiStyle::FallbackLabelX), S(UiStyle::RowLabelY(0)), S(68), S(UiStyle::LabelH), L"Fallback");
    AddRecognitionControl(control);
    AddRecognitionControl(CreateCombo(parent, IDC_ASR_FALLBACK_BACKEND, S(UiStyle::FallbackComboX), S(UiStyle::RowInputY(0)), S(UiStyle::FallbackComboW), S(UiStyle::ComboH)));

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(1)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"ASR model");
    AddRecognitionControl(control);
    AddRecognitionControl(CreateCombo(parent, IDC_MODEL, S(UiStyle::InputLeft), S(UiStyle::RowInputY(1)), S(330), S(180)));
    AddRecognitionControl(CreateButton(parent, IDC_DOWNLOAD_MODELS, S(UiStyle::InputLeft) + S(350), S(UiStyle::RowInputY(1)) - S(1), S(220), S(UiStyle::BtnH), L"Download Local Model"));

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(2)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Model folder");
    AddRecognitionControl(control);
    HWND modelDir = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                    S(UiStyle::InputLeft), S(UiStyle::RowInputY(2)), S(UiStyle::InputW), S(UiStyle::EditH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MODEL_DIR)), GetParentInstance(parent), nullptr);
    ApplyUiFont(modelDir);
    AddRecognitionControl(modelDir);
    AddRecognitionControl(CreateButton(parent, IDC_BROWSE, S(UiStyle::SideBtnX), S(UiStyle::RowInputY(2)) - S(1), S(UiStyle::SideBtnW), S(UiStyle::BtnH), L"Browse..."));

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(3)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Threads");
    AddRecognitionControl(control);
    AddRecognitionControl(CreateCombo(parent, IDC_THREADS, S(UiStyle::InputLeft), S(UiStyle::RowInputY(3)), S(130), S(UiStyle::ComboH)));
    HWND vad = CreateWindowW(L"BUTTON", L"Enable VAD", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                             S(358), S(UiStyle::RowInputY(3)) + S(4), S(140), S(UiStyle::LabelH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VAD)), GetParentInstance(parent), nullptr);
    HWND partial = CreateWindowW(L"BUTTON", L"Partial result", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                 S(518), S(UiStyle::RowInputY(3)) + S(4), S(160), S(UiStyle::LabelH), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PARTIAL)), GetParentInstance(parent), nullptr);
    ApplyUiFont(vad);
    ApplyUiFont(partial);
    AddRecognitionControl(vad);
    AddRecognitionControl(partial);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(4)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"VAD model");
    AddRecognitionControl(control);
    AddRecognitionControl(CreateCombo(parent, IDC_VAD_MODEL, S(UiStyle::InputLeft), S(UiStyle::RowInputY(4)), S(UiStyle::ComboW), S(UiStyle::ComboH)));

    const int vadGroupY = S(UiStyle::RowInputY(5));
    HWND vadGroup = CreateWindowW(L"BUTTON", L"VAD Parameters", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                   S(30), vadGroupY, S(788), S(170), parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(vadGroup);
    AddRecognitionControl(vadGroup);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), vadGroupY + S(28), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Threshold");
    AddRecognitionControl(control);
    HWND vadThreshold = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                         S(UiStyle::InputLeft), vadGroupY + S(20), S(100), S(UiStyle::EditH), parent,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VAD_THRESHOLD)), GetParentInstance(parent), nullptr);
    ApplyUiFont(vadThreshold);
    AddRecognitionControl(vadThreshold);
    control = CreateLabel(parent, S(UiStyle::InputLeft) + S(108), vadGroupY + S(28), S(80), S(UiStyle::LabelH), L"(0.0~1.0)");
    AddRecognitionControl(control);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), vadGroupY + S(68), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Min silence");
    AddRecognitionControl(control);
    HWND vadMinSilence = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                          S(UiStyle::InputLeft), vadGroupY + S(60), S(100), S(UiStyle::EditH), parent,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VAD_MIN_SILENCE)), GetParentInstance(parent), nullptr);
    ApplyUiFont(vadMinSilence);
    AddRecognitionControl(vadMinSilence);
    control = CreateLabel(parent, S(UiStyle::InputLeft) + S(108), vadGroupY + S(68), S(80), S(UiStyle::LabelH), L"ms");
    AddRecognitionControl(control);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), vadGroupY + S(108), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Min speech");
    AddRecognitionControl(control);
    HWND vadMinSpeech = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                         S(UiStyle::InputLeft), vadGroupY + S(100), S(100), S(UiStyle::EditH), parent,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VAD_MIN_SPEECH)), GetParentInstance(parent), nullptr);
    ApplyUiFont(vadMinSpeech);
    AddRecognitionControl(vadMinSpeech);
    control = CreateLabel(parent, S(UiStyle::InputLeft) + S(108), vadGroupY + S(108), S(80), S(UiStyle::LabelH), L"ms");
    AddRecognitionControl(control);

    control = CreateLabel(parent, S(420), vadGroupY + S(68), S(100), S(UiStyle::LabelH), L"Pad start");
    AddRecognitionControl(control);
    AddVadFireredControl(control);
    HWND vadPadStart = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                        S(530), vadGroupY + S(60), S(100), S(UiStyle::EditH), parent,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VAD_PAD_START)), GetParentInstance(parent), nullptr);
    ApplyUiFont(vadPadStart);
    AddRecognitionControl(vadPadStart);
    AddVadFireredControl(vadPadStart);
    control = CreateLabel(parent, S(638), vadGroupY + S(68), S(80), S(UiStyle::LabelH), L"ms");
    AddRecognitionControl(control);
    AddVadFireredControl(control);

    control = CreateLabel(parent, S(420), vadGroupY + S(108), S(100), S(UiStyle::LabelH), L"Smooth win");
    AddRecognitionControl(control);
    AddVadFireredControl(control);
    HWND vadSmoothWin = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                         S(530), vadGroupY + S(100), S(100), S(UiStyle::EditH), parent,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VAD_SMOOTH_WINDOW)), GetParentInstance(parent), nullptr);
    ApplyUiFont(vadSmoothWin);
    AddRecognitionControl(vadSmoothWin);
    AddVadFireredControl(vadSmoothWin);

    control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(9)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Punctuation");
    AddRecognitionControl(control);
    AddRecognitionControl(CreateCombo(parent, IDC_POSTPROCESS, S(UiStyle::InputLeft), S(UiStyle::RowInputY(9)), S(UiStyle::ComboW), S(UiStyle::ComboH)));
}

void TabRecognition::DestroyControls() {
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
    m_vadFireredControls.clear();
    m_vadSileroControls.clear();
}

void TabRecognition::Show(bool visible) {
    for (HWND c : m_controls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
    if (visible) {
        UpdateVadSubGroup(m_controls.empty() ? nullptr : GetParent(m_controls[0]));
    } else {
        for (HWND c : m_vadFireredControls) ShowWindow(c, SW_HIDE);
        for (HWND c : m_vadSileroControls) ShowWindow(c, SW_HIDE);
    }
}

void TabRecognition::LoadControls(HWND parent, const Config& cfg) {
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

    Button_SetCheck(GetDlgItem(parent, IDC_VAD), cfg.enableVad ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(parent, IDC_PARTIAL), cfg.enablePartial ? BST_CHECKED : BST_UNCHECKED);

    HWND post = GetDlgItem(parent, IDC_POSTPROCESS);
    if (post) {
        ComboBox_ResetContent(post);
        ComboBox_AddString(post, L"Disabled");
        ComboBox_AddString(post, L"Auto punctuate");
        ComboBox_AddString(post, L"Auto punctuate + LLM");
        int postIndex = 1;
        if (cfg.postprocess == L"none") postIndex = 0;
        else if (cfg.postprocess == L"llm") postIndex = 2;
        ComboBox_SetCurSel(post, postIndex);
    }

    HWND vadModelCombo = GetDlgItem(parent, IDC_VAD_MODEL);
    if (vadModelCombo) {
        ComboBox_ResetContent(vadModelCombo);
        ComboBox_AddString(vadModelCombo, L"Silero VAD");
        ComboBox_AddString(vadModelCombo, L"FireRed VAD");
        int vadModelIndex = 0;
        if (cfg.vadModel == L"firered") vadModelIndex = 1;
        ComboBox_SetCurSel(vadModelCombo, vadModelIndex);
        ShowVadSubGroup(vadModelIndex);
    }

    wchar_t buf[32] = {};
    swprintf_s(buf, L"%.2f", cfg.vadThreshold);
    SetWindowTextW(GetDlgItem(parent, IDC_VAD_THRESHOLD), buf);

    swprintf(buf, 32, L"%d", cfg.vadMinSilence);
    SetWindowTextW(GetDlgItem(parent, IDC_VAD_MIN_SILENCE), buf);

    swprintf(buf, 32, L"%d", cfg.vadMinSpeech);
    SetWindowTextW(GetDlgItem(parent, IDC_VAD_MIN_SPEECH), buf);

    swprintf(buf, 32, L"%d", cfg.vadPadStart);
    SetWindowTextW(GetDlgItem(parent, IDC_VAD_PAD_START), buf);

    swprintf(buf, 32, L"%d", cfg.vadSmoothWindow);
    SetWindowTextW(GetDlgItem(parent, IDC_VAD_SMOOTH_WINDOW), buf);

    HWND backendCombo = GetDlgItem(parent, IDC_ASR_BACKEND);
    PopulateBackendCombo(backendCombo, cfg.asrBackend, false);
    PopulateBackendCombo(GetDlgItem(parent, IDC_ASR_FALLBACK_BACKEND), cfg.fallbackAsrBackend, true);
}

void TabRecognition::SaveControls(HWND parent, Config& cfg) {
    int sel = ComboBox_GetCurSel(GetDlgItem(parent, IDC_MODEL));
    cfg.modelId = ModelIdFromIndex(sel);

    wchar_t modelDir[MAX_PATH] = {};
    GetWindowTextW(GetDlgItem(parent, IDC_MODEL_DIR), modelDir, MAX_PATH);
    cfg.modelDir = modelDir;

    int threadSel = ComboBox_GetCurSel(GetDlgItem(parent, IDC_THREADS));
    cfg.threads = threadSel == 0 ? L"auto" : std::to_wstring(threadSel);

    cfg.enableVad = Button_GetCheck(GetDlgItem(parent, IDC_VAD)) == BST_CHECKED;
    cfg.enablePartial = Button_GetCheck(GetDlgItem(parent, IDC_PARTIAL)) == BST_CHECKED;

    int postSel = ComboBox_GetCurSel(GetDlgItem(parent, IDC_POSTPROCESS));
    const wchar_t* postValues[] = { L"none", L"auto", L"llm" };
    cfg.postprocess = (postSel >= 0 && postSel < 3) ? postValues[postSel] : L"auto";

    int vadIdx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_VAD_MODEL));
    cfg.vadModel = (vadIdx == 1) ? L"firered" : L"silero";

    wchar_t valBuf[32] = {};
    GetWindowTextW(GetDlgItem(parent, IDC_VAD_THRESHOLD), valBuf, 32);
    cfg.vadThreshold = static_cast<float>(_wtof(valBuf));
    if (cfg.vadThreshold <= 0.0f || cfg.vadThreshold > 1.0f) cfg.vadThreshold = 0.5f;

    GetWindowTextW(GetDlgItem(parent, IDC_VAD_MIN_SILENCE), valBuf, 32);
    int v = _wtoi(valBuf);
    if (v > 0) cfg.vadMinSilence = v;

    GetWindowTextW(GetDlgItem(parent, IDC_VAD_MIN_SPEECH), valBuf, 32);
    v = _wtoi(valBuf);
    if (v > 0) cfg.vadMinSpeech = v;

    GetWindowTextW(GetDlgItem(parent, IDC_VAD_PAD_START), valBuf, 32);
    v = _wtoi(valBuf);
    if (v >= 0) cfg.vadPadStart = v;

    GetWindowTextW(GetDlgItem(parent, IDC_VAD_SMOOTH_WINDOW), valBuf, 32);
    v = _wtoi(valBuf);
    if (v >= 1) cfg.vadSmoothWindow = v;

    cfg.asrBackend = BackendIdFromCombo(GetDlgItem(parent, IDC_ASR_BACKEND), false);
    cfg.fallbackAsrBackend = BackendIdFromCombo(GetDlgItem(parent, IDC_ASR_FALLBACK_BACKEND), true);
    if (cfg.fallbackAsrBackend == cfg.asrBackend) {
        cfg.fallbackAsrBackend = L"none";
    }
}

bool TabRecognition::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    switch (controlId) {
    case IDC_ASR_BACKEND:
        if (notifyCode == CBN_SELCHANGE) {
            std::wstring primary = BackendIdFromCombo(GetDlgItem(parent, IDC_ASR_BACKEND), false);
            std::wstring fallback = BackendIdFromCombo(GetDlgItem(parent, IDC_ASR_FALLBACK_BACKEND), true);
            if (fallback == primary) {
                PopulateBackendCombo(GetDlgItem(parent, IDC_ASR_FALLBACK_BACKEND), L"none", true);
            }
            return true;
        }
        return false;
    case IDC_VAD_MODEL:
        if (notifyCode == CBN_SELCHANGE) {
            int idx = ComboBox_GetCurSel(GetDlgItem(parent, IDC_VAD_MODEL));
            ShowVadSubGroup(idx);
            return true;
        }
        return false;
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

} // namespace ui_tab
