/*
 * Host test for display/components/core/values.c. Run "make test_values && ./test_values" in display/test.
 * redproof.py removes or weakens every rule once (mutations/values.py) and expects this test to fail.
 */
#include <stdlib.h>
#include <stdint.h>
#include <float.h>
#include "test.h"
#include "values.h"

#define GUARD 0xA5

// The values lie between guard bytes: a value written behind the last place lands there and is seen,
// where the address sanitizer would only stop the program without naming a rule.
typedef struct
{
	unsigned char before[128];
	values_t values;
	unsigned char after[128];
} box_t;

static box_t box, snapshot;

// Two tokens more than the reader is told about show whether it writes behind its room
static json_token_t work[VALUES_TOKENS + 2];
static json_token_t large_work[1024];
static char text[16384];

static bool guards_intact(void)
{
	size_t i;

	for(i = 0; i < sizeof(box.before); i++)
	{
		if(box.before[i] != GUARD) return false;
	}
	for(i = 0; i < sizeof(box.after); i++)
	{
		if(box.after[i] != GUARD) return false;
	}
	return true;
}

// Fresh values between fresh guard bytes
static void start(void)
{
	memset(&box, GUARD, sizeof(box));
	values_init(&box.values);
}

static void remember(void)
{
	memcpy(&snapshot, &box, sizeof(box));
}

static bool unchanged(void)
{
	return memcmp(&snapshot, &box, sizeof(box)) == 0;
}

static values_result_t apply(const char *json, int64_t pass, uint64_t now_ms)
{
	return values_apply(&box.values, json, strlen(json), pass, now_ms, work, VALUES_TOKENS);
}

static bool is_number(const char *name, double number, uint64_t seen_ms)
{
	const value_t *value = values_find(&box.values, name);

	return value != NULL && strcmp(value->name, name) == 0 && value->kind == VALUE_NUMBER &&
	       value->number == number && value->seen_ms == seen_ms;
}

static bool is_switch(const char *name, value_kind_t kind, uint64_t seen_ms)
{
	const value_t *value = values_find(&box.values, name);

	return value != NULL && strcmp(value->name, name) == 0 && value->kind == kind &&
	       value->number == 0 && value->seen_ms == seen_ms;
}

static bool is_missing(const char *name)
{
	return values_find(&box.values, name) == NULL;
}

// {"V00":0,"V01":1,...}: `count` members with the numbers i + offset. Built here, not by the code under test.
static void many(int count, int offset)
{
	size_t length = 0;
	int i;

	length += (size_t)snprintf(text + length, sizeof(text) - length, "{");
	for(i = 0; i < count; i++)
	{
		length += (size_t)snprintf(text + length, sizeof(text) - length, "%s\"V%02d\":%d", i ? "," : "", i, i + offset);
	}
	snprintf(text + length, sizeof(text) - length, "}");
}

// What tools/w906/fixtures/autopid_data_ignition_on.json says, copied by hand
static const struct
{
	const char *name;
	double number;
} ignition_on[] = {
	{"ENGINE_RPM", 0}, {"CHARGE_AIR_TEMP_PRE_IC", 21.5}, {"CHARGE_AIR_TEMP_POST_IC", 21.25}, {"EGT_PRE_TURBO", 22},
	{"EGT_POST_EGR_COOLER", 21.75}, {"EGT_PRE_CAT", 23}, {"EGT_PRE_DPF", 24}, {"EGT_PRE_SCR", 22}, {"FUEL_TEMP", 20.75},
	{"COOLANT_TMP", 21.5}, {"ENGINE_OIL_TEMP", 22.5}, {"OIL_LEVEL", 67.31}, {"LAMBDA", 1}, {"DPF_DIFF_PRESSURE", 0},
	{"RAIL_PRESSURE", 0}, {"BOOST_PRESSURE", 1008.83}, {"BOOST_PRESSURE_LP", 1008.83}, {"EXHAUST_BACK_PRESSURE", 1010},
	{"BARO_PRESSURE", 1008.83}, {"INTAKE_AIR_PRESSURE", 1009}, {"INTAKE_AIR_TMP", 20.75}, {"INJECTION_QUANTITY", 0},
	{"AIR_MASS_PER_STROKE", 0}, {"EGR_RATE", 0}, {"ACCEL_PEDAL", 0}, {"WASTEGATE", 0}, {"FUEL_L", 54},
	{"ECU_DISTANCE", 187432}, {"THROTTLE", 5.47}, {"EGR_VALVE", 0}, {"DPF_ASH", 12.52}, {"DPF_KM_SINCE_REGEN", 312},
	{"DPF_REGEN_STATUS", 1}, {"DPF_SOOT_MASS", 4.38}, {"DPF_SOOT_SIM", 6.21},
};
#define IGNITION_ON_COUNT ((int)(sizeof(ignition_on) / sizeof(ignition_on[0])))

static void test_init(void)
{
	// Whatever was in the memory before: a count, a counter that was seen, dropped values
	memset(&box, 0x01, sizeof(box));
	memset(box.before, GUARD, sizeof(box.before));
	memset(box.after, GUARD, sizeof(box.after));
	values_init(&box.values);

	check(box.values.count == 0, "init: no values");
	check(box.values.dropped == 0, "init: nothing dropped");
	check(!box.values.has_pass, "init: no pass counter seen");
	check(is_missing("ENGINE_RPM") && is_missing(""), "init: no value is found");
	check(guards_intact(), "init: writes nothing outside the values");
	check(VALUES_MAX == 64 && VALUE_NAME_SIZE == 33, "room for 64 values with names up to 32 bytes");
}

// What the reader left in the token room from the answer before is not part of the next answer
static void test_stale_tokens(void)
{
	static char error[] = "{\"error\":\"x\"}";
	static char two[] = "{\"A\":1,\"B\":2}";

	start();
	check(values_apply(&box.values, error, strlen(error), -1, 100, work, VALUES_TOKENS) == VALUES_INVALID, "an error object is read");
	// The same memory now holds {} followed by what is left of the error object
	error[1] = '}';
	check(values_apply(&box.values, error, 2, -1, 200, work, VALUES_TOKENS) == VALUES_RENEWED,
	      "{} is not taken for an error object because of the tokens of the answer before");

	check(values_apply(&box.values, two, strlen(two), -1, 300, work, VALUES_TOKENS) == VALUES_RENEWED && is_number("A", 1, 300) && is_number("B", 2, 300),
	      "an answer with two values is read");
	// The same memory now holds {"A":3} followed by "B":9}
	memcpy(two, "{\"A\":3}\"B\":9}", strlen(two));
	check(values_apply(&box.values, two, 7, -1, 400, work, VALUES_TOKENS) == VALUES_RENEWED && is_number("A", 3, 400),
	      "an answer with one value in the same memory is read");
	check(is_number("B", 2, 300) && box.values.count == 2, "the second value of the answer before is not read again from its old tokens");

	// An answer without any token leaves those of the answer before where they are, and the text they refer to as well
	remember();
	check(values_apply(&box.values, two, 0, -1, 500, work, VALUES_TOKENS) == VALUES_INVALID && unchanged(),
	      "length 0 in the same memory: invalid, the tokens of the answer before are not used");
	two[0] = ' ';
	check(values_apply(&box.values, two, 1, -1, 600, work, VALUES_TOKENS) == VALUES_INVALID && unchanged(),
	      "a space in the same memory: invalid, the tokens of the answer before are not used");
}

static void test_fixtures(void)
{
	static char profile[16384];
	char what[80];
	int count, pids, i, j;
	int names = 0, found = 0, same = 0;

	start();
	check(read_fixture("../../tools/w906/fixtures/autopid_data_ignition_on.json", text, sizeof(text)), "fixture ignition on");
	check(apply(text, 1234, 5000) == VALUES_RENEWED, "ignition on: the first answer is applied");
	check(box.values.count == 35 && IGNITION_ON_COUNT == 35 && box.values.dropped == 0, "ignition on: 35 values, none dropped");
	for(i = 0; i < IGNITION_ON_COUNT; i++)
	{
		snprintf(what, sizeof(what), "ignition on: %s is a number seen at the time of the answer", ignition_on[i].name);
		check(is_number(ignition_on[i].name, ignition_on[i].number, 5000), what);
	}
	check(box.values.has_pass && box.values.pass == 1234, "ignition on: the pass counter of the answer is remembered");
	check(guards_intact(), "ignition on: writes nothing outside the values");

	// The names the adapter delivers are the ones of the vehicle profile
	check(read_fixture("../../vehicle_profiles/mercedes/sprinter_w906_om651.json", profile, sizeof(profile)), "vehicle profile");
	count = json_parse(profile, strlen(profile), large_work, 1024);
	pids = count > 0 ? json_member(profile, large_work, 0, "pids") : -1;
	check(pids > 0 && large_work[pids].type == JSON_ARRAY, "vehicle profile: has a list of pids");
	if(pids > 0)
	{
		for(i = 0; i < large_work[pids].size; i++)
		{
			int parameters = json_member(profile, large_work, json_element(large_work, pids, i), "parameters");
			int key = parameters + 1;

			for(j = 0; parameters > 0 && j < large_work[parameters].size; j++)
			{
				char name[64];

				names++;
				if(json_text(profile, &large_work[key], name, sizeof(name)) && values_find(&box.values, name) != NULL) found++;
				key += 1 + large_work[key + 1].skip;
			}
		}
	}
	check(names == 35 && found == 35, "ignition on: every value of the vehicle profile is there");

	// Ignition off: the counter stands, later it moves again without any value
	check(read_fixture("../../tools/w906/fixtures/autopid_data_ignition_off.json", text, sizeof(text)), "fixture ignition off");
	remember();
	check(apply(text, 1234, 6000) == VALUES_REPEATED && unchanged(), "ignition off, counter stands: repeated, nothing changed");
	check(apply(text, 1235, 7000) == VALUES_RENEWED, "ignition off, counter moved: {} is a valid answer");
	for(i = 0; i < IGNITION_ON_COUNT; i++)
	{
		if(is_number(ignition_on[i].name, ignition_on[i].number, 5000)) same++;
	}
	check(same == 35 && box.values.count == 35, "ignition off: {} renews nothing, the values keep content and time");
	check(box.values.has_pass && box.values.pass == 1235, "ignition off: the counter of the empty answer is remembered");
	check(values_age(values_find(&box.values, "ENGINE_RPM"), 7999) == VALUE_AGE_FRESH &&
	      values_age(values_find(&box.values, "ENGINE_RPM"), 8000) == VALUE_AGE_OLD &&
	      values_age(values_find(&box.values, "ENGINE_RPM"), 15000) == VALUE_AGE_GONE,
	      "ignition off: the values grow old from the last answer that contained them");

	start();
	check(apply("{}", 7, 100) == VALUES_RENEWED && box.values.count == 0 && box.values.has_pass && box.values.pass == 7,
	      "{} as the first answer: applied, no values");

	// Binary sensors and the answer of a firmware that cannot build one
	start();
	check(read_fixture("fixtures/values_binary_sensors.json", text, sizeof(text)), "fixture binary sensors");
	check(apply(text, 1, 250) == VALUES_RENEWED && box.values.count == 5, "binary sensors: five values");
	check(is_number("ENGINE_RPM", 812.5, 250) && is_number("COOLANT_TMP", 88.25, 250) && is_number("DPF_REGEN_STATUS", 1, 250),
	      "binary sensors: the numbers");
	check(is_switch("GLOW_ACTIVE", VALUE_ON, 250), "binary sensors: \"on\"");
	check(is_switch("AC_COMPRESSOR", VALUE_OFF, 250), "binary sensors: \"off\"");
	check(read_fixture("fixtures/values_error.json", text, sizeof(text)), "fixture error answer");
	remember();
	check(apply(text, 2, 500) == VALUES_INVALID && unchanged(), "the error answer of the firmware is invalid and changes nothing");
}

static void test_kinds(void)
{
	static const struct
	{
		const char *name;
		const char *what;
	} ignored[] = {
		{"E", "ignored: \"ON\" in capitals"}, {"F", "ignored: another text"}, {"G", "ignored: true"}, {"H", "ignored: false"},
		{"I", "ignored: null"}, {"J", "ignored: an array"}, {"K", "ignored: an object"}, {"L", "ignored: an empty text"},
		{"M", "ignored: \"o\", the beginning of on"}, {"N", "ignored: \"onn\""}, {"O", "ignored: \"of\", the beginning of off"},
		{"P", "ignored: \"offf\""}, {"Q", "ignored: \"Off\""}, {"R", "ignored: \" on\" with a space"},
		{"T", "ignored: \"On\""}, {"U", "ignored: \"OFF\""}, {"V", "ignored: \"oN\""}, {"W", "ignored: \"on \" with a space behind"},
		{"X", "ignored: the text \"true\""}, {"Y", "ignored: the text \"false\""}, {"Z", "ignored: the text \"1\""},
		{"a0", "ignored: the text \"0\""}, {"a1", "ignored: a number in a text"},
	};
	size_t i;

	start();
	check(apply("{\"A\":1,\"B\":\"on\",\"C\":\"off\",\"D\":-12.5,\"E\":\"ON\",\"F\":\"abc\",\"G\":true,\"H\":false,\"I\":null,"
	            "\"J\":[1,{\"a\":2}],\"K\":{\"x\":1,\"y\":[2]},\"L\":\"\",\"M\":\"o\",\"N\":\"onn\",\"O\":\"of\",\"P\":\"offf\","
	            "\"Q\":\"Off\",\"R\":\" on\",\"T\":\"On\",\"U\":\"OFF\",\"V\":\"oN\",\"W\":\"on \",\"X\":\"true\",\"Y\":\"false\","
	            "\"Z\":\"1\",\"a0\":\"0\",\"a1\":\"12.5\",\"S\":0}", -1, 500) == VALUES_RENEWED, "an answer with members of every kind is applied");
	check(is_number("A", 1, 500), "a number becomes a value");
	check(is_number("D", -12.5, 500), "a negative number with a fraction becomes a value");
	check(is_number("S", 0, 500), "a member behind arrays and objects is found: zero becomes a value");
	check(is_switch("B", VALUE_ON, 500), "\"on\" becomes a value, its number is 0");
	check(is_switch("C", VALUE_OFF, 500), "\"off\" becomes a value, its number is 0");
	for(i = 0; i < sizeof(ignored) / sizeof(ignored[0]); i++) check(is_missing(ignored[i].name), ignored[i].what);
	check(is_missing("a") && is_missing("x") && is_missing("y"), "members of nested objects are no values");
	check(box.values.count == 5 && box.values.dropped == 0, "five values, ignored members are not counted as dropped");

	start();
	check(apply("{\"a\":1e3,\"b\":-0,\"c\":0.25,\"d\":187432,\"e\":-2147483648,\"f\":1.7976931348623157e308,"
	            "\"g\":12345678901234567890123456789012345678901234567,"
	            "\"h\":123456789012345678901234567890123456789012345678,\"i\":1e999,\"j\":2.5E-1,\"k\":-1e999,\"l\":1e-305}", -1, 9) == VALUES_RENEWED,
	      "numbers in every notation are applied");
	check(is_number("a", 1000, 9), "number with an exponent");
	check(is_number("b", 0, 9), "minus zero");
	check(is_number("c", 0.25, 9) && is_number("j", 0.25, 9), "fractions");
	check(is_number("d", 187432, 9) && is_number("e", -2147483648.0, 9), "large integers");
	check(is_number("f", DBL_MAX, 9), "the largest number");
	check(is_number("l", 1e-305, 9), "a very small number keeps its value");
	check(values_find(&box.values, "g") != NULL && values_find(&box.values, "g")->number > 1e46, "a number of 47 characters is a value");
	check(is_missing("h"), "a number of 48 characters, which json_number() refuses, is ignored");
	check(values_find(&box.values, "i") != NULL && values_find(&box.values, "i")->kind == VALUE_NUMBER &&
	      values_find(&box.values, "i")->seen_ms == 9, "a number beyond the range of a double is still a value");
	check(values_find(&box.values, "i") != NULL && values_find(&box.values, "i")->number > DBL_MAX,
	      "a number beyond the range of a double is infinite, not a number that could be shown");
	check(values_find(&box.values, "k") != NULL && values_find(&box.values, "k")->kind == VALUE_NUMBER &&
	      values_find(&box.values, "k")->number < -DBL_MAX, "a negative number beyond the range is minus infinite");
	check(box.values.count == 11 && box.values.dropped == 0, "eleven numbers, the refused one is not counted as dropped");

	// One value changes its kind
	start();
	check(apply("{\"A\":5}", -1, 100) == VALUES_RENEWED && is_number("A", 5, 100), "a value starts as a number");
	check(apply("{\"A\":\"on\"}", -1, 200) == VALUES_RENEWED && is_switch("A", VALUE_ON, 200), "the number becomes on: kind on, number 0");
	check(apply("{\"A\":\"off\"}", -1, 300) == VALUES_RENEWED && is_switch("A", VALUE_OFF, 300), "on becomes off");
	check(apply("{\"A\":7.5}", -1, 400) == VALUES_RENEWED && is_number("A", 7.5, 400), "off becomes a number again");
	check(box.values.count == 1, "a value that changes its kind stays one value");

	start();
	check(apply("{\"A\":\"o\\u006e\",\"B\":\"\\u006fff\"}", -1, 1) == VALUES_RENEWED && is_switch("A", VALUE_ON, 1) && is_switch("B", VALUE_OFF, 1),
	      "on and off are recognised with their escapes resolved");

	start();
	check(apply("{\"A\":1,\"A\":2}", -1, 1) == VALUES_RENEWED && is_number("A", 2, 1) && box.values.count == 1,
	      "of two members with the same name the last counts, there is one value");
	check(apply("{\"A\":3,\"A\":\"off\"}", -1, 2) == VALUES_RENEWED && is_switch("A", VALUE_OFF, 2) && box.values.count == 1,
	      "same name, number then off: off counts");
	check(apply("{\"A\":\"on\",\"A\":4}", -1, 3) == VALUES_RENEWED && is_number("A", 4, 3) && box.values.count == 1,
	      "same name, on then number: the number counts");
	check(apply("{\"A\":5,\"A\":\"x\"}", -1, 4) == VALUES_RENEWED && is_number("A", 5, 4) && box.values.count == 1,
	      "same name, number then an ignored text: the number counts");
}

static void test_names(void)
{
	static const char name32[] = "N2345678901234567890123456789012";
	static const char name33[] = "M23456789012345678901234567890123";
	static const char umlauts32[] = "ääääääääääääääää";
	const value_t *value;
	int i;

	check(strlen(name32) == 32 && strlen(name33) == 33 && strlen(umlauts32) == 32, "the test names have 32, 33 and 32 bytes");

	start();
	snprintf(text, sizeof(text), "{\"%s\":1}", name32);
	check(apply(text, -1, 10) == VALUES_RENEWED && is_number(name32, 1, 10) && box.values.count == 1 && box.values.dropped == 0,
	      "a name of 32 bytes is kept completely");
	snprintf(text, sizeof(text), "{\"%s\":2,\"B\":3}", name33);
	check(apply(text, -1, 20) == VALUES_RENEWED && is_missing(name33) && box.values.count == 2 && is_number("B", 3, 20),
	      "a name of 33 bytes is no value, the member behind it is");
	check(box.values.dropped == 1, "a name of 33 bytes is counted as dropped");
	check(is_missing("M2345678901234567890123456789012"), "a name of 33 bytes is not cut to 32");
	check(is_missing("N2345678901234567890123456789012x") && is_missing("N234567890123456789012345678901"),
	      "find: a name that goes on behind the 32 bytes of a stored one, or ends one byte before, is another name");
	check(apply(text, -1, 30) == VALUES_RENEWED && box.values.dropped == 2 && box.values.count == 2, "it is counted with every answer");
	snprintf(text, sizeof(text), "{\"%s\":\"abc\",\"%s\":null}", name33, name33);
	check(apply(text, -1, 40) == VALUES_RENEWED && box.values.dropped == 2, "a member that is no value is not counted, whatever its name");
	snprintf(text, sizeof(text), "{\"%s\":\"on\"}", name33);
	check(apply(text, -1, 50) == VALUES_RENEWED && box.values.dropped == 3 && box.values.count == 2, "a switch with a name of 33 bytes is dropped as well");
	check(guards_intact(), "names at the limit: nothing written outside the values");

	// One answer a second fills 16 bit within a day
	box.values.dropped = 65535;
	check(apply(text, -1, 60) == VALUES_RENEWED && box.values.dropped == 65536, "names that are too long are counted beyond 16 bit");
	box.values.dropped = 4294967294u;
	check(apply(text, -1, 70) == VALUES_RENEWED && box.values.dropped == 4294967295u, "and up to the end of 32 bit");

	start();
	snprintf(text, sizeof(text), "{\"%s\":1,\"N2345678901234567890123456789013\":2}", name32);
	check(apply(text, -1, 5) == VALUES_RENEWED && box.values.count == 2 && is_number(name32, 1, 5) &&
	      is_number("N2345678901234567890123456789013", 2, 5), "names of 32 bytes that differ in the last byte are different values");

	start();
	snprintf(text, sizeof(text), "{\"%s\":1}", umlauts32);
	check(apply(text, -1, 1) == VALUES_RENEWED && is_number(umlauts32, 1, 1), "32 bytes of UTF-8 are a name");
	snprintf(text, sizeof(text), "{\"%sa\":1}", umlauts32);
	check(apply(text, -1, 2) == VALUES_RENEWED && box.values.count == 1 && box.values.dropped == 1, "33 bytes of UTF-8 are dropped");
	snprintf(text, sizeof(text), "{\"a%s\":1}", umlauts32);
	check(apply(text, -1, 3) == VALUES_RENEWED && box.values.count == 1 && box.values.dropped == 2 && is_missing("aäääääääääääääää"),
	      "33 bytes ending in a character of two bytes: dropped, not cut in the middle");

	// The length counts with the escapes resolved
	start();
	strcpy(text, "{\"");
	for(i = 0; i < 32; i++) strcat(text, "\\u0041");
	strcat(text, "\":5}");
	check(apply(text, -1, 1) == VALUES_RENEWED && is_number("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 5, 1), "32 escaped characters are a name of 32 bytes");
	strcpy(text, "{\"");
	for(i = 0; i < 33; i++) strcat(text, "\\u0041");
	strcat(text, "\":5}");
	check(apply(text, -1, 2) == VALUES_RENEWED && box.values.count == 1 && box.values.dropped == 1, "33 escaped characters are too long");
	check(apply("{\"\\u00d6l\":1,\"T\\\"\\\\\":2}", -1, 3) == VALUES_RENEWED && is_number("Öl", 1, 3) && is_number("T\"\\", 2, 3),
	      "a name is stored with its escapes resolved");

	start();
	check(apply("{\"A\\u0000B\":1,\"\\uD83D\":2,\"C\":3}", -1, 1) == VALUES_RENEWED && box.values.count == 1 && is_number("C", 3, 1) &&
	      is_missing("A") && is_missing(""), "a name json_text() refuses is no value");
	check(box.values.dropped == 2, "names json_text() refuses are counted as dropped");

	start();
	check(apply("{\"\":5}", -1, 1) == VALUES_RENEWED && is_number("", 5, 1) && box.values.count == 1, "the empty name is a name");

	start();
	check(apply("{\"\xff\xfe\x80\":7,\"\xc3\":8}", -1, 1) == VALUES_RENEWED && is_number("\xff\xfe\x80", 7, 1) && is_number("\xc3", 8, 1),
	      "bytes that are no UTF-8 are passed on as they are");

	start();
	check(apply("{\"AB\":1,\"A\":2,\"ABC\":3,\"b\":4}", -1, 1) == VALUES_RENEWED && box.values.count == 4, "names that begin alike are different values");
	check(is_number("AB", 1, 1) && is_number("A", 2, 1) && is_number("ABC", 3, 1), "a name is compared completely, not by its beginning");
	check(is_missing("ABCD") && is_missing("") && is_missing("a") && is_missing("B") && is_missing("BC"), "find: unknown names, capitals matter");
	value = values_find(&box.values, "b");
	check(value != NULL && value->number == 4 && value >= box.values.items && value < box.values.items + VALUES_MAX,
	      "find returns the value inside the list");
}

static void test_pass(void)
{
	start();
	check(apply("{\"A\":1}", 7, 100) == VALUES_RENEWED && is_number("A", 1, 100), "first answer: applied");
	check(box.values.has_pass && box.values.pass == 7, "first answer: its counter is remembered");
	remember();
	check(apply("{\"A\":2,\"B\":3}", 7, 200) == VALUES_REPEATED, "counter stood still: repeated");
	check(unchanged(), "counter stood still: nothing changed, neither values nor times nor counters");
	check(apply("{\"A\":2,\"B\":3}", 8, 300) == VALUES_RENEWED && is_number("A", 2, 300) && is_number("B", 3, 300), "counter moved: applied");
	check(box.values.pass == 8, "counter moved: the new counter is remembered");
	check(apply("{\"A\":4}", 8, 400) == VALUES_REPEATED && is_number("A", 2, 300), "the new counter stands still: repeated");
	check(apply("{\"A\":5}", 3, 500) == VALUES_RENEWED && is_number("A", 5, 500) && box.values.pass == 3,
	      "counter went backwards (the adapter restarted): it counts as moved");
	check(apply("{\"A\":6}", 3, 600) == VALUES_REPEATED && is_number("A", 5, 500), "the smaller counter stands still: repeated");
	check(apply("{\"A\":6}", 4, 600) == VALUES_RENEWED && is_number("A", 6, 600), "one more than before: applied");

	start();
	check(apply("{\"A\":1}", 0, 100) == VALUES_RENEWED && is_number("A", 1, 100) && box.values.has_pass && box.values.pass == 0,
	      "first answer with the counter 0: applied, 0 is a counter");
	check(apply("{\"A\":2}", 0, 200) == VALUES_REPEATED && is_number("A", 1, 100), "the counter 0 stood still: repeated");
	check(apply("{\"A\":3}", 1, 300) == VALUES_RENEWED && is_number("A", 3, 300), "from 0 to 1: applied");
	check(apply("{\"A\":4}", 0, 400) == VALUES_RENEWED && is_number("A", 4, 400), "back to 0: applied");

	start();
	check(apply("{\"A\":1}", -1, 100) == VALUES_RENEWED && is_number("A", 1, 100), "no counter at all: applied");
	check(!box.values.has_pass, "no counter at all: none is remembered");
	check(apply("{\"A\":2}", -1, 200) == VALUES_RENEWED && is_number("A", 2, 200), "no counter at all: every answer renews");
	check(apply("{\"A\":3}", -1, 300) == VALUES_RENEWED && is_number("A", 3, 300) && !box.values.has_pass, "no counter, third answer: renews");
	check(apply("{\"A\":4}", -2, 400) == VALUES_RENEWED && apply("{\"A\":5}", -2, 500) == VALUES_RENEWED && is_number("A", 5, 500) &&
	      !box.values.has_pass, "any negative number means no counter");
	check(apply("{\"A\":6}", INT64_MIN, 600) == VALUES_RENEWED && apply("{\"A\":7}", INT64_MIN, 700) == VALUES_RENEWED &&
	      is_number("A", 7, 700) && !box.values.has_pass, "the smallest negative number means no counter");

	// An answer without a counter between answers with one
	start();
	check(apply("{\"A\":1}", 5, 100) == VALUES_RENEWED, "counter 5: applied");
	check(apply("{\"A\":2}", -1, 200) == VALUES_RENEWED && is_number("A", 2, 200), "then an answer without a counter: renews");
	check(box.values.has_pass && box.values.pass == 5, "the answer without a counter leaves the remembered counter alone");
	check(apply("{\"A\":3}", 5, 300) == VALUES_REPEATED && is_number("A", 2, 200), "counter 5 again: it did not move, repeated");
	check(apply("{\"A\":3}", 6, 300) == VALUES_RENEWED && is_number("A", 3, 300), "counter 6: applied");

	// Counters at the limits
	start();
	check(apply("{\"A\":1}", 2147483647, 100) == VALUES_RENEWED && box.values.pass == 2147483647u, "counter 2^31-1: applied");
	check(apply("{\"A\":2}", 2147483647, 200) == VALUES_REPEATED, "counter 2^31-1 stood still: repeated");
	check(apply("{\"A\":2}", 2147483648, 200) == VALUES_RENEWED && box.values.pass == 2147483648u, "counter 2^31: applied");
	check(apply("{\"A\":3}", 4294967295, 300) == VALUES_RENEWED && box.values.pass == 4294967295u, "counter 2^32-1: applied");
	check(apply("{\"A\":4}", 4294967295, 400) == VALUES_REPEATED && is_number("A", 3, 300), "counter 2^32-1 stood still: repeated");
	check(apply("{\"A\":5}", -1, 500) == VALUES_RENEWED && is_number("A", 5, 500), "no counter after the counter 2^32-1: renews, -1 is not that counter");
	check(apply("{\"A\":6}", 0, 600) == VALUES_RENEWED && is_number("A", 6, 600), "counter 0 after 2^32-1: applied");
	check(apply("{\"A\":7}", 4294967294, 700) == VALUES_RENEWED && apply("{\"A\":8}", -2, 800) == VALUES_RENEWED && is_number("A", 8, 800),
	      "-2 after the counter 2^32-2: renews, a negative number is no counter");

	// Counters that differ only in their upper bits are different counters
	start();
	check(apply("{\"A\":1}", 5, 100) == VALUES_RENEWED, "counter 5 before larger ones: applied");
	check(apply("{\"A\":2}", 5 + 256, 200) == VALUES_RENEWED && is_number("A", 2, 200), "counter 5 + 2^8: it moved");
	check(apply("{\"A\":3}", 5 + 256 + 65536, 300) == VALUES_RENEWED && is_number("A", 3, 300), "2^16 more: it moved");
	check(apply("{\"A\":4}", 5 + 256 + 65536 + 16777216, 400) == VALUES_RENEWED && is_number("A", 4, 400), "2^24 more: it moved");
	check(apply("{\"A\":5}", 5 + 256 + 65536 + 16777216 + 2147483648, 500) == VALUES_RENEWED && is_number("A", 5, 500), "2^31 more: it moved");
	check(apply("{\"A\":6}", 5 + 256 + 65536 + 16777216 + 2147483648, 600) == VALUES_REPEATED && is_number("A", 5, 500), "the same large counter: repeated");

	// Invalid answers do not touch the counter, and the counter does not hide an invalid answer
	start();
	check(apply("{\"A\":1}", 7, 100) == VALUES_RENEWED, "counter 7: applied");
	remember();
	check(apply("{\"A\":2", 8, 200) == VALUES_INVALID && unchanged(), "broken answer with a counter that moved: invalid, nothing changed");
	check(apply("{\"A\":3}", 8, 300) == VALUES_RENEWED && is_number("A", 3, 300), "the counter of the broken answer was not remembered");
	remember();
	check(apply("{\"A\":4", 8, 400) == VALUES_INVALID && unchanged(), "broken answer while the counter stands: invalid, not repeated");
	check(apply("[1]", 8, 400) == VALUES_INVALID && apply("{\"error\":\"x\"}", 8, 400) == VALUES_INVALID && unchanged(),
	      "no object and the error object while the counter stands: invalid, not repeated");
	check(apply("{}", 9, 500) == VALUES_RENEWED && box.values.pass == 9 && is_number("A", 3, 300), "{} with a counter that moved: applied, the counter is remembered");
	check(apply("{\"A\":5}", 9, 600) == VALUES_REPEATED && is_number("A", 3, 300), "an answer with the counter of the {}: repeated");

	// The counter counts although there is no value yet
	start();
	check(apply("{}", 7, 100) == VALUES_RENEWED && box.values.count == 0, "{} as the first answer with a counter");
	check(apply("{\"A\":1}", 7, 200) == VALUES_REPEATED && box.values.count == 0, "values with the counter of that {}: repeated although there is no value yet");
	check(apply("{\"A\":1}", 8, 300) == VALUES_RENEWED && is_number("A", 1, 300), "the counter moved: the values are applied");
}

static void test_invalid(void)
{
	static const struct
	{
		const char *json;
		const char *what;
	} invalid[] = {
		// A scalar first: it has no members, so even a reader that goes on does it in an orderly way
		{"5", "invalid: a number"},
		{"{\"A\":1", "invalid: an object that is not closed"},
		{"{\"error\":\"No data available\"}", "invalid: the error object of the firmware"},
		{"{\"error\":5}", "invalid: an error object with a number"},
		{"{\"error\":{\"code\":1}}", "invalid: an error object with an object"},
		{"{\"error\":\"on\"}", "invalid: an error object with the text on, which would be a value under another name"},
		{"{\"error\":\"off\"}", "invalid: an error object with the text off"},
		{"{\"error\":\"\"}", "invalid: an error object with an empty text"},
		{"{\"error\":null}", "invalid: an error object with null"},
		{"{\"error\":true}", "invalid: an error object with true"},
		{"{\"error\":false}", "invalid: an error object with false"},
		{"{\"error\":[1]}", "invalid: an error object with an array"},
		{"{ \"error\" : 0 }", "invalid: an error object with whitespace and the number 0"},
		{"{\"\\u0065rror\":\"x\"}", "invalid: an error object with an escaped key"},
		{"[]", "invalid: an empty array"},
		{"[1,2]", "invalid: an array of numbers"},
		{"[{\"A\":1}]", "invalid: an object inside an array"},
		{"\"on\"", "invalid: a text"},
		{"null", "invalid: null"},
		{"true", "invalid: true"},
		{"{\"A\":1,}", "invalid: a comma before the end"},
		{"{\"A\":}", "invalid: a member without value"},
		{"{\"A\":1}x", "invalid: text behind the object"},
		{"{\"A\":01}", "invalid: a number with a leading zero"},
		{"{\"A\":1,\"B\":[[[[[[[[1]]]]]]]]}", "invalid: nested deeper than the reader follows"},
		{"{", "invalid: only a brace"},
		{"", "invalid: an empty text"},
		{" ", "invalid: a space"},
		{"\n\t \r", "invalid: nothing but whitespace"},
	};
	size_t i;
	bool all = true;

	start();
	check(apply("{\"K\":1}", 1, 100) == VALUES_RENEWED, "a valid answer before the invalid ones");
	remember();
	for(i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
	{
		// The counter moves with every answer: it must not be remembered
		check(apply(invalid[i].json, 2 + (int64_t)i, 200) == VALUES_INVALID && unchanged(), invalid[i].what);
	}
	check(apply("{\"K\":2}", 2, 300) == VALUES_RENEWED && is_number("K", 2, 300) && box.values.count == 1, "after the invalid answers a valid one is applied");

	// As the first answers ever: there is no value and no counter yet that could hide a change
	start();
	remember();
	for(i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
	{
		if(apply(invalid[i].json, (int64_t)i, 200) != VALUES_INVALID || !unchanged()) all = false;
	}
	check(all, "every invalid answer before the first valid one: invalid, nothing changed");
	check(apply("{\"K\":1}", 3, 300) == VALUES_RENEWED && is_number("K", 1, 300),
	      "the first valid answer is applied although an invalid one came with its counter");

	// An object with "error" and something else, and names that only look like it, are ordinary answers
	start();
	check(apply("{\"error\":1,\"A\":2}", -1, 1) == VALUES_RENEWED && is_number("error", 1, 1) && is_number("A", 2, 1),
	      "error as the first of two members: a value named error");
	start();
	check(apply("{\"A\":2,\"error\":\"on\"}", -1, 1) == VALUES_RENEWED && is_switch("error", VALUE_ON, 1) && is_number("A", 2, 1),
	      "error as the second of two members: a value named error");
	start();
	check(apply("{\"error\":\"x\",\"A\":2}", -1, 1) == VALUES_RENEWED && is_number("A", 2, 1) && box.values.count == 1,
	      "an error text next to a value: the value is applied");
	start();
	check(apply("{\"erro\":1}", -1, 1) == VALUES_RENEWED && is_number("erro", 1, 1), "a single member named erro is a value");
	check(apply("{\"errors\":2}", -1, 2) == VALUES_RENEWED && is_number("errors", 2, 2), "a single member named errors is a value");
	check(apply("{\"Error\":3}", -1, 3) == VALUES_RENEWED && is_number("Error", 3, 3), "a single member named Error is a value");
	check(apply("{\"A\":4}", -1, 4) == VALUES_RENEWED && is_number("A", 4, 4), "a single member with another name is a value");

	// Only `length` bytes belong to the answer
	start();
	check(values_apply(&box.values, "{\"A\":1}x", 7, -1, 1, work, VALUES_TOKENS) == VALUES_RENEWED && is_number("A", 1, 1),
	      "bytes behind the given length are not read");
	remember();
	check(values_apply(&box.values, "{\"A\":2}", 6, -1, 2, work, VALUES_TOKENS) == VALUES_INVALID && unchanged(),
	      "a length that ends before the brace: invalid");
	check(values_apply(&box.values, "", 0, -1, 3, work, VALUES_TOKENS) == VALUES_INVALID && unchanged(), "length 0: invalid");
}

static void test_limits(void)
{
	bool all;
	int i;

	// Before anything is read with them: a full answer has 1 + 2 * 64 tokens
	check(VALUES_TOKENS >= 2 * VALUES_MAX + 1, "VALUES_TOKENS is enough for a full answer");

	// As many values as there is room for
	start();
	many(VALUES_MAX, 100);
	check(apply(text, 1, 1000) == VALUES_RENEWED, "64 values: applied with VALUES_TOKENS tokens");
	check(guards_intact(), "64 values: nothing written outside the values");
	check(box.values.count == VALUES_MAX && box.values.dropped == 0, "64 values: all kept, none dropped");
	all = true;
	for(i = 0; i < VALUES_MAX; i++)
	{
		char name[8];

		snprintf(name, sizeof(name), "V%02d", i);
		if(!is_number(name, i + 100, 1000)) all = false;
	}
	check(all, "64 values: each found with its number");

	// One more
	start();
	many(VALUES_MAX + 1, 100);
	check(apply(text, 1, 1000) == VALUES_RENEWED, "65 values: applied");
	check(guards_intact(), "65 values: the one too many is not written behind the list");
	check(box.values.count == VALUES_MAX, "65 values: 64 are kept");
	check(box.values.dropped == 1, "65 values: one is counted as dropped");
	check(is_number("V00", 100, 1000) && is_number("V63", 163, 1000) && is_missing("V64"), "65 values: the first 64 are kept, the last is not");
	many(VALUES_MAX + 1, 200);
	check(apply(text, 2, 2000) == VALUES_RENEWED && guards_intact(), "65 values again: applied");
	check(box.values.count == VALUES_MAX && box.values.dropped == 2, "65 values again: the one too many is counted again");
	check(is_number("V00", 200, 2000) && is_number("V63", 263, 2000) && is_missing("V64"), "a full list still renews the values it knows");
	check(apply("{\"NEW\":1,\"V10\":5}", 3, 3000) == VALUES_RENEWED && is_missing("NEW") && is_number("V10", 5, 3000) &&
	      box.values.dropped == 3 && guards_intact(), "a full list: a new name is dropped, the known one behind it is renewed");

	// 63 values leave room for exactly one more
	start();
	many(VALUES_MAX - 1, 0);
	check(apply(text, -1, 1) == VALUES_RENEWED && box.values.count == VALUES_MAX - 1, "63 values: applied");
	check(apply("{\"X\":1,\"Y\":2}", -1, 2) == VALUES_RENEWED && is_number("X", 1, 2) && is_missing("Y") && box.values.count == VALUES_MAX &&
	      box.values.dropped == 1 && guards_intact(), "63 values and two new ones: the first finds room, the second is dropped");
	box.values.dropped = 65535;
	check(apply("{\"Y\":2}", -1, 3) == VALUES_RENEWED && box.values.dropped == 65536, "values without room are counted beyond 16 bit");

	// Far more members than places, read with a room for tokens that is large enough: every member is looked at
	start();
	check(apply("{\"V299\":7,\"KEEP\":8}", 1, 500) == VALUES_RENEWED && box.values.count == 2, "two values before a very long answer");
	many(300, 0);
	check(values_apply(&box.values, text, strlen(text), 2, 1000, large_work, 1024) == VALUES_RENEWED && guards_intact(),
	      "300 members with 1024 tokens: applied");
	check(box.values.count == VALUES_MAX && is_number("V00", 0, 1000) && is_number("V61", 61, 1000) && is_missing("V62") && is_missing("V298"),
	      "300 members: the first 62 fill the places that were left");
	check(is_number("V299", 299, 1000), "300 members: the last one, which was known, is renewed");
	check(is_number("KEEP", 8, 500), "300 members: a value they do not contain keeps content and time");
	check(box.values.dropped == 237, "300 members: the 237 without room are counted");
	remember();
	check(apply(text, 3, 2000) == VALUES_INVALID && unchanged(), "300 members with VALUES_TOKENS tokens: invalid, nothing changed");

	// VALUES_TOKENS is 192: 95 members need 191 of them, 96 need 193
	start();
	many(95, 0);
	check(apply(text, 1, 10) == VALUES_RENEWED && box.values.count == VALUES_MAX && box.values.dropped == 31,
	      "95 members are the most that VALUES_TOKENS tokens can hold: applied");
	many(96, 0);
	remember();
	check(apply(text, 2, 20) == VALUES_INVALID && unchanged(), "96 members need more than VALUES_TOKENS tokens: invalid");

	// Room for the tokens: a full answer has 1 + 2 * 64 of them
	start();
	many(VALUES_MAX, 0);
	check(values_apply(&box.values, text, strlen(text), 1, 10, work, 2 * VALUES_MAX + 1) == VALUES_RENEWED && box.values.count == VALUES_MAX,
	      "exactly as many tokens as the answer needs: applied");
	start();
	memset(&work[2 * VALUES_MAX], 0x5A, 2 * sizeof(work[0]));
	remember();
	check(values_apply(&box.values, text, strlen(text), 1, 10, work, 2 * VALUES_MAX) == VALUES_INVALID && unchanged(),
	      "one token less than the answer needs: invalid, nothing changed");
	check(work[2 * VALUES_MAX].start == 0x5A5A5A5A && work[2 * VALUES_MAX + 1].start == 0x5A5A5A5A,
	      "one token less than the answer needs: nothing written behind the room of the reader");
	start();
	remember();
	memset(&work[0], 0x5A, 2 * sizeof(work[0]));
	check(values_apply(&box.values, "{}", 2, 1, 10, work, 0) == VALUES_INVALID && unchanged() && work[0].start == 0x5A5A5A5A,
	      "no room for tokens at all: invalid, nothing written");
	remember();
	check(values_apply(&box.values, "{}", 2, 1, 10, work, -1) == VALUES_INVALID && unchanged() && work[0].start == 0x5A5A5A5A,
	      "a negative room for tokens: invalid, nothing written");
}

static void test_age(void)
{
	static const struct
	{
		uint64_t seen_ms;
		uint64_t now_ms;
		value_age_t age;
		const char *what;
	} ages[] = {
		{5000, 5000, VALUE_AGE_FRESH, "age 0 ms: fresh"},
		{5000, 5001, VALUE_AGE_FRESH, "age 1 ms: fresh"},
		{5000, 7999, VALUE_AGE_FRESH, "age 2999 ms: fresh"},
		{5000, 8000, VALUE_AGE_OLD, "age 3000 ms: old"},
		{5000, 8001, VALUE_AGE_OLD, "age 3001 ms: old"},
		{5000, 14999, VALUE_AGE_OLD, "age 9999 ms: old"},
		{5000, 15000, VALUE_AGE_GONE, "age 10000 ms: gone"},
		{5000, 15001, VALUE_AGE_GONE, "age 10001 ms: gone"},
		{5000, 4999, VALUE_AGE_FRESH, "the clock stepped back by 1 ms: no time passed, fresh"},
		{5000, 0, VALUE_AGE_FRESH, "the clock stepped back to 0: fresh"},
		{UINT64_MAX, 0, VALUE_AGE_FRESH, "the clock stepped back by the largest possible amount: fresh"},
		{0, 0, VALUE_AGE_FRESH, "seen at 0, now 0: fresh"},
		{0, 2999, VALUE_AGE_FRESH, "seen at 0, now 2999: fresh"},
		{0, 3000, VALUE_AGE_OLD, "seen at 0, now 3000: old"},
		{0, 9999, VALUE_AGE_OLD, "seen at 0, now 9999: old"},
		{0, 10000, VALUE_AGE_GONE, "seen at 0, now 10000: gone"},
		{0, UINT64_MAX, VALUE_AGE_GONE, "seen at 0, now the largest time: gone"},
		{UINT64_MAX - 2999, UINT64_MAX, VALUE_AGE_FRESH, "at the end of time, age 2999: fresh"},
		{UINT64_MAX - 3000, UINT64_MAX, VALUE_AGE_OLD, "at the end of time, age 3000: old"},
		{UINT64_MAX - 10000, UINT64_MAX, VALUE_AGE_GONE, "at the end of time, age 10000: gone"},
		{(uint64_t)1 << 32, ((uint64_t)1 << 32) + 2999, VALUE_AGE_FRESH, "beyond 32 bit, age 2999: fresh"},
		{((uint64_t)1 << 32) - 1, ((uint64_t)1 << 32) + 2999, VALUE_AGE_OLD, "across 32 bit, age 3000: old"},
		{(uint64_t)1 << 32, 4999, VALUE_AGE_FRESH, "a time whose low 32 bit are behind, but which is before: fresh"},
	};
	static const uint64_t bases[] = {0, 1, 2999, 3000, 10000, 123456789, 0xFFFFFFFFu, (uint64_t)1 << 40};
	value_t value;
	size_t i, base;
	uint64_t age;
	bool all;

	check(VALUE_FRESH_MS == 3000 && VALUE_KEPT_MS == 10000, "fresh for 3 s, kept for 10 s");

	memset(&value, 0, sizeof(value));
	value.kind = VALUE_NUMBER;
	for(i = 0; i < sizeof(ages) / sizeof(ages[0]); i++)
	{
		value.seen_ms = ages[i].seen_ms;
		check(values_age(&value, ages[i].now_ms) == ages[i].age, ages[i].what);
	}
	check(values_age(NULL, 0) == VALUE_AGE_GONE && values_age(NULL, 5000) == VALUE_AGE_GONE && values_age(NULL, UINT64_MAX) == VALUE_AGE_GONE,
	      "a value that was never seen is gone at any time");

	// Nothing but the two times counts
	all = true;
	for(i = 0; i < sizeof(ages) / sizeof(ages[0]); i++)
	{
		static const value_t kinds[] = {
			{"", VALUE_NUMBER, DBL_MAX, 0}, {"A", VALUE_NUMBER, -DBL_MAX, 0}, {"B", VALUE_ON, 0, 0}, {"C", VALUE_OFF, 0, 0},
			{"A_LONGER_NAME", VALUE_NUMBER, 1e-300, 0}, {"@BATT_V", VALUE_NUMBER, 12.5, 0},
		};
		size_t kind;

		for(kind = 0; kind < sizeof(kinds) / sizeof(kinds[0]); kind++)
		{
			value = kinds[kind];
			value.seen_ms = ages[i].seen_ms;
			if(values_age(&value, ages[i].now_ms) != ages[i].age) all = false;
		}
	}
	check(all, "the age is the same for numbers of any size, on, off and any name");
	memset(&value, 0, sizeof(value));
	value.kind = VALUE_NUMBER;

	// Every age up to 12 s, from several starting times, against the rule written down once more
	all = true;
	for(base = 0; base < sizeof(bases) / sizeof(bases[0]); base++)
	{
		value.seen_ms = bases[base];
		for(age = 0; age <= 12000; age++)
		{
			value_age_t expected = age < 3000 ? VALUE_AGE_FRESH : age < 10000 ? VALUE_AGE_OLD : VALUE_AGE_GONE;

			if(values_age(&value, bases[base] + age) != expected) all = false;
			// Before the value was seen
			if(age <= bases[base] && values_age(&value, bases[base] - age) != VALUE_AGE_FRESH) all = false;
		}
	}
	check(all, "every age from 0 to 12000 ms and every step back, from eight starting times");

	// Ages up to the largest: 2^11 is below 3000, 2^12 and 2^13 are below 10000
	all = true;
	for(i = 0; i < 64; i++)
	{
		value_age_t expected = i < 12 ? VALUE_AGE_FRESH : i < 14 ? VALUE_AGE_OLD : VALUE_AGE_GONE;

		value.seen_ms = 0;
		if(values_age(&value, (uint64_t)1 << i) != expected) all = false;
		value.seen_ms = 12345;
		if(values_age(&value, ((uint64_t)1 << i) + 12345) != expected) all = false;
	}
	check(all, "an age of 2^n ms for every n up to 63: no part of the difference of the times gets lost");

	// The age of an applied value
	start();
	check(apply("{\"A\":1,\"B\":2}", 1, 1000) == VALUES_RENEWED, "two values seen at 1000");
	check(values_age(values_find(&box.values, "A"), 3999) == VALUE_AGE_FRESH && values_age(values_find(&box.values, "A"), 4000) == VALUE_AGE_OLD,
	      "an applied value is fresh until 2999 ms after its answer");
	check(apply("{\"A\":1,\"B\":2}", 1, 3500) == VALUES_REPEATED && values_age(values_find(&box.values, "A"), 4000) == VALUE_AGE_OLD,
	      "a repeated answer does not make a value younger");
	check(apply("{\"A\":1}", 2, 3500) == VALUES_RENEWED && values_age(values_find(&box.values, "A"), 4000) == VALUE_AGE_FRESH &&
	      values_age(values_find(&box.values, "A"), 6500) == VALUE_AGE_OLD, "a renewing answer does, for the values it contains");
	check(values_age(values_find(&box.values, "B"), 4000) == VALUE_AGE_OLD && values_age(values_find(&box.values, "B"), 10999) == VALUE_AGE_OLD &&
	      values_age(values_find(&box.values, "B"), 11000) == VALUE_AGE_GONE, "the value it does not contain grows old and is gone after 10 s");
	check(values_age(values_find(&box.values, "C"), 1000) == VALUE_AGE_GONE, "a name that never came is gone");
}

static void test_disappear(void)
{
	static const char name33[] = "M23456789012345678901234567890123";

	// {} from a firmware without counter changes nothing at all
	start();
	snprintf(text, sizeof(text), "{\"A\":1,\"%s\":2}", name33);
	check(apply(text, -1, 1000) == VALUES_RENEWED && box.values.count == 1 && box.values.dropped == 1, "a value and a dropped one");
	remember();
	check(apply("{}", -1, 2000) == VALUES_RENEWED && unchanged(), "{} without a counter: applied, and nothing at all changed, dropped included");

	start();
	check(apply("{\"A\":1,\"B\":2,\"C\":\"on\"}", 1, 1000) == VALUES_RENEWED && box.values.count == 3, "three values");
	check(apply("{\"B\":3}", 2, 2000) == VALUES_RENEWED, "an answer without two of them is applied");
	check(is_number("A", 1, 1000) && is_switch("C", VALUE_ON, 1000), "values not in the answer keep their content and their time");
	check(is_number("B", 3, 2000) && box.values.count == 3, "the value in the answer is renewed, no value is forgotten");
	check(apply("{}", 3, 3000) == VALUES_RENEWED && is_number("A", 1, 1000) && is_number("B", 3, 2000) && is_switch("C", VALUE_ON, 1000) &&
	      box.values.count == 3, "{} renews nothing: every value keeps content and time");
	check(apply("{\"C\":\"off\",\"A\":4}", 4, 12000) == VALUES_RENEWED && is_number("A", 4, 12000) && is_switch("C", VALUE_OFF, 12000),
	      "values that come back are renewed");
	check(box.values.count == 3 && is_number("B", 3, 2000), "values that come back are the same values, not new ones");
	check(values_age(values_find(&box.values, "A"), 12000) == VALUE_AGE_FRESH && values_age(values_find(&box.values, "B"), 12000) == VALUE_AGE_GONE,
	      "the value that came back is fresh, the one that stayed away is gone");
	check(apply("{\"B\":5}", 5, 500) == VALUES_RENEWED && is_number("B", 5, 500) && is_number("A", 4, 12000),
	      "an answer at an earlier time (the clock stepped back) is applied with that time");
	check(guards_intact(), "disappearing values: nothing written outside the values");

	// Times are 64 bit
	start();
	check(apply("{\"A\":1}", -1, ((uint64_t)1 << 32) + 5) == VALUES_RENEWED && is_number("A", 1, ((uint64_t)1 << 32) + 5),
	      "a time beyond 32 bit is kept completely");
	check(values_age(values_find(&box.values, "A"), ((uint64_t)1 << 32) + 3004) == VALUE_AGE_FRESH &&
	      values_age(values_find(&box.values, "A"), ((uint64_t)1 << 32) + 3005) == VALUE_AGE_OLD, "and the value grows old from it");
	check(apply("{\"A\":2}", -1, UINT64_MAX) == VALUES_RENEWED && is_number("A", 2, UINT64_MAX) &&
	      values_age(values_find(&box.values, "A"), UINT64_MAX) == VALUE_AGE_FRESH, "the largest time is kept completely");
}

static void test_clear(void)
{
	static const char name33[] = "M23456789012345678901234567890123";

	start();
	snprintf(text, sizeof(text), "{\"A\":1,\"B\":\"on\",\"%s\":3}", name33);
	check(apply(text, 5, 1000) == VALUES_RENEWED && box.values.count == 2 && box.values.dropped == 1, "two values and a dropped one before clearing");
	values_clear(&box.values);
	check(box.values.count == 0 && is_missing("A") && is_missing("B"), "clear: every value is forgotten");
	check(values_age(values_find(&box.values, "A"), 1000) == VALUE_AGE_GONE, "clear: a forgotten value is gone");
	check(!box.values.has_pass, "clear: the pass counter is forgotten");
	check(box.values.dropped == 1, "clear: dropped keeps counting");
	check(guards_intact(), "clear: nothing written outside the values");
	check(apply("{\"B\":7}", 5, 2000) == VALUES_RENEWED && is_number("B", 7, 2000), "clear: the next answer renews although its counter is the old one");
	check(box.values.count == 1 && is_missing("A"), "clear: only what the next answer contains is there");
	check(apply("{\"B\":8}", 5, 3000) == VALUES_REPEATED && is_number("B", 7, 2000), "clear: after that the counter counts again");
	snprintf(text, sizeof(text), "{\"%s\":3}", name33);
	check(apply(text, 6, 4000) == VALUES_RENEWED && box.values.dropped == 2, "clear: the next dropped value is added to the count");

	// The counter is forgotten although there is no value to forget
	start();
	check(apply("{}", 7, 100) == VALUES_RENEWED && box.values.count == 0 && box.values.has_pass, "an empty answer with a counter before clearing");
	values_clear(&box.values);
	check(!box.values.has_pass, "clear: the pass counter of an empty list is forgotten as well");
	check(apply("{\"A\":1}", 7, 200) == VALUES_RENEWED && is_number("A", 1, 200), "clear: the next answer renews the empty list although its counter is the old one");

	// A full list is empty again
	start();
	many(VALUES_MAX, 0);
	check(apply(text, -1, 1) == VALUES_RENEWED && box.values.count == VALUES_MAX, "a full list before clearing");
	values_clear(&box.values);
	check(apply("{\"NEW\":1}", -1, 2) == VALUES_RENEWED && is_number("NEW", 1, 2) && box.values.count == 1 && box.values.dropped == 0 &&
	      is_missing("V00") && is_missing("V63") && guards_intact(), "clear: there is room again, the old names are gone");
}

/*
 * The same rules written a second time as a simple model: a table of names with what is expected of each.
 * Sequences of pseudo-random answers are applied to the model and to values_apply() and compared.
 */

#define POOL 80

typedef struct
{
	bool known;
	value_kind_t kind;
	double number;
	uint64_t seen_ms;
} expected_t;

static char pool[POOL][40];
static expected_t expected[POOL];
static int model_count;
static uint32_t model_dropped;
static bool model_has_pass;
static int64_t model_pass;
static uint32_t random_state;
// What the sequences came across, to show that they reach the rules at all
static int seen_repeated, seen_invalid, seen_cleared, seen_full, seen_long, seen_backwards, seen_came_back;

static uint32_t rnd(uint32_t below)
{
	random_state = random_state * 1664525u + 1013904223u;
	return (random_state >> 8) % below;
}

static void model_names(void)
{
	int i;

	for(i = 0; i < POOL; i++)
	{
		size_t length;

		snprintf(pool[i], sizeof(pool[i]), "P%02d", i);
		// Some names at the limit and one beyond it
		length = i % 10 == 3 ? 32 : i % 10 == 7 ? 33 : 0;
		while(strlen(pool[i]) < length) strcat(pool[i], "x");
	}
	pool[0][0] = '\0';
}

static bool model_matches(uint64_t now_ms)
{
	bool good = true;
	int i;

	if(box.values.count != model_count || box.values.dropped != model_dropped || box.values.has_pass != model_has_pass ||
	   (model_has_pass && (int64_t)box.values.pass != model_pass))
	{
		printf("  count %d (model %d), dropped %lu (model %lu), has_pass %d (model %d), pass %lu (model %ld)\n",
		       box.values.count, model_count, (unsigned long)box.values.dropped, (unsigned long)model_dropped,
		       box.values.has_pass, model_has_pass, (unsigned long)box.values.pass, (long)model_pass);
		good = false;
	}
	for(i = 0; i < POOL; i++)
	{
		const value_t *value = values_find(&box.values, pool[i]);
		value_age_t age = values_age(value, now_ms);
		value_age_t model_age = VALUE_AGE_GONE;
		bool same;

		if(expected[i].known)
		{
			uint64_t passed = now_ms > expected[i].seen_ms ? now_ms - expected[i].seen_ms : 0;

			model_age = passed <= 2999 ? VALUE_AGE_FRESH : passed <= 9999 ? VALUE_AGE_OLD : VALUE_AGE_GONE;
			same = value != NULL && strcmp(value->name, pool[i]) == 0 && value->kind == expected[i].kind &&
			       value->number == expected[i].number && value->seen_ms == expected[i].seen_ms;
		}
		else same = value == NULL;

		if(!same || age != model_age)
		{
			printf("  %s: model says %s, age %d\n", pool[i], expected[i].known ? "known" : "unknown", (int)model_age);
			good = false;
		}
	}
	return good && guards_intact();
}

static bool model_sequence(uint32_t seed, int steps)
{
	uint64_t now_ms = 1000 + seed;
	int64_t last_pass = 0;
	int step;

	random_state = seed;
	start();
	memset(expected, 0, sizeof(expected));
	model_count = 0;
	model_dropped = 0;
	model_has_pass = false;
	model_pass = 0;

	for(step = 0; step < steps; step++)
	{
		uint32_t what = rnd(100);
		uint32_t density = 1 + rnd(9);
		values_result_t result, model_result;
		size_t length = 0;
		int64_t pass;
		bool first = true;
		int i;

		// Time goes on, sometimes it jumps back
		if(rnd(10) == 0)
		{
			now_ms = now_ms > 5000 ? now_ms - rnd(5000) : 0;
			seen_backwards++;
		}
		else now_ms += rnd(4000);

		if(what < 2)
		{
			values_clear(&box.values);
			for(i = 0; i < POOL; i++) expected[i].known = false;
			model_count = 0;
			model_has_pass = false;
			seen_cleared++;
			if(!model_matches(now_ms)) return false;
			continue;
		}

		switch(rnd(10))
		{
			case 0: case 1: case 2: pass = last_pass; break;
			case 3: case 4: case 5: case 6: pass = last_pass + 1; break;
			case 7: pass = rnd(4); break;
			default: pass = -1; break;
		}
		if(pass >= 0) last_pass = pass;

		if(what < 8)
		{
			static const char *const broken[] = {"[1,2]", "{\"error\":\"x\"}", "{\"P01\":1", "7", "{\"P02\":1,}", ""};
			const char *json = broken[rnd(6)];

			remember();
			result = values_apply(&box.values, json, strlen(json), pass, now_ms, large_work, 1024);
			if(result != VALUES_INVALID || !unchanged()) return false;
			seen_invalid++;
			continue;
		}

		// The model decides before the members are drawn, the text is built in any case
		model_result = pass >= 0 && model_has_pass && model_pass == pass ? VALUES_REPEATED : VALUES_RENEWED;
		if(model_result == VALUES_REPEATED) seen_repeated++;

		length += (size_t)snprintf(text + length, sizeof(text) - length, "{");
		for(i = 0; i < POOL; i++)
		{
			// The order of the members changes from answer to answer
			int index = (i * 7 + step) % POOL;
			uint32_t kind = rnd(20);
			int quarters = (int)rnd(8000) - 4000;
			bool is_value = kind < 16;

			if(rnd(10) >= density) continue;

			length += (size_t)snprintf(text + length, sizeof(text) - length, "%s\"%s\":", first ? "" : ",", pool[index]);
			first = false;
			if(kind < 12) length += (size_t)snprintf(text + length, sizeof(text) - length, "%s%d.%02d", quarters < 0 ? "-" : "",
			                                         abs(quarters) / 4, abs(quarters) % 4 * 25);
			else if(kind < 14) length += (size_t)snprintf(text + length, sizeof(text) - length, "\"on\"");
			else if(kind < 16) length += (size_t)snprintf(text + length, sizeof(text) - length, "\"off\"");
			else if(kind == 16) length += (size_t)snprintf(text + length, sizeof(text) - length, "\"text\"");
			else if(kind == 17) length += (size_t)snprintf(text + length, sizeof(text) - length, "null");
			else if(kind == 18) length += (size_t)snprintf(text + length, sizeof(text) - length, "[1,\"on\"]");
			else length += (size_t)snprintf(text + length, sizeof(text) - length, "{\"P05\":1}");

			if(model_result != VALUES_RENEWED || !is_value) continue;
			if(strlen(pool[index]) > 32)
			{
				model_dropped++;
				seen_long++;
				continue;
			}
			if(!expected[index].known)
			{
				if(model_count == 64)
				{
					model_dropped++;
					seen_full++;
					continue;
				}
				expected[index].known = true;
				model_count++;
			}
			else if(expected[index].seen_ms + 10000 <= now_ms) seen_came_back++;
			expected[index].kind = kind < 12 ? VALUE_NUMBER : kind < 14 ? VALUE_ON : VALUE_OFF;
			expected[index].number = kind < 12 ? quarters / 4.0 : 0;
			expected[index].seen_ms = now_ms;
		}
		snprintf(text + length, sizeof(text) - length, "}");

		// An object with one member named error does not occur: no name of the pool is "error"
		if(model_result == VALUES_RENEWED && pass >= 0)
		{
			model_has_pass = true;
			model_pass = pass;
		}

		result = values_apply(&box.values, text, strlen(text), pass, now_ms, large_work, 1024);
		if(result != model_result)
		{
			printf("  step %d: result %d, model %d\n", step, (int)result, (int)model_result);
			return false;
		}
		if(!model_matches(now_ms))
		{
			printf("  step %d of seed %lu\n", step, (unsigned long)seed);
			return false;
		}
	}
	return true;
}

static void test_model(void)
{
	char what[96];
	uint32_t seed;

	model_names();
	check(strlen(pool[3]) == 32 && strlen(pool[7]) == 33 && strlen(pool[0]) == 0 && strcmp(pool[12], "P12") == 0,
	      "model: the pool has names of 0, 3, 32 and 33 bytes");
	for(seed = 1; seed <= 40; seed++)
	{
		snprintf(what, sizeof(what), "model: 250 random answers with seed %lu behave as the rules say", (unsigned long)seed);
		check(model_sequence(seed, 250), what);
	}
	printf("  repeated %d, invalid %d, cleared %d, no room %d, name too long %d, clock back %d, came back %d\n",
	       seen_repeated, seen_invalid, seen_cleared, seen_full, seen_long, seen_backwards, seen_came_back);
	check(seen_repeated > 100 && seen_invalid > 100 && seen_cleared > 20 && seen_full > 100 && seen_long > 100 &&
	      seen_backwards > 100 && seen_came_back > 100,
	      "model: the sequences contain repeated and invalid answers, clears, full lists, long names, steps back in time, returning values");
}

int main(void)
{
	memset(&work[VALUES_TOKENS], 0x5A, 2 * sizeof(work[0]));

	test_init();
	test_stale_tokens();
	test_fixtures();
	test_kinds();
	test_names();
	test_pass();
	test_invalid();
	test_limits();
	test_age();
	test_disappear();
	test_clear();
	test_model();

	check(work[VALUES_TOKENS].start == 0x5A5A5A5A && work[VALUES_TOKENS + 1].start == 0x5A5A5A5A,
	      "no call wrote behind the VALUES_TOKENS tokens it was given");
	return test_end();
}
