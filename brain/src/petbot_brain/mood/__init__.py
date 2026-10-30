"""Mood engine: continuous feelings and needs -> protocol MOOD/ANIM messages."""

from petbot_brain.mood.engine import MoodEngine, run
from petbot_brain.mood.model import MoodState, initial_state, mood_scores, step
from petbot_brain.mood.personality import Personality, PersonalityError, load

__all__ = [
    "MoodEngine",
    "MoodState",
    "Personality",
    "PersonalityError",
    "initial_state",
    "load",
    "mood_scores",
    "run",
    "step",
]
