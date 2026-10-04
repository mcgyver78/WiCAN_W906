/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINK_H__
#define __LINK_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "net_select.h"

/*
 * The WiFi side of the display: when to scan, which network to join, where the adapter is in that network,
 * and when the display opens its own access point. Pure logic: the caller drives the WiFi driver, reports
 * what happened and asks what to do next. One action is under way at a time.
 *
 * The network may be a router, the access point of the WiCAN itself, or anything else (net_select.h). The
 * adapter may sleep for days while the display is on permanent power, so looking for it must be cheap:
 * scans become rare when nothing is found.
 *
 *   no profile stored          the own access point is on, nothing is scanned
 *   profiles stored            scan -> join the first profile that is in range -> find the adapter there
 *   joined                     by the rule of the profile (net_host_rule): the stored host, the gateway, or
 *                              a query for the service _wican._tcp
 *   lost                       scan again at once, then with the waits below
 */

// Wait before the next scan after a scan that found no stored network, or after a join that failed:
// 2 s, 5 s, 10 s, then 30 s each time. Counted from the end of the scan or of the failed join.
#define LINK_SCAN_WAITS_MS      {2000u, 5000u, 10000u, 30000u}
#define LINK_JOIN_TRIES         2           // attempts to join a network that was seen, then a new scan
#define LINK_JOIN_TIMEOUT_MS    15000u      // an attempt without an outcome counts as failed after this
#define LINK_FIND_RETRY_MS      10000u      // the service was not found: ask again after this
#define LINK_FIND_AGAIN_MS      60000u      // the adapter does not answer this long at an address that was
                                            // found by a query: ask again (its address may have changed)
#define LINK_AP_IDLE_MS         (600u * 1000u)  // the own access point closes after this long without a client
#define LINK_HOST_SIZE          NET_HOST_SIZE
#define LINK_SEEN_MAX           20

typedef enum
{
	LINK_DO_NOTHING,
	LINK_DO_SCAN,       // scan for networks, then link_scanned()
	LINK_DO_JOIN,       // join the profile link_profile(), then link_joined() or link_join_failed()
	LINK_DO_LEAVE,      // leave the network (the profiles changed), then link_left()
	LINK_DO_FIND,       // query the service _wican._tcp, then link_found() or link_not_found()
	LINK_DO_AP_ON,      // open the own access point (next to the station)
	LINK_DO_AP_OFF,
} link_do_t;

typedef enum
{
	LINK_IDLE,          // no profile stored: nothing to do but the access point
	LINK_WAITING,       // until the next scan
	LINK_SCANNING,
	LINK_JOINING,
	LINK_LEAVING,
	LINK_JOINED,        // in a network, the adapter not located yet (finding, or waiting to ask again)
	LINK_UP,            // in a network and link_host() is set: the connection logic (conn.h) can run
} link_phase_t;

typedef struct
{
	link_phase_t phase;
	const net_profile_t *profiles;  // the list of the caller; link_profiles() tells when it changed
	int profile_count;
	int profile;                    // profile being joined or joined, -1 if none
	int tries;                      // attempts to join it since the scan
	int wait_step;                  // index into LINK_SCAN_WAITS_MS
	uint64_t wait_until_ms;
	uint64_t action_since_ms;       // start of the join under way
	bool busy;                      // an action that ends with a report is under way (scan, join, leave, find)
	bool finding;                   // the action under way is a query
	uint64_t find_at_ms;            // next query
	bool host_from_query;
	char host[LINK_HOST_SIZE];      // address or name of the adapter, empty if not known
	bool no_answer;
	uint64_t no_answer_since_ms;
	bool ap_wanted;                 // by the user, by the safe mode, or because no profile is stored
	bool ap_on;                     // as last ordered
	bool ap_forced;                 // safe mode: never closes by itself
	int ap_clients;
	uint64_t ap_idle_since_ms;
	bool changed;                   // the profiles changed while joined: leave first
	uint64_t clock_ms;              // the latest time seen: it follows the calls but never runs backwards
} link_t;

// profiles: the stored networks (the array has to stay; a count outside 0 to NET_PROFILES_MAX counts as 0).
// safe_mode: the own access point is on and stays on.
// Without a profile the access point is wanted as well. The first link_next() orders what is needed.
void link_init(link_t *link, const net_profile_t *profiles, int profile_count, bool safe_mode, uint64_t now_ms);

// What to do now. Returns LINK_DO_NOTHING while an action is under way and while there is nothing to do.
// Each action is handed out once. The access point goes first: LINK_DO_AP_ON / LINK_DO_AP_OFF whenever
// what is wanted differs from what was last ordered; they need no report and do not block the rest.
// A join that got no report within LINK_JOIN_TIMEOUT_MS counts as failed here.
link_do_t link_next(link_t *link, uint64_t now_ms);

// Profile to join after LINK_DO_JOIN. It stays while the display is in that network. -1 if there is none,
// and from link_profiles() on: an index into the old list means nothing in the new one.
int link_profile(const link_t *link);

// The scan ended. seen: the SSIDs found (may be NULL with count 0; at most LINK_SEEN_MAX are looked at).
// The first profile in the order of the list that is in range is joined next (net_choose); if none is,
// the next scan follows after the wait. A report without a scan under way is ignored.
void link_scanned(link_t *link, const char (*seen)[NET_SSID_SIZE], int seen_count, uint64_t now_ms);

// The join succeeded. gateway: address of the gateway as text (may be NULL or empty). The waits start
// from the beginning again. The adapter is then located by the rule of the profile:
//   NET_HOST_GIVEN    the host of the profile: up at once
//   NET_HOST_GATEWAY  the gateway: up at once; without a gateway text as NET_HOST_MDNS
//   NET_HOST_MDNS     LINK_DO_FIND is ordered
// A text without an end within LINK_HOST_SIZE bytes is no text (a gateway too long, the host of a damaged
// profile): the service is queried.
void link_joined(link_t *link, const char *gateway, uint64_t now_ms);

// The join failed. After LINK_JOIN_TRIES attempts a new scan follows after the wait, else the next attempt
// at once.
void link_join_failed(link_t *link, uint64_t now_ms);

// The network was lost while joined (or while finding): the host is forgotten, a scan follows at once and
// the waits start from the beginning. A query under way is given up; its report is ignored when it comes.
// In every other phase the call is ignored: a join under way ends with link_join_failed() or its timeout,
// a leave with link_left().
void link_lost(link_t *link, uint64_t now_ms);

// The network was left after LINK_DO_LEAVE: a scan follows at once
void link_left(link_t *link, uint64_t now_ms);

// The query ended. host: address of the adapter as text; an empty or too long text counts as not found.
// Not found: asked again LINK_FIND_RETRY_MS later; the display stays in the network.
void link_found(link_t *link, const char *host, uint64_t now_ms);
void link_not_found(link_t *link, uint64_t now_ms);

// The connection logic reports every round whether the adapter answers (conn_view() is not
// CONN_VIEW_NO_ANSWER and not CONN_VIEW_CONNECTING). If it does not answer for LINK_FIND_AGAIN_MS at an
// address that came from a query, the service is queried again; the old address stays in use until a new
// one is found. Addresses from a profile or the gateway are never replaced.
// The time counts from the first report without an answer; a report with an answer ends it, and so does a
// new address. Not found: asked again LINK_FIND_RETRY_MS later if the adapter still does not answer.
void link_answering(link_t *link, bool answering, uint64_t now_ms);

// The stored networks changed (the caller has updated its array). If the display is in a network or
// joining one, LINK_DO_LEAVE is ordered first; then it starts over with a scan. Without profiles the phase
// becomes LINK_IDLE and the access point is wanted.
// The count as in link_init(). The host is forgotten with this call (LINK_UP becomes LINK_JOINED). An
// action under way ends first: a query with its report, a join with its outcome - if it fails there is
// nothing to leave - and a scan under way is the scan the new list starts over with. In no network and
// with nothing under way the scan follows at once. The waits start from the beginning in every case.
void link_profiles(link_t *link, const net_profile_t *profiles, int profile_count, uint64_t now_ms);

// The user switched the own access point on or off at the device. In safe mode and without a stored
// profile it stays on whatever is asked.
void link_ap_request(link_t *link, bool on, uint64_t now_ms);

// Number of clients of the own access point, reported when it changes. With no client for LINK_AP_IDLE_MS
// the access point closes by itself - unless safe mode or no stored profile keep it open.
// The time counts from LINK_DO_AP_ON or from the last client leaving. A negative number counts as 0, and
// with LINK_DO_AP_ON the number is 0 again: an access point that was closed has no clients.
void link_ap_clients(link_t *link, int clients, uint64_t now_ms);

// true while the own access point is ordered on
bool link_ap_on(const link_t *link);

// true in LINK_UP
bool link_up(const link_t *link);

// Address or name of the adapter for the HTTP requests, empty unless LINK_UP
const char *link_host(const link_t *link);

#endif
