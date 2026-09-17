#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

bool RegisterWindowClasses();
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowTrayMenu(HWND hwnd);

void DebugModeOpenConsole();
void DebugModeCloseConsole();

void SetTaskbarCreatedMessage(UINT msg);
UINT GetTaskbarCreatedMessage();
