"""PetBot UART line protocol v1 (see protocol/PROTOCOL.md). Pure functions, no I/O.

The allowed names live here and only here; PROTOCOL.md is kept in sync by a test.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Final, TypeAlias

MAX_LINE_BYTES: Final = 128

MOODS: Final[tuple[str, ...]] = (
    "neutral",
    "happy",
    "sad",
    "angry",
    "sleepy",
    "curious",
    "surprised",
)

EVENTS: Final[dict[str, tuple[str, ...]]] = {
    "touch": ("head", "left", "right", "back"),
    "imu": ("pickup", "shake", "tilt", "fall"),
}

_NAME_RE: Final = re.compile(r"[a-z0-9_]+")
MAX_INTENSITY: Final = 100  # hundredths: 100 is "1.00" on the wire
_INTENSITY_RE: Final = re.compile(r"([0-9]+)(?:\.([0-9]{1,2}))?")


class ProtocolError(ValueError):
    """A line or message does not follow the protocol."""


@dataclass(frozen=True)
class Mood:
    """Linux -> ESP32: set the mood. Intensity is in hundredths, 0-100 ("0.80" is 80)."""

    name: str
    intensity: int

    def __post_init__(self) -> None:
        if self.name not in MOODS:
            raise ProtocolError(f"unknown mood {self.name!r}")
        if (
            isinstance(self.intensity, bool)
            or not isinstance(self.intensity, int)
            or not 0 <= self.intensity <= MAX_INTENSITY
        ):
            raise ProtocolError(f"intensity must be an int 0-100, got {self.intensity!r}")


@dataclass(frozen=True)
class Anim:
    """Linux -> ESP32: play a named animation."""

    name: str

    def __post_init__(self) -> None:
        if not isinstance(self.name, str) or not _NAME_RE.fullmatch(self.name):
            raise ProtocolError(f"invalid animation name {self.name!r}")


@dataclass(frozen=True)
class Event:
    """ESP32 -> Linux: something happened on the body."""

    source: str
    detail: str

    def __post_init__(self) -> None:
        details = EVENTS.get(self.source)
        if details is None:
            raise ProtocolError(f"unknown event source {self.source!r}")
        if self.detail not in details:
            raise ProtocolError(f"unknown {self.source} event {self.detail!r}")


Message: TypeAlias = Mood | Anim | Event


def parse_line(line: str | bytes) -> Message:
    """Parse one line (with or without its LF / CR LF terminator) into a message.

    Raises ProtocolError on any invalid input.
    """
    if isinstance(line, str):
        try:
            raw = line.encode("ascii")
        except UnicodeEncodeError as e:
            raise ProtocolError("non-ASCII characters") from e
    else:
        raw = bytes(line)

    size = len(raw) if raw.endswith(b"\n") else len(raw) + 1
    if size > MAX_LINE_BYTES:
        raise ProtocolError(f"line too long ({size} > {MAX_LINE_BYTES} bytes)")
    if not raw.isascii():
        raise ProtocolError("non-ASCII bytes")

    text = raw.decode("ascii")
    if text.endswith("\r\n"):
        text = text[:-2]
    elif text.endswith("\n"):
        text = text[:-1]
    if not text:
        raise ProtocolError("empty line")
    if not text.isprintable():
        raise ProtocolError("control characters in line")

    verb, *args = text.split(" ")
    if "" in args or not verb:
        raise ProtocolError("tokens must be separated by single spaces")

    if verb == "MOOD":
        _expect_args(verb, args, 2)
        name, intensity = args
        match = _INTENSITY_RE.fullmatch(intensity)
        if match is None:
            raise ProtocolError(f"invalid intensity {intensity!r}")
        whole, frac = match.groups()
        hundredths = int(whole) * 100 + int((frac or "0").ljust(2, "0"))
        if hundredths > MAX_INTENSITY:
            raise ProtocolError(f"intensity must be 0.0-1.0, got {intensity!r}")
        return Mood(name, hundredths)
    if verb == "ANIM":
        _expect_args(verb, args, 1)
        return Anim(args[0])
    if verb == "EVT":
        _expect_args(verb, args, 2)
        return Event(args[0], args[1])
    raise ProtocolError(f"unknown command {verb!r}")


def encode(msg: Message) -> bytes:
    """Encode a message as one LF-terminated ASCII line."""
    if isinstance(msg, Mood):
        text = f"MOOD {msg.name} {msg.intensity // 100}.{msg.intensity % 100:02d}"
    elif isinstance(msg, Anim):
        text = f"ANIM {msg.name}"
    elif isinstance(msg, Event):
        text = f"EVT {msg.source} {msg.detail}"
    else:
        raise ProtocolError(f"cannot encode {type(msg).__name__}")
    data = (text + "\n").encode("ascii")
    if len(data) > MAX_LINE_BYTES:
        raise ProtocolError(f"encoded line too long ({len(data)} > {MAX_LINE_BYTES} bytes)")
    return data


def _expect_args(verb: str, args: list[str], count: int) -> None:
    if len(args) != count:
        raise ProtocolError(f"{verb} takes {count} argument(s), got {len(args)}")
