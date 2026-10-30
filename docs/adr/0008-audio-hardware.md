# 0008. Audio hardware

Status: Accepted

## Context
The voice pipeline needs a microphone input and a speaker output on the Linux board.

## Decision
One USB sound card (mic in, speaker out) plus a PAM8403 amplifier.

## Consequences
- Standard Linux USB audio, no custom drivers.
- Uses the board's USB port.
- Microphone and speaker part choice: TBD, decided by body space and test recordings.
