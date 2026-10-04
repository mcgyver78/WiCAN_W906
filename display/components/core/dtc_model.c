/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "dtc_model.h"

// The status texts with a fixed meaning, in the order of dtc_ecu_status_t, closed by NULL
static const char *const STATUS[] = {
	[DTC_ECU_OK] = "ok",
	[DTC_ECU_NO_RESPONSE] = "no_response",
	[DTC_ECU_PENDING_TIMEOUT] = "pending_timeout",
	[DTC_ECU_INCOMPLETE] = "incomplete",
	NULL,
};

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

// A number without fraction and exponent from 0 to 2^32-1. An optional one is 0 if the member is absent.
static bool read_count(const char *json, const json_token_t *tokens, int object, const char *key, bool required, uint32_t *value)
{
	int index = json_member(json, tokens, object, key);
	int64_t number;

	*value = 0;
	if(index < 0) return !required;
	if(!json_integer(json, &tokens[index], &number) || number < 0 || number > UINT32_MAX) return false;
	*value = (uint32_t)number;
	return true;
}

// An optional true or false: 1, 0, or -1 if the member is absent
static bool read_flag(const char *json, const json_token_t *tokens, int object, const char *key, int8_t *flag)
{
	int index = json_member(json, tokens, object, key);

	*flag = -1;
	if(index < 0) return true;
	if(tokens[index].type != JSON_TRUE && tokens[index].type != JSON_FALSE) return false;
	*flag = tokens[index].type == JSON_TRUE ? 1 : 0;
	return true;
}

static int hex_digit(char c)
{
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

static void read_status(const char *json, const json_token_t *token, dtc_ecu_t *ecu)
{
	// Room for "nrc_" and two digits. json_text() refuses what does not fit: a longer text is something else,
	// and a shorter one ends where a digit has to be.
	char text[7];
	int high, low;

	ecu->nrc = 0;
	for(int i = 0; STATUS[i] != NULL; i++)
	{
		if(json_text_is(json, token, STATUS[i]))
		{
			ecu->status = (dtc_ecu_status_t)i;
			return;
		}
	}

	ecu->status = DTC_ECU_OTHER;
	if(!json_text(json, token, text, sizeof(text)) || strncmp(text, "nrc_", 4) != 0) return;
	high = hex_digit(text[4]);
	if(high < 0) return;
	low = hex_digit(text[5]);
	if(low < 0) return;
	ecu->status = DTC_ECU_NRC;
	ecu->nrc = (uint8_t)(high * 16 + low);
}

// A control unit without its codes. *dtcs is the list of them.
static bool read_ecu(const char *json, const json_token_t *tokens, int object, dtc_ecu_t *ecu, int *dtcs)
{
	int protocol = json_member(json, tokens, object, "protocol");
	int status = json_member(json, tokens, object, "status");

	*dtcs = json_member(json, tokens, object, "dtcs");
	if(protocol < 0 || status < 0 || *dtcs < 0) return false;
	if(tokens[status].type != JSON_STRING || tokens[*dtcs].type != JSON_ARRAY) return false;

	ecu->uds = json_text_is(json, &tokens[protocol], "UDS");
	if(!ecu->uds && !json_text_is(json, &tokens[protocol], "KWP")) return false;

	if(!read_text(json, tokens, object, "name", ecu->name, sizeof(ecu->name)) ||
	   !read_text(json, tokens, object, "id", ecu->id, sizeof(ecu->id)) ||
	   !read_flag(json, tokens, object, "cleared", &ecu->cleared) ||
	   !read_count(json, tokens, object, "dtcs_omitted", false, &ecu->omitted)) return false;

	dtc_short_name(ecu->name, ecu->short_name, sizeof(ecu->short_name));
	read_status(json, &tokens[status], ecu);
	return true;
}

static bool read_code(const char *json, const json_token_t *tokens, int object, dtc_code_t *code)
{
	return read_text(json, tokens, object, "code", code->code, sizeof(code->code)) &&
	       read_text(json, tokens, object, "status", code->status, sizeof(code->status)) &&
	       read_flag(json, tokens, object, "active", &code->active);
}

// One pass over the whole result. Without `result` it only checks: a second pass with it then cannot
// fail half way and leave *result half written.
static bool read_result(const char *json, const json_token_t *tokens, dtc_result_t *result)
{
	int state = json_member(json, tokens, 0, "state");
	int action = json_member(json, tokens, 0, "action");
	int ecus = json_member(json, tokens, 0, "ecus");
	dtc_ecu_t *ecu_room = result != NULL ? result->ecus : NULL;
	dtc_code_t *code_room = result != NULL ? result->codes : NULL;
	uint32_t duration_ms, dtc_count;
	int ecu_count = 0;
	int code_count = 0;
	bool cut = false;
	bool clear;
	int element;

	if(state < 0 || !json_text_is(json, &tokens[state], "done")) return false;
	if(action < 0 || ecus < 0 || tokens[ecus].type != JSON_ARRAY) return false;
	clear = json_text_is(json, &tokens[action], "clear");
	if(!clear && !json_text_is(json, &tokens[action], "read")) return false;
	if(!read_count(json, tokens, 0, "duration_ms", true, &duration_ms) ||
	   !read_count(json, tokens, 0, "dtc_count", true, &dtc_count)) return false;

	element = ecus + 1;
	for(int i = 0; i < tokens[ecus].size; i++)
	{
		bool room = ecu_count < DTC_ECUS_MAX;
		dtc_ecu_t ecu;
		int dtcs, dtc;

		if(!read_ecu(json, tokens, element, &ecu, &dtcs)) return false;
		ecu.first_code = (uint16_t)code_count;
		ecu.code_count = 0;

		// What does not fit is still checked
		dtc = dtcs + 1;
		for(int k = 0; k < tokens[dtcs].size; k++)
		{
			dtc_code_t code;

			if(!read_code(json, tokens, dtc, &code)) return false;
			if(room && code_count < DTC_CODES_MAX)
			{
				if(code_room != NULL) code_room[code_count] = code;
				code_count++;
				ecu.code_count++;
			}
			else
			{
				cut = true;
			}
			dtc += tokens[dtc].skip;
		}

		if(room)
		{
			if(ecu_room != NULL) ecu_room[ecu_count] = ecu;
			ecu_count++;
		}
		else
		{
			cut = true;
		}
		element += tokens[element].skip;
	}

	if(result != NULL)
	{
		result->clear = clear;
		result->duration_ms = duration_ms;
		result->dtc_count = dtc_count;
		result->ecu_count = ecu_count;
		result->code_count = code_count;
		result->cut = cut;
	}
	return true;
}

bool dtc_result_parse(const char *json, size_t length, dtc_result_t *result, json_token_t *work, int work_count)
{
	if(json_parse(json, length, work, work_count) < 0) return false;

	return read_result(json, work, NULL) && read_result(json, work, result);
}

void dtc_short_name(const char *name, char *out, size_t size)
{
	const char *text = name;
	size_t length = strlen(name);
	size_t word = strcspn(name, " ");
	size_t rest = word + strspn(name + word, " ");
	const char *open = NULL;
	const char *inside = NULL;
	size_t inside_length = 0;
	bool digit = false;

	if(size == 0) return;

	// An opening parenthesis belongs to the next closing one. The last such pair counts.
	for(const char *c = name; *c != '\0'; c++)
	{
		if(*c == '(')
		{
			open = c + 1;
		}
		else if(*c == ')' && open != NULL)
		{
			inside = open;
			inside_length = (size_t)(c - open);
			open = NULL;
		}
	}
	for(size_t i = 0; i < word; i++)
	{
		if(name[i] >= '0' && name[i] <= '9') digit = true;
	}

	// A rule that would leave nothing is skipped
	if(inside_length > 0)
	{
		text = inside;
		length = inside_length;
	}
	else if(digit && name[rest] != '\0')
	{
		text = name + rest;
		length -= rest;
	}

	if(length >= size)
	{
		// The first byte left out must not be the middle of a character
		length = size - 1;
		while(length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80) length--;
	}
	memcpy(out, text, length);
	out[length] = '\0';
}

// The sum, or 2^32-1 if it is more
static uint32_t add(uint32_t a, uint32_t b)
{
	return b > UINT32_MAX - a ? UINT32_MAX : a + b;
}

void dtc_summarize(const dtc_result_t *result, dtc_summary_t *summary)
{
	summary->ecus_with_codes = 0;
	summary->ecus_not_ok = 0;
	summary->ecus_clean = 0;
	summary->codes = 0;

	for(int i = 0; i < result->ecu_count; i++)
	{
		const dtc_ecu_t *ecu = &result->ecus[i];
		bool codes = ecu->code_count > 0 || ecu->omitted > 0;

		if(codes) summary->ecus_with_codes++;
		if(ecu->status != DTC_ECU_OK) summary->ecus_not_ok++;
		if(ecu->status == DTC_ECU_OK && !codes) summary->ecus_clean++;
		summary->codes = add(summary->codes, add(ecu->code_count, ecu->omitted));
	}
}

void dtc_clear_summarize(const dtc_result_t *before, const dtc_result_t *after, dtc_clear_summary_t *summary)
{
	dtc_summary_t was, is;

	dtc_summarize(before, &was);
	dtc_summarize(after, &is);

	summary->before = was.codes;
	summary->remaining = is.codes;
	summary->cleared = was.codes > is.codes ? was.codes - is.codes : 0;
	summary->unconfirmed = 0;
	for(int i = 0; i < after->ecu_count; i++)
	{
		if(after->ecus[i].cleared == 0) summary->unconfirmed++;
	}
}
