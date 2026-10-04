/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "values.h"

void values_init(values_t *values)
{
	memset(values, 0, sizeof(*values));
}

// Index of the value with that name, -1 if there is none
static int find(const values_t *values, const char *name)
{
	const value_t *item = values->items;

	for(int i = 0; i < values->count; i++, item++)
	{
		if(strcmp(item->name, name) == 0) return i;
	}
	return -1;
}

values_result_t values_apply(values_t *values, const char *json, size_t length, int64_t pass, uint64_t now_ms,
                             json_token_t *work, int work_count)
{
	int count = json_parse(json, length, work, work_count);
	int key = 1;

	if(count < 0 || work[0].type != JSON_OBJECT) return VALUES_INVALID;
	// What the firmware sends when it cannot build the answer, see autopid_data_handler()
	if(work[0].size == 1 && json_text_is(json, &work[1], "error")) return VALUES_INVALID;

	// The adapter repeats its last numbers until the next polling pass: only the counter tells them apart
	if(pass >= 0 && values->has_pass && values->pass == (uint32_t)pass) return VALUES_REPEATED;

	for(int i = 0; i < work[0].size; i++)
	{
		const json_token_t *name_token = &work[key];
		const json_token_t *member = &work[key + 1];
		char name[VALUE_NAME_SIZE];
		value_kind_t kind;
		double number = 0;
		value_t *item;
		int index;

		key += 1 + member->skip;

		if(json_number(json, member, &number)) kind = VALUE_NUMBER;
		else if(json_text_is(json, member, "on")) kind = VALUE_ON;
		else if(json_text_is(json, member, "off")) kind = VALUE_OFF;
		else continue;

		if(!json_text(json, name_token, name, sizeof(name)))
		{
			values->dropped++;
			continue;
		}

		index = find(values, name);
		if(index < 0)
		{
			if(values->count >= VALUES_MAX)
			{
				values->dropped++;
				continue;
			}
			index = values->count++;
		}
		item = values->items + index;
		strcpy(item->name, name);
		item->kind = kind;
		item->number = number;
		item->seen_ms = now_ms;
	}

	if(pass >= 0)
	{
		values->has_pass = true;
		values->pass = (uint32_t)pass;
	}
	return VALUES_RENEWED;
}

const value_t *values_find(const values_t *values, const char *name)
{
	int index = find(values, name);

	return index < 0 ? NULL : values->items + index;
}

value_age_t values_age(const value_t *value, uint64_t now_ms)
{
	uint64_t age;

	if(value == NULL) return VALUE_AGE_GONE;

	// A clock that stepped back must not turn a value into an old one
	age = now_ms > value->seen_ms ? now_ms - value->seen_ms : 0;
	if(age < VALUE_FRESH_MS) return VALUE_AGE_FRESH;
	if(age < VALUE_KEPT_MS) return VALUE_AGE_OLD;
	return VALUE_AGE_GONE;
}

void values_clear(values_t *values)
{
	values->count = 0;
	values->has_pass = false;
}
