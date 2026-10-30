/* PetBot face: two animated eyes drawn with LVGL only (no Arduino, no SDL),
 * shared by the ESP32 firmware and the PC simulator (sim/face_sim). */
#ifndef PETBOT_FACE_H
#define PETBOT_FACE_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FACE_STYLE_FILLED, /* two filled eyes with lids */
    FACE_STYLE_LINE    /* glowing emoticon-like line art with eyebrows and a mouth */
} face_style_t;

/* Create the face (FACE_SCREEN_W x FACE_SCREEN_H, centered) on `parent`. Call once.
 * Starts in FACE_DEFAULT_STYLE (face_style.h). */
lv_obj_t * face_init(lv_obj_t * parent);

/* Animate to a mood by protocol name ("happy", ...), intensity 0-100.
 * Returns false (and changes nothing) for an unknown mood. */
bool face_set_mood(const char * mood, uint8_t intensity);

/* Play an animation: wave_left, wave_right, blink, look_left, look_right.
 * Returns false for an unknown name. */
bool face_play_anim(const char * name);

/* Advance all animation by `elapsed_ms` and redraw. Call regularly (e.g. every frame). */
void face_tick(uint32_t elapsed_ms);

/* Switch the drawing style at runtime (both share all motion). */
void face_set_style(face_style_t style);
face_style_t face_get_style(void);

/* Turn idle life (random blinks, glances, breathing) on or off. On by default;
 * the simulator turns it off for repeatable snapshots. */
void face_set_idle(bool enabled);

/* Glyph eyes and accessories. Anims use these (e.g. "hearts" = heart eyes + blush);
 * they are public so tools can show each one directly. */
typedef enum {
    FACE_GLYPH_NONE,
    FACE_GLYPH_HEART,
    FACE_GLYPH_STAR,
    FACE_GLYPH_SPIRAL,
    FACE_GLYPH_X,
    FACE_GLYPH_CLOSED,
    FACE_GLYPH_COUNT
} face_glyph_t;

typedef enum {
    FACE_ACC_BLUSH,
    FACE_ACC_TEAR,
    FACE_ACC_SWEAT,
    FACE_ACC_ZZZ,
    FACE_ACC_QUESTION,
    FACE_ACC_EXCLAIM,
    FACE_ACC_MUSIC,
    FACE_ACC_COUNT
} face_accessory_t;

/* Morph both eyes into `glyph` for `ms` (0 = until the next call), with a squash pop. */
void face_show_glyph(face_glyph_t glyph, uint32_t ms);

/* Start (or restart) an accessory; it plays once and disappears. */
void face_show_accessory(face_accessory_t accessory);

const char * face_glyph_name(face_glyph_t glyph);
const char * face_accessory_name(face_accessory_t accessory);

#ifdef __cplusplus
}
#endif

#endif /* PETBOT_FACE_H */
