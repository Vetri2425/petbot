# PetBot UART Protocol

Status: draft v0. Locked parts come from ADR 0005; everything else is marked TBD.

## Transport

| Item | Value |
|------|-------|
| Physical | UART between Radxa ZERO 3W and ESP32-S3 |
| Baud | 921600 |
| Encoding | ASCII |
| Framing | One message per line, terminated by `\n` |

## Grammar

```
line     = verb *( SP arg ) LF
verb     = 1*( UPPER )            ; e.g. MOOD, ANIM, EVT
arg      = 1*( VCHAR )            ; no spaces inside an arg
SP       = %x20
LF       = %x0A
```

- Verbs are uppercase; args are lowercase identifiers (`snake_case`) or decimal numbers.
- Max line length: TBD, decided by the ESP32 receive buffer size.
- Whether `\r\n` is accepted: TBD, decided when the first parser is written.

## Messages

### Linux -> ESP32 (commands)

| Command | Args | Example | Meaning |
|---------|------|---------|---------|
| `MOOD` | `<mood> <intensity>` | `MOOD happy 0.8` | Set face/body mood; intensity is a float 0.0-1.0 |
| `ANIM` | `<name>` | `ANIM wave_left` | Play a named animation |

- Full list of mood names: TBD, decided by the Phase 1 face design (`happy` is the first).
- Full list of animation names: TBD, decided by the Phase 1 motion design (`wave_left` is the first).

### ESP32 -> Linux (events)

| Event | Args | Example | Meaning |
|-------|------|---------|---------|
| `EVT touch` | `<zone>` | `EVT touch head` | A touch pad was touched |
| `EVT imu` | `<event>` | `EVT imu pickup` | IMU detected a motion event |

- Touch zones: TBD, decided by the CAD body layout (`head` is the first).
- IMU events: TBD, decided by Phase 1 IMU testing (`pickup` is the first).

### Firmware update

Linux can re-flash the ESP32 over this UART (ADR 0005, 0011).
Mechanism (bootloader entry, transfer format): TBD, decided when `deploy/` update script is built.

## Error handling

- Unknown verbs or malformed lines: TBD, decided when the first parser is written
  (options: ignore silently, or reply with an error event).
- Out-of-range args (e.g. intensity > 1.0): TBD, same decision point.
- Link loss: the ESP32 keeps blinking and reacting on its own (ADR 0001). How it detects
  link loss (e.g. heartbeat): TBD, decided in Phase 1 integration.

## Versioning

- This document is versioned with the repo; current version is v0 (draft, may change freely).
- Version handshake between boards: TBD, decided before the first release that the robot pulls.
- Rule: change this spec first, then update `firmware/` and `brain/` in the same PR.
