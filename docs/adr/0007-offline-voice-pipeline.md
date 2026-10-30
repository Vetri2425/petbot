# 0007. Offline voice pipeline

Status: Accepted

## Context
Phase 2 adds voice chat that must work offline on a 2GB board.

## Decision
Pipeline: openWakeWord -> Vosk small EN (or whisper.cpp tiny) -> Qwen2.5-0.5B-Instruct Q4
via llama.cpp -> Piper TTS. English first.

## Consequences
- Pipeline needs about 1.0-1.2GB RAM (drives ADR 0002).
- STT choice Vosk vs whisper.cpp tiny: TBD, decided by accuracy/latency tests in the 2GB Docker
  container.
- Models are never committed to git; they are downloaded separately.
