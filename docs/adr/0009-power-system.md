# 0009. Power system

Status: Accepted

## Context
Servo current spikes can brown out the logic boards if they share a supply.

## Decision
- 2S 18650 pack with a BMS.
- Two separate 5V 3A buck converters: one for logic, one for servos.

## Consequences
- Servo noise is isolated from the Radxa and ESP32.
- Grounds must be shared between rails for UART and servo signals.
- Charging method and battery monitoring: TBD, decided during electronics integration.
