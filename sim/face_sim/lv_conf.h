/* LVGL configuration for the PC face simulator. Unset options use LVGL's defaults
 * (lv_conf_internal.h). */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 32
/* System malloc: the 2x display buffer and 300 KB snapshots don't fit LVGL's fixed pool. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* SDL2 window + keyboard. Software rendering works with SDL_VIDEODRIVER=dummy. */
#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
#define LV_SDL_ACCELERATED 0
#define LV_SDL_DIRECT_EXIT 0
#define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT /* full framebuffer: the redraw check reads it */
#define LV_SDL_BUF_COUNT 1

/* --snapshot: render objects to buffers and write PNGs with LVGL's bundled lodepng. */
#define LV_USE_SNAPSHOT 1
#define LV_USE_LODEPNG 1

#endif /* LV_CONF_H */
