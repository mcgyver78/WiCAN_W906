/*
 * This file is part of the WiCAN project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "dtc_api.h"

// API.md promises numbers below 2^31, every JSON parser reads them exactly
#define API_NUMBER_MAX  0x7FFFFFFFu

/* Who may ask ------------------------------------------------------------------------------------- */

// isdigit() and isxdigit() depend on the locale and are undefined for a char above 0x7F
static bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static bool is_hex(char c)
{
	return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Returns the text behind `word` (given in lower case) if `text` starts with it in any case, else NULL
static const char *skip_word(const char *text, const char *word)
{
	for(; *word != '\0'; word++, text++)
	{
		char c = *text;

		if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		// The end of the text differs from every letter of the word, nothing is read behind it
		if(c != *word) return NULL;
	}
	return text;
}

// Four numbers 0..255 of 1 to 3 digits with dots between them. Returns the text behind them, else NULL.
// Not inet_aton(): it also takes "127.1", "0x7f.0.0.1" and a single 32 bit number.
static const char *skip_ipv4(const char *text)
{
	int part;

	for(part = 0; part < 4; part++)
	{
		unsigned value = 0;
		int digits = 0;

		if(part > 0)
		{
			if(*text != '.') return NULL;
			text++;
		}
		while(digits < 3 && is_digit(*text))
		{
			value = value * 10u + (unsigned)(*text - '0');
			text++;
			digits++;
		}
		if(digits == 0 || value > 255u) return NULL;
	}
	return text;
}

// The name the adapter announces over mDNS: wican_<id>.local. Returns the text behind it, else NULL.
static const char *skip_mdns_name(const char *text)
{
	size_t id_length = 0;

	text = skip_word(text, "wican_");
	if(text == NULL) return NULL;

	while(is_hex(text[id_length])) id_length++;
	if(id_length < 1 || id_length > 32) return NULL;

	return skip_word(text + id_length, ".local");
}

// Nothing, or a colon and 1 to 5 digits. The number is not looked at: the check is about the name a browser
// was given, and the name of a foreign page stays foreign with every port.
static bool is_port_or_end(const char *text)
{
	size_t digits = 0;

	if(*text == '\0') return true;
	if(*text != ':') return false;
	text++;

	while(is_digit(text[digits])) digits++;
	return digits >= 1 && digits <= 5 && text[digits] == '\0';
}

bool dtc_api_host_allowed(const char *host)
{
	const char *rest;

	if(host == NULL) return false;

	// A name that only starts like an address ("192.168.80.1.example.com") fails at what is left over
	rest = skip_ipv4(host);
	if(rest == NULL) rest = skip_mdns_name(host);
	return rest != NULL && is_port_or_end(rest);
}

/* What is asked ----------------------------------------------------------------------------------- */

// Value of the first parameter called `name` in "a=1&b=2". The value is not terminated, it has `length`
// bytes. A parameter without '=' has an empty value. Nothing is percent-decoded: the texts the API takes
// need no escaping, and a decoded "%26" would not be a separator anyway.
static bool find_parameter(const char *query, const char *name, const char **value, size_t *length)
{
	size_t name_length = strlen(name);

	if(query == NULL) return false;

	while(*query != '\0')
	{
		size_t parameter_length = strcspn(query, "&");
		size_t found_length = strcspn(query, "&=");

		if(found_length == name_length && memcmp(query, name, name_length) == 0)
		{
			// Behind the name is '=', '&' or the end
			*value = query + name_length + (query[name_length] == '=' ? 1 : 0);
			*length = (size_t)(query + parameter_length - *value);
			return true;
		}

		query += parameter_length;
		if(*query == '&') query++;
	}
	return false;
}

static bool value_is(const char *value, size_t length, const char *text)
{
	return length == strlen(text) && memcmp(value, text, length) == 0;
}

// 1 to 10 decimal digits with a value of 1..2^31-1. Not strtoul(): it also takes leading blanks and a sign,
// "-1" becomes the largest number.
static bool parse_seq(const char *text, size_t length, uint32_t *seq)
{
	uint64_t value = 0;
	size_t i;

	// An empty text has the value 0 and is refused with it further down
	if(length > 10) return false;

	for(i = 0; i < length; i++)
	{
		if(!is_digit(text[i])) return false;
		// 10 digits stay far below 2^64, the range is checked once at the end
		value = value * 10u + (uint64_t)(text[i] - '0');
	}
	if(value < 1 || value > DTC_SEQ_MAX) return false;

	*seq = (uint32_t)value;
	return true;
}

dtc_api_request_t dtc_api_parse_request(const char *header, const char *host, const char *query)
{
	dtc_api_request_t request = {403, "forbidden", false, 0};
	const char *value = NULL;
	size_t length = 0;
	uint32_t seq = 0;

	// A browser sends a custom header to another origin only after a preflight, which the adapter does not
	// answer. This is checked before anything of the request is interpreted.
	if(header == NULL || strcmp(header, "1") != 0) return request;
	if(!dtc_api_host_allowed(host)) return request;

	request.status = 400;
	request.reason = "bad_request";

	if(!find_parameter(query, "action", &value, &length)) return request;

	if(value_is(value, length, "clear"))
	{
		// A clear without the number of a read would clear a list nobody has looked at
		if(!find_parameter(query, "seq", &value, &length)) return request;
		if(!parse_seq(value, length, &seq)) return request;
		request.clear = true;
		request.seq = seq;
	}
	else if(!value_is(value, length, "read")) return request;

	request.status = 0;
	request.reason = NULL;
	return request;
}

int dtc_api_status(bool ready, dtc_accept_t result)
{
	// API.md: not_ready is decided before the rules of the scan state
	if(!ready) return 503;
	return result == DTC_ACCEPTED ? 202 : 409;
}

/* Answers ----------------------------------------------------------------------------------------- */

typedef struct
{
	char *buf;
	size_t size;
	size_t len;
	bool overflow;
} json_out_t;

static void put_char(json_out_t *out, char c)
{
	// One byte stays free for the terminating zero
	if(out->len + 1 >= out->size)
	{
		out->overflow = true;
		return;
	}
	out->buf[out->len++] = c;
}

static void put_raw(json_out_t *out, const char *text)
{
	while(*text != '\0') put_char(out, *text++);
}

// Text as JSON string content: quote and backslash escaped, control characters dropped, UTF-8 passed on
static void put_escaped(json_out_t *out, const char *text)
{
	if(text == NULL) return;

	for(; *text != '\0'; text++)
	{
		unsigned char c = (unsigned char)*text;

		if(c < 0x20) continue;
		if(c == '"' || c == '\\') put_char(out, '\\');
		put_char(out, (char)c);
	}
}

static void put_number(json_out_t *out, uint32_t value)
{
	char digits[10];
	size_t count = 0;

	if(value > API_NUMBER_MAX) value = API_NUMBER_MAX;

	do
	{
		digits[count++] = (char)('0' + value % 10u);
		value /= 10u;
	}
	while(value != 0);

	while(count > 0) put_char(out, digits[--count]);
}

// For the fields in which -1 stands for "none"
static void put_number_or_none(json_out_t *out, int32_t value)
{
	if(value < 0) put_raw(out, "-1");
	else put_number(out, (uint32_t)value);
}

// Volts with one decimal, rounded to the nearest tenth. Integer arithmetic: 12.45 is not a binary fraction,
// a float printf rounds it down or up depending on its representation.
static void put_volts(json_out_t *out, int32_t millivolts)
{
	uint32_t tenths;

	if(millivolts < 0)
	{
		put_raw(out, "-1");
		return;
	}

	// Unsigned, the largest value plus the rounding does not fit into int32_t
	tenths = ((uint32_t)millivolts + 50u) / 100u;
	put_number(out, tenths / 10u);
	put_char(out, '.');
	put_char(out, (char)('0' + tenths % 10u));
}

// Either the complete text or an empty one: a lenient parser would take a truncated object for the answer
static int end_json(json_out_t *out)
{
	if(out->overflow)
	{
		out->buf[0] = '\0';
		return -1;
	}

	out->buf[out->len] = '\0';
	return (int)out->len;
}

int dtc_api_body(int status, const char *reason, uint32_t seq, char *body, size_t size)
{
	json_out_t out = {body, size, 0, false};

	if(size == 0) return -1;

	if(status == 202)
	{
		put_raw(&out, "{\"accepted\":true");
	}
	else
	{
		put_raw(&out, "{\"accepted\":false,\"reason\":\"");
		put_escaped(&out, reason);
		put_char(&out, '"');
	}
	put_raw(&out, ",\"seq\":");
	put_number(&out, seq);
	put_char(&out, '}');

	return end_json(&out);
}

int dtc_api_state_json(const dtc_api_status_t *status, const char *dtc_json, char *buf, size_t size)
{
	json_out_t out = {buf, size, 0, false};

	if(size == 0) return -1;

	put_raw(&out, "{\"api\":1,\"id\":\"");
	put_escaped(&out, status->id);
	put_raw(&out, "\",\"fw\":\"");
	put_escaped(&out, status->fw);
	put_raw(&out, "\",\"git\":\"");
	put_escaped(&out, status->git);
	put_raw(&out, "\",\"boot\":");
	put_number(&out, status->boot);
	put_raw(&out, ",\"up\":");
	put_number(&out, status->up_s);
	put_raw(&out, ",\"autopid\":\"");
	put_escaped(&out, status->autopid);
	put_raw(&out, "\",\"pids\":");
	put_number(&out, status->pids);
	put_raw(&out, ",\"ecu\":\"");
	put_raw(&out, status->ecu_online ? "online" : "offline");
	put_raw(&out, "\",\"pass\":");
	put_number(&out, status->pass);
	put_raw(&out, ",\"rx_age_ms\":");
	put_number_or_none(&out, status->rx_age_ms);
	put_raw(&out, ",\"mqtt\":\"");
	put_escaped(&out, status->mqtt);
	put_raw(&out, "\",\"batt_v\":");
	put_volts(&out, status->batt_mv);
	put_raw(&out, ",\"sleep_in_s\":");
	put_number_or_none(&out, status->sleep_in_s);
	put_raw(&out, ",\"heap\":");
	put_number(&out, status->heap);
	put_raw(&out, ",\"heap_min\":");
	put_number(&out, status->heap_min);
	put_raw(&out, ",\"dtc\":");
	// The object comes from dtc_state_json() and is complete or empty, it is not escaped again
	put_raw(&out, (dtc_json != NULL && dtc_json[0] != '\0') ? dtc_json : "{}");
	put_char(&out, '}');

	return end_json(&out);
}

/* Sleep ------------------------------------------------------------------------------------------- */

bool dtc_api_defer_sleep(bool scan_busy, uint64_t overdue_ms)
{
	return scan_busy && overdue_ms <= DTC_API_SLEEP_DEFER_MS;
}
