import logging
import threading
import time
from collections.abc import Iterator

import pytest
from conftest import Pty

from petbot_brain.link import LinkClosed, SerialLink
from petbot_brain.protocol import Anim, Event, Mood


@pytest.fixture
def link(pty: Pty) -> Iterator[SerialLink]:
    with SerialLink.open(pty.path, timeout=0.05) as link:
        yield link


def receive_next(link: SerialLink, timeout: float = 2.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        msg = link.receive()
        if msg is not None:
            return msg
    raise TimeoutError("no message received")


def test_send_writes_encoded_line(pty: Pty, link: SerialLink) -> None:
    link.send(Mood("happy", 0.8))
    link.send(Anim("wave_left"))
    assert pty.read_line() == b"MOOD happy 0.80\n"
    assert pty.read_line() == b"ANIM wave_left\n"


def test_receive_parses_lines(pty: Pty, link: SerialLink) -> None:
    pty.write(b"EVT touch head\r\nEVT imu pickup\n")
    assert receive_next(link) == Event("touch", "head")
    assert receive_next(link) == Event("imu", "pickup")


def test_receive_returns_none_on_timeout(link: SerialLink) -> None:
    assert link.receive() is None


def test_partial_line_is_buffered(pty: Pty, link: SerialLink) -> None:
    pty.write(b"EVT tou")
    assert link.receive() is None
    pty.write(b"ch back\n")
    assert receive_next(link) == Event("touch", "back")


def test_invalid_lines_are_logged_and_skipped(
    pty: Pty, link: SerialLink, caplog: pytest.LogCaptureFixture
) -> None:
    too_long = b"ANIM " + b"a" * 300 + b"\n"
    pty.write(b"HELLO\nEVT touch nose\n" + too_long + b"MOOD happy 9\nEVT imu \xff\n")
    pty.write(b"EVT imu shake\n")
    with caplog.at_level(logging.WARNING, logger="petbot_brain.link"):
        assert receive_next(link) == Event("imu", "shake")
    warnings = [r for r in caplog.records if r.levelno == logging.WARNING]
    assert len(warnings) == 5


def test_disconnect_raises_link_closed(pty: Pty, link: SerialLink) -> None:
    pty.close_master()
    with pytest.raises(LinkClosed):
        for _ in range(100):
            link.receive()


def test_iterator_yields_messages_and_stops_on_disconnect(pty: Pty, link: SerialLink) -> None:
    received = []
    reader = threading.Thread(target=lambda: received.extend(link), daemon=True)
    reader.start()
    pty.write(b"EVT touch left\nbogus\nEVT touch right\n")
    deadline = time.monotonic() + 2
    while len(received) < 2 and time.monotonic() < deadline:
        time.sleep(0.01)
    pty.close_master()
    reader.join(timeout=2)
    assert not reader.is_alive()
    assert received == [Event("touch", "left"), Event("touch", "right")]


def test_open_missing_port_raises_link_closed() -> None:
    with pytest.raises(LinkClosed):
        SerialLink.open("/dev/does-not-exist-petbot")
