# Bill of Materials

Off-the-shelf modules only (ADR 0012). No prices tracked here.

## Compute

| Part | Qty | Purpose |
|------|-----|---------|
| Radxa ZERO 3W, 2GB RAM, 16GB eMMC (RK3566) | 1 | Linux brain: mood, voice, AI |
| ESP32-S3 DevKitC-1 N16R8 | 1 | Real-time co-processor: face, servos, touch, IMU |

## Display, motion, sensing

| Part | Qty | Purpose |
|------|-----|---------|
| 2.0" ST7789 240x320 SPI display | 1 | LVGL face |
| MG90S servo | 3 | Head tilt, left arm, right arm |
| MPU6050 IMU module | 1 | Motion events (e.g. pickup) |
| Copper tape | 1 roll | Touch pads on ESP32 native touch pins |

## Audio (Phase 2)

| Part | Qty | Purpose |
|------|-----|---------|
| USB sound card (mic in, speaker out) | 1 | Audio I/O for the voice pipeline |
| PAM8403 amplifier module | 1 | Drive the speaker |
| Microphone | 1 | Voice input; exact part TBD (body space + test recordings) |
| Speaker | 1 | Voice output; exact part TBD (body space + test recordings) |

## Power

| Part | Qty | Purpose |
|------|-----|---------|
| 18650 Li-ion cell | 2 | 2S battery pack |
| 2S BMS board | 1 | Battery protection |
| 5V 3A buck converter | 2 | Separate logic and servo rails |

## Body

| Part | Qty | Purpose |
|------|-----|---------|
| PETG filament | TBD | Printed frame; qty decided by final CAD |
| PLA filament | TBD | Printed shell; qty decided by final CAD |

## TBD

- Wiring, connectors, switch, fasteners: decided during electronics integration.
- Phase 3 camera and mobile base parts: decided at the start of Phase 3.
