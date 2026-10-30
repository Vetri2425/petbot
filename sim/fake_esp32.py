#!/usr/bin/env python3
"""Fake ESP32 for sim-first development.

Prints every MOOD/ANIM line it receives and sends events from keyboard keys,
or from a script file with --script. Uses petbot_brain.protocol for all parsing.

Keys: h=touch head  l=touch left  r=touch right  b=touch back
      p=imu pickup  s=imu shake   t=imu tilt     f=imu fall    q=quit
"""

from __future__ import annotations

import argparse
import contextlib
import logging
import math
import sys
import termios
import threading
import time
import tty
from collections.abc import Iterator
from pathlib import Path

from petbot_brain.link import DEFAULT_BAUD, LinkClosed, SerialLink
from petbot_brain.protocol import Event, Message, ProtocolError, encode, parse_line

KEYS: dict[str, Event] = {
    "h": Event("touch", "head"),
    "l": Event("touch", "left"),
    "r": Event("touch", "right"),
    "b": Event("touch", "back"),
    "p": Event("imu", "pickup"),
    "s": Event("imu", "shake"),
    "t": Event("imu", "tilt"),
    "f": Event("imu", "fall"),
}
QUIT_KEY = "q"
SCRIPT_TAIL_S = 1.0  # keep listening for replies after the last scripted event


def wire(msg: Message) -> str:
    return encode(msg).decode("ascii").rstrip("\n")


def print_received(link: SerialLink) -> None:
    for msg in link:
        if isinstance(msg, Event):
            print(f"?? unexpected from brain: {wire(msg)}", flush=True)
        else:
            print(f"<- {wire(msg)}", flush=True)


def send(link: SerialLink, event: Event) -> None:
    link.send(event)
    print(f"-> {wire(event)}", flush=True)


def load_script(path: Path) -> list[tuple[float, Event]]:
    """Parse '<delay_seconds> EVT <source> <detail>' lines; '#' starts a comment."""
    steps: list[tuple[float, Event]] = []
    for lineno, raw in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        delay_text, _, rest = line.partition(" ")
        try:
            delay = float(delay_text)
            if not math.isfinite(delay) or delay < 0:
                raise ValueError(f"invalid delay {delay_text!r}")
            msg = parse_line(rest)
        except (ValueError, ProtocolError) as e:
            raise SystemExit(f"{path}:{lineno}: {e}") from e
        if not isinstance(msg, Event):
            raise SystemExit(f"{path}:{lineno}: only EVT lines are allowed")
        steps.append((delay, msg))
    return steps


def run_script(link: SerialLink, steps: list[tuple[float, Event]]) -> None:
    for delay, event in steps:
        time.sleep(delay)
        send(link, event)
    time.sleep(SCRIPT_TAIL_S)


@contextlib.contextmanager
def cbreak_stdin() -> Iterator[None]:
    """Read single key presses without Enter when stdin is a terminal."""
    if not sys.stdin.isatty():
        yield
        return
    fd = sys.stdin.fileno()
    saved = termios.tcgetattr(fd)
    try:
        tty.setcbreak(fd)
        yield
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, saved)


def run_keyboard(link: SerialLink) -> None:
    keys = "  ".join(f"{k}={e.source} {e.detail}" for k, e in KEYS.items())
    print(f"keys: {keys}  {QUIT_KEY}=quit", flush=True)
    with cbreak_stdin():
        while True:
            key = sys.stdin.read(1)
            if key in ("", QUIT_KEY):
                return
            event = KEYS.get(key.lower())
            if event is not None:
                send(link, event)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Fake ESP32 for PetBot simulation")
    parser.add_argument("--port", required=True, help="serial device, e.g. /tmp/petbot-esp")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--script", type=Path, help="file of '<delay> EVT <source> <detail>'")
    args = parser.parse_args(argv)

    logging.basicConfig(level=logging.WARNING, format="%(levelname)s %(name)s: %(message)s")
    steps = load_script(args.script) if args.script else None

    try:
        link = SerialLink.open(args.port, args.baud)
    except LinkClosed as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"fake ESP32 on {args.port}", flush=True)
    threading.Thread(target=print_received, args=(link,), daemon=True).start()
    try:
        if steps is not None:
            run_script(link, steps)
        else:
            run_keyboard(link)
    except LinkClosed as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        pass
    finally:
        link.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
