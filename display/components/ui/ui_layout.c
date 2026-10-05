/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "ui_layout.h"

/*
 * Every text has its place, given by the kind of the scene and not by the text itself. A text that is too
 * wide for its place gets the next smaller font, and when the smallest is still too wide it is cut and ends
 * with an ellipsis. How wide a place is follows from the circle: a line near the top or the bottom is
 * shorter than one in the middle.
 *
 * What changes while somebody looks at it must not change its size on the way: the font of a value is
 * chosen for a number of four digits, whatever is shown (wide()), and all rows to choose from have one
 * font. What still moves: a value and its unit are centred together, so both shift by half a digit when
 * the number gets a digit more, and a number of five digits and more may need a smaller font.
 */

#define CX                  240
#define CY                  240
#define R_TEXT              232     // everything but the ring stays 8 px away from the edge of the circle
#define RING_RADIUS         240
#define RING_WIDTH          6
#define ARC_RADIUS          224     // an arc that runs around a screen: gauge, progress
#define ARC_WIDTH           12
#define R_INSIDE            204     // what stands inside such an arc stays 8 px away from it
#define HOLD_RADIUS         232     // the ring of the hold is slimmer and further out: the question it runs
#define HOLD_WIDTH          8       // around is the longest text of all screens
#define R_HOLD              216
#define GAUGE_START         150     // the gauge is open at the bottom: from 8 o'clock over the top to
#define GAUGE_SWEEP         240     // 4 o'clock
#define GAUGE_OPEN_TOP      360     // from this height down nothing of the gauge is beside a text
#define PANEL_RADIUS        233     // what lies over the screen hides all of it and leaves the ring to be seen
#define R_PANEL             222

#define COLOR_BLACK         0x000000u
#define COLOR_WHITE         0xFFFFFFu   // values, and what is to be read
#define COLOR_LABEL         0xA0A0A0u   // what names something: titles, labels, units, details, notes
#define COLOR_DIM           0x707070u   // a value that is old, an action that is not offered
#define COLOR_SHADE         0x505050u   // a detail on the white bar of the focus
#define COLOR_TRACK         0x303030u   // the part of an arc or a bar that is not filled
#define COLOR_PANEL         0x202020u
#define COLOR_GREY          0x808080u   // the grey ring
#define COLOR_WARN          0xFFB000u   // amber and red say "warning" and "alarm" and nothing else
#define COLOR_ALARM         0xFF2020u

#define ELLIPSIS            "…"
#define MORE_ABOVE          "▲"
#define MORE_BELOW          "▼"

// Value pages
#define VALUES_TITLE_TOP    56
#define GAUGE_TITLE_TOP     62      // inside the gauge of a page with one value the top is narrow
#define TITLE_GAP           15      // below the line of the title: the capitals of the first label begin 25 px
                                    // below the line the title stands on, twice as far as a label is from
                                    // its own value
#define LABEL_GAP           8       // from the line a label stands on down to the digits of its value, and
                                    // an eighth of the height of the digits on top: 12 px at 48, 18 at 120
#define DIGITS              "0000"  // the number every value has room for, see wide()
#define NOTE_BOTTOM         426     // the dots of the pages run along the edge below it
#define NOTE_ROWS           3       // scene.h speaks of two; "WiCAN-Firmware ohne Display-API – nur
                                    // Live-Werte" needs three down here, where the circle is narrow
#define VALUES_BOTTOM       430
#define BAR_HEIGHT          8
#define BAR_BELOW           6       // with the room between two rows more than above a bar: it belongs to
                                    // the value over it
#define BAR_WIDTH           200
#define CELL_GAP            16
#define GRID_ROWS           3
#define GRID_GAP            4       // between the rows; a row has air of its own above and below its letters
#define DOTS_MAX            LAYOUT_PAGES_MAX
#define DOT_SIZE            8

// Lists
#define HINT_ABOVE_TOP      20
#define HINT_BELOW_TOP      434
#define LIST_TITLE_TOP      50
#define LIST_TOP            89      // five rows of two lines each in the smallest font, wide enough for
#define LIST_BOTTOM         359     // "Elektronisches Zündschloss", and below them still two lines of a note
#define ROW_HEIGHT          54      // that are wide enough for "Motorsteuergerät offline"
#define ROW_INSET           8
#define ROW_INDENT          24
#define ROW_GAP             16      // between a text and the detail behind it
#define ROW_RADIUS          12
#define HEAD_MARK_WIDTH     4
#define HEAD_MARK_INSET     8
#define LIST_NOTE_ROWS      2

// Screens of text
#define TITLE_TOP           62
#define ARC_TITLE_TOP       96      // inside an arc the top is narrow: "Fehlerspeicher löschen" has to fit
#define PROGRESS_FONT       UI_FONT_32  // the lines below the number of a request under way
#define CHOICE_TITLE_TOP    76
#define MIDDLE_BOTTOM       418
#define LINE_GAP            8       // between two lines of the scene, so that it is seen where one ends
#define OPTION_HEIGHT       52
#define OPTION_PAD          14
#define OPTION_GAP          12
#define OPTION_RADIUS       12
#define OVER_TOP            70
#define OVER_BOTTOM         410

typedef enum
{
	SHAPE_OPEN,     // nothing but the edge of the screen limits a line
	SHAPE_ARC,      // an arc runs all around
	SHAPE_HOLD,     // the ring of the hold runs all around
	SHAPE_GAUGE,    // the gauge runs around and is open at the bottom
	SHAPE_PANEL,    // on the panel that lies over the screen
} shape_t;

typedef struct
{
	int x0, y0, x1, y1;
} area_t;

// What is centred between the title and the note of a screen of text
typedef struct
{
	ui_layer_t layer;
	int top, bottom;                        // the band it is centred in
	const char *big;                        // a field of SCENE_SHORT_SIZE bytes, NULL: none
	const char (*lines)[SCENE_TEXT_SIZE];
	int line_count;
	const scene_t *choice;                  // the scene whose two options are shown, NULL: none
} middle_t;

static shape_t shape;

// Where the rows of the scene laid out last are, for ui_layout_row_at()
static struct
{
	int count;
	int first;
	area_t areas[SCENE_ROWS_MAX];
} hits;

static int within(int value, int low, int high)
{
	return value < low ? low : value > high ? high : value;
}

static int isqrt(int value)
{
	int root = 0;

	for(int bit = 1 << 7; bit > 0; bit >>= 1)
	{
		if((root + bit) * (root + bit) <= value) root += bit;
	}
	return root;
}

// Half of the width that is free at every height of a band, 0 if the band leaves the screen
static int room(int top, int bottom)
{
	int radius = R_TEXT;
	int above = CY - top, below = bottom - CY;
	int far = above > below ? above : below;

	if(shape == SHAPE_ARC || (shape == SHAPE_GAUGE && top < GAUGE_OPEN_TOP)) radius = R_INSIDE;
	if(shape == SHAPE_HOLD) radius = R_HOLD;
	if(shape == SHAPE_PANEL) radius = R_PANEL;

	if(far >= radius) return 0;
	return isqrt(radius * radius - far * far);
}

// Where a block of a height begins that is to stand in the middle of the screen, as far as the band
// between `top` and `bottom` allows. The middle is where the circle is widest.
static int centred(int top, int bottom, int height)
{
	int start = CY - height / 2;

	if(start > bottom - height) start = bottom - height;
	if(start < top) start = top;
	return start;
}

// A text of the scene: it ends with its zero or with its field. out: UI_TEXT_SIZE bytes
static void text_of(char *out, const char *field, size_t size)
{
	size_t length = 0;

	while(length < size && field[length] != '\0') length++;
	memcpy(out, field, length);
	out[length] = '\0';
}

// Cuts a text that is wider than `width` and puts the ellipsis behind what is left of it; a text that fits
// stays as it is. Nothing is left if not even the ellipsis fits. text: what is left of UI_TEXT_SIZE bytes.
static void fit(char *text, ui_font_t font, int width)
{
	uint8_t ends[UI_TEXT_SIZE];     // ends[n]: bytes of the first n + 1 characters
	char trial[UI_TEXT_SIZE];
	size_t length = strlen(text);
	int count = 0, low = 0, high;

	if(ui_font_width(font, text) <= width) return;

	for(size_t at = 0; at < length;)
	{
		at++;
		while(at < length && ((unsigned char)text[at] & 0xC0) == 0x80) at++;
		ends[count++] = (uint8_t)at;
	}

	// The most characters that fit with the ellipsis behind them, at least one fewer than there are
	high = count - 1;
	while(low < high)
	{
		int middle = (low + high + 1) / 2;

		memcpy(trial, text, ends[middle - 1]);
		strcpy(trial + ends[middle - 1], ELLIPSIS);
		if(ui_font_width(font, trial) <= width) low = middle;
		else high = middle - 1;
	}
	length = low > 0 ? ends[low - 1] : 0;
	while(length > 0 && text[length - 1] == ' ') length--;

	strcpy(text + length, ELLIPSIS);
	if(ui_font_width(font, text) > width) text[0] = '\0';
}

// Breaks a text into rows that follow each other from `top` down. Every row takes the words that fit into
// `width`, or, with a width of 0, into the screen at the height of the row. The blanks at which a row
// ends become line breaks. Returns the number of rows, 0 if they are more than max_rows or a word is wider
// than its row. With `cut` that does not fail: what finds no room is cut off, see fit().
static int flow(char *text, ui_font_t font, int top, int width, int max_rows, bool cut)
{
	int height = ui_font_height(font);
	char *row = text;
	int rows = 0;

	for(;;)
	{
		int space = width > 0 ? width : 2 * room(top + rows * height, top + (rows + 1) * height);
		char *last = NULL;

		rows++;
		if(ui_font_width(font, row) <= space) return rows;

		if(rows < max_rows)
		{
			// The last blank up to which the row fits
			for(char *blank = strchr(row, ' '); blank != NULL; blank = strchr(blank + 1, ' '))
			{
				bool fits;

				*blank = '\0';
				fits = ui_font_width(font, row) <= space;
				*blank = ' ';
				if(!fits) break;
				last = blank;
			}
		}
		if(last == NULL)
		{
			if(!cut) return 0;
			fit(row, font, space);
			return rows;
		}
		*last = '\n';
		row = last + 1;
	}
}

static uint32_t tone_color(scene_tone_t tone)
{
	switch(tone)
	{
		case SCENE_TONE_DIM:
			return COLOR_DIM;
		case SCENE_TONE_WARN:
			return COLOR_WARN;
		case SCENE_TONE_ALARM:
			return COLOR_ALARM;
		default:
			return COLOR_WHITE;
	}
}

// A text with the middle of the screen in its middle
static void put_centred(ui_layer_t layer, const char *text, ui_font_t font, int top, uint32_t color)
{
	ui_put_text(layer, text, font, CX - ui_font_width(font, text) / 2, top, true, color);
}

// An arc that runs around the screen from the top, and the track it runs on
static void put_progress(ui_layer_t layer, int radius, int width, int permille)
{
	ui_put_arc(layer, radius, width, 0, 360, COLOR_TRACK);
	ui_put_arc(layer, radius, width, 270, 360 * within(permille, 0, 1000) / 1000, COLOR_WHITE);
}

static void put_gauge(int permille, uint32_t color)
{
	ui_put_arc(UI_LAYER_SCREEN, ARC_RADIUS, ARC_WIDTH, GAUGE_START, GAUGE_SWEEP, COLOR_TRACK);
	ui_put_arc(UI_LAYER_SCREEN, ARC_RADIUS, ARC_WIDTH, GAUGE_START, GAUGE_SWEEP * within(permille, 0, 1000) / 1000,
	           color);
}

static void put_ring(const ring_t *ring)
{
	int sweep = 360;
	uint32_t color = COLOR_WHITE;

	switch(ring->kind)
	{
		case RING_YELLOW:
			color = COLOR_WARN;
			break;
		case RING_GREY:
			color = COLOR_GREY;
			break;
		case RING_RED:
			color = COLOR_ALARM;
			break;
		case RING_PROGRESS:
			sweep = 360 * within(ring->permille, 0, 1000) / 1000;
			break;
		default:
			sweep = 0;
			break;
	}
	ui_put_arc(UI_LAYER_SCREEN, RING_RADIUS, RING_WIDTH, 270, sweep, color);
}

// One line at the top of a screen, empty or not: it always takes the first text of the screen
static void put_title(const char *field, int top, ui_font_t font, uint32_t color)
{
	char text[UI_TEXT_SIZE];

	text_of(text, field, SCENE_TEXT_SIZE);
	if(ui_font_width(font, text) > 2 * room(top, top + ui_font_height(font))) font = UI_FONT_24;
	fit(text, font, 2 * room(top, top + ui_font_height(font)));
	put_centred(UI_LAYER_SCREEN, text, font, top, color);
}

// The note of a screen: one line if it fits, else up to max_rows lines of the smallest font. from_bottom:
// the rows end at `edge`, else they begin there. Returns where they begin.
static int put_note(const char *field, int edge, bool from_bottom, int max_rows)
{
	char text[UI_TEXT_SIZE];
	ui_font_t font = UI_FONT_24;
	int top = edge;

	for(int rows = 1; rows <= max_rows; rows++)
	{
		for(font = rows == 1 ? UI_FONT_28 : UI_FONT_24; font <= UI_FONT_24; font++)
		{
			top = from_bottom ? edge - rows * ui_font_height(font) : edge;
			text_of(text, field, SCENE_TEXT_SIZE);
			if(flow(text, font, top, 0, rows, false) > 0)
			{
				put_centred(UI_LAYER_SCREEN, text, font, top, COLOR_LABEL);
				return top;
			}
		}
	}

	font = UI_FONT_24;
	top = from_bottom ? edge - max_rows * ui_font_height(font) : edge;
	text_of(text, field, SCENE_TEXT_SIZE);
	flow(text, font, top, 0, max_rows, true);
	put_centred(UI_LAYER_SCREEN, text, font, top, COLOR_LABEL);
	return top;
}

/*
 * Value pages
 */

// From the top of a line down to the top of its digits and capital letters
static int lead(ui_font_t font)
{
	return ui_font_ascent(font) - ui_font_cap(font);
}

// From the line a number stands on down to where something else may begin: its comma reaches half way to
// the bottom of the line, and a little air
static int below(ui_font_t font)
{
	return (ui_font_height(font) - ui_font_ascent(font)) / 2 + 4;
}

// The width a value is given when the font for its place is chosen: at least that of four digits, so that
// the font follows from the place and the unit and not from the number shown at the moment. (The digits
// of the font all have the same width.) 999 and 1000 hPa have one size this way; 99999 km do not grow.
static int wide(ui_font_t font, const char *value)
{
	int width = ui_font_width(font, value), digits = ui_font_width(font, DIGITS);

	return width > digits ? width : digits;
}

// How a label and the value below it stand, from the top of the label down
typedef struct
{
	int line;       // the line the value and its unit stand on
	int under;      // where a bar or a unit below the value begins
	int height;
} cell_t;

// tight: the value is a number. Digits leave the top of their line empty, and the larger the font the more,
// so the label is not placed by the height of the lines but by what is seen: LABEL_GAP below the line it
// stands on the digits begin. The text of a state widget may have an accent up there and letters that
// reach below the line, and gets both lines whole.
static cell_t cell_of(ui_font_t label_font, ui_font_t value_font, bool tight, bool bar)
{
	int descent = ui_font_height(value_font) - ui_font_ascent(value_font);
	int digits = ui_font_cap(value_font);
	cell_t cell;

	cell.line = tight ? ui_font_ascent(label_font) + LABEL_GAP + digits / 8 + digits :
	            ui_font_height(label_font) + ui_font_ascent(value_font);
	cell.under = cell.line + (tight ? below(value_font) : descent);
	cell.height = bar ? cell.under + BAR_HEIGHT + BAR_BELOW : cell.line + descent;
	return cell;
}

// The bar of a bar widget: how far the value lies between its limits
static void put_bar(const scene_item_t *item, int centre, int top, int width)
{
	int left = centre - width / 2;

	ui_put_box(UI_LAYER_SCREEN, left, top, width, BAR_HEIGHT, BAR_HEIGHT / 2, COLOR_TRACK, 0);
	ui_put_box(UI_LAYER_SCREEN, left, top, width * within(item->permille, 0, 1000) / 1000, BAR_HEIGHT, BAR_HEIGHT / 2,
	           tone_color(item->tone), 0);
}

// The only value of a page: label, value and unit below each other in the middle
static void put_single(const scene_item_t *item, int top, int bottom)
{
	char label[UI_TEXT_SIZE], value[UI_TEXT_SIZE], unit[UI_TEXT_SIZE];
	bool bar = item->widget == LAYOUT_WIDGET_BAR;
	bool tight = item->widget != LAYOUT_WIDGET_STATE;
	int unit_top = 0, bar_top = 0, y = top, value_top = top, space;
	ui_font_t font, small;

	text_of(label, item->label, sizeof(item->label));
	text_of(value, item->text, sizeof(item->text));
	text_of(unit, item->unit, sizeof(item->unit));

	// The largest font with which the value fits, and a number of four digits in its place
	for(font = UI_FONT_120;; font++)
	{
		// The unit stands as close below the value as the value below its label, the bar below both
		cell_t cell = cell_of(UI_FONT_28, font, tight, false);
		int height = cell.height;

		unit_top = cell.under - lead(UI_FONT_36);
		bar_top = cell.under;
		if(unit[0] != '\0')
		{
			height = unit_top + ui_font_height(UI_FONT_36);
			bar_top = unit_top + ui_font_ascent(UI_FONT_36) + below(UI_FONT_36);
		}
		if(bar) height = bar_top + BAR_HEIGHT;

		y = centred(top, bottom, height);
		value_top = y + cell.line - ui_font_ascent(font);
		if(font == UI_FONT_24) break;
		if(height <= bottom - top && wide(font, value) <= 2 * room(value_top, value_top + ui_font_height(font))) break;
	}

	small = UI_FONT_28;
	space = 2 * room(y, y + ui_font_height(UI_FONT_28));
	if(ui_font_width(small, label) > space) small = UI_FONT_24;
	fit(label, small, space);
	put_centred(UI_LAYER_SCREEN, label, small, y + ui_font_ascent(UI_FONT_28) - ui_font_ascent(small), COLOR_LABEL);

	fit(value, font, 2 * room(value_top, value_top + ui_font_height(font)));
	put_centred(UI_LAYER_SCREEN, value, font, value_top, tone_color(item->tone));

	space = 2 * room(y + unit_top, y + unit_top + ui_font_height(UI_FONT_36));
	for(small = UI_FONT_36; small < UI_FONT_24 && ui_font_width(small, unit) > space; small++);
	fit(unit, small, space);
	put_centred(UI_LAYER_SCREEN, unit, small, y + unit_top + ui_font_ascent(UI_FONT_36) - ui_font_ascent(small),
	            COLOR_LABEL);

	if(bar) put_bar(item, CX, y + bar_top, BAR_WIDTH);
}

// The font for a value in a cell `width` wide: the largest from `first` down with which a number of four
// digits fits in front of the unit
static ui_font_t cell_font(const scene_item_t *item, int width, ui_font_t first, ui_font_t unit_font)
{
	char value[UI_TEXT_SIZE], unit[UI_TEXT_SIZE];
	int unit_width;
	ui_font_t font;

	text_of(value, item->text, sizeof(item->text));
	text_of(unit, item->unit, sizeof(item->unit));
	unit_width = unit[0] != '\0' ? ui_font_height(unit_font) / 4 + ui_font_width(unit_font, unit) : 0;
	for(font = first; font < UI_FONT_24 && wide(font, value) + unit_width > width; font++);
	return font;
}

// One of several values of a page: the label, below it the value with its unit behind it, and the bar of
// an arc or bar widget (an arc has no room around one value among several). `cell` and the fonts are
// those of the row.
static void put_cell(const scene_item_t *item, int centre, int top, int width, const cell_t *cell,
                     ui_font_t label_font, ui_font_t font, ui_font_t unit_font)
{
	char label[UI_TEXT_SIZE], value[UI_TEXT_SIZE], unit[UI_TEXT_SIZE];
	int gap = ui_font_height(unit_font) / 4;
	int value_width, unit_width, left;
	ui_font_t small = label_font;

	text_of(label, item->label, sizeof(item->label));
	text_of(value, item->text, sizeof(item->text));
	text_of(unit, item->unit, sizeof(item->unit));

	if(ui_font_width(small, label) > width) small = UI_FONT_24;
	fit(label, small, width);
	ui_put_text(UI_LAYER_SCREEN, label, small, centre - ui_font_width(small, label) / 2,
	            top + ui_font_ascent(label_font) - ui_font_ascent(small), false, COLOR_LABEL);

	unit_width = unit[0] != '\0' ? gap + ui_font_width(unit_font, unit) : 0;
	if(ui_font_width(font, value) + unit_width > width)
	{
		// Too wide in the smallest font: the unit gives way before a digit of the value does
		fit(value, font, width);
		fit(unit, unit_font, width - ui_font_width(font, value) - gap);
		unit_width = unit[0] != '\0' ? gap + ui_font_width(unit_font, unit) : 0;
	}
	value_width = ui_font_width(font, value);

	// Both stand on the line of the row
	left = centre - (value_width + unit_width) / 2;
	ui_put_text(UI_LAYER_SCREEN, value, font, left, top + cell->line - ui_font_ascent(font), false,
	            tone_color(item->tone));
	ui_put_text(UI_LAYER_SCREEN, unit, unit_font, left + value_width + gap, top + cell->line - ui_font_ascent(unit_font),
	            false, COLOR_LABEL);

	if(item->widget == LAYOUT_WIDGET_ARC || item->widget == LAYOUT_WIDGET_BAR)
	{
		put_bar(item, centre, top + cell->under, width < BAR_WIDTH ? width : BAR_WIDTH);
	}
}

// Two to six values: rows of one or two
static void put_grid(const scene_item_t *items, int count, int top, int bottom)
{
	// Values in each row: two halves; the first large and the others below it; three rows of two
	static const uint8_t cells[LAYOUT_ITEMS_MAX + 1][GRID_ROWS] =
	{
		{0}, {0}, {1, 1}, {1, 2}, {1, 2, 1}, {1, 2, 2}, {2, 2, 2},
	};
	// Fonts of a large value, of the others and of the label of a large value. The first set that fits
	// between the title and the note is taken: a note of three lines leaves less room than none.
	static const ui_font_t fonts[][3] =
	{
		{UI_FONT_80, UI_FONT_48, UI_FONT_28},
		{UI_FONT_48, UI_FONT_48, UI_FONT_24},
		{UI_FONT_48, UI_FONT_36, UI_FONT_24},
		{UI_FONT_36, UI_FONT_28, UI_FONT_24},
		{UI_FONT_28, UI_FONT_24, UI_FONT_24},
	};
	const int sets = (int)(sizeof(fonts) / sizeof(fonts[0]));
	bool large[GRID_ROWS] = {false}, bars[GRID_ROWS] = {false}, tight[GRID_ROWS] = {false};
	cell_t row_cells[GRID_ROWS] = {{0, 0, 0}};
	int rows = 0, first = 0, set, total, y;

	while(rows < GRID_ROWS && cells[count][rows] > 0)
	{
		large[rows] = cells[count][rows] == 1 && (rows == 0 || count == 2);
		// The values of a row stand on one line, so one text among them decides for the row
		tight[rows] = true;
		for(int i = 0; i < cells[count][rows]; i++)
		{
			layout_widget_t widget = items[first + i].widget;

			if(widget == LAYOUT_WIDGET_ARC || widget == LAYOUT_WIDGET_BAR) bars[rows] = true;
			if(widget == LAYOUT_WIDGET_STATE) tight[rows] = false;
		}
		first += cells[count][rows];
		rows++;
	}

	for(set = 0;; set++)
	{
		total = (rows - 1) * GRID_GAP;
		for(int row = 0; row < rows; row++)
		{
			row_cells[row] = cell_of(large[row] ? fonts[set][2] : UI_FONT_24, large[row] ? fonts[set][0] : fonts[set][1],
			                         tight[row], bars[row]);
			total += row_cells[row].height;
		}
		if(total <= bottom - top || set == sets - 1) break;
	}

	y = centred(top, bottom, total);
	first = 0;
	for(int row = 0; row < rows; row++)
	{
		int in_row = cells[count][row];
		int width = (2 * room(y, y + row_cells[row].height) - (in_row - 1) * CELL_GAP) / in_row;
		ui_font_t label_font = large[row] ? fonts[set][2] : UI_FONT_24;
		ui_font_t value_font = large[row] ? fonts[set][0] : fonts[set][1];
		ui_font_t unit_font = value_font == UI_FONT_80 ? UI_FONT_28 : UI_FONT_24;
		ui_font_t font = value_font;
		cell_t cell;

		// One font for the values of a row: the one its narrowest fit needs. Two sizes side by side
		// would look like a mistake.
		for(int i = 0; i < in_row; i++)
		{
			ui_font_t needed = cell_font(&items[first + i], width, value_font, unit_font);

			if(needed > font) font = needed;
		}
		// The row keeps its height. A smaller font stands where its own size puts it: as close to the
		// labels as any value, the room it does not need below it. A unit is never larger than its value.
		cell = cell_of(label_font, font, tight[row], bars[row]);
		if(unit_font < font) unit_font = font;
		for(int i = 0; i < in_row; i++)
		{
			put_cell(&items[first + i], CX + (2 * i - (in_row - 1)) * (width + CELL_GAP) / 2, y, width, &cell, label_font,
			         font, unit_font);
		}
		first += in_row;
		y += row_cells[row].height + GRID_GAP;
	}
}

// The pages the knob turns through, as dots along the lower edge, the one shown filled
static void put_dots(const scene_t *scene)
{
	// Where a dot lies, by its distance from the lowest point in steps of 2.25 degrees
	static const uint8_t across[DOTS_MAX] = {0, 9, 17, 26, 35, 43, 52, 60, 69, 77, 85, 93};
	static const uint8_t down[DOTS_MAX] = {222, 222, 221, 220, 219, 218, 216, 214, 211, 208, 205, 202};
	int count = within(scene->dots, 0, DOTS_MAX);

	for(int i = 0; i < count; i++)
	{
		int step = 2 * i - (count - 1);
		int x = step < 0 ? CX - across[-step] : CX + across[step];
		int y = CY + down[step < 0 ? -step : step];
		bool shown = i == scene->dot;

		ui_put_box(UI_LAYER_SCREEN, x - DOT_SIZE / 2, y - DOT_SIZE / 2, DOT_SIZE, DOT_SIZE, DOT_SIZE / 2,
		           shown ? COLOR_WHITE : COLOR_LABEL, shown ? 0 : 2);
	}
}

static void put_values(const scene_t *scene)
{
	int count = within(scene->item_count, 0, LAYOUT_ITEMS_MAX);
	bool gauge = count == 1 && scene->items[0].widget == LAYOUT_WIDGET_ARC;
	int title_top = gauge ? GAUGE_TITLE_TOP : VALUES_TITLE_TOP;
	int top = title_top + ui_font_height(UI_FONT_24) + TITLE_GAP;
	int bottom = VALUES_BOTTOM;

	shape = gauge ? SHAPE_GAUGE : SHAPE_OPEN;
	if(gauge) put_gauge(scene->items[0].permille, tone_color(scene->items[0].tone));

	// The title names the page, not a value: smaller than a label, and further from the first label than
	// that is from its value, so that the two are not read as one block
	put_title(scene->title, title_top, UI_FONT_24, COLOR_LABEL);
	if(scene->note[0] != '\0') bottom = put_note(scene->note, NOTE_BOTTOM, true, NOTE_ROWS) - 4;

	if(count == 1) put_single(&scene->items[0], top, bottom);
	else if(count > 1) put_grid(scene->items, count, top, bottom);
}

/*
 * Lists
 */

// One row of a list. All rows have the same place between `left` and `left + width`, wherever they stand:
// a row does not change its looks while the list moves under the focus.
//
// What can be chosen has one font, 28 px: the largest with which every text of the core fits the 336 px of a
// row together with its detail ("Drehrichtung" with "umgekehrt" 334 px, "Freigabe" with "an – noch 10:00"
// 331 px, "Liste vor dem Löschen" 318 px; in 32 px they are 358, 348 and 363 px). With the largest font
// that fits each row, as it was, one row of a menu was smaller than the others, and "Drehrichtung" changed
// its size when it was pressed. 32 px for all need shorter texts in scene.c.
static void put_row(const scene_row_t *source, int left, int top, int width, int height)
{
	char text[UI_TEXT_SIZE], detail[UI_TEXT_SIZE];
	bool head = source->kind == SCENE_ROW_HEAD;
	bool named = head || source->kind == SCENE_ROW_LINE;
	bool two = height >= 2 * ui_font_height(UI_FONT_24);
	int indent = source->kind == SCENE_ROW_SUB ? ROW_INDENT : 0;
	int x = left + ROW_INSET + indent;
	int space = width - 2 * ROW_INSET - indent;
	int small = ui_font_height(UI_FONT_24);
	int detail_width, behind, y;
	bool one;
	uint32_t ink = source->enabled ? COLOR_WHITE : COLOR_DIM, pale = COLOR_LABEL;
	ui_font_t first = head ? UI_FONT_36 : UI_FONT_28, font;

	// The focus: a white bar with dark text on it
	if(source->focus)
	{
		ui_put_box(UI_LAYER_SCREEN, left, top, width, height, ROW_RADIUS, COLOR_WHITE, 0);
		if(source->enabled) ink = COLOR_BLACK;
		pale = COLOR_SHADE;
	}
	else if(head)
	{
		// What makes a head row one when its text is no larger than the others: a mark at its left edge
		ui_put_box(UI_LAYER_SCREEN, left, top + HEAD_MARK_INSET, HEAD_MARK_WIDTH, height - 2 * HEAD_MARK_INSET,
		           HEAD_MARK_WIDTH / 2, COLOR_LABEL, 0);
	}

	text_of(text, source->text, sizeof(source->text));
	text_of(detail, source->detail, sizeof(source->detail));
	while(first < UI_FONT_24 && ui_font_height(first) > height) first++;

	// The largest font with which the text and the detail behind it fit on one line
	detail_width = ui_font_width(UI_FONT_24, detail);
	behind = detail[0] != '\0' ? ROW_GAP + detail_width : 0;
	for(font = first; font < UI_FONT_24 && ui_font_width(font, text) + behind > space; font++);
	one = ui_font_width(font, text) + behind <= space;

	if(two && detail[0] != '\0' && (named || !one))
	{
		// The detail below the text. A line of a list is a name and what is said about it: those always
		// stand like this, so that the rows of a fault memory list look alike, the short names and the long.
		y = top + (height - 2 * small) / 2;
		fit(text, UI_FONT_24, space);
		fit(detail, UI_FONT_24, space);
		ui_put_text(UI_LAYER_SCREEN, text, UI_FONT_24, x, y, false, ink);
		ui_put_text(UI_LAYER_SCREEN, detail, UI_FONT_24, x, y + small, false, pale);
	}
	else if(two && detail[0] == '\0' && !one)
	{
		// The text alone on two lines
		int rows = flow(text, UI_FONT_24, 0, space, 2, true);

		ui_put_text(UI_LAYER_SCREEN, text, UI_FONT_24, x, top + (height - rows * small) / 2, false, ink);
		ui_put_text(UI_LAYER_SCREEN, "", UI_FONT_24, x, top, false, pale);
	}
	else
	{
		if(!one)
		{
			// A row too low for two lines and too narrow for both: the detail gives way to the text
			detail[0] = '\0';
			detail_width = 0;
			for(font = first; font < UI_FONT_24 && ui_font_width(font, text) > space; font++);
			fit(text, font, space);
		}
		// On one line, the detail at the right edge, both standing on the same line
		y = top + (height - ui_font_height(font)) / 2;
		ui_put_text(UI_LAYER_SCREEN, text, font, x, y, false, ink);
		ui_put_text(UI_LAYER_SCREEN, detail, UI_FONT_24, left + width - ROW_INSET - detail_width,
		            y + ui_font_ascent(font) - ui_font_ascent(UI_FONT_24), false, pale);
	}
}

static void put_list(const scene_t *scene)
{
	int rows = within(scene->row_count, 0, SCENE_ROWS_MAX);
	int lines = within(scene->line_count, 0, SCENE_LINES_MAX);
	int line_height = ui_font_height(UI_FONT_24);
	int taken[SCENE_LINES_MAX] = {0};   // rows of text each line gets: one, or two for one that is too wide
	int band = LIST_BOTTOM - LIST_TOP;
	int lines_height = 0, pitch = ROW_HEIGHT, y = LIST_TOP, half;
	bool grown;

	shape = SHAPE_OPEN;
	half = room(LIST_TOP, LIST_BOTTOM);

	put_title(scene->title, LIST_TITLE_TOP, UI_FONT_28, COLOR_LABEL);
	put_centred(UI_LAYER_SCREEN, scene->first > 0 ? MORE_ABOVE : "", UI_FONT_24, HINT_ABOVE_TOP, COLOR_LABEL);
	put_centred(UI_LAYER_SCREEN, scene->first + rows < scene->total ? MORE_BELOW : "", UI_FONT_24, HINT_BELOW_TOP,
	            COLOR_LABEL);
	if(scene->note[0] != '\0') put_note(scene->note, LIST_BOTTOM + 3, false, LIST_NOTE_ROWS);

	// Where the lines stand depends on how many rows of text they take, and how much a row of text holds on
	// where it stands. So every line begins with one row, and one that is too wide where it comes to stand
	// gets a second ("Letzter Auftrag fehlgeschlagen" above the four rows of the fault memory), as long as
	// that leaves the rows of the list the height of a line. Then all is placed again.
	for(int i = 0; i < lines; i++) taken[i] = 1;
	do
	{
		int count = 0, at;

		for(int i = 0; i < lines; i++) count += taken[i];
		lines_height = count * line_height + (lines > 0 && rows > 0 ? 4 : 0);

		// More lines and rows than the screen has room for: the rows become lower
		pitch = ROW_HEIGHT;
		if(rows > 0 && lines_height + rows * pitch > band) pitch = (band - lines_height) / rows;
		y = LIST_TOP + (band - lines_height - rows * pitch) / 2;

		grown = false;
		at = y;
		for(int i = 0; i < lines; i++)
		{
			char text[UI_TEXT_SIZE];

			text_of(text, scene->lines[i], sizeof(scene->lines[i]));
			if(taken[i] == 1 && ui_font_width(UI_FONT_24, text) > 2 * room(at, at + line_height) &&
			   band - lines_height - line_height >= rows * line_height)
			{
				taken[i] = 2;
				grown = true;
				break;
			}
			at += taken[i] * line_height;
		}
	}
	while(grown);

	for(int i = 0; i < lines; i++)
	{
		char text[UI_TEXT_SIZE];

		text_of(text, scene->lines[i], sizeof(scene->lines[i]));
		flow(text, UI_FONT_24, y, 0, taken[i], true);
		put_centred(UI_LAYER_SCREEN, text, UI_FONT_24, y, COLOR_WHITE);
		y += taken[i] * line_height;
	}
	if(lines > 0 && rows > 0) y += 4;

	hits.count = rows;
	hits.first = scene->first;
	for(int i = 0; i < rows; i++)
	{
		put_row(&scene->rows[i], CX - half, y, 2 * half, pitch);
		hits.areas[i] = (area_t){0, y, 2 * CX, y + pitch};
		y += pitch;
	}
}

/*
 * Screens of text: notice, progress, choice, level, and what lies over a screen
 */

// The lines of a body in rows from `top` on, each line in as many rows as it needs. Every line gets at
// least one row; lines for which none is left are left out. Returns the rows taken, 0 if they are more
// than max_rows (never with `cut`). Only with `draw` anything is put.
static int put_lines(const middle_t *middle, ui_font_t font, int top, int max_rows, bool cut, bool draw)
{
	int height = ui_font_height(font);
	int rows = 0;

	for(int i = 0; i < middle->line_count; i++)
	{
		char text[UI_TEXT_SIZE];
		int limit = max_rows - rows - (middle->line_count - 1 - i);
		int y = top + rows * height + i * LINE_GAP;
		int used;

		if(limit < 1 && rows < max_rows) limit = 1;
		if(limit < 1) return cut ? rows : 0;

		text_of(text, middle->lines[i], SCENE_TEXT_SIZE);
		used = flow(text, font, y, 0, limit, cut);
		if(used == 0) return 0;
		if(draw) put_centred(middle->layer, text, font, y, COLOR_WHITE);
		rows += used;
	}
	return rows;
}

// The largest font with which the two options fit side by side into `width`; false if there is none
static bool options_font(const scene_t *scene, int width, ui_font_t *font)
{
	char text[2][UI_TEXT_SIZE];

	text_of(text[0], scene->options[0], sizeof(scene->options[0]));
	text_of(text[1], scene->options[1], sizeof(scene->options[1]));
	for(*font = UI_FONT_32; *font <= UI_FONT_24; (*font)++)
	{
		if(ui_font_width(*font, text[0]) + ui_font_width(*font, text[1]) + 4 * OPTION_PAD + OPTION_GAP <= width)
		{
			return true;
		}
	}
	*font = UI_FONT_24;
	return false;
}

// One of the two answers. The one in focus is white with dark text, like the focused row of a list.
static void put_option(const scene_t *scene, int index, ui_font_t font, int centre, int top, int space)
{
	char text[UI_TEXT_SIZE];
	bool focus = index == (scene->option == 1 ? 1 : 0);
	int text_width, width;

	text_of(text, scene->options[index], sizeof(scene->options[index]));
	fit(text, font, space - 2 * OPTION_PAD);
	text_width = ui_font_width(font, text);
	width = text_width + 2 * OPTION_PAD;

	ui_put_box(UI_LAYER_SCREEN, centre - width / 2, top, width, OPTION_HEIGHT, OPTION_RADIUS,
	           focus ? COLOR_WHITE : COLOR_LABEL, focus ? 0 : 2);
	ui_put_text(UI_LAYER_SCREEN, text, font, centre - text_width / 2, top + (OPTION_HEIGHT - ui_font_height(font)) / 2,
	            false, focus ? COLOR_BLACK : COLOR_WHITE);
	hits.areas[index] = (area_t){centre - width / 2, top, centre - width / 2 + width, top + OPTION_HEIGHT};
}

static void put_options(const scene_t *scene, int top, bool stacked)
{
	ui_font_t font;

	hits.count = 2;
	hits.first = 0;

	if(stacked)
	{
		for(int i = 0; i < 2; i++)
		{
			char text[UI_TEXT_SIZE];
			int y = top + i * (OPTION_HEIGHT + OPTION_GAP);
			int space = 2 * room(y, y + OPTION_HEIGHT);

			text_of(text, scene->options[i], sizeof(scene->options[i]));
			for(font = UI_FONT_32; font < UI_FONT_24 && ui_font_width(font, text) + 2 * OPTION_PAD > space; font++);
			put_option(scene, i, font, CX, y, space);
		}
	}
	else
	{
		char text[UI_TEXT_SIZE];
		int space = 2 * room(top, top + OPTION_HEIGHT);
		int widths[2], left;

		options_font(scene, space, &font);
		for(int i = 0; i < 2; i++)
		{
			text_of(text, scene->options[i], sizeof(scene->options[i]));
			widths[i] = ui_font_width(font, text) + 2 * OPTION_PAD;
		}
		left = CX - (widths[0] + OPTION_GAP + widths[1]) / 2;
		put_option(scene, 0, font, left + widths[0] / 2, top, widths[0]);
		put_option(scene, 1, font, left + widths[0] + OPTION_GAP + widths[1] / 2, top, widths[1]);
	}
}

// Height of the lines of a body in `rows` rows
static int lines_height(const middle_t *middle, ui_font_t font, int rows)
{
	return rows * ui_font_height(font) + (middle->line_count > 1 ? (middle->line_count - 1) * LINE_GAP : 0);
}

// Height of what stands in the middle with `rows` rows of text
static int middle_height(const middle_t *middle, ui_font_t font, int rows, bool stacked)
{
	int height = lines_height(middle, font, rows);

	if(middle->big != NULL) height += ui_font_height(UI_FONT_80);
	if(middle->choice != NULL)
	{
		height += (rows > 0 ? OPTION_GAP : 0) + (stacked ? 2 * OPTION_HEIGHT + OPTION_GAP : OPTION_HEIGHT);
	}
	return height;
}

// Where the options begin when the middle begins at `top`
static int options_top(const middle_t *middle, ui_font_t font, int rows, int top)
{
	return top + middle_height(middle, font, rows, false) - OPTION_HEIGHT;
}

// The big text of a progress or a level, in a place as high as its largest font
static void put_big(const middle_t *middle, int top)
{
	char text[UI_TEXT_SIZE];
	int big_height = ui_font_height(UI_FONT_80);
	int space = 2 * room(top, top + big_height);
	ui_font_t big;

	text_of(text, middle->big, SCENE_SHORT_SIZE);
	for(big = UI_FONT_80; big < UI_FONT_36 && ui_font_width(big, text) > space; big++);
	fit(text, big, space);
	put_centred(middle->layer, text, big, top + (big_height - ui_font_height(big)) / 2, COLOR_WHITE);
}

static void draw_middle(const middle_t *middle, ui_font_t font, int rows, bool stacked, bool cut)
{
	int top = centred(middle->top, middle->bottom, middle_height(middle, font, rows, stacked));

	if(middle->big != NULL)
	{
		put_big(middle, top);
		top += ui_font_height(UI_FONT_80);
	}

	put_lines(middle, font, top, rows, cut, true);
	top += lines_height(middle, font, rows);

	if(middle->choice != NULL) put_options(middle->choice, top + (rows > 0 ? OPTION_GAP : 0), stacked);
}

// Centres the big text, the lines and the options between the title and the note: with the largest font
// with which the lines fit, in as few rows as that font needs. The circle decides how much a row holds,
// and the number of rows where each of them lies, so one is tried after the other.
static void put_middle(const middle_t *middle)
{
	static const ui_font_t fonts[] = {UI_FONT_36, UI_FONT_32, UI_FONT_28, UI_FONT_24};
	int band = middle->bottom - middle->top;
	int rows;

	for(int stacked = 0; stacked <= (middle->choice != NULL ? 1 : 0); stacked++)
	{
		for(size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++)
		{
			for(rows = middle->line_count; middle_height(middle, fonts[i], rows, stacked) <= band; rows++)
			{
				int top = centred(middle->top, middle->bottom, middle_height(middle, fonts[i], rows, stacked));
				int text_top = top + (middle->big != NULL ? ui_font_height(UI_FONT_80) : 0);
				ui_font_t font;

				// Side by side the options need a row that is wide enough for both
				if(middle->choice != NULL && !stacked)
				{
					int y = options_top(middle, fonts[i], rows, top);

					if(!options_font(middle->choice, 2 * room(y, y + OPTION_HEIGHT), &font)) break;
				}
				if(middle->line_count == 0 || put_lines(middle, fonts[i], text_top, rows, false, false) > 0)
				{
					draw_middle(middle, fonts[i], rows, stacked, false);
					return;
				}
			}
			if(middle->line_count == 0) break;
		}
	}

	// Too much for the screen: as many rows of the smallest font as there is room for, the rest is cut
	rows = 0;
	while(middle->line_count > 0 && middle_height(middle, UI_FONT_24, rows + 1, middle->choice != NULL) <= band)
	{
		rows++;
	}
	draw_middle(middle, UI_FONT_24, rows, middle->choice != NULL, true);
}

// The title and the note of a screen of text, and the band between them
static void put_frame(const scene_t *scene, int title_top, uint32_t title_color, middle_t *middle)
{
	middle->layer = UI_LAYER_SCREEN;
	middle->top = title_top;
	middle->bottom = MIDDLE_BOTTOM;

	put_title(scene->title, title_top, UI_FONT_28, title_color);
	if(scene->title[0] != '\0') middle->top = title_top + ui_font_height(UI_FONT_28) + 4;
	if(scene->note[0] != '\0') middle->bottom = put_note(scene->note, NOTE_BOTTOM, true, NOTE_ROWS) - 6;
}

static void put_notice(const scene_t *scene)
{
	middle_t middle = {.lines = scene->lines, .line_count = within(scene->line_count, 0, SCENE_LINES_MAX)};

	shape = SHAPE_OPEN;
	// The title of a notice says what happened, that of a choice is the question: both are to be read
	put_frame(scene, TITLE_TOP, COLOR_WHITE, &middle);
	put_middle(&middle);
}

// The lines of a progress from `top` on, each with the room of two rows. Returns false if one of them needs
// more; only with `draw` anything is put.
static bool put_slots(const middle_t *middle, int top, bool draw)
{
	int height = ui_font_height(PROGRESS_FONT);

	for(int i = 0; i < middle->line_count; i++)
	{
		char text[UI_TEXT_SIZE];
		int y = top + i * (2 * height + LINE_GAP);

		text_of(text, middle->lines[i], SCENE_TEXT_SIZE);
		if(flow(text, PROGRESS_FONT, y, 0, 2, false) == 0) return false;
		if(draw) put_centred(middle->layer, text, PROGRESS_FONT, y, COLOR_WHITE);
	}
	return true;
}

static void put_progress_screen(const scene_t *scene)
{
	middle_t middle = {.big = scene->big, .lines = scene->lines,
	                   .line_count = within(scene->line_count, 0, SCENE_LINES_MAX)};
	int big_height = ui_font_height(UI_FONT_80);
	int height = big_height + middle.line_count * (2 * ui_font_height(PROGRESS_FONT) + LINE_GAP) - LINE_GAP;
	int top;

	shape = SHAPE_ARC;
	put_progress(UI_LAYER_SCREEN, ARC_RADIUS, ARC_WIDTH, scene->permille);
	put_frame(scene, ARC_TITLE_TOP, COLOR_LABEL, &middle);

	// While a request runs the name of the control unit changes every two seconds, and one name in two
	// rows ("Collision Prevention Assist") must not change the font of all lines or move the line below
	// it: every line has the room of two rows in one font. What does not fit that way is laid out like
	// every other text.
	top = centred(middle.top, middle.bottom, height);
	if(middle.line_count > 0 && height <= middle.bottom - middle.top && put_slots(&middle, top + big_height, false))
	{
		put_big(&middle, top);
		put_slots(&middle, top + big_height, true);
		return;
	}
	put_middle(&middle);
}

static void put_choice(const scene_t *scene)
{
	middle_t middle = {.lines = scene->lines, .line_count = within(scene->line_count, 0, SCENE_LINES_MAX),
	                   .choice = scene};

	// The hold: a ring around the screen that fills while the knob is held
	shape = SHAPE_OPEN;
	if(scene->permille >= 0)
	{
		shape = SHAPE_HOLD;
		put_progress(UI_LAYER_SCREEN, HOLD_RADIUS, HOLD_WIDTH, scene->permille);
	}
	put_frame(scene, CHOICE_TITLE_TOP, COLOR_WHITE, &middle);
	put_middle(&middle);
}

static void put_level(const scene_t *scene)
{
	middle_t middle = {.big = scene->big};

	shape = SHAPE_GAUGE;
	put_gauge(scene->permille, COLOR_WHITE);
	put_frame(scene, ARC_TITLE_TOP, COLOR_LABEL, &middle);
	put_middle(&middle);
}

// What lies over the screen: a dark panel that hides it, with its lines in the middle
static void put_over(const scene_t *scene)
{
	middle_t middle = {.layer = UI_LAYER_OVER, .top = OVER_TOP, .bottom = OVER_BOTTOM, .lines = scene->over_lines,
	                   .line_count = within(scene->over_line_count, 0, 3)};

	if(scene->over == SCENE_OVER_NONE) return;

	ui_put_box(UI_LAYER_OVER, CX - PANEL_RADIUS, CY - PANEL_RADIUS, 2 * PANEL_RADIUS, 2 * PANEL_RADIUS, PANEL_RADIUS,
	           COLOR_PANEL, 0);
	shape = SHAPE_PANEL;
	if(scene->over == SCENE_OVER_UPLOAD)
	{
		shape = SHAPE_ARC;
		put_progress(UI_LAYER_OVER, ARC_RADIUS, ARC_WIDTH, scene->over_permille);
	}
	put_middle(&middle);

	// The rows below cannot be touched through the panel
	hits.count = 0;
}

void ui_layout(const scene_t *scene)
{
	hits.count = 0;
	put_ring(&scene->ring);

	switch(scene->kind)
	{
		case SCENE_VALUES:
			put_values(scene);
			put_dots(scene);
			break;
		case SCENE_NOTICE:
			put_notice(scene);
			put_dots(scene);
			break;
		case SCENE_LIST:
			put_list(scene);
			break;
		case SCENE_PROGRESS:
			put_progress_screen(scene);
			break;
		case SCENE_CHOICE:
			put_choice(scene);
			break;
		case SCENE_LEVEL:
			put_level(scene);
			break;
		default:
			break;
	}
	put_over(scene);
}

int ui_layout_row_at(int x, int y)
{
	for(int i = 0; i < hits.count; i++)
	{
		const area_t *area = &hits.areas[i];

		if(x >= area->x0 && x < area->x1 && y >= area->y0 && y < area->y1) return hits.first + i;
	}
	return -1;
}
