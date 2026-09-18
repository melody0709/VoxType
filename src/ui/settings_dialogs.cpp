#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "settings_dialogs.h"
#include "settings_controls.h"
#include "hotkey.h"
#include "ui_types.h"
#include "ui_utils.h"
#include "ui_theme.h"

#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
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

HINSTANCE GetParentInstance(HWND parent) {
    if (parent) {
        HINSTANCE h = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
        if (h) return h;
    }
    return GetModuleHandleW(nullptr);
}

std::wstring QwenControlText(HWND hwnd, int id, size_t capacity = 1024) {
    std::wstring value(capacity, L'\0');
    const int length = GetWindowTextW(GetDlgItem(hwnd, id), value.data(), static_cast<int>(value.size()));
    if (length <= 0) return {};
    value.resize(static_cast<size_t>(length));
    return value;
}

struct QwenAdvancedControls {
    HWND vocabIdLabel = nullptr;
    HWND vocabId = nullptr;
    HWND vocabIdHint = nullptr;
    HWND vocabJsonLabel = nullptr;
    HWND vocabJson = nullptr;
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

    if (c.vocabJsonLabel) MoveWindow(c.vocabJsonLabel, S(UiStyle::QwenAdvancedDialogLeft), S(UiStyle::QwenAdvancedDialogVocabJsonLabelY), S(UiStyle::QwenAdvancedDialogLabelW), S(UiStyle::LabelH), TRUE);
    if (c.vocabJson) MoveWindow(c.vocabJson, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogVocabJsonY), S(UiStyle::QwenAdvancedDialogInputW), S(UiStyle::QwenAdvancedDialogVocabJsonH), TRUE);
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
    data.vocabulary = QwenControlText(hwnd, IDC_QWEN_VOCABULARY, 8192);
    data.semanticPunctuation = Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_SEMANTIC_PUNCTUATION)) == BST_CHECKED;
    data.maxSentenceSilence = QwenControlText(hwnd, IDC_QWEN_MAX_SENTENCE_SILENCE, 32);
    data.multiThreshold = Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_MULTI_THRESHOLD)) == BST_CHECKED;
    data.heartbeat = Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_HEARTBEAT)) == BST_CHECKED;
    data.speechNoiseEnabled = Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_SPEECH_NOISE_ENABLE)) == BST_CHECKED;
    data.speechNoiseThreshold = QwenControlText(hwnd, IDC_QWEN_SPEECH_NOISE_THRESHOLD, 32);
    data.continueContext = Button_GetCheck(GetDlgItem(hwnd, IDC_QWEN_CONTINUE_CONTEXT)) == BST_CHECKED;
    data.specialReplace = QwenControlText(hwnd, IDC_QWEN_SPECIAL_REPLACE, 8192);
    data.specialEmpty = QwenControlText(hwnd, IDC_QWEN_SPECIAL_EMPTY, 8192);
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
                    S(UiStyle::QwenAdvancedDialogLabelW), S(UiStyle::LabelH), L"Inline vocabulary JSON");
        state->controls.vocabJson = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE |
                                              ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
                                          S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogVocabJsonY),
                                          S(UiStyle::QwenAdvancedDialogInputW), S(UiStyle::QwenAdvancedDialogVocabJsonH),
                                          hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_VOCABULARY)),
                                          hInst, nullptr);
        ApplyUiFont(state->controls.vocabJson);
        state->controls.vocabJsonHint = CreateHint(hwnd, S(UiStyle::QwenAdvancedDialogInputLeft), S(UiStyle::QwenAdvancedDialogVocabJsonHintY),
                   S(UiStyle::QwenAdvancedDialogInputW), S(UiStyle::QwenHint2LineH),
                   L"Optional. Weights 1–5 or 50; max 2000 entries, max 50 entries at weight 50.");

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
            SetWindowTextW(state->controls.vocabJson, data->vocabulary.c_str());
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

void LayoutVolcExtraDlg(HWND hDlg) {
    const int btnY = S(UiStyle::VolcExtraDlgBtnY);
    const int btnH = S(UiStyle::BtnH);
    const int rightEdge = S(18) + S(UiStyle::VolcExtraDlgEditW);
    HWND edit = GetDlgItem(hDlg, IDC_VOLC_EXTRA_EDIT);
    HWND filterBtn = GetDlgItem(hDlg, IDC_VOLC_EXTRA_HOTWORDS);
    HWND resultBtn = GetDlgItem(hDlg, IDC_VOLC_EXTRA_CONTEXT);
    HWND resetBtn = GetDlgItem(hDlg, IDC_VOLC_EXTRA_RESET);
    HWND okBtn = GetDlgItem(hDlg, IDOK);
    HWND cancelBtn = GetDlgItem(hDlg, IDCANCEL);
    if (edit) MoveWindow(edit, S(18), S(18), S(UiStyle::VolcExtraDlgEditW), S(UiStyle::VolcExtraDlgEditH), TRUE);
    if (filterBtn) MoveWindow(filterBtn, S(18), btnY, S(120), btnH, TRUE);
    if (resultBtn) MoveWindow(resultBtn, S(18) + S(120) + S(12), btnY, S(120), btnH, TRUE);
    if (resetBtn) MoveWindow(resetBtn, S(18) + S(120) + S(12) + S(120) + S(12), btnY, S(100), btnH, TRUE);
    if (okBtn) MoveWindow(okBtn, rightEdge - S(100) - S(12) - S(100), btnY, S(100), btnH, TRUE);
    if (cancelBtn) MoveWindow(cancelBtn, rightEdge - S(100), btnY, S(100), btnH, TRUE);
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

LRESULT CALLBACK VolcExtraWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        UpdateUiScale(hDlg);
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        auto* data = reinterpret_cast<VolcExtraDlgData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
        SetWindowTextW(hDlg, L"Volcengine ASR Extra Params");
        HINSTANCE hInst = GetParentInstance(hDlg);

        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
                                    S(18), S(18), S(UiStyle::VolcExtraDlgEditW), S(UiStyle::VolcExtraDlgEditH), hDlg,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_EXTRA_EDIT)),
                                    hInst, nullptr);
        ApplyUiFont(edit);
        if (data && !data->text.empty()) SetWindowTextW(edit, data->text.c_str());

        const int btnY = S(UiStyle::VolcExtraDlgBtnY);
        const int btnH = S(UiStyle::BtnH);
        HWND filterBtn = CreateWindowW(L"BUTTON", L"Filter", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                      S(18), btnY, S(120), btnH, hDlg,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_EXTRA_HOTWORDS)),
                                      hInst, nullptr);
        HWND resultBtn = CreateWindowW(L"BUTTON", L"Result", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                      S(18) + S(120) + S(12), btnY, S(120), btnH, hDlg,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_EXTRA_CONTEXT)),
                                      hInst, nullptr);
        HWND resetBtn = CreateWindowW(L"BUTTON", L"Reset", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     S(18) + S(120) + S(12) + S(120) + S(12), btnY, S(100), btnH, hDlg,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_EXTRA_RESET)),
                                     hInst, nullptr);

        const int rightEdge = S(18) + S(UiStyle::VolcExtraDlgEditW);
        HWND okBtn = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                                   rightEdge - S(100) - S(12) - S(100), btnY, S(100), btnH, hDlg,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), hInst, nullptr);
        HWND cancelBtn = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                       rightEdge - S(100), btnY, S(100), btnH, hDlg,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), hInst, nullptr);
        ApplyUiFont(filterBtn);
        ApplyUiFont(resultBtn);
        ApplyUiFont(resetBtn);
        ApplyUiFont(okBtn);
        ApplyUiFont(cancelBtn);
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
        LayoutVolcExtraDlg(hDlg);
        InvalidateRect(hDlg, nullptr, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            auto* data = reinterpret_cast<VolcExtraDlgData*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            if (data) {
                HWND edit = GetDlgItem(hDlg, IDC_VOLC_EXTRA_EDIT);
                int len = GetWindowTextLengthW(edit);
                if (len > 0) {
                    std::vector<wchar_t> buf(len + 1);
                    GetWindowTextW(edit, buf.data(), len + 1);
                    data->text = buf.data();
                } else {
                    data->text.clear();
                }
                data->ok = true;
            }
            DestroyWindow(hDlg);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hDlg);
            return 0;
        }
        if (LOWORD(wParam) == IDC_VOLC_EXTRA_HOTWORDS) {
            HWND edit = GetDlgItem(hDlg, IDC_VOLC_EXTRA_EDIT);
            SetWindowTextW(edit, L"\"sensitive_words_filter\":\"system_reserved_filter\"");
            SendMessageW(edit, EM_SETSEL, 0, -1);
            SetFocus(edit);
            return 0;
        }
        if (LOWORD(wParam) == IDC_VOLC_EXTRA_CONTEXT) {
            HWND edit = GetDlgItem(hDlg, IDC_VOLC_EXTRA_EDIT);
            SetWindowTextW(edit, L"\"result_type\":\"single\",\"vad_segment_duration\":3000");
            SendMessageW(edit, EM_SETSEL, 0, -1);
            SetFocus(edit);
            return 0;
        }
        if (LOWORD(wParam) == IDC_VOLC_EXTRA_RESET) {
            SetWindowTextW(GetDlgItem(hDlg, IDC_VOLC_EXTRA_EDIT), L"");
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

bool ShowVolcExtraDialog(HWND parent, std::wstring& out) {
    HINSTANCE hInst = GetParentInstance(parent);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = VolcExtraWndProc;
        wc.hInstance = hInst;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"VoxTypeVolcExtraDlg";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }
    VolcExtraDlgData data;
    data.text = out;
    UpdateUiScale(parent);
    const int width = S(UiStyle::VolcExtraDlgW);
    const int height = S(UiStyle::VolcExtraDlgH);
    POINT pos = CalculateCenteredDialogPos(parent, width, height);

    HWND dlg = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME,
                               L"VoxTypeVolcExtraDlg", L"Volcengine ASR Extra Params",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                               pos.x, pos.y, width, height, parent, nullptr, hInst, &data);
    if (!dlg) return false;
    RunModalDialogLoop(dlg, parent);
    if (data.ok) { out = data.text; return true; }
    return false;
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
