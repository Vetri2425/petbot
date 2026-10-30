"""Run the brain: python -m petbot_brain --port <dev>"""

from __future__ import annotations

import argparse
import logging
from pathlib import Path

from petbot_brain.link import DEFAULT_BAUD, LinkClosed, SerialLink
from petbot_brain.mood import MoodEngine, PersonalityError, load, run
from petbot_brain.mood.personality import DEFAULT_PATH

log = logging.getLogger("petbot_brain")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="petbot_brain", description="PetBot Linux brain")
    parser.add_argument("--port", required=True, help="serial device, e.g. /tmp/petbot-brain")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--log-level", default="INFO", choices=["DEBUG", "INFO", "WARNING"])
    parser.add_argument("--personality", type=Path, default=DEFAULT_PATH, help="personality.toml")
    parser.add_argument("--seed", type=int, help="RNG seed (default: random)")
    args = parser.parse_args(argv)

    logging.basicConfig(
        level=args.log_level, format="%(asctime)s %(levelname)s %(name)s: %(message)s"
    )
    try:
        personality = load(args.personality)
    except (OSError, PersonalityError) as e:
        log.error("cannot load personality: %s", e)
        return 1
    try:
        with SerialLink.open(args.port, args.baud, timeout=0.02) as link:
            log.info("connected to %s at %d baud", args.port, args.baud)
            run(link, MoodEngine(personality, args.seed))
    except LinkClosed as e:
        log.error("link closed: %s", e)
        return 1
    except KeyboardInterrupt:
        log.info("stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
