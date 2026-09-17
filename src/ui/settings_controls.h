#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace UiStyle {
extern float Scale;
}

void UpdateUiScale(HWND hwnd);
int S(int px);
void MarkSettingsHint(HWND hwnd);
bool IsSettingsHint(HWND hwnd);
void OpenAsrDebugLog(HWND hwnd, const wchar_t* fileName);

HWND CreateLabel(HWND parent, int x, int y, int w, int h, const wchar_t* text);
HWND CreateHint(HWND parent, int x, int y, int w, int h, const wchar_t* text);
HWND CreateCombo(HWND parent, int id, int x, int y, int w, int h);
HWND CreateButton(HWND parent, int id, int x, int y, int w, int h, const wchar_t* text);
HWND CreateCheckBox(HWND parent, int id, int x, int y, int w, int h, const wchar_t* text);
std::wstring ComboText(HWND combo);
void BrowseModelDirectory(HWND hwnd);

int QwenLanguageIndexFromCode(const std::wstring& code);
const wchar_t* QwenLanguageCodeFromIndex(int index);
void PopulateQwenLanguageCombo(HWND combo);
