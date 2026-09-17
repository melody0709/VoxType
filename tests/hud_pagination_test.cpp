#include "hud_pagination.h"

#include <cassert>
#include <iostream>
#include <string>

static void TestBoundaries() {
    assert(IsStreamingPartialStrongBoundary(L'。'));
    assert(IsStreamingPartialStrongBoundary(L'！'));
    assert(IsStreamingPartialStrongBoundary(L'!'));
    assert(IsStreamingPartialStrongBoundary(L'？'));
    assert(IsStreamingPartialStrongBoundary(L'?'));
    assert(IsStreamingPartialStrongBoundary(L'；'));
    assert(IsStreamingPartialStrongBoundary(L';'));
    assert(IsStreamingPartialStrongBoundary(L'\n'));
    assert(!IsStreamingPartialStrongBoundary(L'，'));
    assert(!IsStreamingPartialStrongBoundary(L'a'));

    assert(IsStreamingPartialSoftBoundary(L'，'));
    assert(IsStreamingPartialSoftBoundary(L','));
    assert(IsStreamingPartialSoftBoundary(L'、'));
    assert(IsStreamingPartialSoftBoundary(L'：'));
    assert(IsStreamingPartialSoftBoundary(L':'));
    assert(!IsStreamingPartialSoftBoundary(L'。'));
    assert(!IsStreamingPartialSoftBoundary(L'x'));

    std::wstring text = L" \t\r\n。，！abc";
    size_t skipped = SkipStreamingPartialLeadingSeparators(text, 0);
    assert(skipped == 7); // skips spaces, newline, periods, commas, exclamations up to 'a'
}

static void TestSurrogatePairs() {
    // Emoji character encoded as surrogate pair: U+1F600 (GRINNING FACE) -> 0xD83D 0xDE00
    std::wstring text = L"Hello ";
    text.push_back(static_cast<wchar_t>(0xD83D));
    text.push_back(static_cast<wchar_t>(0xDE00));
    text += L" World";

    // Index 6 is high surrogate 0xD83D, index 7 is low surrogate 0xDE00
    // If we point at low surrogate (7), SafeStreamingPartialSubstringStart moves back to high surrogate (6)
    size_t safePos = SafeStreamingPartialSubstringStart(text, 7);
    assert(safePos == 6);

    // If we advance from high surrogate (6), it skips both surrogates to index 8
    size_t advanced = AdvanceStreamingPartialSubstringStart(text, 6);
    assert(advanced == 8);
}

static void TestPageFormatting() {
    StreamingPartialHudState state;
    const std::wstring status = L"Listening...";

    // Short text that fits in one page
    std::wstring shortText = L"Hello world, this is a test.";
    std::wstring formatted = FormatStreamingPartialHudText(state, status, shortText);
    assert(formatted == status + L"\n" + shortText);
    assert(state.pageStart == 0);
    assert(!state.clearPageMode);

    // Text with sentence boundaries that needs pagination
    std::wstring longText;
    for (int i = 0; i < 15; ++i) {
        longText += L"这是第" + std::to_wstring(i + 1) + L"个很长很长的句子，包含若干测试文字以验证分页行为。";
    }

    formatted = FormatStreamingPartialHudText(state, status, longText);
    assert(!formatted.empty());
    // Since longText is large, pageStart should advance
    assert(state.pageStart > 0);
    assert(state.clearPageMode);
}

int main() {
    TestBoundaries();
    TestSurrogatePairs();
    TestPageFormatting();
    std::cout << "hud_pagination_test: PASS\n";
    return 0;
}
