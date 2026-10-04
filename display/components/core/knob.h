/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __KNOB_H__
#define __KNOB_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * The knob of the display: turning in detents, and its press switch.
 *
 * The switch is read through an I2C port expander about every 20 ms; a reading can fail. The schematic has
 * no debounce capacitors. The switch counts as pressed or released once KNOB_DEBOUNCE readings in a row
 * that succeeded say so; a failed reading says nothing and does not count for either.
 *
 *   short press: released again before KNOB_LONG_MS            -> KNOB_SHORT at the release
 *   long press:  kept pressed for KNOB_LONG_MS                 -> KNOB_LONG once, while still pressed;
 *                                                                 the release that follows reports nothing
 *
 * A failed reading breaks no row either and reports nothing. Both times count from the reading with which the
 * switch began to count as pressed; the release is the reading with which it counts as released. KNOB_LONG
 * is reported by a reading that succeeds and reads pressed. A release at KNOB_LONG_MS or later without
 * such a reading before it (readings were missing) reports nothing: nobody saw how long the switch was held.
 *
 * The clear dialog reads the switch by its own, stricter rules (hold.h); while it is open the caller does
 * not act on the events of this module.
 *
 * The encoder is counted by the pulse counter of the chip. One detent is KNOB_COUNTS_PER_DETENT counts;
 * what is left of a detent is kept, so that slow turning loses nothing, and dropped after KNOB_REST_MS
 * without a count, so that a knob resting between two detents does not add up to a step some day.
 */

#define KNOB_DEBOUNCE           2
#define KNOB_LONG_MS            800u
#define KNOB_COUNTS_PER_DETENT  4
#define KNOB_REST_MS            500u

typedef enum
{
	KNOB_NONE,
	KNOB_SHORT,
	KNOB_LONG,
} knob_event_t;

typedef struct
{
	bool pressed;               // debounced state
	int run;                    // successful readings in a row that differ from `pressed`
	bool long_sent;             // nothing more is reported for the press that is going on: KNOB_LONG was,
	                            // or the switch was not seen released before it (so it is set from the start)
	uint64_t pressed_since_ms;
	uint64_t clock_ms;          // the latest time seen: it follows the calls but never runs backwards
	int rest;                   // counts that are not a whole detent yet, -3 to 3
	uint64_t last_count_ms;
	bool reverse;
} knob_t;

// Released, nothing counted. reverse: turning is reported with the opposite sign (setting of the user:
// the sources disagree about the direction of this board).
void knob_init(knob_t *knob, bool reverse);

// Changes the sign of what knob_turn() returns from now on. What was counted so far stays.
void knob_set_reverse(knob_t *knob, bool reverse);

// One reading of the switch. A switch that reads pressed from the very first reading on (pressed while the
// display starts, or an expander that hangs) reports nothing until it was seen released once: `pressed`
// starts as false, but the first press needs a release before it. One reading that succeeds and reads
// released is that release.
// A time before the latest one given to knob_sample() or knob_turn() counts as no time passed.
knob_event_t knob_sample(knob_t *knob, bool pressed, bool read_ok, uint64_t now_ms);

// true while the debounced switch is pressed
bool knob_is_pressed(const knob_t *knob);

// New counts of the encoder since the last call (positive or negative, 0 if none). Returns the detents
// turned, with the sign reversed if `reverse` is set. A count beyond +-1000 in one call is treated as a
// fault of the counter and dropped together with the rest.
// Detents are rounded towards zero, the rest keeps the direction it was counted in. It is dropped by a count
// that comes KNOB_REST_MS or more after the count before it; a call with 0 is no count.
int knob_turn(knob_t *knob, int counts, uint64_t now_ms);

#endif
