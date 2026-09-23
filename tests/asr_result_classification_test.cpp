// Offline regression: ASR 最终文本的分类 与 fallback 判据必须对齐。
//
// 背景（v0.9.24 起长期存在的缺陷）：
//   main_window.cpp 的流式看门狗用 session->ProviderName() 现场拼超时文案
//       std::wstring(providerName) + L" error: timeout"
//   Qwen Audio 3 的 ProviderName() 是 "Qwen Audio 3 ASR"，
//   而 asr_result_policy::LooksLikeOperationalPrefix() 白名单里写的是
//   "Qwen Audio ASR error:"（少一个 '3'）。于是
//       "Qwen Audio 3 ASR error: timeout"
//   被判成 AsrResultKind::UsableText：
//     - ShouldRunFallback() 要求 OperationalError → 回退被静默跳过；
//     - 该文案还会被当成识别结果注入焦点窗口。
//   修法是把文案构造收进 MakeAsrWatchdogTimeoutText()，前缀固定落在白名单内。
//
// 维护约定：新增 ASR 后端时必须在本文件的 kWatchdogCases / kEmittedErrorPrefixes
// 各加一行。这正是 v0.9.23 那次只给 qwen_free 补一条硬编码断言的教训 ——
// 单条断言防不住同类问题，必须表驱动。

#include "asr_result.h"
#include "asr_result_policy.h"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void Expect(bool condition, const char* description) {
    if (condition) return;
    std::cerr << "FAIL: " << description << '\n';
    ++g_failures;
}

struct WatchdogCase {
    const wchar_t* providerName;
    const wchar_t* primaryBackend;
};

// 每个流式后端 ProviderName() 的规范值，与实际源码保持一致：
//   local        src/asr/asr_session.cpp        LocalBatchSession
//   baidu        src/asr/asr_session.cpp        BaiduBatchSession
//   qwen         src/asr/qwen_streaming_session.cpp / asr_session.cpp
//   qwen(audio)  src/asr/qwen_audio_streaming_session.cpp:140
//   mimo         src/asr/asr_session.cpp        MimoBatchSession
//   mai          src/asr/asr_session.cpp        MaiBatchSession
//   doubao_ime   src/asr/doubao_ime_streaming_session.cpp:169
//   qwen_free    src/asr/qwen_free_streaming_session.cpp:204
//   volcengine   src/asr/volcengine_streaming_session.cpp:201
const WatchdogCase kWatchdogCases[] = {
    {L"Local ASR", L"local"},
    {L"Baidu Cloud", L"baidu"},
    {L"Qwen ASR", L"qwen"},
    {L"Qwen Audio 3 ASR", L"qwen"},
    {L"MiMo ASR", L"mimo"},
    {L"Microsoft MAI Transcribe 2", L"mai"},
    {L"Doubao IME", L"doubao_ime"},
    {L"Qwen IME (Free)", L"qwen_free"},
    {L"Volcano Engine", L"volcengine"},
};

// 各 session/HTTP client 实际会产出的"运维错误"前缀字面量。
// 它们与 ProviderName() 是两处独立维护的字符串，因此必须逐条断言在白名单内。
const wchar_t* kEmittedErrorPrefixes[] = {
    L"ASR failed: ",
    L"Fallback failed: ",
    L"Baidu ASR error: ",
    L"Baidu ASR failed: ",
    L"Qwen ASR error: ",
    L"Qwen ASR failed: ",
    L"Qwen Audio ASR error: ",
    L"Qwen Audio ASR failed: ",
    L"Qwen IME ASR error: ",
    L"Qwen IME (Free) error: ",
    L"Qwen IME rewrite failed: ",
    L"MiMo ASR error: ",
    L"MAI ASR error: ",
    L"Doubao IME ASR error: ",
    L"Doubao IME error: ",
    L"VolcEngine timeout",
    L"VolcEngine error: ",
};

const wchar_t* FallbackFor(const std::wstring& primaryBackend) {
    // 只要求 fallback 合法且与 primary 不同即可。
    return primaryBackend == L"mimo" ? L"local" : L"mimo";
}

Config MakeConfig(const std::wstring& primaryBackend, const wchar_t* fallbackBackend) {
    Config config;
    config.asrBackend = primaryBackend;
    config.fallbackAsrBackend = fallbackBackend;
    return config;
}

} // namespace

int main() {
    // 1) 看门狗文案：对所有后端都必须被分类为运维错误，并且能触发回退。
    //    这是本缺陷的直接回归护栏。
    for (const WatchdogCase& item : kWatchdogCases) {
        const std::wstring providerName = item.providerName;
        const std::wstring text = MakeAsrWatchdogTimeoutText(providerName);
        const Config config = MakeConfig(item.primaryBackend, FallbackFor(item.primaryBackend));

        Expect(asr_result_policy::LooksLikeOperationalPrefix(text),
               "watchdog timeout text must hit the operational prefix allow-list");
        Expect(ClassifyAsrResult(text).kind == AsrResultKind::OperationalError,
               "watchdog timeout text must classify as operational_error");
        Expect(ClassifyAsrResult(text).reason == AsrFailureReason::Timeout,
               "watchdog timeout text must be classified as a timeout");
        Expect(IsFallbackAsrEnabled(config),
               "watchdog test config must have fallback enabled");
        Expect(ShouldRunFallback(config, text, false, false),
               "watchdog timeout must start the configured fallback");

        // 锁死历史上的错误构造形态：不得再退回 "<ProviderName> error: timeout"。
        Expect(text != providerName + L" error: timeout",
               "watchdog timeout text must not reuse the ProviderName-composed form");
    }

    // 2) ProviderName 与它自己的错误前缀是两个独立字符串，容易被改飘。
    //    白名单必须覆盖全部实际会产出的前缀。
    for (const wchar_t* prefix : kEmittedErrorPrefixes) {
        const std::wstring text = std::wstring(prefix) + L"detail";
        Expect(asr_result_policy::LooksLikeOperationalPrefix(text),
               "emitted error prefix must be covered by the allow-list");
        Expect(ClassifyAsrResult(text).kind == AsrResultKind::OperationalError,
               "emitted error prefix must classify as operational_error");
    }

    // 3) 反例：真实识别文本不得被误判，否则会把用户的话当成错误。
    for (const wchar_t* speech : {L"今天下午三点开会", L"hello world", L"No speech detected"}) {
        const Config config = MakeConfig(L"qwen", L"mimo");
        const std::wstring text = speech;
        const AsrResultKind kind = ClassifyAsrResult(text).kind;
        Expect(kind != AsrResultKind::OperationalError,
               "ordinary recognition output must not be an operational error");
        Expect(!ShouldRunFallback(config, text, false, false),
               "ordinary recognition output must not start a fallback");
    }

    // 4) 既有的回退抑制条件不得被削弱。
    {
        const Config config = MakeConfig(L"qwen", L"mimo");
        const std::wstring quota = L"ASR failed: quota exhausted";
        Expect(ClassifyAsrResult(quota).kind == AsrResultKind::OperationalError,
               "quota failures are still operational errors");
        Expect(!ShouldRunFallback(config, quota, false, false),
               "quota exhaustion must not start a fallback");
        Expect(!ShouldRunFallback(config, L"ASR failed: x", true, false),
               "an already-attempted fallback must not run twice");
        Expect(!ShouldRunFallback(config, L"ASR failed: x", false, true),
               "a self-aborted or stale attempt must not start a fallback");

        const Config noFallback = MakeConfig(L"qwen", L"none");
        Expect(!IsFallbackAsrEnabled(noFallback),
               "fallback=none disables the fallback");
        Expect(!ShouldRunFallback(noFallback, L"ASR failed: x", false, false),
               "disabled fallback must never run");

        const Config selfFallback = MakeConfig(L"qwen", L"qwen");
        Expect(!IsFallbackAsrEnabled(selfFallback),
               "fallback equal to the primary backend disables the fallback");
        Expect(!ShouldRunFallback(selfFallback, L"ASR failed: x", false, false),
               "self fallback must never run");
    }

    if (g_failures != 0) {
        std::cerr << "asr_result_classification_test: " << g_failures
                  << " failure(s)\n";
        return 1;
    }
    std::cout << "asr_result_classification_test: PASS\n";
    return 0;
}
