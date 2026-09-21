#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "settings_dialogs.h"
#include "settings_controls.h"
#include "hotkey.h"
#include "ui_types.h"
#include "ui_utils.h"
#include "ui_theme.h"
#include "vocabulary_manager.h"
#include "utils.h"
#include "llm_refine.h"

#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <format>
#include <vector>

namespace {

constexpr COLORREF kBgColor = UiStyle::BgColor;
constexpr COLORREF kControlBgColor = UiStyle::ControlBgColor;
constexpr COLORREF kTextColor = UiStyle::TextColor;
constexpr COLORREF kInputTextColor = UiStyle::InputTextColor;
constexpr COLORREF kHintTextColor = UiStyle::HintTextColor;

constexpr int IDC_QWEN_VOCABULARY_ID = 2099;
constexpr int IDC_QWEN_VOCABULARY = 2120;
constexpr int IDC_QWEN_SEMANTIC_PUNCTUATION = 2121;
constexpr int IDC_QWEN_MAX_SENTENCE_SILENCE = 2122;
constexpr int IDC_QWEN_MULTI_THRESHOLD = 2123;
constexpr int IDC_QWEN_HEARTBEAT = 2124;
constexpr int IDC_QWEN_SPEECH_NOISE_THRESHOLD = 2125;
constexpr int IDC_QWEN_SPEECH_NOISE_ENABLE = 2127;
constexpr int IDC_QWEN_CONTINUE_CONTEXT = 2133;
constexpr int IDC_QWEN_SPECIAL_REPLACE = 2134;
constexpr int IDC_QWEN_SPECIAL_EMPTY = 2135;
constexpr int IDC_QWEN_SYSTEM_FILTER = 2136;
// Dialog-local static labels of the Volcano Engine advanced dialog. The edit and
// checkbox controls reuse the global IDC_VOLC_* ids; only these labels need local
// ids because they are not addressable by name from anywhere else.
constexpr int IDC_VOLC_DLG_HOTWORDS_ID_LABEL = 3220;
constexpr int IDC_VOLC_DLG_HOTWORDS_NAME_LABEL = 3221;
constexpr int IDC_VOLC_DLG_CORRECT_ID_LABEL = 3222;
constexpr int IDC_VOLC_DLG_CORRECT_NAME_LABEL = 3223;
constexpr int IDC_VOLC_DLG_HISTORY_LABEL = 3224;
constexpr int IDC_VOLC_DLG_HISTORY_UNIT = 3225;
constexpr int IDC_VOLC_DLG_END_WINDOW_LABEL = 3226;
constexpr int IDC_VOLC_DLG_END_WINDOW_UNIT = 3227;
constexpr int IDC_VOLC_DLG_FORCE_SPEECH_LABEL = 3228;
constexpr int IDC_VOLC_DLG_FORCE_SPEECH_UNIT = 3229;
constexpr int IDC_VOLC_DLG_JSON_LABEL = 3230;

struct QwenAdvancedControls {
    HWND vocabIdLabel = nullptr;
    HWND vocabId = nullptr;
    HWND vocabIdHint = nullptr;
    HWND vocabJsonLabel = nullptr;
    HWND vocabJsonHint = nullptr;
    HWND streamingGroup = nullptr;
    HWND semantic = nullptr;
    HWND silenceLabel = nullptr;
    HWND silence = nullptr;
    HWND streamingHint1 = nullptr;
    HWND multi = nullptr;
    HWND heartbeat = nullptr;
    HWND streamingHint2 = nullptr;
    HWND noiseEnable = nullptr;
    HWND noise = nullptr;
    HWND noiseHintUnit = nullptr;
    HWND noiseHint = nullptr;
    HWND continueContext = nullptr;
    HWND systemFilter = nullptr;
    HWND continueHint = nullptr;
    HWND specialReplaceLabel = nullptr;
    HWND specialReplace = nullptr;
    HWND specialEmptyLabel = nullptr;
    HWND specialEmpty = nullptr;
    HWND okButton = nullptr;
    HWND cancelButton = nullptr;
};

struct QwenAdvancedDialogState {
    QwenAdvancedDialogData* data = nullptr;
    QwenAdvancedValidator validator = nullptr;
    QwenAdvancedControls controls;
};

void LayoutQwenAdvancedDlg(HWND hwnd, const QwenAdvancedControls& c) {
    if (c.vocabIdLabel) MoveWindow(c.vocabIdLabel, S(UiStyle::QwenAdvancedDialogLeft), S(UiStyle::QwenAdvancedDialogVocabIdLabelY), S(UiStyle::QwenAdvancedDialogLabelW), S(UiStyle::LabelH), TRUE);
    if (c.vocabId) MoveWindow(c.vocabId, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogVocabIdY), S(UiStyle::QwenAdvancedDialogInputW), S(UiStyle::EditH), TRUE);
    if (c.vocabIdHint) MoveWindow(c.vocabIdHint, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogVocabIdHintY), S(UiStyle::QwenAdvancedDialogInputW), S(UiStyle::QwenHint2LineH), TRUE);

    if (c.vocabJsonLabel) MoveWindow(c.vocabJsonLabel, S(UiStyle::QwenAdvancedDialogLeft), S(UiStyle::QwenAdvancedDialogVocabJsonLabelY), S(140), S(UiStyle::LabelH), TRUE);
    if (c.vocabJsonHint) MoveWindow(c.vocabJsonHint, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogVocabJsonHintY), S(UiStyle::QwenAdvancedDialogInputW), S(UiStyle::QwenHint2LineH), TRUE);

    if (c.streamingGroup) MoveWindow(c.streamingGroup, S(UiStyle::QwenAdvancedDialogLeft), S(UiStyle::QwenAdvancedDialogStreamingGroupY), S(UiStyle::QwenAdvancedDialogLabelW), S(UiStyle::QwenAdvancedDialogStreamingGroupH), TRUE);
    if (c.semantic) MoveWindow(c.semantic, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogStreamingRow1Y), S(220), S(UiStyle::CheckH), TRUE);
    if (c.silenceLabel) MoveWindow(c.silenceLabel, S(420), S(UiStyle::QwenAdvancedDialogStreamingRow1Y) + S(4), S(105), S(UiStyle::LabelH), TRUE);
    if (c.silence) MoveWindow(c.silence, S(530), S(UiStyle::QwenAdvancedDialogStreamingRow1Y), S(100), S(UiStyle::EditH), TRUE);
    if (c.streamingHint1) MoveWindow(c.streamingHint1, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogStreamingHint1Y), S(470), S(UiStyle::QwenHint2LineH), TRUE);

    if (c.multi) MoveWindow(c.multi, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogStreamingRow2Y), S(170), S(UiStyle::CheckH), TRUE);
    if (c.heartbeat) MoveWindow(c.heartbeat, S(350), S(UiStyle::QwenAdvancedDialogStreamingRow2Y), S(150), S(UiStyle::CheckH), TRUE);
    if (c.streamingHint2) MoveWindow(c.streamingHint2, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogStreamingHint2Y), S(470), S(UiStyle::QwenHint2LineH), TRUE);

    if (c.noiseEnable) MoveWindow(c.noiseEnable, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogNoiseY), S(220), S(UiStyle::CheckH), TRUE);
    if (c.noise) MoveWindow(c.noise, S(420), S(UiStyle::QwenAdvancedDialogNoiseY), S(100), S(UiStyle::EditH), TRUE);
    if (c.noiseHintUnit) MoveWindow(c.noiseHintUnit, S(530), S(UiStyle::QwenAdvancedDialogNoiseY) + S(4), S(135), S(UiStyle::QwenHintH), TRUE);
    if (c.noiseHint) MoveWindow(c.noiseHint, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogNoiseHintY), S(480), S(UiStyle::QwenHintH), TRUE);

    if (c.continueContext) MoveWindow(c.continueContext, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogContinueY), S(250), S(UiStyle::CheckH), TRUE);
    if (c.systemFilter) MoveWindow(c.systemFilter, S(430), S(UiStyle::QwenAdvancedDialogContinueY), S(230), S(UiStyle::CheckH), TRUE);
    if (c.continueHint) MoveWindow(c.continueHint, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogContinueHintY), S(500), S(UiStyle::QwenHint2LineH), TRUE);

    if (c.specialReplaceLabel) MoveWindow(c.specialReplaceLabel, S(UiStyle::QwenAdvancedDialogLeft), S(UiStyle::QwenAdvancedDialogSpecialLabelY), S(245), S(UiStyle::LabelH), TRUE);
    if (c.specialReplace) MoveWindow(c.specialReplace, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogSpecialY), S(240), S(UiStyle::QwenAdvancedDialogSpecialH), TRUE);

    if (c.specialEmptyLabel) MoveWindow(c.specialEmptyLabel, S(430), S(UiStyle::QwenAdvancedDialogSpecialLabelY), S(250), S(UiStyle::LabelH), TRUE);
    if (c.specialEmpty) MoveWindow(c.specialEmpty, S(430), S(UiStyle::QwenAdvancedDialogSpecialY), S(250), S(UiStyle::QwenAdvancedDialogSpecialH), TRUE);

    if (c.okButton) MoveWindow(c.okButton, S(500), S(UiStyle::QwenAdvancedDialogFooterY), S(UiStyle::FooterBtnW), S(UiStyle::ActionBtnH), TRUE);
    if (c.cancelButton) MoveWindow(c.cancelButton, S(596), S(UiStyle::QwenAdvancedDialogFooterY), S(UiStyle::FooterBtnW), S(UiStyle::ActionBtnH), TRUE);
}

POINT CalculateCenteredDialogPos(HWND parent, int width, int height) {
    RECT work = GetWorkAreaForWindow(parent);
    RECT parentRect = {};
    if (parent) {
        GetWindowRect(parent, &parentRect);
    } else {
        parentRect = work;
    }
    int x = parentRect.left + ((parentRect.right - parentRect.left) - width) / 2;
    int y = parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2;
    const int workLeft = static_cast<int>(work.left);
    const int workTop = static_cast<int>(work.top);
    const int workRight = static_cast<int>(work.right);
    const int workBottom = static_cast<int>(work.bottom);
    x = std::clamp(x, workLeft, (std::max)(workLeft, workRight - width));
    y = std::clamp(y, workTop, (std::max)(workTop, workBottom - height));
    return { x, y };
}

void RunModalDialogLoop(HWND dlg, HWND parent) {
    if (!dlg) return;
    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (dlg && IsDialogMessageW(dlg, &msg)) {
            if (!IsWindow(dlg)) break;
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (!IsWindow(dlg)) break;
    }
    if (parent && !IsWindowEnabled(parent)) {
        EnableWindow(parent, TRUE);
    }
    if (parent) {
        SetActiveWindow(parent);
        SetForegroundWindow(parent);
    }
}

void UpdateQwenAdvancedDialogState(HWND hwnd, bool streaming) {
    const bool semantic = Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_SEMANTIC_PUNCTUATION)) == BST_CHECKED;
    const bool noiseEnabled = Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_SPEECH_NOISE_ENABLE)) == BST_CHECKED;
    EnableWindow(GetDlgItem(hwnd, IDC_QWEN_SEMANTIC_PUNCTUATION), streaming ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_QWEN_MAX_SENTENCE_SILENCE), streaming ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_QWEN_MULTI_THRESHOLD), streaming && !semantic ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_QWEN_HEARTBEAT), streaming ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_QWEN_SPEECH_NOISE_ENABLE), streaming ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_QWEN_SPEECH_NOISE_THRESHOLD),
                 streaming && noiseEnabled ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_QWEN_CONTINUE_CONTEXT), streaming ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_QWEN_SPECIAL_REPLACE), streaming ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_QWEN_SPECIAL_EMPTY), streaming ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_QWEN_SYSTEM_FILTER), streaming ? TRUE : FALSE);
}

bool ReadQwenAdvancedDialog(HWND hwnd, QwenAdvancedDialogData& data, std::wstring& error, QwenAdvancedValidator validator) {
    data.vocabularyId = QwenControlText(hwnd, IDC_QWEN_VOCABULARY_ID, 512);
    // data.vocabulary is managed globally in TabVocabulary (%APPDATA%\VoxType\vocabulary.json)
    data.semanticPunctuation = Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_SEMANTIC_PUNCTUATION)) == BST_CHECKED;
    data.maxSentenceSilence = QwenControlText(hwnd, IDC_QWEN_MAX_SENTENCE_SILENCE, 32);
    data.multiThreshold = Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_MULTI_THRESHOLD)) == BST_CHECKED;
    data.heartbeat = Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_HEARTBEAT)) == BST_CHECKED;
    data.speechNoiseEnabled = Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_SPEECH_NOISE_ENABLE)) == BST_CHECKED;
    data.speechNoiseThreshold = QwenControlText(hwnd, IDC_QWEN_SPEECH_NOISE_THRESHOLD, 32);
    data.continueContext = Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_CONTINUE_CONTEXT)) == BST_CHECKED;
    data.specialReplace = QwenControlText(hwnd, IDC_QWEN_SPECIAL_REPLACE);
    data.specialEmpty = QwenControlText(hwnd, IDC_QWEN_SPECIAL_EMPTY);
    data.systemReservedFilter = Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_SYSTEM_FILTER)) == BST_CHECKED;

    const int silenceMs = _wtoi(data.maxSentenceSilence.c_str());
    if (data.streaming && (silenceMs < 200 || silenceMs > 6000)) {
        error = L"Max sentence silence must be between 200 and 6000 ms.";
        return false;
    }
    if (data.semanticPunctuation && data.multiThreshold) {
        error = L"Semantic punctuation and multi-threshold cannot be enabled together.";
        return false;
    }
    if (data.speechNoiseEnabled) {
        const double threshold = _wtof(data.speechNoiseThreshold.c_str());
        if (threshold < -1.0 || threshold > 1.0) {
            error = L"Speech noise threshold must be between -1.0 and 1.0.";
            return false;
        }
    }
    if (validator) {
        if (!validator(data, error)) return false;
    }
    return true;
}

LRESULT CALLBACK QwenAdvancedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<QwenAdvancedDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    QwenAdvancedDialogData* data = state ? state->data : nullptr;
    static HBRUSH s_bgBrush = CreateSolidBrush(kBgColor);
    static HBRUSH s_ctrlBrush = CreateSolidBrush(kControlBgColor);

    switch (msg) {
    case WM_CREATE: {
        UpdateUiScale(hwnd);
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<QwenAdvancedDialogState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        data = state ? state->data : nullptr;
        SetWindowTextW(hwnd, L"Qwen ASR Advanced Settings");

        HINSTANCE hInst = GetParentInstance(hwnd);
        state->controls.vocabIdLabel = CreateLabel(hwnd, S(UiStyle::QwenAdvancedDialogLeft), S(UiStyle::QwenAdvancedDialogVocabIdLabelY),
                    S(UiStyle::QwenAdvancedDialogLabelW), S(UiStyle::LabelH), L"Vocabulary ID");
        state->controls.vocabId = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                       S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogVocabIdY),
                                       S(UiStyle::QwenAdvancedDialogInputW), S(UiStyle::EditH), hwnd,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_VOCABULARY_ID)),
                                       hInst, nullptr);
        ApplyUiFont(state->controls.vocabId);
        state->controls.vocabIdHint = CreateHint(hwnd, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogVocabIdHintY),
                    S(UiStyle::QwenAdvancedDialogInputW), S(UiStyle::QwenHint2LineH),
                    L"Optional. Its target model must match the selected ASR model.");

        state->controls.vocabJsonLabel = CreateLabel(hwnd, S(UiStyle::QwenAdvancedDialogLeft), S(UiStyle::QwenAdvancedDialogVocabJsonLabelY),
                    S(140), S(UiStyle::LabelH), L"Inline vocabulary");
        state->controls.vocabJsonHint = CreateHint(hwnd, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogVocabJsonHintY),
                   S(UiStyle::QwenAdvancedDialogInputW), S(UiStyle::QwenHint2LineH),
                   L"Managed globally in the top-level \"Vocabulary\" tab (%APPDATA%\\VoxType\\vocabulary.json), shared with Volcano Engine.");

        const wchar_t* groupTitle = data && data->streaming
            ? L"Streaming recognition"
            : L"Streaming recognition (not used by this model)";
        state->controls.streamingGroup = CreateWindowW(L"BUTTON", groupTitle, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                   S(UiStyle::QwenAdvancedDialogLeft), S(UiStyle::QwenAdvancedDialogStreamingGroupY),
                                   S(UiStyle::QwenAdvancedDialogLabelW), S(UiStyle::QwenAdvancedDialogStreamingGroupH),
                                   hwnd, nullptr, hInst, nullptr);
        ApplyUiFont(state->controls.streamingGroup);

        state->controls.semantic = CreateCheckBox(hwnd, IDC_QWEN_SEMANTIC_PUNCTUATION,
                                       S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogStreamingRow1Y),
                                       S(220), S(UiStyle::CheckH), L"Semantic punctuation");
        state->controls.silenceLabel = CreateLabel(hwnd, S(420), S(UiStyle::QwenAdvancedDialogStreamingRow1Y) + S(4),
                    S(105), S(UiStyle::LabelH), L"Max silence (ms)");
        state->controls.silence = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                       S(530), S(UiStyle::QwenAdvancedDialogStreamingRow1Y), S(100), S(UiStyle::EditH),
                                       hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_MAX_SENTENCE_SILENCE)),
                                       hInst, nullptr);
        ApplyUiFont(state->controls.silence);
        state->controls.streamingHint1 = CreateHint(hwnd, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogStreamingHint1Y),
                   S(470), S(UiStyle::QwenHint2LineH),
                   L"Semantic punctuation splits by meaning. Max silence finalizes after 200–6000 ms.");

        state->controls.multi = CreateCheckBox(hwnd, IDC_QWEN_MULTI_THRESHOLD,
                                    S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogStreamingRow2Y),
                                    S(170), S(UiStyle::CheckH), L"Multi-threshold");
        state->controls.heartbeat = CreateCheckBox(hwnd, IDC_QWEN_HEARTBEAT,
                                        S(350), S(UiStyle::QwenAdvancedDialogStreamingRow2Y),
                                        S(150), S(UiStyle::CheckH), L"Heartbeat");
        state->controls.streamingHint2 = CreateHint(hwnd, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogStreamingHint2Y),
                   S(470), S(UiStyle::QwenHint2LineH),
                   L"For noisy audio; can't combine with semantic punctuation. Heartbeat keeps the connection alive.");

        state->controls.noiseEnable = CreateCheckBox(hwnd, IDC_QWEN_SPEECH_NOISE_ENABLE,
                                          S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogNoiseY),
                                          S(220), S(UiStyle::CheckH), L"Speech noise threshold");
        state->controls.noise = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     S(420), S(UiStyle::QwenAdvancedDialogNoiseY), S(100), S(UiStyle::EditH),
                                     hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_SPEECH_NOISE_THRESHOLD)),
                                     hInst, nullptr);
        ApplyUiFont(state->controls.noise);
        state->controls.noiseHintUnit = CreateHint(hwnd, S(530), S(UiStyle::QwenAdvancedDialogNoiseY) + S(4), S(135), S(UiStyle::QwenHintH), L"-1.0 to 1.0");
        state->controls.noiseHint = CreateHint(hwnd, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogNoiseHintY),
                   S(480), S(UiStyle::QwenHintH), L"Only enable for difficult recording environments.");

        state->controls.continueContext = CreateCheckBox(
            hwnd, IDC_QWEN_CONTINUE_CONTEXT,
            S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogContinueY),
            S(250), S(UiStyle::CheckH), L"Refresh context once before finish");
        state->controls.systemFilter = CreateCheckBox(
            hwnd, IDC_QWEN_SYSTEM_FILTER,
            S(430), S(UiStyle::QwenAdvancedDialogContinueY),
            S(230), S(UiStyle::CheckH), L"System reserved filter");
        state->controls.continueHint = CreateHint(hwnd, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogContinueHintY),
                   S(500), S(UiStyle::QwenHint2LineH),
                   L"Audio 3 only. Requires focused-field context.\nReads once in the worker thread; no polling.");

        state->controls.specialReplaceLabel = CreateLabel(hwnd, S(UiStyle::QwenAdvancedDialogLeft), S(UiStyle::QwenAdvancedDialogSpecialLabelY),
                    S(245), S(UiStyle::LabelH), L"Replace words (*) — one per line:");
        state->controls.specialReplace = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
            S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogSpecialY),
            S(240), S(UiStyle::QwenAdvancedDialogSpecialH), hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_SPECIAL_REPLACE)),
            hInst, nullptr);
        ApplyUiFont(state->controls.specialReplace);

        state->controls.specialEmptyLabel = CreateLabel(hwnd, S(430), S(UiStyle::QwenAdvancedDialogSpecialLabelY), S(250), S(UiStyle::LabelH),
                    L"Delete words — 32 total max:");
        state->controls.specialEmpty = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
            S(430), S(UiStyle::QwenAdvancedDialogSpecialY),
            S(250), S(UiStyle::QwenAdvancedDialogSpecialH), hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_SPECIAL_EMPTY)),
            hInst, nullptr);
        ApplyUiFont(state->controls.specialEmpty);

        state->controls.okButton = CreateButton(hwnd, IDOK, S(500), S(UiStyle::QwenAdvancedDialogFooterY),
                                     S(UiStyle::FooterBtnW), S(UiStyle::ActionBtnH), L"OK");
        state->controls.cancelButton = CreateButton(hwnd, IDCANCEL, S(596), S(UiStyle::QwenAdvancedDialogFooterY),
                                         S(UiStyle::FooterBtnW), S(UiStyle::ActionBtnH), L"Cancel");
        SendMessageW(state->controls.okButton, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);

        if (data) {
            SetWindowTextW(state->controls.vocabId, data->vocabularyId.c_str());

            // Vocabulary is managed globally in TabVocabulary (%APPDATA%\VoxType\vocabulary.json)

            Button_SetCheck(state->controls.semantic, data->semanticPunctuation ? BST_CHECKED : BST_UNCHECKED);
            SetWindowTextW(state->controls.silence, data->maxSentenceSilence.c_str());
            Button_SetCheck(state->controls.multi, data->multiThreshold ? BST_CHECKED : BST_UNCHECKED);
            Button_SetCheck(state->controls.heartbeat, data->heartbeat ? BST_CHECKED : BST_UNCHECKED);
            Button_SetCheck(state->controls.noiseEnable, data->speechNoiseEnabled ? BST_CHECKED : BST_UNCHECKED);
            SetWindowTextW(state->controls.noise, data->speechNoiseThreshold.c_str());
            Button_SetCheck(state->controls.continueContext, data->continueContext ? BST_CHECKED : BST_UNCHECKED);
            SetWindowTextW(state->controls.specialReplace, data->specialReplace.c_str());
            SetWindowTextW(state->controls.specialEmpty, data->specialEmpty.c_str());
            Button_SetCheck(state->controls.systemFilter, data->systemReservedFilter ? BST_CHECKED : BST_UNCHECKED);
            UpdateQwenAdvancedDialogState(hwnd, data->streaming);
        }
        SetFocus(state->controls.vocabId);
        return 0;
    }
    case WM_CTLCOLORDLG:
        return reinterpret_cast<LRESULT>(s_bgBrush);
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        SetTextColor(hdc, IsSettingsHint(control) ? kHintTextColor : kTextColor);
        SetBkColor(hdc, kBgColor);
        return reinterpret_cast<LRESULT>(s_bgBrush);
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, kInputTextColor);
        SetBkColor(hdc, kControlBgColor);
        return reinterpret_cast<LRESULT>(s_ctrlBrush);
    }
    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, kBgColor);
        return reinterpret_cast<LRESULT>(s_bgBrush);
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_QWEN_SEMANTIC_PUNCTUATION:
            if (HIWORD(wParam) == BN_CLICKED && data) {
                if (Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_SEMANTIC_PUNCTUATION)) == BST_CHECKED) {
                    Button_SetCheck(GetDlgItem(hwnd, IDC_QWEN_MULTI_THRESHOLD), BST_UNCHECKED);
                }
                UpdateQwenAdvancedDialogState(hwnd, data->streaming);
                return 0;
            }
            break;
        case IDC_QWEN_MULTI_THRESHOLD:
            if (HIWORD(wParam) == BN_CLICKED && data) {
                if (Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_MULTI_THRESHOLD)) == BST_CHECKED) {
                    Button_SetCheck(GetDlgItem(hwnd, IDC_QWEN_SEMANTIC_PUNCTUATION), BST_UNCHECKED);
                }
                UpdateQwenAdvancedDialogState(hwnd, data->streaming);
                return 0;
            }
            break;
        case IDC_QWEN_SPEECH_NOISE_ENABLE:
            if (HIWORD(wParam) == BN_CLICKED && data) {
                UpdateQwenAdvancedDialogState(hwnd, data->streaming);
                return 0;
            }
            break;
        case IDOK:
            if (state && state->data) {
                std::wstring error;
                if (!ReadQwenAdvancedDialog(hwnd, *state->data, error, state->validator)) {
                    MessageBoxW(hwnd, error.c_str(), L"Invalid Qwen Advanced Settings", MB_OK | MB_ICONERROR);
                    return 0;
                }
                state->data->ok = true;
            }
            DestroyWindow(hwnd);
            return 0;
        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_DPICHANGED: {
        const UINT newDpi = HIWORD(wParam);
        UpdateUiScaleForDpi(newDpi);
        RECT* suggested = reinterpret_cast<RECT*>(lParam);
        if (suggested) {
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        HFONT newFont = ui_theme::UiFontForDpi(newDpi);
        EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(newFont));
        if (state) {
            LayoutQwenAdvancedDlg(hwnd, state->controls);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY: {
        HWND parent = GetWindow(hwnd, GW_OWNER);
        if (!parent) parent = GetParent(hwnd);
        if (parent && !IsWindowEnabled(parent)) {
            EnableWindow(parent, TRUE);
            SetForegroundWindow(parent);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void LayoutInputDlg(HWND hDlg) {
    HWND edit = GetDlgItem(hDlg, IDC_INPUT_EDIT);
    HWND okBtn = GetDlgItem(hDlg, IDOK);
    HWND cancelBtn = GetDlgItem(hDlg, IDCANCEL);
    if (edit) MoveWindow(edit, S(18), S(18), S(UiStyle::InputDlgEditW), S(UiStyle::EditH), TRUE);
    if (okBtn) MoveWindow(okBtn, S(18), S(UiStyle::InputDlgBtnY), S(UiStyle::FooterBtnW), S(UiStyle::BtnH), TRUE);
    if (cancelBtn) MoveWindow(cancelBtn, S(18) + S(UiStyle::FooterBtnW) + S(12), S(UiStyle::InputDlgBtnY),
                              S(UiStyle::FooterBtnW), S(UiStyle::BtnH), TRUE);
}

struct VolcAdvancedDialogState {
    VolcAdvancedDialogData* data = nullptr;
    VolcAdvancedValidator validator = nullptr;
    HWND group1 = nullptr;
    HWND group2 = nullptr;
    HWND group3 = nullptr;
};

void UpdateVolcAdvancedDialogState(HWND hDlg, const VolcAdvancedDialogData* data) {
    const bool asyncMode = data && data->mode == L"bigmodel_async";
    const bool nonstreamOn = Button_GetCheck(GetDlgItem(hDlg, IDC_VOLC_ENABLE_NONSTREAM)) == BST_CHECKED;
    const bool fcEnabled =
        (data && data->mode == L"bigmodel_nostream") || (asyncMode && nonstreamOn);
    EnableWindow(GetDlgItem(hDlg, IDC_VOLC_ENABLE_NONSTREAM), asyncMode ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_VOLC_ENABLE_MUSIC_FC), fcEnabled ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_VOLC_ENABLE_POI_FC), fcEnabled ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_VOLC_CONTEXT_HISTORY),
                 Button_GetCheck(GetDlgItem(hDlg, IDC_VOLC_ENABLE_CONTEXT)) == BST_CHECKED ? TRUE : FALSE);
}

bool ReadVolcAdvancedDialog(HWND hDlg, VolcAdvancedDialogData& data, std::wstring& error,
                            VolcAdvancedValidator validator) {
    data.hotwordsId = QwenControlText(hDlg, IDC_VOLC_HOTWORDS_ID, 512);
    data.hotwordsName = QwenControlText(hDlg, IDC_VOLC_HOTWORDS_NAME, 512);
    data.correctTableId = QwenControlText(hDlg, IDC_VOLC_CORRECT_TABLE_ID, 512);
    data.correctTableName = QwenControlText(hDlg, IDC_VOLC_CORRECT_TABLE_NAME, 512);
    data.enableContext = Button_GetCheck(GetDlgItem(hDlg, IDC_VOLC_ENABLE_CONTEXT)) == BST_CHECKED;
    data.contextHistory = QwenControlText(hDlg, IDC_VOLC_CONTEXT_HISTORY, 32);
    data.endWindowSize = QwenControlText(hDlg, IDC_VOLC_END_WINDOW_SIZE, 32);
    data.forceToSpeechTime = QwenControlText(hDlg, IDC_VOLC_FORCE_TO_SPEECH_TIME, 32);
    data.enableDdc = Button_GetCheck(GetDlgItem(hDlg, IDC_VOLC_ENABLE_DDC)) == BST_CHECKED;
    data.enableNonstream = Button_GetCheck(GetDlgItem(hDlg, IDC_VOLC_ENABLE_NONSTREAM)) == BST_CHECKED;
    data.enableMusicFc = Button_GetCheck(GetDlgItem(hDlg, IDC_VOLC_ENABLE_MUSIC_FC)) == BST_CHECKED;
    data.enablePoiFc = Button_GetCheck(GetDlgItem(hDlg, IDC_VOLC_ENABLE_POI_FC)) == BST_CHECKED;
    data.extraParams = QwenControlText(hDlg, IDC_VOLC_EXTRA_PARAMS, 8192);
    if (validator && !validator(data, error)) return false;
    return true;
}

void LayoutVolcAdvancedDlg(HWND hDlg, const VolcAdvancedDialogState& state) {
    const int marginX = S(UiStyle::VolcAdvancedDlgMarginX);
    const int groupW = S(UiStyle::VolcAdvancedDlgGroupW);
    const int labelX = S(UiStyle::VolcAdvancedDlgLabelX);
    const int labelW = S(UiStyle::VolcAdvancedDlgLabelW);
    const int inputX = S(UiStyle::VolcAdvancedDlgInputX);
    const int fieldEditW = S(80);

    if (state.group1) MoveWindow(state.group1, marginX, S(UiStyle::VolcAdvancedDlgGroup1Y), groupW, S(UiStyle::VolcAdvancedDlgGroup1H), TRUE);
    if (state.group2) MoveWindow(state.group2, marginX, S(UiStyle::VolcAdvancedDlgGroup2Y), groupW, S(UiStyle::VolcAdvancedDlgGroup2H), TRUE);
    if (state.group3) MoveWindow(state.group3, marginX, S(UiStyle::VolcAdvancedDlgGroup3Y), groupW, S(UiStyle::VolcAdvancedDlgGroup3H), TRUE);

    const int rowA = S(UiStyle::VolcAdvancedDlgGroup1Y + 24);
    const int rowB = S(UiStyle::VolcAdvancedDlgGroup1Y + 76);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_DLG_HOTWORDS_ID_LABEL), labelX, rowA + S(UiStyle::LabelYOffset), labelW, S(UiStyle::LabelH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_HOTWORDS_ID), inputX, rowA, S(UiStyle::VolcAdvancedDlgIdEditW), S(UiStyle::EditH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_DLG_HOTWORDS_NAME_LABEL), S(UiStyle::VolcAdvancedDlgNameLabelX), rowA + S(UiStyle::LabelYOffset), S(UiStyle::VolcAdvancedDlgNameLabelW), S(UiStyle::LabelH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_HOTWORDS_NAME), S(UiStyle::VolcAdvancedDlgNameEditX), rowA, S(UiStyle::VolcAdvancedDlgNameEditW), S(UiStyle::EditH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_DLG_CORRECT_ID_LABEL), labelX, rowB + S(UiStyle::LabelYOffset), labelW, S(UiStyle::LabelH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_CORRECT_TABLE_ID), inputX, rowB, S(UiStyle::VolcAdvancedDlgIdEditW), S(UiStyle::EditH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_DLG_CORRECT_NAME_LABEL), S(UiStyle::VolcAdvancedDlgNameLabelX), rowB + S(UiStyle::LabelYOffset), S(UiStyle::VolcAdvancedDlgNameLabelW), S(UiStyle::LabelH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_CORRECT_TABLE_NAME), S(UiStyle::VolcAdvancedDlgNameEditX), rowB, S(UiStyle::VolcAdvancedDlgNameEditW), S(UiStyle::EditH), TRUE);

    // History context keeps its switch at the group's text edge so the row reads
    // left to right instead of floating under the ID column.
    const int rowC = S(UiStyle::VolcAdvancedDlgGroup2Y + 26);
    const int rowD = S(UiStyle::VolcAdvancedDlgGroup2Y + 78);
    const int rowE = S(UiStyle::VolcAdvancedDlgGroup2Y + 130);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_ENABLE_CONTEXT), labelX, rowC, S(230), S(UiStyle::CheckH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_DLG_HISTORY_LABEL), labelX + S(240), rowC + S(4), S(100), S(UiStyle::LabelH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_CONTEXT_HISTORY), labelX + S(348), rowC, S(80), S(UiStyle::EditH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_DLG_HISTORY_UNIT), labelX + S(436), rowC + S(4), S(60), S(UiStyle::LabelH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_DLG_END_WINDOW_LABEL), labelX, rowD + S(UiStyle::LabelYOffset), labelW, S(UiStyle::LabelH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_END_WINDOW_SIZE), inputX, rowD, fieldEditW, S(UiStyle::EditH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_DLG_END_WINDOW_UNIT), inputX + S(90), rowD + S(UiStyle::LabelYOffset), S(60), S(UiStyle::LabelH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_DLG_FORCE_SPEECH_LABEL), labelX, rowE + S(UiStyle::LabelYOffset), labelW, S(UiStyle::LabelH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_FORCE_TO_SPEECH_TIME), inputX, rowE, fieldEditW, S(UiStyle::EditH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_DLG_FORCE_SPEECH_UNIT), inputX + S(90), rowE + S(UiStyle::LabelYOffset), S(60), S(UiStyle::LabelH), TRUE);

    // Two switch columns starting at the text edge; 340 design px each keeps the
    // longest label ("Smart music filter (enable_music_fc)") clear of the ticks.
    const int switchW = S(UiStyle::VolcAdvancedDlgSwitchW);
    const int rowF = S(UiStyle::VolcAdvancedDlgGroup3Y + 30);
    const int rowG = S(UiStyle::VolcAdvancedDlgGroup3Y + 70);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_ENABLE_DDC), S(UiStyle::VolcAdvancedDlgSwitchCol1X), rowF, switchW, S(UiStyle::CheckH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_ENABLE_NONSTREAM), S(UiStyle::VolcAdvancedDlgSwitchCol2X), rowF, switchW, S(UiStyle::CheckH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_ENABLE_POI_FC), S(UiStyle::VolcAdvancedDlgSwitchCol1X), rowG, switchW, S(UiStyle::CheckH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_ENABLE_MUSIC_FC), S(UiStyle::VolcAdvancedDlgSwitchCol2X), rowG, switchW, S(UiStyle::CheckH), TRUE);

    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_DLG_JSON_LABEL), marginX, S(UiStyle::VolcAdvancedDlgGroup4Y + UiStyle::VolcAdvancedDlgJsonLabelOffsetY), S(UiStyle::VolcAdvancedDlgJsonLabelW), S(UiStyle::VolcAdvancedDlgJsonLabelH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_EXTRA_HOTWORDS), S(UiStyle::VolcAdvancedDlgSnippetBtnX), S(UiStyle::VolcAdvancedDlgGroup4Y), S(UiStyle::VolcAdvancedDlgSnippetBtnW), S(UiStyle::BtnH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_EXTRA_CONTEXT), S(UiStyle::VolcAdvancedDlgSnippetBtnX + UiStyle::VolcAdvancedDlgSnippetBtnW + 12), S(UiStyle::VolcAdvancedDlgGroup4Y), S(UiStyle::VolcAdvancedDlgSnippetBtnW), S(UiStyle::BtnH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_EXTRA_RESET), S(UiStyle::VolcAdvancedDlgResetBtnX), S(UiStyle::VolcAdvancedDlgGroup4Y), S(UiStyle::VolcAdvancedDlgResetBtnW), S(UiStyle::BtnH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDC_VOLC_EXTRA_PARAMS), marginX, S(UiStyle::VolcAdvancedDlgGroup4Y + UiStyle::VolcAdvancedDlgJsonEditOffsetY), groupW, S(UiStyle::VolcAdvancedDlgJsonEditH), TRUE);

    const int btnY = S(UiStyle::VolcAdvancedDlgBtnY);
    MoveWindow(GetDlgItem(hDlg, IDOK), marginX + groupW - S(212), btnY, S(100), S(UiStyle::ActionBtnH), TRUE);
    MoveWindow(GetDlgItem(hDlg, IDCANCEL), marginX + groupW - S(100), btnY, S(100), S(UiStyle::ActionBtnH), TRUE);
}

LRESULT CALLBACK InputWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        UpdateUiScale(hDlg);
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        auto* data = reinterpret_cast<InputDlgData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
        if (data && data->title) SetWindowTextW(hDlg, data->title);
        HINSTANCE hInst = GetParentInstance(hDlg);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                                    S(18), S(18), S(UiStyle::InputDlgEditW), S(UiStyle::EditH), hDlg,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_INPUT_EDIT)),
                                    hInst, nullptr);
        HWND okBtn = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                                   S(18), S(UiStyle::InputDlgBtnY), S(UiStyle::FooterBtnW), S(UiStyle::BtnH), hDlg,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), hInst, nullptr);
        HWND cancelBtn = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                       S(18) + S(UiStyle::FooterBtnW) + S(12), S(UiStyle::InputDlgBtnY),
                                       S(UiStyle::FooterBtnW), S(UiStyle::BtnH), hDlg,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), hInst, nullptr);
        ApplyUiFont(edit);
        ApplyUiFont(okBtn);
        ApplyUiFont(cancelBtn);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SetFocus(edit);
        return 0;
    }
    case WM_DPICHANGED: {
        const UINT newDpi = HIWORD(wParam);
        UpdateUiScaleForDpi(newDpi);
        RECT* suggested = reinterpret_cast<RECT*>(lParam);
        if (suggested) {
            SetWindowPos(hDlg, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        HFONT newFont = ui_theme::UiFontForDpi(newDpi);
        EnumChildWindows(hDlg, [](HWND child, LPARAM lp) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(newFont));
        LayoutInputDlg(hDlg);
        InvalidateRect(hDlg, nullptr, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            auto* data = reinterpret_cast<InputDlgData*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            if (data) {
                wchar_t buf[256] = {};
                GetWindowTextW(GetDlgItem(hDlg, IDC_INPUT_EDIT), buf, 256);
                data->result = buf;
                data->ok = !data->result.empty();
            }
            DestroyWindow(hDlg);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hDlg);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hDlg);
        return 0;
    case WM_DESTROY: {
        HWND parent = GetWindow(hDlg, GW_OWNER);
        if (!parent) parent = GetParent(hDlg);
        if (parent && !IsWindowEnabled(parent)) {
            EnableWindow(parent, TRUE);
            SetForegroundWindow(parent);
        }
        return 0;
    }
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

LRESULT CALLBACK VolcAdvancedWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<VolcAdvancedDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
    static HBRUSH s_bgBrush = CreateSolidBrush(kBgColor);
    static HBRUSH s_ctrlBrush = CreateSolidBrush(kControlBgColor);

    switch (msg) {
    case WM_CREATE: {
        UpdateUiScale(hDlg);
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<VolcAdvancedDialogState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (!state) return -1;
        SetWindowTextW(hDlg, L"Volcano Engine Advanced Settings");

        const VolcAdvancedDialogData* data = state->data;
        HINSTANCE hInst = GetParentInstance(hDlg);
        const int groupW = S(UiStyle::VolcAdvancedDlgGroupW);
        const int labelW = S(UiStyle::LabelWidth);

        state->group1 = CreateWindowW(L"BUTTON", L"Custom cloud tables", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                      0, 0, 0, 0, hDlg, nullptr, hInst, nullptr);
        ApplyUiFont(state->group1);
        state->group2 = CreateWindowW(L"BUTTON", L"Acoustics & context", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                      0, 0, 0, 0, hDlg, nullptr, hInst, nullptr);
        ApplyUiFont(state->group2);
        state->group3 = CreateWindowW(L"BUTTON", L"Protocol switches", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                      0, 0, 0, 0, hDlg, nullptr, hInst, nullptr);
        ApplyUiFont(state->group3);

        HWND label = CreateLabel(hDlg, 0, 0, labelW, S(UiStyle::LabelH), L"Hotwords ID");
        SetWindowLongPtrW(label, GWLP_ID, IDC_VOLC_DLG_HOTWORDS_ID_LABEL);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                    0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_HOTWORDS_ID)), hInst, nullptr);
        ApplyUiFont(edit);
        label = CreateLabel(hDlg, 0, 0, S(48), S(UiStyle::LabelH), L"Name");
        SetWindowLongPtrW(label, GWLP_ID, IDC_VOLC_DLG_HOTWORDS_NAME_LABEL);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                               0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_HOTWORDS_NAME)), hInst, nullptr);
        ApplyUiFont(edit);

        label = CreateLabel(hDlg, 0, 0, labelW, S(UiStyle::LabelH), L"Correct table ID");
        SetWindowLongPtrW(label, GWLP_ID, IDC_VOLC_DLG_CORRECT_ID_LABEL);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                               0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_CORRECT_TABLE_ID)), hInst, nullptr);
        ApplyUiFont(edit);
        label = CreateLabel(hDlg, 0, 0, S(48), S(UiStyle::LabelH), L"Name");
        SetWindowLongPtrW(label, GWLP_ID, IDC_VOLC_DLG_CORRECT_NAME_LABEL);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                               0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_CORRECT_TABLE_NAME)), hInst, nullptr);
        ApplyUiFont(edit);

        CreateCheckBox(hDlg, IDC_VOLC_ENABLE_CONTEXT, 0, 0, 0, 0, L"Enable history context");
        label = CreateLabel(hDlg, 0, 0, S(100), S(UiStyle::LabelH), L"History turns");
        SetWindowLongPtrW(label, GWLP_ID, IDC_VOLC_DLG_HISTORY_LABEL);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                               0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_CONTEXT_HISTORY)), hInst, nullptr);
        ApplyUiFont(edit);
        label = CreateLabel(hDlg, 0, 0, S(60), S(UiStyle::LabelH), L"turns");
        SetWindowLongPtrW(label, GWLP_ID, IDC_VOLC_DLG_HISTORY_UNIT);

        label = CreateLabel(hDlg, 0, 0, S(150), S(UiStyle::LabelH), L"end_window_size");
        SetWindowLongPtrW(label, GWLP_ID, IDC_VOLC_DLG_END_WINDOW_LABEL);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                               0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_END_WINDOW_SIZE)), hInst, nullptr);
        ApplyUiFont(edit);
        label = CreateLabel(hDlg, 0, 0, S(60), S(UiStyle::LabelH), L"ms");
        SetWindowLongPtrW(label, GWLP_ID, IDC_VOLC_DLG_END_WINDOW_UNIT);

        label = CreateLabel(hDlg, 0, 0, S(150), S(UiStyle::LabelH), L"force_to_speech_time");
        SetWindowLongPtrW(label, GWLP_ID, IDC_VOLC_DLG_FORCE_SPEECH_LABEL);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                               0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_FORCE_TO_SPEECH_TIME)), hInst, nullptr);
        ApplyUiFont(edit);
        label = CreateLabel(hDlg, 0, 0, S(60), S(UiStyle::LabelH), L"ms");
        SetWindowLongPtrW(label, GWLP_ID, IDC_VOLC_DLG_FORCE_SPEECH_UNIT);

        CreateCheckBox(hDlg, IDC_VOLC_ENABLE_DDC, 0, 0, 0, 0, L"DDC smoothing (enable_ddc)");
        CreateCheckBox(hDlg, IDC_VOLC_ENABLE_NONSTREAM, 0, 0, 0, 0, L"Non-stream (enable_nonstream)");
        CreateCheckBox(hDlg, IDC_VOLC_ENABLE_POI_FC, 0, 0, 0, 0, L"Smart POI filter (enable_poi_fc)");
        CreateCheckBox(hDlg, IDC_VOLC_ENABLE_MUSIC_FC, 0, 0, 0, 0, L"Smart music filter (enable_music_fc)");

        label = CreateLabel(hDlg, 0, 0, S(UiStyle::VolcAdvancedDlgJsonLabelW), S(UiStyle::VolcAdvancedDlgJsonLabelH), L"Extra Params (JSON)");
        SetWindowLongPtrW(label, GWLP_ID, IDC_VOLC_DLG_JSON_LABEL);
        CreateButton(hDlg, IDC_VOLC_EXTRA_HOTWORDS, 0, 0, S(UiStyle::VolcAdvancedDlgSnippetBtnW), S(UiStyle::BtnH), L"Filter");
        CreateButton(hDlg, IDC_VOLC_EXTRA_CONTEXT, 0, 0, S(UiStyle::VolcAdvancedDlgSnippetBtnW), S(UiStyle::BtnH), L"Result");
        CreateButton(hDlg, IDC_VOLC_EXTRA_RESET, 0, 0, S(UiStyle::VolcAdvancedDlgResetBtnW), S(UiStyle::BtnH), L"Reset");
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
                               0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_EXTRA_PARAMS)), hInst, nullptr);
        ApplyUiFont(edit);

        HWND okButton = CreateButton(hDlg, IDOK, 0, 0, S(100), S(UiStyle::ActionBtnH), L"OK");
        SendMessageW(okButton, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
        CreateButton(hDlg, IDCANCEL, 0, 0, S(100), S(UiStyle::ActionBtnH), L"Cancel");

        if (data) {
            SetWindowTextW(GetDlgItem(hDlg, IDC_VOLC_HOTWORDS_ID), data->hotwordsId.c_str());
            SetWindowTextW(GetDlgItem(hDlg, IDC_VOLC_HOTWORDS_NAME), data->hotwordsName.c_str());
            SetWindowTextW(GetDlgItem(hDlg, IDC_VOLC_CORRECT_TABLE_ID), data->correctTableId.c_str());
            SetWindowTextW(GetDlgItem(hDlg, IDC_VOLC_CORRECT_TABLE_NAME), data->correctTableName.c_str());
            Button_SetCheck(GetDlgItem(hDlg, IDC_VOLC_ENABLE_CONTEXT), data->enableContext ? BST_CHECKED : BST_UNCHECKED);
            SetWindowTextW(GetDlgItem(hDlg, IDC_VOLC_CONTEXT_HISTORY), data->contextHistory.c_str());
            SetWindowTextW(GetDlgItem(hDlg, IDC_VOLC_END_WINDOW_SIZE), data->endWindowSize.c_str());
            SetWindowTextW(GetDlgItem(hDlg, IDC_VOLC_FORCE_TO_SPEECH_TIME), data->forceToSpeechTime.c_str());
            Button_SetCheck(GetDlgItem(hDlg, IDC_VOLC_ENABLE_DDC), data->enableDdc ? BST_CHECKED : BST_UNCHECKED);
            Button_SetCheck(GetDlgItem(hDlg, IDC_VOLC_ENABLE_NONSTREAM), data->enableNonstream ? BST_CHECKED : BST_UNCHECKED);
            Button_SetCheck(GetDlgItem(hDlg, IDC_VOLC_ENABLE_POI_FC), data->enablePoiFc ? BST_CHECKED : BST_UNCHECKED);
            Button_SetCheck(GetDlgItem(hDlg, IDC_VOLC_ENABLE_MUSIC_FC), data->enableMusicFc ? BST_CHECKED : BST_UNCHECKED);
            SetWindowTextW(GetDlgItem(hDlg, IDC_VOLC_EXTRA_PARAMS), data->extraParams.c_str());
            UpdateVolcAdvancedDialogState(hDlg, data);
        }

        LayoutVolcAdvancedDlg(hDlg, *state);
        SetFocus(GetDlgItem(hDlg, IDC_VOLC_HOTWORDS_ID));
        return 0;
    }
    case WM_CTLCOLORDLG:
        return reinterpret_cast<LRESULT>(s_bgBrush);
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        SetTextColor(hdc, IsSettingsHint(control) ? kHintTextColor : kTextColor);
        SetBkColor(hdc, kBgColor);
        return reinterpret_cast<LRESULT>(s_bgBrush);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, kInputTextColor);
        SetBkColor(hdc, kControlBgColor);
        return reinterpret_cast<LRESULT>(s_ctrlBrush);
    }
    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, kBgColor);
        return reinterpret_cast<LRESULT>(s_bgBrush);
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_VOLC_ENABLE_CONTEXT:
        case IDC_VOLC_ENABLE_NONSTREAM:
            if (HIWORD(wParam) == BN_CLICKED && state) {
                UpdateVolcAdvancedDialogState(hDlg, state->data);
                return 0;
            }
            break;
        case IDC_VOLC_EXTRA_HOTWORDS: {
            HWND edit = GetDlgItem(hDlg, IDC_VOLC_EXTRA_PARAMS);
            SetWindowTextW(edit, L"\"sensitive_words_filter\":\"system_reserved_filter\"");
            SendMessageW(edit, EM_SETSEL, 0, -1);
            SetFocus(edit);
            return 0;
        }
        case IDC_VOLC_EXTRA_CONTEXT: {
            HWND edit = GetDlgItem(hDlg, IDC_VOLC_EXTRA_PARAMS);
            SetWindowTextW(edit, L"\"result_type\":\"single\",\"vad_segment_duration\":3000");
            SendMessageW(edit, EM_SETSEL, 0, -1);
            SetFocus(edit);
            return 0;
        }
        case IDC_VOLC_EXTRA_RESET:
            SetWindowTextW(GetDlgItem(hDlg, IDC_VOLC_EXTRA_PARAMS), L"");
            return 0;
        case IDOK:
            if (state && state->data) {
                std::wstring error;
                if (!ReadVolcAdvancedDialog(hDlg, *state->data, error, state->validator)) {
                    MessageBoxW(hDlg, error.c_str(), L"Invalid Volcano Engine Advanced Settings", MB_OK | MB_ICONERROR);
                    return 0;
                }
                state->data->ok = true;
            }
            DestroyWindow(hDlg);
            return 0;
        case IDCANCEL:
            DestroyWindow(hDlg);
            return 0;
        }
        break;
    case WM_DPICHANGED: {
        const UINT newDpi = HIWORD(wParam);
        UpdateUiScaleForDpi(newDpi);
        RECT* suggested = reinterpret_cast<RECT*>(lParam);
        if (suggested) {
            SetWindowPos(hDlg, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        HFONT newFont = ui_theme::UiFontForDpi(newDpi);
        EnumChildWindows(hDlg, [](HWND child, LPARAM lp) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(newFont));
        if (state) LayoutVolcAdvancedDlg(hDlg, *state);
        InvalidateRect(hDlg, nullptr, TRUE);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hDlg);
        return 0;
    case WM_DESTROY: {
        HWND parent = GetWindow(hDlg, GW_OWNER);
        if (!parent) parent = GetParent(hDlg);
        if (parent && !IsWindowEnabled(parent)) {
            EnableWindow(parent, TRUE);
            SetForegroundWindow(parent);
        }
        return 0;
    }
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

void LayoutPromptManageDlg(HWND hDlg) {
    const int margin = S(UiStyle::PromptDlgMarginX);
    const int labelW = S(UiStyle::PromptDlgPresetLabelW);
    const int comboW = S(UiStyle::PromptDlgPresetComboW);
    const int descW = S(UiStyle::PromptDlgPresetDescW);
    const int presetY = S(UiStyle::PromptDlgPresetY);
    const int editY = S(UiStyle::PromptDlgEditY);
    const int editW = S(UiStyle::PromptDlgEditW);
    const int editH = S(UiStyle::PromptDlgEditH);
    const int btnY = S(UiStyle::PromptDlgBtnY);
    const int btnH = S(UiStyle::BtnH);
    const int resetW = S(UiStyle::PromptDlgResetBtnW);
    const int footerBtnW = S(UiStyle::FooterBtnW);

    HWND presetLabel = GetDlgItem(hDlg, IDC_PROMPT_DLG_LABEL);
    HWND presetCombo = GetDlgItem(hDlg, IDC_PROMPT_DLG_PRESET);
    HWND presetDesc = GetDlgItem(hDlg, IDC_PROMPT_DLG_DESC);
    HWND edit = GetDlgItem(hDlg, IDC_PROMPT_DLG_EDIT);
    HWND resetBtn = GetDlgItem(hDlg, IDC_PROMPT_DLG_RESET);
    HWND okBtn = GetDlgItem(hDlg, IDOK);
    HWND cancelBtn = GetDlgItem(hDlg, IDCANCEL);

    if (presetLabel) MoveWindow(presetLabel, margin, presetY + S(UiStyle::LabelYOffset), labelW, S(UiStyle::LabelH), TRUE);
    if (presetCombo) MoveWindow(presetCombo, margin + labelW + S(8), presetY, comboW, S(200), TRUE);
    if (presetDesc) MoveWindow(presetDesc, margin + labelW + S(8) + comboW + S(16), presetY + S(UiStyle::LabelYOffset), descW, S(UiStyle::LabelH), TRUE);
    if (edit) MoveWindow(edit, margin, editY, editW, editH, TRUE);
    if (resetBtn) MoveWindow(resetBtn, margin, btnY, resetW, btnH, TRUE);
    if (okBtn) MoveWindow(okBtn, margin + editW - footerBtnW * 2 - S(12), btnY, footerBtnW, btnH, TRUE);
    if (cancelBtn) MoveWindow(cancelBtn, margin + editW - footerBtnW, btnY, footerBtnW, btnH, TRUE);
}

LRESULT CALLBACK PromptManageWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        UpdateUiScale(hDlg);
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* data = reinterpret_cast<PromptManageDlgData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
        HINSTANCE hInst = cs->hInstance;

        HWND presetLabel = CreateWindowW(L"STATIC", L"Preset", WS_CHILD | WS_VISIBLE,
                                         0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROMPT_DLG_LABEL)), hInst, nullptr);
        HWND presetCombo = CreateWindowW(WC_COMBOBOXW, nullptr,
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                         0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROMPT_DLG_PRESET)), hInst, nullptr);
        HWND presetDesc = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                        0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROMPT_DLG_DESC)), hInst, nullptr);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
                                    0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROMPT_DLG_EDIT)), hInst, nullptr);
        HWND resetBtn = CreateWindowW(L"BUTTON", L"Reset to Default", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                      0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROMPT_DLG_RESET)), hInst, nullptr);
        HWND okBtn = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                   0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), hInst, nullptr);
        HWND cancelBtn = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                       0, 0, 0, 0, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), hInst, nullptr);

        ApplyUiFont(presetLabel);
        ApplyUiFont(presetCombo);
        ApplyUiFont(presetDesc);
        ApplyUiFont(edit);
        ApplyUiFont(resetBtn);
        ApplyUiFont(okBtn);
        ApplyUiFont(cancelBtn);

        MarkSettingsHint(presetDesc);

        for (int i = 0; i < llm::kPromptPresetCount; ++i) {
            ComboBox_AddString(presetCombo, llm::kPromptPresets[i].name);
        }
        ComboBox_AddString(presetCombo, L"Custom");

        int matchedPreset = -1;
        if (data) {
            for (int i = 0; i < llm::kPromptPresetCount; ++i) {
                if (data->prompt == llm::kPromptPresets[i].prompt) {
                    matchedPreset = i;
                    break;
                }
            }
        }

        if (matchedPreset >= 0) {
            ComboBox_SetCurSel(presetCombo, matchedPreset);
            SetWindowTextW(presetDesc, llm::kPromptPresets[matchedPreset].description);
            if (data) {
                SetWindowTextW(edit, data->prompt.c_str());
                if (data->customBackup.empty()) {
                    data->customBackup = data->prompt.empty() ? llm::kPromptPresets[0].prompt : data->prompt;
                }
            }
            SendMessageW(edit, EM_SETREADONLY, TRUE, 0);
        } else {
            ComboBox_SetCurSel(presetCombo, llm::kPromptPresetCount);
            SetWindowTextW(presetDesc, L"Custom prompt");
            if (data) {
                SetWindowTextW(edit, data->prompt.c_str());
                data->customBackup = data->prompt;
            }
            SendMessageW(edit, EM_SETREADONLY, FALSE, 0);
        }

        LayoutPromptManageDlg(hDlg);
        SetFocus(edit);
        return 0;
    }
    case WM_DPICHANGED: {
        const UINT newDpi = HIWORD(wParam);
        UpdateUiScaleForDpi(newDpi);
        RECT* suggested = reinterpret_cast<RECT*>(lParam);
        if (suggested) {
            SetWindowPos(hDlg, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        HFONT newFont = ui_theme::UiFontForDpi(newDpi);
        EnumChildWindows(hDlg, [](HWND child, LPARAM lp) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(newFont));
        LayoutPromptManageDlg(hDlg);
        InvalidateRect(hDlg, nullptr, TRUE);
        return 0;
    }
    case WM_CTLCOLORDLG:
        return reinterpret_cast<LRESULT>(ui_theme::SettingsBgBrush());
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        if (control == GetDlgItem(hDlg, IDC_PROMPT_DLG_EDIT)) {
            SetTextColor(hdc, kInputTextColor);
            SetBkColor(hdc, kControlBgColor);
            return reinterpret_cast<LRESULT>(ui_theme::ControlBgBrush());
        }
        SetTextColor(hdc, IsSettingsHint(control) ? kHintTextColor : kTextColor);
        SetBkColor(hdc, kBgColor);
        return reinterpret_cast<LRESULT>(ui_theme::SettingsBgBrush());
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, kInputTextColor);
        SetBkColor(hdc, kControlBgColor);
        return reinterpret_cast<LRESULT>(ui_theme::ControlBgBrush());
    }
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        FillRect(reinterpret_cast<HDC>(wParam), &rc, ui_theme::SettingsBgBrush());
        return 1;
    }
    case WM_COMMAND: {
        auto* data = reinterpret_cast<PromptManageDlgData*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
        const WORD controlId = LOWORD(wParam);
        const WORD notifyCode = HIWORD(wParam);

        if (controlId == IDC_PROMPT_DLG_PRESET && notifyCode == CBN_SELCHANGE) {
            HWND combo = GetDlgItem(hDlg, IDC_PROMPT_DLG_PRESET);
            HWND edit = GetDlgItem(hDlg, IDC_PROMPT_DLG_EDIT);
            HWND desc = GetDlgItem(hDlg, IDC_PROMPT_DLG_DESC);
            int sel = ComboBox_GetCurSel(combo);
            if (sel >= 0 && sel < llm::kPromptPresetCount) {
                if (data && (GetWindowLongPtrW(edit, GWL_STYLE) & ES_READONLY) == 0) {
                    int len = GetWindowTextLengthW(edit);
                    std::wstring text(len + 1, L'\0');
                    GetWindowTextW(edit, text.data(), len + 1);
                    text.resize(len);
                    data->customBackup = text;
                }
                SetWindowTextW(edit, llm::kPromptPresets[sel].prompt);
                SetWindowTextW(desc, llm::kPromptPresets[sel].description);
                SendMessageW(edit, EM_SETREADONLY, TRUE, 0);
            } else if (sel == llm::kPromptPresetCount) {
                if (data) {
                    if (data->customBackup.empty()) {
                        data->customBackup = data->prompt.empty() ? llm::kPromptPresets[0].prompt : data->prompt;
                    }
                    SetWindowTextW(edit, data->customBackup.c_str());
                }
                SetWindowTextW(desc, L"Custom prompt");
                SendMessageW(edit, EM_SETREADONLY, FALSE, 0);
                SetFocus(edit);
            }
            return 0;
        }
        if (controlId == IDC_PROMPT_DLG_RESET) {
            HWND combo = GetDlgItem(hDlg, IDC_PROMPT_DLG_PRESET);
            HWND edit = GetDlgItem(hDlg, IDC_PROMPT_DLG_EDIT);
            HWND desc = GetDlgItem(hDlg, IDC_PROMPT_DLG_DESC);
            if (data && (GetWindowLongPtrW(edit, GWL_STYLE) & ES_READONLY) == 0) {
                int len = GetWindowTextLengthW(edit);
                std::wstring text(len + 1, L'\0');
                GetWindowTextW(edit, text.data(), len + 1);
                text.resize(len);
                data->customBackup = text;
            }
            ComboBox_SetCurSel(combo, 0);
            SetWindowTextW(edit, llm::kPromptPresets[0].prompt);
            SetWindowTextW(desc, llm::kPromptPresets[0].description);
            SendMessageW(edit, EM_SETREADONLY, TRUE, 0);
            SetFocus(edit);
            return 0;
        }
        if (controlId == IDOK) {
            if (data) {
                HWND edit = GetDlgItem(hDlg, IDC_PROMPT_DLG_EDIT);
                int len = GetWindowTextLengthW(edit);
                std::wstring text(len + 1, L'\0');
                GetWindowTextW(edit, text.data(), len + 1);
                text.resize(len);
                data->prompt = text;
                if ((GetWindowLongPtrW(edit, GWL_STYLE) & ES_READONLY) == 0) {
                    data->customBackup = text;
                }
                data->ok = true;
            }
            DestroyWindow(hDlg);
            return 0;
        }
        if (controlId == IDCANCEL) {
            DestroyWindow(hDlg);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hDlg);
        return 0;
    case WM_DESTROY: {
        HWND parent = GetWindow(hDlg, GW_OWNER);
        if (!parent) parent = GetParent(hDlg);
        if (parent && !IsWindowEnabled(parent)) {
            EnableWindow(parent, TRUE);
            SetForegroundWindow(parent);
        }
        return 0;
    }
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

}  // namespace

bool ShowInputDialog(HWND parent, const wchar_t* title, std::wstring& out) {
    HINSTANCE hInst = GetParentInstance(parent);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = InputWndProc;
        wc.hInstance = hInst;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"VoxTypeInputDlg";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }
    InputDlgData data;
    data.title = title;
    UpdateUiScale(parent);
    const int width = S(UiStyle::InputDlgW);
    const int height = S(UiStyle::InputDlgH);
    POINT pos = CalculateCenteredDialogPos(parent, width, height);

    HWND dlg = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME,
                               L"VoxTypeInputDlg", title,
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                               pos.x, pos.y, width, height, parent, nullptr, hInst, &data);
    if (!dlg) return false;
    RunModalDialogLoop(dlg, parent);
    if (data.ok) { out = data.result; return true; }
    return false;
}

bool ShowVolcAdvancedDialog(HWND parent, VolcAdvancedDialogData& data, VolcAdvancedValidator validator) {
    HINSTANCE hInst = GetParentInstance(parent);
    static bool registered = false;
    static HBRUSH s_dialogBgBrush = CreateSolidBrush(kBgColor);
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = VolcAdvancedWndProc;
        wc.hInstance = hInst;
        wc.hbrBackground = s_dialogBgBrush;
        wc.lpszClassName = L"VoxTypeVolcAdvancedDlg";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    UpdateUiScale(parent);
    data.ok = false;
    const int width = S(UiStyle::VolcAdvancedDialogW);
    const int height = S(UiStyle::VolcAdvancedDialogH);
    POINT pos = CalculateCenteredDialogPos(parent, width, height);

    VolcAdvancedDialogState state;
    state.data = &data;
    state.validator = validator;

    HWND dlg = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME,
                               L"VoxTypeVolcAdvancedDlg", L"Volcano Engine Advanced Settings",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                               pos.x, pos.y, width, height, parent, nullptr, hInst, &state);
    if (!dlg) return false;
    RunModalDialogLoop(dlg, parent);
    return data.ok;
}

bool ShowQwenAdvancedDialog(HWND parent, QwenAdvancedDialogData& data, QwenAdvancedValidator validator) {
    HINSTANCE hInst = GetParentInstance(parent);
    static bool registered = false;
    static HBRUSH s_dialogBgBrush = CreateSolidBrush(kBgColor);
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = QwenAdvancedWndProc;
        wc.hInstance = hInst;
        wc.hbrBackground = s_dialogBgBrush;
        wc.lpszClassName = L"VoxTypeQwenAdvancedDlg";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    UpdateUiScale(parent);
    data.ok = false;
    const int width = S(UiStyle::QwenAdvancedDialogW);
    const int height = S(UiStyle::QwenAdvancedDialogH);
    POINT pos = CalculateCenteredDialogPos(parent, width, height);

    QwenAdvancedDialogState state;
    state.data = &data;
    state.validator = validator;

    HWND dialog = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME,
                                  L"VoxTypeQwenAdvancedDlg", L"Qwen ASR Advanced Settings",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                  pos.x, pos.y, width, height, parent, nullptr, hInst, &state);
    if (!dialog) return false;
    RunModalDialogLoop(dialog, parent);
    return data.ok;
}

bool ShowPromptManageDialog(HWND parent, std::wstring& outPrompt, std::wstring* customBackup) {
    HINSTANCE hInst = GetParentInstance(parent);
    static bool registered = false;
    static HBRUSH s_dialogBgBrush = CreateSolidBrush(kBgColor);
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = PromptManageWndProc;
        wc.hInstance = hInst;
        wc.hbrBackground = s_dialogBgBrush;
        wc.lpszClassName = L"VoxTypePromptManageDlg";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    PromptManageDlgData data;
    data.prompt = outPrompt;
    if (customBackup && !customBackup->empty()) {
        data.customBackup = *customBackup;
    }
    UpdateUiScale(parent);
    const int width = S(UiStyle::PromptDlgW);
    const int height = S(UiStyle::PromptDlgH);
    POINT pos = CalculateCenteredDialogPos(parent, width, height);

    HWND dlg = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME,
                               L"VoxTypePromptManageDlg", L"System Prompt Management",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                               pos.x, pos.y, width, height, parent, nullptr, hInst, &data);
    if (!dlg) return false;
    RunModalDialogLoop(dlg, parent);
    if (data.ok) {
        outPrompt = data.prompt;
        if (customBackup && !data.customBackup.empty()) {
            *customBackup = data.customBackup;
        }
        return true;
    }
    return false;
}

