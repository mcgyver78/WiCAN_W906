/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __WEB_ROUTE_H__
#define __WEB_ROUTE_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * The web interface of the display: which request is what, and which is refused before any handler runs.
 * Pure logic; the HTTP server only passes on what it received. The interface is described in
 * display/API.md.
 *
 *   GET  /                          the page
 *   GET  /api/info                  version, memory, network, adapter, release
 *   GET  /api/catalog               the values the adapter can deliver
 *   GET  /api/values                the current values (from the memory of the display, the adapter is not asked)
 *   GET  /api/layout                the views in use, as the layout text
 *   PUT  /api/layout?mode=check     checks a layout, changes nothing
 *   PUT  /api/layout?mode=apply     shows it on the display without storing it
 *   PUT  /api/layout?mode=save      stores it
 *   POST /api/layout/reset          back to the built-in views
 *   GET  /api/dtc/last              the last fault memory list and the one before the last clear
 *   GET  /api/wifi                  stored and visible networks, never a password
 *   POST /api/wifi                  store a network                       (knob)
 *   POST /api/wifi/forget           remove a stored network
 *   POST /api/settings              brightness, direction of the knob, ...
 *   POST /api/reboot
 *   POST /api/reset                 factory reset                         (knob)
 *   POST /api/ota                   firmware upload                       (knob, after the upload)
 *   GET  /api/ticket?id=N           what became of a question to the knob
 *
 * The web interface can neither read nor clear the fault memory of the vehicle.
 */

#define WEB_HEADER_NAME     "X-Display"     // has to be sent with the value "1" with every request that is not a GET
#define WEB_HOST_NAME       "wican-display" // the display answers to this name plus ".local"
#define WEB_QUERY_MAX       63              // bytes of the query string, longer ones are refused
#define WEB_BODY_LAYOUT_MAX 16384u          // LAYOUT_TEXT_MAX
#define WEB_BODY_SMALL_MAX  512u            // bodies of the other requests except the firmware
#define WEB_ERROR_SIZE      96

typedef enum
{
	WEB_GET,
	WEB_PUT,
	WEB_POST,
	WEB_OTHER,      // any other method
} web_method_t;

typedef enum
{
	WEB_ROUTE_NONE,             // refused, see status
	WEB_ROUTE_PAGE,
	WEB_ROUTE_INFO,
	WEB_ROUTE_CATALOG,
	WEB_ROUTE_VALUES,
	WEB_ROUTE_LAYOUT,
	WEB_ROUTE_LAYOUT_CHECK,
	WEB_ROUTE_LAYOUT_APPLY,
	WEB_ROUTE_LAYOUT_SAVE,
	WEB_ROUTE_LAYOUT_RESET,
	WEB_ROUTE_DTC_LAST,
	WEB_ROUTE_WIFI,
	WEB_ROUTE_WIFI_STORE,
	WEB_ROUTE_WIFI_FORGET,
	WEB_ROUTE_SETTINGS,
	WEB_ROUTE_REBOOT,
	WEB_ROUTE_RESET,
	WEB_ROUTE_OTA,
	WEB_ROUTE_TICKET,
} web_route_t;

// What the server knows about the request and the display when the headers have arrived
typedef struct
{
	web_method_t method;
	const char *path;           // without the query string, e.g. "/api/layout". Path and query as they were
	                            // sent: %xx is not decoded, "/%61pi/info" is an unknown path
	const char *query;          // behind the '?', NULL or empty if there is none. One longer than
	                            // WEB_QUERY_MAX may arrive whole or cut off behind that many bytes,
	                            // never empty: no query that is accepted is that long
	const char *header;         // value of the header WEB_HEADER_NAME, NULL if it was not sent
	const char *host;           // value of the header Host, NULL if it was not sent
	bool has_length;            // a Content-Length header was sent
	uint32_t length;            // its value
	bool release_open;          // access_is_open()
	bool busy;                  // a fault memory request of the display is under way, the clear dialog is
	                            // open, or a firmware upload runs
	uint32_t slot_size;         // size of the app slot a firmware would be written to
} web_request_t;

typedef struct
{
	web_route_t route;          // WEB_ROUTE_NONE if refused: changes and knob are then false, ticket is 0
	int status;                 // 0 if the request goes on to its handler, else the HTTP status to answer
	const char *error;          // word for the refusal (see below), NULL if not refused
	bool changes;               // the route changes something: the handler has to call access_write() or
	                            // access_ask() and must not go on if that fails
	bool knob;                  // the route needs the knob: access_ask() instead of access_write(). The firmware
	                            // is asked for when it has been uploaded; the upload itself needs the release
	uint32_t ticket;            // WEB_ROUTE_TICKET: the number asked for
} web_decision_t;

// Decides about a request. The checks are made in this order, the first that fails is the answer:
//   404 "not_found"     the path is none of the list above. Paths are compared exactly: no trailing slash,
//                       no upper case, nothing behind them.
//   405 "method"        the path is known, but not with this method
//   403 "host"          the Host header is missing or neither an IPv4 address nor the name of the display,
//                       see web_host_allowed(). Protects every route, also the reading ones, against a web
//                       page whose name was bent to the address of the display (DNS rebinding). Path and
//                       method are judged before it: such a page can learn which paths exist, nothing more.
//   403 "header"        not a GET and the header is missing or not exactly "1". A web page of another origin
//                       cannot send this header: the browser asks first (preflight), and that is never answered.
//                       The preflight is an OPTIONS request, WEB_OTHER here: 405. The argument holds only
//                       while no answer of the display carries an Access-Control-Allow header.
//   403 "locked"        the route changes something and the release is closed
//   400 "query"         the query string is longer than WEB_QUERY_MAX; PUT /api/layout without exactly one
//                       mode of check, apply, save; GET /api/ticket without exactly one id of 1 to 10
//                       digits that is 1 to 2^32-1 (zeros in front are digits like the others: id=01 is
//                       ticket 1); any other route with a query string that is not empty.
//                       "Exactly one" means: the query string is that one pair and nothing else.
//   411 "length"        not a GET and no Content-Length
//   413 "too_large"     Content-Length above the limit of the route: WEB_BODY_LAYOUT_MAX for PUT /api/layout,
//                       slot_size for the firmware, WEB_BODY_SMALL_MAX for every other route that is not a
//                       GET, also for POST /api/layout/reset. An empty firmware (length 0) is refused as
//                       well. A GET has no limit: its Content-Length is not looked at.
//   409 "busy"          firmware upload, reboot or factory reset while `busy`
// PUT /api/layout?mode=check changes nothing: it needs the header but not the release. Every other route
// that is not a GET changes something, also the reboot, and so does PUT /api/layout with a query that is
// not exactly "mode=check": while the release is closed it is answered 403 "locked", not 400 "query".
// A NULL path counts as an unknown path.
web_decision_t web_route(const web_request_t *request);

// true if the Host header can name the display itself: an IPv4 address in dotted notation (four numbers 0 to
// 255 without a sign, without blanks and without leading zeros beyond a single "0"), or WEB_HOST_NAME
// followed by ".local" (letters compared without case), both optionally followed by ":" and a port of 1 to
// 5 digits. Everything else is refused, also an empty text and NULL.
// Every address passes, not only the one of the display, which this module does not know: what has to be
// refused is a name, and a browser sends an address as Host only to that address.
bool web_host_allowed(const char *host);

// The body of a refusal: {"error":"locked","hint":"Am Display: Menü > Web-Zugriff freigeben"} for "locked",
// {"error":"<word>"} for every other word. The word is written as it is, without escaping: it has to be a
// word of the firmware, never text out of a request. Returns the length, -1 if it does not fit or `error`
// is NULL (out is then an empty string; with size 0 nothing is written, out may be NULL then).
// WEB_ERROR_SIZE bytes are enough for every word of this module.
int web_error_body(const char *error, char *out, size_t size);

#endif
