/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>
#include "web_route.h"

typedef struct
{
	web_method_t method;
	const char *path;
	web_route_t route;
	bool knob;      // the change needs the knob
	bool rests;     // refused while the display is busy
} entry_t;

// The list of the header. Every entry that is not a GET changes something.
static const entry_t ROUTES[] = {
	{WEB_GET,  "/",                 WEB_ROUTE_PAGE,         false, false},
	{WEB_GET,  "/api/info",         WEB_ROUTE_INFO,         false, false},
	{WEB_GET,  "/api/catalog",      WEB_ROUTE_CATALOG,      false, false},
	{WEB_GET,  "/api/values",       WEB_ROUTE_VALUES,       false, false},
	{WEB_GET,  "/api/layout",       WEB_ROUTE_LAYOUT,       false, false},
	// Stands for check, apply and save: the mode in the query names the route
	{WEB_PUT,  "/api/layout",       WEB_ROUTE_LAYOUT_SAVE,  false, false},
	{WEB_POST, "/api/layout/reset", WEB_ROUTE_LAYOUT_RESET, false, false},
	{WEB_GET,  "/api/dtc/last",     WEB_ROUTE_DTC_LAST,     false, false},
	{WEB_GET,  "/api/wifi",         WEB_ROUTE_WIFI,         false, false},
	{WEB_POST, "/api/wifi",         WEB_ROUTE_WIFI_STORE,   true,  false},
	{WEB_POST, "/api/wifi/forget",  WEB_ROUTE_WIFI_FORGET,  false, false},
	{WEB_POST, "/api/settings",     WEB_ROUTE_SETTINGS,     false, false},
	{WEB_POST, "/api/reboot",       WEB_ROUTE_REBOOT,       false, true},
	{WEB_POST, "/api/reset",        WEB_ROUTE_RESET,        true,  true},
	{WEB_POST, "/api/ota",          WEB_ROUTE_OTA,          true,  true},
	{WEB_GET,  "/api/ticket",       WEB_ROUTE_TICKET,       false, false},
};

static web_decision_t refused(int status, const char *error)
{
	web_decision_t decision = {WEB_ROUTE_NONE, status, error, false, false, 0};

	return decision;
}

// The route PUT /api/layout means, WEB_ROUTE_NONE if the query is not exactly one mode
static web_route_t layout_route(const char *query)
{
	if(strcmp(query, "mode=check") == 0) return WEB_ROUTE_LAYOUT_CHECK;
	if(strcmp(query, "mode=apply") == 0) return WEB_ROUTE_LAYOUT_APPLY;
	if(strcmp(query, "mode=save") == 0) return WEB_ROUTE_LAYOUT_SAVE;
	return WEB_ROUTE_NONE;
}

// The number GET /api/ticket asks for, 0 if the query is not exactly one id of 1 to 10 digits or the
// number is no ticket (0, or more than 32 bit hold)
static uint32_t ticket_number(const char *query)
{
	uint64_t number = 0;
	int digits = 0;

	if(strncmp(query, "id=", 3) != 0) return 0;

	for(query += 3; *query >= '0' && *query <= '9'; query++)
	{
		if(++digits > 10) return 0;
		number = number * 10 + (uint64_t)(*query - '0');
	}
	if(*query != '\0' || number > UINT32_MAX) return 0;
	return (uint32_t)number;
}

// One number of an address. Returns what follows it, NULL if there is none: no digit, a zero in front of
// another digit, or more than 255.
static const char *behind_number(const char *text)
{
	const char *start = text;
	int number = 0;

	while(*text >= '0' && *text <= '9')
	{
		if(text > start && number == 0) return NULL;
		number = number * 10 + (*text++ - '0');
		if(number > 255) return NULL;
	}
	return text > start ? text : NULL;
}

// Returns what follows the IPv4 address `text` begins with, NULL if it begins with none
static const char *behind_address(const char *text)
{
	for(int i = 0; i < 4; i++)
	{
		if(i > 0 && *text++ != '.') return NULL;

		text = behind_number(text);
		if(text == NULL) return NULL;
	}
	return text;
}

// Returns what follows the name of the display `text` begins with, NULL if it does not begin with it
static const char *behind_name(const char *text)
{
	// Written in lower case
	static const char name[] = WEB_HOST_NAME ".local";

	for(size_t i = 0; i < sizeof(name) - 1; i++)
	{
		char c = text[i];

		// Only the letters have a second form. Setting the bit of the lower case in every byte would turn
		// control characters into the hyphen and the dot of the name.
		if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		if(c != name[i]) return NULL;
	}
	return text + sizeof(name) - 1;
}

// Nothing, or ":" and a port
static bool is_port_or_end(const char *text)
{
	int digits = 0;

	if(*text == '\0') return true;
	if(*text != ':') return false;

	for(text++; *text >= '0' && *text <= '9'; text++)
	{
		if(++digits > 5) return false;
	}
	return *text == '\0' && digits > 0;
}

web_decision_t web_route(const web_request_t *request)
{
	const char *query = request->query != NULL ? request->query : "";
	web_decision_t decision = {WEB_ROUTE_NONE, 0, NULL, false, false, 0};
	uint32_t limit = WEB_BODY_SMALL_MAX;
	bool query_fits = query[0] == '\0';
	const entry_t *entry = NULL;
	bool path_known = false;

	for(size_t i = 0; i < sizeof(ROUTES) / sizeof(ROUTES[0]); i++)
	{
		if(request->path == NULL || strcmp(request->path, ROUTES[i].path) != 0) continue;

		path_known = true;
		if(ROUTES[i].method == request->method) entry = &ROUTES[i];
	}
	if(!path_known) return refused(404, "not_found");
	if(entry == NULL) return refused(405, "method");
	if(!web_host_allowed(request->host)) return refused(403, "host");
	if(entry->method != WEB_GET && (request->header == NULL || strcmp(request->header, "1") != 0)) return refused(403, "header");

	decision.route = entry->route;
	decision.changes = entry->method != WEB_GET;
	decision.knob = entry->knob;
	if(entry->route == WEB_ROUTE_LAYOUT_SAVE)
	{
		// Only the check changes nothing. A query that names no mode has to pass the release like a change:
		// "locked" goes before "query".
		decision.route = layout_route(query);
		decision.changes = decision.route != WEB_ROUTE_LAYOUT_CHECK;
		query_fits = decision.route != WEB_ROUTE_NONE;
		limit = WEB_BODY_LAYOUT_MAX;
	}
	if(entry->route == WEB_ROUTE_TICKET)
	{
		decision.ticket = ticket_number(query);
		query_fits = decision.ticket != 0;
	}
	if(entry->route == WEB_ROUTE_OTA) limit = request->slot_size;

	// No query that fits is longer than 13 bytes, so WEB_QUERY_MAX needs no check of its own
	if(decision.changes && !request->release_open) return refused(403, "locked");
	if(!query_fits) return refused(400, "query");
	if(entry->method != WEB_GET)
	{
		if(!request->has_length) return refused(411, "length");
		if(request->length > limit || (entry->route == WEB_ROUTE_OTA && request->length == 0)) return refused(413, "too_large");
	}
	if(entry->rests && request->busy) return refused(409, "busy");
	return decision;
}

bool web_host_allowed(const char *host)
{
	const char *rest;

	if(host == NULL) return false;

	rest = behind_name(host);
	if(rest == NULL) rest = behind_address(host);
	return rest != NULL && is_port_or_end(rest);
}

int web_error_body(const char *error, char *out, size_t size)
{
	int length = -1;

	if(error != NULL)
	{
		length = snprintf(out, size, "{\"error\":\"%s\"%s}", error,
		                  strcmp(error, "locked") == 0 ? ",\"hint\":\"Am Display: Menü > Web-Zugriff freigeben\"" : "");
	}
	if(length < 0 || (size_t)length >= size)
	{
		if(size > 0) out[0] = '\0';
		return -1;
	}
	return length;
}
