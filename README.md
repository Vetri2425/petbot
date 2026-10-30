# PetBot

PetBot is a hobby desk pet robot in the style of Eilik: a screen face, head tilt
plus two arms, touch reactions, and later offline voice chat with a small local LLM.

It uses a two-brain design: a Linux board (Radxa ZERO 3W) decides mood, voice and AI,
while an ESP32-S3 performs face, servos, touch and IMU in real time. If Linux crashes,
the pet still blinks and reacts.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full design.

## Phase status

- [ ] Phase 1: expressive pet (touch + face + motion)
- [ ] Phase 2: offline voice + local LLM
- [ ] Phase 3: camera/NPU face detection, then mobile EMO-style base

## Repo map

| Path | Purpose |
|------|---------|
| `firmware/` | ESP32-S3 PlatformIO project (face, servos, touch, IMU) |
| `brain/` | Python package `petbot_brain` for the Linux board |
| `protocol/` | UART line protocol spec between the two boards |
| `sim/` | Simulators: LVGL desktop face, fake ESP32, 2GB Docker |
| `cad/` | SolidWorks body design and STL exports |
| `deploy/` | systemd units and update script for the robot |
| `docs/` | Architecture, BOM and ADRs (`docs/adr/`) |

## License

MIT, see [LICENSE](LICENSE).
