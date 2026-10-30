# 0005. UART ASCII line protocol

Status: Accepted

## Context
The two brains need a simple, debuggable link that also allows updating the ESP32.

## Decision
- UART at 921600 baud.
- Newline-terminated ASCII lines.
- Linux->ESP32 commands, e.g. `MOOD happy 0.8`, `ANIM wave_left`.
- ESP32->Linux events, e.g. `EVT touch head`, `EVT imu pickup`.
- Linux can re-flash the ESP32 over this UART.

## Consequences
- Human-readable: can be tested with a serial terminal and a fake ESP32 (ADR 0010).
- Spec lives in `protocol/PROTOCOL.md`; firmware and brain follow it.
- Re-flash mechanism details: TBD, decided when the deploy update script is built.
