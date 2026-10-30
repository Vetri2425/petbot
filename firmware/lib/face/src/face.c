/* PetBot face. See face.h; all tunables are in face_style.h. */
#include "face.h"

#include <math.h>
#include <string.h>

#include "face_style.h"

#define PI_F 3.14159265f
#define EXPR_FIELDS (sizeof(face_expr_t) / sizeof(float))

typedef struct {
    const char * name;
    face_expr_t expr;
} mood_def_t;

static const mood_def_t k_moods[] = {
    {"neutral", FACE_EXPR_NEUTRAL}, {"happy", FACE_EXPR_HAPPY},
    {"sad", FACE_EXPR_SAD},         {"angry", FACE_EXPR_ANGRY},
    {"sleepy", FACE_EXPR_SLEEPY},   {"curious", FACE_EXPR_CURIOUS},
    {"surprised", FACE_EXPR_SURPRISED},
};
#define MOOD_COUNT (sizeof(k_moods) / sizeof(k_moods[0]))

typedef enum { ANIM_NONE, ANIM_LOOK_LEFT, ANIM_LOOK_RIGHT, ANIM_WAVE_LEFT, ANIM_WAVE_RIGHT } anim_t;

typedef struct {
    float from, to;
    uint32_t t, ms;
} tween_t;

typedef struct {
    float cx, cy, w, h, radius;
    float lid_top, slant_in, slant_out, cheek;
} eye_geom_t;

static struct {
    lv_obj_t * obj;
    bool idle;
    uint32_t rng;

    face_expr_t from, to, cur;
    uint32_t trans_t;
    float slow; /* >= 1, stretches idle timings (sleepy) */

    bool blinking;
    uint32_t blink_t;
    int32_t blink_wait;

    tween_t gx, gy;
    bool glance_away;
    int32_t glance_wait;

    float breath_phase;

    anim_t anim;
    uint32_t anim_t;

    eye_geom_t eyes[2]; /* 0 = left, 1 = right */
} s;

/* ---- small helpers ---- */

static uint32_t rand_u32(void)
{
    /* xorshift32: deterministic, no libc state */
    uint32_t x = s.rng;
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

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static float ease_in_out(float t)
{
    t = clamp01(t);
    if(t < 0.5f) return 4.0f * t * t * t;
    const float u = -2.0f * t + 2.0f;
    return 1.0f - u * u * u / 2.0f;
}

static float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static void tween_start(tween_t * tw, float to, uint32_t ms)
{
    const float now = tw->ms ? lerp(tw->from, tw->to, ease_in_out((float)tw->t / tw->ms)) : tw->to;
    tw->from = now;
    tw->to = to;
    tw->t = 0;
    tw->ms = ms ? ms : 1;
}

static float tween_step(tween_t * tw, uint32_t dt)
{
    tw->t = tw->t + dt > tw->ms ? tw->ms : tw->t + dt;
    return lerp(tw->from, tw->to, ease_in_out((float)tw->t / tw->ms));
}

static uint32_t scaled(uint32_t ms)
{
    return (uint32_t)(ms * s.slow);
}

/* Look/wave envelope: ramps 0 -> 1 -> 0 with eased edges. */
static float anim_envelope(uint32_t t, uint32_t total)
{
    if(t < FACE_ANIM_RAMP_MS) return ease_in_out((float)t / FACE_ANIM_RAMP_MS);
    if(t + FACE_ANIM_RAMP_MS > total) return ease_in_out((float)(total - t) / FACE_ANIM_RAMP_MS);
    return 1.0f;
}

/* ---- idle life ---- */

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

/* 0 = open, 1 = closed */
static float blink_amount(uint32_t dt)
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

static void next_glance(void)
{
    const uint32_t move = scaled(FACE_GLANCE_MOVE_MS);
    if(!s.glance_away) {
        tween_start(&s.gx, (float)rand_range(-FACE_GLANCE_MAX_X, FACE_GLANCE_MAX_X), move);
        tween_start(&s.gy, (float)rand_range(-FACE_GLANCE_MAX_Y, FACE_GLANCE_MAX_Y), move);
        s.glance_wait = (int32_t)scaled(
            (uint32_t)rand_range(FACE_GLANCE_HOLD_MIN_MS, FACE_GLANCE_HOLD_MAX_MS));
    }
    else {
        tween_start(&s.gx, 0.0f, move);
        tween_start(&s.gy, 0.0f, move);
        s.glance_wait = (int32_t)scaled(
            (uint32_t)rand_range(FACE_GLANCE_INTERVAL_MIN_MS, FACE_GLANCE_INTERVAL_MAX_MS));
    }
    s.glance_away = !s.glance_away;
}

/* ---- geometry and drawing ---- */

static void compute_eye(int side, float gx, float gy, float breath, float blink)
{
    const face_expr_t * e = &s.cur;
    const float dir = side ? 1.0f : -1.0f;
    const float grow = 1.0f + (side ? e->grow_right : e->grow_left);
    eye_geom_t * g = &s.eyes[side];

    g->w = FACE_EYE_W * (1.0f + e->grow_w) * grow;
    g->h = FACE_EYE_H * (1.0f + e->grow_h) * grow * (1.0f + FACE_BREATH_SCALE * breath);
    g->cx = FACE_SCREEN_W / 2.0f + dir * FACE_EYE_SPACING / 2.0f + gx;
    g->cy = FACE_SCREEN_H / 2.0f + FACE_EYE_Y + e->dy + dir * e->tilt + gy +
            FACE_BREATH_DY * breath;

    const float max_r = (g->w < g->h ? g->w : g->h) / 2.0f;
    g->radius = lerp(FACE_EYE_RADIUS < max_r ? FACE_EYE_RADIUS : max_r, max_r, clamp01(e->round));

    const float blink_lid = blink * FACE_BLINK_CLOSED;
    g->lid_top = clamp01(e->lid_top > blink_lid ? e->lid_top : blink_lid);
    g->slant_in = clamp01(e->slant_in) * (1.0f - g->lid_top); /* lids flatten as they close */
    g->slant_out = clamp01(e->slant_out) * (1.0f - g->lid_top);
    g->cheek = clamp01(e->cheek);
}

static void draw_black_rect(lv_layer_t * layer, float x1, float y1, float x2, float y2,
                            int32_t radius)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(FACE_COLOR_BG);
    d.bg_opa = LV_OPA_COVER;
    d.radius = radius;
    const lv_area_t a = {(int32_t)lroundf(x1), (int32_t)lroundf(y1), (int32_t)lroundf(x2),
                         (int32_t)lroundf(y2)};
    lv_draw_rect(layer, &d, &a);
}

static void draw_black_triangle(lv_layer_t * layer, float ax, float ay, float bx, float by,
                                float cx, float cy)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = lv_color_hex(FACE_COLOR_BG);
    d.opa = LV_OPA_COVER;
    d.p[0].x = (int32_t)lroundf(ax);
    d.p[0].y = (int32_t)lroundf(ay);
    d.p[1].x = (int32_t)lroundf(bx);
    d.p[1].y = (int32_t)lroundf(by);
    d.p[2].x = (int32_t)lroundf(cx);
    d.p[2].y = (int32_t)lroundf(cy);
    lv_draw_triangle(layer, &d);
}

static void draw_eye(lv_layer_t * layer, int32_t ox, int32_t oy, const eye_geom_t * g, int side)
{
    const float x1 = ox + g->cx - g->w / 2.0f;
    const float x2 = ox + g->cx + g->w / 2.0f;
    const float y1 = oy + g->cy - g->h / 2.0f;
    const float y2 = oy + g->cy + g->h / 2.0f;

    lv_draw_rect_dsc_t eye;
    lv_draw_rect_dsc_init(&eye);
    eye.bg_color = lv_color_hex(FACE_COLOR_EYE);
    eye.bg_opa = LV_OPA_COVER;
    eye.radius = (int32_t)lroundf(g->radius);
    const lv_area_t a = {(int32_t)lroundf(x1), (int32_t)lroundf(y1), (int32_t)lroundf(x2),
                         (int32_t)lroundf(y2)};
    lv_draw_rect(layer, &eye, &a);

    /* Lids are black shapes drawn over the eye, 2 px wider so no edge peeks out. */
    const float lx1 = x1 - 2.0f, lx2 = x2 + 2.0f;
    const float lid_y = g->lid_top > 0.0f ? y1 + g->h * g->lid_top : y1 - 1.0f;
    if(g->lid_top > 0.0f) draw_black_rect(layer, lx1, y1 - 2.0f, lx2, lid_y, 0);

    const float inner_x = side ? lx1 : lx2; /* inner corner faces the other eye */
    const float outer_x = side ? lx2 : lx1;
    if(g->slant_in > 0.0f) {
        draw_black_triangle(layer, inner_x, lid_y, outer_x, lid_y, inner_x,
                            lid_y + g->h * g->slant_in);
    }
    if(g->slant_out > 0.0f) {
        draw_black_triangle(layer, inner_x, lid_y, outer_x, lid_y, outer_x,
                            lid_y + g->h * g->slant_out);
    }

    if(g->cheek > 0.0f) {
        const float cx = ox + g->cx;
        const float top = y2 - g->h * g->cheek;
        draw_black_rect(layer, cx - g->w * 0.75f, top, cx + g->w * 0.75f, top + g->h * 1.5f,
                        LV_RADIUS_CIRCLE);
    }
}

static void draw_cb(lv_event_t * e)
{
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(s.obj, &c);
    for(int side = 0; side < 2; ++side) draw_eye(layer, c.x1, c.y1, &s.eyes[side], side);
}

/* ---- public API ---- */

lv_obj_t * face_init(lv_obj_t * parent)
{
    memset(&s, 0, sizeof(s));
    s.idle = true;
    s.rng = 0x5eed1234u;
    s.slow = 1.0f;
    s.glance_wait = FACE_GLANCE_INTERVAL_MIN_MS;
    schedule_blink();

    s.obj = lv_obj_create(parent);
    lv_obj_remove_style_all(s.obj);
    lv_obj_set_size(s.obj, FACE_SCREEN_W, FACE_SCREEN_H);
    lv_obj_center(s.obj);
    lv_obj_set_style_bg_color(s.obj, lv_color_hex(FACE_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s.obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s.obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s.obj, draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

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
    float * to = (float *)&s.to;
    s.from = s.cur;
    for(size_t i = 0; i < EXPR_FIELDS; ++i) to[i] = src[i] * k;
    s.trans_t = 0;

    const bool sleepy = strcmp(def->name, "sleepy") == 0;
    s.slow = 1.0f + (FACE_SLEEPY_SLOWDOWN - 1.0f) * (sleepy ? k : 0.0f);
    return true;
}

bool face_play_anim(const char * name)
{
    if(strcmp(name, "blink") == 0) {
        start_blink();
        return true;
    }
    static const struct {
        const char * name;
        anim_t anim;
    } k_anims[] = {
        {"look_left", ANIM_LOOK_LEFT},
        {"look_right", ANIM_LOOK_RIGHT},
        {"wave_left", ANIM_WAVE_LEFT},
        {"wave_right", ANIM_WAVE_RIGHT},
    };
    for(size_t i = 0; i < sizeof(k_anims) / sizeof(k_anims[0]); ++i) {
        if(strcmp(name, k_anims[i].name) == 0) {
            s.anim = k_anims[i].anim;
            s.anim_t = 0;
            return true;
        }
    }
    return false;
}

void face_set_idle(bool enabled)
{
    s.idle = enabled;
    if(!enabled) {
        tween_start(&s.gx, 0.0f, 1);
        tween_start(&s.gy, 0.0f, 1);
        s.glance_away = false;
        s.breath_phase = 0.0f;
    }
}

void face_tick(uint32_t dt)
{
    /* Mood transition */
    if(s.trans_t < FACE_MOOD_TRANSITION_MS) {
        s.trans_t = s.trans_t + dt > FACE_MOOD_TRANSITION_MS ? FACE_MOOD_TRANSITION_MS
                                                               : s.trans_t + dt;
        const float p = ease_in_out((float)s.trans_t / FACE_MOOD_TRANSITION_MS);
        const float * from = (const float *)&s.from;
        const float * to = (const float *)&s.to;
        float * cur = (float *)&s.cur;
        for(size_t i = 0; i < EXPR_FIELDS; ++i) cur[i] = lerp(from[i], to[i], p);
    }

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
    float gx = tween_step(&s.gx, dt);
    float gy = tween_step(&s.gy, dt);

    /* Animations override the gaze */
    if(s.anim != ANIM_NONE) {
        const bool look = s.anim == ANIM_LOOK_LEFT || s.anim == ANIM_LOOK_RIGHT;
        const uint32_t total = look ? FACE_LOOK_MS : FACE_WAVE_MS;
        s.anim_t += dt;
        if(s.anim_t >= total) {
            s.anim = ANIM_NONE;
        }
        else {
            const bool left = s.anim == ANIM_LOOK_LEFT || s.anim == ANIM_WAVE_LEFT;
            const float env = anim_envelope(s.anim_t, total);
            const float target = (left ? -1.0f : 1.0f) * (look ? FACE_LOOK_X : FACE_WAVE_X);
            gx = lerp(gx, target, env);
            gy = lerp(gy, 0.0f, env);
            if(!look) {
                const float p = (float)s.anim_t / total;
                gy -= FACE_WAVE_BOUNCE_PX * fabsf(sinf(PI_F * FACE_WAVE_BOUNCES * p));
            }
        }
    }

    compute_eye(0, gx, gy, breath, blink);
    compute_eye(1, gx, gy, breath, blink);
    if(s.obj) lv_obj_invalidate(s.obj);
}
