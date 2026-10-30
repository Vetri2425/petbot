# 0003. Co-processor: ESP32-S3 with PlatformIO, Arduino and LVGL

Status: Accepted

## Context
The real-time side needs a display UI, servo control, touch and IMU input, and enough
memory for a smooth animated face.

## Decision
- Board: ESP32-S3 DevKitC-1 N16R8 (16MB flash, 8MB PSRAM).
- Toolchain: PlatformIO with the Arduino framework.
- Face: LVGL on a 2.0" ST7789 240x320 SPI display.

## Consequences
- PSRAM gives room for LVGL frame buffers and animation assets.
- The same LVGL face code can run in a desktop simulator (ADR 0010).
- Firmware is built in CI, not on the phone (ADR 0011).
