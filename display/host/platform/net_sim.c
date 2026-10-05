/*
 * Runs display/main/net.c on a PC: the file is included as it is, and its task runs against the real core
 * and a world made of stand-ins - a WiFi driver with networks in the air, an adapter behind the HTTP
 * client, an answer to the mDNS query. "make" in this directory builds and runs it; redproof.py breaks net.c
 * in one place at a time (mutations/net.py) and expects a check to fail. It runs in this directory: the
 * answers of the adapter are the files of tools/w906/fixtures.
 *
 * What the world does, and what that rests on (ESP-IDF v5.5.2 and espressif/mdns 1.13.1, read, not run):
 *   the driver      a station that is idle, connecting, connected or leaving. A scan and a configuration of
 *                   the station are refused with ESP_ERR_WIFI_STATE while it connects or leaves, as esp_wifi.h
 *                   documents for esp_wifi_scan_start() and esp_wifi_set_config(). An attempt ends with an
 *                   address (IP_EVENT_STA_GOT_IP) or with WIFI_EVENT_STA_DISCONNECTED, a scan with
 *                   WIFI_EVENT_SCAN_DONE, each a moment later. The switches of `drv` make it a driver that
 *                   does what is NOT documented either way, one at a time: an attempt that never ends, a
 *                   disconnect without its event, an access point that forgets its configuration, an order
 *                   that is refused. net.c has to get along with each of them.
 *   the HTTP client the events in the order esp_http_client.c dispatches them (connected once for a
 *                   connection, headers sent, a header, data in pieces); the status is -1 from the moment the
 *                   answer is waited for (esp_http_client_fetch_headers()); a failed request leaves the
 *                   connection as it is. The timeout it was given last is what a step that hangs takes.
 *   the adapter     answers with the files of tools/w906/fixtures, and can be dead, slow, or close the
 *                   connection the client keeps
 *   mDNS            mdns_query_ptr() returns after its timeout without a result, or sooner with one
 *   FreeRTOS        one task, that of net.c. Time passes where it waits, 10 ms a tick, and the world moves
 *                   with it. The task is left at its own wait when the time of a step of the scenario is
 *                   over, and entered again from its top for the next step: net.c keeps nothing in the
 *                   variables of the task function.
 *   the lock        a counter. Taking it twice is a failed check, and so is everything of the world that is
 *                   called while it is held: the driver, the client, the query, a wait (app.h: requests to
 *                   the adapter are sent WITHOUT the lock).
 *   the poll        poll_prepare() and poll_apply() are the real ones, called through a wrapper that looks
 *                   at what net.c hands over
 *
 * What this cannot see, in short: nothing of ESP-IDF runs, so every "the driver does" above is the model.
 * The event loop is the same thread as the task, so an event never arrives in the middle of a function.
 * No radio, no lwIP, no TLS, no real time.
 */
#include <setjmp.h>
#include <stdint.h>
#include <stdbool.h>
#include "sim.h"

// net.c calls these two of the core; here it calls the wrappers further down instead, which look at what
// it hands over and call the real ones
#include "poll.h"

static bool sim_poll_prepare(poll_t *poll, uint64_t now_ms, poll_request_t *request);
static void sim_poll_apply(poll_t *poll, const poll_request_t *request, int status, const char *body, size_t length,
                           const char *seq_header, uint64_t now_ms, json_token_t *work, int work_count);

#define poll_prepare sim_poll_prepare
#define poll_apply sim_poll_apply
#include "net.c"
#undef poll_prepare
#undef poll_apply

#define FIXTURES    "../../../tools/w906/fixtures"

/* The rest of the platform ----------------------------------------------------------------------------- */

app_t *platform_app;
platform_info_t platform_info;

static int locked;
static uint64_t sim_ms = 1000;
static uint32_t events_seen;
static bool events_owed;        // the app may have raised something that platform_events() has not taken
static int events_missed;       // ... and the lock was given back like that
static int stored_calls;
static bool stored_since;       // platform_stored() was called since the last answer was applied
static bool stored_first = true;    // no request ever left before platform_stored() was called for it

void platform_lock(void)
{
	CHECK(locked == 0);
	locked = 1;
}

void platform_unlock(void)
{
	CHECK(locked == 1);
	locked = 0;
	if(events_owed)
	{
		events_missed++;
		events_owed = false;
	}
}

uint64_t platform_now_ms(void)
{
	return sim_ms;
}

void platform_events(void)
{
	CHECK(locked == 1);
	events_owed = false;
	events_seen |= app_take_events(platform_app);
}

void platform_stored(void)
{
	CHECK(locked == 0);
	stored_calls++;
	stored_since = true;
}

const char *esp_err_to_name(esp_err_t code)
{
	(void)code;
	return "ERR";
}

void *heap_caps_malloc(size_t size, uint32_t caps)
{
	// The room of net.c is too large for the internal RAM
	CHECK(caps == MALLOC_CAP_SPIRAM);
	return sim_alloc(size);
}

/* The world: WiFi -------------------------------------------------------------------------------------- */

typedef struct
{
	const char *ssid;
	const char *password;
	uint8_t ip[4];
	uint8_t gw[4];
} air_t;

static air_t air[4];
static int air_count;

ESP_EVENT_DEFINE_BASE(WIFI_EVENT);
ESP_EVENT_DEFINE_BASE(IP_EVENT);

static esp_event_handler_t wifi_handler;
static esp_event_handler_t ip_handler;

typedef enum
{
	ST_IDLE,
	ST_CONNECTING,
	ST_CONNECTED,
	ST_LEAVING,
} station_t;

static struct
{
	wifi_mode_t mode;
	bool started;
	wifi_config_t sta;
	wifi_config_t ap;
	bool ap_given;              // the access point has the configuration of net.c
	bool ap_open_window;        // the access point was on in a mode change before it had its configuration
	station_t state;
	uint64_t leave_done_ms;     // the driver reports the end of a connection a moment after it was told to end it
	const air_t *network;       // being joined or joined
	uint64_t connect_done_ms;
	bool scanning;
	uint64_t scan_done_ms;
	int scans, connects, disconnects, mode_changes;
	uint64_t last_disconnect_ms;
	int refused;                // asked for a scan or a network while the station was connecting or leaving
	int ap_refusals;            // orders for the access point that were refused
	int nvs_enable;             // as esp_wifi_init() was told
	int ap_stations;
	// A driver that does what is not written down, one switch at a time
	bool join_silent;           // an attempt never ends by itself
	bool quiet_abort;           // a disconnect while connecting brings no event
	bool scan_silent;           // a scan never reports its end
	bool forgets_ap;            // the configuration of the access point is gone when it closes
	bool refuse_connect;
	bool stale_ip;              // the address of the last network is still handed out when it is left
	bool ip_races;              // an address is reported just while the station is told to leave
	bool ignores_abort;         // a disconnect while connecting does nothing
	bool ap_refused_busy;       // no configuration of the access point is taken while the station connects
	int mode_refusals;          // so many changes of the mode are refused
	uint64_t slow_join_ms;      // how long an attempt takes, 0 = as usual
} drv;

static EventBits_t group;

static void post_wifi(int32_t id)
{
	wifi_handler(NULL, WIFI_EVENT, id, NULL);
}

static void post_ip(void)
{
	ip_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
}

// What the driver does by itself as time passes
static void world(void)
{
	if(drv.scanning && !drv.scan_silent && sim_ms >= drv.scan_done_ms)
	{
		drv.scanning = false;
		post_wifi(WIFI_EVENT_SCAN_DONE);
	}
	if(drv.state == ST_LEAVING && sim_ms >= drv.leave_done_ms)
	{
		drv.state = ST_IDLE;
		post_wifi(WIFI_EVENT_STA_DISCONNECTED);
	}
	if(drv.state == ST_CONNECTING && !drv.join_silent && sim_ms >= drv.connect_done_ms)
	{
		if(drv.network != NULL)
		{
			drv.state = ST_CONNECTED;
			post_ip();
		}
		else
		{
			drv.state = ST_IDLE;
			post_wifi(WIFI_EVENT_STA_DISCONNECTED);
		}
	}
}

/* FreeRTOS */

static jmp_buf out;
static uint64_t run_until_ms;
static int turns;
static TaskFunction_t the_task;

EventGroupHandle_t xEventGroupCreate(void)
{
	return (EventGroupHandle_t)&group;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t handle, const EventBits_t bits)
{
	(void)handle;
	group |= bits;
	return group;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t handle, const EventBits_t bits)
{
	EventBits_t before = group;

	(void)handle;
	group &= ~bits;
	return before;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t handle, const EventBits_t wait, const BaseType_t clear,
                                const BaseType_t all, TickType_t ticks)
{
	EventBits_t got;

	(void)handle;
	// A wait is a wait: never under the lock
	CHECK(locked == 0);
	// Bits that are cleared when they are returned, and any one of them: the only wait this stand-in knows
	NEED(clear == pdTRUE && all == pdFALSE);
	if(wait == NET_EVENT_ALL)
	{
		// The wait of the task itself: a turn is over, and so may be the step of the scenario
		turns++;
		if(sim_ms >= run_until_ms)
		{
			longjmp(out, 1);
		}
	}
	world();
	if(ticks == 0)
	{
		// Even a turn without a wait takes time
		sim_ms += 1;
	}
	for(TickType_t tick = 0; tick < ticks && (group & wait) == 0; tick++)
	{
		sim_ms += 1000 / configTICK_RATE_HZ;
		world();
	}
	got = group;
	group &= ~wait;
	return got;
}

void vTaskDelay(const TickType_t ticks)
{
	CHECK(locked == 0);
	sim_ms += (uint64_t)ticks * (1000 / configTICK_RATE_HZ);
	world();
}

BaseType_t xTaskCreate(TaskFunction_t code, const char *const name, const uint32_t stack, void *const arg,
                       UBaseType_t priority, TaskHandle_t *const created)
{
	(void)name;
	(void)stack;
	(void)arg;
	(void)priority;
	(void)created;
	the_task = code;
	return pdPASS;
}

/* The event loop and the interfaces */

static int sta_object;
static int ap_object;

esp_err_t esp_netif_init(void)
{
	return ESP_OK;
}

// As if another part of the platform had made the loop already: net.c has to take that for success
esp_err_t esp_event_loop_create_default(void)
{
	return ESP_ERR_INVALID_STATE;
}

esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg)
{
	(void)id;
	(void)arg;
	if(base == WIFI_EVENT)
	{
		wifi_handler = handler;
	}
	else
	{
		ip_handler = handler;
	}
	return ESP_OK;
}

esp_netif_t *esp_netif_create_default_wifi_sta(void)
{
	return (esp_netif_t *)&sta_object;
}

esp_netif_t *esp_netif_create_default_wifi_ap(void)
{
	return (esp_netif_t *)&ap_object;
}

static uint32_t address(const uint8_t bytes[4])
{
	uint32_t value;

	memcpy(&value, bytes, 4);
	return value;
}

esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info)
{
	static const uint8_t own[4] = { 192, 168, 4, 1 };

	memset(info, 0, sizeof(*info));
	if(netif == (esp_netif_t *)&ap_object)
	{
		info->ip.addr = address(own);
	}
	else if((drv.state == ST_CONNECTED || drv.stale_ip) && drv.network != NULL)
	{
		info->ip.addr = address(drv.network->ip);
		info->gw.addr = address(drv.network->gw);
	}
	return ESP_OK;
}

char *esp_ip4addr_ntoa(const esp_ip4_addr_t *addr, char *buf, int buflen)
{
	uint8_t bytes[4];

	memcpy(bytes, &addr->addr, 4);
	snprintf(buf, (size_t)buflen, "%d.%d.%d.%d", bytes[0], bytes[1], bytes[2], bytes[3]);
	return buf;
}

/* The driver */

esp_err_t esp_wifi_init(const wifi_init_config_t *config)
{
	drv.nvs_enable = config->nvs_enable;
	return ESP_OK;
}

esp_err_t esp_wifi_set_storage(wifi_storage_t storage)
{
	// The networks are in the store of the display: the driver writes none into the flash
	CHECK(storage == WIFI_STORAGE_RAM);
	return ESP_OK;
}

esp_err_t esp_wifi_start(void)
{
	drv.started = true;
	return ESP_OK;
}

esp_err_t esp_wifi_set_mode(wifi_mode_t mode)
{
	if(drv.mode_refusals > 0)
	{
		drv.mode_refusals--;
		drv.ap_refusals++;
		return ESP_FAIL;
	}
	if(drv.started && mode == WIFI_MODE_APSTA && !drv.ap_given)
	{
		drv.ap_open_window = true;
	}
	if(drv.started && mode == WIFI_MODE_STA && drv.forgets_ap)
	{
		drv.ap_given = false;
	}
	drv.mode = mode;
	drv.mode_changes++;
	return ESP_OK;
}

esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf)
{
	CHECK(locked == 0);
	if(interface == WIFI_IF_AP)
	{
		// The driver takes the configuration of an interface only while the mode has it
		if(!CHECK(drv.mode == WIFI_MODE_APSTA))
		{
			return ESP_FAIL;
		}
		if(drv.ap_refused_busy && drv.state == ST_CONNECTING)
		{
			drv.ap_refusals++;
			return ESP_ERR_WIFI_STATE;
		}
		drv.ap = *conf;
		drv.ap_given = true;
		return ESP_OK;
	}
	if(drv.state == ST_CONNECTING || drv.state == ST_LEAVING)
	{
		drv.refused++;
		return ESP_ERR_WIFI_STATE;
	}
	drv.sta = *conf;
	return ESP_OK;
}

esp_err_t esp_wifi_connect(void)
{
	char ssid[33] = "";
	char password[65] = "";

	CHECK(locked == 0);
	// net.c ends what the station does before it orders something new
	if(!CHECK(drv.state == ST_IDLE))
	{
		drv.refused++;
		return ESP_ERR_WIFI_STATE;
	}
	if(drv.refuse_connect)
	{
		return ESP_FAIL;
	}
	memcpy(ssid, drv.sta.sta.ssid, 32);
	memcpy(password, drv.sta.sta.password, 64);
	drv.connects++;
	drv.state = ST_CONNECTING;
	drv.network = NULL;
	drv.connect_done_ms = sim_ms + 1500;
	for(int i = 0; i < air_count; i++)
	{
		if(strcmp(air[i].ssid, ssid) == 0 && strcmp(air[i].password, password) == 0)
		{
			drv.network = &air[i];
			drv.connect_done_ms = sim_ms + (drv.slow_join_ms != 0 ? drv.slow_join_ms : 800);
		}
	}
	return ESP_OK;
}

esp_err_t esp_wifi_disconnect(void)
{
	CHECK(locked == 0);
	drv.disconnects++;
	drv.last_disconnect_ms = sim_ms;
	if(drv.state == ST_CONNECTING && drv.ignores_abort)
	{
		return ESP_OK;
	}
	if(drv.ip_races)
	{
		post_ip();
	}
	if(drv.state == ST_CONNECTED || (drv.state == ST_CONNECTING && !drv.quiet_abort))
	{
		drv.state = ST_LEAVING;
		drv.leave_done_ms = sim_ms + 50;
	}
	else if(drv.state != ST_LEAVING)
	{
		drv.state = ST_IDLE;
	}
	return ESP_OK;
}

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block)
{
	CHECK(locked == 0);
	// All channels, and no wait for the end: the task has other things to do meanwhile
	CHECK(config == NULL && !block);
	if(drv.state == ST_CONNECTING || drv.state == ST_LEAVING)
	{
		drv.refused++;
		return ESP_ERR_WIFI_STATE;
	}
	drv.scans++;
	drv.scanning = true;
	drv.scan_done_ms = sim_ms + 2000;
	return ESP_OK;
}

esp_err_t esp_wifi_scan_stop(void)
{
	if(drv.scanning)
	{
		post_wifi(WIFI_EVENT_SCAN_DONE);
	}
	drv.scanning = false;
	return ESP_OK;
}

esp_err_t esp_wifi_clear_ap_list(void)
{
	return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *records)
{
	uint16_t count = 0;

	for(int i = 0; i < air_count && count < *number; i++, count++)
	{
		memset(&records[count], 0, sizeof(records[count]));
		snprintf((char *)records[count].ssid, sizeof(records[count].ssid), "%s", air[i].ssid);
		records[count].rssi = (int8_t)(-50 - i);
		records[count].authmode = air[i].password[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
	}
	*number = count;
	return ESP_OK;
}

esp_err_t esp_wifi_sta_get_rssi(int *rssi)
{
	if(drv.state != ST_CONNECTED)
	{
		return ESP_FAIL;
	}
	*rssi = -61;
	return ESP_OK;
}

esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *list)
{
	list->num = drv.ap_stations;
	return ESP_OK;
}

/* The world: the adapter ------------------------------------------------------------------------------- */

static struct
{
	const char *ip;             // where it answers
	bool dead;                  // no answer at all
	bool closes;                // it has closed the connection the client keeps
	bool huge;                  // its values have no room
	int requests, posts, gets_with_header, posts_without_header, on_kept, connections;
	char last_path[128];
	char last_host[64];
	uint64_t slowest_ms;
	uint64_t closes_after_ms;   // ... and only after this long, and is dead from then on
	bool half_dead;             // it takes the connection after 1.5 s and never answers
	int breaks;                 // so many answers end in the middle of their body
	const char *mdns_ip;        // what the query finds, NULL = nothing
	int queries;
} wican;

#define FILE_ROOM   20000
#define HUGE_ROOM   40000       // more than POLL_BODY_SIZE

// A file of tools/w906/fixtures as a text, in a room large enough for the huge answer as well
static char *file(const char *name)
{
	char path[512];
	char *text = calloc(1, HUGE_ROOM);
	size_t length;
	FILE *source;

	NEED(text != NULL);
	snprintf(path, sizeof(path), FIXTURES "/%s", name);
	source = fopen(path, "rb");
	NEED(source != NULL);
	length = fread(text, 1, FILE_ROOM, source);
	fclose(source);
	NEED(length > 0 && length < FILE_ROOM);
	return text;
}

struct esp_http_client
{
	char host[64];
	char path[128];
	esp_http_client_method_t method;
	bool dtc_header;
	http_event_handle_cb handler;
	bool connected;
	int timeout_ms;
	int status;
};

static int clients_made;

static void dispatch(esp_http_client_handle_t client, esp_http_client_event_id_t id, const char *key, const char *value,
                     const char *data, int length)
{
	esp_http_client_event_t event =
	{
		.event_id = id,
		.client = client,
		.header_key = (char *)key,
		.header_value = (char *)value,
		.data = (void *)data,
		.data_len = length,
	};

	// The handler runs inside esp_http_client_perform(), which is called without the lock
	CHECK(locked == 0);
	client->handler(&event);
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
	esp_http_client_handle_t client;

	// Host and path apart, never a URL: the URL parser of ESP-IDF refuses a name with an underscore, and
	// "wican_<id>.local" is what an adapter is called. And a request is only sent while the address is
	// known (app.h).
	if(!CHECK(config->host != NULL && config->path != NULL && config->url == NULL) || !CHECK(config->host[0] != '\0'))
	{
		return NULL;
	}
	// A request is sent once: the client must not send it again by itself (a clear must never go out twice)
	CHECK(config->disable_auto_redirect && config->max_authorization_retries == -1);
	client = calloc(1, sizeof(*client));
	NEED(client != NULL);
	snprintf(client->host, sizeof(client->host), "%s", config->host);
	client->handler = config->event_handler;
	client->timeout_ms = config->timeout_ms;
	clients_made++;
	return client;
}

esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char *url)
{
	// A path and nothing else: with a host in it the real client would parse that host strictly
	if(url[0] != '/')
	{
		return ESP_ERR_INVALID_ARG;
	}
	snprintf(client->path, sizeof(client->path), "%s", url);
	return ESP_OK;
}

esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method)
{
	client->method = method;
	return ESP_OK;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value)
{
	// The one header of tools/w906/API.md; a value of NULL takes it off again
	CHECK(strcmp(key, POLL_HEADER_NAME) == 0);
	CHECK(value == NULL || strcmp(value, POLL_HEADER_VALUE) == 0);
	client->dtc_header = value != NULL;
	return ESP_OK;
}

esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client, int timeout_ms)
{
	// Never below 1 (the transport takes -1 for "no limit") and never more than the time of a request
	CHECK(timeout_ms >= 1 && timeout_ms <= (int)POLL_TIMEOUT_MS);
	client->timeout_ms = timeout_ms;
	return ESP_OK;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client)
{
	return client->status;
}

esp_err_t esp_http_client_close(esp_http_client_handle_t client)
{
	if(client->connected)
	{
		client->connected = false;
		dispatch(client, HTTP_EVENT_DISCONNECTED, NULL, NULL, NULL, 0);
	}
	return ESP_OK;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{
	esp_http_client_close(client);
	free(client);
	return ESP_OK;
}

static void slowest(uint64_t begin_ms)
{
	if(sim_ms - begin_ms > wican.slowest_ms)
	{
		wican.slowest_ms = sim_ms - begin_ms;
	}
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client)
{
	uint64_t begin_ms = sim_ms;
	const char *body = "";
	const char *seq = NULL;
	char *loaded = NULL;
	bool post = client->method == HTTP_METHOD_POST;
	size_t length;

	// app.h: requests to the adapter are sent WITHOUT the lock
	CHECK(locked == 0);
	if(!stored_since)
	{
		stored_first = false;
	}

	// It takes the connection late and never answers
	if(drv.state == ST_CONNECTED && wican.half_dead && strcmp(client->host, wican.ip) == 0)
	{
		if(!client->connected)
		{
			sim_ms += 1500;
			client->connected = true;
			dispatch(client, HTTP_EVENT_ON_CONNECTED, NULL, NULL, NULL, 0);
		}
		dispatch(client, HTTP_EVENT_HEADERS_SENT, NULL, NULL, NULL, 0);
		client->status = -1;
		// The read runs into the timeout the client has at that moment
		sim_ms += (uint64_t)client->timeout_ms;
		return ESP_FAIL;
	}
	// The address has to be reachable from where the station is
	if(drv.state != ST_CONNECTED || wican.dead || strcmp(client->host, wican.ip) != 0)
	{
		// Connecting runs into the timeout the client was given
		sim_ms += (uint64_t)client->timeout_ms;
		slowest(begin_ms);
		return ESP_FAIL;
	}
	if(client->connected && wican.closes)
	{
		// The request goes out, and nothing comes back but the end of the connection
		wican.closes = false;
		wican.on_kept++;
		dispatch(client, HTTP_EVENT_HEADERS_SENT, NULL, NULL, NULL, 0);
		client->status = -1;
		if(wican.closes_after_ms != 0)
		{
			sim_ms += wican.closes_after_ms;
			wican.closes_after_ms = 0;
			wican.dead = true;
		}
		return ESP_FAIL;
	}
	if(!client->connected)
	{
		client->connected = true;
		wican.closes = false;
		wican.connections++;
		dispatch(client, HTTP_EVENT_ON_CONNECTED, NULL, NULL, NULL, 0);
	}
	dispatch(client, HTTP_EVENT_HEADERS_SENT, NULL, NULL, NULL, 0);
	client->status = -1;
	sim_ms += 30;

	wican.requests++;
	snprintf(wican.last_path, sizeof(wican.last_path), "%s", client->path);
	snprintf(wican.last_host, sizeof(wican.last_host), "%s", client->host);
	if(post)
	{
		wican.posts++;
	}
	if(post && !client->dtc_header)
	{
		wican.posts_without_header++;
	}
	if(!post && client->dtc_header)
	{
		wican.gets_with_header++;
	}

	client->status = 200;
	if(strcmp(client->path, "/api/state") == 0)
	{
		// As the adapter does: the counter of its polling loop moves, and it has been up for a while
		static int pass = 1234;
		char digits[16];
		char *at;

		body = loaded = file("api_state_example.json");
		at = strstr(loaded, "\"pass\":1234");
		NEED(at != NULL);
		pass = pass >= 9000 ? 1235 : pass + 14;
		snprintf(digits, sizeof(digits), "%04d", pass);
		memcpy(at + 7, digits, 4);
	}
	else if(strcmp(client->path, "/load_car_config") == 0)
	{
		body = loaded = file("car_config_w906.json");
	}
	else if(strcmp(client->path, "/autopid_data") == 0)
	{
		body = loaded = file("autopid_data_ignition_on.json");
		if(wican.huge)
		{
			// More than the room of the display holds
			memset(loaded, ' ', HUGE_ROOM - 1);
			loaded[HUGE_ROOM - 1] = '\0';
			loaded[0] = '{';
			loaded[HUGE_ROOM - 2] = '}';
		}
	}
	else if(strcmp(client->path, "/api/dtc/result") == 0)
	{
		client->status = 204;
	}
	else if(strncmp(client->path, "/api/dtc?action=read", 20) == 0 && post)
	{
		client->status = 202;
		body = "{\"accepted\":true,\"seq\":43}";
	}
	else
	{
		client->status = 404;
	}

	dispatch(client, HTTP_EVENT_ON_HEADER, "Content-Type", "application/json", NULL, 0);
	if(wican.breaks > 0 && strlen(body) > 20)
	{
		wican.breaks--;
		dispatch(client, HTTP_EVENT_ON_DATA, NULL, NULL, body, 20);
		free(loaded);
		return ESP_FAIL;
	}
	if(strcmp(client->path, "/autopid_data") == 0)
	{
		// A header nobody asked for, in small letters
		seq = "77";
	}
	if(seq != NULL)
	{
		dispatch(client, HTTP_EVENT_ON_HEADER, "x-dtc-seq", seq, NULL, 0);
	}
	length = strlen(body);
	for(size_t at = 0; at < length; at += 512)
	{
		dispatch(client, HTTP_EVENT_ON_DATA, NULL, NULL, body + at, (int)(length - at < 512 ? length - at : 512));
	}
	free(loaded);
	slowest(begin_ms);
	return ESP_OK;
}

/* mDNS */

static int mdns_started;
static char mdns_name[64];
static int results_freed;

esp_err_t mdns_init(void)
{
	mdns_started++;
	return ESP_OK;
}

esp_err_t mdns_hostname_set(const char *hostname)
{
	snprintf(mdns_name, sizeof(mdns_name), "%s", hostname);
	return ESP_OK;
}

void mdns_query_results_free(mdns_result_t *results)
{
	results_freed++;
	if(results != NULL)
	{
		free(results->addr);
		free(results);
	}
}

esp_err_t mdns_query_ptr(const char *service, const char *proto, uint32_t timeout, size_t max_results, mdns_result_t **results)
{
	unsigned a, b, c, d;
	uint8_t bytes[4];

	CHECK(locked == 0);
	// What the adapter announces (main/wc_mdns.c), and the first that answers
	CHECK(strcmp(service, "_wican") == 0 && strcmp(proto, "_tcp") == 0 && max_results == 1);
	wican.queries++;
	*results = NULL;
	if(wican.mdns_ip == NULL || drv.state != ST_CONNECTED)
	{
		sim_ms += timeout;
		return ESP_OK;
	}
	sim_ms += 120;
	*results = calloc(1, sizeof(mdns_result_t));
	NEED(*results != NULL);
	(*results)->addr = calloc(1, sizeof(mdns_ip_addr_t));
	NEED((*results)->addr != NULL);
	(*results)->addr->addr.type = ESP_IPADDR_TYPE_V4;
	NEED(sscanf(wican.mdns_ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4);
	bytes[0] = (uint8_t)a;
	bytes[1] = (uint8_t)b;
	bytes[2] = (uint8_t)c;
	bytes[3] = (uint8_t)d;
	(*results)->addr->addr.u_addr.ip4.addr = address(bytes);
	return ESP_OK;
}

/* What net.c hands to the poll ------------------------------------------------------------------------- */

static uint64_t prepared_ms;
static uint64_t longest_ms;     // from a request handed out to its answer applied

static struct
{
	int calls, bad_text, no_answer, empty_values_200, values_with_seq, state_with_seq, bad_work;
} applied;

static bool sim_poll_prepare(poll_t *poll, uint64_t now_ms, poll_request_t *request)
{
	bool due = poll_prepare(poll, now_ms, request);

	CHECK(locked == 1);
	if(due)
	{
		prepared_ms = sim_ms;
		events_owed = true;
	}
	return due;
}

static void sim_poll_apply(poll_t *poll, const poll_request_t *request, int status, const char *body, size_t length,
                           const char *seq_header, uint64_t now_ms, json_token_t *work, int work_count)
{
	CHECK(locked == 1);
	stored_since = false;
	events_owed = true;
	applied.calls++;
	if(sim_ms - prepared_ms > longest_ms)
	{
		longest_ms = sim_ms - prepared_ms;
	}
	// The body is a text that ends where its length says (poll.h), in the room for the tokens of the poll
	if(body == NULL || strlen(body) != length)
	{
		applied.bad_text++;
	}
	if(work == NULL || work_count != POLL_TOKENS)
	{
		applied.bad_work++;
	}
	if(status == 0)
	{
		applied.no_answer++;
		if(length != 0)
		{
			applied.bad_text++;
		}
	}
	if(request->kind == POLL_VALUES && status == 200 && length == 0)
	{
		applied.empty_values_200++;
	}
	if(request->kind == POLL_VALUES && seq_header != NULL && strcmp(seq_header, "77") == 0)
	{
		applied.values_with_seq++;
	}
	if(request->kind == POLL_STATE && seq_header != NULL)
	{
		applied.state_with_seq++;
	}
	if(body != NULL)
	{
		poll_apply(poll, request, status, body, length, seq_header, now_ms, work, work_count);
	}
}

/* The scenarios ---------------------------------------------------------------------------------------- */

static void state(void)
{
	printf("   scans %d connects %d mode %d station %d ssid '%s' ip '%s' rssi %d posts %d connections %d reconnects %u phase %d\n",
	       drv.scans, drv.connects, (int)drv.mode, (int)drv.state, platform_info.ssid, platform_info.ip, platform_info.rssi,
	       wican.posts, wican.connections, (unsigned)platform_info.reconnects, (int)platform_app->link.phase);
}

// Lets the task run for `ms` of the time of the world
static void run(uint64_t ms)
{
	run_until_ms = sim_ms + ms;
	if(setjmp(out) == 0)
	{
		the_task(NULL);
	}
	CHECK(locked == 0);
}

// A start of the app with these networks stored. reuse: the same display, started again - the app is new,
// net.c and the driver are as they were, but for the access point, which is closed after net_start().
static void start(const net_profile_t *profiles, int count, bool reuse)
{
	static json_token_t work[LAYOUT_TOKENS];
	app_boot_t boot;

	if(!reuse)
	{
		platform_app = sim_alloc(sizeof(app_t));
	}
	else
	{
		ap_on = false;
		drv.mode = WIFI_MODE_STA;
	}
	memset(&boot, 0, sizeof(boot));
	boot.version = "0.1.0";
	boot.git = "sim";
	boot.profiles = profiles;
	boot.profile_count = count;
	boot.work = work;
	boot.work_count = LAYOUT_TOKENS;
	app_init(platform_app, &boot, sim_ms);
}

static void test_access_point(void)
{
	/* No network stored: the own access point */
	start(NULL, 0, false);
	sim_state = state;
	CHECK(net_start(NULL, "12345678") == ESP_ERR_INVALID_ARG);
	CHECK(net_start("WiCAN-Display-1234", "1234567") == ESP_ERR_INVALID_ARG);
	CHECK(net_start("WiCAN-Display-1234", "geheimes-passwort") == ESP_OK);
	CHECK(net_start("WiCAN-Display-1234", "geheimes-passwort") == ESP_ERR_INVALID_STATE);
	CHECK(drv.started && drv.mode == WIFI_MODE_STA && mdns_started == 1 && strcmp(mdns_name, "wican-display") == 0);
	// The driver gets no NVS of its own: the display starts without a store as well
	CHECK(drv.nvs_enable == 0);
	CHECK(strcmp(platform_info.ap_ssid, "WiCAN-Display-1234") == 0 && strcmp(platform_info.ap_password, "geheimes-passwort") == 0);
	run(1000);
	CHECK(drv.mode == WIFI_MODE_APSTA && !drv.ap_open_window && drv.scans == 0 && link_ap_on(&platform_app->link));
	CHECK(memcmp(drv.ap.ap.ssid, "WiCAN-Display-1234", 18) == 0 && drv.ap.ap.ssid_len == 18 &&
	      drv.ap.ap.authmode == WIFI_AUTH_WPA2_PSK);
	CHECK(strcmp((char *)drv.ap.ap.password, "geheimes-passwort") == 0);
	CHECK(strcmp(platform_info.ip, "192.168.4.1") == 0 && platform_info.ssid[0] == '\0');
	// It waits: about ten turns a second
	CHECK(turns >= 8 && turns <= 12);
	drv.ap_stations = 1;
	post_wifi(WIFI_EVENT_AP_STACONNECTED);
	run(300);
	CHECK(platform_app->link.ap_clients == 1);
}

static void test_adapter(void)
{
	static net_profile_t home[1] = { { "Werkstatt", "geheim1234", "192.168.1.50" } };
	uint32_t ok_before;
	uint32_t failed_before;
	int before;

	/* A network with the address of the adapter */
	start(home, 1, true);
	run(200);
	CHECK(drv.scans == 1 && drv.mode == WIFI_MODE_STA);
	run(4000);
	CHECK(drv.connects == 1 && drv.state == ST_CONNECTED && link_up(&platform_app->link));
	CHECK(platform_app->seen_count == 3 && strcmp(platform_app->seen[1].ssid, "Nachbar") == 0 &&
	      !platform_app->seen[1].secure && platform_app->seen[0].secure);
	CHECK(strcmp(platform_info.ssid, "Werkstatt") == 0 && strcmp(platform_info.ip, "192.168.1.77") == 0 &&
	      platform_info.rssi == -61);
	run(5000);
	CHECK(wican.requests >= 8 && wican.connections == 1 && platform_app->poll.http_ok == (uint32_t)wican.requests &&
	      platform_app->poll.http_failed == 0);
	CHECK(strcmp(wican.last_host, "192.168.1.50") == 0 && wican.gets_with_header == 0 && clients_made == 1);
	CHECK(conn_view(&platform_app->poll.conn, sim_ms) == CONN_VIEW_LIVE && platform_app->poll.catalog_complete);
	CHECK(strcmp(platform_app->poll.bound_id, "a1b2c3d4e5f6") == 0 && (events_seen & APP_EVENT_STORE_BOUND));
	CHECK(platform_info.reconnects == 0);
	CHECK(wican.slowest_ms <= 40);
	CHECK(applied.calls == wican.requests && applied.bad_text == 0 && applied.bad_work == 0 && applied.no_answer == 0);
	CHECK(applied.values_with_seq >= 2 && applied.state_with_seq == 0 && applied.empty_values_200 == 0);

	// The adapter closes the connection: the GET goes once more, nothing is lost
	ok_before = platform_app->poll.http_ok;
	before = wican.requests;
	wican.closes = true;
	run(3000);
	CHECK(wican.on_kept == 1 && wican.connections == 2 && platform_info.reconnects == 1 && platform_app->poll.http_failed == 0);
	CHECK(platform_app->poll.http_ok - ok_before == (uint32_t)(wican.requests - before) && wican.requests - before >= 4);

	// A fault memory read: one POST, over a connection of its own, with the header; the GETs behind it without.
	// The adapter has to answer for a while before a command goes out (conn.h).
	run(15000);
	before = wican.connections;
	platform_lock();
	CHECK(poll_read(&platform_app->poll, sim_ms) == 0);
	platform_unlock();
	run(3000);
	CHECK(wican.posts == 1 && wican.posts_without_header == 0 && wican.gets_with_header == 0 && wican.connections == before + 1);
	CHECK(platform_info.reconnects == 1 && platform_app->poll.http_failed == 0);

	platform_lock();
	poll_dismiss(&platform_app->poll);
	platform_unlock();
	run(20000);

	// Values that have no room: the status that came, an empty body, and the display goes on
	failed_before = platform_app->poll.http_failed;
	ok_before = platform_app->poll.http_ok;
	wican.huge = true;
	run(3000);
	wican.huge = false;
	CHECK(platform_app->poll.http_failed == failed_before && platform_app->poll.http_ok > ok_before);
	CHECK(applied.empty_values_200 >= 1 && applied.bad_text == 0);
	run(5000);

	// The adapter falls silent: no answer is status 0, and a request never takes longer than its time
	wican.dead = true;
	wican.slowest_ms = 0;
	failed_before = platform_app->poll.http_failed;
	run(20000);
	CHECK(platform_app->poll.http_failed > failed_before && wican.slowest_ms <= POLL_TIMEOUT_MS);
	CHECK(conn_view(&platform_app->poll.conn, sim_ms) == CONN_VIEW_NO_ANSWER);
	CHECK(applied.no_answer >= 2 && applied.bad_text == 0 && longest_ms <= POLL_TIMEOUT_MS + 10);
	wican.dead = false;
	run(15000);
	CHECK(conn_view(&platform_app->poll.conn, sim_ms) == CONN_VIEW_LIVE);

	// An answer that ends in the middle, twice in a row: no answer, and nothing of the half body is passed on
	before = applied.no_answer;
	wican.breaks = 2;
	run(5000);
	CHECK(wican.breaks == 0 && applied.no_answer == before + 1 && applied.bad_text == 0);
	run(10000);

	// It closes the kept connection only after 3 s, and does not take the new one: what is left of the time
	// of the request is all the second attempt gets
	longest_ms = 0;
	wican.closes = true;
	wican.closes_after_ms = 3000;
	run(12000);
	CHECK(longest_ms >= 3000 && longest_ms <= POLL_TIMEOUT_MS + 10);
	wican.dead = false;
	run(15000);
	// It takes the connection late and never answers: connecting and waiting together stay within the time
	longest_ms = 0;
	wican.half_dead = true;
	client_drop();
	run(12000);
	CHECK(longest_ms >= 1500 && longest_ms <= POLL_TIMEOUT_MS + 10);
	wican.half_dead = false;
	run(15000);
	CHECK(conn_view(&platform_app->poll.conn, sim_ms) == CONN_VIEW_LIVE);

	// The network goes away and comes back
	before = drv.scans;
	drv.state = ST_IDLE;
	air_count = 0;
	post_wifi(WIFI_EVENT_STA_DISCONNECTED);
	run(1000);
	CHECK(!link_up(&platform_app->link) && platform_info.ssid[0] == '\0' && platform_info.ip[0] == '\0' &&
	      platform_info.rssi == 0);
	run(20000);
	CHECK(drv.scans > before + 2 && drv.state == ST_IDLE);
	air_count = 3;
	run(40000);
	CHECK(drv.state == ST_CONNECTED && link_up(&platform_app->link) &&
	      conn_view(&platform_app->poll.conn, sim_ms) != CONN_VIEW_NO_WIFI);
	CHECK(strcmp(platform_info.ssid, "Werkstatt") == 0);
}

// The network is forgotten in the web interface: the display leaves it, after a moment for the answer
static void test_forget(void)
{
	uint64_t asked_ms = sim_ms;
	int disconnects = drv.disconnects;

	// A driver that has kept nothing of the configuration it was given at the start
	drv.forgets_ap = true;
	drv.ap_given = false;
	platform_lock();
	platform_app->profile_count = net_forget(platform_app->profiles, platform_app->profile_count, "Werkstatt");
	link_profiles(&platform_app->link, platform_app->profiles, platform_app->profile_count, sim_ms);
	app_net(platform_app, sim_ms);
	platform_unlock();
	run(1500);
	CHECK(drv.disconnects == disconnects + 1 && drv.state == ST_IDLE && drv.last_disconnect_ms - asked_ms >= NET_LEAVE_WAIT_MS);
	CHECK(drv.last_disconnect_ms - asked_ms <= NET_LEAVE_WAIT_MS + 2 * NET_STEP_MS + POLL_TIMEOUT_MS);
	CHECK(platform_app->link.phase == LINK_IDLE && drv.mode == WIFI_MODE_APSTA && platform_info.ssid[0] == '\0');
	CHECK(strcmp(platform_info.ip, "192.168.4.1") == 0);
	// Whatever the driver kept of it: the access point is open with the name and password of the display
	CHECK(drv.ap_given && memcmp(drv.ap.ap.ssid, "WiCAN-Display-1234", 18) == 0);
	drv.forgets_ap = false;
}

// A network where the adapter has to be found
static void test_find(void)
{
	static net_profile_t camp[2] = { { "Camping", "platz12345", "" }, { "Werkstatt", "falsch1234", "192.168.1.50" } };

	wican.ip = "192.168.8.33";
	wican.mdns_ip = NULL;
	start(camp, 2, true);
	run(8000);
	CHECK(drv.state == ST_CONNECTED && drv.network == &air[2] && !link_up(&platform_app->link) && wican.queries >= 1);
	wican.mdns_ip = "192.168.8.33";
	run(12000);
	CHECK(link_up(&platform_app->link) && strcmp(app_host(platform_app), "192.168.8.33") == 0 && results_freed >= 1);
	run(5000);
	CHECK(strcmp(wican.last_host, "192.168.8.33") == 0 && clients_made == 2 &&
	      conn_view(&platform_app->poll.conn, sim_ms) == CONN_VIEW_LIVE);
}

static void test_driver(void)
{
	static net_profile_t wrong[1] = { { "Werkstatt", "falsch1234", "192.168.1.50" } };
	static net_profile_t again[1] = { { "Werkstatt", "geheim1234", "192.168.1.50" } };
	int disconnects;
	int refused;
	int before;

	/* A wrong password: the attempts fail, the display scans again and again, and never hangs */
	start(wrong, 1, true);
	before = drv.connects;
	run(30000);
	CHECK(drv.connects >= before + 4 && drv.state != ST_CONNECTED && !link_up(&platform_app->link));

	// A driver that does not even begin: the link hears of it at once and goes on scanning
	start(wrong, 1, true);
	drv.refuse_connect = true;
	before = drv.scans;
	run(30000);
	CHECK(drv.scans >= before + 3);
	drv.refuse_connect = false;

	/* A driver that never ends an attempt: the link gives it up, and the next order still gets through */
	wican.ip = "192.168.1.50";
	start(again, 1, true);
	drv.join_silent = true;
	drv.quiet_abort = true;
	before = drv.connects;
	disconnects = drv.disconnects;
	run(40000);
	CHECK(drv.connects >= before + 2 && drv.disconnects >= disconnects + 1);
	drv.join_silent = false;
	drv.quiet_abort = false;
	run(40000);
	CHECK(drv.state == ST_CONNECTED && link_up(&platform_app->link));

	/* A scan that never ends: taken as over */
	drv.state = ST_IDLE;
	drv.scan_silent = true;
	post_wifi(WIFI_EVENT_STA_DISCONNECTED);
	before = drv.scans;
	run(25000);
	// What the driver had found by then is taken: the display is back in its network
	CHECK(drv.scans == before + 1 && !scanning);
	CHECK(drv.state == ST_CONNECTED && link_up(&platform_app->link));
	drv.scan_silent = false;

	// The driver reports an address and the end of the connection in one go: the end counts
	drv.state = ST_IDLE;
	drv.stale_ip = true;
	post_ip();
	post_wifi(WIFI_EVENT_STA_DISCONNECTED);
	run(300);
	CHECK(platform_info.ssid[0] == '\0' && !link_up(&platform_app->link) && !has_ip);
	run(40000);
	CHECK(drv.state == ST_CONNECTED && link_up(&platform_app->link));

	// The list of networks changes, the display leaves - and an address is reported just then: it belongs to
	// what is being left
	drv.ip_races = true;
	platform_lock();
	link_profiles(&platform_app->link, platform_app->profiles, platform_app->profile_count, sim_ms);
	app_net(platform_app, sim_ms);
	platform_unlock();
	run(1200);
	CHECK(platform_info.ssid[0] == '\0' && !has_ip && drv.state == ST_IDLE);
	drv.ip_races = false;
	drv.stale_ip = false;
	run(40000);
	CHECK(drv.state == ST_CONNECTED && link_up(&platform_app->link));

	// A driver that takes 20 s for a join and cannot be talked out of it: the link gives the attempt up, the
	// driver is in the network all the same - and the display still gets back to a state both agree on
	refused = drv.refused;
	drv.state = ST_IDLE;
	post_wifi(WIFI_EVENT_STA_DISCONNECTED);
	drv.ignores_abort = true;
	drv.slow_join_ms = 20000;
	run(30000);
	drv.ignores_abort = false;
	drv.slow_join_ms = 0;
	run(60000);
	CHECK(drv.state == ST_CONNECTED && link_up(&platform_app->link) && has_ip && sta_busy);
	// Such a driver refuses what it is asked meanwhile: not what is counted below
	drv.refused = refused;

	// The address of the adapter is gone between two requests, and the poll has not heard of it yet: the
	// request it hands out is not sent
	platform_lock();
	link_lost(&platform_app->link, sim_ms);
	platform_unlock();
	before = wican.requests;
	run(300);
	CHECK(wican.requests == before);
	run(30000);
	CHECK(drv.state == ST_CONNECTED && link_up(&platform_app->link) && wican.requests > before);

	// Never was the driver asked for a scan or another network while it was still busy with the last one
	CHECK(drv.refused == 0);
	CHECK(applied.bad_text == 0 && applied.bad_work == 0);
	// After every request that was handed out and every answer the events were taken before the lock was given back
	CHECK(events_missed == 0);
}

// The main task watches the task by its turns, and no request leaves before what was raised is stored
static void test_watch_and_store(void)
{
	uint32_t seen = net_turns();

	run(1000);
	CHECK(net_turns() >= seen + 8 && net_turns() <= seen + 40);
	// One call of platform_stored() for every request that was handed out with an address
	CHECK(stored_calls == applied.calls && stored_calls > 100);
	CHECK(stored_first);
}

static void test_access_point_refused(void)
{
	static net_profile_t wrong[1] = { { "Werkstatt", "falsch1234", "192.168.1.50" } };
	int refusals;

	// The access point is ordered while the station is in an attempt at a network that does not take it,
	// and the driver refuses its configuration just then: the order is not lost
	start(wrong, 1, true);
	drv.ap_refused_busy = true;
	// An earlier scenario opened it on purpose
	drv.ap_open_window = false;
	run(3000);
	for(int i = 0; i < 1000 && drv.state != ST_CONNECTING; i++)
	{
		run(50);
	}
	if(!CHECK(drv.state == ST_CONNECTING))
	{
		return;
	}
	refusals = drv.ap_refusals;
	platform_lock();
	link_ap_request(&platform_app->link, true, sim_ms);
	platform_unlock();
	run(300);
	CHECK(drv.ap_refusals == refusals + 1 && drv.mode == WIFI_MODE_STA && link_ap_on(&platform_app->link));
	run(20000);
	CHECK(drv.mode == WIFI_MODE_APSTA && drv.ap_given && strcmp(platform_info.ip, "192.168.4.1") == 0);
	// Given again between two attempts, not in the middle of one, and never open without its configuration
	CHECK(drv.ap_refusals == refusals + 1 && !drv.ap_open_window);
	drv.ap_refused_busy = false;

	// ... and neither is the order to close it
	drv.mode_refusals = 3;
	platform_lock();
	link_ap_request(&platform_app->link, false, sim_ms);
	platform_unlock();
	run(300);
	CHECK(drv.mode == WIFI_MODE_APSTA && !link_ap_on(&platform_app->link));
	run(20000);
	CHECK(drv.mode == WIFI_MODE_STA && drv.mode_refusals == 0);
	drv.mode_refusals = 0;
	CHECK(platform_info.ip[0] == '\0');
}

// The reason the driver gives for the end of a connection reaches the log
static void test_reason(void)
{
	static net_profile_t right[1] = { { "Werkstatt", "geheim1234", "192.168.1.50" } };
	wifi_event_sta_disconnected_t why = { .reason = 201 };

	start(right, 1, true);
	run(40000);
	CHECK(drv.state == ST_CONNECTED && link_up(&platform_app->link));
	sim_log_wanted = "left by the network (reason 201 of the driver)";
	sim_log_found = 0;
	drv.state = ST_IDLE;
	wifi_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &why);
	// The turn that is under way may be one that waits for the adapter: the report is taken behind it
	run(8000);
	CHECK(sim_log_found == 1);
	sim_log_wanted = NULL;
	CHECK(stored_first);
	CHECK(drv.refused == 0 && events_missed == 0);
}

int main(void)
{
	air[0] = (air_t){ "Werkstatt", "geheim1234", { 192, 168, 1, 77 }, { 192, 168, 1, 1 } };
	air[1] = (air_t){ "Nachbar", "", { 10, 0, 0, 5 }, { 10, 0, 0, 1 } };
	air[2] = (air_t){ "Camping", "platz12345", { 192, 168, 8, 20 }, { 192, 168, 8, 1 } };
	air_count = 3;
	wican.ip = "192.168.1.50";

	// One display from its first start on: each scenario finds net.c and the driver as the one before left them
	test_access_point();
	test_adapter();
	test_forget();
	test_find();
	test_driver();
	test_watch_and_store();
	test_access_point_refused();
	test_reason();

	printf("%d turns of the task in %llu s\n", turns, (unsigned long long)(sim_ms / 1000));
	return sim_end();
}
