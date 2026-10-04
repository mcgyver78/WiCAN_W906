/*
 * Host test for display/components/core/hold.c. Run "make test_hold && ./test_hold" in display/test.
 * redproof.py removes or weakens every rule once (mutations/hold.py) and expects this test to fail.
 *
 * The scenes below all use the same times: the display starts at 0 with the switch released and the focus
 * on the action, the dialog opens at 1000, the switch counts as released at 1300, a press at 1320 is
 * confirmed at 4320.
 */
#include <stdint.h>
#include "test.h"
#include "hold.h"

static hold_t hold;

static hold_event_t sample(bool pressed, bool on_action, uint64_t now_ms)
{
	return hold_sample(&hold, pressed, true, on_action, now_ms);
}

// A reading every 20 ms from `from` to `to`. Returns how many of them did not report `expected`.
static int samples(bool pressed, bool on_action, uint64_t from, uint64_t to, hold_event_t expected)
{
	int others = 0;
	uint64_t now;

	for(now = from; now <= to; now += 20)
	{
		if(sample(pressed, on_action, now) != expected) others++;
	}
	return others;
}

// A failed reading every 20 ms from `from` to `to`, all of them reading pressed. Returns how many of them
// did not report HOLD_WAITING.
static int failed_samples(uint64_t from, uint64_t to)
{
	int others = 0;
	uint64_t now;

	for(now = from; now <= to; now += 20)
	{
		if(hold_sample(&hold, true, false, true, now) != HOLD_WAITING) others++;
	}
	return others;
}

static void open_dialog(void)
{
	hold_init(&hold);
	sample(false, true, 0);
	hold_open(&hold, 1000);
}

// The dialog open, the release seen and the switch pressed at 1320: a hold that began then
static hold_event_t begin_hold(void)
{
	open_dialog();
	samples(false, true, 1000, 1300, HOLD_WAITING);
	return sample(true, true, 1320);
}

// The switch reads pressed since 900, the dialog opens at 1000, pressed until 1980: no release was seen. So
// reads an expander that lost its configuration.
static void open_pressed(void)
{
	hold_init(&hold);
	sample(true, true, 900);
	hold_open(&hold, 1000);
	samples(true, true, 1000, 1980, HOLD_WAITING);
}

static void test_sequence(void)
{
	check(HOLD_RELEASED_MS == 300 && HOLD_CONFIRM_MS == 3000 && HOLD_IDLE_MS == 15000 && HOLD_STUCK_MS == 20000,
	      "300 ms released, 3000 ms held, 15000 ms idle, 20000 ms until a switch counts as hanging");

	// Whatever stood in the memory before
	hold.open = hold.stuck = hold.seen_released = hold.pressed = hold.holding = hold.on_action = hold.failed = true;
	hold.opened_ms = hold.last_input_ms = hold.released_since_ms = hold.pressed_since_ms = hold.held_since_ms = 77777;
	hold.last_ms = hold.clock_ms = 99999;
	hold_init(&hold);
	check(!hold_is_stuck(&hold) && hold_permille(&hold, 0) == 0 && hold_permille(&hold, 200000) == 0, "after the start the switch does not hang and nothing is held, whatever stood in the memory");
	check(sample(false, true, 0) == HOLD_WAITING && sample(true, true, 100) == HOLD_WAITING && sample(true, true, 20000) == HOLD_WAITING,
	      "readings without a dialog after the start: nothing to show");
	hold_init(&hold);
	sample(false, true, 0);
	check(hold_open(&hold, 1000), "the dialog opens while the switch does not hang");
	check(samples(false, true, 1000, 1300, HOLD_WAITING) == 0, "released in the open dialog: nothing to show");
	check(sample(true, true, 1320) == HOLD_PROGRESS, "pressed after 300 ms released, with the focus on the action: the hold begins");
	check(samples(true, true, 1340, 4300, HOLD_PROGRESS) == 0, "kept pressed: progress with every reading");
	check(sample(true, true, 4319) == HOLD_PROGRESS, "held for 2999 ms: not confirmed");
	check(sample(true, true, 4320) == HOLD_CONFIRMED, "held for 3000 ms: confirmed");
	check(sample(true, true, 4340) == HOLD_WAITING, "the confirmation is reported once, the next reading has nothing to show");
	check(!hold_is_stuck(&hold), "a hold of three seconds is no hanging switch");
}

static void test_release(void)
{
	open_dialog();
	check(samples(false, true, 1000, 1280, HOLD_WAITING) == 0 && sample(false, true, 1299) == HOLD_WAITING && sample(true, true, 1300) == HOLD_WAITING,
	      "pressed after 299 ms released since the dialog opened: no hold, although released much longer before it");
	check(samples(true, true, 1320, 4400, HOLD_WAITING) == 0 && hold_permille(&hold, 4400) == 0, "a press without the release before it is never confirmed");

	open_dialog();
	samples(false, true, 1000, 1280, HOLD_WAITING);
	check(sample(false, true, 1300) == HOLD_WAITING && sample(true, true, 1301) == HOLD_PROGRESS, "pressed after a reading that saw 300 ms released: the hold begins");

	hold_init(&hold);
	sample(true, true, 900);
	hold_open(&hold, 1000);
	samples(true, true, 1000, 1980, HOLD_WAITING);
	check(samples(false, true, 2000, 2280, HOLD_WAITING) == 0 && sample(false, true, 2299) == HOLD_WAITING && sample(true, true, 2300) == HOLD_WAITING,
	      "pressed when the dialog opened, then released for 299 ms: no hold");
	check(samples(true, true, 2320, 5400, HOLD_WAITING) == 0, "the press after 299 ms released is never confirmed");

	hold_init(&hold);
	sample(true, true, 900);
	hold_open(&hold, 1000);
	samples(true, true, 1000, 1980, HOLD_WAITING);
	samples(false, true, 2000, 2280, HOLD_WAITING);
	check(sample(false, true, 2300) == HOLD_WAITING && sample(true, true, 2320) == HOLD_PROGRESS,
	      "pressed when the dialog opened, then released for 300 ms and pressed: the hold begins");

	open_dialog();
	samples(false, true, 1000, 1200, HOLD_WAITING);
	sample(true, true, 1220);
	samples(false, true, 1240, 1500, HOLD_WAITING);
	check(sample(true, true, 1520) == HOLD_WAITING, "released for 200 and for 260 ms with a press between: the times do not add up to a release");
	sample(false, true, 1540);
	check(sample(false, true, 1840) == HOLD_WAITING && sample(true, true, 1860) == HOLD_PROGRESS, "released for 300 ms after two short releases: the hold begins");
}

static void test_start(void)
{
	hold_init(&hold);
	hold_open(&hold, 300);
	check(sample(false, true, 599) == HOLD_WAITING && sample(true, true, 600) == HOLD_WAITING,
	      "a dialog opened 300 ms after the start, first reading 299 ms later: no release seen");
	hold_init(&hold);
	hold_open(&hold, 300);
	check(sample(false, true, 600) == HOLD_WAITING && sample(true, true, 620) == HOLD_PROGRESS,
	      "first reading 300 ms after the dialog opened: the switch counts as released from the start on");
	hold_init(&hold);
	hold_open(&hold, 0);
	check(sample(false, true, 300) == HOLD_WAITING && sample(true, true, 301) == HOLD_PROGRESS && sample(true, true, 3300) == HOLD_PROGRESS &&
	      sample(true, true, 3301) == HOLD_CONFIRMED, "a dialog opened at time 0, released at 300, pressed at 301: confirmed at 3301");

	hold_init(&hold);
	hold_open(&hold, 1000);
	check(sample(false, false, 5000) == HOLD_WAITING && sample(false, false, 15999) == HOLD_WAITING && sample(false, false, 16000) == HOLD_CANCELLED,
	      "a first reading with the focus away from the action is no input");
	hold_init(&hold);
	hold_open(&hold, 1000);
	check(sample(false, true, 5000) == HOLD_WAITING && sample(false, true, 16000) == HOLD_WAITING && sample(false, true, 19999) == HOLD_WAITING,
	      "a first reading with the focus on the action is a change of the focus: the idle time starts again");
	check(sample(false, true, 20000) == HOLD_CANCELLED, "cancelled 15000 ms after that first reading");
}

static void test_bounce(void)
{
	check(begin_hold() == HOLD_PROGRESS, "the scene: a hold that began at 1320");
	check(sample(false, true, 1340) == HOLD_WAITING && samples(false, true, 1360, 4400, HOLD_WAITING) == 0, "a short press confirms nothing");

	begin_hold();
	samples(true, true, 1340, 4300, HOLD_PROGRESS);
	check(sample(false, true, 4310) == HOLD_WAITING && hold_permille(&hold, 4310) == 0, "released 10 ms before the end: the hold is broken");
	check(sample(true, true, 4320) == HOLD_PROGRESS && hold_permille(&hold, 4320) == 0, "pressed again at once: a new hold from zero, no confirmation");
	check(sample(true, true, 7319) == HOLD_PROGRESS && sample(true, true, 7320) == HOLD_CONFIRMED, "the new hold is confirmed 3000 ms after it began");
}

static void test_idle(void)
{
	open_dialog();
	check(samples(false, true, 1000, 15980, HOLD_WAITING) == 0 && sample(false, true, 15999) == HOLD_WAITING, "14999 ms without input: the dialog stays open");
	check(sample(false, true, 16000) == HOLD_CANCELLED, "15000 ms without input: cancelled");
	check(sample(false, true, 16020) == HOLD_WAITING, "the cancellation is reported once");
	check(samples(true, true, 16040, 19100, HOLD_WAITING) == 0 && hold_permille(&hold, 19100) == 0, "a hold after the cancellation confirms nothing");

	open_dialog();
	sample(true, true, 10000);
	check(sample(true, true, 24999) == HOLD_WAITING && sample(true, true, 25000) == HOLD_CANCELLED, "a press restarts the idle time: cancelled 15000 ms after it");

	open_dialog();
	sample(true, true, 5000);
	sample(false, true, 10000);
	check(sample(false, true, 24999) == HOLD_WAITING && sample(false, true, 25000) == HOLD_CANCELLED, "a release restarts the idle time: cancelled 15000 ms after it");

	open_dialog();
	sample(false, false, 12000);
	check(sample(false, false, 26999) == HOLD_WAITING && sample(false, false, 27000) == HOLD_CANCELLED, "the focus leaving the action restarts the idle time");

	open_dialog();
	sample(false, false, 12000);
	sample(false, true, 13000);
	check(sample(false, true, 27999) == HOLD_WAITING && sample(false, true, 28000) == HOLD_CANCELLED, "the focus coming back to the action restarts the idle time");

	open_dialog();
	check(hold_sample(&hold, false, false, true, 8000) == HOLD_WAITING && hold_sample(&hold, true, false, true, 9000) == HOLD_WAITING &&
	      sample(false, true, 15999) == HOLD_WAITING && sample(false, true, 16000) == HOLD_CANCELLED,
	      "failed readings of a released switch are no input, whatever they read");

	open_dialog();
	sample(true, true, 5000);
	check(hold_sample(&hold, true, false, true, 12000) == HOLD_WAITING && hold_sample(&hold, true, false, true, 26999) == HOLD_WAITING &&
	      hold_sample(&hold, true, false, true, 27000) == HOLD_CANCELLED,
	      "a reading that fails while the switch is pressed is a change of the switch: cancelled 15000 ms after it");

	open_dialog();
	hold_activity(&hold, 15999);
	check(sample(false, true, 16000) == HOLD_WAITING && sample(false, true, 30998) == HOLD_WAITING, "a turn of the knob restarts the idle time: open 14999 ms after it");
	check(sample(false, true, 30999) == HOLD_CANCELLED, "cancelled 15000 ms after the turn of the knob");
}

static void test_stuck(void)
{
	hold_init(&hold);
	check(samples(true, false, 1000, 20980, HOLD_WAITING) == 0 && sample(true, false, 20999) == HOLD_WAITING && !hold_is_stuck(&hold),
	      "pressed for 19999 ms without a dialog: the switch does not hang");
	check(sample(true, false, 21000) == HOLD_STUCK, "pressed for 20000 ms without a dialog: the switch hangs");
	check(hold_is_stuck(&hold), "a hanging switch is remembered");
	check(sample(false, false, 21020) == HOLD_STUCK && sample(true, true, 21040) == HOLD_STUCK && sample(false, true, 30000) == HOLD_STUCK,
	      "every later reading reports the hanging switch, also when it is released");
	check(!hold_open(&hold, 31000), "a dialog does not open while the switch is known to hang");
	check(samples(false, true, 31000, 31300, HOLD_STUCK) == 0 && samples(true, true, 31320, 34400, HOLD_STUCK) == 0 && hold_permille(&hold, 34400) == 0,
	      "release and hold after the switch hung: nothing is confirmed");
	hold_close(&hold);
	hold_activity(&hold, 35000);
	check(hold_is_stuck(&hold) && !hold_open(&hold, 36000), "closing the dialog and turning the knob do not unlock");
	hold_init(&hold);
	check(!hold_is_stuck(&hold) && hold_open(&hold, 37000) && sample(false, true, 37000) == HOLD_WAITING, "after a restart the switch does not hang any more");

	hold_init(&hold);
	sample(true, false, 0);
	check(sample(true, false, 19999) == HOLD_WAITING && sample(true, false, 20000) == HOLD_STUCK, "a press that began at time 0 hangs at 20000");

	hold_init(&hold);
	samples(true, false, 1000, 15000, HOLD_WAITING);
	sample(false, false, 15020);
	check(samples(true, false, 15040, 35020, HOLD_WAITING) == 0 && sample(true, false, 35039) == HOLD_WAITING,
	      "a release of one reading breaks the press: no hanging switch 20000 ms after the first press, nor 19999 ms after the second");
	check(sample(true, false, 35040) == HOLD_STUCK, "the switch hangs 20000 ms after the second press");

	hold_init(&hold);
	samples(true, false, 1000, 15000, HOLD_WAITING);
	hold_sample(&hold, true, false, false, 15020);
	check(samples(true, false, 15040, 35020, HOLD_WAITING) == 0 && sample(true, false, 35039) == HOLD_WAITING && sample(true, false, 35040) == HOLD_STUCK,
	      "a failed reading breaks the press like a release");

	hold_init(&hold);
	sample(true, false, 1000);
	check(sample(true, false, 20999) == HOLD_WAITING && hold_sample(&hold, true, false, false, 21000) == HOLD_WAITING && !hold_is_stuck(&hold),
	      "the reading that would find the switch hanging fails: the press is broken, the switch does not hang");
	check(sample(true, false, 21020) == HOLD_WAITING && sample(true, false, 41019) == HOLD_WAITING && sample(true, false, 41020) == HOLD_STUCK,
	      "the switch hangs 20000 ms after the reading behind the failed one");

	hold_init(&hold);
	sample(true, false, 1000);
	hold_activity(&hold, 10000);
	hold_open(&hold, 12000);
	hold_close(&hold);
	hold_activity(&hold, 15000);
	check(sample(true, false, 20999) == HOLD_WAITING && sample(true, false, 21000) == HOLD_STUCK, "a turn of the knob or a dialog that opens and closes do not break the press");
}

static void test_stuck_first(void)
{
	// Pressed since 1000, the dialog opens at 6000: idle time and press run out with the same reading
	hold_init(&hold);
	sample(true, true, 1000);
	hold_open(&hold, 6000);
	check(sample(true, true, 20999) == HOLD_WAITING && sample(true, true, 21000) == HOLD_STUCK, "idle time over and switch hanging at once: the hanging switch is reported");
	check(sample(true, true, 21020) == HOLD_STUCK && sample(false, true, 21040) == HOLD_STUCK && !hold_open(&hold, 22000), "no cancellation follows, the dialog was closed by the hanging switch");

	// Pressed since 1320 with the focus on "cancel", which comes to the action at 18320: the hold is complete
	// with the reading that finds the switch hanging
	open_dialog();
	samples(false, true, 1000, 1300, HOLD_WAITING);
	sample(true, false, 1320);
	hold_activity(&hold, 10000);
	sample(true, false, 18300);
	check(sample(true, true, 18320) == HOLD_PROGRESS && sample(true, true, 21319) == HOLD_PROGRESS, "a hold that began 17 s after the press, 2999 ms later: progress");
	check(sample(true, true, 21320) == HOLD_STUCK, "hold complete and switch hanging at once: the hanging switch is reported, nothing is confirmed");
	check(hold_permille(&hold, 21320) == 0, "the hanging switch closed the dialog: no hold is in progress");
	check(sample(true, true, 21340) == HOLD_STUCK && hold_is_stuck(&hold), "no confirmation follows");

	// A hold that began at 1320 and no reading for 15000 ms
	begin_hold();
	check(sample(true, true, 16320) == HOLD_CANCELLED, "hold complete and idle time over at once: cancelled, nothing is confirmed");
	check(sample(true, true, 16340) == HOLD_WAITING, "no confirmation follows the cancellation");
}

static void test_pressed_at_open(void)
{
	hold_init(&hold);
	sample(true, true, 900);
	check(hold_open(&hold, 1000), "the dialog opens while the switch is pressed");
	check(samples(true, true, 1000, 15980, HOLD_WAITING) == 0 && sample(true, true, 15999) == HOLD_WAITING,
	      "pressed since before the dialog opened: no progress and no confirmation, however long");
	check(sample(true, true, 16000) == HOLD_CANCELLED, "pressed since before the dialog opened: cancelled after 15000 ms");
	check(samples(true, true, 16020, 20880, HOLD_WAITING) == 0 && sample(true, true, 20899) == HOLD_WAITING, "still pressed, 19999 ms: the switch does not hang yet");
	check(sample(true, true, 20900) == HOLD_STUCK, "pressed for 20000 ms from before the dialog on: the switch hangs");
}

static void test_failed_reading(void)
{
	begin_hold();
	samples(true, true, 1340, 2300, HOLD_PROGRESS);
	check(hold_sample(&hold, true, false, true, 2320) == HOLD_WAITING && hold_permille(&hold, 2320) == 0, "a failed reading in the middle of a hold breaks it");
	check(sample(true, true, 2340) == HOLD_PROGRESS && hold_permille(&hold, 2340) == 0, "the next good reading begins a new hold from zero");
	check(sample(true, true, 4320) == HOLD_PROGRESS && hold_permille(&hold, 4320) == 660, "3000 ms after the first press: not confirmed, 1980 ms of the new hold");
	check(sample(true, true, 5339) == HOLD_PROGRESS && sample(true, true, 5340) == HOLD_CONFIRMED, "confirmed 3000 ms after the failed reading was over");

	begin_hold();
	samples(true, true, 1340, 2300, HOLD_PROGRESS);
	check(hold_sample(&hold, false, false, true, 2320) == HOLD_WAITING && sample(true, true, 2340) == HOLD_PROGRESS && sample(true, true, 4320) == HOLD_PROGRESS &&
	      sample(true, true, 5339) == HOLD_PROGRESS && sample(true, true, 5340) == HOLD_CONFIRMED, "a failed reading that reads released breaks the hold as well");

}

// A failed reading has observed nothing: it is no part of the release that has to be seen before the press
static void test_failed_release(void)
{
	open_pressed();
	check(failed_samples(2000, 2300) == 0 && sample(true, true, 2320) == HOLD_WAITING,
	      "readings that failed for 300 ms are no release: the press behind them begins no hold");
	check(samples(true, true, 2340, 5400, HOLD_WAITING) == 0 && hold_permille(&hold, 5400) == 0, "pressed for 3000 ms behind the failed readings: no progress, no confirmation");

	open_pressed();
	check(failed_samples(2000, 6000) == 0 && samples(true, true, 6020, 9100, HOLD_WAITING) == 0, "readings that failed for 4000 ms are no release either");

	open_pressed();
	hold_sample(&hold, false, false, true, 2000);
	hold_sample(&hold, false, false, true, 2150);
	hold_sample(&hold, false, false, true, 2300);
	check(sample(true, true, 2320) == HOLD_WAITING && samples(true, true, 2340, 5400, HOLD_WAITING) == 0, "failed readings that read released are no release");

	// The first reading that succeeds is the one at 2020
	open_pressed();
	failed_samples(2000, 2000);
	check(samples(false, true, 2020, 2300, HOLD_WAITING) == 0 && sample(false, true, 2319) == HOLD_WAITING && sample(true, true, 2320) == HOLD_WAITING,
	      "released for 299 ms behind a failed reading, 319 ms after it: no release seen, the time counts from the first reading that succeeds");
	open_pressed();
	failed_samples(2000, 2000);
	samples(false, true, 2020, 2300, HOLD_WAITING);
	check(sample(false, true, 2320) == HOLD_WAITING && sample(true, true, 2340) == HOLD_PROGRESS, "released for 300 ms behind a failed reading: the release is seen, the hold begins");

	open_pressed();
	samples(false, true, 2000, 2200, HOLD_WAITING);
	failed_samples(2220, 2220);
	check(samples(false, true, 2240, 2520, HOLD_WAITING) == 0 && sample(true, true, 2539) == HOLD_WAITING,
	      "released for 200 and for 299 ms with a failed reading between: the times do not add up to a release");
	check(samples(true, true, 2540, 5600, HOLD_WAITING) == 0, "the press behind them is never confirmed");

	open_pressed();
	samples(false, true, 2000, 2280, HOLD_WAITING);
	check(hold_sample(&hold, false, false, true, 2300) == HOLD_WAITING && sample(true, true, 2320) == HOLD_WAITING,
	      "the reading that would complete the 300 ms fails: no release seen");
	samples(false, true, 2340, 2620, HOLD_WAITING);
	check(sample(false, true, 2640) == HOLD_WAITING && sample(true, true, 2660) == HOLD_PROGRESS && sample(true, true, 5659) == HOLD_PROGRESS &&
	      sample(true, true, 5660) == HOLD_CONFIRMED, "a release of 300 ms seen after failed readings counts: the hold is confirmed");

	// A release that was seen stays seen
	begin_hold();
	check(failed_samples(1340, 1700) == 0 && sample(true, true, 1720) == HOLD_PROGRESS, "failed readings after the release was seen do not take it back");

	// The dialog opens while the expander does not answer; it comes back reading pressed
	hold_init(&hold);
	sample(false, false, 900);
	hold_open(&hold, 1000);
	failed_samples(1000, 1400);
	check(samples(true, false, 1420, 2980, HOLD_WAITING) == 0 && samples(true, true, 3000, 6100, HOLD_WAITING) == 0 && hold_permille(&hold, 6100) == 0,
	      "a dialog opened during failed readings, then pressed all the time: the focus coming to the action confirms nothing");
}

static void test_focus(void)
{
	begin_hold();
	samples(true, true, 1340, 2300, HOLD_PROGRESS);
	check(sample(true, false, 2320) == HOLD_WAITING && hold_permille(&hold, 2320) == 0, "the focus leaves the action during a hold: the hold is broken");
	check(sample(true, true, 2340) == HOLD_PROGRESS && hold_permille(&hold, 2340) == 0, "the focus comes back while still pressed: a new hold from zero");
	check(sample(true, true, 4320) == HOLD_PROGRESS, "3000 ms after the press, with the focus away in between: not confirmed");
	check(sample(true, true, 5339) == HOLD_PROGRESS && sample(true, true, 5340) == HOLD_CONFIRMED, "confirmed 3000 ms after the focus came back");

	open_dialog();
	samples(false, false, 1000, 1300, HOLD_WAITING);
	check(samples(true, false, 1320, 4400, HOLD_WAITING) == 0 && hold_permille(&hold, 4400) == 0, "pressed with the focus on cancel: no progress, no confirmation");
	check(sample(true, true, 4420) == HOLD_PROGRESS && hold_permille(&hold, 4420) == 0, "the focus comes to the action while pressed: the hold begins then");
	check(sample(true, true, 7419) == HOLD_PROGRESS && sample(true, true, 7420) == HOLD_CONFIRMED, "confirmed 3000 ms after the focus came to the action");
}

static void test_clock(void)
{
	begin_hold();
	sample(true, true, 2320);
	check(sample(true, true, 1320) == HOLD_PROGRESS && hold_permille(&hold, 1320) == 333, "the time steps back by 1000 ms during a hold: no time passed, the 1000 ms held stay");
	check(sample(true, true, 3319) == HOLD_PROGRESS, "1999 ms after the step back: 2999 ms held, not confirmed");
	check(sample(true, true, 3320) == HOLD_CONFIRMED, "2000 ms after the step back: the time counts on from there, confirmed");

	begin_hold();
	sample(true, true, 2320);
	check(sample(true, true, 500) == HOLD_PROGRESS && sample(true, true, 0) == HOLD_PROGRESS && hold_permille(&hold, 0) == 333,
	      "the time steps back to before the hold and before the dialog: nothing is confirmed");
	check(sample(true, true, 1999) == HOLD_PROGRESS && sample(true, true, 2000) == HOLD_CONFIRMED, "confirmed when 3000 ms were counted in all");

	hold_init(&hold);
	sample(true, false, 1000);
	sample(true, false, 10000);
	check(sample(true, false, 0) == HOLD_WAITING && sample(true, false, 10999) == HOLD_WAITING && !hold_is_stuck(&hold),
	      "the time steps back during a press: 19999 ms counted, the switch does not hang");
	check(sample(true, false, 11000) == HOLD_STUCK, "20000 ms counted over the step back: the switch hangs");

	open_dialog();
	sample(false, true, 9000);
	check(sample(false, true, 2000) == HOLD_WAITING && sample(false, true, 8999) == HOLD_WAITING, "the time steps back in an idle dialog: 14999 ms counted, still open");
	check(sample(false, true, 9000) == HOLD_CANCELLED, "15000 ms counted over the step back: cancelled");

	hold_init(&hold);
	sample(false, true, 5000);
	hold_open(&hold, 1000);
	check(sample(false, true, 1299) == HOLD_WAITING && sample(true, true, 1300) == HOLD_WAITING && samples(true, true, 1320, 4400, HOLD_WAITING) == 0,
	      "a dialog opened with a time before the last reading: 299 ms after it no release is seen");
	hold_init(&hold);
	sample(false, true, 5000);
	hold_open(&hold, 1000);
	check(sample(false, true, 1300) == HOLD_WAITING && sample(true, true, 1320) == HOLD_PROGRESS, "300 ms after that opening the release is seen");

	begin_hold();
	sample(true, true, 2320);
	hold_activity(&hold, 1000);
	check(sample(true, true, 1020) == HOLD_PROGRESS && hold_permille(&hold, 1020) == 0, "a turn of the knob with a time before the last reading breaks the hold then, not earlier");
	check(sample(true, true, 4019) == HOLD_PROGRESS && sample(true, true, 4020) == HOLD_CONFIRMED, "the new hold is confirmed after 3000 ms counted from there");

	open_dialog();
	sample(false, true, 9000);
	sample(false, true, 2000);
	hold_activity(&hold, 3000);
	check(sample(false, true, 17999) == HOLD_WAITING && sample(false, true, 18000) == HOLD_CANCELLED,
	      "a turn of the knob after a step back of the time: the idle time counts from the turn");

	hold_init(&hold);
	sample(true, false, 1000);
	sample(true, false, 10000);
	hold_activity(&hold, 15000);
	check(sample(true, false, 12000) == HOLD_WAITING && sample(true, false, 17999) == HOLD_WAITING && sample(true, false, 18000) == HOLD_STUCK,
	      "a turn of the knob without a dialog is a call like the others: the time up to it passed, a reading with a time before it lets none pass");

	begin_hold();
	sample(true, true, 2820);
	check(hold_permille(&hold, 2820) == 500 && hold_permille(&hold, 2819) == 500 && hold_permille(&hold, 1000) == 500 && hold_permille(&hold, 0) == 500,
	      "progress asked for a time before the last reading: the progress of the last reading");
}

static void test_clock_limits(void)
{
	uint64_t start = UINT64_MAX - 4320;

	// The scene of test_sequence shifted so that it ends with the largest time
	hold_init(&hold);
	sample(false, true, start);
	hold_open(&hold, start + 1000);
	samples(false, true, start + 1000, start + 1300, HOLD_WAITING);
	check(sample(true, true, start + 1320) == HOLD_PROGRESS && sample(true, true, UINT64_MAX - 1) == HOLD_PROGRESS && hold_permille(&hold, UINT64_MAX) == 1000,
	      "a hold near the largest time: progress");
	check(sample(true, true, UINT64_MAX) == HOLD_CONFIRMED, "confirmed at the largest time");

	// The time of the caller wraps around during the hold
	start = UINT64_MAX - 2320;
	hold_init(&hold);
	sample(false, true, start);
	hold_open(&hold, start + 1000);
	samples(false, true, start + 1000, start + 1300, HOLD_WAITING);
	sample(true, true, start + 1320);
	check(sample(true, true, UINT64_MAX) == HOLD_PROGRESS && sample(true, true, 0) == HOLD_PROGRESS && hold_permille(&hold, 0) == 333,
	      "the time wraps from the largest to 0 after 1000 ms of a hold: a step back, nothing is confirmed");
	check(sample(true, true, 1999) == HOLD_PROGRESS && sample(true, true, 2000) == HOLD_CONFIRMED, "the hold goes on behind the wrap and is confirmed after 3000 ms counted");

	hold_init(&hold);
	sample(true, false, UINT64_MAX - 100);
	sample(true, false, UINT64_MAX);
	check(sample(true, false, 0) == HOLD_WAITING && sample(true, false, 19899) == HOLD_WAITING && sample(true, false, 19900) == HOLD_STUCK,
	      "a press over the wrap of the time hangs after 20000 ms counted");
}

static void test_once(void)
{
	begin_hold();
	samples(true, true, 1340, 4300, HOLD_PROGRESS);
	check(sample(true, true, 4320) == HOLD_CONFIRMED && hold_permille(&hold, 4320) == 0, "after the confirmation no hold is in progress");
	check(samples(true, true, 4340, 7400, HOLD_WAITING) == 0, "kept pressed after the confirmation: it is not reported again");
	check(hold_open(&hold, 8000), "the dialog opens again after a confirmation");
	check(samples(true, true, 8000, 11100, HOLD_WAITING) == 0, "still pressed from the first confirmation in the new dialog: no hold");
	samples(false, true, 11120, 11420, HOLD_WAITING);
	check(sample(true, true, 11440) == HOLD_PROGRESS && sample(true, true, 14439) == HOLD_PROGRESS && sample(true, true, 14440) == HOLD_CONFIRMED,
	      "released and held again in the new dialog: confirmed once more");
}

static void test_no_dialog(void)
{
	open_dialog();
	hold_init(&hold);
	samples(false, true, 0, 300, HOLD_WAITING);
	check(samples(true, true, 320, 3400, HOLD_WAITING) == 0 && hold_permille(&hold, 3400) == 0,
	      "release and hold after a restart, while no dialog is open: no progress, no confirmation");

	hold_init(&hold);
	sample(true, false, 1000);
	sample(true, false, 20000);
	hold_init(&hold);
	check(sample(true, false, 20020) == HOLD_WAITING && sample(true, false, 40019) == HOLD_WAITING && sample(true, false, 40020) == HOLD_STUCK,
	      "a press from before a restart is counted from the first reading after it");

	hold_init(&hold);
	sample(false, true, 1000);
	check(sample(false, true, 16000) == HOLD_WAITING && sample(false, true, 100000) == HOLD_WAITING, "no dialog, no input: nothing is cancelled");

	begin_hold();
	sample(true, true, 2320);
	hold_close(&hold);
	check(hold_permille(&hold, 2320) == 0 && samples(true, true, 2340, 4400, HOLD_WAITING) == 0, "the dialog left during a hold: no progress, no confirmation");

	open_dialog();
	hold_close(&hold);
	check(sample(false, true, 15999) == HOLD_WAITING && sample(false, true, 16000) == HOLD_WAITING && sample(false, true, 16020) == HOLD_WAITING,
	      "a dialog that was left is not cancelled later");
}

static void test_activity(void)
{
	begin_hold();
	sample(true, true, 2300);
	hold_activity(&hold, 2320);
	check(hold_permille(&hold, 2320) == 0, "a turn of the knob during a hold breaks it");
	check(sample(true, true, 2340) == HOLD_PROGRESS && hold_permille(&hold, 2340) == 0, "still pressed after the turn: a new hold from zero");
	check(sample(true, true, 4320) == HOLD_PROGRESS, "3000 ms after the press, with a turn of the knob in between: not confirmed");
	check(sample(true, true, 5339) == HOLD_PROGRESS && sample(true, true, 5340) == HOLD_CONFIRMED, "confirmed 3000 ms after the first reading behind the turn");

	hold_init(&hold);
	sample(true, true, 900);
	hold_open(&hold, 1000);
	hold_activity(&hold, 2000);
	check(samples(true, true, 2020, 5100, HOLD_WAITING) == 0, "a turn of the knob is no release: pressed since before the dialog, no hold");

	open_dialog();
	samples(false, true, 1000, 1200, HOLD_WAITING);
	hold_activity(&hold, 1210);
	samples(false, true, 1220, 1280, HOLD_WAITING);
	check(sample(false, true, 1300) == HOLD_WAITING && sample(true, true, 1320) == HOLD_PROGRESS,
	      "a turn of the knob while the switch is released does not start the 300 ms again");
}

static void test_permille(void)
{
	int wrong = 0;
	uint64_t ms;

	open_dialog();
	check(hold_permille(&hold, 1000) == 0, "an open dialog without a hold: progress 0");
	samples(false, true, 1000, 1300, HOLD_WAITING);
	check(hold_permille(&hold, 1300) == 0 && hold_permille(&hold, 9000) == 0, "released: progress 0 at every time");
	sample(true, true, 1320);
	check(hold_permille(&hold, 1320) == 0, "the hold begins: progress 0");
	check(hold_permille(&hold, 1322) == 0 && hold_permille(&hold, 1323) == 1, "progress 1 after 3 ms");
	check(hold_permille(&hold, 2820) == 500, "progress 500 after 1500 ms");
	check(hold_permille(&hold, 4319) == 999, "progress 999 after 2999 ms");
	check(hold_permille(&hold, 4320) == 1000, "progress 1000 after 3000 ms");
	check(hold_permille(&hold, 4321) == 1000 && hold_permille(&hold, 100000) == 1000 && hold_permille(&hold, UINT64_MAX) == 1000, "progress never above 1000");
	for(ms = 0; ms <= 3200; ms++)
	{
		int expected = ms >= 3000 ? 1000 : (int)(ms / 3);

		if(hold_permille(&hold, 1320 + ms) != expected) wrong++;
	}
	check(wrong == 0, "progress grows by one every 3 ms from 0 to 1000");
	check(sample(true, true, 1340) == HOLD_PROGRESS && hold_permille(&hold, 1340) == 6, "asking for the progress at later times lets no time pass");
}

static void test_reopen(void)
{
	begin_hold();
	sample(true, true, 2320);
	check(hold_open(&hold, 2330) && hold_permille(&hold, 2330) == 0, "the dialog opened again during a hold: the hold is broken");
	check(samples(true, true, 2340, 5400, HOLD_WAITING) == 0, "still pressed in the dialog opened again: no hold without a new release");

	open_dialog();
	samples(false, true, 1000, 1300, HOLD_WAITING);
	hold_open(&hold, 1310);
	check(sample(true, true, 1320) == HOLD_WAITING, "the release seen in a dialog does not count in the dialog opened again");

	open_dialog();
	hold_open(&hold, 10000);
	check(sample(false, true, 24999) == HOLD_WAITING && sample(false, true, 25000) == HOLD_CANCELLED, "the idle time starts when the dialog is opened again");
}

/*
 * The same rules written a second time, with times that are counted up instead of points in time that are
 * compared, for the walk below.
 */
typedef struct
{
	bool open, stuck, pressed, focus, release_seen, counting;
	bool was_up;                // the previous reading succeeded and read released
	uint64_t last;              // time of the previous call
	uint64_t pressed_for;       // since the press began
	uint64_t released_for;      // since the first of the readings in a row that saw the switch released, at most
	                            // since the dialog opened
	uint64_t held_for;          // since the hold began
	uint64_t idle_for;          // since the last input
} model_t;

// As after hold_init: the switch counts as seen released
static void model_init(model_t *model)
{
	memset(model, 0, sizeof(*model));
	model->was_up = true;
}

static uint64_t model_passed(const model_t *model, uint64_t now_ms)
{
	return now_ms > model->last ? now_ms - model->last : 0;
}

static void model_time(model_t *model, uint64_t now_ms)
{
	uint64_t passed = model_passed(model, now_ms);

	model->last = now_ms;
	model->pressed_for += passed;
	model->released_for += passed;
	model->held_for += passed;
	model->idle_for += passed;
}

static bool model_open(model_t *model, uint64_t now_ms)
{
	model_time(model, now_ms);
	if(model->stuck) return false;
	model->open = true;
	model->release_seen = false;
	model->counting = false;
	model->released_for = 0;
	model->idle_for = 0;
	return true;
}

static hold_event_t model_sample(model_t *model, bool pressed, bool read_ok, bool focus, uint64_t now_ms)
{
	bool down = read_ok && pressed;
	bool up = read_ok && !pressed;

	model_time(model, now_ms);
	if(down != model->pressed)
	{
		model->pressed = down;
		model->idle_for = 0;
		if(down) model->pressed_for = 0;
	}
	// The time released passes only between two readings that both saw it
	if(!up || !model->was_up) model->released_for = 0;
	model->was_up = up;
	if(focus != model->focus)
	{
		model->focus = focus;
		model->idle_for = 0;
	}

	if(model->pressed && model->pressed_for >= 20000) model->stuck = true;
	if(model->stuck)
	{
		model->open = false;
		model->counting = false;
		return HOLD_STUCK;
	}
	if(!model->open) return HOLD_WAITING;

	if(!model->pressed && model->released_for >= 300) model->release_seen = true;
	if(model->pressed && model->focus && model->release_seen)
	{
		if(!model->counting) model->held_for = 0;
		model->counting = true;
	}
	else
	{
		model->counting = false;
	}

	if(model->idle_for >= 15000 || (model->counting && model->held_for >= 3000))
	{
		hold_event_t event = model->idle_for >= 15000 ? HOLD_CANCELLED : HOLD_CONFIRMED;

		model->open = false;
		model->counting = false;
		return event;
	}
	return model->counting ? HOLD_PROGRESS : HOLD_WAITING;
}

static int model_permille(const model_t *model, uint64_t now_ms)
{
	uint64_t held = model->held_for + model_passed(model, now_ms);

	if(!model->counting) return 0;
	return held >= 3000 ? 1000 : (int)(held / 3);
}

static uint32_t random_state = 20261003;

static uint32_t random_next(void)
{
	random_state = random_state * 1664525u + 1013904223u;
	return random_state >> 8;
}

// Readings, turns of the knob, dialogs opened and left in a random order with times that mostly go on by
// 20 ms, sometimes jump and sometimes step back. Every answer is compared with the model.
static void test_walk(void)
{
	static const char *const names[5] = {"waiting", "progress", "confirmed", "cancelled", "stuck"};
	hold_t walk;
	model_t model;
	uint64_t now = 1000;
	bool pressed = false, focus = true;
	int events[5] = {0, 0, 0, 0, 0};
	int wrong_events = 0, wrong_permille = 0, wrong_stuck = 0, wrong_open = 0, refused = 0, stepped_back = 0, shown = 0;
	int failing = 0, failed = 0;
	long step;

	hold_init(&walk);
	model_init(&model);

	for(step = 0; step < 3000000; step++)
	{
		uint32_t what = random_next() % 10000;
		uint32_t jump = random_next() % 1000;
		uint64_t asked;

		if(jump < 900) now += 20;
		else if(jump < 960) now += random_next() % 50;
		else if(jump < 990) now += random_next() % 700;
		else if(jump < 992) now += random_next() % 20000;
		else if(now > 5000)
		{
			now -= random_next() % 5000;
			stepped_back++;
		}

		if(what < 10 || (!model.open && what < 60))
		{
			bool expected = model_open(&model, now);

			if(hold_open(&walk, now) != expected) wrong_open++;
			if(!expected) refused++;
		}
		else if(what >= 60 && what < 64)
		{
			hold_close(&walk);
			model.open = false;
			model.counting = false;
		}
		else if(what >= 64 && what < 74)
		{
			hold_activity(&walk, now);
			model_time(&model, now);
			model.idle_for = 0;
			model.counting = false;
		}
		else if((what >= 74 && what < 76) || (model.stuck && what >= 76 && what < 200))
		{
			hold_init(&walk);
			model_init(&model);
		}
		else
		{
			bool read_ok, read;
			hold_event_t event, expected;

			// Readings fail one at a time and in runs of up to a second
			if(failing > 0) failing--;
			else if(random_next() % 1500 == 0) failing = (int)(random_next() % 50);
			read_ok = failing == 0 && random_next() % 600 != 0;
			read = read_ok ? pressed : random_next() % 2 == 0;
			if(!read_ok) failed++;

			if(random_next() % 300 == 0) pressed = !pressed;
			if(random_next() % 400 == 0) focus = !focus;

			event = hold_sample(&walk, read, read_ok, focus, now);
			expected = model_sample(&model, read, read_ok, focus, now);
			if(event != expected)
			{
				if(shown++ < 5) printf("  step %ld at %llu: expected %s, got %s\n", step, (unsigned long long)now, names[expected], names[event]);
				wrong_events++;
			}
			events[expected]++;
		}

		// Progress for a time around the one of the call, also before it
		asked = now + random_next() % 200 - 50;
		if(hold_permille(&walk, asked) != model_permille(&model, asked)) wrong_permille++;
		if(hold_is_stuck(&walk) != model.stuck) wrong_stuck++;
	}

	printf("  walk: %d waiting, %d progress, %d confirmed, %d cancelled, %d stuck, %d dialogs refused, %d steps back, %d failed readings\n",
	       events[HOLD_WAITING], events[HOLD_PROGRESS], events[HOLD_CONFIRMED], events[HOLD_CANCELLED], events[HOLD_STUCK], refused, stepped_back, failed);
	check(events[HOLD_PROGRESS] > 10000 && events[HOLD_CONFIRMED] > 300 && events[HOLD_CANCELLED] > 300 && events[HOLD_STUCK] > 300 && refused > 30 && stepped_back > 3000 &&
	      failed > 30000,
	      "the walk reaches progress, confirmations, cancellations, hanging switches, refused dialogs, steps back of the time and failed readings in numbers");
	check(wrong_events == 0, "3000000 random calls: every reading reports what the model of the sequence reports");
	check(wrong_open == 0, "in the walk a dialog opens exactly when the model does not know the switch to hang");
	check(wrong_permille == 0, "in the walk the progress is the one of the model after every call");
	check(wrong_stuck == 0, "in the walk the switch hangs exactly when it hangs in the model");
}

int main(void)
{
	test_sequence();
	test_release();
	test_start();
	test_bounce();
	test_idle();
	test_stuck();
	test_stuck_first();
	test_pressed_at_open();
	test_failed_reading();
	test_failed_release();
	test_focus();
	test_clock();
	test_clock_limits();
	test_once();
	test_no_dialog();
	test_activity();
	test_permille();
	test_reopen();
	test_walk();
	return test_end();
}
