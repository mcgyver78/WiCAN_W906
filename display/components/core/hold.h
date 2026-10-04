/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __HOLD_H__
#define __HOLD_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * The confirmation of "clear fault memory" at the knob: turn to the action, then keep the knob pressed for
 * three seconds. Clearing erases the fault memory of all control units, so a touch, a bounce or a button
 * that hangs must never confirm it.
 *
 * The press switch of the knob is read through an I2C port expander. A read can fail, and an expander
 * that lost its configuration reads "pressed" all the time. Therefore only this sequence confirms, all of
 * it observed after the dialog was opened:
 *   released for at least HOLD_RELEASED_MS -> pressed -> kept pressed for HOLD_CONFIRM_MS without a break,
 *   with the focus on the action the whole time.
 */

#define HOLD_RELEASED_MS    300u
#define HOLD_CONFIRM_MS     3000u
#define HOLD_IDLE_MS        15000u  // the dialog closes by itself after this long without any input
#define HOLD_STUCK_MS       20000u  // pressed this long without a break: the switch hangs

typedef enum
{
	HOLD_WAITING,       // nothing to show
	HOLD_PROGRESS,      // being held, see hold_permille()
	HOLD_CONFIRMED,     // the sequence is complete: reported exactly once, then the dialog is closed
	HOLD_CANCELLED,     // closed after HOLD_IDLE_MS without input: reported exactly once
	HOLD_STUCK,         // the switch hangs: clearing stays locked until the display restarts. The dialog is
	                    // closed, and every sample from then on reports it
} hold_event_t;

typedef struct
{
	bool open;
	bool stuck;                 // stays set over hold_open()
	bool seen_released;         // released long enough since the dialog was opened
	bool pressed;
	uint64_t opened_ms;
	uint64_t last_input_ms;     // last change of the switch, of the focus, or turn of the knob
	uint64_t released_since_ms;
	uint64_t pressed_since_ms;
	uint64_t held_since_ms;     // start of the hold that counts (pressed with the focus on the action)
	bool holding;
	bool on_action;             // focus at the previous sample
	uint64_t last_ms;           // time given with the previous call
	uint64_t clock_ms;          // the time all the others are in: it follows the calls but never runs backwards
	bool failed;                // the previous reading failed
} hold_t;

// The state at the start: no dialog, the switch released, the focus away from the action, nothing hangs
void hold_init(hold_t *hold);

// The confirmation dialog was opened. Returns false if the switch is known to hang (the dialog must not
// offer the action then).
bool hold_open(hold_t *hold, uint64_t now_ms);

// The dialog was left by the user
void hold_close(hold_t *hold);

// One reading of the switch, about every 20 ms, also while no dialog is open (to notice a hanging switch).
//   pressed:   the switch reads pressed
//   read_ok:   the reading succeeded; a failed reading counts as released and breaks a hold. But it has
//              observed nothing: the HOLD_RELEASED_MS begin with the first reading after it that succeeds
//              and reads released
//   on_action: the focus is on the action "clear" (not on "cancel")
// A hold that was broken starts again from zero with the next sample that reads pressed with the focus on
// the action. If several events fall on one sample, HOLD_STUCK goes before HOLD_CANCELLED and that before
// HOLD_CONFIRMED.
// A time before the one of the previous call (hold_open, hold_sample or hold_activity) counts as no time
// passed.
hold_event_t hold_sample(hold_t *hold, bool pressed, bool read_ok, bool on_action, uint64_t now_ms);

// The knob was turned or the screen touched: restarts the idle time of the dialog and breaks a hold
void hold_activity(hold_t *hold, uint64_t now_ms);

// Progress of the hold, 0 to 1000, for the ring that fills
int hold_permille(const hold_t *hold, uint64_t now_ms);

bool hold_is_stuck(const hold_t *hold);

#endif
