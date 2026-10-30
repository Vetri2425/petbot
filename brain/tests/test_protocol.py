import re
from pathlib import Path

import pytest

from petbot_brain.protocol import (
    EVENTS,
    MAX_LINE_BYTES,
    MOODS,
    Anim,
    Event,
    Message,
    Mood,
    ProtocolError,
    encode,
    parse_line,
)

PROTOCOL_MD = Path(__file__).resolve().parents[2] / "protocol" / "PROTOCOL.md"

ALL_MESSAGES: list[Message] = (
    [Mood(name, 0.5) for name in MOODS]
    + [Anim("wave_left"), Anim("wave_right")]
    + [Event(source, detail) for source, details in EVENTS.items() for detail in details]
)


@pytest.mark.parametrize("msg", ALL_MESSAGES, ids=str)
def test_round_trip(msg: Message) -> None:
    assert parse_line(encode(msg)) == msg


@pytest.mark.parametrize("msg", ALL_MESSAGES, ids=str)
def test_round_trip_from_str(msg: Message) -> None:
    assert parse_line(encode(msg).decode("ascii")) == msg


@pytest.mark.parametrize(
    ("msg", "wire"),
    [
        (Mood("happy", 0.8), b"MOOD happy 0.80\n"),
        (Mood("sleepy", 0.4), b"MOOD sleepy 0.40\n"),
        (Mood("neutral", 0), b"MOOD neutral 0.00\n"),
        (Mood("sad", 1.0), b"MOOD sad 1.00\n"),
        (Anim("wave_left"), b"ANIM wave_left\n"),
        (Event("touch", "head"), b"EVT touch head\n"),
        (Event("imu", "pickup"), b"EVT imu pickup\n"),
    ],
)
def test_encode_format(msg: Message, wire: bytes) -> None:
    assert encode(msg) == wire


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("MOOD happy 0.8", Mood("happy", 0.8)),
        ("MOOD happy 1", Mood("happy", 1.0)),
        ("MOOD happy 0.123", Mood("happy", 0.123)),
        ("EVT touch head\r\n", Event("touch", "head")),
        (b"ANIM wave_left\r\n", Anim("wave_left")),
        (b"MOOD curious 0.60\n", Mood("curious", 0.6)),
    ],
)
def test_parse_valid(line: str | bytes, expected: Message) -> None:
    assert parse_line(line) == expected


@pytest.mark.parametrize(
    "line",
    [
        # unknown command
        "HELLO",
        "mood happy 0.5",
        "PING 1",
        # bad arguments
        "MOOD happy",
        "MOOD happy 0.5 extra",
        "MOOD grumpy 0.5",
        "MOOD happy abc",
        "MOOD happy .5",
        "MOOD happy 0.",
        "MOOD happy -0.1",
        "MOOD happy nan",
        "MOOD happy inf",
        "MOOD happy 1e-1",
        "ANIM",
        "ANIM Wave",
        "ANIM wave-left",
        "ANIM a b",
        "EVT touch",
        "EVT touch nose",
        "EVT sonar ping",
        "EVT imu head",
        "EVT touch head now",
        # out-of-range intensity
        "MOOD happy 1.01",
        "MOOD happy 2",
        # spacing
        "MOOD  happy 0.5",
        " MOOD happy 0.5",
        "MOOD happy 0.5 ",
        "MOOD\thappy 0.5",
        # control characters, empty
        "",
        "\n",
        "\r\n",
        "EVT touch head\r",
        "EVT touch\x00 head",
        # non-ASCII
        "MOOD héureux 0.5",
        "EVT touch héad".encode("utf-8"),
        b"EVT touch head\xff\n",
    ],
)
def test_parse_invalid(line: str | bytes) -> None:
    with pytest.raises(ProtocolError):
        parse_line(line)


def _anim_line(total_bytes: int, terminator: bytes = b"\n") -> bytes:
    name_len = total_bytes - len(b"ANIM ") - len(terminator)
    return b"ANIM " + b"a" * name_len + terminator


def test_max_length_accepted() -> None:
    line = _anim_line(MAX_LINE_BYTES)
    assert len(line) == 128
    assert parse_line(line) == Anim("a" * 122)


def test_max_length_without_terminator_counts_newline() -> None:
    line = _anim_line(MAX_LINE_BYTES)[:-1]
    assert parse_line(line) == Anim("a" * 122)
    with pytest.raises(ProtocolError):
        parse_line(_anim_line(MAX_LINE_BYTES + 1)[:-1])


def test_too_long_rejected() -> None:
    line = _anim_line(MAX_LINE_BYTES + 1)
    assert len(line) == 129
    with pytest.raises(ProtocolError):
        parse_line(line)


def test_crlf_counts_toward_length() -> None:
    assert parse_line(_anim_line(MAX_LINE_BYTES, b"\r\n")) == Anim("a" * 121)
    with pytest.raises(ProtocolError):
        parse_line(_anim_line(MAX_LINE_BYTES + 1, b"\r\n"))


def test_encode_rejects_too_long() -> None:
    with pytest.raises(ProtocolError):
        encode(Anim("a" * 123))


@pytest.mark.parametrize(
    "factory",
    [
        lambda: Mood("grumpy", 0.5),
        lambda: Mood("happy", 1.5),
        lambda: Mood("happy", -0.1),
        lambda: Mood("happy", float("nan")),
        lambda: Mood("happy", True),
        lambda: Anim("Wave Left"),
        lambda: Event("touch", "nose"),
        lambda: Event("sonar", "ping"),
    ],
)
def test_invalid_messages_cannot_be_built(factory) -> None:
    with pytest.raises(ProtocolError):
        factory()


def test_encode_rejects_non_message() -> None:
    with pytest.raises(ProtocolError):
        encode("MOOD happy 0.5")  # type: ignore[arg-type]


def test_messages_are_frozen() -> None:
    mood = Mood("happy", 0.5)
    with pytest.raises(AttributeError):
        mood.name = "sad"  # type: ignore[misc]


def test_protocol_md_lists_the_same_names() -> None:
    doc = PROTOCOL_MD.read_text(encoding="utf-8")
    moods = ", ".join(f"`{m}`" for m in MOODS)
    assert f"Mood names: {moods}\n" in doc
    for source, details in EVENTS.items():
        listed = ", ".join(f"`{d}`" for d in details)
        assert re.search(rf"^\| `{source}` \| {re.escape(listed)} \|$", doc, re.MULTILINE)
    documented_sources = re.findall(r"^\| `([a-z_]+)` \| `", doc, re.MULTILINE)
    assert documented_sources == list(EVENTS)
