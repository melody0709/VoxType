#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "settings_dialogs.h"
#include "settings_controls.h"
#include "hotkey.h"
#include "ui_utils.h"

#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <vector>

namespace {

constexpr COLORREF kBgColor = RGB(246, 248, 251);
constexpr COLORREF kControlBgColor = RGB(255, 255, 255);
constexpr COLORREF kTextColor = RGB(30, 41, 59);
constexpr COLORREF kInputTextColor = RGB(17, 24, 39);
constexpr COLORREF kHintTextColor = RGB(120, 130, 145);

constexpr int kQwenAdvancedDialogW = 800;
constexpr int kQwenAdvancedDialogH = 830;
constexpr int kQwenAdvancedDialogLeft = 24;
constexpr int kQwenAdvancedDialogLabelW = 620;
constexpr int kQwenAdvancedDialogInputLeft = 170;
constexpr int kQwenAdvancedDialogInputW = 500;
constexpr int kQwenAdvancedDialogVocabIdLabelY = 28;
constexpr int kQwenAdvancedDialogVocabIdY = 56;
constexpr int kQwenAdvancedDialogVocabIdHintY = 92;
constexpr int kQwenHint2LineH = 34;
constexpr int kQwenAdvancedDialogVocabJsonLabelY = 146;
constexpr int kQwenAdvancedDialogVocabJsonY = 174;
constexpr int kQwenAdvancedDialogVocabJsonH = 96;
constexpr int kQwenAdvancedDialogVocabJsonHintY = 276;
constexpr int kQwenAdvancedDialogStreamingGroupY = 330;
constexpr int kQwenAdvancedDialogStreamingGroupH = 430;
constexpr int kQwenAdvancedDialogStreamingRow1Y = 362;
constexpr int kQwenAdvancedDialogStreamingHint1Y = 396;
constexpr int kQwenAdvancedDialogStreamingRow2Y = 448;
constexpr int kQwenAdvancedDialogStreamingHint2Y = 482;
constexpr int kQwenAdvancedDialogNoiseY = 534;
constexpr int kQwenHintH = 18;
constexpr int kQwenAdvancedDialogNoiseHintY = 568;
constexpr int kQwenAdvancedDialogContinueY = 604;
constexpr int kQwenAdvancedDialogContinueHintY = 632;
constexpr int kQwenAdvancedDialogSpecialLabelY = 682;
constexpr int kQwenAdvancedDialogSpecialY = 710;
constexpr int kQwenAdvancedDialogSpecialH = 42;
constexpr int kQwenAdvancedDialogFooterY = 764;
constexpr int kLabelH = 20;
constexpr int kEditH = 24;
constexpr int kCheckH = 20;
constexpr int kActionBtnH = 28;
constexpr int kFooterBtnW = 84;

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

struct QwenAdvancedDialogState {
    QwenAdvancedDialogData* data = nullptr;
    QwenAdvancedValidator validator = nullptr;
};

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
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<QwenAdvancedDialogState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        data = state ? state->data : nullptr;
        SetWindowTextW(hwnd, L"Qwen ASR Advanced Settings");

        HINSTANCE hInst = GetParentInstance(hwnd);
        CreateLabel(hwnd, S(kQwenAdvancedDialogLeft), S(kQwenAdvancedDialogVocabIdLabelY),
                    S(kQwenAdvancedDialogLabelW), S(kLabelH), L"Vocabulary ID");
        HWND vocabId = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                       S(kQwenAdvancedDialogInputLeft), S(kQwenAdvancedDialogVocabIdY),
                                       S(kQwenAdvancedDialogInputW), S(kEditH), hwnd,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_VOCABULARY_ID)),
                                       hInst, nullptr);
        ApplyUiFont(vocabId);
        CreateHint(hwnd, S(kQwenAdvancedDialogInputLeft), S(kQwenAdvancedDialogVocabIdHintY),
                   S(kQwenAdvancedDialogInputW), S(kQwenHint2LineH),
                   L"Optional. Its target model must match the selected ASR model.");

        CreateLabel(hwnd, S(kQwenAdvancedDialogLeft), S(kQwenAdvancedDialogVocabJsonLabelY),
                    S(kQwenAdvancedDialogLabelW), S(kLabelH), L"Inline vocabulary JSON");
        HWND vocabulary = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE |
                                              ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
                                          S(kQwenAdvancedDialogInputLeft), S(kQwenAdvancedDialogVocabJsonY),
                                          S(kQwenAdvancedDialogInputW), S(kQwenAdvancedDialogVocabJsonH),
                                          hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_VOCABULARY)),
                                          hInst, nullptr);
        ApplyUiFont(vocabulary);
        CreateHint(hwnd, S(kQwenAdvancedDialogInputLeft), S(kQwenAdvancedDialogVocabJsonHintY),
                   S(kQwenAdvancedDialogInputW), S(kQwenHint2LineH),
                   L"Optional. Weights 1–5 or 50; max 2000 entries, max 50 entries at weight 50.");

        const wchar_t* groupTitle = data && data->streaming
            ? L"Streaming recognition"
            : L"Streaming recognition (not used by this model)";
        HWND group = CreateWindowW(L"BUTTON", groupTitle, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                   S(kQwenAdvancedDialogLeft), S(kQwenAdvancedDialogStreamingGroupY),
                                   S(kQwenAdvancedDialogLabelW), S(kQwenAdvancedDialogStreamingGroupH),
                                   hwnd, nullptr, hInst, nullptr);
        ApplyUiFont(group);

        HWND semantic = CreateCheckBox(hwnd, IDC_QWEN_SEMANTIC_PUNCTUATION,
                                       S(kQwenAdvancedDialogInputLeft), S(kQwenAdvancedDialogStreamingRow1Y),
                                       S(220), S(kCheckH), L"Semantic punctuation");
        CreateLabel(hwnd, S(420), S(kQwenAdvancedDialogStreamingRow1Y) + S(4),
                    S(105), S(kLabelH), L"Max silence (ms)");
        HWND silence = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                       S(530), S(kQwenAdvancedDialogStreamingRow1Y), S(100), S(kEditH),
                                       hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_MAX_SENTENCE_SILENCE)),
                                       hInst, nullptr);
        ApplyUiFont(silence);
        CreateHint(hwnd, S(kQwenAdvancedDialogInputLeft), S(kQwenAdvancedDialogStreamingHint1Y),
                   S(470), S(kQwenHint2LineH),
                   L"Semantic punctuation splits by meaning. Max silence finalizes after 200–6000 ms.");

        HWND multi = CreateCheckBox(hwnd, IDC_QWEN_MULTI_THRESHOLD,
                                    S(kQwenAdvancedDialogInputLeft), S(kQwenAdvancedDialogStreamingRow2Y),
                                    S(170), S(kCheckH), L"Multi-threshold");
        HWND heartbeat = CreateCheckBox(hwnd, IDC_QWEN_HEARTBEAT,
                                        S(350), S(kQwenAdvancedDialogStreamingRow2Y),
                                        S(150), S(kCheckH), L"Heartbeat");
        CreateHint(hwnd, S(kQwenAdvancedDialogInputLeft), S(kQwenAdvancedDialogStreamingHint2Y),
                   S(470), S(kQwenHint2LineH),
                   L"For noisy audio; can't combine with semantic punctuation. Heartbeat keeps the connection alive.");

        HWND noiseEnable = CreateCheckBox(hwnd, IDC_QWEN_SPEECH_NOISE_ENABLE,
                                          S(kQwenAdvancedDialogInputLeft), S(kQwenAdvancedDialogNoiseY),
                                          S(220), S(kCheckH), L"Speech noise threshold");
        HWND noise = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     S(420), S(kQwenAdvancedDialogNoiseY), S(100), S(kEditH),
                                     hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_SPEECH_NOISE_THRESHOLD)),
                                     hInst, nullptr);
        ApplyUiFont(noise);
        CreateHint(hwnd, S(530), S(kQwenAdvancedDialogNoiseY) + S(4), S(135), S(kQwenHintH), L"-1.0 to 1.0");
        CreateHint(hwnd, S(kQwenAdvancedDialogInputLeft), S(kQwenAdvancedDialogNoiseHintY),
                   S(480), S(kQwenHintH), L"Only enable for difficult recording environments.");

        HWND continueContext = CreateCheckBox(
            hwnd, IDC_QWEN_CONTINUE_CONTEXT,
            S(kQwenAdvancedDialogInputLeft), S(kQwenAdvancedDialogContinueY),
            S(250), S(kCheckH), L"Refresh context once before finish");
        HWND systemFilter = CreateCheckBox(
            hwnd, IDC_QWEN_SYSTEM_FILTER,
            S(430), S(kQwenAdvancedDialogContinueY),
            S(230), S(kCheckH), L"System reserved filter");
        CreateHint(hwnd, S(kQwenAdvancedDialogInputLeft), S(kQwenAdvancedDialogContinueHintY),
                   S(500), S(kQwenHint2LineH),
                   L"Audio 3 only. Requires focused-field context.\nReads once in the worker thread; no polling.");

        CreateLabel(hwnd, S(kQwenAdvancedDialogLeft), S(kQwenAdvancedDialogSpecialLabelY),
                    S(245), S(kLabelH), L"Replace words (*) — one per line:");
        HWND specialReplace = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
            S(kQwenAdvancedDialogInputLeft), S(kQwenAdvancedDialogSpecialY),
            S(240), S(kQwenAdvancedDialogSpecialH), hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_SPECIAL_REPLACE)),
            hInst, nullptr);
        ApplyUiFont(specialReplace);

        CreateLabel(hwnd, S(430), S(kQwenAdvancedDialogSpecialLabelY), S(250), S(kLabelH),
                    L"Delete words — 32 total max:");
        HWND specialEmpty = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
            S(430), S(kQwenAdvancedDialogSpecialY),
            S(250), S(kQwenAdvancedDialogSpecialH), hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_QWEN_SPECIAL_EMPTY)),
            hInst, nullptr);
        ApplyUiFont(specialEmpty);

        HWND okButton = CreateButton(hwnd, IDOK, S(500), S(kQwenAdvancedDialogFooterY),
                                     S(kFooterBtnW), S(kActionBtnH), L"OK");
        CreateButton(hwnd, IDCANCEL, S(596), S(kQwenAdvancedDialogFooterY),
                     S(kFooterBtnW), S(kActionBtnH), L"Cancel");
        SendMessageW(okButton, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);

        if (data) {
            SetWindowTextW(vocabId, data->vocabularyId.c_str());
            SetWindowTextW(vocabulary, data->vocabulary.c_str());
            Button_SetCheck(semantic, data->semanticPunctuation ? BST_CHECKED : BST_UNCHECKED);
            SetWindowTextW(silence, data->maxSentenceSilence.c_str());
            Button_SetCheck(multi, data->multiThreshold ? BST_CHECKED : BST_UNCHECKED);
            Button_SetCheck(heartbeat, data->heartbeat ? BST_CHECKED : BST_UNCHECKED);
            Button_SetCheck(noiseEnable, data->speechNoiseEnabled ? BST_CHECKED : BST_UNCHECKED);
            SetWindowTextW(noise, data->speechNoiseThreshold.c_str());
            Button_SetCheck(continueContext, data->continueContext ? BST_CHECKED : BST_UNCHECKED);
            SetWindowTextW(specialReplace, data->specialReplace.c_str());
            SetWindowTextW(specialEmpty, data->specialEmpty.c_str());
            Button_SetCheck(systemFilter, data->systemReservedFilter ? BST_CHECKED : BST_UNCHECKED);
            UpdateQwenAdvancedDialogState(hwnd, data->streaming);
        }
        SetFocus(vocabId);
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
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK InputWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        auto* data = reinterpret_cast<InputDlgData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
        if (data && data->title) SetWindowTextW(hDlg, data->title);
        HINSTANCE hInst = GetParentInstance(hDlg);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                        12, 12, 260, 28, hDlg,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_INPUT_EDIT)),
                        hInst, nullptr);
        HWND okBtn = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                                   12, 50, 80, 28, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                      104, 50, 80, 28, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), hInst, nullptr);
        ApplyUiFont(GetDlgItem(hDlg, IDC_INPUT_EDIT));
        ApplyUiFont(okBtn);
        ApplyUiFont(GetDlgItem(hDlg, IDCANCEL));
        SendMessageW(GetDlgItem(hDlg, IDC_INPUT_EDIT), EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(hDlg, IDC_INPUT_EDIT));
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
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

LRESULT CALLBACK VolcExtraWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        auto* data = reinterpret_cast<VolcExtraDlgData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
        SetWindowTextW(hDlg, L"Volcengine ASR Extra Params");
        HINSTANCE hInst = GetParentInstance(hDlg);

        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
                                    12, 12, 500, 280, hDlg,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_EXTRA_EDIT)),
                                    hInst, nullptr);
        ApplyUiFont(edit);
        if (data && !data->text.empty()) SetWindowTextW(edit, data->text.c_str());

        CreateWindowW(L"BUTTON", L"Filter", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                      12, 302, 90, 28, hDlg,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_EXTRA_HOTWORDS)),
                      hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Result", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                      110, 302, 90, 28, hDlg,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_EXTRA_CONTEXT)),
                      hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Reset", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                      208, 302, 80, 28, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_EXTRA_RESET)),
                      hInst, nullptr);

        HWND okBtn = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                                   340, 302, 80, 28, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                      430, 302, 80, 28, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), hInst, nullptr);
        ApplyUiFont(okBtn);
        ApplyUiFont(GetDlgItem(hDlg, IDCANCEL));
        ApplyUiFont(GetDlgItem(hDlg, IDC_VOLC_EXTRA_HOTWORDS));
        ApplyUiFont(GetDlgItem(hDlg, IDC_VOLC_EXTRA_CONTEXT));
        ApplyUiFont(GetDlgItem(hDlg, IDC_VOLC_EXTRA_RESET));
        SetFocus(edit);
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
    case WM_DESTROY:
        return 0;
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
    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = 296, h = 130;
    int x = work.left + (work.right - work.left - w) / 2;
    int y = work.top + (work.bottom - work.top - h) / 2;
    HWND dlg = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME,
                               L"VoxTypeInputDlg", title,
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                               x, y, w, h, parent, nullptr, hInst, &data);
    if (!dlg) return false;
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (!IsWindow(dlg)) break;
    }
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
    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = 540, h = 420;
    int x = work.left + (work.right - work.left - w) / 2;
    int y = work.top + (work.bottom - work.top - h) / 2;
    HWND dlg = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME,
                               L"VoxTypeVolcExtraDlg", L"Volcengine ASR Extra Params",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                               x, y, w, h, parent, nullptr, hInst, &data);
    if (!dlg) return false;
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (!IsWindow(dlg)) break;
    }
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

    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT parentRect = {};
    GetWindowRect(parent, &parentRect);
    const int width = S(kQwenAdvancedDialogW);
    const int height = S(kQwenAdvancedDialogH);
    int x = parentRect.left + ((parentRect.right - parentRect.left) - width) / 2;
    int y = parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2;
    const int workLeft = static_cast<int>(work.left);
    const int workTop = static_cast<int>(work.top);
    const int workRight = static_cast<int>(work.right);
    const int workBottom = static_cast<int>(work.bottom);
    x = std::clamp(x, workLeft, (std::max)(workLeft, workRight - width));
    y = std::clamp(y, workTop, (std::max)(workTop, workBottom - height));

    QwenAdvancedDialogState state;
    state.data = &data;
    state.validator = validator;

    HWND dialog = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME,
                                  L"VoxTypeQwenAdvancedDlg", L"Qwen ASR Advanced Settings",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                  x, y, width, height, parent, nullptr, hInst, &state);
    if (!dialog) return false;
    EnableWindow(parent, FALSE);
    ShowWindow(dialog, SW_SHOW);
    SetForegroundWindow(dialog);
    MSG msg = {};
    while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(dialog, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    return data.ok;
}
