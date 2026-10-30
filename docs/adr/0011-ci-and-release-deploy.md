# 0011. CI builds and Wi-Fi release deploy

Status: Accepted

## Context
The dev environment (Termux on Android) should not install toolchains.

## Decision
- GitHub Actions builds the firmware and runs the brain tests.
- The robot pulls the latest release over Wi-Fi.
- The Dell laptop is used only for first-time flashing.

## Consequences
- Termux needs only git, gh and python.
- Releases must contain the firmware binary; the robot re-flashes the ESP32 over UART.
- Release format and versioning: TBD, decided when the deploy update script is built.
