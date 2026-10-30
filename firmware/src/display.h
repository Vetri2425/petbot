// ST7789 display + LVGL face, running in its own FreeRTOS task.
#pragma once

#include "protocol.h"

namespace petbot {
namespace display {

// Create the face queue and start the face task. Call before link::begin() so the
// queue exists before any MOOD/ANIM can arrive. Never blocks; if the display can't start
// (no memory), it logs and the rest of the firmware (link, debug keys) keeps running.
void begin();

// Hand a MOOD or ANIM to the face. Safe to call from any task: it only enqueues,
// LVGL is called from the face task alone. Messages that arrive while the face is
// still starting are queued, not lost. Drops (with a warning) only if the queue is full.
void post(const protocol::Message& msg);

}  // namespace display
}  // namespace petbot
