#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>

#include "ui_types.h"

extern HWND g_hudWindow;
extern bool g_hudHasSpoken;
extern bool g_hudIsRefining;

void SetHudRecording(bool recording);
bool IsHudRecording();


void AddTrayIcon(HWND hwnd);
void RemoveTrayIcon(HWND hwnd);
float CurrentHudLevel();
DWRITE_TEXT_METRICS MeasureHudText(const std::wstring& text, float maxWidth, DWRITE_WORD_WRAPPING wrapping);
UINT32 HudWrappedLineCount(const std::wstring& text,
                           float maxWidthDip,
                           float maxScreenWidthFraction);
HudSize IdealHudSize(const std::wstring& text, const RECT& workArea, float scale);
void PositionHud(HWND hwnd);
void ShowHud(const std::wstring& text);
void ShowHudConstrained(const std::wstring& text,
                        float maxWidthDip,
                        float maxScreenWidthFraction,
                        int maxLines,
                        int fixedLines = 0);
void StartHudRecordingAnimation();
void HideHud();
HFONT MakeFont(int pointSize, int weight);
void CreateUiResources();
void DeleteUiResources();
bool EnsureHudRenderTarget(HWND hwnd);
void DrawHudDirect2D(HWND hwnd);
LRESULT CALLBACK HudWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
