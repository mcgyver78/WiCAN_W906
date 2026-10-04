/*
 * Host test for display/components/core/conn.c. Run "make test_conn && ./test_conn" in display/test.
 * redproof.py removes or weakens every rule once (mutations/conn.py) and expects this test to fail.
 *
 * The scenes below use the same times: the display joins the network at 5000, so the first round begins
 * at 5000 and the grace time ends at 20000. Unless a scene says otherwise every answer comes at once, at
 * the time its request was handed out.
 */
#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include <sys/wait.h>
#include "test.h"
#include "conn.h"

#define OWN         "a1b2c3d4e5f6"
#define OTHER       "0123456789ab"
#define LONGEST     "0123456789abcdef0123456789abcdef"  // 32 bytes, as many as an id can have
#define LONGEST_TOO "0123456789abcdef0123456789abcdeF"  // the same but for its last byte
#define ODD         " \xC3\xA4 b"                         // begins with a space, has a letter of two bytes
// What a later firmware might answer: a state of AutoPID that is none of the three
#define UNKNOWN_AUTOPID ((wican_autopid_t)3)
// 2^32 ms, 49.7 days after the display started: where a time counted in 32 bit begins anew
#define DAYS_49     4294967296ull

static conn_t conn;

typedef struct
{
	conn_got_t state, result, catalog, values;
} answers_t;

static const answers_t ALL_OK = {CONN_GOT_OK, CONN_GOT_OK, CONN_GOT_OK, CONN_GOT_OK};

// An adapter with AutoPID running, the ignition on, up for 100 s, 35 values and nothing in the fault memory state
static wican_state_t adapter(const char *id, uint32_t boot)
{
	wican_state_t state;

	memset(&state, 0, sizeof(state));
	strcpy(state.id, id);
	state.boot = boot;
	state.up_s = 100;
	state.autopid = WICAN_AUTOPID_RUN;
	state.pids = 35;
	state.ecu_online = true;
	state.dtc.supported = true;
	return state;
}

// The same adapter with its last scan in `phase`, number `seq`, and a stored result with number `result_seq`
static wican_state_t scan(wican_state_t state, wican_dtc_phase_t phase, uint32_t seq, uint32_t result_seq)
{
	state.dtc.phase = phase;
	state.dtc.has_request = true;
	state.dtc.from_http = true;
	state.dtc.seq = seq;
	state.dtc.result_seq = result_seq;
	return state;
}

// A display bound to `bound` that joined a network at 5000
static void join(const char *bound)
{
	conn_init(&conn, bound);
	conn_wifi(&conn, true, 5000);
}

// The round that is due at `now`, every request answered at once as `answers` says. Returns the requests
// in the order they were handed out, one letter each: S state, R result, C catalogue, V values. An empty
// text if nothing was due.
static const char *round_with(const wican_state_t *state, answers_t answers, uint64_t now)
{
	static char letters[12];
	size_t count = 0;

	while(count + 1 < sizeof(letters))
	{
		conn_ask_t ask = conn_next(&conn, now);

		if(ask == CONN_ASK_STATE) conn_got_state(&conn, answers.state, state, now);
		else if(ask == CONN_ASK_RESULT) conn_got_result(&conn, answers.result, now);
		else if(ask == CONN_ASK_CATALOG) conn_got_catalog(&conn, answers.catalog, now);
		else if(ask == CONN_ASK_VALUES) conn_got_values(&conn, answers.values, now);
		else break;
		letters[count++] = "?SRCV"[ask];
	}
	letters[count] = '\0';
	return letters;
}

static bool round_is(wican_state_t state, uint64_t now, const char *expected)
{
	return strcmp(round_with(&state, ALL_OK, now), expected) == 0;
}

// A round that begins at `now` and whose state is not answered. false if no round was due.
static bool failed_round(uint64_t now)
{
	if(conn_next(&conn, now) != CONN_ASK_STATE) return false;
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, now);
	return true;
}

// The view after one answered round with this state, on a fresh connection of a display bound to OWN
static conn_view_t view_of(wican_state_t state)
{
	join(OWN);
	round_with(&state, ALL_OK, 5000);
	return conn_view(&conn, 5500);
}

static void test_constants(void)
{
	static const uint32_t waits[] = CONN_BACKOFF_MS;

	check(CONN_ROUND_MS == 1000 && CONN_GRACE_MS == 15000 && CONN_FAILED_ROUNDS == 3 && CONN_NO_API_RECHECK_MS == 30000,
	      "a round every 1000 ms, 15000 ms grace, 3 failed rounds until no answer, the API asked for again after 30000 ms");
	check(CONN_DTC_MIN_UP_S == 15 && CONN_DTC_MIN_ROUNDS == 2, "fault memory commands after 15 s up and 2 answered rounds");
	check(sizeof(waits) / sizeof(waits[0]) == 4 && waits[0] == 1000 && waits[1] == 2000 && waits[2] == 5000 && waits[3] == 10000,
	      "the waits after failed rounds are 1, 2, 5 and 10 s");
}

static void test_start(void)
{
	wican_state_t before = adapter(OTHER, 5);
	wican_state_t other = scan(adapter(OTHER, 5), WICAN_DTC_DONE, 7, 7);
	wican_state_t own = adapter(OWN, 77);
	char id[40] = "untouched";

	// Whatever stood in the memory before: a display bound to another adapter in the middle of a round, with
	// news to take
	join(OTHER);
	round_with(&before, ALL_OK, 5000);
	other.boot = 6;
	conn_next(&conn, 6000);
	conn_got_state(&conn, CONN_GOT_OK, &other, 6000);
	conn_next(&conn, 6000);
	conn.bind_pending = true;
	conn_init(&conn, NULL);
	check(conn_view(&conn, 0) == CONN_VIEW_NO_WIFI && conn_view(&conn, 100000) == CONN_VIEW_NO_WIFI,
	      "after the start the display is in no network, whatever stood in the memory");
	check(conn_next(&conn, 0) == CONN_ASK_NOTHING && conn_next(&conn, 100000) == CONN_ASK_NOTHING, "without WiFi nothing is asked, at no time");
	check(conn_state(&conn) == NULL && !conn_dtc_allowed(&conn), "after the start there is no state and no fault memory command is allowed");
	check(!conn_take_restarted(&conn) && !conn_take_bind(&conn, id, sizeof(id)) && strcmp(id, "untouched") == 0,
	      "after the start there is no restart and no id to take");

	conn_wifi(&conn, true, 5000);
	check(conn_view(&conn, 5000) == CONN_VIEW_CONNECTING && conn_state(&conn) == NULL && conn_next(&conn, 5000) == CONN_ASK_STATE,
	      "joining after the start: nothing is known, the round begins with the state, whatever stood in the memory");
	conn_got_state(&conn, CONN_GOT_OK, &own, 5000);
	check(conn_view(&conn, 5000) == CONN_VIEW_LIVE && conn_take_bind(&conn, id, sizeof(id)) && strcmp(id, OWN) == 0 && !conn_take_restarted(&conn),
	      "a display started without an id is not bound, whatever stood in the memory: the first adapter is accepted, binds, and is no restart");
	check(conn_next(&conn, 5000) == CONN_ASK_CATALOG && !conn_dtc_allowed(&conn), "the first round after the start asks for the catalogue and counts as the first");

	join(OTHER);
	round_with(&before, ALL_OK, 5000);
	conn_init(&conn, OWN);
	check(conn_view(&conn, 0) == CONN_VIEW_NO_WIFI && conn_next(&conn, 0) == CONN_ASK_NOTHING && conn_state(&conn) == NULL &&
	      !conn_take_restarted(&conn) && !conn_take_bind(&conn, id, sizeof(id)), "a bound display starts the same way");
	conn_wifi(&conn, true, 5000);
	check(round_is(own, 5000, "SCV") && conn_view(&conn, 5000) == CONN_VIEW_LIVE && round_is(before, 6000, "S") && conn_view(&conn, 6000) == CONN_VIEW_FOREIGN,
	      "a display started with an id is bound to that adapter, whatever it was bound to before");
}

static void test_round(void)
{
	wican_state_t state = scan(adapter(OWN, 77), WICAN_DTC_DONE, 7, 7);

	join(OWN);
	check(conn_view(&conn, 5000) == CONN_VIEW_CONNECTING, "in a network, no answer yet: connecting");
	check(conn_next(&conn, 5000) == CONN_ASK_STATE, "the first round is due at once when the WiFi comes up, and a round begins with the state");
	check(conn_next(&conn, 5000) == CONN_ASK_NOTHING && conn_next(&conn, 9000) == CONN_ASK_NOTHING,
	      "a request is handed out once: nothing while it is under way, however long that takes");
	conn_got_state(&conn, CONN_GOT_OK, &state, 5100);
	check(conn_next(&conn, 5100) == CONN_ASK_RESULT, "behind the state: the result it names");
	check(conn_next(&conn, 5100) == CONN_ASK_NOTHING, "the result is handed out once");
	conn_got_result(&conn, CONN_GOT_OK, 5200);
	check(conn_next(&conn, 5200) == CONN_ASK_CATALOG, "behind the result: the catalogue");
	check(conn_next(&conn, 5200) == CONN_ASK_NOTHING, "the catalogue is handed out once");
	conn_got_catalog(&conn, CONN_GOT_OK, 5300);
	check(conn_next(&conn, 5300) == CONN_ASK_VALUES, "behind the catalogue: the values");
	check(conn_next(&conn, 5300) == CONN_ASK_NOTHING, "the values are handed out once");
	conn_got_values(&conn, CONN_GOT_OK, 5400);
	check(conn_next(&conn, 5400) == CONN_ASK_NOTHING, "behind the values the round is over");
	check(conn_view(&conn, 5400) == CONN_VIEW_LIVE, "AutoPID runs, the ignition is on, no scan: live");

	join(OWN);
	check(conn_next(&conn, 4000) == CONN_ASK_STATE, "the first round is due at once, also for a time before the one the WiFi came up");
}

static void test_round_timing(void)
{
	wican_state_t state = adapter(OWN, 77);

	join(OWN);
	conn_next(&conn, 5000);
	conn_got_state(&conn, CONN_GOT_OK, &state, 5100);
	conn_next(&conn, 5100);
	conn_got_catalog(&conn, CONN_GOT_OK, 5300);
	conn_next(&conn, 5300);
	conn_got_values(&conn, CONN_GOT_OK, 5400);
	check(conn_next(&conn, 5400) == CONN_ASK_NOTHING && conn_next(&conn, 5999) == CONN_ASK_NOTHING, "999 ms after the start of a round the next one is not due");
	check(conn_next(&conn, 6000) == CONN_ASK_STATE, "the next round begins 1000 ms after the start of the last, not after its end");

	conn_got_state(&conn, CONN_GOT_OK, &state, 6200);
	conn_next(&conn, 6200);
	conn_got_values(&conn, CONN_GOT_OK, 7500);
	check(conn_next(&conn, 7500) == CONN_ASK_STATE, "a round that took 1500 ms: the next one begins at once");

	conn_got_state(&conn, CONN_GOT_OK, &state, 7600);
	conn_next(&conn, 7600);
	conn_got_values(&conn, CONN_GOT_OK, 8500);
	check(conn_next(&conn, 8500) == CONN_ASK_STATE, "a round that took exactly 1000 ms: the next one begins at once");

	conn_got_state(&conn, CONN_GOT_OK, &state, 8500);
	conn_next(&conn, 8500);
	conn_got_values(&conn, CONN_GOT_OK, 8600);
	check(conn_next(&conn, 9499) == CONN_ASK_NOTHING && conn_next(&conn, 9500) == CONN_ASK_STATE,
	      "after a round that began late the next one is due 1000 ms after that start: rounds are not made up for");

	join(OWN);
	round_with(&state, ALL_OK, 5000);
	check(conn_next(&conn, 4000) == CONN_ASK_NOTHING && conn_next(&conn, 0) == CONN_ASK_NOTHING, "a time before the start of the round: no time passed, nothing is due");
	check(conn_next(&conn, 6000) == CONN_ASK_STATE, "the round after a step back of the time is due at the usual time");

	conn_init(&conn, OWN);
	conn_wifi(&conn, true, DAYS_49 - 500);
	check(round_is(state, DAYS_49 - 500, "SCV"), "the scene: a round that begins 500 ms before 2^32 ms");
	check(conn_next(&conn, DAYS_49 - 1) == CONN_ASK_NOTHING && conn_next(&conn, DAYS_49 + 499) == CONN_ASK_NOTHING && conn_next(&conn, DAYS_49 + 500) == CONN_ASK_STATE,
	      "the round after it is due 1000 ms later, beyond 2^32 ms, and not before");
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, DAYS_49 + 600);
	check(conn_next(&conn, DAYS_49 + 1599) == CONN_ASK_NOTHING && conn_next(&conn, DAYS_49 + 1600) == CONN_ASK_STATE, "the wait after a failed round beyond 2^32 ms is the usual one");
}

static void test_result(void)
{
	wican_state_t none = adapter(OWN, 77);
	wican_state_t seven = scan(none, WICAN_DTC_DONE, 7, 7);
	wican_state_t eight = scan(none, WICAN_DTC_DONE, 8, 8);
	answers_t answers = ALL_OK;

	join(OWN);
	check(round_is(none, 5000, "SCV"), "a state without a result (number 0): no result is asked for");
	check(round_is(seven, 6000, "SRV"), "a state that names a result: it is fetched");
	check(round_is(seven, 7000, "SV") && round_is(seven, 8000, "SV"), "a result is fetched once per number");
	check(round_is(eight, 9000, "SRV"), "a state that names the next result: that one is fetched");
	check(round_is(eight, 10000, "SV"), "the next result is fetched once as well");
	check(round_is(none, 11000, "SV") && round_is(eight, 12000, "SV"), "a state without a result in between does not make a fetched result new");
	check(failed_round(13000) && round_is(eight, 14000, "SV"), "a failed round does not make a fetched result new, nor the catalogue");

	join(OWN);
	round_with(&none, ALL_OK, 5000);
	answers.result = CONN_GOT_NOT_FOUND;
	check(strcmp(round_with(&seven, answers, 6000), "SRV") == 0, "the adapter has no result (204 or 404): the round goes on");
	check(round_is(seven, 7000, "SV"), "a result the adapter did not have counts as fetched");
	check(conn_dtc_allowed(&conn), "a round with a result the adapter did not have has not failed");

	join(OWN);
	round_with(&none, ALL_OK, 5000);
	answers.result = CONN_GOT_FAILED;
	check(strcmp(round_with(&seven, answers, 6000), "SR") == 0, "a result that failed ends the round: the values behind it are not asked for");
	check(conn_next(&conn, 6999) == CONN_ASK_NOTHING && round_is(seven, 7000, "SRV"), "a result that failed is asked for again in the next round");
	check(round_is(seven, 8000, "SV"), "the result fetched at the second attempt is not fetched again");

	join(OWN);
	round_with(&none, ALL_OK, 5000);
	check(round_is(seven, 6000, "SRV") && round_is(scan(none, WICAN_DTC_DONE, 7 + 65536, 7 + 65536), 7000, "SRV"), "a result number that differs by 65536 names another result: it is fetched");
	check(round_is(scan(none, WICAN_DTC_DONE, 7 + 65536, 7 + 65536), 8000, "SV"), "the result with the number above 65536 is fetched once");
	check(round_is(scan(none, WICAN_DTC_DONE, 65536, 65536), 9000, "SRV"), "65536 is a result number like any other: it is fetched");
	seven.dtc.from_http = false;
	seven.dtc.result_seq = 9;
	check(round_is(seven, 10000, "SRV") && round_is(seven, 11000, "SV"), "the result of a request over MQTT is fetched like any other, once");
	seven.dtc.count = 0;
	seven.dtc.result_seq = 10;
	seven.dtc.seq = 12;
	check(round_is(seven, 12000, "SRV"), "a result without trouble codes whose number is not the one of the last request is fetched as well");

	join(OWN);
	round_with(&none, ALL_OK, 5000);
	check(round_is(scan(none, WICAN_DTC_RUNNING, 8, 7), 6000, "SR"), "a result not fetched yet is fetched also while the next scan runs");
	check(round_is(scan(none, WICAN_DTC_ERROR, 8, 7), 7000, "SV"), "the result that outlasts an error of the next scan was fetched: not again");
}

static void test_catalog(void)
{
	wican_state_t state = adapter(OWN, 77);
	wican_state_t more = adapter(OWN, 77);
	answers_t answers = ALL_OK;

	join(OWN);
	check(round_is(state, 5000, "SCV"), "the first round of a connection asks for the catalogue");
	check(round_is(state, 6000, "SV") && round_is(state, 7000, "SV"), "the catalogue is fetched once per connection");
	more.pids = 36;
	check(round_is(more, 8000, "SCV"), "the number of values changed: the catalogue is asked for again");
	check(round_is(more, 9000, "SV"), "the catalogue of the changed number of values is fetched once");
	check(round_is(state, 10000, "SCV"), "the number of values changed back: the catalogue is asked for again");
	more.pids = 35 + 256;
	check(round_is(more, 11000, "SCV") && round_is(more, 12000, "SV"), "the number of values changed by 256: the catalogue is asked for again");
	more.pids = 0;
	check(round_is(more, 13000, "SCV") && round_is(more, 14000, "SV"), "the number of values dropped to 0: the catalogue is asked for again, the values as before");
	check(round_is(state, 15000, "SCV"), "the number of values is back from 0: the catalogue is asked for again");
	check(!conn_take_restarted(&conn), "another number of values is no restart");

	join(OWN);
	state.autopid = WICAN_AUTOPID_STARTING;
	state.pids = 0;
	check(round_is(state, 5000, "SC"), "the catalogue is asked for whatever AutoPID does");
	state.autopid = WICAN_AUTOPID_OFF;
	join(OWN);
	check(round_is(state, 5000, "SC"), "the catalogue is asked for with AutoPID off as well");

	state = adapter(OWN, 77);
	join(OWN);
	check(round_is(scan(state, WICAN_DTC_QUEUED, 7, 0), 5000, "S"), "a scan is queued: the catalogue is not asked for");
	check(round_is(scan(state, WICAN_DTC_RUNNING, 7, 0), 6000, "S"), "a scan runs: the catalogue is not asked for");
	check(round_is(scan(state, WICAN_DTC_DONE, 7, 7), 7000, "SRCV"), "the scan is over: the catalogue that waited is asked for");
	check(round_is(scan(state, WICAN_DTC_DONE, 7, 7), 8000, "SV"), "the catalogue that waited for the scan is fetched once");
	join(OWN);
	check(round_is(scan(state, WICAN_DTC_ERROR, 7, 0), 5000, "SCV"), "a scan that ended with an error does not hold the catalogue back");

	join(OWN);
	answers.catalog = CONN_GOT_NOT_FOUND;
	check(strcmp(round_with(&state, answers, 5000), "SCV") == 0, "the adapter has no catalogue (404): the round goes on with the values");
	check(strcmp(round_with(&state, answers, 6000), "SCV") == 0 && strcmp(round_with(&state, answers, 7000), "SCV") == 0,
	      "a catalogue the adapter did not have is asked for again in the next round");
	check(conn_dtc_allowed(&conn) && conn_view(&conn, 30000) == CONN_VIEW_LIVE, "rounds with a catalogue the adapter did not have have not failed");
	check(round_is(state, 8000, "SCV") && round_is(state, 9000, "SV"), "the catalogue that arrives at last is not asked for again");

	join(OWN);
	answers.catalog = CONN_GOT_FAILED;
	check(strcmp(round_with(&state, answers, 5000), "SC") == 0, "a catalogue that failed ends the round: the values behind it are not asked for");
	check(conn_next(&conn, 5999) == CONN_ASK_NOTHING && strcmp(round_with(&state, answers, 6000), "SC") == 0,
	      "a catalogue that failed is asked for again in the next round");
	check(!conn_dtc_allowed(&conn), "a round with a catalogue that failed has failed");
	check(conn_next(&conn, 7999) == CONN_ASK_NOTHING && round_is(state, 8000, "SCV"), "the second failed catalogue is followed by a wait of 2000 ms");
}

static void test_values(void)
{
	wican_state_t state = adapter(OWN, 77);
	wican_state_t other = state;
	answers_t answers = ALL_OK;

	join(OWN);
	round_with(&state, ALL_OK, 5000);
	check(round_is(state, 6000, "SV"), "AutoPID runs, the ignition is on, no scan: the values are asked for");
	other.autopid = WICAN_AUTOPID_STARTING;
	check(round_is(other, 7000, "S"), "AutoPID is starting: no values");
	other.autopid = WICAN_AUTOPID_OFF;
	check(round_is(other, 8000, "S"), "AutoPID is off: no values");
	other = state;
	other.ecu_online = false;
	check(round_is(other, 9000, "S"), "the ECU is offline: no values");
	check(round_is(scan(state, WICAN_DTC_QUEUED, 7, 0), 10000, "S"), "a scan is queued: no values");
	check(round_is(scan(state, WICAN_DTC_RUNNING, 7, 0), 11000, "S"), "a scan runs: no values");
	check(round_is(scan(state, WICAN_DTC_ERROR, 7, 0), 12000, "SV"), "a scan that ended with an error: values again");
	check(round_is(scan(state, WICAN_DTC_DONE, 8, 8), 13000, "SRV"), "a scan that is done: values again");
	other = state;
	other.dtc.supported = false;
	other.up_s = 3;
	check(round_is(other, 14000, "SV"), "a profile without fault memory, an adapter that is up for 3 s: the values are asked for all the same");
	check(round_is(scan(other, WICAN_DTC_ERROR, 9, 8), 15000, "SV") && round_is(scan(other, WICAN_DTC_ERROR, 10, 9), 16000, "SRV"),
	      "a result is fetched whatever the profile says about the fault memory");
	other = state;
	other.autopid = UNKNOWN_AUTOPID;
	check(round_is(other, 17000, "S") && round_is(state, 18000, "SV"), "an AutoPID state the display does not know: no values until it runs again");

	join(OWN);
	round_with(&state, ALL_OK, 5000);
	round_with(&state, ALL_OK, 6000);
	answers.values = CONN_GOT_FAILED;
	check(conn_dtc_allowed(&conn) && strcmp(round_with(&state, answers, 7000), "SV") == 0 && !conn_dtc_allowed(&conn), "values that failed: the round has failed");
	check(conn_next(&conn, 7999) == CONN_ASK_NOTHING && round_is(state, 8000, "SV"), "the round after failed values begins 1000 ms after them");

	join(OWN);
	round_with(&state, ALL_OK, 5000);
	round_with(&state, ALL_OK, 6000);
	answers.values = CONN_GOT_NOT_FOUND;
	check(strcmp(round_with(&state, answers, 7000), "SV") == 0 && !conn_dtc_allowed(&conn), "a 404 for the values counts as failed");
	round_with(&state, answers, 8000);
	check(conn_next(&conn, 9999) == CONN_ASK_NOTHING && conn_next(&conn, 10000) == CONN_ASK_STATE, "a second 404 for the values is followed by the wait of a second failed round");
}

static void test_backoff(void)
{
	wican_state_t state = adapter(OWN, 77);
	int wrong = 0;
	int i;

	join(OWN);
	conn_next(&conn, 5000);
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 5200);
	check(conn_next(&conn, 5200) == CONN_ASK_NOTHING && conn_next(&conn, 6199) == CONN_ASK_NOTHING, "999 ms after the end of the first failed round: nothing is due");
	check(conn_next(&conn, 6200) == CONN_ASK_STATE, "the round after the first failed one begins 1000 ms after its end, not after its start");
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 6300);
	check(conn_next(&conn, 8299) == CONN_ASK_NOTHING && conn_next(&conn, 8300) == CONN_ASK_STATE, "after the second failed round in a row: 2000 ms");
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 8300);
	check(conn_next(&conn, 13299) == CONN_ASK_NOTHING && conn_next(&conn, 13300) == CONN_ASK_STATE, "after the third: 5000 ms");
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 13300);
	check(conn_next(&conn, 23299) == CONN_ASK_NOTHING && conn_next(&conn, 23300) == CONN_ASK_STATE, "after the fourth: 10000 ms");
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 23300);
	check(conn_next(&conn, 33299) == CONN_ASK_NOTHING && conn_next(&conn, 33300) == CONN_ASK_STATE, "after the fifth: 10000 ms again");
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 33300);
	check(conn_next(&conn, 43299) == CONN_ASK_NOTHING && conn_next(&conn, 43300) == CONN_ASK_STATE, "after the sixth: 10000 ms each time");

	conn_got_state(&conn, CONN_GOT_OK, &state, 43300);
	check(conn_next(&conn, 43300) == CONN_ASK_CATALOG, "a round after failed ones goes on as every round");
	conn_got_catalog(&conn, CONN_GOT_OK, 43300);
	conn_next(&conn, 43300);
	conn_got_values(&conn, CONN_GOT_OK, 43300);
	check(conn_next(&conn, 44299) == CONN_ASK_NOTHING && conn_next(&conn, 44300) == CONN_ASK_STATE, "after an answered round the next one is due 1000 ms after its start");
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 44500);
	check(conn_next(&conn, 45499) == CONN_ASK_NOTHING && conn_next(&conn, 45500) == CONN_ASK_STATE,
	      "an answered round resets the count: the next failed round is followed by 1000 ms again");

	join(OWN);
	check(failed_round(5000) && failed_round(6000) && conn_next(&conn, 8000) == CONN_ASK_STATE, "the scene: two failed rounds, the third round under way");
	conn_got_state(&conn, (conn_got_t)3, &state, 8000);
	check(conn_next(&conn, 12999) == CONN_ASK_NOTHING && conn_next(&conn, 13000) == CONN_ASK_STATE && conn_state(&conn) == NULL,
	      "an outcome that is none of the three counts as failed, whatever state comes with it");

	join(OWN);
	conn_next(&conn, 5000);
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 3000);
	check(conn_next(&conn, 5999) == CONN_ASK_NOTHING && conn_next(&conn, 6000) == CONN_ASK_STATE,
	      "a failed round that ended with a time before its start ended at its start: the wait counts from there");

	join(OWN);
	conn_next(&conn, 5000);
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 5000 + DAYS_49 + 200);
	check(conn_next(&conn, DAYS_49 + 6199) == CONN_ASK_NOTHING && conn_next(&conn, DAYS_49 + 6200) == CONN_ASK_STATE,
	      "a request that failed 2^32 ms and 200 ms after its round began: the wait counts from that end");

	join(OWN);
	check(failed_round(5000) && failed_round(6000) && failed_round(8000), "the scene: three failed rounds, the next one is due at 13000");
	for(i = 0; i < 600; i++)
	{
		uint64_t due = 13000 + (uint64_t)i * 10000;

		// The grace time ends at 20000
		if(conn_next(&conn, due - 1) != CONN_ASK_NOTHING || !failed_round(due) || conn_view(&conn, due) != (i == 0 ? CONN_VIEW_CONNECTING : CONN_VIEW_NO_ANSWER)) wrong++;
	}
	check(wrong == 0, "600 more failed rounds: each begins 10000 ms after the last and not before, and the view is no answer from the end of the grace time on");
}

static void test_no_answer(void)
{
	wican_state_t state = adapter(OWN, 77);

	join(OWN);
	check(failed_round(5000) && failed_round(6000), "the scene: two failed rounds without any answer before");
	check(conn_view(&conn, 30000) == CONN_VIEW_CONNECTING, "two failed rounds in a row, long after the grace time: not shown as no answer");
	check(failed_round(8000) && conn_view(&conn, 8000) == CONN_VIEW_CONNECTING && conn_view(&conn, 19999) == CONN_VIEW_CONNECTING,
	      "three failed rounds in a row within the 15000 ms after the WiFi came up: still connecting");
	check(conn_view(&conn, 20000) == CONN_VIEW_NO_ANSWER && conn_view(&conn, 20001) == CONN_VIEW_NO_ANSWER, "three failed rounds in a row and the grace time over: no answer");
	check(conn_view(&conn, 4000) == CONN_VIEW_CONNECTING, "a time before the one the WiFi came up: no time passed, the grace time is not over");
	check(conn_next(&conn, 13000) == CONN_ASK_STATE && conn_view(&conn, 20000) == CONN_VIEW_NO_ANSWER, "no answer is shown also while the next request is under way");
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 13000);
	check(failed_round(23000) && conn_view(&conn, 23000) == CONN_VIEW_NO_ANSWER, "more than three failed rounds in a row: no answer");
	check(round_is(state, 33000, "SCV") && conn_view(&conn, 33000) == CONN_VIEW_LIVE, "an answered round ends no answer");
	check(failed_round(34000) && failed_round(35000) && conn_view(&conn, 36000) == CONN_VIEW_LIVE,
	      "the failed rounds are counted anew after an answered one: two more are not shown, the last state stays");
	check(failed_round(37000) && conn_view(&conn, 37000) == CONN_VIEW_NO_ANSWER, "the third failed round after the answered one: no answer at once, the grace time is long over");

	join(OWN);
	round_with(&state, ALL_OK, 5000);
	round_with(&state, ALL_OK, 6000);
	check(failed_round(7000) && failed_round(8000) && failed_round(10000) && conn_view(&conn, 19999) == CONN_VIEW_LIVE,
	      "three failed rounds after an answered one within the grace time: the view of the last state");
	check(conn_view(&conn, 20000) == CONN_VIEW_NO_ANSWER, "no answer goes before the view of a state that was answered earlier");
	check(conn_state(&conn) != NULL && conn_state(&conn)->boot == 77, "the last state stays known while the adapter does not answer");

	conn_init(&conn, OWN);
	conn_wifi(&conn, true, DAYS_49 - 1000);
	check(failed_round(DAYS_49 - 1000) && failed_round(DAYS_49) && failed_round(DAYS_49 + 2000) && conn_view(&conn, DAYS_49 + 13999) == CONN_VIEW_CONNECTING &&
	      conn_view(&conn, DAYS_49 + 14000) == CONN_VIEW_NO_ANSWER, "joined 1000 ms before 2^32 ms: the grace time ends 15000 ms later as ever");
	check(conn_view(&conn, 2 * DAYS_49 - 1000 + 5) == CONN_VIEW_NO_ANSWER && conn_view(&conn, 2 * DAYS_49 + 20000) == CONN_VIEW_NO_ANSWER,
	      "no answer stays no answer 2^32 ms after joining: the grace time does not begin anew");
	conn_wifi(&conn, false, DAYS_49 + 30000);
	conn_wifi(&conn, true, DAYS_49 + 40000);
	check(failed_round(DAYS_49 + 40000) && failed_round(DAYS_49 + 41000) && failed_round(DAYS_49 + 43000) && conn_view(&conn, DAYS_49 + 54999) == CONN_VIEW_CONNECTING &&
	      conn_view(&conn, DAYS_49 + 55000) == CONN_VIEW_NO_ANSWER, "joined after 2^32 ms: the grace time counts from that moment");

	// The grace time belongs to the network the display is in now
	join(OWN);
	conn_wifi(&conn, false, 30000);
	conn_wifi(&conn, true, 50000);
	check(failed_round(50000) && failed_round(51000) && failed_round(53000) && conn_view(&conn, 64999) == CONN_VIEW_CONNECTING &&
	      conn_view(&conn, 65000) == CONN_VIEW_NO_ANSWER, "the grace time counts from the last time the WiFi came up");
}

static void test_view(void)
{
	wican_state_t state = adapter(OWN, 77);
	wican_state_t foreign = adapter(OTHER, 5);

	check(view_of(state) == CONN_VIEW_LIVE, "AutoPID runs, ECU online, nothing scanned yet: live");
	check(view_of(scan(state, WICAN_DTC_DONE, 7, 7)) == CONN_VIEW_LIVE && view_of(scan(state, WICAN_DTC_ERROR, 7, 0)) == CONN_VIEW_LIVE, "a scan that ended: live");
	check(view_of(scan(state, WICAN_DTC_QUEUED, 7, 0)) == CONN_VIEW_SCAN, "a scan is queued: scan");
	check(view_of(scan(state, WICAN_DTC_RUNNING, 7, 0)) == CONN_VIEW_SCAN, "a scan runs: scan");

	state.ecu_online = false;
	check(view_of(state) == CONN_VIEW_ECU_OFFLINE, "the ECU is offline: ignition off");
	check(view_of(scan(state, WICAN_DTC_QUEUED, 7, 0)) == CONN_VIEW_SCAN && view_of(scan(state, WICAN_DTC_RUNNING, 7, 0)) == CONN_VIEW_SCAN,
	      "a scan with the ECU offline: the scan goes before the ignition");

	state.autopid = WICAN_AUTOPID_STARTING;
	check(view_of(state) == CONN_VIEW_STARTING && view_of(scan(state, WICAN_DTC_RUNNING, 7, 0)) == CONN_VIEW_STARTING,
	      "AutoPID starting goes before scan and ignition");
	state.ecu_online = true;
	check(view_of(state) == CONN_VIEW_STARTING, "AutoPID starting with the ECU online: starting");

	state.autopid = UNKNOWN_AUTOPID;
	check(view_of(state) == CONN_VIEW_STARTING && view_of(scan(state, WICAN_DTC_RUNNING, 7, 0)) == CONN_VIEW_STARTING,
	      "an AutoPID state the display does not know is not running: starting, before scan and ignition");
	state.ecu_online = false;
	check(view_of(state) == CONN_VIEW_STARTING, "an AutoPID state the display does not know with the ECU offline: starting");
	state.ecu_online = true;

	state.autopid = WICAN_AUTOPID_OFF;
	check(view_of(state) == CONN_VIEW_AUTOPID_OFF, "AutoPID off with the ECU online: AutoPID off");
	state.ecu_online = false;
	check(view_of(state) == CONN_VIEW_AUTOPID_OFF && view_of(scan(state, WICAN_DTC_QUEUED, 7, 0)) == CONN_VIEW_AUTOPID_OFF,
	      "AutoPID off goes before scan and ignition");

	check(view_of(foreign) == CONN_VIEW_FOREIGN, "another adapter than the one the display is bound to: foreign");
	foreign.autopid = WICAN_AUTOPID_OFF;
	foreign.ecu_online = false;
	check(view_of(foreign) == CONN_VIEW_FOREIGN && view_of(scan(foreign, WICAN_DTC_RUNNING, 7, 0)) == CONN_VIEW_FOREIGN, "foreign goes before everything its state says");

	view_of(foreign);
	check(failed_round(6000) && failed_round(7000) && failed_round(9000) && conn_view(&conn, 19999) == CONN_VIEW_FOREIGN && conn_view(&conn, 20000) == CONN_VIEW_NO_ANSWER,
	      "no answer goes before foreign");
	state = adapter(OWN, 77);
	view_of(state);
	conn_wifi(&conn, false, 5600);
	check(conn_view(&conn, 5600) == CONN_VIEW_NO_WIFI && conn_view(&conn, 99000) == CONN_VIEW_NO_WIFI, "the WiFi is lost: no WiFi, whatever was answered before");
	join(OWN);
	failed_round(5000);
	failed_round(6000);
	failed_round(8000);
	conn_wifi(&conn, false, 30000);
	check(conn_view(&conn, 30000) == CONN_VIEW_NO_WIFI, "no WiFi goes before no answer");
}

static void test_state(void)
{
	wican_state_t state = scan(adapter(OWN, 77), WICAN_DTC_RUNNING, 42, 41);
	const wican_state_t *known;

	strcpy(state.fw, "4.21");
	strcpy(state.dtc.name, "N30/4 ESP");
	state.batt_mv = 12400;
	state.dtc.step = 5;
	state.dtc.total = 18;

	join(OWN);
	conn_next(&conn, 5000);
	check(conn_state(&conn) == NULL, "no state while the first one is asked for");
	conn_got_state(&conn, CONN_GOT_OK, &state, 5000);
	known = conn_state(&conn);
	check(known != NULL && known != &state, "an answered state is kept by the connection, not as the pointer of the caller");
	check(known != NULL && strcmp(known->id, OWN) == 0 && known->boot == 77 && known->up_s == 100 && known->autopid == WICAN_AUTOPID_RUN && known->pids == 35 &&
	      known->ecu_online && strcmp(known->fw, "4.21") == 0 && known->batt_mv == 12400, "the state kept is the one that was answered");
	check(known != NULL && known->dtc.supported && known->dtc.phase == WICAN_DTC_RUNNING && known->dtc.seq == 42 && known->dtc.result_seq == 41 &&
	      known->dtc.step == 5 && known->dtc.total == 18 && strcmp(known->dtc.name, "N30/4 ESP") == 0, "the fault memory part of the state is kept as well");

	conn_got_result(&conn, CONN_GOT_OK, 5000);
	state.up_s = 101;
	state.dtc.step = 6;
	check(conn_state(&conn) != NULL && conn_state(&conn)->up_s == 100 && conn_state(&conn)->dtc.step == 5, "what the caller does with its state later does not change the one kept");
	round_with(&state, ALL_OK, 6000);
	check(conn_state(&conn) != NULL && conn_state(&conn)->up_s == 101 && conn_state(&conn)->dtc.step == 6, "the state kept is the last one answered");
	failed_round(7000);
	check(conn_state(&conn) != NULL && conn_state(&conn)->up_s == 101, "a round that failed leaves the last state");
	conn_next(&conn, 8000);
	state.up_s = 999;
	conn_got_state(&conn, CONN_GOT_FAILED, &state, 8000);
	check(conn_state(&conn) != NULL && conn_state(&conn)->up_s == 101, "the state that comes with a failed request is ignored");
}

static void test_dtc_allowed(void)
{
	wican_state_t state = adapter(OWN, 77);
	wican_state_t other = state;
	answers_t answers = ALL_OK;
	int wrong = 0;
	int i;

	join(OWN);
	check(!conn_dtc_allowed(&conn), "no answer yet: no fault memory command");
	round_with(&state, ALL_OK, 5000);
	check(!conn_dtc_allowed(&conn), "one answered round: not allowed yet");
	conn_next(&conn, 6000);
	conn_got_state(&conn, CONN_GOT_OK, &state, 6000);
	check(conn_dtc_allowed(&conn), "the state of the second round in a row is answered: allowed");
	check(conn_next(&conn, 6000) == CONN_ASK_VALUES && conn_dtc_allowed(&conn), "allowed also while a request is under way");
	conn_got_values(&conn, CONN_GOT_OK, 6000);
	check(conn_dtc_allowed(&conn) && round_is(state, 7000, "SV") && conn_dtc_allowed(&conn), "it stays allowed while the rounds are answered");

	state.up_s = 14;
	check(round_is(state, 8000, "SV") && !conn_dtc_allowed(&conn), "the adapter is up for 14 s: not allowed");
	state.up_s = 15;
	check(round_is(state, 9000, "SV") && conn_dtc_allowed(&conn), "the adapter is up for 15 s: allowed");
	state.up_s = 16;
	check(round_is(state, 10000, "SV") && conn_dtc_allowed(&conn), "the adapter is up for 16 s: allowed");

	other.autopid = WICAN_AUTOPID_STARTING;
	check(round_is(other, 11000, "S") && !conn_dtc_allowed(&conn), "AutoPID is starting: not allowed");
	other.autopid = WICAN_AUTOPID_OFF;
	check(round_is(other, 12000, "S") && !conn_dtc_allowed(&conn), "AutoPID is off: not allowed");
	other = state;
	other.ecu_online = false;
	check(round_is(other, 13000, "S") && conn_dtc_allowed(&conn), "the ignition does not matter here: allowed with the ECU offline");
	check(round_is(scan(state, WICAN_DTC_RUNNING, 7, 0), 14000, "S") && conn_dtc_allowed(&conn), "a scan does not matter here: allowed while one runs");

	check(failed_round(15000) && !conn_dtc_allowed(&conn), "a failed round takes it away");
	check(round_is(state, 16000, "SV") && !conn_dtc_allowed(&conn), "one answered round after a failed one: not allowed");
	check(round_is(state, 17000, "SV") && conn_dtc_allowed(&conn), "two answered rounds after a failed one: allowed again");

	answers.values = CONN_GOT_FAILED;
	round_with(&state, answers, 18000);
	check(!conn_dtc_allowed(&conn), "a round whose state was answered and whose values failed takes it away as well");
	conn_next(&conn, 19000);
	conn_got_state(&conn, CONN_GOT_OK, &state, 19000);
	check(!conn_dtc_allowed(&conn), "the round with the failed values does not count as answered: one more state is not enough");
	conn_next(&conn, 19000);
	conn_got_values(&conn, CONN_GOT_OK, 19000);
	round_with(&state, ALL_OK, 20000);
	check(conn_dtc_allowed(&conn), "the scene: allowed again");
	conn_wifi(&conn, false, 20500);
	check(!conn_dtc_allowed(&conn), "the WiFi is lost: not allowed");
	conn_wifi(&conn, true, 21000);
	check(round_is(state, 21000, "SCV") && !conn_dtc_allowed(&conn), "one answered round in the network joined again: not allowed, the rounds before do not count");
	check(round_is(state, 22000, "SV") && conn_dtc_allowed(&conn), "two answered rounds in the network joined again: allowed");

	join(OWN);
	other = adapter(OTHER, 5);
	round_with(&other, ALL_OK, 5000);
	round_with(&other, ALL_OK, 6000);
	round_with(&other, ALL_OK, 7000);
	check(!conn_dtc_allowed(&conn), "a foreign adapter: not allowed, however many rounds it answered");

	join(OWN);
	other = state;
	other.dtc.supported = false;
	check(round_is(other, 5000, "SCV") && round_is(other, 6000, "SV") && conn_dtc_allowed(&conn), "the fault memory table of the profile does not matter here: allowed without one");

	join(OWN);
	other = state;
	other.autopid = UNKNOWN_AUTOPID;
	check(round_is(other, 5000, "SC") && round_is(other, 6000, "S") && !conn_dtc_allowed(&conn), "an AutoPID state the display does not know: the catalogue is asked for, a command is not allowed");

	join(OWN);
	other = state;
	other.up_s = 256;
	check(round_is(other, 5000, "SCV") && round_is(other, 6000, "SV") && conn_dtc_allowed(&conn), "the adapter is up for 256 s: allowed");
	other.up_s = 4294967295u;
	check(round_is(other, 7000, "SV") && conn_dtc_allowed(&conn), "the adapter is up for the largest number of seconds: allowed");

	join(OWN);
	for(i = 0; i < 600; i++)
	{
		round_with(&state, ALL_OK, 5000 + (uint64_t)i * 1000);
		if(i >= 1 && !conn_dtc_allowed(&conn)) wrong++;
	}
	check(wrong == 0, "600 answered rounds in a row: allowed after each one from the second on");
}

static void test_foreign(void)
{
	wican_state_t own = adapter(OWN, 77);
	wican_state_t other = scan(adapter(OTHER, 5), WICAN_DTC_DONE, 7, 7);

	join(OWN);
	check(round_is(other, 5000, "S"), "a foreign adapter is asked for nothing but its state: no result, no catalogue, no values");
	check(conn_view(&conn, 5000) == CONN_VIEW_FOREIGN && conn_state(&conn) != NULL && strcmp(conn_state(&conn)->id, OTHER) == 0,
	      "the state of the foreign adapter is the last answer");
	check(round_is(other, 6000, "S") && round_is(other, 7000, "S"), "the state of a foreign adapter is asked for every round");
	check(conn_next(&conn, 7999) == CONN_ASK_NOTHING && conn_next(&conn, 8000) == CONN_ASK_STATE, "the rounds of a foreign adapter are 1000 ms apart");
	conn_got_state(&conn, CONN_GOT_OK, &own, 8000);
	check(conn_view(&conn, 8000) == CONN_VIEW_LIVE && conn_next(&conn, 8000) == CONN_ASK_CATALOG, "the own adapter answers after the foreign one: it is accepted and asked");
	check(conn_take_restarted(&conn), "the own adapter in place of the foreign one counts as another adapter on this connection");

	join(OWN);
	other = adapter("", 5);
	check(round_is(other, 5000, "S") && conn_view(&conn, 5000) == CONN_VIEW_FOREIGN, "an adapter without an id is foreign to a bound display");
	join(OWN);
	other = adapter(OWN "0", 5);
	check(round_is(other, 5000, "S") && conn_view(&conn, 5000) == CONN_VIEW_FOREIGN, "an id that only begins like the bound one is foreign");
	join(OWN "0");
	check(round_is(own, 5000, "S") && conn_view(&conn, 5000) == CONN_VIEW_FOREIGN, "an id that is only the beginning of the bound one is foreign");
	join(OWN);
	other = adapter("A1B2C3D4E5F6", 77);
	check(round_is(other, 5000, "S") && conn_view(&conn, 5000) == CONN_VIEW_FOREIGN, "an id that differs only in upper and lower case is foreign");

	join(LONGEST);
	other = adapter(LONGEST, 3);
	check(round_is(other, 5000, "SCV") && conn_view(&conn, 5000) == CONN_VIEW_LIVE, "the scene: bound to an adapter with an id of 32 bytes");
	other = adapter(LONGEST_TOO, 3);
	check(round_is(other, 6000, "S") && conn_view(&conn, 6000) == CONN_VIEW_FOREIGN, "an id of 32 bytes that differs in the last one is foreign");
	check(conn_take_restarted(&conn), "another id with the same boot number is another adapter on this connection as well");
}

static void test_bind(void)
{
	wican_state_t own = adapter(OWN, 77);
	wican_state_t other = adapter(OTHER, 5);
	wican_state_t nameless = adapter("", 9);
	wican_state_t longest = adapter(LONGEST, 3);
	wican_state_t odd = adapter(ODD, 5);
	char id[40];
	char many[101];

	memset(id, 'x', sizeof(id));
	join(NULL);
	check(!conn_take_bind(&conn, id, sizeof(id)) && id[0] == 'x', "an unbound display before the first answer: no id to take");
	check(round_is(own, 5000, "SCV") && conn_view(&conn, 5000) == CONN_VIEW_LIVE, "an unbound display accepts the first adapter that answers");
	check(conn_take_bind(&conn, id, sizeof(id)) && strcmp(id, OWN) == 0, "the first answer binds: its id is handed out to be stored");
	check(id[13] == 'x' && id[39] == 'x', "with room for 40 bytes nothing is written behind the 12 of the id and its zero");
	memset(id, 'x', sizeof(id));
	check(!conn_take_bind(&conn, id, sizeof(id)) && id[0] == 'x', "the id is handed out exactly once");
	check(round_is(own, 6000, "SV") && !conn_take_bind(&conn, id, sizeof(id)), "later answers of the same adapter bind nothing");
	check(round_is(other, 7000, "S") && conn_view(&conn, 7000) == CONN_VIEW_FOREIGN && !conn_dtc_allowed(&conn), "from the binding on another adapter is foreign");
	check(!conn_take_bind(&conn, id, sizeof(id)), "a foreign adapter does not bind again");

	join("");
	round_with(&own, ALL_OK, 5000);
	check(conn_take_bind(&conn, id, sizeof(id)) && strcmp(id, OWN) == 0, "an empty id at the start means not bound, like none");

	join(OWN);
	round_with(&own, ALL_OK, 5000);
	check(!conn_take_bind(&conn, id, sizeof(id)), "a display that was bound at the start has no id to take");

	// The id is not taken before the next adapter answers, and not before the WiFi is lost
	join(NULL);
	round_with(&own, ALL_OK, 5000);
	round_with(&other, ALL_OK, 6000);
	conn_wifi(&conn, false, 6500);
	conn_wifi(&conn, true, 9000);
	check(round_is(other, 9000, "S") && conn_view(&conn, 9000) == CONN_VIEW_FOREIGN, "the binding outlasts the network: another adapter is foreign after joining again");
	check(conn_take_bind(&conn, id, sizeof(id)) && strcmp(id, OWN) == 0, "an id not taken yet is the one of the first adapter, also after another one answered and the WiFi was lost");

	join(NULL);
	round_with(&own, ALL_OK, 5000);
	memset(id, 'x', sizeof(id));
	check(!conn_take_bind(&conn, id, 12) && id[0] == 'x' && id[11] == 'x', "room for 12 bytes and an id of 12: nothing is written, no cut id is handed out");
	check(!conn_take_bind(&conn, id, 0) && !conn_take_bind(&conn, id, 1) && id[0] == 'x', "no room at all: nothing is written");
	check(conn_take_bind(&conn, id, 13) && strcmp(id, OWN) == 0 && id[13] == 'x', "the id stays to be taken: room for 13 bytes takes it, nothing is written behind them");
	check(!conn_take_bind(&conn, id, 13), "taken with enough room: not handed out again");

	join(NULL);
	check(round_is(nameless, 5000, "SCV") && conn_view(&conn, 5000) == CONN_VIEW_LIVE, "an adapter without an id is accepted by an unbound display");
	check(!conn_take_bind(&conn, id, sizeof(id)), "an answer without an id binds nothing");
	check(round_is(own, 6000, "SCV") && conn_take_bind(&conn, id, sizeof(id)) && strcmp(id, OWN) == 0, "the first answer with an id binds");
	check(round_is(nameless, 7000, "S") && conn_view(&conn, 7000) == CONN_VIEW_FOREIGN, "after the binding the adapter without an id is foreign");

	join(NULL);
	round_with(&longest, ALL_OK, 5000);
	memset(id, 'x', sizeof(id));
	check(!conn_take_bind(&conn, id, 32) && id[0] == 'x', "an id of 32 bytes does not fit into 32");
	check(conn_take_bind(&conn, id, 33) && strcmp(id, LONGEST) == 0 && id[33] == 'x', "an id of 32 bytes is handed out whole into 33");
	check(round_is(longest, 6000, "SV") && conn_view(&conn, 6000) == CONN_VIEW_LIVE, "the adapter with the id of 32 bytes is the one bound to");

	join(NULL);
	check(round_is(odd, 5000, "SCV") && conn_take_bind(&conn, id, sizeof(id)) && strcmp(id, ODD) == 0,
	      "an id that begins with a space and has bytes above 127 binds like any other and is handed out byte for byte");
	check(round_is(odd, 6000, "SV") && conn_view(&conn, 6000) == CONN_VIEW_LIVE && round_is(own, 7000, "S") && conn_view(&conn, 7000) == CONN_VIEW_FOREIGN,
	      "the adapter with that id is the one bound to, another one is foreign");
	join(ODD);
	check(round_is(odd, 5000, "SCV") && conn_view(&conn, 5000) == CONN_VIEW_LIVE && !conn_take_bind(&conn, id, sizeof(id)), "a display started with that id is bound to its adapter");

	memset(many, 'f', sizeof(many) - 1);
	many[sizeof(many) - 1] = '\0';
	conn_init(&conn, many);
	check(strlen(conn.bound_id) == 32 && memcmp(conn.bound_id, many, 32) == 0 && !conn.wifi && conn.wifi_since_ms == 0 && conn_view(&conn, 0) == CONN_VIEW_NO_WIFI,
	      "a bound id of 100 bytes is cut to the 32 an id can have, nothing is written behind its field");
}

static void test_restart(void)
{
	wican_state_t before = scan(adapter(OWN, 77), WICAN_DTC_DONE, 7, 7);
	wican_state_t after = scan(adapter(OWN, 78), WICAN_DTC_DONE, 7, 7);
	wican_state_t other = adapter(OTHER, 77);

	join(OWN);
	check(round_is(before, 5000, "SRCV") && !conn_take_restarted(&conn), "the first answer on a connection is no restart");
	check(round_is(before, 6000, "SV") && !conn_take_restarted(&conn), "the same boot number again is no restart");
	check(round_is(after, 7000, "SRCV"), "another boot number: the catalogue is asked for again, and a result with a number fetched before is fetched again");
	check(conn_take_restarted(&conn), "another boot number marks a restart");
	check(!conn_take_restarted(&conn), "the restart is reported exactly once");
	check(round_is(after, 8000, "SV") && !conn_take_restarted(&conn), "the new boot number again is no restart");
	check(round_is(before, 9000, "SRCV") && conn_take_restarted(&conn), "the old boot number again is another one than the last: a restart");

	// Two restarts and a lost network before the caller looks
	round_with(&after, ALL_OK, 10000);
	round_with(&before, ALL_OK, 11000);
	conn_wifi(&conn, false, 11500);
	check(conn_take_restarted(&conn) && !conn_take_restarted(&conn), "a restart not taken yet stays over more restarts and the loss of the WiFi, and is reported once");

	join(OWN);
	round_with(&before, ALL_OK, 5000);
	check(round_is(scan(after, WICAN_DTC_RUNNING, 8, 0), 6000, "S") && conn_take_restarted(&conn), "a restart seen while a scan runs: no catalogue yet");
	check(round_is(scan(after, WICAN_DTC_DONE, 8, 8), 7000, "SRCV"), "the catalogue after a restart is asked for when the scan is over");

	join(NULL);
	round_with(&before, ALL_OK, 5000);
	conn_wifi(&conn, false, 5500);
	conn_wifi(&conn, true, 9000);
	check(round_is(after, 9000, "SRCV") && !conn_take_restarted(&conn), "another boot number in a network joined again is the first answer there: no restart");

	join(OWN);
	round_with(&before, ALL_OK, 5000);
	before.up_s = 3;
	check(round_is(before, 6000, "SV") && !conn_take_restarted(&conn), "an uptime that went back with the same boot number is no restart");

	join(NULL);
	round_with(&other, ALL_OK, 5000);
	other.boot = 78;
	check(round_is(other, 6000, "SCV") && conn_take_restarted(&conn), "a restart of the adapter an unbound display bound itself to is a restart as well");

	join(OWN);
	round_with(&after, ALL_OK, 5000);
	after.boot = 78 + 65536;
	check(round_is(after, 6000, "SRCV") && conn_take_restarted(&conn), "a boot number that differs by 65536 is another boot number");
	after.boot = 79;
	after.up_s = 5000;
	check(round_is(after, 7000, "SRCV") && conn_take_restarted(&conn), "a restart seen late, with an uptime above the last one seen, is a restart");
}

static void test_no_api(void)
{
	wican_state_t own = adapter(OWN, 77);
	wican_state_t other = adapter(OTHER, 5);
	answers_t no_api = ALL_OK;
	answers_t answers;
	char id[40];
	uint64_t now;
	int wrong = 0;

	no_api.state = CONN_GOT_NOT_FOUND;

	join(OWN);
	check(strcmp(round_with(&own, no_api, 5000), "SCV") == 0, "a 404 for the state: the round goes on with the catalogue and the values, whatever state comes with it");
	check(conn_view(&conn, 5000) == CONN_VIEW_NO_API, "a firmware without the API was answered: not connecting, but no API");
	check(conn_state(&conn) == NULL && !conn_dtc_allowed(&conn), "a firmware without the API: no state, no fault memory command");
	for(now = 6000; now <= 34000; now += 1000)
	{
		if(strcmp(round_with(NULL, no_api, now), "V") != 0) wrong++;
	}
	check(wrong == 0, "the rounds of a firmware without the API ask for the values every 1000 ms: no state, the catalogue once");
	check(strcmp(round_with(NULL, no_api, 35000), "SV") == 0, "30000 ms after the 404 the state is asked for again; another 404: the round goes on with the values");
	check(strcmp(round_with(NULL, no_api, 36000), "V") == 0 && strcmp(round_with(NULL, no_api, 64999), "V") == 0,
	      "29999 ms after the second 404 the state is not asked for");
	check(conn_next(&conn, 65999) == CONN_ASK_STATE, "the state is asked for again by the first round that is 30000 ms or more after the last 404");

	// The 404 comes 300 ms after its round began
	join(OWN);
	conn_next(&conn, 5000);
	conn_got_state(&conn, CONN_GOT_NOT_FOUND, NULL, 5300);
	check(conn_next(&conn, 5300) == CONN_ASK_CATALOG, "the round goes on behind a 404 that took its time");
	conn_got_catalog(&conn, CONN_GOT_OK, 5300);
	conn_next(&conn, 5300);
	conn_got_values(&conn, CONN_GOT_OK, 5300);
	check(strcmp(round_with(NULL, no_api, 35299), "V") == 0, "29999 ms after the 404 arrived: the state is not asked for, although the round of the 404 began 30299 ms ago");
	join(OWN);
	conn_next(&conn, 5000);
	conn_got_state(&conn, CONN_GOT_NOT_FOUND, NULL, 5300);
	round_with(NULL, no_api, 5300);
	check(strcmp(round_with(NULL, no_api, 35300), "SV") == 0, "30000 ms after the 404 arrived: the state is asked for");
	join(OWN);
	conn_next(&conn, 5000);
	conn_got_state(&conn, CONN_GOT_NOT_FOUND, NULL, 3000);
	round_with(NULL, no_api, 5000);
	check(strcmp(round_with(NULL, no_api, 34999), "V") == 0 && strcmp(round_with(NULL, no_api, 35999), "SV") == 0,
	      "a 404 with a time before the start of its round arrived at that start: the 30000 ms count from there");

	join(OWN);
	round_with(NULL, no_api, 5000);
	check(strcmp(round_with(NULL, no_api, 5000 + DAYS_49 + 5), "SV") == 0, "a round 2^32 ms after the 404: the state is asked for, the 30000 ms do not begin anew");
	check(strcmp(round_with(NULL, no_api, 5000 + DAYS_49 + 30004), "V") == 0 && strcmp(round_with(NULL, no_api, 5000 + DAYS_49 + 31004), "SV") == 0,
	      "a 404 after 2^32 ms: the state is asked for again by the first round 30000 ms later, not before");

	// The catalogue and the values of a firmware without the API
	join(OWN);
	answers = no_api;
	answers.catalog = CONN_GOT_NOT_FOUND;
	check(strcmp(round_with(NULL, answers, 5000), "SCV") == 0 && strcmp(round_with(NULL, answers, 6000), "CV") == 0 && strcmp(round_with(NULL, answers, 7000), "CV") == 0,
	      "without the API a catalogue the adapter did not have is asked for again every round, in front of the values");
	check(strcmp(round_with(NULL, no_api, 8000), "CV") == 0 && strcmp(round_with(NULL, no_api, 9000), "V") == 0, "without the API the catalogue that arrives is fetched once");
	answers = no_api;
	answers.values = CONN_GOT_FAILED;
	check(strcmp(round_with(NULL, answers, 10000), "V") == 0 && conn_next(&conn, 10999) == CONN_ASK_NOTHING && strcmp(round_with(NULL, answers, 11000), "V") == 0 &&
	      conn_next(&conn, 12999) == CONN_ASK_NOTHING && strcmp(round_with(NULL, answers, 13000), "V") == 0,
	      "without the API values that failed are a failed round: the waits of 1000 and 2000 ms follow");
	check(conn_view(&conn, 19999) == CONN_VIEW_NO_API && conn_view(&conn, 20000) == CONN_VIEW_NO_ANSWER, "no answer goes before no API");
	check(strcmp(round_with(NULL, no_api, 18000), "V") == 0 && conn_view(&conn, 20000) == CONN_VIEW_NO_API, "an answered round without a state ends no answer as well");

	// A firmware with the API in place of one without, and the other way round
	join(OWN);
	round_with(NULL, no_api, 5000);
	for(now = 6000; now <= 34000; now += 1000) round_with(NULL, no_api, now);
	check(round_is(own, 35000, "SCV") && conn_view(&conn, 35000) == CONN_VIEW_LIVE && conn_state(&conn) != NULL,
	      "a state after a 404: the firmware has the API now, and the catalogue is asked for again");
	check(conn_take_restarted(&conn) && !conn_take_restarted(&conn), "a state after a 404 is another firmware: it counts as a restart, once");
	check(!conn_dtc_allowed(&conn) && round_is(own, 36000, "SV") && conn_dtc_allowed(&conn), "the rounds before the first state do not count for a fault memory command");

	check(strcmp(round_with(NULL, no_api, 37000), "SCV") == 0, "a 404 after an answered state: the catalogue is asked for again, then the values");
	check(conn_take_restarted(&conn) && !conn_take_restarted(&conn), "a 404 after an answered state is another firmware: it counts as a restart, once");
	check(conn_state(&conn) == NULL && !conn_dtc_allowed(&conn) && conn_view(&conn, 37000) == CONN_VIEW_NO_API, "after the 404 the state of before is gone");
	check(strcmp(round_with(NULL, no_api, 38000), "V") == 0 && !conn_take_restarted(&conn), "the rounds without a state that follow are no restart");
	for(now = 39000; now <= 66000; now += 1000) round_with(NULL, no_api, now);
	check(round_is(scan(own, WICAN_DTC_DONE, 7, 7), 67000, "SRCV") && !conn_dtc_allowed(&conn), "one state after a 404 is one answered round, whatever was answered before the 404");
	check(strcmp(round_with(NULL, no_api, 68000), "SCV") == 0 && strcmp(round_with(NULL, no_api, 69000), "V") == 0,
	      "a 404 after a state that named a result: no result is asked for, a firmware without the API has none");

	join(OWN);
	round_with(&own, ALL_OK, 5000);
	check(round_is(scan(own, WICAN_DTC_RUNNING, 7, 0), 6000, "S") && strcmp(round_with(NULL, no_api, 7000), "SCV") == 0,
	      "a 404 after a state with a running scan: the scan of the firmware before does not hold the catalogue back");

	join(OWN);
	round_with(&own, no_api, 5000);
	check(!conn_take_restarted(&conn), "a 404 as the first answer on a connection is no restart");

	// Binding and a firmware without the API
	join(OWN);
	round_with(&other, ALL_OK, 5000);
	check(strcmp(round_with(&other, no_api, 6000), "SCV") == 0 && conn_view(&conn, 6000) == CONN_VIEW_NO_API,
	      "a firmware without the API has no id: it is not foreign, its values are asked for, also after a foreign adapter");
	for(now = 7000; now <= 35000; now += 1000) round_with(NULL, no_api, now);
	check(round_is(other, 36000, "S") && conn_view(&conn, 36000) == CONN_VIEW_FOREIGN, "a foreign adapter after a 404 is foreign");
	join(NULL);
	round_with(NULL, no_api, 5000);
	check(!conn_take_bind(&conn, id, sizeof(id)), "a 404 binds nothing");

	// Joining again
	join(OWN);
	round_with(NULL, no_api, 5000);
	conn_wifi(&conn, false, 5500);
	conn_wifi(&conn, true, 9000);
	check(conn_view(&conn, 9000) == CONN_VIEW_CONNECTING && conn_next(&conn, 9000) == CONN_ASK_STATE,
	      "joined again after a firmware without the API: nothing is known, the state is asked for at once");
}

static void test_wifi(void)
{
	wican_state_t state = scan(adapter(OWN, 77), WICAN_DTC_DONE, 7, 7);
	wican_state_t after = scan(adapter(OWN, 78), WICAN_DTC_DONE, 7, 7);

	join(OWN);
	conn_next(&conn, 5000);
	conn_wifi(&conn, false, 5100);
	check(conn_view(&conn, 5100) == CONN_VIEW_NO_WIFI && conn_next(&conn, 5100) == CONN_ASK_NOTHING && conn_next(&conn, 99000) == CONN_ASK_NOTHING,
	      "the WiFi is lost in the middle of a request: nothing is asked any more");
	conn_got_state(&conn, CONN_GOT_OK, &state, 5200);
	check(conn_state(&conn) == NULL, "the answer to a request that was under way when the WiFi was lost is ignored");
	conn_wifi(&conn, true, 9000);
	conn_got_state(&conn, CONN_GOT_OK, &state, 9000);
	check(conn_state(&conn) == NULL && conn_view(&conn, 9000) == CONN_VIEW_CONNECTING, "joined again: nothing is known about the adapter, an answer without a request is ignored");
	check(conn_next(&conn, 9000) == CONN_ASK_STATE, "joined again: the first round is due at once");

	// In the middle of a round, behind its state
	join(OWN);
	conn_next(&conn, 5000);
	conn_got_state(&conn, CONN_GOT_OK, &state, 5000);
	conn_next(&conn, 5000);
	conn_wifi(&conn, false, 5100);
	conn_wifi(&conn, true, 5200);
	check(conn_state(&conn) == NULL && conn_view(&conn, 5200) == CONN_VIEW_CONNECTING, "the state of the network before is forgotten in the one joined");
	conn_got_result(&conn, CONN_GOT_OK, 5300);
	check(conn_next(&conn, 5300) == CONN_ASK_STATE, "joined again in the middle of a round: the round does not go on, a new one begins with the state");
	conn_got_result(&conn, CONN_GOT_FAILED, 5400);
	conn_got_state(&conn, CONN_GOT_OK, &state, 5400);
	check(conn_next(&conn, 5400) == CONN_ASK_RESULT, "the end of the result asked for in the network before did not count: it is asked for");

	// What a connection has fetched does not count in the next
	join(OWN);
	check(round_is(state, 5000, "SRCV") && round_is(state, 6000, "SV"), "the scene: result and catalogue are fetched");
	conn_wifi(&conn, false, 6500);
	conn_wifi(&conn, true, 20000);
	check(round_is(state, 20000, "SRCV"), "joined again: the catalogue and the result are fetched again");
	check(!conn_take_restarted(&conn), "joining again is no restart");
	conn_wifi(&conn, false, 20500);
	conn_wifi(&conn, true, 30000);
	check(round_is(after, 30000, "SRCV") && !conn_take_restarted(&conn), "the adapter restarted while the display was in no network: no restart is reported, joining starts over");

	// The wait after failed rounds does not outlast the network
	join(OWN);
	check(failed_round(5000) && failed_round(6000) && failed_round(8000) && failed_round(13000) && conn_next(&conn, 22999) == CONN_ASK_NOTHING,
	      "the scene: four failed rounds, the next one is due at 23000");
	conn_wifi(&conn, false, 14000);
	conn_wifi(&conn, true, 15000);
	check(conn_next(&conn, 15000) == CONN_ASK_STATE, "joined again during the wait after failed rounds: the first round is due at once");
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 15000);
	check(conn_next(&conn, 15999) == CONN_ASK_NOTHING && failed_round(16000) && conn_view(&conn, 99000) == CONN_VIEW_CONNECTING,
	      "the failed rounds are counted anew in the network joined: wait 1000 ms after the first, and two are not shown as no answer");

	// The same value again
	join(OWN);
	conn_next(&conn, 5000);
	conn_wifi(&conn, true, 5500);
	check(conn_next(&conn, 5500) == CONN_ASK_NOTHING, "up while up changes nothing: the request under way is still under way");
	conn_got_state(&conn, CONN_GOT_OK, &state, 5600);
	check(conn_state(&conn) != NULL && conn_next(&conn, 5600) == CONN_ASK_RESULT, "up while up changes nothing: the answer to the request counts");
	conn_wifi(&conn, true, 5700);
	check(conn_state(&conn) != NULL && conn_view(&conn, 5700) == CONN_VIEW_LIVE && conn_next(&conn, 5700) == CONN_ASK_NOTHING, "up while up changes nothing: the state stays");
	conn_got_result(&conn, CONN_GOT_FAILED, 5800);
	check(failed_round(6800) && failed_round(8800), "the scene: three failed rounds in a row");
	conn_wifi(&conn, true, 14000);
	check(conn_next(&conn, 13799) == CONN_ASK_NOTHING && conn_view(&conn, 19999) == CONN_VIEW_LIVE && conn_view(&conn, 20000) == CONN_VIEW_NO_ANSWER,
	      "up while up changes nothing: the wait, the failed rounds and the grace time are the ones of before");
	conn_wifi(&conn, false, 21000);
	conn_wifi(&conn, false, 22000);
	conn_wifi(&conn, true, 23000);
	check(failed_round(23000) && failed_round(24000) && failed_round(26000) && conn_view(&conn, 37999) == CONN_VIEW_CONNECTING && conn_view(&conn, 38000) == CONN_VIEW_NO_ANSWER,
	      "down while down changes nothing either: the next network is joined as usual");
}

static void test_mismatch(void)
{
	wican_state_t state = scan(adapter(OWN, 77), WICAN_DTC_DONE, 7, 7);
	wican_state_t other = adapter(OWN, 78);

	join(OWN);
	conn_got_state(&conn, CONN_GOT_OK, &state, 5000);
	check(conn_state(&conn) == NULL && conn_next(&conn, 5000) == CONN_ASK_STATE, "a state before any request is ignored");
	conn_got_result(&conn, CONN_GOT_OK, 5000);
	conn_got_catalog(&conn, CONN_GOT_OK, 5000);
	conn_got_values(&conn, CONN_GOT_FAILED, 5000);
	check(conn_next(&conn, 5000) == CONN_ASK_NOTHING && conn_state(&conn) == NULL, "result, catalogue and values while the state is under way are ignored: it is still under way");
	conn_got_state(&conn, CONN_GOT_OK, &state, 5000);
	conn_got_state(&conn, CONN_GOT_OK, &other, 5000);
	check(conn_state(&conn) != NULL && conn_state(&conn)->boot == 77 && !conn_take_restarted(&conn), "a second end of the state is ignored");
	check(conn_next(&conn, 5000) == CONN_ASK_RESULT, "after the ignored answers the round goes on with the result");
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 5000);
	conn_got_catalog(&conn, CONN_GOT_FAILED, 5000);
	conn_got_values(&conn, CONN_GOT_FAILED, 5000);
	check(conn_next(&conn, 5000) == CONN_ASK_NOTHING, "state, catalogue and values while the result is under way are ignored");
	conn_got_result(&conn, CONN_GOT_OK, 5000);
	check(conn_next(&conn, 5000) == CONN_ASK_CATALOG, "the round goes on with the catalogue");
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 5000);
	conn_got_result(&conn, CONN_GOT_FAILED, 5000);
	conn_got_values(&conn, CONN_GOT_FAILED, 5000);
	check(conn_next(&conn, 5000) == CONN_ASK_NOTHING, "state, result and values while the catalogue is under way are ignored");
	conn_got_catalog(&conn, CONN_GOT_OK, 5000);
	check(conn_next(&conn, 5000) == CONN_ASK_VALUES, "the round goes on with the values");
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 5000);
	conn_got_result(&conn, CONN_GOT_FAILED, 5000);
	conn_got_catalog(&conn, CONN_GOT_FAILED, 5000);
	check(conn_next(&conn, 5000) == CONN_ASK_NOTHING, "state, result and catalogue while the values are under way are ignored");
	conn_got_values(&conn, CONN_GOT_OK, 5000);

	// Between two rounds
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 5500);
	conn_got_result(&conn, CONN_GOT_FAILED, 5500);
	conn_got_catalog(&conn, CONN_GOT_FAILED, 5500);
	conn_got_values(&conn, CONN_GOT_FAILED, 5500);
	check(conn_next(&conn, 5999) == CONN_ASK_NOTHING && round_is(state, 6000, "SV") && conn_dtc_allowed(&conn),
	      "failures reported between two rounds are ignored: the round has not failed, the next one comes at its time and asks nothing again");
	conn_got_state(&conn, CONN_GOT_OK, &other, 6500);
	check(conn_state(&conn) != NULL && conn_state(&conn)->boot == 77 && !conn_take_restarted(&conn), "a state reported between two rounds is ignored");

	// A second end of a request that failed
	join(OWN);
	conn_next(&conn, 5000);
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 5000);
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 5000);
	conn_got_state(&conn, CONN_GOT_FAILED, NULL, 5500);
	check(conn_next(&conn, 5999) == CONN_ASK_NOTHING && conn_next(&conn, 6000) == CONN_ASK_STATE, "a failure reported twice is one failed round: the wait is 1000 ms from the first report");
}

/*
 * The same rules a second time, for the walk below. In another shape: the requests of a round are a list
 * that is written when the round begins and when its state arrives, and a wait is a duration from a point
 * in time instead of the time it ends.
 */
typedef enum
{
	KNOWN_NOTHING,
	KNOWN_API,
	KNOWN_NO_API,
} known_t;

typedef struct
{
	char bound[33];
	bool news_bind, news_restart;
	bool joined;
	uint64_t joined_at;
	known_t known;              // what answered on this connection last
	wican_state_t last;         // of KNOWN_API
	bool catalog_done;
	uint32_t result_have;
	uint64_t fails, goods;
	bool in_round;
	uint64_t began;
	conn_ask_t list[4];         // the requests of the round
	int listed, handed;
	bool waiting;               // for the end of list[handed - 1]
	uint64_t wait_from, wait_ms;
	uint64_t no_api_at;
} model_t;

static uint64_t model_since(uint64_t now, uint64_t then)
{
	return now > then ? now - then : 0;
}

static void model_init(model_t *model, const char *bound)
{
	memset(model, 0, sizeof(*model));
	if(bound != NULL) snprintf(model->bound, sizeof(model->bound), "%.32s", bound);
}

static void model_wifi(model_t *model, bool up, uint64_t now)
{
	if(up == model->joined) return;

	model->joined = up;
	model->joined_at = now;
	model->known = KNOWN_NOTHING;
	model->catalog_done = false;
	model->result_have = 0;
	model->fails = 0;
	model->goods = 0;
	model->in_round = false;
	model->waiting = false;
	model->wait_ms = 0;
}

static bool model_scanning(const wican_state_t *state)
{
	return state->dtc.phase == WICAN_DTC_QUEUED || state->dtc.phase == WICAN_DTC_RUNNING;
}

static bool model_foreign(const model_t *model)
{
	return model->known == KNOWN_API && model->bound[0] != '\0' && strcmp(model->bound, model->last.id) != 0;
}

static void model_list(model_t *model, conn_ask_t ask)
{
	model->list[model->listed++] = ask;
}

static conn_ask_t model_next(model_t *model, uint64_t now)
{
	if(!model->joined || model->waiting) return CONN_ASK_NOTHING;

	if(!model->in_round)
	{
		if(model_since(now, model->wait_from) < model->wait_ms) return CONN_ASK_NOTHING;

		model->in_round = true;
		model->began = now;
		model->listed = 0;
		model->handed = 0;
		if(model->known == KNOWN_NO_API && model_since(now, model->no_api_at) < 30000)
		{
			if(!model->catalog_done) model_list(model, CONN_ASK_CATALOG);
			model_list(model, CONN_ASK_VALUES);
		}
		else
		{
			model_list(model, CONN_ASK_STATE);
		}
	}
	model->waiting = true;
	return model->list[model->handed++];
}

static void model_another_adapter(model_t *model)
{
	model->news_restart = true;
	model->catalog_done = false;
	model->result_have = 0;
}

static void model_got(model_t *model, conn_ask_t what, conn_got_t got, const wican_state_t *state, uint64_t now)
{
	static const uint64_t waits[] = {1000, 2000, 5000, 10000};
	uint64_t ended = now > model->began ? now : model->began;
	bool failed = false;

	if(!model->waiting || model->list[model->handed - 1] != what) return;
	model->waiting = false;

	if(what == CONN_ASK_STATE && got == CONN_GOT_OK && state != NULL)
	{
		if(model->known == KNOWN_NO_API) model_another_adapter(model);
		if(model->known == KNOWN_API)
		{
			if(model->last.boot != state->boot || strcmp(model->last.id, state->id) != 0) model_another_adapter(model);
			else if(model->last.pids != state->pids) model->catalog_done = false;
		}
		if(model->bound[0] == '\0' && state->id[0] != '\0')
		{
			strcpy(model->bound, state->id);
			model->news_bind = true;
		}
		model->known = KNOWN_API;
		model->last = *state;
		model->goods++;
		if(!model_foreign(model))
		{
			if(state->dtc.result_seq != 0 && state->dtc.result_seq != model->result_have) model_list(model, CONN_ASK_RESULT);
			if(!model->catalog_done && !model_scanning(state)) model_list(model, CONN_ASK_CATALOG);
			if(state->autopid == WICAN_AUTOPID_RUN && state->ecu_online && !model_scanning(state)) model_list(model, CONN_ASK_VALUES);
		}
	}
	else if(what == CONN_ASK_STATE && got == CONN_GOT_NOT_FOUND)
	{
		if(model->known == KNOWN_API) model_another_adapter(model);
		model->known = KNOWN_NO_API;
		model->no_api_at = ended;
		model->goods = 0;
		if(!model->catalog_done) model_list(model, CONN_ASK_CATALOG);
		model_list(model, CONN_ASK_VALUES);
	}
	else if(what == CONN_ASK_RESULT && (got == CONN_GOT_OK || got == CONN_GOT_NOT_FOUND))
	{
		model->result_have = model->last.dtc.result_seq;
	}
	else if(what == CONN_ASK_CATALOG && (got == CONN_GOT_OK || got == CONN_GOT_NOT_FOUND))
	{
		if(got == CONN_GOT_OK) model->catalog_done = true;
	}
	else if(what != CONN_ASK_VALUES || got != CONN_GOT_OK)
	{
		failed = true;
	}

	if(failed)
	{
		model->in_round = false;
		model->goods = 0;
		model->fails++;
		model->wait_from = ended;
		model->wait_ms = waits[model->fails > 4 ? 3 : model->fails - 1];
	}
	else if(model->handed == model->listed)
	{
		model->in_round = false;
		model->fails = 0;
		model->wait_from = model->began;
		model->wait_ms = 1000;
	}
}

static conn_view_t model_view(const model_t *model, uint64_t now)
{
	if(!model->joined) return CONN_VIEW_NO_WIFI;
	if(model->fails >= 3 && model_since(now, model->joined_at) >= 15000) return CONN_VIEW_NO_ANSWER;
	if(model->known == KNOWN_NOTHING) return CONN_VIEW_CONNECTING;
	if(model->known == KNOWN_NO_API) return CONN_VIEW_NO_API;
	if(model_foreign(model)) return CONN_VIEW_FOREIGN;
	if(model->last.autopid != WICAN_AUTOPID_RUN) return model->last.autopid == WICAN_AUTOPID_OFF ? CONN_VIEW_AUTOPID_OFF : CONN_VIEW_STARTING;
	if(model_scanning(&model->last)) return CONN_VIEW_SCAN;
	return model->last.ecu_online ? CONN_VIEW_LIVE : CONN_VIEW_ECU_OFFLINE;
}

static bool model_dtc_allowed(const model_t *model)
{
	return model->known == KNOWN_API && !model_foreign(model) && model->last.autopid == WICAN_AUTOPID_RUN && model->last.up_s >= 15 && model->goods >= 2;
}

static bool model_take_bind(model_t *model, char *id, size_t size)
{
	if(!model->news_bind || id == NULL || strlen(model->bound) + 1 > size) return false;

	strcpy(id, model->bound);
	model->news_bind = false;
	return true;
}

typedef struct
{
	long calls;
	long different;             // walks in which module and model differed
	long asked[5];              // requests handed out, by kind
	long views[10];             // views after a call, by kind
	long restarts, binds, binds_refused, ignored, steps_back, joins, failed_rounds, no_state, allowed, counted_on;
} walk_result_t;

#define WALKS           60
#define WALK_CALLS      50000

static uint32_t walk_random_state;

static uint32_t walk_random(uint32_t below)
{
	walk_random_state = walk_random_state * 1664525u + 1013904223u;
	return (walk_random_state >> 8) % below;
}

// One thing about the adapter changes
static void walk_change(wican_state_t *state)
{
	static const char *const ids[] = {OWN, OWN, OWN, OTHER, "", LONGEST};
	static const uint32_t ups[] = {0, 14, 15, 16, 100};
	static const uint32_t counts[] = {35, 35, 36, 0};
	static const uint32_t results[] = {0, 41, 42, 43};
	static const wican_autopid_t others[] = {WICAN_AUTOPID_OFF, WICAN_AUTOPID_OFF, WICAN_AUTOPID_STARTING, WICAN_AUTOPID_STARTING, UNKNOWN_AUTOPID};
	uint32_t what = walk_random(17);

	if(what < 1) strcpy(state->id, ids[walk_random(6)]);
	else if(what == 16) state->dtc.supported = walk_random(3) != 0;
	else if(what < 2) state->boot = 77 + walk_random(2);
	else if(what < 4) state->up_s = ups[walk_random(5)];
	else if(what < 6) state->autopid = walk_random(3) == 0 ? others[walk_random(5)] : WICAN_AUTOPID_RUN;
	else if(what < 8) state->ecu_online = walk_random(3) != 0;
	else if(what < 9) state->pids = counts[walk_random(4)];
	else if(what < 12) state->dtc.phase = (wican_dtc_phase_t)walk_random(5);
	else state->dtc.result_seq = results[walk_random(4)];
}

static bool walk_same_state(const wican_state_t *got, const wican_state_t *expected)
{
	return strcmp(got->id, expected->id) == 0 && got->boot == expected->boot && got->up_s == expected->up_s && got->autopid == expected->autopid &&
	       got->pids == expected->pids && got->ecu_online == expected->ecu_online && got->dtc.phase == expected->dtc.phase &&
	       got->dtc.result_seq == expected->dtc.result_seq;
}

// Random calls with times that mostly go on a little, sometimes stand exactly at, before or behind a limit,
// and sometimes step back. Module and model are compared after every call. false at the first difference.
static bool walk(uint32_t seed, walk_result_t *result)
{
	static const char *const bounds[] = {NULL, NULL, "", OWN, OWN, OWN, OTHER, LONGEST, LONGEST "0123"};
	static const uint64_t limits[] = {999, 1000, 1001, 1999, 2000, 2001, 4999, 5000, 5001, 9999, 10000, 10001, 14999, 15000, 15001, 29999, 30000, 30001};
	static const conn_ask_t kinds[] = {CONN_ASK_STATE, CONN_ASK_RESULT, CONN_ASK_CATALOG, CONN_ASK_VALUES};
	static conn_t walked;
	static model_t model;
	wican_state_t situation = adapter(OWN, 77);
	const char *bound;
	const char *what = "";
	uint64_t now;
	bool dead = false, upstream = false;
	int call;

	walk_random_state = seed;
	now = walk_random(2) == 0 ? 0 : DAYS_49 - 3000;
	bound = bounds[walk_random(9)];
	memset(&walked, 0x5A, sizeof(walked));
	conn_init(&walked, bound);
	model_init(&model, bound);
	// Half of the walks begin in a network: those that begin shortly before 2^32 ms have rounds across it
	if(walk_random(2) == 0)
	{
		conn_wifi(&walked, true, now);
		model_wifi(&model, true, now);
	}

	for(call = 0; call < WALK_CALLS; call++)
	{
		uint32_t pace = walk_random(1000);
		uint32_t operation = walk_random(1000);
		uint64_t before = now;
		uint64_t asked_at;
		bool same = true;

		if(pace < 500) now += walk_random(40);
		else if(pace < 700) now += walk_random(400);
		// To the limits of this connection, if there are any: the time of the walk stays where it began
		else if(pace < 800) now = model.joined && model.wait_ms != 0 ? model.wait_from + model.wait_ms + walk_random(3) : now;
		else if(pace < 850) now += limits[walk_random(sizeof(limits) / sizeof(limits[0]))];
		else if(pace < 870) now = model.joined ? model.joined_at + 15000 + walk_random(3) : now;
		else if(pace < 890) now = model.known == KNOWN_NO_API ? model.no_api_at + 30000 + walk_random(3) : now;
		else if(pace < 930) now -= walk_random(20000) < now ? walk_random(3000) % (now + 1) : 0;
		// At a limit and one before it
		if(pace >= 700 && pace < 890 && pace % 3 == 0 && now > 0) now--;
		if(now < before) result->steps_back++;

		if(operation < 400 || (operation < 730 && !model.waiting))
		{
			conn_ask_t ask = conn_next(&walked, now);
			conn_ask_t expected = model_next(&model, now);

			same = ask == expected;
			result->asked[expected]++;
			what = "next";
		}
		else if(operation < 730)
		{
			// The end of the request under way, now and then reported as the end of another one
			conn_ask_t kind = model.list[model.handed - 1];
			conn_got_t got = CONN_GOT_OK;
			const wican_state_t *state = &situation;
			uint32_t outcome = walk_random(100);
			uint64_t fails = model.fails;

			if(walk_random(12) == 0) kind = kinds[walk_random(4)];
			if(outcome < 8) got = CONN_GOT_NOT_FOUND;
			else if(outcome < 16) got = CONN_GOT_FAILED;
			else if(outcome < 17) got = (conn_got_t)3;
			else if(outcome < 19) state = NULL;
			if(dead) got = CONN_GOT_FAILED;
			if(upstream && kind == CONN_ASK_STATE && !dead) got = CONN_GOT_NOT_FOUND;
			if(kind == CONN_ASK_STATE && got == CONN_GOT_OK && state == NULL) result->no_state++;
			if(kind != model.list[model.handed - 1]) result->ignored++;

			if(kind == CONN_ASK_STATE) conn_got_state(&walked, got, state, now);
			else if(kind == CONN_ASK_RESULT) conn_got_result(&walked, got, now);
			else if(kind == CONN_ASK_CATALOG) conn_got_catalog(&walked, got, now);
			else conn_got_values(&walked, got, now);
			model_got(&model, kind, got, state, now);
			if(model.fails > fails) result->failed_rounds++;
			what = "got";
		}
		else if(operation < 760)
		{
			// The end of a request while none is under way
			conn_ask_t kind = kinds[walk_random(4)];
			conn_got_t got = (conn_got_t)walk_random(3);

			if(kind == CONN_ASK_STATE) conn_got_state(&walked, got, &situation, now);
			else if(kind == CONN_ASK_RESULT) conn_got_result(&walked, got, now);
			else if(kind == CONN_ASK_CATALOG) conn_got_catalog(&walked, got, now);
			else conn_got_values(&walked, got, now);
			model_got(&model, kind, got, &situation, now);
			if(!model.waiting) result->ignored++;
			what = "got without a request";
		}
		else if(operation < 880)
		{
			// The adapter does not answer for a while, or has a firmware without the API for a while
			walk_change(&situation);
			if(walk_random(dead ? 5 : 80) == 0) dead = !dead;
			if(walk_random(upstream ? 8 : 120) == 0) upstream = !upstream;
			what = "change";
		}
		else if(operation < 892)
		{
			bool up = model.joined ? walk_random(4) != 0 : walk_random(8) != 0;

			conn_wifi(&walked, up, now);
			if(up && !model.joined) result->joins++;
			model_wifi(&model, up, now);
			what = "wifi";
		}
		else if(operation < 935)
		{
			bool expected = model.news_restart;

			same = conn_take_restarted(&walked) == expected;
			model.news_restart = false;
			if(expected) result->restarts++;
			what = "take_restarted";
		}
		else if(operation < 965)
		{
			// Room around the size the id needs, none at all, or no place
			char got_id[48], expected_id[48];
			size_t size = strlen(model.bound) + walk_random(3);
			bool pending = model.news_bind;
			bool got_taken, expected_taken;

			if(walk_random(6) == 0) size = walk_random(2) == 0 ? 0 : sizeof(got_id);
			memset(got_id, 'x', sizeof(got_id));
			memset(expected_id, 'x', sizeof(expected_id));
			if(walk_random(10) == 0)
			{
				got_taken = conn_take_bind(&walked, NULL, size);
				expected_taken = model_take_bind(&model, NULL, size);
			}
			else
			{
				got_taken = conn_take_bind(&walked, got_id, size);
				expected_taken = model_take_bind(&model, expected_id, size);
			}
			same = got_taken == expected_taken && memcmp(got_id, expected_id, sizeof(got_id)) == 0;
			if(expected_taken) result->binds++;
			else if(pending) result->binds_refused++;
			what = "take_bind";
		}
		else if(operation < 985)
		{
			// As after years of rounds: the counts stand just below the largest number. Nothing may change by that.
			if(walked.good_rounds >= CONN_DTC_MIN_ROUNDS)
			{
				walked.good_rounds = INT_MAX - 1;
				result->counted_on++;
			}
			if(walked.failed_rounds >= 4)
			{
				walked.failed_rounds = INT_MAX - 1;
				result->counted_on++;
			}
			what = "years of rounds";
		}
		else if(operation < 986)
		{
			bound = bounds[walk_random(9)];
			conn_init(&walked, bound);
			model_init(&model, bound);
			what = "init";
		}

		// After every call: the same view now, at a time around it and at the end of the grace time, the same
		// state, the same permission, the same news waiting to be taken, bound to the same adapter
		asked_at = now + walk_random(400);
		if(walk_random(4) == 0) asked_at = now - walk_random(20000) % (now + 1);
		same = same && conn_view(&walked, now) == model_view(&model, now) && conn_view(&walked, asked_at) == model_view(&model, asked_at) &&
		       conn_view(&walked, model.joined_at + 14999) == model_view(&model, model.joined_at + 14999) &&
		       conn_view(&walked, model.joined_at + 15000) == model_view(&model, model.joined_at + 15000);
		same = same && (conn_state(&walked) != NULL) == (model.known == KNOWN_API);
		if(same && model.known == KNOWN_API) same = walk_same_state(conn_state(&walked), &model.last);
		same = same && conn_dtc_allowed(&walked) == model_dtc_allowed(&model);
		same = same && walked.restarted == model.news_restart && walked.bind_pending == model.news_bind && strcmp(walked.bound_id, model.bound) == 0;

		result->calls++;
		result->views[model_view(&model, now)]++;
		if(model_dtc_allowed(&model)) result->allowed++;
		if(!same)
		{
			printf("  walk %lu, call %d (%s) at %llu ms: module and model differ; view %d and %d, state %d and %d, allowed %d and %d\n",
			       (unsigned long)seed, call, what, (unsigned long long)now, (int)conn_view(&walked, now), (int)model_view(&model, now),
			       conn_state(&walked) != NULL, model.known == KNOWN_API, conn_dtc_allowed(&walked), model_dtc_allowed(&model));
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
	pid_t child;

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

	printf("  walk: %ld calls; asked %ld state, %ld result, %ld catalogue, %ld values; %ld restarts, %ld ids taken, %ld refused, %ld ends ignored, "
	       "%ld steps back, %ld joins, %ld failed rounds, %ld answers without a state, %ld calls with commands allowed, %ld times years of rounds\n",
	       result.calls, result.asked[CONN_ASK_STATE], result.asked[CONN_ASK_RESULT], result.asked[CONN_ASK_CATALOG], result.asked[CONN_ASK_VALUES],
	       result.restarts, result.binds, result.binds_refused, result.ignored, result.steps_back, result.joins, result.failed_rounds, result.no_state,
	       result.allowed, result.counted_on);
	printf("  walk: views %ld no wifi, %ld connecting, %ld no answer, %ld foreign, %ld no api, %ld autopid off, %ld starting, %ld scan, %ld ecu offline, %ld live\n",
	       result.views[CONN_VIEW_NO_WIFI], result.views[CONN_VIEW_CONNECTING], result.views[CONN_VIEW_NO_ANSWER], result.views[CONN_VIEW_FOREIGN],
	       result.views[CONN_VIEW_NO_API], result.views[CONN_VIEW_AUTOPID_OFF], result.views[CONN_VIEW_STARTING], result.views[CONN_VIEW_SCAN],
	       result.views[CONN_VIEW_ECU_OFFLINE], result.views[CONN_VIEW_LIVE]);

	check(complete && status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0, "60 random walks of 50000 calls each: no crash and no hang, also with a state that is NULL");
	check(complete && result.different == 0 && result.calls == (long)WALKS * WALK_CALLS, "60 random walks of 50000 calls each: module and model agree after every call");
	check(complete && result.asked[CONN_ASK_STATE] > 20000 && result.asked[CONN_ASK_RESULT] > 2000 && result.asked[CONN_ASK_CATALOG] > 2000 &&
	      result.asked[CONN_ASK_VALUES] > 5000 && result.asked[CONN_ASK_NOTHING] > 100000, "the walk hands out every kind of request in numbers");
	check(complete && result.views[CONN_VIEW_NO_WIFI] > 5000 && result.views[CONN_VIEW_CONNECTING] > 5000 && result.views[CONN_VIEW_NO_ANSWER] > 5000 &&
	      result.views[CONN_VIEW_FOREIGN] > 5000 && result.views[CONN_VIEW_NO_API] > 5000 && result.views[CONN_VIEW_AUTOPID_OFF] > 5000 &&
	      result.views[CONN_VIEW_STARTING] > 5000 && result.views[CONN_VIEW_SCAN] > 5000 && result.views[CONN_VIEW_ECU_OFFLINE] > 5000 &&
	      result.views[CONN_VIEW_LIVE] > 5000, "the walk reaches every view in numbers");
	check(complete && result.restarts > 300 && result.binds > 30 && result.binds_refused > 30 && result.ignored > 5000 && result.steps_back > 10000 &&
	      result.joins > 1000 && result.failed_rounds > 5000 && result.no_state > 100 && result.allowed > 5000 && result.counted_on > 300,
	      "the walk reaches restarts, bindings, refused ids, ignored ends, steps back of the time, joins, failed rounds, missing states, allowed commands and years of rounds in numbers");
}

int main(void)
{
	test_constants();
	test_start();
	test_round();
	test_round_timing();
	test_result();
	test_catalog();
	test_values();
	test_backoff();
	test_no_answer();
	test_view();
	test_state();
	test_dtc_allowed();
	test_foreign();
	test_bind();
	test_restart();
	test_no_api();
	test_wifi();
	test_mismatch();
	test_walk();
	return test_end();
}
