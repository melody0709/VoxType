// Offline regression：停录后（post-stop）看门狗预算的算术契约。
//
// 背景：五个流式会话各自手写 CurrentWatchdogMs()，曾出现两种偏差：
//   1) Qwen Audio 3 的外层预算与它自己的"等 final"等待用同一个值，导致内部的
//      replay retry 永远跑不完（已修）；
//   2) qwen(realtime) / doubao_ime / volcengine 在启用 fallback 时外层预算**没有**
//      retry 预留，同样是"到点就 abort、retry 形同虚设"。
// 现统一为 ComputeCloudAsrPostStopWatchdogMs()：总上限 = primaryWait + retryReserve，
// 其中 primaryWait 由 fallback 是否启用决定（6~12s / 8~30s）。
//
// 本测试只覆盖算术（纯函数），不覆盖各 session 的 send/drain/retry 流程 ——
// 后者按 AGENTS.md「不要轻易做的事」不应被强行模板化。

#include "cloud_asr_common.h"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void Expect(bool condition, const std::string& description) {
    if (condition) return;
    std::cerr << "FAIL: " << description << '\n';
    ++g_failures;
}

// EffectiveAudioMs() 在 pcmBytes > 0 时优先用 pcmBytes / 32 推算音频时长，
// 因此用字节数精确构造"录音 N 毫秒"的输入。
size_t BytesForMs(double ms) {
    return static_cast<size_t>(ms * kPcm16k16MonoBytesPerMs);
}

const double kAudioCases[] = {1000.0, 3000.0, 5000.0, 10000.0, 20000.0, 30000.0, 45000.0};

} // namespace

int main() {
    // 1) 结构性不变量：外层总上限必须严格大于 session 内部的 "等 final" 等待，
    //    否则 retry 永远拿不到时间（这正是本次修复的核心）。
    for (const double ms : kAudioCases) {
        const size_t bytes = BytesForMs(ms);
        const std::string tag = "audio=" + std::to_string(static_cast<long long>(ms)) + "ms";

        const DWORD primaryFallback = ComputeCloudAsrStreamingFinalWaitMs(ms, bytes);
        const DWORD primaryLegacy = ComputeCloudAsrLegacyFinalizeTimeoutMs(ms, bytes);
        const DWORD postFallback = ComputeCloudAsrPostStopWatchdogMs(
            true, ms, bytes, kCloudAsrPostStopRetryReserveMs);
        const DWORD postLegacy = ComputeCloudAsrPostStopWatchdogMs(
            false, ms, bytes, kCloudAsrPostStopRetryReserveMs);

        Expect(postFallback > primaryFallback,
               tag + ": post-stop watchdog must exceed the fallback-aware primary wait");
        Expect(postLegacy > primaryLegacy,
               tag + ": post-stop watchdog must exceed the legacy primary wait");
        Expect(postFallback - primaryFallback >= kCloudAsrPostStopRetryReserveMs,
               tag + ": the retry reserve must survive into the post-stop budget");
        Expect(postLegacy - primaryLegacy >= kCloudAsrPostStopRetryReserveMs,
               tag + ": the retry reserve must survive into the legacy post-stop budget");
        Expect(postFallback <= kCloudAsrPostStopWatchdogMaxMs,
               tag + ": fallback post-stop budget must respect the cap");
        Expect(postLegacy <= kCloudAsrPostStopWatchdogMaxMs,
               tag + ": legacy post-stop budget must respect the cap");
        // 启用 fallback 时 primary 预算更短，总上限不应反而超过未启用 fallback 的情形。
        Expect(postFallback <= postLegacy,
               tag + ": enabling fallback must not lengthen the total cap");
        // retryReserve = 0 时不得把总上限压到 primary 之下（防外层早于内层 abort）。
        Expect(ComputeCloudAsrPostStopWatchdogMs(true, ms, bytes, 0) == primaryFallback,
               tag + ": zero reserve must still equal the primary wait, never less");
    }

    // 2) 逐值钉住公式，防止有人悄悄改常量：
    //    1s / 10s / 30s、fallback 开与关。
    Expect(ComputeCloudAsrStreamingFinalWaitMs(1000.0, BytesForMs(1000.0)) == 6000,
           "1s recording: fallback-aware primary wait is 6000ms (clamped low)");
    Expect(ComputeCloudAsrLegacyFinalizeTimeoutMs(1000.0, BytesForMs(1000.0)) == 8000,
           "1s recording: legacy primary wait is 8000ms (clamped low)");
    Expect(ComputeCloudAsrPostStopWatchdogMs(true, 1000.0, BytesForMs(1000.0),
                                             kCloudAsrPostStopRetryReserveMs) == 15000,
           "1s recording: fallback post-stop budget is 15000ms");
    Expect(ComputeCloudAsrPostStopWatchdogMs(false, 1000.0, BytesForMs(1000.0),
                                             kCloudAsrPostStopRetryReserveMs) == 17000,
           "1s recording: legacy post-stop budget is 17000ms");

    Expect(ComputeCloudAsrStreamingFinalWaitMs(10000.0, BytesForMs(10000.0)) == 7000,
           "10s recording: fallback-aware primary wait is 7000ms");
    Expect(ComputeCloudAsrLegacyFinalizeTimeoutMs(10000.0, BytesForMs(10000.0)) == 14000,
           "10s recording: legacy primary wait is 14000ms");
    Expect(ComputeCloudAsrPostStopWatchdogMs(true, 10000.0, BytesForMs(10000.0),
                                             kCloudAsrPostStopRetryReserveMs) == 16000,
           "10s recording: fallback post-stop budget is 16000ms");
    Expect(ComputeCloudAsrPostStopWatchdogMs(false, 10000.0, BytesForMs(10000.0),
                                             kCloudAsrPostStopRetryReserveMs) == 23000,
           "10s recording: legacy post-stop budget is 23000ms");

    Expect(ComputeCloudAsrStreamingFinalWaitMs(30000.0, BytesForMs(30000.0)) == 12000,
           "30s recording: fallback-aware primary wait is 12000ms (clamped high)");
    Expect(ComputeCloudAsrLegacyFinalizeTimeoutMs(30000.0, BytesForMs(30000.0)) == 30000,
           "30s recording: legacy primary wait is 30000ms (clamped high)");
    Expect(ComputeCloudAsrPostStopWatchdogMs(true, 30000.0, BytesForMs(30000.0),
                                             kCloudAsrPostStopRetryReserveMs) == 21000,
           "30s recording: fallback post-stop budget is 21000ms");
    Expect(ComputeCloudAsrPostStopWatchdogMs(false, 30000.0, BytesForMs(30000.0),
                                             kCloudAsrPostStopRetryReserveMs) == 39000,
           "30s recording: legacy post-stop budget is 39000ms");

    // 3) 上限真的生效（只有显式传入超大预留时才会触顶）。
    Expect(ComputeCloudAsrPostStopWatchdogMs(true, 45000.0, BytesForMs(45000.0), 100000)
               == kCloudAsrPostStopWatchdogMaxMs,
           "an oversized retry reserve is clamped by the post-stop cap");
    Expect(kCloudAsrPostStopWatchdogMaxMs == 45000,
           "the post-stop cap stays at 45000ms (historical Qwen Audio 3 value)");
    Expect(kCloudAsrPostStopRetryReserveMs == 9000,
           "the retry reserve stays at 9000ms (historical Qwen Audio 3 value)");

    // 4) replay 门控：重发是实时节奏的，所以"预留 9000ms 装得下多长的重试"是有限窗口。
    //    录音回放字节数 / 32 ≈ 音频时长 ms（16kHz s16le mono）。
    Expect(EstimateCloudAsrReplaySendMs(0) == 0,
           "an empty replay needs no send time");
    Expect(EstimateCloudAsrReplaySendMs(32000) == 1000,
           "32000 bytes of PCM is 1000ms of real-time replay");
    Expect(EstimateCloudAsrReplaySendMs(BytesForMs(30000.0)) == 30000,
           "a 30s recording needs about 30s to replay at real-time cadence");

    // 预算刚好装下 / 差 1ms 装不下：边界必须严格，不能靠"差不多"。
    const DWORD exactFitBudget = 1000 + 6000 + kCloudAsrReplayFitSafetyMs;
    Expect(CloudAsrReplayFitsInBudget(exactFitBudget, 32000, 6000, kCloudAsrReplayFitSafetyMs),
           "a replay that exactly consumes the remaining budget still fits");
    Expect(!CloudAsrReplayFitsInBudget(exactFitBudget - 1, 32000, 6000, kCloudAsrReplayFitSafetyMs),
           "one millisecond short of the budget must not start the replay");
    Expect(!CloudAsrReplayFitsInBudget(0, 32000, 6000, kCloudAsrReplayFitSafetyMs),
           "an exhausted budget must never start a replay");

    // ★ 回归钉：30 秒录音 + 9 秒预留 + 实时重发 ⇒ 注定被外层看门狗抢断，必须跳过。
    //   这正是"9 秒预留不足以完成较长录音的 replay"的判据来源。
    {
        const size_t replay30s = BytesForMs(30000.0);
        const DWORD retryWait30s = ComputeCloudAsrStreamingFinalWaitMs(30000.0, replay30s);
        Expect(retryWait30s == 12000, "30s recording: replay wait budget is 12000ms");
        Expect(!CloudAsrReplayFitsInBudget(kCloudAsrPostStopRetryReserveMs, replay30s,
                                           retryWait30s, kCloudAsrReplayFitSafetyMs),
               "a 30s recording can never replay inside the 9s reserve");
        // 连"早期连接失败"这种剩余预算最充裕的时刻也装不下：
        const DWORD postStop30s = ComputeCloudAsrPostStopWatchdogMs(true, 30000.0, replay30s,
                                                                    kCloudAsrPostStopRetryReserveMs);
        Expect(postStop30s == 21000, "30s recording: post-stop cap is 21000ms");
        Expect(!CloudAsrReplayFitsInBudget(postStop30s, replay30s, retryWait30s,
                                           kCloudAsrReplayFitSafetyMs),
               "a 30s recording cannot replay even from the full post-stop budget");
    }

    // 能装下的窗口确实存在，但很窄：实时重发 + 一次 retry 等待 + 安全余量都要塞进预留。
    // 以 base 不低于 6000ms 为例，窗口约在音频时长 1.5s 以内。
    {
        const size_t replay1s = BytesForMs(1000.0);
        const DWORD retryWait1s = ComputeCloudAsrStreamingFinalWaitMs(1000.0, replay1s);
        Expect(CloudAsrReplayFitsInBudget(kCloudAsrPostStopRetryReserveMs, replay1s,
                                          retryWait1s, kCloudAsrReplayFitSafetyMs),
               "a 1s recording still fits inside the retry reserve");
        const size_t replay3s = BytesForMs(3000.0);
        const DWORD retryWait3s = ComputeCloudAsrStreamingFinalWaitMs(3000.0, replay3s);
        Expect(!CloudAsrReplayFitsInBudget(kCloudAsrPostStopRetryReserveMs, replay3s,
                                           retryWait3s, kCloudAsrReplayFitSafetyMs),
               "a 3s recording already exceeds the retry reserve");
    }

    if (g_failures != 0) {
        std::cerr << "cloud_asr_timeout_test: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "cloud_asr_timeout_test: PASS\n";
    return 0;
}
