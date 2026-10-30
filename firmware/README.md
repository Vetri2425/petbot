# firmware

PlatformIO project (Arduino framework) for the ESP32-S3 DevKitC-1 N16R8.
This board is the real-time "performer": LVGL face on the ST7789 display, MG90S servos,
native touch pads and the MPU6050 IMU. It talks to the Linux brain over UART (see `protocol/`).
Currently it only prints a boot banner over USB serial. Built by CI, not locally in Termux.
Build: `pio run` in this folder (env `esp32-s3-devkitc-1`).
