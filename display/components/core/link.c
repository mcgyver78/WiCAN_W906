/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "link.h"

static const uint32_t SCAN_WAITS[] = LINK_SCAN_WAITS_MS;

#define SCAN_WAIT_COUNT     ((int)(sizeof(SCAN_WAITS) / sizeof(SCAN_WAITS[0])))

// The calls come from more than one task, each with its own reading of the time: one that lies before the
// latest seen lets no time pass.
static void advance(link_t *link, uint64_t now_ms)
{
	if(now_ms > link->clock_ms) link->clock_ms = now_ms;
}

// The time `ms` from now. Behind the largest time there is none: a wait that would end there ends with it,
// instead of at once.
static uint64_t later(const link_t *link, uint32_t ms)
{
	return link->clock_ms > UINT64_MAX - ms ? UINT64_MAX : link->clock_ms + ms;
}

// A count that is no list (net_select.h) holds no profile
static int listed(int profile_count)
{
	if(profile_count < 0 || profile_count > NET_PROFILES_MAX) return 0;
	return profile_count;
}

// Takes `text` as the address of the adapter. false, and nothing taken, if it is none: NULL, empty, or
// without an end within LINK_HOST_SIZE bytes. An address cut short would be the address of something else.
static bool take_host(link_t *link, const char *text)
{
	size_t length = 0;

	if(text == NULL) return false;
	while(length < LINK_HOST_SIZE && text[length] != '\0') length++;
	if(length == 0 || length == LINK_HOST_SIZE) return false;

	memcpy(link->host, text, length + 1);
	return true;
}

// In no network and nothing under way: a scan is due at once and the waits start from the beginning.
// Without a profile there is nothing to scan for.
static void start_over(link_t *link)
{
	link->phase = link->profile_count > 0 ? LINK_WAITING : LINK_IDLE;
	link->profile = -1;
	link->busy = false;
	link->finding = false;
	link->changed = false;
	link->host[0] = '\0';
	link->wait_step = 0;
	link->wait_until_ms = link->clock_ms;
}

// The scan found no stored network, or the join failed for good: the next scan follows after the wait
static void wait_for_scan(link_t *link)
{
	link->phase = LINK_WAITING;
	link->profile = -1;
	link->wait_until_ms = later(link, SCAN_WAITS[link->wait_step]);
	if(link->wait_step < SCAN_WAIT_COUNT - 1) link->wait_step++;
}

// The join under way ended without the network
static void join_failed(link_t *link)
{
	link->busy = false;
	// The list changed during the attempt: the profile it was made for may be gone
	if(link->changed) start_over(link);
	else if(link->tries >= LINK_JOIN_TRIES) wait_for_scan(link);
}

// link->host is the address of the adapter from now on
static void located(link_t *link, bool from_query)
{
	link->phase = LINK_UP;
	link->host_from_query = from_query;
	// What was reported about another address or in another network says nothing about this one
	link->no_answer = false;
}

// The query under way ended with the address it found, or with NULL
static void query_ended(link_t *link, const char *host)
{
	if(!link->finding) return;

	link->finding = false;
	link->busy = false;
	// The network is about to be left: nothing is looked for in it any more
	if(link->changed) return;

	if(take_host(link, host)) located(link, true);
	else link->find_at_ms = later(link, LINK_FIND_RETRY_MS);
}

// Whether the service has to be queried now
static bool query_due(const link_t *link)
{
	if(link->phase == LINK_UP)
	{
		// Only an address that came from a query can have changed, and only silence makes it suspect
		if(!link->host_from_query || !link->no_answer) return false;
		if(link->clock_ms - link->no_answer_since_ms < LINK_FIND_AGAIN_MS) return false;
	}
	else if(link->phase != LINK_JOINED)
	{
		return false;
	}
	return link->clock_ms >= link->find_at_ms;
}

void link_init(link_t *link, const net_profile_t *profiles, int profile_count, bool safe_mode, uint64_t now_ms)
{
	memset(link, 0, sizeof(*link));
	link->clock_ms = now_ms;
	link->profiles = profiles;
	link->profile_count = listed(profile_count);
	link->ap_forced = safe_mode;
	link->ap_wanted = safe_mode || link->profile_count == 0;
	start_over(link);
}

link_do_t link_next(link_t *link, uint64_t now_ms)
{
	advance(link, now_ms);

	// Before everything else, so that the end of the attempt does not depend on what this call hands out
	if(link->phase == LINK_JOINING && link->busy && link->clock_ms - link->action_since_ms >= LINK_JOIN_TIMEOUT_MS) join_failed(link);

	if(link->ap_on && !link->ap_forced && link->profile_count > 0 && link->ap_clients == 0 &&
	   link->clock_ms - link->ap_idle_since_ms >= LINK_AP_IDLE_MS)
	{
		link->ap_wanted = false;
	}
	if(link->ap_wanted != link->ap_on)
	{
		link->ap_on = link->ap_wanted;
		if(!link->ap_on) return LINK_DO_AP_OFF;

		// It opens without a client, whatever was reported while it was closed
		link->ap_clients = 0;
		link->ap_idle_since_ms = link->clock_ms;
		return LINK_DO_AP_ON;
	}

	if(link->busy) return LINK_DO_NOTHING;

	if(link->changed)
	{
		// `changed` stays until the network is left: start_over() takes it away
		link->phase = LINK_LEAVING;
		link->busy = true;
		return LINK_DO_LEAVE;
	}
	if(link->phase == LINK_WAITING && link->clock_ms >= link->wait_until_ms)
	{
		link->phase = LINK_SCANNING;
		link->busy = true;
		return LINK_DO_SCAN;
	}
	if(link->phase == LINK_JOINING)
	{
		link->tries++;
		link->action_since_ms = link->clock_ms;
		link->busy = true;
		return LINK_DO_JOIN;
	}
	if(query_due(link))
	{
		link->finding = true;
		link->busy = true;
		return LINK_DO_FIND;
	}
	return LINK_DO_NOTHING;
}

int link_profile(const link_t *link)
{
	return link->profile;
}

void link_scanned(link_t *link, const char (*seen)[NET_SSID_SIZE], int seen_count, uint64_t now_ms)
{
	advance(link, now_ms);
	if(link->phase != LINK_SCANNING) return;

	link->busy = false;
	if(seen_count > LINK_SEEN_MAX) seen_count = LINK_SEEN_MAX;
	link->profile = net_choose(link->profiles, link->profile_count, seen, seen_count);
	if(link->profile >= 0)
	{
		link->phase = LINK_JOINING;
		link->tries = 0;
	}
	// The last profile was removed during the scan
	else if(link->profile_count == 0) start_over(link);
	else wait_for_scan(link);
}

void link_joined(link_t *link, const char *gateway, uint64_t now_ms)
{
	const net_profile_t *profile;
	bool known = false;

	advance(link, now_ms);
	if(link->phase != LINK_JOINING || !link->busy) return;

	link->phase = LINK_JOINED;
	link->busy = false;
	// The list changed during the join: this network is left again, and the index is one of the old list
	if(link->changed) return;

	profile = &link->profiles[link->profile];
	switch(net_host_rule(profile))
	{
		case NET_HOST_GIVEN:    known = take_host(link, profile->host); break;
		case NET_HOST_GATEWAY:  known = take_host(link, gateway); break;
		default:                break;
	}
	if(known) located(link, false);
	else link->find_at_ms = link->clock_ms;
}

void link_join_failed(link_t *link, uint64_t now_ms)
{
	advance(link, now_ms);
	if(link->phase == LINK_JOINING && link->busy) join_failed(link);
}

void link_lost(link_t *link, uint64_t now_ms)
{
	advance(link, now_ms);
	// A query under way is given up with the network it was sent into
	if(link->phase == LINK_JOINED || link->phase == LINK_UP) start_over(link);
}

void link_left(link_t *link, uint64_t now_ms)
{
	advance(link, now_ms);
	if(link->phase == LINK_LEAVING) start_over(link);
}

void link_found(link_t *link, const char *host, uint64_t now_ms)
{
	advance(link, now_ms);
	query_ended(link, host);
}

void link_not_found(link_t *link, uint64_t now_ms)
{
	advance(link, now_ms);
	query_ended(link, NULL);
}

void link_answering(link_t *link, bool answering, uint64_t now_ms)
{
	advance(link, now_ms);
	// The silence counts from the first report of it
	if(!answering && !link->no_answer) link->no_answer_since_ms = link->clock_ms;
	link->no_answer = !answering;
}

void link_profiles(link_t *link, const net_profile_t *profiles, int profile_count, uint64_t now_ms)
{
	advance(link, now_ms);
	link->profiles = profiles;
	link->profile_count = listed(profile_count);
	if(link->profile_count == 0) link->ap_wanted = true;

	if(link->phase == LINK_JOINED || link->phase == LINK_UP || (link->phase == LINK_JOINING && link->busy))
	{
		// In a network or on the way into one: it is left as soon as nothing is under way. What was taken
		// from the old list is void at once.
		link->changed = true;
		link->profile = -1;
		link->host[0] = '\0';
		if(link->phase == LINK_UP) link->phase = LINK_JOINED;
	}
	// What the scan under way sees is as good for the new list
	else if(link->phase == LINK_SCANNING) link->wait_step = 0;
	// After the leave under way the new list is in use anyway
	else if(link->phase != LINK_LEAVING) start_over(link);
}

void link_ap_request(link_t *link, bool on, uint64_t now_ms)
{
	advance(link, now_ms);
	link->ap_wanted = on || link->ap_forced || link->profile_count == 0;
}

void link_ap_clients(link_t *link, int clients, uint64_t now_ms)
{
	advance(link, now_ms);
	if(clients < 0) clients = 0;
	// The time without a client begins when the last one leaves
	if(clients == 0 && link->ap_clients > 0) link->ap_idle_since_ms = link->clock_ms;
	link->ap_clients = clients;
}

bool link_ap_on(const link_t *link)
{
	return link->ap_on;
}

bool link_up(const link_t *link)
{
	return link->phase == LINK_UP;
}

const char *link_host(const link_t *link)
{
	return link->host;
}
