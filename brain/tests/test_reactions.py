import threading
from collections.abc import Iterator

import pytest
from conftest import Pty

from petbot_brain.link import LinkClosed, SerialLink
from petbot_brain.protocol import Anim, Event, Mood
from petbot_brain.reactions import SLEEP_AFTER_S, SLEEPY, WAKE, ReactionBrain, run


class FakeClock:
    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now


@pytest.fixture
def clock() -> FakeClock:
    return FakeClock()


@pytest.fixture
def brain(clock: FakeClock) -> ReactionBrain:
    return ReactionBrain(clock=clock)


@pytest.mark.parametrize(
    ("event", "expected"),
    [
        (Event("touch", "head"), [Mood("happy", 0.80)]),
        (Event("touch", "left"), [Anim("wave_left")]),
        (Event("touch", "right"), [Anim("wave_right")]),
        (Event("touch", "back"), [Mood("curious", 0.60)]),
        (Event("imu", "pickup"), [Mood("surprised", 0.90)]),
        (Event("imu", "shake"), [Mood("angry", 0.50)]),
        (Event("imu", "fall"), [Mood("sad", 0.70)]),
        (Event("imu", "tilt"), []),
    ],
)
def test_reaction_table(brain: ReactionBrain, event: Event, expected: list) -> None:
    assert brain.on_event(event) == expected


def test_sleepy_after_30s_without_events(brain: ReactionBrain, clock: FakeClock) -> None:
    clock.now += SLEEP_AFTER_S - 0.1
    assert brain.tick() == []
    clock.now += 0.1
    assert brain.tick() == [SLEEPY]
    assert SLEEPY == Mood("sleepy", 0.40)
    clock.now += 100
    assert brain.tick() == []  # only once


def test_event_resets_sleep_timer(brain: ReactionBrain, clock: FakeClock) -> None:
    clock.now += 20
    brain.on_event(Event("touch", "back"))
    clock.now += 20
    assert brain.tick() == []
    clock.now += 10
    assert brain.tick() == [SLEEPY]


def test_event_wakes_to_neutral_before_reacting(brain: ReactionBrain, clock: FakeClock) -> None:
    clock.now += SLEEP_AFTER_S
    assert brain.tick() == [SLEEPY]
    assert brain.on_event(Event("touch", "head")) == [WAKE, Mood("happy", 0.80)]
    assert WAKE.name == "neutral"
    assert brain.on_event(Event("touch", "head")) == [Mood("happy", 0.80)]


def test_event_without_reaction_still_wakes(brain: ReactionBrain, clock: FakeClock) -> None:
    clock.now += SLEEP_AFTER_S
    brain.tick()
    assert brain.on_event(Event("imu", "tilt")) == [WAKE]
    clock.now += SLEEP_AFTER_S
    assert brain.tick() == [SLEEPY]


def test_custom_reaction_table(clock: FakeClock) -> None:
    brain = ReactionBrain(reactions={("imu", "tilt"): Anim("wobble")}, clock=clock)
    assert brain.on_event(Event("imu", "tilt")) == [Anim("wobble")]
    assert brain.on_event(Event("touch", "head")) == []


class Loop:
    """Runs the reaction loop on a pty in a background thread."""

    def __init__(self, brain: ReactionBrain, pty: Pty) -> None:
        self.pty = pty
        self.link = SerialLink.open(self.pty.path, timeout=0.02)
        self.stop = threading.Event()
        self.error: BaseException | None = None
        self.thread = threading.Thread(target=self._run, args=(brain,), daemon=True)
        self.thread.start()

    def _run(self, brain: ReactionBrain) -> None:
        try:
            run(self.link, brain, self.stop)
        except BaseException as e:  # surfaced to the test
            self.error = e

    def close(self) -> None:
        self.stop.set()
        self.thread.join(timeout=2)
        self.link.close()


@pytest.fixture
def loop(brain: ReactionBrain, pty: Pty) -> Iterator[Loop]:
    lp = Loop(brain, pty)
    yield lp
    lp.close()


def test_integration_touch_head_gets_happy(loop: Loop) -> None:
    loop.pty.write(b"EVT touch head\n")
    assert loop.pty.read_line() == b"MOOD happy 0.80\n"
    assert loop.error is None


def test_integration_skips_invalid_then_reacts(loop: Loop) -> None:
    loop.pty.write(b"garbage\nMOOD happy 0.5\nEVT touch left\r\n")
    assert loop.pty.read_line() == b"ANIM wave_left\n"
    assert loop.error is None


def test_integration_sleepy_and_wake(loop: Loop, clock: FakeClock) -> None:
    clock.now += SLEEP_AFTER_S
    assert loop.pty.read_line() == b"MOOD sleepy 0.40\n"
    loop.pty.write(b"EVT imu pickup\n")
    assert loop.pty.read_line() == b"MOOD neutral 0.50\n"
    assert loop.pty.read_line() == b"MOOD surprised 0.90\n"


def test_integration_disconnect_raises_link_closed(loop: Loop) -> None:
    loop.pty.close_master()
    loop.thread.join(timeout=2)
    assert not loop.thread.is_alive()
    assert isinstance(loop.error, LinkClosed)
