# sim

Simulators for sim-first development (ADR 0010).
- `fake_esp32.py`: plays the ESP32 over a serial port. It prints received MOOD/ANIM lines and
  sends events from keys or a `--script` file. Uses `petbot_brain.protocol` for parsing.
- `run_link.sh`: creates a socat virtual serial pair `/tmp/petbot-esp <-> /tmp/petbot-brain`.
- Planned: LVGL desktop face simulator, arm64 Docker container capped at 2GB for the voice pipeline.
No Gazebo. Models are downloaded at run time, never committed.

## Two-terminal demo (brain <-> fake ESP32)

Needs `socat` and the brain package (`pip install -e brain`). Run from the repo root.

0. Start the link and leave it running (prints the commands below):

   ```sh
   sim/run_link.sh
   ```

1. Terminal A, the brain:

   ```sh
   python -m petbot_brain --port /tmp/petbot-brain
   ```

2. Terminal B, the fake ESP32 (keyboard):

   ```sh
   python sim/fake_esp32.py --port /tmp/petbot-esp
   ```

   Keys: `h` touch head, `l` touch left, `r` touch right, `b` touch back,
   `p` imu pickup, `s` imu shake, `t` imu tilt, `f` imu fall, `q` quit.

   Or the repeatable demo (about 43 s, includes the 30 s sleepy timeout):

   ```sh
   python sim/fake_esp32.py --port /tmp/petbot-esp --script sim/demo.script
   ```

Script format: one `<delay_seconds> EVT <source> <detail>` per line; `#` starts a comment.
