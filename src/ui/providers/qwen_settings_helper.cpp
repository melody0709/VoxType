#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "qwen_settings_helper.h"
#include "provider_qwen.h"
#include "settings_controls.h"
#include "settings_dialogs.h"
#include "settings.h"
#include "ui_types.h"
#include "ui_utils.h"
#include "asr_probe_service.h"
#include "qwen_special_word_filter.h"

#include <windowsx.h>
#include <winhttp.h>

namespace ui_provider {

bool IsQwenAudioHttpModel(const std::wstring& model) {
    return model == L"qwen-audio-3.0-asr-flash";
}

bool IsQwenAudioStreamingModel(const std::wstring& model) {
    return model == L"qwen-audio-3.0-asr-flash-streaming";
}

std::wstring QwenModelFromControl(HWND hwnd) {
    wchar_t model[256] = {};
    GetWindowTextW(GetDlgItem(hwnd, IDC_QWEN_MODEL), model, 256);
    return model;
}

void StoreQwenProfileUrl(HWND hwnd, const std::wstring& model, QwenProfileState& state) {
    const std::wstring url = QwenControlText(hwnd, IDC_QWEN_BASE_URL, 2048);
    if (url.empty()) return;
    if (IsQwenAudioHttpModel(model)) state.uiHttpUrl = url;
    else if (IsQwenAudioStreamingModel(model)) state.uiAudioStreamingUrl = url;
    else state.uiLegacyUrl = url;
}

std::wstring NormalizeQwenLanguageHints(const std::wstring& raw) {
    std::wstring normalized;
    size_t start = 0;
    size_t count = 0;
    while (start <= raw.size() && count < 4) {
        const size_t end = raw.find_first_of(L",;", start);
        std::wstring item = Trim(raw.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
        if (!item.empty()) {
            if (item == L"fil") item = L"tl";
            bool valid = item.size() <= 16;
            for (wchar_t ch : item) valid = valid && ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || ch == L'-');
            if (valid) {
                if (!normalized.empty()) normalized += L',';
                normalized += item;
                ++count;
            }
        }
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return normalized;
}

void UpdateQwenLanguageEffectiveHint(HWND hwnd, HWND hintControl) {
    if (!hintControl) return;
    const std::wstring hints = NormalizeQwenLanguageHints(
        QwenControlText(hwnd, IDC_QWEN_LANGUAGE_HINTS, 1024));
    const std::wstring fallback = QwenLanguageCodeFromIndex(
        ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_QWEN_LANGUAGE)));
    const std::wstring effective = hints.empty()
        ? (fallback.empty() ? L"Auto" : fallback)
        : hints;
    SetWindowTextW(
        hintControl,
        (L"Effective language: " + effective +
         L". Non-empty hints override Fallback language; blank = Auto.").c_str());
}

bool ValidateQwenHints(const std::wstring& raw, std::wstring& error) {
    const std::wstring text = Trim(raw);
    if (text.empty()) return true;
    size_t start = 0;
    size_t count = 0;
    while (start <= text.size()) {
        const size_t end = text.find_first_of(L",;", start);
        std::wstring item = Trim(text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
        if (item.empty()) {
            error = L"Language hints must contain 1–4 non-empty language codes.";
            return false;
        }
        if (item == L"fil") item = L"tl";
        if (item.size() > 16) {
            error = L"Each language hint must be at most 16 characters.";
            return false;
        }
        for (wchar_t ch : item) {
            if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || ch == L'-')) {
                error = L"Language hints may contain only letters and hyphens.";
                return false;
            }
        }
        if (++count > 4) {
            error = L"Audio 3 supports at most 4 language hints.";
            return false;
        }
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return true;
}

bool ValidateQwenEndpoint(const std::wstring& raw,
                          const std::wstring& scheme,
                          const std::wstring& path,
                          std::wstring& error) {
    URL_COMPONENTSW parts = {};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    const std::wstring original = Trim(raw);
    if (original.empty() || original.rfind(scheme + L"://", 0) != 0) {
        error = L"Qwen Base URL is invalid.";
        return false;
    }
    std::wstring url = original;
    const std::wstring parsedScheme = (scheme == L"wss") ? L"https" :
        (scheme == L"ws" ? L"http" : scheme);
    if (scheme == L"wss" || scheme == L"ws") url.replace(0, scheme.size(), parsedScheme);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        error = L"Qwen Base URL is invalid.";
        return false;
    }
    const std::wstring actualScheme(parts.lpszScheme, parts.dwSchemeLength);
    if (actualScheme != parsedScheme || parts.dwHostNameLength == 0) {
        error = L"Qwen Base URL must use " + scheme + L" and include a host.";
        return false;
    }
    std::wstring actualPath(parts.lpszUrlPath, parts.dwUrlPathLength);
    while (actualPath.size() > 1 && actualPath.back() == L'/') actualPath.pop_back();
    if (actualPath != path) {
        error = L"Qwen Base URL path must be " + path + L".";
        return false;
    }
    return true;
}

bool ValidateQwenAdvancedData(QwenAdvancedDialogData& data, std::wstring& error) {
    if (!asr_probe::ValidateQwenVocabulary(data.vocabulary, &error)) return false;
    if (!data.streaming) return true;
    qwen_special_word_filter::Config specialFilter;
    if (!qwen_special_word_filter::Normalize(
            data.specialReplace, data.specialEmpty, data.systemReservedFilter,
            specialFilter, &error)) {
        return false;
    }
    data.specialReplace = qwen_special_word_filter::JoinLines(specialFilter.replaceWords);
    data.specialEmpty = qwen_special_word_filter::JoinLines(specialFilter.emptyWords);
    return true;
}

bool ShowQwenAdvancedDialog(HWND hwnd, QwenAdvancedDialogData& data) {
    return ::ShowQwenAdvancedDialog(hwnd, data, ValidateQwenAdvancedData);
}

void ApplyQwenModelProfile(HWND hwnd, const std::wstring& model, bool forceUpdate,
                           QwenProfileState& state, HWND hintControl) {
    StoreQwenProfileUrl(hwnd, state.uiModel, state);
    state.uiModel = model;
    std::wstring urlToSet;
    if (IsQwenAudioHttpModel(model)) {
        urlToSet = state.uiHttpUrl;
    } else if (IsQwenAudioStreamingModel(model)) {
        urlToSet = state.uiAudioStreamingUrl;
    } else {
        urlToSet = state.uiLegacyUrl;
    }
    if (urlToSet.empty() || forceUpdate) {
        if (IsQwenAudioHttpModel(model)) {
            urlToSet = L"https://dashscope.aliyuncs.com/api/v1/services/audio/asr/transcription";
        } else if (IsQwenAudioStreamingModel(model)) {
            urlToSet = L"wss://dashscope.aliyuncs.com/api-ws/v1/inference/";
        } else {
            urlToSet = kQwenDefaultBaseUrl;
        }
    }
    SetWindowTextW(GetDlgItem(hwnd, IDC_QWEN_BASE_URL), urlToSet.c_str());
    ShowQwenSubControls(hwnd);
    UpdateQwenLanguageEffectiveHint(hwnd, hintControl);
}

void EditQwenAdvancedSettings(HWND hwnd, QwenProfileState& state, HWND hintControl) {
    QwenAdvancedDialogData data;
    data.streaming = IsQwenAudioStreamingModel(QwenModelFromControl(hwnd));
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

    if (!ShowQwenAdvancedDialog(hwnd, data)) return;

    SetWindowTextW(GetDlgItem(hwnd, IDC_QWEN_VOCABULARY_ID), data.vocabularyId.c_str());
    SetWindowTextW(GetDlgItem(hwnd, IDC_QWEN_VOCABULARY), data.vocabulary.c_str());
    Button_SetCheck(GetDlgItem(hwnd, IDC_QWEN_SEMANTIC_PUNCTUATION),
                    data.semanticPunctuation ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(GetDlgItem(hwnd, IDC_QWEN_MAX_SENTENCE_SILENCE), data.maxSentenceSilence.c_str());
    Button_SetCheck(GetDlgItem(hwnd, IDC_QWEN_MULTI_THRESHOLD),
                    data.multiThreshold ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(hwnd, IDC_QWEN_HEARTBEAT), data.heartbeat ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(hwnd, IDC_QWEN_SPEECH_NOISE_ENABLE),
                    data.speechNoiseEnabled ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(GetDlgItem(hwnd, IDC_QWEN_SPEECH_NOISE_THRESHOLD), data.speechNoiseThreshold.c_str());
    Button_SetCheck(GetDlgItem(hwnd, IDC_QWEN_CONTINUE_CONTEXT),
                    data.continueContext ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(GetDlgItem(hwnd, IDC_QWEN_SPECIAL_REPLACE), data.specialReplace.c_str());
    SetWindowTextW(GetDlgItem(hwnd, IDC_QWEN_SPECIAL_EMPTY), data.specialEmpty.c_str());
    Button_SetCheck(GetDlgItem(hwnd, IDC_QWEN_SYSTEM_FILTER),
                    data.systemReservedFilter ? BST_CHECKED : BST_UNCHECKED);
    ApplyQwenModelProfile(hwnd, QwenModelFromControl(hwnd), true, state, hintControl);
    SetStatus(hwnd, L"Qwen advanced settings updated. Click Save to apply.");
}

} // namespace ui_provider
