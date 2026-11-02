#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ui_strings.h"

#include <windows.h>

namespace ui_strings {
namespace {

constexpr UiStrings kStringsEn = {
#define UI_TEXT(field, en, zh) en,
#include "ui_strings.inc"
#undef UI_TEXT
};

constexpr UiStrings kStringsZh = {
#define UI_TEXT(field, en, zh) zh,
#include "ui_strings.inc"
#undef UI_TEXT
};

UiLanguage g_language = UiLanguage::Auto;

// 跟随系统：主语言为中文（zh-CN / zh-TW / zh-HK 等）都落到简体表。
UiLanguage SystemLanguage() {
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE
        ? UiLanguage::Zh : UiLanguage::En;
}

} // namespace

UiLanguage ParseUiLanguage(std::wstring_view value) {
    if (value == L"en") return UiLanguage::En;
    if (value == L"zh") return UiLanguage::Zh;
    return UiLanguage::Auto;
}

const wchar_t* UiLanguageCode(UiLanguage language) {
    switch (language) {
    case UiLanguage::En: return L"en";
    case UiLanguage::Zh: return L"zh";
    default: return L"auto";
    }
}

UiLanguage EffectiveUiLanguage() {
    return g_language == UiLanguage::Auto ? SystemLanguage() : g_language;
}

void SetUiLanguage(UiLanguage language) {
    g_language = language;
}

bool ApplyUiLanguageFromConfig(std::wstring_view value) {
    const UiLanguage before = EffectiveUiLanguage();
    SetUiLanguage(ParseUiLanguage(value));
    return EffectiveUiLanguage() != before;
}

const UiStrings& UiText() {
    return EffectiveUiLanguage() == UiLanguage::Zh ? kStringsZh : kStringsEn;
}

} // namespace ui_strings
