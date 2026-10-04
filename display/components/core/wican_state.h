/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __WICAN_STATE_H__
#define __WICAN_STATE_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "json.h"

/*
 * The answer of GET /api/state as a struct. The contract is tools/w906/API.md, examples are
 * tools/w906/fixtures/api_state_*.json and dtc_state_*.json.
 */

#define WICAN_STATE_TOKENS  96      // tokens wican_state_parse() needs

typedef enum
{
	WICAN_AUTOPID_OFF,
	WICAN_AUTOPID_STARTING,
	WICAN_AUTOPID_RUN,
} wican_autopid_t;

typedef enum
{
	WICAN_MQTT_OFF,
	WICAN_MQTT_CONNECTED,
	WICAN_MQTT_DISCONNECTED,
} wican_mqtt_t;

typedef enum
{
	WICAN_DTC_IDLE,
	WICAN_DTC_QUEUED,
	WICAN_DTC_RUNNING,
	WICAN_DTC_DONE,
	WICAN_DTC_ERROR,
} wican_dtc_phase_t;

typedef struct
{
	bool supported;
	wican_dtc_phase_t phase;
	bool has_request;       // action and source are set (they are empty while idle)
	bool clear;
	bool from_http;
	uint32_t seq;
	uint32_t step;          // "ecu"
	uint32_t total;
	char name[64];
	char reason[32];
	uint32_t age_s;
	uint32_t count;
	uint32_t result_seq;
} wican_dtc_t;

typedef struct
{
	char id[33];
	char fw[40];
	char git[48];
	uint32_t boot;
	uint32_t up_s;
	wican_autopid_t autopid;
	uint32_t pids;
	bool ecu_online;
	uint32_t pass;
	int32_t rx_age_ms;      // -1 = none since boot
	wican_mqtt_t mqtt;
	int32_t batt_mv;        // from "batt_v", -1 = not measured
	int32_t sleep_in_s;     // -1 = not counting down
	uint32_t heap;
	uint32_t heap_min;
	wican_dtc_t dtc;
} wican_state_t;

// Fills *state from the answer. Returns false and leaves *state untouched if the text is not what API.md
// describes for "api":1: broken JSON, a required member missing or of the wrong type, a number that is
// negative where none is allowed, has a fraction or is above 2^32-1 (batt_v excepted), a number written
// with an exponent, rx_age_ms or sleep_in_s below -1 or above 2^31-1, an unknown text for
// autopid, ecu, mqtt, dtc.state, dtc.action or dtc.src, or "api" other than 1.
// Members the contract does not know are ignored: a later firmware may add some.
// Texts longer than their field are cut at a character boundary of UTF-8, they are for display only. A text
// also ends before an escape json_text() refuses (\u0000, half a surrogate pair).
// batt_v 12.4 becomes 12400, a negative batt_v becomes -1. Digits below a millivolt are dropped; what is
// then above 2147483.647 (2^31-1 millivolts) is refused. Minus zero is zero, for batt_v and for every
// other number.
// work, work_count: room for the JSON reader (json.h), provided by the caller because it is too large for
// a task stack. A text that needs more tokens than that is treated like broken JSON.
bool wican_state_parse(const char *json, size_t length, wican_state_t *state, json_token_t *work, int work_count);

#endif
