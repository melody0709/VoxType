// ui_strings 双语表完整性测试（离线，不碰窗口与剪贴板）：
// 1. 每条文案英/中都非空；
// 2. 英/中 {} 占位符数量一致（Format 走 vformat，数量不一致会抛 format_error）；
// 3. 语言解析对非法值回退 auto；切换语言后 UiText() 换表。
#include "ui_strings.h"

#include <cstdio>
#include <string_view>

namespace {

struct Entry {
    const wchar_t* en;
    const wchar_t* zh;
};

const Entry kEntries[] = {
#define UI_TEXT(field, en, zh) { en, zh },
#include "ui_strings.inc"
#undef UI_TEXT
};

int g_failures = 0;

void Check(bool ok, const char* what, size_t index) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s (entry %zu)\n", what, index);
    }
}

size_t PlaceholderCount(std::wstring_view s) {
    size_t count = 0;
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        if (s[i] == L'{' && s[i + 1] == L'}') {
            ++count;
            ++i;
        }
    }
    return count;
}

} // namespace

int main() {
    using namespace ui_strings;

    for (size_t i = 0; i < std::size(kEntries); ++i) {
        const Entry& e = kEntries[i];
        Check(e.en && *e.en, "empty English text", i);
        Check(e.zh && *e.zh, "empty Chinese text", i);
        if (e.en && e.zh) {
            Check(PlaceholderCount(e.en) == PlaceholderCount(e.zh),
                  "en/zh placeholder count mismatch", i);
        }
    }

    Check(ParseUiLanguage(L"en") == UiLanguage::En, "parse en", 0);
    Check(ParseUiLanguage(L"zh") == UiLanguage::Zh, "parse zh", 0);
    Check(ParseUiLanguage(L"auto") == UiLanguage::Auto, "parse auto", 0);
    Check(ParseUiLanguage(L"") == UiLanguage::Auto, "parse empty falls back", 0);
    Check(ParseUiLanguage(L"bogus") == UiLanguage::Auto, "parse bogus falls back", 0);
    Check(std::wstring_view(UiLanguageCode(UiLanguage::Auto)) == L"auto", "code auto", 0);
    Check(std::wstring_view(UiLanguageCode(UiLanguage::En)) == L"en", "code en", 0);
    Check(std::wstring_view(UiLanguageCode(UiLanguage::Zh)) == L"zh", "code zh", 0);

    SetUiLanguage(UiLanguage::En);
    Check(EffectiveUiLanguage() == UiLanguage::En, "effective en", 0);
    const wchar_t* enTitle = UiText().settingsTitle;
    SetUiLanguage(UiLanguage::Zh);
    Check(EffectiveUiLanguage() == UiLanguage::Zh, "effective zh", 0);
    Check(UiText().settingsTitle != enTitle, "switching language swaps the table", 0);

    Check(ApplyUiLanguageFromConfig(L"en"), "applying en reports change", 0);
    Check(!ApplyUiLanguageFromConfig(L"en"), "applying same value reports no change", 0);
    Check(EffectiveUiLanguage() == UiLanguage::En, "config value sticks", 0);

    // 语言下拉的序数映射（Save/LoadControls 与 DPI 重建经此往返）：0=auto、1=en、2=zh。
    for (int i = 0; i <= 2; ++i) {
        const wchar_t* code = UiLanguageCode(static_cast<UiLanguage>(i));
        Check(ParseUiLanguage(code) == static_cast<UiLanguage>(i),
              "combo ordinal round-trip (Save/Load identity)", static_cast<size_t>(i));
    }
    Check(std::wstring_view(UiLanguageCode(static_cast<UiLanguage>(3))) == L"auto",
          "out-of-range combo index maps to auto", 0);
    Check(std::wstring_view(UiLanguageCode(static_cast<UiLanguage>(-1))) == L"auto",
          "CB_ERR(-1) maps to auto", 0);

    // Save 后重建的触发门：同值再存必须报"无变化"，否则每次 Save 都会重建窗口。
    Check(ApplyUiLanguageFromConfig(L"zh"), "applying zh reports change", 0);
    Check(!ApplyUiLanguageFromConfig(L"zh"), "re-applying zh reports no change", 0);

    // 提示词预设显示名：英/中都非空且互不相同；越界下标回空串（不回退到错误预设）。
    for (UiLanguage lang : {UiLanguage::En, UiLanguage::Zh}) {
        SetUiLanguage(lang);
        Check(*LlmPresetDisplayName(0) && *LlmPresetDisplayName(1) && *LlmPresetDisplayName(2),
              "preset display names non-empty", 0);
        Check(std::wstring_view(LlmPresetDisplayName(0)) != LlmPresetDisplayName(1) &&
              std::wstring_view(LlmPresetDisplayName(1)) != LlmPresetDisplayName(2),
              "preset display names distinct", 0);
        Check(*LlmPresetDisplayName(3) == L'\0', "preset name out of range is empty", 0);
        Check(*LlmPresetDisplayName(-1) == L'\0', "preset name negative index is empty", 0);
    }

    if (g_failures == 0) {
        std::printf("ui_strings_test PASSED (%zu entries)\n", std::size(kEntries));
        return 0;
    }
    std::printf("ui_strings_test FAILED (%d checks)\n", g_failures);
    return 1;
}
