#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace ui_theme {

HFONT UiFont();
HFONT UiFontForDpi(UINT dpi);
HFONT TitleFont();
HFONT TitleFontForDpi(UINT dpi);
HFONT SectionFont();
HFONT SectionFontForDpi(UINT dpi);
HFONT MonospaceFont();
HFONT MonospaceFontForDpi(UINT dpi);
HBRUSH SettingsBgBrush();
HBRUSH CardBrush();
HBRUSH ControlBgBrush();

void InitTheme();
void CleanupTheme();

} // namespace ui_theme
