/*
 * This file is part of the WiCAN project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __DTC_STATE_H__
#define __DTC_STATE_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * State of the fault memory scan (read_dtc / clear_dtc). One state for both triggers, the MQTT command
 * and the HTTP API, so that a scan started by one of them is visible to the other.
 *
 * Plain C without ESP-IDF: the rules are tested on the host (tools/w906/dtc_state_test.c) and
 * tools/w906/redproof.py shows that each rule makes a test fail when it is removed.
 *
 * Time is passed in as milliseconds since boot. It wraps after 49 days, only differences are used.
 * Not thread safe: the caller holds one mutex around every call.
 */

typedef enum
{
	DTC_STATE_IDLE,     // nothing requested since boot
	DTC_STATE_QUEUED,   // accepted, the AutoPID task has not picked it up yet
	DTC_STATE_RUNNING,
	DTC_STATE_DONE,
	DTC_STATE_ERROR,
} dtc_phase_t;

typedef enum
{
	DTC_SRC_MQTT,
	DTC_SRC_HTTP,
} dtc_src_t;

typedef enum
{
	DTC_ACCEPTED,
	DTC_REJECT_BUSY,              // a scan is queued or running, whoever started it
	DTC_REJECT_READ_REQUIRED,     // clear without a finished read right before it, or that read is too old
	DTC_REJECT_STALE_SEQ,         // clear refers to another read than the last one
	DTC_REJECT_NOTHING_TO_CLEAR,  // the last read found no trouble codes
} dtc_accept_t;

// A clear over HTTP is only accepted this long after the read it refers to
#define DTC_CLEAR_MAX_AGE_MS    (600u * 1000u)
// An HTTP request that waited longer than this for the AutoPID task is dropped instead of run late
#define DTC_HTTP_EXPIRY_MS      (20u * 1000u)
// Sequence numbers stay below 2^31 so that every JSON parser reads them exactly, 0 means "none"
#define DTC_SEQ_MAX             0x7FFFFFFFu

typedef struct
{
	dtc_phase_t phase;
	bool clear;             // action of the last accepted request
	dtc_src_t src;          // who sent the last accepted request
	uint32_t seq;           // number of the last accepted request, 0 = none since boot
	uint32_t next_seq;
	uint32_t queued_ms;
	uint32_t finished_ms;
	uint8_t step;           // 0 = engine check, 1..total = control unit being processed
	uint8_t total;
	const char *name;       // control unit being processed, points into a constant table
	const char *reason;     // of the last error, points to a string literal
	uint32_t result_seq;    // request the stored result belongs to, 0 = no result stored
	uint16_t result_count;  // trouble codes in the stored result
} dtc_state_t;

// seed: any random number, makes the sequence numbers differ from boot to boot
void dtc_state_init(dtc_state_t *s, uint32_t seed);

// Accept or reject a request. Nothing changes on rejection.
// check_seq binds a clear to the read with number `seq` (HTTP); without it a clear is unbound (MQTT, as before).
// seq_out (may be NULL) receives the number of the accepted request.
dtc_accept_t dtc_state_try_begin(dtc_state_t *s, bool clear, dtc_src_t src, bool check_seq, uint32_t seq,
                                 uint32_t now_ms, uint32_t *seq_out);

// The AutoPID task takes the queued request. Returns true if the scan has to run now, false if nothing
// is queued or the request expired (the state is then an error with reason "expired").
bool dtc_state_pickup(dtc_state_t *s, uint32_t now_ms);

void dtc_state_progress(dtc_state_t *s, uint8_t step, uint8_t total, const char *name);

// End of a queued or running request; ignored in every other phase, so a rejected command cannot
// overwrite a finished result.
void dtc_state_error(dtc_state_t *s, const char *reason, uint32_t now_ms);
void dtc_state_done(dtc_state_t *s, uint16_t dtc_count, uint32_t now_ms);

bool dtc_state_busy(const dtc_state_t *s);

// Text for a rejection ("busy", "read_required", "stale_seq", "nothing_to_clear"), NULL for DTC_ACCEPTED
const char *dtc_accept_reason(dtc_accept_t result);

// The state as a JSON object, e.g.
// {"supported":true,"state":"running","action":"read","src":"http","seq":42,"ecu":5,"total":18,
//  "name":"N30/4 ESP","reason":"","age_s":0,"count":0,"result_seq":41}
// Returns the length without the terminating zero, or -1 if it does not fit (buf is then an empty string).
int dtc_state_json(const dtc_state_t *s, bool supported, uint32_t now_ms, char *buf, size_t size);

#endif
