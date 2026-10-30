#!/usr/bin/env bash
# Create a virtual serial pair /tmp/petbot-esp <-> /tmp/petbot-brain for the sim demo.
set -euo pipefail

ESP_PORT=/tmp/petbot-esp
BRAIN_PORT=/tmp/petbot-brain
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v socat >/dev/null 2>&1; then
  echo "socat is not installed. Install it, then re-run this script:" >&2
  echo "  Termux:        pkg install socat" >&2
  echo "  Debian/Ubuntu: sudo apt install socat" >&2
  exit 1
fi

cat <<MSG
Virtual serial pair: $ESP_PORT <-> $BRAIN_PORT
Keep this terminal open (Ctrl-C stops the link). Then run, in two other terminals:

  Terminal A (brain):
    python -m petbot_brain --port $BRAIN_PORT

  Terminal B (fake ESP32, keyboard):
    python $REPO/sim/fake_esp32.py --port $ESP_PORT
  or scripted:
    python $REPO/sim/fake_esp32.py --port $ESP_PORT --script $REPO/sim/demo.script

MSG

exec socat pty,raw,echo=0,link="$ESP_PORT" pty,raw,echo=0,link="$BRAIN_PORT"
