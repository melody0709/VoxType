#pragma once

// Settings 界面文案的双语表（中文 / English）。
// 字段定义由 ui_strings.inc 生成（X-macro），英/中两张表同源同序，
// 缺翻译即编译错误；语言切换后重新取 UiText() 即可拿到当前语言表。

#include <format>
#include <string>
#include <string_view>

namespace ui_strings {

enum class UiLanguage { Auto, En, Zh };

// 解析持久化配置值（"auto" / "en" / "zh"），其余一律回退 Auto。
UiLanguage ParseUiLanguage(std::wstring_view value);
// 配置序列化用的小写代码（"auto" / "en" / "zh"）。
const wchar_t* UiLanguageCode(UiLanguage language);
// Auto 经 GetUserDefaultUILanguage() 解析后只返回 En 或 Zh。
UiLanguage EffectiveUiLanguage();
void SetUiLanguage(UiLanguage language);
// 应用持久化的语言值并报告生效语言是否变化（Save 后据此重建 Settings 窗口）。
bool ApplyUiLanguageFromConfig(std::wstring_view value);

struct UiStrings {
#define UI_TEXT(field, en, zh) const wchar_t* field;
#include "ui_strings.inc"
#undef UI_TEXT
};

// 取当前生效语言的文案表。控件重建（语言切换 / DPI 变化）后必须重新调用，
// 不得把返回值里的指针长期缓存到静态存储。
const UiStrings& UiText();

// LLM 提示词预设的界面显示名（下标与 llm::kPromptPresets 一致：Basic Fix / Deep Fix /
// Polish）。只本地化显示名；preset id 与提示词正文在 llm_refine.h 层，不在此处。
inline const wchar_t* LlmPresetDisplayName(int index) {
    switch (index) {
    case 0: return UiText().llmPresetBasicFixName;
    case 1: return UiText().llmPresetDeepFixName;
    case 2: return UiText().llmPresetPolishName;
    default: return L"";
    }
}

// 文案表里的格式串是运行时值，不能走 std::format 的 consteval 格式串检查，
// 含 {} 占位符的文案统一经此函数展开（内部走 std::vformat）。
template <class... Args>
std::wstring Format(const wchar_t* pattern, Args&&... args) {
    return std::vformat(std::wstring_view(pattern), std::make_wformat_args(args...));
}

} // namespace ui_strings
