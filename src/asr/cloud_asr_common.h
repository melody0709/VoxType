#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstddef>
#include <vector>

constexpr double kPcm16k16MonoBytesPerMs = 32.0;

enum class CloudReplayAppendResult {
    IgnoredEmpty,
    Stored,
    LimitExceeded,
    Disabled,
};

class CloudAsrReplayBuffer {
public:
    explicit CloudAsrReplayBuffer(size_t maxBytes);

    CloudReplayAppendResult Append(const std::vector<BYTE>& chunk);
    bool Available() const { return available_; }
    bool Empty() const { return data_.empty(); }
    size_t Size() const { return data_.size(); }
    size_t MaxBytes() const { return maxBytes_; }
    const std::vector<BYTE>& Data() const { return data_; }

private:
    size_t maxBytes_ = 0;
    bool available_ = true;
    std::vector<BYTE> data_;
};

DWORD ComputeCloudAsrLegacyFinalizeTimeoutMs(double recordingMs, size_t pcmBytes);
DWORD ComputeCloudAsrStreamingFinalWaitMs(double recordingMs, size_t pcmBytes);
DWORD ComputeCloudAsrRecordedRequestTimeoutMs(double recordingMs, size_t pcmBytes);
DWORD ComputeCloudAsrFinalizeTimeoutMs(double recordingMs, size_t pcmBytes);

// 停录后（post-stop）主窗口看门狗预算 —— streaming primary 的**总上限**。
//
// 契约（见 .plan/feat/asr-fallback-backend.md §超时策略）：
//   * 主窗口 post-stop watchdog 是 primary 的总上限；到点即 abort primary、
//     生成 timeout 结果、再由 fallback handler 决定是否启动 fallback。
//   * session 内部"SendFinish 后等 final"只能吃 primaryWait 这一段，
//     不能等于总上限 —— 否则内部的 replay retry 永远拿不到时间。
//
// 因此总上限 = primaryWait + retryReserveMs：
//   fallbackEnabled == true  → primaryWait = ComputeCloudAsrStreamingFinalWaitMs (6~12s)
//   fallbackEnabled == false → primaryWait = ComputeCloudAsrLegacyFinalizeTimeoutMs (8~30s)
//   两种情况下都给出 retryReserveMs，因为内部 replay retry 与是否配置 fallback 无关。
//
// 注意：session 内部等待请直接用 ComputeCloudAsrStreamingFinalWaitMs /
// ComputeCloudAsrLegacyFinalizeTimeoutMs，不要复用本函数的返回值（qwen_audio 曾因此
// 把 retry 预算整段吃掉）。`qwen_free` 有 ASR + bundled LLM 两段自己的预算，不使用本函数；
// `volcengine` 使用本函数，并在其上叠加自己的 opening 守卫。
constexpr DWORD kCloudAsrPostStopRetryReserveMs = 9000;
constexpr DWORD kCloudAsrPostStopWatchdogMaxMs = 45000;
DWORD ComputeCloudAsrPostStopWatchdogMs(bool fallbackEnabled,
                                        double recordingMs,
                                        size_t pcmBytes,
                                        DWORD retryReserveMs);

// replay 必须保持与主流程相同的实时节奏（burst 上传会触发服务端背压、产生误报的
// task failure），所以重发 N 毫秒音频本身就要约 N 毫秒。这条估算决定了"预留 9000ms
// 到底能装下多长的重试"。
DWORD EstimateCloudAsrReplaySendMs(size_t replayBytes);

// 连接握手 / finish 发送 / 调度抖动的安全余量。真实耗时随网络与机器浮动，
// 这里取一个保守常数，宁可少启动一次重试，也不要启动注定被外层看门狗抢断的重试。
constexpr DWORD kCloudAsrReplayFitSafetyMs = 1500;

// replay 是否值得启动：剩余预算能否覆盖"实时重发 + 一次等 final + 安全余量"。
// 设计文档要求 retry 只能在剩余时间内完成；跑不完的重试只会白等到外层看门狗抢占，
// 反而把 fallback 推迟 9 秒。不满足时应当跳过重试、让已完成的主流程结果直接交给
// fallback handler。
bool CloudAsrReplayFitsInBudget(DWORD remainingBudgetMs,
                                size_t replayBytes,
                                DWORD retryWaitMs,
                                DWORD safetyMs);

bool IsShortClosedWithoutText(bool streamingMode,
                              bool finalTextEmpty,
                              bool originalServerClosed,
                              size_t replayBytes,
                              size_t shortRetrySkipBytes);

bool ShouldRetryEmptyCloudFinal(bool streamingMode,
                                bool finalTextEmpty,
                                bool replayAvailable,
                                bool replayEmpty,
                                bool shortClosedWithoutText);
