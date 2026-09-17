#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace ui_theme {

HFONT UiFont();
HFONT TitleFont();
HFONT SectionFont();
HBRUSH SettingsBgBrush();
HBRUSH CardBrush();
HBRUSH ControlBgBrush();

void InitTheme();
void CleanupTheme();

} // namespace ui_theme
