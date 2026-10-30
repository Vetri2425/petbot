// UART link to the Linux brain: receives MOOD/ANIM, sends EVT.
#pragma once

namespace petbot {
namespace link {

// Start UART1 (config.h) and the receive task. Call once from setup().
void begin();

// Send "EVT <source> <detail>". Returns false (and logs) if the names are unknown.
bool sendEvent(const char* source, const char* detail);

}  // namespace link
}  // namespace petbot
