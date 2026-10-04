/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "ui.h"
#include "ui_layout.h"

/*
 * The objects are made once and never deleted: per layer a number of boxes, arcs and labels, enough for
 * the fullest screen. A scene takes as many as it needs, in the order in which ui_layout() puts its parts,
 * and the rest is hidden. Nothing is remembered about an object here: what it shows is asked from LVGL and
 * only set if it differs, because LVGL redraws an object whenever one of its styles is set, changed or not.
 */

#define SIZE    480

typedef enum
{
	PART_BOX,
	PART_ARC,
	PART_TEXT,
	PART_COUNT,
} part_t;

// A value page with six bars and twelve dots has the most boxes, one with six values the most texts
#define PARTS_MAX   24

static const uint8_t parts[UI_LAYER_COUNT][PART_COUNT] =
{
	[UI_LAYER_SCREEN] = {[PART_BOX] = 24, [PART_ARC] = 3, [PART_TEXT] = 20},
	[UI_LAYER_OVER] = {[PART_BOX] = 1, [PART_ARC] = 2, [PART_TEXT] = 3},
};

static lv_obj_t *objects[UI_LAYER_COUNT][PART_COUNT][PARTS_MAX];
static uint8_t taken[UI_LAYER_COUNT][PART_COUNT];   // by the scene that is being laid out

// The scene on the screen: one that is the same again needs no work
static scene_t shown;
static bool shown_valid;

// The next object of a kind, NULL if the scene asks for more than there are
static lv_obj_t *take(ui_layer_t layer, part_t part)
{
	if(taken[layer][part] >= parts[layer][part]) return NULL;
	return objects[layer][part][taken[layer][part]++];
}

static lv_obj_t *plain(lv_obj_t *object)
{
	// Whatever theme the display has: only what is set here is drawn
	lv_obj_remove_style_all(object);
	lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	return object;
}

void ui_init(lv_display_t *display)
{
	lv_obj_t *screen = lv_display_get_screen_active(display);

	ui_font_init();

	lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

	// What is made later lies on top: the layer over the screen after the screen, and in each layer the
	// boxes first, then the arcs, then the texts
	for(int layer = 0; layer < UI_LAYER_COUNT; layer++)
	{
		lv_obj_t *parent = plain(lv_obj_create(screen));

		lv_obj_set_pos(parent, 0, 0);
		lv_obj_set_size(parent, SIZE, SIZE);

		for(int part = 0; part < PART_COUNT; part++)
		{
			for(int i = 0; i < parts[layer][part]; i++)
			{
				lv_obj_t *object;

				if(part == PART_BOX)
				{
					// ui_put_box() sets the colours of the filling and of the outline together and
					// compares only the first, so they start out the same: black, as the outline is
					object = plain(lv_obj_create(parent));
					lv_obj_set_style_bg_color(object, lv_color_hex(0x000000), LV_PART_MAIN);
					lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
				}
				else if(part == PART_ARC)
				{
					object = plain(lv_arc_create(parent));
				}
				else
				{
					object = plain(lv_label_create(parent));
				}
				lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
				objects[layer][part][i] = object;
			}
		}
	}
	shown_valid = false;
}

void ui_show(const scene_t *scene)
{
	if(shown_valid && memcmp(scene, &shown, sizeof(shown)) == 0) return;
	memcpy(&shown, scene, sizeof(shown));
	shown_valid = true;

	memset(taken, 0, sizeof(taken));
	ui_layout(scene);

	// What this scene did not take
	for(int layer = 0; layer < UI_LAYER_COUNT; layer++)
	{
		for(int part = 0; part < PART_COUNT; part++)
		{
			for(int i = taken[layer][part]; i < parts[layer][part]; i++)
			{
				lv_obj_add_flag(objects[layer][part][i], LV_OBJ_FLAG_HIDDEN);
			}
		}
	}
}

int ui_row_at(int x, int y)
{
	return ui_layout_row_at(x, y);
}

void ui_put_box(ui_layer_t layer, int x, int y, int width, int height, int radius, uint32_t color, int border)
{
	lv_obj_t *box = take(layer, PART_BOX);
	lv_color_t lv_color = lv_color_hex(color);

	if(box == NULL) return;
	if(width <= 0 || height <= 0)
	{
		lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
		return;
	}

	// Position and size are compared by LVGL itself
	lv_obj_set_pos(box, x, y);
	lv_obj_set_size(box, width, height);
	if(!lv_color_eq(lv_obj_get_style_bg_color(box, LV_PART_MAIN), lv_color))
	{
		lv_obj_set_style_bg_color(box, lv_color, LV_PART_MAIN);
		lv_obj_set_style_border_color(box, lv_color, LV_PART_MAIN);
	}
	if(lv_obj_get_style_border_width(box, LV_PART_MAIN) != border)
	{
		// An outline is a border around nothing
		lv_obj_set_style_border_width(box, border, LV_PART_MAIN);
		lv_obj_set_style_bg_opa(box, border > 0 ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_MAIN);
	}
	if(lv_obj_get_style_radius(box, LV_PART_MAIN) != radius) lv_obj_set_style_radius(box, radius, LV_PART_MAIN);
	lv_obj_remove_flag(box, LV_OBJ_FLAG_HIDDEN);
}

void ui_put_arc(ui_layer_t layer, int radius, int width, int start, int sweep, uint32_t color)
{
	lv_obj_t *arc = take(layer, PART_ARC);
	lv_color_t lv_color = lv_color_hex(color);

	if(arc == NULL) return;
	if(sweep <= 0)
	{
		lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
		return;
	}

	// Of the arc widget only its background arc is used: the size of the object is the outer edge, and the
	// indicator and the knob have no style and are not drawn
	lv_obj_set_pos(arc, SIZE / 2 - radius, SIZE / 2 - radius);
	lv_obj_set_size(arc, 2 * radius, 2 * radius);
	if(lv_obj_get_style_arc_width(arc, LV_PART_MAIN) != width) lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
	if(!lv_color_eq(lv_obj_get_style_arc_color(arc, LV_PART_MAIN), lv_color))
	{
		lv_obj_set_style_arc_color(arc, lv_color, LV_PART_MAIN);
	}
	// LVGL draws a full ring only for 0 to 360: an end equal to the start is an arc without length. When
	// the angles change it redraws the part between the old and the new end by itself.
	if(sweep >= 360) lv_arc_set_bg_angles(arc, 0, 360);
	else lv_arc_set_bg_angles(arc, start % 360, (start + sweep) % 360);
	lv_obj_remove_flag(arc, LV_OBJ_FLAG_HIDDEN);
}

void ui_put_text(ui_layer_t layer, const char *text, ui_font_t font, int x, int y, bool centred, uint32_t color)
{
	lv_obj_t *label = take(layer, PART_TEXT);
	const lv_font_t *lv_font = ui_font_lv(font);
	lv_color_t lv_color = lv_color_hex(color);
	lv_text_align_t align = centred ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT;

	if(label == NULL) return;
	if(text[0] == '\0')
	{
		lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
		return;
	}

	// A label is as large as its text. Only a line break in the text starts a new line: where a text is
	// broken is decided in ui_layout.c, which knows the circle.
	lv_obj_set_pos(label, x, y);
	if(lv_obj_get_style_text_font(label, LV_PART_MAIN) != lv_font)
	{
		lv_obj_set_style_text_font(label, lv_font, LV_PART_MAIN);
	}
	if(!lv_color_eq(lv_obj_get_style_text_color(label, LV_PART_MAIN), lv_color))
	{
		lv_obj_set_style_text_color(label, lv_color, LV_PART_MAIN);
	}
	if(lv_obj_get_style_text_align(label, LV_PART_MAIN) != align)
	{
		lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
	}
	if(strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
	lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
}
