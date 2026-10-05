/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "poll.h"

// The request for what conn asks
static const poll_kind_t KINDS[] = {
	[CONN_ASK_NOTHING] = POLL_NONE,
	[CONN_ASK_STATE] = POLL_STATE,
	[CONN_ASK_RESULT] = POLL_RESULT,
	[CONN_ASK_CATALOG] = POLL_CATALOG,
	[CONN_ASK_VALUES] = POLL_VALUES,
};

// The path of a request; the clear is written with its number
static const char *const PATHS[] = {
	[POLL_NONE] = "",
	[POLL_STATE] = "/api/state",
	[POLL_RESULT] = "/api/dtc/result",
	[POLL_CATALOG] = "/load_car_config",
	[POLL_VALUES] = "/autopid_data",
	[POLL_DTC_READ] = "/api/dtc?action=read",
};

// The own request was accepted and its result is not there yet
static bool waits(const dtc_flow_t *flow)
{
	return flow->phase == DTC_FLOW_READING || flow->phase == DTC_FLOW_CLEARING;
}

// The list and the outcome of a clear are shown as long as the flow shows them
static void follow(poll_t *poll)
{
	dtc_flow_phase_t phase = poll->flow.phase;
	bool list = poll->has_list && (phase == DTC_FLOW_LIST || phase == DTC_FLOW_CLEAR_SENT || phase == DTC_FLOW_CLEARING);
	bool cleared = poll->has_cleared && phase == DTC_FLOW_CLEARED;

	if(list != poll->has_list || cleared != poll->has_cleared) poll->events |= POLL_EVENT_LISTS;
	poll->has_list = list;
	poll->has_cleared = cleared;
}

// A clear the adapter accepted, or of which nobody knows what became of it, has erased what the list names,
// or may have: that list is the one before the last clear from now on. A clear that was refused, did not
// arrive or was never sent has erased nothing, and the list before the real last clear stays.
// before: the phase of the flow before the call that may have ended the POST of a clear.
static void keep_old(poll_t *poll, dtc_flow_phase_t before)
{
	const dtc_flow_t *flow = &poll->flow;

	// A number of its own tells that the adapter accepted the clear, also when the state that showed it showed
	// its error at once. Without one the clear still waits for its answer, or it has ended without an effect.
	if(before != DTC_FLOW_CLEAR_SENT || (flow->seq == 0 && flow->phase != DTC_FLOW_UNKNOWN)) return;

	poll->old = poll->list;
	strcpy(poll->old_text, poll->list_text);
	poll->has_old = true;
	poll->events |= POLL_EVENT_OLD | POLL_EVENT_LISTS;
}

// An adapter that cannot be reached ends the fault memory request of the display, once per outage
static void watch(poll_t *poll, uint64_t now_ms)
{
	conn_view_t view = conn_view(&poll->conn, now_ms);
	bool out = view == CONN_VIEW_NO_WIFI || view == CONN_VIEW_NO_ANSWER;

	if(out && !poll->lost) dtc_flow_lost(&poll->flow);
	poll->lost = out;
}

void poll_init(poll_t *poll, const char *bound_id)
{
	memset(poll, 0, sizeof(*poll));
	conn_init(&poll->conn, bound_id);
	// conn cuts an id that is too long: both hold the same text
	strcpy(poll->bound_id, poll->conn.bound_id);
	values_init(&poll->values);
	catalog_init(&poll->catalog);
	guard_catalog_init(&poll->catalog_guard, false, 0);
	dtc_flow_init(&poll->flow);
}

void poll_stored(poll_t *poll, const char *catalog_json, size_t catalog_length, const char *old_text,
                 size_t old_length, json_token_t *work, int work_count)
{
	if(catalog_json != NULL && catalog_from_json(&poll->catalog, catalog_json, catalog_length, work, work_count))
	{
		guard_catalog_init(&poll->catalog_guard, true, catalog_checksum(&poll->catalog));
	}
	// A text without room here could not be handed on to the web interface
	if(old_text != NULL && old_length < sizeof(poll->old_text) && dtc_result_parse(old_text, old_length, &poll->old, work, work_count))
	{
		memcpy(poll->old_text, old_text, old_length);
		poll->old_text[old_length] = '\0';
		poll->has_old = true;
		poll->events |= POLL_EVENT_LISTS;
	}
}

void poll_wifi(poll_t *poll, bool up, uint64_t now_ms)
{
	dtc_flow_phase_t before = poll->flow.phase;

	if(up == poll->wifi) return;

	poll->wifi = up;
	poll->asking = false;
	conn_wifi(&poll->conn, up, now_ms);
	if(up)
	{
		// The adapter may have restarted while it was out of sight: its pass counter means nothing now, and
		// conn asks for its profile again
		values_clear(&poll->values);
		poll->catalog_complete = false;
		guard_catalog_connected(&poll->catalog_guard);
	}
	watch(poll, now_ms);
	keep_old(poll, before);
	follow(poll);
}

bool poll_prepare(poll_t *poll, uint64_t now_ms, poll_request_t *request)
{
	poll_kind_t kind = POLL_NONE;
	uint32_t seq = 0;

	if(poll->wifi && !poll->asking)
	{
		dtc_flow_send_t send = dtc_flow_take(&poll->flow, &seq, now_ms);

		if(send == DTC_FLOW_SEND_READ) kind = POLL_DTC_READ;
		else if(send == DTC_FLOW_SEND_CLEAR) kind = POLL_DTC_CLEAR;
		else kind = KINDS[conn_next(&poll->conn, now_ms)];
	}

	request->kind = kind;
	request->post = kind == POLL_DTC_READ || kind == POLL_DTC_CLEAR;
	if(kind == POLL_DTC_CLEAR) snprintf(request->path, sizeof(request->path), "/api/dtc?action=clear&seq=%" PRIu32, seq);
	else strcpy(request->path, PATHS[kind]);
	if(kind == POLL_NONE) return false;

	if(kind == POLL_RESULT)
	{
		// The state may be another one by the time the answer is there
		poll->asked_result_seq = poll->conn.state.dtc.result_seq;
		poll->asked_age_s = poll->conn.state.dtc.age_s;
	}
	poll->asking = true;
	poll->asked = kind;
	return true;
}

// The adapter is another one, or the same after a restart: what came from the one before is void
static void forget(poll_t *poll)
{
	values_clear(&poll->values);
	// A profile of another vehicle must not leave entries behind: what was delivered once would stay for ever
	catalog_init(&poll->catalog);
	poll->catalog_complete = false;
	guard_catalog_connected(&poll->catalog_guard);
	poll->events |= POLL_EVENT_FORGET | POLL_EVENT_LISTS;
	// A restart that shows in the boot number has ended the flow already; this is for the other adapter and
	// the other firmware. What stays is a failure, which the user has to see. follow() drops the lists.
	dtc_flow_lost(&poll->flow);
	if(poll->flow.phase == DTC_FLOW_LIST || poll->flow.phase == DTC_FLOW_CLEARED) dtc_flow_dismiss(&poll->flow);
}

// The battery voltage as a value like those of the vehicle, in volts
static void battery(poll_t *poll, int32_t millivolts, uint64_t now_ms, json_token_t *work, int work_count)
{
	char json[48];

	// Written from the digits, so that 12400 is the same number as the 12.4 of the adapter. Without a pass
	// counter values_apply() takes the answer as new and leaves the counter of the vehicle values alone.
	snprintf(json, sizeof(json), "{\"" CATALOG_BATTERY "\":%" PRId32 ".%03" PRId32 "}", millivolts / 1000, millivolts % 1000);
	values_apply(&poll->values, json, strlen(json), -1, now_ms, work, work_count);
}

static void got_state(poll_t *poll, int status, const char *body, size_t length, uint64_t now_ms, json_token_t *work, int work_count)
{
	wican_state_t state;
	bool read = status == 200 && wican_state_parse(body, length, &state, work, work_count);
	bool restarted;

	if(read)
	{
		conn_got_state(&poll->conn, CONN_GOT_OK, &state, now_ms);
		dtc_flow_state(&poll->flow, &state, now_ms);
	}
	else
	{
		conn_got_state(&poll->conn, status == 404 ? CONN_GOT_NOT_FOUND : CONN_GOT_FAILED, NULL, now_ms);
	}

	if(conn_take_bind(&poll->conn, poll->bound_id, sizeof(poll->bound_id))) poll->events |= POLL_EVENT_BOUND;
	restarted = conn_take_restarted(&poll->conn);
	// conn knows the adapter only since the network was joined. One that restarted while the display was out of
	// the network is seen here: by the start the catalogue came from, which is kept over the pause. A foreign
	// adapter gives nothing to the catalogue and is no such start.
	if(read && !poll->conn.foreign)
	{
		if(poll->start == POLL_START_NO_API) restarted = true;
		if(poll->start == POLL_START_API && (state.boot != poll->start_boot || strcmp(state.id, poll->start_id) != 0)) restarted = true;
		poll->start = POLL_START_API;
		strcpy(poll->start_id, state.id);
		poll->start_boot = state.boot;
	}
	else if(status == 404)
	{
		// A firmware without the API where one with it answered before
		if(poll->start == POLL_START_API) restarted = true;
		poll->start = POLL_START_NO_API;
	}
	if(restarted) forget(poll);
	// Behind the forgetting: this voltage is one of the adapter that answers now. That of a foreign adapter
	// is no value of the vehicle the display belongs to.
	if(read && !poll->conn.foreign && state.batt_mv >= 0) battery(poll, state.batt_mv, now_ms, work, work_count);
}

static void got_result(poll_t *poll, int status, const char *body, size_t length, const char *seq_header, uint64_t now_ms,
                       json_token_t *work, int work_count)
{
	// A list is shown only while no outcome of a clear is: the room of the other one is free to read into
	dtc_result_t *room = poll->has_list ? &poll->cleared : &poll->list;
	dtc_flow_phase_t before = poll->flow.phase;
	bool own = waits(&poll->flow) && poll->flow.seq == poll->asked_result_seq;
	char number[12];

	if(status == 0)
	{
		conn_got_result(&poll->conn, CONN_GOT_FAILED, now_ms);
		return;
	}

	snprintf(number, sizeof(number), "%" PRIu32, poll->asked_result_seq);
	if(status == 200 && seq_header != NULL && strcmp(seq_header, number) == 0 && length < sizeof(poll->list_text) &&
	   dtc_result_parse(body, length, room, work, work_count))
	{
		conn_got_result(&poll->conn, CONN_GOT_OK, now_ms);
		dtc_flow_result(&poll->flow, poll->asked_result_seq, room->clear, room->dtc_count, poll->asked_age_s, now_ms);
		// A result makes a list of READING and an outcome of CLEARING, nothing else
		if(poll->flow.phase != before)
		{
			if(poll->flow.phase == DTC_FLOW_LIST)
			{
				memcpy(poll->list_text, body, length);
				poll->list_text[length] = '\0';
				poll->has_list = true;
			}
			else
			{
				poll->has_cleared = true;
			}
			poll->events |= POLL_EVENT_LISTS;
		}
	}
	else
	{
		// Asking again would bring the same: the rounds have to go on
		conn_got_result(&poll->conn, CONN_GOT_NOT_FOUND, now_ms);
	}
	// conn does not ask for this result again. A flow left waiting for it would wait until its time is over.
	if(own && waits(&poll->flow)) dtc_flow_no_result(&poll->flow);
}

static void got_catalog(poll_t *poll, int status, const char *body, size_t length, uint64_t now_ms, json_token_t *work, int work_count)
{
	const wican_state_t *state = conn_state(&poll->conn);
	conn_got_t got = CONN_GOT_FAILED;

	if(status == 200)
	{
		// A body that cannot be used would be the same next time: conn must not ask for it every round
		if(catalog_apply_config(&poll->catalog, body, length, work, work_count)) poll->catalog_complete = true;
		got = CONN_GOT_OK;
	}
	// Without AutoPID the adapter has no profile and says so with a status 500 (API.md), which conn asks for
	// all the same. That is an answer and no failed round: the display would show "no answer" for it.
	else if(status == 404 || (status != 0 && state != NULL && state->autopid == WICAN_AUTOPID_OFF))
	{
		got = CONN_GOT_NOT_FOUND;
	}
	conn_got_catalog(&poll->conn, got, now_ms);
}

static void got_values(poll_t *poll, int status, const char *body, size_t length, uint64_t now_ms, json_token_t *work, int work_count)
{
	const wican_state_t *state = conn_state(&poll->conn);
	values_result_t result = VALUES_INVALID;

	// A firmware without the API has no counter that tells a repeated answer from a new one
	if(status == 200) result = values_apply(&poll->values, body, length, state != NULL ? (int64_t)state->pass : -1, now_ms, work, work_count);
	if(result == VALUES_RENEWED) catalog_note_values(&poll->catalog, &poll->values);
	conn_got_values(&poll->conn, result == VALUES_INVALID ? CONN_GOT_FAILED : CONN_GOT_OK, now_ms);
}

static void got_posted(poll_t *poll, int status, const char *body, size_t length, uint64_t now_ms, json_token_t *work, int work_count)
{
	// What the flow keeps of a reason. json_text() leaves it empty if it refuses the text.
	char reason[sizeof(poll->flow.reason)] = "";
	int64_t seq = 0;

	if(json_parse(body, length, work, work_count) > 0)
	{
		int number = json_member(body, work, 0, "seq");
		int text = json_member(body, work, 0, "reason");

		if(number < 0 || !json_integer(body, &work[number], &seq) || seq < 0 || seq > UINT32_MAX) seq = 0;
		if(text >= 0) json_text(body, &work[text], reason, sizeof(reason));
	}
	// A refusal has to say why, also one of a server in between that knows nothing of the adapter's reasons
	if(reason[0] == '\0' && status != 202) snprintf(reason, sizeof(reason), "http_%d", status);
	dtc_flow_posted(&poll->flow, status, (uint32_t)seq, reason, now_ms);
}

void poll_apply(poll_t *poll, const poll_request_t *request, int status, const char *body, size_t length,
                const char *seq_header, uint64_t now_ms, json_token_t *work, int work_count)
{
	dtc_flow_phase_t before = poll->flow.phase;

	// An answer to anything but the request under way would be read as something it is not
	if(!poll->asking || request == NULL || request->kind != poll->asked) return;

	poll->asking = false;
	// What an HTTP client reports when it has no status. Taken for a status it would be a refusal of the
	// adapter, and a clear that may have arrived would be shown as one that failed.
	if(status < 0) status = 0;
	if(body == NULL)
	{
		body = "";
		length = 0;
	}

	if(poll->asked == POLL_STATE) got_state(poll, status, body, length, now_ms, work, work_count);
	else if(poll->asked == POLL_RESULT) got_result(poll, status, body, length, seq_header, now_ms, work, work_count);
	else if(poll->asked == POLL_CATALOG) got_catalog(poll, status, body, length, now_ms, work, work_count);
	else if(poll->asked == POLL_VALUES) got_values(poll, status, body, length, now_ms, work, work_count);
	else got_posted(poll, status, body, length, now_ms, work, work_count);

	watch(poll, now_ms);
	keep_old(poll, before);
	follow(poll);
	if(guard_catalog_due(&poll->catalog_guard, catalog_checksum(&poll->catalog), poll->catalog_complete, now_ms))
	{
		poll->events |= POLL_EVENT_CATALOG;
	}
	if(status >= 200 && status <= 499) poll->http_ok++;
	else poll->http_failed++;
}

uint32_t poll_take_events(poll_t *poll)
{
	uint32_t events = poll->events;

	poll->events = 0;
	return events;
}

dtc_flow_block_t poll_read(poll_t *poll, uint64_t now_ms)
{
	dtc_flow_block_t block = dtc_flow_read(&poll->flow, &poll->conn, &poll->values, &poll->catalog, now_ms);

	// A read drops the list that was shown
	follow(poll);
	return block;
}

dtc_flow_block_t poll_clear(poll_t *poll, bool button_stuck, uint64_t now_ms)
{
	return dtc_flow_clear(&poll->flow, &poll->conn, &poll->values, &poll->catalog, button_stuck, now_ms);
}

void poll_dismiss(poll_t *poll)
{
	dtc_flow_dismiss(&poll->flow);
	follow(poll);
}
