/*
 * Host test for display/components/core/catalog.c. Run "make test_catalog && ./test_catalog" in display/test.
 * redproof.py removes or weakens every rule once (mutations/catalog.py) and expects this test to fail.
 */
#include <stdlib.h>
#include <stdint.h>
#include "test.h"
#include "catalog.h"

#define GUARD 0xA5

// The catalogue lies between guard bytes: an entry written behind the last place lands there and is seen,
// where the address sanitizer would only stop the program without naming a rule.
typedef struct
{
	unsigned char before[128];
	catalog_t catalog;
	unsigned char after[128];
} box_t;

static box_t box, snapshot, other;

// Two tokens before and behind the room the reader is told about show whether it leaves that room.
// The two before it look like texts: a member that is looked up at index -1 then shows up as a wrong unit.
static json_token_t work_between_guards[2 + CATALOG_TOKENS + 2];
static json_token_t *const work = &work_between_guards[2];
static const json_token_t front_guard = {JSON_STRING, 0, 1, 0, 1};
static json_token_t large_work[2048];
static json_token_t values_work[VALUES_TOKENS];
static values_t values;
static char text[65536];
static char out[65536];

static const char name32[] = "N2345678901234567890123456789012";
static const char name33[] = "M23456789012345678901234567890123";

static bool guards_intact_of(const box_t *b)
{
	size_t i;

	for(i = 0; i < sizeof(b->before); i++)
	{
		if(b->before[i] != GUARD) return false;
	}
	for(i = 0; i < sizeof(b->after); i++)
	{
		if(b->after[i] != GUARD) return false;
	}
	return true;
}

static bool guards_intact(void)
{
	return guards_intact_of(&box);
}

// A fresh catalogue between fresh guard bytes
static void start(void)
{
	memset(&box, GUARD, sizeof(box));
	catalog_init(&box.catalog);
}

static void remember(void)
{
	memcpy(&snapshot, &box, sizeof(box));
}

static bool unchanged(void)
{
	return memcmp(&snapshot, &box, sizeof(box)) == 0;
}

static bool config(const char *json)
{
	return catalog_apply_config(&box.catalog, json, strlen(json), work, CATALOG_TOKENS);
}

static bool stored(const char *json)
{
	return catalog_from_json(&box.catalog, json, strlen(json), work, CATALOG_TOKENS);
}

// An answer of GET /autopid_data arrives and is noted in the catalogue
static bool deliver(const char *json)
{
	values_init(&values);
	if(values_apply(&values, json, strlen(json), -1, 1, values_work, VALUES_TOKENS) != VALUES_RENEWED) return false;
	catalog_note_values(&box.catalog, &values);
	return true;
}

static bool entry_of_is(const catalog_t *catalog, int index, const char *name, const char *unit, const char *value_class,
                        bool in_profile, bool delivered)
{
	const catalog_entry_t *entry;

	if(index < 0 || index >= catalog->count || index >= CATALOG_MAX) return false;
	entry = catalog->entries + index;
	return strcmp(entry->name, name) == 0 && strcmp(entry->unit, unit) == 0 && strcmp(entry->value_class, value_class) == 0 &&
	       entry->in_profile == in_profile && entry->delivered == delivered && catalog_find(catalog, name) == index;
}

static bool entry_is(int index, const char *name, const char *unit, const char *value_class, bool in_profile, bool delivered)
{
	return entry_of_is(&box.catalog, index, name, unit, value_class, in_profile, delivered);
}

static bool battery_is_first(void)
{
	return entry_is(0, "@BATT_V", "V", "", false, false);
}

static int find(const char *name)
{
	return catalog_find(&box.catalog, name);
}

// {"P000":{"class":"c","unit":"u"},"P001":...}: `count` values of a profile. Built here, not by the code under test.
static void many_config(char prefix, int count, const char *unit)
{
	size_t length = 0;
	int i;

	length += (size_t)snprintf(text + length, sizeof(text) - length, "{");
	for(i = 0; i < count; i++)
	{
		length += (size_t)snprintf(text + length, sizeof(text) - length, "%s\"%c%03d\":{\"class\":\"c\",\"unit\":\"%s\"}",
		                           i ? "," : "", prefix, i, unit);
	}
	snprintf(text + length, sizeof(text) - length, "}");
}

// The same in the format of catalog_to_json
static void many_stored(char prefix, int count)
{
	size_t length = 0;
	int i;

	length += (size_t)snprintf(text + length, sizeof(text) - length, "{");
	for(i = 0; i < count; i++)
	{
		length += (size_t)snprintf(text + length, sizeof(text) - length,
		                           "%s\"%c%03d\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true,\"delivered\":false}", i ? "," : "", prefix, i);
	}
	snprintf(text + length, sizeof(text) - length, "}");
}

static bool many_are(char prefix, int count, const char *unit, bool delivered)
{
	int i;

	for(i = 0; i < count; i++)
	{
		char name[16];	// room for any int: gcc refuses a snprintf() that might cut

		snprintf(name, sizeof(name), "%c%03d", prefix, i);
		if(!entry_is(i + 1, name, unit, "c", true, delivered)) return false;
	}
	return true;
}

// What tools/w906/fixtures/car_config_w906.json says, copied from it
static const struct
{
	const char *name;
	const char *value_class;
	const char *unit;
} w906[] = {
	{"ENGINE_RPM", "frequency", "RPM"},
	{"CHARGE_AIR_TEMP_PRE_IC", "temperature", "°C"},
	{"CHARGE_AIR_TEMP_POST_IC", "temperature", "°C"},
	{"EGT_PRE_TURBO", "temperature", "°C"},
	{"EGT_POST_EGR_COOLER", "temperature", "°C"},
	{"EGT_PRE_CAT", "temperature", "°C"},
	{"EGT_PRE_DPF", "temperature", "°C"},
	{"EGT_PRE_SCR", "temperature", "°C"},
	{"FUEL_TEMP", "temperature", "°C"},
	{"COOLANT_TMP", "temperature", "°C"},
	{"ENGINE_OIL_TEMP", "temperature", "°C"},
	{"OIL_LEVEL", "distance", "mm"},
	{"LAMBDA", "none", ""},
	{"DPF_DIFF_PRESSURE", "pressure", "hPa"},
	{"RAIL_PRESSURE", "pressure", "bar"},
	{"BOOST_PRESSURE", "pressure", "hPa"},
	{"BOOST_PRESSURE_LP", "pressure", "hPa"},
	{"EXHAUST_BACK_PRESSURE", "pressure", "hPa"},
	{"BARO_PRESSURE", "pressure", "hPa"},
	{"INTAKE_AIR_PRESSURE", "pressure", "hPa"},
	{"INTAKE_AIR_TMP", "temperature", "°C"},
	{"INJECTION_QUANTITY", "none", "mg"},
	{"AIR_MASS_PER_STROKE", "none", "mg"},
	{"EGR_RATE", "none", "%"},
	{"ACCEL_PEDAL", "none", "%"},
	{"WASTEGATE", "none", "%"},
	{"FUEL_L", "none", "L"},
	{"ECU_DISTANCE", "distance", "km"},
	{"THROTTLE", "none", "%"},
	{"EGR_VALVE", "none", "%"},
	{"DPF_ASH", "weight", "g"},
	{"DPF_KM_SINCE_REGEN", "distance", "km"},
	{"DPF_REGEN_STATUS", "none", ""},
	{"DPF_SOOT_MASS", "weight", "g"},
	{"DPF_SOOT_SIM", "weight", "g"},
};
#define W906_COUNT ((int)(sizeof(w906) / sizeof(w906[0])))

static bool w906_entries_are(const catalog_t *catalog, bool in_profile, bool delivered)
{
	int i;

	for(i = 0; i < W906_COUNT; i++)
	{
		if(!entry_of_is(catalog, i + 1, w906[i].name, w906[i].unit, w906[i].value_class, in_profile, delivered)) return false;
	}
	return true;
}

// The checksum as catalog.h describes it, written a second time: all bytes in a row first, then FNV-1a over them
static uint32_t model_checksum(const catalog_t *catalog)
{
	static unsigned char bytes[CATALOG_MAX * (VALUE_NAME_SIZE + CATALOG_UNIT_SIZE + CATALOG_CLASS_SIZE + 1)];
	uint32_t hash = 0x811C9DC5u;
	size_t length = 0;
	size_t i;
	int e;

	for(e = 0; e < catalog->count; e++)
	{
		const catalog_entry_t *entry = catalog->entries + e;

		memcpy(bytes + length, entry->name, strlen(entry->name) + 1);
		length += strlen(entry->name) + 1;
		memcpy(bytes + length, entry->unit, strlen(entry->unit) + 1);
		length += strlen(entry->unit) + 1;
		memcpy(bytes + length, entry->value_class, strlen(entry->value_class) + 1);
		length += strlen(entry->value_class) + 1;
		bytes[length++] = entry->in_profile ? 1 : 0;
	}
	for(i = 0; i < length; i++)
	{
		hash ^= bytes[i];
		hash *= 0x01000193u;
	}
	return hash;
}

static void test_init(void)
{
	// Whatever was in the memory before: entries, a count, dropped names
	memset(&box, 0x01, sizeof(box));
	memset(box.before, GUARD, sizeof(box.before));
	memset(box.after, GUARD, sizeof(box.after));
	catalog_init(&box.catalog);

	check(box.catalog.count == 1, "init: one entry");
	check(strcmp(box.catalog.entries[0].name, "@BATT_V") == 0 && strcmp(CATALOG_BATTERY, "@BATT_V") == 0, "init: the entry is the battery voltage @BATT_V");
	check(strcmp(box.catalog.entries[0].unit, "V") == 0, "init: its unit is V");
	check(box.catalog.entries[0].value_class[0] == '\0', "init: it has no class");
	check(battery_is_first(), "init: it is not in the profile and not delivered");
	check(box.catalog.dropped == 0, "init: nothing dropped");
	check(find("@BATT_V") == 0 && find("ENGINE_RPM") == -1 && find("") == -1 && find("@BATT") == -1 && find("@BATT_VV") == -1 && find("V") == -1,
	      "init: find knows the battery entry and nothing else");
	check(guards_intact(), "init: writes nothing outside the catalogue");
	check(CATALOG_MAX == 96 && CATALOG_UNIT_SIZE == 12 && CATALOG_CLASS_SIZE == 24 && sizeof(box.catalog.entries[0].name) == 33,
	      "room for 96 entries, names up to 32, units up to 11, classes up to 23 bytes");
}

// What the reader left in the token room from the text before is not part of the next text
static void test_stale_tokens(void)
{
	static char two[] = "{\"A\":{},\"B\":{}}";
	static char stored_two[] = "{\"A\":{\"unit\":\"\",\"class\":\"\",\"profile\":true},\"B\":{\"unit\":\"\",\"class\":\"\",\"profile\":true}}";
	char *comma = strstr(stored_two, ",\"B\"");

	start();
	check(catalog_apply_config(&box.catalog, two, strlen(two), work, CATALOG_TOKENS) && box.catalog.count == 3 && entry_is(2, "B", "", "", true, false),
	      "a configuration with two values is read");
	// The same memory now holds {"A":{}} followed by "B":{}}
	two[7] = '}';
	check(catalog_apply_config(&box.catalog, two, 8, work, CATALOG_TOKENS) && entry_is(1, "A", "", "", true, false),
	      "a configuration with one value in the same memory is read");
	check(box.catalog.count == 2 && find("B") == -1, "the second value of the configuration before is not read again from its old tokens");

	// A text without any token leaves those of the text before where they are, and the text they refer to as well
	start();
	remember();
	check(!catalog_apply_config(&box.catalog, two, 0, work, CATALOG_TOKENS) && unchanged(),
	      "length 0 in the same memory: no configuration, the tokens of the text before are not used");
	two[0] = ' ';
	check(!catalog_apply_config(&box.catalog, two, 1, work, CATALOG_TOKENS) && unchanged(),
	      "a space in the same memory: no configuration, the tokens of the text before are not used");

	start();
	check(comma != NULL && catalog_from_json(&box.catalog, stored_two, strlen(stored_two), work, CATALOG_TOKENS) && box.catalog.count == 3 &&
	      entry_is(2, "B", "", "", true, false), "a stored catalogue with two entries is read");
	if(comma != NULL)
	{
		// The same memory now holds the first entry, a closing brace, and what is left of the second
		*comma = '}';
		check(catalog_from_json(&box.catalog, stored_two, (size_t)(comma - stored_two) + 1, work, CATALOG_TOKENS) && entry_is(1, "A", "", "", true, false),
		      "a stored catalogue with one entry in the same memory is read");
		check(box.catalog.count == 2 && find("B") == -1, "the second entry of the text before is not read again from its old tokens");

		start();
		remember();
		check(!catalog_from_json(&box.catalog, stored_two, 0, work, CATALOG_TOKENS) && unchanged(),
		      "length 0 in the same memory: no stored catalogue, the tokens of the text before are not used");
		stored_two[0] = ' ';
		check(!catalog_from_json(&box.catalog, stored_two, 1, work, CATALOG_TOKENS) && unchanged(),
		      "a space in the same memory: no stored catalogue, the tokens of the text before are not used");
	}
}

static void test_w906(void)
{
	static char profile[16384];
	char what[96];
	int count, pids, i, j;
	int names = 0, in_order = 0;

	start();
	check(read_fixture("../../tools/w906/fixtures/car_config_w906.json", text, sizeof(text)), "fixture configuration of the W906");
	check(config(text), "W906: the configuration is applied with CATALOG_TOKENS tokens");
	check(box.catalog.count == 36 && W906_COUNT == 35 && box.catalog.dropped == 0, "W906: the battery and 35 values, none dropped");
	check(battery_is_first(), "W906: the battery entry stays the first and is not in the profile");
	for(i = 0; i < W906_COUNT; i++)
	{
		snprintf(what, sizeof(what), "W906: %s with unit and class, in the profile, at its place in the text", w906[i].name);
		check(entry_is(i + 1, w906[i].name, w906[i].unit, w906[i].value_class, true, false), what);
	}
	check(guards_intact(), "W906: writes nothing outside the catalogue");
	check(catalog_checksum(&box.catalog) == 0x670CB785u, "W906: the checksum is the FNV-1a of names, units, classes and in_profile");

	// The same names in the same order as the vehicle profile the adapter loads
	check(read_fixture("../../vehicle_profiles/mercedes/sprinter_w906_om651.json", profile, sizeof(profile)), "vehicle profile");
	count = json_parse(profile, strlen(profile), large_work, 2048);
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
				if(json_text(profile, &large_work[key], name, sizeof(name)) && find(name) == names) in_order++;
				key += 1 + large_work[key + 1].skip;
			}
		}
	}
	check(names == 35 && in_order == 35, "W906: the catalogue has the values of the vehicle profile in its order");

	// The values arrive
	check(read_fixture("../../tools/w906/fixtures/autopid_data_ignition_on.json", text, sizeof(text)), "fixture values of the W906");
	check(deliver(text) && box.catalog.count == 36 && box.catalog.dropped == 0, "W906: the values that arrive are all known");
	check(w906_entries_are(&box.catalog, true, true), "W906: every value that arrived is marked as delivered and keeps unit, class and place");
	check(battery_is_first(), "W906: the battery entry is not delivered by values");
	check(catalog_checksum(&box.catalog) == 0x670CB785u, "W906: the checksum does not change with delivered");

	// Ignition off: nothing arrives, the configuration is read again
	check(read_fixture("../../tools/w906/fixtures/autopid_data_ignition_off.json", text, sizeof(text)), "fixture no values");
	remember();
	check(deliver(text) && unchanged(), "W906: an answer without values changes nothing");
	check(read_fixture("../../tools/w906/fixtures/car_config_w906.json", text, sizeof(text)) && config(text) && unchanged(),
	      "W906: the same configuration again changes nothing, delivered stays");
	check(config("{}") && box.catalog.count == 36 && w906_entries_are(&box.catalog, false, true) && battery_is_first(),
	      "W906: without a profile the delivered values stay, no longer in the profile");

	// Without a configuration the catalogue is made of what arrives
	start();
	check(read_fixture("../../tools/w906/fixtures/autopid_data_ignition_on.json", text, sizeof(text)) && deliver(text), "W906: values without a configuration");
	names = 0;
	for(i = 0; i < W906_COUNT; i++)
	{
		int index = find(w906[i].name);

		if(index > 0 && entry_is(index, w906[i].name, "", "", false, true)) names++;
	}
	check(names == 35 && box.catalog.count == 36 && battery_is_first(),
	      "W906: every value that arrived is appended, delivered, not in the profile, without unit and class");
	check(read_fixture("../../tools/w906/fixtures/car_config_w906.json", text, sizeof(text)) && config(text) && box.catalog.count == 36, "W906: the configuration comes later");
	names = 0;
	for(i = 0; i < W906_COUNT; i++)
	{
		int index = find(w906[i].name);

		if(index > 0 && entry_is(index, w906[i].name, w906[i].unit, w906[i].value_class, true, true)) names++;
	}
	check(names == 35, "W906: the delivered values get unit and class and are in the profile now");
}

static void test_config_members(void)
{
	start();
	check(config("{\"A\":{\"class\":\"c1\",\"unit\":\"u1\"},\"B\":{\"unit\":\"u2\"},\"C\":{\"class\":\"c3\"},\"D\":{},"
	             "\"E\":{\"unit\":\"u5\",\"class\":\"c5\"},"
	             "\"F\":{\"x\":1,\"unit\":\"u6\",\"y\":[{\"unit\":\"no\"}],\"class\":\"c6\",\"z\":{\"class\":\"no\"}}}"),
	      "a configuration with and without units and classes is applied");
	check(box.catalog.count == 7 && box.catalog.dropped == 0, "six values behind the battery");
	check(entry_is(1, "A", "u1", "c1", true, false), "a value with class and unit");
	check(entry_is(2, "B", "u2", "", true, false), "a value without class: the class is empty");
	check(entry_is(3, "C", "", "c3", true, false), "a value without unit: the unit is empty");
	check(entry_is(4, "D", "", "", true, false), "a value without both");
	check(entry_is(5, "E", "u5", "c5", true, false), "unit before class");
	check(entry_is(6, "F", "u6", "c6", true, false), "other members of a value are ignored, also nested ones named unit and class");

	start();
	check(config("{\"A\":{\"unit\":5,\"class\":null},\"B\":{\"unit\":[\"x\"],\"class\":{\"class\":\"c\"}},\"C\":{\"unit\":true,\"class\":false},"
	             "\"D\":{\"unit\":\"\",\"class\":\"\"}}") &&
	      entry_is(1, "A", "", "", true, false) && entry_is(2, "B", "", "", true, false) && entry_is(3, "C", "", "", true, false) &&
	      entry_is(4, "D", "", "", true, false), "a unit or class that is no text counts as none");

	start();
	check(config("{\"A\":5,\"B\":\"x\",\"C\":null,\"D\":[{\"unit\":\"x\"}],\"E\":true,\"F\":{\"unit\":\"u\"},\"G\":false,\"H\":[],\"I\":1.5}"),
	      "a configuration with members that are no objects is applied");
	check(box.catalog.count == 2 && entry_is(1, "F", "u", "", true, false) && find("A") == -1 && find("B") == -1 && find("C") == -1 &&
	      find("D") == -1 && find("E") == -1 && find("G") == -1 && find("H") == -1 && find("I") == -1,
	      "members whose value is no object are ignored, the object between them is taken");
	check(box.catalog.dropped == 0, "ignored members are not counted as dropped");

	// The adapter answers this request with a status 500 when it fails, never with an error object as GET /autopid_data
	// does: an object with a member "error" is a configuration like any other
	start();
	check(config("{\"A\":{}}") && box.catalog.count == 2, "a profile with one value");
	check(config("{\"error\":\"No data available\"}") && box.catalog.count == 1 && battery_is_first(),
	      "an object with a text named error is a configuration that names nothing");
	check(config("{\"error\":{\"unit\":\"u\"}}") && box.catalog.count == 2 && entry_is(1, "error", "u", "", true, false),
	      "an object named error alone in a configuration is a value like any other");
	check(config("{\"A\":null}") && box.catalog.count == 1 && config("{\"A\":{}}") && box.catalog.count == 2 && config("{\"A\":5}") && box.catalog.count == 1 &&
	      config("{\"A\":{}}") && config("{\"A\":false}") && box.catalog.count == 1 && config("{\"A\":{}}") && config("{\"A\":[]}") && box.catalog.count == 1 &&
	      battery_is_first(), "a single member that is null, a number, false or an array is a configuration that names nothing");

	// Known ones are updated in place
	start();
	check(config("{\"A\":{\"unit\":\"u1\",\"class\":\"c1\"},\"B\":{\"unit\":\"u2\",\"class\":\"c2\"}}") && box.catalog.count == 3, "two values of a profile");
	check(config("{\"B\":{\"unit\":\"v2\",\"class\":\"d2\"},\"A\":{\"unit\":\"v1\",\"class\":\"d1\"}}"), "the same names with other texts in another order");
	check(entry_is(1, "A", "v1", "d1", true, false) && entry_is(2, "B", "v2", "d2", true, false) && box.catalog.count == 3,
	      "unit and class of known ones are updated, they keep their place");
	check(config("{\"A\":{},\"B\":{\"unit\":7,\"class\":\"d3\"}}") && entry_is(1, "A", "", "", true, false) && entry_is(2, "B", "", "d3", true, false),
	      "a known one that has no unit or class any more loses the text");

	// Two members with the same name
	start();
	check(config("{\"A\":{\"unit\":\"x\"},\"B\":{},\"A\":{\"unit\":\"y\",\"class\":\"c\"}}") && box.catalog.count == 3 &&
	      entry_is(1, "A", "y", "c", true, false) && entry_is(2, "B", "", "", true, false),
	      "a name that comes twice is one entry at its first place, the second member updates it");
	check(config("{\"A\":5,\"B\":{},\"A\":{\"unit\":\"z\"}}") && box.catalog.count == 3 && entry_is(1, "A", "z", "", true, false) &&
	      entry_is(2, "B", "", "", true, false), "a known name whose first member is no object is still named by the second: it keeps its place");
	check(config("{\"A\":{\"unit\":\"w\"},\"B\":{},\"A\":5}") && box.catalog.count == 3 && entry_is(1, "A", "w", "", true, false) &&
	      entry_is(2, "B", "", "", true, false), "a member that is no object behind the object does not undo it");
	check(config("{\"A\":5,\"B\":{}}") && box.catalog.count == 2 && find("A") == -1 && entry_is(1, "B", "", "", true, false),
	      "a known name that only a member without object names is no longer named");
}

static void test_config_invalid(void)
{
	static const struct
	{
		const char *json;
		const char *what;
	} invalid[] = {
		// A scalar first: it has no members, so even a reader that goes on does it in an orderly way
		{"5", "no configuration: a number"},
		{"{\"N\":{\"unit\":\"u\"}", "no configuration: an object that is not closed"},
		{"[]", "no configuration: an empty array"},
		{"[{\"N\":{}}]", "no configuration: an array with a configuration in it"},
		{"\"x\"", "no configuration: a text"},
		{"null", "no configuration: null"},
		{"true", "no configuration: true"},
		{"{\"N\":{}},", "no configuration: a comma behind the object"},
		{"{\"N\":{},}", "no configuration: a comma before the end"},
		{"{\"N\":{\"unit\":}}", "no configuration: a member without value"},
		{"{\"N\":{}}x", "no configuration: text behind the object"},
		{"{\"N\":{\"a\":[[[[[[[1]]]]]]]}}", "no configuration: nested deeper than the reader follows"},
		{"{", "no configuration: only a brace"},
		{"", "no configuration: an empty text"},
		{" ", "no configuration: a space"},
		{"\n\t \r", "no configuration: nothing but whitespace"},
	};
	size_t i, cut;
	int wrong = 0;

	start();
	check(config("{\"B\":{},\"A\":{\"unit\":\"u\",\"class\":\"c\"}}") && deliver("{\"B\":1}") && box.catalog.count == 3, "a catalogue before the invalid texts");
	remember();
	for(i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) check(!config(invalid[i].json) && unchanged(), invalid[i].what);

	// A body that the connection cut off is no configuration, wherever it ends: an object is only whole with its last byte
	check(read_fixture("../../tools/w906/fixtures/car_config_w906.json", text, sizeof(text)), "fixture configuration of the W906, to be cut off");
	cut = strlen(text);
	for(i = 0; i < cut; i++)
	{
		if(catalog_apply_config(&box.catalog, text, i, work, CATALOG_TOKENS) || !unchanged()) wrong++;
	}
	check(cut > 1700 && wrong == 0, "the configuration of the W906 cut off after any number of its bytes is no configuration, nothing changed");

	// The same when the display starts and the catalogue is fresh
	for(i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
	{
		start();
		remember();
		if(config(invalid[i].json) || !unchanged())
		{
			printf("  %s\n", invalid[i].what);
			wrong++;
		}
	}
	for(i = 0; i < cut; i++)
	{
		start();
		remember();
		if(catalog_apply_config(&box.catalog, text, i, work, CATALOG_TOKENS) || !unchanged()) wrong++;
	}
	check(wrong == 0, "none of these texts is a configuration for a fresh catalogue either, nothing changed");
	start();
	check(config("{\"B\":{},\"A\":{\"unit\":\"u\",\"class\":\"c\"}}") && deliver("{\"B\":1}") && box.catalog.count == 3, "the catalogue before the invalid texts again");
	remember();

	// Only `length` bytes belong to the text
	check(!catalog_apply_config(&box.catalog, "{\"N\":{}}", 7, work, CATALOG_TOKENS) && unchanged(), "a length that ends before the last brace: no configuration");
	check(catalog_apply_config(&box.catalog, "{\"N\":{}}x", 8, work, CATALOG_TOKENS) && entry_is(2, "N", "", "", true, false),
	      "bytes behind the given length are not read");

	// Room for the tokens: {"P000":{"class":"c","unit":"u"}} has 1 + 6 of them
	start();
	many_config('P', 1, "u");
	check(catalog_apply_config(&box.catalog, text, strlen(text), work, 7) && entry_is(1, "P000", "u", "c", true, false),
	      "exactly as many tokens as the configuration needs: applied");
	start();
	memset(&work[6], 0x5A, 2 * sizeof(work[0]));
	remember();
	check(!catalog_apply_config(&box.catalog, text, strlen(text), work, 6) && unchanged(),
	      "one token less than the configuration needs: false, nothing changed");
	check(work[6].start == 0x5A5A5A5A && work[7].start == 0x5A5A5A5A,
	      "one token less than the configuration needs: nothing written behind the room of the reader");
	start();
	remember();
	memset(&work[0], 0x5A, 2 * sizeof(work[0]));
	check(!catalog_apply_config(&box.catalog, "{}", 2, work, 0) && unchanged() && work[0].start == 0x5A5A5A5A,
	      "no room for tokens at all: false, nothing written");
	check(!catalog_apply_config(&box.catalog, "{}", 2, work, -1) && !catalog_from_json(&box.catalog, "{}", 2, work, -1) &&
	      !catalog_from_json(&box.catalog, "{}", 2, work, 0) && unchanged() && work[0].start == 0x5A5A5A5A,
	      "no or a negative room for tokens, both readers: false, nothing written");
}

static void test_config_removal(void)
{
	start();
	check(config("{\"A\":{},\"B\":{},\"C\":{},\"D\":{}}") && box.catalog.count == 5, "four values of a profile");
	check(deliver("{\"B\":1,\"Z\":2}") && entry_is(2, "B", "", "", true, true) && entry_is(5, "Z", "", "", false, true) && box.catalog.count == 6,
	      "one of them arrives, and one the profile does not name");
	check(config("{\"D\":{\"unit\":\"x\"},\"E\":{}}"), "a profile that names one of them and a new one");
	check(box.catalog.count == 5 && find("A") == -1 && find("C") == -1, "entries that are no longer named and were not delivered are removed");
	check(entry_is(1, "B", "", "", false, true), "a delivered one that is no longer named stays and loses in_profile");
	check(entry_is(2, "D", "x", "", true, false), "the one that is still named moves up and is updated");
	check(entry_is(3, "Z", "", "", false, true), "a value that only arrived stays");
	check(entry_is(4, "E", "", "", true, false), "the new one is appended behind all that stay");
	check(battery_is_first(), "the battery entry stays although no profile names it and nothing delivered it");
	check(config("{\"Z\":{\"unit\":\"u\"}}") && box.catalog.count == 3 && entry_is(1, "B", "", "", false, true) && entry_is(2, "Z", "u", "", true, true),
	      "a delivered value the profile names now is in the profile, stays delivered and gets its unit");
	check(config("{}") && box.catalog.count == 3 && entry_is(1, "B", "", "", false, true) && entry_is(2, "Z", "u", "", false, true) && battery_is_first(),
	      "an empty profile: delivered ones stay with their texts, nothing is in the profile");
	check(guards_intact(), "removal: nothing written outside the catalogue");

	// The battery entry
	start();
	check(config("{}") && box.catalog.count == 1 && battery_is_first(), "an empty profile leaves a fresh catalogue as it is");
	check(config("{\"@BATT_V\":{\"unit\":\"mV\",\"class\":\"voltage\"},\"A\":{}}") && box.catalog.count == 2 &&
	      entry_is(0, "@BATT_V", "mV", "voltage", true, false) && entry_is(1, "A", "", "", true, false),
	      "a profile that names the battery updates its entry in place");
	check(config("{\"A\":{}}") && box.catalog.count == 2 && entry_is(0, "@BATT_V", "mV", "voltage", false, false),
	      "the battery entry that is no longer named stays and loses in_profile");
	check(deliver("{\"@BATT_V\":12.5}") && entry_is(0, "@BATT_V", "mV", "voltage", false, true) && box.catalog.count == 2,
	      "a value named like the battery marks its entry as delivered");

	// Only the name CATALOG_BATTERY itself is kept
	start();
	check(config("{\"@\":{},\"@BATT\":{},\"@BATT_VV\":{},\"BATT_V\":{},\"@batt_v\":{}}") && box.catalog.count == 6 && box.catalog.dropped == 0,
	      "a profile with names that look like the one of the battery");
	check(config("{}") && box.catalog.count == 1 && battery_is_first(), "they are removed like any other when they are no longer named");

	// What was dropped stays counted
	start();
	snprintf(text, sizeof(text), "{\"%s\":{}}", name33);
	check(config(text) && box.catalog.count == 1 && box.catalog.dropped == 1, "a profile whose only name is too long");
	check(config("{}") && box.catalog.count == 1 && box.catalog.dropped == 1, "an empty profile does not forget what was dropped");

	// An entry with an empty name is an entry like any other
	start();
	check(config("{\"\":{\"unit\":\"u\"},\"A\":{}}") && box.catalog.count == 3 && entry_is(1, "", "u", "", true, false), "a profile value with an empty name");
	check(config("{\"A\":{}}") && box.catalog.count == 2 && find("") == -1 && entry_is(1, "A", "", "", true, false),
	      "the entry with the empty name is removed when it is no longer named");

	// The end of a full catalogue is treated like its beginning
	start();
	many_config('P', CATALOG_MAX - 1, "u");
	check(config(text) && deliver("{\"P090\":1,\"P070\":2}") && config(text) && box.catalog.count == CATALOG_MAX &&
	      entry_is(71, "P070", "u", "c", true, true) && entry_is(91, "P090", "u", "c", true, true) && entry_is(92, "P091", "u", "c", true, false) &&
	      entry_is(95, "P094", "u", "c", true, false), "the same profile again: entries at the end of a full catalogue keep place and delivered");
	check(config("{\"P094\":{}}") && box.catalog.count == 4 && entry_is(1, "P070", "u", "c", false, true) && entry_is(2, "P090", "u", "c", false, true) &&
	      entry_is(3, "P094", "", "", true, false) && battery_is_first(),
	      "a profile with the last value only: the delivered ones from the end stay, the named one moves up, the others go");

	// A place that was used before is clean
	start();
	check(config("{\"A\":{},\"B\":{\"unit\":\"u\",\"class\":\"c\"}}") && config("{\"B\":{\"unit\":\"u\",\"class\":\"c\"}}") &&
	      box.catalog.count == 2 && entry_is(1, "B", "u", "c", true, false), "an entry moved up, its old place is free");
	check(deliver("{\"Z\":1}") && entry_is(2, "Z", "", "", false, true), "a value appended there has no unit, no class and is not in the profile");
	start();
	check(config("{\"A\":{},\"B\":{}}") && deliver("{\"B\":1}") && config("{}") && box.catalog.count == 2 && entry_is(1, "B", "", "", false, true),
	      "a delivered entry moved up, its old place is free");
	check(config("{\"C\":{}}") && entry_is(2, "C", "", "", true, false), "a profile value appended there is not delivered");

	// A different profile finds room: the old names go before the new ones come
	start();
	many_config('P', CATALOG_MAX - 1, "u");
	check(config(text) && box.catalog.count == CATALOG_MAX && many_are('P', CATALOG_MAX - 1, "u", false), "a profile that fills the catalogue");
	many_config('Q', CATALOG_MAX - 1, "v");
	check(config(text) && guards_intact(), "another profile of the same size is applied");
	check(box.catalog.count == CATALOG_MAX && box.catalog.dropped == 0 && many_are('Q', CATALOG_MAX - 1, "v", false) && find("P000") == -1 &&
	      find("P094") == -1 && battery_is_first(), "all of its values find room, the old ones are gone");
}

static void test_limits(void)
{
	// One place is the battery
	start();
	many_config('P', CATALOG_MAX - 1, "u");
	check(config(text), "95 values: applied");
	check(guards_intact(), "95 values: nothing written outside the catalogue");
	check(box.catalog.count == CATALOG_MAX && box.catalog.dropped == 0 && many_are('P', CATALOG_MAX - 1, "u", false) && battery_is_first(),
	      "95 values and the battery fill the catalogue, none dropped");

	start();
	many_config('P', CATALOG_MAX, "u");
	check(config(text), "96 values: applied");
	check(guards_intact(), "96 values: the one too many is not written behind the catalogue");
	check(box.catalog.count == CATALOG_MAX, "96 values: the catalogue holds 96 entries with the battery");
	check(box.catalog.dropped == 1, "96 values: one is counted as dropped");
	check(many_are('P', CATALOG_MAX - 1, "u", false) && find("P095") == -1, "96 values: the first 95 are kept, the last is not");
	many_config('P', CATALOG_MAX, "v");
	check(config(text) && guards_intact() && box.catalog.count == CATALOG_MAX && box.catalog.dropped == 2 && many_are('P', CATALOG_MAX - 1, "v", false),
	      "96 values again: the known ones are updated, the one too many is counted again");
	box.catalog.dropped = 65535;
	check(config(text) && box.catalog.dropped == 65536, "names without room are counted beyond 16 bit");
	box.catalog.dropped = 2;

	// Values that arrive when the catalogue is full
	check(deliver("{\"NEW\":1,\"P003\":2,\"NEW2\":3}"), "values arrive at a full catalogue");
	check(guards_intact(), "full catalogue: an unknown value is not written behind it");
	check(box.catalog.count == CATALOG_MAX && find("NEW") == -1 && find("NEW2") == -1 && box.catalog.dropped == 4,
	      "full catalogue: unknown values are dropped and counted");
	check(entry_is(4, "P003", "v", "c", true, true), "full catalogue: a known value is still marked as delivered");

	// Far more values than places, read with a room for tokens that is large enough: every member is looked at
	start();
	check(config("{\"P199\":{},\"KEEP\":{}}") && deliver("{\"KEEP\":1}") && box.catalog.count == 3, "two entries before a very long configuration");
	many_config('P', 200, "u");
	remember();
	check(!config(text) && unchanged(), "200 values need 1201 tokens: with CATALOG_TOKENS no configuration, nothing changed");
	check(catalog_apply_config(&box.catalog, text, strlen(text), large_work, 2048) && guards_intact(), "200 values with 2048 tokens: applied");
	check(box.catalog.count == CATALOG_MAX && entry_is(1, "P199", "u", "c", true, false) && entry_is(2, "KEEP", "", "", false, true) &&
	      entry_is(3, "P000", "u", "c", true, false) && entry_is(95, "P092", "u", "c", true, false) && find("P093") == -1 && find("P198") == -1,
	      "200 values: the first 93 fill the places that were left, the last one, which was known, is updated in its place");
	check(box.catalog.dropped == 106, "200 values: the 106 without room are counted");

	// CATALOG_TOKENS is 1024: a configuration with class and unit has 6 tokens per value
	start();
	many_config('P', 170, "u");
	check(config(text) && box.catalog.count == CATALOG_MAX && box.catalog.dropped == 75, "170 values need 1021 tokens: read with CATALOG_TOKENS");
	many_config('P', 171, "u");
	remember();
	check(!config(text) && unchanged(), "171 values need 1027 tokens: no configuration");

	// Exactly CATALOG_TOKENS tokens: 170 values are 1021, a member with an array of one number, which is ignored, three more
	start();
	many_config('P', 170, "u");
	strcpy(text + strlen(text) - 1, ",\"X\":[1]}");
	check(json_parse(text, strlen(text), large_work, 2048) == CATALOG_TOKENS && CATALOG_TOKENS == 1024, "170 values and an array of one number are 1024 tokens");
	check(config(text) && box.catalog.count == CATALOG_MAX && box.catalog.dropped == 75 && find("X") == -1,
	      "a configuration of exactly CATALOG_TOKENS tokens is applied");
	strcpy(text + strlen(text) - 2, ",2]}");
	remember();
	check(json_parse(text, strlen(text), large_work, 2048) == CATALOG_TOKENS + 1 && !config(text) && unchanged(),
	      "one token more than CATALOG_TOKENS: no configuration");

	// Exactly one place left
	start();
	many_config('P', CATALOG_MAX - 2, "u");
	check(config(text) && box.catalog.count == CATALOG_MAX - 1, "94 values leave one place");
	check(deliver("{\"X\":1,\"Y\":2}") && box.catalog.count == CATALOG_MAX && box.catalog.dropped == 1 && guards_intact() &&
	      (find("X") == CATALOG_MAX - 1) != (find("Y") == CATALOG_MAX - 1), "of two unknown values one finds the last place, the other is dropped");

	// 64 values at once
	start();
	{
		size_t length = 0;
		int i, known = 0;

		length += (size_t)snprintf(text + length, sizeof(text) - length, "{");
		for(i = 0; i < VALUES_MAX; i++) length += (size_t)snprintf(text + length, sizeof(text) - length, "%s\"V%02d\":%d", i ? "," : "", i, i);
		snprintf(text + length, sizeof(text) - length, "}");
		check(deliver(text) && values.count == VALUES_MAX && box.catalog.count == VALUES_MAX + 1 && box.catalog.dropped == 0, "64 values arrive: all appended");
		for(i = 0; i < VALUES_MAX; i++)
		{
			char name[8];

			snprintf(name, sizeof(name), "V%02d", i);
			if(find(name) > 0 && entry_is(find(name), name, "", "", false, true)) known++;
		}
		check(known == VALUES_MAX && battery_is_first(), "64 values arrive: each is an entry, delivered and not in the profile");
	}
}

static void test_names(void)
{
	static const char umlauts32[] = "ääääääääääääääää";
	int i;

	check(strlen(name32) == 32 && strlen(name33) == 33 && strlen(umlauts32) == 32, "the test names have 32, 33 and 32 bytes");

	start();
	snprintf(text, sizeof(text), "{\"%s\":{\"unit\":\"a\"},\"%s\":{\"unit\":\"b\"},\"B\":{\"unit\":\"c\"}}", name32, name33);
	check(config(text) && entry_is(1, name32, "a", "", true, false), "a name of 32 bytes is kept completely");
	check(box.catalog.count == 3 && find(name33) == -1 && entry_is(2, "B", "c", "", true, false), "a name of 33 bytes is no entry, the value behind it is");
	check(box.catalog.dropped == 1, "a name of 33 bytes is counted as dropped");
	check(find("M2345678901234567890123456789012") == -1, "a name of 33 bytes is not cut to 32");
	check(find("N2345678901234567890123456789012x") == -1 && find("N234567890123456789012345678901") == -1,
	      "find: a name that goes on behind the 32 bytes of an entry, or ends one byte before, is another name");
	check(config(text) && box.catalog.dropped == 2 && box.catalog.count == 3, "it is counted every time");
	snprintf(text, sizeof(text), "{\"%s\":5,\"%s\":\"x\",\"%s\":{},\"B\":{}}", name33, name33, name32);
	check(config(text) && box.catalog.dropped == 2 && box.catalog.count == 3, "a member that is no object is not counted, whatever its name");
	check(guards_intact(), "names at the limit: nothing written outside the catalogue");
	box.catalog.dropped = 65535;
	snprintf(text, sizeof(text), "{\"%s\":{}}", name33);
	check(config(text) && box.catalog.dropped == 65536, "names that are too long are counted beyond 16 bit");

	start();
	snprintf(text, sizeof(text), "{\"%s\":{\"unit\":\"a\"},\"N2345678901234567890123456789013\":{\"unit\":\"b\"}}", name32);
	check(config(text) && box.catalog.count == 3 && entry_is(1, name32, "a", "", true, false) &&
	      entry_is(2, "N2345678901234567890123456789013", "b", "", true, false), "names of 32 bytes that differ in the last byte are different entries");

	start();
	snprintf(text, sizeof(text), "{\"%s\":{},\"%sa\":{},\"a%s\":{}}", umlauts32, umlauts32, umlauts32);
	check(config(text) && box.catalog.count == 2 && entry_is(1, umlauts32, "", "", true, false) && box.catalog.dropped == 2 &&
	      find("aäääääääääääääää") == -1, "32 bytes of UTF-8 are a name, 33 are dropped and not cut");

	// The length counts with the escapes resolved
	start();
	strcpy(text, "{\"");
	for(i = 0; i < 32; i++) strcat(text, "\\u0041");
	strcat(text, "\":{},\"");
	for(i = 0; i < 33; i++) strcat(text, "\\u0042");
	strcat(text, "\":{},\"\\u00d6l\":{},\"T\\\"\\\\\":{}}");
	check(config(text) && entry_is(1, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "", "", true, false) && box.catalog.dropped == 1,
	      "32 escaped characters are a name of 32 bytes, 33 are too long");
	check(entry_is(2, "Öl", "", "", true, false) && entry_is(3, "T\"\\", "", "", true, false) && box.catalog.count == 4,
	      "a name is stored with its escapes resolved");

	start();
	check(config("{\"\\u00d6l\":{\"unit\":\"u\"},\"T\\\"\\\\\":{},\"Z\":{}}") && box.catalog.count == 4 && entry_is(1, "Öl", "u", "", true, false) &&
	      entry_is(2, "T\"\\", "", "", true, false) && entry_is(3, "Z", "", "", true, false), "three values, two of them written with escapes");
	remember();
	check(config("{\"\\u00d6l\":{\"unit\":\"u\"},\"T\\\"\\\\\":{},\"Z\":{}}") && unchanged(),
	      "the same configuration again: an entry is named by its name with the escapes resolved, nothing changes");
	check(config("{\"Z\":{},\"\\u00D6\\u006c\":{}}") && box.catalog.count == 3 && entry_is(1, "Öl", "", "", true, false) && entry_is(2, "Z", "", "", true, false),
	      "the name written with other escapes names the same entry: it keeps its place");

	start();
	check(config("{\"A\\u0000B\":{},\"\\uD83D\":{},\"C\":{}}") && box.catalog.count == 2 && entry_is(1, "C", "", "", true, false) &&
	      find("A") == -1 && find("") == -1, "a name json_text() refuses is no entry");
	check(box.catalog.dropped == 2, "names json_text() refuses are counted as dropped");

	start();
	check(config("{\"\":{\"unit\":\"u\"}}") && entry_is(1, "", "u", "", true, false), "the empty name is a name");

	start();
	check(config("{\"\xff\xfe\x80\":{},\"\xc3\":{}}") && entry_is(1, "\xff\xfe\x80", "", "", true, false) && entry_is(2, "\xc3", "", "", true, false),
	      "bytes that are no UTF-8 are passed on as they are");

	start();
	check(config("{\"AB\":{},\"A\":{},\"ABC\":{},\"b\":{}}") && box.catalog.count == 5 && entry_is(1, "AB", "", "", true, false) &&
	      entry_is(2, "A", "", "", true, false) && entry_is(3, "ABC", "", "", true, false) && entry_is(4, "b", "", "", true, false),
	      "names that begin alike are different entries");
	check(find("ABCD") == -1 && find("") == -1 && find("a") == -1 && find("B") == -1 && find("BC") == -1 && find("@BATT_V") == 0,
	      "find: unknown names, capitals matter");
	check(config("{\"ABC\":{},\"A\":{}}") && box.catalog.count == 3 && entry_is(1, "A", "", "", true, false) && entry_is(2, "ABC", "", "", true, false) &&
	      find("AB") == -1, "a name is not named by a longer or shorter one of the new profile");
}

// The configuration {"A":{"unit":raw,"class":raw},"B":{}} gives A this unit and this class and leaves the rest alone
static bool cut_is(const char *raw, const char *unit, const char *value_class)
{
	start();
	snprintf(text, sizeof(text), "{\"A\":{\"unit\":\"%s\",\"class\":\"%s\"},\"B\":{}}", raw, raw);
	if(!config(text)) return false;
	if(strlen(unit) > 11 || strlen(value_class) > 23) return false;
	return entry_is(1, "A", unit, value_class, true, false) && entry_is(2, "B", "", "", true, false) && box.catalog.count == 3 &&
	       box.catalog.dropped == 0 && guards_intact();
}

static void test_cut(void)
{
	static const struct
	{
		const char *raw;
		const char *unit;
		const char *value_class;
		const char *what;
	} cuts[] = {
		{"", "", "", "cut: an empty text"},
		{"°C", "°C", "°C", "cut: a short text with a character of two bytes"},
		{"12345678901", "12345678901", "12345678901", "cut: a unit of 11 bytes fits"},
		{"123456789012", "12345678901", "123456789012", "cut: a unit of 12 bytes loses its last byte"},
		{"12345678901234567890123", "12345678901", "12345678901234567890123", "cut: a class of 23 bytes fits"},
		{"123456789012345678901234", "12345678901", "12345678901234567890123", "cut: a class of 24 bytes loses its last byte"},
		{"1234567890123456789012345678901234567890123456789012345678901234567890", "12345678901", "12345678901234567890123",
		 "cut: a text of 70 bytes"},
		{"°C°C°C°C", "°C°C°C°", "°C°C°C°C", "cut: \"°C\" four times is 12 bytes, the last C goes"},
		{"123456789°C", "123456789°", "123456789°C", "cut: the degree sign ends at byte 11, it stays"},
		{"1234567890°C", "1234567890", "1234567890°C", "cut: the degree sign would end at byte 12, it goes as a whole"},
		{"12345678901°C", "12345678901", "12345678901°C", "cut: the degree sign begins behind the limit"},
		{"12345678€", "12345678€", "12345678€", "cut: a character of three bytes ends at byte 11"},
		{"123456789€", "123456789", "123456789€", "cut: a character of three bytes is one byte too long"},
		{"1234567890€", "1234567890", "1234567890€", "cut: a character of three bytes is two bytes too long"},
		{"1234567😀", "1234567😀", "1234567😀", "cut: a character of four bytes ends at byte 11"},
		{"12345678😀", "12345678", "12345678😀", "cut: a character of four bytes is one byte too long"},
		{"123456789😀", "123456789", "123456789😀", "cut: a character of four bytes is two bytes too long"},
		{"1234567890😀", "1234567890", "1234567890😀", "cut: a character of four bytes is three bytes too long"},
		{"123456789012345678901°", "12345678901", "123456789012345678901°", "cut: class with two bytes ending at byte 23"},
		{"1234567890123456789012°", "12345678901", "1234567890123456789012", "cut: class with two bytes ending at byte 24"},
		{"12345678901234567890€", "12345678901", "12345678901234567890€", "cut: class with three bytes ending at byte 23"},
		{"123456789012345678901€", "12345678901", "123456789012345678901", "cut: class with three bytes ending at byte 24"},
		{"1234567890123456789012€", "12345678901", "1234567890123456789012", "cut: class with three bytes ending at byte 25"},
		{"1234567890123456789😀", "12345678901", "1234567890123456789😀", "cut: class with four bytes ending at byte 23"},
		{"12345678901234567890😀", "12345678901", "12345678901234567890", "cut: class with four bytes ending at byte 24"},
		{"1234567890123456789012😀", "12345678901", "1234567890123456789012", "cut: class with four bytes ending at byte 26"},
		{"€€€€€€€€", "€€€", "€€€€€€€", "cut: only characters of three bytes"},
		{"😀😀😀😀😀😀", "😀😀", "😀😀😀😀😀", "cut: only characters of four bytes"},

		// Escapes count with what they stand for
		{"\\u00b0C", "°C", "°C", "cut: an escaped degree sign"},
		{"123456789\\u00b0C", "123456789°", "123456789°C", "cut: an escaped degree sign ends at byte 11"},
		{"1234567890\\u00b0C", "1234567890", "1234567890°C", "cut: an escaped degree sign would end at byte 12"},
		{"12345678\\u20AC", "12345678€", "12345678€", "cut: an escaped character of three bytes fits"},
		{"123456789\\u20AC", "123456789", "123456789€", "cut: an escaped character of three bytes does not fit"},
		{"1234567\\uD83D\\uDE00", "1234567😀", "1234567😀", "cut: a surrogate pair fits"},
		{"12345678\\uD83D\\uDE00", "12345678", "12345678😀", "cut: a surrogate pair does not fit, both halves go"},
		{"1234567890\\uD83D\\uDE00", "1234567890", "1234567890😀", "cut: a surrogate pair begins before the limit"},
		{"\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041", "AAAAAAAAAAA", "AAAAAAAAAAA",
		 "cut: 11 escaped characters are 11 bytes"},
		{"\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041\\u0041", "AAAAAAAAAAA", "AAAAAAAAAAAA",
		 "cut: 12 escaped characters are cut to 11"},
		{"a\\\"b\\\\c\\/d\\n", "a\"b\\c/d\n", "a\"b\\c/d\n", "cut: simple escapes are resolved"},
		{"1234567890\\n", "1234567890\n", "1234567890\n", "cut: a simple escape as byte 11 stays"},
		{"12345678901\\n", "12345678901", "12345678901\n", "cut: a simple escape as byte 12 goes"},
		{"1234567890\\\\\\\\", "1234567890\\", "1234567890\\\\", "cut: two escaped backslashes, the second goes"},
		{"123456789\\\\u0041", "123456789\\u", "123456789\\u0041", "cut: an escaped backslash before a u is no \\u escape"},

		// What json_text() does not return ends the text
		{"ab\\u0000cd", "ab", "ab", "cut: before \\u0000"},
		{"\\u0000", "", "", "cut: only \\u0000"},
		{"ab\\uD83D", "ab", "ab", "cut: before a high surrogate at the end"},
		{"ab\\uD83Dcd", "ab", "ab", "cut: before a high surrogate without its second half"},
		{"ab\\uDE00cd", "ab", "ab", "cut: before a low surrogate alone"},
		{"\\uD83Dab", "", "", "cut: half a surrogate pair at the beginning leaves nothing"},
		{"ab\\uD83D\\uDE00cd", "ab😀cd", "ab😀cd", "cut: a whole surrogate pair in the middle is kept"},
		{"123456789012\\u0000", "12345678901", "123456789012", "cut: a \\u0000 behind the limit of the unit"},

		// Bytes that are no UTF-8
		{"\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80", "", "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80",
		 "cut: 12 continuation bytes have no boundary inside, the unit is empty"},
		{"\xc3", "\xc3", "\xc3", "cut: a single first byte of a character is passed on"},
		{"1234567890\xf0\x9f\x98", "1234567890", "1234567890\xf0\x9f\x98", "cut: a character of four bytes that lacks its last byte goes as a whole"},
		{"1234567890\xc0\xaf", "1234567890", "1234567890\xc0\xaf", "cut: the byte 0xc0 begins a character although UTF-8 never uses it"},
		{"1234567890\xc1\xbf", "1234567890", "1234567890\xc1\xbf", "cut: the byte 0xc1 begins a character as well"},
		{"123456789\xe2\x80\xbf", "123456789", "123456789\xe2\x80\xbf", "cut: 0x80 and 0xbf, the first and the last continuation byte, are not cut off their character"},
		{"1234567890\x7f\x7f", "1234567890\x7f", "1234567890\x7f\x7f", "cut: 0x7f is a character of its own"},
	};
	static const char *const escaped_what[] = {
		"cut: 22 escaped characters, 132 bytes in the text, are a class of 22 bytes",
		"cut: 23 escaped characters, 138 bytes in the text, fit a class completely",
		"cut: 24 escaped characters are cut to a class of 23 bytes",
	};
	size_t i, at;

	for(i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++) check(cut_is(cuts[i].raw, cuts[i].unit, cuts[i].value_class), cuts[i].what);

	// The limit counts what the text stands for, however long it is written
	for(i = 22; i <= 24; i++)
	{
		char raw[160] = "";
		char value_class[32] = "";
		size_t n;

		for(n = 0; n < i; n++) strcat(raw, "\\u0041");
		for(n = 0; n < i && n < 23; n++) strcat(value_class, "A");
		check(strlen(raw) == 6 * i && cut_is(raw, "AAAAAAAAAAA", value_class), escaped_what[i - 22]);
	}

	// Texts the size of a whole answer. Put together by hand: gcc refuses a snprintf() of a text that might not fit.
	start();
	strcpy(text, "{\"A\":{\"unit\":\"");
	at = strlen(text);
	memset(text + at, 'x', 20000);
	strcpy(text + at + 20000, "\",\"class\":\"");
	at = strlen(text);
	memset(text + at, 'x', 20000);
	strcpy(text + at + 20000, "\"}}");
	check(strlen(text) == 40028 && config(text) && entry_is(1, "A", "xxxxxxxxxxx", "xxxxxxxxxxxxxxxxxxxxxxx", true, false) && guards_intact(),
	      "cut: texts of 20000 bytes");
}

/*
 * The cut written a second time as a simple model: a text is a row of characters, each with the bytes it
 * stands for; the cut text is every character from the beginning as long as the sum of bytes fits.
 */

static uint32_t random_state;

static uint32_t rnd(uint32_t below)
{
	random_state = random_state * 1664525u + 1013904223u;
	return (random_state >> 8) % below;
}

static void test_cut_model(void)
{
	static const struct
	{
		const char *bytes;      // what the character is in the catalogue
		const char *raw;        // how it stands in the JSON text unescaped, NULL if it must be escaped
		const char *escaped;
	} characters[] = {
		{"a", "a", "\\u0061"}, {"Z", "Z", "\\u005a"}, {"\"", NULL, "\\\""}, {"\\", NULL, "\\\\"}, {"/", "/", "\\/"},
		{"\n", NULL, "\\n"}, {"\t", NULL, "\\u0009"}, {"u", "u", "\\u0075"}, {"°", "°", "\\u00b0"}, {"ß", "ß", "\\u00DF"},
		{"€", "€", "\\u20AC"}, {"ₓ", "ₓ", "\\u2093"}, {"😀", "😀", "\\uD83D\\uDE00"}, {"𝄞", "𝄞", "\\ud834\\udd1e"},
	};
	const uint32_t kinds = sizeof(characters) / sizeof(characters[0]);
	int wrong = 0, cut_units = 0, cut_classes = 0, whole = 0;
	int round;

	random_state = 906;
	for(round = 0; round < 3000; round++)
	{
		char raw[512] = "";
		char unit[64] = "";
		char value_class[64] = "";
		bool unit_open = true, class_open = true;
		uint32_t count = rnd(16);
		uint32_t i;

		for(i = 0; i < count; i++)
		{
			uint32_t kind = rnd(kinds);
			bool escape = characters[kind].raw == NULL || rnd(3) == 0;

			strcat(raw, escape ? characters[kind].escaped : characters[kind].raw);
			if(unit_open && strlen(unit) + strlen(characters[kind].bytes) <= 11) strcat(unit, characters[kind].bytes);
			else unit_open = false;
			if(class_open && strlen(value_class) + strlen(characters[kind].bytes) <= 23) strcat(value_class, characters[kind].bytes);
			else class_open = false;
		}
		if(!unit_open) cut_units++;
		if(!class_open) cut_classes++;
		if(class_open) whole++;

		start();
		snprintf(text, sizeof(text), "{\"A\":{\"class\":\"%s\",\"unit\":\"%s\"}}", raw, raw);
		if(!config(text) || !entry_is(1, "A", unit, value_class, true, false) || !guards_intact())
		{
			if(wrong++ < 5) printf("  %s: unit \"%s\" (model \"%s\"), class \"%s\" (model \"%s\")\n", raw, box.catalog.entries[1].unit, unit,
			                       box.catalog.entries[1].value_class, value_class);
		}
	}
	check(wrong == 0, "cut model: 3000 random texts of raw and escaped characters are cut as the rule says");
	printf("  units cut %d, classes cut %d, texts that fit %d\n", cut_units, cut_classes, whole);
	check(cut_units > 500 && cut_classes > 300 && whole > 500, "cut model: the texts contain cut units, cut classes and texts that fit");
}

static void test_note_values(void)
{
	start();
	snprintf(text, sizeof(text), "{\"A\":{\"unit\":\"u\",\"class\":\"c\"},\"B\":{},\"C\":{},\"%s\":{}}", name33);
	check(config(text) && box.catalog.count == 4 && box.catalog.dropped == 1, "three values of a profile and a dropped one");
	remember();
	check(deliver("{}") && unchanged(), "no values arrive: nothing changes, dropped included");
	check(deliver("{\"C\":1,\"Z\":\"on\",\"A\":2.5,\"Y\":\"off\"}") && box.catalog.count == 6, "two known and two unknown values arrive");
	check(entry_is(1, "A", "u", "c", true, true) && entry_is(3, "C", "", "", true, true), "known values are marked as delivered and keep their texts");
	check(entry_is(2, "B", "", "", true, false), "a value that did not arrive is not delivered");
	check(find("Z") >= 4 && entry_is(find("Z"), "Z", "", "", false, true) && find("Y") >= 4 && entry_is(find("Y"), "Y", "", "", false, true),
	      "unknown values are appended: delivered, not in the profile, no unit, no class");
	check(battery_is_first() && box.catalog.dropped == 1 && guards_intact(), "the battery entry is not touched, nothing more is dropped");
	remember();
	check(deliver("{\"C\":5,\"Z\":\"off\"}") && unchanged(), "the same values again change nothing");

	// Every value that is there counts, however old it is
	start();
	values_init(&values);
	check(values_apply(&values, "{\"B\":2,\"A\":1}", 13, 1, 1000, values_work, VALUES_TOKENS) == VALUES_RENEWED &&
	      values_apply(&values, "{\"B\":3}", 7, 2, 60000, values_work, VALUES_TOKENS) == VALUES_RENEWED &&
	      values_age(values_find(&values, "A"), 60000) == VALUE_AGE_GONE, "a value that has grown old is still there");
	catalog_note_values(&box.catalog, &values);
	check(box.catalog.count == 3 && find("A") > 0 && entry_is(find("A"), "A", "", "", false, true) && find("B") > 0 &&
	      entry_is(find("B"), "B", "", "", false, true), "old values are noted as well");

	// Values that were cleared are not there
	start();
	values_init(&values);
	check(values_apply(&values, "{\"A\":1}", 7, -1, 1, values_work, VALUES_TOKENS) == VALUES_RENEWED, "a value before clearing");
	values_clear(&values);
	remember();
	catalog_note_values(&box.catalog, &values);
	check(unchanged(), "cleared values are not noted");
}

static void test_to_json(void)
{
	static const char fresh[] = "{\"@BATT_V\":{\"unit\":\"V\",\"class\":\"\",\"profile\":false,\"delivered\":false}}";
	static const char small[] = "{\"@BATT_V\":{\"unit\":\"V\",\"class\":\"\",\"profile\":false,\"delivered\":false},"
	                            "\"A\":{\"unit\":\"°C\",\"class\":\"temperature\",\"profile\":true,\"delivered\":true},"
	                            "\"C\":{\"unit\":\"g\",\"class\":\"weight\",\"profile\":true,\"delivered\":false},"
	                            "\"B\":{\"unit\":\"\",\"class\":\"\",\"profile\":false,\"delivered\":true}}";
	static const char escaped[] = "{\"@BATT_V\":{\"unit\":\"V\",\"class\":\"\",\"profile\":false,\"delivered\":false},"
	                              "\"Q\\\"B\\\\S\\u0001\\u001f\\u000a\\u0009\x7f \xc3\xa4\":{\"unit\":\"\\\"\",\"class\":\"\\\\\",\"profile\":true,\"delivered\":false},"
	                              "\"\":{\"unit\":\"\\u0010\\u000f/\",\"class\":\"\xff\x80\",\"profile\":true,\"delivered\":false}}";
	const int length = (int)strlen(small);
	bool all = true;
	int size, result, count;

	start();
	memset(out, 0x7E, sizeof(out));
	result = catalog_to_json(&box.catalog, out, sizeof(out));
	check(strcmp(out, fresh) == 0, "to_json: a fresh catalogue is the battery entry, without whitespace");
	check(result == (int)strlen(fresh) && result == 69, "to_json: returns the length of the text");

	check(config("{\"A\":{\"class\":\"temperature\",\"unit\":\"°C\"},\"C\":{\"class\":\"weight\",\"unit\":\"g\"}}") && deliver("{\"A\":1,\"B\":2}") &&
	      box.catalog.count == 4, "a catalogue with four entries: two of a profile, one of them delivered, and one that only arrived");
	remember();
	memset(out, 0x7E, sizeof(out));
	result = catalog_to_json(&box.catalog, out, sizeof(out));
	check(strcmp(out, small) == 0, "to_json: entries in catalogue order with unit, class, profile and delivered");
	check(result == length, "to_json: returns the length of the text of four entries");
	check(unchanged(), "to_json: does not change the catalogue");

	// Every size up to one that is larger than needed
	for(size = 0; size <= length + 3; size++)
	{
		int i;

		memset(out, 0x7E, sizeof(out));
		result = catalog_to_json(&box.catalog, out, (size_t)size);
		for(i = size; i < length + 64; i++)
		{
			if(out[i] != 0x7E) all = false;
		}
		if(size <= length)
		{
			if(result != -1 || (size > 0 && out[0] != '\0')) all = false;
		}
		else if(result != length || strcmp(out, small) != 0) all = false;
	}
	check(all, "to_json: with every size from 0 to more than needed: the text or -1 and an empty string, nothing behind the size");

	memset(out, 0x7E, sizeof(out));
	check(catalog_to_json(&box.catalog, out, (size_t)length + 1) == length && strcmp(out, small) == 0 && out[length + 1] == 0x7E,
	      "to_json: fits exactly with its terminating zero");
	memset(out, 0x7E, sizeof(out));
	check(catalog_to_json(&box.catalog, out, (size_t)length) == -1, "to_json: one byte too small: -1");
	check(out[0] == '\0', "to_json: one byte too small: out is an empty string");
	check(out[length] == 0x7E, "to_json: one byte too small: nothing written behind the size");
	memset(out, 0x7E, sizeof(out));
	check(catalog_to_json(&box.catalog, out, 1) == -1 && out[0] == '\0' && out[1] == 0x7E, "to_json: size 1: -1 and an empty string");
	memset(out, 0x7E, sizeof(out));
	check(catalog_to_json(&box.catalog, out, 0) == -1 && out[0] == 0x7E, "to_json: size 0: -1 and nothing written");

	// Bytes behind the end of a text and entries behind the last are not written
	memset(box.catalog.entries[1].name + 2, 'x', sizeof(box.catalog.entries[1].name) - 2);
	memset(box.catalog.entries[1].unit + 4, 'y', sizeof(box.catalog.entries[1].unit) - 4);
	memset(box.catalog.entries[1].value_class + 12, 'z', sizeof(box.catalog.entries[1].value_class) - 12);
	memset(&box.catalog.entries[4], 0x01, sizeof(box.catalog.entries[4]));
	memset(out, 0x7E, sizeof(out));
	check(catalog_to_json(&box.catalog, out, sizeof(out)) == length && strcmp(out, small) == 0,
	      "to_json: bytes behind the end of a text and entries behind the last are not written");

	// No entry at all, which no catalogue made by this module has
	memset(&box, GUARD, sizeof(box));
	memset(&box.catalog, 0, sizeof(box.catalog));
	memset(out, 0x7E, sizeof(out));
	check(catalog_to_json(&box.catalog, out, 3) == 2 && strcmp(out, "{}") == 0 && out[3] == 0x7E, "to_json: no entries give {}");
	check(stored(out) && box.catalog.count == 1 && battery_is_first(), "from_json: and {} gives a fresh catalogue");

	// Characters that JSON does not take as they are
	start();
	box.catalog.count = 3;
	memset(&box.catalog.entries[1], 0, 2 * sizeof(box.catalog.entries[0]));
	strcpy(box.catalog.entries[1].name, "Q\"B\\S\x01\x1f\n\t\x7f \xc3\xa4");
	strcpy(box.catalog.entries[1].unit, "\"");
	strcpy(box.catalog.entries[1].value_class, "\\");
	box.catalog.entries[1].in_profile = true;
	strcpy(box.catalog.entries[2].unit, "\x10\x0f/");
	strcpy(box.catalog.entries[2].value_class, "\xff\x80");
	box.catalog.entries[2].in_profile = true;
	memset(out, 0x7E, sizeof(out));
	result = catalog_to_json(&box.catalog, out, sizeof(out));
	check(strcmp(out, escaped) == 0 && result == (int)strlen(escaped),
	      "to_json: quote and backslash get a backslash, bytes below 0x20 become \\u00xx, 0x7f and bytes above pass");
	count = json_parse(out, strlen(out), work, CATALOG_TOKENS);
	check(count == 31 && json_text_is(out, &work[11], "Q\"B\\S\x01\x1f\n\t\x7f \xc3\xa4") && json_text_is(out, &work[14], "\"") &&
	      json_text_is(out, &work[16], "\\") && json_text_is(out, &work[21], "") && json_text_is(out, &work[24], "\x10\x0f/") &&
	      json_text_is(out, &work[26], "\xff\x80"), "to_json: the JSON reader gets the same bytes back");

	// The largest catalogue
	start();
	{
		int i;

		box.catalog.count = CATALOG_MAX;
		for(i = 1; i < CATALOG_MAX; i++)
		{
			catalog_entry_t *entry = box.catalog.entries + i;

			memset(entry, 0, sizeof(*entry));
			snprintf(entry->name, sizeof(entry->name), "%02d\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
			         "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e", i);
			strcpy(entry->unit, "\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01");
			strcpy(entry->value_class, "\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f");
			entry->delivered = true;
		}
	}
	memset(out, 0x7E, sizeof(out));
	result = catalog_to_json(&box.catalog, out, sizeof(out));
	// Two braces, the battery entry with 67 bytes, and 95 times a comma, a name of 2 + 2 + 30 * 6 bytes, a unit of
	// 2 + 11 * 6, a class of 2 + 23 * 6 and 52 bytes of keys, true and false
	check(result == 2 + 67 + 95 * (1 + 184 + 68 + 140 + 52) && result == 42344 && (int)strlen(out) == result && out[result + 1] == 0x7E,
	      "to_json: the largest catalogue, every byte escaped, has 42344 bytes");
	check(json_parse(out, strlen(out), large_work, 2048) == 1 + 10 * CATALOG_MAX && 1 + 10 * CATALOG_MAX <= CATALOG_TOKENS,
	      "to_json: it is JSON of 10 tokens per entry, CATALOG_TOKENS are enough to read it");
	check(catalog_to_json(&box.catalog, out, (size_t)result) == -1 && out[0] == '\0' && catalog_to_json(&box.catalog, out, (size_t)result + 1) == result,
	      "to_json: the largest catalogue needs exactly its length and one byte");

	// And back: every byte of every name, unit and class was written as six
	remember();
	check(stored(out) && guards_intact() && box.catalog.count == CATALOG_MAX && box.catalog.dropped == 0 && battery_is_first(),
	      "from_json: the largest catalogue is read again with CATALOG_TOKENS tokens");
	all = true;
	for(count = 1; count < CATALOG_MAX; count++)
	{
		const catalog_entry_t *entry = snapshot.catalog.entries + count;

		if(strlen(entry->name) != 32 || strlen(entry->unit) != 11 || strlen(entry->value_class) != 23 ||
		   !entry_is(count, entry->name, entry->unit, entry->value_class, false, false)) all = false;
	}
	check(all, "from_json: every name of 32, unit of 11 and class of 23 escaped bytes is whole again");
}

static void test_from_json(void)
{
	static const struct
	{
		const char *json;
		const char *what;
	} refused[] = {
		// A scalar first: it has no members, so even a reader that goes on does it in an orderly way
		{"5", "from_json refuses: a number"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true}", "from_json refuses: an object that is not closed"},
		{"[]", "from_json refuses: an array"},
		{"[{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true}}]", "from_json refuses: a catalogue inside an array"},
		{"\"x\"", "from_json refuses: a text"},
		{"null", "from_json refuses: null"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true}}x", "from_json refuses: text behind the object"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true},}", "from_json refuses: a comma before the end"},
		{"", "from_json refuses: an empty text"},
		{" ", "from_json refuses: a space"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true,\"x\":[[[[[[[1]]]]]]]}}", "from_json refuses: nested deeper than the reader follows"},
		{"{\"N\":5}", "from_json refuses: a member that is a number"},
		{"{\"N\":\"x\"}", "from_json refuses: a member that is a text"},
		{"{\"N\":null}", "from_json refuses: a member that is null"},
		{"{\"N\":[{\"unit\":\"u\",\"class\":\"c\",\"profile\":true}]}", "from_json refuses: a member that is an array"},
		{"{\"N\":{}}", "from_json refuses: an entry without anything"},
		{"{\"N\":{\"class\":\"c\",\"profile\":true}}", "from_json refuses: an entry without unit"},
		{"{\"N\":{\"unit\":5,\"class\":\"c\",\"profile\":true}}", "from_json refuses: a unit that is a number"},
		{"{\"N\":{\"unit\":null,\"class\":\"c\",\"profile\":true}}", "from_json refuses: a unit that is null"},
		{"{\"N\":{\"unit\":true,\"class\":\"c\",\"profile\":true}}", "from_json refuses: a unit that is true"},
		{"{\"N\":{\"unit\":\"u\",\"profile\":true}}", "from_json refuses: an entry without class"},
		{"{\"N\":{\"unit\":\"u\",\"class\":7,\"profile\":true}}", "from_json refuses: a class that is a number"},
		{"{\"N\":{\"unit\":\"u\",\"class\":false,\"profile\":true}}", "from_json refuses: a class that is false"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\"}}", "from_json refuses: an entry without profile"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":\"true\"}}", "from_json refuses: a profile that is a text"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":1}}", "from_json refuses: a profile that is a number"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":null}}", "from_json refuses: a profile that is null"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"delivered\":true}}", "from_json refuses: delivered instead of profile"},
		{"{\"N\":{\"unit\":5,\"class\":\"c\",\"profile\":true,\"delivered\":false,\"x\":1,\"y\":2}}", "from_json refuses: a unit that is a number among six members"},
		{"{\"N\":{\"x\":1,\"y\":2,\"delivered\":false,\"unit\":\"u\",\"class\":[],\"profile\":true}}", "from_json refuses: a class that is an array among six members"},
		{"{\"N\":{\"x\":1,\"y\":2,\"delivered\":false,\"unit\":\"u\",\"class\":\"c\",\"profile\":0}}", "from_json refuses: a profile that is a number among six members"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true},\"M\":{\"unit\":\"u\",\"class\":\"c\"}}", "from_json refuses: a good entry followed by one without profile"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true},\"M\":7}", "from_json refuses: a good entry followed by a number"},
		{"{\"M\":{\"class\":\"c\",\"profile\":false},\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true}}", "from_json refuses: an entry without unit followed by a good one"},
		{"{\"N\":{\"x\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true}}}", "from_json refuses: unit, class and profile one level too deep"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true},\"M\":{\"unit\":5,\"class\":\"c\",\"profile\":true}}", "from_json refuses: a good entry followed by one whose unit is a number"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true},\"M\":{\"unit\":null,\"class\":\"c\",\"profile\":true}}", "from_json refuses: a good entry followed by one whose unit is null"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true},\"M\":{\"unit\":\"u\",\"class\":null,\"profile\":true}}", "from_json refuses: a good entry followed by one whose class is null"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true},\"M\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":\"x\"}}", "from_json refuses: a good entry followed by one whose profile is a text"},
		{"{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true},\"M\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":null}}", "from_json refuses: a good entry followed by one whose profile is null"},
	};
	static const char one[] = "{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true}}";
	static char many_y[301];
	size_t i, cut;
	int good;
	int wrong = 0, fresh_wrong = 0, cut_wrong = 0;

	// The stored catalogue written by hand
	start();
	check(read_fixture("fixtures/catalog_stored.json", text, sizeof(text)), "fixture stored catalogue");
	check(stored(text), "from_json: the stored catalogue is read");
	check(box.catalog.count == 6 && box.catalog.dropped == 0, "from_json: six entries, none dropped");
	check(battery_is_first(), "from_json: the battery entry");
	check(entry_is(1, "ENGINE_RPM", "RPM", "frequency", true, false), "from_json: an entry of the profile with unit and class");
	check(entry_is(2, "COOLANT_TMP", "°C", "temperature", true, false), "from_json: a unit with a character of two bytes");
	check(entry_is(3, "LAMBDA", "", "none", true, false), "from_json: an entry without unit");
	check(entry_is(4, "GLOW_ACTIVE", "", "", false, false), "from_json: an entry that is not in the profile");
	check(entry_is(5, "OIL_LEVEL", "mm", "distance", true, false), "from_json: the last entry, in the order of the text");
	check(guards_intact(), "from_json: writes nothing outside the catalogue");
	check(catalog_checksum(&box.catalog) == 0x44F6632Eu, "from_json: the checksum of the stored catalogue");
	check(catalog_to_json(&box.catalog, out, sizeof(out)) == (int)strlen(text) && strcmp(out, text) == 0,
	      "to_json: writes the stored catalogue as the fixture has it, byte for byte");

	// The catalogue that was there before is replaced
	start();
	check(config("{\"X\":{\"unit\":\"x\"},\"Y\":{}}") && deliver("{\"Y\":1,\"Z\":2}") && box.catalog.count == 4, "a catalogue with other entries");
	snprintf(text, sizeof(text), "{\"%s\":{},\"%s\":{}}", name33, name33);
	check(config(text) && box.catalog.dropped == 2 && box.catalog.count == 3, "and two dropped names");
	check(stored("{\"A\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true,\"delivered\":true}}"), "from_json: a text without the battery entry is read");
	check(box.catalog.count == 2 && battery_is_first() && entry_is(1, "A", "u", "c", true, false) && find("X") == -1 && find("Y") == -1 && find("Z") == -1,
	      "from_json: the result is a fresh catalogue with the entries of the text, the old entries are gone");
	check(box.catalog.dropped == 0, "from_json: dropped starts again");
	check(!box.catalog.entries[1].delivered, "from_json: delivered is not restored");

	start();
	check(config("{\"@BATT_V\":{\"unit\":\"mV\",\"class\":\"voltage\"}}") && deliver("{\"@BATT_V\":12}") && entry_is(0, "@BATT_V", "mV", "voltage", true, true),
	      "a battery entry that a profile changed");
	check(stored("{\"A\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true}}") && battery_is_first() && entry_is(1, "A", "u", "c", true, false) && box.catalog.count == 2,
	      "from_json: a text without the battery entry gives the battery entry of a fresh catalogue");

	start();
	many_config('P', CATALOG_MAX, "u");
	check(config(text) && deliver("{\"P001\":1,\"P094\":2}") && box.catalog.count == CATALOG_MAX && box.catalog.dropped == 1, "a full catalogue with a dropped name");
	check(stored("{\"A\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true}}") && box.catalog.count == 2 && battery_is_first() &&
	      entry_is(1, "A", "u", "c", true, false) && box.catalog.dropped == 0 && find("P001") == -1 && find("P094") == -1,
	      "from_json: a text with one entry replaces a full catalogue");

	start();
	snprintf(text, sizeof(text), "{\"%s\":{}}", name33);
	check(config(text) && box.catalog.count == 1 && box.catalog.dropped == 1, "a catalogue with nothing but the battery and a dropped name");
	check(stored("{}") && box.catalog.count == 1 && box.catalog.dropped == 0 && battery_is_first(), "from_json: an empty object is read there as well, dropped starts again");

	start();
	check(stored("{}") && box.catalog.count == 1 && battery_is_first(), "from_json: an empty object gives a fresh catalogue");
	check(config("{\"A\":{}}") && deliver("{\"A\":1}") && stored("{}") && box.catalog.count == 1 && battery_is_first() && find("A") == -1,
	      "from_json: an empty object replaces a catalogue with entries");

	start();
	check(stored("{\"A\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":false,\"delivered\":true},"
	             "\"@BATT_V\":{\"unit\":\"mV\",\"class\":\"voltage\",\"profile\":true,\"delivered\":true},"
	             "\"B\":{\"profile\":true,\"class\":\"d\",\"unit\":\"v\"}}"), "from_json: the battery entry in the middle of the text");
	check(box.catalog.count == 3 && entry_is(0, "@BATT_V", "mV", "voltage", true, false), "from_json: the battery entry stays the first and takes unit, class and profile of the text");
	check(entry_is(1, "A", "u", "c", false, false), "from_json: profile false is not in the profile");
	check(entry_is(2, "B", "v", "d", true, false), "from_json: the members of an entry in another order, without delivered");

	start();
	check(stored("{\"A\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true,\"delivered\":5,\"x\":[1,{\"unit\":7}],\"later\":null}}") &&
	      entry_is(1, "A", "u", "c", true, false), "from_json: delivered of any kind and members it does not know are ignored");
	check(stored("{\"A\":{\"unit\":\"1\",\"class\":\"1\",\"profile\":true},\"B\":{\"unit\":\"\",\"class\":\"\",\"profile\":false},"
	             "\"A\":{\"unit\":\"2\",\"class\":\"2\",\"profile\":false}}") && box.catalog.count == 3 && entry_is(1, "A", "2", "2", false, false) &&
	      entry_is(2, "B", "", "", false, false), "from_json: a name that comes twice is one entry, the second member updates it");

	// Names and texts are taken over like those of a configuration
	start();
	snprintf(text, sizeof(text), "{\"%s\":{\"unit\":\"123456789012\",\"class\":\"1234567890123456789012°\",\"profile\":true},"
	         "\"%s\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true},\"A\\u0000\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true},"
	         "\"B\":{\"unit\":\"1234567890\\u00b0C\",\"class\":\"ab\\u0000c\",\"profile\":true}}", name32, name33);
	check(stored(text) && box.catalog.count == 3, "from_json: a text with names and texts that are too long is read");
	check(entry_is(1, name32, "12345678901", "1234567890123456789012", true, false), "from_json: a name of 32 bytes is kept, longer texts are cut at a character boundary");
	check(find(name33) == -1 && find("A") == -1 && box.catalog.dropped == 2, "from_json: a name of 33 bytes and one json_text() refuses are dropped and counted");
	check(entry_is(2, "B", "1234567890", "ab", true, false) && guards_intact(), "from_json: escaped texts are cut like those of a configuration");

	// Refused texts change nothing
	start();
	check(config("{\"K\":{\"unit\":\"u\",\"class\":\"c\"},\"L\":{}}") && deliver("{\"L\":1}") && box.catalog.count == 3, "a catalogue before the refused texts");
	remember();
	for(i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) check(!stored(refused[i].json) && unchanged(), refused[i].what);
	check(read_fixture("../../tools/w906/fixtures/car_config_w906.json", text, sizeof(text)) && !stored(text) && unchanged(),
	      "from_json refuses: a configuration of the adapter is no stored catalogue");

	// The same when the display starts: nothing of a refused text gets into the fresh catalogue
	for(i = 0; i < sizeof(refused) / sizeof(refused[0]); i++)
	{
		start();
		remember();
		if(stored(refused[i].json) || !unchanged())
		{
			printf("  %s\n", refused[i].what);
			fresh_wrong++;
		}
	}
	check(fresh_wrong == 0, "from_json refuses every one of these texts for a fresh catalogue as well, nothing changed");

	// What the flash gives back after a write that was interrupted: the stored catalogue cut off anywhere
	check(read_fixture("fixtures/catalog_stored.json", text, sizeof(text)), "fixture stored catalogue, to be cut off");
	cut = strlen(text);
	for(i = 0; i < cut; i++)
	{
		start();
		remember();
		if(catalog_from_json(&box.catalog, text, i, work, CATALOG_TOKENS) || !unchanged()) cut_wrong++;
	}
	check(cut > 400 && cut_wrong == 0, "from_json refuses the stored catalogue cut off after any number of its bytes, nothing changed");
	start();
	check(config("{\"K\":{\"unit\":\"u\",\"class\":\"c\"},\"L\":{}}") && deliver("{\"L\":1}") && box.catalog.count == 3, "the catalogue before the refused texts again");
	remember();
	check(strlen(one) == 45 && !catalog_from_json(&box.catalog, one, 44, work, CATALOG_TOKENS) && unchanged(),
	      "from_json refuses: a length that ends before the last brace");
	check(catalog_from_json(&box.catalog, "{\"N\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true}}x", 45, work, CATALOG_TOKENS) &&
	      box.catalog.count == 2 && entry_is(1, "N", "u", "c", true, false), "from_json: bytes behind the given length are not read");

	// Room for the tokens: one entry without delivered has 1 + 8 of them
	start();
	check(catalog_from_json(&box.catalog, one, strlen(one), work, 9) && entry_is(1, "N", "u", "c", true, false),
	      "from_json: exactly as many tokens as the text needs");
	start();
	memset(&work[8], 0x5A, 2 * sizeof(work[0]));
	remember();
	check(!catalog_from_json(&box.catalog, one, strlen(one), work, 8) && unchanged(),
	      "from_json refuses: one token less than the text needs, nothing changed");
	check(work[8].start == 0x5A5A5A5A && work[9].start == 0x5A5A5A5A,
	      "from_json: one token less than the text needs: nothing written behind the room of the reader");

	// More names than places
	start();
	many_stored('S', CATALOG_MAX - 1);
	check(stored(text) && guards_intact() && box.catalog.count == CATALOG_MAX && box.catalog.dropped == 0 && many_are('S', CATALOG_MAX - 1, "u", false) &&
	      battery_is_first(), "from_json: 95 entries without the battery fill the catalogue");
	many_stored('S', CATALOG_MAX);
	check(stored(text), "from_json: 96 entries without the battery are read");
	check(guards_intact(), "from_json: the one too many is not written behind the catalogue");
	check(box.catalog.count == CATALOG_MAX && box.catalog.dropped == 1 && many_are('S', CATALOG_MAX - 1, "u", false) && find("S095") == -1,
	      "from_json: the first 95 are kept, the last is dropped and counted");
	many_stored('S', CATALOG_MAX + 2);
	check(stored(text) && guards_intact() && box.catalog.count == CATALOG_MAX && box.catalog.dropped == 3 && many_are('S', CATALOG_MAX - 1, "u", false),
	      "from_json: 98 entries are read as well, three are dropped and counted");
	many_stored('S', 110);
	remember();
	check(!stored(text) && unchanged(), "from_json refuses: 110 entries need 1101 tokens, more than CATALOG_TOKENS");
	check(catalog_from_json(&box.catalog, text, strlen(text), large_work, 2048) && guards_intact() && box.catalog.count == CATALOG_MAX &&
	      box.catalog.dropped == 15 && many_are('S', CATALOG_MAX - 1, "u", false), "from_json: 110 entries with 2048 tokens are read, 15 dropped");

	// Exactly CATALOG_TOKENS tokens: 101 entries are 1011, an entry with a member it does not know, an array of three numbers, 13 more
	start();
	many_stored('S', 101);
	strcpy(text + strlen(text) - 1, ",\"X\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true,\"x\":[1,2,3]}}");
	check(json_parse(text, strlen(text), large_work, 2048) == CATALOG_TOKENS, "101 entries and one with an array of three numbers are 1024 tokens");
	check(stored(text) && box.catalog.count == CATALOG_MAX && box.catalog.dropped == 7 && many_are('S', CATALOG_MAX - 1, "u", false),
	      "from_json: a text of exactly CATALOG_TOKENS tokens is read");
	strcpy(text + strlen(text) - 3, ",4]}}");
	remember();
	check(json_parse(text, strlen(text), large_work, 2048) == CATALOG_TOKENS + 1 && !stored(text) && unchanged(),
	      "from_json refuses: one token more than CATALOG_TOKENS");

	// A member that is no entry is found wherever it stands, also behind more entries than there are places
	for(good = CATALOG_MAX - 2; good <= CATALOG_MAX + 1; good++)
	{
		start();
		many_stored('S', good);
		strcpy(text + strlen(text) - 1, ",\"BAD\":{\"unit\":\"u\",\"class\":\"c\"}}");
		remember();
		if(stored(text) || !unchanged()) wrong++;
	}
	check(wrong == 0, "from_json refuses: an entry without profile behind 94, 95, 96 or 97 good ones, nothing changed");

	// Texts and names far beyond their limits are cut and dropped, not refused
	start();
	memset(many_y, 'y', sizeof(many_y) - 1);
	snprintf(text, sizeof(text), "{\"A\":{\"unit\":\"%s\",\"class\":\"%s\",\"profile\":true},\"%s\":{\"unit\":\"u\",\"class\":\"c\",\"profile\":true},"
	         "\"B\":{\"unit\":\"\",\"class\":\"\",\"profile\":false}}", many_y, many_y, many_y);
	check(stored(text) && box.catalog.count == 3 && entry_is(1, "A", "yyyyyyyyyyy", "yyyyyyyyyyyyyyyyyyyyyyy", true, false) &&
	      entry_is(2, "B", "", "", false, false) && box.catalog.dropped == 1 && guards_intact(),
	      "from_json: a unit, a class and a name of 300 bytes: the texts are cut, the name is dropped, the rest is read");
	snprintf(text, sizeof(text), "{\"%s\":{\"unit\":\"%s\"},\"B\":{\"class\":\"%s\"},\"A\":{}}", many_y, many_y, many_y);
	check(config(text) && box.catalog.count == 3 && entry_is(1, "A", "", "", true, false) && entry_is(2, "B", "", "yyyyyyyyyyyyyyyyyyyyyyy", true, false) &&
	      box.catalog.dropped == 2 && guards_intact(),
	      "a configuration with a name of 300 bytes in front: the entries behind it are still named and keep their places");
}

// A catalogue of pseudo-random entries, written into the struct directly
static void random_text(char *field, uint32_t longest)
{
	uint32_t length = rnd(4) == 0 ? longest : rnd(longest + 1);
	uint32_t i;

	for(i = 0; i < length; i++)
	{
		uint32_t kind = rnd(12);

		field[i] = (char)(kind < 6 ? 'a' + rnd(26) : kind < 8 ? 1 + rnd(31) : kind == 8 ? '"' : kind == 9 ? '\\' : kind == 10 ? 0x7f : 0x80 + rnd(128));
	}
	field[length] = '\0';
}

static void random_catalog(catalog_t *catalog)
{
	int count = rnd(8) == 0 ? CATALOG_MAX : 1 + (int)rnd(CATALOG_MAX);
	int i, j;

	memset(catalog, 0, sizeof(*catalog));
	strcpy(catalog->entries[0].name, "@BATT_V");
	for(i = 0; i < count; i++)
	{
		catalog_entry_t *entry = catalog->entries + i;
		bool taken = true;

		while(i > 0 && taken)
		{
			random_text(entry->name, 32);
			taken = false;
			for(j = 0; j < i; j++)
			{
				if(strcmp(catalog->entries[j].name, entry->name) == 0) taken = true;
			}
		}
		random_text(entry->unit, 11);
		random_text(entry->value_class, 23);
		entry->in_profile = rnd(2) == 1;
		entry->delivered = rnd(2) == 1;
	}
	catalog->count = count;
	catalog->dropped = rnd(5);
}

static void test_round_trip(void)
{
	int wrong = 0, full = 0;
	int round, i, length, tokens;

	// The catalogue of the W906 with some values delivered
	start();
	check(read_fixture("../../tools/w906/fixtures/car_config_w906.json", text, sizeof(text)) && config(text) &&
	      deliver("{\"ENGINE_RPM\":800,\"COOLANT_TMP\":80.5,\"GLOW_ACTIVE\":\"off\"}") && box.catalog.count == 37, "round trip: the catalogue of the W906 and a value more");
	length = catalog_to_json(&box.catalog, out, sizeof(out));
	check(length > 0 && json_parse(out, (size_t)length, large_work, 2048) == 1 + 10 * 37, "round trip: written as JSON of 10 tokens per entry");
	memset(&other, GUARD, sizeof(other));
	catalog_init(&other.catalog);
	check(catalog_from_json(&other.catalog, out, (size_t)length, work, CATALOG_TOKENS), "round trip: read again");
	check(other.catalog.count == 37 && other.catalog.dropped == 0 && w906_entries_are(&other.catalog, true, false) &&
	      entry_of_is(&other.catalog, 0, "@BATT_V", "V", "", false, false) && entry_of_is(&other.catalog, 36, "GLOW_ACTIVE", "", "", false, false),
	      "round trip: the same entries in the same order, delivered is not restored");
	check(catalog_checksum(&other.catalog) == catalog_checksum(&box.catalog), "round trip: the checksum is the same");
	check(guards_intact_of(&other), "round trip: nothing written outside the second catalogue");

	// Pseudo-random catalogues with every kind of byte in names and texts
	random_state = 1906;
	for(round = 0; round < 300; round++)
	{
		bool same = true;

		memset(&box, GUARD, sizeof(box));
		random_catalog(&box.catalog);
		if(box.catalog.count == CATALOG_MAX) full++;
		remember();
		memset(out, 0x7E, sizeof(out));
		length = catalog_to_json(&box.catalog, out, sizeof(out));
		tokens = length > 0 ? json_parse(out, (size_t)length, large_work, 2048) : -1;

		memset(&other, GUARD, sizeof(other));
		catalog_init(&other.catalog);
		if(length <= 0 || (int)strlen(out) != length || tokens != 1 + 10 * box.catalog.count || !unchanged() ||
		   !catalog_from_json(&other.catalog, out, (size_t)length, work, CATALOG_TOKENS)) same = false;
		if(other.catalog.count != box.catalog.count || other.catalog.dropped != 0 || !guards_intact_of(&other)) same = false;
		for(i = 0; same && i < box.catalog.count; i++)
		{
			const catalog_entry_t *entry = box.catalog.entries + i;

			if(!entry_of_is(&other.catalog, i, entry->name, entry->unit, entry->value_class, entry->in_profile, false)) same = false;
		}
		if(catalog_checksum(&other.catalog) != catalog_checksum(&box.catalog) || catalog_checksum(&box.catalog) != model_checksum(&box.catalog)) same = false;
		if(!same && wrong++ < 5) printf("  round %d with %d entries\n", round, box.catalog.count);
	}
	check(wrong == 0, "round trip: 300 random catalogues are written, read as the same entries, and have the checksum of the model");
	check(full > 10, "round trip: full catalogues are among them");
}

/*
 * The rules of the catalogue written a second time as a simple model: a list of entries that refer to a pool
 * of names. Sequences of pseudo-random configurations, arriving values, storing and loading are applied to
 * the model and to the catalogue and compared after every step.
 */

#define NAMES 130

typedef struct
{
	int name;
	const char *unit;
	const char *value_class;
	bool in_profile;
	bool delivered;
} model_entry_t;

// What a configuration says about unit and class, and what the catalogue keeps of it
static const struct
{
	const char *json;       // NULL: the member is missing
	const char *unit;
	const char *value_class;
} model_texts[] = {
	{NULL, "", ""},
	{"", "", ""},
	{"u", "u", "u"},
	{"km/h", "km/h", "km/h"},
	{"temperature", "temperature", "temperature"},
	{"123456789012", "12345678901", "123456789012"},
	{"1234567890°C", "1234567890", "1234567890°C"},
	{"12345678901234567890123", "12345678901", "12345678901234567890123"},
	{"1234567890123456789012°", "12345678901", "1234567890123456789012"},
	{"123456789012345678901234567890", "12345678901", "12345678901234567890123"},
};
#define MODEL_TEXTS ((uint32_t)(sizeof(model_texts) / sizeof(model_texts[0])))

static char names[NAMES][40];
static model_entry_t model[CATALOG_MAX];
static int model_count;
static uint32_t model_dropped;
// What the sequences came across, to show that they reach the rules at all
static int seen_no_room, seen_long_name, seen_removed, seen_kept_delivered, seen_loaded, seen_refused, seen_full, seen_battery_named;

static void model_names(void)
{
	int i;

	strcpy(names[0], "@BATT_V");
	for(i = 1; i < NAMES; i++)
	{
		size_t length = i % 10 == 3 ? 32 : i % 17 == 5 ? 33 : 0;

		snprintf(names[i], sizeof(names[i]), "X%03d", i);
		while(strlen(names[i]) < length) strcat(names[i], "x");
	}
	// Names that look like the one of the battery are names like any other
	strcpy(names[1], "@BATT");
	strcpy(names[2], "@BATT_VV");
}

static void model_init(void)
{
	model_count = 1;
	model_dropped = 0;
	model[0].name = 0;
	model[0].unit = "V";
	model[0].value_class = "";
	model[0].in_profile = false;
	model[0].delivered = false;
}

static int model_find(int name)
{
	int i;

	for(i = 0; i < model_count; i++)
	{
		if(model[i].name == name) return i;
	}
	return -1;
}

// The entry of a name, new at the end if there is none; -1 if the name is too long or there is no room
static int model_entry(int name)
{
	int index = model_find(name);

	if(strlen(names[name]) > 32)
	{
		model_dropped++;
		seen_long_name++;
		return -1;
	}
	if(index >= 0) return index;
	if(model_count == 96)
	{
		model_dropped++;
		seen_no_room++;
		return -1;
	}
	model[model_count].name = name;
	model[model_count].unit = "";
	model[model_count].value_class = "";
	model[model_count].in_profile = false;
	model[model_count].delivered = false;
	return model_count++;
}

static bool model_matches(void)
{
	bool good = true;
	int i;

	if(box.catalog.count != model_count || box.catalog.dropped != model_dropped)
	{
		printf("  count %d (model %d), dropped %lu (model %lu)\n", box.catalog.count, model_count,
		       (unsigned long)box.catalog.dropped, (unsigned long)model_dropped);
		return false;
	}
	for(i = 0; i < model_count; i++)
	{
		if(!entry_is(i, names[model[i].name], model[i].unit, model[i].value_class, model[i].in_profile, model[i].delivered))
		{
			printf("  entry %d: model says %s, unit \"%s\", class \"%s\", profile %d, delivered %d\n", i, names[model[i].name],
			       model[i].unit, model[i].value_class, model[i].in_profile, model[i].delivered);
			good = false;
		}
	}
	return good && guards_intact() && catalog_checksum(&box.catalog) == model_checksum(&box.catalog);
}

static bool model_sequence(uint32_t seed, int steps)
{
	static bool named[NAMES];
	static int members[NAMES];
	static int unit_of[NAMES];
	static int class_of[NAMES];
	int step, i;

	random_state = seed;
	start();
	model_init();

	for(step = 0; step < steps; step++)
	{
		uint32_t what = rnd(100);
		uint32_t density = rnd(4) == 0 ? 10 : 1 + rnd(9);
		int first = (int)rnd(NAMES);

		if(what < 45)
		{
			// A configuration: some names of the pool in an order that changes, some of them with a value that is no object
			size_t length = 0;
			int count = 0;
			int kept = 0;

			memset(named, 0, sizeof(named));
			length += (size_t)snprintf(text + length, sizeof(text) - length, "{");
			for(i = 0; i < NAMES; i++)
			{
				int name = (first + i * 7) % NAMES;
				uint32_t unit = rnd(MODEL_TEXTS);
				uint32_t value_class = rnd(MODEL_TEXTS);

				if(rnd(10) >= density) continue;

				length += (size_t)snprintf(text + length, sizeof(text) - length, "%s\"%s\":", length > 1 ? "," : "", names[name]);
				if(rnd(10) == 0)
				{
					length += (size_t)snprintf(text + length, sizeof(text) - length, "%s", rnd(2) ? "5" : "[{\"unit\":\"no\"}]");
					continue;
				}
				length += (size_t)snprintf(text + length, sizeof(text) - length, "{");
				if(model_texts[value_class].json != NULL)
				{
					length += (size_t)snprintf(text + length, sizeof(text) - length, "\"class\":\"%s\"", model_texts[value_class].json);
				}
				if(model_texts[unit].json != NULL)
				{
					length += (size_t)snprintf(text + length, sizeof(text) - length, "%s\"unit\":\"%s\"",
					                           model_texts[value_class].json != NULL ? "," : "", model_texts[unit].json);
				}
				length += (size_t)snprintf(text + length, sizeof(text) - length, "}");

				named[name] = true;
				members[count] = name;
				unit_of[count] = (int)unit;
				class_of[count] = (int)value_class;
				count++;
			}
			snprintf(text + length, sizeof(text) - length, "}");

			// First the entries that are no longer named go, then the members are taken in their order
			for(i = 0; i < model_count; i++)
			{
				model[i].in_profile = named[model[i].name];
				if(model[i].in_profile || model[i].delivered || model[i].name == 0)
				{
					if(!model[i].in_profile && model[i].delivered) seen_kept_delivered++;
					model[kept++] = model[i];
				}
				else seen_removed++;
			}
			model_count = kept;
			for(i = 0; i < count; i++)
			{
				int index = model_entry(members[i]);

				if(index < 0) continue;
				model[index].unit = model_texts[unit_of[i]].unit;
				model[index].value_class = model_texts[class_of[i]].value_class;
				model[index].in_profile = true;
				if(members[i] == 0) seen_battery_named++;
			}

			if(!catalog_apply_config(&box.catalog, text, strlen(text), large_work, 2048))
			{
				printf("  step %d: the configuration was refused\n", step);
				return false;
			}
		}
		else if(what < 80)
		{
			// Values arrive: the list is written here, in a known order
			values_init(&values);
			for(i = 0; i < NAMES && values.count < VALUES_MAX; i++)
			{
				int name = (first + i * 11) % NAMES;
				int index;

				if(rnd(10) >= density || strlen(names[name]) > 32) continue;

				strcpy(values.items[values.count].name, names[name]);
				values.items[values.count].kind = rnd(2) ? VALUE_NUMBER : VALUE_ON;
				values.count++;

				index = model_entry(name);
				if(index >= 0) model[index].delivered = true;
			}
			catalog_note_values(&box.catalog, &values);
		}
		else if(what < 90)
		{
			// The catalogue is stored and loaded again
			int length = catalog_to_json(&box.catalog, out, sizeof(out));

			if(length <= 0 || !catalog_from_json(&box.catalog, out, (size_t)length, large_work, 2048))
			{
				printf("  step %d: the catalogue was not stored or not loaded\n", step);
				return false;
			}
			for(i = 0; i < model_count; i++) model[i].delivered = false;
			model_dropped = 0;
			seen_loaded++;
		}
		else if(what < 95)
		{
			static const char *const refused[] = {"[1,2]", "{\"X001\":{}", "7", "{\"X001\":{\"unit\":\"u\",\"class\":\"c\"}}", ""};
			uint32_t which = rnd(5);

			remember();
			// The fourth text is a configuration: only the reader of stored catalogues refuses it
			if((which != 3 && catalog_apply_config(&box.catalog, refused[which], strlen(refused[which]), large_work, 2048)) ||
			   catalog_from_json(&box.catalog, refused[which], strlen(refused[which]), large_work, 2048) || !unchanged())
			{
				printf("  step %d: a text that is no catalogue was accepted or changed something\n", step);
				return false;
			}
			seen_refused++;
		}
		else
		{
			catalog_init(&box.catalog);
			model_init();
		}

		if(model_count == 96) seen_full++;
		if(!model_matches())
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
	check(strcmp(names[0], CATALOG_BATTERY) == 0 && strlen(names[3]) == 32 && strlen(names[5]) == 33 && strcmp(names[12], "X012") == 0 &&
	      strcmp(names[1], "@BATT") == 0 && strcmp(names[2], "@BATT_VV") == 0,
	      "model: the pool has the battery, two names that look like it, and names of 4, 32 and 33 bytes");
	for(seed = 1; seed <= 40; seed++)
	{
		snprintf(what, sizeof(what), "model: 200 random steps with seed %lu behave as the rules say", (unsigned long)seed);
		check(model_sequence(seed, 200), what);
	}
	printf("  no room %d, name too long %d, removed %d, kept as delivered %d, loaded %d, refused %d, full %d, battery named %d\n",
	       seen_no_room, seen_long_name, seen_removed, seen_kept_delivered, seen_loaded, seen_refused, seen_full, seen_battery_named);
	check(seen_no_room > 100 && seen_long_name > 100 && seen_removed > 100 && seen_kept_delivered > 100 && seen_loaded > 100 &&
	      seen_refused > 100 && seen_full > 100 && seen_battery_named > 100,
	      "model: the sequences contain full catalogues, long names, removed and kept entries, loading, refused texts, a named battery");
}

static void test_checksum(void)
{
	uint32_t before;

	memset(&box, GUARD, sizeof(box));
	memset(&box.catalog, 0, sizeof(box.catalog));
	check(catalog_checksum(&box.catalog) == 0x811C9DC5u, "checksum: of no entries it is the start value of FNV-1a");

	start();
	check(catalog_checksum(&box.catalog) == 0x57FD340Fu, "checksum: of a fresh catalogue");
	check(config("{\"A\":{\"unit\":\"°C\",\"class\":\"temperature\"}}") && catalog_checksum(&box.catalog) == 0xE918D2C8u,
	      "checksum: of two entries, with a byte above 0x7f in a unit");
	check(model_checksum(&box.catalog) == 0xE918D2C8u, "checksum: the model in this test gives the same");

	// What must change it
	before = catalog_checksum(&box.catalog);
	remember();
	box.catalog.entries[1].name[0] = 'B';
	check(catalog_checksum(&box.catalog) != before && catalog_checksum(&box.catalog) == model_checksum(&box.catalog), "checksum: changes with a name");
	memcpy(&box, &snapshot, sizeof(box));
	strcpy(box.catalog.entries[1].unit, "K");
	check(catalog_checksum(&box.catalog) != before && catalog_checksum(&box.catalog) == model_checksum(&box.catalog), "checksum: changes with a unit");
	memcpy(&box, &snapshot, sizeof(box));
	strcpy(box.catalog.entries[1].value_class, "temperaturf");
	check(catalog_checksum(&box.catalog) != before && catalog_checksum(&box.catalog) == model_checksum(&box.catalog), "checksum: changes with a class");
	memcpy(&box, &snapshot, sizeof(box));
	box.catalog.entries[1].in_profile = false;
	check(catalog_checksum(&box.catalog) != before && catalog_checksum(&box.catalog) == model_checksum(&box.catalog), "checksum: changes with in_profile");
	memcpy(&box, &snapshot, sizeof(box));
	box.catalog.entries[0].in_profile = true;
	check(catalog_checksum(&box.catalog) != before && catalog_checksum(&box.catalog) == model_checksum(&box.catalog), "checksum: changes with in_profile of the first entry");
	memcpy(&box, &snapshot, sizeof(box));
	box.catalog.count = 1;
	check(catalog_checksum(&box.catalog) == 0x57FD340Fu, "checksum: changes when the last entry goes");
	memcpy(&box, &snapshot, sizeof(box));
	check(config("{\"A\":{\"unit\":\"°C\",\"class\":\"temperature\"},\"\":{}}") && catalog_checksum(&box.catalog) != before &&
	      catalog_checksum(&box.catalog) == model_checksum(&box.catalog), "checksum: changes with a new entry that has an empty name and no texts");

	// What must not change it
	memcpy(&box, &snapshot, sizeof(box));
	box.catalog.entries[1].delivered = true;
	box.catalog.entries[0].delivered = true;
	check(catalog_checksum(&box.catalog) == before, "checksum: does not change with delivered");
	box.catalog.dropped = 77;
	check(catalog_checksum(&box.catalog) == before, "checksum: does not change with dropped");
	memset(box.catalog.entries[1].name + 2, 'x', sizeof(box.catalog.entries[1].name) - 2);
	memset(box.catalog.entries[1].unit + 4, 'y', sizeof(box.catalog.entries[1].unit) - 4);
	memset(box.catalog.entries[1].value_class + 12, 'z', sizeof(box.catalog.entries[1].value_class) - 12);
	memset(&box.catalog.entries[2], 0x01, sizeof(box.catalog.entries[2]));
	check(catalog_checksum(&box.catalog) == before, "checksum: bytes behind the end of a text and entries behind the last do not count");

	// Where one text ends and the next begins counts
	start();
	check(config("{\"ab\":{\"unit\":\"c\"}}"), "an entry ab with unit c");
	before = catalog_checksum(&box.catalog);
	check(config("{\"a\":{\"unit\":\"bc\"}}") && catalog_checksum(&box.catalog) != before && catalog_checksum(&box.catalog) == model_checksum(&box.catalog),
	      "checksum: differs for name ab, unit c and name a, unit bc");
	before = catalog_checksum(&box.catalog);
	check(config("{\"a\":{\"class\":\"bc\"}}") && catalog_checksum(&box.catalog) != before && catalog_checksum(&box.catalog) == model_checksum(&box.catalog),
	      "checksum: differs for unit bc and class bc");
	start();
	check(config("{\"A\":{},\"B\":{}}"), "two entries A and B");
	before = catalog_checksum(&box.catalog);
	start();
	check(config("{\"B\":{},\"A\":{}}") && catalog_checksum(&box.catalog) != before && catalog_checksum(&box.catalog) == model_checksum(&box.catalog),
	      "checksum: differs for the same entries in another order");

	// In use: store only when it changed
	start();
	before = catalog_checksum(&box.catalog);
	check(deliver("{\"A\":1}") && catalog_checksum(&box.catalog) != before, "checksum: a value that arrives for the first time changes it (a new name)");
	before = catalog_checksum(&box.catalog);
	check(deliver("{\"A\":2}") && catalog_checksum(&box.catalog) == before, "checksum: the same value again does not");
	check(config("{\"A\":{}}") && catalog_checksum(&box.catalog) != before, "checksum: the profile naming it changes it (in_profile)");
	before = catalog_checksum(&box.catalog);
	check(config("{\"A\":{}}") && catalog_checksum(&box.catalog) == before, "checksum: the same profile again does not");
}

int main(void)
{
	memset(work_between_guards, 0x5A, sizeof(work_between_guards));
	work_between_guards[0] = front_guard;
	work_between_guards[1] = front_guard;

	test_init();
	test_stale_tokens();
	test_w906();
	test_config_members();
	test_config_invalid();
	test_config_removal();
	test_limits();
	test_names();
	test_cut();
	test_cut_model();
	test_note_values();
	test_to_json();
	test_from_json();
	test_round_trip();
	test_model();
	test_checksum();

	check(work[CATALOG_TOKENS].start == 0x5A5A5A5A && work[CATALOG_TOKENS + 1].start == 0x5A5A5A5A &&
	      memcmp(&work_between_guards[0], &front_guard, sizeof(front_guard)) == 0 && memcmp(&work_between_guards[1], &front_guard, sizeof(front_guard)) == 0,
	      "no call wrote before or behind the CATALOG_TOKENS tokens it was given");
	return test_end();
}
