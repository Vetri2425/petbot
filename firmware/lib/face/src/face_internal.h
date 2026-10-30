/* Shared between face.c (motion) and face_draw.c (drawing). Not part of the public API. */
#ifndef PETBOT_FACE_INTERNAL_H
#define PETBOT_FACE_INTERNAL_H

#include "face.h"
#include "face_style.h"

/* One eye, in face-object coordinates (0,0 = top-left of the face). */
typedef struct {
    float cx, cy, w, h, radius;
    float lid_top, slant_in, slant_out, cheek;
} eye_geom_t;

/* Line-style parts, by screen position (0 = screen left = the robot's right). */
typedef struct {
    uint8_t eye[2];     /* face_line_eye_t */
    uint8_t mouth;      /* face_mouth_t */
    uint8_t brow_on[2];
    float brow_tilt;    /* +1 worried, -1 angry */
    float brow_lift;    /* 0..1 */
    float mouth_scale;
    float mouth_x, mouth_y;
} line_render_t;

/* Everything the draw code needs for one frame. Compared with memcmp to skip redraws. */
typedef struct {
    face_style_t style;
    line_render_t line;
    eye_geom_t eyes[2]; /* by screen position: 0 = screen left (robot's right eye) */
    face_glyph_t glyph; /* FACE_GLYPH_NONE: normal eyes */
    float glyph_w, glyph_h; /* glyph (or normal-eye) scale while popping */
    float spiral_deg;
    float acc[FACE_ACC_COUNT]; /* progress 0..1, or < 0 when not showing */
} face_render_t;

#define FACE_MAX_BOXES (3 + FACE_ACC_COUNT * 2)

void face_draw_frame(lv_layer_t * layer, int32_t ox, int32_t oy, const face_render_t * r);

/* Areas (face coordinates) that cover everything face_draw_frame draws for `r`. */
int face_render_boxes(const face_render_t * r, lv_area_t * out, int max);

#endif /* PETBOT_FACE_INTERNAL_H */
