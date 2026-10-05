/*
 * What the simulations of the platform share (main_sim.c, net_sim.c, web_sim.c, screen_sim.c): the checks and
 * the log.
 *
 * A simulation is one program. It includes one file of display/main unchanged, as a unit of its own, and runs
 * it against the real core (display/components/core) and against stand-ins for everything below: the headers
 * in idf/ say what ESP-IDF declares, the simulation says what it does. It prints a line "FAIL ..." for every
 * check that failed and "<n> checks, <m> failed" at its end, and returns 0 only if none failed. redproof.py
 * relies on exactly that: a mutation of the platform file has to end in a line FAIL, not in a crash.
 *
 * So a rule a stand-in watches is a CHECK as well, not an assert(): the lock that must not be held where the
 * platform waits, a call the driver would refuse. Such a check is counted every time it is looked at. Where
 * the stand-in could not go on behind a broken rule, it says so with a FAIL and carries on in a way that is
 * safe for the PC. NEED() is for the simulation itself (a file it cannot open): that is nobody's rule, and it
 * ends the program with status 2.
 *
 * A header with code, for one program each.
 */
#ifndef __SIM_H__
#define __SIM_H__

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static int sim_checks;
static int sim_failures;
// Set by a simulation that has something to tell about its world when a check fails
static void (*sim_state)(void);

static bool sim_check(bool ok, const char *what, const char *function, int line)
{
	sim_checks++;
	if(!ok)
	{
		sim_failures++;
		printf("FAIL %s:%d: %s\n", function, line, what);
		if(sim_state != NULL)
		{
			sim_state();
		}
		// Nothing may get lost if a later step crashes
		fflush(stdout);
	}
	return ok;
}

// As an expression: true if the check passed, so that a stand-in can step around what would crash the PC
#define CHECK(condition)    sim_check((condition), #condition, __func__, __LINE__)

#define NEED(condition) do { \
		if(!(condition)) \
		{ \
			printf("the simulation cannot go on, %s:%d: %s\n", __func__, __LINE__, #condition); \
			exit(2); \
		} \
	} while(0)

// What the platform logs (idf/esp_log.h). The lines are part of the output: they tell what happened before
// a check failed. A simulation that has to know whether something was logged names a text to look for.
static const char *sim_log_wanted;
static int sim_log_found;

void sim_log(char level, const char *tag, const char *format, ...)
{
	char line[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	if(sim_log_wanted != NULL && strstr(line, sim_log_wanted) != NULL)
	{
		sim_log_found++;
	}
	printf("%c %s: %s\n", level, tag, line);
}

// Memory as the heap of the device hands it out: not zeroed. Filled with 0xA5, so that nobody counts on a
// zero there and a run is the same on every PC.
static void *sim_alloc(size_t size)
{
	void *block = malloc(size);

	NEED(block != NULL);
	memset(block, 0xA5, size);
	return block;
}

static int sim_end(void)
{
	printf("%d checks, %d failed\n", sim_checks, sim_failures);
	return sim_failures != 0 ? 1 : 0;
}

#endif
