// ST7789 display + LVGL face, running in its own FreeRTOS task.
#pragma once

#include "protocol.h"

namespace petbot {
namespace display {

// Start the face task. Never blocks; if the display can't start (no memory), it logs
// and the rest of the firmware (link, debug keys) keeps running.
void begin();

// Hand a MOOD or ANIM to the face. Safe to call from any task: it only enqueues,
// LVGL is called from the face task alone. Drops (with a warning) if the queue is full.
void post(const protocol::Message& msg);

}  // namespace display
}  // namespace petbot
