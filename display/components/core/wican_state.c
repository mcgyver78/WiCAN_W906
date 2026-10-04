/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "wican_state.h"

// The texts of a member in the order of its values, closed by NULL. Where the value is no enum, the
// position is false / true or none / first / second.
static const char *const AUTOPID[] = {
	[WICAN_AUTOPID_OFF] = "off",
	[WICAN_AUTOPID_STARTING] = "starting",
	[WICAN_AUTOPID_RUN] = "run",
	NULL,
};
static const char *const MQTT[] = {
	[WICAN_MQTT_OFF] = "off",
	[WICAN_MQTT_CONNECTED] = "connected",
	[WICAN_MQTT_DISCONNECTED] = "disconnected",
	NULL,
};
static const char *const PHASE[] = {
	[WICAN_DTC_IDLE] = "idle",
	[WICAN_DTC_QUEUED] = "queued",
	[WICAN_DTC_RUNNING] = "running",
	[WICAN_DTC_DONE] = "done",
	[WICAN_DTC_ERROR] = "error",
	NULL,
};
static const char *const ECU[] = {"offline", "online", NULL};
static const char *const ACTION[] = {"", "read", "clear", NULL};
static const char *const SOURCE[] = {"", "mqtt", "http", NULL};

// Text of a string for display: as much of it as fits, cut at a character boundary of UTF-8.
// false if the token is no string.
static bool copy_text(const char *json, const json_token_t *token, char *out, size_t size)
{
	json_token_t piece = *token;
	uint32_t end = token->start + token->length;
	size_t length = 0;
	size_t boundary = 0;

	if(token->type != JSON_STRING) return false;

	// One byte or one escape at a time: json_text() resolves it, but refuses a text that does not fit
	while(piece.start < end)
	{
		char bytes[5];
		size_t count;

		piece.length = 1;
		if(json[piece.start] == '\\') piece.length = json[piece.start + 1] == 'u' ? 6 : 2;
		if(!json_text(json, &piece, bytes, sizeof(bytes)))
		{
			// The first half of a surrogate pair is a character only together with the second.
			// Anything else json_text() refuses ends the text.
			piece.length = 12;
			if(piece.start + 12 > end || !json_text(json, &piece, bytes, sizeof(bytes))) break;
		}

		count = strlen(bytes);
		if(((unsigned char)bytes[0] & 0xC0) != 0x80) boundary = length;
		if(length + count + 1 > size)
		{
			// Not the first bytes of a character without its last
			length = boundary;
			break;
		}
		memcpy(&out[length], bytes, count);
		length += count;
		piece.start += piece.length;
	}
	out[length] = '\0';
	return true;
}

static bool read_text(const char *json, const json_token_t *tokens, int object, const char *key, char *out, size_t size)
{
	int index = json_member(json, tokens, object, key);

	return index >= 0 && copy_text(json, &tokens[index], out, size);
}

// A number without fraction and exponent from `lowest` to `highest`
static bool read_integer(const char *json, const json_token_t *tokens, int object, const char *key,
                         int64_t lowest, int64_t highest, int64_t *value)
{
	int index = json_member(json, tokens, object, key);

	return index >= 0 && json_integer(json, &tokens[index], value) && *value >= lowest && *value <= highest;
}

static bool read_unsigned(const char *json, const json_token_t *tokens, int object, const char *key, uint32_t *value)
{
	int64_t number;

	if(!read_integer(json, tokens, object, key, 0, UINT32_MAX, &number)) return false;
	*value = (uint32_t)number;
	return true;
}

// -1 stands for "none"
static bool read_signed(const char *json, const json_token_t *tokens, int object, const char *key, int32_t *value)
{
	int64_t number;

	if(!read_integer(json, tokens, object, key, -1, INT32_MAX, &number)) return false;
	*value = (int32_t)number;
	return true;
}

// Position of the text of member `key` in `texts`. false if it is none of them.
static bool read_choice(const char *json, const json_token_t *tokens, int object, const char *key,
                        const char *const *texts, int *choice)
{
	int index = json_member(json, tokens, object, key);

	if(index < 0) return false;

	for(*choice = 0; texts[*choice] != NULL; (*choice)++)
	{
		if(json_text_is(json, &tokens[index], texts[*choice])) return true;
	}
	return false;
}

// Volts as millivolts, taken from the digits: 12.4 is not exactly 12.4 as a floating point number, and
// the result must not depend on how it is rounded.
static bool read_millivolts(const char *json, const json_token_t *tokens, int object, const char *key, int32_t *value)
{
	int index = json_member(json, tokens, object, key);
	const char *s;
	int64_t millivolts = 0;
	int decimals = 0;
	bool point = false;
	bool zero = true;
	bool negative;

	if(index < 0 || tokens[index].type != JSON_NUMBER) return false;

	s = json + tokens[index].start;
	negative = s[0] == '-';
	for(uint32_t i = negative ? 1 : 0; i < tokens[index].length; i++)
	{
		if(s[i] == '.')
		{
			point = true;
			continue;
		}
		// Written with an exponent. Everything else behind the sign is a digit, the reader has seen to that.
		if(s[i] == 'e' || s[i] == 'E') return false;

		if(s[i] != '0') zero = false;
		// Less than a millivolt
		if(decimals == 3) continue;
		if(point) decimals++;
		// A sum that is too large stays too large; no digit is added to it, they may be many
		if(millivolts <= INT32_MAX) millivolts = millivolts * 10 + (s[i] - '0');
	}
	for(; decimals < 3; decimals++) millivolts *= 10;

	if(negative)
	{
		// "Not measured". Minus zero is zero.
		*value = zero ? 0 : -1;
		return true;
	}
	if(millivolts > INT32_MAX) return false;
	*value = (int32_t)millivolts;
	return true;
}

static bool read_dtc(const char *json, const json_token_t *tokens, int object, wican_dtc_t *dtc)
{
	int supported = json_member(json, tokens, object, "supported");
	int phase, action, source;

	if(supported < 0 || (tokens[supported].type != JSON_TRUE && tokens[supported].type != JSON_FALSE)) return false;
	if(!read_choice(json, tokens, object, "state", PHASE, &phase) ||
	   !read_choice(json, tokens, object, "action", ACTION, &action) ||
	   !read_choice(json, tokens, object, "src", SOURCE, &source)) return false;

	dtc->supported = tokens[supported].type == JSON_TRUE;
	dtc->phase = (wican_dtc_phase_t)phase;
	dtc->has_request = action != 0 && source != 0;
	dtc->clear = action == 2;
	dtc->from_http = source == 2;

	return read_unsigned(json, tokens, object, "seq", &dtc->seq) &&
	       read_unsigned(json, tokens, object, "ecu", &dtc->step) &&
	       read_unsigned(json, tokens, object, "total", &dtc->total) &&
	       read_text(json, tokens, object, "name", dtc->name, sizeof(dtc->name)) &&
	       read_text(json, tokens, object, "reason", dtc->reason, sizeof(dtc->reason)) &&
	       read_unsigned(json, tokens, object, "age_s", &dtc->age_s) &&
	       read_unsigned(json, tokens, object, "count", &dtc->count) &&
	       read_unsigned(json, tokens, object, "result_seq", &dtc->result_seq);
}

bool wican_state_parse(const char *json, size_t length, wican_state_t *state, json_token_t *work, int work_count)
{
	// Read into a copy: *state has to stay as it is if a later member is wrong
	wican_state_t read;
	int64_t api;
	int autopid, ecu, mqtt;

	if(json_parse(json, length, work, work_count) < 0) return false;

	if(!read_integer(json, work, 0, "api", 1, 1, &api)) return false;
	if(!read_choice(json, work, 0, "autopid", AUTOPID, &autopid) ||
	   !read_choice(json, work, 0, "ecu", ECU, &ecu) ||
	   !read_choice(json, work, 0, "mqtt", MQTT, &mqtt)) return false;

	read.autopid = (wican_autopid_t)autopid;
	read.ecu_online = ecu == 1;
	read.mqtt = (wican_mqtt_t)mqtt;

	if(!read_text(json, work, 0, "id", read.id, sizeof(read.id)) ||
	   !read_text(json, work, 0, "fw", read.fw, sizeof(read.fw)) ||
	   !read_text(json, work, 0, "git", read.git, sizeof(read.git)) ||
	   !read_unsigned(json, work, 0, "boot", &read.boot) ||
	   !read_unsigned(json, work, 0, "up", &read.up_s) ||
	   !read_unsigned(json, work, 0, "pids", &read.pids) ||
	   !read_unsigned(json, work, 0, "pass", &read.pass) ||
	   !read_signed(json, work, 0, "rx_age_ms", &read.rx_age_ms) ||
	   !read_millivolts(json, work, 0, "batt_v", &read.batt_mv) ||
	   !read_signed(json, work, 0, "sleep_in_s", &read.sleep_in_s) ||
	   !read_unsigned(json, work, 0, "heap", &read.heap) ||
	   !read_unsigned(json, work, 0, "heap_min", &read.heap_min) ||
	   !read_dtc(json, work, json_member(json, work, 0, "dtc"), &read.dtc)) return false;

	*state = read;
	return true;
}
