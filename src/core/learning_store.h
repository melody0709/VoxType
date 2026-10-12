#pragma once

// Local store of learned correction pairs (P2 learning loop).
//
// File: %LOCALAPPDATA%\VoxType\learning\corrections.json (JSONL: one object per
// line, parsed with the shared escaped-JSON helpers). The store itself performs
// no network I/O and never rewrites vocabulary.json.
//
// PRIVACY: the learned words are NOT local-only in effect. They are merged into
// the runtime vocabulary (vocabulary_manager::GetEffectiveVocabularyEntries) so a
// correction actually biases recognition, and every cloud backend that has a
// hotword channel (Qwen, Volcengine) uploads that vocabulary with the request --
// so treat a learned word as data that may leave the machine, exactly like a
// vocabulary entry the user typed. Turning `asr_learning_enabled` off stops new
// learning; deleting this file drops the existing words.
//
// Merging: learned words join at kLearnedVocabularyWeight (1, the lowest), never
// the 50 "super hotword" tier, so they can never displace explicit vocabulary in
// the character-budgeted hotword string or the capped LLM section. The store's
// own weight (kBaseWeight, kRepeatedWeight once a pair repeats) only drives its
// own eviction order.
//
// Eviction: above `maxPairs`, entries are dropped by (hits asc, lastMs asc), so
// frequently re-corrected pairs survive.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace learning_store {

struct Entry {
    std::wstring original;
    std::wstring corrected;
    int hits = 0;
    int weight = 4;
    long long lastMs = 0;
};

struct LoadingPolicy {
    size_t maxPairs = 300;
    size_t maxPairChars = 48;
};

std::wstring StorePath();

// Read-only view, best first (hits desc, lastMs desc).
std::vector<Entry> Load(const LoadingPolicy& policy = LoadingPolicy{});

// Adds (or reinforces) one pair. Returns false when the pair violates the
// policy or the file could not be written.
bool Add(const std::wstring& original,
         const std::wstring& corrected,
         long long nowMs,
         const LoadingPolicy& policy = LoadingPolicy{});

// Deletes the backing file and drops the in-memory cache.
bool Clear();

size_t Count(const LoadingPolicy& policy = LoadingPolicy{});

// Runtime-only helper: the learned words with their weight, best first.
std::vector<std::pair<std::wstring, int>> WeightedWords(size_t maxEntries,
                                                       const LoadingPolicy& policy = LoadingPolicy{});

// Test hook: redirect the backing file. Pass an empty path to restore the
// default location.
void SetPathOverrideForTests(const std::wstring& path);

} // namespace learning_store
