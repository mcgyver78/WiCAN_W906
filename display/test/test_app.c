/*
 * Host test for display/components/core/app.c. Run "make test_app && ./test_app" in display/test.
 * redproof.py removes or weakens every rule once (mutations/app.py) and expects this test to fail.
 *
 * The app is driven exactly as the platform will drive it, by a harness that stands for the device:
 *   - a clock in steps of 20 ms; the time of the display starts at 0 with every start of the app
 *   - the task of the screen: a reading of the switch with every step, app_tick() every 200 ms,
 *     app_platform() every second
 *   - the task of the network: link_next() carried out against a WiFi of the test, poll_prepare() answered by
 *     an adapter of the test (tools/w906/API.md written once more, as in test_poll.c), app_net() where app.h
 *     asks for it
 *   - after every step app_take_events() carried out against a flash of the test, so that the app can be
 *     started again with what was stored
 * What the driver would see is app_scene() as the text of scene_dump(), compared with hand-written files
 * fixtures/app_<name>.txt, and app_backlight().
 *
 * Most stories start in drive(): the display has the network "Werkstatt" stored with the address of the
 * adapter, is bound to it, and the adapter runs since 100 s with the ignition on and the engine off (W906
 * profile, the values of tools/w906/fixtures/autopid_data_ignition_on.json). The first round is answered at
 * 0, the next ones every second. A read gets the number 42, the clear behind it 43.
 *
 * The web interface (app_web.h) does not exist yet: what it will do with the app is done here by the
 * functions browser_...() with the fields app_web.h names.
 *
 * Behind the stories come the random runs: the same harness driven by a random number generator in a child
 * process, with what app.h promises watched around every call (see there).
 *
 * The answers of the adapter that was measured on the vehicle on 2026-10-04 are not in the repository, they
 * name a private device; fixtures/app_real_*.json are hand-made texts of the same shape. Who has the real
 * ones plays the story with them as well:  WICAN_REAL_ADAPTER=<directory> ./test_app
 */
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "test.h"
#include "app.h"

#define COUNT(a)    ((int)(sizeof(a) / sizeof((a)[0])))
#define STEP_MS     20u
#define FILL        0xAA
#define GUARD       64

#define OWN         "a1b2c3d4e5f6"
#define OTHER       "0123456789ab"
#define OWN_AP      "WiCAN_a1b2c3d4e5f6"    // the access point of the adapter
#define BOOT        1234567890u
#define VERSION     "0.1.0"
#define GIT         "display-v0.1.0-3-g1a2b3c4"

#define PICKUP_MS   300u    // an accepted request waits this long for the AutoPID task
#define SCAN_MS     3000u   // from there to the end of the scan
#define PASS_MS     400u    // one polling pass
#define SCAN_STEPS  3u

#define ALL_STORES  (APP_EVENT_STORE_SETTINGS | APP_EVENT_STORE_WIFI | APP_EVENT_STORE_BOUND | APP_EVENT_STORE_LAYOUT | \
                     APP_EVENT_ERASE_LAYOUT | APP_EVENT_STORE_CATALOG | APP_EVENT_STORE_OLD)

/* ---------------------------------------------------------------------------------------------------
 * The device of the test
 */

// The app under test lies between bytes it is not told of
static struct
{
	unsigned char front[GUARD];
	app_t app;
	unsigned char behind[GUARD];
} box;
static app_t *const app = &box.app;
static json_token_t work[LAYOUT_TOKENS];

// What is kept over a restart (components/store/store.h)
typedef struct
{
	bool has_settings;
	char settings[SETTINGS_JSON_SIZE];
	bool has_wifi;
	net_profile_t profiles[NET_PROFILES_MAX];
	int profile_count;
	bool has_bound;
	char bound[33];
	bool has_layout;
	char layout[LAYOUT_TEXT_MAX + 1];
	bool has_layout_prev;
	char layout_prev[LAYOUT_TEXT_MAX + 1];
	bool has_catalog;
	char catalog[16384];
	bool has_old;
	char old[POLL_TEXT_SIZE];
} flash_t;

// What the boot loader and the memory that outlasts a restart know
typedef struct
{
	bool safe_mode;
	bool update_pending;
	bool previous_firmware;
	bool rolled_back;
	bool no_builtin;        // the built-in layout is damaged
} machine_t;

// What the platform was asked to carry out since the counters were cleared
typedef struct
{
	int settings, wifi, bound, layout, erase, catalog, old;
	int valid, reboot, reset, previous, install;
	int starts;
	uint32_t last;          // the events of the last step that had any
} done_t;

typedef struct
{
	char in_range[LINK_SEEN_MAX][NET_SSID_SIZE];    // what a scan finds
	int in_range_count;
	const char *gateway;    // of every network of the test
	const char *found;      // what a query for the service finds, NULL: nothing
	bool join_fails;
	bool ap_on;             // as last ordered
	bool joined;
	int profile;            // the profile the display is in
	int scans, joins, leaves, finds, aps_on, aps_off;
} wifi_t;

typedef struct
{
	char id[33];
	bool api;               // false: a firmware without the API, 404 for every path below /api/
	bool dead;              // no answer to anything
	uint32_t boot;
	uint64_t boot_ms;       // the time of the world at which the adapter started
	wican_autopid_t autopid;
	bool supported;
	bool ignition;
	int rpm;
	int32_t batt_mv;        // a multiple of 100, or -1
	int32_t sleep_in_s;
	uint32_t memory;        // trouble codes the vehicle has stored
	bool manual;            // a scan goes on only as the story says (adapter_step, adapter_done)
	int refuse;             // the status for the next POST, 0: by the rules
	const char *refuse_reason;
	bool swallow;           // the next POST arrives, but its answer gets lost
	bool lose;              // the next POST does not arrive
	bool no_result;         // the result the state names is not handed out: 204
	const char *config;     // the profile
	const char *values;     // the values while the ignition is on, NULL: those of the W906 with `rpm`
	const char *read_text;  // the result of a read as a story wants it, NULL: made from `memory`
	const char *clear_text;
	const char *fw;         // the version text of the state
	const char *mqtt;
	uint32_t pass_every_ms; // the pass counter moves this often while the adapter polls
	uint32_t scan_ms;       // from the pickup of a request to the end of its scan
	uint32_t steps;         // control units of a scan
	const char *const *names;   // of the steps, [0] is the engine check

	uint32_t pass;
	uint64_t pass_ms;       // when the counter moved last
	bool valid;             // a polling pass has ended since the ignition came on

	wican_dtc_phase_t phase;
	bool clear;
	bool http;
	uint32_t seq, next_seq;
	uint32_t step;
	uint64_t accepted_ms, ended_ms;
	char reason[32];
	uint32_t result_seq, result_count;
	bool result_clear;
	char result[POLL_TEXT_SIZE];
} adapter_t;

static flash_t flash;
static machine_t machine;
static done_t done;
static wifi_t wifi;
static adapter_t wican;
static app_platform_t platform;

static const char *firmware_version = VERSION;

static uint64_t now;            // the time of the display
static uint64_t epoch;          // the time of the world at which the display started
static bool switch_pressed, switch_ok;

// The request to the adapter that is under way, and when its answer arrives or is given up
static poll_request_t request;
static bool flying;
static uint64_t landing_ms;
static uint32_t latency_ms;     // of every answer
static int sent[POLL_DTC_CLEAR + 1];    // requests handed out, by kind
static int dropped;             // requests that were handed out while the address of the adapter was not known
static uint64_t clear_sent_ms;  // when the last clear was handed out
static char clear_path[POLL_PATH_SIZE]; // and its path

// Hand-written texts
static char builtin_text[LAYOUT_TEXT_MAX + 1];
static char w906_config[4096];
static char w906_values[2048];
static char other_config[512];
static char no_motor_config[4096];
static char other_values[256];
static char result_read[POLL_TEXT_SIZE];
static char result_clear[POLL_TEXT_SIZE];
static char stored_layout[2048];
static char stored_catalog[8192];
static char wanted[SCENE_DUMP_SIZE + 16];
static char what[512];

static char body_room[POLL_BODY_SIZE];
static char header_room[24];

static scene_t scene;
static char dumped[SCENE_DUMP_SIZE];

// The random runs watch what happens: before the reading of a step, behind it, behind a tick and at the end
// of the step; before and behind counts of the encoder, a tap and a swipe; with every request that is handed
// out and every answer that is applied; and with all events that are carried out
typedef enum
{
	TOUCH_COUNTS,       // a: the counts
	TOUCH_TAP,          // a: the row
	TOUCH_SWIPE,        // a: dx, b: dy
} touch_t;

static void (*watch_step)(void);
static void (*watch_button)(void);
static void (*watch_tick)(void);
static void (*watch_after)(void);
static void (*watch_touch)(touch_t kind, int a, int b);
static void (*watch_touched)(void);
static void (*on_request)(const poll_request_t *asked);
static void (*on_answer)(const poll_request_t *asked, int status, const char *body, bool waited);
static void (*watch_events)(uint32_t events);
// What a step adds to the time: the readings come every 20 ms, or less often while a story only waits
static uint64_t stride = STEP_MS;
// How far the times the two tasks read lie behind the time of the display: each reads it before it waits
// for the lock
static uint64_t net_skew, screen_skew;
// The latest time the harness gave the app
static uint64_t given_ms;

static uint64_t world(void)
{
	return epoch + now;
}

// A time that is passed to the app
static uint64_t given(uint64_t time_ms)
{
	if(time_ms > given_ms) given_ms = time_ms;
	return time_ms;
}

// The time the task of the screen read before it got the lock
static uint64_t screen_time(void)
{
	return now > screen_skew ? now - screen_skew : 0;
}

static bool all_bytes(const void *memory, size_t size, unsigned char byte)
{
	const unsigned char *bytes = memory;

	for(size_t i = 0; i < size; i++)
	{
		if(bytes[i] != byte) return false;
	}
	return true;
}

/* The adapter ------------------------------------------------------------------------------------------ */

// The control units of the stories, and the 18 of the W906 in the order of its profile
static const char *const UNITS[] = {"", "N3/28 Motorelektronik (CDID3)", "N2/14 Rückhaltesystem (SRS)", "N10 SAM"};
static const char *const UNITS_W906[] = {
	"", "N73 Elektronisches Zündschloss (EZS)", "N3/28 Motorelektronik (CDID3)", "Y3/8n4 Getriebesteuerung (NAG2)", "N15/5 Wählhebelmodul (EWM)", "N30/4 ESP",
	"N10 SAM", "N118/5 Kraftstoffpumpe (FSCU)", "N28/4 Anhängererkennung (AHE)", "N80 Mantelrohrmodul (MRM)", "N70 Dachbedieneinheit (DBE)",
	"N72/1 Oberes Bedienfeld (OBF)", "B162 Collision Prevention Assist", "N87/8 Radio", "A2/30 Navigationsmodul", "A1 Kombiinstrument", "S98 Klimaanlage",
	"N2/14 Rückhaltesystem (SRS)", "N69/1 Fahrertür (TSG)",
};

static void adapter_init(adapter_t *a)
{
	memset(a, 0, sizeof(*a));
	a->fw = "4.21";
	a->mqtt = "off";
	a->pass_every_ms = PASS_MS;
	a->scan_ms = SCAN_MS;
	a->steps = SCAN_STEPS;
	a->names = UNITS;
	strcpy(a->id, OWN);
	a->api = true;
	a->boot = BOOT;
	a->boot_ms = world() - 100000;
	a->autopid = WICAN_AUTOPID_RUN;
	a->supported = true;
	a->ignition = true;
	a->batt_mv = 12400;
	a->sleep_in_s = -1;
	a->memory = 3;
	a->config = w906_config;
	a->pass_ms = a->boot_ms;
	a->next_seq = 42;
}

// The adapter restarts: new boot number, the numbers of the requests begin anew, the result is gone
static void adapter_restart(adapter_t *a, uint32_t boot, uint32_t first_seq)
{
	a->boot = boot;
	a->boot_ms = world();
	a->pass = 0;
	a->pass_ms = world();
	a->valid = false;
	a->phase = WICAN_DTC_IDLE;
	a->clear = false;
	a->http = false;
	a->seq = 0;
	a->step = 0;
	a->next_seq = first_seq;
	a->reason[0] = '\0';
	a->result_seq = 0;
	a->result_count = 0;
	a->result_clear = false;
}

static void adapter_end(adapter_t *a, wican_dtc_phase_t phase, const char *reason, uint64_t at_ms)
{
	a->phase = phase;
	strcpy(a->reason, reason);
	a->ended_ms = at_ms;
}

static void adapter_result(adapter_t *a)
{
	const char *text = a->clear ? a->clear_text : a->read_text;
	int length;

	// One code is back at once after a clear: its cause is still there
	if(a->clear && a->memory > 1) a->memory = 1;
	a->result_seq = a->seq;
	a->result_clear = a->clear;
	a->result_count = a->memory;
	if(text != NULL)
	{
		strcpy(a->result, text);
		return;
	}
	length = snprintf(a->result, sizeof(a->result), "{\"state\":\"done\",\"action\":\"%s\",\"duration_ms\":%u,\"dtc_count\":%u,\"ecus\":[{\"name\":\"N10 SAM\","
	                  "\"id\":\"662\",\"protocol\":\"KWP\",%s\"status\":\"ok\",\"dtcs\":[", a->clear ? "clear" : "read", (unsigned)(30000 + a->seq % 10000),
	                  (unsigned)a->memory, a->clear ? "\"cleared\":true," : "");
	for(uint32_t i = 0; i < a->memory; i++)
	{
		length += snprintf(a->result + length, sizeof(a->result) - (size_t)length, "%s{\"code\":\"930%u\",\"status\":\"60\"}", i > 0 ? "," : "", (unsigned)(i + 1));
	}
	snprintf(a->result + length, sizeof(a->result) - (size_t)length, "]}]}");
}

// The story lets the scan go on: the control unit `step` is being asked (0: the engine check)
static void adapter_step(uint32_t step)
{
	wican.phase = WICAN_DTC_RUNNING;
	wican.step = step;
}

static void adapter_done(void)
{
	wican.step = wican.steps;
	adapter_end(&wican, WICAN_DTC_DONE, "", world());
	adapter_result(&wican);
}

static void adapter_failed(const char *reason)
{
	wican.step = 0;
	adapter_end(&wican, WICAN_DTC_ERROR, reason, world());
}

// What happened in the adapter until now
static void adapter_catch_up(adapter_t *a, uint64_t at_ms)
{
	uint64_t picked = a->accepted_ms + PICKUP_MS;

	if(!a->manual)
	{
		if(a->phase == WICAN_DTC_QUEUED && at_ms >= picked)
		{
			if(!a->supported) adapter_end(a, WICAN_DTC_ERROR, "not_supported", picked);
			else a->phase = WICAN_DTC_RUNNING;
		}
		if(a->phase == WICAN_DTC_RUNNING)
		{
			if(!a->ignition) adapter_end(a, WICAN_DTC_ERROR, "ecu_offline", at_ms);
			else if(a->clear && a->rpm >= 50) adapter_end(a, WICAN_DTC_ERROR, "engine_running", at_ms);
			else if(at_ms >= picked + a->scan_ms)
			{
				a->step = a->steps;
				adapter_end(a, WICAN_DTC_DONE, "", picked + a->scan_ms);
				adapter_result(a);
			}
			else
			{
				// Step 0 is the engine check
				a->step = (uint32_t)((at_ms - picked) * (a->steps + 1) / a->scan_ms);
			}
		}
	}

	// A scan pauses the polling, and so does the ignition
	if(a->autopid != WICAN_AUTOPID_RUN || !a->ignition)
	{
		a->valid = false;
		a->pass_ms = at_ms;
	}
	else if(a->phase == WICAN_DTC_RUNNING || at_ms < a->pass_ms)
	{
		a->pass_ms = at_ms;
	}
	else if(at_ms - a->pass_ms >= a->pass_every_ms)
	{
		uint64_t passes = (at_ms - a->pass_ms) / a->pass_every_ms;

		a->pass += (uint32_t)passes;
		a->pass_ms += passes * a->pass_every_ms;
		a->valid = true;
	}
}

// A request for a scan with the rules of main/dtc_state.h. Returns the status of API.md.
static int adapter_request(adapter_t *a, bool clear, uint32_t seq, uint64_t at_ms, uint32_t *number, const char **reason)
{
	*number = a->seq;
	if(a->autopid != WICAN_AUTOPID_RUN || a->sleep_in_s == 0)
	{
		*number = 0;
		*reason = "not_ready";
		return 503;
	}
	*reason = NULL;
	if(a->phase == WICAN_DTC_QUEUED || a->phase == WICAN_DTC_RUNNING) *reason = "busy";
	else if(clear)
	{
		if(a->phase != WICAN_DTC_DONE || a->clear || at_ms - a->ended_ms > 600000) *reason = "read_required";
		else if(seq != a->seq) *reason = "stale_seq";
		else if(a->result_count == 0) *reason = "nothing_to_clear";
	}
	if(*reason != NULL) return 409;

	a->seq = a->next_seq;
	a->next_seq = a->next_seq >= 0x7FFFFFFFu ? 1 : a->next_seq + 1;
	a->phase = WICAN_DTC_QUEUED;
	a->clear = clear;
	a->http = true;
	a->step = 0;
	a->reason[0] = '\0';
	a->accepted_ms = at_ms;
	*number = a->seq;
	return 202;
}

// The state as the text of API.md
static size_t adapter_state(const adapter_t *a, uint64_t at_ms, char *out, size_t size)
{
	static const char *const autopid[] = {"off", "starting", "run"};
	static const char *const phases[] = {"idle", "queued", "running", "done", "error"};
	bool running = a->phase == WICAN_DTC_RUNNING;
	bool ended = a->phase == WICAN_DTC_DONE || a->phase == WICAN_DTC_ERROR;
	char volts[24];

	if(a->batt_mv < 0) strcpy(volts, "-1");
	else snprintf(volts, sizeof(volts), "%d.%d", (int)(a->batt_mv / 1000), (int)(a->batt_mv % 1000 / 100));
	return (size_t)snprintf(out, size, "{\"api\":1,\"id\":\"%s\",\"fw\":\"%s\",\"git\":\"w906-v1.4.0-9-g0123abc\",\"boot\":%" PRIu32 ",\"up\":%" PRIu32 ","
	                        "\"autopid\":\"%s\",\"pids\":%d,\"ecu\":\"%s\",\"pass\":%" PRIu32 ",\"rx_age_ms\":%d,\"mqtt\":\"%s\",\"batt_v\":%s,"
	                        "\"sleep_in_s\":%" PRId32 ",\"heap\":61000,\"heap_min\":48000,\"dtc\":{\"supported\":%s,\"state\":\"%s\","
	                        "\"action\":\"%s\",\"src\":\"%s\",\"seq\":%" PRIu32 ",\"ecu\":%" PRIu32 ",\"total\":%" PRIu32 ",\"name\":\"%s\",\"reason\":\"%s\","
	                        "\"age_s\":%" PRIu32 ",\"count\":%" PRIu32 ",\"result_seq\":%" PRIu32 "}}",
	                        a->id, a->fw, a->boot, (uint32_t)((at_ms - a->boot_ms) / 1000), autopid[a->autopid], a->autopid == WICAN_AUTOPID_OFF ? 0 : 35,
	                        a->ignition && a->autopid == WICAN_AUTOPID_RUN ? "online" : "offline", a->pass, a->valid ? 140 : -1, a->mqtt, volts, a->sleep_in_s,
	                        a->supported && a->autopid != WICAN_AUTOPID_OFF ? "true" : "false", phases[a->phase],
	                        a->phase == WICAN_DTC_IDLE ? "" : a->clear ? "clear" : "read", a->phase == WICAN_DTC_IDLE ? "" : a->http ? "http" : "mqtt",
	                        a->seq, running || ended ? a->step : 0, running || ended ? a->steps : 0, running ? a->names[a->step] : "", a->reason,
	                        ended ? (uint32_t)((at_ms - a->ended_ms) / 1000) : 0, a->result_count, a->result_seq);
}

// The values of the W906 fixture with the engine speed of the adapter
static void adapter_values(const adapter_t *a, char *out, size_t size)
{
	static const char head[] = "{\"ENGINE_RPM\":0,";

	snprintf(out, size, "{\"ENGINE_RPM\":%d,%s", a->rpm, w906_values + strlen(head));
}

// The answer of the adapter to one request, by method and path as the display wrote them. Returns the status,
// 0 if there is no answer.
static int adapter_answer(adapter_t *a, const poll_request_t *asked, uint64_t at_ms, const char **body, size_t *length, const char **seq_header)
{
	int status = 404;

	adapter_catch_up(a, at_ms);
	strcpy(body_room, "Nothing matches the given URI");
	*body = body_room;
	*seq_header = NULL;

	if(a->dead)
	{
		status = 0;
		body_room[0] = '\0';
	}
	else if(strncmp(asked->path, "/api/", 5) == 0 && !a->api)
	{
		// The firmware does not know the path
	}
	else if(!asked->post && strcmp(asked->path, "/api/state") == 0)
	{
		status = 200;
		adapter_state(a, at_ms, body_room, sizeof(body_room));
	}
	else if(asked->post && strncmp(asked->path, "/api/dtc?", 9) == 0)
	{
		const char *query = asked->path + 9;
		const char *reason = "bad_request";
		uint32_t number = 0;
		bool clear = strncmp(query, "action=clear&seq=", 17) == 0;

		status = 400;
		if(a->lose)
		{
			a->lose = false;
			status = 0;
			reason = "";
		}
		else if(a->refuse != 0)
		{
			status = a->refuse;
			reason = a->refuse_reason;
			number = a->seq;
			a->refuse = 0;
		}
		else if(strcmp(query, "action=read") == 0) status = adapter_request(a, false, 0, at_ms, &number, &reason);
		else if(clear) status = adapter_request(a, true, (uint32_t)strtoul(query + 17, NULL, 10), at_ms, &number, &reason);

		if(reason == NULL) snprintf(body_room, sizeof(body_room), "{\"accepted\":true,\"seq\":%" PRIu32 "}", number);
		else snprintf(body_room, sizeof(body_room), "{\"accepted\":false,\"reason\":\"%s\",\"seq\":%" PRIu32 "}", reason, number);
		if(a->swallow)
		{
			a->swallow = false;
			status = 0;
		}
		if(status == 0) body_room[0] = '\0';
	}
	else if(!asked->post && strcmp(asked->path, "/api/dtc/result") == 0)
	{
		if(a->autopid != WICAN_AUTOPID_RUN)
		{
			status = 503;
			strcpy(body_room, "{\"accepted\":false,\"reason\":\"not_ready\",\"seq\":0}");
		}
		else if(a->result_seq == 0 || a->no_result)
		{
			status = 204;
			body_room[0] = '\0';
		}
		else
		{
			status = 200;
			strcpy(body_room, a->result);
			snprintf(header_room, sizeof(header_room), "%" PRIu32, a->result_seq);
			*seq_header = header_room;
		}
	}
	else if(!asked->post && strcmp(asked->path, "/autopid_data") == 0)
	{
		status = 200;
		if(a->autopid == WICAN_AUTOPID_OFF) strcpy(body_room, "{\"error\":\"No data available\"}");
		else if(!a->valid) strcpy(body_room, "{}");
		else if(a->values != NULL) strcpy(body_room, a->values);
		else adapter_values(a, body_room, sizeof(body_room));
	}
	else if(!asked->post && strcmp(asked->path, "/load_car_config") == 0)
	{
		if(a->autopid == WICAN_AUTOPID_OFF)
		{
			status = 500;
			strcpy(body_room, "Failed to generate JSON");
		}
		else
		{
			status = 200;
			strcpy(body_room, a->config);
		}
	}
	*length = strlen(body_room);
	return status;
}

/* The flash and the platform -------------------------------------------------------------------------- */

// Carries out what the app asks for, storing first
static void carry_out(void)
{
	uint32_t events = app_take_events(app);
	int length;

	if(events == 0) return;
	done.last = events;

	if(events & APP_EVENT_STORE_SETTINGS)
	{
		length = settings_to_json(&app->settings, flash.settings, sizeof(flash.settings));
		flash.has_settings = length > 0;
		done.settings++;
	}
	if(events & APP_EVENT_STORE_WIFI)
	{
		memcpy(flash.profiles, app->profiles, sizeof(flash.profiles));
		flash.profile_count = app->profile_count;
		flash.has_wifi = true;
		done.wifi++;
	}
	if(events & APP_EVENT_STORE_BOUND)
	{
		strcpy(flash.bound, app->poll.bound_id);
		flash.has_bound = true;
		done.bound++;
	}
	if(events & APP_EVENT_STORE_LAYOUT)
	{
		if(flash.has_layout) strcpy(flash.layout_prev, flash.layout);
		flash.has_layout_prev = flash.has_layout;
		memcpy(flash.layout, app->layout_text, app->layout_length);
		flash.layout[app->layout_length] = '\0';
		flash.has_layout = true;
		done.layout++;
	}
	if(events & APP_EVENT_ERASE_LAYOUT)
	{
		flash.has_layout = false;
		done.erase++;
	}
	if(events & APP_EVENT_STORE_CATALOG)
	{
		length = catalog_to_json(&app->poll.catalog, flash.catalog, sizeof(flash.catalog));
		flash.has_catalog = length > 0;
		done.catalog++;
	}
	if(events & APP_EVENT_STORE_OLD)
	{
		strcpy(flash.old, app->poll.old_text);
		flash.has_old = true;
		done.old++;
	}
	if(events & APP_EVENT_MARK_VALID)
	{
		machine.update_pending = false;
		done.valid++;
	}
	if(events & APP_EVENT_REBOOT) done.reboot++;
	if(events & APP_EVENT_FACTORY_RESET) done.reset++;
	if(events & APP_EVENT_PREVIOUS_FIRMWARE) done.previous++;
	if(events & APP_EVENT_INSTALL_FIRMWARE) done.install++;
	if(watch_events != NULL) watch_events(events);
}

// What the platform knows about itself right now
static void tell_platform(void)
{
	static char ssid[NET_SSID_SIZE];

	// The name of the network as the driver knows it, not as the list holds it by now
	strcpy(ssid, wifi.joined ? app->profiles[wifi.profile].ssid : "");
	platform.ssid = ssid;
	platform.ip = wifi.joined ? "192.168.1.77" : "";
	platform.rssi = wifi.joined ? -61 : 0;
	app_platform(app, &platform);
}

// The app starts with what the flash holds. The time of the display begins again, at `at_ms`.
static void start_at(uint64_t at_ms)
{
	app_boot_t boot;

	epoch = world();
	now = at_ms;
	memset(&boot, 0, sizeof(boot));
	boot.version = firmware_version;
	boot.git = GIT;
	boot.safe_mode = machine.safe_mode;
	boot.update_pending = machine.update_pending;
	boot.previous_firmware = machine.previous_firmware;
	boot.rolled_back = machine.rolled_back;
	if(flash.has_settings) boot.settings_json = flash.settings;
	if(flash.has_wifi) boot.profiles = flash.profiles;
	boot.profile_count = flash.profile_count;
	if(flash.has_bound) boot.bound_id = flash.bound;
	if(flash.has_layout)
	{
		boot.layout_text = flash.layout;
		boot.layout_length = strlen(flash.layout);
	}
	if(!machine.no_builtin)
	{
		boot.builtin_layout = builtin_text;
		boot.builtin_length = strlen(builtin_text);
	}
	if(flash.has_catalog)
	{
		boot.catalog_json = flash.catalog;
		boot.catalog_length = strlen(flash.catalog);
	}
	if(flash.has_old)
	{
		boot.old_text = flash.old;
		boot.old_length = strlen(flash.old);
	}
	boot.work = work;
	boot.work_count = LAYOUT_TOKENS;

	// Whatever stood in the memory before
	memset(&box, FILL, sizeof(box));
	app_init(app, &boot, now);
	given_ms = now;

	wifi.joined = false;
	wifi.ap_on = false;
	wifi.profile = -1;
	flying = false;
	switch_pressed = false;
	switch_ok = true;
	done.starts++;
	tell_platform();
}

static void start(void)
{
	start_at(0);
}

// A restart the app asked for, as the platform and the boot loader carry it out
static void restart_as_asked(void)
{
	uint32_t events = done.last;

	if(events & APP_EVENT_FACTORY_RESET)
	{
		// STORE_CFG: settings, networks, binding. The views, the catalogue and the old list stay.
		flash.has_settings = false;
		flash.has_wifi = false;
		flash.profile_count = 0;
		memset(flash.profiles, 0, sizeof(flash.profiles));
		flash.has_bound = false;
	}
	if(events & APP_EVENT_INSTALL_FIRMWARE)
	{
		// The version that ran until now is the one in the other slot
		machine.update_pending = true;
		machine.previous_firmware = true;
	}
	else if(machine.update_pending)
	{
		// A restart before the update was confirmed: the boot loader takes it back
		machine.update_pending = false;
		machine.rolled_back = true;
	}
	done.last = 0;
	start();
}

/* The task that talks to the network ------------------------------------------------------------------ */

// The time the task of the network read before it got the lock
static uint64_t net_time(void)
{
	return now > net_skew ? now - net_skew : 0;
}

static void link_step(void)
{
	uint64_t at = net_time();

	for(int actions = 0; actions < 8; actions++)
	{
		link_do_t action = link_next(&app->link, at);

		switch(action)
		{
			case LINK_DO_SCAN:
				wifi.scans++;
				link_scanned(&app->link, wifi.in_range, wifi.in_range_count, at);
				break;
			case LINK_DO_JOIN:
				wifi.joins++;
				if(wifi.join_fails)
				{
					link_join_failed(&app->link, at);
				}
				else
				{
					wifi.joined = true;
					wifi.profile = link_profile(&app->link);
					link_joined(&app->link, wifi.gateway, at);
				}
				break;
			case LINK_DO_LEAVE:
				wifi.leaves++;
				wifi.joined = false;
				link_left(&app->link, at);
				break;
			case LINK_DO_FIND:
				wifi.finds++;
				if(wifi.found != NULL) link_found(&app->link, wifi.found, at);
				else link_not_found(&app->link, at);
				break;
			case LINK_DO_AP_ON:
				wifi.aps_on++;
				wifi.ap_on = true;
				break;
			case LINK_DO_AP_OFF:
				wifi.aps_off++;
				wifi.ap_on = false;
				break;
			default:
				return;
		}
		app_net(app, given(at));
	}
}

static void land(void)
{
	const char *body, *seq_header;
	size_t length;
	int status = adapter_answer(&wican, &request, world(), &body, &length, &seq_header);
	// An answer that comes after the network was lost is not waited for any more
	bool waited = app->poll.asking;

	flying = false;
	poll_apply(&app->poll, &request, status, body, length, seq_header, net_time(), work, POLL_TOKENS);
	app_net(app, given(net_time()));
	if(on_answer != NULL) on_answer(&request, status, body, waited);
}

static void poll_step(void)
{
	for(int requests = 0; requests < 8; requests++)
	{
		if(flying)
		{
			if(now < landing_ms) return;
			land();
		}
		// Storing comes before the next request is sent
		carry_out();
		if(!poll_prepare(&app->poll, net_time(), &request)) return;
		app_net(app, given(net_time()));

		if(request.kind <= POLL_DTC_CLEAR) sent[request.kind]++;
		if(request.kind == POLL_DTC_CLEAR)
		{
			clear_sent_ms = now;
			strcpy(clear_path, request.path);
		}
		if(on_request != NULL) on_request(&request);
		// A request for an adapter whose address is not known cannot be sent
		if(app_host(app)[0] == '\0')
		{
			dropped++;
			continue;
		}
		flying = true;
		landing_ms = now + (wican.dead ? POLL_TIMEOUT_MS : latency_ms);
	}
}

// The network is gone
static void lose_wifi(void)
{
	wifi.joined = false;
	wifi.profile = -1;
	link_lost(&app->link, now);
	app_net(app, given(now));
}

/* The clock and the task of the screen ---------------------------------------------------------------- */

static void step(void)
{
	if(watch_step != NULL) watch_step();
	app_button(app, switch_pressed, switch_ok, given(screen_time()));
	if(watch_button != NULL) watch_button();
	if(now % 200 == 0)
	{
		app_tick(app, given(screen_time()));
		if(watch_tick != NULL) watch_tick();
	}
	if(now % 1000 == 0) tell_platform();
	link_step();
	poll_step();
	carry_out();
	now += stride;
	if(watch_after != NULL) watch_after();
}

static void run(uint64_t ms)
{
	for(uint64_t passed = 0; passed < ms; passed += stride) step();
}

// Runs until the time of the display is `at_ms`
static void run_to(uint64_t at_ms)
{
	while(now < at_ms) step();
}

// The knob pressed for `ms`, then released for 60 ms: the knob reports a short press 40 ms after the release
static void press(uint64_t ms)
{
	switch_pressed = true;
	run(ms);
	switch_pressed = false;
	run(60);
}

static void short_press(void)
{
	press(100);
}

static void long_press(void)
{
	press(900);
}

static void count(int counts)
{
	if(watch_touch != NULL) watch_touch(TOUCH_COUNTS, counts, 0);
	app_encoder(app, counts, given(screen_time()));
	carry_out();
	if(watch_touched != NULL) watch_touched();
}

static void turn(int detents)
{
	count(detents * KNOB_COUNTS_PER_DETENT);
}

static void tap(int row)
{
	if(watch_touch != NULL) watch_touch(TOUCH_TAP, row, 0);
	app_tap(app, row, given(screen_time()));
	carry_out();
	if(watch_touched != NULL) watch_touched();
}

static void swipe(int dx, int dy)
{
	if(watch_touch != NULL) watch_touch(TOUCH_SWIPE, dx, dy);
	app_swipe(app, dx, dy, given(screen_time()));
	carry_out();
	if(watch_touched != NULL) watch_touched();
}

/* The web interface as app_web.h describes it ----------------------------------------------------------- */

static void set_text(char *field, size_t size, const char *text)
{
	memset(field, 0, size);
	strncpy(field, text, size - 1);
}

// POST /api/wifi. password NULL: the member is missing. Returns the ticket, 0 if refused.
static uint32_t browser_wifi(const char *ssid, const char *password, const char *host)
{
	if(access_may_ask(&app->access, ACCESS_ASK_WIFI, now) != ACCESS_ALLOWED) return 0;

	memset(&app->wifi_asked, 0, sizeof(app->wifi_asked));
	set_text(app->wifi_asked.ssid, sizeof(app->wifi_asked.ssid), ssid);
	if(password != NULL) set_text(app->wifi_asked.password, sizeof(app->wifi_asked.password), password);
	app->wifi_asked.has_password = password != NULL;
	set_text(app->wifi_asked.host, sizeof(app->wifi_asked.host), host);
	app->has_wifi_asked = true;
	set_text(app->ask_detail, sizeof(app->ask_detail), ssid);
	return access_ask(&app->access, ACCESS_ASK_WIFI, now);
}

// POST /api/reset
static uint32_t browser_reset(void)
{
	if(app_busy(app) || access_may_ask(&app->access, ACCESS_ASK_RESET, now) != ACCESS_ALLOWED) return 0;
	return access_ask(&app->access, ACCESS_ASK_RESET, now);
}

// POST /api/ota: the first bytes arrived
static bool browser_upload_begin(const char *version)
{
	if(!access_is_open(&app->access, now) || app_busy(app)) return false;

	app->uploading = true;
	app->upload_percent = 0;
	app->upload_ms = now;
	app->previous_firmware = false;
	machine.previous_firmware = false;
	set_text(app->upload_version, sizeof(app->upload_version), version);
	return true;
}

static void browser_upload_progress(int percent)
{
	app->upload_percent = percent;
	app->upload_ms = now;
}

// The last byte arrived. Returns the ticket of the question, 0 if it cannot be asked.
static uint32_t browser_upload_end(void)
{
	app->uploading = false;
	if(access_may_ask(&app->access, ACCESS_ASK_FIRMWARE, now) != ACCESS_ALLOWED) return 0;

	set_text(app->ask_detail, sizeof(app->ask_detail), app->upload_version);
	return access_ask(&app->access, ACCESS_ASK_FIRMWARE, now);
}

// PUT /api/layout?mode=apply (stored false) or mode=save
static bool browser_layout(const char *text, bool stored)
{
	size_t length = strlen(text);

	if(!access_write(&app->access, now)) return false;
	if(!layout_parse(text, length, &app->checked, NULL, app->work, app->work_count)) return false;

	app->layout = app->checked;
	memcpy(app->layout_text, text, length + 1);
	app->layout_length = length;
	app->source = stored ? APP_LAYOUT_STORED : APP_LAYOUT_PREVIEW;
	app->nav.page = -1;
	if(stored)
	{
		app->stored = app->checked;
		app->has_stored = true;
		app->events |= APP_EVENT_STORE_LAYOUT;
	}
	return true;
}

// POST /api/layout/reset
static bool browser_layout_reset(void)
{
	if(!access_write(&app->access, now)) return false;

	app->has_stored = false;
	app_choose_layout(app);
	app->events |= APP_EVENT_ERASE_LAYOUT;
	return true;
}

// POST /api/wifi/forget
static bool browser_forget(const char *ssid)
{
	int left;

	if(!access_write(&app->access, now)) return false;

	left = net_forget(app->profiles, app->profile_count, ssid);
	if(left == app->profile_count) return false;

	app->profile_count = left;
	link_profiles(&app->link, app->profiles, app->profile_count, now);
	app_net(app, given(now));
	app->events |= APP_EVENT_STORE_WIFI;
	return true;
}

// POST /api/settings
static bool browser_settings(const char *json)
{
	if(!access_write(&app->access, now) || !settings_from_json(&app->settings, json, strlen(json), NULL, 0, app->work, app->work_count)) return false;

	knob_set_reverse(&app->knob, app->settings.reverse);
	app->events |= APP_EVENT_STORE_SETTINGS;
	return true;
}

// POST /api/reboot
static bool browser_reboot(void)
{
	if(!access_write(&app->access, now) || app_busy(app)) return false;

	app_do(app, NAV_DO_REBOOT, given(now));
	return true;
}

/* What the driver sees --------------------------------------------------------------------------------- */

// The screen right now as the text of scene_dump()
static const char *screen(void)
{
	app_scene(app, &scene, now);
	if(scene_dump(&scene, dumped, sizeof(dumped)) < 0) strcpy(dumped, "no dump\n");
	return dumped;
}

static int light(void)
{
	return app_backlight(app, now);
}

// The screen is the text of fixtures/app_<name>.txt, byte for byte
static bool screen_is(const char *name)
{
	char path[160];

	snprintf(path, sizeof(path), "fixtures/app_%s.txt", name);
	if(!read_fixture(path, wanted, sizeof(wanted) - 1)) return false;
	// Every line of a dump ends with a line break; read_fixture() took the last one away
	strcat(wanted, "\n");

	if(strcmp(screen(), wanted) == 0) return true;
	printf("  %s wants\n%s  the screen is\n%s", path, wanted, dumped);
	return false;
}

static void shows(const char *name, const char *rule)
{
	snprintf(what, sizeof(what), "app_%s.txt: %s", name, rule);
	check(screen_is(name), what);
}

// A line of the screen as the dump writes it, e.g. "row: > action | Lesen |  | enabled"
static bool has_line(const char *line)
{
	const char *found = strstr(screen(), line);
	size_t length = strlen(line);

	while(found != NULL)
	{
		if((found == dumped || found[-1] == '\n') && found[length] == '\n') return true;
		found = strstr(found + 1, line);
	}
	return false;
}

static bool on(nav_screen_t expected)
{
	return app->nav.screen == expected;
}

static conn_view_t view(void)
{
	return conn_view(&app->poll.conn, now);
}

static dtc_flow_phase_t phase(void)
{
	return app->poll.flow.phase;
}

/* The scenes -------------------------------------------------------------------------------------------- */

static void load_fixtures(void)
{
	bool loaded = read_fixture("../layouts/w906_default.json", builtin_text, sizeof(builtin_text)) &&
	              read_fixture("../../tools/w906/fixtures/car_config_w906.json", w906_config, sizeof(w906_config)) &&
	              read_fixture("../../tools/w906/fixtures/autopid_data_ignition_on.json", w906_values, sizeof(w906_values)) &&
	              read_fixture("fixtures/app_config_other.json", other_config, sizeof(other_config)) &&
	              read_fixture("fixtures/app_values_other.json", other_values, sizeof(other_values)) &&
	              read_fixture("fixtures/app_config_no_motor.json", no_motor_config, sizeof(no_motor_config)) &&
	              read_fixture("fixtures/app_result_read.json", result_read, sizeof(result_read)) &&
	              read_fixture("fixtures/app_result_clear.json", result_clear, sizeof(result_clear)) &&
	              read_fixture("fixtures/app_layout_stored.json", stored_layout, sizeof(stored_layout)) &&
	              read_fixture("fixtures/app_catalog_stored.json", stored_catalog, sizeof(stored_catalog));

	check(loaded && strncmp(w906_values, "{\"ENGINE_RPM\":0,", 16) == 0, "the fixtures of the stories are there, and the values of the W906 begin with the engine speed");
}

// A device fresh from the factory in a world without any network: nothing stored, nothing in range
static void factory(void)
{
	memset(&flash, 0, sizeof(flash));
	memset(&machine, 0, sizeof(machine));
	memset(&done, 0, sizeof(done));
	memset(&wifi, 0, sizeof(wifi));
	memset(sent, 0, sizeof(sent));
	memset(&platform, 0, sizeof(platform));
	platform.ap_ssid = "WiCAN-Display";
	platform.ap_password = "geheim1234";
	platform.slot = "ota_0";
	platform.reset = "poweron";
	platform.heap = 182340;
	platform.heap_min = 151200;
	platform.psram = 7340032;
	platform.psram_min = 7100416;
	platform.reconnects = 2;
	wifi.gateway = "192.168.1.1";
	wifi.profile = -1;
	dropped = 0;
	clear_sent_ms = 0;
	clear_path[0] = '\0';
	latency_ms = 0;
	firmware_version = VERSION;
	watch_step = NULL;
	watch_button = NULL;
	watch_tick = NULL;
	watch_after = NULL;
	watch_touch = NULL;
	watch_touched = NULL;
	on_request = NULL;
	on_answer = NULL;
	watch_events = NULL;
	stride = STEP_MS;
	net_skew = 0;
	screen_skew = 0;
	// The world is older than the display: the adapter runs since 100 s
	epoch = 1000000;
	now = 0;
	adapter_init(&wican);
	wican.read_text = result_read;
	wican.clear_text = result_clear;
}

// The network "Werkstatt" is in range and stored with the address of the adapter, the display is bound to it
static void garage(void)
{
	factory();
	strcpy(wifi.in_range[0], "Werkstatt");
	wifi.in_range_count = 1;
	strcpy(flash.profiles[0].ssid, "Werkstatt");
	strcpy(flash.profiles[0].password, "geheim-123");
	strcpy(flash.profiles[0].host, "192.168.1.50");
	flash.profile_count = 1;
	flash.has_wifi = true;
	strcpy(flash.bound, OWN);
	flash.has_bound = true;
}

// ... started, and two seconds later: the adapter has answered three rounds, commands are allowed
static void drive(void)
{
	garage();
	start();
	run(2100);
	memset(&done, 0, sizeof(done));
	memset(sent, 0, sizeof(sent));
}

/* ---------------------------------------------------------------------------------------------------
 * Stories
 */

static void test_constants(void)
{
	check(APP_UPDATE_CONFIRM_MS == 300000 && APP_UPLOAD_IDLE_MS == 30000 && APP_INFO_LINES == 14 && APP_INFO_SIZE == 64 && APP_SWIPE_ROWS == 3,
	      "300 s to confirm an update, 30 s until a silent upload is over, 14 info lines of 64 bytes, a swipe moves 3 rows");
	check(APP_EVENT_STORE_SETTINGS == 0x1 && APP_EVENT_STORE_WIFI == 0x2 && APP_EVENT_STORE_BOUND == 0x4 && APP_EVENT_STORE_LAYOUT == 0x8 &&
	      APP_EVENT_ERASE_LAYOUT == 0x10 && APP_EVENT_STORE_CATALOG == 0x20 && APP_EVENT_STORE_OLD == 0x40 && APP_EVENT_MARK_VALID == 0x80 &&
	      APP_EVENT_REBOOT == 0x100 && APP_EVENT_FACTORY_RESET == 0x200 && APP_EVENT_PREVIOUS_FIRMWARE == 0x400 && APP_EVENT_INSTALL_FIRMWARE == 0x800,
	      "the events are twelve different bits");
	printf("  sizeof(app_t) is %lu: poll %lu, four layouts %lu, three lists of lines %lu, layout text %lu\n", (unsigned long)sizeof(app_t),
	       (unsigned long)sizeof(app->poll), (unsigned long)(4 * sizeof(layout_t)), (unsigned long)(3 * sizeof(app->list)), (unsigned long)sizeof(app->layout_text));
	check(sizeof(void *) != 8 || sizeof(app_t) == 262232, "app_t has the 262232 bytes app.h names (on a 64 bit host)");
}


static int stores(void)
{
	return done.settings + done.wifi + done.bound + done.layout + done.erase + done.catalog + done.old;
}

static int restarts(void)
{
	return done.reboot + done.reset + done.previous + done.install;
}

static int requests(void)
{
	int total = 0;

	for(int i = 0; i < COUNT(sent); i++) total += sent[i];
	return total;
}

static bool info_is(int index, const char *text)
{
	bool same = index < app->info_count && strcmp(app->info[index], text) == 0 && app->info_lines[index] == app->info[index];

	if(!same) printf("  info line %d is '%s', expected '%s'\n", index, index < APP_INFO_LINES ? app->info[index] : "", text);
	return same;
}

// The catalogue in the flash can be read, has `count` entries, among them `name` of the profile with this
// unit, and is the catalogue of the app
static bool flash_catalog_is(int count, const char *name, const char *unit)
{
	static catalog_t read_back;
	int index;

	if(!flash.has_catalog || !catalog_from_json(&read_back, flash.catalog, strlen(flash.catalog), work, CATALOG_TOKENS)) return false;

	index = catalog_find(&read_back, name);
	return read_back.count == count && index >= 0 && strcmp(read_back.entries[index].unit, unit) == 0 && read_back.entries[index].in_profile &&
	       catalog_checksum(&read_back) == catalog_checksum(&app->poll.catalog);
}

static void test_first_start(void)
{
	static const flash_t empty;
	access_ticket_t ticket;

	factory();
	start();
	check(all_bytes(box.front, sizeof(box.front), FILL) && all_bytes(box.behind, sizeof(box.behind), FILL), "the start writes no byte outside of its app_t");
	check(stores() + restarts() + done.valid == 0 && app->events == 0, "the start raises no event");
	run(100);
	shows("first_start", "first start without anything stored and without a network: the adapter was not found, the first of the seven built-in pages");
	check(app->source == APP_LAYOUT_BUILTIN && app->has_builtin && !app->has_stored && app->layout.page_count == 7 &&
	      strcmp(app->layout.name, "W906 OM651 Standard") == 0, "first start: the built-in layout is in use");
	check(app->layout_length == strlen(builtin_text) && strcmp(app->layout_text, builtin_text) == 0 && app->builtin_text == builtin_text,
	      "first start: layout_text is the text of the built-in layout, and the app keeps the text it was given");
	check(wifi.ap_on && wifi.aps_on == 1 && wifi.scans == 0 && app->link.phase == LINK_IDLE, "first start: without a stored network the own access point opens and nothing is scanned");
	check(memcmp(&flash, &empty, sizeof(flash)) == 0 && stores() + restarts() + done.valid == 0, "first start: nothing is stored and nothing is asked of the platform");
	check(requests() == 0 && app_host(app)[0] == '\0', "first start: no request without a network, and no address of an adapter");
	check(app->settings.brightness == 80 && app->settings.night == 25 && !app->settings.night_mode && !app->settings.reverse && app->settings.standby_s == 60 && light() == 80,
	      "first start: the settings are the defaults, the backlight is at 80");
	check(app->poll.bound_id[0] == '\0' && app->profile_count == 0 && app->poll.catalog.count == 1 && !app->poll.has_old && app->old_lines == 0,
	      "first start: not bound, no network, the catalogue of catalog_init(), no old list");
	check(on(NAV_PAGES) && app->nav.page == 0 && !hold_is_stuck(&app->hold) && !access_is_open(&app->access, now) && !knob_is_pressed(&app->knob),
	      "first start: the first value page, the release closed, the knob released");
	check(app->brightness_preview == -1 && !app->uploading && !app->update_pending && app->heat == GUARD_HEAT_NORMAL && !app->has_temp && !app_busy(app),
	      "first start: no brightness being set, no upload, no update to confirm, no heat, not busy");
	check(app->upload_percent == 0 && app->upload_ms == 0 && all_bytes(app->upload_version, sizeof(app->upload_version), 0) && all_bytes(app->ask_detail, sizeof(app->ask_detail), 0) &&
	      all_bytes(&app->wifi_asked, sizeof(app->wifi_asked), 0) && all_bytes(app->seen, sizeof(app->seen), 0) && app->seen_count == 0 && all_bytes(&app->checked, sizeof(app->checked), 0) &&
	      app->temp_c == 0 && app->list_lines == 0 && app->cleared_lines == 0,
	      "first start: nothing of what stood in the memory is left in what the web interface, the temperature and the lists will use");

	short_press();
	turn(3);
	shows("first_menu", "first start: a short press opens the menu, three detents lead to Web-Zugriff");
	short_press();
	shows("first_web", "first start: the web screen names the own access point and its password, and no address");
	short_press();
	check(access_is_open(&app->access, now) && now == 580, "first start: a short press on Freigabe gives the release");

	// Somebody enters the network of the workshop in the browser
	strcpy(wifi.in_range[0], "Werkstatt");
	wifi.in_range_count = 1;
	wifi.found = "192.168.1.50";
	check(browser_wifi("Werkstatt", "geheim-123", "") == 1, "first start: the browser asks to store a network and gets the ticket 1");
	shows("first_ask", "first start: the question lies over the web screen with the SSID and 60 seconds");
	short_press();
	ticket = access_ticket(&app->access, 1, now);
	check(ticket == ACCESS_TICKET_WAITING && app->has_wifi_asked && strcmp(app->wifi_asked.password, "geheim-123") == 0 && strcmp(app->ask_detail, "Werkstatt") == 0 &&
	      done.wifi == 0 && app->profile_count == 0, "first start: a press 120 ms after the question is not its answer - it waits on with what it asks for");
	run_to(2100);
	check(wifi.scans == 0 && requests() == 0, "first start: nothing is scanned before the network is confirmed");
	short_press();
	check(access_ticket(&app->access, 1, now) == ACCESS_TICKET_CONFIRMED && done.wifi == 1 && stores() == 2,
	      "first start: a press 1640 ms after the question confirms it - the networks are stored, and the binding that follows at once");
	check(flash.has_wifi && flash.profile_count == 1 && strcmp(flash.profiles[0].ssid, "Werkstatt") == 0 && strcmp(flash.profiles[0].password, "geheim-123") == 0 &&
	      flash.profiles[0].host[0] == '\0', "first start: the flash holds the network with its password and without a host");
	check(!app->has_wifi_asked && all_bytes(&app->wifi_asked, sizeof(app->wifi_asked), 0) && app->ask_detail[0] == '\0',
	      "first start: with the confirmation nothing of the request stays in the memory, least of all the password");
	check(wifi.scans == 1 && wifi.joins == 1 && wifi.finds == 1 && link_up(&app->link) && strcmp(app_host(app), "192.168.1.50") == 0,
	      "first start: the display scans, joins the network and finds the adapter by its service");
	check(done.bound == 1 && flash.has_bound && strcmp(flash.bound, OWN) == 0 && strcmp(app->poll.bound_id, OWN) == 0,
	      "first start: the first adapter that answers is the one of this display, and its id is stored");
	check(view() == CONN_VIEW_LIVE && sent[POLL_STATE] == 1 && sent[POLL_CATALOG] == 1 && sent[POLL_VALUES] == 1 && app->poll.catalog.count == 36,
	      "first start: state, profile and values are asked for in the first round, the catalogue has the 35 values of the W906 and the battery");
	run_to(3100);
	shows("first_web_joined", "first start: in the network the web screen names the address of the display, 9:58 of the release are left");
	long_press();
	check(on(NAV_MENU) && app->nav.row == 3, "first start: a long press leads back to the menu");
	long_press();
	shows("page_motor", "first start: a long press leads back to the value page, which shows the values of the engine");
	check(app->source == APP_LAYOUT_BUILTIN, "first start: the built-in layout suits the W906 and stays");

	run_to(32220);
	check(done.catalog == 0 && !flash.has_catalog, "first start: 29 s after the profile arrived the catalogue is not stored yet");
	run_to(32240);
	check(done.catalog == 1 && done.last == APP_EVENT_STORE_CATALOG && flash_catalog_is(36, "ENGINE_RPM", "RPM"),
	      "first start: 30 s after the profile arrived the catalogue is stored once, as catalog_to_json() writes it");
	run_to(120000);
	check(done.catalog == 1 && done.wifi == 1 && done.bound == 1 && stores() == 3 && restarts() == 0, "first start: nothing else is stored in the two minutes that follow");
	check(light() == 80, "first start: with live values the backlight stays on, however long nobody touches the knob");

	run_to(600000);
	check(wifi.ap_on && wifi.aps_off == 0, "first start: 599.98 s after it opened the own access point is still open");
	run_to(600020);
	check(!wifi.ap_on && wifi.aps_off == 1, "first start: 600 s after it opened without a client the own access point closes, now that a network is stored");

	// The display is switched off and on again
	memset(&done, 0, sizeof(done));
	memset(&wifi, 0, sizeof(wifi));
	strcpy(wifi.in_range[0], "Werkstatt");
	wifi.in_range_count = 1;
	wifi.found = "192.168.1.50";
	wifi.gateway = "192.168.1.1";
	start();
	run(100);
	shows("page_motor", "first start, the next start: the display joins the stored network by itself and shows the values");
	check(wifi.aps_on == 0 && wifi.scans == 1 && wifi.joins == 1 && stores() == 0 && strcmp(app->poll.bound_id, OWN) == 0 && app->poll.catalog.count == 36,
	      "first start, the next start: no access point, the stored binding and the stored catalogue, nothing to store");
}

static void test_stored_start(void)
{
	garage();
	strcpy(flash.settings, "{\"brightness\":60,\"night\":15,\"night_mode\":true,\"reverse\":true,\"standby_s\":0}");
	flash.has_settings = true;
	strcpy(flash.layout, stored_layout);
	flash.has_layout = true;
	strcpy(flash.catalog, stored_catalog);
	flash.has_catalog = true;
	strcpy(flash.old, result_read);
	flash.has_old = true;
	start();
	shows("stored_start", "start with everything stored, before the network is there: the adapter was not found, the first of the two stored pages");
	check(app->source == APP_LAYOUT_STORED && app->has_stored && app->layout.page_count == 2 && strcmp(app->layout.name, "Meine Ansichten") == 0 &&
	      memcmp(&app->stored, &app->layout, sizeof(layout_t)) == 0, "stored start: the stored layout is in use and kept as the stored one");
	check(app->layout_length == strlen(stored_layout) && strcmp(app->layout_text, stored_layout) == 0, "stored start: layout_text is the stored text");
	check(app->settings.brightness == 60 && app->settings.night == 15 && app->settings.night_mode && app->settings.reverse && app->settings.standby_s == 0 &&
	      app->knob.reverse && light() == 15, "stored start: the stored settings are in use - the knob is reversed, the backlight is the one of the night");
	check(app->profile_count == 1 && strcmp(app->profiles[0].ssid, "Werkstatt") == 0 && app->link.profiles == app->profiles && app->link.profile_count == 1 &&
	      strcmp(app->poll.bound_id, OWN) == 0, "stored start: the stored network is the list of the link, the stored binding is in use");
	check(app->poll.catalog.count == 4 && catalog_find(&app->poll.catalog, "FUEL_L") == 3, "stored start: the stored catalogue is in use before the adapter is there");
	check(app->poll.has_old && app->old_lines == 7 && strcmp(app->old[0].text, "3 Fehler") == 0 && strcmp(app->old[6].text, "Rückhaltesystem") == 0 &&
	      strcmp(app->old[6].detail, "keine Antwort") == 0, "stored start: the lines of the stored old list are there from the start");
	check(stores() + restarts() + done.valid == 0 && app->events == 0, "stored start: the start raises no event, although the old list was read");

	run(100);
	shows("stored_page", "stored start: the display joins, the adapter is at the stored address, the first stored page shows its values");
	check(wifi.aps_on == 0 && wifi.scans == 1 && wifi.joins == 1 && wifi.finds == 0 && strcmp(app_host(app), "192.168.1.50") == 0,
	      "stored start: no access point, one scan, one join, no query for an adapter whose address is stored");
	check(done.bound == 0 && app->poll.catalog.count == 36 && app->source == APP_LAYOUT_STORED, "stored start: nothing to bind, the profile of the adapter replaces the stored catalogue, the stored layout stays");
	turn(1);
	shows("stored_page", "stored start: with the direction reversed a detent to the right is one to the left, the hard end of the first page");
	turn(-1);
	shows("stored_page2", "stored start: a detent to the left is the next page, with the bar of the tank at 720");
	turn(-1);
	shows("stored_page2", "stored start: the hard end of the last page");
	swipe(1, 0);
	shows("stored_page", "stored start: a swipe to the right is the page before");
	swipe(1, 0);
	shows("stored_page", "stored start: a swipe to the right on the first page is its hard end");
	swipe(-1, 0);
	shows("stored_page2", "stored start: a swipe to the left is the next page");
	swipe(0, -1);
	swipe(0, 1);
	shows("stored_page2", "stored start: a swipe up or down on a value page changes nothing");
	swipe(1, 0);
	swipe(0, -1);
	swipe(0, 1);
	shows("stored_page", "stored start: a swipe up or down on the first value page changes nothing either");
	swipe(-1, 0);
	long_press();
	shows("stored_page", "stored start: a long press on a value page leads to the first page");

	run_to(30000);
	check(done.catalog == 0, "stored start: 29.98 s after the profile arrived the catalogue is not stored yet");
	run_to(30020);
	check(done.catalog == 1 && stores() == 1 && flash_catalog_is(36, "FUEL_L", "L"), "stored start: 30 s after the profile arrived the catalogue, which differs from the stored one, is stored");
	check(strcmp(flash.layout, stored_layout) == 0 && strcmp(flash.old, result_read) == 0 && done.layout + done.erase + done.old == 0,
	      "stored start: layout and old list in the flash are left alone");
}

static void test_pages(void)
{
	drive();
	shows("page_motor", "the value pages: after the start the first page of the built-in layout");
	turn(1);
	shows("page_ladeluft", "the value pages: one detent is the next page");
	turn(5);
	shows("page_sonstiges", "the value pages: five detents further the last of the seven pages");
	turn(1);
	shows("page_sonstiges", "the value pages: the hard end of the last page");
	app_encoder(app, 3, now);
	shows("page_sonstiges", "the value pages: three counts back are no detent");
	app_encoder(app, -5, now);
	check(app->nav.page == 6, "the value pages: three counts forth and five back are no detent either");
	app_encoder(app, -2, now);
	check(app->nav.page == 5, "the value pages: two counts more make the detent back to the sixth page");
	turn(-9);
	shows("page_motor", "the value pages: nine detents back end on the first page");
}


#define SETTINGS_10     "{\"brightness\":10,\"night\":25,\"night_mode\":false,\"reverse\":false,\"standby_s\":60}"
#define SETTINGS_NIGHT  "{\"brightness\":10,\"night\":25,\"night_mode\":true,\"reverse\":false,\"standby_s\":60}"
#define SETTINGS_30     "{\"brightness\":10,\"night\":30,\"night_mode\":true,\"reverse\":false,\"standby_s\":60}"
#define SETTINGS_40     "{\"brightness\":10,\"night\":40,\"night_mode\":true,\"reverse\":false,\"standby_s\":60}"
#define SETTINGS_TURN   "{\"brightness\":80,\"night\":25,\"night_mode\":false,\"reverse\":true,\"standby_s\":60}"

static bool flash_settings_are(const char *text)
{
	bool same = flash.has_settings && strcmp(flash.settings, text) == 0;

	if(!same) printf("  the flash holds the settings '%s', expected '%s'\n", flash.settings, text);
	return same;
}

static void test_info(void)
{
	static char long_version[160];

	garage();
	start();
	check(app->info_count == 13 && info_is(0, "WLAN: –") && info_is(1, "Adresse: –") && info_is(2, "WiCAN: –") && info_is(3, "WiCAN-ID: " OWN) &&
	      info_is(4, "WiCAN-Firmware: –") && info_is(5, "Version: 0.1.0 (–)") && info_is(6, "Ansichten: W906 OM651 Standard (eingebaut)") &&
	      info_is(7, "Speicher: 0 frei, min. 0") && info_is(8, "PSRAM: 0 frei, min. 0") && info_is(9, "HTTP: 0 ok, 0 Fehler") && info_is(10, "Neuverbindungen: 0") &&
	      info_is(11, "Temperatur: –") && info_is(12, "Letzter Neustart: –"),
	      "the info lines are there from the start: thirteen lines, a dash for every text nobody knows yet");
	check(all_bytes(app->info[13], APP_INFO_SIZE, 0), "the info lines: a line that is not in use is empty");

	run(2100);
	short_press();
	turn(4);
	short_press();
	run_to(10000);
	check(app->info_count == 13 && info_is(0, "WLAN: Werkstatt (-61 dBm)") && info_is(1, "Adresse: 192.168.1.77") && info_is(2, "WiCAN: 192.168.1.50") &&
	      info_is(3, "WiCAN-ID: " OWN) && info_is(4, "WiCAN-Firmware: 4.21") && info_is(5, "Version: 0.1.0 (ota_0)") &&
	      info_is(6, "Ansichten: W906 OM651 Standard (eingebaut)") && info_is(7, "Speicher: 182340 frei, min. 151200") && info_is(8, "PSRAM: 7340032 frei, min. 7100416") &&
	      info_is(9, "HTTP: 21 ok, 0 Fehler") && info_is(10, "Neuverbindungen: 2") && info_is(11, "Temperatur: –") && info_is(12, "Letzter Neustart: poweron"),
	      "the info lines after ten seconds in the network: network, addresses, adapter, version, views, memory, the 21 answers of ten rounds, reconnects, reset reason");
	shows("info_top", "the info screen shows the first five lines");
	turn(6);
	shows("info_middle", "the info screen six detents further: the views in the middle");
	turn(6);
	shows("info_end", "the info screen at its hard end: the last five lines");
	short_press();
	check(on(NAV_MENU) && app->nav.row == 4, "the info screen is left by a short press");

	app_temperature(app, 47, true);
	app_tick(app, now);
	check(info_is(11, "Temperatur: 47 °C") && app->has_temp && app->temp_c == 47, "the info lines: a reading of the temperature is shown with the next tick");
	app_temperature(app, 99, false);
	app_tick(app, now);
	check(info_is(11, "Temperatur: –") && !app->has_temp && app->temp_c == 47, "the info lines: a reading that failed shows a dash; the last reading that succeeded is kept");
	app_temperature(app, -5, true);
	app_tick(app, now);
	check(info_is(11, "Temperatur: -5 °C") && app->has_temp && app->temp_c == -5, "the info lines: a temperature below zero");

	// The round of 11000 gets no answer until 15000, the one that follows at 16000 none until 20000
	wican.dead = true;
	run_to(20400);
	check(info_is(9, "HTTP: 23 ok, 2 Fehler") && app->poll.http_ok == 23 && app->poll.http_failed == 2,
	      "the info lines: two requests without an answer are two failures behind the 23 answers of eleven rounds");

	wifi.in_range_count = 0;
	lose_wifi();
	run_to(21400);
	check(info_is(0, "WLAN: –") && info_is(1, "Adresse: –") && info_is(2, "WiCAN: –") && info_is(3, "WiCAN-ID: " OWN) && info_is(4, "WiCAN-Firmware: –"),
	      "the info lines without a network: no SSID and no signal, no address, no adapter, no firmware of it - the binding stays");

	app->source = APP_LAYOUT_PREVIEW;
	app_tick(app, now);
	check(info_is(6, "Ansichten: W906 OM651 Standard (Vorschau)"), "the info lines: a layout the browser sent to look at is a Vorschau");
	app->source = (app_layout_source_t)4;
	app_tick(app, now);
	check(info_is(6, "Ansichten: W906 OM651 Standard (–)"), "the info lines: a source that is none has no word");
	app->source = (app_layout_source_t)-1;
	app_tick(app, now);
	check(info_is(6, "Ansichten: W906 OM651 Standard (–)"), "the info lines: a source below the first has no word either");

	garage();
	strcpy(flash.layout, stored_layout);
	flash.has_layout = true;
	machine.rolled_back = true;
	start();
	check(app->info_count == 14 && info_is(0, "Update nicht übernommen – vorherige Version aktiv") && info_is(1, "WLAN: –") && info_is(4, "WiCAN-ID: " OWN) &&
	      info_is(7, "Ansichten: Meine Ansichten (gespeichert)") && info_is(13, "Letzter Neustart: –"),
	      "the info lines after an update that was taken back: fourteen lines, the first says so; a stored layout is named gespeichert");

	garage();
	wican.config = other_config;
	wican.values = other_values;
	start();
	run(300);
	check(app->info_count == 13 && info_is(6, "Ansichten: – (erzeugt)"), "the info lines: views made from the catalogue have no name and are named erzeugt");

	// 9 bytes of "Version: " leave room for 54: 27 characters of two bytes each
	garage();
	long_version[0] = '\0';
	for(int i = 0; i < 60; i++) strcat(long_version, "ä");
	firmware_version = long_version;
	start();
	check(strlen(app->info[5]) == 63 && strncmp(app->info[5], "Version: ää", 13) == 0 && strcmp(app->info[5] + 9, long_version + 66) == 0 && info_is(6, "Ansichten: W906 OM651 Standard (eingebaut)"),
	      "the info lines: a line that is too long ends with its field, 63 bytes, and the next line is not touched");
	// One byte more in front: the 27th character has no room, and half of it is not written
	garage();
	strcpy(long_version, "x");
	for(int i = 0; i < 60; i++) strcat(long_version, "ä");
	firmware_version = long_version;
	start();
	check(strlen(app->info[5]) == 62 && strncmp(app->info[5], "Version: x", 10) == 0 && strcmp(app->info[5] + 10, long_version + 69) == 0,
	      "the info lines: a line is cut at a character boundary - 26 characters in 62 bytes, and nothing follows the cut, not even the blank that would fit");
	// The version ends one byte before the room does
	garage();
	strcpy(long_version, "x");
	for(int i = 0; i < 26; i++) strcat(long_version, "ä");
	firmware_version = long_version;
	start();
	check(strlen(app->info[5]) == 63 && strncmp(app->info[5] + 10, long_version + 1, 52) == 0 && app->info[5][62] == ' ',
	      "the info lines: a version that leaves one byte free is followed by the blank behind it - the line is the beginning of its text, 63 bytes of it");
	// ... and with one byte less both fit, and the slot behind them is cut
	garage();
	strcpy(long_version, "");
	for(int i = 0; i < 26; i++) strcat(long_version, "ä");
	firmware_version = long_version;
	start();
	app_tick(app, now);
	check(strlen(app->info[5]) == 63 && strcmp(app->info[5] + 61, " (") == 0, "the info lines: a version that leaves two bytes free is followed by the two bytes that fit, the slot is left out");
	// 51 bytes of version leave one byte behind the parenthesis: the dash for the slot nobody told yet has three
	garage();
	memset(long_version, 'x', 51);
	long_version[51] = '\0';
	firmware_version = long_version;
	start();
	check(strlen(app->info[5]) == 62 && strcmp(app->info[5] + 60, " (") == 0, "the info lines: of a character that does not fit not a single byte is written, also when it is the first of its text");
	firmware_version = NULL;
	start();
	check(info_is(5, "Version: – (–)"), "the info lines: a firmware without a version text gets a dash");
	firmware_version = VERSION;
}

static void test_platform(void)
{
	char text[96];
	app_platform_t given;
	bool copied = true;
	bool ended = true;

	garage();
	start();
	memset(&given, 0, sizeof(given));
	for(size_t length = 0; length < sizeof(text); length++)
	{
		static const size_t sizes[] = {NET_SSID_SIZE, 40, NET_SSID_SIZE, NET_PASSWORD_SIZE, 16, 24};
		const char *fields[] = {app->ssid, app->ip, app->ap_ssid, app->ap_password, app->slot, app->reset};

		memset(text, 'a', length);
		text[length] = '\0';
		given.ssid = given.ip = given.ap_ssid = given.ap_password = given.slot = given.reset = text;
		given.rssi = -61;
		app_platform(app, &given);
		for(int i = 0; i < COUNT(sizes); i++)
		{
			size_t kept = length < sizes[i] ? length : sizes[i] - 1;

			if(strlen(fields[i]) != kept) ended = false;
			else if(strncmp(fields[i], text, kept) != 0) copied = false;
		}
	}
	check(sizeof(app->ssid) == NET_SSID_SIZE && sizeof(app->ip) == 40 && sizeof(app->ap_ssid) == NET_SSID_SIZE && sizeof(app->ap_password) == NET_PASSWORD_SIZE &&
	      sizeof(app->slot) == 16 && sizeof(app->reset) == 24, "the texts of the platform have fields of 33, 40, 33, 65, 16 and 24 bytes");
	check(ended, "the texts of the platform: with every length from 0 to 95 bytes each of the six texts ends within its field");
	check(copied, "the texts of the platform: with every length each field holds as much of its text as it has room for");

	given.slot = "ääääääääää";
	given.reset = "xääääääääääääääää";
	app_platform(app, &given);
	check(strcmp(app->slot, "äääääää") == 0 && strcmp(app->reset, "xäääääääääää") == 0, "the texts of the platform are cut at a character boundary: 7 of 10 characters in 15 bytes, 1 and 11 in 23");

	given.ssid = "Werkstatt";
	given.ip = "10.0.0.7";
	given.rssi = -71;
	given.ap_ssid = "Hotspot";
	given.ap_password = "geheim";
	given.slot = "ota_1";
	given.reset = "panic";
	given.heap = 1;
	given.heap_min = 2;
	given.psram = 3;
	given.psram_min = 4;
	given.reconnects = 5;
	app_platform(app, &given);
	check(strcmp(app->ssid, "Werkstatt") == 0 && strcmp(app->ip, "10.0.0.7") == 0 && app->rssi == -71 && strcmp(app->ap_ssid, "Hotspot") == 0 &&
	      strcmp(app->ap_password, "geheim") == 0 && strcmp(app->slot, "ota_1") == 0 && strcmp(app->reset, "panic") == 0 && app->heap == 1 && app->heap_min == 2 &&
	      app->psram == 3 && app->psram_min == 4 && app->reconnects == 5, "what the platform tells is taken over, each text and each number into its own field");
	app_tick(app, now);
	check(info_is(0, "WLAN: Werkstatt (-71 dBm)") && info_is(1, "Adresse: 10.0.0.7") && info_is(5, "Version: 0.1.0 (ota_1)") && info_is(7, "Speicher: 1 frei, min. 2") &&
	      info_is(8, "PSRAM: 3 frei, min. 4") && info_is(10, "Neuverbindungen: 5") && info_is(12, "Letzter Neustart: panic"), "what the platform tells is in the info lines with the next tick");
	given.ip = "";
	app_platform(app, &given);
	app_tick(app, now);
	check(info_is(0, "WLAN: Werkstatt (-71 dBm)") && info_is(1, "Adresse: –"), "the info lines: in a network without an address yet the signal is told");
	given.ssid = "";
	given.ip = "10.0.0.7";
	app_platform(app, &given);
	app_tick(app, now);
	check(info_is(0, "WLAN: –") && info_is(1, "Adresse: 10.0.0.7"), "the info lines: without the name of a network no signal is told, whatever the address is");
	given.ssid = "Werkstatt";
	given.heap = given.heap_min = given.psram = given.psram_min = given.reconnects = UINT32_MAX;
	given.rssi = INT_MIN;
	app_platform(app, &given);
	app_tick(app, now);
	check(info_is(0, "WLAN: Werkstatt (-2147483648 dBm)") && info_is(7, "Speicher: 4294967295 frei, min. 4294967295") && info_is(8, "PSRAM: 4294967295 frei, min. 4294967295") &&
	      info_is(10, "Neuverbindungen: 4294967295"), "the info lines hold the largest numbers");

	memset(&given, 0, sizeof(given));
	app_platform(app, &given);
	check(app->ssid[0] == '\0' && app->ip[0] == '\0' && app->ap_ssid[0] == '\0' && app->ap_password[0] == '\0' && app->slot[0] == '\0' && app->reset[0] == '\0' &&
	      app->rssi == 0 && app->heap == 0 && app->reconnects == 0, "a text of the platform that is NULL counts as empty");

	// The name of a network may be any bytes: 40 bytes that all continue a character which never began
	memset(text, 0x80, 40);
	text[40] = '\0';
	given.ssid = given.slot = text;
	app_platform(app, &given);
	app_tick(app, now);
	check(strlen(app->ssid) < sizeof(app->ssid) && strlen(app->slot) < sizeof(app->slot) && strlen(app->info[0]) < APP_INFO_SIZE && strlen(app->info[5]) < APP_INFO_SIZE,
	      "a text of the platform that is no UTF-8 at all ends within its field, and so do the info lines made of it");
}

static void test_brightness(void)
{
	drive();
	short_press();
	shows("menu", "a short press on a value page opens the menu");
	turn(1);
	short_press();
	shows("brightness", "the brightness screen opens with the brightness in use");
	check(light() == 80 && app->brightness_preview == -1, "the brightness screen: before the knob is turned nothing is being set");
	turn(-2);
	shows("brightness_70", "the brightness screen: two detents back are ten percent less");
	check(light() == 70 && app->brightness_preview == 70 && app->settings.brightness == 80 && stores() == 0,
	      "the brightness being set is on the backlight at once and is not stored");
	turn(-20);
	check(light() == 5 && app->brightness_preview == 5, "the brightness screen: the lower limit of 5 percent");
	turn(1);
	check(light() == 10 && app->brightness_preview == 10, "the brightness screen: one detent up from the limit");
	short_press();
	check(on(NAV_MENU) && app->nav.row == 1 && app->settings.brightness == 10 && app->settings.night == 25 && app->brightness_preview == -1 && light() == 10,
	      "a short press leaves the brightness screen: the value is the brightness of the day");
	check(done.settings == 1 && stores() == 1 && flash_settings_are(SETTINGS_10), "leaving the brightness screen stores the settings once");

	turn(1);
	short_press();
	shows("menu_night", "a short press on Nachtmodus switches it on: the menu says so and names the brightness of the night");
	check(app->settings.night_mode && done.settings == 2 && flash_settings_are(SETTINGS_NIGHT) && light() == 25, "the night mode is stored and on the backlight at once");
	turn(-1);
	short_press();
	turn(1);
	shows("brightness_night", "the brightness screen in night mode sets the brightness of the night");
	check(light() == 30 && app->settings.night == 25, "the brightness of the night being set is on the backlight and not stored");
	long_press();
	check(on(NAV_MENU) && app->settings.night == 30 && app->settings.brightness == 10 && app->brightness_preview == -1 && done.settings == 3 && flash_settings_are(SETTINGS_30),
	      "a long press leaves the brightness screen as well: in night mode the value is the brightness of the night, the one of the day stays");

	short_press();
	turn(2);
	check(light() == 40 && done.settings == 3, "the brightness screen once more: 40 percent being set");
	run(119900);
	check(on(NAV_BRIGHTNESS) && done.settings == 3, "the brightness screen 119.9 s after the last detent: still open");
	run(400);
	check(on(NAV_PAGES) && app->settings.night == 40 && app->brightness_preview == -1 && done.settings == 4 && flash_settings_are(SETTINGS_40),
	      "after 120 s without input the brightness screen is left for the value pages and what was set is stored");

	start();
	check(app->settings.night_mode && app->settings.night == 40 && app->settings.brightness == 10 && light() == 40,
	      "after a restart the stored brightness and the night mode are in use");
	run(2100);
	short_press();
	turn(2);
	short_press();
	check(!app->settings.night_mode && light() == 10 && flash_settings_are("{\"brightness\":10,\"night\":40,\"night_mode\":false,\"reverse\":false,\"standby_s\":60}"),
	      "a second short press on Nachtmodus switches it off again");
}

static void test_settings(void)
{
	drive();
	short_press();
	turn(5);
	short_press();
	shows("settings", "the settings screen: direction normal, hotspot off, no previous version to start");
	short_press();
	check(app->settings.reverse && app->knob.reverse && done.settings == 1 && stores() == 1 && flash_settings_are(SETTINGS_TURN),
	      "a short press on Drehrichtung reverses the knob and stores the settings");
	turn(-1);
	check(app->nav.row == 1, "with the direction reversed a detent to the left moves the focus down");
	short_press();
	shows("settings_changed", "the settings screen: direction reversed, the hotspot switched on");
	check(wifi.ap_on && wifi.aps_on == 1 && link_ap_on(&app->link) && stores() == 1, "a short press on Hotspot opens the own access point; nothing is stored for it");
	short_press();
	check(!wifi.ap_on && wifi.aps_off == 1 && !link_ap_on(&app->link), "a second short press on Hotspot closes the own access point");

	turn(-1);
	short_press();
	shows("confirm_reboot", "a short press on Neustart asks first, with the focus on Abbrechen");
	short_press();
	check(on(NAV_SETTINGS) && app->nav.row == 2 && restarts() == 0, "Neu starten, short press on Abbrechen: back to the settings, nothing happens");
	short_press();
	long_press();
	check(on(NAV_SETTINGS) && app->nav.row == 2 && restarts() == 0, "Neu starten, long press: back to the settings, nothing happens");
	short_press();
	turn(-1);
	check(restarts() == 0 && app->nav.row == 1, "Neu starten, the focus on Ausführen: nothing happens yet");
	short_press();
	check(done.reboot == 1 && restarts() == 1 && done.last == APP_EVENT_REBOOT && on(NAV_PAGES), "Neu starten, short press on Ausführen: the platform is asked to restart");
	restart_as_asked();
	check(app->settings.reverse && app->knob.reverse && done.starts == 1, "after the restart the stored direction of the knob is in use");
	run(2100);
	turn(-1);
	check(app->nav.page == 1, "after the restart a detent to the left is the next page");

	short_press();
	turn(-5);
	short_press();
	turn(-3);
	short_press();
	check(on(NAV_SETTINGS) && app->nav.row == 3 && has_line("row: > action | Vorherige Version |  | disabled"),
	      "without a firmware in the other slot a short press on Vorherige Version does nothing");

	garage();
	machine.previous_firmware = true;
	start();
	run(2100);
	short_press();
	turn(5);
	short_press();
	turn(3);
	check(has_line("row: > action | Vorherige Version |  | enabled"), "with a firmware in the other slot Vorherige Version is offered");
	short_press();
	turn(1);
	shows("confirm_previous", "a short press on Vorherige Version asks first; one detent puts the focus on Ausführen");
	check(restarts() == 0, "Vorherige Version starten, the focus on Ausführen: nothing happens yet");
	short_press();
	check(done.previous == 1 && restarts() == 1 && done.last == APP_EVENT_PREVIOUS_FIRMWARE && on(NAV_PAGES),
	      "Vorherige Version starten, short press on Ausführen: the platform is asked to boot the other slot");

	garage();
	strcpy(flash.settings, SETTINGS_TURN);
	flash.has_settings = true;
	strcpy(flash.layout, stored_layout);
	flash.has_layout = true;
	strcpy(flash.catalog, stored_catalog);
	flash.has_catalog = true;
	strcpy(flash.old, result_read);
	flash.has_old = true;
	start();
	run(2100);
	short_press();
	turn(-5);
	short_press();
	turn(-4);
	short_press();
	shows("confirm_reset", "a short press on Werkseinstellungen asks first and says what is erased");
	tap(0);
	check(on(NAV_SETTINGS) && app->nav.row == 4 && restarts() == 0, "Werkseinstellungen, a tap on Abbrechen: back to the settings, nothing happens");
	short_press();
	tap(1);
	check(done.reset == 1 && restarts() == 1 && done.last == APP_EVENT_FACTORY_RESET && on(NAV_PAGES), "Werkseinstellungen, a tap on Ausführen: the platform is asked for the factory reset");
	wifi.aps_on = 0;
	restart_as_asked();
	run(100);
	check(!app->settings.reverse && app->settings.brightness == 80 && app->profile_count == 0 && app->poll.bound_id[0] == '\0' && wifi.ap_on && wifi.aps_on == 1,
	      "after the factory reset: default settings, no network, not bound, the own access point open");
	check(app->source == APP_LAYOUT_STORED && strcmp(app->layout.name, "Meine Ansichten") == 0 && app->poll.catalog.count == 4 && app->old_lines == 7,
	      "after the factory reset the views, the catalogue and the old list are still there");
}


/* The fault memory ---------------------------------------------------------------------------------- */

static bool can_clear(void)
{
	nav_world_t seen;

	app_world(app, &seen, now);
	return seen.can_clear;
}

static bool can_read(void)
{
	nav_world_t seen;

	app_world(app, &seen, now);
	return seen.can_read;
}

// drive(), then the menu of the fault memory with the focus on "Lesen": it is 2420
static void scene_dtc(void)
{
	drive();
	short_press();
	short_press();
}

// ... the own read 42, asked for at 2540 and done in the adapter at 5840. Its list arrives at 6000 and is
// shown from 6200 on; a value newer than the list is there at 7000. It is 7100, clearing is offered.
static void scene_list(void)
{
	scene_dtc();
	short_press();
	run_to(7100);
}

// ... the clear dialog, opened at 7220, the focus on "Löschen" since 7260, the switch seen released since
// 7520. It is 7600.
static void scene_dialog(void)
{
	scene_list();
	turn(8);
	short_press();
	turn(1);
	run_to(7600);
}

// ... the knob held from 7600 on: the clear is sent at 10600 and gets the number 43. It is 10620, the knob
// is released.
static void scene_clearing(void)
{
	scene_dialog();
	switch_pressed = true;
	run_to(10620);
	switch_pressed = false;
}

static void test_read_and_clear(void)
{
	drive();
	wican.manual = true;
	short_press();
	short_press();
	shows("dtc_start", "the fault memory menu before anything was read: Lesen is offered, there is no list and no old list");
	check(can_read() && !can_clear() && !app_busy(app) && requests() == 0, "the fault memory menu: reading is allowed, clearing is not, and opening it sent nothing");
	short_press();
	check(sent[POLL_DTC_READ] == 1 && sent[POLL_DTC_CLEAR] == 0 && phase() == DTC_FLOW_READING && app->poll.flow.seq == 42 && on(NAV_DTC_BUSY) && app_busy(app),
	      "a short press on Lesen sends the read at once: the adapter accepts it as number 42, the display is busy");
	shows("busy_sent", "the progress screen right after the read was accepted: nothing known of it yet");
	run_to(3100);
	check(view() == CONN_VIEW_SCAN && has_line("big: …") && has_line("line: Auftrag gesendet") && has_line("ring: none"),
	      "the progress screen while the read waits in the adapter: still nothing to count, and no ring next to the arc of the screen");
	adapter_step(0);
	run_to(4100);
	shows("busy_engine", "the progress screen while the adapter checks the engine: step 0 of 3");
	adapter_step(2);
	run_to(5100);
	shows("busy_running", "the progress screen at the second of three control units: its short name, two thirds");
	long_press();
	shows("page_scan", "a long press leaves the progress screen for the value page: the values stand still, dimmed, and the ring shows the progress");
	check(app_busy(app) && phase() == DTC_FLOW_READING && light() == 80, "the read goes on while the value page is shown");
	short_press();
	short_press();
	check(on(NAV_DTC_BUSY), "while the read is under way a short press on Fehlerspeicher leads to its progress");
	adapter_done();
	run_to(7200);
	check(phase() == DTC_FLOW_LIST && on(NAV_DTC_BUSY) && sent[POLL_RESULT] == 1 && app->list_lines == 7, "the result arrived at 7000: until the next tick the progress screen stays");
	run_to(7300);
	shows("dtc_list_wait", "the list of the own read: three codes in two control units, one that did not answer; clearing is not offered before a newer engine speed is there");
	check(app->poll.has_list && app->summary.codes == 3 && app->summary.ecus_with_codes == 2 && app->summary.ecus_not_ok == 1 && !app_busy(app) && !can_clear(),
	      "the list of the own read is held by the poll, its summary by the app, and the display is not busy any more");
	check(strcmp(app->list[5].text, "9301") == 0 && strcmp(app->list[5].detail, "Status 60") == 0 && app->list[5].kind == DTC_LINE_CODE && app->list[6].kind == DTC_LINE_PROBLEM,
	      "the lines of the list are those of dtc_view_list()");
	run_to(8100);
	shows("dtc_list", "the list one round later: clearing is offered, for 9:59");
	turn(8);
	shows("dtc_list_end", "the end of the list: its last two lines, Erneut lesen, Fehler löschen in focus, Zurück");
	short_press();
	shows("clear_dialog", "a short press on Fehler löschen opens the dialog: what is to be cleared, the warning, the focus on Abbrechen, the ring empty");
	check(on(NAV_DTC_CONFIRM) && app->hold.open && app_busy(app) && stores() == 0, "the clear dialog is open, the display counts as busy, nothing is stored yet");
	turn(1);
	run_to(8600);
	switch_pressed = true;
	run_to(10100);
	shows("clear_hold", "the clear dialog with the knob held for 1500 ms on Löschen: the ring is half full");
	run_to(11600);
	check(sent[POLL_DTC_CLEAR] == 0 && on(NAV_DTC_CONFIRM) && done.old == 0 && has_line("permille: 1000"),
	      "held for 2980 ms of readings: the ring is full at 3000 ms, but no reading has confirmed and nothing is sent");
	run_to(11620);
	check(sent[POLL_DTC_CLEAR] == 1 && clear_sent_ms == 11600 && strcmp(clear_path, "/api/dtc?action=clear&seq=42") == 0,
	      "the reading at 3000 ms confirms: the clear of the read 42 is sent, once");
	check(phase() == DTC_FLOW_CLEARING && app->poll.flow.seq == 43 && on(NAV_DTC_BUSY) && app_busy(app), "the adapter accepted the clear as number 43, the progress screen shows");
	check(done.old == 1 && stores() == 1 && done.last == APP_EVENT_STORE_OLD && flash.has_old && strcmp(flash.old, result_read) == 0 && app->old_lines == 7,
	      "the list before the clear is stored at the moment the adapter accepted, as the text the adapter sent");
	switch_pressed = false;
	run_to(11700);
	shows("busy_clear", "the progress screen of the clear right after it was accepted");
	adapter_step(1);
	run_to(12100);
	shows("busy_clear_running", "the progress screen of the clear at the first of three control units");
	adapter_done();
	run_to(13300);
	shows("cleared", "the outcome of the clear: two of three cleared, one control unit did not confirm, one code is left");
	check(phase() == DTC_FLOW_CLEARED && app->poll.has_cleared && !app->poll.has_list && app->list_lines == 0 && app->cleared_lines == 4 && sent[POLL_DTC_CLEAR] == 1 &&
	      done.old == 1 && stores() == 1, "the outcome is held, the list is not shown any more, the clear was sent once and the old list stored once");
	turn(4);
	short_press();
	shows("dtc_after_clear", "a short press on Fertig leads back: nothing read, the old list can be looked at");
	check(phase() == DTC_FLOW_IDLE && !app->poll.has_cleared && app->cleared_lines == 0 && app->poll.has_old, "after Fertig the outcome is gone, the old list stays");
	turn(2);
	short_press();
	shows("dtc_old", "Zuletzt gelöscht shows the list that was cleared");

	start();
	run(2100);
	short_press();
	short_press();
	turn(2);
	short_press();
	shows("dtc_old", "after a restart Zuletzt gelöscht shows the list from the flash");
	check(stores() == 1, "after the restart nothing more is stored");
}

static void test_list_and_dialog(void)
{
	scene_list();
	check(on(NAV_DTC_LIST) && can_clear() && phase() == DTC_FLOW_LIST && app->poll.flow.list_end_ms == 6000 && now == 7100,
	      "the scene of the list: the own read 42 ended at 6000, its list is shown, clearing is offered at 7100");
	long_press();
	turn(1);
	shows("dtc_listed", "the fault memory menu with a list: Liste ansehen is offered, the line names its codes");
	short_press();
	check(on(NAV_DTC_LIST) && app->nav.row == 0, "a short press on Liste ansehen shows the list again");

	turn(7);
	short_press();
	check(sent[POLL_DTC_READ] == 2 && on(NAV_DTC_BUSY) && !app->poll.has_list && app->list_lines == 0 && phase() == DTC_FLOW_READING,
	      "a short press on Erneut lesen sends a second read; the list is dropped with the press, not with the next round");

	scene_dialog();
	check(on(NAV_DTC_CONFIRM) && app->nav.row == 1 && app->hold.seen_released && !app->hold.holding && now == 7600,
	      "the scene of the dialog: open, the focus on Löschen, the switch seen released, not held, at 7600");
	tap(1);
	run(4000);
	check(sent[POLL_DTC_CLEAR] == 0 && on(NAV_DTC_CONFIRM), "a tap on Löschen clears nothing");
	short_press();
	run(4000);
	check(sent[POLL_DTC_CLEAR] == 0 && on(NAV_DTC_CONFIRM), "a short press on Löschen clears nothing");
	press(2980);
	run(4000);
	check(sent[POLL_DTC_CLEAR] == 0 && on(NAV_DTC_CONFIRM), "a press of 2980 ms on Löschen clears nothing");
	turn(-1);
	press(5000);
	check(sent[POLL_DTC_CLEAR] == 0 && on(NAV_DTC_CONFIRM) && app->nav.row == 0, "a press of 5000 ms on Abbrechen clears nothing and is no way back");
	short_press();
	check(on(NAV_DTC_LIST) && app->nav.row == 8 && !app->hold.open && sent[POLL_DTC_CLEAR] == 0, "a short press on Abbrechen leaves the dialog for the list, the focus on Fehler löschen");
	short_press();
	tap(0);
	check(on(NAV_DTC_LIST) && app->nav.row == 8 && !app->hold.open, "a tap on Abbrechen leaves the dialog as well");

	// The dialog opened at 7220: a press at 7300 was not preceded by 300 ms released
	scene_list();
	turn(8);
	short_press();
	turn(1);
	run_to(7300);
	switch_pressed = true;
	run_to(12000);
	check(sent[POLL_DTC_CLEAR] == 0 && on(NAV_DTC_CONFIRM) && has_line("permille: 0"), "a press that began 80 ms after the dialog opened is never a hold: 4700 ms of it clear nothing");

	scene_dialog();
	switch_pressed = true;
	run_to(9000);
	turn(1);
	run_to(10620);
	check(sent[POLL_DTC_CLEAR] == 0, "a detent during the hold breaks it, also one at the hard end: 3000 ms after the press nothing is sent");
	run_to(12000);
	check(sent[POLL_DTC_CLEAR] == 0, "the hold begins anew with the detent: 2980 ms after it nothing is sent");
	run_to(12020);
	check(sent[POLL_DTC_CLEAR] == 1 && clear_sent_ms == 12000, "3000 ms after the detent the clear is sent");

	scene_dialog();
	switch_pressed = true;
	run_to(9000);
	tap(1);
	run_to(12000);
	check(sent[POLL_DTC_CLEAR] == 0, "a tap during the hold breaks it: 2980 ms after the tap nothing is sent");
	run_to(12020);
	check(sent[POLL_DTC_CLEAR] == 1, "3000 ms after the tap the clear is sent");

	scene_dialog();
	switch_pressed = true;
	run_to(9000);
	swipe(-1, 0);
	run_to(12000);
	check(sent[POLL_DTC_CLEAR] == 0, "a swipe during the hold breaks it: 2980 ms after the swipe nothing is sent");
	run_to(12020);
	check(sent[POLL_DTC_CLEAR] == 1, "3000 ms after the swipe the clear is sent");

	scene_dialog();
	switch_pressed = true;
	run_to(9000);
	switch_ok = false;
	run_to(9020);
	switch_ok = true;
	run_to(12020);
	check(sent[POLL_DTC_CLEAR] == 0, "a reading that failed during the hold breaks it: 3000 ms after it nothing is sent");
	run_to(12040);
	check(sent[POLL_DTC_CLEAR] == 1 && clear_sent_ms == 12020, "3000 ms after the first reading that succeeded again the clear is sent");

	scene_dialog();
	turn(-1);
	switch_pressed = true;
	run_to(9000);
	turn(1);
	run_to(12000);
	check(sent[POLL_DTC_CLEAR] == 0, "a press that began on Abbrechen counts from the detent to Löschen: 2980 ms after it nothing is sent");
	run_to(12020);
	check(sent[POLL_DTC_CLEAR] == 1, "3000 ms on Löschen of a press that began on Abbrechen: the clear is sent");

	// The last input of the dialog was the detent at 7260
	scene_dialog();
	run_to(22260);
	check(on(NAV_DTC_CONFIRM) && app->hold.open, "the clear dialog 14980 ms after its last input: still open");
	run_to(22280);
	check(on(NAV_DTC_LIST) && app->nav.row == 8 && !app->hold.open && sent[POLL_DTC_CLEAR] == 0, "15000 ms without input the clear dialog closes by itself");

	// Detents keep the dialog open while the switch reads pressed from 7600 on: it hangs at 27600
	scene_dialog();
	turn(-1);
	switch_pressed = true;
	run_to(17600);
	turn(-1);
	run_to(27600);
	check(on(NAV_DTC_CONFIRM) && !hold_is_stuck(&app->hold), "the switch pressed for 19980 ms: the dialog is still open");
	run_to(27620);
	check(on(NAV_DTC_LIST) && app->nav.row == 8 && hold_is_stuck(&app->hold) && !app->hold.open && sent[POLL_DTC_CLEAR] == 0,
	      "the switch pressed for 20000 ms hangs: the dialog is left, nothing is sent");
	switch_pressed = false;
	run(200);
	shows("dtc_stuck", "after the switch hung clearing is not offered any more, and the list says why");
	short_press();
	check(on(NAV_DTC_LIST) && !app->hold.open, "after the switch hung a short press on Fehler löschen opens no dialog");

	// nav has opened the dialog, and the switch hangs since the world was filled
	scene_list();
	app->nav.screen = NAV_DTC_CONFIRM;
	app->nav.row = 0;
	app->hold.stuck = true;
	app_do(app, NAV_DO_HOLD_OPEN, now);
	check(on(NAV_DTC_LIST) && app->nav.row == 8 && !app->hold.open, "a dialog that hold_open() refuses is left again");
}


static void test_refusals(void)
{
	scene_dtc();
	wican.refuse = 409;
	wican.refuse_reason = "busy";
	short_press();
	run_to(2700);
	shows("failed_busy", "a read the adapter refuses because another scan runs: the failure with its reason in words");
	check(phase() == DTC_FLOW_FAILED && sent[POLL_DTC_READ] == 1 && !app_busy(app) && stores() == 0, "a refused read is sent once, stores nothing and leaves the display free");
	short_press();
	check(on(NAV_DTC) && phase() == DTC_FLOW_IDLE && has_line("line: Noch nicht gelesen"), "a short press acknowledges the failure");
	wican.refuse = 409;
	wican.refuse_reason = "busy";
	short_press();
	run(200);
	tap(0);
	check(on(NAV_DTC) && phase() == DTC_FLOW_IDLE && sent[POLL_DTC_READ] == 2, "a tap acknowledges the failure as well");

	drive();
	wican.sleep_in_s = 0;
	short_press();
	short_press();
	run_to(3100);
	short_press();
	run_to(3500);
	shows("failed_sleep", "a read the adapter refuses because it is about to sleep: its state tells that it is not starting");

	scene_dtc();
	wican.refuse = 503;
	wican.refuse_reason = "not_ready";
	short_press();
	run_to(2700);
	shows("failed_starting", "a read the adapter refuses as not ready while its state does not count down to sleep: it starts");

	// The POST gets no answer and did not arrive: the states of 3000 and 4000 show no request
	scene_dtc();
	wican.lose = true;
	short_press();
	run_to(3100);
	check(on(NAV_DTC_BUSY) && phase() == DTC_FLOW_READ_SENT && app_busy(app), "a read without an answer: after one state the display still waits");
	run_to(4300);
	shows("failed_no_answer", "a read without an answer that two states do not show did not arrive: it failed");
	check(sent[POLL_DTC_READ] == 1, "a read without an answer is never sent again by the display");

	scene_dtc();
	wican.manual = true;
	short_press();
	adapter_failed("can_bus_off");
	run_to(3300);
	shows("failed_word", "a read that ends with a reason the display has no words for: the reason itself");

	scene_dtc();
	short_press();
	adapter_restart(&wican, 777, 100);
	run_to(3300);
	shows("failed_restarted", "the adapter restarts during the own read: the result is lost");

	scene_dialog();
	wican.refuse = 409;
	wican.refuse_reason = "stale_seq";
	switch_pressed = true;
	run_to(10620);
	switch_pressed = false;
	run_to(10900);
	shows("failed_stale", "a clear the adapter refuses: the failure with its reason");
	check(sent[POLL_DTC_CLEAR] == 1 && done.old == 0 && stores() == 0 && !flash.has_old && !app->poll.has_old && app->old_lines == 0 && !app->poll.has_list && app->list_lines == 0,
	      "a refused clear has cleared nothing: no old list is kept or stored; the list is gone, the user reads again");

	// The POST of the clear gets no answer and did not arrive
	scene_dialog();
	wican.lose = true;
	switch_pressed = true;
	run_to(10620);
	switch_pressed = false;
	check(sent[POLL_DTC_CLEAR] == 1 && phase() == DTC_FLOW_CLEAR_SENT && done.old == 0 && app->poll.has_list, "a clear without an answer: nothing is stored while nobody knows");
	run_to(12300);
	check(on(NAV_DTC_LIST) && phase() == DTC_FLOW_LIST && app->poll.has_list && app->list_lines == 7 && done.old == 0 && !app->poll.has_old && can_clear(),
	      "a clear that two states do not show did not arrive: the list is shown again, nothing is stored, clearing is offered again");
	turn(8);
	short_press();
	turn(1);
	run(400);
	switch_pressed = true;
	run(3040);
	switch_pressed = false;
	check(sent[POLL_DTC_CLEAR] == 2 && phase() == DTC_FLOW_CLEARING && done.old == 1, "only a second confirmation at the knob sends the clear again; accepted, it stores the old list");

	// The POST of the clear arrives, but its answer gets lost
	scene_dialog();
	wican.swallow = true;
	switch_pressed = true;
	run_to(10620);
	switch_pressed = false;
	run_to(11000);
	check(wican.seq == 43 && phase() == DTC_FLOW_CLEAR_SENT && done.old == 0 && stores() == 0, "a clear whose answer got lost: the adapter runs it, the display stores nothing before it knows");
	run_to(11020);
	check(phase() == DTC_FLOW_CLEARING && done.old == 1 && strcmp(flash.old, result_read) == 0, "the state of 11000 shows the clear as accepted: the old list is stored with it");
	run_to(14300);
	check(on(NAV_DTC_CLEARED) && sent[POLL_DTC_CLEAR] == 1 && done.old == 1, "the clear whose answer got lost ends with its outcome; it was sent once and stored once");

	scene_clearing();
	check(phase() == DTC_FLOW_CLEARING && done.old == 1 && now == 10620, "the scene of the clear: accepted at 10600, the old list stored");
	adapter_restart(&wican, 777, 100);
	run_to(11300);
	shows("unknown", "the adapter restarts during the own clear: nobody knows what was cleared");
	check(phase() == DTC_FLOW_UNKNOWN && !app->poll.has_list && app->list_lines == 0 && app->old_lines == 7 && done.old == 1, "after the restart the list is gone, the old list stays");
	short_press();
	shows("dtc_restarted", "acknowledged: the adapter is up for less than 15 s and reading is not offered yet; the old list can be looked at");
	run_to(25900);
	check(!can_read(), "the adapter restarted at 10620: at 25900 its state of 25000 says 14 s, reading is not offered");
	run_to(26100);
	check(can_read(), "with the state of 26000 the adapter is up for 15 s: reading is offered");

	// A clear that still waits to be sent when the network goes
	scene_dialog();
	app_do(app, NAV_DO_CLEAR, now);
	check(phase() == DTC_FLOW_CLEAR_SENT, "a confirmed clear waits for the task of the network");
	lose_wifi();
	run(100);
	check(sent[POLL_DTC_CLEAR] == 0 && phase() == DTC_FLOW_LIST && done.old == 0 && app->poll.has_list, "a clear that was never sent when the network went has done nothing: the list stays, nothing is stored");

	scene_clearing();
	wifi.in_range_count = 0;
	lose_wifi();
	run(300);
	check(phase() == DTC_FLOW_UNKNOWN && on(NAV_DTC_FAILED) && has_line("title: Stand unbekannt") && done.old == 1, "the network goes during the own clear: its outcome is unknown");

	// The network goes while the request of the clear is under way: nobody knows whether it arrived. What the
	// poll makes of that is taken with the very call that tells it.
	scene_dialog();
	switch_pressed = true;
	run_to(10600);
	latency_ms = 500;
	run_to(10800);
	switch_pressed = false;
	check(sent[POLL_DTC_CLEAR] == 1 && phase() == DTC_FLOW_CLEAR_SENT && app->poll.asking && app->list_lines == 7 && app->old_lines == 0, "the scene of the clear whose answer takes 500 ms: sent at 10600, under way");
	link_lost(&app->link, now);
	app_net(app, now);
	check(phase() == DTC_FLOW_UNKNOWN && app->events == APP_EVENT_STORE_OLD && app->old_lines == 7 && app->list_lines == 0 && app->poll.events == 0,
	      "the call that tells the app of the lost network takes what the poll makes of it: the list is to be stored as the one before the clear, its lines are those of the old list at once");
}

static void test_blocks(void)
{
	drive();
	wican.rpm = 800;
	run_to(3100);
	short_press();
	short_press();
	shows("dtc_engine", "with the engine running reading is not offered, and the menu says why");
	short_press();
	check(sent[POLL_DTC_READ] == 0 && on(NAV_DTC), "with the engine running a short press on Lesen sends nothing");

	scene_list();
	wican.rpm = 800;
	run_to(8100);
	turn(8);
	check(!can_clear() && has_line("note: Motor läuft – nur bei Motor aus") && has_line("row: > action | Fehler löschen |  | disabled"),
	      "the engine started after the read: clearing is not offered any more");
	short_press();
	check(on(NAV_DTC_LIST) && !app->hold.open, "with the engine running a short press on Fehler löschen opens no dialog");

	// The engine starts while the dialog is open: the tick after the values of 8000 closes it
	scene_dialog();
	wican.rpm = 800;
	run_to(8200);
	check(on(NAV_DTC_CONFIRM) && app->hold.open && !can_clear(), "the engine starts while the dialog is open: until the next tick it stays");
	run_to(8220);
	check(on(NAV_DTC_LIST) && app->nav.row == 8 && !app->hold.open && sent[POLL_DTC_CLEAR] == 0, "the tick closes a dialog that may not clear any more, and the hold with it");

	// The read ended at 6000: its list may be cleared until 606000
	scene_list();
	run_to(604000);
	check(on(NAV_PAGES) && phase() == DTC_FLOW_LIST, "the list is kept while the display went back to the value pages");
	short_press();
	short_press();
	turn(1);
	short_press();
	turn(8);
	short_press();
	check(on(NAV_DTC_CONFIRM) && app->hold.open, "nine minutes and 58 seconds after the read the dialog still opens");
	run_to(606000);
	check(on(NAV_DTC_CONFIRM), "600 s after the read ended the dialog is still open");
	run_to(606220);
	check(on(NAV_DTC_LIST) && !app->hold.open, "more than 600 s after the read ended the tick closes the dialog");
	shows("dtc_list_old", "a list that is more than 600 s old cannot be cleared, and says so");

	// Somebody else starts a scan over MQTT while the dialog is open
	scene_dialog();
	wican.seq = 99;
	wican.http = false;
	wican.clear = false;
	wican.phase = WICAN_DTC_QUEUED;
	wican.manual = true;
	run_to(8300);
	shows("dtc_other_scan", "a scan of somebody else while the dialog is open: dialog and list are gone, reading is not offered while it waits, and the ring is its progress");
	check(!app->hold.open && !app->poll.has_list && app->list_lines == 0 && phase() == DTC_FLOW_IDLE && sent[POLL_DTC_CLEAR] == 0,
	      "a scan of somebody else closes the hold and drops the list");
}

static void test_adapter(void)
{
	drive();
	wican.ignition = false;
	run_to(3100);
	shows("ignition_off", "ignition off: the notice with the battery voltage, the ring grey");
	wican.batt_mv = -1;
	run_to(4100);
	check(has_line("line: Zündung aus – Motorsteuergerät offline") && !has_line("line: Bordnetz 12,4 V"), "ignition off without a measured voltage: the notice alone");

	drive();
	strcpy(wican.id, OTHER);
	run_to(3100);
	shows("foreign", "another adapter answers at the address: it is not the one of this display");
	check(sent[POLL_STATE] == 1 && sent[POLL_VALUES] == 0 && sent[POLL_CATALOG] == 0 && stores() == 0 && strcmp(app->poll.bound_id, OWN) == 0,
	      "a foreign adapter is asked for its state only, and the binding stays");
	short_press();
	short_press();
	shows("dtc_foreign", "a foreign adapter: reading is not offered");
	long_press();
	long_press();
	strcpy(wican.id, OWN);
	run_to(7100);
	shows("page_motor", "the own adapter is back: values again");

	garage();
	wican.api = false;
	start();
	run(100);
	shows("page_no_api", "a firmware without the API: values, a grey ring and the note");
	check(sent[POLL_STATE] == 1 && sent[POLL_CATALOG] == 1 && sent[POLL_VALUES] == 1 && !can_read(), "a firmware without the API is asked for profile and values; reading the fault memory is not offered");
	short_press();
	short_press();
	check(has_line("note: WiCAN-Firmware ohne Display-API") && has_line("row: > action | Lesen |  | disabled"), "a firmware without the API: the fault memory menu says why nothing is offered");

	drive();
	wican.autopid = WICAN_AUTOPID_OFF;
	run_to(3100);
	shows("autopid_off", "AutoPID off: the notice, the ring grey");
	wican.autopid = WICAN_AUTOPID_STARTING;
	run_to(4100);
	shows("starting", "AutoPID starting: the notice, the ring yellow");

	// The adapter restarts while the list of the own read is shown
	scene_list();
	adapter_restart(&wican, 777, 100);
	run_to(8300);
	check(on(NAV_DTC) && phase() == DTC_FLOW_IDLE && !app->poll.has_list && app->list_lines == 0 && has_line("line: Noch nicht gelesen") && has_line("note: WiCAN startet noch"),
	      "the adapter restarts while the list is shown: the list is gone and the display says that the adapter starts");
	check(app->poll.catalog.count == 36 && app->source == APP_LAYOUT_BUILTIN && view() == CONN_VIEW_LIVE && stores() == 0, "after the restart of the adapter its profile is loaded anew; the built-in layout stays");

	// The adapter goes to sleep and takes its access point with it
	garage();
	strcpy(wifi.in_range[0], "WiCAN_" OWN);
	strcpy(flash.profiles[0].ssid, "WiCAN_" OWN);
	flash.profiles[0].host[0] = '\0';
	wifi.gateway = "192.168.80.1";
	start();
	run(2100);
	check(view() == CONN_VIEW_LIVE && strcmp(app_host(app), "192.168.80.1") == 0 && wifi.finds == 0, "in the access point of the adapter the adapter is the gateway; nothing is queried");
	wifi.in_range_count = 0;
	wifi.scans = 0;
	lose_wifi();
	run(100);
	shows("first_start", "the adapter sleeps and its network is gone: the notice that it was not found");
	check(light() == 80 && app_backlight(app, 59999) == 80 && app_backlight(app, 60000) == 0, "the adapter sleeps: 60 s after the last input the backlight goes off");
	run_to(19200);
	check(wifi.scans == 4 && wifi.joins == 1, "the display looks for the network at once, then 2, 5 and 10 s after the scan before");
	run_to(49200);
	check(wifi.scans == 5, "the fifth scan follows 30 s after the fourth");
	adapter_restart(&wican, 4711, 7);
	wifi.in_range_count = 1;
	run_to(79100);
	check(view() == CONN_VIEW_NO_WIFI && app_backlight(app, now) == 0, "the adapter woke up at 49200: until the next scan the display is dark");
	run_to(80200);
	check(view() == CONN_VIEW_LIVE && wifi.scans == 6 && wifi.joins == 2 && app_backlight(app, now) == 80 && stores() == 0,
	      "the scan of 79100 finds the network again: the display joins, shows values and lights up by itself");
	shows("page_motor", "the adapter woke up: its values on the first page");
}

static void test_joining(void)
{
	// The adapter is found by its service, 20 s after the join
	garage();
	flash.profiles[0].host[0] = '\0';
	start();
	run(100);
	check(wifi.joins == 1 && wifi.finds == 1 && app->link.phase == LINK_JOINED && !link_up(&app->link) && view() == CONN_VIEW_NO_WIFI && requests() == 0,
	      "joined, but the adapter is not found in the network: the display asks nobody");
	shows("first_start", "joined without an adapter: the notice that it was not found");
	run_to(10100);
	check(wifi.finds == 2, "the service is queried again 10 s later");
	wifi.found = "192.168.1.50";
	run_to(20100);
	check(wifi.finds == 3 && link_up(&app->link) && strcmp(app_host(app), "192.168.1.50") == 0 && view() == CONN_VIEW_LIVE && dropped == 0,
	      "the third query finds the adapter: the display asks it at the address found");

	// The adapter does not answer at the address a query found: the service is queried again after 60 s
	garage();
	flash.profiles[0].host[0] = '\0';
	wifi.found = "192.168.1.50";
	wican.dead = true;
	start();
	run(100);
	shows("connecting", "in the network, the adapter located, no answer yet: connecting");
	run_to(15100);
	shows("no_answer", "three rounds without an answer and 15 s in the network: the adapter does not answer");
	wifi.found = "192.168.1.51";
	run_to(60000);
	check(wifi.finds == 1 && strcmp(app_host(app), "192.168.1.50") == 0, "59.98 s without an answer at an address that was found: not queried again yet");
	run_to(60020);
	check(wifi.finds == 2 && strcmp(app_host(app), "192.168.1.51") == 0 && link_up(&app->link), "60 s without an answer: the service is queried again, and the new address is in use");
	wican.dead = false;
	run_to(75000);
	check(view() == CONN_VIEW_LIVE, "the adapter answers at the new address");

	// The adapter does not answer at a stored address: nothing is queried
	garage();
	wican.dead = true;
	start();
	run_to(130000);
	check(wifi.finds == 0 && strcmp(app_host(app), "192.168.1.50") == 0 && view() == CONN_VIEW_NO_ANSWER, "an address stored with the network is never replaced by a query");
	wican.dead = false;
	run_to(145000);
	shows("page_motor", "the adapter answers again at its stored address: values");

	// Joining fails: two attempts per scan
	garage();
	wifi.join_fails = true;
	start();
	run(100);
	check(wifi.scans == 1 && wifi.joins == 2 && view() == CONN_VIEW_NO_WIFI, "a join that fails is tried twice, then the display waits for the next scan");
	run_to(2100);
	check(wifi.scans == 2 && wifi.joins == 4, "2 s later the second scan and two more attempts");
	wifi.join_fails = false;
	run_to(7100);
	check(wifi.scans == 3 && wifi.joins == 5 && view() == CONN_VIEW_LIVE, "5 s later the third scan: the join succeeds and the values are there");

	// The network is lost while the display shows values, and is back at once
	drive();
	wifi.scans = 0;
	wifi.joins = 0;
	lose_wifi();
	check(view() == CONN_VIEW_NO_WIFI && app_host(app)[0] == '\0' && has_line("line: WiCAN nicht gefunden – schläft, stromlos oder außer Reichweite"),
	      "the network is lost: the notice at once, and no address to ask");
	run(100);
	check(wifi.scans == 1 && wifi.joins == 1 && view() == CONN_VIEW_LIVE && dropped == 0, "the network is still there: the display scans, joins and asks again");
	run(1000);
	shows("page_motor", "after the network was lost and found: the values of the engine");
}


/* Standby, heat, safe mode ---------------------------------------------------------------------------- */

static void test_standby(void)
{
	static char without_rpm[sizeof(w906_values) + 2];
	uint64_t nav_input;

	// No network in range: nothing to show
	garage();
	wifi.in_range_count = 0;
	start();
	run(100);
	check(app_backlight(app, 59999) == 80 && app_backlight(app, 60000) == 0, "without anything to show the backlight goes off 60 s after the start, not 1 ms before");
	run_to(61000);
	check(light() == 0, "the scene of the dark screen: no network, 61 s without input");
	short_press();
	check(on(NAV_PAGES) && light() == 80 && app->last_input_ms == 61020, "the first press on a dark screen only wakes it: no menu opens");
	short_press();
	check(on(NAV_MENU) && app->last_input_ms == 61180, "the second press opens the menu");
	run_to(181400);
	check(on(NAV_MENU) && light() == 80, "a menu is something to show: the backlight stays on although 120 s passed since the press");
	run_to(181420);
	check(on(NAV_PAGES) && light() == 0, "when the menu is left by the idle time the screen is dark at once");

	turn(1);
	check(app->nav.page == 0 && light() == 80, "the first detent on a dark screen only wakes it: the page stays");
	turn(1);
	check(app->nav.page == 1, "the second detent turns the page");
	run(60000);
	nav_input = app->nav.last_input_ms;
	check(light() == 0, "60 s after the detent the screen is dark again");
	tap(0);
	check(light() == 80 && app->nav.last_input_ms == nav_input, "the first tap on a dark screen only wakes it: nav is not told");
	tap(0);
	check(app->nav.last_input_ms == now, "the second tap is passed on to nav");
	run(60000);
	swipe(-1, 0);
	check(app->nav.page == 1 && light() == 80, "the first swipe on a dark screen only wakes it: the page stays");
	swipe(-1, 0);
	check(app->nav.page == 2, "the second swipe turns the page");
	run(60000);
	long_press();
	check(app->nav.page == 2 && light() == 80, "a long press that began on a dark screen only wakes it: the page stays");
	long_press();
	check(app->nav.page == 0, "the next long press leads to the first page");
	run(60000);
	app_encoder(app, 2, now);
	check(light() == 0, "counts that make no detent do not wake the screen");
	app_encoder(app, 2, now);
	check(light() == 80 && app->nav.page == 0, "the counts that complete the detent wake it");
	run(59980);
	check(light() == 80 && app_backlight(app, now + 19) == 80 && app_backlight(app, now + 20) == 0, "the idle time counts from the input that woke the screen");

	// A press held from the dark into the light
	run(1000);
	switch_pressed = true;
	run(100);
	check(light() == 80 && on(NAV_PAGES), "the press that wakes the screen");
	run(60000);
	check(light() == 0 && on(NAV_PAGES) && hold_is_stuck(&app->hold), "a switch held for a minute opens nothing, the screen goes dark again, and the switch counts as hanging");
	switch_pressed = false;
	run(100);

	// Settings: never, and one second
	garage();
	wifi.in_range_count = 0;
	strcpy(flash.settings, "{\"standby_s\":0}");
	flash.has_settings = true;
	start();
	check(app_backlight(app, 3600000) == 80, "with the standby time 0 the backlight never goes off");
	strcpy(flash.settings, "{\"standby_s\":1}");
	start();
	check(app_backlight(app, 999) == 80 && app_backlight(app, 1000) == 0, "with the standby time 1 the backlight goes off after 1000 ms");

	// What counts as something to show
	drive();
	check(view() == CONN_VIEW_LIVE && app_backlight(app, 3600000) == 80, "live values are something to show: no standby");
	wican.manual = true;
	wican.seq = 99;
	wican.phase = WICAN_DTC_RUNNING;
	run_to(3100);
	check(view() == CONN_VIEW_SCAN && app_backlight(app, 3600000) == 80, "a scan is something to show: no standby");
	wican.phase = WICAN_DTC_IDLE;
	wican.ignition = false;
	run_to(4100);
	check(view() == CONN_VIEW_ECU_OFFLINE && app_backlight(app, 59999) == 80 && app_backlight(app, 60000) == 0, "with the ignition off there is nothing to show: standby");
	wican.autopid = WICAN_AUTOPID_STARTING;
	run_to(5100);
	check(view() == CONN_VIEW_STARTING && app_backlight(app, 60000) == 0, "an adapter that starts is nothing to show");
	wican.autopid = WICAN_AUTOPID_OFF;
	run_to(6100);
	check(view() == CONN_VIEW_AUTOPID_OFF && app_backlight(app, 60000) == 0, "an adapter without AutoPID is nothing to show");
	wican.autopid = WICAN_AUTOPID_RUN;
	strcpy(wican.id, OTHER);
	run_to(7100);
	check(view() == CONN_VIEW_FOREIGN && app_backlight(app, 60000) == 0, "a foreign adapter is nothing to show");

	drive();
	wican.ignition = false;
	run_to(62200);
	check(view() == CONN_VIEW_ECU_OFFLINE && light() == 0 && values_age(values_find(&app->poll.values, CATALOG_BATTERY), now) == VALUE_AGE_FRESH,
	      "ignition off for a minute: the screen is dark although the battery voltage is measured every second - only a firmware without the API is judged by its values");

	garage();
	wican.dead = true;
	start();
	run(100);
	check(view() == CONN_VIEW_CONNECTING && app_backlight(app, 60000) == 0, "connecting is nothing to show");
	run_to(15100);
	check(view() == CONN_VIEW_NO_ANSWER && app_backlight(app, 60000) == 0, "an adapter that does not answer is nothing to show");

	// A firmware without the API says nothing about the ignition: its values tell
	garage();
	wican.api = false;
	start();
	run_to(70000);
	check(view() == CONN_VIEW_NO_API && light() == 80, "a firmware without the API that delivers values: no standby, 70 s after the last input");
	wican.ignition = false;
	run_to(78900);
	check(light() == 80, "a firmware without the API, the last values 9.9 s old: still something to show");
	run_to(79000);
	check(light() == 0 && view() == CONN_VIEW_NO_API, "a firmware without the API whose values are gone: standby");
	wican.ignition = true;
	run_to(80100);
	check(light() == 80, "a firmware without the API delivers values again: the screen lights up by itself");
	// The first of its values stops coming, the others go on
	snprintf(without_rpm, sizeof(without_rpm), "{%s", w906_values + 16);
	wican.values = without_rpm;
	run_to(92000);
	check(light() == 80 && values_age(values_find(&app->poll.values, "ENGINE_RPM"), now) == VALUE_AGE_GONE && strcmp(app->poll.values.items[0].name, "ENGINE_RPM") == 0,
	      "a firmware without the API: one value that is not gone is enough, also when the first of them is");
	wican.values = "{\"ENGINE_RPM\":0}";
	run_to(104000);
	check(light() == 80 && values_age(values_find(&app->poll.values, "ENGINE_RPM"), now) == VALUE_AGE_FRESH && values_age(values_find(&app->poll.values, "COOLANT_TMP"), now) == VALUE_AGE_GONE,
	      "a firmware without the API: the first value alone is enough as well");

	// ... and one that never delivers a value
	garage();
	strcpy(flash.settings, "{\"standby_s\":1}");
	flash.has_settings = true;
	wican.api = false;
	wican.ignition = false;
	start();
	run_to(3000);
	check(view() == CONN_VIEW_NO_API && app->poll.values.count == 0 && light() == 0, "a firmware without the API and without a single value is nothing to show, also in the first ten seconds of the display");

	// What lies over the screen lights it up
	garage();
	wifi.in_range_count = 0;
	start();
	app_do(app, NAV_DO_RELEASE_ON, now);
	run_to(70000);
	check(light() == 0 && browser_reset() == 1 && light() == 80, "a question of the browser lights up a dark screen");
	run(1500);
	short_press();
	check(done.reset == 1, "a press on the question that lit the screen is its answer, not a wake-up");

	garage();
	wifi.in_range_count = 0;
	start();
	app_do(app, NAV_DO_RELEASE_ON, now);
	run_to(70000);
	check(light() == 0 && browser_upload_begin("0.2.0") && light() == 80, "an upload lights up a dark screen");

	garage();
	wifi.in_range_count = 0;
	machine.update_pending = true;
	start();
	check(app_backlight(app, 200000) == 80, "an update that waits for its confirmation keeps the screen lit");
}

static void test_heat(void)
{
	drive();
	app_temperature(app, 99, false);
	check(app->heat == GUARD_HEAT_NORMAL && light() == 80 && !app->has_temp && app->temp_c == 0, "a reading of 99 degrees that failed is no reading: no limit, no temperature");
	app_temperature(app, 74, true);
	check(app->heat == GUARD_HEAT_NORMAL && light() == 80 && has_line("note:"), "74 degrees: no limit");
	app_temperature(app, 75, true);
	check(app->heat == GUARD_HEAT_DIM && light() == 30, "75 degrees: the backlight is limited to 30");
	shows("page_hot", "too hot: the value page says so");
	app_temperature(app, 84, true);
	check(app->heat == GUARD_HEAT_DIM && light() == 30, "84 degrees: still limited to 30");
	app_temperature(app, 85, true);
	check(app->heat == GUARD_HEAT_OFF && light() == 0, "85 degrees: the backlight is off");
	short_press();
	check(on(NAV_MENU), "while the heat keeps the screen dark a press is passed on: nothing could wake it");
	app_temperature(app, 80, true);
	check(app->heat == GUARD_HEAT_OFF && light() == 0, "back at 80 degrees: still off");
	app_temperature(app, 99, false);
	check(app->heat == GUARD_HEAT_OFF && app->temp_c == 80 && !app->has_temp, "a reading that failed keeps the level and the last temperature");
	app_temperature(app, 79, true);
	check(app->heat == GUARD_HEAT_DIM && light() == 30, "back at 79 degrees: limited to 30");
	app_temperature(app, 70, true);
	check(app->heat == GUARD_HEAT_DIM, "back at 70 degrees: still limited");
	app_temperature(app, 69, true);
	check(app->heat == GUARD_HEAT_NORMAL && light() == 80, "back at 69 degrees: no limit");

	garage();
	strcpy(flash.settings, "{\"brightness\":20}");
	flash.has_settings = true;
	start();
	app_temperature(app, 75, true);
	check(light() == 20, "a brightness below the limit of the heat stays as it is");

	// Dimmed by the heat and dark by the standby rule: there is light to wake
	garage();
	wifi.in_range_count = 0;
	start();
	app_temperature(app, 75, true);
	run_to(61000);
	check(light() == 0, "the scene of standby with the backlight limited by heat: dark");
	short_press();
	check(on(NAV_PAGES) && light() == 30, "a screen that is dark by standby and only dimmed by heat is woken by the first press: no menu opens, the backlight is at its limit");

	// Standby and heat at once: the heat rules
	garage();
	wifi.in_range_count = 0;
	start();
	app_temperature(app, 85, true);
	run_to(61000);
	check(light() == 0, "the scene of heat and standby: dark by both");
	short_press();
	check(on(NAV_MENU), "dark by standby and by heat: the press is passed on, there is nothing to wake");
	app_temperature(app, 70, true);
	check(light() == 30, "cooled down after that press: lit, limited to 30");
}

static void test_safe_mode(void)
{
	static const layout_t untouched;

	garage();
	machine.safe_mode = true;
	strcpy(flash.layout, stored_layout);
	flash.has_layout = true;
	start();
	run(100);
	shows("safe_mode", "safe mode: the built-in views instead of the stored ones, and the note");
	check(app->safe_mode && app->source == APP_LAYOUT_BUILTIN && !app->has_stored && memcmp(&app->stored, &untouched, sizeof(untouched)) == 0,
	      "safe mode: the stored layout is not in use and was not even read");
	check(wifi.ap_on && wifi.aps_on == 1 && wifi.joins == 1 && view() == CONN_VIEW_LIVE, "safe mode: the own access point is open next to the stored network");
	check(stores() == 0 && strcmp(flash.layout, stored_layout) == 0, "safe mode: the stored layout stays in the flash");
	run_to(700000);
	check(wifi.ap_on && wifi.aps_off == 0, "safe mode: the own access point does not close by itself");
	short_press();
	turn(5);
	short_press();
	turn(1);
	short_press();
	run(100);
	check(wifi.ap_on && wifi.aps_off == 0 && has_line("row: > action | Hotspot | an | enabled"), "safe mode: the own access point cannot be switched off at the device");

	machine.safe_mode = false;
	start();
	run(100);
	check(app->source == APP_LAYOUT_STORED && app->has_stored && !wifi.ap_on && has_line("title: Fahrt"), "the start after the safe mode: the stored views, no access point");
}

/* The update and the upload --------------------------------------------------------------------------- */

static void test_update(void)
{
	garage();
	machine.update_pending = true;
	start();
	run(100);
	shows("update", "the first start of an update: the question lies over the value page, five minutes are left");
	run_to(700);
	check(has_line("over_line: sonst alte Version in 5:00"), "the update question at 700 ms: 299.3 s are left, shown rounded up as five minutes");
	run_to(1000);
	check(has_line("over_line: sonst alte Version in 4:59"), "the update question at 1000 ms: 299 s are left");
	turn(1);
	swipe(-1, 0);
	check(app->nav.page == 0 && app->update_pending, "the update question: turning and swiping do nothing");
	long_press();
	check(on(NAV_PAGES) && app->update_pending && done.valid == 0 && has_line("over_line: sonst alte Version in 4:59"), "the update question: a long press does nothing");
	short_press();
	check(done.valid == 1 && done.last == APP_EVENT_MARK_VALID && !app->update_pending && !machine.update_pending && on(NAV_PAGES) && !has_line("over: update"),
	      "the update question: a short press says yes - the firmware is marked as good, the question is gone, the screen below is untouched");
	run_to(400000);
	check(done.reboot == 0 && done.valid == 1, "a confirmed update is not taken back");

	garage();
	machine.update_pending = true;
	start();
	tap(3);
	check(done.valid == 1 && !app->update_pending, "the update question: a tap anywhere says yes as well");

	// Nobody answers
	garage();
	machine.update_pending = true;
	start();
	run_to(299100);
	check(has_line("over_line: sonst alte Version in 0:01") && light() == 80 && app->update_until_ms == 300000, "the update question after 299.1 s: one second left, the screen is lit");
	run_to(300000);
	check(done.reboot == 0 && has_line("over_line: sonst alte Version in 0:00"), "the update question at 300 s, before the tick: nothing asked of the platform yet");
	app_scene(app, &scene, 300500);
	check(strcmp(scene.over_lines[2], "sonst alte Version in 0:00") == 0, "the update question looked at half a second behind its time: no time is left, and none that wrapped around");
	run_to(300020);
	check(done.reboot == 1 && done.last == APP_EVENT_REBOOT && done.valid == 0 && app->update_pending, "an update nobody confirmed for 300 s: the platform is asked to restart");
	run_to(300220);
	check(done.reboot == 2, "the restart is asked for again with the next tick");
	restart_as_asked();
	check(!app->update_pending && app->rolled_back && info_is(0, "Update nicht übernommen – vorherige Version aktiv") && !has_line("over: update"),
	      "after that restart the version before runs, without a question, and the info says that the update was taken back");

	// The time counts from the start, whenever that was
	garage();
	machine.update_pending = true;
	start_at(5000);
	app_tick(app, 304999);
	check(app_take_events(app) == 0, "an update started at 5000: at 304999 no restart");
	app_tick(app, 305000);
	check(app_take_events(app) == APP_EVENT_REBOOT, "an update started at 5000: at 305000 the restart");
	app_tick(app, 305000);
	check(app_take_events(app) == APP_EVENT_REBOOT, "the restart is asked for with every tick from then on");
	app_do(app, NAV_DO_UPDATE_OK, 305000);
	app_tick(app, 400000);
	check(app_take_events(app) == APP_EVENT_MARK_VALID, "an update confirmed late is confirmed: no restart any more");

	// Behind the largest time nothing ends
	garage();
	machine.update_pending = true;
	start_at(UINT64_MAX - 1000);
	check(app->update_until_ms == UINT64_MAX && has_line("over_line: sonst alte Version in 0:01"), "an update started 1000 ms before the largest time ends with it, not at once");
	app_tick(app, UINT64_MAX - 1);
	check(app_take_events(app) == 0, "one millisecond before the largest time: no restart");
	app_tick(app, UINT64_MAX);
	check(app_take_events(app) == APP_EVENT_REBOOT, "at the largest time: the restart");

	// Without an update nothing is asked and nothing restarts
	garage();
	start();
	run_to(301000);
	check(done.reboot == 0 && !has_line("over: update"), "a firmware that is not new asks nothing and restarts nothing");
}

// Nothing of what the browser asked for is in the memory
static bool nothing_asked(void)
{
	return !app->has_wifi_asked && all_bytes(&app->wifi_asked, sizeof(app->wifi_asked), 0) && app->ask_detail[0] == '\0';
}

// drive(), then the release given at the device at 2540; back on the value page it is 5460
static void scene_released(void)
{
	drive();
	short_press();
	turn(3);
	short_press();
	short_press();
	long_press();
	long_press();
	run_to(5460);
}

static void test_upload(void)
{
	scene_released();
	check(on(NAV_PAGES) && access_is_open(&app->access, now) && now == 5460, "the scene of the release: given at the knob, the value page shown again");
	machine.previous_firmware = true;
	app->previous_firmware = true;
	check(browser_upload_begin("0.2.0") && app_busy(app) && !app->previous_firmware, "a firmware upload begins: the display is busy, the other slot holds no version to go back to");
	browser_upload_progress(42);
	shows("upload", "the upload lies over the value page with its percent");
	short_press();
	turn(1);
	tap(0);
	swipe(-1, 0);
	long_press();
	check(on(NAV_PAGES) && app->nav.page == 0 && app->uploading, "during the upload every input is ignored");
	run_to(10000);
	browser_upload_progress(43);
	run_to(40000);
	check(app->uploading && app_busy(app) && has_line("over_line: 43 %"), "29.98 s after the upload brought something last it still runs");
	run_to(40020);
	check(!app->uploading && !app_busy(app) && !has_line("over: upload") && restarts() == 0, "an upload that brought nothing for 30 s is over: the display takes input again");
	short_press();
	check(on(NAV_MENU), "after the upload that stalled the knob works again");

	// The web server writes the time of the upload, and it reads its clock by itself
	scene_released();
	browser_upload_begin("0.2.0");
	app->upload_ms = now + 5000;
	run(1000);
	check(app->uploading, "an upload whose last bytes came at a time ahead of the tick: no time has passed, it runs on");
	run_to(40600);
	check(app->uploading, "with the tick at 40400, 29.94 s behind that time, the upload still runs");
	run_to(40620);
	check(!app->uploading, "with the tick at 40600, 30.14 s behind that time, it is over");

	scene_released();
	browser_upload_begin("0.2.0");
	browser_upload_progress(100);
	check(browser_upload_end() == 1 && !app->uploading, "the upload is complete: the browser asks to install it and gets the ticket 1");
	shows("ask_firmware", "the question names the version of the uploaded firmware");
	run(1500);
	short_press();
	check(done.install == 1 && done.last == APP_EVENT_INSTALL_FIRMWARE && app->ask_detail[0] == '\0' && access_ticket(&app->access, 1, now) == ACCESS_TICKET_CONFIRMED,
	      "a press on the question installs the firmware: the platform is asked to boot it");
	restart_as_asked();
	run(100);
	check(app->update_pending && has_line("over: update") && has_line("over_line: sonst alte Version in 5:00"), "the new firmware starts and asks whether the update is in order");
	short_press();
	check(done.valid == 1 && !machine.update_pending, "the new firmware is confirmed");

	// A question waits when the upload begins, and expires while it runs
	scene_released();
	check(browser_wifi("Neu", "passwort1", "") == 1 && browser_upload_begin("0.2.0"), "the scene of an upload that begins while a network question waits");
	run_to(30000);
	browser_upload_progress(50);
	run_to(59000);
	browser_upload_progress(90);
	check(app->has_wifi_asked && app->uploading, "56 s after the question, under the upload: what it asks for is still kept");
	run_to(65620);
	check(app->uploading && nothing_asked() && access_ticket(&app->access, 1, now) == ACCESS_TICKET_EXPIRED,
	      "the question expired under the upload: what it asked for is dropped with the next tick, password included, although the upload runs on");

	// The dialog of the previous version was open before the upload overwrote the other slot
	garage();
	machine.previous_firmware = true;
	start();
	run(2100);
	app_do(app, NAV_DO_RELEASE_ON, now);
	short_press();
	turn(5);
	short_press();
	turn(3);
	short_press();
	turn(1);
	check(on(NAV_CONFIRM) && app->nav.row == 1 && app->nav.confirm == NAV_DO_PREVIOUS_FIRMWARE, "the scene of the dialog Vorherige Version starten, the focus on Ausführen");
	browser_upload_begin("0.2.0");
	run(31000);
	check(!app->uploading && on(NAV_CONFIRM) && !app->previous_firmware, "the upload stalled and is over; the dialog is still open, the other slot is half written");
	short_press();
	check(done.previous == 0 && restarts() == 0 && on(NAV_PAGES), "Ausführen in a dialog that is older than the upload starts nothing: what is in the other slot now nobody confirmed");
}

/* The release and the questions of the browser ----------------------------------------------------- */

static void test_release(void)
{
	drive();
	short_press();
	turn(3);
	short_press();
	shows("web", "the web screen in the network: the address of the display, the release off");
	check(browser_wifi("Neu", "passwort1", "") == 0 && browser_reset() == 0 && !browser_upload_begin("0.2.0") && nothing_asked(), "without the release the browser can ask nothing");
	short_press();
	shows("web_open", "a short press on Freigabe gives the release for ten minutes");
	long_press();
	shows("menu_open", "the menu tells that the web access is free");
	short_press();
	short_press();
	check(!access_is_open(&app->access, now) && has_line("row: > action | Freigabe | aus | enabled") && stores() == 0, "a second short press on Freigabe takes the release back; nothing of it is stored");
	short_press();
	check(access_is_open(&app->access, now) && now == 4020, "the release given again at 3980");

	check(browser_reset() == 1, "the browser asks for the factory reset and gets the ticket 1");
	shows("ask_reset", "the question of the factory reset lies over the web screen, without a detail");
	turn(1);
	swipe(0, -1);
	check(app->nav.row == 0 && access_asking(&app->access, now) == ACCESS_ASK_RESET, "under the question turning and swiping do nothing");
	long_press();
	check(access_ticket(&app->access, 1, now) == ACCESS_TICKET_REFUSED && restarts() == 0 && access_is_open(&app->access, now) && !has_line("over: ask"),
	      "a long press refuses the question: nothing is reset, the release goes on");

	run_to(6000);
	check(browser_wifi("Neu", "passwort1", "") == 2 && app->has_wifi_asked, "the browser asks to store a network and gets the ticket 2");
	run_to(66000);
	check(access_ticket(&app->access, 2, now) == ACCESS_TICKET_EXPIRED && app->has_wifi_asked && strcmp(app->wifi_asked.password, "passwort1") == 0,
	      "60 s later the question has expired; until the next tick the app still holds what was asked");
	run_to(66020);
	check(nothing_asked() && done.wifi == 0 && app->profile_count == 1, "the tick drops what the expired question asked for, password included; nothing was stored");

	check(browser_wifi("Neu", "passwort1", "") == 3, "the browser asks again and gets the ticket 3");
	app_do(app, NAV_DO_RELEASE_ON, now);
	check(app->has_wifi_asked && strcmp(app->ask_detail, "Neu") == 0 && access_asking(&app->access, now) == ACCESS_ASK_WIFI, "giving the release again keeps a question that waits");
	app_do(app, NAV_DO_RELEASE_OFF, now);
	check(access_ticket(&app->access, 3, now) == ACCESS_TICKET_REFUSED && nothing_asked() && !access_is_open(&app->access, now) && done.wifi == 0,
	      "taking the release back refuses the question and drops what it asked for");

	// The release ends by itself
	drive();
	app_do(app, NAV_DO_RELEASE_ON, now);
	check(access_is_open(&app->access, 602099) && !access_is_open(&app->access, 602100), "a release given at 2100 ends at 602100");
}

// The question of the browser is answered with yes, 1500 ms after it was asked
static void confirm(void)
{
	run(1500);
	app_do(app, NAV_DO_ASK_CONFIRM, now);
	carry_out();
}

static bool profile_is(int index, const char *ssid, const char *password, const char *host)
{
	const net_profile_t *profile = &app->profiles[index];

	return index < app->profile_count && strcmp(profile->ssid, ssid) == 0 && strcmp(profile->password, password) == 0 && strcmp(profile->host, host) == 0;
}

static void test_questions(void)
{
	uint64_t asked_ms;

	// The time a question has to be shown before the knob counts
	scene_released();
	browser_wifi("Neu", "passwort1", "10.0.0.5");
	asked_ms = now;
	app_do(app, NAV_DO_ASK_CONFIRM, asked_ms + 1499);
	check(app_take_events(app) == 0 && app->has_wifi_asked && strcmp(app->wifi_asked.ssid, "Neu") == 0 && strcmp(app->ask_detail, "Neu") == 0 && app->profile_count == 1 &&
	      access_ticket(&app->access, 1, asked_ms + 1499) == ACCESS_TICKET_WAITING, "a confirmation 1499 ms after the question does nothing and drops nothing");
	app_do(app, NAV_DO_ASK_CONFIRM, asked_ms + 1500);
	check(app_take_events(app) == APP_EVENT_STORE_WIFI && nothing_asked() && app->profile_count == 2 && profile_is(0, "Neu", "passwort1", "10.0.0.5") &&
	      profile_is(1, "Werkstatt", "geheim-123", "192.168.1.50"), "a confirmation 1500 ms after the question stores the network in front of the others");
	app_do(app, NAV_DO_ASK_CONFIRM, asked_ms + 1600);
	check(app_take_events(app) == 0 && app->profile_count == 2, "a second confirmation has nothing to confirm");

	// The display leaves its network for the new list
	scene_released();
	wifi.scans = wifi.joins = wifi.leaves = 0;
	strcpy(wifi.in_range[1], "Neu");
	wifi.in_range_count = 2;
	wifi.found = "192.168.7.7";
	browser_wifi("Neu", "passwort1", "");
	run(1500);
	short_press();
	check(done.wifi == 1 && flash.profile_count == 2 && strcmp(flash.profiles[0].ssid, "Neu") == 0 && strcmp(flash.profiles[1].ssid, "Werkstatt") == 0,
	      "a press on the question stores both networks in the flash, the new one first");
	check(app->link.profile_count == 2 && wifi.leaves == 1 && wifi.scans == 1 && wifi.joins == 1 && wifi.profile == 0 && strcmp(app_host(app), "192.168.7.7") == 0 && dropped == 0,
	      "with the new list the display leaves its network, scans and joins the new one, which comes first; no request went out without an address");
	run(1200);
	check(view() == CONN_VIEW_LIVE && info_is(0, "WLAN: Neu (-61 dBm)"), "in the new network the adapter answers");

	// At the moment of the confirmation the poll learns that the adapter is gone
	scene_released();
	browser_wifi("Neu", "passwort1", "");
	run(1500);
	app_do(app, NAV_DO_ASK_CONFIRM, now);
	check(!link_up(&app->link) && !app->poll.wifi && app_host(app)[0] == '\0' && view() == CONN_VIEW_NO_WIFI, "with the confirmation the poll knows at once that the address of the adapter was forgotten");

	// A request without a password
	scene_released();
	browser_wifi("Werkstatt", NULL, "192.168.1.60");
	confirm();
	check(app->profile_count == 1 && profile_is(0, "Werkstatt", "geheim-123", "192.168.1.60") && done.wifi == 1, "a stored network asked for without a password keeps its password and gets the new host");
	browser_wifi("Offen", NULL, "");
	confirm();
	check(app->profile_count == 2 && profile_is(0, "Offen", "", "") && profile_is(1, "Werkstatt", "geheim-123", "192.168.1.60") && done.wifi == 2,
	      "a new network asked for without a password is an open one");
	browser_wifi("Werkstatt", NULL, "192.168.1.61");
	confirm();
	check(app->profile_count == 2 && profile_is(0, "Offen", "", "") && profile_is(1, "Werkstatt", "geheim-123", "192.168.1.61") && done.wifi == 3,
	      "a stored network that is not the first of the list keeps its password as well, and its place");
	browser_wifi("Werkstatt", "", "");
	confirm();
	check(app->profile_count == 2 && profile_is(1, "Werkstatt", "", "") && done.wifi == 4, "a stored network asked for with an empty password becomes an open one");

	// A list read from the flash with the same SSID twice
	garage();
	strcpy(flash.profiles[0].ssid, "Doppelt");
	strcpy(flash.profiles[0].password, "erstes-passwort");
	strcpy(flash.profiles[1].ssid, "Doppelt");
	strcpy(flash.profiles[1].password, "zweites-passwort");
	flash.profile_count = 2;
	start();
	app_do(app, NAV_DO_RELEASE_ON, now);
	browser_wifi("Doppelt", NULL, "10.0.0.9");
	confirm();
	check(app->profile_count == 2 && profile_is(0, "Doppelt", "erstes-passwort", "10.0.0.9") && profile_is(1, "Doppelt", "zweites-passwort", ""),
	      "of two stored entries with the SSID asked for the first one is replaced and its password kept");

	// A full list
	garage();
	for(int i = 0; i < NET_PROFILES_MAX; i++)
	{
		snprintf(flash.profiles[i].ssid, NET_SSID_SIZE, "Netz %d", i);
		strcpy(flash.profiles[i].password, "passwort");
	}
	flash.profile_count = NET_PROFILES_MAX;
	start();
	app_do(app, NAV_DO_RELEASE_ON, now);
	browser_wifi("Neu", "passwort1", "");
	confirm();
	check(app->profile_count == 4 && profile_is(0, "Neu", "passwort1", "") && profile_is(1, "Netz 0", "passwort", "192.168.1.50") && profile_is(3, "Netz 2", "passwort", "") &&
	      flash.profile_count == 4 && strcmp(flash.profiles[3].ssid, "Netz 2") == 0, "a fifth network takes the first place and the last one falls out");

	// What net_store() refuses
	scene_released();
	browser_wifi("Neu", "kurz", "");
	confirm();
	check(done.wifi == 0 && app->profile_count == 1 && profile_is(0, "Werkstatt", "geheim-123", "192.168.1.50") && nothing_asked() && link_up(&app->link) &&
	      access_ticket(&app->access, 1, now) == ACCESS_TICKET_CONFIRMED, "a network net_store() refuses is not stored: no event, the list and the link stay; the request is dropped");
	check(access_ask(&app->access, ACCESS_ASK_WIFI, now) == 2, "a question for a network without a request of the browser, as app_web.h never asks it");
	confirm();
	check(done.wifi == 0 && app->profile_count == 1 && link_up(&app->link), "a confirmed network question without a network asked for stores nothing");
	strcpy(app->wifi_asked.ssid, "Geist");
	strcpy(app->wifi_asked.password, "passwort1");
	app->wifi_asked.has_password = true;
	check(access_ask(&app->access, ACCESS_ASK_WIFI, now) == 3 && !app->has_wifi_asked, "a network in the room of the request that is not marked as asked for");
	confirm();
	check(done.wifi == 0 && app->profile_count == 1 && nothing_asked(), "what is not marked as asked for is not stored, whatever stands in the room of the request");
	strcpy(app->wifi_asked.ssid, "Garage");
	strcpy(app->wifi_asked.password, "hintertuer1");
	app->has_wifi_asked = true;
	check(access_ask(&app->access, ACCESS_ASK_WIFI, now) == 4 && !app->wifi_asked.has_password, "a request that is marked as one without a password, with a text in the room of the password");
	confirm();
	check(done.wifi == 1 && app->profile_count == 2 && profile_is(0, "Garage", "", ""), "a request without a password for a new network stores an open one, whatever stands in the room of the password");

	// The other two questions
	scene_released();
	check(browser_reset() == 1, "the browser asks for the factory reset");
	run(1500);
	tap(0);
	check(done.reset == 1 && done.last == APP_EVENT_FACTORY_RESET && restarts() == 1, "a tap on the question of the factory reset confirms it");

	scene_released();
	browser_upload_begin("0.2.0");
	browser_upload_end();
	app_do(app, NAV_DO_ASK_REFUSE, now);
	check(app_take_events(app) == 0 && app->ask_detail[0] == '\0' && access_ticket(&app->access, 1, now) == ACCESS_TICKET_REFUSED, "a refused firmware question installs nothing and drops the version asked for");
	run(1600);
	short_press();
	check(on(NAV_MENU) && restarts() == 0, "after the refusal a press is a press on the screen again");
}

/* The choice of the layout --------------------------------------------------------------------------- */

static layout_t parsed;
static char written[LAYOUT_TEXT_MAX + 1];

// layout_text is the text layout_to_json() writes for the layout in use, and reads back as a layout with that
// text. (The layouts themselves are not compared: layout_parse() leaves the bytes between the fields of a
// limit as they were on its stack.)
static bool text_follows(void)
{
	int length = layout_to_json(&app->layout, written, sizeof(written));

	if(length <= 0 || (size_t)length != app->layout_length || strcmp(written, app->layout_text) != 0) return false;
	if(!layout_parse(app->layout_text, app->layout_length, &parsed, NULL, work, LAYOUT_TOKENS)) return false;
	return layout_to_json(&parsed, written, sizeof(written)) == length && strcmp(written, app->layout_text) == 0 && parsed.page_count == app->layout.page_count;
}

static void test_layout_choice(void)
{
	static char with_extra[sizeof(w906_values) + 16];

	// Another vehicle
	garage();
	wican.config = other_config;
	wican.values = other_values;
	start();
	check(app->source == APP_LAYOUT_BUILTIN && app->catalog_sum == catalog_checksum(&app->poll.catalog), "before the catalogue is loaded the built-in views are chosen, whatever the vehicle will be");
	turn(3);
	check(app->nav.page == 3, "the scene of another vehicle: the fourth built-in page is shown when its profile arrives");
	run(100);
	shows("generated", "another vehicle: views made from its catalogue, four values per page, and the first page shown");
	check(app->source == APP_LAYOUT_GENERATED && app->layout.page_count == 2 && app->nav.page == 0 && text_follows() && app->catalog_sum == catalog_checksum(&app->poll.catalog),
	      "another vehicle: the generated layout is in use, layout_text is its text, the check sum is the one of the catalogue it was made from");
	check(stores() == 0, "generated views are not stored");
	turn(1);
	shows("generated2", "another vehicle, the second page: the last value and the battery voltage");

	// The catalogue grows: the same views made anew
	wican.values = "{\"SPEED\":0,\"RPM\":790,\"COOLANT\":81.5,\"FUEL\":62,\"INTAKE_TEMP\":19,\"EXTRA\":1}";
	run_to(1100);
	check(can_read(), "a profile without an engine speed leaves the decision about reading to the adapter");
	shows("generated_grown", "a value the profile did not name arrives: the views are made anew with it, and the page shown stays");
	check(app->source == APP_LAYOUT_GENERATED && app->nav.page == 1 && text_follows() && strstr(app->layout_text, "\"EXTRA\"") != NULL, "the views made anew: layout_text follows");
	run_to(31100);
	check(done.catalog == 1 && stores() == 1 && flash_catalog_is(7, "SPEED", "km/h"), "30 s after the catalogue came to rest it is stored");

	// The adapter restarts with the profile of the W906
	adapter_restart(&wican, 777, 100);
	wican.config = w906_config;
	wican.values = NULL;
	run_to(32100);
	shows("page_motor", "the adapter comes back with the profile of the W906: the built-in views, from their first page");
	check(app->source == APP_LAYOUT_BUILTIN && strcmp(app->layout_text, builtin_text) == 0 && app->layout_length == strlen(builtin_text) && app->nav.page == 0,
	      "back on the built-in views: layout_text is their text again");
	turn(2);
	run_to(40100);
	check(app->nav.page == 2 && app->source == APP_LAYOUT_BUILTIN, "the built-in views stay, and so does the page, while the same catalogue is confirmed round by round");
	snprintf(with_extra, sizeof(with_extra), "{\"EXTRA\":1,%s", w906_values + 1);
	wican.values = with_extra;
	run_to(41100);
	check(app->poll.catalog.count == 37 && app->catalog_sum == catalog_checksum(&app->poll.catalog) && app->source == APP_LAYOUT_BUILTIN && app->nav.page == 2 && strcmp(app->layout_text, builtin_text) == 0,
	      "a value the profile of the W906 did not name arrives: the built-in views are chosen again for the catalogue that grew, and the page shown stays");

	// The start with the stored catalogue of another vehicle
	garage();
	flash.has_catalog = true;
	strcpy(flash.catalog, "{\"@BATT_V\":{\"unit\":\"V\",\"class\":\"\",\"profile\":false,\"delivered\":false},\"SPEED\":{\"unit\":\"km/h\",\"class\":\"speed\",\"profile\":true,\"delivered\":false}}");
	wifi.in_range_count = 0;
	start();
	check(app->source == APP_LAYOUT_GENERATED && app->layout.page_count == 1 && app->layout.pages[0].item_count == 2 && strcmp(app->layout.pages[0].title, "Werte 1") == 0 &&
	      strcmp(app->layout.pages[0].items[0].key, "SPEED") == 0 && text_follows() && app->nav.page == 0,
	      "the start with the stored catalogue of another vehicle: views made from it at once");
	shows("generated_battery", "the start with the stored catalogue of another vehicle and no network: the notice, one page");

	// A stored layout stays, whatever the vehicle
	garage();
	strcpy(flash.layout, stored_layout);
	flash.has_layout = true;
	wican.config = other_config;
	wican.values = other_values;
	start();
	run(1100);
	check(app->source == APP_LAYOUT_STORED && strcmp(app->layout_text, stored_layout) == 0 && app->poll.catalog.count == 6 && app->catalog_sum == catalog_checksum(&app->poll.catalog) &&
	      has_line("item: Drehzahl | n. v. |  | dim | number | -1") && has_line("dots: 1/1"), "a stored layout stays in use on a vehicle that has but one of its values; the check sum follows the catalogue all the same");

	// A stored layout that cannot be read
	garage();
	strcpy(flash.layout, "{\"format\":\"wican-display-layout\",\"v\":2,\"pages\":[]}");
	flash.has_layout = true;
	start();
	run(100);
	check(app->source == APP_LAYOUT_BUILTIN && !app->has_stored && has_line("title: Motor") && stores() == 0 && strcmp(flash.layout, "{\"format\":\"wican-display-layout\",\"v\":2,\"pages\":[]}") == 0,
	      "a stored layout of a later version cannot be read: the built-in views are in use, and the stored text is left alone");

	// A stored layout longer than any layout may be
	garage();
	memset(flash.layout, ' ', LAYOUT_TEXT_MAX);
	memcpy(flash.layout, stored_layout, strlen(stored_layout));
	flash.layout[LAYOUT_TEXT_MAX] = '\0';
	flash.has_layout = true;
	start();
	check(app->source == APP_LAYOUT_STORED && app->layout_length == LAYOUT_TEXT_MAX && memcmp(app->layout_text, flash.layout, LAYOUT_TEXT_MAX + 1) == 0 &&
	      all_bytes(box.behind, sizeof(box.behind), FILL), "a stored layout of the largest size, 16384 bytes, is taken with its whole text");
	app_choose_layout(app);
	check(app->source == APP_LAYOUT_BUILTIN && strcmp(app->layout_text, builtin_text) == 0 && app->layout_length == strlen(builtin_text),
	      "the choice made anew behind the longest text: layout_text is the built-in text and ends where that ends");

	// A vehicle with the profile of the W906 but for the four values of its first page, behind another vehicle
	// whose values stay in the catalogue: the profile is loaded anew because the number of its values changed
	garage();
	wican.config = other_config;
	wican.values = other_values;
	start();
	run(100);
	check(app->source == APP_LAYOUT_GENERATED && app->nav.page == 0, "the scene of the other vehicle: generated views, their first page");
	wican.autopid = WICAN_AUTOPID_OFF;
	run_to(1100);
	wican.autopid = WICAN_AUTOPID_RUN;
	wican.config = no_motor_config;
	wican.values = "{}";
	run_to(2020);
	check(app->source == APP_LAYOUT_BUILTIN && app->poll.catalog.count == 37 && !layout_page_shown(&app->layout, 0, &app->poll.catalog) && app->nav.page == 1,
	      "the built-in views are chosen for a catalogue without the values of their first page: they start at the first page they show, the second");

	// No built-in layout
	garage();
	machine.no_builtin = true;
	start();
	check(!app->has_builtin && app->source == APP_LAYOUT_GENERATED && app->layout.page_count == 1 && strcmp(app->layout.pages[0].items[0].key, "@BATT_V") == 0 && text_follows(),
	      "without a built-in layout the views are made from the catalogue: before it is loaded, the battery voltage alone");
	run(100);
	check(app->source == APP_LAYOUT_GENERATED && app->layout.page_count == 9 && has_line("title: Werte 1") && has_line("item: Engine Rpm | 0,0 | RPM | normal | number | -1") && text_follows(),
	      "without a built-in layout the 36 values of the W906 are shown on nine generated pages");

	// What the browser does with the layout (app_web.h)
	scene_released();
	check(browser_layout(stored_layout, false) && app->source == APP_LAYOUT_PREVIEW, "the browser sends a layout to look at");
	run(300);
	check(has_line("title: Fahrt") && has_line("dots: 1/2") && stores() == 0, "the preview is shown from its first page and is not stored");
	adapter_restart(&wican, 777, 100);
	run(2000);
	check(app->source == APP_LAYOUT_PREVIEW && has_line("title: Fahrt"), "a preview stays when the catalogue changes");
	check(browser_layout_reset() && app->source == APP_LAYOUT_BUILTIN && app->nav.page == 0 && strcmp(app->layout_text, builtin_text) == 0,
	      "the browser resets the layout: app_choose_layout() makes the choice anew, whatever the source was");
	run(100);
	check(done.erase == 1 && has_line("title: Motor"), "the reset is carried out and the built-in views are shown");
	check(browser_layout(stored_layout, true) && app->source == APP_LAYOUT_STORED, "the browser stores a layout");
	run(300);
	check(done.layout == 1 && strcmp(flash.layout, stored_layout) == 0 && has_line("title: Fahrt"), "the stored layout is in the flash and on the screen");
	adapter_restart(&wican, 778, 100);
	wican.config = other_config;
	wican.values = other_values;
	run(2000);
	check(app->source == APP_LAYOUT_STORED && app->poll.catalog.count == 6, "a layout stored by the browser stays when another vehicle answers");
	check(browser_layout_reset() && app->source == APP_LAYOUT_GENERATED && app->nav.page == 0 && text_follows(), "reset on another vehicle: generated views");
}


/* The functions one by one ------------------------------------------------------------------------- */

// A copy of the app to compare with. It is never used as an app: its pointers lead into the original.
static app_t snapshot;

static void remember(void)
{
	memcpy(&snapshot, app, sizeof(snapshot));
}

static bool unchanged(void)
{
	return memcmp(&snapshot, app, sizeof(snapshot)) == 0;
}

static void test_do(void)
{
	static const int values[] = {INT_MIN, -1, 0, 4, 5, 6, 99, 100, 101, 255, 256, 261, INT_MAX};
	static const int kept[] = {5, 5, 5, 5, 5, 6, 99, 100, 100, 100, 100, 100, 100};
	static const nav_do_t none[] = {NAV_DO_NOTHING, (nav_do_t)(NAV_DO_FACTORY_RESET + 1), (nav_do_t)-1, (nav_do_t)INT_MAX};
	bool same = true;

	scene_list();
	for(int i = 0; i < COUNT(none); i++)
	{
		remember();
		app_do(app, none[i], app->clock_ms);
		same = same && unchanged();
	}
	check(same, "app_do() with nothing to do, and with what is no action of nav.h, changes nothing");
	app_do(app, NAV_DO_NOTHING, now + 500);
	check(app->clock_ms == now + 500, "app_do() takes the time over, also with nothing to do");

	drive();
	same = true;
	for(int i = 0; i < COUNT(values); i++)
	{
		app->nav.value = values[i];
		app_do(app, NAV_DO_BRIGHTNESS, now);
		same = same && app->brightness_preview == kept[i] && light() == kept[i];
	}
	check(same && app->events == 0 && app->settings.brightness == 80, "the brightness being set is kept within 5 and 100 whatever nav holds, and is not stored");
	same = true;
	for(int i = 0; i < COUNT(values); i++)
	{
		app->nav.value = values[i];
		app_do(app, NAV_DO_SETTINGS_STORE, now);
		same = same && app->settings.brightness == kept[i] && app->settings.night == 25 && app->brightness_preview == -1 && app_take_events(app) == APP_EVENT_STORE_SETTINGS;
	}
	check(same, "the brightness stored is kept within 5 and 100 whatever nav holds: what is stored can be read at the next start");
	app_do(app, NAV_DO_NIGHT_TOGGLE, now);
	check(app->settings.night_mode && app_take_events(app) == APP_EVENT_STORE_SETTINGS, "the night mode is switched on and to be stored");
	same = true;
	for(int i = 0; i < COUNT(values); i++)
	{
		app->nav.value = values[i];
		app_do(app, NAV_DO_BRIGHTNESS, now);
		same = same && light() == kept[i];
		app_do(app, NAV_DO_SETTINGS_STORE, now);
		same = same && app->settings.night == kept[i] && app->settings.brightness == 100 && app_take_events(app) == APP_EVENT_STORE_SETTINGS;
	}
	check(same, "in night mode the brightness being set and stored is the one of the night, within the same limits");
	app->nav.value = 45;
	app_do(app, NAV_DO_SETTINGS_STORE, now);
	check(app->settings.night == 45 && app->settings.brightness == 100 && app_take_events(app) == APP_EVENT_STORE_SETTINGS,
	      "in night mode a brightness screen that is left without a detent stores what nav holds, not a value that was never set");
	app_do(app, NAV_DO_NIGHT_TOGGLE, now);
	check(!app->settings.night_mode && app_take_events(app) == APP_EVENT_STORE_SETTINGS, "the night mode is switched off and to be stored");
	app_do(app, NAV_DO_REVERSE_TOGGLE, now);
	check(app->settings.reverse && app->knob.reverse && app_take_events(app) == APP_EVENT_STORE_SETTINGS, "the direction is reversed, in the settings and at the knob, and to be stored");
	app_do(app, NAV_DO_REVERSE_TOGGLE, now);
	check(!app->settings.reverse && !app->knob.reverse && app_take_events(app) == APP_EVENT_STORE_SETTINGS, "the direction is normal again, in the settings and at the knob");

	app_do(app, NAV_DO_AP_TOGGLE, now);
	check(app->link.ap_wanted && !link_ap_on(&app->link) && app->events == 0, "the access point is asked for; the link orders it with its next action");
	app_do(app, NAV_DO_AP_TOGGLE, now);
	check(app->link.ap_wanted, "asked for once more before the link ordered it: the opposite of what is on is still wanted");
	link_step();
	app_do(app, NAV_DO_AP_TOGGLE, now);
	check(!app->link.ap_wanted && link_ap_on(&app->link), "the access point that is on is asked to close");
	link_step();

	app_do(app, NAV_DO_UPDATE_OK, now);
	check(app_take_events(app) == APP_EVENT_MARK_VALID && !app->update_pending, "the update is confirmed: the firmware is to be marked as good");
	app_do(app, NAV_DO_REBOOT, now);
	check(app_take_events(app) == APP_EVENT_REBOOT, "the restart is one event and nothing else");
	app_do(app, NAV_DO_FACTORY_RESET, now);
	check(app_take_events(app) == APP_EVENT_FACTORY_RESET, "the factory reset is one event and nothing else");
	app_do(app, NAV_DO_PREVIOUS_FIRMWARE, now);
	check(app_take_events(app) == 0, "the previous firmware is not started while the other slot holds none");
	app->previous_firmware = true;
	app_do(app, NAV_DO_PREVIOUS_FIRMWARE, now);
	check(app_take_events(app) == APP_EVENT_PREVIOUS_FIRMWARE, "the previous firmware is one event and nothing else");
	app_do(app, NAV_DO_REBOOT, now);
	app_do(app, NAV_DO_NIGHT_TOGGLE, now);
	check(app_take_events(app) == (APP_EVENT_REBOOT | APP_EVENT_STORE_SETTINGS) && app_take_events(app) == 0, "the events are collected until they are taken, and taken once");

	app_do(app, NAV_DO_ASK_CONFIRM, now);
	app_do(app, NAV_DO_ASK_REFUSE, now);
	check(app_take_events(app) == 0 && access_asking(&app->access, now) == ACCESS_ASK_NONE, "confirming and refusing without a question does nothing");
	app_do(app, NAV_DO_RELEASE_ON, now);
	check(access_is_open(&app->access, now) && app->access.open_until_ms == now + ACCESS_OPEN_MS, "the release is given at the time of the call");
	app_do(app, NAV_DO_RELEASE_OFF, now);
	check(!access_is_open(&app->access, now), "the release is taken back");

	// What is not allowed is not sent
	drive();
	wican.rpm = 800;
	run_to(3100);
	app_do(app, NAV_DO_READ, now);
	app_do(app, NAV_DO_CLEAR, now);
	run(100);
	check(requests() == 2 && phase() == DTC_FLOW_IDLE, "a read and a clear that are not allowed are not sent");

	scene_list();
	app_do(app, NAV_DO_DISMISS, now);
	check(phase() == DTC_FLOW_IDLE && !app->poll.has_list && app->list_lines == 0 && app->poll.events == 0, "a dismissed list is gone at once, lines included: the events of the poll are taken with it");
	scene_list();
	app_do(app, NAV_DO_READ, now);
	check(phase() == DTC_FLOW_READ_SENT && app->list_lines == 0 && app->poll.events == 0, "a read drops the lines of the list at once");
	scene_list();
	app->hold.stuck = true;
	app_do(app, NAV_DO_CLEAR, now);
	check(phase() == DTC_FLOW_LIST, "a clear with a switch that hangs is not started");
	app->hold.stuck = false;
	app_do(app, NAV_DO_CLEAR, now);
	check(phase() == DTC_FLOW_CLEAR_SENT && app->poll.events == 0, "a clear with a sound switch is started");
	app_do(app, NAV_DO_HOLD_OPEN, now);
	check(app->hold.open && app->hold.opened_ms == now, "the hold dialog opens at the time of the call");
	app_do(app, NAV_DO_HOLD_CLOSE, now);
	check(!app->hold.open, "the hold dialog closes");
}

static void test_world(void)
{
	static struct
	{
		unsigned char front[GUARD];
		nav_world_t world;
		unsigned char behind[GUARD];
	} over, zeros;
	const nav_world_t *seen = &over.world;

	scene_list();
	memset(&over, FILL, sizeof(over));
	app_world(app, &over.world, now);
	memset(&zeros, 0, sizeof(zeros));
	app_world(app, &zeros.world, now);
	check(memcmp(&over.world, &zeros.world, sizeof(nav_world_t)) == 0, "app_world() writes every byte of the world");
	check(all_bytes(over.front, GUARD, FILL) && all_bytes(over.behind, GUARD, FILL), "app_world() writes no byte outside of the world");
	check(seen->layout == &app->layout && seen->catalog == &app->poll.catalog && seen->flow == DTC_FLOW_LIST && seen->can_read && seen->can_clear && seen->list_lines == 7 &&
	      seen->cleared_lines == 0 && seen->old_lines == 0 && seen->info_lines == 13 && seen->asking == ACCESS_ASK_NONE && !seen->release_open && !seen->update_pending &&
	      !seen->uploading && !seen->previous_firmware && !seen->night_mode && seen->brightness == 80, "the world with a list of the own read: every member is the one of the app");

	garage();
	strcpy(flash.settings, "{\"brightness\":60,\"night\":15,\"night_mode\":true}");
	flash.has_settings = true;
	strcpy(flash.old, result_read);
	flash.has_old = true;
	machine.update_pending = true;
	machine.previous_firmware = true;
	machine.rolled_back = true;
	start();
	app_world(app, &over.world, now);
	check(seen->flow == DTC_FLOW_IDLE && !seen->can_read && !seen->can_clear && seen->list_lines == 0 && seen->old_lines == 7 && seen->info_lines == 14 && seen->update_pending &&
	      seen->previous_firmware && seen->night_mode && seen->brightness == 15, "the world at a start with an update to confirm, an old list and the night mode");
	app->settings.night_mode = false;
	app_world(app, &over.world, now);
	check(!seen->night_mode && seen->brightness == 60, "the world by day: the brightness of the day");
	machine.previous_firmware = false;
	machine.update_pending = false;
	start();
	app_world(app, &over.world, now);
	check(!seen->previous_firmware && !seen->update_pending && seen->info_lines == 14, "the world after an update that was taken back, with nothing in the other slot: no firmware to go back to");

	scene_released();
	browser_upload_begin("0.2.0");
	app_world(app, &over.world, now);
	check(seen->uploading && seen->release_open && seen->asking == ACCESS_ASK_NONE, "the world with an upload and the release");
	browser_upload_end();
	app_world(app, &over.world, now);
	check(!seen->uploading && seen->asking == ACCESS_ASK_FIRMWARE, "the world with a question");
	app_world(app, &over.world, now + ACCESS_CONFIRM_MS);
	check(seen->asking == ACCESS_ASK_NONE && seen->release_open, "the world at the time the question expires: the time given counts");
	app_world(app, &over.world, now + ACCESS_OPEN_MS);
	check(!seen->release_open, "the world at the time the release ends");

	scene_clearing();
	run_to(14300);
	app_world(app, &over.world, now);
	check(seen->flow == DTC_FLOW_CLEARED && seen->cleared_lines == 4 && seen->old_lines == 7 && seen->list_lines == 0, "the world with the outcome of a clear");

	scene_dialog();
	app_world(app, &over.world, now);
	check(seen->can_clear, "the scene of the dialog: clearing is allowed");
	app->hold.stuck = true;
	app_world(app, &over.world, now);
	check(!seen->can_clear && seen->can_read, "the world with a switch that hangs: clearing is not allowed, reading is");
}

static void test_scene_inputs(void)
{
	static struct
	{
		unsigned char front[GUARD];
		scene_t scene;
		unsigned char behind[GUARD];
	} over, zeros;
	nav_world_t seen;

	scene_list();
	memset(&over, FILL, sizeof(over));
	app_scene(app, &over.scene, now);
	memset(&zeros, 0, sizeof(zeros));
	app_scene(app, &zeros.scene, now);
	check(memcmp(&over.scene, &zeros.scene, sizeof(scene_t)) == 0 && all_bytes(over.front, GUARD, FILL) && all_bytes(over.behind, GUARD, FILL),
	      "app_scene() writes every byte of the scene and none outside of it");
	remember();
	app_scene(app, &scene, now);
	app_backlight(app, now);
	app_busy(app);
	app_host(app);
	app_world(app, &seen, now);
	check(unchanged(), "looking at the screen, the backlight, the world, the host and whether the display is busy changes nothing in the app");

	// What the poll does not hold is not shown, whatever lines the app still has
	scene_list();
	app->poll.has_list = false;
	check(app->list_lines == 7 && has_line("total: 3") && has_line("row: > action | Erneut lesen |  | enabled") && !has_line("row: - line | Motorelektronik | 7E0 · 2 Fehler | enabled"),
	      "a list the poll does not hold any more is not shown: the screen has its three choices alone");
	app->poll.has_list = true;
	long_press();
	check(has_line("line: 3 Fehler in 2 Steuergeräten"), "the scene of the fault memory menu with a list: its summary");
	app->poll.has_list = false;
	check(has_line("line: Liste gelesen") && !has_line("line: 3 Fehler in 2 Steuergeräten"), "without the list in the poll its summary is not shown either");

	scene_clearing();
	run_to(14300);
	check(on(NAV_DTC_CLEARED) && has_line("total: 5"), "the scene of the outcome: four lines and Fertig");
	app->poll.has_cleared = false;
	check(has_line("total: 1") && has_line("row: > action | Fertig |  | enabled"), "an outcome the poll does not hold any more is not shown");
	app->poll.has_cleared = true;
	turn(4);
	short_press();
	turn(2);
	short_press();
	check(on(NAV_DTC_OLD) && has_line("total: 8"), "the scene of the old list: seven lines and Zurück");
	app->poll.has_old = false;
	check(has_line("total: 1") && has_line("row: > action | Zurück |  | enabled"), "an old list the poll does not hold any more is not shown");

	// The texts of the platform on the web screen
	drive();
	short_press();
	turn(3);
	short_press();
	platform.ap_ssid = "Mein Display";
	platform.ap_password = "nur-ich-1";
	tell_platform();
	app_do(app, NAV_DO_AP_TOGGLE, now);
	check(app->link.ap_wanted && !has_line("line: WLAN: Mein Display"), "an access point that is asked for and not ordered yet is not named on the web screen");
	link_step();
	check(has_line("line: http://192.168.1.77") && has_line("line: WLAN: Mein Display") && has_line("line: Passwort: nur-ich-1"), "the web screen names the address and what a phone needs for the own access point");
	platform.ap_ssid = "WiCAN-Display";
	platform.ap_password = "geheim1234";
	app->ip[0] = '\0';
	check(!has_line("line: http://192.168.1.77") && !has_line("line: http://") && has_line("line: WLAN: Mein Display"), "without an ip the web screen names no address");
	strcpy(app->ip, "2001:db8:85a3:8d3:1319:8a2e:370:7348");
	check(has_line("line: http://2001:db8:85a3:8d3:1319:8a2e:370:7348"), "the longest ip has room behind the http://");
}

static void test_inputs(void)
{
	drive();
	short_press();
	swipe(0, -1);
	check(on(NAV_MENU) && app->nav.row == 3, "the menu, a swipe up: the focus moves three rows down");
	swipe(0, -1);
	check(app->nav.row == 6, "the menu, a second swipe up: the last row");
	swipe(0, 1);
	check(app->nav.row == 3, "the menu, a swipe down: the focus moves three rows up");
	swipe(0, 1);
	swipe(0, 1);
	check(app->nav.row == 0, "the menu, two more swipes down: the first row");
	swipe(0, INT_MIN);
	check(app->nav.row == 3, "of a vertical swipe only the direction counts: far up is three rows down");
	swipe(0, INT_MAX);
	check(app->nav.row == 0, "far down is three rows up");
	swipe(-1, -1);
	swipe(1, -1);
	swipe(-1, 0);
	swipe(1, 0);
	check(app->nav.row == 0 && on(NAV_MENU), "a swipe with a horizontal part does nothing in the menu");
	run(1000);
	swipe(0, 0);
	check(app->nav.last_input_ms == now && app->last_input_ms == now && on(NAV_MENU) && app->nav.row == 0, "a swipe without a direction is an input and nothing else");
	swipe(0, -1);
	swipe(0, 0);
	check(app->nav.row == 3, "a swipe without a direction moves no focus, wherever the focus is");
	swipe(0, 1);
	tap(7);
	tap(-1);
	check(on(NAV_MENU) && app->nav.row == 0, "a tap on a row the menu does not have does nothing");
	tap(2);
	check(app->settings.night_mode && app->nav.row == 2, "a tap on Nachtmodus switches it");
	tap(6);
	check(on(NAV_PAGES), "a tap on Zurück leaves the menu");

	swipe(INT_MIN, 0);
	check(app->nav.page == 1, "of a horizontal swipe only the direction counts: far left is the next page");
	swipe(-5, 7);
	check(app->nav.page == 2, "a swipe to the left that also goes down is the next page");
	swipe(INT_MAX, 0);
	check(app->nav.page == 1, "far right is the page before");
	swipe(3, -9);
	check(app->nav.page == 0, "a swipe to the right that also goes up is the page before");

	// A screen without rows
	short_press();
	turn(1);
	short_press();
	swipe(0, -1);
	swipe(0, 1);
	check(on(NAV_BRIGHTNESS) && app->nav.value == 25 && app->brightness_preview == -1, "the brightness screen has no rows: a vertical swipe changes nothing");
	long_press();

	// The dialogs
	turn(4);
	short_press();
	turn(2);
	short_press();
	swipe(0, -1);
	check(on(NAV_CONFIRM) && app->nav.row == 1 && restarts() == 0, "a dialog has two rows: a swipe up puts the focus on the second, nothing is carried out");
	swipe(0, 1);
	check(app->nav.row == 0, "a swipe down puts the focus on Abbrechen again");

	scene_list();
	turn(8);
	short_press();
	run(400);
	swipe(0, -1);
	check(on(NAV_DTC_CONFIRM) && app->nav.row == 1 && app->hold.last_input_ms == now, "in the clear dialog a swipe moves the focus like the knob, and is an activity for the hold");
	switch_pressed = true;
	run(2000);
	swipe(0, -1);
	run(2980);
	check(sent[POLL_DTC_CLEAR] == 0, "a vertical swipe during the hold breaks it");
	run(40);
	check(sent[POLL_DTC_CLEAR] == 1, "3000 ms after the swipe the hold is complete");

	// The info screen
	drive();
	short_press();
	turn(4);
	short_press();
	swipe(0, -1);
	check(on(NAV_INFO) && app->nav.row == 3, "the info screen scrolls three lines with a swipe");
	tap(5);
	check(on(NAV_MENU) && app->nav.row == 4, "a tap on a line of the info screen leaves it, like a short press");

	// The switch reads pressed while the display starts
	garage();
	start();
	switch_pressed = true;
	run(100);
	switch_pressed = false;
	run(100);
	check(on(NAV_PAGES) && app->nav.last_input_ms == 0, "a switch that reads pressed when the display starts and is released then reports nothing: no menu opens");
	short_press();
	check(on(NAV_MENU), "the press that follows that release is a press");

	// Readings that fail say nothing, whatever they read
	drive();
	switch_ok = false;
	switch_pressed = true;
	run(200);
	switch_pressed = false;
	run(40);
	switch_ok = true;
	run(100);
	check(on(NAV_PAGES) && app->nav.last_input_ms == 0 && app->last_input_ms == 0, "readings that failed and read pressed are no press: no menu opens, nothing counts as an input");
	switch_ok = false;
	switch_pressed = true;
	run(1000);
	switch_ok = true;
	switch_pressed = false;
	run(100);
	check(on(NAV_PAGES) && app->nav.last_input_ms == 0, "a second of readings that failed and read pressed is no long press either");
}

static void test_clock(void)
{
	nav_world_t seen;

	// The hold counts in the time of the app: a reading that comes with an earlier time lets none pass
	scene_dialog();
	switch_pressed = true;
	run_to(9000);
	now = 4000;
	run_to(5620);
	check(sent[POLL_DTC_CLEAR] == 0 && app->clock_ms == 8980, "the time of the readings steps back by 5 s during the hold: 3000 ms of readings in all confirm nothing");
	run_to(10600);
	check(sent[POLL_DTC_CLEAR] == 0, "the hold goes on when the time of the readings has caught up: at 10580 nothing is sent");
	run_to(10620);
	check(sent[POLL_DTC_CLEAR] == 1 && clear_sent_ms == 10600, "3000 ms of the time of the app after the press the clear is sent");

	garage();
	wifi.in_range_count = 0;
	start();
	run_to(70000);
	check(app_backlight(app, 5) == 0 && app_backlight(app, 69000) == 0, "the backlight asked for with a time before the latest seen: no time passed, the screen stays dark");
	remember();
	app_tap(app, 0, 100);
	check(light() == 80 && app->last_input_ms == app->clock_ms && app->nav.last_input_ms == snapshot.nav.last_input_ms && app->clock_ms == snapshot.clock_ms,
	      "a tap with an earlier time on a dark screen only wakes it: the idle time restarts at the time of the app");
	run_to(140000);
	remember();
	app_encoder(app, KNOB_COUNTS_PER_DETENT, 100);
	check(light() == 80 && app->nav.page == 0 && app->last_input_ms == snapshot.clock_ms, "a detent with an earlier time on a dark screen only wakes it");
	run_to(210000);
	remember();
	app_swipe(app, -1, 0, 100);
	check(light() == 80 && app->nav.page == 0 && app->last_input_ms == snapshot.clock_ms, "a swipe with an earlier time on a dark screen only wakes it");

	garage();
	machine.update_pending = true;
	start();
	app_net(app, 100000);
	check(app->clock_ms == 100000, "app_net() takes the time over");
	app_scene(app, &scene, 0);
	check(strcmp(scene.over_lines[2], "sonst alte Version in 3:20") == 0, "the screen asked for with an earlier time shows the time of the app: 200 s are left");
	app_scene(app, &scene, 110000);
	check(strcmp(scene.over_lines[2], "sonst alte Version in 3:10") == 0, "the screen asked for with a later time shows that time");
	check(app->clock_ms == 100000, "looking at the screen does not move the time of the app");
	app_net(app, 300000);
	app_tick(app, 100);
	check(app_take_events(app) == APP_EVENT_REBOOT && app->clock_ms == 300000, "a tick with an earlier time: the time of the update is over all the same");

	// The age of the list and of the engine speed
	scene_list();
	// The engine speed was seen at 7000: at 16999 it is old, at 17000 gone
	app_world(app, &seen, 16999);
	check(seen.can_clear && seen.can_read && app->clock_ms < 7100, "the world asked for at a later time, 9999 ms after the last engine speed: reading and clearing are allowed");
	app_world(app, &seen, 17000);
	check(!seen.can_clear && !seen.can_read, "the world asked for one ms later: the engine speed is gone at that time, nothing is allowed");
	app_scene(app, &scene, 16999);
	check(strcmp(scene.note, "Löschen möglich: 9:50") == 0, "the screen asked for 9999 ms after the last engine speed: clearing possible, with the time that is left then");
	app_scene(app, &scene, 17000);
	check(strcmp(scene.note, "Drehzahl nicht lesbar") == 0, "the screen asked for one ms later says why clearing is not offered at that time");
	long_press();
	app_scene(app, &scene, now + 5000);
	check(scene.note[0] == '\0' && scene.rows[0].enabled, "the fault memory menu asked for 5 s later: reading is allowed, no note");
	app_scene(app, &scene, now + 20000);
	check(strcmp(scene.note, "Drehzahl nicht lesbar") == 0 && !scene.rows[0].enabled, "the fault memory menu asked for 20 s later says why reading is not offered at that time");
	app_tick(app, 606001);
	app_world(app, &seen, 7100);
	check(!seen.can_clear, "the world asked for with an earlier time: the list is as old as the time of the app says");
	drive();
	wican.values = "{}";
	run_to(20000);
	check(view() == CONN_VIEW_LIVE && !can_read(), "the scene of the old engine speed: no value for 18 s, reading is not offered");
	app_do(app, NAV_DO_READ, 2500);
	check(phase() == DTC_FLOW_IDLE, "a read asked for with an earlier time is judged at the time of the app: the engine speed is gone");
	app_scene(app, &scene, 2500);
	check(scene.kind == SCENE_VALUES && strcmp(scene.items[0].text, SCENE_DASH) == 0 && scene.ring.kind == RING_YELLOW,
	      "the screen asked for with an earlier time shows the values as old as the time of the app says: gone, not the fresh ones of then");

	// The screen asked for with a later time: what lies over it, and what the list still allows
	scene_released();
	check(browser_reset() == 1, "the scene of the question: asked at 5460");
	app_scene(app, &scene, now + ACCESS_CONFIRM_MS - 1);
	check(scene.over == SCENE_OVER_ASK && strcmp(scene.over_lines[1], "Drücken = ja · lang = nein (1 s)") == 0, "the screen asked for 1 ms before the question expires shows it with one second left");
	app_scene(app, &scene, now + ACCESS_CONFIRM_MS);
	check(scene.over == SCENE_OVER_NONE && scene.over_line_count == 0, "the screen asked for at the time the question expires does not show it any more");

	// The network task comes with an earlier time
	garage();
	start();
	app_tick(app, 50000);
	link_step();
	check(link_up(&app->link) && app->poll.wifi && app->poll.conn.wifi_since_ms == 50000, "the link comes up with an earlier time: the poll is told the time of the app");
	app_button(app, false, true, 60000);
	check(app->clock_ms == 60000, "app_button() takes the time over");
	app_encoder(app, 0, 61000);
	check(app->clock_ms == 61000, "app_encoder() takes the time over, also without a count");
	app_tap(app, 0, 62000);
	check(app->clock_ms == 62000, "app_tap() takes the time over");
	app_swipe(app, 0, 0, 63000);
	check(app->clock_ms == 63000, "app_swipe() takes the time over");
	app_tick(app, 64000);
	check(app->clock_ms == 64000, "app_tick() takes the time over");
	check(app_backlight(app, 130000) == 0, "the scene of the dark screen: no adapter answers, 66 s after the last input");
	app_swipe(app, 0, 0, 130000);
	check(app->clock_ms == 130000 && app->last_input_ms == 130000, "app_swipe() takes the time over on a dark screen, where it does nothing else");
	app_tap(app, 0, 200000);
	check(app->clock_ms == 200000 && app->last_input_ms == 200000, "app_tap() takes the time over on a dark screen");
	app_encoder(app, KNOB_COUNTS_PER_DETENT, 270000);
	check(app->clock_ms == 270000 && app->last_input_ms == 270000, "app_encoder() takes the time over on a dark screen");
}

static void test_boot(void)
{
	static net_profile_t damaged[NET_PROFILES_MAX];
	static char long_old[POLL_TEXT_SIZE + 64];
	app_boot_t boot;
	size_t length;

	// Nothing at all
	memset(&boot, 0, sizeof(boot));
	boot.work = work;
	boot.work_count = LAYOUT_TOKENS;
	memset(&box, FILL, sizeof(box));
	app_init(app, &boot, 0);
	check(!app->has_builtin && app->source == APP_LAYOUT_GENERATED && app->layout.page_count == 1 && app->version == NULL && app->git == NULL && app->info_count == 13 &&
	      app->profile_count == 0 && app->work == work && app->work_count == LAYOUT_TOKENS, "a start with nothing but the room for the reader: views made from the battery voltage");

	// The members one by one
	memset(&boot, 0, sizeof(boot));
	boot.work = work;
	boot.work_count = LAYOUT_TOKENS;
	boot.version = VERSION;
	boot.git = GIT;
	boot.safe_mode = true;
	app_init(app, &boot, 0);
	check(app->safe_mode && !app->update_pending && !app->previous_firmware && !app->rolled_back && app->link.ap_forced && app->version == boot.version && app->git == boot.git,
	      "the start takes the safe mode over, tells the link, and keeps the texts of version and git as they are");
	boot.safe_mode = false;
	boot.update_pending = true;
	app_init(app, &boot, 0);
	check(!app->safe_mode && app->update_pending && !app->previous_firmware && !app->rolled_back && !app->link.ap_forced, "the start takes over that an update waits for its confirmation");
	boot.update_pending = false;
	boot.previous_firmware = true;
	app_init(app, &boot, 0);
	check(!app->safe_mode && !app->update_pending && app->previous_firmware && !app->rolled_back, "the start takes over that the other slot holds a firmware");
	boot.previous_firmware = false;
	boot.rolled_back = true;
	app_init(app, &boot, 0);
	check(!app->safe_mode && !app->update_pending && !app->previous_firmware && app->rolled_back, "the start takes over that the last update was taken back");

	// The networks
	garage();
	for(int i = 0; i < NET_PROFILES_MAX; i++) snprintf(flash.profiles[i].ssid, NET_SSID_SIZE, "Netz %d", i);
	flash.profile_count = 4;
	start();
	check(app->profile_count == 4 && strcmp(app->profiles[3].ssid, "Netz 3") == 0 && app->link.profile_count == 4 && app->link.phase == LINK_WAITING, "four stored networks are the most: all are taken");
	flash.profile_count = 5;
	start();
	check(app->profile_count == 0 && all_bytes(app->profiles, sizeof(app->profiles), 0) && app->link.phase == LINK_IDLE && app->link.ap_wanted, "a count of five networks is no list: none is taken, the access point is wanted");
	flash.profile_count = -1;
	start();
	check(app->profile_count == 0 && all_bytes(app->profiles, sizeof(app->profiles), 0) && app->link.phase == LINK_IDLE, "a negative count of networks is no list");
	flash.profile_count = 2;
	flash.has_wifi = false;
	start();
	check(app->profile_count == 0 && app->link.phase == LINK_IDLE, "a count without a list is no list");
	flash.has_wifi = true;
	flash.profile_count = 1;
	start();
	check(app->profile_count == 1 && strcmp(app->profiles[0].ssid, "Netz 0") == 0 && all_bytes(&app->profiles[1], 3 * sizeof(net_profile_t), 0), "of a list of one network only that one is taken");

	memset(damaged, 'x', sizeof(damaged));
	memcpy(flash.profiles, damaged, sizeof(damaged));
	flash.profile_count = 2;
	start();
	check(strlen(app->profiles[0].ssid) == NET_SSID_SIZE - 1 && strlen(app->profiles[0].password) == NET_PASSWORD_SIZE - 1 && strlen(app->profiles[0].host) == NET_HOST_SIZE - 1 &&
	      strlen(app->profiles[1].ssid) == NET_SSID_SIZE - 1 && strlen(app->profiles[1].password) == NET_PASSWORD_SIZE - 1 && strlen(app->profiles[1].host) == NET_HOST_SIZE - 1 &&
	      all_bytes(&app->profiles[2], 2 * sizeof(net_profile_t), 0), "stored networks whose texts have no end are cut: each text ends in its field");

	// The settings
	garage();
	strcpy(flash.settings, "{\"night\":40}");
	flash.has_settings = true;
	start();
	check(app->settings.night == 40 && app->settings.brightness == 80 && app->settings.standby_s == 60, "stored settings with one member: the others are the defaults");
	strcpy(flash.settings, "{\"night\":40,\"brightness\":101}");
	start();
	check(app->settings.night == 25 && app->settings.brightness == 80, "stored settings that settings_from_json() refuses: the defaults, all of them");
	strcpy(flash.settings, "");
	start();
	check(app->settings.night == 25 && app->settings.brightness == 80 && !app->settings.reverse && !app->knob.reverse, "an empty text of settings: the defaults");

	// The binding
	garage();
	strcpy(flash.bound, "0123456789abcdef0123456789abcdef");
	start();
	check(strcmp(app->poll.bound_id, "0123456789abcdef0123456789abcdef") == 0 && strcmp(app->poll.conn.bound_id, app->poll.bound_id) == 0, "a stored id of 32 bytes is the binding");
	flash.has_bound = false;
	start();
	check(app->poll.bound_id[0] == '\0', "without a stored id the display is not bound");

	// Catalogue and old list
	garage();
	strcpy(flash.catalog, "{\"X\":1}");
	flash.has_catalog = true;
	strcpy(flash.old, "{\"state\":\"running\"}");
	flash.has_old = true;
	start();
	check(app->poll.catalog.count == 1 && !app->poll.has_old && app->old_lines == 0 && app->source == APP_LAYOUT_BUILTIN && stores() == 0 && strcmp(flash.old, "{\"state\":\"running\"}") == 0,
	      "a stored catalogue and a stored old list that cannot be read count as not stored; the flash is left alone");
	length = strlen(result_read);
	memset(long_old, ' ', sizeof(long_old));
	long_old[sizeof(long_old) - 1] = '\0';
	memcpy(long_old, result_read, length);
	memset(&boot, 0, sizeof(boot));
	boot.work = work;
	boot.work_count = LAYOUT_TOKENS;
	boot.old_text = long_old;
	boot.old_length = POLL_TEXT_SIZE - 1;
	app_init(app, &boot, 0);
	check(app->poll.has_old && app->old_lines == 7 && strlen(app->poll.old_text) == POLL_TEXT_SIZE - 1, "a stored old list of 5199 bytes, the longest the poll has room for, is taken");
	boot.old_length = POLL_TEXT_SIZE;
	app_init(app, &boot, 0);
	check(!app->poll.has_old && app->old_lines == 0, "a stored old list of 5200 bytes is not taken");
	boot.old_length = length - 1;
	app_init(app, &boot, 0);
	check(!app->poll.has_old && app->old_lines == 0, "the length of the old list counts, not its zero: one byte less is no result");
	boot.old_text = NULL;
	boot.catalog_json = stored_catalog;
	boot.catalog_length = strlen(stored_catalog) - 1;
	app_init(app, &boot, 0);
	check(app->poll.catalog.count == 1, "the length of the catalogue counts, not its zero: one byte less is no catalogue");
	boot.catalog_length = strlen(stored_catalog);
	app_init(app, &boot, 0);
	check(app->poll.catalog.count == 4 && app->poll.catalog_guard.has_stored, "the stored catalogue with its length is taken, and guard.h knows what is in the flash");

	// The layouts
	boot.catalog_json = NULL;
	boot.builtin_layout = builtin_text;
	boot.builtin_length = strlen(builtin_text) - 1;
	app_init(app, &boot, 0);
	check(!app->has_builtin && app->builtin_text == NULL && app->source == APP_LAYOUT_GENERATED, "the length of the built-in layout counts: one byte less is no layout, and the views are generated");
	boot.builtin_length = strlen(builtin_text);
	boot.layout_text = stored_layout;
	boot.layout_length = strlen(stored_layout) - 1;
	app_init(app, &boot, 0);
	check(app->has_builtin && !app->has_stored && app->source == APP_LAYOUT_BUILTIN && app->layout_length == strlen(builtin_text), "the length of the stored layout counts: one byte less is no layout");
	boot.layout_length = strlen(stored_layout);
	app_init(app, &boot, 0);
	check(app->has_builtin && app->has_stored && app->source == APP_LAYOUT_STORED && app->layout_length == strlen(stored_layout) && app->layout_text[app->layout_length] == '\0',
	      "the stored layout with its length is taken; its text ends where it ends");
	boot.layout_text = "[]";
	boot.layout_length = 2;
	app_init(app, &boot, 0);
	check(!app->has_stored && app->source == APP_LAYOUT_BUILTIN && all_bytes(&app->stored, sizeof(layout_t), 0), "a stored text that is no layout: the built-in views");

	// The time of the start
	garage();
	wifi.in_range_count = 0;
	start_at(5000);
	check(app->clock_ms == 5000 && app->last_input_ms == 5000 && app->nav.last_input_ms == 5000 && app->link.clock_ms == 5000 && app_backlight(app, 64999) == 80 && app_backlight(app, 65000) == 0,
	      "a start at 5000: the idle time of the standby counts from there");
}

static void test_busy(void)
{
	drive();
	check(!app_busy(app), "a display that shows values is not busy");
	app->uploading = true;
	check(app_busy(app), "busy: a firmware upload runs");
	app->uploading = false;
	app->nav.screen = NAV_DTC_CONFIRM;
	check(app_busy(app), "busy: the clear dialog shows");
	app->nav.screen = NAV_CONFIRM;
	check(!app_busy(app), "not busy: the dialog of the settings shows");
	app->nav.screen = NAV_PAGES;
	app->poll.flow.phase = DTC_FLOW_READ_SENT;
	check(app_busy(app), "busy: a read is to be sent");
	app->poll.flow.phase = DTC_FLOW_READING;
	check(app_busy(app), "busy: a read runs");
	app->poll.flow.phase = DTC_FLOW_CLEAR_SENT;
	check(app_busy(app), "busy: a clear is to be sent");
	app->poll.flow.phase = DTC_FLOW_CLEARING;
	check(app_busy(app), "busy: a clear runs");
	app->poll.flow.phase = DTC_FLOW_LIST;
	check(!app_busy(app), "not busy: a list is shown");
	app->poll.flow.phase = DTC_FLOW_CLEARED;
	check(!app_busy(app), "not busy: an outcome is shown");
	app->poll.flow.phase = DTC_FLOW_FAILED;
	check(!app_busy(app), "not busy: a request failed");
	app->poll.flow.phase = DTC_FLOW_UNKNOWN;
	check(!app_busy(app), "not busy: the outcome of a clear is unknown");
}

// The events the app has raised and nobody has taken yet are the event that waited before and `event`
static bool added(uint32_t event)
{
	return app_take_events(app) == (APP_EVENT_STORE_LAYOUT | event);
}

// Before each of them an event of the web interface waits to be taken: it must not get lost
static void test_events_add_up(void)
{
	drive();
	app->events = APP_EVENT_STORE_LAYOUT;
	app->poll.events = POLL_EVENT_BOUND;
	app_net(app, now);
	check(added(APP_EVENT_STORE_BOUND), "the binding to be stored is added to the events that wait");
	app->events = APP_EVENT_STORE_LAYOUT;
	app->poll.events = POLL_EVENT_CATALOG;
	app_net(app, now);
	check(added(APP_EVENT_STORE_CATALOG), "the catalogue to be stored is added to the events that wait");
	app->events = APP_EVENT_STORE_LAYOUT;
	app->poll.events = POLL_EVENT_OLD;
	app_net(app, now);
	check(added(APP_EVENT_STORE_OLD), "the old list to be stored is added to the events that wait");
	app->events = APP_EVENT_STORE_LAYOUT;
	app->poll.events = POLL_EVENT_BOUND | POLL_EVENT_CATALOG | POLL_EVENT_OLD | POLL_EVENT_LISTS | POLL_EVENT_FORGET;
	app_net(app, now);
	check(added(APP_EVENT_STORE_BOUND | APP_EVENT_STORE_CATALOG | APP_EVENT_STORE_OLD) && app->poll.events == 0, "all events of the poll at once become the three events of the app, and are taken from the poll");

	app->events = APP_EVENT_STORE_LAYOUT;
	app_do(app, NAV_DO_SETTINGS_STORE, now);
	check(added(APP_EVENT_STORE_SETTINGS), "the brightness to be stored is added to the events that wait");
	app->events = APP_EVENT_STORE_LAYOUT;
	app_do(app, NAV_DO_NIGHT_TOGGLE, now);
	check(added(APP_EVENT_STORE_SETTINGS), "the night mode to be stored is added to the events that wait");
	app->events = APP_EVENT_STORE_LAYOUT;
	app_do(app, NAV_DO_REVERSE_TOGGLE, now);
	check(added(APP_EVENT_STORE_SETTINGS), "the direction to be stored is added to the events that wait");
	app->events = APP_EVENT_STORE_LAYOUT;
	app_do(app, NAV_DO_UPDATE_OK, now);
	check(added(APP_EVENT_MARK_VALID), "the confirmed update is added to the events that wait");
	app->events = APP_EVENT_STORE_LAYOUT;
	app_do(app, NAV_DO_REBOOT, now);
	check(added(APP_EVENT_REBOOT), "the restart is added to the events that wait");
	app->events = APP_EVENT_STORE_LAYOUT;
	app_do(app, NAV_DO_FACTORY_RESET, now);
	check(added(APP_EVENT_FACTORY_RESET), "the factory reset is added to the events that wait");
	app->previous_firmware = true;
	app->events = APP_EVENT_STORE_LAYOUT;
	app_do(app, NAV_DO_PREVIOUS_FIRMWARE, now);
	check(added(APP_EVENT_PREVIOUS_FIRMWARE), "the previous firmware is added to the events that wait");

	scene_released();
	browser_wifi("Neu", "passwort1", "");
	run(1500);
	app->events = APP_EVENT_STORE_LAYOUT;
	app_do(app, NAV_DO_ASK_CONFIRM, now);
	check(added(APP_EVENT_STORE_WIFI), "the networks to be stored are added to the events that wait");
	browser_reset();
	run(1500);
	app->events = APP_EVENT_STORE_LAYOUT;
	app_do(app, NAV_DO_ASK_CONFIRM, now);
	check(added(APP_EVENT_FACTORY_RESET), "the confirmed factory reset is added to the events that wait");
	browser_upload_begin("0.2.0");
	browser_upload_end();
	run(1500);
	app->events = APP_EVENT_STORE_LAYOUT;
	app_do(app, NAV_DO_ASK_CONFIRM, now);
	check(added(APP_EVENT_INSTALL_FIRMWARE), "the firmware to be installed is added to the events that wait");

	garage();
	machine.update_pending = true;
	start();
	app->events = APP_EVENT_STORE_LAYOUT;
	app_tick(app, APP_UPDATE_CONFIRM_MS);
	check(added(APP_EVENT_REBOOT), "the restart of an update nobody confirmed is added to the events that wait");
}

static void test_net(void)
{
	drive();
	check(!app->link.no_answer, "the link is told that an adapter with live values answers");
	wican.dead = true;
	run_to(3020);
	check(view() == CONN_VIEW_LIVE && !app->link.no_answer, "a request under way: the adapter still counts as answering");
	run_to(18000);
	check(view() == CONN_VIEW_LIVE && !app->link.no_answer, "two failed rounds: the adapter still counts as answering");
	run_to(18020);
	check(view() == CONN_VIEW_NO_ANSWER && app->link.no_answer && app->link.no_answer_since_ms == 18000, "after the third failed round the link is told that the adapter does not answer");
	wican.dead = false;
	run_to(25000);
	check(view() == CONN_VIEW_LIVE && !app->link.no_answer, "the link is told that the adapter answers again");
	lose_wifi();
	check(view() == CONN_VIEW_NO_WIFI && !app->link.no_answer && !app->poll.wifi, "without a network the link is not told of silence: there is nobody to answer");

	garage();
	wican.dead = true;
	start();
	run(100);
	check(view() == CONN_VIEW_CONNECTING && app->link.no_answer && app->link.no_answer_since_ms == 0, "an adapter that has not answered yet counts as not answering, from the join on");

	// The network goes while the adapter does not answer: there is nobody left whose silence could count
	drive();
	wican.dead = true;
	run_to(18020);
	check(app->link.no_answer, "the scene of the silent adapter: the link knows");
	wifi.in_range_count = 0;
	lose_wifi();
	check(view() == CONN_VIEW_NO_WIFI && !app->link.no_answer, "when the network goes the link is told that nobody is silent any more");

	// One call behind the join: the poll follows first, and the link is told what the poll then shows
	garage();
	start();
	link_step();
	check(link_up(&app->link) && app->poll.wifi && view() == CONN_VIEW_CONNECTING && app->link.no_answer && requests() == 0,
	      "with the call behind the join the poll is in the network and the link is told at once that the adapter has not answered yet");

	// Twice the same
	drive();
	remember();
	app_net(app, app->clock_ms);
	check(unchanged(), "app_net() without anything new changes nothing");
}

/* More refusals and failures on the way of a read and a clear --------------------------------------- */

static void test_more_failures(void)
{
	// A read that finds nothing
	scene_dtc();
	wican.memory = 0;
	wican.read_text = NULL;
	short_press();
	run_to(7100);
	shows("dtc_list_empty", "a read that finds no trouble code: the list says so, and clearing is not offered - there is nothing to clear");
	check(phase() == DTC_FLOW_LIST && app->list_lines == 2 && app->summary.codes == 0 && !can_clear() && can_read(), "an empty list is a list: it can be read again, not cleared");
	turn(3);
	short_press();
	check(on(NAV_DTC_LIST) && !app->hold.open && sent[POLL_DTC_CLEAR] == 0, "with an empty list a short press on Fehler löschen opens no dialog");

	// A profile without a fault memory table
	drive();
	wican.supported = false;
	run_to(3100);
	short_press();
	short_press();
	check(!can_read() && has_line("note: Profil ohne Fehlerspeicher") && has_line("row: > action | Lesen |  | disabled"), "a profile without a fault memory table: reading is not offered, and the menu says why");
	short_press();
	check(on(NAV_DTC) && sent[POLL_DTC_READ] == 0, "a profile without a fault memory table: a short press on Lesen sends nothing");

	// The adapter says done, but does not hand out the result
	scene_dtc();
	wican.no_result = true;
	short_press();
	run_to(6300);
	check(on(NAV_DTC_FAILED) && phase() == DTC_FLOW_FAILED && has_line("line: Ergebnis nicht abrufbar – erneut lesen") && sent[POLL_RESULT] == 1 && !app->poll.has_list && app->list_lines == 0,
	      "a read whose result the adapter does not hand out: asked for once, then the failure says to read again");
	scene_clearing();
	wican.no_result = true;
	run_to(14300);
	shows("unknown", "a clear whose result the adapter does not hand out: nobody knows what was cleared");
	check(done.old == 1 && app->old_lines == 7 && app->cleared_lines == 0, "the list before that clear was stored when the adapter accepted, and is there to look at");

	// A request the adapter accepted and never ends: accepted at 2540, given up by the first state more than 180 s
	// later. From 3000 on the story only waits: a reading and a tick every 200 ms.
	scene_dtc();
	wican.manual = true;
	short_press();
	check(phase() == DTC_FLOW_READING && app->poll.flow.accepted_ms == 2540, "the scene of the read that never ends: accepted at 2540");
	run_to(3000);
	stride = 200;
	run_to(182200);
	check(on(NAV_DTC_BUSY) && phase() == DTC_FLOW_READING && app_busy(app), "a read that is queued for 179.46 s: the display still waits, and no idle time leaves the progress");
	run_to(183400);
	check(on(NAV_DTC_FAILED) && phase() == DTC_FLOW_FAILED && has_line("line: Keine Antwort vom WiCAN") && has_line("ring: progress 0"),
	      "a read that has not ended 180 s after its acceptance is given up; the ring still shows that the adapter has a scan waiting");
	check(!app_busy(app) && sent[POLL_DTC_READ] == 1, "the read that was given up was sent once, and the display is free again");

	// The clear of the scene was accepted at 10600
	scene_clearing();
	wican.manual = true;
	wican.phase = WICAN_DTC_QUEUED;
	run_to(11000);
	stride = 200;
	run_to(190600);
	check(phase() == DTC_FLOW_CLEARING && on(NAV_DTC_BUSY), "a clear that is queued for 179.4 s: the display still waits");
	run_to(191400);
	check(on(NAV_DTC_FAILED) && phase() == DTC_FLOW_UNKNOWN && has_line("title: Stand unbekannt") && has_line("line: Stand des Löschens unbekannt – bitte erneut lesen"),
	      "a clear that has not ended 180 s after its acceptance: nobody knows what was cleared");
	check(sent[POLL_DTC_CLEAR] == 1 && done.old == 1, "the clear that was given up was sent once, and its list stored once");

	// The adapter accepts the clear and refuses it at its own check of the engine
	scene_dialog();
	switch_pressed = true;
	run_to(10620);
	switch_pressed = false;
	check(phase() == DTC_FLOW_CLEARING && done.old == 1 && strcmp(flash.old, result_read) == 0, "the scene of the clear the adapter will refuse: accepted, the list stored");
	wican.rpm = 800;
	run_to(11300);
	check(on(NAV_DTC_FAILED) && phase() == DTC_FLOW_FAILED && has_line("line: Motor läuft – nur bei Motor aus") && !app->poll.has_list && app->list_lines == 0 && done.old == 1 && app->old_lines == 7,
	      "a clear the adapter accepts and then refuses because the engine runs: the failure with its reason; the list stays stored as the one before the last clear");

	// Somebody else starts a scan before the display has seen the end of its own
	scene_dtc();
	wican.manual = true;
	short_press();
	wican.seq = 99;
	wican.http = false;
	run_to(3300);
	check(on(NAV_DTC_FAILED) && has_line("line: Von einem anderen Scan überholt") && has_line("ring: progress 0"), "a read that another scan overtook: the failure says so, and the ring shows that the scan of the other waits");

	// The ignition goes off during the read
	scene_dtc();
	short_press();
	run_to(3500);
	wican.ignition = false;
	run_to(4300);
	check(on(NAV_DTC_FAILED) && has_line("line: Motorsteuergerät offline – Zündung an?") && has_line("ring: grey"), "the ignition goes off during the read: the failure with the reason of the adapter");
}

/* What lies over the clear dialog -------------------------------------------------------------------- */

static void test_dialog_under_question(void)
{
	// The question arrives 1400 ms into the hold
	scene_dialog();
	app_do(app, NAV_DO_RELEASE_ON, now);
	switch_pressed = true;
	run_to(9000);
	check(has_line("permille: 466") && app->hold.holding, "the scene of the hold: 1400 ms of 3000");
	check(browser_reset() == 0 && browser_wifi("Neu", "passwort1", "") == 1 && has_line("over: ask"),
	      "while the clear dialog shows the display is busy for a factory reset; a network can be asked for, and the question lies over the dialog");
	run_to(10700);
	check(sent[POLL_DTC_CLEAR] == 0 && on(NAV_DTC_CONFIRM) && !app->hold.holding && has_line("permille: 0") && phase() == DTC_FLOW_LIST,
	      "a question over the clear dialog breaks the hold: 3100 ms after the press nothing is sent, the ring is empty");
	check(done.wifi == 0 && access_asking(&app->access, now) == ACCESS_ASK_WIFI, "the knob held under the question is no answer to it");
	switch_pressed = false;
	run(60);
	// Pressed from 10760 on: the long press is reported at 11580, the question is gone with it
	switch_pressed = true;
	run(900);
	check(access_ticket(&app->access, 1, now) == ACCESS_TICKET_REFUSED && done.wifi == 0 && on(NAV_DTC_CONFIRM) && app->hold.open,
	      "a long press under the question refuses it, and is no way out of the dialog below");
	run_to(14600);
	check(sent[POLL_DTC_CLEAR] == 0, "the press that refused the question goes on as a hold from the reading behind it: 2980 ms later nothing is sent");
	run_to(14620);
	check(sent[POLL_DTC_CLEAR] == 1 && clear_sent_ms == 14600 && phase() == DTC_FLOW_CLEARING && done.old == 1, "3000 ms after the question went the hold is complete: the clear is sent");
	switch_pressed = false;

	// The answer to the question is given over the dialog
	scene_dialog();
	app_do(app, NAV_DO_RELEASE_ON, now);
	check(browser_wifi("Neu", "passwort1", "") == 1, "the scene of the question over the dialog: asked at 7600, the focus on Löschen");
	run(1500);
	short_press();
	check(done.wifi == 1 && sent[POLL_DTC_CLEAR] == 0 && flash.profile_count == 2, "a short press on Löschen under the question is the answer to the question: the network is stored, nothing is cleared");
	run(300);
	check(on(NAV_DTC_LIST) && !app->hold.open && sent[POLL_DTC_CLEAR] == 0 && phase() == DTC_FLOW_LIST,
	      "the display left its network for the new list: the adapter is out of reach, and the tick closes the dialog that may not clear any more");

	// A firmware upload takes every input, also the hold
	scene_dialog();
	app->uploading = true;
	app->upload_ms = now;
	switch_pressed = true;
	run(3100);
	switch_pressed = false;
	check(sent[POLL_DTC_CLEAR] == 0 && on(NAV_DTC_CONFIRM) && has_line("over: upload") && has_line("permille: 0"), "under a firmware upload the hold does not count");
}

/* The adapter is replaced ---------------------------------------------------------------------------- */

static void test_replaced(void)
{
	// The firmware of the adapter is another one: without the API
	scene_list();
	wican.api = false;
	run_to(8300);
	check(view() == CONN_VIEW_NO_API && on(NAV_DTC) && phase() == DTC_FLOW_IDLE && !app->poll.has_list && app->list_lines == 0 && has_line("note: WiCAN-Firmware ohne Display-API"),
	      "the firmware of the adapter is replaced by one without the API while the list is shown: the list is gone, the menu says why nothing is offered");
	check(info_is(4, "WiCAN-Firmware: –") && app->source == APP_LAYOUT_BUILTIN && app->poll.catalog.count == 36 && stores() == 0, "a firmware without the API names no version; its profile is loaded anew and nothing is stored");
	long_press();
	long_press();
	shows("page_no_api", "the firmware without the API: its values on the first page, the note, a grey ring");
	wican.api = true;
	run_to(40300);
	shows("page_motor", "the firmware with the API is back: asked for it again after 30 s, values as before");

	// Another adapter with another vehicle at the same address, for a display that is not bound
	garage();
	flash.has_bound = false;
	strcpy(wican.id, OTHER);
	wican.config = other_config;
	wican.values = other_values;
	start();
	run(1100);
	check(done.bound == 1 && strcmp(flash.bound, OTHER) == 0 && app->source == APP_LAYOUT_GENERATED && view() == CONN_VIEW_LIVE, "a display that is not bound takes the first adapter that answers, whatever its vehicle");
	strcpy(wican.id, OWN);
	wican.config = w906_config;
	wican.values = NULL;
	adapter_restart(&wican, 4711, 7);
	run_to(3100);
	shows("foreign", "the adapter is replaced by another one: the display stays bound to the first and says so");
	check(done.bound == 1 && strcmp(app->poll.bound_id, OTHER) == 0 && app->poll.values.count == 0, "the binding is not replaced, and the values of the first adapter are forgotten");
}

/* The adapter as it was measured on the vehicle on 2026-10-04 ---------------------------------------- */

static char real_state[1024];
static char real_result[POLL_TEXT_SIZE];
static char real_clear[POLL_TEXT_SIZE];
static char real_config[4096];
static char real_values[2048];
static char real_fw[40];
static char real_id[33];
static uint32_t real_seq;

// The adapter of the test goes on where a state it once answered left off, with the times that were measured:
// 35 s for a scan of 18 control units, 14 steps of the pass counter in a second, 100 ms for an answer
static bool adapter_goes_on(const char *state_text)
{
	wican_state_t state;

	if(!wican_state_parse(state_text, strlen(state_text), &state, work, WICAN_STATE_TOKENS)) return false;

	adapter_init(&wican);
	strcpy(real_id, state.id);
	strcpy(real_fw, state.fw);
	real_seq = state.dtc.seq;
	strcpy(wican.id, state.id);
	wican.fw = real_fw;
	wican.mqtt = "connected";
	wican.boot = state.boot;
	wican.boot_ms = world() - (uint64_t)state.up_s * 1000;
	wican.pass = state.pass;
	wican.pass_ms = world();
	wican.valid = true;
	wican.batt_mv = state.batt_mv;
	wican.phase = state.dtc.phase;
	wican.clear = state.dtc.clear;
	wican.http = state.dtc.from_http;
	wican.seq = state.dtc.seq;
	wican.next_seq = state.dtc.seq + 1;
	wican.step = state.dtc.step;
	wican.ended_ms = world() - (uint64_t)state.dtc.age_s * 1000;
	wican.result_seq = state.dtc.result_seq;
	wican.result_count = state.dtc.count;
	wican.memory = state.dtc.count;
	strcpy(wican.result, real_result);
	wican.config = real_config;
	wican.values = real_values;
	wican.read_text = real_result;
	wican.clear_text = real_clear;
	wican.pass_every_ms = 70;
	wican.scan_ms = 35000;
	wican.steps = 18;
	wican.names = UNITS_W906;
	latency_ms = 100;
	return true;
}

// A display fresh from the factory but for the network of the adapter, which somebody entered: it meets the
// adapter half a minute after somebody else read the fault memory, reads it itself and clears it.
// literal: the texts are the hand-made ones, and the screens are compared with files. Else they are what a real
// adapter answered: only what does not depend on its numbers is checked.
static void real_story(bool literal, const char *name)
{
	char ssid[NET_SSID_SIZE];
	char path[POLL_PATH_SIZE];
	char line[APP_INFO_SIZE];
	bool same;

	factory();
	same = adapter_goes_on(real_state);
	snprintf(what, sizeof(what), "%s: the state of the adapter is one this display reads", name);
	check(same, what);
	if(!same) return;

	snprintf(ssid, sizeof(ssid), "WiCAN_%.26s", real_id);
	strcpy(wifi.in_range[0], ssid);
	wifi.in_range_count = 1;
	wifi.gateway = "192.168.80.1";
	strcpy(flash.profiles[0].ssid, ssid);
	strcpy(flash.profiles[0].password, "ein-eigenes-passwort");
	flash.profile_count = 1;
	flash.has_wifi = true;
	start();
	run(2100);

	snprintf(what, sizeof(what), "%s: in the access point of the adapter the display finds it at the gateway, binds itself to its id and stores that", name);
	check(view() == CONN_VIEW_LIVE && strcmp(app_host(app), "192.168.80.1") == 0 && strcmp(app->poll.bound_id, real_id) == 0 && done.bound == 1 && strcmp(flash.bound, real_id) == 0 && stores() == 1, what);
	snprintf(what, sizeof(what), "%s: the result somebody else read half a minute ago is fetched once and not shown: no list, nothing to clear", name);
	check(sent[POLL_RESULT] == 1 && !app->poll.has_list && app->list_lines == 0 && phase() == DTC_FLOW_IDLE && !can_clear() && can_read(), what);
	snprintf(what, sizeof(what), "%s: the profile has 35 values, and every one of them has arrived", name);
	check(app->poll.catalog.count == 36 && app->poll.values.count == 36 && app->source == APP_LAYOUT_BUILTIN, what);
	snprintf(line, sizeof(line), "WiCAN-Firmware: %s", real_fw);
	snprintf(what, sizeof(what), "%s: the info names the version text of the fork", name);
	check(info_is(4, line), what);
	if(literal) shows("page_motor", "the adapter as measured: its values on the first page");

	// The read: asked for at 2540, accepted at 2640, running from 2940 to 37940
	short_press();
	short_press();
	short_press();
	run_to(3200);
	snprintf(what, sizeof(what), "%s: the read is accepted with the number behind the one the adapter had, and begins with the check of the engine", name);
	check(phase() == DTC_FLOW_READING && app->poll.flow.seq == real_seq + 1 && app->poll.flow.accepted_ms == 2640 && has_line("big: 0/18") && has_line("line: Prüfe Motor …"), what);
	long_press();
	run_to(7200);
	snprintf(what, sizeof(what), "%s: 5 s into the scan the values of before it stand still, dimmed", name);
	check(view() == CONN_VIEW_SCAN && has_line("ring: progress 111") && has_line("note: Live-Werte angehalten (Fehlerspeicher-Scan)") && light() == 80, what);
	if(literal) shows("real_page_scan", "the adapter as measured, 5 s into its scan: the values of before it, dimmed; the ring at 2 of 18");
	run_to(12180);
	snprintf(what, sizeof(what), "%s: 9.98 s after the last values they are still shown", name);
	check(has_line("item: Drehzahl | 0 | 1/min | dim | arc | 0"), what);
	run_to(12200);
	snprintf(what, sizeof(what), "%s: 10 s after the last values they are gone, 25 s before the scan ends", name);
	check(has_line("item: Drehzahl | – |  | dim | arc | -1") && has_line("ring: progress 222"), what);
	if(literal) shows("real_page_gone", "the adapter as measured, 10 s into its scan: dashes instead of values that are 10 s old; the ring at 4 of 18");
	short_press();
	short_press();
	run_to(13200);
	snprintf(what, sizeof(what), "%s: the progress screen at the fifth of 18 control units", name);
	check(on(NAV_DTC_BUSY) && has_line("big: 5/18") && has_line("line: ESP") && has_line("permille: 277"), what);
	if(literal) shows("real_busy", "the adapter as measured: the progress at the fifth of 18 control units, the ESP");
	run_to(38200);
	snprintf(what, sizeof(what), "%s: 35.56 s after the read was accepted the display still waits", name);
	check(phase() == DTC_FLOW_READING && on(NAV_DTC_BUSY), what);
	run_to(38500);
	snprintf(what, sizeof(what), "%s: the list arrives 35.6 s after the read was accepted, with one trouble code in one of 18 control units", name);
	check(on(NAV_DTC_LIST) && app->poll.flow.list_end_ms == 38200 && app->list_lines == 4 && strcmp(app->list[0].text, "1 Fehler") == 0 &&
	      strcmp(app->list[0].detail, "18 Steuergeräte · 35 s") == 0 && strcmp(app->list[3].text, "17 Steuergeräte ohne Fehler") == 0 && app->summary.ecus_with_codes == 1, what);
	snprintf(what, sizeof(what), "%s: the catalogue, which came to rest in the first round, was stored once in the meantime; nothing else was", name);
	check(done.catalog == 1 && flash_catalog_is(36, "ENGINE_RPM", "RPM") && stores() == 2, what);
	snprintf(what, sizeof(what), "%s: the values that follow the result in the same round are newer than the list - clearing is offered at once, for ten minutes", name);
	check(can_clear() && has_line("note: Löschen möglich: 10:00"), what);
	if(literal) shows("real_list", "the adapter as measured: the list with its one code, the control units without one summed up");

	// The clear: confirmed and sent at 42100, accepted at 42200, running from 42500 to 77500
	turn(5);
	short_press();
	if(literal) shows("real_dialog", "the adapter as measured: the clear dialog names one code in one control unit");
	turn(1);
	run_to(39100);
	switch_pressed = true;
	run_to(42120);
	switch_pressed = false;
	snprintf(path, sizeof(path), "/api/dtc?action=clear&seq=%" PRIu32, real_seq + 1);
	snprintf(what, sizeof(what), "%s: the hold of 3 s sends the clear of the own read, in front of the values of that round", name);
	check(sent[POLL_DTC_CLEAR] == 1 && clear_sent_ms == 42100 && strcmp(clear_path, path) == 0 && phase() == DTC_FLOW_CLEAR_SENT && done.old == 0, what);
	run_to(42220);
	snprintf(what, sizeof(what), "%s: the adapter accepts 100 ms later: with that answer the list is stored as the one before the clear, as the text the adapter sent", name);
	check(phase() == DTC_FLOW_CLEARING && app->poll.flow.seq == real_seq + 2 && done.old == 1 && stores() == 3 && strcmp(flash.old, real_result) == 0, what);
	run_to(78500);
	snprintf(what, sizeof(what), "%s: the outcome arrives 36 s after the clear was accepted: one of one cleared", name);
	check(on(NAV_DTC_CLEARED) && phase() == DTC_FLOW_CLEARED && app->cleared_lines == 1 && strcmp(app->cleared[0].text, "Gelöscht 1 von 1") == 0 && strcmp(app->cleared[0].detail, "verbleibend 0") == 0 &&
	      sent[POLL_DTC_CLEAR] == 1 && done.old == 1 && dropped == 0, what);
	if(literal) shows("real_cleared", "the adapter as measured: the outcome of the clear");
	snprintf(what, sizeof(what), "%s: in 78.5 s the display sent 94 requests - 92 questions, one read, one clear - and every one was answered", name);
	check(sent[POLL_DTC_READ] == 1 && sent[POLL_STATE] == 79 && sent[POLL_VALUES] == 9 && sent[POLL_RESULT] == 3 && sent[POLL_CATALOG] == 1 && requests() == 94 &&
	      app->poll.http_ok == 94 && app->poll.http_failed == 0, what);
}

static bool read_file(const char *directory, const char *file, char *text, size_t size)
{
	char path[512];

	return snprintf(path, sizeof(path), "%s/%s", directory, file) < (int)sizeof(path) && read_fixture(path, text, size);
}

static void test_real_adapter(void)
{
	// The answers of the adapter of the vehicle are not in the repository: they name a private device. Who has
	// them runs the story with them as well: WICAN_REAL_ADAPTER=<directory> ./test_app
	const char *directory = getenv("WICAN_REAL_ADAPTER");
	bool loaded = read_fixture("fixtures/app_real_state.json", real_state, sizeof(real_state)) && read_fixture("fixtures/app_real_result.json", real_result, sizeof(real_result)) &&
	              read_fixture("fixtures/app_real_clear.json", real_clear, sizeof(real_clear));

	strcpy(real_config, w906_config);
	strcpy(real_values, w906_values);
	check(loaded, "the hand-made answers in the shape of the measured adapter are there");
	if(loaded) real_story(true, "the adapter as measured");

	if(directory == NULL)
	{
		printf("  WICAN_REAL_ADAPTER is not set: the story is not played with the answers of the real adapter\n");
		return;
	}
	loaded = loaded && read_file(directory, "api_state.json", real_state, sizeof(real_state)) && read_file(directory, "dtc_result.json", real_result, sizeof(real_result)) &&
	         read_file(directory, "car_config.json", real_config, sizeof(real_config)) && read_file(directory, "autopid_data.json", real_values, sizeof(real_values));
	check(loaded, "the answers of the real adapter are there");
	if(loaded) real_story(false, "the real adapter");
}

/* ---------------------------------------------------------------------------------------------------
 * Random runs: the harness driven by a random number generator - inputs at the knob and the screen, a
 * browser, faults of the adapter and of the network, times that step back - with what app.h promises
 * watched around every call. The promises are written here once more from the headers and in another
 * shape than app.c: not what to do with an input, but what holds before and behind it. The modules below
 * app.c are taken as given (their own tests see to them) and asked as oracles: a copy of the knob tells
 * what the knob will report, the flow of the poll which request is under way.
 */

#define RUNS            24
#define RUN_DEEDS       240     // things a run does: an input, a wait, a fault, something the user sets out to do
#define SHOWN           4       // broken promises of each kind that are printed

enum
{
	PROMISE_CLEAR,      // a clear without three seconds of pressed readings on "Löschen", or sent twice
	PROMISE_DANGER,     // restart, factory reset, another firmware or "update in order" by a way the headers do not name
	PROMISE_STORE,      // something that is stored changed without its event, or an event came without its cause
	PROMISE_OLD,        // the list before a clear: stored when the adapter accepted, as the text it sent
	PROMISE_SCENE,      // the screen shows a list, an outcome or a value the modules below do not hold
	PROMISE_LIGHT,      // the backlight is not what settings, standby and heat say
	PROMISE_WAKE,       // an input on a dark screen was passed on, or one on a lit screen was not
	PROMISE_WORLD,      // what nav is told, busy, the host, the time, the hold dialog
	PROMISE_LAYOUT,     // the choice of the layout
	PROMISE_INFO,       // the info lines
	PROMISE_MEMORY,     // a byte outside of the app
	PROMISE_HEAL,       // no way back to live values on the first page
	PROMISES,
};

static const char *const PROMISE_NAMES[PROMISES] = {"clear", "danger", "store", "old list", "scene", "light", "wake", "world", "layout", "info", "memory", "heal"};

#define RESTARTS        (APP_EVENT_REBOOT | APP_EVENT_FACTORY_RESET | APP_EVENT_PREVIOUS_FIRMWARE | APP_EVENT_INSTALL_FIRMWARE)
// The events with one cause each that the run knows before the call
#define FORETOLD        (RESTARTS | APP_EVENT_MARK_VALID | APP_EVENT_STORE_SETTINGS | APP_EVENT_STORE_WIFI | APP_EVENT_STORE_LAYOUT | APP_EVENT_ERASE_LAYOUT)

typedef struct
{
	long steps, touches, web;
	long broken[PROMISES];
	long screens[NAV_CONFIRM + 1];
	long views[CONN_VIEW_LIVE + 1];
	long overlays[NAV_OVER_UPDATE + 1];
	long sources[APP_LAYOUT_PREVIEW + 1];
	long phases[DTC_FLOW_UNKNOWN + 1];
	long raised[12];            // events by their bit
	long kinds[POLL_DTC_CLEAR + 1];
	long holds, clears, withdrawn, lists, outcomes;
	long old_at_once, old_later;
	long dark_inputs, dark_presses, lit_inputs;
	long back, dim, off, standby;
	long starts, healed;
	long values, lines, summaries;
	long reboots_late, confirmed_asks, taps_that_acted;
} run_result_t;

// What the run knows by itself
typedef struct
{
	uint64_t started;           // the time of the start
	uint64_t last_input;        // the last press, detent, tap or swipe
	bool woke;                  // the press that is going on began on a dark screen
	guard_heat_t heat;
	int temp;
	bool has_temp;
	settings_t settings;        // as the flash holds them
	int preview;                // the brightness being set, -1 if none

	bool held;                  // the readings read pressed on "Löschen" without a break ...
	uint64_t held_since;        // ... since this time
	bool hold_done;             // ... for three seconds with the reading of this step
	int confirmed;              // clears that a hold confirmed and that were not handed out yet
	dtc_flow_phase_t phase;     // of the flow, as last seen

	// Around the call that is going on
	uint64_t time;              // the time of the app with it
	nav_t nav;                  // before it
	nav_overlay_t over;
	bool dark;
	bool answered;              // an answer of the adapter was applied
	bool input;                 // it is an input: a detent, a tap, a swipe
	bool turned;                // ... a detent
	nav_t twin;                 // what nav makes of that input, asked of a copy of it
	bool toggles_release;       // the input acts on "Freigabe" of the web screen
	bool was_open;              // the release before it
	bool uploading;             // an upload of the browser runs ...
	uint64_t upload_ms;         // ... and brought something at this time
	knob_event_t knob;          // what the knob will report with the reading
	uint32_t must;              // the events it has to raise, of those in FORETOLD
	uint32_t events;            // the events carried out since it began
	int page;                   // the page before the network task had its turn

	// The list of the own read and its clear
	uint32_t result_seq;        // the last result the adapter handed out, and its text
	char result_text[POLL_TEXT_SIZE];
	uint32_t list_seq;          // the read whose list is shown
	char list_text[POLL_TEXT_SIZE];
	bool clear_out;             // a clear was handed out, and no list was stored for it yet
	bool clear_refused;         // ... and the adapter refused it
	bool accepted;              // ... and the answer that accepts it arrived in this step
	char clear_text[POLL_TEXT_SIZE];

	// What was there after the call before
	char bound[33];
	app_layout_source_t source;
	uint32_t sum;
	char layout_text[LAYOUT_TEXT_MAX + 1];  // the layout of the user, as the browser sent or the flash held it
	bool wifi_good;             // the network the browser asked for is one net_store() takes
} model_t;

static model_t m;
static run_result_t *tally;
static uint32_t run_number;
static const char *doing = "";
static uint32_t dice;
static bool quiet;              // no faults are drawn
static int storm;               // one reading in this many brings a fault
static layout_t builtin_layout;

static void broke(int promise, const char *why)
{
	if(tally->broken[promise]++ < SHOWN)
	{
		printf("  run %u, %s at %" PRIu64 " ms after start %ld: the promise '%s' is broken - %s (screen %d row %d, flow %d, view %d)\n", (unsigned)run_number, doing, now,
		       tally->starts, PROMISE_NAMES[promise], why, (int)app->nav.screen, app->nav.row, (int)phase(), (int)view());
	}
}

// The app has started with what the flash holds
static void model_start(void)
{
	memset(&m, 0, sizeof(m));
	m.started = now;
	m.last_input = now;
	m.heat = GUARD_HEAT_NORMAL;
	m.preview = -1;
	settings_defaults(&m.settings);
	if(flash.has_settings) settings_from_json(&m.settings, flash.settings, strlen(flash.settings), NULL, 0, work, SETTINGS_TOKENS);
	strcpy(m.bound, flash.has_bound ? flash.bound : "");
	m.source = app->source;
	m.sum = app->catalog_sum;
	m.page = app->nav.page;
	if(flash.has_layout) strcpy(m.layout_text, flash.layout);
	// In safe mode the stored layout is not used; every layout the runs store is one that can be read
	if((app->source == APP_LAYOUT_STORED) != (flash.has_layout && !machine.safe_mode)) broke(PROMISE_LAYOUT, "the start does not use the stored layout, or uses it in safe mode");
	tally->starts++;
}

static uint32_t roll(void)
{
	dice ^= dice << 13;
	dice ^= dice >> 17;
	dice ^= dice << 5;
	return dice;
}

// 0 to count - 1
static int pick(int count)
{
	return (int)(roll() % (uint32_t)count);
}

static bool chance(int percent)
{
	return pick(100) < percent;
}

/* What the run knows --------------------------------------------------------------------------------- */

// What lies over the screen at a time (nav.h, the order of it)
static nav_overlay_t over_at(uint64_t time)
{
	if(app->uploading) return NAV_OVER_UPLOAD;
	if(access_asking(&app->access, time) != ACCESS_ASK_NONE) return NAV_OVER_ASK;
	return app->update_pending ? NAV_OVER_UPDATE : NAV_OVER_NONE;
}

// The backlight by the rules of app.h, settings.h and guard.h, from what the flash holds as settings, the
// temperatures the run gave and the inputs it made
static int light_at(uint64_t time)
{
	const values_t *values = &app->poll.values;
	conn_view_t seen = conn_view(&app->poll.conn, time);
	bool showing = !on(NAV_PAGES) || over_at(time) != NAV_OVER_NONE || seen == CONN_VIEW_LIVE || seen == CONN_VIEW_SCAN;
	int percent = m.preview >= 0 ? m.preview : m.settings.night_mode ? m.settings.night : m.settings.brightness;

	for(int i = 0; seen == CONN_VIEW_NO_API && i < values->count; i++)
	{
		if(values_age(&values->items[i], time) != VALUE_AGE_GONE) showing = true;
	}
	if(m.heat == GUARD_HEAT_OFF) return 0;
	if(m.settings.standby_s != 0 && !showing && time - m.last_input >= (uint64_t)m.settings.standby_s * 1000) return 0;
	return m.heat == GUARD_HEAT_DIM && percent > GUARD_DIM_PERCENT ? GUARD_DIM_PERCENT : percent;
}

// The events a short press, or a tap on `row`, has to raise in the state the app is in: the ways the
// headers name for a restart, another firmware, the factory reset, the confirmed update and the stored
// settings and networks
static uint32_t acts(int row, uint64_t time)
{
	uint64_t access_time = time > app->access.clock_ms ? time : app->access.clock_ms;
	access_ask_t asking = access_asking(&app->access, time);

	if(m.over == NAV_OVER_UPLOAD) return 0;
	if(m.over == NAV_OVER_UPDATE) return APP_EVENT_MARK_VALID;
	if(m.over == NAV_OVER_ASK)
	{
		// A press in the first 1500 ms was meant for the screen below
		if(access_time - app->access.asking_since_ms < ACCESS_ASK_SHOWN_MS) return 0;
		if(asking == ACCESS_ASK_FIRMWARE) return APP_EVENT_INSTALL_FIRMWARE;
		if(asking == ACCESS_ASK_RESET) return APP_EVENT_FACTORY_RESET;
		return m.wifi_good ? APP_EVENT_STORE_WIFI : 0;
	}
	if(on(NAV_CONFIRM) && row == 1)
	{
		if(app->nav.confirm == NAV_DO_REBOOT) return APP_EVENT_REBOOT;
		if(app->nav.confirm == NAV_DO_FACTORY_RESET) return APP_EVENT_FACTORY_RESET;
		return app->previous_firmware ? APP_EVENT_PREVIOUS_FIRMWARE : 0;
	}
	// Nachtmodus and Drehrichtung
	if((on(NAV_MENU) && row == 2) || (on(NAV_SETTINGS) && row == 0)) return APP_EVENT_STORE_SETTINGS;
	return 0;
}

// The phase of the flow changed. Only a hold that is complete with this very reading makes it wait to send
// a clear.
static void follow_phase(bool by_hold)
{
	dtc_flow_phase_t seen = phase();

	if(seen == m.phase) return;

	if(seen == DTC_FLOW_CLEAR_SENT)
	{
		if(!by_hold) broke(PROMISE_CLEAR, "a clear waits to be sent that no hold of three seconds on Löschen confirmed");
		m.confirmed = 1;
		tally->holds++;
	}
	else if(m.phase == DTC_FLOW_CLEAR_SENT && m.confirmed != 0)
	{
		// Withdrawn before it was handed out: the network went, or the list became too old
		m.confirmed = 0;
		tally->withdrawn++;
	}
	// The clear did not arrive: nothing is stored for it any more
	if(seen == DTC_FLOW_LIST && m.phase == DTC_FLOW_CLEAR_SENT) m.clear_out = false;
	if(seen == DTC_FLOW_LIST && m.phase == DTC_FLOW_READING) tally->lists++;
	if(seen == DTC_FLOW_CLEARED) tally->outcomes++;
	m.phase = seen;
}

static dtc_line_t fresh[DTC_VIEW_LINES_MAX];

// The lines the poll holds for a screen, made anew: -1 for a screen without such lines
static int fresh_lines(nav_screen_t screen)
{
	const poll_t *poll = &app->poll;

	if(screen == NAV_DTC_LIST) return poll->has_list ? dtc_view_list(&poll->list, fresh, DTC_VIEW_LINES_MAX) : 0;
	if(screen == NAV_DTC_CLEARED) return poll->has_cleared ? dtc_view_cleared(&poll->old, &poll->cleared, fresh, DTC_VIEW_LINES_MAX) : 0;
	if(screen == NAV_DTC_OLD) return poll->has_old ? dtc_view_list(&poll->old, fresh, DTC_VIEW_LINES_MAX) : 0;
	return -1;
}

// The screen shows no list, no outcome, no summary and no value that the poll does not hold right now
static void honest(void)
{
	static scene_t shown;
	const poll_t *poll = &app->poll;
	int count = fresh_lines(app->nav.screen);
	int choices = on(NAV_DTC_LIST) ? 3 : 1;
	char summed[SCENE_TEXT_SIZE];
	dtc_summary_t summary;

	app_scene(app, &shown, now);
	if(count >= 0)
	{
		if(shown.kind != SCENE_LIST || shown.total != count + choices) broke(PROMISE_SCENE, "the rows of the screen are not the lines the poll holds and the choices");
		for(int i = 0; i < shown.row_count && shown.kind == SCENE_LIST; i++)
		{
			const scene_row_t *row = &shown.rows[i];
			int index = shown.first + i;

			if(index >= count)
			{
				if(row->kind != SCENE_ROW_ACTION) broke(PROMISE_SCENE, "a line is shown behind the lines the poll holds");
			}
			else if(row->kind == SCENE_ROW_ACTION || strcmp(row->text, fresh[index].text) != 0 || strcmp(row->detail, fresh[index].detail) != 0)
			{
				broke(PROMISE_SCENE, "a line is shown that is not the line of the result the poll holds");
			}
			else tally->lines++;
		}
	}
	for(int i = 0; i < shown.line_count; i++)
	{
		if(strstr(shown.lines[i], " Fehler in ") == NULL) continue;

		dtc_summarize(&poll->list, &summary);
		snprintf(summed, sizeof(summed), "%" PRIu32 " Fehler in %d %s", summary.codes, summary.ecus_with_codes, summary.ecus_with_codes == 1 ? "Steuergerät" : "Steuergeräten");
		if(!poll->has_list || strcmp(shown.lines[i], summed) != 0) broke(PROMISE_SCENE, "the summary of a list is shown that the poll does not hold");
		else tally->summaries++;
	}
	if(shown.kind == SCENE_VALUES)
	{
		const layout_page_t *page = &app->layout.pages[app->nav.page < 0 || app->nav.page >= app->layout.page_count ? 0 : app->nav.page];

		if(app->nav.page < 0 || app->nav.page >= app->layout.page_count || shown.item_count != page->item_count) broke(PROMISE_SCENE, "the values of a page the layout does not have");
		for(int i = 0; i < shown.item_count && i < page->item_count; i++)
		{
			const value_t *value = values_find(&poll->values, page->items[i].key);
			char text[SCENE_VALUE_SIZE];

			if(strcmp(shown.items[i].text, SCENE_DASH) == 0 || strcmp(shown.items[i].text, SCENE_UNAVAILABLE) == 0) continue;

			if(values_age(value, now) == VALUE_AGE_GONE || !layout_item_text(&page->items[i], value, text, sizeof(text)) || strcmp(shown.items[i].text, text) != 0)
			{
				broke(PROMISE_SCENE, "a value is shown that the poll does not hold, or does not hold any more");
			}
			else tally->values++;
		}
	}
	if(poll->has_list)
	{
		// The list is the one the adapter handed out for the own read
		if(m.list_seq != poll->flow.read_seq)
		{
			m.list_seq = poll->flow.read_seq;
			if(m.result_seq == m.list_seq) strcpy(m.list_text, m.result_text);
			else broke(PROMISE_SCENE, "a list is held that the adapter did not hand out");
		}
		if(strcmp(poll->list_text, m.list_text) != 0) broke(PROMISE_SCENE, "the list held is not the text the adapter sent");
	}
	else m.list_seq = 0;
}

// What nav is told, and the other answers of the app, against what the modules below say right now
static void world_holds(void)
{
	const poll_t *poll = &app->poll;
	dtc_flow_phase_t flow = phase();
	bool under_way = flow == DTC_FLOW_READ_SENT || flow == DTC_FLOW_READING || flow == DTC_FLOW_CLEAR_SENT || flow == DTC_FLOW_CLEARING;
	bool can_read = dtc_flow_read_block(&poll->flow, &poll->conn, &poll->values, &poll->catalog, now) == DTC_FLOW_ALLOWED;
	bool can_clear = dtc_flow_clear_block(&poll->flow, &poll->conn, &poll->values, &poll->catalog, hold_is_stuck(&app->hold), now) == DTC_FLOW_ALLOWED;
	int list = poll->has_list ? dtc_view_list(&poll->list, fresh, DTC_VIEW_LINES_MAX) : 0;
	int cleared = poll->has_cleared ? dtc_view_cleared(&poll->old, &poll->cleared, fresh, DTC_VIEW_LINES_MAX) : 0;
	int old = poll->has_old ? dtc_view_list(&poll->old, fresh, DTC_VIEW_LINES_MAX) : 0;
	nav_world_t seen;

	app_world(app, &seen, now);
	if(seen.layout != &app->layout || seen.catalog != &poll->catalog || seen.flow != flow || seen.can_read != can_read || seen.can_clear != can_clear)
	{
		broke(PROMISE_WORLD, "nav is told another layout, catalogue or flow, or another answer to whether reading and clearing are allowed");
	}
	if(seen.list_lines != list || seen.cleared_lines != cleared || seen.old_lines != old || seen.info_lines != (machine.rolled_back ? 14 : 13))
	{
		broke(PROMISE_WORLD, "nav is told other numbers of lines than the results the poll holds have");
	}
	if(seen.asking != access_asking(&app->access, now) || seen.release_open != access_is_open(&app->access, now) || seen.update_pending != machine.update_pending ||
	   seen.uploading != app->uploading || seen.previous_firmware != machine.previous_firmware)
	{
		broke(PROMISE_WORLD, "nav is told another question, release, update or upload than there is");
	}
	if(seen.night_mode != m.settings.night_mode || seen.brightness != (m.settings.night_mode ? m.settings.night : m.settings.brightness))
	{
		broke(PROMISE_WORLD, "nav is told another brightness than the one stored");
	}
	if(app_busy(app) != (under_way || on(NAV_DTC_CONFIRM) || app->uploading)) broke(PROMISE_WORLD, "busy is not what flow, clear dialog and upload say");
	if(strcmp(app_host(app), link_host(&app->link)) != 0 || app->poll.wifi != link_up(&app->link) || dropped != 0)
	{
		broke(PROMISE_WORLD, "the poll does not follow the link: a request for an adapter without an address");
	}
	if(!all_bytes(box.front, sizeof(box.front), FILL) || !all_bytes(box.behind, sizeof(box.behind), FILL)) broke(PROMISE_MEMORY, "a byte outside of the app was written");
}

static char layout_a[LAYOUT_TEXT_MAX + 1], layout_b[LAYOUT_TEXT_MAX + 1];

// The layout in use is the choice of app.h for the catalogue of the poll, or the one of the user
static void layout_holds(bool stepped)
{
	static layout_t made;
	const catalog_t *catalog = &app->poll.catalog;
	uint32_t sum = catalog_checksum(catalog);
	bool own = app->source == APP_LAYOUT_BUILTIN || app->source == APP_LAYOUT_GENERATED;
	bool builtin = !machine.no_builtin && layout_suits(&builtin_layout, catalog);

	if(app->catalog_sum != sum) broke(PROMISE_LAYOUT, "the check sum the choice was made with is not the one of the catalogue");
	if(stepped && app->source != m.source && (m.source == APP_LAYOUT_STORED || m.source == APP_LAYOUT_PREVIEW))
	{
		broke(PROMISE_LAYOUT, "the layout of the user went by itself: only the web interface takes it away");
	}
	if(sum != m.sum || app->source != m.source)
	{
		if(own)
		{
			if(!builtin) layout_from_catalog(catalog, &made);
			if(app->source != (builtin ? APP_LAYOUT_BUILTIN : APP_LAYOUT_GENERATED)) broke(PROMISE_LAYOUT, "not the built-in views where they suit, or not generated ones where they do not");
			else if(layout_to_json(&app->layout, layout_a, sizeof(layout_a)) < 0 || layout_to_json(builtin ? &builtin_layout : &made, layout_b, sizeof(layout_b)) < 0 ||
			        strcmp(layout_a, layout_b) != 0 || strcmp(app->layout_text, builtin ? builtin_text : layout_b) != 0)
			{
				broke(PROMISE_LAYOUT, "the layout in use or its text is not the one of the choice");
			}
			if(stepped && app->source != m.source && app->nav.page != layout_first_page(&app->layout, catalog)) broke(PROMISE_LAYOUT, "other views do not start at their first page");
			if(stepped && app->source == m.source && app->nav.page != m.page) broke(PROMISE_LAYOUT, "the same views made anew do not keep the page");
		}
		m.sum = sum;
		m.source = app->source;
	}
	m.page = app->nav.page;
	if(!own && strcmp(app->layout_text, m.layout_text) != 0) broke(PROMISE_LAYOUT, "the layout of the user was replaced");
	if(app->layout_length != strlen(app->layout_text)) broke(PROMISE_LAYOUT, "the length of the layout text is not its length");
}

static const char *or_dash(const char *text)
{
	return text != NULL && text[0] != '\0' ? text : "–";
}

// The info lines, written once more with printf from what the run told the app
static void info_holds(void)
{
	static const char *const words[] = {"gespeichert", "eingebaut", "erzeugt", "Vorschau"};
	static char lines[APP_INFO_LINES][160];
	const wican_state_t *state = conn_state(&app->poll.conn);
	int count = 0;
	bool same;

	if(machine.rolled_back) strcpy(lines[count++], "Update nicht übernommen – vorherige Version aktiv");
	if(platform.ssid[0] != '\0') snprintf(lines[count++], sizeof(lines[0]), "WLAN: %s (%d dBm)", platform.ssid, platform.rssi);
	else strcpy(lines[count++], "WLAN: –");
	snprintf(lines[count++], sizeof(lines[0]), "Adresse: %s", or_dash(platform.ip));
	snprintf(lines[count++], sizeof(lines[0]), "WiCAN: %s", or_dash(link_host(&app->link)));
	snprintf(lines[count++], sizeof(lines[0]), "WiCAN-ID: %s", or_dash(flash.has_bound ? flash.bound : ""));
	snprintf(lines[count++], sizeof(lines[0]), "WiCAN-Firmware: %s", or_dash(state != NULL ? state->fw : ""));
	snprintf(lines[count++], sizeof(lines[0]), "Version: %s (%s)", VERSION, platform.slot);
	snprintf(lines[count++], sizeof(lines[0]), "Ansichten: %s (%s)", or_dash(app->layout.name), words[m.source]);
	snprintf(lines[count++], sizeof(lines[0]), "Speicher: %" PRIu32 " frei, min. %" PRIu32, platform.heap, platform.heap_min);
	snprintf(lines[count++], sizeof(lines[0]), "PSRAM: %" PRIu32 " frei, min. %" PRIu32, platform.psram, platform.psram_min);
	snprintf(lines[count++], sizeof(lines[0]), "HTTP: %" PRIu32 " ok, %" PRIu32 " Fehler", app->poll.http_ok, app->poll.http_failed);
	snprintf(lines[count++], sizeof(lines[0]), "Neuverbindungen: %" PRIu32, platform.reconnects);
	if(m.has_temp) snprintf(lines[count++], sizeof(lines[0]), "Temperatur: %d °C", m.temp);
	else strcpy(lines[count++], "Temperatur: –");
	snprintf(lines[count++], sizeof(lines[0]), "Letzter Neustart: %s", platform.reset);

	same = app->info_count == count;
	for(int i = 0; i < APP_INFO_LINES; i++)
	{
		if(app->info_lines[i] != app->info[i] || strcmp(app->info[i], i < count ? lines[i] : "") != 0) same = false;
	}
	if(!same) broke(PROMISE_INFO, "the info lines are not what version, network, adapter, views, memory, counters and temperature say");
}

/* Around every call ---------------------------------------------------------------------------------- */

// Dark by the standby rule, not by the heat: an input only wakes the screen then
static bool dark_at(uint64_t time)
{
	return light_at(time) == 0 && m.heat != GUARD_HEAT_OFF;
}

// A call begins that gives the app the time `time`
static void before(uint64_t time)
{
	m.time = time > given_ms ? time : given_ms;
	m.nav = app->nav;
	m.over = over_at(m.time);
	m.dark = false;
	m.input = false;
	m.knob = KNOB_NONE;
	m.must = 0;
	m.events = 0;
	m.answered = false;
	m.page = app->nav.page;
}

// The call is over, and what it asked for is carried out. quick: a reading between two ticks that the knob
// had nothing to report of, that raised nothing and during which no answer came - only what such a reading
// can change is looked at.
static void behind(bool stepped, bool quick)
{
	const poll_t *poll = &app->poll;

	if(app->clock_ms != given_ms) broke(PROMISE_WORLD, "the time of the app is not the latest it was given");
	if(app->last_input_ms != m.last_input) broke(PROMISE_WAKE, "the idle time does not count from the last press, detent, tap or swipe");
	if(app->hold.open != on(NAV_DTC_CONFIRM)) broke(PROMISE_WORLD, "the hold dialog is not open exactly while the clear dialog shows");
	if(app->events != 0) broke(PROMISE_WORLD, "an event is left behind app_take_events()");
	if(quick && phase() == m.phase && app->nav.screen == m.nav.screen && app->nav.page == m.nav.page && app->nav.row == m.nav.row && app->nav.value == m.nav.value)
	{
		// Every 100 ms
		if(now % 100 == 0 && app_backlight(app, now) != light_at(now)) broke(PROMISE_LIGHT, "the backlight is not what the settings, the standby rule and the heat say between two ticks");
		return;
	}

	follow_phase(false);

	// What had to be raised was, and nothing of that kind besides
	if((m.events & RESTARTS) != (m.must & RESTARTS) || (m.events & APP_EVENT_MARK_VALID) != (m.must & APP_EVENT_MARK_VALID))
	{
		broke(PROMISE_DANGER, "a restart, a factory reset, another firmware or a confirmed update without its cause, or the cause without it");
	}
	if((m.events & FORETOLD & ~RESTARTS & ~APP_EVENT_MARK_VALID) != (m.must & FORETOLD & ~RESTARTS & ~APP_EVENT_MARK_VALID))
	{
		broke(PROMISE_STORE, "settings, networks or a layout to be stored without their cause, or the cause without them");
	}
	for(int bit = 0; bit < 12; bit++)
	{
		if(m.events & (1u << bit)) tally->raised[bit]++;
	}

	// What is stored is what the app holds: nothing changes without its event
	if(m.events & APP_EVENT_STORE_SETTINGS)
	{
		settings_defaults(&m.settings);
		if(!settings_from_json(&m.settings, flash.settings, strlen(flash.settings), NULL, 0, work, SETTINGS_TOKENS)) broke(PROMISE_STORE, "the settings stored cannot be read");
	}
	if(memcmp(&m.settings, &app->settings, sizeof(settings_t)) != 0 || app->knob.reverse != m.settings.reverse) broke(PROMISE_STORE, "the settings in use are not the settings stored");
	if(app->profile_count != flash.profile_count || memcmp(app->profiles, flash.profiles, sizeof(flash.profiles)) != 0 || app->link.profile_count != flash.profile_count)
	{
		broke(PROMISE_STORE, "the networks in use are not the networks stored");
	}
	if(strcmp(poll->bound_id, flash.has_bound ? flash.bound : "") != 0) broke(PROMISE_STORE, "the binding in use is not the binding stored");
	if(((m.events & APP_EVENT_STORE_BOUND) != 0) != (strcmp(poll->bound_id, m.bound) != 0) || ((m.events & APP_EVENT_STORE_BOUND) && (m.bound[0] != '\0' || strcmp(poll->bound_id, wican.id) != 0)))
	{
		broke(PROMISE_STORE, "the binding is stored exactly when a display that is not bound gets the answer of an adapter, as the id of that adapter");
	}
	strcpy(m.bound, poll->bound_id);

	// The list before a clear
	if(poll->has_old != flash.has_old || (poll->has_old && strcmp(poll->old_text, flash.old) != 0)) broke(PROMISE_OLD, "the old list in use is not the one stored");
	if(m.events & APP_EVENT_STORE_OLD)
	{
		if(!m.clear_out || m.clear_refused) broke(PROMISE_OLD, "an old list is stored without a clear that the adapter accepted or may have accepted");
		else if(strcmp(flash.old, m.clear_text) != 0) broke(PROMISE_OLD, "the old list stored is not the list that was shown when its clear was sent");
		if(m.accepted) tally->old_at_once++;
		else tally->old_later++;
		m.clear_out = false;
	}
	else if(m.accepted) broke(PROMISE_OLD, "the adapter accepted a clear, and the list was not stored with that answer");
	m.accepted = false;

	if(app_backlight(app, now) != light_at(now)) broke(PROMISE_LIGHT, "the backlight is not what settings, the value being set, standby and heat say");
	world_holds();
	layout_holds(stepped);
	honest();

	// A restart the app asked for, as the platform and the boot loader carry it out
	if(m.events & RESTARTS)
	{
		done.last = m.events;
		restart_as_asked();
		model_start();
	}
}

static void trouble(void);

static void run_step(void)
{
	knob_t twin = app->knob;
	hold_t hold_twin = app->hold;
	hold_event_t held;
	nav_do_t asked = NAV_DO_NOTHING;
	nav_world_t seen;
	bool focus;

	// A fault can strike between any two readings
	if(!quiet && storm != 0 && pick(storm) == 0) trouble();
	before(screen_time());
	focus = on(NAV_DTC_CONFIRM) && app->nav.row == 1 && m.over == NAV_OVER_NONE;
	m.knob = knob_sample(&twin, switch_pressed, switch_ok, m.time);
	if(!app->knob.pressed && twin.pressed)
	{
		// A press begins: it is an input, and in the dark nothing else
		m.woke = dark_at(m.time);
		m.last_input = m.time;
		if(m.woke) tally->dark_presses++;
	}

	// Three seconds of readings that read pressed, with the focus on "Löschen" and nothing over the dialog
	if(switch_pressed && switch_ok && focus)
	{
		if(!m.held) m.held_since = m.time;
		m.held = true;
	}
	else m.held = false;
	m.hold_done = m.held && m.time - m.held_since >= HOLD_CONFIRM_MS;

	if(m.knob == KNOB_SHORT && !m.woke)
	{
		m.must = acts(app->nav.row, m.time);
		if(m.over == NAV_OVER_NONE && on(NAV_BRIGHTNESS)) m.must |= APP_EVENT_STORE_SETTINGS;
	}
	if(m.knob == KNOB_LONG && !m.woke && m.over == NAV_OVER_NONE && on(NAV_BRIGHTNESS)) m.must |= APP_EVENT_STORE_SETTINGS;
	if(screen_time() < given_ms) tally->back++;

	// What nav makes of the reading, by app.h: first of what the knob reports, unless its press began in the
	// dark, then of what the hold reports - both asked of copies
	app_world(app, &seen, m.time);
	m.twin = app->nav;
	held = hold_sample(&hold_twin, switch_pressed, switch_ok, focus, m.time);
	if(m.knob != KNOB_NONE && !m.woke) asked = m.knob == KNOB_SHORT ? nav_short(&m.twin, &seen, m.time) : nav_long(&m.twin, &seen, m.time);
	if(asked == NAV_DO_HOLD_OPEN && hold_is_stuck(&hold_twin)) nav_hold(&m.twin, HOLD_STUCK, &seen, m.time);
	nav_hold(&m.twin, held, &seen, m.time);
	m.toggles_release = asked == NAV_DO_RELEASE_ON || asked == NAV_DO_RELEASE_OFF;
	m.was_open = seen.release_open;
}

static void run_button(void)
{
	follow_phase(m.hold_done);
	if(m.hold_done) m.held = false;

	// What the knob reports is an input for nav, unless its press began in the dark; a reading without a
	// report is none
	if(m.knob != KNOB_NONE && !m.woke)
	{
		if(app->nav.last_input_ms != m.time) broke(PROMISE_WAKE, "a press on a lit screen was not passed on");
	}
	else if(app->nav.last_input_ms != m.nav.last_input_ms) broke(PROMISE_WAKE, "nav was told of an input that was none, or of a press that began in the dark");
	if(memcmp(&m.twin, &app->nav, sizeof(nav_t)) != 0) broke(PROMISE_WAKE, "what knob and hold reported of the reading was not passed on to nav as app.h says");
	if(m.toggles_release && access_is_open(&app->access, m.time) == m.was_open) broke(PROMISE_WORLD, "a press on Freigabe did not give the release or take it back");

	// The tick of this step: the update nobody confirmed, and the brightness screen that nobody leaves
	if(now % 200 == 0)
	{
		if(app->update_pending && m.time >= m.started + APP_UPDATE_CONFIRM_MS)
		{
			m.must |= APP_EVENT_REBOOT;
			tally->reboots_late++;
		}
		if(on(NAV_BRIGHTNESS) && over_at(m.time) == NAV_OVER_NONE && m.time - app->nav.last_input_ms >= NAV_IDLE_MS) m.must |= APP_EVENT_STORE_SETTINGS;
	}
	m.page = app->nav.page;
}

static void run_tick(void)
{
	info_holds();
	m.page = app->nav.page;

	// Behind the tick nothing is kept of a question that does not wait any more
	if(access_asking(&app->access, m.time) == ACCESS_ASK_NONE && (app->has_wifi_asked || app->ask_detail[0] != '\0' || !all_bytes(&app->wifi_asked, sizeof(app->wifi_asked), 0)))
	{
		broke(PROMISE_WORLD, "what the browser asked for is still kept behind the tick, although no question waits");
	}
	// An upload ends by itself exactly when it has brought nothing for 30 s
	if(m.uploading && m.time > m.upload_ms && m.time - m.upload_ms >= APP_UPLOAD_IDLE_MS) m.uploading = false;
	if(app->uploading != m.uploading) broke(PROMISE_WORLD, "an upload was ended before it had brought nothing for 30 s, or runs on behind that time");
}

static void run_after(void)
{
	// The step that is over began one stride ago
	bool ticked = (now - stride) % 200 == 0;

	if(!on(NAV_BRIGHTNESS)) m.preview = -1;
	behind(true, !ticked && m.knob == KNOB_NONE && m.events == 0 && !m.answered && !m.hold_done);

	tally->steps++;
	tally->screens[app->nav.screen]++;
	tally->views[view()]++;
	tally->overlays[over_at(now)]++;
	tally->sources[app->source]++;
	tally->phases[phase()]++;
	if(m.heat == GUARD_HEAT_DIM) tally->dim++;
	if(m.heat == GUARD_HEAT_OFF) tally->off++;
	if(m.heat != GUARD_HEAT_OFF && light_at(now) == 0) tally->standby++;
}

static void run_touch(touch_t kind, int a, int b)
{
	knob_t twin = app->knob;
	nav_world_t seen;
	nav_do_t asked;
	int detents;

	before(screen_time());
	detents = kind == TOUCH_COUNTS ? knob_turn(&twin, a, m.time) : 0;
	m.input = kind != TOUCH_COUNTS || detents != 0;
	m.turned = kind == TOUCH_COUNTS && m.input;
	m.toggles_release = false;
	if(!m.input) return;

	m.dark = dark_at(m.time);
	m.last_input = m.time;
	if(m.dark)
	{
		tally->dark_inputs++;
		return;
	}
	tally->lit_inputs++;
	// What nav makes of the input, by app.h: detents are a turn, a tap is a tap, a vertical swipe moves the
	// focus of a screen with rows by three, every other swipe is one for the pages
	app_world(app, &seen, m.time);
	m.twin = app->nav;
	if(kind == TOUCH_COUNTS) asked = nav_turn(&m.twin, detents, &seen, m.time);
	else if(kind == TOUCH_TAP) asked = nav_tap(&m.twin, a, &seen, m.time);
	else if(a == 0 && b != 0 && nav_rows(&m.twin, &seen) > 0) asked = nav_turn(&m.twin, b < 0 ? 3 : -3, &seen, m.time);
	else asked = nav_swipe(&m.twin, a < 0 ? 1 : a > 0 ? -1 : 0, &seen, m.time);
	// A dialog the hold refuses is left at once
	if(asked == NAV_DO_HOLD_OPEN && hold_is_stuck(&app->hold)) nav_hold(&m.twin, HOLD_STUCK, &seen, m.time);
	m.toggles_release = asked == NAV_DO_RELEASE_ON || asked == NAV_DO_RELEASE_OFF;
	m.was_open = seen.release_open;
	// A touch and a detent break the hold
	if(on(NAV_DTC_CONFIRM)) m.held = false;
	if(kind == TOUCH_TAP && !(m.over == NAV_OVER_NONE && on(NAV_BRIGHTNESS)))
	{
		m.must = acts(a, m.time);
		if(m.must != 0) tally->taps_that_acted++;
	}
}

static void run_touched(void)
{
	if(m.input && !m.dark)
	{
		if(app->nav.last_input_ms != m.time) broke(PROMISE_WAKE, "an input on a lit screen was not passed on");
		if(memcmp(&m.twin, &app->nav, sizeof(nav_t)) != 0) broke(PROMISE_WAKE, "an input on a lit screen was not passed on to nav as app.h says");
		if(m.toggles_release && access_is_open(&app->access, m.time) == m.was_open) broke(PROMISE_WORLD, "a tap on Freigabe did not give the release or take it back");
		// A detent on the brightness screen sets the brightness at once
		if(m.turned && m.nav.screen == NAV_BRIGHTNESS && m.over == NAV_OVER_NONE) m.preview = app->nav.value;
	}
	else if(memcmp(&m.nav, &app->nav, sizeof(nav_t)) != 0) broke(PROMISE_WAKE, "an input on a dark screen, or counts that made no detent, were passed on");
	if(!on(NAV_BRIGHTNESS)) m.preview = -1;
	behind(false, false);
	tally->touches++;
}

static void run_request(const poll_request_t *asked)
{
	static const char clear[] = "/api/dtc?action=clear&seq=";

	tally->kinds[asked->kind]++;
	if(asked->kind != POLL_DTC_CLEAR) return;

	tally->clears++;
	if(m.confirmed != 1) broke(PROMISE_CLEAR, "a clear is handed out that no hold confirmed, or a second time for one hold");
	if(strncmp(asked->path, clear, strlen(clear)) != 0 || strtoul(asked->path + strlen(clear), NULL, 10) != m.list_seq || m.list_seq == 0)
	{
		broke(PROMISE_CLEAR, "the clear does not name the read whose list is shown");
	}
	m.confirmed = 0;
	m.clear_out = true;
	m.clear_refused = false;
	strcpy(m.clear_text, m.list_text);
}

static void run_answer(const poll_request_t *asked, int status, const char *body, bool waited)
{
	conn_view_t seen = conn_view(&app->poll.conn, app->clock_ms);

	// The catalogue changes with an answer, and the layout follows at once
	layout_holds(true);
	m.answered = true;
	// ... and the link is told whether the adapter answers
	if(app->link.no_answer != (seen == CONN_VIEW_NO_ANSWER || seen == CONN_VIEW_CONNECTING)) broke(PROMISE_WORLD, "the link is not told what the connection shows behind an answer");
	if(!waited) return;

	if(asked->kind == POLL_RESULT && status == 200)
	{
		m.result_seq = wican.result_seq;
		strcpy(m.result_text, body);
	}
	if(asked->kind == POLL_DTC_CLEAR)
	{
		if(status == 202) m.accepted = true;
		else if(status != 0) m.clear_refused = true;
	}
}

static void run_events(uint32_t events)
{
	static catalog_t stored;
	const poll_t *poll = &app->poll;

	m.events |= events;
	// Looked at here, where it was written: the catalogue may change again with the next answer of the step
	if((events & APP_EVENT_STORE_CATALOG) &&
	   (!poll->catalog_complete || !catalog_from_json(&stored, flash.catalog, strlen(flash.catalog), work, CATALOG_TOKENS) || catalog_checksum(&stored) != catalog_checksum(&poll->catalog)))
	{
		broke(PROMISE_STORE, "the catalogue stored is not the complete catalogue of the poll");
	}
}

/* What the run does ---------------------------------------------------------------------------------- */

// What the web interface does is carried out and looked at like a call of the app. raised: the events the
// web interface raised itself (app_web.h).
static void web_begin(const char *name)
{
	doing = name;
	before(now);
}

static void web_end(uint32_t raised)
{
	m.must = raised;
	carry_out();
	if(!on(NAV_BRIGHTNESS)) m.preview = -1;
	behind(false, false);
	tally->web++;
}

// The web interface has told the app that an upload runs and has brought something just now, or that it is over
static void uploads(bool running)
{
	m.uploading = running;
	m.upload_ms = now;
}

// A reading of the chip temperature
static void feel(int celsius, bool valid)
{
	m.heat = guard_heat(m.heat, celsius, valid);
	m.has_temp = valid;
	if(valid) m.temp = celsius;
	app_temperature(app, celsius, valid);
}

// A world in which nothing is wrong: the adapter the display is bound to, awake, with the W906 standing still
static void calm(void)
{
	wican.dead = false;
	wican.api = true;
	strcpy(wican.id, flash.has_bound ? flash.bound : OWN);
	wican.autopid = WICAN_AUTOPID_RUN;
	wican.supported = true;
	wican.ignition = true;
	wican.rpm = 0;
	wican.sleep_in_s = -1;
	wican.refuse = 0;
	wican.lose = false;
	wican.swallow = false;
	wican.no_result = false;
	wican.manual = false;
	wican.config = w906_config;
	wican.values = NULL;
	wifi.join_fails = false;
	wifi.found = "192.168.1.50";
	strcpy(wifi.in_range[0], "Werkstatt");
	strcpy(wifi.in_range[1], "Neu");
	strcpy(wifi.in_range[2], OWN_AP);
	wifi.in_range_count = 3;
	latency_ms = 0;
	net_skew = 0;
	screen_skew = 0;
}

// Something goes wrong, or everything is well again
static void trouble(void)
{
	static const char *const reasons[] = {"busy", "read_required", "stale_seq", "nothing_to_clear", "not_ready", "forbidden", "bad_request"};
	static const int statuses[] = {409, 409, 409, 409, 503, 403, 400};
	static const uint64_t skews[] = {0, 20, 400, 3000, 20000};
	static int turn_of;
	int reason = pick(COUNT(reasons));

	// Every second time all is well again; the faults take turns, so that each of them comes in every run
	if(chance(45))
	{
		calm();
		return;
	}
	switch(turn_of++ % 29)
	{
		case 0: wican.dead = !wican.dead; break;
		case 1: wican.api = !wican.api; break;
		case 2: strcpy(wican.id, strcmp(wican.id, OWN) == 0 ? OTHER : OWN); break;
		case 3: wican.autopid = wican.autopid == WICAN_AUTOPID_RUN ? WICAN_AUTOPID_OFF : WICAN_AUTOPID_RUN; break;
		case 27: wican.autopid = wican.autopid == WICAN_AUTOPID_RUN ? WICAN_AUTOPID_STARTING : WICAN_AUTOPID_RUN; break;
		case 28:
			net_skew = skews[1 + pick(COUNT(skews) - 1)];
			screen_skew = skews[pick(COUNT(skews))];
			break;
		case 4: wican.ignition = !wican.ignition; break;
		case 5: wican.rpm = wican.rpm == 0 ? 800 : 0; break;
		case 6: wican.supported = !wican.supported; break;
		case 7: wican.sleep_in_s = wican.sleep_in_s == 0 ? -1 : 0; break;
		case 8:
			wican.refuse = statuses[reason];
			wican.refuse_reason = reasons[reason];
			break;
		case 9: wican.lose = true; break;
		case 10: wican.swallow = true; break;
		case 11: wican.no_result = !wican.no_result; break;
		case 12:
		case 13: adapter_restart(&wican, wican.boot + 1, (uint32_t)(1 + pick(1000))); break;
		case 14:
			// Another vehicle
			wican.config = wican.config == w906_config ? other_config : w906_config;
			wican.values = wican.config == w906_config ? NULL : other_values;
			if(chance(70)) adapter_restart(&wican, wican.boot + 1, (uint32_t)(1 + pick(1000)));
			break;
		case 15:
			// Somebody else reads the fault memory
			if(wican.phase != WICAN_DTC_QUEUED && wican.phase != WICAN_DTC_RUNNING)
			{
				wican.seq = wican.next_seq++;
				wican.phase = WICAN_DTC_QUEUED;
				wican.clear = false;
				wican.http = chance(50);
				wican.step = 0;
				wican.reason[0] = '\0';
				wican.accepted_ms = world();
			}
			break;
		case 16: wican.manual = !wican.manual; break;
		case 17: latency_ms = 20u * (uint32_t)pick(160); break;
		case 18: if(wifi.joined) lose_wifi(); break;
		case 19: wifi.in_range_count = pick(4); break;
		case 20: wifi.join_fails = !wifi.join_fails; break;
		case 21: wifi.found = wifi.found == NULL ? "192.168.1.50" : NULL; break;
		case 22: net_skew = skews[pick(COUNT(skews))]; break;
		case 23: screen_skew = skews[pick(COUNT(skews))]; break;
		case 24: wican.memory = (uint32_t)pick(4); break;
		case 25:
			// Results made from the trouble codes the vehicle has, or the hand-written ones
			wican.read_text = wican.read_text == NULL ? result_read : NULL;
			wican.clear_text = wican.read_text == NULL ? NULL : result_clear;
			break;
		default: wican.batt_mv = chance(50) ? -1 : 11000 + 100 * pick(40); break;
	}
}

// The focus to a row with the knob, whatever its direction is
static void focus_on(int row)
{
	int detents = row - app->nav.row;

	if(detents != 0) turn(app->settings.reverse ? -detents : detents);
}

// Towards the value pages, as far as the inputs lead
static void home(void)
{
	for(int i = 0; i < 8; i++)
	{
		nav_overlay_t over = over_at(now);

		if(over == NAV_OVER_UPLOAD || (over == NAV_OVER_NONE && on(NAV_PAGES) && light() != 0)) return;

		if(over == NAV_OVER_UPDATE) short_press();
		else if(over == NAV_OVER_NONE && on(NAV_DTC_CONFIRM))
		{
			// Keeping the knob pressed is no way back here
			focus_on(0);
			short_press();
		}
		else long_press();
	}
}

// From the value pages into a row of the menu
static void to_menu(int row)
{
	home();
	short_press();
	if(on(NAV_MENU))
	{
		focus_on(row);
		short_press();
	}
}

// The user answers the question of the browser, or does not
static void answer(void)
{
	int how = pick(100);

	if(how < 15)
	{
		// Too soon: meant for the screen below
		short_press();
		how = pick(100);
	}
	if(how < 55)
	{
		run(ACCESS_ASK_SHOWN_MS + 20u * (uint32_t)pick(20));
		if(chance(50)) short_press();
		else tap(pick(3));
	}
	else if(how < 70) long_press();
	else if(how < 80) run(ACCESS_CONFIRM_MS + 1000);
}

static void wait_long(void)
{
	// Only waiting: a reading and a tick every 200 ms or every second, now and then with a switch that hangs
	while(now % 1000 != 0) step();
	if(chance(60))
	{
		stride = 200;
		run(1000 * (uint64_t)(1 + pick(30)));
	}
	else
	{
		stride = 1000;
		switch_pressed = chance(8);
		run(1000 * (uint64_t)(1 + pick(chance(20) ? 700 : 70)));
		switch_pressed = false;
	}
	stride = STEP_MS;
}

// Waits while the progress of a request shows, at most `rounds` fifths of a second
static void wait_for_scan(int rounds)
{
	for(int i = 0; i < rounds && on(NAV_DTC_BUSY); i++) run(200);
}

// The knob is held in the clear dialog for about three seconds: mostly as it has to be done, and now and
// then in one of the ways that must not clear anything
static void hold_in_dialog(void)
{
	int how = pick(12);

	// 0: the focus stays on "Abbrechen"
	if(how != 0) focus_on(1);
	run(260 + 20u * (uint32_t)pick(10));
	switch_pressed = true;
	run(1500);
	if(how == 1)
	{
		// A reading in the middle fails
		switch_ok = false;
		run(40);
		switch_ok = true;
	}
	else if(how == 2) count(KNOB_COUNTS_PER_DETENT * (app->settings.reverse ? -1 : 1));
	else if(how == 3) tap(1);
	else if(how == 4) swipe(pick(3) - 1, 0);
	run(1440 + 20u * (uint32_t)pick(12));
	switch_pressed = false;
	run(60);
}

// The release for the web interface, given at the knob if it is not open
static void release(void)
{
	if(access_is_open(&app->access, now)) return;

	doing = "release";
	to_menu(3);
	if(on(NAV_WEB) && !access_is_open(&app->access, now))
	{
		focus_on(0);
		short_press();
	}
}

// Something the user sets out to do, done the honest way
static void intent(void)
{
	static const char *const ssids[] = {"Werkstatt", "Neu", OWN_AP};
	static const char *const passwords[] = {"geheim-123", "passwort1", "", "kurz"};
	static const char *const settings[] = {"{\"brightness\":40}", "{\"night_mode\":true,\"night\":10}", "{\"reverse\":true}", "{\"standby_s\":0}", "{\"standby_s\":5,\"night_mode\":false}",
	                                       "{\"brightness\":100,\"reverse\":false,\"standby_s\":60}", "{\"brightness\":4}"};
	int password = pick(5);
	int which = pick(25);
	uint32_t ticket;
	bool ok;

	// Who uses the web interface has mostly given the release before
	if(which >= 5 && which <= 12 && chance(75)) release();

	switch(which)
	{
		case 0:
			doing = "read";
			to_menu(0);
			if(on(NAV_DTC))
			{
				focus_on(0);
				short_press();
			}
			if(chance(70)) wait_for_scan(pick(30));
			break;
		case 1:
		case 2:
			doing = "clear";
			if(!on(NAV_DTC_LIST))
			{
				to_menu(0);
				if(on(NAV_DTC))
				{
					focus_on(1);
					short_press();
				}
			}
			if(on(NAV_DTC_LIST))
			{
				focus_on(app->list_lines + 1);
				short_press();
			}
			if(on(NAV_DTC_CONFIRM)) hold_in_dialog();
			break;
		case 17:
		case 18:
		case 19:
		case 20:
		case 21:
			// The whole way, mostly in a world where nothing is wrong
			doing = "read and clear";
			if(chance(75)) calm();
			to_menu(0);
			if(on(NAV_DTC))
			{
				focus_on(0);
				short_press();
			}
			wait_for_scan(40);
			run(1200);
			if(on(NAV_DTC_LIST))
			{
				focus_on(app->list_lines + 1);
				short_press();
			}
			if(on(NAV_DTC_CONFIRM))
			{
				// What can happen to the request of the clear
				switch(pick(12))
				{
					case 0: wican.swallow = true; break;
					case 1: wican.lose = true; break;
					case 2: wican.refuse = 409; wican.refuse_reason = "stale_seq"; break;
					case 3: latency_ms = 1500; break;
					case 4: wican.rpm = 800; break;
					default: break;
				}
				hold_in_dialog();
				if(phase() == DTC_FLOW_CLEAR_SENT && chance(50) && wifi.joined) lose_wifi();
			}
			if(chance(75)) wait_for_scan(40);
			if(on(NAV_DTC_CLEARED) && chance(60))
			{
				focus_on(app->cleared_lines);
				short_press();
			}
			break;
		case 3:
			doing = "leave the outcome";
			if(on(NAV_DTC_CLEARED))
			{
				focus_on(app->cleared_lines);
				short_press();
			}
			else if(on(NAV_DTC_FAILED)) short_press();
			else
			{
				to_menu(0);
				if(on(NAV_DTC))
				{
					focus_on(1 + pick(2));
					short_press();
				}
			}
			break;
		case 4:
			// On or off
			doing = "release";
			to_menu(3);
			if(on(NAV_WEB))
			{
				focus_on(0);
				short_press();
			}
			break;
		case 5:
			web_begin("the browser asks for a network");
			ticket = browser_wifi(ssids[pick(COUNT(ssids))], password < COUNT(passwords) ? passwords[password] : NULL, chance(50) ? "" : "192.168.1.50");
			// A password of four bytes is one net_store() refuses
			if(ticket != 0) m.wifi_good = password != 3;
			web_end(0);
			if(ticket != 0) answer();
			break;
		case 6:
			web_begin("the browser asks for the factory reset");
			ticket = browser_reset();
			web_end(0);
			if(ticket != 0) answer();
			break;
		case 7:
			web_begin("the browser uploads a firmware");
			ok = browser_upload_begin("0.2.0");
			if(ok) uploads(true);
			web_end(0);
			if(!ok) break;

			run(20u * (uint32_t)pick(100));
			web_begin("the upload goes on");
			browser_upload_progress(pick(101));
			uploads(app->uploading);
			web_end(0);
			switch(pick(5))
			{
				case 0:
				case 1:
				case 2:
					web_begin("the upload is complete");
					ticket = browser_upload_end();
					uploads(false);
					web_end(0);
					if(ticket != 0) answer();
					break;
				case 3:
					web_begin("the upload breaks");
					app->uploading = false;
					uploads(false);
					web_end(0);
					break;
				default:
					// It stalls: the display ends it
					break;
			}
			break;
		case 8:
			web_begin("the browser sends a layout");
			ok = chance(15) ? browser_layout("{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":[]}", false) : browser_layout(stored_layout, chance(50));
			if(ok) strcpy(m.layout_text, stored_layout);
			web_end(ok && app->source == APP_LAYOUT_STORED ? APP_EVENT_STORE_LAYOUT : 0);
			break;
		case 9:
			web_begin("the browser resets the layout");
			ok = browser_layout_reset();
			web_end(ok ? APP_EVENT_ERASE_LAYOUT : 0);
			break;
		case 10:
			web_begin("the browser forgets a network");
			ok = browser_forget(ssids[pick(COUNT(ssids))]);
			web_end(ok ? APP_EVENT_STORE_WIFI : 0);
			break;
		case 11:
			web_begin("the browser sets the settings");
			ok = browser_settings(settings[pick(COUNT(settings))]);
			web_end(ok ? APP_EVENT_STORE_SETTINGS : 0);
			break;
		case 12:
			web_begin("the browser restarts the display");
			ok = browser_reboot();
			web_end(ok ? APP_EVENT_REBOOT : 0);
			break;
		case 13:
		case 23:
		case 24:
			doing = "settings";
			to_menu(5);
			if(on(NAV_SETTINGS))
			{
				// Mostly one of the three rows that ask first
				focus_on(chance(60) ? 2 + pick(3) : pick(NAV_SETTINGS_ROWS));
				short_press();
			}
			if(on(NAV_CONFIRM))
			{
				if(app->nav.confirm == NAV_DO_PREVIOUS_FIRMWARE && access_is_open(&app->access, now) && chance(60))
				{
					// The other slot is overwritten while the dialog is open
					web_begin("an upload under the dialog of the previous version");
					if(browser_upload_begin("0.2.0")) uploads(true);
					web_end(0);
					web_begin("that upload breaks");
					app->uploading = false;
					uploads(false);
					web_end(0);
				}
				if(chance(50))
				{
					focus_on(pick(2));
					short_press();
				}
				else tap(pick(2));
			}
			break;
		case 14:
			doing = "brightness";
			to_menu(1 + pick(2));
			if(on(NAV_BRIGHTNESS))
			{
				turn(pick(9) - 4);
				if(chance(30)) wait_long();
				else if(chance(50)) short_press();
				else long_press();
			}
			break;
		case 15:
			doing = "info";
			to_menu(4);
			turn(pick(15));
			swipe(0, pick(3) - 1);
			break;
		case 16:
			// Nothing to show for a minute and more - the adapter sleeps and takes its network with it, the
			// ignition is off, AutoPID is, another adapter answers, none does, a firmware without the API has no
			// values - and then somebody comes back
			doing = "nothing to show";
			switch(pick(7))
			{
				case 0: wican.ignition = false; break;
				case 1: wican.autopid = WICAN_AUTOPID_OFF; break;
				case 2: wican.autopid = WICAN_AUTOPID_STARTING; break;
				case 3: strcpy(wican.id, strcmp(wican.id, OWN) == 0 ? OTHER : OWN); break;
				case 4: wican.dead = true; break;
				case 5:
					wican.api = false;
					wican.ignition = chance(50);
					break;
				default:
					wifi.in_range_count = 0;
					if(wifi.joined) lose_wifi();
					break;
			}
			home();
			while(now % 1000 != 0) step();
			stride = 1000;
			run(1000u * (uint32_t)(50 + pick(40)));
			stride = STEP_MS;
			doing = "an input after the sleep";
			switch(pick(5))
			{
				case 0: short_press(); break;
				case 1: long_press(); break;
				case 2: turn(pick(5) - 2); break;
				case 3: tap(pick(4)); break;
				default: swipe(pick(3) - 1, pick(3) - 1); break;
			}
			if(chance(60)) calm();
			break;
		default:
			doing = "power off and on";
			machine.safe_mode = chance(15);
			start();
			model_start();
			break;
	}
}

// One thing the run does
static void one(void)
{
	int what_now = pick(100);

	if(what_now < 12)
	{
		doing = "short press";
		press(60 + 20u * (uint32_t)pick(30));
	}
	else if(what_now < 17)
	{
		doing = "long press";
		press(820 + 20u * (uint32_t)pick(60));
	}
	else if(what_now < 33)
	{
		doing = "turn";
		turn(pick(7) - 3);
	}
	else if(what_now < 36)
	{
		doing = "counts";
		count(pick(15) - 7);
	}
	else if(what_now < 44)
	{
		doing = "tap";
		tap(pick(12) - 1);
	}
	else if(what_now < 49)
	{
		doing = "swipe";
		swipe(pick(3) - 1, pick(3) - 1);
	}
	else if(what_now < 52)
	{
		doing = "hold";
		press(1000u * (uint32_t)(1 + pick(4)));
	}
	else if(what_now < 55)
	{
		doing = "readings that fail";
		switch_ok = false;
		switch_pressed = chance(50);
		run(20u * (uint32_t)(1 + pick(60)));
		switch_pressed = false;
		switch_ok = true;
	}
	else if(what_now < 60)
	{
		doing = "wait";
		run(20u * (uint32_t)(1 + pick(100)));
	}
	else if(what_now < 66)
	{
		doing = "long wait";
		wait_long();
	}
	else if(what_now < 71)
	{
		doing = "trouble";
		if(storm != 0) trouble();
	}
	else if(what_now < 73)
	{
		doing = "temperature";
		feel(40 + pick(55), !chance(15));
	}
	else intent();
}

// Live values on the first page: the adapter answers, the first page of the views shows, at least one of
// its values is fresh, nothing lies over it, and the screen is lit
static bool at_home(void)
{
	static scene_t shown;
	int live = 0;

	if(view() != CONN_VIEW_LIVE || !on(NAV_PAGES) || over_at(now) != NAV_OVER_NONE || app->nav.page != layout_first_page(&app->layout, &app->poll.catalog) || light() == 0) return false;

	app_scene(app, &shown, now);
	for(int i = 0; i < shown.item_count; i++)
	{
		if(shown.items[i].tone == SCENE_TONE_NORMAL) live++;
	}
	return shown.kind == SCENE_VALUES && live > 0;
}

// A healthy adapter, honest inputs and time: back to live values on the first page
static bool heal(void)
{
	doing = "the way back";
	quiet = true;
	calm();
	stride = STEP_MS;
	switch_pressed = false;
	switch_ok = true;
	feel(40, true);
	for(int i = 0; i < 60 && !at_home(); i++)
	{
		nav_overlay_t over;

		run(1000);
		over = over_at(now);
		if(over == NAV_OVER_UPLOAD) run(APP_UPLOAD_IDLE_MS + 1000);
		else if(over == NAV_OVER_ASK) long_press();
		else if(over == NAV_OVER_UPDATE) short_press();
		else if(light() == 0) tap(0);
		else if(!on(NAV_PAGES)) home();
		else if(app->profile_count == 0)
		{
			// Without a network nobody reaches the adapter: the user gives the release and enters one
			release();
			web_begin("the browser enters the network again");
			if(browser_wifi("Werkstatt", "geheim-123", "192.168.1.50") != 0) m.wifi_good = true;
			web_end(0);
			run(ACCESS_ASK_SHOWN_MS + 100);
			short_press();
		}
		else if(view() != CONN_VIEW_LIVE) run(30000);
		else if(app->nav.page != layout_first_page(&app->layout, &app->poll.catalog)) long_press();
		// Else the values are on their way: a request that was under way when the adapter came back takes
		// its time
	}
	quiet = false;
	return at_home();
}

static void random_run(uint32_t number, run_result_t *result)
{
	static const char *const settings[] = {"{\"brightness\":60,\"night\":15,\"night_mode\":true,\"reverse\":true,\"standby_s\":0}", "{\"standby_s\":2}", "{\"reverse\":true}"};
	static const int storms[] = {0, 1200, 400, 150};

	run_number = number;
	tally = result;
	dice = number * 2654435761u + 977u;
	doing = "start";
	quiet = false;
	// One run in four without faults, the others with more and more of them
	storm = storms[number % COUNT(storms)];

	// A fifth of the runs begin with a display fresh from the factory, the others with one that was in use
	if(chance(20)) factory();
	else garage();
	calm();
	if(chance(40))
	{
		strcpy(flash.settings, settings[pick(COUNT(settings))]);
		flash.has_settings = true;
	}
	if(chance(30))
	{
		strcpy(flash.layout, stored_layout);
		flash.has_layout = true;
	}
	if(chance(30))
	{
		strcpy(flash.catalog, stored_catalog);
		flash.has_catalog = true;
	}
	if(chance(30))
	{
		strcpy(flash.old, result_read);
		flash.has_old = true;
	}
	machine.update_pending = chance(15);
	machine.previous_firmware = chance(60);
	machine.rolled_back = chance(15);
	machine.no_builtin = chance(10);
	machine.safe_mode = chance(10);

	watch_step = run_step;
	watch_button = run_button;
	watch_tick = run_tick;
	watch_after = run_after;
	watch_touch = run_touch;
	watch_touched = run_touched;
	on_request = run_request;
	on_answer = run_answer;
	watch_events = run_events;
	start();
	model_start();
	if(machine.update_pending && chance(50))
	{
		// Nobody is there to say that the update is in order
		doing = "nobody at the display";
		stride = 1000;
		run(APP_UPDATE_CONFIRM_MS + 1000u * (uint32_t)pick(20));
		stride = STEP_MS;
	}

	for(int deed = 0; deed < RUN_DEEDS; deed++) one();

	if(heal()) result->healed++;
	else broke(PROMISE_HEAL, "a healthy adapter, honest inputs and time do not lead back to live values on the first page");
}

static void test_random_runs(void)
{
	static const char *const promises[PROMISES] = {
		"in every random run a clear is handed out only after 3000 ms of pressed readings on the clear dialog with the focus on Löschen and nothing over it, once per hold, with the number of the list shown",
		"in every random run restart, factory reset, previous and uploaded firmware and the confirmed update are raised exactly by the ways the headers name",
		"in every random run settings, networks, binding and catalogue in use are those stored, and each store event comes exactly with its cause",
		"in every random run the list before a clear is stored exactly when the adapter accepted or may have accepted, with the answer that says so, as the text the adapter sent",
		"in every random run the screen shows no list, outcome, summary or value that the poll does not hold at that moment",
		"in every random run the backlight is what the stored settings, the value being set, the standby rule and the heat say",
		"in every random run the first input on a dark screen only wakes it, and every other input is passed on",
		"in every random run nav is told what the modules below hold, the poll follows the link, the time never runs backwards and the hold dialog is open exactly while the clear dialog shows",
		"in every random run the layout is the built-in one where it suits the catalogue, a generated one where it does not, and the one of the user stays whatever the catalogue does",
		"in every random run the info lines are what the platform, the adapter and the views say at the tick",
		"in every random run no byte outside of the app is written",
		"after every random run a healthy adapter, honest inputs and time lead back to live values on the first page",
	};
	run_result_t result;
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

	// In a child process: a crash or a hang of the app is then a failed check here, not the end of the test
	alarm(400);
	child = fork();
	if(child == 0)
	{
		close(ends[0]);
		alarm(300);
		if(!layout_parse(builtin_text, strlen(builtin_text), &builtin_layout, NULL, work, LAYOUT_TOKENS)) _exit(2);
		for(uint32_t number = 1; number <= RUNS; number++) random_run(number, &result);
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

	printf("  random runs: %ld steps, %ld touches, %ld calls of the web interface, %ld starts, %ld healed; broken promises:", result.steps, result.touches, result.web, result.starts, result.healed);
	for(int i = 0; i < PROMISES; i++) printf(" %s %ld", PROMISE_NAMES[i], result.broken[i]);
	printf("\n  random runs: screens");
	for(int i = 0; i <= NAV_CONFIRM; i++) printf(" %ld", result.screens[i]);
	printf("; views");
	for(int i = 0; i <= CONN_VIEW_LIVE; i++) printf(" %ld", result.views[i]);
	printf("; overlays");
	for(int i = 0; i <= NAV_OVER_UPDATE; i++) printf(" %ld", result.overlays[i]);
	printf("; sources");
	for(int i = 0; i <= APP_LAYOUT_PREVIEW; i++) printf(" %ld", result.sources[i]);
	printf("; phases");
	for(int i = 0; i <= DTC_FLOW_UNKNOWN; i++) printf(" %ld", result.phases[i]);
	printf("; events");
	for(int i = 0; i < 12; i++) printf(" %ld", result.raised[i]);
	printf("; requests");
	for(int i = POLL_STATE; i <= POLL_DTC_CLEAR; i++) printf(" %ld", result.kinds[i]);
	printf("\n  random runs: %ld holds, %ld clears, %ld withdrawn, %ld lists, %ld outcomes, old list stored %ld times with the answer and %ld times later; %ld inputs in the dark, %ld presses "
	       "in the dark, %ld inputs on a lit screen; %ld steps with a time that stepped back, %ld dimmed by heat, %ld dark by heat, %ld dark by standby; %ld values, %ld lines and %ld "
	       "summaries looked at; %ld ticks with an update that was not confirmed in time, %ld taps that acted\n",
	       result.holds, result.clears, result.withdrawn, result.lists, result.outcomes, result.old_at_once, result.old_later, result.dark_inputs, result.dark_presses, result.lit_inputs,
	       result.back, result.dim, result.off, result.standby, result.values, result.lines, result.summaries, result.reboots_late, result.taps_that_acted);

	check(complete && status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0, "24 random runs of 240 deeds each: no crash and no hang");
	for(int i = 0; i < PROMISES; i++) check(complete && result.broken[i] == 0, promises[i]);
	check(complete && result.healed == RUNS, "every one of the 24 random runs ends on live values");

	for(int i = 0; i <= NAV_CONFIRM; i++) reached = reached && result.screens[i] >= 100;
	for(int i = 0; i <= CONN_VIEW_LIVE; i++) reached = reached && result.views[i] >= 100;
	for(int i = 0; i <= NAV_OVER_UPDATE; i++) reached = reached && result.overlays[i] >= 100;
	for(int i = 0; i <= APP_LAYOUT_PREVIEW; i++) reached = reached && result.sources[i] >= 100;
	for(int i = 0; i <= DTC_FLOW_UNKNOWN; i++) reached = reached && result.phases[i] >= 20;
	for(int i = 0; i < 12; i++) reached = reached && result.raised[i] >= 3;
	for(int i = POLL_STATE; i <= POLL_DTC_CLEAR; i++) reached = reached && result.kinds[i] >= 10;
	check(complete && reached, "the random runs reach every screen, every view of the connection, every overlay, every source of the layout, every phase of the flow, every event and every request, in numbers");
	check(complete && result.clears >= 10 && result.holds >= result.clears && result.lists >= 30 && result.outcomes >= 3 && result.old_at_once >= 5 && result.old_later >= 1,
	      "the random runs confirm clears, send them, and store old lists with the accepting answer and behind a lost one");
	check(complete && result.dark_inputs >= 20 && result.dark_presses >= 5 && result.lit_inputs >= 1000 && result.back >= 1000 && result.dim >= 100 && result.off >= 100 && result.standby >= 100,
	      "the random runs make inputs in the dark and on a lit screen, with times that step back, with the backlight limited and switched off by heat and by standby");
	check(complete && result.values >= 1000 && result.lines >= 500 && result.summaries >= 50 && result.reboots_late >= 2 && result.taps_that_acted >= 5 && result.starts >= RUNS + 10,
	      "the random runs look at values, lines and summaries on the screen, restart the display and let an update go unconfirmed");
}


int main(void)
{
	load_fixtures();
	test_constants();
	test_first_start();
	test_stored_start();
	test_pages();
	test_info();
	test_platform();
	test_brightness();
	test_settings();
	test_read_and_clear();
	test_list_and_dialog();
	test_refusals();
	test_blocks();
	test_adapter();
	test_joining();
	test_standby();
	test_heat();
	test_safe_mode();
	test_update();
	test_upload();
	test_release();
	test_questions();
	test_layout_choice();
	test_do();
	test_world();
	test_scene_inputs();
	test_inputs();
	test_clock();
	test_boot();
	test_busy();
	test_net();
	test_events_add_up();
	test_more_failures();
	test_dialog_under_question();
	test_replaced();
	test_real_adapter();
	test_random_runs();

	return test_end();
}
