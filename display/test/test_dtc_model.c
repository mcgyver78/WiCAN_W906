/*
 * Host test for display/components/core/dtc_model.c. Run "make test_dtc_model && ./test_dtc_model" in
 * display/test. redproof.py removes or weakens every rule once (mutations/dtc_model.py) and expects this
 * test to fail.
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include "test.h"
#include "dtc_model.h"

#define FIXTURES    "../../tools/w906/fixtures/"
#define FILL        0xA5
#define COUNT(a)    ((int)(sizeof(a) / sizeof((a)[0])))
// Made-up results may be larger than a real one
#define WORK_TOKENS 4096

// The result between bytes the module must not touch
static struct
{
	unsigned char before[32];
	dtc_result_t result;
	unsigned char behind[32];
} box;

// Room for the JSON reader, with two tokens the module is not told of
static json_token_t work[WORK_TOKENS + 2];

// What a test expects, written down by hand or put together next to the text it is read from
static dtc_result_t expected;

// Calls that wrote where they must not
static int stray_writes = 0;

static char what[256];

/* The results the tests start from --------------------------------------------------------------------- */

// The 18 control units of the W906 as main/autopid.c names them, and their short names by hand
typedef struct
{
	const char *name;
	const char *id;
	bool uds;
	const char *short_name;
} unit_t;

static const unit_t UNITS[18] = {
	{"N73 Elektronisches Zündschloss (EZS)", "4E0", false, "EZS"},
	{"N3/28 Motorelektronik (CDID3)", "7E0", true, "CDID3"},
	{"Y3/8n4 Getriebesteuerung (NAG2)", "7E1", false, "NAG2"},
	{"N15/5 Wählhebelmodul (EWM)", "788", false, "EWM"},
	{"N30/4 ESP", "784", true, "ESP"},
	{"N10 SAM", "662", false, "SAM"},
	{"N118/5 Kraftstoffpumpe (FSCU)", "778", true, "FSCU"},
	{"N28/4 Anhängererkennung (AHE)", "730", true, "AHE"},
	{"N80 Mantelrohrmodul (MRM)", "792", false, "MRM"},
	{"N70 Dachbedieneinheit (DBE)", "667", false, "DBE"},
	{"N72/1 Oberes Bedienfeld (OBF)", "6A5", true, "OBF"},
	{"B162 Collision Prevention Assist", "65E", true, "Collision Prevention Assist"},
	{"N87/8 Radio", "5D6", true, "Radio"},
	{"A2/30 Navigationsmodul", "633", true, "Navigationsmodul"},
	{"A1 Kombiinstrument", "796", false, "Kombiinstrument"},
	{"S98 Klimaanlage", "791", false, "Klimaanlage"},
	{"N2/14 Rückhaltesystem (SRS)", "6BC", false, "SRS"},
	{"N69/1 Fahrertür (TSG)", "6C8", false, "TSG"},
};

// A small result with every member the format has, each value there only once
#define BASE \
	"{\"state\":\"done\",\"action\":\"clear\",\"duration_ms\":31500,\"dtc_count\":4,\"ecus\":[" \
	"{\"name\":\"N3/28 Motorelektronik (CDID3)\",\"id\":\"7E0\",\"protocol\":\"UDS\",\"cleared\":true,\"status\":\"ok\"," \
	"\"dtcs\":[{\"code\":\"P242F-FA\",\"status\":\"68\",\"active\":false}],\"dtcs_omitted\":2}," \
	"{\"name\":\"N10 SAM\",\"id\":\"662\",\"protocol\":\"KWP\",\"status\":\"incomplete\"," \
	"\"dtcs\":[{\"code\":\"9301\",\"status\":\"60\"}]}]}"

// The members of BASE as they stand there. Kind: t text for display, c one of a few texts, s status of a
// control unit, n number, a list; N and B are a number and a true or false that may be absent.
typedef struct
{
	const char *member;
	char kind;
} member_t;

static const member_t MEMBERS[] = {
	{"\"state\":\"done\"", 'c'}, {"\"action\":\"clear\"", 'c'}, {"\"duration_ms\":31500", 'n'}, {"\"dtc_count\":4", 'n'},
	{"\"ecus\":[", 'a'},
	{"\"name\":\"N3/28 Motorelektronik (CDID3)\"", 't'}, {"\"id\":\"7E0\"", 't'}, {"\"protocol\":\"UDS\"", 'c'},
	{"\"cleared\":true", 'B'}, {"\"status\":\"ok\"", 's'}, {"\"dtcs\":[{\"code\":\"P242F-FA\"", 'a'}, {"\"dtcs_omitted\":2", 'N'},
	{"\"code\":\"P242F-FA\"", 't'}, {"\"status\":\"68\"", 't'}, {"\"active\":false", 'B'},
	{"\"name\":\"N10 SAM\"", 't'}, {"\"id\":\"662\"", 't'}, {"\"protocol\":\"KWP\"", 'c'}, {"\"status\":\"incomplete\"", 's'},
	{"\"dtcs\":[{\"code\":\"9301\"", 'a'}, {"\"code\":\"9301\"", 't'}, {"\"status\":\"60\"", 't'},
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

// The first bytes of a text that fit into a field. Only for texts without characters of several bytes at the cut.
static void copy_bytes(char *field, size_t size, const char *text)
{
	size_t length = strlen(text);

	if(length > size - 1) length = size - 1;
	memcpy(field, text, length);
	field[length] = '\0';
}

// One call with `count` tokens of room. Everything around the result and behind the room is watched.
static bool parse_room(const char *json, size_t length, int count)
{
	int told = count < 0 ? 0 : count;
	bool ok;

	memset(&box, FILL, sizeof(box));
	memset(work, FILL, sizeof(work));
	ok = dtc_result_parse(json, length, &box.result, work, count);
	if(!filled(box.before, sizeof(box.before)) || !filled(box.behind, sizeof(box.behind)) ||
	   !filled(&work[told], sizeof(work) - (size_t)told * sizeof(work[0])))
	{
		printf("  written outside the result or behind the %d tokens\n", count);
		stray_writes++;
	}
	return ok;
}

static bool accepted(const char *json)
{
	return json != NULL && parse_room(json, strlen(json), WORK_TOKENS);
}

// false, and not one byte of the result has changed
static bool refused(const char *json)
{
	return json != NULL && !parse_room(json, strlen(json), WORK_TOKENS) && filled(&box.result, sizeof(box.result));
}

// `base` with `old` replaced by `new`. NULL unless `old` is there exactly once: a variant that changes
// nothing, or another place than meant, would test nothing. A variant of a variant is possible once.
static const char *variant(const char *base, const char *old, const char *new)
{
	static char texts[2][65536];
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

// The member with another value: "\"dtc_count\":4" and "-1" give "\"dtc_count\":-1". The beginning of a list
// moves to an unknown member, so that the text stays JSON: "\"ecus\":[" and "1" give "\"ecus\":1,\"x\":[".
// NULL if it gets too long.
static const char *with_value(const char *member, const char *value)
{
	static char text[8192];
	int key = (int)(strchr(member, ':') - member) + 1;
	bool list = member[key] == '[';

	if(snprintf(text, sizeof(text), "%.*s%s%s%s", key, member, value, list ? ",\"x\":" : "", list ? member + key : "") >=
	   (int)sizeof(text)) return NULL;
	return text;
}

// BASE with another value for one member
static const char *base_with(const char *member, const char *value)
{
	return variant(BASE, member, with_value(member, value));
}

// The member under a name the format does not know: "\"dtc_count\":4" gives "\"dtc_count_\":4"
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

static bool text_is(const char *field, size_t size, const char *text)
{
	return memchr(field, '\0', size) != NULL && strcmp(field, text) == 0;
}

// The result that was read, field by field, and nothing behind the control units and codes that count
static bool result_is(const dtc_result_t *e)
{
	const dtc_result_t *r = &box.result;
	bool same = true;

	// First: a result that was not written at all has no counts to go by, and no true or false to look at
	if(r->ecu_count != e->ecu_count || r->code_count != e->code_count)
	{
		printf("  %d control units with %d codes, expected %d with %d\n", r->ecu_count, r->code_count, e->ecu_count, e->code_count);
		return false;
	}
	if(flag(&r->clear) != e->clear || r->duration_ms != e->duration_ms || r->dtc_count != e->dtc_count || flag(&r->cut) != e->cut)
	{
		printf("  clear %d, duration %lu, count %lu, cut %d; expected %d, %lu, %lu, %d\n", flag(&r->clear), (unsigned long)r->duration_ms,
		       (unsigned long)r->dtc_count, flag(&r->cut), e->clear, (unsigned long)e->duration_ms, (unsigned long)e->dtc_count, e->cut);
		same = false;
	}
	for(int i = 0; i < e->ecu_count; i++)
	{
		const dtc_ecu_t *a = &r->ecus[i];
		const dtc_ecu_t *b = &e->ecus[i];

		if(!text_is(a->name, sizeof(a->name), b->name) || !text_is(a->short_name, sizeof(a->short_name), b->short_name) ||
		   !text_is(a->id, sizeof(a->id), b->id) || flag(&a->uds) != b->uds || a->status != b->status || a->nrc != b->nrc ||
		   a->cleared != b->cleared || a->first_code != b->first_code || a->code_count != b->code_count || a->omitted != b->omitted)
		{
			printf("  control unit %d is not %s (%s) %s uds %d status %d nrc %d cleared %d codes %d from %d omitted %lu\n", i, b->name,
			       b->short_name, b->id, b->uds, (int)b->status, b->nrc, b->cleared, b->code_count, b->first_code, (unsigned long)b->omitted);
			same = false;
		}
	}
	for(int i = 0; i < e->code_count; i++)
	{
		const dtc_code_t *a = &r->codes[i];
		const dtc_code_t *b = &e->codes[i];

		if(!text_is(a->code, sizeof(a->code), b->code) || !text_is(a->status, sizeof(a->status), b->status) || a->active != b->active)
		{
			printf("  code %d is not %s status %s active %d\n", i, b->code, b->status, b->active);
			same = false;
		}
	}
	if(!filled(r->ecus + r->ecu_count, (size_t)(DTC_ECUS_MAX - r->ecu_count) * sizeof(r->ecus[0])) ||
	   !filled(r->codes + r->code_count, (size_t)(DTC_CODES_MAX - r->code_count) * sizeof(r->codes[0])))
	{
		printf("  something is written behind the control units or codes that count\n");
		same = false;
	}
	return same;
}

static void expect_begin(bool clear, uint32_t duration_ms, uint32_t dtc_count)
{
	memset(&expected, 0, sizeof(expected));
	expected.clear = clear;
	expected.duration_ms = duration_ms;
	expected.dtc_count = dtc_count;
}

// The next control unit: status ok, no codes, nothing cleared, nothing omitted
static dtc_ecu_t *expect_ecu(const char *name, const char *short_name, const char *id, bool uds)
{
	dtc_ecu_t *ecu = &expected.ecus[expected.ecu_count++];

	copy_bytes(ecu->name, sizeof(ecu->name), name);
	copy_bytes(ecu->short_name, sizeof(ecu->short_name), short_name);
	copy_bytes(ecu->id, sizeof(ecu->id), id);
	ecu->uds = uds;
	ecu->status = DTC_ECU_OK;
	ecu->cleared = -1;
	ecu->first_code = (uint16_t)expected.code_count;
	return ecu;
}

// The next code of the control unit added last
static void expect_code(const char *code, const char *status, int active)
{
	dtc_code_t *entry = &expected.codes[expected.code_count++];

	copy_bytes(entry->code, sizeof(entry->code), code);
	copy_bytes(entry->status, sizeof(entry->status), status);
	entry->active = (int8_t)active;
	expected.ecus[expected.ecu_count - 1].code_count++;
}

static void expect_unit(int index)
{
	expect_ecu(UNITS[index].name, UNITS[index].short_name, UNITS[index].id, UNITS[index].uds);
}

// What BASE has to become
static void expect_base(void)
{
	expect_begin(true, 31500, 4);
	expect_unit(1);
	expected.ecus[0].cleared = 1;
	expected.ecus[0].omitted = 2;
	expect_code("P242F-FA", "68", 0);
	expect_unit(5);
	expected.ecus[1].status = DTC_ECU_INCOMPLETE;
	expect_code("9301", "60", -1);
}

static uint32_t random_state = 20261003;

/*
 * Never two rolls among the arguments of one call: C leaves open which argument is worked out first, gcc
 * takes the last and clang the first, and the made-up results of the CI were others than the ones on a Mac.
 * Where a call needs two, they are rolled before it, in the order gcc had: the last argument first.
 */
static uint32_t random_below(uint32_t limit)
{
	random_state = random_state * 1103515245u + 12345u;
	return (random_state >> 8) % limit;
}

/* Short names ------------------------------------------------------------------------------------------ */

// The short name in a room of `size` bytes inside a larger one; the bytes behind the room are watched
static bool short_name_is(const char *name, size_t size, const char *short_name)
{
	char out[96];

	memset(out, FILL, sizeof(out));
	dtc_short_name(name, out, size);
	if(!filled(out + size, sizeof(out) - size))
	{
		printf("  written behind the %d bytes for the short name\n", (int)size);
		return false;
	}
	if(size == 0) return true;
	if(memchr(out, '\0', size) == NULL || strcmp(out, short_name) != 0)
	{
		printf("  the short name of \"%s\" in %d bytes is not \"%s\"\n", name, (int)size, short_name);
		return false;
	}
	return true;
}

// As many whole characters from the beginning of a text as fit into `room` bytes, counted character by character
static size_t whole_characters(const char *text, size_t room)
{
	size_t length = 0;

	while(text[length] != '\0')
	{
		unsigned char first = (unsigned char)text[length];
		size_t bytes = first < 0x80 ? 1 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : 4;

		if(length + bytes > room) break;
		length += bytes;
	}
	return length;
}

// The rules of the header a second time: looked for from the end of the name, cut character by character.
// Only for names of whole UTF-8 characters.
static void model_short_name(const char *name, size_t size, char *out)
{
	const char *text = name;
	size_t length = strlen(name);
	const char *blank = strchr(name, ' ');
	const char *open = NULL;
	size_t kept = 0;

	// The last opening parenthesis that has a closing one behind it; its pair is the first of those
	for(size_t i = length; i > 0 && open == NULL; i--)
	{
		if(name[i - 1] == '(' && strchr(name + i, ')') != NULL) open = name + i - 1;
	}
	if(open != NULL && open[1] != ')')
	{
		text = open + 1;
		length = (size_t)(strchr(open, ')') - text);
	}
	else if(blank != NULL && strcspn(name, "0123456789") < (size_t)(blank - name) && blank[strspn(blank, " ")] != '\0')
	{
		text = blank + strspn(blank, " ");
		length = strlen(text);
	}

	while(kept < length)
	{
		size_t bytes = 1;

		while(kept + bytes < length && ((unsigned char)text[kept + bytes] & 0xC0) == 0x80) bytes++;
		if(kept + bytes + 1 > size) break;
		kept += bytes;
	}
	memcpy(out, text, kept);
	out[kept] = '\0';
}

static void test_short_name(void)
{
	static const struct
	{
		const char *name;
		const char *short_name;
		const char *rule;
	} cases[] = {
		{"N2/14 Rückhaltesystem (SRS)", "SRS", "the text in the parentheses"},
		{"N30/4 ESP", "ESP", "without the first word, which has a digit"},
		{"A1 Kombiinstrument", "Kombiinstrument", "without the first word, which has a digit"},
		{"Radio", "Radio", "the whole name"},
		{"", "", "an empty name"},
		{"Oberes Bedienfeld", "Oberes Bedienfeld", "the whole name: no digit in the first word"},
		{"Motor M1", "Motor M1", "the whole name: a digit in the second word does not count"},
		{"Motor 2 links", "Motor 2 links", "the whole name: a digit in the second word does not count"},
		{"N1x ESP", "ESP", "a digit in the middle of the first word"},
		{"1N ESP", "ESP", "a digit at the beginning of the first word"},
		{"9 ESP", "ESP", "a first word that is only a digit"},
		{"N30/4 ESP links", "ESP links", "all words behind the first"},
		{"N10  SAM", "SAM", "two blanks behind the first word"},
		{"N1(B C", "C", "a word ends at a blank, not at a parenthesis"},
		{"(N1 C", "C", "a word may begin with a parenthesis"},
		{"N/: ESP", "N/: ESP", "the characters next to the digits in ASCII are no digits"},
		{"A (B) (C)", "C", "the last of two pairs of parentheses"},
		{"A (B) C", "B", "parentheses in the middle of the name"},
		{"(B)", "B", "a name of parentheses only"},
		{"N1 (B) C2", "B", "parentheses come before the rule of the first word"},
		{"A (B", "A (B", "an opening parenthesis alone is no pair"},
		{"N1 (B", "(B", "an opening parenthesis alone is no pair: the rule of the first word"},
		{"A B)", "A B)", "a closing parenthesis alone is no pair"},
		{"A )B(", "A )B(", "a closing parenthesis before the opening one is no pair"},
		{"A (B) (C", "B", "the last pair, not the last opening parenthesis"},
		{"A (B) C)", "B", "the last pair, not the last closing parenthesis"},
		{"A (B (C)", "C", "an opening parenthesis belongs to the next closing one"},
		{"A (B (C) D)", "C", "an opening parenthesis belongs to the next closing one"},
		{"A (Tür 1)", "Tür 1", "an umlaut and a blank in the parentheses"},
		{"N10", "N10", "one word with a digit: nothing would be left, so the whole name"},
		{"N10 ", "N10 ", "one word with a digit and a blank: nothing would be left"},
		{"N10   ", "N10   ", "one word with a digit and blanks: nothing would be left"},
		{"N10 SAM ()", "SAM ()", "empty parentheses: nothing would be left, so the rule of the first word"},
		{"Radio ()", "Radio ()", "empty parentheses and no digit: the whole name"},
		{"N10 (A) ()", "(A) ()", "the last pair is empty: the rule of the first word"},
		{" N10 SAM", " N10 SAM", "a blank at the beginning: the first word is empty and has no digit"},
		{" ", " ", "a blank alone"},
		{"N1 ( )", " ", "a blank in the parentheses is a text"},
		{"A ( B )", " B ", "blanks at the beginning and the end of the text in the parentheses belong to it"},
		{"N1 )B", ")B", "the name without its first word may begin with a parenthesis"},
		{"N1 2 B", "2 B", "only the first word is left out, also if the second has a digit too"},
		{"A\xE2\x82\xAC B", "A\xE2\x82\xAC B", "the whole name: the bytes of a character of three bytes are no digits"},
	};
	static const char *const cut_names[] = {
		"N2/14 Rückhaltesystem", "A1 Zündschloßgehäuse €5 \xF0\x9F\x98\x80 x", "B (\xF0\x9F\x98\x80\xE2\x82\xAC\xC3\xBCz\xC3\xBC\xE2\x82\xAC\xF0\x9F\x98\x80)",
		"ÄÖÜäöüß", "abcdefghijklmnopqrstuvwxyz", "x",
	};
	static const char *const cut_shorts[] = {
		"Rückhaltesystem", "Zündschloßgehäuse €5 \xF0\x9F\x98\x80 x", "\xF0\x9F\x98\x80\xE2\x82\xAC\xC3\xBCz\xC3\xBC\xE2\x82\xAC\xF0\x9F\x98\x80",
		"ÄÖÜäöüß", "abcdefghijklmnopqrstuvwxyz", "x",
	};
	static char long_name[1001];
	char digits[8] = "X0 Y";
	bool same = true;

	for(int i = 0; i < COUNT(UNITS); i++)
	{
		snprintf(what, sizeof(what), "short name of \"%s\" is \"%s\"", UNITS[i].name, UNITS[i].short_name);
		check(short_name_is(UNITS[i].name, 64, UNITS[i].short_name), what);
	}
	for(int i = 0; i < COUNT(cases); i++)
	{
		snprintf(what, sizeof(what), "short name of \"%s\" is \"%s\": %s", cases[i].name, cases[i].short_name, cases[i].rule);
		check(short_name_is(cases[i].name, 64, cases[i].short_name), what);
	}
	for(char digit = '0'; digit <= '9'; digit++)
	{
		digits[1] = digit;
		same = short_name_is(digits, 64, "Y") && same;
	}
	check(same, "each of the digits 0 to 9 in the first word counts");

	// Every size from none to more than enough: whole characters only, never a byte behind the room
	for(int i = 0; i < COUNT(cut_names); i++)
	{
		same = true;
		for(size_t size = 1; size <= strlen(cut_shorts[i]) + 3; size++)
		{
			char cut[64];

			snprintf(cut, sizeof(cut), "%.*s", (int)whole_characters(cut_shorts[i], size - 1), cut_shorts[i]);
			same = short_name_is(cut_names[i], size, cut) && same;
		}
		snprintf(what, sizeof(what), "short name of \"%s\" in every size: as many whole characters as fit", cut_names[i]);
		check(same, what);
	}
	check(short_name_is("N2/14 Rückhaltesystem", 17, "Rückhaltesystem"), "a short name of 16 bytes fits into 17");
	check(short_name_is("N2/14 Rückhaltesystem", 16, "Rückhaltesyste"), "a short name of 16 bytes is cut to 15 in a room of 16");
	check(short_name_is("N2/14 Rückhaltesystem", 3, "R"), "the two bytes of an umlaut are not cut apart: one letter in a room of 3");
	check(short_name_is("N2/14 Rückhaltesystem", 4, "Rü"), "the umlaut fits into a room of 4");
	check(short_name_is("N2/14 Rückhaltesystem", 1, ""), "a room of 1 byte holds an empty text");
	check(short_name_is("N2/14 Rückhaltesystem", 0, ""), "a room of 0 bytes is not written at all");
	check(short_name_is("Radio", 0, ""), "a room of 0 bytes is not written at all, also for a whole name");

	memset(long_name, 'n', sizeof(long_name) - 1);
	memset(digits, 'n', sizeof(digits) - 1);
	check(short_name_is(long_name, sizeof(digits), digits), "a name of 1000 bytes is cut to 7 in a room of 8");

	// Bytes that are no UTF-8: whatever comes out ends inside the room
	{
		static const char *const broken[] = {"\x80\x80\x80\x80\x80\x80", "N1 \xBF\xBF\xBF\xBF", "(\xC3\xC3\xC3\xC3)", "\xF0\x9F\x98", "a\xFF\xFEx"};
		char out[16];

		same = true;
		for(int i = 0; i < COUNT(broken); i++)
		{
			for(size_t size = 0; size <= 8; size++)
			{
				memset(out, FILL, sizeof(out));
				dtc_short_name(broken[i], out, size);
				if(!filled(out + size, sizeof(out) - size) || (size > 0 && memchr(out, '\0', size) == NULL)) same = false;
			}
		}
		check(same, "names that are no UTF-8: the short name ends inside its room, nothing is written behind it");
	}
	check(short_name_is("a\xFF\xFEx", 64, "a\xFF\xFEx"), "bytes that are no UTF-8 are passed on if they fit");
	check(short_name_is("\x80\x80\x80\x80\x80\x80", 7, "\x80\x80\x80\x80\x80\x80") && short_name_is("\x80\x80\x80\x80\x80\x80", 6, ""),
	      "continuation bytes alone are passed on if they fit, else there is no character boundary but the beginning");

	// Names put together at random from blanks, parentheses, digits and characters of 1 to 4 bytes, in rooms of every size
	same = true;
	for(int i = 0; i < 20000 && same; i++)
	{
		static const char *const pieces[] = {
			" ", " ", "(", "(", ")", ")", "N", "a", "1", "7", "\xC3\xBC", "\xE2\x82\xAC", "\xF0\x9F\x98\x80",
		};
		int count = (int)random_below(13);
		size_t size = random_below(56);
		char name[64] = "";
		char model[64];

		for(int c = 0; c < count; c++) strcat(name, pieces[random_below(COUNT(pieces))]);
		model_short_name(name, size, model);
		same = short_name_is(name, size, model);
	}
	check(same, "20000 names put together at random, in rooms of 0 to 55 bytes: the short name the rules give when applied a second way");
}

/* Fixtures --------------------------------------------------------------------------------------------- */

static char fixture[8192];

static bool read_result_fixture(const char *file)
{
	char path[128];

	snprintf(path, sizeof(path), "%s%s", strchr(file, '/') != NULL ? "" : FIXTURES, file);
	return read_fixture(path, fixture, sizeof(fixture));
}

// The fixture read with the room the header promises to be enough
static bool fixture_is_expected(const char *file)
{
	return read_result_fixture(file) && parse_room(fixture, strlen(fixture), DTC_RESULT_TOKENS) && result_is(&expected);
}

static bool summary_is(int with_codes, int not_ok, int clean, uint32_t codes)
{
	struct
	{
		dtc_summary_t summary;
		unsigned char behind[8];
	} room;

	memset(&room, FILL, sizeof(room));
	dtc_summarize(&box.result, &room.summary);
	if(room.summary.ecus_with_codes != with_codes || room.summary.ecus_not_ok != not_ok || room.summary.ecus_clean != clean ||
	   room.summary.codes != codes || !filled(room.behind, sizeof(room.behind)))
	{
		printf("  summary %d with codes, %d not ok, %d clean, %lu codes; expected %d, %d, %d, %lu\n", room.summary.ecus_with_codes,
		       room.summary.ecus_not_ok, room.summary.ecus_clean, (unsigned long)room.summary.codes, with_codes, not_ok, clean,
		       (unsigned long)codes);
		return false;
	}
	return true;
}

static bool clear_summary_is(const dtc_result_t *before, const dtc_result_t *after, uint32_t was, uint32_t remaining,
                             uint32_t cleared, int unconfirmed)
{
	struct
	{
		dtc_clear_summary_t summary;
		unsigned char behind[8];
	} room;

	memset(&room, FILL, sizeof(room));
	dtc_clear_summarize(before, after, &room.summary);
	if(room.summary.before != was || room.summary.remaining != remaining || room.summary.cleared != cleared ||
	   room.summary.unconfirmed != unconfirmed || !filled(room.behind, sizeof(room.behind)))
	{
		printf("  %lu before, %lu remaining, %lu cleared, %d unconfirmed; expected %lu, %lu, %lu, %d\n",
		       (unsigned long)room.summary.before, (unsigned long)room.summary.remaining, (unsigned long)room.summary.cleared,
		       room.summary.unconfirmed, (unsigned long)was, (unsigned long)remaining, (unsigned long)cleared, unconfirmed);
		return false;
	}
	return true;
}

static void test_fixtures(void)
{
	static dtc_result_t read_codes;
	char code[32];

	// A read with codes in four control units, one of them does not answer
	expect_begin(false, 34300, 5);
	for(int i = 0; i < 18; i++)
	{
		expect_unit(i);
		if(i == 1)
		{
			expect_code("P0100-13", "2F", 1);
			expect_code("P242F-FA", "68", 0);
		}
		if(i == 4) expect_code("U0100-87", "28", 0);
		if(i == 5) expect_code("9301", "60", -1);
		if(i == 16) expect_code("9100", "E0", -1);
	}
	expected.ecus[11].status = DTC_ECU_NO_RESPONSE;
	check(fixture_is_expected("dtc_result_read_codes.json"), "fixture dtc_result_read_codes.json: UDS and KWP codes, a silent control unit");
	check(text_is(box.result.ecus[11].short_name, 24, "Collision Prevention As"), "the short name of B162 is cut to the 23 bytes of its field");
	check(summary_is(4, 1, 13, 5), "summary of dtc_result_read_codes.json: 4 control units with codes, 1 not ok, 13 clean, 5 codes");
	read_codes = box.result;

	// The clear that followed: two codes are left, one control unit did not confirm
	expect_begin(true, 37500, 2);
	for(int i = 0; i < 18; i++)
	{
		expect_unit(i);
		if(i == 1 || i == 4 || i == 5) expected.ecus[i].cleared = 1;
		if(i == 16) expected.ecus[i].cleared = 0;
		if(i == 5) expect_code("9301", "60", -1);
		if(i == 16) expect_code("9100", "E0", -1);
	}
	expected.ecus[11].status = DTC_ECU_NO_RESPONSE;
	check(fixture_is_expected("dtc_result_clear.json"), "fixture dtc_result_clear.json: cleared true, false and absent");
	check(summary_is(2, 1, 15, 2), "summary of dtc_result_clear.json: 2 control units with codes, 1 not ok, 15 clean, 2 codes");
	check(clear_summary_is(&read_codes, &box.result, 5, 2, 3, 1),
	      "dtc_result_read_codes.json cleared to dtc_result_clear.json: 5 before, 2 remain, 3 cleared, 1 control unit unconfirmed");

	expect_begin(false, 34900, 0);
	for(int i = 0; i < 18; i++) expect_unit(i);
	check(fixture_is_expected("dtc_result_read_empty.json"), "fixture dtc_result_read_empty.json: 18 control units without codes");
	check(summary_is(0, 0, 18, 0), "summary of dtc_result_read_empty.json: 18 clean control units");

	// Shortened by the adapter: two lists replaced by their length
	expect_begin(false, 34900, 165);
	for(int i = 0; i < 18; i++)
	{
		expect_unit(i);
		if(i == 1) expected.ecus[i].omitted = 80;
		if(i == 4) expected.ecus[i].omitted = 75;
		if(i == 5)
		{
			expect_code("9301", "60", -1);
			expect_code("9302", "60", -1);
		}
		for(int k = 0; i == 6 && k < 8; k++)
		{
			snprintf(code, sizeof(code), "P019%d-11", k);
			expect_code(code, "28", 0);
		}
	}
	check(fixture_is_expected("dtc_result_shortened.json"), "fixture dtc_result_shortened.json: dtcs_omitted, dtc_count above the codes listed");
	check(summary_is(4, 0, 14, 165), "summary of dtc_result_shortened.json: 4 control units with codes, 10 listed and 155 omitted codes");

	// As large as the adapter sends it: 94 codes in 5112 of at most 5119 bytes
	expect_begin(false, 36100, 94);
	for(int i = 0; i < 18; i++)
	{
		expect_unit(i);
		for(int k = 1; i == 1 && k <= 25; k++)
		{
			snprintf(code, sizeof(code), "P01%02d-11", k);
			expect_code(code, "2F", 1);
		}
		for(int k = 1; i == 4 && k <= 10; k++)
		{
			snprintf(code, sizeof(code), "C10%02d-00", k);
			expect_code(code, "28", 0);
		}
		for(int k = 1; i == 5 && k <= 40; k++)
		{
			snprintf(code, sizeof(code), "90%02d", k);
			expect_code(code, "60", -1);
		}
		for(int k = 1; i == 16 && k <= 19; k++)
		{
			snprintf(code, sizeof(code), "91%02d", k);
			expect_code(code, "E0", -1);
		}
	}
	check(read_result_fixture("fixtures/dtc_model_largest.json") && strlen(fixture) > 5000 && strlen(fixture) <= 5119,
	      "fixture dtc_model_largest.json is about 5 KB and not longer than the adapter sends");
	check(fixture_is_expected("fixtures/dtc_model_largest.json"), "fixture dtc_model_largest.json: the largest result, nothing is cut");
	check(summary_is(4, 0, 14, 94), "summary of dtc_model_largest.json: 94 codes in 4 control units");
	// 11 tokens for the result, 11 for each of 18 control units, 7 for each of 35 UDS codes, 5 for each of 59 KWP codes
	check(parse_room(fixture, strlen(fixture), 749) && result_is(&expected), "the largest result needs 749 tokens");
	check(!parse_room(fixture, strlen(fixture), 748) && filled(&box.result, sizeof(box.result)), "refused: the largest result with room for 748 tokens");
}

/* Members ---------------------------------------------------------------------------------------------- */

static void test_members(void)
{
	// A value of every type of JSON
	static const member_t values[] = {{"1", 'n'}, {"\"1\"", 't'}, {"true", 'b'}, {"null", 0}, {"[]", 'a'}, {"{}", 0}};
	const char *json;

	expect_base();
	check(accepted(BASE) && result_is(&expected), "every member of a small result arrives in its field");

	for(int i = 0; i < COUNT(MEMBERS); i++)
	{
		char kind = MEMBERS[i].kind;

		json = variant(BASE, MEMBERS[i].member, renamed(MEMBERS[i].member));
		if(kind == 'B' || kind == 'N')
		{
			snprintf(what, sizeof(what), "accepted without %s, it may be absent", MEMBERS[i].member);
			check(accepted(json), what);
		}
		else
		{
			snprintf(what, sizeof(what), "refused: without %s", MEMBERS[i].member);
			check(refused(json), what);
		}
	}

	for(int i = 0; i < COUNT(MEMBERS); i++)
	{
		char kind = MEMBERS[i].kind;

		for(int v = 0; v < COUNT(values); v++)
		{
			// Any text will do for display and as a status. One of the few texts must not be "1" either.
			if(values[v].kind == 't' && (kind == 't' || kind == 's')) continue;
			if(values[v].kind == 'n' && (kind == 'n' || kind == 'N')) continue;
			if(values[v].kind == 'b' && kind == 'B') continue;
			if(values[v].kind == 'a' && kind == 'a') continue;

			snprintf(what, sizeof(what), "refused: %s is %s", MEMBERS[i].member, values[v].member);
			check(refused(base_with(MEMBERS[i].member, values[v].member)), what);
		}
	}

	// Members that may be absent
	expect_base();
	expected.ecus[0].cleared = -1;
	check(accepted(variant(BASE, "\"cleared\":true,", "")) && result_is(&expected), "without \"cleared\" the control unit has -1");
	expect_base();
	expected.ecus[0].cleared = 0;
	check(accepted(base_with("\"cleared\":true", "false")) && result_is(&expected), "\"cleared\":false is 0");
	expect_base();
	expected.ecus[1].cleared = 1;
	check(accepted(variant(BASE, "\"protocol\":\"KWP\",", "\"protocol\":\"KWP\",\"cleared\":true,")) && result_is(&expected),
	      "\"cleared\":true is 1, also in the second control unit");
	expect_base();
	expected.codes[0].active = -1;
	check(accepted(variant(BASE, ",\"active\":false", "")) && result_is(&expected), "without \"active\" the code has -1");
	expect_base();
	expected.codes[0].active = 1;
	check(accepted(base_with("\"active\":false", "true")) && result_is(&expected), "\"active\":true is 1");
	expect_base();
	expected.codes[1].active = 0;
	check(accepted(variant(BASE, "\"status\":\"60\"", "\"status\":\"60\",\"active\":false")) && result_is(&expected),
	      "\"active\":false is 0, also in a KWP control unit");
	expect_base();
	expected.ecus[0].omitted = 0;
	check(accepted(variant(BASE, ",\"dtcs_omitted\":2", "")) && result_is(&expected), "without \"dtcs_omitted\" nothing is omitted");
	expect_base();
	expected.ecus[1].omitted = 80;
	check(accepted(variant(BASE, "\"status\":\"60\"}]", "\"status\":\"60\"}],\"dtcs_omitted\":80")) && result_is(&expected),
	      "\"dtcs_omitted\" of the second control unit");

	// The few texts
	expect_base();
	expected.clear = false;
	check(accepted(base_with("\"action\":\"clear\"", "\"read\"")) && result_is(&expected), "action read: clear is false");
	expect_base();
	expected.ecus[0].uds = false;
	expected.ecus[1].uds = true;
	check(accepted(variant(base_with("\"protocol\":\"UDS\"", "\"KWP\""), "\"protocol\":\"KWP\",\"status\":\"incomplete\"",
	                       "\"protocol\":\"UDS\",\"status\":\"incomplete\"")) && result_is(&expected),
	      "the protocol of each control unit is its own");
	{
		static const struct
		{
			const char *member;
			const char *texts[6];
		} wrong[] = {
			{"\"state\":\"done\"", {"", "running", "error", "Done", "don", "done "}},
			{"\"action\":\"clear\"", {"", "scan", "Clear", "clea", "reads", "read_dtc"}},
			{"\"protocol\":\"UDS\"", {"", "uds", "OBD", "UD", "UDSS", "KW"}},
			{"\"protocol\":\"KWP\"", {"", "kwp", "CAN", "KW", "KWP2000", "UD"}},
		};

		for(int i = 0; i < COUNT(wrong); i++)
		{
			for(int t = 0; t < COUNT(wrong[i].texts); t++)
			{
				char quoted[32];

				snprintf(quoted, sizeof(quoted), "\"%s\"", wrong[i].texts[t]);
				snprintf(what, sizeof(what), "refused: %s with the text %s", wrong[i].member, quoted);
				check(refused(base_with(wrong[i].member, quoted)), what);
			}
		}
	}
	check(refused("{\"state\":\"running\",\"action\":\"read\",\"ecu\":5,\"total\":18}"), "refused: the progress message of a running scan");
	check(refused("{\"state\":\"error\",\"action\":\"clear\",\"reason\":\"engine_running\"}"), "refused: the message of a failed scan");

	// Unknown members anywhere, also with names of known ones inside them
	expect_base();
	json = variant(BASE, "{\"state\":\"done\",", "{\"new\":{\"state\":\"error\",\"ecus\":[],\"dtc_count\":9,\"list\":[1,[2]]},\"state\":\"done\",");
	check(accepted(json) && result_is(&expected), "an unknown member before the known ones is ignored, also what is inside it");
	json = variant(BASE, "\"id\":\"7E0\",", "\"id\":\"7E0\",\"rx\":\"7E8\",\"mask\":45,\"more\":{\"cleared\":false,\"dtcs_omitted\":9,\"dtcs\":[1]},");
	check(accepted(json) && result_is(&expected), "unknown members of a control unit are ignored");
	json = variant(BASE, "{\"code\":\"9301\",", "{\"text\":null,\"sub\":{\"active\":true,\"code\":\"x\"},\"code\":\"9301\",");
	check(accepted(json) && result_is(&expected), "unknown members of a code are ignored");
	json = variant(BASE, "}]}]}", "}],\"last\":[]}],\"end\":true}");
	check(accepted(json) && result_is(&expected), "unknown members at the end of a control unit and of the result are ignored");

	// A member counts only where the format has it
	json = variant(variant(BASE, "\"dtc_count\":4,", ""), "\"id\":\"7E0\",", "\"id\":\"7E0\",\"dtc_count\":4,");
	check(refused(json), "refused: a member of the result inside a control unit");
	json = variant(variant(BASE, "\"id\":\"662\",", ""), "\"code\":\"9301\",", "\"code\":\"9301\",\"id\":\"662\",");
	check(refused(json), "refused: a member of a control unit inside its code");
	expect_base();
	expected.ecus[0].omitted = 0;
	json = variant(variant(BASE, ",\"dtcs_omitted\":2", ""), "\"dtc_count\":4,", "\"dtc_count\":4,\"dtcs_omitted\":2,\"cleared\":false,\"active\":true,");
	check(accepted(json) && result_is(&expected), "members of a control unit and of a code are not looked for in the result");

	// Lists
	expect_begin(true, 31500, 4);
	check(accepted(variant(BASE, "\"ecus\":[", "\"ecus\":[],\"x\":[")) && result_is(&expected), "a result without control units");
	expect_base();
	expected.code_count = 1;
	expected.ecus[1].code_count = 0;
	check(accepted(variant(BASE, "[{\"code\":\"9301\",\"status\":\"60\"}]", "[]")) && result_is(&expected), "a control unit without codes");
	check(refused(variant(BASE, "{\"name\":\"N10 SAM\"", "1,{\"name\":\"N10 SAM\"")), "refused: a number among the control units");
	check(refused(variant(BASE, "{\"code\":\"9301\"", "\"9301\",{\"code\":\"9301\"")), "refused: a text among the codes");
	check(refused(variant(BASE, "{\"code\":\"9301\",\"status\":\"60\"}", "[{\"code\":\"9301\",\"status\":\"60\"}]")), "refused: a list among the codes");
}

/* Numbers ---------------------------------------------------------------------------------------------- */

static void test_numbers(void)
{
	static const char *const wrong[] = {
		"-1", "-2147483648", "0.5", "4.0", "4.5", "4e0", "1E1", "4294967296", "4294967300", "9223372036854775807",
		"18446744073709551616", "99999999999999999999999999",
	};
	static const struct
	{
		const char *member;
		uint32_t *field;
	} numbers[] = {
		{"\"duration_ms\":31500", &box.result.duration_ms}, {"\"dtc_count\":4", &box.result.dtc_count},
		{"\"dtcs_omitted\":2", &box.result.ecus[0].omitted},
	};

	for(int i = 0; i < COUNT(numbers); i++)
	{
		snprintf(what, sizeof(what), "%s may be 0", numbers[i].member);
		check(accepted(base_with(numbers[i].member, "0")) && *numbers[i].field == 0, what);
		snprintf(what, sizeof(what), "%s may be -0: minus zero is zero, a number from 0 to 2^32-1", numbers[i].member);
		check(accepted(base_with(numbers[i].member, "-0")) && *numbers[i].field == 0, what);
		snprintf(what, sizeof(what), "%s may be 2^31", numbers[i].member);
		check(accepted(base_with(numbers[i].member, "2147483648")) && *numbers[i].field == 2147483648u, what);
		snprintf(what, sizeof(what), "%s may be 2^32-1", numbers[i].member);
		check(accepted(base_with(numbers[i].member, "4294967295")) && *numbers[i].field == 4294967295u, what);
		for(int v = 0; v < COUNT(wrong); v++)
		{
			snprintf(what, sizeof(what), "refused: %s with the value %s", numbers[i].member, wrong[v]);
			check(refused(base_with(numbers[i].member, wrong[v])), what);
		}
	}
}

/* Status of a control unit ----------------------------------------------------------------------------- */

static bool status_is(const char *text, dtc_ecu_status_t status, int nrc)
{
	char quoted[64];

	snprintf(quoted, sizeof(quoted), "\"%s\"", text);
	if(!accepted(base_with("\"status\":\"ok\"", quoted))) return false;
	if(box.result.ecus[0].status != status || box.result.ecus[0].nrc != nrc)
	{
		printf("  status \"%s\" is %d with nrc %d, expected %d with %d\n", text, (int)box.result.ecus[0].status, box.result.ecus[0].nrc, (int)status, nrc);
		return false;
	}
	return true;
}

static void test_status(void)
{
	static const struct
	{
		const char *text;
		dtc_ecu_status_t status;
		int nrc;
	} cases[] = {
		{"ok", DTC_ECU_OK, 0}, {"no_response", DTC_ECU_NO_RESPONSE, 0}, {"pending_timeout", DTC_ECU_PENDING_TIMEOUT, 0},
		{"incomplete", DTC_ECU_INCOMPLETE, 0},
		{"nrc_22", DTC_ECU_NRC, 0x22}, {"nrc_33", DTC_ECU_NRC, 0x33}, {"nrc_7F", DTC_ECU_NRC, 0x7F}, {"nrc_00", DTC_ECU_NRC, 0},
		{"nrc_FF", DTC_ECU_NRC, 0xFF}, {"nrc_ff", DTC_ECU_NRC, 0xFF}, {"nrc_a9", DTC_ECU_NRC, 0xA9}, {"nrc_9A", DTC_ECU_NRC, 0x9A},
		{"nrc_10", DTC_ECU_NRC, 0x10}, {"nrc_01", DTC_ECU_NRC, 0x01},
		{"", DTC_ECU_OTHER, 0}, {"o", DTC_ECU_OTHER, 0}, {"OK", DTC_ECU_OTHER, 0}, {"okay", DTC_ECU_OTHER, 0}, {"ok ", DTC_ECU_OTHER, 0},
		{"no_respons", DTC_ECU_OTHER, 0}, {"no_response_", DTC_ECU_OTHER, 0}, {"timeout", DTC_ECU_OTHER, 0},
		{"pending_timeouts", DTC_ECU_OTHER, 0}, {"incomplet", DTC_ECU_OTHER, 0}, {"busy", DTC_ECU_OTHER, 0},
		{"nrc_", DTC_ECU_OTHER, 0}, {"nrc_2", DTC_ECU_OTHER, 0}, {"nrc_222", DTC_ECU_OTHER, 0}, {"nrc_2222", DTC_ECU_OTHER, 0},
		{"nrc22", DTC_ECU_OTHER, 0}, {"nrc-22", DTC_ECU_OTHER, 0}, {"NRC_22", DTC_ECU_OTHER, 0}, {"nrd_22", DTC_ECU_OTHER, 0},
		{"xrc_22", DTC_ECU_OTHER, 0}, {"nxc_22", DTC_ECU_OTHER, 0}, {"nrc_2G", DTC_ECU_OTHER, 0}, {"nrc_G2", DTC_ECU_OTHER, 0},
		{"nrc_ 2", DTC_ECU_OTHER, 0}, {"nrc_2 ", DTC_ECU_OTHER, 0}, {"_nrc_22", DTC_ECU_OTHER, 0}, {"nrc_22_", DTC_ECU_OTHER, 0},
		{"nrc_0x", DTC_ECU_OTHER, 0}, {"nrc_-1", DTC_ECU_OTHER, 0},
	};
	static const char hex[] = "0123456789ABCDEFabcdef";
	char text[16] = "nrc_00";
	bool same = true;

	for(int i = 0; i < COUNT(cases); i++)
	{
		snprintf(what, sizeof(what), "status \"%s\" is %s%s", cases[i].text,
		         cases[i].status == DTC_ECU_OTHER ? "an unknown one" : cases[i].status == DTC_ECU_NRC ? "a negative response" : "known",
		         cases[i].status != DTC_ECU_NRC ? ", nrc is 0" : "");
		check(status_is(cases[i].text, cases[i].status, cases[i].nrc), what);
	}

	// Every character that can stand in a text without escape, in both places of the number
	for(int place = 4; place <= 5; place++)
	{
		for(int c = 0x20; c < 0x7F && same; c++)
		{
			const char *found = strchr(hex, c);
			int digit = found == NULL ? -1 : found - hex < 16 ? (int)(found - hex) : (int)(found - hex) - 6;

			if(c == '"' || c == '\\') continue;
			strcpy(text, "nrc_11");
			text[place] = (char)c;
			same = digit < 0 ? status_is(text, DTC_ECU_OTHER, 0) : status_is(text, DTC_ECU_NRC, place == 4 ? digit * 16 + 1 : 16 + digit);
		}
	}
	check(same, "nrc_XX: only the digits 0-9, A-F and a-f make a negative response, each with its value");

	for(int nrc = 0; nrc < 256 && same; nrc++)
	{
		snprintf(text, sizeof(text), "nrc_%02X", nrc);
		same = status_is(text, DTC_ECU_NRC, nrc);
	}
	check(same, "nrc_00 to nrc_FF as the adapter writes them become their number");

	for(int high = 0; high < 22 && same; high++)
	{
		for(int low = 0; low < 22 && same; low++)
		{
			strcpy(text, "nrc_00");
			text[4] = hex[high];
			text[5] = hex[low];
			same = status_is(text, DTC_ECU_NRC, (high < 16 ? high : high - 6) * 16 + (low < 16 ? low : low - 6));
		}
	}
	check(same, "nrc_XX with every pair of the digits 0-9, A-F and a-f, capital and small letters mixed, becomes its number");

	check(status_is("\\u006fk", DTC_ECU_OK, 0), "an escape in a status is resolved before it is compared");
	check(status_is("nrc_\\u0032\\u0032", DTC_ECU_NRC, 0x22), "an escape in the number of a negative response is resolved");
	check(status_is("nrc_22\\u0000", DTC_ECU_OTHER, 0), "a status json_text() refuses is an unknown one");
}

/* Texts for display ------------------------------------------------------------------------------------ */

// The texts of the second control unit of BASE and its code, where they arrive and how large the field is
// according to the header
typedef struct
{
	const char *member;
	char *field;
	size_t size;
} text_t;

static const text_t TEXTS[] = {
	{"\"name\":\"N10 SAM\"", box.result.ecus[1].name, 64}, {"\"id\":\"662\"", box.result.ecus[1].id, 8},
	{"\"code\":\"9301\"", box.result.codes[1].code, 16}, {"\"status\":\"60\"", box.result.codes[1].status, 8},
	{"\"name\":\"N3/28 Motorelektronik (CDID3)\"", box.result.ecus[0].name, 64}, {"\"id\":\"7E0\"", box.result.ecus[0].id, 8},
	{"\"code\":\"P242F-FA\"", box.result.codes[0].code, 16}, {"\"status\":\"68\"", box.result.codes[0].status, 8},
};

// BASE with another text, given as it stands in JSON, for one of the members of TEXTS. NULL if it does not fit.
static const char *base_with_text(const text_t *text, const char *json_text)
{
	static char value[4096];

	if(snprintf(value, sizeof(value), "\"%s\"", json_text) >= (int)sizeof(value)) return NULL;
	return base_with(text->member, value);
}

// The field holds exactly these bytes and ends inside itself
static bool field_is(const text_t *text, const char *bytes, size_t length)
{
	return memchr(text->field, '\0', text->size) != NULL && strlen(text->field) == length && memcmp(text->field, bytes, length) == 0;
}

// Characters as they stand in JSON and as UTF-8, of every length and every kind of escape
static const struct
{
	const char *json;
	const char *utf8;
} CHARACTERS[] = {
	{"a", "a"}, {"Z", "Z"}, {" ", " "}, {"-", "-"}, {"\x7F", "\x7F"},
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
	static char bytes[4096];
	const text_t *name = &TEXTS[0];
	const text_t *code = &TEXTS[2];
	bool same;

	for(size_t i = 0; i < sizeof(long_text) - 1; i++) long_text[i] = (char)('a' + i % 26);

	for(int i = 0; i < COUNT(TEXTS); i++)
	{
		const text_t *text = &TEXTS[i];
		size_t room = text->size - 1;
		static char value[1024];

		snprintf(what, sizeof(what), "%s may be empty", text->member);
		check(accepted(base_with_text(text, "")) && field_is(text, "", 0), what);

		snprintf(value, sizeof(value), "%.*s", (int)(room - 1), long_text);
		snprintf(what, sizeof(what), "%s of %d bytes is kept whole", text->member, (int)(room - 1));
		check(accepted(base_with_text(text, value)) && field_is(text, long_text, room - 1), what);

		snprintf(value, sizeof(value), "%.*s", (int)room, long_text);
		snprintf(what, sizeof(what), "%s of %d bytes fills its field and is kept whole", text->member, (int)room);
		check(accepted(base_with_text(text, value)) && field_is(text, long_text, room), what);

		snprintf(value, sizeof(value), "%.*s", (int)(room + 1), long_text);
		snprintf(what, sizeof(what), "%s of %d bytes is cut to %d, the result is accepted", text->member, (int)(room + 1), (int)room);
		check(accepted(base_with_text(text, value)) && field_is(text, long_text, room), what);

		snprintf(what, sizeof(what), "%s of 1023 bytes is cut to %d", text->member, (int)room);
		check(accepted(base_with_text(text, long_text)) && field_is(text, long_text, room), what);
	}

	// A character of 2, 3 and 4 bytes at every position around the end of every field
	for(int i = 0; i < COUNT(TEXTS); i++)
	{
		static const char *const characters[] = {"\xC3\xBC", "\xE2\x82\xAC", "\xF0\x9F\x98\x80"};
		const text_t *text = &TEXTS[i];
		size_t room = text->size - 1;

		for(int c = 0; c < COUNT(characters); c++)
		{
			size_t length = strlen(characters[c]);

			same = true;
			for(size_t before = room - 6; before <= room + 1; before++)
			{
				// Whole if it ends inside the field, else gone: with the letter behind it, or without
				size_t kept = before + length <= room ? before + length : before;

				if(kept > room) kept = room;
				snprintf(json_text, sizeof(json_text), "%.*s%sz", (int)before, long_text, characters[c]);
				snprintf(bytes, sizeof(bytes), "%.*s%s", (int)before, long_text, characters[c]);
				if(before + length + 1 <= room)
				{
					strcat(bytes, "z");
					kept++;
				}
				if(!accepted(base_with_text(text, json_text)) || !field_is(text, bytes, kept))
				{
					printf("  %d bytes before the character: not the first %d bytes\n", (int)before, (int)kept);
					same = false;
				}
			}
			snprintf(what, sizeof(what), "%s: a character of %d bytes at the end of the field is kept whole or left out",
			         text->member, (int)length);
			check(same, what);
		}
	}

	// Escapes count with the bytes they stand for
	check(accepted(base_with_text(name, "a\\\"b\\\\c\\/d\\u00fc\\uD83D\\uDE00")) && field_is(name, "a\"b\\c/d\xC3\xBC\xF0\x9F\x98\x80", 13),
	      "escapes in a text are resolved");
	check(accepted(base_with_text(name, "x\\uD83D\\uDE00")) && field_is(name, "x\xF0\x9F\x98\x80", 5),
	      "a surrogate pair at the very end of a text is one character");
	for(int i = 0; i < 64; i++) strcpy(json_text + 2 * i, "\\\\");
	memset(bytes, '\\', 64);
	check(accepted(base_with_text(name, json_text)) && field_is(name, bytes, 63), "64 escaped backslashes are 64 bytes: cut to 63");
	json_text[2 * 63] = '\0';
	check(accepted(base_with_text(name, json_text)) && field_is(name, bytes, 63),
	      "63 escaped backslashes are 63 bytes although they are 126 in JSON: kept whole");
	for(int i = 0; i < 8; i++)
	{
		strcpy(json_text + 6 * i, "\\u00fc");
		strcpy(bytes + 2 * i, "\xC3\xBC");
	}
	check(accepted(base_with_text(code, json_text)) && field_is(code, bytes, 14),
	      "8 escaped characters of 2 bytes: 7 fit into the 15 bytes of a code, the last is not cut in half");
	for(int i = 0; i < 4; i++)
	{
		strcpy(json_text + 12 * i, "\\uD83D\\uDE00");
		strcpy(bytes + 4 * i, "\xF0\x9F\x98\x80");
	}
	check(accepted(base_with_text(code, json_text)) && field_is(code, bytes, 12), "4 surrogate pairs of 4 bytes: 3 fit into 15 bytes");

	// What json_text() refuses ends the text, the result stays valid
	expect_base();
	strcpy(expected.ecus[0].name, "ab");
	strcpy(expected.ecus[0].short_name, "ab");
	check(accepted(base_with_text(&TEXTS[4], "ab\\u0000cd")) && result_is(&expected),
	      "a text ends before \\u0000, everything behind it is read as usual");
	check(accepted(base_with_text(name, "\\u0000")) && field_is(name, "", 0), "a text of \\u0000 alone is empty");
	check(accepted(base_with_text(name, "ab\\uD83Dcd")) && field_is(name, "ab", 2),
	      "a text ends before the first half of a surrogate pair without the second");
	check(accepted(base_with_text(name, "ab\\uD83D")) && field_is(name, "ab", 2), "a text ends before half a surrogate pair at its end");
	check(accepted(base_with_text(name, "ab\\uD83D\\u0041cd")) && field_is(name, "ab", 2),
	      "a text ends before a first half followed by another character");
	check(accepted(base_with_text(name, "ab\\uDE00\\uD83Dcd")) && field_is(name, "ab", 2),
	      "a text ends before the second half of a surrogate pair alone");

	// Bytes that are no UTF-8 must not lead anywhere: the field ends inside itself, the result is accepted
	check(accepted(base_with_text(name, "a\xFF" "b\x80")) && field_is(name, "a\xFF" "b\x80", 4), "bytes that are no UTF-8 are passed on if they fit");
	memset(json_text, 0x80, 200);
	json_text[200] = '\0';
	check(accepted(base_with_text(name, json_text)) && field_is(name, "", 0),
	      "200 continuation bytes have no character boundary but the beginning of the text: cut to nothing");
	json_text[64] = '\0';
	check(accepted(base_with_text(name, json_text)) && field_is(name, "", 0), "64 continuation bytes, one more than fit: cut to nothing");
	json_text[63] = '\0';
	check(accepted(base_with_text(name, json_text)) && field_is(name, json_text, 63), "63 continuation bytes fit and are passed on");
	memset(json_text, 0xF0, 200);
	check(accepted(base_with_text(code, json_text)) && field_is(code, json_text, 15), "200 first bytes of characters: each stands alone, 15 fit");

	// The short name is made of the name as it was read
	expect_base();
	strcpy(expected.ecus[1].name, "N14 T\xC3\xBCr \"links\" (T\\L)");
	strcpy(expected.ecus[1].short_name, "T\\L");
	check(accepted(base_with_text(name, "N14 T\\u00fcr \\\"links\\\" (T\\\\L)")) && result_is(&expected),
	      "the short name comes from the name with its escapes resolved");
	expect_base();
	snprintf(expected.ecus[1].name, sizeof(expected.ecus[1].name), "N1 %.60s", long_text);
	snprintf(expected.ecus[1].short_name, sizeof(expected.ecus[1].short_name), "%.23s", long_text);
	snprintf(json_text, sizeof(json_text), "N1 %.70s (X)", long_text);
	check(accepted(base_with_text(name, json_text)) && result_is(&expected),
	      "a name cut to 63 bytes gives the short name of what is left, cut to 23 bytes");
	expect_base();
	strcpy(expected.ecus[1].name, "N1 abcdefghijklmnopqrstuv\xC3\xBCx");
	strcpy(expected.ecus[1].short_name, "abcdefghijklmnopqrstuv");
	check(accepted(base_with_text(name, "N1 abcdefghijklmnopqrstuv\xC3\xBCx")) && result_is(&expected),
	      "a short name of 22 letters and an umlaut loses the umlaut in the 23 bytes of its field, not half of it");

	// Texts of random characters in every field: as many whole characters as fit, counted one by one
	same = true;
	for(int i = 0; i < 2000 && same; i++)
	{
		const text_t *text = &TEXTS[random_below(COUNT(TEXTS))];
		int characters = (int)random_below(40);
		size_t kept = 0;
		bool cut = false;

		json_text[0] = '\0';
		bytes[0] = '\0';
		for(int c = 0; c < characters; c++)
		{
			int pick = (int)random_below(COUNT(CHARACTERS));

			strcat(json_text, CHARACTERS[pick].json);
			if(!cut && kept + strlen(CHARACTERS[pick].utf8) <= text->size - 1)
			{
				strcat(bytes, CHARACTERS[pick].utf8);
				kept += strlen(CHARACTERS[pick].utf8);
			}
			else
			{
				cut = true;
			}
		}
		if(!accepted(base_with_text(text, json_text)) || !field_is(text, bytes, kept))
		{
			printf("  %s with the text %s\n", text->member, json_text);
			same = false;
		}
	}
	check(same, "2000 texts of random characters and escapes: as many whole characters as fit into the field");
}

/* Made-up results: more than there is room for --------------------------------------------------------- */

static char made[131072];
static size_t made_length;

static void put(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	made_length += (size_t)vsnprintf(made + made_length, sizeof(made) - made_length, format, arguments);
	va_end(arguments);
	if(made_length >= sizeof(made))
	{
		printf("  the made-up result does not fit\n");
		exit(2);
	}
}

static const struct
{
	const char *text;
	dtc_ecu_status_t status;
	uint8_t nrc;
} STATUSES[] = {
	{"ok", DTC_ECU_OK, 0}, {"ok", DTC_ECU_OK, 0}, {"ok", DTC_ECU_OK, 0}, {"no_response", DTC_ECU_NO_RESPONSE, 0},
	{"pending_timeout", DTC_ECU_PENDING_TIMEOUT, 0}, {"incomplete", DTC_ECU_INCOMPLETE, 0}, {"nrc_22", DTC_ECU_NRC, 0x22},
	{"nrc_7F", DTC_ECU_NRC, 0x7F}, {"nrc_a0", DTC_ECU_NRC, 0xA0}, {"busy", DTC_ECU_OTHER, 0}, {"", DTC_ECU_OTHER, 0},
	{"nrc_123", DTC_ECU_OTHER, 0},
};

// What a made-up result adds up to, counted while it is made: the model for the summaries
static struct
{
	int with_codes, not_ok, clean, unconfirmed;
	uint64_t codes;
} made_sum;

// A result of `units` control units with codes[i] codes in unit i: as text in `made`, and in `expected` what
// has to arrive of it, with what does not fit left out. Plain: the W906 units in turn, status ok, UDS and KWP
// in turn. Varied: everything else chosen at random too.
static void make_result(int units, const int *codes, bool varied)
{
	int sequence = 0;
	uint32_t dtc_count = varied ? random_below(1000) : 7;
	uint32_t duration_ms = varied ? random_below(100000) : 31500;
	bool clear = varied && random_below(2) == 1;

	made_length = 0;
	memset(&made_sum, 0, sizeof(made_sum));
	expect_begin(clear, duration_ms, dtc_count);
	put("{\"state\":\"done\",\"action\":\"%s\",\"duration_ms\":%lu,\"dtc_count\":%lu,\"ecus\":[", expected.clear ? "clear" : "read",
	    (unsigned long)expected.duration_ms, (unsigned long)expected.dtc_count);

	for(int u = 0; u < units; u++)
	{
		const unit_t *unit = &UNITS[varied ? random_below(COUNT(UNITS)) : (uint32_t)u % COUNT(UNITS)];
		int status = varied ? (int)random_below(COUNT(STATUSES)) : 0;
		bool uds = varied ? random_below(2) == 1 : u % 2 == 0;
		int cleared = varied ? (int)random_below(3) - 1 : -1;
		bool has_omitted = varied && random_below(4) == 0;
		uint32_t omitted = !has_omitted ? 0 : random_below(8) == 0 ? 4294967295u - random_below(3) : random_below(300);
		bool room = u < DTC_ECUS_MAX;
		dtc_ecu_t *ecu = NULL;
		char id[32];

		snprintf(id, sizeof(id), "%03X", 0x600 + u);
		if(room)
		{
			ecu = expect_ecu(unit->name, unit->short_name, id, uds);
			ecu->status = STATUSES[status].status;
			ecu->nrc = STATUSES[status].nrc;
			ecu->cleared = (int8_t)cleared;
			ecu->omitted = omitted;
		}
		else
		{
			expected.cut = true;
		}

		put("%s{\"name\":\"%s\",\"id\":\"%s\",\"protocol\":\"%s\",", u > 0 ? "," : "", unit->name, id, uds ? "UDS" : "KWP");
		if(cleared >= 0) put("\"cleared\":%s,", cleared ? "true" : "false");
		put("\"status\":\"%s\",\"dtcs\":[", STATUSES[status].text);
		for(int k = 0; k < codes[u]; k++)
		{
			int active = varied ? (int)random_below(3) - 1 : uds ? sequence % 2 : -1;
			char code[32], code_status[32];

			snprintf(code, sizeof(code), "C%04d-%02X", sequence, (unsigned)(sequence * 7) & 0xFFu);
			snprintf(code_status, sizeof(code_status), "%02X", (unsigned)(sequence * 3) & 0xFFu);
			put("%s{\"code\":\"%s\",\"status\":\"%s\"", k > 0 ? "," : "", code, code_status);
			if(active >= 0) put(",\"active\":%s", active ? "true" : "false");
			put("}");
			if(room && expected.code_count < DTC_CODES_MAX)
			{
				expect_code(code, code_status, active);
			}
			else
			{
				expected.cut = true;
			}
			sequence++;
		}
		put("]");
		if(has_omitted) put(",\"dtcs_omitted\":%lu", (unsigned long)omitted);
		put("}");

		if(room)
		{
			bool with_codes = ecu->code_count > 0 || omitted > 0;

			if(with_codes) made_sum.with_codes++;
			if(ecu->status != DTC_ECU_OK) made_sum.not_ok++;
			else if(!with_codes) made_sum.clean++;
			if(cleared == 0) made_sum.unconfirmed++;
			made_sum.codes += (uint64_t)ecu->code_count + omitted;
		}
	}
	put("]}");
}

static bool made_is_expected(void)
{
	return parse_room(made, made_length, WORK_TOKENS) && result_is(&expected);
}

static void test_limits(void)
{
	static int codes[256];
	int shortest;
	bool same = true;

	check(DTC_ECUS_MAX == 24 && DTC_CODES_MAX == 128, "there is room for 24 control units and 128 codes");

	// Control units. Each has a code, so that one written behind the room would show in the codes.
	for(int i = 0; i < COUNT(codes); i++) codes[i] = 1;
	make_result(23, codes, false);
	check(made_is_expected() && box.result.ecu_count == 23 && !box.result.cut, "23 control units: all of them, nothing is cut");
	make_result(24, codes, false);
	check(made_is_expected() && box.result.ecu_count == 24 && box.result.code_count == 24 && !box.result.cut,
	      "24 control units fill the room: all of them, nothing is cut");
	make_result(25, codes, false);
	check(made_is_expected() && box.result.ecu_count == 24 && box.result.code_count == 24 && box.result.cut,
	      "25 control units: the first 24 and their codes, the result is marked as cut");
	codes[24] = 0;
	make_result(25, codes, false);
	check(made_is_expected() && box.result.ecu_count == 24 && box.result.cut, "25 control units, the last without codes: marked as cut");
	codes[24] = 1;
	make_result(60, codes, false);
	check(made_is_expected() && box.result.ecu_count == 24 && box.result.code_count == 24 && box.result.cut,
	      "60 control units: the first 24 and their codes");
	check(refused(variant(made, "\"id\":\"63B\",", "")), "refused: the 60th of 60 control units has no id, although it is left out");
	check(refused(variant(made, "\"code\":\"C0059-9D\",", "")), "refused: the code of the 60th control unit has no number, although it is left out");

	// More control units than DTC_RESULT_TOKENS hold, 11 tokens each: every one of them is still checked
	memset(codes, 0, sizeof(codes));
	make_result(200, codes, false);
	check(made_is_expected() && box.result.ecu_count == 24 && box.result.code_count == 0 && box.result.cut,
	      "200 control units without codes: the first 24");
	check(refused(variant(made, "\"id\":\"6C7\",", "")), "refused: the 200th of 200 control units has no id, although it is left out");

	// Codes
	memset(codes, 0, sizeof(codes));
	codes[0] = 127;
	make_result(2, codes, false);
	check(made_is_expected() && box.result.code_count == 127 && !box.result.cut, "127 codes in one control unit: all of them");
	codes[0] = 128;
	make_result(2, codes, false);
	check(made_is_expected() && box.result.code_count == 128 && box.result.ecus[0].code_count == 128 && !box.result.cut,
	      "128 codes fill the room: all of them, nothing is cut");
	codes[0] = 129;
	make_result(2, codes, false);
	check(made_is_expected() && box.result.code_count == 128 && box.result.ecus[0].code_count == 128 && box.result.cut,
	      "129 codes in one control unit: the first 128, the result is marked as cut");
	check(box.result.ecu_count == 2 && box.result.ecus[1].code_count == 0, "the control unit behind the one that was cut is still listed");
	check(refused(variant(made, "\"code\":\"C0128-80\",", "")), "refused: the 129th code has no number, although it is left out");
	codes[0] = 100;
	codes[1] = 28;
	codes[2] = 0;
	codes[3] = 1;
	make_result(5, codes, false);
	check(made_is_expected() && box.result.code_count == 128 && box.result.cut && box.result.ecus[3].code_count == 0 &&
	      box.result.ecu_count == 5, "128 codes in two control units and one more in the fourth: that one is left out, marked as cut");
	codes[3] = 0;
	make_result(5, codes, false);
	check(made_is_expected() && box.result.code_count == 128 && !box.result.cut, "128 codes in two control units and none behind: not cut");
	codes[0] = 120;
	codes[1] = 20;
	codes[2] = 5;
	make_result(4, codes, false);
	check(made_is_expected() && box.result.ecus[1].code_count == 8 && box.result.ecus[1].first_code == 120 &&
	      box.result.ecus[2].code_count == 0 && box.result.cut,
	      "the room ends inside the second control unit: 8 of its 20 codes, none of the third");
	codes[0] = 500;
	codes[1] = 3;
	make_result(2, codes, false);
	check(made_is_expected() && box.result.code_count == 128 && box.result.cut, "500 codes in one control unit: the first 128");
	// 11 tokens for the result, 11 for each control unit, 7 for each of 500 UDS codes, 5 for each of 3 KWP codes
	check(parse_room(made, made_length, 3548) && result_is(&expected),
	      "500 codes need 3548 tokens, more than DTC_RESULT_TOKENS: accepted with room for them, all the room that is given is used");
	check(!parse_room(made, made_length, 3547) && filled(&box.result, sizeof(box.result)), "refused: 500 codes with room for 3547 tokens");
	check(!parse_room(made, made_length, DTC_RESULT_TOKENS) && filled(&box.result, sizeof(box.result)),
	      "refused: 500 codes with room for DTC_RESULT_TOKENS, they need more");
	check(refused(variant(made, "\"code\":\"C0499-A5\",", "")), "refused: the last of 500 codes has no number, although it is left out");

	// Both at once
	for(int i = 0; i < COUNT(codes); i++) codes[i] = 5;
	make_result(30, codes, false);
	check(made_is_expected() && box.result.ecu_count == 24 && box.result.code_count == 120 && box.result.cut,
	      "30 control units of 5 codes: 24 control units and their 120 codes");
	for(int i = 0; i < COUNT(codes); i++) codes[i] = 6;
	make_result(30, codes, false);
	check(made_is_expected() && box.result.ecu_count == 24 && box.result.code_count == 128 && box.result.cut,
	      "30 control units of 6 codes: 24 control units and 128 codes");

	// As many codes as 5119 bytes can hold if every text is empty: the room the header promises is enough for them.
	// Nothing the format knows has more tokens per byte than these 5 in 24 bytes.
	made_length = 0;
	shortest = 1;
	put("{\"state\":\"done\",\"action\":\"read\",\"duration_ms\":0,\"dtc_count\":0,\"ecus\":[{\"name\":\"\",\"id\":\"\",\"protocol\":\"KWP\","
	    "\"status\":\"ok\",\"dtcs\":[{\"code\":\"\",\"status\":\"\"}");
	while(made_length + 24 + 4 <= 5119)
	{
		put(",{\"code\":\"\",\"status\":\"\"}");
		shortest++;
	}
	put("]}]}");
	check(made_length > 5119 - 24 && made_length <= 5119 && parse_room(made, made_length, DTC_RESULT_TOKENS) &&
	      box.result.ecu_count == 1 && box.result.code_count == 128 && box.result.cut,
	      "DTC_RESULT_TOKENS are enough for 5119 bytes of the shortest codes the format allows");
	// 151 bytes up to the first code, 24 for each further one, 4 at the end; 11 tokens for the result, 11 for the control unit
	check(shortest == 207 && made_length == 5099 && parse_room(made, made_length, 22 + 5 * 207) && box.result.code_count == 128,
	      "these are 207 codes in 5099 bytes and 1057 tokens");
	check(!parse_room(made, made_length, 22 + 5 * 207 - 1) && filled(&box.result, sizeof(box.result)), "refused: the 207 shortest codes with room for 1056 tokens");

	// Results chosen at random against what was noted while they were made
	for(int i = 0; i < 400 && same; i++)
	{
		int units = (int)random_below(31);
		int total = 0;

		// Not more than the tokens of this test hold
		for(int u = 0; u < units; u++)
		{
			codes[u] = random_below(2) == 0 ? 0 : random_below(8) == 0 ? (int)random_below(60) : (int)random_below(9);
			if(total + codes[u] > 450) codes[u] = 0;
			total += codes[u];
		}
		make_result(units, codes, true);
		same = made_is_expected() &&
		       summary_is(made_sum.with_codes, made_sum.not_ok, made_sum.clean, made_sum.codes > 4294967295u ? 4294967295u : (uint32_t)made_sum.codes);
		if(!same) printf("  result %d of %d control units\n", i, units);
	}
	check(same, "400 results chosen at random: every field, the cut and the summary are as noted while they were made");
}

/* Summaries -------------------------------------------------------------------------------------------- */

// Results put together by hand, field by field
static dtc_result_t first, second;

static void unit_of(dtc_result_t *result, dtc_ecu_status_t status, int codes, uint32_t omitted, int cleared)
{
	dtc_ecu_t *ecu = &result->ecus[result->ecu_count++];

	ecu->status = status;
	ecu->first_code = (uint16_t)result->code_count;
	ecu->code_count = (uint16_t)codes;
	ecu->omitted = omitted;
	ecu->cleared = (int8_t)cleared;
	result->code_count += codes;
}

// The summary of a result made by hand
static bool summary_of(const dtc_result_t *result, int with_codes, int not_ok, int clean, uint32_t codes)
{
	box.result = *result;
	return summary_is(with_codes, not_ok, clean, codes);
}

static void test_summaries(void)
{
	static const dtc_ecu_status_t not_ok[] = {DTC_ECU_NO_RESPONSE, DTC_ECU_PENDING_TIMEOUT, DTC_ECU_INCOMPLETE, DTC_ECU_NRC, DTC_ECU_OTHER};
	bool same = true;

	memset(&first, 0, sizeof(first));
	check(summary_of(&first, 0, 0, 0, 0), "summary of a result without control units: nothing");

	unit_of(&first, DTC_ECU_OK, 0, 0, -1);
	check(summary_of(&first, 0, 0, 1, 0), "a control unit that is ok and has no code is clean");
	unit_of(&first, DTC_ECU_OK, 2, 0, -1);
	check(summary_of(&first, 1, 0, 1, 2), "a control unit with two listed codes has codes and is not clean");
	unit_of(&first, DTC_ECU_OK, 0, 7, -1);
	check(summary_of(&first, 2, 0, 1, 9), "a control unit with omitted codes only has codes, they count");
	unit_of(&first, DTC_ECU_NO_RESPONSE, 0, 0, -1);
	check(summary_of(&first, 2, 1, 1, 9), "a control unit that is not ok and has no code is neither clean nor one with codes");
	unit_of(&first, DTC_ECU_INCOMPLETE, 3, 1, -1);
	check(summary_of(&first, 3, 2, 1, 13), "a control unit that is not ok and has codes counts as both, listed plus omitted codes");
	unit_of(&first, DTC_ECU_OK, 0, 0, 1);
	check(summary_of(&first, 3, 2, 2, 13), "a second clean control unit");
	first.dtc_count = 99;
	first.code_count = 77;
	first.cut = true;
	first.clear = true;
	check(summary_of(&first, 3, 2, 2, 13), "the summary counts what the control units hold: dtc_count, code_count, cut and clear of the result change nothing");

	for(int i = 0; i < COUNT(not_ok); i++)
	{
		memset(&first, 0, sizeof(first));
		unit_of(&first, not_ok[i], 0, 0, -1);
		same = summary_of(&first, 0, 1, 0, 0) && same;
	}
	check(same, "every status but ok counts as not ok: no_response, pending_timeout, incomplete, a negative response, an unknown one");

	// Only the control units that count
	memset(&first, 0, sizeof(first));
	unit_of(&first, DTC_ECU_OK, 1, 0, -1);
	unit_of(&first, DTC_ECU_NRC, 4, 5, 0);
	first.ecu_count = 1;
	check(summary_of(&first, 1, 0, 0, 1), "a control unit behind ecu_count does not count");

	memset(&first, 0, sizeof(first));
	for(int i = 0; i < DTC_ECUS_MAX; i++) unit_of(&first, i % 2 == 0 ? DTC_ECU_OK : DTC_ECU_OTHER, i < 8 ? 16 : 0, 0, -1);
	check(first.ecu_count == 24 && first.code_count == 128 && summary_of(&first, 8, 12, 8, 128),
	      "24 control units and 128 codes, as many as there is room for");

	// The sum stops at 2^32-1
	memset(&first, 0, sizeof(first));
	unit_of(&first, DTC_ECU_OK, 4, 4294967290u, -1);
	check(summary_of(&first, 1, 0, 0, 4294967294u), "4 listed and 2^32-6 omitted codes are 2^32-2");
	first.ecus[0].code_count = 5;
	check(summary_of(&first, 1, 0, 0, 4294967295u), "5 listed and 2^32-6 omitted codes are 2^32-1");
	first.ecus[0].code_count = 6;
	check(summary_of(&first, 1, 0, 0, 4294967295u), "6 listed and 2^32-6 omitted codes are 2^32-1, not 0");
	first.ecus[0].code_count = 128;
	first.ecus[0].omitted = 4294967295u;
	check(summary_of(&first, 1, 0, 0, 4294967295u), "128 listed and 2^32-1 omitted codes are 2^32-1");
	memset(&first, 0, sizeof(first));
	unit_of(&first, DTC_ECU_OK, 0, 4294967290u, -1);
	unit_of(&first, DTC_ECU_OK, 0, 5, -1);
	check(summary_of(&first, 2, 0, 0, 4294967295u), "2^32-6 and 5 omitted codes in two control units are 2^32-1");
	first.ecus[1].omitted = 6;
	check(summary_of(&first, 2, 0, 0, 4294967295u), "2^32-6 and 6 omitted codes in two control units are 2^32-1, not 0");
	first.ecus[1].omitted = 4294967295u;
	unit_of(&first, DTC_ECU_OK, 3, 4294967295u, -1);
	check(summary_of(&first, 3, 0, 0, 4294967295u), "three control units with about 2^32 omitted codes each are 2^32-1");

	// What a clear achieved
	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));
	check(clear_summary_is(&first, &second, 0, 0, 0, 0), "clear of nothing: nothing");
	unit_of(&first, DTC_ECU_OK, 2, 0, -1);
	unit_of(&first, DTC_ECU_OK, 3, 0, -1);
	unit_of(&first, DTC_ECU_OK, 0, 0, -1);
	unit_of(&second, DTC_ECU_OK, 0, 0, 1);
	unit_of(&second, DTC_ECU_OK, 2, 0, 0);
	unit_of(&second, DTC_ECU_OK, 0, 0, -1);
	check(clear_summary_is(&first, &second, 5, 2, 3, 1), "5 codes before, 2 remain: 3 cleared, one control unit did not confirm");
	first.dtc_count = 50;
	first.cut = true;
	second.dtc_count = 40;
	second.cut = true;
	second.clear = true;
	check(clear_summary_is(&first, &second, 5, 2, 3, 1), "what a clear achieved is counted from the control units: dtc_count, cut and clear of both results change nothing");
	check(clear_summary_is(&second, &first, 2, 5, 0, 0), "2 codes before, 5 remain: 0 cleared, not a negative number");
	check(clear_summary_is(&first, &first, 5, 5, 0, 0), "as many codes as before: 0 cleared");
	check(clear_summary_is(&second, &second, 2, 2, 0, 1), "unconfirmed control units are those of the result of the clear");
	second.ecus[1].code_count = 0;
	second.code_count = 0;
	check(clear_summary_is(&first, &second, 5, 0, 5, 1), "nothing remains: all cleared");
	second.ecus[0].cleared = 0;
	second.ecus[2].cleared = 0;
	check(clear_summary_is(&first, &second, 5, 0, 5, 3), "three control units did not confirm");
	second.ecu_count = 2;
	check(clear_summary_is(&first, &second, 5, 0, 5, 2), "a control unit behind ecu_count does not count as unconfirmed");
	second.ecu_count = 3;
	second.ecus[0].cleared = 1;
	second.ecus[1].cleared = 1;
	second.ecus[2].cleared = -1;
	check(clear_summary_is(&first, &second, 5, 0, 5, 0), "confirmed and absent: none unconfirmed");

	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));
	for(int i = 0; i < DTC_ECUS_MAX; i++)
	{
		unit_of(&first, DTC_ECU_OK, 2, 0, -1);
		unit_of(&second, DTC_ECU_OK, 0, 0, 0);
	}
	check(clear_summary_is(&first, &second, 48, 0, 48, 24), "24 control units, none of them confirmed, yet no code remains: 48 cleared, 24 unconfirmed");

	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));
	unit_of(&first, DTC_ECU_OK, 1, 100, -1);
	unit_of(&second, DTC_ECU_OK, 0, 40, 1);
	check(clear_summary_is(&first, &second, 101, 40, 61, 0), "omitted codes count before and after the clear");
	first.ecus[0].omitted = 4294967295u;
	second.ecus[0].omitted = 5;
	check(clear_summary_is(&first, &second, 4294967295u, 5, 4294967290u, 0), "2^32-1 codes or more before, 5 remain");
	check(clear_summary_is(&second, &first, 5, 4294967295u, 0, 0), "5 codes before, 2^32-1 or more remain: 0 cleared");

	// Results filled at random, added up a second time in 64 bit
	same = true;
	for(int i = 0; i < 2000 && same; i++)
	{
		dtc_result_t *results[2] = {&first, &second};
		uint64_t codes[2] = {0, 0};
		int with_codes = 0, bad = 0, clean = 0, unconfirmed = 0;
		uint32_t was, remaining;

		for(int r = 0; r < 2; r++)
		{
			int units = (int)random_below(DTC_ECUS_MAX + 1);

			memset(results[r], 0, sizeof(*results[r]));
			for(int u = 0; u < units; u++)
			{
				dtc_ecu_status_t status = random_below(2) == 0 ? DTC_ECU_OK : not_ok[random_below(COUNT(not_ok))];
				int listed = random_below(3) == 0 ? (int)random_below(6) : 0;
				uint32_t omitted = random_below(4) != 0 ? 0 : random_below(6) == 0 ? 4294967295u - random_below(4) : random_below(50);
				int cleared = (int)random_below(3) - 1;

				unit_of(results[r], status, listed, omitted, cleared);
				codes[r] += (uint64_t)listed + omitted;
				if(r == 1)
				{
					if(listed > 0 || omitted > 0) with_codes++;
					if(status != DTC_ECU_OK) bad++;
					else if(listed == 0 && omitted == 0) clean++;
					if(cleared == 0) unconfirmed++;
				}
			}
			results[r]->clear = random_below(2) == 1;
			results[r]->cut = random_below(2) == 1;
			results[r]->dtc_count = random_below(1000);
			results[r]->duration_ms = random_below(100000);
		}
		was = codes[0] > 4294967295u ? 4294967295u : (uint32_t)codes[0];
		remaining = codes[1] > 4294967295u ? 4294967295u : (uint32_t)codes[1];
		same = summary_of(&second, with_codes, bad, clean, remaining) &&
		       clear_summary_is(&first, &second, was, remaining, was > remaining ? was - remaining : 0, unconfirmed);
	}
	check(same, "2000 pairs of results filled at random: both summaries as added up a second time, whatever the results say about themselves");
}

/* Broken JSON and the room for the reader -------------------------------------------------------------- */

static void test_json(void)
{
	static const char *const no_result[] = {"", " ", "{}", "[]", "1", "null", "\"done\"", "[" BASE "]", "{\"result\":" BASE "}"};
	static char text[8192];
	size_t length = strlen(BASE);
	bool same = true;

	for(int i = 0; i < COUNT(no_result); i++)
	{
		snprintf(what, sizeof(what), "refused: %.60s", no_result[i]);
		check(refused(no_result[i]), what);
	}
	check(refused(BASE "}"), "refused: a brace behind the result");
	check(refused(BASE "x"), "refused: text behind the result");
	check(refused(variant(BASE, "\"dtc_count\":4,", "\"dtc_count\":4,,")), "refused: two commas");
	check(refused(variant(BASE, "\"dtc_count\":4,", "\"dtc_count\":04,")), "refused: a number with a leading zero");
	check(refused(variant(BASE, "\"id\":\"662\"", "\"id\":\"662\n\"")), "refused: a line break inside a text");

	expect_base();
	snprintf(text, sizeof(text), "%sgarbage", BASE);
	check(parse_room(text, length, WORK_TOKENS) && result_is(&expected), "bytes behind `length` are not looked at");

	// BASE has 49 tokens: 11 for the result, 22 for the first control unit and its code, 16 for the second
	check(parse_room(BASE, length, 49) && result_is(&expected), "49 tokens are enough for a result of 49");
	check(!parse_room(BASE, length, 48) && filled(&box.result, sizeof(box.result)), "refused: room for 48 tokens, one too few");
	check(!parse_room(BASE, length, 1) && filled(&box.result, sizeof(box.result)), "refused: room for one token");
	check(!parse_room(BASE, length, 0) && filled(&box.result, sizeof(box.result)), "refused: no room for tokens");
	check(!parse_room(BASE, length, -1) && filled(&box.result, sizeof(box.result)), "refused: a negative room for tokens");

	// An unknown member behind the known ones with a list of n numbers adds 2 + n tokens: 49 + 2 + 9 are 60
	for(int numbers = 9; numbers <= 10; numbers++)
	{
		snprintf(text, sizeof(text), "%.*s,\"x\":[0", (int)length - 1, BASE);
		for(int i = 1; i < numbers; i++) strcat(text, ",0");
		strcat(text, "]}");
		if(numbers == 9)
		{
			check(parse_room(text, strlen(text), 60) && result_is(&expected), "a result of exactly as many tokens as there is room for");
		}
		else
		{
			check(!parse_room(text, strlen(text), 60) && filled(&box.result, sizeof(box.result)),
			      "refused: one token more than there is room for, the unknown ones last");
		}
	}

	// The result is level 1, a code is level 5, the reader goes down to level 8
	check(JSON_MAX_DEPTH == 8 && accepted(variant(BASE, "{\"code\":\"9301\",", "{\"x\":[[[0]]],\"code\":\"9301\",")) && result_is(&expected),
	      "an unknown member of a code nested as deep as the reader goes is ignored");
	check(refused(variant(BASE, "{\"code\":\"9301\",", "{\"x\":[[[[0]]]],\"code\":\"9301\",")), "refused: an unknown member nested deeper than the reader goes");
	snprintf(text, sizeof(text), "%.*s,\"x\":[[[[[[[[0]]]]]]]]}", (int)length - 1, BASE);
	check(refused(text), "refused: an unknown member behind the known ones nested deeper than the reader goes");

	// Last: a reader that went on after an error would walk through tokens that were never written
	for(size_t cut = 0; cut < length; cut++)
	{
		if(parse_room(BASE, cut, WORK_TOKENS) || !filled(&box.result, sizeof(box.result)))
		{
			printf("  the first %d bytes are accepted or change the result\n", (int)cut);
			same = false;
		}
	}
	check(same, "refused: every beginning of a result that is not the whole result");
}

int main(void)
{
	test_short_name();
	test_fixtures();
	test_members();
	test_numbers();
	test_status();
	test_texts();
	test_limits();
	test_summaries();
	test_json();
	check(stray_writes == 0, "no call wrote before or behind the result or behind the room for tokens");
	return test_end();
}
