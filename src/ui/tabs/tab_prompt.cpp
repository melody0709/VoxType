#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tab_prompt.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "llm_refine.h"

#include <windowsx.h>

namespace ui_tab {

namespace {

std::wstring s_customPromptBackup;
bool s_isCustomMode = false;

} // namespace

void TabPrompt::CreateControls(HWND parent) {
    m_controls.clear();
    HWND control = CreateLabel(parent, S(UiStyle::ContentLeft), S(UiStyle::RowLabelY(0)), S(UiStyle::LabelWidth), S(UiStyle::LabelH), L"Preset");
    AddPromptControl(control);
    AddPromptControl(CreateCombo(parent, IDC_LLM_PRESET_COMBO, S(UiStyle::InputLeft), S(UiStyle::RowInputY(0)), S(220), S(200)));

    control = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                            S(UiStyle::ContentLeft), S(UiStyle::RowInputY(0)) + S(UiStyle::EditH) + S(4),
                            S(UiStyle::InputWFull) + S(UiStyle::InputLeft) - S(UiStyle::ContentLeft), S(40),
                            parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LLM_PRESET_DESC)), GetParentInstance(parent), nullptr);
    ApplyUiFont(control);
    AddPromptControl(control);

    const int groupBoxY = S(UiStyle::RowInputY(0)) + S(UiStyle::EditH) + S(4) + S(40) + S(8);
    HWND promptGroup = CreateWindowW(L"BUTTON", L"System Prompt",
                                      WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                      S(30), groupBoxY, S(788), S(400),
                                      parent, nullptr, GetParentInstance(parent), nullptr);
    ApplyUiFont(promptGroup);
    AddPromptControl(promptGroup);

    const int promptEditY = groupBoxY + S(28);
    const int editWidth = S(788) - (S(UiStyle::ContentLeft) - S(30)) - S(8);
    const int editHeight = S(400) - S(28) - S(8) - S(48) - S(4);
    HWND llmPrompt = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
                                      S(UiStyle::ContentLeft), promptEditY,
                                      editWidth, editHeight,
                                      parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LLM_PROMPT)), GetParentInstance(parent), nullptr);
    ApplyUiFont(llmPrompt);
    AddPromptControl(llmPrompt);

    control = CreateWindowW(L"STATIC",
                            L"Note: System Prompt only takes effect when Punctuation is set to \"Auto punctuate + LLM\" in the Recognition tab.",
                            WS_CHILD | WS_VISIBLE,
                            S(UiStyle::ContentLeft), promptEditY + editHeight + S(4),
                            editWidth, S(48),
                            parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LLM_PROMPT_HINT)), GetParentInstance(parent), nullptr);
    ApplyUiFont(control);
    AddPromptControl(control);
}

void TabPrompt::DestroyControls() {
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
}

void TabPrompt::Show(bool visible) {
    for (HWND c : m_controls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
}

void TabPrompt::LoadControls(HWND parent, const Config& cfg) {
    SetWindowTextW(GetDlgItem(parent, IDC_LLM_PROMPT), cfg.llmPrompt.c_str());

    HWND presetCombo = GetDlgItem(parent, IDC_LLM_PRESET_COMBO);
    if (presetCombo) {
        ComboBox_ResetContent(presetCombo);
        for (int i = 0; i < llm::kPromptPresetCount; ++i) {
            ComboBox_AddString(presetCombo, llm::kPromptPresets[i].name);
        }
        ComboBox_AddString(presetCombo, L"Custom");
        int matchedPreset = -1;
        for (int i = 0; i < llm::kPromptPresetCount; ++i) {
            if (cfg.llmPrompt == llm::kPromptPresets[i].prompt) {
                matchedPreset = i;
                break;
            }
        }
        if (matchedPreset >= 0) {
            ComboBox_SetCurSel(presetCombo, matchedPreset);
            SetWindowTextW(GetDlgItem(parent, IDC_LLM_PRESET_DESC), llm::kPromptPresets[matchedPreset].description);
            s_customPromptBackup.clear();
            s_isCustomMode = false;
            SendMessageW(GetDlgItem(parent, IDC_LLM_PROMPT), EM_SETREADONLY, TRUE, 0);
        } else {
            ComboBox_SetCurSel(presetCombo, llm::kPromptPresetCount);
            SetWindowTextW(GetDlgItem(parent, IDC_LLM_PRESET_DESC), L"Custom prompt");
            s_customPromptBackup = cfg.llmPrompt;
            s_isCustomMode = true;
            SendMessageW(GetDlgItem(parent, IDC_LLM_PROMPT), EM_SETREADONLY, FALSE, 0);
        }
    }
}

void TabPrompt::SaveControls(HWND parent, Config& cfg) {
    int promptLen = GetWindowTextLengthW(GetDlgItem(parent, IDC_LLM_PROMPT));
    std::wstring promptText(promptLen + 1, L'\0');
    GetWindowTextW(GetDlgItem(parent, IDC_LLM_PROMPT), promptText.data(), promptLen + 1);
    promptText.resize(promptLen);
    cfg.llmPrompt = promptText;
}

bool TabPrompt::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    if (controlId == IDC_LLM_PRESET_COMBO && notifyCode == CBN_SELCHANGE) {
        HWND presetCombo = GetDlgItem(parent, IDC_LLM_PRESET_COMBO);
        HWND promptEdit = GetDlgItem(parent, IDC_LLM_PROMPT);
        HWND descStatic = GetDlgItem(parent, IDC_LLM_PRESET_DESC);
        int sel = ComboBox_GetCurSel(presetCombo);
        if (sel >= 0 && sel < llm::kPromptPresetCount) {
            if (s_isCustomMode) {
                int len = GetWindowTextLengthW(promptEdit);
                std::wstring text(len + 1, L'\0');
                GetWindowTextW(promptEdit, text.data(), len + 1);
                text.resize(len);
                s_customPromptBackup = text;
            }
            s_isCustomMode = false;
            SetWindowTextW(promptEdit, llm::kPromptPresets[sel].prompt);
            SetWindowTextW(descStatic, llm::kPromptPresets[sel].description);
            SendMessageW(promptEdit, EM_SETREADONLY, TRUE, 0);
        } else if (sel == llm::kPromptPresetCount) {
            s_isCustomMode = true;
            SetWindowTextW(promptEdit, s_customPromptBackup.c_str());
            SetWindowTextW(descStatic, L"Custom prompt");
            SendMessageW(promptEdit, EM_SETREADONLY, FALSE, 0);
        }
        return true;
    }
    return false;
}

} // namespace ui_tab
