"""Run protocol/test_vectors.txt (shared with the C++ firmware tests) through the Python side."""

import time
from pathlib import Path
from typing import NamedTuple

import pytest
from conftest import Pty

from petbot_brain.link import SerialLink
from petbot_brain.protocol import ANIMS, EVENTS, MOODS, ProtocolError, encode, parse_line

VECTORS_PATH = Path(__file__).resolve().parents[2] / "protocol" / "test_vectors.txt"
_ESCAPES = {"\\": b"\\", "n": b"\n", "r": b"\r", "t": b"\t"}


class Vector(NamedTuple):
    lineno: int
    valid: bool
    raw: bytes
    note: str  # VALID: canonical encoding; INVALID: reason

    def __str__(self) -> str:
        return f"L{self.lineno}"


def unescape(text: str) -> bytes:
    out = bytearray()
    i = 0
    while i < len(text):
        ch = text[i]
        if ch != "\\":
            out += ch.encode("ascii")
            i += 1
            continue
        esc = text[i + 1 : i + 2]
        if esc in _ESCAPES:
            out += _ESCAPES[esc]
            i += 2
        elif esc == "x" and len(text) >= i + 4:
            out.append(int(text[i + 2 : i + 4], 16))
            i += 4
        else:
            raise ValueError(f"bad escape at {i} in {text!r}")
    return bytes(out)


def load_vectors(path: Path = VECTORS_PATH) -> list[Vector]:
    vectors = []
    for lineno, line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        kind, sep, rest = line.partition(" ")
        if kind not in ("VALID", "INVALID") or not sep:
            raise ValueError(f"{path}:{lineno}: expected VALID or INVALID")
        case, _, note = rest.partition("  #")
        note = note.strip()
        valid = kind == "VALID"
        if valid:
            if not note.startswith("=> "):
                raise ValueError(f"{path}:{lineno}: VALID needs '  # => <canonical>'")
            note = note[3:]
        vectors.append(Vector(lineno, valid, unescape(case), note))
    return vectors


VECTORS = load_vectors()


def test_unescape() -> None:
    assert unescape(r"a\\b\n\r\t\x00\xff\x20") == b"a\\b\n\r\t\x00\xff "
    with pytest.raises(ValueError):
        unescape(r"\q")


def test_vectors_file_is_not_empty() -> None:
    assert sum(v.valid for v in VECTORS) >= 30
    assert sum(not v.valid for v in VECTORS) >= 50


def test_vectors_cover_every_name() -> None:
    valid = {parse_line(v.raw) for v in VECTORS if v.valid}
    assert {m.name for m in valid if hasattr(m, "intensity")} == set(MOODS)
    events = {(m.source, m.detail) for m in valid if hasattr(m, "source")}
    assert events == {(s, d) for s, details in EVENTS.items() for d in details}
    anims = {m.name for m in valid if type(m).__name__ == "Anim"}
    assert set(ANIMS) <= anims


@pytest.mark.parametrize("vector", [v for v in VECTORS if v.valid], ids=str)
def test_valid_vector(vector: Vector) -> None:
    msg = parse_line(vector.raw)
    assert encode(msg) == vector.note.encode("ascii") + b"\n"


@pytest.mark.parametrize("vector", [v for v in VECTORS if not v.valid], ids=str)
def test_invalid_vector(vector: Vector) -> None:
    with pytest.raises(ProtocolError):
        parse_line(vector.raw)


def test_vectors_through_serial_link(pty: Pty) -> None:
    """Stream all vectors over a pty: exactly the VALID ones must come out, in order.

    Skipped: vectors with an LF inside, and a lone trailing CR (adding LF would make it CR LF).
    """
    stream = [v for v in VECTORS if b"\n" not in v.raw[:-1] and not v.raw.endswith(b"\r")]
    expected = [v.note for v in stream if v.valid]
    data = b"".join(v.raw if v.raw.endswith(b"\n") else v.raw + b"\n" for v in stream)
    with SerialLink.open(pty.path, timeout=0.05) as link:
        pty.write(data)
        received: list[str] = []
        deadline = time.monotonic() + 5
        while len(received) < len(expected) and time.monotonic() < deadline:
            msg = link.receive()
            if msg is not None:
                received.append(encode(msg).decode("ascii").rstrip("\n"))
        assert link.receive() is None, "unexpected extra message"
    assert received == expected
