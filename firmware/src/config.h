// PetBot firmware configuration.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace petbot {
namespace config {

// UART link to the Linux brain (protocol/PROTOCOL.md, ADR 0005).
// Pins: TBD until PCB/wiring. GPIO17/18 are the ESP32-S3 default UART1 pins.
const int kLinkTxPin = 17;
const int kLinkRxPin = 18;
const uint32_t kLinkBaud = 921600;
const size_t kLinkRxBufferBytes = 1024;  // driver RX buffer, ~11 ms at 921600 baud

// USB serial (log output and debug keys).
const uint32_t kUsbBaud = 115200;

}  // namespace config
}  // namespace petbot
