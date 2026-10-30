# PetBot UART Protocol

Status: v1. Transport and examples come from ADR 0005; open points are marked TBD.
The allowed names below are defined in code in `brain/src/petbot_brain/protocol.py`
(`MOODS`, `EVENTS`); a test keeps this document in sync with it.

## Transport

| Item | Value |
|------|-------|
| Physical | UART between Radxa ZERO 3W and ESP32-S3 |
| Baud | 921600 |
| Encoding | ASCII only |
| Framing | One message per line, terminated by `\n` |
| Max line length | 128 bytes, including the `\n` |

## Grammar

```
line     = verb 1*( SP arg ) EOL
verb     = "MOOD" / "ANIM" / "EVT"
arg      = 1*( %x21-7E )          ; printable ASCII, no spaces
SP       = %x20                   ; exactly one space between tokens
EOL      = LF / CR LF             ; senders emit LF; receivers also accept CR LF
```

- No leading, trailing or repeated spaces. No other control characters.
- Names are lowercase `[a-z0-9_]+`.
- Intensity is a decimal number `[0-9]+(.[0-9]{1,2})?` (at most 2 decimals) in the range 0.0-1.0.
  Senders format it with 2 decimals (`0.80`).

## Messages

### Linux -> ESP32 (commands)

| Command | Args | Example | Meaning |
|---------|------|---------|---------|
| `MOOD` | `<name> <intensity>` | `MOOD happy 0.80` | Set face/body mood; intensity 0.0-1.0 |
| `ANIM` | `<name>` | `ANIM wave_left` | Play a named animation |

### ESP32 -> Linux (events)

| Event | Args | Example | Meaning |
|-------|------|---------|---------|
| `EVT` | `<source> <detail>` | `EVT touch head` | Something happened on the body |

### Allowed names (v1)

Mood names: `neutral`, `happy`, `sad`, `angry`, `sleepy`, `curious`, `surprised`

| Source | Details |
|--------|---------|
| `touch` | `head`, `left`, `right`, `back` |
| `imu` | `pickup`, `shake`, `tilt`, `fall` |

Animation names: any `[a-z0-9_]+` token on the wire. The brain currently uses `wave_left`
and `wave_right`. The full set the firmware supports: TBD, decided by the Phase 1 motion design.

### Firmware update

Linux can re-flash the ESP32 over this UART (ADR 0005, 0011).
Mechanism (bootloader entry, transfer format): TBD, decided when the `deploy/` update script is built.

## Error handling

A line is invalid if it has an unknown command, a wrong number of arguments, an unknown
name, an intensity that is malformed, has more than 2 decimals or is outside 0.0-1.0, is
longer than 128 bytes, contains non-ASCII bytes or control characters, or breaks the
single-space rule.

On an invalid line the receiver:
- logs a warning and skips the line;
- never crashes;
- sends no reply. There are no ACKs or error replies in v1.

A line longer than 128 bytes is discarded up to and including its next `\n`.

Link loss: the ESP32 keeps blinking and reacting on its own (ADR 0001). How it detects
link loss (e.g. heartbeat): TBD, decided in Phase 1 integration.

## Versioning

- This document is versioned with the repo; current protocol version is v1.
- Version handshake between boards: TBD, decided before the first release that the robot pulls.
- Rule: change this spec first, then update `firmware/` and `brain/` in the same PR.
