/*
 * Host test for display/components/core/dtc_flow.c. Run "make test_dtc_flow && ./test_dtc_flow" in display/test.
 * redproof.py removes or weakens every rule once (mutations/dtc_flow.py) and expects this test to fail.
 *
 * The scenes play in a small world: an adapter that answers what the scene tells it to, and the task of
 * the display that asks it once a second (tick) and passes on what arrives, as the real caller does. The
 * world of a scene starts in setup(): the display is bound to its adapter and connected, AutoPID runs, the
 * ignition is on, the engine stands, the last request the adapter saw was a read over MQTT with number 41.
 * setup() ends at 8000. The own read gets number 42 and ends at 11000, the own clear gets number 43.
 */
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#include "test.h"
#include "dtc_flow.h"

#define OWN         "a1b2c3d4e5f6"
#define OTHER       "0123456789ab"
#define READ        false
#define CLEAR       true
#define MQTT        false
#define HTTP        true
// 2^32 ms, 49.7 days after the display started: where a time counted in 32 bit begins anew
#define DAYS_49     4294967296ull
// Numbers as a real adapter has them: a boot number and a request number anywhere below 2^31
#define BIG_BOOT    1234567890u
#define BIG_SEQ     2000000041u

static conn_t conn;
static dtc_flow_t flow;
static values_t values;
static catalog_t catalog;
static json_token_t work[CATALOG_TOKENS];

static uint64_t epoch;              // the time the display started with, 0 unless a scene says otherwise
static uint64_t now;
static wican_state_t adapter;       // what GET /api/state answers
static bool result_clear;           // action of the result the adapter has stored
static const char *engine;          // ENGINE_RPM in the answer of GET /autopid_data as JSON, NULL: it is missing

// An answer of GET /autopid_data that arrives at `at`
static void values_answer(const char *rpm, uint64_t at)
{
	char json[96];

	if(rpm == NULL) snprintf(json, sizeof(json), "{\"COOLANT_TMP\":21.5}");
	else snprintf(json, sizeof(json), "{\"ENGINE_RPM\":%s,\"COOLANT_TMP\":21.5}", rpm);
	values_apply(&values, json, strlen(json), -1, at, work, VALUES_TOKENS);
}

static void load_catalog(const char *path)
{
	static char text[4096];

	catalog_init(&catalog);
	if(!read_fixture(path, text, sizeof(text)) || !catalog_apply_config(&catalog, text, strlen(text), work, CATALOG_TOKENS))
	{
		check(false, "the catalogue of the scene is loaded");
	}
}

// One second later the display asks the adapter: the state goes to the flow, a result is handed to the
// flow with the age of that state, the values arrive at the same time
static void tick(void)
{
	int requests;

	now += 1000;
	for(requests = 0; requests < 8; requests++)
	{
		conn_ask_t ask = conn_next(&conn, now);

		if(ask == CONN_ASK_STATE)
		{
			conn_got_state(&conn, CONN_GOT_OK, &adapter, now);
			dtc_flow_state(&flow, conn_state(&conn), now);
		}
		else if(ask == CONN_ASK_RESULT)
		{
			conn_got_result(&conn, CONN_GOT_OK, now);
			dtc_flow_result(&flow, adapter.dtc.result_seq, result_clear, adapter.dtc.count, adapter.dtc.age_s, now);
		}
		else if(ask == CONN_ASK_CATALOG)
		{
			conn_got_catalog(&conn, CONN_GOT_OK, now);
		}
		else if(ask == CONN_ASK_VALUES)
		{
			values_answer(engine, now);
			conn_got_values(&conn, CONN_GOT_OK, now);
		}
		else
		{
			break;
		}
	}
}

// One second later the adapter does not answer
static void silent_tick(void)
{
	now += 1000;
	while(conn_next(&conn, now) == CONN_ASK_STATE) conn_got_state(&conn, CONN_GOT_FAILED, NULL, now);
}

// The adapter stops answering: three rounds fail, the fourth second is the wait behind the second failure
static void silence(void)
{
	silent_tick();
	silent_tick();
	silent_tick();
	silent_tick();
}

// The last request the adapter accepted, as its state shows it
static void shows(wican_dtc_phase_t phase, bool clear, bool http, uint32_t seq)
{
	adapter.dtc.phase = phase;
	adapter.dtc.has_request = true;
	adapter.dtc.clear = clear;
	adapter.dtc.from_http = http;
	adapter.dtc.seq = seq;
	adapter.dtc.age_s = 0;
	adapter.dtc.reason[0] = '\0';
}

static void shows_done(bool clear, bool http, uint32_t seq, uint32_t count)
{
	shows(WICAN_DTC_DONE, clear, http, seq);
	adapter.dtc.result_seq = seq;
	adapter.dtc.count = count;
	result_clear = clear;
}

static void shows_error(bool clear, bool http, uint32_t seq, const char *reason)
{
	shows(WICAN_DTC_ERROR, clear, http, seq);
	strcpy(adapter.dtc.reason, reason);
}

// The adapter restarted 20 s ago: another boot number, nothing requested, no result
static void restarts(void)
{
	adapter.boot = 78;
	adapter.up_s = 20;
	memset(&adapter.dtc, 0, sizeof(adapter.dtc));
	adapter.dtc.supported = true;
}

// A display that is connected to nothing yet, and an adapter ready to answer
static void world(const char *bound)
{
	now = epoch + 5000;
	conn_init(&conn, bound);
	dtc_flow_init(&flow);
	values_init(&values);
	load_catalog("../../tools/w906/fixtures/car_config_w906.json");
	memset(&adapter, 0, sizeof(adapter));
	strcpy(adapter.id, OWN);
	adapter.boot = 77;
	adapter.up_s = 100;
	adapter.autopid = WICAN_AUTOPID_RUN;
	adapter.pids = 35;
	adapter.ecu_online = true;
	adapter.dtc.supported = true;
	shows_done(READ, MQTT, 41, 2);
	engine = "0";
}

static void setup(void)
{
	world(OWN);
	conn_wifi(&conn, true, now);
	tick();
	tick();
	tick();
}

static dtc_flow_block_t read_block(void)
{
	return dtc_flow_read_block(&flow, &conn, &values, &catalog, now);
}

static dtc_flow_block_t clear_block(void)
{
	return dtc_flow_clear_block(&flow, &conn, &values, &catalog, false, now);
}

static dtc_flow_block_t ask_read(void)
{
	return dtc_flow_read(&flow, &conn, &values, &catalog, now);
}

static dtc_flow_block_t ask_clear(void)
{
	return dtc_flow_clear(&flow, &conn, &values, &catalog, false, now);
}

static bool failed_with(const char *reason)
{
	return flow.phase == DTC_FLOW_FAILED && strcmp(flow.reason, reason) == 0;
}

// The scenes on the way of a read and a clear. Each returns whether it got there.
static bool at_read_waiting(void)
{
	setup();
	return ask_read() == DTC_FLOW_ALLOWED && flow.phase == DTC_FLOW_READ_SENT;
}

static bool at_read_sent(void)
{
	uint32_t seq = 99;

	return at_read_waiting() && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_READ && seq == 0;
}

// The POST of the read ended without an answer
static bool at_read_silent(void)
{
	if(!at_read_sent()) return false;
	dtc_flow_posted(&flow, 0, 0, NULL, now);
	return flow.phase == DTC_FLOW_READ_SENT;
}

static bool at_reading(void)
{
	if(!at_read_sent()) return false;
	dtc_flow_posted(&flow, 202, 42, NULL, now);
	shows(WICAN_DTC_QUEUED, READ, HTTP, 42);
	tick();
	return flow.phase == DTC_FLOW_READING;
}

// The list of the own read with `count` codes; the read ended at 11000, it is 12000 and the engine speed
// was seen after the read
static bool at_list(uint32_t count)
{
	if(!at_reading()) return false;
	shows(WICAN_DTC_RUNNING, READ, HTTP, 42);
	tick();
	shows_done(READ, HTTP, 42, count);
	tick();
	tick();
	return flow.phase == DTC_FLOW_LIST && now == epoch + 12000 && flow.list_end_ms == epoch + 11000;
}

static bool at_clear_waiting(void)
{
	return at_list(3) && ask_clear() == DTC_FLOW_ALLOWED && flow.phase == DTC_FLOW_CLEAR_SENT;
}

static bool at_clear_sent(void)
{
	uint32_t seq = 99;

	return at_clear_waiting() && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_CLEAR && seq == 42;
}

static bool at_clear_silent(void)
{
	if(!at_clear_sent()) return false;
	dtc_flow_posted(&flow, 0, 0, NULL, now);
	return flow.phase == DTC_FLOW_CLEAR_SENT;
}

static bool at_clearing(void)
{
	if(!at_clear_sent()) return false;
	dtc_flow_posted(&flow, 202, 43, NULL, now);
	shows(WICAN_DTC_QUEUED, CLEAR, HTTP, 43);
	tick();
	return flow.phase == DTC_FLOW_CLEARING;
}

static bool at_cleared(void)
{
	if(!at_clearing()) return false;
	shows(WICAN_DTC_RUNNING, CLEAR, HTTP, 43);
	tick();
	shows_done(CLEAR, HTTP, 43, 1);
	tick();
	return flow.phase == DTC_FLOW_CLEARED;
}

static bool at_failed(void)
{
	if(!at_read_sent()) return false;
	dtc_flow_posted(&flow, 409, 41, "busy", now);
	return failed_with("busy");
}

static bool at_unknown(void)
{
	if(!at_clearing()) return false;
	dtc_flow_lost(&flow);
	return flow.phase == DTC_FLOW_UNKNOWN;
}

static bool setup_idle(void)
{
	setup();
	return flow.phase == DTC_FLOW_IDLE;
}

static bool at_list_of_three(void)
{
	return at_list(3);
}

typedef bool (*scene_t)(void);

static void test_constants(void)
{
	check(DTC_FLOW_LIST_MS == 600000 && DTC_FLOW_RPM_LIMIT == 50.0 && strcmp(DTC_FLOW_RPM_NAME, "ENGINE_RPM") == 0 && DTC_FLOW_NO_ANSWER_ROUNDS == 2,
	      "a list may be cleared for 600 s, the engine stands below 50 rpm of ENGINE_RPM, two states decide about a request without an answer");
	check(DTC_FLOW_WAIT_MS == 180000, "an accepted request is given 180 s to end");
	check(VALUE_KEPT_MS == 10000 && VALUE_FRESH_MS == 3000, "the scenes below know the ages of values.h: fresh for less than 3000 ms, gone from 10000 ms on");
	check(DTC_FLOW_ALLOWED == 0 && DTC_FLOW_NO_ADAPTER == 1 && DTC_FLOW_FOREIGN == 2 && DTC_FLOW_NO_API == 3 && DTC_FLOW_AUTOPID_OFF == 4 && DTC_FLOW_STARTING == 5 &&
	      DTC_FLOW_NOT_SUPPORTED == 6 && DTC_FLOW_BUSY == 7 && DTC_FLOW_ECU_OFFLINE == 8 && DTC_FLOW_ENGINE_RUNNING == 9 && DTC_FLOW_RPM_UNKNOWN == 10 &&
	      DTC_FLOW_NO_LIST == 11 && DTC_FLOW_LIST_OLD == 12 && DTC_FLOW_NO_CODES == 13 && DTC_FLOW_BUTTON_STUCK == 14,
	      "the order of the reasons is the one the scenes below go through");
}

static void test_start(void)
{
	uint32_t seq = 99;

	// Whatever stood in the memory before: a clear that waits to be taken
	check(at_clear_waiting(), "the scene: a clear waits to be taken");
	dtc_flow_init(&flow);
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0 && flow.list_count == 0 && flow.reason[0] == '\0', "after the start nothing was read, whatever stood in the memory");
	check(dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_NOTHING && seq == 0, "after the start there is nothing to send, whatever stood in the memory");
	check(dtc_flow_seconds_left(&flow, now) == 0, "after the start no time is left to clear a list");
	check(read_block() == DTC_FLOW_ALLOWED && clear_block() == DTC_FLOW_NO_LIST, "after the start a read is offered, a clear is not: there is no list");
	check(at_failed(), "the scene: a failure with a reason");
	dtc_flow_init(&flow);
	check(flow.phase == DTC_FLOW_IDLE && flow.reason[0] == '\0', "after the start there is no failure and no reason, whatever stood in the memory");
	tick();
	dtc_flow_posted(&flow, 202, 43, NULL, now);
	dtc_flow_result(&flow, 0, READ, 3, 0, now);
	dtc_flow_result(&flow, 0, CLEAR, 3, 0, now);
	dtc_flow_no_result(&flow);
	dtc_flow_lost(&flow);
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_IDLE && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_NOTHING,
	      "states, answers, results, a result that cannot be had, a lost adapter and a dismissal change nothing while nothing was asked");
}

static void test_good_path(void)
{
	uint32_t seq = 99;

	setup();
	check(read_block() == DTC_FLOW_ALLOWED, "connected, AutoPID runs, the ignition is on, the engine stands: a read is offered");
	check(ask_read() == DTC_FLOW_ALLOWED && flow.phase == DTC_FLOW_READ_SENT, "the user asks to read: the request is ready, the phase is read sent");
	check(read_block() == DTC_FLOW_BUSY && clear_block() == DTC_FLOW_BUSY, "while the own request waits to be sent nothing else is offered");
	check(dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_READ && seq == 0, "the read is handed out, with number 0");
	seq = 99;
	check(dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_NOTHING && seq == 0 && flow.phase == DTC_FLOW_READ_SENT, "the read is handed out exactly once");
	tick();
	check(flow.phase == DTC_FLOW_READ_SENT, "a state while the POST is under way changes nothing");
	dtc_flow_posted(&flow, 202, 42, NULL, now);
	check(flow.phase == DTC_FLOW_READING && flow.seq == 42, "202: the read is accepted with its number");
	shows(WICAN_DTC_QUEUED, READ, HTTP, 42);
	tick();
	check(flow.phase == DTC_FLOW_READING && read_block() == DTC_FLOW_BUSY, "the own read is queued: reading, nothing is offered");
	shows(WICAN_DTC_RUNNING, READ, HTTP, 42);
	tick();
	tick();
	check(flow.phase == DTC_FLOW_READING, "the own read runs: reading");
	shows(WICAN_DTC_DONE, READ, HTTP, 42);
	dtc_flow_state(&flow, &adapter, now);
	check(flow.phase == DTC_FLOW_READING, "the own read is done: reading until the result is there");
	shows_done(READ, HTTP, 42, 3);
	tick();
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42 && flow.list_count == 3 && flow.list_end_ms == 13000, "the result of the own read arrives: the list, with its number, its codes and its time");
	check(dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_NOTHING, "a list hands nothing out");
	check(dtc_flow_seconds_left(&flow, now) == 600, "the list that just arrived may be cleared for 600 s");
	check(read_block() == DTC_FLOW_ALLOWED, "with a list a read is offered again");
	check(clear_block() == DTC_FLOW_RPM_UNKNOWN, "the engine speed that came with the result is not one from after the read: no clear yet");
	tick();
	check(clear_block() == DTC_FLOW_ALLOWED, "the engine speed of the next second is from after the read: the clear is offered");
	check(ask_clear() == DTC_FLOW_ALLOWED && flow.phase == DTC_FLOW_CLEAR_SENT, "the user confirms the clear: the request is ready, the phase is clear sent");
	check(read_block() == DTC_FLOW_BUSY && clear_block() == DTC_FLOW_BUSY, "while the clear waits to be sent nothing else is offered");
	check(dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_CLEAR && seq == 42, "the clear is handed out with the number of the read whose list is shown");
	seq = 99;
	check(dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_NOTHING && seq == 0 && flow.phase == DTC_FLOW_CLEAR_SENT, "the clear is handed out exactly once");
	dtc_flow_posted(&flow, 202, 43, NULL, now);
	check(flow.phase == DTC_FLOW_CLEARING && flow.seq == 43, "202: the clear is accepted with its number");
	shows(WICAN_DTC_QUEUED, CLEAR, HTTP, 43);
	tick();
	shows(WICAN_DTC_RUNNING, CLEAR, HTTP, 43);
	tick();
	check(flow.phase == DTC_FLOW_CLEARING && clear_block() == DTC_FLOW_BUSY, "the own clear runs: clearing, nothing is offered");
	shows(WICAN_DTC_DONE, CLEAR, HTTP, 43);
	dtc_flow_state(&flow, &adapter, now);
	check(flow.phase == DTC_FLOW_CLEARING, "the own clear is done: clearing until the result is there");
	shows_done(CLEAR, HTTP, 43, 1);
	tick();
	check(flow.phase == DTC_FLOW_CLEARED, "the result of the own clear arrives: cleared");
	check(dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_NOTHING && clear_block() == DTC_FLOW_NO_LIST && dtc_flow_seconds_left(&flow, now) == 0,
	      "after the clear nothing is handed out and no second clear is offered: the list was cleared");
	tick();
	tick();
	check(flow.phase == DTC_FLOW_CLEARED, "the outcome of the clear stays while the adapter shows it");
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "the user leaves the outcome: idle, no list");
}

// Every reason that stands against a read, in the order of the enum, each with the smallest situation
static void test_read_blocks(void)
{
	setup();
	check(read_block() == DTC_FLOW_ALLOWED, "the scene: a read is offered");

	world(OWN);
	check(read_block() == DTC_FLOW_NO_ADAPTER, "no WiFi: no adapter");
	conn_wifi(&conn, true, now);
	check(read_block() == DTC_FLOW_NO_ADAPTER, "connecting: no adapter");
	silence();
	now = 19999;
	check(conn_view(&conn, now) == CONN_VIEW_CONNECTING && read_block() == DTC_FLOW_NO_ADAPTER, "the adapter does not answer, within the grace time: no adapter");
	now = 20000;
	check(conn_view(&conn, now) == CONN_VIEW_NO_ANSWER && read_block() == DTC_FLOW_NO_ADAPTER, "no answer: no adapter");
	setup();
	silence();
	now = 30000;
	values_answer("0", now);
	check(conn_view(&conn, now) == CONN_VIEW_NO_ANSWER && read_block() == DTC_FLOW_NO_ADAPTER, "the adapter that answered before does not answer any more: no adapter");

	world(OTHER);
	conn_wifi(&conn, true, now);
	tick();
	tick();
	check(read_block() == DTC_FLOW_FOREIGN, "the adapter is not the one the display is bound to: foreign");

	world(OWN);
	conn_wifi(&conn, true, now);
	conn_next(&conn, now);
	conn_got_state(&conn, CONN_GOT_NOT_FOUND, NULL, now);
	check(read_block() == DTC_FLOW_NO_API, "a firmware without the API: no API");

	setup();
	adapter.autopid = WICAN_AUTOPID_OFF;
	tick();
	check(read_block() == DTC_FLOW_AUTOPID_OFF, "AutoPID is off: AutoPID off");

	setup();
	adapter.autopid = WICAN_AUTOPID_STARTING;
	tick();
	check(read_block() == DTC_FLOW_STARTING, "AutoPID is starting: starting");
	setup();
	adapter.up_s = 14;
	tick();
	check(conn_view(&conn, now) == CONN_VIEW_LIVE && read_block() == DTC_FLOW_STARTING, "the adapter is up for 14 s: commands are not allowed yet, starting");
	world(OWN);
	conn_wifi(&conn, true, now);
	tick();
	check(conn_view(&conn, now) == CONN_VIEW_LIVE && read_block() == DTC_FLOW_STARTING, "one answered round: commands are not allowed yet, starting");
	tick();
	check(read_block() == DTC_FLOW_ALLOWED, "two answered rounds: allowed");
	silent_tick();
	check(conn_view(&conn, now) == CONN_VIEW_LIVE && read_block() == DTC_FLOW_STARTING, "a failed round: commands are not allowed, starting");

	setup();
	adapter.dtc.supported = false;
	tick();
	check(read_block() == DTC_FLOW_NOT_SUPPORTED, "the profile has no fault memory table: not supported");

	setup();
	shows(WICAN_DTC_QUEUED, READ, MQTT, 42);
	tick();
	check(read_block() == DTC_FLOW_BUSY, "somebody else's scan is queued: busy");
	shows(WICAN_DTC_RUNNING, CLEAR, MQTT, 42);
	tick();
	check(read_block() == DTC_FLOW_BUSY, "somebody else's scan runs: busy");
	shows_error(CLEAR, MQTT, 42, "engine_running");
	tick();
	check(read_block() == DTC_FLOW_ALLOWED, "somebody else's scan ended: a read is offered again");
	check(at_read_waiting() && read_block() == DTC_FLOW_BUSY, "the own read waits to be taken: busy");
	check(at_read_sent() && read_block() == DTC_FLOW_BUSY, "the own read was sent: busy");
	check(at_read_silent() && read_block() == DTC_FLOW_BUSY, "the own read got no answer and is not decided yet: busy");
	check(at_reading() && read_block() == DTC_FLOW_BUSY, "the own read runs: busy");
	check(at_clear_sent() && read_block() == DTC_FLOW_BUSY, "the own clear was sent: busy");
	check(at_clearing() && read_block() == DTC_FLOW_BUSY, "the own clear runs: busy");

	setup();
	adapter.ecu_online = false;
	tick();
	check(read_block() == DTC_FLOW_ECU_OFFLINE, "the ECU is offline: ignition off");

	setup();
	engine = "780";
	tick();
	check(read_block() == DTC_FLOW_ENGINE_RUNNING, "ENGINE_RPM is 780: the engine runs");

	setup();
	engine = NULL;
	values_clear(&values);
	tick();
	check(read_block() == DTC_FLOW_RPM_UNKNOWN, "ENGINE_RPM is in the catalogue and has no value: engine speed unknown");

	check(at_list(3) && read_block() == DTC_FLOW_ALLOWED, "with a list: a read is offered");
	check(at_cleared() && read_block() == DTC_FLOW_ALLOWED, "after a clear: a read is offered");
	check(at_failed() && read_block() == DTC_FLOW_ALLOWED, "after a failure: a read is offered");
	check(at_unknown() && read_block() == DTC_FLOW_BUSY, "after a clear with unknown outcome whose scan is still queued: busy");
	shows_error(CLEAR, HTTP, 43, "engine_running");
	tick();
	check(flow.phase == DTC_FLOW_UNKNOWN && read_block() == DTC_FLOW_ALLOWED, "after a clear with unknown outcome: a read is offered when the adapter is free");
}

// One second later the display asks the adapter, but nobody tells the flow yet
static void tick_without_flow(void)
{
	int requests;

	now += 1000;
	for(requests = 0; requests < 8; requests++)
	{
		conn_ask_t ask = conn_next(&conn, now);

		if(ask == CONN_ASK_STATE) conn_got_state(&conn, CONN_GOT_OK, &adapter, now);
		else if(ask == CONN_ASK_RESULT) conn_got_result(&conn, CONN_GOT_OK, now);
		else if(ask == CONN_ASK_CATALOG) conn_got_catalog(&conn, CONN_GOT_OK, now);
		else if(ask == CONN_ASK_VALUES)
		{
			values_answer(engine, now);
			conn_got_values(&conn, CONN_GOT_OK, now);
		}
		else break;
	}
}

static bool nothing_to_take(void)
{
	uint32_t seq = 99;

	return dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_NOTHING && seq == 0;
}

// Every reason that stands against a clear, in the order of the enum, each with the smallest situation
static void test_clear_blocks(void)
{
	check(at_list(3) && clear_block() == DTC_FLOW_ALLOWED, "the scene: a list of 3 codes, the engine stands: a clear is offered");

	at_list(3);
	conn_wifi(&conn, false, now);
	check(clear_block() == DTC_FLOW_NO_ADAPTER, "clear, the WiFi is lost: no adapter");
	at_list(3);
	silence();
	now = 30000;
	values_answer("0", now);
	check(conn_view(&conn, now) == CONN_VIEW_NO_ANSWER && clear_block() == DTC_FLOW_NO_ADAPTER, "clear, the adapter does not answer any more: no adapter");

	at_list(3);
	strcpy(adapter.id, OTHER);
	tick();
	check(flow.phase == DTC_FLOW_LIST && clear_block() == DTC_FLOW_FOREIGN, "clear, another adapter answers with the same numbers: foreign");

	at_list(3);
	now += 1000;
	conn_next(&conn, now);
	conn_got_state(&conn, CONN_GOT_NOT_FOUND, NULL, now);
	check(clear_block() == DTC_FLOW_NO_API, "clear, the firmware has no API any more: no API");

	at_list(3);
	adapter.autopid = WICAN_AUTOPID_OFF;
	tick();
	check(clear_block() == DTC_FLOW_AUTOPID_OFF, "clear, AutoPID is off: AutoPID off");

	at_list(3);
	adapter.autopid = WICAN_AUTOPID_STARTING;
	tick();
	check(clear_block() == DTC_FLOW_STARTING, "clear, AutoPID is starting: starting");
	at_list(3);
	silent_tick();
	values_answer("0", now);
	check(conn_view(&conn, now) == CONN_VIEW_LIVE && clear_block() == DTC_FLOW_STARTING, "clear, a round failed: commands are not allowed, starting");

	at_list(3);
	adapter.dtc.supported = false;
	tick();
	check(clear_block() == DTC_FLOW_NOT_SUPPORTED, "clear, the profile has no fault memory table: not supported");

	at_list(3);
	shows(WICAN_DTC_QUEUED, READ, MQTT, 43);
	tick_without_flow();
	check(flow.phase == DTC_FLOW_LIST && clear_block() == DTC_FLOW_BUSY, "clear, somebody else's scan is queued: busy");
	check(at_clear_waiting() && clear_block() == DTC_FLOW_BUSY, "clear, the own clear waits to be taken: busy");
	check(at_clear_silent() && clear_block() == DTC_FLOW_BUSY, "clear, the own clear got no answer and is not decided yet: busy");
	check(at_reading() && clear_block() == DTC_FLOW_BUSY, "clear, the own read runs: busy");

	at_list(3);
	adapter.ecu_online = false;
	tick();
	values_answer("0", now);
	check(clear_block() == DTC_FLOW_ECU_OFFLINE, "clear, the ECU is offline: ignition off");

	at_list(3);
	engine = "780";
	tick();
	check(clear_block() == DTC_FLOW_ENGINE_RUNNING, "clear, ENGINE_RPM is 780: the engine runs");

	at_list(3);
	values_clear(&values);
	check(clear_block() == DTC_FLOW_RPM_UNKNOWN, "clear, ENGINE_RPM has no value: engine speed unknown");

	setup();
	check(clear_block() == DTC_FLOW_NO_LIST, "clear, nothing was read: no list");
	now = 700000;
	values_answer("0", now);
	check(clear_block() == DTC_FLOW_NO_LIST, "clear, nothing was read and 700 s have passed: no list, not an old one");
	check(at_cleared() && clear_block() == DTC_FLOW_NO_LIST, "clear after a clear: no list");
	check(at_failed() && clear_block() == DTC_FLOW_NO_LIST, "clear after a read that failed: no list");
	check(at_unknown() && (shows_error(CLEAR, HTTP, 43, "internal"), tick(), tick(), clear_block()) == DTC_FLOW_NO_LIST, "clear after a clear with unknown outcome: no list");

	at_list(3);
	values_answer("0", 611000);
	now = 611001;
	check(clear_block() == DTC_FLOW_LIST_OLD, "clear, the read ended 600001 ms ago: list old");

	check(at_list(0) && clear_block() == DTC_FLOW_NO_CODES, "clear, the list has no trouble code: no codes");
	check(at_list(1) && clear_block() == DTC_FLOW_ALLOWED && ask_clear() == DTC_FLOW_ALLOWED, "a list with one trouble code can be cleared");
	at_list(3);
	adapter.dtc.count = 0;
	tick();
	check(conn_state(&conn)->dtc.count == 0 && clear_block() == DTC_FLOW_ALLOWED, "the codes of the list count, not the number the state names: 3 in the list, 0 in the state");
	at_list(0);
	adapter.dtc.count = 5;
	tick();
	check(conn_state(&conn)->dtc.count == 5 && clear_block() == DTC_FLOW_NO_CODES, "the codes of the list count, not the number the state names: 0 in the list, 5 in the state");

	at_list(3);
	check(dtc_flow_clear_block(&flow, &conn, &values, &catalog, true, now) == DTC_FLOW_BUTTON_STUCK, "clear, the switch of the knob hangs: button stuck");
	check(dtc_flow_clear(&flow, &conn, &values, &catalog, true, now) == DTC_FLOW_BUTTON_STUCK && flow.phase == DTC_FLOW_LIST && nothing_to_take(),
	      "a clear confirmed with a hanging switch is not sent, the list stays");
	check(dtc_flow_read_block(&flow, &conn, &values, &catalog, now) == DTC_FLOW_ALLOWED, "a hanging switch does not stand against a read");
}

// Of two reasons that apply the one that comes first in the enum is reported. The scene begins with
// everything wrong and mends one thing after the other.
static void test_block_order(void)
{
	dtc_flow_block_t expected;

	world(OWN);
	conn_wifi(&conn, true, now);
	strcpy(adapter.id, OTHER);
	adapter.autopid = WICAN_AUTOPID_OFF;
	adapter.up_s = 14;
	adapter.dtc.supported = false;
	adapter.ecu_online = false;
	shows(WICAN_DTC_RUNNING, READ, MQTT, 42);
	tick();
	tick();
	silence();
	now = 40000;
	values_answer("780", now);

#define BOTH_ARE(reason) (expected = (reason), read_block() == expected && dtc_flow_clear_block(&flow, &conn, &values, &catalog, true, now) == expected)
	check(BOTH_ARE(DTC_FLOW_NO_ADAPTER), "no answer from a foreign adapter with everything else wrong: no adapter goes first");
	tick();
	values_answer("780", now);
	check(BOTH_ARE(DTC_FLOW_FOREIGN), "the foreign adapter answers again: foreign goes before AutoPID, profile, scan, ignition and engine");
	strcpy(adapter.id, OWN);
	tick();
	values_answer("780", now);
	check(BOTH_ARE(DTC_FLOW_AUTOPID_OFF), "the own adapter with AutoPID off: goes before starting, profile, scan, ignition and engine");
	adapter.autopid = WICAN_AUTOPID_STARTING;
	tick();
	values_answer("780", now);
	check(BOTH_ARE(DTC_FLOW_STARTING), "AutoPID starting: goes before profile, scan, ignition and engine");
	adapter.autopid = WICAN_AUTOPID_RUN;
	tick();
	values_answer("780", now);
	check(conn_view(&conn, now) == CONN_VIEW_SCAN && BOTH_ARE(DTC_FLOW_STARTING), "AutoPID runs, the adapter is up for 14 s: starting goes before profile, scan, ignition and engine");
	adapter.up_s = 100;
	tick();
	values_answer("780", now);
	check(BOTH_ARE(DTC_FLOW_NOT_SUPPORTED), "commands are allowed, the profile has no fault memory: goes before scan, ignition and engine");
	adapter.dtc.supported = true;
	tick();
	values_answer("780", now);
	check(BOTH_ARE(DTC_FLOW_BUSY), "a scan runs with the ignition off: busy goes before ignition and engine");
	shows_error(READ, MQTT, 42, "ecu_offline");
	tick();
	values_answer("780", now);
	check(BOTH_ARE(DTC_FLOW_ECU_OFFLINE), "the ignition is off and a fresh engine speed says running: ignition goes before engine");
	adapter.ecu_online = true;
	engine = "780";
	tick();
	check(BOTH_ARE(DTC_FLOW_ENGINE_RUNNING), "the engine runs: goes before the list and the switch");
	engine = "\"on\"";
	tick();
	check(BOTH_ARE(DTC_FLOW_RPM_UNKNOWN), "the engine speed is no number: goes before the list and the switch");
	engine = "0";
	tick();
	check(read_block() == DTC_FLOW_ALLOWED && dtc_flow_clear_block(&flow, &conn, &values, &catalog, true, now) == DTC_FLOW_NO_LIST,
	      "nothing stands against a read; a clear has no list: goes before the switch");
#undef BOTH_ARE

	// A firmware without the API and a fresh engine speed that says running
	world(OWN);
	conn_wifi(&conn, true, now);
	conn_next(&conn, now);
	conn_got_state(&conn, CONN_GOT_NOT_FOUND, NULL, now);
	values_answer("780", now);
	check(read_block() == DTC_FLOW_NO_API && dtc_flow_clear_block(&flow, &conn, &values, &catalog, true, now) == DTC_FLOW_NO_API, "no API goes before the engine and the switch");

	// The own request under way while commands are not allowed
	at_reading();
	adapter.up_s = 14;
	tick();
	check(read_block() == DTC_FLOW_STARTING, "the own request runs and commands are not allowed: starting goes before busy");
	at_reading();
	adapter.ecu_online = false;
	tick();
	check(read_block() == DTC_FLOW_BUSY && clear_block() == DTC_FLOW_BUSY, "the own request runs with the ignition off: busy goes before ignition");
	at_read_sent();
	adapter.ecu_online = false;
	tick();
	check(conn_view(&conn, now) == CONN_VIEW_ECU_OFFLINE && read_block() == DTC_FLOW_BUSY && clear_block() == DTC_FLOW_BUSY,
	      "the own request was sent, the adapter does not show it yet and the ignition is off: busy goes before ignition");

	// With a list: old, without codes, the switch hangs, the ignition off, the engine speed missing
	at_list(0);
	now = 700000;
	adapter.ecu_online = false;
	tick();
	values_answer("780", now);
	check(dtc_flow_clear_block(&flow, &conn, &values, &catalog, true, now) == DTC_FLOW_ECU_OFFLINE, "an old list without codes, switch, engine and ignition wrong: the ignition goes first");
	adapter.ecu_online = true;
	engine = "780";
	tick();
	check(dtc_flow_clear_block(&flow, &conn, &values, &catalog, true, now) == DTC_FLOW_ENGINE_RUNNING, "then the running engine");
	engine = "\"off\"";
	tick();
	check(dtc_flow_clear_block(&flow, &conn, &values, &catalog, true, now) == DTC_FLOW_RPM_UNKNOWN, "then the unknown engine speed");
	engine = "0";
	tick();
	check(dtc_flow_clear_block(&flow, &conn, &values, &catalog, true, now) == DTC_FLOW_LIST_OLD, "then the old list, before the missing codes and the switch");
	check(at_list(0) && dtc_flow_clear_block(&flow, &conn, &values, &catalog, true, now) == DTC_FLOW_NO_CODES, "a young list without codes and a hanging switch: no codes goes first");

	// A running engine seen before the read ended is a running engine, not an unknown speed
	at_list(3);
	values_clear(&values);
	values_answer("780", 10500);
	check(clear_block() == DTC_FLOW_ENGINE_RUNNING, "a fresh engine speed from before the end of the read that says running: the engine runs goes before unknown");
}

static void test_engine(void)
{
	static char text[512];
	value_t *rpm;

	setup();
	check(values_find(&values, "ENGINE_RPM") != NULL && values_find(&values, "ENGINE_RPM")->seen_ms == 8000 && now == 8000, "the scene: ENGINE_RPM 0 was seen at 8000");
	now = 10999;
	check(read_block() == DTC_FLOW_ALLOWED, "an engine speed seen 2999 ms ago is fresh: allowed");
	now = 11000;
	check(values_age(values_find(&values, "ENGINE_RPM"), now) == VALUE_AGE_OLD && read_block() == DTC_FLOW_ALLOWED,
	      "an engine speed seen 3000 ms ago is not fresh any more and still counts: allowed");
	now = 17999;
	check(read_block() == DTC_FLOW_ALLOWED, "an engine speed seen 9999 ms ago is not gone: allowed");
	now = 18000;
	check(values_age(values_find(&values, "ENGINE_RPM"), now) == VALUE_AGE_GONE && read_block() == DTC_FLOW_RPM_UNKNOWN, "an engine speed seen 10000 ms ago is gone: unknown");
	now = 18001;
	check(read_block() == DTC_FLOW_RPM_UNKNOWN, "an engine speed seen 10001 ms ago: unknown");
	now = 7000;
	check(read_block() == DTC_FLOW_ALLOWED, "a time before the engine speed was seen: no time passed, the value counts");

	values_answer("0", 0);
	now = 9999;
	check(read_block() == DTC_FLOW_ALLOWED, "an engine speed seen at time 0 is good for a read: it has to be newer than nothing");

	now = 8000;
	values_answer("49.99", now);
	check(read_block() == DTC_FLOW_ALLOWED, "49.99 rpm: the engine stands");
	values_answer("50", now);
	check(read_block() == DTC_FLOW_ENGINE_RUNNING, "50 rpm: the engine runs");
	values_answer("50.01", now);
	check(read_block() == DTC_FLOW_ENGINE_RUNNING, "50.01 rpm: the engine runs");
	values_answer("49", now);
	check(read_block() == DTC_FLOW_ALLOWED, "49 rpm: the engine stands");
	values_answer("1e999", now);
	check(read_block() == DTC_FLOW_ENGINE_RUNNING, "a speed beyond every number: the engine runs");
	values_answer("\"on\"", now);
	check(read_block() == DTC_FLOW_RPM_UNKNOWN, "ENGINE_RPM \"on\" is no speed: unknown");
	values_answer("\"off\"", now);
	check(read_block() == DTC_FLOW_RPM_UNKNOWN, "ENGINE_RPM \"off\" is no speed either, and no engine that stands: unknown");
	values_answer("0", now);
	rpm = (value_t *)values_find(&values, "ENGINE_RPM");
	rpm->number = NAN;
	check(read_block() == DTC_FLOW_ENGINE_RUNNING, "a speed that is no number at all does not pass for an engine that stands");

	// A running engine that is no news any more
	values_answer("780", 8000);
	now = 11000;
	check(read_block() == DTC_FLOW_ENGINE_RUNNING, "780 rpm seen 3000 ms ago: the engine runs, an old value says so as a fresh one does");
	now = 17999;
	check(read_block() == DTC_FLOW_ENGINE_RUNNING, "780 rpm seen 9999 ms ago: the engine runs");
	now = 18000;
	check(read_block() == DTC_FLOW_RPM_UNKNOWN, "780 rpm seen 10000 ms ago: not the engine runs, but unknown");
	values_answer("\"on\"", 8000);
	now = 11000;
	check(read_block() == DTC_FLOW_RPM_UNKNOWN, "ENGINE_RPM \"on\" seen 3000 ms ago is no speed either: unknown");

	// The values of an idling engine as the adapter sends them
	setup();
	check(read_fixture("fixtures/dtc_flow_values_idling.json", text, sizeof(text)) &&
	      values_apply(&values, text, strlen(text), -1, now, work, VALUES_TOKENS) == VALUES_RENEWED && read_block() == DTC_FLOW_ENGINE_RUNNING,
	      "the values of an idling engine: the engine runs");

	// A profile without ENGINE_RPM
	setup();
	load_catalog("fixtures/dtc_flow_config_without_rpm.json");
	values_clear(&values);
	check(read_block() == DTC_FLOW_ALLOWED, "ENGINE_RPM is not in the catalogue and has no value: the adapter decides, a read is offered");
	values_answer("780", now);
	check(read_block() == DTC_FLOW_ALLOWED, "ENGINE_RPM is not in the catalogue: its value does not count");
	catalog_note_values(&catalog, &values);
	check(read_block() == DTC_FLOW_ENGINE_RUNNING, "ENGINE_RPM came with the values and so into the catalogue: it counts");
	at_list(3);
	load_catalog("fixtures/dtc_flow_config_without_rpm.json");
	values_clear(&values);
	check(clear_block() == DTC_FLOW_ALLOWED, "ENGINE_RPM is not in the catalogue: a clear is offered without an engine speed");

	// For a clear: a value from after the read
	check(at_list(3) && flow.list_end_ms == 11000 && values_find(&values, "ENGINE_RPM")->seen_ms == 12000 && clear_block() == DTC_FLOW_ALLOWED,
	      "the scene: the read ended at 11000, the engine speed was seen at 12000, the clear is offered");
	values_answer("0", 11000);
	check(clear_block() == DTC_FLOW_RPM_UNKNOWN, "an engine speed seen at the very time the read ended is not from after it: unknown");
	check(read_block() == DTC_FLOW_ALLOWED, "for a read the same engine speed is good");
	values_answer("0", 11001);
	check(clear_block() == DTC_FLOW_ALLOWED, "an engine speed seen 1 ms after the read ended: the clear is offered");
	values_answer("0", 10999);
	check(clear_block() == DTC_FLOW_RPM_UNKNOWN, "a fresh engine speed seen 1 ms before the read ended: unknown");
	now = 14001;
	values_answer("0", 11001);
	check(clear_block() == DTC_FLOW_ALLOWED, "the engine speed from after the read, seen 3000 ms ago: not fresh any more, the clear is offered all the same");
	now = 21000;
	check(clear_block() == DTC_FLOW_ALLOWED, "the engine speed from after the read, seen 9999 ms ago: the clear is offered");
	now = 21001;
	check(clear_block() == DTC_FLOW_RPM_UNKNOWN, "the engine speed from after the read, seen 10000 ms ago: gone, unknown");
	values_answer("0", 10999);
	now = 14500;
	check(clear_block() == DTC_FLOW_RPM_UNKNOWN && read_block() == DTC_FLOW_ALLOWED, "an old engine speed from before the end of the read: unknown for a clear, good for a read");
	values_answer("780", 10999);
	now = 14000;
	check(clear_block() == DTC_FLOW_ENGINE_RUNNING, "an old engine speed from before the end of the read that says running: the engine runs");

	// Without a list the engine is judged as for a read
	at_clear_sent();
	dtc_flow_posted(&flow, 409, 42, "busy", now);
	values_answer("0", 11000);
	check(failed_with("busy") && flow.list_end_ms == 11000 && clear_block() == DTC_FLOW_NO_LIST,
	      "after a clear that failed the list is none: an engine speed from the end of its read is good, the missing list is the reason");
	setup();
	check(clear_block() == DTC_FLOW_NO_LIST, "a clear without a list and a fresh engine speed: the missing list is the reason, not the engine speed");
}

static void test_list_time(void)
{
	uint32_t left[10];

	check(at_list(3), "the scene: a list whose read ended at 11000");
	left[0] = dtc_flow_seconds_left(&flow, 11000);
	left[1] = dtc_flow_seconds_left(&flow, 11001);
	left[2] = dtc_flow_seconds_left(&flow, 11999);
	left[3] = dtc_flow_seconds_left(&flow, 12000);
	left[4] = dtc_flow_seconds_left(&flow, 12001);
	check(left[0] == 600 && left[1] == 600 && left[2] == 600 && left[3] == 599 && left[4] == 599, "600 s are left when the read ends, 599 when 1000 ms have passed: rounded up");
	left[5] = dtc_flow_seconds_left(&flow, 610000);
	left[6] = dtc_flow_seconds_left(&flow, 610001);
	left[7] = dtc_flow_seconds_left(&flow, 610999);
	check(left[5] == 1 && left[6] == 1 && left[7] == 1, "1 s is left during the last second");
	check(dtc_flow_seconds_left(&flow, 611000) == 0 && dtc_flow_seconds_left(&flow, 611001) == 0 && dtc_flow_seconds_left(&flow, UINT64_MAX) == 0,
	      "0 s are left when 600000 ms have passed, and ever after");
	check(dtc_flow_seconds_left(&flow, 10000) == 600 && dtc_flow_seconds_left(&flow, 0) == 600, "a time before the end of the read: no time passed, 600 s are left");
	check(at_list(0) && dtc_flow_seconds_left(&flow, 12000) == 599, "the time of a list without trouble codes runs as well");
	at_list(3);

	values_answer("0", 610999);
	now = 611000;
	check(clear_block() == DTC_FLOW_ALLOWED, "600000 ms after the read ended the clear is still offered");
	now = 611001;
	check(clear_block() == DTC_FLOW_LIST_OLD, "600001 ms after the read ended the clear is not offered any more");
	check(ask_clear() == DTC_FLOW_LIST_OLD && flow.phase == DTC_FLOW_LIST && nothing_to_take(), "a clear confirmed for an old list is not sent, the list stays to be looked at");
	now = 611000;
	check(ask_clear() == DTC_FLOW_ALLOWED && flow.phase == DTC_FLOW_CLEAR_SENT, "a clear confirmed 600000 ms after the read ended is sent");

	at_list(3);
	values_answer("0", 900000);
	now = 5000;
	check(clear_block() == DTC_FLOW_ALLOWED, "a time before the end of the read: no time passed, the list is not old");

	// The age the adapter gives for the end of the read
	check(at_reading(), "the scene: the own read runs");
	shows_done(READ, HTTP, 42, 3);
	adapter.dtc.age_s = 5;
	tick();
	check(flow.phase == DTC_FLOW_LIST && now == 10000 && flow.list_end_ms == 5000 && dtc_flow_seconds_left(&flow, now) == 595,
	      "a result that arrives with an age of 5 s: the read ended 5 s ago, 595 s are left");
	at_reading();
	dtc_flow_result(&flow, 42, READ, 3, 9, now);
	check(flow.list_end_ms == 0 && dtc_flow_seconds_left(&flow, now) == 591, "an age of exactly the time of the display: the read ended at 0");
	at_reading();
	dtc_flow_result(&flow, 42, READ, 3, 10, now);
	check(flow.phase == DTC_FLOW_LIST && flow.list_end_ms == 0 && dtc_flow_seconds_left(&flow, now) == 591, "an age of more than the time of the display: the read ended at 0, not before");
	at_reading();
	dtc_flow_result(&flow, 42, READ, 3, 4294967295u, now);
	check(flow.phase == DTC_FLOW_LIST && flow.list_end_ms == 0, "the largest age: the read ended at 0");
	at_reading();
	dtc_flow_result(&flow, 42, READ, 3, 65540, 70000000);
	check(flow.phase == DTC_FLOW_LIST && flow.list_end_ms == 4460000, "an age of 65540 s at 70000 s: the read ended at 4460 s");
	at_reading();
	dtc_flow_result(&flow, 42, READ, 3, 4294968, now);
	check(flow.phase == DTC_FLOW_LIST && flow.list_end_ms == 0, "an age of 4294968 s, more milliseconds than 32 bit hold: the read ended at 0");

	at_reading();
	now = 900000;
	values_answer("0", now);
	dtc_flow_result(&flow, 42, READ, 3, 600, now);
	check(flow.list_end_ms == 300000 && dtc_flow_seconds_left(&flow, now) == 0 && clear_block() == DTC_FLOW_BUSY, "the scene: a result that arrives 600 s old while the adapter still shows the read as queued");
	shows_done(READ, HTTP, 42, 3);
	tick_without_flow();
	now = 900000;
	check(clear_block() == DTC_FLOW_ALLOWED, "a list that arrived 600 s old may be cleared at that moment");
	now = 900001;
	check(clear_block() == DTC_FLOW_LIST_OLD, "a list that arrived 600 s old is old 1 ms later");
}

static void test_posted(void)
{
	static const struct
	{
		int status;
		const char *reason;
	} refusals[] = {{409, "busy"}, {409, "read_required"}, {409, "stale_seq"}, {409, "nothing_to_clear"}, {503, "not_ready"}, {403, "forbidden"}, {400, "bad_request"}};
	char what[160];
	size_t i;

	for(i = 0; i < sizeof(refusals) / sizeof(refusals[0]); i++)
	{
		at_read_sent();
		dtc_flow_posted(&flow, refusals[i].status, 41, refusals[i].reason, now);
		snprintf(what, sizeof(what), "a read answered with %d %s: failed with that reason, there is no list", refusals[i].status, refusals[i].reason);
		check(failed_with(refusals[i].reason) && flow.read_seq == 0 && flow.list_count == 0 && nothing_to_take(), what);

		at_clear_sent();
		dtc_flow_posted(&flow, refusals[i].status, 42, refusals[i].reason, now);
		snprintf(what, sizeof(what), "a clear answered with %d %s: failed with that reason, the list stays", refusals[i].status, refusals[i].reason);
		check(failed_with(refusals[i].reason) && flow.read_seq == 42 && flow.list_count == 3 && flow.list_end_ms == 11000 && nothing_to_take(), what);
		snprintf(what, sizeof(what), "after a clear answered with %d %s the list cannot be cleared: the user reads again", refusals[i].status, refusals[i].reason);
		check(clear_block() == DTC_FLOW_NO_LIST && ask_clear() == DTC_FLOW_NO_LIST && failed_with(refusals[i].reason) && nothing_to_take() &&
		      dtc_flow_seconds_left(&flow, now) == 0 && read_block() == DTC_FLOW_ALLOWED, what);
	}

	at_read_sent();
	dtc_flow_posted(&flow, 500, 0, NULL, now);
	check(failed_with(""), "a status without a reason: failed with an empty reason");
	at_failed();
	check(ask_read() == DTC_FLOW_ALLOWED && dtc_flow_take(&flow, NULL, now) == DTC_FLOW_SEND_READ, "the scene: a read after one that failed with a reason");
	dtc_flow_posted(&flow, 500, 0, NULL, now);
	check(failed_with(""), "a failure without a reason does not keep the reason of the failure before");
	at_read_sent();
	dtc_flow_posted(&flow, 200, 42, "", now);
	check(failed_with(""), "200 is not 202: failed");
	at_read_sent();
	dtc_flow_posted(&flow, 404, 0, "x", now);
	check(failed_with("x"), "404: failed");
	at_clear_sent();
	dtc_flow_posted(&flow, -1, 0, "minus", now);
	check(failed_with("minus"), "a status below 0 is another status: failed");
	at_read_sent();
	dtc_flow_posted(&flow, 202 + 256, 42, "odd", now);
	check(failed_with("odd"), "a status of 202 + 256 is another status: failed");
	at_read_sent();
	dtc_flow_posted(&flow, 409, 0, "0123456789012345678901234567890123456789", now);
	check(failed_with("0123456789012345678901234567890"), "a reason of 40 bytes is cut to the 31 that fit");
	at_read_sent();
	dtc_flow_posted(&flow, 409, 0, "0123456789012345678901234567890", now);
	check(failed_with("0123456789012345678901234567890"), "a reason of 31 bytes fits");
	at_read_sent();
	dtc_flow_posted(&flow, 409, 0, " zu hei\xC3\x9F, bitte warten", now);
	check(failed_with(" zu hei\xC3\x9F, bitte warten"), "a reason with spaces and bytes above 127 is kept byte for byte");
	at_reading();
	shows_error(READ, HTTP, 42, "Z\xC3\xBCndung aus");
	tick();
	check(failed_with("Z\xC3\xBCndung aus"), "a reason of the state with a space and bytes above 127 is kept byte for byte");

	// 202
	at_read_sent();
	dtc_flow_posted(&flow, 202, 42, "busy", now);
	check(flow.phase == DTC_FLOW_READING && flow.seq == 42, "202 for a read: reading, whatever reason comes with it");
	at_clear_sent();
	dtc_flow_posted(&flow, 202, 43, NULL, now);
	check(flow.phase == DTC_FLOW_CLEARING && flow.seq == 43 && flow.read_seq == 42, "202 for a clear: clearing");
	at_read_sent();
	dtc_flow_posted(&flow, 202, 2147483647u, NULL, now);
	check(flow.phase == DTC_FLOW_READING && flow.seq == 2147483647u, "202 with the largest number");
	at_read_sent();
	dtc_flow_posted(&flow, 202, 0, NULL, now);
	check(flow.phase == DTC_FLOW_READ_SENT, "202 without a number: not accepted with number 0");
	tick();
	tick();
	check(failed_with("no_answer"), "202 without a number is as good as no answer: two states with the number of before end it");
	at_read_sent();
	dtc_flow_posted(&flow, 202, 0, NULL, now);
	shows(WICAN_DTC_QUEUED, READ, HTTP, 42);
	tick();
	check(flow.phase == DTC_FLOW_READING && flow.seq == 42, "202 without a number: the state tells the number");

	// Calls that belong to no request
	at_read_waiting();
	dtc_flow_posted(&flow, 202, 42, NULL, now);
	dtc_flow_posted(&flow, 409, 41, "busy", now);
	dtc_flow_posted(&flow, 0, 0, NULL, now);
	check(flow.phase == DTC_FLOW_READ_SENT && flow.seq == 0 && !flow.posted, "an answer before the request was taken is ignored");
	tick();
	tick();
	tick();
	check(flow.phase == DTC_FLOW_READ_SENT, "the ignored answers do not make the states count");
	check(dtc_flow_take(&flow, NULL, now) == DTC_FLOW_SEND_READ, "the request is still handed out after the ignored answers");
	at_read_sent();
	dtc_flow_posted(&flow, 409, 41, "busy", now);
	dtc_flow_posted(&flow, 202, 42, NULL, now);
	dtc_flow_posted(&flow, 409, 41, "stale_seq", now);
	check(failed_with("busy"), "a request has one answer: a second one after a refusal is ignored");
	at_read_sent();
	dtc_flow_posted(&flow, 202, 42, NULL, now);
	dtc_flow_posted(&flow, 202, 44, NULL, now);
	dtc_flow_posted(&flow, 409, 41, "busy", now);
	dtc_flow_posted(&flow, 0, 0, NULL, now);
	check(flow.phase == DTC_FLOW_READING && flow.seq == 42, "a second answer after a 202 is ignored");
	at_read_silent();
	dtc_flow_posted(&flow, 202, 44, NULL, now);
	dtc_flow_posted(&flow, 409, 41, "busy", now);
	check(flow.phase == DTC_FLOW_READ_SENT && flow.seq == 0, "an answer after the request was given up is ignored");
	at_list(3);
	dtc_flow_posted(&flow, 202, 44, NULL, now);
	dtc_flow_posted(&flow, 409, 41, "busy", now);
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42, "an answer while a list is shown is ignored");
	at_cleared();
	dtc_flow_posted(&flow, 409, 41, "busy", now);
	check(flow.phase == DTC_FLOW_CLEARED, "an answer after the clear is ignored");
	setup();
	dtc_flow_posted(&flow, 202, 44, NULL, now);
	dtc_flow_posted(&flow, 409, 41, "busy", now);
	check(flow.phase == DTC_FLOW_IDLE, "an answer while nothing was asked is ignored");
}

static void test_no_answer(void)
{
	wican_state_t odd;

	// A read
	check(at_read_silent() && nothing_to_take(), "the scene: the POST of the read ended without an answer; it is not handed out again");
	shows(WICAN_DTC_QUEUED, READ, HTTP, 42);
	tick();
	check(flow.phase == DTC_FLOW_READING && flow.seq == 42, "the state shows another number, from HTTP, a read: the request did arrive, reading with that number");
	shows_done(READ, HTTP, 42, 3);
	tick();
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42 && flow.list_count == 3, "the read without an answer ends with its list");

	at_read_silent();
	tick();
	check(flow.phase == DTC_FLOW_READ_SENT && read_block() == DTC_FLOW_BUSY, "one state with the number of before: not decided yet");
	tick();
	check(failed_with("no_answer") && nothing_to_take(), "two states with the number of before: the read did not arrive, failed with no_answer");
	check(flow.read_seq == 0 && read_block() == DTC_FLOW_ALLOWED, "after a read that did not arrive there is no list, a read is offered again");

	at_read_silent();
	tick();
	shows(WICAN_DTC_QUEUED, READ, HTTP, 42);
	tick();
	check(flow.phase == DTC_FLOW_READING && flow.seq == 42, "the read shows up with the second state: it did arrive");

	at_read_sent();
	tick();
	tick();
	tick();
	check(flow.phase == DTC_FLOW_READ_SENT, "states while the POST is under way decide nothing, however many");
	dtc_flow_posted(&flow, 0, 0, NULL, now);
	tick();
	check(flow.phase == DTC_FLOW_READ_SENT, "the states before the POST ended do not count: one state after it decides nothing");
	tick();
	check(failed_with("no_answer"), "the second state after the POST ended decides");

	at_read_silent();
	shows(WICAN_DTC_QUEUED, READ, MQTT, 42);
	tick();
	check(failed_with("superseded"), "another number, but a read over MQTT: not the own request, failed with superseded");
	at_read_silent();
	shows(WICAN_DTC_QUEUED, CLEAR, HTTP, 42);
	tick();
	check(failed_with("superseded"), "another number from HTTP, but a clear: not the read that was sent, failed with superseded");
	at_read_silent();
	odd = adapter;
	odd.dtc.seq = 42;
	odd.dtc.phase = WICAN_DTC_QUEUED;
	odd.dtc.has_request = false;
	odd.dtc.from_http = true;
	odd.dtc.clear = false;
	dtc_flow_state(&flow, &odd, now);
	check(failed_with("superseded"), "another number from HTTP without an action: not the read that was sent");
	at_read_silent();
	odd.dtc.seq = 0;
	odd.dtc.has_request = true;
	dtc_flow_state(&flow, &odd, now);
	check(failed_with("superseded"), "number 0 is no number of a request: not the read that was sent");

	at_read_silent();
	shows_error(READ, HTTP, 42, "expired");
	tick();
	check(failed_with("expired"), "the read without an answer shows up as an error: failed with the reason of the state");
	at_read_silent();
	shows_done(READ, HTTP, 42, 3);
	tick();
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42, "the read without an answer shows up as done: its result makes the list");

	// A clear
	check(at_clear_silent() && nothing_to_take(), "the scene: the POST of the clear ended without an answer; it is not handed out again");
	shows(WICAN_DTC_QUEUED, CLEAR, HTTP, 43);
	tick();
	check(flow.phase == DTC_FLOW_CLEARING && flow.seq == 43, "the state shows another number, from HTTP, a clear: the request did arrive, clearing with that number");
	shows_done(CLEAR, HTTP, 43, 0);
	tick();
	check(flow.phase == DTC_FLOW_CLEARED, "the clear without an answer ends as cleared");

	at_clear_silent();
	tick();
	check(flow.phase == DTC_FLOW_CLEAR_SENT, "clear: one state with the number of before decides nothing");
	tick();
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42 && flow.list_count == 3 && flow.list_end_ms == 11000, "clear: two states with the number of before: it did not arrive, back to the list");
	check(nothing_to_take(), "back at the list the clear is not handed out by itself");
	check(clear_block() == DTC_FLOW_ALLOWED && ask_clear() == DTC_FLOW_ALLOWED && dtc_flow_take(&flow, NULL, now) == DTC_FLOW_SEND_CLEAR, "back at the list the user may confirm again: the clear is handed out once more");

	// The states of the request before do not count for the next one
	at_read_silent();
	tick();
	shows(WICAN_DTC_QUEUED, READ, HTTP, 42);
	tick();
	shows_done(READ, HTTP, 42, 3);
	tick();
	tick();
	check(flow.phase == DTC_FLOW_LIST && ask_clear() == DTC_FLOW_ALLOWED && dtc_flow_take(&flow, NULL, now) == DTC_FLOW_SEND_CLEAR,
	      "the scene: a clear after a read that was found with the second state after its POST");
	dtc_flow_posted(&flow, 0, 0, NULL, now);
	tick();
	check(flow.phase == DTC_FLOW_CLEAR_SENT, "the state that counted for the read does not count for the clear: one state with the number of before decides nothing");
	tick();
	check(flow.phase == DTC_FLOW_LIST, "the second state after the POST of that clear decides");

	at_clear_silent();
	shows(WICAN_DTC_QUEUED, CLEAR, MQTT, 43);
	tick();
	check(flow.phase == DTC_FLOW_UNKNOWN, "clear: another number, but over MQTT: what became of the own clear is not known");
	at_clear_silent();
	shows(WICAN_DTC_QUEUED, READ, HTTP, 43);
	tick();
	check(flow.phase == DTC_FLOW_UNKNOWN, "clear: another number from HTTP, but a read: unknown");
	at_clear_silent();
	shows_error(CLEAR, HTTP, 43, "engine_running");
	tick();
	check(failed_with("engine_running") && flow.read_seq == 42, "the clear without an answer shows up as an error: failed with the reason of the state, the list stays");
}

static void test_own_scan(void)
{
	// The own number as an error
	at_reading();
	shows_error(READ, HTTP, 42, "ecu_offline");
	tick();
	check(failed_with("ecu_offline") && flow.read_seq == 0, "the own read ends with an error: failed with the reason of the state, no list");
	at_clearing();
	shows_error(CLEAR, HTTP, 43, "engine_running");
	tick();
	check(failed_with("engine_running") && flow.read_seq == 42 && flow.list_count == 3, "the own clear ends with an error: failed with the reason of the state, the list stays");
	check(clear_block() == DTC_FLOW_NO_LIST, "after a clear that ended with an error the list cannot be cleared: the user reads again");
	at_reading();
	shows_error(READ, MQTT, 41, "ecu_offline");
	adapter.dtc.result_seq = 42;
	dtc_flow_state(&flow, &adapter, now);
	check(flow.phase == DTC_FLOW_READING, "an error of another number is not the error of the own read");
	at_reading();
	shows_error(READ, HTTP, 42, "");
	tick();
	check(failed_with(""), "the own read ends with an error without a reason: failed with an empty reason");
	at_reading();
	shows_error(CLEAR, MQTT, 42, "internal");
	tick();
	check(failed_with("internal"), "the own number as an error ends the own request, whatever action and source the state names with it");
	at_reading();
	shows_error(READ, HTTP, 42, "internal");
	adapter.dtc.has_request = false;
	dtc_flow_state(&flow, &adapter, now);
	check(failed_with("internal"), "the own number as an error ends the own request, also when the state names no action with it");
	at_reading();
	shows(WICAN_DTC_QUEUED, READ, HTTP, 43);
	tick();
	check(failed_with("superseded"), "a later request over HTTP supersedes the own read as one over MQTT does");
	at_reading();
	adapter.dtc.result_seq = 0;
	adapter.dtc.count = 0;
	shows(WICAN_DTC_QUEUED, READ, MQTT, 43);
	tick();
	check(failed_with("superseded"), "a later request while the adapter has no result stored: superseded");

	// A later number
	at_reading();
	shows_done(READ, HTTP, 42, 3);
	shows(WICAN_DTC_QUEUED, READ, MQTT, 43);
	dtc_flow_state(&flow, &adapter, now);
	check(flow.phase == DTC_FLOW_READING, "the state shows a later number and the result of the own read is still stored: wait for it");
	tick();
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42 && flow.list_count == 3 && clear_block() == DTC_FLOW_BUSY, "the result of the own read arrives although a later request runs: the list");
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "the next state shows the later number: the list is dropped");

	at_reading();
	shows(WICAN_DTC_QUEUED, READ, MQTT, 43);
	tick();
	check(failed_with("superseded"), "the state shows a later number and the stored result is not the one of the own read: failed with superseded");
	at_reading();
	shows_error(READ, MQTT, 43, "ecu_offline");
	tick();
	check(failed_with("superseded"), "a later number that ended with an error is not the error of the own read: superseded");
	at_reading();
	shows_done(READ, MQTT, 43, 5);
	tick();
	check(failed_with("superseded") && flow.read_seq == 0, "the result of a later read is not the own list: superseded");
	at_reading();
	shows(WICAN_DTC_QUEUED, READ, MQTT, 7);
	tick();
	check(failed_with("superseded"), "a lower number than the own one is another request as well: superseded");

	at_clearing();
	shows_done(CLEAR, HTTP, 43, 1);
	shows(WICAN_DTC_RUNNING, READ, MQTT, 44);
	dtc_flow_state(&flow, &adapter, now);
	check(flow.phase == DTC_FLOW_CLEARING, "clear: a later number and the result of the own clear is still stored: wait for it");
	tick();
	check(flow.phase == DTC_FLOW_CLEARED, "the result of the own clear arrives although a later request runs: cleared");
	tick();
	check(flow.phase == DTC_FLOW_CLEARED, "the outcome of the clear stays when somebody else scans");
	at_clearing();
	shows(WICAN_DTC_RUNNING, READ, MQTT, 44);
	tick();
	check(flow.phase == DTC_FLOW_UNKNOWN && nothing_to_take(), "clear: a later number and the stored result is not the own: what was cleared is not known");
}

static void test_result(void)
{
	at_reading();
	dtc_flow_result(&flow, 41, READ, 2, 0, now);
	dtc_flow_result(&flow, 43, READ, 2, 0, now);
	dtc_flow_result(&flow, 0, READ, 2, 0, now);
	check(flow.phase == DTC_FLOW_READING && flow.read_seq == 0, "a result with another number than the own read is ignored");
	dtc_flow_result(&flow, 42, CLEAR, 2, 0, now);
	check(flow.phase == DTC_FLOW_READING && flow.read_seq == 0, "a result with the own number that is a clear is no list of a read: ignored");
	dtc_flow_result(&flow, 42, READ, 3, 0, now);
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42 && flow.list_count == 3 && flow.list_end_ms == 9000, "the result of the own read makes the list");
	dtc_flow_result(&flow, 42, READ, 9, 0, now + 500);
	dtc_flow_result(&flow, 42, CLEAR, 9, 0, now + 500);
	check(flow.phase == DTC_FLOW_LIST && flow.list_count == 3 && flow.list_end_ms == 9000, "the same result once more changes nothing");

	at_clearing();
	dtc_flow_result(&flow, 42, CLEAR, 0, 0, now);
	dtc_flow_result(&flow, 42, READ, 3, 0, now);
	dtc_flow_result(&flow, 44, CLEAR, 0, 0, now);
	check(flow.phase == DTC_FLOW_CLEARING, "a result with another number than the own clear is ignored, also the one of the read before");
	dtc_flow_result(&flow, 43, READ, 3, 0, now);
	check(flow.phase == DTC_FLOW_CLEARING && flow.list_count == 3 && flow.list_end_ms == 11000, "a result with the number of the own clear that is a read is ignored");
	dtc_flow_result(&flow, 43, CLEAR, 1, 7, now);
	check(flow.phase == DTC_FLOW_CLEARED, "the result of the own clear, with a code that came back and an age: cleared");
	dtc_flow_result(&flow, 43, READ, 1, 0, now);
	check(flow.phase == DTC_FLOW_CLEARED && flow.read_seq == 42, "a result after the clear changes nothing");

	at_reading();
	dtc_flow_result(&flow, 42, READ, 4000000000u, 0, now);
	check(flow.phase == DTC_FLOW_LIST && flow.list_count == 4000000000u, "a result with the largest count makes a list with that count");

	at_read_sent();
	dtc_flow_result(&flow, 0, READ, 3, 0, now);
	dtc_flow_result(&flow, 41, READ, 3, 0, now);
	dtc_flow_result(&flow, 42, READ, 3, 0, now);
	check(flow.phase == DTC_FLOW_READ_SENT && flow.read_seq == 0, "a result before the read was accepted is ignored, whatever its number");
	at_clear_sent();
	dtc_flow_result(&flow, 0, CLEAR, 3, 0, now);
	dtc_flow_result(&flow, 42, CLEAR, 3, 0, now);
	dtc_flow_result(&flow, 42, READ, 9, 0, now);
	check(flow.phase == DTC_FLOW_CLEAR_SENT && flow.list_count == 3, "a result before the clear was accepted is ignored");
	at_failed();
	dtc_flow_result(&flow, 0, READ, 3, 0, now);
	dtc_flow_result(&flow, 41, READ, 3, 0, now);
	check(failed_with("busy") && flow.read_seq == 0, "a result after a failure is ignored");
	at_reading();
	dtc_flow_lost(&flow);
	dtc_flow_result(&flow, 42, READ, 3, 0, now);
	check(failed_with("no_answer") && flow.read_seq == 0, "the result of a read that was given up is ignored");
	at_unknown();
	dtc_flow_result(&flow, 43, CLEAR, 0, 0, now);
	check(flow.phase == DTC_FLOW_UNKNOWN, "the result of a clear that was given up is ignored: the user reads again");
}

static void test_restart(void)
{
	static const struct
	{
		scene_t scene;
		dtc_flow_phase_t phase;
		const char *reason;
		const char *what;
	} cases[] = {
		{at_read_waiting, DTC_FLOW_IDLE, "", "the adapter restarts while the read waits to be taken: it was never sent, idle without a failure"},
		{at_read_sent, DTC_FLOW_FAILED, "restarted", "the adapter restarts while the POST of the read is under way: failed with restarted"},
		{at_read_silent, DTC_FLOW_FAILED, "restarted", "the adapter restarts after the POST of the read got no answer: failed with restarted"},
		{at_reading, DTC_FLOW_FAILED, "restarted", "the adapter restarts during the own read: failed with restarted"},
		{at_clear_waiting, DTC_FLOW_IDLE, NULL, "the adapter restarts while the clear waits to be taken: it was never sent, idle and not unknown"},
		{at_clear_sent, DTC_FLOW_UNKNOWN, NULL, "the adapter restarts while the POST of the clear is under way: unknown"},
		{at_clear_silent, DTC_FLOW_UNKNOWN, NULL, "the adapter restarts after the POST of the clear got no answer: unknown"},
		{at_clearing, DTC_FLOW_UNKNOWN, NULL, "the adapter restarts during the own clear: unknown"},
		{at_cleared, DTC_FLOW_IDLE, NULL, "the adapter restarts while the outcome of the clear is shown: dropped"},
		{at_unknown, DTC_FLOW_UNKNOWN, NULL, "the adapter restarts after a clear with unknown outcome: it stays unknown"},
		{at_failed, DTC_FLOW_FAILED, "busy", "the adapter restarts after a failure: the failure stays with its reason"},
	};
	size_t i;

	for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		bool there = cases[i].scene();

		restarts();
		tick();
		check(there && flow.phase == cases[i].phase && (cases[i].reason == NULL || strcmp(flow.reason, cases[i].reason) == 0) && nothing_to_take(), cases[i].what);
	}

	at_list(3);
	restarts();
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0 && flow.list_count == 0 && dtc_flow_seconds_left(&flow, now) == 0, "the adapter restarts while the list is shown: the list is dropped");
	tick();
	check(clear_block() == DTC_FLOW_NO_LIST && ask_clear() == DTC_FLOW_NO_LIST && nothing_to_take(), "after the restart there is no list to clear");
	setup();
	restarts();
	tick();
	check(flow.phase == DTC_FLOW_IDLE && read_block() == DTC_FLOW_ALLOWED, "a restart while nothing was asked changes nothing");

	at_list(3);
	adapter.boot = 76;
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "a lower boot number is another boot number as well: the list is dropped");
	at_clearing();
	adapter.boot = 76;
	tick();
	check(flow.phase == DTC_FLOW_UNKNOWN, "a lower boot number during the own clear: unknown");

	// The request that was withdrawn is not sent late, and a late answer changes nothing
	at_clear_waiting();
	restarts();
	tick();
	dtc_flow_posted(&flow, 202, 1, NULL, now);
	check(flow.phase == DTC_FLOW_IDLE && nothing_to_take(), "a clear that waited when the adapter restarted is never handed out");
	at_read_sent();
	restarts();
	tick();
	dtc_flow_posted(&flow, 202, 1, NULL, now);
	check(failed_with("restarted"), "an answer to the read after the restart was seen is ignored");

	// The restarted adapter shows the very number of the own request
	at_reading();
	restarts();
	shows(WICAN_DTC_RUNNING, READ, HTTP, 42);
	tick();
	check(failed_with("restarted"), "the same number after a restart is not the own read");
	at_clearing();
	restarts();
	shows_done(CLEAR, HTTP, 43, 0);
	tick();
	check(flow.phase == DTC_FLOW_UNKNOWN, "the same number after a restart is not the own clear, its result is not taken: unknown");

	// conn has seen the restart, the flow was not told yet
	at_list(3);
	restarts();
	adapter.up_s = 100;
	shows_done(READ, MQTT, 42, 3);
	tick_without_flow();
	tick_without_flow();
	check(flow.phase == DTC_FLOW_LIST && read_block() == DTC_FLOW_ALLOWED && clear_block() == DTC_FLOW_NO_LIST,
	      "the adapter restarted and shows the number of the own read; the flow was not told yet: the list is no list any more");
	check(ask_clear() == DTC_FLOW_NO_LIST && flow.phase == DTC_FLOW_LIST && nothing_to_take(), "a clear confirmed after the restart is not sent");
	check(ask_read() == DTC_FLOW_ALLOWED && flow.boot == 78, "a read asked for after the restart belongs to the new boot number");
	tick();
	check(flow.phase == DTC_FLOW_READ_SENT, "the state of the new boot number is no restart for the read that was asked for after it");
}

static void test_foreign_scan(void)
{
	at_list(3);
	tick();
	tick();
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42, "states that show the own read as the last request: the list stays");

	at_list(3);
	dtc_flow_state(&flow, NULL, now);
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42, "a state that is NULL while a list is shown is ignored");
	at_reading();
	dtc_flow_state(&flow, NULL, now);
	check(flow.phase == DTC_FLOW_READING && flow.seq == 42 && nothing_to_take(), "a state that is NULL during the own read is ignored");

	at_list(3);
	shows(WICAN_DTC_QUEUED, READ, MQTT, 43);
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0 && flow.list_count == 0, "somebody else's read is queued: the list is dropped");
	at_list(3);
	shows_done(READ, MQTT, 7, 1);
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "the adapter shows a lower number than the one of the own read: the list is dropped as well");
	at_list(3);
	shows(WICAN_DTC_RUNNING, CLEAR, MQTT, 43);
	tick();
	check(flow.phase == DTC_FLOW_IDLE, "somebody else's clear runs: the list is dropped");
	at_list(3);
	shows_done(READ, HTTP, 43, 1);
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "somebody else's read is done already: the list is dropped, the foreign result does not make a list");
	tick();
	check(clear_block() == DTC_FLOW_NO_LIST && ask_clear() == DTC_FLOW_NO_LIST && nothing_to_take(), "after somebody else's scan there is no list to clear");

	// conn has seen the other request, the flow was not told yet
	at_list(3);
	shows_error(READ, MQTT, 43, "ecu_offline");
	tick_without_flow();
	check(flow.phase == DTC_FLOW_LIST && clear_block() == DTC_FLOW_NO_LIST && ask_clear() == DTC_FLOW_NO_LIST && nothing_to_take(),
	      "the adapter shows another request than the own read; the flow was not told yet: a clear is not offered and not sent");

	at_cleared();
	shows(WICAN_DTC_QUEUED, READ, MQTT, 44);
	tick();
	shows_done(READ, MQTT, 44, 0);
	tick();
	check(flow.phase == DTC_FLOW_CLEARED, "somebody else's scan after the own clear: the outcome stays");
	at_failed();
	shows(WICAN_DTC_QUEUED, READ, MQTT, 44);
	tick();
	check(failed_with("busy"), "somebody else's scan after a failure: the failure stays");
}

static void test_lost(void)
{
	static const struct
	{
		scene_t scene;
		dtc_flow_phase_t phase;
		const char *reason;
		const char *what;
	} cases[] = {
		{at_read_waiting, DTC_FLOW_IDLE, "", "the adapter is lost while the read waits to be taken: it was never sent, idle without a failure, and it is not sent"},
		{at_read_sent, DTC_FLOW_FAILED, "no_answer", "the adapter is lost while the POST of the read is under way: failed with no_answer"},
		{at_read_silent, DTC_FLOW_FAILED, "no_answer", "the adapter is lost after the POST of the read got no answer: failed with no_answer"},
		{at_reading, DTC_FLOW_FAILED, "no_answer", "the adapter is lost during the own read: failed with no_answer"},
		{at_clear_waiting, DTC_FLOW_LIST, NULL, "the adapter is lost while the clear waits to be taken: it was never sent, back to the list and not unknown, and it is not sent"},
		{at_clear_sent, DTC_FLOW_UNKNOWN, NULL, "the adapter is lost while the POST of the clear is under way: unknown"},
		{at_clear_silent, DTC_FLOW_UNKNOWN, NULL, "the adapter is lost after the POST of the clear got no answer: unknown"},
		{at_clearing, DTC_FLOW_UNKNOWN, NULL, "the adapter is lost during the own clear: unknown"},
		{at_cleared, DTC_FLOW_CLEARED, NULL, "the adapter is lost while the outcome of the clear is shown: it stays"},
		{at_unknown, DTC_FLOW_UNKNOWN, NULL, "the adapter is lost after a clear with unknown outcome: it stays unknown"},
		{at_failed, DTC_FLOW_FAILED, "busy", "the adapter is lost after a failure: the failure stays with its reason"},
	};
	size_t i;

	for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		bool there = cases[i].scene();

		dtc_flow_lost(&flow);
		check(there && flow.phase == cases[i].phase && (cases[i].reason == NULL || strcmp(flow.reason, cases[i].reason) == 0) && nothing_to_take(), cases[i].what);
	}

	at_list(3);
	dtc_flow_lost(&flow);
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42 && flow.list_count == 3 && flow.list_end_ms == 11000 && clear_block() == DTC_FLOW_ALLOWED,
	      "the adapter is lost while the list is shown: the list stays");
	setup();
	dtc_flow_lost(&flow);
	check(flow.phase == DTC_FLOW_IDLE, "the adapter is lost while nothing was asked: idle");

	at_reading();
	dtc_flow_lost(&flow);
	shows_done(READ, HTTP, 42, 3);
	tick();
	check(failed_with("no_answer") && flow.read_seq == 0, "the adapter is back with the result of the read that was given up: it stays failed");
	at_clearing();
	dtc_flow_lost(&flow);
	shows_done(CLEAR, HTTP, 43, 0);
	tick();
	check(flow.phase == DTC_FLOW_UNKNOWN, "the adapter is back with the result of the clear that was given up: it stays unknown");
	at_read_sent();
	dtc_flow_lost(&flow);
	dtc_flow_posted(&flow, 202, 42, NULL, now);
	check(failed_with("no_answer"), "an answer to a read that was given up is ignored");
}

// A request that waits to be taken was never sent: taken back, it has done nothing
static void test_never_sent(void)
{
	uint32_t seq = 99;

	// The adapter is lost
	check(at_read_waiting(), "the scene: a read waits to be taken when the adapter is lost");
	dtc_flow_lost(&flow);
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0 && flow.reason[0] == '\0' && nothing_to_take(),
	      "lost while the read waits: idle, no failure and no reason, no list, nothing to send");
	dtc_flow_posted(&flow, 202, 42, NULL, now);
	check(flow.phase == DTC_FLOW_IDLE && flow.seq == 0, "an answer to the read that was never sent is ignored");
	tick();
	check(flow.phase == DTC_FLOW_IDLE && read_block() == DTC_FLOW_ALLOWED && ask_read() == DTC_FLOW_ALLOWED && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_READ && seq == 0,
	      "after the read that was never sent the user may read again, and that read is handed out");

	check(at_clear_waiting(), "the scene: a clear waits to be taken when the adapter is lost");
	dtc_flow_lost(&flow);
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42 && flow.list_count == 3 && flow.list_end_ms == 11000 && nothing_to_take(),
	      "lost while the clear waits: back to the list with its number, its codes and its time, nothing to send");
	check(dtc_flow_seconds_left(&flow, now) == 599, "the list a clear came back to when the adapter was lost has the time of its read");
	dtc_flow_posted(&flow, 202, 43, NULL, now);
	check(flow.phase == DTC_FLOW_LIST && flow.seq == 0, "an answer to the clear that was never sent is ignored");
	tick();
	check(flow.phase == DTC_FLOW_LIST && clear_block() == DTC_FLOW_ALLOWED, "the states that follow keep the list of the clear that was never sent: the clear is offered again");
	seq = 99;
	check(ask_clear() == DTC_FLOW_ALLOWED && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_CLEAR && seq == 42,
	      "the user confirms again: the clear is handed out with the number of the list");

	at_clear_waiting();
	dtc_flow_lost(&flow);
	dtc_flow_lost(&flow);
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42 && flow.list_count == 3, "lost a second time: the list a clear came back to stays, as any list does");
	now = 611001;
	values_answer("0", now);
	check(clear_block() == DTC_FLOW_LIST_OLD && dtc_flow_seconds_left(&flow, now) == 0, "the list a clear came back to when the adapter was lost grows old like any other");
	at_clear_waiting();
	shows(WICAN_DTC_QUEUED, READ, MQTT, 43);
	tick();
	dtc_flow_lost(&flow);
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42, "lost while the clear waits and somebody else's scan is queued: back to the list, the flow was not told otherwise");
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "the next state shows somebody else's scan: that list is dropped like any other");

	// The adapter restarts
	check(at_read_waiting(), "the scene: a read waits to be taken when the adapter restarts");
	restarts();
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.reason[0] == '\0' && nothing_to_take(), "a restart while the read waits: idle, no failure and no reason, nothing to send");
	check(read_block() == DTC_FLOW_ALLOWED && ask_read() == DTC_FLOW_ALLOWED && flow.boot == 78 && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_READ,
	      "after the restart the user may read again: that read belongs to the new boot number and is handed out");

	check(at_clear_waiting(), "the scene: a clear waits to be taken when the adapter restarts");
	restarts();
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0 && flow.list_count == 0 && flow.list_end_ms == 0 && nothing_to_take(),
	      "a restart while the clear waits: idle, the list is dropped with its number, its codes and its time, nothing to send");
	tick();
	check(dtc_flow_seconds_left(&flow, now) == 0 && clear_block() == DTC_FLOW_NO_LIST && ask_clear() == DTC_FLOW_NO_LIST && nothing_to_take(),
	      "after a restart while the clear waited there is no list to clear");
	at_clear_waiting();
	restarts();
	adapter.up_s = 100;
	shows_done(READ, HTTP, 42, 3);
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "the restarted adapter shows the very number of the list the clear waited with: the list is dropped all the same");
	at_clear_waiting();
	adapter.boot = 77 + 65536;
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0 && nothing_to_take(), "a boot number that differs by 65536 while the clear waits is a restart");
	at_clear_waiting();
	adapter.boot = 76;
	tick();
	check(flow.phase == DTC_FLOW_IDLE && nothing_to_take(), "a lower boot number while the clear waits is a restart as well");
}

// An accepted request that does not end
static void test_wait(void)
{
	check(at_reading() && flow.accepted_ms == 8000 && now == 9000, "the scene: the own read was accepted at 8000 and is queued");
	shows(WICAN_DTC_RUNNING, READ, HTTP, 42);
	dtc_flow_state(&flow, &adapter, 187999);
	check(flow.phase == DTC_FLOW_READING, "a state 179999 ms after the read was accepted that shows it running: it waits");
	dtc_flow_state(&flow, &adapter, 188000);
	check(flow.phase == DTC_FLOW_READING, "a state 180000 ms after the read was accepted: it still waits");
	dtc_flow_state(&flow, &adapter, 188001);
	check(failed_with("no_answer") && flow.read_seq == 0 && nothing_to_take(), "a state 180001 ms after the read was accepted: failed with no_answer, there is no list");
	dtc_flow_result(&flow, 42, READ, 3, 0, 189000);
	check(failed_with("no_answer") && flow.read_seq == 0, "the result of the read that was given up after its time is ignored");
	at_reading();
	dtc_flow_state(&flow, &adapter, 7000);
	dtc_flow_state(&flow, &adapter, 0);
	check(flow.phase == DTC_FLOW_READING, "states at times before the acceptance: no time passed, the read waits");

	check(at_clearing() && flow.accepted_ms == 12000 && now == 13000, "the scene: the own clear was accepted at 12000 and is queued");
	shows(WICAN_DTC_RUNNING, CLEAR, HTTP, 43);
	dtc_flow_state(&flow, &adapter, 192000);
	check(flow.phase == DTC_FLOW_CLEARING, "a state 180000 ms after the clear was accepted: it still waits");
	dtc_flow_state(&flow, &adapter, 192001);
	check(flow.phase == DTC_FLOW_UNKNOWN && nothing_to_take(), "a state 180001 ms after the clear was accepted: what was cleared is not known");
	dtc_flow_result(&flow, 43, CLEAR, 0, 0, 193000);
	check(flow.phase == DTC_FLOW_UNKNOWN, "the result of the clear that was given up after its time is ignored: the user reads again");

	// When a request was accepted
	at_read_sent();
	dtc_flow_posted(&flow, 202, 42, NULL, 8700);
	check(flow.phase == DTC_FLOW_READING && flow.accepted_ms == 8700, "the time of the acceptance is the time the 202 was reported, not the time the request was taken");
	shows(WICAN_DTC_RUNNING, READ, HTTP, 42);
	dtc_flow_state(&flow, &adapter, 188700);
	check(flow.phase == DTC_FLOW_READING, "180000 ms after the 202 was reported the read still waits");
	dtc_flow_state(&flow, &adapter, 188701);
	check(failed_with("no_answer"), "180001 ms after the 202 was reported the read is given up");
	check(at_read_silent(), "the scene: the POST of the read ended without an answer at 8000");
	shows(WICAN_DTC_QUEUED, READ, HTTP, 42);
	dtc_flow_state(&flow, &adapter, 9500);
	check(flow.phase == DTC_FLOW_READING && flow.seq == 42 && flow.accepted_ms == 9500, "a read found by a state after a POST without an answer is accepted at the time of that state");
	dtc_flow_state(&flow, &adapter, 189500);
	check(flow.phase == DTC_FLOW_READING, "180000 ms after the state that found the read it still waits");
	dtc_flow_state(&flow, &adapter, 189501);
	check(failed_with("no_answer"), "180001 ms after the state that found the read it is given up");
	at_clear_silent();
	shows(WICAN_DTC_QUEUED, CLEAR, HTTP, 43);
	dtc_flow_state(&flow, &adapter, 500000);
	check(flow.phase == DTC_FLOW_CLEARING && flow.accepted_ms == 500000, "a clear found by a state 488 s after its POST ended: its time begins with that state, which does not end it");
	dtc_flow_state(&flow, &adapter, 680000);
	check(flow.phase == DTC_FLOW_CLEARING, "180000 ms after the state that found the clear it still waits");
	dtc_flow_state(&flow, &adapter, 680001);
	check(flow.phase == DTC_FLOW_UNKNOWN, "180001 ms after the state that found the clear its outcome is unknown");

	at_read_silent();
	shows_done(READ, HTTP, 42, 3);
	adapter.dtc.age_s = 5;
	dtc_flow_state(&flow, &adapter, 9500);
	check(flow.phase == DTC_FLOW_READING && flow.accepted_ms == 9500, "a read found by a state that shows it done 5 s ago is accepted at the time of that state, not at the end of the scan");

	// A display that runs for a long time: the time counts from the acceptance, not from the start
	epoch = 1000000;
	check(at_reading() && flow.accepted_ms == 1008000, "a read accepted 1008 s after the display started: the state of the next second does not end it");
	dtc_flow_state(&flow, &adapter, 1188000);
	check(flow.phase == DTC_FLOW_READING, "180000 ms after an acceptance at 1008 s the read still waits");
	dtc_flow_state(&flow, &adapter, 1188001);
	check(failed_with("no_answer"), "180001 ms after an acceptance at 1008 s the read is given up");
	at_read_silent();
	shows(WICAN_DTC_QUEUED, READ, HTTP, 42);
	tick();
	check(flow.phase == DTC_FLOW_READING && flow.accepted_ms == 1009000, "a read found by a state 1009 s after the display started is accepted then and waits");
	epoch = DAYS_49 - 100000;
	check(at_reading() && flow.accepted_ms == DAYS_49 - 92000, "the scene: a read accepted 92 s before 2^32 ms");
	dtc_flow_state(&flow, &adapter, DAYS_49 + 88000);
	check(flow.phase == DTC_FLOW_READING, "180000 ms after an acceptance shortly before 2^32 ms the read still waits");
	dtc_flow_state(&flow, &adapter, DAYS_49 + 88001);
	check(failed_with("no_answer"), "180001 ms after an acceptance shortly before 2^32 ms the read is given up");
	epoch = DAYS_49 + 1000000;
	check(at_reading() && flow.accepted_ms == DAYS_49 + 1008000, "a read accepted after 2^32 ms waits from its acceptance, with the whole time");
	at_read_silent();
	shows(WICAN_DTC_QUEUED, READ, HTTP, 42);
	tick();
	check(flow.phase == DTC_FLOW_READING && flow.accepted_ms == DAYS_49 + 1009000, "a read found by a state after 2^32 ms is accepted at the whole time of that state");
	epoch = 0;
	at_reading();
	dtc_flow_state(&flow, &adapter, 8000 + DAYS_49 + 5);
	check(failed_with("no_answer"), "2^32 ms after the acceptance the read is given up: its time does not begin anew");

	// What the state shows goes first
	at_reading();
	shows_error(READ, HTTP, 42, "ecu_offline");
	dtc_flow_state(&flow, &adapter, 188001);
	check(failed_with("ecu_offline"), "a state after the time that shows the own read as an error: failed with the reason of the state, not with no_answer");
	at_reading();
	shows(WICAN_DTC_QUEUED, READ, MQTT, 43);
	dtc_flow_state(&flow, &adapter, 188001);
	check(failed_with("superseded"), "a state after the time that shows a later request and no result of the own read: superseded, not no_answer");
	at_clearing();
	shows_error(CLEAR, HTTP, 43, "engine_running");
	dtc_flow_state(&flow, &adapter, 192001);
	check(failed_with("engine_running") && flow.read_seq == 42, "a state after the time that shows the own clear as an error: failed with the reason of the state, not unknown");
	at_reading();
	shows_done(READ, HTTP, 42, 3);
	dtc_flow_state(&flow, &adapter, 188000);
	check(flow.phase == DTC_FLOW_READING, "the own read is done and its result is not there 180000 ms after the acceptance: it still waits");
	dtc_flow_state(&flow, &adapter, 188001);
	check(failed_with("no_answer") && flow.read_seq == 0, "the own read is done and its result is not there 180001 ms after the acceptance: given up");
	at_reading();
	shows_done(READ, HTTP, 42, 3);
	shows(WICAN_DTC_QUEUED, READ, MQTT, 43);
	dtc_flow_state(&flow, &adapter, 188001);
	check(failed_with("no_answer"), "a later request runs, the result of the own read is still stored and the time is over: given up with no_answer, not superseded");
	at_reading();
	shows_done(READ, HTTP, 42, 3);
	shows_error(READ, MQTT, 43, "ecu_offline");
	dtc_flow_state(&flow, &adapter, 188000);
	check(flow.phase == DTC_FLOW_READING, "a later request ended with an error and the result of the own read is still stored: it is waited for until the time is over");
	dtc_flow_state(&flow, &adapter, 188001);
	check(failed_with("no_answer"), "a later request ended with an error, the result of the own read is still stored and the time is over: given up with no_answer");

	// Only a state ends the wait, and only the wait of an accepted request
	at_reading();
	dtc_flow_result(&flow, 42, READ, 3, 0, 500000);
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42 && flow.list_end_ms == 500000, "a result that arrives 492 s after the acceptance with no state in between is the list: only a state ends the wait");
	dtc_flow_state(&flow, &adapter, 900000);
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42, "the list is not ended by the time of the read that made it");
	at_cleared();
	dtc_flow_state(&flow, &adapter, 900000);
	check(flow.phase == DTC_FLOW_CLEARED, "the outcome of a clear stays, however long ago the clear was accepted");
	at_read_sent();
	dtc_flow_state(&flow, &adapter, 900000);
	check(flow.phase == DTC_FLOW_READ_SENT && !flow.posted, "a POST under way is no accepted request: a state 892 s after it was taken changes nothing");
	check(at_clear_sent(), "the scene: the POST of a clear is under way, the read before it was accepted at 8000");
	dtc_flow_state(&flow, &adapter, 200000);
	check(flow.phase == DTC_FLOW_CLEAR_SENT, "a state 192 s after the read before was accepted does not end the clear whose POST is under way");
	dtc_flow_posted(&flow, 0, 0, NULL, 200000);
	dtc_flow_state(&flow, &adapter, 201000);
	check(flow.phase == DTC_FLOW_CLEAR_SENT, "a POST without an answer waits for its two states, not for a time: the first one decides nothing");
	dtc_flow_state(&flow, &adapter, 202000);
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42, "the second state decides that the clear did not arrive: back to the list, 194 s after its read was accepted");
	at_read_waiting();
	dtc_flow_state(&flow, &adapter, 900000);
	check(flow.phase == DTC_FLOW_READ_SENT && dtc_flow_take(&flow, NULL, 900000) == DTC_FLOW_SEND_READ, "a read that waits to be taken is handed out, however long it waited");
}

// The result of the own request cannot be had
static void test_no_result(void)
{
	static const struct
	{
		scene_t scene;
		dtc_flow_phase_t phase;
		const char *reason;
		dtc_flow_send_t send;
		const char *what;
	} cases[] = {
		{setup_idle, DTC_FLOW_IDLE, NULL, DTC_FLOW_SEND_NOTHING, "no result while nothing was asked: idle"},
		{at_read_waiting, DTC_FLOW_READ_SENT, NULL, DTC_FLOW_SEND_READ, "no result while the read waits to be taken: nothing changes, it is still handed out"},
		{at_read_sent, DTC_FLOW_READ_SENT, NULL, DTC_FLOW_SEND_NOTHING, "no result while the POST of the read is under way: nothing changes"},
		{at_read_silent, DTC_FLOW_READ_SENT, NULL, DTC_FLOW_SEND_NOTHING, "no result after the POST of the read got no answer: nothing changes"},
		{at_reading, DTC_FLOW_FAILED, "no_result", DTC_FLOW_SEND_NOTHING, "no result during the own read: failed with no_result"},
		{at_list_of_three, DTC_FLOW_LIST, NULL, DTC_FLOW_SEND_NOTHING, "no result while the list is shown: the list stays"},
		{at_clear_waiting, DTC_FLOW_CLEAR_SENT, NULL, DTC_FLOW_SEND_CLEAR, "no result while the clear waits to be taken: nothing changes, it is still handed out"},
		{at_clear_sent, DTC_FLOW_CLEAR_SENT, NULL, DTC_FLOW_SEND_NOTHING, "no result while the POST of the clear is under way: nothing changes"},
		{at_clear_silent, DTC_FLOW_CLEAR_SENT, NULL, DTC_FLOW_SEND_NOTHING, "no result after the POST of the clear got no answer: nothing changes"},
		{at_clearing, DTC_FLOW_UNKNOWN, NULL, DTC_FLOW_SEND_NOTHING, "no result during the own clear: what was cleared is not known"},
		{at_cleared, DTC_FLOW_CLEARED, NULL, DTC_FLOW_SEND_NOTHING, "no result while the outcome of the clear is shown: it stays"},
		{at_unknown, DTC_FLOW_UNKNOWN, NULL, DTC_FLOW_SEND_NOTHING, "no result after a clear with unknown outcome: it stays unknown"},
		{at_failed, DTC_FLOW_FAILED, "busy", DTC_FLOW_SEND_NOTHING, "no result after a failure: the failure stays with its reason"},
	};
	size_t i;

	for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		bool there = cases[i].scene();
		uint32_t list = flow.read_seq, codes = flow.list_count, number = flow.seq;
		bool posted = flow.posted;

		dtc_flow_no_result(&flow);
		check(there && flow.phase == cases[i].phase && (cases[i].reason == NULL || strcmp(flow.reason, cases[i].reason) == 0) && flow.read_seq == list &&
		      flow.list_count == codes && flow.seq == number && flow.posted == posted && dtc_flow_take(&flow, NULL, now) == cases[i].send, cases[i].what);
	}

	at_reading();
	dtc_flow_no_result(&flow);
	dtc_flow_result(&flow, 42, READ, 3, 0, now);
	shows_done(READ, HTTP, 42, 3);
	tick();
	check(failed_with("no_result") && flow.read_seq == 0, "the result of the read arrives after it was given up as not to be had: it stays failed, there is no list");
	check(read_block() == DTC_FLOW_ALLOWED && ask_read() == DTC_FLOW_ALLOWED && flow.phase == DTC_FLOW_READ_SENT, "after a result that could not be had the user may read again");
	at_clearing();
	dtc_flow_no_result(&flow);
	dtc_flow_result(&flow, 43, CLEAR, 0, 0, now);
	shows_done(CLEAR, HTTP, 43, 0);
	tick();
	tick();
	check(flow.phase == DTC_FLOW_UNKNOWN && clear_block() == DTC_FLOW_NO_LIST, "the result of the clear arrives after it was given up as not to be had: it stays unknown, nothing can be cleared");
	at_read_sent();
	dtc_flow_no_result(&flow);
	dtc_flow_posted(&flow, 202, 42, NULL, now);
	check(flow.phase == DTC_FLOW_READING && flow.seq == 42, "no result while the POST is under way: the POST still has its answer");
	at_read_silent();
	tick();
	shows(WICAN_DTC_QUEUED, READ, HTTP, 42);
	tick();
	check(flow.phase == DTC_FLOW_READING && flow.rounds_without_answer == 1, "the scene: a read found by the second state after its POST got no answer");
	dtc_flow_no_result(&flow);
	check(failed_with("no_result"), "no result for a read that was found by a state: failed with no_result like one that was accepted with a 202");
	at_clear_silent();
	tick();
	shows(WICAN_DTC_QUEUED, CLEAR, HTTP, 43);
	tick();
	dtc_flow_no_result(&flow);
	check(flow.phase == DTC_FLOW_UNKNOWN, "no result for a clear that was found by a state: unknown");
	at_reading();
	dtc_flow_no_result(&flow);
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_IDLE, "the user leaves the failure of a result that could not be had: idle");
}

static void test_dismiss(void)
{
	uint32_t seq = 99;

	at_list(3);
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0 && flow.list_count == 0 && flow.list_end_ms == 0, "the user leaves the list: idle, the list is dropped");
	check(dtc_flow_seconds_left(&flow, now) == 0 && clear_block() == DTC_FLOW_NO_LIST && ask_clear() == DTC_FLOW_NO_LIST && nothing_to_take(), "a list that was left cannot be cleared");
	at_cleared();
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "the user leaves the outcome of the clear: idle");
	at_failed();
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_IDLE, "the user leaves the failure: idle");
	at_clear_sent();
	dtc_flow_posted(&flow, 409, 42, "busy", now);
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0 && flow.list_count == 0, "the user leaves the failure of a clear: idle, the list that stayed is dropped");
	at_unknown();
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "the user leaves the unknown outcome: idle");
	setup();
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_IDLE && read_block() == DTC_FLOW_ALLOWED, "leaving while nothing was asked: idle");

	// While a request is under way nothing changes
	at_read_waiting();
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_READ_SENT && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_READ, "leaving while the read waits to be taken: it is still sent");
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_READ_SENT, "leaving while the POST of the read is under way: nothing changes");
	dtc_flow_posted(&flow, 0, 0, NULL, now);
	dtc_flow_dismiss(&flow);
	shows(WICAN_DTC_QUEUED, READ, HTTP, 42);
	tick();
	check(flow.phase == DTC_FLOW_READING && flow.seq == 42, "leaving after the POST got no answer: the states still decide");
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_READING, "leaving during the own read: it goes on");
	shows_done(READ, HTTP, 42, 3);
	tick();
	check(flow.phase == DTC_FLOW_LIST && flow.list_count == 3, "the outcome of the read that was left is kept: the list");

	at_clear_waiting();
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_CLEAR_SENT && flow.read_seq == 42 && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_CLEAR && seq == 42, "leaving while the clear waits to be taken: it is still sent, with its number");
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_CLEAR_SENT && flow.read_seq == 42, "leaving while the POST of the clear is under way: nothing changes");
	dtc_flow_posted(&flow, 202, 43, NULL, now);
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_CLEARING, "leaving during the own clear: it goes on");
	shows_done(CLEAR, HTTP, 43, 0);
	tick();
	check(flow.phase == DTC_FLOW_CLEARED, "the outcome of the clear that was left is kept: cleared");
}

static void test_read_again(void)
{
	uint32_t seq = 99;

	at_list(3);
	check(ask_read() == DTC_FLOW_ALLOWED && flow.phase == DTC_FLOW_READ_SENT && flow.read_seq == 0 && flow.list_count == 0 && flow.list_end_ms == 0,
	      "a read asked for while a list is shown drops the list");
	check(dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_READ && seq == 0, "the read asked for with a list is handed out, with number 0");
	dtc_flow_posted(&flow, 202, 43, NULL, now);
	shows_done(READ, HTTP, 43, 0);
	tick();
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 43 && flow.list_count == 0 && flow.list_end_ms == 13000, "the second read ends with its own list");

	at_list(3);
	engine = "780";
	tick();
	check(ask_read() == DTC_FLOW_ENGINE_RUNNING && flow.phase == DTC_FLOW_LIST && flow.read_seq == 42 && flow.list_count == 3 && nothing_to_take(),
	      "a read that is not offered changes nothing: the list stays, nothing is sent");
	at_list(0);
	check(ask_clear() == DTC_FLOW_NO_CODES && flow.phase == DTC_FLOW_LIST && nothing_to_take(), "a clear that is not offered changes nothing: nothing is sent");
	at_reading();
	check(ask_read() == DTC_FLOW_BUSY && flow.phase == DTC_FLOW_READING && flow.seq == 42 && nothing_to_take(), "a read asked for while one runs is not sent, the one that runs goes on");

	check(at_cleared() && ask_read() == DTC_FLOW_ALLOWED && flow.phase == DTC_FLOW_READ_SENT && flow.read_seq == 0, "a read after a clear");
	check(at_failed() && ask_read() == DTC_FLOW_ALLOWED && flow.phase == DTC_FLOW_READ_SENT, "a read after a failure");
	at_clear_sent();
	dtc_flow_posted(&flow, 409, 42, "busy", now);
	check(ask_read() == DTC_FLOW_ALLOWED && flow.phase == DTC_FLOW_READ_SENT && flow.read_seq == 0 && flow.list_count == 0, "a read after a clear that failed drops the list that stayed");
	at_unknown();
	shows_error(CLEAR, HTTP, 43, "internal");
	tick();
	check(ask_read() == DTC_FLOW_ALLOWED && flow.phase == DTC_FLOW_READ_SENT, "a read after a clear with unknown outcome");

	// What the adapter showed when the request began
	setup();
	ask_read();
	check(flow.boot == 77 && flow.seq_before == 41 && flow.seq == 0, "a read notes the boot number and the request number the adapter showed");
	at_list(3);
	ask_clear();
	check(flow.boot == 77 && flow.seq_before == 42 && flow.seq == 0 && flow.read_seq == 42, "a clear notes the boot number and the request number the adapter showed");
	setup();
	shows_error(READ, MQTT, 42, "ecu_offline");
	tick();
	ask_read();
	check(adapter.dtc.result_seq == 41 && flow.seq_before == 42, "the number noted is the one of the last request, not the one of the stored result");
	dtc_flow_take(&flow, NULL, now);
	dtc_flow_posted(&flow, 0, 0, NULL, now);
	tick();
	tick();
	check(failed_with("no_answer"), "two states with the number of that last request: the read did not arrive");
}

// A request that waits for the task of the display to take it
static void test_waiting(void)
{
	uint32_t seq = 99;

	check(at_clear_waiting() && now == 12000 && flow.list_end_ms == 11000, "the scene: a clear waits to be taken, its read ended at 11000");
	now = 611000;
	check(dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_CLEAR && seq == 42 && flow.phase == DTC_FLOW_CLEAR_SENT, "a clear taken 600000 ms after its read ended is handed out");
	at_clear_waiting();
	now = 611001;
	seq = 99;
	check(dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_NOTHING && seq == 0, "a clear taken 600001 ms after its read ended is not handed out");
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 42 && flow.list_count == 3 && flow.list_end_ms == 11000, "the clear that waited too long: back to the list, nothing was sent");
	check(clear_block() == DTC_FLOW_RPM_UNKNOWN && dtc_flow_seconds_left(&flow, now) == 0 && read_block() == DTC_FLOW_RPM_UNKNOWN,
	      "back at the list after waiting too long no request is under way any more: the engine speed of 600 s ago is the reason now");
	dtc_flow_posted(&flow, 202, 43, NULL, now);
	check(flow.phase == DTC_FLOW_LIST && flow.seq == 0, "an answer to the clear that was not handed out is ignored");
	now = 12000;
	check(nothing_to_take() && flow.phase == DTC_FLOW_LIST, "the clear that waited too long is not handed out later, whatever the time is then");
	check(clear_block() == DTC_FLOW_ALLOWED && ask_clear() == DTC_FLOW_ALLOWED && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_CLEAR && seq == 42,
	      "after the clear that waited too long only a new confirmation hands a clear out");

	at_clear_waiting();
	dtc_flow_take(&flow, &seq, 611001);
	check(flow.phase == DTC_FLOW_LIST && dtc_flow_seconds_left(&flow, 12000) == 599 && dtc_flow_seconds_left(&flow, 611001) == 0,
	      "the list a clear came back to after waiting too long has the time of its read");
	shows(WICAN_DTC_QUEUED, READ, MQTT, 43);
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "somebody else's scan drops that list like any other");
	at_clear_waiting();
	dtc_flow_take(&flow, &seq, 611001);
	restarts();
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "a restart drops that list like any other");
	at_clear_waiting();
	dtc_flow_take(&flow, &seq, 611001);
	dtc_flow_dismiss(&flow);
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "the user leaves that list like any other");

	at_clear_waiting();
	check(dtc_flow_take(&flow, &seq, 0) == DTC_FLOW_SEND_CLEAR && seq == 42, "a clear taken at a time before its read ended: no time passed, it is handed out");
	at_clear_waiting();
	seq = 99;
	check(dtc_flow_take(&flow, &seq, UINT64_MAX) == DTC_FLOW_SEND_NOTHING && seq == 0 && flow.phase == DTC_FLOW_LIST, "a clear taken at the largest time is not handed out");
	at_clear_waiting();
	check(dtc_flow_take(&flow, NULL, 611001) == DTC_FLOW_SEND_NOTHING && flow.phase == DTC_FLOW_LIST && nothing_to_take(), "a clear that waited too long is withdrawn also without a place for the number");

	at_read_waiting();
	seq = 99;
	check(dtc_flow_take(&flow, &seq, 5000000) == DTC_FLOW_SEND_READ && seq == 0 && flow.phase == DTC_FLOW_READ_SENT, "a read that waits is handed out whenever it is taken");
	at_list(3);
	now = 700000;
	check(ask_read() == DTC_FLOW_RPM_UNKNOWN && (values_answer("0", now), ask_read()) == DTC_FLOW_ALLOWED && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_READ && seq == 0,
	      "a read asked for while an old list is shown is handed out: the age of the list it drops does not count");
}

// The display against the rules of the adapter (main/dtc_state.h): what the adapter does between what the
// display knows and what it sends
static void test_adapter_rules(void)
{
	uint32_t seq = 99;

	// Somebody else gets in between the confirmation and the request
	at_clear_waiting();
	shows(WICAN_DTC_QUEUED, READ, MQTT, 43);
	tick();
	check(flow.phase == DTC_FLOW_CLEAR_SENT && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_CLEAR && seq == 42,
	      "somebody else's scan shows up while the clear waits to be taken: the clear goes out once, with the number of the list the user confirmed");
	dtc_flow_posted(&flow, 409, 43, "busy", now);
	check(failed_with("busy") && nothing_to_take() && clear_block() == DTC_FLOW_BUSY, "the adapter refuses that clear as busy: failed, nothing is sent again");
	shows_done(READ, MQTT, 43, 5);
	tick();
	tick();
	check(failed_with("busy") && clear_block() == DTC_FLOW_NO_LIST && ask_clear() == DTC_FLOW_NO_LIST && nothing_to_take(),
	      "somebody else's read ended with its own codes: the list of the own read cannot be cleared any more");
	at_clear_waiting();
	shows(WICAN_DTC_QUEUED, READ, MQTT, 43);
	tick();
	dtc_flow_take(&flow, &seq, now);
	dtc_flow_posted(&flow, 0, 0, NULL, now);
	tick();
	check(flow.phase == DTC_FLOW_UNKNOWN && nothing_to_take(), "that clear without an answer: the state shows somebody else's request, what became of the own one is not known");

	// The request that was given up as not arrived arrives after all
	at_clear_silent();
	tick();
	tick();
	shows(WICAN_DTC_QUEUED, CLEAR, HTTP, 43);
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0 && clear_block() == DTC_FLOW_BUSY && nothing_to_take(),
	      "the clear that was given up as not arrived shows up after all: the list is dropped, no second clear is offered or sent");
	shows_done(CLEAR, HTTP, 43, 0);
	tick();
	tick();
	check(flow.phase == DTC_FLOW_IDLE && clear_block() == DTC_FLOW_NO_LIST, "the outcome of the clear that arrived late makes no list and nothing to clear");
	at_read_silent();
	tick();
	tick();
	shows(WICAN_DTC_QUEUED, READ, HTTP, 42);
	tick();
	shows_done(READ, HTTP, 42, 3);
	tick();
	tick();
	check(failed_with("no_answer") && flow.read_seq == 0 && clear_block() == DTC_FLOW_NO_LIST && ask_clear() == DTC_FLOW_NO_LIST && nothing_to_take(),
	      "the read that was given up as not arrived shows up after all: it stays failed, its result makes no list that could be cleared");

	// An accepted request the AutoPID task never picks up ends as an error after 20 s
	at_clearing();
	shows_error(CLEAR, HTTP, 43, "expired");
	tick();
	check(failed_with("expired") && flow.read_seq == 42 && clear_block() == DTC_FLOW_NO_LIST && nothing_to_take(), "the own clear expired on the adapter: failed, nothing is sent again, the user reads again");

	// Behind the largest request number the adapter begins at 1 again
	setup();
	shows_done(READ, MQTT, 2147483647u, 2);
	tick();
	tick();
	check(ask_read() == DTC_FLOW_ALLOWED && flow.seq_before == 2147483647u && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_READ, "the scene: a read while the adapter is at its largest request number");
	dtc_flow_posted(&flow, 0, 0, NULL, now);
	shows(WICAN_DTC_QUEUED, READ, HTTP, 1);
	tick();
	check(flow.phase == DTC_FLOW_READING && flow.seq == 1, "the read without an answer is found under number 1, the one behind the largest");
	shows_done(READ, HTTP, 1, 3);
	tick();
	tick();
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == 1 && ask_clear() == DTC_FLOW_ALLOWED && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_CLEAR && seq == 1,
	      "the list with number 1 is cleared with number 1");
	dtc_flow_posted(&flow, 202, 2, NULL, now);
	shows_done(CLEAR, HTTP, 2, 0);
	tick();
	check(flow.phase == DTC_FLOW_CLEARED, "the clear with number 2 behind the list with number 1: cleared");

	setup();
	shows_done(READ, MQTT, 2147483647u, 2);
	tick();
	tick();
	ask_read();
	dtc_flow_take(&flow, &seq, now);
	dtc_flow_posted(&flow, 202, 1, NULL, now);
	check(flow.phase == DTC_FLOW_READING && flow.seq == 1, "202 with number 1 while the adapter was at its largest number: accepted, a number need not be higher than the one before");

	// Another adapter answers in place of the own one
	at_list(3);
	strcpy(adapter.id, OTHER);
	adapter.boot = 5;
	tick();
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "another adapter with another boot number answers: the list is dropped");
	strcpy(adapter.id, OWN);
	adapter.boot = 77;
	tick();
	tick();
	tick();
	check(flow.phase == DTC_FLOW_IDLE && clear_block() == DTC_FLOW_NO_LIST && ask_clear() == DTC_FLOW_NO_LIST && nothing_to_take(), "the own adapter answers again with the numbers of before: the list stays dropped");
	at_clearing();
	strcpy(adapter.id, OTHER);
	adapter.boot = 5;
	tick();
	check(flow.phase == DTC_FLOW_UNKNOWN, "another adapter answers during the own clear: unknown");

	// A POST whose end nobody reports: the WiFi was lost while it was under way
	at_clear_sent();
	conn_wifi(&conn, false, now);
	conn_wifi(&conn, true, now);
	tick();
	tick();
	tick();
	check(flow.phase == DTC_FLOW_CLEAR_SENT && read_block() == DTC_FLOW_BUSY,
	      "the WiFi was lost while the POST of the clear was under way and nobody told the flow: the states of the same boot do not end the request");
	dtc_flow_lost(&flow);
	check(flow.phase == DTC_FLOW_UNKNOWN && nothing_to_take(), "told that the adapter was lost, the clear whose POST never ended is unknown");
	dtc_flow_posted(&flow, 202, 43, NULL, now);
	check(flow.phase == DTC_FLOW_UNKNOWN, "the end of that POST, reported after all, is ignored");
}

// The numbers of a real adapter and the times of a display that runs for weeks: nothing is compared or kept
// in fewer bits than it has
static void test_large_numbers(void)
{
	uint32_t seq = 99;

	setup();
	adapter.boot = BIG_BOOT;
	shows_done(READ, MQTT, BIG_SEQ, 2);
	tick();
	tick();
	check(ask_read() == DTC_FLOW_ALLOWED && flow.boot == BIG_BOOT && flow.seq_before == BIG_SEQ && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_READ,
	      "the scene: a read from an adapter with a boot number and a request number above 10^9");
	dtc_flow_posted(&flow, 202, BIG_SEQ + 1, NULL, now);
	shows_done(READ, HTTP, BIG_SEQ + 1, 3);
	tick();
	tick();
	check(flow.phase == DTC_FLOW_LIST && flow.read_seq == BIG_SEQ + 1 && clear_block() == DTC_FLOW_ALLOWED, "the read with a number above 10^9 ends with its list, which may be cleared");
	check(ask_clear() == DTC_FLOW_ALLOWED && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_CLEAR && seq == BIG_SEQ + 1, "the clear is handed out with the whole number of that read");
	dtc_flow_posted(&flow, 0, 0, NULL, now);
	shows(WICAN_DTC_QUEUED, CLEAR, HTTP, BIG_SEQ + 2);
	tick();
	check(flow.phase == DTC_FLOW_CLEARING && flow.seq == BIG_SEQ + 2, "the clear without an answer is found under its whole number");
	shows_done(CLEAR, HTTP, BIG_SEQ + 2, 0);
	tick();
	check(flow.phase == DTC_FLOW_CLEARED, "the clear with a number above 10^9 ends as cleared");

	// Numbers that differ by 65536 are different numbers
	at_list(3);
	adapter.boot = 77 + 65536;
	tick_without_flow();
	check(flow.phase == DTC_FLOW_LIST && clear_block() == DTC_FLOW_NO_LIST, "the adapter shows a boot number 65536 above the one of the read; the flow was not told yet: no list");
	dtc_flow_state(&flow, conn_state(&conn), now);
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "a boot number that differs by 65536 is a restart: the list is dropped");
	at_reading();
	adapter.boot = 77 + 65536;
	tick();
	check(failed_with("restarted"), "a boot number that differs by 65536 during the own read: restarted");

	at_list(3);
	shows_done(READ, MQTT, 42 + 65536, 3);
	tick_without_flow();
	check(flow.phase == DTC_FLOW_LIST && clear_block() == DTC_FLOW_NO_LIST, "the adapter shows a request 65536 above the own read; the flow was not told yet: no list");
	dtc_flow_state(&flow, conn_state(&conn), now);
	check(flow.phase == DTC_FLOW_IDLE && flow.read_seq == 0, "a request number that differs by 65536 from the one of the own read drops the list");

	at_read_silent();
	shows(WICAN_DTC_QUEUED, READ, HTTP, 41 + 65536);
	tick();
	check(flow.phase == DTC_FLOW_READING && flow.seq == 41 + 65536, "a number 65536 above the one before is another number: the read without an answer is found");
	at_reading();
	shows(WICAN_DTC_QUEUED, READ, MQTT, 42 + 65536);
	tick();
	check(failed_with("superseded"), "a request 65536 above the own one is a later request: superseded");
	at_reading();
	shows(WICAN_DTC_QUEUED, READ, MQTT, 43);
	adapter.dtc.result_seq = 42 + 65536;
	tick();
	check(failed_with("superseded") && flow.read_seq == 0, "a stored result 65536 above the own number is not the own result: superseded");
	at_reading();
	dtc_flow_result(&flow, 42 + 65536, READ, 3, 0, now);
	check(flow.phase == DTC_FLOW_READING && flow.read_seq == 0, "a result 65536 above the own number is not the own result: ignored");

	// 65536 is a number like any other
	setup();
	shows_done(READ, MQTT, 65535, 2);
	tick();
	tick();
	ask_read();
	dtc_flow_take(&flow, &seq, now);
	dtc_flow_posted(&flow, 0, 0, NULL, now);
	shows(WICAN_DTC_QUEUED, READ, HTTP, 65536);
	tick();
	check(flow.phase == DTC_FLOW_READING && flow.seq == 65536, "the read without an answer is found under number 65536");
	setup();
	shows_done(READ, MQTT, 65535, 2);
	tick();
	tick();
	ask_read();
	dtc_flow_take(&flow, &seq, now);
	dtc_flow_posted(&flow, 202, 65536, NULL, now);
	check(flow.phase == DTC_FLOW_READING && flow.seq == 65536, "202 with number 65536: accepted with that number");
	shows_done(READ, HTTP, 65536, 65536);
	tick();
	tick();
	check(flow.phase == DTC_FLOW_LIST && flow.list_count == 65536 && clear_block() == DTC_FLOW_ALLOWED, "a list of 65536 trouble codes is no list without codes");
	check(ask_clear() == DTC_FLOW_ALLOWED && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_CLEAR && seq == 65536, "the list with number 65536 is cleared with number 65536");

	// 2^32 ms after the read ended
	at_list(3);
	now = 11000 + DAYS_49 + 5;
	values_answer("0", now);
	check(clear_block() == DTC_FLOW_LIST_OLD && dtc_flow_seconds_left(&flow, now) == 0, "2^32 ms after its read ended a list is old: its time does not begin anew");
	at_clear_waiting();
	seq = 99;
	check(dtc_flow_take(&flow, &seq, 11000 + DAYS_49 + 5) == DTC_FLOW_SEND_NOTHING && seq == 0 && flow.phase == DTC_FLOW_LIST, "a clear taken 2^32 ms after its read ended is not handed out");

	// A read that ends shortly before 2^32 ms
	epoch = DAYS_49 - 11500;
	check(at_list(3) && flow.list_end_ms == DAYS_49 - 500 && values_find(&values, "ENGINE_RPM")->seen_ms == DAYS_49 + 500,
	      "the scene: a read that ended 500 ms before 2^32 ms, an engine speed seen 500 ms after");
	check(clear_block() == DTC_FLOW_ALLOWED && dtc_flow_seconds_left(&flow, now) == 599, "the engine speed seen after 2^32 ms is one from after the read; 599 s are left");
	check(dtc_flow_seconds_left(&flow, DAYS_49 + 599499) == 1 && dtc_flow_seconds_left(&flow, DAYS_49 + 599500) == 0, "the time of that list ends 600000 ms after the read, beyond 2^32 ms");
	now = DAYS_49 + 599500;
	values_answer("0", now);
	check(clear_block() == DTC_FLOW_ALLOWED && ask_clear() == DTC_FLOW_ALLOWED && dtc_flow_take(&flow, &seq, now) == DTC_FLOW_SEND_CLEAR && seq == 42,
	      "600000 ms after that read the clear is still offered and handed out");
	at_list(3);
	now = DAYS_49 + 599501;
	values_answer("0", now);
	check(clear_block() == DTC_FLOW_LIST_OLD, "600001 ms after that read the list is old");
	epoch = DAYS_49 + 1000000;
	check(at_list(3) && clear_block() == DTC_FLOW_ALLOWED && dtc_flow_seconds_left(&flow, now) == 599, "a read that began and ended after 2^32 ms: its list may be cleared for 600 s");
	now += 600000;
	values_answer("0", now);
	check(clear_block() == DTC_FLOW_LIST_OLD && dtc_flow_seconds_left(&flow, now) == 0, "601 s after that read the list is old");
	epoch = 0;
}

/*
 * The same rules a second time, for the walk below. In another shape: one record for the own request with
 * the stage it is in, one for the list, one for what is shown while no request is under way; and the
 * reasons against a command each as a statement of its own, of which the first in the order of the enum
 * is taken.
 */
typedef enum
{
	OWN_NONE,
	OWN_READ,
	OWN_CLEAR,
} own_t;

typedef enum
{
	STAGE_WAITING,      // to be taken
	STAGE_POSTING,      // taken, the POST is under way
	STAGE_SILENT,       // the POST ended without an answer
	STAGE_ACCEPTED,
} stage_t;

typedef enum
{
	SHOWN_NOTHING,
	SHOWN_LIST,
	SHOWN_CLEARED,
	SHOWN_FAILED,
	SHOWN_UNKNOWN,
} shown_t;

typedef struct
{
	own_t own;
	stage_t stage;
	uint32_t boot;              // of the adapter when the last request began
	uint32_t before;            // number the adapter showed then
	uint32_t number;            // of the own request once it is accepted
	uint64_t since;             // the time it was accepted at
	int silent_states;
	shown_t shown;
	char why[32];
	bool list;                  // a list of an own read is kept
	uint32_t list_number, list_codes;
	uint64_t list_ended;
} model_t;

static dtc_flow_phase_t model_phase(const model_t *model)
{
	static const dtc_flow_phase_t shown[] = {DTC_FLOW_IDLE, DTC_FLOW_LIST, DTC_FLOW_CLEARED, DTC_FLOW_FAILED, DTC_FLOW_UNKNOWN};

	if(model->own == OWN_READ) return model->stage == STAGE_ACCEPTED ? DTC_FLOW_READING : DTC_FLOW_READ_SENT;
	if(model->own == OWN_CLEAR) return model->stage == STAGE_ACCEPTED ? DTC_FLOW_CLEARING : DTC_FLOW_CLEAR_SENT;
	return shown[model->shown];
}

static dtc_flow_send_t model_waiting(const model_t *model)
{
	if(model->own == OWN_NONE || model->stage != STAGE_WAITING) return DTC_FLOW_SEND_NOTHING;
	return model->own == OWN_READ ? DTC_FLOW_SEND_READ : DTC_FLOW_SEND_CLEAR;
}

static dtc_flow_block_t model_block(const model_t *model, const conn_t *connection, const values_t *seen, const catalog_t *listed, bool clear, bool stuck, uint64_t at)
{
	bool applies[DTC_FLOW_BUTTON_STUCK + 1] = {false};
	conn_view_t view = conn_view(connection, at);
	const wican_state_t *state = conn_state(connection);
	bool has_list = model->own == OWN_NONE && model->shown == SHOWN_LIST && state != NULL && state->boot == model->boot && state->dtc.seq == model->list_number;
	const value_t *rpm = NULL;
	bool rpm_listed = false, rpm_kept, rpm_good;
	int i, reason;

	for(i = 0; i < seen->count; i++)
	{
		if(strcmp(seen->items[i].name, "ENGINE_RPM") == 0) rpm = &seen->items[i];
	}
	for(i = 0; i < listed->count; i++)
	{
		if(strcmp(listed->entries[i].name, "ENGINE_RPM") == 0) rpm_listed = true;
	}
	rpm_kept = rpm != NULL && rpm->kind == VALUE_NUMBER && (at <= rpm->seen_ms || at - rpm->seen_ms < 10000);
	rpm_good = rpm_kept && (!clear || !has_list || rpm->seen_ms > model->list_ended);

	applies[DTC_FLOW_NO_ADAPTER] = view == CONN_VIEW_NO_WIFI || view == CONN_VIEW_CONNECTING || view == CONN_VIEW_NO_ANSWER;
	applies[DTC_FLOW_FOREIGN] = state != NULL && strcmp(state->id, connection->bound_id) != 0;
	applies[DTC_FLOW_NO_API] = view == CONN_VIEW_NO_API;
	applies[DTC_FLOW_AUTOPID_OFF] = state != NULL && state->autopid == WICAN_AUTOPID_OFF;
	applies[DTC_FLOW_STARTING] = (state != NULL && state->autopid == WICAN_AUTOPID_STARTING) || !conn_dtc_allowed(connection);
	applies[DTC_FLOW_NOT_SUPPORTED] = state != NULL && !state->dtc.supported;
	applies[DTC_FLOW_BUSY] = (state != NULL && (state->dtc.phase == WICAN_DTC_QUEUED || state->dtc.phase == WICAN_DTC_RUNNING)) || model->own != OWN_NONE;
	applies[DTC_FLOW_ECU_OFFLINE] = state != NULL && !state->ecu_online;
	applies[DTC_FLOW_ENGINE_RUNNING] = rpm_listed && rpm_kept && (isnan(rpm->number) || rpm->number >= 50.0);
	applies[DTC_FLOW_RPM_UNKNOWN] = rpm_listed && !rpm_good;
	if(clear)
	{
		applies[DTC_FLOW_NO_LIST] = !has_list;
		applies[DTC_FLOW_LIST_OLD] = has_list && at > model->list_ended && at - model->list_ended > 600000;
		applies[DTC_FLOW_NO_CODES] = has_list && model->list_codes == 0;
		applies[DTC_FLOW_BUTTON_STUCK] = stuck;
	}
	for(reason = DTC_FLOW_NO_ADAPTER; reason <= DTC_FLOW_BUTTON_STUCK; reason++)
	{
		if(applies[reason]) return (dtc_flow_block_t)reason;
	}
	return DTC_FLOW_ALLOWED;
}

static void model_drop_list(model_t *model)
{
	model->list = false;
	model->list_number = 0;
	model->list_codes = 0;
	model->list_ended = 0;
}

static dtc_flow_block_t model_ask(model_t *model, const conn_t *connection, const values_t *seen, const catalog_t *listed, bool clear, bool stuck, uint64_t at)
{
	dtc_flow_block_t block = model_block(model, connection, seen, listed, clear, stuck, at);

	if(block != DTC_FLOW_ALLOWED) return block;

	if(!clear) model_drop_list(model);
	model->own = clear ? OWN_CLEAR : OWN_READ;
	model->stage = STAGE_WAITING;
	model->boot = conn_state(connection)->boot;
	model->before = conn_state(connection)->dtc.seq;
	model->number = 0;
	model->shown = SHOWN_NOTHING;
	return DTC_FLOW_ALLOWED;
}

static dtc_flow_send_t model_take(model_t *model, uint32_t *seq, uint64_t at)
{
	dtc_flow_send_t send = model_waiting(model);

	*seq = 0;
	if(send == DTC_FLOW_SEND_NOTHING) return send;

	// A clear that waited until its list is old is shown as the list again, nothing goes out
	if(send == DTC_FLOW_SEND_CLEAR && at > model->list_ended && at - model->list_ended > 600000)
	{
		model->own = OWN_NONE;
		model->shown = SHOWN_LIST;
		return DTC_FLOW_SEND_NOTHING;
	}
	model->stage = STAGE_POSTING;
	if(send == DTC_FLOW_SEND_CLEAR) *seq = model->list_number;
	return send;
}

static void model_failed(model_t *model, const char *why)
{
	model->own = OWN_NONE;
	model->shown = SHOWN_FAILED;
	snprintf(model->why, sizeof(model->why), "%.31s", why != NULL ? why : "");
}

// The own request is over and nobody saw how: a read failed, of a clear nothing is known
static void model_gone(model_t *model, const char *why)
{
	if(model->own == OWN_CLEAR)
	{
		model->own = OWN_NONE;
		model->shown = SHOWN_UNKNOWN;
	}
	else
	{
		model_failed(model, why);
	}
}

static void model_posted(model_t *model, int status, uint32_t seq, const char *reason, uint64_t at)
{
	if(model->own == OWN_NONE || model->stage != STAGE_POSTING) return;

	if(status == 202 && seq != 0)
	{
		model->stage = STAGE_ACCEPTED;
		model->number = seq;
		model->since = at;
	}
	else if(status == 0 || status == 202)
	{
		model->stage = STAGE_SILENT;
		model->silent_states = 0;
	}
	else
	{
		model_failed(model, reason);
	}
}

// A request that waits to be taken is taken back: it was never sent, so nothing failed and nothing is
// unknown. Of a clear the list is shown again if it still means something.
static void model_never_sent(model_t *model, bool list_still_good)
{
	model->shown = model->own == OWN_CLEAR && list_still_good ? SHOWN_LIST : SHOWN_NOTHING;
	model->own = OWN_NONE;
	if(model->shown == SHOWN_NOTHING) model_drop_list(model);
}

static void model_state(model_t *model, const wican_state_t *state, uint64_t at)
{
	if(state == NULL) return;

	if(model->own == OWN_NONE)
	{
		// What is shown: a list lives as long as the adapter shows its read as the last request, the outcome
		// of a clear as long as the adapter did not restart
		if((model->shown == SHOWN_LIST && (state->boot != model->boot || state->dtc.seq != model->list_number)) ||
		   (model->shown == SHOWN_CLEARED && state->boot != model->boot))
		{
			model->shown = SHOWN_NOTHING;
			model_drop_list(model);
		}
		return;
	}

	if(state->boot != model->boot)
	{
		if(model->stage == STAGE_WAITING) model_never_sent(model, false);
		else model_gone(model, "restarted");
		return;
	}
	if(model->stage == STAGE_SILENT)
	{
		if(state->dtc.seq == model->before)
		{
			model->silent_states++;
			if(model->silent_states < 2) return;
			if(model->own == OWN_READ)
			{
				model_failed(model, "no_answer");
			}
			else
			{
				model->own = OWN_NONE;
				model->shown = SHOWN_LIST;
			}
			return;
		}
		if(state->dtc.seq != 0 && state->dtc.has_request && state->dtc.from_http && state->dtc.clear == (model->own == OWN_CLEAR))
		{
			model->stage = STAGE_ACCEPTED;
			model->number = state->dtc.seq;
			model->since = at;
		}
		else
		{
			model_gone(model, "superseded");
			return;
		}
	}
	if(model->stage != STAGE_ACCEPTED) return;

	if(state->dtc.seq == model->number)
	{
		if(state->dtc.phase == WICAN_DTC_ERROR) model_failed(model, state->dtc.reason);
	}
	else if(state->dtc.result_seq != model->number)
	{
		model_gone(model, "superseded");
	}
	// Whatever the state left under way has had its time when more than 180 s have passed
	if(model->own != OWN_NONE && at > model->since && at - model->since > 180000) model_gone(model, "no_answer");
}

static void model_no_result(model_t *model)
{
	if(model->own != OWN_NONE && model->stage == STAGE_ACCEPTED) model_gone(model, "no_result");
}

static void model_lost(model_t *model)
{
	if(model->own == OWN_NONE) return;

	if(model->stage == STAGE_WAITING) model_never_sent(model, true);
	else model_gone(model, "no_answer");
}

static void model_result(model_t *model, uint32_t seq, bool clear, uint32_t count, uint32_t age_s, uint64_t at)
{
	uint64_t age_ms = (uint64_t)age_s * 1000;

	if(model->own == OWN_NONE || model->stage != STAGE_ACCEPTED || seq != model->number || clear != (model->own == OWN_CLEAR)) return;

	if(model->own == OWN_READ)
	{
		model->shown = SHOWN_LIST;
		model->list = true;
		model->list_number = seq;
		model->list_codes = count;
		model->list_ended = at > age_ms ? at - age_ms : 0;
	}
	else
	{
		model->shown = SHOWN_CLEARED;
	}
	model->own = OWN_NONE;
}

static void model_dismiss(model_t *model)
{
	if(model->own != OWN_NONE) return;
	model->shown = SHOWN_NOTHING;
	model_drop_list(model);
}

static uint32_t model_seconds_left(const model_t *model, uint64_t at)
{
	uint64_t age = at > model->list_ended ? at - model->list_ended : 0;
	uint64_t left;

	if(model_phase(model) != DTC_FLOW_LIST || age >= 600000) return 0;
	left = 600000 - age;
	return (uint32_t)(left / 1000 + (left % 1000 != 0 ? 1 : 0));
}

/*
 * The adapter of the walk: the rules of main/dtc_state.h and tools/w906/API.md once more, as far as they
 * decide about a POST, so that the walk goes through sequences that make sense.
 */
typedef struct
{
	wican_state_t state;
	uint32_t upcoming;          // number of the next request it accepts
	bool stored_clear;          // action of the result it has stored
	uint64_t ended_at;          // when its last scan ended, in the time of the display
} sim_t;

static uint32_t walk_random_state;

static uint32_t walk_random(uint32_t below)
{
	walk_random_state = walk_random_state * 1664525u + 1013904223u;
	return (walk_random_state >> 8) % below;
}

// Another boot number than `boot`: near it, 65536 away from it, or far away
static uint32_t walk_other_boot(uint32_t boot)
{
	static const uint32_t boots[] = {77, 78, 77 + 65536, BIG_BOOT};
	uint32_t other = boots[walk_random(4)];

	return other != boot ? other : boots[walk_random(3)] + 1000;
}

static void sim_boot(sim_t *sim, uint32_t boot)
{
	// Request numbers begin anywhere: small, around 65536, far up, and shortly before the largest
	static const uint32_t firsts[] = {1, 1, 65500, 1000000000u, 2147483600u};

	strcpy(sim->state.id, OWN);
	sim->state.boot = boot;
	sim->state.up_s = 100;
	sim->state.autopid = WICAN_AUTOPID_RUN;
	sim->state.pids = 35;
	sim->state.ecu_online = true;
	sim->state.dtc.supported = true;
	sim->upcoming = firsts[walk_random(5)] + walk_random(40);
}

static bool sim_busy(const sim_t *sim)
{
	return sim->state.dtc.phase == WICAN_DTC_QUEUED || sim->state.dtc.phase == WICAN_DTC_RUNNING;
}

// POST /api/dtc. Returns the status; *number and *reason as in the body (the reason of a 202 is NULL).
static int sim_post(sim_t *sim, bool clear, bool http, uint32_t seq, uint64_t at, uint32_t *number, const char **reason)
{
	wican_dtc_t *dtc = &sim->state.dtc;

	*number = 0;
	*reason = "not_ready";
	if(sim->state.autopid != WICAN_AUTOPID_RUN) return 503;

	*number = dtc->seq;
	*reason = "busy";
	if(sim_busy(sim)) return 409;
	if(clear && http)
	{
		*reason = "read_required";
		if(dtc->phase != WICAN_DTC_DONE || dtc->clear || (at > sim->ended_at && at - sim->ended_at > 600000)) return 409;
		*reason = "stale_seq";
		if(seq != dtc->seq) return 409;
		*reason = "nothing_to_clear";
		if(dtc->count == 0) return 409;
	}

	dtc->seq = sim->upcoming;
	sim->upcoming = sim->upcoming == 2147483647u ? 1 : sim->upcoming + 1;
	dtc->phase = WICAN_DTC_QUEUED;
	dtc->has_request = true;
	dtc->clear = clear;
	dtc->from_http = http;
	dtc->reason[0] = '\0';
	*number = dtc->seq;
	*reason = NULL;
	return 202;
}

// The scan goes one step on: picked up, then done or failed
static void sim_step(sim_t *sim, uint64_t at)
{
	static const char *const reasons[] = {"ecu_offline", "engine_running", "engine_state_unknown", "internal", "expired",
	                                      "a_reason_longer_than_the_31_bytes_of_the_field"};
	static const uint32_t counts[] = {0, 1, 3, 3, 3, 65535};
	wican_dtc_t *dtc = &sim->state.dtc;

	if(dtc->phase == WICAN_DTC_QUEUED && walk_random(8) != 0)
	{
		dtc->phase = WICAN_DTC_RUNNING;
	}
	else if(sim_busy(sim))
	{
		if(walk_random(5) == 0)
		{
			dtc->phase = WICAN_DTC_ERROR;
			snprintf(dtc->reason, sizeof(dtc->reason), "%.31s", reasons[walk_random(6)]);
		}
		else
		{
			dtc->phase = WICAN_DTC_DONE;
			dtc->result_seq = dtc->seq;
			dtc->count = counts[walk_random(6)];
			sim->stored_clear = dtc->clear;
		}
		sim->ended_at = at;
	}
}

// The state as the adapter answers it at `at`
static const wican_state_t *sim_state(sim_t *sim, uint64_t at)
{
	bool ended = sim->state.dtc.phase == WICAN_DTC_DONE || sim->state.dtc.phase == WICAN_DTC_ERROR;

	sim->state.dtc.age_s = ended && at > sim->ended_at ? (uint32_t)((at - sim->ended_at) / 1000) : 0;
	return &sim->state;
}

// What the walk watches next to the model: the promises about what is handed out
typedef struct
{
	bool read_due, clear_due;   // allowed by dtc_flow_read() / dtc_flow_clear() and not handed out yet
	bool read_out;              // a read was handed out and has not made a list yet
	bool clear_out;             // a clear was handed out since the last request was allowed
	uint32_t list_seq;          // number of the result that made the list of an own read, 0: there is none
	uint32_t list_boot;         // boot number the adapter showed when that read was asked for
	uint64_t list_end;          // when that read ended: the time its result came minus the age it came with
	bool restarted;             // a state with another boot number was seen since
} promises_t;

// The promises of dtc_flow.h about what is offered and handed out, each watched on its own
typedef enum
{
	PROMISE_ONCE,               // handed out once per call that allowed it, nothing else
	PROMISE_LIST,               // a clear only for the list of an own read, with its number, never after a restart
	PROMISE_TIME,               // no clear later than 600 s after its read ended
	PROMISE_OFFER,              // only with the engine off, a clear also with the switch free
	PROMISE_OUTCOME,            // a list and a "cleared" only from the result of a request that was handed out
	PROMISES,
} promise_t;

typedef struct
{
	long calls;
	long different;             // walks in which module and model differed
	long broken[PROMISES];      // times a promise was broken
	long phases[DTC_FLOW_UNKNOWN + 1];
	long read_blocks[DTC_FLOW_BUTTON_STUCK + 1];
	long clear_blocks[DTC_FLOW_BUTTON_STUCK + 1];
	long reads_out, clears_out, lists, cleared, adopted, back_to_list, no_answer, restarted, superseded, refused, scan_errors, unknown;
	long steps_back, without_seq, without_state, ignored_results, late_told, too_late, last_moment, last_moment_out;
	long lost_unsent_reads, lost_unsent_clears, restart_unsent, waited_out_reads, waited_out_clears, no_results, old_speeds;
} walk_result_t;

// The engine as the top of dtc_flow.h asks for it: a speed below 50 that is a number, seen less than 10000 ms
// ago and, for a clear, later than the end of the read; a catalogue without the speed asks for nothing
static bool walk_engine_stands(const values_t *seen, const catalog_t *listed, bool after, uint64_t read_end, uint64_t at)
{
	const value_t *rpm = values_find(seen, "ENGINE_RPM");

	if(catalog_find(listed, "ENGINE_RPM") < 0) return true;
	if(rpm == NULL || rpm->kind != VALUE_NUMBER || !(rpm->number < 50.0)) return false;
	if(at > rpm->seen_ms && at - rpm->seen_ms >= 10000) return false;
	return !after || rpm->seen_ms > read_end;
}

// The adapter restarted or is lost: what waits to be taken must never go out
static void walk_gone(promises_t *promise, bool restarted)
{
	if(restarted) promise->restarted = true;
	promise->read_due = false;
	promise->clear_due = false;
}

// A result made a list or the outcome of a clear: only for a request of that kind that was handed out
static void walk_outcome(promises_t *promise, walk_result_t *result, dtc_flow_phase_t before, dtc_flow_phase_t after, uint32_t number, bool clear,
                         uint32_t age_s, uint64_t at)
{
	uint64_t age_ms = (uint64_t)age_s * 1000;

	if(after == DTC_FLOW_LIST && before == DTC_FLOW_READING)
	{
		if(!promise->read_out || clear || number == 0) result->broken[PROMISE_OUTCOME]++;
		promise->list_seq = number;
		promise->list_end = at > age_ms ? at - age_ms : 0;
		promise->read_out = false;
	}
	else if(after == DTC_FLOW_CLEARED && before == DTC_FLOW_CLEARING)
	{
		if(!promise->clear_out || !clear || number == 0) result->broken[PROMISE_OUTCOME]++;
		promise->clear_out = false;
	}
	// A result changes nothing else
	else if(after != before)
	{
		result->broken[PROMISE_OUTCOME]++;
	}
}

#define WALKS           80
#define WALK_CALLS      40000

static conn_t walk_conn;
static dtc_flow_t walk_flow;
static values_t walk_values;
static catalog_t walk_with_rpm, walk_without_rpm;

static void walk_values_answer(const char *rpm, uint64_t at)
{
	char json[64];

	if(rpm == NULL) snprintf(json, sizeof(json), "{\"COOLANT_TMP\":21.5}");
	else snprintf(json, sizeof(json), "{\"ENGINE_RPM\":%s}", rpm);
	values_apply(&walk_values, json, strlen(json), -1, at, work, VALUES_TOKENS);
}

// Random calls of the user, the task of the display and the adapter, with times that mostly go on by a
// part of a second, sometimes stand at, before or behind a limit and sometimes step back. Module and model
// are compared after every call. false if they differed; the walk goes on to its end all the same, so
// that the promises are watched over all of it.
static bool walk(uint32_t seed, walk_result_t *result)
{
	static const char *const engines[] = {"0", "0", "0", "0", "0", "0", "49.99", "50", "780", "\"on\"", "\"off\"", NULL};
	static const char *const reasons[] = {NULL, "", "busy", "read_required", "stale_seq", "nothing_to_clear", "not_ready", "forbidden", "bad_request",
	                                      "a_reason_longer_than_the_31_bytes_of_the_field"};
	static const int statuses[] = {0, 202, 409, 503, 403, 400, 500, -1};
	static const uint32_t ages[] = {0, 0, 1, 5, 599, 600, 601, 4294967295u};
	static const uint32_t counts[] = {0, 1, 3, 65535, 4000000000u};
	conn_t *c = &walk_conn;
	dtc_flow_t *f = &walk_flow;
	const catalog_t *listed = &walk_with_rpm;
	model_t model;
	sim_t sim;
	promises_t promise;
	const char *engine_now = "0";
	const char *what = "";
	uint64_t at;
	bool dead = false, upstream = false;
	// Module and model differed in this walk: from there on only the promises are watched
	bool differed = false;
	// The POST that was handed out and whose end was not reported yet
	bool answer_due = false;
	int answer_status = 0;
	uint32_t answer_number = 0;
	const char *answer_reason = NULL;
	int call;

	walk_random_state = seed;
	at = walk_random(2) == 0 ? 1000 : 4294967296ull - 300000;
	memset(&model, 0, sizeof(model));
	memset(&promise, 0, sizeof(promise));
	sim_boot(&sim, 77);
	conn_init(c, walk_random(4) == 0 ? NULL : OWN);
	conn_wifi(c, true, at);
	memset(f, 0x5A, sizeof(*f));
	dtc_flow_init(f);
	values_init(&walk_values);

	for(call = 0; call < WALK_CALLS; call++)
	{
		uint32_t pace = walk_random(1000);
		uint32_t operation = walk_random(1000);
		uint64_t before = at;
		dtc_flow_phase_t phase_before = f->phase;
		shown_t shown_before = model.shown;
		own_t own_before = model.own;
		stage_t stage_before = model.stage;
		uint64_t since_before = model.since;
		bool same = true;
		// What this call told the module: a state, that the adapter is lost
		bool told_state = false, told_lost = false;
		int i;

		if(pace < 380) at += walk_random(200);
		else if(pace < 800) at += 1000;
		// Around the ages at which a value is not fresh any more and at which it is gone
		else if(pace < 830) at += 2999 + walk_random(3);
		else if(pace < 840) at += 9999 + walk_random(3);
		else if(pace < 860)
		{
			// To the end of the time a list may be cleared, with an engine speed from that moment, and back
			// to the time its read ended
			if(model.list && at < model.list_ended + 599999)
			{
				at = model.list_ended + 599999 + walk_random(3);
				walk_values_answer("0", at);
			}
		}
		else if(pace < 875)
		{
			if(model.list && at < model.list_ended + 4000) at = model.list_ended + walk_random(3);
		}
		else if(pace < 905) at -= walk_random(4000) % (at + 1);
		else if(pace < 908) at += 10000 + walk_random(600000);
		else if(pace < 911)
		{
			// To the end of the time an accepted request is given
			if(model.own != OWN_NONE && model.stage == STAGE_ACCEPTED && at < model.since + 179999) at = model.since + 179999 + walk_random(3);
		}
		if(at < before) result->steps_back++;

		if(operation < 300)
		{
			// The task of the display: the next request of the connection, answered at once
			conn_ask_t ask = conn_next(c, at);

			if(ask == CONN_ASK_STATE)
			{
				if(dead || walk_random(40) == 0)
				{
					conn_got_state(c, CONN_GOT_FAILED, NULL, at);
				}
				else if(upstream)
				{
					conn_got_state(c, CONN_GOT_NOT_FOUND, NULL, at);
				}
				else
				{
					conn_got_state(c, CONN_GOT_OK, sim_state(&sim, at), at);
					// Now and then the flow is told only with the next state
					if(walk_random(20) != 0)
					{
						if(conn_state(c) != NULL && conn_state(c)->boot != promise.list_boot) walk_gone(&promise, true);
						dtc_flow_state(f, conn_state(c), at);
						model_state(&model, conn_state(c), at);
						told_state = true;
					}
					else
					{
						result->late_told++;
					}
				}
			}
			else if(ask == CONN_ASK_RESULT)
			{
				const wican_dtc_t *dtc = &conn_state(c)->dtc;

				conn_got_result(c, walk_random(40) == 0 ? CONN_GOT_FAILED : CONN_GOT_OK, at);
				dtc_flow_result(f, sim.state.dtc.result_seq, sim.stored_clear, sim.state.dtc.count, dtc->age_s, at);
				model_result(&model, sim.state.dtc.result_seq, sim.stored_clear, sim.state.dtc.count, dtc->age_s, at);
				walk_outcome(&promise, result, phase_before, f->phase, sim.state.dtc.result_seq, sim.stored_clear, dtc->age_s, at);
			}
			else if(ask == CONN_ASK_CATALOG)
			{
				conn_got_catalog(c, CONN_GOT_OK, at);
			}
			else if(ask == CONN_ASK_VALUES)
			{
				walk_values_answer(engine_now, at);
				conn_got_values(c, CONN_GOT_OK, at);
			}
			what = "round";
		}
		else if(operation < 400)
		{
			sim_step(&sim, at);
			what = "scan step";
		}
		else if(operation < 450)
		{
			dtc_flow_block_t got = dtc_flow_read(f, c, &walk_values, listed, at);

			same = got == model_ask(&model, c, &walk_values, listed, false, false, at);
			if(got == DTC_FLOW_ALLOWED)
			{
				const value_t *rpm = values_find(&walk_values, "ENGINE_RPM");

				// Only with the engine off, and never while a request is on its way
				if(!walk_engine_stands(&walk_values, listed, false, 0, at)) result->broken[PROMISE_OFFER]++;
				if(listed == &walk_with_rpm && rpm != NULL && values_age(rpm, at) == VALUE_AGE_OLD) result->old_speeds++;
				if(promise.read_due || promise.clear_due) result->broken[PROMISE_ONCE]++;
				promise.read_due = true;
				promise.read_out = false;
				promise.clear_out = false;
				promise.list_seq = 0;
				promise.list_boot = conn_state(c)->boot;
				promise.restarted = false;
			}
			what = "read";
		}
		else if(operation < 520)
		{
			bool stuck = walk_random(12) == 0;
			dtc_flow_block_t got;

			// Now and then the user confirms in the last moment of the list, or one millisecond later
			if(model.own == OWN_NONE && model.shown == SHOWN_LIST && at < model.list_ended + 599999 && walk_random(10) == 0)
			{
				at = model.list_ended + 599999 + walk_random(3);
				walk_values_answer("0", at);
			}
			got = dtc_flow_clear(f, c, &walk_values, listed, stuck, at);

			same = got == model_ask(&model, c, &walk_values, listed, true, stuck, at);
			if(got == DTC_FLOW_ALLOWED)
			{
				// Only for the list of an own read that the adapter still shows as its last request, in the boot
				// of that read, not later than 600 s after it ended, with the engine off since, the switch free
				if(promise.list_seq == 0 || promise.restarted || conn_state(c)->boot != promise.list_boot || conn_state(c)->dtc.seq != promise.list_seq) result->broken[PROMISE_LIST]++;
				if(at > promise.list_end && at - promise.list_end > 600000) result->broken[PROMISE_TIME]++;
				if(!walk_engine_stands(&walk_values, listed, true, promise.list_end, at) || stuck) result->broken[PROMISE_OFFER]++;
				if(promise.read_due || promise.clear_due) result->broken[PROMISE_ONCE]++;
				if(at > promise.list_end && at - promise.list_end >= 599000) result->last_moment++;
				promise.clear_due = true;
				promise.clear_out = false;
			}
			what = "clear";
		}
		else if(operation < 620)
		{
			uint32_t got_seq = 0xDEADBEEFu, expected_seq = 0;
			bool with_seq = walk_random(10) != 0;
			dtc_flow_send_t got, expected;

			// Now and then a clear has waited until the last moment of its list, or one millisecond longer
			if(model.own == OWN_CLEAR && model.stage == STAGE_WAITING && at < model.list_ended + 599999 && walk_random(6) == 0)
			{
				at = model.list_ended + 599999 + walk_random(3);
			}
			// Now and then the adapter is lost or shows another boot number before the request that waits is
			// taken: the take that follows must hand out nothing
			if(model.own != OWN_NONE && model.stage == STAGE_WAITING)
			{
				uint32_t mishap = walk_random(24);

				if(mishap < 2)
				{
					dtc_flow_lost(f);
					model_lost(&model);
					walk_gone(&promise, false);
					told_lost = true;
				}
				else if(mishap == 2)
				{
					wican_state_t state = *sim_state(&sim, at);

					state.boot = walk_other_boot(model.boot);
					walk_gone(&promise, true);
					dtc_flow_state(f, &state, at);
					model_state(&model, &state, at);
					told_state = true;
				}
			}
			got = dtc_flow_take(f, with_seq ? &got_seq : NULL, at);
			expected = model_take(&model, &expected_seq, at);

			same = got == expected && (!with_seq || got_seq == expected_seq);
			if(!with_seq) result->without_seq++;
			if(got == DTC_FLOW_SEND_READ)
			{
				if(!promise.read_due) result->broken[PROMISE_ONCE]++;
				if(with_seq && got_seq != 0) result->broken[PROMISE_LIST]++;
				promise.read_out = true;
				result->reads_out++;
			}
			else if(got == DTC_FLOW_SEND_CLEAR)
			{
				// With the number of the list, never after a restart, never later than 600 s after the read ended
				if(!promise.clear_due) result->broken[PROMISE_ONCE]++;
				if(promise.list_seq == 0 || promise.restarted || (with_seq && got_seq != promise.list_seq) || f->read_seq != promise.list_seq) result->broken[PROMISE_LIST]++;
				if(at > promise.list_end && at - promise.list_end > 600000) result->broken[PROMISE_TIME]++;
				if(at > promise.list_end && at - promise.list_end >= 599999) result->last_moment_out++;
				promise.clear_out = true;
				result->clears_out++;
			}
			else
			{
				if(got != DTC_FLOW_SEND_NOTHING || (with_seq && got_seq != 0)) result->broken[PROMISE_ONCE]++;
				if(promise.clear_due && at > promise.list_end && at - promise.list_end > 600000) result->too_late++;
			}
			// Whatever was due is not due any more: handed out now, or never
			promise.read_due = false;
			promise.clear_due = false;
			if(got != DTC_FLOW_SEND_NOTHING)
			{
				// What the caller sends is what the module handed out, with the number it knows the list by
				bool sends_clear = got == DTC_FLOW_SEND_CLEAR;
				uint32_t sends_seq = with_seq ? got_seq : f->read_seq;

				// What becomes of the POST: it arrives and is answered, it arrives and the answer is lost, it does
				// not arrive, or the answer is one the display cannot read
				uint32_t fate = walk_random(100);

				answer_due = true;
				answer_status = 0;
				answer_number = 0;
				answer_reason = NULL;
				if(dead || fate < 10)
				{
					// lost on the way
				}
				else if(fate < 20)
				{
					sim_post(&sim, sends_clear, true, sends_seq, at, &answer_number, &answer_reason);
					answer_number = 0;
					answer_reason = NULL;
				}
				else if(fate < 24)
				{
					if(sim_post(&sim, sends_clear, true, sends_seq, at, &answer_number, &answer_reason) == 202) answer_status = 202;
					answer_number = 0;
				}
				else if(fate < 28)
				{
					answer_status = statuses[2 + walk_random(6)];
					answer_reason = reasons[walk_random(10)];
				}
				else
				{
					answer_status = sim_post(&sim, sends_clear, true, sends_seq, at, &answer_number, &answer_reason);
				}
			}
			what = "take";
		}
		else if(operation < 700)
		{
			if(answer_due)
			{
				dtc_flow_posted(f, answer_status, answer_number, answer_reason, at);
				model_posted(&model, answer_status, answer_number, answer_reason, at);
				answer_due = false;
			}
			what = "posted";
		}
		else if(operation < 750)
		{
			if(walk_random(4) == 0) engine_now = engines[walk_random(12)];
			if(walk_random(40) == 0) values_clear(&walk_values);
			else walk_values_answer(engine_now, at);
			if(walk_random(30) == 0) listed = listed == &walk_with_rpm ? &walk_without_rpm : &walk_with_rpm;
			what = "values";
		}
		else if(operation < 758)
		{
			// Left while no request was under way: whatever was shown is gone
			if(phase_before != DTC_FLOW_READ_SENT && phase_before != DTC_FLOW_READING && phase_before != DTC_FLOW_CLEAR_SENT && phase_before != DTC_FLOW_CLEARING) promise.list_seq = 0;
			dtc_flow_dismiss(f);
			model_dismiss(&model);
			what = "dismiss";
		}
		else if(operation < 762)
		{
			dtc_flow_lost(f);
			walk_gone(&promise, false);
			model_lost(&model);
			told_lost = true;
			what = "lost";
		}
		else if(operation < 772)
		{
			// Somebody else asks the adapter: over MQTT, or another client over HTTP
			uint32_t number;
			const char *reason;

			sim_post(&sim, walk_random(3) == 0, walk_random(3) == 0, sim.state.dtc.seq, at, &number, &reason);
			what = "other request";
		}
		else if(operation < 802)
		{
			// The adapter and the network around it change
			uint32_t change = walk_random(40);

			if(change < 2) sim_boot(&sim, walk_other_boot(sim.state.boot));
			else if(change < 4) sim.state.up_s = 14;
			else if(change < 7) sim.state.autopid = (wican_autopid_t)walk_random(2);
			else if(change < 10) sim.state.ecu_online = false;
			else if(change < 12) sim.state.dtc.supported = false;
			else if(change < 14) strcpy(sim.state.id, OTHER);
			else if(change < 16) dead = true;
			else if(change < 18) upstream = true;
			else if(change < 20) conn_wifi(c, false, at);
			else
			{
				dead = false;
				upstream = false;
				strcpy(sim.state.id, OWN);
				sim.state.up_s = 100;
				sim.state.autopid = WICAN_AUTOPID_RUN;
				sim.state.ecu_online = true;
				sim.state.dtc.supported = true;
				conn_wifi(c, true, at);
			}
			what = "change";
		}
		else if(operation < 812)
		{
			// An answer to a POST that belongs to nothing, or comes with whatever
			int status = statuses[walk_random(8)];
			uint32_t numbers[] = {0, model.number, model.before, model.before + 1, model.list_number, 1 + walk_random(100), 65536, model.before + 65536};
			uint32_t number = numbers[walk_random(8)];
			const char *reason = reasons[walk_random(10)];

			dtc_flow_posted(f, status, number, reason, at);
			model_posted(&model, status, number, reason, at);
			what = "posted, any";
		}
		else if(operation < 827)
		{
			// A result with whatever number and action
			uint32_t numbers[] = {0, model.number, model.number + 1, model.before, model.list_number, sim.state.dtc.result_seq, model.number + 65536, model.number - 65536};
			uint32_t number = numbers[walk_random(8)];
			bool clear = walk_random(2) == 0;
			uint32_t count = counts[walk_random(5)];
			uint32_t age = ages[walk_random(8)];

			dtc_flow_result(f, number, clear, count, age, at);
			model_result(&model, number, clear, count, age, at);
			walk_outcome(&promise, result, phase_before, f->phase, number, clear, age, at);
			if(f->phase == phase_before) result->ignored_results++;
			what = "result, any";
		}
		else if(operation < 852)
		{
			// A state with whatever it shows, or none at all
			wican_state_t state = *sim_state(&sim, at);
			uint32_t numbers[] = {0, model.number, model.number + 1, model.before, model.before + 1, model.list_number, sim.state.dtc.seq,
			                      model.number + 65536, model.before + 65536, model.list_number + 65536, 65536};
			uint32_t results[] = {0, model.number, model.list_number, sim.state.dtc.result_seq, model.number + 65536};

			if(walk_random(30) == 0) state.boot = walk_other_boot(state.boot);
			if(walk_random(3) != 0)
			{
				state.dtc.seq = numbers[walk_random(11)];
				state.dtc.phase = (wican_dtc_phase_t)walk_random(5);
				state.dtc.has_request = walk_random(8) != 0;
				state.dtc.from_http = walk_random(4) != 0;
				state.dtc.clear = walk_random(2) == 0;
				state.dtc.result_seq = results[walk_random(5)];
				snprintf(state.dtc.reason, sizeof(state.dtc.reason), "%s", walk_random(2) == 0 ? "ecu_offline" : "a_reason_of_exactly_31_bytes_xx");
			}
			if(walk_random(12) == 0)
			{
				dtc_flow_state(f, NULL, at);
				model_state(&model, NULL, at);
				result->without_state++;
			}
			else
			{
				if(state.boot != promise.list_boot) walk_gone(&promise, true);
				dtc_flow_state(f, &state, at);
				model_state(&model, &state, at);
				told_state = true;
			}
			what = "state, any";
		}
		else if(operation < 854)
		{
			dtc_flow_init(f);
			memset(&model, 0, sizeof(model));
			memset(&promise, 0, sizeof(promise));
			answer_due = false;
			what = "init";
		}
		else if(operation < 857)
		{
			// The result of whatever is under way cannot be had
			dtc_flow_no_result(f);
			model_no_result(&model);
			if(f->phase != phase_before) result->no_results++;
			what = "no result";
		}

		// After every call: the same phase, the same reason of a failure, the same request waiting to be taken,
		// the same list, the same reasons against a read and a clear, the same time left
		same = same && f->phase == model_phase(&model) && f->to_send == model_waiting(&model);
		same = same && (f->phase != DTC_FLOW_FAILED || strcmp(f->reason, model.why) == 0);
		same = same && f->read_seq == model.list_number && f->list_count == model.list_codes && f->list_end_ms == model.list_ended && (f->read_seq != 0) == model.list;
		same = same && dtc_flow_read_block(f, c, &walk_values, listed, at) == model_block(&model, c, &walk_values, listed, false, false, at);
		same = same && dtc_flow_clear_block(f, c, &walk_values, listed, false, at) == model_block(&model, c, &walk_values, listed, true, false, at);
		same = same && dtc_flow_clear_block(f, c, &walk_values, listed, true, at) == model_block(&model, c, &walk_values, listed, true, true, at);
		same = same && dtc_flow_seconds_left(f, at) == model_seconds_left(&model, at);
		for(i = 0; i < 3 && same; i++)
		{
			uint64_t asked = model.list_ended + (i == 0 ? walk_random(700000) : i == 1 ? 599001 : 600000 - walk_random(2));

			same = dtc_flow_seconds_left(f, asked) == model_seconds_left(&model, asked);
		}

		result->calls++;
		result->phases[model_phase(&model)]++;
		result->read_blocks[model_block(&model, c, &walk_values, listed, false, false, at)]++;
		result->clear_blocks[model_block(&model, c, &walk_values, listed, true, walk_random(8) == 0, at)]++;
		if(model.shown == SHOWN_LIST && own_before == OWN_READ && model.own == OWN_NONE) result->lists++;
		if(model.shown == SHOWN_LIST && own_before == OWN_CLEAR && model.own == OWN_NONE) result->back_to_list++;
		if(model.shown == SHOWN_CLEARED && shown_before != SHOWN_CLEARED) result->cleared++;
		if(model.shown == SHOWN_UNKNOWN && shown_before != SHOWN_UNKNOWN) result->unknown++;
		if(own_before != OWN_NONE && stage_before == STAGE_SILENT && model.own != OWN_NONE && model.stage == STAGE_ACCEPTED) result->adopted++;
		// A request that was never sent and is not sent any more, and one that was given its time
		if(own_before != OWN_NONE && stage_before == STAGE_WAITING && model.own == OWN_NONE)
		{
			if(told_lost && own_before == OWN_READ) result->lost_unsent_reads++;
			if(told_lost && own_before == OWN_CLEAR) result->lost_unsent_clears++;
			if(told_state) result->restart_unsent++;
		}
		if(told_state && own_before != OWN_NONE && stage_before == STAGE_ACCEPTED && model.own == OWN_NONE && at > since_before && at - since_before > 180000)
		{
			if(own_before == OWN_READ && model.shown == SHOWN_FAILED && strcmp(model.why, "no_answer") == 0) result->waited_out_reads++;
			if(own_before == OWN_CLEAR && model.shown == SHOWN_UNKNOWN) result->waited_out_clears++;
		}
		if(model.shown == SHOWN_FAILED && own_before != OWN_NONE && model.own == OWN_NONE)
		{
			if(strcmp(model.why, "no_answer") == 0) result->no_answer++;
			else if(strcmp(model.why, "restarted") == 0) result->restarted++;
			else if(strcmp(model.why, "superseded") == 0) result->superseded++;
			else if(stage_before == STAGE_POSTING) result->refused++;
			else if(strcmp(model.why, "no_result") != 0) result->scan_errors++;
		}

		if(!same && !differed)
		{
			printf("  walk %lu, call %d (%s) at %llu ms: module and model differ; phase %d and %d, to send %d and %d, list %lu and %lu, "
			       "read block %d and %d, clear block %d and %d, reason '%s' and '%s'\n",
			       (unsigned long)seed, call, what, (unsigned long long)at, (int)f->phase, (int)model_phase(&model), (int)f->to_send, (int)model_waiting(&model),
			       (unsigned long)f->read_seq, (unsigned long)model.list_number,
			       (int)dtc_flow_read_block(f, c, &walk_values, listed, at), (int)model_block(&model, c, &walk_values, listed, false, false, at),
			       (int)dtc_flow_clear_block(f, c, &walk_values, listed, false, at), (int)model_block(&model, c, &walk_values, listed, true, false, at),
			       f->reason, model.why);
			differed = true;
		}
	}
	return !differed;
}

static void test_walk(void)
{
	static const char with_rpm[] = "{\"ENGINE_RPM\":{\"class\":\"frequency\",\"unit\":\"RPM\"},\"COOLANT_TMP\":{\"class\":\"temperature\",\"unit\":\"°C\"}}";
	static const char without_rpm[] = "{\"COOLANT_TMP\":{\"class\":\"temperature\",\"unit\":\"°C\"}}";
	walk_result_t result;
	int ends[2];
	int status = -1;
	bool complete = false;
	bool every_phase = true, every_read_block = true, every_clear_block = true;
	pid_t child;
	int i;

	catalog_init(&walk_with_rpm);
	catalog_init(&walk_without_rpm);
	check(catalog_apply_config(&walk_with_rpm, with_rpm, strlen(with_rpm), work, CATALOG_TOKENS) &&
	      catalog_apply_config(&walk_without_rpm, without_rpm, strlen(without_rpm), work, CATALOG_TOKENS), "the two catalogues of the walk are loaded");

	memset(&result, 0, sizeof(result));
	fflush(stdout);
	if(pipe(ends) != 0)
	{
		check(false, "the walk has a pipe for its result");
		return;
	}

	// In a child process: a crash or a hang of the module is then a failed check here, not the end of the test
	alarm(240);
	child = fork();
	if(child == 0)
	{
		uint32_t seed;

		close(ends[0]);
		alarm(120);
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

	printf("  walk: %ld calls; promises broken: %ld once, %ld list, %ld time, %ld offer, %ld outcome; handed out %ld reads and %ld clears; %ld lists, %ld cleared, "
	       "%ld adopted without an answer, %ld back to the list, %ld no_answer, %ld restarted, %ld superseded, %ld refused, %ld scan errors, %ld unknown\n",
	       result.calls, result.broken[PROMISE_ONCE], result.broken[PROMISE_LIST], result.broken[PROMISE_TIME], result.broken[PROMISE_OFFER], result.broken[PROMISE_OUTCOME],
	       result.reads_out, result.clears_out, result.lists, result.cleared, result.adopted, result.back_to_list, result.no_answer,
	       result.restarted, result.superseded, result.refused, result.scan_errors, result.unknown);
	printf("  walk: %ld steps back, %ld takes without a place for the number, %ld states that are NULL, %ld results ignored, %ld states told late, "
	       "%ld clears allowed in the last second of their list, %ld handed out in its last 2 ms, %ld not handed out because they waited too long\n",
	       result.steps_back, result.without_seq, result.without_state, result.ignored_results, result.late_told, result.last_moment, result.last_moment_out,
	       result.too_late);
	printf("  walk: %ld reads and %ld clears taken back when the adapter was lost before they were sent, %ld requests when it restarted; %ld reads and %ld clears "
	       "given up after their time; %ld results that cannot be had; %ld reads allowed with an engine speed that is not fresh\n",
	       result.lost_unsent_reads, result.lost_unsent_clears, result.restart_unsent, result.waited_out_reads, result.waited_out_clears, result.no_results,
	       result.old_speeds);
	printf("  walk: phases");
	for(i = 0; i <= DTC_FLOW_UNKNOWN; i++)
	{
		printf(" %ld", result.phases[i]);
		if(result.phases[i] < 2000) every_phase = false;
	}
	printf("; reasons against a read");
	for(i = 0; i <= DTC_FLOW_RPM_UNKNOWN; i++)
	{
		printf(" %ld", result.read_blocks[i]);
		if(result.read_blocks[i] < 2000) every_read_block = false;
	}
	printf("; against a clear");
	for(i = 0; i <= DTC_FLOW_BUTTON_STUCK; i++)
	{
		printf(" %ld", result.clear_blocks[i]);
		if(result.clear_blocks[i] < 200) every_clear_block = false;
	}
	printf("\n");

	check(complete && status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0,
	      "80 random walks of 40000 calls each: no crash and no hang, also without a state, a reason or a place for the number");
	check(complete && result.different == 0 && result.calls == (long)WALKS * WALK_CALLS, "80 random walks of 40000 calls each: module and model agree after every call");
	// Each promise on its own, so that a broken one is named. That the walk gets where a promise can break
	// is checked below.
	check(complete && result.broken[PROMISE_ONCE] == 0, "in the walk a read or a clear is handed out once per call that allowed it, never again, and never after it was withdrawn");
	check(complete && result.broken[PROMISE_LIST] == 0,
	      "in the walk a clear is allowed and handed out only for the list of an own read the adapter shows as its last request, with its number, never after a restart");
	check(complete && result.broken[PROMISE_TIME] == 0, "in the walk no clear is allowed or handed out later than 600000 ms after its read ended");
	check(complete && result.broken[PROMISE_OFFER] == 0, "in the walk a read is allowed only with the engine off, a clear only with the engine off since its read ended and the switch free");
	check(complete && result.broken[PROMISE_OUTCOME] == 0, "in the walk a list and the outcome of a clear only come from the result of a request of that kind that was handed out");
	check(complete && result.reads_out > 2000 && result.clears_out > 400 && result.last_moment > 30 && result.last_moment_out > 30 && result.too_late > 30,
	      "the walk hands out reads and clears, allows clears in the last second of a list, hands them out in its last 2 ms and withholds those that waited too long, in numbers");
	check(complete && every_phase && every_read_block && every_clear_block, "the walk reaches every phase and every reason against a read and a clear in numbers");
	check(complete && result.lists > 500 && result.cleared > 100 && result.adopted > 100 && result.back_to_list > 30 && result.no_answer > 100 && result.restarted > 30 &&
	      result.superseded > 100 && result.refused > 100 && result.scan_errors > 100 && result.unknown > 100,
	      "the walk reaches lists, clears, requests found without an answer, clears that did not arrive, and every kind of failure in numbers");
	check(complete && result.steps_back > 10000 && result.without_seq > 1000 && result.without_state > 1000 && result.ignored_results > 10000 && result.late_told > 1000,
	      "the walk reaches steps back of the time, missing arguments, ignored results and states told late in numbers");
	check(complete && result.lost_unsent_reads > 100 && result.lost_unsent_clears > 30 && result.restart_unsent > 100,
	      "the walk reaches reads and clears that are taken back before they were sent, by a lost adapter and by a restart, in numbers");
	check(complete && result.waited_out_reads > 100 && result.waited_out_clears > 30 && result.no_results > 100 && result.old_speeds > 100,
	      "the walk reaches requests given up after their time, results that cannot be had and reads allowed with an engine speed that is not fresh in numbers");
}

int main(void)
{
	test_constants();
	test_start();
	test_good_path();
	test_read_blocks();
	test_clear_blocks();
	test_block_order();
	test_engine();
	test_list_time();
	test_posted();
	test_no_answer();
	test_own_scan();
	test_result();
	test_restart();
	test_foreign_scan();
	test_lost();
	test_never_sent();
	test_wait();
	test_no_result();
	test_dismiss();
	test_read_again();
	test_waiting();
	test_adapter_rules();
	test_large_numbers();
	test_walk();
	return test_end();
}
