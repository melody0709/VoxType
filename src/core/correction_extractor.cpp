#include "correction_extractor.h"

#include <algorithm>

namespace correction_extractor {

namespace {

bool ContainsControlWhitespace(const std::wstring& text) {
    return text.find_first_of(L"\r\n\t") != std::wstring::npos;
}

bool IsPunctuation(wchar_t ch) {
    // Only used for the "the edit is nothing but punctuation" filter, so a
    // coarse ASCII/CJK check is enough. It must not reject real word edits.
    switch (ch) {
    case L'.': case L',': case L'!': case L'?': case L';': case L':':
    case L'\'': case L'"': case L'(': case L')': case L'[': case L']':
    case L'{': case L'}': case L'-': case L'_': case L'/': case L'\\':
        return true;
    default:
        break;
    }
    return (ch >= 0x3001 && ch <= 0x3003) || (ch >= 0x3008 && ch <= 0x3011) ||
           (ch >= 0x3014 && ch <= 0x301F) || (ch >= 0xFF01 && ch <= 0xFF0F) ||
           (ch >= 0xFF1A && ch <= 0xFF20) || (ch >= 0xFF3B && ch <= 0xFF65);
}

size_t PunctuationCount(const std::wstring& text) {
    return static_cast<size_t>(std::count_if(text.begin(), text.end(), IsPunctuation));
}

} // namespace

bool Extract(const std::wstring& injected,
             const std::wstring& observed,
             CorrectionPair& out,
             const Limits& limits) {
    out = CorrectionPair{};
    if (injected.empty() || observed.empty()) return false;
    if (injected == observed) return false;
    if (injected.size() < limits.minInjectedChars) return false;
    // Only the injection must be single-line. The observation may legitimately be
    // a multi-line document window, so its own line breaks are not a reason to
    // refuse; a *pair* containing them is (checked after trimming).
    if (ContainsControlWhitespace(injected)) return false;

    const size_t shorter = (std::min)(injected.size(), observed.size());
    size_t prefix = 0;
    while (prefix < shorter && injected[prefix] == observed[prefix]) ++prefix;
    size_t suffix = 0;
    while (suffix < shorter - prefix &&
           injected[injected.size() - 1 - suffix] == observed[observed.size() - 1 - suffix]) {
        ++suffix;
    }

    const std::wstring original = injected.substr(prefix, injected.size() - prefix - suffix);
    const std::wstring corrected = observed.substr(prefix, observed.size() - prefix - suffix);
    // Replacement only: a pure insertion or deletion is far more often the user
    // continuing to type than a correction of the recognized text.
    if (original.empty() || corrected.empty()) return false;
    if (ContainsControlWhitespace(original) || ContainsControlWhitespace(corrected)) return false;
    if (original.size() > limits.maxDiffChars || corrected.size() > limits.maxDiffChars) return false;
    if (original.size() > limits.maxPairChars || corrected.size() > limits.maxPairChars) return false;

    const size_t larger = (std::max)(original.size(), corrected.size());
    const size_t smaller = (std::min)(original.size(), corrected.size());
    if (smaller == 0) return false;
    if (static_cast<double>(larger) / static_cast<double>(smaller) > limits.maxLengthRatio) return false;

    const size_t diffChars = original.size() + corrected.size();
    if (static_cast<double>(diffChars) >
        static_cast<double>(injected.size()) * limits.maxDiffToInjectedRatio) {
        return false;
    }
    // An edit that is nothing but punctuation is noise (ITN/half-width churn),
    // not a learned word.
    if (PunctuationCount(original) + PunctuationCount(corrected) >= diffChars) return false;

    out.original = original;
    out.corrected = corrected;
    return true;
}

bool ExtractFromDocument(const std::wstring& injected,
                         const std::wstring& document,
                         CorrectionPair& out,
                         const Limits& limits,
                         size_t* locatedAt) {
    out = CorrectionPair{};
    if (injected.empty() || document.empty()) return false;
    if (injected.size() < limits.minInjectedChars) return false;

    const std::wstring* rawDocument = &document;
    std::wstring collapsed;
    size_t position = rawDocument->rfind(injected);
    bool anchored = position != std::wstring::npos;
    if (!anchored) {
        // Some applications normalize line endings (or strip trailing spaces)
        // before the text is committed; retry on a whitespace-collapsed copy
        // before giving up on the exact match.
        collapsed.reserve(document.size());
        for (wchar_t ch : document) {
            if (ch == L'\r' || ch == L'\n' || ch == L'\t') continue;
            collapsed.push_back(ch);
        }
        if (collapsed != document) {
            rawDocument = &collapsed;
            position = collapsed.rfind(injected);
            anchored = position != std::wstring::npos;
        }
    }
    if (!anchored) {
        // The user edited inside the recognized text, so the whole string is no
        // longer present. Anchor on its head, then on its tail, shortening the
        // anchor until one survives: a correction in the middle kills an anchor
        // that spans it, but the far side still matches. An anchor below four
        // characters is not specific enough to trust.
        const std::wstring& text = *rawDocument;
        for (size_t anchorLength = (std::min)(injected.size(), static_cast<size_t>(8));
             anchorLength >= 4 && !anchored; --anchorLength) {
            const size_t headMatch = text.rfind(injected.substr(0, anchorLength));
            if (headMatch != std::wstring::npos) {
                position = headMatch;
                anchored = true;
                break;
            }
            const size_t tailMatch = text.rfind(injected.substr(injected.size() - anchorLength));
            if (tailMatch != std::wstring::npos) {
                const size_t offset = injected.size() - anchorLength;
                position = tailMatch >= offset ? tailMatch - offset : 0;
                anchored = true;
                break;
            }
        }
        if (!anchored) return false;
    }
    const std::wstring& text = *rawDocument;
    if (position >= text.size()) return false;

    // Bound the compared region to the injection's own extent. Without this, the
    // text that follows the injection in the document (punctuation, the next
    // sentence) would be compared too and could never be trimmed away as a
    // suffix, so every real correction would look like a whole-sentence rewrite.
    // The tail probe also absorbs insertions inside the corrected text.
    size_t regionLength = (std::min)(injected.size(), text.size() - position);
    const size_t tailProbe = (std::min)(injected.size(), static_cast<size_t>(8));
    if (tailProbe >= 2) {
        const size_t tailMatch = text.find(injected.substr(injected.size() - tailProbe), position);
        if (tailMatch != std::wstring::npos) {
            const size_t regionEnd = tailMatch + tailProbe;
            if (regionEnd > position) regionLength = regionEnd - position;
        }
    }
    if (locatedAt) *locatedAt = position;

    // First compare the exact region, then allow the edit to have grown it (a
    // pure insertion lengthens the corrected text beyond the original extent).
    const size_t candidates[] = {regionLength, regionLength + limits.maxDiffChars};
    for (size_t windowLength : candidates) {
        const std::wstring window =
            text.substr(position, (std::min)(windowLength, text.size() - position));
        if (Extract(injected, window, out, limits)) return true;
    }
    out = CorrectionPair{};
    return false;
}

} // namespace correction_extractor
