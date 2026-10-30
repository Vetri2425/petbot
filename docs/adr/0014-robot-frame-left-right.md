# 0014. Left/right means the robot's own frame

Status: Accepted

## Context
"Left" was ambiguous: the face sim used the screen (viewer's) left, while touch zones and
arms are naturally named from the robot's body. The robot faces the viewer, so the two
frames are mirrored.

## Decision
Every left/right name uses the robot's own frame, as if you were the robot:
- Touch zones: `EVT touch left` is the pad on the robot's left side.
- Arms: `ANIM wave_left` waves the robot's left arm.
- Eyes: `look_left` turns the eyes to the robot's left, which is the viewer's right on
  screen. "Left eye" in face code is the robot's left eye (drawn on the screen's right).

## Consequences
- Brain, protocol, firmware and face all use one convention; no mirroring layer.
- On screen, left-named eye motion goes toward screen-right; a face_sim check enforces it.
- CAD and wiring docs must label pads and servos from the robot's point of view.
