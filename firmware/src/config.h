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

// ST7789 2.0" 240x320 display on SPI (FSPI), shown as 320x240 landscape.
// Pins: TBD until wiring. Chosen to avoid the link UART (17/18), USB (19/20),
// octal PSRAM (35-37) and strapping pins.
const int kDisplaySckPin = 12;
const int kDisplayMosiPin = 11;
const int kDisplayCsPin = 10;
const int kDisplayDcPin = 9;
const int kDisplayRstPin = 8;
const int kDisplayBacklightPin = 7;  // HIGH = on
const uint32_t kDisplaySpiHz = 40000000;  // TBD: try higher once tested on hardware
const bool kDisplayInvertColors = true;   // most IPS ST7789 panels need this; TBD
const bool kDisplayRotate270 = false;     // 90 vs 270 depends on panel mounting; TBD

}  // namespace config
}  // namespace petbot
