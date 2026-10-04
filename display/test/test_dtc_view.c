/*
 * Host test for display/components/core/dtc_view.c. Run "make test_dtc_view && ./test_dtc_view" in
 * display/test. redproof.py removes or weakens every rule once (mutations/dtc_view.py) and expects this
 * test to fail.
 *
 * Three kinds of checks:
 *   - examples: one result, the lines the header gives for it, written down by hand here or in
 *     fixtures/dtc_view_*.txt
 *   - the rules for names and for what is no UTF-8 a second time (plain_model, decode), compared with the
 *     module over generated texts
 *   - made-up results of every shape, compared line by line with a second way to put the list together
 *     (model_list, model_cleared), in a child process: a crash of the module is then a failed check
 */
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <unistd.h>
#include <sys/wait.h>
#include "test.h"
#include "dtc_view.h"

#define FIXTURES    "../../tools/w906/fixtures/"
#define FILL        0xA5
#define COUNT(a)    ((int)(sizeof(a) / sizeof((a)[0])))

// The lines under test lie between lines the module is not told of: a line written before the first or
// behind the last lands there and is seen, where the address sanitizer would only stop the program.
// The room ends with a zero of its own (END), so that a text that lost its end ends there at the latest.
#define FRONT       4
#define BEHIND      4
#define ROOM        (DTC_VIEW_LINES_MAX + 8)
#define END         1

static struct
{
	dtc_line_t lines[FRONT + ROOM + BEHIND];
	char end[END];
} room_box;
static dtc_line_t *const room = room_box.lines;
static dtc_line_t *const lines = &room_box.lines[FRONT];

// The same for the text of the export
#define TEXT_FRONT  16
#define TEXT_ROOM   24000
#define TEXT_BEHIND 64

static char text_room[TEXT_FRONT + TEXT_ROOM + TEXT_BEHIND + END];
static char *const exported = &text_room[TEXT_FRONT];

static json_token_t work[DTC_RESULT_TOKENS];
static char fixture[8192];
static char wanted_text[8192];
static dtc_result_t parsed, parsed_before;

// Results built by hand
static dtc_result_t made, made_before;
static dtc_result_t *making;
static dtc_ecu_t *unit;

static char what[320];

typedef struct
{
	dtc_line_kind_t kind;
	const char *text;
	const char *detail;
} want_t;

/* Helpers ---------------------------------------------------------------------------------------------- */

// Every byte is still the one the memory was filled with: the first is, and each is the one before it
static bool filled(const void *memory, size_t size)
{
	const unsigned char *bytes = memory;

	return size == 0 || (bytes[0] == FILL && memcmp(bytes, bytes + 1, size - 1) == 0);
}

static const char *kind_name(dtc_line_kind_t kind)
{
	static const char *const names[] = {"HEAD", "ECU", "CODE", "NOTE", "PROBLEM", "CLEAN"};

	return (unsigned)kind < 6u ? names[kind] : "?";
}

// What a call left behind: the count if it wrote only into its `max` lines and every text of them ends
// inside its field, else -1, which no expectation meets
static int settle(int count, int max)
{
	int told = max < 0 ? 0 : max;
	bool good = filled(room, FRONT * sizeof(room[0])) && filled(&lines[told], (size_t)(ROOM + BEHIND - told) * sizeof(room[0]));

	if(!good) printf("  written outside the room for %d lines\n", max);
	if(count < 0 || count > told)
	{
		printf("  %d lines returned for a room of %d\n", count, max);
		good = false;
	}
	for(int i = 0; good && i < count; i++)
	{
		if(memchr(lines[i].text, '\0', sizeof(lines[i].text)) == NULL || memchr(lines[i].detail, '\0', sizeof(lines[i].detail)) == NULL)
		{
			printf("  line %d has a text without an end\n", i);
			good = false;
		}
	}
	return good ? count : -1;
}

static int list(const dtc_result_t *result, int max)
{
	memset(room, FILL, sizeof(room_box.lines));
	return settle(dtc_view_list(result, lines, max), max);
}

static int cleared(const dtc_result_t *before, const dtc_result_t *after, int max)
{
	memset(room, FILL, sizeof(room_box.lines));
	return settle(dtc_view_cleared(before, after, lines, max), max);
}

static bool line_is(int index, dtc_line_kind_t kind, const char *text, const char *detail)
{
	return lines[index].kind == kind && strcmp(lines[index].text, text) == 0 && strcmp(lines[index].detail, detail) == 0;
}

static void show_lines(int count)
{
	for(int i = 0; i < count && i < ROOM; i++)
	{
		printf("    %2d %-7s \"%.*s\" \"%.*s\"\n", i, kind_name(lines[i].kind), DTC_VIEW_TEXT_SIZE, lines[i].text, DTC_VIEW_DETAIL_SIZE, lines[i].detail);
	}
}

// The lines of the last call are exactly these
static bool lines_are(int count, const want_t *want, int want_count)
{
	bool same = count == want_count;

	for(int i = 0; same && i < count; i++)
	{
		same = line_is(i, want[i].kind, want[i].text, want[i].detail);
	}
	if(!same)
	{
		printf("  %d lines, expected %d:\n", count, want_count);
		show_lines(count);
		printf("  expected:\n");
		for(int i = 0; i < want_count; i++) printf("    %2d %-7s \"%s\" \"%s\"\n", i, kind_name(want[i].kind), want[i].text, want[i].detail);
	}
	return same;
}

// The export of the first `count` lines of `from` in a room of `size` bytes. Returns what the module
// returned, or -2 if it wrote outside the room.
static int export_of(const dtc_line_t *from, int count, size_t size)
{
	int length;

	memset(text_room, FILL, sizeof(text_room) - END);
	length = dtc_view_export(from, count, exported, size);
	if(!filled(text_room, TEXT_FRONT) || !filled(exported + size, sizeof(text_room) - END - TEXT_FRONT - size))
	{
		printf("  written outside the room of %d bytes for the export\n", (int)size);
		return -2;
	}
	return length;
}

// The export of the lines of the last call is this text
static bool export_is(int count, const char *text)
{
	int length = export_of(lines, count, TEXT_ROOM);
	bool same = length == (int)strlen(text) && memchr(exported, '\0', TEXT_ROOM) != NULL && strcmp(exported, text) == 0;

	if(!same) printf("  export of %d lines returned %d, expected %d:\n%s", count, length, (int)strlen(text), text);
	return same;
}

static bool parse_fixture(const char *file, dtc_result_t *result)
{
	char path[160];

	snprintf(path, sizeof(path), "%s%s", strchr(file, '/') != NULL ? "" : FIXTURES, file);
	return read_fixture(path, fixture, sizeof(fixture)) && dtc_result_parse(fixture, strlen(fixture), result, work, DTC_RESULT_TOKENS);
}

// A text of the export as it is written in fixtures/, with the line break of its last line
static const char *export_fixture(const char *file)
{
	char path[160];

	snprintf(path, sizeof(path), "fixtures/%s", file);
	if(!read_fixture(path, wanted_text, sizeof(wanted_text) - 1)) return "fixture missing";
	strcat(wanted_text, "\n");
	return wanted_text;
}

/* Results by hand -------------------------------------------------------------------------------------- */

// An empty result. Behind its counts lie a control unit and a code that would show if a loop went too far.
static void make(dtc_result_t *result, uint32_t duration_ms)
{
	memset(result, 0, sizeof(*result));
	for(int i = 0; i < DTC_ECUS_MAX; i++)
	{
		dtc_ecu_t *ghost = &result->ecus[i];

		strcpy(ghost->name, "N9 Ghost (G)");
		strcpy(ghost->id, "GGG");
		ghost->status = DTC_ECU_NO_RESPONSE;
		ghost->cleared = 0;
		ghost->code_count = 1;
		ghost->omitted = 1;
	}
	for(int i = 0; i < DTC_CODES_MAX; i++)
	{
		strcpy(result->codes[i].code, "GHOST");
		strcpy(result->codes[i].status, "GG");
		result->codes[i].active = 1;
	}
	result->duration_ms = duration_ms;
	making = result;
	unit = NULL;
}

static dtc_ecu_t *add_unit(const char *name, const char *id, dtc_ecu_status_t status)
{
	unit = &making->ecus[making->ecu_count++];
	memset(unit, 0, sizeof(*unit));
	strcpy(unit->name, name);
	strcpy(unit->id, id);
	unit->uds = true;
	unit->status = status;
	unit->cleared = -1;
	unit->first_code = (uint16_t)making->code_count;
	return unit;
}

static void add_code(const char *code, const char *status, int active)
{
	dtc_code_t *entry = &making->codes[making->code_count++];

	memset(entry, 0, sizeof(*entry));
	strcpy(entry->code, code);
	strcpy(entry->status, status);
	entry->active = (int8_t)active;
	unit->code_count++;
}

/* UTF-8 a second time ---------------------------------------------------------------------------------- */

// The character at the beginning of a text read as a number: its bytes, 0 if there is no well-formed one.
// The module looks at ranges of bytes; here the number is put together and looked at.
static size_t decode(const unsigned char *text, size_t length, uint32_t *point)
{
	static const uint32_t smallest[5] = {0, 0, 0x80, 0x800, 0x10000};
	size_t bytes;
	uint32_t value;

	if(text[0] < 0x80)
	{
		*point = text[0];
		return 1;
	}
	if((text[0] & 0xE0) == 0xC0)
	{
		bytes = 2;
		value = text[0] & 0x1Fu;
	}
	else if((text[0] & 0xF0) == 0xE0)
	{
		bytes = 3;
		value = text[0] & 0x0Fu;
	}
	else if((text[0] & 0xF8) == 0xF0)
	{
		bytes = 4;
		value = text[0] & 0x07u;
	}
	else return 0;

	if(bytes > length) return 0;
	for(size_t i = 1; i < bytes; i++)
	{
		if((text[i] & 0xC0) != 0x80) return 0;
		value = value << 6 | (text[i] & 0x3Fu);
	}
	if(value < smallest[bytes] || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return 0;

	*point = value;
	return bytes;
}

// What a line may hold: well-formed UTF-8 without control characters
static bool showable(const char *text)
{
	size_t length = strlen(text);
	size_t at = 0;

	while(at < length)
	{
		uint32_t point = 0;
		size_t bytes = decode((const unsigned char *)text + at, length - at, &point);

		if(bytes == 0 || point < 0x20 || point == 0x7F) return false;
		at += bytes;
	}
	return true;
}

// A text as a field of `size` bytes shows it: what is no character or a control character as '?', and as
// many whole characters as fit
static void clean_model(const char *text, size_t length, char *out, size_t size)
{
	size_t used = 0;
	size_t at = 0;

	while(at < length)
	{
		uint32_t point = 0;
		size_t bytes = decode((const unsigned char *)text + at, length - at, &point);
		bool shown = bytes > 0 && point >= 0x20 && point != 0x7F;
		size_t taken = shown ? bytes : 1;

		if(used + taken > size - 1) break;
		if(shown) memcpy(out + used, text + at, bytes);
		else out[used] = '?';
		used += taken;
		at += taken;
	}
	out[used] = '\0';
}

// The characters the font of the display has: ASCII from the blank to the tilde, the German letters, the en
// dash and the ellipsis; and the middle dot of the lists
static bool in_font(const char *text)
{
	static const char *const letters[] = {
		"\xC3\x84", "\xC3\x96", "\xC3\x9C", "\xC3\xA4", "\xC3\xB6", "\xC3\xBC", "\xC3\x9F", "\xE2\x80\x93", "\xE2\x80\xA6", "\xC2\xB7",
	};

	while(*text != '\0')
	{
		size_t bytes = 0;

		if((unsigned char)*text >= 0x20 && (unsigned char)*text <= 0x7E) bytes = 1;
		for(int i = 0; bytes == 0 && i < COUNT(letters); i++)
		{
			if(strncmp(text, letters[i], strlen(letters[i])) == 0) bytes = strlen(letters[i]);
		}
		if(bytes == 0) return false;
		text += bytes;
	}
	return true;
}

static bool lines_in_font(int count)
{
	for(int i = 0; i < count; i++)
	{
		if(!in_font(lines[i].text) || !in_font(lines[i].detail)) return false;
	}
	return count > 0;
}

/* Names ------------------------------------------------------------------------------------------------ */

// The 18 control units of the W906 as main/autopid.c names them, and their names for a list by hand
static const struct
{
	const char *name;
	const char *plain;
} UNITS[18] = {
	{"N73 Elektronisches Zündschloss (EZS)", "Elektronisches Zündschloss"},
	{"N3/28 Motorelektronik (CDID3)", "Motorelektronik"},
	{"Y3/8n4 Getriebesteuerung (NAG2)", "Getriebesteuerung"},
	{"N15/5 Wählhebelmodul (EWM)", "Wählhebelmodul"},
	{"N30/4 ESP", "ESP"},
	{"N10 SAM", "SAM"},
	{"N118/5 Kraftstoffpumpe (FSCU)", "Kraftstoffpumpe"},
	{"N28/4 Anhängererkennung (AHE)", "Anhängererkennung"},
	{"N80 Mantelrohrmodul (MRM)", "Mantelrohrmodul"},
	{"N70 Dachbedieneinheit (DBE)", "Dachbedieneinheit"},
	{"N72/1 Oberes Bedienfeld (OBF)", "Oberes Bedienfeld"},
	{"B162 Collision Prevention Assist", "Collision Prevention Assist"},
	{"N87/8 Radio", "Radio"},
	{"A2/30 Navigationsmodul", "Navigationsmodul"},
	{"A1 Kombiinstrument", "Kombiinstrument"},
	{"S98 Klimaanlage", "Klimaanlage"},
	{"N2/14 Rückhaltesystem (SRS)", "Rückhaltesystem"},
	{"N69/1 Fahrertür (TSG)", "Fahrertür"},
};

// The name for a list in a room of `size` bytes inside a larger one; the bytes behind the room are watched
static bool plain_is(const char *name, size_t size, const char *plain)
{
	char out[160];

	// The last byte is a zero: a name that lost its end ends there at the latest
	memset(out, FILL, sizeof(out) - 1);
	out[sizeof(out) - 1] = '\0';
	dtc_plain_name(name, out + 8, size);
	if(!filled(out, 8) || !filled(out + 8 + size, sizeof(out) - 1 - 8 - size) || out[sizeof(out) - 1] != '\0')
	{
		printf("  written outside the %d bytes for the name of \"%s\"\n", (int)size, name);
		return false;
	}
	if(size == 0) return true;
	if(memchr(out + 8, '\0', size) == NULL || strcmp(out + 8, plain) != 0)
	{
		out[8 + size - 1] = '\0';
		printf("  the name of \"%s\" in %d bytes is \"%s\", expected \"%s\"\n", name, (int)size, out + 8, plain);
		return false;
	}
	return true;
}

// As many whole characters from the beginning of a text as fit into `room` bytes, counted character by
// character. Only for well-formed UTF-8.
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

// The rules of the header a second time, on a copy that is shortened step by step: the first word is
// walked over from the front, the pairs of parentheses are found reading forward. `plain` gets the whole
// name for a list, not yet cleaned or cut.
static void plain_model(const char *name, char *plain)
{
	char copy[256];
	char *text = copy;
	char *rest = copy;
	char *open = NULL;
	char *pair = NULL;
	char *closed = NULL;
	bool digit = false;

	strcpy(copy, name);

	while(*rest != '\0' && *rest != ' ')
	{
		if(strchr("0123456789", *rest) != NULL) digit = true;
		rest++;
	}
	while(*rest == ' ') rest++;
	if(digit && *rest != '\0') text = rest;

	// An opening parenthesis belongs to the next closing one; the last such pair is kept
	for(char *c = text; *c != '\0'; c++)
	{
		if(*c == '(')
		{
			open = c;
		}
		else if(*c == ')' && open != NULL)
		{
			pair = open;
			closed = c;
			open = NULL;
		}
	}
	if(pair != NULL && closed[1 + strspn(closed + 1, " ")] == '\0')
	{
		while(pair > text && pair[-1] == ' ') pair--;
		if(pair > text) *pair = '\0';
	}
	strcpy(plain, text);
}

static uint32_t random_state = 20261004;

static uint32_t random_below(uint32_t limit)
{
	random_state = random_state * 1103515245u + 12345u;
	return (random_state >> 8) % limit;
}

static void test_constants(void)
{
	dtc_line_t line;

	check(DTC_VIEW_TEXT_SIZE == 48 && sizeof(line.text) == 48, "the text of a line has room for 48 bytes");
	check(DTC_VIEW_DETAIL_SIZE == 40 && sizeof(line.detail) == 40, "the detail of a line has room for 40 bytes");
	check(DTC_VIEW_LINES_MAX == 226, "226 lines are promised to be enough for every result");
	check(DTC_ECUS_MAX == 24 && DTC_CODES_MAX == 128, "a result holds 24 control units and 128 codes, which the 226 lines are counted from");
}

static void test_plain_name(void)
{
	static const struct
	{
		const char *name;
		const char *plain;
		const char *rule;
	} cases[] = {
		{"N3/28 Motorelektronik (CDID3)", "Motorelektronik", "example of the header: designation and parentheses left out"},
		{"N2/14 R\xC3\xBC" "ckhaltesystem (SRS)", "R\xC3\xBC" "ckhaltesystem", "example of the header: the umlaut stays as its two bytes"},
		{"N30/4 (ESP)", "(ESP)", "example of the header: without the parentheses nothing would be left"},
		{"N10", "N10", "example of the header: without the first word nothing would be left"},
		{"", "", "an empty name"},
		{"Radio", "Radio", "nothing to leave out"},
		{"Oberes Bedienfeld", "Oberes Bedienfeld", "no digit in the first word"},
		{"Motor M1", "Motor M1", "a digit in the second word does not count"},
		{"Motor 2 links", "Motor 2 links", "a second word of a digit does not count"},
		{"N1x ESP", "ESP", "a digit in the middle of the first word"},
		{"1N ESP", "ESP", "a digit at the beginning of the first word"},
		{"N1 ESP", "ESP", "a digit at the end of the first word"},
		{"9 ESP", "ESP", "a first word that is only a digit"},
		{"N30/4 ESP links", "ESP links", "all words behind the first stay"},
		{"N10  SAM", "SAM", "two blanks behind the first word go with it"},
		{"N10 ", "N10 ", "a designation and a blank: nothing would be left"},
		{"N10   ", "N10   ", "a designation and blanks: nothing would be left"},
		{" N10 SAM", " N10 SAM", "a blank at the beginning: the first word is empty and has no digit"},
		{"N/: ESP", "N/: ESP", "the characters next to the digits in ASCII are no digits"},
		{"N1 2 B", "2 B", "only the first word is left out, also if the second has a digit too"},
		{"A\xE2\x82\xAC B", "A\xE2\x82\xAC B", "the bytes of a character of three bytes are no digits"},
		{"Radio (R)", "Radio", "parentheses at the end and the blank before them"},
		{"Radio   (R)", "Radio", "three blanks before the parentheses"},
		{"Radio(R)", "Radio", "parentheses without a blank before them"},
		{"Radio (R) ", "Radio", "a blank behind the parentheses goes with them"},
		{"Radio (R)   ", "Radio", "three blanks behind the parentheses go with them"},
		{"Radio (R) x", "Radio (R) x", "parentheses with a word behind them are not the last"},
		{"Radio (R).", "Radio (R).", "parentheses with a full stop behind them are not the last"},
		{"Radio (R) (", "Radio (R) (", "parentheses with an opening one behind them are not the last"},
		{"A (B) (C)", "A (B)", "only the last of two pairs is left out"},
		{"A (B)(C)", "A (B)", "only the last of two pairs without a blank between"},
		{"A (B (C)", "A (B", "an opening parenthesis belongs to the next closing one: the inner pair"},
		{"A (B (C) D)", "A (B (C) D)", "the closing parenthesis at the end has no opening one of its own"},
		{"A (B) C)", "A (B) C)", "a closing parenthesis alone at the end is no pair"},
		{"A (B", "A (B", "an opening parenthesis alone is no pair"},
		{"A B)", "A B)", "a closing parenthesis alone is no pair"},
		{"A )B(", "A )B(", "a closing parenthesis before the opening one is no pair"},
		{"A ()", "A", "empty parentheses are a pair"},
		{"A ( )", "A", "parentheses around a blank are a pair"},
		{"A (B C)", "A", "parentheses around two words"},
		{"A (T\xC3\xBCr 1)", "A", "parentheses around an umlaut and a digit"},
		{"(B)", "(B)", "a name of parentheses only: nothing would be left"},
		{" (B)", " (B)", "a blank and parentheses: nothing would be left, the blank goes with the parentheses"},
		{"  (B) ", "  (B) ", "blanks around parentheses only: nothing would be left"},
		{"N1 (B)", "(B)", "designation left out, then nothing but parentheses"},
		{"N1  (B)  ", "(B)  ", "designation and its blanks left out, the parentheses stay with what follows them"},
		{"N1 X (Y)", "X", "both rules"},
		{"N1 (X) (Y)", "(X)", "both rules with two pairs"},
		{"A1 (B) C2 (D)", "(B) C2", "the designation at the front and the pair at the end"},
		{"N1(B C)", "C)", "the second rule is applied to what the first left: the opening parenthesis is gone"},
		{"N1 X(Y", "X(Y", "an opening parenthesis alone behind the designation"},
		{"N1 )", ")", "a closing parenthesis alone behind the designation"},
		{"X (1)", "X", "a digit in the parentheses is no digit of the first word"},
		{"(N1) X", "X", "a first word in parentheses with a digit is left out"},
		{"(N1)", "(N1)", "one word in parentheses with a digit: both rules would leave nothing"},
		{"T\xC3\xBCr 2 (T2)", "T\xC3\xBCr 2", "no digit in the first word, a digit in the second, parentheses"},
	};
	static const char *const cut_names[] = {
		"N2/14 R\xC3\xBC" "ckhaltesystem (SRS)", "A1 Z\xC3\xBCndschlo\xC3\x9Fgeh\xC3\xA4use \xE2\x82\xAC" "5 \xF0\x9F\x98\x80 x",
		"\xC3\x84\xC3\x96\xC3\x9C\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F", "\xF0\x9F\x98\x80\xE2\x82\xAC\xC3\xBCz\xC3\xBC\xE2\x82\xAC\xF0\x9F\x98\x80 (x)",
		"abcdefghijklmnopqrstuvwxyz", "x",
	};
	static const char *const cut_plains[] = {
		"R\xC3\xBC" "ckhaltesystem", "Z\xC3\xBCndschlo\xC3\x9Fgeh\xC3\xA4use \xE2\x82\xAC" "5 \xF0\x9F\x98\x80 x",
		"\xC3\x84\xC3\x96\xC3\x9C\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F", "\xF0\x9F\x98\x80\xE2\x82\xAC\xC3\xBCz\xC3\xBC\xE2\x82\xAC\xF0\x9F\x98\x80",
		"abcdefghijklmnopqrstuvwxyz", "x",
	};
	static char long_name[1001];
	char digits[8] = "X0 Y";
	char sevens[8];
	bool same = true;

	for(int i = 0; i < COUNT(UNITS); i++)
	{
		snprintf(what, sizeof(what), "name for a list of \"%s\" is \"%s\"", UNITS[i].name, UNITS[i].plain);
		check(plain_is(UNITS[i].name, 64, UNITS[i].plain), what);
	}
	for(int i = 0; i < COUNT(cases); i++)
	{
		snprintf(what, sizeof(what), "name for a list of \"%s\" is \"%s\": %s", cases[i].name, cases[i].plain, cases[i].rule);
		check(plain_is(cases[i].name, 64, cases[i].plain), what);
	}
	for(char digit = '0'; digit <= '9'; digit++)
	{
		digits[1] = digit;
		same = plain_is(digits, 64, "Y") && same;
	}
	check(same, "each of the digits 0 to 9 in the first word makes it a designation");

	// Every size from none to more than enough: whole characters only, never a byte outside the room
	for(int i = 0; i < COUNT(cut_names); i++)
	{
		same = true;
		for(size_t size = 0; size <= strlen(cut_plains[i]) + 3; size++)
		{
			char cut[64];

			snprintf(cut, sizeof(cut), "%.*s", size == 0 ? 0 : (int)whole_characters(cut_plains[i], size - 1), cut_plains[i]);
			same = plain_is(cut_names[i], size, cut) && same;
		}
		snprintf(what, sizeof(what), "name for a list of \"%s\" in every size: as many whole characters as fit", cut_names[i]);
		check(same, what);
	}
	check(plain_is("N2/14 R\xC3\xBC" "ckhaltesystem (SRS)", 17, "R\xC3\xBC" "ckhaltesystem"), "a name of 16 bytes fits into a room of 17");
	check(plain_is("N2/14 R\xC3\xBC" "ckhaltesystem (SRS)", 16, "R\xC3\xBC" "ckhaltesyste"), "a name of 16 bytes is cut to 15 in a room of 16");
	check(plain_is("N2/14 R\xC3\xBC" "ckhaltesystem (SRS)", 4, "R\xC3\xBC"), "the umlaut fits into a room of 4");
	check(plain_is("N2/14 R\xC3\xBC" "ckhaltesystem (SRS)", 3, "R"), "the two bytes of an umlaut are not cut apart: one letter in a room of 3");
	check(plain_is("N2/14 R\xC3\xBC" "ckhaltesystem (SRS)", 2, "R"), "one letter in a room of 2");
	check(plain_is("N2/14 R\xC3\xBC" "ckhaltesystem (SRS)", 1, ""), "a room of 1 byte holds an empty text");
	check(plain_is("N2/14 R\xC3\xBC" "ckhaltesystem (SRS)", 0, ""), "a room of 0 bytes is not written at all");
	check(plain_is("Radio", 0, "") && plain_is("", 0, ""), "a room of 0 bytes is not written at all, also for a name without anything to leave out");
	check(plain_is("x\xE2\x82\xAC", 4, "x") && plain_is("x\xE2\x82\xAC", 5, "x\xE2\x82\xAC"), "a character of three bytes needs its three bytes of room");
	check(plain_is("x\xF0\x9F\x98\x80", 5, "x") && plain_is("x\xF0\x9F\x98\x80", 6, "x\xF0\x9F\x98\x80"), "a character of four bytes needs its four bytes of room");

	memset(long_name, 'n', sizeof(long_name) - 1);
	memset(sevens, 'n', sizeof(sevens) - 1);
	sevens[sizeof(sevens) - 1] = '\0';
	check(plain_is(long_name, sizeof(sevens), sevens), "a name of 1000 bytes is cut to 7 in a room of 8");
}

// What the adapter sent is shown, but nothing that is no character
static void test_clean(void)
{
	static const struct
	{
		const char *name;
		const char *shown;
		const char *rule;
	} cases[] = {
		{"a\xFFz", "a?z", "the byte FF is no UTF-8"},
		{"a\xFEz", "a?z", "the byte FE is no UTF-8"},
		{"a\x80z", "a?z", "a continuation byte alone"},
		{"a\xBFz", "a?z", "the last continuation byte alone"},
		{"\x80\x80\x80", "???", "three continuation bytes: one mark each"},
		{"a\xC3", "a?", "the first byte of an umlaut at the end of the name"},
		{"a\xC3z", "a?z", "the first byte of an umlaut before a letter: the letter stays"},
		{"a\xC3\xC3\xBCz", "a?\xC3\xBCz", "the first byte of an umlaut before an umlaut: the umlaut stays"},
		{"a\xC0\x80z", "a??z", "C0 80 is a longer way to write the byte 0"},
		{"a\xC1\xBFz", "a??z", "C1 BF is a longer way to write the byte 7F"},
		{"a\xC2\x80z", "a\xC2\x80z", "C2 80 is the first character of two bytes"},
		{"a\xDF\xBFz", "a\xDF\xBFz", "DF BF is the last character of two bytes"},
		{"a\xC2\x7Fz", "a??z", "C2 7F: the second byte is below the continuation bytes, and is a control character itself"},
		{"a\xC2\xC0z", "a??z", "C2 C0: the second byte is above the continuation bytes"},
		{"a\xE0\x9F\xBFz", "a???z", "E0 9F BF is a longer way to write a character of two bytes"},
		{"a\xE0\xA0\x80z", "a\xE0\xA0\x80z", "E0 A0 80 is the first character of three bytes"},
		{"a\xE1\x80\x80z", "a\xE1\x80\x80z", "E1 80 80 is a character: the second byte from 80 on"},
		{"a\xEC\xBF\xBFz", "a\xEC\xBF\xBFz", "EC BF BF is a character: the second byte up to BF"},
		{"a\xED\x9F\xBFz", "a\xED\x9F\xBFz", "ED 9F BF is the last character before the surrogates"},
		{"a\xED\xA0\x80z", "a???z", "ED A0 80 is the first half of a surrogate pair"},
		{"a\xED\xBF\xBFz", "a???z", "ED BF BF is the last half of a surrogate pair"},
		{"a\xEE\x80\x80z", "a\xEE\x80\x80z", "EE 80 80 is the first character behind the surrogates"},
		{"a\xEF\xBF\xBFz", "a\xEF\xBF\xBFz", "EF BF BF is the last character of three bytes"},
		{"a\xE2\x82", "a??", "two of the three bytes of the euro sign at the end of the name"},
		{"a\xE2\x82z", "a??z", "two of the three bytes of the euro sign before a letter"},
		{"a\xE2z\xAC", "a?z?", "the bytes of the euro sign with a letter between"},
		{"a\xE2\x82\x7Fz", "a???z", "E2 82 7F: the third byte is below the continuation bytes"},
		{"a\xE2\x82\xC0z", "a???z", "E2 82 C0: the third byte is above the continuation bytes"},
		{"a\xE2\x7F\xACz", "a???z", "E2 7F AC: the second byte is below the continuation bytes"},
		{"a\xE2\xC0\xACz", "a???z", "E2 C0 AC: the second byte is above the continuation bytes"},
		{"a\xF0\x8F\xBF\xBFz", "a????z", "F0 8F BF BF is a longer way to write a character of three bytes"},
		{"a\xF0\x90\x80\x80z", "a\xF0\x90\x80\x80z", "F0 90 80 80 is the first character of four bytes"},
		{"a\xF1\x80\x80\x80z", "a\xF1\x80\x80\x80z", "F1 80 80 80 is a character: the second byte from 80 on"},
		{"a\xF3\xBF\xBF\xBFz", "a\xF3\xBF\xBF\xBFz", "F3 BF BF BF is a character: the second byte up to BF"},
		{"a\xF4\x8F\xBF\xBFz", "a\xF4\x8F\xBF\xBFz", "F4 8F BF BF is the last character of Unicode"},
		{"a\xF4\x90\x80\x80z", "a????z", "F4 90 80 80 is behind the end of Unicode"},
		{"a\xF5\x80\x80\x80z", "a????z", "F5 begins no character"},
		{"a\xF8\x88\x80\x80\x80z", "a?????z", "F8 begins no character"},
		{"a\xF0\x9F\x98", "a???", "three of the four bytes of an emoji at the end of the name"},
		{"a\xF0\x9F\x98z", "a???z", "three of the four bytes of an emoji before a letter"},
		{"a\xF0\x9F\x98\x7Fz", "a????z", "F0 9F 98 7F: the fourth byte is below the continuation bytes"},
		{"a\xF0\x9F\x98\xC0z", "a????z", "F0 9F 98 C0: the fourth byte is above the continuation bytes"},
		{"a\xF0\x9F\x7F\x80z", "a????z", "F0 9F 7F 80: the third byte is below the continuation bytes"},
		{"a\xF0\x9F\xC0\x80z", "a????z", "F0 9F C0 80: the third byte is above the continuation bytes"},
		{"a\nb", "a?b", "a line break would make two lines of one"},
		{"a\tb", "a?b", "a tabulator is a control character"},
		{"a\rb", "a?b", "a carriage return is a control character"},
		{"a\x01" "b", "a?b", "the first control character"},
		{"a\x1F" "b", "a?b", "the last control character below the blank"},
		{"a\x7F" "b", "a?b", "the byte 7F is a control character"},
		{"a~b", "a~b", "the tilde, the last character before 7F, stays"},
		{"a!b", "a!b", "the exclamation mark, the first character behind the blank, stays"},
		{"a?b", "a?b", "a question mark of the adapter stays one"},
		{"N1\tX", "N1?X", "a tabulator is no blank: the designation is one word with what follows, nothing would be left"},
		{"N1 X\n(Y)", "X?", "a line break before the parentheses is no blank: it stays, as a mark, when the pair is left out"},
		{"a\xC3 (x)", "a?", "the first byte of an umlaut before parentheses that are left out: the blank behind it does not complete it"},
		{"a\xE2\x82(x)", "a??", "two of the three bytes of the euro sign before parentheses that are left out"},
		{"a\xF0\x9F\x98 (\x80)", "a???", "three bytes of an emoji before parentheses that hold the fourth"},
	};
	char name[8] = "a_z";
	char shown[8] = "a_z";
	bool same = true;

	for(int i = 0; i < COUNT(cases); i++)
	{
		snprintf(what, sizeof(what), "what is no character becomes a question mark: %s", cases[i].rule);
		check(plain_is(cases[i].name, 64, cases[i].shown), what);
	}

	// Every byte between two letters. Alone it is a character only from the blank to the tilde.
	for(int byte = 1; byte < 256; byte++)
	{
		name[1] = (char)byte;
		shown[1] = byte >= 0x20 && byte <= 0x7E ? (char)byte : '?';
		same = plain_is(name, 64, shown) && same;
	}
	check(same, "each of the 255 bytes between two letters: itself from the blank to the tilde, else a question mark");

	check(plain_is("\xC3\xBC\xFF", 3, "\xC3\xBC") && plain_is("\xC3\xBC\xFF", 4, "\xC3\xBC?"), "the mark for a byte needs one byte of room");
	check(plain_is("\xFF\xFF\xFF\xFF", 3, "??") && plain_is("\xFF\xFF\xFF\xFF", 5, "????"), "marks are cut like letters");
	check(plain_is("\xF0\x9F\x98", 3, "??") && plain_is("\xF0\x9F\x98", 2, "?"), "the bytes of half a character need one byte of room each, not the room of the whole character");
}

/* The adapter's fixtures ------------------------------------------------------------------------------- */

// The lines of fixtures/dtc_view_mixed.json
static const want_t MIXED[] = {
	{DTC_LINE_HEAD, "10 Fehler", "8 Steuergeräte · 36 s"},
	{DTC_LINE_ECU, "Motorelektronik", "7E0 · 6 Fehler"},
	{DTC_LINE_CODE, "P0100-13", "aktiv"},
	{DTC_LINE_CODE, "P242F-FA", "gespeichert"},
	{DTC_LINE_NOTE, "4 Codes nicht übertragen", ""},
	{DTC_LINE_ECU, "SAM", "662 · 2 Fehler"},
	{DTC_LINE_CODE, "9301", "Status 60"},
	{DTC_LINE_CODE, "9302", ""},
	{DTC_LINE_ECU, "ESP", "784 · 2 Fehler"},
	{DTC_LINE_CODE, "U0100-87", "gespeichert"},
	{DTC_LINE_NOTE, "1 Code nicht übertragen", ""},
	{DTC_LINE_PROBLEM, "ESP", "abgelehnt (NRC 22)"},
	{DTC_LINE_PROBLEM, "Collision Prevention Assist", "keine Antwort"},
	{DTC_LINE_PROBLEM, "Rückhaltesystem", "Antwort ausstehend"},
	{DTC_LINE_PROBLEM, "Fahrertür", "unvollst\xC3\xA4ndig"},
	{DTC_LINE_PROBLEM, "Radio", "unbekannter Status"},
	{DTC_LINE_CLEAN, "1 Steuerger\xC3\xA4t ohne Fehler", ""},
};

// The outcome of a clear with the list of dtc_view_mixed.json as what is left, after a read that found 12 codes
static const want_t MIXED_CLEARED[] = {
	{DTC_LINE_HEAD, "Gelöscht 2 von 12", "verbleibend 10"},
	{DTC_LINE_PROBLEM, "SAM", "Löschen nicht bestätigt"},
	{DTC_LINE_PROBLEM, "Radio", "Löschen nicht bestätigt"},
	{DTC_LINE_ECU, "Motorelektronik", "7E0 · 6 Fehler"},
	{DTC_LINE_CODE, "P0100-13", "aktiv"},
	{DTC_LINE_CODE, "P242F-FA", "gespeichert"},
	{DTC_LINE_NOTE, "4 Codes nicht übertragen", ""},
	{DTC_LINE_ECU, "SAM", "662 · 2 Fehler"},
	{DTC_LINE_CODE, "9301", "Status 60"},
	{DTC_LINE_CODE, "9302", ""},
	{DTC_LINE_ECU, "ESP", "784 · 2 Fehler"},
	{DTC_LINE_CODE, "U0100-87", "gespeichert"},
	{DTC_LINE_NOTE, "1 Code nicht übertragen", ""},
	{DTC_LINE_PROBLEM, "ESP", "abgelehnt (NRC 22)"},
	{DTC_LINE_PROBLEM, "Collision Prevention Assist", "keine Antwort"},
	{DTC_LINE_PROBLEM, "Rückhaltesystem", "Antwort ausstehend"},
	{DTC_LINE_PROBLEM, "Fahrertür", "unvollständig"},
	{DTC_LINE_PROBLEM, "Radio", "unbekannter Status"},
};

static void test_fixtures(void)
{
	static const want_t read_codes[] = {
		{DTC_LINE_HEAD, "5 Fehler", "18 Steuerger\xC3\xA4te \xC2\xB7 34 s"},
		{DTC_LINE_ECU, "Motorelektronik", "7E0 \xC2\xB7 2 Fehler"},
		{DTC_LINE_CODE, "P0100-13", "aktiv"},
		{DTC_LINE_CODE, "P242F-FA", "gespeichert"},
		{DTC_LINE_ECU, "ESP", "784 \xC2\xB7 1 Fehler"},
		{DTC_LINE_CODE, "U0100-87", "gespeichert"},
		{DTC_LINE_ECU, "SAM", "662 \xC2\xB7 1 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
		{DTC_LINE_ECU, "R\xC3\xBC" "ckhaltesystem", "6BC \xC2\xB7 1 Fehler"},
		{DTC_LINE_CODE, "9100", "Status E0"},
		{DTC_LINE_PROBLEM, "Collision Prevention Assist", "keine Antwort"},
		{DTC_LINE_CLEAN, "13 Steuerger\xC3\xA4te ohne Fehler", ""},
	};
	static const want_t read_empty[] = {
		{DTC_LINE_HEAD, "0 Fehler", "18 Steuergeräte · 35 s"},
		{DTC_LINE_CLEAN, "18 Steuergeräte ohne Fehler", ""},
	};
	static const want_t shortened[] = {
		{DTC_LINE_HEAD, "165 Fehler", "18 Steuergeräte · 35 s"},
		{DTC_LINE_ECU, "Motorelektronik", "7E0 · 80 Fehler"},
		{DTC_LINE_NOTE, "80 Codes nicht \xC3\xBC" "bertragen", ""},
		{DTC_LINE_ECU, "ESP", "784 · 75 Fehler"},
		{DTC_LINE_NOTE, "75 Codes nicht übertragen", ""},
		{DTC_LINE_ECU, "SAM", "662 · 2 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
		{DTC_LINE_CODE, "9302", "Status 60"},
		{DTC_LINE_ECU, "Kraftstoffpumpe", "778 · 8 Fehler"},
		{DTC_LINE_CODE, "P0190-11", "gespeichert"},
		{DTC_LINE_CODE, "P0191-11", "gespeichert"},
		{DTC_LINE_CODE, "P0192-11", "gespeichert"},
		{DTC_LINE_CODE, "P0193-11", "gespeichert"},
		{DTC_LINE_CODE, "P0194-11", "gespeichert"},
		{DTC_LINE_CODE, "P0195-11", "gespeichert"},
		{DTC_LINE_CODE, "P0196-11", "gespeichert"},
		{DTC_LINE_CODE, "P0197-11", "gespeichert"},
		{DTC_LINE_CLEAN, "14 Steuergeräte ohne Fehler", ""},
	};
	static const want_t clear_as_list[] = {
		{DTC_LINE_HEAD, "2 Fehler", "18 Steuergeräte · 38 s"},
		{DTC_LINE_ECU, "SAM", "662 · 1 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
		{DTC_LINE_ECU, "Rückhaltesystem", "6BC · 1 Fehler"},
		{DTC_LINE_CODE, "9100", "Status E0"},
		{DTC_LINE_PROBLEM, "Collision Prevention Assist", "keine Antwort"},
		{DTC_LINE_CLEAN, "15 Steuergeräte ohne Fehler", ""},
	};
	static const want_t outcome[] = {
		{DTC_LINE_HEAD, "Gel\xC3\xB6scht 3 von 5", "verbleibend 2"},
		{DTC_LINE_PROBLEM, "Rückhaltesystem", "L\xC3\xB6schen nicht best\xC3\xA4tigt"},
		{DTC_LINE_ECU, "SAM", "662 · 1 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
		{DTC_LINE_ECU, "Rückhaltesystem", "6BC · 1 Fehler"},
		{DTC_LINE_CODE, "9100", "Status E0"},
		{DTC_LINE_PROBLEM, "Collision Prevention Assist", "keine Antwort"},
	};
	int count;

	check(parse_fixture("dtc_result_read_codes.json", &parsed), "fixture dtc_result_read_codes.json is a result");
	count = list(&parsed, DTC_VIEW_LINES_MAX);
	check(lines_are(count, read_codes, COUNT(read_codes)), "list of dtc_result_read_codes.json: head, four control units with their codes, one without answer, 13 clean");
	check(lines_in_font(count), "list of dtc_result_read_codes.json: every character is one the font has");
	check(export_is(count, export_fixture("dtc_view_read_codes.txt")), "export of the list of dtc_result_read_codes.json is fixtures/dtc_view_read_codes.txt");

	check(parse_fixture("dtc_result_read_empty.json", &parsed), "fixture dtc_result_read_empty.json is a result");
	count = list(&parsed, DTC_VIEW_LINES_MAX);
	check(lines_are(count, read_empty, COUNT(read_empty)), "list of dtc_result_read_empty.json: the head and 18 clean control units");
	check(export_is(count, export_fixture("dtc_view_read_empty.txt")), "export of the list of dtc_result_read_empty.json is fixtures/dtc_view_read_empty.txt");

	check(parse_fixture("dtc_result_shortened.json", &parsed), "fixture dtc_result_shortened.json is a result");
	count = list(&parsed, DTC_VIEW_LINES_MAX);
	check(lines_are(count, shortened, COUNT(shortened)), "list of dtc_result_shortened.json: omitted codes are counted and told per control unit");
	check(lines_in_font(count), "list of dtc_result_shortened.json: every character is one the font has");
	check(export_is(count, export_fixture("dtc_view_shortened.txt")), "export of the list of dtc_result_shortened.json is fixtures/dtc_view_shortened.txt");

	check(parse_fixture("dtc_result_clear.json", &parsed), "fixture dtc_result_clear.json is a result");
	count = list(&parsed, DTC_VIEW_LINES_MAX);
	check(lines_are(count, clear_as_list, COUNT(clear_as_list)), "dtc_result_clear.json as a list: what is left, 37500 ms are 38 s");
	check(export_is(count, export_fixture("dtc_view_clear.txt")), "export of dtc_result_clear.json as a list is fixtures/dtc_view_clear.txt");

	check(parse_fixture("dtc_result_read_codes.json", &parsed_before), "fixture dtc_result_read_codes.json is the list before the clear");
	count = cleared(&parsed_before, &parsed, DTC_VIEW_LINES_MAX);
	check(lines_are(count, outcome, COUNT(outcome)), "outcome of dtc_result_clear.json after dtc_result_read_codes.json: 3 of 5 cleared, one control unit did not confirm");
	check(lines_in_font(count), "outcome of dtc_result_clear.json: every character is one the font has");
	check(export_is(count, export_fixture("dtc_view_cleared.txt")), "export of the outcome of dtc_result_clear.json is fixtures/dtc_view_cleared.txt");

	check(parse_fixture("fixtures/dtc_view_mixed.json", &parsed), "fixture dtc_view_mixed.json is a result");
	count = list(&parsed, DTC_VIEW_LINES_MAX);
	check(lines_are(count, MIXED, COUNT(MIXED)), "list of dtc_view_mixed.json: every kind of line and every status the adapter knows");
	check(lines_in_font(count), "list of dtc_view_mixed.json: every character is one the font has");
	check(export_is(count, export_fixture("dtc_view_mixed.txt")), "export of the list of dtc_view_mixed.json is fixtures/dtc_view_mixed.txt");
}

/* The head --------------------------------------------------------------------------------------------- */

static void test_head(void)
{
	static const struct
	{
		uint32_t ms;
		const char *seconds;
	} durations[] = {
		{0, "0"}, {1, "0"}, {499, "0"}, {500, "1"}, {501, "1"}, {999, "1"}, {1000, "1"}, {1499, "1"}, {1500, "2"}, {34300, "34"},
		{34499, "34"}, {34500, "35"}, {34900, "35"}, {37500, "38"}, {59999, "60"}, {999499, "999"}, {999500, "1000"},
		{4294966499u, "4294966"}, {4294966500u, "4294967"}, {4294966796u, "4294967"}, {4294966999u, "4294967"}, {4294967000u, "4294967"},
		{4294967295u, "4294967"},
	};
	static const want_t one_clean[] = {
		{DTC_LINE_HEAD, "0 Fehler", "1 Steuerger\xC3\xA4t \xC2\xB7 1 s"},
		{DTC_LINE_CLEAN, "1 Steuerger\xC3\xA4t ohne Fehler", ""},
	};
	static const want_t two_clean[] = {
		{DTC_LINE_HEAD, "0 Fehler", "2 Steuerger\xC3\xA4te \xC2\xB7 2 s"},
		{DTC_LINE_CLEAN, "2 Steuerger\xC3\xA4te ohne Fehler", ""},
	};
	static const want_t one_code[] = {
		{DTC_LINE_HEAD, "1 Fehler", "1 Steuergerät · 0 s"},
		{DTC_LINE_ECU, "SAM", "662 · 1 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
	};
	static const want_t two_codes[] = {
		{DTC_LINE_HEAD, "2 Fehler", "1 Steuergerät · 0 s"},
		{DTC_LINE_ECU, "SAM", "662 · 2 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
		{DTC_LINE_CODE, "9302", "Status 61"},
	};
	static const want_t reported[] = {
		{DTC_LINE_HEAD, "3 Fehler", "1 Steuergerät · 0 s"},
		{DTC_LINE_ECU, "SAM", "662 · 3 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
		{DTC_LINE_NOTE, "2 Codes nicht übertragen", ""},
	};
	static const want_t most[] = {
		{DTC_LINE_HEAD, "4294967295 Fehler", "2 Steuergeräte · 0 s"},
		{DTC_LINE_ECU, "Motorelektronik", "7E0 · 4294967295 Fehler"},
		{DTC_LINE_NOTE, "4294967295 Codes nicht übertragen", ""},
		{DTC_LINE_ECU, "SAM", "662 · 1 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
	};
	static const want_t most_in_one[] = {
		{DTC_LINE_HEAD, "4294967295 Fehler", "1 Steuergerät · 0 s"},
		{DTC_LINE_ECU, "SAM", "662 · 4294967295 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
		{DTC_LINE_NOTE, "4294967295 Codes nicht übertragen", ""},
	};
	static const want_t below_most[] = {
		{DTC_LINE_HEAD, "4294967295 Fehler", "1 Steuergerät · 0 s"},
		{DTC_LINE_ECU, "SAM", "662 · 4294967295 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
		{DTC_LINE_NOTE, "4294967294 Codes nicht übertragen", ""},
	};
	static const want_t far_below_most[] = {
		{DTC_LINE_HEAD, "4294967294 Fehler", "1 Steuergerät · 0 s"},
		{DTC_LINE_ECU, "SAM", "662 · 4294967294 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
		{DTC_LINE_NOTE, "4294967293 Codes nicht übertragen", ""},
	};

	make(&made, 0);
	check(list(&made, ROOM) == 1 && line_is(0, DTC_LINE_HEAD, "0 Fehler", "0 Steuerger\xC3\xA4te \xC2\xB7 0 s"),
	      "a result without control units: the head alone, \"0 Fehler\" and \"0 Steuergeräte\"");

	make(&made, 1000);
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	check(lines_are(list(&made, ROOM), one_clean, COUNT(one_clean)), "one clean control unit: \"1 Steuergerät\" in the head and in the line of the clean ones");
	add_unit("N30/4 ESP", "784", DTC_ECU_OK);
	made.duration_ms = 2000;
	check(lines_are(list(&made, ROOM), two_clean, COUNT(two_clean)), "two clean control units: \"2 Steuergeräte\" in the head and in the line of the clean ones");

	make(&made, 0);
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	add_code("9301", "60", -1);
	check(lines_are(list(&made, ROOM), one_code, COUNT(one_code)), "one code: \"1 Fehler\" in the head and for the control unit, no line of clean ones");
	add_code("9302", "61", -1);
	check(lines_are(list(&made, ROOM), two_codes, COUNT(two_codes)), "two codes: \"2 Fehler\" in the head and for the control unit");

	make(&made, 0);
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	add_code("9301", "60", -1);
	unit->omitted = 2;
	made.dtc_count = 99;
	check(lines_are(list(&made, ROOM), reported, COUNT(reported)), "the head counts listed plus omitted codes, not the dtc_count of the adapter");

	for(int i = 0; i < COUNT(durations); i++)
	{
		char detail[DTC_VIEW_DETAIL_SIZE];

		make(&made, durations[i].ms);
		snprintf(detail, sizeof(detail), "0 Steuergeräte · %s s", durations[i].seconds);
		snprintf(what, sizeof(what), "%" PRIu32 " ms are shown as %s s", durations[i].ms, durations[i].seconds);
		check(list(&made, ROOM) == 1 && line_is(0, DTC_LINE_HEAD, "0 Fehler", detail), what);
	}

	make(&made, 0);
	add_unit("N3/28 Motorelektronik (CDID3)", "7E0", DTC_ECU_OK);
	unit->omitted = UINT32_MAX;
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	add_code("9301", "60", -1);
	check(lines_are(list(&made, ROOM), most, COUNT(most)), "more than 2^32-1 codes in two control units: the head says 4294967295");

	make(&made, 0);
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	add_code("9301", "60", -1);
	unit->omitted = UINT32_MAX;
	check(lines_are(list(&made, ROOM), most_in_one, COUNT(most_in_one)), "2^32 codes in one control unit: head and control unit say 4294967295, the unit is not taken for a clean one");
	unit->omitted = UINT32_MAX - 1;
	check(lines_are(list(&made, ROOM), below_most, COUNT(below_most)), "exactly 2^32-1 codes in one control unit: 4294967295");
	unit->omitted = UINT32_MAX - 2;
	check(lines_are(list(&made, ROOM), far_below_most, COUNT(far_below_most)), "2^32-2 codes in one control unit: 4294967294");
}

/* Control units with codes ----------------------------------------------------------------------------- */

static void test_units(void)
{
	static const want_t order[] = {
		{DTC_LINE_HEAD, "10 Fehler", "5 Steuergeräte · 34 s"},
		{DTC_LINE_ECU, "Motorelektronik", "7E0 · 2 Fehler"},
		{DTC_LINE_CODE, "P0100-13", "aktiv"},
		{DTC_LINE_CODE, "P242F-FA", "gespeichert"},
		{DTC_LINE_ECU, "SAM", "662 · 3 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
		{DTC_LINE_CODE, "9302", ""},
		{DTC_LINE_NOTE, "1 Code nicht \xC3\xBC" "bertragen", ""},
		{DTC_LINE_ECU, "ESP", "784 · 4 Fehler"},
		{DTC_LINE_NOTE, "4 Codes nicht \xC3\xBC" "bertragen", ""},
		{DTC_LINE_ECU, "Radio", "5D6 · 1 Fehler"},
		{DTC_LINE_CODE, "U0100-87", "gespeichert"},
		{DTC_LINE_CLEAN, "1 Steuergerät ohne Fehler", ""},
	};
	static const want_t details[] = {
		{DTC_LINE_HEAD, "13 Fehler", "1 Steuergerät · 0 s"},
		{DTC_LINE_ECU, "Motorelektronik", "7E0 · 13 Fehler"},
		{DTC_LINE_CODE, "C1", "aktiv"},
		{DTC_LINE_CODE, "C2", "gespeichert"},
		{DTC_LINE_CODE, "C3", "Status 68"},
		{DTC_LINE_CODE, "C4", ""},
		{DTC_LINE_CODE, "C5", "Status 69"},
		{DTC_LINE_CODE, "C6", "Status 6A"},
		{DTC_LINE_CODE, "C7", "Status 6B"},
		{DTC_LINE_CODE, "C8", "Status 6C"},
		{DTC_LINE_CODE, "C9", ""},
		{DTC_LINE_CODE, "C10", "aktiv"},
		{DTC_LINE_CODE, "C11", "gespeichert"},
		{DTC_LINE_CODE, "C12", "Status 1234567"},
		{DTC_LINE_CODE, "P0123456789ABCD", "Status 0"},
	};
	static const want_t protocols[] = {
		{DTC_LINE_HEAD, "4 Fehler", "2 Steuergeräte · 0 s"},
		{DTC_LINE_ECU, "SAM", "662 · 2 Fehler"},
		{DTC_LINE_CODE, "9301", "aktiv"},
		{DTC_LINE_CODE, "9302", "gespeichert"},
		{DTC_LINE_ECU, "ESP", "784 · 2 Fehler"},
		{DTC_LINE_CODE, "U0100-87", "Status 28"},
		{DTC_LINE_CODE, "U0101-87", ""},
	};
	static const want_t no_id[] = {
		{DTC_LINE_HEAD, "1 Fehler", "1 Steuergerät · 0 s"},
		{DTC_LINE_ECU, "", " \xC2\xB7 1 Fehler"},
		{DTC_LINE_CODE, "", "Status 60"},
	};
	static const want_t longest_id[] = {
		{DTC_LINE_HEAD, "4294967295 Fehler", "1 Steuergerät · 0 s"},
		{DTC_LINE_ECU, "Radio", "18DAF11 · 4294967295 Fehler"},
		{DTC_LINE_NOTE, "4294967295 Codes nicht übertragen", ""},
	};
	static const want_t untrusted[] = {
		{DTC_LINE_HEAD, "2 Fehler", "1 Steuergerät · 0 s"},
		{DTC_LINE_ECU, "a?b", "7?0 · 2 Fehler"},
		{DTC_LINE_CODE, "P?\xC3\xBC?", "Status ?8"},
		{DTC_LINE_CODE, "??\xE2\x82\xAC?", "Status ?("},
		{DTC_LINE_PROBLEM, "a?b", "keine Antwort"},
	};

	make(&made, 34300);
	add_unit("N3/28 Motorelektronik (CDID3)", "7E0", DTC_ECU_OK);
	add_code("P0100-13", "2F", 1);
	add_code("P242F-FA", "68", 0);
	add_unit("N73 Elektronisches Zündschloss (EZS)", "4E0", DTC_ECU_OK);
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	add_code("9301", "60", -1);
	add_code("9302", "", -1);
	unit->omitted = 1;
	add_unit("N30/4 ESP", "784", DTC_ECU_OK);
	unit->omitted = 4;
	add_unit("N87/8 Radio", "5D6", DTC_ECU_OK);
	add_code("U0100-87", "28", 0);
	check(lines_are(list(&made, ROOM), order, COUNT(order)),
	      "control units in the order of the result, each with its own codes and then its note; one with omitted codes only is listed, one without codes is not");

	make(&made, 0);
	add_unit("N3/28 Motorelektronik (CDID3)", "7E0", DTC_ECU_OK);
	add_code("C1", "68", 1);
	add_code("C2", "68", 0);
	add_code("C3", "68", -1);
	add_code("C4", "", -1);
	add_code("C5", "69", 2);
	add_code("C6", "6A", -2);
	add_code("C7", "6B", 127);
	add_code("C8", "6C", -128);
	add_code("C9", "", 2);
	add_code("C10", "", 1);
	add_code("C11", "", 0);
	add_code("C12", "1234567", -1);
	add_code("P0123456789ABCD", "0", -1);
	check(lines_are(list(&made, ROOM), details, COUNT(details)),
	      "detail of a code: active true, false, absent with and without a status text, and values of active that are neither");

	make(&made, 0);
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	unit->uds = false;
	add_code("9301", "60", 1);
	add_code("9302", "60", 0);
	add_unit("N30/4 ESP", "784", DTC_ECU_OK);
	unit->uds = true;
	add_code("U0100-87", "28", -1);
	add_code("U0101-87", "", -1);
	check(lines_are(list(&made, ROOM), protocols, COUNT(protocols)), "the member active decides about the detail of a code, not the protocol of its control unit");

	make(&made, 0);
	add_unit("", "", DTC_ECU_OK);
	add_code("", "60", -1);
	check(lines_are(list(&made, ROOM), no_id, COUNT(no_id)), "a control unit without name and id and a code without text: the lines are there, the texts empty");

	make(&made, 0);
	add_unit("Radio", "18DAF11", DTC_ECU_OK);
	unit->omitted = UINT32_MAX;
	check(lines_are(list(&made, ROOM), longest_id, COUNT(longest_id)) && strlen(lines[1].detail) == 28,
	      "the longest id with the largest number: a detail of 28 bytes, nothing is cut");

	make(&made, 0);
	add_unit("a\nb", "7\xFF" "0", DTC_ECU_NO_RESPONSE);
	add_code("P\x01\xC3\xBC\xC3", "\x7F" "8", -1);
	add_code("\xED\xA0\xE2\x82\xAC\t", "\xC3(", -1);
	check(lines_are(list(&made, ROOM), untrusted, COUNT(untrusted)),
	      "what is no character in a name, an id, a code and a status text is a question mark in the line");
}

/* Control units with a status that is not ok ----------------------------------------------------------- */

static void test_problems(void)
{
	static const want_t statuses[] = {
		{DTC_LINE_HEAD, "0 Fehler", "8 Steuergeräte · 0 s"},
		{DTC_LINE_PROBLEM, "ESP", "keine Antwort"},
		{DTC_LINE_PROBLEM, "SAM", "Antwort ausstehend"},
		{DTC_LINE_PROBLEM, "Kombiinstrument", "unvollst\xC3\xA4ndig"},
		{DTC_LINE_PROBLEM, "Radio", "abgelehnt (NRC 22)"},
		{DTC_LINE_PROBLEM, "Klimaanlage", "unbekannter Status"},
		{DTC_LINE_PROBLEM, "Mantelrohrmodul", "unbekannter Status"},
		{DTC_LINE_PROBLEM, "Dachbedieneinheit", "unbekannter Status"},
		{DTC_LINE_CLEAN, "1 Steuergerät ohne Fehler", ""},
	};
	static const want_t both_groups[] = {
		{DTC_LINE_HEAD, "5 Fehler", "4 Steuergeräte · 0 s"},
		{DTC_LINE_ECU, "Motorelektronik", "7E0 · 3 Fehler"},
		{DTC_LINE_CODE, "P0100-13", "aktiv"},
		{DTC_LINE_NOTE, "2 Codes nicht übertragen", ""},
		{DTC_LINE_ECU, "SAM", "662 · 1 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
		{DTC_LINE_ECU, "Radio", "5D6 · 1 Fehler"},
		{DTC_LINE_NOTE, "1 Code nicht übertragen", ""},
		{DTC_LINE_PROBLEM, "Motorelektronik", "unvollständig"},
		{DTC_LINE_PROBLEM, "ESP", "abgelehnt (NRC 31)"},
		{DTC_LINE_PROBLEM, "Radio", "keine Antwort"},
	};
	static const struct
	{
		uint8_t nrc;
		const char *detail;
	} codes[] = {
		{0x00, "abgelehnt (NRC 00)"}, {0x01, "abgelehnt (NRC 01)"}, {0x09, "abgelehnt (NRC 09)"}, {0x0A, "abgelehnt (NRC 0A)"},
		{0x0F, "abgelehnt (NRC 0F)"}, {0x10, "abgelehnt (NRC 10)"}, {0x22, "abgelehnt (NRC 22)"}, {0x7F, "abgelehnt (NRC 7F)"},
		{0x9A, "abgelehnt (NRC 9A)"}, {0xAB, "abgelehnt (NRC AB)"}, {0xF0, "abgelehnt (NRC F0)"}, {0xFF, "abgelehnt (NRC FF)"},
	};

	make(&made, 0);
	add_unit("N30/4 ESP", "784", DTC_ECU_NO_RESPONSE);
	add_unit("N10 SAM", "662", DTC_ECU_PENDING_TIMEOUT);
	add_unit("A1 Kombiinstrument", "796", DTC_ECU_INCOMPLETE);
	add_unit("N87/8 Radio", "5D6", DTC_ECU_NRC);
	unit->nrc = 0x22;
	add_unit("S98 Klimaanlage", "791", DTC_ECU_OTHER);
	add_unit("N80 Mantelrohrmodul (MRM)", "792", (dtc_ecu_status_t)6);
	add_unit("N70 Dachbedieneinheit (DBE)", "667", (dtc_ecu_status_t)-1);
	add_unit("N69/1 Fahrertür (TSG)", "6C8", DTC_ECU_OK);
	check(lines_are(list(&made, ROOM), statuses, COUNT(statuses)),
	      "every status that is not ok has its words, in the order of the result; a status that is no member of the enum is unknown and not clean");

	for(int i = 0; i < COUNT(codes); i++)
	{
		make(&made, 0);
		add_unit("N87/8 Radio", "5D6", DTC_ECU_NRC);
		unit->nrc = codes[i].nrc;
		snprintf(what, sizeof(what), "NRC %d is written as \"%s\": two capital hexadecimal digits", codes[i].nrc, codes[i].detail);
		check(list(&made, ROOM) == 2 && line_is(1, DTC_LINE_PROBLEM, "Radio", codes[i].detail), what);
	}

	make(&made, 0);
	add_unit("N87/8 Radio", "5D6", DTC_ECU_NO_RESPONSE);
	unit->nrc = 0x22;
	check(list(&made, ROOM) == 2 && line_is(1, DTC_LINE_PROBLEM, "Radio", "keine Antwort"), "an NRC left in a control unit with another status is not shown");

	make(&made, 0);
	add_unit("N3/28 Motorelektronik (CDID3)", "7E0", DTC_ECU_INCOMPLETE);
	add_code("P0100-13", "2F", 1);
	unit->omitted = 2;
	add_unit("N30/4 ESP", "784", DTC_ECU_NRC);
	unit->nrc = 0x31;
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	add_code("9301", "60", -1);
	add_unit("N87/8 Radio", "5D6", DTC_ECU_NO_RESPONSE);
	unit->omitted = 1;
	check(lines_are(list(&made, ROOM), both_groups, COUNT(both_groups)),
	      "a control unit with codes whose status is not ok appears in both groups; first all with codes, then all that are not ok; none is clean");
}

/* The clean control units and the note of a cut result -------------------------------------------------- */

static void test_clean_and_cut(void)
{
	static const want_t not_clean[] = {
		{DTC_LINE_HEAD, "2 Fehler", "5 Steuergeräte · 0 s"},
		{DTC_LINE_ECU, "ESP", "784 · 1 Fehler"},
		{DTC_LINE_NOTE, "1 Code nicht übertragen", ""},
		{DTC_LINE_ECU, "SAM", "662 · 1 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
		{DTC_LINE_PROBLEM, "Radio", "keine Antwort"},
		{DTC_LINE_CLEAN, "2 Steuergeräte ohne Fehler", ""},
	};
	static const want_t cut_clean[] = {
		{DTC_LINE_HEAD, "0 Fehler", "2 Steuergeräte · 0 s"},
		{DTC_LINE_CLEAN, "2 Steuergeräte ohne Fehler", ""},
		{DTC_LINE_NOTE, "Liste unvollst\xC3\xA4ndig", ""},
	};
	static const want_t cut_codes[] = {
		{DTC_LINE_HEAD, "1 Fehler", "2 Steuergeräte · 0 s"},
		{DTC_LINE_ECU, "SAM", "662 · 1 Fehler"},
		{DTC_LINE_CODE, "9301", "Status 60"},
		{DTC_LINE_PROBLEM, "ESP", "keine Antwort"},
		{DTC_LINE_NOTE, "Liste unvollständig", ""},
	};

	make(&made, 0);
	add_unit("N73 Elektronisches Zündschloss (EZS)", "4E0", DTC_ECU_OK);
	add_unit("N30/4 ESP", "784", DTC_ECU_OK);
	unit->omitted = 1;
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	add_code("9301", "60", -1);
	add_unit("N87/8 Radio", "5D6", DTC_ECU_NO_RESPONSE);
	add_unit("A1 Kombiinstrument", "796", DTC_ECU_OK);
	check(lines_are(list(&made, ROOM), not_clean, COUNT(not_clean)),
	      "clean are the control units with status ok and without a code: not one with omitted codes only, not one without answer");

	make(&made, 0);
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	add_unit("N30/4 ESP", "784", DTC_ECU_OK);
	made.cut = true;
	check(lines_are(list(&made, ROOM), cut_clean, COUNT(cut_clean)), "a cut result says so in its last line, behind the line of the clean control units");

	make(&made, 0);
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	add_code("9301", "60", -1);
	add_unit("N30/4 ESP", "784", DTC_ECU_NO_RESPONSE);
	made.cut = true;
	check(lines_are(list(&made, ROOM), cut_codes, COUNT(cut_codes)), "a cut result without clean control units: the note follows the control units that are not ok");
	made.cut = false;
	check(list(&made, ROOM) == 4 && line_is(3, DTC_LINE_PROBLEM, "ESP", "keine Antwort"), "the same result not cut: no note");
}

/* Names that do not fit --------------------------------------------------------------------------------- */

// `count` times a letter and a text behind them
static const char *name_of(char letter, int count, const char *tail)
{
	static char names[4][128];
	static int next = 0;
	char *name = names[next++ % 4];

	memset(name, letter, (size_t)count);
	strcpy(name + count, tail);
	return name;
}

// A control unit with this name has this text in its line with codes and in its line without answer
static bool named(const char *name, const char *text)
{
	make(&made, 0);
	add_unit(name, "7E0", DTC_ECU_NO_RESPONSE);
	unit->omitted = 1;
	if(list(&made, ROOM) == 4 && line_is(1, DTC_LINE_ECU, text, "7E0 \xC2\xB7 1 Fehler") && line_is(3, DTC_LINE_PROBLEM, text, "keine Antwort")) return true;

	printf("  \"%s\" is shown as \"%s\", expected \"%s\"\n", name, lines[1].text, text);
	return false;
}

static void test_long_names(void)
{
	static const struct
	{
		const char *character;
		const char *bytes;
	} characters[] = {{"\xC3\xBC", "two"}, {"\xE2\x82\xAC", "three"}, {"\xF0\x9F\x98\x80", "four"}};
	char long_name[64];

	check(named(name_of('a', 47, ""), name_of('a', 47, "")), "a name of 47 bytes fits into the text of a line");
	check(named(name_of('a', 48, ""), name_of('a', 47, "")), "a name of 48 bytes is cut to 47");
	check(named(name_of('a', 63, ""), name_of('a', 47, "")), "the longest name a result holds, 63 bytes, is cut to 47");
	check(named(name_of('a', 45, "\xC3\xBC"), name_of('a', 45, "\xC3\xBC")), "an umlaut as bytes 46 and 47 fits");
	check(named(name_of('a', 46, "\xC3\xBC"), name_of('a', 46, "")), "an umlaut as bytes 47 and 48 is left out whole");
	check(named(name_of('a', 44, "\xE2\x82\xAC"), name_of('a', 44, "\xE2\x82\xAC")), "a character of three bytes as bytes 45 to 47 fits");
	check(named(name_of('a', 45, "\xE2\x82\xAC"), name_of('a', 45, "")), "a character of three bytes that ends with byte 48 is left out whole");
	check(named(name_of('a', 46, "\xE2\x82\xAC"), name_of('a', 46, "")), "a character of three bytes that begins with byte 47 is left out whole");
	check(named(name_of('a', 43, "\xF0\x9F\x98\x80"), name_of('a', 43, "\xF0\x9F\x98\x80")), "a character of four bytes as bytes 44 to 47 fits");
	check(named(name_of('a', 44, "\xF0\x9F\x98\x80"), name_of('a', 44, "")), "a character of four bytes that ends with byte 48 is left out whole");
	check(named(name_of('a', 46, "\xF0\x9F\x98\x80"), name_of('a', 46, "")), "a character of four bytes that begins with byte 47 is left out whole");
	check(named(name_of('a', 46, "\xFF"), name_of('a', 46, "?")), "the mark for a byte that is no character as byte 47 fits");
	check(named(name_of('a', 47, "\xFF"), name_of('a', 47, "")), "the mark for a byte that is no character as byte 48 is left out");

	for(int i = 0; i < COUNT(characters); i++)
	{
		bool same = true;

		for(int letters = 38; letters <= 50; letters++)
		{
			const char *name;
			char rest[8];
			char text[64];

			snprintf(rest, sizeof(rest), "%szz", characters[i].character);
			name = name_of('a', letters, rest);
			snprintf(text, sizeof(text), "%.*s", (int)whole_characters(name, 47), name);
			same = named(name, text) && same;
		}
		snprintf(what, sizeof(what), "a character of %s bytes at every place around the end of the text: as many whole characters as fit into 47 bytes", characters[i].bytes);
		check(same, what);
	}

	snprintf(long_name, sizeof(long_name), "N3/28 %s", name_of('b', 57, ""));
	check(strlen(long_name) == 63 && named(long_name, name_of('b', 47, "")),
	      "a designation and 57 letters: the designation is left out first, then the rest is cut to 47");
	snprintf(long_name, sizeof(long_name), "%s (%s)", name_of('b', 40, ""), name_of('c', 15, ""));
	check(strlen(long_name) == 58 && named(long_name, name_of('b', 40, "")), "a name of 58 bytes whose name for a list has 40: nothing is cut");
	snprintf(long_name, sizeof(long_name), "%s (%s)", name_of('b', 47, ""), name_of('c', 10, ""));
	check(strlen(long_name) == 60 && named(long_name, name_of('b', 47, "")), "a name of 60 bytes whose name for a list has 47: it fits");
	snprintf(long_name, sizeof(long_name), "%s (%s)", name_of('b', 48, ""), name_of('c', 3, ""));
	check(strlen(long_name) == 54 && named(long_name, name_of('b', 47, "")), "a name of 54 bytes whose name for a list has 48 is cut to 47");
}

/* Too few lines ---------------------------------------------------------------------------------------- */

// The lines of a call into a room of `max` lines, of `want_count` lines there are
static bool shortened_to(int count, int max, const want_t *want, int want_count)
{
	if(max <= 0) return count == 0;
	if(max >= want_count) return lines_are(count, want, want_count);
	if(!lines_are(count - 1, want, max - 1) || !line_is(max - 1, DTC_LINE_NOTE, "Liste gek\xC3\xBCrzt", ""))
	{
		printf("  %d lines in a room for %d, the last one %s \"%.*s\"\n", count, max, count > 0 ? kind_name(lines[count - 1].kind) : "", DTC_VIEW_TEXT_SIZE,
		       count > 0 ? lines[count - 1].text : "");
		return false;
	}
	return count == max;
}

static void test_max(void)
{
	check(parse_fixture("fixtures/dtc_view_mixed.json", &parsed), "fixture dtc_view_mixed.json is the result for the rooms that are too small");
	// From no room up, then less than none
	for(int step = 0; step <= 22; step++)
	{
		int max = step <= 20 ? step : 20 - step;

		snprintf(what, sizeof(what), "a list of 17 lines in a room for %d: %s", max,
		         max <= 0 ? "nothing is written, 0 lines" : max >= 17 ? "all of it" : "the last line that fits says that the list is cut");
		check(shortened_to(list(&parsed, max), max, MIXED, COUNT(MIXED)), what);
	}
	check(list(&parsed, INT_MIN) == 0, "a list in a room for the smallest number of lines there is: nothing is written, 0 lines");

	make(&made, 0);
	check(list(&made, 1) == 1 && line_is(0, DTC_LINE_HEAD, "0 Fehler", "0 Steuergeräte · 0 s"), "a list of one line in a room for one: the head, nothing is cut");
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	check(list(&made, 1) == 1 && line_is(0, DTC_LINE_NOTE, "Liste gek\xC3\xBCrzt", ""), "a list of two lines in a room for one: the note alone");
	check(list(&made, 2) == 2 && line_is(0, DTC_LINE_HEAD, "0 Fehler", "1 Steuergerät · 0 s") && line_is(1, DTC_LINE_CLEAN, "1 Steuergerät ohne Fehler", ""),
	      "a list of two lines in a room for two: nothing is cut");

	// The same for the outcome of a clear
	make(&made_before, 0);
	add_unit("N3/28 Motorelektronik (CDID3)", "7E0", DTC_ECU_OK);
	unit->omitted = 12;
	parsed.ecus[1].cleared = 0;
	parsed.ecus[6].cleared = 0;
	parsed.ecus[0].cleared = 1;
	for(int step = 0; step <= 23; step++)
	{
		int max = step <= 21 ? step : 21 - step;

		snprintf(what, sizeof(what), "an outcome of 18 lines in a room for %d: %s", max,
		         max <= 0 ? "nothing is written, 0 lines" : max >= 18 ? "all of it" : "the last line that fits says that the list is cut");
		check(shortened_to(cleared(&made_before, &parsed, max), max, MIXED_CLEARED, COUNT(MIXED_CLEARED)), what);
	}
	check(cleared(&made_before, &parsed, INT_MIN) == 0, "an outcome in a room for the smallest number of lines there is: nothing is written, 0 lines");
}

/* The largest result ----------------------------------------------------------------------------------- */

// 24 control units that are not ok, did not confirm the clear and left codes out, with the 128 codes a
// result holds, and cut: every line there can be
static void make_largest(dtc_result_t *result)
{
	int number = 0;

	make(result, 0);
	for(int i = 0; i < DTC_ECUS_MAX; i++)
	{
		char name[64], id[8];

		snprintf(name, sizeof(name), "N%d Einheit%02d (E)", i, i);
		snprintf(id, sizeof(id), "7%02d", i);
		add_unit(name, id, DTC_ECU_NO_RESPONSE);
		unit->cleared = 0;
		unit->omitted = 1;
		for(int k = 0; k < (i < 8 ? 6 : 5); k++)
		{
			char code[16], status[8];

			snprintf(code, sizeof(code), "C%03d", number);
			snprintf(status, sizeof(status), "%02X", number);
			add_code(code, status, -1);
			number++;
		}
	}
	result->cut = true;
}

// The lines of the largest result from line `at` on, as the header orders them. Returns the line behind them.
static int largest_follows(int at, bool *same)
{
	int number = 0;

	for(int i = 0; i < DTC_ECUS_MAX; i++)
	{
		char name[64], detail[64];
		int codes = i < 8 ? 6 : 5;

		snprintf(name, sizeof(name), "Einheit%02d", i);
		snprintf(detail, sizeof(detail), "7%02d \xC2\xB7 %d Fehler", i, codes + 1);
		*same = line_is(at++, DTC_LINE_ECU, name, detail) && *same;
		for(int k = 0; k < codes; k++)
		{
			snprintf(name, sizeof(name), "C%03d", number);
			snprintf(detail, sizeof(detail), "Status %02X", number);
			*same = line_is(at++, DTC_LINE_CODE, name, detail) && *same;
			number++;
		}
		*same = line_is(at++, DTC_LINE_NOTE, "1 Code nicht übertragen", "") && *same;
	}
	for(int i = 0; i < DTC_ECUS_MAX; i++)
	{
		char name[64];

		snprintf(name, sizeof(name), "Einheit%02d", i);
		*same = line_is(at++, DTC_LINE_PROBLEM, name, "keine Antwort") && *same;
	}
	*same = line_is(at++, DTC_LINE_NOTE, "Liste unvollständig", "") && *same;
	return at;
}

static void test_largest(void)
{
	bool same = true;
	int count, at;

	make_largest(&made);
	make(&made_before, 0);
	check(made.ecu_count == DTC_ECUS_MAX && made.code_count == DTC_CODES_MAX, "the largest result has 24 control units and 128 codes");

	count = list(&made, DTC_VIEW_LINES_MAX);
	same = line_is(0, DTC_LINE_HEAD, "152 Fehler", "24 Steuergeräte · 0 s");
	at = largest_follows(1, &same);
	check(count == 202 && at == 202, "the longest list has 202 lines: the head, 24 control units, 128 codes, 24 notes, 24 that are not ok, the note of the cut result");
	check(same, "the longest list: every line as the header orders them, none says that the list is cut");

	count = cleared(&made_before, &made, DTC_VIEW_LINES_MAX);
	same = line_is(0, DTC_LINE_HEAD, "Gelöscht 0 von 0", "verbleibend 152");
	for(int i = 0; i < DTC_ECUS_MAX; i++)
	{
		char name[64];

		snprintf(name, sizeof(name), "Einheit%02d", i);
		same = line_is(1 + i, DTC_LINE_PROBLEM, name, "Löschen nicht bestätigt") && same;
	}
	at = largest_follows(25, &same);
	check(count == 226 && at == 226 && DTC_VIEW_LINES_MAX == 226, "the longest outcome of a clear has 226 lines, 24 more than the longest list: it fills DTC_VIEW_LINES_MAX exactly");
	check(same, "the longest outcome of a clear: every line as the header orders them, none says that the list is cut");

	count = cleared(&made_before, &made, DTC_VIEW_LINES_MAX - 1);
	check(count == 225 && line_is(224, DTC_LINE_NOTE, "Liste gekürzt", "") && line_is(223, DTC_LINE_PROBLEM, "Einheit22", "keine Antwort"),
	      "the longest outcome in a room of one line less: the last line says that the list is cut");
	count = list(&made, 201);
	check(count == 201 && line_is(200, DTC_LINE_NOTE, "Liste gekürzt", "") && line_is(199, DTC_LINE_PROBLEM, "Einheit22", "keine Antwort"),
	      "the longest list in a room of 201 lines: the last line says that the list is cut");

	// No result has more lines: every kind of line is there or not, for every number of control units
	same = true;
	for(int units = 0; units <= DTC_ECUS_MAX && same; units++)
	{
		for(int shape = 0; shape < 32 && same; shape++)
		{
			bool codes = (shape & 1) != 0, omitted = (shape & 2) != 0, not_ok = (shape & 4) != 0, unconfirmed = (shape & 8) != 0, cut = (shape & 16) != 0;
			int listed = codes && units > 0 ? DTC_CODES_MAX : 0;
			int with_codes = codes || omitted ? units : 0;
			int body = with_codes + listed + (omitted ? units : 0) + (not_ok ? units : 0) + (cut ? 1 : 0);
			int clean = !codes && !omitted && !not_ok && units > 0 ? 1 : 0;

			make(&made, 0);
			for(int i = 0; i < units; i++)
			{
				add_unit("N1 Einheit", "7E0", not_ok ? DTC_ECU_INCOMPLETE : DTC_ECU_OK);
				unit->cleared = unconfirmed ? 0 : 1;
				unit->omitted = omitted ? 3 : 0;
				// The codes a result holds, shared out; the first control unit gets what is left over
				for(int k = 0; codes && k < DTC_CODES_MAX / units + (i == 0 ? DTC_CODES_MAX % units : 0); k++) add_code("C", "00", 1);
			}
			made.cut = cut;
			if(list(&made, ROOM) != 1 + body + clean || cleared(&made_before, &made, ROOM) != 1 + (unconfirmed ? units : 0) + body)
			{
				printf("  %d control units of shape %d: %d lines in the list, %d in the outcome\n", units, shape, list(&made, ROOM), cleared(&made_before, &made, ROOM));
				same = false;
			}
			if(1 + (unconfirmed ? units : 0) + body > DTC_VIEW_LINES_MAX || 1 + body + clean > 202) same = false;
		}
	}
	check(same, "800 shapes of a result, every kind of line there or not for 0 to 24 control units: the number of lines the header gives, never more than 226, never more than 202 in a list");
}

/* The outcome of a clear ------------------------------------------------------------------------------- */

static void test_cleared(void)
{
	static const want_t unconfirmed[] = {
		{DTC_LINE_HEAD, "Gel\xC3\xB6scht 3 von 4", "verbleibend 1"},
		{DTC_LINE_PROBLEM, "ESP", "L\xC3\xB6schen nicht best\xC3\xA4tigt"},
		{DTC_LINE_PROBLEM, "Kombiinstrument", "Löschen nicht bestätigt"},
		{DTC_LINE_ECU, "Kombiinstrument", "796 · 1 Fehler"},
		{DTC_LINE_CODE, "9100", "Status E0"},
	};
	static const want_t ordered[] = {
		{DTC_LINE_HEAD, "Gelöscht 2 von 5", "verbleibend 3"},
		{DTC_LINE_PROBLEM, "Motorelektronik", "Löschen nicht bestätigt"},
		{DTC_LINE_PROBLEM, "Radio", "Löschen nicht bestätigt"},
		{DTC_LINE_ECU, "Motorelektronik", "7E0 · 3 Fehler"},
		{DTC_LINE_CODE, "P0100-13", "aktiv"},
		{DTC_LINE_NOTE, "2 Codes nicht übertragen", ""},
		{DTC_LINE_PROBLEM, "Motorelektronik", "abgelehnt (NRC 22)"},
		{DTC_LINE_PROBLEM, "Radio", "keine Antwort"},
		{DTC_LINE_NOTE, "Liste unvollständig", ""},
	};

	// The list that was read: four codes, one control unit without answer, cut
	make(&made_before, 34300);
	add_unit("N3/28 Motorelektronik (CDID3)", "7E0", DTC_ECU_OK);
	add_code("P0100-13", "2F", 1);
	add_code("P242F-FA", "68", 0);
	add_unit("N10 SAM", "662", DTC_ECU_NO_RESPONSE);
	add_code("9301", "60", -1);
	unit->omitted = 1;
	made_before.cut = true;

	make(&made, 37500);
	add_unit("N3/28 Motorelektronik (CDID3)", "7E0", DTC_ECU_OK);
	unit->cleared = 1;
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	unit->cleared = 1;
	add_unit("N87/8 Radio", "5D6", DTC_ECU_OK);
	check(cleared(&made_before, &made, ROOM) == 1 && line_is(0, DTC_LINE_HEAD, "Gel\xC3\xB6scht 4 von 4", "verbleibend 0"),
	      "everything cleared: the head alone; nothing of the list before, no line of clean control units, no note that the list before was cut");

	make(&made, 0);
	add_unit("N30/4 ESP", "784", DTC_ECU_OK);
	unit->cleared = 0;
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	unit->cleared = 1;
	add_unit("N87/8 Radio", "5D6", DTC_ECU_OK);
	unit->cleared = -1;
	add_unit("A1 Kombiinstrument", "796", DTC_ECU_OK);
	unit->cleared = 0;
	add_code("9100", "E0", -1);
	add_unit("S98 Klimaanlage", "791", DTC_ECU_OK);
	unit->cleared = 2;
	add_unit("N80 Mantelrohrmodul (MRM)", "792", DTC_ECU_OK);
	unit->cleared = -2;
	check(lines_are(cleared(&made_before, &made, ROOM), unconfirmed, COUNT(unconfirmed)),
	      "\"cleared\" false gets a line, with and without codes left, before the control units with codes; true, absent and any other value get none");

	make(&made, 0);
	add_unit("N3/28 Motorelektronik (CDID3)", "7E0", DTC_ECU_NRC);
	unit->nrc = 0x22;
	unit->cleared = 0;
	add_code("P0100-13", "2F", 1);
	unit->omitted = 2;
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	unit->cleared = 1;
	add_unit("N87/8 Radio", "5D6", DTC_ECU_NO_RESPONSE);
	unit->cleared = 0;
	made.cut = true;
	make(&made_before, 0);
	add_unit("N3/28 Motorelektronik (CDID3)", "7E0", DTC_ECU_OK);
	unit->omitted = 5;
	check(lines_are(cleared(&made_before, &made, ROOM), ordered, COUNT(ordered)),
	      "the outcome in the order of the header: head, not confirmed, with codes, not ok, the note of a cut result; no line of clean control units");

	make(&made_before, 0);
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	add_code("9301", "60", -1);
	make(&made, 0);
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	unit->cleared = 1;
	unit->omitted = 3;
	check(cleared(&made_before, &made, ROOM) == 3 && line_is(0, DTC_LINE_HEAD, "Gelöscht 0 von 1", "verbleibend 3"), "more codes left than there were: 0 cleared");

	make(&made_before, 0);
	make(&made, 0);
	check(cleared(&made_before, &made, ROOM) == 1 && line_is(0, DTC_LINE_HEAD, "Gelöscht 0 von 0", "verbleibend 0"), "nothing before and nothing after: 0 of 0");

	make(&made_before, 0);
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	unit->omitted = 1;
	check(cleared(&made_before, &made, ROOM) == 1 && line_is(0, DTC_LINE_HEAD, "Gelöscht 1 von 1", "verbleibend 0"), "one code before and none after: 1 of 1");

	unit->omitted = UINT32_MAX;
	check(cleared(&made_before, &made, ROOM) == 1 && line_is(0, DTC_LINE_HEAD, "Gelöscht 4294967295 von 4294967295", "verbleibend 0") && strlen(lines[0].text) == 35,
	      "the largest numbers in the head of an outcome: a text of 35 bytes, nothing is cut");
	make(&made, 0);
	add_unit("N10 SAM", "662", DTC_ECU_OK);
	unit->cleared = 1;
	unit->omitted = UINT32_MAX;
	check(cleared(&made_before, &made, ROOM) == 3 && line_is(0, DTC_LINE_HEAD, "Gelöscht 0 von 4294967295", "verbleibend 4294967295"),
	      "the largest number left: \"verbleibend 4294967295\"");
}

/* The export ------------------------------------------------------------------------------------------- */

static void set_line(dtc_line_t *line, dtc_line_kind_t kind, const char *text, const char *detail)
{
	memset(line, 0, sizeof(*line));
	line->kind = kind;
	strcpy(line->text, text);
	strcpy(line->detail, detail);
}

// The export of these lines in a room that is large enough
static bool export_of_is(const dtc_line_t *from, int count, const char *text)
{
	int length = export_of(from, count, TEXT_ROOM);

	if(length == (int)strlen(text) && memchr(exported, '\0', TEXT_ROOM) != NULL && strcmp(exported, text) == 0) return true;
	printf("  export returned %d, expected %d \"%s\"\n", length, (int)strlen(text), text);
	return false;
}

static void test_export(void)
{
	static const struct
	{
		dtc_line_kind_t kind;
		const char *text;
		const char *name;
	} kinds[] = {
		{DTC_LINE_HEAD, "T - D\n", "a HEAD line starts in the first column"},
		{DTC_LINE_ECU, "T - D\n", "an ECU line starts in the first column"},
		{DTC_LINE_CODE, "  T - D\n", "a CODE line is indented by two blanks"},
		{DTC_LINE_NOTE, "T - D\n", "a NOTE line starts in the first column"},
		{DTC_LINE_PROBLEM, "T - D\n", "a PROBLEM line starts in the first column"},
		{DTC_LINE_CLEAN, "T - D\n", "a CLEAN line starts in the first column"},
		{(dtc_line_kind_t)6, "T - D\n", "a line of a kind behind the last one starts in the first column"},
		{(dtc_line_kind_t)-1, "T - D\n", "a line of a kind before the first one starts in the first column"},
	};
	static dtc_line_t some[4];
	static char mixed_text[1024];
	size_t mixed_length;
	bool same;
	int count;

	for(int i = 0; i < COUNT(kinds); i++)
	{
		set_line(&some[0], kinds[i].kind, "T", "D");
		snprintf(what, sizeof(what), "export: %s", kinds[i].name);
		check(export_of_is(some, 1, kinds[i].text), what);
	}

	set_line(&some[0], DTC_LINE_ECU, "T", "");
	check(export_of_is(some, 1, "T\n"), "export: a line without detail is its text alone");
	set_line(&some[0], DTC_LINE_CODE, "T", "");
	check(export_of_is(some, 1, "  T\n"), "export: a CODE line without detail is its indented text alone");
	set_line(&some[0], DTC_LINE_ECU, "", "D");
	check(export_of_is(some, 1, " - D\n"), "export: a line without text keeps the hyphen before its detail");
	set_line(&some[0], DTC_LINE_ECU, "", "");
	check(export_of_is(some, 1, "\n"), "export: a line without text and detail is an empty line");
	set_line(&some[0], DTC_LINE_CODE, "", "");
	check(export_of_is(some, 1, "  \n"), "export: a CODE line without text and detail is its indentation");

	set_line(&some[0], DTC_LINE_HEAD, "H", "h");
	set_line(&some[1], DTC_LINE_CODE, "C", "");
	set_line(&some[2], DTC_LINE_CODE, "D", "d");
	set_line(&some[3], DTC_LINE_NOTE, "N", "");
	check(export_of_is(some, 4, "H - h\n  C\n  D - d\nN\n"), "export: four lines in their order, each ended by a line break");
	check(export_of_is(some, 3, "H - h\n  C\n  D - d\n"), "export: the first three of four lines");
	check(export_of_is(some, 1, "H - h\n"), "export: the first of four lines");
	check(export_of_is(some, 0, ""), "export: a count of 0 gives an empty text and the length 0");
	check(export_of_is(some, -1, "") && export_of_is(some, INT_MIN, ""), "export: a count below 0 gives an empty text and the length 0");
	check(export_of(some, 0, 1) == 0 && exported[0] == '\0', "export: an empty text fits into a room of 1 byte");
	check(export_of(some, 0, 0) == -1 && export_of(some, -1, 0) == -1 && export_of(some, 4, 0) == -1,
	      "export: a room of 0 bytes holds nothing, not even an empty text: -1, nothing is written");

	// Two lines of 10 and 2 bytes: the short one must not slip in where the long one did not fit
	set_line(&some[0], DTC_LINE_ECU, "AAAAAAAAA", "");
	set_line(&some[1], DTC_LINE_ECU, "B", "");
	check(export_of(some, 2, 12) == -1 && exported[0] == '\0', "export: 12 bytes of text do not fit into a room of 12");
	check(export_of(some, 2, 13) == 12 && strcmp(exported, "AAAAAAAAA\nB\n") == 0, "export: 12 bytes of text fit into a room of 13");
	check(export_of(some, 2, 5) == -1 && exported[0] == '\0' && export_of(some, 2, 3) == -1 && exported[0] == '\0' && export_of(some, 2, 1) == -1 && exported[0] == '\0',
	      "export: a room for the second line alone is no room for both: -1 and an empty text");

	// The longest line: 2 + 47 + 3 + 39 + 1 bytes
	set_line(&some[0], DTC_LINE_CODE, name_of('t', 47, ""), name_of('d', 39, ""));
	check(export_of(some, 1, 93) == 92 && strlen(exported) == 92 && export_of(some, 1, 92) == -1, "export: the longest line there is has 92 bytes and needs a room of 93");

	// Every size from none to more than enough
	check(parse_fixture("fixtures/dtc_view_mixed.json", &parsed), "fixture dtc_view_mixed.json is the result for the export into rooms of every size");
	count = list(&parsed, ROOM);
	snprintf(mixed_text, sizeof(mixed_text), "%s", export_fixture("dtc_view_mixed.txt"));
	mixed_length = strlen(mixed_text);
	check(mixed_length == 449, "fixtures/dtc_view_mixed.txt has 449 bytes with its last line break");
	same = true;
	for(size_t size = 0; size <= mixed_length; size++)
	{
		int length = export_of(lines, count, size);

		if(length != -1 || (size > 0 && exported[0] != '\0'))
		{
			printf("  export of 449 bytes into a room of %d returned %d\n", (int)size, length);
			same = false;
		}
	}
	check(same, "export of 449 bytes into every room from 0 to 449 bytes: -1 and an empty text, nothing written behind the room");
	same = true;
	for(size_t size = mixed_length + 1; size <= mixed_length + 40; size++)
	{
		int length = export_of(lines, count, size);

		if(length != (int)mixed_length || memchr(exported, '\0', size) == NULL || strcmp(exported, mixed_text) != 0)
		{
			printf("  export of 449 bytes into a room of %d returned %d\n", (int)size, length);
			same = false;
		}
	}
	check(same, "export of 449 bytes into every room from 450 to 489 bytes: the text and its length, nothing written behind the room");
}

/* Bytes of every kind ---------------------------------------------------------------------------------- */

// The text of a code as its line shows it, in the result test_bytes() made for it
static const char *code_shown(const char *code)
{
	strcpy(made.codes[0].code, code);
	return list(&made, 8) == 3 ? lines[2].text : "no line";
}

static void test_bytes(void)
{
	static const unsigned char leads3[] = {0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF};
	static const unsigned char leads4[] = {0xF0, 0xF1, 0xF3, 0xF4, 0xF5, 0xF7};
	static const unsigned char seconds[] = {0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0};
	static const unsigned char others[] = {0x7F, 0x80, 0xBF, 0xC0};
	char code[16];
	char model[16];
	int different = 0, kept = 0, tried = 0;

	make(&made, 0);
	add_unit("Radio", "5D6", DTC_ECU_OK);
	add_code("", "", -1);

	// Two bytes: every pair there is
	for(int first = 1; first < 256; first++)
	{
		for(int second = 1; second < 256; second++)
		{
			code[0] = (char)first;
			code[1] = (char)second;
			code[2] = '\0';
			clean_model(code, 2, model, sizeof(model));
			if(strcmp(code_shown(code), model) != 0) different++;
			if(first >= 0x80 && strcmp(model, code) == 0) kept++;
		}
	}
	check(different == 0, "each of the 65025 pairs of bytes as a code: shown as the rules for UTF-8 say when the number of the character is put together and looked at");
	check(kept == 30 * 64, "of the pairs of bytes 1920 are characters of two bytes: C2 to DF with 80 to BF");

	// Three and four bytes around every limit, between two letters
	different = kept = 0;
	for(int a = 0; a < COUNT(leads3); a++)
	{
		for(int b = 0; b < COUNT(seconds); b++)
		{
			for(int c = 0; c < COUNT(others); c++)
			{
				snprintf(code, sizeof(code), "x%c%c%cy", leads3[a], seconds[b], others[c]);
				clean_model(code, 5, model, sizeof(model));
				if(strcmp(code_shown(code), model) != 0) different++;
				if(strcmp(model, code) == 0) kept++;
				tried++;
			}
		}
	}
	check(different == 0 && tried == 192, "192 texts of three bytes around the limits of the second and third byte: shown as the rules for UTF-8 say");
	// E0 with A0 and BF; E1, EC, EE, EF with 80, 8F, 90, 9F, A0, BF; ED with 80, 8F, 90, 9F; each with 80 and BF as the third byte
	check(kept == (2 + 4 * 6 + 4) * 2, "of them 60 are characters");

	different = kept = tried = 0;
	for(int a = 0; a < COUNT(leads4); a++)
	{
		for(int b = 0; b < COUNT(seconds); b++)
		{
			for(int c = 0; c < COUNT(others); c++)
			{
				for(int d = 0; d < COUNT(others); d++)
				{
					snprintf(code, sizeof(code), "x%c%c%c%cy", leads4[a], seconds[b], others[c], others[d]);
					clean_model(code, 6, model, sizeof(model));
					if(strcmp(code_shown(code), model) != 0) different++;
					if(strcmp(model, code) == 0) kept++;
					tried++;
				}
			}
		}
	}
	check(different == 0 && tried == 768, "768 texts of four bytes around the limits of the second, third and fourth byte: shown as the rules for UTF-8 say");
	// F0 with 90, 9F, A0, BF; F1 and F3 with 80, 8F, 90, 9F, A0, BF; F4 with 80, 8F; each with 80 and BF as the third and as the fourth byte
	check(kept == (4 + 2 * 6 + 2) * 4, "of them 72 are characters");

	// Bytes put together at random, with whole and broken characters next to each other
	different = 0;
	for(int i = 0; i < 20000; i++)
	{
		static const char *const pieces[] = {
			"a", "?", " ", "\n", "\x7F", "\x1F", "\xC3\xBC", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\xC3", "\xE2\x82", "\xE2", "\xF0\x9F\x98", "\xF0\x9F",
			"\xF0", "\x80", "\xBF", "\xC0", "\xC1", "\xED\xA0\x80", "\xE0\x80", "\xF4\x90", "\xFF", "\xFE", "\xED\x9F\xBF", "\xF4\x8F\xBF\xBF",
		};
		size_t length = 0;

		code[0] = '\0';
		for(int pieces_left = (int)random_below(8); pieces_left > 0; pieces_left--)
		{
			const char *piece = pieces[random_below(COUNT(pieces))];

			if(length + strlen(piece) > 15) break;
			strcpy(code + length, piece);
			length += strlen(piece);
		}
		clean_model(code, length, model, sizeof(model));
		if(strcmp(code_shown(code), model) != 0) different++;
		if(!showable(lines[2].text)) different++;
	}
	check(different == 0, "20000 codes put together from whole and broken characters: shown as the rules for UTF-8 say, and what is shown is UTF-8 without control characters");
}

/* Names put together at random ------------------------------------------------------------------------- */

static void test_names_against_the_model(void)
{
	static const char *const pieces[] = {
		" ", " ", "(", "(", ")", ")", "N", "a", "1", "7", "\xC3\xBC", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\xFF", "\xC3", "\n",
	};
	bool same = true;
	bool agree = true;
	int compared = 0;

	for(int i = 0; i < 40000 && same; i++)
	{
		int count = (int)random_below(13);
		size_t size = random_below(56);
		char name[64] = "";
		char plain[256];
		char model[64];

		// The last three pieces are no characters: only every other name has some
		for(int c = 0; c < count; c++) strcat(name, pieces[random_below((uint32_t)COUNT(pieces) - (i % 2 == 0 ? 3 : 0))]);
		plain_model(name, plain);
		if(size > 0) clean_model(plain, strlen(plain), model, size);
		same = plain_is(name, size, size > 0 ? model : "");

		// Without parentheses the name for a list and the short name of dtc_model.h follow the same rule
		if(i % 2 == 0 && strpbrk(name, "()") == NULL && size > 0)
		{
			char short_name[64];

			dtc_short_name(name, short_name, size);
			if(strcmp(short_name, model) != 0) agree = false;
			compared++;
		}
	}
	check(same, "40000 names put together at random, in rooms of 0 to 55 bytes: the name for a list the rules give when applied a second way");
	check(agree && compared > 1000, "names of whole characters without parentheses: the name for a list is the short name of dtc_model.h");
}

/* In a child process ----------------------------------------------------------------------------------- */

// Runs a part of the test in a child process: a crash or a hang of the module is then a failed check here,
// not the end of the test. Returns what the part returned (0 to 100), or -1 if it did not come back.
static int in_child(int (*part)(void))
{
	int status = 0;
	pid_t child;

	fflush(stdout);
	child = fork();
	if(child == 0)
	{
		int result;

		// A module that never returns ends the child here
		alarm(120);
		result = part();
		fflush(stdout);
		_exit(result);
	}
	if(child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status)) return -1;
	return WEXITSTATUS(status);
}

// Who has no room needs no lines. Returns the number of calls that did not return what the header says.
static int calls_without_lines(void)
{
	int wrong = 0;
	char out[8] = "~~~~~~~";

	make(&made_before, 0);
	make_largest(&made);
	if(dtc_view_list(&made, NULL, 0) != 0) wrong++;
	if(dtc_view_list(&made, NULL, -1) != 0) wrong++;
	if(dtc_view_list(&made, NULL, INT_MIN) != 0) wrong++;
	if(dtc_view_cleared(&made_before, &made, NULL, 0) != 0) wrong++;
	if(dtc_view_cleared(&made_before, &made, NULL, -1) != 0) wrong++;
	if(dtc_view_cleared(&made_before, &made, NULL, INT_MIN) != 0) wrong++;
	if(dtc_view_export(NULL, 0, out, sizeof(out)) != 0 || out[0] != '\0') wrong++;
	if(dtc_view_export(NULL, -1, out, sizeof(out)) != 0 || out[0] != '\0') wrong++;
	if(dtc_view_export(NULL, 0, NULL, 0) != -1) wrong++;
	return wrong;
}

static void test_without_lines(void)
{
	int wrong = in_child(calls_without_lines);

	check(wrong != -1, "calls without lines (NULL) for a room of 0 or less: no crash");
	check(wrong == 0, "calls without lines (NULL) for a room of 0 or less: 0 lines, an empty export, -1 for an export without room");
}

/*
 * The list a second way: every line is written as one text with printf, with the name by plain_model, and
 * then cleaned and cut as a whole by clean_model; the numbers are added up in 64 bit.
 */
static dtc_line_t model[ROOM];
static int model_count;
static char model_text[TEXT_ROOM];

static void model_line(dtc_line_kind_t kind, const char *text, const char *detail)
{
	if(model_count >= ROOM) return;
	model[model_count].kind = kind;
	clean_model(text, strlen(text), model[model_count].text, sizeof(model[model_count].text));
	clean_model(detail, strlen(detail), model[model_count].detail, sizeof(model[model_count].detail));
	model_count++;
}

static uint64_t model_codes(const dtc_result_t *result)
{
	uint64_t codes = 0;

	for(int i = 0; i < result->ecu_count; i++) codes += (uint64_t)result->ecus[i].code_count + result->ecus[i].omitted;
	return codes > UINT32_MAX ? UINT32_MAX : codes;
}

// The lines of the control units, and at the end the note of a cut result. `clean`: with the line of the
// clean control units.
static void model_units(const dtc_result_t *result, bool clean)
{
	static const char *const words[] = {"", "keine Antwort", "Antwort ausstehend", "unvollständig"};
	char text[256], detail[256];
	int clean_units = 0;

	for(int i = 0; i < result->ecu_count; i++)
	{
		const dtc_ecu_t *ecu = &result->ecus[i];
		uint64_t codes = (uint64_t)ecu->code_count + ecu->omitted;

		if(codes == 0)
		{
			if(ecu->status == DTC_ECU_OK) clean_units++;
			continue;
		}
		plain_model(ecu->name, text);
		snprintf(detail, sizeof(detail), "%s \xC2\xB7 %llu Fehler", ecu->id, (unsigned long long)(codes > UINT32_MAX ? UINT32_MAX : codes));
		model_line(DTC_LINE_ECU, text, detail);
		for(int k = 0; k < ecu->code_count; k++)
		{
			const dtc_code_t *code = &result->codes[ecu->first_code + k];

			snprintf(detail, sizeof(detail), "Status %s", code->status);
			model_line(DTC_LINE_CODE, code->code, code->active == 1 ? "aktiv" : code->active == 0 ? "gespeichert" : strlen(code->status) > 0 ? detail : "");
		}
		if(ecu->omitted != 0)
		{
			snprintf(text, sizeof(text), "%" PRIu32 " Code%s nicht übertragen", ecu->omitted, ecu->omitted == 1 ? "" : "s");
			model_line(DTC_LINE_NOTE, text, "");
		}
	}
	for(int i = 0; i < result->ecu_count; i++)
	{
		const dtc_ecu_t *ecu = &result->ecus[i];

		if(ecu->status == DTC_ECU_OK) continue;
		plain_model(ecu->name, text);
		snprintf(detail, sizeof(detail), "abgelehnt (NRC %c%c)", "0123456789ABCDEF"[ecu->nrc >> 4], "0123456789ABCDEF"[ecu->nrc & 15]);
		model_line(DTC_LINE_PROBLEM, text,
		           ecu->status == DTC_ECU_NRC ? detail : ecu->status >= DTC_ECU_NO_RESPONSE && ecu->status <= DTC_ECU_INCOMPLETE ? words[ecu->status] : "unbekannter Status");
	}
	if(clean && clean_units > 0)
	{
		snprintf(text, sizeof(text), "%d Steuergerät%s ohne Fehler", clean_units, clean_units == 1 ? "" : "e");
		model_line(DTC_LINE_CLEAN, text, "");
	}
	if(result->cut) model_line(DTC_LINE_NOTE, "Liste unvollständig", "");
}

static void model_list(const dtc_result_t *result)
{
	char text[256], detail[256];

	model_count = 0;
	snprintf(text, sizeof(text), "%llu Fehler", (unsigned long long)model_codes(result));
	snprintf(detail, sizeof(detail), "%d Steuergerät%s \xC2\xB7 %llu s", result->ecu_count, result->ecu_count == 1 ? "" : "e",
	         (unsigned long long)(((uint64_t)result->duration_ms + 500) / 1000));
	model_line(DTC_LINE_HEAD, text, detail);
	model_units(result, true);
}

static void model_cleared(const dtc_result_t *before, const dtc_result_t *after)
{
	uint64_t was = model_codes(before), is = model_codes(after);
	char text[256], detail[256];

	model_count = 0;
	snprintf(text, sizeof(text), "Gelöscht %llu von %llu", (unsigned long long)(was > is ? was - is : 0), (unsigned long long)was);
	snprintf(detail, sizeof(detail), "verbleibend %llu", (unsigned long long)is);
	model_line(DTC_LINE_HEAD, text, detail);
	for(int i = 0; i < after->ecu_count; i++)
	{
		if(after->ecus[i].cleared != 0) continue;
		plain_model(after->ecus[i].name, text);
		model_line(DTC_LINE_PROBLEM, text, "Löschen nicht bestätigt");
	}
	model_units(after, false);
}

// The lines of the model in a room of `max` lines
static void model_limit(int max)
{
	if(max <= 0)
	{
		model_count = 0;
	}
	else if(model_count > max)
	{
		model_count = max - 1;
		model_line(DTC_LINE_NOTE, "Liste gekürzt", "");
	}
}

static void model_export(void)
{
	size_t length = 0;

	model_text[0] = '\0';
	for(int i = 0; i < model_count; i++)
	{
		length += (size_t)snprintf(model_text + length, sizeof(model_text) - length, "%s%s%s%s\n", model[i].kind == DTC_LINE_CODE ? "  " : "", model[i].text,
		                           model[i].detail[0] != '\0' ? " - " : "", model[i].detail);
	}
}

// A text of at most `room` bytes put together from at most `most` pieces
static void random_text(char *out, size_t room, const char *const *pieces, int piece_count, int most)
{
	size_t length = 0;

	out[0] = '\0';
	for(int left = (int)random_below((uint32_t)most + 1); left > 0; left--)
	{
		const char *piece = pieces[random_below((uint32_t)piece_count)];

		if(length + strlen(piece) > room) break;
		strcpy(out + length, piece);
		length += strlen(piece);
	}
}

// A result of any shape: few or all control units, every status and some that are none, every flag,
// omitted codes up to the largest number, names and texts with whole and broken characters
static void random_result(dtc_result_t *result)
{
	static const uint32_t durations[] = {0, 499, 500, 34300, 37500, 4294966999u, 4294967295u};
	static const uint32_t omitted[] = {0, 0, 0, 0, 1, 2, 75, UINT32_MAX - 1, UINT32_MAX};
	static const int statuses[] = {
		DTC_ECU_OK, DTC_ECU_OK, DTC_ECU_OK, DTC_ECU_NO_RESPONSE, DTC_ECU_PENDING_TIMEOUT, DTC_ECU_INCOMPLETE, DTC_ECU_NRC, DTC_ECU_OTHER, 6, -1,
	};
	static const int flags[] = {-1, -1, 0, 0, 1, 1, 2, -2};
	static const char *const name_pieces[] = {
		" ", " ", "(", ")", "N", "Tür", "1", "7", "\xC3\xBC", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\xFF", "\xC3", "\n", "\xED\xA0\x80", "Steuergerät",
	};
	static const char *const text_pieces[] = {"7", "E", "0", "P", "-", "F", "\xC3\xBC", "\xFF", "\xE2\x82", "\t"};
	bool full = random_below(8) == 0;
	int units = full ? DTC_ECUS_MAX : (int)random_below(7);

	make(result, random_below(2) == 0 ? durations[random_below(COUNT(durations))] : random_below(100000));
	for(int i = 0; i < units; i++)
	{
		char name[64], id[8];
		int codes = full ? 6 : (int)random_below(5);

		if(random_below(3) == 0) strcpy(name, UNITS[random_below(COUNT(UNITS))].name);
		else random_text(name, sizeof(name) - 1, name_pieces, COUNT(name_pieces), 14);
		random_text(id, sizeof(id) - 1, text_pieces, COUNT(text_pieces), 4);
		add_unit(name, id, (dtc_ecu_status_t)statuses[random_below(COUNT(statuses))]);
		unit->uds = random_below(2) == 0;
		unit->nrc = (uint8_t)random_below(256);
		unit->cleared = (int8_t)flags[random_below(COUNT(flags))];
		unit->omitted = omitted[random_below(COUNT(omitted))];
		for(int k = 0; k < codes && result->code_count < DTC_CODES_MAX; k++)
		{
			char code[16], status[8];

			random_text(code, sizeof(code) - 1, text_pieces, COUNT(text_pieces), 8);
			random_text(status, sizeof(status) - 1, text_pieces, COUNT(text_pieces), 3);
			add_code(code, status, flags[random_below(COUNT(flags))]);
		}
	}
	result->cut = random_below(4) == 0;
	result->dtc_count = random_below(300);
}

// The lines of the last call are the ones of the model, and nothing in them is what a line must not hold
static bool same_as_model(int count, const char *call, int number)
{
	bool same = count == model_count;

	for(int i = 0; same && i < count; i++)
	{
		same = lines[i].kind == model[i].kind && strcmp(lines[i].text, model[i].text) == 0 && strcmp(lines[i].detail, model[i].detail) == 0 &&
		       showable(lines[i].text) && showable(lines[i].detail);
		if(!same)
		{
			printf("  result %d, %s, line %d: module %s \"%s\" \"%s\", model %s \"%s\" \"%s\"\n", number, call, i, kind_name(lines[i].kind), lines[i].text,
			       lines[i].detail, kind_name(model[i].kind), model[i].text, model[i].detail);
		}
	}
	if(count != model_count) printf("  result %d, %s: module %d lines, model %d\n", number, call, count, model_count);
	return same;
}

// The export of the last call into rooms around the size it needs is the one of the model
static bool export_as_model(int count, int number)
{
	size_t needed;

	model_export();
	needed = strlen(model_text) + 1;
	for(size_t size = needed > 2 ? needed - 2 : 0; size <= needed + 1; size++)
	{
		int length = export_of(lines, count, size);
		bool same = size >= needed ? length == (int)(needed - 1) && memchr(exported, '\0', size) != NULL && strcmp(exported, model_text) == 0
		                           : length == -1 && (size == 0 || exported[0] == '\0');

		if(!same)
		{
			printf("  result %d: export of %d bytes into a room of %d returned %d\n", number, (int)needed - 1, (int)size, length);
			return false;
		}
	}
	return true;
}

// Returns 0 if module and model agree on every result, else 10
static int results_against_the_model(void)
{
	int longest = 0;

	make(&made_before, 0);
	for(int i = 0; i < 3000; i++)
	{
		int full, max;

		random_result(&made);

		model_list(&made);
		full = model_count;
		if(!same_as_model(list(&made, ROOM), "list", i) || full > 202 || !export_as_model(full, i)) return 10;
		max = (int)random_below((uint32_t)full + 3);
		model_limit(max);
		if(!same_as_model(list(&made, max), "list in a room that is too small", i) || !export_as_model(model_count, i)) return 10;

		model_cleared(&made_before, &made);
		full = model_count;
		if(full > longest) longest = full;
		if(!same_as_model(cleared(&made_before, &made, ROOM), "outcome", i) || full > DTC_VIEW_LINES_MAX || !export_as_model(full, i)) return 10;
		max = (int)random_below((uint32_t)full + 3);
		model_limit(max);
		if(!same_as_model(cleared(&made_before, &made, max), "outcome in a room that is too small", i) || !export_as_model(model_count, i)) return 10;

		made_before = made;
	}
	printf("  3000 results, the longest outcome has %d lines\n", longest);
	return longest > 150 ? 0 : 10;
}

static void test_results_against_the_model(void)
{
	int result = in_child(results_against_the_model);

	check(result == 0 || result == 10, "3000 made-up results: no crash");
	check(result == 0, "3000 made-up results: list, outcome and export in rooms of every size are what the header gives when it is followed a second way, "
	      "every line is UTF-8 without control characters, none has more than 226 lines");
}

int main(void)
{
	test_constants();
	test_plain_name();
	test_clean();
	test_fixtures();
	test_head();
	test_units();
	test_problems();
	test_clean_and_cut();
	test_long_names();
	test_max();
	test_largest();
	test_cleared();
	test_export();
	test_bytes();
	test_names_against_the_model();
	test_without_lines();
	test_results_against_the_model();
	return test_end();
}
