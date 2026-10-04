/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include <limits.h>
#include "conn.h"

static const uint32_t BACKOFF[] = CONN_BACKOFF_MS;

#define BACKOFF_COUNT   ((int)(sizeof(BACKOFF) / sizeof(BACKOFF[0])))

// A time before the stored one counts as no time passed
static uint64_t passed(uint64_t now_ms, uint64_t since_ms)
{
	return now_ms > since_ms ? now_ms - since_ms : 0;
}

// As much of the text as fits
static void copy_text(char *out, size_t size, const char *text)
{
	size_t length = 0;

	while(length + 1 < size && text[length] != '\0')
	{
		out[length] = text[length];
		length++;
	}
	out[length] = '\0';
}

static bool scanning(const conn_t *conn)
{
	return conn->has_state && (conn->state.dtc.phase == WICAN_DTC_QUEUED || conn->state.dtc.phase == WICAN_DTC_RUNNING);
}

// The request of a round that comes behind `asked`, CONN_ASK_NOTHING if `asked` was the last one.
// The requests of a round go out in the order of the enum.
static conn_ask_t following(const conn_t *conn, conn_ask_t asked)
{
	if(asked < CONN_ASK_RESULT && conn->want_result) return CONN_ASK_RESULT;
	// The catalogue waits for the end of a polling pass, and a scan stops the polling
	if(asked < CONN_ASK_CATALOG && conn->want_catalog && !conn->foreign && !scanning(conn)) return CONN_ASK_CATALOG;
	if(asked < CONN_ASK_VALUES && conn->want_values) return CONN_ASK_VALUES;
	return CONN_ASK_NOTHING;
}

// The request under way was answered. The round ends here if nothing is left to ask.
static void answered(conn_t *conn)
{
	if(following(conn, conn->asked) != CONN_ASK_NOTHING) return;

	conn->asked = CONN_ASK_NOTHING;
	conn->failed_rounds = 0;
	conn->next_round_ms = conn->round_start_ms + CONN_ROUND_MS;
}

// The time a request of the running round ended: not before the round began
static uint64_t ended(const conn_t *conn, uint64_t now_ms)
{
	return conn->round_start_ms + passed(now_ms, conn->round_start_ms);
}

// The request under way failed, and its round with it: what was left of the round is not asked. An adapter
// that does not answer one request will not answer the next, and every request waits for its own timeout.
static void failed(conn_t *conn, uint64_t now_ms)
{
	conn->asked = CONN_ASK_NOTHING;
	conn->good_rounds = 0;
	if(conn->failed_rounds < INT_MAX) conn->failed_rounds++;
	conn->next_round_ms = ended(conn, now_ms) + BACKOFF[(conn->failed_rounds < BACKOFF_COUNT ? conn->failed_rounds : BACKOFF_COUNT) - 1];
}

// true if the request under way is `asked`: its end is taken. Any other end belongs to no request.
static bool ends(conn_t *conn, conn_ask_t asked)
{
	if(!conn->asking || conn->asked != asked) return false;

	conn->asking = false;
	return true;
}

// The adapter is not the one of the last answer any more: what was fetched from that one is void
static void replaced(conn_t *conn)
{
	conn->restarted = true;
	conn->want_catalog = true;
	conn->fetched_result_seq = 0;
}

void conn_init(conn_t *conn, const char *bound_id)
{
	memset(conn, 0, sizeof(*conn));
	if(bound_id != NULL) copy_text(conn->bound_id, sizeof(conn->bound_id), bound_id);
}

void conn_wifi(conn_t *conn, bool up, uint64_t now_ms)
{
	if(up == conn->wifi) return;

	// Nothing about the adapter outlasts the network it was seen in. What the caller has not taken yet stays.
	// foreign, want_result and want_values are set by every answered state before they are used.
	conn->wifi = up;
	conn->wifi_since_ms = now_ms;
	conn->asking = false;
	conn->asked = CONN_ASK_NOTHING;
	conn->next_round_ms = 0;
	conn->failed_rounds = 0;
	conn->good_rounds = 0;
	conn->has_state = false;
	conn->no_api = false;
	conn->want_catalog = true;
	conn->fetched_result_seq = 0;
}

conn_ask_t conn_next(conn_t *conn, uint64_t now_ms)
{
	conn_ask_t ask = CONN_ASK_STATE;

	if(!conn->wifi || conn->asking) return CONN_ASK_NOTHING;

	if(conn->asked != CONN_ASK_NOTHING)
	{
		// In the middle of a round: answered() has seen that there is something left
		ask = following(conn, conn->asked);
	}
	else
	{
		if(now_ms < conn->next_round_ms) return CONN_ASK_NOTHING;

		conn->round_start_ms = now_ms;
		// A firmware without the API is asked for it only now and then, its other rounds begin behind the state
		if(conn->no_api && passed(now_ms, conn->no_api_since_ms) < CONN_NO_API_RECHECK_MS) ask = following(conn, CONN_ASK_STATE);
	}

	conn->asked = ask;
	conn->asking = true;
	return ask;
}

static void take_state(conn_t *conn, const wican_state_t *state)
{
	if(conn->has_state)
	{
		if(state->boot != conn->state.boot || strcmp(state->id, conn->state.id) != 0) replaced(conn);
		else if(state->pids != conn->state.pids) conn->want_catalog = true;
	}
	// A firmware that had no API and now has one is another firmware as well
	else if(conn->no_api) replaced(conn);

	// An adapter without an id cannot be told from others: nothing to bind to
	if(conn->bound_id[0] == '\0' && state->id[0] != '\0')
	{
		copy_text(conn->bound_id, sizeof(conn->bound_id), state->id);
		conn->bind_pending = true;
	}
	// An unbound display is here only with an adapter without an id: the two empty texts are equal
	conn->foreign = strcmp(state->id, conn->bound_id) != 0;

	conn->state = *state;
	conn->has_state = true;
	conn->no_api = false;
	if(conn->good_rounds < INT_MAX) conn->good_rounds++;

	conn->want_result = !conn->foreign && state->dtc.result_seq != 0 && state->dtc.result_seq != conn->fetched_result_seq;
	conn->want_values = !conn->foreign && state->autopid == WICAN_AUTOPID_RUN && state->ecu_online && !scanning(conn);
}

void conn_got_state(conn_t *conn, conn_got_t got, const wican_state_t *state, uint64_t now_ms)
{
	if(!ends(conn, CONN_ASK_STATE)) return;

	if(got == CONN_GOT_OK && state != NULL)
	{
		take_state(conn, state);
	}
	else if(got == CONN_GOT_NOT_FOUND)
	{
		// The firmware that answered before had the API: this is another one
		if(conn->has_state) replaced(conn);
		conn->has_state = false;
		conn->no_api = true;
		conn->no_api_since_ms = ended(conn, now_ms);
		conn->foreign = false;
		conn->good_rounds = 0;
		// Nothing says whether AutoPID runs: the values are asked for in any case
		conn->want_result = false;
		conn->want_values = true;
	}
	else
	{
		failed(conn, now_ms);
		return;
	}
	answered(conn);
}

void conn_got_result(conn_t *conn, conn_got_t got, uint64_t now_ms)
{
	if(!ends(conn, CONN_ASK_RESULT)) return;

	if(got != CONN_GOT_OK && got != CONN_GOT_NOT_FOUND)
	{
		failed(conn, now_ms);
		return;
	}
	conn->fetched_result_seq = conn->state.dtc.result_seq;
	answered(conn);
}

void conn_got_catalog(conn_t *conn, conn_got_t got, uint64_t now_ms)
{
	if(!ends(conn, CONN_ASK_CATALOG)) return;

	if(got != CONN_GOT_OK && got != CONN_GOT_NOT_FOUND)
	{
		failed(conn, now_ms);
		return;
	}
	if(got == CONN_GOT_OK) conn->want_catalog = false;
	answered(conn);
}

void conn_got_values(conn_t *conn, conn_got_t got, uint64_t now_ms)
{
	if(!ends(conn, CONN_ASK_VALUES)) return;

	if(got != CONN_GOT_OK)
	{
		failed(conn, now_ms);
		return;
	}
	answered(conn);
}

conn_view_t conn_view(const conn_t *conn, uint64_t now_ms)
{
	if(!conn->wifi) return CONN_VIEW_NO_WIFI;
	if(conn->failed_rounds >= CONN_FAILED_ROUNDS && passed(now_ms, conn->wifi_since_ms) >= CONN_GRACE_MS) return CONN_VIEW_NO_ANSWER;
	// A 404 of /api/state is an answer as well
	if(!conn->has_state && !conn->no_api) return CONN_VIEW_CONNECTING;
	if(conn->foreign) return CONN_VIEW_FOREIGN;
	if(conn->no_api) return CONN_VIEW_NO_API;
	if(conn->state.autopid == WICAN_AUTOPID_OFF) return CONN_VIEW_AUTOPID_OFF;
	// Also a value this display does not know: nothing says that AutoPID runs
	if(conn->state.autopid != WICAN_AUTOPID_RUN) return CONN_VIEW_STARTING;
	if(scanning(conn)) return CONN_VIEW_SCAN;
	if(!conn->state.ecu_online) return CONN_VIEW_ECU_OFFLINE;
	return CONN_VIEW_LIVE;
}

const wican_state_t *conn_state(const conn_t *conn)
{
	return conn->has_state ? &conn->state : NULL;
}

bool conn_dtc_allowed(const conn_t *conn)
{
	// good_rounds counts answered states since the last round without one: there is a state if it is not 0
	return conn->good_rounds >= CONN_DTC_MIN_ROUNDS && !conn->foreign && conn->state.autopid == WICAN_AUTOPID_RUN &&
	       conn->state.up_s >= CONN_DTC_MIN_UP_S;
}

bool conn_take_restarted(conn_t *conn)
{
	bool restarted = conn->restarted;

	conn->restarted = false;
	return restarted;
}

bool conn_take_bind(conn_t *conn, char *id, size_t size)
{
	// An id cut short would be stored as that of another adapter: it stays to be taken with enough room
	if(!conn->bind_pending || id == NULL || strlen(conn->bound_id) >= size) return false;

	strcpy(id, conn->bound_id);
	conn->bind_pending = false;
	return true;
}
