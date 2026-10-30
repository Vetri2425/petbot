# brain

Python package `petbot_brain` that runs on the Radxa ZERO 3W (Linux).
It is the "decider": mood, and in Phase 2 the offline voice pipeline and local LLM.
It sends commands to and receives events from the ESP32-S3 over UART (see `protocol/`).
Plain Python, no ROS 2. Currently a placeholder package with one passing test.
Dev: `pip install -e ".[dev]"`, then `ruff check .` and `pytest`.
