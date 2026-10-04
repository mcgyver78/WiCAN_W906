/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/*
 * LVGL 9.5.0 for the renderer on a PC (render.c). It says what display/sdkconfig.defaults says for the
 * firmware, line by line, so that the pictures made here are the ones the device draws: whoever changes
 * one of the two files changes the other. What neither names keeps the default of LVGL. The defaults of
 * lv_conf_internal.h (used here) and of Kconfig (used by the firmware) are the same for what ui.c uses,
 * with one exception that is put right below.
 */

// RGB565, as the panel is wired
#define LV_COLOR_DEPTH                  16

// The default of Kconfig for 16 bit; lv_conf_internal.h alone would take 0
#define LV_COLOR_MIX_ROUND_OFS          128

// The allocator of LVGL with a small pool of its own, and room for the 512 KB that are added after
// lv_init(): by the firmware from the PSRAM, by render.c from an array
#define LV_USE_STDLIB_MALLOC            LV_STDLIB_BUILTIN
#define LV_MEM_SIZE                     (16 * 1024U)
#define LV_MEM_POOL_EXPAND_SIZE         (512 * 1024U)

// Fonts from the TrueType file, see components/ui/ui_font.c. No cache for kerning: the fonts are made
// without it.
#define LV_USE_TINY_TTF                 1
#define LV_TINY_TTF_FILE_SUPPORT        0
#define LV_TINY_TTF_CACHE_KERNING_CNT   0

// No theme: ui.c sets every style it wants to see
#define LV_USE_THEME_DEFAULT            0
#define LV_USE_THEME_SIMPLE             0
#define LV_USE_THEME_MONO               0

// Warnings and errors of LVGL: a character the font does not have, memory that ran out. Printed, until
// render.c asks for them itself to count them.
#define LV_USE_LOG                      1
#define LV_LOG_LEVEL                    LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF                   1

#define LV_BUILD_EXAMPLES               0
#define LV_BUILD_DEMOS                  0

/*
 * Only here
 */

// A failed assertion ends the run instead of hanging it
#define LV_ASSERT_HANDLER_INCLUDE       <stdlib.h>
#define LV_ASSERT_HANDLER               abort();

#endif
