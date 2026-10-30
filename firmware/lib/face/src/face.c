/* PetBot face: motion. See face.h; drawing is in face_draw.c; tunables in face_style.h. */
#include "face.h"

#include <math.h>
#include <string.h>

#include "face_internal.h"
#include "face_style.h"

#define PI_F 3.14159265f
#define DIRTY_MARGIN 4 /* px around every drawn box */
#define EXPR_FIELDS (sizeof(face_expr_t) / sizeof(float))

typedef struct {
    const char * name;
    face_expr_t expr;
} mood_def_t;

typedef struct {
    const char * name;
    face_emoji_t mild;   /* below FACE_EMOJI_INTENSITY */
    face_emoji_t strong; /* at or above it (calm moods: same as mild) */
} emoji_mood_t;

static const emoji_mood_t k_emoji_moods[] = {
    {"neutral", FACE_EMOJI_CALM, FACE_EMOJI_CALM},
    {"happy", FACE_EMOJI_MILD_HAPPY, FACE_EMOJI_HAPPY},
    {"sad", FACE_EMOJI_MILD_SAD, FACE_EMOJI_SAD},
    {"angry", FACE_EMOJI_MILD_ANGRY, FACE_EMOJI_ANGRY},
    {"sleepy", FACE_EMOJI_SLEEPY, FACE_EMOJI_SLEEPY},
    {"curious", FACE_EMOJI_CURIOUS, FACE_EMOJI_CURIOUS},
    {"surprised", FACE_EMOJI_MILD_SURPRISED, FACE_EMOJI_SURPRISED},
};

static const mood_def_t k_moods[] = {
    {"neutral", FACE_EXPR_NEUTRAL}, {"happy", FACE_EXPR_HAPPY},
    {"sad", FACE_EXPR_SAD},         {"angry", FACE_EXPR_ANGRY},
    {"sleepy", FACE_EXPR_SLEEPY},   {"curious", FACE_EXPR_CURIOUS},
    {"surprised", FACE_EXPR_SURPRISED},
};
#define MOOD_COUNT (sizeof(k_moods) / sizeof(k_moods[0]))

static const char * const k_glyph_names[FACE_GLYPH_COUNT] = {
    "none", "heart", "star", "spiral", "x", "closed", "bulb",
};
static const char * const k_acc_names[FACE_ACC_COUNT] = {
    "blush", "tear", "sweat", "zzz", "question", "exclaim", "music",
};
static const uint32_t k_acc_ms[FACE_ACC_COUNT] = {
    FACE_BLUSH_MS, FACE_TEARS_MS, FACE_SWEAT_MS, FACE_ZZZ_MS,
    FACE_QUESTION_MS, FACE_EXCLAIM_MS, FACE_MUSIC_MS,
};

typedef enum { ANIM_NONE, ANIM_LOOK_LEFT, ANIM_LOOK_RIGHT, ANIM_WAVE_LEFT, ANIM_WAVE_RIGHT } anim_t;

/* Per-eye spring channels, driven toward the mood expression. */
enum { P_W, P_H, P_DY, P_ROUND, P_LID, P_SLANT_IN, P_SLANT_OUT, P_CHEEK, P_COUNT };

typedef struct {
    float x, v;
} spring_t;

static struct {
    lv_obj_t * obj;
    bool idle;
    uint32_t rng;
    face_style_t style;

    /* emoji style: shapes swap with the glyph pop; brow and mouth amounts are springs */
    face_emoji_t emoji_mood;  /* face for the current mood */
    face_emoji_t emoji_want;  /* emoji_mood, or a temporary face (scared) */
    face_emoji_t emoji_shown; /* lags emoji_want until the pop swaps it */
    uint32_t override_ms;     /* > 0 while a temporary face shows */
    bool tears_loop;          /* sad enough to cry */
    uint32_t tears_t;         /* ms, drives the drops down the streams */
    spring_t brow_tilt, brow_lift, mouth_scale;

    face_expr_t target; /* mood expression scaled by intensity */
    const char * mood;
    spring_t eye[2][P_COUNT];
    float slow; /* >= 1, stretches idle timings (sleepy) */
    bool sleep_closed;

    /* gaze: springs chase a destination; saccades add a short anticipation */
    spring_t gaze_x, gaze_y;
    float dest_x, dest_y;       /* where the eyes are heading */
    float aim_x, aim_y;         /* current spring target (anticipation, then dest) */
    int32_t anticipate_ms;      /* > 0 while anticipating */
    bool saccade_in_flight;

    spring_t squash;

    bool blinking;
    uint32_t blink_t;
    int32_t blink_wait;

    float glance_x, glance_y; /* idle glance destination */
    bool glance_away;
    int32_t glance_wait;

    float breath_phase;

    anim_t anim;
    uint32_t anim_t;
    int hop; /* wave hop index, to detect landings */

    face_glyph_t glyph_req;   /* asked for by face_show_glyph (NONE: no request) */
    face_glyph_t glyph_want;  /* request, else the default (closed when deeply sleepy) */
    face_glyph_t glyph_shown; /* lags glyph_want until the old glyph has shrunk away */
    uint32_t glyph_left_ms;
    bool glyph_timed;
    spring_t glyph_scale;
    float spiral_deg;

    bool acc_on[FACE_ACC_COUNT];
    uint32_t acc_t[FACE_ACC_COUNT];

    face_render_t render;
    face_render_t drawn;
    lv_area_t drawn_boxes[FACE_MAX_BOXES];
    int drawn_count;
    bool drawn_valid;
} s;

/* ---- helpers ---- */

static uint32_t rand_u32(void)
{
    uint32_t x = s.rng; /* xorshift32: deterministic, no libc state */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s.rng = x;
    return x;
}

static int32_t rand_range(int32_t lo, int32_t hi)
{
    return lo + (int32_t)(rand_u32() % (uint32_t)(hi - lo + 1));
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Clamp a 0..1 shape amount; springs never land exactly on 0, so tiny values count as 0. */
static float settled(float v)
{
    return v < 0.01f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static uint32_t scaled(uint32_t ms)
{
    return (uint32_t)(ms * s.slow);
}

static void spring_step(spring_t * sp, float target, float k, float c, float dt)
{
    /* semi-implicit Euler: stable for the stiffness/step combinations in face_style.h */
    sp->v += (k * (target - sp->x) - c * sp->v) * dt;
    sp->x += sp->v * dt;
}

/* ---- gaze and saccades ---- */

static void saccade_to(float x, float y)
{
    if(x == s.dest_x && y == s.dest_y) return;
    s.aim_x = s.gaze_x.x - FACE_SACCADE_ANTICIPATE * (x - s.gaze_x.x);
    s.aim_y = s.gaze_y.x - FACE_SACCADE_ANTICIPATE * (y - s.gaze_y.x);
    s.dest_x = x;
    s.dest_y = y;
    s.anticipate_ms = FACE_SACCADE_ANTICIPATE_MS;
    s.saccade_in_flight = true;
}

static void next_glance(void)
{
    if(!s.glance_away) {
        s.glance_x = (float)rand_range(-FACE_GLANCE_MAX_X, FACE_GLANCE_MAX_X);
        s.glance_y = (float)rand_range(-FACE_GLANCE_MAX_Y, FACE_GLANCE_MAX_Y);
        s.glance_wait = (int32_t)scaled(
            (uint32_t)rand_range(FACE_GLANCE_HOLD_MIN_MS, FACE_GLANCE_HOLD_MAX_MS));
    }
    else {
        s.glance_x = 0.0f;
        s.glance_y = 0.0f;
        s.glance_wait = (int32_t)scaled(
            (uint32_t)rand_range(FACE_GLANCE_INTERVAL_MIN_MS, FACE_GLANCE_INTERVAL_MAX_MS));
    }
    s.glance_away = !s.glance_away;
}

/* ---- blinks ---- */

static void schedule_blink(void)
{
    s.blink_wait = (int32_t)scaled(
        (uint32_t)rand_range(FACE_BLINK_INTERVAL_MIN_MS, FACE_BLINK_INTERVAL_MAX_MS));
}

static void start_blink(void)
{
    s.blinking = true;
    s.blink_t = 0;
}

static float blink_amount(uint32_t dt) /* 0 = open, 1 = closed */
{
    if(!s.blinking) return 0.0f;
    s.blink_t += dt;
    const uint32_t close = scaled(FACE_BLINK_CLOSE_MS);
    const uint32_t hold = scaled(FACE_BLINK_HOLD_MS);
    const uint32_t open = scaled(FACE_BLINK_OPEN_MS);
    const uint32_t t = s.blink_t;
    if(t < close) return (float)t / close;
    if(t < close + hold) return 1.0f;
    if(t < close + hold + open) return 1.0f - (float)(t - close - hold) / open;
    s.blinking = false;
    schedule_blink();
    return 0.0f;
}

/* ---- springs for the eye shape ---- */

static void eye_targets(int side, float out[P_COUNT])
{
    const face_expr_t * e = &s.target;
    const bool robot_left = side == 1; /* ADR 0014: the robot's left eye is on screen right */
    const float grow = 1.0f + (robot_left ? e->grow_left : e->grow_right);
    out[P_W] = FACE_EYE_W * (1.0f + e->grow_w) * grow;
    out[P_H] = FACE_EYE_H * (1.0f + e->grow_h) * grow;
    if(s.style == FACE_STYLE_EMOJI && s.emoji_shown.eyes != FACE_EYE_BLOB) {
        out[P_W] = FACE_EMOJI_EYE_W; /* line-art eyes use a fixed box */
        out[P_H] = FACE_EMOJI_EYE_H;
    }
    out[P_DY] = e->dy + (robot_left ? -e->tilt : e->tilt);
    out[P_ROUND] = e->round;
    out[P_LID] = e->lid_top;
    out[P_SLANT_IN] = e->slant_in;
    out[P_SLANT_OUT] = e->slant_out;
    out[P_CHEEK] = e->cheek;
}

static bool same_shapes(const face_emoji_t * a, const face_emoji_t * b)
{
    return a->eyes == b->eyes && a->mouth == b->mouth && a->brows == b->brows;
}

/* Shown content (glyph, and the emoji shapes in emoji style) differs from what's wanted. */
static bool content_pending(void)
{
    if(s.glyph_want != s.glyph_shown) return true;
    return s.style == FACE_STYLE_EMOJI && !same_shapes(&s.emoji_want, &s.emoji_shown);
}

static void step_springs(float dt)
{
    for(int side = 0; side < 2; ++side) {
        float target[P_COUNT];
        eye_targets(side, target);
        for(int p = 0; p < P_COUNT; ++p) {
            const bool lid = p >= P_LID;
            spring_step(&s.eye[side][p], target[p], lid ? FACE_SPRING_LID_K : FACE_SPRING_SHAPE_K,
                        lid ? FACE_SPRING_LID_C : FACE_SPRING_SHAPE_C, dt);
        }
    }
    spring_step(&s.gaze_x, s.aim_x, FACE_SPRING_GAZE_K, FACE_SPRING_GAZE_C, dt);
    spring_step(&s.gaze_y, s.aim_y, FACE_SPRING_GAZE_K, FACE_SPRING_GAZE_C, dt);
    spring_step(&s.squash, 0.0f, FACE_SPRING_SQUASH_K, FACE_SPRING_SQUASH_C, dt);
    spring_step(&s.glyph_scale, content_pending() ? 0.0f : 1.0f, FACE_SPRING_GLYPH_K,
                FACE_SPRING_GLYPH_C, dt);
    spring_step(&s.brow_tilt, s.emoji_want.brow_tilt, FACE_SPRING_SHAPE_K, FACE_SPRING_SHAPE_C, dt);
    spring_step(&s.brow_lift, s.emoji_want.brow_lift, FACE_SPRING_SHAPE_K, FACE_SPRING_SHAPE_C, dt);
    spring_step(&s.mouth_scale, s.emoji_want.mouth_scale, FACE_SPRING_SHAPE_K, FACE_SPRING_SHAPE_C,
                dt);
}

static void snap_springs(void)
{
    for(int side = 0; side < 2; ++side) {
        float target[P_COUNT];
        eye_targets(side, target);
        for(int p = 0; p < P_COUNT; ++p) s.eye[side][p] = (spring_t){target[p], 0.0f};
    }
}

/* ---- glyphs ---- */

/* Runs every spring sub-step, so a shrink -> swap -> pop completes within one long tick. */
static void update_glyph(uint32_t dt)
{
    if(s.glyph_timed) {
        if(s.glyph_left_ms > dt) {
            s.glyph_left_ms -= dt;
        }
        else {
            s.glyph_timed = false;
            s.glyph_req = FACE_GLYPH_NONE;
        }
    }
    if(s.glyph_req != FACE_GLYPH_NONE) {
        s.glyph_want = s.glyph_req;
    }
    else {
        s.glyph_want = s.sleep_closed ? FACE_GLYPH_CLOSED : FACE_GLYPH_NONE;
    }
    /* The old glyph shrinks away; once small, swap and pop the new one in. */
    if(content_pending() && s.glyph_scale.x <= FACE_GLYPH_SWAP_BELOW) {
        s.glyph_shown = s.glyph_want;
        s.emoji_shown = s.emoji_want;
    }
}

/* ---- frame geometry ---- */

/* The mouth a glyph shows with, in emoji style. */
static uint8_t glyph_mouth(face_glyph_t glyph, uint8_t mood_mouth)
{
    switch(glyph) {
        case FACE_GLYPH_HEART: return FACE_MOUTH_SMILE;
        case FACE_GLYPH_STAR: return FACE_MOUTH_OPEN_SMILE;
        case FACE_GLYPH_SPIRAL: return FACE_MOUTH_WOBBLY;
        case FACE_GLYPH_X: return FACE_MOUTH_FLAT;
        case FACE_GLYPH_CLOSED: return FACE_MOUTH_NONE;
        case FACE_GLYPH_BULB: return FACE_MOUTH_O;
        default: return mood_mouth;
    }
}

static void build_emoji_render(emoji_render_t * l, float hop_dy)
{
    const face_emoji_t * x = &s.emoji_shown;
    const bool glyph = s.glyph_shown != FACE_GLYPH_NONE;
    l->eyes = x->eyes;
    l->mouth = glyph_mouth(s.glyph_shown, x->mouth);
    l->brows = !glyph && x->brows;
    l->brow_tilt = s.brow_tilt.x;
    l->brow_lift = s.brow_lift.x;
    l->mouth_scale = fmaxf(0.0f, s.mouth_scale.x);
    const float dy = (s.eye[0][P_DY].x + s.eye[1][P_DY].x) / 2.0f;
    l->mouth_x = FACE_SCREEN_W / 2.0f + s.gaze_x.x * FACE_EMOJI_MOUTH_GAZE;
    l->mouth_y = FACE_SCREEN_H / 2.0f + FACE_EMOJI_MOUTH_Y + dy * 0.5f +
                 s.gaze_y.x * FACE_EMOJI_MOUTH_GAZE + hop_dy;
}

static void build_render(float blink, float breath, float hop_dy)
{
    face_render_t * r = &s.render;
    const bool emoji = s.style == FACE_STYLE_EMOJI;
    /* Line-art eyes: no lids, a blink flattens them. Filled (blob) eyes keep their lids. */
    const bool line = emoji && s.emoji_shown.eyes != FACE_EYE_BLOB;
    r->style = s.style;
    if(emoji) {
        build_emoji_render(&r->emoji, hop_dy);
    }
    else {
        memset(&r->emoji, 0, sizeof(r->emoji));
    }
    r->tears_phase = (float)(s.tears_t % FACE_TEARS_PERIOD_MS) / FACE_TEARS_PERIOD_MS;
    const float squash = clampf(s.squash.x, -FACE_SQUASH_MAX, FACE_SQUASH_MAX);
    for(int side = 0; side < 2; ++side) {
        const spring_t * e = s.eye[side];
        eye_geom_t * g = &r->eyes[side];
        const float dir = side ? 1.0f : -1.0f;
        const float spacing = FACE_EYE_SPACING;
        g->w = fmaxf(4.0f, e[P_W].x * (1.0f + squash) * (1.0f + FACE_BLINK_SQUASH * blink));
        g->h = fmaxf(4.0f, e[P_H].x * (1.0f - squash) * (1.0f + FACE_BREATH_SCALE * breath));
        if(line) g->h = fmaxf(4.0f, g->h * (1.0f - FACE_EMOJI_BLINK_FLATTEN * blink));
        g->cx = FACE_SCREEN_W / 2.0f + dir * spacing / 2.0f + s.gaze_x.x;
        g->cy = FACE_SCREEN_H / 2.0f + FACE_EYE_Y + e[P_DY].x +
                s.gaze_y.x + hop_dy + FACE_BREATH_DY * breath;
        const float max_r = (g->w < g->h ? g->w : g->h) / 2.0f;
        const float base_r = FACE_EYE_RADIUS < max_r ? FACE_EYE_RADIUS : max_r;
        g->radius = base_r + (max_r - base_r) * clampf(e[P_ROUND].x, 0.0f, 1.0f);
        const float blink_lid = line ? 0.0f : blink * FACE_BLINK_CLOSED; /* line: flattens */
        const float lid = line ? 0.0f : settled(e[P_LID].x);
        g->lid_top = lid > blink_lid ? lid : blink_lid;
        g->slant_in = line ? 0.0f : settled(e[P_SLANT_IN].x) * (1.0f - g->lid_top);
        g->slant_out = line ? 0.0f : settled(e[P_SLANT_OUT].x) * (1.0f - g->lid_top);
        g->cheek = line ? 0.0f : settled(e[P_CHEEK].x);
    }
    /* Pop: the scale overshoots past 1 and squashes on the way. */
    const float scale = fmaxf(0.0f, s.glyph_scale.x);
    const float pop = FACE_GLYPH_POP_SQUASH * (1.0f - scale);
    r->glyph = s.glyph_shown;
    r->glyph_w = scale * (1.0f + pop);
    r->glyph_h = scale * (1.0f - pop);
    r->spiral_deg = s.glyph_shown == FACE_GLYPH_SPIRAL ? s.spiral_deg : 0.0f;
    for(int i = 0; i < FACE_ACC_COUNT; ++i) {
        r->acc[i] = s.acc_on[i] ? (float)s.acc_t[i] / k_acc_ms[i] : -1.0f;
    }
}

static void invalidate_changed(void)
{
    if(s.obj == NULL) return;
    if(s.drawn_valid && memcmp(&s.render, &s.drawn, sizeof(s.render)) == 0) return;
    lv_area_t c;
    lv_obj_get_coords(s.obj, &c);
    lv_area_t boxes[FACE_MAX_BOXES];
    const int n = face_render_boxes(&s.render, boxes, FACE_MAX_BOXES);
    for(int i = 0; i < n; ++i) {
        boxes[i].x1 += c.x1 - DIRTY_MARGIN;
        boxes[i].y1 += c.y1 - DIRTY_MARGIN;
        boxes[i].x2 += c.x1 + DIRTY_MARGIN;
        boxes[i].y2 += c.y1 + DIRTY_MARGIN;
    }
    if(s.drawn_valid) {
        for(int i = 0; i < s.drawn_count; ++i) lv_obj_invalidate_area(s.obj, &s.drawn_boxes[i]);
    }
    for(int i = 0; i < n; ++i) lv_obj_invalidate_area(s.obj, &boxes[i]);
    memcpy(s.drawn_boxes, boxes, sizeof(boxes[0]) * (size_t)n);
    s.drawn_count = n;
    s.drawn = s.render;
    s.drawn_valid = true;
}

static void draw_cb(lv_event_t * e)
{
    lv_area_t c;
    lv_obj_get_coords(s.obj, &c);
    face_draw_frame(lv_event_get_layer(e), c.x1, c.y1, &s.render);
}

/* ---- public API ---- */

lv_obj_t * face_init(lv_obj_t * parent)
{
    memset(&s, 0, sizeof(s));
    s.idle = true;
    s.rng = 0x5eed1234u;
    s.style = FACE_DEFAULT_STYLE;
    const face_emoji_t calm = FACE_EMOJI_CALM;
    s.emoji_mood = s.emoji_want = s.emoji_shown = calm;
    s.mouth_scale.x = calm.mouth_scale;
    s.slow = 1.0f;
    s.mood = "neutral";
    s.glance_wait = FACE_GLANCE_INTERVAL_MIN_MS;
    s.glyph_scale.x = 1.0f;
    schedule_blink();
    snap_springs();

    s.obj = lv_obj_create(parent);
    lv_obj_remove_style_all(s.obj);
    lv_obj_set_size(s.obj, FACE_SCREEN_W, FACE_SCREEN_H);
    lv_obj_center(s.obj);
    lv_obj_set_style_bg_color(s.obj, lv_color_hex(FACE_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s.obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s.obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s.obj, draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_update_layout(s.obj);
    lv_obj_invalidate(s.obj);

    face_tick(0);
    return s.obj;
}

bool face_set_mood(const char * mood, uint8_t intensity)
{
    const mood_def_t * def = NULL;
    for(size_t i = 0; i < MOOD_COUNT; ++i) {
        if(strcmp(mood, k_moods[i].name) == 0) def = &k_moods[i];
    }
    if(def == NULL) return false;

    const float k = (intensity > 100 ? 100 : intensity) / 100.0f;
    const float * src = (const float *)&def->expr;
    float * to = (float *)&s.target;
    for(size_t i = 0; i < EXPR_FIELDS; ++i) to[i] = src[i] * k;

    for(size_t i = 0; i < sizeof(k_emoji_moods) / sizeof(k_emoji_moods[0]); ++i) {
        const emoji_mood_t * em = &k_emoji_moods[i];
        if(strcmp(em->name, def->name) != 0) continue;
        if(intensity >= FACE_EMOJI_INTENSITY) {
            s.emoji_mood = em->strong;
        }
        else {
            s.emoji_mood = em->mild;
            s.emoji_mood.mouth_scale *= 0.6f + 0.4f * k; /* mild mouths grow with intensity */
        }
    }
    if(s.override_ms == 0) s.emoji_want = s.emoji_mood;
    const bool was_crying = s.tears_loop;
    s.tears_loop = strcmp(def->name, "sad") == 0 && intensity >= FACE_TEARS_SAD_INTENSITY;
    if(was_crying && !s.tears_loop && s.acc_on[FACE_ACC_TEAR]) {
        /* stopped being sad: fade the tears out now instead of finishing the cycle */
        const uint32_t fade_from = FACE_TEARS_MS - FACE_TEARS_MS / 5;
        if(s.acc_t[FACE_ACC_TEAR] < fade_from) s.acc_t[FACE_ACC_TEAR] = fade_from;
    }
    if(s.tears_loop && !s.acc_on[FACE_ACC_TEAR]) face_show_accessory(FACE_ACC_TEAR);

    const bool surprised = strcmp(def->name, "surprised") == 0;
    if(surprised && strcmp(s.mood, "surprised") != 0) s.squash.v += FACE_STRETCH_SURPRISE_KICK;
    s.mood = def->name;
    const bool sleepy = strcmp(def->name, "sleepy") == 0;
    s.slow = 1.0f + (FACE_SLEEPY_SLOWDOWN - 1.0f) * (sleepy ? k : 0.0f);
    s.sleep_closed = sleepy && intensity >= FACE_SLEEP_CLOSED_INTENSITY;
    return true;
}

void face_set_style(face_style_t style)
{
    if(style != FACE_STYLE_FILLED && style != FACE_STYLE_EMOJI) return;
    s.style = style;
    s.emoji_shown = s.emoji_want;
    snap_springs(); /* each style has its own eye sizes */
    s.drawn_valid = false;
    if(s.obj) lv_obj_invalidate(s.obj);
}

face_style_t face_get_style(void)
{
    return s.style;
}

void face_show_glyph(face_glyph_t glyph, uint32_t ms)
{
    if(glyph >= FACE_GLYPH_COUNT) return;
    s.glyph_req = glyph;
    s.glyph_timed = ms > 0 && glyph != FACE_GLYPH_NONE;
    s.glyph_left_ms = ms;
}

void face_show_accessory(face_accessory_t accessory)
{
    if(accessory >= FACE_ACC_COUNT) return;
    s.acc_on[accessory] = true;
    s.acc_t[accessory] = 0;
    if(accessory == FACE_ACC_EXCLAIM) s.squash.v += FACE_STRETCH_EXCLAIM_KICK;
}

const char * face_glyph_name(face_glyph_t glyph)
{
    return glyph < FACE_GLYPH_COUNT ? k_glyph_names[glyph] : "?";
}

const char * face_accessory_name(face_accessory_t accessory)
{
    return accessory < FACE_ACC_COUNT ? k_acc_names[accessory] : "?";
}

bool face_play_anim(const char * name)
{
    static const struct {
        const char * name;
        anim_t anim;
    } k_moves[] = {
        {"look_left", ANIM_LOOK_LEFT},
        {"look_right", ANIM_LOOK_RIGHT},
        {"wave_left", ANIM_WAVE_LEFT},
        {"wave_right", ANIM_WAVE_RIGHT},
    };
    static const struct {
        const char * name;
        int glyph; /* face_glyph_t, or -1 */
        uint32_t glyph_ms;
        int acc; /* face_accessory_t, or -1 */
    } k_fx[] = {
        {"hearts", FACE_GLYPH_HEART, FACE_HEARTS_MS, FACE_ACC_BLUSH},
        {"stars", FACE_GLYPH_STAR, FACE_STARS_MS, -1},
        {"dizzy", FACE_GLYPH_SPIRAL, FACE_DIZZY_MS, -1},
        {"zzz", -1, 0, FACE_ACC_ZZZ},
        {"tear", -1, 0, FACE_ACC_TEAR},
        {"idea", FACE_GLYPH_BULB, FACE_IDEA_MS, -1},
        {"blush", -1, 0, FACE_ACC_BLUSH},
        {"sweat", -1, 0, FACE_ACC_SWEAT},
        {"question", -1, 0, FACE_ACC_QUESTION},
        {"exclaim", -1, 0, FACE_ACC_EXCLAIM},
        {"music", -1, 0, FACE_ACC_MUSIC},
    };
    if(strcmp(name, "blink") == 0) {
        start_blink();
        return true;
    }
    if(strcmp(name, "scared") == 0) { /* a temporary emoji face, then back to the mood */
        const face_emoji_t scared = FACE_EMOJI_SCARED;
        s.emoji_want = scared;
        s.override_ms = FACE_SCARED_MS;
        s.squash.v += FACE_SQUASH_LAND_KICK; /* flinch */
        return true;
    }
    for(size_t i = 0; i < sizeof(k_moves) / sizeof(k_moves[0]); ++i) {
        if(strcmp(name, k_moves[i].name) == 0) {
            s.anim = k_moves[i].anim;
            s.anim_t = 0;
            s.hop = 0;
            return true;
        }
    }
    for(size_t i = 0; i < sizeof(k_fx) / sizeof(k_fx[0]); ++i) {
        if(strcmp(name, k_fx[i].name) == 0) {
            if(k_fx[i].glyph >= 0) face_show_glyph((face_glyph_t)k_fx[i].glyph, k_fx[i].glyph_ms);
            if(k_fx[i].acc >= 0) face_show_accessory((face_accessory_t)k_fx[i].acc);
            return true;
        }
    }
    return false;
}

void face_set_idle(bool enabled)
{
    s.idle = enabled;
    if(!enabled) {
        s.glance_x = s.glance_y = 0.0f;
        s.glance_away = false;
        s.breath_phase = 0.0f;
    }
}

void face_tick(uint32_t dt)
{
    /* Idle life */
    if(s.idle) {
        if(!s.blinking) {
            s.blink_wait -= (int32_t)dt;
            if(s.blink_wait <= 0) start_blink();
        }
        s.glance_wait -= (int32_t)dt;
        if(s.glance_wait <= 0) next_glance();
        s.breath_phase += 2.0f * PI_F * dt / (FACE_BREATH_PERIOD_MS * s.slow);
        if(s.breath_phase > 2.0f * PI_F) s.breath_phase -= 2.0f * PI_F;
    }
    const float blink = blink_amount(dt);
    const float breath = s.idle ? sinf(s.breath_phase) : 0.0f;

    /* Where the eyes should look: an anim overrides the idle glance (robot frame, ADR 0014). */
    float want_x = s.glance_x, want_y = s.glance_y, hop_dy = 0.0f;
    if(s.anim != ANIM_NONE) {
        const bool look = s.anim == ANIM_LOOK_LEFT || s.anim == ANIM_LOOK_RIGHT;
        const uint32_t total = look ? FACE_LOOK_MS : FACE_WAVE_MS;
        s.anim_t += dt;
        if(s.anim_t >= total) {
            s.anim = ANIM_NONE;
        }
        else {
            const bool left = s.anim == ANIM_LOOK_LEFT || s.anim == ANIM_WAVE_LEFT;
            want_x = (left ? 1.0f : -1.0f) * (look ? FACE_LOOK_X : FACE_WAVE_X);
            want_y = 0.0f;
            if(!look) {
                const float p = (float)s.anim_t / total * FACE_WAVE_BOUNCES;
                hop_dy = -FACE_WAVE_BOUNCE_PX * fabsf(sinf(PI_F * p));
                if((int)p > s.hop) { /* a hop just landed */
                    s.hop = (int)p;
                    s.squash.v += FACE_SQUASH_LAND_KICK;
                }
            }
        }
    }
    saccade_to(want_x, want_y);

    /* Springs, in fixed sub-steps */
    for(uint32_t left = dt; left > 0;) {
        const uint32_t step = left < FACE_SPRING_STEP_MS ? left : FACE_SPRING_STEP_MS;
        if(s.anticipate_ms > 0) {
            s.anticipate_ms -= (int32_t)step;
            if(s.anticipate_ms <= 0) {
                s.aim_x = s.dest_x;
                s.aim_y = s.dest_y;
            }
        }
        update_glyph(step);
        step_springs(step / 1000.0f);
        left -= step;
    }
    if(s.saccade_in_flight && s.anticipate_ms <= 0 &&
       fabsf(s.gaze_x.x - s.dest_x) < FACE_SACCADE_LANDED_PX &&
       fabsf(s.gaze_y.x - s.dest_y) < FACE_SACCADE_LANDED_PX) {
        s.saccade_in_flight = false;
        s.squash.v += FACE_SQUASH_SACCADE_KICK;
    }

    s.spiral_deg = fmodf(s.spiral_deg + FACE_SPIRAL_DEG_PER_S * dt / 1000.0f, 360.0f);
    if(s.override_ms > 0) {
        s.override_ms = s.override_ms > dt ? s.override_ms - dt : 0;
        if(s.override_ms == 0) s.emoji_want = s.emoji_mood;
    }
    for(int i = 0; i < FACE_ACC_COUNT; ++i) {
        if(!s.acc_on[i]) continue;
        s.acc_t[i] += dt;
        /* Crying keeps the tears in the fully shown middle of their life (between the fade-in
         * and the fade-out) instead of ending them; the drops run on their own clock. */
        if(i == FACE_ACC_TEAR && s.tears_loop && s.acc_t[i] > k_acc_ms[i] * 3 / 5) {
            s.acc_t[i] -= k_acc_ms[i] * 9 / 20;
        }
        if(s.acc_t[i] >= k_acc_ms[i]) s.acc_on[i] = false;
    }
    s.tears_t = s.acc_on[FACE_ACC_TEAR] ? s.tears_t + dt : 0;

    build_render(blink, breath, hop_dy);
    invalidate_changed();
}
