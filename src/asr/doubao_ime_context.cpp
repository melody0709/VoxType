#include "doubao_ime_context.h"

#include "asr_context.h"
#include "config_store.h"
#include "utils.h"
#include "vocabulary_manager.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace doubao_ime_context {

namespace {

void AppendEscapedUtf8(std::string& out, const std::string& utf8) {
    out.push_back('"');
    for (char ch : utf8) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                char buffer[8] = {};
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", ch);
                out += buffer;
            } else {
                out.push_back(ch);
            }
        }
    }
    out.push_back('"');
}

void AppendEscapedJsonString(std::string& out, const std::wstring& text) {
    AppendEscapedUtf8(out, WideToUtf8(text));
}

// Trailing newline is dropped by the official serializer before the inner JSON
// is stored in `data`.
std::wstring WithoutTrailingNewline(std::wstring text) {
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) {
        text.pop_back();
    }
    return text;
}

} // namespace

size_t CountCodePoints(const std::wstring& text) {
    size_t count = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        ++count;
        if (i + 1 < text.size()) {
            const wchar_t high = text[i];
            const wchar_t low = text[i + 1];
            if (high >= 0xD800 && high <= 0xDBFF && low >= 0xDC00 && low <= 0xDFFF) {
                ++i;  // surrogate pair = one code point
            }
        }
    }
    return count;
}

std::string EncodeBase64(const std::string& bytes) {
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    size_t index = 0;
    while (index + 3 <= bytes.size()) {
        const unsigned value = (static_cast<unsigned char>(bytes[index]) << 16) |
                               (static_cast<unsigned char>(bytes[index + 1]) << 8) |
                               static_cast<unsigned char>(bytes[index + 2]);
        out.push_back(kAlphabet[(value >> 18) & 0x3F]);
        out.push_back(kAlphabet[(value >> 12) & 0x3F]);
        out.push_back(kAlphabet[(value >> 6) & 0x3F]);
        out.push_back(kAlphabet[value & 0x3F]);
        index += 3;
    }
    const size_t remaining = bytes.size() - index;
    if (remaining == 1) {
        const unsigned value = static_cast<unsigned char>(bytes[index]) << 16;
        out.push_back(kAlphabet[(value >> 18) & 0x3F]);
        out.push_back(kAlphabet[(value >> 12) & 0x3F]);
        out += "==";
    } else if (remaining == 2) {
        const unsigned value = (static_cast<unsigned char>(bytes[index]) << 16) |
                               (static_cast<unsigned char>(bytes[index + 1]) << 8);
        out.push_back(kAlphabet[(value >> 18) & 0x3F]);
        out.push_back(kAlphabet[(value >> 12) & 0x3F]);
        out.push_back(kAlphabet[(value >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

std::string BuildHotwordsJson(const std::vector<std::wstring>& words,
                              const ContextOptions& options) {
    std::string json = "[";
    bool first = true;
    std::vector<std::wstring> emitted;
    for (const std::wstring& word : words) {
        if (emitted.size() >= options.maxHotwords) break;
        if (word.empty()) continue;
        if (std::find(emitted.begin(), emitted.end(), word) != emitted.end()) continue;
        emitted.push_back(word);
        if (!first) json += ",";
        first = false;
        json += "{\"word\":";
        AppendEscapedJsonString(json, word);
        // Official client: class comes from its dictionary metadata and the
        // frequency field is hardcoded 0 (E-017 B4); ours reuses that shape.
        json += ",\"class\":\"user\",\"frequency\":0}";
    }
    json += "]";
    return json;
}

std::string BuildAsrContextJson(const std::vector<std::wstring>& history,
                                const Config& config,
                                const ContextOptions& options) {
    const std::vector<std::wstring> turns = asr_context::ClampTurns(
        asr_context::BuildHistoryTurns(history, options.maxHistoryTurns, options.maxHistoryChars),
        asr_context::kMaxContextTurns, options.maxHistoryChars);

    const long long nowSeconds = static_cast<long long>(std::time(nullptr));
    std::string json = "{";

    json += "\"chat\":[";
    bool firstChat = true;
    for (const std::wstring& turn : turns) {
        const std::wstring text = WithoutTrailingNewline(turn);
        if (text.empty()) continue;
        if (!firstChat) json += ",";
        firstChat = false;
        std::string data;
        data += "{\"cursor_position\":";
        data += std::to_string(CountCodePoints(text));
        data += ",\"text\":";
        AppendEscapedJsonString(data, text);
        data += "}";
        json += "{\"type\":\"user_input\",\"data\":";
        AppendEscapedUtf8(json, data);
        json += ",\"time\":";
        json += std::to_string(nowSeconds);
        json += "}";
    }
    json += "],";

    const vocabulary_manager::VocabularyList entries =
        vocabulary_manager::GetEffectiveVocabularyEntries(config.qwenVocabulary,
                                                          config.asrLearningEnabled);
    std::vector<std::wstring> words;
    words.reserve(entries.size());
    for (const vocabulary_manager::VocabularyEntry& entry : entries) {
        if (!entry.word.empty()) words.push_back(entry.word);
    }
    json += "\"hotwords\":";
    json += BuildHotwordsJson(words, options);

    if (options.includeStartType) {
        json += ",\"trackingInfo\":{\"start_type\":\"shortcut_long\"}";
    }
    json += "}";
    return json;
}

std::string BuildAsrContextBase64(const std::vector<std::wstring>& history,
                                  const Config& config,
                                  const ContextOptions& options) {
    return EncodeBase64(BuildAsrContextJson(history, config, options));
}

} // namespace doubao_ime_context
