#pragma once

#include "config_store.h"

#include <string>

enum class AsrResultKind {
    UsableText,
    NoSpeech,
    TooShort,
    OperationalError,
    Cancelled,
};

enum class AsrFailureReason {
    None,
    Timeout,
    Network,
    AuthOrConfig,
    ModelLoad,
    ProviderError,
    BufferOverflow,
    Unknown,
};

struct AsrResultClassification {
    AsrResultKind kind = AsrResultKind::UsableText;
    AsrFailureReason reason = AsrFailureReason::None;
};

std::wstring NormalizeAsrText(std::wstring text);
AsrResultClassification ClassifyAsrResult(const std::wstring& text);
// 看门狗超时文案的唯一构造入口。它承担的是分类契约，不是显示文案：
// 前缀必须命中 asr_result_policy::LooksLikeOperationalPrefix()，否则该文案会被
// 判成 UsableText —— 既不触发 fallback，还会被当成识别结果注入焦点窗口。
// 不要把 ProviderName() 现拼成 "<ProviderName> error: timeout" 来用：
// Qwen Audio 3 的 ProviderName() 是 "Qwen Audio 3 ASR"，与白名单里的
// "Qwen Audio ASR error:" 差一个字符，导致 v0.9.24 起超时回退一直静默失效。
std::wstring MakeAsrWatchdogTimeoutText(const std::wstring& providerName);
const char* AsrResultKindDebugName(AsrResultKind kind);
const char* AsrFailureReasonDebugName(AsrFailureReason reason);
bool IsOperationalAsrError(const std::wstring& text);
bool IsUsableAsrTextForContext(const std::wstring& text);
bool ShouldRunLlmRefine(const Config& config, const std::wstring& text);
std::wstring AsrBackendDisplayName(const Config& config);
const char* AsrBackendDebugName(const std::wstring& asrBackend);
const char* AsrBackendLogName(const std::wstring& asrBackend);
bool IsSupportedFallbackBackend(const std::wstring& backend);
bool IsFallbackAsrEnabled(const Config& config);
Config BuildFallbackConfig(const Config& primary);
bool ShouldRunFallback(const Config& primary,
                       const std::wstring& text,
                       bool fallbackAlreadyAttempted,
                       bool selfAbortOrStaleAttempt);
