# 0002. Main board: Radxa ZERO 3W 2GB

Status: Accepted

## Context
The Linux brain must run the Phase 2 voice pipeline, which needs about 1.0-1.2GB RAM.

## Decision
Use the Radxa ZERO 3W (RK3566) with 2GB RAM and 16GB eMMC.

## Consequences
- The 1GB variant is rejected: it cannot fit the voice pipeline.
- About 0.8-1.0GB is left for OS and the rest of the brain; RAM is a hard budget (see
  ARCHITECTURE.md).
- The RK3566 NPU is available for Phase 3 face detection.
