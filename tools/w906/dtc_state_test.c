/*
 * Host test for main/dtc_state.c, the rules of the fault memory scan shared by MQTT and HTTP.
 * Run in tools/w906 (the JSON output is compared with the files in fixtures/):
 *   cc -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I../../main ../../main/dtc_state.c dtc_state_test.c -o dtc_state_test && ./dtc_state_test
 *
 * Two kinds of checks:
 *   - examples: one call sequence, one expected outcome, named after the rule (they are the specification)
 *   - a walk: long pseudo-random call sequences, after every call the module is compared with a second,
 *     independent implementation of the rules further down in this file (it catches the combinations no
 *     example thinks of)
 * redproof.py applies changes that remove or weaken a rule and expects this test to fail for each.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "dtc_state.h"

#define READ    false
#define CLEAR   true

static int failures = 0;

static void check(int condition, const char *what)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	// Nothing may get lost if a later check crashes
	fflush(stdout);
	if(!condition) failures++;
}

static int same_text(const char *a, const char *b)
{
	return (a == NULL && b == NULL) || (a != NULL && b != NULL && strcmp(a, b) == 0);
}

static int same_state(const dtc_state_t *a, const dtc_state_t *b)
{
	return a->phase == b->phase && a->clear == b->clear && a->src == b->src && a->seq == b->seq &&
	       a->next_seq == b->next_seq && a->queued_ms == b->queued_ms && a->finished_ms == b->finished_ms &&
	       a->step == b->step && a->total == b->total && same_text(a->name, b->name) &&
	       same_text(a->reason, b->reason) && a->result_seq == b->result_seq && a->result_count == b->result_count;
}

static int json_is(const dtc_state_t *s, bool supported, uint64_t now_ms, const char *expected)
{
	char json[512];
	int length;

	// A missing terminating zero shows up as a difference instead of a read behind the buffer
	memset(json, '~', sizeof(json));
	length = dtc_state_json(s, supported, now_ms, json, sizeof(json));
	json[sizeof(json) - 1] = '\0';

	if(length == (int)strlen(expected) && strcmp(json, expected) == 0) return 1;

	printf("  got      %s\n  expected %s\n", json, expected);
	return 0;
}

static int json_is_fixture(const dtc_state_t *s, bool supported, uint64_t now_ms, const char *name)
{
	char path[128];
	char expected[512];
	size_t length;
	FILE *file;

	snprintf(path, sizeof(path), "fixtures/%s", name);
	file = fopen(path, "rb");
	if(file == NULL)
	{
		printf("  cannot open %s (run the test in tools/w906)\n", path);
		return 0;
	}
	length = fread(expected, 1, sizeof(expected) - 1, file);
	fclose(file);
	while(length > 0 && (expected[length - 1] == '\n' || expected[length - 1] == '\r')) length--;
	expected[length] = '\0';

	return json_is(s, supported, now_ms, expected);
}

// A complete read that found `count` trouble codes, returns its sequence number
static uint32_t read_done(dtc_state_t *s, dtc_src_t src, uint16_t count, uint64_t start_ms, uint64_t end_ms)
{
	uint32_t seq = 0;

	dtc_state_try_begin(s, READ, src, 0, start_ms, &seq);
	dtc_state_pickup(s, start_ms);
	dtc_state_done(s, count, end_ms);
	return seq;
}

/* ------------------------------------------------------------------------------------------------ */
/* Examples                                                                                           */
/* ------------------------------------------------------------------------------------------------ */

static void test_constants(void)
{
	check(DTC_CLEAR_MAX_AGE_MS == 600000u, "a read can be cleared for 600 s");
	check(DTC_HTTP_EXPIRY_MS == 20000u, "an HTTP request expires after 20 s");
	check(DTC_SEQ_MAX == 2147483647u, "sequence numbers end at 2^31-1");
	check(DTC_ACCEPTED == 0, "DTC_ACCEPTED is 0, a caller may test the result like a boolean");
}

static void test_init(void)
{
	dtc_state_t s, fresh;

	// Defined memory, so that the comparison below does not depend on what the stack held before
	memset(&s, 0, sizeof(s));
	memset(&fresh, 0, sizeof(fresh));
	dtc_state_init(&fresh, 41);
	dtc_state_init(&s, 99);
	read_done(&s, DTC_SRC_HTTP, 3, 1000, 36000);
	dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, 0, 40000, NULL);
	dtc_state_pickup(&s, 40000);
	dtc_state_progress(&s, 4, 18, "N15/5 Wählhebelmodul (EWM)");
	dtc_state_init(&s, 41);
	check(same_state(&s, &fresh), "init resets a used state completely");
	check(json_is_fixture(&s, true, 500, "dtc_state_idle.json"), "JSON after init of a used state is idle");
}

static void test_sequence_numbers(void)
{
	dtc_state_t s;
	uint32_t seq = 0;

	dtc_state_init(&s, 0);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, 0, &seq);
	check(seq == 1, "seed 0: first number is 1, never 0");
	check(json_is(&s, true, 0, "{\"supported\":true,\"state\":\"queued\",\"action\":\"read\",\"src\":\"http\","
	      "\"seq\":1,\"ecu\":0,\"total\":0,\"name\":\"\",\"reason\":\"\",\"age_s\":0,\"count\":0,\"result_seq\":0}"),
	      "JSON of request number 1");

	dtc_state_init(&s, 0x80000000u);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, 0, &seq);
	check(seq == 1, "seed 0x80000000: masked to 31 bit, 0 skipped");

	dtc_state_init(&s, 0x80000029u);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, 0, &seq);
	check(seq == 41, "seed 0x80000029: number 41, below 2^31");

	dtc_state_init(&s, 0xFFFFFFFEu);
	seq = read_done(&s, DTC_SRC_HTTP, 0, 0, 10);
	check(seq == 0x7FFFFFFEu, "seed 0xFFFFFFFE: first number is 2^31-2");
	seq = read_done(&s, DTC_SRC_HTTP, 0, 20, 30);
	check(seq == 0x7FFFFFFFu, "the number after 2^31-2 is 2^31-1");
	seq = read_done(&s, DTC_SRC_HTTP, 0, 40, 50);
	check(seq == 1, "after 2^31-1 the numbers continue with 1");

	dtc_state_init(&s, 0xFFFE);
	read_done(&s, DTC_SRC_HTTP, 0, 0, 10);
	seq = read_done(&s, DTC_SRC_HTTP, 0, 20, 30);
	check(seq == 0xFFFF, "the number after 65534 is 65535");
	seq = read_done(&s, DTC_SRC_HTTP, 0, 40, 50);
	check(seq == 0x10000, "the number after 65535 is 65536");

	dtc_state_init(&s, 7);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, 0, &seq);
	dtc_state_pickup(&s, 5);
	dtc_state_error(&s, "ecu_offline", 10);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, 20, &seq);
	check(seq == 8, "a failed request has used its number too");
	dtc_state_pickup(&s, 25);
	dtc_state_error(&s, "ecu_offline", 30);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, 40, &seq);
	check(seq == 9, "the request after two failed ones gets a new number again");

	dtc_state_init(&s, 7);
	check(dtc_state_try_begin(&s, READ, DTC_SRC_MQTT, 0, 0, NULL) == DTC_ACCEPTED && s.seq == 7,
	      "request without seq_out");
}

static void test_json_of_a_run(void)
{
	dtc_state_t s, before;
	uint32_t seq = 0;

	dtc_state_init(&s, 41);
	check(json_is_fixture(&s, true, 500, "dtc_state_idle.json"), "JSON idle");
	check(json_is_fixture(&s, false, 500, "dtc_state_unsupported.json"), "JSON profile without fault memory");
	check(json_is_fixture(&s, true, 3600000, "dtc_state_idle.json"), "JSON idle has no age, however long ago the boot was");
	check(!dtc_state_busy(&s), "idle is not busy");

	check(dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, 1000, &seq) == DTC_ACCEPTED && seq == 41,
	      "read accepted in idle");
	check(s.phase == DTC_STATE_QUEUED && dtc_state_busy(&s), "accepted request is queued and busy");
	check(json_is_fixture(&s, true, 1500, "dtc_state_queued.json"), "JSON queued");

	check(dtc_state_pickup(&s, 1200) && s.phase == DTC_STATE_RUNNING, "pickup starts the scan");
	dtc_state_progress(&s, 5, 18, "N30/4 ESP");
	check(json_is_fixture(&s, true, 11000, "dtc_state_running.json"), "JSON running");
	dtc_state_progress(&s, 17, 18, "N2/14 Rückhaltesystem (SRS)");
	check(json_is_fixture(&s, true, 33000, "dtc_state_running_umlaut.json"), "JSON running, UTF-8 name unchanged");
	dtc_state_progress(&s, 0, 18, NULL);
	check(s.name == NULL && s.step == 0, "progress without a name drops the old one");

	dtc_state_progress(&s, 18, 18, "N69/1 Fahrertür (TSG)");
	dtc_state_done(&s, 3, 36000);
	check(s.phase == DTC_STATE_DONE && !dtc_state_busy(&s), "done is not busy");
	check(json_is_fixture(&s, true, 48500, "dtc_state_done.json"), "JSON done, age in whole seconds");
	check(json_is(&s, true, 35000, "{\"supported\":true,\"state\":\"done\",\"action\":\"read\",\"src\":\"http\","
	      "\"seq\":41,\"ecu\":18,\"total\":18,\"name\":\"\",\"reason\":\"\",\"age_s\":0,\"count\":3,\"result_seq\":41}"),
	      "JSON done with a clock 1 s behind: age 0, not 49 days");

	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, 0, 50000, &seq) == DTC_ACCEPTED && seq == 42,
	      "next request gets the next number");
	check(json_is(&s, true, 50500, "{\"supported\":true,\"state\":\"queued\",\"action\":\"clear\",\"src\":\"mqtt\","
	      "\"seq\":42,\"ecu\":0,\"total\":0,\"name\":\"\",\"reason\":\"\",\"age_s\":0,\"count\":3,\"result_seq\":41}"),
	      "JSON queued after a result: no age, progress reset, result kept");
	dtc_state_pickup(&s, 50100);
	dtc_state_progress(&s, 0, 18, NULL);
	dtc_state_error(&s, "engine_running", 51000);
	check(json_is_fixture(&s, true, 54999, "dtc_state_error.json"), "JSON error, earlier result still referenced");

	before = s;
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, 42, 55000, NULL) == DTC_REJECT_READ_REQUIRED &&
	      same_state(&s, &before) && same_text(s.reason, "engine_running"),
	      "a refused request keeps the reason of the last error");

	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, 60000, &seq);
	check(json_is(&s, true, 60000, "{\"supported\":true,\"state\":\"queued\",\"action\":\"read\",\"src\":\"http\","
	      "\"seq\":43,\"ecu\":0,\"total\":0,\"name\":\"\",\"reason\":\"\",\"age_s\":0,\"count\":3,\"result_seq\":41}"),
	      "a new request clears the reason of the last error");
	dtc_state_pickup(&s, 60100);
	dtc_state_progress(&s, 9, 18, "N80 Mantelrohrmodul (MRM)");
	dtc_state_done(&s, 0, 95000);
	dtc_state_try_begin(&s, READ, DTC_SRC_MQTT, 0, 96000, &seq);
	check(s.step == 0 && s.total == 0, "a read after a finished scan starts with the progress reset, like a clear");
}

static void test_json_limits(void)
{
	dtc_state_t s;

	dtc_state_init(&s, 0xFFFFFFFFu);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, 1000, NULL);
	dtc_state_pickup(&s, 1000);
	dtc_state_progress(&s, 255, 255, "N10 SAM");
	dtc_state_done(&s, 65535, 5000);
	check(json_is_fixture(&s, true, 5000 + 2147483647999ull, "dtc_state_limits.json"),
	      "JSON with every number at its limit");
	check(json_is(&s, true, 5000 + 4294967295999ull, "{\"supported\":true,\"state\":\"done\",\"action\":\"read\",\"src\":\"http\","
	      "\"seq\":2147483647,\"ecu\":255,\"total\":255,\"name\":\"\",\"reason\":\"\",\"age_s\":4294967295,\"count\":65535,\"result_seq\":2147483647}"),
	      "JSON with an age of 2^32-1 s");
}

static void test_busy(void)
{
	dtc_state_t s, before;
	uint32_t seq;

	dtc_state_init(&s, 7);
	dtc_state_try_begin(&s, READ, DTC_SRC_MQTT, 0, 100, NULL);

	before = s;
	seq = 0xDEADBEEFu;
	check(dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, 200, &seq) == DTC_REJECT_BUSY, "queued: HTTP read is busy");
	check(seq == 7, "a busy answer carries the number of the request that is in the way");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, 0, 200, NULL) == DTC_REJECT_BUSY, "queued: MQTT clear is busy");
	check(same_state(&s, &before), "queued: a rejected request changes nothing");

	dtc_state_pickup(&s, 300);
	dtc_state_progress(&s, 6, 18, "N10 SAM");
	before = s;
	check(dtc_state_try_begin(&s, READ, DTC_SRC_MQTT, 0, 400, NULL) == DTC_REJECT_BUSY, "running: MQTT read is busy");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, 7, 400, NULL) == DTC_REJECT_BUSY, "running: HTTP clear is busy");
	check(same_state(&s, &before), "running: a rejected request changes nothing, the progress stays");

	dtc_state_init(&s, 7);
	read_done(&s, DTC_SRC_HTTP, 2, 100, 35000);
	dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, s.seq, 36000, NULL);
	before = s;
	check(dtc_state_try_begin(&s, READ, DTC_SRC_MQTT, 0, 36100, NULL) == DTC_REJECT_BUSY,
	      "scan started over HTTP: an MQTT read during a queued clear is busy");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, 0, 36100, NULL) == DTC_REJECT_BUSY,
	      "scan started over HTTP: an MQTT clear is busy");
	check(same_state(&s, &before), "scan started over HTTP: rejected MQTT commands change nothing");
}

static void test_calls_in_the_wrong_phase(void)
{
	dtc_state_t s, before;
	int clear;

	// The same for a read and for a clear
	for(clear = 0; clear <= 1; clear++)
	{
		dtc_state_init(&s, 7);
		before = s;
		check(!dtc_state_pickup(&s, 10) && same_state(&s, &before), "idle: nothing to pick up");
		dtc_state_error(&s, "internal", 10);
		check(same_state(&s, &before), "idle: error is ignored");
		dtc_state_done(&s, 5, 10);
		check(same_state(&s, &before), "idle: done is ignored");
		dtc_state_progress(&s, 3, 18, "N10 SAM");
		check(same_state(&s, &before), "idle: progress is ignored");

		dtc_state_try_begin(&s, clear != 0, DTC_SRC_MQTT, 0, 100, NULL);
		before = s;
		dtc_state_done(&s, 5, 110);
		check(same_state(&s, &before), "queued: done is ignored, the scan has not run");
		dtc_state_progress(&s, 3, 18, "N10 SAM");
		check(same_state(&s, &before), "queued: progress is ignored");
		dtc_state_error(&s, "internal", 120);
		check(same_state(&s, &before), "queued: error is ignored, only the task that picked it up ends a request");

		dtc_state_pickup(&s, 200);
		dtc_state_progress(&s, 3, 18, "N10 SAM");
		before = s;
		check(!dtc_state_pickup(&s, 300) && same_state(&s, &before), "running: a second pickup does not start another scan");

		dtc_state_error(&s, "ecu_offline", 400);
		check(s.phase == DTC_STATE_ERROR && same_text(s.reason, "ecu_offline") && s.finished_ms == 400 &&
		      s.name == NULL && s.step == 3, "running: an error ends the request, drops the name and keeps the step");
		before = s;
		check(!dtc_state_pickup(&s, 500) && same_state(&s, &before), "error: the failed request is not picked up again");
		dtc_state_error(&s, "internal", 500);
		check(same_state(&s, &before), "error: a second error keeps the first reason");
		dtc_state_done(&s, 2, 500);
		check(same_state(&s, &before), "error: done does not turn the error into a result");
		dtc_state_progress(&s, 4, 18, "N10 SAM");
		check(same_state(&s, &before), "error: progress is ignored");

		dtc_state_try_begin(&s, clear != 0, DTC_SRC_MQTT, 0, 1000, NULL);
		dtc_state_pickup(&s, 1000);
		dtc_state_done(&s, 2, 35000);
		before = s;
		dtc_state_error(&s, "internal", 36000);
		check(same_state(&s, &before), "done: a later error does not overwrite the result");
		dtc_state_done(&s, 9, 36000);
		check(same_state(&s, &before), "done: a second done is ignored");
		check(!dtc_state_pickup(&s, 36000) && same_state(&s, &before), "done: nothing to pick up");
		dtc_state_progress(&s, 4, 18, "N10 SAM");
		check(same_state(&s, &before), "done: progress is ignored");
	}
}

static void test_clear_is_bound_to_a_read(void)
{
	dtc_state_t s, t, before;
	uint32_t seq, other_seq = 0, clear_seq = 0;

	dtc_state_init(&s, 100);
	before = s;
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, 0, 1000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "clear without any read is refused");
	check(same_state(&s, &before), "refused clear changes nothing");
	t = s;
	check(dtc_state_try_begin(&t, READ, DTC_SRC_HTTP, 12345, 1000, NULL) == DTC_ACCEPTED,
	      "a read ignores the number");

	seq = read_done(&s, DTC_SRC_HTTP, 3, 5000, 40000);
	before = s;
	other_seq = 0xDEADBEEFu;
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq + 1, 41000, &other_seq) == DTC_REJECT_STALE_SEQ,
	      "clear with another number is refused");
	check(other_seq == seq, "a stale answer carries the number of the last read");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq ^ 0x40000000u, 41000, NULL) == DTC_REJECT_STALE_SEQ,
	      "clear with a number that differs in bit 30 only is refused");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq | 0x80000000u, 41000, NULL) == DTC_REJECT_STALE_SEQ,
	      "clear with a number that differs in bit 31 only is refused");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, 0, 41000, NULL) == DTC_REJECT_STALE_SEQ,
	      "clear with number 0 is refused");
	check(same_state(&s, &before), "stale clear changes nothing");

	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_HTTP, seq, 40000 + 600001, NULL) == DTC_REJECT_READ_REQUIRED,
	      "clear 600.001 s after the read is refused");
	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_HTTP, seq, 40000 + 600000, NULL) == DTC_ACCEPTED,
	      "clear exactly 600 s after the read is accepted");
	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_HTTP, seq, 40000 + 4294967296ull + 5000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "clear 2^32 ms + 5 s after the read is refused");
	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_HTTP, seq, 39995, NULL) == DTC_ACCEPTED,
	      "clear with a clock 5 ms behind the end of the read is accepted");
	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_HTTP, seq + 1, 40000 + 600001, NULL) == DTC_REJECT_READ_REQUIRED,
	      "too old and another number: the reason is read_required");
	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_MQTT, 0, 40000 + 600001, NULL) == DTC_ACCEPTED,
	      "MQTT clear long after a read, without a number, stays unbound");

	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 45000, &clear_seq) == DTC_ACCEPTED &&
	      clear_seq == seq + 1 && s.clear && s.src == DTC_SRC_HTTP, "clear with the number of the read is accepted");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 45100, NULL) == DTC_REJECT_BUSY,
	      "the same clear sent twice is busy");
	dtc_state_pickup(&s, 45200);
	dtc_state_progress(&s, 18, 18, "N69/1 Fahrertür (TSG)");
	dtc_state_done(&s, 1, 80000);
	check(s.result_seq == clear_seq && s.result_count == 1, "the result of a clear replaces the result of the read");
	check(json_is_fixture(&s, true, 81000, "dtc_state_done_clear.json"), "JSON done after a clear");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, clear_seq, 81000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "after a clear its own number cannot clear again");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 81000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "after a clear the number of the old read is used up");
	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_MQTT, 0, 81000, NULL) == DTC_ACCEPTED,
	      "MQTT clear right after a clear stays unbound");

	dtc_state_init(&s, 150);
	seq = read_done(&s, DTC_SRC_HTTP, 1, 0, 35000);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 36000, NULL) == DTC_ACCEPTED,
	      "a single trouble code can be cleared");

	dtc_state_init(&s, 160);
	seq = read_done(&s, DTC_SRC_HTTP, 256, 0, 35000);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 36000, NULL) == DTC_ACCEPTED,
	      "256 trouble codes can be cleared");

	dtc_state_init(&s, 200);
	seq = read_done(&s, DTC_SRC_MQTT, 3, 0, 35000);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, 36000, &other_seq);
	dtc_state_pickup(&s, 36100);
	dtc_state_progress(&s, 5, 18, "N30/4 ESP");
	dtc_state_error(&s, "ecu_offline", 36500);
	check(s.result_seq == seq && s.result_count == 3, "a failed read keeps the result of the read before");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, other_seq, 37000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "a failed read cannot be cleared although an older result is stored");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 37000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "the read before a failed read cannot be cleared either");
	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_MQTT, 0, 37000, NULL) == DTC_ACCEPTED,
	      "MQTT clear after a failed read stays unbound");

	dtc_state_init(&s, 250);
	dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, 0, 0, &seq);
	dtc_state_pickup(&s, 100);
	dtc_state_done(&s, 2, 35000);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 36000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "after an MQTT clear with codes left an HTTP clear with its number is refused");

	dtc_state_init(&s, 260);
	dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, 0, 0, &seq);
	dtc_state_pickup(&s, 100);
	dtc_state_done(&s, 0, 35000);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 36000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "after an MQTT clear without codes left the reason is read_required, not nothing_to_clear");

	dtc_state_init(&s, 300);
	read_done(&s, DTC_SRC_HTTP, 4, 0, 10000);
	seq = read_done(&s, DTC_SRC_HTTP, 0, 20000, 35000);
	check(s.result_seq == seq && s.result_count == 0, "a read without trouble codes replaces the older result");
	before = s;
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 36000, NULL) == DTC_REJECT_NOTHING_TO_CLEAR,
	      "clear after a read without trouble codes is refused");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq + 1, 36000, NULL) == DTC_REJECT_STALE_SEQ,
	      "another number and no trouble codes: the reason is stale_seq");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 35000 + 600001, NULL) == DTC_REJECT_READ_REQUIRED,
	      "too old and no trouble codes: the reason is read_required");
	check(same_state(&s, &before), "refused clear of an empty list changes nothing");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, 0, 36000, NULL) == DTC_ACCEPTED,
	      "MQTT clear after a read without trouble codes stays unbound");

	dtc_state_init(&s, 400);
	seq = read_done(&s, DTC_SRC_MQTT, 2, 0, 35000);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq + 1, 36000, NULL) == DTC_REJECT_STALE_SEQ,
	      "read over MQTT: HTTP clear with another number is refused");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 35000 + 600001, NULL) == DTC_REJECT_READ_REQUIRED,
	      "read over MQTT: HTTP clear 600.001 s later is refused");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 36000, NULL) == DTC_ACCEPTED,
	      "read over MQTT: HTTP clear with its number is accepted");

	dtc_state_init(&s, 450);
	seq = read_done(&s, DTC_SRC_MQTT, 0, 0, 35000);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 36000, NULL) == DTC_REJECT_NOTHING_TO_CLEAR,
	      "read over MQTT without trouble codes: HTTP clear is refused");

	dtc_state_init(&s, 500);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, 0, 0, NULL) == DTC_ACCEPTED,
	      "MQTT clear right after boot stays unbound");
}

static void test_expiry(void)
{
	dtc_state_t s, t;
	uint32_t seq;

	dtc_state_init(&s, 700);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, 1000, NULL);
	t = s;
	check(dtc_state_pickup(&t, 1000 + 20000) && t.phase == DTC_STATE_RUNNING,
	      "HTTP request picked up after exactly 20 s runs");
	t = s;
	check(!dtc_state_pickup(&t, 1000 + 20001) && t.phase == DTC_STATE_ERROR &&
	      same_text(t.reason, "expired") && t.finished_ms == 1000 + 20001,
	      "HTTP request picked up after 20.001 s expires and does not run");
	check(json_is(&t, true, 30000, "{\"supported\":true,\"state\":\"error\",\"action\":\"read\",\"src\":\"http\","
	      "\"seq\":700,\"ecu\":0,\"total\":0,\"name\":\"\",\"reason\":\"expired\",\"age_s\":8,\"count\":0,\"result_seq\":0}"),
	      "JSON of an expired request");
	t = s;
	check(!dtc_state_pickup(&t, 1000 + 4294967296ull + 5000), "HTTP request picked up 2^32 ms + 5 s later expires");
	t = s;
	check(dtc_state_pickup(&t, 995), "HTTP request picked up with a clock 5 ms behind runs");

	dtc_state_init(&s, 700);
	dtc_state_try_begin(&s, READ, DTC_SRC_MQTT, 0, 1000, NULL);
	check(dtc_state_pickup(&s, 1000 + 3600000u) && s.phase == DTC_STATE_RUNNING,
	      "MQTT read does not expire, as before");
	dtc_state_init(&s, 700);
	dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, 0, 1000, NULL);
	check(dtc_state_pickup(&s, 1000 + 3600000u) && s.phase == DTC_STATE_RUNNING,
	      "MQTT clear does not expire, as before");

	dtc_state_init(&s, 800);
	seq = read_done(&s, DTC_SRC_HTTP, 3, 0, 35000);
	dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 36000, NULL);
	check(!dtc_state_pickup(&s, 36000 + 20001), "expired clear does not run");
	check(s.result_seq == seq && s.result_count == 3, "an expired request keeps the stored result");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, 60000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "an expired clear has used up the read");
}

static void test_times_above_32_bit(void)
{
	const uint64_t late = 5000000000ull;    // 57 days after boot
	dtc_state_t s;
	uint32_t seq;

	dtc_state_init(&s, 900);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, late, NULL);
	check(dtc_state_pickup(&s, late + 100), "57 days after boot: an HTTP request picked up 100 ms later runs");
	dtc_state_done(&s, 2, late + 35000);
	check(json_is(&s, true, late + 40000, "{\"supported\":true,\"state\":\"done\",\"action\":\"read\",\"src\":\"http\","
	      "\"seq\":900,\"ecu\":0,\"total\":0,\"name\":\"\",\"reason\":\"\",\"age_s\":5,\"count\":2,\"result_seq\":900}"),
	      "57 days after boot: age of a result");
	seq = s.seq;
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, seq, late + 36000, NULL) == DTC_ACCEPTED,
	      "57 days after boot: clear 1 s after the read is accepted");
	dtc_state_pickup(&s, late + 36100);
	dtc_state_error(&s, "engine_running", late + 37000);
	check(json_is(&s, true, late + 39000, "{\"supported\":true,\"state\":\"error\",\"action\":\"clear\",\"src\":\"http\","
	      "\"seq\":901,\"ecu\":0,\"total\":0,\"name\":\"\",\"reason\":\"engine_running\",\"age_s\":2,\"count\":2,\"result_seq\":900}"),
	      "57 days after boot: age of an error");

	dtc_state_init(&s, 900);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, late, NULL);
	dtc_state_pickup(&s, late + 20001);
	check(json_is(&s, true, late + 23001, "{\"supported\":true,\"state\":\"error\",\"action\":\"read\",\"src\":\"http\","
	      "\"seq\":900,\"ecu\":0,\"total\":0,\"name\":\"\",\"reason\":\"expired\",\"age_s\":3,\"count\":0,\"result_seq\":0}"),
	      "57 days after boot: age of an expired request");
}

static void test_json_buffer_and_escaping(void)
{
	static char long_name[301];
	dtc_state_t s;
	char json[1024];
	char small[1024];
	int length, i, clean;

	dtc_state_init(&s, 41);
	length = dtc_state_json(&s, true, 0, json, sizeof(json));
	check(length > 0 && length == (int)strlen(json), "JSON returns its length");

	memset(small, 'x', sizeof(small));
	check(dtc_state_json(&s, true, 0, small, (size_t)length + 1) == length && small[length] == '\0' &&
	      strcmp(small, json) == 0 && small[length + 1] == 'x', "JSON fits exactly with one byte for the terminating zero");
	memset(small, 'x', sizeof(small));
	check(dtc_state_json(&s, true, 0, small, (size_t)length) == -1 && small[0] == '\0',
	      "JSON one byte too long: -1 and an empty string, not a truncated object");
	for(clean = 1, i = length; i < (int)sizeof(small); i++) clean = clean && small[i] == 'x';
	check(clean, "JSON one byte too long: nothing is written behind the buffer");
	memset(small, 'x', sizeof(small));
	check(dtc_state_json(&s, true, 0, small, 1) == -1 && small[0] == '\0' && small[1] == 'x',
	      "JSON into a 1 byte buffer: an empty string");
	memset(small, 'x', sizeof(small));
	check(dtc_state_json(&s, true, 0, small, 0) == -1 && small[0] == 'x', "JSON into a 0 byte buffer writes nothing");

	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, 0, 0, NULL);
	dtc_state_pickup(&s, 0);
	dtc_state_progress(&s, 1, 18, "a\"b\\c\td\ne\001f\037g\rh\033i j\177k");
	dtc_state_json(&s, true, 0, json, sizeof(json));
	check(strstr(json, "\"name\":\"a\\\"b\\\\cdefghi j\177k\",") != NULL,
	      "name: quote and backslash escaped, 0x01..0x1F dropped, space and 0x7F kept");

	memset(long_name, 'A', sizeof(long_name) - 1);
	dtc_state_progress(&s, 1, 18, long_name);
	length = dtc_state_json(&s, true, 0, json, sizeof(json));
	check(length == (int)strlen(json) && length > 400 && json[length - 1] == '}' && strstr(json, long_name) != NULL,
	      "JSON longer than 255 bytes is complete");

	dtc_state_error(&s, "a\"b\\c\037d", 10);
	dtc_state_json(&s, true, 10, json, sizeof(json));
	check(strstr(json, "\"reason\":\"a\\\"b\\\\cd\",") != NULL, "reason is escaped like the name");
}

static void test_reasons(void)
{
	check(dtc_accept_reason(DTC_ACCEPTED) == NULL, "no reason for an accepted request");
	check(same_text(dtc_accept_reason(DTC_REJECT_BUSY), "busy"), "reason busy");
	check(same_text(dtc_accept_reason(DTC_REJECT_READ_REQUIRED), "read_required"), "reason read_required");
	check(same_text(dtc_accept_reason(DTC_REJECT_STALE_SEQ), "stale_seq"), "reason stale_seq");
	check(same_text(dtc_accept_reason(DTC_REJECT_NOTHING_TO_CLEAR), "nothing_to_clear"), "reason nothing_to_clear");
}

/* ------------------------------------------------------------------------------------------------ */
/* Model: the rules of main/dtc_state.h once more, written from the header text and in another shape  */
/* (one request record with a status, reasons as text, JSON through one format string). The walk       */
/* below compares the module with it after every call.                                                 */
/* ------------------------------------------------------------------------------------------------ */

typedef enum { M_WAITING, M_STARTED, M_OK, M_FAILED } model_status_t;

typedef struct
{
	bool any;               // a request was accepted since init
	bool is_clear;
	bool from_http;
	uint32_t number;
	uint64_t accepted_at;
	model_status_t status;
	uint64_t ended_at;
	const char *why;
	unsigned step, total;
	const char *unit;
	uint32_t upcoming;
	uint32_t result_number;
	unsigned result_codes;
} model_t;

static void model_init(model_t *m, uint32_t seed)
{
	memset(m, 0, sizeof(*m));
	m->upcoming = seed % 2147483648u;
	if(m->upcoming == 0) m->upcoming = 1;
}

static bool model_active(const model_t *m)
{
	return m->any && (m->status == M_WAITING || m->status == M_STARTED);
}

static uint64_t model_since(uint64_t now, uint64_t then)
{
	return now > then ? now - then : 0;
}

// Returns the reason of a rejection, NULL if accepted
static const char *model_begin(model_t *m, bool clear, bool http, uint32_t seq, uint64_t now, uint32_t *number)
{
	*number = m->any ? m->number : 0;

	if(model_active(m)) return "busy";

	if(clear && http)
	{
		bool good_read = m->any && m->status == M_OK && !m->is_clear;

		if(!good_read || model_since(now, m->ended_at) > 600000) return "read_required";
		if(seq != m->number) return "stale_seq";
		if(m->result_codes == 0) return "nothing_to_clear";
	}

	m->any = true;
	m->is_clear = clear;
	m->from_http = http;
	m->number = m->upcoming;
	m->upcoming = m->upcoming == 2147483647u ? 1 : m->upcoming + 1;
	m->accepted_at = now;
	m->status = M_WAITING;
	m->step = 0;
	m->total = 0;
	m->unit = NULL;
	m->why = NULL;
	*number = m->number;
	return NULL;
}

static bool model_pickup(model_t *m, uint64_t now)
{
	if(!m->any || m->status != M_WAITING) return false;

	if(m->from_http && model_since(now, m->accepted_at) > 20000)
	{
		m->status = M_FAILED;
		m->why = "expired";
		m->ended_at = now;
		return false;
	}
	m->status = M_STARTED;
	return true;
}

static void model_progress(model_t *m, unsigned step, unsigned total, const char *unit)
{
	if(!m->any || m->status != M_STARTED) return;
	m->step = step;
	m->total = total;
	m->unit = unit;
}

static void model_error(model_t *m, const char *why, uint64_t now)
{
	if(!m->any || m->status != M_STARTED) return;
	m->status = M_FAILED;
	m->why = why;
	m->unit = NULL;
	m->ended_at = now;
}

static void model_done(model_t *m, unsigned codes, uint64_t now)
{
	if(!m->any || m->status != M_STARTED) return;
	m->status = M_OK;
	m->unit = NULL;
	m->ended_at = now;
	m->result_number = m->number;
	m->result_codes = codes;
}

static void model_escape(char *out, size_t size, const char *text)
{
	size_t n = 0;

	for(; text != NULL && *text != '\0' && n + 3 < size; text++)
	{
		unsigned char c = (unsigned char)*text;

		if(c <= 0x1F) continue;
		if(c == '\\' || c == '"') out[n++] = '\\';
		out[n++] = (char)c;
	}
	out[n] = '\0';
}

static int model_json(const model_t *m, bool supported, uint64_t now, char *out, size_t size)
{
	static const char *const states[] = {"queued", "running", "done", "error"};
	char unit[1400], why[1400], text[4096];
	bool ended = m->any && (m->status == M_OK || m->status == M_FAILED);
	int length;

	model_escape(unit, sizeof(unit), m->unit);
	model_escape(why, sizeof(why), m->why);
	length = snprintf(text, sizeof(text),
	                  "{\"supported\":%s,\"state\":\"%s\",\"action\":\"%s\",\"src\":\"%s\",\"seq\":%lu,\"ecu\":%u,"
	                  "\"total\":%u,\"name\":\"%s\",\"reason\":\"%s\",\"age_s\":%lu,\"count\":%u,\"result_seq\":%lu}",
	                  supported ? "true" : "false",
	                  m->any ? states[m->status] : "idle",
	                  !m->any ? "" : m->is_clear ? "clear" : "read",
	                  !m->any ? "" : m->from_http ? "http" : "mqtt",
	                  (unsigned long)m->number, m->step, m->total, unit, why,
	                  (unsigned long)(uint32_t)(ended ? model_since(now, m->ended_at) / 1000 : 0),
	                  m->result_codes, (unsigned long)m->result_number);

	if(size == 0) return -1;
	if((size_t)length + 1 > size)
	{
		out[0] = '\0';
		return -1;
	}
	memcpy(out, text, (size_t)length + 1);
	return length;
}

/* ------------------------------------------------------------------------------------------------ */
/* Walk                                                                                               */
/* ------------------------------------------------------------------------------------------------ */

static uint32_t walk_random_state;

static uint32_t walk_random(uint32_t below)
{
	walk_random_state = walk_random_state * 1664525u + 1013904223u;
	return (walk_random_state >> 8) % below;
}

#define WALK_STEPS      4000
#define WALK_BUFFER     1600

// Returns the step of the first difference, -1 if module and model agree all the way
static int walk(uint32_t walk_seed)
{
	static char long_unit[301], longer_unit[1101], all_bytes[256];
	unsigned byte;
	static const uint32_t seeds[] = {0, 1, 41, 0xFFFDu, 0xFFFEu, 0xFFFFu, 0x7FFFFFFDu, 0x7FFFFFFEu, 0x7FFFFFFFu,
	                                 0x80000000u, 0xFFFFFFFFu};
	static const uint64_t starts[] = {0, 1000, 4294967296ull - 30000, 4294967296ull + 5, 1099511627776ull};
	static const uint64_t small_steps[] = {0, 0, 1, 7, 100, 999, 1000, 1001, 5000};
	static const uint64_t limit_steps[] = {19999, 20000, 20001, 35000, 599999, 600000, 600001};
	static const uint64_t huge_steps[] = {3600000, 4294967296ull - 1, 4294967296ull + 5000};
	static const unsigned counts[] = {0, 0, 1, 2, 3, 255, 256, 300, 65535};
	const char *units[] = {NULL, "", "N30/4 ESP", "N2/14 Rückhaltesystem (SRS)", "a\"b\\c", "x\001y\037z\177", long_unit, all_bytes, longer_unit};
	const char *reasons[] = {"ecu_offline", "engine_running", "internal", "a\"b", NULL, "", long_unit, all_bytes, longer_unit};
	dtc_state_t s;
	model_t m;
	uint64_t now;
	uint32_t seed;
	int step;

	memset(long_unit, 'A', sizeof(long_unit) - 1);
	for(byte = 1; byte <= 255; byte++) all_bytes[byte - 1] = (char)byte;
	// 1100 bytes, every byte value once more at its end
	memset(longer_unit, 'B', sizeof(longer_unit) - 1);
	memcpy(longer_unit + sizeof(longer_unit) - 1 - 255, all_bytes, 255);
	memset(&s, 0, sizeof(s));
	walk_random_state = walk_seed;
	seed = seeds[walk_random(sizeof(seeds) / sizeof(seeds[0]))];
	now = starts[walk_random(sizeof(starts) / sizeof(starts[0]))];
	dtc_state_init(&s, seed);
	model_init(&m, seed);

	for(step = 0; step < WALK_STEPS; step++)
	{
		char got[WALK_BUFFER], expected[WALK_BUFFER];
		const char *what = "";
		uint32_t operation = walk_random(100);
		uint32_t pace = walk_random(100);
		size_t size = sizeof(got);
		int got_length, expected_length;
		bool supported = walk_random(2) != 0;
		bool same = true;
		size_t i;

		// Mostly small steps, sometimes around the two limits, sometimes very long, sometimes backwards
		if(pace < 2) now = now >= 5 ? now - 5 : 0;
		else if(pace < 3) now = 0;
		else if(pace < 4) now = now >= 700000 ? now - 700000 : 0;
		else if(pace < 6)
		{
			// the time the last request was accepted or ended, or 1 ms before it
			now = walk_random(2) != 0 ? m.accepted_at : m.ended_at;
			if(walk_random(2) != 0 && now > 0) now--;
		}
		else if(pace < 70) now += small_steps[walk_random(sizeof(small_steps) / sizeof(small_steps[0]))];
		else if(pace < 94) now += limit_steps[walk_random(sizeof(limit_steps) / sizeof(limit_steps[0]))];
		else now += huge_steps[walk_random(sizeof(huge_steps) / sizeof(huge_steps[0]))];

		if(operation < 40)
		{
			bool clear = walk_random(2) != 0;
			bool http = walk_random(2) != 0;
			uint32_t got_number = 0xDEADBEEFu, expected_number = 0;
			uint32_t seq;
			const char *got_reason, *expected_reason;
			bool with_number;
			dtc_accept_t result;

			switch(walk_random(10))
			{
				case 5:  seq = m.number ^ (1u << walk_random(32)); break;
				case 6:  seq = m.upcoming + walk_random(2); break;
				case 0:  seq = m.number + 1; break;
				case 1:  seq = m.number ^ 0x40000000u; break;
				case 2:  seq = m.number | 0x80000000u; break;
				case 3:  seq = 0; break;
				case 4:  seq = m.result_number; break;
				default: seq = m.number; break;
			}
			with_number = walk_random(4) != 0;
			result = dtc_state_try_begin(&s, clear, http ? DTC_SRC_HTTP : DTC_SRC_MQTT, seq, now, with_number ? &got_number : NULL);
			got_reason = dtc_accept_reason(result);
			expected_reason = model_begin(&m, clear, http, seq, now, &expected_number);
			same = same_text(got_reason, expected_reason) && (!with_number || got_number == expected_number) &&
			       (result == DTC_ACCEPTED) == (expected_reason == NULL);
			what = "try_begin";
		}
		else if(operation < 60)
		{
			same = dtc_state_pickup(&s, now) == model_pickup(&m, now);
			what = "pickup";
		}
		else if(operation < 72)
		{
			unsigned at = walk_random(256), of = walk_random(256);
			const char *unit = units[walk_random(sizeof(units) / sizeof(units[0]))];

			dtc_state_progress(&s, (uint8_t)at, (uint8_t)of, unit);
			model_progress(&m, at, of, unit);
			what = "progress";
		}
		else if(operation < 84)
		{
			unsigned count = walk_random(3) == 0 ? 1u << walk_random(16) : counts[walk_random(sizeof(counts) / sizeof(counts[0]))];

			dtc_state_done(&s, (uint16_t)count, now);
			model_done(&m, count, now);
			what = "done";
		}
		else if(operation < 92)
		{
			const char *reason = reasons[walk_random(sizeof(reasons) / sizeof(reasons[0]))];

			dtc_state_error(&s, reason, now);
			model_error(&m, reason, now);
			what = "error";
		}
		else if(operation < 98)
		{
			// JSON into a buffer around the exact size
			static const int around[] = {-2, -1, 0, 1, 2, 300};
			int exact = model_json(&m, supported, now, expected, sizeof(expected)) + 1;
			int wanted = exact + around[walk_random(sizeof(around) / sizeof(around[0]))];

			size = (size_t)(wanted < 0 ? 0 : wanted);
			if(walk_random(8) == 0) size = walk_random(3);
			if(size > sizeof(got)) size = sizeof(got);
			what = "json into a small buffer";
		}
		else
		{
			seed = seeds[walk_random(sizeof(seeds) / sizeof(seeds[0]))];
			dtc_state_init(&s, seed);
			model_init(&m, seed);
			what = "init";
		}

		// After every call: same answer, same busy, same JSON, and not one byte written behind the buffer
		memset(got, 0x5A, sizeof(got));
		memset(expected, 0x5A, sizeof(expected));
		got_length = dtc_state_json(&s, supported, now, got, size);
		expected_length = model_json(&m, supported, now, expected, size);
		same = same && dtc_state_busy(&s) == model_active(&m) && got_length == expected_length;
		if(size > 0) same = same && memchr(got, '\0', size) != NULL && strcmp(got, expected) == 0;
		for(i = size; i < sizeof(got); i++) same = same && got[i] == 0x5A;

		// Now and then into a buffer of 64 KiB and more: the complete text, whatever the size
		if(same && walk_random(16) == 0)
		{
			static char big[70000];
			static const size_t big_sizes[] = {65535, 65536, 65537, 69000};
			size_t big_size = big_sizes[walk_random(sizeof(big_sizes) / sizeof(big_sizes[0]))];

			expected_length = model_json(&m, supported, now, expected, sizeof(expected));
			memset(big, 0x5A, sizeof(expected));
			memset(big + big_size, 0x5A, 8);
			got_length = dtc_state_json(&s, supported, now, big, big_size);
			same = got_length == expected_length && memchr(big, '\0', sizeof(expected)) != NULL && strcmp(big, expected) == 0;
			for(i = big_size; i < big_size + 8; i++) same = same && big[i] == 0x5A;
			memcpy(got, big, sizeof(got));
			size = big_size;
			what = "json into a big buffer";
		}

		if(!same)
		{
			got[sizeof(got) - 1] = '\0';
			expected[sizeof(expected) - 1] = '\0';
			printf("  walk %lu, step %d, after %s at %llu ms, buffer %lu:\n  module %d %.600s\n  model  %d %.600s\n",
			       (unsigned long)walk_seed, step, what, (unsigned long long)now, (unsigned long)size,
			       got_length, size > 0 ? got : "", expected_length, size > 0 ? expected : "");
			return step;
		}
	}
	return -1;
}

static void test_walk_against_the_model(void)
{
	uint32_t walk_seed;
	int different = 0, status = 0;
	pid_t child;

	// In a child process: a crash of the module is then a failed check here, not the end of the test
	fflush(stdout);
	alarm(90);
	child = fork();
	if(child == 0)
	{
		// A module that never returns ends the child here; the walk itself takes a few seconds
		alarm(30);
		for(walk_seed = 1; walk_seed <= 60; walk_seed++)
		{
			if(walk(walk_seed) >= 0) different++;
		}
		fflush(stdout);
		_exit(different == 0 ? 0 : 10);
	}
	if(child < 0 || waitpid(child, &status, 0) != child) status = -1;
	check(status != -1 && WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 10),
	      "60 random walks of 4000 calls each: no crash");
	check(status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0,
	      "60 random walks of 4000 calls each: module and model agree after every call");
}

int main(void)
{
	test_walk_against_the_model();
	test_constants();
	test_init();
	test_sequence_numbers();
	test_json_of_a_run();
	test_json_limits();
	test_busy();
	test_calls_in_the_wrong_phase();
	test_clear_is_bound_to_a_read();
	test_expiry();
	test_times_above_32_bit();
	test_json_buffer_and_escaping();
	test_reasons();

	printf("%s\n", failures ? "FAILED" : "OK");
	return failures ? 1 : 0;
}
