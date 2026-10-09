/*
 * Host test for display/components/core/knob.c. Run "make test_knob && ./test_knob" in display/test.
 * redproof.py removes or weakens every rule once (mutations/knob.py) and expects this test to fail.
 *
 * The scenes of the switch all use the same times: the display starts at 0 and sees the switch released,
 * the first reading that reads pressed comes at 1000, the second at 1020. From then on the switch counts as
 * pressed, so the long press is due at 1820.
 */
#include <limits.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include "test.h"
#include "knob.h"

static knob_t knob;

static knob_event_t reading(bool pressed, uint64_t now_ms)
{
	return knob_sample(&knob, pressed, true, now_ms);
}

static knob_event_t failed(bool pressed, uint64_t now_ms)
{
	return knob_sample(&knob, pressed, false, now_ms);
}

// A reading every 20 ms from `from` to `to`. Returns how many of them reported an event.
static int readings(bool pressed, uint64_t from, uint64_t to)
{
	int events = 0;
	uint64_t now;

	for(now = from; now <= to; now += 20)
	{
		if(reading(pressed, now) != KNOB_NONE) events++;
	}
	return events;
}

// The same with readings that fail
static int failed_readings(bool pressed, uint64_t from, uint64_t to)
{
	int events = 0;
	uint64_t now;

	for(now = from; now <= to; now += 20)
	{
		if(failed(pressed, now) != KNOB_NONE) events++;
	}
	return events;
}

static int turn(int counts, uint64_t now_ms)
{
	return knob_turn(&knob, counts, now_ms);
}

// The display started and saw the switch released
static void started(void)
{
	knob_init(&knob, false);
	reading(false, 0);
}

// The switch counts as pressed since 1020. Returns what the reading that made it so reported.
static knob_event_t pressed_at_1020(void)
{
	started();
	reading(true, 1000);
	return reading(true, 1020);
}

// Whatever stood in the memory before knob_init
static void garbage(void)
{
	knob.pressed = true;
	knob.run = 1;
	knob.long_sent = false;
	knob.pressed_since_ms = 77777;
	knob.clock_ms = 99999;
	knob.rest = 1;
	knob.last_count_ms = 88888;
	knob.reverse = true;
}

static void test_constants(void)
{
	check(KNOB_DEBOUNCE == 2 && KNOB_LONG_MS == 800 && KNOB_COUNTS_PER_DETENT == 2 && KNOB_REST_MS == 500,
	      "2 readings in a row, 800 ms for a long press, 2 counts per detent, 500 ms until a rest is dropped");
}

static void test_init(void)
{
	garbage();
	knob_init(&knob, false);
	check(!knob_is_pressed(&knob), "after the start the switch counts as released, whatever stood in the memory");
	// 112 ms after the last count that stood in the memory (garbage()): a count left there would still count
	check(turn(1, 89000) == 0 && turn(1, 89010) == 1, "after the start nothing is counted, whatever stood in the memory: two counts are the first detent, in the direction given");

	garbage();
	knob_init(&knob, true);
	check(turn(2, 10) == -1 && turn(-2, 20) == 1, "started with reverse: a detent is reported with the opposite sign");

	garbage();
	knob_init(&knob, false);
	check(reading(true, 1000) == KNOB_NONE && !knob_is_pressed(&knob), "after the start a row of readings begins anew, whatever stood in the memory: the first that reads pressed is not enough");

	garbage();
	knob_init(&knob, false);
	reading(false, 0);
	check(reading(true, 1000) == KNOB_NONE && !knob_is_pressed(&knob), "after the start and a reading that reads released: one that reads pressed is not enough");
	check(reading(true, 1020) == KNOB_NONE && knob_is_pressed(&knob), "after the start the second reading in a row makes the switch count as pressed");
	check(reading(true, 1819) == KNOB_NONE && reading(true, 1820) == KNOB_LONG, "after the start the time begins anew: the long press comes 800 ms after the press");

	garbage();
	knob_init(&knob, false);
	check(readings(true, 0, 3000) == 0 && knob_is_pressed(&knob) && readings(false, 3020, 3040) == 0,
	      "after the start no release was seen, whatever stood in the memory: a press found then reports nothing");
}

static void test_debounce(void)
{
	bool wrong = false;
	uint64_t now;

	started();
	check(reading(true, 1000) == KNOB_NONE && !knob_is_pressed(&knob), "one reading that reads pressed: the switch does not count as pressed");
	check(reading(true, 1020) == KNOB_NONE && knob_is_pressed(&knob), "two readings in a row that read pressed: it counts as pressed, and the press reports nothing");
	check(reading(false, 1040) == KNOB_NONE && knob_is_pressed(&knob), "one reading that reads released: the switch still counts as pressed");
	check(reading(false, 1060) == KNOB_SHORT && !knob_is_pressed(&knob), "two readings in a row that read released: it counts as released, a short press");

	started();
	for(now = 1000; now <= 3000; now += 40)
	{
		if(reading(true, now) != KNOB_NONE || knob_is_pressed(&knob)) wrong = true;
		if(reading(false, now + 20) != KNOB_NONE || knob_is_pressed(&knob)) wrong = true;
	}
	check(!wrong, "readings that alternate for two seconds never make the switch count as pressed");
	check(reading(true, 3040) == KNOB_NONE && !knob_is_pressed(&knob) && reading(true, 3060) == KNOB_NONE && knob_is_pressed(&knob),
	      "after alternating readings a row begins anew: the second reading in a row counts");

	started();
	reading(true, 1000);
	reading(false, 1020);
	check(reading(true, 1040) == KNOB_NONE && !knob_is_pressed(&knob), "pressed, released, pressed: the two readings that read pressed are not in a row");
	check(reading(true, 1060) == KNOB_NONE && knob_is_pressed(&knob), "pressed, released, pressed, pressed: pressed with the fourth reading");

	pressed_at_1020();
	wrong = false;
	for(now = 1040; now <= 1400; now += 40)
	{
		if(reading(false, now) != KNOB_NONE || !knob_is_pressed(&knob)) wrong = true;
		if(reading(true, now + 20) != KNOB_NONE || !knob_is_pressed(&knob)) wrong = true;
	}
	check(!wrong, "readings that alternate during a press do not release it and report nothing");
	check(reading(false, 1440) == KNOB_NONE && reading(false, 1460) == KNOB_SHORT, "after a bounce during the press the release needs two readings in a row again");
	check(readings(false, 1480, 2000) == 0 && !knob_is_pressed(&knob), "the short press is reported once");

	pressed_at_1020();
	reading(false, 1040);
	reading(true, 1060);
	check(reading(false, 1080) == KNOB_NONE && knob_is_pressed(&knob), "released, pressed, released during a press: the two readings that read released are not in a row");
	check(reading(false, 1100) == KNOB_SHORT, "released, pressed, released, released: the release comes with the fourth reading");

	// The bounce of a real press and release
	started();
	reading(true, 1000);
	reading(false, 1020);
	reading(true, 1040);
	reading(true, 1060);
	reading(true, 1080);
	reading(false, 1100);
	reading(true, 1120);
	check(reading(false, 1140) == KNOB_NONE && reading(false, 1160) == KNOB_SHORT && readings(false, 1180, 1400) == 0,
	      "a press that bounces at both ends is one short press");
}

static void test_failed_readings(void)
{
	started();
	reading(true, 1000);
	check(failed(true, 1020) == KNOB_NONE && !knob_is_pressed(&knob), "a failed reading that reads pressed does not count as pressed");
	check(reading(true, 1040) == KNOB_NONE && knob_is_pressed(&knob), "a failed reading between two that read pressed does not break their row");

	started();
	reading(true, 1000);
	failed(false, 1020);
	check(reading(true, 1040) == KNOB_NONE && knob_is_pressed(&knob), "a failed reading that reads released does not break a row of pressed readings");

	started();
	check(failed_readings(true, 1000, 3000) == 0 && !knob_is_pressed(&knob), "readings that fail for two seconds and read pressed never press the switch");
	check(reading(true, 3020) == KNOB_NONE && !knob_is_pressed(&knob), "after failed readings that read pressed one good reading is not enough");

	pressed_at_1020();
	reading(false, 1100);
	check(failed(false, 1120) == KNOB_NONE && knob_is_pressed(&knob), "a failed reading that reads released does not count as released");
	check(failed(true, 1140) == KNOB_NONE && reading(false, 1160) == KNOB_SHORT, "failed readings between two that read released do not break their row");

	pressed_at_1020();
	check(failed_readings(false, 1040, 1700) == 0 && knob_is_pressed(&knob), "readings that fail and read released do not release the switch");
	check(reading(false, 1720) == KNOB_NONE && reading(false, 1740) == KNOB_SHORT, "released behind failed readings, 720 ms after the press: a short press");

	pressed_at_1020();
	failed_readings(true, 1040, 1800);
	check(reading(true, 1820) == KNOB_LONG, "the time of a press goes on while readings fail: the long press at 800 ms");

	pressed_at_1020();
	readings(true, 1040, 1800);
	check(failed(true, 1820) == KNOB_NONE && failed(true, 1840) == KNOB_NONE, "a failed reading reports no long press, although the 800 ms are over");
	check(reading(true, 1860) == KNOB_LONG, "the first good reading behind the failed ones reports the long press");

	pressed_at_1020();
	failed_readings(true, 1040, 3000);
	check(reading(false, 3020) == KNOB_NONE && reading(false, 3040) == KNOB_NONE && !knob_is_pressed(&knob),
	      "pressed, readings failed for two seconds, released: nobody saw how long it was held, nothing is reported");

	pressed_at_1020();
	check(failed_readings(false, 1040, 61000) == 0 && knob_is_pressed(&knob), "readings that fail for a minute do not release the switch");
	check(reading(true, 61020) == KNOB_LONG, "the first good reading after a minute of failed ones reads pressed: the long press");
}

static void test_short(void)
{
	check(pressed_at_1020() == KNOB_NONE, "the scene: the switch counts as pressed since 1020, nothing reported yet");
	check(reading(false, 1040) == KNOB_NONE && reading(false, 1060) == KNOB_SHORT, "the shortest press, released 40 ms after it began: a short press");

	pressed_at_1020();
	reading(false, 1021);
	check(reading(false, 1022) == KNOB_SHORT, "released two milliseconds after the press: a short press");

	started();
	reading(true, 1000);
	reading(true, 1000);
	reading(false, 1000);
	check(reading(false, 1000) == KNOB_SHORT, "pressed and released within the same millisecond: a short press");

	pressed_at_1020();
	check(readings(true, 1040, 1760) == 0 && reading(false, 1780) == KNOB_NONE, "pressed for 760 ms: nothing reported while pressed");
	check(reading(false, 1800) == KNOB_SHORT, "released 780 ms after the press, one reading before the long press: a short press");

	pressed_at_1020();
	readings(true, 1040, 1780);
	reading(false, 1800);
	check(reading(false, 1819) == KNOB_SHORT, "released 799 ms after the press: a short press");

	pressed_at_1020();
	readings(true, 1040, 1780);
	reading(false, 1800);
	check(reading(false, 1820) == KNOB_NONE && !knob_is_pressed(&knob), "released 800 ms after the press: no short press, and no reading saw it pressed that long");
	check(readings(false, 1840, 2000) == 0, "the release at 800 ms is not reported later either");
	reading(true, 2020);
	reading(true, 2040);
	reading(false, 2060);
	check(reading(false, 2080) == KNOB_SHORT, "the press after a release at 800 ms is a short press again");

	// The time counts from the press, not from the release or the press before
	pressed_at_1020();
	readings(false, 1040, 4980);
	reading(true, 5000);
	reading(true, 5020);
	reading(false, 5040);
	check(reading(false, 5060) == KNOB_SHORT, "a short press four seconds after the one before: its time counts from its own beginning");
}

static void test_long(void)
{
	pressed_at_1020();
	check(readings(true, 1040, 1800) == 0, "kept pressed for 780 ms: nothing reported");
	check(reading(true, 1819) == KNOB_NONE, "kept pressed for 799 ms: no long press");
	check(reading(true, 1820) == KNOB_LONG && knob_is_pressed(&knob), "kept pressed for 800 ms: the long press, while still pressed");
	check(readings(true, 1840, 12000) == 0, "kept pressed for ten seconds more: the long press is reported once");
	check(reading(false, 12020) == KNOB_NONE && reading(false, 12040) == KNOB_NONE && !knob_is_pressed(&knob), "the release after a long press reports nothing");
	check(readings(false, 12060, 12400) == 0, "nothing follows the release of a long press");

	pressed_at_1020();
	readings(true, 1040, 1820);
	reading(false, 1840);
	check(reading(false, 1860) == KNOB_NONE, "released two readings after the long press: no short press");

	pressed_at_1020();
	readings(true, 1040, 1820);
	reading(false, 1840);
	check(readings(true, 1860, 4000) == 0 && knob_is_pressed(&knob), "one reading released after the long press, then kept pressed: the long press is not reported again");
	check(readings(false, 4020, 4040) == 0 && !knob_is_pressed(&knob), "the release behind that bounce reports nothing");

	// The time counts from the reading that made the switch count as pressed, not from the first one
	pressed_at_1020();
	readings(true, 1040, 1780);
	check(reading(true, 1800) == KNOB_NONE && reading(true, 1820) == KNOB_LONG, "800 ms after the first reading that read pressed: no long press yet, 20 ms later it is");

	pressed_at_1020();
	check(reading(true, 6000) == KNOB_LONG && reading(true, 6020) == KNOB_NONE, "no reading for five seconds, then pressed: the long press with that reading, once");

	pressed_at_1020();
	readings(true, 1040, 1800);
	check(reading(false, 1820) == KNOB_NONE && knob_is_pressed(&knob), "a reading that reads released at 800 ms reports no long press");
	check(reading(true, 1840) == KNOB_LONG, "the next reading that reads pressed reports the long press");

	pressed_at_1020();
	readings(true, 1040, 1400);
	reading(false, 1420);
	check(readings(true, 1440, 1800) == 0 && reading(true, 1819) == KNOB_NONE && reading(true, 1820) == KNOB_LONG,
	      "a bounce during the press does not restart its time: the long press 800 ms after the press");

	// Short, long, short, long: every press on its own
	pressed_at_1020();
	readings(true, 1040, 1820);
	readings(false, 1840, 2980);
	reading(true, 3000);
	reading(true, 3020);
	reading(false, 3040);
	check(reading(false, 3060) == KNOB_SHORT, "a short press after a long one is reported");
	reading(true, 4000);
	reading(true, 4020);
	check(readings(true, 4040, 4800) == 0 && reading(true, 4819) == KNOB_NONE && reading(true, 4820) == KNOB_LONG,
	      "a long press after a long and a short one is reported, 800 ms after its own beginning");
	check(readings(true, 4840, 6000) == 0 && readings(false, 6020, 6100) == 0, "the second long press is reported once and its release not at all");

	// Readings were missing around the release
	pressed_at_1020();
	readings(true, 1040, 1700);
	check(reading(false, 2500) == KNOB_NONE && knob_is_pressed(&knob), "a late reading that reads released, 1480 ms after the press: no long press");
	check(reading(false, 2520) == KNOB_NONE && !knob_is_pressed(&knob), "released 1500 ms after the press without a reading that saw it pressed that long: nothing");
}

// A switch that reads pressed from the very first reading on
static void test_first_press(void)
{
	knob_init(&knob, false);
	check(reading(true, 0) == KNOB_NONE && reading(true, 20) == KNOB_NONE && knob_is_pressed(&knob), "pressed from the first reading on: the switch counts as pressed");
	check(readings(true, 40, 5000) == 0, "pressed from the first reading on, kept for five seconds: no long press");
	check(reading(false, 5020) == KNOB_NONE && reading(false, 5040) == KNOB_NONE && !knob_is_pressed(&knob), "the release of a press found at the start reports nothing");
	reading(true, 6000);
	reading(true, 6020);
	check(readings(true, 6040, 6800) == 0 && reading(true, 6819) == KNOB_NONE && reading(true, 6820) == KNOB_LONG,
	      "the press after that release is a long press like any other");

	knob_init(&knob, false);
	reading(true, 0);
	reading(true, 20);
	reading(false, 40);
	check(reading(false, 60) == KNOB_NONE, "a short press found at the start reports nothing");
	reading(true, 80);
	reading(true, 100);
	reading(false, 120);
	check(reading(false, 140) == KNOB_SHORT, "the short press after the release of the one found at the start is reported");

	knob_init(&knob, false);
	check(reading(false, 0) == KNOB_NONE && reading(true, 20) == KNOB_NONE && reading(true, 40) == KNOB_NONE && reading(false, 60) == KNOB_NONE &&
	      reading(false, 80) == KNOB_SHORT, "released at the first reading: the first press is a short press");

	knob_init(&knob, false);
	failed(false, 0);
	failed(false, 20);
	reading(true, 40);
	reading(true, 60);
	check(readings(true, 80, 2000) == 0 && readings(false, 2020, 2040) == 0, "failed readings that read released are no release that was seen: the first press reports nothing");

	// One reading is the release
	knob_init(&knob, false);
	reading(true, 0);
	reading(false, 20);
	reading(true, 40);
	reading(true, 60);
	reading(false, 80);
	check(reading(false, 100) == KNOB_SHORT, "pressed, then one reading that reads released, then a press: it was seen released once, a short press");

	knob_init(&knob, false);
	reading(true, 0);
	reading(false, 20);
	reading(true, 40);
	reading(true, 60);
	check(readings(true, 80, 840) == 0 && reading(true, 859) == KNOB_NONE && reading(true, 860) == KNOB_LONG,
	      "pressed, one reading released, then kept pressed: a long press 800 ms after it counted as pressed");

	// A reading that reads released while the press found at the start still counts is a bounce, not the release
	knob_init(&knob, false);
	reading(true, 0);
	reading(true, 20);
	reading(false, 40);
	check(readings(true, 60, 2000) == 0, "one reading released during the press found at the start: it stays that press, no long press");
	check(readings(false, 2020, 2040) == 0 && !knob_is_pressed(&knob), "and no short press at its release");

	knob_init(&knob, false);
	check(reading(true, 50000) == KNOB_NONE && reading(true, 50020) == KNOB_NONE && readings(true, 50040, 52000) == 0 && readings(false, 52020, 52040) == 0,
	      "the first reading comes 50 seconds after the start and reads pressed: nothing is reported");

	knob_init(&knob, true);
	check(readings(true, 0, 3000) == 0 && knob_is_pressed(&knob) && readings(false, 3020, 3040) == 0, "started with reverse and pressed from the first reading on: nothing is reported either");

	// A restart in the middle of everything
	pressed_at_1020();
	knob_init(&knob, false);
	check(!knob_is_pressed(&knob) && readings(true, 1040, 3000) == 0 && readings(false, 3020, 3040) == 0,
	      "started again during a press: released, and the press that goes on reports nothing");
}

static void test_clock(void)
{
	// With a time that counts on from the step back, 420 would be 800 ms after the press
	pressed_at_1020();
	reading(true, 1500);
	check(reading(true, 100) == KNOB_NONE && reading(true, 420) == KNOB_NONE && reading(true, 1500) == KNOB_NONE,
	      "the time steps back during a press: no time passes until it is beyond the latest one seen");
	check(reading(true, 1819) == KNOB_NONE && reading(true, 1820) == KNOB_LONG, "after the step back the long press comes 800 ms after the press by the latest time");

	started();
	reading(false, 5000);
	reading(true, 1000);
	reading(true, 1020);
	check(reading(true, 1820) == KNOB_NONE && reading(true, 5799) == KNOB_NONE && reading(true, 5800) == KNOB_LONG,
	      "a press read with times before the latest one begins at the latest one");

	pressed_at_1020();
	reading(true, 1700);
	check(reading(false, 100) == KNOB_NONE && reading(false, 120) == KNOB_SHORT, "released with times that stepped back, 680 ms counted: a short press");

	pressed_at_1020();
	reading(true, 1819);
	check(reading(false, 0) == KNOB_NONE && reading(false, 1) == KNOB_SHORT, "released with times at 0 and 1 after 799 ms counted: a short press, the times before count as no time");

	pressed_at_1020();
	knob_turn(&knob, 0, 1820);
	check(reading(true, 1100) == KNOB_LONG, "a time given to knob_turn is a time seen: a reading with a time before it lets none pass, the long press is due");

	pressed_at_1020();
	knob_turn(&knob, 0, 1819);
	check(reading(true, 1100) == KNOB_NONE && reading(true, 1820) == KNOB_LONG, "a time of 1819 given to knob_turn: the long press one millisecond later");

	pressed_at_1020();
	failed(true, 1820);
	check(reading(true, 1100) == KNOB_LONG, "a time given with a failed reading is a time seen: the reading behind it with a time before it reports the long press");

	// The largest time
	knob_init(&knob, false);
	reading(false, UINT64_MAX - 900);
	reading(true, UINT64_MAX - 820);
	reading(true, UINT64_MAX - 800);
	check(reading(true, UINT64_MAX - 1) == KNOB_NONE && reading(true, UINT64_MAX) == KNOB_LONG, "a press 800 ms before the largest time: the long press at the largest time");

	// The time of the caller wraps around during a press
	knob_init(&knob, false);
	reading(false, UINT64_MAX - 500);
	reading(true, UINT64_MAX - 420);
	reading(true, UINT64_MAX - 400);
	reading(true, UINT64_MAX);
	check(reading(true, 0) == KNOB_NONE && reading(true, 399) == KNOB_NONE && reading(true, 400) == KNOB_NONE && reading(true, 100000) == KNOB_NONE,
	      "the time wraps from the largest to 0 during a press: a step back, no long press");
	check(reading(false, 100020) == KNOB_NONE && reading(false, 100040) == KNOB_SHORT, "released behind the wrap, 400 ms counted: a short press");
}

static void test_long_times(void)
{
	static const uint64_t gaps[4] = {65536 + 100, 0x100000000ull + 100, 0x1000000000000ull + 100, 0x8000000000000000ull};
	bool wrong_long = false, wrong_release = false, wrong_rest = false;
	size_t i;

	for(i = 0; i < 4; i++)
	{
		pressed_at_1020();
		if(reading(true, 1020 + gaps[i]) != KNOB_LONG) wrong_long = true;

		pressed_at_1020();
		if(reading(false, 1020 + gaps[i]) != KNOB_NONE || reading(false, 1040 + gaps[i]) != KNOB_NONE || knob_is_pressed(&knob)) wrong_release = true;

		knob_init(&knob, false);
		turn(1, 1000);
		if(turn(1, 1000 + gaps[i]) != 0) wrong_rest = true;
	}
	check(!wrong_long, "no reading for 65 seconds, 49 days, thousands of years or half of all time, then pressed: the long press");
	check(!wrong_release, "released 65 seconds, 49 days, thousands of years or half of all time after the press, no reading between: no short press");
	check(!wrong_rest, "a count 65 seconds, 49 days, thousands of years or half of all time after the one before: the rest was dropped");

	// Around the time that needs 33 bits
	knob_init(&knob, false);
	reading(false, 0x100000000ull - 2000);
	reading(true, 0x100000000ull - 420);
	reading(true, 0x100000000ull - 400);
	check(reading(true, 0x100000000ull + 399) == KNOB_NONE && reading(true, 0x100000000ull + 400) == KNOB_LONG,
	      "a press 400 ms before the time needs 33 bits: the long press 400 ms behind that point");

	knob_init(&knob, false);
	reading(false, 0x100000000ull - 2000);
	reading(true, 0x100000000ull - 420);
	reading(true, 0x100000000ull - 400);
	reading(false, 0x100000000ull + 300);
	check(reading(false, 0x100000000ull + 399) == KNOB_SHORT, "a press 400 ms before the time needs 33 bits, released 399 ms behind that point: a short press");

	knob_init(&knob, false);
	turn(1, 0x100000000ull - 100);
	check(turn(1, 0x100000000ull + 399) == 1, "a rest counted 100 ms before the time needs 33 bits is kept 399 ms behind that point");
	knob_init(&knob, false);
	turn(1, 0x100000000ull - 100);
	check(turn(1, 0x100000000ull + 400) == 0, "a rest counted 100 ms before the time needs 33 bits is dropped 400 ms behind that point");
}

static void test_time_zero(void)
{
	knob_init(&knob, false);
	reading(false, 0);
	reading(true, 0);
	check(reading(true, 0) == KNOB_NONE && knob_is_pressed(&knob), "released, pressed, pressed, all at time 0: the switch counts as pressed");
	check(reading(true, 799) == KNOB_NONE && reading(true, 800) == KNOB_LONG, "a press that began at time 0: the long press at 800");

	knob_init(&knob, false);
	reading(false, 0);
	reading(true, 0);
	reading(true, 0);
	reading(false, 100);
	check(reading(false, 120) == KNOB_SHORT, "a press that began at time 0, released at 120: a short press");

	knob_init(&knob, false);
	turn(1, 0);
	check(turn(1, 499) == 1, "a rest counted at time 0 is kept at 499");
	knob_init(&knob, false);
	turn(1, 0);
	check(turn(1, 500) == 0, "a rest counted at time 0 is dropped at 500");
}

// How many whole detents `counts` holds, taken off two at a time; what is left stays in *counts
static int twos(int *counts)
{
	int detents = 0;

	while(*counts >= 2)
	{
		*counts -= 2;
		detents++;
	}
	while(*counts <= -2)
	{
		*counts += 2;
		detents--;
	}
	return detents;
}

static void test_every_count(void)
{
	// One count either way tells what was left: one count forth completes a detent, one back, or neither
	static const int more[2] = {1, -1};
	int wrong_first = 0, wrong_reverse = 0, wrong_more = 0, wrong_rest = 0;
	int counts, rest;
	size_t m;

	for(counts = -1000; counts <= 1000; counts++)
	{
		int left = counts;
		int expected = twos(&left);

		knob_init(&knob, false);
		if(turn(counts, 1000) != expected) wrong_first++;
		knob_init(&knob, true);
		if(turn(counts, 1000) != -expected) wrong_reverse++;

		for(m = 0; m < 2; m++)
		{
			int together = left + more[m];
			int then = twos(&together);

			knob_init(&knob, false);
			turn(counts, 1000);
			if(turn(more[m], 1010) != then) wrong_more++;
		}
	}
	check(wrong_first == 0, "every count from -1000 to 1000 in one call: as many detents as it holds whole twos");
	check(wrong_reverse == 0, "every count from -1000 to 1000 with reverse set: the same detents with the opposite sign");
	check(wrong_more == 0, "every count from -1000 to 1000: what is left of it counts on with one count more either way");

	for(rest = -1; rest <= 1; rest++)
	{
		for(counts = -1000; counts <= 1000; counts++)
		{
			int together = rest + counts;
			int expected = twos(&together);

			knob_init(&knob, false);
			turn(rest, 1000);
			if(turn(counts, 1010) != expected) wrong_rest++;
		}
	}
	check(wrong_rest == 0, "every count from -1000 to 1000 on top of a rest of one count either way or of none: the whole twos of both together");
}

static void test_detents(void)
{
	bool wrong = false;
	int i;

	knob_init(&knob, false);
	check(turn(2, 1000) == 1, "two counts: one detent");
	check(turn(-2, 1010) == -1, "two counts the other way: one detent the other way");
	check(turn(4, 1020) == 2 && turn(-6, 1030) == -3, "fast turning: four counts are two detents, six the other way three");
	check(turn(0, 1040) == 0, "no counts: no detent");
	check(turn(1, 1050) == 0, "slow turning: one count is no detent yet");
	check(turn(1, 1060) == 1, "slow turning: the second count completes the detent");
	check(turn(-1, 1070) == 0 && turn(-1, 1080) == -1, "slow turning the other way: the second count completes the detent");

	knob_init(&knob, false);
	turn(1, 1000);
	check(turn(0, 1010) == 0 && turn(1, 1020) == 1, "a call without counts between does not lose what was counted");

	// What a call leaves is one count or none: three counts are a detent and one count in their direction
	knob_init(&knob, false);
	check(turn(3, 1000) == 1 && turn(1, 1010) == 1, "three counts: one detent, not two, and one count left that the next completes");
	knob_init(&knob, false);
	check(turn(-3, 1000) == -1 && turn(-1, 1010) == -1, "three counts the other way: one detent that way, not two, and one count left that way");

	knob_init(&knob, false);
	for(i = 0; i < 400; i++)
	{
		if(turn(1, 1000 + (uint64_t)i * 20) != (i % 2 == 1 ? 1 : 0)) wrong = true;
	}
	check(!wrong, "400 counts, one every 20 ms: a detent with every second, 200 in all");
}

static void test_direction(void)
{
	bool wrong = false;
	int i;

	knob_init(&knob, false);
	turn(1, 1000);
	check(turn(-1, 1010) == 0 && turn(1, 1020) == 0 && turn(1, 1030) == 1, "one count forth and one back are nothing: two more forth are one detent");

	knob_init(&knob, false);
	turn(1, 1000);
	check(turn(-1, 1010) == 0 && turn(-1, 1020) == 0 && turn(-1, 1030) == -1, "one count forth and one back are nothing: two more back are one detent");

	knob_init(&knob, false);
	turn(1, 1000);
	check(turn(-3, 1010) == -1 && turn(1, 1020) == 0 && turn(1, 1030) == 1, "one forth, three back: one detent back and nothing left");

	knob_init(&knob, false);
	turn(1, 1000);
	check(turn(-2, 1010) == 0 && turn(-1, 1020) == -1, "one forth, two back: one count back is left, the next completes the detent");

	knob_init(&knob, false);
	turn(-1, 1000);
	check(turn(2, 1010) == 0 && turn(1, 1020) == 1, "one back, two forth: one count forth is left");

	knob_init(&knob, false);
	for(i = 0; i < 200; i++)
	{
		if(turn(i % 2 == 0 ? 1 : -1, 1000 + (uint64_t)i * 20) != 0) wrong = true;
	}
	check(!wrong, "a knob that trembles between two counts for four seconds turns no detent");
}

static void test_rest(void)
{
	bool wrong = false;
	int i;

	knob_init(&knob, false);
	turn(1, 1000);
	check(turn(1, 1499) == 1, "the second count 499 ms after the first: the rest was kept, a detent");

	knob_init(&knob, false);
	turn(1, 1000);
	check(turn(1, 1500) == 0, "the second count 500 ms after the first: the rest was dropped, no detent");
	check(turn(1, 1600) == 1, "the count that dropped the rest is the first of a new one: the next completes the detent");

	knob_init(&knob, false);
	turn(-1, 1000);
	check(turn(-1, 1500) == 0 && turn(-1, 1600) == -1, "a rest the other way is dropped after 500 ms as well");

	knob_init(&knob, false);
	check(turn(1, 1000) == 0 && turn(1, 1499) == 1 && turn(1, 1998) == 0 && turn(1, 2497) == 1, "a count every 499 ms: every second one completes a detent");

	knob_init(&knob, false);
	for(i = 0; i < 40; i++)
	{
		if(turn(1, 1000 + (uint64_t)i * 500) != 0) wrong = true;
	}
	check(!wrong, "a count every 500 ms never adds up to a detent");

	// A rest outlives a count only in a call of several: one count is left at 1000, and one again at 1400
	knob_init(&knob, false);
	check(turn(1, 1000) == 0 && turn(2, 1400) == 1 && turn(1, 1800) == 1,
	      "one count, two 400 ms later, one 400 ms after them: two detents, the time of a rest counts from the count before and not from its first");

	knob_init(&knob, false);
	turn(1, 1000);
	turn(0, 1300);
	check(turn(1, 1500) == 0, "a call without counts is no count: it does not keep the rest alive");

	knob_init(&knob, false);
	turn(1, 1000);
	turn(0, 1400);
	check(turn(1, 1499) == 1, "a call without counts does not drop a rest that is younger than 500 ms");

	knob_init(&knob, false);
	turn(1, 1000);
	turn(0, 1600);
	check(turn(1, 1700) == 0, "a call without counts after the rest time, then a count: the rest is gone");

	knob_init(&knob, false);
	turn(2, 1000);
	turn(1, 1400);
	check(turn(1, 1899) == 1, "the rest time counts from the last count, also when that one completed a detent before");

	knob_init(&knob, false);
	turn(1, 1000);
	check(turn(3, 1500) == 1 && turn(1, 1600) == 1, "three counts after a dropped rest: one detent and one count left");

	knob_init(&knob, true);
	turn(1, 1000);
	check(turn(1, 1500) == 0 && turn(1, 1600) == -1, "with reverse set the rest is dropped after 500 ms as well");

	pressed_at_1020();
	turn(1, 1100);
	check(turn(1, 1600) == 0 && turn(1, 1700) == 1, "while the switch is pressed the rest is dropped after 500 ms as well");

	// Without a rest nothing can be dropped
	knob_init(&knob, false);
	check(turn(2, 100000) == 1 && turn(2, 900000) == 1, "whole detents are reported whatever the time between them");
}

static void test_fault(void)
{
	static const int far_out[] = {4096, 5000, 32767, 32768, 65535, 65536, 65540, 66536, 100000, 1 << 20, 1 << 24, 1 << 30};
	int wrong = 0;
	int counts;
	size_t i;

	knob_init(&knob, false);
	check(turn(1000, 1000) == 500, "1000 counts in one call: 500 detents");
	check(turn(-1000, 1010) == -500, "1000 counts the other way: 500 detents that way");

	knob_init(&knob, false);
	check(turn(999, 1000) == 499 && turn(1, 1010) == 1, "999 counts: 499 detents and one count left");
	knob_init(&knob, false);
	check(turn(-999, 1000) == -499 && turn(-1, 1010) == -1, "999 counts the other way: 499 detents and one count left that way");

	knob_init(&knob, false);
	turn(1, 1000);
	check(turn(1000, 1010) == 500 && turn(1, 1020) == 1, "1000 counts on top of a rest of one: 500 detents, the rest stays");

	knob_init(&knob, false);
	check(turn(1001, 1000) == 0, "1001 counts in one call: a fault of the counter, no detent");
	check(turn(-1001, 1010) == 0, "1001 counts the other way: a fault of the counter, no detent");

	knob_init(&knob, false);
	turn(1, 1000);
	turn(1001, 1010);
	check(turn(1, 1020) == 0 && turn(1, 1030) == 1, "a fault of the counter drops the rest: two counts behind it are the next detent, not one");

	knob_init(&knob, false);
	turn(-1, 1000);
	turn(-1001, 1010);
	check(turn(-1, 1020) == 0 && turn(-1, 1030) == -1, "a fault of the counter the other way drops the rest as well");

	knob_init(&knob, false);
	turn(1, 1000);
	turn(-1001, 1010);
	check(turn(1, 1020) == 0 && turn(1, 1030) == 1, "a fault of the counter against the direction of the rest drops it too");

	for(counts = 1001; counts <= 3000; counts++)
	{
		knob_init(&knob, false);
		turn(1, 1000);
		if(turn(counts, 1010) != 0 || turn(1, 1020) != 0 || turn(1, 1030) != 1) wrong++;
		knob_init(&knob, false);
		turn(-1, 1000);
		if(turn(-counts, 1010) != 0 || turn(-1, 1020) != 0 || turn(-1, 1030) != -1) wrong++;
	}
	check(wrong == 0, "every count from 1001 to 3000 either way: a fault, no detent, the rest dropped");

	wrong = 0;
	for(i = 0; i < sizeof(far_out) / sizeof(far_out[0]); i++)
	{
		knob_init(&knob, false);
		turn(1, 1000);
		if(turn(far_out[i], 1010) != 0 || turn(1, 1020) != 0 || turn(1, 1030) != 1) wrong++;
		knob_init(&knob, false);
		turn(-1, 1000);
		if(turn(-far_out[i], 1010) != 0 || turn(-1, 1020) != 0 || turn(-1, 1030) != -1) wrong++;
	}
	check(wrong == 0, "counts far beyond the limit either way, around 16 bits and up to 30 bits: a fault, no detent, the rest dropped");

	knob_init(&knob, false);
	turn(1, 1000);
	check(turn(INT_MAX - 1, 1010) == 0 && turn(1, 1020) == 0 && turn(1, 1030) == 1, "one below the largest int as count: a fault, no detent, the rest dropped");
	knob_init(&knob, false);
	turn(-1, 1000);
	check(turn(INT_MIN + 1, 1010) == 0 && turn(-1, 1020) == 0 && turn(-1, 1030) == -1, "one above the smallest int as count: a fault, no detent, the rest dropped");

	knob_init(&knob, false);
	turn(1, 1000);
	check(turn(INT_MAX, 1010) == 0 && turn(1, 1020) == 0 && turn(1, 1030) == 1, "the largest int as count: a fault, no detent, the rest dropped");
	knob_init(&knob, false);
	turn(-1, 1000);
	check(turn(INT_MIN, 1010) == 0 && turn(-1, 1020) == 0 && turn(-1, 1030) == -1, "the smallest int as count: a fault, no detent, the rest dropped");

	knob_init(&knob, true);
	check(turn(INT_MAX, 1000) == 0 && turn(INT_MIN, 1010) == 0 && turn(1001, 1020) == 0 && turn(-1001, 1030) == 0, "faults of the counter with reverse set: no detent");
	check(turn(1000, 1040) == -500 && turn(-1000, 1050) == 500, "1000 counts with reverse set: 500 detents with the opposite sign");
}

static void test_reverse(void)
{
	knob_init(&knob, true);
	check(turn(2, 1000) == -1 && turn(-2, 1010) == 1 && turn(4, 1020) == -2, "reverse: every detent is reported with the opposite sign");
	check(turn(0, 1030) == 0 && turn(1, 1040) == 0 && turn(1, 1050) == -1, "reverse: counts add up to a detent as without it");

	knob_set_reverse(&knob, false);
	check(turn(2, 1060) == 1 && turn(-2, 1070) == -1, "reverse switched off: detents with the sign of the counts");
	knob_set_reverse(&knob, true);
	check(turn(2, 1080) == -1 && turn(-2, 1090) == 1, "reverse switched on: detents with the opposite sign");
	knob_set_reverse(&knob, true);
	check(turn(2, 1100) == -1, "reverse switched on twice: still the opposite sign");

	// The rest is kept as it was counted
	knob_init(&knob, false);
	turn(1, 1000);
	knob_set_reverse(&knob, true);
	check(turn(1, 1010) == -1, "reverse switched on with one count left: the second completes the detent, reported with the opposite sign");

	knob_init(&knob, true);
	turn(-1, 1000);
	knob_set_reverse(&knob, false);
	check(turn(-1, 1010) == -1, "reverse switched off with one count left: the second completes the detent");

	// Nothing but the sign of the detents
	pressed_at_1020();
	readings(true, 1040, 1400);
	knob_set_reverse(&knob, true);
	check(knob_is_pressed(&knob) && readings(true, 1420, 1800) == 0 && reading(true, 1820) == KNOB_LONG, "reverse switched on during a press: the press goes on, and so does its time");
	check(readings(true, 1840, 2400) == 0, "with reverse set the long press is reported once as well");
	knob_set_reverse(&knob, false);
	check(readings(true, 2420, 3000) == 0 && readings(false, 3020, 3040) == 0, "reverse switched off after the long press: it is not reported again, the release reports nothing");

	started();
	reading(true, 1000);
	knob_set_reverse(&knob, true);
	check(reading(true, 1020) == KNOB_NONE && knob_is_pressed(&knob), "reverse switched on between two readings that read pressed: they are still in a row");
	reading(false, 1040);
	knob_set_reverse(&knob, false);
	check(reading(false, 1060) == KNOB_SHORT, "reverse switched off between two readings that read released: they are still in a row");

	knob_init(&knob, false);
	reading(true, 0);
	reading(true, 20);
	knob_set_reverse(&knob, true);
	check(readings(true, 40, 2000) == 0 && readings(false, 2020, 2040) == 0, "reverse switched on during the press found at the start: it still reports nothing");

	knob_init(&knob, false);
	turn(1, 1000);
	turn(0, 1400);
	knob_set_reverse(&knob, true);
	check(turn(1, 1500) == 0, "reverse switched on does not keep a rest alive: dropped 500 ms after the last count");

	knob_init(&knob, true);
	check(reading(false, 0) == KNOB_NONE && reading(true, 1000) == KNOB_NONE && reading(true, 1020) == KNOB_NONE && knob_is_pressed(&knob) &&
	      reading(false, 1040) == KNOB_NONE && reading(false, 1060) == KNOB_SHORT, "reverse has nothing to do with the switch: a short press as without it");
}

static void test_turn_clock(void)
{
	knob_init(&knob, false);
	turn(1, 1000);
	check(turn(1, 200) == 1, "a count with a time before the latest one: no time passed, the rest is kept");

	// A detent at 1000. The count with the time 600 is counted at 1000 as well, so the one at 1100 comes
	// 100 ms after it and not 500
	knob_init(&knob, false);
	turn(2, 1000);
	turn(1, 600);
	check(turn(1, 1100) == 1, "a count with a time before the latest one is a count at the latest one");

	knob_init(&knob, false);
	turn(1, 1000);
	turn(0, 3000);
	check(turn(1, 1100) == 0, "a call without counts moves the time on: the count behind it comes two seconds after the last one");

	knob_init(&knob, false);
	reading(false, 5000);
	turn(1, 1000);
	check(turn(1, 5499) == 1, "a time given to knob_sample is a time seen: a count with a time before it is counted at it, 499 ms later the rest is kept");
	knob_init(&knob, false);
	reading(false, 5000);
	turn(1, 1000);
	check(turn(1, 5500) == 0, "counted at the time given to knob_sample, 500 ms later the rest is dropped");

	knob_init(&knob, false);
	failed(false, 5000);
	turn(1, 1000);
	check(turn(1, 5499) == 1, "a time given with a failed reading is a time seen: the count behind it is counted at it");

	knob_init(&knob, false);
	turn(1, UINT64_MAX - 499);
	check(turn(1, UINT64_MAX) == 1, "a rest 499 ms before the largest time is kept at the largest time");
	knob_init(&knob, false);
	turn(1, UINT64_MAX - 500);
	check(turn(1, UINT64_MAX) == 0, "a rest 500 ms before the largest time is dropped at the largest time");

	knob_init(&knob, false);
	turn(1, UINT64_MAX);
	check(turn(1, 0) == 1 && turn(1, 100000) == 0 && turn(1, 900000) == 1, "the time wraps from the largest to 0: a step back, no time passes any more");
}

static void test_switch_and_encoder(void)
{
	pressed_at_1020();
	check(turn(2, 1100) == 1 && turn(1, 1200) == 0 && knob_is_pressed(&knob), "turning while pressed: detents are reported, the switch stays pressed");
	check(reading(false, 1300) == KNOB_NONE && reading(false, 1320) == KNOB_SHORT, "turning during a press does not change what the press reports");
	check(turn(1, 1400) == 1, "press and release do not change what was counted");

	// The encoder does not look at the switch
	knob_init(&knob, true);
	reading(false, 0);
	reading(true, 1000);
	reading(true, 1020);
	check(turn(2, 1100) == -1 && turn(-4, 1200) == 2, "turning while pressed with reverse set: the opposite sign as without a press");

	started();
	turn(1, 900);
	reading(true, 1000);
	reading(true, 1020);
	check(turn(1, 1030) == 1, "a press does not drop what was counted before it");

	started();
	turn(1, 1000);
	reading(true, 1400);
	reading(true, 1420);
	check(turn(1, 1500) == 0, "a press does not keep a rest alive: dropped 500 ms after the last count");

	// The switch does not look at the encoder
	started();
	reading(true, 1000);
	turn(1, 1010);
	check(reading(true, 1020) == KNOB_NONE && knob_is_pressed(&knob), "a turn between two readings that read pressed does not break their row");
	check(turn(2, 1400) == 1 && reading(true, 1819) == KNOB_NONE && reading(true, 1820) == KNOB_LONG, "turning during a press does not restart its time: the long press 800 ms after the press");
	check(turn(2, 1900) == 1 && readings(true, 1920, 3000) == 0, "turning after the long press does not report it again");

	started();
	turn(1, 900);
	reading(true, 1000);
	reading(true, 1020);
	check(readings(true, 1040, 1800) == 0 && reading(true, 1820) == KNOB_LONG, "a rest of the encoder does not keep the long press from being reported");

	knob_init(&knob, false);
	reading(true, 0);
	reading(true, 20);
	check(turn(2, 100) == 1 && readings(true, 120, 2000) == 0 && readings(false, 2020, 2040) == 0, "turning during the press found at the start: it still reports nothing");
}

/*
 * The same rules a second time, in another shape: durations that are counted up instead of points in time
 * that are compared, the reading before instead of a counter of the row, counts walked one by one instead
 * of divided.
 */
typedef struct
{
	uint64_t latest;        // the largest time given so far
	int before;             // what the reading that succeeded before this one read: 1 pressed, 0 released, -1 none
	bool down;              // counts as pressed
	bool seen_up;           // a reading saw the switch released since the start
	bool mute;              // the press that is going on reports nothing (more)
	uint64_t down_for;      // since it began to count as pressed
	int part;               // the count on the way to the next detent: -1, 0 or 1
	uint64_t still_for;     // since the last count
	bool reverse;
} model_t;

// What the walks reached, counted by the model
typedef struct
{
	long shorts, longs, mute_presses, unseen_releases, mute_releases, detents_forth, detents_back, reversed, rests_dropped, faults;
	long steps_back, far_jumps, failed, bounces, inits;
} reach_t;

static reach_t reach;

static void model_init(model_t *model, bool reverse)
{
	memset(model, 0, sizeof(*model));
	model->before = -1;
	model->reverse = reverse;
}

static void model_time(model_t *model, uint64_t now_ms)
{
	uint64_t passed = now_ms > model->latest ? now_ms - model->latest : 0;

	model->latest += passed;
	model->down_for += passed;
	model->still_for += passed;
}

static knob_event_t model_sample(model_t *model, bool pressed, bool read_ok, uint64_t now_ms)
{
	knob_event_t event = KNOB_NONE;
	bool twice;

	model_time(model, now_ms);
	if(!read_ok) return KNOB_NONE;

	twice = model->before == (pressed ? 1 : 0);
	model->before = pressed ? 1 : 0;

	if(pressed)
	{
		if(!model->down)
		{
			if(twice)
			{
				model->down = true;
				model->down_for = 0;
				model->mute = !model->seen_up;
				if(model->mute) reach.mute_presses++;
			}
		}
		else if(!model->mute && model->down_for >= 800)
		{
			model->mute = true;
			event = KNOB_LONG;
			reach.longs++;
		}
		return event;
	}

	if(model->down && twice)
	{
		model->down = false;
		if(model->mute) reach.mute_releases++;
		else if(model->down_for >= 800) reach.unseen_releases++;
		else
		{
			event = KNOB_SHORT;
			reach.shorts++;
		}
	}
	model->seen_up = true;
	return event;
}

static int model_turn(model_t *model, int counts, uint64_t now_ms)
{
	long long left = counts;
	int step = counts < 0 ? -1 : 1;
	int detents = 0;

	model_time(model, now_ms);
	if(left > 1000 || left < -1000)
	{
		model->part = 0;
		reach.faults++;
		return 0;
	}
	if(left == 0) return 0;

	if(model->still_for >= 500)
	{
		if(model->part != 0) reach.rests_dropped++;
		model->part = 0;
	}
	model->still_for = 0;
	for(; left != 0; left -= step)
	{
		model->part += step;
		if(model->part == 2 || model->part == -2)
		{
			model->part = 0;
			detents += model->reverse ? -step : step;
			if(step > 0) reach.detents_forth++;
			else reach.detents_back++;
			if(model->reverse) reach.reversed++;
		}
	}
	return detents;
}

static uint32_t random_state;

static uint32_t random_next(void)
{
	random_state = random_state * 1664525u + 1013904223u;
	return random_state >> 8;
}

static uint32_t random_below(uint32_t limit)
{
	return random_next() % limit;
}

static const char *event_name(knob_event_t event)
{
	if(event == KNOB_NONE) return "none";
	if(event == KNOB_SHORT) return "short";
	if(event == KNOB_LONG) return "long";
	return "no event of the enum";
}

#define WALKS       48
#define WALK_CALLS  60000

/*
 * One walk: a hand that presses, releases and turns, read with times that mostly go on by 20 ms, sometimes
 * jitter, go on by one, stand still, jump or step back; readings that bounce and fail; now and then a
 * restart. The seed chooses how rough it is and where the time begins. Returns the number of the first call that differs from the
 * model, -1 if none does.
 */
static int walk(uint32_t seed)
{
	static const int extremes[] = {INT_MAX, INT_MIN, 1000, -1000, 1001, -1001, 999, -999, 65540, -65540};
	static const uint64_t far[] = {65535, 65536, 65536 + 700, 0xFFFFFFFFull, 0x100000000ull, 0x100000000ull + 700, 0x10000000000ull};
	knob_t module;
	model_t model;
	int rough = (int)(seed % 3);
	uint32_t steady = rough == 0 ? 960 : rough == 1 ? 600 : 800;
	bool at_the_end = seed % 8 == 7;
	uint64_t now = at_the_end ? UINT64_MAX - 20000 : (uint64_t)seed * 1000;
	bool hand = false, busy = false;
	int spin = 1, failing = 0, stall = 0, call;

	random_state = seed * 2654435761u + 20261004u;
	knob_init(&module, false);
	model_init(&model, false);

	for(call = 0; call < WALK_CALLS; call++)
	{
		uint32_t what = random_below(1000);
		uint32_t jump = random_below(1000);
		const char *did;
		bool same;

		if(stall > 0) stall--;
		else if(jump < steady) now += 20;
		else if(jump < steady + (985 - steady) / 2) now += 15 + random_below(11);
		else if(jump < 975) now += random_below(rough == 2 ? 3000 : 700);
		else if(jump < 985) now += 1;
		else if(jump < 986)
		{
			now += far[random_below(sizeof(far) / sizeof(far[0]))];
			reach.far_jumps++;
		}
		else if(jump < 988) stall = (int)random_below(6);
		else if(jump >= 992)
		{
			uint32_t back = random_below(2000);

			now = now > back ? now - back : 0;
			reach.steps_back++;
		}

		if(what < 3)
		{
			bool reverse = random_below(2) == 0;

			knob_init(&module, reverse);
			model_init(&model, reverse);
			// Every second start finds the switch pressed, and every second one is a restart of the display:
			// its time begins anew, or shortly before the largest time in the walks that began there
			hand = random_below(2) == 0;
			if(random_below(2) == 0) now = at_the_end ? UINT64_MAX - random_below(3000) : random_below(3) == 0 ? 0 : random_below(600);
			stall = (int)random_below(8);
			reach.inits++;
			same = true;
			did = "init";
		}
		else if(what < 12)
		{
			bool reverse = random_below(2) == 0;

			knob_set_reverse(&module, reverse);
			model.reverse = reverse;
			same = true;
			did = "set_reverse";
		}
		else if(what < 400)
		{
			uint32_t kind = random_below(100);
			int counts, got, expected;

			if(kind < (busy ? 10u : 85u)) counts = 0;
			else if(kind < 93) counts = spin;
			else if(kind < 96) counts = spin * (int)(1 + random_below(9));
			else if(kind < 98) counts = (int)random_below(2001) - 1000;
			else if(kind < 99) counts = spin * (int)(995 + random_below(10));
			else counts = extremes[random_below(sizeof(extremes) / sizeof(extremes[0]))];
			if(random_below(40) == 0) busy = !busy;
			if(random_below(12) == 0) spin = -spin;

			got = knob_turn(&module, counts, now);
			expected = model_turn(&model, counts, now);
			same = got == expected;
			did = "turn";
			if(!same) printf("  walk %lu, call %d: %d counts at %llu, expected %d detents, got %d\n", (unsigned long)seed, call, counts, (unsigned long long)now, expected, got);
		}
		else
		{
			bool read_ok, read;
			knob_event_t got, expected;

			if(random_below(hand ? 45 : 60) == 0) hand = !hand;
			// Readings fail one at a time and in runs
			if(failing > 0) failing--;
			else if(random_below(rough == 2 ? 300 : 1500) == 0) failing = (int)random_below(60);
			read_ok = failing == 0 && random_below(rough == 2 ? 12 : 60) != 0;
			read = hand;
			if(random_below(rough == 2 ? 6 : 25) == 0)
			{
				read = !read;
				reach.bounces++;
			}
			if(!read_ok)
			{
				read = random_below(2) == 0;
				reach.failed++;
			}

			got = knob_sample(&module, read, read_ok, now);
			expected = model_sample(&model, read, read_ok, now);
			same = got == expected;
			did = "sample";
			if(!same)
			{
				printf("  walk %lu, call %d: read %s%s at %llu, expected %s, got %s\n", (unsigned long)seed, call, read ? "pressed" : "released",
				       read_ok ? "" : " (failed)", (unsigned long long)now, event_name(expected), event_name(got));
			}
		}

		if(knob_is_pressed(&module) != model.down)
		{
			printf("  walk %lu, call %d: after %s at %llu the switch counts as %s, expected %s\n", (unsigned long)seed, call, did, (unsigned long long)now,
			       knob_is_pressed(&module) ? "pressed" : "released", model.down ? "pressed" : "released");
			same = false;
		}
		if(!same) return call;
	}
	return -1;
}

static bool reached_enough(void)
{
	return reach.shorts > 3000 && reach.longs > 3000 && reach.mute_presses > 1000 && reach.unseen_releases > 500 && reach.mute_releases > 3000 &&
	       reach.detents_forth > 20000 && reach.detents_back > 20000 && reach.reversed > 10000 && reach.rests_dropped > 3000 && reach.faults > 1000 &&
	       reach.steps_back > 5000 && reach.far_jumps > 1000 && reach.failed > 20000 && reach.bounces > 50000 && reach.inits > 2000;
}

// Exit status of the child: 0 all well, 10 a walk differs, 11 the walks did not reach enough, 12 both
static void test_walk_against_the_model(void)
{
	int status = 0, code = -1;
	pid_t child;

	// In a child process: a crash or a hang of the module is then a failed check here, not the end of the test
	fflush(stdout);
	child = fork();
	if(child == 0)
	{
		uint32_t seed;
		int different = 0;

		alarm(120);
		for(seed = 1; seed <= WALKS; seed++)
		{
			if(walk(seed) >= 0) different++;
		}
		printf("  walks: %ld short, %ld long, %ld presses found at the start, %ld releases nobody saw the length of, %ld releases with nothing left to report,\n"
		       "  %ld detents forth, %ld back, %ld reversed, %ld rests dropped, %ld faults, %ld steps back, %ld jumps far ahead, %ld failed readings, %ld bounces, %ld starts\n",
		       reach.shorts, reach.longs, reach.mute_presses, reach.unseen_releases, reach.mute_releases, reach.detents_forth, reach.detents_back, reach.reversed,
		       reach.rests_dropped, reach.faults, reach.steps_back, reach.far_jumps, reach.failed, reach.bounces, reach.inits);
		fflush(stdout);
		_exit((different == 0 ? 0 : 10) + (reached_enough() ? 0 : different == 0 ? 11 : 2));
	}
	if(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status)) code = WEXITSTATUS(status);

	check(code == 0 || code == 10 || code == 11 || code == 12, "48 random walks of 60000 calls each: no crash and no hang");
	check(code == 0 || code == 11, "48 random walks of 60000 calls each: events, detents and the state of the switch are those of the model after every call");
	check(code == 0 || code == 10,
	      "the walks reach short and long presses, presses found at the start, releases of unknown length, detents both ways, dropped rests, faults, "
	      "steps back and jumps far ahead of the time, failed readings, bounces and restarts in numbers");
}

int main(void)
{
	test_walk_against_the_model();
	test_constants();
	test_init();
	test_debounce();
	test_failed_readings();
	test_short();
	test_long();
	test_first_press();
	test_clock();
	test_long_times();
	test_time_zero();
	test_every_count();
	test_detents();
	test_direction();
	test_rest();
	test_fault();
	test_reverse();
	test_turn_clock();
	test_switch_and_encoder();
	return test_end();
}
