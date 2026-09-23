#pragma once

#include "config_store.h"

#include <cstdarg>

namespace asr_runtime_log {

bool Enabled();
bool ProviderDebugEnabled();
void SetDebugModeEnabled(bool enabled);
void SetQwenFreeEnabled(bool enabled);
void SetDiagnosticAudioEnabled(bool enabled);
// 从配置同步三个开关。集中一处，避免 app 层多个入口（启动、Settings 保存后重载、
// 托盘 Debug 开关）各自漏接，导致运行时日志整条链路静默失效 —— 2026-09 曾出现
// 三个 setter 零调用点，所有 asr_runtime_log::Write() 变成空操作，
// 排查 fallback 时既看不到 event=fallback_start 也看不到 event=fallback_suppressed。
void ApplyRuntimeLogConfig(const Config& config);
void Write(const char* format, ...);
void WriteIf(bool enabled, const char* format, ...);
void WriteNamedV(const wchar_t* fileName, const char* format, va_list args);

} // namespace asr_runtime_log
