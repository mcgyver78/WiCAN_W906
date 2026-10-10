/*
 * Runs display/main/web.c on a PC: the file is included as it is, and its handler is called with one request
 * after the other, against the real core and stand-ins for the HTTP server of ESP-IDF and for the flash.
 * "make" in this directory builds and runs it; redproof.py breaks web.c in one place at a time
 * (mutations/web.py) and expects a check to fail. It runs in this directory: the page it compares the
 * answer to GET / with is ../../main/web/index.html, and the compressed page is build/index.html.gz, which
 * the Makefile makes of it as the build of the firmware does. No zlib is linked here: that the compressed
 * file unpacks to exactly the page is checked by the Makefile, with Python's gzip, before the simulations
 * run (display/tools/page_gz.py --check).
 *
 * What the server does here, and what that rests on (ESP-IDF v5.5.2, read, not run):
 *   a request       comes to the one handler web.c registers, with its method, its URI and its headers. A
 *                   header value is copied as httpd_req_get_hdr_value_str() copies it: that of the first
 *                   line with the name, cut where it has no room, and reported as cut only from two bytes
 *                   too many on (httpd_parse.c, the strlcpy() and the comparison behind it).
 *   the length      req->content_len is the server's own reading of Content-Length: 32 bit of what its
 *                   parser counted, and 0 where those are all ones (httpd_parse.c). That many bytes are
 *                   what httpd_req_recv() hands out, never more (httpd_txrx.c).
 *   a body          arrives in pieces of `chunk` bytes, each taking `recv_ms`; a receive can time out
 *                   (HTTPD_SOCK_ERR_TIMEOUT), and 0 is the client that has closed. While the server waits the
 *                   screen task can go on ticking the app.
 *   the answer      one per request: status, type, the headers set before it, the body. A header is
 *                   refused when max_resp_headers of them are set, eight by default (httpd_txrx.c).
 *   the flash       esp_ota_begin(), _write(), _end() and _abort() counted, the bytes written kept; a write
 *                   or the check of the image can be made to fail
 *   the lock        a counter. Taking it twice is a failed check, and so is everything that waits or writes
 *                   while it is held: a receive, the answer, the flash (platform.h).
 *   the clock       every look at it moves it by 3 ms, and a receive by what it takes
 *
 * What this cannot see, in short: nothing of ESP-IDF runs. The parser of the server is not here: a request
 * arrives as the model above says the parser leaves it, and what the parser refuses never arrives. One
 * request at a time, as the server has it; no sockets, no second client, no real time.
 */
#include <stdint.h>
#include <stdbool.h>
#include <strings.h>
#include "sim.h"
#include "web.c"

/* The rest of the platform ----------------------------------------------------------------------------- */

app_t *platform_app;
platform_info_t platform_info;

static int locked;
static int lock_count;
static uint64_t now_ms = 1000;
static uint32_t events_seen;
static int stored_calls;
static int lock_count_at_begin;     // of the request under way
static bool stored_early;           // platform_stored() was called for it before the lock was ever taken
static int begun_calls;
static bool begun_before_flash;     // platform_upload_begun() came before anything was erased
static bool begun_fails;            // the begin of an upload cannot be recorded
static int complete_calls;
static int ota_begins, ota_ends, ota_aborts;

void platform_lock(void)
{
	CHECK(locked == 0);
	locked = 1;
	lock_count++;
}

void platform_unlock(void)
{
	CHECK(locked == 1);
	locked = 0;
}

uint64_t platform_now_ms(void)
{
	return now_ms += 3;
}

void platform_events(void)
{
	CHECK(locked == 1);
	events_seen |= app_take_events(platform_app);
}

void platform_stored(void)
{
	CHECK(locked == 0);
	stored_calls++;
	stored_early = lock_count == lock_count_at_begin;
}

bool platform_upload_begun(void)
{
	// It writes to the flash
	CHECK(locked == 0);
	begun_calls++;
	begun_before_flash = ota_begins == 0;
	return !begun_fails;
}

void platform_upload_complete(void)
{
	CHECK(locked == 0);
	complete_calls++;
}

/* ESP-IDF ---------------------------------------------------------------------------------------------- */

const char *esp_err_to_name(esp_err_t code)
{
	(void)code;
	return "ERR";
}

void *heap_caps_malloc(size_t size, uint32_t caps)
{
	// The block of a firmware in the internal RAM, where the flash driver writes it in one piece; every
	// other room in the external RAM (web.c)
	if(size == WEB_BLOCK_SIZE)
	{
		CHECK(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
	}
	else
	{
		CHECK(caps == MALLOC_CAP_SPIRAM);
	}
	return sim_alloc(size);
}

static int nodelay;     // connections that were told not to hold small answers back

int lwip_setsockopt(int s, int level, int optname, const void *optval, uint32_t optlen)
{
	(void)s;
	if(level == IPPROTO_TCP && optname == TCP_NODELAY && optval != NULL && optlen == sizeof(int) && *(const int *)optval != 0)
	{
		nodelay++;
	}
	return 0;
}

/* The flash */

#define OTA_HANDLE  7

static esp_partition_t other = { .size = 0x400000, .label = "ota_1" };
static uint8_t flash[0x10000];
static size_t flash_written;
static int write_fail_at = -1;      // the write with this number fails
static int write_calls;
static bool end_fails;              // the check of the image fails

const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *from)
{
	(void)from;
	return &other;
}

esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t size, esp_ota_handle_t *handle)
{
	CHECK(locked == 0);
	// The slot that does not run, erased as the writing goes: erased in one piece nothing would be written
	// for many seconds, and bytes written are what tells the app that the upload is alive (web.c)
	CHECK(partition == &other);
	CHECK(size == OTA_WITH_SEQUENTIAL_WRITES);
	ota_begins++;
	flash_written = 0;
	write_calls = 0;
	*handle = OTA_HANDLE;
	return ESP_OK;
}

esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size)
{
	CHECK(locked == 0);
	CHECK(handle == OTA_HANDLE);
	if(write_calls++ == write_fail_at)
	{
		return ESP_FAIL;
	}
	// Never more than a block, and never more than the file has
	if(!CHECK(size <= WEB_BLOCK_SIZE) || !CHECK(flash_written + size <= sizeof(flash)))
	{
		return ESP_FAIL;
	}
	memcpy(flash + flash_written, data, size);
	flash_written += size;
	return ESP_OK;
}

esp_err_t esp_ota_end(esp_ota_handle_t handle)
{
	CHECK(locked == 0);
	CHECK(handle == OTA_HANDLE);
	ota_ends++;
	return end_fails ? ESP_FAIL : ESP_OK;
}

esp_err_t esp_ota_abort(esp_ota_handle_t handle)
{
	CHECK(handle == OTA_HANDLE);
	ota_aborts++;
	return ESP_OK;
}

/* The HTTP server */

static esp_err_t (*the_handler)(httpd_req_t *);
static httpd_err_handler_func_t the_error_handler[HTTPD_ERR_CODE_MAX];
static httpd_config_t the_config;

esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config)
{
	the_config = *config;
	*handle = (httpd_handle_t)&the_config;
	return ESP_OK;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri)
{
	(void)handle;
	// One handler for every path and every method: the server never comes to an answer of its own
	CHECK(strcmp(uri->uri, "*") == 0 && (int)uri->method == HTTP_ANY);
	the_handler = uri->handler;
	return ESP_OK;
}

esp_err_t httpd_register_err_handler(httpd_handle_t handle, httpd_err_code_t error, httpd_err_handler_func_t handler)
{
	(void)handle;
	if(CHECK((unsigned)error < HTTPD_ERR_CODE_MAX))
	{
		the_error_handler[error] = handler;
	}
	return ESP_OK;
}

bool httpd_uri_match_wildcard(const char *uri_template, const char *uri, size_t length)
{
	(void)uri_template;
	(void)uri;
	(void)length;
	return true;
}

#define WIRE_HEADERS    8
// The page is the largest answer there is
#define WIRE_ANSWER     (256 * 1024)

// One request as the server hands it over, and what came back
static struct
{
	const char *names[WIRE_HEADERS];
	const char *values[WIRE_HEADERS];
	int headers;
	const uint8_t *body;
	size_t body_length;     // what the client really sends
	size_t remaining;       // what the server still expects (Content-Length)
	size_t sent;            // bytes handed out
	size_t chunk;           // at most this many per receive
	uint64_t recv_ms;       // how long one receive takes
	size_t slow_after;      // behind so many bytes a receive brings one byte and takes 4 s; 0: never
	long timeout_at;        // a receive timeout when this many bytes were handed out ...
	int timeouts;           // ... so many times
	bool ticking;           // the screen task ticks the app while the server receives or waits
	char status[40];
	char type[40];
	const char *out_names[WIRE_HEADERS];
	const char *out_values[WIRE_HEADERS];
	int out_headers;
	int out_refused;        // headers the server had no room for
	char answer[WIRE_ANSWER + 1];
	size_t answer_length;
	bool answered;
	size_t received_after_answer;
} wire;

static httpd_req_t req;

// The screen task goes on while the server is busy with a client: app_tick() is what ends an upload that
// brings nothing
static void tick(void)
{
	platform_lock();
	app_tick(platform_app, now_ms);
	platform_unlock();
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *field, char *val, size_t val_size)
{
	(void)r;
	for(int i = 0; i < wire.headers; i++)
	{
		if(strcasecmp(wire.names[i], field) == 0)
		{
			// As httpd_parse.c does, with its off by one: a value of exactly val_size bytes is cut and not
			// reported
			size_t full = strlcpy(val, wire.values[i], val_size);

			return val_size < full ? ESP_ERR_HTTPD_RESULT_TRUNC : ESP_OK;
		}
	}
	return ESP_ERR_NOT_FOUND;
}

int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len)
{
	(void)r;
	CHECK(locked == 0);
	if(buf_len > wire.remaining)
	{
		buf_len = wire.remaining;
	}
	if(buf_len == 0)
	{
		return 0;
	}
	if(wire.timeout_at >= 0 && (size_t)wire.timeout_at == wire.sent && wire.timeouts > 0)
	{
		wire.timeouts--;
		if(wire.ticking)
		{
			now_ms += (uint64_t)the_config.recv_wait_timeout * 1000;
			tick();
		}
		return HTTPD_SOCK_ERR_TIMEOUT;
	}
	if(wire.sent >= wire.body_length)
	{
		// The client has closed
		return 0;
	}
	if(buf_len > wire.body_length - wire.sent)
	{
		buf_len = wire.body_length - wire.sent;
	}
	if(buf_len > wire.chunk)
	{
		buf_len = wire.chunk;
	}
	if(wire.slow_after != 0 && wire.sent >= wire.slow_after)
	{
		buf_len = 1;
		now_ms += 4000;
	}
	else
	{
		now_ms += wire.recv_ms;
	}
	if(wire.ticking)
	{
		tick();
	}
	memcpy(buf, wire.body + wire.sent, buf_len);
	wire.sent += buf_len;
	wire.remaining -= buf_len;
	if(wire.answered)
	{
		wire.received_after_answer += buf_len;
	}
	return (int)buf_len;
}

esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status)
{
	(void)r;
	snprintf(wire.status, sizeof(wire.status), "%s", status);
	return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type)
{
	(void)r;
	snprintf(wire.type, sizeof(wire.type), "%s", type);
	return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value)
{
	(void)r;
	CHECK(!wire.answered);
	// The server keeps the two pointers until the answer is sent, and has room for as many as its
	// configuration says: one more is refused, and the answer leaves without it (httpd_txrx.c)
	if(wire.out_headers >= the_config.max_resp_headers || wire.out_headers >= WIRE_HEADERS)
	{
		wire.out_refused++;
		return ESP_ERR_HTTPD_RESP_HDR;
	}
	wire.out_names[wire.out_headers] = field;
	wire.out_values[wire.out_headers] = value;
	wire.out_headers++;
	return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t length)
{
	(void)r;
	// Sending waits for the client: never under the lock. And a request has one answer.
	CHECK(locked == 0);
	CHECK(!wire.answered);
	if(!CHECK(length >= 0 && (size_t)length <= WIRE_ANSWER && (buf != NULL || length == 0)))
	{
		return ESP_FAIL;
	}
	if(length > 0)
	{
		memcpy(wire.answer, buf, (size_t)length);
	}
	wire.answer[length] = '\0';
	wire.answer_length = (size_t)length;
	wire.answered = true;
	return ESP_OK;
}

/* A request -------------------------------------------------------------------------------------------- */

#define HOST    "192.168.4.1"

static const char *out_header(const char *name);

static void state(void)
{
	// A compressed answer is no text
	if(out_header("Content-Encoding") == NULL)
	{
		printf("   status '%s' answer '%.200s'\n", wire.status, wire.answer);
	}
	else
	{
		printf("   status '%s' answer of %zu bytes, %s\n", wire.status, wire.answer_length, out_header("Content-Encoding"));
	}
	for(int i = 0; i < wire.headers; i++)
	{
		if(strcasecmp(wire.names[i], "Accept-Encoding") == 0)
		{
			printf("   asked with Accept-Encoding '%s'\n", wire.values[i]);
		}
	}
}

static void begin(int method, const char *uri)
{
	memset(&wire, 0, sizeof(wire));
	wire.chunk = 1460;
	wire.recv_ms = 7;
	wire.timeout_at = -1;
	memset(&req, 0, sizeof(req));
	req.method = method;
	snprintf((char *)req.uri, sizeof(req.uri), "%s", uri);
	events_seen = 0;
	lock_count_at_begin = lock_count;
	stored_early = false;
}

static void hdr(const char *name, const char *value)
{
	NEED(wire.headers < WIRE_HEADERS);
	wire.names[wire.headers] = name;
	wire.values[wire.headers] = value;
	wire.headers++;
	if(strcasecmp(name, "Content-Length") == 0)
	{
		// httpd_parse.c: 32 bit of what the parser counted, and no length where those are all ones. (More
		// digits than 64 bit hold never come this far: the parser refuses them.)
		unsigned long long counted = strtoull(value, NULL, 10);

		req.content_len = (uint32_t)counted != UINT32_MAX ? (uint32_t)counted : 0;
		wire.remaining = req.content_len;
	}
}

// The request of the page of the display: its host, and the header every change needs
static void begin_change(int method, const char *uri)
{
	begin(method, uri);
	hdr("Host", HOST);
	hdr("X-Display", "1");
}

static void begin_get(const char *uri)
{
	begin(HTTP_GET, uri);
	hdr("Host", HOST);
}

// The body the client sends, and the length it claims
static void body(const void *data, size_t length, const char *length_header)
{
	wire.body = data;
	wire.body_length = length;
	hdr("Content-Length", length_header);
}

static esp_err_t run(void)
{
	esp_err_t err = the_handler(&req);

	CHECK(locked == 0);
	return err;
}

static bool status_is(const char *number)
{
	return strncmp(wire.status, number, 3) == 0;
}

static bool answer_is(const char *text)
{
	return strcmp(wire.answer, text) == 0;
}

static const char *out_header(const char *name)
{
	for(int i = 0; i < wire.out_headers; i++)
	{
		if(strcasecmp(wire.out_names[i], name) == 0)
		{
			return wire.out_values[i];
		}
	}
	return NULL;
}

// The release, given at the knob
static void release(bool on)
{
	app_do(platform_app, on ? NAV_DO_RELEASE_ON : NAV_DO_RELEASE_OFF, now_ms);
	app_take_events(platform_app);
}

// The beginning of a firmware file that ota_check() takes
static void image(uint8_t *data, size_t size, const char *project)
{
	for(size_t i = 0; i < size; i++)
	{
		data[i] = (uint8_t)(i * 7 + 1);
	}
	data[0] = 0xE9;
	data[12] = 0x09;
	data[13] = 0x00;
	data[32] = 0x32;
	data[33] = 0x54;
	data[34] = 0xCD;
	data[35] = 0xAB;
	memset(&data[48], 0, 64);
	strcpy((char *)&data[48], "0.2.0");
	strcpy((char *)&data[80], project);
}

/* The scenarios ---------------------------------------------------------------------------------------- */

// The built-in layout as the build embeds it (the Makefile links it for main.c): a layout to send
extern const char layout_start[] __asm__("_binary_w906_default_json_start");
extern const char layout_end[] __asm__("_binary_w906_default_json_end");

static uint8_t file[10000];
static char large[200000];
static char page[WIRE_ANSWER];
static size_t page_length;
static char page_gz[WIRE_ANSWER];
static size_t page_gz_length;
static char number[32];

static void test_start(void)
{
	static json_token_t work[LAYOUT_TOKENS];
	app_boot_t boot;
	FILE *source = fopen("../../main/web/index.html", "rb");

	NEED(source != NULL);
	page_length = fread(page, 1, sizeof(page), source);
	fclose(source);
	NEED(page_length > 0 && page_length < sizeof(page));
	// The compressed page as the Makefile made it: a gzip member (RFC 1952: 1f 8b, deflate) that is smaller
	// than the page, or no check below could tell the two forms apart
	source = fopen("build/index.html.gz", "rb");
	NEED(source != NULL);
	page_gz_length = fread(page_gz, 1, sizeof(page_gz), source);
	fclose(source);
	NEED(page_gz_length > 18 && page_gz_length < page_length);
	NEED((uint8_t)page_gz[0] == 0x1f && (uint8_t)page_gz[1] == 0x8b && page_gz[2] == 8);

	platform_app = sim_alloc(sizeof(app_t));
	memset(&boot, 0, sizeof(boot));
	boot.version = "0.1.0";
	boot.git = "sim";
	boot.builtin_layout = layout_start;
	boot.builtin_length = (size_t)(layout_end - layout_start);
	boot.work = work;
	boot.work_count = LAYOUT_TOKENS;
	app_init(platform_app, &boot, 0);
	sim_state = state;

	CHECK(web_start() == ESP_OK);
	CHECK(the_handler != NULL);
	// What the server cannot read as a request gets no answer of the server: every error closes the connection
	for(int i = 0; i < HTTPD_ERR_CODE_MAX; i++)
	{
		CHECK(the_error_handler[i] != NULL && the_error_handler[i](&req, (httpd_err_code_t)i) == ESP_FAIL);
	}
	CHECK(the_config.uri_match_fn == httpd_uri_match_wildcard && the_config.max_uri_handlers == 1 && the_config.open_fn != NULL);
	// The page is answered with four headers at most: the room the server has by default is not cut
	CHECK(the_config.max_resp_headers >= 4);
	// A new connection is told not to hold a small answer back: the one before a restart has to leave
	CHECK(the_config.open_fn != NULL && the_config.open_fn(&the_config, 5) == ESP_OK && nodelay == 1);
	CHECK(web_start() == ESP_ERR_INVALID_STATE);
}

static void test_reading(void)
{
	// The page: the embedded file, byte for byte, and nothing behind it
	begin_get("/");
	CHECK(run() == ESP_OK && strcmp(wire.status, "200 OK") == 0 && strncmp(wire.type, "text/html", 9) == 0);
	CHECK(wire.answer_length == page_length && memcmp(wire.answer, page, page_length) == 0);

	// A reading route, with a host in another spelling
	begin(HTTP_GET, "/api/info");
	hdr("hOST", "wican-display.local:80");
	CHECK(run() == ESP_OK && strcmp(wire.status, "200 OK") == 0 && strcmp(wire.type, "application/json") == 0 &&
	      strstr(wire.answer, "\"project\":\"wican-display\"") != NULL);
	CHECK(wire.answer_length == strlen(wire.answer));
	// Every request waits for what the one before it raised, before it looks at the app (app_web.h)
	CHECK(stored_calls == 2 && stored_early);
}

// What web_route() refuses is answered with its status and its word, and no function is called for it
static void test_refusals(void)
{
	begin_get("/nope");
	CHECK(run() == ESP_OK && strcmp(wire.status, "404 Not Found") == 0 && answer_is("{\"error\":\"not_found\"}"));
	begin_get("/api/info?x=1");
	CHECK(run() == ESP_OK && status_is("400") && answer_is("{\"error\":\"query\"}"));
	begin_get("/api/ticket?id=3");
	CHECK(run() == ESP_OK && status_is("200") && strstr(wire.answer, "\"ticket\":3") != NULL);
	begin(HTTP_GET, "/api/info");
	CHECK(run() == ESP_OK && status_is("403") && answer_is("{\"error\":\"host\"}"));
	// A host that begins like an address and is cut where it has no room
	begin(HTTP_GET, "/api/info");
	hdr("Host", "192.168.4.1.evil.example.com.this.name.is.longer.than.the.room.for.it.example");
	CHECK(run() == ESP_OK && status_is("403") && answer_is("{\"error\":\"host\"}"));
	// A method that is not served: answered, and the connection is closed (what it sends behind its
	// headers would be taken for the next request)
	begin(HTTP_OPTIONS, "/api/settings");
	hdr("Host", HOST);
	CHECK(run() == ESP_FAIL && strcmp(wire.status, "405 Method Not Allowed") == 0 && answer_is("{\"error\":\"method\"}"));
	begin(HTTP_HEAD, "/");
	hdr("Host", HOST);
	CHECK(run() == ESP_FAIL && status_is("405"));
	// ... also when it says that it sends nothing: a HEAD is answered with a body its sender does not expect
	begin(HTTP_HEAD, "/");
	hdr("Host", HOST);
	hdr("Content-Length", "0");
	CHECK(run() == ESP_FAIL && status_is("405"));
	// A change without the header, and with one that only begins like it
	begin(HTTP_POST, "/api/reboot");
	hdr("Host", HOST);
	CHECK(run() == ESP_FAIL && status_is("403") && answer_is("{\"error\":\"header\"}"));
	begin(HTTP_POST, "/api/reboot");
	hdr("Host", HOST);
	hdr("X-Display", "1x");
	CHECK(run() == ESP_FAIL && status_is("403") && answer_is("{\"error\":\"header\"}"));
	begin(HTTP_POST, "/api/reboot");
	hdr("Host", HOST);
	hdr("X-Display", "1abc");
	CHECK(run() == ESP_FAIL && status_is("403") && answer_is("{\"error\":\"header\"}"));
	// With a length the connection stays
	begin(HTTP_POST, "/api/reboot");
	hdr("Host", HOST);
	hdr("Content-Length", "0");
	CHECK(run() == ESP_OK && status_is("403") && answer_is("{\"error\":\"header\"}"));

	// Closed release: the body nobody read is dropped behind the answer
	begin_change(HTTP_POST, "/api/settings");
	body("{\"brightness\":50}", 17, "17");
	CHECK(run() == ESP_OK && status_is("403") && strstr(wire.answer, "\"locked\"") != NULL &&
	      wire.received_after_answer == 17 && wire.remaining == 0);
}

static void test_settings(void)
{
	release(true);

	begin_change(HTTP_POST, "/api/settings");
	body("{\"brightness\":50}", 17, "17");
	wire.chunk = 5;
	CHECK(run() == ESP_OK && status_is("200") && strstr(wire.answer, "\"brightness\":50") != NULL &&
	      (events_seen & APP_EVENT_STORE_SETTINGS) && wire.received_after_answer == 0);
	CHECK(platform_app->settings.brightness == 50);
	// The time of the request is the clock of the platform, read last before the lock: not ahead, not behind
	CHECK(platform_app->clock_ms == now_ms);

	// No length: answered, and the connection is closed
	begin_change(HTTP_POST, "/api/settings");
	CHECK(run() == ESP_FAIL && strcmp(wire.status, "411 Length Required") == 0 && answer_is("{\"error\":\"length\"}"));
	// Lengths that are too large
	begin_change(HTTP_POST, "/api/settings");
	body("x", 1, "513");
	run();
	CHECK(status_is("413"));
	// ... beyond 32 bit, where the server itself counts on with what is left of the number
	begin_change(HTTP_POST, "/api/settings");
	hdr("Content-Length", "4294967306");
	CHECK(req.content_len == 10 && run() == ESP_FAIL && status_is("413"));
	// ... and beyond every number (the parser of the server refuses this one before it comes here)
	begin_change(HTTP_POST, "/api/settings");
	hdr("Content-Length", "99999999999999999999999999");
	run();
	CHECK(status_is("413"));
	// A body that does not arrive whole: no answer, nothing changed
	begin_change(HTTP_POST, "/api/settings");
	body("{\"brightness\":70}", 9, "17");
	CHECK(run() == ESP_FAIL && !wire.answered && platform_app->settings.brightness == 50);
}

static void test_layout(void)
{
	size_t length = (size_t)(layout_end - layout_start);

	snprintf(number, sizeof(number), "%zu", length);
	// A layout in pieces
	begin_change(HTTP_PUT, "/api/layout?mode=check");
	body(layout_start, length, number);
	CHECK(run() == ESP_OK && status_is("200") && strstr(wire.answer, "\"ok\":true") != NULL);
	begin_change(HTTP_PUT, "/api/layout?mode=save");
	body(layout_start, length, number);
	CHECK(run() == ESP_OK && status_is("200") && (events_seen & APP_EVENT_STORE_LAYOUT));
	begin_change(HTTP_PUT, "/api/layout?mode=check");
	body("{}", 2, "2");
	CHECK(run() == ESP_OK && status_is("400") && strstr(wire.answer, "\"ok\":false") != NULL);
}

static void upload_request(size_t sent)
{
	begin_change(HTTP_POST, "/api/ota");
	body(file, sent, "10000");
}

static void test_firmware(void)
{
	// Refused before anything is erased, and the rest of the file is dropped
	image(file, sizeof(file), "wican");
	upload_request(sizeof(file));
	CHECK(run() == ESP_OK && strcmp(wire.status, "422 Unprocessable Content") == 0 && answer_is("{\"error\":\"wrong_project\"}"));
	CHECK(ota_begins == 0 && begun_calls == 0 && wire.received_after_answer == sizeof(file) - WEB_BLOCK_SIZE &&
	      !platform_app->uploading);

	// Written whole, with two receive timeouts in the middle of a block
	image(file, sizeof(file), OTA_PROJECT_NAME);
	upload_request(sizeof(file));
	wire.timeout_at = 5556;
	wire.timeouts = 2;
	CHECK(run() == ESP_OK && strcmp(wire.status, "202 Accepted") == 0 && strstr(wire.answer, "\"ticket\":") != NULL);
	CHECK(begun_calls == 1 && begun_before_flash && ota_begins == 1 && ota_ends == 1 && ota_aborts == 0 && complete_calls == 1);
	CHECK(flash_written == sizeof(file) && memcmp(flash, file, sizeof(file)) == 0 && write_calls == 3 && wire.timeouts == 0);
	CHECK(!platform_app->uploading && platform_app->upload_percent == 100);

	// A second one while the question waits
	upload_request(sizeof(file));
	CHECK(run() == ESP_OK && status_is("409") && answer_is("{\"error\":\"asking\"}") && ota_begins == 1);
	// The knob refuses
	app_do(platform_app, NAV_DO_ASK_REFUSE, now_ms);

	// The flash fails at the second block
	write_fail_at = 1;
	upload_request(sizeof(file));
	CHECK(run() == ESP_OK && status_is("500") && answer_is("{\"error\":\"upload\"}"));
	CHECK(ota_begins == 2 && ota_ends == 1 && ota_aborts == 1 && complete_calls == 1 && begun_calls == 2 &&
	      !platform_app->uploading);
	CHECK(wire.remaining == 0);
	write_fail_at = -1;

	// The connection breaks
	upload_request(5000);
	CHECK(run() == ESP_OK && status_is("500"));
	CHECK(ota_begins == 3 && ota_ends == 1 && ota_aborts == 2 && complete_calls == 1 && !platform_app->uploading);

	// The image is not good
	end_fails = true;
	upload_request(sizeof(file));
	CHECK(run() == ESP_OK && status_is("500"));
	CHECK(ota_begins == 4 && ota_ends == 2 && ota_aborts == 2 && complete_calls == 1 && !platform_app->uploading);
	end_fails = false;

	// The sender falls silent, and the app ends the upload (app_tick() after APP_UPLOAD_IDLE_MS)
	upload_request(sizeof(file));
	wire.timeout_at = WEB_BLOCK_SIZE;
	wire.timeouts = 1000000;
	wire.ticking = true;
	CHECK(run() == ESP_FAIL && status_is("500") && answer_is("{\"error\":\"upload\"}"));
	CHECK(ota_begins == 5 && ota_ends == 2 && ota_aborts == 3 && complete_calls == 1 && !platform_app->uploading);
	CHECK(wire.timeouts < 1000000 - 5 && wire.timeouts > 1000000 - 12);
}

// The page says that it must not be shown in a frame, and that it has another form for another request
// (test_compressed()); no other answer has a header of its own, and none ever lets another origin in
static void test_headers(void)
{
	begin_get("/");
	CHECK(run() == ESP_OK && strcmp(wire.status, "200 OK") == 0);
	CHECK(out_header("X-Frame-Options") != NULL && strcmp(out_header("X-Frame-Options"), "DENY") == 0);
	CHECK(out_header("Content-Security-Policy") != NULL &&
	      strstr(out_header("Content-Security-Policy"), "frame-ancestors 'none'") != NULL);
	CHECK(out_header("Vary") != NULL && strcmp(out_header("Vary"), "Accept-Encoding") == 0);
	CHECK(wire.out_headers == 3 && wire.out_refused == 0 && out_header("Access-Control-Allow-Origin") == NULL);
	begin_get("/api/info");
	CHECK(run() == ESP_OK && wire.out_headers == 0);
	begin_get("/nope");
	CHECK(run() == ESP_OK && wire.out_headers == 0);
}

// GET / from a client that sent this Accept-Encoding; NULL: it sent none
static esp_err_t get_page(const char *accept)
{
	begin_get("/");
	if(accept != NULL)
	{
		hdr("Accept-Encoding", accept);
	}
	return run();
}

static bool header_is(const char *name, const char *value)
{
	const char *sent = out_header(name);

	return sent != NULL && strcmp(sent, value) == 0;
}

// What the answer with the page has in both of its forms: all it had before there were two, and Vary
static bool is_page(void)
{
	return strcmp(wire.status, "200 OK") == 0 && strcmp(wire.type, "text/html; charset=utf-8") == 0 &&
	       header_is("X-Frame-Options", "DENY") && header_is("Content-Security-Policy", "frame-ancestors 'none'") &&
	       header_is("Vary", "Accept-Encoding") && out_header("Access-Control-Allow-Origin") == NULL;
}

// The page as it is: no word of a coding, and the bytes of index.html
static bool is_plain(void)
{
	return is_page() && wire.out_headers == 3 && out_header("Content-Encoding") == NULL &&
	       wire.answer_length == page_length && memcmp(wire.answer, page, page_length) == 0;
}

// The compressed page: named gzip, and the bytes of index.html.gz
static bool is_compressed(void)
{
	return is_page() && wire.out_headers == 4 && header_is("Content-Encoding", "gzip") &&
	       wire.answer_length == page_gz_length && memcmp(wire.answer, page_gz, page_gz_length) == 0;
}

// Each of these lists, up to the NULL behind them, gets the page in this form. A check that fails says
// which list it was (state()).
static void pages(const char *const *lists, bool compressed)
{
	for(; *lists != NULL; lists++)
	{
		CHECK(get_page(*lists) == ESP_OK && (compressed ? is_compressed() : is_plain()) && wire.out_refused == 0);
	}
}

// A list of exactly `length` bytes that ends with `end`: codings nobody asks the display for in front of it
static const char *list_of(size_t length, const char *end)
{
	static char list[200];
	size_t filled = length - strlen(end);

	NEED(length < sizeof(list) && strlen(end) + 3 <= length);
	for(size_t i = 0; i < filled; i++)
	{
		// "br,br,br," and blanks up to the end
		list[i] = i >= filled - filled % 3 ? ' ' : "br,"[i % 3];
	}
	strcpy(list + filled, end);
	NEED(strlen(list) == length);
	return list;
}

// The page goes compressed to the client that says it reads gzip, and as it is to every other one
static void test_compressed(void)
{
	// What browsers send; in another order, in other letters, with the blanks and the empty elements a
	// list may have
	static const char *const named[] =
	{
		"gzip", "gzip, deflate", "gzip, deflate, br", "gzip, deflate, br, zstd",
		"deflate, gzip", "br,gzip,deflate", "GZip", "GZIP, DEFLATE", ", gzip ,", "deflate\t,\tgzip", NULL
	};
	// A weight above 0 is a yes, whatever the other codings weigh and whatever else is refused
	static const char *const weighted[] =
	{
		"gzip;q=1", "gzip;q=1.", "gzip;q=1.0", "gzip;q=1.000", "gzip;q=0.5", "gzip;q=0.001", "gzip;q=0.01",
		"gzip; q=0.8", "gzip ;Q=0.8 , identity;q=0.1", "br;q=1.0, gzip;q=0.8, *;q=0.1", "gzip;q=0.1, identity;q=1",
		"identity;q=0, gzip", "*;q=0, gzip", "deflate;q=0, gzip", NULL
	};
	// No list that names gzip
	static const char *const unnamed[] =
	{
		"", ",", "identity", "deflate", "deflate, br, zstd", "identity;q=1, deflate;q=0.5", NULL
	};
	// "*" names nothing, and a coding that only begins or ends like gzip is another one
	static const char *const others[] =
	{
		"*", "*;q=1", "deflate, *", "x-gzip", "gzipx", "xgzip", "gzip2", "gzip-9", "gzi", "g", NULL
	};
	// The weight 0 refuses, however it is written and wherever it stands
	static const char *const refused[] =
	{
		"gzip;q=0", "gzip;q=0.", "gzip;q=0.0", "gzip;q=0.00", "gzip;q=0.000", "gzip ; q=0", "GZIP;Q=0",
		"deflate, gzip;q=0, br", "gzip;q=0, *", NULL
	};
	// ... also next to a second mention that does not
	static const char *const twice[] = { "gzip, gzip;q=0", "gzip;q=0, gzip", "gzip;q=0.5, deflate, gzip;q=0.0", NULL };
	// The client that refuses the plain page as well gets it: there is no third form
	static const char *const nothing[] = { "identity;q=0", "*;q=0", "gzip;q=0, identity;q=0", NULL };
	// What is no list of codings counts for nothing, also where it names gzip: an element without a name
	// or with a name that is none, a parameter that is no weight, a weight that is no number from 0 to 1
	// with three decimals at most, and anything behind an element
	static const char *const broken[] =
	{
		";q=1, gzip", "(x), gzip", "\"gzip\"", "gzip/1.0", "gzip=1",
		"gzip;x=1", "gzip;level=9", "gzip;", "gzip;q", "gzip;q=", "gzip;q=, deflate", "gzip;q= 1",
		"gzip;q=2", "gzip;q=1.5", "gzip;q=1.0000", "gzip;q=0.5000", "gzip;q=0.0001", "gzip;q=.5", "gzip;q=-1",
		"gzip;q=0.5;x=1", "gzip deflate", "gzip q=1", "gzip, br;q=9", "gzip, deflate br", NULL
	};
	uint16_t headers_max = the_config.max_resp_headers;

	pages(named, true);
	pages(weighted, true);
	CHECK(get_page(NULL) == ESP_OK && is_plain());
	pages(unnamed, false);
	pages(others, false);
	pages(refused, false);
	pages(twice, false);
	pages(nothing, false);
	pages(broken, false);

	// A list that has no room is not read, whatever is left of it. This one refuses gzip just behind the
	// byte where its room ends: cut, it reads like a yes. The server says that it was cut; web.c does not ask.
	CHECK(get_page(list_of(sizeof(codings) + 3, ", gzip;q=0")) == ESP_OK && is_plain());
	// ... this one is cut by its last byte, which makes another coding of gzip, and the server does not
	// report it (httpd_parse.c)
	CHECK(get_page(list_of(sizeof(codings), ", gzipx")) == ESP_OK && is_plain());
	// ... so one that fills its room is not read either, though this one is whole
	CHECK(get_page(list_of(sizeof(codings) - 1, ", gzip")) == ESP_OK && is_plain());
	// ... and the longest that is read is one byte shorter
	CHECK(get_page(list_of(sizeof(codings) - 2, ", gzip")) == ESP_OK && is_compressed());
	CHECK(get_page(list_of(sizeof(codings) - 2, ", gzip;q=0")) == ESP_OK && is_plain());

	// The server has no room for the header that names the coding: then the page as it is. Never the
	// compressed bytes without their name.
	the_config.max_resp_headers = 3;
	CHECK(get_page("gzip") == ESP_OK && is_plain() && wire.out_refused == 1);
	the_config.max_resp_headers = headers_max;
	CHECK(get_page("gzip") == ESP_OK && is_compressed() && wire.out_refused == 0);

	// A request for the page that brings a body: answered like the others, and the body is dropped behind
	// the answer as behind every other
	begin_get("/");
	hdr("Accept-Encoding", "gzip");
	body("hello", 5, "5");
	CHECK(run() == ESP_OK && is_compressed() && wire.received_after_answer == 5 && wire.remaining == 0);

	// No other answer is compressed or says that it has another form, whatever its client reads: the
	// routes that are no page, and the page where web_route() refuses the request
	begin_get("/api/info");
	hdr("Accept-Encoding", "gzip");
	CHECK(run() == ESP_OK && status_is("200") && strcmp(wire.type, "application/json") == 0 && wire.out_headers == 0 &&
	      wire.answer_length == strlen(wire.answer) && strstr(wire.answer, "\"project\":\"wican-display\"") != NULL);
	begin_get("/nope");
	hdr("Accept-Encoding", "gzip");
	CHECK(run() == ESP_OK && status_is("404") && answer_is("{\"error\":\"not_found\"}") && wire.out_headers == 0);
	begin_get("/?x=1");
	hdr("Accept-Encoding", "gzip");
	CHECK(run() == ESP_OK && status_is("400") && answer_is("{\"error\":\"query\"}") && wire.out_headers == 0);
	begin(HTTP_GET, "/");
	hdr("Accept-Encoding", "gzip");
	CHECK(run() == ESP_OK && status_is("403") && answer_is("{\"error\":\"host\"}") && wire.out_headers == 0);
	begin(HTTP_HEAD, "/");
	hdr("Host", HOST);
	hdr("Accept-Encoding", "gzip");
	CHECK(run() == ESP_FAIL && status_is("405") && answer_is("{\"error\":\"method\"}") && wire.out_headers == 0);
}

// A length with zeros in front, cut in its room: never the number that is left of it
static void test_lengths(void)
{
	int before;

	release(true);
	before = platform_app->settings.brightness;
	begin_change(HTTP_POST, "/api/settings");
	body("{\"brightness\":61}", 17, "000000000000000017");
	CHECK(run() == ESP_FAIL && status_is("413") && wire.received_after_answer == 0);
	CHECK(platform_app->settings.brightness == before);
	// ... also the one the server does not report as cut (as long as the room, httpd_parse.c)
	begin_change(HTTP_POST, "/api/settings");
	body("{\"brightness\":61}", 17, "0000000000000017");
	CHECK(run() == ESP_FAIL && status_is("413") && wire.received_after_answer == 0);
	// ... and one of fifteen
	begin_change(HTTP_POST, "/api/settings");
	body("{\"brightness\":61}", 17, "000000000000017");
	CHECK(run() == ESP_FAIL && status_is("413") && wire.received_after_answer == 0);
	// Zeros in front that have room are a number like any other
	begin_change(HTTP_POST, "/api/settings");
	body("{\"brightness\":61}", 17, "0017");
	CHECK(run() == ESP_OK && status_is("200") && platform_app->settings.brightness == 61);
	// The server takes 4294967295 for no length at all and leaves the body where it is: closed
	begin_change(HTTP_POST, "/api/settings");
	hdr("Content-Length", "4294967295");
	CHECK(req.content_len == 0 && run() == ESP_FAIL && status_is("413"));
}

// Every wait for a client has an end of its own
static void test_slow_clients(void)
{
	uint64_t began_ms;
	int before;

	// A body that trickles in: given up after WEB_BODY_MS, not after as long as its sender likes
	begin_change(HTTP_POST, "/api/settings");
	body("{\"brightness\":62}", 17, "17");
	wire.chunk = 1;
	wire.recv_ms = 4000;
	began_ms = now_ms;
	CHECK(run() == ESP_FAIL && !wire.answered && platform_app->settings.brightness == 61);
	CHECK(now_ms - began_ms >= 15000 && now_ms - began_ms < 21000);
	// ... one that is merely slow arrives
	begin_change(HTTP_POST, "/api/settings");
	body("{\"brightness\":62}", 17, "17");
	wire.chunk = 1;
	wire.recv_ms = 500;
	CHECK(run() == ESP_OK && status_is("200") && platform_app->settings.brightness == 62);

	// What is dropped behind a refusal has an end as well
	release(false);
	begin_change(HTTP_POST, "/api/settings");
	body(large, sizeof(large), "200000");
	wire.recv_ms = 4000;
	began_ms = now_ms;
	CHECK(run() == ESP_FAIL && status_is("403") && strstr(wire.answer, "\"locked\"") != NULL);
	CHECK(now_ms - began_ms >= 60000 && now_ms - began_ms < 70000 && wire.received_after_answer < sizeof(large));
	// ... a firmware that was refused and comes at a usual pace is dropped whole
	begin_change(HTTP_POST, "/api/settings");
	body(large, sizeof(large), "200000");
	CHECK(run() == ESP_OK && status_is("403") && wire.received_after_answer == sizeof(large) && wire.remaining == 0);
	release(true);

	// Firmware: a block that trickles in while the app ends the upload is not written. The first block at
	// once, then a byte every four seconds.
	image(file, sizeof(file), OTA_PROJECT_NAME);
	upload_request(sizeof(file));
	wire.chunk = WEB_BLOCK_SIZE;
	wire.ticking = true;
	wire.slow_after = WEB_BLOCK_SIZE;
	before = ota_begins;
	began_ms = now_ms;
	run();
	CHECK(status_is("500") && answer_is("{\"error\":\"upload\"}"));
	CHECK(ota_begins == before + 1 && write_calls == 1 && !platform_app->uploading);
	CHECK(now_ms - began_ms < APP_UPLOAD_IDLE_MS + 5000 + 70000);
}

// Firmware: its begin cannot be recorded - nothing is erased, and the upload ends as one that failed
static void test_upload_not_recorded(void)
{
	int before = ota_begins;

	begun_fails = true;
	upload_request(sizeof(file));
	CHECK(run() == ESP_OK && status_is("500") && answer_is("{\"error\":\"upload\"}"));
	CHECK(ota_begins == before && !platform_app->uploading && wire.remaining == 0);
	begun_fails = false;
}

int main(void)
{
	test_start();
	test_reading();
	test_refusals();
	test_settings();
	test_layout();
	test_firmware();
	test_headers();
	test_compressed();
	test_lengths();
	test_slow_clients();
	test_upload_not_recorded();

	printf("the lock was taken %d times\n", lock_count);
	return sim_end();
}
