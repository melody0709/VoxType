#include "ui_theme.h"
#include "ui_types.h"

#include <mutex>
#include <vector>

namespace ui_theme {
namespace {

struct DpiFontEntry {
    UINT dpi = 0;
    HFONT uiFont = nullptr;
    HFONT titleFont = nullptr;
    HFONT sectionFont = nullptr;
};

std::vector<DpiFontEntry> s_fontCache;
std::mutex s_themeMutex;

HBRUSH s_settingsBgBrush = nullptr;
HBRUSH s_cardBrush = nullptr;
HBRUSH s_controlBgBrush = nullptr;

HFONT CreateDpiFont(int pointSize, int weight, UINT dpi) {
    if (dpi == 0) dpi = 96;
    const int height = -MulDiv(pointSize, static_cast<int>(dpi), 72);
    HFONT font = CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!font) {
        font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }
    return font;
}

DpiFontEntry& GetOrCreateEntryLocked(UINT dpi) {
    if (dpi == 0) {
        dpi = GetDpiForSystem();
        if (dpi == 0) dpi = 96;
    }
    for (auto& entry : s_fontCache) {
        if (entry.dpi == dpi) {
            return entry;
        }
    }
    DpiFontEntry entry;
    entry.dpi = dpi;
    entry.uiFont = CreateDpiFont(9, FW_NORMAL, dpi);
    entry.titleFont = CreateDpiFont(14, FW_SEMIBOLD, dpi);
    entry.sectionFont = CreateDpiFont(9, FW_SEMIBOLD, dpi);
    s_fontCache.push_back(entry);
    return s_fontCache.back();
}

} // namespace

void InitTheme() {
    std::lock_guard<std::mutex> lock(s_themeMutex);
    GetOrCreateEntryLocked(0);
    if (!s_settingsBgBrush) s_settingsBgBrush = CreateSolidBrush(UiStyle::BgColor);
    if (!s_cardBrush) s_cardBrush = CreateSolidBrush(UiStyle::ControlBgColor);
    if (!s_controlBgBrush) s_controlBgBrush = CreateSolidBrush(UiStyle::ControlBgColor);
}

void CleanupTheme() {
    std::lock_guard<std::mutex> lock(s_themeMutex);
    for (auto& entry : s_fontCache) {
        if (entry.uiFont) { DeleteObject(entry.uiFont); entry.uiFont = nullptr; }
        if (entry.titleFont) { DeleteObject(entry.titleFont); entry.titleFont = nullptr; }
        if (entry.sectionFont) { DeleteObject(entry.sectionFont); entry.sectionFont = nullptr; }
    }
    s_fontCache.clear();
    if (s_settingsBgBrush) { DeleteObject(s_settingsBgBrush); s_settingsBgBrush = nullptr; }
    if (s_cardBrush) { DeleteObject(s_cardBrush); s_cardBrush = nullptr; }
    if (s_controlBgBrush) { DeleteObject(s_controlBgBrush); s_controlBgBrush = nullptr; }
}

HFONT UiFontForDpi(UINT dpi) {
    std::lock_guard<std::mutex> lock(s_themeMutex);
    return GetOrCreateEntryLocked(dpi).uiFont;
}

HFONT TitleFontForDpi(UINT dpi) {
    std::lock_guard<std::mutex> lock(s_themeMutex);
    return GetOrCreateEntryLocked(dpi).titleFont;
}

HFONT SectionFontForDpi(UINT dpi) {
    std::lock_guard<std::mutex> lock(s_themeMutex);
    return GetOrCreateEntryLocked(dpi).sectionFont;
}

HFONT UiFont() { return UiFontForDpi(0); }
HFONT TitleFont() { return TitleFontForDpi(0); }
HFONT SectionFont() { return SectionFontForDpi(0); }

HBRUSH SettingsBgBrush() { if (!s_settingsBgBrush) InitTheme(); return s_settingsBgBrush; }
HBRUSH CardBrush() { if (!s_cardBrush) InitTheme(); return s_cardBrush; }
HBRUSH ControlBgBrush() { if (!s_controlBgBrush) InitTheme(); return s_controlBgBrush; }

} // namespace ui_theme
