#include "hud_pagination.h"

#include <algorithm>
#include <atomic>
#include <cwctype>

static std::atomic<HudLineCounter> s_defaultLineCounter{nullptr};

void SetDefaultHudLineCounter(HudLineCounter counter) {
    s_defaultLineCounter.store(counter, std::memory_order_release);
}

HudLineCounter GetDefaultHudLineCounter() {
    return s_defaultLineCounter.load(std::memory_order_acquire);
}

bool IsStreamingPartialStrongBoundary(wchar_t c) {
    return c == L'\n' || c == L'\r' ||
           c == L'。' || c == L'！' || c == L'？' ||
           c == L'!' || c == L'?' ||
           c == L'；' || c == L';';
}

bool IsStreamingPartialSoftBoundary(wchar_t c) {
    return c == L'，' || c == L',' || c == L'、' || c == L'：' || c == L':';
}

size_t SkipStreamingPartialLeadingSeparators(const std::wstring& text, size_t pos) {
    while (pos < text.size() &&
           (iswspace(text[pos]) ||
            IsStreamingPartialStrongBoundary(text[pos]) ||
            IsStreamingPartialSoftBoundary(text[pos]))) {
        ++pos;
    }
    return pos;
}

size_t SafeStreamingPartialSubstringStart(const std::wstring& text, size_t start) {
    if (start > 0 && start < text.size() && text[start] >= 0xDC00 && text[start] <= 0xDFFF) {
        --start;
    }
    return start;
}

size_t AdvanceStreamingPartialSubstringStart(const std::wstring& text, size_t start) {
    if (start >= text.size()) return text.size();
    ++start;
    if (start < text.size() && text[start] >= 0xDC00 && text[start] <= 0xDFFF) {
        ++start;
    }
    return (std::min)(start, text.size());
}

std::wstring BuildStreamingPartialHudText(const std::wstring& statusLine, const std::wstring& body) {
    return body.empty()
        ? statusLine
        : statusLine + L"\n" + body;
}

uint32_t EstimateStreamingPartialHudLineCount(const std::wstring& statusLine, const std::wstring& body) {
    (void)statusLine;
    return static_cast<uint32_t>(1 + (body.size() + 43) / 44);
}

uint32_t StreamingPartialHudLineCount(const std::wstring& statusLine, const std::wstring& body, HudLineCounter lineCounter) {
    if (!lineCounter) {
        lineCounter = GetDefaultHudLineCounter();
    }
    if (lineCounter) {
        uint32_t lines = lineCounter(statusLine, body);
        if (lines > 0) return lines;
    }
    return EstimateStreamingPartialHudLineCount(statusLine, body);
}

std::wstring BuildStreamingHudPageBody(const std::wstring& text, size_t pageStart) {
    pageStart = (std::min)(pageStart, text.size());
    pageStart = SafeStreamingPartialSubstringStart(text, pageStart);
    return text.substr(pageStart);
}

bool StreamingHudPageFits(const std::wstring& statusLine, const std::wstring& text, size_t pageStart, HudLineCounter lineCounter) {
    return StreamingPartialHudLineCount(statusLine, BuildStreamingHudPageBody(text, pageStart), lineCounter) <=
           static_cast<uint32_t>(kStreamingPartialHudMaxLines);
}

size_t FindStreamingCurrentSentenceStart(const std::wstring& statusLine,
                                        const std::wstring& text,
                                        size_t pageStart,
                                        HudLineCounter lineCounter) {
    if (text.empty()) return 0;
    pageStart = (std::min)(pageStart, text.size());

    size_t scanEnd = text.size();
    while (scanEnd > pageStart && iswspace(text[scanEnd - 1])) {
        --scanEnd;
    }
    size_t contentEnd = scanEnd;
    while (scanEnd > pageStart &&
           (IsStreamingPartialStrongBoundary(text[scanEnd - 1]) ||
            IsStreamingPartialSoftBoundary(text[scanEnd - 1]))) {
        --scanEnd;
    }

    for (size_t i = scanEnd; i > pageStart; --i) {
        if (IsStreamingPartialStrongBoundary(text[i - 1])) {
            return SkipStreamingPartialLeadingSeparators(text, i);
        }
    }

    for (size_t i = scanEnd; i > pageStart; --i) {
        if (IsStreamingPartialSoftBoundary(text[i - 1])) {
            return SkipStreamingPartialLeadingSeparators(text, i);
        }
    }

    size_t candidate = contentEnd > kStreamingPartialHudTailChars
        ? contentEnd - kStreamingPartialHudTailChars
        : pageStart + 1;
    candidate = (std::min)(candidate, contentEnd);
    if (candidate <= pageStart && pageStart < text.size()) {
        candidate = pageStart + 1;
    }
    candidate = SafeStreamingPartialSubstringStart(text, candidate);
    candidate = SkipStreamingPartialLeadingSeparators(text, candidate);

    while (candidate < contentEnd && !StreamingHudPageFits(statusLine, text, candidate, lineCounter)) {
        candidate = AdvanceStreamingPartialSubstringStart(text, candidate);
        candidate = SkipStreamingPartialLeadingSeparators(text, candidate);
    }
    return (std::min)(candidate, text.size());
}

std::wstring FormatStreamingPartialHudText(StreamingPartialHudState& state,
                                          const std::wstring& statusLine,
                                          const std::wstring& text,
                                          HudLineCounter lineCounter) {
    if (state.pageStart > text.size()) {
        const bool keepFixedHeight = state.fixedHeightMode;
        state = {};
        state.fixedHeightMode = keepFixedHeight;
    }

    if (!state.clearPageMode &&
        StreamingPartialHudLineCount(statusLine, text, lineCounter) <= static_cast<uint32_t>(kStreamingPartialHudMaxLines)) {
        return BuildStreamingPartialHudText(statusLine, text);
    }

    if (!StreamingHudPageFits(statusLine, text, state.pageStart, lineCounter)) {
        state.fixedHeightMode = true;
        state.pageStart = FindStreamingCurrentSentenceStart(statusLine, text, state.pageStart, lineCounter);
    }

    state.clearPageMode = state.pageStart > 0;
    if (state.clearPageMode) {
        state.fixedHeightMode = true;
    }
    state.body = BuildStreamingHudPageBody(text, state.pageStart);
    while (state.pageStart < text.size() &&
           StreamingPartialHudLineCount(statusLine, state.body, lineCounter) > static_cast<uint32_t>(kStreamingPartialHudMaxLines)) {
        state.fixedHeightMode = true;
        state.pageStart = AdvanceStreamingPartialSubstringStart(text, state.pageStart);
        state.pageStart = SkipStreamingPartialLeadingSeparators(text, state.pageStart);
        state.body = BuildStreamingHudPageBody(text, state.pageStart);
    }
    return BuildStreamingPartialHudText(statusLine, state.body);
}
