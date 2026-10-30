/* LVGL settings shared by the ESP32 firmware (firmware/include/lv_conf.h) and the PC face
 * simulator (sim/face_sim/lv_conf.h). Target-specific settings live in those two files.
 * Unset options use LVGL's defaults (lv_conf_internal.h). */
#ifndef PETBOT_LV_CONF_COMMON_H
#define PETBOT_LV_CONF_COMMON_H

#define LV_DEF_REFR_PERIOD 33   /* ms, ~30 fps */

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

#define LV_DRAW_SW_COMPLEX 1    /* rounded rects and triangles used by the face */
#define LV_USE_SYSMON 0

#endif /* PETBOT_LV_CONF_COMMON_H */
