#pragma once

#include <string>
#include <cstdint>
#include <cstddef>

constexpr float kStreamingPartialHudMaxWidthDip = 900.0f;
constexpr float kStreamingPartialHudMaxScreenFraction = 0.75f;
constexpr int kStreamingPartialHudMaxLines = 4;
constexpr size_t kStreamingPartialHudTailChars = 132;

struct StreamingPartialHudState {
    bool clearPageMode = false;
    bool fixedHeightMode = false;
    size_t pageStart = 0;
    std::wstring body;
};

using HudLineCounter = uint32_t(*)(const std::wstring& statusLine, const std::wstring& body);

bool IsStreamingPartialStrongBoundary(wchar_t c);
bool IsStreamingPartialSoftBoundary(wchar_t c);
size_t SkipStreamingPartialLeadingSeparators(const std::wstring& text, size_t pos);
size_t SafeStreamingPartialSubstringStart(const std::wstring& text, size_t start);
size_t AdvanceStreamingPartialSubstringStart(const std::wstring& text, size_t start);
std::wstring BuildStreamingPartialHudText(const std::wstring& statusLine, const std::wstring& body);
uint32_t EstimateStreamingPartialHudLineCount(const std::wstring& statusLine, const std::wstring& body);
uint32_t StreamingPartialHudLineCount(const std::wstring& statusLine, const std::wstring& body, HudLineCounter lineCounter = nullptr);
std::wstring BuildStreamingHudPageBody(const std::wstring& text, size_t pageStart);
bool StreamingHudPageFits(const std::wstring& statusLine, const std::wstring& text, size_t pageStart, HudLineCounter lineCounter = nullptr);
size_t FindStreamingCurrentSentenceStart(const std::wstring& statusLine,
                                         const std::wstring& text,
                                         size_t pageStart,
                                         HudLineCounter lineCounter = nullptr);
std::wstring FormatStreamingPartialHudText(StreamingPartialHudState& state,
                                          const std::wstring& statusLine,
                                          const std::wstring& text,
                                          HudLineCounter lineCounter = nullptr);

void SetDefaultHudLineCounter(HudLineCounter counter);
HudLineCounter GetDefaultHudLineCounter();
