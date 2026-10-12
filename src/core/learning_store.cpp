#include "learning_store.h"

#include "path_service.h"
#include "utils.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <mutex>

namespace learning_store {

namespace {

constexpr int kBaseWeight = 4;
constexpr int kRepeatedWeight = 5;
constexpr int kRepeatedHits = 3;

std::mutex g_mutex;
std::wstring g_pathOverride;

std::wstring EffectivePath() {
    if (!g_pathOverride.empty()) return g_pathOverride;
    return MutableDataDir() + L"\\learning\\corrections.json";
}

bool EnsureParentDirectory(const std::wstring& path) {
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) return false;
    const std::wstring dir = path.substr(0, slash);
    // Single level below the mutable data dir; the parent already exists.
    if (CreateDirectoryW(dir.c_str(), nullptr)) return true;
    const DWORD error = GetLastError();
    return error == ERROR_ALREADY_EXISTS;
}

std::string EscapeForJson(const std::wstring& text) {
    std::string utf8 = WideToUtf8(text);
    std::string out;
    out.reserve(utf8.size() + 8);
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
    return out;
}

long long FindIntAfterKey(const std::string& line, const std::string& key, long long fallback) {
    const std::string needle = "\"" + key + "\"";
    const size_t pos = line.find(needle);
    if (pos == std::string::npos) return fallback;
    size_t cursor = line.find(':', pos + needle.size());
    if (cursor == std::string::npos) return fallback;
    ++cursor;
    while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) ++cursor;
    bool negative = false;
    if (cursor < line.size() && line[cursor] == '-') {
        negative = true;
        ++cursor;
    }
    long long value = 0;
    bool any = false;
    while (cursor < line.size() && line[cursor] >= '0' && line[cursor] <= '9') {
        value = value * 10 + (line[cursor] - '0');
        ++cursor;
        any = true;
    }
    if (!any) return fallback;
    return negative ? -value : value;
}

std::vector<Entry> LoadUnlocked(const LoadingPolicy& policy) {
    std::vector<Entry> entries;
    const std::wstring path = EffectivePath();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return entries;

    std::string content;
    char buffer[4096] = {};
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        content.append(buffer, read);
    }
    CloseHandle(file);

    size_t lineStart = 0;
    while (lineStart < content.size()) {
        size_t lineEnd = content.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = content.size();
        const std::string line = content.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;
        if (line.find("\"o\"") == std::string::npos) continue;

        Entry entry;
        entry.original = ExtractJsonStringDecoded(line, "o");
        entry.corrected = ExtractJsonStringDecoded(line, "c");
        if (entry.original.empty() || entry.corrected.empty()) continue;
        if (entry.original.size() > policy.maxPairChars ||
            entry.corrected.size() > policy.maxPairChars) {
            continue;
        }
        entry.hits = static_cast<int>(FindIntAfterKey(line, "hits", 0));
        entry.lastMs = FindIntAfterKey(line, "last", 0);
        entry.weight = entry.hits >= kRepeatedHits ? kRepeatedWeight : kBaseWeight;
        entries.push_back(std::move(entry));
    }

    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.hits != b.hits) return a.hits > b.hits;
        return a.lastMs > b.lastMs;
    });
    if (entries.size() > policy.maxPairs) entries.resize(policy.maxPairs);
    return entries;
}

bool WriteUnlocked(const std::vector<Entry>& entries) {
    const std::wstring path = EffectivePath();
    if (!EnsureParentDirectory(path)) return false;

    std::string content;
    for (const Entry& entry : entries) {
        content += "{\"o\":\"" + EscapeForJson(entry.original) + "\",\"c\":\"" +
                   EscapeForJson(entry.corrected) + "\",\"hits\":" + std::to_string(entry.hits) +
                   ",\"w\":" + std::to_string(entry.weight) +
                   ",\"last\":" + std::to_string(entry.lastMs) + "}\n";
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    size_t offset = 0;
    bool ok = true;
    while (offset < content.size()) {
        DWORD written = 0;
        const DWORD request = static_cast<DWORD>((std::min<size_t>)(content.size() - offset, 1u << 20));
        if (!WriteFile(file, content.data() + offset, request, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        offset += written;
    }
    CloseHandle(file);
    return ok;
}

} // namespace

void SetPathOverrideForTests(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pathOverride = path;
}

std::wstring StorePath() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return EffectivePath();
}

std::vector<Entry> Load(const LoadingPolicy& policy) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return LoadUnlocked(policy);
}

bool Add(const std::wstring& original,
         const std::wstring& corrected,
         long long nowMs,
         const LoadingPolicy& policy) {
    if (original.empty() || corrected.empty() || original == corrected) return false;
    if (original.size() > policy.maxPairChars || corrected.size() > policy.maxPairChars) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<Entry> entries = LoadUnlocked(policy);
    bool found = false;
    for (Entry& entry : entries) {
        if (entry.original != original || entry.corrected != corrected) continue;
        ++entry.hits;
        entry.lastMs = nowMs;
        entry.weight = entry.hits >= kRepeatedHits ? kRepeatedWeight : kBaseWeight;
        found = true;
        break;
    }
    if (!found) {
        Entry entry;
        entry.original = original;
        entry.corrected = corrected;
        entry.hits = 1;
        entry.weight = kBaseWeight;
        entry.lastMs = nowMs;
        entries.push_back(std::move(entry));
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.hits != b.hits) return a.hits > b.hits;
        return a.lastMs > b.lastMs;
    });
    if (entries.size() > policy.maxPairs) entries.resize(policy.maxPairs);
    return WriteUnlocked(entries);
}

bool Clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::wstring path = EffectivePath();
    if (!DeleteFileW(path.c_str())) {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) return false;
    }
    return true;
}

size_t Count(const LoadingPolicy& policy) {
    return Load(policy).size();
}

std::vector<std::pair<std::wstring, int>> WeightedWords(size_t maxEntries,
                                                       const LoadingPolicy& policy) {
    std::vector<std::pair<std::wstring, int>> words;
    for (const Entry& entry : Load(policy)) {
        if (maxEntries != 0 && words.size() >= maxEntries) break;
        words.emplace_back(entry.corrected, entry.weight);
    }
    return words;
}

} // namespace learning_store
