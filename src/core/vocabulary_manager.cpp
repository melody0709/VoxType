#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "vocabulary_manager.h"
#include "path_service.h"
#include "utils.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <format>
#include <sstream>
#include <vector>

namespace vocabulary_manager {

namespace {

std::wstring TrimW(std::wstring_view sv) {
    const size_t first = sv.find_first_not_of(L" \t\r\n");
    if (first == std::wstring_view::npos) return {};
    const size_t last = sv.find_last_not_of(L" \t\r\n");
    return std::wstring(sv.substr(first, last - first + 1));
}

void SkipWs(std::wstring_view str, size_t& pos) {
    while (pos < str.size() && (str[pos] == L' ' || str[pos] == L'\t' ||
                               str[pos] == L'\r' || str[pos] == L'\n')) {
        ++pos;
    }
}

size_t UnicodeScalarCount(std::wstring_view term) {
    size_t count = 0;
    for (size_t i = 0; i < term.size(); ++i) {
        const wchar_t ch = term[i];
        if (ch >= 0xd800 && ch <= 0xdbff && i + 1 < term.size()) {
            const wchar_t low = term[i + 1];
            if (low >= 0xdc00 && low <= 0xdfff) {
                ++i;
            }
        }
        ++count;
    }
    return count;
}

std::wstring StripComments(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    bool inString = false;
    bool escape = false;
    size_t i = 0;
    while (i < text.size()) {
        const wchar_t ch = text[i];
        if (inString) {
            out.push_back(ch);
            if (escape) {
                escape = false;
            } else if (ch == L'\\') {
                escape = true;
            } else if (ch == L'"') {
                inString = false;
            }
            ++i;
        } else {
            if (ch == L'"') {
                inString = true;
                out.push_back(ch);
                ++i;
            } else if (ch == L'/' && i + 1 < text.size() && text[i + 1] == L'/') {
                i += 2;
                while (i < text.size() && text[i] != L'\n' && text[i] != L'\r') {
                    ++i;
                }
            } else if (ch == L'/' && i + 1 < text.size() && text[i + 1] == L'*') {
                i += 2;
                while (i + 1 < text.size() && !(text[i] == L'*' && text[i + 1] == L'/')) {
                    ++i;
                }
                if (i + 1 < text.size()) i += 2;
            } else {
                out.push_back(ch);
                ++i;
            }
        }
    }
    return out;
}

bool DecodeHex4(std::wstring_view s, size_t pos, uint32_t& val) {
    if (pos + 4 > s.size()) return false;
    val = 0;
    for (size_t i = 0; i < 4; ++i) {
        wchar_t c = s[pos + i];
        int d = -1;
        if (c >= L'0' && c <= L'9') d = c - L'0';
        else if (c >= L'a' && c <= L'f') d = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') d = c - L'A' + 10;
        if (d < 0) return false;
        val = (val << 4) | static_cast<uint32_t>(d);
    }
    return true;
}

bool DecodeJsonStringW(std::wstring_view s, size_t& pos, std::wstring& out) {
    out.clear();
    SkipWs(s, pos);
    if (pos >= s.size() || s[pos] != L'"') return false;
    ++pos; // skip opening quote
    while (pos < s.size()) {
        wchar_t ch = s[pos++];
        if (ch == L'"') {
            return true;
        }
        if (ch < 0x20) {
            return false;
        }
        if (ch != L'\\') {
            out.push_back(ch);
            continue;
        }
        if (pos >= s.size()) return false;
        wchar_t esc = s[pos++];
        switch (esc) {
        case L'"': out.push_back(L'"'); break;
        case L'\\': out.push_back(L'\\'); break;
        case L'/': out.push_back(L'/'); break;
        case L'b': out.push_back(L'\b'); break;
        case L'f': out.push_back(L'\f'); break;
        case L'n': out.push_back(L'\n'); break;
        case L'r': out.push_back(L'\r'); break;
        case L't': out.push_back(L'\t'); break;
        case L'u': {
            uint32_t cp = 0;
            if (!DecodeHex4(s, pos, cp)) return false;
            pos += 4;
            if (cp >= 0xd800 && cp <= 0xdbff) {
                if (pos + 6 <= s.size() && s[pos] == L'\\' && s[pos + 1] == L'u') {
                    uint32_t low = 0;
                    if (DecodeHex4(s, pos + 2, low) && low >= 0xdc00 && low <= 0xdfff) {
                        pos += 6;
                        out.push_back(static_cast<wchar_t>(cp));
                        out.push_back(static_cast<wchar_t>(low));
                        break;
                    }
                }
                return false;
            } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                return false;
            }
            out.push_back(static_cast<wchar_t>(cp));
            break;
        }
        default:
            return false;
        }
    }
    return false;
}

void SkipJsonValue(std::wstring_view s, size_t& pos) {
    SkipWs(s, pos);
    if (pos >= s.size()) return;
    if (s[pos] == L'"') {
        std::wstring ignored;
        DecodeJsonStringW(s, pos, ignored);
        return;
    }
    if (s[pos] == L'{') {
        ++pos;
        int depth = 1;
        bool inStr = false, esc = false;
        while (pos < s.size() && depth > 0) {
            wchar_t c = s[pos++];
            if (inStr) {
                if (esc) esc = false;
                else if (c == L'\\') esc = true;
                else if (c == L'"') inStr = false;
            } else {
                if (c == L'"') inStr = true;
                else if (c == L'{') ++depth;
                else if (c == L'}') --depth;
            }
        }
        return;
    }
    if (s[pos] == L'[') {
        ++pos;
        int depth = 1;
        bool inStr = false, esc = false;
        while (pos < s.size() && depth > 0) {
            wchar_t c = s[pos++];
            if (inStr) {
                if (esc) esc = false;
                else if (c == L'\\') esc = true;
                else if (c == L'"') inStr = false;
            } else {
                if (c == L'"') inStr = true;
                else if (c == L'[') ++depth;
                else if (c == L']') --depth;
            }
        }
        return;
    }
    while (pos < s.size() && s[pos] != L',' && s[pos] != L'}' && s[pos] != L']') {
        ++pos;
    }
}

std::wstring EscapeJsonStringW(std::wstring_view val) {
    std::wstring out;
    out.reserve(val.size() + 8);
    for (wchar_t c : val) {
        switch (c) {
        case L'\\': out += L"\\\\"; break;
        case L'"':  out += L"\\\""; break;
        case L'\b': out += L"\\b"; break;
        case L'\f': out += L"\\f"; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default:
            if (c < 0x20) {
                out += std::format(L"\\u{:04x}", static_cast<uint32_t>(c));
            } else {
                out.push_back(c);
            }
            break;
        }
    }
    return out;
}

} // namespace

int WeightToScale10(int weight) {
    return std::clamp(static_cast<int>(std::round(weight / 5.0)), 1, 10);
}

float WeightToVolcengineScale(int weight) {
    const float raw = std::clamp(1.0f + (static_cast<float>(weight) / 50.0f) * 2.0f, 1.0f, 3.0f);
    return std::round(raw * 10.0f) / 10.0f;
}

int WeightToQwen(int weight) {
    if (weight >= 10) return 50;
    return std::clamp(weight, 1, 5);
}

float WeightToSherpaScore(int weight) {
    const float raw = std::clamp(1.0f + (static_cast<float>(weight) / 50.0f) * 2.0f, 1.0f, 3.0f);
    return std::round(raw * 10.0f) / 10.0f;
}

bool IsCommentKey(std::wstring_view key) {
    const size_t first = key.find_first_not_of(L" \t\r\n");
    if (first == std::wstring_view::npos) return false;
    const std::wstring_view trimmed = key.substr(first);
    return trimmed.starts_with(L"//") || trimmed.starts_with(L"#");
}

bool IsValidTerm(std::wstring_view term) {
    if (term.empty()) return false;
    for (wchar_t ch : term) {
        if (ch < 0x20) return false;
    }
    bool hasNonAscii = false;
    for (wchar_t ch : term) {
        if (static_cast<uint32_t>(ch) > 0x7f) {
            hasNonAscii = true;
            break;
        }
    }
    if (hasNonAscii) {
        return UnicodeScalarCount(term) <= 15;
    }
    size_t segments = 0;
    bool inSegment = false;
    for (wchar_t ch : term) {
        if (iswspace(ch)) {
            inSegment = false;
        } else if (!inSegment) {
            inSegment = true;
            if (++segments > 7) return false;
        }
    }
    return segments > 0 && term.size() <= 64;
}

std::expected<VocabularyList, std::wstring> ParseVocabularyJson(std::wstring_view jsonStr) {
    const std::wstring cleaned = StripComments(jsonStr);
    size_t pos = 0;
    SkipWs(cleaned, pos);
    if (pos >= cleaned.size() || cleaned[pos] != L'{') {
        return std::unexpected(L"Vocabulary JSON must begin with '{'");
    }
    ++pos; // skip '{'

    VocabularyList result;
    bool closed = false;
    while (pos < cleaned.size()) {
        SkipWs(cleaned, pos);
        if (pos < cleaned.size() && cleaned[pos] == L'}') {
            ++pos;
            closed = true;
            break;
        }

        std::wstring key;
        if (!DecodeJsonStringW(cleaned, pos, key)) {
            return std::unexpected(L"Expected valid JSON string for vocabulary key");
        }

        SkipWs(cleaned, pos);
        if (pos >= cleaned.size() || cleaned[pos] != L':') {
            return std::unexpected(L"Expected ':' after vocabulary key");
        }
        ++pos; // skip ':'

        if (IsCommentKey(key)) {
            SkipJsonValue(cleaned, pos);
        } else {
            if (!IsValidTerm(key)) {
                return std::unexpected(std::format(
                    L"Vocabulary term \"{}\" exceeds limits (max 15 Chinese chars or 7 English words)", key));
            }

            SkipWs(cleaned, pos);
            int weight = 50;
            if (pos < cleaned.size() && cleaned[pos] == L'"') {
                std::wstring weightStr;
                if (!DecodeJsonStringW(cleaned, pos, weightStr)) {
                    return std::unexpected(std::format(L"Invalid string weight for term \"{}\"", key));
                }
                const double dval = _wtof(weightStr.c_str());
                if (dval <= 0.0) {
                    return std::unexpected(std::format(L"Invalid numeric weight \"{}\" for term \"{}\"", weightStr, key));
                }
                weight = std::clamp(static_cast<int>(std::round(dval)), 1, 100);
            } else if (pos < cleaned.size() &&
                       ((cleaned[pos] >= L'0' && cleaned[pos] <= L'9') || cleaned[pos] == L'-' || cleaned[pos] == L'+')) {
                const size_t numStart = pos;
                while (pos < cleaned.size() &&
                       ((cleaned[pos] >= L'0' && cleaned[pos] <= L'9') || cleaned[pos] == L'.' ||
                        cleaned[pos] == L'e' || cleaned[pos] == L'E' || cleaned[pos] == L'+' || cleaned[pos] == L'-')) {
                    ++pos;
                }
                const std::wstring numStr(cleaned.substr(numStart, pos - numStart));
                const double dval = _wtof(numStr.c_str());
                weight = std::clamp(static_cast<int>(std::round(dval)), 1, 100);
            } else {
                return std::unexpected(std::format(L"Expected numeric weight for vocabulary term \"{}\"", key));
            }

            // Upsert into result
            auto it = std::find_if(result.begin(), result.end(),
                                   [&key](const VocabularyEntry& e) { return e.word == key; });
            if (it != result.end()) {
                it->weight = weight;
            } else {
                if (result.size() >= 2000) {
                    return std::unexpected(L"Vocabulary exceeds the maximum limit of 2000 entries");
                }
                result.push_back({ std::move(key), weight });
            }
        }

        SkipWs(cleaned, pos);
        if (pos < cleaned.size() && cleaned[pos] == L',') {
            ++pos;
        } else if (pos < cleaned.size() && cleaned[pos] == L'}') {
            ++pos;
            closed = true;
            break;
        } else {
            return std::unexpected(L"Expected ',' or '}' in vocabulary JSON");
        }
    }

    if (!closed) {
        return std::unexpected(L"Vocabulary JSON missing closing '}'");
    }

    SkipWs(cleaned, pos);
    if (pos != cleaned.size()) {
        return std::unexpected(L"Unexpected trailing content after vocabulary JSON");
    }

    return result;
}

std::expected<VocabularyList, std::wstring> ParseVocabularyLines(std::wstring_view text) {
    VocabularyList result;
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find_first_of(L"\r\n", lineStart);
        if (lineEnd == std::wstring_view::npos) lineEnd = text.size();

        std::wstring line = TrimW(text.substr(lineStart, lineEnd - lineStart));
        lineStart = lineEnd + 1;
        if (lineStart < text.size() && text[lineEnd] == L'\r' && text[lineStart] == L'\n') {
            ++lineStart;
        }

        if (line.empty() || line.starts_with(L"//") || line.starts_with(L"#")) {
            continue;
        }

        std::wstring word;
        int weight = 50;

        if (line.front() == L'"') {
            const size_t closeQuote = line.find(L'"', 1);
            if (closeQuote != std::wstring::npos) {
                word = line.substr(1, closeQuote - 1);
                std::wstring rest = TrimW(line.substr(closeQuote + 1));
                if (!rest.empty() && (rest.front() == L':' || rest.front() == L',' || rest.front() == L'=')) {
                    rest = TrimW(rest.substr(1));
                }
                if (!rest.empty()) {
                    const double w = _wtof(rest.c_str());
                    if (w > 0.0) weight = std::clamp(static_cast<int>(std::round(w)), 1, 100);
                }
            } else {
                word = line.substr(1);
            }
        } else {
            const size_t delim = line.find_last_of(L" \t:,=");
            if (delim != std::wstring::npos && delim > 0) {
                std::wstring left = TrimW(line.substr(0, delim));
                std::wstring right = TrimW(line.substr(delim + 1));
                bool rightIsNumber = !right.empty();
                int dotCount = 0;
                for (wchar_t c : right) {
                    if (c == L'.') {
                        ++dotCount;
                        if (dotCount > 1) { rightIsNumber = false; break; }
                    } else if (c < L'0' || c > L'9') {
                        rightIsNumber = false;
                        break;
                    }
                }
                if (rightIsNumber) {
                    word = left;
                    const size_t wordDelim = word.find_last_of(L":,=");
                    if (wordDelim != std::wstring::npos && wordDelim == word.size() - 1) {
                        word = TrimW(word.substr(0, wordDelim));
                    }
                    const double dval = _wtof(right.c_str());
                    if (dval > 0.0) {
                        weight = std::clamp(static_cast<int>(std::round(dval)), 1, 100);
                    }
                } else {
                    word = line;
                }
            } else {
                word = line;
            }
        }

        word = TrimW(word);
        if (word.empty() || IsCommentKey(word)) continue;

        if (!IsValidTerm(word)) {
            return std::unexpected(std::format(
                L"Vocabulary term \"{}\" exceeds limits (max 15 Chinese chars or 7 English words)", word));
        }

        auto it = std::find_if(result.begin(), result.end(),
                               [&word](const VocabularyEntry& e) { return e.word == word; });
        if (it != result.end()) {
            it->weight = weight;
        } else {
            if (result.size() >= 2000) {
                return std::unexpected(L"Vocabulary exceeds the maximum limit of 2000 entries");
            }
            result.push_back({ std::move(word), weight });
        }
    }

    return result;
}

std::expected<VocabularyList, std::wstring> ParseVocabularyText(std::wstring_view text) {
    const std::wstring trimmed = TrimW(text);
    if (trimmed.empty()) return VocabularyList{};
    if (trimmed.starts_with(L"{")) {
        return ParseVocabularyJson(trimmed);
    }
    return ParseVocabularyLines(trimmed);
}

bool ValidateVocabulary(std::wstring_view text, std::wstring* error) {
    const std::wstring trimmed = TrimW(text);
    if (trimmed.empty()) return true;
    auto res = ParseVocabularyText(trimmed);
    if (!res) {
        if (error) *error = res.error();
        return false;
    }
    return true;
}

std::string FormatVocabularyJson(const VocabularyList& entries, bool pretty) {
    if (entries.empty()) return "{}";
    std::string out = pretty ? "{\r\n" : "{";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (pretty) out += "  ";
        out += "\"" + WideToUtf8(EscapeJsonStringW(entries[i].word)) + "\": " +
               std::to_string(entries[i].weight);
        if (i + 1 < entries.size()) out += ",";
        if (pretty) out += "\r\n";
    }
    out += "}";
    return out;
}

std::string TranspileToQwenJson(const VocabularyList& entries) {
    if (entries.empty()) return {};
    std::string out = "{";
    size_t superCount = 0;
    size_t count = 0;
    for (const auto& entry : entries) {
        if (count >= 2000) break;
        int weight = WeightToQwen(entry.weight);
        if (weight == 50) {
            if (superCount < 50) {
                ++superCount;
            } else {
                weight = 5;
            }
        }
        if (count > 0) out += ",";
        out += "\"" + WideToUtf8(EscapeJsonStringW(entry.word)) + "\":" + std::to_string(weight);
        ++count;
    }
    out += "}";
    return out;
}

std::string TranspileToVolcengineHotwordsJson(const VocabularyList& entries) {
    if (entries.empty()) return "[]";
    std::string out = "[";
    size_t count = 0;
    for (const auto& entry : entries) {
        if (count > 0) out += ",";
        const float scale = WeightToVolcengineScale(entry.weight);
        char scaleBuf[32];
        snprintf(scaleBuf, sizeof(scaleBuf), "%.1f", scale);
        out += "{\"word\":\"" + WideToUtf8(EscapeJsonStringW(entry.word)) + "\",\"scale\":" + scaleBuf + "}";
        ++count;
    }
    out += "]";
    return out;
}

std::wstring BuildVolcengineContextJson(
    const VocabularyList& entries,
    const std::wstring& inputFieldText,
    const std::vector<std::wstring>& history) {

    const bool hasHotwords = !entries.empty();
    const bool hasInput = !inputFieldText.empty();
    const bool hasHistory = !history.empty();

    if (!hasHotwords && !hasInput && !hasHistory) {
        return L"";
    }

    std::wstring json = L"{";
    bool needComma = false;

    if (hasHotwords) {
        json += L"\"hotwords\":[";
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i > 0) json += L",";
            const float scale = WeightToVolcengineScale(entries[i].weight);
            wchar_t scaleBuf[32];
            swprintf_s(scaleBuf, L"%.1f", scale);
            json += L"{\"word\":\"" + EscapeJsonStringW(entries[i].word) + L"\",\"scale\":" + scaleBuf + L"}";
        }
        json += L"]";
        needComma = true;
    }

    if (hasInput || hasHistory) {
        if (needComma) json += L",";
        json += L"\"context_type\":\"dialog_ctx\",\"context_data\":[";
        int idx = 0;
        if (hasInput) {
            json += L"{\"text\":\"" + EscapeJsonStringW(inputFieldText) + L"\"}";
            idx++;
        }
        for (const auto& item : history) {
            if (idx > 0) json += L",";
            json += L"{\"text\":\"" + EscapeJsonStringW(item) + L"\"}";
            idx++;
        }
        json += L"]";
    }

    json += L"}";
    return json;
}

std::string TranspileToSherpaHotwords(const VocabularyList& entries) {
    std::string out;
    for (const auto& entry : entries) {
        const float score = WeightToSherpaScore(entry.weight);
        char buf[32];
        snprintf(buf, sizeof(buf), " : %.1f\n", score);
        out += WideToUtf8(entry.word) + buf;
    }
    return out;
}

std::wstring GetVocabularyFilePath() {
    return PathService::VocabularyPath();
}

bool EnsureVocabularyFileTemplate(const std::wstring& path) {
    const std::wstring target = path.empty() ? GetVocabularyFilePath() : path;
    const DWORD attr = GetFileAttributesW(target.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    const size_t slash = target.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        CreateDirectoryW(target.substr(0, slash).c_str(), nullptr);
    }
    const std::wstring templateJson =
        L"{\r\n"
        L"  \"// 说明\": \"支持人名、专有名词、公司术语。权重可选 1-5 或 50（50 为强制优先，最多 50 项；总计最多 2000 项）\",\r\n"
        L"  \"何启煊\": 50,\r\n"
        L"  \"何燮煊\": 50,\r\n"
        L"  \"何悦滢\": 50,\r\n"
        L"  \"李协煊\": 50\r\n"
        L"}\r\n";
    auto writeRes = WriteVocabularyFile(templateJson, target);
    return writeRes.has_value();
}

std::expected<std::wstring, std::wstring> ReadVocabularyFile(const std::wstring& path) {
    const std::wstring target = path.empty() ? GetVocabularyFilePath() : path;
    HANDLE hFile = CreateFileW(
        target.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return std::unexpected(L"Cannot open vocabulary file");
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(hFile, &size) || size.QuadPart > 10 * 1024 * 1024) {
        CloseHandle(hFile);
        return std::unexpected(L"Vocabulary file is missing or exceeds 10MB");
    }
    std::string utf8(static_cast<size_t>(size.QuadPart), '\0');
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, utf8.data(), static_cast<DWORD>(utf8.size()), &bytesRead, nullptr)) {
        CloseHandle(hFile);
        return std::unexpected(L"Failed to read vocabulary file contents");
    }
    CloseHandle(hFile);
    utf8.resize(bytesRead);
    // Strip UTF-8 BOM if present
    if (utf8.size() >= 3 &&
        static_cast<unsigned char>(utf8[0]) == 0xef &&
        static_cast<unsigned char>(utf8[1]) == 0xbb &&
        static_cast<unsigned char>(utf8[2]) == 0xbf) {
        utf8.erase(0, 3);
    }
    return Utf8ToWide(utf8);
}

std::expected<void, std::wstring> WriteVocabularyFile(const std::wstring& content, const std::wstring& path) {
    const std::wstring target = path.empty() ? GetVocabularyFilePath() : path;
    const size_t slash = target.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        CreateDirectoryW(target.substr(0, slash).c_str(), nullptr);
    }
    HANDLE hFile = CreateFileW(
        target.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return std::unexpected(L"Cannot create vocabulary file for writing");
    }
    const std::string utf8 = WideToUtf8(content);
    DWORD bytesWritten = 0;
    if (!WriteFile(hFile, utf8.data(), static_cast<DWORD>(utf8.size()), &bytesWritten, nullptr) ||
        bytesWritten != utf8.size()) {
        CloseHandle(hFile);
        return std::unexpected(L"Failed to write vocabulary file data");
    }
    CloseHandle(hFile);
    return {};
}

std::wstring BuildLlmVocabularySection(const VocabularyList& entries) {
    if (entries.empty()) return L"";

    std::vector<size_t> order(entries.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&entries](size_t left, size_t right) {
        return entries[left].weight > entries[right].weight;
    });
    const size_t kept = std::min(order.size(), kLlmVocabularyMaxEntries);

    std::wstring section = L"【用户词表】以下是用户确认过的正确写法，其优先级高于你的常识：{";
    for (size_t i = 0; i < kept; ++i) {
        if (i != 0) section += L", ";
        section += entries[order[i]].word;
    }
    section += L"}\n";
    section += L"这些写法若出现在输入中，一律视为已经正确，不得改动其拼写、大小写或写法。";
    if (kept < order.size()) section += L"（已截断）";
    return section;
}

VocabularyList GetEffectiveVocabularyEntries(const std::wstring& fallbackConfigVocab) {
    auto fileRes = ReadVocabularyFile();
    if (fileRes) {
        auto parsed = ParseVocabularyText(*fileRes);
        if (parsed && !parsed->empty()) {
            return *parsed;
        }
    }
    if (!fallbackConfigVocab.empty()) {
        auto parsed = ParseVocabularyText(fallbackConfigVocab);
        if (parsed && !parsed->empty()) {
            return *parsed;
        }
    }
    return {};
}

std::wstring GetEffectiveQwenVocabulary(const std::wstring& fallbackConfigVocab) {
    auto entries = GetEffectiveVocabularyEntries(fallbackConfigVocab);
    if (entries.empty()) return L"";
    std::string qwenJson = TranspileToQwenJson(entries);
    return Utf8ToWide(qwenJson);
}

} // namespace vocabulary_manager
