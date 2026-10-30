/* PetBot face: every tunable timing and shape value lives here.
 * Left/right is the robot's own frame (ADR 0014): the robot faces the viewer, so its left
 * eye is drawn on the screen's right and look_left moves the eyes toward screen-right.
 * Only face.c (and tools that need the timings, like the simulator) include this file. */
#ifndef PETBOT_FACE_STYLE_H
#define PETBOT_FACE_STYLE_H

#include <stdint.h>

/* ---- Face style --------------------------------------------------------------------- */
/* FACE_STYLE_LINE: glowing emoticon-like line art with eyebrows and a mouth (calm eyes are
 * filled pills). FACE_STYLE_FILLED: two filled eyes with lids. face_set_style() overrides. */
#define FACE_DEFAULT_STYLE FACE_STYLE_LINE

/* ---- Screen and colors ------------------------------------------------------------- */
#define FACE_SCREEN_W 320          /* px, 2.0" ST7789 in landscape */
#define FACE_SCREEN_H 240
#define FACE_COLOR_BG 0x000000
#define FACE_COLOR_EYE 0x00E5FF    /* cyan */

/* ---- Neutral eye geometry ---------------------------------------------------------- */
#define FACE_EYE_W 72              /* px */
#define FACE_EYE_H 96
#define FACE_EYE_RADIUS 24         /* corner radius at neutral */
#define FACE_EYE_SPACING 150       /* px between the two eye centers */
#define FACE_EYE_Y (-6)            /* px, eye centers relative to the screen middle */

/* ---- Springs: every shape and position change overshoots a little, then settles -------
 * Damped springs: stiffness K (1/s^2) and damping C (1/s); damping ratio = C / (2*sqrt(K)).
 * Lower C = bouncier, higher K = faster. */
#define FACE_SPRING_SHAPE_K 220.0f  /* eye size, roundness, offsets: ~0.45 s, ~15% overshoot */
#define FACE_SPRING_SHAPE_C 15.0f
#define FACE_SPRING_LID_K 420.0f    /* lids, slants, cheek arc */
#define FACE_SPRING_LID_C 26.0f
#define FACE_SPRING_GAZE_K 6400.0f  /* eye position: saccades land in ~60-90 ms */
#define FACE_SPRING_GAZE_C 110.0f
#define FACE_SPRING_SQUASH_K 520.0f /* squash & stretch wobble */
#define FACE_SPRING_SQUASH_C 13.0f
#define FACE_SPRING_GLYPH_K 520.0f  /* glyph pop in/out */
#define FACE_SPRING_GLYPH_C 17.0f
#define FACE_SPRING_STEP_MS 2       /* integration sub-step */
#define FACE_MOOD_SETTLE_MS 700     /* roughly how long a mood change takes to settle */

/* ---- Squash & stretch (kicks are spring velocities; + = wide/short, - = tall/narrow) ---- */
#define FACE_SQUASH_LAND_KICK 3.2f      /* a hop lands */
#define FACE_SQUASH_SACCADE_KICK 1.6f   /* a saccade lands */
#define FACE_STRETCH_SURPRISE_KICK (-4.0f) /* becoming surprised */
#define FACE_STRETCH_EXCLAIM_KICK (-3.0f)
#define FACE_SQUASH_MAX 0.3f            /* clamp, fraction of the eye size */
#define FACE_BLINK_SQUASH 0.18f         /* the eye widens by this as it closes */

/* ---- Saccades (fast glances) -------------------------------------------------------- */
#define FACE_SACCADE_ANTICIPATE 0.15f   /* small opposite move first, fraction of the jump */
#define FACE_SACCADE_ANTICIPATE_MS 25
#define FACE_SACCADE_LANDED_PX 1.5f     /* within this of the target = landed */

/* ---- Idle life: blinks ------------------------------------------------------------- */
#define FACE_BLINK_INTERVAL_MIN_MS 2000
#define FACE_BLINK_INTERVAL_MAX_MS 6000
#define FACE_BLINK_CLOSE_MS 70
#define FACE_BLINK_HOLD_MS 40
#define FACE_BLINK_OPEN_MS 110
#define FACE_BLINK_CLOSED 0.92f    /* top-lid position when closed (1 = fully shut) */

/* ---- Idle life: glances ------------------------------------------------------------ */
#define FACE_GLANCE_INTERVAL_MIN_MS 1500 /* time looking ahead before the next glance */
#define FACE_GLANCE_INTERVAL_MAX_MS 4500
#define FACE_GLANCE_HOLD_MIN_MS 500      /* time spent looking away */
#define FACE_GLANCE_HOLD_MAX_MS 1400
#define FACE_GLANCE_MAX_X 14             /* px */
#define FACE_GLANCE_MAX_Y 6

/* ---- Idle life: breathing ---------------------------------------------------------- */
#define FACE_BREATH_PERIOD_MS 4000
#define FACE_BREATH_SCALE 0.025f   /* eye height +/- fraction */
#define FACE_BREATH_DY 1.5f        /* px up/down */

/* ---- Sleepy slows idle life -------------------------------------------------------- */
#define FACE_SLEEPY_SLOWDOWN 2.5f  /* time multiplier at sleepy intensity 100 */

/* ---- Animations (eye-only until arms exist) ---------------------------------------- */
#define FACE_LOOK_MS 900           /* look_left / look_right total time */
#define FACE_LOOK_X 30             /* px */
#define FACE_WAVE_MS 1000          /* wave_left / wave_right total time */
#define FACE_WAVE_X 20             /* px glance toward the waving arm's side */
#define FACE_WAVE_BOUNCES 2
#define FACE_WAVE_BOUNCE_PX 10

/* ---- Moods ------------------------------------------------------------------------- */
/* An expression is a set of deltas from neutral (all zero = neutral). The mood's
 * intensity (0-100) scales every field linearly. */
typedef struct {
    float grow_w;     /* eye width change, fraction (+0.2 = 20% wider) */
    float grow_h;     /* eye height change, fraction */
    float grow_left;  /* extra size change of the robot's left eye (screen right), fraction */
    float grow_right; /* extra size change of the robot's right eye (screen left), fraction */
    float lid_top;    /* flat top lid, 0-1 of eye height */
    float slant_in;   /* top lid lowered at the inner corners, 0-1 of eye height (angry) */
    float slant_out;  /* top lid lowered at the outer corners, 0-1 of eye height (sad) */
    float cheek;      /* bottom cut by an arc, 0-1 of eye height (happy: upward arcs) */
    float round;      /* 0 = FACE_EYE_RADIUS corners, 1 = fully round */
    float dy;         /* px, both eyes (positive = down) */
    float tilt;       /* px, robot's left eye up and right eye down (head-tilt offset) */
} face_expr_t;

#define FACE_EXPR_NEUTRAL {.grow_w = 0.0f}
#define FACE_EXPR_HAPPY {.grow_w = 0.08f, .cheek = 0.55f, .round = 0.4f, .dy = -4.0f}
#define FACE_EXPR_SAD {.grow_h = -0.10f, .lid_top = 0.08f, .slant_out = 0.45f, .dy = 10.0f}
#define FACE_EXPR_ANGRY {.grow_h = -0.05f, .lid_top = 0.05f, .slant_in = 0.55f}
#define FACE_EXPR_SLEEPY {.lid_top = 0.55f, .dy = 8.0f}
#define FACE_EXPR_CURIOUS {.grow_left = 0.18f, .grow_right = -0.10f, .tilt = 10.0f}
#define FACE_EXPR_SURPRISED {.grow_w = 0.22f, .grow_h = 0.12f, .round = 1.0f, .dy = -6.0f}

/* ---- Line style ---------------------------------------------------------------------- */
#define FACE_LINE_EYE_SPACING 112       /* px between eye centers */
#define FACE_LINE_EYE_Y (-16)           /* px, eye centers relative to the screen middle */
#define FACE_LINE_PILL_W 34             /* calm (neutral) eyes are filled pills */
#define FACE_LINE_PILL_H 56
#define FACE_LINE_SHAPE_W 52            /* box for line-art eyes */
#define FACE_LINE_SHAPE_H 44
#define FACE_LINE_RING_D 52             /* ring-eye diameter */
#define FACE_LINE_STROKE 7              /* px line width */
#define FACE_LINE_GLOW 9                /* px of glow around every line and pill */
#define FACE_LINE_GLOW_OPA 55           /* 0-255 */
#define FACE_LINE_BLINK_FLATTEN 0.85f   /* a blink squashes the eye this much */
#define FACE_LINE_BROW_LEN 42
#define FACE_LINE_BROW_GAP 14           /* px above the eye box */
#define FACE_LINE_BROW_TILT_PX 9        /* inner end up (worried) or down (angry) at tilt 1 */
#define FACE_LINE_BROW_LIFT_PX 10       /* raised brow at lift 1 */
#define FACE_LINE_MOUTH_Y 54            /* px below the screen middle */
#define FACE_LINE_MOUTH_W 40
#define FACE_LINE_MOUTH_H 13
#define FACE_LINE_MOUTH_GAZE 0.5f       /* the mouth follows the gaze this much */
#define FACE_LINE_MIN_INTENSITY 30      /* weaker moods show the calm face */
#define FACE_LINE_LAUGH_INTENSITY 90    /* happy at/above this laughs (^ ^ and a grin) */
#define FACE_LINE_FURIOUS_INTENSITY 85  /* angry at/above this fumes (> < and a wavy mouth) */

typedef enum {
    FACE_EYE_PILL,     /* filled pill (calm) */
    FACE_EYE_CRESCENT, /* upward arc (happy) */
    FACE_EYE_CHEVRON,  /* ^ (laughing) */
    FACE_EYE_DROOP,    /* low curve (sad) */
    FACE_EYE_HALF,     /* filled lower half (angry) */
    FACE_EYE_FLAT,     /* - (sleepy, squint) */
    FACE_EYE_RING,     /* O with a pupil (surprised, curious) */
    FACE_EYE_SQUINT    /* > < (fuming, scrunched) */
} face_line_eye_t;

typedef enum {
    FACE_MOUTH_NONE,
    FACE_MOUTH_SMILE,
    FACE_MOUTH_GRIN,  /* open half-disc */
    FACE_MOUTH_FROWN,
    FACE_MOUTH_FLAT,
    FACE_MOUTH_O,
    FACE_MOUTH_WAVY,
    FACE_MOUTH_DOT
} face_mouth_t;

typedef enum { FACE_BROWS_NONE, FACE_BROWS_BOTH, FACE_BROWS_LEFT } face_brows_t;

/* A line-style expression. Eyes are in the robot's frame (ADR 0014). Intensity scales
 * brow tilt/lift and mouth size; eye and mouth shapes swap with a squash pop. */
typedef struct {
    uint8_t eye_right;  /* face_line_eye_t, robot's right eye (screen left) */
    uint8_t eye_left;   /* robot's left eye (screen right) */
    uint8_t mouth;      /* face_mouth_t */
    uint8_t brows;      /* face_brows_t; LEFT = robot's left brow only */
    float brow_tilt;    /* +1 worried (inner ends up), -1 angry (inner ends down) */
    float brow_lift;    /* 0..1 raise */
    float mouth_scale;
} face_line_expr_t;

#define FACE_LINE_NEUTRAL {.eye_right = FACE_EYE_PILL, .eye_left = FACE_EYE_PILL, .mouth_scale = 1.0f}
#define FACE_LINE_HAPPY {.eye_right = FACE_EYE_CRESCENT, .eye_left = FACE_EYE_CRESCENT, \
                         .mouth = FACE_MOUTH_SMILE, .mouth_scale = 1.0f}
#define FACE_LINE_LAUGH {.eye_right = FACE_EYE_CHEVRON, .eye_left = FACE_EYE_CHEVRON, \
                         .mouth = FACE_MOUTH_GRIN, .mouth_scale = 1.0f}
#define FACE_LINE_SAD {.eye_right = FACE_EYE_DROOP, .eye_left = FACE_EYE_DROOP, \
                       .mouth = FACE_MOUTH_FROWN, .brows = FACE_BROWS_BOTH, .brow_tilt = 1.0f, \
                       .mouth_scale = 0.8f}
#define FACE_LINE_ANGRY {.eye_right = FACE_EYE_HALF, .eye_left = FACE_EYE_HALF, \
                         .mouth = FACE_MOUTH_FLAT, .brows = FACE_BROWS_BOTH, .brow_tilt = -1.0f, \
                         .mouth_scale = 0.9f}
#define FACE_LINE_FURIOUS {.eye_right = FACE_EYE_SQUINT, .eye_left = FACE_EYE_SQUINT, \
                           .mouth = FACE_MOUTH_WAVY, .brows = FACE_BROWS_BOTH, .brow_tilt = -1.0f, \
                           .mouth_scale = 1.0f}
#define FACE_LINE_SLEEPY {.eye_right = FACE_EYE_FLAT, .eye_left = FACE_EYE_FLAT, \
                          .mouth = FACE_MOUTH_DOT, .mouth_scale = 1.0f}
#define FACE_LINE_CURIOUS {.eye_right = FACE_EYE_FLAT, .eye_left = FACE_EYE_RING, \
                           .mouth = FACE_MOUTH_O, .brows = FACE_BROWS_LEFT, .brow_lift = 1.0f, \
                           .mouth_scale = 0.7f}
#define FACE_LINE_SURPRISED {.eye_right = FACE_EYE_RING, .eye_left = FACE_EYE_RING, \
                             .mouth = FACE_MOUTH_O, .brows = FACE_BROWS_BOTH, .brow_lift = 1.0f, \
                             .mouth_scale = 1.0f}

/* ---- Glyph eyes: each eye becomes a vector symbol for a while ------------------------- */
#define FACE_GLYPH_SIZE 92              /* px, glyph box at scale 1 */
#define FACE_GLYPH_POP_SQUASH 0.35f     /* squash/stretch while a glyph pops in or out */
#define FACE_GLYPH_SWAP_BELOW 0.08f     /* swap glyphs once the old one has shrunk to this */
#define FACE_COLOR_HEART 0xFF4F7B
#define FACE_COLOR_STAR 0xFFD43B
#define FACE_GLYPH_STROKE 11            /* px, x and spiral line width */
#define FACE_CLOSED_STROKE 8            /* px, closed-eye arc width */
#define FACE_SPIRAL_DEG_PER_S 540.0f
#define FACE_HEARTS_MS 2600             /* hearts anim: heart eyes (+ blush) */
#define FACE_STARS_MS 2200
#define FACE_DIZZY_MS 2000              /* dizzy anim: rotating spirals */
#define FACE_SLEEP_CLOSED_INTENSITY 95  /* sleepy at or above this shows closed eyes */

/* ---- Accessories: overlays around the eyes ------------------------------------------- */
#define FACE_COLOR_BLUSH 0xFF6F91
#define FACE_COLOR_WATER 0x7CC8FF       /* tear, sweat */
#define FACE_COLOR_MARK 0xFFC233        /* question and exclamation marks */
#define FACE_COLOR_NOTE 0xB79CFF        /* music notes */
#define FACE_COLOR_ZZZ 0xA9C4FF
#define FACE_BLUSH_MS 3000
#define FACE_BLUSH_OPA 170              /* 0-255 at the center (three soft layers) */
#define FACE_BLUSH_W 46
#define FACE_BLUSH_H 16
#define FACE_BLUSH_DY 14                /* px below the eye */
#define FACE_TEAR_MS 1600
#define FACE_TEAR_SIZE 9                /* drop radius */
#define FACE_TEAR_FALL_PX 72
#define FACE_SWEAT_MS 1600
#define FACE_SWEAT_SIZE 8
#define FACE_SWEAT_SLIDE_PX 24
#define FACE_ZZZ_MS 2600
#define FACE_ZZZ_SIZE 14                /* first Z; later ones grow */
#define FACE_ZZZ_RISE_PX 64
#define FACE_QUESTION_MS 2200
#define FACE_EXCLAIM_MS 1300
#define FACE_MARK_SIZE 40               /* height of ? and ! */
#define FACE_MUSIC_MS 2600
#define FACE_MUSIC_RISE_PX 54

#endif /* PETBOT_FACE_STYLE_H */
