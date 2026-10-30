# firmware

PlatformIO project (Arduino framework) for the ESP32-S3 DevKitC-1 N16R8.
This board is the real-time "performer": LVGL face on the ST7789 display, MG90S servos,
native touch pads and the MPU6050 IMU. It talks to the Linux brain over UART (see `protocol/`).
Built and tested by CI, not locally in Termux.

| File | Purpose |
|------|---------|
| `src/protocol.h/.cpp` | Line protocol parser/encoder and line assembler. No Arduino, no heap |
| `src/link.h/.cpp` | UART1 receive task; logs MOOD/ANIM and queues them for the face, `sendEvent()` |
| `src/display.h/.cpp` | Face task: LVGL + ST7789 over SPI at ~30 fps, fed by a queue |
| `src/serial_log.h/.cpp` | Mutex-guarded USB serial logging |
| `src/config.h` | Pins, baud rates, display options (pins TBD until wiring) |
| `src/main.cpp` | Boot banner, start link and face, debug keys on USB serial |
| `lib/face/` | The face (LVGL only); the same code runs in `sim/face_sim` |
| `include/lv_conf*.h` | LVGL config; `lv_conf_common.h` is shared with the sim |
| `test/test_protocol/` | Unity tests, run on the host; include `protocol/test_vectors.txt` |

Build: `pio run` (env `esp32-s3-devkitc-1`). Host tests: `pio test -e native`.

## Display wiring (TBD until wiring is final)

2.0" ST7789 240x320 SPI module, shown as 320x240 landscape. Pins are in `src/config.h`.

| Display pin | ESP32-S3 | Notes |
|-------------|----------|-------|
| VCC | 3V3 | |
| GND | GND | |
| SCL / SCK | GPIO12 | SPI clock (40 MHz) |
| SDA / MOSI | GPIO11 | SPI data |
| CS | GPIO10 | |
| DC / RS | GPIO9 | data/command |
| RES / RST | GPIO8 | |
| BLK / BL | GPIO7 | backlight, HIGH = on |

With no display connected the firmware still boots and the link works (SPI is write-only).
`kDisplayInvertColors` and `kDisplayRotate270` in `config.h` may need flipping for your panel.

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
