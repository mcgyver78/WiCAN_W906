/*
 * Helpers shared by the host tests of the display logic (display/components/core).
 * A test is one program test_<module>.c: it prints PASS or FAIL per check, "OK" or "FAILED" at the
 * end, and returns 0 only if every check passed. redproof.py relies on exactly that.
 */
#ifndef __TEST_H__
#define __TEST_H__

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static int test_failures = 0;

static void check(int condition, const char *what)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	// Nothing may get lost if a later check crashes
	fflush(stdout);
	if(!condition) test_failures++;
}

static int test_end(void)
{
	printf("%s\n", test_failures ? "FAILED" : "OK");
	return test_failures ? 1 : 0;
}

// Content of a fixture file, zero terminated, without the line break at its end. Fixtures are written by
// hand from the contract (tools/w906/API.md, display/SPEC.md), never by the code under test.
// The tests run in display/test: path is relative to it, e.g. "fixtures/layout_w906.json" or
// "../../tools/w906/fixtures/api_state_run.json". Returns false if the file is missing or too large.
static bool read_fixture(const char *path, char *text, size_t size)
{
	FILE *file = fopen(path, "rb");
	size_t length;

	if(file == NULL)
	{
		printf("  cannot open %s (run the test in display/test)\n", path);
		return false;
	}
	length = fread(text, 1, size, file);
	fclose(file);
	if(length >= size)
	{
		printf("  %s does not fit into %lu bytes\n", path, (unsigned long)size);
		return false;
	}
	while(length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) length--;
	text[length] = '\0';
	return true;
}

static inline void test_unused(void)
{
	(void)read_fixture;
	(void)test_end;
	(void)check;
}

#endif
