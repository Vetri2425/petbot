# 0010. Sim-first development

Status: Accepted

## Context
Development happens mostly away from the hardware, often from a phone.

## Decision
- LVGL desktop simulator for the face.
- Fake ESP32 script over a socat virtual serial pair to test the brain.
- Voice pipeline tested in an arm64 Docker container capped at 2GB RAM.
- No Gazebo.

## Consequences
- Brain and face logic can be developed and tested without hardware.
- The 2GB container enforces the RAM budget early.
- Servo/IMU physics are not simulated.
