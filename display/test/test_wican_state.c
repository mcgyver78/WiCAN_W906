/*
 * Host test for display/components/core/wican_state.c. Run "make test_wican_state && ./test_wican_state" in
 * display/test. redproof.py removes or weakens every rule once (mutations/wican_state.py) and expects this
 * test to fail.
 */
#include <stdlib.h>
#include <stdint.h>
#include "test.h"
#include "wican_state.h"

#define FIXTURES    "../../tools/w906/fixtures/"
#define FILL        0xA5
#define COUNT(a)    ((int)(sizeof(a) / sizeof((a)[0])))

// The state between bytes the module must not touch
static struct
{
	unsigned char before[16];
	wican_state_t state;
	unsigned char behind[16];
} box;

// Room for the JSON reader: twice what the header names, and two tokens the module is never told of
static json_token_t work[2 * WICAN_STATE_TOKENS + 2];

// Calls that wrote where they must not
static int stray_writes = 0;

static char what[256];

/* The answers the tests start from --------------------------------------------------------------------- */

// The example of API.md without its "dtc" object, and that object
#define OUTER \
	"{\"api\":1,\"id\":\"a1b2c3d4e5f6\",\"fw\":\"4.21\",\"git\":\"w906-v1.4.0-9-g0123abc\",\"boot\":1234567890,\"up\":812," \
	"\"autopid\":\"run\",\"pids\":35,\"ecu\":\"online\",\"pass\":1234,\"rx_age_ms\":140,\"mqtt\":\"connected\"," \
	"\"batt_v\":12.4,\"sleep_in_s\":-1,\"heap\":61000,\"heap_min\":48000,\"dtc\":"
#define IDLE \
	"{\"supported\":true,\"state\":\"idle\",\"action\":\"\",\"src\":\"\",\"seq\":0,\"ecu\":0,\"total\":0,\"name\":\"\"," \
	"\"reason\":\"\",\"age_s\":0,\"count\":0,\"result_seq\":0}"

static const wican_state_t EXAMPLE = {
	.id = "a1b2c3d4e5f6", .fw = "4.21", .git = "w906-v1.4.0-9-g0123abc", .boot = 1234567890, .up_s = 812,
	.autopid = WICAN_AUTOPID_RUN, .pids = 35, .ecu_online = true, .pass = 1234, .rx_age_ms = 140,
	.mqtt = WICAN_MQTT_CONNECTED, .batt_mv = 12400, .sleep_in_s = -1, .heap = 61000, .heap_min = 48000,
	.dtc = {.supported = true, .phase = WICAN_DTC_IDLE},
};

// An answer in which no two numbers and no two texts are the same: a value in the wrong field shows
#define DISTINCT \
	"{\"api\":1,\"id\":\"i\",\"fw\":\"f\",\"git\":\"g\",\"boot\":11,\"up\":12,\"autopid\":\"starting\",\"pids\":13," \
	"\"ecu\":\"online\",\"pass\":14,\"rx_age_ms\":15,\"mqtt\":\"disconnected\",\"batt_v\":1.9,\"sleep_in_s\":16," \
	"\"heap\":17,\"heap_min\":18,\"dtc\":{\"supported\":true,\"state\":\"running\",\"action\":\"read\",\"src\":\"http\"," \
	"\"seq\":21,\"ecu\":22,\"total\":23,\"name\":\"n\",\"reason\":\"r\",\"age_s\":24,\"count\":25,\"result_seq\":26}}"

static const wican_state_t DISTINCT_STATE = {
	.id = "i", .fw = "f", .git = "g", .boot = 11, .up_s = 12, .autopid = WICAN_AUTOPID_STARTING, .pids = 13,
	.ecu_online = true, .pass = 14, .rx_age_ms = 15, .mqtt = WICAN_MQTT_DISCONNECTED, .batt_mv = 1900,
	.sleep_in_s = 16, .heap = 17, .heap_min = 18,
	.dtc = {.supported = true, .phase = WICAN_DTC_RUNNING, .has_request = true, .clear = false, .from_http = true,
	        .seq = 21, .step = 22, .total = 23, .name = "n", .reason = "r", .age_s = 24, .count = 25, .result_seq = 26},
};

// The members of DISTINCT as they stand there. Kind: t text for display, c one of a few texts, n number,
// b true or false, o object.
typedef struct
{
	const char *member;
	char kind;
} member_t;

static const member_t MEMBERS[] = {
	{"\"api\":1", 'n'}, {"\"id\":\"i\"", 't'}, {"\"fw\":\"f\"", 't'}, {"\"git\":\"g\"", 't'}, {"\"boot\":11", 'n'},
	{"\"up\":12", 'n'}, {"\"autopid\":\"starting\"", 'c'}, {"\"pids\":13", 'n'}, {"\"ecu\":\"online\"", 'c'},
	{"\"pass\":14", 'n'}, {"\"rx_age_ms\":15", 'n'}, {"\"mqtt\":\"disconnected\"", 'c'}, {"\"batt_v\":1.9", 'n'},
	{"\"sleep_in_s\":16", 'n'}, {"\"heap\":17", 'n'}, {"\"heap_min\":18", 'n'}, {"\"dtc\":{", 'o'},
	{"\"supported\":true", 'b'}, {"\"state\":\"running\"", 'c'}, {"\"action\":\"read\"", 'c'}, {"\"src\":\"http\"", 'c'},
	{"\"seq\":21", 'n'}, {"\"ecu\":22", 'n'}, {"\"total\":23", 'n'}, {"\"name\":\"n\"", 't'}, {"\"reason\":\"r\"", 't'},
	{"\"age_s\":24", 'n'}, {"\"count\":25", 'n'}, {"\"result_seq\":26", 'n'},
};

// The numbers without sign and where they have to arrive
typedef struct
{
	const char *member;
	uint32_t *field;
} unsigned_t;

static const unsigned_t UNSIGNED[] = {
	{"\"boot\":11", &box.state.boot}, {"\"up\":12", &box.state.up_s}, {"\"pids\":13", &box.state.pids},
	{"\"pass\":14", &box.state.pass}, {"\"heap\":17", &box.state.heap}, {"\"heap_min\":18", &box.state.heap_min},
	{"\"seq\":21", &box.state.dtc.seq}, {"\"ecu\":22", &box.state.dtc.step}, {"\"total\":23", &box.state.dtc.total},
	{"\"age_s\":24", &box.state.dtc.age_s}, {"\"count\":25", &box.state.dtc.count},
	{"\"result_seq\":26", &box.state.dtc.result_seq},
};

typedef struct
{
	const char *member;
	int32_t *field;
} signed_t;

static const signed_t SIGNED[] = {
	{"\"rx_age_ms\":15", &box.state.rx_age_ms}, {"\"sleep_in_s\":16", &box.state.sleep_in_s},
};

// The texts for display, where they have to arrive and how large the field is according to the header
typedef struct
{
	const char *member;
	char *field;
	size_t size;
} text_t;

static const text_t TEXTS[] = {
	{"\"id\":\"i\"", box.state.id, 33}, {"\"fw\":\"f\"", box.state.fw, 40}, {"\"git\":\"g\"", box.state.git, 48},
	{"\"name\":\"n\"", box.state.dtc.name, 64}, {"\"reason\":\"r\"", box.state.dtc.reason, 32},
};

/* Helpers ---------------------------------------------------------------------------------------------- */

static bool filled(const void *memory, size_t size)
{
	const unsigned char *bytes = memory;

	for(size_t i = 0; i < size; i++)
	{
		if(bytes[i] != FILL) return false;
	}
	return true;
}

// One call with `count` tokens of room. Everything around the state and behind the room is watched.
static bool parse_room(const char *json, size_t length, int count)
{
	int told = count < 0 ? 0 : count;
	bool ok;

	memset(&box, FILL, sizeof(box));
	memset(work, FILL, sizeof(work));
	ok = wican_state_parse(json, length, &box.state, work, count);
	if(!filled(box.before, sizeof(box.before)) || !filled(box.behind, sizeof(box.behind)) ||
	   !filled(&work[told], sizeof(work) - (size_t)told * sizeof(work[0])))
	{
		printf("  written outside the state or behind the %d tokens\n", count);
		stray_writes++;
	}
	return ok;
}

static bool accepted(const char *json)
{
	return json != NULL && parse_room(json, strlen(json), WICAN_STATE_TOKENS);
}

// false, and not one byte of the state has changed
static bool refused(const char *json)
{
	return json != NULL && !parse_room(json, strlen(json), WICAN_STATE_TOKENS) && filled(&box.state, sizeof(box.state));
}

// `base` with `old` replaced by `new`. NULL unless `old` is there exactly once: a variant that changes
// nothing, or another place than meant, would test nothing. A variant of a variant is possible once.
static const char *variant(const char *base, const char *old, const char *new)
{
	static char texts[2][8192];
	static int turn = 0;
	const char *at = base != NULL && new != NULL ? strstr(base, old) : NULL;
	char *text;
	size_t head;

	if(at == NULL || strstr(at + 1, old) != NULL || strlen(base) + strlen(new) >= sizeof(texts[0]))
	{
		printf("  no variant: %s is not exactly once in the text, or the text gets too long\n", old);
		return NULL;
	}
	turn = 1 - turn;
	text = texts[turn];
	head = (size_t)(at - base);
	memcpy(text, base, head);
	strcpy(text + head, new);
	strcat(text, at + strlen(old));
	return text;
}

// The member with another value: "\"boot\":11" and "-1" give "\"boot\":-1". NULL if it gets too long.
static const char *with_value(const char *member, const char *value)
{
	static char text[4096];
	int key = (int)(strchr(member, ':') - member) + 1;

	if(snprintf(text, sizeof(text), "%.*s%s", key, member, value) >= (int)sizeof(text)) return NULL;
	return text;
}

// DISTINCT with another value for one member
static const char *distinct_with(const char *member, const char *value)
{
	return variant(DISTINCT, member, with_value(member, value));
}

// The member under a name the contract does not know: "\"boot\":11" gives "\"boot_\":11"
static const char *renamed(const char *member)
{
	static char text[128];
	int key = (int)(strchr(member, ':') - member) - 1;

	snprintf(text, sizeof(text), "%.*s_\"%s", key, member, member + key + 1);
	return text;
}

// A true or false as the byte it is stored in: a field that was never written holds neither
static int flag(const bool *field)
{
	return *(const unsigned char *)field;
}

#define SAME_FLAG(field) \
	if(flag(&box.state.field) != expected->field) \
	{ \
		printf("  %s is %d, expected %d\n", #field, flag(&box.state.field), expected->field); \
		same = false; \
	}
#define SAME_NUMBER(field) \
	if(box.state.field != expected->field) \
	{ \
		printf("  %s is %lld, expected %lld\n", #field, (long long)box.state.field, (long long)expected->field); \
		same = false; \
	}
#define SAME_TEXT(field) \
	if(memchr(box.state.field, '\0', sizeof(box.state.field)) == NULL || strcmp(box.state.field, expected->field) != 0) \
	{ \
		printf("  %s is not \"%s\"\n", #field, expected->field); \
		same = false; \
	}

// Field by field: the bytes between the fields and behind a text are nobody's business
static bool state_is(const wican_state_t *expected)
{
	bool same = true;

	SAME_TEXT(id)
	SAME_TEXT(fw)
	SAME_TEXT(git)
	SAME_NUMBER(boot)
	SAME_NUMBER(up_s)
	SAME_NUMBER(autopid)
	SAME_NUMBER(pids)
	SAME_FLAG(ecu_online)
	SAME_NUMBER(pass)
	SAME_NUMBER(rx_age_ms)
	SAME_NUMBER(mqtt)
	SAME_NUMBER(batt_mv)
	SAME_NUMBER(sleep_in_s)
	SAME_NUMBER(heap)
	SAME_NUMBER(heap_min)
	SAME_FLAG(dtc.supported)
	SAME_NUMBER(dtc.phase)
	SAME_FLAG(dtc.has_request)
	SAME_FLAG(dtc.clear)
	SAME_FLAG(dtc.from_http)
	SAME_NUMBER(dtc.seq)
	SAME_NUMBER(dtc.step)
	SAME_NUMBER(dtc.total)
	SAME_TEXT(dtc.name)
	SAME_TEXT(dtc.reason)
	SAME_NUMBER(dtc.age_s)
	SAME_NUMBER(dtc.count)
	SAME_NUMBER(dtc.result_seq)
	return same;
}

static uint32_t random_state = 20261003;

/*
 * Never two rolls in one expression whose order C leaves open, as on both sides of a |: which side is worked
 * out first is the choice of the compiler, and a number put together from two rolls would be another number
 * with another one. Where a number needs two, the first is rolled before it - from left to right, the order
 * gcc and clang both had.
 */
static uint32_t random_below(uint32_t limit)
{
	random_state = random_state * 1103515245u + 12345u;
	return (random_state >> 8) % limit;
}

/* Fixtures --------------------------------------------------------------------------------------------- */

static bool fixture_is(const char *file, const wican_state_t *expected)
{
	static char text[4096];
	char path[128];

	snprintf(path, sizeof(path), FIXTURES "%s", file);
	return read_fixture(path, text, sizeof(text)) && accepted(text) && state_is(expected);
}

// A fixture of the "dtc" object alone, inside the example of API.md
static bool dtc_fixture_is(const char *file, const wican_dtc_t *dtc)
{
	static char text[4096];
	wican_state_t expected = EXAMPLE;
	char path[128];
	size_t length = strlen(OUTER);

	expected.dtc = *dtc;
	strcpy(text, OUTER);
	snprintf(path, sizeof(path), FIXTURES "%s", file);
	if(!read_fixture(path, text + length, sizeof(text) - length - 2)) return false;
	strcat(text, "}");
	return accepted(text) && state_is(&expected);
}

static void test_fixtures(void)
{
	static const wican_state_t limits = {
		.id = "ffffffffffff", .fw = "4.21", .git = "w906-v1.4.0-9-g0123abc-dirty", .boot = 2147483647,
		.up_s = 2147483647, .autopid = WICAN_AUTOPID_RUN, .pids = 2147483647, .ecu_online = true, .pass = 2147483647,
		.rx_age_ms = 2147483647, .mqtt = WICAN_MQTT_CONNECTED, .batt_mv = 2147483600, .sleep_in_s = 2147483647,
		.heap = 2147483647, .heap_min = 2147483647,
		.dtc = {.supported = true, .phase = WICAN_DTC_DONE, .has_request = true, .clear = false, .from_http = true,
		        .seq = 2147483647, .step = 255, .total = 255, .age_s = 2147483647, .count = 65535, .result_seq = 2147483647},
	};
	static const wican_state_t offline = {
		.id = "a1b2c3d4e5f6", .fw = "4.21", .git = "w906-v1.4.0-9-g0123abc", .boot = 1234567890, .up_s = 4500,
		.autopid = WICAN_AUTOPID_RUN, .pids = 35, .ecu_online = false, .pass = 2710, .rx_age_ms = 95000,
		.mqtt = WICAN_MQTT_DISCONNECTED, .batt_mv = 12100, .sleep_in_s = 87, .heap = 58200, .heap_min = 47100,
		.dtc = {.supported = true, .phase = WICAN_DTC_ERROR, .has_request = true, .clear = true, .from_http = false,
		        .seq = 42, .step = 0, .total = 18, .reason = "engine_running", .age_s = 3, .count = 3, .result_seq = 41},
	};
	static const wican_state_t scan = {
		.id = "0123456789ab", .fw = "4.21", .git = "w906-v1.4.0", .boot = 41, .up_s = 3600,
		.autopid = WICAN_AUTOPID_RUN, .pids = 35, .ecu_online = true, .pass = 3391, .rx_age_ms = 0,
		.mqtt = WICAN_MQTT_OFF, .batt_mv = 14400, .sleep_in_s = -1, .heap = 60000, .heap_min = 47999,
		.dtc = {.supported = true, .phase = WICAN_DTC_RUNNING, .has_request = true, .clear = false, .from_http = true,
		        .seq = 41, .step = 17, .total = 18, .name = "N2/14 Rückhaltesystem (SRS)"},
	};
	static const wican_state_t starting = {
		.id = "a1b2c3d4e5f6", .fw = "4.21", .git = "w906-v1.4.0-9-g0123abc", .boot = 7, .up_s = 0,
		.autopid = WICAN_AUTOPID_STARTING, .pids = 0, .ecu_online = false, .pass = 0, .rx_age_ms = -1,
		.mqtt = WICAN_MQTT_OFF, .batt_mv = -1, .sleep_in_s = -1, .heap = 112000, .heap_min = 111000,
		.dtc = {.supported = false, .phase = WICAN_DTC_IDLE},
	};
	static const wican_state_t empty = {
		.autopid = WICAN_AUTOPID_OFF, .mqtt = WICAN_MQTT_OFF, .dtc = {.supported = true, .phase = WICAN_DTC_IDLE},
	};

	static const wican_dtc_t done = {
		.supported = true, .phase = WICAN_DTC_DONE, .has_request = true, .clear = false, .from_http = true,
		.seq = 41, .step = 18, .total = 18, .age_s = 12, .count = 3, .result_seq = 41,
	};
	static const wican_dtc_t done_clear = {
		.supported = true, .phase = WICAN_DTC_DONE, .has_request = true, .clear = true, .from_http = true,
		.seq = 101, .step = 18, .total = 18, .age_s = 1, .count = 1, .result_seq = 101,
	};
	static const wican_dtc_t error = {
		.supported = true, .phase = WICAN_DTC_ERROR, .has_request = true, .clear = true, .from_http = false,
		.seq = 42, .step = 0, .total = 18, .reason = "engine_running", .age_s = 3, .count = 3, .result_seq = 41,
	};
	static const wican_dtc_t idle = {.supported = true, .phase = WICAN_DTC_IDLE};
	static const wican_dtc_t dtc_limits = {
		.supported = true, .phase = WICAN_DTC_DONE, .has_request = true, .clear = false, .from_http = true,
		.seq = 2147483647, .step = 255, .total = 255, .age_s = 2147483647, .count = 65535, .result_seq = 2147483647,
	};
	static const wican_dtc_t queued = {
		.supported = true, .phase = WICAN_DTC_QUEUED, .has_request = true, .clear = false, .from_http = true, .seq = 41,
	};
	static const wican_dtc_t running = {
		.supported = true, .phase = WICAN_DTC_RUNNING, .has_request = true, .clear = false, .from_http = true,
		.seq = 41, .step = 5, .total = 18, .name = "N30/4 ESP",
	};
	static const wican_dtc_t running_umlaut = {
		.supported = true, .phase = WICAN_DTC_RUNNING, .has_request = true, .clear = false, .from_http = true,
		.seq = 41, .step = 17, .total = 18, .name = "N2/14 Rückhaltesystem (SRS)",
	};
	static const wican_dtc_t unsupported = {.supported = false, .phase = WICAN_DTC_IDLE};

	static char text[4096];
	const char *json;

	check(read_fixture(FIXTURES "api_state_example.json", text, sizeof(text)) && strcmp(text, OUTER IDLE "}") == 0,
	      "the answer the tests start from is the fixture api_state_example.json byte for byte");

	check(fixture_is("api_state_example.json", &EXAMPLE), "fixture api_state_example.json: the example of API.md");
	check(fixture_is("api_state_limits.json", &limits), "fixture api_state_limits.json: every number as large as the adapter writes it");
	check(fixture_is("api_state_offline.json", &offline), "fixture api_state_offline.json: ignition off, a clear that failed");
	check(fixture_is("api_state_scan.json", &scan), "fixture api_state_scan.json: a running scan, a name with an umlaut");
	check(fixture_is("api_state_starting.json", &starting), "fixture api_state_starting.json: nothing measured yet, -1 for none");

	// What the adapter writes with nothing set is not a state of API.md: no text for autopid and mqtt, no scan state
	check(read_fixture(FIXTURES "api_state_empty.json", text, sizeof(text)) && refused(text),
	      "fixture api_state_empty.json is refused: autopid, mqtt and the scan state are empty");
	json = variant(variant(text, "\"autopid\":\"\"", "\"autopid\":\"off\""), "\"mqtt\":\"\"", "\"mqtt\":\"off\"");
	check(refused(json), "fixture api_state_empty.json with texts for autopid and mqtt is still refused: \"dtc\" has no members");
	json = variant(json, "\"dtc\":{}", "\"dtc\":" IDLE);
	check(accepted(json) && state_is(&empty), "with a scan state as well it is accepted: empty texts, 0 for every number, 0.0 V");

	check(dtc_fixture_is("dtc_state_done.json", &done), "fixture dtc_state_done.json");
	check(dtc_fixture_is("dtc_state_done_clear.json", &done_clear), "fixture dtc_state_done_clear.json");
	check(dtc_fixture_is("dtc_state_error.json", &error), "fixture dtc_state_error.json");
	check(dtc_fixture_is("dtc_state_idle.json", &idle), "fixture dtc_state_idle.json: no request, action and source are empty");
	check(dtc_fixture_is("dtc_state_limits.json", &dtc_limits), "fixture dtc_state_limits.json");
	check(dtc_fixture_is("dtc_state_queued.json", &queued), "fixture dtc_state_queued.json");
	check(dtc_fixture_is("dtc_state_running.json", &running), "fixture dtc_state_running.json");
	check(dtc_fixture_is("dtc_state_running_umlaut.json", &running_umlaut), "fixture dtc_state_running_umlaut.json");
	check(dtc_fixture_is("dtc_state_unsupported.json", &unsupported), "fixture dtc_state_unsupported.json: supported is false");

	check(accepted(DISTINCT) && state_is(&DISTINCT_STATE), "every member arrives in its own field");
}

/* Members ---------------------------------------------------------------------------------------------- */

static void test_members(void)
{
	// A value of every type of JSON
	static const member_t values[] = {{"1", 'n'}, {"\"1\"", 't'}, {"true", 'b'}, {"null", 0}, {"[]", 0}, {"{}", 'o'}};
	wican_state_t unsupported = DISTINCT_STATE;
	const char *json;

	for(int i = 0; i < COUNT(MEMBERS); i++)
	{
		snprintf(what, sizeof(what), "refused: without %s", MEMBERS[i].member);
		check(refused(variant(DISTINCT, MEMBERS[i].member, renamed(MEMBERS[i].member))), what);
	}

	for(int i = 0; i < COUNT(MEMBERS); i++)
	{
		for(int v = 0; v < COUNT(values); v++)
		{
			char value[32];

			// A text for display may be any text. One of the few texts must not be "1" either,
			// and an object without members is no scan state.
			if(values[v].kind == MEMBERS[i].kind && MEMBERS[i].kind != 'o') continue;

			// The members of the scan state move to an unknown member, the text stays JSON
			snprintf(value, sizeof(value), "%s%s", values[v].member, MEMBERS[i].kind == 'o' ? ",\"x\":{" : "");
			snprintf(what, sizeof(what), "refused: %s is %s", MEMBERS[i].member, values[v].member);
			check(refused(distinct_with(MEMBERS[i].member, value)), what);
		}
	}
	check(refused(distinct_with("\"supported\":true", "\"true\"")), "refused: supported is the text \"true\"");
	unsupported.dtc.supported = false;
	check(accepted(distinct_with("\"supported\":true", "false")) && state_is(&unsupported),
	      "supported false is accepted and false, every other member is read as it stands");

	// Unknown members anywhere, also with names and values of known ones inside them
	json = variant(DISTINCT, "{\"api\":1,", "{\"new\":{\"api\":2,\"boot\":5,\"dtc\":{\"seq\":6},\"list\":[1,[2],{\"up\":3}]},\"api\":1,");
	check(accepted(json) && state_is(&DISTINCT_STATE), "an unknown member before the known ones is ignored, also what is inside it");
	json = variant(DISTINCT, "\"pids\":13,", "\"pids\":13,\"later\":\"x\",\"more\":null,\"flag\":true,\"number\":-1.5e3,");
	check(accepted(json) && state_is(&DISTINCT_STATE), "unknown members of every type between the known ones are ignored");
	json = variant(DISTINCT, "\"result_seq\":26}}", "\"result_seq\":26,\"progress\":[1,2]},\"last\":{}}");
	check(accepted(json) && state_is(&DISTINCT_STATE), "unknown members at the end of the scan state and of the answer are ignored");
	json = variant(DISTINCT, "{\"supported\":true,", "{\"first\":{\"state\":\"idle\",\"seq\":7},\"supported\":true,");
	check(accepted(json) && state_is(&DISTINCT_STATE), "an unknown member at the beginning of the scan state is ignored");

	// A member counts only where the contract has it
	json = variant(variant(DISTINCT, "\"seq\":21,", ""), "\"heap\":17,", "\"heap\":17,\"seq\":21,");
	check(refused(json), "refused: a member of the scan state outside of it");
	json = variant(variant(DISTINCT, "\"boot\":11,", ""), "\"seq\":21,", "\"seq\":21,\"boot\":11,");
	check(refused(json), "refused: a member of the answer inside the scan state");
}

/* Numbers ---------------------------------------------------------------------------------------------- */

static void test_numbers(void)
{
	static const char *const api_wrong[] = {"0", "2", "-1", "11", "1.0", "1.5", "1e0", "10e-1", "4294967297"};
	static const char *const unsigned_wrong[] = {
		"-1", "-2147483648", "0.5", "11.0", "11.5", "1e1", "1E1", "11e0", "4294967296", "9223372036854775807",
		"18446744073709551616", "99999999999999999999999999",
	};
	static const char *const signed_wrong[] = {
		"-2", "-2147483648", "-9223372036854775808", "-1.0", "-1.5", "0.5", "15.0", "1e1", "-1e0", "2147483648", "4294967295",
		"4294967296", "9223372036854775807", "99999999999999999999999999",
	};

	check(accepted(distinct_with("\"api\":1", "1")) && state_is(&DISTINCT_STATE), "api 1 is accepted");
	for(int i = 0; i < COUNT(api_wrong); i++)
	{
		snprintf(what, sizeof(what), "refused: api %s", api_wrong[i]);
		check(refused(distinct_with("\"api\":1", api_wrong[i])), what);
	}

	for(int i = 0; i < COUNT(UNSIGNED); i++)
	{
		const char *member = UNSIGNED[i].member;

		snprintf(what, sizeof(what), "%s may be 0", member);
		check(accepted(distinct_with(member, "0")) && *UNSIGNED[i].field == 0, what);
		snprintf(what, sizeof(what), "%s may be -0: minus zero is zero, not a negative number", member);
		check(accepted(distinct_with(member, "-0")) && *UNSIGNED[i].field == 0, what);
		snprintf(what, sizeof(what), "%s may be 2^31, the field has no sign", member);
		check(accepted(distinct_with(member, "2147483648")) && *UNSIGNED[i].field == 2147483648u, what);
		snprintf(what, sizeof(what), "%s may be 2^32-1", member);
		check(accepted(distinct_with(member, "4294967295")) && *UNSIGNED[i].field == 4294967295u, what);
		for(int v = 0; v < COUNT(unsigned_wrong); v++)
		{
			snprintf(what, sizeof(what), "refused: %s with the value %s", member, unsigned_wrong[v]);
			check(refused(distinct_with(member, unsigned_wrong[v])), what);
		}
	}

	for(int i = 0; i < COUNT(SIGNED); i++)
	{
		const char *member = SIGNED[i].member;

		snprintf(what, sizeof(what), "%s may be -1 for none", member);
		check(accepted(distinct_with(member, "-1")) && *SIGNED[i].field == -1, what);
		snprintf(what, sizeof(what), "%s may be 0", member);
		check(accepted(distinct_with(member, "0")) && *SIGNED[i].field == 0, what);
		snprintf(what, sizeof(what), "%s may be -0: minus zero is zero", member);
		check(accepted(distinct_with(member, "-0")) && *SIGNED[i].field == 0, what);
		snprintf(what, sizeof(what), "%s may be 2^31-1", member);
		check(accepted(distinct_with(member, "2147483647")) && *SIGNED[i].field == 2147483647, what);
		for(int v = 0; v < COUNT(signed_wrong); v++)
		{
			snprintf(what, sizeof(what), "refused: %s with the value %s", member, signed_wrong[v]);
			check(refused(distinct_with(member, signed_wrong[v])), what);
		}
	}
}

/* Battery voltage -------------------------------------------------------------------------------------- */

static bool batt_is(const char *value, int32_t millivolts)
{
	if(!accepted(distinct_with("\"batt_v\":1.9", value))) return false;
	if(box.state.batt_mv != millivolts) printf("  batt_v %s is %ld mV, expected %ld\n", value, (long)box.state.batt_mv, (long)millivolts);
	return box.state.batt_mv == millivolts;
}

static void test_battery(void)
{
	static const struct
	{
		const char *value;
		int32_t millivolts;
		const char *rule;
	} good[] = {
		{"12.4", 12400, "one decimal, as the adapter writes it"},
		{"12.449", 12449, "three decimals are millivolts"},
		{"0", 0, "zero"},
		{"-1", -1, "-1 is not measured"},
		{"0.0", 0, "zero with a decimal"},
		{"12", 12000, "no decimal"},
		{"12.44", 12440, "two decimals"},
		{"0.001", 1, "one millivolt"},
		{"9.999", 9999, "three nines"},
		{"0.05", 50, "a zero right behind the point"},
		{"10.05", 10050, "zeros before and behind the point"},
		{"12.4499", 12449, "digits below a millivolt are dropped, not rounded"},
		{"12.4495", 12449, "half a millivolt is dropped too"},
		{"0.0009", 0, "less than a millivolt is zero"},
		{"0.999999", 999, "many digits below a millivolt"},
		{"1.0000000000000000000000000000000000000001", 1000, "40 decimals"},
		{"-12.4", -1, "a negative voltage is not measured"},
		{"-0.1", -1, "a negative voltage below one volt"},
		{"-0.0004", -1, "negative by less than a millivolt"},
		{"-0", 0, "minus zero is zero, not negative"},
		{"-0.0", 0, "minus zero with a decimal"},
		{"-0.000", 0, "minus zero with three decimals"},
		{"-2147483.648", -1, "negative beyond 2^31 millivolts"},
		{"-99999999999999999999999999", -1, "negative with 26 digits"},
		{"2147483.6", 2147483600, "the largest value the adapter writes"},
		{"2147483.647", 2147483647, "2^31-1 millivolts"},
		{"2147483.6479", 2147483647, "2^31-1 millivolts and less than one more"},
		{"2147483", 2147483000, "the largest whole number of volts"},
		{"214748.3647", 214748364, "the digits of 2^31-1 with the point elsewhere"},
	};
	static const char *const wrong[] = {
		"2147483.648", "2147483.65", "2147483.7", "2147484", "2147484.0", "21474836.47", "2147483647", "4294967.296",
		"4294979.696", "4294967296", "99999999999999999999999999", "99999999999999999999999999.9",
		"1e1", "1E1", "1.24e1", "124e-1", "124E-1", "0e0", "0.0e0", "-1e0", "12.4e0", "1.2345e1", "0.0000E0", "-1.23456e-2",
	};
	static char digits[3001];
	bool same = true;

	for(int i = 0; i < COUNT(good); i++)
	{
		snprintf(what, sizeof(what), "batt_v %s is %ld mV: %s", good[i].value, (long)good[i].millivolts, good[i].rule);
		check(batt_is(good[i].value, good[i].millivolts), what);
	}
	for(int i = 0; i < COUNT(wrong); i++)
	{
		snprintf(what, sizeof(what), "refused: batt_v %s (more than 2^31-1 mV, or an exponent)", wrong[i]);
		check(refused(distinct_with("\"batt_v\":1.9", wrong[i])), what);
	}

	// Numbers of 3000 characters: every digit is looked at, however many there are
	memset(digits, '0', 3000);
	memcpy(digits, "12.449", 6);
	check(batt_is(digits, 12449), "batt_v 12.449 followed by 2994 zeros is 12449 mV");
	digits[2999] = '9';
	check(batt_is(digits, 12449), "batt_v 12.449 followed by 2993 zeros and a nine is 12449 mV");
	memcpy(digits, "-0.000", 6);
	check(batt_is(digits, -1), "batt_v minus zero with a nine as the 2997th decimal is negative: not measured");
	digits[2999] = '0';
	check(batt_is(digits, 0), "batt_v minus zero with 2997 decimals is zero");
	memset(digits, '9', 3000);
	check(refused(distinct_with("\"batt_v\":1.9", digits)), "refused: batt_v of 3000 nines");
	digits[0] = '-';
	check(batt_is(digits, -1), "batt_v minus 2999 nines is not measured");
	memcpy(digits, "0.", 2);
	check(batt_is(digits, 999), "batt_v 0.999 followed by 2995 more nines is 999 mV");

	// Every voltage up to 30 V in millivolts: no value is off by one as a floating point product would be
	for(int32_t millivolts = 0; millivolts <= 30000 && same; millivolts++)
	{
		char value[32];

		snprintf(value, sizeof(value), "%d.%03d", (int)(millivolts / 1000), (int)(millivolts % 1000));
		same = batt_is(value, millivolts);
	}
	check(same, "every batt_v from 0.000 to 30.000 becomes exactly its millivolts");

	same = true;
	for(int32_t tenths = 0; tenths <= 1000 && same; tenths++)
	{
		char value[32];

		snprintf(value, sizeof(value), "%d.%d", (int)(tenths / 10), (int)(tenths % 10));
		same = batt_is(value, tenths * 100);
	}
	check(same, "every batt_v from 0.0 to 100.0 with one decimal becomes exactly its millivolts");

	// Whole volts and up to six decimals chosen at random; the expected value is put together from the same parts
	same = true;
	for(int i = 0; i < 3000 && same; i++)
	{
		uint32_t volts = random_below(4) == 0 ? 2147400 + random_below(200) : random_below(100000);
		int decimals = (int)random_below(7);
		int64_t millivolts = (int64_t)volts * 1000;
		char value[32];
		int length = snprintf(value, sizeof(value), "%lu", (unsigned long)volts);

		if(decimals > 0) value[length++] = '.';
		for(int d = 0; d < decimals; d++)
		{
			int digit = (int)random_below(10);

			value[length++] = (char)('0' + digit);
			if(d == 0) millivolts += digit * 100;
			if(d == 1) millivolts += digit * 10;
			if(d == 2) millivolts += digit;
		}
		value[length] = '\0';

		if(millivolts > 2147483647)
		{
			same = refused(distinct_with("\"batt_v\":1.9", value));
			if(!same) printf("  batt_v %s is not refused\n", value);
		}
		else
		{
			same = batt_is(value, (int32_t)millivolts);
		}
	}
	check(same, "3000 voltages chosen at random become the millivolts of their digits, above 2^31-1 mV they are refused");
}

/* The few texts ---------------------------------------------------------------------------------------- */

static void test_choices(void)
{
	static const struct
	{
		const char *member;
		const char *text;
		int value;
	} good[] = {
		{"\"autopid\":\"starting\"", "off", WICAN_AUTOPID_OFF}, {"\"autopid\":\"starting\"", "starting", WICAN_AUTOPID_STARTING},
		{"\"autopid\":\"starting\"", "run", WICAN_AUTOPID_RUN},
		{"\"ecu\":\"online\"", "online", 1}, {"\"ecu\":\"online\"", "offline", 0},
		{"\"mqtt\":\"disconnected\"", "off", WICAN_MQTT_OFF}, {"\"mqtt\":\"disconnected\"", "connected", WICAN_MQTT_CONNECTED},
		{"\"mqtt\":\"disconnected\"", "disconnected", WICAN_MQTT_DISCONNECTED},
		{"\"state\":\"running\"", "idle", WICAN_DTC_IDLE}, {"\"state\":\"running\"", "queued", WICAN_DTC_QUEUED},
		{"\"state\":\"running\"", "running", WICAN_DTC_RUNNING}, {"\"state\":\"running\"", "done", WICAN_DTC_DONE},
		{"\"state\":\"running\"", "error", WICAN_DTC_ERROR},
	};
	static const struct
	{
		const char *member;
		const char *texts[8];
	} wrong[] = {
		{"\"autopid\":\"starting\"", {"", "on", "RUN", "ru", "runs", "running", "of", "start"}},
		{"\"ecu\":\"online\"", {"", "on", "off", "Online", "onlin", "onlinee", "offlin", "true"}},
		{"\"mqtt\":\"disconnected\"", {"", "on", "connecte", "connecting", "Connected", "disconnect", "of", "offf"}},
		{"\"state\":\"running\"", {"", "busy", "Done", "don", "donee", "run", "errors", "idl"}},
		{"\"action\":\"read\"", {" ", "scan", "rea", "READ", "reads", "clea", "cleared", "read_dtc"}},
		{"\"src\":\"http\"", {" ", "ble", "htt", "https", "HTTP", "mqt", "mqtts", "web"}},
	};
	static const struct
	{
		const char *action;
		const char *source;
		bool has_request, clear, from_http;
		const char *rule;
	} requests[] = {
		{"read", "http", true, false, true, "a read over HTTP"},
		{"read", "mqtt", true, false, false, "a read over MQTT"},
		{"clear", "http", true, true, true, "a clear over HTTP"},
		{"clear", "mqtt", true, true, false, "a clear over MQTT"},
		{"", "", false, false, false, "action and source empty: no request"},
		{"read", "", false, false, false, "an action without source is no request"},
		{"clear", "", false, true, false, "a clear without source is no request, but still a clear"},
		{"", "http", false, false, true, "a source without action is no request"},
		{"", "mqtt", false, false, false, "the source MQTT without action is no request"},
	};
	char quoted[64];

	for(int i = 0; i < COUNT(good); i++)
	{
		wican_state_t expected = DISTINCT_STATE;
		int value = good[i].value;

		if(strstr(good[i].member, "autopid") != NULL) expected.autopid = (wican_autopid_t)value;
		if(strstr(good[i].member, "ecu") != NULL) expected.ecu_online = value == 1;
		if(strstr(good[i].member, "mqtt") != NULL) expected.mqtt = (wican_mqtt_t)value;
		if(strstr(good[i].member, "state") != NULL) expected.dtc.phase = (wican_dtc_phase_t)value;

		snprintf(quoted, sizeof(quoted), "\"%s\"", good[i].text);
		snprintf(what, sizeof(what), "%.*s may be %s", (int)(strchr(good[i].member, ':') - good[i].member), good[i].member, quoted);
		check(accepted(distinct_with(good[i].member, quoted)) && state_is(&expected), what);
	}

	for(int i = 0; i < COUNT(wrong); i++)
	{
		for(int t = 0; t < COUNT(wrong[i].texts); t++)
		{
			snprintf(quoted, sizeof(quoted), "\"%s\"", wrong[i].texts[t]);
			snprintf(what, sizeof(what), "refused: %.*s with the unknown text %s",
			         (int)(strchr(wrong[i].member, ':') - wrong[i].member), wrong[i].member, quoted);
			check(refused(distinct_with(wrong[i].member, quoted)), what);
		}
	}

	for(int i = 0; i < COUNT(requests); i++)
	{
		wican_state_t expected = DISTINCT_STATE;
		char action[32], source[32];

		expected.dtc.has_request = requests[i].has_request;
		expected.dtc.clear = requests[i].clear;
		expected.dtc.from_http = requests[i].from_http;
		snprintf(action, sizeof(action), "\"action\":\"%s\"", requests[i].action);
		snprintf(source, sizeof(source), "\"src\":\"%s\"", requests[i].source);
		check(accepted(variant(variant(DISTINCT, "\"action\":\"read\"", action), "\"src\":\"http\"", source)) && state_is(&expected),
		      requests[i].rule);
	}
}

/* Texts for display ------------------------------------------------------------------------------------ */

// DISTINCT with another text, given as it stands in JSON, for one of the members of TEXTS. NULL if it does not fit.
static const char *distinct_with_text(const text_t *text, const char *json_text)
{
	static char value[4096];

	if(snprintf(value, sizeof(value), "\"%s\"", json_text) >= (int)sizeof(value)) return NULL;
	return distinct_with(text->member, value);
}

// The field holds exactly these bytes and ends inside itself
static bool field_is(const text_t *text, const char *expected, size_t length)
{
	return memchr(text->field, '\0', text->size) != NULL && strlen(text->field) == length &&
	       memcmp(text->field, expected, length) == 0;
}

// Characters as they stand in JSON and as UTF-8, of every length and every kind of escape
static const struct
{
	const char *json;
	const char *utf8;
} CHARACTERS[] = {
	{"a", "a"}, {"Z", "Z"}, {" ", " "}, {"(", "("}, {"\x7F", "\x7F"},
	{"\xC3\xBC", "\xC3\xBC"}, {"\xC3\x9F", "\xC3\x9F"}, {"\xDF\xBF", "\xDF\xBF"},
	{"\xE2\x82\xAC", "\xE2\x82\xAC"}, {"\xE4\xB8\xAD", "\xE4\xB8\xAD"}, {"\xEF\xBF\xBD", "\xEF\xBF\xBD"},
	{"\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80"}, {"\xF0\x9D\x84\x9E", "\xF0\x9D\x84\x9E"},
	{"\\\"", "\""}, {"\\\\", "\\"}, {"\\/", "/"}, {"\\n", "\n"}, {"\\t", "\t"},
	{"\\u0041", "A"}, {"\\u00e4", "\xC3\xA4"}, {"\\u00DF", "\xC3\x9F"}, {"\\u20AC", "\xE2\x82\xAC"},
	{"\\uD83D\\uDE00", "\xF0\x9F\x98\x80"}, {"\\ud834\\udd1e", "\xF0\x9D\x84\x9E"},
};

static void test_texts(void)
{
	static char long_text[1024];
	static char json_text[4096];
	static char expected[4096];
	wican_state_t ended;
	bool same;

	for(size_t i = 0; i < sizeof(long_text) - 1; i++) long_text[i] = (char)('a' + i % 26);

	for(int i = 0; i < COUNT(TEXTS); i++)
	{
		const text_t *text = &TEXTS[i];
		size_t room = text->size - 1;
		static char value[1024];

		snprintf(what, sizeof(what), "%s may be empty", text->member);
		check(accepted(distinct_with_text(text, "")) && field_is(text, "", 0), what);

		snprintf(value, sizeof(value), "%.*s", (int)(room - 1), long_text);
		snprintf(what, sizeof(what), "%s of %d bytes is kept whole", text->member, (int)(room - 1));
		check(accepted(distinct_with_text(text, value)) && field_is(text, long_text, room - 1), what);

		snprintf(value, sizeof(value), "%.*s", (int)room, long_text);
		snprintf(what, sizeof(what), "%s of %d bytes fills its field and is kept whole", text->member, (int)room);
		check(accepted(distinct_with_text(text, value)) && field_is(text, long_text, room), what);

		snprintf(value, sizeof(value), "%.*s", (int)(room + 1), long_text);
		snprintf(what, sizeof(what), "%s of %d bytes is cut to %d, the answer is accepted", text->member, (int)(room + 1), (int)room);
		check(accepted(distinct_with_text(text, value)) && field_is(text, long_text, room), what);

		snprintf(what, sizeof(what), "%s of 1023 bytes is cut to %d", text->member, (int)room);
		check(accepted(distinct_with_text(text, long_text)) && field_is(text, long_text, room), what);
	}

	// A character of 2, 3 and 4 bytes at every position around the end of every field
	for(int i = 0; i < COUNT(TEXTS); i++)
	{
		static const char *const characters[] = {"\xC3\xBC", "\xE2\x82\xAC", "\xF0\x9F\x98\x80"};
		const text_t *text = &TEXTS[i];
		size_t room = text->size - 1;

		for(int c = 0; c < COUNT(characters); c++)
		{
			size_t bytes = strlen(characters[c]);

			same = true;
			for(size_t before = room - 6; before <= room + 1; before++)
			{
				// Whole if it ends inside the field, else gone: with the letter behind it, or without
				size_t kept = before + bytes <= room ? before + bytes : before;

				if(kept > room) kept = room;
				snprintf(json_text, sizeof(json_text), "%.*s%sz", (int)before, long_text, characters[c]);
				snprintf(expected, sizeof(expected), "%.*s%s", (int)before, long_text, characters[c]);
				if(before + bytes + 1 <= room)
				{
					strcat(expected, "z");
					kept++;
				}
				if(!accepted(distinct_with_text(text, json_text)) || !field_is(text, expected, kept))
				{
					printf("  %d bytes before the character: not the first %d bytes\n", (int)before, (int)kept);
					same = false;
				}
			}
			snprintf(what, sizeof(what), "%s: a character of %d bytes at the end of the field is kept whole or left out",
			         text->member, (int)bytes);
			check(same, what);
		}
	}

	// Escapes count with the bytes they stand for
	check(accepted(distinct_with_text(&TEXTS[3], "a\\\"b\\\\c\\/d\\u00fc\\uD83D\\uDE00")) &&
	      field_is(&TEXTS[3], "a\"b\\c/d\xC3\xBC\xF0\x9F\x98\x80", 13), "escapes in a text are resolved");
	check(accepted(distinct_with_text(&TEXTS[3], "x\\uD83D\\uDE00")) && field_is(&TEXTS[3], "x\xF0\x9F\x98\x80", 5),
	      "a surrogate pair at the very end of a text is one character");
	for(int i = 0; i < 64; i++) strcpy(json_text + 2 * i, "\\\\");
	memset(expected, '\\', 64);
	check(accepted(distinct_with_text(&TEXTS[3], json_text)) && field_is(&TEXTS[3], expected, 63),
	      "64 escaped backslashes are 64 bytes: cut to 63");
	json_text[2 * 63] = '\0';
	check(accepted(distinct_with_text(&TEXTS[3], json_text)) && field_is(&TEXTS[3], expected, 63),
	      "63 escaped backslashes are 63 bytes although they are 126 in JSON: kept whole");
	for(int i = 0; i < 32; i++)
	{
		strcpy(json_text + 6 * i, "\\u00fc");
		strcpy(expected + 2 * i, "\xC3\xBC");
	}
	check(accepted(distinct_with_text(&TEXTS[3], json_text)) && field_is(&TEXTS[3], expected, 62),
	      "32 escaped characters of 2 bytes: 31 fit into 63 bytes, the last is not cut in half");
	for(int i = 0; i < 16; i++)
	{
		strcpy(json_text + 12 * i, "\\uD83D\\uDE00");
		strcpy(expected + 4 * i, "\xF0\x9F\x98\x80");
	}
	check(accepted(distinct_with_text(&TEXTS[3], json_text)) && field_is(&TEXTS[3], expected, 60),
	      "16 surrogate pairs of 4 bytes: 15 fit into 63 bytes");

	// What json_text() refuses ends the text, the answer stays valid
	ended = DISTINCT_STATE;
	strcpy(ended.dtc.name, "ab");
	check(accepted(distinct_with_text(&TEXTS[3], "ab\\u0000cd")) && state_is(&ended),
	      "a text ends before \\u0000, the members behind it are read as usual");
	check(accepted(distinct_with_text(&TEXTS[3], "\\u0000")) && field_is(&TEXTS[3], "", 0), "a text of \\u0000 alone is empty");
	check(accepted(distinct_with_text(&TEXTS[3], "ab\\uD83Dcd")) && field_is(&TEXTS[3], "ab", 2),
	      "a text ends before the first half of a surrogate pair without the second");
	check(accepted(distinct_with_text(&TEXTS[3], "ab\\uD83D")) && field_is(&TEXTS[3], "ab", 2),
	      "a text ends before half a surrogate pair at its end");
	check(accepted(distinct_with_text(&TEXTS[3], "ab\\uD83D\\u0041cd")) && field_is(&TEXTS[3], "ab", 2),
	      "a text ends before a first half followed by another character");
	check(accepted(distinct_with_text(&TEXTS[3], "ab\\uDE00\\uD83Dcd")) && field_is(&TEXTS[3], "ab", 2),
	      "a text ends before the second half of a surrogate pair alone");

	// Bytes that are no UTF-8 must not lead anywhere: the field ends inside itself, the answer is accepted
	check(accepted(distinct_with_text(&TEXTS[3], "a\xFF" "b\x80")) && field_is(&TEXTS[3], "a\xFF" "b\x80", 4),
	      "bytes that are no UTF-8 are passed on if they fit");
	memset(json_text, 0x80, 200);
	json_text[200] = '\0';
	check(accepted(distinct_with_text(&TEXTS[3], json_text)) && field_is(&TEXTS[3], "", 0),
	      "200 continuation bytes have no character boundary but the beginning of the text: cut to nothing");
	json_text[64] = '\0';
	check(accepted(distinct_with_text(&TEXTS[3], json_text)) && field_is(&TEXTS[3], "", 0),
	      "64 continuation bytes, one more than fit: cut to nothing");
	json_text[63] = '\0';
	check(accepted(distinct_with_text(&TEXTS[3], json_text)) && field_is(&TEXTS[3], json_text, 63),
	      "63 continuation bytes fit and are passed on");
	memset(json_text, 0xF0, 200);
	check(accepted(distinct_with_text(&TEXTS[4], json_text)) && field_is(&TEXTS[4], json_text, 31),
	      "200 first bytes of characters: each stands alone, 31 fit");

	// Texts of random characters in every field: as many whole characters as fit, counted one by one
	same = true;
	for(int i = 0; i < 2000 && same; i++)
	{
		const text_t *text = &TEXTS[random_below(COUNT(TEXTS))];
		int characters = (int)random_below(50);
		size_t kept = 0;
		bool cut = false;

		json_text[0] = '\0';
		expected[0] = '\0';
		for(int c = 0; c < characters; c++)
		{
			int pick = (int)random_below(COUNT(CHARACTERS));

			strcat(json_text, CHARACTERS[pick].json);
			if(!cut && kept + strlen(CHARACTERS[pick].utf8) <= text->size - 1)
			{
				strcat(expected, CHARACTERS[pick].utf8);
				kept += strlen(CHARACTERS[pick].utf8);
			}
			else
			{
				cut = true;
			}
		}
		if(!accepted(distinct_with_text(text, json_text)) || !field_is(text, expected, kept))
		{
			printf("  %s with the text %s\n", text->member, json_text);
			same = false;
		}
	}
	check(same, "2000 texts of random characters and escapes: as many whole characters as fit into the field");
}

/* Every member at random ------------------------------------------------------------------------------- */

// A number up to `highest`: a small one, one at the limit, or any
static uint32_t random_number(uint32_t highest)
{
	uint32_t high = random_below(65536);
	uint32_t any = high << 16 | random_below(65536);

	switch(random_below(4))
	{
		case 0:  return random_below(20);
		case 1:  return highest - random_below(3);
		default: return highest == UINT32_MAX ? any : any % (highest + 1);
	}
}

// Each member has to arrive in its own field whatever the other members are: an answer is put together
// member by member, and next to it the state it has to become
static void test_random(void)
{
	static const char *const autopid[] = {"off", "starting", "run"};
	static const char *const mqtt[] = {"off", "connected", "disconnected"};
	static const char *const phase[] = {"idle", "queued", "running", "done", "error"};
	static const char *const action[] = {"", "read", "clear"};
	static const char *const source[] = {"", "mqtt", "http"};
	static const char *const words[] = {"", "a", "4.21", "0123456789ab", "N30/4 ESP", "engine_running", "w906-v1.4.0-9-g0123abc"};
	static char text[2048];
	bool same = true;

	for(int i = 0; i < 3000 && same; i++)
	{
		wican_state_t e;
		int chosen_action = (int)random_below(COUNT(action));
		int chosen_source = (int)random_below(COUNT(source));
		int tenths = (int)random_below(400);
		char batt[32];

		memset(&e, 0, sizeof(e));
		strcpy(e.id, words[random_below(COUNT(words))]);
		strcpy(e.fw, words[random_below(COUNT(words))]);
		strcpy(e.git, words[random_below(COUNT(words))]);
		e.boot = random_number(UINT32_MAX);
		e.up_s = random_number(UINT32_MAX);
		e.autopid = (wican_autopid_t)random_below(COUNT(autopid));
		e.pids = random_number(UINT32_MAX);
		e.ecu_online = random_below(2) == 1;
		e.pass = random_number(UINT32_MAX);
		e.rx_age_ms = (int32_t)((int64_t)random_number(INT32_MAX + 1u) - 1);
		e.mqtt = (wican_mqtt_t)random_below(COUNT(mqtt));
		e.batt_mv = random_below(5) == 0 ? -1 : tenths * 100;
		e.sleep_in_s = (int32_t)((int64_t)random_number(INT32_MAX + 1u) - 1);
		e.heap = random_number(UINT32_MAX);
		e.heap_min = random_number(UINT32_MAX);
		e.dtc.supported = random_below(2) == 1;
		e.dtc.phase = (wican_dtc_phase_t)random_below(COUNT(phase));
		e.dtc.has_request = chosen_action != 0 && chosen_source != 0;
		e.dtc.clear = chosen_action == 2;
		e.dtc.from_http = chosen_source == 2;
		e.dtc.seq = random_number(UINT32_MAX);
		e.dtc.step = random_number(UINT32_MAX);
		e.dtc.total = random_number(UINT32_MAX);
		strcpy(e.dtc.name, words[random_below(COUNT(words))]);
		strcpy(e.dtc.reason, words[random_below(COUNT(words))]);
		e.dtc.age_s = random_number(UINT32_MAX);
		e.dtc.count = random_number(UINT32_MAX);
		e.dtc.result_seq = random_number(UINT32_MAX);

		if(e.batt_mv < 0) strcpy(batt, "-1");
		else snprintf(batt, sizeof(batt), "%d.%d", tenths / 10, tenths % 10);
		snprintf(text, sizeof(text),
		         "{\"api\":1,\"id\":\"%s\",\"fw\":\"%s\",\"git\":\"%s\",\"boot\":%lu,\"up\":%lu,\"autopid\":\"%s\",\"pids\":%lu,\"ecu\":\"%s\","
		         "\"pass\":%lu,\"rx_age_ms\":%ld,\"mqtt\":\"%s\",\"batt_v\":%s,\"sleep_in_s\":%ld,\"heap\":%lu,\"heap_min\":%lu,"
		         "\"dtc\":{\"supported\":%s,\"state\":\"%s\",\"action\":\"%s\",\"src\":\"%s\",\"seq\":%lu,\"ecu\":%lu,\"total\":%lu,"
		         "\"name\":\"%s\",\"reason\":\"%s\",\"age_s\":%lu,\"count\":%lu,\"result_seq\":%lu}}",
		         e.id, e.fw, e.git, (unsigned long)e.boot, (unsigned long)e.up_s, autopid[e.autopid], (unsigned long)e.pids,
		         e.ecu_online ? "online" : "offline", (unsigned long)e.pass, (long)e.rx_age_ms, mqtt[e.mqtt], batt, (long)e.sleep_in_s,
		         (unsigned long)e.heap, (unsigned long)e.heap_min, e.dtc.supported ? "true" : "false", phase[e.dtc.phase],
		         action[chosen_action], source[chosen_source], (unsigned long)e.dtc.seq, (unsigned long)e.dtc.step,
		         (unsigned long)e.dtc.total, e.dtc.name, e.dtc.reason, (unsigned long)e.dtc.age_s, (unsigned long)e.dtc.count,
		         (unsigned long)e.dtc.result_seq);
		same = accepted(text) && state_is(&e);
		if(!same) printf("  answer %d: %s\n", i, text);
	}
	check(same, "3000 answers with every member chosen at random: each arrives in its own field, whatever the others are");
}

/* Broken JSON and the room for the reader -------------------------------------------------------------- */

static void test_json(void)
{
	static const char *const no_state[] = {"", " ", "{}", "[]", "1", "null", "\"api\"", "[" DISTINCT "]", "{\"state\":" DISTINCT "}"};
	static char text[4096];
	size_t length = strlen(DISTINCT);
	bool same = true;

	for(int i = 0; i < COUNT(no_state); i++)
	{
		snprintf(what, sizeof(what), "refused: %.60s", no_state[i]);
		check(refused(no_state[i]), what);
	}
	check(refused(DISTINCT "}"), "refused: a brace behind the answer");
	check(refused(DISTINCT "x"), "refused: text behind the answer");
	check(refused(variant(DISTINCT, "\"up\":12,", "\"up\":12,,")), "refused: two commas");
	check(refused(variant(DISTINCT, "\"up\":12,", "\"up\":012,")), "refused: a number with a leading zero");
	check(refused(variant(DISTINCT, "\"id\":\"i\"", "\"id\":\"i\n\"")), "refused: a line break inside a text");

	snprintf(text, sizeof(text), "%sgarbage", DISTINCT);
	check(parse_room(text, length, WICAN_STATE_TOKENS) && state_is(&DISTINCT_STATE), "bytes behind `length` are not looked at");

	// DISTINCT has 59 tokens: the object, 17 names and values, 12 names and values of the scan state
	check(parse_room(DISTINCT, length, 59) && state_is(&DISTINCT_STATE), "59 tokens are enough for an answer without unknown members");
	check(!parse_room(DISTINCT, length, 58) && filled(&box.state, sizeof(box.state)), "refused: room for 58 tokens, one too few");
	check(!parse_room(DISTINCT, length, 1) && filled(&box.state, sizeof(box.state)), "refused: room for one token");
	check(!parse_room(DISTINCT, length, 0) && filled(&box.state, sizeof(box.state)), "refused: no room for tokens");
	check(!parse_room(DISTINCT, length, -1) && filled(&box.state, sizeof(box.state)), "refused: a negative room for tokens");

	// An unknown member with a list of n numbers adds 2 + n tokens: 59 + 2 + 35 are 96
	for(int numbers = 35; numbers <= 36; numbers++)
	{
		strcpy(text, "{\"x\":[0");
		for(int i = 1; i < numbers; i++) strcat(text, ",0");
		strcat(text, "],");
		strcat(text, &DISTINCT[1]);
		if(numbers == 35)
		{
			check(WICAN_STATE_TOKENS == 96 && accepted(text) && state_is(&DISTINCT_STATE),
			      "an answer of exactly WICAN_STATE_TOKENS tokens, 37 of them unknown, is accepted");
		}
		else
		{
			check(refused(text), "refused: an answer of WICAN_STATE_TOKENS + 1 tokens, the unknown ones first");
		}

		// The same behind the known members: they are all read before the room runs out
		snprintf(text, sizeof(text), "%.*s,\"x\":[0", (int)length - 1, DISTINCT);
		for(int i = 1; i < numbers; i++) strcat(text, ",0");
		strcat(text, "]}");
		if(numbers == 35)
		{
			check(accepted(text) && state_is(&DISTINCT_STATE), "exactly WICAN_STATE_TOKENS tokens, the unknown ones last, are accepted");
		}
		else
		{
			check(refused(text), "refused: an answer of WICAN_STATE_TOKENS + 1 tokens, the unknown ones last");
		}
	}

	// A caller may give more room than the header names: 59 + 2 + 131 are 192
	strcpy(text, "{\"x\":[0");
	for(int i = 1; i < 131; i++) strcat(text, ",0");
	strcat(text, "],");
	strcat(text, &DISTINCT[1]);
	check(parse_room(text, strlen(text), 2 * WICAN_STATE_TOKENS) && state_is(&DISTINCT_STATE),
	      "an answer of 192 tokens is accepted with room for 192: all the room that is given is used");
	check(!parse_room(text, strlen(text), 2 * WICAN_STATE_TOKENS - 1) && filled(&box.state, sizeof(box.state)),
	      "refused: an answer of 192 tokens with room for 191");

	// The answer is level 1, the reader goes down to level 8
	snprintf(text, sizeof(text), "%.*s,\"x\":[[[[[[[0]]]]]]]}", (int)length - 1, DISTINCT);
	check(JSON_MAX_DEPTH == 8 && accepted(text) && state_is(&DISTINCT_STATE), "an unknown member nested as deep as the reader goes is ignored");
	snprintf(text, sizeof(text), "%.*s,\"x\":[[[[[[[[0]]]]]]]]}", (int)length - 1, DISTINCT);
	check(refused(text), "refused: an unknown member nested deeper than the reader goes");

	// Last: a reader that went on after an error would walk through tokens that were never written
	for(size_t cut = 0; cut < length; cut++)
	{
		if(parse_room(DISTINCT, cut, WICAN_STATE_TOKENS) || !filled(&box.state, sizeof(box.state)))
		{
			printf("  the first %d bytes are accepted or change the state\n", (int)cut);
			same = false;
		}
	}
	check(same, "refused: every beginning of an answer that is not the whole answer");
}

int main(void)
{
	test_fixtures();
	test_members();
	test_numbers();
	test_battery();
	test_choices();
	test_texts();
	test_random();
	test_json();
	check(stray_writes == 0, "no call wrote before or behind the state or behind the room for tokens");
	return test_end();
}
