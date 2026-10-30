"""Mood engine model: a pure, deterministic step(state, events, dt) -> (state, outputs).

All randomness comes from the `rng` argument (a seeded random.Random), and all time
comes from `dt`, so the same seed, events and dts always give the same outputs.
"""

from __future__ import annotations

import math
import random
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field, replace

from petbot_brain.mood.personality import Personality, event_key
from petbot_brain.protocol import MOODS, Anim, Event, Message, Mood

RATE_WINDOW_S = 1.0
EPS = 1e-6  # simulated time is a float sum of dts; compare durations with this slack


@dataclass(frozen=True)
class MoodState:
    t: float  # seconds since the engine started
    valence: float  # -1 .. 1
    arousal: float  # 0 .. 1
    energy: float  # 0 .. 1
    social: float  # 0 .. 1
    boredom: float  # 0 .. 1
    asleep: bool
    mood: str  # the model's mood (protocol name); a reflex may show something else
    intensity: int  # 0 .. 100
    mood_since: float
    habituation: Mapping[str, float] = field(default_factory=dict)  # event key -> recent repeats
    sent_mood: str | None = None
    sent_intensity: int | None = None
    sent_times: tuple[float, ...] = ()  # output times within the last RATE_WINDOW_S
    next_idle_anim_t: float = 0.0
    last_event_t: float = -math.inf
    reflex_mood: str | None = None  # shown instead of `mood` until reflex_until
    reflex_intensity: int = 0
    reflex_until: float = -math.inf
    reflex_fired: Mapping[str, float] = field(default_factory=dict)  # event key -> last fired
    pending_anims: tuple[tuple[float, str], ...] = ()  # (due time, anim) from reflex sequences
    last_touch_t: float = -math.inf
    # trigger name -> (condition true since, last fired, fired this episode)
    triggers: Mapping[str, tuple[float | None, float, bool]] = field(default_factory=dict)

    def shown(self) -> tuple[str, int]:
        """The mood and intensity on the face: an active reflex, else the model's mood."""
        if self.reflex_mood is not None and self.t < self.reflex_until:
            return self.reflex_mood, self.reflex_intensity
        return self.mood, self.intensity


def initial_state(p: Personality) -> MoodState:
    return MoodState(
        t=0.0,
        valence=p.valence.baseline,
        arousal=p.arousal.baseline,
        energy=p.energy_start,
        social=p.social_start,
        boredom=p.boredom_start,
        asleep=False,
        mood="neutral",
        intensity=p.neutral_intensity,
        mood_since=-math.inf,  # the first event may change the mood immediately
        next_idle_anim_t=p.idle_quiet_after_event_s,
    )


def _approach(value: float, target: float, half_life_s: float, dt: float) -> float:
    return value + (target - value) * (1.0 - 0.5 ** (dt / half_life_s))


def _clamp(value: float, lo: float, hi: float) -> float:
    return lo if value < lo else hi if value > hi else value


def ramp(x: float, start: float, full: float) -> float:
    """0 at `start`, 1 at `full`, linear between (works for full < start too)."""
    if start == full:
        return 1.0 if x >= full else 0.0
    return _clamp((x - start) / (full - start), 0.0, 1.0)


def mood_scores(s: MoodState, p: Personality) -> dict[str, float]:
    signals = {
        "valence": s.valence,
        "arousal": s.arousal,
        "energy": s.energy,
        "social": s.social,
        "boredom": s.boredom,
    }
    scores = {"neutral": p.neutral_score}
    for mood, ranges in p.mood_ranges.items():
        score = 1.0
        for signal, (start, full) in ranges.items():
            score *= ramp(signals[signal], start, full)
        scores[mood] = score
    return scores


def _loneliness(s: MoodState, p: Personality) -> float:
    """0 at or above lonely_below, rising to 1 at social = 0."""
    if s.social >= p.lonely_below or p.lonely_below <= 0.0:
        return 0.0
    return 1.0 - s.social / p.lonely_below


def _valence_baseline(s: MoodState, p: Personality) -> float:
    return p.valence.baseline + p.lonely_valence * _loneliness(s, p)


def _decay(s: MoodState, p: Personality, dt: float) -> MoodState:
    if s.asleep:
        energy = s.energy + p.energy_recover_asleep_per_hour * dt / 3600.0
    elif s.mood == "sleepy":
        energy = s.energy + p.energy_recover_sleepy_per_hour * dt / 3600.0
    else:
        energy = s.energy - p.energy_drain_per_hour * dt / 3600.0
    habituation = {
        key: level * 0.5 ** (dt / p.habituation_half_life_s)
        for key, level in s.habituation.items()
        if level * 0.5 ** (dt / p.habituation_half_life_s) >= 0.01
    }
    return replace(
        s,
        t=s.t + dt,
        valence=_approach(s.valence, _valence_baseline(s, p), p.valence.half_life_s, dt),
        arousal=_approach(s.arousal, p.arousal.baseline, p.arousal.half_life_s, dt),
        social=_approach(s.social, p.social.baseline, p.social.half_life_s, dt),
        boredom=_approach(s.boredom, p.boredom_idle_target, p.boredom_rise_half_life_s, dt),
        energy=_clamp(energy, 0.0, 1.0),
        habituation=habituation,
    )


def _apply_event(
    s: MoodState, event: Event, p: Personality, rng: random.Random
) -> tuple[MoodState, list[Message]]:
    """Returns (state, reaction anims). Reflex anims are scheduled in pending_anims."""
    key = event_key(event.source, event.detail)
    anims: list[Message] = []

    # Reflex: a fixed reaction shown over the mood for a moment (checked before waking).
    reflex = p.reflexes.get(key)
    if (
        reflex is not None
        and (s.asleep or not reflex.only_when_asleep)
        and s.t - s.reflex_fired.get(key, -math.inf) >= reflex.cooldown_s - EPS
    ):
        s = replace(
            s,
            reflex_fired={**s.reflex_fired, key: s.t},
            pending_anims=s.pending_anims
            + tuple((s.t + after_s, anim) for anim, after_s in reflex.anims),
        )
        if reflex.mood is not None:
            s = replace(
                s,
                reflex_mood=reflex.mood,
                reflex_intensity=reflex.intensity,
                reflex_until=s.t + reflex.hold_s,
            )

    arousal_kick = 0.0
    if s.asleep:
        s = replace(s, asleep=False)
        arousal_kick = p.wake_arousal

    level = s.habituation.get(key, 0.0)
    jitter = 1.0 + rng.uniform(-p.jitter, p.jitter)
    annoyed = level >= p.annoyed_above
    spoil = 0.0
    comfort = 0.0
    if annoyed:
        impulse, scale = p.annoyance, jitter
        spoil = p.annoyance_spoil * max(0.0, s.valence)  # annoyance wipes out happiness first
    else:
        impulse = p.impulses[key]
        scale = max(p.min_response, p.weaken_per_repeat**level) * jitter
        if impulse.get("social", 0.0) > 0.0:  # a social touch comforts a lonely pet
            comfort = p.comfort_when_lonely * _loneliness(s, p) * scale
        if key in p.reactions:
            anims.append(Anim(rng.choice(p.reactions[key])))

    s = replace(
        s,
        valence=_clamp(
            s.valence + impulse.get("valence", 0.0) * scale - spoil + comfort, -1.0, 1.0
        ),
        arousal=_clamp(s.arousal + impulse.get("arousal", 0.0) * scale + arousal_kick, 0.0, 1.0),
        social=_clamp(s.social + impulse.get("social", 0.0) * scale, 0.0, 1.0),
        boredom=_clamp(s.boredom + impulse.get("boredom", 0.0) * scale, 0.0, 1.0),
        habituation={**s.habituation, key: level + 1.0},
        last_event_t=s.t,
        last_touch_t=s.t if event.source == "touch" else s.last_touch_t,
        next_idle_anim_t=s.t
        + p.idle_quiet_after_event_s
        + rng.uniform(p.idle_interval_min_s, p.idle_interval_max_s),
    )
    return s, anims


def _choose_mood(s: MoodState, p: Personality) -> MoodState:
    if s.asleep:
        if s.mood == "sleepy" and s.intensity == 100:
            return s
        return replace(s, mood="sleepy", intensity=100, mood_since=s.t)

    scores = mood_scores(s, p)
    best = max(MOODS, key=lambda m: scores[m])  # ties: first in protocol order
    current = scores[s.mood]
    mood = s.mood
    if best != s.mood:
        margin = scores[best] - current
        held = s.t - s.mood_since >= p.min_hold_s
        if (held and margin > p.hysteresis) or margin > p.override_margin:
            mood = best
    if mood == "neutral":
        intensity = p.neutral_intensity
    else:
        intensity = max(p.min_intensity, min(100, round(scores[mood] * 100)))
    if mood != s.mood:
        return replace(s, mood=mood, intensity=intensity, mood_since=s.t)
    return replace(s, intensity=intensity)


def _due_anims(s: MoodState) -> tuple[MoodState, list[Message]]:
    """Reflex sequence anims whose time has come, in order."""
    due = sorted((t, a) for t, a in s.pending_anims if t <= s.t + 1e-9)
    if not due:
        return s, []
    later = tuple((t, a) for t, a in s.pending_anims if t > s.t + 1e-9)
    return replace(s, pending_anims=later), [Anim(a) for _, a in due]


def _triggers(s: MoodState, p: Personality) -> tuple[MoodState, list[Message]]:
    """State-condition anims (hearts while petted, zzz while asleep, ...)."""
    shown_mood, shown_intensity = s.shown()
    signals = {
        "valence": s.valence,
        "arousal": s.arousal,
        "energy": s.energy,
        "social": s.social,
        "boredom": s.boredom,
    }
    states = dict(s.triggers)
    anims: list[Message] = []
    for name, trig in p.triggers.items():
        since, last_fired, fired_episode = states.get(name, (None, -math.inf, False))
        holds = (
            (trig.asleep is None or s.asleep == trig.asleep)
            and (trig.mood is None or shown_mood == trig.mood)
            and shown_intensity >= (trig.min_intensity if trig.mood is not None else 0)
            and all(signals[sig] > threshold for sig, threshold in trig.above.items())
            and (trig.touched_within_s is None or s.t - s.last_touch_t <= trig.touched_within_s)
        )
        if not holds:
            states[name] = (None, last_fired, False)
            continue
        since = s.t if since is None else since
        ready = s.t - since >= trig.hold_s - EPS
        if trig.cooldown_s > 0:
            ready = ready and s.t - last_fired >= trig.cooldown_s - EPS
        else:
            ready = ready and not fired_episode
        if ready:
            anims.append(Anim(trig.anim))
            last_fired, fired_episode = s.t, True
        states[name] = (since, last_fired, fired_episode)
    return replace(s, triggers=states), anims


def _idle_anim(s: MoodState, p: Personality, rng: random.Random) -> tuple[MoodState, list[Message]]:
    if s.asleep or s.t < s.next_idle_anim_t:
        return s, []
    anim = Anim(rng.choice(p.idle_anims))
    return replace(
        s, next_idle_anim_t=s.t + rng.uniform(p.idle_interval_min_s, p.idle_interval_max_s)
    ), [anim]


def step(
    state: MoodState,
    events: Sequence[Event],
    dt: float,
    p: Personality,
    rng: random.Random,
) -> tuple[MoodState, list[Message]]:
    """Advance the model by `dt` seconds, apply `events`, and return the messages to send."""
    s = _decay(state, p, max(0.0, dt))

    wanted_anims: list[Message] = []
    for event in events:
        s, anims = _apply_event(s, event, p, rng)
        wanted_anims += anims
    s, reflex_anims = _due_anims(s)
    if not events:
        s, anims = _idle_anim(s, p, rng)
        wanted_anims += anims

    # Fall asleep on very low energy, or after dozing (sleepy with no events) for a while.
    # Only after a quiet spell, so an event that wakes it isn't undone at once.
    quiet = s.t - s.last_event_t >= p.idle_quiet_after_event_s
    dozing = s.mood == "sleepy" and s.t - max(s.mood_since, s.last_event_t) >= p.doze_after_s
    if not s.asleep and quiet and (s.energy < p.asleep_below or dozing):
        s = replace(s, asleep=True)
    s = _choose_mood(s, p)
    s, trigger_anims = _triggers(s, p)
    wanted_anims = trigger_anims + wanted_anims

    # Outputs, all rate-limited: reflex anims, then MOOD (only when the shown mood changed
    # enough), then reaction/idle anims. A MOOD that doesn't fit is retried next step
    # (sent_* is unchanged); anims that don't fit are dropped.
    sent_times = tuple(t for t in s.sent_times if s.t - t < RATE_WINDOW_S)
    budget = p.max_messages_per_s - len(sent_times)
    outputs: list[Message] = reflex_anims[: max(0, budget)]
    budget -= len(outputs)
    mood, intensity = s.shown()
    moved = abs(intensity - s.sent_intensity) if s.sent_intensity is not None else 0
    # Falling asleep always shows (sleepy 92 -> 100 is under the threshold but means "asleep").
    mood_changed = (
        mood != s.sent_mood or moved >= p.min_intensity_change or (s.asleep and moved > 0)
    )
    if mood_changed and budget > 0:
        outputs.append(Mood(mood, intensity))
        s = replace(s, sent_mood=mood, sent_intensity=intensity)
        budget -= 1
    for anim in wanted_anims[: max(0, budget)]:
        outputs.append(anim)
    return replace(s, sent_times=sent_times + (s.t,) * len(outputs)), outputs
