/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "guard.h"

static uint32_t check_of(const guard_memory_t *memory)
{
	return memory->magic ^ memory->crashes ^ memory->layout_fresh ^ 0xFFFFFFFFu;
}

// Whether the memory holds what this firmware wrote there
static bool is_known(const guard_memory_t *memory)
{
	return memory->magic == GUARD_MAGIC && memory->check == check_of(memory);
}

static void store(guard_memory_t *memory, uint32_t crashes, bool layout_fresh)
{
	memory->magic = GUARD_MAGIC;
	memory->crashes = crashes;
	memory->layout_fresh = layout_fresh ? GUARD_MAGIC : 0;
	memory->check = check_of(memory);
}

guard_start_t guard_start(guard_memory_t *memory, guard_reset_t reset, bool knob_held)
{
	// A reason that is no value of the enum is taken for a crash: one safe mode too many locks nobody out,
	// one too few does
	bool crashed = reset != GUARD_RESET_POWER_ON && reset != GUARD_RESET_SOFTWARE;
	bool known = reset != GUARD_RESET_POWER_ON && is_known(memory);
	uint32_t crashes = known ? memory->crashes : 0;
	bool fresh = known && memory->layout_fresh == GUARD_MAGIC;
	guard_start_t start;

	// The counter stays at its largest value: one more would begin at 0 again, out of the safe mode
	if(crashed && crashes < UINT32_MAX) crashes++;

	start.safe_mode = knob_held || crashes >= GUARD_CRASHES;
	start.previous_layout = crashed && fresh;
	store(memory, crashes, fresh && !crashed);
	return start;
}

void guard_alive(guard_memory_t *memory)
{
	store(memory, 0, false);
}

void guard_layout_stored(guard_memory_t *memory)
{
	store(memory, is_known(memory) ? memory->crashes : 0, true);
}

guard_heat_t guard_heat(guard_heat_t before, int temp_c, bool valid)
{
	if(before != GUARD_HEAT_NORMAL && before != GUARD_HEAT_DIM) before = GUARD_HEAT_OFF;
	if(!valid) return before;

	if(temp_c >= GUARD_TEMP_OFF_C) return GUARD_HEAT_OFF;
	if(before == GUARD_HEAT_OFF && temp_c >= GUARD_TEMP_OFF_C - GUARD_TEMP_BACK_C) return GUARD_HEAT_OFF;
	// From here on the backlight is not off any more. Coming down from OFF it is limited like coming from DIM.
	if(temp_c >= GUARD_TEMP_DIM_C) return GUARD_HEAT_DIM;
	if(before != GUARD_HEAT_NORMAL && temp_c >= GUARD_TEMP_DIM_C - GUARD_TEMP_BACK_C) return GUARD_HEAT_DIM;
	return GUARD_HEAT_NORMAL;
}

int guard_brightness(guard_heat_t heat, int wanted)
{
	int limit = 0;

	if(heat == GUARD_HEAT_NORMAL) limit = 100;
	if(heat == GUARD_HEAT_DIM) limit = GUARD_DIM_PERCENT;

	if(wanted > limit) wanted = limit;
	return wanted < 0 ? 0 : wanted;
}

void guard_catalog_init(guard_catalog_t *guard, bool has_stored, uint32_t stored_sum)
{
	memset(guard, 0, sizeof(*guard));
	guard->has_stored = has_stored;
	guard->stored_sum = stored_sum;
}

void guard_catalog_connected(guard_catalog_t *guard)
{
	guard->written = false;
	guard->has_seen = false;
}

bool guard_catalog_due(guard_catalog_t *guard, uint32_t sum, bool complete, uint64_t now_ms)
{
	if(!guard->has_seen || sum != guard->seen_sum)
	{
		guard->has_seen = true;
		guard->seen_sum = sum;
		guard->seen_since_ms = now_ms;
	}

	if(!complete || guard->written) return false;
	if(guard->has_stored && sum == guard->stored_sum) return false;
	if(now_ms < guard->seen_since_ms || now_ms - guard->seen_since_ms < GUARD_CATALOG_REST_MS) return false;

	guard->stored_sum = sum;
	guard->has_stored = true;
	guard->written = true;
	return true;
}
