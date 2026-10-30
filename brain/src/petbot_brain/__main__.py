"""Run the brain: python -m petbot_brain --port <dev>"""

from __future__ import annotations

import argparse
import logging

from petbot_brain.link import DEFAULT_BAUD, LinkClosed, SerialLink
from petbot_brain.reactions import ReactionBrain, run

log = logging.getLogger("petbot_brain")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="petbot_brain", description="PetBot Linux brain")
    parser.add_argument("--port", required=True, help="serial device, e.g. /tmp/petbot-brain")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--log-level", default="INFO", choices=["DEBUG", "INFO", "WARNING"])
    args = parser.parse_args(argv)

    logging.basicConfig(
        level=args.log_level, format="%(asctime)s %(levelname)s %(name)s: %(message)s"
    )
    try:
        with SerialLink.open(args.port, args.baud) as link:
            log.info("connected to %s at %d baud", args.port, args.baud)
            run(link, ReactionBrain())
    except LinkClosed as e:
        log.error("link closed: %s", e)
        return 1
    except KeyboardInterrupt:
        log.info("stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
