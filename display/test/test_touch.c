/*
 * Host test for display/components/core/touch.c. Run "make test_touch && ./test_touch" in display/test.
 * redproof.py removes or weakens every rule once (mutations/touch.py) and expects this test to fail.
 *
 * The scenes use one picture where they can: the finger goes down at the time 1000, mostly at the centre
 * (240, 240), and the controller is read every 30 ms. A touch counts as lifted with the second reading in a
 * row without a finger, and its time runs up to that reading.
 */
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "test.h"
#include "touch.h"

#define UNSET   (-7777)     // stands in a place for the tap point that was not written
#define EARLY   (-1)        // a reading before the last of a scene reported a gesture

static touch_t touch;
static int tap_x, tap_y;    // what the last reading wrote as the tap point

static touch_event_t reading(bool read_ok, bool down, int x, int y, uint64_t now_ms)
{
	tap_x = UNSET;
	tap_y = UNSET;
	return touch_sample(&touch, read_ok, down, x, y, now_ms, &tap_x, &tap_y);
}

static touch_event_t finger(int x, int y, uint64_t now_ms)
{
	return reading(true, true, x, y, now_ms);
}

static touch_event_t no_finger(uint64_t now_ms)
{
	return reading(true, false, 0, 0, now_ms);
}

static touch_event_t failed(uint64_t now_ms)
{
	return reading(false, false, 0, 0, now_ms);
}

// The last reading reported nothing and wrote no point
static bool silent(touch_event_t event)
{
	return event == TOUCH_NONE && tap_x == UNSET && tap_y == UNSET;
}

static bool tap_at(int event, int x, int y)
{
	return event == TOUCH_TAP && tap_x == x && tap_y == y;
}

// A swipe, or nothing at the end of a scene: that event and no point written
static bool only(int event, touch_event_t expected)
{
	return event == (int)expected && tap_x == UNSET && tap_y == UNSET;
}

// The usual touch: down at (x0, y0) at 1000, read at (x1, y1) at 1030, the first reading without a finger at
// 1060 and the second, with which it counts as lifted, `lasted` ms after it went down (60 or more). Returns
// what that last reading reported, or EARLY if a reading before it reported a gesture.
static int stroke(int x0, int y0, int x1, int y1, uint64_t lasted)
{
	touch_init(&touch);
	if(finger(x0, y0, 1000) != TOUCH_NONE || finger(x1, y1, 1030) != TOUCH_NONE || no_finger(1060) != TOUCH_NONE) return EARLY;
	return no_finger(1000 + lasted);
}

// The finger goes down at the centre at 1000 and is read at each of the points 30 ms apart, then lifted with
// two readings. Returns what the last of them reported, or EARLY.
static int path(const int (*points)[2], int count)
{
	uint64_t now = 1000;
	int i;

	touch_init(&touch);
	if(finger(240, 240, now) != TOUCH_NONE) return EARLY;
	for(i = 0; i < count; i++)
	{
		now += 30;
		if(finger(points[i][0], points[i][1], now) != TOUCH_NONE) return EARLY;
	}
	if(no_finger(now + 30) != TOUCH_NONE) return EARLY;
	return no_finger(now + 60);
}

// A touch at the centre that began at 1000, with a first reading that failed at `first_failed`
static void failing_touch(uint64_t first_failed)
{
	touch_init(&touch);
	finger(240, 240, 1000);
	failed(first_failed);
}

/*
 * A crash or a hang of the module must be a failed check and not the end of the test: what could crash runs
 * in a child process that hands its numbers back through a pipe.
 */
static bool in_child(void (*work)(long *results), long *results, size_t count)
{
	size_t size = count * sizeof(results[0]), have = 0;
	int ends[2], status = 0;
	bool complete = false;
	pid_t child;

	fflush(stdout);
	if(pipe(ends) != 0) return false;
	child = fork();
	if(child == 0)
	{
		close(ends[0]);
		// A module that never returns ends the child here; the walks take a few seconds
		alarm(120);
		work(results);
		fflush(stdout);
		_exit(write(ends[1], results, size) == (ssize_t)size ? 0 : 3);
	}
	close(ends[1]);
	if(child > 0)
	{
		ssize_t got;

		while(have < size && (got = read(ends[0], (char *)results + have, size - have)) > 0) have += (size_t)got;
		complete = waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 && have == size;
	}
	close(ends[0]);
	return complete;
}

static void test_constants(void)
{
	check(TOUCH_SIZE == 480 && TOUCH_TAP_MS == 600 && TOUCH_TAP_MOVE == 20 && TOUCH_SWIPE_MS == 1000 && TOUCH_SWIPE_MOVE == 60 &&
	      TOUCH_LIFT_READINGS == 2 && TOUCH_LOST_MS == 300,
	      "480 pixels, a tap within 600 ms and 20 pixels, a swipe within 1000 ms from 60 pixels, lifted after 2 readings, lost after 300 ms");
	check(TOUCH_NONE == 0 && TOUCH_TAP == 1 && TOUCH_SWIPE_LEFT == 2 && TOUCH_SWIPE_RIGHT == 3 && TOUCH_SWIPE_UP == 4 && TOUCH_SWIPE_DOWN == 5,
	      "no gesture is the value 0, the gestures follow in the order of the header");
}

static void test_init(void)
{
	// Whatever stood in the memory before
	touch.down = touch.moved = touch.failing = true;
	touch.start_x = touch.start_y = touch.last_x = touch.last_y = touch.lifted = 77777;
	touch.start_ms = touch.failing_since_ms = touch.clock_ms = 99999;
	touch_init(&touch);
	check(!touch_is_down(&touch), "after the start no touch is going on, whatever stood in the memory");
	check(silent(no_finger(1000)) && silent(no_finger(1030)) && silent(no_finger(1060)) && !touch_is_down(&touch),
	      "readings without a finger after the start: no gesture, no touch");

	touch_init(&touch);
	finger(240, 240, 1000);
	touch_init(&touch);
	check(!touch_is_down(&touch) && silent(no_finger(1030)) && silent(no_finger(1060)), "a touch from before a restart is forgotten: no gesture when it is lifted");

	touch_init(&touch);
	finger(240, 240, 50000);
	touch_init(&touch);
	finger(240, 240, 1000);
	no_finger(1571);
	check(silent(no_finger(1601)), "the time starts anew with a restart: a touch of 601 ms after it is no tap");
}

static void test_tap(void)
{
	touch_init(&touch);
	check(silent(finger(240, 240, 1000)) && touch_is_down(&touch), "a finger goes down: no gesture, a touch is going on");
	check(silent(finger(240, 240, 1030)) && silent(finger(240, 240, 1060)) && touch_is_down(&touch), "the finger stays: no gesture while it is down");
	check(silent(no_finger(1090)), "the first reading without a finger: no gesture yet");
	check(touch_is_down(&touch), "after one reading without a finger the touch still goes on");
	check(no_finger(1120) == TOUCH_TAP, "the second reading in a row without a finger: lifted, a tap");
	check(tap_x == 240 && tap_y == 240, "a tap at the centre reports the point (240, 240)");
	check(!touch_is_down(&touch), "after the lift no touch is going on");
	check(silent(no_finger(1150)) && silent(no_finger(1180)) && silent(no_finger(1700)), "the tap is reported exactly once");

	check(tap_at(stroke(0, 0, 0, 0, 90), 0, 0), "a tap at the corner (0, 0)");
	check(tap_at(stroke(479, 0, 479, 0, 90), 479, 0), "a tap at the corner (479, 0)");
	check(tap_at(stroke(0, 479, 0, 479, 90), 0, 479), "a tap at the corner (0, 479)");
	check(tap_at(stroke(479, 479, 479, 479, 90), 479, 479), "a tap at the corner (479, 479)");
	check(tap_at(stroke(100, 200, 115, 185, 90), 100, 200), "the tap point is where the finger went down, not where it was lifted");

	touch_init(&touch);
	finger(100, 200, 1000);
	no_finger(1030);
	check(tap_at(no_finger(1060), 100, 200), "one reading with a finger is a touch: a tap");
	touch_init(&touch);
	finger(100, 200, 1000);
	no_finger(1000);
	check(tap_at(no_finger(1000), 100, 200), "down and lifted within the same millisecond: a tap");
}

static void test_tap_time(void)
{
	check(tap_at(stroke(240, 240, 240, 240, 599), 240, 240), "lifted after 599 ms: a tap");
	check(tap_at(stroke(240, 240, 240, 240, 600), 240, 240), "lifted after 600 ms: a tap");
	check(only(stroke(240, 240, 240, 240, 601), TOUCH_NONE), "lifted after 601 ms: no tap, nothing");
	check(only(stroke(240, 240, 240, 240, 1000), TOUCH_NONE), "lifted after 1000 ms without moving: nothing, the time of a swipe does not make a tap");

	touch_init(&touch);
	finger(240, 240, 1000);
	no_finger(1600);
	check(silent(no_finger(1601)), "the first reading without a finger after 600 ms, the second after 601: no tap, the time runs until it counts as lifted");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(240, 240, 1500);
	no_finger(1570);
	check(silent(no_finger(1601)), "the time of a tap counts from the first reading with the finger, not from the last");
	touch_init(&touch);
	finger(240, 240, 1000);
	finger(240, 240, 1500);
	no_finger(1570);
	check(tap_at(no_finger(1600), 240, 240), "600 ms from the first of several readings with the finger: a tap");
}

static void test_tap_distance(void)
{
	static const int right[2][2] = {{261, 240}, {240, 240}}, left[2][2] = {{219, 240}, {240, 240}};
	static const int up[2][2] = {{240, 219}, {240, 240}}, down[2][2] = {{240, 261}, {240, 240}};
	static const int at_limit[5][2] = {{260, 240}, {220, 240}, {240, 260}, {240, 220}, {240, 240}};
	static const int first_left[3][2] = {{261, 240}, {250, 240}, {245, 240}};
	static const int jitter[6][2] = {{243, 238}, {237, 244}, {250, 250}, {230, 229}, {259, 221}, {241, 240}};
	static const int creep[3][2] = {{255, 240}, {270, 240}, {285, 240}};

	check(tap_at(stroke(240, 240, 260, 240, 90), 240, 240), "20 pixels to the right: a tap");
	check(only(stroke(240, 240, 261, 240, 90), TOUCH_NONE), "21 pixels to the right: no tap, nothing");
	check(tap_at(stroke(240, 240, 220, 240, 90), 240, 240), "20 pixels to the left: a tap");
	check(only(stroke(240, 240, 219, 240, 90), TOUCH_NONE), "21 pixels to the left: no tap, nothing");
	check(tap_at(stroke(240, 240, 240, 260, 90), 240, 240), "20 pixels down: a tap");
	check(only(stroke(240, 240, 240, 261, 90), TOUCH_NONE), "21 pixels down: no tap, nothing");
	check(tap_at(stroke(240, 240, 240, 220, 90), 240, 240), "20 pixels up: a tap");
	check(only(stroke(240, 240, 240, 219, 90), TOUCH_NONE), "21 pixels up: no tap, nothing");
	check(tap_at(stroke(240, 240, 260, 220, 90), 240, 240) && tap_at(stroke(240, 240, 220, 260, 90), 240, 240),
	      "20 pixels in x and in y at once: a tap, each axis counts by itself");
	check(only(stroke(240, 240, 261, 220, 90), TOUCH_NONE) && only(stroke(240, 240, 220, 261, 90), TOUCH_NONE),
	      "21 pixels in one axis and 20 in the other: no tap");
	check(tap_at(stroke(0, 479, 20, 459, 90), 0, 479) && only(stroke(0, 479, 21, 479, 90), TOUCH_NONE) && only(stroke(0, 479, 0, 458, 90), TOUCH_NONE),
	      "the 20 pixels count from where the finger went down, also at a corner");

	check(only(path(right, 2), TOUCH_NONE), "21 pixels to the right and back: no tap, the points in between count");
	check(only(path(left, 2), TOUCH_NONE), "21 pixels to the left and back: no tap");
	check(only(path(up, 2), TOUCH_NONE), "21 pixels up and back: no tap");
	check(only(path(down, 2), TOUCH_NONE), "21 pixels down and back: no tap");
	check(only(path(first_left, 3), TOUCH_NONE), "out of the range with the first movement, then two readings inside it: no tap");
	touch_init(&touch);
	finger(240, 240, 1000);
	finger(261, 240, 1000);
	finger(240, 240, 1000);
	no_finger(1000);
	check(only(no_finger(1000), TOUCH_NONE), "21 pixels to the right and back within the same millisecond: no tap, however quick");
	check(only(path(creep, 3), TOUCH_NONE), "a finger creeping 15 pixels with every reading to 45 pixels: no tap, the way counts from where it went down");
	check(tap_at(path(at_limit, 5), 240, 240), "20 pixels to each side and back: a tap");
	check(tap_at(path(jitter, 6), 240, 240), "a finger that trembles within the range: a tap at the point where it went down");
}

static void test_rest(void)
{
	int wrong = 0;
	uint64_t now;

	touch_init(&touch);
	for(now = 1000; now <= 3970; now += 30)
	{
		if(!silent(finger(240, 240, now)) || !touch_is_down(&touch)) wrong++;
	}
	check(wrong == 0, "a finger resting on the glass for three seconds: no gesture, the touch goes on");
	check(silent(no_finger(4000)) && silent(no_finger(4030)) && !touch_is_down(&touch), "the resting finger is lifted: nothing");
	check(silent(no_finger(4060)) && silent(no_finger(9000)), "nothing follows a touch that was no gesture");

	touch_init(&touch);
	finger(240, 240, 1000);
	check(silent(finger(240, 240, 61000)) && touch_is_down(&touch), "a finger resting for a minute: the touch goes on");
	check(silent(finger(240, 240, 3601000)) && touch_is_down(&touch), "a finger resting for an hour: the touch goes on");
	check(silent(finger(240, 240, ((uint64_t)1 << 32) + 5000)) && touch_is_down(&touch), "a finger resting for more than 2^32 ms: the touch goes on");
	check(silent(no_finger(((uint64_t)1 << 32) + 5030)) && touch_is_down(&touch) && silent(no_finger(((uint64_t)1 << 32) + 5060)) && !touch_is_down(&touch),
	      "the finger that rested for more than 2^32 ms is lifted with the second reading without it: nothing");
}

static void test_swipe(void)
{
	check(only(stroke(240, 240, 180, 240, 90), TOUCH_SWIPE_LEFT), "60 pixels to the left, x became smaller: a swipe to the left");
	check(only(stroke(240, 240, 300, 240, 90), TOUCH_SWIPE_RIGHT), "60 pixels to the right: a swipe to the right");
	check(only(stroke(240, 240, 240, 180, 90), TOUCH_SWIPE_UP), "60 pixels up, y became smaller: a swipe up");
	check(only(stroke(240, 240, 240, 300, 90), TOUCH_SWIPE_DOWN), "60 pixels down: a swipe down");

	check(only(stroke(240, 240, 181, 240, 90), TOUCH_NONE), "59 pixels to the left: nothing");
	check(only(stroke(240, 240, 299, 240, 90), TOUCH_NONE), "59 pixels to the right: nothing");
	check(only(stroke(240, 240, 240, 181, 90), TOUCH_NONE), "59 pixels up: nothing");
	check(only(stroke(240, 240, 240, 299, 90), TOUCH_NONE), "59 pixels down: nothing");

	check(only(stroke(240, 240, 179, 240, 90), TOUCH_SWIPE_LEFT) && only(stroke(240, 240, 301, 240, 90), TOUCH_SWIPE_RIGHT) &&
	      only(stroke(240, 240, 240, 179, 90), TOUCH_SWIPE_UP) && only(stroke(240, 240, 240, 301, 90), TOUCH_SWIPE_DOWN),
	      "61 pixels in each direction: a swipe");
	check(only(stroke(479, 240, 0, 240, 90), TOUCH_SWIPE_LEFT) && only(stroke(0, 240, 479, 240, 90), TOUCH_SWIPE_RIGHT) &&
	      only(stroke(240, 479, 240, 0, 90), TOUCH_SWIPE_UP) && only(stroke(240, 0, 240, 479, 90), TOUCH_SWIPE_DOWN),
	      "from one edge of the screen to the other: a swipe in each direction");
	check(only(stroke(40, 40, 100, 40, 90), TOUCH_SWIPE_RIGHT) && only(stroke(440, 440, 440, 380, 90), TOUCH_SWIPE_UP),
	      "the 60 pixels count from where the finger went down, wherever that is");
	check(only(stroke(100, 300, 200, 300, 90), TOUCH_SWIPE_RIGHT) && only(stroke(300, 100, 200, 100, 90), TOUCH_SWIPE_LEFT),
	      "from a point with different x and y: the direction of a horizontal swipe is judged by x alone");
	check(only(stroke(300, 100, 300, 200, 90), TOUCH_SWIPE_DOWN) && only(stroke(100, 300, 100, 200, 90), TOUCH_SWIPE_UP),
	      "from a point with different x and y: the direction of a vertical swipe is judged by y alone");
	check(only(stroke(100, 300, 130, 300, 90), TOUCH_NONE) && only(stroke(100, 300, 100, 270, 90), TOUCH_NONE) &&
	      only(stroke(300, 100, 270, 100, 90), TOUCH_NONE) && only(stroke(300, 100, 300, 130, 90), TOUCH_NONE),
	      "30 pixels from a point with different x and y: nothing, each axis is compared with its own start");
	check(only(stroke(0, 100, 0, 300, 90), TOUCH_SWIPE_DOWN) && only(stroke(479, 300, 479, 100, 90), TOUCH_SWIPE_UP) &&
	      only(stroke(100, 0, 300, 0, 90), TOUCH_SWIPE_RIGHT) && only(stroke(300, 479, 100, 479, 90), TOUCH_SWIPE_LEFT),
	      "swipes along the four edges of the screen");
}

static void test_swipe_factor(void)
{
	check(only(stroke(240, 240, 340, 290, 90), TOUCH_SWIPE_RIGHT), "100 to the right and 50 down: twice as far, a swipe to the right");
	check(only(stroke(240, 240, 340, 291, 90), TOUCH_NONE), "100 to the right and 51 down: less than twice as far, nothing");
	check(only(stroke(240, 240, 339, 290, 90), TOUCH_NONE), "99 to the right and 50 down: nothing");
	check(only(stroke(240, 240, 340, 190, 90), TOUCH_SWIPE_RIGHT), "100 to the right and 50 up: a swipe to the right");
	check(only(stroke(240, 240, 340, 189, 90), TOUCH_NONE), "100 to the right and 51 up: nothing");

	check(only(stroke(240, 240, 140, 290, 90), TOUCH_SWIPE_LEFT), "100 to the left and 50 down: a swipe to the left");
	check(only(stroke(240, 240, 140, 291, 90), TOUCH_NONE), "100 to the left and 51 down: nothing");
	check(only(stroke(240, 240, 141, 290, 90), TOUCH_NONE), "99 to the left and 50 down: nothing");
	check(only(stroke(240, 240, 140, 190, 90), TOUCH_SWIPE_LEFT), "100 to the left and 50 up: a swipe to the left");
	check(only(stroke(240, 240, 140, 189, 90), TOUCH_NONE), "100 to the left and 51 up: nothing");

	check(only(stroke(240, 240, 290, 340, 90), TOUCH_SWIPE_DOWN), "100 down and 50 to the right: a swipe down");
	check(only(stroke(240, 240, 291, 340, 90), TOUCH_NONE), "100 down and 51 to the right: nothing");
	check(only(stroke(240, 240, 290, 339, 90), TOUCH_NONE), "99 down and 50 to the right: nothing");
	check(only(stroke(240, 240, 190, 340, 90), TOUCH_SWIPE_DOWN), "100 down and 50 to the left: a swipe down");
	check(only(stroke(240, 240, 189, 340, 90), TOUCH_NONE), "100 down and 51 to the left: nothing");

	check(only(stroke(240, 240, 290, 140, 90), TOUCH_SWIPE_UP), "100 up and 50 to the right: a swipe up");
	check(only(stroke(240, 240, 291, 140, 90), TOUCH_NONE), "100 up and 51 to the right: nothing");
	check(only(stroke(240, 240, 290, 141, 90), TOUCH_NONE), "99 up and 50 to the right: nothing");
	check(only(stroke(240, 240, 190, 140, 90), TOUCH_SWIPE_UP), "100 up and 50 to the left: a swipe up");
	check(only(stroke(240, 240, 189, 140, 90), TOUCH_NONE), "100 up and 51 to the left: nothing");

	check(only(stroke(240, 240, 300, 270, 90), TOUCH_SWIPE_RIGHT) && only(stroke(240, 240, 210, 180, 90), TOUCH_SWIPE_UP),
	      "60 pixels in one axis and 30 in the other: a swipe, both limits met exactly");
	check(only(stroke(240, 240, 300, 271, 90), TOUCH_NONE) && only(stroke(240, 240, 209, 180, 90), TOUCH_NONE),
	      "60 pixels in one axis and 31 in the other: nothing");
	check(only(stroke(240, 240, 299, 269, 90), TOUCH_NONE) && only(stroke(240, 240, 211, 181, 90), TOUCH_NONE),
	      "59 pixels in one axis and 29 in the other: nothing, although twice as far");
	check(only(stroke(240, 240, 360, 300, 90), TOUCH_SWIPE_RIGHT) && only(stroke(240, 240, 180, 360, 90), TOUCH_SWIPE_DOWN),
	      "60 pixels or more in both axes: the axis with twice the way decides");
	check(only(stroke(240, 240, 360, 301, 90), TOUCH_NONE) && only(stroke(240, 240, 179, 360, 90), TOUCH_NONE),
	      "120 pixels in one axis and 61 in the other: nothing");
	check(only(stroke(240, 240, 340, 340, 90), TOUCH_NONE) && only(stroke(240, 240, 140, 140, 90), TOUCH_NONE) &&
	      only(stroke(240, 240, 340, 140, 90), TOUCH_NONE) && only(stroke(240, 240, 140, 340, 90), TOUCH_NONE),
	      "a diagonal smear of 100 pixels in both axes, in each of the four directions: nothing");
	check(only(stroke(0, 0, 479, 239, 90), TOUCH_SWIPE_RIGHT) && only(stroke(0, 0, 479, 240, 90), TOUCH_NONE),
	      "479 to the right and 239 down is a swipe, 479 and 240 is none");
	check(only(stroke(0, 0, 400, 200, 90), TOUCH_SWIPE_RIGHT) && only(stroke(0, 0, 200, 400, 90), TOUCH_SWIPE_DOWN) &&
	      only(stroke(479, 479, 79, 279, 90), TOUCH_SWIPE_LEFT) && only(stroke(479, 479, 279, 79, 90), TOUCH_SWIPE_UP),
	      "400 pixels in one axis and 200 in the other: twice as far is enough at a long way as well");
	check(only(stroke(0, 0, 400, 201, 90), TOUCH_NONE) && only(stroke(0, 0, 201, 400, 90), TOUCH_NONE) &&
	      only(stroke(479, 479, 79, 278, 90), TOUCH_NONE) && only(stroke(479, 479, 278, 79, 90), TOUCH_NONE),
	      "400 pixels in one axis and 201 in the other: nothing");
}

static void test_swipe_time(void)
{
	check(only(stroke(240, 240, 340, 240, 601), TOUCH_SWIPE_RIGHT), "a swipe lifted after 601 ms: the 600 ms of the tap do not apply to it");
	check(only(stroke(240, 240, 340, 240, 999), TOUCH_SWIPE_RIGHT), "a swipe to the right lifted after 999 ms: a swipe");
	check(only(stroke(240, 240, 340, 240, 1000), TOUCH_SWIPE_RIGHT), "a swipe to the right lifted after 1000 ms: a swipe");
	check(only(stroke(240, 240, 340, 240, 1001), TOUCH_NONE), "a swipe to the right lifted after 1001 ms: nothing");
	check(only(stroke(240, 240, 140, 240, 1000), TOUCH_SWIPE_LEFT) && only(stroke(240, 240, 140, 240, 1001), TOUCH_NONE),
	      "a swipe to the left lifted after 1000 ms is one, after 1001 ms none");
	check(only(stroke(240, 240, 240, 140, 1000), TOUCH_SWIPE_UP) && only(stroke(240, 240, 240, 140, 1001), TOUCH_NONE),
	      "a swipe up lifted after 1000 ms is one, after 1001 ms none");
	check(only(stroke(240, 240, 240, 340, 1000), TOUCH_SWIPE_DOWN) && only(stroke(240, 240, 240, 340, 1001), TOUCH_NONE),
	      "a swipe down lifted after 1000 ms is one, after 1001 ms none");

	check(only(stroke(240, 240, 479, 240, 1001), TOUCH_NONE) && only(stroke(240, 240, 240, 0, 1001), TOUCH_NONE),
	      "a swipe across half the screen lifted after 1001 ms: nothing, however far it went");
	check(only(stroke(240, 240, 479, 240, 3000), TOUCH_NONE) && only(stroke(240, 240, 240, 479, 3000), TOUCH_NONE),
	      "a swipe across half the screen lifted after 3000 ms: nothing");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(340, 240, 1900);
	no_finger(1970);
	check(only(no_finger(2001), TOUCH_NONE), "the time of a swipe counts from the first reading with the finger, not from the last");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(340, 240, 1000);
	no_finger(1000);
	check(only(no_finger(1000), TOUCH_SWIPE_RIGHT), "down, 100 pixels to the right and lifted within the same millisecond: a swipe");
	touch_init(&touch);
	finger(240, 240, 1000);
	finger(240, 140, 1000);
	no_finger(1000);
	check(only(no_finger(1000), TOUCH_SWIPE_UP), "down, 100 pixels up and lifted within the same millisecond: a swipe");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(340, 240, ((uint64_t)1 << 32) + 1500);
	no_finger(((uint64_t)1 << 32) + 1530);
	check(only(no_finger(((uint64_t)1 << 32) + 1560), TOUCH_NONE), "a swipe of 2^32 + 560 ms: nothing, the time is not counted in 32 bits");
}

static void test_swipe_path(void)
{
	static const int detour[2][2] = {{240, 400}, {100, 240}};
	static const int back[2][2] = {{400, 240}, {270, 240}};
	static const int back_to_limit[2][2] = {{400, 240}, {300, 240}};
	static const int over[2][2] = {{300, 240}, {250, 240}};
	static const int slow[6][2] = {{250, 241}, {270, 243}, {290, 244}, {310, 246}, {330, 247}, {350, 249}};
	int wrong = 0, i;

	check(only(path(detour, 2), TOUCH_SWIPE_LEFT), "160 pixels down and then to a point 140 to the left of the start: a swipe to the left, only the last point counts");
	check(only(path(back, 2), TOUCH_NONE), "160 pixels to the right and back to 30: no swipe and no tap");
	check(only(path(back_to_limit, 2), TOUCH_SWIPE_RIGHT), "160 pixels to the right and back to 60: a swipe to the right");
	check(only(path(over, 2), TOUCH_NONE), "60 pixels to the right and back to 10: the last point read before the lift is compared, nothing");

	touch_init(&touch);
	if(!silent(finger(240, 240, 1000))) wrong++;
	for(i = 0; i < 6; i++)
	{
		if(!silent(finger(slow[i][0], slow[i][1], 1030 + 30 * (uint64_t)i)) || !touch_is_down(&touch)) wrong++;
	}
	check(wrong == 0, "a finger moving across the screen: no gesture before it is lifted");
	check(silent(no_finger(1210)) && only(no_finger(1240), TOUCH_SWIPE_RIGHT), "the moving finger is lifted: a swipe to the right, judged at the lift");
	check(silent(no_finger(1270)) && silent(no_finger(1300)), "the swipe is reported exactly once");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(250, 240, 1030);
	reading(true, false, 340, 240, 1060);
	check(tap_at(reading(true, false, 340, 240, 1090), 240, 240), "the point given with a reading without a finger is not looked at: a tap, no swipe");
}

static void test_lift(void)
{
	touch_init(&touch);
	finger(100, 200, 1000);
	check(silent(no_finger(1030)) && touch_is_down(&touch) && silent(finger(105, 210, 1060)) && touch_is_down(&touch),
	      "a single reading without a finger inside a touch: the touch goes on");
	check(silent(no_finger(1090)) && touch_is_down(&touch), "a second single reading without a finger, with a finger between: the two are not in a row");
	finger(95, 190, 1120);
	no_finger(1150);
	check(tap_at(no_finger(1180), 100, 200), "the touch with two single readings without a finger ends as one tap at its first point");

	touch_init(&touch);
	finger(240, 240, 1000);
	no_finger(1030);
	finger(340, 240, 1060);
	no_finger(1090);
	check(only(no_finger(1120), TOUCH_SWIPE_RIGHT), "a single reading without a finger inside a swipe: one touch from the first point, a swipe");
	touch_init(&touch);
	finger(240, 240, 1000);
	no_finger(1030);
	finger(240, 340, 1060);
	no_finger(1090);
	check(only(no_finger(1120), TOUCH_SWIPE_DOWN), "a single reading without a finger inside a swipe down: one touch from the first point as well");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(270, 240, 1030);
	no_finger(1060);
	finger(240, 240, 1090);
	no_finger(1120);
	check(only(no_finger(1150), TOUCH_NONE), "a single reading without a finger does not forgive the movement before it: no tap");

	touch_init(&touch);
	finger(240, 240, 1000);
	no_finger(1300);
	finger(240, 240, 1400);
	no_finger(1571);
	check(silent(no_finger(1601)), "the time of a touch runs on over a single reading without a finger: 601 ms, no tap");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(240, 240, 2500);
	check(silent(no_finger(2530)) && touch_is_down(&touch) && silent(finger(240, 240, 2560)) && touch_is_down(&touch),
	      "a single reading without a finger after 1500 ms of a touch: the touch goes on as well");
	no_finger(2590);
	check(only(no_finger(2620), TOUCH_NONE), "the long touch with a single reading without a finger is one touch: nothing when it is lifted");

	touch_init(&touch);
	finger(240, 240, 1000);
	no_finger(1030);
	check(silent(failed(1060)) && touch_is_down(&touch), "a failed reading behind one without a finger does not lift");
	check(tap_at(no_finger(1090), 240, 240), "a failed reading between two readings without a finger does not break their row: lifted with the second");

	touch_init(&touch);
	finger(240, 240, 1000);
	failed(1030);
	check(silent(no_finger(1060)) && touch_is_down(&touch) && silent(failed(1090)) && touch_is_down(&touch),
	      "a failed reading is no reading without a finger: one of each do not lift");
	check(tap_at(no_finger(1120), 240, 240), "failed readings before and between: lifted with the second reading that saw no finger");
}

static void test_failed(void)
{
	touch_init(&touch);
	check(silent(reading(false, true, 240, 240, 1000)) && !touch_is_down(&touch), "a failed reading that claims a finger starts no touch");
	check(silent(no_finger(1030)) && silent(no_finger(1060)), "no gesture follows a failed reading that claimed a finger");

	touch_init(&touch);
	finger(240, 240, 1000);
	check(silent(reading(false, true, 400, 240, 1030)) && touch_is_down(&touch), "a failed reading inside a touch: no gesture, the touch goes on");
	no_finger(1060);
	check(tap_at(no_finger(1090), 240, 240), "the point of a failed reading is not looked at: the touch did not move, a tap");

	touch_init(&touch);
	finger(240, 240, 1000);
	check(silent(reading(false, false, 0, 0, 1030)) && silent(reading(false, false, 0, 0, 1060)) && silent(reading(false, false, 0, 0, 1090)) && touch_is_down(&touch),
	      "failed readings that claim no finger do not lift it");
	no_finger(1120);
	check(tap_at(no_finger(1150), 240, 240), "after failed readings the touch ends as usual: a tap");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(340, 240, 1030);
	failed(1060);
	no_finger(1090);
	check(only(no_finger(1120), TOUCH_SWIPE_RIGHT), "a failed reading before a swipe is lifted: the swipe is judged from the points read before it");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(270, 240, 1030);
	failed(1060);
	finger(240, 240, 1090);
	no_finger(1120);
	check(only(no_finger(1150), TOUCH_NONE), "a failed reading does not forgive the movement before it: no tap");

	touch_init(&touch);
	finger(240, 240, 1000);
	failed(1500);
	no_finger(1571);
	check(silent(no_finger(1601)), "the time of a touch runs on over a failed reading: 601 ms, no tap");
}

static void test_lost(void)
{
	uint64_t now;
	int wrong = 0;

	failing_touch(1030);
	check(silent(failed(1329)) && touch_is_down(&touch), "readings fail for 299 ms: the touch goes on");
	no_finger(1359);
	check(tap_at(no_finger(1389), 240, 240), "the touch that lost 299 ms is lifted: a tap");

	failing_touch(1030);
	check(silent(failed(1330)) && !touch_is_down(&touch), "readings fail for 300 ms: the touch is dropped, no gesture");
	check(silent(no_finger(1360)) && silent(no_finger(1390)), "the dropped touch is lifted: no gesture");

	failing_touch(1030);
	for(now = 1060; now <= 1300; now += 30)
	{
		if(!silent(failed(now)) || !touch_is_down(&touch)) wrong++;
	}
	check(wrong == 0, "readings fail every 30 ms for 270 ms: the touch goes on");
	check(silent(failed(1330)) && !touch_is_down(&touch), "the 300 ms count from the first of the failed readings, not from the one before");

	failing_touch(1030);
	check(silent(failed(1500)) && !touch_is_down(&touch), "the second failed reading comes 470 ms after the first: dropped, more than 300 ms count as well");
	failing_touch(1030);
	check(silent(failed(3601030)) && !touch_is_down(&touch), "readings fail for an hour: dropped");
	failing_touch(1030);
	check(silent(failed(((uint64_t)1 << 32) + 1130)) && !touch_is_down(&touch), "readings fail for 2^32 + 100 ms: dropped, the time is not counted in 32 bits");

	failing_touch(1400);
	check(touch_is_down(&touch) && silent(failed(1699)) && touch_is_down(&touch), "the first failed reading comes 400 ms after the last good one: the 300 ms begin with it");
	check(silent(failed(1700)) && !touch_is_down(&touch), "dropped 300 ms after the first failed reading");

	failing_touch(1030);
	failed(1230);
	finger(240, 240, 1260);
	failed(1290);
	check(silent(failed(1490)) && touch_is_down(&touch), "readings fail twice for 200 ms with a good one between: the times do not add up");
	no_finger(1520);
	check(tap_at(no_finger(1550), 240, 240), "the touch with two short losses ends as a tap");

	failing_touch(1030);
	failed(1230);
	no_finger(1260);
	failed(1290);
	check(silent(failed(1490)) && touch_is_down(&touch), "a reading without a finger between failed ones ends their row as well");
	check(tap_at(no_finger(1520), 240, 240), "the second reading without a finger behind the failed ones: a tap");

	failing_touch(1030);
	check(silent(finger(240, 240, 1500)) && touch_is_down(&touch), "one failed reading and a pause of the readings are not 300 ms of failing");
	no_finger(1530);
	check(tap_at(no_finger(1560), 240, 240), "the touch with one failed reading and a pause ends as a tap");

	touch_init(&touch);
	failed(1000);
	failed(1500);
	finger(240, 240, 1530);
	failed(1560);
	check(silent(failed(1859)) && touch_is_down(&touch), "readings that failed before the touch began do not count for it");
	check(silent(failed(1860)) && !touch_is_down(&touch), "dropped 300 ms after the first failed reading inside the touch");

	touch_init(&touch);
	finger(240, 240, 1000);
	no_finger(1030);
	failed(1060);
	check(silent(failed(1360)) && !touch_is_down(&touch), "readings fail for 300 ms behind one without a finger: dropped as well");
	check(silent(no_finger(1390)) && silent(no_finger(1420)), "the reading without a finger from before the loss lifts nothing any more");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(340, 240, 1030);
	failed(1060);
	failed(1360);
	check(silent(no_finger(1390)) && silent(no_finger(1420)), "a swipe that was dropped is no gesture");
}

static void test_after_lost(void)
{
	touch_init(&touch);
	finger(100, 200, 1000);
	failed(1030);
	failed(1330);
	check(silent(finger(300, 400, 1900)) && touch_is_down(&touch), "a finger still on the screen after a touch was dropped starts a new touch");
	no_finger(2470);
	check(tap_at(no_finger(2500), 300, 400), "the new touch has its own point and time: a tap 600 ms after it began");

	touch_init(&touch);
	finger(100, 200, 1000);
	failed(1030);
	failed(1330);
	finger(300, 400, 1900);
	no_finger(2470);
	check(silent(no_finger(2501)), "the new touch after a dropped one is lifted after 601 ms: no tap");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(400, 240, 1030);
	failed(1060);
	failed(1360);
	finger(400, 240, 1390);
	no_finger(1420);
	check(tap_at(no_finger(1450), 400, 240), "a touch that moved was dropped: the new touch behind it did not move, a tap");

	touch_init(&touch);
	finger(240, 240, 1000);
	no_finger(1030);
	failed(1060);
	failed(1360);
	finger(100, 200, 1390);
	check(silent(no_finger(1420)) && touch_is_down(&touch), "a touch dropped behind one reading without a finger: the new touch needs two of its own");
	check(tap_at(no_finger(1450), 100, 200), "the new touch is lifted with its second reading without a finger");
}

static void test_outside(void)
{
	static const int outside[10][2] = {{480, 240}, {-1, 240}, {240, 480}, {240, -1}, {480, 480}, {-1, -1}, {INT_MAX, 240}, {INT_MIN, 240}, {240, INT_MAX}, {240, INT_MIN}};
	static const char *const starts[10] = {
		"x of 480 is outside the screen: no touch starts", "x of -1 is outside the screen: no touch starts",
		"y of 480 is outside the screen: no touch starts", "y of -1 is outside the screen: no touch starts",
		"x and y of 480: no touch starts", "x and y of -1: no touch starts",
		"the largest x: no touch starts", "the smallest x: no touch starts", "the largest y: no touch starts", "the smallest y: no touch starts",
	};
	static const char *const inside[10] = {
		"x of 480 inside a touch says nothing: it did not move, a tap", "x of -1 inside a touch says nothing: a tap",
		"y of 480 inside a touch says nothing: a tap", "y of -1 inside a touch says nothing: a tap",
		"x and y of 480 inside a touch say nothing: a tap", "x and y of -1 inside a touch say nothing: a tap",
		"the largest x inside a touch says nothing: a tap", "the smallest x inside a touch says nothing: a tap",
		"the largest y inside a touch says nothing: a tap", "the smallest y inside a touch says nothing: a tap",
	};
	int i;

	for(i = 0; i < 10; i++)
	{
		touch_init(&touch);
		check(silent(finger(outside[i][0], outside[i][1], 1000)) && !touch_is_down(&touch) && silent(no_finger(1030)) && silent(no_finger(1060)), starts[i]);
		check(tap_at(stroke(240, 240, outside[i][0], outside[i][1], 90), 240, 240), inside[i]);
	}

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(290, 240, 1030);
	finger(480, 240, 1060);
	no_finger(1090);
	check(only(no_finger(1120), TOUCH_NONE), "a point outside the screen is not the last point of a touch: 50 pixels to the last one inside, nothing");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(500, 240, 1030);
	check(silent(finger(500, 240, 1329)) && touch_is_down(&touch), "points outside the screen for 299 ms: the touch goes on");
	check(silent(finger(500, 240, 1330)) && !touch_is_down(&touch), "points outside the screen for 300 ms: the touch is dropped like with failed readings");

	touch_init(&touch);
	finger(240, 240, 1000);
	failed(1030);
	finger(240, -5, 1180);
	check(silent(failed(1329)) && touch_is_down(&touch) && silent(finger(-5, 240, 1330)) && !touch_is_down(&touch),
	      "failed readings and points outside the screen in a row count together: dropped after 300 ms");

	touch_init(&touch);
	finger(240, 240, 1000);
	no_finger(1030);
	check(silent(finger(-1, 240, 1060)) && touch_is_down(&touch), "a point outside the screen is no reading without a finger: not lifted");
	check(tap_at(no_finger(1090), 240, 240), "a point outside the screen between two readings without a finger does not break their row");

	touch_init(&touch);
	finger(240, 240, 1000);
	reading(true, false, -1, 480, 1030);
	check(tap_at(reading(true, false, 9999, -9999, 1060), 240, 240), "without a finger x and y are not looked at: readings with points outside the screen lift the finger");

	touch_init(&touch);
	finger(240, 240, 1000);
	failed(1030);
	reading(true, false, -1, -1, 1200);
	failed(1230);
	check(silent(failed(1400)) && touch_is_down(&touch), "a reading without a finger and a point outside the screen is no failed reading: it ends their row");
}

static void test_first_reading(void)
{
	touch_init(&touch);
	check(silent(finger(100, 200, 5000)) && touch_is_down(&touch), "a finger on the screen at the very first reading starts a touch");
	no_finger(5570);
	check(tap_at(no_finger(5600), 100, 200), "the touch of the first reading is lifted 600 ms after that reading: a tap, its time counts from there");

	touch_init(&touch);
	finger(100, 200, 5000);
	no_finger(5571);
	check(silent(no_finger(5601)), "the touch of the first reading is lifted 601 ms after it: no tap");

	touch_init(&touch);
	check(silent(finger(100, 200, 0)) && touch_is_down(&touch), "a finger at the time 0 starts a touch");
	no_finger(570);
	check(tap_at(no_finger(600), 100, 200), "a touch that begins at the time 0 and is lifted at 600: a tap");
	touch_init(&touch);
	finger(100, 200, 0);
	no_finger(570);
	check(silent(no_finger(601)), "a touch that begins at the time 0 and is lifted at 601: no tap");
}

static void test_clock(void)
{
	touch_init(&touch);
	finger(240, 240, 1000);
	finger(240, 240, 700);
	no_finger(1570);
	check(tap_at(no_finger(1600), 240, 240), "a reading with a time before the one before it lets no time pass: a tap 600 ms after it began");
	touch_init(&touch);
	finger(240, 240, 1000);
	finger(240, 240, 700);
	no_finger(1570);
	check(silent(no_finger(1601)), "601 ms after it began, with a reading of an earlier time in between: no tap");

	touch_init(&touch);
	no_finger(5000);
	finger(100, 200, 2000);
	no_finger(5570);
	check(tap_at(no_finger(5600), 100, 200), "a touch that begins with a time before the latest one seen begins at the latest: a tap 600 ms after that");
	touch_init(&touch);
	no_finger(5000);
	finger(100, 200, 2000);
	no_finger(5571);
	check(silent(no_finger(5601)), "601 ms after the latest time seen when the touch began: no tap");

	touch_init(&touch);
	finger(240, 240, 5000);
	no_finger(4000);
	check(tap_at(no_finger(3000), 240, 240), "lifted with times before the touch began: no time passed, a tap");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(240, 240, 3000);
	no_finger(500);
	check(silent(no_finger(600)), "2000 ms passed before the time stepped back: no tap, whatever the caller's time says");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(240, 240, 500);
	no_finger(800);
	check(tap_at(no_finger(1600), 240, 240), "the time between two earlier times does not pass either: only the latest time seen counts");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(340, 240, 400);
	no_finger(1970);
	check(only(no_finger(2000), TOUCH_SWIPE_RIGHT), "a swipe with a step back of the time, lifted 1000 ms after it began: a swipe");
	touch_init(&touch);
	finger(240, 240, 1000);
	finger(340, 240, 400);
	no_finger(1970);
	check(only(no_finger(2001), TOUCH_NONE), "a swipe with a step back of the time, lifted 1001 ms after it began: nothing");

	failing_touch(1030);
	failed(500);
	check(silent(failed(1329)) && touch_is_down(&touch), "the time steps back while readings fail: 299 ms counted, the touch goes on");
	check(silent(failed(1330)) && !touch_is_down(&touch), "300 ms counted over the step back: dropped");

	failing_touch(400);
	check(silent(failed(1299)) && touch_is_down(&touch), "the first failed reading has a time before the touch began: the 300 ms begin at the latest time seen");
	check(silent(failed(1300)) && !touch_is_down(&touch), "dropped 300 ms after the latest time seen at the first failed reading");
}

static void test_clock_limits(void)
{
	touch_init(&touch);
	finger(240, 240, UINT64_MAX - 600);
	no_finger(UINT64_MAX - 30);
	check(tap_at(no_finger(UINT64_MAX), 240, 240), "a touch that ends at the largest time, 600 ms long: a tap");

	touch_init(&touch);
	finger(240, 240, UINT64_MAX - 601);
	no_finger(UINT64_MAX - 30);
	check(silent(no_finger(UINT64_MAX)), "a touch that ends at the largest time, 601 ms long: no tap");

	touch_init(&touch);
	finger(240, 240, UINT64_MAX - 100);
	finger(240, 240, UINT64_MAX);
	no_finger(0);
	check(tap_at(no_finger(400), 240, 240), "the time wraps from the largest to 0 during a touch: a step back, a tap");

	touch_init(&touch);
	finger(240, 240, UINT64_MAX - 700);
	finger(240, 240, UINT64_MAX);
	no_finger(0);
	check(silent(no_finger(400)), "700 ms passed before the time wrapped: no tap");

	touch_init(&touch);
	finger(240, 240, 1000);
	no_finger((uint64_t)1 << 40);
	check(silent(no_finger(((uint64_t)1 << 40) + 30)), "a touch that lasted longer than 32 bits of milliseconds hold: nothing");
	touch_init(&touch);
	finger(240, 240, ((uint64_t)1 << 32) + 1000);
	no_finger(((uint64_t)1 << 32) + 1570);
	check(tap_at(no_finger(((uint64_t)1 << 32) + 1600), 240, 240), "a tap at times above 32 bits");
	touch_init(&touch);
	finger(240, 240, 1000);
	no_finger(((uint64_t)1 << 32) + 1000);
	check(silent(no_finger(((uint64_t)1 << 32) + 1400)), "a touch of 2^32 + 400 ms: no tap, the time is not counted in 32 bits");
}

static void test_is_down(void)
{
	touch_init(&touch);
	no_finger(1000);
	check(!touch_is_down(&touch), "no finger was read: touch_is_down() is false");
	finger(240, 240, 1030);
	check(touch_is_down(&touch), "a finger was read: touch_is_down() is true");
	failed(1060);
	check(touch_is_down(&touch), "one reading failed: touch_is_down() stays true");
	finger(400, 240, 1090);
	no_finger(1120);
	check(touch_is_down(&touch), "one reading without a finger: touch_is_down() stays true");
	no_finger(1150);
	check(!touch_is_down(&touch), "the finger counts as lifted: touch_is_down() is false");
	failed(1180);
	check(!touch_is_down(&touch), "a failed reading without a touch: touch_is_down() stays false");
	finger(240, 240, 1210);
	finger(240, 240, 5000);
	check(touch_is_down(&touch), "a finger resting long after a tap could be one: touch_is_down() stays true");
}

static void test_sequence(void)
{
	touch_init(&touch);
	finger(240, 240, 1000);
	finger(340, 240, 1030);
	no_finger(1060);
	check(only(no_finger(1090), TOUCH_SWIPE_RIGHT), "the scene: a swipe first");
	check(silent(finger(100, 200, 1120)) && silent(no_finger(1150)) && tap_at(no_finger(1180), 100, 200),
	      "a tap behind a swipe: the movement of the touch before does not count");
	check(silent(finger(300, 50, 1210)) && silent(no_finger(1240)) && tap_at(no_finger(1270), 300, 50), "a second tap at another point reports its own point");
	check(silent(finger(300, 50, 1300)) && silent(finger(300, 110, 1330)) && silent(no_finger(1360)) && only(no_finger(1390), TOUCH_SWIPE_DOWN),
	      "a swipe behind a tap is judged from its own first point");
	check(silent(no_finger(1420)) && silent(no_finger(1450)) && silent(failed(1480)) && silent(no_finger(1510)), "four gestures, each reported once");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(240, 300, 3000);
	no_finger(3030);
	check(only(no_finger(3060), TOUCH_NONE), "the scene: a touch that moved and lasted too long, nothing");
	finger(240, 300, 3090);
	no_finger(3120);
	check(tap_at(no_finger(3150), 240, 300), "a tap behind a touch that was nothing: time and movement of the touch before do not count");

	touch_init(&touch);
	finger(240, 240, 1000);
	no_finger(1030);
	no_finger(1060);
	finger(240, 240, 1090);
	no_finger(1120);
	check(tap_at(no_finger(1150), 240, 240), "two taps at the same point quickly behind each other: the second is a tap as well");

	touch_init(&touch);
	finger(240, 240, 1000);
	finger(340, 240, 1030);
	no_finger(1060);
	no_finger(1090);
	finger(240, 240, 1120);
	no_finger(1790);
	check(only(no_finger(1820), TOUCH_NONE), "a touch of 700 ms that did not move, behind a swipe: nothing, the last point of the touch before does not count");

	touch_init(&touch);
	finger(300, 50, 1000);
	no_finger(1030);
	no_finger(1060);
	finger(0, 0, 1090);
	no_finger(1120);
	check(tap_at(no_finger(1150), 0, 0), "a tap at (0, 0) behind a tap elsewhere reports (0, 0)");
	finger(479, 479, 1180);
	finger(479, 0, 1210);
	no_finger(1240);
	check(only(no_finger(1270), TOUCH_SWIPE_UP), "a swipe along the right edge behind a tap at (0, 0) is judged from its own first point");
}

// The places for the tap point may be NULL, each by itself
static void null_places(long *results)
{
	int x = UNSET, y = UNSET;
	touch_t unit;

	touch_init(&unit);
	touch_sample(&unit, true, true, 100, 200, 1000, NULL, NULL);
	touch_sample(&unit, true, false, 0, 0, 1030, NULL, NULL);
	results[0] = touch_sample(&unit, true, false, 0, 0, 1060, NULL, NULL) == TOUCH_TAP;

	touch_sample(&unit, true, true, 100, 200, 2000, &x, NULL);
	touch_sample(&unit, true, false, 0, 0, 2030, &x, NULL);
	results[1] = touch_sample(&unit, true, false, 0, 0, 2060, &x, NULL) == TOUCH_TAP && x == 100;

	touch_sample(&unit, true, true, 100, 200, 3000, NULL, &y);
	touch_sample(&unit, true, false, 0, 0, 3030, NULL, &y);
	results[2] = touch_sample(&unit, true, false, 0, 0, 3060, NULL, &y) == TOUCH_TAP && y == 200;

	touch_sample(&unit, true, true, 240, 240, 4000, NULL, NULL);
	touch_sample(&unit, true, true, 240, 340, 4030, NULL, NULL);
	touch_sample(&unit, false, true, 240, 340, 4060, NULL, NULL);
	touch_sample(&unit, true, false, 0, 0, 4090, NULL, NULL);
	results[3] = touch_sample(&unit, true, false, 0, 0, 4120, NULL, NULL) == TOUCH_SWIPE_DOWN && !touch_is_down(&unit);
}

static void test_null_places(void)
{
	long results[4] = {0, 0, 0, 0};
	bool complete = in_child(null_places, results, 4);

	check(complete, "readings without places for the tap point: no crash");
	check(complete && results[0] != 0, "a tap without places for its point is reported all the same");
	check(complete && results[1] != 0, "a tap with a place for x only: x is written");
	check(complete && results[2] != 0, "a tap with a place for y only: y is written");
	check(complete && results[3] != 0, "a whole touch without places for a point: the swipe is reported");
}

/*
 * The same rules written a second time, for the walks below: times that are counted up instead of points in
 * time that are compared, the way of the finger kept as how far it ever got to each side, and the four
 * directions of a swipe from one table.
 */
typedef struct
{
	bool touching;
	int first_x, first_y;       // where the finger went down
	int end_x, end_y;           // where it was read last
	int reach[4];               // how far it ever got from there: to the left, to the right, up, down
	uint64_t age;               // ms since it went down
	int gone;                   // readings without a finger since the last one with a finger
	bool blind;                 // the previous reading failed
	uint64_t blind_for;         // ms since the first of the readings that failed in a row
	uint64_t latest;            // the latest time of all calls
	// The touch that was lifted last, for the statistics of the walk
	uint64_t lifted_age;
	int lifted_reach, lifted_along, lifted_across;
} model_t;

static void model_init(model_t *model)
{
	memset(model, 0, sizeof(*model));
}

static touch_event_t model_sample(model_t *model, bool read_ok, bool down, int x, int y, uint64_t now_ms, int *tap_x_out, int *tap_y_out)
{
	static const int toward[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
	static const touch_event_t swipes[4] = {TOUCH_SWIPE_LEFT, TOUCH_SWIPE_RIGHT, TOUCH_SWIPE_UP, TOUCH_SWIPE_DOWN};
	uint64_t passed = now_ms > model->latest ? now_ms - model->latest : 0;
	bool finger_seen = read_ok && down && x >= 0 && x <= 479 && y >= 0 && y <= 479;
	bool nothing_seen = read_ok && !down;
	int i;

	model->latest += passed;
	model->age += passed;
	model->blind_for += passed;

	if(!finger_seen && !nothing_seen)
	{
		if(!model->blind) model->blind_for = 0;
		model->blind = true;
		if(model->blind_for >= 300) model->touching = false;
		return TOUCH_NONE;
	}
	model->blind = false;

	if(finger_seen)
	{
		if(!model->touching)
		{
			model->touching = true;
			model->first_x = x;
			model->first_y = y;
			model->age = 0;
			memset(model->reach, 0, sizeof(model->reach));
		}
		for(i = 0; i < 4; i++)
		{
			int went = (x - model->first_x) * toward[i][0] + (y - model->first_y) * toward[i][1];

			if(went > model->reach[i]) model->reach[i] = went;
		}
		model->end_x = x;
		model->end_y = y;
		model->gone = 0;
		return TOUCH_NONE;
	}

	if(!model->touching) return TOUCH_NONE;
	model->gone++;
	if(model->gone < 2) return TOUCH_NONE;
	model->touching = false;

	model->lifted_age = model->age;
	model->lifted_reach = 0;
	for(i = 0; i < 4; i++)
	{
		if(model->reach[i] > model->lifted_reach) model->lifted_reach = model->reach[i];
	}
	model->lifted_along = abs(model->end_x - model->first_x);
	model->lifted_across = abs(model->end_y - model->first_y);
	if(model->lifted_across > model->lifted_along)
	{
		int swap = model->lifted_along;

		model->lifted_along = model->lifted_across;
		model->lifted_across = swap;
	}

	if(model->age <= 600 && model->lifted_reach <= 20)
	{
		*tap_x_out = model->first_x;
		*tap_y_out = model->first_y;
		return TOUCH_TAP;
	}
	if(model->age > 1000) return TOUCH_NONE;
	for(i = 0; i < 4; i++)
	{
		int along = (model->end_x - model->first_x) * toward[i][0] + (model->end_y - model->first_y) * toward[i][1];
		int across = abs((model->end_x - model->first_x) * toward[i][1] + (model->end_y - model->first_y) * toward[i][0]);

		if(along >= 60 && across + across <= along) return swipes[i];
	}
	return TOUCH_NONE;
}

// What the walks came across, counted from the model and from what was fed, never from the module
typedef struct
{
	long calls, failed, outside, stepped_back, wrapped, bridged, dropped, kept_299, dropped_300, restarts, without_places, resting;
	long events[6];
	long tap_600, none_601;         // not moved and lifted after exactly 600 ms: a tap; after 601 ms: nothing
	long tap_20, none_21;           // lifted in time, at most 20 pixels away: a tap; 21 pixels: nothing
	long swipe_1000, none_1001;     // far enough and lifted after exactly 1000 ms: a swipe; after 1001 ms: nothing
	long swipe_60, none_59;         // in time, 60 pixels: a swipe; 59 pixels: nothing
	long swipe_twice, none_twice;   // in time, exactly twice as far as in the other axis: a swipe; just less: nothing
} seen_t;

typedef struct
{
	uint32_t random;
	uint64_t now;               // the time of the readings: it steps back now and then
	touch_t touch;
	model_t model;
	int last_x, last_y;         // of the noise
	// The properties, asked of the module alone
	bool finger_since;          // a finger was read since the last gesture
	int gestures;               // gestures since touch_is_down() turned true
	long different, twice, while_down, without_finger, off_screen;
	uint32_t seed;
	long step;
	int shown;
	seen_t *seen;
} walk_t;

static uint32_t walk_random(walk_t *walk, uint32_t below)
{
	walk->random = walk->random * 1664525u + 1013904223u;
	return (walk->random >> 8) % below;
}

#define ONE_OF(walk, table) ((table)[walk_random(walk, (uint32_t)(sizeof(table) / sizeof((table)[0])))])

static const int walk_outside[] = {-1, 480, 481, -480, 1000, INT_MAX, INT_MIN};

static void walk_pass(walk_t *walk, uint64_t ms)
{
	if(walk->now + ms < walk->now) walk->seen->wrapped++;
	walk->now += ms;
}

static void walk_restart(walk_t *walk)
{
	touch_init(&walk->touch);
	model_init(&walk->model);
	walk->finger_since = false;
	walk->gestures = 0;
	walk->seen->restarts++;
}

// One reading for the module and for the model, with both, one or none of the places for the tap point
static void feed(walk_t *walk, bool read_ok, bool down, int x, int y)
{
	static const char *const names[6] = {"none", "tap", "left", "right", "up", "down"};
	uint32_t places = walk_random(walk, 8);
	int got_x = UNSET, got_y = UNSET, expected_x = UNSET, expected_y = UNSET;
	int *x_out = places == 0 || places == 2 ? NULL : &got_x;
	int *y_out = places == 0 || places == 1 ? NULL : &got_y;
	bool finger_seen = read_ok && down && x >= 0 && x <= 479 && y >= 0 && y <= 479;
	bool was_down = touch_is_down(&walk->touch);
	model_t before = walk->model;
	seen_t *seen = walk->seen;
	touch_event_t event, expected;
	uint64_t now = walk->now;
	bool is_down;

	// A single reading with a time before the others
	if(walk_random(walk, 60) == 0) now = now > 2000 ? now - 1 - walk_random(walk, 2000) : 0;

	event = touch_sample(&walk->touch, read_ok, down, x, y, now, x_out, y_out);
	expected = model_sample(&walk->model, read_ok, down, x, y, now, &expected_x, &expected_y);
	is_down = touch_is_down(&walk->touch);
	if(x_out == NULL) expected_x = UNSET;
	if(y_out == NULL) expected_y = UNSET;
	walk->step++;

	if(event != expected || got_x != expected_x || got_y != expected_y || is_down != walk->model.touching)
	{
		if(walk->shown++ < 3)
		{
			printf("  walk %lu, reading %ld (%s, %s, %d, %d) at %llu: expected %s (%d, %d) %s, got %s (%d, %d) %s\n",
			       (unsigned long)walk->seed, walk->step, read_ok ? "ok" : "failed", down ? "finger" : "no finger", x, y, (unsigned long long)now,
			       names[expected], expected_x, expected_y, walk->model.touching ? "down" : "up",
			       (unsigned)event < 6 ? names[event] : "?", got_x, got_y, is_down ? "down" : "up");
		}
		walk->different++;
	}

	if(finger_seen) walk->finger_since = true;
	if(!was_down && is_down) walk->gestures = 0;
	if(event != TOUCH_NONE)
	{
		if(++walk->gestures > 1) walk->twice++;
		if(!read_ok || down || is_down) walk->while_down++;
		if(!walk->finger_since) walk->without_finger++;
		walk->finger_since = false;
		if(event == TOUCH_TAP && ((x_out != NULL && (got_x < 0 || got_x > 479)) || (y_out != NULL && (got_y < 0 || got_y > 479)))) walk->off_screen++;
	}

	seen->calls++;
	seen->events[expected]++;
	if(!read_ok) seen->failed++;
	if(read_ok && down && !finger_seen) seen->outside++;
	if(now < before.latest) seen->stepped_back++;
	if(places == 0) seen->without_places++;
	if(finger_seen && before.touching && before.gone == 1) seen->bridged++;
	if(finger_seen && walk->model.age >= 60000) seen->resting++;
	if(before.touching && walk->model.blind)
	{
		if(!walk->model.touching) seen->dropped++;
		if(walk->model.touching && walk->model.blind_for == 299) seen->kept_299++;
		if(!walk->model.touching && walk->model.blind_for == 300) seen->dropped_300++;
	}
	if(before.touching && !walk->model.touching && !walk->model.blind)
	{
		const model_t *model = &walk->model;
		bool far = model->lifted_along >= 60, twice = model->lifted_across * 2 <= model->lifted_along;

		if(model->lifted_reach <= 20 && model->lifted_age == 600 && expected == TOUCH_TAP) seen->tap_600++;
		if(model->lifted_reach <= 20 && model->lifted_age == 601 && expected == TOUCH_NONE) seen->none_601++;
		if(model->lifted_reach == 20 && model->lifted_age <= 600 && expected == TOUCH_TAP) seen->tap_20++;
		if(model->lifted_reach == 21 && model->lifted_age <= 600 && expected == TOUCH_NONE) seen->none_21++;
		if(far && twice && model->lifted_age == 1000 && expected >= TOUCH_SWIPE_LEFT) seen->swipe_1000++;
		if(far && twice && model->lifted_age == 1001 && expected == TOUCH_NONE) seen->none_1001++;
		if(model->lifted_along == 60 && twice && model->lifted_age <= 1000 && expected >= TOUCH_SWIPE_LEFT) seen->swipe_60++;
		if(model->lifted_along == 59 && twice && model->lifted_age <= 1000 && expected == TOUCH_NONE) seen->none_59++;
		if(far && model->lifted_across * 2 == model->lifted_along && model->lifted_age <= 1000 && expected >= TOUCH_SWIPE_LEFT) seen->swipe_twice++;
		if(far && !twice && model->lifted_across * 2 <= model->lifted_along + 2 && model->lifted_age <= 1000 && expected == TOUCH_NONE) seen->none_twice++;
	}
}

// A reading that failed, with whatever the controller left in its answer
static void feed_failed(walk_t *walk)
{
	bool down = walk_random(walk, 2) != 0;
	int x = walk_random(walk, 4) == 0 ? ONE_OF(walk, walk_outside) : (int)walk_random(walk, 480);
	int y = walk_random(walk, 4) == 0 ? ONE_OF(walk, walk_outside) : (int)walk_random(walk, 480);

	feed(walk, false, down, x, y);
}

// A reading that succeeded and saw no finger, with whatever point the controller left in its answer
static void feed_no_finger(walk_t *walk)
{
	int x = walk_random(walk, 3) == 0 ? ONE_OF(walk, walk_outside) : (int)walk_random(walk, 480);
	int y = walk_random(walk, 3) == 0 ? ONE_OF(walk, walk_outside) : (int)walk_random(walk, 480);

	feed(walk, true, false, x, y);
}

// A reading that succeeded with a finger at a point that is not on the screen
static void feed_outside(walk_t *walk)
{
	int x = (int)walk_random(walk, 480), y = (int)walk_random(walk, 480);
	uint32_t which = walk_random(walk, 3);

	if(which != 1) x = ONE_OF(walk, walk_outside);
	if(which != 0) y = ONE_OF(walk, walk_outside);
	feed(walk, true, true, x, y);
}

// The faults of a controller in the `ms` before a reading: none most of the time, else one reading that
// failed, a point outside the screen, one reading without a finger, or readings that fail for a while
static void walk_faults(walk_t *walk, uint64_t ms)
{
	static const int spans[] = {100, 298, 299, 300, 301, 302, 500};
	uint32_t fault = walk_random(walk, 24);

	if(fault > 3)
	{
		walk_pass(walk, ms);
		return;
	}
	walk_pass(walk, ms / 2);
	if(fault == 0) feed_failed(walk);
	if(fault == 1) feed_outside(walk);
	if(fault == 2) feed_no_finger(walk);
	if(fault == 3)
	{
		uint64_t span = (uint64_t)ONE_OF(walk, spans);
		uint32_t more = 1 + walk_random(walk, 3), i;

		if(walk_random(walk, 2) == 0) feed_failed(walk);
		else feed_outside(walk);
		for(i = 0; i < more; i++)
		{
			walk_pass(walk, span / more + (i == 0 ? span % more : 0));
			if(walk_random(walk, 3) == 0) feed_outside(walk);
			else feed_failed(walk);
		}
	}
	walk_pass(walk, ms - ms / 2);
}

// One finger going down, moving and going up, as the controller reads it
static void walk_touch(walk_t *walk)
{
	static const int places[] = {0, 1, 20, 21, 22, 60, 61, 239, 240, 418, 419, 457, 458, 459, 478, 479};
	static const int jitter[] = {-22, -21, -20, -19, -3, 0, 0, 0, 2, 19, 20, 21, 22};
	static const int ways[] = {58, 59, 60, 61, 62, 90, 100, 101, 120, 200};
	static const int lasting[] = {0, 1, 59, 60, 90, 150, 400, 599, 600, 601, 602, 700, 999, 1000, 1001, 1002, 1500, 4000};
	static const int toward[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
	int xs[6], ys[6];
	uint32_t kind = walk_random(walk, 10), readings, i;
	int count = 1 + (int)walk_random(walk, 5);
	static const uint64_t long_lasting[] = {59000, 61000, 3601000, UINT64_C(4294967000), UINT64_C(4294968000), UINT64_C(5000000000)};
	uint64_t lasts = (uint64_t)ONE_OF(walk, lasting);

	xs[0] = walk_random(walk, 3) == 0 ? ONE_OF(walk, places) : (int)walk_random(walk, 480);
	ys[0] = walk_random(walk, 3) == 0 ? ONE_OF(walk, places) : (int)walk_random(walk, 480);
	// A finger that rests for minutes, hours and longer than 32 bits of milliseconds hold, in every third walk
	if(walk->seed % 3 == 0 && walk_random(walk, 10) == 0) lasts = ONE_OF(walk, long_lasting);

	if(kind >= 3 && kind <= 6)
	{
		// A swipe from the middle of the screen, so that every way fits: around the 60 pixels and around twice
		// the other axis
		const int *direction = toward[walk_random(walk, 4)];
		int along = ONE_OF(walk, ways), across;

		switch(walk_random(walk, 8))
		{
			case 0:  across = along / 2 - 1; break;
			case 1:  across = along / 2; break;
			case 2:  across = along / 2 + 1; break;
			case 3:  across = along; break;
			case 4:  across = 29 + (int)walk_random(walk, 3); break;
			default: across = (int)walk_random(walk, 12); break;
		}
		if(walk_random(walk, 2) == 0) across = -across;
		if(count < 2) count = 2;
		xs[0] = 200 + (int)walk_random(walk, 80);
		ys[0] = 200 + (int)walk_random(walk, 80);
		for(i = 1; i < (uint32_t)count; i++)
		{
			xs[i] = (int)walk_random(walk, 480);
			ys[i] = (int)walk_random(walk, 480);
		}
		xs[count - 1] = xs[0] + along * direction[0] + across * direction[1];
		ys[count - 1] = ys[0] + along * direction[1] + across * direction[0];
	}
	else
	{
		for(i = 1; i < (uint32_t)count; i++)
		{
			// A tap with a finger that trembles around the 20 pixels (it may leave the screen at an edge), a
			// finger that goes away and comes back, or one that wanders
			bool close_by = kind < 3 || (kind == 7 && i == (uint32_t)count - 1);

			xs[i] = close_by ? xs[0] + ONE_OF(walk, jitter) : (int)walk_random(walk, 480);
			ys[i] = close_by ? ys[0] + ONE_OF(walk, jitter) : (int)walk_random(walk, 480);
		}
	}

	// The finger is read `count` times, then twice not; the last of these readings comes `lasts` ms after
	// the first unless readings fail for a while in between
	readings = (uint32_t)count + 1;
	feed(walk, true, true, xs[0], ys[0]);
	for(i = 1; i <= readings; i++)
	{
		walk_faults(walk, lasts / readings + (i == readings ? lasts % readings : 0));
		if(i < (uint32_t)count) feed(walk, true, true, xs[i], ys[i]);
		else feed_no_finger(walk);
	}
}

// Readings in no order at all: fingers near and far from the one before, no finger, failed readings, points
// outside the screen, at times around every limit
static void walk_noise(walk_t *walk)
{
	static const int steps[] = {0, 1, 29, 30, 30, 30, 31, 100, 150, 299, 300, 301, 599, 600, 601, 1000, 1001};
	static const int moves[] = {-200, -61, -60, -59, -30, -21, -20, 0, 0, 0, 20, 21, 30, 59, 60, 61, 200};
	uint32_t count = 5 + walk_random(walk, 30), i;

	for(i = 0; i < count; i++)
	{
		walk_pass(walk, (uint64_t)ONE_OF(walk, steps));
		switch(walk_random(walk, 10))
		{
			case 0:
			case 1:
				feed_failed(walk);
				break;
			case 2:
				feed_outside(walk);
				break;
			case 3:
			case 4:
			case 5:
				feed_no_finger(walk);
				break;
			default:
				walk->last_x += ONE_OF(walk, moves);
				walk->last_y += ONE_OF(walk, moves);
				if(walk->last_x < 0 || walk->last_x > 479) walk->last_x = (int)walk_random(walk, 480);
				if(walk->last_y < 0 || walk->last_y > 479) walk->last_y = (int)walk_random(walk, 480);
				feed(walk, true, true, walk->last_x, walk->last_y);
				break;
		}
	}
}

#define WALKS           60
#define WALK_SCENES     6000

static void walk_run(walk_t *walk, uint32_t seed, seen_t *seen)
{
	static const uint64_t pauses[] = {0, 1, 30, 30, 30, 60, 100, 299, 300, 301, 1000, 5000};
	int scene;

	memset(walk, 0, sizeof(*walk));
	walk->seed = seed;
	walk->random = seed * 2654435761u;
	walk->seen = seen;
	walk->last_x = walk->last_y = 240;
	// One walk begins at the time 0, one so close to the largest time that it wraps, the others anywhere
	walk->now = (uint64_t)walk_random(walk, 1u << 20) << 20;
	walk->now += walk_random(walk, 1u << 20);
	if(seed == 1) walk->now = 0;
	if(seed == 2) walk->now = UINT64_MAX - 4000000;

	// Whatever stood in the memory before
	walk->touch.down = walk->touch.moved = walk->touch.failing = true;
	walk->touch.start_x = walk->touch.last_x = -5;
	walk->touch.start_y = walk->touch.last_y = 600;
	walk->touch.lifted = 1;
	walk->touch.start_ms = walk->touch.failing_since_ms = walk->touch.clock_ms = UINT64_MAX;
	walk_restart(walk);

	for(scene = 0; scene < WALK_SCENES; scene++)
	{
		uint32_t what = walk_random(walk, 100);

		if(what < 75) walk_touch(walk);
		else if(what < 97) walk_noise(walk);
		else if(what < 98) walk_restart(walk);
		else
		{
			// The time steps back and stays there
			uint64_t back = 1 + walk_random(walk, 3000);

			walk->now = walk->now > back ? walk->now - back : 0;
		}
		walk_pass(walk, ONE_OF(walk, pauses));
		if(walk_random(walk, 3) == 0) feed_no_finger(walk);
		if(walk_random(walk, 12) == 0) feed_failed(walk);
	}
}

enum
{
	WALK_DIFFERENT, WALK_TWICE, WALK_WHILE_DOWN, WALK_WITHOUT_FINGER, WALK_OFF_SCREEN, WALK_THIN, WALK_RESULTS
};

static void walks(long *results)
{
	static walk_t walk;
	static seen_t seen;
	uint32_t seed;
	bool thin;
	int i;

	for(seed = 1; seed <= WALKS; seed++)
	{
		walk_run(&walk, seed, &seen);
		results[WALK_DIFFERENT] += walk.different;
		results[WALK_TWICE] += walk.twice;
		results[WALK_WHILE_DOWN] += walk.while_down;
		results[WALK_WITHOUT_FINGER] += walk.without_finger;
		results[WALK_OFF_SCREEN] += walk.off_screen;
	}

	printf("  walks: %ld readings, %ld failed, %ld outside the screen, %ld with a time before the latest, %ld wraps of the time, "
	       "%ld without places, %ld restarts\n",
	       seen.calls, seen.failed, seen.outside, seen.stepped_back, seen.wrapped, seen.without_places, seen.restarts);
	printf("  walks: %ld taps, %ld left, %ld right, %ld up, %ld down, %ld single readings without a finger bridged, %ld touches dropped, "
	       "%ld readings of a finger resting for a minute or more\n",
	       seen.events[TOUCH_TAP], seen.events[TOUCH_SWIPE_LEFT], seen.events[TOUCH_SWIPE_RIGHT], seen.events[TOUCH_SWIPE_UP],
	       seen.events[TOUCH_SWIPE_DOWN], seen.bridged, seen.dropped, seen.resting);
	printf("  walks at the limits: tap after 600 ms %ld, nothing after 601 ms %ld, tap at 20 pixels %ld, nothing at 21 pixels %ld,\n"
	       "    swipe after 1000 ms %ld, nothing after 1001 ms %ld, swipe at 60 pixels %ld, nothing at 59 pixels %ld,\n"
	       "    swipe at twice the other axis %ld, nothing just below twice %ld, kept after 299 ms of failing %ld, dropped after 300 ms %ld\n",
	       seen.tap_600, seen.none_601, seen.tap_20, seen.none_21, seen.swipe_1000, seen.none_1001, seen.swipe_60, seen.none_59,
	       seen.swipe_twice, seen.none_twice, seen.kept_299, seen.dropped_300);

	thin = seen.calls < 2000000 || seen.failed < 200000 || seen.outside < 100000 || seen.stepped_back < 40000 || seen.wrapped != 1 ||
	       seen.without_places < 200000 || seen.restarts < 1500 || seen.bridged < 60000 || seen.dropped < 25000 || seen.resting < 4000;
	for(i = TOUCH_TAP; i <= TOUCH_SWIPE_DOWN; i++)
	{
		if(seen.events[i] < 8000) thin = true;
	}
	if(seen.tap_600 < 1000 || seen.none_601 < 1000 || seen.tap_20 < 1000 || seen.none_21 < 1000 || seen.swipe_1000 < 1000 || seen.none_1001 < 1000 ||
	   seen.swipe_60 < 1000 || seen.none_59 < 1000 || seen.swipe_twice < 1000 || seen.none_twice < 1000 || seen.kept_299 < 1000 || seen.dropped_300 < 1000)
	{
		thin = true;
	}
	results[WALK_THIN] = thin;
}

// Touches as a controller reads them and readings in no order, with fixed seeds, against the model
static void test_walks(void)
{
	long results[WALK_RESULTS] = {0, 0, 0, 0, 0, 0};
	bool complete = in_child(walks, results, WALK_RESULTS);

	check(complete, "60 walks of 6000 scenes each: no crash and no hang");
	check(complete && results[WALK_THIN] == 0,
	      "the walks reach every gesture, failed readings, points outside, fingers resting long, steps back and one wrap of the time and every limit from both sides in numbers");
	check(complete && results[WALK_DIFFERENT] == 0, "in the walks every reading reports the gesture, the tap point and the touch going on that the model reports");
	check(complete && results[WALK_TWICE] == 0, "in the walks no touch reports more than one gesture");
	check(complete && results[WALK_WHILE_DOWN] == 0, "in the walks no gesture is reported by a reading with a finger or a failed one, nor while the touch goes on");
	check(complete && results[WALK_WITHOUT_FINGER] == 0, "in the walks no gesture is reported without a finger having been read since the one before");
	check(complete && results[WALK_OFF_SCREEN] == 0, "in the walks every tap point is on the screen");
}

int main(void)
{
	// First what runs in a child process: a module that crashes fails a check there before it ends the test
	test_walks();
	test_null_places();

	test_constants();
	test_init();
	test_tap();
	test_tap_time();
	test_tap_distance();
	test_rest();
	test_swipe();
	test_swipe_factor();
	test_swipe_time();
	test_swipe_path();
	test_lift();
	test_failed();
	test_lost();
	test_after_lost();
	test_outside();
	test_first_reading();
	test_clock();
	test_clock_limits();
	test_is_down();
	test_sequence();
	return test_end();
}
