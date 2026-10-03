/*
 * Host test for main/dtc_state.c, the rules of the fault memory scan shared by MQTT and HTTP.
 * Run in tools/w906 (the JSON output is compared with the files in fixtures/):
 *   cc -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I../../main ../../main/dtc_state.c dtc_state_test.c -o dtc_state_test && ./dtc_state_test
 * redproof.py removes or weakens every rule once and expects this test to fail.
 */
#include <stdio.h>
#include <string.h>
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

	dtc_state_try_begin(s, READ, src, false, 0, start_ms, &seq);
	dtc_state_pickup(s, start_ms);
	dtc_state_done(s, count, end_ms);
	return seq;
}

static void test_constants(void)
{
	check(DTC_CLEAR_MAX_AGE_MS == 600000u, "a read can be cleared for 600 s");
	check(DTC_HTTP_EXPIRY_MS == 20000u, "an HTTP request expires after 20 s");
	check(DTC_SEQ_MAX == 2147483647u, "sequence numbers end at 2^31-1");
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
	dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, false, 0, 40000, NULL);
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
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 0, &seq);
	check(seq == 1, "seed 0: first number is 1, never 0");

	dtc_state_init(&s, 0x80000000u);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 0, &seq);
	check(seq == 1, "seed 0x80000000: masked to 31 bit, 0 skipped");

	dtc_state_init(&s, 0x80000029u);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 0, &seq);
	check(seq == 41, "seed 0x80000029: number 41, below 2^31");

	dtc_state_init(&s, 0xFFFFFFFFu);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 0, &seq);
	check(seq == 0x7FFFFFFFu, "seed 0xFFFFFFFF: first number is 2^31-1");
	dtc_state_pickup(&s, 5);
	dtc_state_error(&s, "ecu_offline", 10);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 20, &seq);
	check(seq == 1, "after 2^31-1 the numbers continue with 1");

	dtc_state_init(&s, 7);
	check(dtc_state_try_begin(&s, READ, DTC_SRC_MQTT, false, 0, 0, NULL) == DTC_ACCEPTED && s.seq == 7,
	      "request without seq_out");
}

static void test_json_of_a_run(void)
{
	dtc_state_t s;
	uint32_t seq = 0;

	dtc_state_init(&s, 41);
	check(json_is_fixture(&s, true, 500, "dtc_state_idle.json"), "JSON idle");
	check(json_is_fixture(&s, false, 500, "dtc_state_unsupported.json"), "JSON profile without fault memory");
	check(!dtc_state_busy(&s), "idle is not busy");

	check(dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 1000, &seq) == DTC_ACCEPTED && seq == 41,
	      "read accepted in idle");
	check(s.phase == DTC_STATE_QUEUED && dtc_state_busy(&s), "accepted request is queued and busy");
	check(json_is_fixture(&s, true, 1500, "dtc_state_queued.json"), "JSON queued");

	check(dtc_state_pickup(&s, 1200) && s.phase == DTC_STATE_RUNNING, "pickup starts the scan");
	dtc_state_progress(&s, 5, 18, "N30/4 ESP");
	check(json_is_fixture(&s, true, 11000, "dtc_state_running.json"), "JSON running");
	dtc_state_progress(&s, 17, 18, "N2/14 Rückhaltesystem (SRS)");
	check(json_is_fixture(&s, true, 33000, "dtc_state_running_umlaut.json"), "JSON running, UTF-8 name unchanged");

	dtc_state_progress(&s, 18, 18, "N69/1 Fahrertür (TSG)");
	dtc_state_done(&s, 3, 36000);
	check(s.phase == DTC_STATE_DONE && !dtc_state_busy(&s), "done is not busy");
	check(json_is_fixture(&s, true, 48500, "dtc_state_done.json"), "JSON done, age in whole seconds");
	check(json_is(&s, true, 35000, "{\"supported\":true,\"state\":\"done\",\"action\":\"read\",\"src\":\"http\","
	      "\"seq\":41,\"ecu\":18,\"total\":18,\"name\":\"\",\"reason\":\"\",\"age_s\":0,\"count\":3,\"result_seq\":41}"),
	      "JSON done with a clock 1 s behind: age 0, not 49 days");

	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, false, 0, 50000, &seq) == DTC_ACCEPTED && seq == 42,
	      "next request gets the next number");
	check(json_is(&s, true, 50500, "{\"supported\":true,\"state\":\"queued\",\"action\":\"clear\",\"src\":\"mqtt\","
	      "\"seq\":42,\"ecu\":0,\"total\":0,\"name\":\"\",\"reason\":\"\",\"age_s\":0,\"count\":3,\"result_seq\":41}"),
	      "JSON queued after a result: no age, progress reset, result kept");
	dtc_state_pickup(&s, 50100);
	dtc_state_progress(&s, 0, 18, NULL);
	dtc_state_error(&s, "engine_running", 51000);
	check(json_is_fixture(&s, true, 54999, "dtc_state_error.json"), "JSON error, earlier result still referenced");

	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 60000, &seq);
	check(json_is(&s, true, 60000, "{\"supported\":true,\"state\":\"queued\",\"action\":\"read\",\"src\":\"http\","
	      "\"seq\":43,\"ecu\":0,\"total\":0,\"name\":\"\",\"reason\":\"\",\"age_s\":0,\"count\":3,\"result_seq\":41}"),
	      "a new request clears the reason of the last error");
}

static void test_json_limits(void)
{
	dtc_state_t s;

	dtc_state_init(&s, 0xFFFFFFFFu);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 1000, NULL);
	dtc_state_pickup(&s, 1000);
	dtc_state_progress(&s, 255, 255, "N10 SAM");
	dtc_state_done(&s, 65535, 5000);
	check(json_is_fixture(&s, true, 5000 + 2147483647999ull, "dtc_state_limits.json"),
	      "JSON with every number at its limit");
}

static void test_busy(void)
{
	dtc_state_t s, before;
	uint32_t seq;

	dtc_state_init(&s, 7);
	dtc_state_try_begin(&s, READ, DTC_SRC_MQTT, false, 0, 100, NULL);

	before = s;
	seq = 0xDEADBEEFu;
	check(dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 200, &seq) == DTC_REJECT_BUSY, "queued: HTTP read is busy");
	check(seq == 7, "a busy answer carries the number of the request that is in the way");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, false, 0, 200, NULL) == DTC_REJECT_BUSY, "queued: MQTT clear is busy");
	check(same_state(&s, &before), "queued: a rejected request changes nothing");

	dtc_state_pickup(&s, 300);
	before = s;
	check(dtc_state_try_begin(&s, READ, DTC_SRC_MQTT, false, 0, 400, NULL) == DTC_REJECT_BUSY, "running: MQTT read is busy");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, 7, 400, NULL) == DTC_REJECT_BUSY, "running: HTTP clear is busy");
	check(same_state(&s, &before), "running: a rejected request changes nothing");
}

static void test_calls_in_the_wrong_phase(void)
{
	dtc_state_t s, before;

	dtc_state_init(&s, 7);
	before = s;
	check(!dtc_state_pickup(&s, 10) && same_state(&s, &before), "idle: nothing to pick up");
	dtc_state_error(&s, "internal", 10);
	check(same_state(&s, &before), "idle: error is ignored");
	dtc_state_done(&s, 5, 10);
	check(same_state(&s, &before), "idle: done is ignored");
	dtc_state_progress(&s, 3, 18, "N10 SAM");
	check(same_state(&s, &before), "idle: progress is ignored");

	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 100, NULL);
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
	check(s.phase == DTC_STATE_ERROR && same_text(s.reason, "ecu_offline") && s.finished_ms == 400 && s.name == NULL,
	      "running: an error ends the request and drops the control unit name");
	before = s;
	check(!dtc_state_pickup(&s, 500) && same_state(&s, &before), "error: the failed request is not picked up again");
	dtc_state_error(&s, "internal", 500);
	check(same_state(&s, &before), "error: a second error keeps the first reason");
	dtc_state_done(&s, 2, 500);
	check(same_state(&s, &before), "error: done does not turn the error into a result");
	dtc_state_progress(&s, 4, 18, "N10 SAM");
	check(same_state(&s, &before), "error: progress is ignored");

	dtc_state_init(&s, 7);
	read_done(&s, DTC_SRC_HTTP, 2, 100, 35000);
	before = s;
	dtc_state_error(&s, "internal", 36000);
	check(same_state(&s, &before), "done: a later error does not overwrite the result");
	dtc_state_done(&s, 9, 36000);
	check(same_state(&s, &before), "done: a second done is ignored");
	check(!dtc_state_pickup(&s, 36000) && same_state(&s, &before), "done: nothing to pick up");
	dtc_state_progress(&s, 4, 18, "N10 SAM");
	check(same_state(&s, &before), "done: progress is ignored");
}

static void test_clear_is_bound_to_a_read(void)
{
	dtc_state_t s, t, before;
	uint32_t seq, other_seq = 0, clear_seq = 0;

	dtc_state_init(&s, 100);
	before = s;
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, 0, 1000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "clear without any read is refused");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, false, 0, 1000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "an HTTP clear is bound even if the caller does not ask for it");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, true, 0, 1000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "an MQTT clear is bound if the caller asks for it");
	check(same_state(&s, &before), "refused clear changes nothing");
	t = s;
	check(dtc_state_try_begin(&t, READ, DTC_SRC_HTTP, true, 0, 1000, NULL) == DTC_ACCEPTED,
	      "a read is never bound");

	seq = read_done(&s, DTC_SRC_HTTP, 3, 5000, 40000);
	before = s;
	other_seq = 0xDEADBEEFu;
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq + 1, 41000, &other_seq) == DTC_REJECT_STALE_SEQ,
	      "clear with another number is refused");
	check(other_seq == seq, "a stale answer carries the number of the last read");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq ^ 0x40000000u, 41000, NULL) == DTC_REJECT_STALE_SEQ,
	      "clear with a number that differs in bit 30 only is refused");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, 0, 41000, NULL) == DTC_REJECT_STALE_SEQ,
	      "clear with number 0 is refused");
	check(same_state(&s, &before), "stale clear changes nothing");

	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_HTTP, true, seq, 40000 + 600001, NULL) == DTC_REJECT_READ_REQUIRED,
	      "clear 600.001 s after the read is refused");
	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_HTTP, true, seq, 40000 + 600000, NULL) == DTC_ACCEPTED,
	      "clear exactly 600 s after the read is accepted");
	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_HTTP, true, seq, 40000 + 4294967296ull + 5000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "clear 2^32 ms + 5 s after the read is refused");
	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_HTTP, true, seq, 39995, NULL) == DTC_ACCEPTED,
	      "clear with a clock 5 ms behind the end of the read is accepted");
	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_HTTP, true, seq + 1, 40000 + 600001, NULL) == DTC_REJECT_READ_REQUIRED,
	      "too old and another number: the reason is read_required");

	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 45000, &clear_seq) == DTC_ACCEPTED &&
	      clear_seq == seq + 1 && s.clear && s.src == DTC_SRC_HTTP, "clear with the number of the read is accepted");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 45100, NULL) == DTC_REJECT_BUSY,
	      "the same clear sent twice is busy");
	dtc_state_pickup(&s, 45200);
	dtc_state_progress(&s, 18, 18, "N69/1 Fahrertür (TSG)");
	dtc_state_done(&s, 1, 80000);
	check(s.result_seq == clear_seq && s.result_count == 1, "the result of a clear replaces the result of the read");
	check(json_is_fixture(&s, true, 81000, "dtc_state_done_clear.json"), "JSON done after a clear");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, clear_seq, 81000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "after a clear its own number cannot clear again");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 81000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "after a clear the number of the old read is used up");

	dtc_state_init(&s, 150);
	seq = read_done(&s, DTC_SRC_HTTP, 1, 0, 35000);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 36000, NULL) == DTC_ACCEPTED,
	      "a single trouble code can be cleared");

	dtc_state_init(&s, 200);
	seq = read_done(&s, DTC_SRC_MQTT, 3, 0, 35000);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 36000, &other_seq);
	dtc_state_pickup(&s, 36100);
	dtc_state_error(&s, "ecu_offline", 36500);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, other_seq, 37000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "a failed read cannot be cleared although an older result is stored");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 37000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "the read before a failed read cannot be cleared either");

	dtc_state_init(&s, 250);
	dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, false, 0, 0, &seq);
	dtc_state_pickup(&s, 100);
	dtc_state_done(&s, 2, 35000);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 36000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "after an MQTT clear with codes left an HTTP clear with its number is refused");

	dtc_state_init(&s, 300);
	seq = read_done(&s, DTC_SRC_HTTP, 0, 0, 35000);
	before = s;
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 36000, NULL) == DTC_REJECT_NOTHING_TO_CLEAR,
	      "clear after a read without trouble codes is refused");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq + 1, 36000, NULL) == DTC_REJECT_STALE_SEQ,
	      "another number and no trouble codes: the reason is stale_seq");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 35000 + 600001, NULL) == DTC_REJECT_READ_REQUIRED,
	      "too old and no trouble codes: the reason is read_required");
	check(same_state(&s, &before), "refused clear of an empty list changes nothing");

	dtc_state_init(&s, 400);
	seq = read_done(&s, DTC_SRC_MQTT, 2, 0, 35000);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq + 1, 36000, NULL) == DTC_REJECT_STALE_SEQ,
	      "read over MQTT: HTTP clear with another number is refused");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 35000 + 600001, NULL) == DTC_REJECT_READ_REQUIRED,
	      "read over MQTT: HTTP clear 600.001 s later is refused");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 36000, NULL) == DTC_ACCEPTED,
	      "read over MQTT: HTTP clear with its number is accepted");

	dtc_state_init(&s, 450);
	seq = read_done(&s, DTC_SRC_MQTT, 0, 0, 35000);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 36000, NULL) == DTC_REJECT_NOTHING_TO_CLEAR,
	      "read over MQTT without trouble codes: HTTP clear is refused");

	dtc_state_init(&s, 500);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, false, 0, 0, NULL) == DTC_ACCEPTED,
	      "MQTT clear stays unbound");
}

static void test_expiry(void)
{
	dtc_state_t s, t;
	uint32_t seq;

	dtc_state_init(&s, 700);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 1000, NULL);
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
	dtc_state_try_begin(&s, READ, DTC_SRC_MQTT, false, 0, 1000, NULL);
	check(dtc_state_pickup(&s, 1000 + 3600000u) && s.phase == DTC_STATE_RUNNING,
	      "MQTT request does not expire, as before");

	dtc_state_init(&s, 800);
	seq = read_done(&s, DTC_SRC_HTTP, 3, 0, 35000);
	dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 36000, NULL);
	check(!dtc_state_pickup(&s, 36000 + 20001), "expired clear does not run");
	check(s.result_seq == seq && s.result_count == 3, "an expired request keeps the stored result");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 60000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "an expired clear has used up the read");
}

static void test_json_buffer_and_escaping(void)
{
	dtc_state_t s;
	char json[512];
	char small[512];
	int length;

	dtc_state_init(&s, 41);
	length = dtc_state_json(&s, true, 0, json, sizeof(json));
	check(length > 0 && length == (int)strlen(json), "JSON returns its length");

	memset(small, 'x', sizeof(small));
	check(dtc_state_json(&s, true, 0, small, (size_t)length + 1) == length && small[length] == '\0' &&
	      strcmp(small, json) == 0, "JSON fits exactly with one byte for the terminating zero");
	memset(small, 'x', sizeof(small));
	check(dtc_state_json(&s, true, 0, small, (size_t)length) == -1 && small[0] == '\0',
	      "JSON one byte too long: -1 and an empty string, not a truncated object");
	check(dtc_state_json(&s, true, 0, small, 1) == -1 && small[0] == '\0', "JSON into a 1 byte buffer");
	small[0] = 'x';
	check(dtc_state_json(&s, true, 0, small, 0) == -1 && small[0] == 'x', "JSON into a 0 byte buffer writes nothing");

	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 0, NULL);
	dtc_state_pickup(&s, 0);
	dtc_state_progress(&s, 1, 18, "a\"b\\c\td\ne\001f\037g\rh i\177j");
	dtc_state_json(&s, true, 0, json, sizeof(json));
	check(strstr(json, "\"name\":\"a\\\"b\\\\cdefgh i\177j\",") != NULL,
	      "name: quote and backslash escaped, 0x01..0x1F dropped, space and 0x7F kept");

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

int main(void)
{
	test_constants();
	test_init();
	test_sequence_numbers();
	test_json_of_a_run();
	test_json_limits();
	test_busy();
	test_calls_in_the_wrong_phase();
	test_clear_is_bound_to_a_read();
	test_expiry();
	test_json_buffer_and_escaping();
	test_reasons();

	printf("%s\n", failures ? "FAILED" : "OK");
	return failures ? 1 : 0;
}
