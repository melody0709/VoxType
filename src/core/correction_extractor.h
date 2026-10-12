#pragma once

// Pure, local extraction of a user correction pair (P2 learning loop).
//
// `injected` is what VoxType typed; `observed` is what the focused document
// contains a moment later. The extractor trims the longest common prefix and
// suffix and keeps only the middle difference, so a replacement somewhere in a
// long paragraph still yields a small pair.
//
// It must refuse anything that is not a confident, local, textual replacement:
// a rejected pair only costs one learning opportunity, while a wrong pair would
// teach the recognizer a wrong habit, so every limit below is a hard filter.

#include <string>

namespace correction_extractor {

struct Limits {
    // Per-side cap on the differing middle segment.
    size_t maxDiffChars = 24;
    // observed / injected length ratio cap (pure insertions are rejected too).
    double maxLengthRatio = 3.0;
    // The injected text itself must be long enough to be a real utterance.
    size_t minInjectedChars = 2;
    // Cap on either side of the stored pair (also enforced by the store).
    size_t maxPairChars = 48;
    // The edit may not be larger than the whole injection.
    double maxDiffToInjectedRatio = 1.0;
};

struct CorrectionPair {
    std::wstring original;
    std::wstring corrected;
};

// Compares the injected text against one observation.
// Returns false when the observation must not be learned; default-constructed
// Limits are the production policy.
bool Extract(const std::wstring& injected,
             const std::wstring& observed,
             CorrectionPair& out,
             const Limits& limits = Limits{});

// Same, for a read-back of a whole document.
//
// The injector types into a document that usually holds other text, and the
// user's correction normally sits *inside* the injected sentence, so comparing
// the injected text with the entire document can never match. This helper
// locates the injected text first (exact match, then a head anchor when the user
// edited inside it) and runs Extract() on a window around that position.
// `locatedAt` reports the match offset in the document when it was located.
bool ExtractFromDocument(const std::wstring& injected,
                         const std::wstring& document,
                         CorrectionPair& out,
                         const Limits& limits = Limits{},
                         size_t* locatedAt = nullptr);

} // namespace correction_extractor
