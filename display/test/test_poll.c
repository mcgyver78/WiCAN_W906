/*
 * Host test for display/components/core/poll.c. Run "make test_poll && ./test_poll" in display/test.
 * redproof.py removes or weakens every rule once (mutations/poll.py) and expects this test to fail.
 *
 * The scenes play against a small adapter inside this test: it answers every poll_request_t the display
 * hands out from its own state, with the rules of tools/w906/API.md written once more (which request is
 * accepted, how a scan goes on, what is stored). The display asks it once a second (second()), as the real
 * caller does. Most scenes start in setup(): the display is bound to its adapter, which runs since 100 s
 * with the ignition on and the engine off; the display joins the network at 100000 and setup() ends at
 * 102000, after the third round. The own read gets number 42 and ends 3300 ms after it was accepted, the
 * own clear gets number 43.
 *
 * The requests of a scene are noted as letters, one group per second:
 *   S GET /api/state   R GET /api/dtc/result   C GET /load_car_config   V GET /autopid_data
 *   r POST read        c POST clear
 */
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <unistd.h>
#include <sys/wait.h>
#include "test.h"
#include "poll.h"

#define OWN         "a1b2c3d4e5f6"
#define OTHER       "0123456789ab"
#define BOOT        1234567890u

// The adapter of the test. Invented, as in mock_wican.py: the durations.
#define PICKUP_MS   300u    // an accepted request waits this long for the AutoPID task
#define SCAN_MS     3000u   // from there to the end of the scan
#define PASS_MS     400u    // one polling pass

#define CONFIG_RPM      "{\"ENGINE_RPM\":{\"class\":\"frequency\",\"unit\":\"RPM\"},\"COOLANT_TMP\":{\"class\":\"temperature\",\"unit\":\"°C\"},\"FUEL_L\":{\"class\":\"none\",\"unit\":\"L\"}}"
#define CONFIG_NO_RPM   "{\"COOLANT_TMP\":{\"class\":\"temperature\",\"unit\":\"°C\"},\"FUEL_L\":{\"class\":\"none\",\"unit\":\"L\"}}"
#define NOT_FOUND_TEXT  "Nothing matches the given URI"
#define SHORTEST_RESULT "{\"state\":\"done\",\"action\":\"read\",\"duration_ms\":1,\"dtc_count\":0,\"ecus\":[]}"

static const char LETTERS[] = "-SRCVrc";

typedef struct
{
	char id[33];
	bool api;               // false: a firmware without the API, 404 for every path below /api/
	bool dead;              // no answer to anything
	uint32_t boot;
	uint64_t boot_ms;       // the time of the display at which the adapter started
	wican_autopid_t autopid;
	bool supported;
	bool ignition;
	int rpm;
	bool has_rpm;           // the profile has ENGINE_RPM
	int32_t batt_mv;        // a multiple of 100, or -1
	int32_t sleep_in_s;
	uint32_t memory;        // trouble codes the vehicle has stored, 0 to 2
	uint32_t pickup_ms;
	uint32_t pass_time;     // one polling pass takes this long, in ms
	const char *read_text;  // the result of a read as a scene wants it, NULL: made from `memory`
	const char *clear_text;
	const char *config_text;    // the profile as a scene wants it, NULL: by has_rpm

	uint32_t pass;
	uint64_t pass_ms;       // when the counter moved last
	bool valid;             // a polling pass has ended since the ignition came on

	wican_dtc_phase_t phase;
	bool clear;
	bool http;
	uint32_t seq, next_seq;
	uint64_t accepted_ms, ended_ms;
	char reason[32];
	uint32_t result_seq, result_count;
	bool result_clear;
	char result[POLL_TEXT_SIZE];
} adapter_t;

// An answer, and what it says: known from how it was made, not from reading it
typedef struct
{
	int status;             // 0: none
	const char *body;       // zero terminated, NULL: none
	size_t length;
	const char *seq_header; // value of X-DTC-Seq, NULL: not sent

	bool is_state;          // the body is a state as API.md describes it
	wican_state_t state;
	bool is_result;         // the body is a finished result
	bool result_clear;
	uint32_t result_count;
	bool names;             // the header is the number `number` in decimal digits and nothing else
	uint32_t number;
	uint32_t post_seq;      // "seq" and "reason" of the body of a POST, 0 and empty if it has none
	char post_reason[32];
} answer_t;

static poll_t poll;
static json_token_t work[POLL_TOKENS];
static adapter_t wican;
static uint64_t now;
static poll_request_t request;      // the last one the display handed out
static char trace[512];

static char body_room[POLL_BODY_SIZE + 8192];
static char header_room[24];

/* ---------------------------------------------------------------------------------------------------
 * The adapter
 */

static void adapter_init(adapter_t *a, uint64_t boot_ms)
{
	memset(a, 0, sizeof(*a));
	strcpy(a->id, OWN);
	a->api = true;
	a->boot = BOOT;
	a->boot_ms = boot_ms;
	a->autopid = WICAN_AUTOPID_RUN;
	a->supported = true;
	a->ignition = true;
	a->has_rpm = true;
	a->batt_mv = 12400;
	a->sleep_in_s = -1;
	a->memory = 2;
	a->pickup_ms = PICKUP_MS;
	a->pass_time = PASS_MS;
	a->pass_ms = boot_ms;
	a->next_seq = 42;
}

// The adapter restarts: new boot number, the numbers of the requests begin anew, the result is gone
static void adapter_restart(adapter_t *a, uint32_t boot, uint32_t first_seq, uint64_t now_ms)
{
	a->boot = boot;
	a->boot_ms = now_ms;
	a->pass = 0;
	a->pass_ms = now_ms;
	a->valid = false;
	a->phase = WICAN_DTC_IDLE;
	a->clear = false;
	a->http = false;
	a->seq = 0;
	a->next_seq = first_seq;
	a->reason[0] = '\0';
	a->result_seq = 0;
	a->result_count = 0;
	a->result_clear = false;
}

static void adapter_end(adapter_t *a, wican_dtc_phase_t phase, const char *reason, uint64_t at_ms)
{
	a->phase = phase;
	strcpy(a->reason, reason);
	a->ended_ms = at_ms;
}

static void adapter_result(adapter_t *a)
{
	const char *text = a->clear ? a->clear_text : a->read_text;
	int length;
	uint32_t i;

	a->result_seq = a->seq;
	a->result_clear = a->clear;
	a->result_count = a->memory;
	if(text != NULL)
	{
		strcpy(a->result, text);
		return;
	}
	// The duration differs from scan to scan: no two results are the same text
	length = snprintf(a->result, sizeof(a->result), "{\"state\":\"done\",\"action\":\"%s\",\"duration_ms\":%u,\"dtc_count\":%u,\"ecus\":[{\"name\":\"N10 SAM\","
	                  "\"id\":\"662\",\"protocol\":\"KWP\",%s\"status\":\"ok\",\"dtcs\":[", a->clear ? "clear" : "read", (unsigned)(30000 + a->seq % 10000),
	                  (unsigned)a->memory, a->clear ? "\"cleared\":true," : "");
	for(i = 0; i < a->memory; i++)
	{
		length += snprintf(a->result + length, sizeof(a->result) - (size_t)length, "%s{\"code\":\"930%u\",\"status\":\"60\"}", i > 0 ? "," : "", (unsigned)(i + 1));
	}
	snprintf(a->result + length, sizeof(a->result) - (size_t)length, "]}]}");
}

// What happened in the adapter until now
static void adapter_catch_up(adapter_t *a, uint64_t now_ms)
{
	uint64_t picked = a->accepted_ms + a->pickup_ms;

	if(a->phase == WICAN_DTC_QUEUED && now_ms >= picked)
	{
		if(a->http && a->pickup_ms > 20000) adapter_end(a, WICAN_DTC_ERROR, "expired", picked);
		else if(!a->supported) adapter_end(a, WICAN_DTC_ERROR, "not_supported", picked);
		else a->phase = WICAN_DTC_RUNNING;
	}
	if(a->phase == WICAN_DTC_RUNNING)
	{
		if(!a->ignition) adapter_end(a, WICAN_DTC_ERROR, "ecu_offline", now_ms);
		else if(a->clear && a->rpm >= 50) adapter_end(a, WICAN_DTC_ERROR, "engine_running", now_ms);
		else if(now_ms >= picked + SCAN_MS)
		{
			// One code is back at once after a clear: its cause is still there
			if(a->clear && a->memory > 1) a->memory = 1;
			adapter_end(a, WICAN_DTC_DONE, "", picked + SCAN_MS);
			adapter_result(a);
		}
	}

	// A scan pauses the polling, and so does the ignition
	if(a->autopid != WICAN_AUTOPID_RUN || !a->ignition)
	{
		a->valid = false;
		a->pass_ms = now_ms;
	}
	else if(a->phase == WICAN_DTC_RUNNING || now_ms < a->pass_ms)
	{
		a->pass_ms = now_ms;
	}
	else if(now_ms - a->pass_ms >= a->pass_time)
	{
		uint64_t passes = (now_ms - a->pass_ms) / a->pass_time;

		a->pass += (uint32_t)passes;
		a->pass_ms += passes * a->pass_time;
		a->valid = true;
	}
}

// A request for a scan with the rules of main/dtc_state.h. Returns the status of API.md.
static int adapter_request(adapter_t *a, bool clear, bool http, uint32_t seq, uint64_t now_ms, uint32_t *number, const char **reason)
{
	*number = a->seq;
	if(http && (a->autopid != WICAN_AUTOPID_RUN || a->sleep_in_s == 0))
	{
		*number = 0;
		*reason = "not_ready";
		return 503;
	}
	*reason = NULL;
	if(a->phase == WICAN_DTC_QUEUED || a->phase == WICAN_DTC_RUNNING) *reason = "busy";
	else if(clear && http)
	{
		if(a->phase != WICAN_DTC_DONE || a->clear || now_ms - a->ended_ms > 600000) *reason = "read_required";
		else if(seq != a->seq) *reason = "stale_seq";
		else if(a->result_count == 0) *reason = "nothing_to_clear";
	}
	if(*reason != NULL) return 409;

	a->seq = a->next_seq;
	a->next_seq = a->next_seq >= 0x7FFFFFFFu ? 1 : a->next_seq + 1;
	a->phase = WICAN_DTC_QUEUED;
	a->clear = clear;
	a->http = http;
	a->reason[0] = '\0';
	a->accepted_ms = now_ms;
	*number = a->seq;
	return 202;
}

// The state as the adapter knows it
static void adapter_state(const adapter_t *a, uint64_t now_ms, wican_state_t *state)
{
	static const char *const names[] = {"N3/28 Motorelektronik (CDID3)", "N2/14 Rückhaltesystem (SRS)", "N10 SAM"};

	memset(state, 0, sizeof(*state));
	strcpy(state->id, a->id);
	strcpy(state->fw, "4.21");
	strcpy(state->git, "w906-v1.4.0-9-g0123abc");
	state->boot = a->boot;
	state->up_s = (uint32_t)((now_ms - a->boot_ms) / 1000);
	state->autopid = a->autopid;
	state->pids = a->autopid == WICAN_AUTOPID_OFF ? 0 : a->has_rpm ? 3 : 2;
	state->ecu_online = a->ignition && a->autopid == WICAN_AUTOPID_RUN;
	state->pass = a->pass;
	state->rx_age_ms = a->valid ? 140 : -1;
	state->mqtt = WICAN_MQTT_CONNECTED;
	state->batt_mv = a->batt_mv;
	state->sleep_in_s = a->sleep_in_s;
	state->heap = 61000;
	state->heap_min = 48000;
	state->dtc.supported = a->supported && a->autopid != WICAN_AUTOPID_OFF;
	state->dtc.phase = a->phase;
	state->dtc.has_request = a->phase != WICAN_DTC_IDLE;
	state->dtc.clear = a->clear;
	state->dtc.from_http = a->http;
	state->dtc.seq = a->seq;
	if(a->phase == WICAN_DTC_RUNNING)
	{
		uint64_t run_ms = now_ms - (a->accepted_ms + a->pickup_ms);

		// Step 0 is the engine check
		state->dtc.total = 3;
		state->dtc.step = (uint32_t)(run_ms * 4 / SCAN_MS);
		if(state->dtc.step > 3) state->dtc.step = 3;
		if(state->dtc.step > 0) strcpy(state->dtc.name, names[state->dtc.step - 1]);
	}
	if(a->phase == WICAN_DTC_DONE || a->phase == WICAN_DTC_ERROR)
	{
		state->dtc.total = 3;
		state->dtc.step = a->phase == WICAN_DTC_DONE ? 3 : 0;
		state->dtc.age_s = (uint32_t)((now_ms - a->ended_ms) / 1000);
	}
	strcpy(state->dtc.reason, a->reason);
	state->dtc.count = a->result_count;
	state->dtc.result_seq = a->result_seq;
}

// The state as the text of API.md
static size_t state_json(const wican_state_t *s, char *out, size_t size)
{
	static const char *const autopid[] = {"off", "starting", "run"};
	static const char *const phases[] = {"idle", "queued", "running", "done", "error"};
	char volts[24];

	if(s->batt_mv < 0) strcpy(volts, "-1");
	else snprintf(volts, sizeof(volts), "%d.%d", (int)(s->batt_mv / 1000), (int)(s->batt_mv % 1000 / 100));
	return (size_t)snprintf(out, size, "{\"api\":1,\"id\":\"%s\",\"fw\":\"%s\",\"git\":\"%s\",\"boot\":%" PRIu32 ",\"up\":%" PRIu32 ",\"autopid\":\"%s\","
	                        "\"pids\":%" PRIu32 ",\"ecu\":\"%s\",\"pass\":%" PRIu32 ",\"rx_age_ms\":%" PRId32 ",\"mqtt\":\"connected\",\"batt_v\":%s,"
	                        "\"sleep_in_s\":%" PRId32 ",\"heap\":%" PRIu32 ",\"heap_min\":%" PRIu32 ",\"dtc\":{\"supported\":%s,\"state\":\"%s\","
	                        "\"action\":\"%s\",\"src\":\"%s\",\"seq\":%" PRIu32 ",\"ecu\":%" PRIu32 ",\"total\":%" PRIu32 ",\"name\":\"%s\",\"reason\":\"%s\","
	                        "\"age_s\":%" PRIu32 ",\"count\":%" PRIu32 ",\"result_seq\":%" PRIu32 "}}",
	                        s->id, s->fw, s->git, s->boot, s->up_s, autopid[s->autopid], s->pids, s->ecu_online ? "online" : "offline", s->pass, s->rx_age_ms,
	                        volts, s->sleep_in_s, s->heap, s->heap_min, s->dtc.supported ? "true" : "false", phases[s->dtc.phase],
	                        !s->dtc.has_request ? "" : s->dtc.clear ? "clear" : "read", !s->dtc.has_request ? "" : s->dtc.from_http ? "http" : "mqtt",
	                        s->dtc.seq, s->dtc.step, s->dtc.total, s->dtc.name, s->dtc.reason, s->dtc.age_s, s->dtc.count, s->dtc.result_seq);
}

// The number of a read as API.md wants it in a query: 1 to 10 decimal digits, 1 to 2147483647
static bool query_seq(const char *text, uint32_t *seq)
{
	uint64_t number = 0;
	size_t i;

	for(i = 0; text[i] != '\0'; i++)
	{
		if(text[i] < '0' || text[i] > '9' || i >= 10) return false;
		number = number * 10 + (uint64_t)(text[i] - '0');
	}
	*seq = (uint32_t)number;
	return i > 0 && number >= 1 && number <= 0x7FFFFFFFu;
}

static void answer_text(answer_t *answer, int status, const char *text)
{
	answer->status = status;
	strcpy(body_room, text);
	answer->body = body_room;
	answer->length = strlen(text);
}

// The answer of the adapter to one request, by method and path as the display wrote them
static void adapter_answer(adapter_t *a, const poll_request_t *asked, uint64_t now_ms, answer_t *answer)
{
	memset(answer, 0, sizeof(*answer));
	adapter_catch_up(a, now_ms);
	if(a->dead) return;

	if(strncmp(asked->path, "/api/", 5) == 0 && !a->api)
	{
		answer_text(answer, 404, NOT_FOUND_TEXT);
	}
	else if(!asked->post && strcmp(asked->path, "/api/state") == 0)
	{
		adapter_state(a, now_ms, &answer->state);
		answer->status = 200;
		answer->length = state_json(&answer->state, body_room, sizeof(body_room));
		answer->body = body_room;
		answer->is_state = true;
	}
	else if(asked->post && strncmp(asked->path, "/api/dtc?", 9) == 0)
	{
		const char *query = asked->path + 9;
		const char *reason = "bad_request";
		uint32_t number = 0;
		uint32_t seq = 0;

		answer->status = 400;
		if(strcmp(query, "action=read") == 0) answer->status = adapter_request(a, false, true, 0, now_ms, &number, &reason);
		else if(strncmp(query, "action=clear&seq=", 17) == 0 && query_seq(query + 17, &seq)) answer->status = adapter_request(a, true, true, seq, now_ms, &number, &reason);

		if(reason == NULL) answer->length = (size_t)snprintf(body_room, sizeof(body_room), "{\"accepted\":true,\"seq\":%" PRIu32 "}", number);
		else answer->length = (size_t)snprintf(body_room, sizeof(body_room), "{\"accepted\":false,\"reason\":\"%s\",\"seq\":%" PRIu32 "}", reason, number);
		answer->body = body_room;
		answer->post_seq = number;
		if(reason != NULL) strcpy(answer->post_reason, reason);
	}
	else if(!asked->post && strcmp(asked->path, "/api/dtc/result") == 0)
	{
		if(a->autopid != WICAN_AUTOPID_RUN)
		{
			answer_text(answer, 503, "{\"accepted\":false,\"reason\":\"not_ready\",\"seq\":0}");
		}
		else if(a->result_seq == 0)
		{
			answer_text(answer, 204, "");
		}
		else
		{
			answer_text(answer, 200, a->result);
			snprintf(header_room, sizeof(header_room), "%" PRIu32, a->result_seq);
			answer->seq_header = header_room;
			answer->is_result = true;
			answer->result_clear = a->result_clear;
			answer->result_count = a->result_count;
			answer->names = true;
			answer->number = a->result_seq;
		}
	}
	else if(!asked->post && strcmp(asked->path, "/autopid_data") == 0)
	{
		answer->status = 200;
		if(a->autopid == WICAN_AUTOPID_OFF) strcpy(body_room, "{\"error\":\"No data available\"}");
		else if(!a->valid) strcpy(body_room, "{}");
		else if(a->has_rpm) snprintf(body_room, sizeof(body_room), "{\"ENGINE_RPM\":%d,\"COOLANT_TMP\":21.5,\"FUEL_L\":54}", a->rpm);
		else strcpy(body_room, "{\"COOLANT_TMP\":21.5,\"FUEL_L\":54}");
		answer->body = body_room;
		answer->length = strlen(body_room);
	}
	else if(!asked->post && strcmp(asked->path, "/load_car_config") == 0)
	{
		if(a->autopid == WICAN_AUTOPID_OFF) answer_text(answer, 500, "Failed to generate JSON");
		else answer_text(answer, 200, a->config_text != NULL ? a->config_text : a->has_rpm ? CONFIG_RPM : CONFIG_NO_RPM);
	}
	else
	{
		// A path the adapter does not know, or a known one with the other method
		answer_text(answer, 404, NOT_FOUND_TEXT);
	}
}

/* ---------------------------------------------------------------------------------------------------
 * The task of the display that talks to the adapter
 */

// poll_prepare() now; what it hands out is noted
static bool send(void)
{
	bool sent = poll_prepare(&poll, now, &request);
	size_t length = strlen(trace);

	if(sent && length + 1 < sizeof(trace))
	{
		trace[length] = request.kind <= POLL_DTC_CLEAR ? LETTERS[request.kind] : '?';
		trace[length + 1] = '\0';
	}
	return sent;
}

// An answer made by the scene
static void reply(int status, const char *body, const char *seq_header)
{
	poll_apply(&poll, &request, status, body, body != NULL ? strlen(body) : 0, seq_header, now, work, POLL_TOKENS);
}

// The answer of the adapter to the request under way. A body that has no room at the caller is passed as
// the status that came with it and an empty body.
static void answer(void)
{
	answer_t made;

	adapter_answer(&wican, &request, now, &made);
	if(made.length >= POLL_BODY_SIZE) poll_apply(&poll, &request, made.status, "", 0, made.seq_header, now, work, POLL_TOKENS);
	else poll_apply(&poll, &request, made.status, made.body, made.length, made.seq_header, now, work, POLL_TOKENS);
}

// Everything the display wants to send now, each request answered by the adapter at once
static void exchange(void)
{
	int requests;

	for(requests = 0; requests < 16 && send(); requests++) answer();
}

// Requests are sent and answered until the display hands out one of this kind: it is left without an answer.
// false if it does not ask for one now.
static bool until(poll_kind_t kind)
{
	int requests;

	for(requests = 0; requests < 16 && send(); requests++)
	{
		if(request.kind == kind) return true;
		answer();
	}
	return false;
}

// One second later
static void second(void)
{
	size_t length = strlen(trace);

	now += 1000;
	if(length > 0 && length + 1 < sizeof(trace)) strcpy(trace + length, " ");
	exchange();
}

static void seconds(int count)
{
	while(count-- > 0) second();
}

// The requests since the last call
static bool sent(const char *expected)
{
	bool same = strcmp(trace, expected) == 0;

	if(!same) printf("  sent '%s', expected '%s'\n", trace, expected);
	trace[0] = '\0';
	return same;
}

static void join(const char *bound_id, uint64_t at_ms)
{
	now = at_ms;
	poll_init(&poll, bound_id);
	poll_wifi(&poll, true, now);
	trace[0] = '\0';
}

static void setup(void)
{
	adapter_init(&wican, 0);
	join(OWN, 100000);
	exchange();
	seconds(2);
	poll_take_events(&poll);
	trace[0] = '\0';
}

static conn_view_t view(void)
{
	return conn_view(&poll.conn, now);
}

static const value_t *value(const char *name)
{
	return values_find(&poll.values, name);
}

// When a value was seen last; a time that never comes if there is no such value
static uint64_t seen_at(const char *name)
{
	return value(name) != NULL ? value(name)->seen_ms : UINT64_MAX;
}

// The number of a value; a number no value has if there is none or it is a switch
static double number_of(const char *name)
{
	return value(name) != NULL && value(name)->kind == VALUE_NUMBER ? value(name)->number : -1e300;
}

static bool reason_is(const char *reason)
{
	return poll.flow.phase == DTC_FLOW_FAILED && strcmp(poll.flow.reason, reason) == 0;
}

static dtc_flow_block_t read_block(void)
{
	return dtc_flow_read_block(&poll.flow, &poll.conn, &poll.values, &poll.catalog, now);
}

static bool fresh(const char *name)
{
	return values_age(value(name), now) == VALUE_AGE_FRESH;
}

// Hand-written texts (fixtures)
static char read_text[512];         // a result of a read with two codes
static char clear_text[512];        // the result of the clear behind it: one code is back
static char empty_text[2048];       // a result of a read without codes
static char shortened_text[4096];   // a result of a read with 165 codes, of which 10 are listed
static char state_example[512];     // the example of API.md: adapter a1b2c3d4e5f6, boot 1234567890, up 812, pass 1234, 12.4 V
static char state_starting[512];    // the same adapter starting: boot 7, up 0, no voltage
static char state_scan[512];        // adapter 0123456789ab with a read from HTTP running, 14.4 V
static char state_limits[512];      // every number at its largest
static char stored_catalog[512];
static char config_without_rpm[256];

static void load_fixtures(void)
{
	bool loaded = read_fixture("fixtures/poll_result_read.json", read_text, sizeof(read_text)) &&
	              read_fixture("fixtures/poll_result_clear.json", clear_text, sizeof(clear_text)) &&
	              read_fixture("../../tools/w906/fixtures/dtc_result_read_empty.json", empty_text, sizeof(empty_text)) &&
	              read_fixture("../../tools/w906/fixtures/dtc_result_shortened.json", shortened_text, sizeof(shortened_text)) &&
	              read_fixture("../../tools/w906/fixtures/api_state_example.json", state_example, sizeof(state_example)) &&
	              read_fixture("../../tools/w906/fixtures/api_state_starting.json", state_starting, sizeof(state_starting)) &&
	              read_fixture("../../tools/w906/fixtures/api_state_scan.json", state_scan, sizeof(state_scan)) &&
	              read_fixture("../../tools/w906/fixtures/api_state_limits.json", state_limits, sizeof(state_limits)) &&
	              read_fixture("fixtures/poll_stored_catalog.json", stored_catalog, sizeof(stored_catalog)) &&
	              read_fixture("fixtures/poll_config_without_rpm.json", config_without_rpm, sizeof(config_without_rpm));

	check(loaded, "the fixtures of the scenes are there");
}

// setup() with the results of the fixtures: the vehicle has two codes stored, a clear leaves one
static void scene(void)
{
	setup();
	wican.read_text = read_text;
	wican.clear_text = clear_text;
}

// The own read 42 from 102000 to its list at 106000
static void scene_list(void)
{
	scene();
	poll_read(&poll, now);
	exchange();
	seconds(4);
	poll_take_events(&poll);
	trace[0] = '\0';
}

// ... and the own clear 43, confirmed at 107000, under way from then on
static void scene_clearing(void)
{
	scene_list();
	second();
	poll_clear(&poll, false, now);
	exchange();
	poll_take_events(&poll);
	trace[0] = '\0';
}

// ... to its outcome at 111000
static void scene_cleared(void)
{
	scene_clearing();
	seconds(4);
	poll_take_events(&poll);
	trace[0] = '\0';
}

static bool shows_list(const char *text, uint32_t codes)
{
	return poll.has_list && strcmp(poll.list_text, text) == 0 && !poll.list.clear && poll.list.dtc_count == codes && poll.list.ecu_count == 1 &&
	       strcmp(poll.list.ecus[0].name, "N10 SAM") == 0;
}

static bool old_is(const char *text, uint32_t codes)
{
	return poll.has_old && strcmp(poll.old_text, text) == 0 && !poll.old.clear && poll.old.dtc_count == codes;
}

// A request as an earlier call may have left it: every member has to be written anew
static void used(poll_request_t *asked)
{
	asked->kind = POLL_VALUES;
	asked->post = true;
	memset(asked->path, 'x', sizeof(asked->path) - 1);
	asked->path[sizeof(asked->path) - 1] = '\0';
}

// Runs a scene in a child process: a crash or a hang of the module is then a failed check and not the end
// of the test. Returns what the scene returned, false if it did not return.
static bool survives(bool (*played)(void))
{
	int status = -1;
	pid_t child;

	fflush(stdout);
	child = fork();
	if(child == 0)
	{
		alarm(60);
		_exit(played() ? 0 : 1);
	}
	return child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* ---------------------------------------------------------------------------------------------------
 * Examples
 */

static void test_constants(void)
{
	check(POLL_PATH_SIZE == 48 && POLL_BODY_SIZE == 16384 && POLL_TEXT_SIZE == 5200 && POLL_TOKENS == 2048 && POLL_TOKENS == DTC_RESULT_TOKENS &&
	      POLL_TIMEOUT_MS == 4000, "48 bytes for a path, 16384 for a body, 5200 for a result text, 2048 tokens, 4000 ms for a request");
	check(strcmp(POLL_HEADER_NAME, "X-WiCAN-DTC") == 0 && strcmp(POLL_HEADER_VALUE, "1") == 0 && strcmp(POLL_SEQ_HEADER, "X-DTC-Seq") == 0,
	      "the headers of API.md: X-WiCAN-DTC: 1 with a POST, X-DTC-Seq with a result");
	check(POLL_EVENT_BOUND == 1 && POLL_EVENT_CATALOG == 2 && POLL_EVENT_OLD == 4 && POLL_EVENT_LISTS == 8 && POLL_EVENT_FORGET == 16,
	      "the events are five different bits");
	check(sizeof(((poll_t *)0)->list_text) == POLL_TEXT_SIZE && sizeof(((poll_t *)0)->old_text) == POLL_TEXT_SIZE && sizeof(((poll_request_t *)0)->path) == POLL_PATH_SIZE &&
	      sizeof(((poll_t *)0)->bound_id) == 33, "the rooms of the struct have the sizes named");
}

static void test_init(void)
{
	static const char id_32[] = "0123456789abcdef0123456789abcdef";
	static const char id_33[] = "0123456789abcdef0123456789abcdefX";
	poll_request_t asked;

	// A struct that was in use: a clear under way, a list, an old list, values, a catalogue, events, counters
	scene_clearing();
	now += 1000;
	send();
	poll.has_cleared = true;
	poll.lost = true;
	poll.http_failed = 6;
	poll.events = 0x1F;
	strcpy(poll.bound_id, "another");
	poll_init(&poll, OWN);
	check(strcmp(poll.bound_id, OWN) == 0 && strcmp(poll.conn.bound_id, OWN) == 0 && !poll.conn.bind_pending, "init with a stored id: the display is bound to it, nothing is to be stored");
	check(!poll.wifi && !poll.asking && !poll.lost && poll.events == 0 && poll.http_ok == 0 && poll.http_failed == 0,
	      "after init: no network, no request under way, no event, no answer counted, whatever stood in the memory");
	check(!poll.has_list && !poll.has_cleared && !poll.has_old && !poll.catalog_complete, "after init: no list, no outcome, no old list, the catalogue not complete");
	check(poll.catalog.count == 1 && strcmp(poll.catalog.entries[0].name, "@BATT_V") == 0 && strcmp(poll.catalog.entries[0].unit, "V") == 0 &&
	      !poll.catalog.entries[0].in_profile && poll.catalog.dropped == 0, "after init the catalogue is that of catalog_init(): the battery voltage alone");
	check(poll.values.count == 0 && !poll.values.has_pass && poll.values.dropped == 0, "after init there is no value and no pass counter");
	check(poll.flow.phase == DTC_FLOW_IDLE && poll.flow.to_send == DTC_FLOW_SEND_NOTHING && poll.flow.read_seq == 0 && poll.flow.reason[0] == '\0',
	      "after init the fault memory flow is idle");
	check(!poll.catalog_guard.has_stored && !poll.catalog_guard.written && !poll.catalog_guard.has_seen, "after init guard knows of no catalogue in the flash");
	check(conn_view(&poll.conn, 0) == CONN_VIEW_NO_WIFI && !poll.conn.wifi && !poll.conn.asking && conn_state(&poll.conn) == NULL, "after init the connection is that of conn_init(): no WiFi");

	used(&asked);
	check(!poll_prepare(&poll, 5000, &asked) && asked.kind == POLL_NONE && !asked.post && asked.path[0] == '\0',
	      "without a network there is no request: POLL_NONE, no POST, an empty path");
	check(poll_take_events(&poll) == 0 && !poll.asking, "asking for a request without a network raises nothing and leaves none under way");

	scene();
	poll_init(&poll, NULL);
	check(poll.bound_id[0] == '\0' && poll.conn.bound_id[0] == '\0' && poll.values.count == 0 && poll.catalog.count == 1 && !poll.wifi, "init without an id (NULL): not bound");
	check(POLL_START_UNKNOWN == 0 && poll.start == POLL_START_UNKNOWN, "after init nothing is known of the start of an adapter, whatever answered before it");
	scene();
	poll_init(&poll, "");
	check(poll.bound_id[0] == '\0' && poll.conn.bound_id[0] == '\0', "init with an empty id: not bound");
	poll_init(&poll, id_32);
	check(strcmp(poll.bound_id, id_32) == 0 && strcmp(poll.conn.bound_id, id_32) == 0, "an id of 32 bytes is kept whole");
	poll_init(&poll, id_33);
	check(strcmp(poll.bound_id, id_32) == 0 && strcmp(poll.conn.bound_id, poll.bound_id) == 0, "an id of 33 bytes: the struct and conn hold the same 32 bytes");
}

// A result with blanks behind it, which are JSON, `length` bytes long
static const char *padded_result(const char *text, size_t length)
{
	static char padded[POLL_TEXT_SIZE + 64];

	memset(padded, ' ', sizeof(padded) - 1);
	memcpy(padded, text, strlen(text));
	padded[length] = '\0';
	return padded;
}

static bool old_without_room(void)
{
	poll_init(&poll, OWN);
	poll_stored(&poll, NULL, 0, padded_result(read_text, POLL_TEXT_SIZE), POLL_TEXT_SIZE, work, POLL_TOKENS);
	if(poll.has_old || poll_take_events(&poll) != 0 || poll.bound_id[0] != 'a') return false;
	poll_stored(&poll, NULL, 0, padded_result(read_text, POLL_TEXT_SIZE + 1), POLL_TEXT_SIZE + 1, work, POLL_TOKENS);
	return !poll.has_old && poll_take_events(&poll) == 0;
}

static void test_stored(void)
{
	// The catalogue the display has after it loaded the profile of the adapter of this test
	static const char same_catalog[] = "{\"@BATT_V\":{\"unit\":\"V\",\"class\":\"\",\"profile\":false,\"delivered\":true},"
	                                   "\"ENGINE_RPM\":{\"unit\":\"RPM\",\"class\":\"frequency\",\"profile\":true,\"delivered\":true},"
	                                   "\"COOLANT_TMP\":{\"unit\":\"°C\",\"class\":\"temperature\",\"profile\":true,\"delivered\":true},"
	                                   "\"FUEL_L\":{\"unit\":\"L\",\"class\":\"none\",\"profile\":true,\"delivered\":false}}";
	// An entry as the adapter sends it, not as the display stores it: it has no "profile"
	static const char no_catalog[] = "{\"X\":{\"unit\":\"V\",\"class\":\"\"}}";
	static const char no_result[] = "{\"state\":\"done\"}";
	static char padded[POLL_TEXT_SIZE + 64];
	uint32_t sum, events;
	size_t length = strlen(read_text);

	poll_init(&poll, OWN);
	poll_stored(&poll, stored_catalog, strlen(stored_catalog), NULL, 0, work, POLL_TOKENS);
	check(poll.catalog.count == 3 && strcmp(poll.catalog.entries[1].name, "ENGINE_RPM") == 0 && strcmp(poll.catalog.entries[1].unit, "RPM") == 0 &&
	      poll.catalog.entries[1].in_profile && !poll.catalog.entries[1].delivered && strcmp(poll.catalog.entries[2].name, "OIL_TEMP_OLD") == 0 &&
	      strcmp(poll.catalog.entries[2].value_class, "temperature") == 0, "a stored catalogue is put in: its three entries, none delivered yet");
	check(poll.catalog_guard.has_stored && poll.catalog_guard.stored_sum == catalog_checksum(&poll.catalog) && !poll.catalog_guard.written,
	      "guard is told the check sum of the stored catalogue");
	check(!poll.has_old && poll_take_events(&poll) == 0 && !poll.catalog_complete, "a stored catalogue alone: no old list, no event, the catalogue is not complete");

	sum = catalog_checksum(&poll.catalog);
	poll_stored(&poll, NULL, 0, NULL, 0, work, POLL_TOKENS);
	check(poll.catalog.count == 3 && poll.catalog_guard.has_stored && poll.catalog_guard.stored_sum == sum, "no catalogue stored (NULL): catalogue and guard stay as they were");
	poll_stored(&poll, no_catalog, strlen(no_catalog), NULL, 0, work, POLL_TOKENS);
	check(poll.catalog.count == 3 && catalog_checksum(&poll.catalog) == sum && poll.catalog_guard.has_stored && poll.catalog_guard.stored_sum == sum,
	      "a text that is no stored catalogue counts as nothing stored: catalogue and guard stay as they were");

	poll_init(&poll, OWN);
	poll_stored(&poll, no_catalog, strlen(no_catalog), NULL, 0, work, POLL_TOKENS);
	check(poll.catalog.count == 1 && !poll.catalog_guard.has_stored, "a text that is no stored catalogue, at the start: the catalogue of init, guard knows of none in the flash");
	poll_init(&poll, OWN);
	poll_stored(&poll, stored_catalog, strlen(stored_catalog) - 1, NULL, 0, work, POLL_TOKENS);
	check(poll.catalog.count == 1 && !poll.catalog_guard.has_stored, "a stored catalogue passed one byte short is not read behind its length: nothing stored");
	poll_init(&poll, OWN);
	poll_stored(&poll, stored_catalog, strlen(stored_catalog), NULL, 0, work, 30);
	check(poll.catalog.count == 1 && !poll.catalog_guard.has_stored, "a stored catalogue of 31 tokens with room for 30: nothing stored");
	poll_stored(&poll, stored_catalog, strlen(stored_catalog), NULL, 0, work, 31);
	check(poll.catalog.count == 3 && poll.catalog_guard.has_stored, "a stored catalogue of 31 tokens with room for 31: taken");

	// What guard does with it: the catalogue of the adapter is the stored one, nothing is written
	adapter_init(&wican, 0);
	join(OWN, 100000);
	poll_stored(&poll, same_catalog, strlen(same_catalog), NULL, 0, work, POLL_TOKENS);
	exchange();
	seconds(40);
	events = poll_take_events(&poll);
	check(poll.catalog_complete && (events & POLL_EVENT_CATALOG) == 0 && !poll.catalog_guard.written, "the profile of the adapter is the stored catalogue: it is never stored again");
	adapter_restart(&wican, BOOT + 1, 42, now + 500);
	seconds(40);
	events = poll_take_events(&poll);
	check((events & POLL_EVENT_FORGET) != 0 && (events & POLL_EVENT_CATALOG) == 0 && poll.catalog_complete && poll.catalog_guard.has_stored && !poll.catalog_guard.written,
	      "the adapter restarts and its profile is still the stored catalogue: the catalogue is started anew and loaded again, and still not stored again");
	adapter_init(&wican, 0);
	join(OWN, 100000);
	poll_stored(&poll, stored_catalog, strlen(stored_catalog), NULL, 0, work, POLL_TOKENS);
	exchange();
	seconds(29);
	events = poll_take_events(&poll);
	second();
	check((events & POLL_EVENT_CATALOG) == 0 && poll_take_events(&poll) == POLL_EVENT_CATALOG && poll.catalog_guard.written,
	      "the profile of the adapter differs from the stored catalogue: POLL_EVENT_CATALOG after it stood for 30 s, not after 29 s");
	check(poll.catalog.count == 4 && catalog_find(&poll.catalog, "OIL_TEMP_OLD") < 0 && catalog_find(&poll.catalog, "FUEL_L") == 3,
	      "the profile of the adapter replaces the stored one: what it does not name is gone");

	// The list before the last clear
	poll_init(&poll, OWN);
	poll_stored(&poll, NULL, 0, read_text, length, work, POLL_TOKENS);
	check(old_is(read_text, 2) && poll.old.code_count == 2 && strcmp(poll.old.codes[1].code, "9302") == 0, "a stored old list is put in: the struct and its text");
	check(poll_take_events(&poll) == POLL_EVENT_LISTS, "a stored old list raises POLL_EVENT_LISTS");
	check(poll.catalog.count == 1 && !poll.catalog_guard.has_stored && !poll.has_list && !poll.has_cleared, "a stored old list alone: the catalogue stays, no list and no outcome");
	poll_stored(&poll, NULL, 0, NULL, 0, work, POLL_TOKENS);
	check(old_is(read_text, 2) && poll_take_events(&poll) == 0, "no old list stored (NULL): the one there stays, no event");
	poll_stored(&poll, NULL, 0, no_result, strlen(no_result), work, POLL_TOKENS);
	check(old_is(read_text, 2) && poll_take_events(&poll) == 0, "a text that is no result counts as nothing stored: the old list there stays, no event");
	poll_stored(&poll, NULL, 0, clear_text, strlen(clear_text), work, POLL_TOKENS);
	check(poll.has_old && strcmp(poll.old_text, clear_text) == 0 && poll.old.clear && poll.old.dtc_count == 1 && poll_take_events(&poll) == POLL_EVENT_LISTS,
	      "another stored old list replaces the one there");

	poll_init(&poll, OWN);
	poll_stored(&poll, NULL, 0, no_result, strlen(no_result), work, POLL_TOKENS);
	check(!poll.has_old && poll_take_events(&poll) == 0, "a text that is no result, at the start: no old list, no event");
	poll_stored(&poll, NULL, 0, read_text, length - 1, work, POLL_TOKENS);
	check(!poll.has_old && poll_take_events(&poll) == 0, "an old list passed one byte short is not read behind its length: nothing stored");
	poll_stored(&poll, NULL, 0, read_text, length, work, 31);
	check(!poll.has_old && poll_take_events(&poll) == 0, "an old list of 32 tokens with room for 31: nothing stored");
	poll_stored(&poll, NULL, 0, read_text, length, work, 32);
	check(old_is(read_text, 2), "an old list of 32 tokens with room for 32: taken");

	poll_init(&poll, OWN);
	poll_stored(&poll, NULL, 0, SHORTEST_RESULT, strlen(SHORTEST_RESULT), work, POLL_TOKENS);
	check(poll.has_old && strcmp(poll.old_text, SHORTEST_RESULT) == 0 && poll.old.ecu_count == 0, "the shortest result there is, is an old list");

	// A text followed by something else: only `length` bytes are the list
	poll_init(&poll, OWN);
	memset(padded, 'x', sizeof(padded) - 1);
	memcpy(padded, read_text, length);
	memset(poll.old_text, 'y', sizeof(poll.old_text));
	poll_stored(&poll, NULL, 0, padded, length, work, POLL_TOKENS);
	check(old_is(read_text, 2) && poll.old_text[length + 1] == 'y', "an old list followed by other bytes: its text ends at its length");

	// Blanks behind a result are JSON: a list as long as the room allows, and one byte more
	memset(padded, ' ', sizeof(padded) - 1);
	memcpy(padded, read_text, length);
	poll_init(&poll, OWN);
	poll_stored(&poll, NULL, 0, padded, POLL_TEXT_SIZE - 1, work, POLL_TOKENS);
	check(poll.has_old && strlen(poll.old_text) == POLL_TEXT_SIZE - 1 && memcmp(poll.old_text, padded, POLL_TEXT_SIZE - 1) == 0 && poll.old.dtc_count == 2,
	      "an old list of 5199 bytes fills old_text to its last byte");
	check(survives(old_without_room), "an old list of 5200 or 5201 bytes has no room: nothing stored, no event, nothing written behind old_text");

	// Both at once
	poll_init(&poll, OWN);
	poll_stored(&poll, stored_catalog, strlen(stored_catalog), read_text, length, work, POLL_TOKENS);
	check(poll.catalog.count == 3 && poll.catalog_guard.has_stored && old_is(read_text, 2) && poll.old.code_count == 2, "catalogue and old list stored: both are put in");
	poll_init(&poll, OWN);
	poll_stored(&poll, "[]", 2, read_text, length, work, POLL_TOKENS);
	check(poll.catalog.count == 1 && old_is(read_text, 2), "a catalogue that cannot be read does not keep the old list out");
	poll_init(&poll, OWN);
	poll_stored(&poll, stored_catalog, strlen(stored_catalog), "[]", 2, work, POLL_TOKENS);
	check(poll.catalog.count == 3 && !poll.has_old, "an old list that cannot be read does not keep the catalogue out");
}

static void test_wifi(void)
{
	uint32_t ok;

	adapter_init(&wican, 0);
	now = 100000;
	poll_init(&poll, OWN);
	trace[0] = '\0';
	check(!send() && request.kind == POLL_NONE, "before the display joined a network nothing is sent");
	poll_wifi(&poll, true, now);
	check(poll.wifi && poll.conn.wifi && view() == CONN_VIEW_CONNECTING && !poll.lost, "the display joined a network: connecting");
	check(send() && request.kind == POLL_STATE && sent("S"), "in a network the state is asked for at once");

	scene();
	poll_wifi(&poll, true, now + 500);
	check(poll.values.count == 4 && poll.catalog_complete && conn_state(&poll.conn) != NULL && poll.conn.wifi_since_ms == 100000 && poll_take_events(&poll) == 0,
	      "joining while joined changes nothing: values, catalogue and connection stay");

	// Losing the network
	now = 103000;
	check(send() && request.kind == POLL_STATE, "the scene: the state of the fourth round is under way");
	ok = poll.http_ok;
	poll_wifi(&poll, false, now + 100);
	check(!poll.wifi && !poll.asking && conn_view(&poll.conn, now + 100) == CONN_VIEW_NO_WIFI && poll.lost, "the network is lost: no request is under way any more, the outage is noted");
	check(poll.values.count == 4 && poll.catalog_complete && poll_take_events(&poll) == 0, "losing the network keeps values and catalogue and raises nothing by itself");
	now = 103200;
	reply(200, state_example, NULL);
	check(poll.http_ok == ok && poll.http_failed == 0 && conn_state(&poll.conn) == NULL && seen_at("@BATT_V") == 102000,
	      "the answer to the request that was under way when the network was lost is ignored");
	check(!send() && request.kind == POLL_NONE && request.path[0] == '\0', "without the network nothing is sent");
	poll_wifi(&poll, false, now + 5000);
	check(poll.conn.wifi_since_ms == 103100 && poll.lost && !poll.wifi, "losing the network while it is lost changes nothing");

	// Joining again
	now = 110000;
	poll_wifi(&poll, true, now);
	check(poll.values.count == 0 && !poll.values.has_pass && value("ENGINE_RPM") == NULL, "joining forgets the values and the pass counter");
	check(!poll.catalog_complete && poll.catalog.count == 4 && !poll.catalog_guard.has_seen, "joining: the catalogue stays but is not complete, guard is told of the new connection");
	check(!poll.lost && view() == CONN_VIEW_CONNECTING && poll_take_events(&poll) == 0, "joining ends the outage and raises nothing");
	sent("S");
	exchange();
	check(sent("SCV") && poll.catalog_complete && poll.values.count == 4 && seen_at("ENGINE_RPM") == 110000, "after joining again: state, profile and values are asked for anew");

	// What found no room in the values is counted on
	scene();
	now += 1000;
	until(POLL_VALUES);
	reply(200, "{\"ENGINE_RPM\":0,\"A_NAME_OF_33_BYTES_IS_TOO_LONG_XX\":1}", NULL);
	check(poll.values.dropped == 1 && poll.values.count == 4, "the scene: a value with a name of 33 bytes was dropped");
	poll_wifi(&poll, false, now);
	poll_wifi(&poll, true, now);
	check(poll.values.count == 0 && poll.values.dropped == 1, "joining forgets the values, not the count of those that were dropped");

	// What becomes of a fault memory request of the display
	scene();
	poll_read(&poll, now);
	poll_wifi(&poll, false, now);
	check(poll.flow.phase == DTC_FLOW_IDLE && poll.flow.to_send == DTC_FLOW_SEND_NOTHING && poll.lost && poll_take_events(&poll) == 0,
	      "the network is lost while a read waits to be sent: it was never sent - idle, no failure");
	now = 104000;
	poll_wifi(&poll, true, now);
	exchange();
	second();
	check(sent("SCV SV") && poll.flow.phase == DTC_FLOW_IDLE && wican.seq == 0, "the read that waited when the network was lost is never sent");

	scene();
	poll_read(&poll, now);
	send();
	poll_wifi(&poll, false, now);
	check(reason_is("no_answer") && !poll.asking, "the network is lost while the POST of a read is under way: failed, no answer");
	reply(202, "{\"accepted\":true,\"seq\":42}", NULL);
	check(reason_is("no_answer") && poll.http_ok == 7, "the answer to that POST is ignored when it comes late");

	scene();
	poll_read(&poll, now);
	exchange();
	poll_wifi(&poll, false, now);
	check(reason_is("no_answer"), "the network is lost while the own read runs: failed, no answer");

	scene_list();
	poll_wifi(&poll, false, now);
	check(poll.flow.phase == DTC_FLOW_LIST && shows_list(read_text, 2) && poll_take_events(&poll) == 0, "the network is lost while the list is shown: it stays");

	scene_list();
	second();
	check(poll_clear(&poll, false, now) == DTC_FLOW_ALLOWED && poll.has_list, "the scene: a clear waits to be sent");
	poll_wifi(&poll, false, now);
	check(poll.flow.phase == DTC_FLOW_LIST && poll.flow.to_send == DTC_FLOW_SEND_NOTHING && shows_list(read_text, 2) && !poll.has_old && poll_take_events(&poll) == 0,
	      "the network is lost while a clear waits to be sent: it was never sent - the list is shown as before, nothing became the old list");
	trace[0] = '\0';
	now = 108000;
	poll_wifi(&poll, true, now);
	exchange();
	seconds(2);
	check(sent("SRCV SV SV") && wican.seq == 42 && poll.flow.phase == DTC_FLOW_LIST && shows_list(read_text, 2) && !poll.has_old,
	      "the clear that waited when the network was lost is never sent; the list stays while the adapter shows its read");
	check(poll_clear(&poll, false, now) == DTC_FLOW_ALLOWED && send() && request.kind == POLL_DTC_CLEAR && strcmp(request.path, "/api/dtc?action=clear&seq=42") == 0,
	      "after the network came back the user confirms again: the clear goes out");

	scene_list();
	second();
	poll_clear(&poll, false, now);
	check(send() && request.kind == POLL_DTC_CLEAR && !poll.has_old && poll_take_events(&poll) == 0, "the scene: the POST of a clear is under way, nothing became the old list");
	poll_wifi(&poll, false, now);
	check(poll.flow.phase == DTC_FLOW_UNKNOWN && !poll.asking && !poll.has_list && old_is(read_text, 2) && poll_take_events(&poll) == (POLL_EVENT_OLD | POLL_EVENT_LISTS),
	      "the network is lost while the POST of a clear is under way: it may have arrived - unknown, and its list is the old list with POLL_EVENT_OLD");

	scene_clearing();
	poll_wifi(&poll, false, now);
	check(poll.flow.phase == DTC_FLOW_UNKNOWN && !poll.has_list && old_is(read_text, 2) && poll_take_events(&poll) == POLL_EVENT_LISTS,
	      "the network is lost while the own clear runs: unknown, the list lives on as the old one it became with the 202");

	scene_cleared();
	poll_wifi(&poll, false, now);
	check(poll.flow.phase == DTC_FLOW_CLEARED && poll.has_cleared && poll_take_events(&poll) == 0, "the network is lost while the outcome of the clear is shown: it stays");

	// The same adapter after the network came back: the list is still the one of its last request
	scene_list();
	poll_wifi(&poll, false, now);
	now = 107000;
	poll_wifi(&poll, true, now);
	exchange();
	check(sent("SRCV") && poll.flow.phase == DTC_FLOW_LIST && shows_list(read_text, 2) && poll.list.code_count == 2 && poll_take_events(&poll) == 0,
	      "the network comes back and the adapter did not restart: the list stays, although its result is fetched once more");

	// The adapter restarted while the network was gone. conn starts over and names no restart; the start the
	// catalogue came from is another one all the same (test_restart_unseen()).
	scene_list();
	poll_wifi(&poll, false, now);
	adapter_restart(&wican, BOOT + 1, 42, 106500);
	now = 107000;
	poll_wifi(&poll, true, now);
	exchange();
	check(sent("SCV") && poll.flow.phase == DTC_FLOW_IDLE && !poll.has_list && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "the network comes back and the adapter has restarted: the list is dropped, and what came from the start before is forgotten");
}

static void test_requests(void)
{
	struct
	{
		poll_request_t request;
		unsigned char guard[32];
	} room;
	static const unsigned char untouched[32] = {0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5,
	                                            0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5};

	// The first round, request by request
	adapter_init(&wican, 0);
	join(OWN, 100000);
	check(send() && request.kind == POLL_STATE && !request.post && strcmp(request.path, "/api/state") == 0 && poll.asking && poll.asked == POLL_STATE,
	      "the first request: GET /api/state");
	memset(&room, 0xA5, sizeof(room));
	used(&room.request);
	check(!poll_prepare(&poll, now, &room.request) && room.request.kind == POLL_NONE && !room.request.post && room.request.path[0] == '\0' &&
	      memcmp(room.guard, untouched, sizeof(untouched)) == 0, "while a request is under way there is no other: POLL_NONE with an empty path");
	check(poll.asking && poll.asked == POLL_STATE, "asking again does not end the request under way");
	answer();
	check(!poll.asking && send() && request.kind == POLL_CATALOG && !request.post && strcmp(request.path, "/load_car_config") == 0, "the profile: GET /load_car_config");
	answer();
	memset(&room, 0xA5, sizeof(room));
	used(&room.request);
	room.request.kind = POLL_NONE;
	room.request.post = true;
	check(poll_prepare(&poll, now, &room.request) && room.request.kind == POLL_VALUES && !room.request.post && strcmp(room.request.path, "/autopid_data") == 0 &&
	      memcmp(room.guard, untouched, sizeof(untouched)) == 0, "the values: GET /autopid_data, nothing is written behind the request");
	request = room.request;
	answer();
	check(!send() && request.kind == POLL_NONE && request.path[0] == '\0', "the round is over: nothing to send");
	now = 100999;
	check(!send(), "999 ms after the round began it is not time yet");
	now = 101000;
	check(send() && request.kind == POLL_STATE, "1000 ms after the round began the next one begins");

	// The fault memory request of the user goes first
	scene();
	poll_read(&poll, now);
	now = 103000;
	exchange();
	check(sent("rS") && wican.seq == 42 && view() == CONN_VIEW_SCAN, "a read the user asked for goes out before the round that is due; its state shows it queued, so no values are asked for");
	scene();
	now = 103000;
	send();
	answer();
	check(poll_read(&poll, now) == DTC_FLOW_ALLOWED, "the scene: the user asks to read in the middle of a round");
	exchange();
	check(sent("SrV"), "a read the user asked for goes out before the rest of the round, which goes on behind it");
	scene();
	now = 103000;
	send();
	used(&room.request);
	check(poll_read(&poll, now) == DTC_FLOW_ALLOWED && !poll_prepare(&poll, now, &room.request) && room.request.kind == POLL_NONE && poll.flow.to_send == DTC_FLOW_SEND_READ,
	      "a read asked for while a request is under way waits for its answer");
	answer();
	check(send() && request.kind == POLL_DTC_READ && sent("Sr"), "the read that waited goes out as soon as the request under way is answered");

	// The largest number a list can have
	scene();
	wican.next_seq = 4294967295u;
	poll_read(&poll, now);
	exchange();
	seconds(5);
	check(poll.flow.phase == DTC_FLOW_LIST && poll.flow.read_seq == 4294967295u && poll_clear(&poll, false, now) == DTC_FLOW_ALLOWED, "the scene: a list with the number 4294967295");
	memset(&room, 0xA5, sizeof(room));
	used(&room.request);
	room.request.post = false;
	check(poll_prepare(&poll, now, &room.request) && room.request.kind == POLL_DTC_CLEAR && room.request.post &&
	      strcmp(room.request.path, "/api/dtc?action=clear&seq=4294967295") == 0 && memcmp(room.guard, untouched, sizeof(untouched)) == 0,
	      "the clear of the list 4294967295: the path has all ten digits, nothing is written behind the request");
}

static void test_read(void)
{
	const wican_state_t *state;

	scene();
	check(poll_read(&poll, now) == DTC_FLOW_ALLOWED && poll.flow.phase == DTC_FLOW_READ_SENT && poll.flow.to_send == DTC_FLOW_SEND_READ && poll_take_events(&poll) == 0,
	      "the user asks to read with the engine off: allowed, the request waits to be sent");
	check(send() && request.kind == POLL_DTC_READ && request.post && strcmp(request.path, "/api/dtc?action=read") == 0 && poll.asking && poll.asked == POLL_DTC_READ,
	      "the read goes out with the next request: POST /api/dtc?action=read");
	check(poll.flow.phase == DTC_FLOW_READ_SENT && poll.flow.to_send == DTC_FLOW_SEND_NOTHING && !poll.has_old && poll_take_events(&poll) == 0,
	      "a read that goes out makes no old list and raises nothing");
	answer();
	check(poll.flow.phase == DTC_FLOW_READING && poll.flow.seq == 42 && poll.http_ok == 8 && poll.http_failed == 0, "202 with the number 42: the own read is accepted, the answer is counted");
	check(!send(), "the read is sent once: nothing else goes out in this second");

	second();
	state = conn_state(&poll.conn);
	check(sent("r S") && view() == CONN_VIEW_SCAN && state->dtc.phase == WICAN_DTC_RUNNING && state->dtc.seq == 42 && state->dtc.step == 0 && state->dtc.total == 3,
	      "one second later the state shows the own read running: no result, no values are asked for");
	second();
	check(sent("S") && conn_state(&poll.conn)->dtc.step == 2 && strcmp(conn_state(&poll.conn)->dtc.name, "N2/14 Rückhaltesystem (SRS)") == 0 &&
	      poll.flow.phase == DTC_FLOW_READING, "the progress of the scan is in the state of the connection: step 2 of 3 with the name of the control unit");
	second();
	check(sent("S") && !poll.has_list && poll_take_events(&poll) == 0, "while the read runs there is no list and no event");

	now += 1000;
	check(until(POLL_RESULT) && !request.post && strcmp(request.path, "/api/dtc/result") == 0 && poll.asked_result_seq == 42 && poll.asked_age_s == 0,
	      "the state shows the read done with a result: GET /api/dtc/result, asked for with the number 42 and the age 0");
	check(poll.flow.phase == DTC_FLOW_READING && !poll.has_list, "the state that shows the read done does not make the list yet");
	answer();
	check(poll.flow.phase == DTC_FLOW_LIST && poll.flow.read_seq == 42 && poll.flow.list_count == 2 && poll.flow.list_end_ms == 106000,
	      "the result with the number 42 in its header: the flow has its list, with two codes, ended at 106000");
	check(shows_list(read_text, 2) && poll.list.code_count == 2 && strcmp(poll.list.codes[0].code, "9301") == 0 && strcmp(poll.list.codes[1].status, "20") == 0 &&
	      poll.list.duration_ms == 35100, "the result is the list: the struct, and the text byte for byte as the adapter sent it");
	check(!poll.has_cleared && !poll.has_old && poll_take_events(&poll) == POLL_EVENT_LISTS, "the list raises POLL_EVENT_LISTS and nothing else");
	exchange();
	check(sent("SRV") && poll.conn.fetched_result_seq == 42 && seen_at("ENGINE_RPM") == 106000, "behind the result the round goes on with the values");
	second();
	check(sent("SV") && shows_list(read_text, 2) && poll_take_events(&poll) == 0, "the result is fetched once: the next round asks for state and values only");
}

static void test_clear(void)
{
	scene_list();
	check(poll_clear(&poll, false, now) == DTC_FLOW_RPM_UNKNOWN && poll.flow.phase == DTC_FLOW_LIST, "a clear in the second the list arrived: no engine speed newer than the list");
	second();
	check(poll_clear(&poll, true, now) == DTC_FLOW_BUTTON_STUCK && poll.flow.phase == DTC_FLOW_LIST, "a clear with a switch that hangs is refused: the flag is passed on");
	check(poll_clear(&poll, false, now) == DTC_FLOW_ALLOWED && poll.flow.phase == DTC_FLOW_CLEAR_SENT && poll.flow.to_send == DTC_FLOW_SEND_CLEAR,
	      "a clear with a value of the engine speed newer than the list: allowed, the request waits to be sent");
	check(shows_list(read_text, 2) && !poll.has_old && poll_take_events(&poll) == 0, "a confirmed clear that waits changes nothing shown and raises nothing");
	sent("SV");

	check(send() && request.kind == POLL_DTC_CLEAR && request.post && strcmp(request.path, "/api/dtc?action=clear&seq=42") == 0 && poll.asked == POLL_DTC_CLEAR,
	      "the clear goes out with the next request: POST /api/dtc?action=clear&seq=42, the number of the list");
	check(poll.events == 0 && !poll.has_old && shows_list(read_text, 2) && wican.seq == 42,
	      "a clear that goes out makes no old list and raises nothing: the list is shown, and the adapter has not seen the clear yet");
	answer();
	check(poll.flow.phase == DTC_FLOW_CLEARING && poll.flow.seq == 43 && shows_list(read_text, 2), "202 with the number 43: the own clear is accepted, the list is still shown");
	check(poll.events == (POLL_EVENT_OLD | POLL_EVENT_LISTS), "with the 202 of the clear POLL_EVENT_OLD and POLL_EVENT_LISTS are raised");
	check(old_is(read_text, 2) && poll.old.code_count == 2 && strcmp(poll.old.codes[1].code, "9302") == 0 && poll.old.duration_ms == 35100,
	      "with the 202 of the clear the list is the old list: the struct, and the text byte for byte");
	check(poll_take_events(&poll) == (POLL_EVENT_OLD | POLL_EVENT_LISTS) && poll_take_events(&poll) == 0 && poll.events == 0, "the events are taken once");

	seconds(3);
	check(sent("c S S S") && poll.flow.phase == DTC_FLOW_CLEARING && shows_list(read_text, 2) && !poll.has_cleared && poll_take_events(&poll) == 0,
	      "while the clear runs the list stays shown");
	now += 1000;
	check(until(POLL_RESULT) && poll.asked_result_seq == 43, "the state shows the clear done: its result is asked for");
	answer();
	check(poll.flow.phase == DTC_FLOW_CLEARED && poll.has_cleared && poll.cleared.clear && poll.cleared.dtc_count == 1 && poll.cleared.code_count == 1 &&
	      strcmp(poll.cleared.codes[0].code, "9301") == 0 && poll.cleared.ecus[0].cleared == 1, "the result of the own clear is the outcome");
	check(!poll.has_list && old_is(read_text, 2) && poll_take_events(&poll) == POLL_EVENT_LISTS, "with the outcome the list is dropped; it lives on as the old list");
	exchange();
	seconds(2);
	check(sent("SRV SV SV") && poll.has_cleared && poll.flow.phase == DTC_FLOW_CLEARED && poll_take_events(&poll) == 0, "the outcome stays while the rounds go on");

	// An event that waits to be taken when the clear goes out
	scene_list();
	seconds(24);
	check(poll.events == POLL_EVENT_CATALOG && poll_clear(&poll, false, now) == DTC_FLOW_ALLOWED, "the scene: the catalogue is to be stored, and the user confirms the clear");
	check(send() && request.kind == POLL_DTC_CLEAR && poll.events == POLL_EVENT_CATALOG, "the clear goes out: the event of the catalogue still waits, alone");
	answer();
	check(poll_take_events(&poll) == (POLL_EVENT_CATALOG | POLL_EVENT_OLD | POLL_EVENT_LISTS), "the events of a clear that is accepted join those that wait to be taken");

	// The next read
	scene_cleared();
	check(poll_read(&poll, now) == DTC_FLOW_ALLOWED && !poll.has_cleared && old_is(read_text, 2) && poll_take_events(&poll) == POLL_EVENT_LISTS,
	      "a read drops the outcome of the clear at once; the old list stays");
	scene_list();
	second();
	check(poll_read(&poll, now) == DTC_FLOW_ALLOWED && !poll.has_list && !poll.has_old && poll_take_events(&poll) == POLL_EVENT_LISTS, "a read drops the list that was shown at once");
	scene_list();
	wican.rpm = 780;
	second();
	check(poll_read(&poll, now) == DTC_FLOW_ENGINE_RUNNING && shows_list(read_text, 2) && poll.flow.phase == DTC_FLOW_LIST && poll_take_events(&poll) == 0,
	      "a read that is refused keeps the list: the engine speed is taken from the values of the struct");
	check(poll_clear(&poll, false, now) == DTC_FLOW_ENGINE_RUNNING && poll.flow.phase == DTC_FLOW_LIST, "a clear with the engine running is refused");

	// The time of the call is what the flow judges with
	scene();
	now = 112000;
	check(poll_read(&poll, now) == DTC_FLOW_RPM_UNKNOWN && poll.flow.phase == DTC_FLOW_IDLE, "a read 10000 ms after the last engine speed arrived is refused: the value is gone");
	now = 111999;
	check(!fresh("ENGINE_RPM") && poll_read(&poll, now) == DTC_FLOW_ALLOWED, "a read 9999 ms after the last engine speed arrived is allowed: the value is not fresh, but not gone");

	// A list without codes
	scene();
	wican.read_text = empty_text;
	wican.memory = 0;
	poll_read(&poll, now);
	exchange();
	seconds(5);
	check(poll.has_list && strcmp(poll.list_text, empty_text) == 0 && poll.list.ecu_count == 18 && poll.list.dtc_count == 0 && poll.flow.list_count == 0,
	      "a read without codes: the list of the 18 control units");
	check(poll_clear(&poll, false, now) == DTC_FLOW_NO_CODES && !send(), "a list without codes is not cleared: nothing is sent");

	// A profile without an engine speed: the catalogue of the struct decides
	adapter_init(&wican, 0);
	wican.has_rpm = false;
	join(OWN, 100000);
	exchange();
	seconds(2);
	check(catalog_find(&poll.catalog, "ENGINE_RPM") < 0 && value("ENGINE_RPM") == NULL && poll_read(&poll, now) == DTC_FLOW_ALLOWED,
	      "a profile without ENGINE_RPM: a read is allowed without a value of it");
}

// The own read of the scene, its POST answered with this
static void read_answered(int status, const char *body)
{
	scene();
	poll_read(&poll, now);
	send();
	reply(status, body, NULL);
}

// A POST that ended and whose fate the states have to tell
static bool undecided(dtc_flow_phase_t phase)
{
	return poll.flow.phase == phase && poll.flow.posted && poll.flow.seq == 0 && poll.flow.to_send == DTC_FLOW_SEND_NOTHING;
}

static bool refusal_without_body(void)
{
	scene();
	poll_read(&poll, now);
	send();
	poll_apply(&poll, &request, 409, NULL, 99, NULL, now, work, POLL_TOKENS);
	return reason_is("http_409");
}

static void test_post(void)
{
	static const struct
	{
		const char *file;
		int status;
		const char *reason;
	} refusals[] = {
		{"../../tools/w906/fixtures/api_body_busy.json", 409, "busy"},
		{"../../tools/w906/fixtures/api_body_read_required.json", 409, "read_required"},
		{"../../tools/w906/fixtures/api_body_stale_seq.json", 409, "stale_seq"},
		{"../../tools/w906/fixtures/api_body_nothing_to_clear.json", 409, "nothing_to_clear"},
		{"../../tools/w906/fixtures/api_body_not_ready.json", 503, "not_ready"},
		{"../../tools/w906/fixtures/api_body_forbidden.json", 403, "forbidden"},
		{"../../tools/w906/fixtures/api_body_bad_request.json", 400, "bad_request"},
	};
	static const char *const no_numbers[] = {
		"{\"accepted\":true}", "{\"accepted\":true,\"seq\":0}", "{\"accepted\":true,\"seq\":4294967296}", "{\"accepted\":true,\"seq\":-1}",
		"{\"accepted\":true,\"seq\":43.0}", "{\"accepted\":true,\"seq\":4.3e1}", "{\"accepted\":true,\"seq\":\"43\"}", "{\"accepted\":true,\"seq\":true}",
		"[{\"seq\":43}]", "{\"accepted\":true,\"seq\":43", "", "43", "{\"accepted\":true,\"seq\":99999999999999999999}", "{\"accepted\":true,\"seq\":4294967339}",
	};
	static const struct
	{
		int status;
		const char *body;
		const char *reason;
	} reasons[] = {
		{409, NULL, "http_409"},
		{409, "", "http_409"},
		{409, "{\"accepted\":false,\"seq\":42}", "http_409"},
		{409, "{\"accepted\":false,\"reason\":\"\",\"seq\":42}", "http_409"},
		{409, "{\"accepted\":false,\"reason\":7,\"seq\":42}", "http_409"},
		{409, "{\"accepted\":false,\"reason\":[\"busy\"],\"seq\":42}", "http_409"},
		{409, "{\"accepted\":false,\"reason\":\"busy\",\"seq\":42", "http_409"},
		{409, "[{\"reason\":\"busy\"}]", "http_409"},
		{409, "{\"reason\":\"a_reason_of_exactly_31_bytes_xx\"}", "a_reason_of_exactly_31_bytes_xx"},
		{409, "{\"reason\":\"a_reason_of_exactly_32_bytes_xxx\"}", "http_409"},
		{409, "{\"reason\":\"not\\u005fready\"}", "not_ready"},
		{409, "{\"reason\":\"not\\u0000ready\"}", "http_409"},
		{409, "{\"reason\":\"busy\",\"seq\":\"x\"}", "busy"},
		{500, "Internal Server Error", "http_500"},
		{404, NOT_FOUND_TEXT, "http_404"},
		{200, "{\"accepted\":true,\"seq\":43}", "http_200"},
		{201, "{\"accepted\":true,\"seq\":43}", "http_201"},
		{203, "{\"accepted\":true,\"seq\":43}", "http_203"},
		{100, NULL, "http_100"},
		{1, NULL, "http_1"},
		{599, NULL, "http_599"},
		{INT_MAX, NULL, "http_2147483647"},
	};
	static char text[128];
	const char *reason;
	uint32_t number;
	int wrong_read = 0, wrong_clear = 0, wrong = 0;
	size_t i;

	for(i = 0; i < sizeof(refusals) / sizeof(refusals[0]); i++)
	{
		if(!read_fixture(refusals[i].file, text, sizeof(text))) wrong_read++;

		read_answered(refusals[i].status, text);
		if(!reason_is(refusals[i].reason) || poll.has_list || poll.has_old || poll_take_events(&poll) != 0) wrong_read++;

		scene_list();
		second();
		poll_clear(&poll, false, now);
		send();
		poll_take_events(&poll);
		reply(refusals[i].status, text, NULL);
		if(!reason_is(refusals[i].reason) || poll.has_list || poll.has_old || poll_take_events(&poll) != POLL_EVENT_LISTS) wrong_clear++;
	}
	check(wrong_read == 0, "each of the seven refusals of API.md ends the own read as failed with its reason");
	check(wrong_clear == 0, "each of the seven refusals ends the own clear as failed with its reason: the list is dropped and is not the old list, nothing was cleared");

	// The adapter itself refuses
	scene();
	adapter_request(&wican, false, false, 0, now, &number, &reason);
	poll_read(&poll, now);
	exchange();
	check(sent("r") && reason_is("busy") && poll.http_ok == 8, "somebody started a scan the display has not seen yet: the read is refused, busy; a 409 is an answer");
	scene();
	wican.sleep_in_s = 0;
	poll_read(&poll, now);
	exchange();
	check(reason_is("not_ready") && poll.http_ok == 7 && poll.http_failed == 1, "the adapter is due to sleep: the read is refused, not_ready; a 503 counts as failed");
	scene_list();
	second();
	poll_clear(&poll, false, now);
	wican.ended_ms = now - 600001;
	exchange();
	check(reason_is("read_required") && !poll.has_list && !poll.has_old, "the adapter finds the read older than 600 s: the clear is refused, read_required");

	// The number of an accepted request
	if(!read_fixture("../../tools/w906/fixtures/api_body_accepted.json", text, sizeof(text))) wrong++;
	read_answered(202, text);
	check(wrong == 0 && poll.flow.phase == DTC_FLOW_READING && poll.flow.seq == 43 && poll.flow.posted, "202 with the body of API.md: accepted with the number 43");
	read_answered(202, "{\"accepted\":true,\"seq\":4294967295}");
	check(poll.flow.phase == DTC_FLOW_READING && poll.flow.seq == 4294967295u, "202 with the number 4294967295: accepted with it");
	read_answered(202, "{\"accepted\":false,\"reason\":\"busy\",\"seq\":43}");
	check(poll.flow.phase == DTC_FLOW_READING && poll.flow.seq == 43, "202 with a number and a reason: the status says accepted");
	for(i = 0; i < sizeof(no_numbers) / sizeof(no_numbers[0]); i++)
	{
		read_answered(202, no_numbers[i]);
		if(!undecided(DTC_FLOW_READ_SENT))
		{
			printf("  202 with '%s': phase %d, number %lu\n", no_numbers[i], (int)poll.flow.phase, (unsigned long)poll.flow.seq);
			wrong++;
		}
	}
	read_answered(202, NULL);
	check(wrong == 0 && undecided(DTC_FLOW_READ_SENT), "202 without a number that is one from 1 to 4294967295 written in digits: as good as no answer");

	// The reason of a refusal
	for(i = 0; i < sizeof(reasons) / sizeof(reasons[0]); i++)
	{
		read_answered(reasons[i].status, reasons[i].body);
		if(!reason_is(reasons[i].reason))
		{
			printf("  %d with '%s': phase %d, reason '%s'\n", reasons[i].status, reasons[i].body != NULL ? reasons[i].body : "(null)", (int)poll.flow.phase, poll.flow.reason);
			wrong++;
		}
	}
	check(wrong == 0, "a refusal has the reason of its body if that is a text of at most 31 bytes, else http_ and the status");

	// No answer
	read_answered(0, "{\"accepted\":true,\"seq\":43}");
	check(undecided(DTC_FLOW_READ_SENT) && poll.http_failed == 1, "status 0 is no answer, whatever the body says");
	read_answered(0, "{\"accepted\":false,\"reason\":\"busy\",\"seq\":42}");
	check(undecided(DTC_FLOW_READ_SENT), "status 0 with the body of a refusal is no refusal");
	read_answered(-1, "{\"accepted\":false,\"reason\":\"busy\",\"seq\":42}");
	check(undecided(DTC_FLOW_READ_SENT) && poll.http_failed == 1 && poll.http_ok == 7, "a negative status is no answer either, not a refusal");
	read_answered(INT_MIN, NULL);
	check(undecided(DTC_FLOW_READ_SENT), "the smallest status there is: no answer");

	// The body ends at its length
	scene();
	poll_read(&poll, now);
	send();
	poll_apply(&poll, &request, 202, "{\"accepted\":true,\"seq\":43}xyz", 26, NULL, now, work, POLL_TOKENS);
	check(poll.flow.phase == DTC_FLOW_READING && poll.flow.seq == 43, "the body of a POST is read up to its length: what stands behind it is not looked at");
	scene();
	poll_read(&poll, now);
	send();
	poll_apply(&poll, &request, 202, "{\"accepted\":true,\"seq\":43}", 25, NULL, now, work, POLL_TOKENS);
	check(undecided(DTC_FLOW_READ_SENT), "the body of a POST passed one byte short cannot be read: no number");
	check(survives(refusal_without_body), "a body that is NULL counts as an empty one, whatever its length is said to be");
}

static void test_no_answer(void)
{
	answer_t lost;
	const char *reason;
	uint32_t number;

	// The read arrived, its answer did not
	scene();
	poll_read(&poll, now);
	send();
	adapter_answer(&wican, &request, now, &lost);
	reply(0, NULL, NULL);
	check(undecided(DTC_FLOW_READ_SENT) && wican.seq == 42, "the scene: the adapter accepted the read, the display got no answer");
	second();
	check(poll.flow.phase == DTC_FLOW_READING && poll.flow.seq == 42, "the next state shows a new request from HTTP, a read: it is the own one");
	seconds(3);
	check(sent("r S S S SRV") && poll.flow.phase == DTC_FLOW_LIST && shows_list(read_text, 2), "the read without an answer goes on to its list");

	// The read did not arrive
	scene();
	poll_read(&poll, now);
	send();
	reply(0, NULL, NULL);
	second();
	check(undecided(DTC_FLOW_READ_SENT), "the first state without a new request: the read may still arrive");
	second();
	check(sent("r SV SV") && reason_is("no_answer") && wican.seq == 0, "the second state without a new request: the read did not arrive, and it is not sent again");

	// The clear did not arrive
	scene_list();
	second();
	poll_clear(&poll, false, now);
	send();
	reply(0, NULL, NULL);
	seconds(2);
	check(poll.flow.phase == DTC_FLOW_LIST && shows_list(read_text, 2) && !poll.has_old && poll_take_events(&poll) == 0,
	      "two states without a new request: the clear did not arrive - the list is back and was never dropped, nothing became the old list");
	check(poll_clear(&poll, false, now) == DTC_FLOW_ALLOWED && send() && request.kind == POLL_DTC_CLEAR && strcmp(request.path, "/api/dtc?action=clear&seq=42") == 0 &&
	      !poll.has_old && poll_take_events(&poll) == 0, "the user confirms again: the clear goes out again");
	answer();
	check(old_is(read_text, 2) && poll_take_events(&poll) == (POLL_EVENT_OLD | POLL_EVENT_LISTS), "the second clear is accepted: now its list is the old list");
	seconds(4);
	check(poll.flow.phase == DTC_FLOW_CLEARED && poll.has_cleared && !poll.has_list, "the second clear ends with its outcome");

	// The clear arrived, its answer did not
	scene_list();
	second();
	poll_clear(&poll, false, now);
	send();
	adapter_answer(&wican, &request, now, &lost);
	reply(-1, NULL, NULL);
	check(undecided(DTC_FLOW_CLEAR_SENT) && shows_list(read_text, 2) && wican.seq == 43 && !poll.has_old && poll_take_events(&poll) == 0,
	      "the scene: the adapter accepted the clear, the display got no status; nothing became the old list");
	second();
	check(poll.flow.phase == DTC_FLOW_CLEARING && poll.flow.seq == 43 && shows_list(read_text, 2), "the next state shows a new clear from HTTP: it is the own one");
	check(old_is(read_text, 2) && poll_take_events(&poll) == (POLL_EVENT_OLD | POLL_EVENT_LISTS), "with the state that shows the own clear its list is the old list, POLL_EVENT_OLD");
	seconds(3);
	check(poll.flow.phase == DTC_FLOW_CLEARED && poll.has_cleared && poll.cleared.dtc_count == 1 && !poll.has_list, "the clear without an answer goes on to its outcome");

	// Somebody else got in
	scene();
	poll_read(&poll, now);
	send();
	reply(0, NULL, NULL);
	adapter_request(&wican, false, false, 0, now, &number, &reason);
	second();
	check(reason_is("superseded"), "a read without an answer and a state with a request from MQTT: the own one cannot be found");
	scene_list();
	second();
	poll_clear(&poll, false, now);
	send();
	reply(0, NULL, NULL);
	poll_take_events(&poll);
	adapter_request(&wican, true, false, 0, now, &number, &reason);
	second();
	check(poll.flow.phase == DTC_FLOW_UNKNOWN && !poll.has_list && old_is(read_text, 2) && poll_take_events(&poll) == (POLL_EVENT_OLD | POLL_EVENT_LISTS),
	      "a clear without an answer and a state with a clear from MQTT: unknown - the list is dropped and is the old list, it may have been cleared");

	// The clear arrived, its answer did not, and the first state shows that it ended with an error
	scene_list();
	second();
	poll_clear(&poll, false, now);
	send();
	adapter_answer(&wican, &request, now, &lost);
	reply(0, NULL, NULL);
	wican.rpm = 780;
	second();
	check(reason_is("engine_running") && poll.flow.seq == 43 && !poll.has_list, "a clear without an answer and a state that shows it accepted and ended with an error: failed with that reason");
	check(old_is(read_text, 2) && poll_take_events(&poll) == (POLL_EVENT_OLD | POLL_EVENT_LISTS), "the clear was accepted, so its list is the old list although the display never saw it running");
}

static void test_read_error(void)
{
	scene();
	poll_read(&poll, now);
	exchange();
	wican.ignition = false;
	second();
	check(reason_is("ecu_offline") && view() == CONN_VIEW_ECU_OFFLINE && !poll.has_list, "the state shows the own read ended with an error: failed with the reason of the state");
	seconds(2);
	check(sent("r S S S") && reason_is("ecu_offline"), "with the ignition off no values are asked for; the failure stays until the user leaves it");

	scene();
	wican.pickup_ms = 25000;
	poll_read(&poll, now);
	exchange();
	seconds(24);
	check(poll.flow.phase == DTC_FLOW_READING && view() == CONN_VIEW_SCAN, "a read the adapter does not pick up stays queued: the display waits");
	second();
	check(reason_is("expired"), "the adapter drops the read it picked up too late: failed, expired");

	scene_list();
	second();
	poll_clear(&poll, false, now);
	exchange();
	wican.rpm = 780;
	second();
	check(reason_is("engine_running") && !poll.has_list && old_is(read_text, 2), "the adapter finds the engine running: the own clear failed, the list is dropped");
}

// A copy of `text` with its first `from` replaced by `to`
static const char *patched(const char *text, const char *from, const char *to)
{
	static char out[1024];
	const char *at = strstr(text, from);

	if(at == NULL)
	{
		check(false, "the text to patch is in the fixture");
		return text;
	}
	snprintf(out, sizeof(out), "%.*s%s%s", (int)(at - text), text, to, at + strlen(from));
	return out;
}

// The first answer to GET /api/state after the display joined at 5000
static void first_state(const char *bound_id, int status, const char *body)
{
	join(bound_id, 5000);
	send();
	reply(status, body, NULL);
}

// The battery voltage the display has after a first state with this batt_v, -1 if it has none
static double volts_of(const char *batt_v)
{
	char member[48];
	const value_t *volts;

	snprintf(member, sizeof(member), "\"batt_v\":%s", batt_v);
	first_state(NULL, 200, patched(state_example, "\"batt_v\":12.4", member));
	volts = value("@BATT_V");
	if(volts == NULL) return -1;
	return volts->kind == VALUE_NUMBER && volts->seen_ms == 5000 && poll.values.count == 1 ? volts->number : -2;
}

static bool state_without_body(void)
{
	join(NULL, 5000);
	send();
	poll_apply(&poll, &request, 200, NULL, strlen(state_example), NULL, now, work, POLL_TOKENS);
	return conn_state(&poll.conn) == NULL && poll.conn.failed_rounds == 1 && poll.http_ok == 1;
}

static void test_state(void)
{
	static const struct
	{
		int status;
		const char *body;
	} failures[] = {
		{200, "{}"}, {200, ""}, {200, "Nothing"}, {200, "[]"}, {500, NULL}, {503, NULL}, {204, NULL}, {403, NULL}, {202, NULL}, {0, NULL}, {-1, NULL},
		{201, NULL}, {199, NULL}, {400, NULL}, {405, NULL},
	};
	static char longer[600];
	const wican_state_t *state;
	int wrong = 0;
	size_t i;

	first_state(NULL, 200, state_example);
	state = conn_state(&poll.conn);
	check(state != NULL && state->boot == 1234567890 && state->up_s == 812 && state->pass == 1234 && view() == CONN_VIEW_LIVE && poll.http_ok == 1 && poll.http_failed == 0,
	      "200 with the state of API.md: the connection has it, the answer is counted");
	check(strcmp(poll.bound_id, "a1b2c3d4e5f6") == 0 && poll_take_events(&poll) == POLL_EVENT_BOUND, "the first state of an adapter binds a display that is not bound: its id and POLL_EVENT_BOUND");
	check(poll.values.count == 1 && number_of("@BATT_V") == 12.4 && seen_at("@BATT_V") == 5000 &&
	      !poll.values.has_pass, "the battery voltage of the state is the value @BATT_V in volts, seen now; the pass counter is not touched");
	check(send() && request.kind == POLL_CATALOG, "behind the state the round goes on");
	first_state(OWN, 200, state_example);
	check(poll_take_events(&poll) == 0 && strcmp(poll.bound_id, OWN) == 0 && value("@BATT_V") != NULL, "the state of the adapter the display is bound to: no event");
	first_state(NULL, 200, patched(state_example, "\"id\":\"a1b2c3d4e5f6\"", "\"id\":\"0123456789abcdef0123456789abcdef\""));
	check(strcmp(poll.bound_id, "0123456789abcdef0123456789abcdef") == 0 && poll_take_events(&poll) == POLL_EVENT_BOUND && view() == CONN_VIEW_LIVE,
	      "an adapter with an id of 32 bytes, the longest there is: the display binds to it");

	check(volts_of("0.005") == 0.005 && volts_of("12.05") == 12.05 && volts_of("0.1") == 0.1 && volts_of("7") == 7.0 && volts_of("100") == 100.0 &&
	      volts_of("2147483.647") == 2147483.647, "the voltage is taken to the millivolt: 0.005, 12.05, 0.1, 7, 100 and 2147483.647 V arrive as these numbers");
	check(volts_of("0") == 0.0 && volts_of("0.0") == 0.0 && volts_of("-0.0") == 0.0, "a voltage of 0 is a value");
	check(volts_of("12.4004") == 12.4 && volts_of("12.4009") == 12.4, "digits below a millivolt are dropped");
	check(volts_of("-1") == -1 && volts_of("-0.1") == -1, "a voltage that was not measured is no value");
	first_state(NULL, 200, state_starting);
	check(view() == CONN_VIEW_STARTING && value("@BATT_V") == NULL && poll.values.count == 0 && strcmp(poll.bound_id, "a1b2c3d4e5f6") == 0,
	      "the state of an adapter that is starting, without a voltage: bound, no value");
	first_state(NULL, 200, state_limits);
	check(value("@BATT_V") != NULL && number_of("@BATT_V") == 2147483.6 && strcmp(poll.bound_id, "ffffffffffff") == 0 && conn_state(&poll.conn)->dtc.seq == 2147483647,
	      "the state with every number at its largest is taken");

	// Another adapter
	first_state(OWN, 200, state_scan);
	check(view() == CONN_VIEW_FOREIGN && poll.values.count == 0 && value("@BATT_V") == NULL && strcmp(poll.bound_id, OWN) == 0 && poll_take_events(&poll) == 0,
	      "the state of another adapter than the one the display is bound to: foreign, its voltage is no value, nothing is bound");
	check(!send(), "nothing else is asked of a foreign adapter");
	first_state(NULL, 200, state_scan);
	check(view() == CONN_VIEW_SCAN && strcmp(poll.bound_id, "0123456789ab") == 0 && value("@BATT_V") != NULL && number_of("@BATT_V") == 14.4,
	      "the same state for a display that is not bound: it binds, the voltage is a value");

	// No API, no answer
	first_state(NULL, 404, NOT_FOUND_TEXT);
	check(view() == CONN_VIEW_NO_API && conn_state(&poll.conn) == NULL && poll.values.count == 0 && poll.bound_id[0] == '\0' && poll.http_ok == 1 && poll_take_events(&poll) == 0,
	      "404: a firmware without the API; the answer is counted");
	first_state(NULL, 404, state_example);
	check(view() == CONN_VIEW_NO_API && conn_state(&poll.conn) == NULL && poll.values.count == 0 && poll.bound_id[0] == '\0', "404 with a state as its body: without the API all the same");
	for(i = 0; i < sizeof(failures) / sizeof(failures[0]); i++)
	{
		first_state(NULL, failures[i].status, failures[i].body != NULL ? failures[i].body : state_example);
		if(poll.conn.failed_rounds != 1 || conn_state(&poll.conn) != NULL || poll.conn.no_api || poll.values.count != 0 || poll.bound_id[0] != '\0' || poll_take_events(&poll) != 0 ||
		   poll.asking || send())
		{
			printf("  %d with '%s' is not a failed round\n", failures[i].status, failures[i].body != NULL ? failures[i].body : "the state of API.md");
			wrong++;
		}
	}
	first_state(NULL, 200, patched(state_example, "\"api\":1", "\"api\":2"));
	check(wrong == 0 && poll.conn.failed_rounds == 1 && conn_state(&poll.conn) == NULL && poll.bound_id[0] == '\0',
	      "a body that is no state of API 1, and every status but 200 and 404 also with a state as its body: the round has failed, nothing is taken");
	first_state(NULL, 500, state_example);
	check(poll.http_ok == 0 && poll.http_failed == 1, "a state with the status 500 counts as a failed answer");
	first_state(NULL, 204, state_example);
	check(poll.http_ok == 1 && poll.http_failed == 0, "a state with the status 204 is no state, but an answer");

	// The body ends at its length
	snprintf(longer, sizeof(longer), "%s,{\"more\":1}", state_example);
	join(NULL, 5000);
	send();
	poll_apply(&poll, &request, 200, longer, strlen(state_example), NULL, now, work, POLL_TOKENS);
	check(conn_state(&poll.conn) != NULL && value("@BATT_V") != NULL, "a state is read up to its length: what stands behind it is not looked at");
	join(NULL, 5000);
	send();
	poll_apply(&poll, &request, 200, state_example, strlen(state_example) - 1, NULL, now, work, POLL_TOKENS);
	check(conn_state(&poll.conn) == NULL && poll.conn.failed_rounds == 1 && poll.values.count == 0, "a state passed one byte short cannot be read: the round has failed");
	check(survives(state_without_body), "a state that is NULL is an empty body, whatever its length is said to be");

	// Which answers count
	first_state(NULL, 200, "");
	check(poll.http_ok == 1 && poll.http_failed == 0, "an answer with the status 200 is counted in http_ok");
	first_state(NULL, 199, "");
	check(poll.http_ok == 0 && poll.http_failed == 1, "an answer with the status 199 is counted in http_failed");
	first_state(NULL, 499, "");
	check(poll.http_ok == 1 && poll.http_failed == 0, "an answer with the status 499 is counted in http_ok");
	first_state(NULL, 500, "");
	check(poll.http_ok == 0 && poll.http_failed == 1, "an answer with the status 500 is counted in http_failed");
	first_state(NULL, 0, "");
	check(poll.http_ok == 0 && poll.http_failed == 1, "no answer is counted in http_failed");
	first_state(NULL, -1, "");
	check(poll.http_ok == 0 && poll.http_failed == 1, "a negative status is counted in http_failed");
}

// The own read 42 is done and its result is asked for at 106000
static void scene_result(void)
{
	scene();
	poll_read(&poll, now);
	exchange();
	seconds(3);
	now += 1000;
	until(POLL_RESULT);
	poll_take_events(&poll);
	trace[0] = '\0';
}

// The own clear 43 is done and its result is asked for at 111000
static void scene_clear_result(void)
{
	scene_clearing();
	seconds(3);
	now += 1000;
	until(POLL_RESULT);
	poll_take_events(&poll);
	trace[0] = '\0';
}

// The header of the scenes with the read 42, for the clear 43: one number higher
static const char *clear_header(const char *header)
{
	static char out[16];
	char *at;

	if(header == NULL) return NULL;
	strcpy(out, header);
	at = strstr(out, "42");
	if(at != NULL) at[1] = '3';
	else if(strcmp(out, "43") == 0) out[1] = '4';
	return out;
}

// The result that was asked for cannot be had: conn does not ask for it again and the round goes on
static bool result_gone(void)
{
	return poll.conn.fetched_result_seq == wican.result_seq && poll.conn.failed_rounds == 0 && !poll.asking && !poll.has_list && !poll.has_cleared;
}

// A profile without ENGINE_RPM. Somebody else's read 42 is done at 106000; the user reads right after the
// state that shows it, so that the result 42 is asked for while the own read 43 waits.
static bool scene_other_result(void)
{
	const char *reason;
	uint32_t number;
	bool allowed;

	adapter_init(&wican, 0);
	wican.has_rpm = false;
	wican.read_text = read_text;
	join(OWN, 100000);
	exchange();
	seconds(2);
	adapter_request(&wican, false, false, 0, now, &number, &reason);
	seconds(3);
	now += 1000;
	send();
	answer();
	allowed = poll_read(&poll, now) == DTC_FLOW_ALLOWED && send() && request.kind == POLL_DTC_READ;
	answer();
	poll_take_events(&poll);
	return allowed && send() && request.kind == POLL_RESULT && poll.asked_result_seq == 42 && poll.flow.phase == DTC_FLOW_READING && poll.flow.seq == 43;
}

static bool result_without_room(void)
{
	scene_result();
	poll_apply(&poll, &request, 200, padded_result(read_text, POLL_TEXT_SIZE), POLL_TEXT_SIZE, "42", now, work, POLL_TOKENS);
	if(!result_gone() || !reason_is("no_result")) return false;
	scene_result();
	poll_apply(&poll, &request, 200, padded_result(read_text, POLL_TEXT_SIZE + 1), POLL_TEXT_SIZE + 1, "42", now, work, POLL_TOKENS);
	if(!result_gone() || !reason_is("no_result")) return false;
	scene_clear_result();
	poll_apply(&poll, &request, 200, padded_result(clear_text, POLL_TEXT_SIZE), POLL_TEXT_SIZE, "43", now, work, POLL_TOKENS);
	return result_gone() && poll.flow.phase == DTC_FLOW_UNKNOWN;
}

static void test_result(void)
{
	static const struct
	{
		int status;
		const char *body;       // NULL: the result of the read
		const char *header;
	} gone[] = {
		{204, "", NULL}, {204, NULL, "42"}, {503, "{\"accepted\":false,\"reason\":\"not_ready\",\"seq\":0}", NULL}, {503, NULL, "42"},
		{200, NULL, NULL}, {200, NULL, "43"}, {200, NULL, "41"}, {200, NULL, " 42"}, {200, NULL, "42 "}, {200, NULL, "042"}, {200, NULL, "+42"}, {200, NULL, "42.0"},
		{200, NULL, ""}, {200, NULL, "4"}, {200, NULL, "424"}, {200, NULL, "0x2A"}, {200, NULL, "42\n"}, {200, NULL, "4 2"}, {200, NULL, "0"},
		{200, "{\"state\":\"done\"}", "42"}, {200, "", "42"}, {200, "[]", "42"},
		{404, NULL, "42"}, {500, NULL, "42"}, {202, NULL, "42"}, {201, NULL, "42"}, {199, NULL, "42"}, {1, NULL, "42"}, {409, NULL, "42"},
	};
	static char padded[POLL_TEXT_SIZE + 64];
	poll_request_t other;
	const char *reason;
	uint32_t number;
	size_t length = strlen(read_text);
	int wrong = 0;
	size_t i;

	for(i = 0; i < sizeof(gone) / sizeof(gone[0]); i++)
	{
		scene_result();
		reply(gone[i].status, gone[i].body != NULL ? gone[i].body : read_text, gone[i].header);
		if(!result_gone() || !reason_is("no_result") || poll_take_events(&poll) != 0 || !send() || request.kind != POLL_VALUES)
		{
			printf("  a read: %d with the header '%s' and the body '%.20s'\n", gone[i].status, gone[i].header != NULL ? gone[i].header : "(none)",
			       gone[i].body != NULL ? gone[i].body : "(the result)");
			wrong++;
		}
		scene_clear_result();
		reply(gone[i].status, gone[i].body != NULL ? gone[i].body : clear_text, clear_header(gone[i].header));
		if(!result_gone() || poll.flow.phase != DTC_FLOW_UNKNOWN || !old_is(read_text, 2) || poll_take_events(&poll) != POLL_EVENT_LISTS || !send() || request.kind != POLL_VALUES)
		{
			printf("  a clear: %d with the header '%s' and the body '%.20s'\n", gone[i].status, gone[i].header != NULL ? gone[i].header : "(none)",
			       gone[i].body != NULL ? gone[i].body : "(the result)");
			wrong++;
		}
	}
	check(wrong == 0, "204, 503, a result without the header, with another number or with more than its digits, a body that is no result, any other status: "
	                  "the result cannot be had - conn goes on, the own read failed with no_result, the outcome of the own clear is unknown");
	scene_result();
	reply(204, "", NULL);
	exchange();
	second();
	check(sent("V SV") && poll.http_ok == 16 && poll.http_failed == 0, "a result that cannot be had is not asked for again; a 204 is an answer");
	scene_result();
	reply(503, NULL, NULL);
	check(poll.http_ok == 12 && poll.http_failed == 1 && poll.conn.failed_rounds == 0, "a 503 for a result counts as a failed answer, but it does not fail the round");

	// The wrong action under the right number
	scene_result();
	reply(200, clear_text, "42");
	check(result_gone() && reason_is("no_result") && poll_take_events(&poll) == 0, "the result of a clear under the number of the own read: fetched, but no list - the read failed, no_result");
	scene_clear_result();
	reply(200, read_text, "43");
	check(result_gone() && poll.flow.phase == DTC_FLOW_UNKNOWN && poll_take_events(&poll) == POLL_EVENT_LISTS,
	      "the result of a read under the number of the own clear: fetched, but no outcome - unknown, the list is dropped");

	// How short a result may be
	scene_result();
	reply(200, SHORTEST_RESULT, "42");
	check(poll.flow.phase == DTC_FLOW_LIST && poll.has_list && strcmp(poll.list_text, SHORTEST_RESULT) == 0 && poll.list.ecu_count == 0 && poll.list.duration_ms == 1 &&
	      poll.flow.list_count == 0, "the shortest result there is, without any control unit, is a list");

	// How long a result may be
	memset(padded, ' ', sizeof(padded) - 1);
	memcpy(padded, read_text, length);
	scene_result();
	poll_apply(&poll, &request, 200, padded, POLL_TEXT_SIZE - 1, "42", now, work, POLL_TOKENS);
	check(poll.flow.phase == DTC_FLOW_LIST && poll.has_list && strlen(poll.list_text) == POLL_TEXT_SIZE - 1 && memcmp(poll.list_text, padded, POLL_TEXT_SIZE - 1) == 0 &&
	      poll.list.dtc_count == 2, "a result of 5199 bytes fills list_text to its last byte");
	check(survives(result_without_room), "a result of 5200 bytes has no room in list_text: it cannot be had, also the result of a clear, which needs no text");

	// The body ends at its length
	memset(padded, 'x', sizeof(padded) - 1);
	memcpy(padded, read_text, length);
	scene_result();
	memset(poll.list_text, 'y', sizeof(poll.list_text));
	poll_apply(&poll, &request, 200, padded, length, "42", now, work, POLL_TOKENS);
	check(shows_list(read_text, 2) && poll.list_text[length + 1] == 'y', "a result followed by other bytes: the list and its text end at its length");
	scene_result();
	poll_apply(&poll, &request, 200, read_text, length - 1, "42", now, work, POLL_TOKENS);
	check(result_gone() && reason_is("no_result"), "a result passed one byte short cannot be read: no_result");

	// No answer
	scene_result();
	reply(0, read_text, "42");
	check(poll.flow.phase == DTC_FLOW_READING && poll.conn.failed_rounds == 1 && poll.conn.fetched_result_seq == 0 && !poll.has_list && !send() && poll.http_failed == 1,
	      "status 0 for the result, whatever body and header say: the round has failed, the read goes on waiting");
	now = 107000;
	check(until(POLL_RESULT) && poll.asked_result_seq == 42 && poll.asked_age_s == 1, "the result that got no answer is asked for again in the next round, with the age of its state");
	reply(-1, read_text, "42");
	check(poll.flow.phase == DTC_FLOW_READING && poll.conn.failed_rounds == 2 && poll.http_failed == 2, "a negative status for the result is no answer either");
	now = 109000;
	until(POLL_RESULT);
	reply(0, NULL, NULL);
	now = 114000;
	check(until(POLL_RESULT) && poll.asked_age_s == 8 && poll.flow.phase == DTC_FLOW_READING && !poll.lost, "three rounds failed within the grace time: the read still waits for its result");
	answer();
	check(poll.flow.phase == DTC_FLOW_LIST && poll.flow.list_end_ms == 106000 && shows_list(read_text, 2),
	      "a result that arrives 8 s after the read ended: the list ended at the time it arrived minus the age its state named");

	// Results of somebody else
	adapter_init(&wican, 0);
	wican.read_text = read_text;
	adapter_request(&wican, false, false, 0, 90000, &number, &reason);
	adapter_catch_up(&wican, 95000);
	join(OWN, 100000);
	exchange();
	check(sent("SRCV") && poll.conn.fetched_result_seq == 42 && poll.flow.phase == DTC_FLOW_IDLE && !poll.has_list && !poll.has_cleared && poll_take_events(&poll) == 0,
	      "the result of a read somebody else started is fetched and changes nothing shown");
	scene_cleared();
	adapter_request(&wican, false, false, 0, now, &number, &reason);
	seconds(4);
	check(sent("S S S SRV") && poll.conn.fetched_result_seq == 44 && poll.flow.phase == DTC_FLOW_CLEARED && poll.has_cleared && poll.cleared.clear && poll.cleared.dtc_count == 1 &&
	      !poll.has_list && poll_take_events(&poll) == 0, "the result of somebody else's read while the outcome of the own clear is shown: the outcome stays as it is");

	// The own read waits for another number than the result that is asked for
	check(scene_other_result(), "the scene: somebody else's read 42 is done, the user reads at once, and the result 42 is asked for while the own read 43 waits");
	other = request;
	reply(204, "", NULL);
	check(poll.flow.phase == DTC_FLOW_READING && poll.flow.seq == 43 && poll.conn.fetched_result_seq == 42, "a result that cannot be had does not end a read that waits for another number");
	seconds(4);
	check(poll.flow.phase == DTC_FLOW_LIST && poll.flow.read_seq == 43 && shows_list(read_text, 2), "the read that waited for another number gets its list");
	scene_other_result();
	request = other;
	reply(200, read_text, "42");
	check(poll.flow.phase == DTC_FLOW_READING && poll.flow.seq == 43 && poll.conn.fetched_result_seq == 42 && !poll.has_list && poll_take_events(&poll) == 0,
	      "the result of somebody else's read is no list for a read that waits for another number");
	scene_other_result();
	request = other;
	reply(200, clear_text, "42");
	check(poll.flow.phase == DTC_FLOW_READING && poll.flow.seq == 43 && !poll.has_cleared, "the result of somebody else's clear does not end a read that waits for another number");

	// The result is asked for by the number of the stored result, not by that of the last request
	scene_list();
	adapter_request(&wican, false, false, 0, now, &number, &reason);
	second();
	poll_wifi(&poll, false, now);
	now = 108000;
	poll_wifi(&poll, true, now);
	check(until(POLL_RESULT) && conn_state(&poll.conn)->dtc.seq == 43 && poll.asked_result_seq == 42 && poll.asked_age_s == 0,
	      "the state shows the scan 43 running and the stored result 42: the result is asked for with the number 42");
	answer();
	check(poll.conn.fetched_result_seq == 42 && !poll.has_list && !send(), "the result of the read before the running scan is fetched; nothing else is asked for while the scan runs");

	// The number of codes of a list is the one the result reports, not the number it lists
	scene();
	wican.read_text = shortened_text;
	poll_read(&poll, now);
	exchange();
	seconds(4);
	check(poll.flow.phase == DTC_FLOW_LIST && poll.flow.list_count == 165 && poll.list.dtc_count == 165 && poll.list.code_count == 10 && strcmp(poll.list_text, shortened_text) == 0,
	      "a shortened result with 165 codes of which it lists 10: the list has 165");
}

// The display joined at 100000 and got its first state; the profile is asked for
static void scene_catalog(void)
{
	join(OWN, 100000);
	send();
	answer();
	send();
}

static void test_catalog(void)
{
	static const char *const useless[] = {"[]", "", "{", "Failed to generate JSON", "7", "{\"ENGINE_RPM\":{\"class\":\"frequency\",\"unit\":\"RPM\"}"};
	static const int failing[] = {500, 503, 204, 202, 201, 199, 400, 403, 0, -1};
	static const int absent[] = {500, 503, 204, 404, 400, 202, 1};
	static char huge[POLL_BODY_SIZE + 16];
	int wrong = 0;
	uint32_t events;
	size_t i;

	adapter_init(&wican, 0);
	scene_catalog();
	check(request.kind == POLL_CATALOG && !poll.catalog_complete && poll.catalog.count == 1, "the scene: the profile is asked for, the catalogue is that of the start");
	answer();
	check(poll.catalog_complete && !poll.conn.want_catalog && poll.catalog.count == 4 && strcmp(poll.catalog.entries[2].name, "COOLANT_TMP") == 0 &&
	      strcmp(poll.catalog.entries[2].unit, "°C") == 0 && strcmp(poll.catalog.entries[2].value_class, "temperature") == 0 && poll.catalog.entries[2].in_profile &&
	      !poll.catalog.entries[2].delivered, "200 with a profile: it is the catalogue, which is complete now; conn has it for this connection");
	exchange();
	second();
	check(sent("SCV SV") && poll.catalog.entries[2].delivered && poll.catalog.count == 4, "the profile is asked for once; the values that arrive are marked in the catalogue");

	adapter_init(&wican, 0);
	scene_catalog();
	reply(200, "{}", NULL);
	check(poll.catalog_complete && !poll.conn.want_catalog && poll.catalog.count == 1, "200 with a profile without any value: taken, the catalogue is complete");

	// 200 with something that is no profile
	for(i = 0; i < sizeof(useless) / sizeof(useless[0]); i++)
	{
		adapter_init(&wican, 0);
		scene_catalog();
		reply(200, useless[i], NULL);
		if(poll.catalog_complete || poll.conn.want_catalog || poll.conn.failed_rounds != 0 || poll.catalog.count != 1 || !send() || request.kind != POLL_VALUES) wrong++;
	}
	adapter_init(&wican, 0);
	scene_catalog();
	reply(200, NULL, NULL);
	check(wrong == 0 && !poll.catalog_complete && !poll.conn.want_catalog && poll.conn.failed_rounds == 0 && poll.catalog.count == 1,
	      "200 with a body that is no profile: conn has its answer, the round goes on, but the catalogue is not complete");
	trace[0] = '\0';
	exchange();
	seconds(40);
	events = poll_take_events(&poll);
	check(strchr(trace, 'C') == NULL && poll.catalog.count == 4 && strcmp(poll.catalog.entries[1].name, "ENGINE_RPM") == 0 && poll.catalog.entries[1].delivered &&
	      !poll.catalog.entries[1].in_profile, "a profile that cannot be used is not asked for again: the catalogue is what the values bring");
	check(!poll.catalog_complete && (events & POLL_EVENT_CATALOG) == 0, "a catalogue that is not complete is never stored");
	adapter_init(&wican, 0);
	scene_catalog();
	poll_apply(&poll, &request, 200, CONFIG_RPM, strlen(CONFIG_RPM) - 1, NULL, now, work, POLL_TOKENS);
	check(!poll.catalog_complete && !poll.conn.want_catalog && poll.catalog.count == 1, "a profile passed one byte short cannot be used");
	snprintf(huge, sizeof(huge), "%s]]", CONFIG_RPM);
	adapter_init(&wican, 0);
	scene_catalog();
	poll_apply(&poll, &request, 200, huge, strlen(CONFIG_RPM), NULL, now, work, POLL_TOKENS);
	check(poll.catalog_complete && poll.catalog.count == 4, "a profile is read up to its length: what stands behind it is not looked at");

	// 404
	adapter_init(&wican, 0);
	scene_catalog();
	reply(404, NOT_FOUND_TEXT, NULL);
	check(!poll.catalog_complete && poll.conn.want_catalog && poll.conn.failed_rounds == 0 && send() && request.kind == POLL_VALUES, "404 for the profile: the round goes on with the values");
	answer();
	second();
	check(sent("SCV SCV") && poll.catalog_complete, "a profile that was not found is asked for again in the next round");
	adapter_init(&wican, 0);
	scene_catalog();
	reply(404, CONFIG_RPM, NULL);
	check(!poll.catalog_complete && poll.catalog.count == 1 && poll.conn.want_catalog, "404 with a profile as its body: not taken");

	// Anything else fails the round
	wrong = 0;
	for(i = 0; i < sizeof(failing) / sizeof(failing[0]); i++)
	{
		adapter_init(&wican, 0);
		scene_catalog();
		reply(failing[i], CONFIG_RPM, NULL);
		if(poll.catalog_complete || poll.catalog.count != 1 || !poll.conn.want_catalog || poll.conn.failed_rounds != 1 || send())
		{
			printf("  the status %d for the profile does not fail the round\n", failing[i]);
			wrong++;
		}
	}
	check(wrong == 0, "any other status for the profile, also with a profile as its body, and no answer: the round has failed, the values are not asked for");

	// AutoPID is off: the adapter has no profile and says so with a 500
	adapter_init(&wican, 0);
	wican.autopid = WICAN_AUTOPID_OFF;
	join(OWN, 100000);
	exchange();
	seconds(20);
	check(strncmp(trace, "SC SC SC SC", 11) == 0 && strchr(trace, 'V') == NULL && view() == CONN_VIEW_AUTOPID_OFF && poll.conn.failed_rounds == 0 && !poll.catalog_complete &&
	      !poll.lost, "AutoPID is off and the profile answers 500 every round: no round fails, the display shows that AutoPID is off, not that nothing answers");
	check(poll.http_ok == 21 && poll.http_failed == 21, "the 500 of the profile counts as a failed answer all the same");
	wrong = 0;
	for(i = 0; i < sizeof(absent) / sizeof(absent[0]); i++)
	{
		adapter_init(&wican, 0);
		wican.autopid = WICAN_AUTOPID_OFF;
		scene_catalog();
		reply(absent[i], CONFIG_RPM, NULL);
		if(poll.catalog_complete || !poll.conn.want_catalog || poll.conn.failed_rounds != 0) wrong++;
	}
	check(wrong == 0, "while AutoPID is off every status but 200 says that there is no profile: the round does not fail");
	adapter_init(&wican, 0);
	wican.autopid = WICAN_AUTOPID_OFF;
	scene_catalog();
	reply(0, NULL, NULL);
	check(poll.conn.failed_rounds == 1 && poll.conn.want_catalog, "while AutoPID is off no answer for the profile still fails the round");
	adapter_init(&wican, 0);
	wican.autopid = WICAN_AUTOPID_OFF;
	scene_catalog();
	reply(-1, NULL, NULL);
	check(poll.conn.failed_rounds == 1, "while AutoPID is off a negative status for the profile fails the round: it is no answer");
	adapter_init(&wican, 0);
	wican.autopid = WICAN_AUTOPID_STARTING;
	scene_catalog();
	reply(500, NULL, NULL);
	check(poll.conn.failed_rounds == 1, "while AutoPID is starting a 500 for the profile fails the round");
	adapter_init(&wican, 0);
	wican.api = false;
	wican.autopid = WICAN_AUTOPID_OFF;
	scene_catalog();
	check(request.kind == POLL_CATALOG && conn_state(&poll.conn) == NULL, "the scene: a firmware without the API, the profile is asked for");
	answer();
	check(poll.conn.failed_rounds == 1 && poll.http_failed == 1, "without a state nothing says that AutoPID is off: a 500 for the profile fails the round");

	// A profile that has no room at the caller is passed with its status and an empty body
	memset(huge, ' ', sizeof(huge) - 1);
	memcpy(huge, CONFIG_RPM, strlen(CONFIG_RPM));
	huge[POLL_BODY_SIZE - 1] = '\0';
	adapter_init(&wican, 0);
	wican.config_text = huge;
	join(OWN, 100000);
	exchange();
	check(sent("SCV") && poll.catalog_complete && poll.catalog.count == 4, "a profile of 16383 bytes fills the room of the caller and is taken");
	huge[POLL_BODY_SIZE - 1] = ' ';
	huge[POLL_BODY_SIZE] = '\0';
	adapter_init(&wican, 0);
	wican.config_text = huge;
	join(OWN, 100000);
	exchange();
	check(sent("SCV") && !poll.catalog_complete && !poll.conn.want_catalog && poll.conn.failed_rounds == 0 && poll.http_ok == 3 && poll.http_failed == 0,
	      "a profile of 16384 bytes has no room: 200 with an empty body - an answer that cannot be used, conn has it for this connection and the round goes on");
	seconds(20);
	check(strchr(trace, 'C') == NULL && view() == CONN_VIEW_LIVE && poll.conn.failed_rounds == 0 && !poll.catalog_complete && !poll.lost,
	      "a profile without room is not asked for again and fails no round: the display stays live, not without an answer");
	check(poll.catalog.count == 4 && catalog_find(&poll.catalog, "ENGINE_RPM") == 1 && poll.catalog.entries[1].delivered && !poll.catalog.entries[1].in_profile,
	      "the catalogue of a connection whose profile had no room is what the values bring");
}

// A display that got the state of API.md (pass 1234) and a profile without ENGINE_RPM; its values are asked for at 5000
static void scene_values(void)
{
	join(OWN, 5000);
	send();
	reply(200, state_example, NULL);
	send();
	reply(200, config_without_rpm, NULL);
	send();
}

static void test_values(void)
{
	static const char first[] = "{\"ENGINE_RPM\":0,\"COOLANT_TMP\":21.5}";
	static const char later[] = "{\"ENGINE_RPM\":900,\"COOLANT_TMP\":80,\"NEW\":\"on\"}";
	static const struct
	{
		int status;
		const char *body;       // NULL: the first values
	} failures[] = {
		{200, "{\"error\":\"No data available\"}"}, {200, "[]"}, {200, ""}, {200, "{\"ENGINE_RPM\":0"}, {200, "0"},
		{500, NULL}, {404, NULL}, {204, NULL}, {202, NULL}, {201, NULL}, {199, NULL}, {0, NULL}, {-1, NULL}, {503, NULL},
	};
	char expected[128];
	int wrong = 0;
	size_t i;

	scene_values();
	check(request.kind == POLL_VALUES && poll.values.count == 1 && poll.catalog.count == 3, "the scene: the values are asked for, the catalogue has the two names of the profile");
	reply(200, first, NULL);
	check(poll.values.count == 3 && number_of("ENGINE_RPM") == 0 && seen_at("ENGINE_RPM") == 5000 &&
	      number_of("COOLANT_TMP") == 21.5 && poll.values.has_pass && poll.values.pass == 1234, "200 with values: they are taken with the pass counter of the last state");
	check(poll.catalog.count == 4 && strcmp(poll.catalog.entries[3].name, "ENGINE_RPM") == 0 && poll.catalog.entries[3].delivered && !poll.catalog.entries[3].in_profile &&
	      poll.catalog.entries[1].delivered && !poll.catalog.entries[2].delivered && poll.catalog.entries[0].delivered,
	      "renewed values are noted in the catalogue: delivered, and a name the profile does not have is added");
	check(poll.conn.failed_rounds == 0 && !send() && poll.http_ok == 3, "the values end the round");

	now = 6000;
	send();
	reply(200, state_example, NULL);
	send();
	reply(200, later, NULL);
	check(number_of("ENGINE_RPM") == 0 && seen_at("ENGINE_RPM") == 5000 && value("NEW") == NULL && poll.values.count == 3 && catalog_find(&poll.catalog, "NEW") < 0,
	      "values that arrive while the pass counter of the state stands are a repetition: nothing is renewed");
	check(poll.conn.failed_rounds == 0 && !poll.asking && !send() && poll.http_ok == 5, "a repetition is an answer: the round has not failed");
	check(seen_at("@BATT_V") == 6000, "the battery voltage of every state is new, whatever the pass counter says");

	now = 7000;
	send();
	reply(200, patched(state_example, "\"pass\":1234", "\"pass\":1235"), NULL);
	send();
	reply(200, later, NULL);
	check(number_of("ENGINE_RPM") == 900 && seen_at("ENGINE_RPM") == 7000 && value("NEW") != NULL && value("NEW")->kind == VALUE_ON && poll.values.pass == 1235,
	      "the pass counter of the state moved: the values are renewed");
	check(poll.catalog.count == 5 && strcmp(poll.catalog.entries[4].name, "NEW") == 0 && poll.catalog.entries[4].delivered, "a name that arrives later is added to the catalogue then");

	for(i = 0; i < sizeof(failures) / sizeof(failures[0]); i++)
	{
		scene_values();
		reply(failures[i].status, failures[i].body != NULL ? failures[i].body : first, NULL);
		if(poll.conn.failed_rounds != 1 || poll.values.count != 1 || poll.values.has_pass || poll.catalog.count != 3 || poll.asking)
		{
			printf("  values: %d with '%s' does not fail the round\n", failures[i].status, failures[i].body != NULL ? failures[i].body : first);
			wrong++;
		}
	}
	scene_values();
	reply(200, NULL, NULL);
	check(wrong == 0 && poll.conn.failed_rounds == 1 && poll.values.count == 1, "the error object, a body that is no object, any other status and no answer: the round has failed, no value is taken");
	scene_values();
	reply(200, "{}", NULL);
	check(poll.conn.failed_rounds == 0 && poll.values.count == 1 && poll.values.has_pass && poll.values.pass == 1234 && !poll.asking, "an empty object is an answer without values");
	scene_values();
	reply(200, "{\"error\":\"x\",\"ENGINE_RPM\":0}", NULL);
	check(poll.conn.failed_rounds == 0 && value("ENGINE_RPM") != NULL, "an object with a member error next to a value is no error object");
	scene_values();
	poll_apply(&poll, &request, 200, "{\"ENGINE_RPM\":0},", 16, NULL, now, work, POLL_TOKENS);
	check(poll.conn.failed_rounds == 0 && value("ENGINE_RPM") != NULL, "values are read up to their length: what stands behind them is not looked at");
	scene_values();
	poll_apply(&poll, &request, 200, first, strlen(first) - 1, NULL, now, work, POLL_TOKENS);
	check(poll.conn.failed_rounds == 1 && poll.values.count == 1, "values passed one byte short cannot be read: the round has failed");

	// The pass counter is compared as a whole
	scene_values();
	reply(200, first, NULL);
	now = 6000;
	send();
	reply(200, patched(state_example, "\"pass\":1234", "\"pass\":66770"), NULL);
	send();
	reply(200, later, NULL);
	check(number_of("ENGINE_RPM") == 900 && seen_at("ENGINE_RPM") == 6000 && poll.values.pass == 66770, "a pass counter that moved by 65536 has moved: the values are renewed");

	// The voltage arrives with a state; the catalogue marks it when values are renewed
	join(OWN, 5000);
	send();
	reply(200, patched(state_example, "\"batt_v\":12.4", "\"batt_v\":-1"), NULL);
	send();
	reply(200, config_without_rpm, NULL);
	send();
	reply(200, first, NULL);
	check(value("@BATT_V") == NULL && !poll.catalog.entries[0].delivered, "the scene: no voltage was measured yet, the catalogue does not have it as delivered");
	now = 6000;
	send();
	reply(200, state_example, NULL);
	send();
	reply(200, later, NULL);
	check(value("@BATT_V") != NULL && !poll.catalog.entries[0].delivered, "a repetition notes nothing in the catalogue, not even the voltage that arrived with the state");
	now = 7000;
	send();
	reply(200, patched(state_example, "\"pass\":1234", "\"pass\":1235"), NULL);
	send();
	reply(200, later, NULL);
	check(poll.catalog.entries[0].delivered, "with the next renewed values the voltage is marked as delivered");

	// A firmware without the API has no pass counter
	adapter_init(&wican, 0);
	wican.api = false;
	join(OWN, 100000);
	exchange();
	check(sent("SCV") && view() == CONN_VIEW_NO_API && value("ENGINE_RPM") != NULL && seen_at("ENGINE_RPM") == 100000 && !poll.values.has_pass && value("@BATT_V") == NULL,
	      "a firmware without the API: profile and values are asked for, the values are taken without a pass counter");
	strcpy(expected, "");
	for(i = 0; i < 29; i++)
	{
		second();
		strcat(expected, i == 0 ? "V" : " V");
		if(seen_at("ENGINE_RPM") != now || poll.values.has_pass) wrong++;
	}
	check(sent(expected) && wrong == 0, "without the API the values are asked for every second and every answer renews them");
	second();
	check(sent("SV") && view() == CONN_VIEW_NO_API && poll.http_ok == 34 && poll.http_failed == 0, "30 s after the 404 the state is asked for again; a 404 is an answer");
	check(poll_read(&poll, now) == DTC_FLOW_NO_API && poll.flow.phase == DTC_FLOW_IDLE, "without the API the fault memory is not read: poll_read() says why");
}

// One second later the state is asked for and answered; the rest of the round waits
static void state_second(void)
{
	now += 1000;
	send();
	answer();
}

static void test_restart(void)
{
	// What a restart drops
	scene();
	adapter_restart(&wican, BOOT + 1, 42, now + 500);
	state_second();
	check(poll.values.count == 1 && value("@BATT_V") != NULL && seen_at("@BATT_V") == 103000 && !poll.values.has_pass,
	      "the state shows another boot number: the values are dropped; the voltage of that very state stays");
	check(!poll.catalog_complete && poll.catalog.count == 1 && strcmp(poll.catalog.entries[0].name, "@BATT_V") == 0 && !poll.catalog.entries[0].delivered &&
	      catalog_find(&poll.catalog, "ENGINE_RPM") < 0, "after a restart the catalogue is started anew: the battery voltage alone, not delivered, and not complete");
	check(poll.catalog_guard.seen_since_ms == 103000 && !poll.catalog_guard.written && poll.conn.want_catalog, "after a restart guard is told: its rest time begins anew, and conn asks for the profile");
	check(poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS) && poll.flow.phase == DTC_FLOW_IDLE, "a restart raises POLL_EVENT_FORGET and POLL_EVENT_LISTS");
	exchange();
	second();
	check(sent("SCV SV") && poll.catalog_complete && poll.catalog.count == 4 && poll.catalog.entries[1].in_profile && poll_take_events(&poll) == 0,
	      "after a restart the profile is asked for again, once, and is the catalogue");

	scene();
	now += 1000;
	until(POLL_VALUES);
	reply(200, "{\"ENGINE_RPM\":0,\"A_NAME_OF_33_BYTES_IS_TOO_LONG_XX\":1}", NULL);
	adapter_restart(&wican, BOOT + 1, 42, now);
	state_second();
	check(poll.values.count == 1 && poll.values.dropped == 1, "a restart forgets the values, not the count of those that were dropped");

	// What becomes of the fault memory flow
	scene();
	now += 1000;
	send();
	poll_read(&poll, now);
	adapter_restart(&wican, BOOT + 1, 42, now);
	answer();
	check(poll.flow.phase == DTC_FLOW_IDLE && poll.flow.to_send == DTC_FLOW_SEND_NOTHING && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "the adapter restarts while a read waits behind the request under way: it was never sent - idle, no failure");
	exchange();
	check(sent("SCV") && wican.seq == 0 && poll.flow.phase == DTC_FLOW_IDLE, "the read that waited when the restart showed is never sent");
	scene();
	poll_read(&poll, now);
	exchange();
	adapter_restart(&wican, BOOT + 1, 42, now);
	state_second();
	check(reason_is("restarted") && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS), "the adapter restarts while the own read runs: failed, restarted");
	scene_list();
	adapter_restart(&wican, BOOT + 1, 42, now);
	state_second();
	check(poll.flow.phase == DTC_FLOW_IDLE && !poll.has_list && !poll.has_old && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "the adapter restarts while the list is shown: the list is dropped");
	scene_clearing();
	adapter_restart(&wican, BOOT + 1, 42, now);
	state_second();
	check(poll.flow.phase == DTC_FLOW_UNKNOWN && !poll.has_list && old_is(read_text, 2) && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "the adapter restarts while the own clear runs: unknown, the list is dropped, the old list stays");
	scene_cleared();
	adapter_restart(&wican, BOOT + 1, 42, now);
	state_second();
	check(poll.flow.phase == DTC_FLOW_IDLE && !poll.has_cleared && old_is(read_text, 2) && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "the adapter restarts while the outcome of the clear is shown: the outcome is dropped, the old list stays");
	read_answered(409, "{\"accepted\":false,\"reason\":\"busy\",\"seq\":42}");
	adapter_restart(&wican, BOOT + 1, 42, now);
	state_second();
	check(reason_is("busy") && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS), "the adapter restarts while a failure is shown: the failure stays until the user leaves it");

	// The same adapter with a number of values that changed is no restart
	scene_list();
	wican.has_rpm = false;
	second();
	check(sent("SCV") && poll.flow.phase == DTC_FLOW_LIST && shows_list(read_text, 2) && poll.catalog_complete && poll.values.count == 4 && poll_take_events(&poll) == 0,
	      "another number of values without another boot number: the profile is asked for again, nothing is dropped");
	check(poll.catalog.count == 4 && catalog_find(&poll.catalog, "ENGINE_RPM") == 1 && poll.catalog.entries[1].delivered && !poll.catalog.entries[1].in_profile,
	      "without a restart the catalogue keeps an entry that was delivered, also when the profile does not name it any more");

	// A restart with another profile leaves nothing of the one before behind
	scene();
	check(catalog_find(&poll.catalog, "ENGINE_RPM") == 1 && poll.catalog.entries[1].delivered && poll.catalog.count == 4, "the scene: ENGINE_RPM is in the catalogue and was delivered");
	wican.has_rpm = false;
	adapter_restart(&wican, BOOT + 1, 42, now + 500);
	seconds(2);
	check(sent("SCV SV") && poll.catalog_complete && poll.catalog.count == 3 && catalog_find(&poll.catalog, "ENGINE_RPM") < 0 &&
	      strcmp(poll.catalog.entries[1].name, "COOLANT_TMP") == 0 && poll.catalog.entries[1].in_profile && strcmp(poll.catalog.entries[2].name, "FUEL_L") == 0,
	      "the adapter restarts with a profile without ENGINE_RPM: the catalogue is that profile, the entry delivered before the restart is gone");
	seconds(13);
	check(now == 117000 && read_block() == DTC_FLOW_STARTING, "14 s after that restart no read is offered yet: the adapter is not up for 15 s");
	second();
	check(value("ENGINE_RPM") == NULL && read_block() == DTC_FLOW_ALLOWED, "15 s after that restart a read is offered without an engine speed: the profile of this adapter has none");

	// A catalogue that is not complete is started anew as well
	adapter_init(&wican, 0);
	wican.config_text = "[]";
	join(OWN, 100000);
	exchange();
	seconds(2);
	check(!poll.catalog_complete && poll.catalog.count == 4 && poll.catalog.entries[1].delivered, "the scene: the profile cannot be used, the catalogue is what the values brought");
	adapter_restart(&wican, BOOT + 1, 42, now + 500);
	state_second();
	check(poll.catalog.count == 1 && !poll.catalog_complete && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "a restart starts the catalogue anew also when it never was complete: what the values of the adapter before brought is gone");

	// Joining a network is no restart: the stored catalogue serves
	adapter_init(&wican, 0);
	wican.dead = true;
	join(OWN, 100000);
	poll_stored(&poll, stored_catalog, strlen(stored_catalog), NULL, 0, work, POLL_TOKENS);
	exchange();
	seconds(20);
	check(view() == CONN_VIEW_NO_ANSWER && poll.catalog.count == 3 && catalog_find(&poll.catalog, "OIL_TEMP_OLD") == 2, "an adapter that does not answer: the stored catalogue serves");
	wican.dead = false;
	now += 10000;
	send();
	answer();
	check(conn_state(&poll.conn) != NULL && poll.catalog.count == 3 && catalog_find(&poll.catalog, "OIL_TEMP_OLD") == 2 && (poll_take_events(&poll) & POLL_EVENT_FORGET) == 0,
	      "the first state of a connection is no restart: the stored catalogue still serves, nothing is forgotten");
	exchange();
	check(poll.catalog_complete && poll.catalog.count == 4 && catalog_find(&poll.catalog, "OIL_TEMP_OLD") < 0, "the profile of that connection replaces the stored catalogue");
}

// The start of the adapter the catalogue came from is kept over a pause of the network: conn.h knows the
// adapter only since the network was joined
static void test_restart_unseen(void)
{
	uint32_t events;

	// The adapter restarts with another profile while the display is out of the network
	scene();
	check(catalog_find(&poll.catalog, "ENGINE_RPM") == 1 && poll.catalog.entries[1].delivered && poll.catalog.count == 4 && poll.start == POLL_START_API &&
	      strcmp(poll.start_id, OWN) == 0 && poll.start_boot == BOOT,
	      "the scene: ENGINE_RPM is in the catalogue and was delivered; the start the catalogue came from is the own adapter with its boot number");
	poll_wifi(&poll, false, now);
	wican.has_rpm = false;
	adapter_restart(&wican, BOOT + 1, 42, now + 500);
	now += 5000;
	poll_wifi(&poll, true, now);
	check(poll.catalog.count == 4 && poll_take_events(&poll) == 0 && poll.start_boot == BOOT, "joining the network again forgets nothing by itself: the adapter has not answered yet");
	send();
	answer();
	check(sent("S") && poll.catalog.count == 1 && !poll.catalog_complete && poll.values.count == 1 && value("@BATT_V") != NULL &&
	      poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "the first state after the pause shows another boot number than the start the catalogue came from: the adapter restarted unseen - the catalogue is started anew, "
	      "the voltage of that state stays, POLL_EVENT_FORGET and POLL_EVENT_LISTS");
	check(poll.start == POLL_START_API && poll.start_boot == BOOT + 1 && strcmp(poll.start_id, OWN) == 0, "the start is from then on the one that answered");
	exchange();
	check(sent("CV") && poll.catalog_complete && poll.catalog.count == 3 && catalog_find(&poll.catalog, "ENGINE_RPM") < 0 && strcmp(poll.catalog.entries[1].name, "COOLANT_TMP") == 0,
	      "the profile of the restarted adapter is fetched and is the catalogue: the entry delivered before the pause is gone");
	second();
	check(sent("SV") && poll_take_events(&poll) == 0 && poll.catalog.count == 3, "the next state shows the same boot number: nothing more is forgotten");

	// The same pause without a restart: what was delivered stays, as within a connection
	scene();
	poll_wifi(&poll, false, now);
	wican.has_rpm = false;
	now += 5000;
	poll_wifi(&poll, true, now);
	exchange();
	events = poll_take_events(&poll);
	check(sent("SCV") && (events & POLL_EVENT_FORGET) == 0 && poll.catalog.count == 4 && catalog_find(&poll.catalog, "ENGINE_RPM") == 1 && poll.catalog.entries[1].delivered &&
	      !poll.catalog.entries[1].in_profile,
	      "the network comes back and the adapter is the same start: nothing is forgotten - the profile is fetched again, and an entry that was delivered stays "
	      "although the profile does not name it any more");

	// Over several pauses the start is the one that answered last
	scene();
	poll_wifi(&poll, false, now);
	adapter_restart(&wican, BOOT + 1, 42, now + 500);
	now += 5000;
	poll_wifi(&poll, true, now);
	exchange();
	poll_take_events(&poll);
	poll_wifi(&poll, false, now);
	now += 5000;
	poll_wifi(&poll, true, now);
	exchange();
	check((poll_take_events(&poll) & POLL_EVENT_FORGET) == 0 && poll.catalog_complete && poll.start_boot == BOOT + 1, "a second pause behind which the same start answers: nothing is forgotten");
	poll_wifi(&poll, false, now);
	adapter_restart(&wican, BOOT, 42, now + 500);
	now += 5000;
	poll_wifi(&poll, true, now);
	send();
	answer();
	check(poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS) && poll.catalog.count == 1 && poll.start_boot == BOOT,
	      "a third pause behind which the boot number of the first start answers: it is another one than the last - forgotten");

	// The uptime does not count, as for conn.h
	scene();
	poll_wifi(&poll, false, now);
	wican.boot_ms = now;
	now += 5000;
	poll_wifi(&poll, true, now);
	exchange();
	check(conn_state(&poll.conn) != NULL && conn_state(&poll.conn)->up_s == 5 && (poll_take_events(&poll) & POLL_EVENT_FORGET) == 0 && poll.catalog.count == 4 && poll.catalog.entries[1].delivered,
	      "the same boot number after the pause with an uptime of 5 s where it was 102 s: the same start - nothing is forgotten");

	// An answer that is neither a state nor a 404 says nothing about the start
	scene();
	poll_wifi(&poll, false, now);
	adapter_restart(&wican, BOOT + 1, 42, now + 500);
	now += 5000;
	poll_wifi(&poll, true, now);
	send();
	reply(500, "", NULL);
	check(poll.start == POLL_START_API && poll.start_boot == BOOT && poll.catalog.count == 4 && poll_take_events(&poll) == 0, "a 500 for the state after the pause: no start is known from it, nothing is forgotten");
	send();
	reply(200, "{\"api\":1", NULL);
	check(poll.start == POLL_START_API && poll.start_boot == BOOT && poll.catalog.count == 4 && poll_take_events(&poll) == 0, "a state that cannot be read: no start is known from it either");
	seconds(3);
	check(poll.start_boot == BOOT + 1 && poll.catalog_complete && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "the first state that is taken shows the other boot number: forgotten then");

	// A foreign adapter is no start the catalogue could come from
	scene();
	poll_wifi(&poll, false, now);
	strcpy(wican.id, OTHER);
	wican.boot = BOOT + 5;
	now += 5000;
	poll_wifi(&poll, true, now);
	exchange();
	second();
	check(view() == CONN_VIEW_FOREIGN && sent("S S") && poll_take_events(&poll) == 0 && poll.catalog.count == 4 && poll.catalog.entries[1].delivered,
	      "a foreign adapter answers after the pause, with another boot number: nothing is fetched from it and nothing is forgotten for it");
	check(poll.start == POLL_START_API && strcmp(poll.start_id, OWN) == 0 && poll.start_boot == BOOT, "the start the catalogue came from stays the own adapter");
	poll_wifi(&poll, false, now);
	strcpy(wican.id, OWN);
	wican.boot = BOOT;
	now += 5000;
	poll_wifi(&poll, true, now);
	exchange();
	events = poll_take_events(&poll);
	check(view() == CONN_VIEW_LIVE && (events & POLL_EVENT_FORGET) == 0 && poll.catalog.count == 4 && poll.catalog.entries[1].delivered,
	      "back in the network of the own adapter, which did not restart meanwhile: the same start - nothing is forgotten");
	poll_wifi(&poll, false, now);
	strcpy(wican.id, OTHER);
	wican.boot = BOOT + 5;
	now += 5000;
	poll_wifi(&poll, true, now);
	exchange();
	poll_wifi(&poll, false, now);
	strcpy(wican.id, OWN);
	adapter_restart(&wican, BOOT + 1, 42, now + 500);
	now += 5000;
	poll_wifi(&poll, true, now);
	send();
	answer();
	check(poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS) && poll.catalog.count == 1 && poll.start_boot == BOOT + 1,
	      "back in the network of the own adapter, which did restart while the foreign one answered: forgotten");

	// A firmware without the API has no boot number: it is another firmware than one with the API
	scene();
	poll_wifi(&poll, false, now);
	wican.api = false;
	now += 5000;
	poll_wifi(&poll, true, now);
	send();
	answer();
	check(view() == CONN_VIEW_NO_API && poll.start == POLL_START_NO_API && poll.catalog.count == 1 && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "404 for the state after the pause, where a firmware with the API answered before it: another firmware - the catalogue is started anew");
	exchange();
	check(sent("SCV") && poll.catalog_complete && poll.catalog.count == 4 && poll_take_events(&poll) == 0, "the firmware without the API is asked for its profile and its values");
	poll_wifi(&poll, false, now);
	wican.has_rpm = false;
	now += 5000;
	poll_wifi(&poll, true, now);
	exchange();
	events = poll_take_events(&poll);
	check(view() == CONN_VIEW_NO_API && (events & POLL_EVENT_FORGET) == 0 && poll.catalog.count == 4 && catalog_find(&poll.catalog, "ENGINE_RPM") == 1,
	      "a firmware without the API behind the next pause as well: nothing tells two of them apart - nothing is forgotten");
	poll_wifi(&poll, false, now);
	wican.api = true;
	now += 5000;
	poll_wifi(&poll, true, now);
	send();
	answer();
	check(view() != CONN_VIEW_NO_API && poll.start == POLL_START_API && poll.start_boot == BOOT && poll.catalog.count == 1 &&
	      poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "a state after the pause, where a firmware without the API answered before it: another firmware - forgotten, also with the boot number of the scene");

	// Before anything answered since the display started nothing is another start: the stored catalogue serves
	adapter_init(&wican, 0);
	wican.api = false;
	join(OWN, 100000);
	poll_stored(&poll, stored_catalog, strlen(stored_catalog), NULL, 0, work, POLL_TOKENS);
	send();
	answer();
	check(poll.start == POLL_START_NO_API && poll.catalog.count == 3 && catalog_find(&poll.catalog, "OIL_TEMP_OLD") == 2 && (poll_take_events(&poll) & POLL_EVENT_FORGET) == 0,
	      "a 404 as the first answer since the display started is no other start: the stored catalogue still serves");
	adapter_init(&wican, 0);
	wican.boot = 7;
	join(OWN, 100000);
	poll_stored(&poll, stored_catalog, strlen(stored_catalog), NULL, 0, work, POLL_TOKENS);
	send();
	answer();
	check(poll.start == POLL_START_API && poll.start_boot == 7 && poll.catalog.count == 3 && catalog_find(&poll.catalog, "OIL_TEMP_OLD") == 2 &&
	      (poll_take_events(&poll) & POLL_EVENT_FORGET) == 0,
	      "a state as the first answer since the display started is no other start, whatever its boot number: the stored catalogue still serves, and the start is known from now on");
	scene();
	poll_init(&poll, OWN);
	wican.boot = BOOT + 1;
	poll_wifi(&poll, true, now);
	send();
	answer();
	check((poll_take_events(&poll) & POLL_EVENT_FORGET) == 0 && poll.start_boot == BOOT + 1, "after a new init the start of before is not known any more: the first state is no other start");

	// Another id with the same boot number is another adapter: a display that is not bound, an adapter without an id
	adapter_init(&wican, 0);
	wican.id[0] = '\0';
	join(NULL, 100000);
	exchange();
	seconds(2);
	check(conn_state(&poll.conn) != NULL && poll.bound_id[0] == '\0' && !poll.conn.foreign && poll.start == POLL_START_API && poll.start_id[0] == '\0' && poll.catalog.count == 4,
	      "the scene: an adapter without an id answers a display that is not bound - not foreign, and the start the catalogue came from");
	poll_take_events(&poll);
	poll_wifi(&poll, false, now);
	strcpy(wican.id, OWN);
	now += 5000;
	poll_wifi(&poll, true, now);
	send();
	answer();
	check(strcmp(poll.bound_id, OWN) == 0 && strcmp(poll.start_id, OWN) == 0 && poll.catalog.count == 1 &&
	      poll_take_events(&poll) == (POLL_EVENT_BOUND | POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "behind the pause an adapter with an id answers, with the same boot number: another adapter - the display is bound to it, what came from the one before is forgotten");
}

static void test_foreign(void)
{
	scene_list();
	strcpy(wican.id, OTHER);
	state_second();
	check(view() == CONN_VIEW_FOREIGN && poll.values.count == 0 && !poll.catalog_complete && strcmp(poll.bound_id, OWN) == 0,
	      "another adapter answers: foreign, the values are dropped and its voltage is no value");
	check(poll.catalog.count == 1 && catalog_find(&poll.catalog, "ENGINE_RPM") < 0, "another adapter answers: the catalogue is started anew");
	check(poll.flow.phase == DTC_FLOW_IDLE && !poll.has_list && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "another adapter answers while the list is shown: the list is dropped although it shows the same numbers");
	exchange();
	seconds(3);
	check(sent("S S S S") && poll.values.count == 0 && poll_read(&poll, now) == DTC_FLOW_FOREIGN && poll_take_events(&poll) == 0, "a foreign adapter is asked for its state and nothing else");
	strcpy(wican.id, OWN);
	second();
	check(sent("SRCV") && view() == CONN_VIEW_LIVE && poll.catalog_complete && poll.values.count == 4 && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "the own adapter answers again: it counts as replaced once more - result, profile and values are asked for anew");
	check(poll.flow.phase == DTC_FLOW_IDLE && !poll.has_list, "the result of the read whose list was dropped is fetched again and is no list");

	scene();
	poll_read(&poll, now);
	exchange();
	strcpy(wican.id, OTHER);
	state_second();
	check(reason_is("no_answer"), "another adapter answers while the own read runs: failed, also if it shows the same boot and request number");
	scene_clearing();
	strcpy(wican.id, OTHER);
	state_second();
	check(poll.flow.phase == DTC_FLOW_UNKNOWN && !poll.has_list && old_is(read_text, 2), "another adapter answers while the own clear runs: unknown");
	scene_cleared();
	strcpy(wican.id, OTHER);
	state_second();
	check(poll.flow.phase == DTC_FLOW_IDLE && !poll.has_cleared && old_is(read_text, 2), "another adapter answers while the outcome of the clear is shown: the outcome is dropped");
	scene();
	poll_read(&poll, now);
	strcpy(wican.id, OTHER);
	now += 1000;
	exchange();
	check(sent("rS") && view() == CONN_VIEW_FOREIGN && reason_is("no_answer"), "a read that waited goes to whatever adapter answers; when the state shows a foreign one, it failed");
}

static void test_no_api(void)
{
	// The API goes away
	scene();
	poll_read(&poll, now);
	exchange();
	wican.api = false;
	state_second();
	check(view() == CONN_VIEW_NO_API && reason_is("no_answer") && poll.values.count == 0 && !poll.catalog_complete && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "404 for the state after a state was answered: another firmware - values dropped, the own read failed, no voltage");
	check(poll.catalog.count == 1, "404 for the state after a state was answered: the catalogue is started anew");
	exchange();
	check(sent("rSCV") && poll.catalog_complete && poll.catalog.count == 4, "the firmware without the API is asked for its profile and its values");
	scene_list();
	wican.api = false;
	state_second();
	check(poll.flow.phase == DTC_FLOW_IDLE && !poll.has_list && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS), "the API goes away while the list is shown: the list is dropped");
	scene_clearing();
	wican.api = false;
	state_second();
	check(poll.flow.phase == DTC_FLOW_UNKNOWN && !poll.has_list && old_is(read_text, 2), "the API goes away while the own clear runs: unknown");
	scene_cleared();
	wican.api = false;
	state_second();
	check(poll.flow.phase == DTC_FLOW_IDLE && !poll.has_cleared, "the API goes away while the outcome of the clear is shown: the outcome is dropped");

	// The API appears
	adapter_init(&wican, 0);
	wican.api = false;
	join(OWN, 100000);
	exchange();
	seconds(29);
	wican.api = true;
	poll_take_events(&poll);
	trace[0] = '\0';
	state_second();
	check(view() == CONN_VIEW_LIVE && poll.values.count == 1 && value("@BATT_V") != NULL && !poll.catalog_complete && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "a state after a 404: another firmware - the values of the old one are dropped, the voltage of the state is there");
	check(poll.catalog.count == 1, "a state after a 404: the catalogue of the firmware without the API is gone, it is started anew");
	exchange();
	check(sent("SCV") && poll.catalog_complete && poll.values.has_pass, "the firmware with the API is asked for its profile again, the values have a pass counter now");
}

static void test_other_scan(void)
{
	const char *reason;
	uint32_t number;

	scene_list();
	adapter_request(&wican, false, false, 0, now, &number, &reason);
	second();
	check(sent("S") && poll.flow.phase == DTC_FLOW_IDLE && !poll.has_list && !poll.has_old && poll_take_events(&poll) == POLL_EVENT_LISTS,
	      "somebody else starts a scan while the list is shown: the list is dropped with the state that shows it");
	check(poll_clear(&poll, false, now) == DTC_FLOW_BUSY && poll_read(&poll, now) == DTC_FLOW_BUSY && !send(), "while somebody else's scan runs nothing is sent");

	scene_list();
	second();
	poll_clear(&poll, false, now);
	adapter_request(&wican, false, false, 0, now, &number, &reason);
	exchange();
	check(sent("SVc") && reason_is("busy") && !poll.has_list && !poll.has_old && wican.seq == 43 && !wican.clear,
	      "somebody else starts a scan between the confirmation and the POST: the adapter refuses the clear, busy");
}

// Sends at `at_ms` what is due then
static void at(uint64_t at_ms)
{
	now = at_ms;
	exchange();
}

static char second_text[POLL_TEXT_SIZE];    // the result of the read 44 as the adapter of the test wrote it

// The own read 44 behind the clear 43: a second list, with the one code that came back, is shown since
// 115000. It is 117000, and the list of the first clear, with two codes, is the old list.
static bool scene_second_list(void)
{
	scene_cleared();
	wican.read_text = NULL;
	poll_read(&poll, now);
	exchange();
	seconds(6);
	strcpy(second_text, wican.result);
	poll_take_events(&poll);
	trace[0] = '\0';
	return now == 117000 && poll.flow.phase == DTC_FLOW_LIST && poll.flow.read_seq == 44 && poll.has_list && strcmp(poll.list_text, second_text) == 0 &&
	       poll.list.dtc_count == 1 && strcmp(second_text, read_text) != 0 && old_is(read_text, 2);
}

static bool old_is_second(void)
{
	return poll.has_old && strcmp(poll.old_text, second_text) == 0 && !poll.old.clear && poll.old.dtc_count == 1 && poll.old.code_count == 1;
}

// The list before the last clear is only replaced by a list that was cleared, or may have been
static void test_old(void)
{
	answer_t lost;
	const char *reason;
	uint32_t number;

	check(scene_second_list(), "the scene: a second list with one code is shown; the list of the first clear, with two codes, is the old list");

	// The adapter accepts
	poll_clear(&poll, false, now);
	check(send() && request.kind == POLL_DTC_CLEAR && strcmp(request.path, "/api/dtc?action=clear&seq=44") == 0 && old_is(read_text, 2) && poll_take_events(&poll) == 0,
	      "the clear of the second list goes out: the old list is still the one of the first clear, no event");
	answer();
	check(poll.flow.phase == DTC_FLOW_CLEARING && old_is_second() && poll_take_events(&poll) == (POLL_EVENT_OLD | POLL_EVENT_LISTS),
	      "202: the second list replaces the old list, POLL_EVENT_OLD and POLL_EVENT_LISTS");
	wican.rpm = 780;
	second();
	check(reason_is("engine_running") && old_is_second() && poll_take_events(&poll) == POLL_EVENT_LISTS,
	      "the accepted clear ends with an error: the old list stays the list of that clear, POLL_EVENT_OLD is not raised a second time");

	// The adapter refuses
	scene_second_list();
	poll_clear(&poll, false, now);
	wican.sleep_in_s = 0;
	exchange();
	check(sent("c") && reason_is("not_ready") && !poll.has_list && old_is(read_text, 2) && poll_take_events(&poll) == POLL_EVENT_LISTS,
	      "a clear the adapter refuses leaves the old list as it was: the list of the first clear, no POLL_EVENT_OLD");

	// The clear does not arrive
	scene_second_list();
	poll_clear(&poll, false, now);
	send();
	reply(0, NULL, NULL);
	seconds(2);
	check(poll.flow.phase == DTC_FLOW_LIST && poll.has_list && strcmp(poll.list_text, second_text) == 0 && old_is(read_text, 2) && poll_take_events(&poll) == 0,
	      "a clear that did not arrive leaves the old list as it was and the second list shown, no event");

	// The clear arrives, its answer does not
	scene_second_list();
	poll_clear(&poll, false, now);
	send();
	adapter_answer(&wican, &request, now, &lost);
	reply(0, NULL, NULL);
	check(undecided(DTC_FLOW_CLEAR_SENT) && old_is(read_text, 2) && poll_take_events(&poll) == 0, "the POST of a clear without an answer changes no old list: the states decide");
	second();
	check(poll.flow.phase == DTC_FLOW_CLEARING && poll.flow.seq == 45 && old_is_second() && poll_take_events(&poll) == (POLL_EVENT_OLD | POLL_EVENT_LISTS),
	      "the state shows the clear accepted: the second list replaces the old list with that state");
	scene_second_list();
	poll_clear(&poll, false, now);
	send();
	adapter_answer(&wican, &request, now, &lost);
	reply(202, "{\"accepted\":true}", NULL);
	check(undecided(DTC_FLOW_CLEAR_SENT) && old_is(read_text, 2) && poll_take_events(&poll) == 0,
	      "202 without a number is no acceptance the display can follow: the old list stays until the states decide");
	second();
	check(poll.flow.phase == DTC_FLOW_CLEARING && old_is_second(), "the state behind a 202 without a number shows the clear: now the second list is the old list");

	// The number of the accepted clear need not be the one behind the number of the list
	scene();
	wican.next_seq = 2147483647u;
	poll_read(&poll, now);
	exchange();
	seconds(5);
	poll_take_events(&poll);
	check(poll.flow.phase == DTC_FLOW_LIST && poll.flow.read_seq == 2147483647u && poll_clear(&poll, false, now) == DTC_FLOW_ALLOWED, "the scene: a list with the largest number an adapter gives, 2147483647");
	exchange();
	check(poll.flow.phase == DTC_FLOW_CLEARING && poll.flow.seq == 1 && old_is(read_text, 2) && poll_take_events(&poll) == (POLL_EVENT_OLD | POLL_EVENT_LISTS),
	      "its clear is accepted with the number 1, the one behind the largest: a lower number than the one of the list is an acceptance all the same, the list is the old list");
	scene_list();
	second();
	poll_clear(&poll, false, now);
	send();
	reply(0, NULL, NULL);
	second();
	check(undecided(DTC_FLOW_CLEAR_SENT) && poll.flow.rounds_without_answer == 1 && !poll.has_old, "the scene: one state after the POST of a clear got no answer shows no new request");
	adapter_request(&wican, true, true, 42, now, &number, &reason);
	second();
	check(poll.flow.phase == DTC_FLOW_CLEARING && poll.flow.seq == 43 && old_is(read_text, 2) && poll_take_events(&poll) == (POLL_EVENT_OLD | POLL_EVENT_LISTS),
	      "the second state after the POST shows the clear accepted: its list is the old list with that state");

	// Nobody knows what became of the clear
	scene_second_list();
	poll_clear(&poll, false, now);
	send();
	reply(0, NULL, NULL);
	adapter_request(&wican, false, false, 0, now, &number, &reason);
	second();
	check(poll.flow.phase == DTC_FLOW_UNKNOWN && !poll.has_list && old_is_second() && poll_take_events(&poll) == (POLL_EVENT_OLD | POLL_EVENT_LISTS),
	      "a clear whose outcome is unknown may have cleared: the second list replaces the old list");

	// The network
	scene_second_list();
	poll_clear(&poll, false, now);
	poll_wifi(&poll, false, now);
	check(poll.flow.phase == DTC_FLOW_LIST && poll.has_list && old_is(read_text, 2) && poll_take_events(&poll) == 0,
	      "the network is lost while the clear of the second list waits: never sent, the old list stays");
	scene_second_list();
	poll_clear(&poll, false, now);
	send();
	poll_wifi(&poll, false, now);
	check(poll.flow.phase == DTC_FLOW_UNKNOWN && old_is_second() && poll_take_events(&poll) == (POLL_EVENT_OLD | POLL_EVENT_LISTS),
	      "the network is lost while the POST of the clear of the second list is under way: the second list replaces the old list");

	// A restart
	scene_second_list();
	now += 1000;
	send();
	check(request.kind == POLL_STATE && poll_clear(&poll, false, now) == DTC_FLOW_ALLOWED, "the scene: the clear of the second list waits behind the state under way");
	adapter_restart(&wican, BOOT + 1, 42, now);
	answer();
	check(poll.flow.phase == DTC_FLOW_IDLE && !poll.has_list && old_is(read_text, 2) && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS),
	      "the adapter restarts while the clear of the second list waits: never sent, the old list stays");
	exchange();
	check(sent("SCV") && wican.seq == 0, "the clear that waited when the restart showed is never sent");
	scene_second_list();
	poll_clear(&poll, false, now);
	send();
	reply(0, NULL, NULL);
	adapter_restart(&wican, BOOT + 1, 42, now);
	second();
	check(poll.flow.phase == DTC_FLOW_UNKNOWN && !poll.has_list && old_is_second() && poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_OLD | POLL_EVENT_LISTS),
	      "the adapter restarts after the POST of the clear got no answer: unknown, the second list replaces the old list");

	// The clear waits too long
	scene_second_list();
	poll_clear(&poll, false, now);
	now = 715001;
	check(send() && request.kind == POLL_STATE && poll.flow.phase == DTC_FLOW_LIST && old_is(read_text, 2) && poll.events == 0,
	      "a clear of the second list that waited 600001 ms is not handed out: the old list stays, no event");

	// The old list that was read from the flash
	adapter_init(&wican, 0);
	wican.read_text = read_text;
	join(OWN, 100000);
	poll_stored(&poll, NULL, 0, SHORTEST_RESULT, strlen(SHORTEST_RESULT), work, POLL_TOKENS);
	exchange();
	seconds(2);
	poll_read(&poll, now);
	exchange();
	seconds(5);
	poll_take_events(&poll);
	check(poll.flow.phase == DTC_FLOW_LIST && shows_list(read_text, 2) && poll_clear(&poll, false, now) == DTC_FLOW_ALLOWED && strcmp(poll.old_text, SHORTEST_RESULT) == 0,
	      "the scene: an old list from the flash, and the clear of a list just read waits");
	send();
	check(request.kind == POLL_DTC_CLEAR && poll.has_old && strcmp(poll.old_text, SHORTEST_RESULT) == 0 && poll.old.ecu_count == 0 && poll_take_events(&poll) == 0,
	      "that clear goes out: the old list from the flash is still the old list, nothing is to be stored");
	reply(409, "{\"accepted\":false,\"reason\":\"busy\",\"seq\":42}", NULL);
	check(reason_is("busy") && poll.has_old && strcmp(poll.old_text, SHORTEST_RESULT) == 0 && poll.old.ecu_count == 0 && poll.old.dtc_count == 0 &&
	      poll_take_events(&poll) == POLL_EVENT_LISTS, "that clear is refused: the old list from the flash outlasts it");
}

// An answer whose body had no room at the caller: the status that came, with an empty body
static void test_no_room(void)
{
	answer_t lost;

	join(NULL, 5000);
	send();
	reply(200, "", NULL);
	check(conn_state(&poll.conn) == NULL && poll.conn.failed_rounds == 1 && !poll.conn.no_api && poll.http_ok == 1 && poll.http_failed == 0 && !send(),
	      "a state without room, 200 with an empty body: the round has failed, and an answer is counted");

	scene_result();
	reply(200, "", "42");
	check(result_gone() && reason_is("no_result") && poll.conn.failed_rounds == 0 && poll.http_failed == 0 && send() && request.kind == POLL_VALUES,
	      "a result without room, 200 with its header and an empty body: it cannot be had - the read failed with no_result, the round goes on");
	scene_clear_result();
	reply(200, NULL, "43");
	check(result_gone() && poll.flow.phase == DTC_FLOW_UNKNOWN && old_is(read_text, 2) && poll.conn.failed_rounds == 0,
	      "the result of a clear without room, 200 with its header and no body: the outcome is unknown, the round goes on");
	scene_result();
	reply(0, "", "42");
	check(poll.flow.phase == DTC_FLOW_READING && poll.conn.failed_rounds == 1 && poll.http_failed == 1 && !send(),
	      "status 0 with an empty body is something else: no answer for the result - the round has failed, the read waits and the result is asked for again");

	adapter_init(&wican, 0);
	scene_catalog();
	reply(200, "", NULL);
	check(!poll.catalog_complete && !poll.conn.want_catalog && poll.conn.failed_rounds == 0 && poll.catalog.count == 1 && poll.http_ok == 2 && send() && request.kind == POLL_VALUES,
	      "a profile without room, 200 with an empty body: done for this connection as with any body that cannot be used, the round goes on");
	adapter_init(&wican, 0);
	scene_catalog();
	reply(0, "", NULL);
	check(poll.conn.want_catalog && poll.conn.failed_rounds == 1 && poll.http_failed == 1 && !send(), "status 0 with an empty body for the profile is no answer: the round has failed");
	adapter_init(&wican, 0);
	join(OWN, 100000);
	poll_stored(&poll, stored_catalog, strlen(stored_catalog), NULL, 0, work, POLL_TOKENS);
	send();
	answer();
	send();
	reply(200, "", NULL);
	check(request.kind == POLL_CATALOG && !poll.catalog_complete && poll.catalog.count == 3 && catalog_find(&poll.catalog, "OIL_TEMP_OLD") == 2,
	      "a profile without room leaves the catalogue as it was: the stored one still serves");

	scene_values();
	reply(200, "", NULL);
	check(poll.conn.failed_rounds == 1 && poll.values.count == 1 && !poll.values.has_pass && poll.http_ok == 3 && poll.http_failed == 0,
	      "values without room, 200 with an empty body: the round has failed, no value is taken, and an answer is counted");

	read_answered(202, "");
	check(undecided(DTC_FLOW_READ_SENT) && poll.http_ok == 8, "the 202 of a read without room, with an empty body: accepted without a number, the states decide");
	read_answered(409, "");
	check(reason_is("http_409") && poll.http_ok == 8, "a refusal without room, 409 with an empty body: failed with the reason of its status");
	scene_list();
	second();
	poll_clear(&poll, false, now);
	send();
	adapter_answer(&wican, &request, now, &lost);
	reply(202, "", NULL);
	check(undecided(DTC_FLOW_CLEAR_SENT) && !poll.has_old, "the 202 of a clear without room: not accepted with a number, no old list yet");
	second();
	check(poll.flow.phase == DTC_FLOW_CLEARING && poll.flow.seq == 43 && old_is(read_text, 2), "the state behind the 202 without room shows the clear: it goes on as accepted, its list is the old list");
}

// An adapter that accepts a request and does not end it
static void test_wait(void)
{
	scene();
	wican.pickup_ms = 400000;
	poll_read(&poll, now);
	exchange();
	check(poll.flow.phase == DTC_FLOW_READING && poll.flow.seq == 42 && poll.flow.accepted_ms == 102000, "the scene: the own read was accepted at 102000 by an adapter that leaves it queued");
	seconds(180);
	check(now == 282000 && poll.flow.phase == DTC_FLOW_READING && view() == CONN_VIEW_SCAN && conn_state(&poll.conn)->dtc.phase == WICAN_DTC_QUEUED,
	      "a state 180000 ms after the acceptance shows the read still queued: the display still waits");
	scene();
	wican.pickup_ms = 400000;
	poll_read(&poll, now);
	exchange();
	seconds(179);
	at(282001);
	check(reason_is("no_answer") && !poll.has_list && view() == CONN_VIEW_SCAN && poll.conn.failed_rounds == 0,
	      "a state 180001 ms after the acceptance shows the read still queued: the read is given up, no answer, although the adapter answers every round");
	check(poll_read(&poll, now) == DTC_FLOW_BUSY && poll_clear(&poll, false, now) == DTC_FLOW_BUSY, "the adapter still shows its scan queued: nothing is offered while it does");

	scene_list();
	second();
	wican.pickup_ms = 400000;
	poll_clear(&poll, false, now);
	exchange();
	check(poll.flow.phase == DTC_FLOW_CLEARING && poll.flow.accepted_ms == 107000, "the scene: the own clear was accepted at 107000 by an adapter that leaves it queued");
	seconds(180);
	check(now == 287000 && poll.flow.phase == DTC_FLOW_CLEARING && shows_list(read_text, 2), "a state 180000 ms after the acceptance shows the clear still queued: the display still waits, the list shown");
	scene_list();
	second();
	wican.pickup_ms = 400000;
	poll_clear(&poll, false, now);
	exchange();
	seconds(179);
	poll_take_events(&poll);
	at(287001);
	check(poll.flow.phase == DTC_FLOW_UNKNOWN && !poll.has_list && old_is(read_text, 2) && poll_take_events(&poll) == POLL_EVENT_LISTS,
	      "a state 180001 ms after the acceptance shows the clear still queued: its outcome is unknown, the list is dropped and stays the old list");
}

// An adapter whose polling pass takes longer than a value stays fresh
static void test_slow_pass(void)
{
	int renewed = 0, not_fresh = 0, refused = 0;
	int i;

	adapter_init(&wican, 0);
	wican.pass_time = 3500;
	join(OWN, 100000);
	exchange();
	seconds(2);
	check(conn_state(&poll.conn)->pass == 29 && seen_at("ENGINE_RPM") == 102000, "the scene: a polling pass takes 3500 ms; the 29th ended at 101500, its values arrived at 102000");
	for(i = 0; i < 70; i++)
	{
		second();
		if(seen_at("ENGINE_RPM") == now) renewed++;
		if(!fresh("ENGINE_RPM")) not_fresh++;
		if(read_block() != DTC_FLOW_ALLOWED) refused++;
	}
	check(renewed == 20, "in 70 s the values are renewed 20 times, 3 or 4 s apart: only when a pass has ended");
	check(not_fresh == 10, "in 10 of the 70 s the engine speed is 3000 ms old at the end of the round: not fresh");
	check(refused == 0, "a read is offered in every one of the 70 s: an engine speed that is not fresh counts while it is not gone");
}

static void test_outage(void)
{
	// The adapter stops answering while the own read runs. The network is there since 100000: grace until 115000.
	scene();
	poll_read(&poll, now);
	exchange();
	wican.dead = true;
	at(103000);
	at(104000);
	at(106000);
	at(111000);
	check(sent("rSSSS") && poll.conn.failed_rounds == 4 && poll.flow.phase == DTC_FLOW_READING && !poll.lost && view() != CONN_VIEW_NO_ANSWER && poll.http_failed == 4,
	      "four rounds without an answer within the grace time: the own read still waits");
	at(120999);
	check(sent("") && poll.flow.phase == DTC_FLOW_READING, "between the answers nothing is decided");
	at(121000);
	check(sent("S") && view() == CONN_VIEW_NO_ANSWER && reason_is("no_answer") && poll.lost && poll.http_failed == 5,
	      "the fifth round fails after the grace time: no answer - the own read failed with the answer that showed it");
	wican.dead = false;
	now = 131000;
	send();
	answer();
	check(view() == CONN_VIEW_NO_ANSWER && poll.lost, "the first state that is answered again does not end the outage: its round is not over");
	exchange();
	check(view() == CONN_VIEW_LIVE && !poll.lost, "the round that ends without a failure ends the outage");

	// A second outage is one of its own
	second();
	poll_dismiss(&poll);
	check(poll_read(&poll, now) == DTC_FLOW_ALLOWED, "the scene: two answered rounds later the user reads again");
	exchange();
	wican.dead = true;
	at(133000);
	at(134000);
	check(poll.flow.phase == DTC_FLOW_READING && !poll.lost && poll.conn.failed_rounds == 2, "two failed rounds are no outage");
	at(136000);
	check(view() == CONN_VIEW_NO_ANSWER && reason_is("no_answer") && poll.lost, "the third failed round, long after the grace time: the second outage ends the second read");

	// The edge of the grace time: the network is there since 100000
	scene();
	poll_read(&poll, now);
	exchange();
	wican.dead = true;
	at(103000);
	at(104000);
	now = 106000;
	send();
	now = 114999;
	reply(0, NULL, NULL);
	check(poll.conn.failed_rounds == 3 && poll.flow.phase == DTC_FLOW_READING && !poll.lost, "the third round fails 14999 ms after the network came: within the grace time, the own read waits");
	scene();
	poll_read(&poll, now);
	exchange();
	wican.dead = true;
	at(103000);
	at(104000);
	now = 106000;
	send();
	now = 115000;
	reply(0, NULL, NULL);
	check(poll.conn.failed_rounds == 3 && reason_is("no_answer") && poll.lost, "the third round fails 15000 ms after the network came: no answer, the own read failed");

	// The same while the own clear runs: the list goes with the answer that shows the outage
	scene_clearing();
	wican.dead = true;
	at(108000);
	at(109000);
	at(111000);
	check(poll.flow.phase == DTC_FLOW_CLEARING && shows_list(read_text, 2) && poll.conn.failed_rounds == 3 && poll_take_events(&poll) == 0,
	      "three rounds without an answer within the grace time: the own clear still waits, the list is shown");
	at(116000);
	check(poll.flow.phase == DTC_FLOW_UNKNOWN && !poll.has_list && old_is(read_text, 2) && poll_take_events(&poll) == POLL_EVENT_LISTS && poll.lost,
	      "the round that fails after the grace time: the outcome of the clear is unknown and the list is dropped, both with that answer");

	// The POST of the clear got no answer, and then nothing answers any more: it may have arrived
	scene_list();
	second();
	poll_clear(&poll, false, now);
	send();
	reply(0, NULL, NULL);
	wican.dead = true;
	at(108000);
	at(109000);
	at(111000);
	check(undecided(DTC_FLOW_CLEAR_SENT) && shows_list(read_text, 2) && !poll.has_old && poll.conn.failed_rounds == 3 && poll_take_events(&poll) == 0,
	      "the POST of a clear got no answer and three rounds failed within the grace time: not decided, the list is shown, nothing became the old list");
	at(116000);
	check(poll.flow.phase == DTC_FLOW_UNKNOWN && !poll.has_list && old_is(read_text, 2) && poll.lost && poll_take_events(&poll) == (POLL_EVENT_OLD | POLL_EVENT_LISTS),
	      "the round that fails after the grace time: the clear without an answer is unknown, and its list is the old list with that answer");

	// Nothing to lose
	scene_list();
	wican.dead = true;
	at(107000);
	at(108000);
	at(110000);
	at(115000);
	check(view() == CONN_VIEW_NO_ANSWER && poll.lost && poll.flow.phase == DTC_FLOW_LIST && shows_list(read_text, 2) && poll_take_events(&poll) == 0,
	      "an outage while the list is shown: the list stays");
}

// What an answer that is not taken must leave alone
typedef struct
{
	bool asking;
	poll_kind_t asked;
	uint32_t ok, failed, events;
	dtc_flow_phase_t phase;
	bool posted;
	int values;
	uint64_t volts_ms;
	bool complete;
	int entries;
	int failed_rounds;
	bool conn_asking;
	uint32_t pass;
	bool has_old;
} untouched_t;

static untouched_t untouched(void)
{
	untouched_t is = {poll.asking, poll.asked, poll.http_ok, poll.http_failed, poll.events, poll.flow.phase, poll.flow.posted, poll.values.count,
	                  value("@BATT_V") != NULL ? seen_at("@BATT_V") : 0, poll.catalog_complete, poll.catalog.count, poll.conn.failed_rounds, poll.conn.asking,
	                  poll.values.pass, poll.has_old};

	return is;
}

static bool same_as(untouched_t was)
{
	untouched_t is = untouched();

	return is.asking == was.asking && is.asked == was.asked && is.ok == was.ok && is.failed == was.failed && is.events == was.events && is.phase == was.phase &&
	       is.posted == was.posted && is.values == was.values && is.volts_ms == was.volts_ms && is.complete == was.complete && is.entries == was.entries &&
	       is.failed_rounds == was.failed_rounds && is.conn_asking == was.conn_asking && is.pass == was.pass && is.has_old == was.has_old;
}

// An answer that fits a request of this kind
static void good_answer(poll_kind_t kind, const poll_request_t *as)
{
	static const char *const bodies[] = {"", NULL, NULL, CONFIG_NO_RPM, "{\"ENGINE_RPM\":1,\"OTHER\":2}", "{\"accepted\":true,\"seq\":43}", "{\"accepted\":true,\"seq\":43}"};
	const char *body = kind == POLL_STATE ? patched(state_example, "\"pass\":1234", "\"pass\":77") : kind == POLL_RESULT ? read_text : kind <= POLL_DTC_CLEAR ? bodies[kind] : "";

	poll_apply(&poll, as, kind == POLL_DTC_READ || kind == POLL_DTC_CLEAR ? 202 : 200, body, strlen(body), "42", now, work, POLL_TOKENS);
}

// A request of kind `kind` under way, in a scene that leads to it. false if the scene does not get there.
static bool under_way(poll_kind_t kind)
{
	if(kind == POLL_RESULT)
	{
		scene_result();
	}
	else if(kind == POLL_CATALOG)
	{
		adapter_init(&wican, 0);
		scene_catalog();
	}
	else if(kind == POLL_DTC_CLEAR)
	{
		scene_list();
		second();
		poll_clear(&poll, false, now);
		send();
	}
	else
	{
		scene();
		if(kind == POLL_DTC_READ) poll_read(&poll, now);
		now += 1000;
		until(kind);
	}
	return request.kind == kind && poll.asking && poll.asked == kind;
}

// An answer without a request (NULL) for every kind of request under way, in a child process: the module
// must not look at what is not there
static bool without_request(void)
{
	untouched_t was;
	int k;

	for(k = POLL_STATE; k <= POLL_DTC_CLEAR; k++)
	{
		if(!under_way((poll_kind_t)k)) return false;
		was = untouched();
		good_answer((poll_kind_t)k, NULL);
		if(!same_as(was)) return false;
		poll_apply(&poll, NULL, 0, NULL, 0, NULL, now, work, POLL_TOKENS);
		if(!same_as(was)) return false;

		// The answer that fits is still taken
		good_answer((poll_kind_t)k, &request);
		if(poll.asking || poll.http_ok != was.ok + 1) return false;
	}
	return true;
}

// An answer that comes later than its request went out: the time of the answer is what conn is told
static void test_timing(void)
{
	char expected[128];
	int i;

	scene();
	now = 103000;
	send();
	now = 103400;
	reply(0, NULL, NULL);
	now = 104399;
	check(poll.conn.failed_rounds == 1 && !send(), "a state without an answer 400 ms after it went out: 999 ms after that failure it is not time for the next round");
	now = 104400;
	check(send() && request.kind == POLL_STATE, "1000 ms after the failure of the state the next round begins");

	scene_result();
	now = 106700;
	reply(0, NULL, NULL);
	now = 107699;
	check(poll.conn.failed_rounds == 1 && !send(), "a result without an answer 700 ms after its round began: 999 ms after that failure it is not time yet");
	now = 107700;
	check(send() && request.kind == POLL_STATE, "1000 ms after the failure of the result the next round begins");

	adapter_init(&wican, 0);
	scene_catalog();
	now = 100250;
	reply(503, NULL, NULL);
	now = 101249;
	check(poll.conn.failed_rounds == 1 && !send(), "a profile that fails 250 ms after its round began: 999 ms after that failure it is not time yet");
	now = 101250;
	check(send() && request.kind == POLL_STATE, "1000 ms after the failure of the profile the next round begins");

	scene();
	now = 103000;
	until(POLL_VALUES);
	now = 103900;
	reply(500, NULL, NULL);
	now = 104899;
	check(poll.conn.failed_rounds == 1 && !send(), "values that fail 900 ms after their round began: 999 ms after that failure it is not time yet");
	now = 104900;
	check(send() && request.kind == POLL_STATE, "1000 ms after the failure of the values the next round begins");

	// The 404 of a firmware without the API arrives 600 ms after the state was asked for
	adapter_init(&wican, 0);
	wican.api = false;
	join(OWN, 5000);
	send();
	now = 5600;
	answer();
	exchange();
	strcpy(expected, "SCV");
	for(i = 0; i < 30; i++)
	{
		at(6000 + (uint64_t)i * 1000);
		strcat(expected, "V");
	}
	check(sent(expected), "29400 ms after the 404 arrived the state is not asked for again");
	at(36000);
	check(sent("SV"), "30400 ms after the 404 arrived the state is asked for again: the 30 s count from the answer");

	// The network: the time of the call is the time it came
	adapter_init(&wican, 0);
	wican.dead = true;
	now = 100000;
	poll_init(&poll, OWN);
	poll_wifi(&poll, true, 100700);
	check(poll.conn.wifi_since_ms == 100700, "the time the network came is the time poll_wifi() was called with");
}

static void test_ignored(void)
{
	static const poll_kind_t kinds[] = {POLL_NONE, POLL_STATE, POLL_RESULT, POLL_CATALOG, POLL_VALUES, POLL_DTC_READ, POLL_DTC_CLEAR, (poll_kind_t)7, (poll_kind_t)99};
	poll_request_t other;
	untouched_t was;
	int wrong = 0;
	size_t i;
	int k;

	// No request under way
	for(i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++)
	{
		scene();
		was = untouched();
		memset(&other, 0, sizeof(other));
		other.kind = kinds[i];
		now += 300;
		good_answer(kinds[i], &other);
		if(!same_as(was)) wrong++;
	}
	check(wrong == 0, "an answer while no request is under way is ignored, of whatever kind it claims to be");

	// Every kind of request under way, and an answer for every other kind
	for(k = POLL_STATE; k <= POLL_DTC_CLEAR; k++)
	{
		for(i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++)
		{
			if(kinds[i] == (poll_kind_t)k) continue;

			if(!under_way((poll_kind_t)k)) wrong++;
			was = untouched();
			other = request;
			other.kind = kinds[i];
			good_answer(kinds[i], &other);
			if(!same_as(was))
			{
				printf("  under way %d, answered as %d: not ignored\n", k, (int)kinds[i]);
				wrong++;
			}

			// The answer that fits is still taken
			good_answer((poll_kind_t)k, &request);
			if(poll.asking || poll.http_ok != was.ok + 1) wrong++;
		}
	}
	check(wrong == 0, "an answer of another kind than the request under way is ignored, whatever it brings; the fitting answer is taken afterwards");
	check(survives(without_request), "an answer without a request (NULL) is ignored and does not end the request under way, of whatever kind that is");
}

static void test_dismiss(void)
{
	read_answered(409, "{\"accepted\":false,\"reason\":\"busy\",\"seq\":42}");
	poll_dismiss(&poll);
	check(poll.flow.phase == DTC_FLOW_IDLE && poll_take_events(&poll) == 0, "the user leaves a failure: idle, no list changed");
	scene_list();
	poll_dismiss(&poll);
	check(poll.flow.phase == DTC_FLOW_IDLE && poll.flow.read_seq == 0 && !poll.has_list && poll_take_events(&poll) == POLL_EVENT_LISTS, "the user leaves the list: it is dropped");
	scene_cleared();
	poll_dismiss(&poll);
	check(poll.flow.phase == DTC_FLOW_IDLE && !poll.has_cleared && old_is(read_text, 2) && poll_take_events(&poll) == POLL_EVENT_LISTS,
	      "the user leaves the outcome of the clear: it is dropped, the old list stays");
	scene_clearing();
	poll_wifi(&poll, false, now);
	poll_take_events(&poll);
	poll_dismiss(&poll);
	check(poll.flow.phase == DTC_FLOW_IDLE && old_is(read_text, 2) && poll_take_events(&poll) == 0, "the user leaves an unknown outcome: idle");
	scene();
	poll_dismiss(&poll);
	check(poll.flow.phase == DTC_FLOW_IDLE && poll_take_events(&poll) == 0, "leaving while nothing is shown changes nothing");

	scene();
	poll_read(&poll, now);
	poll_dismiss(&poll);
	check(poll.flow.phase == DTC_FLOW_READ_SENT && send() && request.kind == POLL_DTC_READ, "leaving while a read waits to be sent changes nothing: it goes out");
	answer();
	poll_dismiss(&poll);
	check(poll.flow.phase == DTC_FLOW_READING && poll.flow.seq == 42, "leaving while the own read runs changes nothing");
	scene_list();
	second();
	poll_clear(&poll, false, now);
	poll_dismiss(&poll);
	check(poll.flow.phase == DTC_FLOW_CLEAR_SENT && shows_list(read_text, 2) && poll_take_events(&poll) == 0, "leaving while a clear waits to be sent changes nothing: the list stays shown");
	scene_clearing();
	poll_dismiss(&poll);
	check(poll.flow.phase == DTC_FLOW_CLEARING && shows_list(read_text, 2) && poll_take_events(&poll) == 0, "leaving while the own clear runs changes nothing");
}

static void test_events(void)
{
	adapter_init(&wican, 0);
	join(NULL, 100000);
	check(poll_take_events(&poll) == 0, "no event without a cause");
	poll_stored(&poll, NULL, 0, read_text, strlen(read_text), work, POLL_TOKENS);
	send();
	answer();
	check(poll.events == (POLL_EVENT_LISTS | POLL_EVENT_BOUND), "events collect until they are taken");
	check(poll_take_events(&poll) == (POLL_EVENT_LISTS | POLL_EVENT_BOUND) && poll.events == 0 && poll_take_events(&poll) == 0, "taking the events clears them");
	adapter_init(&wican, 0);
	join(NULL, 100000);
	send();
	answer();
	poll_stored(&poll, NULL, 0, read_text, strlen(read_text), work, POLL_TOKENS);
	check(poll_take_events(&poll) == (POLL_EVENT_LISTS | POLL_EVENT_BOUND), "an old list that is put in joins the events that wait to be taken");
	exchange();
	seconds(5);
	check(poll_take_events(&poll) == 0 && strcmp(poll.bound_id, OWN) == 0, "the display is bound once: the states that follow raise nothing");
}

static void test_late_clear(void)
{
	// The list of the read 42 ended at 106000 for the display; the adapter counts from 105300
	scene_list();
	at(705500);
	check(sent("SV") && poll.flow.phase == DTC_FLOW_LIST && fresh("ENGINE_RPM") && poll_take_events(&poll) == POLL_EVENT_CATALOG,
	      "the scene: the list is shown since almost 600 s; the catalogue is stored with the first answer after its rest");
	now = 706001;
	check(poll_clear(&poll, false, now) == DTC_FLOW_LIST_OLD && poll.flow.phase == DTC_FLOW_LIST, "a clear 600001 ms after the read ended is refused: the list is old");
	now = 706000;
	check(poll_clear(&poll, false, now) == DTC_FLOW_ALLOWED, "a clear 600000 ms after the read ended is allowed");
	now = 706001;
	check(!send() && request.kind == POLL_NONE && poll.flow.phase == DTC_FLOW_LIST && poll.flow.to_send == DTC_FLOW_SEND_NOTHING,
	      "the confirmed clear gets its turn 1 ms too late: it is not handed out, the flow is back at the list");
	check(!poll.has_old && poll_take_events(&poll) == 0 && shows_list(read_text, 2), "a clear that is not handed out makes no old list and raises nothing");
	at(706500);
	check(sent("SV") && wican.seq == 42, "the clear that came too late is never sent");

	scene_list();
	at(705500);
	poll_take_events(&poll);
	now = 706000;
	poll_clear(&poll, false, now);
	check(send() && request.kind == POLL_DTC_CLEAR && !poll.has_old && poll_take_events(&poll) == 0,
	      "the confirmed clear gets its turn exactly 600000 ms after the read ended: it is handed out");
	answer();
	check(reason_is("read_required") && !poll.has_list && !poll.has_old, "the adapter counts the 600 s from the end of the read, not from the arrival of the list: it refuses");
}

// The adapter and the display start together
static void test_boot(void)
{
	uint32_t events;

	adapter_init(&wican, 100000);
	wican.autopid = WICAN_AUTOPID_STARTING;
	join(NULL, 100000);
	exchange();
	check(sent("SC") && view() == CONN_VIEW_STARTING && strcmp(poll.bound_id, OWN) == 0 && poll.catalog_complete && poll.catalog.count == 4,
	      "an adapter that is starting: the display binds to it and loads its profile, no values are asked for");
	check(poll_take_events(&poll) == POLL_EVENT_BOUND && number_of("@BATT_V") == 12.4, "the first state of the starting adapter binds the display and brings the voltage");
	second();
	wican.autopid = WICAN_AUTOPID_RUN;
	seconds(2);
	check(sent("S SV SV") && view() == CONN_VIEW_LIVE && seen_at("ENGINE_RPM") == 103000, "AutoPID runs: the values are asked for every second and renewed");
	check(poll_read(&poll, now) == DTC_FLOW_STARTING && poll.flow.phase == DTC_FLOW_IDLE && !send(), "3 s after the adapter started a read is refused: poll_read() says why, nothing is sent");
	seconds(11);
	check(read_block() == DTC_FLOW_STARTING, "14 s after the adapter started a read is still refused");
	second();
	check(read_block() == DTC_FLOW_ALLOWED && poll_take_events(&poll) == 0, "15 s after the adapter started a read is allowed");
	seconds(14);
	events = poll_take_events(&poll);
	second();
	check(events == 0 && poll_take_events(&poll) == POLL_EVENT_CATALOG, "the catalogue stands since 30 s and none is stored: POLL_EVENT_CATALOG, not a second earlier");
	seconds(60);
	check(poll_take_events(&poll) == 0, "the catalogue is stored once");
}

static void test_guard(void)
{
	uint32_t events;

	// One write per connection
	adapter_init(&wican, 0);
	join(OWN, 100000);
	exchange();
	seconds(30);
	check((poll_take_events(&poll) & POLL_EVENT_CATALOG) != 0, "the scene: the catalogue of the first connection is stored after 30 s");
	wican.has_rpm = false;
	seconds(40);
	check(poll_take_events(&poll) == 0 && poll.catalog_complete && catalog_find(&poll.catalog, "ENGINE_RPM") == 1 && !poll.catalog.entries[1].in_profile,
	      "the profile changes on the same connection and rests for 30 s: the catalogue is not stored a second time");

	// A new connection allows one more
	poll_wifi(&poll, false, now);
	now += 1000;
	poll_wifi(&poll, true, now);
	exchange();
	seconds(29);
	events = poll_take_events(&poll);
	second();
	check(events == 0 && poll_take_events(&poll) == POLL_EVENT_CATALOG, "after the network came back the changed catalogue is stored, 30 s after it was loaded again");

	// And so does a restart of the adapter
	wican.has_rpm = true;
	adapter_restart(&wican, BOOT + 1, 42, now + 500);
	second();
	check(poll_take_events(&poll) == (POLL_EVENT_FORGET | POLL_EVENT_LISTS) && poll.catalog.entries[1].in_profile, "the scene: the adapter restarts with the first profile");
	seconds(29);
	events = poll_take_events(&poll);
	second();
	check(events == 0 && poll_take_events(&poll) == POLL_EVENT_CATALOG, "after a restart of the adapter the changed catalogue is stored, 30 s after it was loaded again");
}

// Whether the bytes behind `count` tokens are still the pattern
static bool guard_intact(const json_token_t *room, int count, int total)
{
	const unsigned char *bytes = (const unsigned char *)(room + count);
	size_t size = (size_t)(total - count) * sizeof(json_token_t);
	size_t i;

	for(i = 0; i < size; i++)
	{
		if(bytes[i] != 0xA5) return false;
	}
	return true;
}

#define ROOM_TOKENS 72

// Every answer with every room for the JSON reader from none to more than enough
static void test_rooms(void)
{
	static json_token_t room[ROOM_TOKENS + 8];
	int wrong_state = 0, wrong_result = 0, wrong_catalog = 0, wrong_values = 0, wrong_post = 0, wrong_stored = 0, damaged = 0;
	int count;

	for(count = 0; count <= ROOM_TOKENS; count++)
	{
		// The state of API.md has 59 tokens
		memset(room, 0xA5, sizeof(room));
		join(NULL, 5000);
		send();
		poll_apply(&poll, &request, 200, state_example, strlen(state_example), NULL, now, room, count);
		if(count >= 59 ? conn_state(&poll.conn) == NULL || value("@BATT_V") == NULL || poll.bound_id[0] == '\0' :
		                 conn_state(&poll.conn) != NULL || poll.conn.failed_rounds != 1 || poll.values.count != 0 || poll.bound_id[0] != '\0') wrong_state++;
		if(!guard_intact(room, count, ROOM_TOKENS + 8)) damaged++;

		// The result of the read has 32
		memset(room, 0xA5, sizeof(room));
		scene_result();
		poll_apply(&poll, &request, 200, read_text, strlen(read_text), "42", now, room, count);
		if(count >= 32 ? !shows_list(read_text, 2) || poll.flow.phase != DTC_FLOW_LIST : !result_gone() || !reason_is("no_result")) wrong_result++;
		if(!guard_intact(room, count, ROOM_TOKENS + 8)) damaged++;

		// The profile without ENGINE_RPM has 13
		memset(room, 0xA5, sizeof(room));
		adapter_init(&wican, 0);
		scene_catalog();
		poll_apply(&poll, &request, 200, config_without_rpm, strlen(config_without_rpm), NULL, now, room, count);
		if(count >= 13 ? !poll.catalog_complete || poll.catalog.count != 3 : poll.catalog_complete || poll.catalog.count != 1 || poll.conn.want_catalog) wrong_catalog++;
		if(!guard_intact(room, count, ROOM_TOKENS + 8)) damaged++;

		// Two values have 5
		memset(room, 0xA5, sizeof(room));
		scene_values();
		poll_apply(&poll, &request, 200, "{\"ENGINE_RPM\":0,\"COOLANT_TMP\":21.5}", 35, NULL, now, room, count);
		if(count >= 5 ? poll.values.count != 3 || poll.conn.failed_rounds != 0 : poll.values.count != 1 || poll.conn.failed_rounds != 1) wrong_values++;
		if(!guard_intact(room, count, ROOM_TOKENS + 8)) damaged++;

		// The body of an accepted request has 5, that of a refusal 7
		memset(room, 0xA5, sizeof(room));
		scene();
		poll_read(&poll, now);
		send();
		poll_apply(&poll, &request, 202, "{\"accepted\":true,\"seq\":43}", 26, NULL, now, room, count);
		if(count >= 5 ? poll.flow.phase != DTC_FLOW_READING || poll.flow.seq != 43 : !undecided(DTC_FLOW_READ_SENT)) wrong_post++;
		if(!guard_intact(room, count, ROOM_TOKENS + 8)) damaged++;
		memset(room, 0xA5, sizeof(room));
		scene();
		poll_read(&poll, now);
		send();
		poll_apply(&poll, &request, 409, "{\"accepted\":false,\"reason\":\"busy\",\"seq\":42}", 43, NULL, now, room, count);
		if(!reason_is(count >= 7 ? "busy" : "http_409")) wrong_post++;
		if(!guard_intact(room, count, ROOM_TOKENS + 8)) damaged++;

		// The stored catalogue has 31, the stored list 32
		memset(room, 0xA5, sizeof(room));
		poll_init(&poll, OWN);
		poll_stored(&poll, stored_catalog, strlen(stored_catalog), read_text, strlen(read_text), room, count);
		if((poll.catalog.count == 3) != (count >= 31) || poll.catalog_guard.has_stored != (count >= 31) || poll.has_old != (count >= 32)) wrong_stored++;
		if(!guard_intact(room, count, ROOM_TOKENS + 8)) damaged++;
	}
	check(wrong_state == 0, "a state of 59 tokens is taken with room for 59 or more; with less the round has failed");
	check(wrong_result == 0, "a result of 32 tokens is the list with room for 32 or more; with less it cannot be had");
	check(wrong_catalog == 0, "a profile of 13 tokens is taken with room for 13 or more; with less it cannot be used and is not asked for again");
	check(wrong_values == 0, "values of 5 tokens are taken with room for 5 or more; with less the round has failed");
	check(wrong_post == 0, "the body of a POST is read with room for its tokens; with less it has no number and no reason");
	check(wrong_stored == 0, "what was stored is taken with room for its tokens, each part by itself");
	check(damaged == 0, "no token behind the room that was named is touched, whatever the answer and the room");

	// No room at all
	join(NULL, 5000);
	send();
	poll_apply(&poll, &request, 200, state_example, strlen(state_example), NULL, now, NULL, 0);
	check(conn_state(&poll.conn) == NULL && poll.conn.failed_rounds == 1, "a state without any room for the reader (NULL, 0) cannot be read");
	scene();
	poll_read(&poll, now);
	send();
	poll_apply(&poll, &request, 409, "{\"accepted\":false,\"reason\":\"busy\",\"seq\":42}", 43, NULL, now, NULL, 0);
	check(reason_is("http_409"), "a refusal without any room for the reader has the reason of its status");
}

/* ---------------------------------------------------------------------------------------------------
 * The model: poll.h written a second time, on its own copies of the modules below it. It is told what an
 * answer says (answer_t) instead of reading it, takes a state as the struct the adapter filled, and decides
 * what is shown from what was shown before a call and the phase after it.
 */

typedef struct
{
	conn_t conn;
	dtc_flow_t flow;
	values_t values;
	catalog_t catalog;
	guard_catalog_t guard;
	bool complete;
	bool joined;
	bool waits;                 // a request was handed out and has no answer yet
	poll_kind_t kind;
	uint32_t result_number, result_age;
	bool outage;
	bool list_shown, cleared_shown, has_old;
	char list_text[POLL_TEXT_SIZE], old_text[POLL_TEXT_SIZE];
	dtc_result_t list, cleared, old;
	char bound[33];
	// The start of the adapter the catalogue came from, as one text: empty before the first answer, "no api", or
	// id and boot number. 32 bytes of an id, a slash, 10 digits.
	char origin[64];
	uint32_t events;
	uint32_t answered, unanswered;
} model_t;

static model_t model;
static json_token_t model_work[POLL_TOKENS];

static void model_init(model_t *m, const char *bound_id)
{
	memset(m, 0, sizeof(*m));
	conn_init(&m->conn, bound_id);
	values_init(&m->values);
	catalog_init(&m->catalog);
	guard_catalog_init(&m->guard, false, 0);
	dtc_flow_init(&m->flow);
	strcpy(m->bound, m->conn.bound_id);
}

static void model_stored(model_t *m, const char *catalog_json, size_t catalog_length, const char *old_text, size_t old_length)
{
	static dtc_result_t parsed;

	if(catalog_json != NULL && catalog_from_json(&m->catalog, catalog_json, catalog_length, model_work, POLL_TOKENS))
	{
		guard_catalog_init(&m->guard, true, catalog_checksum(&m->catalog));
	}
	if(old_text != NULL && old_length <= POLL_TEXT_SIZE - 1 && dtc_result_parse(old_text, old_length, &parsed, model_work, POLL_TOKENS))
	{
		m->old = parsed;
		snprintf(m->old_text, sizeof(m->old_text), "%.*s", (int)old_length, old_text);
		m->has_old = true;
		m->events |= POLL_EVENT_LISTS;
	}
}

// What is shown follows the flow; a change of it is an event
static void model_shown(model_t *m, bool had_list, bool had_cleared)
{
	switch(m->flow.phase)
	{
		case DTC_FLOW_LIST:
		case DTC_FLOW_CLEAR_SENT:
		case DTC_FLOW_CLEARING:
			m->cleared_shown = false;
			break;
		case DTC_FLOW_CLEARED:
			m->list_shown = false;
			break;
		default:
			m->list_shown = false;
			m->cleared_shown = false;
			break;
	}
	if(m->list_shown != had_list || m->cleared_shown != had_cleared) m->events |= POLL_EVENT_LISTS;
}

// The list before the last clear. A clear that was sent replaces it when the answers say that the adapter
// took it, or leave open whether it did: the clear runs, nobody knows, or it failed - told by a state and not
// by the answer to its POST, which would be a refusal.
static void model_old(model_t *m, bool was_sent, bool told_by_state)
{
	bool taken = false;

	switch(m->flow.phase)
	{
		case DTC_FLOW_CLEARING:
		case DTC_FLOW_UNKNOWN:
			taken = true;
			break;
		case DTC_FLOW_FAILED:
			taken = told_by_state;
			break;
		default:
			break;
	}
	if(!was_sent || !taken) return;

	m->old = m->list;
	snprintf(m->old_text, sizeof(m->old_text), "%s", m->list_text);
	m->has_old = true;
	m->events |= POLL_EVENT_OLD | POLL_EVENT_LISTS;
}

// An adapter that cannot be reached ends the own request. Losing it twice changes nothing.
static void model_reach(model_t *m, uint64_t now_ms)
{
	conn_view_t seen = conn_view(&m->conn, now_ms);

	m->outage = seen == CONN_VIEW_NO_WIFI || seen == CONN_VIEW_NO_ANSWER;
	if(m->outage) dtc_flow_lost(&m->flow);
}

static void model_wifi(model_t *m, bool up, uint64_t now_ms)
{
	bool had_list = m->list_shown, had_cleared = m->cleared_shown;
	bool was_sent = m->flow.phase == DTC_FLOW_CLEAR_SENT;

	if(up == m->joined) return;

	m->joined = up;
	m->waits = false;
	conn_wifi(&m->conn, up, now_ms);
	if(up)
	{
		values_clear(&m->values);
		m->complete = false;
		guard_catalog_connected(&m->guard);
	}
	model_reach(m, now_ms);
	model_old(m, was_sent, false);
	model_shown(m, had_list, had_cleared);
}

static bool model_prepare(model_t *m, uint64_t now_ms, poll_request_t *expected)
{
	uint32_t number = 0;

	memset(expected, 0, sizeof(*expected));
	expected->kind = POLL_NONE;
	if(!m->joined || m->waits) return false;

	switch(dtc_flow_take(&m->flow, &number, now_ms))
	{
		case DTC_FLOW_SEND_READ:
			expected->kind = POLL_DTC_READ;
			expected->post = true;
			strcpy(expected->path, "/api/dtc?action=read");
			break;
		case DTC_FLOW_SEND_CLEAR:
			expected->kind = POLL_DTC_CLEAR;
			expected->post = true;
			snprintf(expected->path, sizeof(expected->path), "/api/dtc?action=clear&seq=%lu", (unsigned long)number);
			break;
		default:
			switch(conn_next(&m->conn, now_ms))
			{
				case CONN_ASK_STATE:
					expected->kind = POLL_STATE;
					strcpy(expected->path, "/api/state");
					break;
				case CONN_ASK_RESULT:
					expected->kind = POLL_RESULT;
					strcpy(expected->path, "/api/dtc/result");
					m->result_number = conn_state(&m->conn)->dtc.result_seq;
					m->result_age = conn_state(&m->conn)->dtc.age_s;
					break;
				case CONN_ASK_CATALOG:
					expected->kind = POLL_CATALOG;
					strcpy(expected->path, "/load_car_config");
					break;
				case CONN_ASK_VALUES:
					expected->kind = POLL_VALUES;
					strcpy(expected->path, "/autopid_data");
					break;
				default:
					return false;
			}
			break;
	}
	m->waits = true;
	m->kind = expected->kind;
	return true;
}

static void model_state(model_t *m, int status, const answer_t *a, uint64_t now_ms)
{
	bool taken = status == 200 && a->is_state;
	bool restarted;

	if(taken)
	{
		conn_got_state(&m->conn, CONN_GOT_OK, &a->state, now_ms);
		dtc_flow_state(&m->flow, &a->state, now_ms);
	}
	else
	{
		conn_got_state(&m->conn, status == 404 ? CONN_GOT_NOT_FOUND : CONN_GOT_FAILED, NULL, now_ms);
	}

	if(conn_take_bind(&m->conn, m->bound, sizeof(m->bound))) m->events |= POLL_EVENT_BOUND;
	restarted = conn_take_restarted(&m->conn);
	// What conn forgot with the network: whoever answers now is compared with whoever answered last. A foreign
	// adapter is not written down, and an answer that is neither a state nor a 404 names nobody.
	if(taken ? !m->conn.foreign : status == 404)
	{
		char origin[sizeof(m->origin)] = "no api";

		if(taken) snprintf(origin, sizeof(origin), "%s/%" PRIu32, a->state.id, a->state.boot);
		if(m->origin[0] != '\0' && strcmp(origin, m->origin) != 0) restarted = true;
		strcpy(m->origin, origin);
	}
	if(restarted)
	{
		values_clear(&m->values);
		catalog_init(&m->catalog);
		m->complete = false;
		guard_catalog_connected(&m->guard);
		m->events |= POLL_EVENT_FORGET | POLL_EVENT_LISTS;
		dtc_flow_lost(&m->flow);
		if(m->flow.phase == DTC_FLOW_LIST || m->flow.phase == DTC_FLOW_CLEARED) dtc_flow_dismiss(&m->flow);
	}
	if(taken && !m->conn.foreign && a->state.batt_mv >= 0)
	{
		char volts[64];

		snprintf(volts, sizeof(volts), "{\"@BATT_V\":%ld.%03ld}", (long)(a->state.batt_mv / 1000), (long)(a->state.batt_mv % 1000));
		values_apply(&m->values, volts, strlen(volts), -1, now_ms, model_work, POLL_TOKENS);
	}
}

static void model_result(model_t *m, int status, const answer_t *a, const char *body, uint64_t now_ms)
{
	static dtc_result_t parsed;
	bool waited = (m->flow.phase == DTC_FLOW_READING || m->flow.phase == DTC_FLOW_CLEARING) && m->flow.seq == m->result_number;
	dtc_flow_phase_t before = m->flow.phase;

	if(status == 0)
	{
		conn_got_result(&m->conn, CONN_GOT_FAILED, now_ms);
		return;
	}
	if(status == 200 && a->names && a->number == m->result_number && a->length <= POLL_TEXT_SIZE - 1 && a->is_result)
	{
		conn_got_result(&m->conn, CONN_GOT_OK, now_ms);
		dtc_flow_result(&m->flow, m->result_number, a->result_clear, a->result_count, m->result_age, now_ms);
		if(m->flow.phase != before && dtc_result_parse(body, a->length, &parsed, model_work, POLL_TOKENS))
		{
			if(m->flow.phase == DTC_FLOW_LIST)
			{
				m->list = parsed;
				snprintf(m->list_text, sizeof(m->list_text), "%.*s", (int)a->length, body);
				m->list_shown = true;
			}
			else if(m->flow.phase == DTC_FLOW_CLEARED)
			{
				m->cleared = parsed;
				m->cleared_shown = true;
			}
			m->events |= POLL_EVENT_LISTS;
		}
	}
	else
	{
		conn_got_result(&m->conn, CONN_GOT_NOT_FOUND, now_ms);
	}
	if(waited && (m->flow.phase == DTC_FLOW_READING || m->flow.phase == DTC_FLOW_CLEARING)) dtc_flow_no_result(&m->flow);
}

static void model_apply(model_t *m, const poll_request_t *as, const answer_t *a, uint64_t now_ms)
{
	bool had_list = m->list_shown, had_cleared = m->cleared_shown;
	bool was_sent = m->flow.phase == DTC_FLOW_CLEAR_SENT;
	int status = a->status < 0 ? 0 : a->status;
	const char *body = a->body != NULL ? a->body : "";
	size_t length = a->body != NULL ? a->length : 0;
	const wican_state_t *state;

	if(!m->waits || as == NULL || as->kind != m->kind) return;
	m->waits = false;

	state = conn_state(&m->conn);
	switch(m->kind)
	{
		case POLL_STATE:
			model_state(m, status, a, now_ms);
			break;
		case POLL_RESULT:
			model_result(m, status, a, body, now_ms);
			break;
		case POLL_CATALOG:
			if(status == 200)
			{
				if(catalog_apply_config(&m->catalog, body, length, model_work, POLL_TOKENS)) m->complete = true;
				conn_got_catalog(&m->conn, CONN_GOT_OK, now_ms);
			}
			else if(status == 404 || (status != 0 && state != NULL && state->autopid == WICAN_AUTOPID_OFF))
			{
				conn_got_catalog(&m->conn, CONN_GOT_NOT_FOUND, now_ms);
			}
			else
			{
				conn_got_catalog(&m->conn, CONN_GOT_FAILED, now_ms);
			}
			break;
		case POLL_VALUES:
		{
			values_result_t taken = VALUES_INVALID;

			if(status == 200) taken = values_apply(&m->values, body, length, state != NULL ? (int64_t)state->pass : -1, now_ms, model_work, POLL_TOKENS);
			if(taken == VALUES_RENEWED) catalog_note_values(&m->catalog, &m->values);
			conn_got_values(&m->conn, taken == VALUES_INVALID ? CONN_GOT_FAILED : CONN_GOT_OK, now_ms);
			break;
		}
		default:
		{
			char reason[32];

			strcpy(reason, a->post_reason);
			if(reason[0] == '\0' && status != 202) snprintf(reason, sizeof(reason), "http_%d", status);
			dtc_flow_posted(&m->flow, status, a->post_seq, reason, now_ms);
			break;
		}
	}

	model_reach(m, now_ms);
	model_old(m, was_sent, m->kind == POLL_STATE);
	model_shown(m, had_list, had_cleared);
	if(guard_catalog_due(&m->guard, catalog_checksum(&m->catalog), m->complete, now_ms)) m->events |= POLL_EVENT_CATALOG;
	if(status >= 200 && status < 500) m->answered++;
	else m->unanswered++;
}

static dtc_flow_block_t model_read(model_t *m, uint64_t now_ms)
{
	bool had_list = m->list_shown, had_cleared = m->cleared_shown;
	dtc_flow_block_t block = dtc_flow_read(&m->flow, &m->conn, &m->values, &m->catalog, now_ms);

	model_shown(m, had_list, had_cleared);
	return block;
}

static dtc_flow_block_t model_clear(model_t *m, bool button_stuck, uint64_t now_ms)
{
	return dtc_flow_clear(&m->flow, &m->conn, &m->values, &m->catalog, button_stuck, now_ms);
}

static void model_dismiss(model_t *m)
{
	bool had_list = m->list_shown, had_cleared = m->cleared_shown;

	dtc_flow_dismiss(&m->flow);
	model_shown(m, had_list, had_cleared);
}

// Field by field: the structs have gaps, and a state is copied with them
static bool same_state(const wican_state_t *a, const wican_state_t *b)
{
	return strcmp(a->id, b->id) == 0 && strcmp(a->fw, b->fw) == 0 && strcmp(a->git, b->git) == 0 && a->boot == b->boot && a->up_s == b->up_s && a->autopid == b->autopid &&
	       a->pids == b->pids && a->ecu_online == b->ecu_online && a->pass == b->pass && a->rx_age_ms == b->rx_age_ms && a->mqtt == b->mqtt && a->batt_mv == b->batt_mv &&
	       a->sleep_in_s == b->sleep_in_s && a->heap == b->heap && a->heap_min == b->heap_min && a->dtc.supported == b->dtc.supported && a->dtc.phase == b->dtc.phase &&
	       a->dtc.has_request == b->dtc.has_request && a->dtc.clear == b->dtc.clear && a->dtc.from_http == b->dtc.from_http && a->dtc.seq == b->dtc.seq &&
	       a->dtc.step == b->dtc.step && a->dtc.total == b->dtc.total && strcmp(a->dtc.name, b->dtc.name) == 0 && strcmp(a->dtc.reason, b->dtc.reason) == 0 &&
	       a->dtc.age_s == b->dtc.age_s && a->dtc.count == b->dtc.count && a->dtc.result_seq == b->dtc.result_seq;
}

static bool same_conn(const conn_t *a, const conn_t *b)
{
	return strcmp(a->bound_id, b->bound_id) == 0 && a->wifi == b->wifi && a->wifi_since_ms == b->wifi_since_ms && a->asking == b->asking && a->asked == b->asked &&
	       a->round_start_ms == b->round_start_ms && a->next_round_ms == b->next_round_ms && a->failed_rounds == b->failed_rounds && a->good_rounds == b->good_rounds &&
	       a->has_state == b->has_state && (!a->has_state || same_state(&a->state, &b->state)) && a->no_api == b->no_api && a->no_api_since_ms == b->no_api_since_ms &&
	       a->foreign == b->foreign && a->want_result == b->want_result && a->want_catalog == b->want_catalog && a->want_values == b->want_values &&
	       a->fetched_result_seq == b->fetched_result_seq && a->restarted == b->restarted && a->bind_pending == b->bind_pending;
}

static bool same_flow(const dtc_flow_t *a, const dtc_flow_t *b)
{
	return a->phase == b->phase && a->to_send == b->to_send && a->boot == b->boot && a->seq_before == b->seq_before && a->seq == b->seq && a->read_seq == b->read_seq &&
	       a->list_count == b->list_count && a->list_end_ms == b->list_end_ms && a->posted == b->posted && a->rounds_without_answer == b->rounds_without_answer &&
	       a->accepted_ms == b->accepted_ms && strcmp(a->reason, b->reason) == 0;
}

static bool same_values(const values_t *a, const values_t *b)
{
	int i;

	if(a->count != b->count || a->has_pass != b->has_pass || (a->has_pass && a->pass != b->pass) || a->dropped != b->dropped) return false;
	for(i = 0; i < a->count; i++)
	{
		if(strcmp(a->items[i].name, b->items[i].name) != 0 || a->items[i].kind != b->items[i].kind || a->items[i].number != b->items[i].number ||
		   a->items[i].seen_ms != b->items[i].seen_ms) return false;
	}
	return true;
}

static bool same_catalog(const catalog_t *a, const catalog_t *b)
{
	int i;

	if(a->count != b->count || a->dropped != b->dropped) return false;
	for(i = 0; i < a->count; i++)
	{
		if(strcmp(a->entries[i].name, b->entries[i].name) != 0 || strcmp(a->entries[i].unit, b->entries[i].unit) != 0 ||
		   strcmp(a->entries[i].value_class, b->entries[i].value_class) != 0 || a->entries[i].in_profile != b->entries[i].in_profile ||
		   a->entries[i].delivered != b->entries[i].delivered) return false;
	}
	return true;
}

static bool same_result(const dtc_result_t *a, const dtc_result_t *b)
{
	int i;

	if(a->clear != b->clear || a->duration_ms != b->duration_ms || a->dtc_count != b->dtc_count || a->ecu_count != b->ecu_count || a->code_count != b->code_count ||
	   a->cut != b->cut) return false;
	for(i = 0; i < a->ecu_count; i++)
	{
		if(strcmp(a->ecus[i].name, b->ecus[i].name) != 0 || strcmp(a->ecus[i].id, b->ecus[i].id) != 0 || a->ecus[i].cleared != b->ecus[i].cleared ||
		   a->ecus[i].first_code != b->ecus[i].first_code || a->ecus[i].code_count != b->ecus[i].code_count || a->ecus[i].status != b->ecus[i].status) return false;
	}
	for(i = 0; i < a->code_count; i++)
	{
		if(strcmp(a->codes[i].code, b->codes[i].code) != 0 || strcmp(a->codes[i].status, b->codes[i].status) != 0 || a->codes[i].active != b->codes[i].active) return false;
	}
	return true;
}

// Where the module differs from the model, NULL if nowhere
// The start the module keeps, written as the model writes it
static bool same_start(const model_t *m)
{
	char origin[sizeof(m->origin)] = "";

	if(poll.start == POLL_START_NO_API) strcpy(origin, "no api");
	if(poll.start == POLL_START_API) snprintf(origin, sizeof(origin), "%s/%" PRIu32, poll.start_id, poll.start_boot);
	return strcmp(origin, m->origin) == 0;
}

static const char *differs(const model_t *m)
{
	const guard_catalog_t *g = &poll.catalog_guard;

	if(!same_conn(&poll.conn, &m->conn)) return "the connection";
	if(!same_flow(&poll.flow, &m->flow)) return "the flow";
	if(!same_values(&poll.values, &m->values)) return "the values";
	if(!same_catalog(&poll.catalog, &m->catalog)) return "the catalogue";
	if(g->has_stored != m->guard.has_stored || (g->has_stored && g->stored_sum != m->guard.stored_sum) || g->has_seen != m->guard.has_seen ||
	   (g->has_seen && (g->seen_sum != m->guard.seen_sum || g->seen_since_ms != m->guard.seen_since_ms)) || g->written != m->guard.written) return "guard";
	if(poll.catalog_complete != m->complete) return "catalog_complete";
	if(poll.has_list != m->list_shown) return "has_list";
	if(poll.has_list && (strcmp(poll.list_text, m->list_text) != 0 || !same_result(&poll.list, &m->list))) return "the list";
	if(poll.has_cleared != m->cleared_shown) return "has_cleared";
	if(poll.has_cleared && !same_result(&poll.cleared, &m->cleared)) return "the outcome";
	if(poll.has_old != m->has_old) return "has_old";
	if(poll.has_old && (strcmp(poll.old_text, m->old_text) != 0 || !same_result(&poll.old, &m->old))) return "the old list";
	if(strcmp(poll.bound_id, m->bound) != 0) return "bound_id";
	if(!same_start(m)) return "the start the catalogue came from";
	if(poll.wifi != m->joined) return "wifi";
	if(poll.asking != m->waits || (poll.asking && poll.asked != m->kind)) return "the request under way";
	if(poll.asking && poll.asked == POLL_RESULT && (poll.asked_result_seq != m->result_number || poll.asked_age_s != m->result_age)) return "the result asked for";
	if(poll.lost != m->outage) return "lost";
	if(poll.events != m->events) return "the events";
	if(poll.http_ok != m->answered || poll.http_failed != m->unanswered) return "the counters";
	return NULL;
}

/* ---------------------------------------------------------------------------------------------------
 * Long random conversations. After every call the module is compared with the model, and the promises of
 * poll.h are watched on their own, without the model:
 *   - never two requests under way
 *   - a clear is only handed out after poll_clear() allowed it, with the number of the list shown
 *   - the old list changes exactly when the flow leaves CLEAR_SENT with the clear accepted or its outcome
 *     unknown, and is then the text of the list that was shown; POLL_EVENT_OLD is raised then and only then
 *   - has_list and has_cleared follow the flow
 *   - the events are raised when their cause happens and only then
 *   - after any conversation a healthy adapter and enough time lead back to renewed values and an allowed read
 */

#define WALKS           160
#define WALK_CALLS      2500
#define WALK_HEAL_MS    150000u     // the time a healthy adapter is given at the end of a walk

enum
{
	PROMISE_ONE_REQUEST,
	PROMISE_CLEAR,
	PROMISE_OLD,
	PROMISE_SHOWN,
	PROMISE_EVENTS,
	PROMISE_STUCK,
	PROMISES,
};

typedef struct
{
	long calls, different;
	long broken[PROMISES];
	long prepared[POLL_DTC_CLEAR + 1];
	long phases[DTC_FLOW_UNKNOWN + 1];
	long views[CONN_VIEW_LIVE + 1];
	long raised[5];             // by bit
	long lists, outcomes, unknown, failures, back_to_list, late_clears, old_lists, clears_out, clears_without_old, catalogs_anew;
	long unseen_restarts;       // catalogues started anew for an adapter that restarted, or was replaced, behind a pause of the network
	long unseen_same;           // pauses of the network behind which the start that answered before answered again
	long ignored, stale, steps_back, starts, stored, faults, no_room, at_limit, heals, healed_under_way;
} walk_result_t;

static uint32_t walk_seed;

/*
 * Never two rolls in one expression whose order C leaves open - among the arguments of one call, or on both
 * sides of an operator: gcc works out the last argument first and clang the first, and the walks of the CI
 * were other walks than the ones on a Mac. Where an expression needs two, they are rolled before it, one in
 * a statement: arguments in the order gcc had (the last one first), operands from left to right, as both did.
 */
static uint32_t walk_random(uint32_t range)
{
	walk_seed = walk_seed * 1664525u + 1013904223u;
	return (walk_seed >> 8) % range;
}

static uint32_t text_sum(const char *text)
{
	uint32_t sum = 2166136261u;

	for(; *text != '\0'; text++) sum = (sum ^ (unsigned char)*text) * 16777619u;
	return sum;
}

// The list before the last clear, to see whether a call changed it
static uint32_t old_sum(void)
{
	return poll.has_old ? text_sum(poll.old_text) * 31 + (uint32_t)poll.old.dtc_count + 1 : 0;
}

// What the screen shows of the lists, to see whether a call changed it
static uint32_t shown_sum(void)
{
	uint32_t sum = (poll.has_list ? 1u : 0u) + (poll.has_cleared ? 2u : 0u) + (poll.has_old ? 4u : 0u);

	if(poll.has_list) sum = sum * 31 + text_sum(poll.list_text);
	if(poll.has_cleared) sum = sum * 31 + poll.cleared.duration_ms * 7 + poll.cleared.dtc_count;
	if(poll.has_old) sum = sum * 31 + text_sum(poll.old_text);
	return sum;
}

// The adapter the display belongs to
static const char *walk_own(void)
{
	return poll.bound_id[0] != '\0' ? poll.bound_id : OWN;
}

static void walk_heal(void)
{
	wican.dead = false;
	wican.api = true;
	strcpy(wican.id, walk_own());
	wican.autopid = WICAN_AUTOPID_RUN;
	wican.supported = true;
	wican.ignition = true;
	wican.rpm = 0;
	wican.sleep_in_s = -1;
	wican.batt_mv = 12400;
	wican.pickup_ms = PICKUP_MS;
}

// The adapter, the vehicle or the people around them change something
static void walk_change(uint64_t world)
{
	static const int speeds[] = {0, 49, 50, 780};
	static const int32_t volts[] = {-1, 0, 11900, 14400};
	static const uint32_t firsts[] = {1, 7, 42, 0x7FFFFFFEu, 0x7FFFFFFFu};
	uint32_t change = walk_random(100);
	const char *reason;
	uint32_t number;

	adapter_catch_up(&wican, world);
	if(change < 30) walk_heal();
	else if(change < 38)
	{
		uint32_t first = firsts[walk_random(5)];

		adapter_restart(&wican, wican.boot % 1000000 + 1 + walk_random(3), first, world);
	}
	else if(change < 44) wican.dead = true;
	else if(change < 49) wican.api = false;
	else if(change < 54) strcpy(wican.id, strcmp(walk_own(), OTHER) == 0 ? OWN : OTHER);
	else if(change < 58) wican.autopid = walk_random(2) == 0 ? WICAN_AUTOPID_OFF : WICAN_AUTOPID_STARTING;
	else if(change < 63) wican.ignition = false;
	else if(change < 69) wican.rpm = speeds[walk_random(4)];
	else if(change < 72) wican.has_rpm = !wican.has_rpm;
	else if(change < 75) wican.supported = false;
	else if(change < 78) wican.sleep_in_s = 0;
	else if(change < 81) wican.batt_mv = volts[walk_random(4)];
	else if(change < 86) wican.memory = walk_random(3);
	else if(change < 97)
	{
		bool http = walk_random(2) == 0;
		bool clear = walk_random(3) == 0;

		adapter_request(&wican, clear, http, wican.seq, world, &number, &reason);
	}
	else wican.pickup_ms = wican.pickup_ms == PICKUP_MS ? 25000 : PICKUP_MS;
}

// Nothing of the answer arrives
static void walk_no_answer(answer_t *answer)
{
	static const int none[] = {0, 0, 0, -1, INT_MIN};

	memset(answer, 0, sizeof(*answer));
	answer->status = none[walk_random(5)];
}

// The body says nothing that can be read
static void walk_unreadable(answer_t *answer)
{
	answer->is_state = false;
	answer->is_result = false;
	answer->post_seq = 0;
	answer->post_reason[0] = '\0';
}

// The answer to a request the display sends at `world`. `trouble`: how often something goes wrong, in 1000.
static void walk_answer(const poll_request_t *asked, uint64_t world, uint32_t trouble, answer_t *answer, walk_result_t *result)
{
	static const int statuses[] = {500, 503, 204, 404, 409, 400, 403, 200, 201, 199, 499, 600, 301, 100, 1};
	static const char *const garbage[] = {NOT_FOUND_TEXT, "[]", "{}", "", "null", "{\"x\":1}", "{\"error\":\"No data available\"}"};
	static const struct
	{
		const char *body;
		uint32_t seq;
		const char *reason;
	} posts[] = {
		{"{\"accepted\":true,\"seq\":3000000001}", 3000000001u, ""},
		{"{\"accepted\":true,\"seq\":4294967295}", 4294967295u, ""},
		{"{\"accepted\":true,\"seq\":4294967296}", 0, ""},
		{"{\"accepted\":true,\"seq\":0}", 0, ""},
		{"{\"accepted\":true}", 0, ""},
		{"{\"accepted\":true,\"seq\":3000000001.0}", 0, ""},
		{"{\"accepted\":true,\"seq\":3e9}", 0, ""},
		{"{\"accepted\":true,\"seq\":-3000000001}", 0, ""},
		{"{\"accepted\":true,\"seq\":\"3000000001\"}", 0, ""},
		{"{\"accepted\":false,\"reason\":\"busy\",\"seq\":3000000002}", 3000000002u, "busy"},
		{"{\"accepted\":false,\"reason\":\"a_reason_of_exactly_31_bytes_xx\",\"seq\":3000000003}", 3000000003u, "a_reason_of_exactly_31_bytes_xx"},
		{"{\"accepted\":false,\"reason\":\"a_reason_of_exactly_32_bytes_xxx\",\"seq\":3000000003}", 3000000003u, ""},
		{"{\"accepted\":false,\"reason\":\"not\\u005fready\",\"seq\":0}", 0, "not_ready"},
		{"{\"accepted\":false,\"reason\":7}", 0, ""},
		{"{\"accepted\":false,\"reason\":\"\"}", 0, ""},
		{"[{\"seq\":3000000001,\"reason\":\"busy\"}]", 0, ""},
		{"{\"reason\":\"only\"}", 0, "only"},
	};
	static const int post_statuses[] = {202, 202, 202, 409, 409, 503, 400, 403, 500, 404, 200};
	bool post = asked->kind == POLL_DTC_READ || asked->kind == POLL_DTC_CLEAR;
	uint32_t fate = walk_random(1000);

	// A POST more often than the rest: it is sent rarely and has the most ways to go wrong
	if(fate >= trouble * 5 + (post ? 200 : 0))
	{
		adapter_answer(&wican, asked, world, answer);
		return;
	}
	result->faults++;
	fate = walk_random(100);
	if(fate < (post ? 26u : 12u))
	{
		// Lost on its way to the adapter
		walk_no_answer(answer);
		return;
	}
	adapter_answer(&wican, asked, world, answer);
	if(fate < (post ? 40u : 26u))
	{
		// The adapter got it, the answer is lost
		walk_no_answer(answer);
	}
	else if(fate < (post ? 50u : 42u))
	{
		// Another status. Not 202 for a POST: an adapter that accepts with the number of an old request is not healthy.
		do answer->status = statuses[walk_random(15)];
		while(post && answer->status == 202);
	}
	else if(fate < 58)
	{
		uint32_t damage = walk_random(4);

		if(damage == 0 && answer->length > 0)
		{
			answer->length -= 1 + walk_random((uint32_t)answer->length);
			body_room[answer->length] = '\0';
		}
		else if(damage == 1)
		{
			answer->body = NULL;
			answer->length = walk_random(2) * 77;
		}
		else
		{
			strcpy(body_room, garbage[walk_random(7)]);
			answer->body = body_room;
			answer->length = strlen(body_room);
		}
		walk_unreadable(answer);
	}
	else if(asked->kind == POLL_STATE && answer->is_state)
	{
		// A state of an API this display does not know
		char *api = strstr(body_room, "\"api\":1");

		if(api != NULL) api[6] = '2';
		walk_unreadable(answer);
	}
	else if(asked->kind == POLL_RESULT && answer->is_result)
	{
		if(walk_random(2) == 0)
		{
			// The header is missing, names another number, or is more than the digits of the number
			static char header[32];
			unsigned long number = (unsigned long)answer->number;

			answer->seq_header = header;
			switch(walk_random(10))
			{
				case 0: header[0] = '\0'; break;
				case 1: strcpy(header, "0"); break;
				case 2: snprintf(header, sizeof(header), " %lu", number); break;
				case 3: snprintf(header, sizeof(header), "%lu ", number); break;
				case 4: snprintf(header, sizeof(header), "0%lu", number); break;
				case 5: snprintf(header, sizeof(header), "+%lu", number); break;
				case 6: snprintf(header, sizeof(header), "%lu.0", number); break;
				case 7: snprintf(header, sizeof(header), "%lu0", number); break;
				case 8: snprintf(header, sizeof(header), "%lu", number + 1); break;
				default: answer->seq_header = NULL; break;
			}
			answer->names = false;
		}
		else
		{
			// Blanks behind a result are JSON: up to the last byte that has room, and beyond
			size_t padded = POLL_TEXT_SIZE - 2 + walk_random(4);

			memset(body_room + answer->length, ' ', padded - answer->length);
			body_room[padded] = '\0';
			answer->length = padded;
			result->at_limit++;
		}
	}
	else if(asked->kind == POLL_CATALOG && answer->status == 200)
	{
		// A profile around the size of the room of the caller
		size_t padded = POLL_BODY_SIZE - 2 + walk_random(4);

		memset(body_room + answer->length, ' ', padded - answer->length);
		body_room[padded] = '\0';
		answer->length = padded;
	}
	else if(asked->kind == POLL_VALUES && answer->status == 200)
	{
		static const char *const values[] = {"{}", "{\"ENGINE_RPM\":\"on\"}", "{\"ENGINE_RPM\":12,\"EXTRA_1\":1}", "{\"EXTRA_2\":\"off\",\"EXTRA_3\":2.5}", "{\"ENGINE_RPM\":49.9}"};

		strcpy(body_room, values[walk_random(5)]);
		answer->length = strlen(body_room);
	}
	else if(post)
	{
		uint32_t which = walk_random(sizeof(posts) / sizeof(posts[0]));

		strcpy(body_room, posts[which].body);
		answer->body = body_room;
		answer->length = strlen(body_room);
		answer->status = post_statuses[walk_random(11)];
		answer->post_seq = posts[which].seq;
		strcpy(answer->post_reason, posts[which].reason);
	}
}

// The answer arrives at the display, which tells it to module and model
static void walk_deliver(const poll_request_t *as, answer_t *answer, uint64_t now_ms, walk_result_t *result)
{
	// A body that has no room at the caller is passed as the status that came with it and an empty body
	if(answer->length >= POLL_BODY_SIZE)
	{
		answer->body = NULL;
		answer->length = 0;
		walk_unreadable(answer);
		result->no_room++;
	}
	poll_apply(&poll, as, answer->status, answer->body, answer->length, answer->seq_header, now_ms, work, POLL_TOKENS);
	model_apply(&model, as, answer, now_ms);
}

static void walk_start(walk_result_t *result)
{
	static const char *const ids[] = {OWN, OWN, OWN, NULL, "", OTHER};
	static const char *const catalogs[] = {NULL, NULL, "{}", "[]", "{\"ENGINE_RPM\":{\"unit\":\"RPM\",\"class\":\"frequency\",\"profile\":true,\"delivered\":true}}",
	                                       "{\"ENGINE_RPM\":{\"unit\":\"RPM\",\"class\":\"frequency\"}}"};
	const char *bound_id = ids[walk_random(6)];
	const char *catalog_json = catalogs[walk_random(6)];
	const char *old_text = NULL;
	uint32_t old = walk_random(6);

	if(old == 0) old_text = read_text;
	else if(old == 1) old_text = clear_text;
	else if(old == 2) old_text = "{\"state\":\"done\"}";

	poll_init(&poll, bound_id);
	model_init(&model, bound_id);
	if(walk_random(3) != 0)
	{
		poll_stored(&poll, catalog_json, catalog_json != NULL ? strlen(catalog_json) : 0, old_text, old_text != NULL ? strlen(old_text) : 0, work, POLL_TOKENS);
		model_stored(&model, catalog_json, catalog_json != NULL ? strlen(catalog_json) : 0, old_text, old_text != NULL ? strlen(old_text) : 0);
		result->stored++;
	}
	result->starts++;
}

static bool walk(uint32_t seed, walk_result_t *result)
{
	static char list_text[POLL_TEXT_SIZE];      // the text of the result that became the list shown
	static const uint32_t limits[] = {1000, 2000, 3000, 4000, 5000, 10000, 15000, 30000, 600000};
	uint64_t world = 100000;                    // the time of the world; the display sometimes reads its clock late
	uint32_t trouble = 1 + seed % 8;
	poll_request_t asked;                       // the request under way
	poll_request_t handed, expected;
	answer_t pending;                           // its answer, on its way
	bool under_way = false;
	bool confirmed = false;                     // poll_clear() allowed a clear that was not handed out yet
	uint32_t list_number = 0;                   // the number in the header of the result that became the list
	int known = 0;                              // what answered GET /api/state last on this connection: 0 nothing, 1 a state, 2 a 404
	uint32_t known_boot = 0;
	char known_id[33] = "";
	// The same for the adapter the catalogue came from: a foreign one is left out, and joining a network does
	// not forget it
	int came = 0;
	uint32_t came_boot = 0;
	char came_id[33] = "";
	const char *what = "start";
	const char *wrong;
	uint64_t now_ms = world;
	int call;

	walk_seed = seed * 2654435761u + 20261004u;
	adapter_init(&wican, 0);
	wican.boot = 1000 + seed;
	memset(&pending, 0, sizeof(pending));
	memset(&asked, 0, sizeof(asked));
	walk_start(result);
	poll_wifi(&poll, true, world);
	model_wifi(&model, true, world);

	for(call = 0; call < WALK_CALLS + 1; call++)
	{
		bool healing = call == WALK_CALLS;
		uint32_t operation = walk_random(1000);
		uint32_t step = walk_random(1000);
		uint32_t events_before, shown_before, old_before;
		dtc_flow_phase_t phase_before;
		char bound_before[33];
		bool expect_forget = false, fresh_start = false, delivered = false;
		poll_kind_t answered = POLL_NONE;           // the kind of the request whose answer this call delivered

		// The time goes on, now and then to a limit; the display reads it, now and then late
		if(step < 700) world += walk_random(200);
		else if(step < 900) world += 200 + walk_random(800);
		else if(step < 970) world += 1000 + walk_random(3000);
		else if(step < 990)
		{
			uint32_t limit = limits[walk_random(9)];

			world += limit - 1 + walk_random(3);
		}
		else if(step < 993 && poll.flow.list_end_ms + 600000 > world) world = poll.flow.list_end_ms + 599999 + walk_random(3);
		else if(step < 996 && poll.catalog_guard.seen_since_ms + 30000 > world) world = poll.catalog_guard.seen_since_ms + 29999 + walk_random(3);
		else if(step < 1000 && poll.conn.next_round_ms > world) world = poll.conn.next_round_ms - 1 + walk_random(3);
		// A confirmed clear that waits for its turn until the list is as old as it may be, or 1 or 2 ms older
		if(poll.flow.to_send == DTC_FLOW_SEND_CLEAR && walk_random(12) == 0 && poll.flow.list_end_ms + 600000 > world) world = poll.flow.list_end_ms + 600000 + walk_random(3);
		now_ms = world;
		if(walk_random(25) == 0)
		{
			uint64_t back = walk_random(4) == 0 ? walk_random(200000) : walk_random(3000);

			now_ms = world > back ? world - back : 0;
			result->steps_back++;
		}

		// Whatever is still to be taken was raised by an earlier call
		if(walk_random(5) != 0)
		{
			if(poll_take_events(&poll) != model.events) result->different++;
			model.events = 0;
		}
		// Without a network little happens: it comes back soon
		if(!poll.wifi && walk_random(20) == 0) operation = 885;
		events_before = poll.events;
		shown_before = shown_sum();
		old_before = old_sum();
		phase_before = poll.flow.phase;
		strcpy(bound_before, poll.bound_id);

		if(healing)
		{
			// The end of the walk: see below
			what = "heal";
		}
		else if(operation < 600 && under_way)
		{
			// The answer arrives. Before it, now and then, one that belongs to nothing.
			if(walk_random(12) == 0)
			{
				answer_t other = pending;
				poll_request_t as = asked;

				as.kind = (poll_kind_t)((asked.kind + 1 + walk_random(8)) % 9);
				walk_deliver(walk_random(4) == 0 ? NULL : &as, &other, now_ms, result);
				result->ignored++;
				what = "an answer of another kind";
			}
			else
			{
				bool state_taken = pending.status == 200 && pending.is_state && pending.length < POLL_BODY_SIZE;

				if(asked.kind == POLL_STATE && state_taken)
				{
					// A display that is bound takes every other id for a foreign adapter
					bool foreign = bound_before[0] != '\0' && strcmp(pending.state.id, bound_before) != 0;

					expect_forget = known == 2 || (known == 1 && (pending.state.boot != known_boot || strcmp(pending.state.id, known_id) != 0));
					if(!foreign)
					{
						bool other = came == 2 || (came == 1 && (pending.state.boot != came_boot || strcmp(pending.state.id, came_id) != 0));

						// Nothing was known of the adapter on this connection: the restart happened behind a pause
						if(other && known == 0) result->unseen_restarts++;
						if(!other && known == 0 && came != 0) result->unseen_same++;
						if(other) expect_forget = true;
						came = 1;
						came_boot = pending.state.boot;
						strcpy(came_id, pending.state.id);
					}
					known = 1;
					known_boot = pending.state.boot;
					strcpy(known_id, pending.state.id);
				}
				else if(asked.kind == POLL_STATE && pending.status == 404)
				{
					expect_forget = known == 1;
					if(came == 1 && known == 0) result->unseen_restarts++;
					if(came == 2 && known == 0) result->unseen_same++;
					if(came == 1) expect_forget = true;
					came = 2;
					known = 2;
				}
				walk_deliver(&asked, &pending, now_ms, result);
				answered = asked.kind;
				if(expect_forget) result->catalogs_anew++;
				if(asked.kind == POLL_RESULT && poll.has_list && phase_before == DTC_FLOW_READING)
				{
					// This result became the list
					list_number = pending.number;
					snprintf(list_text, sizeof(list_text), "%.*s", (int)pending.length, pending.body);
				}
				under_way = false;
				delivered = true;
				what = "the answer";
			}
		}
		else if(operation < 720)
		{
			bool got = poll_prepare(&poll, now_ms, &handed);
			bool expected_got = model_prepare(&model, now_ms, &expected);

			if(got != expected_got || handed.kind != expected.kind || handed.post != expected.post || strcmp(handed.path, expected.path) != 0) result->different++;
			if(got)
			{
				if(under_way) result->broken[PROMISE_ONE_REQUEST]++;
				if(handed.kind == POLL_DTC_CLEAR)
				{
					char path[POLL_PATH_SIZE];

					snprintf(path, sizeof(path), "/api/dtc?action=clear&seq=%lu", (unsigned long)list_number);
					if(!confirmed || list_number == 0 || strcmp(handed.path, path) != 0) result->broken[PROMISE_CLEAR]++;
					result->clears_out++;
				}
				if(handed.kind <= POLL_DTC_CLEAR) result->prepared[handed.kind]++;
				asked = handed;
				walk_answer(&asked, world, trouble, &pending, result);
				under_way = true;
			}
			// A clear that waited too long is not handed out; whatever else is due goes out
			if(phase_before == DTC_FLOW_CLEAR_SENT && poll.flow.phase == DTC_FLOW_LIST)
			{
				if(handed.kind == POLL_DTC_CLEAR) result->broken[PROMISE_CLEAR]++;
				result->late_clears++;
			}
			what = "prepare";
		}
		else if(operation < 770 && (poll.flow.phase != DTC_FLOW_LIST || walk_random(4) == 0))
		{
			dtc_flow_block_t block = poll_read(&poll, now_ms);

			if(block != model_read(&model, now_ms)) result->different++;
			what = "read";
		}
		else if(operation < 850)
		{
			bool stuck = walk_random(8) == 0;
			dtc_flow_block_t block = poll_clear(&poll, stuck, now_ms);

			if(block != model_clear(&model, stuck, now_ms)) result->different++;
			if(block == DTC_FLOW_ALLOWED) confirmed = true;
			what = "clear";
		}
		else if(operation < 865)
		{
			poll_dismiss(&poll);
			model_dismiss(&model);
			what = "dismiss";
		}
		else if(operation < 885)
		{
			// An answer although nothing was asked
			answer_t other;
			poll_request_t as;

			if(!under_way)
			{
				memset(&as, 0, sizeof(as));
				memset(&other, 0, sizeof(other));
				as.kind = (poll_kind_t)walk_random(8);
				other.status = walk_random(3) == 0 ? 0 : 202;
				other.body = "{\"accepted\":true,\"seq\":3000000001}";
				other.length = strlen(other.body);
				other.seq_header = "3000000001";
				walk_deliver(walk_random(6) == 0 ? NULL : &as, &other, now_ms, result);
				result->ignored++;
			}
			what = "an answer to nothing";
		}
		else if(operation < 885 + trouble)
		{
			bool up = poll.wifi ? walk_random(4) == 0 : walk_random(5) != 0;
			bool was_up = poll.wifi;

			if(under_way && !up && poll.wifi)
			{
				// The answer to the request that was under way comes late, or never
				poll_wifi(&poll, up, now_ms);
				model_wifi(&model, up, now_ms);
				if(walk_random(2) == 0) walk_deliver(&asked, &pending, now_ms, result);
				under_way = false;
				result->stale++;
			}
			else
			{
				poll_wifi(&poll, up, now_ms);
				model_wifi(&model, up, now_ms);
			}
			// Joining starts over: nothing is known about the adapter
			if(up != was_up) known = 0;
			what = "wifi";
		}
		else if(operation < 885 + trouble * 6)
		{
			walk_change(world);
			what = "change";
		}
		else if(operation < 886 + trouble * 6 && seed % 3 == 0)
		{
			walk_start(result);
			if(walk_random(4) != 0)
			{
				poll_wifi(&poll, true, now_ms);
				model_wifi(&model, true, now_ms);
			}
			under_way = false;
			confirmed = false;
			list_number = 0;
			known = 0;
			came = 0;
			fresh_start = true;
			what = "init";
		}

		if(healing)
		{
			// A healthy adapter and enough time: whatever was under way ends, values are renewed, a read is allowed
			uint64_t until_ms = world + WALK_HEAL_MS;
			bool was_under_way = poll.flow.phase == DTC_FLOW_READ_SENT || poll.flow.phase == DTC_FLOW_READING || poll.flow.phase == DTC_FLOW_CLEAR_SENT ||
			                     poll.flow.phase == DTC_FLOW_CLEARING;

			// Every second walk ends with the user asking to read, so that often a request is under way
			if(seed % 2 == 0 && poll_read(&poll, now_ms) != model_read(&model, now_ms)) result->different++;
			was_under_way = was_under_way || poll.flow.phase == DTC_FLOW_READ_SENT;
			walk_heal();
			// The profile with the engine speed: once delivered the name stays in the catalogue, and a read then
			// waits for a value of it
			wican.has_rpm = true;
			if(!poll.wifi)
			{
				poll_wifi(&poll, true, world);
				model_wifi(&model, true, world);
			}
			if(under_way) walk_deliver(&asked, &pending, world, result);
			for(; world < until_ms && differs(&model) == NULL; world += 100)
			{
				if(poll_prepare(&poll, world, &asked) != model_prepare(&model, world, &expected) || asked.kind != expected.kind) result->different++;
				else if(poll.asking)
				{
					adapter_answer(&wican, &asked, world, &pending);
					walk_deliver(&asked, &pending, world, result);
				}
			}
			now_ms = world;
			if(dtc_flow_read_block(&poll.flow, &poll.conn, &poll.values, &poll.catalog, world) != DTC_FLOW_ALLOWED ||
			   values_age(values_find(&poll.values, "ENGINE_RPM"), world) != VALUE_AGE_FRESH || values_age(values_find(&poll.values, "@BATT_V"), world) != VALUE_AGE_FRESH ||
			   conn_view(&poll.conn, world) != CONN_VIEW_LIVE)
			{
				printf("  walk %lu: stuck after the adapter was healthy for %u s: view %d, phase %d, read block %d, engine speed %d, voltage %d\n",
				       (unsigned long)seed, WALK_HEAL_MS / 1000, (int)conn_view(&poll.conn, world), (int)poll.flow.phase,
				       (int)dtc_flow_read_block(&poll.flow, &poll.conn, &poll.values, &poll.catalog, world),
				       (int)values_age(values_find(&poll.values, "ENGINE_RPM"), world), (int)values_age(values_find(&poll.values, "@BATT_V"), world));
				result->broken[PROMISE_STUCK]++;
			}
			result->heals++;
			if(was_under_way) result->healed_under_way++;
		}
		else
		{
			uint32_t raised = poll.events & ~events_before;
			bool shown_changed = shown_sum() != shown_before;
			bool in_list = poll.flow.phase == DTC_FLOW_LIST || poll.flow.phase == DTC_FLOW_CLEAR_SENT || poll.flow.phase == DTC_FLOW_CLEARING;
			// The clear that was sent is over. The adapter took it, or may have: it runs, nobody knows, or a state
			// (not the answer to the POST, which would be a refusal) shows that it failed.
			bool left_sent = phase_before == DTC_FLOW_CLEAR_SENT && poll.flow.phase != DTC_FLOW_CLEAR_SENT && !fresh_start;
			bool expect_old = left_sent && (poll.flow.phase == DTC_FLOW_CLEARING || poll.flow.phase == DTC_FLOW_UNKNOWN ||
			                                (poll.flow.phase == DTC_FLOW_FAILED && answered == POLL_STATE));
			int bit;

			// The promises
			if(expect_old)
			{
				if(!poll.has_old || strcmp(poll.old_text, list_text) != 0 || list_number == 0) result->broken[PROMISE_OLD]++;
				result->old_lists++;
			}
			else if(old_sum() != old_before && !fresh_start)
			{
				result->broken[PROMISE_OLD]++;
			}
			if(left_sent && !expect_old) result->clears_without_old++;
			if(poll.has_list != in_list || poll.has_cleared != (poll.flow.phase == DTC_FLOW_CLEARED)) result->broken[PROMISE_SHOWN]++;
			if(events_before == 0 && !fresh_start)
			{
				bool bound_now = bound_before[0] == '\0' && poll.bound_id[0] != '\0';

				if(((raised & POLL_EVENT_BOUND) != 0) != bound_now) result->broken[PROMISE_EVENTS]++;
				if(((raised & POLL_EVENT_OLD) != 0) != expect_old) result->broken[PROMISE_EVENTS]++;
				if(((raised & POLL_EVENT_FORGET) != 0) != expect_forget) result->broken[PROMISE_EVENTS]++;
				if((raised & POLL_EVENT_CATALOG) != 0 && !delivered) result->broken[PROMISE_EVENTS]++;
				if(shown_changed && (raised & POLL_EVENT_LISTS) == 0) result->broken[PROMISE_EVENTS]++;
				if((raised & POLL_EVENT_LISTS) != 0 && !shown_changed && !expect_forget && !expect_old) result->broken[PROMISE_EVENTS]++;
				for(bit = 0; bit < 5; bit++)
				{
					if((raised & (1u << bit)) != 0) result->raised[bit]++;
				}
			}
			if(poll.flow.to_send != DTC_FLOW_SEND_CLEAR) confirmed = false;

			if(poll.flow.phase != phase_before)
			{
				if(poll.flow.phase == DTC_FLOW_LIST && phase_before == DTC_FLOW_READING) result->lists++;
				if(poll.flow.phase == DTC_FLOW_LIST && phase_before == DTC_FLOW_CLEAR_SENT && delivered) result->back_to_list++;
				if(poll.flow.phase == DTC_FLOW_CLEARED) result->outcomes++;
				if(poll.flow.phase == DTC_FLOW_UNKNOWN) result->unknown++;
				if(poll.flow.phase == DTC_FLOW_FAILED) result->failures++;
			}
		}

		result->calls++;
		if(poll.flow.phase <= DTC_FLOW_UNKNOWN) result->phases[poll.flow.phase]++;
		result->views[conn_view(&poll.conn, now_ms)]++;
		wrong = differs(&model);
		if(wrong != NULL)
		{
			printf("  walk %lu, call %d (%s) at %llu ms: module and model differ in %s; phase %d and %d, request under way %d, kind %d and %d, events %lu and %lu\n",
			       (unsigned long)seed, call, what, (unsigned long long)now_ms, wrong, (int)poll.flow.phase, (int)model.flow.phase, (int)poll.asking, (int)poll.asked,
			       (int)model.kind, (unsigned long)poll.events, (unsigned long)model.events);
			return false;
		}
	}
	return true;
}

static void test_walk(void)
{
	walk_result_t result;
	int ends[2];
	int status = -1;
	bool complete = false;
	bool every_phase = true, every_view = true, every_kind = true, every_event = true;
	pid_t child;
	int i;

	memset(&result, 0, sizeof(result));
	fflush(stdout);
	if(pipe(ends) != 0)
	{
		check(false, "the walk has a pipe for its result");
		return;
	}

	// In a child process: a crash or a hang of the module is then a failed check here, not the end of the test
	alarm(600);
	child = fork();
	if(child == 0)
	{
		uint32_t seed;

		close(ends[0]);
		alarm(300);
		for(seed = 1; seed <= WALKS; seed++)
		{
			if(!walk(seed, &result)) result.different++;
		}
		fflush(stdout);
		_exit(write(ends[1], &result, sizeof(result)) == (ssize_t)sizeof(result) ? 0 : 1);
	}
	close(ends[1]);
	if(child > 0)
	{
		complete = read(ends[0], &result, sizeof(result)) == (ssize_t)sizeof(result);
		if(waitpid(child, &status, 0) != child) status = -1;
	}
	close(ends[0]);
	alarm(0);

	printf("  walk: %ld calls, %ld differences; promises broken: %ld one request, %ld clear, %ld old, %ld shown, %ld events, %ld stuck\n", result.calls, result.different,
	       result.broken[PROMISE_ONE_REQUEST], result.broken[PROMISE_CLEAR], result.broken[PROMISE_OLD], result.broken[PROMISE_SHOWN], result.broken[PROMISE_EVENTS],
	       result.broken[PROMISE_STUCK]);
	printf("  walk: requests");
	for(i = POLL_STATE; i <= POLL_DTC_CLEAR; i++)
	{
		printf(" %ld", result.prepared[i]);
		if(result.prepared[i] < 150) every_kind = false;
	}
	printf("; phases");
	for(i = 0; i <= DTC_FLOW_UNKNOWN; i++)
	{
		printf(" %ld", result.phases[i]);
		if(result.phases[i] < 400) every_phase = false;
	}
	printf("; views");
	for(i = 0; i <= CONN_VIEW_LIVE; i++)
	{
		printf(" %ld", result.views[i]);
		if(result.views[i] < 400) every_view = false;
	}
	printf("; events");
	for(i = 0; i < 5; i++)
	{
		printf(" %ld", result.raised[i]);
		if(result.raised[i] < 60) every_event = false;
	}
	printf("\n  walk: %ld lists, %ld outcomes of a clear, %ld unknown, %ld failures, %ld clears that did not arrive, %ld that waited too long to be handed out, "
	       "%ld clears handed out, %ld old lists made, %ld clears that ended without one; %ld answers ignored, %ld late after the network was lost, %ld steps back, "
	       "%ld starts, %ld with stored texts, %ld answers with a fault, %ld without room, %ld results at the limit, %ld catalogues started anew, "
	       "%ld of them for a restart behind a pause of the network, %ld pauses behind which the same start answered, %ld walks healed, "
	       "%ld of them with a request under way\n",
	       result.lists, result.outcomes, result.unknown, result.failures, result.back_to_list, result.late_clears, result.clears_out, result.old_lists,
	       result.clears_without_old, result.ignored, result.stale, result.steps_back, result.starts, result.stored, result.faults, result.no_room, result.at_limit,
	       result.catalogs_anew, result.unseen_restarts, result.unseen_same, result.heals, result.healed_under_way);

	check(complete && status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0, "160 random conversations of 2500 calls each: no crash and no hang");
	check(complete && result.different == 0 && result.calls == (long)WALKS * (WALK_CALLS + 1), "160 random conversations: module and model agree after every call");
	check(complete && result.broken[PROMISE_ONE_REQUEST] == 0, "in every conversation: never two requests under way");
	check(complete && result.broken[PROMISE_CLEAR] == 0 && result.clears_out > 100,
	      "in every conversation: a clear is only handed out after poll_clear() allowed it, with the number of the list shown");
	check(complete && result.broken[PROMISE_OLD] == 0 && result.old_lists > 100 && result.clears_without_old > 100,
	      "in every conversation: the old list changes exactly when the flow leaves CLEAR_SENT with the clear accepted or its outcome unknown, and is then the list that was shown");
	check(complete && result.broken[PROMISE_SHOWN] == 0, "in every conversation: has_list and has_cleared follow the flow after every call");
	check(complete && result.broken[PROMISE_EVENTS] == 0 && every_event, "in every conversation: each event is raised when its cause happens, and only then");
	check(complete && result.unseen_restarts > 100 && result.unseen_same > 100,
	      "the conversations reach adapters that restarted or were replaced behind a pause of the network, and pauses behind which the same start answered, in numbers");
	check(complete && result.broken[PROMISE_STUCK] == 0 && result.heals == WALKS && result.healed_under_way > 10,
	      "after every conversation a healthy adapter and 150 s lead back to renewed values and a read that is allowed");
	check(complete && every_kind && every_phase && every_view, "the conversations hand out every kind of request and reach every phase of the flow and every view in numbers");
	check(complete && result.lists > 300 && result.outcomes > 60 && result.unknown > 10 && result.failures > 200 && result.back_to_list > 8 && result.late_clears > 15,
	      "the conversations reach lists, outcomes of a clear, unknown outcomes, failures, clears that did not arrive and clears that came too late in numbers");
	check(complete && result.ignored > 3000 && result.stale > 100 && result.steps_back > 5000 && result.starts > 200 && result.stored > 100 && result.faults > 500 &&
	      result.no_room > 3 && result.at_limit > 3,
	      "the conversations reach ignored answers, late answers, steps back of the clock, new starts, stored texts, faulty answers, bodies without room and results at the limit in numbers");
}

int main(void)
{
	load_fixtures();
	test_constants();
	test_init();
	test_stored();
	test_wifi();
	test_requests();
	test_read();
	test_clear();
	test_post();
	test_no_answer();
	test_read_error();
	test_state();
	test_result();
	test_catalog();
	test_values();
	test_restart();
	test_restart_unseen();
	test_foreign();
	test_no_api();
	test_other_scan();
	test_outage();
	test_old();
	test_no_room();
	test_wait();
	test_slow_pass();
	test_timing();
	test_ignored();
	test_dismiss();
	test_events();
	test_late_clear();
	test_boot();
	test_guard();
	test_rooms();
	test_walk();
	return test_end();
}
