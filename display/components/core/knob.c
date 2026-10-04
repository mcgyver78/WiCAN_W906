/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "knob.h"

// More counts than this in one call is no hand turning the knob
#define COUNTS_FAULT    1000

// The time of the module at now_ms. Durations are taken from it and not from now_ms: there a step
// backwards would turn "since" into a huge time, a long press or a rest long gone.
static uint64_t advance(knob_t *knob, uint64_t now_ms)
{
	if(now_ms > knob->clock_ms) knob->clock_ms = now_ms;
	return knob->clock_ms;
}

void knob_init(knob_t *knob, bool reverse)
{
	memset(knob, 0, sizeof(*knob));
	knob->reverse = reverse;
	// Nobody saw the switch released yet: a press found at the start reports as little as one whose
	// long press was reported already
	knob->long_sent = true;
}

void knob_set_reverse(knob_t *knob, bool reverse)
{
	knob->reverse = reverse;
}

knob_event_t knob_sample(knob_t *knob, bool pressed, bool read_ok, uint64_t now_ms)
{
	uint64_t now = advance(knob, now_ms);
	knob_event_t event = KNOB_NONE;

	// A failed reading has seen nothing: it is no part of a row, breaks none and reports nothing
	if(!read_ok) return KNOB_NONE;

	if(pressed == knob->pressed)
	{
		knob->run = 0;
		// Only a reading that sees the switch pressed reports the long press. After a pause of the readings
		// nobody knows how long it was held.
		if(pressed && !knob->long_sent && now - knob->pressed_since_ms >= KNOB_LONG_MS)
		{
			knob->long_sent = true;
			event = KNOB_LONG;
		}
	}
	else if(++knob->run >= KNOB_DEBOUNCE)
	{
		knob->run = 0;
		knob->pressed = pressed;
		if(pressed) knob->pressed_since_ms = now;
		else if(!knob->long_sent && now - knob->pressed_since_ms < KNOB_LONG_MS) event = KNOB_SHORT;
	}

	// Seen released while it counts as released: the press that follows is seen from its beginning
	if(!pressed && !knob->pressed) knob->long_sent = false;
	return event;
}

bool knob_is_pressed(const knob_t *knob)
{
	return knob->pressed;
}

int knob_turn(knob_t *knob, int counts, uint64_t now_ms)
{
	uint64_t now = advance(knob, now_ms);
	int detents;

	if(counts > COUNTS_FAULT || counts < -COUNTS_FAULT)
	{
		knob->rest = 0;
		return 0;
	}
	if(counts == 0) return 0;

	if(now - knob->last_count_ms >= KNOB_REST_MS) knob->rest = 0;
	knob->last_count_ms = now;

	// Division and remainder round towards zero: the rest keeps the direction of the turn it is left of
	counts += knob->rest;
	detents = counts / KNOB_COUNTS_PER_DETENT;
	knob->rest = counts % KNOB_COUNTS_PER_DETENT;
	return knob->reverse ? -detents : detents;
}
