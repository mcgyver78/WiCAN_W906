/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include <strings.h>
#include <stdatomic.h>
#include "platform.h"
#include "app_web.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "mdns.h"

/*
 * The network task: it drives the WiFi driver by what app->link orders and asks the adapter what app->poll
 * hands out. It decides nothing. Written without the board and without a build: every call was read in the
 * sources of ESP-IDF v5.5.2 and of espressif/mdns 1.13.1. What only the board can decide is marked CHECK.
 *
 * One task does both, so nothing here needs a lock of its own. What the task waits on, wherever it waits:
 *   net_task()       an event of the driver, at most NET_STEP_MS; the link and the poll have times of their
 *                    own and are asked again then
 *   poll_step()      the main task, until what the events named is stored (platform_stored(); app.h)
 *   ask()            the adapter, at most POLL_TIMEOUT_MS for the whole request
 *   find()           the first answer to the mDNS query, at most NET_FIND_MS
 *   station_stop()   the driver's report that the station has left, at most NET_STOP_WAIT_MS
 *   leave()          NET_LEAVE_WAIT_MS, so that the answer of the web server is out before the network is
 *
 * Two of these limits are not this task's to keep. ask(): the HTTP client knows a time for each of its
 * steps, not for a request, and on_http() shortens it step by step - but while the headers of the answer
 * are read no step ends as long as bytes arrive (esp_http_client_fetch_headers() reads on until the headers
 * are complete, whatever its parser made of them), and the client does not look at what on_http() returns.
 * An adapter, or something else at its address, that never finishes its headers holds the task. find(): the
 * mDNS component ends the query by a timer of its own task, and mdns_query_ptr() waits for that task
 * without a limit. So the task counts its turns, and the main task restarts the display when the count
 * stands still (net_turns(), main.c).
 *
 * The events of the driver arrive in the task of the event loop. They are passed on as bits and not as a
 * queue of messages: bits cannot run over while this task waits for the adapter, and what an event says is
 * read from the driver when the bit is looked at.
 */

#define TAG "net"

// CHECK: uxTaskGetStackHighWaterMark() of this task after a day with scans, queries and a fault memory read.
// The logic of the core needs less than 1 KB of it (frames compiled on a PC), the HTTP client was not measured.
#define NET_STACK_BYTES     6144
#define NET_PRIORITY        5
#define NET_STEP_MS         100u
#define NET_SIGNAL_MS       1000u   // the signal strength for the info page is read this often
#define NET_LEAVE_WAIT_MS   500u
#define NET_STOP_WAIT_MS    2000u
#define NET_SCAN_LIMIT_MS   10000u  // a scan of all channels takes about 2 s (120 ms each, esp_wifi.h)
#define NET_FIND_MS         3000u
#define NET_SERVICE         "_wican"    // what the adapter announces (main/wc_mdns.c)
#define NET_PROTO           "_tcp"
#define NET_AP_CHANNEL      1       // until the station joins a network: then the driver moves it there
#define NET_AP_CLIENTS      4
#define NET_AP_RETRY_MS     1000u   // a refused order for the access point is given again this often
#define NET_ADDRESS_SIZE    16      // "255.255.255.255" and its zero

#define NET_EVENT_SCAN_DONE     ((EventBits_t)0x01)
#define NET_EVENT_DISCONNECTED  ((EventBits_t)0x02)
#define NET_EVENT_GOT_IP        ((EventBits_t)0x04)
#define NET_EVENT_AP_CLIENTS    ((EventBits_t)0x08)
#define NET_EVENT_ALL           (NET_EVENT_SCAN_DONE | NET_EVENT_DISCONNECTED | NET_EVENT_GOT_IP | NET_EVENT_AP_CLIENTS)

// What is too large for the internal RAM, allocated once in the external RAM
typedef struct
{
	wifi_ap_record_t records[LINK_SEEN_MAX];
	char seen[LINK_SEEN_MAX][NET_SSID_SIZE];
	web_seen_t seen_web[LINK_SEEN_MAX];
	json_token_t work[POLL_TOKENS];
	char body[POLL_BODY_SIZE];
} net_room_t;

// The answer to the request under way, as on_http() collects it
typedef struct
{
	uint64_t until_ms;      // when the request is given up
	size_t length;          // bytes of the body in room->body
	bool too_large;         // the body has no room there
	bool connected;         // a connection was opened for this request
	bool sent;              // the request went out
	bool has_seq;
	char seq[16];           // value of POLL_SEQ_HEADER; empty if it is longer than any number
} net_reply_t;

static net_room_t *room;
static EventGroupHandle_t events;
static esp_netif_t *sta_netif;
static esp_netif_t *ap_netif;

// The station, as this task knows the driver
static bool sta_busy;       // esp_wifi_connect() was called and the driver has not reported the end of it
static bool joining;        // the link waits for the outcome of that call
static bool has_ip;         // the station is in a network
static atomic_uchar sta_reason;     // why the driver last reported the station as disconnected: set by the
                                    // task of the event loop, read by this one for the log
static char sta_ssid[NET_SSID_SIZE];
static char sta_ip[NET_ADDRESS_SIZE];
static int sta_rssi;
static uint64_t signal_ms;

static bool scanning;
static uint64_t scan_since_ms;

static wifi_config_t ap_config;
static bool ap_on;
static bool ap_wanted;      // as the link ordered last
static bool ap_owed;        // the driver has refused that order: ap_step() gives it again
static uint64_t ap_tried_ms;
static char ap_ip[NET_ADDRESS_SIZE];

static atomic_uint task_turns;   // of net_task(), for the main task (net_turns())

// The conversation with the adapter
static esp_http_client_handle_t client;
static char client_host[LINK_HOST_SIZE];    // the address `client` was made for
static bool kept;           // a connection to the adapter was opened, and it was not the display that closed it
static uint32_t reconnects;
static net_reply_t reply;

uint32_t net_turns(void)
{
	return (uint32_t)atomic_load(&task_turns);
}

/* What the info page shows --------------------------------------------------------------------------- */

// The part of platform_info this task fills. The lock is held.
static void info_fill(void)
{
	// In no network the display is reached through its own access point: its address is the one to show then
	const char *ip = has_ip ? sta_ip : ap_on ? ap_ip : "";

	strlcpy(platform_info.ssid, has_ip ? sta_ssid : "", sizeof(platform_info.ssid));
	strlcpy(platform_info.ip, ip, sizeof(platform_info.ip));
	platform_info.rssi = has_ip ? sta_rssi : 0;
	platform_info.reconnects = reconnects;
}

// A report to the link is made under the lock and is followed by app_net() and platform_events() before the
// lock is given back (app.h). The time is read before the lock is waited for.
static uint64_t report_begin(void)
{
	uint64_t now = platform_now_ms();

	platform_lock();
	return now;
}

static void report_end(uint64_t now)
{
	app_net(platform_app, now);
	platform_events();
	info_fill();
	platform_unlock();
}

static void signal_read(void)
{
	int rssi;

	signal_ms = platform_now_ms();
	if(esp_wifi_sta_get_rssi(&rssi) == ESP_OK)
	{
		sta_rssi = rssi;
	}
}

// Once a second while the station is in a network: the signal strength, and with it the count of new
// connections to the adapter, which only moves then
static void signal_step(void)
{
	if(!has_ip || platform_now_ms() - signal_ms < NET_SIGNAL_MS)
	{
		return;
	}
	signal_read();
	platform_lock();
	info_fill();
	platform_unlock();
}

/* The conversation with the adapter ------------------------------------------------------------------ */

// What is left of the time of the request, for the next step of the HTTP client. Never less than 1: the
// transport below it takes -1 for "no limit".
static int time_left(uint64_t until_ms)
{
	uint64_t now = platform_now_ms();

	return now < until_ms ? (int)(until_ms - now) : 1;
}

// The display itself gives the connection up: the network is gone, or the next request must not use it
static void client_drop(void)
{
	if(client != NULL)
	{
		esp_http_client_close(client);
	}
	kept = false;
}

// Called by the HTTP client from within esp_http_client_perform(), in this task
static esp_err_t on_http(esp_http_client_event_t *event)
{
	switch(event->event_id)
	{
		case HTTP_EVENT_ON_CONNECTED:
			// The adapter may close the connection at any time (tools/w906/API.md). A connection that had to
			// be opened although one was kept is what the info page counts.
			if(kept)
			{
				reconnects++;
			}
			kept = true;
			reply.connected = true;
			break;
		case HTTP_EVENT_HEADERS_SENT:
			reply.sent = true;
			break;
		case HTTP_EVENT_ON_HEADER:
			if(strcasecmp(event->header_key, POLL_SEQ_HEADER) == 0)
			{
				// A value without room here is longer than any number of 32 bit: sent, but no number
				size_t length = strlen(event->header_value);

				reply.has_seq = true;
				reply.seq[0] = '\0';
				if(length < sizeof(reply.seq))
				{
					memcpy(reply.seq, event->header_value, length + 1);
				}
			}
			break;
		case HTTP_EVENT_ON_DATA:
			// The last byte of the room is for the zero behind the body (poll.h)
			if(!reply.too_large && event->data_len >= 0 && reply.length + (size_t)event->data_len < POLL_BODY_SIZE)
			{
				memcpy(&room->body[reply.length], event->data, (size_t)event->data_len);
				reply.length += (size_t)event->data_len;
			}
			else
			{
				reply.too_large = true;
			}
			break;
		default:
			break;
	}
	// The client takes its timeout for every single step - connecting, sending, each read. POLL_TIMEOUT_MS
	// is meant for the request as a whole: each step may take what is left of it.
	esp_http_client_set_timeout_ms(event->client, time_left(reply.until_ms));
	return ESP_OK;
}

// The client for the adapter at `host`; made anew when the address is another one. The host is given to it
// as it is, once: handed over inside a URL the client refuses every name with an underscore (the URL parser
// of ESP-IDF is built strict), and "wican_<id>.local" is what an adapter is called.
static bool client_for(const char *host)
{
	const esp_http_client_config_t config =
	{
		.host = host,
		.path = "/",
		.event_handler = on_http,
		.timeout_ms = (int)POLL_TIMEOUT_MS,
		// A request is sent once. The client would send it again by itself after a 3xx, and after a 401
		// if it could log in: a clear of the fault memory must never go out twice.
		.disable_auto_redirect = true,
		.max_authorization_retries = -1,
	};

	if(client != NULL && strcmp(client_host, host) == 0)
	{
		return true;
	}
	if(client != NULL)
	{
		esp_http_client_cleanup(client);
		kept = false;
	}
	client = esp_http_client_init(&config);
	if(client == NULL)
	{
		ESP_LOGE(TAG, "no HTTP client for %s", host);
		return false;
	}
	strlcpy(client_host, host, sizeof(client_host));
	return true;
}

// One attempt at the request. Returns the HTTP status and leaves body and header in `reply` and room->body,
// or 0 if no answer came: the connection is closed then.
static int attempt(const char *host, const poll_request_t *request, uint64_t until_ms)
{
	int status = 0;

	memset(&reply, 0, sizeof(reply));
	reply.until_ms = until_ms;
	room->body[0] = '\0';

	// The path with its query, and no host in it: the client keeps the one it was made with
	if(!client_for(host) || esp_http_client_set_url(client, request->path) != ESP_OK)
	{
		return 0;
	}
	esp_http_client_set_method(client, request->post ? HTTP_METHOD_POST : HTTP_METHOD_GET);
	// The client keeps its headers from request to request: a GET takes this one off again. A POST without
	// a body goes out with "Content-Length: 0".
	esp_http_client_set_header(client, POLL_HEADER_NAME, request->post ? POLL_HEADER_VALUE : NULL);
	esp_http_client_set_timeout_ms(client, time_left(until_ms));

	if(esp_http_client_perform(client) == ESP_OK)
	{
		status = esp_http_client_get_status_code(client);
		// A body larger than the room is passed on as an empty one, with the status that came (poll.h)
		if(reply.too_large)
		{
			reply.length = 0;
		}
	}
	else
	{
		// A 401 is an answer as well, but the client stops at it without reading the body. The status it
		// holds is that of this request only if the request went out: it is reset when the answer is
		// waited for.
		if(reply.sent && esp_http_client_get_status_code(client) == HttpStatus_Unauthorized)
		{
			status = HttpStatus_Unauthorized;
		}
		// After an error the client is left wherever it was, with the connection open: the next request
		// would go on from there
		esp_http_client_close(client);
		// A body that did not arrive whole is none
		reply.length = 0;
	}
	room->body[reply.length] = '\0';
	return status > 0 ? status : 0;
}

// Sends the request and waits for its answer, POLL_TIMEOUT_MS at most. Returns the HTTP status, 0 without
// an answer.
static int ask(const char *host, const poll_request_t *request)
{
	uint64_t until_ms = platform_now_ms() + POLL_TIMEOUT_MS;
	int status;

	// CHECK: a host stored as a name (wican_<id>.local) is looked up by lwIP before the client connects,
	// and that time is not under POLL_TIMEOUT_MS. With an adapter that is switched off, see in the log how
	// long a request takes then.

	// A POST is never sent twice (API.md), so it must not go into a connection the adapter may have closed
	// meanwhile: whether it arrived could not be told then. It gets a connection of its own.
	if(request->post)
	{
		client_drop();
	}
	status = attempt(host, request, until_ms);
	// A GET that went into the connection kept open and got no answer: the adapter has closed it. Once more
	// over a new connection, while the time of the request lasts.
	if(status == 0 && !request->post && !reply.connected && platform_now_ms() < until_ms)
	{
		status = attempt(host, request, until_ms);
	}
	return status;
}

// One request to the adapter, if the poll has one. Returns true if an answer came.
static bool poll_step(void)
{
	poll_request_t request;
	char host[LINK_HOST_SIZE] = "";
	uint64_t now = platform_now_ms();
	bool due;
	int status;

	platform_lock();
	due = poll_prepare(&platform_app->poll, now, &request);
	if(due)
	{
		app_net(platform_app, now);
		platform_events();
		// The text lies in the app and changes with it
		strlcpy(host, app_host(platform_app), sizeof(host));
	}
	platform_unlock();
	// Without an address app_net() has ended the request already: the link is not up (app.h)
	if(!due || host[0] == '\0')
	{
		return false;
	}

	// Not before all that the events named up to here is stored (app.h): what the last answer raised, and
	// what the knob or the browser stored meanwhile. Usually nothing waits. A restart that is under way is
	// waited for as well: no request goes out in its last half second.
	platform_stored();
	status = ask(host, &request);

	now = platform_now_ms();
	platform_lock();
	poll_apply(&platform_app->poll, &request, status, room->body, reply.length, reply.has_seq ? reply.seq : NULL,
	           now, room->work, POLL_TOKENS);
	app_net(platform_app, now);
	platform_events();
	platform_unlock();
	return status != 0;
}

/* The station ---------------------------------------------------------------------------------------- */

// The station is in no network and on its way into none
static void station_idle(void)
{
	sta_busy = false;
	joining = false;
	has_ip = false;
	client_drop();
}

/*
 * Ends what the station does - a network it is in, or an attempt the link has given up - and waits until
 * the driver says that it is over: before that the driver takes neither a scan nor another network
 * (ESP_ERR_WIFI_STATE, esp_wifi.h). Nothing is reported to the link: it has ordered something else already.
 *
 * CHECK: the warning below never shows. esp_wifi_disconnect() is documented to end with the disconnected
 * event for a station that is connected; for one that is still connecting nothing is written down. If the
 * warning shows there, the wait is lost time - and the report may still come later. It is then booked on
 * whatever the station does by that time (station_ended()): a join that is under way counts as failed
 * although it goes on, and the order after it finds a driver that is still connecting. The link gets over
 * that with its own waits, half a minute at most. Each such report is logged with the reason the driver
 * gives for it; 8 (WIFI_REASON_ASSOC_LEAVE) is the one a disconnect that was asked for ends with. If the
 * log shows that reason behind the warning, a late report has to be told apart from a failed join by it.
 */
static void station_stop(void)
{
	if(sta_busy && esp_wifi_disconnect() == ESP_OK)
	{
		uint64_t now = platform_now_ms();
		uint64_t until_ms = now + NET_STOP_WAIT_MS;

		while(sta_busy && now < until_ms)
		{
			// One tick more: a wait of no tick at all would turn the end of this loop into a busy one
			if(xEventGroupWaitBits(events, NET_EVENT_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(until_ms - now) + 1) &
			   NET_EVENT_DISCONNECTED)
			{
				sta_busy = false;
			}
			now = platform_now_ms();
		}
		if(sta_busy)
		{
			ESP_LOGW(TAG, "the driver did not report that the station has left \"%s\"", sta_ssid);
		}
	}
	// What the driver reported about the station up to here is about what has just been ended
	xEventGroupClearBits(events, NET_EVENT_DISCONNECTED | NET_EVENT_GOT_IP);
	station_idle();
}

// The driver reports that the station is in no network: the attempt to join has failed, or the network is
// lost. The link ignores the report that does not fit its state (link.h).
static void station_ended(void)
{
	bool failed = joining;
	uint64_t now;

	station_idle();
	ESP_LOGI(TAG, "\"%s\": %s (reason %u of the driver)", sta_ssid, failed ? "not joined" : "left by the network",
	         (unsigned)atomic_load(&sta_reason));

	now = report_begin();
	if(failed)
	{
		link_join_failed(&platform_app->link, now);
	}
	else
	{
		link_lost(&platform_app->link, now);
	}
	report_end(now);
}

// The station has an address: the network is joined. The event also comes when the address is renewed.
static void station_got_ip(void)
{
	esp_netif_ip_info_t ip;
	char gateway[NET_ADDRESS_SIZE] = "";
	uint64_t now;

	if(esp_netif_get_ip_info(sta_netif, &ip) != ESP_OK || ip.ip.addr == 0)
	{
		return;
	}
	esp_ip4addr_ntoa(&ip.ip, sta_ip, sizeof(sta_ip));
	if(ip.gw.addr != 0)
	{
		esp_ip4addr_ntoa(&ip.gw, gateway, sizeof(gateway));
	}
	// An address means a connection, whatever was assumed about the driver before
	sta_busy = true;
	has_ip = true;
	signal_read();
	ESP_LOGI(TAG, "\"%s\": address %s, gateway %s", sta_ssid, sta_ip, gateway);

	now = report_begin();
	if(joining)
	{
		joining = false;
		link_joined(&platform_app->link, gateway, now);
	}
	report_end(now);
}

static void join(const char *ssid, const char *password)
{
	wifi_config_t config;
	uint64_t now;

	station_stop();

	// A union: the station side of it. Name and password may fill their fields to the last byte.
	// Everything else stays 0: any channel, the first access point with that name, and - by the driver's
	// own rule for a password of 8 bytes or more - nothing weaker than WPA2.
	memset(&config, 0, sizeof(config));
	memcpy(config.sta.ssid, ssid, strnlen(ssid, sizeof(config.sta.ssid)));
	memcpy(config.sta.password, password, strnlen(password, sizeof(config.sta.password)));
	strlcpy(sta_ssid, ssid, sizeof(sta_ssid));

	if(esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK && esp_wifi_connect() == ESP_OK)
	{
		// The driver tries once and ends the attempt with an event: got_ip, or disconnected. An attempt
		// that ends with neither is given up by the link after LINK_JOIN_TIMEOUT_MS.
		sta_busy = true;
		joining = true;
		ESP_LOGI(TAG, "\"%s\": joining", sta_ssid);
		return;
	}
	ESP_LOGW(TAG, "\"%s\": the driver does not take the network", sta_ssid);
	now = report_begin();
	link_join_failed(&platform_app->link, now);
	report_end(now);
}

static void leave(void)
{
	uint64_t now;

	// The order follows a request to the web server (a network was forgotten, app_web.h), and the answer
	// to that request has to leave through the network that is given up here. The server sends it as soon
	// as it has the lock back, and without delay (web.c): this wait is for that.
	vTaskDelay(pdMS_TO_TICKS(NET_LEAVE_WAIT_MS));
	station_stop();

	now = report_begin();
	link_left(&platform_app->link, now);
	report_end(now);
}

/* Scan and query ------------------------------------------------------------------------------------- */

// The scan is over with the first `count` of room->records
static void scan_report(uint16_t count)
{
	uint64_t now;

	scanning = false;
	for(uint16_t i = 0; i < count; i++)
	{
		const wifi_ap_record_t *record = &room->records[i];
		web_seen_t *seen = &room->seen_web[i];

		// The name has 32 bytes at most and its zero; a hidden network has an empty one
		memset(seen, 0, sizeof(*seen));
		strlcpy(seen->ssid, (const char *)record->ssid, sizeof(seen->ssid));
		seen->rssi = record->rssi;
		seen->secure = record->authmode != WIFI_AUTH_OPEN;
		memcpy(room->seen[i], seen->ssid, NET_SSID_SIZE);
	}
	ESP_LOGI(TAG, "scan: %u networks", (unsigned)count);

	now = report_begin();
	link_scanned(&platform_app->link, (const char (*)[NET_SSID_SIZE])room->seen, count, now);
	app_web_seen(platform_app, room->seen_web, count);
	report_end(now);
}

// The driver has ended a scan. Its list is fetched in every case, also for the late end of a scan that was
// given up: the driver keeps the list until somebody takes it, and the link ignores the report of a scan it
// does not wait for (link.h).
static void scan_done(void)
{
	uint16_t count = LINK_SEEN_MAX;

	// The strongest come first, and what has no room here is freed by the same call. In a place with more
	// than LINK_SEEN_MAX networks the weakest are not seen: the adapter in the same vehicle is not one of them.
	if(esp_wifi_scan_get_ap_records(&count, room->records) != ESP_OK)
	{
		esp_wifi_clear_ap_list();
		count = 0;
	}
	scan_report(count);
}

static void scan_start(void)
{
	station_stop();
	// All channels, active, hidden networks left out: the defaults of the driver
	if(esp_wifi_scan_start(NULL, false) != ESP_OK)
	{
		ESP_LOGW(TAG, "scan: the driver does not start it");
		scan_report(0);
		return;
	}
	scanning = true;
	scan_since_ms = platform_now_ms();
}

// The link has no time limit for a scan (link.h): without a report it would wait for good, and the display
// would never join a network again.
// CHECK: this warning never shows. The driver promises the event for every scan that was started.
static void scan_watch(void)
{
	if(scanning && platform_now_ms() - scan_since_ms >= NET_SCAN_LIMIT_MS)
	{
		ESP_LOGW(TAG, "scan: no end after %u ms, taken as over", (unsigned)NET_SCAN_LIMIT_MS);
		esp_wifi_scan_stop();
		scan_done();
	}
}

// Asks for the service the adapter announces and reports the address of the first that answers
static void find(void)
{
	mdns_result_t *results = NULL;
	char host[NET_ADDRESS_SIZE] = "";
	uint64_t now;

	// The query ends with the first answer; its records for the host come in the same packet
	if(mdns_query_ptr(NET_SERVICE, NET_PROTO, NET_FIND_MS, 1, &results) == ESP_OK)
	{
		for(const mdns_result_t *result = results; result != NULL && host[0] == '\0'; result = result->next)
		{
			for(const mdns_ip_addr_t *address = result->addr; address != NULL; address = address->next)
			{
				if(address->addr.type == ESP_IPADDR_TYPE_V4 && address->addr.u_addr.ip4.addr != 0)
				{
					esp_ip4addr_ntoa(&address->addr.u_addr.ip4, host, sizeof(host));
					break;
				}
			}
		}
		mdns_query_results_free(results);
	}
	ESP_LOGI(TAG, "query for the adapter: %s", host[0] != '\0' ? host : "no answer");

	now = report_begin();
	if(host[0] != '\0')
	{
		link_found(&platform_app->link, host, now);
	}
	else
	{
		link_not_found(&platform_app->link, now);
	}
	report_end(now);
}

/* The own access point ------------------------------------------------------------------------------- */

/*
 * CHECK: the station stays in its network when the access point is switched on or off (no "left by the
 * network" in the log right after). If it does not, the display finds its way back by itself: scan, join.
 * CHECK: when the access point opens, no network "ESP_..." without a password shows up, not even for a
 * moment (watch with a phone that lists networks). The driver starts the access point with the mode, with
 * what it has kept of the configuration net_start() gave it; whether it keeps it is not documented.
 */
static void access_point(bool on)
{
	esp_netif_ip_info_t ip;
	esp_err_t err = esp_wifi_set_mode(on ? WIFI_MODE_APSTA : WIFI_MODE_STA);

	if(err == ESP_OK && on)
	{
		err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
		if(err != ESP_OK)
		{
			// Never an access point with anything but the name and the password of the display
			esp_wifi_set_mode(WIFI_MODE_STA);
		}
	}
	ap_wanted = on;
	ap_tried_ms = platform_now_ms();
	if(err == ESP_OK)
	{
		ap_owed = false;
		ap_on = on;
		ESP_LOGI(TAG, "access point %s", on ? "on" : "off");
	}
	else
	{
		// Said once for an order, not with every attempt at it
		if(!ap_owed)
		{
			ESP_LOGE(TAG, "access point %s: %s, tried again until the driver takes it", on ? "on" : "off",
			         esp_err_to_name(err));
		}
		ap_owed = true;
		// One that was to open is closed (above); one that was to close is as open as it was
		if(on)
		{
			ap_on = false;
		}
	}
	ap_ip[0] = '\0';
	if(ap_on && esp_netif_get_ip_info(ap_netif, &ip) == ESP_OK)
	{
		esp_ip4addr_ntoa(&ip.ip, ap_ip, sizeof(ap_ip));
	}

	platform_lock();
	info_fill();
	platform_unlock();
}

/*
 * The link hands each of its orders out once and takes it as carried out (link.h): an order for the access
 * point that the driver refused would be lost for good - the menu says "on", and no network opens. So it is
 * given again until the driver takes it. Not while the station is on its way into a network: that is when
 * the driver is documented to refuse a configuration (ESP_ERR_WIFI_STATE, esp_wifi.h), and a change of the
 * mode in the middle of an attempt is not known to leave the attempt alone. Between two attempts at a
 * network that does not take the display there is time for it: in every turn of the task this comes after
 * the reports of the driver and before the next order of the link.
 * CHECK: with a stored network whose password is wrong, switch the access point on in the menu while the
 * log says "joining": the access point has to open, at once or with the next "not joined".
 */
static void ap_step(void)
{
	if(ap_owed && !(sta_busy && !has_ip) && platform_now_ms() - ap_tried_ms >= NET_AP_RETRY_MS)
	{
		access_point(ap_wanted);
	}
}

// CHECK: with a phone that joins the access point and leaves it again the access point closes
// LINK_AP_IDLE_MS after the phone has left, not earlier and not never: the list of the driver is read when
// its event is looked at, and has to be up to date by then.
static void ap_clients(void)
{
	wifi_sta_list_t list;
	uint64_t now;
	int count = 0;

	if(ap_on && esp_wifi_ap_get_sta_list(&list) == ESP_OK)
	{
		count = list.num;
	}
	now = report_begin();
	link_ap_clients(&platform_app->link, count, now);
	report_end(now);
}

/* The task ------------------------------------------------------------------------------------------- */

// In the task of the event loop: only the bit, everything else is done by the network task
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
	EventBits_t bits = 0;

	if(base == WIFI_EVENT)
	{
		switch(id)
		{
			case WIFI_EVENT_SCAN_DONE:          bits = NET_EVENT_SCAN_DONE; break;
			case WIFI_EVENT_STA_DISCONNECTED:
				bits = NET_EVENT_DISCONNECTED;
				// The one thing a bit cannot carry and the driver does not tell again when asked
				if(data != NULL)
				{
					atomic_store(&sta_reason, ((const wifi_event_sta_disconnected_t *)data)->reason);
				}
				break;
			case WIFI_EVENT_AP_STACONNECTED:
			case WIFI_EVENT_AP_STADISCONNECTED: bits = NET_EVENT_AP_CLIENTS; break;
			default:                            break;
		}
	}
	else if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
	{
		bits = NET_EVENT_GOT_IP;
	}
	if(bits != 0)
	{
		xEventGroupSetBits(events, bits);
	}
}

static void driver_events(EventBits_t bits)
{
	if(bits & NET_EVENT_DISCONNECTED)
	{
		// Both at once: the address came first and went with the connection. Nothing can follow the end
		// of a connection but what this task orders.
		bits &= ~NET_EVENT_GOT_IP;
		station_ended();
	}
	if(bits & NET_EVENT_GOT_IP)
	{
		station_got_ip();
	}
	if(bits & NET_EVENT_SCAN_DONE)
	{
		scan_done();
	}
	if(bits & NET_EVENT_AP_CLIENTS)
	{
		ap_clients();
	}
}

// What the link orders now, one order with each turn of the task
static void link_step(void)
{
	char ssid[NET_SSID_SIZE] = "";
	char password[NET_PASSWORD_SIZE] = "";
	uint64_t now = platform_now_ms();
	link_do_t order;

	platform_lock();
	order = link_next(&platform_app->link, now);
	if(order == LINK_DO_JOIN)
	{
		int profile = link_profile(&platform_app->link);

		if(profile >= 0 && profile < NET_PROFILES_MAX)
		{
			// Whole fields, and a zero of their own: the copies end whatever the list holds
			memcpy(ssid, platform_app->profiles[profile].ssid, sizeof(ssid) - 1);
			memcpy(password, platform_app->profiles[profile].password, sizeof(password) - 1);
		}
	}
	platform_unlock();

	switch(order)
	{
		case LINK_DO_SCAN:      scan_start(); break;
		case LINK_DO_JOIN:      join(ssid, password); break;
		case LINK_DO_LEAVE:     leave(); break;
		case LINK_DO_FIND:      find(); break;
		case LINK_DO_AP_ON:     access_point(true); break;
		case LINK_DO_AP_OFF:    access_point(false); break;
		default:                break;
	}
}

static void net_task(void *arg)
{
	EventBits_t bits = 0;

	for(;;)
	{
		bool answered;

		atomic_fetch_add(&task_turns, 1);
		driver_events(bits);
		scan_watch();
		ap_step();
		link_step();
		answered = poll_step();
		signal_step();

		// The task waits here. Not after an answer of the adapter: the next request of the round may be
		// due at once, and the round is the poll's to time (one a second, conn.h) - the adapter's answer
		// is what the task has waited for then.
		bits = xEventGroupWaitBits(events, NET_EVENT_ALL, pdTRUE, pdFALSE, answered ? 0 : pdMS_TO_TICKS(NET_STEP_MS)) & NET_EVENT_ALL;
	}
}

esp_err_t net_start(const char *ap_ssid, const char *ap_password)
{
	wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
	size_t ssid_length = ap_ssid != NULL ? strlen(ap_ssid) : 0;
	size_t password_length = ap_password != NULL ? strlen(ap_password) : 0;
	esp_err_t err;

	ESP_RETURN_ON_FALSE(room == NULL, ESP_ERR_INVALID_STATE, TAG, "net_start() called twice");
	// WPA2 takes 8 to 63 characters. Without a password the access point would be open to everybody.
	ESP_RETURN_ON_FALSE(ssid_length >= 1 && ssid_length <= sizeof(ap_config.ap.ssid) &&
	                    password_length >= 8 && password_length < sizeof(ap_config.ap.password),
	                    ESP_ERR_INVALID_ARG, TAG, "access point: name or password cannot be used");

	events = xEventGroupCreate();
	ESP_RETURN_ON_FALSE(events != NULL, ESP_ERR_NO_MEM, TAG, "no memory for the events");
	room = heap_caps_malloc(sizeof(*room), MALLOC_CAP_SPIRAM);
	ESP_RETURN_ON_FALSE(room != NULL, ESP_ERR_NO_MEM, TAG, "no external RAM for %u bytes", (unsigned)sizeof(*room));

	ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "TCP/IP stack");
	// Another part of the platform may have made the loop already: there is one for all
	err = esp_event_loop_create_default();
	ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "event loop");
	sta_netif = esp_netif_create_default_wifi_sta();
	ap_netif = esp_netif_create_default_wifi_ap();

	// The driver gets no NVS of its own. It would keep there what it is told to keep in the RAM two lines
	// below, and it is known not to start with one it cannot open ("wifi nvs_open fail"; that check is in
	// the binary part of the driver and was not read). Without the store (store_init() failed, main.c goes
	// on) the display would then have neither access point nor web interface, just when somebody needs them
	// to get another firmware in. The calibration of the radio is not the driver's: esp_phy keeps it in the
	// NVS and calibrates in full at every start when it cannot (esp_phy_load_cal_and_init(), phy_init.c).
	// CHECK: once, with an NVS partition that cannot be read (the log names the failed store): the access
	// point opens and the page answers.
	init_config.nvs_enable = 0;
	ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "WiFi driver");
	// The networks are in the store of the display (STORE_KEY_WIFI): the driver writes none into the flash
	ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "WiFi storage");
	ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL), TAG, "WiFi events");
	ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL), TAG, "IP events");

	memset(&ap_config, 0, sizeof(ap_config));
	memcpy(ap_config.ap.ssid, ap_ssid, ssid_length);
	ap_config.ap.ssid_len = (uint8_t)ssid_length;
	memcpy(ap_config.ap.password, ap_password, password_length);
	ap_config.ap.channel = NET_AP_CHANNEL;
	ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
	ap_config.ap.max_connection = NET_AP_CLIENTS;
	// The driver takes the configuration of an interface only while the mode has that interface. It is
	// given once before the start, so that the access point does not open with the driver's own (see
	// access_point()), and the display starts as a station: the link orders the access point if it wants it.
	ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "WiFi mode");
	ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "access point");
	ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "WiFi mode");

	// The display answers to WEB_HOST_NAME.local on both interfaces. Without mDNS it still works by its
	// address, and the adapter is found where the link needs no query: no reason not to start.
	err = mdns_init();
	if(err == ESP_OK)
	{
		err = mdns_hostname_set(WEB_HOST_NAME);
	}
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "mDNS: %s", esp_err_to_name(err));
	}

	ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "WiFi start");
	// The station does not sleep between the beacons of its network (the default of the driver is
	// WIFI_PS_MIN_MODEM). Measured on the board on 2026-10-09 with the default: of 150 pings to the display,
	// five a second, 51 % were lost and the others took 154 ms on average, while the adapter in the same
	// network lost none of 100 (5 ms); requests to the adapter ran into their 4 s in phases although it
	// answered the PC every time, and the web interface of the display did not answer for seconds. With
	// the own access point switched on, which keeps the station awake, the same pings lost none of 150 and
	// the adapter was answered twice a second without a failure. The display hangs on the supply of the
	// vehicle: there is nothing to save.
	// CHECK: with this line the same pings lose (nearly) none, the count of answers on the info page grows
	// by about two a second with the ignition on, and the chip temperature there stays below the 75 degrees
	// where the heat rule begins - the radio now listens all the time.
	ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "WiFi power save");

	platform_lock();
	strlcpy(platform_info.ap_ssid, ap_ssid, sizeof(platform_info.ap_ssid));
	strlcpy(platform_info.ap_password, ap_password, sizeof(platform_info.ap_password));
	info_fill();
	platform_unlock();

	ESP_RETURN_ON_FALSE(xTaskCreate(net_task, "net", NET_STACK_BYTES, NULL, NET_PRIORITY, NULL) == pdPASS,
	                    ESP_ERR_NO_MEM, TAG, "no memory for the task");
	return ESP_OK;
}
