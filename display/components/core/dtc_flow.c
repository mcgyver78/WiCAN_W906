/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "dtc_flow.h"

// A time before the stored one counts as no time passed
static uint64_t passed(uint64_t now_ms, uint64_t since_ms)
{
	return now_ms > since_ms ? now_ms - since_ms : 0;
}

static bool clearing(const dtc_flow_t *flow)
{
	return flow->phase == DTC_FLOW_CLEAR_SENT || flow->phase == DTC_FLOW_CLEARING;
}

static bool under_way(const dtc_flow_t *flow)
{
	return flow->phase == DTC_FLOW_READ_SENT || flow->phase == DTC_FLOW_READING || clearing(flow);
}

// The POST ended without an answer, the states have to tell what became of it
static bool unanswered(const dtc_flow_t *flow)
{
	return (flow->phase == DTC_FLOW_READ_SENT || flow->phase == DTC_FLOW_CLEAR_SENT) && flow->posted;
}

// The own request failed. reason: as much of the text as fits, none if NULL.
static void fail(dtc_flow_t *flow, const char *reason)
{
	size_t length = 0;

	while(reason != NULL && length + 1 < sizeof(flow->reason) && reason[length] != '\0')
	{
		flow->reason[length] = reason[length];
		length++;
	}
	flow->reason[length] = '\0';
	flow->phase = DTC_FLOW_FAILED;
}

// The own request cannot be followed any more. One that was not taken yet is not sent late; of a clear
// nobody can say what it did.
static void give_up(dtc_flow_t *flow, const char *reason)
{
	flow->to_send = DTC_FLOW_SEND_NOTHING;
	if(clearing(flow)) flow->phase = DTC_FLOW_UNKNOWN;
	else fail(flow, reason);
}

static void drop_list(dtc_flow_t *flow)
{
	flow->phase = DTC_FLOW_IDLE;
	flow->read_seq = 0;
	flow->list_count = 0;
	flow->list_end_ms = 0;
}

void dtc_flow_init(dtc_flow_t *flow)
{
	memset(flow, 0, sizeof(*flow));
}

// What stands against a command whatever the engine does
static dtc_flow_block_t adapter_block(const dtc_flow_t *flow, const conn_t *conn, uint64_t now_ms)
{
	conn_view_t view = conn_view(conn, now_ms);

	if(view == CONN_VIEW_NO_WIFI || view == CONN_VIEW_CONNECTING || view == CONN_VIEW_NO_ANSWER) return DTC_FLOW_NO_ADAPTER;
	if(view == CONN_VIEW_FOREIGN) return DTC_FLOW_FOREIGN;
	if(view == CONN_VIEW_NO_API) return DTC_FLOW_NO_API;
	if(view == CONN_VIEW_AUTOPID_OFF) return DTC_FLOW_AUTOPID_OFF;
	// Also while AutoPID is starting: conn allows no command then
	if(!conn_dtc_allowed(conn)) return DTC_FLOW_STARTING;
	// From here on there is a state: conn_dtc_allowed() needs one
	if(!conn_state(conn)->dtc.supported) return DTC_FLOW_NOT_SUPPORTED;
	if(view == CONN_VIEW_SCAN || under_way(flow)) return DTC_FLOW_BUSY;
	if(view == CONN_VIEW_ECU_OFFLINE) return DTC_FLOW_ECU_OFFLINE;
	return DTC_FLOW_ALLOWED;
}

// The engine as far as the display can tell. newer: the value has to be seen later than than_ms.
static dtc_flow_block_t engine_block(const values_t *values, const catalog_t *catalog, bool newer, uint64_t than_ms, uint64_t now_ms)
{
	const value_t *value;
	bool fresh;

	// A profile without an engine speed: the adapter decides
	if(catalog_find(catalog, DTC_FLOW_RPM_NAME) < 0) return DTC_FLOW_ALLOWED;

	// values_age() takes a value that is not there as gone. A switch ("on" / "off") is no speed.
	value = values_find(values, DTC_FLOW_RPM_NAME);
	fresh = values_age(value, now_ms) == VALUE_AGE_FRESH && value->kind == VALUE_NUMBER;
	// Not "number >= limit": what is no number at all must not pass for an engine that stands
	if(fresh && !(value->number < DTC_FLOW_RPM_LIMIT)) return DTC_FLOW_ENGINE_RUNNING;
	if(!fresh || (newer && value->seen_ms <= than_ms)) return DTC_FLOW_RPM_UNKNOWN;
	return DTC_FLOW_ALLOWED;
}

dtc_flow_block_t dtc_flow_read_block(const dtc_flow_t *flow, const conn_t *conn, const values_t *values,
                                     const catalog_t *catalog, uint64_t now_ms)
{
	dtc_flow_block_t block = adapter_block(flow, conn, now_ms);

	return block != DTC_FLOW_ALLOWED ? block : engine_block(values, catalog, false, 0, now_ms);
}

dtc_flow_block_t dtc_flow_clear_block(const dtc_flow_t *flow, const conn_t *conn, const values_t *values,
                                      const catalog_t *catalog, bool button_stuck, uint64_t now_ms)
{
	dtc_flow_block_t block = adapter_block(flow, conn, now_ms);
	const wican_state_t *state = conn_state(conn);
	bool list;

	if(block != DTC_FLOW_ALLOWED) return block;

	// The list is void as soon as the adapter shows another boot number or another request, also before
	// dtc_flow_state() was told
	list = flow->phase == DTC_FLOW_LIST && state->boot == flow->boot && state->dtc.seq == flow->read_seq;
	block = engine_block(values, catalog, list, flow->list_end_ms, now_ms);
	if(block != DTC_FLOW_ALLOWED) return block;

	if(!list) return DTC_FLOW_NO_LIST;
	if(passed(now_ms, flow->list_end_ms) > DTC_FLOW_LIST_MS) return DTC_FLOW_LIST_OLD;
	if(flow->list_count == 0) return DTC_FLOW_NO_CODES;
	if(button_stuck) return DTC_FLOW_BUTTON_STUCK;
	return DTC_FLOW_ALLOWED;
}

// A request of the display begins: what the adapter shows now is what it is told apart from later
static void begin(dtc_flow_t *flow, const wican_state_t *state, dtc_flow_phase_t phase, dtc_flow_send_t send)
{
	flow->phase = phase;
	flow->to_send = send;
	flow->boot = state->boot;
	flow->seq_before = state->dtc.seq;
	flow->seq = 0;
	flow->posted = false;
	flow->rounds_without_answer = 0;
}

dtc_flow_block_t dtc_flow_read(dtc_flow_t *flow, const conn_t *conn, const values_t *values,
                               const catalog_t *catalog, uint64_t now_ms)
{
	dtc_flow_block_t block = dtc_flow_read_block(flow, conn, values, catalog, now_ms);

	if(block != DTC_FLOW_ALLOWED) return block;

	drop_list(flow);
	begin(flow, conn_state(conn), DTC_FLOW_READ_SENT, DTC_FLOW_SEND_READ);
	return DTC_FLOW_ALLOWED;
}

dtc_flow_block_t dtc_flow_clear(dtc_flow_t *flow, const conn_t *conn, const values_t *values,
                                const catalog_t *catalog, bool button_stuck, uint64_t now_ms)
{
	dtc_flow_block_t block = dtc_flow_clear_block(flow, conn, values, catalog, button_stuck, now_ms);

	if(block != DTC_FLOW_ALLOWED) return block;

	begin(flow, conn_state(conn), DTC_FLOW_CLEAR_SENT, DTC_FLOW_SEND_CLEAR);
	return DTC_FLOW_ALLOWED;
}

dtc_flow_send_t dtc_flow_take(dtc_flow_t *flow, uint32_t *seq, uint64_t now_ms)
{
	dtc_flow_send_t send = flow->to_send;

	flow->to_send = DTC_FLOW_SEND_NOTHING;
	// The clear waited for the caller until its list is one that is not offered any more
	if(send == DTC_FLOW_SEND_CLEAR && passed(now_ms, flow->list_end_ms) > DTC_FLOW_LIST_MS)
	{
		flow->phase = DTC_FLOW_LIST;
		send = DTC_FLOW_SEND_NOTHING;
	}
	if(seq != NULL) *seq = send == DTC_FLOW_SEND_CLEAR ? flow->read_seq : 0;
	return send;
}

void dtc_flow_posted(dtc_flow_t *flow, int status, uint32_t seq, const char *reason, uint64_t now_ms)
{
	(void)now_ms;
	// Only a request that was taken can have an answer, and it has one answer
	if((flow->phase != DTC_FLOW_READ_SENT && flow->phase != DTC_FLOW_CLEAR_SENT) || flow->to_send != DTC_FLOW_SEND_NOTHING || flow->posted) return;

	flow->posted = true;
	if(status == 202 && seq != 0)
	{
		flow->seq = seq;
		flow->phase = clearing(flow) ? DTC_FLOW_CLEARING : DTC_FLOW_READING;
	}
	// Accepted without a number is as good as no answer: the states have to tell
	else if(status != 0 && status != 202)
	{
		fail(flow, reason);
	}
}

void dtc_flow_state(dtc_flow_t *flow, const wican_state_t *state, uint64_t now_ms)
{
	bool clear = clearing(flow);

	(void)now_ms;
	// A failure and an unknown outcome stay as they are until the user leaves them
	if(state == NULL || flow->phase == DTC_FLOW_FAILED || flow->phase == DTC_FLOW_UNKNOWN) return;

	if(state->boot != flow->boot)
	{
		// The numbers of another boot mean nothing
		if(under_way(flow)) give_up(flow, "restarted");
		else drop_list(flow);
		return;
	}

	if(unanswered(flow))
	{
		if(state->dtc.seq == flow->seq_before)
		{
			if(++flow->rounds_without_answer < DTC_FLOW_NO_ANSWER_ROUNDS) return;

			// It did not arrive. Nothing was cleared: the list is as good as before.
			if(clear) flow->phase = DTC_FLOW_LIST;
			else fail(flow, "no_answer");
			return;
		}
		// Another number, but not of the request that was sent: somebody else got in, and what became of
		// the own request cannot be told
		if(state->dtc.seq == 0 || !state->dtc.has_request || !state->dtc.from_http || state->dtc.clear != clear)
		{
			give_up(flow, "superseded");
			return;
		}
		flow->seq = state->dtc.seq;
		flow->phase = clear ? DTC_FLOW_CLEARING : DTC_FLOW_READING;
	}

	if(flow->phase == DTC_FLOW_READING || flow->phase == DTC_FLOW_CLEARING)
	{
		if(state->dtc.seq == flow->seq)
		{
			if(state->dtc.phase == WICAN_DTC_ERROR) fail(flow, state->dtc.reason);
		}
		// A later request. The own one is over; its result may still be there to be fetched.
		else if(state->dtc.result_seq != flow->seq)
		{
			give_up(flow, "superseded");
		}
	}
	else if(flow->phase == DTC_FLOW_LIST && state->dtc.seq != flow->read_seq)
	{
		drop_list(flow);
	}
}

void dtc_flow_result(dtc_flow_t *flow, uint32_t result_seq, bool clear, uint32_t count, uint32_t age_s, uint64_t now_ms)
{
	uint64_t age_ms = (uint64_t)age_s * 1000;

	if(result_seq != flow->seq) return;

	if(flow->phase == DTC_FLOW_READING && !clear)
	{
		flow->phase = DTC_FLOW_LIST;
		flow->read_seq = result_seq;
		flow->list_count = count;
		flow->list_end_ms = passed(now_ms, age_ms);
	}
	else if(flow->phase == DTC_FLOW_CLEARING && clear)
	{
		flow->phase = DTC_FLOW_CLEARED;
	}
}

void dtc_flow_lost(dtc_flow_t *flow)
{
	if(under_way(flow)) give_up(flow, "no_answer");
}

void dtc_flow_dismiss(dtc_flow_t *flow)
{
	if(!under_way(flow)) drop_list(flow);
}

uint32_t dtc_flow_seconds_left(const dtc_flow_t *flow, uint64_t now_ms)
{
	uint64_t age_ms = passed(now_ms, flow->list_end_ms);

	if(flow->phase != DTC_FLOW_LIST || age_ms >= DTC_FLOW_LIST_MS) return 0;
	// Rounded up: 0 only when no time is left
	return (uint32_t)((DTC_FLOW_LIST_MS - age_ms + 999) / 1000);
}
