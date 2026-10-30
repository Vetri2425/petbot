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


PICKUP = Event("imu", "pickup")


# ---- personality.toml ----


def raw_personality() -> dict:
    with DEFAULT_PATH.open("rb") as f:
        return tomllib.load(f)


def test_personality_loads() -> None:
    assert P.max_messages_per_s == 5
    pickup = P.reflexes["imu pickup"]
    assert (pickup.mood, pickup.intensity, pickup.hold_s) == ("surprised", 90, 1.5)
    head = P.reflexes["touch head"]
    assert head.anims == (("blink", 0.0),) and head.only_when_asleep
    assert P.reflexes["imu fall"].anims == (("scared", 0.0), ("exclaim", 0.0), ("tear", 0.9))
    assert P.reflexes["imu pickup"].anims == (("scared", 0.0),)
    assert set(P.triggers) == {"hearts", "zzz", "question"}
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
        lambda d: d["reflexes"]["imu"]["pickup"].update(mood="startled"),  # unknown mood
        lambda d: d["reflexes"]["imu"]["pickup"].pop("hold_s"),  # mood needs hold_s
        lambda d: d["reflexes"]["imu"].update(tilt={}),  # needs a mood or anims
        lambda d: d["reflexes"]["touch"]["head"].update(only_when_asleep="yes"),  # not a bool
        lambda d: d["reflexes"]["touch"]["head"].update(anims=[{"anim": "Blink", "after_s": 0}]),
        # valid on the wire but not a known anim: config may only use the known list
        lambda d: d["reflexes"]["touch"]["head"].update(anims=[{"anim": "spin", "after_s": 0}]),
        lambda d: d["reactions"]["touch"].update(left=["wave_left", "x_1"]),
        lambda d: d["idle"].update(anims=["blink", "wink"]),
        lambda d: d["triggers"]["zzz"].update(anim="snore"),
        lambda d: d["reflexes"]["imu"]["fall"]["anims"][1].pop("after_s"),  # entry incomplete
        lambda d: d["reflexes"]["imu"]["shake"].update(anims=[]),  # empty sequence
        lambda d: d["triggers"].update(empty={"anim": "music", "hold_s": 1, "cooldown_s": 1}),
        lambda d: d["triggers"]["question"].update(above={"hunger": 0.5}),  # unknown signal
        lambda d: d["triggers"]["hearts"].update(mood="ecstatic"),  # unknown mood
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
    s = replace(initial_state(P), valence=-1.0, arousal=1.0, social=1.0)  # not lonely
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
    asleep_anims = {m.name for _, m in quiet if isinstance(m, Anim)}
    assert asleep_anims <= {"zzz"}  # no idle glances/blinks while asleep, only snoring
    s, woke = step(s, [HEAD], TICK_S, P, rng)
    assert not s.asleep
    assert woke[0] == Anim("blink")  # reflex: blink, then the wake-up mood
    assert isinstance(woke[1], Mood) and woke[1] != Mood("sleepy", 100)
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


# ---- reflexes ----


def test_pickup_while_happy_shows_surprised_then_returns_to_happy() -> None:
    for seed in range(5):
        rng = random.Random(seed)
        s, _ = run_ticks(initial_state(P), 5, rng, {0: [HEAD], 20: [HEAD]})
        assert s.shown()[0] == "happy"
        s, sent = run_ticks(s, 5, rng, {0: [PICKUP]})
        ms = moods(sent)
        assert ms[0] == Mood("surprised", 90)
        assert ms[1].name == "happy"
        assert s.mood == "happy"  # the model stayed happy underneath


def test_reflex_timing_follows_the_injected_clock() -> None:
    """hold_s is measured on the engine's clock, not wall time: drive it with fake times."""
    hold = P.reflexes["imu pickup"].hold_s
    engine = MoodEngine(P, seed=0)
    engine.update(1000.0)
    engine.update(1001.0, [HEAD])
    out = engine.update(1010.0, [PICKUP])
    assert out[0] == Anim("scared") and Mood("surprised", 90) in out
    engine.update(1010.0 + hold - 0.05)
    assert engine.state.shown() == ("surprised", 90)
    out = engine.update(1010.0 + hold + 0.05)
    assert engine.state.shown()[0] != "surprised"
    assert out and isinstance(out[0], Mood) and out[0].name != "surprised"


def test_reflex_still_feeds_the_mood_model() -> None:
    rng = random.Random(0)
    s = initial_state(P)
    after, _ = step(s, [PICKUP], TICK_S, P, rng)
    assert after.arousal > s.arousal + 0.4  # pickup impulse applied as usual


def test_reflex_outputs_respect_the_rate_limit() -> None:
    rng = random.Random(0)
    s = replace(initial_state(P), sent_times=(0.0,) * P.max_messages_per_s, t=0.05)
    s, out = step(s, [PICKUP], 0.0, P, rng)
    assert out == []  # budget used up: nothing goes out this step...
    s, out = step(s, [], 1.0, P, rng)
    assert out[0] == Mood("surprised", 90)  # ...the reflex mood follows once there's room


def test_touch_head_reflex_only_when_asleep() -> None:
    _, out = step(initial_state(P), [HEAD], TICK_S, P, random.Random(0))
    assert Anim("blink") not in out


def anim_times(sent: list[tuple[float, Message]], name: str) -> list[float]:
    return [round(t, 2) for t, m in sent if m == Anim(name)]


def test_every_configured_anim_is_known() -> None:
    """CI check: personality.toml may only reference anims in the protocol's known list."""
    from petbot_brain.protocol import ANIMS

    used = set(P.idle_anims) | {a for names in P.reactions.values() for a in names}
    used |= {a for r in P.reflexes.values() for a, _ in r.anims}
    used |= {t.anim for t in P.triggers.values()}
    assert used <= set(ANIMS)
    assert {"hearts", "zzz", "question", "dizzy", "exclaim", "tear", "scared"} <= used


def test_fall_sends_exclaim_then_tear_on_the_engine_clock() -> None:
    engine = MoodEngine(P, seed=0)
    engine.update(100.0)
    out = engine.update(101.0, [Event("imu", "fall")])
    assert out[:2] == [Anim("scared"), Anim("exclaim")] and Mood("surprised", 100) in out
    assert Anim("tear") not in engine.update(101.85)
    assert engine.update(101.95)[0] == Anim("tear")  # 0.9 s after the fall


def test_shake_dizzy_has_a_cooldown() -> None:
    shakes = {i: [Event("imu", "shake")] for i in range(0, 30, 4)}  # every 0.4 s for 3 s
    _, sent = run_ticks(initial_state(P), 4, random.Random(0), shakes)
    times = anim_times(sent, "dizzy")
    assert len(times) == 2 and times[1] - times[0] >= P.reflexes["imu shake"].cooldown_s


def test_hearts_need_sustained_happy_petting() -> None:
    pats = {i: [HEAD] for i in range(0, 300, 30)}  # a pat every 3 s for 30 s
    _, sent = run_ticks(initial_state(P), 30, random.Random(0), pats)
    hearts = anim_times(sent, "hearts")
    assert len(hearts) == 1 and hearts[0] >= P.triggers["hearts"].hold_s

    # Just as happy, but nobody is petting it: no hearts.
    s = initial_state(P)
    _, sent = run_ticks(replace(s, valence=1.0), 30, random.Random(0))
    assert not anim_times(sent, "hearts")


def test_zzz_loops_while_asleep_and_stops_on_waking() -> None:
    rng = random.Random(0)
    s = replace(initial_state(P), asleep=True, energy=0.5, mood="sleepy", intensity=100)
    s, sent = run_ticks(s, 20, rng)
    zzz = anim_times(sent, "zzz")
    assert len(zzz) == 5  # at 2 s, then every 4 s
    assert all(b - a == pytest.approx(P.triggers["zzz"].cooldown_s) for a, b in zip(zzz, zzz[1:]))
    s, _ = step(s, [HEAD], TICK_S, P, rng)
    _, sent = run_ticks(s, 20, rng)
    assert not anim_times(sent, "zzz")


def test_question_once_per_boredom_peak() -> None:
    rng = random.Random(0)
    s, sent = run_ticks(initial_state(P), 1800, rng)  # 30 min alone
    assert len(anim_times(sent, "question")) == 1
    s, _ = step(s, [HEAD], TICK_S, P, rng)  # boredom drops...
    _, sent = run_ticks(s, 1200, rng)  # ...and peaks again
    assert len(anim_times(sent, "question")) == 1


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


@pytest.mark.parametrize("seed", range(5))
def test_scenario_petting_never_sad(replay, seed: int) -> None:
    names = [m.name for _, m in scenario_moods(replay, "petting", seed)]
    assert names[1] == "happy"
    assert "angry" in names[names.index("happy") :]  # grumpy from too many pats...
    assert "sad" not in names  # ...but over-petting is not a reason to be sad
    assert names[-1] == "neutral"


@pytest.mark.parametrize("seed", range(5))
def test_scenario_ignored_naps_and_wakes_happy(replay, seed: int) -> None:
    steps = replay.load_script(SCENARIOS / "ignored.script")
    sent: list[tuple[float, Message]] = []
    for state, outputs in replay.replay(steps, P, seed, tail_s=60):
        sent += [(state.t, m) for m in outputs]
    shown = [m for _, m in sent if isinstance(m, Mood)]
    assert "sad" not in {m.name for m in shown}  # ignored is not a reason to be sad
    before = [m.name for t, m in sent if isinstance(m, Mood) and t < 3601]
    assert before.index("happy") < before.index("curious") < before.index("sleepy")
    assert [m for t, m in sent if isinstance(m, Mood) and t < 3601][-1] == Mood("sleepy", 100)
    wake = [m for t, m in sent if 3600.5 < t < 3602]
    assert wake[0] == Anim("blink")  # reflex, then...
    assert wake[1].name == "happy" and wake[1].intensity >= 60  # ...wakes up happy


def test_loneliness_alone_cannot_reach_sad() -> None:
    lowest = P.valence.baseline + P.lonely_valence  # valence baseline at social = 0
    assert lowest > P.mood_ranges["sad"]["valence"][0]


def test_scenario_shaken(replay) -> None:
    sent = scenario_moods(replay, "shaken")
    names = [m.name for _, m in sent]
    assert names.index("surprised") < names.index("angry") < names.index("sad")
    assert sent[1] == (2.0, Mood("surprised", 90))  # pickup reflex
    assert any(t == pytest.approx(6.5) and m == Mood("surprised", 100) for t, m in sent)  # fall


def test_scenario_pickup_while_happy(replay) -> None:
    sent = scenario_moods(replay, "pickup_happy")
    shown = [(t, m) for t, m in sent if t >= 8.9]
    assert shown[0] == (9.0, Mood("surprised", 90))
    assert shown[1][1].name == "happy" and shown[1][0] == pytest.approx(10.5)


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
