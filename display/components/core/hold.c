/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "hold.h"

// The time of the module at now_ms. Durations are taken from it and not from now_ms: there a step
// backwards would turn "since" into a huge time and confirm at once.
static uint64_t clock_at(const hold_t *hold, uint64_t now_ms)
{
	return hold->clock_ms + (now_ms > hold->last_ms ? now_ms - hold->last_ms : 0);
}

static uint64_t advance(hold_t *hold, uint64_t now_ms)
{
	hold->clock_ms = clock_at(hold, now_ms);
	hold->last_ms = now_ms;
	return hold->clock_ms;
}

void hold_init(hold_t *hold)
{
	memset(hold, 0, sizeof(*hold));
}

bool hold_open(hold_t *hold, uint64_t now_ms)
{
	uint64_t now = advance(hold, now_ms);

	if(hold->stuck) return false;

	hold->open = true;
	hold->seen_released = false;
	hold->holding = false;
	hold->opened_ms = now;
	hold->last_input_ms = now;
	return true;
}

void hold_close(hold_t *hold)
{
	hold->open = false;
	hold->holding = false;
}

hold_event_t hold_sample(hold_t *hold, bool pressed, bool read_ok, bool on_action, uint64_t now_ms)
{
	uint64_t now = advance(hold, now_ms);

	if(!read_ok) pressed = false;

	if(pressed != hold->pressed)
	{
		hold->pressed = pressed;
		hold->last_input_ms = now;
		if(pressed) hold->pressed_since_ms = now;
		else hold->released_since_ms = now;
	}
	if(on_action != hold->on_action)
	{
		hold->on_action = on_action;
		hold->last_input_ms = now;
	}
	// A reading that failed is no released switch that was seen. An expander that does not answer and then
	// reads "pressed" all the time must not pass for a release and a press.
	if(!read_ok || hold->failed) hold->released_since_ms = now;
	hold->failed = !read_ok;

	// Watched with and without a dialog, and before everything else: a switch that hangs confirms nothing
	if(pressed && now - hold->pressed_since_ms >= HOLD_STUCK_MS) hold->stuck = true;
	if(hold->stuck)
	{
		hold_close(hold);
		return HOLD_STUCK;
	}

	if(!hold->open) return HOLD_WAITING;

	// What was read before the dialog was opened does not count
	if(!pressed && now - hold->released_since_ms >= HOLD_RELEASED_MS && now - hold->opened_ms >= HOLD_RELEASED_MS)
	{
		hold->seen_released = true;
	}

	if(pressed && on_action && hold->seen_released)
	{
		if(!hold->holding)
		{
			hold->holding = true;
			hold->held_since_ms = now;
		}
	}
	else
	{
		hold->holding = false;
	}

	// Both at once is only possible if the samples stopped for seconds. Then nobody saw the switch being
	// held, so the idle time goes first.
	if(now - hold->last_input_ms >= HOLD_IDLE_MS)
	{
		hold_close(hold);
		return HOLD_CANCELLED;
	}
	if(hold->holding && now - hold->held_since_ms >= HOLD_CONFIRM_MS)
	{
		hold_close(hold);
		return HOLD_CONFIRMED;
	}
	return hold->holding ? HOLD_PROGRESS : HOLD_WAITING;
}

void hold_activity(hold_t *hold, uint64_t now_ms)
{
	hold->last_input_ms = advance(hold, now_ms);
	hold->holding = false;
}

int hold_permille(const hold_t *hold, uint64_t now_ms)
{
	uint64_t held;

	if(!hold->holding) return 0;

	held = clock_at(hold, now_ms) - hold->held_since_ms;
	if(held >= HOLD_CONFIRM_MS) return 1000;
	return (int)(held * 1000 / HOLD_CONFIRM_MS);
}

bool hold_is_stuck(const hold_t *hold)
{
	return hold->stuck;
}
