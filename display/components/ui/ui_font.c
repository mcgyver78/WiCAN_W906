/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ui_font.h"

// DejaVuSans.ttf, linked in as it is. TinyTTF reads the outlines from here whenever it draws a glyph it has
// not kept: the file is never copied.
extern const uint8_t ttf_start[] __asm__("_binary_DejaVuSans_ttf_start");
extern const uint8_t ttf_end[] __asm__("_binary_DejaVuSans_ttf_end");

// Size in pixels (the em square) and the number of glyphs TinyTTF keeps drawn. A glyph that is not kept is
// drawn again from its outline every time it is shown, which is slow but not wrong. What a font can cost
// when all its places are taken, on the ESP32 (bitmap of 8 bits per pixel plus about 260 bytes of
// bookkeeping per glyph): 120 px about 6.5 KB per glyph, 80 px 2.9 KB, 48 px 1.1 KB, 36 px 0.6 KB,
// 32 px 0.5 KB, 28 px 0.4 KB, 24 px 0.3 KB. With the numbers below that is at most about 360 KB of the
// LVGL heap, and only if every font has shown that many different characters: the large ones show digits.
static const struct
{
	int16_t size;
	uint8_t glyphs;
} sizes[UI_FONT_COUNT] =
{
	[UI_FONT_120] = {120, 14},
	[UI_FONT_80] = {80, 20},
	[UI_FONT_48] = {48, 28},
	[UI_FONT_36] = {36, 48},
	[UI_FONT_32] = {32, 48},
	[UI_FONT_28] = {28, 72},
	[UI_FONT_24] = {24, 72},
};

static const lv_font_t *fonts[UI_FONT_COUNT];

void ui_font_init(void)
{
	size_t length = (size_t)(ttf_end - ttf_start);

	for(int i = 0; i < UI_FONT_COUNT; i++)
	{
		// Without kerning: a digit then takes the same room whatever stands next to it, and a text is as
		// wide when it is measured as when it is drawn
		const lv_font_t *font = lv_tiny_ttf_create_data_ex(ttf_start, length, sizes[i].size, LV_FONT_KERNING_NONE,
		                                                    sizes[i].glyphs);

		fonts[i] = font != NULL ? font : lv_font_get_default();
	}
}

const lv_font_t *ui_font_lv(ui_font_t font)
{
	const lv_font_t *found = fonts[(unsigned)font < UI_FONT_COUNT ? font : UI_FONT_24];

	// Asked before ui_font_init(): nothing may be drawn with a font that is not there
	return found != NULL ? found : lv_font_get_default();
}

int ui_font_height(ui_font_t font)
{
	return (int)lv_font_get_line_height(ui_font_lv(font));
}

int ui_font_ascent(ui_font_t font)
{
	const lv_font_t *lv = ui_font_lv(font);

	return (int)(lv->line_height - lv->base_line);
}

int ui_font_width(ui_font_t font, const char *text)
{
	lv_point_t size;

	// EXPAND: lines end where the text says so, not at a width
	lv_text_get_size(&size, text, ui_font_lv(font), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
	return (int)size.x;
}
