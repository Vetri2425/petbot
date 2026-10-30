"""Mood engine: model behaviour, personality loading, scenarios and the link loop."""

import random
import sys
import threading
import time
import tomllib
from dataclasses import replace
from pathlib import Path

import pytest
from conftest import Pty

from petbot_brain.link import SerialLink
from petbot_brain.mood import MoodEngine, MoodState, PersonalityError, initial_state, load, step
from petbot_brain.mood.engine import TICK_S, run
from petbot_brain.mood.personality import DEFAULT_PATH, parse
from petbot_brain.protocol import EVENTS, Anim, Event, Message, Mood, encode, parse_line

SIM_DIR = Path(__file__).resolve().parents[2] / "sim"
SCENARIOS = SIM_DIR / "mood_scenarios"
HEAD = Event("touch", "head")
P = load()


def run_ticks(
    state: MoodState,
    seconds: float,
    rng: random.Random,
    events_at: dict[int, list[Event]] | None = None,
) -> tuple[MoodState, list[tuple[float, Message]]]:
    """Step at 10 Hz for `seconds`; events_at maps tick index -> events."""
    sent: list[tuple[float, Message]] = []
    for i in range(round(seconds / TICK_S)):
        events = (events_at or {}).get(i, [])
        state, outputs = step(state, events, TICK_S, P, rng)
        sent += [(state.t, m) for m in outputs]
    return state, sent


def moods(sent: list[tuple[float, Message]]) -> list[Mood]:
    return [m for _, m in sent if isinstance(m, Mood)]


# ---- personality.toml ----


def raw_personality() -> dict:
    with DEFAULT_PATH.open("rb") as f:
        return tomllib.load(f)


def test_personality_loads() -> None:
    assert P.max_messages_per_s == 5
    assert set(P.mood_ranges) == {"happy", "sad", "angry", "sleepy", "curious", "surprised"}
    assert len(P.impulses) == sum(len(d) for d in EVENTS.values())


@pytest.mark.parametrize(
    "mutate",
    [
        lambda d: d["valence"].pop("half_life_s"),  # missing value
        lambda d: d["valence"].update(typo=1.0),  # unknown key
        lambda d: d.update(extra={}),  # unknown table
        lambda d: d["impulses"]["touch"].update(nose={"valence": 0.1}),  # unknown event
        lambda d: d["impulses"]["imu"].pop("fall"),  # every event needs an impulse
        lambda d: d["reactions"]["touch"].update(left=["Wave-Left"]),  # invalid anim name
        lambda d: d["moods"]["happy"].update(mood=[0, 1]),  # unknown signal
        lambda d: d["moods"]["happy"].update(valence=[0.1]),  # range needs 2 values
        lambda d: d["output"].update(max_messages_per_s=0),  # out of range
        lambda d: d["arousal"].update(baseline="high"),  # not a number
    ],
)
def test_personality_is_strict(mutate) -> None:
    data = raw_personality()
    mutate(data)
    with pytest.raises(PersonalityError):
        parse(data)


# ---- decay ----


def test_values_decay_toward_baseline_with_half_life() -> None:
    s = replace(initial_state(P), valence=1.0, arousal=1.0, social=1.0)
    rng = random.Random(0)
    s, _ = step(s, [], P.arousal.half_life_s, P, rng)
    expected_arousal = P.arousal.baseline + (1.0 - P.arousal.baseline) / 2
    assert s.arousal == pytest.approx(expected_arousal, abs=1e-6)

    s = replace(initial_state(P), valence=1.0, social=1.0)
    s, _ = step(s, [], P.valence.half_life_s, P, rng)
    assert s.valence == pytest.approx(P.valence.baseline + (1.0 - P.valence.baseline) / 2, abs=1e-6)

    s = replace(initial_state(P), social=1.0)
    s, _ = step(s, [], P.social.half_life_s, P, rng)
    assert s.social == pytest.approx(P.social.baseline + (1.0 - P.social.baseline) / 2, abs=1e-6)


def test_long_quiet_returns_feelings_to_baseline() -> None:
    s = replace(initial_state(P), valence=-1.0, arousal=1.0)
    s, _ = run_ticks(s, 300, random.Random(0))
    assert s.valence == pytest.approx(P.valence.baseline, abs=0.01)
    assert s.arousal == pytest.approx(P.arousal.baseline, abs=0.01)


def test_boredom_rises_without_events_and_drops_with_one() -> None:
    s, _ = run_ticks(initial_state(P), 600, random.Random(0))
    assert s.boredom > 0.7
    before = s.boredom
    s, _ = step(s, [HEAD], TICK_S, P, random.Random(0))
    assert s.boredom < before - 0.3


def test_energy_drains_awake_and_recovers_asleep() -> None:
    s, _ = run_ticks(initial_state(P), 600, random.Random(0))
    assert s.energy == pytest.approx(1.0 - P.energy_drain_per_hour / 6, abs=0.01)
    asleep = replace(s, asleep=True, energy=0.2)
    asleep, _ = run_ticks(asleep, 600, random.Random(0))
    assert asleep.energy == pytest.approx(0.2 + P.energy_recover_asleep_per_hour / 6, abs=0.01)


# ---- habituation ----


def valence_gain(s: MoodState, rng: random.Random) -> tuple[MoodState, float]:
    after, _ = step(s, [HEAD], 0.5, P, rng)
    decayed, _ = step(s, [], 0.5, P, random.Random(0))
    return after, after.valence - decayed.valence


def test_habituation_weakens_then_annoys_then_recovers() -> None:
    rng = random.Random(0)
    s = replace(initial_state(P), valence=-0.5)  # room to rise without clamping
    gains = []
    for _ in range(10):
        s, gain = valence_gain(s, rng)
        gains.append(gain)
    assert gains[0] > 0.3
    assert gains[1] < gains[0] and gains[3] < gains[1]  # weaker each time
    assert all(g < 0 for g in gains[7:])  # past the threshold: annoyance

    s, _ = run_ticks(s, 60, rng)  # habituation fades
    s = replace(s, valence=-0.5)
    _, gain = valence_gain(s, rng)
    assert gain > 0.3


def test_ten_fast_pats_make_it_mildly_angry() -> None:
    pats = {i: [HEAD] for i in range(0, 50, 5)}  # 10 pats, 0.5 s apart
    for seed in range(5):
        rng = random.Random(seed)
        s, during = run_ticks(initial_state(P), 5, rng, pats)
        _, after = run_ticks(s, 10, rng)
        angry = [m for m in moods(during + after) if m.name == "angry"]
        assert angry, f"seed {seed}: never angry"
        assert max(m.intensity for m in angry) <= 75, f"seed {seed}: too angry"


@pytest.mark.parametrize(
    ("pats", "every_ticks"),
    [(6, 5), (10, 30)],  # 6 fast pats; 10 pats 3 s apart
    ids=["six-fast", "ten-slow"],
)
def test_fewer_or_slower_pats_never_annoy(pats: int, every_ticks: int) -> None:
    events = {i * every_ticks: [HEAD] for i in range(pats)}
    for seed in range(5):
        seconds = pats * every_ticks * TICK_S + 15
        _, sent = run_ticks(initial_state(P), seconds, random.Random(seed), events)
        assert "angry" not in {m.name for m in moods(sent)}


# ---- mood selection ----


def test_touch_head_reads_happy() -> None:
    for seed in range(10):
        _, sent = run_ticks(initial_state(P), 1, random.Random(seed), {2: [HEAD]})
        happy = [m for m in moods(sent) if m.name == "happy"]
        assert happy and happy[0].intensity >= 60


def test_no_flicker_near_a_boundary() -> None:
    """Valence wobbling around the happy/neutral boundary must not toggle the mood."""
    rng = random.Random(0)
    s, _ = run_ticks(initial_state(P), 1, rng)
    changes = 0
    shown = s.mood
    for i in range(600):  # 60 s
        wobble = 0.30 + (0.04 if i % 2 else -0.04)  # happy score ~0.25-0.4 vs neutral 0.25
        s, _ = step(replace(s, valence=wobble), [], TICK_S, P, rng)
        if s.mood != shown:
            changes += 1
            shown = s.mood
    assert changes <= 1


def test_min_hold_time() -> None:
    """After a switch, a small advantage can't switch again until min_hold_s has passed."""
    rng = random.Random(0)
    s, _ = step(initial_state(P), [HEAD], TICK_S, P, rng)
    assert s.mood == "happy"
    switched_at = s.mood_since
    # Surprise beats happy by ~0.25: more than hysteresis (0.12), less than override (0.35).
    feel = {"valence": 0.375, "arousal": 0.825}  # happy ~0.5, surprised ~0.75
    s, _ = step(replace(s, **feel), [], TICK_S, P, rng)
    assert s.mood == "happy"
    while s.t - switched_at < P.min_hold_s + TICK_S:
        s, _ = step(replace(s, **feel), [], TICK_S, P, rng)
    assert s.mood == "surprised"


def test_mood_resent_only_on_change_of_10() -> None:
    _, sent = run_ticks(initial_state(P), 120, random.Random(0), {10: [HEAD]})
    ms = moods(sent)
    for prev, cur in zip(ms, ms[1:], strict=False):
        assert cur.name != prev.name or abs(cur.intensity - prev.intensity) >= 10


# ---- sleep ----


def test_sleepy_dozes_off_and_an_event_wakes_it() -> None:
    rng = random.Random(0)
    s = replace(initial_state(P), energy=0.12, boredom=0.0)
    s, sent = run_ticks(s, 200, rng)
    assert s.asleep and s.mood == "sleepy" and s.intensity == 100
    assert Mood("sleepy", 100) in moods(sent)
    _, quiet = run_ticks(s, 60, rng)
    assert not [m for _, m in quiet if isinstance(m, Anim)]  # no idle anims while asleep
    s, _ = step(s, [HEAD], TICK_S, P, rng)
    assert not s.asleep
    s, _ = run_ticks(s, P.idle_quiet_after_event_s - 1, rng)
    assert not s.asleep  # doesn't fall straight back asleep


def test_very_low_energy_falls_asleep() -> None:
    s = replace(initial_state(P), energy=P.asleep_below - 0.01)
    s, _ = run_ticks(s, P.idle_quiet_after_event_s + 1, random.Random(0))
    assert s.asleep


def test_idle_anims_happen_when_awake() -> None:
    _, sent = run_ticks(initial_state(P), 120, random.Random(0))
    anims = {m.name for _, m in sent if isinstance(m, Anim)}
    assert anims and anims <= set(P.idle_anims)


# ---- outputs ----


def test_rate_limit_five_per_second() -> None:
    # No habituation, so every touch wants an anim: 3 per tick = 30/s wanted.
    eager = replace(P, annoyed_above=1e9, min_response=1.0)
    rng = random.Random(0)
    burst = [Event("touch", "left"), Event("imu", "pickup"), Event("touch", "right")]
    state, sent = initial_state(eager), []
    for _ in range(100):
        state, outputs = step(state, burst, TICK_S, eager, rng)
        sent += [(state.t, m) for m in outputs]
    times = [t for t, _ in sent]
    assert len(times) >= 45  # ~5/s for 10 s
    for i, t in enumerate(times):
        in_window = [u for u in times[i:] if u - t < 1.0 - 1e-9]
        assert len(in_window) <= eager.max_messages_per_s


def random_events(rng: random.Random, n: int) -> dict[int, list[Event]]:
    all_events = [Event(s, d) for s, details in EVENTS.items() for d in details]
    return {rng.randrange(6000): [rng.choice(all_events)] for _ in range(n)}


def test_same_seed_same_outputs() -> None:
    events = random_events(random.Random(42), 200)
    a = run_ticks(initial_state(P), 600, random.Random(7), events)[1]
    b = run_ticks(initial_state(P), 600, random.Random(7), events)[1]
    c = run_ticks(initial_state(P), 600, random.Random(8), events)[1]
    assert a == b
    assert a != c


@pytest.mark.parametrize("seed", range(5))
def test_every_output_is_valid_protocol(seed: int) -> None:
    events = random_events(random.Random(seed), 300)
    _, sent = run_ticks(initial_state(P), 600, random.Random(seed), events)
    assert sent
    for _, msg in sent:
        assert isinstance(msg, (Mood, Anim))
        assert parse_line(encode(msg)) == msg


# ---- scenarios (sim/mood_replay.py) ----


@pytest.fixture(scope="module")
def replay():
    sys.path.insert(0, str(SIM_DIR))
    try:
        import mood_replay
    finally:
        sys.path.remove(str(SIM_DIR))
    return mood_replay


def scenario_moods(replay, name: str, seed: int = 0) -> list[tuple[float, Mood]]:
    steps = replay.load_script(SCENARIOS / f"{name}.script")
    sent = []
    for state, outputs in replay.replay(steps, P, seed, tail_s=60):
        sent += [(state.t, m) for m in outputs if isinstance(m, Mood)]
    for _, m in sent:
        assert parse_line(encode(m)) == m
    return sent


def test_scenario_petting(replay) -> None:
    names = [m.name for _, m in scenario_moods(replay, "petting")]
    assert names[1] == "happy"
    assert "angry" in names[names.index("happy") :]


def test_scenario_ignored(replay) -> None:
    sent = scenario_moods(replay, "ignored")
    before_pat = [m for t, m in sent if t < 3601]
    assert "curious" in [m.name for m in before_pat]
    assert before_pat[-1] == Mood("sleepy", 100)  # asleep when the pat comes
    assert [m for t, m in sent if t >= 3601][0].name == "happy"


def test_scenario_shaken(replay) -> None:
    names = [m.name for _, m in scenario_moods(replay, "shaken")]
    assert names.index("surprised") < names.index("angry") < names.index("sad")


def test_replay_writes_csv(replay, tmp_path: Path) -> None:
    out = tmp_path / "shaken.csv"
    assert replay.main([str(SCENARIOS / "shaken.script"), "--out", str(out)]) == 0
    lines = out.read_text().splitlines()
    assert lines[0] == ",".join(replay.COLUMNS)
    assert any("MOOD surprised" in line for line in lines)


# ---- the engine on a real (pseudo) serial link ----


def test_engine_loop_touch_head_happy_within_1s(pty: Pty) -> None:
    link = SerialLink.open(pty.path, timeout=0.02)
    stop = threading.Event()
    thread = threading.Thread(target=run, args=(link, MoodEngine(P, seed=1), time.monotonic, stop))
    thread.start()
    try:
        assert pty.read_line() == b"MOOD neutral 0.50\n"
        pty.write(b"EVT touch head\n")
        start = time.monotonic()
        line = pty.read_line(timeout=1.0)
        assert line.startswith(b"MOOD happy ")
        assert time.monotonic() - start < 1.0
    finally:
        stop.set()
        thread.join(timeout=2)
        link.close()
