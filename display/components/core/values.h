/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __VALUES_H__
#define __VALUES_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "json.h"

/*
 * The current values of the vehicle as the WiCAN reports them with GET /autopid_data: a flat JSON object,
 * numbers for measurements, "on" / "off" for binary sensors. Only valid values are in the answer; a value
 * that is missing was not answered by the vehicle.
 *
 * The display must never show a value as newer than it is. A value is as old as the last answer that
 * contained it and that the adapter had really renewed: GET /api/state carries a counter `pass` that moves
 * whenever a polling pass had at least one answered request. An answer taken while the counter stands
 * still repeats old numbers and renews nothing.
 */

#define VALUES_MAX          64
#define VALUE_NAME_SIZE     33      // names up to 32 bytes
#define VALUES_TOKENS       (2 * VALUES_MAX + 64)   // tokens values_apply() needs for a full answer

// A value shown normally, dimmed, or replaced by a dash
#define VALUE_FRESH_MS      3000u
#define VALUE_KEPT_MS       10000u

typedef enum
{
	VALUE_NUMBER,
	VALUE_ON,
	VALUE_OFF,
} value_kind_t;

typedef struct
{
	char name[VALUE_NAME_SIZE];
	value_kind_t kind;
	double number;          // of VALUE_NUMBER, else 0
	uint64_t seen_ms;       // time of the last renewing answer that contained the value
} value_t;

typedef struct
{
	value_t items[VALUES_MAX];
	int count;
	bool has_pass;          // an answer with a pass counter was applied before
	uint32_t pass;          // counter of that answer
	uint32_t dropped;       // values that found no room or had a name that is too long (or that json_text()
	                        // refuses), counted with every answer, since init
} values_t;

typedef enum
{
	VALUES_RENEWED,         // the answer was applied
	VALUES_REPEATED,        // the pass counter stands since the last applied answer that had one: nothing changed
	VALUES_INVALID,         // not a JSON object, or the error object of the firmware
} values_result_t;

typedef enum
{
	VALUE_AGE_FRESH,        // seen less than VALUE_FRESH_MS ago
	VALUE_AGE_OLD,          // seen less than VALUE_KEPT_MS ago: shown dimmed
	VALUE_AGE_GONE,         // older, or never seen: a dash
} value_age_t;

void values_init(values_t *values);

// Applies one answer of GET /autopid_data. pass is the counter (0 to 2^32-1) of the GET /api/state answer
// taken right before it, or -1 (any negative number) if the firmware has no such counter (then every
// answer renews).
// - every member with a number, "on" or "off" becomes a value seen at now_ms; other members are ignored,
//   also a number json_number() refuses. A number beyond the range of a double becomes infinite (which
//   fmt_number() refuses). Of two members with the same name the last counts.
// - values not in the answer keep their content and their time, so they grow old
// - {"error":...} as the only member, anything that is not an object, and broken JSON are VALUES_INVALID
//   and change nothing, whether the counter moved or not
// - "{}" is a valid answer that renews nothing
// work, work_count: room for the JSON reader (json.h), provided by the caller because it is too large for
// a task stack. A text that needs more tokens than that is treated like broken JSON.
values_result_t values_apply(values_t *values, const char *json, size_t length, int64_t pass, uint64_t now_ms,
                             json_token_t *work, int work_count);

// NULL if there never was such a value
const value_t *values_find(const values_t *values, const char *name);

// A time before seen_ms counts as no time passed. value may be NULL (gone).
value_age_t values_age(const value_t *value, uint64_t now_ms);

// Forgets every value and the pass counter, for example after the adapter restarted or another adapter
// answers. `dropped` keeps counting.
void values_clear(values_t *values);

#endif
