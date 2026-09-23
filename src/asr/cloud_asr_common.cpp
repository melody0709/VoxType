#include "cloud_asr_common.h"

#include <algorithm>

CloudAsrReplayBuffer::CloudAsrReplayBuffer(size_t maxBytes)
    : maxBytes_(maxBytes) {
    data_.reserve((std::min)(maxBytes_, static_cast<size_t>(32000)));
}

CloudReplayAppendResult CloudAsrReplayBuffer::Append(const std::vector<BYTE>& chunk) {
    if (chunk.empty()) return CloudReplayAppendResult::IgnoredEmpty;
    if (!available_) return CloudReplayAppendResult::Disabled;

    if (data_.size() + chunk.size() > maxBytes_) {
        available_ = false;
        return CloudReplayAppendResult::LimitExceeded;
    }

    data_.insert(data_.end(), chunk.begin(), chunk.end());
    return CloudReplayAppendResult::Stored;
}

namespace {

double EffectiveAudioMs(double recordingMs, size_t pcmBytes) {
    return pcmBytes > 0
        ? static_cast<double>(pcmBytes) / kPcm16k16MonoBytesPerMs
        : recordingMs;
}

} // namespace

DWORD ComputeCloudAsrLegacyFinalizeTimeoutMs(double recordingMs, size_t pcmBytes) {
    const double audioMs = EffectiveAudioMs(recordingMs, pcmBytes);
    const DWORD adaptive = static_cast<DWORD>(audioMs * 0.8 + 6000.0);
    return std::clamp<DWORD>(adaptive, 8000, 30000);
}

DWORD ComputeCloudAsrStreamingFinalWaitMs(double recordingMs, size_t pcmBytes) {
    const double audioMs = EffectiveAudioMs(recordingMs, pcmBytes);
    const DWORD adaptive = static_cast<DWORD>(audioMs * 0.25 + 4500.0);
    return std::clamp<DWORD>(adaptive, 6000, 12000);
}

DWORD ComputeCloudAsrRecordedRequestTimeoutMs(double recordingMs, size_t pcmBytes) {
    const double audioMs = pcmBytes > 0
        ? static_cast<double>(pcmBytes) / kPcm16k16MonoBytesPerMs
        : recordingMs;
    const DWORD adaptive = static_cast<DWORD>(audioMs * 0.8 + 6000.0);
    return std::clamp<DWORD>(adaptive, 8000, 30000);
}

DWORD ComputeCloudAsrFinalizeTimeoutMs(double recordingMs, size_t pcmBytes) {
    return ComputeCloudAsrLegacyFinalizeTimeoutMs(recordingMs, pcmBytes);
}

DWORD ComputeCloudAsrPostStopWatchdogMs(bool fallbackEnabled,
                                        double recordingMs,
                                        size_t pcmBytes,
                                        DWORD retryReserveMs) {
    const DWORD primaryWait = fallbackEnabled
        ? ComputeCloudAsrStreamingFinalWaitMs(recordingMs, pcmBytes)
        : ComputeCloudAsrLegacyFinalizeTimeoutMs(recordingMs, pcmBytes);
    // 先封顶再取 max：不能写成 std::clamp(total, primaryWait, cap) ——
    // clamp 要求 lo <= hi，而一旦将来 primaryWait 的取值上限被抬高到 cap 之上，
    // 那种写法就是 UB。这里显式表达"封顶但绝不低于 primaryWait"。
    const DWORD capped = (std::min)(primaryWait + retryReserveMs,
                                    kCloudAsrPostStopWatchdogMaxMs);
    return (std::max)(primaryWait, capped);
}

DWORD EstimateCloudAsrReplaySendMs(size_t replayBytes) {
    if (replayBytes == 0) return 0;
    return static_cast<DWORD>(
        static_cast<double>(replayBytes) / kPcm16k16MonoBytesPerMs);
}

bool CloudAsrReplayFitsInBudget(DWORD remainingBudgetMs,
                               size_t replayBytes,
                               DWORD retryWaitMs,
                               DWORD safetyMs) {
    const ULONGLONG needed =
        static_cast<ULONGLONG>(EstimateCloudAsrReplaySendMs(replayBytes)) +
        retryWaitMs + safetyMs;
    return needed <= remainingBudgetMs;
}

bool IsShortClosedWithoutText(bool streamingMode,
                              bool finalTextEmpty,
                              bool originalServerClosed,
                              size_t replayBytes,
                              size_t shortRetrySkipBytes) {
    return streamingMode
        && finalTextEmpty
        && originalServerClosed
        && replayBytes <= shortRetrySkipBytes;
}

bool ShouldRetryEmptyCloudFinal(bool streamingMode,
                                bool finalTextEmpty,
                                bool replayAvailable,
                                bool replayEmpty,
                                bool shortClosedWithoutText) {
    return streamingMode
        && finalTextEmpty
        && replayAvailable
        && !replayEmpty
        && !shortClosedWithoutText;
}
