#include "text_post_process.h"

#include "config_store.h"

#include <algorithm>
#include <vector>

namespace text_post_process {

namespace {

bool InRange(char32_t cp, char32_t lo, char32_t hi) {
    return cp >= lo && cp <= hi;
}

bool InSet(char32_t cp, std::initializer_list<char32_t> values) {
    return std::find(values.begin(), values.end(), cp) != values.end();
}

// Official AutoSpace class normalisation: digits and ASCII letters share the
// same boundary bucket, so "A1" never gets a space inserted.
int NormalizeAutoSpaceClass(int cls) {
    return cls == 3 ? 2 : cls;
}

} // namespace

namespace code_set {

bool IsPostProcessPunctuation(char32_t cp) {
    if (cp < 0x80) {
        switch (cp) {
        case 0x21: case 0x22: case 0x23:
        case 0x25: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A:
        case 0x2C: case 0x2D: case 0x2E: case 0x2F:
        case 0x3A: case 0x3B: case 0x3F: case 0x40:
        case 0x5B: case 0x5C: case 0x5D: case 0x5F:
        case 0x7B: case 0x7D:
            return true;
        default:
            return false;
        }
    }
    // Latin-1 supplement: inverted question/exclamation, section, guillemets,
    // pilcrow, middle dot (0xA1,0xA7,0xAB,0xB6,0xB7,0xBB,0xBF).
    if (InSet(cp, {0xA1, 0xA7, 0xAB, 0xB6, 0xB7, 0xBB, 0xBF})) return true;
    if (InRange(cp, 0x2010, 0x2027)) return true;
    if (InRange(cp, 0x2030, 0x205E)) return true;
    if (InRange(cp, 0x3001, 0x3003)) return true;
    if (InRange(cp, 0x3008, 0x3011)) return true;
    if (InRange(cp, 0x3014, 0x301F)) return true;
    if (cp == 0x30FB) return true;
    // Full-width forms keep the same semantics as their ASCII counterparts.
    if (InRange(cp, 0xFF01, 0xFF0A)) return true;
    if (InRange(cp, 0xFF0C, 0xFF0F)) return true;
    if (InSet(cp, {0xFF1A, 0xFF1B, 0xFF1F, 0xFF20})) return true;
    if (InRange(cp, 0xFF3B, 0xFF3D)) return true;
    if (cp == 0xFF3F) return true;
    if (InRange(cp, 0xFF5B, 0xFF5D)) return true;
    if (InRange(cp, 0xFF5F, 0xFF65)) return true;
    return false;
}

bool IsTrailingKeepPunctuation(char32_t cp) {
    // Closing quotes and paired brackets: stripping them would break pairing.
    if (InRange(cp, 0x3008, 0x3011)) return true;
    if (InRange(cp, 0x3014, 0x301B)) return true;
    if (InRange(cp, 0x301D, 0x301F)) return true;
    if (InSet(cp, {0x2018, 0x2019, 0x201C, 0x201D, 0x2039, 0x203A, 0x2045, 0x2046})) return true;
    if (InSet(cp, {0x22, 0x27, 0x28, 0x29, 0x7B, 0x7D, 0xAB, 0xBB})) return true;
    if (InSet(cp, {0xFF02, 0xFF07, 0xFF08, 0xFF09})) return true;
    if (InSet(cp, {0xFF3B, 0xFF3D, 0xFF5B, 0xFF5D, 0xFF5F, 0xFF60, 0xFF62, 0xFF63})) return true;
    return false;
}

bool IsPostProcessDigit(char32_t cp) {
    return InRange(cp, 0x30, 0x39) || InRange(cp, 0xFF10, 0xFF19);
}

bool IsTrimmedTrailingSpace(char32_t cp) {
    return InSet(cp, {0x20, 0x09, 0xA0, 0x3000});
}

bool IsAutoSpaceWhitespace(char32_t cp) {
    return InSet(cp, {0x09, 0x0A, 0x0D, 0x20, 0xA0, 0x3000});
}

int AutoSpaceClass(char32_t cp) {
    if (InRange(cp, 0x4E00, 0x9FFF) || InRange(cp, 0x3400, 0x4DBF) ||
        InRange(cp, 0xF900, 0xF9FF) || InRange(cp, 0x20000, 0x2FA1F)) {
        return 1;
    }
    if (InRange(cp, 0x30, 0x39)) return 3;
    if (InRange(cp, 0x41, 0x5A) || InRange(cp, 0x61, 0x7A)) return 2;
    return 0;
}

std::u32string Decode(std::wstring_view text) {
    std::u32string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const char32_t cp = static_cast<char32_t>(text[i]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
            const char32_t low = static_cast<char32_t>(text[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                out.push_back(0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00));
                ++i;
                continue;
            }
        }
        out.push_back(cp);
    }
    return out;
}

std::wstring Encode(std::u32string_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (char32_t cp : text) {
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<wchar_t>(cp));
            continue;
        }
        const char32_t adjusted = cp - 0x10000;
        out.push_back(static_cast<wchar_t>(0xD800 + (adjusted >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00 + (adjusted & 0x3FF)));
    }
    return out;
}

} // namespace code_set

namespace detail {

bool BypassesPunctuationPostProcess(const std::u32string& cps) {
    // Official FUN_1407755c0: text made only of digits and punctuation, with at
    // least one non-punctuation character, whose last character is '%' or '℃'
    // is passed through untouched so that amounts survive post-processing.
    if (cps.empty()) return false;
    bool sawNonPunctuation = false;
    for (char32_t cp : cps) {
        if (code_set::IsPostProcessPunctuation(cp)) continue;
        if (!code_set::IsPostProcessDigit(cp)) return false;
        sawNonPunctuation = true;
    }
    if (!sawNonPunctuation) return false;
    const char32_t last = cps.back();
    return last == 0x25 || last == 0x2103;
}

namespace {

std::u32string ToSpaceReplace(const std::u32string& cps) {
    std::u32string out;
    out.reserve(cps.size());
    for (size_t i = 0; i < cps.size(); ++i) {
        const char32_t cp = cps[i];
        if (!code_set::IsPostProcessPunctuation(cp)) {
            out.push_back(cp);
            continue;
        }
        const bool first = (i == 0);
        const bool last = (i + 1 == cps.size());
        const bool prevDigit = !first && code_set::IsPostProcessDigit(cps[i - 1]);
        const bool nextDigit = !last && code_set::IsPostProcessDigit(cps[i + 1]);
        // Digits on both sides ("1,000", "3.14") keep the punctuation.
        if (!first && !last && prevDigit && nextDigit) {
            out.push_back(cp);
            continue;
        }
        // Paired angle brackets stay readable.
        if (cp == 0x300A || cp == 0x300B) {
            out.push_back(cp);
            continue;
        }
        // Percent / degree sign directly after a digit belongs to the number.
        if (!first && prevDigit && (cp == 0x25 || cp == 0x2103)) {
            out.push_back(cp);
            continue;
        }
        out.push_back(U' ');
    }
    // Trailing whitespace is dropped; the official client carries the space in
    // the preedit suffix instead, which a one-shot injection has no use for.
    if (!out.empty() && code_set::IsTrimmedTrailingSpace(out.back())) {
        out.pop_back();
    }
    return out;
}

void StripTrailingPunctuation(std::u32string& line) {
    if (line.empty()) return;
    const char32_t last = line.back();
    if (!code_set::IsPostProcessPunctuation(last)) return;
    if (code_set::IsTrailingKeepPunctuation(last)) return;
    if (line.size() >= 2 && code_set::IsPostProcessDigit(line[line.size() - 2]) &&
        (last == 0x25 || last == 0x2103)) {
        return;
    }
    line.pop_back();
}

std::u32string ToRemoveTrailing(const std::u32string& cps) {
    std::u32string out;
    out.reserve(cps.size());
    std::u32string line;
    auto flush = [&out, &line]() {
        bool hadCarriageReturn = false;
        if (!line.empty() && line.back() == U'\r') {
            line.pop_back();
            hadCarriageReturn = true;
        }
        StripTrailingPunctuation(line);
        out += line;
        if (hadCarriageReturn) out.push_back(U'\r');
        line.clear();
    };
    for (char32_t cp : cps) {
        if (cp == U'\n') {
            flush();
            out.push_back(U'\n');
            continue;
        }
        line.push_back(cp);
    }
    flush();
    return out;
}

} // namespace

std::wstring ApplyPunctuationMode(const std::u32string& cps, PunctMode mode) {
    switch (mode) {
    case PunctMode::KeepAll:
        return code_set::Encode(cps);
    case PunctMode::SpaceReplace:
        return code_set::Encode(ToSpaceReplace(cps));
    case PunctMode::RemoveTrailing:
        return code_set::Encode(ToRemoveTrailing(cps));
    }
    return code_set::Encode(cps);
}

std::wstring ApplyInnerSpacing(const std::u32string& cps) {
    if (cps.size() < 2) return code_set::Encode(cps);
    std::u32string out;
    out.reserve(cps.size() + 4);
    out.push_back(cps[0]);
    for (size_t i = 1; i < cps.size(); ++i) {
        const int previous = NormalizeAutoSpaceClass(code_set::AutoSpaceClass(cps[i - 1]));
        const int current = NormalizeAutoSpaceClass(code_set::AutoSpaceClass(cps[i]));
        if (previous != 0 && current != 0 && previous != current) {
            out.push_back(U' ');
        }
        out.push_back(cps[i]);
    }
    return code_set::Encode(out);
}

} // namespace detail

PunctMode PunctModeFromString(std::wstring_view value) {
    if (value == voice_text_mode::kSpaceReplace) return PunctMode::SpaceReplace;
    if (value == voice_text_mode::kRemoveTrailing) return PunctMode::RemoveTrailing;
    return PunctMode::KeepAll;
}

const char* PunctModeLogName(PunctMode mode) {
    switch (mode) {
    case PunctMode::SpaceReplace: return "space_replace";
    case PunctMode::RemoveTrailing: return "remove_trailing";
    case PunctMode::KeepAll: break;
    }
    return "keep_all";
}

std::wstring Apply(std::wstring_view text, const Options& options) {
    if (text.empty()) return std::wstring(text);
    if (options.punctMode == PunctMode::KeepAll && !options.spacing) {
        return std::wstring(text);
    }
    std::u32string cps = code_set::Decode(text);
    std::wstring result;
    if (options.punctMode != PunctMode::KeepAll &&
        !detail::BypassesPunctuationPostProcess(cps)) {
        result = detail::ApplyPunctuationMode(cps, options.punctMode);
        // Re-decode: the spacing rule must see the post-processed text, exactly
        // like the official pipeline (punctuation first, AutoSpace second).
        cps = code_set::Decode(result);
    } else {
        result = std::wstring(text);
    }
    if (options.spacing) {
        result = detail::ApplyInnerSpacing(cps);
    }
    return result;
}

std::wstring ApplyVoicePostProcess(std::wstring text, const Config& config) {
    Options options;
    options.punctMode = PunctModeFromString(config.voicePunctMode);
    options.spacing = config.voiceSpacingEnabled;
    if (options.punctMode == PunctMode::KeepAll && !options.spacing) {
        return text;
    }
    return Apply(text, options);
}

} // namespace text_post_process
