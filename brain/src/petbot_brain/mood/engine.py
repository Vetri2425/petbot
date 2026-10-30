"""Mood engine runtime: wraps the pure model with a clock and RNG, and runs it on a link."""

from __future__ import annotations

import logging
import random
import threading
import time
from collections.abc import Callable, Sequence

from petbot_brain.link import SerialLink
from petbot_brain.mood.model import MoodState, initial_state, step
from petbot_brain.mood.personality import Personality
from petbot_brain.protocol import Event, Message, encode

log = logging.getLogger(__name__)

TICK_S = 0.1  # 10 Hz


class MoodEngine:
    """Holds the model state; `update(now, events)` steps it to `now` (seconds, any epoch)."""

    def __init__(self, personality: Personality, seed: int | None = None) -> None:
        self.personality = personality
        self.rng = random.Random(seed)
        self.state: MoodState = initial_state(personality)
        self._last: float | None = None

    def update(self, now: float, events: Sequence[Event] = ()) -> list[Message]:
        dt = 0.0 if self._last is None else max(0.0, now - self._last)
        self._last = now
        self.state, outputs = step(self.state, events, dt, self.personality, self.rng)
        return outputs


def run(
    link: SerialLink,
    engine: MoodEngine,
    clock: Callable[[], float] = time.monotonic,
    stop: threading.Event | None = None,
) -> None:
    """Feed events from `link` into `engine` and send its outputs, ticking at 10 Hz.

    Events are applied as soon as they arrive (low latency); otherwise the engine ticks
    every TICK_S. Raises LinkClosed if the link goes away.
    """
    next_tick = clock()
    while stop is None or not stop.is_set():
        msg = link.receive()  # returns within the link's read timeout
        events: list[Event] = []
        if isinstance(msg, Event):
            log.info("event: %s %s", msg.source, msg.detail)
            events.append(msg)
        elif msg is not None:
            log.warning("ignoring %s sent by the ESP32", type(msg).__name__)
        now = clock()
        if not events and now < next_tick:
            continue
        next_tick = now + TICK_S
        for out in engine.update(now, events):
            log.info("send: %s", encode(out).decode("ascii").rstrip())
            link.send(out)
