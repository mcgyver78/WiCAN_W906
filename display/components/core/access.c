/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "access.h"

// The end of something that begins now. Behind the largest time there is no moment at which it could be
// found over, so it ends there: a sum that wrapped around would end it at once or never.
static uint64_t after(uint64_t now, uint64_t duration_ms)
{
	return now > UINT64_MAX - duration_ms ? UINT64_MAX : now + duration_ms;
}

static uint32_t seconds_rounded_up(uint64_t ms)
{
	return (uint32_t)((ms + 999) / 1000);
}

// The question that waits ends as `end`. A ticket that has ended keeps its end: nothing happens if none waits.
static void end_question(access_t *access, access_ticket_t end)
{
	if(access->asking == ACCESS_ASK_NONE) return;

	access->asking = ACCESS_ASK_NONE;
	access->ticket_end = end;
}

// Brings the struct to now_ms. Nobody calls at the moment a time runs out, so what ran out since the
// previous call is ended here, the way it ended then.
static void settle(access_t *access, uint64_t now_ms)
{
	if(now_ms > access->clock_ms) access->clock_ms = now_ms;

	// A question whose own time was over when the release ended has expired, however late that is noticed.
	// One that still waited then was refused, also if its own time is over by now as well.
	if(access->clock_ms >= access->asking_until_ms && access->asking_until_ms <= access->open_until_ms)
	{
		end_question(access, ACCESS_TICKET_EXPIRED);
	}
	if(access->clock_ms >= access->open_until_ms)
	{
		end_question(access, ACCESS_TICKET_REFUSED);
		access->open = false;
	}
}

// The struct as it is at now_ms, for the functions that only ask
static access_t at(const access_t *access, uint64_t now_ms)
{
	access_t state = *access;

	settle(&state, now_ms);
	return state;
}

// The release is given or renewed now. No renewal reaches beyond the latest end: who writes again and again
// must not keep open what was released by somebody at the device.
static void renew(access_t *access)
{
	uint64_t end = after(access->clock_ms, ACCESS_OPEN_MS);

	access->open_until_ms = end < access->open_max_ms ? end : access->open_max_ms;
}

// Why a question cannot be asked in this state, which settle() has brought to the present. The closed
// release goes first: who may not change anything learns nothing else.
static access_refusal_t refusal(const access_t *access, access_ask_t ask)
{
	if(!access->open) return ACCESS_CLOSED;
	if(ask != ACCESS_ASK_WIFI && ask != ACCESS_ASK_FIRMWARE && ask != ACCESS_ASK_RESET) return ACCESS_BAD_QUESTION;
	if(access->asking != ACCESS_ASK_NONE) return ACCESS_ASKING;
	return ACCESS_ALLOWED;
}

void access_init(access_t *access)
{
	memset(access, 0, sizeof(*access));
}

void access_open(access_t *access, uint64_t now_ms)
{
	settle(access, now_ms);
	access->open = true;
	access->open_max_ms = after(access->clock_ms, ACCESS_OPEN_MAX_MS);
	renew(access);
}

void access_close(access_t *access, uint64_t now_ms)
{
	settle(access, now_ms);
	end_question(access, ACCESS_TICKET_REFUSED);
	access->open = false;
}

bool access_is_open(const access_t *access, uint64_t now_ms)
{
	return at(access, now_ms).open;
}

uint32_t access_seconds_left(const access_t *access, uint64_t now_ms)
{
	access_t state = at(access, now_ms);

	return state.open ? seconds_rounded_up(state.open_until_ms - state.clock_ms) : 0;
}

bool access_write(access_t *access, uint64_t now_ms)
{
	settle(access, now_ms);
	if(!access->open) return false;

	renew(access);
	return true;
}

uint32_t access_ask(access_t *access, access_ask_t ask, uint64_t now_ms)
{
	settle(access, now_ms);
	if(refusal(access, ask) != ACCESS_ALLOWED) return 0;

	// The last ticket has ended, settle() saw to that: its end is final and moves on with it
	access->previous_end = access->ticket_end;
	access->ticket = access->ticket == UINT32_MAX ? 1 : access->ticket + 1;
	access->ticket_end = ACCESS_TICKET_WAITING;
	access->asking = ask;
	access->asking_since_ms = access->clock_ms;
	access->asking_until_ms = after(access->clock_ms, ACCESS_CONFIRM_MS);
	renew(access);
	return access->ticket;
}

access_refusal_t access_may_ask(const access_t *access, access_ask_t ask, uint64_t now_ms)
{
	access_t state = at(access, now_ms);

	return refusal(&state, ask);
}

access_ask_t access_asking(const access_t *access, uint64_t now_ms)
{
	return at(access, now_ms).asking;
}

uint32_t access_ask_seconds_left(const access_t *access, uint64_t now_ms)
{
	access_t state = at(access, now_ms);
	// The release may end before the time of the question is over, and the question with it
	uint64_t end = state.asking_until_ms < state.open_until_ms ? state.asking_until_ms : state.open_until_ms;

	return state.asking != ACCESS_ASK_NONE ? seconds_rounded_up(end - state.clock_ms) : 0;
}

access_ask_t access_confirm(access_t *access, uint64_t now_ms)
{
	access_ask_t confirmed;

	settle(access, now_ms);
	// A press that comes this soon was meant for the screen the question appeared over: the question waits
	// on. If none waits there is nothing to confirm either way.
	if(access->clock_ms - access->asking_since_ms < ACCESS_ASK_SHOWN_MS) return ACCESS_ASK_NONE;

	confirmed = access->asking;
	end_question(access, ACCESS_TICKET_CONFIRMED);
	return confirmed;
}

void access_refuse(access_t *access, uint64_t now_ms)
{
	settle(access, now_ms);
	end_question(access, ACCESS_TICKET_REFUSED);
}

access_ticket_t access_ticket(const access_t *access, uint32_t ticket, uint64_t now_ms)
{
	access_t state = at(access, now_ms);

	// While no ticket was given the last one is 0 and its end UNKNOWN: who asks for 0 gets that
	if(ticket == state.ticket) return state.ticket_end;
	// The one before the last has the number before it, and before 1 came 2^32-1. If there was none, its
	// end is UNKNOWN.
	if(ticket == (state.ticket == 1 ? UINT32_MAX : state.ticket - 1)) return state.previous_end;
	return ACCESS_TICKET_UNKNOWN;
}
