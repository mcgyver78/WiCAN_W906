/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __SCENE_H__
#define __SCENE_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "nav.h"
#include "conn.h"
#include "values.h"
#include "catalog.h"
#include "layout.h"
#include "dtc_flow.h"
#include "dtc_model.h"
#include "dtc_view.h"
#include "hold.h"
#include "access.h"
#include "guard.h"
#include "texts.h"

/*
 * What is on the screen, as plain data: every text, every number, every state the drawing code needs,
 * and nothing it would have to decide. The drawing code (LVGL) only places and paints what is in a scene_t;
 * all rules are here, where they are tested on the host. German texts, UTF-8.
 *
 * Characters used in texts of this module: ASCII, ÄÖÜäöüß, the en dash "–", the ellipsis "…", the middle
 * dot "·" and the degree sign. Texts from the adapter and from the layout are passed on as they are.
 * A text that does not fit its field is cut at a character boundary of UTF-8.
 */

#define SCENE_TEXT_SIZE     96
#define SCENE_SHORT_SIZE    40
#define SCENE_VALUE_SIZE    24
#define SCENE_ROWS_MAX      5       // rows of a list visible at once
#define SCENE_LINES_MAX     4

#define SCENE_DASH          "–"     // shown instead of a value that is not there
#define SCENE_UNAVAILABLE   "n. v." // shown for a value the vehicle profile does not provide

typedef enum
{
	SCENE_VALUES,       // a value page: title, 1 to 6 values, page dots
	SCENE_NOTICE,       // text in the middle of the screen: state of the adapter, failure of a request
	SCENE_LIST,         // rows with a focus: menu, fault memory, info, settings
	SCENE_PROGRESS,     // a fault memory request under way
	SCENE_CHOICE,       // a question with two answers
	SCENE_LEVEL,        // the brightness being set
} scene_kind_t;

typedef enum
{
	SCENE_TONE_NORMAL,
	SCENE_TONE_DIM,     // old value, or values standing still during a scan
	SCENE_TONE_WARN,    // at or beyond a warn limit
	SCENE_TONE_ALARM,   // at or beyond a crit limit
} scene_tone_t;

typedef struct
{
	char label[LAYOUT_TITLE_SIZE];
	char text[SCENE_VALUE_SIZE];    // the value, SCENE_DASH or SCENE_UNAVAILABLE
	char unit[CATALOG_UNIT_SIZE];   // empty if there is none, for a state widget and when text is not a value
	scene_tone_t tone;
	layout_widget_t widget;
	int permille;                   // arc and bar: where the value lies between min and max, 0 to 1000
	                                // (clamped); -1 for other widgets and without a value
} scene_item_t;

typedef enum
{
	SCENE_ROW_ACTION,   // something to choose
	SCENE_ROW_HEAD,     // head line of a list
	SCENE_ROW_LINE,     // a line of a list
	SCENE_ROW_SUB,      // an indented line (a trouble code below its control unit)
} scene_row_kind_t;

typedef struct
{
	scene_row_kind_t kind;
	char text[SCENE_TEXT_SIZE];
	char detail[SCENE_SHORT_SIZE];  // shown smaller, behind or below the text; may be empty
	bool enabled;                   // an action that does something now; lines are always enabled
	bool focus;                     // the knob is on this row
} scene_row_t;

typedef enum
{
	SCENE_OVER_NONE,
	SCENE_OVER_UPLOAD,
	SCENE_OVER_ASK,
	SCENE_OVER_UPDATE,
} scene_over_t;

typedef struct
{
	scene_kind_t kind;
	ring_t ring;                        // texts.h
	char title[SCENE_TEXT_SIZE];
	char note[SCENE_TEXT_SIZE];         // one line at the lower edge of the content, may be empty

	scene_item_t items[LAYOUT_ITEMS_MAX];   // SCENE_VALUES
	int item_count;
	int dots;                           // pages in the rotation of the knob
	int dot;                            // the one shown, 0 to dots - 1; -1 if the page is not in the rotation

	scene_row_t rows[SCENE_ROWS_MAX];   // SCENE_LIST: the rows that are visible
	int row_count;
	int first;                          // index of rows[0] among all rows of the screen
	int total;                          // rows of the screen

	char lines[SCENE_LINES_MAX][SCENE_TEXT_SIZE];   // text lines: the body of NOTICE and CHOICE, below the big
	int line_count;                                 // text of PROGRESS, above the rows of a LIST
	char big[SCENE_SHORT_SIZE];         // SCENE_PROGRESS: "5/18"; SCENE_LEVEL: "80 %"
	int permille;                       // SCENE_PROGRESS: progress; SCENE_LEVEL: the level; SCENE_CHOICE:
	                                    // progress of the hold, -1 if this choice has no hold
	char options[2][SCENE_SHORT_SIZE];  // SCENE_CHOICE
	int option;                         // the one in focus, 0 or 1

	scene_over_t over;                  // what lies over the screen
	char over_lines[3][SCENE_TEXT_SIZE];
	int over_line_count;
	int over_permille;                  // SCENE_OVER_UPLOAD: progress; -1 otherwise
} scene_t;

// Everything the scene is made from. Pointers that are marked "may be NULL" count as "nothing there"; a
// text that is NULL (ap_ssid, ap_password, a text of `info`) counts as empty. What the other pointers lead
// to is taken as its module fills it: counts within their limits, texts with their zero.
typedef struct
{
	const nav_t *nav;
	const nav_world_t *world;           // the same that was passed to nav (layout and catalogue are in it)
	const conn_t *conn;
	const values_t *values;
	const dtc_flow_t *flow;
	dtc_flow_block_t read_block;        // dtc_flow_read_block()
	dtc_flow_block_t clear_block;       // dtc_flow_clear_block()
	const dtc_line_t *list;             // world->list_lines lines (dtc_view_list of the own read), may be NULL
	const dtc_line_t *cleared;          // world->cleared_lines lines (dtc_view_cleared), may be NULL
	const dtc_line_t *old;              // world->old_lines lines, may be NULL
	const dtc_summary_t *summary;       // of the list of the own read, may be NULL
	const hold_t *hold;
	const access_t *access;
	const char *const *info;            // world->info_lines texts for the info screen, may be NULL
	const char *address;                // where the web interface is, e.g. "http://192.168.88.37"; NULL or
	                                    // empty if the display is in no network
	bool ap_on;                         // own access point
	const char *ap_ssid;                // its name and password, shown so that a phone can join
	const char *ap_password;
	bool reverse;                       // settings.reverse
	const char *ask_detail;             // what the browser asks for: the SSID to store, the version of the
	                                    // uploaded firmware; may be NULL
	int upload_percent;                 // of a running firmware upload
	uint32_t update_left_s;             // seconds until an unconfirmed update is taken back
	bool safe_mode;
	guard_heat_t heat;
	uint64_t now_ms;
} scene_input_t;

/*
 * Fills *scene. Every field is set on every call (unused texts empty, unused counts 0, unused permille -1,
 * dot -1 without dots), every byte of the struct: items, rows and lines that are not used are zero, and so
 * is what lies between the fields. Two scenes that show the same are the same memory.
 *
 * What a row does when it is pressed is decided by nav.h from the world, so whether a row is enabled comes
 * from the world as well (can_read, can_clear, flow, old_lines, previous_firmware). Phase, number and reason
 * of the own request come from `flow`.
 *
 * ring: ring_state() with the view of the connection, its state, and - on a value page - the worst level
 * and whether a value of the page is old; level 0 and not old on every other screen. Only a value that is
 * shown counts (LIVE or OLD, with a text): one that became a dash raises no level, although values.h still
 * has its last number.
 *
 * NAV_PAGES
 *   The view of the connection decides (conn_view):
 *   LIVE, SCAN, NO_API          SCENE_VALUES of page nav->page
 *   every other view            SCENE_NOTICE, title empty, line 1 text_view(view); for ECU_OFFLINE line 2
 *                               "Bordnetz 12,4 V" if the state has a battery voltage (one decimal, comma)
 *   nav->page is -1             SCENE_NOTICE with the line "Keine Ansicht mit verfügbaren Werten"
 *   or outside the layout       (in the views LIVE, SCAN and NO_API; else the notice of the view)
 *   The battery voltage is rounded to a tenth of a volt; there is one if batt_mv is 0 or more.
 *   SCENE_VALUES: title = title of the page. Per item, by layout_item_state():
 *     LIVE         text = layout_item_text(), tone by layout_item_level(): 2 ALARM, 1 WARN, else NORMAL
 *     OLD          the same text, tone DIM
 *     NO_VALUE     text SCENE_DASH, tone DIM
 *     UNAVAILABLE  text SCENE_UNAVAILABLE, tone DIM
 *     A value whose text cannot be made (layout_item_text() false) counts as NO_VALUE.
 *     In the view SCAN every tone that is not DIM becomes DIM (the values stand still).
 *     label = the label of the item, or fmt_label() of the key if it has none (empty if that does not fit).
 *     unit = layout_item_unit(); empty for a state widget and when the text is the dash or SCENE_UNAVAILABLE.
 *     widget as in the layout (what is no member of the enum becomes number, as layout.h shows it).
 *     permille for arc and bar with a value: (shown value - min) * 1000 / (max - min), rounded down,
 *     clamped to 0..1000; the shown value is value * scale, on = 1, off = 0. -1 without a range: min and
 *     max have to be set, min below max (layout_parse() sees to that), and the calculation must not leave
 *     the numbers of a double (limits of about 1e305 and beyond are no range).
 *   For every scene of NAV_PAGES, the notices as well:
 *   dots = pages for which layout_page_shown() holds, dot = position of nav->page among them, -1 if it is
 *   not one of them (a page that is hidden or has no value in the catalogue is still shown, until
 *   nav_tick() leaves it).
 *   note, the first that applies: "Sicherer Modus – eingebaute Ansichten" in safe mode; "Zu heiß – Anzeige
 *   gedimmt" if heat is not GUARD_HEAT_NORMAL; text_view(view) for SCAN and NO_API; else empty.
 * NAV_MENU       SCENE_LIST "Menü": "Fehlerspeicher"; "Helligkeit" detail "80 %" (world->brightness);
 *                "Nachtmodus" detail "an" / "aus"; "Web-Zugriff" detail "frei" / "gesperrt"
 *                (world->release_open); "Info"; "Einstellungen"; "Zurück"
 * NAV_DTC        SCENE_LIST "Fehlerspeicher": "Lesen" (enabled if can_read); "Liste ansehen" (enabled if
 *                world->flow is LIST, CLEARED, FAILED or UNKNOWN); "Zuletzt gelöscht" (enabled if old_lines > 0);
 *                "Zurück". note = text_block(read_block), empty if allowed.
 * NAV_DTC_BUSY   SCENE_PROGRESS, title "Fehlerspeicher lesen" (flow READ_SENT, READING) or "Fehlerspeicher
 *                löschen" (CLEAR_SENT, CLEARING); in every other phase title "Fehlerspeicher", big "…",
 *                permille 0 and no line (nothing is under way; nav_tick() leaves the screen).
 *                If the state of the adapter shows the own request (dtc.seq equals flow->seq, flow in READING
 *                or CLEARING) running, or done with the result still on its way: big "5/18" (dtc.step "/"
 *                dtc.total), line 1 the short name of the control unit (dtc_short_name of dtc.name, empty if
 *                the state names none), or "Prüfe Motor …" at step 0; permille = step * 1000 / total (0 if
 *                total is 0, at most 1000). Otherwise big "…", line 1 "Auftrag gesendet", permille 0. The
 *                last line is then always "ca. 35 s – Live-Werte pausieren".
 * NAV_DTC_LIST   SCENE_LIST "Fehlerspeicher": the lines of `list`, then "Erneut lesen" (enabled if can_read),
 *                "Fehler löschen" (enabled if can_clear), "Zurück".
 *                note: if can_clear "Löschen möglich: 9:12" (dtc_flow_seconds_left() as minutes:seconds, two
 *                digits for the seconds), else text_block(clear_block).
 * NAV_DTC_CONFIRM SCENE_CHOICE "Fehler löschen?"; lines: "3 Fehler in 2 Steuergeräten" (summary->codes and
 *                summary->ecus_with_codes; "1 Steuergerät"; the line is left out without a summary),
 *                "Betrifft alle Steuergeräte, auch SRS und ESP.", "Zündung an, Motor aus, Fahrzeug steht.";
 *                options "Abbrechen", "Löschen"; option = nav->row, kept within 0 and 1; permille =
 *                hold_permille();
 *                note "Auf Löschen drehen, Knopf 3 s halten"
 * NAV_DTC_CLEARED SCENE_LIST "Gelöscht": the lines of `cleared`, then "Fertig"
 * NAV_DTC_FAILED SCENE_NOTICE. Flow FAILED: title "Fehlgeschlagen", line 1 text_reason(flow->reason) or the
 *                reason word itself if it has no text; for the reason "not_ready" while the state of the
 *                adapter says sleep_in_s 0: "WiCAN schaltet ab – später erneut lesen" (the adapter gives
 *                this reason both while it starts and when it is about to sleep). Flow UNKNOWN: title "Stand unbekannt", line 1 "Stand
 *                des Löschens unbekannt – bitte erneut lesen". Any other phase: title "Fehlerspeicher", no
 *                line. note "Knopf drücken" in every phase.
 * NAV_DTC_OLD    SCENE_LIST "Zuletzt gelöscht": the lines of `old`, then "Zurück"
 * NAV_BRIGHTNESS SCENE_LEVEL "Helligkeit", in night mode "Helligkeit (Nacht)"; big "80 %" (nav->value);
 *                permille = nav->value * 10, kept within 0..1000; note "Drehen zum Ändern, Drücken zum
 *                Speichern"
 * NAV_WEB        SCENE_LIST "Web-Zugriff": "Freigabe" detail "an – noch 9:12" (access_seconds_left()) or
 *                "aus" (when that is 0); "Zurück". lines: the address, or "Kein WLAN" without one; and with
 *                the own access point on: "WLAN: <ap_ssid>" and "Passwort: <ap_password>".
 * NAV_INFO       SCENE_LIST "Info": the texts of `info` as rows of the kind SCENE_ROW_LINE
 * NAV_SETTINGS   SCENE_LIST "Einstellungen": "Drehrichtung" detail "umgekehrt" / "normal"; "Hotspot" detail
 *                "an" / "aus"; "Neustart"; "Vorherige Version" (enabled if previous_firmware);
 *                "Werkseinstellungen"; "Zurück"
 * NAV_CONFIRM    SCENE_CHOICE, by nav->confirm: NAV_DO_REBOOT "Neu starten?"; NAV_DO_PREVIOUS_FIRMWARE
 *                "Vorherige Version starten?"; NAV_DO_FACTORY_RESET "Werkseinstellungen?" with the lines
 *                "WLAN, Kopplung und Einstellungen werden gelöscht." and "Die Ansichten bleiben.";
 *                options "Abbrechen", "Ausführen"; option = nav->row, kept within 0 and 1; permille -1.
 *                Any other value of nav->confirm: no title, no line.
 * A screen that is no member of nav_screen_t: SCENE_NOTICE without any text.
 *
 * Lists: the lines of a fault memory list become rows by their kind: HEAD -> SCENE_ROW_HEAD, CODE ->
 * SCENE_ROW_SUB, everything else SCENE_ROW_LINE, with text and detail of the line; the choices are
 * SCENE_ROW_ACTION. total = all rows of the screen (nav_rows()). Visible are at most SCENE_ROWS_MAX rows
 * with the focused row (nav->row) in the middle where that is possible: first = nav->row - 2, kept within
 * 0 and total - SCENE_ROWS_MAX (0 if total is smaller). A focus that lies on no row (a list became shorter
 * and nav_tick() has not run yet) leaves every row without it. A line pointer that is NULL counts as no
 * lines even if the world names some: the rows are then the choices alone, and total counts only them.
 *
 * Overlay (nav_overlay()):
 *   UPLOAD   "Firmware wird übertragen", "42 %" (upload_percent, kept within 0..100); over_permille = ten
 *            times that
 *   ASK      line 1 by the question (world->asking): WIFI "WLAN speichern?", FIRMWARE "Firmware
 *            installieren?", RESET "Werkseinstellungen?", empty for any other value; line 2 ask_detail if
 *            there is one (not NULL, not empty); then "Drücken = ja · lang = nein (42 s)" with
 *            access_ask_seconds_left()
 *   UPDATE   "Update in Ordnung?", "Knopf drücken oder Bildschirm berühren", "sonst alte Version in 4:12"
 *            (update_left_s as minutes:seconds)
 * The screen below is filled as without the overlay.
 */
void scene_build(const scene_input_t *input, scene_t *scene);

// The scene as text, for the tests and for a look at a screen without a display: one "name: value" line per
// field that is in use, in the order of the struct; items and rows one line each. Every line ends with "\n".
//   kind: values                  always; values, notice, list, progress, choice, level
//   ring: progress 277            always; none, yellow, grey, red, progress - the permille only for progress
//   title: Motor                  always
//   note:                         always; an empty text leaves no blank behind the colon, here and below
//   item: Drehzahl | 812 | 1/min | normal | arc | 162
//                                 item_count times: label | text | unit | tone (normal, dim, warn, alarm) |
//                                 widget (number, arc, bar, state) | permille
//   dots: 2/7                     dot + 1 "/" dots: the second of seven. Not for dots 0 with dot -1.
//   row: > action | Lesen |  | enabled
//                                 row_count times: ">" marks the focus, "-" a row without it; kind (action,
//                                 head, line, sub) | text | detail | enabled or disabled
//   first: 0                      these two unless both are 0
//   total: 4
//   line: WiCAN antwortet nicht   line_count times
//   big: 5/18                     unless empty
//   permille: 277                 unless -1
//   option: > Abbrechen           both options unless both are empty; ">" marks the one in focus
//   option: - Löschen
//   over: ask                     unless none; upload, ask, update
//   over_line: WLAN speichern?    over_line_count times
//   over_permille: 420            unless -1
// A member of an enum that is none is written as "?". A count beyond its array counts as the size of the
// array, one below 0 as 0; a text without its zero ends with its field; a focus or an enabled that holds
// another byte than 0 and 1 counts as true: every scene_t can be written, also one that scene_build() did
// not fill. SCENE_DUMP_SIZE is enough for each (the largest dump has 2734 bytes).
// Returns the length, -1 if it does not fit (out is then an empty string; with size 0 nothing is written and
// out may be NULL).
#define SCENE_DUMP_SIZE     4096
int scene_dump(const scene_t *scene, char *out, size_t size);

#endif
