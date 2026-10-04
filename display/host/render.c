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
#include <math.h>
#include "lvgl.h"
#include "ui.h"

/*
 * Draws every kind of screen without a display and looks at what came out. LVGL renders into a buffer of
 * 480 x 480 pixels in the memory, with the configuration of the firmware (lv_conf.h); the scenes are
 * filled in by hand below, one for everything that looks different.
 *
 *   render [directory]      writes <name>.png into the directory (default: out), prints every screen as
 *                           text into the log, and ends with status 1 if a check failed
 *
 * For every screen:
 *   - nothing but the ring reaches outside the circle of radius 236
 *   - no two texts overlap, and no text lies half on a bar, an arc or a box
 *   - every text stands out from what it stands on, and nothing shows through an overlay
 *   - every text of the scene is on the screen, whole; for the screens marked `may_cut`, which hold more
 *     than a screen can show, every text is at least the beginning of one of the scene, with an ellipsis
 *   - a list shows the marks for more rows exactly when there are more
 *   - ui_row_at() names the right row at the middle of every row and of both options, and none elsewhere
 *   - the ring is where the scene says: sampled all around
 *   - LVGL did not complain (a character the font does not have, memory)
 *   - the screen looks the same whatever was shown before it: all are drawn a second time in reverse order
 * The parts are looked at one by one: all others are hidden, the screen is rendered and the pixels that
 * are lit are the part. So the checks see what a person sees, not the boxes LVGL reckons with.
 *
 * What it cannot tell: whether a screen reads well, whether a colour is the right one (only the ring is
 * looked at for that, and a text for being bright or dark enough against its ground), and how any of it
 * looks on the panel. For that there are the pictures, and in the end the device.
 */

#define SIZE            480
#define CENTRE          240
#define R_LIMIT         236
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

// A page of the built-in layout as it is: among six values there is no room for labels of twenty letters
static void values_six_long_labels(scene_t *scene)
{
	fresh(scene, SCENE_VALUES, "Ladeluft");
	dots(scene, 1, 7);
	item(scene, "Ladedruck", "1013", "hPa", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Ladedruck Niederdruck", "1004", "hPa", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Ladeluft vor Kühler", "64", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Ladeluft nach Kühler", "31", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Ansaugluft", "24", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
	item(scene, "Wastegate", "37", "%", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1);
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

// Four rows, the focus on an action that is not offered, and the longest reason for it
static void list_dtc_blocked(scene_t *scene)
{
	fresh(scene, SCENE_LIST, "Fehlerspeicher");
	window(scene, 0, 4);
	row(scene, SCENE_ROW_ACTION, "Lesen", "", false, true);
	row(scene, SCENE_ROW_ACTION, "Liste ansehen", "", false, false);
	row(scene, SCENE_ROW_ACTION, "Zuletzt gelöscht", "", true, false);
	row(scene, SCENE_ROW_ACTION, "Zurück", "", true, false);
	PUT(scene->note, "Zündung aus – Motorsteuergerät offline");
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
	fresh(scene, SCENE_LIST, "Zuletzt gelöscht");
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

static void progress_running(scene_t *scene)
{
	fresh(scene, SCENE_PROGRESS, "Fehlerspeicher löschen");
	ring(scene, RING_PROGRESS, 277);
	PUT(scene->big, "5/18");
	scene->permille = 277;
	line(scene, "Collision Prevention Assist");
	line(scene, "ca. 35 s – Live-Werte pausieren");
}

static void progress_done(scene_t *scene)
{
	fresh(scene, SCENE_PROGRESS, "Fehlerspeicher lesen");
	ring(scene, RING_PROGRESS, 1000);
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
	{"values_four_hot", values_four_hot, false},
	{"values_five", values_five, false},
	{"values_six", values_six, false},
	{"values_six_alarm", values_six_alarm, false},
	{"values_six_old", values_six_old, false},
	{"values_six_scan", values_six_scan, false},
	{"values_six_no_api", values_six_no_api, false},
	{"values_six_long_labels", values_six_long_labels, true},
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
	{"list_dtc_top", list_dtc_top, false},
	{"list_dtc_middle", list_dtc_middle, false},
	{"list_dtc_end", list_dtc_end, false},
	{"list_cleared", list_cleared, false},
	{"list_old", list_old, false},
	{"list_web", list_web, false},
	{"list_info", list_info, false},
	{"list_settings", list_settings, false},
	{"list_empty", list_empty, false},
	{"list_longest", list_longest, true},
	{"progress_sent", progress_sent, false},
	{"progress_running", progress_running, false},
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
	int x0, y0, x1, y1;     // around its pixels; x1 < x0: it lights none
	int count;
	bool used;              // a text: it stands for a text of the scene
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

		for(int i = 0; i < (int)lv_obj_get_child_count(parent); i++)
		{
			lv_obj_t *object = lv_obj_get_child(parent, i);
			thing_t *thing = &things[thing_count];

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
			thing->kind = lv_obj_check_type(object, &lv_label_class) ? THING_TEXT :
			              lv_obj_check_type(object, &lv_arc_class) ? THING_ARC : THING_BOX;
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

static void check_circle(void)
{
	for(int i = 0; i < thing_count; i++)
	{
		const thing_t *thing = &things[i];
		bool outside = false;

		if(thing->count == 0)
		{
			fail("%s is on the screen but lights no pixel", name_of(thing));
			continue;
		}
		for(int y = thing->y0; y <= thing->y1 && !outside; y++)
		{
			for(int x = thing->x0; x <= thing->x1 && !outside; x++)
			{
				// Twice the distance of the middle of the pixel from the middle of the screen
				int dx = 2 * x + 1 - SIZE, dy = 2 * y + 1 - SIZE;

				if(is_lit(thing, x, y) && dx * dx + dy * dy > 4 * R_LIMIT * R_LIMIT)
				{
					fail("%s reaches outside the circle of radius %d at (%d, %d)", name_of(thing), R_LIMIT, x, y);
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
		if(found >= 0) things[found].used = true;
		else if(!may_cut) fail("\"%s\" is not on the screen, or not whole", texts[i]);
	}

	// What is left has to be the beginning of a text of the scene
	for(int i = 0; i < thing_count; i++)
	{
		thing_t *thing = &things[i];
		bool known = false;

		if(thing->kind != THING_TEXT || thing->used) continue;
		for(int at = 0; at < count && !known; at++) known = shows(text_of(thing), texts[at]) == 1;
		if(known && may_cut) cut++;
		else if(known) fail("%s is cut", name_of(thing));
		else fail("%s is no text of the scene", name_of(thing));
	}
	if(may_cut) printf("  %d texts cut\n", cut);
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

static void check_rows(const scene_t *scene)
{
	bool covered = scene->over != SCENE_OVER_NONE;
	int before = thing_count;
	char what[160];

	probe("the top of the screen", CENTRE, 12, -1);
	if(scene->kind == SCENE_LIST)
	{
		for(int i = scene->row_count - 1; i >= 0; i--)
		{
			const thing_t *label;

			// The detail was put after the text
			if(scene->rows[i].detail[0] != '\0') label_of(scene->rows[i].detail, &before);
			label = label_of(scene->rows[i].text, &before);
			if(label == NULL || label->count == 0) continue;
			snprintf(what, sizeof(what), "row %d, \"%.100s\"", scene->first + i, scene->rows[i].text);
			probe(what, (label->x0 + label->x1) / 2, (label->y0 + label->y1) / 2, covered ? -1 : scene->first + i);
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
		}
	}
	else
	{
		probe("the middle of a screen without rows", CENTRE, CENTRE, -1);
	}
}

// The ring is looked at in the frame as the scene has it: in the middle of its band, every five degrees
static void check_ring(const scene_t *scene)
{
	static const char *const names[] = {"dark", "red", "amber", "grey or white"};
	int sweep = scene->ring.kind == RING_PROGRESS ? scene->ring.permille * 360 / 1000 : 360;

	for(int angle = 0; angle < 360; angle += 5)
	{
		double turn = angle * 3.14159265358979 / 180.0;
		int x = (int)(CENTRE + 237.0 * sin(turn)), y = (int)(CENTRE - 237.0 * cos(turn));
		uint16_t pixel = frame[y * SIZE + x];
		int green = (pixel >> 5) & 63, blue = pixel & 31;
		int seen = pixel == 0 ? 0 : green < 16 && blue < 8 ? 1 : blue < 8 ? 2 : 3;
		int expected = 0;

		switch(scene->ring.kind)
		{
			case RING_RED:
				expected = 1;
				break;
			case RING_YELLOW:
				expected = 2;
				break;
			case RING_GREY:
				expected = 3;
				break;
			case RING_PROGRESS:
				// The end of the arc may fall on either side of a sample next to it
				if(angle > sweep - 4 && angle < sweep + 4) continue;
				expected = angle < sweep ? 3 : 0;
				break;
			default:
				break;
		}
		if(seen != expected)
		{
			fail("the ring is %s at %d degrees from the top, not %s", names[seen], angle, names[expected]);
			return;
		}
	}
}

// Nothing but the ring out there, whatever it belongs to
static void check_edge(void)
{
	for(int y = 0; y < SIZE; y++)
	{
		for(int x = 0; x < SIZE; x++)
		{
			int dx = 2 * x + 1 - SIZE, dy = 2 * y + 1 - SIZE;

			if(frame[y * SIZE + x] != 0 && dx * dx + dy * dy > 4 * R_LIMIT * R_LIMIT)
			{
				fail("something is drawn outside the circle of radius %d at (%d, %d)", R_LIMIT, x, y);
				return;
			}
		}
	}
}

static void show(const scene_t *scene)
{
	ui_show(scene);
	lv_refr_now(display);
}

int main(int argc, char **argv)
{
	static uint64_t hashes[sizeof(screens) / sizeof(screens[0])];
	static scene_t scene, bare;
	const char *directory = argc > 1 ? argv[1] : "out";
	lv_mem_monitor_t memory;
	char path[512];

	setvbuf(stdout, NULL, _IOLBF, 0);
	start();

	for(int i = 0; i < SCREENS; i++)
	{
		screen_name = screens[i].name;
		printf("== %s%s\n", screen_name, screens[i].may_cut ? " (more than a screen can show)" : "");
		screens[i].fill(&scene);

		show(&scene);
		hashes[i] = frame_hash();
		snprintf(path, sizeof(path), "%s/%s.png", directory, screen_name);
		if(!write_png(path)) fail("%s could not be written", path);
		print_art();
		check_ring(&scene);

		// The same scene again changes nothing
		ui_show(&scene);
		lv_refr_now(display);
		if(frame_hash() != hashes[i]) fail("showing the same scene again changes the picture");

		// The parts are looked at without the ring, which is the one thing that may be at the edge
		bare = scene;
		bare.ring.kind = RING_NONE;
		bare.ring.permille = 0;
		show(&bare);
		check_edge();
		collect();
		look();
		check_seen(&bare);
		check_circle();
		check_overlap();
		check_texts(&bare, screens[i].may_cut);
		check_rows(&bare);

		// Also what it said before the first screen, while the fonts were made
		if(complaints > 0) fail("LVGL complained %d times", complaints);
		complaints = 0;
	}

	// Every screen once more, each after another one than before
	for(int i = SCREENS - 1; i >= 0; i--)
	{
		screen_name = screens[i].name;
		screens[i].fill(&scene);
		show(&scene);
		if(frame_hash() != hashes[i]) fail("the picture differs when the screen is shown after another one");
	}

	lv_mem_monitor(&memory);
	printf("LVGL heap: %zu bytes, at most %zu used (%zu %%), %zu free in %zu parts\n", memory.total_size,
	       memory.max_used, memory.max_used * 100 / memory.total_size, memory.free_size, memory.free_cnt);

	printf("%d screens, %d failures\n", SCREENS, failures);
	return failures == 0 ? 0 : 1;
}
