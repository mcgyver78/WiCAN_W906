/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __DTC_FLOW_H__
#define __DTC_FLOW_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "wican_state.h"
#include "conn.h"
#include "values.h"
#include "catalog.h"

/*
 * Reading and clearing the fault memory from the display: when an action may be offered, what is sent,
 * and how the outcome is read from the state of the adapter. Pure logic; the caller sends the requests
 * (POST /api/dtc, tools/w906/API.md) and reports what came back.
 *
 * Clearing erases the fault memory of every control unit of the vehicle. So:
 *   - a clear is only offered for the list this display has read itself and is showing, for
 *     DTC_FLOW_LIST_MS after that read ended, if it has at least one trouble code
 *   - reading and clearing are only offered with the engine off: the display sits in the cab, and whether
 *     the vehicle stands still cannot be checked. Engine off means ENGINE_RPM below DTC_FLOW_RPM_LIMIT, from
 *     a value that is not gone (values_age() is not VALUE_AGE_GONE: seen less than VALUE_KEPT_MS ago; a
 *     number: "on" / "off" is no speed). A fresh one is not asked for: a polling pass of the adapter may
 *     take longer than VALUE_FRESH_MS, the speed would be unknown at random, and the adapter checks the
 *     engine itself before it clears. For a clear a value that arrived after the read ended (seen later
 *     than list_end_ms). A profile without ENGINE_RPM in its catalogue leaves the decision to the adapter.
 *   - every request is sent exactly once and never repeated by the display. If no answer comes, the state
 *     of the adapter tells what happened. A request that was never sent has done nothing: where it is
 *     withdrawn, it leaves no failure and no unknown outcome behind.
 *   - nothing waits for ever: an accepted request that has not ended DTC_FLOW_WAIT_MS after its acceptance
 *     is given up.
 *   - a read outlasts a pause of the connection, a clear does not. A scan takes 35 s, and the link of the
 *     display to the adapter drops out in phases (measured 2026-10-10 on the vehicle: the adapter had
 *     scanned all 18 control units and held the result, the display had given the read up after three
 *     rounds without an answer). So a read that was handed out waits for the adapter to answer again
 *     (dtc_flow_lost()), and what the adapter then shows decides as if there had been no pause
 *     (dtc_flow_state()); an adapter that stays away is waited for DTC_FLOW_WAIT_MS (dtc_flow_silent()). The
 *     read is not sent a second time for that. Of a clear the outcome is unknown as soon as the adapter is
 *     out of reach: nothing may be cleared a second time on a guess, the user reads again.
 *
 * Times are milliseconds of the display. A time before a stored one counts as no time passed.
 */

#define DTC_FLOW_LIST_MS        (600u * 1000u)
// An accepted request is given this long to end. A clear with entries in all 18 control units is estimated
// at 60 to 90 s, and the adapter itself gives a scan 120 s (API.md, "Sleep").
#define DTC_FLOW_WAIT_MS        (180u * 1000u)
#define DTC_FLOW_RPM_LIMIT      50.0
#define DTC_FLOW_RPM_NAME       "ENGINE_RPM"
#define DTC_FLOW_NO_ANSWER_ROUNDS 2     // answers of GET /api/state to wait for a request without an answer

typedef enum
{
	DTC_FLOW_IDLE,          // nothing read by this display, or the list was dropped
	DTC_FLOW_READ_SENT,     // POST read handed out, no answer yet
	DTC_FLOW_READING,       // own read accepted: queued, running, or done and the result on its way
	DTC_FLOW_LIST,          // the result of the own read is there
	DTC_FLOW_CLEAR_SENT,    // POST clear handed out, no answer yet
	DTC_FLOW_CLEARING,      // own clear accepted: queued, running, or done and the result on its way
	DTC_FLOW_CLEARED,       // the result of the own clear is there
	DTC_FLOW_FAILED,        // see reason
	DTC_FLOW_UNKNOWN,       // a clear was sent and its outcome cannot be had: the adapter restarted, cannot be
	                        // reached, does not give the result or did not end in time. What was cleared is
	                        // not known, the user has to read again
} dtc_flow_phase_t;

typedef enum
{
	DTC_FLOW_SEND_NOTHING,
	DTC_FLOW_SEND_READ,
	DTC_FLOW_SEND_CLEAR,
} dtc_flow_send_t;

// Why an action is not offered
typedef enum
{
	DTC_FLOW_ALLOWED,
	DTC_FLOW_NO_ADAPTER,        // no WiFi, connecting or no answer
	DTC_FLOW_FOREIGN,           // not the adapter of this display
	DTC_FLOW_NO_API,            // firmware without the API
	DTC_FLOW_AUTOPID_OFF,
	DTC_FLOW_STARTING,          // the adapter is starting, or commands are not allowed yet (conn_dtc_allowed)
	DTC_FLOW_NOT_SUPPORTED,     // the vehicle profile has no fault memory table
	DTC_FLOW_BUSY,              // a scan is queued or running, whoever started it, or a request of this
	                            // display is under way
	DTC_FLOW_ECU_OFFLINE,       // ignition off
	DTC_FLOW_ENGINE_RUNNING,
	DTC_FLOW_RPM_UNKNOWN,       // ENGINE_RPM is in the catalogue but there is no value young enough: none that is
	                            // not gone, or for a clear none from after the read
	DTC_FLOW_NO_LIST,           // clear: no list of an own read (phase is not LIST)
	DTC_FLOW_LIST_OLD,          // clear: the read ended more than DTC_FLOW_LIST_MS ago
	DTC_FLOW_NO_CODES,          // clear: the list has no trouble code
	DTC_FLOW_BUTTON_STUCK,      // clear: the switch of the knob hangs (hold.h)
} dtc_flow_block_t;

typedef struct
{
	dtc_flow_phase_t phase;
	dtc_flow_send_t to_send;        // not yet taken by dtc_flow_take()
	uint32_t boot;                  // boot number of the adapter when the request was started
	uint32_t seq_before;            // state.dtc.seq when the request was started
	uint32_t seq;                   // number of the own accepted request (read or clear)
	uint64_t accepted_ms;           // when it was accepted: the time of the 202, or of the state that showed
	                                // the request after a POST without an answer
	uint64_t sent_ms;               // when the own request was handed out (dtc_flow_take())
	uint32_t read_seq;              // number of the own read whose list is shown, 0 = there is no list
	uint32_t list_count;            // trouble codes of that list
	uint64_t list_end_ms;           // when that read ended, in the time of the display: the time the result
	                                // arrived minus its age, not before 0
	bool posted;                    // the POST was answered or given up
	int rounds_without_answer;      // states seen since a POST ended without an answer
	char reason[32];                // of DTC_FLOW_FAILED: a reason of the adapter (API.md) or one of
	                                // "no_answer", "no_result", "restarted", "superseded"; cut if it is
	                                // longer, empty if the adapter gave none
} dtc_flow_t;

void dtc_flow_init(dtc_flow_t *flow);

// The first reason that applies, in the order of the enum. values, catalog: for ENGINE_RPM.
// A clear has a list only while the last state of conn still shows the boot number and the request number
// of the own read, also before dtc_flow_state() was told otherwise. Without a list the engine is judged as
// for a read.
dtc_flow_block_t dtc_flow_read_block(const dtc_flow_t *flow, const conn_t *conn, const values_t *values,
                                     const catalog_t *catalog, uint64_t now_ms);
dtc_flow_block_t dtc_flow_clear_block(const dtc_flow_t *flow, const conn_t *conn, const values_t *values,
                                      const catalog_t *catalog, bool button_stuck, uint64_t now_ms);

// The user asked to read / confirmed the clear (hold.h). Returns the block; on DTC_FLOW_ALLOWED the request
// is ready to be taken and the phase is READ_SENT / CLEAR_SENT. A read drops the list that was shown.
dtc_flow_block_t dtc_flow_read(dtc_flow_t *flow, const conn_t *conn, const values_t *values,
                               const catalog_t *catalog, uint64_t now_ms);
dtc_flow_block_t dtc_flow_clear(dtc_flow_t *flow, const conn_t *conn, const values_t *values,
                                const catalog_t *catalog, bool button_stuck, uint64_t now_ms);

// The request to send now, handed out exactly once. *seq (seq may be NULL) is the number to send with a
// clear (the number of the read whose list is shown), 0 for a read or if there is nothing to send.
// The caller takes a request as soon as it can send one. A clear that waited until its read ended more
// than DTC_FLOW_LIST_MS ago is not handed out any more: back to LIST, nothing was sent.
// now_ms is kept as the time the request was handed out (sent_ms); a call that hands out nothing leaves it.
dtc_flow_send_t dtc_flow_take(dtc_flow_t *flow, uint32_t *seq, uint64_t now_ms);

// The answer to the POST. status 202: accepted, `seq` is the number of the request and now_ms the time of
// the acceptance. Any other status from the adapter (409, 503, 403, 400): failed with the reason of the
// body ("busy", "read_required", ...); after a failed clear the list stays (phase LIST is NOT restored: the
// user reads again), after a failed read there is no list. status 0: no answer (timeout, connection lost) -
// the request may or may not have arrived; the next answers of GET /api/state decide, see dtc_flow_state().
// So does a 202 without a number (seq 0). reason may be NULL. A call without a request that was taken and
// has no answer yet is ignored.
void dtc_flow_posted(dtc_flow_t *flow, int status, uint32_t seq, const char *reason, uint64_t now_ms);

// Every answer of GET /api/state goes through here (NULL is ignored).
// - another boot number than when the request was started: a request that still waits to be taken was
//   never sent and has done nothing -> IDLE, a read as well as a clear (the list of the clear is dropped
//   with it), and it is not handed out any more. One that was taken: READ_SENT or READING -> FAILED
//   "restarted"; CLEAR_SENT or CLEARING -> UNKNOWN. LIST and CLEARED are dropped (IDLE): their numbers mean
//   nothing now.
// - while the request waits to be taken or its POST is under way, a state of the same boot changes
//   nothing, whatever it shows: the adapter decides when the request arrives
// - own request accepted (READING / CLEARING) and the state shows this number as error -> FAILED with the
//   reason of the state; as done -> stay until dtc_flow_result() brings the result
// - own request accepted and the state shows a later number: the own one ended and another started before
//   the display saw the end. If the stored result still belongs to the own number, wait for it; else
//   FAILED "superseded" (after a clear: UNKNOWN).
// - a POST without answer: if the state shows a number other than seq_before, from HTTP, with the action
//   that was sent, the request did arrive: continue as accepted with that number, now_ms being the time of
//   the acceptance. If the number is still seq_before after DTC_FLOW_NO_ANSWER_ROUNDS states, it did not
//   arrive: a read -> FAILED "no_answer", a clear -> back to LIST (the user may confirm again). Another
//   number that is not from HTTP, has another action or is 0: somebody else got in and the own request
//   cannot be found -> FAILED "superseded" (after a clear: UNKNOWN).
// - own request accepted and, after all of the above, still READING / CLEARING more than DTC_FLOW_WAIT_MS
//   after its acceptance (a state exactly DTC_FLOW_WAIT_MS after it still waits): READING -> FAILED
//   "no_answer", CLEARING -> UNKNOWN. What the state shows goes first: an error that is seen late keeps its
//   reason. Here only a state ends the wait, and a result that arrives late is taken; the wait of a read
//   whose adapter sends no state any more is ended by dtc_flow_silent().
// - LIST: a state whose number is no longer the one of the own read (somebody else started a scan) drops
//   the list -> IDLE; the adapter would refuse the clear anyway.
void dtc_flow_state(dtc_flow_t *flow, const wican_state_t *state, uint64_t now_ms);

// A result was fetched: its number, action, trouble code count and age (age_s of the state it was asked
// with). READING + the own number + a read -> LIST; CLEARING + the own number + a clear -> CLEARED. Anything
// else is ignored.
void dtc_flow_result(dtc_flow_t *flow, uint32_t result_seq, bool clear, uint32_t count, uint32_t age_s,
                     uint64_t now_ms);

// The result of the own request cannot be had: the adapter answered, but not with a result that can be read
// (poll.h), and asking again would bring the same. READING -> FAILED "no_result"; CLEARING -> UNKNOWN; every
// other phase stays as it is.
void dtc_flow_no_result(dtc_flow_t *flow);

// The adapter is out of reach for now: the display left the network, or no answer comes any more. It may
// answer again in a moment, with the same boot number and the scan of the display still running or done.
// - A request that still waits to be taken was never sent and has done nothing: it is not handed out any
//   more, a clear goes back to LIST (the user may confirm again while the list is still good), a read to
//   IDLE.
// - A clear that was taken: CLEAR_SENT / CLEARING -> UNKNOWN.
// - A read that was taken stays what it is, READ_SENT or READING, and waits. The end of a POST that was
//   under way need not be reported any more, it would be ignored: that POST counts as ended without an
//   answer from now on. When the adapter answers again its states decide by the rules of dtc_flow_state() -
//   READING: the own number still queued or running -> the read goes on; done -> its result makes the list;
//   an error -> FAILED with the reason of the state; another number without the own result -> FAILED
//   "superseded"; another boot number -> FAILED "restarted". READ_SENT: a new number from HTTP that is a
//   read -> it did arrive, on as READING; DTC_FLOW_NO_ANSWER_ROUNDS states with the number of before ->
//   FAILED "no_answer". Nothing is sent a second time. If the adapter stays away: dtc_flow_silent().
// - LIST and CLEARED stay.
// Calling it again while the adapter is still out of reach changes nothing.
void dtc_flow_lost(dtc_flow_t *flow);

// The adapter the request went to is not there any more: another adapter answers in its place, or a
// firmware without the API (conn_take_restarted(), poll.h) - a restart that shows in the boot number is seen
// by dtc_flow_state() as well. What dtc_flow_lost() does, and a read that was taken does not wait: no state
// of this adapter says anything about it. READ_SENT / READING -> FAILED "no_answer".
void dtc_flow_gone(dtc_flow_t *flow);

// Time passed while the adapter was out of sight (no network, no answer in it yet, or none any more): no
// state comes that could end the wait of a read. A read that was taken is given up when its time is over:
// READING more than DTC_FLOW_WAIT_MS after its acceptance, READ_SENT more than DTC_FLOW_WAIT_MS after it was
// handed out (at exactly DTC_FLOW_WAIT_MS both still wait) -> FAILED "no_answer". A result or a state that
// comes later finds a failure and changes nothing. Everything else stays as it is, whatever the time: a read
// that still waits to be taken, and a clear in every phase - dtc_flow_lost() has made its outcome unknown,
// it never waits for an adapter that is out of sight.
void dtc_flow_silent(dtc_flow_t *flow, uint64_t now_ms);

// The user left the result or the failure: back to IDLE. While a request is under way (READ_SENT, READING,
// CLEAR_SENT, CLEARING) nothing changes, it goes on and its outcome is kept.
void dtc_flow_dismiss(dtc_flow_t *flow);

// Seconds left to clear the list that is shown, rounded up; 0 if there is none or the time is over
uint32_t dtc_flow_seconds_left(const dtc_flow_t *flow, uint64_t now_ms);

#endif
