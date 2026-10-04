/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __LAYOUT_H__
#define __LAYOUT_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "values.h"
#include "catalog.h"
#include "json.h"

/*
 * The views of the display: which values are shown together on which page. The user edits them in the
 * browser; the layout is stored on the display as the JSON text below and can be exported and shared.
 * The geometry is NOT part of it: the display places 1 to 6 values of a page by fixed templates, so
 * nobody can build a page that is unreadable on a 2.1 inch round screen.
 *
 *   {"format":"wican-display-layout","v":1,"name":"W906 OM651 Standard","profile_hint":"W906","pages":[
 *     {"title":"Motor","items":[
 *       {"key":"ENGINE_RPM","label":"Drehzahl","unit":"1/min","dec":0,"widget":"arc","min":0,"max":5000},
 *       {"key":"COOLANT_TMP","label":"Kühlwasser","unit":"°C"},
 *       {"key":"RAIL_PRESSURE","label":"Raildruck","unit":"bar","scale":0.001,"dec":0,"warn_hi":1900},
 *       {"key":"DPF_REGEN_STATUS","label":"Regeneration","widget":"state","map":{"1":"inaktiv","*":"aktiv"}}]},
 *     {"title":"DPF","hidden":true,"items":[...]}]}
 *
 * Examples: display/layouts/w906_default.json and display/test/fixtures/layout_*.json.
 */

#define LAYOUT_FORMAT       "wican-display-layout"
#define LAYOUT_VERSION      1
#define LAYOUT_TEXT_MAX     16384   // bytes of the JSON text
#define LAYOUT_PAGES_MAX    12
#define LAYOUT_ITEMS_MAX    6
#define LAYOUT_MAP_MAX      8
#define LAYOUT_TITLE_SIZE   25      // titles and labels up to 24 bytes
#define LAYOUT_UNIT_SIZE    9       // units up to 8 bytes
#define LAYOUT_NAME_SIZE    33
#define LAYOUT_MAP_RAW_SIZE 12
#define LAYOUT_MAP_TEXT_SIZE 24
#define LAYOUT_TOKENS       4096    // tokens layout_parse() needs for the largest layout

typedef enum
{
	LAYOUT_WIDGET_NUMBER,
	LAYOUT_WIDGET_ARC,
	LAYOUT_WIDGET_BAR,
	LAYOUT_WIDGET_STATE,
} layout_widget_t;

typedef struct
{
	bool set;
	double value;
} layout_limit_t;

typedef struct
{
	char raw[LAYOUT_MAP_RAW_SIZE];      // value as text, e.g. "1", "on", or "*" for every other value
	char text[LAYOUT_MAP_TEXT_SIZE];
} layout_map_t;

typedef struct
{
	char key[VALUE_NAME_SIZE];
	char label[LAYOUT_TITLE_SIZE];      // empty: the display makes one from the key
	char unit[LAYOUT_UNIT_SIZE];
	bool has_unit;                      // false: the unit of the catalogue is used
	uint8_t decimals;                   // "dec", 0 to 3, default 0
	layout_widget_t widget;
	double scale;                       // shown value = value * scale, default 1
	layout_limit_t min, max;            // range of arc and bar
	layout_limit_t warn_lo, warn_hi, crit_lo, crit_hi;     // limits of the shown (scaled) value
	layout_map_t map[LAYOUT_MAP_MAX];   // texts of a state widget
	uint8_t map_count;
} layout_item_t;

typedef struct
{
	char title[LAYOUT_TITLE_SIZE];
	bool hidden;
	layout_item_t items[LAYOUT_ITEMS_MAX];
	uint8_t item_count;
} layout_page_t;

// layout_parse() and layout_from_catalog() fill a layout completely: counts within their limits, texts
// terminated, every byte they do not use zero. The functions that read a layout expect its counts to be
// within their limits and its texts to be terminated.
typedef struct
{
	char name[LAYOUT_NAME_SIZE];
	char profile_hint[LAYOUT_NAME_SIZE];
	layout_page_t pages[LAYOUT_PAGES_MAX];
	uint8_t page_count;
} layout_t;

// Why a layout was refused, and what was put right silently
typedef struct
{
	char path[48];          // where, e.g. "pages[2].items[0].min", or "pages[0].items[3].map[1]" for the second
	                        // entry of a map; empty if the layout is fine or the text as a whole is refused
	char problem[64];       // short English text, e.g. "min is not below max", empty if the layout is fine
	int warnings;           // things that were accepted with a correction, 0 if the layout is refused
	char warning_path[48];  // the first of them
	char warning[64];
} layout_report_t;

// Reads and checks a layout. Returns true and fills *layout if it can be used; false with report->path
// and report->problem set otherwise (*layout is then untouched). report may be NULL.
//
// Refused: text longer than LAYOUT_TEXT_MAX or not a JSON object; "format" not LAYOUT_FORMAT; "v" not an
// integer or above LAYOUT_VERSION (a layout of a newer display) or below 1; "pages" missing, empty or
// more than LAYOUT_PAGES_MAX; a page without "items", with none or more than LAYOUT_ITEMS_MAX; an item
// without "key" or with a key that is empty or longer than 32 bytes; a text that is too long for its field
// (title, label 24 bytes; unit 8; name, profile_hint 32; map value 11, map text 23) or contains a control
// character (a byte below 0x20, or 0x7F) or half a surrogate pair; "dec" not an integer 0 to 3; "scale" zero
// or not finite; "min" not below "max" when both are given; a limit or number that is not a number or not
// finite; "hidden" not a boolean; "map" not an object of texts or with more than LAYOUT_MAP_MAX entries; a
// member of the wrong type in general.
//
// Of several problems one is reported: the first in the order format, v, name, profile_hint, pages; of a
// page title, hidden, items; of an item key, label, unit, dec, widget, scale, min, max (then their order),
// warn_lo, warn_hi, crit_lo, crit_hi, map. Pages and items are gone through in the order of the text.
//
// Accepted with a warning: an unknown "widget" text (becomes number); "arc" or "bar" without both "min"
// and "max" (becomes number); "state" without "map" or with an empty one (becomes number).
// Members the format does not know are ignored without a warning: a later version may add some.
// "title", "label", "name", "profile_hint" are optional and default to empty.
// work, work_count: room for the JSON reader (json.h), provided by the caller because it is too large for
// a task stack. A text that needs more tokens than that is treated like broken JSON.
// layout_t is large (about 35 KB): never put it on a task stack.
bool layout_parse(const char *json, size_t length, layout_t *layout, layout_report_t *report,
                  json_token_t *work, int work_count);

// A page takes part in the rotation of the knob if it is not hidden and at least one of its values is in
// the catalogue. With a catalogue that holds nothing but CATALOG_BATTERY (not loaded yet) every page that
// is not hidden counts: the display shows dashes instead of jumping around when the catalogue arrives.
bool layout_page_shown(const layout_t *layout, int page, const catalog_t *catalog);

// The first shown page, -1 if there is none
int layout_first_page(const layout_t *layout, const catalog_t *catalog);

// The next shown page in a direction (+1 or -1) from `page`. Hard ends, no wrap: at the last shown page
// +1 returns `page` itself, likewise -1 at the first. If `page` itself is not shown the nearest shown one
// in that direction is returned, or the nearest in the other direction, or -1 if none is shown.
// Of another direction only the sign counts, 0 is +1.
int layout_step_page(const layout_t *layout, const catalog_t *catalog, int page, int direction);

// true if the layout suits the vehicle: the catalogue is not loaded yet (see above), or at least half of
// the distinct keys of the layout are in it. Decides between the built-in W906 layout and
// layout_from_catalog() when the user has not stored a layout.
bool layout_suits(const layout_t *layout, const catalog_t *catalog);

// A layout for a vehicle nobody made one for: the entries of the catalogue in its order, 4 per page,
// titles "Werte 1", "Werte 2", ..., labels by fmt_label() (empty if longer than 24 bytes), unit from the
// catalogue (has_unit false), widget number, 1 decimal. CATALOG_BATTERY comes last. At most
// LAYOUT_PAGES_MAX pages: what finds no room is left out.
// A name that layout_parse() would not take as a key (empty, or with a control character) is left out as
// well: the layout has to come back from the editor in the browser. A catalogue without a name that can be
// used gives a layout without pages.
void layout_from_catalog(const catalog_t *catalog, layout_t *layout);

// The longest text layout_to_json() writes for a layout of layout_from_catalog(), without its zero: 48
// items whose names are 24 quotes and 8 underscores
#define LAYOUT_GENERATED_TEXT_MAX   6844

// The layout as its JSON text, for a layout that has no text of its own (layout_from_catalog()): the editor
// in the browser gets the views in use as a text, whatever they were made from.
// Without whitespace, the members in this order, each only if it says something:
//   {"format":"wican-display-layout","v":1,"name":"..","profile_hint":"..","pages":[
//     {"title":"..","hidden":true,"items":[
//       {"key":"..","label":"..","unit":"..","scale":N,"dec":N,"widget":"..","min":N,"max":N,
//        "warn_lo":N,"warn_hi":N,"crit_lo":N,"crit_hi":N,"map":{"1":"..","*":".."}}]}]}
// Left out: "name", "profile_hint", "title" and "label" that are empty; "hidden" of a page that is not
// hidden; "unit" unless has_unit (then an empty one is written too); "scale" 1; "dec" 0; "widget" of a
// number widget (and of a value that is none of the enum); a limit that is not set; a map without entries.
// Texts: " and \ get a backslash, a byte below 0x20 becomes \u00xx (lower case), every other byte is
// passed on.
// Numbers: printf with "%.15g" if the JSON reader takes that text for the same double, else with "%.17g":
// 0.001 is "0.001", 1e15 is "1e+15", a third is "0.33333333333333331". Each text is read back before it is
// used and not trusted: a printf that rounds wrongly cannot change a layout.
// Returns the length of the text, or -1 if it does not fit with its zero, or if a number is not finite or
// does not come back as itself (out is then an empty string; with size 0 nothing is written and out may be
// NULL). No byte from out[size] on is touched.
//
// layout_parse() reads the text back as the same layout, field by field, if the layout was read by
// layout_parse() or made by layout_from_catalog() with at least one page, and the text is not longer than
// LAYOUT_TEXT_MAX. For a layout of layout_from_catalog() that is certain (LAYOUT_GENERATED_TEXT_MAX). For
// a layout that was read from a text it is NOT: texts and members never come out longer than they went in,
// but numbers do ("1e14" comes out as "100000000000000"), and a text of LAYOUT_TEXT_MAX bytes can come out
// with a third more. So LAYOUT_TEXT_MAX + 1 bytes are enough for a generated layout and for the built-in
// one; whoever holds the text a layout was read from keeps that text instead of writing a new one.
// A layout built by hand is written as it stands, also where layout_parse() would refuse it or put it
// right (no page, an arc without a range, 4 decimals, an empty key).
int layout_to_json(const layout_t *layout, char *out, size_t size);

typedef enum
{
	LAYOUT_ITEM_LIVE,           // value there and fresh
	LAYOUT_ITEM_OLD,            // value there, VALUE_AGE_OLD: shown dimmed
	LAYOUT_ITEM_NO_VALUE,       // key in the catalogue (or catalogue not loaded yet) but no usable value: a dash
	LAYOUT_ITEM_UNAVAILABLE,    // key not in the catalogue: "n. v." (the profile does not provide it)
} layout_item_state_t;

layout_item_state_t layout_item_state(const layout_item_t *item, const catalog_t *catalog,
                                      const values_t *values, uint64_t now_ms);

// The text to show for a value. Number widgets (every widget but state, also a value that is none of the
// enum): value * scale by fmt_number() with the decimals of the item. VALUE_ON / VALUE_OFF count as the
// raw texts "on" / "off" and as the numbers 1 / 0; a kind that is none of the three counts as off.
// State widget: the text of the map entry whose raw text equals the value ("on", "off", or the number
// printed without decimals when it has none, else with fmt_number and 3 decimals trimmed of trailing
// zeros), else the text of the "*" entry, else as a number widget.
// Returns false and an empty string if there is no value, fmt_number() refuses the number (not finite, or
// 1e12 and more), or the text does not fit.
bool layout_item_text(const layout_item_t *item, const value_t *value, char *out, size_t size);

// 0: within the limits, 1: at or beyond a warn limit, 2: at or beyond a crit limit. Compared is the
// scaled value. No value or no limits: 0.
int layout_item_level(const layout_item_t *item, const value_t *value);

// Unit to show: the one of the item if it has one, else the one of the catalogue, else empty
const char *layout_item_unit(const layout_item_t *item, const catalog_t *catalog);

#endif
