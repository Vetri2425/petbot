"""Placeholder reaction loop: maps ESP32 events to MOOD/ANIM commands.

The real mood engine will replace REACTIONS and ReactionBrain later.
"""

from __future__ import annotations

import logging
import threading
import time
from collections.abc import Callable, Mapping
from typing import Final

from petbot_brain.link import SerialLink
from petbot_brain.protocol import Anim, Event, Message, Mood, encode

log = logging.getLogger(__name__)

SLEEP_AFTER_S: Final = 30.0
SLEEPY: Final = Mood("sleepy", 40)
WAKE: Final = Mood("neutral", 50)

# (source, detail) -> command. Events not listed (e.g. imu tilt) are only logged.
REACTIONS: Final[Mapping[tuple[str, str], Message]] = {
    ("touch", "head"): Mood("happy", 80),
    ("touch", "left"): Anim("wave_left"),
    ("touch", "right"): Anim("wave_right"),
    ("touch", "back"): Mood("curious", 60),
    ("imu", "pickup"): Mood("surprised", 90),
    ("imu", "shake"): Mood("angry", 50),
    ("imu", "fall"): Mood("sad", 70),
}


class ReactionBrain:
    """Reacts to events and falls asleep after SLEEP_AFTER_S seconds without events."""

    def __init__(
        self,
        reactions: Mapping[tuple[str, str], Message] = REACTIONS,
        clock: Callable[[], float] = time.monotonic,
        sleep_after: float = SLEEP_AFTER_S,
    ) -> None:
        self._reactions = reactions
        self._clock = clock
        self._sleep_after = sleep_after
        self._last_event = clock()
        self.asleep = False

    def on_event(self, event: Event) -> list[Message]:
        self._last_event = self._clock()
        out: list[Message] = []
        if self.asleep:
            self.asleep = False
            out.append(WAKE)
        reaction = self._reactions.get((event.source, event.detail))
        if reaction is not None:
            out.append(reaction)
        return out

    def tick(self) -> list[Message]:
        if not self.asleep and self._clock() - self._last_event >= self._sleep_after:
            self.asleep = True
            return [SLEEPY]
        return []


def run(link: SerialLink, brain: ReactionBrain, stop: threading.Event | None = None) -> None:
    """Pump events from `link` through `brain` until `stop` is set.

    Raises LinkClosed if the link goes away.
    """
    while stop is None or not stop.is_set():
        msg = link.receive()
        out: list[Message] = []
        if isinstance(msg, Event):
            log.info("event: %s %s", msg.source, msg.detail)
            out = brain.on_event(msg)
        elif msg is not None:
            log.warning("ignoring %s sent by the ESP32", type(msg).__name__)
        out += brain.tick()
        for cmd in out:
            log.info("send: %s", encode(cmd).decode("ascii").rstrip())
            link.send(cmd)
