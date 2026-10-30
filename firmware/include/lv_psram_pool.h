/* LVGL memory pool hook (LV_MEM_POOL_ALLOC in lv_conf.h). Included from LVGL's C sources. */
#ifndef PETBOT_LV_PSRAM_POOL_H
#define PETBOT_LV_PSRAM_POOL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the pool reserved by display.cpp (PSRAM if present) before lv_init(). */
void * petbot_lv_pool_alloc(size_t size);

#ifdef __cplusplus
}
#endif

#endif /* PETBOT_LV_PSRAM_POOL_H */
