#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include "settings_dialogs.h"

namespace ui_provider {

constexpr wchar_t kQwenDefaultBaseUrl[] = L"wss://dashscope.aliyuncs.com/api-ws/v1/inference/";
constexpr wchar_t kQwenDefaultModel[] = L"paraformer-realtime-v2";

bool IsQwenAudioHttpModel(const std::wstring& model);
bool IsQwenAudioStreamingModel(const std::wstring& model);

std::wstring QwenModelFromControl(HWND hwnd);
struct QwenProfileState {
    std::wstring uiModel;
    std::wstring uiHttpUrl;
    std::wstring uiAudioStreamingUrl;
    std::wstring uiLegacyUrl;
};

void StoreQwenProfileUrl(HWND hwnd, const std::wstring& model, QwenProfileState& state);
std::wstring NormalizeQwenLanguageHints(const std::wstring& raw);
void UpdateQwenLanguageEffectiveHint(HWND hwnd, HWND hintControl);
bool ValidateQwenHints(const std::wstring& raw, std::wstring& error);
bool ValidateQwenEndpoint(const std::wstring& raw,
                          const std::wstring& scheme,
                          const std::wstring& path,
                          std::wstring& error);
bool ValidateQwenAdvancedData(QwenAdvancedDialogData& data, std::wstring& error);
bool ShowQwenAdvancedDialog(HWND hwnd, QwenAdvancedDialogData& data);
void ApplyQwenModelProfile(HWND hwnd, const std::wstring& model, bool forceUpdate,
                           QwenProfileState& state, HWND hintControl);
void EditQwenAdvancedSettings(HWND hwnd, QwenProfileState& state, HWND hintControl);

} // namespace ui_provider
