/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __POLL_H__
#define __POLL_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "json.h"
#include "conn.h"
#include "values.h"
#include "catalog.h"
#include "dtc_flow.h"
#include "dtc_model.h"
#include "guard.h"

/*
 * The conversation with the WiCAN, one HTTP request at a time: which request is sent next, and what each
 * answer means for the connection (conn.h), the values, the catalogue and the fault memory flow
 * (dtc_flow.h). Pure logic without sockets: the caller sends what poll_prepare() hands out and passes the
 * answer to poll_apply(). The same code runs on the display and, with plain sockets, on a PC against
 * tools/w906/mock_wican.py.
 *
 * The caller holds one lock around poll_prepare() and around poll_apply(), not around the request itself:
 * the user interface reads values and starts fault memory requests in between.
 *
 *   request                                     answer
 *   GET  /api/state                             200 wican_state.h | 404 firmware without the API
 *   GET  /api/dtc/result                        200 dtc_model.h, header X-DTC-Seq | 204 none
 *   GET  /load_car_config                       200 catalog.h
 *   GET  /autopid_data                          200 values.h
 *   POST /api/dtc?action=read                   202 {"accepted":true,"seq":N} | 4xx, 503
 *   POST /api/dtc?action=clear&seq=N            {"accepted":false,"reason":"..","seq":N}
 *        with the header X-WiCAN-DTC: 1         (tools/w906/API.md)
 */

#define POLL_PATH_SIZE      48
#define POLL_BODY_SIZE      16384   // room the caller provides for the body of an answer, with its zero
#define POLL_TEXT_SIZE      5200    // a result text of the adapter (it sends at most 5119 bytes)
#define POLL_TOKENS         DTC_RESULT_TOKENS   // the largest of the token rooms the readers need
#define POLL_HEADER_NAME    "X-WiCAN-DTC"
#define POLL_HEADER_VALUE   "1"
#define POLL_SEQ_HEADER     "X-DTC-Seq"
#define POLL_TIMEOUT_MS     4000u   // the caller gives up a request after this long: status 0

typedef enum
{
	POLL_NONE,
	POLL_STATE,
	POLL_RESULT,
	POLL_CATALOG,
	POLL_VALUES,
	POLL_DTC_READ,
	POLL_DTC_CLEAR,
} poll_kind_t;

typedef struct
{
	poll_kind_t kind;
	bool post;                  // POST with the header POLL_HEADER_NAME and an empty body, else GET
	char path[POLL_PATH_SIZE];  // with the query string
} poll_request_t;

// What the caller has to do because of an answer or a request. Bits, taken with poll_take_events().
#define POLL_EVENT_BOUND    0x01u   // the display was bound to an adapter: store poll_t.bound_id
#define POLL_EVENT_CATALOG  0x02u   // store the catalogue (guard.h decided that it is time)
#define POLL_EVENT_OLD      0x04u   // `old` is another list now: store old_text (see poll_apply())
#define POLL_EVENT_LISTS    0x08u   // list, cleared or old changed: the lines for the screen are to be rebuilt
#define POLL_EVENT_FORGET   0x10u   // the adapter restarted or was replaced: values and lists were dropped, the
                                    // catalogue was started anew

// What is known of the start of the adapter that answered GET /api/state last
typedef enum
{
	POLL_START_UNKNOWN,     // nothing answered since the display started
	POLL_START_API,         // a firmware with the API: its id and its boot number name the start
	POLL_START_NO_API,      // a firmware without the API (404): nothing tells one of its starts from another
} poll_start_t;

typedef struct
{
	conn_t conn;
	values_t values;
	catalog_t catalog;
	guard_catalog_t catalog_guard;
	bool catalog_complete;      // the profile was loaded on this connection
	// The start of the adapter the catalogue came from: the one that answered GET /api/state last, a foreign
	// adapter left out (nothing is fetched from it). Kept over a pause of the network, see poll_apply().
	poll_start_t start;
	char start_id[33];          // POLL_START_API: "id" and "boot" of that state (tools/w906/API.md: boot is a
	uint32_t start_boot;        // random number chosen at boot, another one means the adapter restarted)
	dtc_flow_t flow;

	// list, list_text, cleared, old and old_text mean something only while their has_.. is true. Without it
	// anything may stand there: a result of somebody else is read into the room that is not shown.
	bool has_list;              // the result of the own read that is shown
	dtc_result_t list;
	char list_text[POLL_TEXT_SIZE];     // as the adapter sent it, for the web interface
	bool has_cleared;           // the result of the own clear
	dtc_result_t cleared;
	bool has_old;               // the list before the last clear the adapter accepted, or may have
	dtc_result_t old;
	char old_text[POLL_TEXT_SIZE];

	char bound_id[33];          // adapter this display belongs to, empty = not bound
	bool wifi;
	bool asking;                // a request was handed out and its answer is not applied yet
	poll_kind_t asked;
	uint32_t asked_result_seq;  // POLL_RESULT: the number the state named when the request was made
	uint32_t asked_age_s;       // and its age
	bool lost;                  // dtc_flow_lost() was called for the outage that is going on
	uint32_t events;
	uint32_t http_ok, http_failed;      // counters for the info page
} poll_t;

// bound_id: the stored id, NULL or empty if the display is not bound. The catalogue starts as after
// catalog_init(); a stored one is put in with poll_stored() before the first request.
void poll_init(poll_t *poll, const char *bound_id);

// What was read from the flash at the start. catalog_json and old_text may be NULL (nothing stored). A text
// that cannot be read (catalog_from_json(), dtc_result_parse()) counts as nothing stored, and so does an old
// list of POLL_TEXT_SIZE bytes or more: old_text has no room for it. What is not stored leaves its part as
// it was. The stored catalogue also tells guard.h what is in the flash (guard_catalog_init()). An old list
// that is taken raises POLL_EVENT_LISTS.
void poll_stored(poll_t *poll, const char *catalog_json, size_t catalog_length, const char *old_text,
                 size_t old_length, json_token_t *work, int work_count);

// The display joined a network and knows where the adapter is, or lost that (conn_wifi()).
// Losing it ends a request under way - its answer will be ignored - and with it a fault memory request of
// the display: dtc_flow_lost(). One that still waits to be sent was never sent and ends without a failure,
// a clear with its list shown again (dtc_flow.h). old, list and cleared follow the flow as after an answer:
// a clear whose POST was under way may have arrived, and its list becomes `old`.
// Joining forgets the values (values_clear()): the adapter may have restarted in between, and its pass
// counter means nothing then. conn asks for the profile again, so the catalogue counts as not complete
// until it is loaded, and guard.h is told (guard_catalog_connected()). The catalogue itself stays: the one
// of poll_stored() serves until the profile of this connection is loaded. Whether the adapter did restart
// in between shows with its first answer: poll_apply() then starts the catalogue anew (POLL_STATE).
// Calling it again with the same value changes nothing.
void poll_wifi(poll_t *poll, bool up, uint64_t now_ms);

// The request to send now. Returns false if there is none (a request is under way, no network, or it is not
// time yet); *request is then POLL_NONE with an empty path. A fault memory request the user started
// (dtc_flow_take() with now_ms) goes before everything else; then what conn_next() asks for. Handing out a
// clear changes nothing that is shown or stored and raises no event: the list stays `list` until the
// answers tell what became of the clear (`old` below).
bool poll_prepare(poll_t *poll, uint64_t now_ms, poll_request_t *request);

/*
 * The answer to the request that was handed out. request: what poll_prepare() filled. status: the HTTP
 * status, 0 if no answer came (timeout, connection refused or lost); a negative number counts as 0 (what
 * an HTTP client reports when it has no status). body: zero terminated, `length` bytes; NULL counts as an
 * empty one. An answer whose body is larger than the room (POLL_BODY_SIZE) did come: the caller passes the
 * status it received with an empty body (length 0), not status 0, and the request is treated as with any
 * other body it cannot read. seq_header: value of the header POLL_SEQ_HEADER, NULL if it was not sent.
 * work: POLL_TOKENS tokens.
 * A call without a request under way is ignored, and so is one whose request is NULL or of another kind
 * than the one under way: its body would be read as something it is not.
 *
 * POLL_STATE    200 and wican_state_parse() takes it: conn_got_state(OK), then dtc_flow_state(). The battery
 *               voltage of the state becomes the value CATALOG_BATTERY (in volts; no value if not measured,
 *               and none of a foreign adapter: that is not the vehicle of this display).
 *               404: conn_got_state(NOT_FOUND). Anything else, also a 200 with a body that cannot be read
 *               or is empty: FAILED.
 *               After it: conn_take_bind() -> bound_id and POLL_EVENT_BOUND. Then, if the adapter is
 *               another one or restarted - conn_take_restarted(), or another start than the one the
 *               catalogue came from, see below -: values, list, cleared (not old) are dropped, the catalogue
 *               is started anew (catalog_init(): a profile of another vehicle must not leave entries
 *               behind, and what was delivered once would stay for ever) and counts as not complete,
 *               guard_catalog_connected(), POLL_EVENT_FORGET and POLL_EVENT_LISTS, once per answer; and
 *               the flow must not go on with numbers of another adapter or boot: dtc_flow_lost(), then
 *               dtc_flow_dismiss() if it still shows a list or an outcome (LIST, CLEARED). The battery
 *               voltage of the very state that showed the restart stays: it is one of the adapter that
 *               answers now.
 *               The start the catalogue came from. conn.h knows the adapter only since the network was
 *               joined: an adapter that restarted while the display was out of the network is to conn the
 *               first answer of a connection, no restart. So the start is remembered here, over every
 *               pause of the network, until poll_init(): `start`, with start_id and start_boot of the last
 *               state that was taken from an adapter that is not foreign (a foreign one gives nothing to
 *               the catalogue and is no start it could come from; the rule of bound_id is that of conn.h,
 *               unchanged). The adapter that answers is another start if
 *                 - a state is taken, the adapter is not foreign, and the start was one with the API and
 *                   another "boot" or another "id" (the uptime does not count, as for conn.h), or the
 *                   start was a firmware without the API;
 *                 - the answer is 404 and the start was one with the API.
 *               While nothing has answered since the display started (POLL_START_UNKNOWN) no answer is
 *               another start: the catalogue of poll_stored() serves until the profile is loaded, and
 *               that one has no entry marked as delivered that the profile could not replace. Two
 *               firmwares without the API cannot be told apart. Then `start` becomes what answered.
 * POLL_RESULT   200, the header carries the number that was asked for (its decimal digits and nothing
 *               else), the body is shorter than POLL_TEXT_SIZE and dtc_result_parse() takes it:
 *               conn_got_result(OK) and dtc_flow_result(). If the flow is then LIST the result is `list`
 *               (and list_text), cleared is dropped; if it is CLEARED the result is `cleared` and the list is
 *               dropped (it lives on as `old`). A result of somebody else's request changes nothing shown.
 *               POLL_EVENT_LISTS if something changed.
 *               Status 0 (no answer): FAILED, conn asks again in the next round.
 *               Any other answer - 204, a 200 with another number in the header or without the header (the
 *               result was replaced meanwhile, the next state names the new one), a 200 whose body cannot be
 *               read, is empty or has no room in list_text, any other status: the result cannot be had, and
 *               asking again would bring the same - conn_got_result(NOT_FOUND), so that the rounds go on.
 *               If the flow waits for the very result that was asked for (READING or CLEARING with that
 *               number) and still waits after an answer came: dtc_flow_no_result(). conn does not ask for
 *               that result again, the flow would wait until its time is over. A flow that waits for
 *               another number is not concerned.
 * POLL_CATALOG  200 and catalog_apply_config() takes it: conn_got_catalog(OK), the catalogue is complete.
 *               200 with a body that cannot be used, also an empty one: conn_got_catalog(OK) as well -
 *               asking again would bring the same; the catalogue of this connection is then what the
 *               values bring. 404: NOT_FOUND.
 *               Anything else: FAILED - but an answer (not status 0) while the last state says that AutoPID
 *               is off: NOT_FOUND. The adapter has no profile then and answers 500 (API.md); conn asks for
 *               it all the same, and the failed rounds would show "no answer" for an adapter that answers.
 * POLL_VALUES   200: values_apply() with the pass counter of the last state (-1 without the API);
 *               RENEWED or REPEATED: conn_got_values(OK), and catalog_note_values() if renewed.
 *               INVALID (also an empty body), or any other status: FAILED.
 * POLL_DTC_READ, POLL_DTC_CLEAR
 *               dtc_flow_posted() with the status and with "seq" and "reason" of the body (0 and an empty
 *               reason if the body cannot be read; a status that is not 202 without a reason gets the reason
 *               "http_<status>"). "seq" is a number from 0 to 2^32-1 without fraction and exponent, "reason"
 *               a text that json_text() takes into the 32 bytes the flow keeps of it; anything else counts
 *               as 0 and as no reason. Status 0 is passed on as it is: no answer.
 *
 * After every answer, in this order:
 * - if conn_view() is NO_WIFI or NO_ANSWER: dtc_flow_lost(), once per outage
 * - the list before the last clear is only replaced by a list that was cleared or may have been: if the
 *   flow was CLEAR_SENT before the answer and has left it with the clear accepted or its outcome unknown -
 *   it is CLEARING (accepted with 202, or taken over from the state after a POST without an answer),
 *   UNKNOWN, or FAILED by the error of the very state that showed the clear as accepted - `list` becomes
 *   `old` (and list_text old_text), and POLL_EVENT_OLD is raised with POLL_EVENT_LISTS. A clear that the
 *   adapter refused, that did not arrive or was never sent has cleared nothing: `old` stays what it was.
 * - if the flow dropped its list by itself (it is not LIST, CLEAR_SENT or CLEARING any more while has_list):
 *   has_list follows the flow - a list is kept only while the flow is LIST, CLEAR_SENT or CLEARING; `cleared`
 *   only while it is CLEARED. POLL_EVENT_LISTS if something changed.
 * - guard_catalog_due() with the check sum of the catalogue: POLL_EVENT_CATALOG
 * - the counters: http_ok for every answer with a status from 200 to 499, http_failed for the rest
 */
void poll_apply(poll_t *poll, const poll_request_t *request, int status, const char *body, size_t length,
                const char *seq_header, uint64_t now_ms, json_token_t *work, int work_count);

// The events raised since the last call; they are cleared by it
uint32_t poll_take_events(poll_t *poll);

// The user asked to read / confirmed the clear at the knob: dtc_flow_read() / dtc_flow_clear() with the
// values and the catalogue of this struct. The request is sent with the next poll_prepare().
dtc_flow_block_t poll_read(poll_t *poll, uint64_t now_ms);
dtc_flow_block_t poll_clear(poll_t *poll, bool button_stuck, uint64_t now_ms);

// The user left the result or the failure: dtc_flow_dismiss(); list and cleared follow the flow as above
void poll_dismiss(poll_t *poll);

#endif
