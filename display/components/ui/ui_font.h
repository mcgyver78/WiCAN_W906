/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __UI_FONT_H__
#define __UI_FONT_H__

#include "lvgl.h"

/*
 * The fonts of the screen: one typeface in seven sizes, made at run time from one TrueType file by LVGL's
 * TinyTTF. The file is DejaVuSans.ttf as it comes with LVGL (scripts/built_in_font): it has the umlauts,
 * the degree sign, the dash, the ellipsis, the middle dot and the rest of Latin-1 that the fonts built into
 * LVGL lack, and its ten digits all have the same width, so a value does not move when it changes.
 *
 * The file is linked into the program as it is and has to provide the symbols
 * _binary_DejaVuSans_ttf_start and _binary_DejaVuSans_ttf_end (CMakeLists.txt for the firmware,
 * display/host/Makefile for the renderer on a PC).
 */

// From the largest to the smallest: a text that does not fit goes on to the next one
typedef enum
{
	UI_FONT_120,    // the one value of a page
	UI_FONT_80,     // the first value of a page, the big text of a progress or a level
	UI_FONT_48,     // the other values
	UI_FONT_36,     // the head of a list, lines of text
	UI_FONT_32,     // the answers of a question, lines of text
	UI_FONT_28,     // labels, titles, rows of a list
	UI_FONT_24,     // the smallest that is read at arm's length: details, notes, what found no room larger
	UI_FONT_COUNT,
} ui_font_t;

// Makes the fonts. Needs lv_init() and a few KB of the LVGL heap; the glyphs are drawn and kept when they
// are first shown (ui_font.c says what that can cost). A font that cannot be made is replaced by the
// default font of LVGL, so that a text is still seen. A value that is no member of the enum counts as
// UI_FONT_24 in the functions below.
void ui_font_init(void);

const lv_font_t *ui_font_lv(ui_font_t font);

// Height of a line of text
int ui_font_height(ui_font_t font);

// From the top of a line down to the line the letters stand on
int ui_font_ascent(ui_font_t font);

// Height of a digit and of a capital letter without an accent: what a number takes of its line
int ui_font_cap(ui_font_t font);

// Width of a text; of its widest line if it has line breaks
int ui_font_width(ui_font_t font, const char *text);

#endif
