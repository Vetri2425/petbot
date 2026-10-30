/* LVGL configuration for the ESP32-S3 firmware. Shared settings: lv_conf_common.h. */
#ifndef LV_CONF_H
#define LV_CONF_H

#include "lv_conf_common.h"

/* ST7789 over SPI: RGB565, rendered byte-swapped so it can be sent as-is. */
#define LV_COLOR_DEPTH 16
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 1
#define LV_USE_ST7789 1

/* LVGL's own allocator on a pool placed in PSRAM (lv_psram_pool.h), keeping internal RAM free.
 * Draw buffers are allocated separately in PSRAM by src/display.cpp. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (96 * 1024U)
#define LV_MEM_POOL_INCLUDE "lv_psram_pool.h"
#define LV_MEM_POOL_ALLOC petbot_lv_pool_alloc

/* Only the face task calls LVGL, so no OS integration is needed. */
#define LV_USE_OS LV_OS_NONE

/* Log output goes to logLine() (src/display.cpp), not printf. */
#define LV_LOG_PRINTF 0

#endif /* LV_CONF_H */
