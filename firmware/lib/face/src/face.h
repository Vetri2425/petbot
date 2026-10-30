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

/* Create the face (FACE_SCREEN_W x FACE_SCREEN_H, centered) on `parent`. Call once. */
lv_obj_t * face_init(lv_obj_t * parent);

/* Animate to a mood by protocol name ("happy", ...), intensity 0-100.
 * Returns false (and changes nothing) for an unknown mood. */
bool face_set_mood(const char * mood, uint8_t intensity);

/* Play an animation: wave_left, wave_right, blink, look_left, look_right.
 * Returns false for an unknown name. */
bool face_play_anim(const char * name);

/* Advance all animation by `elapsed_ms` and redraw. Call regularly (e.g. every frame). */
void face_tick(uint32_t elapsed_ms);

/* Turn idle life (random blinks, glances, breathing) on or off. On by default;
 * the simulator turns it off for repeatable snapshots. */
void face_set_idle(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* PETBOT_FACE_H */
