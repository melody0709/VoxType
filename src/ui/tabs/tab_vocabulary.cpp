#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tab_vocabulary.h"
#include "settings_controls.h"
#include "settings.h"
#include "ui_types.h"
#include "ui_theme.h"
#include "vocabulary_manager.h"
#include "path_service.h"
#include "utils.h"

#include <windowsx.h>
#include <shellapi.h>
#include <format>
#include <vector>

namespace ui_tab {

void TabVocabulary::UpdateStatusFromText(HWND parent, std::wstring_view text) {
    HWND statusLabel = GetDlgItem(parent, IDC_VOCAB_TAB_STATUS);
    if (!statusLabel) return;

    const std::wstring trimmed(text);
    if (trimmed.empty()) {
        SetWindowTextW(statusLabel, L"Status: Empty (0 entries). Common vocabulary disabled until words are added.");
        return;
    }

    auto parsed = vocabulary_manager::ParseVocabularyText(trimmed);
    if (parsed) {
        const size_t count = parsed->size();
        size_t superCount = 0;
        for (const auto& entry : *parsed) {
            if (entry.weight >= 10) superCount++;
        }
        std::wstring status = std::format(
            L"Status: {} active entries ({} high priority). Synced across Qwen & Volcano Engine.",
            count, superCount);
        SetWindowTextW(statusLabel, status.c_str());
    } else {
        std::wstring status = std::format(L"Status: Syntax error — {}", parsed.error());
        SetWindowTextW(statusLabel, status.c_str());
    }
}

void TabVocabulary::CreateControls(HWND parent) {
    m_controls.clear();

    const int topY = S(UiStyle::RowInputY(0));
    HWND btnEdit = CreateButton(parent, IDC_VOCAB_TAB_EDIT_FILE, S(30), topY, S(200), S(UiStyle::ActionBtnH), L"Edit in External Editor");
    AddVocabControl(btnEdit);

    HWND btnReload = CreateButton(parent, IDC_VOCAB_TAB_RELOAD, S(240), topY, S(150), S(UiStyle::ActionBtnH), L"Reload from File");
    AddVocabControl(btnReload);

    HWND btnFormat = CreateButton(parent, IDC_VOCAB_TAB_FORMAT, S(400), topY, S(120), S(UiStyle::ActionBtnH), L"Format JSON");
    AddVocabControl(btnFormat);

    HWND btnOpenFolder = CreateButton(parent, IDC_VOCAB_TAB_OPEN_FOLDER, S(530), topY, S(140), S(UiStyle::ActionBtnH), L"Open Folder");
    AddVocabControl(btnOpenFolder);

    const int groupBoxY = topY + S(UiStyle::ActionBtnH) + S(10);
    const int groupH = S(416);
    HWND vocabGroup = CreateWindowW(L"BUTTON", L"Custom Vocabulary (Universal)", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                    S(30), groupBoxY, S(788), groupH, parent,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOCAB_TAB_GROUP)),
                                    GetParentInstance(parent), nullptr);
    ApplyUiFont(vocabGroup);
    AddVocabControl(vocabGroup);

    const int editY = groupBoxY + S(26);
    const int editW = S(764);
    const int editH = groupH - S(38);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
                                S(42), editY, editW, editH, parent,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOCAB_TAB_TEXT)),
                                GetParentInstance(parent), nullptr);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(ui_theme::MonospaceFontForDpi(GetDpiForWindow(parent))), TRUE);
    AddVocabControl(edit);

    const int footerY = groupBoxY + groupH + S(8);
    HWND statusLabel = CreateWindowW(L"STATIC", L"Status: Ready", WS_CHILD | WS_VISIBLE,
                                     S(30), footerY, S(788), S(24), parent,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOCAB_TAB_STATUS)),
                                     GetParentInstance(parent), nullptr);
    ApplyUiFont(statusLabel);
    AddVocabControl(statusLabel);

    HWND hintLabel = CreateHint(parent, S(30), footerY + S(24), S(788), S(UiStyle::LabelH),
                                L"Supports JSON (\"word\": weight) or line format (word [weight]). Weights: 1–5 or 50 (default: 50).");
    AddVocabControl(hintLabel);
}

void TabVocabulary::DestroyControls() {
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
}

void TabVocabulary::Show(bool visible) {
    for (HWND c : m_controls) {
        ShowWindow(c, visible ? SW_SHOW : SW_HIDE);
    }
}

void TabVocabulary::LoadControls(HWND parent, const Config& cfg) {
    vocabulary_manager::EnsureVocabularyFileTemplate();

    auto fileContent = vocabulary_manager::ReadVocabularyFile();
    std::wstring textToDisplay;
    if (fileContent && !fileContent->empty()) {
        textToDisplay = *fileContent;
    } else if (!cfg.qwenVocabulary.empty()) {
        textToDisplay = cfg.qwenVocabulary;
    }

    HWND edit = GetDlgItem(parent, IDC_VOCAB_TAB_TEXT);
    if (edit) {
        SetWindowTextW(edit, textToDisplay.c_str());
    }
    UpdateStatusFromText(parent, textToDisplay);
}

void TabVocabulary::SaveControls(HWND parent, Config& cfg) {
    HWND edit = GetDlgItem(parent, IDC_VOCAB_TAB_TEXT);
    if (!edit) return;

    const int len = GetWindowTextLengthW(edit);
    std::wstring text;
    if (len > 0) {
        std::vector<wchar_t> buf(len + 1);
        GetWindowTextW(edit, buf.data(), len + 1);
        text = buf.data();
    }

    cfg.qwenVocabulary = text;

    if (text.empty()) {
        (void)vocabulary_manager::WriteVocabularyFile(L"{}\r\n");
    } else {
        auto parsed = vocabulary_manager::ParseVocabularyText(text);
        if (parsed) {
            std::string formatted = vocabulary_manager::FormatVocabularyJson(*parsed, true);
            (void)vocabulary_manager::WriteVocabularyFile(Utf8ToWide(formatted));
        } else {
            (void)vocabulary_manager::WriteVocabularyFile(text);
        }
    }
}

bool TabVocabulary::HandleCommand(HWND parent, WORD notifyCode, WORD controlId, HWND control) {
    switch (controlId) {
    case IDC_VOCAB_TAB_EDIT_FILE: {
        vocabulary_manager::EnsureVocabularyFileTemplate();
        const std::wstring vocabPath = vocabulary_manager::GetVocabularyFilePath();
        const HINSTANCE res = ShellExecuteW(parent, L"open", vocabPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(res) <= 32) {
            const std::wstring quotedPath = L"\"" + vocabPath + L"\"";
            ShellExecuteW(parent, L"open", L"notepad.exe", quotedPath.c_str(), nullptr, SW_SHOWNORMAL);
        }
        SetStatus(parent, L"Opened vocabulary.json in external editor.");
        return true;
    }
    case IDC_VOCAB_TAB_OPEN_FOLDER: {
        vocabulary_manager::EnsureVocabularyFileTemplate();
        const std::wstring vocabPath = vocabulary_manager::GetVocabularyFilePath();
        const size_t slash = vocabPath.find_last_of(L"\\/");
        const std::wstring vocabDir = (slash != std::wstring::npos) ? vocabPath.substr(0, slash) : PathService::AppDataDir();
        HINSTANCE res = ShellExecuteW(parent, L"open", vocabDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(res) <= 32) {
            res = ShellExecuteW(parent, L"open", L"explorer.exe", vocabDir.c_str(), nullptr, SW_SHOWNORMAL);
        }
        if (reinterpret_cast<INT_PTR>(res) <= 32) {
            SetStatus(parent, L"Unable to open vocabulary folder.");
        } else {
            SetStatus(parent, L"Opened vocabulary folder.");
        }
        return true;
    }
    case IDC_VOCAB_TAB_RELOAD: {
        auto res = vocabulary_manager::ReadVocabularyFile();
        if (res) {
            HWND edit = GetDlgItem(parent, IDC_VOCAB_TAB_TEXT);
            if (edit) {
                SetWindowTextW(edit, res->c_str());
            }
            UpdateStatusFromText(parent, *res);
            SetStatus(parent, L"Vocabulary reloaded from file.");
        } else {
            std::wstring err = L"Failed to read vocabulary.json: " + res.error();
            MessageBoxW(parent, err.c_str(), L"Vocabulary Reload Failed", MB_ICONERROR | MB_OK);
        }
        return true;
    }
    case IDC_VOCAB_TAB_FORMAT: {
        HWND edit = GetDlgItem(parent, IDC_VOCAB_TAB_TEXT);
        if (!edit) return true;

        const int len = GetWindowTextLengthW(edit);
        if (len <= 0) return true;

        std::vector<wchar_t> buf(len + 1);
        GetWindowTextW(edit, buf.data(), len + 1);

        auto parsed = vocabulary_manager::ParseVocabularyText(buf.data());
        if (parsed) {
            std::string formatted = vocabulary_manager::FormatVocabularyJson(*parsed, true);
            std::wstring wideFormatted = Utf8ToWide(formatted);
            SetWindowTextW(edit, wideFormatted.c_str());
            UpdateStatusFromText(parent, wideFormatted);
            SetStatus(parent, L"Vocabulary formatted as JSON.");
        } else {
            std::wstring err = L"Cannot format invalid vocabulary: " + parsed.error();
            MessageBoxW(parent, err.c_str(), L"Format Error", MB_ICONWARNING | MB_OK);
        }
        return true;
    }
    case IDC_VOCAB_TAB_TEXT: {
        if (notifyCode == EN_CHANGE) {
            HWND edit = GetDlgItem(parent, IDC_VOCAB_TAB_TEXT);
            if (edit) {
                const int len = GetWindowTextLengthW(edit);
                std::wstring text;
                if (len > 0) {
                    std::vector<wchar_t> buf(len + 1);
                    GetWindowTextW(edit, buf.data(), len + 1);
                    text = buf.data();
                }
                UpdateStatusFromText(parent, text);
            }
            return true;
        }
        break;
    }
    }
    return false;
}

} // namespace ui_tab
