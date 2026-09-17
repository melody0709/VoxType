#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "debug_logger.h"

#include <windows.h>
#include <cstdio>
#include <mutex>
#include <vector>

namespace debug_log {
namespace {

std::mutex g_logMutex;

} // namespace

void WriteV(const char* format, va_list args) {
    if (!format) return;

    va_list argsCopy;
    va_copy(argsCopy, args);
    const int required = vsnprintf(nullptr, 0, format, argsCopy);
    va_end(argsCopy);

    if (required <= 0) return;

    std::vector<char> buffer(static_cast<size_t>(required) + 2, '\0');
    vsnprintf(buffer.data(), buffer.size(), format, args);
    buffer[required] = '\n';
    buffer[required + 1] = '\0';

    std::lock_guard<std::mutex> lock(g_logMutex);
    OutputDebugStringA(buffer.data());
}

void Write(const char* format, ...) {
    va_list args;
    va_start(args, format);
    WriteV(format, args);
    va_end(args);
}

} // namespace debug_log
