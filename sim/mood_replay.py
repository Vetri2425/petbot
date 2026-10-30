#!/usr/bin/env python3
"""Replay an event timeline through the mood engine, faster than real time, into a CSV.

The timeline uses the fake_esp32.py script format: '<delay_seconds> EVT <source> <detail>',
delays relative to the previous line. The engine ticks at 10 Hz of simulated time.

  python sim/mood_replay.py sim/mood_scenarios/petting.script --out petting.csv

The CSV has one row per --sample seconds plus a row for every step that sent something.
A short mood timeline is printed to stdout.
"""

from __future__ import annotations

import argparse
import csv
import random
import sys
from collections.abc import Iterator
from pathlib import Path
from typing import TextIO

from fake_esp32 import load_script

from petbot_brain.mood import Personality, initial_state, load, step
from petbot_brain.mood.engine import TICK_S
from petbot_brain.protocol import Event, Message, encode

COLUMNS = (
    "t", "valence", "arousal", "energy", "social", "boredom", "asleep", "mood", "intensity",
    "outputs",
)  # fmt: skip


def wire(msg: Message) -> str:
    return encode(msg).decode("ascii").rstrip("\n")


def replay(
    steps: list[tuple[float, Event]], personality: Personality, seed: int, tail_s: float
) -> Iterator[tuple[object, list[Message]]]:
    """Yield (state, outputs) after every engine step, at 10 Hz plus at each event time."""
    rng = random.Random(seed)
    state = initial_state(personality)
    times: list[tuple[float, Event]] = []
    t = 0.0
    for delay, event in steps:
        t += delay
        times.append((t, event))
    end = t + tail_s
    now, next_tick, i = 0.0, 0.0, 0
    while now <= end:
        events: list[Event] = []
        target = next_tick
        if i < len(times) and times[i][0] <= next_tick:
            target = times[i][0]
            while i < len(times) and times[i][0] == target:
                events.append(times[i][1])
                i += 1
        state, outputs = step(state, events, target - now, personality, rng)
        now = target
        if not events:
            next_tick = round(next_tick + TICK_S, 9)
        yield state, outputs


def write_csv(
    out: TextIO, rows: Iterator[tuple[object, list[Message]]], sample_s: float
) -> list[tuple[float, str]]:
    """Write the CSV and return the (time, message) list of every output."""
    writer = csv.writer(out)
    writer.writerow(COLUMNS)
    sent: list[tuple[float, str]] = []
    next_sample = 0.0
    for s, outputs in rows:
        sent += [(s.t, wire(m)) for m in outputs]
        if outputs or s.t >= next_sample:
            writer.writerow([
                f"{s.t:.1f}", f"{s.valence:.3f}", f"{s.arousal:.3f}", f"{s.energy:.3f}",
                f"{s.social:.3f}", f"{s.boredom:.3f}", int(s.asleep), s.mood, s.intensity,
                ";".join(wire(m) for m in outputs),
            ])  # fmt: skip
            if s.t >= next_sample:
                next_sample += sample_s
    return sent


def summary(sent: list[tuple[float, str]]) -> str:
    lines = [f"{t:8.1f}s  {msg}" for t, msg in sent if msg.startswith("MOOD")]
    anims = [msg for _, msg in sent if msg.startswith("ANIM")]
    lines.append(f"{len(anims)} ANIM messages: " + ", ".join(sorted(set(anims))))
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Replay events through the mood engine")
    parser.add_argument("script", type=Path, help="timeline, '<delay> EVT <source> <detail>'")
    parser.add_argument("--out", type=Path, help="CSV file (default: stdout)")
    parser.add_argument("--personality", type=Path, help="personality.toml (default: brain's)")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--tail", type=float, default=30.0, help="seconds after the last event")
    parser.add_argument("--sample", type=float, default=1.0, help="seconds between CSV rows")
    args = parser.parse_args(argv)

    personality = load(args.personality) if args.personality else load()
    rows = replay(load_script(args.script), personality, args.seed, args.tail)
    if args.out:
        with args.out.open("w", newline="", encoding="ascii") as f:
            sent = write_csv(f, rows, args.sample)
        print(summary(sent))
    else:
        sent = write_csv(sys.stdout, rows, args.sample)
        print(summary(sent), file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
