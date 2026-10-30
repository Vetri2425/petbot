import os
import select
import time
from collections.abc import Iterator

import pytest


class Pty:
    """A pseudo-terminal pair: tests write/read the master, SerialLink opens the slave."""

    def __init__(self) -> None:
        self.master, self._slave = os.openpty()
        self.path = os.ttyname(self._slave)

    def write(self, data: bytes) -> None:
        os.write(self.master, data)

    def read_line(self, timeout: float = 2.0) -> bytes:
        buf = b""
        deadline = time.monotonic() + timeout
        while not buf.endswith(b"\n"):
            remaining = deadline - time.monotonic()
            ready, _, _ = select.select([self.master], [], [], max(remaining, 0))
            if not ready:
                raise TimeoutError(f"no full line within {timeout}s, got {buf!r}")
            buf += os.read(self.master, 1)
        return buf

    def close_master(self) -> None:
        if self.master >= 0:
            os.close(self.master)
            self.master = -1

    def close(self) -> None:
        self.close_master()
        os.close(self._slave)


@pytest.fixture
def pty() -> Iterator[Pty]:
    p = Pty()
    yield p
    p.close()
