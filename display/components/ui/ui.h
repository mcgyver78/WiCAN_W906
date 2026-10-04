/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __UI_H__
#define __UI_H__

#include "lvgl.h"
#include "scene.h"

/*
 * Draws a scene (scene.h) on the round screen with LVGL 9.5.0. It decides nothing: what is shown, in which
 * words, in which tone and with which row in focus is all in the scene. Here it is only placed: where a
 * text stands, how large it is, and what happens to one that is too long for its place (ui_layout.c).
 *
 * Portable C that needs LVGL and the headers of the core, no operating system and no memory of its own
 * beyond LVGL's. Nothing in here locks: the caller holds the lock of LVGL around every call.
 *
 * The look, in short. Black ground, white values, grey labels; an old value and an action that is not
 * offered are darker grey; amber and red mean warning and alarm and are used for nothing else. The ring of
 * the scene is a band of 6 px at the edge. Whatever else is shown stays 8 px away from the edge.
 *   values    title at the top, the dots of the pages along the lower edge, the note above them. One value:
 *             label, value and unit below each other, an arc widget as a gauge around them, a bar widget as
 *             a bar below. Two: one above the other. Three to five: the first large, the others below it
 *             in rows of two. Six: three rows of two. Among several values the unit stands behind the
 *             value, and arc and bar widgets both get a bar.
 *   list      title, the lines of the scene, the rows, the note; a mark at the very top or bottom if there
 *             are more rows that way. The row in focus is a white bar with dark text. The detail of an
 *             action or a sub row stands at the right edge of its row; that of a line or a head row below
 *             its text, as it does wherever both do not fit on one line. A head row has a mark at its left
 *             edge, a sub row is indented.
 *   notice, progress, choice, level
 *             title, note, and between them, centred: the big text, the lines (in as many rows as the circle
 *             makes of them), the two options. Progress and the hold of a choice are a ring that fills from
 *             the top, a level is a gauge. The option in focus is white with dark text.
 *   overlay   a dark panel over everything but the ring, with its lines; an upload with a ring that fills.
 * The dots of the pages are shown on values and notices, the kinds the value pages are made of.
 *
 * display/host/render.c draws every kind of screen on a PC and checks the result; the CI keeps the pictures.
 */

// Builds the objects on the active screen of the display, once: 53 boxes, arcs and labels that are never
// deleted. Going by the sizes of LVGL's structs they take some 20 KB of its heap (reckoned, not measured;
// render.c prints what the heap holds at the end of its run). Glyphs come on top as they are first shown,
// see ui_font.c. The screen has to be 480 x 480.
void ui_init(lv_display_t *display);

// Draws the scene. Called about five times a second and after every input: a scene that is the one shown
// costs a comparison, and of a new one only the parts that differ from what is on the screen are touched,
// so LVGL redraws nothing else.
void ui_show(const scene_t *scene);

// The row of the scene shown last that lies at a point of the screen, for taps: for a SCENE_LIST the index
// among all rows of the screen (scene->first + position; a row is as wide as the screen), for a SCENE_CHOICE
// 0 or 1 for its options. -1 where there is no row, on every other kind of scene, and for every point
// while something lies over the screen.
int ui_row_at(int x, int y);

#endif
