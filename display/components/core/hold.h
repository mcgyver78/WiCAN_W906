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
 *
 * Observed means: by readings that succeeded and lie at most HOLD_GAP_MS apart. Nothing is assumed about
 * the time between two readings that lie further apart, nor about the time before the first reading.
 *
 * A hold that was broken never goes on by itself, and a press that began for another reason never becomes
 * the confirmation. The release that was observed is taken back, and has to be observed again as after
 * hold_open() - HOLD_RELEASED_MS of readings that succeeded and read released - by
 *   - a reading that reads pressed while the focus is not on the action,
 *   - a turn of the knob or a touch of the screen while the switch is pressed (hold_activity()),
 *   - a reading that failed,
 *   - a reading that comes more than HOLD_GAP_MS after the one before it.
 * So a hold begins only with the reading at which a press begins, with the focus on the action, behind
 * such a release; it is as old as its press. A hold that is broken by a reading that reads released keeps
 * the release: the next press begins a new hold from zero.
 */

#define HOLD_RELEASED_MS    300u
#define HOLD_GAP_MS         200u    // the switch is read every 20 ms: readings further apart than this have
                                    // observed nothing between them
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
	bool seen_released;         // the release was observed since the dialog was opened, and nothing took it back.
	                            // With `open` and `pressed`: a hold is going on, since pressed_since_ms
	bool pressed;
	bool on_action;             // focus at the previous sample
	bool watched;               // the previous reading succeeded; false until the first reading
	uint64_t opened_ms;
	uint64_t last_input_ms;     // last change of the switch, of the focus, or turn of the knob
	uint64_t released_since_ms;
	uint64_t pressed_since_ms;
	uint64_t sampled_ms;        // the previous reading
	uint64_t last_ms;           // time given with the previous call
	uint64_t clock_ms;          // the time all the others are in: it follows the calls but never runs backwards
} hold_t;

// The state at the start: no dialog, the switch released, the focus away from the action, nothing hangs,
// no reading yet
void hold_init(hold_t *hold);

// The confirmation dialog was opened. Returns false if the switch is known to hang (the dialog must not
// offer the action then).
bool hold_open(hold_t *hold, uint64_t now_ms);

// The dialog was left by the user
void hold_close(hold_t *hold);

// One reading of the switch, about every 20 ms, also while no dialog is open (to notice a hanging switch).
//   pressed:   the switch reads pressed
//   read_ok:   the reading succeeded; a failed reading counts as released and breaks a hold. But it has
//              observed nothing: the release has to be observed again, and its HOLD_RELEASED_MS begin with
//              the first reading after it that succeeds and reads released
//   on_action: the focus is on the action "clear" (not on "cancel")
// A reading that comes more than HOLD_GAP_MS after the one before it, and the first reading after
// hold_init(), has nothing observed before it: a hold that is going on is broken, the release has to be
// observed again, and the released time begins with this reading at the earliest. Only that: the time the
// switch is pressed (HOLD_STUCK_MS) and the idle time are counted across readings that are missing.
// If HOLD_STUCK and HOLD_CANCELLED fall on one sample, HOLD_STUCK is reported. HOLD_CONFIRMED falls on a
// sample with neither of them: a hold is as old as its press, and its press was an input, so it is confirmed
// long before the switch counts as hanging or the idle time is over.
// A time before the one of the previous call (hold_open, hold_sample or hold_activity) counts as no time
// passed.
hold_event_t hold_sample(hold_t *hold, bool pressed, bool read_ok, bool on_action, uint64_t now_ms);

// The knob was turned or the screen touched: restarts the idle time of the dialog. While the switch is
// pressed it breaks a hold and takes the observed release back (see above); while it is released the
// release, and the time that counts towards it, stay.
void hold_activity(hold_t *hold, uint64_t now_ms);

// Progress of the hold, 0 to 1000, for the ring that fills
int hold_permille(const hold_t *hold, uint64_t now_ms);

bool hold_is_stuck(const hold_t *hold);

#endif
