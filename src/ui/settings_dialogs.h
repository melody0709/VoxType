#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

constexpr int IDC_INPUT_EDIT = 3001;

struct InputDlgData {
    const wchar_t* title = nullptr;
    std::wstring result;
    bool ok = false;
};

bool ShowInputDialog(HWND parent, const wchar_t* title, std::wstring& out);

constexpr int IDC_VOLC_EXTRA_EDIT = 3002;
constexpr int IDC_VOLC_EXTRA_HOTWORDS = 3003;
constexpr int IDC_VOLC_EXTRA_CONTEXT = 3004;
constexpr int IDC_VOLC_EXTRA_RESET = 3005;

struct VolcExtraDlgData {
    std::wstring text;
    bool ok = false;
};

bool ShowVolcExtraDialog(HWND parent, std::wstring& out);

constexpr int IDC_PROMPT_DLG_PRESET = 3010;
constexpr int IDC_PROMPT_DLG_DESC = 3011;
constexpr int IDC_PROMPT_DLG_EDIT = 3012;
constexpr int IDC_PROMPT_DLG_RESET = 3013;
constexpr int IDC_PROMPT_DLG_LABEL = 3014;

struct PromptManageDlgData {
    std::wstring prompt;
    std::wstring customBackup;
    bool ok = false;
};

bool ShowPromptManageDialog(HWND parent, std::wstring& outPrompt, std::wstring* customBackup = nullptr);

struct QwenAdvancedDialogData {
    bool streaming = false;
    bool ok = false;
    std::wstring vocabularyId;
    std::wstring vocabulary;
    bool semanticPunctuation = false;
    std::wstring maxSentenceSilence;
    bool multiThreshold = false;
    bool heartbeat = false;
    bool speechNoiseEnabled = false;
    std::wstring speechNoiseThreshold;
    bool continueContext = false;
    std::wstring specialReplace;
    std::wstring specialEmpty;
    bool systemReservedFilter = false;
};

using QwenAdvancedValidator = bool (*)(QwenAdvancedDialogData& data, std::wstring& error);

bool ShowQwenAdvancedDialog(HWND parent, QwenAdvancedDialogData& data, QwenAdvancedValidator validator);
