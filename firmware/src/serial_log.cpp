#include "serial_log.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

namespace petbot {

namespace {

SemaphoreHandle_t g_mutex = nullptr;
char g_buf[640];  // guarded by g_mutex; fits an escaped 128-byte line plus a prefix

}  // namespace

void logBegin() {
    if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();
}

void logLine(const char* fmt, ...) {
    // Before logBegin() only setup() runs, so no lock is needed yet.
    if (g_mutex != nullptr) xSemaphoreTake(g_mutex, portMAX_DELAY);

    const size_t cap = sizeof(g_buf) - 1;  // keep room for the '\n'
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(g_buf, cap, fmt, args);
    va_end(args);
    size_t len = n < 0 ? 0 : static_cast<size_t>(n);
    if (len >= cap) len = cap - 1;  // truncated
    g_buf[len++] = '\n';
    Serial.write(reinterpret_cast<const uint8_t*>(g_buf), len);

    if (g_mutex != nullptr) xSemaphoreGive(g_mutex);
}

}  // namespace petbot
