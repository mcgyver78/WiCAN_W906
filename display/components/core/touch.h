/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __TOUCH_H__
#define __TOUCH_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * Taps and swipes from the readings of the touch controller. The controller is polled about every 30 ms;
 * a reading can fail. Touch is an addition to the knob: everything works without it, and nothing that
 * matters (clearing the fault memory) can be confirmed by it.
 *
 * A gesture is one finger going down and up again. It is judged when the finger is lifted:
 *   tap      lifted within TOUCH_TAP_MS and never further than TOUCH_TAP_MOVE pixels from where it went down
 *            (in x and in y each)
 *   swipe    lifted within TOUCH_SWIPE_MS, at least TOUCH_SWIPE_MOVE pixels from where it went down in one
 *            axis and at least twice as far in that axis as in the other. The direction is where the
 *            finger went: left = x became smaller, up = y became smaller.
 *   else     nothing (a finger resting on the glass, a diagonal smear)
 * Compared are the point where the finger went down and the last point read before it was lifted; for the
 * tap also every point in between. The time runs from the reading with which the finger went down to the
 * reading with which it counts as lifted (TOUCH_LIFT_READINGS).
 */

#define TOUCH_SIZE          480     // points are 0 to TOUCH_SIZE - 1 in x and y
#define TOUCH_TAP_MS        600u
#define TOUCH_TAP_MOVE      20
#define TOUCH_SWIPE_MS      1000u
#define TOUCH_SWIPE_MOVE    60
#define TOUCH_LIFT_READINGS 2       // readings in a row without a finger until it counts as lifted
#define TOUCH_LOST_MS       300u    // readings fail this long during a touch: it is dropped without a gesture

typedef enum
{
	TOUCH_NONE,
	TOUCH_TAP,
	TOUCH_SWIPE_LEFT,
	TOUCH_SWIPE_RIGHT,
	TOUCH_SWIPE_UP,
	TOUCH_SWIPE_DOWN,
} touch_event_t;

typedef struct
{
	bool down;                  // a touch is going on
	bool moved;                 // it left the tap range at some point
	int start_x, start_y;
	int last_x, last_y;
	uint64_t start_ms;
	int lifted;                 // readings without a finger in a row
	bool failing;
	uint64_t failing_since_ms;
	uint64_t clock_ms;          // the latest time seen: it follows the calls but never runs backwards
} touch_t;

void touch_init(touch_t *touch);

// One reading. read_ok false: the reading failed and says nothing; down, x and y are not looked at. It
// neither counts as one of the TOUCH_LIFT_READINGS nor breaks their row. TOUCH_LOST_MS run from the first
// of the readings that failed in a row to the latest of them.
// down: a finger is on the screen at x, y. A point outside 0 to TOUCH_SIZE - 1 counts as a failed reading.
// Without a finger x and y are not looked at.
// Returns the gesture that ended with this reading, exactly once; for TOUCH_TAP *x_out and *y_out (may be
// NULL) receive the point where the finger went down, otherwise they are not written.
// A finger that is on the screen at the very first reading after touch_init(), or at the first reading that
// succeeds after a touch was dropped, starts a touch like any other.
// A time before the latest one seen (clock_ms) counts as no time passed.
touch_event_t touch_sample(touch_t *touch, bool read_ok, bool down, int x, int y, uint64_t now_ms,
                           int *x_out, int *y_out);

// true while a finger is on the screen (for the idle time of the display)
bool touch_is_down(const touch_t *touch);

#endif
