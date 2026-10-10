/*
 * Host test for display/components/core/app_web.c. Run "make test_app_web && ./test_app_web" in display/test.
 * redproof.py removes or weakens every rule once (mutations/app_web.py) and expects this test to fail.
 *
 * The app runs on the device of app_harness.h (clock, adapter, WiFi, knob, flash). The requests reach it as
 * the HTTP server of the platform will pass them on: web_route() with what app_web_request() fills in, then
 * the function of the route, then the events - serve() below. Where a rule of app_web.h can only be seen
 * when something happens between the look of web_route() and the function (the release ends, a read begins),
 * the story lets it happen there (meanwhile), or calls the function by itself.
 *
 * The bodies are compared byte for byte with the hand-written files fixtures/app_web_<name>.json, the screen
 * with fixtures/app_web_screen_<name>.txt.
 *
 * Behind the stories: random sequences of requests and of inputs at the device in a child process, with what
 * app_web.h and display/API.md promise watched around every request (see there).
 */
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "test.h"
#include "app.h"
#include "app_web.h"
#include "texts.h"
#include "app_harness.h"

#define SLOT            0x400000u   // an app slot of the display, display/partitions.csv
#define FILE_SIZE       1500000u    // the firmware of the stories
#define HOST            "192.168.1.77"
#define ANSWER_TOKENS   8192

#define WEB_EVENTS      (APP_EVENT_STORE_SETTINGS | APP_EVENT_STORE_WIFI | APP_EVENT_STORE_LAYOUT | APP_EVENT_ERASE_LAYOUT | APP_EVENT_REBOOT | \
                         APP_EVENT_FACTORY_RESET | APP_EVENT_PREVIOUS_FIRMWARE | APP_EVENT_INSTALL_FIRMWARE | APP_EVENT_MARK_VALID)

/* ---------------------------------------------------------------------------------------------------
 * The HTTP server of the test
 */

// The room for an answer lies in front of bytes no answer may touch
static struct
{
	char body[APP_WEB_OUT_SIZE];
	unsigned char behind[GUARD];
} room;
static char *const reply = room.body;
static size_t reply_length;
static int code;                    // the status of the last answer
static web_route_t routed;          // what web_route() made of the last request
static bool by_route;               // the last request was refused by web_route(), no function was called
static json_token_t answer_tokens[ANSWER_TOKENS];
static char expected[APP_WEB_OUT_SIZE + 64];

// Between the look of web_route() and the function of the route the lock is not given up, but the function
// is told a time of its own, and a story may let something happen
static uint64_t handler_lag;
static void (*meanwhile)(void);
// The Content-Length of POST /api/ota: the size of the file, of which the body passed on is the beginning
static uint32_t file_size;

// A room no answer was written into: every byte FILL, and a length no answer has
static void fresh_room(void)
{
	memset(&room, FILL, sizeof(room));
	reply_length = SIZE_MAX;
	code = -1;
}

// What holds for every answer: it lies in its room with its zero at *length and nowhere before, nothing
// behind the room is touched, and it is JSON as json.h reads it. Only the begin of an upload that may go on
// has no body.
static bool sound(void)
{
	bool whole = reply_length < APP_WEB_OUT_SIZE && reply[reply_length] == '\0' && strlen(reply) == reply_length;
	bool kept = all_bytes(room.behind, sizeof(room.behind), FILL);
	bool json = whole && (code == 0 ? reply_length == 0 : json_parse(reply, reply_length, answer_tokens, ANSWER_TOKENS) > 0);

	if(!whole) printf("  the answer has no end at its length %lu\n", (unsigned long)reply_length);
	if(!kept) printf("  bytes behind the room of the answer were written\n");
	if(whole && !json) printf("  the answer is no JSON: %.200s\n", reply);
	return whole && kept && json;
}

// No answer holds a password: none that is stored, none that is asked for, not the one of the access point
static bool discreet(void)
{
	const char *secrets[NET_PROFILES_MAX + 2];
	int count = 0;

	for(int i = 0; i < app->profile_count && i < NET_PROFILES_MAX; i++) secrets[count++] = app->profiles[i].password;
	secrets[count++] = app->wifi_asked.password;
	secrets[count++] = app->ap_password;
	for(int i = 0; i < count; i++)
	{
		if(secrets[i][0] != '\0' && code != 0 && strstr(reply, secrets[i]) != NULL)
		{
			printf("  the answer holds the password %s\n", secrets[i]);
			return false;
		}
	}
	return true;
}

// One request as the platform passes it on. target: path and query as they were sent. at_ms: the time the
// server read when the request arrived.
static int serve(web_method_t method, const char *target, const char *body, size_t body_length, uint64_t at_ms)
{
	static char path[160], query[160];
	const char *mark = strchr(target, '?');
	size_t path_length = mark != NULL ? (size_t)(mark - target) : strlen(target);
	web_request_t asked;
	web_decision_t decision;
	uint64_t at;

	memcpy(path, target, path_length);
	path[path_length] = '\0';
	if(mark != NULL) strcpy(query, mark + 1);

	memset(&asked, 0, sizeof(asked));
	asked.method = method;
	asked.path = path;
	asked.query = mark != NULL ? query : NULL;
	asked.header = method == WEB_GET ? NULL : "1";
	asked.host = HOST;
	asked.has_length = method != WEB_GET;
	asked.length = method == WEB_POST && strcmp(path, "/api/ota") == 0 ? file_size : (uint32_t)body_length;
	asked.slot_size = SLOT;
	app_web_request(app, &asked, at_ms);
	decision = web_route(&asked);

	fresh_room();
	routed = decision.route;
	by_route = decision.status != 0;
	if(by_route)
	{
		reply_length = (size_t)web_error_body(decision.error, reply, APP_WEB_OUT_SIZE);
		code = decision.status;
		return code;
	}

	if(meanwhile != NULL) meanwhile();
	at = given(at_ms + handler_lag);
	switch(decision.route)
	{
		case WEB_ROUTE_LAYOUT_CHECK:
		case WEB_ROUTE_LAYOUT_APPLY:
		case WEB_ROUTE_LAYOUT_SAVE:
		case WEB_ROUTE_LAYOUT_RESET:
			code = app_web_layout(app, decision.route, body, body_length, reply, &reply_length, at);
			break;
		case WEB_ROUTE_WIFI_STORE:
		case WEB_ROUTE_WIFI_FORGET:
			code = app_web_wifi(app, decision.route, body, body_length, reply, &reply_length, at);
			break;
		case WEB_ROUTE_SETTINGS:
			code = app_web_settings(app, body, body_length, reply, &reply_length, at);
			break;
		case WEB_ROUTE_REBOOT:
		case WEB_ROUTE_RESET:
			code = app_web_action(app, decision.route, reply, &reply_length, at);
			break;
		case WEB_ROUTE_OTA:
			code = app_web_upload_begin(app, (const uint8_t *)body, body_length, asked.length, asked.slot_size, reply, &reply_length, at);
			// The boot loader must not offer what is being overwritten, also after a restart
			if(code == 0) machine.previous_firmware = false;
			break;
		default:
			// The page is the platform's, and app_web_get() says so
			code = app_web_get(app, decision.route, decision.ticket, reply, &reply_length, at);
			break;
	}
	// The events are taken before the lock is given back
	carry_out();
	return code;
}

static int get_at(const char *target, uint64_t at_ms)
{
	return serve(WEB_GET, target, NULL, 0, at_ms);
}

static int get(const char *target)
{
	return get_at(target, now);
}

static int put(const char *target, const char *body)
{
	return serve(WEB_PUT, target, body, strlen(body), now);
}

static int post(const char *target, const char *body)
{
	return serve(WEB_POST, target, body, strlen(body), now);
}

// The text of fixtures/app_web_<name>.json
static const char *fixture(const char *name)
{
	char path[160];

	snprintf(path, sizeof(path), "fixtures/app_web_%s.json", name);
	if(!read_fixture(path, expected, sizeof(expected))) strcpy(expected, "no fixture");
	return expected;
}

// The last answer has this status and, byte for byte, the body `text`
static bool answered_text(int status, const char *text)
{
	bool same = code == status && sound() && discreet() && reply_length == strlen(text) && strcmp(reply, text) == 0;

	if(!same) printf("  wanted %d %.600s\n  got    %d %.600s\n", status, text, code, reply_length < APP_WEB_OUT_SIZE ? reply : "?");
	return same;
}

// ... the body of fixtures/app_web_<name>.json
static bool answered(int status, const char *name)
{
	return answered_text(status, fixture(name));
}

// The last answer has this status and a body that is sound and holds `part`
static bool answered_with(int status, const char *part)
{
	bool same = code == status && sound() && discreet() && strstr(reply, part) != NULL;

	if(!same) printf("  wanted %d with %s\n  got    %d %.900s\n", status, part, code, reply_length < APP_WEB_OUT_SIZE ? reply : "?");
	return same;
}

// The screen is fixtures/app_web_screen_<name>.txt
static void sees(const char *name, const char *rule)
{
	char file[80];

	snprintf(file, sizeof(file), "web_screen_%s", name);
	shows(file, rule);
}

/* The firmware file ------------------------------------------------------------------------------------ */

// The first bytes of a firmware of the display, as ota_check.h lays them out
static uint8_t image[OTA_CHECK_BYTES + 16];

static void make_image(const char *version)
{
	memset(image, 0, sizeof(image));
	image[0] = 0xE9;
	image[12] = 0x09;
	image[13] = 0x00;
	image[32] = 0x32;
	image[33] = 0x54;
	image[34] = 0xCD;
	image[35] = 0xAB;
	memcpy(&image[48], version, strlen(version));
	memcpy(&image[80], "wican-display", 13);
}

// POST /api/ota until the first bytes of the body have arrived: `first_length` bytes of a file of `size`
static int upload_first(size_t first_length, uint32_t size)
{
	file_size = size;
	return serve(WEB_POST, "/api/ota", (const char *)image, first_length, now);
}

// The begin of an upload of FILE_SIZE bytes with this version
static int upload_begin(const char *version)
{
	make_image(version);
	return upload_first(OTA_CHECK_BYTES, FILE_SIZE);
}

static void upload_progress(uint32_t written)
{
	app_web_upload_progress(app, written, FILE_SIZE, given(now));
	carry_out();
}

static int upload_end(bool ok)
{
	fresh_room();
	code = app_web_upload_end(app, ok, reply, &reply_length, given(now));
	carry_out();
	return code;
}

/* What a refused request leaves ------------------------------------------------------------------------ */

// A copy of the app to compare with. It is never used as an app: its pointers lead into the original.
static app_t before;

static void remember(void)
{
	memcpy(&before, app, sizeof(before));
}

// No byte of the app changed since remember()
static bool untouched(void)
{
	return memcmp(&before, app, sizeof(before)) == 0;
}

// Nothing changed but what a refused change may leave behind: the time the app has seen, and the release
// with its own time
static bool only_time(void)
{
	bool forward = app->clock_ms >= before.clock_ms;

	before.clock_ms = app->clock_ms;
	memcpy(&before.access, &app->access, sizeof(before.access));
	return forward && untouched();
}

// ... and the room for a layout that is being checked
static bool only_room(void)
{
	memcpy(&before.checked, &app->checked, sizeof(before.checked));
	return untouched();
}

// The parts of the app a request may change, by what app_web.h names for it
#define MAY_CLOCK       0x01u   // the time the app has seen: it is then the time of the request
#define MAY_CHECKED     0x02u   // the room a layout is read into
#define MAY_LAYOUT      0x04u   // the views in use, their text and source, and the page shown
#define MAY_ASKED       0x08u   // what a question asks for: the network and the detail
#define MAY_NETWORKS    0x10u   // the stored networks, the link, and what the poll and the lines make of a network that is left
#define MAY_SETTINGS    0x20u   // the settings and the direction of the knob
#define MAY_UPLOAD      0x40u   // the upload: running, percent, time, version, and the version to go back to

// Since remember() nothing changed but the parts named, the time of the app is `time` if it may change, and
// the release is byte for byte `release`: what one call of access.h made of it, or what it was
static bool nothing_but(unsigned may, const access_t *release, uint64_t time)
{
	bool exact = memcmp(&app->access, release, sizeof(*release)) == 0 && app->clock_ms == (may & MAY_CLOCK ? time : before.clock_ms);
	// What a forgotten network must not take along
	bool kept = strcmp(app->poll.bound_id, before.poll.bound_id) == 0 && memcmp(&app->poll.catalog, &before.poll.catalog, sizeof(app->poll.catalog)) == 0 &&
	            app->poll.http_ok == before.poll.http_ok && app->poll.http_failed == before.poll.http_failed;

	memcpy(&before.access, &app->access, sizeof(before.access));
	before.clock_ms = app->clock_ms;
	if(may & MAY_CHECKED) memcpy(&before.checked, &app->checked, sizeof(before.checked));
	if(may & MAY_LAYOUT)
	{
		memcpy(&before.layout, &app->layout, sizeof(before.layout));
		memcpy(before.layout_text, app->layout_text, sizeof(before.layout_text));
		before.layout_length = app->layout_length;
		before.source = app->source;
		before.nav.page = app->nav.page;
	}
	if(may & MAY_ASKED)
	{
		memcpy(&before.wifi_asked, &app->wifi_asked, sizeof(before.wifi_asked));
		before.has_wifi_asked = app->has_wifi_asked;
		memcpy(before.ask_detail, app->ask_detail, sizeof(before.ask_detail));
	}
	if(may & MAY_NETWORKS)
	{
		memcpy(before.profiles, app->profiles, sizeof(before.profiles));
		before.profile_count = app->profile_count;
		memcpy(&before.link, &app->link, sizeof(before.link));
		memcpy(&before.poll, &app->poll, sizeof(before.poll));
		memcpy(before.list, app->list, sizeof(before.list));
		before.list_lines = app->list_lines;
		memcpy(before.cleared, app->cleared, sizeof(before.cleared));
		before.cleared_lines = app->cleared_lines;
		memcpy(before.old, app->old, sizeof(before.old));
		before.old_lines = app->old_lines;
		memcpy(&before.summary, &app->summary, sizeof(before.summary));
	}
	if(may & MAY_SETTINGS)
	{
		memcpy(&before.settings, &app->settings, sizeof(before.settings));
		before.knob.reverse = app->knob.reverse;
	}
	if(may & MAY_UPLOAD)
	{
		before.uploading = app->uploading;
		before.upload_percent = app->upload_percent;
		before.upload_ms = app->upload_ms;
		memcpy(before.upload_version, app->upload_version, sizeof(before.upload_version));
		before.previous_firmware = app->previous_firmware;
	}
	return exact && kept && untouched();
}

// Nothing of what the browser asked for is in the memory
static bool nothing_asked(void)
{
	return !app->has_wifi_asked && all_bytes(&app->wifi_asked, sizeof(app->wifi_asked), 0) && app->ask_detail[0] == '\0';
}

static bool profile_is(int index, const char *ssid, const char *password, const char *host)
{
	const net_profile_t *profile = &app->profiles[index];

	return index < app->profile_count && strcmp(profile->ssid, ssid) == 0 && strcmp(profile->password, password) == 0 && strcmp(profile->host, host) == 0;
}

// What the platform was asked to store or to restart for since the counters were cleared - but for the
// catalogue, the binding and the old list, which the conversation with the adapter brings by itself
static int carried(void)
{
	return done.settings + done.wifi + done.layout + done.erase + done.valid + done.reboot + done.reset + done.previous + done.install;
}

/* The scenes ------------------------------------------------------------------------------------------- */

// drive(), and the release given at 2100: it lasts until 602100, at the latest until 1802100
static void released(void)
{
	drive();
	app_do(app, NAV_DO_RELEASE_ON, now);
}

// The other vehicle of the harness: five values and the battery voltage, two seconds after the start
static void other_vehicle(void)
{
	garage();
	wican.config = other_config;
	wican.values = other_values;
	start();
	run(2100);
	memset(&done, 0, sizeof(done));
}

// The display has read the fault memory at the knob: the list of app_result_read.json is shown
static void listed(void)
{
	released();
	app_do(app, NAV_DO_READ, now);
	run(6000);
}

static void begin_read(void)
{
	app_do(app, NAV_DO_READ, now);
}

static void begin_upload(void)
{
	app->uploading = true;
}

static void close_release(void)
{
	app_do(app, NAV_DO_RELEASE_OFF, now);
}

static void upload_and_close(void)
{
	app->uploading = true;
	app_do(app, NAV_DO_RELEASE_OFF, now);
}

static void read_and_close(void)
{
	app_do(app, NAV_DO_READ, now);
	app_do(app, NAV_DO_RELEASE_OFF, now);
}

static void ask_reset(void)
{
	access_ask(&app->access, ACCESS_ASK_RESET, now);
}

// The clear dialog on the screen: menu, Fehlerspeicher, Liste ansehen, Fehler löschen
static void open_dialog(void)
{
	short_press();
	short_press();
	turn(1);
	short_press();
	turn(app->list_lines + 1);
	short_press();
}

/* ---------------------------------------------------------------------------------------------------
 * The rules one by one
 */

static void test_constants(void)
{
	check(APP_WEB_OUT_SIZE == 20480 && APP_WEB_UPLOAD_LEFT_S == 300, "an answer has 20480 bytes of room, an upload begins with 300 s of release left");
	check(APP_WEB_OUT_SIZE > LAYOUT_TEXT_MAX && APP_WEB_OUT_SIZE >= WEB_VALUES_SIZE && APP_WEB_OUT_SIZE >= WEB_REPORT_SIZE && APP_WEB_OUT_SIZE >= 2 * POLL_TEXT_SIZE + 64 &&
	      APP_WEB_OUT_SIZE >= WEB_ERROR_SIZE, "the room holds the longest layout text, the values, a report, two fault memory lists and every refusal");
}

static void test_request(void)
{
	web_request_t asked;

	drive();
	memset(&asked, 0, sizeof(asked));
	asked.method = WEB_OTHER;
	asked.path = "/pfad";
	asked.query = "frage";
	asked.header = "kopf";
	asked.host = "wirt";
	asked.has_length = true;
	asked.length = 77;
	asked.release_open = true;
	asked.busy = true;
	asked.slot_size = 99;
	remember();
	app_web_request(app, &asked, now);
	check(!asked.release_open && !asked.busy, "a request while the release is closed and the display rests: release_open and busy are false");
	check(asked.method == WEB_OTHER && strcmp(asked.path, "/pfad") == 0 && strcmp(asked.query, "frage") == 0 && strcmp(asked.header, "kopf") == 0 && strcmp(asked.host, "wirt") == 0 &&
	      asked.has_length && asked.length == 77 && asked.slot_size == 99, "app_web_request() leaves method, path, query, headers, length and slot size as the platform filled them");
	check(untouched(), "app_web_request() changes nothing of the app, not even the time it has seen");

	app_do(app, NAV_DO_RELEASE_ON, now);
	app_web_request(app, &asked, now);
	check(asked.release_open && !asked.busy, "a request while the release is open and the display rests: release_open true, busy false");
	asked.release_open = false;
	app_web_request(app, &asked, 602099);
	check(asked.release_open, "a request at 602099, the last millisecond of a release given at 2100: release_open");
	app_web_request(app, &asked, 602100);
	check(!asked.release_open, "a request at 602100: the release has ended, release_open is false");
	app_tick(app, 602100);
	asked.release_open = true;
	app_web_request(app, &asked, 602000);
	check(!asked.release_open, "a request with a time before the latest the app has seen: the release that ended by the time of the app is closed");

	drive();
	app_do(app, NAV_DO_READ, now);
	app_web_request(app, &asked, now);
	check(!asked.release_open && asked.busy && phase() == DTC_FLOW_READ_SENT, "a request while a read of the display waits to be sent: busy, and the closed release stays closed");
	run(6000);
	asked.busy = true;
	app_web_request(app, &asked, now);
	check(!asked.busy && phase() == DTC_FLOW_LIST, "a request while the list is shown: not busy");
	open_dialog();
	app_web_request(app, &asked, now);
	check(asked.busy && on(NAV_DTC_CONFIRM) && phase() == DTC_FLOW_LIST, "a request while the clear dialog shows: busy");

	drive();
	app->uploading = true;
	app_web_request(app, &asked, now);
	check(asked.busy, "a request while a firmware upload runs: busy");
}

/* The reading routes ----------------------------------------------------------------------------------- */

static void test_info(void)
{
	released();
	app_temperature(app, 47, true);
	app->poll.http_ok = 12345;
	app->poll.http_failed = 7;
	remember();
	get("/api/info");
	check(answered(200, "info"), "app_web_info.json: GET /api/info two seconds after the start, the release given just now");
	check(untouched(), "GET /api/info changes nothing of the app, not even the time it has seen");
	check(strstr(reply, "geheim1234") == NULL && strcmp(app->ap_password, "geheim1234") == 0, "the info does not hold the password of the own access point");

	get_at("/api/info", 2999);
	check(answered_with(200, "\"up\":2,") && answered_with(200, "\"left_s\":600}"), "the info at 2999 ms: up 2 s, the release ends in 599.101 s, written as 600");
	get_at("/api/info", 3000);
	check(answered_with(200, "\"up\":3,"), "the info at 3000 ms: up 3 s");
	get_at("/api/info", 3100);
	check(answered_with(200, "\"left_s\":599}"), "the info at 3100 ms: the release ends in 599 s");
	get_at("/api/info", 602099);
	check(answered_with(200, "\"release\":{\"open\":true,\"left_s\":1}"), "the info in the last millisecond of the release: open, 1 s left");
	get_at("/api/info", 602100);
	check(answered_with(200, "\"release\":{\"open\":false,\"left_s\":0}"), "the info when the release has ended: closed, 0 s left");
	get_at("/api/info", 3000000000999u);
	check(answered_with(200, "\"up\":3000000000,"), "the info after 3000000000.999 s, more than 31 bit hold: up 3000000000");
	get_at("/api/info", 4294967294999u);
	check(answered_with(200, "\"up\":4294967294,"), "the info one second before the largest number: up 4294967294");
	get_at("/api/info", 4294967295999u);
	check(answered_with(200, "\"up\":4294967295,"), "the info at the last millisecond 32 bit of seconds hold: up 4294967295");
	get_at("/api/info", 4294967296000u);
	check(answered_with(200, "\"up\":4294967295,"), "the info one millisecond later: up stays at the largest number");
	get_at("/api/info", UINT64_MAX);
	check(answered_with(200, "\"up\":4294967295,"), "the info at the largest time: up is the largest number");

	// The view of the connection is the one at the time of the request
	app->poll.conn.failed_rounds = 3;
	get_at("/api/info", 14999);
	check(answered_with(200, "\"view\":\"live\"}"), "three failed rounds 14999 ms after the network came up: within the grace time the info says live");
	get_at("/api/info", 15000);
	check(answered_with(200, "\"view\":\"no_answer\"}"), "three failed rounds 15000 ms after the network came up: the info says no_answer");
	app_tick(app, 15000);
	get_at("/api/info", 2100);
	check(answered_with(200, "\"view\":\"no_answer\"}") && answered_with(200, "\"up\":15,"), "an info request with a time before the latest the app has seen is answered for the time of the app");

	// Safe mode, an update that was taken back, one that waits, the heat
	factory();
	machine.safe_mode = true;
	machine.rolled_back = true;
	machine.update_pending = true;
	start();
	run(100);
	app_temperature(app, 90, true);
	app_temperature(app, 20, false);
	get("/api/info");
	check(answered(200, "info_safe"), "app_web_info_safe.json: the info of a display in safe mode without a network, 90 degrees read last, the reading after it failed");
	app_temperature(app, 82, true);
	get("/api/info");
	check(answered_with(200, "\"temp_c\":82,\"heat\":\"off\","), "the info at 82 degrees coming from 90: the heat is still off");
	app_temperature(app, 79, true);
	get("/api/info");
	check(answered_with(200, "\"temp_c\":79,\"heat\":\"dim\","), "the info at 79 degrees coming from 90: the heat is dim");
	app_temperature(app, -12, true);
	get("/api/info");
	check(answered_with(200, "\"temp_c\":-12,\"heat\":\"normal\","), "the info at -12 degrees: the heat is normal");

	garage();
	machine.safe_mode = true;
	start();
	get("/api/info");
	check(answered_with(200, "\"safe_mode\":true,\"rolled_back\":false,\"update_pending\":false,") && answered_with(200, "\"temp_c\":0,\"heat\":\"normal\","),
	      "the info in safe mode alone, before a temperature was read: temp_c 0");
	garage();
	machine.rolled_back = true;
	start();
	get("/api/info");
	check(answered_with(200, "\"safe_mode\":false,\"rolled_back\":true,\"update_pending\":false,"), "the info after an update that was taken back");
	garage();
	machine.update_pending = true;
	start();
	get("/api/info");
	check(answered_with(200, "\"safe_mode\":false,\"rolled_back\":false,\"update_pending\":true,") && answered_with(200, "\"wican\":{\"host\":\"\",\"id\":\"a1b2c3d4e5f6\",\"fw\":\"\",\"view\":\"no_wifi\"}"),
	      "the info of an update that waits, before the network is up: bound to the adapter, no host, no firmware of the adapter");

	// The source of the layout
	garage();
	strcpy(flash.layout, stored_layout);
	flash.has_layout = true;
	start();
	get("/api/info");
	check(answered_with(200, "\"layout\":{\"name\":\"Meine Ansichten\",\"source\":\"stored\"}"), "the info with a stored layout: its name, source stored");
	app->source = APP_LAYOUT_PREVIEW;
	get("/api/info");
	check(answered_with(200, "\"layout\":{\"name\":\"Meine Ansichten\",\"source\":\"preview\"}"), "the info with a layout that is looked at: source preview");
	app->source = (app_layout_source_t)4;
	get("/api/info");
	check(answered_with(200, "\"layout\":{\"name\":\"Meine Ansichten\",\"source\":\"\"}"), "the info with a source that is none of the enum, 4: an empty word");
	app->source = (app_layout_source_t)-1;
	get("/api/info");
	check(answered_with(200, "\"layout\":{\"name\":\"Meine Ansichten\",\"source\":\"\"}"), "the info with a source that is none of the enum, -1: an empty word");
	other_vehicle();
	get("/api/info");
	check(answered_with(200, "\"layout\":{\"name\":\"\",\"source\":\"generated\"}"), "the info on another vehicle: views made from its catalogue, source generated");

	// In a network whose adapter is not found yet, and in no network with an address of its own
	garage();
	flash.profiles[0].host[0] = '\0';
	start();
	run(1100);
	check(!link_up(&app->link) && wifi.joined && strcmp(app->ssid, "Werkstatt") == 0, "the scene of a display that is in its network and has not found the adapter");
	get("/api/info");
	check(answered_with(200, "\"wifi\":{\"ssid\":\"Werkstatt\",\"ip\":\"192.168.1.77\",\"rssi\":-61,\"ap\":false,") && answered_with(200, "\"wican\":{\"host\":\"\",\"id\":\"a1b2c3d4e5f6\",\"fw\":\"\",\"view\":\"no_wifi\"}"),
	      "the info names the network the display is in while the adapter is not found: no host, view no_wifi");
	get("/api/wifi");
	check(answered_with(200, "{\"current\":\"Werkstatt\",\"profiles\":[{\"ssid\":\"Werkstatt\",\"host\":\"\","), "the list of networks names the network the display is in while the adapter is not found");
	platform.ssid = "";
	platform.ip = "192.168.4.1";
	platform.rssi = -40;
	app_platform(app, &platform);
	get("/api/info");
	check(answered_with(200, "\"wifi\":{\"ssid\":\"\",\"ip\":\"192.168.4.1\",\"rssi\":-40,"), "the info names address and signal as the platform brought them, also without a network name");

	// An update nobody confirmed in time still waits for its answer
	garage();
	machine.update_pending = true;
	start();
	stride = 1000;
	run(301000);
	stride = STEP_MS;
	check(app->update_pending && app->update_given_up && done.reboot == 1, "the scene of an update nobody confirmed in 300 s: the restart is asked for");
	get("/api/info");
	check(answered_with(200, "\"rolled_back\":false,\"update_pending\":true,"), "the info of an update nobody confirmed in time: it is still pending");

	// The settings in use, and the own access point
	released();
	post("/api/settings", "{\"brightness\":40,\"reverse\":true,\"standby_s\":0}");
	app_do(app, NAV_DO_AP_TOGGLE, now);
	get("/api/info");
	check(answered_with(200, "\"ap\":false,\"ap_ssid\":\"WiCAN-Display\"}"), "the hotspot was asked for at the device and is not ordered yet: the info says that it is off");
	run(100);
	get("/api/info");
	check(answered_with(200, "\"settings\":{\"brightness\":40,\"night\":25,\"night_mode\":false,\"reverse\":true,\"standby_s\":0}}") && answered_with(200, "\"ap\":true,\"ap_ssid\":\"WiCAN-Display\"}"),
	      "the info after the settings were changed and the hotspot was switched on at the device");
}

// The info with texts that need room: 32 quotes as the name of each network
static void test_info_long(void)
{
	static const char quotes[] = "\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"";
	static char written[160];

	released();
	app_temperature(app, 47, true);
	app->poll.http_ok = 12345;
	app->poll.http_failed = 7;
	platform.ssid = quotes;
	platform.ap_ssid = quotes;
	app_platform(app, &platform);
	get("/api/info");
	// The names of app_web_info.json have 9 and 13 bytes; a quote is written with two
	check(code == 200 && sound() && reply_length == strlen(fixture("info")) - 9 - 13 + 64 + 64 && reply_length == 788, "the info with two network names of 32 quotes each has 788 bytes: 200");
	snprintf(written, sizeof(written), "\"wifi\":{\"ssid\":\"%s%s\",\"ip\":", "\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"", "\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"");
	check(strstr(reply, written) != NULL, "the name of the network is written with a backslash before each of its 32 quotes");

	// The version texts are the platform's and have no limit: with app_web_info.json, 682 bytes with a git
	// text of 25, a git text of 19822 bytes fills the room to its last byte
	released();
	app_temperature(app, 47, true);
	app->poll.http_ok = 12345;
	app->poll.http_failed = 7;
	memset(expected, 'g', 19823);
	expected[19822] = '\0';
	app->git = expected;
	get("/api/info");
	check(code == 200 && sound() && reply_length == 20479, "an info of 20479 bytes fills the room to its last byte: 200");
	expected[19822] = 'g';
	expected[19823] = '\0';
	get("/api/info");
	check(code == 500 && sound() && strcmp(reply, "{\"error\":\"too_large\"}") == 0, "an info of 20480 bytes has no room for its zero: 500 too_large");
	app->git = GIT;
}

static void test_values_and_catalog(void)
{
	other_vehicle();
	remember();
	get("/api/values");
	check(answered(200, "values"), "app_web_values.json: GET /api/values of the other vehicle at 2100, the values seen at 2000");
	check(untouched(), "GET /api/values changes nothing of the app");
	get_at("/api/values", 4999);
	check(answered(200, "values"), "the values 2999 ms after they were seen: fresh");
	get_at("/api/values", 5000);
	check(answered(200, "values_old"), "app_web_values_old.json: the values 3000 ms after they were seen: old");
	get_at("/api/values", 11999);
	check(answered(200, "values_old"), "the values 9999 ms after they were seen: old");
	get_at("/api/values", 12000);
	check(answered(200, "values_gone"), "app_web_values_gone.json: the values 10000 ms after they were seen: left out, the view of the connection is still the last one");

	get("/api/catalog");
	check(answered(200, "catalog"), "app_web_catalog.json: GET /api/catalog of the other vehicle: the battery voltage first, then the profile in its order, everything delivered");
	check(untouched(), "GET /api/catalog changes nothing of the app");

	lose_wifi();
	get("/api/values");
	check(answered_with(200, "{\"view\":\"no_wifi\",\"values\":{\"@BATT_V\":{\"v\":12.4,\"age\":\"fresh\"},\"SPEED\":{\"v\":0,\"age\":\"fresh\"},") && reply_length == strlen(fixture("values")) + 3,
	      "the network is lost: the values the display still holds are answered with their age, the view says no_wifi");
	other_vehicle();
	app->poll.conn.failed_rounds = 3;
	get_at("/api/values", 14999);
	check(answered_text(200, "{\"view\":\"live\",\"values\":{}}"), "three failed rounds 14999 ms after the network came up: within the grace time the values say live");
	get_at("/api/values", 15000);
	check(answered_text(200, "{\"view\":\"no_answer\",\"values\":{}}"), "three failed rounds 15000 ms after the network came up: the values say no_answer");

	factory();
	start();
	get("/api/values");
	check(answered(200, "values_start"), "app_web_values_start.json: the values of a display in no network: none, view no_wifi");
	get("/api/catalog");
	check(answered(200, "catalog_start"), "app_web_catalog_start.json: the catalogue before an adapter answered: the battery voltage alone");
}

// A catalogue of 96 entries whose names are `plain` letters behind `controls` control characters for the
// first `full` entries, and the entry behind them, and empty for the rest
static void fill_catalog(int full, int controls, int plain)
{
	catalog_t *catalog = &app->poll.catalog;

	memset(catalog, 0, sizeof(*catalog));
	catalog->count = CATALOG_MAX;
	for(int i = 0; i < full; i++) memset(catalog->entries[i].name, 0x01, VALUE_NAME_SIZE - 1);
	memset(catalog->entries[full].name, 0x01, (size_t)controls);
	memset(catalog->entries[full].name + controls, 'A', (size_t)plain);
}

static void test_largest_answers(void)
{
	catalog_t *catalog = &app->poll.catalog;
	poll_t *poll = &app->poll;

	// An entry without texts is "":{"unit":"","class":"","profile":false,"delivered":false} with 59 bytes, 96
	// of them with their commas and the braces 5761. A control character is written with 6 bytes: 76 names
	// of 32 are 14592.
	drive();
	fill_catalog(76, 21, 0);
	get("/api/catalog");
	check(code == 200 && sound() && reply_length == 20479 && strncmp(reply, "{\"\\u0001\\u0001", 14) == 0 && strcmp(reply + 20479 - 58, ":{\"unit\":\"\",\"class\":\"\",\"profile\":false,\"delivered\":false}}") == 0,
	      "a catalogue whose text has 20479 bytes fills the room to its last byte: 200");
	fill_catalog(76, 20, 5);
	get("/api/catalog");
	check(code == 200 && sound() && reply_length == 20478, "a catalogue whose text has 20478 bytes: 200");
	fill_catalog(76, 20, 7);
	get("/api/catalog");
	check(answered(500, "too_large"), "app_web_too_large.json: a catalogue whose text has 20480 bytes has no room for its zero: 500");
	fill_catalog(76, 20, 8);
	get("/api/catalog");
	check(answered(500, "too_large"), "a catalogue whose text has 20481 bytes: 500");

	// The largest catalogue: every text of every entry control characters
	for(int i = 0; i < CATALOG_MAX; i++)
	{
		memset(catalog->entries[i].name, 0x1F, VALUE_NAME_SIZE - 1);
		memset(catalog->entries[i].unit, 0x1F, CATALOG_UNIT_SIZE - 1);
		memset(catalog->entries[i].value_class, 0x1F, CATALOG_CLASS_SIZE - 1);
	}
	get("/api/catalog");
	check(answered(500, "too_large"), "the largest catalogue there can be, 43777 bytes of text: 500, and nothing is written behind the room");
	// ... and the largest whose texts need no \u00xx
	for(int i = 0; i < CATALOG_MAX; i++)
	{
		memset(catalog->entries[i].name, '"', VALUE_NAME_SIZE - 1);
		memset(catalog->entries[i].unit, '\\', CATALOG_UNIT_SIZE - 1);
		memset(catalog->entries[i].value_class, '"', CATALOG_CLASS_SIZE - 1);
	}
	get("/api/catalog");
	check(code == 200 && sound() && reply_length == 18433, "the largest catalogue of texts without control characters has the 18433 bytes app_web.h names: 200");

	// Two fault memory lists of the longest texts the poll keeps, and the largest age
	memset(poll->list_text, ' ', POLL_TEXT_SIZE - 1);
	memcpy(poll->list_text, "{\"a\":1", 6);
	poll->list_text[POLL_TEXT_SIZE - 2] = '}';
	poll->list_text[POLL_TEXT_SIZE - 1] = '\0';
	memcpy(poll->old_text, poll->list_text, POLL_TEXT_SIZE);
	poll->has_list = true;
	poll->has_old = true;
	poll->flow.list_end_ms = 0;
	get_at("/api/dtc/last", UINT64_MAX);
	check(code == 200 && sound() && reply_length == 10447 && strstr(reply, ",\"read_age_s\":4294967295,\"before_clear\":{\"a\":1") != NULL,
	      "two fault memory lists of 5199 bytes each and the largest age have the 10447 bytes app_web.h names: 200");

	// The layout text
	released();
	memset(expected, ' ', LAYOUT_TEXT_MAX);
	memcpy(expected, stored_layout, strlen(stored_layout));
	expected[LAYOUT_TEXT_MAX] = '\0';
	serve(WEB_PUT, "/api/layout?mode=apply", expected, LAYOUT_TEXT_MAX, now);
	check(code == 200 && app->layout_length == LAYOUT_TEXT_MAX && all_bytes(box.behind, sizeof(box.behind), FILL), "a layout text of the largest size, 16384 bytes, is taken as the layout in use");
	get("/api/layout");
	check(code == 200 && sound() && reply_length == LAYOUT_TEXT_MAX && memcmp(reply, expected, LAYOUT_TEXT_MAX) == 0, "the layout text of the largest size is answered whole");
	expected[LAYOUT_TEXT_MAX] = ' ';
	expected[LAYOUT_TEXT_MAX + 1] = '\0';
	remember();
	serve(WEB_PUT, "/api/layout?mode=apply", expected, LAYOUT_TEXT_MAX + 1, now);
	check(by_route && code == 413, "a layout text of 16385 bytes is refused by web_route()");
	fresh_room();
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_APPLY, expected, LAYOUT_TEXT_MAX + 1, reply, &reply_length, now);
	check(answered_with(400, "{\"ok\":false,\"path\":\"\",\"problem\":") && only_time() && app_take_events(app) == 0, "a layout text of 16385 bytes passed to the function: 400 with the report, nothing changes");
}

static void test_layout_text(void)
{
	drive();
	remember();
	get("/api/layout");
	check(answered_text(200, builtin_text) && reply_length == strlen(builtin_text), "GET /api/layout with the built-in views: the text of layouts/w906_default.json byte for byte");
	check(untouched(), "GET /api/layout changes nothing of the app");

	other_vehicle();
	get("/api/layout");
	check(code == 200 && sound() && strncmp(reply, "{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":[{\"title\":\"Werte 1\",\"items\":[{\"key\":\"SPEED\",", 91) == 0 &&
	      strcmp(reply, app->layout_text) == 0 && reply_length == app->layout_length, "GET /api/layout with generated views: the text the app wrote for them");

	app->layout_length = 0;
	app->layout_text[0] = '\0';
	get("/api/layout");
	check(answered(500, "too_large"), "GET /api/layout while the views in use have no text (it could not be written): 500 too_large, not an empty body");
	app->layout_length = APP_WEB_OUT_SIZE - 1;
	get("/api/layout");
	check(code == 200 && reply_length == APP_WEB_OUT_SIZE - 1 && all_bytes(room.behind, sizeof(room.behind), FILL), "a layout text said to be 20479 bytes long fills the room to its last byte");
	app->layout_length = APP_WEB_OUT_SIZE;
	get("/api/layout");
	check(answered(500, "too_large"), "a layout text said to be 20480 bytes long has no room: 500, nothing behind the room is written");
}

static void test_dtc_last(void)
{
	uint64_t ended;

	drive();
	remember();
	get("/api/dtc/last");
	check(answered(200, "dtc_last_none"), "app_web_dtc_last_none.json: GET /api/dtc/last before anything was read");
	check(untouched(), "GET /api/dtc/last changes nothing of the app");

	listed();
	ended = app->poll.flow.list_end_ms;
	check(phase() == DTC_FLOW_LIST && app->poll.has_list && !app->poll.has_old && ended > 2100 && ended < now, "the scene of a list read at the knob");
	get_at("/api/dtc/last", ended + 95999);
	check(answered(200, "dtc_last_read"), "app_web_dtc_last_read.json: the list as the adapter sent it, 95.999 s after its read ended: 95 s old");
	get_at("/api/dtc/last", ended + 94999);
	check(answered_with(200, ",\"read_age_s\":94,\"before_clear\":null}"), "the list 94.999 s after its read ended: 94 s old");
	get_at("/api/dtc/last", ended + 96000);
	check(answered_with(200, ",\"read_age_s\":96,\"before_clear\":null}"), "the list 96 s after its read ended: 96 s old");
	app->poll.flow.list_end_ms = now + 5000;
	get("/api/dtc/last");
	check(answered(200, "dtc_last_fresh"), "app_web_dtc_last_fresh.json: a read that ended at a time ahead of the request: 0 s old");
	app->poll.flow.list_end_ms = 0;
	get_at("/api/dtc/last", 4294967295999u);
	check(answered_with(200, ",\"read_age_s\":4294967295,\"before_clear\":null}"), "a list 4294967295.999 s old");
	get_at("/api/dtc/last", 4294967296000u);
	check(answered_with(200, ",\"read_age_s\":4294967295,\"before_clear\":null}"), "a list one millisecond older: the age stays at the largest number");

	// The clear at the knob
	listed();
	ended = app->poll.flow.list_end_ms;
	run(2000);
	app_do(app, NAV_DO_CLEAR, now);
	run(1000);
	check(phase() == DTC_FLOW_CLEARING && app->poll.has_list && app->poll.has_old, "the scene of a clear the adapter accepted: the list is still shown and is the list before the clear as well");
	get_at("/api/dtc/last", ended + 7000);
	check(answered(200, "dtc_last_clearing"), "app_web_dtc_last_clearing.json: while the clear runs the list is read and before_clear");
	run(5000);
	check(phase() == DTC_FLOW_CLEARED && !app->poll.has_list && app->poll.has_old, "the scene of the outcome of the clear");
	get("/api/dtc/last");
	check(answered(200, "dtc_last_cleared"), "app_web_dtc_last_cleared.json: after the clear there is no read list, the one before the clear stays");

	// The list before the last clear that was stored
	garage();
	strcpy(flash.old, result_read);
	flash.has_old = true;
	start();
	get("/api/dtc/last");
	check(answered(200, "dtc_last_cleared") && phase() == DTC_FLOW_IDLE, "after a start the list before the last clear is the one of the flash, before any network is up");

	// What the poll does not hold is not answered, whatever stands in its room
	app->poll.has_old = false;
	get("/api/dtc/last");
	check(answered(200, "dtc_last_none"), "a text in the room of the old list that the poll does not hold is not answered");
	strcpy(app->poll.list_text, "{\"rest\":1}");
	get("/api/dtc/last");
	check(answered(200, "dtc_last_none"), "a text in the room of the list that the poll does not hold is not answered");
}

static void test_wifi_list(void)
{
	static web_seen_t found[LINK_SEEN_MAX + 5];
	char name[NET_SSID_SIZE + 8];

	drive();
	strcpy(found[0].ssid, "Werkstatt");
	found[0].rssi = -52;
	found[0].secure = true;
	found[1].rssi = -70;
	found[1].secure = true;
	strcpy(found[2].ssid, "Freifunk");
	found[2].rssi = -88;
	found[3].rssi = -99;
	app->seen[3].rssi = 77;
	app_web_seen(app, found, 3);
	check(app->seen_count == 3 && all_bytes(box.front, sizeof(box.front), FILL) && all_bytes(box.behind, sizeof(box.behind), FILL) && app->seen[3].rssi == 77 && app->seen[2].rssi == -88,
	      "three networks of a scan are kept, the fourth of the list is not read");
	remember();
	get("/api/wifi");
	check(answered(200, "wifi"), "app_web_wifi.json: GET /api/wifi with one stored network and a scan of three, one of them hidden");
	check(untouched(), "GET /api/wifi changes nothing of the app");
	check(strstr(reply, "geheim-123") == NULL && strcmp(app->profiles[0].password, "geheim-123") == 0, "the stored password is not in the answer");

	app_web_seen(app, found, 2);
	check(app->seen_count == 2, "a scan that found two networks replaces the three seen before");
	app_web_seen(app, found, 0);
	get("/api/wifi");
	check(answered_with(200, ",\"seen\":[]}") && app->seen_count == 0, "a scan that found nothing replaces the networks seen before");
	app_web_seen(app, found, 2);
	app_web_seen(app, found, -1);
	get("/api/wifi");
	check(answered_with(200, ",\"seen\":[]}") && app->seen_count == 0, "a count of -1 networks counts as none");
	app_web_seen(app, found, 1);
	get("/api/wifi");
	check(answered_with(200, ",\"seen\":[{\"ssid\":\"Werkstatt\",\"rssi\":-52,\"secure\":true}]}"), "a scan with one network");

	for(int i = 0; i < LINK_SEEN_MAX + 5; i++)
	{
		snprintf(found[i].ssid, sizeof(found[i].ssid), "Netz-%02d", i);
		found[i].rssi = -30 - i;
		found[i].secure = i % 2 == 0;
	}
	app_web_seen(app, found, LINK_SEEN_MAX - 1);
	get("/api/wifi");
	check(code == 200 && sound() && app->seen_count == 19 && strstr(reply, "{\"ssid\":\"Netz-18\",\"rssi\":-48,\"secure\":true}]}") != NULL, "19 networks of a scan are kept and listed");
	app_web_seen(app, found, LINK_SEEN_MAX);
	get("/api/wifi");
	check(code == 200 && sound() && app->seen_count == 20 && strstr(reply, "{\"ssid\":\"Netz-19\",\"rssi\":-49,\"secure\":false}]}") != NULL, "20 networks of a scan are kept and listed");
	app_web_seen(app, found, INT_MIN);
	check(app->seen_count == 0, "a count of INT_MIN networks counts as none");

	// The network the display is in is the one the platform names
	snprintf(name, sizeof(name), "%s", "Gast \"1\"");
	platform.ssid = name;
	app_platform(app, &platform);
	app_web_seen(app, found, 0);
	get("/api/wifi");
	check(answered_with(200, "{\"current\":\"Gast \\\"1\\\"\",\"profiles\":[{\"ssid\":\"Werkstatt\","), "the current network is the one the platform names, written as a JSON text");

	factory();
	start();
	get("/api/wifi");
	check(answered(200, "wifi_empty"), "app_web_wifi_empty.json: GET /api/wifi of a display fresh from the factory");
	strcpy(found[0].ssid, "Werkstatt");
	found[0].rssi = -52;
	found[0].secure = true;
	app_web_seen(app, found, 1);
	get("/api/wifi");
	check(answered_text(200, "{\"current\":\"\",\"profiles\":[],\"seen\":[{\"ssid\":\"Werkstatt\",\"rssi\":-52,\"secure\":true}]}") && !link_up(&app->link),
	      "the networks of a scan are listed while the display is in no network: that is when they are needed");
}

static void test_tickets(void)
{
	released();
	remember();
	get("/api/ticket?id=7");
	check(answered(200, "ticket_unknown") && routed == WEB_ROUTE_TICKET, "app_web_ticket_unknown.json: a ticket that was never given");
	check(untouched(), "GET /api/ticket changes nothing of the app");

	post("/api/reset", "");
	check(answered(202, "asked_1"), "app_web_asked_1.json: the first question gets the ticket 1");
	remember();
	get("/api/ticket?id=1");
	check(answered(200, "ticket_waiting"), "app_web_ticket_waiting.json: the ticket of a question that was asked just now: waiting, 60 s left");
	check(untouched(), "GET /api/ticket of a question that waits changes nothing of the app");
	get_at("/api/ticket?id=1", now + 59999);
	check(answered(200, "ticket_waiting_1s"), "app_web_ticket_waiting_1s.json: 59.999 s after the question: waiting, 1 s left");
	get_at("/api/ticket?id=1", now + 60000);
	check(answered(200, "ticket_expired"), "app_web_ticket_expired.json: 60 s after the question: expired");
	get("/api/ticket?id=2");
	check(answered_text(200, "{\"ticket\":2,\"state\":\"unknown\",\"left_s\":0}"), "the ticket behind the last one given is unknown, and the time of the question that waits is not its time");
	get("/api/ticket?id=01");
	check(answered(200, "ticket_waiting"), "the ticket 01 is the ticket 1");

	run(1500);
	short_press();
	get("/api/ticket?id=1");
	check(answered(200, "ticket_confirmed") && done.reset == 1, "app_web_ticket_confirmed.json: the ticket of a question the knob confirmed");

	released();
	post("/api/reset", "");
	long_press();
	get("/api/ticket?id=1");
	check(answered(200, "ticket_refused"), "app_web_ticket_refused.json: the ticket of a question a long press refused");
	run_to(20000);
	post("/api/wifi", "{\"ssid\":\"Neu\",\"password\":\"passwort1\"}");
	check(answered(202, "asked_2"), "app_web_asked_2.json: the second question gets the ticket 2");
	get_at("/api/ticket?id=2", now + 18000);
	check(answered(200, "ticket_second"), "app_web_ticket_second.json: the second ticket 18 s after its question: waiting, 42 s left");
	get_at("/api/ticket?id=1", now + 18000);
	check(answered(200, "ticket_first_of_two"), "app_web_ticket_first_of_two.json: the ticket before the last one keeps its end, and has no time left while the last one waits");
	get("/api/ticket?id=4294967295");
	check(answered_text(200, "{\"ticket\":4294967295,\"state\":\"unknown\",\"left_s\":0}"), "the largest ticket number is unknown");

	fresh_room();
	code = app_web_get(app, WEB_ROUTE_TICKET, 0, reply, &reply_length, now);
	check(answered_text(200, "{\"ticket\":0,\"state\":\"unknown\",\"left_s\":0}"), "the ticket 0, which web_route() never lets through, is unknown");
}

// Every route a function does not serve, and two numbers that are no route
static const int ROUTES[] = {
	WEB_ROUTE_NONE, WEB_ROUTE_PAGE, WEB_ROUTE_INFO, WEB_ROUTE_CATALOG, WEB_ROUTE_VALUES, WEB_ROUTE_LAYOUT, WEB_ROUTE_LAYOUT_CHECK, WEB_ROUTE_LAYOUT_APPLY, WEB_ROUTE_LAYOUT_SAVE,
	WEB_ROUTE_LAYOUT_RESET, WEB_ROUTE_DTC_LAST, WEB_ROUTE_WIFI, WEB_ROUTE_WIFI_STORE, WEB_ROUTE_WIFI_FORGET, WEB_ROUTE_SETTINGS, WEB_ROUTE_REBOOT, WEB_ROUTE_RESET, WEB_ROUTE_OTA,
	WEB_ROUTE_TICKET, WEB_ROUTE_TICKET + 1, -1,
};

static void test_other_routes(void)
{
	static const char wifi_body[] = "{\"ssid\":\"Werkstatt\"}";
	int get_refused = 0, layout_refused = 0, wifi_refused = 0, action_refused = 0;
	bool get_right = true, layout_right = true, wifi_right = true, action_right = true;

	released();
	remember();
	for(int i = 0; i < COUNT(ROUTES); i++)
	{
		web_route_t route = (web_route_t)ROUTES[i];
		bool reads = route == WEB_ROUTE_INFO || route == WEB_ROUTE_CATALOG || route == WEB_ROUTE_VALUES || route == WEB_ROUTE_LAYOUT || route == WEB_ROUTE_DTC_LAST ||
		             route == WEB_ROUTE_WIFI || route == WEB_ROUTE_TICKET;
		bool layouts = route == WEB_ROUTE_LAYOUT_CHECK || route == WEB_ROUTE_LAYOUT_APPLY || route == WEB_ROUTE_LAYOUT_SAVE || route == WEB_ROUTE_LAYOUT_RESET;
		bool wifis = route == WEB_ROUTE_WIFI_STORE || route == WEB_ROUTE_WIFI_FORGET;
		bool actions = route == WEB_ROUTE_REBOOT || route == WEB_ROUTE_RESET;

		if(!reads)
		{
			fresh_room();
			code = app_web_get(app, route, 1, reply, &reply_length, now + 500);
			get_right = get_right && answered(404, "not_found") && untouched();
			get_refused++;
		}
		if(!layouts)
		{
			fresh_room();
			code = app_web_layout(app, route, stored_layout, strlen(stored_layout), reply, &reply_length, now + 500);
			layout_right = layout_right && answered(404, "not_found") && untouched();
			layout_refused++;
		}
		if(!wifis)
		{
			fresh_room();
			code = app_web_wifi(app, route, wifi_body, strlen(wifi_body), reply, &reply_length, now + 500);
			wifi_right = wifi_right && answered(404, "not_found") && untouched();
			wifi_refused++;
		}
		if(!actions)
		{
			fresh_room();
			code = app_web_action(app, route, reply, &reply_length, now + 500);
			action_right = action_right && answered(404, "not_found") && untouched();
			action_refused++;
		}
	}
	check(get_right && get_refused == 14, "app_web_not_found.json: app_web_get() answers 404 for each of the 12 other routes and for two numbers that are no route, and changes nothing");
	check(layout_right && layout_refused == 17, "app_web_layout() answers 404 for each of the 15 other routes and for two numbers that are no route, and changes nothing, not even the time");
	check(wifi_right && wifi_refused == 19, "app_web_wifi() answers 404 for each of the 17 other routes and for two numbers that are no route, and changes nothing, not even the time");
	check(action_right && action_refused == 19, "app_web_action() answers 404 for each of the 17 other routes and for two numbers that are no route, and changes nothing, not even the time");
	check(app_take_events(app) == 0 && access_asking(&app->access, now) == ACCESS_ASK_NONE, "no function raises an event or asks a question for a route it does not serve");

	get("/");
	check(routed == WEB_ROUTE_PAGE && answered(404, "not_found"), "the page is the platform's: app_web_get() does not answer it");
}

/* Where app_web.h allows no pointer -------------------------------------------------------------------- */

// The calls without a body, without first bytes and without a list. Returns one bit for each that went wrong.
static int without_pointers(void)
{
	static web_seen_t found[2] = {{"Werkstatt", -52, true}, {"Freifunk", -88, false}};
	int wrong = 0;

	released();
	remember();
	fresh_room();
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_CHECK, NULL, 77, reply, &reply_length, now);
	if(!answered_with(400, "{\"ok\":false,\"path\":\"\",\"problem\":\"") || !untouched()) wrong |= 0x01;
	fresh_room();
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_APPLY, NULL, 99, reply, &reply_length, now);
	if(!answered_with(400, "{\"ok\":false,\"path\":\"\",\"problem\":\"") || !only_time()) wrong |= 0x02;
	fresh_room();
	code = app_web_wifi(app, WEB_ROUTE_WIFI_STORE, NULL, 40, reply, &reply_length, now);
	if(!answered(400, "body") || !nothing_asked()) wrong |= 0x04;
	fresh_room();
	code = app_web_wifi(app, WEB_ROUTE_WIFI_FORGET, NULL, 20, reply, &reply_length, now);
	if(!answered(400, "body") || app->profile_count != 1) wrong |= 0x08;
	fresh_room();
	code = app_web_settings(app, NULL, 30, reply, &reply_length, now);
	if(!answered(400, "member_none") || app_take_events(app) != 0) wrong |= 0x10;
	make_image("0.2.0");
	fresh_room();
	code = app_web_upload_begin(app, NULL, OTA_CHECK_BYTES, FILE_SIZE, SLOT, reply, &reply_length, now);
	if(!answered(422, "too_short") || app->uploading) wrong |= 0x20;
	app_web_seen(app, found, 2);
	app_web_seen(app, NULL, 2);
	get("/api/wifi");
	if(!answered_with(200, ",\"seen\":[]}") || app->seen_count != 0) wrong |= 0x40;
	return wrong;
}

// More networks than there is room for. Returns one bit for each call that went wrong.
static int too_many_networks(void)
{
	static web_seen_t found[LINK_SEEN_MAX + 5];
	int wrong = 0;

	drive();
	for(int i = 0; i < LINK_SEEN_MAX + 5; i++)
	{
		snprintf(found[i].ssid, sizeof(found[i].ssid), "Netz-%02d", i);
		found[i].rssi = -30 - i;
		found[i].secure = i % 2 == 0;
	}
	app_web_seen(app, found, LINK_SEEN_MAX + 1);
	get("/api/wifi");
	if(code != 200 || !sound() || app->seen_count != 20 || strstr(reply, "{\"ssid\":\"Netz-19\",\"rssi\":-49,\"secure\":false}]}") == NULL || strstr(reply, "Netz-20") != NULL) wrong |= 0x01;
	app_web_seen(app, found, LINK_SEEN_MAX + 5);
	if(app->seen_count != 20 || !all_bytes(box.front, sizeof(box.front), FILL) || !all_bytes(box.behind, sizeof(box.behind), FILL) || strcmp(app->seen[19].ssid, "Netz-19") != 0 ||
	   app->seen[0].rssi != -30 || !app->seen[0].secure || app->seen[1].secure || strcmp(app->ssid, "Werkstatt") != 0 || app->heat != GUARD_HEAT_NORMAL) wrong |= 0x02;
	app_web_seen(app, found, INT_MAX);
	if(app->seen_count != 20) wrong |= 0x04;
	return wrong;
}

// Runs the calls in a child process: a function that reads or writes what is not there is then a failed
// check, not the end of the test. Returns the bits of the calls that went wrong, all of them after a crash.
static int in_child(int (*calls)(void))
{
	int status = -1;
	pid_t child;

	fflush(stdout);
	child = fork();
	if(child == 0)
	{
		alarm(30);
		status = calls();
		fflush(stdout);
		_exit(status);
	}
	return child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) ? WEXITSTATUS(status) & 0x7F : 0x7F;
}

static void test_without_pointers(void)
{
	int wrong = in_child(without_pointers);

	check(!(wrong & 0x01), "a check without a body: 400, as for an empty text, and nothing changes");
	check(!(wrong & 0x02), "a layout to apply without a body: 400, nothing changes but the time and the release");
	check(!(wrong & 0x04), "a network request without a body: 400 body, nothing is asked");
	check(!(wrong & 0x08), "a forget request without a body: 400 body, nothing is forgotten");
	check(!(wrong & 0x10), "settings without a body: 400 with an empty name, no event");
	check(!(wrong & 0x20), "an upload without first bytes: 422 too_short, whatever their number is said to be");
	check(!(wrong & 0x40), "no list of networks counts as none, whatever the count says");

	wrong = in_child(too_many_networks);
	check(!(wrong & 0x01), "of 21 networks the first 20 are kept");
	check(!(wrong & 0x02), "of 25 networks the first 20 are kept, and nothing next to their room is written");
	check(!(wrong & 0x04), "of a count of INT_MAX networks 20 are read");
}

/* The layout ------------------------------------------------------------------------------------------- */

static layout_t parsed;

// The layout in use is what layout_parse() reads from `text`, and `text` is its text
static bool in_use(const char *text)
{
	size_t length = strlen(text);

	return layout_parse(text, length, &parsed, NULL, work, LAYOUT_TOKENS) && memcmp(&parsed, &app->layout, sizeof(parsed)) == 0 && app->layout_length == length &&
	       memcmp(app->layout_text, text, length + 1) == 0;
}

static void test_layout_check(void)
{
	static char refused[1024], warning[1024];
	bool loaded = read_fixture("fixtures/app_web_layout_refused.json", refused, sizeof(refused)) && read_fixture("fixtures/app_web_layout_warning.json", warning, sizeof(warning));

	check(loaded, "the layouts of the browser are there");

	// Without the release
	drive();
	remember();
	put("/api/layout?mode=check", stored_layout);
	check(answered(200, "report_stored") && routed == WEB_ROUTE_LAYOUT_CHECK, "app_web_report_stored.json: a layout that is taken is checked without the release: 200 with its report");
	check(only_room() && app_take_events(app) == 0 && in_use(builtin_text), "the check of a layout that is taken changes nothing but the room it was read into: not the views, not the time, no event");
	check(layout_parse(stored_layout, strlen(stored_layout), &parsed, NULL, work, LAYOUT_TOKENS) && memcmp(&parsed, &app->checked, sizeof(parsed)) == 0, "the layout that was checked lies in the room for it");
	remember();
	put("/api/layout?mode=check", refused);
	check(answered(400, "report_refused"), "app_web_report_refused.json: a layout that is refused: 400 with path and problem");
	check(untouched(), "the check of a layout that is refused changes nothing at all");
	put("/api/layout?mode=check", warning);
	check(answered(200, "report_warning"), "app_web_report_warning.json: a layout that is taken with a correction: 200 with the warning and the key the catalogue does not hold");
	remember();
	handler_lag = 5000;
	put("/api/layout?mode=check", stored_layout);
	handler_lag = 0;
	check(code == 200 && only_room(), "a check that comes with a later time does not take it over");
	put("/api/layout?mode=check", "");
	check(answered_with(400, "{\"ok\":false,\"path\":\"\",\"problem\":\""), "a check of an empty text: 400");

	// The keys are judged by the catalogue in use
	other_vehicle();
	put("/api/layout?mode=check", stored_layout);
	check(answered(200, "report_other"), "app_web_report_other.json: checked on another vehicle, the report names the keys its catalogue does not hold");

	// The check does not need the release, the other three do
	drive();
	remember();
	put("/api/layout?mode=apply", stored_layout);
	check(by_route && answered(403, "locked") && untouched(), "app_web_locked.json: without the release web_route() refuses to apply a layout");
	put("/api/layout?mode=save", stored_layout);
	check(by_route && answered(403, "locked") && untouched(), "without the release web_route() refuses to save a layout");
	post("/api/layout/reset", "");
	check(by_route && answered(403, "locked") && untouched(), "without the release web_route() refuses to reset the layout");
}

static void test_layout_change(void)
{
	static char warning[1024], hidden[1024], refused[1024], largest[LAYOUT_TEXT_MAX + 1];
	// Three pages, none hidden: the second names a value no profile of the harness has. In a room with bytes
	// to spare, as the other bodies: a function that reads behind the length it was given must fail a check
	// here, not end the test.
	static char unknown_middle[1024] = "{\"format\":\"wican-display-layout\",\"v\":1,\"name\":\"Mitte fehlt\",\"pages\":["
	                                     "{\"title\":\"A\",\"items\":[{\"key\":\"ENGINE_RPM\"}]},"
	                                     "{\"title\":\"B\",\"items\":[{\"key\":\"TURBO_SPEED\"}]},"
	                                     "{\"title\":\"C\",\"items\":[{\"key\":\"FUEL_L\"}]}]}";
	size_t length;
	bool loaded = read_fixture("fixtures/app_web_layout_warning.json", warning, sizeof(warning)) && read_fixture("fixtures/app_web_layout_hidden.json", hidden, sizeof(hidden)) &&
	              read_fixture("fixtures/app_web_layout_refused.json", refused, sizeof(refused));

	check(loaded, "the layouts the browser applies are there");

	// The release ends between the look of web_route() and the function
	released();
	run_to(602000);
	turn(2);
	remember();
	handler_lag = 100;
	put("/api/layout?mode=apply", stored_layout);
	check(!by_route && answered(403, "locked") && only_time() && app->clock_ms == 602100, "the release ends before the function runs: a layout is not applied, 403; only the time is taken over");
	handler_lag = 0;
	released();
	turn(2);
	remember();
	meanwhile = close_release;
	put("/api/layout?mode=save", stored_layout);
	meanwhile = NULL;
	check(!by_route && answered(403, "locked") && only_time() && carried() == 0, "the release is taken back before the function runs: a layout is not saved");
	released();
	turn(2);
	remember();
	meanwhile = close_release;
	post("/api/layout/reset", "");
	meanwhile = NULL;
	check(!by_route && answered(403, "locked") && only_time() && carried() == 0 && app->nav.page == 2, "the release is taken back before the function runs: the layout is not reset, the page shown stays");

	// Apply
	released();
	turn(2);
	check(app->nav.page == 2 && app->source == APP_LAYOUT_BUILTIN, "the scene of the third built-in page");
	run_to(100000);
	put("/api/layout?mode=apply", stored_layout);
	check(answered(200, "report_stored") && routed == WEB_ROUTE_LAYOUT_APPLY, "a layout that is taken is applied: 200 with its report");
	check(in_use(stored_layout) && app->source == APP_LAYOUT_PREVIEW && app->nav.page == 0 && carried() == 0 && !flash.has_layout,
	      "the applied layout is the layout in use with its text, source preview; it has two pages and none at the position of the third, so it is shown from its first; nothing is stored");
	check(access_is_open(&app->access, 699999) && !access_is_open(&app->access, 700000), "a layout applied at 100000 renews the release until 700000");
	sees("preview", "the applied layout is on the screen at once");
	remember();
	put("/api/layout?mode=apply", refused);
	check(answered(400, "report_refused") && only_time() && in_use(stored_layout), "a layout that is refused is not applied: 400 with the report, the views stay");
	run_to(150000);
	put("/api/layout?mode=apply", refused);
	check(code == 400 && access_is_open(&app->access, 749999) && !access_is_open(&app->access, 750000), "a layout refused at 150000 has renewed the release all the same, until 750000");

	// The page shown stays where the new layout shows a page at its position
	turn(1);
	check(app->nav.page == 1, "the scene of the second page of a preview");
	put("/api/layout?mode=apply", builtin_text);
	check(code == 200 && in_use(builtin_text) && app->source == APP_LAYOUT_PREVIEW && app->nav.page == 1,
	      "another layout applied while the second page shows: its second page is shown, the display does not jump back to the first");
	shows("page_ladeluft", "the second page of the applied layout is on the screen at once");
	put("/api/layout?mode=apply", builtin_text);
	check(code == 200 && app->nav.page == 1, "the same layout applied again keeps the page as well");
	turn(5);
	check(app->nav.page == 6, "the scene of the seventh page of a preview");
	put("/api/layout?mode=apply", stored_layout);
	check(code == 200 && in_use(stored_layout) && app->nav.page == 0,
	      "a layout of two pages applied while the seventh page shows: it has no page at that position - the value pages start at its first, not at its last");

	// A page at the position that the layout does not show is no page to stay on
	put("/api/layout?mode=apply", warning);
	check(answered(200, "report_warning") && in_use(warning) && app->nav.page == 1,
	      "a layout whose page at the position shown, the first, is hidden: the first page it shows, its second");
	put("/api/layout?mode=apply", hidden);
	check(answered(200, "report_hidden") && in_use(hidden) && app->nav.page == -1, "app_web_report_hidden.json: a layout without a page to show is applied, no page is shown");
	put("/api/layout?mode=apply", stored_layout);
	check(code == 200 && app->nav.page == 0, "a layout applied while no page is shown starts at its first page");
	turn(1);
	put("/api/layout?mode=apply", unknown_middle);
	check(code == 200 && in_use(unknown_middle) && app->layout.page_count == 3 && !app->layout.pages[1].hidden && app->nav.page == 0,
	      "a layout whose page at the position shown, the second, holds no value of the profile: it is not shown - the first page");
	put("/api/layout?mode=apply", builtin_text);
	turn(2);
	put("/api/layout?mode=apply", unknown_middle);
	check(code == 200 && app->nav.page == 2, "the same layout applied while the third page shows: its third page, behind the one that is not shown, stays in front");

	// Below another screen the page is kept by the same rule
	put("/api/layout?mode=apply", builtin_text);
	short_press();
	check(on(NAV_MENU) && app->nav.page == 2, "the scene of the menu over the third page");
	put("/api/layout?mode=apply", stored_layout);
	check(code == 200 && on(NAV_MENU) && app->nav.page == 0, "a layout of two pages applied while the menu lies over the third page: the menu stays, the value pages below start at its first page");
	long_press();
	turn(1);
	short_press();
	turn(3);
	check(on(NAV_MENU) && app->nav.row == 3 && app->nav.page == 1, "the scene of the menu, the focus on its fourth row, over the second page");
	put("/api/layout?mode=apply", builtin_text);
	check(code == 200 && on(NAV_MENU) && app->nav.row == 3 && app->nav.page == 1, "a layout applied while the menu lies over the second page: menu and focus stay, and its second page waits below");
	long_press();
	check(on(NAV_PAGES) && app->nav.page == 1, "back from the menu the second page of the applied layout is shown");

	// The layout with the most values there can be: 12 pages of 6 state widgets with 8 texts each
	length = (size_t)snprintf(largest, sizeof(largest), "{\"format\":\"wican-display-layout\",\"v\":1,\"name\":\"Gross\",\"pages\":[");
	for(int page = 0; page < LAYOUT_PAGES_MAX; page++)
	{
		length += (size_t)snprintf(largest + length, sizeof(largest) - length, "%s{\"title\":\"S%d\",\"items\":[", page > 0 ? "," : "", page);
		for(int item = 0; item < LAYOUT_ITEMS_MAX; item++)
		{
			length += (size_t)snprintf(largest + length, sizeof(largest) - length, "%s{\"key\":\"ENGINE_RPM\",\"label\":\"L\",\"unit\":\"u\",\"scale\":2,\"dec\":1,\"widget\":\"state\",\"min\":0,\"max\":9,"
			                           "\"warn_lo\":1,\"warn_hi\":8,\"crit_lo\":0,\"crit_hi\":9,\"map\":{\"0\":\"a\",\"1\":\"b\",\"2\":\"c\",\"3\":\"d\",\"4\":\"e\",\"5\":\"f\",\"6\":\"g\",\"*\":\"h\"}}",
			                           item > 0 ? "," : "");
		}
		length += (size_t)snprintf(largest + length, sizeof(largest) - length, "]}");
	}
	length += (size_t)snprintf(largest + length, sizeof(largest) - length, "]}");
	check(length < LAYOUT_TEXT_MAX && json_parse(largest, length, work, LAYOUT_TOKENS) > LAYOUT_TOKENS / 2, "the scene of a layout that needs more than half of the tokens the app was given");
	put("/api/layout?mode=check", largest);
	check(answered_text(200, "{\"ok\":true,\"name\":\"Gross\",\"pages\":12,\"items\":72,\"warnings\":0,\"warning_path\":\"\",\"warning\":\"\",\"unknown\":[]}"),
	      "a layout of 12 pages with 6 state widgets each is checked with all the tokens of the app: 200");
	put("/api/layout?mode=apply", largest);
	check(code == 200 && in_use(largest) && app->layout.page_count == 12 && app->layout.pages[11].items[5].map_count == 8, "the layout of 72 state widgets is applied");

	// A preview is not stored: a restart ends it
	start();
	run(2100);
	check(app->source == APP_LAYOUT_BUILTIN && in_use(builtin_text), "a restart ends the preview: nothing was stored, the built-in views are in use");

	// Save
	released();
	turn(3);
	put("/api/layout?mode=save", stored_layout);
	check(answered(200, "report_stored") && routed == WEB_ROUTE_LAYOUT_SAVE, "a layout that is taken is saved: 200 with its report");
	check(in_use(stored_layout) && app->source == APP_LAYOUT_STORED && app->nav.page == 0 && done.layout == 1 && carried() == 1 && done.last == APP_EVENT_STORE_LAYOUT,
	      "the saved layout is the layout in use, source stored; it has no page at the position of the fourth, so it is shown from its first; "
	      "the platform is asked to store it, and nothing else");
	check(flash.has_layout && strcmp(flash.layout, stored_layout) == 0, "the text of the saved layout is in the flash, byte for byte");
	remember();
	put("/api/layout?mode=save", refused);
	check(answered(400, "report_refused") && only_time() && carried() == 1 && strcmp(flash.layout, stored_layout) == 0, "a layout that is refused is not saved: 400, no event, the stored one stays");
	turn(1);
	put("/api/layout?mode=save", stored_layout);
	check(code == 200 && app->nav.page == 1 && done.layout == 2, "the same layout saved again while its second page shows is stored again, and the second page stays in front");
	shows("stored_page2", "the second page of the saved layout is still on the screen");
	put("/api/layout?mode=save", builtin_text);
	check(code == 200 && app->source == APP_LAYOUT_STORED && in_use(builtin_text) && app->nav.page == 1 && done.layout == 3,
	      "another layout saved while the second page shows: its second page is shown");
	turn(5);
	put("/api/layout?mode=save", stored_layout);
	check(code == 200 && app->nav.page == 0 && done.layout == 4, "a layout of two pages saved while the seventh page shows: the value pages start at its first");
	put("/api/layout?mode=apply", warning);
	check(code == 200 && app->source == APP_LAYOUT_PREVIEW && done.layout == 4 && strcmp(flash.layout, stored_layout) == 0, "a layout applied over a saved one is a preview: the stored one stays in the flash");
	start();
	run(2100);
	check(app->source == APP_LAYOUT_STORED && in_use(stored_layout), "after a restart the saved layout is in use, the preview is gone");
	get("/api/layout");
	check(answered_text(200, stored_layout), "after the restart GET /api/layout answers the text that was saved");

	// Reset
	app_do(app, NAV_DO_RELEASE_ON, now);
	turn(1);
	memset(&done, 0, sizeof(done));
	serve(WEB_POST, "/api/layout/reset", "{\"format\":\"wican-display-layout\"", 32, now);
	check(answered(200, "report_builtin") && routed == WEB_ROUTE_LAYOUT_RESET, "app_web_report_builtin.json: the reset answers the report of the built-in views, whatever its body is");
	check(in_use(builtin_text) && app->source == APP_LAYOUT_BUILTIN && app->nav.page == 0 && done.erase == 1 && carried() == 1 && done.last == APP_EVENT_ERASE_LAYOUT && !flash.has_layout,
	      "the reset puts the built-in views in use from their first page and asks the platform to remove the stored layout, and nothing else");
	run(100);
	sees("motor", "after the reset the first built-in page is on the screen");
	turn(4);
	post("/api/layout/reset", "");
	check(code == 200 && app->nav.page == 0 && done.erase == 2 && app->source == APP_LAYOUT_BUILTIN, "a reset while the built-in views are in use raises the event again and goes to their first page");
	fresh_room();
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_RESET, NULL, 0, reply, &reply_length, now);
	check(answered(200, "report_builtin") && app_take_events(app) == APP_EVENT_ERASE_LAYOUT, "a reset without a body is a reset");
	start();
	run(2100);
	check(app->source == APP_LAYOUT_BUILTIN && in_use(builtin_text), "after a restart the built-in views are in use: the stored layout is gone");

	// A preview over a stored layout: the reset removes the stored one
	released();
	put("/api/layout?mode=save", stored_layout);
	put("/api/layout?mode=apply", warning);
	check(app->source == APP_LAYOUT_PREVIEW && flash.has_layout, "the scene of a preview over a stored layout");
	post("/api/layout/reset", "");
	check(answered(200, "report_builtin") && app->source == APP_LAYOUT_BUILTIN && !flash.has_layout && done.erase == 1, "a reset while a preview lies over a stored layout removes the stored one and ends the preview");

	// Reset on another vehicle
	other_vehicle();
	app_do(app, NAV_DO_RELEASE_ON, now);
	put("/api/layout?mode=save", stored_layout);
	check(answered(200, "report_other") && app->source == APP_LAYOUT_STORED, "a layout saved on another vehicle is taken with the keys its catalogue does not hold");
	turn(1);
	post("/api/layout/reset", "");
	check(answered(200, "report_generated") && app->source == APP_LAYOUT_GENERATED && app->nav.page == 0 && app->layout.page_count == 2 && strcmp(app->layout.pages[0].items[0].key, "SPEED") == 0 &&
	      !flash.has_layout, "app_web_report_generated.json: the reset on another vehicle puts views made from its catalogue in use and answers their report");
	turn(1);
	post("/api/layout/reset", "");
	check(code == 200 && app->source == APP_LAYOUT_GENERATED && app->nav.page == 0, "a reset while generated views are in use goes to their first page");
}

/* The networks ----------------------------------------------------------------------------------------- */

#define NEU     "{\"ssid\":\"Neu\",\"password\":\"passwort1\",\"host\":\"10.0.0.5\"}"

static void test_wifi_store(void)
{
	// Refused by web_route() without the release
	drive();
	remember();
	post("/api/wifi", NEU);
	check(by_route && answered(403, "locked") && untouched(), "without the release web_route() refuses to store a network");

	// The order of the refusals of the function itself
	released();
	run_to(10000);
	remember();
	meanwhile = close_release;
	post("/api/wifi", NEU);
	meanwhile = NULL;
	check(!by_route && answered(403, "locked") && nothing_asked() && access_ticket(&app->access, 1, now) == ACCESS_TICKET_UNKNOWN, "the release is taken back before the function runs: no network is asked for, 403");

	released();
	run_to(10000);
	remember();
	app->uploading = true;
	post("/api/wifi", NEU);
	check(!by_route && answered(409, "busy") && nothing_asked() && access_asking(&app->access, now) == ACCESS_ASK_NONE, "app_web_busy.json: while a firmware upload runs no network is asked for: 409 busy");
	before.uploading = true;
	check(only_time() && access_is_open(&app->access, 602099) && !access_is_open(&app->access, 602100), "a question that is refused as busy leaves everything as it was, and does not renew the release");
	app->uploading = false;

	post("/api/wifi", "{\"ssid\":\"Erstes\",\"password\":\"passwort-eins\"}");
	check(answered(202, "asked_1") && app->has_wifi_asked, "the scene of a question that waits");
	remember();
	post("/api/wifi", NEU);
	check(!by_route && answered(409, "asking") && only_time() && strcmp(app->wifi_asked.ssid, "Erstes") == 0 && strcmp(app->wifi_asked.password, "passwort-eins") == 0 &&
	      strcmp(app->ask_detail, "Erstes") == 0 && access_ticket(&app->access, 2, now) == ACCESS_TICKET_UNKNOWN,
	      "app_web_asking.json: while a question waits a second network is not asked for: 409 asking, and the first one stays as it was asked");
	post("/api/wifi", "kein JSON");
	check(answered(409, "asking"), "while a question waits a body that cannot be read is answered asking, not body");
	app->uploading = true;
	post("/api/wifi", NEU);
	check(answered(409, "busy"), "while a question waits and an upload runs the answer is busy, not asking");
	app->uploading = false;
	meanwhile = close_release;
	post("/api/wifi", "kein JSON");
	meanwhile = NULL;
	check(!by_route && answered(403, "locked"), "the release is taken back before the function runs: a body that cannot be read is answered locked, not body");
	app_do(app, NAV_DO_RELEASE_ON, now);
	post("/api/wifi", "{\"ssid\":\"Erstes\",\"password\":\"passwort-eins\"}");
	app->uploading = true;
	meanwhile = close_release;
	post("/api/wifi", NEU);
	meanwhile = NULL;
	check(answered(403, "locked"), "while an upload runs and the release is closed the answer is locked, not busy");
	app->uploading = false;

	// A fault memory request of the display keeps the question away: the answer of the knob would take the
	// adapter from under it
	released();
	run_to(10000);
	app_do(app, NAV_DO_READ, now);
	remember();
	post("/api/wifi", NEU);
	check(!by_route && answered(409, "busy") && only_time() && nothing_asked() && access_asking(&app->access, now) == ACCESS_ASK_NONE && phase() == DTC_FLOW_READ_SENT,
	      "while a read of the display waits to be sent no network is asked for: 409 busy, and nothing changes but the time");
	check(access_is_open(&app->access, 602099) && !access_is_open(&app->access, 602100) && access_ticket(&app->access, 1, now) == ACCESS_TICKET_UNKNOWN,
	      "the network question that was refused as busy has not renewed the release, and no ticket was given");
	run(1000);
	post("/api/wifi", NEU);
	check(answered(409, "busy") && phase() == DTC_FLOW_READING && nothing_asked(), "while the adapter reads for the display no network is asked for either");
	post("/api/wifi", "kein JSON");
	check(answered(409, "busy"), "while a read is under way a body that cannot be read is answered busy, not body");
	run(6000);
	post("/api/wifi", NEU);
	check(answered(202, "asked_1") && phase() == DTC_FLOW_LIST && sent[POLL_DTC_READ] == 1, "when the read has ended with its list the network is asked for");

	// The clear dialog alone does not: nothing is sent yet, and no hold counts under the question
	listed();
	open_dialog();
	post("/api/wifi", NEU);
	check(answered(202, "asked_1") && app_busy(app) && on(NAV_DTC_CONFIRM) && has_line("over: ask") && has_line("over_line: WLAN speichern?"),
	      "while the clear dialog shows a network is asked for all the same: the question lies over the dialog");

	// The body
	released();
	run_to(10000);
	remember();
	post("/api/wifi", "{\"ssid\":\"\"}");
	check(!by_route && answered(400, "body") && only_time() && nothing_asked() && access_asking(&app->access, now) == ACCESS_ASK_NONE, "app_web_body.json: a network without a name: 400 body, nothing is asked");
	check(access_is_open(&app->access, 602099) && !access_is_open(&app->access, 602100), "a network request with a body that is refused does not renew the release");
	post("/api/wifi", "{\"ssid\":\"Neu\",\"password\":\"kurz\"}");
	check(answered(400, "body") && nothing_asked(), "a network with a password of four bytes: 400 body");
	post("/api/wifi", "");
	check(answered(400, "body"), "a network request with an empty body: 400 body");

	released();
	fresh_room();
	code = app_web_wifi(app, WEB_ROUTE_WIFI_STORE, NEU, strlen(NEU), reply, &reply_length, 602099);
	check(answered(202, "asked_1") && app->access.asking_since_ms == 602099, "a network asked for in the last millisecond of the release is asked for, at that time");
	released();
	fresh_room();
	code = app_web_wifi(app, WEB_ROUTE_WIFI_STORE, NEU, strlen(NEU), reply, &reply_length, 602100);
	check(answered(403, "locked") && nothing_asked(), "a network asked for in the millisecond the release ends is not asked for: 403");

	// Asked
	released();
	run_to(10000);
	post("/api/wifi", NEU);
	check(answered(202, "asked_1") && routed == WEB_ROUTE_WIFI_STORE, "a network that is asked for: 202 with the ticket");
	check(app->has_wifi_asked && strcmp(app->wifi_asked.ssid, "Neu") == 0 && strcmp(app->wifi_asked.password, "passwort1") == 0 && app->wifi_asked.has_password &&
	      strcmp(app->wifi_asked.host, "10.0.0.5") == 0 && strcmp(app->ask_detail, "Neu") == 0, "the network asked for is kept as it was sent, the detail of the question is its SSID");
	check(access_asking(&app->access, now) == ACCESS_ASK_WIFI && access_ticket(&app->access, 1, now) == ACCESS_TICKET_WAITING && carried() == 0 && app->profile_count == 1,
	      "the question waits for the knob: nothing is stored yet");
	check(access_is_open(&app->access, 609999) && !access_is_open(&app->access, 610000), "a question asked at 10000 renews the release until 610000");
	sees("ask_wifi", "the question lies over the value page with the SSID and 60 seconds");

	released();
	post("/api/wifi", "{\"ssid\":\"Ein Netz mit dem laengsten Namen\",\"password\":\"0123456789012345678901234567890123456789012345678901234567890123\",\"host\":\"ein-rechner-mit-39-zeichen.example.org.\"}");
	check(code == 202 && strcmp(app->ask_detail, "Ein Netz mit dem laengsten Namen") == 0 && strlen(app->wifi_asked.ssid) == 32 && strlen(app->wifi_asked.password) == 64 && strlen(app->wifi_asked.host) == 39,
	      "a network with the longest name, password and host there can be is asked for with its whole name as the detail");
}

static void test_wifi_story(void)
{
	// Confirmed at the knob, stored, joined
	released();
	strcpy(wifi.in_range[1], "Neu");
	wifi.in_range_count = 2;
	wifi.found = "192.168.7.7";
	wifi.scans = wifi.joins = wifi.leaves = 0;
	post("/api/wifi", "{\"ssid\":\"Neu\",\"password\":\"passwort1\"}");
	check(code == 202 && strcmp(app->wifi_asked.host, "") == 0, "the story of a new network: asked for without a host");
	run(1500);
	short_press();
	check(done.wifi == 1 && carried() == 1 && nothing_asked() && access_ticket(&app->access, 1, now) == ACCESS_TICKET_CONFIRMED, "the knob confirms the question: the platform is asked to store the networks");
	check(flash.profile_count == 2 && strcmp(flash.profiles[0].ssid, "Neu") == 0 && strcmp(flash.profiles[0].password, "passwort1") == 0 && strcmp(flash.profiles[1].ssid, "Werkstatt") == 0,
	      "both networks are in the flash, the new one first");
	check(wifi.leaves == 1 && wifi.scans == 1 && wifi.joins == 1 && wifi.profile == 0 && strcmp(app_host(app), "192.168.7.7") == 0, "the display leaves its network and joins the new one");
	run(1200);
	get("/api/wifi");
	check(answered(200, "wifi_new"), "app_web_wifi_new.json: GET /api/wifi names the new network as the current one and lists both, without a password");
	check(strstr(reply, "passwort1") == NULL && strstr(reply, "geheim-123") == NULL, "neither the password that was sent nor the one that was stored is in the list");

	// Kept: the stored password of a network asked for without one
	released();
	post("/api/wifi", "{\"ssid\":\"Werkstatt\",\"host\":\"192.168.1.60\"}");
	check(code == 202 && !app->wifi_asked.has_password, "the story of a new host for a stored network: asked for without a password");
	run(1500);
	short_press();
	check(app->profile_count == 1 && profile_is(0, "Werkstatt", "geheim-123", "192.168.1.60") && done.wifi == 1, "confirmed: the stored network keeps its password and gets the new host");
	post("/api/wifi", "{\"ssid\":\"Offen\"}");
	run(1500);
	short_press();
	check(app->profile_count == 2 && profile_is(0, "Offen", "", "") && done.wifi == 2, "a network that is not stored, asked for without a password and confirmed, is stored as an open one");

	// Refused at the knob
	released();
	post("/api/wifi", NEU);
	run(1500);
	long_press();
	get("/api/ticket?id=1");
	check(answered(200, "ticket_refused") && carried() == 0 && app->profile_count == 1 && nothing_asked() && access_is_open(&app->access, now),
	      "a long press refuses the network: nothing is stored, what was asked for is dropped, the release goes on");
	run(2000);
	short_press();
	check(on(NAV_MENU) && carried() == 0 && app->profile_count == 1, "a press after the refusal is a press on the screen: the network that was refused is not stored by it");

	// Expired
	released();
	post("/api/wifi", NEU);
	run_to(62080);
	check(access_asking(&app->access, now) == ACCESS_ASK_WIFI && has_line("over: ask"), "59.98 s after the question it still waits");
	run_to(62300);
	get("/api/ticket?id=1");
	check(answered(200, "ticket_expired") && nothing_asked() && carried() == 0 && !has_line("over: ask"), "60 s after the question it has expired: what it asked for is dropped, password included");
	short_press();
	check(on(NAV_MENU) && carried() == 0 && app->profile_count == 1, "a press after the question expired is a press on the screen: nothing is stored");

	// What an earlier question left behind
	released();
	post("/api/wifi", "{\"ssid\":\"Ein langer Netzwerkname\",\"password\":\"ein-langes-passwort-0123456789\",\"host\":\"adapter.example.org\"}");
	app_do(app, NAV_DO_ASK_REFUSE, now + 60000);
	app->has_wifi_asked = true;
	strcpy(app->wifi_asked.ssid, "Ein langer Netzwerkname");
	strcpy(app->wifi_asked.password, "ein-langes-passwort-0123456789");
	strcpy(app->wifi_asked.host, "adapter.example.org");
	strcpy(app->ask_detail, "Ein langer Netzwerkname");
	post("/api/wifi", "{\"ssid\":\"Kurz\"}");
	check(code == 202 && strcmp(app->wifi_asked.ssid, "Kurz") == 0 && all_bytes(app->wifi_asked.ssid + 4, NET_SSID_SIZE - 4, 0) && all_bytes(app->wifi_asked.password, NET_PASSWORD_SIZE, 0) &&
	      all_bytes(app->wifi_asked.host, NET_HOST_SIZE, 0) && !app->wifi_asked.has_password && strcmp(app->ask_detail, "Kurz") == 0,
	      "a new network replaces what an earlier question left behind: no byte of the old name, password and host stays");
	app_do(app, NAV_DO_ASK_REFUSE, now + 60000);
	app->has_wifi_asked = true;
	strcpy(app->wifi_asked.ssid, "Rest");
	strcpy(app->wifi_asked.password, "rest-passwort");
	strcpy(app->wifi_asked.host, "rest.example.org");
	strcpy(app->ask_detail, "Rest");
	remember();
	fresh_room();
	code = app_web_wifi(app, WEB_ROUTE_WIFI_STORE, "{\"ssid\":\"Neu\",\"password\":\"kurz\"}", 34, reply, &reply_length, now + 60000);
	check(answered(400, "body") && only_time() && strcmp(app->wifi_asked.ssid, "Rest") == 0 && strcmp(app->wifi_asked.password, "rest-passwort") == 0 && strcmp(app->ask_detail, "Rest") == 0,
	      "a network request that is refused for its body leaves what an earlier question left behind as it was, until the tick drops it");
	fresh_room();
	code = app_web_action(app, WEB_ROUTE_RESET, reply, &reply_length, now + 60000);
	check(answered(202, "asked_3") && nothing_asked(), "app_web_asked_3.json: the question of the factory reset drops the network an earlier question left behind, and its detail");
}

static void test_wifi_forget(void)
{
	drive();
	remember();
	post("/api/wifi/forget", "{\"ssid\":\"Werkstatt\"}");
	check(by_route && answered(403, "locked") && untouched(), "without the release web_route() refuses to forget a network");

	released();
	run_to(602000);
	remember();
	handler_lag = 100;
	post("/api/wifi/forget", "{\"ssid\":\"Werkstatt\"}");
	handler_lag = 0;
	check(!by_route && answered(403, "locked") && only_time() && app->profile_count == 1 && carried() == 0, "the release ends before the function runs: no network is forgotten, 403");

	released();
	run_to(10000);
	remember();
	post("/api/wifi/forget", "{\"ssid\":\"Garage\"}");
	check(answered(404, "not_found") && only_time() && carried() == 0 && link_up(&app->link), "a network that is not stored cannot be forgotten: 404, the list and the link stay");
	check(access_is_open(&app->access, 609999) && !access_is_open(&app->access, 610000), "the request for a network that is not stored has renewed the release, until 610000");
	remember();
	post("/api/wifi/forget", "{\"name\":\"Werkstatt\"}");
	check(answered(400, "body") && only_time() && app->profile_count == 1, "a forget request without an SSID: 400 body, nothing is forgotten");
	post("/api/wifi/forget", "");
	check(answered(400, "body"), "a forget request with an empty body: 400 body");

	wifi.leaves = wifi.aps_on = 0;
	handler_lag = 15;
	post("/api/wifi/forget", "{\"ssid\":\"Werkstatt\"}");
	handler_lag = 0;
	check(answered(200, "ok") && routed == WEB_ROUTE_WIFI_FORGET, "app_web_ok.json: a stored network is forgotten: 200");
	check(app->clock_ms == 10015 && app->link.clock_ms == 10015, "the link and the app are told the time of the request, 10015");
	check(app->profile_count == 0 && all_bytes(app->profiles, sizeof(app->profiles), 0) && done.wifi == 1 && carried() == 1 && flash.profile_count == 0 && all_bytes(flash.profiles, sizeof(flash.profiles), 0),
	      "the forgotten network is gone from the list and from the flash, password included");
	check(!link_up(&app->link) && app->link.profile_count == 0 && !app->poll.wifi && app_host(app)[0] == '\0' && view() == CONN_VIEW_NO_WIFI,
	      "with the request the link has the new list and the poll knows that the address of the adapter was forgotten");
	run(100);
	check(wifi.leaves == 1 && wifi.aps_on == 1 && wifi.ap_on, "the display leaves the network and, without any network stored, opens its own access point");
	run(1000);
	get("/api/wifi");
	check(answered(200, "wifi_empty"), "after the last network was forgotten GET /api/wifi lists none");
	start();
	check(app->profile_count == 0, "after a restart the forgotten network is still gone");

	garage();
	strcpy(flash.profiles[1].ssid, "Ein Netz mit dem laengsten Namen");
	strcpy(flash.profiles[1].password, "zelt-und-wurst");
	flash.profile_count = 2;
	start();
	app_do(app, NAV_DO_RELEASE_ON, now);
	post("/api/wifi/forget", "{\"ssid\":\"Ein Netz mit dem laengsten Namen\"}");
	check(answered(200, "ok") && app->profile_count == 1 && profile_is(0, "Werkstatt", "geheim-123", "192.168.1.50"), "a network with a name of 32 bytes is forgotten");

	// One of two, and a fault memory request under way: the display would leave its network in the middle of it
	garage();
	strcpy(flash.profiles[1].ssid, "Camping");
	strcpy(flash.profiles[1].password, "zelt-und-wurst");
	flash.profile_count = 2;
	start();
	run(2100);
	memset(&done, 0, sizeof(done));
	app_do(app, NAV_DO_RELEASE_ON, now);
	run_to(10000);
	app_do(app, NAV_DO_READ, now);
	remember();
	post("/api/wifi/forget", "{\"ssid\":\"Camping\"}");
	check(!by_route && answered(409, "busy") && only_time() && carried() == 0 && phase() == DTC_FLOW_READ_SENT && app->profile_count == 2,
	      "while a read of the display waits to be sent no network is forgotten: 409 busy, and nothing changes but the time and the release");
	check(access_is_open(&app->access, 609999) && !access_is_open(&app->access, 610000), "the forget request that was refused as busy at 10000 has renewed the release, as every change that found it open");
	run(1000);
	check(phase() == DTC_FLOW_READING, "the scene of a read the adapter accepted, two networks stored");
	remember();
	post("/api/wifi/forget", "{\"ssid\":\"Camping\"}");
	check(answered(409, "busy") && only_time() && app->profile_count == 2 && profile_is(1, "Camping", "zelt-und-wurst", "") && flash.profile_count == 2 && link_up(&app->link),
	      "while the adapter reads for the display the second of two networks is not forgotten: the display would leave its network for the new list, whichever network is named");
	post("/api/wifi/forget", "{\"ssid\":\"Garage\"}");
	check(answered(409, "busy"), "while a read is under way a network that is not stored is answered busy, not not_found");
	post("/api/wifi/forget", "kein JSON");
	check(answered(409, "busy"), "while a read is under way a forget request that cannot be read is answered busy, not body");
	meanwhile = close_release;
	post("/api/wifi/forget", "{\"ssid\":\"Camping\"}");
	meanwhile = NULL;
	check(!by_route && answered(403, "locked") && app->profile_count == 2, "the release is taken back while a read is under way: the forget request is answered locked, not busy");
	app_do(app, NAV_DO_RELEASE_ON, now);
	run(6000);
	check(phase() == DTC_FLOW_LIST && app->poll.has_list && sent[POLL_DTC_READ] == 1 && link_up(&app->link), "nothing took the adapter away: the read went on and its list is there");
	post("/api/wifi/forget", "{\"ssid\":\"Camping\"}");
	check(answered(200, "ok") && app->profile_count == 1 && profile_is(0, "Werkstatt", "geheim-123", "192.168.1.50") && all_bytes(&app->profiles[1], sizeof(app->profiles[1]), 0) && flash.profile_count == 1,
	      "when the read has ended the second of two networks is forgotten: the first stays, the room of the second is emptied");
	check(!link_up(&app->link) && app_host(app)[0] == '\0', "the display leaves its network for the new list also when another network was forgotten");

	// ... and while it clears: the knob is held on Löschen for 3000 ms
	listed();
	open_dialog();
	turn(1);
	run(400);
	switch_pressed = true;
	run(3020);
	switch_pressed = false;
	run(200);
	check(phase() == DTC_FLOW_CLEARING && sent[POLL_DTC_CLEAR] == 1, "the scene of a clear the adapter accepted");
	post("/api/wifi/forget", "{\"ssid\":\"Werkstatt\"}");
	check(answered(409, "busy") && app->profile_count == 1 && link_up(&app->link) && done.wifi == 0, "while the adapter clears for the display no network is forgotten: the outcome of the clear would be unknown");
	run(6000);
	check(phase() == DTC_FLOW_CLEARED && app->poll.has_cleared, "nothing took the adapter away: the clear went on to its outcome");
	post("/api/wifi/forget", "{\"ssid\":\"Werkstatt\"}");
	check(answered(200, "ok") && app->profile_count == 0, "when the clear has ended the network is forgotten");

	// The clear dialog alone keeps nothing away: nothing is sent yet, and the dialog closes when the adapter is gone
	listed();
	open_dialog();
	check(on(NAV_DTC_CONFIRM) && app_busy(app) && phase() == DTC_FLOW_LIST, "the scene of the clear dialog, nothing sent");
	post("/api/wifi/forget", "{\"ssid\":\"Werkstatt\"}");
	check(answered(200, "ok") && app->profile_count == 0 && !link_up(&app->link), "while the clear dialog only shows a network is forgotten");
	run(400);
	check(!on(NAV_DTC_CONFIRM) && !app->hold.open && sent[POLL_DTC_CLEAR] == 0, "the adapter is gone with its network: the tick has closed the clear dialog, nothing was cleared");
	// ... and neither does an upload, or the heat: forgetting asks nothing at the display
	released();
	app->uploading = true;
	post("/api/wifi/forget", "{\"ssid\":\"Werkstatt\"}");
	check(answered(200, "ok") && app->profile_count == 0, "while a firmware upload runs a network is forgotten");
	released();
	app_temperature(app, 85, true);
	post("/api/wifi/forget", "{\"ssid\":\"Werkstatt\"}");
	check(answered(200, "ok") && app->profile_count == 0, "while the heat keeps the screen dark a network is forgotten: nothing is asked at the display");
}

// What takes the adapter away, in every phase of the flow: the web interface and the settings at the knob agree
static void test_under_way(void)
{
	static const struct
	{
		dtc_flow_phase_t phase;
		bool under_way;
		const char *rule;
	} phases[] = {
		{DTC_FLOW_IDLE, false, "nothing read: a network is forgotten and asked for, the restart carried out, and the settings at the knob ask before they restart"},
		{DTC_FLOW_READ_SENT, true, "a read waits for its answer: forgetting and storing a network and the restart are answered busy, and Neustart at the knob opens no dialog"},
		{DTC_FLOW_READING, true, "the adapter reads: forgetting and storing a network and the restart are answered busy, and Neustart at the knob opens no dialog"},
		{DTC_FLOW_LIST, false, "the list is shown: a network is forgotten and asked for, the restart carried out, and the settings at the knob ask before they restart"},
		{DTC_FLOW_CLEAR_SENT, true, "a clear waits for its answer: forgetting and storing a network and the restart are answered busy, and Neustart at the knob opens no dialog"},
		{DTC_FLOW_CLEARING, true, "the adapter clears: forgetting and storing a network and the restart are answered busy, and Neustart at the knob opens no dialog"},
		{DTC_FLOW_CLEARED, false, "the outcome of a clear is shown: a network is forgotten and asked for, the restart carried out, and the settings at the knob ask before they restart"},
		{DTC_FLOW_FAILED, false, "a request has failed: a network is forgotten and asked for, the restart carried out, and the settings at the knob ask before they restart"},
		{DTC_FLOW_UNKNOWN, false, "the outcome of a clear is unknown: a network is forgotten and asked for, the restart carried out, and the settings at the knob ask before they restart"},
	};

	for(int i = 0; i < COUNT(phases); i++)
	{
		bool busy = phases[i].under_way;
		bool forget, store, reboot, knob;

		released();
		app->poll.flow.phase = phases[i].phase;
		post("/api/wifi/forget", "{\"ssid\":\"Werkstatt\"}");
		forget = !by_route && (busy ? answered(409, "busy") && app->profile_count == 1 && link_up(&app->link) && carried() == 0 : answered(200, "ok") && app->profile_count == 0 && done.wifi == 1);

		released();
		app->poll.flow.phase = phases[i].phase;
		post("/api/wifi", NEU);
		store = !by_route && (busy ? answered(409, "busy") && nothing_asked() && access_asking(&app->access, now) == ACCESS_ASK_NONE : answered(202, "asked_1") && access_asking(&app->access, now) == ACCESS_ASK_WIFI);

		released();
		app->poll.flow.phase = phases[i].phase;
		post("/api/reboot", "");
		reboot = busy ? answered(409, "busy") && done.reboot == 0 : answered(200, "ok") && done.reboot == 1;

		// The settings of the device, the focus on Neustart; the press ends before the next round of the adapter
		released();
		short_press();
		turn(5);
		short_press();
		turn(2);
		run_to(3300);
		app->poll.flow.phase = phases[i].phase;
		short_press();
		knob = on(busy ? NAV_SETTINGS : NAV_CONFIRM) && app->poll.flow.phase == phases[i].phase && done.reboot == 0;

		if(!forget || !store || !reboot || !knob) printf("  phase %d: forget %d, store %d, reboot %d, knob %d\n", (int)phases[i].phase, forget, store, reboot, knob);
		check(forget && store && reboot && knob, phases[i].rule);
	}

	// A read that waits for an adapter that is out of reach is under way like every other (dtc_flow.h): the adapter
	// scans on, and what restarts the display or takes it from its network would lose the list
	{
		bool forget, store, reboot, reset, upload;

		released();
		run_to(20000);
		app_do(app, NAV_DO_READ, now);
		run(100);
		wican.dead = true;
		run_to(36100);
		check(view() == CONN_VIEW_NO_ANSWER && app->poll.lost && phase() == DTC_FLOW_READING && app->poll.flow.seq == 42 && app_busy(app),
		      "the scene: the adapter fell silent, and the read of the display that it accepted waits for it");
		post("/api/reboot", "");
		reboot = answered(409, "busy") && done.reboot == 0;
		post("/api/reset", "");
		reset = answered(409, "busy") && access_asking(&app->access, now) == ACCESS_ASK_NONE;
		post("/api/wifi", NEU);
		store = !by_route && answered(409, "busy") && nothing_asked() && access_asking(&app->access, now) == ACCESS_ASK_NONE;
		post("/api/wifi/forget", "{\"ssid\":\"Werkstatt\"}");
		forget = !by_route && answered(409, "busy") && app->profile_count == 1 && link_up(&app->link);
		upload = upload_begin("0.2.0") == 409 && answered(409, "busy") && !app->uploading;
		if(!forget || !store || !reboot || !reset || !upload) printf("  a read that waits: forget %d, store %d, reboot %d, reset %d, upload %d\n", forget, store, reboot, reset, upload);
		check(forget && store && reboot && reset && upload && phase() == DTC_FLOW_READING && carried() == 0,
		      "while a read waits for a silent adapter the restart, the factory reset, forgetting and storing a network and a firmware upload are answered busy, as during every read");
		wican.dead = false;
		run_to(41300);
		check(phase() == DTC_FLOW_LIST && app->list_lines == 7 && !app_busy(app), "the scene: the adapter answers again at 41000, and the list of the read that waited is there");
		post("/api/reboot", "");
		check(answered(200, "ok") && done.reboot == 1, "when the read that waited has ended with its list the restart is carried out");

		// ... and while the display is in no network at all
		released();
		run_to(20000);
		app_do(app, NAV_DO_READ, now);
		run(100);
		wifi.in_range_count = 0;
		lose_wifi();
		run(1000);
		check(view() == CONN_VIEW_NO_WIFI && !link_up(&app->link) && phase() == DTC_FLOW_READING && app_busy(app), "the scene: the network of the adapter is gone, and the read of the display waits");
		post("/api/reboot", "");
		reboot = answered(409, "busy") && done.reboot == 0;
		post("/api/wifi", NEU);
		store = !by_route && answered(409, "busy") && nothing_asked();
		post("/api/wifi/forget", "{\"ssid\":\"Werkstatt\"}");
		forget = !by_route && answered(409, "busy") && app->profile_count == 1;
		check(forget && store && reboot && phase() == DTC_FLOW_READING && carried() == 0,
		      "out of its network with a read that waits the display is busy as well: no restart, no network forgotten or asked for");
	}
}

/* The settings ----------------------------------------------------------------------------------------- */

static void test_settings(void)
{
	static const struct
	{
		const char *body, *fixture, *rule;
	} refusals[] = {
		{"{\"brightness\":4}", "member_brightness", "app_web_member_brightness.json: settings with a brightness of 4: 400 with the name of the member"},
		{"{\"brightness\":50,\"night\":101}", "member_night", "app_web_member_night.json: settings with a night brightness of 101: 400 with the name of the member"},
		{"{\"night_mode\":1}", "member_night_mode", "app_web_member_night_mode.json: settings with a night mode that is a number: 400 with the name of the member"},
		{"{\"reverse\":\"ja\"}", "member_reverse", "app_web_member_reverse.json: settings with a direction that is a text: 400 with the name of the member"},
		{"{\"standby_s\":3601}", "member_standby_s", "app_web_member_standby_s.json: settings with a standby time of 3601 s: 400 with the name of the member"},
		{"[]", "member_none", "app_web_member_none.json: settings that are no object: 400 with an empty name"},
		{"", "member_none", "settings with an empty body: 400 with an empty name"},
	};
	static char from_browser[512];

	check(read_fixture("fixtures/settings_from_browser.json", from_browser, sizeof(from_browser)), "the settings of the browser are there");

	drive();
	remember();
	post("/api/settings", "{\"brightness\":40}");
	check(by_route && answered(403, "locked") && untouched(), "without the release web_route() refuses to change the settings");

	released();
	run_to(602000);
	remember();
	handler_lag = 100;
	post("/api/settings", "{\"brightness\":40}");
	handler_lag = 0;
	check(!by_route && answered(403, "locked") && only_time() && carried() == 0 && app->clock_ms == 602100, "the release ends before the function runs: the settings stay, 403; the time is taken over");

	released();
	run_to(10000);
	for(int i = 0; i < COUNT(refusals); i++)
	{
		remember();
		post("/api/settings", refusals[i].body);
		check(answered(400, refusals[i].fixture) && only_time() && carried() == 0, refusals[i].rule);
	}
	check(access_is_open(&app->access, 609999) && !access_is_open(&app->access, 610000), "settings that are refused at 10000 have renewed the release all the same, until 610000");

	check(light() == 80 && app->nav.page == 0, "the scene of the settings: 80 percent, the first page");
	run_to(20000);
	post("/api/settings", "{\"brightness\":40,\"reverse\":true,\"standby_s\":0}");
	check(answered(200, "settings_changed") && routed == WEB_ROUTE_SETTINGS, "app_web_settings_changed.json: settings that are taken: 200 with the settings now in use");
	check(light() == 40 && done.settings == 1 && carried() == 1 && done.last == APP_EVENT_STORE_SETTINGS && strcmp(flash.settings, fixture("settings_changed")) == 0,
	      "the new brightness is in use at once, and the platform is asked to store the settings, and nothing else");
	check(access_is_open(&app->access, 619999) && !access_is_open(&app->access, 620000), "settings changed at 20000 renew the release until 620000");
	turn(1);
	check(app->nav.page == 0, "the direction of the knob is reversed at once: a detent to the right is the hard end of the first page");
	turn(-1);
	check(app->nav.page == 1, "with the direction reversed a detent to the left is the next page");
	start();
	run(2100);
	check(light() == 40 && app->settings.reverse && app->settings.standby_s == 0, "after a restart the settings of the browser are in use");

	app_do(app, NAV_DO_RELEASE_ON, now);
	// Held for a second: a long press, which on the first value page does nothing
	switch_pressed = true;
	run(1000);
	post("/api/settings", "{\"reverse\":false}");
	check(code == 200 && knob_is_pressed(&app->knob) && !app->knob.reverse && on(NAV_PAGES), "settings that arrive while the knob is pressed: its direction follows at once");
	post("/api/settings", "{\"reverse\":true}");
	switch_pressed = false;
	run(100);
	memset(&done, 0, sizeof(done));
	done.settings = 1;
	post("/api/settings", from_browser);
	check(answered(200, "settings_browser") && light() == 10 && done.settings == 2, "app_web_settings_browser.json: the settings as the page sends them, with a member the display does not know: night mode at 10 percent");
	post("/api/settings", "{}");
	check(answered(200, "settings_browser") && done.settings == 3, "settings without a member change nothing and are stored all the same: 200 with the settings in use");
	post("/api/settings", "{\"reverse\":false}");
	turn(1);
	check(code == 200 && !app->settings.reverse && app->nav.page == 1, "the direction set back by the browser: a detent to the right is the next page again");

	// The browser changes the settings while the brightness is being set at the knob
	released();
	short_press();
	turn(1);
	short_press();
	turn(1);
	check(on(NAV_BRIGHTNESS) && app->brightness_preview == 85 && light() == 85, "the scene of the brightness screen: 85 percent are being set");
	remember();
	post("/api/settings", "{\"standby_s\":120}");
	check(code == 200 && nothing_but(MAY_CLOCK | MAY_SETTINGS, &app->access, now) && app->brightness_preview == 85 && light() == 85 && on(NAV_BRIGHTNESS),
	      "settings of the browser change the settings and the direction of the knob and nothing else: the brightness being set at the knob stays");

	factory();
	start();
	app_do(app, NAV_DO_RELEASE_ON, now);
	post("/api/settings", "{\"standby_s\":60}");
	check(answered(200, "settings_defaults"), "app_web_settings_defaults.json: the settings of a display fresh from the factory");
}

/* Restart and factory reset ---------------------------------------------------------------------------- */

static void test_reboot(void)
{
	drive();
	remember();
	post("/api/reboot", "");
	check(by_route && answered(403, "locked") && untouched(), "without the release web_route() refuses the restart");

	released();
	run_to(602000);
	remember();
	handler_lag = 100;
	post("/api/reboot", "");
	handler_lag = 0;
	check(!by_route && answered(403, "locked") && only_time() && carried() == 0, "the release ends before the function runs: no restart, 403");

	// Busy, by each of its three reasons
	released();
	app_do(app, NAV_DO_READ, now);
	remember();
	post("/api/reboot", "");
	check(by_route && answered(409, "busy") && untouched(), "while a read of the display is under way web_route() refuses the restart");
	released();
	run_to(10000);
	meanwhile = begin_read;
	post("/api/reboot", "");
	meanwhile = NULL;
	check(!by_route && answered(409, "busy") && carried() == 0 && phase() == DTC_FLOW_READ_SENT, "a read begins before the function runs: no restart, 409 busy");
	check(access_is_open(&app->access, 609999) && !access_is_open(&app->access, 610000), "the restart that was refused as busy at 10000 has renewed the release, until 610000");
	run(6000);
	open_dialog();
	check(on(NAV_DTC_CONFIRM) && phase() == DTC_FLOW_LIST, "the scene of the clear dialog");
	fresh_room();
	code = app_web_action(app, WEB_ROUTE_REBOOT, reply, &reply_length, now);
	check(answered(409, "busy") && app_take_events(app) == 0, "while the clear dialog shows the function refuses the restart: 409 busy");
	released();
	meanwhile = begin_upload;
	post("/api/reboot", "");
	meanwhile = NULL;
	check(!by_route && answered(409, "busy") && carried() == 0, "an upload begins before the function runs: no restart, 409 busy");
	released();
	meanwhile = upload_and_close;
	post("/api/reboot", "");
	meanwhile = NULL;
	check(!by_route && answered(403, "locked") && carried() == 0, "an upload begins and the release is taken back before the function runs: the restart is answered locked, not busy");

	// Carried out
	released();
	run_to(10000);
	post("/api/reboot", "garbage the restart does not read");
	check(answered(200, "ok") && routed == WEB_ROUTE_REBOOT, "the restart: 200");
	check(done.reboot == 1 && carried() == 1 && done.last == APP_EVENT_REBOOT && app->clock_ms == 10000, "the platform is asked to restart, and nothing else; the time of the app is the one of the request");
	restart_as_asked();
	check(done.starts == 1 && !access_is_open(&app->access, now), "the display starts again, the release is closed");

	// A question that waits does not keep the restart away
	released();
	post("/api/wifi", NEU);
	post("/api/reboot", "");
	check(answered(200, "ok") && done.reboot == 1 && done.wifi == 0, "a restart while a question waits is carried out: the question is lost with it");
}

static void test_reset(void)
{
	drive();
	remember();
	post("/api/reset", "");
	check(by_route && answered(403, "locked") && untouched(), "without the release web_route() refuses the factory reset");

	released();
	run_to(10000);
	remember();
	meanwhile = close_release;
	post("/api/reset", "");
	meanwhile = NULL;
	check(!by_route && answered(403, "locked") && access_ticket(&app->access, 1, now) == ACCESS_TICKET_UNKNOWN && carried() == 0, "the release is taken back before the function runs: no factory reset is asked for, 403");

	released();
	app_do(app, NAV_DO_READ, now);
	post("/api/reset", "");
	check(by_route && answered(409, "busy"), "while a read of the display is under way web_route() refuses the factory reset");
	released();
	run_to(10000);
	remember();
	meanwhile = begin_upload;
	post("/api/reset", "");
	meanwhile = NULL;
	before.uploading = true;
	check(!by_route && answered(409, "busy") && only_time() && access_asking(&app->access, now) == ACCESS_ASK_NONE && access_is_open(&app->access, 602099) && !access_is_open(&app->access, 602100),
	      "an upload begins before the function runs: no factory reset is asked for, 409 busy, and the release is not renewed");
	released();
	meanwhile = begin_read;
	post("/api/reset", "");
	meanwhile = NULL;
	check(!by_route && answered(409, "busy") && access_asking(&app->access, now) == ACCESS_ASK_NONE, "a read begins before the function runs: no factory reset is asked for, 409 busy");
	run(6000);
	open_dialog();
	fresh_room();
	code = app_web_action(app, WEB_ROUTE_RESET, reply, &reply_length, now);
	check(answered(409, "busy") && on(NAV_DTC_CONFIRM) && access_asking(&app->access, now) == ACCESS_ASK_NONE, "while the clear dialog shows the function asks for no factory reset: 409 busy");

	released();
	post("/api/wifi", NEU);
	remember();
	post("/api/reset", "");
	check(answered(409, "asking") && only_time() && access_asking(&app->access, now) == ACCESS_ASK_WIFI && strcmp(app->ask_detail, "Neu") == 0, "while a network question waits no factory reset is asked for: 409 asking, the question stays");
	meanwhile = begin_upload;
	post("/api/reset", "");
	meanwhile = NULL;
	check(answered(409, "busy"), "the factory reset while a question waits and an upload runs: busy, not asking");
	released();
	meanwhile = upload_and_close;
	post("/api/reset", "");
	meanwhile = NULL;
	check(!by_route && answered(403, "locked"), "an upload begins and the release is taken back before the function runs: the factory reset is answered locked, not busy");

	released();
	fresh_room();
	code = app_web_action(app, WEB_ROUTE_RESET, reply, &reply_length, 602099);
	check(answered(202, "asked_1") && app->access.asking_since_ms == 602099, "a factory reset asked for in the last millisecond of the release is asked for, at that time");
	released();
	fresh_room();
	code = app_web_action(app, WEB_ROUTE_RESET, reply, &reply_length, 602100);
	check(answered(403, "locked") && access_ticket(&app->access, 1, 602100) == ACCESS_TICKET_UNKNOWN, "a factory reset asked for in the millisecond the release ends is not asked for: 403");

	// Asked, confirmed, carried out
	released();
	run_to(10000);
	post("/api/reset", "garbage the reset does not read");
	check(answered(202, "asked_1") && routed == WEB_ROUTE_RESET, "the factory reset: 202 with the ticket");
	check(access_asking(&app->access, now) == ACCESS_ASK_RESET && nothing_asked() && carried() == 0, "the question of the factory reset waits for the knob without a detail: nothing is reset yet");
	check(access_is_open(&app->access, 609999) && !access_is_open(&app->access, 610000), "the question of the factory reset asked at 10000 renews the release until 610000");
	sees("ask_reset", "the question of the factory reset lies over the value page");
	run(1300);
	short_press();
	check(carried() == 0 && access_asking(&app->access, now) == ACCESS_ASK_RESET && now == 11460, "a press the knob reports 1440 ms after the question confirms nothing");
	short_press();
	check(done.reset == 1 && carried() == 1 && done.last == APP_EVENT_FACTORY_RESET, "the next press confirms: the platform is asked for the factory reset, and nothing else");
	restart_as_asked();
	check(done.starts == 1 && app->profile_count == 0 && app->poll.bound_id[0] == '\0', "after the factory reset the display starts without network and binding");

	// Refused, expired
	released();
	post("/api/reset", "");
	long_press();
	run(2000);
	short_press();
	check(carried() == 0 && on(NAV_MENU) && access_ticket(&app->access, 1, now) == ACCESS_TICKET_REFUSED, "a factory reset refused by a long press is not carried out, also not by a press after it");
	released();
	post("/api/reset", "");
	run(61000);
	short_press();
	check(carried() == 0 && on(NAV_MENU) && access_ticket(&app->access, 1, now) == ACCESS_TICKET_EXPIRED, "a factory reset nobody confirmed in 60 s is not carried out, also not by a press after it");
}

/* The firmware upload ---------------------------------------------------------------------------------- */

// drive() with a version in the other slot to go back to, and the release given at 2100
static void ready_for_upload(void)
{
	garage();
	machine.previous_firmware = true;
	start();
	run(2100);
	memset(&done, 0, sizeof(done));
	app_do(app, NAV_DO_RELEASE_ON, now);
}

static void test_upload_story(void)
{
	ready_for_upload();
	check(app->previous_firmware && !app_busy(app), "the scene of an upload: the release is given, the other slot holds the version before this one");
	upload_begin("0.2.0");
	check(code == 0 && reply_length == 0 && reply[0] == '\0' && sound() && routed == WEB_ROUTE_OTA && !by_route, "the first bytes of a firmware of the display: 0, the platform may erase and write; no body");
	check(app->uploading && app_busy(app) && app->upload_percent == 0 && app->upload_ms == 2100 && strcmp(app->upload_version, "0.2.0") == 0 && all_bytes(app->upload_version + 5, sizeof(app->upload_version) - 5, 0),
	      "the upload runs from its begin on: the display is busy, 0 percent, the version of the image is kept");
	check(!app->previous_firmware && !machine.previous_firmware && carried() == 0, "with the begin of the upload the other slot holds no version to go back to; no event is raised");
	check(access_asking(&app->access, now) == ACCESS_ASK_NONE && app->ask_detail[0] == '\0', "the begin of an upload asks nothing");
	sees("upload_begun", "the upload lies over the value page with 0 percent");

	run(1000);
	upload_progress(630000);
	check(app->upload_percent == 42 && app->upload_ms == 3100 && app->uploading, "630000 of 1500000 bytes written at 3100: 42 percent, the idle time counts from there");
	sees("upload", "the upload on the screen with its 42 percent");
	short_press();
	turn(1);
	tap(0);
	check(on(NAV_PAGES) && app->nav.page == 0, "during the upload the display takes no input");
	post("/api/wifi", NEU);
	check(answered(409, "busy") && nothing_asked(), "during the upload no network is asked for");
	post("/api/reset", "");
	check(by_route && answered(409, "busy"), "during the upload web_route() refuses the factory reset");
	post("/api/reboot", "");
	check(by_route && answered(409, "busy") && carried() == 0, "during the upload web_route() refuses the restart");
	upload_begin("0.3.0");
	check(by_route && answered(409, "busy") && strcmp(app->upload_version, "0.2.0") == 0, "during the upload web_route() refuses a second one");
	fresh_room();
	make_image("0.3.0");
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, reply, &reply_length, now);
	check(answered(409, "busy") && strcmp(app->upload_version, "0.2.0") == 0 && app->upload_percent == 42 && app->upload_ms == 3100, "during the upload the function refuses a second one: the one that runs is not touched");
	put("/api/layout?mode=apply", stored_layout);
	check(code == 200 && app->source == APP_LAYOUT_PREVIEW, "during the upload a layout can be applied all the same");

	run(20000);
	upload_progress(FILE_SIZE);
	check(app->upload_percent == 100, "all bytes written: 100 percent");
	run(100);
	upload_end(true);
	check(answered(202, "asked_1"), "the upload is complete: 202 with the ticket of the question");
	check(!app->uploading && !app_busy(app) && access_asking(&app->access, now) == ACCESS_ASK_FIRMWARE && strcmp(app->ask_detail, "0.2.0") == 0 && !app->has_wifi_asked && carried() == 0 &&
	      app->access.asking_since_ms == now,
	      "the upload is over and the question waits for the knob with the version of the image: nothing is installed yet");
	put("/api/layout?mode=apply", builtin_text);
	run(100);
	sees("ask_firmware", "the question names the version of the uploaded firmware");
	upload_begin("0.3.0");
	check(!by_route && answered(409, "asking") && !app->uploading && strcmp(app->ask_detail, "0.2.0") == 0, "while the firmware question waits no other upload begins: 409 asking");
	run(1500);
	short_press();
	check(done.install == 1 && carried() == 1 && done.last == APP_EVENT_INSTALL_FIRMWARE && access_ticket(&app->access, 1, now) == ACCESS_TICKET_CONFIRMED,
	      "the knob confirms: the platform is asked to boot the uploaded firmware, and nothing else");
	restart_as_asked();
	run(100);
	check(app->update_pending && app->previous_firmware && has_line("over: update"), "the new firmware starts, asks whether the update is in order, and the version before it is in the other slot");
}

static void test_upload_refused(void)
{
	static const struct
	{
		int at;
		uint8_t byte;
		const char *fixture, *rule;
	} files[] = {
		{0, 0xEA, "no_image", "app_web_no_image.json: a file that is no image of the chip maker: 422 no_image"},
		{12, 0x05, "wrong_chip", "app_web_wrong_chip.json: an image for another chip: 422 wrong_chip"},
		{32, 0x33, "no_description", "app_web_no_description.json: an image without an application description: 422 no_description"},
		{80, 'W', "wrong_project", "app_web_wrong_project.json: the firmware of another project: 422 wrong_project"},
	};

	for(int i = 0; i < COUNT(files); i++)
	{
		ready_for_upload();
		run_to(10000);
		make_image("0.2.0");
		image[files[i].at] = files[i].byte;
		remember();
		upload_first(OTA_CHECK_BYTES, FILE_SIZE);
		check(!by_route && answered(422, files[i].fixture) && only_time() && app->previous_firmware && machine.previous_firmware && !app_busy(app) && carried() == 0, files[i].rule);
	}

	ready_for_upload();
	run_to(10000);
	make_image("0.2.0");
	remember();
	upload_first(OTA_CHECK_BYTES - 1, FILE_SIZE);
	check(!by_route && answered(422, "too_short") && only_time() && app->previous_firmware, "app_web_too_short.json: 111 bytes of a firmware are too few to judge it: 422 too_short");
	check(access_is_open(&app->access, 609999) && !access_is_open(&app->access, 610000), "the upload that was refused at 10000 has renewed the release, until 610000");
	fresh_room();
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, SLOT + 1, SLOT, reply, &reply_length, now);
	check(answered(422, "too_large") && !app->uploading && app->previous_firmware, "a firmware one byte larger than the slot: 422 too_large");
	fresh_room();
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, 0, SLOT, reply, &reply_length, now);
	check(answered(422, "too_large") && !app->uploading, "a firmware of no bytes: 422 too_large");
	upload_first(0, 0);
	check(by_route && answered_text(413, "{\"error\":\"too_large\"}"), "an empty firmware is refused by web_route() already");
	remember();
	fresh_room();
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, SLOT, SLOT, reply, &reply_length, now);
	check(code == 0 && app->uploading, "a firmware that fills the slot to its last byte begins");
	before.uploading = true;
	before.upload_ms = app->upload_ms;
	before.previous_firmware = false;
	memcpy(before.upload_version, "0.2.0", 6);
	check(only_time(), "the begin of an upload changes nothing but the upload: running, its time, its version, and the version to go back to");

	// A version of the longest size
	ready_for_upload();
	make_image("0.2.0-123456789-123456789-12345");
	upload_first(OTA_CHECK_BYTES, FILE_SIZE);
	check(code == 0 && strcmp(app->upload_version, "0.2.0-123456789-123456789-12345") == 0, "a version of 31 bytes, the longest an image holds, is kept whole");
	upload_progress(750000);
	upload_end(true);
	check(code == 202 && strcmp(app->ask_detail, "0.2.0-123456789-123456789-12345") == 0 && app->upload_percent == 50, "the longest version is the detail of the question");
	app_do(app, NAV_DO_ASK_REFUSE, now);
	run(1000);
	upload_begin("0.3");
	check(code == 0 && app->upload_percent == 0 && app->upload_ms == 3100 && strcmp(app->upload_version, "0.3") == 0 && all_bytes(app->upload_version + 3, sizeof(app->upload_version) - 3, 0),
	      "a second upload begins at 0 percent with its own time and version: no byte of the longer version before it stays");
	upload_end(false);
	upload_begin("");
	check(code == 0 && all_bytes(app->upload_version, sizeof(app->upload_version), 0), "an image without a version text begins with an empty version, not with the one before it");
	upload_end(true);
	check(code == 202 && app->ask_detail[0] == '\0', "the question for an image without a version text has no detail");
}

static void test_upload_order(void)
{
	// Without the release
	ready_for_upload();
	app_do(app, NAV_DO_RELEASE_OFF, now);
	remember();
	upload_begin("0.2.0");
	check(by_route && answered(403, "locked") && untouched(), "without the release web_route() refuses the upload");
	fresh_room();
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, reply, &reply_length, now);
	check(answered(403, "locked") && only_time() && app->previous_firmware, "without the release the function refuses the upload: 403, the version to go back to stays");

	// The release is renewed by the upload like by every change
	ready_for_upload();
	run_to(400000);
	upload_begin("0.2.0");
	check(code == 0 && access_seconds_left(&app->access, now) == 600 && !access_is_open(&app->access, 1000000),
	      "an upload that begins 202.1 s before the release would end: it renews the release like every change and begins");

	// ... but not beyond its latest end, 1802100
	ready_for_upload();
	fresh_room();
	code = app_web_settings(app, "{}", 2, reply, &reply_length, 600000);
	code = app_web_settings(app, "{}", 2, reply, &reply_length, 1199000);
	app_take_events(app);
	check(code == 200 && access_is_open(&app->access, 1798999) && !access_is_open(&app->access, 1799000), "the scene of a release kept open by changes: it ends at 1799000, 3100 ms before its latest end");
	remember();
	fresh_room();
	make_image("0.2.0");
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, reply, &reply_length, 1503100);
	check(answered(403, "locked") && only_time() && !app->uploading && app->previous_firmware, "an upload 299 s before the latest end of the release does not begin: 403 locked");
	check(!access_is_open(&app->access, 1802100) && access_is_open(&app->access, 1802099), "the upload that came too late has renewed the release up to its latest end");

	ready_for_upload();
	fresh_room();
	code = app_web_settings(app, "{}", 2, reply, &reply_length, 600000);
	code = app_web_settings(app, "{}", 2, reply, &reply_length, 1199000);
	app_take_events(app);
	make_image("0.2.0");
	fresh_room();
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, reply, &reply_length, 1503099);
	check(code == 0 && app->uploading && app->upload_ms == 1503099, "an upload 299.001 s before the latest end of the release, which count as 300 s, begins");
	ready_for_upload();
	fresh_room();
	code = app_web_settings(app, "{}", 2, reply, &reply_length, 600000);
	code = app_web_settings(app, "{}", 2, reply, &reply_length, 1199000);
	app_take_events(app);
	make_image("0.2.0");
	fresh_room();
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, reply, &reply_length, 1502100);
	check(code == 0 && app->uploading, "an upload exactly 300 s before the latest end of the release begins");

	// Busy
	ready_for_upload();
	app_do(app, NAV_DO_READ, now);
	upload_begin("0.2.0");
	check(by_route && answered(409, "busy") && app->previous_firmware, "while a read of the display is under way web_route() refuses the upload");
	ready_for_upload();
	run_to(10000);
	remember();
	meanwhile = begin_read;
	upload_begin("0.2.0");
	meanwhile = NULL;
	check(!by_route && answered(409, "busy") && !app->uploading && app->previous_firmware && machine.previous_firmware, "a read begins before the function runs: the upload does not begin, 409 busy");
	run(6000);
	open_dialog();
	fresh_room();
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, reply, &reply_length, now);
	check(answered(409, "busy") && !app->uploading && on(NAV_DTC_CONFIRM), "while the clear dialog shows no upload begins: 409 busy");

	// A question keeps the upload away exactly as long as it waits
	ready_for_upload();
	post("/api/reset", "");
	make_image("0.2.0");
	fresh_room();
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, reply, &reply_length, 62099);
	check(answered(409, "asking") && !app->uploading, "an upload 59.999 s after a question that nobody answered: it still waits, 409 asking");
	fresh_room();
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, reply, &reply_length, 62100);
	check(code == 0 && app->uploading, "an upload 60 s after the question: it has expired, the upload begins");

	// The order: locked, busy, asking, the file
	ready_for_upload();
	post("/api/reset", "");
	make_image("0.2.0");
	image[0] = 0;
	remember();
	upload_first(OTA_CHECK_BYTES, FILE_SIZE);
	check(!by_route && answered(409, "asking") && only_time() && access_asking(&app->access, now) == ACCESS_ASK_RESET, "while a question waits a file that is no firmware is answered asking, not no_image");
	meanwhile = begin_read;
	upload_first(OTA_CHECK_BYTES, FILE_SIZE);
	meanwhile = NULL;
	check(!by_route && answered(409, "busy"), "an upload while a question waits and a read is under way: busy, not asking");
	ready_for_upload();
	meanwhile = read_and_close;
	upload_first(OTA_CHECK_BYTES, FILE_SIZE);
	meanwhile = NULL;
	check(!by_route && answered(403, "locked") && !app->uploading, "a read begins and the release is taken back before the function runs: the upload is answered locked, not busy");
}

static void test_upload_and_update(void)
{
	garage();
	machine.update_pending = true;
	machine.previous_firmware = true;
	start();
	run(2100);
	memset(&done, 0, sizeof(done));
	app_do(app, NAV_DO_RELEASE_ON, now);
	remember();
	upload_begin("0.3.0");
	check(!by_route && answered(409, "busy") && only_time() && !app->uploading && app->previous_firmware && machine.previous_firmware,
	      "while the running firmware waits for its confirmation no upload begins: 409 busy, the version to go back to stays");
	post("/api/settings", "{}");
	check(code == 200, "while the running firmware waits for its confirmation the settings can be changed all the same");
	post("/api/wifi", NEU);
	check(answered(202, "asked_1") && has_line("over: ask"), "while the running firmware waits for its confirmation a network is asked for: the question lies over the one of the update");
	upload_begin("0.3.0");
	check(!by_route && answered(409, "busy"), "an upload while the running firmware is not confirmed and a question waits: busy, not asking");
	long_press();
	post("/api/reset", "");
	check(answered(202, "asked_2"), "while the running firmware waits for its confirmation a factory reset is asked for");
	long_press();
	check(app->update_pending && access_asking(&app->access, now) == ACCESS_ASK_NONE && has_line("over: update"), "the long presses refused the two questions and left the one of the update");
	// In steps of a second from 5000 on, so that every step has its tick
	run_to(5000);
	stride = 1000;
	run(300000);
	stride = STEP_MS;
	check(app->update_given_up && done.reboot == 1 && access_is_open(&app->access, now), "the scene of an update nobody confirmed in time, the restart not carried out yet");
	upload_begin("0.3.0");
	check(!by_route && answered(409, "busy") && app->previous_firmware, "when the time of the confirmation is over no upload begins either");
	short_press();
	check(!app->update_pending && done.valid == 1, "the scene of the update confirmed at the knob after all");
	upload_begin("0.3.0");
	check(code == 0 && app->uploading && !app->previous_firmware, "when the running firmware is confirmed the upload begins");

	garage();
	machine.rolled_back = true;
	machine.previous_firmware = true;
	start();
	run(2100);
	app_do(app, NAV_DO_RELEASE_ON, now);
	upload_begin("0.3.0");
	check(code == 0 && app->uploading, "after an update that was taken back the next upload begins");

	garage();
	machine.update_pending = true;
	start();
	run(2100);
	app_do(app, NAV_DO_RELEASE_ON, now);
	upload_begin("0.3.0");
	check(!by_route && answered(409, "busy") && !app->previous_firmware && !app->uploading, "an unconfirmed firmware keeps the upload away also when the other slot is said to hold nothing to go back to");
}

static void test_upload_ends(void)
{
	// Broken
	ready_for_upload();
	upload_begin("0.2.0");
	run(1000);
	upload_progress(300000);
	upload_end(false);
	check(answered(500, "upload"), "app_web_upload.json: an upload that broke: 500 upload");
	check(!app->uploading && !app_busy(app) && access_asking(&app->access, now) == ACCESS_ASK_NONE && nothing_asked() && carried() == 0 && !app->previous_firmware && !machine.previous_firmware,
	      "the broken upload is over: nothing is asked, nothing installed, and the other slot holds no version to go back to");
	short_press();
	turn(5);
	short_press();
	turn(3);
	short_press();
	check(on(NAV_SETTINGS) && app->nav.row == 3 && carried() == 0, "after the broken upload Vorherige Version does nothing: what lies in the other slot is half a firmware");
	upload_end(true);
	check(answered(500, "upload") && access_asking(&app->access, now) == ACCESS_ASK_NONE && app->ask_detail[0] == '\0', "a second end of an upload that is over, said to be complete: 500, nothing is asked");

	ready_for_upload();
	upload_begin("0.2.0");
	upload_progress(FILE_SIZE);
	upload_end(false);
	check(answered(500, "upload") && app->upload_percent == 100 && !app->uploading && access_asking(&app->access, now) == ACCESS_ASK_NONE && app->ask_detail[0] == '\0',
	      "an upload whose last byte was written and whose check sum is wrong: 500 upload, nothing is asked");

	// Stalled
	ready_for_upload();
	upload_begin("0.2.0");
	run_to(10000);
	upload_progress(300000);
	run_to(40000);
	check(app->uploading && app_busy(app), "29.8 s after the upload brought something last it still runs");
	run_to(40020);
	check(!app->uploading && !app_busy(app), "an upload that brought nothing for 30 s is over");
	remember();
	upload_progress(900000);
	check(!app->uploading && app->upload_percent == 20 && app->upload_ms == 10000 && only_time(), "bytes that arrive after the upload was ended do not bring it back: nothing changes");
	upload_end(true);
	check(answered(500, "upload") && access_asking(&app->access, now) == ACCESS_ASK_NONE && app->ask_detail[0] == '\0' && carried() == 0 && !app->previous_firmware,
	      "the end of an upload the display has ended, said to be complete: 500 upload, nothing is asked");
	short_press();
	check(on(NAV_MENU), "after the upload that stalled the knob works again");

	// An end without any begin
	ready_for_upload();
	remember();
	upload_end(true);
	check(answered(500, "upload") && only_time() && access_asking(&app->access, now) == ACCESS_ASK_NONE && app->previous_firmware, "the end of an upload that never began: 500 upload, nothing is asked, nothing changes");
	upload_progress(900000);
	check(only_time(), "bytes of an upload that never began change nothing");
	post("/api/reset", "");
	upload_end(true);
	check(answered(500, "upload") && access_asking(&app->access, now) == ACCESS_ASK_RESET && access_ticket(&app->access, 1, now) == ACCESS_TICKET_WAITING,
	      "the end of an upload that does not run leaves a question that waits as it is");
	upload_end(false);
	check(answered(500, "upload") && access_asking(&app->access, now) == ACCESS_ASK_RESET, "the end of a broken upload that does not run leaves the question as well");

	// The release ends during the upload
	ready_for_upload();
	run_to(302000);
	upload_begin("0.2.0");
	check(code == 0 && !access_is_open(&app->access, 902000), "the scene of an upload that begins at 302000: the release lasts until 902000");
	stride = 1000;
	for(int i = 0; i < 601; i++)
	{
		run(1000);
		upload_progress(1000u * (uint32_t)i);
	}
	stride = STEP_MS;
	check(app->uploading && now == 903000, "the scene of an upload that crawls for 601 s");
	upload_end(true);
	check(answered(403, "locked") && !app->uploading && access_asking(&app->access, now) == ACCESS_ASK_NONE && app->ask_detail[0] == '\0' && carried() == 0 && !app->previous_firmware,
	      "the release has ended when the upload is complete: 403 locked, the upload is over, nothing is asked");

	ready_for_upload();
	upload_begin("0.2.0");
	fresh_room();
	code = app_web_upload_end(app, true, reply, &reply_length, 602099);
	check(answered(202, "asked_1") && app->access.asking_since_ms == 602099 && strcmp(app->ask_detail, "0.2.0") == 0, "an upload complete in the last millisecond of the release is asked for, at that time");
	ready_for_upload();
	upload_begin("0.2.0");
	fresh_room();
	code = app_web_upload_end(app, true, reply, &reply_length, 602100);
	check(answered(403, "locked") && !app->uploading && access_ticket(&app->access, 1, 602100) == ACCESS_TICKET_UNKNOWN, "an upload complete in the millisecond the release ends is not asked for: 403");

	// A read under way when the upload is complete, as the screen under an upload never starts it
	ready_for_upload();
	upload_begin("0.2.0");
	app_do(app, NAV_DO_READ, now);
	upload_end(true);
	check(answered(202, "asked_1") && phase() == DTC_FLOW_READ_SENT && app_busy(app), "a read of the display is under way when the upload is complete: the firmware is asked for all the same");

	// A question in the way, as this interface never asks it
	ready_for_upload();
	upload_begin("0.2.0");
	ask_reset();
	upload_end(true);
	check(answered(409, "asking") && !app->uploading && access_asking(&app->access, now) == ACCESS_ASK_RESET && app->ask_detail[0] == '\0', "a question waits when the upload is complete: 409 asking, the upload is over, the question that waits stays");
	ready_for_upload();
	upload_begin("0.2.0");
	access_ask(&app->access, ACCESS_ASK_WIFI, now);
	upload_end(true);
	check(answered(409, "asking") && access_asking(&app->access, now) == ACCESS_ASK_WIFI && access_ticket(&app->access, 1, now) == ACCESS_TICKET_WAITING && app->ask_detail[0] == '\0',
	      "a network question waits when the upload is complete: 409 asking as well, it is not replaced");

	// The question refused, and expired
	ready_for_upload();
	upload_begin("0.2.0");
	upload_end(true);
	run(1500);
	long_press();
	check(access_ticket(&app->access, 1, now) == ACCESS_TICKET_REFUSED && carried() == 0 && app->ask_detail[0] == '\0' && !app->previous_firmware && !machine.previous_firmware,
	      "the firmware question refused by a long press: nothing is installed, the other slot holds no version to go back to");
	run(2000);
	short_press();
	check(on(NAV_MENU) && carried() == 0, "a press after the refusal is a press on the screen: the firmware is not installed by it");
	start();
	check(!app->previous_firmware, "after a restart the uploaded firmware nobody confirmed is still no version to go back to");

	ready_for_upload();
	upload_begin("0.2.0");
	upload_end(true);
	run(61000);
	short_press();
	check(access_ticket(&app->access, 1, now) == ACCESS_TICKET_EXPIRED && carried() == 0 && on(NAV_MENU) && !app->previous_firmware, "the firmware question nobody answered in 60 s: nothing is installed, also not by a press after it");

	// What an earlier question left behind
	ready_for_upload();
	post("/api/wifi", NEU);
	app_do(app, NAV_DO_ASK_REFUSE, now);
	app->has_wifi_asked = true;
	strcpy(app->wifi_asked.ssid, "Rest");
	strcpy(app->wifi_asked.password, "rest-passwort");
	strcpy(app->wifi_asked.host, "rest.example.org");
	strcpy(app->ask_detail, "Rest");
	upload_begin("0.2.0");
	upload_end(true);
	check(code == 202 && !app->has_wifi_asked && all_bytes(&app->wifi_asked, sizeof(app->wifi_asked), 0) && strcmp(app->ask_detail, "0.2.0") == 0,
	      "the firmware question drops the network an earlier question left behind and has the version as its detail");
}

// Returns one bit for each call that went wrong
static int progress_without_size(void)
{
	int wrong = 0;

	app->upload_percent = 55;
	app_web_upload_progress(app, 5, 0, now);
	if(app->upload_percent != 0 || !app->uploading) wrong |= 0x01;
	app->upload_percent = 55;
	app_web_upload_progress(app, 0, 0, now);
	if(app->upload_percent != 0 || !app->uploading) wrong |= 0x02;
	return wrong;
}

static void test_upload_progress(void)
{
	static const struct
	{
		uint32_t written, size;
		int percent;
		const char *rule;
	} steps[] = {
		{0, 1500000, 0, "0 of 1500000 bytes: 0 percent"},
		{14999, 1500000, 0, "14999 of 1500000 bytes: 0 percent"},
		{15000, 1500000, 1, "15000 of 1500000 bytes: 1 percent"},
		{1484999, 1500000, 98, "1484999 of 1500000 bytes: 98 percent"},
		{1499999, 1500000, 99, "1499999 of 1500000 bytes: 99 percent"},
		{1500000, 1500000, 100, "1500000 of 1500000 bytes: 100 percent"},
		{1500001, 1500000, 100, "one byte more than the file has: 100 percent"},
		{1515000, 1500000, 100, "a hundredth more than the file has: 100 percent"},
		{1530000, 1500000, 100, "two hundredths more than the file has: 100 percent"},
		{UINT32_MAX, 1, 100, "the largest number of bytes of a file of one byte: 100 percent"},
		{UINT32_MAX, UINT32_MAX, 100, "all bytes of the largest file: 100 percent"},
		{UINT32_MAX - 1, UINT32_MAX, 99, "all bytes but one of the largest file: 99 percent"},
		{42949673, UINT32_MAX, 1, "42949673 bytes of the largest file: 1 percent, counted in 64 bit"},
		{42949672, UINT32_MAX, 0, "42949672 bytes of the largest file: 0 percent"},
	};
	int wrong;

	ready_for_upload();
	upload_begin("0.2.0");
	for(int i = 0; i < COUNT(steps); i++)
	{
		app->upload_percent = 55;
		app_web_upload_progress(app, steps[i].written, steps[i].size, now);
		check(app->upload_percent == steps[i].percent && app->uploading, steps[i].rule);
	}

	wrong = in_child(progress_without_size);
	check(!(wrong & 0x01), "bytes of a file without a size: 0 percent");
	check(!(wrong & 0x02), "no bytes of a file without a size: 0 percent");

	// The time the idle time counts from
	run_to(5000);
	app_web_upload_progress(app, 1, FILE_SIZE, 5000);
	check(app->upload_ms == 5000 && app->clock_ms == 5000, "bytes at 5000: the idle time counts from 5000");
	app_web_upload_progress(app, 2, FILE_SIZE, 4000);
	check(app->upload_ms == 5000 && app->upload_percent == 0, "bytes with a time before the latest the app has seen: the idle time counts from the time of the app, it does not step back");
	app_web_upload_progress(app, 3, FILE_SIZE, 9000);
	check(app->upload_ms == 9000 && app->clock_ms == 9000, "bytes with a time ahead of the app: the time is taken over and the idle time counts from it");
	check(access_is_open(&app->access, 602099) && !access_is_open(&app->access, 602100) && app_take_events(app) == 0, "the bytes of an upload do not renew the release, which its begin renewed at 2100, and raise no event");
	app_web_upload_progress(app, FILE_SIZE, FILE_SIZE, 9500);
	app_web_upload_progress(app, 0, FILE_SIZE, 9800);
	check(app->upload_ms == 9800 && app->upload_percent == 0, "a number of bytes smaller than the one before it is taken as it is, also behind 100 percent, and renews the time as well");
	run_to(39620);
	check(app->uploading, "with the tick at 39600, 29.8 s after the last bytes, the upload still runs");
	run_to(39820);
	check(!app->uploading, "with the tick at 39800, 30 s after the last bytes, the upload is over");
}

// The heat keeps the backlight off: nobody could see a question, none is asked and no upload begins
static void test_heat(void)
{
	// The questions
	released();
	run_to(10000);
	app_temperature(app, 85, true);
	remember();
	post("/api/wifi", NEU);
	check(!by_route && answered(409, "hot") && only_time() && nothing_asked() && access_asking(&app->access, now) == ACCESS_ASK_NONE && access_ticket(&app->access, 1, now) == ACCESS_TICKET_UNKNOWN,
	      "app_web_hot.json: while the heat keeps the backlight off no network is asked for: 409 hot, no ticket, and nothing changes but the time");
	remember();
	post("/api/reset", "");
	check(!by_route && answered(409, "hot") && only_time() && access_asking(&app->access, now) == ACCESS_ASK_NONE && carried() == 0, "while the heat keeps the backlight off no factory reset is asked for: 409 hot");
	check(access_is_open(&app->access, 602099) && !access_is_open(&app->access, 602100), "a question that is refused as hot does not renew the release");
	app_temperature(app, 99, false);
	post("/api/reset", "");
	check(answered(409, "hot"), "a reading of the temperature that failed leaves the light off: still 409 hot");
	app_temperature(app, 80, true);
	post("/api/reset", "");
	check(answered(409, "hot") && app->heat == GUARD_HEAT_OFF, "back at 80 degrees the light is still off: still 409 hot");
	app_temperature(app, 79, true);
	post("/api/reset", "");
	check(answered(202, "asked_1") && app->heat == GUARD_HEAT_DIM && has_line("over: ask"), "back at 79 degrees the screen is lit again, its backlight limited: the factory reset is asked for");
	released();
	app_temperature(app, 84, true);
	post("/api/wifi", NEU);
	check(answered(202, "asked_1") && app->heat == GUARD_HEAT_DIM, "at 84 degrees the backlight is only limited: a network is asked for");

	// The upload
	ready_for_upload();
	run_to(10000);
	app_temperature(app, 85, true);
	remember();
	upload_begin("0.2.0");
	check(!by_route && answered(409, "hot") && only_time() && !app->uploading && !app_busy(app) && app->previous_firmware && machine.previous_firmware && carried() == 0,
	      "while the heat keeps the backlight off no upload begins: 409 hot, nothing is erased, the version to go back to stays");
	check(access_is_open(&app->access, 609999) && !access_is_open(&app->access, 610000), "the upload that was refused as hot at 10000 has renewed the release, as every change that found it open");
	app_temperature(app, 79, true);
	upload_begin("0.2.0");
	check(code == 0 && app->uploading, "cooled down to 79 degrees the upload begins");
	run(1000);
	upload_progress(750000);
	app_temperature(app, 85, true);
	run(1000);
	check(app->uploading && app->upload_percent == 50 && has_line("over: upload"), "the heat switches the light off while a firmware arrives: the upload runs on");
	upload_end(true);
	check(answered(409, "hot") && !app->uploading && !app_busy(app) && access_asking(&app->access, now) == ACCESS_ASK_NONE && app->ask_detail[0] == '\0' && carried() == 0 && !app->previous_firmware,
	      "the heat has switched the light off when the upload is complete: 409 hot, the upload is over, the firmware is not asked for");
	check(access_ticket(&app->access, 1, now) == ACCESS_TICKET_UNKNOWN, "no ticket was given for the firmware nobody could be asked about");

	// The order: locked, busy, asking, hot, the rest
	released();
	app_temperature(app, 85, true);
	post("/api/wifi", "kein JSON");
	check(answered(409, "hot"), "in the heat a network request that cannot be read is answered hot, not body");
	make_image("0.2.0");
	image[0] = 0;
	upload_first(OTA_CHECK_BYTES, FILE_SIZE);
	check(!by_route && answered(409, "hot"), "in the heat a file that is no firmware is answered hot, not no_image");
	app->uploading = true;
	post("/api/wifi", NEU);
	check(answered(409, "busy"), "in the heat, while an upload runs, a network request is answered busy, not hot");
	app->uploading = false;
	meanwhile = begin_read;
	post("/api/reset", "");
	check(!by_route && answered(409, "busy"), "in the heat, while a read is under way, the factory reset is answered busy, not hot");
	make_image("0.2.0");
	upload_first(OTA_CHECK_BYTES, FILE_SIZE);
	meanwhile = NULL;
	check(answered(409, "busy") && !app->uploading, "in the heat, while a read is under way, an upload is answered busy, not hot");
	released();
	app->heat = GUARD_HEAT_OFF;
	ask_reset();
	post("/api/wifi", NEU);
	check(answered(409, "asking"), "a question that waits on a dark screen, as the display never has it: a second one is answered asking, not hot");
	make_image("0.2.0");
	upload_first(OTA_CHECK_BYTES, FILE_SIZE);
	check(!by_route && answered(409, "asking") && !app->uploading, "... and so is the begin of an upload");
	meanwhile = close_release;
	post("/api/wifi", NEU);
	meanwhile = NULL;
	check(!by_route && answered(403, "locked"), "in the heat, with the release taken back before the function runs, the answer is locked, not hot");
	released();
	app_temperature(app, 85, true);
	make_image("0.2.0");
	meanwhile = close_release;
	upload_first(OTA_CHECK_BYTES, FILE_SIZE);
	meanwhile = NULL;
	check(!by_route && answered(403, "locked") && !app->uploading, "... and so it is for the begin of an upload");
	garage();
	machine.update_pending = true;
	start();
	run(2100);
	app_do(app, NAV_DO_RELEASE_ON, now);
	app_temperature(app, 85, true);
	upload_begin("0.3.0");
	check(!by_route && answered(409, "busy"), "in the heat, while the running firmware is not confirmed, an upload is answered busy, not hot");

	// The story: the browser asks, the board gets too hot, and cools down again
	released();
	strcpy(wifi.in_range[1], "Neu");
	wifi.in_range_count = 2;
	post("/api/wifi", NEU);
	check(answered(202, "asked_1"), "the story of the heat: a network is asked for");
	run(2000);
	app_temperature(app, 85, true);
	get("/api/ticket?id=1");
	check(answered(200, "ticket_refused") && nothing_asked() && carried() == 0, "the story of the heat: the light goes off, and the ticket of the question that waited says refused; what it asked for is dropped");
	get("/api/info");
	check(answered_with(200, "\"temp_c\":85,\"heat\":\"off\","), "the story of the heat: the info tells why");
	post("/api/wifi", NEU);
	check(answered(409, "hot"), "the story of the heat: asked again at once, the answer is hot");
	short_press();
	check(carried() == 0 && app->profile_count == 1 && on(NAV_PAGES), "the story of the heat: a press on the dark screen stores nothing");
	app_temperature(app, 69, true);
	post("/api/wifi", NEU);
	check(answered(202, "asked_2") && has_line("over: ask"), "the story of the heat: cooled down, the network is asked for with the next ticket");
	run(1500);
	short_press();
	check(done.wifi == 1 && app->profile_count == 2 && access_ticket(&app->access, 2, now) == ACCESS_TICKET_CONFIRMED, "the story of the heat: the knob confirms, the network is stored");

	// What asks nothing at the display goes on in the heat
	released();
	app_temperature(app, 85, true);
	post("/api/settings", "{\"brightness\":40}");
	check(code == 200 && app->settings.brightness == 40, "in the heat the settings can be changed");
	put("/api/layout?mode=save", stored_layout);
	check(code == 200 && app->source == APP_LAYOUT_STORED, "in the heat a layout can be saved");
	post("/api/reboot", "");
	check(answered(200, "ok") && done.reboot == 1, "in the heat the display can be restarted");
}

// A save and a reset of the layout between two takes of the events: the later alone counts
static void test_layout_events(void)
{
	static char warning[1024], refused[1024];
	// What the browser sends, and what it leaves to be carried out: s save, r reset, a apply, x a save that is refused
	static const char kinds[] = "srax";
	bool loaded = read_fixture("fixtures/app_web_layout_warning.json", warning, sizeof(warning)) && read_fixture("fixtures/app_web_layout_refused.json", refused, sizeof(refused));
	int wrong = 0, sequences = 0;

	check(loaded, "the layouts of the layout events are there");

	// The functions alone: serve() takes the events behind every request, as the platform does
	released();
	fresh_room();
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_SAVE, stored_layout, strlen(stored_layout), reply, &reply_length, now);
	check(code == 200 && app->events == APP_EVENT_STORE_LAYOUT, "the scene of a save whose event was not taken yet");
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_RESET, NULL, 0, reply, &reply_length, now);
	check(code == 200 && app->events == APP_EVENT_ERASE_LAYOUT, "a reset behind a save that was not taken yet: the erase alone waits, the save is taken back");
	carry_out();
	check(done.layout == 0 && done.erase == 1 && !flash.has_layout && app->source == APP_LAYOUT_BUILTIN && app_take_events(app) == 0,
	      "carried out, nothing was stored and the flash holds no layout: a reset after a save erases");

	released();
	put("/api/layout?mode=save", warning);
	check(code == 200 && flash.has_layout && done.layout == 1, "the scene of a stored layout in the flash");
	fresh_room();
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_RESET, NULL, 0, reply, &reply_length, now);
	check(code == 200 && app->events == APP_EVENT_ERASE_LAYOUT, "the scene of a reset whose event was not taken yet");
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_SAVE, stored_layout, strlen(stored_layout), reply, &reply_length, now);
	check(code == 200 && app->events == APP_EVENT_STORE_LAYOUT, "a save behind a reset that was not taken yet: the store alone waits, the reset is taken back");
	carry_out();
	check(done.layout == 2 && done.erase == 0 && flash.has_layout && strcmp(flash.layout, stored_layout) == 0 && app->source == APP_LAYOUT_STORED && in_use(stored_layout),
	      "carried out, the flash holds the layout that was saved last and nothing was erased: a save after a reset stores");

	// Every other event that waits stays
	app->events = APP_EVENT_STORE_BOUND | APP_EVENT_STORE_SETTINGS | APP_EVENT_STORE_LAYOUT | APP_EVENT_REBOOT;
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_RESET, NULL, 0, reply, &reply_length, now);
	check(code == 200 && app_take_events(app) == (APP_EVENT_STORE_BOUND | APP_EVENT_STORE_SETTINGS | APP_EVENT_ERASE_LAYOUT | APP_EVENT_REBOOT),
	      "a reset takes back the save that waits and no other event: binding, settings and restart stay to be carried out");
	app->events = APP_EVENT_STORE_BOUND | APP_EVENT_STORE_SETTINGS | APP_EVENT_ERASE_LAYOUT | APP_EVENT_REBOOT;
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_SAVE, stored_layout, strlen(stored_layout), reply, &reply_length, now);
	check(code == 200 && app_take_events(app) == (APP_EVENT_STORE_BOUND | APP_EVENT_STORE_SETTINGS | APP_EVENT_STORE_LAYOUT | APP_EVENT_REBOOT),
	      "a save takes back the reset that waits and no other event");

	// What stores and erases nothing takes nothing back
	app->events = APP_EVENT_ERASE_LAYOUT;
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_SAVE, refused, strlen(refused), reply, &reply_length, now);
	check(code == 400 && app->events == APP_EVENT_ERASE_LAYOUT, "a save that is refused leaves the reset that waits");
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_APPLY, stored_layout, strlen(stored_layout), reply, &reply_length, now);
	check(code == 200 && app->events == APP_EVENT_ERASE_LAYOUT, "a layout that is only applied leaves the reset that waits: the preview is not stored");
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_CHECK, stored_layout, strlen(stored_layout), reply, &reply_length, now);
	check(code == 200 && app_take_events(app) == APP_EVENT_ERASE_LAYOUT, "a layout that is only checked leaves it as well");
	app->events = APP_EVENT_STORE_LAYOUT;
	app_do(app, NAV_DO_RELEASE_OFF, now);
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_RESET, NULL, 0, reply, &reply_length, now);
	check(code == 403 && app_take_events(app) == APP_EVENT_STORE_LAYOUT, "a reset that is refused without the release leaves the save that waits");
	app->events = APP_EVENT_ERASE_LAYOUT;
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_SAVE, stored_layout, strlen(stored_layout), reply, &reply_length, now);
	check(code == 403 && app_take_events(app) == APP_EVENT_ERASE_LAYOUT, "a save that is refused without the release leaves the reset that waits");

	// Every sequence of one to five requests without a take in between
	released();
	for(int length = 1; length <= 5; length++)
	{
		int count = 1;

		for(int i = 0; i < length; i++) count *= 4;
		for(int sequence = 0; sequence < count; sequence++)
		{
			uint32_t expected = 0, taken;
			int rest = sequence;

			for(int i = 0; i < length; i++, rest /= 4)
			{
				char kind = kinds[rest % 4];

				if(kind == 's') code = app_web_layout(app, WEB_ROUTE_LAYOUT_SAVE, stored_layout, strlen(stored_layout), reply, &reply_length, now);
				if(kind == 'r') code = app_web_layout(app, WEB_ROUTE_LAYOUT_RESET, NULL, 0, reply, &reply_length, now);
				if(kind == 'a') code = app_web_layout(app, WEB_ROUTE_LAYOUT_APPLY, warning, strlen(warning), reply, &reply_length, now);
				if(kind == 'x') code = app_web_layout(app, WEB_ROUTE_LAYOUT_SAVE, refused, strlen(refused), reply, &reply_length, now);
				// The later of a save and a reset alone counts
				if(kind == 's') expected = APP_EVENT_STORE_LAYOUT;
				if(kind == 'r') expected = APP_EVENT_ERASE_LAYOUT;
				if(code != (kind == 'x' ? 400 : 200)) wrong++;
			}
			taken = app_take_events(app);
			if(taken != expected)
			{
				if(wrong < 4) printf("  sequence %d of length %d: events 0x%04x, expected 0x%04x\n", sequence, length, (unsigned)taken, (unsigned)expected);
				wrong++;
			}
			sequences++;
		}
	}
	check(wrong == 0 && sequences == 1364, "all 1364 sequences of one to five saves, resets, previews and refused saves without a take in between: what waits is the event of the last save or "
	                                        "reset alone, never both, and nothing without one of them");
}

// What each request that is accepted changes: the parts of the app that app_web.h names for it, the time,
// and the release as one change or one question at its time leaves it - and nothing else
static void test_frame(void)
{
	access_t release;

	// On the brightness screen, where an input, a focus and a value being set are there to be spoilt
	released();
	short_press();
	turn(1);
	short_press();
	turn(1);
	run_to(10000);
	check(on(NAV_BRIGHTNESS) && app->brightness_preview == 85 && app->last_input_ms < 10000, "the scene of the frame: the brightness screen, the last input before 10000");

	release = app->access;
	access_write(&release, now);
	remember();
	post("/api/settings", "{\"night\":30,\"reverse\":true}");
	check(code == 200 && nothing_but(MAY_CLOCK | MAY_SETTINGS, &release, 10000), "settings change the settings, the direction of the knob, the time and the release, and nothing else");

	release = app->access;
	access_write(&release, now);
	remember();
	put("/api/layout?mode=apply", stored_layout);
	check(code == 200 && nothing_but(MAY_CLOCK | MAY_LAYOUT | MAY_CHECKED, &release, 10000) && on(NAV_BRIGHTNESS), "an applied layout changes the views, their text, source and page, and nothing else");
	release = app->access;
	access_write(&release, now);
	remember();
	put("/api/layout?mode=save", builtin_text);
	check(code == 200 && nothing_but(MAY_CLOCK | MAY_LAYOUT | MAY_CHECKED, &release, 10000) && done.layout == 1, "a saved layout changes the views, their text, source and page, and nothing else");
	release = app->access;
	access_write(&release, now);
	remember();
	post("/api/layout/reset", "");
	check(code == 200 && nothing_but(MAY_CLOCK | MAY_LAYOUT, &release, 10000) && done.erase == 1, "a reset changes the views, their text, source and page, and nothing else: not even the room for a layout");

	release = app->access;
	access_ask(&release, ACCESS_ASK_WIFI, now);
	remember();
	post("/api/wifi", NEU);
	check(code == 202 && nothing_but(MAY_CLOCK | MAY_ASKED, &release, 10000) && on(NAV_BRIGHTNESS), "a network question changes what is asked for and nothing else: the screen below it stays, it is no input");
	release = app->access;
	access_write(&release, now);
	remember();
	post("/api/wifi/forget", "{\"ssid\":\"Werkstatt\"}");
	check(code == 200 && nothing_but(MAY_CLOCK | MAY_NETWORKS, &release, 10000) && access_asking(&app->access, now) == ACCESS_ASK_WIFI && strcmp(app->wifi_asked.ssid, "Neu") == 0 &&
	      strcmp(app->poll.bound_id, OWN) == 0, "a forgotten network changes the networks, the link and what the poll makes of it, and nothing else: the question that waits and the binding stay");
	app_do(app, NAV_DO_ASK_REFUSE, now);

	release = app->access;
	access_ask(&release, ACCESS_ASK_RESET, now);
	remember();
	post("/api/reset", "");
	check(code == 202 && nothing_but(MAY_CLOCK | MAY_ASKED, &release, 10000), "the question of the factory reset changes what is asked for and nothing else");
	app_do(app, NAV_DO_ASK_REFUSE, now);

	make_image("0.2.0");
	release = app->access;
	access_write(&release, now);
	remember();
	upload_first(OTA_CHECK_BYTES, FILE_SIZE);
	check(code == 0 && nothing_but(MAY_CLOCK | MAY_UPLOAD, &release, 10000), "the begin of an upload changes the upload and the version to go back to, and nothing else");
	release = app->access;
	access_ask(&release, ACCESS_ASK_FIRMWARE, now);
	remember();
	before.uploading = false;
	upload_end(true);
	check(code == 202 && nothing_but(MAY_CLOCK | MAY_ASKED, &release, 10000), "the end of an upload ends it and changes what is asked for, and nothing else");
	app_do(app, NAV_DO_ASK_REFUSE, now);

	// The restart, under an update that waits for its confirmation
	garage();
	machine.update_pending = true;
	start();
	run(2100);
	memset(&done, 0, sizeof(done));
	app_do(app, NAV_DO_RELEASE_ON, now);
	release = app->access;
	access_write(&release, now);
	remember();
	post("/api/reboot", "");
	check(code == 200 && nothing_but(MAY_CLOCK, &release, 2100) && done.reboot == 1 && done.valid == 0 && done.last == APP_EVENT_REBOOT && app->update_pending && !app->update_given_up,
	      "the restart raises its event and changes nothing else: the update that waits is neither confirmed nor given up by it");
}

// A body is as long as its length says: the bytes behind it are not the request's, and no zero ends it
static void test_body_length(void)
{
	static char tailed[2400];
	size_t length = strlen(stored_layout);

	released();
	memcpy(tailed, stored_layout, length);
	strcpy(tailed + length, "}}}");
	serve(WEB_PUT, "/api/layout?mode=check", tailed, length, now);
	check(answered(200, "report_stored"), "a layout that is checked ends with its length: what follows it in the memory is not read");
	serve(WEB_PUT, "/api/layout?mode=apply", tailed, length, now);
	check(answered(200, "report_stored") && in_use(stored_layout) && app->layout_text[length] == '\0' && strlen(app->layout_text) == length, "a layout that is applied ends with its length, and so does its text in the app");
	serve(WEB_PUT, "/api/layout?mode=apply", tailed, length + 1, now);
	check(code == 400, "the same layout with one byte more of what follows it: 400");

	serve(WEB_POST, "/api/settings", "{\"brightness\":40}xyz", 17, now);
	check(code == 200 && app->settings.brightness == 40, "settings end with their length: what follows them is not read");
	serve(WEB_POST, "/api/settings", "{\"brightness\":50}xyz", 18, now);
	check(answered(400, "member_none") && app->settings.brightness == 40, "the same settings with one byte more of what follows them: 400");
	serve(WEB_POST, "/api/wifi/forget", "{\"ssid\":\"Garage\"}xyz", 17, now);
	check(answered(404, "not_found"), "a forget request ends with its length: the network it names is not stored, 404");
	serve(WEB_POST, "/api/wifi/forget", "{\"ssid\":\"Garage\"}xyz", 18, now);
	check(answered(400, "body"), "the same forget request with one byte more of what follows it: 400");
	serve(WEB_POST, "/api/wifi", "{\"ssid\":\"Neu\"}xyz", 14, now);
	check(answered(202, "asked_1") && strcmp(app->wifi_asked.ssid, "Neu") == 0, "a network request ends with its length: it is asked for");
}

// An event that waits to be taken is not lost by a request
static void test_events_add_up(void)
{
	released();
	app->events = APP_EVENT_STORE_BOUND;
	fresh_room();
	code = app_web_settings(app, "{}", 2, reply, &reply_length, now);
	check(code == 200 && app_take_events(app) == (APP_EVENT_STORE_BOUND | APP_EVENT_STORE_SETTINGS), "the event of the settings is added to one that waits");
	app->events = APP_EVENT_STORE_BOUND;
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_SAVE, stored_layout, strlen(stored_layout), reply, &reply_length, now);
	check(code == 200 && app_take_events(app) == (APP_EVENT_STORE_BOUND | APP_EVENT_STORE_LAYOUT), "the event of a saved layout is added to one that waits");
	app->events = APP_EVENT_STORE_BOUND;
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_RESET, NULL, 0, reply, &reply_length, now);
	check(code == 200 && app_take_events(app) == (APP_EVENT_STORE_BOUND | APP_EVENT_ERASE_LAYOUT), "the event of a reset layout is added to one that waits");
	app->events = APP_EVENT_STORE_BOUND;
	code = app_web_action(app, WEB_ROUTE_REBOOT, reply, &reply_length, now);
	check(code == 200 && app_take_events(app) == (APP_EVENT_STORE_BOUND | APP_EVENT_REBOOT), "the event of the restart is added to one that waits");
	app->events = APP_EVENT_STORE_BOUND;
	code = app_web_wifi(app, WEB_ROUTE_WIFI_FORGET, "{\"ssid\":\"Werkstatt\"}", 20, reply, &reply_length, now);
	check(code == 200 && app_take_events(app) == (APP_EVENT_STORE_BOUND | APP_EVENT_STORE_WIFI), "the event of a forgotten network is added to one that waits");
}

// The last millisecond of the release, for every change
static void test_last_millisecond(void)
{
	released();
	fresh_room();
	code = app_web_settings(app, "{\"brightness\":40}", 17, reply, &reply_length, 602099);
	check(code == 200 && app->settings.brightness == 40, "settings in the last millisecond of the release are taken");
	released();
	fresh_room();
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_APPLY, stored_layout, strlen(stored_layout), reply, &reply_length, 602099);
	check(code == 200 && app->source == APP_LAYOUT_PREVIEW, "a layout in the last millisecond of the release is applied");
	released();
	fresh_room();
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_RESET, NULL, 0, reply, &reply_length, 602100);
	check(answered(403, "locked") && app_take_events(app) == 0, "a reset of the layout in the millisecond the release ends is refused");
	released();
	fresh_room();
	code = app_web_wifi(app, WEB_ROUTE_WIFI_FORGET, "{\"ssid\":\"Werkstatt\"}", 20, reply, &reply_length, 602099);
	check(code == 200 && app->profile_count == 0, "a network is forgotten in the last millisecond of the release");
	released();
	fresh_room();
	code = app_web_action(app, WEB_ROUTE_REBOOT, reply, &reply_length, 602099);
	check(code == 200 && app_take_events(app) == APP_EVENT_REBOOT, "a restart in the last millisecond of the release is carried out");
	released();
	make_image("0.2.0");
	fresh_room();
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, reply, &reply_length, 602099);
	check(code == 0 && app->uploading && access_seconds_left(&app->access, 602099) == 600, "an upload in the last millisecond of the release begins, and the release is renewed");
}

// The time of every function that can change something
static void test_time(void)
{
	released();
	remember();
	fresh_room();
	code = app_web_settings(app, "{}", 2, reply, &reply_length, 1000);
	check(code == 200 && app->clock_ms == before.clock_ms && access_seconds_left(&app->access, now) == 600, "settings with a time before the latest the app has seen: no time passed, the release is renewed from the time of the app");
	app_take_events(app);

	// The release has ended by the time of the app; the server read its clock before that
	app_tick(app, 602100);
	check(app->clock_ms == 602100 && app->access.clock_ms < 602100, "the scene of a release that has ended by the time of the app, which the release itself was not told");
	fresh_room();
	code = app_web_settings(app, "{\"brightness\":40}", 17, reply, &reply_length, 602000);
	check(answered(403, "locked") && app->settings.brightness == 80, "settings with a time at which the release was still open, while it has ended by the time of the app: 403");
	fresh_room();
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_APPLY, stored_layout, strlen(stored_layout), reply, &reply_length, 602000);
	check(answered(403, "locked") && app->source == APP_LAYOUT_BUILTIN, "a layout with such a time is not applied: 403");
	fresh_room();
	code = app_web_wifi(app, WEB_ROUTE_WIFI_FORGET, "{\"ssid\":\"Werkstatt\"}", 20, reply, &reply_length, 602000);
	check(answered(403, "locked") && app->profile_count == 1, "a network is not forgotten with such a time: 403");
	fresh_room();
	code = app_web_wifi(app, WEB_ROUTE_WIFI_STORE, NEU, strlen(NEU), reply, &reply_length, 602000);
	check(answered(403, "locked") && nothing_asked(), "a network is not asked for with such a time: 403");
	fresh_room();
	code = app_web_action(app, WEB_ROUTE_REBOOT, reply, &reply_length, 602000);
	check(answered(403, "locked") && app_take_events(app) == 0, "no restart with such a time: 403");
	fresh_room();
	code = app_web_action(app, WEB_ROUTE_RESET, reply, &reply_length, 602000);
	check(answered(403, "locked") && access_asking(&app->access, 602100) == ACCESS_ASK_NONE, "no factory reset is asked for with such a time: 403");
	make_image("0.2.0");
	fresh_room();
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, reply, &reply_length, 602000);
	check(answered(403, "locked") && !app->uploading, "no upload begins with such a time: 403");

	// The end of an upload with such a time
	released();
	upload_begin("0.2.0");
	app_web_upload_progress(app, 1, 2, 602090);
	app_tick(app, 602100);
	check(app->uploading, "the scene of an upload that runs when the release ends");
	fresh_room();
	code = app_web_upload_end(app, true, reply, &reply_length, 602000);
	check(answered(403, "locked") && !app->uploading && access_asking(&app->access, 602100) == ACCESS_ASK_NONE, "the firmware question is not asked with a time at which the release was still open, while it has ended by the time of the app");

	// Every function that can change something takes its time over, also when it refuses
	drive();
	fresh_room();
	code = app_web_settings(app, "{}", 2, reply, &reply_length, 3000);
	check(code == 403 && app->clock_ms == 3000, "settings that are refused take their time over");
	code = app_web_layout(app, WEB_ROUTE_LAYOUT_SAVE, stored_layout, strlen(stored_layout), reply, &reply_length, 4000);
	check(code == 403 && app->clock_ms == 4000, "a layout that is refused takes its time over");
	code = app_web_wifi(app, WEB_ROUTE_WIFI_STORE, NEU, strlen(NEU), reply, &reply_length, 5000);
	check(code == 403 && app->clock_ms == 5000, "a network request that is refused takes its time over");
	code = app_web_wifi(app, WEB_ROUTE_WIFI_FORGET, NEU, strlen(NEU), reply, &reply_length, 6000);
	check(code == 403 && app->clock_ms == 6000, "a forget request that is refused takes its time over");
	code = app_web_action(app, WEB_ROUTE_REBOOT, reply, &reply_length, 7000);
	check(code == 403 && app->clock_ms == 7000, "a restart that is refused takes its time over");
	code = app_web_action(app, WEB_ROUTE_RESET, reply, &reply_length, 8000);
	check(code == 403 && app->clock_ms == 8000, "a factory reset that is refused takes its time over");
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, reply, &reply_length, 9000);
	check(code == 403 && app->clock_ms == 9000, "an upload that is refused takes its time over");
	app_web_upload_progress(app, 1, 2, 10000);
	check(app->clock_ms == 10000 && app->upload_ms == 0, "bytes of an upload that does not run take their time over, and nothing else");
	code = app_web_upload_end(app, true, reply, &reply_length, 11000);
	check(code == 500 && app->clock_ms == 11000, "the end of an upload that does not run takes its time over");
	code = app_web_get(app, WEB_ROUTE_INFO, 0, reply, &reply_length, 12000);
	check(code == 200 && app->clock_ms == 11000, "a reading request does not take its time over");

	// An upload begins at the time of the app
	released();
	app_tick(app, 9000);
	make_image("0.2.0");
	fresh_room();
	code = app_web_upload_begin(app, image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, reply, &reply_length, 3000);
	check(code == 0 && app->upload_ms == 9000, "an upload with a time before the latest the app has seen begins at the time of the app: its idle time does not begin in the past");

	// The question is asked at the time of the app
	released();
	app_tick(app, 9000);
	fresh_room();
	code = app_web_action(app, WEB_ROUTE_RESET, reply, &reply_length, 3000);
	check(code == 202 && app->access.asking_since_ms == 9000, "a question with a time before the latest the app has seen is asked at the time of the app");
	app_do(app, NAV_DO_ASK_CONFIRM, 10499);
	check(app_take_events(app) == 0, "the knob 1499 ms after that time confirms nothing");
	app_do(app, NAV_DO_ASK_CONFIRM, 10500);
	check(app_take_events(app) == APP_EVENT_FACTORY_RESET, "the knob 1500 ms after that time confirms");
}

/* The stories across web and device, from the first request to the restart ------------------------------ */

// The release given at the knob: menu, Web-Zugriff, Freigabe, and back to the value page
static void release_at_knob(void)
{
	short_press();
	turn(3);
	short_press();
	short_press();
	long_press();
	long_press();
}

static void test_stories(void)
{
	// The views: checked, applied, looked at, saved, stored, there again after a restart, reset
	drive();
	put("/api/layout?mode=apply", stored_layout);
	check(by_route && code == 403, "the story of the views: without the release the layout cannot be applied");
	put("/api/layout?mode=check", stored_layout);
	check(answered(200, "report_stored"), "the story of the views: it can be checked");
	release_at_knob();
	check(on(NAV_PAGES) && access_is_open(&app->access, now), "the story of the views: the release is given at the knob");
	get("/api/info");
	check(answered_with(200, "\"release\":{\"open\":true,\"left_s\":"), "the story of the views: the info tells that the release is open");
	put("/api/layout?mode=apply", stored_layout);
	run(200);
	sees("preview", "the story of the views: the applied layout is on the screen");
	get("/api/info");
	check(answered_with(200, "\"layout\":{\"name\":\"Meine Ansichten\",\"source\":\"preview\"}") && !flash.has_layout, "the story of the views: the info names the preview, the flash holds no layout");
	put("/api/layout?mode=save", stored_layout);
	check(code == 200 && flash.has_layout && strcmp(flash.layout, stored_layout) == 0 && done.layout == 1, "the story of the views: saved, the event has put the text into the flash");
	start();
	run(2100);
	sees("preview", "the story of the views: after a restart the saved layout is on the screen");
	get("/api/layout");
	check(answered_text(200, stored_layout), "the story of the views: after the restart the browser gets the saved text");
	post("/api/layout/reset", "");
	check(by_route && code == 403 && flash.has_layout, "the story of the views: after the restart the release is closed, the reset is refused");
	release_at_knob();
	post("/api/layout/reset", "");
	run(200);
	check(code == 200 && !flash.has_layout, "the story of the views: the reset removes the layout from the flash");
	sees("motor", "the story of the views: after the reset the built-in views are on the screen");
	start();
	run(2100);
	sees("motor", "the story of the views: after the next restart the built-in views are still there");

	// A display fresh from the factory gets its first network through its own access point
	factory();
	strcpy(wifi.in_range[0], "Werkstatt");
	wifi.in_range_count = 1;
	wifi.found = "192.168.1.50";
	start();
	run(1000);
	check(wifi.ap_on && app->profile_count == 0, "the story of the first network: without a stored network the display has opened its own access point");
	release_at_knob();
	post("/api/wifi", "{\"ssid\":\"Werkstatt\",\"password\":\"geheim-123\"}");
	check(answered(202, "asked_1"), "the story of the first network: asked for through the access point");
	run(1500);
	short_press();
	run(3000);
	check(done.wifi == 1 && strcmp(flash.profiles[0].password, "geheim-123") == 0 && wifi.joined && view() == CONN_VIEW_LIVE && strcmp(flash.bound, OWN) == 0,
	      "the story of the first network: confirmed at the knob, stored, joined, the adapter found and bound");
	get("/api/info");
	check(answered_with(200, "\"wifi\":{\"ssid\":\"Werkstatt\",\"ip\":\"192.168.1.77\",\"rssi\":-61,") && answered_with(200, "\"wican\":{\"host\":\"192.168.1.50\",\"id\":\"a1b2c3d4e5f6\",\"fw\":\"4.21\",\"view\":\"live\"}"),
	      "the story of the first network: the info names the network and the adapter");
}

/* ---------------------------------------------------------------------------------------------------
 * Random sequences of requests and of inputs at the device
 *
 * A browser that sends whatever it likes at any moment, a driver who presses, turns and taps, a board that
 * gets too hot and cools down, and time that passes - for RUNS displays, RUN_DEEDS deeds each, in a child
 * process. Next to it runs a second account of
 * the rules, in another shape than the module: what each request has to answer is read from a list of
 * refusals in the order of app_web.h, judged by what could be seen of the display before the request
 * (seen_t); what the display holds afterwards - networks, settings, views, questions - is kept by the test
 * from the requests it sent and the questions it saw confirmed (told_t). Around every request and every
 * input the promises below are checked.
 */

#define RUNS        60
#define RUN_DEEDS   400
#define SHOWN       4       // broken promises of each kind that are printed

#define B_LOCKED    "{\"error\":\"locked\",\"hint\":\"Am Display: Menü > Web-Zugriff freigeben\"}"
#define B_BUSY      "{\"error\":\"busy\"}"
#define B_ASKING    "{\"error\":\"asking\"}"
#define B_HOT       "{\"error\":\"hot\"}"
#define B_BODY      "{\"error\":\"body\"}"
#define B_NOT_FOUND "{\"error\":\"not_found\"}"
#define B_UPLOAD    "{\"error\":\"upload\"}"
#define B_OK        "{\"ok\":true}"

#define RESTARTS    (APP_EVENT_REBOOT | APP_EVENT_FACTORY_RESET | APP_EVENT_PREVIOUS_FIRMWARE | APP_EVENT_INSTALL_FIRMWARE)

enum
{
	PROMISE_ANSWER,     // status and body of every request are what the list of refusals says
	PROMISE_CLOSED,     // nothing changes without the release
	PROMISE_REFUSED,    // a request that is refused leaves everything but the release and the time
	PROMISE_READING,    // a reading request changes nothing at all
	PROMISE_ASKED,      // no question is asked while one waits, an upload runs or the heat keeps the screen dark
	PROMISE_KNOB,       // what a question asks for is carried out only when the knob confirmed that question, never by a tap
	PROMISE_EVENTS,     // every event has its cause
	PROMISE_PASSWORD,   // no answer holds a password
	PROMISE_DTC,        // no request starts a read or a clear of the fault memory
	PROMISE_SOUND,      // every answer is JSON within its room
	PROMISE_HELD,       // networks, settings and views in use and stored are what was sent and confirmed
	PROMISE_MEMORY,     // no byte outside of the app is written
	PROMISE_FRAME,      // a request that is accepted changes what app_web.h names for it, and nothing else
	PROMISES,
};

static const char *const PROMISE_NAMES[PROMISES] = {"answer", "closed", "refused", "reading", "asked", "knob", "events", "password", "dtc", "sound", "held", "memory", "frame"};

typedef enum
{
	K_INFO, K_CATALOG, K_VALUES, K_LAYOUT, K_DTC_LAST, K_WIFI_LIST, K_TICKET,
	K_CHECK, K_APPLY, K_SAVE, K_LAYOUT_RESET,
	K_WIFI, K_FORGET, K_SETTINGS, K_REBOOT, K_RESET,
	K_BEGIN, K_PROGRESS, K_END,
	KINDS,
} kind_t;

static const char *const KIND_NAMES[KINDS] = {"info", "catalog", "values", "layout", "dtc", "wifi", "ticket", "check", "apply", "save", "layout reset", "store wifi", "forget", "settings",
                                              "reboot", "reset", "upload begin", "upload progress", "upload end"};

// The answers a request can get, as columns of the tally
enum
{
	A_GO, A_200, A_202, A_400, A_403, A_404, A_BUSY, A_ASKING, A_HOT, A_422, A_500, ANSWERS,
};

typedef struct
{
	long requests, direct, by_route, back, ahead, inputs, waits, starts;
	long answers[KINDS][ANSWERS];
	long broken[PROMISES];
	long closed;                // requests that would change something, without the release
	long confirmed[4];          // questions the knob confirmed, by kind
	long refused, expired, replaced;
	long uploads_complete, uploads_stalled, uploads_broken, uploads_locked;
	long unconfirmed;           // uploads that did not begin because the running firmware was not confirmed
	long by_dialog;             // restarts and resets the dialog of the device itself asked for
	long busy_read, busy_dialog, busy_upload;
	long taps_on_questions;     // taps while a question of the browser waited that the knob could have confirmed
	long taps_on_updates;       // ... while the update question showed
	long heat_refused;          // questions that waited when the heat switched the light off
	long hot;                   // requests while the heat kept the screen dark
	long previews, saves, resets, forgotten, settings;
	long pages_kept[2];         // layouts taken that kept the page shown: the first page, a later one
	long pages_first[2];        // layouts taken that started at their first page: no page was shown before, none at the position
	long passwords;             // answers searched for passwords
	long leftovers;             // questions asked over what an earlier one left behind
} walk_result_t;

// The layouts the browser of the walk sends
static char walk_warning[1024], walk_hidden[1024], walk_refused[1024];

typedef struct
{
	const char *text;
	bool good;
} walk_layout_t;

static const walk_layout_t WALK_LAYOUTS[] = {
	{stored_layout, true}, {builtin_text, true}, {walk_warning, true}, {walk_hidden, true}, {walk_refused, false}, {"kein JSON", false}, {"", false},
};

// The networks it asks for. password NULL: the member is missing.
typedef struct
{
	const char *body, *ssid, *password, *host;
} walk_network_t;

static const walk_network_t WALK_NETWORKS[] = {
	{"{\"ssid\":\"Neu\",\"password\":\"lauf-passwort-1\",\"host\":\"10.0.0.5\"}", "Neu", "lauf-passwort-1", "10.0.0.5"},
	{"{\"ssid\":\"Werkstatt\",\"host\":\"192.168.1.50\"}", "Werkstatt", NULL, "192.168.1.50"},
	{"{\"ssid\":\"Camping\",\"password\":\"lauf-passwort-2\"}", "Camping", "lauf-passwort-2", ""},
	{"{\"ssid\":\"Offen\",\"password\":\"\"}", "Offen", "", ""},
	{"{\"ssid\":\"Garage\",\"password\":\"lauf-passwort-3\",\"host\":\"wican.local\"}", "Garage", "lauf-passwort-3", "wican.local"},
	{"{\"ssid\":\"Hof\"}", "Hof", NULL, ""},
	{"{\"ssid\":\"Werkstatt\",\"password\":\"lauf-passwort-4\",\"host\":\"192.168.1.50\"}", "Werkstatt", "lauf-passwort-4", "192.168.1.50"},
	{"{\"ssid\":\"\"}", NULL, NULL, NULL},
	{"{\"ssid\":\"Kurz\",\"password\":\"kurz\"}", NULL, NULL, NULL},
	{"nichts", NULL, NULL, NULL},
};

// Every password of the walk: none of them is an SSID or a host
static const char *const WALK_PASSWORDS[] = {"lauf-passwort-1", "lauf-passwort-2", "lauf-passwort-3", "lauf-passwort-4", "geheim-123", "zelt-und-wurst", "geheim1234"};

static const char *const WALK_FORGETS[] = {"Werkstatt", "Neu", "Camping", "Offen", "Garage", "Hof", "Nirgends"};

// The settings it sends: -1 leaves a member as it is
typedef struct
{
	const char *body;
	int brightness, night, night_mode, reverse, standby_s;
} walk_settings_t;

static const walk_settings_t WALK_SETTINGS[] = {
	{"{\"brightness\":40}", 40, -1, -1, -1, -1},
	{"{\"night\":10,\"night_mode\":true}", -1, 10, 1, -1, -1},
	{"{\"night_mode\":false,\"reverse\":true}", -1, -1, 0, 1, -1},
	{"{\"reverse\":false,\"standby_s\":0}", -1, -1, -1, 0, 0},
	{"{\"brightness\":100,\"night\":5,\"standby_s\":3600}", 100, 5, -1, -1, 3600},
	{"{}", -1, -1, -1, -1, -1},
	{"{\"brightness\":200}", 0, 0, 0, 0, 0},
	{"[]", 0, 0, 0, 0, 0},
};
#define WALK_SETTINGS_GOOD  6

// What can be seen of the display before a request, at the time the request counts for
typedef struct
{
	uint64_t time;
	bool open;                  // the release
	uint32_t left_s;            // what would be left of it if it were renewed now
	bool busy, uploading, asking;
	bool under_way;             // a fault memory request of the display: sent or accepted, and not ended
	bool hot;                   // the heat keeps the backlight off
	dtc_flow_phase_t phase;
	dtc_flow_send_t to_send;
	int reads, clears;          // requests of the fault memory handed out
	int page;                   // the value page shown, or waiting below another screen
} seen_t;

// What the test knows from the requests it sent and the questions it saw confirmed
typedef struct
{
	uint32_t tickets;           // given since the start of the display
	access_ask_t asked;         // the question of the last ticket
	uint64_t asked_ms;
	int network;                // WIFI: index into WALK_NETWORKS
	char version[40];           // FIRMWARE: the version of the upload
	bool over;                  // the end of the last ticket was seen

	net_profile_t profiles[NET_PROFILES_MAX];
	int profile_count;
	settings_t settings;
	int views;                  // index into WALK_LAYOUTS of the layout the browser put in use, -1: the display chose
	bool saved;                 // ... and it is stored, not only looked at
	int flash_views;            // index of the layout in the flash, -1: none
	bool previous;              // the other slot holds a version to go back to
	bool upload;                // an upload began and its end was not told
	char upload_version[40];
} told_t;

static told_t told;
static walk_result_t *tally;
static uint32_t run_number, deed_number;
static const char *doing = "";
static uint32_t dice;
static uint32_t gathered;       // the events carried out during the deed that is going on

static void broke(int promise, const char *why)
{
	if(tally->broken[promise]++ < SHOWN)
	{
		printf("  run %" PRIu32 ", deed %" PRIu32 " (%s) at %" PRIu64 ": %s - %s\n", run_number, deed_number, doing, now, PROMISE_NAMES[promise], why);
		fflush(stdout);
	}
}

static uint32_t roll(void)
{
	dice = dice * 1664525u + 1013904223u;
	return dice >> 8;
}

static int pick(int count)
{
	return (int)(roll() % (uint32_t)count);
}

static bool chance(int percent)
{
	return pick(100) < percent;
}

/*
 * Never two rolls among the arguments of one call: C leaves open which argument is worked out first, gcc
 * takes the last and clang the first, and the runs of the CI were other runs than the ones on a Mac. Where a
 * call needs two, they are rolled before it, in the order gcc had, which is the one the CI has always run.
 */
static void swipe_anywhere(void)
{
	int dy = pick(3) - 1;
	int dx = pick(3) - 1;

	swipe(dx, dy);
}

static void gather(uint32_t events)
{
	gathered |= events;
}

static seen_t look(uint64_t at_ms)
{
	// The release as it would be after a change that renews it: tried on a copy
	access_t renewed = app->access;
	seen_t seen;

	memset(&seen, 0, sizeof(seen));
	seen.time = at_ms > app->clock_ms ? at_ms : app->clock_ms;
	seen.open = access_is_open(&app->access, seen.time);
	if(access_write(&renewed, seen.time)) seen.left_s = access_seconds_left(&renewed, seen.time);
	seen.busy = app_busy(app);
	seen.uploading = app->uploading;
	seen.asking = access_asking(&app->access, seen.time) != ACCESS_ASK_NONE;
	seen.phase = app->poll.flow.phase;
	seen.under_way = seen.phase == DTC_FLOW_READ_SENT || seen.phase == DTC_FLOW_READING || seen.phase == DTC_FLOW_CLEAR_SENT || seen.phase == DTC_FLOW_CLEARING;
	seen.hot = app->heat == GUARD_HEAT_OFF;
	seen.to_send = app->poll.flow.to_send;
	seen.reads = sent[POLL_DTC_READ];
	seen.clears = sent[POLL_DTC_CLEAR];
	seen.page = app->nav.page;
	return seen;
}

/* What the test holds ---------------------------------------------------------------------------------- */

// The list of net_select.h, written once more: a known SSID is replaced in place, a new one comes first
static void told_store(const walk_network_t *network)
{
	const char *password = network->password;
	int index = -1;

	for(int i = told.profile_count - 1; i >= 0; i--)
	{
		if(strcmp(told.profiles[i].ssid, network->ssid) == 0) index = i;
	}
	// Without a password in the request the stored one stays, and a network that is not stored is an open one
	if(password == NULL) password = index >= 0 ? told.profiles[index].password : "";
	if(index < 0)
	{
		if(told.profile_count < NET_PROFILES_MAX) told.profile_count++;
		for(int i = told.profile_count - 1; i > 0; i--) told.profiles[i] = told.profiles[i - 1];
		index = 0;
	}
	else
	{
		// The text may be the one that is overwritten
		static char kept[NET_PASSWORD_SIZE];

		strcpy(kept, password);
		password = kept;
	}
	memset(&told.profiles[index], 0, sizeof(told.profiles[index]));
	strcpy(told.profiles[index].ssid, network->ssid);
	strcpy(told.profiles[index].password, password);
	strcpy(told.profiles[index].host, network->host);
}

static bool told_forget(const char *ssid)
{
	for(int i = 0; i < told.profile_count; i++)
	{
		if(strcmp(told.profiles[i].ssid, ssid) != 0) continue;

		for(int j = i; j < told.profile_count - 1; j++) told.profiles[j] = told.profiles[j + 1];
		told.profile_count--;
		memset(&told.profiles[told.profile_count], 0, sizeof(told.profiles[0]));
		return true;
	}
	return false;
}

// The display starts: what is stored is what the test holds, everything else begins anew
static void told_start(void)
{
	told.tickets = 0;
	told.asked = ACCESS_ASK_NONE;
	told.over = true;
	// A preview ends with the restart: the layout in the flash is the one in use
	told.views = told.flash_views;
	told.saved = told.flash_views != -1;
	told.upload = false;
	told.previous = machine.previous_firmware;
}

// The very first start of a run: the test takes over what the flash was given
static void told_begin(void)
{
	memset(&told, 0, sizeof(told));
	told.profile_count = flash.profile_count;
	memcpy(told.profiles, flash.profiles, sizeof(told.profiles));
	settings_defaults(&told.settings);
	// The layout of the fixture is the first of the layouts of the walk
	told.flash_views = flash.has_layout ? 0 : -1;
	told_start();
}

// Networks, settings and views in use and in the flash are those the test holds
static void held(void)
{
	char text[SETTINGS_JSON_SIZE];
	const char *flash_text = told.flash_views >= 0 ? WALK_LAYOUTS[told.flash_views].text : NULL;

	if(app->profile_count != told.profile_count || memcmp(app->profiles, told.profiles, sizeof(told.profiles)) != 0) broke(PROMISE_HELD, "the networks in use are not those that were sent and confirmed");
	if(flash.has_wifi && (flash.profile_count != told.profile_count || memcmp(flash.profiles, told.profiles, sizeof(told.profiles)) != 0)) broke(PROMISE_HELD, "the networks in the flash are not those in use");
	if(!flash.has_wifi && told.profile_count != 0) broke(PROMISE_HELD, "networks are in use that were never stored");

	if(app->settings.brightness != told.settings.brightness || app->settings.night != told.settings.night || app->settings.night_mode != told.settings.night_mode ||
	   app->settings.reverse != told.settings.reverse || app->settings.standby_s != told.settings.standby_s) broke(PROMISE_HELD, "the settings in use are not those that were sent");
	settings_to_json(&told.settings, text, sizeof(text));
	if(flash.has_settings && strcmp(flash.settings, text) != 0) broke(PROMISE_HELD, "the settings in the flash are not those in use");
	if(app->knob.reverse != told.settings.reverse) broke(PROMISE_HELD, "the knob does not turn the way the settings say");

	if(flash.has_layout != (flash_text != NULL) || (flash_text != NULL && strcmp(flash.layout, flash_text) != 0)) broke(PROMISE_HELD, "the layout in the flash is not the one that was saved last");
	if(told.views >= 0)
	{
		const char *text_in_use = WALK_LAYOUTS[told.views].text;

		if(app->source != (told.saved ? APP_LAYOUT_STORED : APP_LAYOUT_PREVIEW) || app->layout_length != strlen(text_in_use) || strcmp(app->layout_text, text_in_use) != 0)
		{
			broke(PROMISE_HELD, "the views in use are not those the browser sent last");
		}
	}
	else if(told.flash_views == -1 && app->source != APP_LAYOUT_BUILTIN && app->source != APP_LAYOUT_GENERATED)
	{
		broke(PROMISE_HELD, "views of the browser are in use that it never sent or took back");
	}
	if(app->source == APP_LAYOUT_STORED && (!flash.has_layout || strcmp(flash.layout, app->layout_text) != 0)) broke(PROMISE_HELD, "the views in use are said to be stored and are not in the flash");

	if(strcmp(app->poll.bound_id, flash.has_bound ? flash.bound : "") != 0) broke(PROMISE_HELD, "the adapter the display is bound to is not the one in the flash");
	if(app->uploading && (app->previous_firmware || machine.previous_firmware)) broke(PROMISE_HELD, "an upload runs and the other slot is still offered as the version before");
	if(app->previous_firmware != told.previous) broke(PROMISE_HELD, "the version to go back to is not what the uploads left");
	if(!all_bytes(box.front, sizeof(box.front), FILL) || !all_bytes(box.behind, sizeof(box.behind), FILL)) broke(PROMISE_MEMORY, "a byte next to the app was written");
}

/* The restart ------------------------------------------------------------------------------------------ */

static void walk_restart(void)
{
	if(done.last & APP_EVENT_FACTORY_RESET)
	{
		memset(told.profiles, 0, sizeof(told.profiles));
		told.profile_count = 0;
		settings_defaults(&told.settings);
	}
	restart_as_asked();
	tally->starts++;
	told_start();
	gathered = 0;
	run(2100);
}

/* The knob and the events ------------------------------------------------------------------------------ */

typedef enum
{
	DEED_REQUEST,
	DEED_WAIT,
	DEED_PRESS,     // a short press of the knob: what confirms
	DEED_TAP,       // a tap: it confirms nothing
	DEED_OTHER,     // any other input, and what happens to the board
} deed_t;

// The state of the last ticket the browser got
static access_ticket_t ticket_now(void)
{
	return told.tickets == 0 ? ACCESS_TICKET_UNKNOWN : access_ticket(&app->access, told.tickets, app->clock_ms > now ? app->clock_ms : now);
}

// What the device showed before a deed
typedef struct
{
	access_ticket_t ticket;
	nav_screen_t screen;
	nav_do_t confirm;
	int row;
	bool pending, previous;
} earlier_t;

static earlier_t earlier(void)
{
	earlier_t was = {ticket_now(), app->nav.screen, app->nav.confirm, app->nav.row, app->update_pending, app->previous_firmware};

	return was;
}

// What happened during a deed: every event has its cause, and what a question asks for is carried out
// exactly when the knob confirmed that very question. by_web: the events the request itself accounts for.
static void judge(deed_t deed, const earlier_t *was, uint32_t by_web)
{
	access_ticket_t ticket_after = ticket_now();
	bool confirmed = was->ticket == ACCESS_TICKET_WAITING && ticket_after == ACCESS_TICKET_CONFIRMED;
	// "Ausführen" of the dialog of the device itself, with the knob
	bool dialog = deed == DEED_PRESS && was->screen == NAV_CONFIRM && was->row == 1;
	uint32_t events = gathered & ~by_web;
	// The conversation with the adapter stores by itself
	uint32_t allowed = APP_EVENT_STORE_BOUND | APP_EVENT_STORE_CATALOG | APP_EVENT_STORE_OLD;
	uint32_t needed = 0;

	if(was->ticket == ACCESS_TICKET_WAITING && ticket_after != ACCESS_TICKET_WAITING && !told.over)
	{
		told.over = true;
		if(ticket_after == ACCESS_TICKET_REFUSED) tally->refused++;
		if(ticket_after == ACCESS_TICKET_EXPIRED) tally->expired++;
	}
	if(deed != DEED_REQUEST && was->ticket != ACCESS_TICKET_WAITING && ticket_after != was->ticket) broke(PROMISE_KNOB, "a ticket that had ended changed its end");
	// A request ends no question: one that waited before it is refused only with the release
	if(deed == DEED_REQUEST && was->ticket == ACCESS_TICKET_WAITING && ticket_after == ACCESS_TICKET_REFUSED && access_is_open(&app->access, app->clock_ms > now ? app->clock_ms : now))
	{
		broke(PROMISE_KNOB, "a request refused the question that waited");
	}
	if(confirmed && deed != DEED_PRESS) broke(PROMISE_KNOB, "a question was confirmed without a press of the knob: by a tap, another input, a request or time");
	if(confirmed)
	{
		tally->confirmed[told.asked]++;
		if(app->clock_ms < told.asked_ms + ACCESS_ASK_SHOWN_MS) broke(PROMISE_KNOB, "a question was confirmed before it was shown for 1500 ms");
		if(told.asked == ACCESS_ASK_WIFI)
		{
			told_store(&WALK_NETWORKS[told.network]);
			needed = APP_EVENT_STORE_WIFI;
		}
		if(told.asked == ACCESS_ASK_FIRMWARE) needed = APP_EVENT_INSTALL_FIRMWARE;
		if(told.asked == ACCESS_ASK_RESET) needed = APP_EVENT_FACTORY_RESET;
	}
	if((events & needed) != needed) broke(PROMISE_KNOB, "the knob confirmed a question and what it asks for was not carried out");
	allowed |= needed;

	if(dialog && was->confirm == NAV_DO_FACTORY_RESET) allowed |= APP_EVENT_FACTORY_RESET;
	if(dialog && was->confirm == NAV_DO_REBOOT) allowed |= APP_EVENT_REBOOT;
	if(dialog && was->confirm == NAV_DO_PREVIOUS_FIRMWARE && was->previous) allowed |= APP_EVENT_PREVIOUS_FIRMWARE;
	if(dialog && !confirmed && (events & RESTARTS) != 0) tally->by_dialog++;
	// An update nobody confirmed asks for the restart; a press of the knob says that it is in order, a tap does not
	if(was->pending && deed != DEED_REQUEST) allowed |= APP_EVENT_REBOOT;
	if(was->pending && deed == DEED_PRESS) allowed |= APP_EVENT_MARK_VALID;
	// Settings are changed at the device as well
	if(deed != DEED_REQUEST)
	{
		allowed |= APP_EVENT_STORE_SETTINGS;
		if(events & APP_EVENT_STORE_SETTINGS) told.settings = app->settings;
	}

	if(events & ~allowed & APP_EVENT_INSTALL_FIRMWARE) broke(PROMISE_KNOB, "a firmware is installed that the knob did not confirm");
	if(events & ~allowed & APP_EVENT_FACTORY_RESET) broke(PROMISE_KNOB, "a factory reset is carried out that the knob did not confirm");
	if(events & ~allowed & APP_EVENT_STORE_WIFI) broke(PROMISE_KNOB, "networks are stored without a confirmed question and without a forget request");
	if(events & ~allowed & APP_EVENT_PREVIOUS_FIRMWARE) broke(PROMISE_KNOB, "the other slot is started although it holds no version to go back to, or without the dialog");
	if(events & ~allowed & ~(APP_EVENT_INSTALL_FIRMWARE | APP_EVENT_FACTORY_RESET | APP_EVENT_STORE_WIFI | APP_EVENT_PREVIOUS_FIRMWARE)) broke(PROMISE_EVENTS, "an event was raised that nothing asked for");
}

/* One request ------------------------------------------------------------------------------------------ */

static int column(void)
{
	switch(code)
	{
		case 0:   return A_GO;
		case 200: return A_200;
		case 202: return A_202;
		case 400: return A_400;
		case 403: return A_403;
		case 404: return A_404;
		case 409: return strcmp(reply, B_BUSY) == 0 ? A_BUSY : strcmp(reply, B_HOT) == 0 ? A_HOT : A_ASKING;
		case 422: return A_422;
		default:  return A_500;
	}
}

// The answer that was foretold: the status, and the body where the rule names one
typedef struct
{
	int code;
	const char *body;
} foretold_t;

static foretold_t foretell(int status, const char *body)
{
	foretold_t answer = {status, body};

	return answer;
}

// The refusals every change begins with
static bool closed(const seen_t *seen, foretold_t *answer)
{
	if(seen->open) return false;

	*answer = foretell(403, B_LOCKED);
	return true;
}

// ... and every question: locked, then busy, then asking, then hot
static bool unasked(const seen_t *seen, bool busy, foretold_t *answer)
{
	if(closed(seen, answer)) return true;

	if(busy) *answer = foretell(409, B_BUSY);
	else if(seen->asking) *answer = foretell(409, B_ASKING);
	else if(seen->hot) *answer = foretell(409, B_HOT);
	else return false;
	return true;
}

// The ticket in an answer to a question
static uint32_t ticket_of_answer(void)
{
	return strncmp(reply, "{\"ticket\":", 10) == 0 ? (uint32_t)strtoul(reply + 10, NULL, 10) : 0;
}

// A question was asked with the last request: the test keeps what it asks for
static void asked_now(const seen_t *seen, access_ask_t question, int network, const char *detail)
{
	if(told.asked != ACCESS_ASK_NONE && !told.over) tally->replaced++;
	if(app->has_wifi_asked && question != ACCESS_ASK_WIFI) broke(PROMISE_ASKED, "a question that asks for no network left one in the room of the request");
	if(seen->asking || (seen->uploading && question != ACCESS_ASK_FIRMWARE)) broke(PROMISE_ASKED, "a question was asked while another one waited or an upload ran");
	if(seen->hot) broke(PROMISE_ASKED, "a question was asked on a screen the heat keeps dark");
	if(seen->under_way && question == ACCESS_ASK_WIFI) broke(PROMISE_ASKED, "a network was asked for while a fault memory request of the display was under way");

	told.tickets++;
	told.asked = question;
	told.asked_ms = seen->time;
	told.network = network;
	told.over = false;
	if(ticket_of_answer() != told.tickets) broke(PROMISE_ANSWER, "the ticket of the answer is not the next one");
	if(access_asking(&app->access, seen->time) != question || access_ticket(&app->access, told.tickets, seen->time) != ACCESS_TICKET_WAITING) broke(PROMISE_ASKED, "the question that was answered with a ticket does not wait");
	if(strcmp(app->ask_detail, detail) != 0) broke(PROMISE_ASKED, "the detail of the question is not what was asked for");
	if(question == ACCESS_ASK_WIFI)
	{
		const walk_network_t *wanted_network = &WALK_NETWORKS[network];

		if(!app->has_wifi_asked || strcmp(app->wifi_asked.ssid, wanted_network->ssid) != 0 || strcmp(app->wifi_asked.host, wanted_network->host) != 0 ||
		   app->wifi_asked.has_password != (wanted_network->password != NULL) || strcmp(app->wifi_asked.password, wanted_network->password != NULL ? wanted_network->password : "") != 0)
		{
			broke(PROMISE_ASKED, "the network that is kept for the knob is not the one that was sent");
		}
	}
}

// The browser has decided to ask a question next, or to send a firmware
static bool ask_next, upload_next;

static void request_deed(void)
{
	static uint8_t walk_image[OTA_CHECK_BYTES];
	kind_t kind = upload_next ? K_BEGIN : ask_next ? (chance(70) ? K_WIFI : K_RESET) : (kind_t)pick(KINDS);
	bool direct = chance(35);
	uint64_t at = now;
	int variant = 0;
	const char *body = "";
	size_t body_length;
	web_method_t method = WEB_POST;
	const char *target = "";
	web_route_t route = WEB_ROUTE_NONE;
	bool good = true;
	bool reads = kind <= K_TICKET;
	seen_t seen;
	foretold_t answer = {200, NULL};
	uint32_t by_web = 0;
	uint32_t ticket = 1 + (uint32_t)pick(4);
	char ticket_target[40], version[40] = "", forgotten[NET_SSID_SIZE] = "";
	const char *forget_name;
	earlier_t was = earlier();
	bool forgot = false;
	bool leftover, ended;
	access_t release;
	unsigned may = MAY_CLOCK;

	ask_next = false;
	upload_next = false;
	// The server read its clock before it waited for the lock, and the tasks of the display went on since.
	// Its time is never ahead of the clock they all read: `now` is the time of the next reading of the switch.
	if(chance(25)) at = now > 3000 ? now - (uint64_t)pick(3000) : 0;

	doing = KIND_NAMES[kind];
	switch(kind)
	{
		case K_INFO:        method = WEB_GET; target = "/api/info"; route = WEB_ROUTE_INFO; break;
		case K_CATALOG:     method = WEB_GET; target = "/api/catalog"; route = WEB_ROUTE_CATALOG; break;
		case K_VALUES:      method = WEB_GET; target = "/api/values"; route = WEB_ROUTE_VALUES; break;
		case K_LAYOUT:      method = WEB_GET; target = "/api/layout"; route = WEB_ROUTE_LAYOUT; break;
		case K_DTC_LAST:    method = WEB_GET; target = "/api/dtc/last"; route = WEB_ROUTE_DTC_LAST; break;
		case K_WIFI_LIST:   method = WEB_GET; target = "/api/wifi"; route = WEB_ROUTE_WIFI; break;
		case K_TICKET:
			snprintf(ticket_target, sizeof(ticket_target), "/api/ticket?id=%" PRIu32, ticket);
			method = WEB_GET;
			target = ticket_target;
			route = WEB_ROUTE_TICKET;
			break;
		case K_CHECK:
		case K_APPLY:
		case K_SAVE:
			variant = pick(COUNT(WALK_LAYOUTS));
			body = WALK_LAYOUTS[variant].text;
			good = WALK_LAYOUTS[variant].good;
			method = WEB_PUT;
			target = kind == K_CHECK ? "/api/layout?mode=check" : kind == K_APPLY ? "/api/layout?mode=apply" : "/api/layout?mode=save";
			route = kind == K_CHECK ? WEB_ROUTE_LAYOUT_CHECK : kind == K_APPLY ? WEB_ROUTE_LAYOUT_APPLY : WEB_ROUTE_LAYOUT_SAVE;
			break;
		case K_LAYOUT_RESET: target = "/api/layout/reset"; route = WEB_ROUTE_LAYOUT_RESET; break;
		case K_WIFI:
			variant = pick(COUNT(WALK_NETWORKS));
			body = WALK_NETWORKS[variant].body;
			good = WALK_NETWORKS[variant].ssid != NULL;
			target = "/api/wifi";
			route = WEB_ROUTE_WIFI_STORE;
			break;
		case K_FORGET:
			variant = pick(COUNT(WALK_FORGETS) + 1);
			good = variant < COUNT(WALK_FORGETS);
			// Mostly a network that is stored
			if(told.profile_count > 0 && chance(60)) forget_name = told.profiles[pick(told.profile_count)].ssid;
			else forget_name = good ? WALK_FORGETS[variant] : "";
			good = forget_name[0] != '\0';
			snprintf(expected, sizeof(expected), "{\"ssid\":\"%s\"}", forget_name);
			snprintf(forgotten, sizeof(forgotten), "%s", forget_name);
			body = expected;
			target = "/api/wifi/forget";
			route = WEB_ROUTE_WIFI_FORGET;
			break;
		case K_SETTINGS:
			variant = pick(COUNT(WALK_SETTINGS));
			body = WALK_SETTINGS[variant].body;
			good = variant < WALK_SETTINGS_GOOD;
			target = "/api/settings";
			route = WEB_ROUTE_SETTINGS;
			break;
		case K_REBOOT:      target = "/api/reboot"; route = WEB_ROUTE_REBOOT; break;
		case K_RESET:       target = "/api/reset"; route = WEB_ROUTE_RESET; break;
		case K_BEGIN:
			variant = pick(1000);
			good = chance(75);
			snprintf(version, sizeof(version), "9.%d.%d", variant, (int)run_number);
			make_image(version);
			// A file that is no firmware of the display, in one of three ways
			if(!good) image[variant % 3 == 0 ? 0 : variant % 3 == 1 ? 12 : 85] ^= 0x40;
			memcpy(walk_image, image, sizeof(walk_image));
			target = "/api/ota";
			route = WEB_ROUTE_OTA;
			file_size = FILE_SIZE;
			break;
		case K_PROGRESS:
		case K_END:
			direct = true;
			good = chance(80);
			break;
		default:
			break;
	}
	body_length = strlen(body);
	if(kind == K_BEGIN)
	{
		body = (const char *)walk_image;
		body_length = OTA_CHECK_BYTES;
	}

	seen = look(at);
	// What a question that is over left in the room of the request
	leftover = app->has_wifi_asked && !seen.asking;
	remember();
	gathered = 0;
	tally->requests++;
	if(at < app->clock_ms) tally->back++;
	if(at > app->clock_ms) tally->ahead++;
	if(seen.hot) tally->hot++;
	if(seen.busy && seen.uploading) tally->busy_upload++;
	else if(seen.busy && app->nav.screen == NAV_DTC_CONFIRM) tally->busy_dialog++;
	else if(seen.busy) tally->busy_read++;

	if(kind == K_PROGRESS)
	{
		uint32_t written = (uint32_t)pick(FILE_SIZE);

		app_web_upload_progress(app, written, FILE_SIZE, given(at));
		carry_out();
		// Nothing but the upload that runs is touched, not the release either, and one that is over stays over
		if(seen.uploading && (app->upload_percent != (int)((uint64_t)written * 100 / FILE_SIZE) || app->upload_ms != seen.time)) broke(PROMISE_ANSWER, "percent or time of the upload are not those of its bytes");
		if(seen.uploading)
		{
			before.upload_ms = app->upload_ms;
			before.upload_percent = app->upload_percent;
		}
		memcpy(&release, &before.access, sizeof(release));
		if(!nothing_but(MAY_CLOCK, &release, seen.time) || gathered != 0) broke(PROMISE_REFUSED, "the bytes of an upload changed something else than its percent and its time");
		tally->answers[kind][A_GO]++;
		judge(DEED_REQUEST, &was, 0);
		return;
	}

	// The request
	if(kind == K_END)
	{
		fresh_room();
		code = app_web_upload_end(app, good, reply, &reply_length, given(at));
		by_route = false;
		carry_out();
	}
	else if(!direct)
	{
		serve(method, target, body, body_length, at);
	}
	else
	{
		fresh_room();
		by_route = false;
		given(at);
		if(reads) code = app_web_get(app, route, ticket, reply, &reply_length, at);
		else if(kind <= K_LAYOUT_RESET) code = app_web_layout(app, route, body, body_length, reply, &reply_length, at);
		else if(kind <= K_FORGET) code = app_web_wifi(app, route, body, body_length, reply, &reply_length, at);
		else if(kind == K_SETTINGS) code = app_web_settings(app, body, body_length, reply, &reply_length, at);
		else if(kind <= K_RESET) code = app_web_action(app, route, reply, &reply_length, at);
		else
		{
			code = app_web_upload_begin(app, walk_image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, reply, &reply_length, at);
			if(code == 0) machine.previous_firmware = false;
		}
		carry_out();
		tally->direct++;
	}
	tally->answers[kind][column()]++;

	// What holds for every answer
	if(!sound()) broke(PROMISE_SOUND, "an answer is no JSON, has no end or leaves its room");
	tally->passwords++;
	for(int i = 0; i < COUNT(WALK_PASSWORDS); i++)
	{
		if(code != 0 && strstr(reply, WALK_PASSWORDS[i]) != NULL) broke(PROMISE_PASSWORD, "an answer holds a password");
	}
	if(!discreet()) broke(PROMISE_PASSWORD, "an answer holds a password that is stored or asked for");
	if((app->poll.flow.phase != seen.phase && (app->poll.flow.phase == DTC_FLOW_READ_SENT || app->poll.flow.phase == DTC_FLOW_CLEAR_SENT)) ||
	   (app->poll.flow.to_send != seen.to_send && app->poll.flow.to_send != DTC_FLOW_SEND_NOTHING) || sent[POLL_DTC_READ] != seen.reads || sent[POLL_DTC_CLEAR] != seen.clears)
	{
		broke(PROMISE_DTC, "a request of the browser started a read or a clear of the fault memory");
	}

	// Refused before any function ran: web_route() has its own test, here it must have had a reason
	if(by_route)
	{
		tally->by_route++;
		if(!untouched() || gathered != 0) broke(PROMISE_REFUSED, "a request that web_route() refused changed something");
		if(!(code == 403 && !seen.open) && !(code == 409 && seen.busy)) broke(PROMISE_ANSWER, "web_route() refused a request without the release being closed or the display busy");
		judge(DEED_REQUEST, &was, 0);
		return;
	}

	// The list of refusals of app_web.h, in its order
	switch(kind)
	{
		case K_CHECK:
			answer = foretell(good ? 200 : 400, NULL);
			break;
		case K_APPLY:
		case K_SAVE:
			if(!closed(&seen, &answer)) answer = foretell(good ? 200 : 400, NULL);
			break;
		case K_LAYOUT_RESET:
			closed(&seen, &answer);
			break;
		case K_WIFI:
			if(!unasked(&seen, seen.uploading || seen.under_way, &answer)) answer = good ? foretell(202, NULL) : foretell(400, B_BODY);
			break;
		case K_FORGET:
			if(closed(&seen, &answer)) break;
			// Whatever the body is: the display would leave its network
			if(seen.under_way) answer = foretell(409, B_BUSY);
			else if(!good) answer = foretell(400, B_BODY);
			else if(!told_forget(forgotten)) answer = foretell(404, B_NOT_FOUND);
			else
			{
				answer = foretell(200, B_OK);
				forgot = true;
			}
			break;
		case K_SETTINGS:
			if(!closed(&seen, &answer)) answer = foretell(good ? 200 : 400, NULL);
			break;
		case K_REBOOT:
			if(closed(&seen, &answer)) break;
			answer = seen.busy ? foretell(409, B_BUSY) : foretell(200, B_OK);
			break;
		case K_RESET:
			if(!unasked(&seen, seen.busy, &answer)) answer = foretell(202, NULL);
			break;
		case K_BEGIN:
			if(closed(&seen, &answer)) break;
			if(seen.left_s < APP_WEB_UPLOAD_LEFT_S) answer = foretell(403, B_LOCKED);
			else if(seen.busy || was.pending)
			{
				answer = foretell(409, B_BUSY);
				if(!seen.busy) tally->unconfirmed++;
			}
			else if(seen.asking) answer = foretell(409, B_ASKING);
			else if(seen.hot) answer = foretell(409, B_HOT);
			else answer = foretell(good ? 0 : 422, NULL);
			break;
		case K_END:
			if(!good || !seen.uploading) answer = foretell(500, B_UPLOAD);
			else if(!unasked(&seen, false, &answer)) answer = foretell(202, NULL);
			break;
		default:
			break;
	}
	if(code != answer.code || (answer.body != NULL && strcmp(reply, answer.body) != 0))
	{
		printf("  foretold %d %s, answered %d %.200s\n", answer.code, answer.body != NULL ? answer.body : "", code, code != 0 ? reply : "");
		broke(PROMISE_ANSWER, "the answer is not the one the list of refusals of app_web.h gives");
		// What the display holds now is not known any more: the run goes on with what it says
		memcpy(told.profiles, app->profiles, sizeof(told.profiles));
		told.profile_count = app->profile_count;
	}

	// The release behind the request: what one call of access.h at the time of the request makes of it
	memcpy(&release, &before.access, sizeof(release));
	if(kind == K_WIFI || kind == K_RESET || kind == K_END)
	{
		// A question renews it when it is asked, and only then
		if(code == 202) access_ask(&release, kind == K_WIFI ? ACCESS_ASK_WIFI : kind == K_RESET ? ACCESS_ASK_RESET : ACCESS_ASK_FIRMWARE, seen.time);
	}
	else if(!reads && kind != K_CHECK) access_write(&release, seen.time);

	// What the request may have changed, by its kind and its answer
	ended = kind == K_END && seen.uploading;
	if(ended) before.uploading = false;
	if(ended && app->uploading) broke(PROMISE_ANSWER, "the end of an upload left it running");
	if(reads) may = 0;
	else if(kind == K_CHECK) may = code == 200 ? MAY_CHECKED : 0;
	else if(code < 400) switch(kind)
	{
		case K_APPLY:
		case K_SAVE:         may |= MAY_LAYOUT | MAY_CHECKED; break;
		case K_LAYOUT_RESET: may |= MAY_LAYOUT; break;
		case K_WIFI:
		case K_RESET:
		case K_END:          may |= MAY_ASKED; break;
		case K_FORGET:       may |= MAY_NETWORKS; break;
		case K_SETTINGS:     may |= MAY_SETTINGS; break;
		case K_BEGIN:        may |= MAY_UPLOAD; break;
		default:             break;
	}
	if(!nothing_but(may, &release, seen.time))
	{
		if(reads || kind == K_CHECK) broke(PROMISE_READING, "a reading request or the check of a layout changed something");
		else if(code >= 400) broke(PROMISE_REFUSED, "a request that was refused changed something else than the time, or the release in another way than one change does");
		else broke(PROMISE_FRAME, "a request that was accepted changed something app_web.h does not name for it, or the release in another way than one change does");
	}
	if((reads || kind == K_CHECK || code >= 400) && gathered != 0) broke(PROMISE_REFUSED, "a request that changes nothing raised an event");
	if(!reads && kind != K_CHECK && !seen.open)
	{
		tally->closed++;
		// With the release closed not even the release changes: it stays closed, and no question appears
		if(code < 400) broke(PROMISE_CLOSED, "a change was accepted without the release");
		if(access_is_open(&app->access, seen.time) || access_asking(&app->access, seen.time) != ACCESS_ASK_NONE || gathered != 0) broke(PROMISE_CLOSED, "without the release something changed");
	}

	// What an accepted request leaves
	if(code == answer.code && code < 400 && !reads) switch(kind)
	{
		case K_APPLY:
		case K_SAVE:
			told.views = variant;
			told.saved = kind == K_SAVE;
			if(kind == K_SAVE)
			{
				told.flash_views = variant;
				by_web = APP_EVENT_STORE_LAYOUT;
				tally->saves++;
			}
			else tally->previews++;
			if(!in_use(body)) broke(PROMISE_ANSWER, "a layout that was taken is not in use");
			// The page shown before stays where the layout shows one at that position; else its first page
			if(layout_page_shown(&app->layout, seen.page, &app->poll.catalog))
			{
				if(app->nav.page != seen.page) broke(PROMISE_ANSWER, "a layout that shows a page at the position shown before left that page");
				tally->pages_kept[seen.page > 0]++;
			}
			else
			{
				if(app->nav.page != layout_first_page(&app->layout, &app->poll.catalog)) broke(PROMISE_ANSWER, "a layout without a page at the position shown before is not shown from its first page");
				tally->pages_first[seen.page >= 0]++;
			}
			break;
		case K_LAYOUT_RESET:
			told.views = -1;
			told.saved = false;
			told.flash_views = -1;
			by_web = APP_EVENT_ERASE_LAYOUT;
			tally->resets++;
			if((app->source != APP_LAYOUT_BUILTIN && app->source != APP_LAYOUT_GENERATED) || app->nav.page != layout_first_page(&app->layout, &app->poll.catalog))
			{
				broke(PROMISE_ANSWER, "after a reset the views are not those the display chooses, from their first page");
			}
			break;
		case K_WIFI:
			asked_now(&seen, ACCESS_ASK_WIFI, variant, WALK_NETWORKS[variant].ssid);
			break;
		case K_FORGET:
			by_web = APP_EVENT_STORE_WIFI;
			tally->forgotten++;
			break;
		case K_SETTINGS:
			if(WALK_SETTINGS[variant].brightness >= 0) told.settings.brightness = (uint8_t)WALK_SETTINGS[variant].brightness;
			if(WALK_SETTINGS[variant].night >= 0) told.settings.night = (uint8_t)WALK_SETTINGS[variant].night;
			if(WALK_SETTINGS[variant].night_mode >= 0) told.settings.night_mode = WALK_SETTINGS[variant].night_mode == 1;
			if(WALK_SETTINGS[variant].reverse >= 0) told.settings.reverse = WALK_SETTINGS[variant].reverse == 1;
			if(WALK_SETTINGS[variant].standby_s >= 0) told.settings.standby_s = (uint16_t)WALK_SETTINGS[variant].standby_s;
			by_web = APP_EVENT_STORE_SETTINGS;
			tally->settings++;
			break;
		case K_REBOOT:
			by_web = APP_EVENT_REBOOT;
			break;
		case K_RESET:
			asked_now(&seen, ACCESS_ASK_RESET, 0, "");
			break;
		case K_BEGIN:
			told.upload = true;
			told.previous = false;
			strcpy(told.upload_version, version);
			if(!app->uploading || app->upload_percent != 0 || app->upload_ms != seen.time || strcmp(app->upload_version, told.upload_version) != 0 || access_seconds_left(&app->access, seen.time) != seen.left_s)
			{
				broke(PROMISE_ANSWER, "an upload that began does not run with 0 percent, its time, its version and the release renewed");
			}
			break;
		case K_END:
			strcpy(told.version, told.upload_version);
			asked_now(&seen, ACCESS_ASK_FIRMWARE, 0, told.version);
			tally->uploads_complete++;
			break;
		default:
			break;
	}
	if(kind == K_END && seen.uploading)
	{
		if(code == 500) tally->uploads_broken++;
		if(code == 403) tally->uploads_locked++;
		told.upload = false;
	}
	if(forgot && seen.under_way) broke(PROMISE_DTC, "a network was forgotten while a fault memory request of the display was under way");
	if(code == 202 && leftover) tally->leftovers++;
	// The events of the request are exactly those its answer stands for
	if((gathered & WEB_EVENTS) != by_web) broke(PROMISE_EVENTS, "the events of a request are not those of its answer");
	if(forgot && (app->link.profile_count != told.profile_count || link_up(&app->link))) broke(PROMISE_ANSWER, "the link was not told the list without the forgotten network");
	judge(DEED_REQUEST, &was, by_web);
}

/* One input, or time ----------------------------------------------------------------------------------- */

// Back on the value pages from wherever the driver is; a question that waits is refused on the way
static void home(void)
{
	for(int i = 0; i < 6; i++)
	{
		if(access_asking(&app->access, now) != ACCESS_ASK_NONE) long_press();
		else if(on(NAV_DTC_CONFIRM)) tap(0);
		else if(!on(NAV_PAGES)) long_press();
		else return;
	}
}

// The driver has decided to press the knob next, and to tap on the screen before that
static bool press_next, tap_next;

static void device_deed(void)
{
	static web_seen_t found[3] = {{"Werkstatt", -52, true}, {"", -70, true}, {"Freifunk", -88, false}};
	earlier_t was;
	bool was_uploading = app->uploading;
	deed_t deed = DEED_OTHER;
	int what_now = tap_next ? 14 : press_next ? 0 : pick(100);

	// The heat does not last: while it keeps the screen dark nothing can be done at the display
	if(app->heat == GUARD_HEAT_OFF && chance(35)) app_temperature(app, 40 + pick(30), true);
	was = earlier();
	if(tap_next) tap_next = false;
	else press_next = false;
	gathered = 0;
	if(what_now < 14)
	{
		doing = "short press";
		deed = DEED_PRESS;
		short_press();
	}
	else if(what_now < 20)
	{
		// On a row, on none, and behind the rows of every screen
		doing = "tap";
		deed = DEED_TAP;
		if(app_backlight(app, now) != 0 && !app->uploading)
		{
			if(access_asking(&app->access, now) != ACCESS_ASK_NONE && now >= told.asked_ms + ACCESS_ASK_SHOWN_MS) tally->taps_on_questions++;
			else if(access_asking(&app->access, now) == ACCESS_ASK_NONE && app->update_pending) tally->taps_on_updates++;
		}
		tap(pick(9) - 1);
	}
	else if(what_now < 26)
	{
		doing = "long press";
		long_press();
	}
	else if(what_now < 38)
	{
		doing = "turn";
		turn(pick(7) - 3);
	}
	else if(what_now < 41)
	{
		doing = "swipe";
		swipe_anywhere();
	}
	else if(what_now < 51)
	{
		doing = "release given";
		app_do(app, NAV_DO_RELEASE_ON, given(now));
	}
	else if(what_now < 53)
	{
		doing = "release taken back";
		app_do(app, NAV_DO_RELEASE_OFF, given(now));
	}
	else if(what_now < 61)
	{
		// The driver waits until the question counts, and mostly says yes - now and then with a finger first
		doing = "wait for the question";
		deed = DEED_WAIT;
		run(1500);
		press_next = chance(70);
		tap_next = chance(30);
	}
	else if(what_now < 65)
	{
		doing = "read at the knob";
		app_do(app, NAV_DO_READ, given(now));
	}
	else if(what_now < 69)
	{
		// Menu, Fehlerspeicher, Liste ansehen, Fehler löschen
		doing = "the clear dialog";
		home();
		if(on(NAV_PAGES) && phase() == DTC_FLOW_LIST && !app->uploading && !app->update_pending)
		{
			short_press();
			tap(0);
			tap(1);
			tap(app->list_lines + 1);
		}
	}
	else if(what_now < 73)
	{
		// Menu, Einstellungen, one of Neustart, Vorherige Version, Werkseinstellungen, the focus on Ausführen
		doing = "the settings of the device";
		home();
		if(on(NAV_PAGES) && !app->uploading && !app->update_pending)
		{
			short_press();
			tap(5);
			tap(2 + pick(3));
			turn(app->settings.reverse ? -1 : 1);
			press_next = chance(50);
		}
	}
	else if(what_now < 75)
	{
		doing = "networks seen";
		app_web_seen(app, found, pick(4));
	}
	else if(what_now < 79)
	{
		// Until the question that waits has expired, and not as far as the tick that drops what it asked for:
		// the browser asks again at once
		doing = "the question expires";
		deed = DEED_WAIT;
		if(told.asked == ACCESS_ASK_WIFI && !told.over && access_asking(&app->access, now) == ACCESS_ASK_WIFI)
		{
			run_to(told.asked_ms + ACCESS_CONFIRM_MS);
			ask_next = true;
		}
	}
	else if(what_now < 89)
	{
		doing = "time";
		deed = DEED_WAIT;
		run(20u * (uint64_t)(1 + pick(100)));
	}
	else if(what_now < 92)
	{
		// The board gets too hot to keep its light on, mostly while something is asked or arrives
		bool asked = access_asking(&app->access, now) != ACCESS_ASK_NONE;

		doing = "heat";
		if(asked || app->uploading || chance(40))
		{
			app_temperature(app, GUARD_TEMP_OFF_C + pick(10), true);
			if(asked) tally->heat_refused++;
			if(asked && (ticket_now() != ACCESS_TICKET_REFUSED || app->has_wifi_asked || app->ask_detail[0] != '\0')) broke(PROMISE_ASKED, "the heat switched the light off and the question that waited was not refused and dropped");
		}
	}
	else
	{
		// Long enough for a question to expire, an upload to stall, a release to end
		static const uint32_t waits[] = {31000, 61000, 61000, 290000, 601000};

		doing = "a long time";
		deed = DEED_WAIT;
		stride = 1000;
		run(waits[pick(COUNT(waits))]);
		stride = STEP_MS;
	}
	if(deed == DEED_WAIT) tally->waits++;
	else tally->inputs++;

	// The 30 s of an upload that brings nothing run out during a press as well
	if(was_uploading && !app->uploading) tally->uploads_stalled++;
	judge(deed, &was, 0);
}

static void walk_run(uint32_t number)
{
	run_number = number;
	deed_number = 0;
	dice = number * 2654435761u + 40503u;
	doing = "start";

	if(chance(12)) factory();
	else garage();
	if(chance(40))
	{
		strcpy(flash.profiles[flash.profile_count].ssid, "Camping");
		strcpy(flash.profiles[flash.profile_count].password, "zelt-und-wurst");
		flash.profile_count++;
		flash.has_wifi = true;
	}
	strcpy(wifi.in_range[wifi.in_range_count++], "Neu");
	strcpy(wifi.in_range[wifi.in_range_count++], "Camping");
	wifi.found = "192.168.1.50";
	if(chance(25))
	{
		wican.config = other_config;
		wican.values = other_values;
	}
	if(chance(30))
	{
		strcpy(flash.layout, stored_layout);
		flash.has_layout = true;
	}
	machine.previous_firmware = chance(60);
	machine.update_pending = chance(25);
	watch_events = gather;

	gathered = 0;
	start();
	tally->starts++;
	told_begin();
	run(2100);
	held();

	press_next = false;
	tap_next = false;
	ask_next = false;
	// Somebody who sends the next firmware before he has confirmed the one that runs
	upload_next = machine.update_pending && chance(60);
	if(upload_next) app_do(app, NAV_DO_RELEASE_ON, given(now));
	for(deed_number = 1; deed_number <= RUN_DEEDS; deed_number++)
	{
		if(ask_next || upload_next || (!press_next && !tap_next && chance(62))) request_deed();
		else device_deed();
		held();
		// The platform restarts when it has carried out what waits
		if(gathered & RESTARTS)
		{
			doing = "restart";
			done.last = gathered;
			walk_restart();
			held();
		}
	}
}

static void test_random_walk(void)
{
	static const char *const promises[PROMISES] = {
		"in every random run status and body of every request are what the refusals of app_web.h give in their order, and what an accepted request leaves is what it asked for",
		"in every random run nothing changes without the release: no request is accepted, the release stays closed, no question appears, no event is raised",
		"in every random run a request that is refused leaves everything as it was but the release and the time, and bytes of an upload touch nothing but its percent and its time",
		"in every random run a reading request and the check of a layout change nothing, not even the time",
		"in every random run no question is asked while one waits, an upload runs or the heat keeps the screen dark, no network while a fault memory request is under way, "
		"the question that waits holds exactly what was sent, and the heat refuses and drops the one that waits when it switches the light off",
		"in every random run what a question asks for is carried out only with the press of the knob that confirmed that very question, 1500 ms or more after it was asked - never by a tap, "
		"and no tap says that an update is in order",
		"in every random run every event is the one of the request that was answered or of what happened at the device",
		"in every random run no answer holds a password, stored, asked for or of the own access point",
		"in every random run no request of the browser starts a read or a clear of the fault memory, and none forgets a network while one is under way",
		"in every random run every answer is JSON as json.h reads it, ends at its length and stays within its room",
		"in every random run networks, settings and views in use and in the flash are those that were sent and confirmed, also after a restart",
		"in every random run no byte outside of the app is written",
		"in every random run a request that is accepted changes exactly the parts of the app that app_web.h names for it, and leaves the release as one change or one question at its time does",
	};
	// What the runs have to reach, by kind and answer: at least this many times
	static const struct
	{
		kind_t kind;
		int answer;
		long least;
	} reach[] = {
		{K_INFO, A_200, 300}, {K_CATALOG, A_200, 300}, {K_VALUES, A_200, 300}, {K_LAYOUT, A_200, 300}, {K_DTC_LAST, A_200, 300}, {K_WIFI_LIST, A_200, 300}, {K_TICKET, A_200, 300},
		{K_CHECK, A_200, 200}, {K_CHECK, A_400, 100},
		{K_APPLY, A_200, 100}, {K_APPLY, A_400, 50}, {K_APPLY, A_403, 100},
		{K_SAVE, A_200, 100}, {K_SAVE, A_400, 50}, {K_SAVE, A_403, 100},
		{K_LAYOUT_RESET, A_200, 100}, {K_LAYOUT_RESET, A_403, 100},
		{K_WIFI, A_202, 50}, {K_WIFI, A_400, 20}, {K_WIFI, A_403, 100}, {K_WIFI, A_BUSY, 20}, {K_WIFI, A_ASKING, 20}, {K_WIFI, A_HOT, 5},
		{K_FORGET, A_200, 20}, {K_FORGET, A_400, 20}, {K_FORGET, A_403, 100}, {K_FORGET, A_404, 100}, {K_FORGET, A_BUSY, 10},
		{K_SETTINGS, A_200, 100}, {K_SETTINGS, A_400, 50}, {K_SETTINGS, A_403, 100},
		{K_REBOOT, A_200, 20}, {K_REBOOT, A_403, 100}, {K_REBOOT, A_BUSY, 20},
		{K_RESET, A_202, 30}, {K_RESET, A_403, 100}, {K_RESET, A_BUSY, 20}, {K_RESET, A_ASKING, 10}, {K_RESET, A_HOT, 3},
		{K_BEGIN, A_GO, 30}, {K_BEGIN, A_403, 100}, {K_BEGIN, A_BUSY, 20}, {K_BEGIN, A_ASKING, 5}, {K_BEGIN, A_HOT, 3}, {K_BEGIN, A_422, 10},
		{K_PROGRESS, A_GO, 300},
		{K_END, A_202, 5}, {K_END, A_HOT, 1}, {K_END, A_500, 200},
	};
	walk_result_t result;
	int ends[2];
	int status = -1;
	bool complete = false;
	bool reached = true;
	pid_t child;

	memset(&result, 0, sizeof(result));
	fflush(stdout);
	if(pipe(ends) != 0)
	{
		check(false, "the random runs have a pipe for their result");
		return;
	}

	// In a child process: a crash or a hang of the module is then a failed check here, not the end of the test
	alarm(240);
	child = fork();
	if(child == 0)
	{
		close(ends[0]);
		alarm(180);
		tally = &result;
		for(uint32_t number = 1; number <= RUNS; number++) walk_run(number);
		fflush(stdout);
		_exit(write(ends[1], &result, sizeof(result)) == (ssize_t)sizeof(result) ? 0 : 1);
	}
	close(ends[1]);
	if(child > 0)
	{
		complete = read(ends[0], &result, sizeof(result)) == (ssize_t)sizeof(result);
		if(waitpid(child, &status, 0) != child) status = -1;
	}
	close(ends[0]);
	alarm(0);

	printf("  random runs: %ld requests (%ld passed to the function alone, %ld refused by web_route(), %ld with a time before the one of the app, %ld ahead of it), %ld inputs, %ld waits, %ld starts\n",
	       result.requests, result.direct, result.by_route, result.back, result.ahead, result.inputs, result.waits, result.starts);
	printf("  random runs: answers by kind (go 200 202 400 403 404 busy asking hot 422 500):");
	for(int kind = 0; kind < KINDS; kind++)
	{
		printf(" %s", KIND_NAMES[kind]);
		for(int i = 0; i < ANSWERS; i++) printf(" %ld", result.answers[kind][i]);
		printf(";");
	}
	printf("\n  random runs: %ld changes asked for without the release; questions confirmed: %ld network, %ld firmware, %ld reset; %ld refused, %ld expired, %ld asked over what an earlier one left; "
	       "uploads: %ld complete, %ld stalled, %ld broken, %ld with the release gone, %ld kept away by an unconfirmed firmware; %ld restarts and resets by the dialog of the device; requests while busy by a read %ld, by the clear dialog %ld, "
	       "by an upload %ld; %ld previews, %ld saves, %ld resets, %ld networks forgotten, %ld settings; %ld answers searched for passwords\n",
	       result.closed, result.confirmed[ACCESS_ASK_WIFI], result.confirmed[ACCESS_ASK_FIRMWARE], result.confirmed[ACCESS_ASK_RESET], result.refused, result.expired, result.leftovers,
	       result.uploads_complete, result.uploads_stalled, result.uploads_broken, result.uploads_locked, result.unconfirmed, result.by_dialog, result.busy_read, result.busy_dialog, result.busy_upload,
	       result.previews, result.saves, result.resets, result.forgotten, result.settings, result.passwords);
	printf("  random runs: %ld requests while the heat kept the screen dark, %ld questions that waited when it switched the light off; %ld taps on a question the knob could have confirmed, "
	       "%ld on the update question\n", result.hot, result.heat_refused, result.taps_on_questions, result.taps_on_updates);
	printf("  random runs: layouts taken that kept the page shown: the first page %ld, a later one %ld; that started at their first page: no page was shown before %ld, "
	       "none at the position %ld\n", result.pages_kept[0], result.pages_kept[1], result.pages_first[0], result.pages_first[1]);
	printf("  random runs: broken promises:");
	for(int i = 0; i < PROMISES; i++) printf(" %s %ld", PROMISE_NAMES[i], result.broken[i]);
	printf("\n");

	check(complete && status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0, "60 random runs of 400 deeds each: no crash and no hang");
	for(int i = 0; i < PROMISES; i++) check(complete && result.broken[i] == 0, promises[i]);

	for(int i = 0; i < COUNT(reach); i++)
	{
		if(result.answers[reach[i].kind][reach[i].answer] >= reach[i].least) continue;

		printf("  %s was answered %d only %ld times, not %ld\n", KIND_NAMES[reach[i].kind], reach[i].answer, result.answers[reach[i].kind][reach[i].answer], reach[i].least);
		reached = false;
	}
	check(complete && reached, "the random runs reach every answer of every route in numbers: each refusal, each acceptance");
	check(complete && result.requests >= 10000 && result.direct >= 2000 && result.by_route >= 1000 && result.back >= 1000 && result.ahead >= 1000 && result.closed >= 1000 && result.starts >= RUNS + 30,
	      "the random runs send requests through web_route() and to the functions alone, with times before and behind the latest the app has seen, without the release, and restart the display");
	check(complete && result.confirmed[ACCESS_ASK_WIFI] >= 10 && result.confirmed[ACCESS_ASK_FIRMWARE] >= 2 && result.confirmed[ACCESS_ASK_RESET] >= 5 && result.refused >= 10 && result.expired >= 10 &&
	      result.leftovers >= 3, "the random runs confirm questions of each kind at the knob, refuse them, let them expire and ask over what an expired one left behind");
	check(complete && result.uploads_complete >= 5 && result.uploads_stalled >= 5 && result.uploads_broken >= 5 && result.unconfirmed >= 3 && result.busy_read >= 50 && result.busy_dialog >= 20 &&
	      result.busy_upload >= 50 && result.by_dialog >= 3,
	      "the random runs complete uploads, let them stall and break, try them under an unconfirmed firmware, send requests while the display is busy by a read, by the clear dialog and by an upload, "
	      "and restart by the dialog of the device");
	check(complete && result.previews >= 100 && result.saves >= 100 && result.resets >= 100 && result.forgotten >= 20 && result.settings >= 100 && result.passwords >= 10000,
	      "the random runs apply, save and reset layouts, forget networks, change settings and search every answer for passwords");
	check(complete && result.pages_kept[0] >= 50 && result.pages_kept[1] >= 20 && result.pages_first[0] >= 30 && result.pages_first[1] >= 80,
	      "the random runs apply and save layouts that keep the page shown, the first and a later one, and layouts that start at their first page because none was shown "
	      "or because they show none at that position");
	check(complete && result.hot >= 200 && result.heat_refused >= 5 && result.taps_on_questions >= 10 && result.taps_on_updates >= 5,
	      "the random runs send requests while the heat keeps the screen dark, let it switch the light off under questions that wait, and tap on questions the knob could have confirmed and on the update question");
}

int main(void)
{
	load_fixtures();
	check(read_fixture("fixtures/app_web_layout_warning.json", walk_warning, sizeof(walk_warning)) && read_fixture("fixtures/app_web_layout_hidden.json", walk_hidden, sizeof(walk_hidden)) &&
	      read_fixture("fixtures/app_web_layout_refused.json", walk_refused, sizeof(walk_refused)), "the layouts of the random runs are there");
	test_constants();
	test_request();
	test_info();
	test_info_long();
	test_values_and_catalog();
	test_largest_answers();
	test_layout_text();
	test_dtc_last();
	test_wifi_list();
	test_tickets();
	test_other_routes();
	test_without_pointers();
	test_layout_check();
	test_layout_change();
	test_wifi_store();
	test_wifi_story();
	test_wifi_forget();
	test_under_way();
	test_settings();
	test_reboot();
	test_reset();
	test_upload_story();
	test_upload_refused();
	test_upload_order();
	test_upload_and_update();
	test_upload_ends();
	test_upload_progress();
	test_heat();
	test_layout_events();
	test_frame();
	test_body_length();
	test_events_add_up();
	test_last_millisecond();
	test_time();
	test_stories();
	test_random_walk();

	return test_end();
}
