"""Load and validate brain/config/personality.toml (all mood-engine tuning)."""

from __future__ import annotations

import tomllib
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Final

from petbot_brain.protocol import ANIMS, EVENTS, MOODS, Event, ProtocolError

DEFAULT_PATH: Final = Path(__file__).resolve().parents[3] / "config" / "personality.toml"
SIGNALS: Final = ("valence", "arousal", "energy", "social", "boredom")
IMPULSE_SIGNALS: Final = ("valence", "arousal", "social", "boredom")

Range = tuple[float, float]


@dataclass(frozen=True)
class Drive:
    baseline: float
    half_life_s: float


@dataclass(frozen=True)
class Reflex:
    """A short fixed reaction to one event, shown over the mood, then control goes back."""

    mood: str | None  # shown instead of the mood for hold_s (None: anims only)
    intensity: int
    hold_s: float
    anims: tuple[tuple[str, float], ...]  # (anim, after_s), sent before any MOOD when due
    only_when_asleep: bool
    cooldown_s: float  # the reflex won't fire again within this long


@dataclass(frozen=True)
class Trigger:
    """An anim sent when a state condition has held for hold_s (not tied to one event)."""

    anim: str
    asleep: bool | None  # must be asleep (True) / awake (False); None: either
    mood: str | None  # shown mood must be this...
    min_intensity: int  # ...at least this strong
    above: Mapping[str, float]  # signal -> threshold, all must be exceeded
    touched_within_s: float | None  # a touch event must have come this recently
    hold_s: float
    cooldown_s: float  # repeat period while the condition holds; 0: once per episode


@dataclass(frozen=True)
class Personality:
    valence: Drive
    arousal: Drive
    social: Drive
    social_start: float
    lonely_below: float
    lonely_valence: float
    comfort_when_lonely: float
    boredom_start: float
    boredom_idle_target: float
    boredom_rise_half_life_s: float
    energy_start: float
    energy_drain_per_hour: float
    energy_recover_sleepy_per_hour: float
    energy_recover_asleep_per_hour: float
    asleep_below: float
    doze_after_s: float
    wake_arousal: float
    impulses: Mapping[str, Mapping[str, float]]  # "touch head" -> {"valence": 0.4, ...}
    habituation_half_life_s: float
    weaken_per_repeat: float
    min_response: float
    annoyed_above: float
    annoyance: Mapping[str, float]
    annoyance_spoil: float  # fraction of current positive valence each annoyed repeat removes
    jitter: float
    reactions: Mapping[str, tuple[str, ...]]  # "touch left" -> ("wave_left", "look_left")
    reflexes: Mapping[str, Reflex]  # "imu pickup" -> Reflex(...)
    triggers: Mapping[str, Trigger]  # name -> Trigger(...), checked in name order
    neutral_score: float
    neutral_intensity: int
    min_intensity: int
    hysteresis: float
    min_hold_s: float
    override_margin: float
    mood_ranges: Mapping[str, Mapping[str, Range]]  # every mood except neutral
    min_intensity_change: int
    max_messages_per_s: int
    idle_anims: tuple[str, ...]
    idle_quiet_after_event_s: float
    idle_interval_min_s: float
    idle_interval_max_s: float


class PersonalityError(ValueError):
    """personality.toml is missing a value, has an unknown key, or a bad value."""


def event_key(source: str, detail: str) -> str:
    return f"{source} {detail}"


class _Table:
    """Reads one TOML table; every key must be consumed exactly once (see done())."""

    def __init__(self, data: Mapping[str, Any], path: str) -> None:
        self._data = dict(data)
        self._path = path

    def raw(self, key: str) -> Any:
        if key not in self._data:
            raise PersonalityError(f"missing {self.where(key)}")
        return self._data.pop(key)

    def where(self, key: str) -> str:
        return f"{self._path}.{key}" if self._path else key

    def num(self, key: str, lo: float | None = None, hi: float | None = None) -> float:
        value = self.raw(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise PersonalityError(f"{self.where(key)} must be a number, got {value!r}")
        if (lo is not None and value < lo) or (hi is not None and value > hi):
            raise PersonalityError(f"{self.where(key)} must be in [{lo}, {hi}], got {value}")
        return float(value)

    def integer(self, key: str, lo: int, hi: int) -> int:
        value = self.raw(key)
        if isinstance(value, bool) or not isinstance(value, int) or not lo <= value <= hi:
            raise PersonalityError(f"{self.where(key)} must be an int in [{lo}, {hi}]")
        return value

    def table(self, key: str) -> _Table:
        value = self.raw(key)
        if not isinstance(value, dict):
            raise PersonalityError(f"{self.where(key)} must be a table")
        return _Table(value, self.where(key))

    def strings(self, key: str) -> tuple[str, ...]:
        value = self.raw(key)
        if not isinstance(value, list) or not value or not all(isinstance(v, str) for v in value):
            raise PersonalityError(f"{self.where(key)} must be a non-empty list of strings")
        return tuple(value)

    def keys(self) -> list[str]:
        return list(self._data)

    def has(self, key: str) -> bool:
        return key in self._data

    def boolean(self, key: str) -> bool:
        value = self.raw(key)
        if not isinstance(value, bool):
            raise PersonalityError(f"{self.where(key)} must be true or false")
        return value

    def done(self) -> None:
        if self._data:
            raise PersonalityError(f"unknown key(s) in [{self._path}]: {', '.join(self._data)}")


def _check_anim(name: object, where: str) -> str:
    """Every anim the brain may send must be in the protocol's known list."""
    if not isinstance(name, str) or name not in ANIMS:
        raise PersonalityError(f"{where}: {name!r} is not a known animation ({', '.join(ANIMS)})")
    return name


def _anims(table: _Table, key: str) -> tuple[str, ...]:
    names = table.strings(key)
    for name in names:
        _check_anim(name, table.where(key))
    return names


def _impulse(t: _Table, signals: tuple[str, ...]) -> dict[str, float]:
    impulse = {s: t.num(s, -1.0, 1.0) for s in signals if s in t.keys()}
    t.done()
    return impulse


def _mood_name(t: _Table, key: str) -> str:
    mood = t.raw(key)
    if mood not in MOODS:
        raise PersonalityError(f"{t.where(key)}: unknown mood {mood!r}")
    return mood


def _anim_sequence(t: _Table, key: str) -> tuple[tuple[str, float], ...]:
    """anims = [{ anim = "exclaim", after_s = 0.0 }, { anim = "tear", after_s = 0.8 }]"""
    items = t.raw(key)
    if not isinstance(items, list) or not items:
        raise PersonalityError(f"{t.where(key)} must be a non-empty list of {{anim, after_s}}")
    sequence = []
    for i, item in enumerate(items):
        if not isinstance(item, dict):
            raise PersonalityError(f"{t.where(key)}[{i}] must be {{ anim = ..., after_s = ... }}")
        entry = _Table(item, f"{t.where(key)}[{i}]")
        anim = _check_anim(entry.raw("anim"), entry.where("anim"))
        sequence.append((anim, entry.num("after_s", 0.0)))
        entry.done()
    return tuple(sequence)


def _reflex(t: _Table) -> Reflex:
    mood = intensity = hold_s = None
    if t.has("mood"):
        mood = _mood_name(t, "mood")
        intensity = t.integer("intensity", 0, 100)
        hold_s = t.num("hold_s", 0.001)
    anims = _anim_sequence(t, "anims") if t.has("anims") else ()
    only_when_asleep = t.boolean("only_when_asleep") if t.has("only_when_asleep") else False
    cooldown_s = t.num("cooldown_s", 0.0) if t.has("cooldown_s") else 0.0
    t.done()
    if mood is None and not anims:
        raise PersonalityError(f"[{t.where('')[:-1]}] needs a mood or anims")
    return Reflex(
        mood,
        intensity if intensity is not None else 0,
        hold_s if hold_s is not None else 0.0,
        anims,
        only_when_asleep,
        cooldown_s,
    )


def _trigger(t: _Table) -> Trigger:
    anim = _check_anim(t.raw("anim"), t.where("anim"))
    asleep = t.boolean("asleep") if t.has("asleep") else None
    mood = _mood_name(t, "mood") if t.has("mood") else None
    min_intensity = t.integer("min_intensity", 0, 100) if t.has("min_intensity") else 0
    above: dict[str, float] = {}
    if t.has("above"):
        above_t = t.table("above")
        for signal in above_t.keys():
            if signal not in SIGNALS:
                raise PersonalityError(f"{above_t.where(signal)}: unknown signal")
            above[signal] = above_t.num(signal, -1.0, 1.0)
        above_t.done()
    touched = t.num("touched_within_s", 0.0) if t.has("touched_within_s") else None
    trigger = Trigger(
        anim, asleep, mood, min_intensity, above, touched, t.num("hold_s", 0.0),
        t.num("cooldown_s", 0.0),
    )  # fmt: skip
    t.done()
    if asleep is None and mood is None and not above and touched is None:
        raise PersonalityError(f"[{t.where('')[:-1]}] needs at least one condition")
    return trigger


def _drive(t: _Table) -> Drive:
    return Drive(t.num("baseline", -1.0, 1.0), t.num("half_life_s", 0.001))


def parse(data: Mapping[str, Any]) -> Personality:
    root = _Table(data, "")
    valence_t, arousal_t = root.table("valence"), root.table("arousal")
    valence, arousal = _drive(valence_t), _drive(arousal_t)
    valence_t.done()
    arousal_t.done()

    social_t = root.table("social")
    social_start = social_t.num("start", 0.0, 1.0)
    lonely_below = social_t.num("lonely_below", 0.0, 1.0)
    lonely_valence = social_t.num("lonely_valence", -1.0, 1.0)
    comfort_when_lonely = social_t.num("comfort_when_lonely", 0.0, 2.0)
    social = _drive(social_t)
    social_t.done()

    boredom_t = root.table("boredom")
    boredom = (
        boredom_t.num("start", 0.0, 1.0),
        boredom_t.num("idle_target", 0.0, 1.0),
        boredom_t.num("rise_half_life_s", 0.001),
    )
    boredom_t.done()

    energy_t = root.table("energy")
    energy = (
        energy_t.num("start", 0.0, 1.0),
        energy_t.num("drain_per_hour", 0.0),
        energy_t.num("recover_sleepy_per_hour", 0.0),
        energy_t.num("recover_asleep_per_hour", 0.0),
        energy_t.num("asleep_below", 0.0, 1.0),
        energy_t.num("doze_after_s", 0.0),
        energy_t.num("wake_arousal", 0.0, 1.0),
    )
    energy_t.done()

    impulses_t = root.table("impulses")
    impulses: dict[str, dict[str, float]] = {}
    for source in impulses_t.keys():
        source_t = impulses_t.table(source)
        for detail in source_t.keys():
            try:
                Event(source, detail)
            except ProtocolError as e:
                raise PersonalityError(f"impulses.{source}.{detail}: {e}") from e
            impulses[event_key(source, detail)] = _impulse(source_t.table(detail), IMPULSE_SIGNALS)
        source_t.done()
    impulses_t.done()
    expected = {event_key(s, d) for s, details in EVENTS.items() for d in details}
    if missing := expected - impulses.keys():
        raise PersonalityError(f"missing impulses for: {', '.join(sorted(missing))}")

    hab_t = root.table("habituation")
    habituation = (
        hab_t.num("half_life_s", 0.001),
        hab_t.num("weaken_per_repeat", 0.0, 1.0),
        hab_t.num("min_response", 0.0, 1.0),
        hab_t.num("annoyed_above", 0.0),
        _impulse(hab_t.table("annoyance"), ("valence", "arousal")),
        hab_t.num("annoyance_spoils", 0.0, 1.0),
    )
    hab_t.done()

    jitter_t = root.table("jitter")
    jitter = jitter_t.num("impulse", 0.0, 1.0)
    jitter_t.done()

    reactions_t = root.table("reactions")
    reactions: dict[str, tuple[str, ...]] = {}
    for source in reactions_t.keys():
        source_t = reactions_t.table(source)
        for detail in source_t.keys():
            if event_key(source, detail) not in expected:
                raise PersonalityError(f"reactions.{source}.{detail}: unknown event")
            reactions[event_key(source, detail)] = _anims(source_t, detail)
        source_t.done()
    reactions_t.done()

    reflexes: dict[str, Reflex] = {}
    reflexes_t = root.table("reflexes")
    for source in reflexes_t.keys():
        source_t = reflexes_t.table(source)
        for detail in source_t.keys():
            if event_key(source, detail) not in expected:
                raise PersonalityError(f"reflexes.{source}.{detail}: unknown event")
            reflexes[event_key(source, detail)] = _reflex(source_t.table(detail))
        source_t.done()
    reflexes_t.done()

    triggers_t = root.table("triggers")
    triggers = {name: _trigger(triggers_t.table(name)) for name in sorted(triggers_t.keys())}
    triggers_t.done()

    moods_t = root.table("moods")
    mood_values = (
        moods_t.num("neutral_score", 0.0, 1.0),
        moods_t.integer("neutral_intensity", 0, 100),
        moods_t.integer("min_intensity", 0, 100),
        moods_t.num("hysteresis", 0.0, 1.0),
        moods_t.num("min_hold_s", 0.0),
        moods_t.num("override_margin", 0.0, 1.0),
    )
    mood_ranges: dict[str, dict[str, Range]] = {}
    for mood in MOODS:
        if mood == "neutral":
            continue
        mood_t = moods_t.table(mood)
        ranges: dict[str, Range] = {}
        for signal in mood_t.keys():
            if signal not in SIGNALS:
                raise PersonalityError(f"moods.{mood}.{signal}: unknown signal")
            value = mood_t.raw(signal)
            if (
                not isinstance(value, list)
                or len(value) != 2
                or not all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in value)
            ):
                raise PersonalityError(f"moods.{mood}.{signal} must be [start, full]")
            ranges[signal] = (float(value[0]), float(value[1]))
        if not ranges:
            raise PersonalityError(f"moods.{mood} needs at least one signal range")
        mood_t.done()
        mood_ranges[mood] = ranges
    moods_t.done()

    output_t = root.table("output")
    output = (
        output_t.integer("min_intensity_change", 1, 100),
        output_t.integer("max_messages_per_s", 1, 100),
    )
    output_t.done()

    idle_t = root.table("idle")
    idle = (
        _anims(idle_t, "anims"),
        idle_t.num("quiet_after_event_s", 0.0),
        idle_t.num("interval_min_s", 0.1),
        idle_t.num("interval_max_s", 0.1),
    )
    idle_t.done()
    if idle[3] < idle[2]:
        raise PersonalityError("idle.interval_max_s must be >= idle.interval_min_s")
    root.done()

    return Personality(
        valence=valence,
        arousal=arousal,
        social=social,
        social_start=social_start,
        lonely_below=lonely_below,
        lonely_valence=lonely_valence,
        comfort_when_lonely=comfort_when_lonely,
        boredom_start=boredom[0],
        boredom_idle_target=boredom[1],
        boredom_rise_half_life_s=boredom[2],
        energy_start=energy[0],
        energy_drain_per_hour=energy[1],
        energy_recover_sleepy_per_hour=energy[2],
        energy_recover_asleep_per_hour=energy[3],
        asleep_below=energy[4],
        doze_after_s=energy[5],
        wake_arousal=energy[6],
        impulses=impulses,
        habituation_half_life_s=habituation[0],
        weaken_per_repeat=habituation[1],
        min_response=habituation[2],
        annoyed_above=habituation[3],
        annoyance=habituation[4],
        annoyance_spoil=habituation[5],
        jitter=jitter,
        reactions=reactions,
        reflexes=reflexes,
        triggers=triggers,
        neutral_score=mood_values[0],
        neutral_intensity=mood_values[1],
        min_intensity=mood_values[2],
        hysteresis=mood_values[3],
        min_hold_s=mood_values[4],
        override_margin=mood_values[5],
        mood_ranges=mood_ranges,
        min_intensity_change=output[0],
        max_messages_per_s=output[1],
        idle_anims=idle[0],
        idle_quiet_after_event_s=idle[1],
        idle_interval_min_s=idle[2],
        idle_interval_max_s=idle[3],
    )


def load(path: Path = DEFAULT_PATH) -> Personality:
    with path.open("rb") as f:
        try:
            data = tomllib.load(f)
        except tomllib.TOMLDecodeError as e:
            raise PersonalityError(f"{path}: {e}") from e
    return parse(data)
