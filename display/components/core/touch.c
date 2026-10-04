/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "touch.h"

// The time of the module. Durations are taken from it and not from now_ms: there a step backwards would
// turn "since" into a huge time.
static uint64_t advance(touch_t *touch, uint64_t now_ms)
{
	if(now_ms > touch->clock_ms) touch->clock_ms = now_ms;
	return touch->clock_ms;
}

static bool on_screen(int x, int y)
{
	return x >= 0 && x < TOUCH_SIZE && y >= 0 && y < TOUCH_SIZE;
}

// Both points are on the screen, so the difference is small
static int distance(int a, int b)
{
	return a > b ? a - b : b - a;
}

// The gesture of the touch that ended at `now`. Tap and swipe exclude each other, and so do the two axes
// of a swipe: the order of the questions does not matter.
static touch_event_t judge(const touch_t *touch, uint64_t now)
{
	uint64_t lasted = now - touch->start_ms;
	int far_x = distance(touch->last_x, touch->start_x);
	int far_y = distance(touch->last_y, touch->start_y);

	if(!touch->moved && lasted <= TOUCH_TAP_MS) return TOUCH_TAP;
	if(lasted > TOUCH_SWIPE_MS) return TOUCH_NONE;

	if(far_x >= TOUCH_SWIPE_MOVE && far_x >= 2 * far_y)
	{
		return touch->last_x < touch->start_x ? TOUCH_SWIPE_LEFT : TOUCH_SWIPE_RIGHT;
	}
	if(far_y >= TOUCH_SWIPE_MOVE && far_y >= 2 * far_x)
	{
		return touch->last_y < touch->start_y ? TOUCH_SWIPE_UP : TOUCH_SWIPE_DOWN;
	}
	return TOUCH_NONE;
}

void touch_init(touch_t *touch)
{
	memset(touch, 0, sizeof(*touch));
}

touch_event_t touch_sample(touch_t *touch, bool read_ok, bool down, int x, int y, uint64_t now_ms,
                           int *x_out, int *y_out)
{
	uint64_t now = advance(touch, now_ms);
	touch_event_t event;

	// A controller that answers with a point that does not exist has said nothing either
	if(down && !on_screen(x, y)) read_ok = false;

	if(!read_ok)
	{
		if(!touch->failing)
		{
			touch->failing = true;
			touch->failing_since_ms = now;
		}
		// Nobody knows what the finger did in the meantime: no gesture is guessed from the rest
		if(now - touch->failing_since_ms >= TOUCH_LOST_MS) touch->down = false;
		return TOUCH_NONE;
	}
	touch->failing = false;

	if(down)
	{
		if(!touch->down)
		{
			touch->down = true;
			touch->moved = false;
			touch->start_x = x;
			touch->start_y = y;
			touch->start_ms = now;
		}
		// Remembered, because the finger may come back to where it started
		if(distance(x, touch->start_x) > TOUCH_TAP_MOVE || distance(y, touch->start_y) > TOUCH_TAP_MOVE)
		{
			touch->moved = true;
		}
		touch->last_x = x;
		touch->last_y = y;
		touch->lifted = 0;
		return TOUCH_NONE;
	}

	if(!touch->down) return TOUCH_NONE;

	// One reading without a finger is not taken for a lift yet: the finger may be read again with the next
	touch->lifted++;
	if(touch->lifted < TOUCH_LIFT_READINGS) return TOUCH_NONE;

	touch->down = false;
	event = judge(touch, now);
	if(event == TOUCH_TAP)
	{
		if(x_out != NULL) *x_out = touch->start_x;
		if(y_out != NULL) *y_out = touch->start_y;
	}
	return event;
}

bool touch_is_down(const touch_t *touch)
{
	return touch->down;
}
