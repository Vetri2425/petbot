/* PetBot face: every tunable timing and shape value lives here.
 * Directions are as seen on the screen (left = smaller x).
 * Only face.c (and tools that need the timings, like the simulator) include this file. */
#ifndef PETBOT_FACE_STYLE_H
#define PETBOT_FACE_STYLE_H

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

/* ---- Mood transitions -------------------------------------------------------------- */
#define FACE_MOOD_TRANSITION_MS 300 /* ease-in-out between expressions */

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
#define FACE_GLANCE_MOVE_MS 180          /* eye travel time */
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
#define FACE_WAVE_X 20             /* px glance toward the waving side */
#define FACE_WAVE_BOUNCES 2
#define FACE_WAVE_BOUNCE_PX 10
#define FACE_ANIM_RAMP_MS 160      /* ease in/out at the start and end of look/wave */

/* ---- Moods ------------------------------------------------------------------------- */
/* An expression is a set of deltas from neutral (all zero = neutral). The mood's
 * intensity (0-100) scales every field linearly. */
typedef struct {
    float grow_w;     /* eye width change, fraction (+0.2 = 20% wider) */
    float grow_h;     /* eye height change, fraction */
    float grow_left;  /* extra size change of the left eye only, fraction */
    float grow_right; /* extra size change of the right eye only, fraction */
    float lid_top;    /* flat top lid, 0-1 of eye height */
    float slant_in;   /* top lid lowered at the inner corners, 0-1 of eye height (angry) */
    float slant_out;  /* top lid lowered at the outer corners, 0-1 of eye height (sad) */
    float cheek;      /* bottom cut by an arc, 0-1 of eye height (happy: upward arcs) */
    float round;      /* 0 = FACE_EYE_RADIUS corners, 1 = fully round */
    float dy;         /* px, both eyes (positive = down) */
    float tilt;       /* px, left eye up and right eye down (head-tilt offset) */
} face_expr_t;

#define FACE_EXPR_NEUTRAL {.grow_w = 0.0f}
#define FACE_EXPR_HAPPY {.grow_w = 0.08f, .cheek = 0.55f, .round = 0.4f, .dy = -4.0f}
#define FACE_EXPR_SAD {.grow_h = -0.10f, .lid_top = 0.08f, .slant_out = 0.45f, .dy = 10.0f}
#define FACE_EXPR_ANGRY {.grow_h = -0.05f, .lid_top = 0.05f, .slant_in = 0.55f}
#define FACE_EXPR_SLEEPY {.lid_top = 0.55f, .dy = 8.0f}
#define FACE_EXPR_CURIOUS {.grow_left = 0.18f, .grow_right = -0.10f, .tilt = 10.0f}
#define FACE_EXPR_SURPRISED {.grow_w = 0.22f, .grow_h = 0.12f, .round = 1.0f, .dy = -6.0f}

#endif /* PETBOT_FACE_STYLE_H */
