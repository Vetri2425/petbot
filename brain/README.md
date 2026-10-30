# brain

Python package `petbot_brain` that runs on the Radxa ZERO 3W (Linux).
It is the "decider": the mood engine, and in Phase 2 the offline voice pipeline and local LLM.
It sends commands to and receives events from the ESP32-S3 over UART (see `protocol/`).
Plain Python, no ROS 2. Runtime dependency: `pyserial`.

| Module | Purpose |
|--------|---------|
| `protocol.py` | Message types, allowed names, `parse_line()` / `encode()` (pure, no I/O) |
| `link.py` | `SerialLink`: pyserial wrapper, skips invalid lines, raises `LinkClosed` on disconnect |
| `mood/` | Mood engine: feelings and needs that build up and fade, habituation, randomness |
| `__main__.py` | CLI entry point (runs the mood engine) |

All mood tuning lives in [`config/personality.toml`](config/personality.toml): baselines, half-lives,
event impulses, habituation, mood thresholds, rate limits and idle behaviour. Edit it and try
the result offline with `sim/mood_replay.py` (see `sim/README.md`); no code changes needed.

## Run

```sh
pip install -e .
python -m petbot_brain --port <uart-device>     # real UART; device name TBD, decided by wiring
python -m petbot_brain --port /tmp/petbot-brain  # against sim/fake_esp32.py, see sim/README.md
```

Options: `--baud` (default 921600), `--log-level DEBUG|INFO|WARNING`,
`--personality <file.toml>` (default `config/personality.toml`), `--seed <n>` (repeatable
behaviour; default random). Exits with code 1 if the personality is invalid or the port cannot
be opened or disconnects.

## Develop

```sh
pip install -e ".[dev]"
ruff check .
pytest
```
