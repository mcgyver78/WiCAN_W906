/*
 * Host test for display/components/core/web_json.c. Run "make test_web_json && ./test_web_json" in
 * display/test. redproof.py removes or weakens every rule once (mutations/web_json.py) and expects this
 * test to fail.
 */
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "test.h"
#include "web_json.h"

#define GUARD       0xA5
#define ROOM_SIZE   24000
#define BEHIND      64

// Every text is written into this room, which is larger than any room the writer is told about: what it
// writes behind the room it was given lands in guard bytes and is seen, where the address sanitizer would
// only stop the program without naming a rule.
static unsigned char room[ROOM_SIZE + BEHIND];
static char expected[ROOM_SIZE];
static char fixture[ROOM_SIZE];
static char text[ROOM_SIZE];
static json_token_t tokens[4096];

// Two tokens more than the reader is told about show whether it writes behind its room
static json_token_t work[WEB_WIFI_TOKENS + 2];
static json_token_t large_work[512];
static json_token_t huge_work[2000];
static char huge_text[70000];

static uint32_t random_state = 20261004;

static uint32_t rnd(uint32_t below)
{
	random_state = random_state * 1664525u + 1013904223u;
	return (random_state >> 8) % below;
}

// Runs checks in a child process: a crash or a hang of the module is then a failed check here, not the end
// of the test. The checks of the child are printed like all others. Returns false if the child did not come
// to its end.
static bool in_child(void (*checks)(void), const char *what)
{
	int status = 0;
	pid_t child;

	fflush(stdout);
	child = fork();
	if(child == 0)
	{
		// A module that never returns ends the child here
		alarm(120);
		test_failures = 0;
		checks();
		fflush(stdout);
		_exit(test_failures ? 10 : 0);
	}
	if(child < 0 || waitpid(child, &status, 0) != child) status = -1;
	check(status != -1 && WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 10), what);
	if(status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 10) test_failures++;
	return status != -1 && WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 10);
}

/*
 * The writers behind one signature, so that every one of them can be tried with every room
 */

typedef int (*call_t)(char *out, size_t size);

static const web_info_t *info_now;
static struct
{
	const values_t *values;
	const char *view;
	uint64_t now_ms;
} values_now;
static struct
{
	const net_profile_t *profiles;
	int profile_count;
	const char *current;
	const web_seen_t *seen;
	int seen_count;
} wifi_now;
static struct
{
	bool ok;
	const layout_report_t *report;
	const layout_t *layout;
	const catalog_t *catalog;
} report_now;
static struct
{
	uint32_t ticket;
	access_ticket_t state;
	uint32_t left_s;
} ticket_now;
static struct
{
	const char *read;
	uint32_t read_age_s;
	const char *before_clear;
} dtc_now;

static int call_info(char *out, size_t size)
{
	return web_info_json(info_now, out, size);
}

static int call_values(char *out, size_t size)
{
	return web_values_json(values_now.values, values_now.view, values_now.now_ms, out, size);
}

static int call_wifi(char *out, size_t size)
{
	return web_wifi_json(wifi_now.profiles, wifi_now.profile_count, wifi_now.current, wifi_now.seen, wifi_now.seen_count, out, size);
}

static int call_report(char *out, size_t size)
{
	return web_layout_report_json(report_now.ok, report_now.report, report_now.layout, report_now.catalog, out, size);
}

static int call_ticket(char *out, size_t size)
{
	return web_ticket_json(ticket_now.ticket, ticket_now.state, ticket_now.left_s, out, size);
}

static int call_asked(char *out, size_t size)
{
	return web_asked_json(ticket_now.ticket, out, size);
}

static int call_dtc(char *out, size_t size)
{
	return web_dtc_last_json(dtc_now.read, dtc_now.read_age_s, dtc_now.before_clear, out, size);
}

// One call with a room of `size` bytes: what the header says of a text of this length - the length and the
// text with its zero if it fits, else -1 and an empty string, with size 0 nothing, and never a byte from
// out[size] on
static bool room_is_right(call_t call, const char *wanted, size_t size)
{
	size_t length = strlen(wanted);
	size_t window = (size > length + 1 ? size : length + 1) + BEHIND;
	size_t i;
	int result;

	memset(room, GUARD, window);
	result = call((char *)room, size);
	if(size > length)
	{
		if(result != (int)length || memcmp(room, wanted, length + 1) != 0)
		{
			room[size - 1] = '\0';
			printf("  room %lu: %d %.300s\n  expected %lu %.300s\n", (unsigned long)size, result, (char *)room, (unsigned long)length, wanted);
			return false;
		}
	}
	else if(result != -1 || (size > 0 && room[0] != '\0'))
	{
		printf("  room %lu for a text of %lu bytes: %d, first byte %d\n", (unsigned long)size, (unsigned long)length, result, room[0]);
		return false;
	}
	for(i = size; i < window; i++)
	{
		if(room[i] != GUARD)
		{
			printf("  room %lu for a text of %lu bytes: byte %lu was written\n", (unsigned long)size, (unsigned long)length, (unsigned long)i);
			return false;
		}
	}
	return true;
}

// The text in a room that is large enough
static bool gives(call_t call, const char *wanted)
{
	return room_is_right(call, wanted, ROOM_SIZE);
}

// The text with every room from none to 8 bytes more than it needs
static bool every_room(call_t call, const char *wanted)
{
	size_t length = strlen(wanted);
	size_t size;

	for(size = 0; size <= length + 8; size++)
	{
		if(!room_is_right(call, wanted, size)) return false;
	}
	return room_is_right(call, wanted, ROOM_SIZE);
}

// A long text with the rooms around its length
static bool rooms_around(call_t call, const char *wanted)
{
	size_t length = strlen(wanted);
	size_t sizes[] = {0, 1, 2, length / 2, length - 1, length, length + 1, length + 2, ROOM_SIZE};
	size_t i;

	for(i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
	{
		if(!room_is_right(call, wanted, sizes[i])) return false;
	}
	return true;
}

// What the reader of the project says
static bool is_json_object(const char *json)
{
	return json_parse(json, strlen(json), tokens, 4096) > 0 && tokens[0].type == JSON_OBJECT;
}

// A fixture is valid JSON, and the writer gives it byte for byte with every room
static bool gives_fixture(call_t call, const char *path)
{
	return read_fixture(path, fixture, sizeof(fixture)) && is_json_object(fixture) && every_room(call, fixture);
}

static int count_of(const char *haystack, const char *needle)
{
	int count = 0;

	for(haystack = strstr(haystack, needle); haystack != NULL; haystack = strstr(haystack + 1, needle)) count++;
	return count;
}

/*
 * The formats of the header written a second time, with printf, for the comparisons with many inputs. The
 * examples and the fixtures are written by hand.
 */

// A text as the header wants it in an answer: one byte at a time, what it becomes
static const char *q_limited(const char *plain, size_t limit)
{
	static char pool[40][520];
	static int next = 0;
	char *to = pool[next++ % 40];
	size_t at = 0;
	size_t i;

	if(plain == NULL) plain = "";
	to[at++] = '"';
	for(i = 0; i < limit && plain[i] != '\0'; i++)
	{
		unsigned byte = (unsigned char)plain[i];

		if(byte == 0x22) at += (size_t)snprintf(to + at, 8, "\\\"");
		else if(byte == 0x5C) at += (size_t)snprintf(to + at, 8, "\\\\");
		else if(byte <= 0x1F || byte == 0x7F) at += (size_t)snprintf(to + at, 8, "\\u%04x", byte);
		else to[at++] = (char)byte;
	}
	to[at++] = '"';
	to[at] = '\0';
	return to;
}

static const char *q(const char *plain)
{
	return q_limited(plain, 80);
}

static const char *b(bool value)
{
	return value ? "true" : "false";
}

static const char *embedded(const char *json)
{
	return json == NULL || json[0] == '\0' ? "null" : json;
}

static void model_info(const web_info_t *i)
{
	snprintf(expected, sizeof(expected),
	         "{\"project\":\"wican-display\",\"version\":%s,\"git\":%s,\"slot\":%s,\"reset\":%s,\"up\":%lu,\"safe_mode\":%s,"
	         "\"rolled_back\":%s,\"update_pending\":%s,\"heap\":%lu,\"heap_min\":%lu,\"psram\":%lu,\"psram_min\":%lu,\"temp_c\":%d,\"heat\":%s,"
	         "\"release\":{\"open\":%s,\"left_s\":%lu},\"wifi\":{\"ssid\":%s,\"ip\":%s,\"rssi\":%d,\"ap\":%s,\"ap_ssid\":%s},"
	         "\"wican\":{\"host\":%s,\"id\":%s,\"fw\":%s,\"view\":%s},\"layout\":{\"name\":%s,\"source\":%s},"
	         "\"http\":{\"ok\":%lu,\"failed\":%lu,\"reconnects\":%lu},\"settings\":%s}",
	         q(i->version), q(i->git), q(i->slot), q(i->reset), (unsigned long)i->up_s, b(i->safe_mode),
	         b(i->rolled_back), b(i->update_pending), (unsigned long)i->heap, (unsigned long)i->heap_min, (unsigned long)i->psram,
	         (unsigned long)i->psram_min, i->temp_c, q(i->heat),
	         b(i->release_open), (unsigned long)i->release_left_s, q(i->ssid), q(i->ip), i->rssi, b(i->ap_on), q(i->ap_ssid),
	         q(i->wican_host), q(i->wican_id), q(i->wican_fw), q(i->view), q(i->layout_name), q(i->layout_source),
	         (unsigned long)i->http_ok, (unsigned long)i->http_failed, (unsigned long)i->reconnects, embedded(i->settings));
}

static void model_values(const values_t *values, const char *view, uint64_t now_ms)
{
	size_t at = 0;
	bool first = true;
	int i;

	at += (size_t)snprintf(expected + at, sizeof(expected) - at, "{\"view\":%s,\"values\":{", q(view));
	for(i = 0; i < values->count; i++)
	{
		const value_t *value = &values->items[i];
		uint64_t age = now_ms > value->seen_ms ? now_ms - value->seen_ms : 0;
		char shown[40];

		if(age > 9999) continue;
		if(value->kind == VALUE_ON) snprintf(shown, sizeof(shown), "\"on\"");
		else if(value->kind == VALUE_OFF) snprintf(shown, sizeof(shown), "\"off\"");
		else if(value->kind != VALUE_NUMBER || isnan(value->number) || isinf(value->number)) continue;
		else if(value->number == 0) snprintf(shown, sizeof(shown), "0");
		else snprintf(shown, sizeof(shown), "%.9g", value->number);

		at += (size_t)snprintf(expected + at, sizeof(expected) - at, "%s%s:{\"v\":%s,\"age\":\"%s\"}", first ? "" : ",", q(value->name), shown,
		                       age > 2999 ? "old" : "fresh");
		first = false;
	}
	snprintf(expected + at, sizeof(expected) - at, "}}");
}

static void model_wifi(const net_profile_t *profiles, int profile_count, const char *current, const web_seen_t *seen, int seen_count)
{
	size_t at = 0;
	bool first = true;
	int i;

	at += (size_t)snprintf(expected + at, sizeof(expected) - at, "{\"current\":%s,\"profiles\":[", q(current));
	for(i = 0; i < profile_count && profile_count <= 4; i++)
	{
		const net_profile_t *p = &profiles[i];

		at += (size_t)snprintf(expected + at, sizeof(expected) - at, "%s{\"ssid\":%s,\"host\":%s,\"password\":%s,\"factory\":%s,\"wican_ap\":%s}",
		                       i ? "," : "", q_limited(p->ssid, 32), q_limited(p->host, 39), b(p->password[0] != '\0'),
		                       b(memcmp(p->password, "@meatpi#", 9) == 0), b(memcmp(p->ssid, "WiCAN_", 6) == 0 && p->ssid[6] != '\0'));
	}
	at += (size_t)snprintf(expected + at, sizeof(expected) - at, "],\"seen\":[");
	for(i = 0; i < seen_count; i++)
	{
		if(seen[i].ssid[0] == '\0') continue;
		at += (size_t)snprintf(expected + at, sizeof(expected) - at, "%s{\"ssid\":%s,\"rssi\":%d,\"secure\":%s}", first ? "" : ",",
		                       q_limited(seen[i].ssid, 32), seen[i].rssi, b(seen[i].secure));
		first = false;
	}
	snprintf(expected + at, sizeof(expected) - at, "]}");
}

// The unknown keys are found by keeping a list of the keys met so far
static void model_report(bool ok, const layout_report_t *report, const layout_t *layout, const catalog_t *catalog)
{
	const char *met[LAYOUT_PAGES_MAX * LAYOUT_ITEMS_MAX];
	int met_count = 0;
	int items = 0;
	bool loaded = false;
	bool first = true;
	size_t at = 0;
	int p, i, k;

	if(!ok)
	{
		snprintf(expected, sizeof(expected), "{\"ok\":false,\"path\":%s,\"problem\":%s}", q(report->path), q(report->problem));
		return;
	}
	for(p = 0; p < layout->page_count; p++) items += layout->pages[p].item_count;
	for(k = 0; k < catalog->count; k++)
	{
		if(strcmp(catalog->entries[k].name, "@BATT_V") != 0) loaded = true;
	}
	at += (size_t)snprintf(expected + at, sizeof(expected) - at, "{\"ok\":true,\"name\":%s,\"pages\":%d,\"items\":%d,\"warnings\":%d,\"warning_path\":%s,"
	                       "\"warning\":%s,\"unknown\":[", q(layout->name), (int)layout->page_count, items, report->warnings, q(report->warning_path),
	                       q(report->warning));
	for(p = 0; p < layout->page_count && loaded; p++)
	{
		for(i = 0; i < layout->pages[p].item_count; i++)
		{
			const char *key = layout->pages[p].items[i].key;
			bool known = false;

			for(k = 0; k < met_count; k++)
			{
				if(strcmp(met[k], key) == 0) known = true;
			}
			met[met_count++] = key;
			for(k = 0; k < catalog->count; k++)
			{
				if(strcmp(catalog->entries[k].name, key) == 0) known = true;
			}
			if(known) continue;
			at += (size_t)snprintf(expected + at, sizeof(expected) - at, "%s%s", first ? "" : ",", q(key));
			first = false;
		}
	}
	snprintf(expected + at, sizeof(expected) - at, "]}");
}

/*
 * Texts
 */

static void test_constants(void)
{
	check(WEB_VALUES_SIZE == 15360, "WEB_VALUES_SIZE is 15360");
	check(WEB_REPORT_SIZE == 15360, "WEB_REPORT_SIZE is 15360");
	check(WEB_WIFI_TOKENS == 16, "WEB_WIFI_TOKENS is 16");
	check(sizeof(((web_seen_t *)0)->ssid) == 33 && sizeof(((web_wifi_request_t *)0)->ssid) == 33 &&
	      sizeof(((web_wifi_request_t *)0)->password) == 65 && sizeof(((web_wifi_request_t *)0)->host) == 40,
	      "SSIDs have room for 32 bytes, passwords for 64, hosts for 39");
}

// Through the simplest answer with a text: {"current":"..","profiles":[],"seen":[]}
static bool current_gives(const char *current, const char *written)
{
	char wanted[2048];

	wifi_now.profiles = NULL;
	wifi_now.profile_count = 0;
	wifi_now.current = current;
	wifi_now.seen = NULL;
	wifi_now.seen_count = 0;
	snprintf(wanted, sizeof(wanted), "{\"current\":\"%s\",\"profiles\":[],\"seen\":[]}", written);
	return every_room(call_wifi, wanted);
}

static void test_escaping(void)
{
	char all[256];
	char back[300];
	size_t total = 0;
	bool same = true, lengths = true, returned = true;
	int byte, current;

	check(current_gives("Werkstatt", "Werkstatt"), "escaping: letters are passed on");
	check(current_gives("a\"b", "a\\\"b"), "escaping: a quote is written with a backslash");
	check(current_gives("a\\b", "a\\\\b"), "escaping: a backslash is written with a backslash");
	check(current_gives("\"\\\"\\", "\\\"\\\\\\\"\\\\"), "escaping: quotes and backslashes one after the other");
	check(current_gives("\x01", "\\u0001"), "escaping: the byte 0x01 is written as \\u0001");
	check(current_gives("a\x1f" "b", "a\\u001fb"), "escaping: the byte 0x1F is written as \\u001f, with a small letter");
	check(current_gives("\n\t\r\b\f", "\\u000a\\u0009\\u000d\\u0008\\u000c"), "escaping: line break, tab and the like are written as \\u00xx, not as \\n");
	check(current_gives("\x1a\x0b\x1e", "\\u001a\\u000b\\u001e"), "escaping: the hexadecimal digits are small letters");
	check(current_gives("\x7f", "\\u007f"), "escaping: the byte 0x7F is written as \\u007f");
	check(current_gives("\x10\x11", "\\u0010\\u0011"), "escaping: the bytes 0x10 and 0x11 keep both digits");
	check(current_gives(" ~", " ~"), "escaping: the bytes 0x20 and 0x7E are passed on");
	check(current_gives("a/b'c", "a/b'c"), "escaping: slash and apostrophe are passed on");
	check(current_gives("K\xc3\xbchlwasser \xc2\xb0" "C", "K\xc3\xbchlwasser \xc2\xb0" "C"), "escaping: UTF-8 is passed on");
	check(current_gives("\x80\xff\xc3", "\x80\xff\xc3"), "escaping: bytes that are no UTF-8 are passed on as they are");
	check(current_gives("", ""), "escaping: an empty text");
	check(current_gives(NULL, ""), "escaping: a text pointer that is NULL counts as an empty text");

	// Every byte value alone, against the rule written down once more, and back through the reader of the project
	for(byte = 1; byte <= 255; byte++)
	{
		char one[2] = {(char)byte, '\0'};
		size_t wanted_length = byte == 0x22 || byte == 0x5C ? 2 : byte <= 0x1F || byte == 0x7F ? 6 : 1;
		int length;

		wifi_now.current = one;
		snprintf(expected, sizeof(expected), "{\"current\":%s,\"profiles\":[],\"seen\":[]}", q(one));
		if(!gives(call_wifi, expected)) same = false;
		length = call_wifi((char *)room, ROOM_SIZE);
		// The frame has 38 bytes
		if(length != (int)(38 + wanted_length)) lengths = false;
		total += (size_t)length - 38;
		current = json_parse((char *)room, (size_t)length, tokens, 4096) > 0 ? json_member((char *)room, tokens, 0, "current") : -1;
		if(current < 0 || !json_text((char *)room, &tokens[current], back, sizeof(back)) || strcmp(back, one) != 0) returned = false;
	}
	check(same, "escaping: every byte value from 1 to 255 is written as the rule says");
	check(lengths && total == 32 * 6 + 2 * 2 + 221, "escaping: 32 byte values become six bytes, two become two, 221 stay one");
	check(returned, "escaping: every byte value comes back from the JSON reader of the project as it was");

	for(byte = 1; byte <= 255; byte++) all[byte - 1] = (char)byte;
	all[255] = '\0';
	wifi_now.current = all;
	snprintf(expected, sizeof(expected), "{\"current\":%s,\"profiles\":[],\"seen\":[]}", q_limited(all, 255));
	check(strlen(expected) == 38 + 417 && every_room(call_wifi, expected), "escaping: a text of all 255 byte values, 417 bytes when written");
	current = json_parse(expected, strlen(expected), tokens, 4096) > 0 ? json_member(expected, tokens, 0, "current") : -1;
	check(current > 0 && json_text(expected, &tokens[current], back, sizeof(back)) && strcmp(back, all) == 0,
	      "escaping: the text of all byte values comes back from the JSON reader as it was");

	// A text far longer than any field: 1000 letters, a quote, 999 letters, a line break, 17998 letters with a blank
	// among them, a backslash
	memset(text, 'a', 20000);
	text[1000] = '"';
	text[2000] = '\n';
	text[3000] = ' ';
	text[19999] = '\\';
	text[20000] = '\0';
	memset(expected, 'a', 20100);
	memcpy(expected, "{\"current\":\"", 12);
	memcpy(expected + 12 + 1000, "\\\"", 2);
	memcpy(expected + 12 + 1000 + 2 + 999, "\\u000a", 6);
	expected[12 + 1000 + 2 + 999 + 6 + 999] = ' ';
	strcpy(expected + 12 + 1000 + 2 + 999 + 6 + 17998, "\\\\\",\"profiles\":[],\"seen\":[]}");
	wifi_now.current = text;
	check(strlen(expected) == 38 + 20000 + 1 + 5 + 1 && is_json_object(expected) && rooms_around(call_wifi, expected),
	      "escaping: a text of 20000 bytes is written completely, 20045 bytes");
}

/*
 * GET /api/info
 */

static const web_info_t typical_info = {
	.version = "0.1.0", .git = "display-v0.1.0-3-g1a2b3c4", .slot = "ota_0", .reset = "poweron", .up_s = 4711,
	.safe_mode = false, .rolled_back = false, .update_pending = true,
	.heap = 182340, .heap_min = 151200, .psram = 7340032, .psram_min = 7100416, .temp_c = 47, .heat = "normal",
	.release_open = true, .release_left_s = 540,
	.ssid = "Werkstatt", .ip = "192.168.1.77", .rssi = -61, .ap_on = false, .ap_ssid = "WiCAN-Display",
	.wican_host = "192.168.1.50", .wican_id = "a1b2c3d4e5f6", .wican_fw = "4.21", .view = "live",
	.layout_name = "W906 OM651 Standard", .layout_source = "stored",
	.http_ok = 12345, .http_failed = 7, .reconnects = 2,
	.settings = "{\"brightness\":80,\"night\":25,\"night_mode\":false,\"reverse\":false,\"standby_s\":60}",
};

static const web_info_t safe_mode_info = {
	.version = "0.2.0-rc1", .git = "", .slot = "ota_1", .reset = "panic", .up_s = 0,
	.safe_mode = true, .rolled_back = true, .update_pending = false,
	.heap = 4294967295u, .heap_min = 0, .psram = 0, .psram_min = 0, .temp_c = -12, .heat = "off",
	.release_open = false, .release_left_s = 0,
	.ssid = NULL, .ip = "", .rssi = 0, .ap_on = true, .ap_ssid = "WiCAN-Display \"Bus\"",
	.wican_host = "", .wican_id = NULL, .wican_fw = "", .view = "no_wifi",
	.layout_name = "K\xc3\xbchlwasser & \xc3\x96l", .layout_source = "builtin",
	.http_ok = 0, .http_failed = 0, .reconnects = 0,
	.settings = NULL,
};

// An info with no text, every number 0 and every boolean false
static const char empty_info_text[] =
	"{\"project\":\"wican-display\",\"version\":\"\",\"git\":\"\",\"slot\":\"\",\"reset\":\"\",\"up\":0,\"safe_mode\":false,"
	"\"rolled_back\":false,\"update_pending\":false,\"heap\":0,\"heap_min\":0,\"psram\":0,\"psram_min\":0,\"temp_c\":0,\"heat\":\"\","
	"\"release\":{\"open\":false,\"left_s\":0},\"wifi\":{\"ssid\":\"\",\"ip\":\"\",\"rssi\":0,\"ap\":false,\"ap_ssid\":\"\"},"
	"\"wican\":{\"host\":\"\",\"id\":\"\",\"fw\":\"\",\"view\":\"\"},\"layout\":{\"name\":\"\",\"source\":\"\"},"
	"\"http\":{\"ok\":0,\"failed\":0,\"reconnects\":0},\"settings\":null}";

static void test_info(void)
{
	static const struct
	{
		size_t offset;
		const char *where;
		const char *what;
	} texts[] = {
		{offsetof(web_info_t, version), "\"project\":\"wican-display\",\"version\":\"a\\\"MARK\",\"git\"", "info: version stands at its place and is escaped"},
		{offsetof(web_info_t, git), "\"version\":\"\",\"git\":\"a\\\"MARK\",\"slot\"", "info: git stands at its place and is escaped"},
		{offsetof(web_info_t, slot), "\"git\":\"\",\"slot\":\"a\\\"MARK\",\"reset\"", "info: slot stands at its place and is escaped"},
		{offsetof(web_info_t, reset), "\"slot\":\"\",\"reset\":\"a\\\"MARK\",\"up\"", "info: reset stands at its place and is escaped"},
		{offsetof(web_info_t, heat), "\"temp_c\":0,\"heat\":\"a\\\"MARK\",\"release\"", "info: heat stands at its place and is escaped"},
		{offsetof(web_info_t, ssid), "\"wifi\":{\"ssid\":\"a\\\"MARK\",\"ip\"", "info: wifi.ssid stands at its place and is escaped"},
		{offsetof(web_info_t, ip), "\"ssid\":\"\",\"ip\":\"a\\\"MARK\",\"rssi\"", "info: wifi.ip stands at its place and is escaped"},
		{offsetof(web_info_t, ap_ssid), "\"ap\":false,\"ap_ssid\":\"a\\\"MARK\"},\"wican\"", "info: wifi.ap_ssid stands at its place and is escaped"},
		{offsetof(web_info_t, wican_host), "\"wican\":{\"host\":\"a\\\"MARK\",\"id\"", "info: wican.host stands at its place and is escaped"},
		{offsetof(web_info_t, wican_id), "\"host\":\"\",\"id\":\"a\\\"MARK\",\"fw\"", "info: wican.id stands at its place and is escaped"},
		{offsetof(web_info_t, wican_fw), "\"id\":\"\",\"fw\":\"a\\\"MARK\",\"view\"", "info: wican.fw stands at its place and is escaped"},
		{offsetof(web_info_t, view), "\"fw\":\"\",\"view\":\"a\\\"MARK\"},\"layout\"", "info: wican.view stands at its place and is escaped"},
		{offsetof(web_info_t, layout_name), "\"layout\":{\"name\":\"a\\\"MARK\",\"source\"", "info: layout.name stands at its place and is escaped"},
		{offsetof(web_info_t, layout_source), "\"name\":\"\",\"source\":\"a\\\"MARK\"},\"http\"", "info: layout.source stands at its place and is escaped"},
	};
	static const struct
	{
		size_t offset;
		const char *where;
		const char *what;
	} numbers[] = {
		{offsetof(web_info_t, up_s), "\"reset\":\"\",\"up\":4294967295,\"safe_mode\"", "info: up stands at its place with all 32 bit"},
		{offsetof(web_info_t, heap), "\"update_pending\":false,\"heap\":4294967295,\"heap_min\":0", "info: heap stands at its place with all 32 bit"},
		{offsetof(web_info_t, heap_min), "\"heap\":0,\"heap_min\":4294967295,\"psram\":0", "info: heap_min stands at its place with all 32 bit"},
		{offsetof(web_info_t, psram), "\"heap_min\":0,\"psram\":4294967295,\"psram_min\":0", "info: psram stands at its place with all 32 bit"},
		{offsetof(web_info_t, psram_min), "\"psram\":0,\"psram_min\":4294967295,\"temp_c\":0", "info: psram_min stands at its place with all 32 bit"},
		{offsetof(web_info_t, release_left_s), "\"release\":{\"open\":false,\"left_s\":4294967295},\"wifi\"", "info: release.left_s stands at its place with all 32 bit"},
		{offsetof(web_info_t, http_ok), "\"http\":{\"ok\":4294967295,\"failed\":0", "info: http.ok stands at its place with all 32 bit"},
		{offsetof(web_info_t, http_failed), "\"ok\":0,\"failed\":4294967295,\"reconnects\":0", "info: http.failed stands at its place with all 32 bit"},
		{offsetof(web_info_t, reconnects), "\"failed\":0,\"reconnects\":4294967295},\"settings\"", "info: http.reconnects stands at its place with all 32 bit"},
	};
	static const struct
	{
		size_t offset;
		const char *where;
		const char *what;
	} flags[] = {
		{offsetof(web_info_t, safe_mode), "\"up\":0,\"safe_mode\":true,\"rolled_back\":false", "info: safe_mode alone is true at its place"},
		{offsetof(web_info_t, rolled_back), "\"safe_mode\":false,\"rolled_back\":true,\"update_pending\":false", "info: rolled_back alone is true at its place"},
		{offsetof(web_info_t, update_pending), "\"rolled_back\":false,\"update_pending\":true,\"heap\"", "info: update_pending alone is true at its place"},
		{offsetof(web_info_t, release_open), "\"release\":{\"open\":true,\"left_s\"", "info: release.open alone is true at its place"},
		{offsetof(web_info_t, ap_on), "\"rssi\":0,\"ap\":true,\"ap_ssid\"", "info: wifi.ap alone is true at its place"},
	};
	static const int ints[] = {0, 1, -1, 9, -9, 10, -10, 47, -61, 99, 100, -100, 125, 1000, -40, INT_MAX, INT_MIN, INT_MAX - 1, INT_MIN + 1};
	web_info_t info;
	const char *mark = "a\"MARK";
	uint32_t most = 4294967295u;
	bool yes = true;
	bool all;
	size_t i;

	info_now = &typical_info;
	check(gives_fixture(call_info, "fixtures/web_info.json"), "info: a display in use gives the fixture, with every room from 0 bytes on");
	info_now = &safe_mode_info;
	check(gives_fixture(call_info, "fixtures/web_info_safe_mode.json"),
	      "info: safe mode, no network, texts that are NULL, no settings give the fixture, with every room from 0 bytes on");

	memset(&info, 0, sizeof(info));
	info_now = &info;
	check(every_room(call_info, empty_info_text) && is_json_object(empty_info_text), "info: nothing set: every text empty, every number 0, settings null");

	// One member at a time: it stands where the header puts it, and nowhere else
	for(i = 0; i < sizeof(texts) / sizeof(texts[0]); i++)
	{
		memset(&info, 0, sizeof(info));
		memcpy((char *)&info + texts[i].offset, &mark, sizeof(mark));
		call_info((char *)room, ROOM_SIZE);
		check(strstr((char *)room, texts[i].where) != NULL && count_of((char *)room, "MARK") == 1 && strlen((char *)room) == strlen(empty_info_text) + 7,
		      texts[i].what);
	}
	for(i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++)
	{
		memset(&info, 0, sizeof(info));
		memcpy((char *)&info + numbers[i].offset, &most, sizeof(most));
		call_info((char *)room, ROOM_SIZE);
		check(strstr((char *)room, numbers[i].where) != NULL && count_of((char *)room, "4294967295") == 1 && strlen((char *)room) == strlen(empty_info_text) + 9,
		      numbers[i].what);
	}
	for(i = 0; i < sizeof(flags) / sizeof(flags[0]); i++)
	{
		memset(&info, 0, sizeof(info));
		memcpy((char *)&info + flags[i].offset, &yes, sizeof(yes));
		call_info((char *)room, ROOM_SIZE);
		check(strstr((char *)room, flags[i].where) != NULL && count_of((char *)room, "true") == 1 && strlen((char *)room) == strlen(empty_info_text) - 1,
		      flags[i].what);
	}

	// The two numbers with a sign
	memset(&info, 0, sizeof(info));
	info.temp_c = -2147483647 - 1;
	call_info((char *)room, ROOM_SIZE);
	check(strstr((char *)room, "\"psram_min\":0,\"temp_c\":-2147483648,\"heat\"") != NULL && count_of((char *)room, "-") == 2,
	      "info: temp_c stands at its place, the smallest int is written completely");
	info.temp_c = 0;
	info.rssi = -2147483647 - 1;
	call_info((char *)room, ROOM_SIZE);
	check(strstr((char *)room, "\"ip\":\"\",\"rssi\":-2147483648,\"ap\"") != NULL && count_of((char *)room, "-") == 2,
	      "info: wifi.rssi stands at its place, the smallest int is written completely");
	all = true;
	for(i = 0; i < sizeof(ints) / sizeof(ints[0]); i++)
	{
		memset(&info, 0, sizeof(info));
		info.temp_c = ints[i];
		info.rssi = ints[sizeof(ints) / sizeof(ints[0]) - 1 - i];
		model_info(&info);
		if(!gives(call_info, expected)) all = false;
	}
	check(all, "info: temp_c and rssi from the smallest to the largest int are written as printf writes them");

	// The settings are embedded as they are
	memset(&info, 0, sizeof(info));
	info.settings = "";
	check(gives(call_info, empty_info_text), "info: empty settings become null");
	info.settings = "{\"brightness\":5}";
	call_info((char *)room, ROOM_SIZE);
	check(strstr((char *)room, "\"reconnects\":0},\"settings\":{\"brightness\":5}}") != NULL && strlen((char *)room) == strlen(empty_info_text) + 12,
	      "info: the settings text stands at the end, without quotes");
	info.settings = "{ \"a\" : \"b\\\"c\" ,\n \"d\":[1 , 2]}";
	call_info((char *)room, ROOM_SIZE);
	check(strstr((char *)room, "\"settings\":{ \"a\" : \"b\\\"c\" ,\n \"d\":[1 , 2]}}") != NULL, "info: the settings text is passed on byte for byte, nothing in it is escaped");
	info.settings = "x";
	call_info((char *)room, ROOM_SIZE);
	check(strstr((char *)room, "\"settings\":x}") != NULL, "info: a settings text of one byte is passed on, whatever it is");
}

/*
 * GET /api/values
 */

static values_t values;

static void no_values(void)
{
	memset(&values, 0, sizeof(values));
}

static void add_value(const char *name, value_kind_t kind, double number, uint64_t seen_ms)
{
	value_t *value = &values.items[values.count++];

	// What follows the name in the memory is not zero by chance
	memset(value, 'x', sizeof(*value));
	strcpy(value->name, name);
	value->kind = kind;
	value->number = number;
	value->seen_ms = seen_ms;
}

static bool values_give(const char *view, uint64_t now_ms, const char *wanted)
{
	values_now.values = &values;
	values_now.view = view;
	values_now.now_ms = now_ms;
	return every_room(call_values, wanted);
}

// One number, fresh, under the name N
static bool number_gives(double number, const char *written)
{
	char wanted[128];

	no_values();
	add_value("N", VALUE_NUMBER, number, 1000);
	snprintf(wanted, sizeof(wanted), "{\"view\":\"\",\"values\":{\"N\":{\"v\":%s,\"age\":\"fresh\"}}}", written);
	values_now.values = &values;
	values_now.view = "";
	values_now.now_ms = 1000;
	return gives(call_values, wanted);
}

static void test_values(void)
{
	static const struct
	{
		double number;
		const char *written;
		const char *what;
	} numbers[] = {
		{812.5, "812.5", "values: 812.5"},
		{0, "0", "values: 0"},
		{-40, "-40", "values: -40"},
		{0.001, "0.001", "values: 0.001"},
		{1e9, "1e+09", "values: 10^9 is written with an exponent"},
		{999999999, "999999999", "values: 999999999, nine digits, is written without an exponent"},
		{-999999999, "-999999999", "values: -999999999"},
		{1234567890, "1.23456789e+09", "values: ten digits are rounded to nine"},
		{123456789.4, "123456789", "values: a tenth digit behind the point is rounded away"},
		{123456789.6, "123456790", "values: a tenth digit behind the point is rounded up"},
		{1, "1", "values: 1"},
		{-1, "-1", "values: -1"},
		{100, "100", "values: 100 keeps its zeros"},
		{187432, "187432", "values: 187432"},
		{1008.83, "1008.83", "values: 1008.83"},
		{67.31, "67.31", "values: 67.31"},
		{12.4, "12.4", "values: 12.4"},
		{0.1, "0.1", "values: 0.1"},
		{0.25, "0.25", "values: 0.25"},
		{-12.5, "-12.5", "values: -12.5"},
		{1.5, "1.5", "values: 1.5"},
		{1234567.89, "1234567.89", "values: nine digits around the point"},
		{0.123456789, "0.123456789", "values: nine digits behind the point"},
		{0.1234567894, "0.123456789", "values: the tenth digit behind the point is rounded away"},
		{1.0 / 3, "0.333333333", "values: a third has nine digits"},
		{2.0 / 3, "0.666666667", "values: two thirds are rounded up"},
		{0.0001, "0.0001", "values: 0.0001 is written without an exponent"},
		{0.00001, "1e-05", "values: 0.00001 is written with an exponent"},
		{0.000123456789, "0.000123456789", "values: the smallest size without an exponent, nine digits"},
		{-0.000123456789, "-0.000123456789", "values: the same below zero, 15 bytes"},
		{1.5e-7, "1.5e-07", "values: a small number with two digits"},
		{1e100, "1e+100", "values: 10^100"},
		{1e-100, "1e-100", "values: 10^-100"},
		{1e308, "1e+308", "values: 10^308"},
		{1.7976931348623157e308, "1.79769313e+308", "values: the largest number"},
		{-1.7976931348623157e308, "-1.79769313e+308", "values: the smallest number, 16 bytes"},
		{2.2250738585072014e-308, "2.22507386e-308", "values: the smallest normal number"},
		{4.9406564584124654e-324, "4.94065646e-324", "values: the smallest number above zero"},
		{-1.23456789e-308, "-1.23456789e-308", "values: a negative number with nine digits and an exponent of three, 16 bytes"},
		{4294967295.0, "4.2949673e+09", "values: 2^32 - 1 loses its trailing zero"},
		{2147483648.0, "2.14748365e+09", "values: 2^31"},
		{16777217, "16777217", "values: a number a float could not hold"},
	};
	static const struct
	{
		uint64_t seen_ms;
		uint64_t now_ms;
		const char *written;
		const char *what;
	} ages[] = {
		{5000, 5000, "{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"fresh\"}}}", "values: seen just now: fresh"},
		{5000, 7999, "{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"fresh\"}}}", "values: seen 2999 ms ago: fresh"},
		{5000, 8000, "{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"old\"}}}", "values: seen 3000 ms ago: old"},
		{5000, 14999, "{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"old\"}}}", "values: seen 9999 ms ago: old"},
		{5000, 15000, "{\"view\":\"live\",\"values\":{}}", "values: seen 10000 ms ago: gone, left out"},
		{5000, UINT64_MAX, "{\"view\":\"live\",\"values\":{}}", "values: seen the longest time ago: left out"},
		{5000, 4999, "{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"fresh\"}}}", "values: the clock stepped back by 1 ms: fresh"},
		{5000, 0, "{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"fresh\"}}}", "values: the clock stepped back to 0: fresh"},
		{((uint64_t)1 << 32) + 5000, ((uint64_t)1 << 32) + 8000, "{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"old\"}}}",
		 "values: times beyond 32 bit, seen 3000 ms ago: old"},
		{(uint64_t)1 << 32, 2999, "{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"fresh\"}}}",
		 "values: a time whose low 32 bit are behind, but which is before: fresh"},
	};
	// Three values of which every subset is gone
	static const char *const subsets[8] = {
		"{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"fresh\"},\"B\":{\"v\":\"on\",\"age\":\"fresh\"},\"C\":{\"v\":3,\"age\":\"fresh\"}}}",
		"{\"view\":\"live\",\"values\":{\"B\":{\"v\":\"on\",\"age\":\"fresh\"},\"C\":{\"v\":3,\"age\":\"fresh\"}}}",
		"{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"fresh\"},\"C\":{\"v\":3,\"age\":\"fresh\"}}}",
		"{\"view\":\"live\",\"values\":{\"C\":{\"v\":3,\"age\":\"fresh\"}}}",
		"{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"fresh\"},\"B\":{\"v\":\"on\",\"age\":\"fresh\"}}}",
		"{\"view\":\"live\",\"values\":{\"B\":{\"v\":\"on\",\"age\":\"fresh\"}}}",
		"{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"fresh\"}}}",
		"{\"view\":\"live\",\"values\":{}}",
	};
	bool all;
	size_t i;
	int gone;

	// The example of the header
	no_values();
	add_value("ENGINE_RPM", VALUE_NUMBER, 812.5, 10000);
	add_value("DPF_REGEN_STATUS", VALUE_ON, 0, 5000);
	values_now.values = &values;
	values_now.view = "live";
	values_now.now_ms = 12999;
	check(gives_fixture(call_values, "fixtures/web_values.json"), "values: the example of the header gives the fixture, with every room from 0 bytes on");

	no_values();
	add_value("COOLANT_TMP", VALUE_NUMBER, 88.25, 20000);
	add_value("GLOW_ACTIVE", VALUE_OFF, 0, 17001);
	add_value("LONG_AGO", VALUE_NUMBER, 5, 10000);
	add_value("@BATT_V", VALUE_NUMBER, 12.4, 17000);
	add_value("ECU_DISTANCE", VALUE_NUMBER, 187432, 20000);
	add_value("BEYOND_RANGE", VALUE_NUMBER, HUGE_VAL, 20000);
	add_value("RAIL_PRESSURE", VALUE_NUMBER, 0, 10001);
	add_value("OUTSIDE_TMP", VALUE_NUMBER, -40, 25000);
	add_value("NO_NUMBER", VALUE_NUMBER, NAN, 20000);
	values_now.view = "ecu_offline";
	values_now.now_ms = 20000;
	check(gives_fixture(call_values, "fixtures/web_values_mixed.json"),
	      "values: numbers and switches of every age give the fixture: gone and not finite ones are left out, the others keep their order");

	no_values();
	values_now.view = "no_wifi";
	check(gives_fixture(call_values, "fixtures/web_values_empty.json"), "values: no values give the fixture with an empty object");

	for(i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++) check(number_gives(numbers[i].number, numbers[i].written), numbers[i].what);
	check(number_gives(-0.0, "0") && signbit(values.items[0].number), "values: minus zero is written as 0");

	no_values();
	add_value("A", VALUE_NUMBER, HUGE_VAL, 1000);
	check(values_give("live", 1000, "{\"view\":\"live\",\"values\":{}}"), "values: infinity is left out");
	no_values();
	add_value("A", VALUE_NUMBER, -HUGE_VAL, 1000);
	check(values_give("live", 1000, "{\"view\":\"live\",\"values\":{}}"), "values: minus infinity is left out");
	no_values();
	add_value("A", VALUE_NUMBER, NAN, 1000);
	check(values_give("live", 1000, "{\"view\":\"live\",\"values\":{}}"), "values: a number that is none is left out");
	no_values();
	add_value("A", VALUE_NUMBER, -NAN, 1000);
	check(values_give("live", 1000, "{\"view\":\"live\",\"values\":{}}"), "values: a number that is none with a sign is left out");
	no_values();
	add_value("A", VALUE_NUMBER, 1, 1000);
	add_value("B", VALUE_NUMBER, HUGE_VAL, 1000);
	add_value("C", VALUE_NUMBER, NAN, 1000);
	add_value("D", VALUE_NUMBER, 4, 1000);
	check(values_give("live", 1000, "{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"fresh\"},\"D\":{\"v\":4,\"age\":\"fresh\"}}}"),
	      "values: numbers that are not finite between others leave no comma behind");

	// Switches
	no_values();
	add_value("A", VALUE_ON, 0, 1000);
	add_value("B", VALUE_OFF, 0, 1000);
	check(values_give("live", 1000, "{\"view\":\"live\",\"values\":{\"A\":{\"v\":\"on\",\"age\":\"fresh\"},\"B\":{\"v\":\"off\",\"age\":\"fresh\"}}}"),
	      "values: on and off are written as texts");
	no_values();
	add_value("A", VALUE_ON, 7.5, 1000);
	add_value("B", VALUE_OFF, HUGE_VAL, 1000);
	add_value("C", VALUE_ON, NAN, 1000);
	check(values_give("live", 4000, "{\"view\":\"live\",\"values\":{\"A\":{\"v\":\"on\",\"age\":\"old\"},\"B\":{\"v\":\"off\",\"age\":\"old\"},\"C\":{\"v\":\"on\",\"age\":\"old\"}}}"),
	      "values: the number of a switch does not matter, whatever it is");
	no_values();
	add_value("A", (value_kind_t)3, 1, 1000);
	add_value("B", VALUE_NUMBER, 2, 1000);
	add_value("C", (value_kind_t)-1, 3, 1000);
	add_value("D", (value_kind_t)99, 4, 1000);
	add_value("E", (value_kind_t)0x7FFFFFFF, 5, 1000);
	check(values_give("live", 1000, "{\"view\":\"live\",\"values\":{\"B\":{\"v\":2,\"age\":\"fresh\"}}}"), "values: a value whose kind is none of the three is left out");
	no_values();
	add_value("A", (value_kind_t)256, 1, 1000);
	add_value("B", (value_kind_t)257, 2, 1000);
	add_value("C", (value_kind_t)258, 3, 1000);
	add_value("D", (value_kind_t)65536, 4, 1000);
	add_value("E", (value_kind_t)65537, 5, 1000);
	add_value("F", (value_kind_t)65538, 6, 1000);
	add_value("G", VALUE_OFF, 7, 1000);
	check(values_give("live", 1000, "{\"view\":\"live\",\"values\":{\"G\":{\"v\":\"off\",\"age\":\"fresh\"}}}"),
	      "values: a value whose kind is one of the three only in its low 8 or 16 bit is left out");

	for(i = 0; i < sizeof(ages) / sizeof(ages[0]); i++)
	{
		no_values();
		add_value("A", VALUE_NUMBER, 1, ages[i].seen_ms);
		check(values_give("live", ages[i].now_ms, ages[i].written), ages[i].what);
	}
	no_values();
	add_value("A", VALUE_OFF, 0, 5000);
	check(values_give("live", 15000, "{\"view\":\"live\",\"values\":{}}") &&
	      values_give("live", 14999, "{\"view\":\"live\",\"values\":{\"A\":{\"v\":\"off\",\"age\":\"old\"}}}"),
	      "values: a switch is left out when it is gone as well, and old 1 ms before");

	// Values that are left out leave no comma, wherever they stand
	all = true;
	for(gone = 0; gone < 8; gone++)
	{
		no_values();
		add_value("A", VALUE_NUMBER, 1, (gone & 1) ? 0 : 20000);
		add_value("B", VALUE_ON, 0, (gone & 2) ? 0 : 20000);
		add_value("C", VALUE_NUMBER, 3, (gone & 4) ? 0 : 20000);
		if(!values_give("live", 20000, subsets[gone])) all = false;
	}
	check(all, "values: of three values every subset may be gone: the others stay in their order, with commas only between them");

	// Only the values that are counted
	no_values();
	add_value("A", VALUE_NUMBER, 1, 1000);
	add_value("B", VALUE_NUMBER, 2, 1000);
	values.count = 1;
	check(values_give("live", 1000, "{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"fresh\"}}}"), "values: an item behind the count is not written");
	values.count = 0;
	check(values_give("live", 1000, "{\"view\":\"live\",\"values\":{}}"), "values: with the count 0 nothing is written");

	// Texts
	no_values();
	add_value("a\"b\\c\x01\x7f", VALUE_NUMBER, 1, 1000);
	add_value("\xc3\x96l", VALUE_OFF, 0, 1000);
	add_value("", VALUE_NUMBER, 3, 1000);
	check(values_give("li\"ve\n", 1000, "{\"view\":\"li\\\"ve\\u000a\",\"values\":{\"a\\\"b\\\\c\\u0001\\u007f\":{\"v\":1,\"age\":\"fresh\"},"
	                  "\"\xc3\x96l\":{\"v\":\"off\",\"age\":\"fresh\"},\"\":{\"v\":3,\"age\":\"fresh\"}}}"),
	      "values: view and names are escaped, UTF-8 and the empty name are passed on");
	check(values_give(NULL, 1000, "{\"view\":\"\",\"values\":{\"a\\\"b\\\\c\\u0001\\u007f\":{\"v\":1,\"age\":\"fresh\"},"
	                  "\"\xc3\x96l\":{\"v\":\"off\",\"age\":\"fresh\"},\"\":{\"v\":3,\"age\":\"fresh\"}}}"),
	      "values: a view that is NULL counts as an empty text");
	no_values();
	add_value("N2345678901234567890123456789012", VALUE_NUMBER, 1, 1000);
	check(values_give("live", 1000, "{\"view\":\"live\",\"values\":{\"N2345678901234567890123456789012\":{\"v\":1,\"age\":\"fresh\"}}}"),
	      "values: a name of 32 bytes is written completely");
}

// Many numbers of every size: what is written is a JSON number, has at most 16 bytes and nine digits that count,
// and is the value to nine digits
static void test_values_numbers(void)
{
	bool is_number = true, is_short = true, is_near = true, is_model = true;
	size_t longest = 0;
	int round, i;

	for(round = 0; round < 400; round++)
	{
		int count, object, key;

		no_values();
		for(i = 0; i < VALUES_MAX; i++)
		{
			char name[8];
			double number;
			uint32_t kind = rnd(4);

			snprintf(name, sizeof(name), "V%02d", i);
			if(kind == 0)
			{
				// Any bits
				uint64_t bits = ((uint64_t)rnd(1 << 16) << 48) | ((uint64_t)rnd(1 << 16) << 32) | ((uint64_t)rnd(1 << 16) << 16) | rnd(1 << 16);

				memcpy(&number, &bits, sizeof(number));
			}
			else if(kind == 1) number = ((double)rnd(2000001) - 1000000) / 100;
			else if(kind == 2) number = ldexp((double)rnd(1 << 24) - (1 << 23), (int)rnd(200) - 100);
			else number = pow(10, (double)rnd(40) - 20) * (rnd(2) ? 1 : -1) * (1 + rnd(9));
			add_value(name, VALUE_NUMBER, number, 1000);
		}
		values_now.values = &values;
		values_now.view = "live";
		values_now.now_ms = 1000;
		model_values(&values, "live", 1000);
		if(!gives(call_values, expected)) is_model = false;

		count = json_parse(expected, strlen(expected), tokens, 4096);
		object = count > 0 ? json_member(expected, tokens, 0, "values") : -1;
		key = object + 1;
		for(i = 0; object > 0 && i < tokens[object].size; i++)
		{
			const json_token_t *written = &tokens[json_member(expected, tokens, key + 1, "v")];
			const value_t *value;
			char name[8];
			double back;

			// The names are V00 to V63
			if(!json_text(expected, &tokens[key], name, sizeof(name)) || atoi(name + 1) < 0 || atoi(name + 1) >= VALUES_MAX)
			{
				is_number = false;
				break;
			}
			value = &values.items[atoi(name + 1)];
			key += 1 + tokens[key + 1].skip;

			if(written->type != JSON_NUMBER || !json_number(expected, written, &back))
			{
				is_number = false;
				continue;
			}
			if(written->length > longest) longest = written->length;
			if(written->length > 16) is_short = false;
			// Half a unit of the ninth digit is at most 5e-9 of the value
			if(fabs(back - value->number) > fabs(value->number) * 5.0000001e-9) is_near = false;
		}
	}
	printf("  the longest number has %lu bytes\n", (unsigned long)longest);
	check(is_model, "values: 25600 numbers of every size are written as printf(\"%.9g\") writes them");
	check(is_number, "values: every number that is written is a JSON number for the reader of the project");
	check(is_short && longest == 16, "values: no number is written with more than 16 bytes");
	check(is_near, "values: every number that is written is the value to nine digits");
}

// The longest text of all: WEB_VALUES_SIZE is enough for it
static void test_values_size(void)
{
	char view[33];
	char name[33];
	size_t length;
	int i, at;

	// 64 names of 32 bytes that need no escape, the longest number, the longer age, a view of 32 bytes
	no_values();
	for(i = 0; i < VALUES_MAX; i++)
	{
		snprintf(name, sizeof(name), "NAME_OF_32_BYTES_%02d_ABCDEFGHIJKL", i);
		add_value(name, VALUE_NUMBER, -1.23456789e-308, 1000);
	}
	memset(view, 'v', 32);
	view[32] = '\0';
	// {"view":"..","values":{  "NAME":{"v":NUMBER,"age":"fresh"}  commas  }}
	length = 9 + 32 + 12 + 64 * (1 + 32 + 7 + 16 + 15) + 63 + 2;
	check(strlen(name) == 32 && values.count == 64 && length == 4662, "the longest text without escapes has 4662 bytes");
	model_values(&values, view, 1000);
	values_now.values = &values;
	values_now.view = view;
	values_now.now_ms = 1000;
	check(strlen(expected) == 4662 && rooms_around(call_values, expected) && is_json_object(expected),
	      "values: 64 values with names of 32 bytes, the longest number and a view of 32 bytes give 4662 bytes");

	// The same with names and a view of which every byte becomes six
	no_values();
	for(i = 0; i < VALUES_MAX; i++)
	{
		for(at = 0; at < 32; at++) name[at] = (char)(1 + (i + at) % 31);
		name[32] = '\0';
		add_value(name, VALUE_NUMBER, -1.23456789e-308, 1000);
	}
	memset(view, 0x7F, 32);
	view[32] = '\0';
	length = 9 + 32 * 6 + 12 + 64 * (1 + 32 * 6 + 7 + 16 + 15) + 63 + 2;
	check(length == 15062, "the longest text of all, with every byte of the names and the view escaped, has 15062 bytes");
	model_values(&values, NULL, 1000);
	// The view is longer than the model writes in one piece
	snprintf(text, sizeof(text), "{\"view\":\"");
	for(at = 0; at < 32; at++) strcat(text, "\\u007f");
	strcat(text, expected + 9);
	check(strlen(text) == 15062 && is_json_object(text), "the longest text of all is built by the test, 15062 bytes of JSON");
	values_now.view = view;
	check(rooms_around(call_values, text), "values: the longest text of all is written, and refused with a room of 15062 bytes or less");
	check(room_is_right(call_values, text, WEB_VALUES_SIZE) && WEB_VALUES_SIZE >= 15062 + 1,
	      "values: WEB_VALUES_SIZE is enough for 64 values with the longest names and a view of 32 bytes, all of them escaped");
}

/*
 * GET /api/wifi
 */

// One entry more than a list may have, and one in front: what is read outside the list is seen in the text
static net_profile_t profile_room[1 + NET_PROFILES_MAX + 1];
static net_profile_t *const profiles = &profile_room[1];
static web_seen_t seen[66];

static void profile(int index, const char *ssid, const char *password, const char *host)
{
	// Bytes behind the texts are not zero by chance
	memset(&profiles[index], '#', sizeof(profiles[index]));
	strcpy(profiles[index].ssid, ssid);
	strcpy(profiles[index].password, password);
	strcpy(profiles[index].host, host);
}

static void network(int index, const char *ssid, int rssi, bool secure)
{
	memset(&seen[index], '#', sizeof(seen[index]));
	strcpy(seen[index].ssid, ssid);
	seen[index].rssi = rssi;
	seen[index].secure = secure;
}

static bool wifi_gives(int profile_count, const char *current, int seen_count, const char *wanted)
{
	wifi_now.profiles = profiles;
	wifi_now.profile_count = profile_count;
	wifi_now.current = current;
	wifi_now.seen = seen;
	wifi_now.seen_count = seen_count;
	return every_room(call_wifi, wanted);
}

// The profiles member alone
static bool profiles_give(int profile_count, const char *written)
{
	char wanted[2048];

	snprintf(wanted, sizeof(wanted), "{\"current\":\"\",\"profiles\":[%s],\"seen\":[]}", written);
	return wifi_gives(profile_count, "", 0, wanted);
}

static bool seen_gives(int seen_count, const char *written)
{
	char wanted[2048];

	snprintf(wanted, sizeof(wanted), "{\"current\":\"\",\"profiles\":[],\"seen\":[%s]}", written);
	return wifi_gives(0, "", seen_count, wanted);
}

static void test_wifi(void)
{
	static const int counts[] = {-1, 5, 6, 100, INT_MAX, INT_MIN, -5};
	static const char four[] =
		"{\"ssid\":\"A\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false},"
		"{\"ssid\":\"B\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false},"
		"{\"ssid\":\"C\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false},"
		"{\"ssid\":\"D\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false}";
	bool all;
	size_t i;
	int hidden, at;

	memset(profile_room, '#', sizeof(profile_room));
	memset(seen, '#', sizeof(seen));

	profile(0, "Werkstatt", "geheim123", "192.168.1.50");
	profile(1, "WiCAN_a1b2c3d4e5f6", "@meatpi#", "");
	profile(2, "Camping", "", "");
	network(0, "Werkstatt", -52, true);
	network(1, "", -70, true);
	network(2, "WiCAN_a1b2c3d4e5f6", -40, true);
	network(3, "Freifunk", -88, false);
	wifi_now.profiles = profiles;
	wifi_now.profile_count = 3;
	wifi_now.current = "Werkstatt";
	wifi_now.seen = seen;
	wifi_now.seen_count = 4;
	check(gives_fixture(call_wifi, "fixtures/web_wifi.json"),
	      "wifi: three profiles and four networks, one of them hidden, give the fixture, with every room from 0 bytes on");
	wifi_now.profile_count = 0;
	wifi_now.current = "";
	wifi_now.seen_count = 0;
	check(gives_fixture(call_wifi, "fixtures/web_wifi_empty.json"), "wifi: no profile, no network, in no network give the fixture");
	wifi_now.profiles = NULL;
	wifi_now.current = NULL;
	wifi_now.seen = NULL;
	check(gives_fixture(call_wifi, "fixtures/web_wifi_empty.json"), "wifi: with the counts 0 the lists are not read, they may be NULL");

	// What is told about a profile
	profile(0, "Home", "", "");
	check(profiles_give(1, "{\"ssid\":\"Home\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false}"), "wifi: an open network: no password");
	profile(0, "Home", "x", "");
	check(profiles_give(1, "{\"ssid\":\"Home\",\"host\":\"\",\"password\":true,\"factory\":false,\"wican_ap\":false}"),
	      "wifi: a password of one byte is a password");
	profile(0, "Home", "@meatpi#", "");
	check(profiles_give(1, "{\"ssid\":\"Home\",\"host\":\"\",\"password\":true,\"factory\":true,\"wican_ap\":false}"),
	      "wifi: the password of the documentation is told, also in a network that is no WiCAN");
	profile(0, "Home", "@meatpi#x", "");
	check(profiles_give(1, "{\"ssid\":\"Home\",\"host\":\"\",\"password\":true,\"factory\":false,\"wican_ap\":false}"),
	      "wifi: a password that only begins like the one of the documentation is not it");
	profile(0, "Home", "@meatpi", "");
	check(profiles_give(1, "{\"ssid\":\"Home\",\"host\":\"\",\"password\":true,\"factory\":false,\"wican_ap\":false}"),
	      "wifi: the password of the documentation without its last byte is not it");
	profile(0, "WiCAN_x", "longpassword", "");
	check(profiles_give(1, "{\"ssid\":\"WiCAN_x\",\"host\":\"\",\"password\":true,\"factory\":false,\"wican_ap\":true}"),
	      "wifi: WiCAN_ and one byte is the access point of a WiCAN");
	profile(0, "WiCAN_", "", "");
	check(profiles_give(1, "{\"ssid\":\"WiCAN_\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false}"),
	      "wifi: WiCAN_ alone is no access point of a WiCAN");
	profile(0, "wican_x", "", "");
	check(profiles_give(1, "{\"ssid\":\"wican_x\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false}"),
	      "wifi: wican_x in small letters is no access point of a WiCAN");
	profile(0, "Home", "", "wican.local");
	check(profiles_give(1, "{\"ssid\":\"Home\",\"host\":\"wican.local\",\"password\":false,\"factory\":false,\"wican_ap\":false}"),
	      "wifi: the host of a profile is told");
	profile(0, "", "", "");
	check(profiles_give(1, "{\"ssid\":\"\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false}"),
	      "wifi: a profile with an empty SSID is listed like every other");
	profile(0, "a\"b\\c\x01\x7f\xc3\xa4", "", "h\"o\\s\nt");
	check(profiles_give(1, "{\"ssid\":\"a\\\"b\\\\c\\u0001\\u007f\xc3\xa4\",\"host\":\"h\\\"o\\\\s\\u000at\",\"password\":false,\"factory\":false,\"wican_ap\":false}"),
	      "wifi: SSID and host of a profile are escaped");

	// The list and its count
	profile(0, "A", "", "");
	profile(1, "B", "", "");
	profile(2, "C", "", "");
	profile(3, "D", "", "");
	memset(&profiles[4], 0, sizeof(profiles[4]));
	strcpy(profiles[4].ssid, "E");
	check(profiles_give(4, four), "wifi: four profiles are listed in the order of the list");
	check(profiles_give(0, ""), "wifi: a count of 0 lists no profile");
	profile(1, "", "", "");
	profile(3, "", "", "");
	check(profiles_give(4, "{\"ssid\":\"A\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false},"
	                       "{\"ssid\":\"\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false},"
	                       "{\"ssid\":\"C\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false},"
	                       "{\"ssid\":\"\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false}") &&
	      profiles_give(2, "{\"ssid\":\"A\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false},"
	                       "{\"ssid\":\"\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false}"),
	      "wifi: profiles with an empty SSID in the middle and at the end of the list are listed and counted");
	profile(0, "", "", "");
	profile(2, "", "", "");
	all = true;
	for(at = 1; at <= NET_PROFILES_MAX; at++)
	{
		char wanted[512] = "";

		for(hidden = 0; hidden < at; hidden++)
		{
			snprintf(wanted + strlen(wanted), sizeof(wanted) - strlen(wanted), "%s{\"ssid\":\"\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false}",
			         hidden ? "," : "");
		}
		if(!profiles_give(at, wanted)) all = false;
	}
	check(all, "wifi: of a list of profiles with empty SSIDs as many are listed as the count says, from 1 to 4");
	profile(0, "A", "", "");
	profile(1, "B", "", "");
	profile(2, "C", "", "");
	profile(3, "D", "", "");
	check(profiles_give(1, "{\"ssid\":\"A\",\"host\":\"\",\"password\":false,\"factory\":false,\"wican_ap\":false}"),
	      "wifi: a count of 1 lists the first profile only");
	check(profiles_give(5, ""), "wifi: a profile count of 5, one more than a list can hold, counts as 0");
	check(profiles_give(-1, ""), "wifi: a profile count of -1 counts as 0");
	all = true;
	for(i = 0; i < sizeof(counts) / sizeof(counts[0]); i++)
	{
		if(!profiles_give(counts[i], "")) all = false;
	}
	check(all, "wifi: every profile count that is not 0 to 4 counts as 0, up to the largest and the smallest int");

	// The network the display is in
	check(wifi_gives(0, "Caf\xc3\xa9 \"Nord\"\\\x02", 0, "{\"current\":\"Caf\xc3\xa9 \\\"Nord\\\"\\\\\\u0002\",\"profiles\":[],\"seen\":[]}"),
	      "wifi: the current network is escaped");

	// Networks seen
	network(0, "Net", 0, false);
	check(seen_gives(1, "{\"ssid\":\"Net\",\"rssi\":0,\"secure\":false}"), "wifi: an open network seen with rssi 0");
	network(0, "Net", -2147483647 - 1, true);
	check(seen_gives(1, "{\"ssid\":\"Net\",\"rssi\":-2147483648,\"secure\":true}"), "wifi: a secure network seen with the smallest rssi");
	network(0, "Net", 2147483647, true);
	check(seen_gives(1, "{\"ssid\":\"Net\",\"rssi\":2147483647,\"secure\":true}"), "wifi: a network seen with the largest rssi");
	network(0, "a\"b\\c\x1f\xc3\xa4", -5, false);
	check(seen_gives(1, "{\"ssid\":\"a\\\"b\\\\c\\u001f\xc3\xa4\",\"rssi\":-5,\"secure\":false}"), "wifi: the SSID of a network seen is escaped");
	network(0, "Net", -50, true);
	network(1, "Net", -60, true);
	check(seen_gives(2, "{\"ssid\":\"Net\",\"rssi\":-50,\"secure\":true},{\"ssid\":\"Net\",\"rssi\":-60,\"secure\":true}"),
	      "wifi: two access points of the same network are both listed");
	check(seen_gives(-1, "") && seen_gives(INT_MIN, "") && seen_gives(-100, ""), "wifi: a negative count of networks seen counts as 0");
	check(seen_gives(1, "{\"ssid\":\"Net\",\"rssi\":-50,\"secure\":true}"), "wifi: a network behind the count is not listed");

	// Hidden networks leave no comma, wherever they stand
	all = true;
	for(hidden = 0; hidden < 8; hidden++)
	{
		char wanted[256] = "";

		for(at = 0; at < 3; at++)
		{
			static const char *const names[3] = {"A", "B", "C"};

			network(at, (hidden >> at) & 1 ? "" : names[at], -10 * (at + 1), false);
			if(((hidden >> at) & 1) == 0)
			{
				snprintf(wanted + strlen(wanted), sizeof(wanted) - strlen(wanted), "%s{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":false}",
				         wanted[0] ? "," : "", names[at], -10 * (at + 1));
			}
		}
		if(!seen_gives(3, wanted)) all = false;
	}
	check(all, "wifi: of three networks every subset may be hidden: the others stay in their order, with commas only between them");

	// A long scan
	for(at = 0; at < 64; at++)
	{
		char name[NET_SSID_SIZE];

		snprintf(name, sizeof(name), "Network %d", at);
		network(at, name, -at, at % 2 == 0);
	}
	network(64, "Behind", 0, false);
	model_wifi(profiles, 0, "", seen, 64);
	wifi_now.profiles = profiles;
	wifi_now.profile_count = 0;
	wifi_now.current = "";
	wifi_now.seen = seen;
	wifi_now.seen_count = 64;
	check(count_of(expected, "{\"ssid\":\"Network ") == 64 && strstr(expected, "{\"ssid\":\"Network 63\",\"rssi\":-63,\"secure\":false}]}") != NULL &&
	      rooms_around(call_wifi, expected), "wifi: a scan of 64 networks is listed completely");
	profile(0, "Network 9", "password-9", "");
	profile(1, "Network 7", "", "h");
	profile(2, "Network 63", "@meatpi#", "");
	profile(3, "Network 0", "", "");
	model_wifi(profiles, 4, "Network 7", seen, 64);
	wifi_now.profile_count = 4;
	wifi_now.current = "Network 7";
	check(count_of(expected, "{\"ssid\":\"Network ") == 68 && count_of(expected, "\"rssi\":") == 64 && rooms_around(call_wifi, expected),
	      "wifi: a scan of 64 networks is listed completely also when the current network and the stored ones are among them");
}

// A count that is read in fewer bits than it has would list what lies behind the list: in a child, where
// reading there ends the child and not the test
static void test_wifi_large_counts(void)
{
	static const int counts[] = {256, 257, 258, 259, 260, 511, 65536, 65537, 65540, 0x1000001, 0x40000004, -252, -255, -65535, INT_MIN + 1, INT_MIN + 4};
	bool all = true;
	size_t i;

	memset(profile_room, '#', sizeof(profile_room));
	profile(0, "A", "", "");
	profile(1, "B", "", "");
	profile(2, "C", "", "");
	profile(3, "D", "", "");
	for(i = 0; i < sizeof(counts) / sizeof(counts[0]); i++)
	{
		if(!profiles_give(counts[i], "")) all = false;
	}
	check(all, "wifi: profile counts whose low 8 or 16 bit are 0 to 4 count as 0 like every other count that is no list");
}

// A list read from the flash may be damaged: a field without its zero ends before its last byte
static void test_wifi_damaged(void)
{
	char wanted[512];

	memset(profile_room, '#', sizeof(profile_room));
	memset(seen, '#', sizeof(seen));

	profile(0, "x", "TOPSECRET-1", "h");
	memset(profiles[0].ssid, 'S', NET_SSID_SIZE);
	snprintf(wanted, sizeof(wanted), "{\"ssid\":\"%.32s\",\"host\":\"h\",\"password\":true,\"factory\":false,\"wican_ap\":false}", "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS");
	check(profiles_give(1, wanted), "wifi: an SSID without a terminating zero is written with its first 32 bytes, nothing of the password behind it");
	call_wifi((char *)room, ROOM_SIZE);
	check(strstr((char *)room, "TOPSECRET") == NULL && strstr((char *)room, "SSSST") == NULL, "wifi: the password behind an SSID without zero is not in the answer");

	profile(0, "x", "TOPSECRET-2", "h");
	profile(1, "NEXT", "", "");
	memset(profiles[0].host, 'H', NET_HOST_SIZE);
	snprintf(wanted, sizeof(wanted), "{\"ssid\":\"x\",\"host\":\"%.39s\",\"password\":true,\"factory\":false,\"wican_ap\":false}", "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH");
	check(profiles_give(1, wanted), "wifi: a host without a terminating zero is written with its first 39 bytes, nothing of the next profile");

	// The last profile of a full list: behind it there is no list any more
	profile(0, "A", "", "");
	profile(1, "B", "", "");
	profile(2, "C", "", "");
	profile(3, "D", "", "");
	memset(profiles[3].host, 'H', NET_HOST_SIZE);
	profile(4, "BEHIND", "", "");
	wifi_now.profiles = profiles;
	wifi_now.profile_count = 4;
	wifi_now.current = "";
	wifi_now.seen_count = 0;
	call_wifi((char *)room, ROOM_SIZE);
	check(strstr((char *)room, "BEHIND") == NULL && strstr((char *)room, "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH\",\"password\":false") != NULL,
	      "wifi: a host without zero in the last profile of a full list: nothing behind the list is read");

	// 32 bytes and a zero are a whole SSID, 39 bytes and a zero a whole host
	profile(0, "x", "", "");
	memset(profiles[0].ssid, 'S', NET_SSID_SIZE - 1);
	profiles[0].ssid[NET_SSID_SIZE - 1] = '\0';
	memset(profiles[0].host, 'H', NET_HOST_SIZE - 1);
	profiles[0].host[NET_HOST_SIZE - 1] = '\0';
	snprintf(wanted, sizeof(wanted), "{\"ssid\":\"%.32s\",\"host\":\"%.39s\",\"password\":false,\"factory\":false,\"wican_ap\":false}",
	         "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS", "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH");
	check(profiles_give(1, wanted), "wifi: an SSID of 32 bytes and a host of 39 bytes are written completely");
	memset(profiles[0].ssid, 'S', 31);
	profiles[0].ssid[31] = '\0';
	memset(profiles[0].host, 'H', 38);
	profiles[0].host[38] = '\0';
	snprintf(wanted, sizeof(wanted), "{\"ssid\":\"%.31s\",\"host\":\"%.38s\",\"password\":false,\"factory\":false,\"wican_ap\":false}",
	         "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS", "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH");
	check(profiles_give(1, wanted), "wifi: an SSID of 31 bytes and a host of 38 bytes end at their zero");

	// A password without its zero is a password, and still none is in the answer
	profile(0, "x", "", "h");
	memset(profiles[0].password, 'P', NET_PASSWORD_SIZE);
	check(profiles_give(1, "{\"ssid\":\"x\",\"host\":\"h\",\"password\":true,\"factory\":false,\"wican_ap\":false}"),
	      "wifi: a password without a terminating zero is told as a password and not written");
	memset(profiles[0].password, 'P', NET_PASSWORD_SIZE);
	memcpy(profiles[0].password, "@meatpi#", 8);
	check(profiles_give(1, "{\"ssid\":\"x\",\"host\":\"h\",\"password\":true,\"factory\":false,\"wican_ap\":false}"),
	      "wifi: a password without zero that begins like the one of the documentation is not it");

	network(0, "x", -50, false);
	memset(seen[0].ssid, 'N', NET_SSID_SIZE);
	snprintf(wanted, sizeof(wanted), "{\"ssid\":\"%.32s\",\"rssi\":-50,\"secure\":false}", "NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN");
	check(seen_gives(1, wanted), "wifi: the SSID of a network seen without a terminating zero is written with its first 32 bytes");
	memset(seen[0].ssid, 'N', NET_SSID_SIZE - 1);
	seen[0].ssid[NET_SSID_SIZE - 1] = '\0';
	check(seen_gives(1, wanted), "wifi: the SSID of a network seen of 32 bytes is written completely");
}

// No answer ever contains a password: many lists, with passwords made of bytes that stand nowhere else
static void test_wifi_passwords(void)
{
	static const char password_bytes[] = "QXZ~^%$#@";
	static const char other_bytes[] = "abcdefghijklmnopqrstuvwxyz0123456789_-. ";
	int found = 0, different = 0, with_password = 0, factory = 0, damaged = 0;
	int round, i, at;

	memset(profile_room, 0, sizeof(profile_room));
	for(round = 0; round < 3000; round++)
	{
		int profile_count = (int)rnd(NET_PROFILES_MAX + 1);
		int seen_count = (int)rnd(4);
		char current[NET_SSID_SIZE];
		int length;

		// Also the entries that are not in the list hold passwords
		for(i = -1; i <= NET_PROFILES_MAX; i++)
		{
			net_profile_t *p = &profiles[i];
			uint32_t kind = rnd(10);

			memset(p, 0, sizeof(*p));
			length = 1 + (int)rnd(NET_SSID_SIZE - 1);
			for(at = 0; at < length; at++) p->ssid[at] = other_bytes[rnd(sizeof(other_bytes) - 1)];
			if(rnd(4) == 0) memcpy(p->ssid, "WiCAN_", 6);
			// Sometimes the SSID has lost its zero and runs into the password
			if(rnd(8) == 0)
			{
				memset(p->ssid + length, 'a', (size_t)(NET_SSID_SIZE - length));
				damaged++;
			}
			length = (int)rnd(NET_HOST_SIZE);
			for(at = 0; at < length; at++) p->host[at] = other_bytes[rnd(sizeof(other_bytes) - 1)];

			if(kind == 0) continue;
			if(kind == 1)
			{
				strcpy(p->password, "@meatpi#");
				continue;
			}
			// With and without a zero at its end
			length = kind == 2 ? NET_PASSWORD_SIZE : 1 + (int)rnd(NET_PASSWORD_SIZE - 1);
			for(at = 0; at < length; at++) p->password[at] = password_bytes[rnd(sizeof(password_bytes) - 1)];
		}
		for(i = 0; i < seen_count; i++)
		{
			memset(&seen[i], 0, sizeof(seen[i]));
			length = (int)rnd(NET_SSID_SIZE);
			for(at = 0; at < length; at++) seen[i].ssid[at] = other_bytes[rnd(sizeof(other_bytes) - 1)];
			seen[i].rssi = -(int)rnd(100);
			seen[i].secure = rnd(2) == 1;
		}
		memset(current, 0, sizeof(current));
		length = (int)rnd(NET_SSID_SIZE);
		for(at = 0; at < length; at++) current[at] = other_bytes[rnd(sizeof(other_bytes) - 1)];

		wifi_now.profiles = profiles;
		wifi_now.profile_count = profile_count;
		wifi_now.current = current;
		wifi_now.seen = seen;
		wifi_now.seen_count = seen_count;
		model_wifi(profiles, profile_count, current, seen, seen_count);
		if(!gives(call_wifi, expected) || !is_json_object(expected)) different++;

		length = call_wifi((char *)room, ROOM_SIZE);
		if(length < 0) different++;
		// No byte a password is made of, and no password as a whole
		if(strpbrk((char *)room, password_bytes) != NULL) found++;
		for(i = -1; i <= NET_PROFILES_MAX; i++)
		{
			char stored[NET_PASSWORD_SIZE + 1];

			memcpy(stored, profiles[i].password, NET_PASSWORD_SIZE);
			stored[NET_PASSWORD_SIZE] = '\0';
			if(stored[0] != '\0' && strstr((char *)room, stored) != NULL) found++;
			if(i >= 0 && i < profile_count && stored[0] != '\0') with_password++;
		}
		factory += count_of((char *)room, "\"factory\":true");
	}
	printf("  profiles with a password %d, with the one of the documentation %d, SSIDs without zero %d\n", with_password, factory, damaged);
	check(with_password > 3000 && factory > 300 && damaged > 1000, "wifi: 3000 random lists hold profiles with passwords, with the one of the documentation and damaged SSIDs");
	check(found == 0, "wifi: no stored password and no byte of one is in any of 3000 answers");
	check(different == 0, "wifi: 3000 random lists are written as the format of the header says, every answer is JSON for the reader of the project");
}

// Passwords made of the letters of the other texts, many of them a part of an SSID, a host or the current network
// of the same answer. Such a password stands in the answer - as a part of that text. What can be said then: the
// answer does not depend on the passwords. With every password replaced by another one it is the same byte for
// byte, so that it tells nothing of a password but whether there is one and whether it is the one of the
// documentation.
static void test_wifi_passwords_alike(void)
{
	static const char letters[] = "abcdefghijklmnopqrstuvwxyz0123456789_-. ";
	static const char replacing[] = "QXZ~^%$";
	static char first[8192];
	int different = 0, found = 0, shared = 0, replaced = 0, invalid = 0;
	int round, i, at;

	memset(profile_room, 0, sizeof(profile_room));
	for(round = 0; round < 3000; round++)
	{
		int profile_count = 1 + (int)rnd(NET_PROFILES_MAX);
		int seen_count = 1 + (int)rnd(3);
		char current[NET_SSID_SIZE];
		int length, first_length;

		for(i = 0; i < seen_count; i++)
		{
			memset(&seen[i], 0, sizeof(seen[i]));
			length = 1 + (int)rnd(NET_SSID_SIZE - 1);
			for(at = 0; at < length; at++) seen[i].ssid[at] = letters[rnd(sizeof(letters) - 1)];
			seen[i].rssi = -(int)rnd(100);
			seen[i].secure = rnd(2) == 1;
		}
		memset(current, 0, sizeof(current));
		length = 1 + (int)rnd(NET_SSID_SIZE - 1);
		for(at = 0; at < length; at++) current[at] = letters[rnd(sizeof(letters) - 1)];

		// Also the entries that are not in the list hold passwords
		for(i = -1; i <= NET_PROFILES_MAX; i++)
		{
			net_profile_t *p = &profiles[i];
			uint32_t kind = rnd(10);
			const char *source = NULL;

			memset(p, 0, sizeof(*p));
			length = 1 + (int)rnd(NET_SSID_SIZE - 1);
			for(at = 0; at < length; at++) p->ssid[at] = letters[rnd(sizeof(letters) - 1)];
			if(rnd(4) == 0) memcpy(p->ssid, "WiCAN_", 6);
			// Sometimes the SSID has lost its zero and runs into the password
			if(rnd(8) == 0) memset(p->ssid + length, 'a', (size_t)(NET_SSID_SIZE - length));
			length = (int)rnd(NET_HOST_SIZE);
			for(at = 0; at < length; at++) p->host[at] = letters[rnd(sizeof(letters) - 1)];

			if(kind == 0) continue;
			if(kind == 1)
			{
				strcpy(p->password, "@meatpi#");
				continue;
			}
			// The end of a text of the same answer, from any of its bytes on
			if(kind == 2) source = p->ssid;
			if(kind == 3) source = p->host;
			if(kind == 4) source = current;
			if(kind == 5) source = seen[0].ssid;
			if(kind == 6 && i >= 0) source = profiles[i - 1].ssid;
			if(source != NULL && source[0] != '\0')
			{
				// A field without its zero is told with one byte less than it has
				int limit = source == p->host ? NET_HOST_SIZE - 1 : NET_SSID_SIZE - 1;
				char whole[NET_HOST_SIZE];

				memset(whole, 0, sizeof(whole));
				for(at = 0; at < limit && source[at] != '\0'; at++) whole[at] = source[at];
				strcpy(p->password, whole + rnd((uint32_t)at));
				continue;
			}
			// Any text of the same letters, with and without a zero at its end
			length = kind == 7 ? NET_PASSWORD_SIZE : 1 + (int)rnd(NET_PASSWORD_SIZE - 1);
			for(at = 0; at < length; at++) p->password[at] = letters[rnd(sizeof(letters) - 1)];
		}

		wifi_now.profiles = profiles;
		wifi_now.profile_count = profile_count;
		wifi_now.current = current;
		wifi_now.seen = seen;
		wifi_now.seen_count = seen_count;
		model_wifi(profiles, profile_count, current, seen, seen_count);
		if(!is_json_object(expected)) invalid++;
		if(!gives(call_wifi, expected)) different++;
		first_length = call_wifi(first, sizeof(first));

		for(i = 0; i < profile_count; i++)
		{
			char stored[NET_PASSWORD_SIZE + 1];

			memcpy(stored, profiles[i].password, NET_PASSWORD_SIZE);
			stored[NET_PASSWORD_SIZE] = '\0';
			if(stored[0] != '\0' && strcmp(stored, "@meatpi#") != 0 && strstr(first, stored) != NULL) shared++;
		}

		// Every password but the one of the documentation becomes another one, of bytes that stand nowhere else
		for(i = -1; i <= NET_PROFILES_MAX; i++)
		{
			net_profile_t *p = &profiles[i];

			if(p->password[0] == '\0' || memcmp(p->password, "@meatpi#", 9) == 0) continue;
			memset(p->password, 0, sizeof(p->password));
			length = rnd(4) == 0 ? NET_PASSWORD_SIZE : 1 + (int)rnd(NET_PASSWORD_SIZE - 1);
			for(at = 0; at < length; at++) p->password[at] = replacing[rnd(sizeof(replacing) - 1)];
			replaced++;
		}
		length = call_wifi((char *)room, ROOM_SIZE);
		if(length < 0 || length != first_length || strcmp((char *)room, first) != 0) different++;
		if(strpbrk((char *)room, replacing) != NULL) found++;
	}
	printf("  answers that contain a stored password as a part of another text %d, passwords replaced %d\n", shared, replaced);
	check(shared > 1000 && replaced > 5000, "wifi: 3000 random lists hold passwords that are a part of an SSID, a host or the current network of the same answer");
	check(different == 0, "wifi: with every password replaced by another one the answer stays the same byte for byte: it tells nothing of a password "
	      "but whether there is one and whether it is the one of the documentation");
	check(found == 0, "wifi: no byte of the passwords put in their place is in any of these answers");
	check(invalid == 0, "wifi: every one of these answers is JSON for the reader of the project");
}

/*
 * POST /api/wifi and POST /api/wifi/forget
 */

typedef struct
{
	unsigned char before[64];
	web_wifi_request_t request;
	unsigned char after[64];
} request_box_t;

typedef struct
{
	unsigned char before[64];
	char ssid[NET_SSID_SIZE];
	unsigned char after[64];
} ssid_box_t;

static request_box_t request_box;
static ssid_box_t ssid_box;

static bool all_guard(const void *memory, size_t size)
{
	const unsigned char *bytes = memory;
	size_t i;

	for(i = 0; i < size; i++)
	{
		if(bytes[i] != GUARD) return false;
	}
	return true;
}

// The request as the header describes it: the texts, every other byte zero
static bool request_is(const char *ssid, const char *password, bool has_password, const char *host)
{
	web_wifi_request_t wanted;

	memset(&wanted, 0, sizeof(wanted));
	strcpy(wanted.ssid, ssid);
	strcpy(wanted.password, password);
	wanted.has_password = has_password;
	strcpy(wanted.host, host);
	return memcmp(&request_box.request, &wanted, sizeof(wanted)) == 0 && all_guard(request_box.before, sizeof(request_box.before)) &&
	       all_guard(request_box.after, sizeof(request_box.after));
}

static bool wifi_parse(const char *json, size_t length, json_token_t *room_for_tokens, int count)
{
	// Whatever was in the request before
	memset(&request_box, GUARD, sizeof(request_box));
	return web_wifi_parse(json, length, &request_box.request, room_for_tokens, count);
}

static bool wifi_taken(const char *json, const char *ssid, const char *password, bool has_password, const char *host)
{
	return wifi_parse(json, strlen(json), work, WEB_WIFI_TOKENS) && request_is(ssid, password, has_password, host);
}

static bool wifi_refused(const char *json)
{
	return !wifi_parse(json, strlen(json), work, WEB_WIFI_TOKENS) && all_guard(&request_box, sizeof(request_box));
}

static bool forget_parse(const char *json, size_t length, json_token_t *room_for_tokens, int count)
{
	memset(&ssid_box, GUARD, sizeof(ssid_box));
	return web_forget_parse(json, length, ssid_box.ssid, room_for_tokens, count);
}

static bool forget_taken(const char *json, const char *ssid)
{
	char wanted[NET_SSID_SIZE];

	memset(wanted, 0, sizeof(wanted));
	strcpy(wanted, ssid);
	return forget_parse(json, strlen(json), work, WEB_WIFI_TOKENS) && memcmp(ssid_box.ssid, wanted, sizeof(wanted)) == 0 &&
	       all_guard(ssid_box.before, sizeof(ssid_box.before)) && all_guard(ssid_box.after, sizeof(ssid_box.after));
}

static bool forget_refused(const char *json)
{
	return !forget_parse(json, strlen(json), work, WEB_WIFI_TOKENS) && all_guard(&ssid_box, sizeof(ssid_box));
}

// `count` times the same piece
static const char *times(const char *piece, int count)
{
	static char texts[6][600];
	static int next = 0;
	char *to = texts[next++ % 6];
	int i;

	to[0] = '\0';
	for(i = 0; i < count; i++) strcat(to, piece);
	return to;
}

// Runs before any other text is read: a reader that goes on with the tokens of the text before finds them
// in the same memory here, and the check names it. Later texts lie elsewhere.
static void test_stale_tokens(void)
{
	static char same[] = "{\"ssid\":\"Home\"}";
	static char two[] = "{\"host\":\"h1\",\"ssid\":\"Home\"}";
	static char forget[] = "{\"ssid\":\"Home\"}";
	static char forget_two[] = "{\"x\":\"y\",\"ssid\":\"Home\"}";

	check(wifi_parse(same, strlen(same), work, WEB_WIFI_TOKENS) && request_is("Home", "", false, ""), "wifi request: a text is read before the same memory is used again");
	check(!wifi_parse(same, 0, work, WEB_WIFI_TOKENS) && all_guard(&request_box, sizeof(request_box)),
	      "wifi request: length 0 in the same memory: refused, the tokens of the text before are not used");
	same[0] = ' ';
	check(!wifi_parse(same, 1, work, WEB_WIFI_TOKENS) && all_guard(&request_box, sizeof(request_box)),
	      "wifi request: a space in the same memory: refused, the tokens of the text before are not used");
	check(wifi_parse(two, strlen(two), work, WEB_WIFI_TOKENS) && request_is("Home", "", false, "h1"),
	      "wifi request: a text with two members is read before its memory is used again");
	// The same memory now holds {"host":"h2"} followed by "ssid":"Home"}
	memcpy(two, "{\"host\":\"h2\"}", 13);
	check(!wifi_parse(two, 13, work, WEB_WIFI_TOKENS) && all_guard(&request_box, sizeof(request_box)),
	      "wifi request: a text without SSID in the same memory: the SSID of the text before is not read again from its old tokens");

	check(forget_parse(forget, strlen(forget), work, WEB_WIFI_TOKENS) && strcmp(ssid_box.ssid, "Home") == 0,
	      "forget request: a text is read before the same memory is used again");
	check(!forget_parse(forget, 0, work, WEB_WIFI_TOKENS) && all_guard(&ssid_box, sizeof(ssid_box)),
	      "forget request: length 0 in the same memory: refused, the tokens of the text before are not used");
	forget[0] = ' ';
	check(!forget_parse(forget, 1, work, WEB_WIFI_TOKENS) && all_guard(&ssid_box, sizeof(ssid_box)),
	      "forget request: a space in the same memory: refused, the tokens of the text before are not used");
	check(forget_parse(forget_two, strlen(forget_two), work, WEB_WIFI_TOKENS) && strcmp(ssid_box.ssid, "Home") == 0,
	      "forget request: a text with two members is read before its memory is used again");
	memcpy(forget_two, "{\"x\":\"z\"}", 9);
	check(!forget_parse(forget_two, 9, work, WEB_WIFI_TOKENS) && all_guard(&ssid_box, sizeof(ssid_box)),
	      "forget request: a text without SSID in the same memory: the SSID of the text before is not read again from its old tokens");
}

// What is asked of a text member, the same for the three of them; `fits` is the largest number of bytes
static void test_request_texts(const char *member, int fits)
{
	static const struct
	{
		const char *value;
		const char *read;       // NULL: refused
		const char *what;
	} cases[] = {
		{"\"Home\"", "Home", "a text is taken"},
		{"\"Caf\\u00e9 Nord\"", "Caf\xc3\xa9 Nord", "an escape is resolved"},
		{"\"a\\\"b\\\\c\\/d\"", "a\"b\\c/d", "escaped quote, backslash and slash are resolved"},
		{"\"\\ud83d\\ude00 Bus\"", "\xf0\x9f\x98\x80 Bus", "a surrogate pair becomes four bytes"},
		{"\"K\xc3\xbc" "che 2,4 GHz\"", "K\xc3\xbc" "che 2,4 GHz", "UTF-8, blanks and a comma are taken"},
		{"\" a \"", " a ", "blanks at both ends stay"},
		{"\"\\u0020\\u007f~\"", " \x7f~", "the bytes 0x20, 0x7E and 0x7F are allowed"},
		{"\"\x7f\"", "\x7f", "the byte 0x7F written as it is is allowed"},
		{"\"\x80\xff\"", "\x80\xff", "bytes that are no UTF-8 are passed on"},
		{"5", NULL, "a number is refused"},
		{"12345678", NULL, "a number of eight digits is refused"},
		{"null", NULL, "null is refused"},
		{"true", NULL, "true is refused"},
		{"false", NULL, "false is refused"},
		{"[\"Home1234\"]", NULL, "a list with a text is refused"},
		{"{\"a\":\"Home1234\"}", NULL, "an object with a text is refused"},
		{"\"Home1234\\u0001\"", NULL, "the byte 0x01 written as an escape is refused"},
		{"\"\\u0001Home1234\"", NULL, "the byte 0x01 at the beginning is refused"},
		{"\"Home\\u001f1234\"", NULL, "the byte 0x1F is refused"},
		{"\"Home\\n1234\"", NULL, "a line break written as \\n is refused"},
		{"\"Home\\t1234\"", NULL, "a tab written as \\t is refused"},
		{"\"Home\\r1234\"", NULL, "a carriage return written as \\r is refused"},
		{"\"Home\\b1234\"", NULL, "a backspace written as \\b is refused"},
		{"\"Home\\f1234\"", NULL, "a form feed written as \\f is refused"},
		{"\"Home\\u00001234\"", NULL, "\\u0000 is refused"},
		{"\"Home1234\\ud83d\"", NULL, "the first half of a surrogate pair alone is refused"},
		{"\"Home1234\\ude00\"", NULL, "the second half of a surrogate pair alone is refused"},
		{"\"Home1234\\ud83dx\"", NULL, "half a surrogate pair followed by a letter is refused"},
	};
	bool is_ssid = strcmp(member, "ssid") == 0, is_password = strcmp(member, "password") == 0;
	char json[1024];
	char what[160];
	char longest[80], too_long[84];
	size_t i;

	for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		const char *read = cases[i].read;
		bool right;

		// A password has to have 8 bytes, the other rules are the same
		if(is_password && read != NULL && strlen(read) < 8) continue;

		if(is_ssid) snprintf(json, sizeof(json), "{\"ssid\":%s}", cases[i].value);
		else snprintf(json, sizeof(json), "{\"ssid\":\"Net\",\"%s\":%s}", member, cases[i].value);

		if(read == NULL) right = wifi_refused(json);
		else right = wifi_taken(json, is_ssid ? read : "Net", is_password ? read : "", is_password, !is_ssid && !is_password ? read : "");
		snprintf(what, sizeof(what), "wifi request, %s: %s", member, cases[i].what);
		check(right, what);

		if(!is_ssid) continue;
		snprintf(what, sizeof(what), "forget request, ssid: %s", cases[i].what);
		check(read == NULL ? forget_refused(json) : forget_taken(json, read), what);
	}

	// The room of the field
	memset(longest, 'x', (size_t)fits);
	longest[fits] = '\0';
	memset(too_long, 'y', (size_t)fits + 1);
	too_long[fits + 1] = '\0';

	if(is_ssid) snprintf(json, sizeof(json), "{\"ssid\":\"%s\"}", longest);
	else snprintf(json, sizeof(json), "{\"ssid\":\"Net\",\"%s\":\"%s\"}", member, longest);
	snprintf(what, sizeof(what), "wifi request, %s: %d bytes fit", member, fits);
	check(wifi_taken(json, is_ssid ? longest : "Net", is_password ? longest : "", is_password, !is_ssid && !is_password ? longest : ""), what);
	if(is_ssid)
	{
		snprintf(what, sizeof(what), "forget request, ssid: %d bytes fit", fits);
		check(forget_taken(json, longest), what);
	}

	if(is_ssid) snprintf(json, sizeof(json), "{\"ssid\":\"%s\"}", longest + 1);
	else snprintf(json, sizeof(json), "{\"ssid\":\"Net\",\"%s\":\"%s\"}", member, longest + 1);
	snprintf(what, sizeof(what), "wifi request, %s: %d bytes, one less than fit, are taken", member, fits - 1);
	check(wifi_taken(json, is_ssid ? longest + 1 : "Net", is_password ? longest + 1 : "", is_password, !is_ssid && !is_password ? longest + 1 : ""), what);

	if(is_ssid) snprintf(json, sizeof(json), "{\"ssid\":\"%s\"}", too_long);
	else snprintf(json, sizeof(json), "{\"ssid\":\"Net\",\"%s\":\"%s\"}", member, too_long);
	snprintf(what, sizeof(what), "wifi request, %s: %d bytes do not fit, the text is refused and not cut", member, fits + 1);
	check(wifi_refused(json), what);
	if(is_ssid)
	{
		snprintf(what, sizeof(what), "forget request, ssid: %d bytes do not fit, the text is refused and not cut", fits + 1);
		check(forget_refused(json), what);
	}

	// The length counts in bytes, with the escapes resolved
	if(is_ssid) snprintf(json, sizeof(json), "{\"ssid\":\"%s\"}", times("\\u0041", fits));
	else snprintf(json, sizeof(json), "{\"ssid\":\"Net\",\"%s\":\"%s\"}", member, times("\\u0041", fits));
	snprintf(what, sizeof(what), "wifi request, %s: %d escaped characters fit", member, fits);
	check(wifi_taken(json, is_ssid ? times("A", fits) : "Net", is_password ? times("A", fits) : "", is_password,
	                 !is_ssid && !is_password ? times("A", fits) : ""), what);
	if(is_ssid) snprintf(json, sizeof(json), "{\"ssid\":\"%s\"}", times("\\u0041", fits + 1));
	else snprintf(json, sizeof(json), "{\"ssid\":\"Net\",\"%s\":\"%s\"}", member, times("\\u0041", fits + 1));
	snprintf(what, sizeof(what), "wifi request, %s: %d escaped characters do not fit", member, fits + 1);
	check(wifi_refused(json), what);

	// fits - 1 bytes and a character of two bytes: one byte too many, and nothing is cut in the middle
	if(is_ssid) snprintf(json, sizeof(json), "{\"ssid\":\"%s\xc3\xa4\"}", longest + 1);
	else snprintf(json, sizeof(json), "{\"ssid\":\"Net\",\"%s\":\"%s\xc3\xa4\"}", member, longest + 1);
	snprintf(what, sizeof(what), "wifi request, %s: %d bytes that end in a character of two bytes do not fit", member, fits + 1);
	check(wifi_refused(json), what);
	if(is_ssid) snprintf(json, sizeof(json), "{\"ssid\":\"%s\xc3\xa4\"}", longest + 2);
	else snprintf(json, sizeof(json), "{\"ssid\":\"Net\",\"%s\":\"%s\xc3\xa4\"}", member, longest + 2);
	snprintf(too_long, sizeof(too_long), "%s\xc3\xa4", longest + 2);
	snprintf(what, sizeof(what), "wifi request, %s: %d bytes that end in a character of two bytes fit", member, fits);
	check(wifi_taken(json, is_ssid ? too_long : "Net", is_password ? too_long : "", is_password, !is_ssid && !is_password ? too_long : ""), what);
}

static void test_wifi_request(void)
{
	static const struct
	{
		const char *json;
		const char *what;
	} refused[] = {
		// First a text whose tokens are all there: a reader that goes on does it in an orderly way
		{"{\"ssid\":\"Home\"}x", "wifi request: text behind the object is refused"},
		{"", "wifi request: an empty text is refused"},
		{" ", "wifi request: a space is refused"},
		{"{", "wifi request: only a brace is refused"},
		{"{\"ssid\":\"Home\"", "wifi request: an object that is not closed is refused"},
		{"{\"ssid\":\"Home\",}", "wifi request: a comma before the end is refused"},
		{"{\"ssid\":}", "wifi request: a member without value is refused"},
		{"{\"ssid\":\"Home}", "wifi request: a text that is not closed is refused"},
		{"{\"ssid\":\"Ho\\me\"}", "wifi request: an escape JSON does not know is refused"},
		{"{\"ssid\":\"Ho\nme\"}", "wifi request: a line break inside a text is refused"},
		{"{\"ssid\":\"Home\",\"x\":[[[[[[[[1]]]]]]]]}", "wifi request: nested deeper than the reader follows is refused"},
		{"[]", "wifi request: an empty list is refused"},
		{"[{\"ssid\":\"Home\"}]", "wifi request: an object inside a list is refused"},
		{"[\"ssid\",\"Home\"]", "wifi request: a list of a name and a value is refused"},
		{"\"Home\"", "wifi request: a text alone is refused"},
		{"5", "wifi request: a number is refused"},
		{"null", "wifi request: null is refused"},
		{"true", "wifi request: true is refused"},
		{"{}", "wifi request: an object without ssid is refused"},
		{"{\"password\":\"12345678\"}", "wifi request: a password without ssid is refused"},
		{"{\"host\":\"wican.local\"}", "wifi request: a host without ssid is refused"},
		{"{\"password\":\"12345678\",\"host\":\"wican.local\"}", "wifi request: password and host without ssid are refused"},
		{"{\"SSID\":\"Home\"}", "wifi request: SSID in capitals is not the ssid"},
		{"{\"ssi\":\"Home\"}", "wifi request: ssi is not the ssid"},
		{"{\"ssid2\":\"Home\"}", "wifi request: ssid2 is not the ssid"},
		{"{\"x\":{\"ssid\":\"Home\"}}", "wifi request: an ssid inside another member is not the ssid"},
		{"{\"ssid\":\"\"}", "wifi request: an empty SSID is refused"},
		{"{\"ssid\":\"\",\"password\":\"12345678\"}", "wifi request: an empty SSID with a password is refused"},
		{"{\"ssid\":\"Home\",\"password\":\"1\"}", "wifi request: a password of 1 byte is refused"},
		{"{\"ssid\":\"Home\",\"password\":\"1234567\"}", "wifi request: a password of 7 bytes is refused"},
		{"{\"ssid\":\"Home\",\"password\":\"\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\"}", "wifi request: a password of 7 escaped characters is refused"},
		{"{\"ssid\":\"Home\",\"password\":\"\xc3\xa4\xc3\xa4\xc3\xa4x\"}", "wifi request: a password of 7 bytes in 4 characters is refused"},
		{"{\"password\":\"1234567\",\"ssid\":\"Home\"}", "wifi request: a password of 7 bytes in front of the ssid is refused"},

		// The same name twice
		{"{\"ssid\":5,\"ssid\":\"Home\"}", "wifi request: an ssid that is no text is not healed by a good one behind it"},
		{"{\"ssid\":\"Home\",\"ssid\":5}", "wifi request: a good ssid followed by one that is no text is refused"},
		{"{\"ssid\":\"\",\"ssid\":\"Home\"}", "wifi request: an empty ssid is not healed by a good one behind it"},
		{"{\"ssid\":\"Home\",\"ssid\":\"\"}", "wifi request: a good ssid followed by an empty one is refused"},
		{"{\"ssid\":\"Home\",\"password\":\"abc\",\"password\":\"12345678\"}", "wifi request: a short password is not healed by a good one behind it"},
		{"{\"ssid\":\"Home\",\"password\":\"12345678\",\"password\":\"abc\"}", "wifi request: a good password followed by a short one is refused"},
		{"{\"ssid\":\"Home\",\"password\":\"12345678\",\"password\":null}", "wifi request: a good password followed by null is refused"},
		{"{\"ssid\":\"Home\",\"host\":5,\"host\":\"h\"}", "wifi request: a host that is no text is not healed by a good one behind it"},
		{"{\"ssid\":\"Home\",\"host\":\"h\",\"host\":\"a\\nb\"}", "wifi request: a good host followed by one with a line break is refused"},
	};
	// Three members, four unknown ones and one with a list: 1 + 2 * 6 + 3 = 16 tokens
	static const char tokens16[] = "{\"a\":1,\"b\":2,\"c\":3,\"d\":[4],\"ssid\":\"Home\",\"password\":\"12345678\",\"host\":\"h\"}";
	// One member more instead of the list: 1 + 2 * 8 = 17 tokens
	static const char tokens17[] = "{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"ssid\":\"Home\",\"password\":\"12345678\",\"host\":\"h\"}";
	static const char full[] = "{\"ssid\":\"Home\",\"password\":\"12345678\",\"host\":\"h\"}";
	size_t i, at;
	int count;
	bool all;

	check(read_fixture("fixtures/web_wifi_request.json", fixture, sizeof(fixture)) && is_json_object(fixture) &&
	      wifi_taken(fixture, "Werkstatt", "geheim123", true, "192.168.1.50"), "wifi request: the fixture is read: SSID, password and host");

	check(wifi_taken("{\"ssid\":\"x\"}", "x", "", false, ""), "wifi request: an ssid of one byte alone: no password member, no host");
	check(wifi_taken("{\"ssid\":\"Home\",\"password\":\"\"}", "Home", "", true, ""), "wifi request: an empty password is a password member: an open network");
	check(wifi_taken("{\"ssid\":\"Home\",\"password\":\"12345678\"}", "Home", "12345678", true, ""), "wifi request: a password of 8 bytes is taken");
	check(wifi_taken("{\"ssid\":\"Home\",\"password\":\"123456789\"}", "Home", "123456789", true, ""), "wifi request: a password of 9 bytes is taken");
	check(wifi_taken("{\"ssid\":\"Home\",\"password\":\"\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\"}", "Home", "\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4", true, ""),
	      "wifi request: a password of 8 bytes in 4 characters is taken");
	check(wifi_taken("{\"ssid\":\"Home\",\"password\":\"\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\"}", "Home", "AAAAAAAA", true, ""),
	      "wifi request: a password of 8 escaped characters is taken");
	check(wifi_taken("{\"ssid\":\"Home\",\"host\":\"\"}", "Home", "", false, ""), "wifi request: an empty host means: find the adapter");
	check(wifi_taken("{\"ssid\":\"Home\",\"host\":\"wican_a1b2c3d4e5f6.local\"}", "Home", "", false, "wican_a1b2c3d4e5f6.local"),
	      "wifi request: a host without a password member");
	check(wifi_taken("{\"host\":\"h\",\"password\":\"12345678\",\"ssid\":\"Home\"}", "Home", "12345678", true, "h"), "wifi request: the order of the members does not matter");
	check(wifi_taken(" {\t\"ssid\" :\n\"Home\" ,\r\n \"host\" : \"h\" } ", "Home", "", false, "h"), "wifi request: whitespace between the parts is allowed");
	check(wifi_taken("{\"\\u0073sid\":\"Home\",\"pass\\u0077ord\":\"12345678\",\"h\\u006fst\":\"h\"}", "Home", "12345678", true, "h"),
	      "wifi request: names written with escapes are the members");
	check(wifi_taken("{\"mode\":5,\"ssid\":\"Home\",\"Password\":\"1\",\"pass\":null,\"hostname\":[\"h\"]}", "Home", "", false, ""),
	      "wifi request: members the format does not know are ignored, whatever they hold");
	check(wifi_taken("{\"ssid\":\"Home\",\"x\":{\"ssid\":\"\",\"password\":\"1\",\"host\":5}}", "Home", "", false, ""),
	      "wifi request: known names inside an unknown member are not looked at");
	check(wifi_taken("{\"x\":{\"host\":\"inner\"},\"ssid\":\"Home\"}", "Home", "", false, ""), "wifi request: the member behind an unknown object is read");

	// The same name twice
	check(wifi_taken("{\"ssid\":\"First\",\"ssid\":\"Home\"}", "Home", "", false, ""), "wifi request: of two ssid members the last counts");
	check(wifi_taken("{\"ssid\":\"A longer first name\",\"ssid\":\"x\"}", "x", "", false, ""),
	      "wifi request: nothing of a longer first ssid stays behind a shorter second one");
	check(wifi_taken("{\"ssid\":\"Home\",\"password\":\"first-password\",\"password\":\"12345678\"}", "Home", "12345678", true, ""),
	      "wifi request: of two passwords the last counts, nothing of a longer first one stays behind it");
	check(wifi_taken("{\"ssid\":\"Home\",\"password\":\"first-password\",\"password\":\"\"}", "Home", "", true, ""),
	      "wifi request: a password followed by an empty one: an open network, nothing of the first one stays");
	check(wifi_taken("{\"ssid\":\"Home\",\"host\":\"first.example\",\"host\":\"h\"}", "Home", "", false, "h"),
	      "wifi request: of two hosts the last counts, nothing of a longer first one stays behind it");

	for(i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) check(wifi_refused(refused[i].json), refused[i].what);

	test_request_texts("ssid", 32);
	test_request_texts("password", 64);
	test_request_texts("host", 39);

	// Only `length` bytes belong to the text
	check(wifi_parse("{\"ssid\":\"Home\"}x", 15, work, WEB_WIFI_TOKENS) && request_is("Home", "", false, ""), "wifi request: bytes behind the given length are not read");
	check(!wifi_parse("{\"ssid\":\"Home\"}", 14, work, WEB_WIFI_TOKENS) && all_guard(&request_box, sizeof(request_box)),
	      "wifi request: a length that ends before the brace is refused");

	// The room for the tokens
	check(json_parse(tokens16, strlen(tokens16), large_work, 512) == 16 && json_parse(tokens17, strlen(tokens17), large_work, 512) == 17 &&
	      json_parse(full, strlen(full), large_work, 512) == 7, "the test texts have 16, 17 and 7 tokens");
	check(wifi_taken(tokens16, "Home", "12345678", true, "h"), "wifi request: a text of 16 tokens is read with WEB_WIFI_TOKENS tokens");
	check(wifi_refused(tokens17), "wifi request: a text of 17 tokens is refused with WEB_WIFI_TOKENS tokens");
	check(wifi_parse(tokens17, strlen(tokens17), large_work, 512) && request_is("Home", "12345678", true, "h"), "wifi request: a text of 17 tokens is read with a larger room");
	all = true;
	for(count = -2; count <= 512; count++)
	{
		if(wifi_parse(full, strlen(full), large_work, count) != (count >= 7)) all = false;
		if(count >= 7 ? !request_is("Home", "12345678", true, "h") : !all_guard(&request_box, sizeof(request_box))) all = false;
	}
	check(all, "wifi request: with every room from -2 to 512 tokens the text of 7 tokens is taken from 7 on and refused below");
	memset(&work[6], 0x5A, 2 * sizeof(work[0]));
	check(!wifi_parse(full, strlen(full), work, 6) && work[6].start == 0x5A5A5A5A && work[7].start == 0x5A5A5A5A,
	      "wifi request: one token less than the text needs: refused, nothing written behind the room of the reader");
	memset(&work[0], 0x5A, 2 * sizeof(work[0]));
	check(!wifi_parse("{}", 2, work, 0) && work[0].start == 0x5A5A5A5A && !wifi_parse("{}", 2, work, -1) && work[0].start == 0x5A5A5A5A,
	      "wifi request: no room for tokens or a negative one: refused, nothing written");

	// A long text
	snprintf(text, sizeof(text), "%s{\"later\":[%s0],%s\"ssid\":\"Home\"%s}", times(" ", 500), times("1,", 150), times("\n", 500), times("\t", 500));
	check(strlen(text) > 1800 && wifi_parse(text, strlen(text), large_work, 512) && request_is("Home", "", false, ""),
	      "wifi request: a text of more than 1800 bytes with a list of 151 numbers in front of the ssid is read");

	// Many members
	at = (size_t)snprintf(text, sizeof(text), "{");
	for(count = 0; count < 300; count++) at += (size_t)snprintf(text + at, sizeof(text) - at, "\"later%d\":%d,", count, count);
	snprintf(text + at, sizeof(text) - at, "\"host\":\"h\",\"password\":\"12345678\",\"ssid\":\"Home\"}");
	check(json_parse(text, strlen(text), huge_work, 2000) == 607 && wifi_parse(text, strlen(text), huge_work, 2000) && request_is("Home", "12345678", true, "h"),
	      "wifi request: host, password and ssid behind 300 members of a later firmware are read, 607 tokens");
	check(forget_parse(text, strlen(text), huge_work, 2000) && strcmp(ssid_box.ssid, "Home") == 0, "forget request: the ssid behind 302 other members is read");

	// A text that needs exactly as many tokens as the largest room here has
	at = (size_t)snprintf(text, sizeof(text), "{\"later\":[");
	for(count = 0; count < 1993; count++) at += (size_t)snprintf(text + at, sizeof(text) - at, "%s%d", count ? "," : "", count % 10);
	snprintf(text + at, sizeof(text) - at, "],\"ssid\":\"Home\",\"host\":\"h\"}");
	check(json_parse(text, strlen(text), huge_work, 2000) == 2000 && wifi_parse(text, strlen(text), huge_work, 2000) && request_is("Home", "", false, "h") &&
	      forget_parse(text, strlen(text), huge_work, 2000) && strcmp(ssid_box.ssid, "Home") == 0, "requests: a text of 2000 tokens is read with a room of 2000 tokens");
	check(!wifi_parse(text, strlen(text), huge_work, 1999) && all_guard(&request_box, sizeof(request_box)) && !forget_parse(text, strlen(text), huge_work, 1999) &&
	      all_guard(&ssid_box, sizeof(ssid_box)), "requests: a text of 2000 tokens is refused with a room of 1999 tokens");

	// A text longer than 16 bit can count: 69900 blanks in front of the object
	memset(huge_text, ' ', sizeof(huge_text));
	memcpy(huge_text + 69900, full, strlen(full));
	check(wifi_parse(huge_text, 69900 + strlen(full), work, WEB_WIFI_TOKENS) && request_is("Home", "12345678", true, "h") &&
	      forget_parse(huge_text, 69900 + strlen(full), work, WEB_WIFI_TOKENS) && strcmp(ssid_box.ssid, "Home") == 0,
	      "requests: a text of 69949 bytes is read up to its end");
	huge_text[69900 + 9] = '\\';
	huge_text[69900 + 10] = 'n';
	check(!wifi_parse(huge_text, 69900 + strlen(full), work, WEB_WIFI_TOKENS) && all_guard(&request_box, sizeof(request_box)) &&
	      !forget_parse(huge_text, 69900 + strlen(full), work, WEB_WIFI_TOKENS) && all_guard(&ssid_box, sizeof(ssid_box)),
	      "requests: an ssid with a line break behind 69900 bytes is refused");

	// A text of 1207 tokens and more than 12000 bytes: a list of 1200 numbers and 8000 blanks in front of the members
	at = (size_t)snprintf(text, sizeof(text), "{\"later\":[");
	for(count = 0; count < 1200; count++) at += (size_t)snprintf(text + at, sizeof(text) - at, "%s%d", count ? "," : "", count);
	at += (size_t)snprintf(text + at, sizeof(text) - at, "],");
	memset(text + at, ' ', 8000);
	at += 8000;
	snprintf(text + at, sizeof(text) - at, "\"ssid\":\"Home\",\"host\":\"h\"}");
	check(strlen(text) > 12000 && json_parse(text, strlen(text), huge_work, 2000) == 1207 && wifi_parse(text, strlen(text), huge_work, 2000) &&
	      request_is("Home", "", false, "h"), "wifi request: a text of 1207 tokens and more than 12000 bytes is read with a room of 2000 tokens");
	check(forget_parse(text, strlen(text), huge_work, 2000) && strcmp(ssid_box.ssid, "Home") == 0,
	      "forget request: a text of 1207 tokens and more than 12000 bytes is read with a room of 2000 tokens");
	check(!wifi_parse(text, strlen(text), huge_work, 1206) && all_guard(&request_box, sizeof(request_box)) && !forget_parse(text, strlen(text), huge_work, 1206) &&
	      all_guard(&ssid_box, sizeof(ssid_box)), "requests: the text of 1207 tokens is refused with a room of 1206");

	// Every byte value in every text member: the bytes below 0x20 are refused, all others taken
	all = true;
	for(count = 1; count <= 255; count++)
	{
		char read[16];
		char written[24];

		// Eight bytes in front, so that it is a password as well
		snprintf(read, sizeof(read), "12345678%c", count);
		if(count < 0x80) snprintf(written, sizeof(written), "12345678\\u%04x", (unsigned)count);
		else snprintf(written, sizeof(written), "%s", read);

		snprintf(text, sizeof(text), "{\"ssid\":\"%s\"}", written);
		if(count >= 0x20 ? !wifi_taken(text, read, "", false, "") : !wifi_refused(text)) all = false;
		if(count >= 0x20 ? !forget_taken(text, read) : !forget_refused(text)) all = false;
		snprintf(text, sizeof(text), "{\"ssid\":\"Home\",\"password\":\"%s\"}", written);
		if(count >= 0x20 ? !wifi_taken(text, "Home", read, true, "") : !wifi_refused(text)) all = false;
		snprintf(text, sizeof(text), "{\"ssid\":\"Home\",\"host\":\"%s\"}", written);
		if(count >= 0x20 ? !wifi_taken(text, "Home", "", false, read) : !wifi_refused(text)) all = false;
	}
	check(all, "requests: every byte value at the end of an ssid, a password and a host: 0x01 to 0x1F are refused, 0x20 to 0xFF are taken");
	all = true;
	for(count = 1; count <= 255; count++)
	{
		char read[16];
		char written[24];

		snprintf(read, sizeof(read), "%c2345678", count);
		if(count < 0x80) snprintf(written, sizeof(written), "\\u%04x2345678", (unsigned)count);
		else snprintf(written, sizeof(written), "%s", read);

		snprintf(text, sizeof(text), "{\"ssid\":\"%s\"}", written);
		if(count >= 0x20 ? !wifi_taken(text, read, "", false, "") : !wifi_refused(text)) all = false;
		if(count >= 0x20 ? !forget_taken(text, read) : !forget_refused(text)) all = false;
		snprintf(text, sizeof(text), "{\"ssid\":\"Home\",\"password\":\"%s\"}", written);
		if(count >= 0x20 ? !wifi_taken(text, "Home", read, true, "") : !wifi_refused(text)) all = false;
		snprintf(text, sizeof(text), "{\"ssid\":\"Home\",\"host\":\"%s\"}", written);
		if(count >= 0x20 ? !wifi_taken(text, "Home", "", false, read) : !wifi_refused(text)) all = false;
	}
	check(all, "requests: every byte value at the beginning of an ssid, a password and a host: 0x01 to 0x1F are refused, 0x20 to 0xFF are taken");

	// The rules of the password do not depend on the network
	check(wifi_refused("{\"ssid\":\"WiCAN_a1b2c3\",\"password\":\"1234567\"}") && wifi_refused("{\"password\":\"1234567\",\"ssid\":\"WiCAN_a1b2c3\"}"),
	      "wifi request: a password of 7 bytes is refused for the access point of a WiCAN as well");
	check(wifi_taken("{\"ssid\":\"WiCAN_a1b2c3\",\"password\":\"@meatpi#\"}", "WiCAN_a1b2c3", "@meatpi#", true, ""),
	      "wifi request: the password of the documentation is a password like every other");

	// Every length around the limits
	all = true;
	for(count = 0; count <= 40; count++)
	{
		snprintf(text, sizeof(text), "{\"ssid\":\"%s\"}", times("s", count));
		if(count >= 1 && count <= 32 ? !wifi_taken(text, times("s", count), "", false, "") : !wifi_refused(text)) all = false;
		if(count >= 1 && count <= 32 ? !forget_taken(text, times("s", count)) : !forget_refused(text)) all = false;
	}
	check(all, "requests: of the SSIDs of 0 to 40 bytes those of 1 to 32 bytes are taken, the others refused");
	all = true;
	for(count = 0; count <= 72; count++)
	{
		snprintf(text, sizeof(text), "{\"ssid\":\"Home\",\"password\":\"%s\"}", times("p", count));
		if(count == 0 || (count >= 8 && count <= 64) ? !wifi_taken(text, "Home", times("p", count), true, "") : !wifi_refused(text)) all = false;
	}
	check(all, "wifi request: of the passwords of 0 to 72 bytes those of 0 and of 8 to 64 bytes are taken, the others refused");
	all = true;
	for(count = 0; count <= 48; count++)
	{
		snprintf(text, sizeof(text), "{\"ssid\":\"Home\",\"host\":\"%s\"}", times("h", count));
		if(count <= 39 ? !wifi_taken(text, "Home", "", false, times("h", count)) : !wifi_refused(text)) all = false;
	}
	check(all, "wifi request: of the hosts of 0 to 48 bytes those of up to 39 bytes are taken, the others refused");
}

static void test_forget_request(void)
{
	static const struct
	{
		const char *json;
		const char *what;
	} refused[] = {
		{"{\"ssid\":\"Home\"}x", "forget request: text behind the object is refused"},
		{"", "forget request: an empty text is refused"},
		{" ", "forget request: a space is refused"},
		{"{\"ssid\":\"Home\"", "forget request: an object that is not closed is refused"},
		{"{\"ssid\":\"Home\",\"x\":[[[[[[[[1]]]]]]]]}", "forget request: nested deeper than the reader follows is refused"},
		{"[]", "forget request: an empty list is refused"},
		{"[\"ssid\",\"Home\"]", "forget request: a list of a name and a value is refused"},
		{"\"Home\"", "forget request: a text alone is refused"},
		{"null", "forget request: null is refused"},
		{"{}", "forget request: an object without ssid is refused"},
		{"{\"password\":\"12345678\",\"host\":\"h\"}", "forget request: password and host without ssid are refused"},
		{"{\"SSID\":\"Home\"}", "forget request: SSID in capitals is not the ssid"},
		{"{\"ssid2\":\"Home\"}", "forget request: ssid2 is not the ssid"},
		{"{\"ssid\":\"\"}", "forget request: an empty SSID is refused"},
		{"{\"ssid\":5,\"ssid\":\"Home\"}", "forget request: an ssid that is no text is not healed by a good one behind it"},
		{"{\"ssid\":\"Home\",\"ssid\":\"\"}", "forget request: a good ssid followed by an empty one is refused"},
	};
	static const char tokens16[] = "{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":[6],\"ssid\":\"Home\"}";
	static const char tokens17[] = "{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"ssid\":\"Home\"}";
	size_t i;
	int count;
	bool all;

	check(read_fixture("fixtures/web_forget_request.json", fixture, sizeof(fixture)) && is_json_object(fixture) && forget_taken(fixture, "Camping"),
	      "forget request: the fixture is read, the 33 bytes of the ssid are written with zeros behind the text");
	check(forget_taken("{\"ssid\":\"x\"}", "x"), "forget request: an ssid of one byte");
	check(forget_taken(" { \"ssid\" :\n\"Home\" } ", "Home"), "forget request: whitespace between the parts is allowed");
	check(forget_taken("{\"\\u0073sid\":\"Home\"}", "Home"), "forget request: the name written with an escape is the ssid");
	check(forget_taken("{\"ssid\":\"First\",\"ssid\":\"Home\"}", "Home"), "forget request: of two ssid members the last counts");
	check(forget_taken("{\"ssid\":\"A longer first name\",\"ssid\":\"x\"}", "x"), "forget request: nothing of a longer first ssid stays behind a shorter second one");
	check(forget_taken("{\"ssid\":\"Home\",\"password\":\"1\",\"host\":5,\"x\":null}", "Home"),
	      "forget request: password and host are unknown members here: a short password and a host that is no text do not matter");
	check(forget_taken("{\"password\":null,\"host\":\"a\\nb\",\"x\":{\"ssid\":\"\"},\"ssid\":\"Home\"}", "Home"),
	      "forget request: unknown members in front of the ssid are ignored, whatever they hold");

	for(i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) check(forget_refused(refused[i].json), refused[i].what);

	check(forget_parse("{\"ssid\":\"Home\"}x", 15, work, WEB_WIFI_TOKENS) && strcmp(ssid_box.ssid, "Home") == 0, "forget request: bytes behind the given length are not read");
	check(!forget_parse("{\"ssid\":\"Home\"}", 14, work, WEB_WIFI_TOKENS) && all_guard(&ssid_box, sizeof(ssid_box)),
	      "forget request: a length that ends before the brace is refused");

	check(json_parse(tokens16, strlen(tokens16), large_work, 512) == 16 && json_parse(tokens17, strlen(tokens17), large_work, 512) == 17,
	      "the test texts for forgetting have 16 and 17 tokens");
	check(forget_taken(tokens16, "Home"), "forget request: a text of 16 tokens is read with WEB_WIFI_TOKENS tokens");
	check(forget_refused(tokens17), "forget request: a text of 17 tokens is refused with WEB_WIFI_TOKENS tokens");
	all = true;
	for(count = -2; count <= 512; count++)
	{
		if(forget_parse("{\"ssid\":\"Home\"}", 15, large_work, count) != (count >= 3)) all = false;
		if(count >= 3 ? strcmp(ssid_box.ssid, "Home") != 0 : !all_guard(&ssid_box, sizeof(ssid_box))) all = false;
	}
	check(all, "forget request: with every room from -2 to 512 tokens the text of 3 tokens is taken from 3 on and refused below");
}

/*
 * The rules of the two readers written a second time: the text is made member by member from a list that
 * says of each member what it is, and the outcome is told from the list alone.
 */

#define LIST_MAX 8

typedef struct
{
	int name;           // 0 ssid, 1 password, 2 host, 3 unknown
	bool good;          // for POST /api/wifi
	char read[80];      // of a good text
} listed_t;

static int seen_taken, seen_refused, seen_no_ssid, seen_twice, seen_forget_taken;

// The value of one member as JSON; the list entry says what the reader has to make of it
static void write_member_value(char *to, size_t size, listed_t *entry)
{
	static const char *const no_text[] = {"5", "null", "true", "false", "[\"abcdefgh\"]", "{\"ssid\":\"abcdefgh\"}"};
	static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_.";
	int limit = entry->name == 0 ? 32 : entry->name == 1 ? 64 : 39;
	uint32_t pick = rnd(20);
	int length, i;

	entry->good = true;
	entry->read[0] = '\0';
	if(entry->name == 3)
	{
		snprintf(to, size, "%s", rnd(2) ? no_text[rnd(6)] : "\"x\"");
		return;
	}

	if(pick == 0)
	{
		entry->good = false;
		snprintf(to, size, "%s", no_text[rnd(6)]);
		return;
	}
	if(pick < 12) length = 1 + (int)rnd((uint32_t)limit);
	else if(pick == 12) length = limit;
	else if(pick == 13) length = limit + 1 + (int)rnd(3);
	else if(pick == 14) length = 0;
	else if(pick == 15) length = (int)rnd(10);
	else length = 8 + (int)rnd(8);

	for(i = 0; i < length; i++) entry->read[i] = letters[rnd(sizeof(letters) - 1)];
	entry->read[length] = '\0';

	if(length > limit) entry->good = false;
	if(entry->name == 0 && length == 0) entry->good = false;
	if(entry->name == 1 && length >= 1 && length <= 7) entry->good = false;

	if(pick == 16 || pick == 17)
	{
		// A control character or an escape the reader refuses, somewhere in the text
		static const char *const bad[] = {"\\u0001", "\\n", "\\u0000", "\\ud83d", "\\u001f", "\\t"};
		int at = (int)rnd((uint32_t)length + 1);

		entry->good = false;
		snprintf(to, size, "\"%.*s%s%s\"", at, entry->read, bad[rnd(6)], entry->read + at);
		return;
	}
	snprintf(to, size, "\"%s\"", entry->read);
}

static bool request_walk(uint32_t seed, int steps)
{
	listed_t list[LIST_MAX];
	int step;

	random_state = seed;
	for(step = 0; step < steps; step++)
	{
		static const char *const names[3] = {"ssid", "password", "host"};
		int count = 1 + (int)rnd(LIST_MAX);
		bool broken = rnd(15) == 0;
		size_t length = 0;
		int last[3] = {-1, -1, -1};
		bool every_good = true, ssid_good = true;
		bool expected_wifi, expected_forget, result;
		int i;

		length += (size_t)snprintf(text + length, sizeof(text) - length, "{");
		for(i = 0; i < count; i++)
		{
			char value[200];

			// The SSID more often than the others, so that most texts have one
			list[i].name = rnd(3) == 0 ? 0 : (int)rnd(4);
			write_member_value(value, sizeof(value), &list[i]);
			if(list[i].name == 3) length += (size_t)snprintf(text + length, sizeof(text) - length, "%s\"u%u\":%s", i ? "," : "", (unsigned)rnd(3), value);
			else length += (size_t)snprintf(text + length, sizeof(text) - length, "%s%s\"%s\":%s", i ? "," : "", rnd(6) == 0 ? " " : "", names[list[i].name], value);

			if(list[i].name < 3)
			{
				if(last[list[i].name] >= 0) seen_twice++;
				last[list[i].name] = i;
				if(!list[i].good) every_good = false;
			}
			// For forgetting only the SSID is looked at; its rules are those of the SSID of a request
			if(list[i].name == 0 && !list[i].good) ssid_good = false;
		}
		length += (size_t)snprintf(text + length, sizeof(text) - length, "%s", broken ? (rnd(2) ? "}x" : "") : "}");

		expected_wifi = !broken && every_good && last[0] >= 0;
		expected_forget = !broken && ssid_good && last[0] >= 0;
		if(expected_wifi) seen_taken++;
		else if(!broken && every_good) seen_no_ssid++;
		else seen_refused++;
		if(expected_forget) seen_forget_taken++;

		result = wifi_parse(text, length, large_work, 512);
		if(result != expected_wifi ||
		   (expected_wifi ? !request_is(list[last[0]].read, last[1] >= 0 ? list[last[1]].read : "", last[1] >= 0, last[2] >= 0 ? list[last[2]].read : "")
		                  : !all_guard(&request_box, sizeof(request_box))))
		{
			printf("  seed %lu, step %d, wifi request: module %d, model %d: %s\n", (unsigned long)seed, step, result, expected_wifi, text);
			return false;
		}

		result = forget_parse(text, length, large_work, 512);
		if(result != expected_forget || (expected_forget ? strcmp(ssid_box.ssid, list[last[0]].read) != 0 || !all_guard(ssid_box.after, sizeof(ssid_box.after))
		                                                 : !all_guard(&ssid_box, sizeof(ssid_box))))
		{
			printf("  seed %lu, step %d, forget request: module %d, model %d: %s\n", (unsigned long)seed, step, result, expected_forget, text);
			return false;
		}
	}
	return true;
}

static void test_request_model(void)
{
	char what[128];
	uint32_t seed;

	for(seed = 1; seed <= 30; seed++)
	{
		snprintf(what, sizeof(what), "requests: 1000 random texts with seed %lu are taken or refused as the rules say", (unsigned long)seed);
		check(request_walk(seed, 1000), what);
	}
	printf("  wifi taken %d, refused %d, no ssid %d, a name twice %d, forget taken %d\n", seen_taken, seen_refused, seen_no_ssid, seen_twice, seen_forget_taken);
	check(seen_taken > 1000 && seen_refused > 1000 && seen_no_ssid > 300 && seen_twice > 1000 && seen_forget_taken > seen_taken + 1000,
	      "requests: the walks contain texts that are taken, refused, without ssid, names twice, and texts only the forget request takes");
}

/*
 * The answer to PUT /api/layout
 */

// layout_t is large: never on the stack
static layout_t layout;
static catalog_t catalog;
static layout_report_t report;

static void no_layout(const char *name)
{
	memset(&layout, 0, sizeof(layout));
	strcpy(layout.name, name);
}

// A page with these keys, separated by blanks
static void page(const char *keys, bool hidden)
{
	layout_page_t *added = &layout.pages[layout.page_count++];
	char copy[256];
	char *key;

	snprintf(copy, sizeof(copy), "%s", keys);
	added->hidden = hidden;
	for(key = strtok(copy, " "); key != NULL; key = strtok(NULL, " ")) strcpy(added->items[added->item_count++].key, key);
}

static void no_catalog(void)
{
	memset(&catalog, 0, sizeof(catalog));
}

static void entry(const char *name)
{
	strcpy(catalog.entries[catalog.count++].name, name);
}

static void no_report(void)
{
	memset(&report, 0, sizeof(report));
}

static bool report_gives(bool ok, const char *wanted)
{
	report_now.ok = ok;
	report_now.report = &report;
	report_now.layout = &layout;
	report_now.catalog = &catalog;
	return every_room(call_report, wanted);
}

// The list of unknown keys alone, of a layout named L without warnings
static bool unknown_gives(int pages, int items, const char *written)
{
	char wanted[1024];

	snprintf(wanted, sizeof(wanted), "{\"ok\":true,\"name\":\"L\",\"pages\":%d,\"items\":%d,\"warnings\":0,\"warning_path\":\"\",\"warning\":\"\",\"unknown\":[%s]}",
	         pages, items, written);
	no_report();
	return report_gives(true, wanted);
}

static void test_report(void)
{
	static const int warnings[] = {0, 1, 2, 9, 10, 72, 100, 1000, INT_MAX, -1, INT_MIN};
	char wanted[256];
	bool all;
	size_t i;
	int p;

	// Refused. Whatever a layout and a catalogue hold: they are not told
	no_layout("LAYOUT_NAME");
	page("UNKNOWN_KEY", false);
	no_catalog();
	entry("@BATT_V");
	entry("OTHER");
	no_report();
	strcpy(report.path, "pages[2].items[0].min");
	strcpy(report.problem, "min is not below max");
	report_now.ok = false;
	report_now.report = &report;
	report_now.layout = &layout;
	report_now.catalog = &catalog;
	check(gives_fixture(call_report, "fixtures/web_report_refused.json"),
	      "report: a refused layout gives the fixture of the header, with every room from 0 bytes on");
	no_report();
	strcpy(report.problem, "not valid JSON");
	check(every_room(call_report, "{\"ok\":false,\"path\":\"\",\"problem\":\"not valid JSON\"}"), "report: a text refused as a whole has an empty path");
	no_report();
	strcpy(report.path, "a\"b\\c\x01");
	strcpy(report.problem, "d\"e\\f\x7f\xc3\xa4");
	// What a taken layout would tell must not show up
	report.warnings = 3;
	strcpy(report.warning_path, "WARNING_PATH");
	strcpy(report.warning, "WARNING");
	check(every_room(call_report, "{\"ok\":false,\"path\":\"a\\\"b\\\\c\\u0001\",\"problem\":\"d\\\"e\\\\f\\u007f\xc3\xa4\"}"),
	      "report: path and problem of a refused layout are escaped, its warnings are not told");
	memset(report.path, 'p', sizeof(report.path) - 1);
	memset(report.problem, 'q', sizeof(report.problem) - 1);
	snprintf(wanted, sizeof(wanted), "{\"ok\":false,\"path\":\"%s\",\"problem\":\"%s\"}", times("p", 47), times("q", 63));
	check(every_room(call_report, wanted), "report: a path of 47 bytes and a problem of 63 bytes are written completely");

	// Taken
	no_report();
	no_layout("W906 OM651 Standard");
	page("ENGINE_RPM COOLANT_TMP RAIL_PRESSURE", false);
	page("DPF_SOOT_MASS ENGINE_RPM", true);
	no_catalog();
	entry("@BATT_V");
	entry("ENGINE_RPM");
	entry("COOLANT_TMP");
	entry("RAIL_PRESSURE");
	entry("DPF_SOOT_MASS");
	report_now.ok = true;
	report_now.report = &report;
	report_now.layout = &layout;
	report_now.catalog = &catalog;
	check(gives_fixture(call_report, "fixtures/web_report_taken.json"),
	      "report: a layout whose keys are all in the catalogue gives the fixture: two pages, five items, nothing unknown");
	// A problem that was left in the report must not show up
	strcpy(report.path, "PATH");
	strcpy(report.problem, "PROBLEM");
	check(gives_fixture(call_report, "fixtures/web_report_taken.json"), "report: path and problem are not told of a layout that was taken");

	no_report();
	report.warnings = 2;
	strcpy(report.warning_path, "pages[1].items[0].widget");
	strcpy(report.warning, "arc or bar without min and max, shown as number");
	no_layout("Eigene Ansicht");
	page("ENGINE_RPM OIL_PRESSURE COOLANT_TMP", false);
	page("TURBO_SPEED OIL_PRESSURE @BATT_V", false);
	page("GEAR TURBO_SPEED", true);
	no_catalog();
	entry("@BATT_V");
	entry("ENGINE_RPM");
	entry("COOLANT_TMP");
	check(gives_fixture(call_report, "fixtures/web_report_unknown.json"),
	      "report: warnings and three unknown keys, two of them used twice, give the fixture: each key once, in the order of first use");

	// The catalogue
	no_layout("L");
	page("A B", false);
	page("C", false);
	no_catalog();
	entry("@BATT_V");
	check(unknown_gives(2, 3, ""), "report: a catalogue that holds nothing but the battery voltage is not loaded: nothing is unknown");
	no_catalog();
	check(unknown_gives(2, 3, ""), "report: an empty catalogue is not loaded: nothing is unknown");
	no_catalog();
	entry("@BATT_V");
	entry("@BATT_V");
	check(unknown_gives(2, 3, ""), "report: a catalogue that holds the battery voltage twice is not loaded");
	no_catalog();
	for(p = 0; p < CATALOG_MAX; p++) entry("@BATT_V");
	check(unknown_gives(2, 3, ""), "report: a full catalogue that holds nothing but the battery voltage is not loaded");
	catalog.count = 0;
	strcpy(catalog.entries[CATALOG_MAX - 1].name, "B");
	catalog.count = CATALOG_MAX;
	check(unknown_gives(2, 3, "\"A\",\"C\""), "report: a full catalogue whose last entry is another value is loaded, and that value is found");
	no_catalog();
	entry("@BATT_V");
	entry("B");
	check(unknown_gives(2, 3, "\"A\",\"C\""), "report: a catalogue with one more value is loaded: the other keys are unknown");
	no_catalog();
	entry("B");
	check(unknown_gives(2, 3, "\"A\",\"C\""), "report: a catalogue without the battery voltage is loaded as well");
	no_catalog();
	entry("@BATT_V");
	entry("X");
	check(unknown_gives(2, 3, "\"A\",\"B\",\"C\""), "report: no key in the catalogue: all are unknown, in the order of the pages");
	no_catalog();
	entry("@BATT_V");
	entry("C");
	entry("B");
	entry("A");
	check(unknown_gives(2, 3, ""), "report: the last entries of the catalogue are found as well");
	no_catalog();
	entry("@BATT_V");
	entry("a");
	entry("AB");
	entry("");
	check(unknown_gives(2, 3, "\"A\",\"B\",\"C\""), "report: a key is compared completely and with its case");
	no_catalog();
	entry("@BATT_V");
	entry("@BATT_V2");
	check(unknown_gives(2, 3, "\"A\",\"B\",\"C\""), "report: a catalogue with a value whose name begins with that of the battery voltage is loaded");
	no_catalog();
	entry("@BATT_X");
	check(unknown_gives(2, 3, "\"A\",\"B\",\"C\""), "report: a catalogue with a value whose name begins like that of the battery voltage is loaded");
	no_catalog();
	entry("@BATT_V");
	entry("@BATT_");
	check(unknown_gives(2, 3, "\"A\",\"B\",\"C\""), "report: a catalogue with a value whose name is the beginning of that of the battery voltage is loaded");
	no_catalog();
	entry("@BATT_V");
	entry("X");
	no_layout("L");
	page("@BATT_V @BATT_V2 @BATT", false);
	check(unknown_gives(1, 3, "\"@BATT_V2\",\"@BATT\""), "report: the battery voltage is in a loaded catalogue, names that look like it are not");

	// Each key once, in the order of first use
	no_catalog();
	entry("@BATT_V");
	entry("K");
	no_layout("L");
	page("A A", false);
	check(unknown_gives(1, 2, "\"A\""), "report: a key used twice on one page is listed once");
	no_layout("L");
	page("A B", false);
	page("B A", false);
	check(unknown_gives(2, 4, "\"A\",\"B\""), "report: keys used again on a later page are listed once, in the order of their first use");
	no_layout("L");
	page("B K A", false);
	page("K C B", false);
	page("A K", false);
	page("D", false);
	check(unknown_gives(4, 9, "\"B\",\"A\",\"C\",\"D\""), "report: known keys between unknown ones leave no comma");
	no_layout("L");
	page("A AB ABC", false);
	page("AB A B", false);
	check(unknown_gives(2, 6, "\"A\",\"AB\",\"ABC\",\"B\""), "report: keys that begin alike are different keys");
	no_layout("L");
	page("ABC AB A", false);
	page("B AB", false);
	check(unknown_gives(2, 5, "\"ABC\",\"AB\",\"A\",\"B\""), "report: a key that is the beginning of an earlier one is a key of its own");
	no_layout("L");
	page("a A", false);
	page("A a b B", false);
	check(unknown_gives(2, 6, "\"a\",\"A\",\"b\",\"B\""), "report: keys that differ only in the case of their letters are different keys");
	no_layout("L");
	page("X", true);
	page("K", false);
	page("Y X", true);
	check(unknown_gives(3, 4, "\"X\",\"Y\""), "report: hidden pages count and their keys are looked at like the others");
	no_layout("L");
	page("K", false);
	check(unknown_gives(1, 1, ""), "report: one page with one known key");
	no_layout("L");
	check(unknown_gives(0, 0, ""), "report: a layout without pages: 0 pages, 0 items");
	no_layout("L");
	page("A K", false);
	page("B C", false);
	page("D", false);
	layout.page_count = 2;
	layout.pages[1].item_count = 1;
	check(unknown_gives(2, 3, "\"A\",\"B\""), "report: a page behind the page count and an item behind the item count of a page are neither counted nor listed");
	layout.pages[0].item_count = 1;
	strcpy(layout.pages[0].items[1].key, "B");
	check(unknown_gives(2, 2, "\"A\",\"B\""), "report: a key behind the item count of an earlier page is not a key that was used before");

	// The largest layout: 12 pages of 6 items
	no_layout("L");
	for(p = 0; p < LAYOUT_PAGES_MAX; p++) page("K U1 K U2 K K", p % 2 == 1);
	check(unknown_gives(12, 72, "\"U1\",\"U2\""), "report: 12 pages of 6 items are 72 items");
	no_layout("L");
	for(p = 0; p < LAYOUT_PAGES_MAX; p++)
	{
		snprintf(wanted, sizeof(wanted), "K P%dA P%dB", p, p);
		page(wanted, false);
	}
	check(unknown_gives(12, 36, "\"P0A\",\"P0B\",\"P1A\",\"P1B\",\"P2A\",\"P2B\",\"P3A\",\"P3B\",\"P4A\",\"P4B\",\"P5A\",\"P5B\",\"P6A\",\"P6B\",\"P7A\",\"P7B\","
	                           "\"P8A\",\"P8B\",\"P9A\",\"P9B\",\"P10A\",\"P10B\",\"P11A\",\"P11B\""),
	      "report: unknown keys of every one of 12 pages are listed");

	// Texts
	no_report();
	report.warnings = 1;
	strcpy(report.warning_path, "p\"a\\t\x01h");
	strcpy(report.warning, "w\"a\\r\x7fn \xc3\xa4");
	no_layout("N\"a\\m\x1f" "e \xc3\x96l");
	page("K\"e\\y\x02", false);
	check(report_gives(true, "{\"ok\":true,\"name\":\"N\\\"a\\\\m\\u001fe \xc3\x96l\",\"pages\":1,\"items\":1,\"warnings\":1,\"warning_path\":\"p\\\"a\\\\t\\u0001h\","
	                   "\"warning\":\"w\\\"a\\\\r\\u007fn \xc3\xa4\",\"unknown\":[\"K\\\"e\\\\y\\u0002\"]}"),
	      "report: name, warning path, warning and unknown keys are escaped");

	all = true;
	no_layout("L");
	page("K", false);
	for(i = 0; i < sizeof(warnings) / sizeof(warnings[0]); i++)
	{
		no_report();
		report.warnings = warnings[i];
		snprintf(wanted, sizeof(wanted), "{\"ok\":true,\"name\":\"L\",\"pages\":1,\"items\":1,\"warnings\":%d,\"warning_path\":\"\",\"warning\":\"\",\"unknown\":[]}", warnings[i]);
		if(!report_gives(true, wanted)) all = false;
	}
	check(all, "report: the number of warnings is written as it is, from the smallest to the largest int");
	no_report();
	report.warnings = -2147483647 - 1;
	check(report_gives(true, "{\"ok\":true,\"name\":\"L\",\"pages\":1,\"items\":1,\"warnings\":-2147483648,\"warning_path\":\"\",\"warning\":\"\",\"unknown\":[]}"),
	      "report: the smallest number of warnings an int can hold is written completely");
}

// The longest reports: WEB_REPORT_SIZE is enough for them
static void test_report_size(void)
{
	size_t length;
	int p, i, at;

	// Texts that need no escape
	no_report();
	report.warnings = -2147483647 - 1;
	memset(report.warning_path, 'p', sizeof(report.warning_path) - 1);
	memset(report.warning, 'w', sizeof(report.warning) - 1);
	memset(&layout, 0, sizeof(layout));
	memset(layout.name, 'n', sizeof(layout.name) - 1);
	layout.page_count = LAYOUT_PAGES_MAX;
	for(p = 0; p < LAYOUT_PAGES_MAX; p++)
	{
		layout.pages[p].item_count = LAYOUT_ITEMS_MAX;
		for(i = 0; i < LAYOUT_ITEMS_MAX; i++) snprintf(layout.pages[p].items[i].key, VALUE_NAME_SIZE, "KEY_OF_32_BYTES_%02d_%d_ABCDEFGHIJK", p, i);
	}
	no_catalog();
	entry("@BATT_V");
	entry("K");
	// {"ok":true,"name":".." ,"pages":12 ,"items":72 ,"warnings":-2147483648 ,"warning_path":".." ,"warning":".." ,"unknown":[ keys commas ]}
	length = 18 + (2 + 32) + 9 + 2 + 9 + 2 + 12 + 11 + 16 + (2 + 47) + 11 + (2 + 63) + 12 + 72 * (2 + 32) + 71 + 2;
	check(strlen(layout.pages[11].items[5].key) == 32 && length == 2771, "the longest report without escapes has 2771 bytes");
	model_report(true, &report, &layout, &catalog);
	report_now.ok = true;
	report_now.report = &report;
	report_now.layout = &layout;
	report_now.catalog = &catalog;
	check(strlen(expected) == 2771 && rooms_around(call_report, expected) && is_json_object(expected),
	      "report: 72 unknown keys of 32 bytes, name, path and warning as long as their fields give 2771 bytes");

	// What layout_parse() lets through: quotes and backslashes in the name and the keys, which become two bytes
	memset(layout.name, '"', sizeof(layout.name) - 1);
	for(p = 0; p < LAYOUT_PAGES_MAX; p++)
	{
		for(i = 0; i < LAYOUT_ITEMS_MAX; i++)
		{
			char *key = layout.pages[p].items[i].key;

			memset(key, '\\', VALUE_NAME_SIZE - 1);
			// 72 different keys
			key[p] = '"';
			key[12 + i] = '"';
		}
	}
	length = 18 + (2 + 32 * 2) + 9 + 2 + 9 + 2 + 12 + 11 + 16 + (2 + 47) + 11 + (2 + 63) + 12 + 72 * (2 + 32 * 2) + 71 + 2;
	check(length == 5107, "the longest report of a layout without control characters has 5107 bytes");
	model_report(true, &report, &layout, &catalog);
	check(strlen(expected) == 5107 && rooms_around(call_report, expected) && is_json_object(expected),
	      "report: 72 unknown keys and a name of quotes and backslashes give 5107 bytes");

	// Every byte of every text a control character, which becomes six bytes
	memset(report.warning_path, 0x01, sizeof(report.warning_path) - 1);
	memset(report.warning, 0x7F, sizeof(report.warning) - 1);
	memset(layout.name, 0x1F, sizeof(layout.name) - 1);
	for(p = 0; p < LAYOUT_PAGES_MAX; p++)
	{
		for(i = 0; i < LAYOUT_ITEMS_MAX; i++)
		{
			char *key = layout.pages[p].items[i].key;

			for(at = 0; at < VALUE_NAME_SIZE - 1; at++) key[at] = (char)(1 + (p * LAYOUT_ITEMS_MAX + i + at) % 31);
			key[0] = (char)(1 + p);
			key[1] = (char)(1 + i);
		}
	}
	length = 18 + (2 + 32 * 6) + 9 + 2 + 9 + 2 + 12 + 11 + 16 + (2 + 47 * 6) + 11 + (2 + 63 * 6) + 12 + 72 * (2 + 32 * 6) + 71 + 2;
	check(length == 15001, "the longest report of all, with every byte of every text escaped, has 15001 bytes");
	// The texts are longer than the model writes in one piece: the head by hand, the keys by the model
	at = snprintf(text, sizeof(text), "{\"ok\":true,\"name\":\"%s\",\"pages\":12,\"items\":72,\"warnings\":-2147483648,\"warning_path\":\"", times("\\u001f", 32));
	at += snprintf(text + at, sizeof(text) - (size_t)at, "%s\",\"warning\":\"", times("\\u0001", 47));
	at += snprintf(text + at, sizeof(text) - (size_t)at, "%s\",\"unknown\":[", times("\\u007f", 63));
	report.warning_path[0] = '\0';
	report.warning[0] = '\0';
	layout.name[0] = '\0';
	model_report(true, &report, &layout, &catalog);
	snprintf(text + at, sizeof(text) - (size_t)at, "%s", strstr(expected, "\"unknown\":[") + 11);
	memset(report.warning_path, 0x01, 1);
	memset(report.warning, 0x7F, 1);
	memset(layout.name, 0x1F, 1);
	check(strlen(text) == 15001 && is_json_object(text) && count_of(text, "\\u00") == 32 + 47 + 63 + 72 * 32,
	      "the longest report of all is built by the test, 15001 bytes of JSON");
	check(rooms_around(call_report, text), "report: the longest report of all is written, and refused with a room of 15001 bytes or less");
	check(room_is_right(call_report, text, WEB_REPORT_SIZE) && WEB_REPORT_SIZE >= 15001 + 1,
	      "report: WEB_REPORT_SIZE is enough for the largest layout with the longest texts, all of them escaped");

	// The refused report is far smaller
	memset(report.path, 0x01, sizeof(report.path) - 1);
	memset(report.problem, 0x02, sizeof(report.problem) - 1);
	snprintf(text, sizeof(text), "{\"ok\":false,\"path\":\"%s\",\"problem\":\"%s\"}", times("\\u0001", 47), times("\\u0002", 63));
	report_now.ok = false;
	check(strlen(text) == 695 && rooms_around(call_report, text), "report: the longest report of a refused layout has 695 bytes");
}

/*
 * Every text member of every answer with every byte value: it is written as the formats written a second time say,
 * the answer is JSON for the reader of the project, and the member comes back from that reader as it was
 */

typedef enum
{
	MEMBER_INFO,            // a text of web_info_t, at `offset`
	MEMBER_VIEW,
	MEMBER_VALUE_NAME,
	MEMBER_CURRENT,
	MEMBER_PROFILE_SSID,
	MEMBER_PROFILE_HOST,
	MEMBER_SEEN_SSID,
	MEMBER_PATH,
	MEMBER_PROBLEM,
	MEMBER_LAYOUT_NAME,
	MEMBER_WARNING_PATH,
	MEMBER_WARNING,
	MEMBER_KEY,
} member_kind_t;

typedef struct
{
	member_kind_t kind;
	size_t offset;
	const char *path;       // where the reader finds it: names with dots between them, [1] for the second element of
	                        // a list, # for the name of the first member of an object
	size_t fits;            // bytes the member holds at most; 60 where it is a text of any length
	const char *name;
} text_member_t;

// Index of the token at a path, -1 if there is none
static int token_at(const char *json, const char *path)
{
	int index = 0;

	while(*path != '\0' && index >= 0)
	{
		char name[32];
		size_t length = strcspn(path, ".[");

		if(*path == '#') return tokens[index].type == JSON_OBJECT && tokens[index].size > 0 ? index + 1 : -1;
		if(*path == '[')
		{
			index = json_element(tokens, index, atoi(path + 1));
			path = strchr(path, ']') + 1;
		}
		else
		{
			memcpy(name, path, length);
			name[length] = '\0';
			index = json_member(json, tokens, index, name);
			path += length;
		}
		if(*path == '.') path++;
	}
	return index;
}

// Puts the text into the member of an answer that has other members and other entries around it, and writes the
// answer a second time into `expected`. Returns the writer to call.
static call_t member_set(const text_member_t *member, const char *chunk)
{
	static web_info_t info;
	member_kind_t kind = member->kind;

	if(kind == MEMBER_INFO)
	{
		memset(&info, 0, sizeof(info));
		info.version = "0.1.0";
		info.heat = "normal";
		info.view = "live";
		info.settings = "{\"brightness\":80}";
		memcpy((char *)&info + member->offset, &chunk, sizeof(chunk));
		info_now = &info;
		model_info(&info);
		return call_info;
	}
	if(kind == MEMBER_VIEW || kind == MEMBER_VALUE_NAME)
	{
		no_values();
		add_value(kind == MEMBER_VALUE_NAME ? chunk : "FIRST", VALUE_NUMBER, 1, 1000);
		add_value("SECOND", VALUE_ON, 0, 1000);
		values_now.values = &values;
		values_now.view = kind == MEMBER_VIEW ? chunk : "live";
		values_now.now_ms = 1000;
		model_values(&values, values_now.view, 1000);
		return call_values;
	}
	if(kind == MEMBER_CURRENT || kind == MEMBER_PROFILE_SSID || kind == MEMBER_PROFILE_HOST || kind == MEMBER_SEEN_SSID)
	{
		profile(0, "First", "password-1", "first.host");
		profile(1, kind == MEMBER_PROFILE_SSID ? chunk : "Second", "", kind == MEMBER_PROFILE_HOST ? chunk : "");
		network(0, "Seen first", -50, true);
		network(1, kind == MEMBER_SEEN_SSID ? chunk : "Seen second", -60, false);
		wifi_now.profiles = profiles;
		wifi_now.profile_count = 2;
		wifi_now.current = kind == MEMBER_CURRENT ? chunk : "First";
		wifi_now.seen = seen;
		wifi_now.seen_count = 2;
		model_wifi(profiles, 2, wifi_now.current, seen, 2);
		return call_wifi;
	}

	no_report();
	report.warnings = 1;
	strcpy(report.path, kind == MEMBER_PATH ? chunk : "pages[0]");
	strcpy(report.problem, kind == MEMBER_PROBLEM ? chunk : "problem");
	strcpy(report.warning_path, kind == MEMBER_WARNING_PATH ? chunk : "pages[1]");
	strcpy(report.warning, kind == MEMBER_WARNING ? chunk : "warning");
	no_layout(kind == MEMBER_LAYOUT_NAME ? chunk : "L");
	layout.page_count = 1;
	layout.pages[0].item_count = 2;
	strcpy(layout.pages[0].items[0].key, "FIRST");
	strcpy(layout.pages[0].items[1].key, kind == MEMBER_KEY ? chunk : "SECOND");
	no_catalog();
	entry("K");
	report_now.ok = kind != MEMBER_PATH && kind != MEMBER_PROBLEM;
	report_now.report = &report;
	report_now.layout = &layout;
	report_now.catalog = &catalog;
	model_report(report_now.ok, &report, &layout, &catalog);
	return call_report;
}

static void test_every_text_member(void)
{
	static const text_member_t members[] = {
		{MEMBER_INFO, offsetof(web_info_t, version), "version", 60, "info version"},
		{MEMBER_INFO, offsetof(web_info_t, git), "git", 60, "info git"},
		{MEMBER_INFO, offsetof(web_info_t, slot), "slot", 60, "info slot"},
		{MEMBER_INFO, offsetof(web_info_t, reset), "reset", 60, "info reset"},
		{MEMBER_INFO, offsetof(web_info_t, heat), "heat", 60, "info heat"},
		{MEMBER_INFO, offsetof(web_info_t, ssid), "wifi.ssid", 60, "info wifi.ssid"},
		{MEMBER_INFO, offsetof(web_info_t, ip), "wifi.ip", 60, "info wifi.ip"},
		{MEMBER_INFO, offsetof(web_info_t, ap_ssid), "wifi.ap_ssid", 60, "info wifi.ap_ssid"},
		{MEMBER_INFO, offsetof(web_info_t, wican_host), "wican.host", 60, "info wican.host"},
		{MEMBER_INFO, offsetof(web_info_t, wican_id), "wican.id", 60, "info wican.id"},
		{MEMBER_INFO, offsetof(web_info_t, wican_fw), "wican.fw", 60, "info wican.fw"},
		{MEMBER_INFO, offsetof(web_info_t, view), "wican.view", 60, "info wican.view"},
		{MEMBER_INFO, offsetof(web_info_t, layout_name), "layout.name", 60, "info layout.name"},
		{MEMBER_INFO, offsetof(web_info_t, layout_source), "layout.source", 60, "info layout.source"},
		{MEMBER_VIEW, 0, "view", 60, "values view"},
		{MEMBER_VALUE_NAME, 0, "values.#", 32, "the name of a value"},
		{MEMBER_CURRENT, 0, "current", 60, "wifi current"},
		{MEMBER_PROFILE_SSID, 0, "profiles[1].ssid", 32, "the ssid of a profile"},
		{MEMBER_PROFILE_HOST, 0, "profiles[1].host", 39, "the host of a profile"},
		{MEMBER_SEEN_SSID, 0, "seen[1].ssid", 32, "the ssid of a network seen"},
		{MEMBER_PATH, 0, "path", 47, "report path"},
		{MEMBER_PROBLEM, 0, "problem", 63, "report problem"},
		{MEMBER_LAYOUT_NAME, 0, "name", 32, "report name"},
		{MEMBER_WARNING_PATH, 0, "warning_path", 47, "report warning_path"},
		{MEMBER_WARNING, 0, "warning", 63, "report warning"},
		{MEMBER_KEY, 0, "unknown[1]", 32, "an unknown key of the report"},
	};
	char what[160];
	char chunk[64];
	char back[64];
	size_t i, at;
	int start;

	for(i = 0; i < sizeof(members) / sizeof(members[0]); i++)
	{
		bool written = true, json = true, returned = true;
		int bytes = 0;

		// All byte values from 1 to 255, in pieces as long as the member can be
		for(start = 1; start <= 255; start += (int)members[i].fits)
		{
			size_t length = 255 - start + 1 < (int)members[i].fits ? (size_t)(255 - start + 1) : members[i].fits;
			call_t call;
			int index;

			for(at = 0; at < length; at++) chunk[at] = (char)(start + (int)at);
			chunk[length] = '\0';
			bytes += (int)length;

			call = member_set(&members[i], chunk);
			if(!gives(call, expected)) written = false;
			if(json_parse((char *)room, strlen((char *)room), tokens, 4096) <= 0 || tokens[0].type != JSON_OBJECT) json = false;
			index = json ? token_at((char *)room, members[i].path) : -1;
			if(index < 0 || !json_text((char *)room, &tokens[index], back, sizeof(back)) || strcmp(back, chunk) != 0) returned = false;
		}
		snprintf(what, sizeof(what), "every byte value in %s: written as the rule says, in an answer that is JSON, and read back from it as it was",
		         members[i].name);
		check(bytes == 255 && written && json && returned, what);
	}
}

/*
 * GET /api/ticket, the answer to a request that needs the knob, GET /api/dtc/last
 */

static bool ticket_gives(uint32_t ticket, access_ticket_t state, uint32_t left_s, const char *wanted)
{
	ticket_now.ticket = ticket;
	ticket_now.state = state;
	ticket_now.left_s = left_s;
	return every_room(call_ticket, wanted) && is_json_object(wanted);
}

static void test_ticket(void)
{
	static const uint32_t numbers[] = {0, 1, 9, 10, 99, 100, 12345, 65535, 65536, 999999999, 1000000000, 2147483647, 2147483648u, 4294967294u, 4294967295u};
	bool all = true;
	size_t i;
	int state, bit;

	ticket_now.ticket = 17;
	ticket_now.state = ACCESS_TICKET_WAITING;
	ticket_now.left_s = 42;
	check(gives_fixture(call_ticket, "fixtures/web_ticket.json"), "ticket: a ticket that waits gives the fixture, with every room from 0 bytes on");

	check(ticket_gives(17, ACCESS_TICKET_UNKNOWN, 0, "{\"ticket\":17,\"state\":\"unknown\",\"left_s\":0}"), "ticket: unknown");
	check(ticket_gives(17, ACCESS_TICKET_CONFIRMED, 0, "{\"ticket\":17,\"state\":\"confirmed\",\"left_s\":0}"), "ticket: confirmed");
	check(ticket_gives(17, ACCESS_TICKET_REFUSED, 0, "{\"ticket\":17,\"state\":\"refused\",\"left_s\":0}"), "ticket: refused");
	check(ticket_gives(17, ACCESS_TICKET_EXPIRED, 0, "{\"ticket\":17,\"state\":\"expired\",\"left_s\":0}"), "ticket: expired");
	check(ticket_gives(17, ACCESS_TICKET_WAITING, 0, "{\"ticket\":17,\"state\":\"waiting\",\"left_s\":0}"), "ticket: waiting with no second left");
	check(ticket_gives(17, ACCESS_TICKET_WAITING, 60, "{\"ticket\":17,\"state\":\"waiting\",\"left_s\":60}"), "ticket: waiting with 60 s left");
	check(ticket_gives(17, ACCESS_TICKET_WAITING, 4294967295u, "{\"ticket\":17,\"state\":\"waiting\",\"left_s\":4294967295}"),
	      "ticket: waiting with the largest time left");

	// left_s is 0 unless it waits
	check(ticket_gives(17, ACCESS_TICKET_UNKNOWN, 42, "{\"ticket\":17,\"state\":\"unknown\",\"left_s\":0}"), "ticket: an unknown ticket has no time left, whatever is passed");
	check(ticket_gives(17, ACCESS_TICKET_CONFIRMED, 42, "{\"ticket\":17,\"state\":\"confirmed\",\"left_s\":0}"), "ticket: a confirmed ticket has no time left, whatever is passed");
	check(ticket_gives(17, ACCESS_TICKET_REFUSED, 42, "{\"ticket\":17,\"state\":\"refused\",\"left_s\":0}"), "ticket: a refused ticket has no time left, whatever is passed");
	check(ticket_gives(17, ACCESS_TICKET_EXPIRED, 42, "{\"ticket\":17,\"state\":\"expired\",\"left_s\":0}"), "ticket: an expired ticket has no time left, whatever is passed");

	// A state that is none
	check(ticket_gives(17, (access_ticket_t)5, 42, "{\"ticket\":17,\"state\":\"unknown\",\"left_s\":0}"), "ticket: the state 5, one behind the last, counts as unknown");
	check(ticket_gives(17, (access_ticket_t)-1, 42, "{\"ticket\":17,\"state\":\"unknown\",\"left_s\":0}"), "ticket: the state -1 counts as unknown");
	check(ticket_gives(17, (access_ticket_t)0x7FFFFFFF, 42, "{\"ticket\":17,\"state\":\"unknown\",\"left_s\":0}"), "ticket: the largest state counts as unknown");
	check(ticket_gives(17, (access_ticket_t)257, 42, "{\"ticket\":17,\"state\":\"unknown\",\"left_s\":0}"), "ticket: the state 257, which is 1 in 8 bit, counts as unknown");

	// The time left of a ticket that does not wait, whatever it is
	all = true;
	ticket_now.ticket = 17;
	for(state = 0; state <= 70000; state++)
	{
		ticket_now.left_s = (uint32_t)state;
		ticket_now.state = ACCESS_TICKET_UNKNOWN;
		if(!gives(call_ticket, "{\"ticket\":17,\"state\":\"unknown\",\"left_s\":0}")) all = false;
		ticket_now.state = ACCESS_TICKET_CONFIRMED;
		if(!gives(call_ticket, "{\"ticket\":17,\"state\":\"confirmed\",\"left_s\":0}")) all = false;
		ticket_now.state = ACCESS_TICKET_REFUSED;
		if(!gives(call_ticket, "{\"ticket\":17,\"state\":\"refused\",\"left_s\":0}")) all = false;
		ticket_now.state = ACCESS_TICKET_EXPIRED;
		if(!gives(call_ticket, "{\"ticket\":17,\"state\":\"expired\",\"left_s\":0}")) all = false;
	}
	check(all, "ticket: a ticket that does not wait has no time left, whatever time from 0 to 70000 s is passed");

	// Every state far around the five that there are, and those that are one of them in fewer bits
	all = true;
	ticket_now.ticket = 17;
	ticket_now.left_s = 42;
	for(state = -70000; state <= 70000; state++)
	{
		if(state >= 0 && state <= 4) continue;
		ticket_now.state = (access_ticket_t)state;
		if(!gives(call_ticket, "{\"ticket\":17,\"state\":\"unknown\",\"left_s\":0}")) all = false;
	}
	for(bit = 8; bit < 32; bit++)
	{
		for(state = 0; state <= 4; state++)
		{
			ticket_now.state = (access_ticket_t)(((uint32_t)1 << bit) + (uint32_t)state);
			if(!gives(call_ticket, "{\"ticket\":17,\"state\":\"unknown\",\"left_s\":0}")) all = false;
		}
	}
	check(all, "ticket: every state from -70000 to 70000 that is none of the five counts as unknown, and so do those that are one of them in fewer bits");
	all = true;

	for(i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++)
	{
		char wanted[96];

		snprintf(wanted, sizeof(wanted), "{\"ticket\":%lu,\"state\":\"waiting\",\"left_s\":%lu}", (unsigned long)numbers[i],
		         (unsigned long)numbers[sizeof(numbers) / sizeof(numbers[0]) - 1 - i]);
		if(!ticket_gives(numbers[i], ACCESS_TICKET_WAITING, numbers[sizeof(numbers) / sizeof(numbers[0]) - 1 - i], wanted)) all = false;
	}
	check(all, "ticket: numbers and times from 0 to 2^32 - 1 are written as printf writes them");
	check(ticket_gives(4294967295u, ACCESS_TICKET_EXPIRED, 0, "{\"ticket\":4294967295,\"state\":\"expired\",\"left_s\":0}"), "ticket: the largest ticket number");
	check(ticket_gives(0, ACCESS_TICKET_UNKNOWN, 0, "{\"ticket\":0,\"state\":\"unknown\",\"left_s\":0}"), "ticket: the number 0, which is never given");
}

static void test_asked(void)
{
	// The bytes of the two umlauts written out
	static const char seventeen[] = "{\"ticket\":17,\"hint\":\"Am Display best\xc3\xa4tigen: Knopf dr\xc3\xbc" "cken\"}";

	ticket_now.ticket = 17;
	check(gives_fixture(call_asked, "fixtures/web_asked.json"), "asked: the ticket 17 gives the fixture, with every room from 0 bytes on");
	check(every_room(call_asked, seventeen) && strlen(seventeen) == 61, "asked: the hint is UTF-8, the answer has 61 bytes");
	ticket_now.ticket = 1;
	check(every_room(call_asked, "{\"ticket\":1,\"hint\":\"Am Display best\xc3\xa4tigen: Knopf dr\xc3\xbc" "cken\"}"), "asked: the ticket 1");
	ticket_now.ticket = 4294967295u;
	check(every_room(call_asked, "{\"ticket\":4294967295,\"hint\":\"Am Display best\xc3\xa4tigen: Knopf dr\xc3\xbc" "cken\"}"), "asked: the largest ticket number");
	ticket_now.ticket = 1000000000;
	check(every_room(call_asked, "{\"ticket\":1000000000,\"hint\":\"Am Display best\xc3\xa4tigen: Knopf dr\xc3\xbc" "cken\"}"), "asked: a ticket number with zeros");
	ticket_now.ticket = 0;
	check(every_room(call_asked, "{\"ticket\":0,\"hint\":\"Am Display best\xc3\xa4tigen: Knopf dr\xc3\xbc" "cken\"}"), "asked: the number 0 is written like every other");
}

static bool dtc_gives(const char *read, uint32_t read_age_s, const char *before_clear, const char *wanted)
{
	dtc_now.read = read;
	dtc_now.read_age_s = read_age_s;
	dtc_now.before_clear = before_clear;
	return every_room(call_dtc, wanted);
}

static void test_dtc_last(void)
{
	static const char read[] = "{\"state\":\"done\",\"action\":\"read\",\"duration_ms\":34300,\"dtc_count\":1,\"ecus\":[{\"name\":\"N3/28 Motorelektronik (CDID3)\","
	                           "\"id\":\"7E0\",\"protocol\":\"UDS\",\"status\":\"ok\",\"dtcs\":[{\"code\":\"P242F-FA\",\"status\":\"68\",\"active\":false}]}]}";
	static const char before[] = "{\"state\":\"done\",\"action\":\"read\",\"duration_ms\":35100,\"dtc_count\":2,\"ecus\":[{\"name\":\"N10 SAM\",\"id\":\"662\","
	                             "\"protocol\":\"KWP\",\"status\":\"ok\",\"dtcs\":[{\"code\":\"9301\",\"status\":\"60\"},{\"code\":\"9302\",\"status\":\"20\"}]}]}";
	static char result[8192];
	static char cleared[8192];
	static char large_read[10000];
	static char large_before[11100];
	size_t length;
	bool all;
	int byte;

	dtc_now.read = read;
	dtc_now.read_age_s = 95;
	dtc_now.before_clear = before;
	check(gives_fixture(call_dtc, "fixtures/web_dtc_last.json"), "dtc last: a read and the list before the clear give the fixture, with every room from 0 bytes on");
	dtc_now.read = NULL;
	dtc_now.read_age_s = 0;
	dtc_now.before_clear = NULL;
	check(gives_fixture(call_dtc, "fixtures/web_dtc_last_none.json"), "dtc last: nothing read yet gives the fixture: null, 0, null");

	check(dtc_gives("", 0, "", "{\"read\":null,\"read_age_s\":0,\"before_clear\":null}"), "dtc last: empty texts become null");
	check(dtc_gives("{\"a\":1}", 7, NULL, "{\"read\":{\"a\":1},\"read_age_s\":7,\"before_clear\":null}"), "dtc last: a read alone, with its age");
	check(dtc_gives(NULL, 0, "{\"b\":2}", "{\"read\":null,\"read_age_s\":0,\"before_clear\":{\"b\":2}}"), "dtc last: a list before a clear alone");
	check(dtc_gives("{\"a\":1}", 0, "{\"b\":2}", "{\"read\":{\"a\":1},\"read_age_s\":0,\"before_clear\":{\"b\":2}}"), "dtc last: a read fetched just now has the age 0");
	check(dtc_gives("{\"a\":1}", 4294967295u, "{\"b\":2}", "{\"read\":{\"a\":1},\"read_age_s\":4294967295,\"before_clear\":{\"b\":2}}"), "dtc last: the largest age");
	check(dtc_gives("{\"a\":1}", 1000000000, "", "{\"read\":{\"a\":1},\"read_age_s\":1000000000,\"before_clear\":null}"), "dtc last: an age with zeros, the other text empty");

	// The age belongs to the read
	check(dtc_gives(NULL, 95, NULL, "{\"read\":null,\"read_age_s\":0,\"before_clear\":null}"), "dtc last: without a read the age is written as 0, whatever is passed");
	check(dtc_gives("", 95, "{\"b\":2}", "{\"read\":null,\"read_age_s\":0,\"before_clear\":{\"b\":2}}"),
	      "dtc last: with an empty read the age is written as 0, also when there is a list before a clear");
	check(dtc_gives("{\"a\":1}", 95, "", "{\"read\":{\"a\":1},\"read_age_s\":95,\"before_clear\":null}"), "dtc last: the age of a read does not depend on the other text");

	all = true;
	for(length = 0; length <= 70000; length++)
	{
		dtc_now.read = length % 2 ? NULL : "";
		dtc_now.read_age_s = (uint32_t)length;
		dtc_now.before_clear = length % 3 ? NULL : "{}";
		if(!gives(call_dtc, length % 3 ? "{\"read\":null,\"read_age_s\":0,\"before_clear\":null}" : "{\"read\":null,\"read_age_s\":0,\"before_clear\":{}}")) all = false;
	}
	check(all, "dtc last: without a read every age from 0 to 70000 is written as 0, with and without a list before a clear");

	// The texts are embedded as they are
	check(dtc_gives(" { \"a\" : \"b\\\"c\\\\\" }\n", 1, "[1 , 2]", "{\"read\": { \"a\" : \"b\\\"c\\\\\" }\n,\"read_age_s\":1,\"before_clear\":[1 , 2]}"),
	      "dtc last: the texts are passed on byte for byte, nothing in them is escaped or left out");
	check(dtc_gives("x", 1, "y", "{\"read\":x,\"read_age_s\":1,\"before_clear\":y}"), "dtc last: texts of one byte are passed on, whatever they are");
	check(dtc_gives("null", 1, "0", "{\"read\":null,\"read_age_s\":1,\"before_clear\":0}"), "dtc last: a text that says null is a text: its age is written");

	// Whatever a text begins with
	all = true;
	for(byte = 1; byte <= 255; byte++)
	{
		char one[3] = {(char)byte, 'z', '\0'};
		web_info_t info;

		snprintf(text, sizeof(text), "{\"read\":%s,\"read_age_s\":3,\"before_clear\":null}", one);
		if(!dtc_gives(one, 3, NULL, text)) all = false;
		snprintf(text, sizeof(text), "{\"read\":null,\"read_age_s\":0,\"before_clear\":%s}", one);
		if(!dtc_gives(NULL, 3, one, text)) all = false;

		memset(&info, 0, sizeof(info));
		info.settings = one;
		info_now = &info;
		snprintf(text, sizeof(text), "%.*s%s}", (int)strlen(empty_info_text) - 5, empty_info_text, one);
		if(!gives(call_info, text)) all = false;
	}
	check(all, "embedded texts are passed on whatever their first byte is, as read, as before_clear and as the settings of the info");

	// Texts far larger than a result of the adapter: 9901 and 11008 bytes
	length = (size_t)snprintf(large_read, sizeof(large_read), "[");
	for(byte = 0; byte < 900; byte++) length += (size_t)snprintf(large_read + length, sizeof(large_read) - length, "%s1234567890", byte ? "," : "");
	snprintf(large_read + length, sizeof(large_read) - length, "]");
	memset(large_before, 'x', sizeof(large_before));
	memcpy(large_before, "{\"a\":\"", 6);
	strcpy(large_before + 11006, "\"}");
	length = (size_t)snprintf(text, sizeof(text), "{\"read\":%s,\"read_age_s\":4294967295,\"before_clear\":%s}", large_read, large_before);
	dtc_now.read = large_read;
	dtc_now.read_age_s = 4294967295u;
	dtc_now.before_clear = large_before;
	check(strlen(large_read) == 9901 && strlen(large_before) == 11008 && length == 9901 + 11008 + 49 && is_json_object(text) && rooms_around(call_dtc, text),
	      "dtc last: texts of 9901 and 11008 bytes are embedded completely, 20958 bytes, and refused by a room of 20958 bytes");

	// Results of the adapter as they are delivered
	check(read_fixture("../../tools/w906/fixtures/dtc_result_read_codes.json", result, sizeof(result)) &&
	      read_fixture("../../tools/w906/fixtures/dtc_result_clear.json", cleared, sizeof(cleared)), "the results of the adapter are there");
	length = (size_t)snprintf(text, sizeof(text), "{\"read\":%s,\"read_age_s\":600,\"before_clear\":%s}", result, cleared);
	dtc_now.read = result;
	dtc_now.read_age_s = 600;
	dtc_now.before_clear = cleared;
	check(length == strlen(result) + strlen(cleared) + 42 && rooms_around(call_dtc, text), "dtc last: two results of the adapter are embedded completely");
	check(is_json_object(text) && json_member(text, tokens, json_member(text, tokens, 0, "read"), "ecus") > 0 &&
	      json_member(text, tokens, json_member(text, tokens, 0, "before_clear"), "ecus") > 0 && tokens[json_member(text, tokens, 0, "read_age_s")].type == JSON_NUMBER,
	      "dtc last: with results of the adapter the answer is JSON for the reader of the project, the results are members of it");
}

// Every answer of the examples is JSON for the reader of the project and has no whitespace
static void test_valid_json(void)
{
	static const char *const paths[] = {
		"fixtures/web_info.json", "fixtures/web_info_safe_mode.json", "fixtures/web_values.json", "fixtures/web_values_mixed.json",
		"fixtures/web_values_empty.json", "fixtures/web_wifi.json", "fixtures/web_wifi_empty.json", "fixtures/web_report_refused.json",
		"fixtures/web_report_taken.json", "fixtures/web_report_unknown.json", "fixtures/web_ticket.json", "fixtures/web_asked.json",
		"fixtures/web_dtc_last.json", "fixtures/web_dtc_last_none.json", "fixtures/web_wifi_request.json", "fixtures/web_forget_request.json",
	};
	bool json = true, whitespace = false;
	size_t i, at;

	for(i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
	{
		bool in_text = false;

		if(!read_fixture(paths[i], fixture, sizeof(fixture)) || !is_json_object(fixture)) json = false;
		for(at = 0; fixture[at] != '\0'; at++)
		{
			if(fixture[at] == '\\') at++;
			else if(fixture[at] == '"') in_text = !in_text;
			else if(!in_text && (fixture[at] == ' ' || fixture[at] == '\n' || fixture[at] == '\t' || fixture[at] == '\r')) whitespace = true;
		}
	}
	check(json, "every one of the 16 fixtures is a JSON object for the reader of the project");
	check(!whitespace, "no fixture has whitespace outside of its texts");
}

/*
 * Every writer with many random inputs against the formats written a second time
 */

static const char *random_text(void)
{
	static const char *const texts[] = {
		"", "0.1.0", "ota_0", "a\"b", "back\\slash", "\x01\x02", "tab\there", "\x7f", "caf\xc3\xa9", "\xff\xfe", "WiCAN_a1b2c3", "x",
		"A text of sixty-four bytes, as long as a problem may be, .......", "new\nline", "\"\"\"", "\\\\", " ", "{\"a\":1}", "null", "\x1f\x20\x7e\x80",
	};
	uint32_t pick = rnd(22);

	return pick >= 20 ? NULL : texts[pick];
}

static uint32_t random_number(void)
{
	static const uint32_t numbers[] = {0, 1, 9, 10, 99, 100, 4294967295u, 2147483648u, 1000000000, 999999999, 12345, 65536};
	uint32_t pick = rnd(16);

	return pick < 12 ? numbers[pick] : (rnd(1 << 16) << 16) | rnd(1 << 16);
}

static int random_int(void)
{
	static const int numbers[] = {0, 1, -1, INT_MIN, INT_MAX, -40, 85, -100, -9, -10, 10, 9};
	uint32_t pick = rnd(16);

	return pick < 12 ? numbers[pick] : (int)((rnd(1 << 16) << 16) | rnd(1 << 16));
}

static void test_random_info(void)
{
	static const char *const settings[] = {NULL, "", "{\"brightness\":80}", "{}", "null", "{ \"a\" : [1, 2] }"};
	web_info_t info;
	int different = 0, invalid = 0;
	int round;

	for(round = 0; round < 3000; round++)
	{
		info.version = random_text();
		info.git = random_text();
		info.slot = random_text();
		info.reset = random_text();
		info.up_s = random_number();
		info.safe_mode = rnd(2) == 1;
		info.rolled_back = rnd(2) == 1;
		info.update_pending = rnd(2) == 1;
		info.heap = random_number();
		info.heap_min = random_number();
		info.psram = random_number();
		info.psram_min = random_number();
		info.temp_c = random_int();
		info.heat = random_text();
		info.release_open = rnd(2) == 1;
		info.release_left_s = random_number();
		info.ssid = random_text();
		info.ip = random_text();
		info.rssi = random_int();
		info.ap_on = rnd(2) == 1;
		info.ap_ssid = random_text();
		info.wican_host = random_text();
		info.wican_id = random_text();
		info.wican_fw = random_text();
		info.view = random_text();
		info.layout_name = random_text();
		info.layout_source = random_text();
		info.http_ok = random_number();
		info.http_failed = random_number();
		info.reconnects = random_number();
		info.settings = settings[rnd(6)];

		model_info(&info);
		info_now = &info;
		if(!gives(call_info, expected) || !room_is_right(call_info, expected, rnd(1200))) different++;
		// Every one of the settings texts above is JSON
		if(!is_json_object(expected)) invalid++;
	}
	check(different == 0, "info: 3000 random infos are written as the format of the header says, each also with a random room");
	check(invalid == 0, "info: every one of the 3000 random answers is JSON for the reader of the project");
}

static void test_random_values(void)
{
	int different = 0, invalid = 0, left_out = 0, written = 0;
	int round, i;

	for(round = 0; round < 3000; round++)
	{
		int count = (int)rnd(12);
		uint64_t now_ms = 20000 + rnd(1000);
		const char *view = random_text();

		no_values();
		for(i = 0; i < count; i++)
		{
			static const double numbers[] = {0, -0.0, 1, -1, 812.5, 0.001, 1e9, 999999999, 1234567890, 1e-5, 1e300, -1e-300, 12.4, 187432};
			static const uint64_t ages[] = {0, 1, 2999, 3000, 3001, 9999, 10000, 10001, 20000};
			uint32_t kind = rnd(10);
			const char *name = random_text();
			double number = rnd(8) == 0 ? (rnd(2) ? HUGE_VAL : NAN) : numbers[rnd(14)];
			uint64_t age = ages[rnd(9)];

			// A name is at most 32 bytes
			if(name == NULL || strlen(name) > 32) name = "PLAIN_NAME";
			// Sometimes the clock stepped back behind the time the value was seen
			add_value(name, kind < 6 ? VALUE_NUMBER : kind < 8 ? VALUE_ON : kind == 8 ? VALUE_OFF : (value_kind_t)(3 + rnd(3)), number,
			          rnd(10) == 0 ? now_ms + age : now_ms - age);
		}
		model_values(&values, view, now_ms);
		values_now.values = &values;
		values_now.view = view;
		values_now.now_ms = now_ms;
		if(!gives(call_values, expected) || !room_is_right(call_values, expected, rnd(600))) different++;
		if(!is_json_object(expected)) invalid++;
		written += count_of(expected, "\"age\":");
		left_out += count - count_of(expected, "\"age\":");
	}
	printf("  values written %d, left out %d\n", written, left_out);
	check(written > 3000 && left_out > 3000, "values: 3000 random lists hold values that are written and values that are left out");
	check(different == 0, "values: 3000 random lists are written as the format of the header says, each also with a random room");
	check(invalid == 0, "values: every one of the 3000 random answers is JSON for the reader of the project");
}

static void test_random_report(void)
{
	static const char *const keys[] = {"A", "B", "C", "AB", "ENGINE_RPM", "@BATT_V", "a\"b", "K\\", "\x01", "N2345678901234567890123456789012", "", "x"};
	int different = 0, invalid = 0, unknown = 0, not_loaded = 0;
	int round, p, i;

	for(round = 0; round < 3000; round++)
	{
		bool ok = rnd(5) != 0;
		const char *name = random_text();
		const char *path = random_text();
		const char *problem = random_text();
		int entries = (int)rnd(8);

		no_report();
		snprintf(report.path, sizeof(report.path), "%s", path == NULL ? "pages" : path);
		snprintf(report.problem, sizeof(report.problem), "%s", problem == NULL ? "missing" : problem);
		path = random_text();
		problem = random_text();
		snprintf(report.warning_path, sizeof(report.warning_path), "%s", path == NULL ? "pages[0]" : path);
		snprintf(report.warning, sizeof(report.warning), "%s", problem == NULL ? "unknown widget" : problem);
		report.warnings = random_int();

		memset(&layout, 0, sizeof(layout));
		snprintf(layout.name, sizeof(layout.name), "%.32s", name == NULL ? "Name" : name);
		layout.page_count = (uint8_t)rnd(LAYOUT_PAGES_MAX + 1);
		for(p = 0; p < layout.page_count; p++)
		{
			layout.pages[p].hidden = rnd(3) == 0;
			layout.pages[p].item_count = (uint8_t)(1 + rnd(LAYOUT_ITEMS_MAX));
			for(i = 0; i < layout.pages[p].item_count; i++) strcpy(layout.pages[p].items[i].key, keys[rnd(12)]);
		}
		no_catalog();
		if(rnd(4) != 0) entry("@BATT_V");
		for(i = 0; i < entries; i++) entry(keys[rnd(12)]);

		model_report(ok, &report, &layout, &catalog);
		report_now.ok = ok;
		report_now.report = &report;
		report_now.layout = &layout;
		report_now.catalog = &catalog;
		if(!gives(call_report, expected) || !room_is_right(call_report, expected, rnd(700))) different++;
		if(!is_json_object(expected)) invalid++;
		if(ok && strstr(expected, "\"unknown\":[]") == NULL) unknown++;
		if(ok && strstr(expected, "\"unknown\":[]") != NULL && layout.page_count > 0) not_loaded++;
	}
	printf("  reports with unknown keys %d, with none %d\n", unknown, not_loaded);
	check(unknown > 1000 && not_loaded > 100, "report: 3000 random layouts and catalogues give reports with and without unknown keys");
	check(different == 0, "report: 3000 random reports are written as the format of the header says, each also with a random room");
	check(invalid == 0, "report: every one of the 3000 random answers is JSON for the reader of the project");
}

// A room far larger than any answer is a room like every other
static void test_large_room(void)
{
	static unsigned char large_room[70000];
	static char large_text[68001];
	static const struct
	{
		call_t call;
		const char *wanted;
		const char *what;
	} cases[] = {
		{call_info, empty_info_text, "info: a room of 65536 bytes holds the answer"},
		{call_values, "{\"view\":\"live\",\"values\":{\"A\":{\"v\":1,\"age\":\"fresh\"}}}", "values: a room of 65536 bytes holds the answer"},
		{call_wifi, "{\"current\":\"Home\",\"profiles\":[],\"seen\":[]}", "wifi: a room of 65536 bytes holds the answer"},
		{call_report, "{\"ok\":false,\"path\":\"\",\"problem\":\"empty\"}", "report: a room of 65536 bytes holds the answer"},
		{call_ticket, "{\"ticket\":17,\"state\":\"waiting\",\"left_s\":42}", "ticket: a room of 65536 bytes holds the answer"},
		{call_asked, "{\"ticket\":17,\"hint\":\"Am Display best\xc3\xa4tigen: Knopf dr\xc3\xbc" "cken\"}", "asked: a room of 65536 bytes holds the answer"},
		{call_dtc, "{\"read\":{},\"read_age_s\":1,\"before_clear\":null}", "dtc last: a room of 65536 bytes holds the answer"},
	};
	web_info_t info;
	size_t i, at;

	memset(&info, 0, sizeof(info));
	info_now = &info;
	no_values();
	add_value("A", VALUE_NUMBER, 1, 1000);
	values_now.values = &values;
	values_now.view = "live";
	values_now.now_ms = 1000;
	wifi_now.profiles = NULL;
	wifi_now.profile_count = 0;
	wifi_now.current = "Home";
	wifi_now.seen = NULL;
	wifi_now.seen_count = 0;
	no_report();
	strcpy(report.problem, "empty");
	report_now.ok = false;
	report_now.report = &report;
	ticket_now.ticket = 17;
	ticket_now.state = ACCESS_TICKET_WAITING;
	ticket_now.left_s = 42;
	dtc_now.read = "{}";
	dtc_now.read_age_s = 1;
	dtc_now.before_clear = NULL;

	for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		size_t length = strlen(cases[i].wanted);
		bool right;

		memset(large_room, GUARD, sizeof(large_room));
		right = cases[i].call((char *)large_room, 65536) == (int)length && memcmp(large_room, cases[i].wanted, length + 1) == 0;
		for(at = 65536; at < sizeof(large_room); at++)
		{
			if(large_room[at] != GUARD) right = false;
		}
		check(right, cases[i].what);
	}

	// An answer longer than 16 bit can count: a text of 68000 bytes in rooms of 69000, 68044 and 68045 bytes
	memset(large_text, '7', 68000);
	large_text[68000] = '\0';
	dtc_now.read = large_text;
	dtc_now.read_age_s = 1;
	dtc_now.before_clear = NULL;
	memset(large_room, GUARD, sizeof(large_room));
	check(web_dtc_last_json(large_text, 1, NULL, (char *)large_room, 69000) == 68044 && memcmp(large_room, "{\"read\":7777", 12) == 0 &&
	      memcmp(large_room + 8 + 67990, "7777777777,\"read_age_s\":1,\"before_clear\":null}", 47) == 0 && large_room[69000] == GUARD,
	      "dtc last: an answer of 68044 bytes is written completely and its length returned");
	memset(large_room, GUARD, sizeof(large_room));
	check(web_dtc_last_json(large_text, 1, NULL, (char *)large_room, 68044) == -1 && large_room[0] == '\0' && large_room[68044] == GUARD,
	      "dtc last: an answer of 68044 bytes does not fit into a room of 68044 bytes");
	memset(large_room, GUARD, sizeof(large_room));
	check(web_dtc_last_json(large_text, 1, NULL, (char *)large_room, 68045) == 68044 && large_room[68044] == '\0' && large_room[68045] == GUARD,
	      "dtc last: an answer of 68044 bytes fits into a room of 68045 bytes");
}

// Every function once with ordinary input, before everything else and in a child: a module that reads or writes
// outside of its memory there, or never returns, ends the child. That is a failed check, and the test ends with it -
// what such a module does with the other inputs says nothing any more.
static void test_ordinary(void)
{
	web_info_t info = typical_info;
	int p;

	info.git = "a\"b\\c\x01\x1f\x7f";
	info.up_s = 4294967295u;
	info.temp_c = -2147483647 - 1;
	info.settings = "";
	model_info(&info);
	info_now = &info;
	check(gives(call_info, expected), "ordinary input: info");

	no_values();
	add_value("ENGINE_RPM", VALUE_NUMBER, 812.5, 20000);
	add_value("DPF_REGEN_STATUS", VALUE_ON, 0, 15000);
	add_value("GLOW_ACTIVE", VALUE_OFF, 0, 5000);
	add_value("COOLANT\x1f", VALUE_NUMBER, -40, 20000);
	values_now.values = &values;
	values_now.view = "live";
	values_now.now_ms = 20000;
	check(gives(call_values, "{\"view\":\"live\",\"values\":{\"ENGINE_RPM\":{\"v\":812.5,\"age\":\"fresh\"},\"DPF_REGEN_STATUS\":{\"v\":\"on\",\"age\":\"old\"},"
	            "\"COOLANT\\u001f\":{\"v\":-40,\"age\":\"fresh\"}}}"), "ordinary input: values");

	memset(profile_room, '#', sizeof(profile_room));
	memset(seen, '#', sizeof(seen));
	profile(0, "Werkstatt", "geheim123", "192.168.1.50");
	profile(1, "WiCAN_a1b2c3d4e5f6", "@meatpi#", "");
	profile(2, "Camping", "", "");
	profile(3, "Tab\tstelle", "12345678", "h");
	network(0, "Werkstatt", -52, true);
	network(1, "", -70, true);
	network(2, "Freifunk", -88, false);
	model_wifi(profiles, 4, "Werkstatt", seen, 3);
	wifi_now.profiles = profiles;
	wifi_now.profile_count = 4;
	wifi_now.current = "Werkstatt";
	wifi_now.seen = seen;
	wifi_now.seen_count = 3;
	check(gives(call_wifi, expected), "ordinary input: wifi");

	check(wifi_taken("{\"later\":{\"a\":[1,2]},\"ssid\":\"Werkstatt\",\"password\":\"geheim123\",\"host\":\"192.168.1.50\"}", "Werkstatt", "geheim123", true, "192.168.1.50"),
	      "ordinary input: a wifi request");
	check(wifi_refused("{\"ssid\":\"Werkstatt\",\"password\":\"geheim\"}"), "ordinary input: a wifi request that is refused");
	check(forget_taken("{\"later\":{\"a\":[1,2]},\"ssid\":\"Camping\"}", "Camping"), "ordinary input: a forget request");

	no_report();
	report.warnings = 1;
	strcpy(report.warning_path, "pages[1].items[0].widget");
	strcpy(report.warning, "unknown widget, shown as number");
	no_layout("W906 OM651 Standard");
	for(p = 0; p < LAYOUT_PAGES_MAX; p++) page("ENGINE_RPM OIL_PRESSURE COOLANT_TMP GEAR OIL_PRESSURE @BATT_V", p % 2 == 1);
	no_catalog();
	entry("@BATT_V");
	entry("ENGINE_RPM");
	entry("COOLANT_TMP");
	model_report(true, &report, &layout, &catalog);
	report_now.ok = true;
	report_now.report = &report;
	report_now.layout = &layout;
	report_now.catalog = &catalog;
	check(gives(call_report, expected) && strstr(expected, "\"pages\":12,\"items\":72,") != NULL && strstr(expected, "\"unknown\":[\"OIL_PRESSURE\",\"GEAR\"]}") != NULL,
	      "ordinary input: the report of a layout that was taken");
	strcpy(report.path, "pages[2].items[0].min");
	strcpy(report.problem, "min is not below max");
	report_now.ok = false;
	check(gives(call_report, "{\"ok\":false,\"path\":\"pages[2].items[0].min\",\"problem\":\"min is not below max\"}"), "ordinary input: the report of a layout that was refused");

	ticket_now.ticket = 4294967295u;
	ticket_now.state = ACCESS_TICKET_WAITING;
	ticket_now.left_s = 42;
	check(gives(call_ticket, "{\"ticket\":4294967295,\"state\":\"waiting\",\"left_s\":42}"), "ordinary input: ticket");
	check(gives(call_asked, "{\"ticket\":4294967295,\"hint\":\"Am Display best\xc3\xa4tigen: Knopf dr\xc3\xbc" "cken\"}"), "ordinary input: asked");
	dtc_now.read = "";
	dtc_now.read_age_s = 5;
	dtc_now.before_clear = "{}";
	check(gives(call_dtc, "{\"read\":null,\"read_age_s\":0,\"before_clear\":{}}"), "ordinary input: dtc last without a read");
	dtc_now.read = "{}";
	dtc_now.before_clear = "";
	check(gives(call_dtc, "{\"read\":{},\"read_age_s\":5,\"before_clear\":null}"), "ordinary input: dtc last with a read");
}

// Texts that are NULL, calls that give no buffer where no room is, and no lists where they are not read
static void test_null(void)
{
	web_info_t info;

	memset(&info, 0, sizeof(info));
	info_now = &info;
	check(gives(call_info, empty_info_text), "info: every text and the settings NULL: empty texts and null");
	wifi_now.profiles = NULL;
	wifi_now.profile_count = 0;
	wifi_now.current = NULL;
	wifi_now.seen = NULL;
	wifi_now.seen_count = 0;
	check(gives(call_wifi, "{\"current\":\"\",\"profiles\":[],\"seen\":[]}"), "wifi: a current network that is NULL is an empty text");
	no_values();
	values_now.values = &values;
	values_now.view = NULL;
	values_now.now_ms = 0;
	check(gives(call_values, "{\"view\":\"\",\"values\":{}}"), "values: a view that is NULL is an empty text");
	dtc_now.read = NULL;
	dtc_now.read_age_s = 5;
	dtc_now.before_clear = NULL;
	check(gives(call_dtc, "{\"read\":null,\"read_age_s\":0,\"before_clear\":null}"), "dtc last: texts that are NULL become null");

	check(web_info_json(&info, NULL, 0) == -1, "info: no room and no buffer: -1");
	no_values();
	add_value("A", VALUE_NUMBER, 1, 1000);
	check(web_values_json(&values, "live", 1000, NULL, 0) == -1, "values: no room and no buffer: -1");
	profile(0, "Home", "", "");
	network(0, "Home", -50, true);
	check(web_wifi_json(profiles, 1, "Home", seen, 1, NULL, 0) == -1, "wifi: no room and no buffer: -1");
	no_report();
	no_layout("L");
	page("A", false);
	no_catalog();
	entry("B");
	check(web_layout_report_json(true, &report, &layout, &catalog, NULL, 0) == -1, "report of a taken layout: no room and no buffer: -1");
	check(web_layout_report_json(false, &report, NULL, NULL, NULL, 0) == -1, "report of a refused layout: no room and no buffer: -1");
	check(web_ticket_json(17, ACCESS_TICKET_WAITING, 42, NULL, 0) == -1, "ticket: no room and no buffer: -1");
	check(web_asked_json(17, NULL, 0) == -1, "asked: no room and no buffer: -1");
	check(web_dtc_last_json("{}", 1, "{}", NULL, 0) == -1, "dtc last: no room and no buffer: -1");
	check(web_dtc_last_json(NULL, 1, NULL, NULL, 0) == -1, "dtc last: no texts, no room and no buffer: -1");

	// The readers without a room for tokens and without a text
	check(!wifi_parse("{\"ssid\":\"Home\"}", 15, NULL, 0) && all_guard(&request_box, sizeof(request_box)) &&
	      !forget_parse("{\"ssid\":\"Home\"}", 15, NULL, 0) && all_guard(&ssid_box, sizeof(ssid_box)), "requests: no room for tokens and no array for them: refused");
	check(!wifi_parse(NULL, 0, work, WEB_WIFI_TOKENS) && all_guard(&request_box, sizeof(request_box)) &&
	      !forget_parse(NULL, 0, work, WEB_WIFI_TOKENS) && all_guard(&ssid_box, sizeof(ssid_box)), "requests: a text of length 0 that is NULL: refused");

	// Lists that are not read
	wifi_now.profiles = NULL;
	wifi_now.seen = NULL;
	wifi_now.current = NULL;
	wifi_now.profile_count = 5;
	wifi_now.seen_count = -1;
	check(gives(call_wifi, "{\"current\":\"\",\"profiles\":[],\"seen\":[]}"), "wifi: with counts that count as 0 the lists are not read");
	wifi_now.profile_count = -1;
	check(gives(call_wifi, "{\"current\":\"\",\"profiles\":[],\"seen\":[]}"), "wifi: with a negative profile count the list is not read");
	no_report();
	strcpy(report.problem, "empty");
	report_now.ok = false;
	report_now.report = &report;
	report_now.layout = NULL;
	report_now.catalog = NULL;
	check(gives(call_report, "{\"ok\":false,\"path\":\"\",\"problem\":\"empty\"}"), "report: of a refused layout neither layout nor catalogue are read");
}

int main(void)
{
	memset(&work[WEB_WIFI_TOKENS], 0x5A, 2 * sizeof(work[0]));

	test_constants();
	if(!in_child(test_ordinary, "every function with ordinary input: no crash, no hang")) return test_end();
	// What a NULL would break is named here, where it breaks only a child
	in_child(test_null, "texts, buffers and lists that are NULL where the header allows it: no crash");
	test_stale_tokens();
	test_escaping();
	test_info();
	test_values();
	test_values_numbers();
	test_values_size();
	test_wifi();
	in_child(test_wifi_large_counts, "wifi: profile counts that are a list only in their low bits: nothing behind the list is read");
	test_wifi_damaged();
	in_child(test_wifi_passwords, "wifi: 3000 random lists: no crash, no hang");
	in_child(test_wifi_passwords_alike, "wifi: 3000 random lists with passwords like the other texts: no crash, no hang");
	test_wifi_request();
	test_forget_request();
	in_child(test_request_model, "requests: 30 walks of 1000 random texts each: no crash, no hang");
	test_report();
	test_report_size();
	test_every_text_member();
	test_ticket();
	test_asked();
	test_dtc_last();
	test_valid_json();
	test_large_room();
	in_child(test_random_info, "info: 3000 random infos: no crash, no hang");
	in_child(test_random_values, "values: 3000 random lists: no crash, no hang");
	in_child(test_random_report, "report: 3000 random reports: no crash, no hang");

	check(work[WEB_WIFI_TOKENS].start == 0x5A5A5A5A && work[WEB_WIFI_TOKENS + 1].start == 0x5A5A5A5A,
	      "no call wrote behind the WEB_WIFI_TOKENS tokens it was given");
	return test_end();
}
