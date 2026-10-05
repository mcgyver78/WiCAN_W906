/*
 * Host test for display/components/core/guard.c. Run "make test_guard && ./test_guard" in display/test.
 * redproof.py removes or weakens every rule once (mutations/guard.py) and expects this test to fail.
 *
 * The check words below are worked out by hand from the formula of the header, magic ^ crashes ^
 * layout_fresh ^ 0xFFFFFFFF with the magic 0x57444731: a counter of 0 to 4 without the layout mark gives
 * A8BBB8CE, ..CF, ..CC, ..CD, ..CA; with the mark the magic cancels itself and the check is the counter
 * with every bit turned, FFFFFFFF, ..FE, ..FD, ..FC.
 */
#include <limits.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include "test.h"
#include "guard.h"

#define M   0x57444731u

static guard_memory_t memory;

static void memory_set(uint32_t magic, uint32_t crashes, uint32_t layout_fresh, uint32_t check_word)
{
	memory.magic = magic;
	memory.crashes = crashes;
	memory.layout_fresh = layout_fresh;
	memory.check = check_word;
}

static bool memory_is(uint32_t magic, uint32_t crashes, uint32_t layout_fresh, uint32_t check_word)
{
	return memory.magic == magic && memory.crashes == crashes && memory.layout_fresh == layout_fresh && memory.check == check_word;
}

// One start. Returns whether it decided as expected.
static bool start(guard_reset_t reset, bool knob_held, bool safe_mode, bool previous_layout)
{
	guard_start_t decided = guard_start(&memory, reset, knob_held);

	return decided.safe_mode == safe_mode && decided.previous_layout == previous_layout;
}

// The display as it leaves the factory: power comes on, the memory holds whatever it held
static void power_on(void)
{
	memory_set(0x5A5A5A5Au, 0x5A5A5A5Au, 0x5A5A5A5Au, 0x5A5A5A5Au);
	guard_start(&memory, GUARD_RESET_POWER_ON, false);
}

static void test_constants(void)
{
	check(GUARD_MAGIC == 0x57444731u && GUARD_CRASHES == 3 && GUARD_ALIVE_MS == 60000, "the magic is WDG1, three crashes in a row, 60000 ms until a start counts as alive");
	check(GUARD_TEMP_DIM_C == 75 && GUARD_TEMP_OFF_C == 85 && GUARD_TEMP_BACK_C == 5 && GUARD_DIM_PERCENT == 30, "dimmed to 30 percent from 75 degrees, off from 85, back 5 degrees below");
	check(GUARD_CATALOG_REST_MS == 30000, "the check sum of the catalogue has to stand for 30000 ms");
}

static void test_three_crashes(void)
{
	memory_set(0x5A5A5A5Au, 0x5A5A5A5Au, 0x5A5A5A5Au, 0x5A5A5A5Au);
	check(start(GUARD_RESET_POWER_ON, false, false, false), "power on with whatever the memory held: no safe mode, the layout that is stored");
	check(memory_is(M, 0, 0, 0xA8BBB8CEu), "power on leaves the memory valid: the magic, counter 0, no layout mark, the check");
	check(start(GUARD_RESET_CRASH, false, false, false) && memory_is(M, 1, 0, 0xA8BBB8CFu), "the first crash: counter 1, no safe mode");
	check(start(GUARD_RESET_CRASH, false, false, false) && memory_is(M, 2, 0, 0xA8BBB8CCu), "the second crash in a row: counter 2, no safe mode");
	check(start(GUARD_RESET_CRASH, false, true, false) && memory_is(M, 3, 0, 0xA8BBB8CDu), "the third crash in a row: counter 3, safe mode");
	check(start(GUARD_RESET_CRASH, false, true, false) && memory_is(M, 4, 0, 0xA8BBB8CAu), "a crash in the safe mode: counter 4, safe mode again");
	check(start(GUARD_RESET_SOFTWARE, false, true, false) && memory_is(M, 4, 0, 0xA8BBB8CAu), "a restart by software out of the safe mode: the counter stays, safe mode again");
	guard_alive(&memory);
	check(memory_is(M, 0, 0, 0xA8BBB8CEu), "alive in the safe mode: the counter goes back to 0");
	check(start(GUARD_RESET_SOFTWARE, false, false, false) && memory_is(M, 0, 0, 0xA8BBB8CEu), "the restart after the safe mode was alive: no safe mode any more");
}

static void test_alive_between(void)
{
	power_on();
	guard_start(&memory, GUARD_RESET_CRASH, false);
	guard_start(&memory, GUARD_RESET_CRASH, false);
	guard_alive(&memory);
	check(memory_is(M, 0, 0, 0xA8BBB8CEu), "alive after two crashes: the counter goes back to 0");
	check(start(GUARD_RESET_CRASH, false, false, false) && memory_is(M, 1, 0, 0xA8BBB8CFu), "crash, crash, alive, crash: counter 1, no safe mode");
	check(start(GUARD_RESET_CRASH, false, false, false) && memory_is(M, 2, 0, 0xA8BBB8CCu), "a crash after the first one behind alive: counter 2, no safe mode");
	check(start(GUARD_RESET_CRASH, false, true, false), "the third crash in a row behind alive: safe mode");

	power_on();
	guard_start(&memory, GUARD_RESET_CRASH, false);
	guard_alive(&memory);
	guard_start(&memory, GUARD_RESET_CRASH, false);
	guard_alive(&memory);
	guard_start(&memory, GUARD_RESET_CRASH, false);
	guard_alive(&memory);
	check(start(GUARD_RESET_CRASH, false, false, false) && memory_is(M, 1, 0, 0xA8BBB8CFu), "four crashes, each start alive before the next: counter 1, no safe mode");
}

static void test_restart_between(void)
{
	power_on();
	guard_start(&memory, GUARD_RESET_CRASH, false);
	guard_start(&memory, GUARD_RESET_CRASH, false);
	check(start(GUARD_RESET_SOFTWARE, false, false, false) && memory_is(M, 2, 0, 0xA8BBB8CCu), "a restart by software after two crashes: the counter stays at 2, no safe mode");
	check(start(GUARD_RESET_CRASH, false, true, false) && memory_is(M, 3, 0, 0xA8BBB8CDu), "crash, crash, restart by software, crash: counter 3, safe mode");

	power_on();
	guard_start(&memory, GUARD_RESET_CRASH, false);
	guard_start(&memory, GUARD_RESET_SOFTWARE, false);
	guard_start(&memory, GUARD_RESET_CRASH, false);
	guard_start(&memory, GUARD_RESET_SOFTWARE, false);
	check(start(GUARD_RESET_CRASH, false, true, false), "three crashes with a restart by software between each: safe mode");

	power_on();
	check(start(GUARD_RESET_SOFTWARE, false, false, false) && start(GUARD_RESET_SOFTWARE, false, false, false) && start(GUARD_RESET_SOFTWARE, false, false, false) &&
	      start(GUARD_RESET_SOFTWARE, false, false, false) && memory_is(M, 0, 0, 0xA8BBB8CEu), "four restarts by software in a row: counter 0, no safe mode");

	power_on();
	guard_start(&memory, GUARD_RESET_CRASH, false);
	guard_start(&memory, GUARD_RESET_CRASH, false);
	check(start(GUARD_RESET_POWER_ON, false, false, false) && memory_is(M, 0, 0, 0xA8BBB8CEu), "power on after two crashes: the counter starts at 0");
	check(start(GUARD_RESET_CRASH, false, false, false) && memory_is(M, 1, 0, 0xA8BBB8CFu), "crash, crash, power on, crash: counter 1, no safe mode");

	memory_set(M, 3, 0, 0xA8BBB8CDu);
	check(start(GUARD_RESET_POWER_ON, false, false, false) && memory_is(M, 0, 0, 0xA8BBB8CEu), "power on with a valid counter of 3: the memory does not count, no safe mode");
}

static void test_knob_held(void)
{
	memory_set(0x5A5A5A5Au, 0x5A5A5A5Au, 0x5A5A5A5Au, 0x5A5A5A5Au);
	check(start(GUARD_RESET_POWER_ON, true, true, false) && memory_is(M, 0, 0, 0xA8BBB8CEu), "power on with the knob held: safe mode, the counter is 0");
	check(start(GUARD_RESET_SOFTWARE, false, false, false), "the restart after a safe mode by the knob: no safe mode");

	power_on();
	check(start(GUARD_RESET_SOFTWARE, true, true, false) && memory_is(M, 0, 0, 0xA8BBB8CEu), "a restart by software with the knob held: safe mode, the counter stays 0");
	check(start(GUARD_RESET_CRASH, true, true, false) && memory_is(M, 1, 0, 0xA8BBB8CFu), "a crash with the knob held: safe mode, the counter counts the crash and nothing else");
	check(start(GUARD_RESET_CRASH, false, false, false) && memory_is(M, 2, 0, 0xA8BBB8CCu), "the second crash, without the knob: the safe mode by the knob was no crash, no safe mode");
	check(start(GUARD_RESET_CRASH, true, true, false) && memory_is(M, 3, 0, 0xA8BBB8CDu), "the third crash with the knob held: safe mode for both reasons");
}

static void test_layout_mark(void)
{
	power_on();
	guard_layout_stored(&memory);
	check(memory_is(M, 0, M, 0xFFFFFFFFu), "a layout was stored: the mark is the magic, the counter stays 0, the memory is valid");
	check(start(GUARD_RESET_CRASH, false, false, true), "a crash after a layout was stored: the start uses the layout before it");
	check(memory_is(M, 1, 0, 0xA8BBB8CFu), "the start that uses the previous layout takes the mark away and counts the crash");
	check(start(GUARD_RESET_CRASH, false, false, false) && memory_is(M, 2, 0, 0xA8BBB8CCu), "the crash after that: the layout that is stored then, the previous one was used exactly once");

	power_on();
	guard_layout_stored(&memory);
	guard_alive(&memory);
	check(memory_is(M, 0, 0, 0xA8BBB8CEu), "alive after a layout was stored: the mark goes back to 0");
	check(start(GUARD_RESET_CRASH, false, false, false), "layout stored, alive, crash: the stored layout is used");

	power_on();
	guard_layout_stored(&memory);
	check(start(GUARD_RESET_SOFTWARE, false, false, false), "a restart by software after a layout was stored: the stored layout is used");
	check(memory_is(M, 0, M, 0xFFFFFFFFu), "a restart by software leaves the layout mark");
	check(start(GUARD_RESET_CRASH, false, false, true) && memory_is(M, 1, 0, 0xA8BBB8CFu), "layout stored, restart by software, crash before alive: the layout before it");

	power_on();
	guard_layout_stored(&memory);
	check(start(GUARD_RESET_POWER_ON, false, false, false) && memory_is(M, 0, 0, 0xA8BBB8CEu), "power on after a layout was stored: no mark");
	check(start(GUARD_RESET_CRASH, false, false, false), "layout stored, power on, crash: the stored layout is used");

	power_on();
	guard_start(&memory, GUARD_RESET_CRASH, false);
	guard_start(&memory, GUARD_RESET_CRASH, false);
	guard_layout_stored(&memory);
	check(memory_is(M, 2, M, 0xFFFFFFFDu), "a layout stored after two crashes: the counter stays 2");
	check(start(GUARD_RESET_CRASH, false, true, true) && memory_is(M, 3, 0, 0xA8BBB8CDu), "the third crash right after a layout was stored: safe mode and the layout before it");

	power_on();
	guard_layout_stored(&memory);
	guard_layout_stored(&memory);
	check(memory_is(M, 0, M, 0xFFFFFFFFu), "two layouts stored one after the other: one mark");
	check(start(GUARD_RESET_CRASH, true, true, true), "a crash after a layout was stored, with the knob held: safe mode and the layout before it");

	power_on();
	check(start(GUARD_RESET_CRASH, false, false, false) && start(GUARD_RESET_CRASH, false, false, false), "crashes without a layout stored before: never the previous layout");
}

static void test_alive_and_stored(void)
{
	memory_set(M, 2, M, 0xFFFFFFFDu);
	guard_alive(&memory);
	check(memory_is(M, 0, 0, 0xA8BBB8CEu), "alive with a counter of 2 and the layout mark: both go back to 0, the memory is valid");

	memory_set(0x5A5A5A5Au, 0x5A5A5A5Au, 0x5A5A5A5Au, 0x5A5A5A5Au);
	guard_alive(&memory);
	check(memory_is(M, 0, 0, 0xA8BBB8CEu), "alive with a memory that was damaged meanwhile: counter 0, no mark, valid");

	memory_set(M, 0xFFFFFFFFu, 0, 0x57444731u);
	guard_alive(&memory);
	check(memory_is(M, 0, 0, 0xA8BBB8CEu), "alive with the largest counter: back to 0");

	memory_set(M, 2, 0, 0xA8BBB8CCu);
	guard_layout_stored(&memory);
	check(memory_is(M, 2, M, 0xFFFFFFFDu), "a layout stored with a valid counter of 2: the counter stays, the mark is set");

	memory_set(M, 0xFFFFFFFFu, 0, 0x57444731u);
	guard_layout_stored(&memory);
	check(memory_is(M, 0xFFFFFFFFu, M, 0), "a layout stored with the largest counter: the counter stays");

	memory_set(M, 5, 0, 0);
	guard_layout_stored(&memory);
	check(memory_is(M, 0, M, 0xFFFFFFFFu), "a layout stored with a wrong check in the memory: the damaged counter counts as 0, the mark is set");

	memory_set(0x57444732u, 3, 0, 0xA8BBB8CEu);
	guard_layout_stored(&memory);
	check(memory_is(M, 0, M, 0xFFFFFFFFu), "a layout stored with another magic in the memory: counter 0, the mark is set, the magic is ours");

	memory_set(0x5A5A5A5Au, 0x5A5A5A5Au, 0x5A5A5A5Au, 0x5A5A5A5Au);
	guard_layout_stored(&memory);
	check(start(GUARD_RESET_CRASH, false, false, true) && memory_is(M, 1, 0, 0xA8BBB8CFu), "a layout stored into a damaged memory, then a crash: the layout before it, counter 1");
}

typedef struct
{
	uint32_t crashes;
	bool mark, safe_mode, previous_layout;
} outcome_t;

typedef struct
{
	const char *name;
	uint32_t magic, crashes, layout_fresh, check_word;  // the memory before the start
	outcome_t software, crash;                          // after a restart by software, after a crash
} start_case_t;

// What no counter of this firmware comes to: a restart by software begins at 0, a crash is the first
#define NOTHING     {0, false, false, false}, {1, false, false, false}

/*
 * Every content of the memory with what a start makes of it, written down by hand from the rules of the
 * header. After power on every one of them is counter 0, no mark, no safe mode, the stored layout.
 */
static const start_case_t start_cases[] = {
	// a valid counter, no layout mark
	{"a valid counter of 0", M, 0, 0, 0xA8BBB8CEu, {0, false, false, false}, {1, false, false, false}},
	{"a valid counter of 1", M, 1, 0, 0xA8BBB8CFu, {1, false, false, false}, {2, false, false, false}},
	{"a valid counter of 2, one below the limit", M, 2, 0, 0xA8BBB8CCu, {2, false, false, false}, {3, false, true, false}},
	{"a valid counter of 3, at the limit", M, 3, 0, 0xA8BBB8CDu, {3, false, true, false}, {4, false, true, false}},
	{"a valid counter of 4", M, 4, 0, 0xA8BBB8CAu, {4, false, true, false}, {5, false, true, false}},
	{"a valid counter of 255", M, 255, 0, 0xA8BBB831u, {255, false, true, false}, {256, false, true, false}},
	{"a valid counter of 256", M, 256, 0, 0xA8BBB9CEu, {256, false, true, false}, {257, false, true, false}},
	{"a valid counter of 65535", M, 65535, 0, 0xA8BB4731u, {65535, false, true, false}, {65536, false, true, false}},
	{"a valid counter of 65536", M, 65536, 0, 0xA8BAB8CEu, {65536, false, true, false}, {65537, false, true, false}},
	{"a valid counter with every bit but the highest set", M, 0x7FFFFFFFu, 0, 0xD7444731u, {0x7FFFFFFFu, false, true, false}, {0x80000000u, false, true, false}},
	{"a valid counter with only the highest bit set", M, 0x80000000u, 0, 0x28BBB8CEu, {0x80000000u, false, true, false}, {0x80000001u, false, true, false}},
	{"a valid counter one below the largest", M, 0xFFFFFFFEu, 0, 0x57444730u, {0xFFFFFFFEu, false, true, false}, {0xFFFFFFFFu, false, true, false}},
	{"a valid counter at the largest value", M, 0xFFFFFFFFu, 0, 0x57444731u, {0xFFFFFFFFu, false, true, false}, {0xFFFFFFFFu, false, true, false}},
	// a valid counter with the layout mark
	{"counter 0 and the layout mark", M, 0, M, 0xFFFFFFFFu, {0, true, false, false}, {1, false, false, true}},
	{"counter 1 and the layout mark", M, 1, M, 0xFFFFFFFEu, {1, true, false, false}, {2, false, false, true}},
	{"counter 2 and the layout mark", M, 2, M, 0xFFFFFFFDu, {2, true, false, false}, {3, false, true, true}},
	{"counter 3 and the layout mark", M, 3, M, 0xFFFFFFFCu, {3, true, true, false}, {4, false, true, true}},
	{"the largest counter and the layout mark", M, 0xFFFFFFFFu, M, 0, {0xFFFFFFFFu, true, true, false}, {0xFFFFFFFFu, false, true, true}},
	// valid, but the mark is not the magic: no mark
	{"counter 2 and a mark of 1", M, 2, 1, 0xA8BBB8CDu, {2, false, false, false}, {3, false, true, false}},
	{"counter 0 and a mark of all ones", M, 0, 0xFFFFFFFFu, 0x57444731u, {0, false, false, false}, {1, false, false, false}},
	{"counter 1 and a mark one above the magic", M, 1, 0x57444732u, 0xFFFFFFFDu, {1, false, false, false}, {2, false, false, false}},
	{"counter 1 and a mark that is the low half of the magic", M, 1, 0x00004731u, 0xA8BBFFFEu, {1, false, false, false}, {2, false, false, false}},
	{"counter 1 and a mark that is the high half of the magic", M, 1, 0x57440000u, 0xFFFFB8CFu, {1, false, false, false}, {2, false, false, false}},
	{"counter 1 and a mark with the high half of the magic turned", M, 1, 0xA8BB4731u, 0x0000FFFEu, {1, false, false, false}, {2, false, false, false}},
	// another magic, with the check that fits it
	{"magic 0 with a fitting check", 0, 2, 0, 0xFFFFFFFDu, NOTHING},
	{"the magic of a later firmware with a fitting check, the mark set", 0x57444732u, 3, M, 0xFFFFFFFFu, NOTHING},
	{"the magic with one bit turned and a fitting check", 0x57444730u, 5, 0, 0xA8BBB8CAu, NOTHING},
	{"every bit of the magic turned, with a fitting check", 0xA8BBB8CEu, 3, 0, 0x57444732u, NOTHING},
	{"the highest bit of the magic turned, with a fitting check", 0xD7444731u, 3, 0, 0x28BBB8CDu, NOTHING},
	// the magic, but a wrong check
	{"counter 3 with the check of counter 2", M, 3, 0, 0xA8BBB8CCu, NOTHING},
	{"counter 3 and the mark with a check of 0", M, 3, M, 0, NOTHING},
	{"counter 3 with a check that was not turned", M, 3, 0, 0x57444732u, NOTHING},
	{"counter 3 with the highest bit of the check turned", M, 3, 0, 0x28BBB8CDu, NOTHING},
	{"counter 0 and the mark with the check of no mark", M, 0, M, 0xA8BBB8CEu, NOTHING},
	{"a memory of zeros", 0, 0, 0, 0, NOTHING},
	{"a memory of ones", 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, NOTHING},
	// counter 3 with the mark, check FFFFFFFC, and one field damaged after it was written
	{"the counter damaged from 3 to 7", M, 7, M, 0xFFFFFFFCu, NOTHING},
	{"the counter damaged from 3 to 0", M, 0, M, 0xFFFFFFFCu, NOTHING},
	{"the mark damaged to 0", M, 3, 0, 0xFFFFFFFCu, NOTHING},
	{"one bit of the magic damaged", 0x57444721u, 3, M, 0xFFFFFFFCu, NOTHING},
	{"one bit of the check damaged", M, 3, M, 0xFFFFFFF8u, NOTHING},
	// the same bit turned in two fields: the check cannot see that, the content is taken as it is
	{"the same bit turned in counter and mark", M, 7, 0x57444735u, 0xFFFFFFFCu, {7, false, true, false}, {8, false, true, false}},
};

// Reasons that are no value of the enum count as a crash
static const int reset_values[] = {GUARD_RESET_POWER_ON, GUARD_RESET_SOFTWARE, GUARD_RESET_CRASH, 3, 4, 99, -1, INT_MAX, INT_MIN};

static void test_start_table(void)
{
	static const outcome_t after_power_on = {0, false, false, false};
	char text[200];
	size_t i, r;
	int held;

	for(i = 0; i < sizeof(start_cases) / sizeof(start_cases[0]); i++)
	{
		const start_case_t *given = &start_cases[i];
		int wrong = 0;

		for(r = 0; r < sizeof(reset_values) / sizeof(reset_values[0]); r++)
		{
			guard_reset_t reset = (guard_reset_t)reset_values[r];
			const outcome_t *expected = reset == GUARD_RESET_POWER_ON ? &after_power_on : reset == GUARD_RESET_SOFTWARE ? &given->software : &given->crash;
			uint32_t mark = expected->mark ? M : 0;

			for(held = 0; held <= 1; held++)
			{
				guard_start_t decided;

				memory_set(given->magic, given->crashes, given->layout_fresh, given->check_word);
				decided = guard_start(&memory, reset, held == 1);
				if(decided.safe_mode != (expected->safe_mode || held == 1) || decided.previous_layout != expected->previous_layout ||
				   !memory_is(M, expected->crashes, mark, M ^ expected->crashes ^ mark ^ 0xFFFFFFFFu))
				{
					printf("  %s, reset %d, knob %d: safe mode %d, previous layout %d, memory %08lX %08lX %08lX %08lX\n", given->name, reset_values[r], held,
					       decided.safe_mode, decided.previous_layout, (unsigned long)memory.magic, (unsigned long)memory.crashes,
					       (unsigned long)memory.layout_fresh, (unsigned long)memory.check);
					wrong++;
				}
			}
		}
		snprintf(text, sizeof(text), "start with %s: the decision and the memory after every reset reason, with and without the knob", given->name);
		check(wrong == 0, text);
	}
}

// Counter 1 with the layout mark, check FFFFFFFE, and a single bit turned somewhere
static void test_every_bit_of_the_memory(void)
{
	static const char *const texts[4] = {
		"a single bit of the magic turned, whichever: no counter, no mark",
		"a single bit of the counter turned, whichever: no counter, no mark",
		"a single bit of the layout mark turned, whichever: no counter, no mark",
		"a single bit of the check turned, whichever: no counter, no mark",
	};
	int wrong, word;
	uint32_t bit;

	for(word = 0; word < 4; word++)
	{
		wrong = 0;
		for(bit = 0; bit < 32; bit++)
		{
			uint32_t turned = 1u << bit;

			memory_set(M ^ (word == 0 ? turned : 0), 1 ^ (word == 1 ? turned : 0), M ^ (word == 2 ? turned : 0), 0xFFFFFFFEu ^ (word == 3 ? turned : 0));
			if(!start(GUARD_RESET_SOFTWARE, false, false, false) || !memory_is(M, 0, 0, 0xA8BBB8CEu)) wrong++;
			memory_set(M ^ (word == 0 ? turned : 0), 1 ^ (word == 1 ? turned : 0), M ^ (word == 2 ? turned : 0), 0xFFFFFFFEu ^ (word == 3 ? turned : 0));
			if(!start(GUARD_RESET_CRASH, false, false, false) || !memory_is(M, 1, 0, 0xA8BBB8CFu)) wrong++;
		}
		check(wrong == 0, texts[word]);
	}

	// The same with a check that fits the turned bit: it is the magic and the mark themselves that count
	wrong = 0;
	for(bit = 0; bit < 32; bit++)
	{
		uint32_t turned = 1u << bit;

		memory_set(M ^ turned, 1, M, 0xFFFFFFFEu ^ turned);
		if(!start(GUARD_RESET_CRASH, false, false, false) || !memory_is(M, 1, 0, 0xA8BBB8CFu)) wrong++;
	}
	check(wrong == 0, "a single bit of the magic turned and a check that fits it: no counter, no mark");

	wrong = 0;
	for(bit = 0; bit < 32; bit++)
	{
		uint32_t turned = 1u << bit;

		memory_set(M, 1, M ^ turned, 0xFFFFFFFEu ^ turned);
		if(!start(GUARD_RESET_CRASH, false, false, false) || !memory_is(M, 2, 0, 0xA8BBB8CCu)) wrong++;
	}
	check(wrong == 0, "a single bit of the mark turned and a check that fits it: the counter counts, but there is no mark");

	wrong = 0;
	for(bit = 2; bit < 32; bit++)
	{
		uint32_t turned = 1u << bit;

		memory_set(M, turned, 0, 0xA8BBB8CEu ^ turned);
		if(!start(GUARD_RESET_SOFTWARE, false, true, false) || !memory_is(M, turned, 0, 0xA8BBB8CEu ^ turned)) wrong++;
	}
	check(wrong == 0, "a valid counter with a single bit set, from 4 up to the highest: safe mode, the counter stays");
}

static void test_unknown_reset(void)
{
	memory_set(M, 2, M, 0xFFFFFFFDu);
	check(start((guard_reset_t)3, false, true, true) && memory_is(M, 3, 0, 0xA8BBB8CDu), "a reset reason behind the last of the enum counts as a crash: counted, safe mode, the previous layout");
	memory_set(M, 2, M, 0xFFFFFFFDu);
	check(start((guard_reset_t)-1, false, true, true) && memory_is(M, 3, 0, 0xA8BBB8CDu), "a reset reason of -1 counts as a crash");
}

static const char *heat_name(guard_heat_t heat)
{
	if(heat == GUARD_HEAT_NORMAL) return "N";
	if(heat == GUARD_HEAT_DIM) return "D";
	if(heat == GUARD_HEAT_OFF) return "O";
	return "?";
}

// The levels after each of the temperatures, given the level before, as a text of N, D and O
static const char *heat_row(guard_heat_t before, const int *temps, size_t count)
{
	static char row[64];
	size_t i;

	for(i = 0; i < count && i < sizeof(row) - 1; i++) row[i] = heat_name(guard_heat(before, temps[i], true))[0];
	row[i] = '\0';
	return row;
}

// The levels of a temperature that goes from `from` to `to` one degree at a time, each reading with the
// level of the one before
static const char *heat_run(guard_heat_t level, int from, int to)
{
	static char run[64];
	int step = from <= to ? 1 : -1;
	size_t i = 0;
	int temp;

	for(temp = from; i < sizeof(run) - 1; temp += step)
	{
		level = guard_heat(level, temp, true);
		run[i++] = heat_name(level)[0];
		if(temp == to) break;
	}
	run[i] = '\0';
	return run;
}

static void test_heat(void)
{
	static const int temps[23] = {INT_MIN, -1000, -150, -40, 0, 69, 70, 71, 74, 75, 76, 79, 80, 81, 84, 85, 86, 125, 200, 336, 1000, 65606, INT_MAX};
	static const int invalid[] = {3, 4, 100, -1, INT_MAX, INT_MIN};
	static const guard_heat_t levels[3] = {GUARD_HEAT_NORMAL, GUARD_HEAT_DIM, GUARD_HEAT_OFF};
	int degrees[36];
	bool wrong = false;
	size_t i, t;
	int temp;

	for(i = 0; i < 36; i++) degrees[i] = 60 + (int)i;

	check(guard_heat(GUARD_HEAT_NORMAL, 74, true) == GUARD_HEAT_NORMAL, "rising, 74 degrees: normal");
	check(guard_heat(GUARD_HEAT_NORMAL, 75, true) == GUARD_HEAT_DIM, "rising, 75 degrees: dimmed");
	check(guard_heat(GUARD_HEAT_NORMAL, 84, true) == GUARD_HEAT_DIM && guard_heat(GUARD_HEAT_DIM, 84, true) == GUARD_HEAT_DIM, "rising, 84 degrees: dimmed, not off");
	check(guard_heat(GUARD_HEAT_DIM, 85, true) == GUARD_HEAT_OFF, "rising, 85 degrees: off");
	check(guard_heat(GUARD_HEAT_NORMAL, 85, true) == GUARD_HEAT_OFF, "from normal to 85 degrees with one reading: off");
	check(guard_heat(GUARD_HEAT_OFF, 80, true) == GUARD_HEAT_OFF, "falling, 80 degrees: still off");
	check(guard_heat(GUARD_HEAT_OFF, 79, true) == GUARD_HEAT_DIM, "falling, 79 degrees: dimmed");
	check(guard_heat(GUARD_HEAT_DIM, 70, true) == GUARD_HEAT_DIM && guard_heat(GUARD_HEAT_OFF, 70, true) == GUARD_HEAT_DIM, "falling, 70 degrees: still dimmed");
	check(guard_heat(GUARD_HEAT_DIM, 69, true) == GUARD_HEAT_NORMAL, "falling, 69 degrees: normal");
	check(guard_heat(GUARD_HEAT_OFF, 69, true) == GUARD_HEAT_NORMAL, "from off to 69 degrees with one reading: normal");
	check(guard_heat(GUARD_HEAT_OFF, 74, true) == GUARD_HEAT_DIM, "from off to 74 degrees with one reading: dimmed, the limit that began at 75 is lifted below 70");
	check(guard_heat(GUARD_HEAT_NORMAL, 70, true) == GUARD_HEAT_NORMAL && guard_heat(GUARD_HEAT_NORMAL, 80, true) == GUARD_HEAT_DIM,
	      "the lower limits are those of the way down: from normal 70 degrees stay normal and 80 are dimmed, not off");

	check(strcmp(heat_row(GUARD_HEAT_NORMAL, temps, 23), "NNNNNNNNNDDDDDDOOOOOOOO") == 0, "from normal: normal up to 74 degrees, dimmed from 75 to 84, off from 85, at every temperature of the list");
	check(strcmp(heat_row(GUARD_HEAT_DIM, temps, 23), "NNNNNNDDDDDDDDDOOOOOOOO") == 0, "from dimmed: normal up to 69 degrees, dimmed from 70 to 84, off from 85, at every temperature of the list");
	check(strcmp(heat_row(GUARD_HEAT_OFF, temps, 23), "NNNNNNDDDDDDOOOOOOOOOOO") == 0, "from off: normal up to 69 degrees, dimmed from 70 to 79, off from 80, at every temperature of the list");

	check(strcmp(heat_row(GUARD_HEAT_NORMAL, degrees, 36), "NNNNNNNNNNNNNNNDDDDDDDDDDOOOOOOOOOOO") == 0, "from normal, every degree from 60 to 95 on its own: dimmed from 75, off from 85");
	check(strcmp(heat_row(GUARD_HEAT_DIM, degrees, 36), "NNNNNNNNNNDDDDDDDDDDDDDDDOOOOOOOOOOO") == 0, "from dimmed, every degree from 60 to 95 on its own: dimmed from 70, off from 85");
	check(strcmp(heat_row(GUARD_HEAT_OFF, degrees, 36), "NNNNNNNNNNDDDDDDDDDDOOOOOOOOOOOOOOOO") == 0, "from off, every degree from 60 to 95 on its own: dimmed from 70, off from 80");

	wrong = false;
	for(temp = -300; temp <= 300; temp++)
	{
		if(guard_heat(GUARD_HEAT_NORMAL, temp, true) != (temp < 75 ? GUARD_HEAT_NORMAL : temp < 85 ? GUARD_HEAT_DIM : GUARD_HEAT_OFF)) wrong = true;
		if(guard_heat(GUARD_HEAT_DIM, temp, true) != (temp < 70 ? GUARD_HEAT_NORMAL : temp < 85 ? GUARD_HEAT_DIM : GUARD_HEAT_OFF)) wrong = true;
		if(guard_heat(GUARD_HEAT_OFF, temp, true) != (temp < 70 ? GUARD_HEAT_NORMAL : temp < 80 ? GUARD_HEAT_DIM : GUARD_HEAT_OFF)) wrong = true;
	}
	check(!wrong, "every degree from -300 to 300: from normal the limits are 75 and 85, from dimmed 70 and 85, from off 70 and 80");

	check(strcmp(heat_run(GUARD_HEAT_NORMAL, 60, 95), "NNNNNNNNNNNNNNNDDDDDDDDDDOOOOOOOOOOO") == 0, "a temperature rising from 60 to 95 degrees: dimmed at 75, off at 85");
	check(strcmp(heat_run(GUARD_HEAT_OFF, 95, 60), "OOOOOOOOOOOOOOOODDDDDDDDDDNNNNNNNNNN") == 0, "a temperature falling from 95 to 60 degrees: dimmed at 79, normal at 69");
	check(strcmp(heat_run(GUARD_HEAT_NORMAL, 74, 76), "NDD") == 0 && strcmp(heat_run(GUARD_HEAT_DIM, 76, 70), "DDDDDDD") == 0,
	      "a temperature that wavers around 75 degrees dims once and stays dimmed down to 70");
	check(strcmp(heat_run(GUARD_HEAT_DIM, 84, 86), "DOO") == 0 && strcmp(heat_run(GUARD_HEAT_OFF, 86, 80), "OOOOOOO") == 0,
	      "a temperature that wavers around 85 degrees switches off once and stays off down to 80");

	for(t = 0; t < 23; t++)
	{
		if(guard_heat(GUARD_HEAT_NORMAL, temps[t], false) != GUARD_HEAT_NORMAL) wrong = true;
	}
	check(!wrong, "a failed reading keeps the level normal, whatever temperature comes with it");
	wrong = false;
	for(t = 0; t < 23; t++)
	{
		if(guard_heat(GUARD_HEAT_DIM, temps[t], false) != GUARD_HEAT_DIM) wrong = true;
	}
	check(!wrong, "a failed reading keeps the level dimmed, whatever temperature comes with it");
	wrong = false;
	for(t = 0; t < 23; t++)
	{
		if(guard_heat(GUARD_HEAT_OFF, temps[t], false) != GUARD_HEAT_OFF) wrong = true;
	}
	check(!wrong, "a failed reading keeps the level off, whatever temperature comes with it");
	wrong = false;
	for(i = 0; i < 3; i++)
	{
		for(temp = -300; temp <= 300; temp++)
		{
			if(guard_heat(levels[i], temp, false) != levels[i]) wrong = true;
		}
	}
	check(!wrong, "a failed reading keeps every level with every temperature from -300 to 300 degrees");

	wrong = false;
	for(i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
	{
		if(strcmp(heat_row((guard_heat_t)invalid[i], temps, 23), "NNNNNNDDDDDDOOOOOOOOOOO") != 0) wrong = true;
	}
	check(!wrong, "a level that is no value of the enum counts as off: off down to 80 degrees, dimmed down to 70");
	wrong = false;
	for(i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
	{
		for(t = 0; t < 23; t++)
		{
			if(guard_heat((guard_heat_t)invalid[i], temps[t], false) != GUARD_HEAT_OFF) wrong = true;
		}
	}
	check(!wrong, "a level that is no value of the enum and a failed reading: off");
}

static bool brightness_is(guard_heat_t heat, const int *expected)
{
	static const int wanted[11] = {INT_MIN, -1, 0, 1, 29, 30, 31, 99, 100, 101, INT_MAX};
	bool same = true;
	size_t i;

	for(i = 0; i < 11; i++)
	{
		if(guard_brightness(heat, wanted[i]) != expected[i]) same = false;
	}
	return same;
}

static void test_brightness(void)
{
	static const int normal[11] = {0, 0, 0, 1, 29, 30, 31, 99, 100, 100, 100};
	static const int dimmed[11] = {0, 0, 0, 1, 29, 30, 30, 30, 30, 30, 30};
	static const int off[11] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	int wrong_normal = 0, wrong_dimmed = 0, wrong_off = 0;
	int wanted;

	check(guard_brightness(GUARD_HEAT_NORMAL, 60) == 60, "normal: the brightness wanted");
	check(guard_brightness(GUARD_HEAT_NORMAL, 100) == 100 && guard_brightness(GUARD_HEAT_NORMAL, 101) == 100, "normal: 100 stays, 101 is clamped to 100");
	check(guard_brightness(GUARD_HEAT_NORMAL, 0) == 0 && guard_brightness(GUARD_HEAT_NORMAL, -1) == 0, "normal: 0 stays, -1 is clamped to 0");
	check(guard_brightness(GUARD_HEAT_DIM, 60) == 30, "dimmed: 60 wanted is limited to 30");
	check(guard_brightness(GUARD_HEAT_DIM, 29) == 29 && guard_brightness(GUARD_HEAT_DIM, 30) == 30 && guard_brightness(GUARD_HEAT_DIM, 31) == 30, "dimmed: 29 and 30 stay, 31 is limited to 30");
	check(guard_brightness(GUARD_HEAT_DIM, 10) == 10, "dimmed: a brightness below the limit is not raised");
	check(guard_brightness(GUARD_HEAT_OFF, 60) == 0 && guard_brightness(GUARD_HEAT_OFF, 1) == 0, "off: 0 whatever is wanted");
	check(brightness_is(GUARD_HEAT_NORMAL, normal), "normal: every brightness of the list clamped to 0..100");
	check(brightness_is(GUARD_HEAT_DIM, dimmed), "dimmed: every brightness of the list clamped to 0..30");
	check(brightness_is(GUARD_HEAT_OFF, off), "off: every brightness of the list is 0");
	check(brightness_is((guard_heat_t)3, off) && brightness_is((guard_heat_t)100, off) && brightness_is((guard_heat_t)-1, off) && brightness_is((guard_heat_t)INT_MIN, off),
	      "a level that is no value of the enum: every brightness of the list is 0");

	for(wanted = -300; wanted <= 300; wanted++)
	{
		if(guard_brightness(GUARD_HEAT_NORMAL, wanted) != (wanted < 0 ? 0 : wanted > 100 ? 100 : wanted)) wrong_normal++;
		if(guard_brightness(GUARD_HEAT_DIM, wanted) != (wanted < 0 ? 0 : wanted > 30 ? 30 : wanted)) wrong_dimmed++;
		if(guard_brightness(GUARD_HEAT_OFF, wanted) != 0 || guard_brightness((guard_heat_t)3, wanted) != 0) wrong_off++;
	}
	check(wrong_normal == 0, "normal: every brightness from 0 to 100 is itself, below it 0, above it 100");
	check(wrong_dimmed == 0, "dimmed: every brightness from 0 to 30 is itself, below it 0, above it 30");
	check(wrong_off == 0, "off or no level of the enum: 0 for every brightness from -300 to 300");
}

static guard_catalog_t catalog;

static bool due(uint32_t sum, bool complete, uint64_t now_ms)
{
	return guard_catalog_due(&catalog, sum, complete, now_ms);
}

// A round every second from `from` to `to`. Returns how often the catalogue was due.
static int rounds(uint32_t sum, bool complete, uint64_t from, uint64_t to)
{
	int times = 0;
	uint64_t now;

	for(now = from; now <= to; now += 1000)
	{
		if(due(sum, complete, now)) times++;
	}
	return times;
}

// The display started with a catalogue of check sum 0x1111 in the flash and connected
static void connected_with_1111(void)
{
	guard_catalog_init(&catalog, true, 0x1111);
	guard_catalog_connected(&catalog);
}

static void test_catalog_due(void)
{
	connected_with_1111();
	check(!due(0x2222, true, 1000), "a complete catalogue with another check sum, seen for the first time: not due");
	check(rounds(0x2222, true, 2000, 30000) == 0 && !due(0x2222, true, 30999), "the check sum stands for 29999 ms: not due");
	check(due(0x2222, true, 31000), "the check sum stands for 30000 ms: due");
	check(!due(0x2222, true, 31000) && !due(0x2222, true, 31001), "due is reported once: the call counts the catalogue as stored");
	check(rounds(0x2222, true, 32000, 200000) == 0, "the catalogue that was written is never due again on this connection");
	check(rounds(0x3333, true, 201000, 400000) == 0, "another check sum that stands on the same connection: no second write");

	connected_with_1111();
	check(rounds(0x1111, true, 1000, 200000) == 0, "the check sum of the stored catalogue is never due");

	guard_catalog_init(&catalog, false, 0x1111);
	guard_catalog_connected(&catalog);
	check(!due(0x1111, true, 1000) && !due(0x1111, true, 30999) && due(0x1111, true, 31000), "no catalogue stored: the check sum given for it is not looked at, due after 30000 ms");

	guard_catalog_init(&catalog, true, 0x1111);
	check(!due(0x2222, true, 1000) && !due(0x2222, true, 30999) && due(0x2222, true, 31000), "after the start, without a new connection reported: one write is allowed");
	check(rounds(0x3333, true, 32000, 200000) == 0, "after the start, without a new connection reported: no second write");

	guard_catalog_init(&catalog, false, 0x1111);
	check(!due(0x1111, true, 50000) && !due(0x1111, true, 79999) && due(0x1111, true, 80000),
	      "no catalogue stored, the first round 50 s after the start: the rest time begins with that round");

	guard_catalog_init(&catalog, true, 0);
	check(rounds(0, true, 1000, 100000) == 0, "a stored check sum of 0 and a catalogue with 0: never due");
	check(!due(0xFFFFFFFFu, true, 101000) && !due(0xFFFFFFFFu, true, 130999) && due(0xFFFFFFFFu, true, 131000), "the largest check sum against a stored 0: due after 30000 ms");

	guard_catalog_init(&catalog, false, 0);
	check(!due(0, true, 1000) && due(0, true, 31000), "no catalogue stored and a check sum of 0: due");
}

static void test_catalog_incomplete(void)
{
	connected_with_1111();
	check(rounds(0x2222, false, 1000, 400000) == 0, "an incomplete catalogue is never due, however long its check sum stands");
	check(due(0x2222, true, 400001), "the catalogue becomes complete and its check sum stood long enough: due at once");

	connected_with_1111();
	due(0x2222, true, 1000);
	check(!due(0x2222, false, 31000) && !due(0x2222, false, 40000), "complete at first, incomplete when the rest time is over: not due");
	check(due(0x2222, true, 41000), "complete again later: due then");

	connected_with_1111();
	due(0x2222, false, 1000);
	check(!due(0x2222, true, 30999) && due(0x2222, true, 31000), "the rest time counts from the first time the check sum was seen, also if the catalogue was incomplete then");
}

static void test_catalog_rest(void)
{
	bool was_due = false;
	uint64_t now;
	uint32_t sum;

	connected_with_1111();
	for(now = 1000; now <= 400000; now += 1000)
	{
		if(due(now / 1000 % 2 == 0 ? 0x2222 : 0x3333, true, now)) was_due = true;
	}
	check(!was_due, "a check sum that changes every second never comes to rest");

	// A new check sum every 29999 ms, asked for when it comes and one millisecond before the next
	connected_with_1111();
	was_due = false;
	for(sum = 1; sum <= 20; sum++)
	{
		now = 1000 + (uint64_t)sum * 29999;
		if(due(0x2000 + sum, true, now) || due(0x2000 + sum, true, now + 29998)) was_due = true;
	}
	check(!was_due, "a check sum that changes every 29999 ms never comes to rest");

	connected_with_1111();
	due(0x2222, true, 1000);
	due(0x2222, true, 20000);
	check(!due(0x3333, true, 21000) && !due(0x3333, true, 31000) && !due(0x3333, true, 50999), "the check sum changed after 20 s: the rest time starts anew, not due 29999 ms later");
	check(due(0x3333, true, 51000), "the check sum that changed is due 30000 ms after the change");

	connected_with_1111();
	due(0x2222, true, 1000);
	due(0x3333, true, 20000);
	check(!due(0x2222, true, 21000) && !due(0x2222, true, 31000) && !due(0x2222, true, 50999), "a check sum that comes back does not count the time it stood before");
	check(due(0x2222, true, 51000), "a check sum that came back is due 30000 ms after it came back");

	connected_with_1111();
	due(0x2222, true, 1000);
	due(0x1111, true, 20000);
	check(rounds(0x1111, true, 21000, 100000) == 0 && !due(0x2222, true, 101000) && !due(0x2222, true, 130999) && due(0x2222, true, 131000),
	      "back at the stored check sum for a while, then another one: due 30000 ms after that one came");
}

static void test_catalog_connected(void)
{
	connected_with_1111();
	due(0x2222, true, 1000);
	due(0x2222, true, 31000);
	guard_catalog_connected(&catalog);
	check(rounds(0x2222, true, 40000, 200000) == 0, "a new connection with the check sum written on the one before: it is the stored one now, never due");
	guard_catalog_connected(&catalog);
	check(!due(0x1111, true, 300000) && !due(0x1111, true, 329999) && due(0x1111, true, 330000), "the check sum that was stored at the start differs from the stored one now: due on a new connection");

	connected_with_1111();
	due(0x2222, true, 1000);
	due(0x2222, true, 31000);
	guard_catalog_connected(&catalog);
	check(!due(0x3333, true, 40000) && !due(0x3333, true, 69999) && due(0x3333, true, 70000), "a new connection allows one write again");
	check(rounds(0x4444, true, 71000, 200000) == 0, "a new connection allows one write, not two");

	connected_with_1111();
	due(0x2222, true, 1000);
	due(0x2222, true, 25000);
	guard_catalog_connected(&catalog);
	check(!due(0x2222, true, 26000) && !due(0x2222, true, 31000) && !due(0x2222, true, 55999), "a new connection starts the rest time anew: not due 30000 ms after the check sum was first seen");
	check(due(0x2222, true, 56000), "due 30000 ms after the check sum was first seen on the new connection");

	connected_with_1111();
	rounds(0x1111, true, 1000, 100000);
	guard_catalog_connected(&catalog);
	check(rounds(0x1111, true, 101000, 300000) == 0, "a new connection does not forget what is stored: the stored check sum is never due");

	guard_catalog_init(&catalog, false, 0);
	due(0x2222, true, 1000);
	due(0x2222, true, 31000);
	guard_catalog_connected(&catalog);
	check(rounds(0x2222, true, 40000, 200000) == 0, "none stored at the start, written once: on the next connection that check sum is the stored one");
}

static void test_catalog_clock(void)
{
	connected_with_1111();
	due(0x2222, true, 100000);
	check(!due(0x2222, true, 50000) && !due(0x2222, true, 0), "a time before the one the check sum was first seen at: no time passed, not due");
	check(!due(0x2222, true, 129999) && due(0x2222, true, 130000), "after the step back the rest time still counts from the first sighting");

	connected_with_1111();
	due(0x2222, true, 100000);
	check(!due(0x3333, true, 50000) && !due(0x3333, true, 79999) && due(0x3333, true, 80000), "a check sum first seen with a time that stepped back: due 30000 ms after that time");

	connected_with_1111();
	check(!due(0x2222, true, UINT64_MAX - 30000) && !due(0x2222, true, UINT64_MAX - 1) && due(0x2222, true, UINT64_MAX), "a check sum seen 30000 ms before the largest time: due at the largest time");

	connected_with_1111();
	check(!due(0x2222, true, UINT64_MAX - 29999) && !due(0x2222, true, UINT64_MAX), "a check sum seen 29999 ms before the largest time: not due at the largest time");
	check(!due(0x2222, true, 0) && !due(0x2222, true, 40000), "the time wraps to 0: a time before the first sighting, not due");

	connected_with_1111();
	check(!due(0x2222, true, 0) && !due(0x2222, true, 29999) && due(0x2222, true, 30000), "a check sum first seen at time 0: due at 30000");
}

static void test_catalog_every_bit(void)
{
	int wrong_stored = 0, wrong_seen = 0;
	uint32_t bit;

	for(bit = 0; bit < 32; bit++)
	{
		uint32_t other = 0x12345678u ^ (1u << bit);

		guard_catalog_init(&catalog, true, 0x12345678u);
		if(due(other, true, 1000) || due(other, true, 30999) || !due(other, true, 31000)) wrong_stored++;

		guard_catalog_init(&catalog, true, 0);
		due(0x12345678u, true, 1000);
		if(due(other, true, 20000) || due(other, true, 31000) || due(other, true, 49999) || !due(other, true, 50000)) wrong_seen++;
	}
	check(wrong_stored == 0, "a check sum that differs from the stored one in a single bit, whichever, is due after 30000 ms");
	check(wrong_seen == 0, "a check sum that changes in a single bit, whichever, starts the rest time anew");

	guard_catalog_init(&catalog, true, 0xDEADBEEFu);
	check(rounds(0xDEADBEEFu, true, 1000, 100000) == 0, "a stored check sum with its highest bits set is never due");
	check(!due(0xCAFEF00Du, true, 101000) && due(0xCAFEF00Du, true, 131000), "another check sum with its highest bits set is due after 30000 ms");
	guard_catalog_connected(&catalog);
	check(rounds(0xCAFEF00Du, true, 140000, 300000) == 0, "a check sum with its highest bits set that was written is the stored one on the next connection");
}

static void test_catalog_long_times(void)
{
	connected_with_1111();
	due(0x2222, true, 1000);
	check(due(0x2222, true, 1000 + 0x100000000ull + 5), "a check sum that stands for 49 days is at rest: due");

	connected_with_1111();
	due(0x2222, true, 1000);
	check(due(0x2222, true, 1000 + 0x1000000000000ull + 5), "a check sum that stands for thousands of years is at rest: due");

	connected_with_1111();
	due(0x2222, true, 0x100000000ull - 10000);
	check(!due(0x2222, true, 0x100000000ull + 19999) && due(0x2222, true, 0x100000000ull + 20000),
	      "a check sum first seen 10 s before the time needs 33 bits: due 20 s behind that point");

	connected_with_1111();
	due(0x2222, true, 0x100000000ull + 1000);
	check(!due(0x2222, true, 0x100000000ull + 30999) && due(0x2222, true, 0x100000000ull + 31000), "a check sum first seen at a time of 33 bits: due 30000 ms later");

	connected_with_1111();
	due(0x2222, true, 0x100000000ull + 50000);
	check(!due(0x2222, true, 60000) && !due(0x2222, true, 0x100000000ull + 79999) && due(0x2222, true, 0x100000000ull + 80000),
	      "a time of 32 bits after a first sighting at a time of 33 bits is a time before it: no time passed");
}

static void test_catalog_init(void)
{
	// Whatever stood in the memory: a check sum seen long ago and a write
	catalog.stored_sum = 0x2222;
	catalog.has_stored = true;
	catalog.seen_sum = 0x3333;
	catalog.seen_since_ms = 0;
	catalog.has_seen = true;
	catalog.written = true;
	guard_catalog_init(&catalog, true, 0x1111);
	check(!due(0x3333, true, 50000) && !due(0x3333, true, 79999), "after the start no check sum was seen, whatever stood in the memory: the rest time begins with the first round");
	check(due(0x3333, true, 80000), "after the start nothing was written, whatever stood in the memory: due");

	catalog.stored_sum = 0x2222;
	catalog.has_stored = true;
	catalog.has_seen = false;
	catalog.written = false;
	guard_catalog_init(&catalog, true, 0x1111);
	due(0x2222, true, 1000);
	check(due(0x2222, true, 31000), "after the start the stored check sum is the one given, whatever stood in the memory");

	catalog.stored_sum = 0x2222;
	catalog.has_stored = true;
	guard_catalog_init(&catalog, false, 0x2222);
	due(0x2222, true, 1000);
	check(due(0x2222, true, 31000), "started without a stored catalogue, whatever stood in the memory: the check sum is due");

	catalog.has_stored = false;
	guard_catalog_init(&catalog, true, 0x2222);
	check(rounds(0x2222, true, 1000, 100000) == 0, "started with a stored catalogue, whatever stood in the memory: its check sum is never due");
}

/*
 * The same rules a second time, in another shape, for the walks below.
 *
 * The memory: what the four words mean (is there a counter, its value, a mark) instead of the words; the
 * check as "all four words together have every bit set".
 */
typedef struct
{
	uint64_t count;     // crashes in a row; 0 if the memory holds no counter of this firmware
	bool mark;
} memory_model_t;

typedef struct
{
	long safe_by_counter, safe_by_knob, previous, at_largest, unknown_memory, software, power_on, strange_reset, alive, stored, written;
	long catalog_due, catalog_same, catalog_incomplete, catalog_second, catalog_too_early, catalog_connected, catalog_back;
} reach_t;

static reach_t reach;

static void model_read(memory_model_t *model, const uint32_t *words)
{
	bool known = words[0] == 0x57444731u && (words[0] ^ words[1] ^ words[2] ^ words[3]) == 0xFFFFFFFFu;

	model->count = known ? words[1] : 0;
	model->mark = known && words[2] == 0x57444731u;
	if(!known) reach.unknown_memory++;
}

static void model_words(const memory_model_t *model, uint32_t *words)
{
	words[0] = 0x57444731u;
	words[1] = (uint32_t)model->count;
	words[2] = model->mark ? 0x57444731u : 0;
	words[3] = ~(words[0] ^ words[1] ^ words[2]);
}

static void model_start(memory_model_t *model, int reset, bool knob_held, bool *safe_mode, bool *previous_layout)
{
	*previous_layout = false;
	if(reset == GUARD_RESET_POWER_ON)
	{
		model->count = 0;
		model->mark = false;
		reach.power_on++;
	}
	else if(reset == GUARD_RESET_SOFTWARE)
	{
		reach.software++;
	}
	else
	{
		if(reset != GUARD_RESET_CRASH) reach.strange_reset++;
		model->count++;
		if(model->count > 0xFFFFFFFFu)
		{
			model->count = 0xFFFFFFFFu;
			reach.at_largest++;
		}
		*previous_layout = model->mark;
		model->mark = false;
		if(*previous_layout) reach.previous++;
	}
	*safe_mode = knob_held || model->count > 2;
	if(model->count > 2) reach.safe_by_counter++;
	else if(knob_held) reach.safe_by_knob++;
}

static uint32_t random_state;

/*
 * Never two rolls in one expression whose order C leaves open, as on both sides of ^ or of an assignment:
 * which side is worked out first is the choice of the compiler, and a walk that depends on it would be
 * another walk with another one. Where an expression needs two, one is rolled before it, in the order gcc
 * and clang both had: operands from left to right, the right side of an assignment before the left one.
 */
static uint32_t random_next(void)
{
	random_state = random_state * 1664525u + 1013904223u;
	return random_state >> 8;
}

static uint32_t random_below(uint32_t limit)
{
	return random_next() % limit;
}

static uint32_t random_word(void)
{
	uint32_t high = random_next();

	return (high << 16) ^ random_next();
}

#define MEMORY_WALKS    40
#define MEMORY_CALLS    20000

// Starts for every reason, alive, layouts stored and a memory that is overwritten in every way, in a random
// order. Returns the number of the first call that differs from the model, -1 if none does.
static int memory_walk(uint32_t seed)
{
	static const uint32_t counters[] = {0, 1, 2, 3, 4, 255, 256, 65535, 65536, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFEu, 0xFFFFFFFFu};
	static const uint32_t marks[] = {0, 1, 0x00004731u, 0x57440000u, 0x57444730u, 0xD7444731u};
	static const int strange[] = {3, 4, 99, -1, INT_MAX, INT_MIN};
	struct
	{
		uint32_t before[2];
		guard_memory_t memory;
		uint32_t after[2];
	} room;
	memory_model_t model = {0, false};
	uint32_t words[4];
	int call;

	random_state = seed * 2654435761u + 20261004u;
	memset(&room, 0x5A, sizeof(room));
	words[0] = words[1] = words[2] = words[3] = 0x5A5A5A5Au;
	model_read(&model, words);

	for(call = 0; call < MEMORY_CALLS; call++)
	{
		uint32_t what = random_below(100);
		const char *did;
		bool same = true;

		if(what < 45)
		{
			uint32_t which = random_below(100);
			int reset = which < 10 ? GUARD_RESET_POWER_ON : which < 35 ? GUARD_RESET_SOFTWARE : which < 90 ? GUARD_RESET_CRASH : strange[random_below(6)];
			bool knob_held = random_below(5) == 0;
			bool safe_mode, previous_layout;
			guard_start_t decided = guard_start(&room.memory, (guard_reset_t)reset, knob_held);

			model_start(&model, reset, knob_held, &safe_mode, &previous_layout);
			same = decided.safe_mode == safe_mode && decided.previous_layout == previous_layout;
			did = "start";
		}
		else if(what < 53)
		{
			guard_alive(&room.memory);
			model.count = 0;
			model.mark = false;
			reach.alive++;
			did = "alive";
		}
		else if(what < 68)
		{
			guard_layout_stored(&room.memory);
			model.mark = true;
			reach.stored++;
			did = "layout stored";
		}
		else
		{
			// The memory is overwritten: by something valid, by damage of one bit, by anything
			uint32_t kind = random_below(10);

			words[0] = room.memory.magic;
			words[1] = room.memory.crashes;
			words[2] = room.memory.layout_fresh;
			words[3] = room.memory.check;
			if(kind < 4)
			{
				words[0] = 0x57444731u;
				words[1] = random_below(8) == 0 ? random_word() : counters[random_below(13)];
				words[2] = random_below(3) == 0 ? 0x57444731u : random_below(3) == 0 ? random_word() : marks[random_below(6)];
				words[3] = ~(words[0] ^ words[1] ^ words[2]);
			}
			else if(kind < 7)
			{
				uint32_t bit = random_below(32);

				words[random_below(4)] ^= 1u << bit;
			}
			else if(kind < 8)
			{
				// Another magic with a check that fits it
				words[0] = random_below(2) == 0 ? 0x57444732u : random_word();
				words[3] = ~(words[0] ^ words[1] ^ words[2]);
			}
			else if(kind < 9)
			{
				words[0] = random_word();
				words[1] = random_word();
				words[2] = random_word();
				words[3] = random_word();
			}
			else
			{
				words[0] = words[1] = words[2] = words[3] = random_below(2) == 0 ? 0 : 0xFFFFFFFFu;
			}
			room.memory.magic = words[0];
			room.memory.crashes = words[1];
			room.memory.layout_fresh = words[2];
			room.memory.check = words[3];
			model_read(&model, words);
			reach.written++;
			continue;
		}

		model_words(&model, words);
		if(room.memory.magic != words[0] || room.memory.crashes != words[1] || room.memory.layout_fresh != words[2] || room.memory.check != words[3]) same = false;
		if(room.before[0] != 0x5A5A5A5Au || room.before[1] != 0x5A5A5A5Au || room.after[0] != 0x5A5A5A5Au || room.after[1] != 0x5A5A5A5Au) same = false;
		if(!same)
		{
			printf("  memory walk %lu, call %d, after %s: memory %08lX %08lX %08lX %08lX, expected %08lX %08lX %08lX %08lX (or another decision, or written beside the memory)\n",
			       (unsigned long)seed, call, did, (unsigned long)room.memory.magic, (unsigned long)room.memory.crashes, (unsigned long)room.memory.layout_fresh,
			       (unsigned long)room.memory.check, (unsigned long)words[0], (unsigned long)words[1], (unsigned long)words[2], (unsigned long)words[3]);
			return call;
		}
	}
	return -1;
}

// Heat as a table: the level before in the rows, the range of the temperature in the columns
static guard_heat_t heat_model(guard_heat_t before, int temp_c, bool valid)
{
	static const guard_heat_t levels[3] = {GUARD_HEAT_NORMAL, GUARD_HEAT_DIM, GUARD_HEAT_OFF};
	//                                  below 70  70 to 74  75 to 79  80 to 84  from 85
	static const int table[3][5] = {
		/* normal */                    {0,       0,        1,        1,        2},
		/* dimmed */                    {0,       1,        1,        1,        2},
		/* off */                       {0,       1,        1,        2,        2},
	};
	int row = before == GUARD_HEAT_NORMAL ? 0 : before == GUARD_HEAT_DIM ? 1 : 2;
	int column = temp_c < 70 ? 0 : temp_c < 75 ? 1 : temp_c < 80 ? 2 : temp_c < 85 ? 3 : 4;

	return levels[valid ? table[row][column] : row];
}

// Brightness counted up from 0 until the wish or the limit of the level is reached
static int brightness_model(guard_heat_t heat, int wanted)
{
	int limit = heat == GUARD_HEAT_NORMAL ? 100 : heat == GUARD_HEAT_DIM ? 30 : 0;
	int value = 0;

	while(value < wanted && value < limit) value++;
	return value;
}

// Every level, also those that are none, with every temperature and every brightness around the limits and
// at the ends of int. Returns how many answers differ from the models.
static int heat_sweep(void)
{
	static const int levels[] = {GUARD_HEAT_NORMAL, GUARD_HEAT_DIM, GUARD_HEAT_OFF, 3, 4, 77, -1, INT_MAX, INT_MIN};
	static const int ends[] = {INT_MIN, INT_MIN + 1, -100000, 100000, INT_MAX - 1, INT_MAX};
	int different = 0;
	size_t l, e;
	int value;

	for(l = 0; l < sizeof(levels) / sizeof(levels[0]); l++)
	{
		guard_heat_t level = (guard_heat_t)levels[l];

		for(value = -300; value <= 300; value++)
		{
			if(guard_heat(level, value, true) != heat_model(level, value, true)) different++;
			if(guard_heat(level, value, false) != heat_model(level, value, false)) different++;
			if(guard_brightness(level, value) != brightness_model(level, value)) different++;
		}
		for(e = 0; e < sizeof(ends) / sizeof(ends[0]); e++)
		{
			if(guard_heat(level, ends[e], true) != heat_model(level, ends[e], true)) different++;
			if(guard_heat(level, ends[e], false) != heat_model(level, ends[e], false)) different++;
			if(guard_brightness(level, ends[e]) != brightness_model(level, ends[e])) different++;
		}
	}
	if(different > 0) printf("  heat: %d answers differ from the model\n", different);
	return different;
}

/*
 * The catalogue: the moment from which the check sum counts as at rest instead of the time it stands, and
 * what is in the flash as its own pair of values.
 */
typedef struct
{
	bool watching;
	uint32_t watched;
	uint64_t first;         // when the watched check sum was seen first
	bool flash_has;
	uint32_t flash_sum;
	bool wrote;             // on this connection
} catalog_model_t;

static void catalog_model_init(catalog_model_t *model, bool has_stored, uint32_t stored_sum)
{
	memset(model, 0, sizeof(*model));
	model->flash_has = has_stored;
	model->flash_sum = stored_sum;
}

static bool catalog_model_due(catalog_model_t *model, uint32_t sum, bool complete, uint64_t now_ms)
{
	bool rested, differs;

	if(!model->watching || model->watched != sum)
	{
		if(model->watching) reach.catalog_back++;
		model->watching = true;
		model->watched = sum;
		model->first = now_ms;
	}
	// A first sighting so late that the time has no room for the rest behind it never comes to rest
	rested = model->first <= UINT64_MAX - 30000 && now_ms >= model->first + 30000;
	differs = !model->flash_has || model->flash_sum != sum;

	if(!rested)
	{
		if(complete && differs && !model->wrote) reach.catalog_too_early++;
		return false;
	}
	if(!complete)
	{
		reach.catalog_incomplete++;
		return false;
	}
	if(!differs)
	{
		reach.catalog_same++;
		return false;
	}
	if(model->wrote)
	{
		reach.catalog_second++;
		return false;
	}
	model->flash_has = true;
	model->flash_sum = sum;
	model->wrote = true;
	reach.catalog_due++;
	return true;
}

#define CATALOG_WALKS   40
#define CATALOG_CALLS   20000

// Rounds with a check sum that changes now and then, connections, restarts, and times that mostly go on by
// a second, often land around the end of the rest time and sometimes step back
static int catalog_walk(uint32_t seed)
{
	static const uint32_t sums[] = {0, 1, 0x1111, 0x2222, 0x80001111u, 0x00011111u, 0x7FFFFFFFu, 0xFFFFFFFFu};
	static const uint64_t far[] = {65536, 0xFFFFFFFFull, 0x100000000ull, 0x100000000ull + 29999, 0x10000000000ull};
	guard_catalog_t module;
	catalog_model_t model;
	bool at_the_end = seed % 8 == 7;
	uint64_t now = at_the_end ? UINT64_MAX - 60000 : (uint64_t)seed * 1000;
	uint64_t changed = now;     // when the walk last changed something
	uint32_t sum = sums[0];
	bool complete = false;
	int call;

	random_state = seed * 2654435761u + 4102026u;
	guard_catalog_init(&module, false, 0);
	catalog_model_init(&model, false, 0);

	for(call = 0; call < CATALOG_CALLS; call++)
	{
		uint32_t what = random_below(1000);
		uint32_t jump = random_below(100);
		bool got, expected;

		if(jump < 60) now += 1000;
		else if(jump < 80) now = changed + 29998 + random_below(4);
		else if(jump < 90) now += random_below(40000);
		else if(jump < 94) now += 0;
		else if(jump < 95) now += random_below(20) == 0 ? far[random_below(5)] : 1;
		else
		{
			uint32_t back = random_below(40000);

			now = now > back ? now - back : 0;
		}

		if(what < 4)
		{
			bool has_stored = random_below(3) != 0;
			uint32_t stored = sums[random_below(8)];

			guard_catalog_init(&module, has_stored, stored);
			catalog_model_init(&model, has_stored, stored);
			// The display restarted: its time begins anew, or shortly before the largest time in the walks
			// that began there
			if(at_the_end) now = UINT64_MAX - random_below(60000);
			else if(random_below(2) == 0) now = random_below(3) == 0 ? 0 : random_below(5000);
			changed = now;
			continue;
		}
		if(what < 30)
		{
			guard_catalog_connected(&module);
			model.wrote = false;
			model.watching = false;
			reach.catalog_connected++;
			changed = now;
			continue;
		}
		if(what < 60)
		{
			sum = sums[random_below(8)];
			changed = now;
		}
		if(what >= 60 && what < 90) complete = !complete;

		got = guard_catalog_due(&module, sum, complete, now);
		expected = catalog_model_due(&model, sum, complete, now);
		if(got != expected)
		{
			printf("  catalogue walk %lu, call %d: check sum %08lX, complete %d at %llu: expected %d, got %d\n", (unsigned long)seed, call, (unsigned long)sum,
			       complete, (unsigned long long)now, expected, got);
			return call;
		}
	}
	return -1;
}

static bool reached_enough(void)
{
	return reach.safe_by_counter > 50000 && reach.safe_by_knob > 20000 && reach.previous > 20000 && reach.at_largest > 3000 && reach.unknown_memory > 80000 &&
	       reach.software > 40000 && reach.power_on > 15000 && reach.strange_reset > 15000 && reach.alive > 30000 && reach.stored > 60000 && reach.written > 100000 &&
	       reach.catalog_due > 4000 && reach.catalog_same > 60000 && reach.catalog_incomplete > 80000 && reach.catalog_second > 20000 && reach.catalog_too_early > 20000 &&
	       reach.catalog_connected > 10000 && reach.catalog_back > 10000;
}

// Exit status of the child: 0 all well, 10 something differs from the models, 11 the walks did not reach
// enough, 12 both
static void test_walks_against_the_models(void)
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
		for(seed = 1; seed <= MEMORY_WALKS; seed++)
		{
			if(memory_walk(seed) >= 0) different++;
		}
		different += heat_sweep();
		for(seed = 1; seed <= CATALOG_WALKS; seed++)
		{
			if(catalog_walk(seed) >= 0) different++;
		}
		printf("  memory walks: %ld safe modes by the counter, %ld by the knob, %ld previous layouts, %ld crashes at the largest counter, %ld memories without a counter,\n"
		       "  %ld restarts by software, %ld power on, %ld reasons outside the enum, %ld alive, %ld layouts stored, %ld memories overwritten\n"
		       "  catalogue walks: %ld due, %ld at rest but stored already, %ld at rest but incomplete, %ld at rest but written before, %ld not at rest yet,\n"
		       "  %ld connections, %ld changes of the check sum\n",
		       reach.safe_by_counter, reach.safe_by_knob, reach.previous, reach.at_largest, reach.unknown_memory, reach.software, reach.power_on, reach.strange_reset,
		       reach.alive, reach.stored, reach.written, reach.catalog_due, reach.catalog_same, reach.catalog_incomplete, reach.catalog_second, reach.catalog_too_early,
		       reach.catalog_connected, reach.catalog_back);
		fflush(stdout);
		_exit((different == 0 ? 0 : 10) + (reached_enough() ? 0 : different == 0 ? 11 : 2));
	}
	if(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status)) code = WEXITSTATUS(status);

	check(code == 0 || code == 10 || code == 11 || code == 12, "random walks over the memory and the catalogue, and the sweep over heat and brightness: no crash and no hang");
	check(code == 0 || code == 11,
	      "40 random walks of 20000 calls over the memory, 40 over the catalogue, every level with every temperature and brightness: the answers of the models after every call");
	check(code == 0 || code == 10,
	      "the walks reach safe modes by counter and by knob, previous layouts, the largest counter, damaged memories, every reset reason, catalogues due and "
	      "refused for every reason in numbers");
}

int main(void)
{
	test_walks_against_the_models();
	test_constants();
	test_three_crashes();
	test_alive_between();
	test_restart_between();
	test_knob_held();
	test_layout_mark();
	test_alive_and_stored();
	test_start_table();
	test_every_bit_of_the_memory();
	test_unknown_reset();
	test_heat();
	test_brightness();
	test_catalog_due();
	test_catalog_incomplete();
	test_catalog_rest();
	test_catalog_connected();
	test_catalog_clock();
	test_catalog_every_bit();
	test_catalog_long_times();
	test_catalog_init();
	return test_end();
}
