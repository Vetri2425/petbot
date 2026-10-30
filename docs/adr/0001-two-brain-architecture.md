# 0001. Two-brain architecture

Status: Accepted

## Context
The pet must feel alive at all times (blinking, reacting to touch), while mood, voice and AI
need a Linux environment that can be slow, busy or crash.

## Decision
Split the robot into two brains:
- Linux board **decides**: mood, voice, AI.
- ESP32-S3 **performs** in real time: face, servos, touch, IMU.

## Consequences
- If Linux crashes, the pet still blinks and reacts.
- A defined link protocol between the boards is required (ADR 0005).
- Two codebases (firmware C++, brain Python) must stay in sync with the protocol spec.
