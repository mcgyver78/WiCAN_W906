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

// A hold is going on. The release is taken back by everything but a release that would break a hold, so a
// press behind it began with the focus on the action and was watched ever since: it is the hold.
static bool holding(const hold_t *hold)
{
	return hold->open && hold->pressed && hold->seen_released;
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
	hold->opened_ms = now;
	hold->last_input_ms = now;
	return true;
}

void hold_close(hold_t *hold)
{
	hold->open = false;
}

hold_event_t hold_sample(hold_t *hold, bool pressed, bool read_ok, bool on_action, uint64_t now_ms)
{
	uint64_t now = advance(hold, now_ms);
	// Nothing was observed up to this reading: the one before it failed, there was none before it, or it is
	// too long ago
	bool unseen = !hold->watched || now - hold->sampled_ms > HOLD_GAP_MS;

	// A reading that failed counts as released, which breaks a hold, and tells the next one that it saw nothing
	hold->watched = read_ok;
	hold->sampled_ms = now;
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
	// What nobody observed is no released switch that was seen, and a switch that reads pressed behind it is
	// no press that was seen to begin. An expander that does not answer and then reads "pressed" all the time
	// must not pass for a release and a press.
	if(unseen) hold->released_since_ms = now;
	// A press with the focus elsewhere was meant for something else, and stays that when the focus comes
	if(unseen || (pressed && !on_action)) hold->seen_released = false;

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

	if(now - hold->last_input_ms >= HOLD_IDLE_MS)
	{
		hold_close(hold);
		return HOLD_CANCELLED;
	}
	if(holding(hold) && now - hold->pressed_since_ms >= HOLD_CONFIRM_MS)
	{
		hold_close(hold);
		return HOLD_CONFIRMED;
	}
	return holding(hold) ? HOLD_PROGRESS : HOLD_WAITING;
}

void hold_activity(hold_t *hold, uint64_t now_ms)
{
	hold->last_input_ms = advance(hold, now_ms);
	// The hold is broken, and the press that goes on must not become a new one
	if(hold->pressed) hold->seen_released = false;
}

int hold_permille(const hold_t *hold, uint64_t now_ms)
{
	uint64_t held;

	if(!holding(hold)) return 0;

	held = clock_at(hold, now_ms) - hold->pressed_since_ms;
	if(held >= HOLD_CONFIRM_MS) return 1000;
	return (int)(held * 1000 / HOLD_CONFIRM_MS);
}

bool hold_is_stuck(const hold_t *hold)
{
	return hold->stuck;
}
