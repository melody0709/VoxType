#include "ui_theme.h"
#include "ui_types.h"

namespace ui_theme {
namespace {

HFONT s_uiFont = nullptr;
HFONT s_titleFont = nullptr;
HFONT s_sectionFont = nullptr;
HBRUSH s_settingsBgBrush = nullptr;
HBRUSH s_cardBrush = nullptr;
HBRUSH s_controlBgBrush = nullptr;

HFONT MakeFont(int pointSize, int weight) {
    HDC hdc = GetDC(nullptr);
    const int logPixelsY = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) ReleaseDC(nullptr, hdc);
    const int height = -MulDiv(pointSize, logPixelsY, 72);
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

} // namespace

void InitTheme() {
    if (!s_uiFont) s_uiFont = MakeFont(9, FW_NORMAL);
    if (!s_titleFont) s_titleFont = MakeFont(14, FW_SEMIBOLD);
    if (!s_sectionFont) s_sectionFont = MakeFont(9, FW_SEMIBOLD);
    if (!s_settingsBgBrush) s_settingsBgBrush = CreateSolidBrush(UiStyle::BgColor);
    if (!s_cardBrush) s_cardBrush = CreateSolidBrush(UiStyle::ControlBgColor);
    if (!s_controlBgBrush) s_controlBgBrush = CreateSolidBrush(UiStyle::ControlBgColor);
}

void CleanupTheme() {
    if (s_uiFont) { DeleteObject(s_uiFont); s_uiFont = nullptr; }
    if (s_titleFont) { DeleteObject(s_titleFont); s_titleFont = nullptr; }
    if (s_sectionFont) { DeleteObject(s_sectionFont); s_sectionFont = nullptr; }
    if (s_settingsBgBrush) { DeleteObject(s_settingsBgBrush); s_settingsBgBrush = nullptr; }
    if (s_cardBrush) { DeleteObject(s_cardBrush); s_cardBrush = nullptr; }
    if (s_controlBgBrush) { DeleteObject(s_controlBgBrush); s_controlBgBrush = nullptr; }
}

HFONT UiFont() { if (!s_uiFont) InitTheme(); return s_uiFont; }
HFONT TitleFont() { if (!s_titleFont) InitTheme(); return s_titleFont; }
HFONT SectionFont() { if (!s_sectionFont) InitTheme(); return s_sectionFont; }
HBRUSH SettingsBgBrush() { if (!s_settingsBgBrush) InitTheme(); return s_settingsBgBrush; }
HBRUSH CardBrush() { if (!s_cardBrush) InitTheme(); return s_cardBrush; }
HBRUSH ControlBgBrush() { if (!s_controlBgBrush) InitTheme(); return s_controlBgBrush; }

} // namespace ui_theme
