/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __CATALOG_H__
#define __CATALOG_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "values.h"
#include "json.h"

/*
 * The values the adapter can deliver for the loaded vehicle profile, with their units. Nothing about a
 * vehicle is built into the display: the catalogue comes from the WiCAN at run time, so other profiles
 * work as well. Sources:
 *   - GET /load_car_config: {"NAME":{"class":"temperature","unit":"°C"}, ...} for every value of the
 *     profile, also with the ignition off (tools/w906/fixtures/car_config_w906.json)
 *   - the names that actually arrive with GET /autopid_data
 *   - the battery voltage of the adapter from GET /api/state under the name CATALOG_BATTERY
 * The last catalogue is stored on the display, so that the views can be edited while the adapter sleeps.
 */

#define CATALOG_MAX         96
#define CATALOG_UNIT_SIZE   12
#define CATALOG_CLASS_SIZE  24
#define CATALOG_BATTERY     "@BATT_V"
#define CATALOG_TOKENS      (10 * CATALOG_MAX + 64) // tokens the two readers below need: catalog_to_json
                                                    // writes 10 per entry

typedef struct
{
	char name[VALUE_NAME_SIZE];
	char unit[CATALOG_UNIT_SIZE];
	char value_class[CATALOG_CLASS_SIZE];
	bool in_profile;        // named by /load_car_config
	bool delivered;         // arrived with values at least once since the catalogue was (re)loaded
} catalog_entry_t;

typedef struct
{
	catalog_entry_t entries[CATALOG_MAX];
	int count;
	uint32_t dropped;       // names that found no room or are too long (or that json_text() refuses),
	                        // counted every time, since catalog_init or catalog_from_json
} catalog_t;

// An empty catalogue with the entry CATALOG_BATTERY (unit "V", in_profile false)
void catalog_init(catalog_t *catalog);

// Replaces the profile part by the answer of GET /load_car_config: entries that are no longer named lose
// in_profile and are removed unless they were delivered (CATALOG_BATTERY always stays); new ones are
// appended in the order of the text; unit and class of known ones are updated. "unit" and "class" are
// optional texts (without one the field is empty), longer ones are cut at a character boundary, also
// before a \u0000 or half a surrogate pair. Members whose value is no object are ignored.
// Returns false and changes nothing if the text is not a JSON object.
// work, work_count: room for the JSON reader (json.h), provided by the caller because it is too large for
// a task stack. A text that needs more tokens than that is treated like broken JSON.
bool catalog_apply_config(catalog_t *catalog, const char *json, size_t length, json_token_t *work, int work_count);

// Marks every value that is there as delivered and appends names the catalogue does not know yet
void catalog_note_values(catalog_t *catalog, const values_t *values);

// Index of the entry, -1 if there is none
int catalog_find(const catalog_t *catalog, const char *name);

// The catalogue as one JSON object for storage and for the editor:
// {"NAME":{"unit":"°C","class":"temperature","profile":true,"delivered":false}, ...} in catalogue order,
// without whitespace; " and \ are written with a backslash, bytes below 0x20 as \u00xx.
// Returns the length, -1 if it does not fit (out is then an empty string; with size 0 nothing is written).
int catalog_to_json(const catalog_t *catalog, char *out, size_t size);

// Reads what catalog_to_json wrote: the result is a catalogue as after catalog_init with the entries of the
// text, taken over like those of a configuration (order, names, cut texts, dropped), in_profile from
// "profile". `delivered` is not restored (it describes the running connection).
// Returns false and changes nothing if the text is not such an object: every member has to be an object
// with the texts "unit" and "class" and the boolean "profile".
bool catalog_from_json(catalog_t *catalog, const char *json, size_t length, json_token_t *work, int work_count);

// A number that changes when names, units, classes or in_profile change (FNV-1a over them), not with
// `delivered`. Used to store the catalogue only when it really changed.
// 32 bit FNV-1a, entry by entry over name, unit and class, each with its terminating zero, and one byte
// 1 or 0 for in_profile.
uint32_t catalog_checksum(const catalog_t *catalog);

#endif
