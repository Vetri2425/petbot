# firmware

PlatformIO project (Arduino framework) for the ESP32-S3 DevKitC-1 N16R8.
This board is the real-time "performer": LVGL face on the ST7789 display, MG90S servos,
native touch pads and the MPU6050 IMU. It talks to the Linux brain over UART (see `protocol/`).
Built and tested by CI, not locally in Termux.

| File | Purpose |
|------|---------|
| `src/protocol.h/.cpp` | Line protocol parser/encoder and line assembler. No Arduino, no heap |
| `src/link.h/.cpp` | UART1 receive task; logs MOOD/ANIM to USB serial, `sendEvent()` |
| `src/config.h` | Pins and baud rates (link pins TBD until PCB/wiring) |
| `src/main.cpp` | Boot banner, link start, debug keys on USB serial |
| `test/test_protocol/` | Unity tests, run on the host; include `protocol/test_vectors.txt` |

Build: `pio run` (env `esp32-s3-devkitc-1`). Host tests: `pio test -e native`.

## Try it with a USB-UART adapter

The brain runs on a PC, and a 3.3 V USB-UART adapter stands in for the Radxa:

| Adapter | ESP32-S3 |
|---------|----------|
| TX | GPIO18 (link RX) |
| RX | GPIO17 (link TX) |
| GND | GND |

Don't connect the adapter's VCC. Power the board from its USB port, and check the adapter
supports 921600 baud.

1. Flash, then open the USB serial log on the board's UART USB port:
   `pio run -t upload && pio device monitor` (115200 baud).
2. On the PC: `python -m petbot_brain --port /dev/ttyUSB0` (your adapter's device).
3. In the monitor, type `h`. The board logs `[link] tx EVT touch head`, the brain logs the
   event and replies, and the board logs `[link] rx MOOD happy 0.80`.
   Keys: `h l r b` touch head/left/right/back, `p s t f` imu pickup/shake/tilt/fall, `?` help.
