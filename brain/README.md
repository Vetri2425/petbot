# brain

Python package `petbot_brain` that runs on the Radxa ZERO 3W (Linux).
It is the "decider": mood, and in Phase 2 the offline voice pipeline and local LLM.
It sends commands to and receives events from the ESP32-S3 over UART (see `protocol/`).
Plain Python, no ROS 2. Runtime dependency: `pyserial`.

| Module | Purpose |
|--------|---------|
| `protocol.py` | Message types, allowed names, `parse_line()` / `encode()` (pure, no I/O) |
| `link.py` | `SerialLink`: pyserial wrapper, skips invalid lines, raises `LinkClosed` on disconnect |
| `reactions.py` | Placeholder reaction table (`REACTIONS`) and 30 s sleepy timeout |
| `__main__.py` | CLI entry point |

## Run

```sh
pip install -e .
python -m petbot_brain --port <uart-device>     # real UART; device name TBD, decided by wiring
python -m petbot_brain --port /tmp/petbot-brain  # against sim/fake_esp32.py, see sim/README.md
```

Options: `--baud` (default 921600), `--log-level DEBUG|INFO|WARNING`. Exits with code 1 if the
port cannot be opened or disconnects.

## Develop

```sh
pip install -e ".[dev]"
ruff check .
pytest
```
