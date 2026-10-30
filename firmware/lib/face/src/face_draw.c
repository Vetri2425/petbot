/* PetBot face: drawing. Eyes, glyph eyes and accessories, from LVGL primitives only. */
#include <math.h>

#include "face_internal.h"
#include "face_style.h"

#define PI_F 3.14159265f

/* ---- primitives ---- */

static int32_t px(float v)
{
    return (int32_t)lroundf(v);
}

static lv_opa_t opa_of(float f)
{
    f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    return (lv_opa_t)lroundf(f * 255.0f);
}

static void fill_rect(lv_layer_t * layer, float x1, float y1, float x2, float y2, int32_t radius,
                      uint32_t color, lv_opa_t opa)
{
    if(opa == 0 || x2 <= x1 || y2 <= y1) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color);
    d.bg_opa = opa;
    d.radius = radius;
    const lv_area_t a = {px(x1), px(y1), px(x2), px(y2)};
    lv_draw_rect(layer, &d, &a);
}

static void fill_ellipse(lv_layer_t * layer, float cx, float cy, float rx, float ry, uint32_t color,
                         lv_opa_t opa)
{
    fill_rect(layer, cx - rx, cy - ry, cx + rx, cy + ry, LV_RADIUS_CIRCLE, color, opa);
}

static void fill_triangle(lv_layer_t * layer, float ax, float ay, float bx, float by, float cx,
                          float cy, uint32_t color, lv_opa_t opa)
{
    if(opa == 0) return;
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = lv_color_hex(color);
    d.opa = opa;
    d.p[0].x = px(ax);
    d.p[0].y = px(ay);
    d.p[1].x = px(bx);
    d.p[1].y = px(by);
    d.p[2].x = px(cx);
    d.p[2].y = px(cy);
    lv_draw_triangle(layer, &d);
}

static void stroke_line(lv_layer_t * layer, float x1, float y1, float x2, float y2, float width,
                        uint32_t color, lv_opa_t opa)
{
    if(opa == 0 || width < 1.0f) return;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = px(x1);
    d.p1.y = px(y1);
    d.p2.x = px(x2);
    d.p2.y = px(y2);
    d.width = px(width);
    d.color = lv_color_hex(color);
    d.opa = opa;
    d.round_start = 1;
    d.round_end = 1;
    lv_draw_line(layer, &d);
}

static void stroke_arc(lv_layer_t * layer, float cx, float cy, float radius, float start_deg,
                       float end_deg, float width, uint32_t color, lv_opa_t opa)
{
    if(opa == 0 || radius < 1.0f || width < 1.0f) return;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = px(cx);
    d.center.y = px(cy);
    d.radius = (uint16_t)px(radius);
    d.width = px(width);
    d.start_angle = px(start_deg);
    d.end_angle = px(end_deg);
    d.color = lv_color_hex(color);
    d.opa = opa;
    d.rounded = 1;
    lv_draw_arc(layer, &d);
}

/* Fade in over `in`, out over `out` (fractions of the accessory's life). */
static float envelope(float p, float in, float out)
{
    if(p < in) return p / in;
    if(p > 1.0f - out) return (1.0f - p) / out;
    return 1.0f;
}

/* Overshooting pop-in, 0 -> ~1.1 -> 1 over the first `span` of p. */
static float pop_in(float p, float span)
{
    float t = p / span;
    if(t >= 1.0f) return 1.0f;
    const float c = 1.70158f, u = t - 1.0f;
    return 1.0f + (c + 1.0f) * u * u * u + c * u * u;
}

/* A water drop: round bottom at (x, y) with a pointed top. */
static void drop(lv_layer_t * layer, float x, float y, float r, uint32_t color, lv_opa_t opa)
{
    fill_triangle(layer, x - r * 0.92f, y - r * 0.3f, x + r * 0.92f, y - r * 0.3f, x, y - r * 2.4f,
                  color, opa);
    fill_ellipse(layer, x, y, r, r, color, opa);
}

/* ---- eyes ---- */

static void draw_eye(lv_layer_t * layer, int32_t ox, int32_t oy, const eye_geom_t * g, int side,
                     float sw, float sh)
{
    const float w = g->w * sw, h = g->h * sh;
    if(w < 1.0f || h < 1.0f) return;
    const float x1 = ox + g->cx - w / 2.0f, x2 = ox + g->cx + w / 2.0f;
    const float y1 = oy + g->cy - h / 2.0f, y2 = oy + g->cy + h / 2.0f;
    const float radius = g->radius * (sw < sh ? sw : sh);
    fill_rect(layer, x1, y1, x2, y2, px(radius), FACE_COLOR_EYE, LV_OPA_COVER);

    /* Lids are black shapes over the eye, 2 px wider so no edge peeks out. */
    const float lx1 = x1 - 2.0f, lx2 = x2 + 2.0f;
    const float lid_y = g->lid_top > 0.0f ? y1 + h * g->lid_top : y1 - 1.0f;
    if(g->lid_top > 0.0f) fill_rect(layer, lx1, y1 - 2.0f, lx2, lid_y, 0, FACE_COLOR_BG, LV_OPA_COVER);
    const float inner_x = side ? lx1 : lx2; /* inner corner faces the other eye */
    const float outer_x = side ? lx2 : lx1;
    if(g->slant_in > 0.0f) {
        fill_triangle(layer, inner_x, lid_y, outer_x, lid_y, inner_x, lid_y + h * g->slant_in,
                      FACE_COLOR_BG, LV_OPA_COVER);
    }
    if(g->slant_out > 0.0f) {
        fill_triangle(layer, inner_x, lid_y, outer_x, lid_y, outer_x, lid_y + h * g->slant_out,
                      FACE_COLOR_BG, LV_OPA_COVER);
    }
    if(g->cheek > 0.0f) {
        const float cx = ox + g->cx, top = y2 - h * g->cheek;
        fill_rect(layer, cx - w * 0.75f, top, cx + w * 0.75f, top + h * 1.5f, LV_RADIUS_CIRCLE,
                  FACE_COLOR_BG, LV_OPA_COVER);
    }
}

/* ---- glyph eyes (unit coordinates: -0.5..0.5 of the glyph box) ---- */

static void draw_heart(lv_layer_t * layer, float cx, float cy, float sx, float sy)
{
    const uint32_t c = FACE_COLOR_HEART;
    fill_ellipse(layer, cx - 0.22f * sx, cy - 0.13f * sy, 0.26f * sx, 0.26f * sy, c, LV_OPA_COVER);
    fill_ellipse(layer, cx + 0.22f * sx, cy - 0.13f * sy, 0.26f * sx, 0.26f * sy, c, LV_OPA_COVER);
    fill_triangle(layer, cx - 0.47f * sx, cy - 0.04f * sy, cx + 0.47f * sx, cy - 0.04f * sy, cx,
                  cy + 0.44f * sy, c, LV_OPA_COVER);
}

static void draw_star(lv_layer_t * layer, float cx, float cy, float sx, float sy)
{
    float ox[5], oy[5], ix[5], iy[5];
    for(int i = 0; i < 5; ++i) {
        const float a_out = (-90.0f + 72.0f * i) * PI_F / 180.0f;
        const float a_in = a_out + 36.0f * PI_F / 180.0f;
        ox[i] = cx + cosf(a_out) * 0.5f * sx;
        oy[i] = cy + sinf(a_out) * 0.5f * sy;
        ix[i] = cx + cosf(a_in) * 0.22f * sx;
        iy[i] = cy + sinf(a_in) * 0.22f * sy;
    }
    for(int i = 0; i < 5; ++i) {
        const int prev = (i + 4) % 5;
        fill_triangle(layer, cx, cy, ix[prev], iy[prev], ox[i], oy[i], FACE_COLOR_STAR, LV_OPA_COVER);
        fill_triangle(layer, cx, cy, ox[i], oy[i], ix[i], iy[i], FACE_COLOR_STAR, LV_OPA_COVER);
    }
}

/* Spiral from half-circle arcs whose centers alternate, so the radius grows each half turn. */
static void draw_spiral(lv_layer_t * layer, float cx, float cy, float sx, float sy, float rot_deg)
{
    const float s = sqrtf(sx * sy);
    const float d = 0.19f * s; /* radius grows d/2 per half turn */
    const float ux = cosf(rot_deg * PI_F / 180.0f), uy = sinf(rot_deg * PI_F / 180.0f);
    const float stroke = FACE_GLYPH_STROKE * s / FACE_GLYPH_SIZE;
    for(int k = 0; k < 5; ++k) {
        const float shift = (k % 2) ? d / 2.0f : 0.0f;
        const float start = rot_deg + 180.0f * k;
        stroke_arc(layer, cx + ux * shift - ux * d / 4.0f, cy + uy * shift - uy * d / 4.0f,
                   (k + 1) * d / 2.0f, start, start + 180.0f, stroke, FACE_COLOR_EYE, LV_OPA_COVER);
    }
}

static void draw_x(lv_layer_t * layer, float cx, float cy, float sx, float sy)
{
    const float stroke = FACE_GLYPH_STROKE * sqrtf(sx * sy) / FACE_GLYPH_SIZE;
    stroke_line(layer, cx - 0.3f * sx, cy - 0.3f * sy, cx + 0.3f * sx, cy + 0.3f * sy, stroke,
                FACE_COLOR_EYE, LV_OPA_COVER);
    stroke_line(layer, cx + 0.3f * sx, cy - 0.3f * sy, cx - 0.3f * sx, cy + 0.3f * sy, stroke,
                FACE_COLOR_EYE, LV_OPA_COVER);
}

static void draw_closed(lv_layer_t * layer, float cx, float cy, float sx, float sy)
{
    const float s = sqrtf(sx * sy);
    stroke_arc(layer, cx, cy - 0.18f * sy, 0.36f * s, 25.0f, 155.0f,
               FACE_CLOSED_STROKE * s / FACE_GLYPH_SIZE, FACE_COLOR_EYE, LV_OPA_COVER);
}

static void draw_glyph(lv_layer_t * layer, float cx, float cy, const face_render_t * r)
{
    const float sx = FACE_GLYPH_SIZE * r->glyph_w, sy = FACE_GLYPH_SIZE * r->glyph_h;
    if(sx < 1.0f || sy < 1.0f) return;
    switch(r->glyph) {
        case FACE_GLYPH_HEART: draw_heart(layer, cx, cy, sx, sy); break;
        case FACE_GLYPH_STAR: draw_star(layer, cx, cy, sx, sy); break;
        case FACE_GLYPH_SPIRAL: draw_spiral(layer, cx, cy, sx, sy, r->spiral_deg); break;
        case FACE_GLYPH_X: draw_x(layer, cx, cy, sx, sy); break;
        case FACE_GLYPH_CLOSED: draw_closed(layer, cx, cy, sx, sy); break;
        default: break;
    }
}

/* ---- accessories (positions relative to the eyes, in face coordinates) ---- */

typedef struct {
    float x, y;
} pt_t;

static pt_t tear_pos(const face_render_t * r, float p)
{
    const eye_geom_t * g = &r->eyes[0];
    const float fall = p < 0.2f ? 0.0f : (p - 0.2f) / 0.8f;
    return (pt_t){g->cx + g->w * 0.22f, g->cy + g->h / 2.0f - 2.0f + FACE_TEAR_FALL_PX * fall * fall};
}

static pt_t sweat_pos(const face_render_t * r, float p)
{
    const eye_geom_t * g = &r->eyes[1];
    return (pt_t){g->cx + g->w / 2.0f + 9.0f, g->cy - g->h / 2.0f + 8.0f + FACE_SWEAT_SLIDE_PX * p};
}

static pt_t mark_pos(const face_render_t * r)
{
    const eye_geom_t * g = &r->eyes[1];
    return (pt_t){g->cx + g->w / 2.0f + 26.0f, g->cy - g->h / 2.0f - 2.0f};
}

static pt_t zzz_base(const face_render_t * r)
{
    const eye_geom_t * g = &r->eyes[1];
    return (pt_t){g->cx + g->w / 2.0f + 2.0f, g->cy - g->h / 2.0f - 6.0f};
}

static pt_t music_base(const face_render_t * r)
{
    const eye_geom_t * g = &r->eyes[0];
    return (pt_t){g->cx - g->w / 2.0f - 16.0f, g->cy - g->h / 2.0f + 18.0f};
}

static void draw_blush(lv_layer_t * layer, int32_t ox, int32_t oy, const face_render_t * r, float p)
{
    const float f = envelope(p, 0.15f, 0.25f);
    for(int side = 0; side < 2; ++side) {
        const eye_geom_t * g = &r->eyes[side];
        const float cx = ox + g->cx, cy = oy + g->cy + g->h / 2.0f + FACE_BLUSH_DY + FACE_BLUSH_H / 2.0f;
        /* Soft edge: three stacked ellipses, faint and wide to strong and small. */
        for(int k = 0; k < 3; ++k) {
            const float grow = 1.0f + 0.22f * (2 - k);
            fill_ellipse(layer, cx, cy, FACE_BLUSH_W / 2.0f * grow, FACE_BLUSH_H / 2.0f * grow,
                         FACE_COLOR_BLUSH, (lv_opa_t)(FACE_BLUSH_OPA * f * 0.4f));
        }
    }
}

static void draw_zzz(lv_layer_t * layer, int32_t ox, int32_t oy, const face_render_t * r, float p)
{
    const pt_t b = zzz_base(r);
    for(int i = 0; i < 3; ++i) {
        float q = (p - 0.18f * i) / 0.64f;
        if(q <= 0.0f) continue;
        q = q > 1.0f ? 1.0f : q;
        const float h = FACE_ZZZ_SIZE * (1.0f + 0.35f * i) * (0.7f + 0.5f * q) / 2.0f;
        const float x = ox + b.x + q * FACE_ZZZ_RISE_PX * 0.55f + i * 6.0f;
        const float y = oy + b.y - q * FACE_ZZZ_RISE_PX - i * 4.0f;
        const lv_opa_t opa = opa_of(envelope(q, 0.15f, 0.35f));
        const float wdt = h * 0.4f > 2.0f ? h * 0.4f : 2.0f;
        stroke_line(layer, x - h, y - h, x + h, y - h, wdt, FACE_COLOR_ZZZ, opa);
        stroke_line(layer, x + h, y - h, x - h, y + h, wdt, FACE_COLOR_ZZZ, opa);
        stroke_line(layer, x - h, y + h, x + h, y + h, wdt, FACE_COLOR_ZZZ, opa);
    }
}

static void draw_question(lv_layer_t * layer, int32_t ox, int32_t oy, const face_render_t * r,
                          float p)
{
    const pt_t m = mark_pos(r);
    const float sz = FACE_MARK_SIZE * pop_in(p, 0.2f);
    const float x = ox + m.x + 3.0f * sinf(p * 4.0f * PI_F), y = oy + m.y;
    const lv_opa_t opa = opa_of(envelope(p, 0.01f, 0.2f));
    const float wdt = sz * 0.13f;
    stroke_arc(layer, x, y - sz * 0.22f, sz * 0.2f, 180.0f, 90.0f, wdt, FACE_COLOR_MARK, opa);
    stroke_line(layer, x, y - sz * 0.02f, x, y + sz * 0.12f, wdt, FACE_COLOR_MARK, opa);
    fill_ellipse(layer, x, y + sz * 0.33f, sz * 0.08f, sz * 0.08f, FACE_COLOR_MARK, opa);
}

static void draw_exclaim(lv_layer_t * layer, int32_t ox, int32_t oy, const face_render_t * r,
                         float p)
{
    const pt_t m = mark_pos(r);
    const float sz = FACE_MARK_SIZE * pop_in(p, 0.2f);
    const float x = ox + m.x, y = oy + m.y;
    const lv_opa_t opa = opa_of(envelope(p, 0.01f, 0.25f));
    fill_rect(layer, x - sz * 0.08f, y - sz * 0.45f, x + sz * 0.08f, y + sz * 0.15f,
              LV_RADIUS_CIRCLE, FACE_COLOR_MARK, opa);
    fill_ellipse(layer, x, y + sz * 0.34f, sz * 0.09f, sz * 0.09f, FACE_COLOR_MARK, opa);
}

static void draw_music(lv_layer_t * layer, int32_t ox, int32_t oy, const face_render_t * r, float p)
{
    const pt_t b = music_base(r);
    for(int i = 0; i < 2; ++i) {
        float q = (p - 0.25f * i) / 0.75f;
        if(q <= 0.0f) continue;
        q = q > 1.0f ? 1.0f : q;
        const float x = ox + b.x - i * 20.0f + 7.0f * sinf(2.0f * PI_F * q * 1.2f);
        const float y = oy + b.y - q * FACE_MUSIC_RISE_PX - i * 6.0f;
        const lv_opa_t opa = opa_of(envelope(q, 0.15f, 0.35f));
        fill_ellipse(layer, x, y, 6.0f, 4.5f, FACE_COLOR_NOTE, opa);
        stroke_line(layer, x + 5.0f, y, x + 5.0f, y - 22.0f, 3.0f, FACE_COLOR_NOTE, opa);
        stroke_line(layer, x + 5.0f, y - 22.0f, x + 13.0f, y - 15.0f, 3.0f, FACE_COLOR_NOTE, opa);
    }
}

void face_draw_frame(lv_layer_t * layer, int32_t ox, int32_t oy, const face_render_t * r)
{
    for(int side = 0; side < 2; ++side) {
        const eye_geom_t * g = &r->eyes[side];
        if(r->glyph == FACE_GLYPH_NONE) {
            draw_eye(layer, ox, oy, g, side, r->glyph_w, r->glyph_h);
        }
        else {
            draw_glyph(layer, ox + g->cx, oy + g->cy, r);
        }
    }
    const float * a = r->acc;
    if(a[FACE_ACC_BLUSH] >= 0.0f) draw_blush(layer, ox, oy, r, a[FACE_ACC_BLUSH]);
    if(a[FACE_ACC_TEAR] >= 0.0f) {
        const float p = a[FACE_ACC_TEAR];
        const pt_t t = tear_pos(r, p);
        const float grow = p < 0.2f ? p / 0.2f : 1.0f;
        drop(layer, ox + t.x, oy + t.y, FACE_TEAR_SIZE * grow, FACE_COLOR_WATER,
             opa_of(envelope(p, 0.01f, 0.25f)));
    }
    if(a[FACE_ACC_SWEAT] >= 0.0f) {
        const float p = a[FACE_ACC_SWEAT];
        const pt_t t = sweat_pos(r, p);
        drop(layer, ox + t.x, oy + t.y, FACE_SWEAT_SIZE, FACE_COLOR_WATER,
             opa_of(envelope(p, 0.15f, 0.3f)));
    }
    if(a[FACE_ACC_ZZZ] >= 0.0f) draw_zzz(layer, ox, oy, r, a[FACE_ACC_ZZZ]);
    if(a[FACE_ACC_QUESTION] >= 0.0f) draw_question(layer, ox, oy, r, a[FACE_ACC_QUESTION]);
    if(a[FACE_ACC_EXCLAIM] >= 0.0f) draw_exclaim(layer, ox, oy, r, a[FACE_ACC_EXCLAIM]);
    if(a[FACE_ACC_MUSIC] >= 0.0f) draw_music(layer, ox, oy, r, a[FACE_ACC_MUSIC]);
}

/* ---- dirty areas: generous boxes around everything drawn above ---- */

static lv_area_t box(float x1, float y1, float x2, float y2)
{
    lv_area_t a = {(int32_t)floorf(x1), (int32_t)floorf(y1), (int32_t)ceilf(x2), (int32_t)ceilf(y2)};
    return a;
}

int face_render_boxes(const face_render_t * r, lv_area_t * out, int max)
{
    int n = 0;
#define ADD(a)                \
    do {                      \
        if(n < max) out[n++] = (a); \
    } while(0)
    for(int side = 0; side < 2; ++side) {
        const eye_geom_t * g = &r->eyes[side];
        const float sw = r->glyph_w > 1.0f ? r->glyph_w : 1.0f;
        const float sh = r->glyph_h > 1.0f ? r->glyph_h : 1.0f;
        const float glyph = FACE_GLYPH_SIZE * 0.62f * (sw > sh ? sw : sh); /* spiral is widest */
        const float hw = fmaxf(g->w * sw / 2.0f + 3.0f, glyph);
        const float hh = fmaxf(g->h * sh / 2.0f + 3.0f, glyph);
        ADD(box(g->cx - hw, g->cy - hh, g->cx + hw, g->cy + hh));
    }
    const float * a = r->acc;
    if(a[FACE_ACC_BLUSH] >= 0.0f) {
        for(int side = 0; side < 2; ++side) {
            const eye_geom_t * g = &r->eyes[side];
            const float cy = g->cy + g->h / 2.0f + FACE_BLUSH_DY + FACE_BLUSH_H / 2.0f;
            ADD(box(g->cx - FACE_BLUSH_W, cy - FACE_BLUSH_H, g->cx + FACE_BLUSH_W, cy + FACE_BLUSH_H));
        }
    }
    if(a[FACE_ACC_TEAR] >= 0.0f) {
        const pt_t t = tear_pos(r, a[FACE_ACC_TEAR]);
        const float s = FACE_TEAR_SIZE;
        ADD(box(t.x - s - 2, t.y - s * 2.6f, t.x + s + 2, t.y + s + 2));
    }
    if(a[FACE_ACC_SWEAT] >= 0.0f) {
        const pt_t t = sweat_pos(r, a[FACE_ACC_SWEAT]);
        const float s = FACE_SWEAT_SIZE;
        ADD(box(t.x - s - 2, t.y - s * 2.6f, t.x + s + 2, t.y + s + 2));
    }
    if(a[FACE_ACC_ZZZ] >= 0.0f) {
        const pt_t b = zzz_base(r);
        const float big = FACE_ZZZ_SIZE * 1.7f * 1.2f;
        ADD(box(b.x - big, b.y - FACE_ZZZ_RISE_PX - 8 - big, b.x + FACE_ZZZ_RISE_PX * 0.55f + 12 + big,
                b.y + big));
    }
    if(a[FACE_ACC_QUESTION] >= 0.0f || a[FACE_ACC_EXCLAIM] >= 0.0f) {
        const pt_t m = mark_pos(r);
        const float s = FACE_MARK_SIZE * 1.2f;
        ADD(box(m.x - s * 0.5f - 4, m.y - s * 0.6f, m.x + s * 0.5f + 4, m.y + s * 0.5f));
    }
    if(a[FACE_ACC_MUSIC] >= 0.0f) {
        const pt_t b = music_base(r);
        ADD(box(b.x - 20 - 16, b.y - FACE_MUSIC_RISE_PX - 6 - 30, b.x + 24, b.y + 10));
    }
#undef ADD
    return n;
}
