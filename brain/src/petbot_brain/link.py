"""Serial transport for the PetBot line protocol (pyserial)."""

from __future__ import annotations

import logging
from collections.abc import Iterator
from types import TracebackType
from typing import Final

import serial

from petbot_brain.protocol import MAX_LINE_BYTES, Message, ProtocolError, encode, parse_line

log = logging.getLogger(__name__)

DEFAULT_BAUD: Final = 921600
DEFAULT_TIMEOUT_S: Final = 0.1


class LinkClosed(Exception):
    """The serial port could not be opened, was closed or disconnected."""


class SerialLink:
    """Send and receive protocol messages over a serial port.

    Invalid lines are logged and skipped. I/O errors are raised as LinkClosed.
    """

    def __init__(self, port: serial.SerialBase) -> None:
        self._port = port
        self._buf = bytearray()
        self._discarding = False

    @classmethod
    def open(
        cls, port: str, baud: int = DEFAULT_BAUD, timeout: float = DEFAULT_TIMEOUT_S
    ) -> SerialLink:
        """Open `port`; reads return after at most `timeout` seconds."""
        try:
            return cls(serial.Serial(port, baud, timeout=timeout))
        except (serial.SerialException, OSError) as e:
            raise LinkClosed(f"cannot open {port}: {e}") from e

    def close(self) -> None:
        self._port.close()

    def __enter__(self) -> SerialLink:
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: TracebackType | None,
    ) -> None:
        self.close()

    def send(self, msg: Message) -> None:
        data = encode(msg)
        try:
            self._port.write(data)
            self._port.flush()
        except (serial.SerialException, OSError) as e:
            raise LinkClosed(f"write failed: {e}") from e
        log.debug("tx %r", data)

    def receive(self) -> Message | None:
        """Return the next valid message, or None if the read timed out."""
        while True:
            line = self._next_line()
            if line is None:
                return None
            log.debug("rx %r", line)
            try:
                return parse_line(line)
            except ProtocolError as e:
                log.warning("skipping invalid line %r: %s", line, e)

    def __iter__(self) -> Iterator[Message]:
        """Yield valid messages until the link closes; timeouts are skipped."""
        while True:
            try:
                msg = self.receive()
            except LinkClosed as e:
                log.info("link closed: %s", e)
                return
            if msg is not None:
                yield msg

    def _next_line(self) -> bytes | None:
        while True:
            end = self._buf.find(b"\n")
            if end >= 0:
                line = bytes(self._buf[: end + 1])
                del self._buf[: end + 1]
                if self._discarding:
                    # Tail of an over-long line, already reported.
                    self._discarding = False
                    continue
                return line
            if len(self._buf) >= MAX_LINE_BYTES:
                if not self._discarding:
                    log.warning("skipping line longer than %d bytes", MAX_LINE_BYTES)
                self._discarding = True
                self._buf.clear()
            chunk = self._read()
            if not chunk:
                return None
            self._buf += chunk

    def _read(self) -> bytes:
        try:
            return self._port.read(self._port.in_waiting or 1)
        except (serial.SerialException, OSError) as e:
            raise LinkClosed(f"read failed: {e}") from e
