// USB serial logging shared by all tasks.
#pragma once

namespace petbot {

// Create the log mutex. Call once from setup() before starting other tasks.
void logBegin();

// Format one log line (a '\n' is appended) and write it to USB serial while
// holding a FreeRTOS mutex, so lines from different tasks never interleave.
// Lines longer than the internal buffer are truncated.
void logLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace petbot
