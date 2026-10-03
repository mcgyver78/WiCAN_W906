/*
 * Host test for main/dtc_state.c, the rules of the fault memory scan shared by MQTT and HTTP.
 * Run in tools/w906 (the JSON output is compared with the files in fixtures/):
 *   cc -Wall -Wextra -Werror -fsanitize=address,undefined -I../../main ../../main/dtc_state.c dtc_state_test.c -o dtc_state_test && ./dtc_state_test
 * redproof.py removes every rule once and expects this test to fail.
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

static int json_is(const dtc_state_t *s, bool supported, uint32_t now_ms, const char *expected)
{
	char json[512];
	int length = dtc_state_json(s, supported, now_ms, json, sizeof(json));

	if(length == (int)strlen(expected) && strcmp(json, expected) == 0) return 1;

	printf("  got      %s\n  expected %s\n", json, expected);
	return 0;
}

static int json_is_fixture(const dtc_state_t *s, bool supported, uint32_t now_ms, const char *name)
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
static uint32_t read_done(dtc_state_t *s, dtc_src_t src, uint16_t count, uint32_t start_ms, uint32_t end_ms)
{
	uint32_t seq = 0;

	dtc_state_try_begin(s, READ, src, false, 0, start_ms, &seq);
	dtc_state_pickup(s, start_ms);
	dtc_state_done(s, count, end_ms);
	return seq;
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

	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, false, 0, 50000, &seq) == DTC_ACCEPTED && seq == 42,
	      "next request gets the next number");
	check(json_is(&s, true, 50500, "{\"supported\":true,\"state\":\"queued\",\"action\":\"clear\",\"src\":\"mqtt\","
	      "\"seq\":42,\"ecu\":0,\"total\":18,\"name\":\"\",\"reason\":\"\",\"age_s\":0,\"count\":3,\"result_seq\":41}"),
	      "JSON queued after a result: no age, step reset, result kept");
	dtc_state_pickup(&s, 50100);
	dtc_state_error(&s, "engine_running", 51000);
	check(json_is_fixture(&s, true, 54999, "dtc_state_error.json"), "JSON error, earlier result still referenced");

	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 60000, &seq);
	check(json_is(&s, true, 60000, "{\"supported\":true,\"state\":\"queued\",\"action\":\"read\",\"src\":\"http\","
	      "\"seq\":43,\"ecu\":0,\"total\":18,\"name\":\"\",\"reason\":\"\",\"age_s\":0,\"count\":3,\"result_seq\":41}"),
	      "a new request clears the reason of the last error");
}

static void test_busy(void)
{
	dtc_state_t s, before;

	dtc_state_init(&s, 7);
	dtc_state_try_begin(&s, READ, DTC_SRC_MQTT, false, 0, 100, NULL);

	before = s;
	check(dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 200, NULL) == DTC_REJECT_BUSY, "queued: HTTP read is busy");
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
	dtc_state_error(&s, "busy", 10);
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
	check(s.phase == DTC_STATE_ERROR && same_text(s.reason, "internal") && s.finished_ms == 120,
	      "queued: an error ends the request");

	dtc_state_init(&s, 7);
	read_done(&s, DTC_SRC_HTTP, 2, 100, 35000);
	before = s;
	dtc_state_error(&s, "busy", 36000);
	check(same_state(&s, &before), "done: a later error does not overwrite the result");
	dtc_state_done(&s, 9, 36000);
	check(same_state(&s, &before), "done: a second done is ignored");
	check(!dtc_state_pickup(&s, 36000) && same_state(&s, &before), "done: nothing to pick up");
}

static void test_clear_is_bound_to_a_read(void)
{
	dtc_state_t s, t, before;
	uint32_t seq, failed_seq = 0, clear_seq = 0;

	dtc_state_init(&s, 100);
	before = s;
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, 0, 1000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "clear without any read is refused");
	check(same_state(&s, &before), "refused clear changes nothing");

	seq = read_done(&s, DTC_SRC_HTTP, 3, 5000, 40000);
	before = s;
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq + 1, 41000, NULL) == DTC_REJECT_STALE_SEQ,
	      "clear with another number is refused");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, 0, 41000, NULL) == DTC_REJECT_STALE_SEQ,
	      "clear with number 0 is refused");
	check(same_state(&s, &before), "stale clear changes nothing");

	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_HTTP, true, seq, 40000 + DTC_CLEAR_MAX_AGE_MS + 1, NULL) == DTC_REJECT_READ_REQUIRED,
	      "clear 600.001 s after the read is refused");
	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_HTTP, true, seq, 40000 + DTC_CLEAR_MAX_AGE_MS, NULL) == DTC_ACCEPTED,
	      "clear exactly 600 s after the read is accepted");

	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 45000, &clear_seq) == DTC_ACCEPTED &&
	      clear_seq == seq + 1 && s.clear && s.src == DTC_SRC_HTTP, "clear with the number of the read is accepted");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 45100, NULL) == DTC_REJECT_BUSY,
	      "the same clear sent twice is busy");
	dtc_state_pickup(&s, 45200);
	dtc_state_done(&s, 1, 80000);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, clear_seq, 81000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "after a clear its own number cannot clear again");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 81000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "after a clear the number of the old read is used up");

	dtc_state_init(&s, 200);
	seq = read_done(&s, DTC_SRC_MQTT, 3, 0, 35000);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 36000, &failed_seq);
	dtc_state_pickup(&s, 36100);
	dtc_state_error(&s, "ecu_offline", 36500);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, failed_seq, 37000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "a failed read cannot be cleared although an older result is stored");
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 37000, NULL) == DTC_REJECT_READ_REQUIRED,
	      "the read before a failed read cannot be cleared either");

	dtc_state_init(&s, 300);
	seq = read_done(&s, DTC_SRC_HTTP, 0, 0, 35000);
	before = s;
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 36000, NULL) == DTC_REJECT_NOTHING_TO_CLEAR,
	      "clear after a read without trouble codes is refused");
	check(same_state(&s, &before), "refused clear of an empty list changes nothing");

	dtc_state_init(&s, 400);
	seq = read_done(&s, DTC_SRC_MQTT, 2, 0, 35000);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 36000, NULL) == DTC_ACCEPTED,
	      "a read started over MQTT can be cleared over HTTP with its number");

	dtc_state_init(&s, 500);
	check(dtc_state_try_begin(&s, CLEAR, DTC_SRC_MQTT, false, 0, 0, NULL) == DTC_ACCEPTED,
	      "MQTT clear stays unbound");

	dtc_state_init(&s, 600);
	seq = read_done(&s, DTC_SRC_HTTP, 2, 0xFFFFFF00u - 35000, 0xFFFFFF00u);
	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_HTTP, true, seq, 0x00000100u, NULL) == DTC_ACCEPTED,
	      "clock wrap between read and clear: 512 ms later is accepted");
	t = s;
	check(dtc_state_try_begin(&t, CLEAR, DTC_SRC_HTTP, true, seq, 0xFFFFFF00u + DTC_CLEAR_MAX_AGE_MS + 1, NULL) == DTC_REJECT_READ_REQUIRED,
	      "clock wrap between read and clear: too old is refused");
}

static void test_expiry(void)
{
	dtc_state_t s, t;
	uint32_t seq;
	char json[512];

	dtc_state_init(&s, 700);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 1000, NULL);
	t = s;
	check(dtc_state_pickup(&t, 1000 + DTC_HTTP_EXPIRY_MS) && t.phase == DTC_STATE_RUNNING,
	      "HTTP request picked up after exactly 20 s runs");
	t = s;
	check(!dtc_state_pickup(&t, 1000 + DTC_HTTP_EXPIRY_MS + 1) && t.phase == DTC_STATE_ERROR &&
	      same_text(t.reason, "expired") && t.finished_ms == 1000 + DTC_HTTP_EXPIRY_MS + 1,
	      "HTTP request picked up after 20.001 s expires and does not run");
	dtc_state_json(&t, true, 30000, json, sizeof(json));
	check(strstr(json, "\"state\":\"error\"") != NULL && strstr(json, "\"reason\":\"expired\"") != NULL,
	      "JSON of an expired request");

	dtc_state_init(&s, 700);
	dtc_state_try_begin(&s, READ, DTC_SRC_MQTT, false, 0, 1000, NULL);
	check(dtc_state_pickup(&s, 1000 + 3600000u) && s.phase == DTC_STATE_RUNNING,
	      "MQTT request does not expire, as before");

	dtc_state_init(&s, 700);
	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 0xFFFFFFF0u, NULL);
	check(dtc_state_pickup(&s, 0x00000010u), "clock wrap between request and pickup: 32 ms later runs");

	dtc_state_init(&s, 800);
	seq = read_done(&s, DTC_SRC_HTTP, 3, 0, 35000);
	dtc_state_try_begin(&s, CLEAR, DTC_SRC_HTTP, true, seq, 36000, NULL);
	check(!dtc_state_pickup(&s, 36000 + DTC_HTTP_EXPIRY_MS + 1), "expired clear does not run");
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
	check(dtc_state_json(&s, true, 0, small, (size_t)length + 1) == length && strcmp(small, json) == 0,
	      "JSON fits exactly with one byte for the terminating zero");
	memset(small, 'x', sizeof(small));
	check(dtc_state_json(&s, true, 0, small, (size_t)length) == -1 && small[0] == '\0',
	      "JSON one byte too long: -1 and an empty string, not a truncated object");
	check(dtc_state_json(&s, true, 0, small, 1) == -1 && small[0] == '\0', "JSON into a 1 byte buffer");
	small[0] = 'x';
	check(dtc_state_json(&s, true, 0, small, 0) == -1 && small[0] == 'x', "JSON into a 0 byte buffer writes nothing");

	dtc_state_try_begin(&s, READ, DTC_SRC_HTTP, false, 0, 0, NULL);
	dtc_state_pickup(&s, 0);
	dtc_state_progress(&s, 1, 18, "a\"b\\c\td\n");
	dtc_state_json(&s, true, 0, json, sizeof(json));
	check(strstr(json, "\"name\":\"a\\\"b\\\\cd\",") != NULL,
	      "quote and backslash are escaped, control characters dropped");
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
	test_sequence_numbers();
	test_json_of_a_run();
	test_busy();
	test_calls_in_the_wrong_phase();
	test_clear_is_bound_to_a_read();
	test_expiry();
	test_json_buffer_and_escaping();
	test_reasons();

	printf("%s\n", failures ? "FAILED" : "OK");
	return failures ? 1 : 0;
}
