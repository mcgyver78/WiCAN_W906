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
#include <limits.h>
#include <inttypes.h>
#include <sys/socket.h>
#include "platform.h"
#include "app_web.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"

/*
 * The HTTP server of the display: it reads a request, asks web_route() what it is and calls the function of
 * app_web.h for it. It decides nothing, and it calls nothing of the app but app_web.h and platform_events(),
 * and reads app->uploading. Written without the board and without a build: every call was read in the
 * sources of ESP-IDF v5.5.2. What only the board can decide is marked CHECK.
 *
 * Every request goes through web_route(), whatever its method and path: one handler is registered for the
 * path "*" (httpd_uri_match_wildcard) and the method HTTP_ANY, and the server looks for no other match, so
 * it never comes to its own 404 or 405 (httpd_uri.c) - once the handler is registered: web_start() names
 * the moment before that. What does not become a request at all - a method the parser does not know, a
 * line or headers too long, a client that falls silent - would be answered by the server with a text of its
 * own: on_error() is registered for all of that and closes the connection without an answer. No header is
 * ever set here but status, type and length, and with the page the two that forbid to show it in a frame
 * (handle()).
 *
 * One request at a time: the server has a single task, which runs the handler of one request to its end
 * before it looks at the next connection (httpd_main.c), and nothing in this file runs in another task
 * after web_start(). The buffers below are used by that task alone. This is also what makes a firmware
 * upload the only one: while it runs, no other request is even read.
 *
 * And it is why every wait for a client has an end of its own here. The server gives each single read its
 * receive timeout (5 s) and knows no time for a body: a client that sends a byte every few seconds would
 * hold the one task, and with it the web interface, for as long as it likes. A body has WEB_BODY_MS, a
 * firmware as long as the app lets the upload run (APP_UPLOAD_IDLE_MS without a block), and what is dropped
 * behind an answer WEB_DROP_MS.
 */

#define TAG "web"

// CHECK: uxTaskGetStackHighWaterMark() of the task "httpd" after a firmware upload: esp_ota_end() checks
// the whole image on this stack. The functions of the core need less than 1 KB of it (compiled on a PC).
#define WEB_STACK_BYTES     8192
// The server takes three more for itself, and lwIP has 10 (CONFIG_LWIP_MAX_SOCKETS); one is the
// connection to the adapter
#define WEB_SOCKETS         4
// All headers of one request. The default of 1024 is close to what a browser sends once a cookie of
// another device at the same address is added; a request above the limit gets no answer at all.
#define WEB_HEADERS_BYTES   2048
#define WEB_BLOCK_SIZE      4096    // of a firmware: one sector of the flash
// A body other than a firmware, from its first byte to its last: 16 KB of layout at most. The page gives
// up on its request after 10 s (index.html, which has the CHECK for that number).
#define WEB_BODY_MS         15000u
// While a block of a firmware is received the app is asked this often whether the upload still runs
#define WEB_LOOK_MS         1000u
// What a refused request still sends is read and dropped for this long: a firmware of the size of the slot
// that was refused has to pass in it, or its sender sees a broken connection instead of the refusal
#define WEB_DROP_MS         60000u
#define WEB_TYPE_JSON       "application/json"
#define WEB_TYPE_PAGE       "text/html; charset=utf-8"

// What is too large for the internal RAM, allocated once in the external RAM
typedef struct
{
	char uri[CONFIG_HTTPD_MAX_URI_LEN + 1];     // the URI of the request, cut into path and query
	char body[WEB_BODY_LAYOUT_MAX];             // the largest body web_route() lets through, the firmware apart
	char out[APP_WEB_OUT_SIZE];
} web_room_t;

_Static_assert(WEB_BODY_SMALL_MAX <= WEB_BODY_LAYOUT_MAX, "the room for a body is that of the layout");

// The page: main/web/index.html, embedded as it is (EMBED_FILES in CMakeLists.txt, no zero behind it). The
// build names the symbols after the file name alone.
extern const char page_start[] asm("_binary_index_html_start");
extern const char page_end[] asm("_binary_index_html_end");

static web_room_t *room;
// A block of the firmware. In the internal RAM, unlike every other buffer: the flash driver writes from
// there in one piece, and from the external RAM 32 bytes at a time (esp_flash_write()) - 128 times as often
// with the cache switched off, which is what disturbs the picture.
static uint8_t *block;
static const esp_partition_t *slot;     // the app slot a firmware is written to: the one that does not run
static uint32_t slot_size;

// Of the request being served. A value that does not fit is cut, and each room is larger than every value
// web_route() accepts: 25 bytes for a host, "1", ten digits. So a cut host or X-Display is always a refused
// one. Not so a length: zeros in front make a number long without making it large, and cut behind them it
// is a small number. A length that fills its room is not read as a number at all (read_request()).
static char host[64];
static char header[4];
static char length_text[16];
static bool close_after;    // nothing more can be read from this connection once the answer is sent

static const char *status_line(int status)
{
	static char other[24];  // room for every int: a format that may not fit does not build

	switch(status)
	{
		case 200:   return "200 OK";
		case 202:   return "202 Accepted";
		case 400:   return "400 Bad Request";
		case 403:   return "403 Forbidden";
		case 404:   return "404 Not Found";
		case 405:   return "405 Method Not Allowed";
		case 409:   return "409 Conflict";
		case 411:   return "411 Length Required";
		case 413:   return "413 Content Too Large";
		case 422:   return "422 Unprocessable Content";
		case 500:   return "500 Internal Server Error";
		default:
			// A status the core may answer one day: the number is what counts
			snprintf(other, sizeof(other), "%d Status", status);
			return other;
	}
}

// The value of a header as it was sent, NULL if it was not sent
static const char *header_value(httpd_req_t *req, const char *name, char *out, size_t size)
{
	out[0] = '\0';
	return httpd_req_get_hdr_value_str(req, name, out, size) == ESP_ERR_NOT_FOUND ? NULL : out;
}

// The number of a Content-Length. The parser of the server lets nothing but digits through. A number
// without room in 32 bit, and whatever else is no plain number, counts as the largest there is: that is
// above every limit of web_route().
static uint32_t length_of(const char *text)
{
	uint64_t value = 0;

	if(text == NULL)
	{
		return 0;
	}
	if(*text < '0' || *text > '9')
	{
		return UINT32_MAX;
	}
	while(*text >= '0' && *text <= '9')
	{
		value = value * 10 + (uint64_t)(*text++ - '0');
		if(value > UINT32_MAX)
		{
			return UINT32_MAX;
		}
	}
	while(*text == ' ' || *text == '\t')
	{
		text++;
	}
	return *text == '\0' ? (uint32_t)value : UINT32_MAX;
}

// What web_route() wants to know, as it was received: nothing is decoded
static void read_request(httpd_req_t *req, web_request_t *request)
{
	const char *length;
	char *query;

	memset(request, 0, sizeof(*request));
	switch(req->method)
	{
		case HTTP_GET:  request->method = WEB_GET; break;
		case HTTP_PUT:  request->method = WEB_PUT; break;
		case HTTP_POST: request->method = WEB_POST; break;
		default:        request->method = WEB_OTHER; break;
	}

	strlcpy(room->uri, req->uri, sizeof(room->uri));
	query = strchr(room->uri, '?');
	if(query != NULL)
	{
		*query++ = '\0';
	}
	request->path = room->uri;
	request->query = query;

	request->host = header_value(req, "Host", host, sizeof(host));
	request->header = header_value(req, WEB_HEADER_NAME, header, sizeof(header));
	length = header_value(req, "Content-Length", length_text, sizeof(length_text));
	request->has_length = length != NULL;
	// A value that fills its room may have lost its end, and the server does not say so for every one that
	// has (it reports a cut value only from two bytes too many on, httpd_parse.c). Fifteen digits are more
	// than 32 bit hold unless zeros lead them, and no client sends those: the largest number there is.
	request->length = length != NULL && strlen(length) >= sizeof(length_text) - 1 ? UINT32_MAX : length_of(length);
	request->slot_size = slot_size;

	// The server reads a body of the length it was told and no other: what a request without one sends
	// behind its headers would be taken for the next request. And a HEAD is answered like every method that
	// is not served, with a body the client does not expect.
	// The length the server goes by is its own reading of the header (req->content_len, 0 for one it takes
	// for none: beyond 32 bit it wraps). Where that is not the number web_route() was given, nobody knows
	// where this request ends: it is answered - refused, with a length like that - and nothing more is read.
	close_after = request->method == WEB_OTHER || (request->method != WEB_GET && !request->has_length) ||
	              (request->has_length && request->length != req->content_len);
}

// Whether the firmware upload still runs. The app ends one that brings nothing (app_tick()).
static bool uploading(void)
{
	bool runs;

	platform_lock();
	runs = platform_app->uploading;
	platform_unlock();
	return runs;
}

// Receives exactly `length` bytes of the body, without the lock. Returns false if they did not all arrive:
// the connection broke, nothing came for the receive timeout of the server, or the body as a whole took
// longer than WEB_BODY_MS.
// patient: a block of a running firmware upload. How long an upload may bring nothing is the app's to say,
// not the server's: the wait goes on while the upload runs. The app is asked after every timeout, once in
// WEB_LOOK_MS while bytes come in, and when the block is whole - the caller writes a block that is
// returned, and nothing is written once the app has ended the upload (app_web.h).
static bool receive(httpd_req_t *req, char *buffer, size_t length, bool patient)
{
	uint64_t begun_ms = platform_now_ms();
	uint64_t looked_ms = begun_ms;
	size_t got = 0;

	while(got < length)
	{
		int more = httpd_req_recv(req, buffer + got, length - got);
		uint64_t now = platform_now_ms();

		if(more > 0)
		{
			got += (size_t)more;
		}
		else if(more != HTTPD_SOCK_ERR_TIMEOUT || !patient)
		{
			return false;
		}

		if(patient)
		{
			if(more <= 0 || got == length || now - looked_ms >= WEB_LOOK_MS)
			{
				looked_ms = now;
				if(!uploading())
				{
					return false;
				}
			}
		}
		else if(got < length && now - begun_ms >= WEB_BODY_MS)
		{
			return false;
		}
	}
	return true;
}

/*
 * Sends the answer. Returns what the handler returns: anything but ESP_OK closes the connection.
 *
 * A request that was refused may have a body nobody read - megabytes, if it is a firmware. It is read and
 * dropped here, behind the answer: a browser does not look at the answer before it has sent all of its
 * request, and a connection closed under it shows there as a network error instead of the refusal. A client
 * that stops sending ends in the receive timeout of the server, one that is still sending after WEB_DROP_MS
 * ends there, and the connection is closed: nobody else is served while this lasts.
 */
static esp_err_t answer(httpd_req_t *req, int status, const char *type, const char *body, size_t length)
{
	uint64_t until_ms = 0;
	int dropped;

	httpd_resp_set_status(req, status_line(status));
	httpd_resp_set_type(req, type);
	if(httpd_resp_send(req, body, (ssize_t)length) != ESP_OK || close_after)
	{
		return ESP_FAIL;
	}
	// Nothing of the request is left in most cases: the first call returns 0 then, and no time is taken
	for(;;)
	{
		dropped = httpd_req_recv(req, room->body, sizeof(room->body));
		if(dropped <= 0)
		{
			break;
		}
		if(until_ms == 0)
		{
			until_ms = platform_now_ms() + WEB_DROP_MS;
		}
		else if(platform_now_ms() >= until_ms)
		{
			return ESP_FAIL;
		}
	}
	return dropped == 0 ? ESP_OK : ESP_FAIL;
}

// A step of the flash that failed is why the upload ends
static bool flashed(esp_err_t err, const char *step)
{
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "firmware: %s: %s", step, esp_err_to_name(err));
	}
	return err == ESP_OK;
}

// Tells the app what was written, and returns whether the upload still runs
static bool progress(uint32_t written, uint32_t size)
{
	uint64_t now = platform_now_ms();
	bool runs;

	platform_lock();
	app_web_upload_progress(platform_app, written, size, now);
	runs = platform_app->uploading;
	platform_events();
	platform_unlock();
	return runs;
}

/*
 * POST /api/ota (app_web.h): the first block goes to app_web_upload_begin() before anything is erased. If it
 * is taken, the firmware is written block by block into the slot that does not run, and esp_ota_end() checks
 * the whole image. Nothing here makes that slot the one to boot: the main task does, when the knob has
 * confirmed (APP_EVENT_INSTALL_FIRMWARE).
 *
 * The slot is erased as the writing goes (OTA_WITH_SEQUENTIAL_WRITES), a sector ahead of each block. Erased
 * in one piece before the first byte, 4 MB would keep the flash busy for many seconds in which nothing is
 * written - and bytes written are what tells the app that the upload is alive.
 *
 * CHECK: the picture during an upload (board.c names what to look at while the flash is written), and
 * how long 2 MB take.
 */
static esp_err_t upload(httpd_req_t *req, uint32_t size)
{
	esp_ota_handle_t ota = 0;
	uint32_t written = 0;
	size_t have = size < WEB_BLOCK_SIZE ? size : WEB_BLOCK_SIZE;
	size_t length = 0;
	uint64_t now;
	bool begun;
	bool ok;
	int status;

	// Nothing has begun yet, and a client that does not send its request waits for no answer
	if(!receive(req, (char *)block, have, false))
	{
		return ESP_FAIL;
	}

	now = platform_now_ms();
	platform_lock();
	status = app_web_upload_begin(platform_app, block, have, size, slot_size, room->out, &length, now);
	platform_events();
	platform_unlock();
	if(status != 0)
	{
		return answer(req, status, WEB_TYPE_JSON, room->out, length);
	}

	// From here on app_web_upload_end() is owed, once, however this ends.
	// First it is made known, for good, that the other slot holds no version to go back to (main.c). If
	// that cannot be written down, nothing is erased and the upload ends as one that failed: what it would
	// leave in the slot could be started as the "previous version" after a restart.
	ESP_LOGI(TAG, "firmware: %" PRIu32 " bytes into %s", size, slot->label);

	begun = platform_upload_begun() && flashed(esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota), "begin");
	ok = begun;
	while(ok)
	{
		ok = flashed(esp_ota_write(ota, block, have), "write");
		if(!ok)
		{
			break;
		}
		written += have;
		// Only after bytes were written, and with a look at what the app says: it may have ended the upload
		ok = progress(written, size);
		if(!ok || written == size)
		{
			break;
		}
		have = size - written < WEB_BLOCK_SIZE ? size - written : WEB_BLOCK_SIZE;
		ok = receive(req, (char *)block, have, true);
	}
	if(ok)
	{
		// Reads the image back and verifies its check sum; the handle is gone with it in every case
		ok = flashed(esp_ota_end(ota), "check of the image");
	}
	else if(begun)
	{
		esp_ota_abort(ota);
	}
	ESP_LOGI(TAG, "firmware: %" PRIu32 " of %" PRIu32 " bytes written, %s", written, size, ok ? "image good" : "given up");

	// Before the app asks for the knob: when the press comes, the main task has to know that the slot is good
	if(ok)
	{
		platform_upload_complete();
	}

	now = platform_now_ms();
	platform_lock();
	status = app_web_upload_end(platform_app, ok, room->out, &length, now);
	platform_events();
	platform_unlock();
	return answer(req, status, WEB_TYPE_JSON, room->out, length);
}

// Every request, in the task of the server
static esp_err_t handle(httpd_req_t *req)
{
	web_request_t request;
	web_decision_t decision;
	size_t length = 0;
	uint64_t now;
	int status;

	// What the requests before this one raised is stored before this one is served (app_web.h). Usually
	// nothing waits. After a request that restarts the display this waits for the restart: the answer to
	// that request is out, and no other is given.
	platform_stored();
	read_request(req, &request);

	// The time is read after the request has arrived and before the lock is waited for
	now = platform_now_ms();
	platform_lock();
	app_web_request(platform_app, &request, now);
	decision = web_route(&request);
	platform_unlock();

	if(decision.route == WEB_ROUTE_NONE)
	{
		int written = web_error_body(decision.error, room->out, APP_WEB_OUT_SIZE);

		return answer(req, decision.status, WEB_TYPE_JSON, room->out, written > 0 ? (size_t)written : 0);
	}
	if(decision.route == WEB_ROUTE_PAGE)
	{
		// The page must not be shown inside the page of somebody else: a click that is led into it there
		// is a request of the page itself, with its header and all, while the release is open. Said in the
		// two ways browsers know; the policy the page carries cannot say it (frame-ancestors does not
		// count in a <meta>). The server keeps the pointers until the answer is sent: constants.
		httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
		httpd_resp_set_hdr(req, "Content-Security-Policy", "frame-ancestors 'none'");
		return answer(req, 200, WEB_TYPE_PAGE, page_start, (size_t)(page_end - page_start));
	}
	if(decision.route == WEB_ROUTE_OTA)
	{
		return upload(req, request.length);
	}

	// The body, whole, before the function of the route is called: one that did not arrive whole is no
	// request, and the function is not called for it. web_route() has refused what is larger than the room.
	if(request.method != WEB_GET &&
	   (request.length > sizeof(room->body) || !receive(req, room->body, request.length, false)))
	{
		return ESP_FAIL;
	}

	now = platform_now_ms();
	platform_lock();
	switch(decision.route)
	{
		case WEB_ROUTE_LAYOUT_CHECK:
		case WEB_ROUTE_LAYOUT_APPLY:
		case WEB_ROUTE_LAYOUT_SAVE:
		case WEB_ROUTE_LAYOUT_RESET:
			status = app_web_layout(platform_app, decision.route, room->body, request.length, room->out, &length, now);
			break;
		case WEB_ROUTE_WIFI_STORE:
		case WEB_ROUTE_WIFI_FORGET:
			status = app_web_wifi(platform_app, decision.route, room->body, request.length, room->out, &length, now);
			break;
		case WEB_ROUTE_SETTINGS:
			status = app_web_settings(platform_app, room->body, request.length, room->out, &length, now);
			break;
		case WEB_ROUTE_REBOOT:
		case WEB_ROUTE_RESET:
			status = app_web_action(platform_app, decision.route, room->out, &length, now);
			break;
		default:
			// The reading routes; a route this file does not know yet is answered 404 there
			status = app_web_get(platform_app, decision.route, decision.ticket, room->out, &length, now);
			break;
	}
	// What the request raised is taken, with what it names, before the lock is given back. A restart is
	// carried out by the main task a moment later; the answer is on its way by then (on_open()).
	platform_events();
	platform_unlock();
	return answer(req, status, WEB_TYPE_JSON, room->out, length);
}

// What the server cannot read as a request. Its own answer would be a text of the server: none is given.
static esp_err_t on_error(httpd_req_t *req, httpd_err_code_t error)
{
	ESP_LOGW(TAG, "no request (error %d of the server): connection closed without an answer", (int)error);
	return ESP_FAIL;
}

// A new connection. Without this the stack holds the end of a small answer back until the client has
// confirmed its beginning, which a client does with a delay of its own: every answer would be late, and the
// one before a restart, or before the network is left (net.c), might not leave at all.
static esp_err_t on_open(httpd_handle_t hd, int fd)
{
	int on = 1;

	if(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) != 0)
	{
		ESP_LOGW(TAG, "connection %d: answers may leave with a delay", fd);
	}
	return ESP_OK;
}

esp_err_t web_start(void)
{
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	const httpd_uri_t every =
	{
		.uri = "*",
		.method = HTTP_ANY,
		.handler = handle,
	};
	httpd_handle_t server = NULL;

	ESP_RETURN_ON_FALSE(room == NULL, ESP_ERR_INVALID_STATE, TAG, "web_start() called twice");
	room = heap_caps_malloc(sizeof(*room), MALLOC_CAP_SPIRAM);
	ESP_RETURN_ON_FALSE(room != NULL, ESP_ERR_NO_MEM, TAG, "no external RAM for %u bytes", (unsigned)sizeof(*room));
	block = heap_caps_malloc(WEB_BLOCK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	ESP_RETURN_ON_FALSE(block != NULL, ESP_ERR_NO_MEM, TAG, "no internal RAM for %d bytes", WEB_BLOCK_SIZE);

	// Without such a slot its size is 0, and web_route() refuses every firmware
	slot = esp_ota_get_next_update_partition(NULL);
	slot_size = slot != NULL ? slot->size : 0;
	ESP_LOGI(TAG, "firmware goes into %s, %" PRIu32 " bytes", slot != NULL ? slot->label : "no slot", slot_size);

	config.stack_size = WEB_STACK_BYTES;
	config.max_open_sockets = WEB_SOCKETS;
	// A browser keeps its connections open: a new client gets the one that was used least recently
	config.lru_purge_enable = true;
	config.max_uri_handlers = 1;
	config.max_req_hdr_len = WEB_HEADERS_BYTES;
	config.uri_match_fn = httpd_uri_match_wildcard;
	config.open_fn = on_open;

	// Needs the TCP/IP stack and the default event loop, which net_start() brings up
	ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "HTTP server");
	// The errors first: the server runs already, and until the handler is there it would answer by itself.
	// It does, for a request that comes within the few instructions from httpd_start() to the turn of the
	// loop below that registers on_error() for it: "404 Not Found" as text, without a header of ours and
	// without anything of the display in it (httpd_uri.c, httpd_req_handle_err()). The handlers need the
	// handle, so the moment cannot be closed. It lies at the start of the display: the station is in no
	// network yet, and nobody can have joined the own access point.
	for(int error = 0; error < HTTPD_ERR_CODE_MAX; error++)
	{
		ESP_RETURN_ON_ERROR(httpd_register_err_handler(server, (httpd_err_code_t)error, on_error), TAG, "error handler");
	}
	ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &every), TAG, "handler");
	return ESP_OK;
}
