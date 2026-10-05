/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <limits.h>
#include <math.h>
#include "lvgl.h"
#include "ui.h"

/*
 * Draws the screens without a display and looks at what came out. LVGL renders into a buffer of 480 x 480
 * pixels in the memory, with the configuration of the firmware (lv_conf.h). Two kinds of scenes are shown:
 * those filled in by hand below, one for everything that looks different, with the longest texts a scene
 * can hold; and those the core makes, read back from the files its host tests keep (read_scene()).
 *
 *   render [directory [scene.txt ...]]
 *                           writes <name>.png into the directory (default: out) and prints the screen as
 *                           text into the log for every scene made by hand, and for a scene of the core if
 *                           something is wrong with it; ends with status 1 if a check failed, with 2 if a
 *                           file cannot be read
 *
 * For every screen:
 *   - every text has its place within the circle of radius 232, every bar, arc and box within 233; the
 *     ring fills its band from 234 to 240 and reaches nowhere else
 *   - no two texts overlap, and no text lies half on a bar, an arc or a box
 *   - every text stands out from what it stands on, and nothing shows through an overlay
 *   - every text of the scene is on the screen, whole; for the screens marked `may_cut`, which hold more
 *     than a screen can show, every text is at least the beginning of one of the scene, with an ellipsis.
 *     A scene of the core is never one of those. The labels of known_cut are the exception, each by name.
 *   - a list shows the marks for more rows exactly when there are more
 *   - ui_row_at() names the right row at the middle of every row and of both options, and none elsewhere
 *   - the row and the option in focus have the white bar and no other has; an action that is not offered
 *     is grey, every other row white; all actions of all screens have one font
 *   - as many dots as there are pages, and the one of the page shown is filled
 *   - gauge, arc of a request, ring of the hold, arc of an upload and the bars are filled as far as the
 *     scene says, from where they begin; a bar has the colour of the tone of its value
 *   - the value of every item is white, grey, amber or red as its tone says; the ring has its colour
 *   - the title of a value page is set apart from the label of the first value (check_title())
 *   - LVGL did not complain (a character the font does not have, memory)
 *   - the screen looks the same whatever was shown before it: all made by hand are drawn a second time in
 *     reverse order
 * and for the pairs of `steady`: no text changes its font or its height with a value.
 * The parts are looked at one by one: all others are hidden, the screen is rendered and the pixels that
 * are lit are the part. So the checks see what a person sees, not the boxes LVGL reckons with.
 *
 * What it cannot tell: whether a screen reads well and whether its fonts are large enough (only that they
 * do not change), whether a grey is the right grey, whether a value and its unit stand where they should
 * within their place, and how any of it looks on the panel. For that there are the pictures, and in the end
 * the device.
 */

#define SIZE            480
#define CENTRE          240
#define R_TEXT          232             // every text stays 8 px away from the edge of the circle
#define R_SHAPE         233             // bars, arcs and the panel that lies over a screen; the ring begins at 234
#define RING_INSIDE     234
#define RING_OUTSIDE    240
#define CONTRAST        60              // of 255: what the brightest pixel of a text has to differ from its ground
#define ART_COLUMNS     96
#define ART_ROWS        48
#define HEAP_SIZE       (512u * 1024u)  // the pool the firmware takes from the PSRAM, see sdkconfig.defaults

typedef struct
{
	const char *name;
	void (*fill)(scene_t *scene);
	bool may_cut;
} screen_t;

/*
 * The scenes
 */

#define PUT(field, text)    put(field, sizeof(field), text)

static void put(char *field, size_t size, const char *text)
{
	size_t length = strlen(text);

	if(length >= size)
	{
		fprintf(stderr, "render.c: \"%s\" is too long for its field of %zu bytes\n", text, size);
		exit(2);
	}
	memcpy(field, text, length + 1);
}

#define LONGEST(field)      longest(field, sizeof(field))

static int longest_count;   // texts longest() made for the scene that is being filled

// The longest text a field holds, with umlauts and without a blank at its end. Each one of a scene begins
// with another letter, so that the checks can tell which text a label shows.
static void longest(char *field, size_t size)
{
	static const char words[] = "Überlänge größtmöglich für Kühlmittelüberwachung ";
	size_t length = 0;

	field[length++] = (char)('A' + longest_count++ % 26);
	field[length++] = ' ';
	while(length + 1 < size)
	{
		field[length] = words[(length - 2) % (sizeof(words) - 1)];
		length++;
	}
	// Not half a character at the end
	while(length > 0 && ((unsigned char)field[length - 1] & 0xC0) == 0x80) length--;
	if(length > 0 && (unsigned char)field[length - 1] >= 0xC0) length--;
	while(length > 0 && field[length - 1] == ' ') length--;
	field[length] = '\0';
}

// As scene_build() leaves what a scene does not use
static void fresh(scene_t *scene, scene_kind_t kind, const char *title)
{
	memset(scene, 0, sizeof(*scene));
	longest_count = 0;
	scene->kind = kind;
	PUT(scene->title, title);
	scene->dot = -1;
	scene->permille = -1;
	scene->over_permille = -1;
}

static void ring(scene_t *scene, ring_kind_t kind, int permille)
{
	scene->ring.kind = kind;
	scene->ring.permille = permille;
}

static void dots(scene_t *scene, int dot, int count)
{
	scene->dot = dot;
	scene->dots = count;
}

static void item(scene_t *scene, const char *label, const char *text, const char *unit, scene_tone_t tone,
                 layout_widget_t widget, int permille)
{
	scene_item_t *target = &scene->items[scene->item_count++];

	PUT(target->label, label);
	PUT(target->text, text);
	PUT(target->unit, unit);
	target->tone = tone;
	target->widget = widget;
	target->permille = permille;
}

static void row(scene_t *scene, scene_row_kind_t kind, const char *text, const char *detail, bool enabled, bool focus)
{
	scene_row_t *target = &scene->rows[scene->row_count++];

	target->kind = kind;
	PUT(target->text, text);
	PUT(target->detail, detail);
	target->enabled = enabled;
	target->focus = focus;
}

static void window(scene_t *scene, int first, int total)
{
	scene->first = first;
	scene->total = total;
}

static void line(scene_t *scene, const char *text)
{
	PUT(scene->lines[scene->line_count], text);
	scene->line_count++;
}

static void options(scene_t *scene, const char *first, const char *second, int focus, int permille)
{
	PUT(scene->options[0], first);
	PUT(scene->options[1], second);
	scene->option = focus;
	scene->permille = permille;
}

static void over(scene_t *scene, scene_over_t kind, int permille, const char *first, const char *second,
                 const char *third)
{
	const char *lines[3] = {first, second, third};

	scene->over = kind;
	scene->over_permille = permille;
	for(int i = 0; i < 3 && lines[i] != NULL; i++)
	{
		PUT(scene->over_lines[i], lines[i]);
		scene->over_line_count++;
	}
}

static void motor(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Motor");
	dots(scene, 0, 7);
	item(scene, "Drehzahl", "812", "1/min", SCENE_TONE_NORMAL, LAYOUT_WIDGET_ARC, 162);
	item(scene, "Kühlwasser", "88", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Ladedruck", "1,01", "bar", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Fahrpedal", "13", "%", SCENE_TONE_NORMAL, LAYOUT_WIDGET_BAR, 125);
	item(scene, "Regeneration", "inaktiv", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_STATE, -1);
	item(scene, "Bordnetz", "14,1", "V", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
}

static void values_one_arc(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Kühlwasser");
	dots(scene, 1, 7);
	item(scene, "Kühlwasser", "88", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_ARC, 537);
}

static void values_one_arc_warn(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Drehzahl");
	ring(scene, RING_YELLOW, 0);
	dots(scene, 1, 7);
	item(scene, "Drehzahl", "4000", "1/min", SCENE_TONE_WARN, LAYOUT_WIDGET_ARC, 800);
}

static void values_one_arc_dash(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Kühlwasser");
	dots(scene, 1, 7);
	item(scene, "Kühlwasser", SCENE_DASH, "", SCENE_TONE_DIM, LAYOUT_WIDGET_ARC, -1);
}

static void values_one_bar(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Tank");
	dots(scene, 6, 7);
	item(scene, "Tankinhalt", "43", "L", SCENE_TONE_NORMAL, LAYOUT_WIDGET_BAR, 573);
}

static void values_one_alarm(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Abgas");
	ring(scene, RING_RED, 0);
	dots(scene, 2, 7);
	item(scene, "Abgas vor Turbo", "812", "°C", SCENE_TONE_ALARM, LAYOUT_WIDGET_NUMBER, -1);
}

static void values_one_state(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "DPF");
	dots(scene, 3, 7);
	item(scene, "Regeneration", "inaktiv", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_STATE, -1);
}

static void values_one_unavailable(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Getriebe");
	item(scene, "Getriebeöl", SCENE_UNAVAILABLE, "", SCENE_TONE_DIM, LAYOUT_WIDGET_NUMBER, -1);
}

// A value too wide for the largest font
static void values_one_wide(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Sonstiges");
	dots(scene, 6, 7);
	item(scene, "Wegstrecke Motor-SG", "187436", "km", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
}

static void values_two(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Sonstiges");
	dots(scene, 6, 7);
	item(scene, "Ölstand", "61,3", "mm", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Tankinhalt", "43", "L", SCENE_TONE_WARN, LAYOUT_WIDGET_BAR, 573);
}

static void values_three(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Öl");
	dots(scene, 3, 7);
	item(scene, "Ölstand", "61,3", "mm", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Lambda", "1,34", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Motoröl", "94", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
}

static void values_four(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Motor");
	dots(scene, 0, 7);
	item(scene, "Drehzahl", "812", "1/min", SCENE_TONE_NORMAL, LAYOUT_WIDGET_ARC, 162);
	item(scene, "Kühlwasser", "88", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Motoröl", "94", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Fahrpedal", "13", "%", SCENE_TONE_NORMAL, LAYOUT_WIDGET_BAR, 125);
}

// The same page at speed: a digit more, and nothing else may change (check_steady())
static void values_four_fast(scene_t *scene)
{
	values_four(scene);
	PUT(scene->items[0].text, "2150");
	scene->items[0].permille = 430;
	PUT(scene->items[3].text, "100");
	scene->items[3].permille = 1000;
}

static void values_five(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Abgas");
	dots(scene, 2, 7);
	item(scene, "Abgas vor Turbo", "412", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Abgas vor DPF", "289", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Getriebeöl", SCENE_UNAVAILABLE, "", SCENE_TONE_DIM, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Ruß", "11,3", "g", SCENE_TONE_NORMAL, LAYOUT_WIDGET_BAR, 282);
	item(scene, "Vorglühen", "aus", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_STATE, -1);
}

static void values_six(scene_t *scene)
{
	motor(scene);
}

// The same above 1000 1/min. With its unit a number of four digits needs a smaller font in this place than
// one of three: the place has that font from the start (check_steady())
static void values_six_fast(scene_t *scene)
{
	motor(scene);
	PUT(scene->items[0].text, "1012");
	scene->items[0].permille = 202;
}

static void values_six_alarm(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Motor");
	ring(scene, RING_RED, 0);
	dots(scene, 0, 7);
	item(scene, "Drehzahl", "4500", "1/min", SCENE_TONE_ALARM, LAYOUT_WIDGET_ARC, 900);
	item(scene, "Kühlwasser", "115", "°C", SCENE_TONE_ALARM, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Ladedruck", "1,01", "bar", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Fahrpedal", "120", "%", SCENE_TONE_NORMAL, LAYOUT_WIDGET_BAR, 1000);
	item(scene, "Regeneration", "aktiv", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_STATE, -1);
	item(scene, "Bordnetz", "11,8", "V", SCENE_TONE_WARN, LAYOUT_WIDGET_NUMBER, -1);
}

// Values that are old, gone and not provided by the profile
static void values_six_old(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Motor");
	ring(scene, RING_YELLOW, 0);
	dots(scene, 0, 7);
	item(scene, "Drehzahl", "812", "1/min", SCENE_TONE_NORMAL, LAYOUT_WIDGET_ARC, 162);
	item(scene, "Kühlwasser", "88", "°C", SCENE_TONE_DIM, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Ladedruck", SCENE_DASH, "", SCENE_TONE_DIM, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Fahrpedal", "13", "%", SCENE_TONE_DIM, LAYOUT_WIDGET_BAR, 125);
	item(scene, "Regeneration", SCENE_DASH, "", SCENE_TONE_DIM, LAYOUT_WIDGET_STATE, -1);
	item(scene, "Getriebeöl", SCENE_UNAVAILABLE, "", SCENE_TONE_DIM, LAYOUT_WIDGET_NUMBER, -1);
}

static void values_six_scan(scene_t *scene)
{
	motor(scene);
	ring(scene, RING_PROGRESS, 277);
	PUT(scene->note, "Live-Werte angehalten (Fehlerspeicher-Scan)");
	for(int i = 0; i < scene->item_count; i++) scene->items[i].tone = SCENE_TONE_DIM;
}

// The longest note of a value page: three lines
static void values_six_no_api(scene_t *scene)
{
	motor(scene);
	ring(scene, RING_GREY, 0);
	PUT(scene->note, "WiCAN-Firmware ohne Display-API – nur Live-Werte");
}

static void values_one_safe_mode(scene_t *scene)
{
	values_one_arc(scene);
	PUT(scene->note, "Sicherer Modus – eingebaute Ansichten");
}

static void values_four_hot(scene_t *scene)
{
	values_four(scene);
	PUT(scene->note, "Zu heiß – Anzeige gedimmt");
}

// The pages of the built-in layout (display/layouts/w906_default.json) with six and with five values, as
// they are: the labels are those of the file
static void page_ladeluft(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Ladeluft");
	dots(scene, 1, 7);
	item(scene, "Ladedruck", "1013", "hPa", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Ladedruck ND", "1004", "hPa", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Luft vor LLK", "64", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Luft nach LLK", "31", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Ansaugluft", "24", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Wastegate", "37", "%", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
}

static void page_abgas(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Abgas");
	dots(scene, 2, 7);
	item(scene, "vor Turbo", "412", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "AGR-Kühler", "118", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "vor Kat", "301", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "vor DPF", "289", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "vor SCR", "244", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Gegendruck", "1087", "hPa", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
}

static void page_dpf(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "DPF");
	dots(scene, 3, 7);
	item(scene, "Ruß gemessen", "11,3", "g", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Ruß berechnet", "12,8", "g", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Asche", "31,0", "g", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Differenzdruck", "14", "hPa", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "km seit Reg.", "412", "km", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Regeneration", "inaktiv", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_STATE, -1);
}

static void page_kraftstoff(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Kraftstoff");
	dots(scene, 4, 7);
	item(scene, "Raildruck", "312", "bar", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Einspritzmenge", "8,4", "mg", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Temperatur", "38", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Tankinhalt", "43", "L", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Lambda", "1,34", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Luft je Hub", "478", "mg", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
}

// Two pressures of four digits in the lowest row, the narrowest
static void page_agr(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "AGR/Luft");
	dots(scene, 5, 7);
	item(scene, "AGR-Rate", "23", "%", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "AGR-Ventil", "31", "%", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Drosselklappe", "88", "%", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Ansaugdruck", "1004", "hPa", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Luftdruck", "1013", "hPa", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
}

// The same with one digit fewer: nothing but the digit may change, see check_steady()
static void page_agr_low(scene_t *scene)
{
	page_agr(scene);
	PUT(scene->items[3].text, "999");
	PUT(scene->items[4].text, "998");
}

static void values_longest(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "");
	LONGEST(scene->title);
	LONGEST(scene->note);
	ring(scene, RING_PROGRESS, 1000);
	dots(scene, 11, 12);
	for(int i = 0; i < LAYOUT_ITEMS_MAX; i++)
	{
		item(scene, "", "", "", (scene_tone_t)(i % 4), (layout_widget_t)(i % 4), i * 200);
		LONGEST(scene->items[i].label);
		LONGEST(scene->items[i].text);
		LONGEST(scene->items[i].unit);
	}
}

static void values_one_longest(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "");
	LONGEST(scene->title);
	LONGEST(scene->note);
	dots(scene, 0, 12);
	item(scene, "", "999999999999,999", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_ARC, 1000);
	LONGEST(scene->items[0].label);
	LONGEST(scene->items[0].unit);
}

static void notice_no_wifi(scene_t *scene)
{
	fresh(scene, SCENE_NOTICE, "");
	ring(scene, RING_RED, 0);
	dots(scene, 0, 7);
	line(scene, "WiCAN nicht gefunden – schläft, stromlos oder außer Reichweite");
}

static void notice_connecting(scene_t *scene)
{
	fresh(scene, SCENE_NOTICE, "");
	ring(scene, RING_YELLOW, 0);
	dots(scene, 0, 7);
	line(scene, "Verbinde mit WiCAN …");
}

static void notice_ecu_offline(scene_t *scene)
{
	fresh(scene, SCENE_NOTICE, "");
	ring(scene, RING_GREY, 0);
	dots(scene, 2, 7);
	line(scene, "Zündung aus – Motorsteuergerät offline");
	line(scene, "Bordnetz 12,4 V");
}

static void notice_no_page(scene_t *scene)
{
	fresh(scene, SCENE_NOTICE, "");
	dots(scene, -1, 7);
	line(scene, "Keine Ansicht mit verfügbaren Werten");
	PUT(scene->note, "Sicherer Modus – eingebaute Ansichten");
}

static void notice_hot(scene_t *scene)
{
	fresh(scene, SCENE_NOTICE, "");
	ring(scene, RING_GREY, 0);
	dots(scene, 0, 7);
	line(scene, "AutoPID nicht aktiv");
	PUT(scene->note, "Zu heiß – Anzeige gedimmt");
}

static void notice_failed(scene_t *scene)
{
	fresh(scene, SCENE_NOTICE, "Fehlgeschlagen");
	line(scene, "Motor läuft – nur bei Motor aus");
	PUT(scene->note, "Knopf drücken");
}

static void notice_unknown(scene_t *scene)
{
	fresh(scene, SCENE_NOTICE, "Stand unbekannt");
	line(scene, "Stand des Löschens unbekannt – bitte erneut lesen");
	PUT(scene->note, "Knopf drücken");
}

static void notice_empty(scene_t *scene)
{
	fresh(scene, SCENE_NOTICE, "");
}

static void notice_longest(scene_t *scene)
{
	fresh(scene, SCENE_NOTICE, "");
	LONGEST(scene->title);
	LONGEST(scene->note);
	dots(scene, 5, 12);
	for(int i = 0; i < SCENE_LINES_MAX; i++)
	{
		line(scene, "");
		LONGEST(scene->lines[i]);
	}
}

static void list_menu_top(scene_t *scene)
{
	fresh(scene, SCENE_LIST, "Menü");
	window(scene, 0, 7);
	row(scene, SCENE_ROW_ACTION, "Fehlerspeicher", "", true, true);
	row(scene, SCENE_ROW_ACTION, "Helligkeit", "80 %", true, false);
	row(scene, SCENE_ROW_ACTION, "Nachtmodus", "aus", true, false);
	row(scene, SCENE_ROW_ACTION, "Web-Zugriff", "gesperrt", true, false);
	row(scene, SCENE_ROW_ACTION, "Info", "", true, false);
}

static void list_menu_end(scene_t *scene)
{
	fresh(scene, SCENE_LIST, "Menü");
	window(scene, 2, 7);
	row(scene, SCENE_ROW_ACTION, "Nachtmodus", "an", true, false);
	row(scene, SCENE_ROW_ACTION, "Web-Zugriff", "frei", true, false);
	row(scene, SCENE_ROW_ACTION, "Info", "", true, false);
	row(scene, SCENE_ROW_ACTION, "Einstellungen", "", true, false);
	row(scene, SCENE_ROW_ACTION, "Zurück", "", true, true);
}

// The start of the fault memory: a line that says where things stand, and four rows
static void dtc_start(scene_t *scene, const char *state, bool read, bool list, int focus, const char *note)
{
	fresh(scene, SCENE_LIST, "Fehlerspeicher");
	window(scene, 0, 4);
	line(scene, state);
	row(scene, SCENE_ROW_ACTION, "Lesen", "", read, focus == 0);
	row(scene, SCENE_ROW_ACTION, "Liste ansehen", "", list, focus == 1);
	row(scene, SCENE_ROW_ACTION, "Liste vor dem Löschen", "", true, focus == 2);
	row(scene, SCENE_ROW_ACTION, "Zurück", "", true, focus == 3);
	PUT(scene->note, note);
}

// The focus on an action that is not offered, and the longest reason for it
static void list_dtc_blocked(scene_t *scene)
{
	dtc_start(scene, "Noch nicht gelesen", false, false, 0, "Zündung aus – Motorsteuergerät offline");
}

// The lines too long for the place above four rows: they take two rows of text
static void list_dtc_failed(scene_t *scene)
{
	dtc_start(scene, "Letzter Auftrag fehlgeschlagen", true, true, 1, "");
}

static void list_dtc_unknown(scene_t *scene)
{
	dtc_start(scene, "Stand des Löschens unbekannt", false, true, 1, "Motor läuft – nur bei Motor aus");
}

static void list_dtc_many(scene_t *scene)
{
	dtc_start(scene, "165 Fehler in 18 Steuergeräten", true, true, 2, "");
}

static void list_dtc_top(scene_t *scene)
{
	fresh(scene, SCENE_LIST, "Fehlerspeicher");
	window(scene, 0, 20);
	row(scene, SCENE_ROW_HEAD, "10 Fehler", "18 Steuergeräte · 36 s", true, true);
	row(scene, SCENE_ROW_LINE, "Motorelektronik", "7E0 · 6 Fehler", true, false);
	row(scene, SCENE_ROW_SUB, "P0100-13", "aktiv", true, false);
	row(scene, SCENE_ROW_SUB, "P242F-FA", "gespeichert", true, false);
	row(scene, SCENE_ROW_LINE, "4 Codes nicht übertragen", "", true, false);
	PUT(scene->note, "Löschen möglich: 9:12");
}

static void list_dtc_middle(scene_t *scene)
{
	fresh(scene, SCENE_LIST, "Fehlerspeicher");
	window(scene, 5, 20);
	row(scene, SCENE_ROW_LINE, "Rückhaltesystem", "6BC · 1 Fehler", true, false);
	row(scene, SCENE_ROW_SUB, "9301", "Status 60", true, false);
	row(scene, SCENE_ROW_LINE, "Elektronisches Zündschloss", "abgelehnt (NRC 22)", true, true);
	row(scene, SCENE_ROW_LINE, "Collision Prevention Assist", "keine Antwort", true, false);
	row(scene, SCENE_ROW_LINE, "15 Steuergeräte ohne Fehler", "", true, false);
	PUT(scene->note, "Zündung aus – Motorsteuergerät offline");
}

static void list_dtc_end(scene_t *scene)
{
	fresh(scene, SCENE_LIST, "Fehlerspeicher");
	window(scene, 15, 20);
	row(scene, SCENE_ROW_LINE, "Radio", "unbekannter Status", true, false);
	row(scene, SCENE_ROW_LINE, "1 Steuergerät ohne Fehler", "", true, false);
	row(scene, SCENE_ROW_ACTION, "Erneut lesen", "", true, false);
	row(scene, SCENE_ROW_ACTION, "Fehler löschen", "", false, false);
	row(scene, SCENE_ROW_ACTION, "Zurück", "", true, true);
	PUT(scene->note, "Liste veraltet – erneut lesen");
}

static void list_cleared(scene_t *scene)
{
	fresh(scene, SCENE_LIST, "Gelöscht");
	window(scene, 0, 8);
	row(scene, SCENE_ROW_HEAD, "Gelöscht 3 von 5", "verbleibend 2", true, false);
	row(scene, SCENE_ROW_LINE, "ESP", "Löschen nicht bestätigt", true, true);
	row(scene, SCENE_ROW_LINE, "Getriebesteuerung", "7E1 · 2 Fehler", true, false);
	row(scene, SCENE_ROW_SUB, "P0715-00", "aktiv", true, false);
	row(scene, SCENE_ROW_SUB, "P2767-64", "gespeichert", true, false);
}

static void list_old(scene_t *scene)
{
	fresh(scene, SCENE_LIST, "Vor dem Löschen");
	window(scene, 9, 14);
	row(scene, SCENE_ROW_LINE, "ESP", "784 · 1 Fehler", true, false);
	row(scene, SCENE_ROW_SUB, "U0100-87", "gespeichert", true, false);
	row(scene, SCENE_ROW_LINE, "ESP", "Antwort ausstehend", true, false);
	row(scene, SCENE_ROW_LINE, "Liste unvollständig", "", true, false);
	row(scene, SCENE_ROW_ACTION, "Zurück", "", true, true);
}

static void list_web(scene_t *scene)
{
	fresh(scene, SCENE_LIST, "Web-Zugriff");
	window(scene, 0, 2);
	line(scene, "http://192.168.4.1");
	line(scene, "WLAN: WiCAN-Display");
	line(scene, "Passwort: geheim1234");
	row(scene, SCENE_ROW_ACTION, "Freigabe", "an – noch 9:12", true, true);
	row(scene, SCENE_ROW_ACTION, "Zurück", "", true, false);
}

// The same at the moment the release begins: the longest detail of all rows (check_steady())
static void list_web_begun(scene_t *scene)
{
	list_web(scene);
	PUT(scene->rows[0].detail, "an – noch 10:00");
}

// Lines without a detail that are too long for one line
static void list_info(scene_t *scene)
{
	fresh(scene, SCENE_LIST, "Info");
	window(scene, 0, 7);
	row(scene, SCENE_ROW_LINE, "WLAN: Werkstatt (-61 dBm)", "", true, true);
	row(scene, SCENE_ROW_LINE, "Adresse: 192.168.88.37", "", true, false);
	row(scene, SCENE_ROW_LINE, "WiCAN: a1b2c3d4e5f6", "", true, false);
	row(scene, SCENE_ROW_LINE, "Firmware: w906-display 0.1", "", true, false);
	row(scene, SCENE_ROW_LINE, "Ansichten: W906 OM651 Standard", "", true, false);
}

static void list_settings(scene_t *scene)
{
	fresh(scene, SCENE_LIST, "Einstellungen");
	window(scene, 0, 6);
	row(scene, SCENE_ROW_ACTION, "Drehrichtung", "umgekehrt", true, false);
	row(scene, SCENE_ROW_ACTION, "Hotspot", "aus", true, false);
	row(scene, SCENE_ROW_ACTION, "Neustart", "", true, false);
	row(scene, SCENE_ROW_ACTION, "Vorherige Version", "", false, false);
	row(scene, SCENE_ROW_ACTION, "Werkseinstellungen", "", true, true);
}

// The same after a press on the first row: a shorter detail (check_steady())
static void list_settings_normal(scene_t *scene)
{
	list_settings(scene);
	PUT(scene->rows[0].detail, "normal");
}

static void list_empty(scene_t *scene)
{
	fresh(scene, SCENE_LIST, "Info");
}

static void list_longest(scene_t *scene)
{
	static const scene_row_kind_t kinds[SCENE_ROWS_MAX] =
	{
		SCENE_ROW_HEAD, SCENE_ROW_LINE, SCENE_ROW_SUB, SCENE_ROW_ACTION, SCENE_ROW_ACTION,
	};

	fresh(scene, SCENE_LIST, "");
	LONGEST(scene->title);
	LONGEST(scene->note);
	window(scene, 3, 99);
	for(int i = 0; i < SCENE_LINES_MAX; i++)
	{
		line(scene, "");
		LONGEST(scene->lines[i]);
	}
	for(int i = 0; i < SCENE_ROWS_MAX; i++)
	{
		row(scene, kinds[i], "", "", i != 3, i == 2);
		LONGEST(scene->rows[i].text);
		LONGEST(scene->rows[i].detail);
	}
}

static void progress_sent(scene_t *scene)
{
	fresh(scene, SCENE_PROGRESS, "Fehlerspeicher lesen");
	PUT(scene->big, "…");
	scene->permille = 0;
	line(scene, "Auftrag gesendet");
	line(scene, "ca. 35 s – Live-Werte pausieren");
}

// Never with the ring of the scan: the screen has an arc of its own (scene.h)
static void progress_running(scene_t *scene)
{
	fresh(scene, SCENE_PROGRESS, "Fehlerspeicher löschen");
	PUT(scene->big, "5/18");
	scene->permille = 277;
	line(scene, "Collision Prevention Assist");
	line(scene, "ca. 35 s – Live-Werte pausieren");
}

// The same with a name that fits one row: nothing else may change (check_steady())
static void progress_running_short(scene_t *scene)
{
	progress_running(scene);
	PUT(scene->lines[0], "Motorelektronik");
}

static void progress_done(scene_t *scene)
{
	fresh(scene, SCENE_PROGRESS, "Fehlerspeicher lesen");
	PUT(scene->big, "18/18");
	scene->permille = 1000;
	line(scene, "Radio");
	line(scene, "ca. 35 s – Live-Werte pausieren");
}

static void progress_longest(scene_t *scene)
{
	fresh(scene, SCENE_PROGRESS, "");
	LONGEST(scene->title);
	LONGEST(scene->note);
	LONGEST(scene->big);
	scene->permille = 500;
	for(int i = 0; i < SCENE_LINES_MAX; i++)
	{
		line(scene, "");
		LONGEST(scene->lines[i]);
	}
}

static void clear_dialog(scene_t *scene, int focus, int permille)
{
	fresh(scene, SCENE_CHOICE, "Fehler löschen?");
	line(scene, "10 Fehler in 3 Steuergeräten");
	line(scene, "Betrifft alle Steuergeräte, auch SRS und ESP.");
	line(scene, "Zündung an, Motor aus, Fahrzeug steht.");
	options(scene, "Abbrechen", "Löschen", focus, permille);
	PUT(scene->note, "Auf Löschen drehen, Knopf 3 s halten");
}

static void choice_clear(scene_t *scene)
{
	clear_dialog(scene, 0, 0);
}

static void choice_clear_hold(scene_t *scene)
{
	clear_dialog(scene, 1, 500);
}

static void choice_clear_held(scene_t *scene)
{
	clear_dialog(scene, 1, 1000);
}

static void choice_reboot(scene_t *scene)
{
	fresh(scene, SCENE_CHOICE, "Neu starten?");
	options(scene, "Abbrechen", "Ausführen", 0, -1);
}

static void choice_previous(scene_t *scene)
{
	fresh(scene, SCENE_CHOICE, "Vorherige Version starten?");
	options(scene, "Abbrechen", "Ausführen", 1, -1);
}

static void choice_reset(scene_t *scene)
{
	fresh(scene, SCENE_CHOICE, "Werkseinstellungen?");
	line(scene, "WLAN, Kopplung und Einstellungen werden gelöscht.");
	line(scene, "Die Ansichten bleiben.");
	options(scene, "Abbrechen", "Ausführen", 0, -1);
}

static void choice_longest(scene_t *scene)
{
	fresh(scene, SCENE_CHOICE, "");
	LONGEST(scene->title);
	LONGEST(scene->note);
	options(scene, "", "", 1, 333);
	LONGEST(scene->options[0]);
	LONGEST(scene->options[1]);
	for(int i = 0; i < SCENE_LINES_MAX; i++)
	{
		line(scene, "");
		LONGEST(scene->lines[i]);
	}
}

static void level_day(scene_t *scene)
{
	fresh(scene, SCENE_LEVEL, "Helligkeit");
	PUT(scene->big, "80 %");
	scene->permille = 800;
	PUT(scene->note, "Drehen zum Ändern, Drücken zum Speichern");
}

static void level_night(scene_t *scene)
{
	fresh(scene, SCENE_LEVEL, "Helligkeit (Nacht)");
	PUT(scene->big, "5 %");
	scene->permille = 50;
	PUT(scene->note, "Drehen zum Ändern, Drücken zum Speichern");
}

static void level_full(scene_t *scene)
{
	fresh(scene, SCENE_LEVEL, "Helligkeit");
	PUT(scene->big, "100 %");
	scene->permille = 1000;
	PUT(scene->note, "Drehen zum Ändern, Drücken zum Speichern");
}

static void level_longest(scene_t *scene)
{
	fresh(scene, SCENE_LEVEL, "");
	LONGEST(scene->title);
	LONGEST(scene->note);
	LONGEST(scene->big);
	scene->permille = 500;
}

static void over_upload(scene_t *scene)
{
	motor(scene);
	over(scene, SCENE_OVER_UPLOAD, 420, "Firmware wird übertragen", "42 %", NULL);
}

static void over_upload_begun(scene_t *scene)
{
	list_menu_top(scene);
	over(scene, SCENE_OVER_UPLOAD, 0, "Firmware wird übertragen", "0 %", NULL);
}

static void over_ask_wifi(scene_t *scene)
{
	list_menu_top(scene);
	over(scene, SCENE_OVER_ASK, -1, "WLAN speichern?", "Werkstatt", "Drücken = ja · lang = nein (42 s)");
}

static void over_ask_firmware(scene_t *scene)
{
	clear_dialog(scene, 0, 0);
	over(scene, SCENE_OVER_ASK, -1, "Firmware installieren?", "w906-display 0.2",
	     "Drücken = ja · lang = nein (7 s)");
}

static void over_ask_reset(scene_t *scene)
{
	values_one_arc(scene);
	ring(scene, RING_RED, 0);
	over(scene, SCENE_OVER_ASK, -1, "Werkseinstellungen?", "Drücken = ja · lang = nein (60 s)", NULL);
}

static void over_update(scene_t *scene)
{
	motor(scene);
	over(scene, SCENE_OVER_UPDATE, -1, "Update in Ordnung?", "Knopf drücken oder Bildschirm berühren",
	     "sonst alte Version in 4:12");
}

static void over_longest(scene_t *scene)
{
	list_longest(scene);
	over(scene, SCENE_OVER_UPLOAD, 999, "", "", "");
	for(int i = 0; i < 3; i++) LONGEST(scene->over_lines[i]);
}

static const screen_t screens[] =
{
	{"values_one_arc", values_one_arc, false},
	{"values_one_arc_warn", values_one_arc_warn, false},
	{"values_one_arc_dash", values_one_arc_dash, false},
	{"values_one_bar", values_one_bar, false},
	{"values_one_alarm", values_one_alarm, false},
	{"values_one_state", values_one_state, false},
	{"values_one_unavailable", values_one_unavailable, false},
	{"values_one_wide", values_one_wide, false},
	{"values_one_safe_mode", values_one_safe_mode, false},
	{"values_two", values_two, false},
	{"values_three", values_three, false},
	{"values_four", values_four, false},
	{"values_four_fast", values_four_fast, false},
	{"values_four_hot", values_four_hot, false},
	{"values_five", values_five, false},
	{"values_six", values_six, false},
	{"values_six_fast", values_six_fast, false},
	{"values_six_alarm", values_six_alarm, false},
	{"values_six_old", values_six_old, false},
	{"values_six_scan", values_six_scan, false},
	{"values_six_no_api", values_six_no_api, false},
	{"page_ladeluft", page_ladeluft, false},
	{"page_abgas", page_abgas, false},
	{"page_dpf", page_dpf, false},
	{"page_kraftstoff", page_kraftstoff, false},
	{"page_agr", page_agr, false},
	{"page_agr_low", page_agr_low, false},
	{"values_longest", values_longest, true},
	{"values_one_longest", values_one_longest, true},
	{"notice_no_wifi", notice_no_wifi, false},
	{"notice_connecting", notice_connecting, false},
	{"notice_ecu_offline", notice_ecu_offline, false},
	{"notice_no_page", notice_no_page, false},
	{"notice_hot", notice_hot, false},
	{"notice_failed", notice_failed, false},
	{"notice_unknown", notice_unknown, false},
	{"notice_empty", notice_empty, false},
	{"notice_longest", notice_longest, true},
	{"list_menu_top", list_menu_top, false},
	{"list_menu_end", list_menu_end, false},
	{"list_dtc_blocked", list_dtc_blocked, false},
	{"list_dtc_failed", list_dtc_failed, false},
	{"list_dtc_unknown", list_dtc_unknown, false},
	{"list_dtc_many", list_dtc_many, false},
	{"list_dtc_top", list_dtc_top, false},
	{"list_dtc_middle", list_dtc_middle, false},
	{"list_dtc_end", list_dtc_end, false},
	{"list_cleared", list_cleared, false},
	{"list_old", list_old, false},
	{"list_web", list_web, false},
	{"list_web_begun", list_web_begun, false},
	{"list_info", list_info, false},
	{"list_settings", list_settings, false},
	{"list_settings_normal", list_settings_normal, false},
	{"list_empty", list_empty, false},
	{"list_longest", list_longest, true},
	{"progress_sent", progress_sent, false},
	{"progress_running", progress_running, false},
	{"progress_running_short", progress_running_short, false},
	{"progress_done", progress_done, false},
	{"progress_longest", progress_longest, true},
	{"choice_clear", choice_clear, false},
	{"choice_clear_hold", choice_clear_hold, false},
	{"choice_clear_held", choice_clear_held, false},
	{"choice_reboot", choice_reboot, false},
	{"choice_previous", choice_previous, false},
	{"choice_reset", choice_reset, false},
	{"choice_longest", choice_longest, true},
	{"level_day", level_day, false},
	{"level_night", level_night, false},
	{"level_full", level_full, false},
	{"level_longest", level_longest, true},
	{"over_upload", over_upload, false},
	{"over_upload_begun", over_upload_begun, false},
	{"over_ask_wifi", over_ask_wifi, false},
	{"over_ask_firmware", over_ask_firmware, false},
	{"over_ask_reset", over_ask_reset, false},
	{"over_update", over_update, false},
	{"over_longest", over_longest, true},
};

#define SCREENS     ((int)(sizeof(screens) / sizeof(screens[0])))

// Screens that differ only in what changes while somebody looks at them: a value with a digit more, the
// detail of a row, the name of the control unit that is being read. Every text of the one has the font and
// the height on the screen it has in the other.
static const char *const steady[][2] =
{
	{"values_four", "values_four_fast"},
	{"values_six", "values_six_fast"},
	{"page_agr", "page_agr_low"},
	{"list_settings", "list_settings_normal"},
	{"list_web", "list_web_begun"},
	{"progress_running", "progress_running_short"},
};

// Labels that are too wide for their place and are cut on the device, named so that the run can stay strict
// about every other text. A label of this list that is no longer cut fails the run: the list cannot
// outlive its reason. Empty since the labels of the built-in layout were shortened for the pages with six
// values (188 px in the first and the last row, 222 px in the middle one, at 24 px); the first entry only
// keeps the array from being empty and matches no screen.
static const struct
{
	const char *screen;
	const char *text;
} known_cut[] =
{
	{"", ""},
};

/*
 * The scenes of the core: the files display/test/fixtures/scene_*.txt and app_*.txt hold what scene_build()
 * makes, written by scene_dump(), and the host tests of the core compare them byte for byte. Read back
 * here, they are the screens the device really shows, with the words the core has today.
 */

#define COUNT(a)    ((int)(sizeof(a) / sizeof((a)[0])))

// The words of scene_dump() (scene.h): it takes them by the number of the member, so this is their order
static const char *const kind_words[] = {"values", "notice", "list", "progress", "choice", "level"};
static const char *const ring_words[] = {"none", "yellow", "grey", "red", "progress"};
static const char *const tone_words[] = {"normal", "dim", "warn", "alarm"};
static const char *const widget_words[] = {"number", "arc", "bar", "state"};
static const char *const row_words[] = {"action", "head", "line", "sub"};
static const char *const over_words[] = {"none", "upload", "ask", "update"};

static const char *dump_path;   // the file that is being read, and the line in it
static int dump_line;

// A file this cannot read ends the run: a screen that is skipped would be a screen nobody looked at
static void refuse(const char *what)
{
	fprintf(stderr, "render.c: %s, line %d: %s\n", dump_path, dump_line, what);
	exit(2);
}

// What stands behind a name and its blank, NULL if the line begins with another name. An empty text has no
// blank behind the colon.
static char *behind(char *text, const char *name)
{
	size_t length = strlen(name);

	if(strncmp(text, name, length) != 0) return NULL;
	if(text[length] == '\0') return text + length;
	return text[length] == ' ' ? text + length + 1 : NULL;
}

static int number_of(const char *text)
{
	char *end;
	long value = strtol(text, &end, 10);

	if(end == text || *end != '\0' || value < INT_MIN || value > INT_MAX) refuse("no number where one has to be");
	return (int)value;
}

static int word_of(const char *text, const char *const *words, int count)
{
	for(int i = 0; i < count; i++)
	{
		if(strcmp(text, words[i]) == 0) return i;
	}
	refuse("a word scene_dump() does not have (\"?\" stands for a member that is none)");
	return 0;
}

// Cuts a line into its `count` fields at every " | ". A text that has " | " in itself cannot be told from
// two fields, and is refused.
static void split(char *text, char *fields[], int count)
{
	for(int i = 0; i < count; i++)
	{
		char *bar = strstr(text, " | ");

		fields[i] = text;
		if(i == count - 1)
		{
			if(bar != NULL) refuse("more fields than the line has");
			return;
		}
		if(bar == NULL) refuse("fewer fields than the line has");
		*bar = '\0';
		text = bar + 3;
	}
}

#define TAKE(field, text)   take(field, sizeof(field), text)

static void take(char *field, size_t size, const char *text)
{
	if(strlen(text) >= size) refuse("a text too long for its field");
	strcpy(field, text);
}

static void read_scene(const char *path, scene_t *scene)
{
	char text[512];
	char *fields[6], *rest;
	int options_read = 0;
	FILE *file = fopen(path, "r");

	dump_path = path;
	dump_line = 0;
	if(file == NULL) refuse("cannot be opened");

	// What scene_dump() leaves out is what scene_build() sets for "not used"
	memset(scene, 0, sizeof(*scene));
	scene->dot = -1;
	scene->permille = -1;
	scene->over_permille = -1;

	while(fgets(text, sizeof(text), file) != NULL)
	{
		size_t length = strlen(text);

		dump_line++;
		if(length == 0 || text[length - 1] != '\n') refuse("a line without an end, or longer than any scene_dump() writes");
		text[length - 1] = '\0';

		if((rest = behind(text, "kind:")) != NULL)
		{
			scene->kind = (scene_kind_t)word_of(rest, kind_words, COUNT(kind_words));
		}
		else if((rest = behind(text, "ring:")) != NULL)
		{
			// "progress 277": the permille stands only behind this kind
			char *blank = strchr(rest, ' ');

			if(blank != NULL)
			{
				*blank = '\0';
				scene->ring.permille = number_of(blank + 1);
			}
			scene->ring.kind = (ring_kind_t)word_of(rest, ring_words, COUNT(ring_words));
		}
		else if((rest = behind(text, "title:")) != NULL) TAKE(scene->title, rest);
		else if((rest = behind(text, "note:")) != NULL) TAKE(scene->note, rest);
		else if((rest = behind(text, "item:")) != NULL)
		{
			scene_item_t *target = &scene->items[scene->item_count];

			if(scene->item_count == LAYOUT_ITEMS_MAX) refuse("more items than a scene holds");
			split(rest, fields, 6);
			TAKE(target->label, fields[0]);
			TAKE(target->text, fields[1]);
			TAKE(target->unit, fields[2]);
			target->tone = (scene_tone_t)word_of(fields[3], tone_words, COUNT(tone_words));
			target->widget = (layout_widget_t)word_of(fields[4], widget_words, COUNT(widget_words));
			target->permille = number_of(fields[5]);
			scene->item_count++;
		}
		else if((rest = behind(text, "dots:")) != NULL)
		{
			// "2/7": the second of seven
			char *slash = strchr(rest, '/');

			if(slash == NULL) refuse("dots without a slash");
			*slash = '\0';
			scene->dot = number_of(rest) - 1;
			scene->dots = number_of(slash + 1);
		}
		else if((rest = behind(text, "row: >")) != NULL || (rest = behind(text, "row: -")) != NULL)
		{
			scene_row_t *target = &scene->rows[scene->row_count];

			if(scene->row_count == SCENE_ROWS_MAX) refuse("more rows than a scene holds");
			split(rest, fields, 4);
			target->kind = (scene_row_kind_t)word_of(fields[0], row_words, COUNT(row_words));
			TAKE(target->text, fields[1]);
			TAKE(target->detail, fields[2]);
			if(strcmp(fields[3], "enabled") != 0 && strcmp(fields[3], "disabled") != 0) refuse("neither enabled nor disabled");
			target->enabled = fields[3][0] == 'e';
			target->focus = text[5] == '>';
			scene->row_count++;
		}
		else if((rest = behind(text, "first:")) != NULL) scene->first = number_of(rest);
		else if((rest = behind(text, "total:")) != NULL) scene->total = number_of(rest);
		else if((rest = behind(text, "line:")) != NULL)
		{
			if(scene->line_count == SCENE_LINES_MAX) refuse("more lines than a scene holds");
			TAKE(scene->lines[scene->line_count], rest);
			scene->line_count++;
		}
		else if((rest = behind(text, "big:")) != NULL) TAKE(scene->big, rest);
		else if((rest = behind(text, "permille:")) != NULL) scene->permille = number_of(rest);
		else if((rest = behind(text, "option: >")) != NULL || (rest = behind(text, "option: -")) != NULL)
		{
			if(options_read == 2) refuse("more than two options");
			TAKE(scene->options[options_read], rest);
			if(text[8] == '>') scene->option = options_read;
			options_read++;
		}
		else if((rest = behind(text, "over:")) != NULL)
		{
			scene->over = (scene_over_t)word_of(rest, over_words, COUNT(over_words));
		}
		else if((rest = behind(text, "over_line:")) != NULL)
		{
			if(scene->over_line_count == COUNT(scene->over_lines)) refuse("more lines over the screen than a scene holds");
			TAKE(scene->over_lines[scene->over_line_count], rest);
			scene->over_line_count++;
		}
		else if((rest = behind(text, "over_permille:")) != NULL) scene->over_permille = number_of(rest);
		else refuse("a line scene_dump() does not write");
	}
	fclose(file);
	if(dump_line == 0) refuse("empty");
}

/*
 * LVGL without a display
 */

static lv_display_t *display;
static _Alignas(8) uint16_t frame[SIZE * SIZE];     // RGB565, as the panel gets it
static _Alignas(16) uint8_t heap[HEAP_SIZE];
static int complaints;                               // of LVGL, since they were last looked at

static void flushed(lv_display_t *flushing, const lv_area_t *area, uint8_t *pixels)
{
	(void)area;
	(void)pixels;
	lv_display_flush_ready(flushing);
}

static void complained(lv_log_level_t level, const char *text)
{
	if(level < LV_LOG_LEVEL_WARN) return;
	printf("  LVGL: %s", text);
	complaints++;
}

static void start(void)
{
	lv_mem_monitor_t memory;

	lv_init();
	lv_log_register_print_cb(complained);

	// As the firmware has to do it: lv_init() works with the small pool that is part of the program, and
	// the heap proper comes from the PSRAM directly after it
	lv_mem_monitor(&memory);
	printf("LVGL heap after lv_init(): %zu of %zu bytes used\n", memory.total_size - memory.free_size,
	       memory.total_size);
	if(lv_mem_add_pool(heap, sizeof(heap)) == NULL)
	{
		printf("the pool of %zu bytes was not taken\n", sizeof(heap));
		exit(1);
	}

	display = lv_display_create(SIZE, SIZE);
	lv_display_set_buffers(display, frame, NULL, sizeof(frame), LV_DISPLAY_RENDER_MODE_DIRECT);
	lv_display_set_flush_cb(display, flushed);

	ui_init(display);
}

static void show(const scene_t *scene)
{
	ui_show(scene);
	lv_refr_now(display);
}

/*
 * Pictures
 */

static void rgb(uint16_t pixel, uint8_t *out)
{
	out[0] = (uint8_t)(((pixel >> 11) & 31) * 255 / 31);
	out[1] = (uint8_t)(((pixel >> 5) & 63) * 255 / 63);
	out[2] = (uint8_t)((pixel & 31) * 255 / 31);
}

// How bright a pixel is to the eye, 0 to 255
static int brightness(uint16_t pixel)
{
	uint8_t color[3];

	rgb(pixel, color);
	return (color[0] * 77 + color[1] * 150 + color[2] * 29) >> 8;
}

static uint32_t crc(uint32_t sum, const uint8_t *data, size_t length)
{
	for(size_t i = 0; i < length; i++)
	{
		sum ^= data[i];
		for(int bit = 0; bit < 8; bit++) sum = (sum >> 1) ^ (0xEDB88320u & (0u - (sum & 1u)));
	}
	return sum;
}

static void big_endian(uint8_t *out, uint32_t value)
{
	out[0] = (uint8_t)(value >> 24);
	out[1] = (uint8_t)(value >> 16);
	out[2] = (uint8_t)(value >> 8);
	out[3] = (uint8_t)value;
}

static void chunk(FILE *file, const char *type, const uint8_t *data, size_t length)
{
	uint8_t word[4];
	uint32_t sum = crc(crc(0xFFFFFFFFu, (const uint8_t *)type, 4), data, length) ^ 0xFFFFFFFFu;

	big_endian(word, (uint32_t)length);
	fwrite(word, 1, 4, file);
	fwrite(type, 1, 4, file);
	fwrite(data, 1, length, file);
	big_endian(word, sum);
	fwrite(word, 1, 4, file);
}

#define PNG_ROW     ((size_t)(1 + 3 * SIZE))    // the filter of the row, then its pixels
#define PNG_RAW     (PNG_ROW * SIZE)
#define PNG_BLOCK   ((size_t)65535)             // the most a stored block of deflate holds

// The frame as a PNG: 8 bits of red, green and blue, in deflate blocks that are stored, not compressed
static bool write_png(const char *path)
{
	static uint8_t raw[PNG_RAW];
	static uint8_t packed[2 + PNG_RAW + 5 * (PNG_RAW / PNG_BLOCK + 1) + 4];
	static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
	uint8_t header[13] = {0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 0, 0, 0};
	uint32_t a = 1, b = 0;
	size_t length = 0;
	FILE *file;
	bool ok;

	for(size_t y = 0; y < SIZE; y++)
	{
		raw[y * PNG_ROW] = 0;   // filter: none
		for(size_t x = 0; x < SIZE; x++) rgb(frame[y * SIZE + x], &raw[y * PNG_ROW + 1 + 3 * x]);
	}

	// The header of zlib, the blocks, and the checksum of zlib over what is in them
	packed[length++] = 0x78;
	packed[length++] = 0x01;
	for(size_t at = 0; at < PNG_RAW; at += PNG_BLOCK)
	{
		size_t part = PNG_RAW - at < PNG_BLOCK ? PNG_RAW - at : PNG_BLOCK;

		packed[length++] = at + part == PNG_RAW ? 1 : 0;    // the last block says so
		packed[length++] = (uint8_t)part;
		packed[length++] = (uint8_t)(part >> 8);
		packed[length++] = (uint8_t)~part;
		packed[length++] = (uint8_t)(~part >> 8);
		memcpy(&packed[length], &raw[at], part);
		length += part;
	}
	for(size_t i = 0; i < PNG_RAW; i++)
	{
		a = (a + raw[i]) % 65521u;
		b = (b + a) % 65521u;
	}
	big_endian(&packed[length], (b << 16) | a);
	length += 4;

	file = fopen(path, "wb");
	if(file == NULL) return false;
	big_endian(&header[0], SIZE);
	big_endian(&header[4], SIZE);
	fwrite(signature, 1, sizeof(signature), file);
	chunk(file, "IHDR", header, sizeof(header));
	chunk(file, "IDAT", packed, length);
	chunk(file, "IEND", header, 0);
	ok = ferror(file) == 0;
	return fclose(file) == 0 && ok;
}

// The frame as text: one character for 5 x 10 pixels, the brighter the denser
static void print_art(void)
{
	static const char shades[] = " .:-=+*#%@";

	for(int row = 0; row < ART_ROWS; row++)
	{
		char text[ART_COLUMNS + 1];

		for(int column = 0; column < ART_COLUMNS; column++)
		{
			unsigned sum = 0;

			for(int y = row * 10; y < row * 10 + 10; y++)
			{
				for(int x = column * 5; x < column * 5 + 5; x++) sum += (unsigned)brightness(frame[y * SIZE + x]);
			}
			text[column] = shades[sum / 50 * 9 / 255];
		}
		text[ART_COLUMNS] = '\0';
		printf("  |%s|\n", text);
	}
}

static uint64_t frame_hash(void)
{
	const uint8_t *bytes = (const uint8_t *)frame;
	uint64_t hash = 0xCBF29CE484222325u;

	for(size_t i = 0; i < sizeof(frame); i++) hash = (hash ^ bytes[i]) * 0x100000001B3u;
	return hash;
}

/*
 * The checks
 */

typedef enum
{
	THING_BOX,
	THING_ARC,
	THING_TEXT,
} thing_kind_t;

// A part that is on the screen, and the pixels it lights when it is there alone
typedef struct
{
	lv_obj_t *object;
	int layer;              // 0: the screen, 1: what lies over it
	thing_kind_t kind;
	int number;             // among the parts of its layer
	int order;              // among the parts of its kind in its layer, those that are hidden counted
	int left, top, right, bottom;   // the place LVGL gives it: for a text its lines in full height and width
	int x0, y0, x1, y1;     // around its pixels; x1 < x0: it lights none
	int count;
	uint16_t ink;           // the brightest of its pixels when it is there alone; a text is white then
	bool used;              // a text: it stands for a text of the scene
	bool toned;             // a text: it is the value of an item
	uint8_t lit[SIZE * SIZE / 8];
} thing_t;

#define THINGS_MAX  64

static thing_t things[THINGS_MAX];
static int thing_count;
static const char *screen_name = "start";
static int failures;

static void fail(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void fail(const char *format, ...)
{
	va_list arguments;

	printf("  FAILED %s: ", screen_name);
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
	failures++;
}

static bool is_lit(const thing_t *thing, int x, int y)
{
	return (thing->lit[(y * SIZE + x) / 8] >> ((y * SIZE + x) % 8)) & 1;
}

static const char *text_of(const thing_t *thing)
{
	return thing->kind == THING_TEXT ? lv_label_get_text(thing->object) : "";
}

// What a part is called in a message
static const char *name_of(const thing_t *thing)
{
	static char names[2][160];
	static int turn;
	static const char *const kinds[] = {"box", "arc", "text"};
	char *name = names[turn++ % 2];

	if(thing->kind == THING_TEXT)
	{
		snprintf(name, sizeof(names[0]), "text \"%.100s\"", text_of(thing));
	}
	else
	{
		snprintf(name, sizeof(names[0]), "%s %d of the %s (%d..%d, %d..%d)", kinds[thing->kind], thing->number,
		         thing->layer == 0 ? "screen" : "overlay", thing->x0, thing->x1, thing->y0, thing->y1);
	}
	for(char *at = name; *at != '\0'; at++)
	{
		if(*at == '\n') *at = '/';
	}
	return name;
}

// Everything that is not hidden. ui.c puts the parts of the screen into the first child of the LVGL screen
// and what lies over it into the second.
static void collect(void)
{
	lv_obj_t *screen = lv_display_get_screen_active(display);

	thing_count = 0;
	for(int layer = 0; layer < (int)lv_obj_get_child_count(screen); layer++)
	{
		lv_obj_t *parent = lv_obj_get_child(screen, layer);
		int orders[3] = {0, 0, 0};

		for(int i = 0; i < (int)lv_obj_get_child_count(parent); i++)
		{
			lv_obj_t *object = lv_obj_get_child(parent, i);
			thing_t *thing = &things[thing_count];
			thing_kind_t kind = lv_obj_check_type(object, &lv_label_class) ? THING_TEXT :
			                    lv_obj_check_type(object, &lv_arc_class) ? THING_ARC : THING_BOX;
			int order = orders[kind]++;

			if(lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) continue;
			if(thing_count == THINGS_MAX)
			{
				fail("more than %d parts on the screen", THINGS_MAX);
				return;
			}
			memset(thing, 0, sizeof(*thing));
			thing->object = object;
			thing->layer = layer;
			thing->number = i;
			thing->order = order;
			thing->kind = kind;
			// As of the last redraw, which every caller has just had made
			thing->left = (int)lv_obj_get_x(object);
			thing->top = (int)lv_obj_get_y(object);
			thing->right = thing->left + (int)lv_obj_get_width(object) - 1;
			thing->bottom = thing->top + (int)lv_obj_get_height(object) - 1;
			thing_count++;
		}
	}
}

// Renders each part alone and notes its pixels
static void look(void)
{
	for(int i = 0; i < thing_count; i++)
	{
		thing_t *thing = &things[i];
		lv_color_t color = lv_color_hex(0);

		for(int other = 0; other < thing_count; other++)
		{
			if(other != i) lv_obj_add_flag(things[other].object, LV_OBJ_FLAG_HIDDEN);
		}
		// A dark text on a white bar would not be seen on the black ground
		if(thing->kind == THING_TEXT)
		{
			color = lv_obj_get_style_text_color(thing->object, LV_PART_MAIN);
			lv_obj_set_style_text_color(thing->object, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
		}
		lv_refr_now(display);

		thing->x0 = SIZE;
		thing->y0 = SIZE;
		thing->x1 = -1;
		thing->y1 = -1;
		for(int y = 0; y < SIZE; y++)
		{
			for(int x = 0; x < SIZE; x++)
			{
				if(frame[y * SIZE + x] == 0) continue;
				thing->lit[(y * SIZE + x) / 8] |= (uint8_t)(1u << ((y * SIZE + x) % 8));
				thing->count++;
				if(brightness(frame[y * SIZE + x]) > brightness(thing->ink)) thing->ink = frame[y * SIZE + x];
				if(x < thing->x0) thing->x0 = x;
				if(x > thing->x1) thing->x1 = x;
				if(y < thing->y0) thing->y0 = y;
				if(y > thing->y1) thing->y1 = y;
			}
		}

		if(thing->kind == THING_TEXT) lv_obj_set_style_text_color(thing->object, color, LV_PART_MAIN);
		for(int other = 0; other < thing_count; other++)
		{
			if(other != i) lv_obj_remove_flag(things[other].object, LV_OBJ_FLAG_HIDDEN);
		}
	}
	lv_refr_now(display);
}

// What is seen of each part in the frame as a whole: the part is taken away, and it is looked at how much
// the pixels it lit change. A text has to stand out from what it stands on. Under an overlay nothing of
// the screen may change at all: the panel hides it.
static void check_seen(const scene_t *scene)
{
	static uint16_t whole[SIZE * SIZE];
	int top = scene->over != SCENE_OVER_NONE ? 1 : 0;

	memcpy(whole, frame, sizeof(whole));
	for(int i = 0; i < thing_count; i++)
	{
		const thing_t *thing = &things[i];
		int most = 0;

		lv_obj_add_flag(thing->object, LV_OBJ_FLAG_HIDDEN);
		lv_refr_now(display);
		for(int y = thing->y0; y <= thing->y1; y++)
		{
			for(int x = thing->x0; x <= thing->x1; x++)
			{
				int change = brightness(whole[y * SIZE + x]) - brightness(frame[y * SIZE + x]);

				if(!is_lit(thing, x, y)) continue;
				if(change < 0) change = -change;
				if(change == 0 && whole[y * SIZE + x] != frame[y * SIZE + x]) change = 1;
				if(change > most) most = change;
			}
		}
		lv_obj_remove_flag(thing->object, LV_OBJ_FLAG_HIDDEN);

		if(thing->layer < top)
		{
			if(most != 0) fail("%s shows through what lies over the screen", name_of(thing));
		}
		else if(thing->kind == THING_TEXT && most < CONTRAST)
		{
			// Only a text: the track of a bar that is full lies under its filling, as it should
			fail("%s is hard to read: it differs from its ground by %d of 255", name_of(thing), most);
		}
	}
	lv_refr_now(display);
}

// Twice the distance of the middle of a pixel from the middle of the screen, squared
static int far2(int x, int y)
{
	int dx = 2 * x + 1 - SIZE, dy = 2 * y + 1 - SIZE;

	return dx * dx + dy * dy;
}

// Every part inside its circle: a text within R_TEXT, a shape within R_SHAPE. Half a pixel is granted to
// both: the edge of a round shape is drawn into the pixels it only touches. Of a text in one line the whole
// place counts, not only the pixels its letters happen to light: a text that ends on a blank side of a
// letter would hide that it was given too much room. (The lines of a text in several are each as wide as
// the circle lets them be where they stand; the place around all of them has corners that mean nothing.)
static void check_circle(void)
{
	for(int i = 0; i < thing_count; i++)
	{
		const thing_t *thing = &things[i];
		int radius = thing->kind == THING_TEXT ? R_TEXT : R_SHAPE;
		bool outside = false;

		if(thing->count == 0)
		{
			fail("%s is on the screen but lights no pixel", name_of(thing));
			continue;
		}
		if(thing->kind == THING_TEXT && strchr(text_of(thing), '\n') == NULL)
		{
			int x = 2 * thing->left + 1 < SIZE ? thing->left : thing->right;
			int y = 2 * thing->top + 1 < SIZE ? thing->top : thing->bottom;

			// The corner that is furthest from the middle of the screen
			if(far2(thing->right, y) > far2(x, y)) x = thing->right;
			if(far2(thing->left, y) > far2(x, y)) x = thing->left;
			if(far2(x, thing->bottom) > far2(x, y)) y = thing->bottom;
			if(far2(x, thing->top) > far2(x, y)) y = thing->top;
			if(far2(x, y) > (2 * radius + 1) * (2 * radius + 1))
			{
				fail("the place of %s reaches outside the circle of radius %d at (%d, %d)", name_of(thing), radius, x, y);
				continue;
			}
		}
		for(int y = thing->y0; y <= thing->y1 && !outside; y++)
		{
			for(int x = thing->x0; x <= thing->x1 && !outside; x++)
			{
				if(is_lit(thing, x, y) && far2(x, y) > (2 * radius + 1) * (2 * radius + 1))
				{
					fail("%s reaches outside the circle of radius %d at (%d, %d)", name_of(thing), radius, x, y);
					outside = true;
				}
			}
		}
	}
}

// Whether a part lights a pixel inside the box around the pixels of another
static bool reaches(const thing_t *thing, const thing_t *other)
{
	for(int y = other->y0; y <= other->y1; y++)
	{
		for(int x = other->x0; x <= other->x1; x++)
		{
			if(is_lit(thing, x, y)) return true;
		}
	}
	return false;
}

// Pixels that both parts light
static int shared(const thing_t *thing, const thing_t *other)
{
	int count = 0;

	for(int y = thing->y0; y <= thing->y1; y++)
	{
		for(int x = thing->x0; x <= thing->x1; x++) count += is_lit(thing, x, y) && is_lit(other, x, y);
	}
	return count;
}

static void check_overlap(void)
{
	for(int i = 0; i < thing_count; i++)
	{
		const thing_t *text = &things[i];

		if(text->kind != THING_TEXT || text->count == 0) continue;
		for(int other = 0; other < thing_count; other++)
		{
			const thing_t *thing = &things[other];

			if(other == i || thing->layer != text->layer || thing->count == 0) continue;
			if(thing->x0 > text->x1 || thing->x1 < text->x0 || thing->y0 > text->y1 || thing->y1 < text->y0) continue;

			if(thing->kind == THING_TEXT)
			{
				// Two texts: neither may reach into the box around the other. Both ways, because the box
				// around a text of several lines has corners where the text is not.
				if(other > i && reaches(text, thing) && reaches(thing, text))
				{
					fail("%s and %s overlap", name_of(text), name_of(thing));
				}
			}
			else
			{
				// A text and a shape: the text may lie on it, as on the bar of the focus, but not half
				int both = shared(text, thing);

				if(both != 0 && both != text->count) fail("%s lies half on %s", name_of(text), name_of(thing));
			}
		}
	}
}

// 2: the label shows the text as it is, with line breaks for blanks; 1: its beginning and an ellipsis; 0: neither
static int shows(const char *label, const char *text)
{
	static const char ellipsis[] = "…";
	size_t length = strlen(label), cut;

	if(length == strlen(text))
	{
		size_t at = 0;

		while(at < length && (label[at] == text[at] || (label[at] == '\n' && text[at] == ' '))) at++;
		if(at == length) return 2;
	}
	if(length < sizeof(ellipsis) - 1 || strcmp(label + length - (sizeof(ellipsis) - 1), ellipsis) != 0) return 0;
	cut = length - (sizeof(ellipsis) - 1);
	if(cut >= strlen(text)) return 0;
	for(size_t at = 0; at < cut; at++)
	{
		if(label[at] != text[at] && !(label[at] == '\n' && text[at] == ' ')) return 0;
	}
	return 1;
}

// The texts of a scene that have to be on the screen
static int texts_of(const scene_t *scene, const char *texts[], int max)
{
	int count = 0;

#define ADD(text)   do { if((text)[0] != '\0' && count < max) texts[count++] = (text); } while(0)
	ADD(scene->title);
	ADD(scene->note);
	if(scene->kind == SCENE_VALUES)
	{
		for(int i = 0; i < scene->item_count; i++)
		{
			ADD(scene->items[i].label);
			ADD(scene->items[i].text);
			ADD(scene->items[i].unit);
		}
	}
	if(scene->kind == SCENE_PROGRESS || scene->kind == SCENE_LEVEL) ADD(scene->big);
	if(scene->kind != SCENE_VALUES && scene->kind != SCENE_LEVEL)
	{
		for(int i = 0; i < scene->line_count; i++) ADD(scene->lines[i]);
	}
	if(scene->kind == SCENE_LIST)
	{
		for(int i = 0; i < scene->row_count; i++)
		{
			ADD(scene->rows[i].text);
			ADD(scene->rows[i].detail);
		}
	}
	if(scene->kind == SCENE_CHOICE)
	{
		ADD(scene->options[0]);
		ADD(scene->options[1]);
	}
	if(scene->over != SCENE_OVER_NONE)
	{
		for(int i = 0; i < scene->over_line_count; i++) ADD(scene->over_lines[i]);
	}
#undef ADD
	return count;
}

// Whether a text of the screen that is being looked at is one of known_cut
static bool is_known_cut(const char *text)
{
	for(int i = 0; i < (int)(sizeof(known_cut) / sizeof(known_cut[0])); i++)
	{
		if(strcmp(known_cut[i].screen, screen_name) == 0 && strcmp(known_cut[i].text, text) == 0) return true;
	}
	return false;
}

static int known_cuts;  // labels of known_cut that were seen cut

static void check_texts(const scene_t *scene, bool may_cut)
{
	const char *texts[64];
	int count = texts_of(scene, texts, 64);
	bool above = scene->kind == SCENE_LIST && scene->first > 0;
	bool below = scene->kind == SCENE_LIST && scene->first + scene->row_count < scene->total;
	int cut = 0;

	// The marks for more rows are no text of the scene
	for(int i = 0; i < thing_count; i++)
	{
		thing_t *thing = &things[i];

		if(thing->kind != THING_TEXT || thing->layer != 0) continue;
		if(above && !thing->used && strcmp(text_of(thing), "▲") == 0)
		{
			thing->used = true;
			above = false;
		}
		else if(below && !thing->used && strcmp(text_of(thing), "▼") == 0)
		{
			thing->used = true;
			below = false;
		}
	}
	if(above) fail("there are rows above, and no mark says so");
	if(below) fail("there are rows below, and no mark says so");

	// Every text of the scene on a label of its own, whole
	for(int i = 0; i < count; i++)
	{
		int found = -1;

		for(int at = 0; at < thing_count && found < 0; at++)
		{
			if(things[at].kind == THING_TEXT && !things[at].used && shows(text_of(&things[at]), texts[i]) == 2) found = at;
		}
		if(found >= 0)
		{
			things[found].used = true;
			if(is_known_cut(texts[i])) fail("\"%s\" is whole now: take it out of known_cut", texts[i]);
		}
		else if(!may_cut && !is_known_cut(texts[i])) fail("\"%s\" is not on the screen, or not whole", texts[i]);
	}

	// What is left has to be the beginning of a text of the scene
	for(int i = 0; i < thing_count; i++)
	{
		thing_t *thing = &things[i];
		bool known = false;

		const char *whole = "";

		if(thing->kind != THING_TEXT || thing->used) continue;
		for(int at = 0; at < count && !known; at++)
		{
			known = shows(text_of(thing), texts[at]) == 1;
			whole = texts[at];
		}
		if(known && may_cut) cut++;
		else if(known && is_known_cut(whole))
		{
			printf("  known: the label \"%s\" does not fit its place, it is %s\n", whole, name_of(thing));
			known_cuts++;
		}
		else if(known) fail("%s is cut", name_of(thing));
		else fail("%s is no text of the scene", name_of(thing));
	}
	if(may_cut) printf("  %d texts cut\n", cut);
}

// The fonts of the texts of a scene, in the order of the labels of ui.c, those that are hidden left out
static int fonts_of(const scene_t *scene, const thing_t *texts[], int max)
{
	int count = 0;

	show(scene);
	collect();
	for(int i = 0; i < thing_count && count < max; i++)
	{
		if(things[i].kind == THING_TEXT) texts[count++] = &things[i];
	}
	return count;
}

static const screen_t *screen_named(const char *name)
{
	for(int i = 0; i < SCREENS; i++)
	{
		if(strcmp(screens[i].name, name) == 0) return &screens[i];
	}
	fprintf(stderr, "render.c: no screen is called %s\n", name);
	exit(2);
}

// The pairs of `steady`: the texts of both screens are the same labels in the same fonts at the same height
static void check_steady(void)
{
	static scene_t scene;
	const lv_font_t *fonts[THINGS_MAX];
	int orders[THINGS_MAX], layers[THINGS_MAX], tops[THINGS_MAX];
	const thing_t *texts[THINGS_MAX];

	for(int pair = 0; pair < (int)(sizeof(steady) / sizeof(steady[0])); pair++)
	{
		int count, other;

		screen_name = steady[pair][1];
		screen_named(steady[pair][0])->fill(&scene);
		count = fonts_of(&scene, texts, THINGS_MAX);
		for(int i = 0; i < count; i++)
		{
			fonts[i] = lv_obj_get_style_text_font(texts[i]->object, LV_PART_MAIN);
			orders[i] = texts[i]->order;
			layers[i] = texts[i]->layer;
			tops[i] = texts[i]->top;
		}

		screen_named(steady[pair][1])->fill(&scene);
		other = fonts_of(&scene, texts, THINGS_MAX);
		if(other != count)
		{
			fail("%d texts, %s has %d: the two are to differ in a value only", other, steady[pair][0], count);
			continue;
		}
		for(int i = 0; i < count; i++)
		{
			const lv_font_t *font = lv_obj_get_style_text_font(texts[i]->object, LV_PART_MAIN);

			if(texts[i]->order != orders[i] || texts[i]->layer != layers[i])
			{
				fail("%s stands where %s has no text", name_of(texts[i]), steady[pair][0]);
			}
			else if(font != fonts[i])
			{
				fail("%s is written %d px high, and %d px on %s: a text must not change its size with a value",
				     name_of(texts[i]), (int)lv_font_get_line_height(font), (int)lv_font_get_line_height(fonts[i]),
				     steady[pair][0]);
			}
			else if(texts[i]->top != tops[i])
			{
				fail("%s begins at y = %d, and at %d on %s: a text must not jump with a value", name_of(texts[i]),
				     texts[i]->top, tops[i], steady[pair][0]);
			}
		}
	}
}

// The label that shows a text, looked for from the last part back: the rows and the options are put last
static const thing_t *label_of(const char *text, int *before)
{
	for(int i = *before - 1; i >= 0; i--)
	{
		if(things[i].kind == THING_TEXT && things[i].layer == 0 && shows(text_of(&things[i]), text) != 0)
		{
			*before = i;
			return &things[i];
		}
	}
	return NULL;
}

static void probe(const char *what, int x, int y, int expected)
{
	int got = ui_row_at(x, y);

	printf("  ui_row_at(%d, %d) = %d  %s\n", x, y, got, what);
	if(got != expected) fail("ui_row_at(%d, %d) is %d, not %d: %s", x, y, got, expected, what);
}

typedef enum
{
	SHADE_DARK,
	SHADE_RED,
	SHADE_AMBER,
	SHADE_GREY,
	SHADE_WHITE,
} shade_t;

static const char *const shade_names[] = {"dark", "red", "amber", "grey", "white"};

// What a pixel looks like, as far as the screens tell things apart by colour
static shade_t shade_of(uint16_t pixel)
{
	int red = pixel >> 11, green = (pixel >> 5) & 63, blue = pixel & 31;

	if(pixel == 0) return SHADE_DARK;
	if(blue < 8 && red > 16) return green < 16 ? SHADE_RED : SHADE_AMBER;
	return red >= 28 && green >= 56 && blue >= 28 ? SHADE_WHITE : SHADE_GREY;
}

static shade_t tone_shade(scene_tone_t tone)
{
	return tone == SCENE_TONE_ALARM ? SHADE_RED : tone == SCENE_TONE_WARN ? SHADE_AMBER :
	       tone == SCENE_TONE_DIM ? SHADE_GREY : SHADE_WHITE;
}

// The brightest pixel of a part in the frame as it is, with everything else around it
static uint16_t brightest_of(const thing_t *thing)
{
	uint16_t brightest = 0;

	for(int y = thing->y0; y <= thing->y1; y++)
	{
		for(int x = thing->x0; x <= thing->x1; x++)
		{
			if(is_lit(thing, x, y) && brightness(frame[y * SIZE + x]) > brightness(brightest)) brightest = frame[y * SIZE + x];
		}
	}
	return brightest;
}

// Whether a text lies on a box filled white: the bar that marks the focus
static bool on_bar(const thing_t *text)
{
	for(int i = 0; i < thing_count; i++)
	{
		const thing_t *box = &things[i];

		if(box->kind != THING_BOX || box->layer != text->layer || box->ink != 0xFFFF) continue;
		if(shared(text, box) == text->count) return true;
	}
	return false;
}

// The rows and the options: ui_row_at() finds each at its middle, the one in focus has the white bar and no
// other has, and all that can be chosen is written in one font, here and on every other screen
static void check_rows(const scene_t *scene, bool may_cut)
{
	static const lv_font_t *action_font;
	static char action_text[SCENE_TEXT_SIZE], action_screen[64];
	bool covered = scene->over != SCENE_OVER_NONE;
	int before = thing_count;
	char what[160];

	probe("the top of the screen", CENTRE, 12, -1);
	if(scene->kind == SCENE_LIST)
	{
		for(int i = scene->row_count - 1; i >= 0; i--)
		{
			const scene_row_t *source = &scene->rows[i];
			const thing_t *label;

			// The detail was put after the text
			if(source->detail[0] != '\0') label_of(source->detail, &before);
			label = label_of(source->text, &before);
			if(label == NULL || label->count == 0) continue;
			snprintf(what, sizeof(what), "row %d, \"%.100s\"", scene->first + i, source->text);
			probe(what, (label->x0 + label->x1) / 2, (label->y0 + label->y1) / 2, covered ? -1 : scene->first + i);

			if(on_bar(label) != source->focus)
			{
				fail("%s %s", what, source->focus ? "is in focus and has no white bar" : "has a white bar and is not in focus");
			}
			// White is what can be read or chosen, grey an action that is not offered. On the bar of the
			// focus both are dark, and check_seen() looks at that.
			if(!source->focus && !covered && shade_of(brightest_of(label)) != (source->enabled ? SHADE_WHITE : SHADE_GREY))
			{
				fail("%s is %s and written in %s", what, source->enabled ? "enabled" : "disabled",
				     shade_names[shade_of(brightest_of(label))]);
			}
			// A screen with more than it can show may have to make a row smaller
			if(source->kind == SCENE_ROW_ACTION && !may_cut)
			{
				const lv_font_t *font = lv_obj_get_style_text_font(label->object, LV_PART_MAIN);

				if(action_font == NULL)
				{
					action_font = font;
					snprintf(action_text, sizeof(action_text), "%s", source->text);
					snprintf(action_screen, sizeof(action_screen), "%.60s", screen_name);
				}
				else if(font != action_font)
				{
					fail("%s is written %d px high, \"%s\" of %s %d px: what can be chosen has one font", what,
					     (int)lv_font_get_line_height(font), action_text, action_screen,
					     (int)lv_font_get_line_height(action_font));
				}
			}
		}
	}
	else if(scene->kind == SCENE_CHOICE)
	{
		for(int i = 1; i >= 0; i--)
		{
			const thing_t *label = label_of(scene->options[i], &before);

			if(label == NULL || label->count == 0) continue;
			snprintf(what, sizeof(what), "option %d, \"%.100s\"", i, scene->options[i]);
			probe(what, (label->x0 + label->x1) / 2, (label->y0 + label->y1) / 2, covered ? -1 : i);

			if(on_bar(label) != (i == (scene->option == 1 ? 1 : 0)))
			{
				fail("%s %s", what, on_bar(label) ? "is filled white and is not in focus" : "is in focus and is not filled white");
			}
		}
	}
	else
	{
		probe("the middle of a screen without rows", CENTRE, CENTRE, -1);
	}
}

// A dot of the pages: a small box at the lower edge
static bool is_dot(const thing_t *thing)
{
	return thing->kind == THING_BOX && thing->layer == 0 && thing->count > 0 && thing->y0 > 430 &&
	       thing->x1 - thing->x0 < 10 && thing->y1 - thing->y0 < 10;
}

// As many dots as the knob has pages, from left to right, and filled is the one of the page shown. A dot
// that is no more than an outline is dark in its middle.
static void check_dots(const scene_t *scene)
{
	bool pages = scene->kind == SCENE_VALUES || scene->kind == SCENE_NOTICE;
	int expected = !pages || scene->dots < 0 ? 0 : scene->dots > LAYOUT_PAGES_MAX ? LAYOUT_PAGES_MAX : scene->dots;
	int count = 0, last_x = -1;

	for(int i = 0; i < thing_count; i++)
	{
		const thing_t *thing = &things[i];
		bool filled;

		if(!is_dot(thing)) continue;
		filled = is_lit(thing, (thing->x0 + thing->x1) / 2, (thing->y0 + thing->y1) / 2);
		if(filled != (count == scene->dot))
		{
			fail("dot %d of %d is %s, the page shown is number %d", count + 1, expected, filled ? "filled" : "an outline",
			     scene->dot + 1);
		}
		if(filled && thing->ink != 0xFFFF) fail("dot %d is filled and not white", count + 1);
		if(thing->x0 <= last_x) fail("dot %d is not to the right of the one before it", count + 1);
		last_x = thing->x1;
		count++;
	}
	if(count != expected) fail("%d dots for %d pages", count, expected);
}

static const thing_t *arc_of(int layer, int order)
{
	for(int i = 0; i < thing_count; i++)
	{
		if(things[i].kind == THING_ARC && things[i].layer == layer && things[i].order == order) return &things[i];
	}
	return NULL;
}

// An arc that shows how far something is: its track from `start` (degrees clockwise from 3 o'clock) over
// `sweep` degrees, and on it, from the same start, the part that `permille` says. ui.c puts the track
// before the part, `first` is the place of the track among the arcs of its layer. sweep 0: no arc at all.
static void check_arc(const char *what, int layer, int first, int start, int sweep, int permille)
{
	const thing_t *track = arc_of(layer, first), *part = arc_of(layer, first + 1);
	int filled = sweep * (permille < 0 ? 0 : permille > 1000 ? 1000 : permille) / 1000;
	int outer, width = 0;
	double middle;

	if(sweep == 0)
	{
		if(track != NULL || part != NULL) fail("%s on a screen that has none", what);
		return;
	}
	if(track == NULL || track->count == 0)
	{
		fail("%s has no track", what);
		return;
	}

	// The track passes 9 o'clock, 12 o'clock and 3 o'clock: its pixels are as wide as its circle, and at
	// the top it is as thick as it is
	outer = (track->x1 - track->x0 + 1) / 2;
	while(width < outer && is_lit(track, CENTRE, CENTRE - outer + width)) width++;
	middle = outer - width / 2.0;

	for(int angle = 0; angle < 360; angle += 5)
	{
		double turn = (start + angle) * 3.14159265358979 / 180.0;
		int x = (int)(CENTRE + middle * cos(turn)), y = (int)(CENTRE + middle * sin(turn));
		bool on_part = part != NULL && is_lit(part, x, y);

		// An end may fall on either side of a sample next to it
		if(angle < 4 || angle > 356) continue;
		if(!(angle > sweep - 4 && angle < sweep + 4) && is_lit(track, x, y) != (angle < sweep))
		{
			fail("the track of %s is %s %d degrees from its start", what, angle < sweep ? "missing" : "drawn", angle);
			return;
		}
		if(!(angle > filled - 4 && angle < filled + 4) && on_part != (angle < filled))
		{
			fail("%s is %s %d degrees from its start, it has to reach %d degrees (%d permille of %d)", what,
			     on_part ? "filled" : "empty", angle, filled, permille, sweep);
			return;
		}
	}
}

// Whether an item has a bar: an arc or bar widget among several values, a bar widget alone
static bool has_bar(const scene_t *scene, int index)
{
	layout_widget_t widget = scene->items[index].widget;

	return widget == LAYOUT_WIDGET_BAR || (widget == LAYOUT_WIDGET_ARC && scene->item_count > 1);
}

// The bars of a value page, in the order of its items: a track, and on it from the left the part that the
// permille of the item says, in the tone of the item. Nothing else on a value page is a box, but the dots.
static void check_bars(const scene_t *scene)
{
	int at = 0;

	if(scene->kind != SCENE_VALUES) return;
	for(int i = 0; i <= scene->item_count; i++)
	{
		const scene_item_t *item = &scene->items[i < scene->item_count ? i : 0];
		const thing_t *track, *part = NULL;
		int width, filled, drawn = 0;

		while(at < thing_count && (things[at].kind != THING_BOX || things[at].layer != 0 || is_dot(&things[at]))) at++;
		if(i == scene->item_count)
		{
			if(at < thing_count) fail("%s belongs to no item", name_of(&things[at]));
			return;
		}
		if(!has_bar(scene, i)) continue;
		if(at == thing_count)
		{
			fail("item %d, \"%s\", has no bar", i + 1, item->label);
			return;
		}
		track = &things[at++];
		width = track->x1 - track->x0 + 1;
		filled = width * (item->permille < 0 ? 0 : item->permille > 1000 ? 1000 : item->permille) / 1000;

		// The part lies on the track and begins where it begins
		if(at < thing_count && things[at].kind == THING_BOX && things[at].layer == 0 && things[at].x0 == track->x0 &&
		   things[at].y0 == track->y0)
		{
			part = &things[at++];
			drawn = part->x1 - part->x0 + 1;
		}
		if(drawn < filled - 1 || drawn > filled + 1)
		{
			fail("the bar of item %d, \"%s\", is filled %d of %d px, %d permille are %d px", i + 1, item->label, drawn,
			     width, item->permille, filled);
		}
		else if(part != NULL && shade_of(part->ink) != tone_shade(item->tone))
		{
			fail("the bar of item %d, \"%s\", is %s, its tone is %s", i + 1, item->label, shade_names[shade_of(part->ink)],
			     shade_names[tone_shade(item->tone)]);
		}
	}
}

// The first text of a kind that shows a text of the scene, looked for from `from` on; -1: none
static int text_showing(const char *text, int from)
{
	for(int i = from; i < thing_count; i++)
	{
		if(things[i].kind == THING_TEXT && things[i].layer == 0 && shows(text_of(&things[i]), text) != 0) return i;
	}
	return -1;
}

// The line the first line of a text stands on
static int base_of(const thing_t *text)
{
	const lv_font_t *font = lv_obj_get_style_text_font(text->object, LV_PART_MAIN);

	return text->top + (int)(font->line_height - font->base_line);
}

// The title of a value page names the page, and the label below it the first value: the two must not read
// as one block of two lines. From the line the title stands on down to the label it is nearly twice as far
// (seven quarters) as from the line of the label down to its value; or the title is written smaller than
// the label, and still further from it than the label from its value.
static void check_title(const scene_t *scene)
{
	int title, label, value, above, below;
	int title_height, label_height;

	if(scene->kind != SCENE_VALUES || scene->item_count < 1 || scene->title[0] == '\0') return;
	// Only a number begins at the top of its line: a dash and the small letters of "n. v." stand lower
	if(scene->items[0].label[0] == '\0' || scene->items[0].text[0] < '0' || scene->items[0].text[0] > '9') return;

	// ui.c takes the title first, then per value its label and the value itself
	title = text_showing(scene->title, 0);
	label = title < 0 ? -1 : text_showing(scene->items[0].label, title + 1);
	value = label < 0 ? -1 : text_showing(scene->items[0].text, label + 1);
	if(value < 0 || things[title].count == 0 || things[label].count == 0 || things[value].count == 0) return;

	above = things[label].y0 - base_of(&things[title]);
	below = things[value].y0 - base_of(&things[label]);
	title_height = (int)lv_font_get_line_height(lv_obj_get_style_text_font(things[title].object, LV_PART_MAIN));
	label_height = (int)lv_font_get_line_height(lv_obj_get_style_text_font(things[label].object, LV_PART_MAIN));
	if(above <= below || (title_height >= label_height && 4 * above < 7 * below))
	{
		fail("the label \"%s\" (%d px) begins %d px below the line of the title \"%s\" (%d px), its value %d px "
		     "below its own line: they read as one block", scene->items[0].label, label_height, above, scene->title,
		     title_height, below);
	}
}

// What shows a level: the gauge of a value or of the brightness (open at the bottom, from 8 o'clock over
// the top to 4 o'clock), the arc of a request or of the hold (all around, from the top), the same over the
// screen for an upload, and the bars
static void check_levels(const scene_t *scene)
{
	bool gauge = scene->kind == SCENE_LEVEL ||
	             (scene->kind == SCENE_VALUES && scene->item_count == 1 && scene->items[0].widget == LAYOUT_WIDGET_ARC);
	bool around = scene->kind == SCENE_PROGRESS || (scene->kind == SCENE_CHOICE && scene->permille >= 0);

	// On the screen the ring comes first among the arcs; the scene looked at here has none
	if(arc_of(0, 0) != NULL) fail("a ring on a scene without one");
	if(gauge)
	{
		check_arc("the gauge", 0, 1, 150, 240, scene->kind == SCENE_LEVEL ? scene->permille : scene->items[0].permille);
	}
	else if(around)
	{
		check_arc(scene->kind == SCENE_PROGRESS ? "the arc of the request" : "the ring of the hold", 0, 1, 270, 360,
		          scene->permille);
	}
	else
	{
		check_arc("an arc", 0, 1, 0, 0, 0);
	}

	if(scene->over == SCENE_OVER_UPLOAD) check_arc("the arc of the upload", 1, 0, 270, 360, scene->over_permille);
	else check_arc("an arc over the screen", 1, 0, 0, 0, 0);

	check_bars(scene);
}

// The value of every item is written in the colour of its tone: white, grey for what is dimmed, amber and
// red. Looked at in the frame as a whole: the brightest pixel of the text.
static void check_tones(const scene_t *scene)
{
	if(scene->kind != SCENE_VALUES || scene->over != SCENE_OVER_NONE) return;

	for(int i = 0; i < scene->item_count; i++)
	{
		const scene_item_t *item = &scene->items[i];
		shade_t seen = SHADE_DARK;
		bool found = false;

		if(item->text[0] == '\0') continue;
		for(int at = 0; at < thing_count && !found; at++)
		{
			thing_t *thing = &things[at];

			if(thing->kind != THING_TEXT || thing->layer != 0 || thing->toned || shows(text_of(thing), item->text) == 0) continue;
			seen = shade_of(brightest_of(thing));
			if(seen == tone_shade(item->tone))
			{
				thing->toned = true;
				found = true;
			}
		}
		if(!found)
		{
			fail("the value \"%s\" of item %d, \"%s\", is not written in %s (it is found in %s)", item->text, i + 1, item->label,
			     shade_names[tone_shade(item->tone)], shade_names[seen]);
		}
	}
}

// The ring is looked at in the frame as the scene has it: every five degrees, near both edges of its band
// and in the middle of it
static void check_ring(const scene_t *scene)
{
	int sweep = scene->ring.kind == RING_PROGRESS ? scene->ring.permille * 360 / 1000 : 360;

	for(int angle = 0; angle < 360; angle += 5)
	{
		double turn = angle * 3.14159265358979 / 180.0;
		shade_t expected = SHADE_DARK;

		switch(scene->ring.kind)
		{
			case RING_RED:
				expected = SHADE_RED;
				break;
			case RING_YELLOW:
				expected = SHADE_AMBER;
				break;
			case RING_GREY:
				expected = SHADE_GREY;
				break;
			case RING_PROGRESS:
				// The end of the arc may fall on either side of a sample next to it
				if(angle > sweep - 4 && angle < sweep + 4) continue;
				expected = angle < sweep ? SHADE_WHITE : SHADE_DARK;
				break;
			default:
				break;
		}
		// The pixels at the very edges of the band are only touched by the ring: 1.5 px away from both
		for(int step = 0; step < 3; step++)
		{
			double radius = RING_INSIDE + 1.5 + step * (RING_OUTSIDE - RING_INSIDE - 3.0) / 2.0;
			int x = (int)(CENTRE + radius * sin(turn)), y = (int)(CENTRE - radius * cos(turn));
			shade_t seen = shade_of(frame[y * SIZE + x]);

			if(seen != expected)
			{
				fail("the ring is %s at %d degrees from the top, %.1f px from the middle, not %s", shade_names[seen],
				     angle, radius, shade_names[expected]);
				return;
			}
		}
	}
}

// The ring is the one thing out there, and it is nowhere else: `ringed` is the frame with the ring, the
// frame itself the same scene without it
static void check_edge(const uint16_t *ringed)
{
	for(int y = 0; y < SIZE; y++)
	{
		for(int x = 0; x < SIZE; x++)
		{
			if(frame[y * SIZE + x] != 0 && far2(x, y) > (2 * R_SHAPE + 1) * (2 * R_SHAPE + 1))
			{
				fail("something that is not the ring is drawn outside the circle of radius %d at (%d, %d)", R_SHAPE, x, y);
				return;
			}
			if(frame[y * SIZE + x] != ringed[y * SIZE + x] && far2(x, y) <= 4 * R_SHAPE * R_SHAPE)
			{
				fail("the ring reaches inside the circle of radius %d at (%d, %d)", R_SHAPE, x, y);
				return;
			}
		}
	}
}

// Shows a scene and looks at it. always: its picture is written and printed whatever comes of it; else only
// if something is wrong with it. Returns what tells its picture from every other.
static uint64_t examine(const scene_t *scene, bool may_cut, const char *directory, bool always)
{
	static uint16_t ringed[SIZE * SIZE];
	static scene_t bare;
	int before = failures;
	uint64_t hash;
	char path[512];

	show(scene);
	hash = frame_hash();
	memcpy(ringed, frame, sizeof(ringed));
	snprintf(path, sizeof(path), "%s/%s.png", directory, screen_name);
	if(always)
	{
		if(!write_png(path)) fail("%s could not be written", path);
		print_art();
	}
	check_ring(scene);

	// The same scene again changes nothing
	ui_show(scene);
	lv_refr_now(display);
	if(frame_hash() != hash) fail("showing the same scene again changes the picture");

	// The parts are looked at without the ring, which is the one thing that may be at the edge
	bare = *scene;
	bare.ring.kind = RING_NONE;
	bare.ring.permille = 0;
	show(&bare);
	check_edge(ringed);
	collect();
	look();
	check_seen(&bare);
	check_circle();
	check_overlap();
	check_texts(&bare, may_cut);
	check_rows(&bare, may_cut);
	check_dots(&bare);
	check_levels(&bare);
	check_tones(&bare);
	if(!may_cut) check_title(&bare);

	// Also what it said before the first screen, while the fonts were made
	if(complaints > 0) fail("LVGL complained %d times", complaints);
	complaints = 0;

	if(!always && failures != before)
	{
		show(scene);
		if(!write_png(path)) fail("%s could not be written", path);
		print_art();
	}
	return hash;
}

// The name of a screen of the core: that of its file, without the directory and the ending
static const char *name_from(const char *path)
{
	static char name[64];
	const char *slash = strrchr(path, '/');
	size_t length;

	snprintf(name, sizeof(name), "%.60s", slash != NULL ? slash + 1 : path);
	length = strlen(name);
	if(length > 4 && strcmp(name + length - 4, ".txt") == 0) name[length - 4] = '\0';
	return name;
}

int main(int argc, char **argv)
{
	static uint64_t hashes[sizeof(screens) / sizeof(screens[0])];
	static scene_t scene;
	const char *directory = argc > 1 ? argv[1] : "out";
	int by_hand, from_core = 0;
	lv_mem_monitor_t memory;

	setvbuf(stdout, NULL, _IOLBF, 0);
	start();

	for(int i = 0; i < SCREENS; i++)
	{
		screen_name = screens[i].name;
		printf("== %s%s\n", screen_name, screens[i].may_cut ? " (more than a screen can show)" : "");
		screens[i].fill(&scene);
		hashes[i] = examine(&scene, screens[i].may_cut, directory, true);
	}

	// Every screen once more, each after another one than before
	for(int i = SCREENS - 1; i >= 0; i--)
	{
		screen_name = screens[i].name;
		screens[i].fill(&scene);
		show(&scene);
		if(frame_hash() != hashes[i]) fail("the picture differs when the screen is shown after another one");
	}
	check_steady();
	by_hand = failures;

	// The scenes of the core. Each has to fit: what the core makes is what the device shows.
	for(int i = 2; i < argc; i++)
	{
		int before = failures;

		screen_name = name_from(argv[i]);
		read_scene(argv[i], &scene);
		examine(&scene, false, directory, false);
		printf("== %s: %s\n", screen_name, failures == before ? "fits" : "FAILED, see above");
		from_core++;
	}
	if(from_core == 0) printf("No scene of the core was given: only the screens made by hand were looked at\n");

	lv_mem_monitor(&memory);
	printf("LVGL heap: %zu bytes, at most %zu used (%zu %%), %zu free in %zu parts\n", memory.total_size,
	       memory.max_used, memory.max_used * 100 / memory.total_size, memory.free_size, memory.free_cnt);

	printf("%d screens made by hand: %d failures. %d scenes of the core: %d failures. "
	       "%d labels cut that are known not to fit\n", SCREENS, by_hand, from_core, failures - by_hand, known_cuts);
	return failures == 0 ? 0 : 1;
}
