/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __UI_LAYOUT_H__
#define __UI_LAYOUT_H__

#include <stdint.h>
#include <stdbool.h>
#include "scene.h"
#include "ui_font.h"

/*
 * Where the parts of a scene stand on the round screen: 480 x 480 pixels, visible is the circle of radius
 * 240 around (240, 240). This file knows the geometry and nothing of LVGL but the sizes of texts
 * (ui_font.h); ui.c knows LVGL and nothing of the geometry. What is to be seen is handed over as three
 * kinds of parts, in two layers: the screen, and what lies over it.
 *
 * Colours are 0xRRGGBB.
 */

typedef enum
{
	UI_LAYER_SCREEN,
	UI_LAYER_OVER,
	UI_LAYER_COUNT,
} ui_layer_t;

// The longest text that is put: a text of the scene, cut and with the ellipsis behind it
#define UI_TEXT_SIZE    (SCENE_TEXT_SIZE + 4)

/*
 * Implemented by ui.c. Within a layer the boxes lie at the bottom, the arcs over them and the texts on top;
 * among parts of one kind a later one lies over an earlier one.
 */

// A rectangle with rounded corners. border 0: filled; else only its outline, that many pixels wide
void ui_put_box(ui_layer_t layer, int x, int y, int width, int height, int radius, uint32_t color, int border);

// A part of a ring around the middle of the screen: `radius` is its outer edge, `start` in degrees clockwise
// from 3 o'clock, `sweep` clockwise from there, 360 and more a full ring. A sweep of 0 or less shows nothing
// and still takes the place of an arc, so that the arcs behind it stay what they were.
void ui_put_arc(ui_layer_t layer, int radius, int width, int start, int sweep, uint32_t color);

// A text with its top left corner at x, y. A line break in it starts a new line; centred: the lines are
// centred on each other instead of starting at the same x.
void ui_put_text(ui_layer_t layer, const char *text, ui_font_t font, int x, int y, bool centred, uint32_t color);

// Hands every part of the scene to the three functions above
void ui_layout(const scene_t *scene);

// The row of the scene laid out last at a point, see ui_row_at()
int ui_layout_row_at(int x, int y);

#endif
