/*
 * This file is part of the WiCAN project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "dtc_state.h"

static uint32_t seq_after(uint32_t seq)
{
	return seq >= DTC_SEQ_MAX ? 1 : seq + 1;
}

static uint64_t elapsed_ms(uint64_t now_ms, uint64_t then_ms)
{
	return now_ms >= then_ms ? now_ms - then_ms : 0;
}

void dtc_state_init(dtc_state_t *s, uint32_t seed)
{
	memset(s, 0, sizeof(*s));
	s->phase = DTC_STATE_IDLE;
	s->next_seq = seed & DTC_SEQ_MAX;
	if(s->next_seq == 0) s->next_seq = 1;
}

bool dtc_state_busy(const dtc_state_t *s)
{
	return s->phase == DTC_STATE_QUEUED || s->phase == DTC_STATE_RUNNING;
}

dtc_accept_t dtc_state_try_begin(dtc_state_t *s, bool clear, dtc_src_t src, bool check_seq, uint32_t seq,
                                 uint64_t now_ms, uint32_t *seq_out)
{
	if(seq_out != NULL) *seq_out = s->seq;

	// Commands during a scan are rejected instead of queued, a queued clear could run much later
	if(dtc_state_busy(s)) return DTC_REJECT_BUSY;

	if(clear && (check_seq || src == DTC_SRC_HTTP))
	{
		// Only the list that was just read may be cleared: the last finished request has to be a read
		if(s->phase != DTC_STATE_DONE) return DTC_REJECT_READ_REQUIRED;
		if(s->clear) return DTC_REJECT_READ_REQUIRED;
		if(elapsed_ms(now_ms, s->finished_ms) > DTC_CLEAR_MAX_AGE_MS) return DTC_REJECT_READ_REQUIRED;
		if(seq != s->seq) return DTC_REJECT_STALE_SEQ;
		if(s->result_count == 0) return DTC_REJECT_NOTHING_TO_CLEAR;
	}

	s->seq = s->next_seq;
	s->next_seq = seq_after(s->next_seq);
	s->phase = DTC_STATE_QUEUED;
	s->clear = clear;
	s->src = src;
	s->queued_ms = now_ms;
	s->step = 0;
	s->total = 0;
	s->reason = NULL;

	if(seq_out != NULL) *seq_out = s->seq;
	return DTC_ACCEPTED;
}

static void finish(dtc_state_t *s, dtc_phase_t phase, const char *reason, uint64_t now_ms)
{
	s->phase = phase;
	s->reason = reason;
	s->name = NULL;
	s->finished_ms = now_ms;
}

bool dtc_state_pickup(dtc_state_t *s, uint64_t now_ms)
{
	if(s->phase != DTC_STATE_QUEUED) return false;

	// The HTTP client has given up long ago, a clear must not run behind its back
	if(s->src == DTC_SRC_HTTP && elapsed_ms(now_ms, s->queued_ms) > DTC_HTTP_EXPIRY_MS)
	{
		finish(s, DTC_STATE_ERROR, "expired", now_ms);
		return false;
	}

	s->phase = DTC_STATE_RUNNING;
	return true;
}

void dtc_state_progress(dtc_state_t *s, uint8_t step, uint8_t total, const char *name)
{
	if(s->phase != DTC_STATE_RUNNING) return;

	s->step = step;
	s->total = total;
	s->name = name;
}

void dtc_state_error(dtc_state_t *s, const char *reason, uint64_t now_ms)
{
	if(s->phase != DTC_STATE_RUNNING) return;

	finish(s, DTC_STATE_ERROR, reason, now_ms);
}

void dtc_state_done(dtc_state_t *s, uint16_t dtc_count, uint64_t now_ms)
{
	if(s->phase != DTC_STATE_RUNNING) return;

	finish(s, DTC_STATE_DONE, NULL, now_ms);
	s->result_seq = s->seq;
	s->result_count = dtc_count;
}

const char *dtc_accept_reason(dtc_accept_t result)
{
	switch(result)
	{
		case DTC_REJECT_BUSY:             return "busy";
		case DTC_REJECT_READ_REQUIRED:    return "read_required";
		case DTC_REJECT_STALE_SEQ:        return "stale_seq";
		case DTC_REJECT_NOTHING_TO_CLEAR: return "nothing_to_clear";
		default:                          return NULL;
	}
}

typedef struct
{
	char *buf;
	size_t size;
	size_t len;
	bool overflow;
} json_out_t;

static void put_char(json_out_t *out, char c)
{
	// One byte stays free for the terminating zero
	if(out->len + 1 >= out->size)
	{
		out->overflow = true;
		return;
	}
	out->buf[out->len++] = c;
}

static void put_raw(json_out_t *out, const char *text)
{
	while(*text != '\0') put_char(out, *text++);
}

// Text as JSON string content: quote and backslash escaped, control characters dropped, UTF-8 passed on
static void put_escaped(json_out_t *out, const char *text)
{
	if(text == NULL) return;

	for(; *text != '\0'; text++)
	{
		unsigned char c = (unsigned char)*text;

		if(c < 0x20) continue;
		if(c == '"' || c == '\\') put_char(out, '\\');
		put_char(out, (char)c);
	}
}

static void put_number(json_out_t *out, uint32_t value)
{
	char digits[11];

	snprintf(digits, sizeof(digits), "%" PRIu32, value);
	put_raw(out, digits);
}

static const char *phase_text(dtc_phase_t phase)
{
	switch(phase)
	{
		case DTC_STATE_QUEUED:  return "queued";
		case DTC_STATE_RUNNING: return "running";
		case DTC_STATE_DONE:    return "done";
		case DTC_STATE_ERROR:   return "error";
		default:                return "idle";
	}
}

int dtc_state_json(const dtc_state_t *s, bool supported, uint64_t now_ms, char *buf, size_t size)
{
	json_out_t out = {buf, size, 0, false};
	bool requested = s->seq != 0;
	bool finished = s->phase == DTC_STATE_DONE || s->phase == DTC_STATE_ERROR;

	if(size == 0) return -1;

	put_raw(&out, "{\"supported\":");
	put_raw(&out, supported ? "true" : "false");
	put_raw(&out, ",\"state\":\"");
	put_raw(&out, phase_text(s->phase));
	put_raw(&out, "\",\"action\":\"");
	if(requested) put_raw(&out, s->clear ? "clear" : "read");
	put_raw(&out, "\",\"src\":\"");
	if(requested) put_raw(&out, s->src == DTC_SRC_HTTP ? "http" : "mqtt");
	put_raw(&out, "\",\"seq\":");
	put_number(&out, s->seq);
	put_raw(&out, ",\"ecu\":");
	put_number(&out, s->step);
	put_raw(&out, ",\"total\":");
	put_number(&out, s->total);
	put_raw(&out, ",\"name\":\"");
	put_escaped(&out, s->name);
	put_raw(&out, "\",\"reason\":\"");
	put_escaped(&out, s->reason);
	put_raw(&out, "\",\"age_s\":");
	put_number(&out, finished ? (uint32_t)(elapsed_ms(now_ms, s->finished_ms) / 1000u) : 0);
	put_raw(&out, ",\"count\":");
	put_number(&out, s->result_count);
	put_raw(&out, ",\"result_seq\":");
	put_number(&out, s->result_seq);
	put_char(&out, '}');

	if(out.overflow)
	{
		buf[0] = '\0';
		return -1;
	}

	buf[out.len] = '\0';
	return (int)out.len;
}
