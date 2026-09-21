#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tab_advanced.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "config_store.h"
#include "asr_probe_service.h"

#include <windowsx.h>
#include <shellapi.h>

namespace ui_tab {

namespace {

std::wstring DiagnosticAudioModeFromControl(HWND hwnd) {
    const int index = ComboBox_GetCurSel(
        GetDlgItem(hwnd, IDC_DIAGNOSTIC_AUDIO_MODE));
    if (index == 1) return L"failures";
    if (index == 2) return L"all";
    return L"off";
}

void UpdateDiagnosticAudioHint(HWND hwnd) {
    HWND hint = GetDlgItem(hwnd, IDC_DIAGNOSTIC_AUDIO_HINT);
    if (!hint) return;
    const std::wstring mode = DiagnosticAudioModeFromControl(hwnd);
    const wchar_t* text = mode == L"all"
        ? L"Privacy: every utterance is saved as a playable WAV on this PC. Old files are removed automatically."
        : (mode == L"failures"
            ? L"Only diagnostic failures are saved on this PC. Old files are removed automatically."
            : L"Audio recording diagnostics are off. No diagnostic WAV files are saved.");
    SetWindowTextW(hint, text);
}

} // namespace

void TabAdvanced::ShowVadSubGroup(int vadModelIdx) {
    for (HWND c : m_vadFireredControls) ShowWindow(c, vadModelIdx == 1 ? SW_SHOW : SW_HIDE);
}

void TabAdvanced::UpdateVadSubGroup(HWND parent) {
    int vadModelIndex = 0;
    if (g_config.vadModel == L"firered") vadModelIndex = 1;
    HWND vadModelCombo = GetDlgItem(parent, IDC_VAD_MODEL);
    if (vadModelCombo) vadModelIndex = ComboBox_GetCurSel(vadModelCombo);
    ShowVadSubGroup(vadModelIndex);
}

void TabAdvanced::CreateControls(HWND parent) {
    m_controls.clear();
    m_vadFireredControls.clear();

    const int vadY = S(UiStyle::AdvancedVadGroupY);
    HWND vadGroup = CreateWindowW(L"BUTTON", L"Voice Activity Detection", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                  S(UiStyle::GeneralGroupX), vadY, S(UiStyle::GeneralGroupW), S(UiStyle::AdvancedVadGroupH), parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(vadGroup);
    AddAdvancedControl(vadGroup);

    HWND vadEnable = CreateWindowW(L"BUTTON", L"Enable VAD", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                   S(UiStyle::ContentLeft), vadY + S(UiStyle::AdvancedVadEnableOffsetY), S(200), S(UiStyle::CheckH), parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VAD)), GetParentInstance(parent), nullptr);
    ApplyUiFont(vadEnable);
    AddAdvancedControl(vadEnable);

    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), vadY + S(UiStyle::AdvancedVadModelOffsetY + UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"VAD model");
    AddAdvancedControl(control);
    AddAdvancedControl(CreateCombo(parent, IDC_VAD_MODEL, S(UiStyle::InputLeft), vadY + S(UiStyle::AdvancedVadModelOffsetY), S(UiStyle::ComboW), S(UiStyle::ComboH)));

    const int vadGroupParamsY = vadY + S(UiStyle::AdvancedVadParamsGroupOffsetY);
    HWND vadParamsGroup = CreateWindowW(L"BUTTON", L"VAD Parameters", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                        S(UiStyle::GeneralGroupX), vadGroupParamsY, S(UiStyle::GeneralGroupW), S(UiStyle::AdvancedVadParamsGroupH), parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(vadParamsGroup);
    AddAdvancedControl(vadParamsGroup);

    const int row1Y = vadGroupParamsY + S(UiStyle::AdvancedVadParamRowOffset1);
    control = CreateLabel(parent, S(UiStyle::ContentLeft), row1Y + S(UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Threshold");
    AddAdvancedControl(control);
    HWND vadThreshold = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                        S(UiStyle::InputLeft), row1Y, S(100), S(UiStyle::EditH), parent,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VAD_THRESHOLD)), GetParentInstance(parent), nullptr);
    ApplyUiFont(vadThreshold);
    AddAdvancedControl(vadThreshold);
    control = CreateLabel(parent, S(UiStyle::InputLeft) + S(108), row1Y + S(UiStyle::LabelYOffset), S(80), S(UiStyle::LabelH), L"(0.0~1.0)");
    AddAdvancedControl(control);

    const int row2Y = vadGroupParamsY + S(UiStyle::AdvancedVadParamRowOffset2);
    control = CreateLabel(parent, S(UiStyle::ContentLeft), row2Y + S(UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Min silence");
    AddAdvancedControl(control);
    HWND vadMinSilence = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                         S(UiStyle::InputLeft), row2Y, S(100), S(UiStyle::EditH), parent,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VAD_MIN_SILENCE)), GetParentInstance(parent), nullptr);
    ApplyUiFont(vadMinSilence);
    AddAdvancedControl(vadMinSilence);
    control = CreateLabel(parent, S(UiStyle::InputLeft) + S(108), row2Y + S(UiStyle::LabelYOffset), S(80), S(UiStyle::LabelH), L"ms");
    AddAdvancedControl(control);

    control = CreateLabel(parent, S(420), row2Y + S(UiStyle::LabelYOffset), S(100), S(UiStyle::LabelH), L"Pad start");
    AddAdvancedControl(control);
    AddVadFireredControl(control);
    HWND vadPadStart = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                       S(530), row2Y, S(100), S(UiStyle::EditH), parent,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VAD_PAD_START)), GetParentInstance(parent), nullptr);
    ApplyUiFont(vadPadStart);
    AddAdvancedControl(vadPadStart);
    AddVadFireredControl(vadPadStart);
    control = CreateLabel(parent, S(638), row2Y + S(UiStyle::LabelYOffset), S(80), S(UiStyle::LabelH), L"ms");
    AddAdvancedControl(control);
    AddVadFireredControl(control);

    const int row3Y = vadGroupParamsY + S(UiStyle::AdvancedVadParamRowOffset3);
    control = CreateLabel(parent, S(UiStyle::ContentLeft), row3Y + S(UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Min speech");
    AddAdvancedControl(control);
    HWND vadMinSpeech = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                        S(UiStyle::InputLeft), row3Y, S(100), S(UiStyle::EditH), parent,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VAD_MIN_SPEECH)), GetParentInstance(parent), nullptr);
    ApplyUiFont(vadMinSpeech);
    AddAdvancedControl(vadMinSpeech);
    control = CreateLabel(parent, S(UiStyle::InputLeft) + S(108), row3Y + S(UiStyle::LabelYOffset), S(80), S(UiStyle::LabelH), L"ms");
    AddAdvancedControl(control);

    control = CreateLabel(parent, S(420), row3Y + S(UiStyle::LabelYOffset), S(100), S(UiStyle::LabelH), L"Smooth win");
    AddAdvancedControl(control);
    AddVadFireredControl(control);
    HWND vadSmoothWin = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                        S(530), row3Y, S(100), S(UiStyle::EditH), parent,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VAD_SMOOTH_WINDOW)), GetParentInstance(parent), nullptr);
    ApplyUiFont(vadSmoothWin);
    AddAdvancedControl(vadSmoothWin);
    AddVadFireredControl(vadSmoothWin);

    const int diagnosticsY = S(UiStyle::AdvancedDiagnosticsGroupY);
    HWND diagnosticsGroup = CreateWindowW(
        L"BUTTON", L"Diagnostics", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        S(UiStyle::GeneralGroupX), diagnosticsY,
        S(UiStyle::GeneralGroupW), S(UiStyle::AdvancedDiagnosticsGroupH),
        parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(diagnosticsGroup);
    AddAdvancedControl(diagnosticsGroup);

    control = CreateLabel(
        parent, S(UiStyle::ContentLeft),
        diagnosticsY + S(UiStyle::AdvancedDiagnosticsModeOffsetY + UiStyle::LabelYOffset),
        S(UiStyle::AdvancedDiagnosticsModeLabelW), S(UiStyle::LabelH),
        L"Recording diagnostics");
    AddAdvancedControl(control);
    AddAdvancedControl(CreateCombo(
        parent, IDC_DIAGNOSTIC_AUDIO_MODE, S(UiStyle::InputLeft),
        diagnosticsY + S(UiStyle::AdvancedDiagnosticsModeOffsetY),
        S(UiStyle::AdvancedDiagnosticsModeW), S(UiStyle::ComboH)));

    const int diagnosticActionsY =
        diagnosticsY + S(UiStyle::AdvancedDiagnosticsActionsOffsetY);
    AddAdvancedControl(CreateButton(
        parent, IDC_DIAGNOSTIC_AUDIO_OPEN_FOLDER, S(UiStyle::ContentLeft),
        diagnosticActionsY, S(UiStyle::AdvancedDiagnosticsOpenButtonW),
        S(UiStyle::ActionBtnH), L"Open recordings folder"));
    AddAdvancedControl(CreateButton(
        parent, IDC_DIAGNOSTIC_AUDIO_DELETE,
        S(UiStyle::ContentLeft + UiStyle::AdvancedDiagnosticsOpenButtonW +
          UiStyle::AdvancedDiagnosticsButtonGap),
        diagnosticActionsY, S(UiStyle::AdvancedDiagnosticsDeleteButtonW),
        S(UiStyle::ActionBtnH), L"Delete saved recordings..."));

    control = CreateHint(
        parent, S(UiStyle::ContentLeft),
        diagnosticsY + S(UiStyle::AdvancedDiagnosticsHintOffsetY),
        S(UiStyle::AdvancedDiagnosticsHintW), S(UiStyle::LabelH), L"");
    SetWindowLongPtrW(control, GWLP_ID, IDC_DIAGNOSTIC_AUDIO_HINT);
    AddAdvancedControl(control);
}

void TabAdvanced::DestroyControls() {
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
    m_vadFireredControls.clear();
}

void TabAdvanced::Show(bool visible) {
    for (HWND c : m_controls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
    if (visible) {
        UpdateVadSubGroup(m_controls.empty() ? nullptr : GetParent(m_controls[0]));
    } else {
        for (HWND c : m_vadFireredControls) ShowWindow(c, SW_HIDE);
    }
}

void TabAdvanced::LoadControls(HWND parent, const Config& cfg) {
    Button_SetCheck(GetDlgItem(parent, IDC_VAD), cfg.enableVad ? BST_CHECKED : BST_UNCHECKED);

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

    HWND audioDiagCombo = GetDlgItem(parent, IDC_DIAGNOSTIC_AUDIO_MODE);
    if (audioDiagCombo) {
        ComboBox_ResetContent(audioDiagCombo);
        ComboBox_AddString(audioDiagCombo, L"Off (default)");
        ComboBox_AddString(audioDiagCombo, L"Save failed requests only");
        ComboBox_AddString(audioDiagCombo, L"Save all utterances (debug)");
        const std::wstring mode = asr_probe::NormalizeDiagnosticAudioMode(cfg.diagnosticAudioMode);
        int sel = 0;
        if (mode == L"failures") sel = 1;
        else if (mode == L"all") sel = 2;
        ComboBox_SetCurSel(audioDiagCombo, sel);
        UpdateDiagnosticAudioHint(parent);
    }
}

void TabAdvanced::SaveControls(HWND parent, Config& cfg) {
    cfg.enableVad = Button_GetCheck(GetDlgItem(parent, IDC_VAD)) == BST_CHECKED;

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

    cfg.diagnosticAudioMode = DiagnosticAudioModeFromControl(parent);
}

bool TabAdvanced::HandleCommand(HWND hwnd, WORD notifyCode, WORD controlId, HWND control) {
    switch (controlId) {
    case IDC_VAD_MODEL:
        if (notifyCode == CBN_SELCHANGE) {
            int idx = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_VAD_MODEL));
            ShowVadSubGroup(idx);
            return true;
        }
        return false;
    case IDC_DIAGNOSTIC_AUDIO_MODE:
        if (notifyCode == CBN_SELCHANGE) {
            UpdateDiagnosticAudioHint(hwnd);
            return true;
        }
        return false;
    case IDC_DIAGNOSTIC_AUDIO_OPEN_FOLDER:
        OpenDiagnosticAudioFolder(hwnd);
        return true;
    case IDC_DIAGNOSTIC_AUDIO_DELETE:
        DeleteDiagnosticAudioFiles(hwnd);
        return true;
    default:
        return false;
    }
}

void OpenDiagnosticAudioFolder(HWND hwnd) {
    std::wstring error;
    if (!asr_probe::EnsureDiagnosticAudioDir(&error)) {
        MessageBoxW(hwnd, error.c_str(), L"Recording diagnostics",
                    MB_OK | MB_ICONERROR);
        return;
    }
    const std::wstring directory = asr_probe::GetDiagnosticAudioDir();
    const HINSTANCE result = ShellExecuteW(
        hwnd, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(hwnd, L"Unable to open the recordings folder.",
                    L"Recording diagnostics", MB_OK | MB_ICONERROR);
    }
}

void DeleteDiagnosticAudioFiles(HWND hwnd) {
    const int choice = MessageBoxW(
        hwnd,
        L"Delete all recordings and JSON manifests managed by VoxType?\n\n"
        L"Unknown files in the folder will be preserved.",
        L"Delete saved recordings", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (choice != IDYES) return;

    size_t deletedGroups = 0;
    std::wstring error;
    if (!asr_probe::DeleteDiagnosticManagedRecordings(&deletedGroups, &error)) {
        MessageBoxW(hwnd, error.c_str(), L"Delete saved recordings",
                    MB_OK | MB_ICONERROR);
        return;
    }
    SetStatus(hwnd, L"Deleted " + std::to_wstring(deletedGroups) +
                    L" managed recording group(s). Unknown files were preserved.");
}

} // namespace ui_tab
