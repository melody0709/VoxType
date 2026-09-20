#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <string_view>
#include <vector>
#include <expected>
#include <cstdint>

namespace vocabulary_manager {

struct VocabularyEntry {
    std::wstring word;
    int weight = 50;
};

using VocabularyList = std::vector<VocabularyEntry>;

// Proportional linear scaling functions (based on weight 1..50)
int WeightToScale10(int weight);
float WeightToVolcengineScale(int weight);
int WeightToQwen(int weight);
float WeightToSherpaScore(int weight);

// Validation
bool IsCommentKey(std::wstring_view key);
bool IsValidTerm(std::wstring_view term);
bool ValidateVocabulary(std::wstring_view text, std::wstring* error = nullptr);

// Parsing & Serialization
std::expected<VocabularyList, std::wstring> ParseVocabularyJson(std::wstring_view jsonStr);
std::expected<VocabularyList, std::wstring> ParseVocabularyLines(std::wstring_view text);
std::expected<VocabularyList, std::wstring> ParseVocabularyText(std::wstring_view text);

std::string FormatVocabularyJson(const VocabularyList& entries, bool pretty = true);
std::string TranspileToQwenJson(const VocabularyList& entries);
std::string TranspileToVolcengineHotwordsJson(const VocabularyList& entries);
std::wstring BuildVolcengineContextJson(
    const VocabularyList& entries,
    const std::wstring& inputFieldText = L"",
    const std::vector<std::wstring>& history = {});
std::string TranspileToSherpaHotwords(const VocabularyList& entries);

// File I/O
std::wstring GetVocabularyFilePath();
bool EnsureVocabularyFileTemplate(const std::wstring& path = L"");
std::expected<std::wstring, std::wstring> ReadVocabularyFile(const std::wstring& path = L"");
std::expected<void, std::wstring> WriteVocabularyFile(const std::wstring& content, const std::wstring& path = L"");

// Convenience Accessors
VocabularyList GetEffectiveVocabularyEntries(const std::wstring& fallbackConfigVocab = L"");
std::wstring GetEffectiveQwenVocabulary(const std::wstring& fallbackConfigVocab = L"");

} // namespace vocabulary_manager
