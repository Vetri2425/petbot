# 0004. Actuators and sensors

Status: Accepted

## Context
Phase 1 needs expressive motion and the ability to sense touch and being handled.

## Decision
- 3x MG90S servos: head tilt, left arm, right arm.
- MPU6050 IMU (e.g. pickup detection).
- Copper-tape pads on ESP32-S3 native touch pins.
All are driven/read by the ESP32-S3.

## Consequences
- No extra touch-controller chip; pads are cheap and shaped to the body.
- Servos need their own power rail (ADR 0009).
- Touch zones and pin mapping: TBD, decided by the CAD body layout.
