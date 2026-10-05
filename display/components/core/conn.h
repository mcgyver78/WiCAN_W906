/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __CONN_H__
#define __CONN_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "wican_state.h"

/*
 * The connection of the display to the WiCAN: what to ask next, and what to show when nothing comes back.
 * Pure logic. The caller does WiFi and HTTP, reports what happened and asks what to do.
 *
 * The HTTP server of the adapter is a single task with few sessions, so the display uses one connection
 * and sends one request at a time. One round per second:
 *   1. GET /api/state                      always
 *   2. GET /api/dtc/result                 if the state names a result this display has not fetched yet
 *   3. GET /load_car_config                once per connection, again after the adapter restarted or its
 *                                          number of values changed; never while a scan is queued or running
 *   4. GET /autopid_data                   if AutoPID runs, the ECU is online and no scan is queued or running
 * A firmware without /api/state (404) still has 3 and 4: the display then shows values only.
 *
 * Times are milliseconds of the display. A time before a stored one counts as no time passed.
 */

#define CONN_ROUND_MS           1000u   // from the start of one round to the start of the next
#define CONN_GRACE_MS           15000u  // after the WiFi came up, failures are not shown as "no answer"
#define CONN_FAILED_ROUNDS      3       // failed rounds in a row until "no answer"
#define CONN_NO_API_RECHECK_MS  30000u  // firmware without /api/state: ask for it again this often
#define CONN_DTC_MIN_UP_S       15u     // fault memory commands only when the adapter is up this long ...
#define CONN_DTC_MIN_ROUNDS     2       // ... and answered this many rounds in a row
// Wait from the end of a failed round to the next one: 1 s, 2 s, 5 s, then 10 s each time
#define CONN_BACKOFF_MS         {1000u, 2000u, 5000u, 10000u}

typedef enum
{
	CONN_ASK_NOTHING,       // a request is under way, or it is not time yet
	CONN_ASK_STATE,
	CONN_ASK_RESULT,
	CONN_ASK_CATALOG,
	CONN_ASK_VALUES,
} conn_ask_t;

typedef enum
{
	CONN_GOT_OK,            // status 200 and a body the caller could use
	CONN_GOT_NOT_FOUND,     // status 404; for the result also 204: there is none
	CONN_GOT_FAILED,        // no answer in time, connection lost, any other status, unusable body
} conn_got_t;

typedef enum
{
	CONN_VIEW_NO_WIFI,      // in no network: the adapter sleeps, has no power or is out of range
	CONN_VIEW_CONNECTING,   // in a network, no answer yet
	CONN_VIEW_NO_ANSWER,    // in a network, CONN_FAILED_ROUNDS rounds in a row failed
	CONN_VIEW_FOREIGN,      // another adapter answers than the one the display is bound to
	CONN_VIEW_NO_API,       // firmware without /api/state: values only, no fault memory
	CONN_VIEW_AUTOPID_OFF,
	CONN_VIEW_STARTING,
	CONN_VIEW_SCAN,         // a fault memory scan is queued or running: the values stand still
	CONN_VIEW_ECU_OFFLINE,  // ignition off
	CONN_VIEW_LIVE,
} conn_view_t;

typedef struct
{
	char bound_id[33];          // adapter this display belongs to, empty = the first that answers
	bool wifi;
	uint64_t wifi_since_ms;
	bool asking;                // a request is under way
	conn_ask_t asked;
	uint64_t round_start_ms;
	uint64_t next_round_ms;
	int failed_rounds;          // in a row
	int good_rounds;            // rounds with an answered state, in a row
	bool has_state;             // state holds an answer of this connection
	wican_state_t state;
	bool no_api;
	uint64_t no_api_since_ms;   // last time /api/state answered 404
	bool foreign;
	bool want_result;
	bool want_catalog;
	bool want_values;
	uint32_t fetched_result_seq;
	bool restarted;             // not yet taken
	bool bind_pending;          // not yet taken
} conn_t;

// bound_id: id of the adapter the display is bound to, empty or NULL if it is not bound yet
void conn_init(conn_t *conn, const char *bound_id);

// The display joined or left a network. Joining starts over: nothing is known about the adapter, the first
// round is due at once - so an adapter that restarted while the display was out of the network is no restart
// here (poll.h sees it, by the start it keeps over the pause). Calling it again with the same value changes
// nothing.
void conn_wifi(conn_t *conn, bool up, uint64_t now_ms);

// What to send now. Returns CONN_ASK_NOTHING while a request is under way (until the matching conn_got_*
// call), while there is no WiFi, and until the next round is due. The same request is handed out once.
conn_ask_t conn_next(conn_t *conn, uint64_t now_ms);

// The end of a request. A call that does not match the request under way is ignored.
// conn_got_state: `state` is the parsed answer for CONN_GOT_OK (without one, NULL, it counts as FAILED) and
//   ignored otherwise.
//   OK: the round goes on with result, catalogue and values as the header comment says. A boot number
//       other than the last one seen on this connection marks a restart: the catalogue is asked again and
//       fetched results are forgotten. An id other than bound_id (if bound) makes the adapter foreign:
//       nothing else is asked from it. If the display is not bound, the first answer with an id binds it.
//   NOT_FOUND: the firmware has no API; the round goes on with catalogue (once) and values. A 404 after an
//       answered state, and an answered state after a 404, is another firmware: it counts as a restart.
//   FAILED: the round has failed.
// conn_got_result:  OK or NOT_FOUND: the result named by the last state counts as fetched. FAILED: asked
//   again next round; the round counts as failed.
// conn_got_catalog: OK: done for this connection. NOT_FOUND or FAILED: asked again next round, and the
//   round counts as failed only for FAILED.
// conn_got_values:  FAILED: the round counts as failed. NOT_FOUND counts as FAILED.
// A request that fails ends its round: what was left of the round is not asked.
// A round that ends without failure resets the count of failed rounds; the next round starts
// CONN_ROUND_MS after this one started, or at once if that time has passed. After a failed round the next
// one starts after the waits of CONN_BACKOFF_MS, counted from the end of the failed round.
void conn_got_state(conn_t *conn, conn_got_t got, const wican_state_t *state, uint64_t now_ms);
void conn_got_result(conn_t *conn, conn_got_t got, uint64_t now_ms);
void conn_got_catalog(conn_t *conn, conn_got_t got, uint64_t now_ms);
void conn_got_values(conn_t *conn, conn_got_t got, uint64_t now_ms);

// What the display shows. Decided in this order:
//   no WiFi -> NO_WIFI
//   CONN_FAILED_ROUNDS or more failed rounds in a row and the grace time over -> NO_ANSWER
//   no answer on this connection yet -> CONNECTING
//   foreign -> FOREIGN;  no API -> NO_API
//   autopid off -> AUTOPID_OFF;  starting, or any other value that is not run -> STARTING
//   scan queued or running -> SCAN;  ECU offline -> ECU_OFFLINE;  else LIVE
conn_view_t conn_view(const conn_t *conn, uint64_t now_ms);

// The last answer of GET /api/state on this connection, NULL if there is none (also in NO_API)
const wican_state_t *conn_state(const conn_t *conn);

// true if a fault memory command may be sent: an answered state, not foreign, AutoPID running, the adapter up
// for CONN_DTC_MIN_UP_S, and CONN_DTC_MIN_ROUNDS answered rounds in a row. A failed round takes it away.
bool conn_dtc_allowed(const conn_t *conn);

// true exactly once after the adapter restarted or was replaced by another one on this connection: the
// caller forgets values, results and a fault memory dialog in progress
bool conn_take_restarted(conn_t *conn);

// true exactly once after the first answer of an adapter while the display was not bound: `id` receives
// the id to store. From then on only this adapter is accepted. With `id` NULL or `size` too small for the
// id and its terminating zero nothing is written, false is returned and the id stays to be taken.
bool conn_take_bind(conn_t *conn, char *id, size_t size);

#endif
