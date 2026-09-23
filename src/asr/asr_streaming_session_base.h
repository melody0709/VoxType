#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "asr_dispatcher.h"
#include "asr_streaming_session.h"
#include "asr_diagnostics.h"
#include "asr_runtime_log.h"
#include "app_messages.h"
#include "cloud_asr_common.h"

#include <string>
#include <utility>

class StreamingAsrSessionBase : public IStreamingAsrSession {
public:
    StreamingAsrSessionBase(Config config,
                            HWND targetWindow,
                            AsrLlmRefineFn refineFn,
                            std::wstring* lastRawAsrText)
        : config_(std::move(config)),
          targetWindow_(targetWindow),
          refineFn_(refineFn),
          lastRawAsrText_(lastRawAsrText) {}

    void SetPartialCallback(AsrPartialCallback cb, void* userData) override {
        partialCallback_ = cb;
        partialUserData_ = userData;
    }

    void SetFinalCallback(AsrFinalCallback cb, void* userData) override {
        finalCallback_ = cb;
        finalUserData_ = userData;
    }

protected:
    void NotifyStatus(const std::wstring& status) const {
        if (!targetWindow_) return;
        auto* message = new std::wstring(status);
        if (!PostMessageW(targetWindow_, kHudUpdateMessage,
                          static_cast<WPARAM>(config_.asrAttemptId),
                          reinterpret_cast<LPARAM>(message))) {
            delete message;
        }
    }

    void NotifyPartial(const std::wstring& text, bool isFinal) const {
        if (partialCallback_) {
            partialCallback_(text, isFinal, partialUserData_);
        }
    }

    void DispatchFinal(std::wstring text,
                       bool bundledPostProcessApplied = false) {
        if (finalCallback_) {
            finalCallback_(std::move(text), config_,
                           bundledPostProcessApplied, finalUserData_);
            return;
        }
        AsrFinalMetadata metadata;
        metadata.bundledPostProcessApplied = bundledPostProcessApplied;
        DispatchAsrFinalText(targetWindow_, std::move(text), config_, refineFn_,
                             lastRawAsrText_, metadata);
    }

    const Config& ConfigRef() const { return config_; }

    // Shared diagnostic stage helpers used by streaming providers.
    // 带附加行为的后端（doubao 的 opus 标注、qwen_free 的 replay 阶段路由）
    // 保留各自的本地版本。
    audio_diagnostics::StageMetadata PrimaryStage(
        std::wstring reason = {}) const {
        return asr_diagnostics::MakeStageMetadata(config_, std::move(reason));
    }

    audio_diagnostics::StageMetadata RetryStage(unsigned index,
                                                 std::wstring reason) const {
        return asr_diagnostics::MakeRetryStageMetadata(
            config_, index, std::move(reason));
    }

    void CompletePrimary(audio_diagnostics::StageTerminal terminal) {
        audio_diagnostics::CompleteStage(
            config_.asrAttemptId, config_.asrDiagnosticStageKind,
            config_.asrDiagnosticStageIndex, terminal);
    }

    // ---- post-stop replay 预算门控 ------------------------------------------
    // 集中实现一份，避免各 session 各写一份后逐渐漂移 —— Qwen Audio 3 的历史问题
    // 正是"主窗口看门狗与 session 内层等待各算一份预算"。契约见 cloud_asr_common.h。
    //
    // totalBudgetMs：本 session CurrentWatchdogMs() 在停录后的返回值，也就是主窗口
    //               看门狗从停录起算的总上限。
    // stopTick：StopInput() 时刻的 GetTickCount64()；0 表示尚未停录。
    static DWORD RemainingPostStopBudgetMs(DWORD totalBudgetMs, ULONGLONG stopTick) {
        if (stopTick == 0) return 0;
        const ULONGLONG elapsed = GetTickCount64() - stopTick;
        return elapsed >= totalBudgetMs ? 0 : static_cast<DWORD>(totalBudgetMs - elapsed);
    }

    // 失败路径的 replay 是否值得启动。replay 保持实时节奏重发，长录音的重发本身就要
    // 几十秒，而预留只有 kCloudAsrPostStopRetryReserveMs；启动一个注定被外层看门狗
    // 抢断的重试只会把 fallback 推迟一整个预留窗口，并丢掉更精确的 provider 错误文案。
    // 装不下就跳过，让已完成的主流程错误直接交给 fallback handler。
    //
    // 只用于**失败**路径。空结果路径（final 为空但未失败）不要门控：跳过它会把一次
    // 可疑的空 final 直接变成 "No speech detected" 且不给 fallback 机会。
    bool ShouldStartFailureReplay(const char* backend, const char* site,
                                  DWORD totalBudgetMs, ULONGLONG stopTick,
                                  size_t replayBytes, DWORD retryWaitMs) {
        const DWORD remainingMs = RemainingPostStopBudgetMs(totalBudgetMs, stopTick);
        if (CloudAsrReplayFitsInBudget(remainingMs, replayBytes, retryWaitMs,
                                       kCloudAsrReplayFitSafetyMs)) {
            return true;
        }
        asr_runtime_log::Write(
            "event=replay_skipped attempt=%llu backend=%s site=%s reason=insufficient_budget remaining_ms=%lu needed_ms=%llu pcm_bytes=%zu",
            static_cast<unsigned long long>(config_.asrAttemptId),
            backend ? backend : "unknown",
            site ? site : "unknown",
            static_cast<unsigned long>(remainingMs),
            static_cast<unsigned long long>(
                static_cast<ULONGLONG>(EstimateCloudAsrReplaySendMs(replayBytes)) +
                retryWaitMs + kCloudAsrReplayFitSafetyMs),
            replayBytes);
        return false;
    }

    Config config_;
    HWND targetWindow_ = nullptr;
    AsrLlmRefineFn refineFn_ = nullptr;
    std::wstring* lastRawAsrText_ = nullptr;

private:
    AsrPartialCallback partialCallback_ = nullptr;
    void* partialUserData_ = nullptr;
    AsrFinalCallback finalCallback_ = nullptr;
    void* finalUserData_ = nullptr;
};
