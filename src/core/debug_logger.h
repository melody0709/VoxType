#pragma once

#include <cstdarg>

namespace debug_log {

void Write(const char* format, ...);
void WriteV(const char* format, va_list args);

} // namespace debug_log
