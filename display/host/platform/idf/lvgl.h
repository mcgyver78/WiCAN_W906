/*
 * Stand-in for lvgl.h of LVGL 9.5.0 (the managed component lvgl/lvgl, pinned in display/main/idf_component.yml):
 * what display/main/screen.c uses of it, with the signatures of that version (src/lv_init.h,
 * src/tick/lv_tick.h, src/stdlib/lv_mem.h, src/display/lv_display.h, src/misc/lv_timer.h, src/misc/lv_area.h),
 * and the two settings of the heap as lv_conf_kconfig.h and lv_conf_internal.h make them from sdkconfig.
 * The real components/ui/ui.h is built against this file: of LVGL it names the display and nothing else.
 *
 * What LVGL DOES is not in a header: screen_sim.c says what it stands for, and that is little - when it
 * draws, what it flushes, how long it says it can sleep.
 */
#ifndef __SIM_LVGL_H__
#define __SIM_LVGL_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "sdkconfig.h"

// lv_conf_internal.h
#define LV_STDLIB_BUILTIN           0
#define LV_STDLIB_CLIB              1

// lv_conf_kconfig.h: the allocator of LVGL if sdkconfig asks for it, and the size of the pool it can take on
// top of its own, in bytes
#ifdef CONFIG_LV_USE_BUILTIN_MALLOC
#define LV_USE_STDLIB_MALLOC        LV_STDLIB_BUILTIN
#else
#define LV_USE_STDLIB_MALLOC        LV_STDLIB_CLIB
#endif
#ifdef CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES
#define LV_MEM_POOL_EXPAND_SIZE     (CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES * 1024U)
#else
#define LV_MEM_POOL_EXPAND_SIZE     0
#endif

// lv_timer_handler() returns this when no timer of LVGL is due at all
#define LV_NO_TIMER_READY           0xFFFFFFFF

typedef struct _lv_display_t lv_display_t;

// A rectangle with all four edges included
typedef struct
{
	int32_t x1;
	int32_t y1;
	int32_t x2;
	int32_t y2;
} lv_area_t;

typedef void *lv_mem_pool_t;

typedef enum
{
	LV_DISPLAY_RENDER_MODE_PARTIAL,
	LV_DISPLAY_RENDER_MODE_DIRECT,
	LV_DISPLAY_RENDER_MODE_FULL,
} lv_display_render_mode_t;

typedef uint32_t (*lv_tick_get_cb_t)(void);
typedef void (*lv_display_flush_cb_t)(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

void lv_init(void);
void lv_tick_set_cb(lv_tick_get_cb_t cb);
lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes);
lv_display_t *lv_display_create(int32_t hor_res, int32_t ver_res);
void lv_display_set_user_data(lv_display_t *disp, void *user_data);
void *lv_display_get_user_data(lv_display_t *disp);
void lv_display_set_buffers(lv_display_t *disp, void *buf1, void *buf2, uint32_t buf_size,
                            lv_display_render_mode_t render_mode);
void lv_display_set_flush_cb(lv_display_t *disp, lv_display_flush_cb_t flush_cb);
void lv_display_flush_ready(lv_display_t *disp);
uint32_t lv_timer_handler(void);

#endif
