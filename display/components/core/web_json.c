/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "web_json.h"

#define WPA2_PASSWORD_MIN   8

typedef struct
{
	char *out;
	size_t size;
	size_t length;      // of the whole text, also when it does not fit
} writer_t;

static void put_char(writer_t *writer, char c)
{
	// What does not fit is only counted
	if(writer->length < writer->size) writer->out[writer->length] = c;
	writer->length++;
}

static void put(writer_t *writer, const char *text)
{
	for(; *text != '\0'; text++) put_char(writer, *text);
}

// A text between quotes. It ends at its zero, at the latest after `limit` bytes.
static void put_limited(writer_t *writer, const char *text, size_t limit)
{
	static const char hex[] = "0123456789abcdef";

	put_char(writer, '"');
	for(size_t i = 0; i < limit && text[i] != '\0'; i++)
	{
		unsigned char c = (unsigned char)text[i];

		if(c < 0x20 || c == 0x7F)
		{
			put(writer, "\\u00");
			put_char(writer, hex[c >> 4]);
			put_char(writer, hex[c & 0x0F]);
			continue;
		}
		if(c == '"' || c == '\\') put_char(writer, '\\');
		put_char(writer, text[i]);
	}
	put_char(writer, '"');
}

static void put_string(writer_t *writer, const char *text)
{
	put_limited(writer, text == NULL ? "" : text, SIZE_MAX);
}

static void put_bool(writer_t *writer, bool value)
{
	put(writer, value ? "true" : "false");
}

static void put_unsigned(writer_t *writer, uint32_t number)
{
	char digits[10];
	int count = 0;

	do
	{
		digits[count++] = (char)('0' + number % 10);
		number /= 10;
	}
	while(number != 0);
	while(count > 0) put_char(writer, digits[--count]);
}

static void put_int(writer_t *writer, int number)
{
	// Not -number: the smallest int has no positive counterpart
	uint32_t magnitude = number < 0 ? 0u - (uint32_t)number : (uint32_t)number;

	if(number < 0) put_char(writer, '-');
	put_unsigned(writer, magnitude);
}

// A text that is JSON already, null if there is none
static void put_embedded(writer_t *writer, const char *json)
{
	put(writer, json == NULL || json[0] == '\0' ? "null" : json);
}

// The length, or -1 and an empty text if it did not fit
static int finish(const writer_t *writer)
{
	if(writer->size == 0) return -1;
	if(writer->length >= writer->size)
	{
		writer->out[0] = '\0';
		return -1;
	}
	writer->out[writer->length] = '\0';
	return (int)writer->length;
}

int web_info_json(const web_info_t *info, char *out, size_t size)
{
	writer_t writer = {out, size, 0};

	put(&writer, "{\"project\":\"wican-display\",\"version\":");
	put_string(&writer, info->version);
	put(&writer, ",\"git\":");
	put_string(&writer, info->git);
	put(&writer, ",\"slot\":");
	put_string(&writer, info->slot);
	put(&writer, ",\"reset\":");
	put_string(&writer, info->reset);
	put(&writer, ",\"up\":");
	put_unsigned(&writer, info->up_s);
	put(&writer, ",\"safe_mode\":");
	put_bool(&writer, info->safe_mode);
	put(&writer, ",\"rolled_back\":");
	put_bool(&writer, info->rolled_back);
	put(&writer, ",\"update_pending\":");
	put_bool(&writer, info->update_pending);
	put(&writer, ",\"heap\":");
	put_unsigned(&writer, info->heap);
	put(&writer, ",\"heap_min\":");
	put_unsigned(&writer, info->heap_min);
	put(&writer, ",\"psram\":");
	put_unsigned(&writer, info->psram);
	put(&writer, ",\"psram_min\":");
	put_unsigned(&writer, info->psram_min);
	put(&writer, ",\"temp_c\":");
	put_int(&writer, info->temp_c);
	put(&writer, ",\"heat\":");
	put_string(&writer, info->heat);

	put(&writer, ",\"release\":{\"open\":");
	put_bool(&writer, info->release_open);
	put(&writer, ",\"left_s\":");
	put_unsigned(&writer, info->release_left_s);

	put(&writer, "},\"wifi\":{\"ssid\":");
	put_string(&writer, info->ssid);
	put(&writer, ",\"ip\":");
	put_string(&writer, info->ip);
	put(&writer, ",\"rssi\":");
	put_int(&writer, info->rssi);
	put(&writer, ",\"ap\":");
	put_bool(&writer, info->ap_on);
	put(&writer, ",\"ap_ssid\":");
	put_string(&writer, info->ap_ssid);

	put(&writer, "},\"wican\":{\"host\":");
	put_string(&writer, info->wican_host);
	put(&writer, ",\"id\":");
	put_string(&writer, info->wican_id);
	put(&writer, ",\"fw\":");
	put_string(&writer, info->wican_fw);
	put(&writer, ",\"view\":");
	put_string(&writer, info->view);

	put(&writer, "},\"layout\":{\"name\":");
	put_string(&writer, info->layout_name);
	put(&writer, ",\"source\":");
	put_string(&writer, info->layout_source);

	put(&writer, "},\"http\":{\"ok\":");
	put_unsigned(&writer, info->http_ok);
	put(&writer, ",\"failed\":");
	put_unsigned(&writer, info->http_failed);
	put(&writer, ",\"reconnects\":");
	put_unsigned(&writer, info->reconnects);

	put(&writer, "},\"settings\":");
	put_embedded(&writer, info->settings);
	put_char(&writer, '}');
	return finish(&writer);
}

int web_values_json(const values_t *values, const char *view, uint64_t now_ms, char *out, size_t size)
{
	writer_t writer = {out, size, 0};
	const value_t *value = values->items;
	bool first = true;

	put(&writer, "{\"view\":");
	put_string(&writer, view);
	put(&writer, ",\"values\":{");
	for(int i = 0; i < values->count; i++, value++)
	{
		value_age_t age = values_age(value, now_ms);
		// The longest number is "-1.23456789e-308"
		char number[24];
		const char *shown;

		if(age == VALUE_AGE_GONE) continue;

		if(value->kind == VALUE_ON) shown = "\"on\"";
		else if(value->kind == VALUE_OFF) shown = "\"off\"";
		else if(value->kind == VALUE_NUMBER && isfinite(value->number))
		{
			// printf would write minus zero as "-0"
			snprintf(number, sizeof(number), "%.9g", value->number == 0 ? 0.0 : value->number);
			shown = number;
		}
		else continue;

		if(!first) put_char(&writer, ',');
		first = false;
		put_string(&writer, value->name);
		put(&writer, ":{\"v\":");
		put(&writer, shown);
		put(&writer, ",\"age\":");
		put(&writer, age == VALUE_AGE_FRESH ? "\"fresh\"" : "\"old\"");
		put_char(&writer, '}');
	}
	put(&writer, "}}");
	return finish(&writer);
}

int web_wifi_json(const net_profile_t *profiles, int profile_count, const char *current,
                  const web_seen_t *seen, int seen_count, char *out, size_t size)
{
	writer_t writer = {out, size, 0};
	bool first = true;

	// A count that was stored may be damaged like the list itself, see net_select.c. A negative one lists
	// nothing by itself.
	if(profile_count > NET_PROFILES_MAX) profile_count = 0;

	put(&writer, "{\"current\":");
	put_string(&writer, current);
	put(&writer, ",\"profiles\":[");
	for(int i = 0; i < profile_count; i++)
	{
		const net_profile_t *profile = &profiles[i];

		if(i > 0) put_char(&writer, ',');
		// The password lies right behind the SSID: a field that lost its zero must not run into it
		put(&writer, "{\"ssid\":");
		put_limited(&writer, profile->ssid, sizeof(profile->ssid) - 1);
		put(&writer, ",\"host\":");
		put_limited(&writer, profile->host, sizeof(profile->host) - 1);
		put(&writer, ",\"password\":");
		put_bool(&writer, profile->password[0] != '\0');
		put(&writer, ",\"factory\":");
		put_bool(&writer, net_is_factory_password(profile->password));
		put(&writer, ",\"wican_ap\":");
		put_bool(&writer, net_is_wican_ap(profile->ssid));
		put_char(&writer, '}');
	}
	put(&writer, "],\"seen\":[");
	for(int i = 0; i < seen_count; i++)
	{
		// A scan lists hidden networks with an empty SSID
		if(seen[i].ssid[0] == '\0') continue;

		if(!first) put_char(&writer, ',');
		first = false;
		put(&writer, "{\"ssid\":");
		put_limited(&writer, seen[i].ssid, sizeof(seen[i].ssid) - 1);
		put(&writer, ",\"rssi\":");
		put_int(&writer, seen[i].rssi);
		put(&writer, ",\"secure\":");
		put_bool(&writer, seen[i].secure);
		put_char(&writer, '}');
	}
	put(&writer, "]}");
	return finish(&writer);
}

// Text of a member into its field, which is written whole. false if it is no text, does not fit, has an
// escape json_text() refuses or contains a control character.
static bool take_text(const char *json, const json_token_t *token, char *out, size_t size)
{
	// Nothing of a longer text of a member with the same name before this one may stay behind it
	memset(out, 0, size);
	if(!json_text(json, token, out, size)) return false;

	for(; *out != '\0'; out++)
	{
		if((unsigned char)*out < 0x20) return false;
	}
	return true;
}

// What POST /api/wifi and POST /api/wifi/forget have in common. ssid_only: the format knows nothing but
// the SSID. *request is written in any case, also when the text is refused.
static bool read_request(const char *json, size_t length, bool ssid_only, web_wifi_request_t *request,
                         json_token_t *work, int work_count)
{
	int count = json_parse(json, length, work, work_count);
	bool has_ssid = false;
	int key = 1;

	// Members that are missing stay empty
	memset(request, 0, sizeof(*request));
	if(count < 0 || work[0].type != JSON_OBJECT) return false;

	for(int i = 0; i < work[0].size; i++)
	{
		const json_token_t *name = &work[key];
		const json_token_t *value = &work[key + 1];

		key += 1 + value->skip;

		if(json_text_is(json, name, "ssid"))
		{
			if(!take_text(json, value, request->ssid, sizeof(request->ssid)) || request->ssid[0] == '\0') return false;
			has_ssid = true;
		}
		else if(ssid_only) continue;
		else if(json_text_is(json, name, "password"))
		{
			size_t password_length;

			if(!take_text(json, value, request->password, sizeof(request->password))) return false;
			password_length = strlen(request->password);
			if(password_length > 0 && password_length < WPA2_PASSWORD_MIN) return false;
			request->has_password = true;
		}
		else if(json_text_is(json, name, "host"))
		{
			if(!take_text(json, value, request->host, sizeof(request->host))) return false;
		}
	}
	return has_ssid;
}

bool web_wifi_parse(const char *json, size_t length, web_wifi_request_t *request,
                    json_token_t *work, int work_count)
{
	// Read in place and put back if the text is refused. With a copy on the stack, what a missing member holds
	// would depend on the stack if that copy were ever not emptied, and no test could show it.
	web_wifi_request_t before = *request;

	if(read_request(json, length, false, request, work, work_count)) return true;

	*request = before;
	return false;
}

bool web_forget_parse(const char *json, size_t length, char *ssid, json_token_t *work, int work_count)
{
	web_wifi_request_t read;

	if(!read_request(json, length, true, &read, work, work_count)) return false;

	memcpy(ssid, read.ssid, sizeof(read.ssid));
	return true;
}

// Not loaded yet: the catalogue holds nothing but the entry catalog_init() makes
static bool catalog_loaded(const catalog_t *catalog)
{
	for(int i = 0; i < catalog->count; i++)
	{
		if(strcmp(catalog->entries[i].name, CATALOG_BATTERY) != 0) return true;
	}
	return false;
}

// true if an item before this one, in the order of the pages, has the same key
static bool used_before(const layout_t *layout, int page, int item)
{
	const char *key = layout->pages[page].items[item].key;

	for(int p = 0; p <= page; p++)
	{
		int count = p < page ? layout->pages[p].item_count : item;

		for(int i = 0; i < count; i++)
		{
			if(strcmp(layout->pages[p].items[i].key, key) == 0) return true;
		}
	}
	return false;
}

int web_layout_report_json(bool ok, const layout_report_t *report, const layout_t *layout,
                           const catalog_t *catalog, char *out, size_t size)
{
	writer_t writer = {out, size, 0};
	uint32_t items = 0;
	bool first = true;

	if(!ok)
	{
		put(&writer, "{\"ok\":false,\"path\":");
		put_string(&writer, report->path);
		put(&writer, ",\"problem\":");
		put_string(&writer, report->problem);
		put_char(&writer, '}');
		return finish(&writer);
	}

	for(int p = 0; p < layout->page_count; p++) items += layout->pages[p].item_count;

	put(&writer, "{\"ok\":true,\"name\":");
	put_string(&writer, layout->name);
	put(&writer, ",\"pages\":");
	put_unsigned(&writer, layout->page_count);
	put(&writer, ",\"items\":");
	put_unsigned(&writer, items);
	put(&writer, ",\"warnings\":");
	put_int(&writer, report->warnings);
	put(&writer, ",\"warning_path\":");
	put_string(&writer, report->warning_path);
	put(&writer, ",\"warning\":");
	put_string(&writer, report->warning);
	put(&writer, ",\"unknown\":[");
	// Before the catalogue is there every key would be unknown
	if(catalog_loaded(catalog))
	{
		for(int p = 0; p < layout->page_count; p++)
		{
			for(int i = 0; i < layout->pages[p].item_count; i++)
			{
				const char *key = layout->pages[p].items[i].key;

				if(catalog_find(catalog, key) >= 0 || used_before(layout, p, i)) continue;

				if(!first) put_char(&writer, ',');
				first = false;
				put_string(&writer, key);
			}
		}
	}
	put(&writer, "]}");
	return finish(&writer);
}

int web_ticket_json(uint32_t ticket, access_ticket_t state, uint32_t left_s, char *out, size_t size)
{
	writer_t writer = {out, size, 0};
	const char *word = "unknown";

	switch(state)
	{
		case ACCESS_TICKET_WAITING:   word = "waiting"; break;
		case ACCESS_TICKET_CONFIRMED: word = "confirmed"; break;
		case ACCESS_TICKET_REFUSED:   word = "refused"; break;
		case ACCESS_TICKET_EXPIRED:   word = "expired"; break;
		default:                      break;
	}

	put(&writer, "{\"ticket\":");
	put_unsigned(&writer, ticket);
	put(&writer, ",\"state\":\"");
	put(&writer, word);
	put(&writer, "\",\"left_s\":");
	put_unsigned(&writer, state == ACCESS_TICKET_WAITING ? left_s : 0);
	put_char(&writer, '}');
	return finish(&writer);
}

int web_asked_json(uint32_t ticket, char *out, size_t size)
{
	writer_t writer = {out, size, 0};

	put(&writer, "{\"ticket\":");
	put_unsigned(&writer, ticket);
	put(&writer, ",\"hint\":\"Am Display bestätigen: Knopf drücken\"}");
	return finish(&writer);
}

int web_dtc_last_json(const char *read, uint32_t read_age_s, const char *before_clear, char *out, size_t size)
{
	writer_t writer = {out, size, 0};
	bool has_read = read != NULL && read[0] != '\0';

	put(&writer, "{\"read\":");
	put_embedded(&writer, read);
	put(&writer, ",\"read_age_s\":");
	put_unsigned(&writer, has_read ? read_age_s : 0);
	put(&writer, ",\"before_clear\":");
	put_embedded(&writer, before_clear);
	put_char(&writer, '}');
	return finish(&writer);
}
