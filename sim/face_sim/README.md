# face_sim

PC simulator for the PetBot face (`firmware/lib/face`). It uses LVGL v9.5.0 (pinned, fetched by
CMake) with SDL2 and shows a 320x240 window at 2x zoom. It reuses `firmware/src/protocol.*`
for parsing, so it can also act as the ESP32 on a serial port.

## Build

Needs CMake, a C/C++ compiler and SDL2 (`sudo apt install libsdl2-dev`; CMake stops with
that hint if SDL2 is missing). From the repo root:

```sh
cmake -S sim/face_sim -B sim/face_sim/build
cmake --build sim/face_sim/build -j
```

## Run

```sh
sim/face_sim/build/face_sim                          # window only
sim/face_sim/build/face_sim --port /tmp/petbot-esp   # act as the ESP32 (see below)
SDL_VIDEODRIVER=dummy sim/face_sim/build/face_sim --snapshot snapshots   # PNGs, then exit
```

Keys (in the window, or typed in the terminal): `h l r b` touch head/left/right/back, `p s t f` imu
pickup/shake/tilt/fall (sent as EVT when `--port` is set), `1`-`7` force
neutral/happy/sad/angry/sleepy/curious/surprised at intensity 100, `q` quit.

## Full chain: keyboard -> EVT -> brain -> MOOD/ANIM -> face

Three terminals, from the repo root (brain installed with `pip install -e brain`):

```sh
sim/run_link.sh                                      # 1: socat pair, keep running
python -m petbot_brain --port /tmp/petbot-brain      # 2: brain
sim/face_sim/build/face_sim --port /tmp/petbot-esp   # 3: face (replaces fake_esp32.py)
```

Press `h` in the face window and the brain answers `MOOD happy 0.80`, so the eyes turn happy.
After 30 s without events the brain sends `MOOD sleepy 0.40`.

## Snapshots

`--snapshot <dir>` turns idle life off (so frames are repeatable) and writes:
`mood_<n>_<name>_{050,100}.png`, `transition_<from>_to_<to>_{25,50,75}.png`,
`blink_{closing,closed,opening}.png` and `anim_<name>.png`. CI uploads them as the
`face-snapshots` artifact.
