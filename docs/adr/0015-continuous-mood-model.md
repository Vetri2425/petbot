# 0015. Pet behaviour is a continuous mood model tuned by data

Status: Accepted

## Context
A fixed event -> reaction table felt like a button box. The pet needs moods that build up
and fade, needs, habituation and some randomness, tunable without code changes.

## Decision
- The brain's behaviour comes from a deterministic, seeded state model (feelings + needs)
  stepped at 10 Hz; all tuning lives in `brain/config/personality.toml`.
- A reflex layer sits on top: a short fixed reaction to an event (e.g. pickup -> surprised
  90 for 1.5 s) overrides the shown mood, then hands control back. Reflexes still feed the
  model, and outputs keep the rate limit and dedupe rules.
- The Phase 2 LLM feeds events/impulses into this model; it never sets moods directly.
- Sadness needs a real cause (a fall, rough handling). Being ignored only goes
  curious -> sleepy -> asleep: loneliness may lower valence slightly but never to sad,
  so the pet isn't sad every time you come back from lunch.

## Consequences
- Tuning is a data change, checked offline with `sim/mood_replay.py`.
- Tests pin behaviour by seed; the same events and seed give the same outputs.
- Voice/LLM output stays consistent with the pet's current mood and needs.
