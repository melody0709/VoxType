#include "learning_intake.h"

#include "asr_runtime_log.h"
#include "correction_extractor.h"
#include "injection_ledger.h"
#include "learning_store.h"

#include <algorithm>
#include <string>

namespace learning_intake {

namespace {

// Upper bound on how much of the observed document is compared. The extractor
// windows around the located injection itself, so this only guards the read-back
// against a runaway document size.
constexpr size_t kMaxObservedChars = 20000;

} // namespace

void HandleReflection(const InjectionReflection& reflection, const Config& config) {
    if (!reflection.readSucceeded) {
        asr_runtime_log::Write("event=injection_reflection_skipped reason=%s seq=%llu",
                               reflection.reason ? reflection.reason : "unknown",
                               static_cast<unsigned long long>(reflection.sequence));
        return;
    }
    if (!config.asrLearningEnabled) {
        asr_runtime_log::Write("event=injection_reflection_skipped reason=learning_disabled seq=%llu",
                               static_cast<unsigned long long>(reflection.sequence));
        return;
    }

    injection_ledger::Record record;
    if (!injection_ledger::Snapshot(record) || record.sequence != reflection.sequence) {
        asr_runtime_log::Write("event=injection_reflection_skipped reason=stale_record seq=%llu",
                               static_cast<unsigned long long>(reflection.sequence));
        return;
    }
    if (record.sensitive) {
        // Belt and braces: the worker already refuses to read such documents.
        asr_runtime_log::Write("event=injection_reflection_skipped reason=sensitive seq=%llu",
                               static_cast<unsigned long long>(reflection.sequence));
        return;
    }

    // The document usually holds more than what we typed, so the extractor first
    // locates the injection inside it and only then compares the surrounding
    // window; everything shared with the document cancels out as common context.
    size_t position = 0;
    correction_extractor::CorrectionPair pair;
    if (!correction_extractor::ExtractFromDocument(record.injectedText, reflection.observedText,
                                                   pair, correction_extractor::Limits{},
                                                   &position)) {
        asr_runtime_log::Write(
            "event=injection_reflection_skipped reason=no_local_replacement seq=%llu injected_chars=%zu observed_chars=%zu",
            static_cast<unsigned long long>(reflection.sequence), record.injectedText.size(),
            (std::min)(reflection.observedText.size(), kMaxObservedChars));
        return;
    }
    if (position > kMaxObservedChars) {
        asr_runtime_log::Write("event=injection_reflection_skipped reason=observed_too_large seq=%llu",
                               static_cast<unsigned long long>(reflection.sequence));
        return;
    }

    learning_store::LoadingPolicy policy;
    if (config.asrLearningMaxPairs > 0) {
        policy.maxPairs = static_cast<size_t>(config.asrLearningMaxPairs);
    }
    const long long nowMs = static_cast<long long>(GetTickCount64());
    if (!learning_store::Add(pair.original, pair.corrected, nowMs, policy)) {
        asr_runtime_log::Write("event=injection_reflection_skipped reason=store_write_failed seq=%llu",
                               static_cast<unsigned long long>(reflection.sequence));
        return;
    }
    asr_runtime_log::Write(
        "event=learning_pair_stored seq=%llu original_chars=%zu corrected_chars=%zu pairs=%zu",
        static_cast<unsigned long long>(reflection.sequence), pair.original.size(),
        pair.corrected.size(), learning_store::Count(policy));
}

} // namespace learning_intake
