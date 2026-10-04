/*
 * This file is part of the WiCAN project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __DTC_API_H__
#define __DTC_API_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "dtc_state.h"

/*
 * Decisions and texts of the HTTP API for standalone clients, see tools/w906/API.md.
 * Plain C without ESP-IDF, tested on the host (tools/w906/dtc_api_test.c). main/dtc_http.c only fetches
 * the header fields and the query from the request, calls these functions and sends what they return.
 */

// Largest body dtc_api_body() writes, with the terminating zero
#define DTC_API_BODY_SIZE       96

typedef struct
{
	int status;             // 0: go on and ask the scan state, else the HTTP status to answer with (403, 400)
	const char *reason;     // "forbidden" or "bad_request" if status is not 0, else NULL
	bool clear;
	uint32_t seq;           // number given with a clear, 0 for a read
} dtc_api_request_t;

// First part of a POST /api/dtc: who may ask, and what is asked.
// header: value of the request header X-WiCAN-DTC, NULL if absent
// host:   value of the Host header, NULL if absent
// query:  text behind the '?', NULL if there is none
dtc_api_request_t dtc_api_parse_request(const char *header, const char *host, const char *query);

// Host header of a request to the adapter itself: an IPv4 address or wican_<id>.local, with or without port.
// Refuses the name of a foreign web page that was pointed at the address of the adapter (DNS rebinding).
bool dtc_api_host_allowed(const char *host);

// Whether a request may be started at all, asked before the rules of the scan state; over MQTT too.
// task_ready: the AutoPID task can take the request (HTTP: it is in its loop, MQTT: it exists).
// sleep_in_s: the countdown GET /api/state reports, negative while the adapter is not counting down.
// At 0 the adapter is due to sleep and only stays awake for a scan accepted earlier; one accepted now
// could be cut off half way.
bool dtc_api_ready(bool task_ready, int32_t sleep_in_s);

// Second part: HTTP status for the answer of the scan state. ready is the answer of dtc_api_ready();
// if it is false, result is ignored.
int dtc_api_status(bool ready, dtc_accept_t result);

// Body of every answer to POST /api/dtc: {"accepted":true,"seq":43} for status 202, else
// {"accepted":false,"reason":"...","seq":42}. reason: text for the status, see API.md; for 409 the text of
// dtc_accept_reason(). Returns the length, or -1 if size is smaller than needed.
int dtc_api_body(int status, const char *reason, uint32_t seq, char *body, size_t size);

// Everything GET /api/state reports apart from the scan state
typedef struct
{
	const char *id;         // device id
	const char *fw;         // version text of the application image, at most 31 bytes; in builds of this
	                        // repository the same text as git (no version.txt, so ESP-IDF takes git describe)
	const char *git;        // git describe of the build
	uint32_t boot;          // random number of this boot, 1..2^31-1
	uint32_t up_s;
	const char *autopid;    // "off", "starting" or "run"
	uint32_t pids;
	bool ecu_online;
	uint32_t pass;          // polling passes with at least one answered request
	int32_t rx_age_ms;      // since the last answered request, -1 = none since boot
	const char *mqtt;       // "off", "connected" or "disconnected"
	int32_t batt_mv;        // battery voltage in millivolts, negative = not measured
	int32_t sleep_in_s;     // -1 = not counting down
	uint32_t heap;
	uint32_t heap_min;
} dtc_api_status_t;

// The answer to GET /api/state. dtc_json is the object written by dtc_state_json().
// Numbers above 2^31-1 are written as 2147483647. Returns the length, or -1 if it does not fit
// (buf is then an empty string, with size 0 nothing is written).
int dtc_api_state_json(const dtc_api_status_t *status, const char *dtc_json, char *buf, size_t size);

// The adapter is about to go to sleep, which switches CAN and WiFi off. Returns true if that has to wait.
// scan_holds: the answer of dtc_state_hold_awake(). overdue_ms: how long the adapter should already be
// asleep. The wait ends after DTC_API_SLEEP_DEFER_MS in any case, whatever the state says. The limit is
// longer than a scan accepted before sleep was due and the time its result is held, so it cuts neither;
// it is there for scans that follow each other, which dtc_api_ready() does not let in.
#define DTC_API_SLEEP_DEFER_MS  (150u * 1000u)
bool dtc_api_defer_sleep(bool scan_holds, uint64_t overdue_ms);

#endif
