/*
 * Host test for display/components/core/layout.c. Run "make test_layout && ./test_layout" in display/test.
 * redproof.py removes or weakens every rule once (mutations/layout.py) and expects this test to fail.
 */
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "test.h"
#include "layout.h"
#include "fmt.h"

#define GUARD   0xA5
// What a layout holds before layout_parse is called. Not the guard byte: a bool can only be read if it is
// 0 or 1. Not zero either: zero is what layout_parse writes where the text says nothing.
#define STALE   0x01
// A limit that is not set
#define NONE    NAN
#define ROOM    64      // largest buffer tried for the text of an item, behind it more guard bytes

// The layout lies between guard bytes. Behind it there is room for two pages: a page written behind the
// last one lands there and is seen, where the address sanitizer would only stop the program without
// naming a rule.
typedef struct
{
	unsigned char before[256];
	layout_t layout;
	unsigned char after[2 * sizeof(layout_page_t)];
} box_t;

typedef struct
{
	unsigned char before[64];
	layout_report_t report;
	unsigned char after[64];
} report_box_t;

static box_t box, snapshot;
static report_box_t reported;
static layout_t made, expected;
static catalog_t catalog;
static values_t values;

// Two tokens before and behind the room the reader is told about show whether it leaves that room.
// The two before it look like texts: a member that is looked up at index -1 is then no number, no array
// and no object.
static json_token_t work_between_guards[2 + LAYOUT_TOKENS + 2];
static json_token_t *const work = &work_between_guards[2];
static const json_token_t front_guard = {JSON_STRING, 0, 1, 0, 1};
static json_token_t catalog_work[CATALOG_TOKENS];
static json_token_t values_work[VALUES_TOKENS];

static char text[65536];        // the layout under test
static char part[32768];        // a piece of it
static size_t text_length;      // of a layout written with emit()

// Every test that draws numbers begins with a seed of its own: what it draws does not depend on the tests
// before it
static uint32_t random_state;

static void random_seed(uint32_t seed)
{
	random_state = seed;
}

static uint32_t random_next(void)
{
	random_state = random_state * 1664525u + 1013904223u;
	return random_state >> 8;
}

static int random_below(int limit)
{
	return (int)(random_next() % (uint32_t)limit);
}

static bool bytes_are(const void *memory, size_t count, unsigned char value)
{
	const unsigned char *bytes = memory;
	size_t i;

	for(i = 0; i < count; i++)
	{
		if(bytes[i] != value) return false;
	}
	return true;
}

static bool guards_intact(void)
{
	return bytes_are(box.before, sizeof(box.before), GUARD) && bytes_are(box.after, sizeof(box.after), GUARD) &&
	       bytes_are(reported.before, sizeof(reported.before), GUARD) && bytes_are(reported.after, sizeof(reported.after), GUARD);
}

// A text field holds exactly this text. Does not read behind the field, whatever it holds.
static bool field_is(const char *field, size_t size, const char *wanted)
{
	size_t length = strlen(wanted);

	return length < size && memcmp(field, wanted, length + 1) == 0;
}

#define TEXT_IS(field, wanted) field_is(field, sizeof(field), wanted)

// A layout full of stale bytes between fresh guard bytes
static void start(void)
{
	memset(&box, GUARD, sizeof(box));
	memset(&box.layout, STALE, sizeof(box.layout));
}

// The report holds guard bytes before the call: what it says afterwards was written by the call
static bool parse(const char *json, size_t length)
{
	memset(&reported, GUARD, sizeof(reported));
	return layout_parse(json, length, &box.layout, &reported.report, work, LAYOUT_TOKENS);
}

static bool report_is(const char *path, const char *problem, int warnings, const char *warning_path, const char *warning)
{
	const layout_report_t *report = &reported.report;

	return TEXT_IS(report->path, path) && TEXT_IS(report->problem, problem) && report->warnings == warnings &&
	       TEXT_IS(report->warning_path, warning_path) && TEXT_IS(report->warning, warning);
}

static void show_report(void)
{
	const layout_report_t *report = &reported.report;

	printf("  report: \"%.47s\": \"%.63s\", %d warnings, the first \"%.47s\": \"%.63s\"\n",
	       report->path, report->problem, report->warnings, report->warning_path, report->warning);
}

// The text is read into a fresh layout, with this many warnings
static bool accepted_with(const char *json, size_t length, int warnings, const char *warning_path, const char *warning)
{
	start();
	if(!parse(json, length) || !report_is("", "", warnings, warning_path, warning) || !guards_intact())
	{
		show_report();
		return false;
	}
	return true;
}

// The text is read into a fresh layout without a problem and without a warning
static bool accepted(const char *json)
{
	return accepted_with(json, strlen(json), 0, "", "");
}

static bool warned(const char *json, int warnings, const char *warning_path, const char *warning)
{
	return accepted_with(json, strlen(json), warnings, warning_path, warning);
}

// The text is refused: the layout is untouched, the report names the place and the problem and no warning
static bool refused_length(const char *json, size_t length, const char *path, const char *problem)
{
	bool result;

	start();
	memcpy(&snapshot, &box, sizeof(box));
	result = parse(json, length);
	if(result || memcmp(&snapshot, &box, sizeof(box)) != 0 || !report_is(path, problem, 0, "", "") || !guards_intact())
	{
		printf("  expected \"%s\": \"%s\"%s\n", path, problem, result ? ", but the layout was accepted" : "");
		show_report();
		return false;
	}
	return true;
}

static bool refused(const char *json, const char *path, const char *problem)
{
	return refused_length(json, strlen(json), path, problem);
}

// A call that a broken module may not survive (a pointer that may be NULL, a text one byte too long for the
// room of the reader) runs in a child process: a crash or a hang is then a failed check that names its rule,
// not the end of the test. The child says with its exit status whether the answer was right.
static bool in_child(bool (*right)(void))
{
	int status = 0;
	pid_t child;

	fflush(stdout);
	child = fork();
	if(child == 0)
	{
		bool result;

		alarm(60);
		result = right();
		fflush(stdout);
		_exit(result ? 0 : 1);
	}
	return child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// The tests write ' for " in their JSON texts
static char *quoted(char *json)
{
	char *c;

	for(c = json; *c != '\0'; c++)
	{
		if(*c == '\'') *c = '"';
	}
	return json;
}

static const char *doc(const char *format, ...) __attribute__((format(printf, 1, 2)));
static const char *piece(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void emit(const char *format, ...) __attribute__((format(printf, 1, 2)));

// A whole text in `text`
static const char *doc(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	return quoted(text);
}

// A piece of a text in `part`
static const char *piece(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(part, sizeof(part), format, arguments);
	va_end(arguments);
	return part;
}

// Appends to `text`, without changing quotes
static void emit(const char *format, ...)
{
	va_list arguments;

	if(text_length >= sizeof(text)) return;
	va_start(arguments, format);
	text_length += (size_t)vsnprintf(text + text_length, sizeof(text) - text_length, format, arguments);
	va_end(arguments);
}

#define FORMAT  "'format':'wican-display-layout'"
#define HEAD    "{" FORMAT ",'v':1,"
#define ITEM    "{'key':'K'}"
#define PAGE    "{'items':[" ITEM "]}"
#define PAGES   "'pages':[" PAGE "]"

// A layout with these pages
static const char *with_pages(const char *pages)
{
	return doc(HEAD "'pages':[%s]}", pages);
}

// A layout of one page with these members
static const char *with_page(const char *members)
{
	return doc(HEAD "'pages':[{%s}]}", members);
}

// A layout of one page with these items
static const char *with_items(const char *items)
{
	return doc(HEAD "'pages':[{'items':[%s]}]}", items);
}

// A layout of one page with one item that has these members and no others
static const char *with_item(const char *members)
{
	return doc(HEAD "'pages':[{'items':[{%s}]}]}", members);
}

// A layout of one page with one item that has the key K and these members
static const char *with_members(const char *members)
{
	return doc(HEAD "'pages':[{'items':[{'key':'K',%s}]}]}", members);
}

// A text of `count` bytes, "abcdefghij...", so that a lost or doubled byte is seen. Four of them can be
// in use at a time.
static const char *letters(int count)
{
	static char texts[4][160];
	static int next;
	char *out = texts[next++ % 4];
	int i;

	for(i = 0; i < count; i++) out[i] = (char)('a' + i % 26);
	out[count] = '\0';
	return out;
}

// `count` times the same text
static const char *times(int count, const char *unit)
{
	static char texts[4][512];
	static int next;
	char *out = texts[next++ % 4];
	int i;

	out[0] = '\0';
	for(i = 0; i < count; i++) strcat(out, unit);
	return out;
}

static const layout_item_t *first_item(void)
{
	return &box.layout.pages[0].items[0];
}

// unit NULL: the item has none
static bool item_is(const layout_item_t *item, const char *key, const char *label, const char *unit, int decimals,
                    layout_widget_t widget, double scale)
{
	return TEXT_IS(item->key, key) && TEXT_IS(item->label, label) && TEXT_IS(item->unit, unit != NULL ? unit : "") &&
	       item->has_unit == (unit != NULL) && item->decimals == decimals && item->widget == widget && item->scale == scale;
}

static bool limit_is(const layout_limit_t *limit, double wanted)
{
	if(isnan(wanted)) return !limit->set && limit->value == 0;
	return limit->set && limit->value == wanted;
}

static bool limits_are(const layout_item_t *item, double min, double max, double warn_lo, double warn_hi, double crit_lo, double crit_hi)
{
	return limit_is(&item->min, min) && limit_is(&item->max, max) && limit_is(&item->warn_lo, warn_lo) &&
	       limit_is(&item->warn_hi, warn_hi) && limit_is(&item->crit_lo, crit_lo) && limit_is(&item->crit_hi, crit_hi);
}

static bool map_is(const layout_item_t *item, int index, const char *raw, const char *shown)
{
	return index < item->map_count && TEXT_IS(item->map[index].raw, raw) && TEXT_IS(item->map[index].text, shown);
}

// An item as the text {"key":"K"} gives it
static bool is_plain(const layout_item_t *item, const char *key)
{
	return item_is(item, key, "", NULL, 0, LAYOUT_WIDGET_NUMBER, 1) && limits_are(item, NONE, NONE, NONE, NONE, NONE, NONE) &&
	       item->map_count == 0;
}

// The catalogue as it is before the adapter was heard, then the values of a configuration
static bool load_catalog(const char *config)
{
	static char json[4096];

	snprintf(json, sizeof(json), "%s", config);
	quoted(json);
	catalog_init(&catalog);
	return config[0] == '\0' || catalog_apply_config(&catalog, json, strlen(json), catalog_work, CATALOG_TOKENS);
}

static bool load_values(const char *answer, uint64_t now_ms)
{
	static char json[4096];

	snprintf(json, sizeof(json), "%s", answer);
	quoted(json);
	values_init(&values);
	return values_apply(&values, json, strlen(json), -1, now_ms, values_work, VALUES_TOKENS) == VALUES_RENEWED;
}

// A layout built by hand for the functions that only read one
static void make_layout(int pages)
{
	memset(&made, 0, sizeof(made));
	made.page_count = (uint8_t)pages;
}

static layout_item_t *add_item(int page, const char *key)
{
	layout_page_t *p = &made.pages[page];
	layout_item_t *item = &p->items[p->item_count++];

	strcpy(item->key, key);
	item->scale = 1;
	return item;
}

/*
 * layout_parse: what is read
 */

static void test_example(void)
{
	static char file[4096];
	const layout_t *layout = &box.layout;
	const layout_page_t *motor = &layout->pages[0];
	const layout_page_t *dpf = &layout->pages[1];

	check(read_fixture("fixtures/layout_example.json", file, sizeof(file)), "example: the fixture is there");
	check(accepted(file), "example: the layout of the header is accepted without a problem and without a warning");
	check(TEXT_IS(layout->name, "W906 OM651 Standard") && TEXT_IS(layout->profile_hint, "W906"), "example: name and profile hint");
	check(layout->page_count == 2, "example: two pages");
	check(TEXT_IS(motor->title, "Motor") && !motor->hidden && motor->item_count == 4, "example: the first page has its title, is not hidden and has four items");
	check(item_is(&motor->items[0], "ENGINE_RPM", "Drehzahl", "1/min", 0, LAYOUT_WIDGET_ARC, 1) &&
	      limits_are(&motor->items[0], 0, 5000, NONE, NONE, NONE, NONE) && motor->items[0].map_count == 0,
	      "example: an arc with key, label, unit, decimals and its range");
	check(item_is(&motor->items[1], "COOLANT_TMP", "K\303\274hlwasser", "\302\260C", 0, LAYOUT_WIDGET_NUMBER, 1) &&
	      limits_are(&motor->items[1], NONE, NONE, NONE, NONE, NONE, NONE), "example: label and unit in UTF-8, the widget is number without a member widget");
	check(item_is(&motor->items[2], "RAIL_PRESSURE", "Raildruck", "bar", 0, LAYOUT_WIDGET_NUMBER, 0.001) &&
	      limits_are(&motor->items[2], NONE, NONE, NONE, 1900, NONE, NONE), "example: scale and upper warn limit");
	check(item_is(&motor->items[3], "DPF_REGEN_STATUS", "Regeneration", NULL, 0, LAYOUT_WIDGET_STATE, 1) &&
	      motor->items[3].map_count == 2 && map_is(&motor->items[3], 0, "1", "inaktiv") && map_is(&motor->items[3], 1, "*", "aktiv"),
	      "example: a state widget without unit, its map in the order of the text");
	check(TEXT_IS(dpf->title, "DPF") && dpf->hidden && dpf->item_count == 2, "example: the second page is hidden and has two items");
	check(item_is(&dpf->items[0], "DPF_SOOT_MASS", "Ru\303\237 gemessen", "g", 1, LAYOUT_WIDGET_NUMBER, 1) &&
	      limits_are(&dpf->items[0], NONE, NONE, NONE, 30, NONE, 45), "example: one decimal, upper warn and crit limit");
	check(item_is(&dpf->items[1], "DPF_KM_SINCE_REGEN", "km seit Regeneration", "km", 0, LAYOUT_WIDGET_NUMBER, 1), "example: the last item of the last page");
}

static void test_defaults(void)
{
	const layout_t *layout = &box.layout;

	check(accepted(doc(HEAD PAGES "}")), "defaults: format, v and one page with one item that has a key are a layout");
	check(TEXT_IS(layout->name, "") && TEXT_IS(layout->profile_hint, ""), "defaults: name and profile_hint are optional and empty");
	check(layout->page_count == 1 && TEXT_IS(layout->pages[0].title, "") && !layout->pages[0].hidden && layout->pages[0].item_count == 1,
	      "defaults: title is optional and empty, a page is not hidden");
	check(TEXT_IS(first_item()->key, "K") && TEXT_IS(first_item()->label, ""), "defaults: label is optional and empty");
	check(!first_item()->has_unit && TEXT_IS(first_item()->unit, ""), "defaults: an item without unit has none of its own");
	check(first_item()->decimals == 0, "defaults: dec is 0");
	check(first_item()->widget == LAYOUT_WIDGET_NUMBER, "defaults: the widget is number");
	check(first_item()->scale == 1, "defaults: scale is 1");
	check(limits_are(first_item(), NONE, NONE, NONE, NONE, NONE, NONE), "defaults: no range and no limits, their values are 0");
	check(first_item()->map_count == 0, "defaults: no map");
	check(bytes_are(&layout->pages[0].items[1], 5 * sizeof(layout_item_t), 0) && bytes_are(&layout->pages[1], 11 * sizeof(layout_page_t), 0),
	      "defaults: the layout is filled completely, items and pages the text does not have are zero");

	check(accepted(doc("{'pages':[{'items':[{'label':'L','key':'K'}],'title':'T'}],'profile_hint':'H','name':'N','v':1," FORMAT "}")) &&
	      TEXT_IS(layout->name, "N") && TEXT_IS(layout->profile_hint, "H") && TEXT_IS(layout->pages[0].title, "T") &&
	      TEXT_IS(first_item()->key, "K") && TEXT_IS(first_item()->label, "L"), "members are found in any order");
	check(accepted(doc(" {\n\t" FORMAT " ,\r\n 'v' : 1 , 'pages' : [ { 'items' : [ { 'key' : 'K' } ] } ] } \n")) && is_plain(first_item(), "K"),
	      "whitespace between the tokens and around the layout does not matter");
}

static void test_every_member(void)
{
	static char file[4096];
	const layout_t *layout = &box.layout;
	const layout_page_t *page = &layout->pages[0];

	check(read_fixture("fixtures/layout_every_member.json", file, sizeof(file)), "every member: the fixture is there");
	check(accepted(file), "every member: accepted without a problem and without a warning");
	check(TEXT_IS(layout->name, "Jeder Schl\303\274ssel einmal") && TEXT_IS(layout->profile_hint, "Pr\303\274fstand"),
	      "every member: escapes in name and profile_hint are resolved");
	check(layout->page_count == 2 && TEXT_IS(page->title, "Gr\303\266\303\237en") && !page->hidden && page->item_count == 3,
	      "every member: \"hidden\":false is not hidden, members the format does not know are ignored");
	check(item_is(&page->items[0], "A", "Ladedruck", "bar", 2, LAYOUT_WIDGET_BAR, 0.001), "every member: key, label, unit, dec, widget bar, scale");
	check(limits_are(&page->items[0], -1, 2.5, -0.5, 1.8, -0.75, 2.2), "every member: min, max, warn_lo, warn_hi, crit_lo, crit_hi, each in its own place");
	check(page->items[0].map_count == 1 && map_is(&page->items[0], 0, "0", "null"), "every member: a map is kept with a widget that is no state");
	check(item_is(&page->items[1], "@BATT_V", "", "", 3, LAYOUT_WIDGET_NUMBER, -1000) && page->items[1].has_unit,
	      "every member: an empty unit is a unit of the item, an empty label is none, scale may be negative, widget number");
	check(item_is(&page->items[2], "SWITCH", "", NULL, 0, LAYOUT_WIDGET_STATE, 1) && page->items[2].map_count == 3 &&
	      map_is(&page->items[2], 0, "on", "l\303\244uft") && map_is(&page->items[2], 1, "off", "steht") && map_is(&page->items[2], 2, "*", "?"),
	      "every member: a state widget with three map entries");
	check(TEXT_IS(layout->pages[1].title, "") && layout->pages[1].hidden && layout->pages[1].item_count == 1 &&
	      is_plain(&layout->pages[1].items[0], "\360\237\230\200\360\237\230\200"),
	      "every member: an empty title, a hidden page, a key of four byte characters, written out and as a surrogate pair");
}

static void test_ignored_members(void)
{
	check(accepted(doc("{'x':1," FORMAT ",'later':{'pages':[]},'v':1,'Pages':5," PAGES ",'pages2':null}")) && box.layout.page_count == 1,
	      "unknown members of the layout are ignored, also with names that begin like known ones");
	check(accepted(with_page("'x':[1,2],'items':[" ITEM "],'Hidden':true,'titles':5")) && !box.layout.pages[0].hidden &&
	      TEXT_IS(box.layout.pages[0].title, ""), "unknown members of a page are ignored");
	check(accepted(with_item("'colour':'red','key':'K','Label':'x','units':'y','decimals':2,'min ':1,'maps':{'a':1},'warn':5,'crit_hi2':'z'")) &&
	      is_plain(first_item(), "K"), "unknown members of an item are ignored");
	check(accepted(doc("{" FORMAT ",'v':1,'name':'A','name':'B'," PAGES ",'pages':[]}")) && TEXT_IS(box.layout.name, "A") && box.layout.page_count == 1,
	      "of two members with the same name the first counts");
	check(accepted(with_item("'key':'K','key':'','dec':1,'dec':9")) && TEXT_IS(first_item()->key, "K") && first_item()->decimals == 1,
	      "of two members of an item with the same name the first counts");
}

static void test_widgets(void)
{
	const layout_item_t *item = first_item();

	check(accepted(with_members("'widget':'number'")) && item->widget == LAYOUT_WIDGET_NUMBER, "widget number");
	check(accepted(with_members("'widget':'arc','min':0,'max':1")) && item->widget == LAYOUT_WIDGET_ARC && limits_are(item, 0, 1, NONE, NONE, NONE, NONE),
	      "widget arc with min and max");
	check(accepted(with_members("'widget':'bar','min':-5,'max':5")) && item->widget == LAYOUT_WIDGET_BAR && limits_are(item, -5, 5, NONE, NONE, NONE, NONE),
	      "widget bar with min and max");
	check(accepted(with_members("'widget':'state','map':{'1':'an'}")) && item->widget == LAYOUT_WIDGET_STATE && item->map_count == 1 &&
	      map_is(item, 0, "1", "an"), "widget state with a map of one entry");
	check(accepted(with_members("'widget':'st\\u0061te','map':{'\\u0031':'\\u0061n'}")) && item->widget == LAYOUT_WIDGET_STATE &&
	      map_is(item, 0, "1", "an"), "escapes in the widget and in the map are resolved");
	check(accepted(with_members("'widget':'number','min':0,'max':10,'map':{'1':'an'}")) && item->widget == LAYOUT_WIDGET_NUMBER &&
	      limits_are(item, 0, 10, NONE, NONE, NONE, NONE) && item->map_count == 1, "a number widget keeps a range and a map it is given");
	check(accepted(with_members("'min':3")) && limits_are(item, 3, NONE, NONE, NONE, NONE, NONE), "min alone is accepted");
	check(accepted(with_members("'max':-3")) && limits_are(item, NONE, -3, NONE, NONE, NONE, NONE), "max alone is accepted");
	check(accepted(with_members("'min':-0.5,'max':-0.25")) && limits_are(item, -0.5, -0.25, NONE, NONE, NONE, NONE), "min just below max is accepted");
	check(accepted(with_members("'warn_lo':9,'warn_hi':1,'crit_lo':8,'crit_hi':2")) && limits_are(item, NONE, NONE, 9, 1, 8, 2),
	      "limits are taken as they are, in any order of size");
	check(accepted(with_members("'warn_lo':0,'warn_hi':0,'crit_lo':0,'crit_hi':0,'min':0,'max':1")) && limits_are(item, 0, 1, 0, 0, 0, 0),
	      "a limit of 0 is a limit");
	check(accepted(with_members("'scale':-2.5e-1")) && item->scale == -0.25, "a scale with sign, fraction and exponent");
	check(accepted(with_members("'scale':-1")) && item->scale == -1, "a scale of -1 is not the scale 1");
	check(accepted(with_members("'scale':1")) && item->scale == 1 && accepted(with_members("'scale':2")) && item->scale == 2 &&
	      accepted(with_members("'scale':0.5")) && item->scale == 0.5, "the scales 1, 2 and a half");
	check(accepted(with_members("'scale':1e300,'min':-1e308,'max':1e308")) && item->scale == 1e300 && limits_are(item, -1e308, 1e308, NONE, NONE, NONE, NONE),
	      "large numbers that are finite are accepted");
	check(accepted(with_members("'dec':0")) && item->decimals == 0, "dec 0");
	check(accepted(with_members("'dec':1")) && item->decimals == 1, "dec 1");
	check(accepted(with_members("'dec':2")) && item->decimals == 2, "dec 2");
	check(accepted(with_members("'dec':3")) && item->decimals == 3, "dec 3");
	check(accepted(with_members("'unit':''")) && item->has_unit && TEXT_IS(item->unit, ""), "an empty unit is the unit of the item");
	check(accepted(with_page("'hidden':true,'items':[" ITEM "]")) && box.layout.pages[0].hidden, "hidden true");
	check(accepted(with_page("'hidden':false,'items':[" ITEM "]")) && !box.layout.pages[0].hidden, "hidden false");
	check(accepted(with_members("'map':{'':'leer','*':'','a':'a','a':'b'}")) && item->map_count == 4 && map_is(item, 0, "", "leer") &&
	      map_is(item, 1, "*", "") && map_is(item, 2, "a", "a") && map_is(item, 3, "a", "b"),
	      "a map may have an empty value, an empty text and the same value twice");
}

/*
 * layout_parse: what is refused
 */

typedef struct
{
	const char *json;
	const char *path;
	const char *problem;
	const char *what;
} refusal_t;

static void refusals(const refusal_t *cases, size_t count)
{
	size_t i;

	for(i = 0; i < count; i++)
	{
		char what[160];

		snprintf(what, sizeof(what), "refused: %s", cases[i].what);
		check(refused(doc("%s", cases[i].json), cases[i].path, cases[i].problem), what);
	}
}

#define REFUSALS(cases) refusals(cases, sizeof(cases) / sizeof(cases[0]))
#define MEMBERS(members) HEAD "'pages':[{'items':[{'key':'K'," members "}]}]}"

static void test_refused_text(void)
{
	static const refusal_t cases[] = {
		{"", "", "not valid JSON", "an empty text"},
		{" ", "", "not valid JSON", "a space"},
		{"{", "", "not valid JSON", "an open brace"},
		{HEAD "'pages':[{'items':[{'key':'K'}]}]", "", "not valid JSON", "a layout without its closing brace"},
		{HEAD PAGES "} x", "", "not valid JSON", "text behind the layout"},
		{HEAD PAGES ",}", "", "not valid JSON", "a comma too many"},
		{"{" FORMAT ",'v':01," PAGES "}", "", "not valid JSON", "a number with a leading zero"},
		{"{" FORMAT ",'v':1,'pages':[{'items':[{'key':'K','later':[[[[[1]]]]]}]}]}", "", "not valid JSON", "containers nested deeper than the JSON reader goes"},
		{"[]", "", "not a JSON object", "an empty array"},
		{"[" HEAD PAGES "}]", "", "not a JSON object", "a layout inside an array"},
		{"1", "", "not a JSON object", "a number"},
		{"'wican-display-layout'", "", "not a JSON object", "a text"},
		{"null", "", "not a JSON object", "null"},
		{"true", "", "not a JSON object", "true"},

		{"{}", "format", "not wican-display-layout", "an empty object"},
		{"{'v':1," PAGES "}", "format", "not wican-display-layout", "format is missing"},
		{"{'format':'wican-display-layou','v':1," PAGES "}", "format", "not wican-display-layout", "format one character short"},
		{"{'format':'wican-display-layout2','v':1," PAGES "}", "format", "not wican-display-layout", "format one character longer"},
		{"{'format':'Wican-display-layout','v':1," PAGES "}", "format", "not wican-display-layout", "format in other letters"},
		{"{'format':'','v':1," PAGES "}", "format", "not wican-display-layout", "an empty format"},
		{"{'format':1,'v':1," PAGES "}", "format", "not wican-display-layout", "format is a number"},
		{"{'format':null,'v':1," PAGES "}", "format", "not wican-display-layout", "format is null"},
		{"{'format':['wican-display-layout'],'v':1," PAGES "}", "format", "not wican-display-layout", "format is an array"},
		{"{'Format':'wican-display-layout','v':1," PAGES "}", "format", "not wican-display-layout", "format under another name"},

		{"{" FORMAT "," PAGES "}", "v", "not an integer", "v is missing"},
		{"{" FORMAT ",'v':'1'," PAGES "}", "v", "not an integer", "v is a text"},
		{"{" FORMAT ",'v':1.0," PAGES "}", "v", "not an integer", "v with a fraction"},
		{"{" FORMAT ",'v':1e0," PAGES "}", "v", "not an integer", "v with an exponent"},
		{"{" FORMAT ",'v':true," PAGES "}", "v", "not an integer", "v is true"},
		{"{" FORMAT ",'v':null," PAGES "}", "v", "not an integer", "v is null"},
		{"{" FORMAT ",'v':[1]," PAGES "}", "v", "not an integer", "v is an array"},
		{"{" FORMAT ",'v':9223372036854775808," PAGES "}", "v", "not an integer", "v does not fit into 64 bit"},
		{"{" FORMAT ",'v':2," PAGES "}", "v", "layout of a newer display", "v one above the version of this display"},
		{"{" FORMAT ",'v':9223372036854775807," PAGES "}", "v", "layout of a newer display", "the largest v"},
		{"{" FORMAT ",'v':256," PAGES "}", "v", "layout of a newer display", "v 256, which is 0 in eight bit"},
		{"{" FORMAT ",'v':257," PAGES "}", "v", "layout of a newer display", "v 257, which is 1 in eight bit"},
		{"{" FORMAT ",'v':65537," PAGES "}", "v", "layout of a newer display", "v 2^16 + 1"},
		{"{" FORMAT ",'v':4294967297," PAGES "}", "v", "layout of a newer display", "v 2^32 + 1"},
		{"{" FORMAT ",'v':0," PAGES "}", "v", "below 1", "v 0"},
		{"{" FORMAT ",'v':-1," PAGES "}", "v", "below 1", "a negative v"},
		{"{" FORMAT ",'v':-255," PAGES "}", "v", "below 1", "v -255, which is 1 in eight bit"},
		{"{" FORMAT ",'v':-65535," PAGES "}", "v", "below 1", "v 1 - 2^16"},
		{"{" FORMAT ",'v':-4294967295," PAGES "}", "v", "below 1", "v 1 - 2^32"},
		{"{" FORMAT ",'v':-9223372036854775808," PAGES "}", "v", "below 1", "the smallest v"},

		{HEAD "'name':5," PAGES "}", "name", "not a text", "name is a number"},
		{HEAD "'name':null," PAGES "}", "name", "not a text", "name is null"},
		{HEAD "'name':['a']," PAGES "}", "name", "not a text", "name is an array"},
		{HEAD "'profile_hint':true," PAGES "}", "profile_hint", "not a text", "profile_hint is true"},
		{HEAD "'profile_hint':{}," PAGES "}", "profile_hint", "not a text", "profile_hint is an object"},

		{"{" FORMAT ",'v':1}", "pages", "missing", "pages is missing"},
		{HEAD "'Pages':[" PAGE "]}", "pages", "missing", "pages under another name"},
		{HEAD "'pages':{}}", "pages", "not an array", "pages is an object"},
		{HEAD "'pages':'x'}", "pages", "not an array", "pages is a text"},
		{HEAD "'pages':5}", "pages", "not an array", "pages is a number"},
		{HEAD "'pages':null}", "pages", "not an array", "pages is null"},
		{HEAD "'pages':[]}", "pages", "empty", "no page"},

		{HEAD "'pages':[5]}", "pages[0]", "not an object", "a page is a number"},
		{HEAD "'pages':[[" ITEM "]]}", "pages[0]", "not an object", "a page is an array"},
		{HEAD "'pages':[null]}", "pages[0]", "not an object", "a page is null"},
		{HEAD "'pages':[" PAGE ",'x']}", "pages[1]", "not an object", "the second page is a text"},
		{HEAD "'pages':[{'title':5,'items':[" ITEM "]}]}", "pages[0].title", "not a text", "title is a number"},
		{HEAD "'pages':[{'title':null,'items':[" ITEM "]}]}", "pages[0].title", "not a text", "title is null"},
		{HEAD "'pages':[{'hidden':0,'items':[" ITEM "]}]}", "pages[0].hidden", "not a boolean", "hidden is 0"},
		{HEAD "'pages':[{'hidden':1,'items':[" ITEM "]}]}", "pages[0].hidden", "not a boolean", "hidden is 1"},
		{HEAD "'pages':[{'hidden':'true','items':[" ITEM "]}]}", "pages[0].hidden", "not a boolean", "hidden is a text"},
		{HEAD "'pages':[{'hidden':null,'items':[" ITEM "]}]}", "pages[0].hidden", "not a boolean", "hidden is null"},
		{HEAD "'pages':[" PAGE ",{'hidden':[],'items':[" ITEM "]}]}", "pages[1].hidden", "not a boolean", "hidden of the second page is an array"},

		{HEAD "'pages':[{}]}", "pages[0].items", "missing", "a page without items"},
		{HEAD "'pages':[{'title':'T','Items':[" ITEM "]}]}", "pages[0].items", "missing", "items under another name"},
		{HEAD "'pages':[{'items':{}}]}", "pages[0].items", "not an array", "items is an object"},
		{HEAD "'pages':[{'items':'x'}]}", "pages[0].items", "not an array", "items is a text"},
		{HEAD "'pages':[{'items':5}]}", "pages[0].items", "not an array", "items is a number"},
		{HEAD "'pages':[{'items':null}]}", "pages[0].items", "not an array", "items is null"},
		{HEAD "'pages':[{'items':[]}]}", "pages[0].items", "empty", "a page with no item"},
		{HEAD "'pages':[" PAGE "," PAGE ",{'items':[]}]}", "pages[2].items", "empty", "the third page has no item"},

		{HEAD "'pages':[{'items':[5]}]}", "pages[0].items[0]", "not an object", "an item is a number"},
		{HEAD "'pages':[{'items':['K']}]}", "pages[0].items[0]", "not an object", "an item is a text"},
		{HEAD "'pages':[{'items':[['key','K']]}]}", "pages[0].items[0]", "not an object", "an item is an array"},
		{HEAD "'pages':[{'items':[null]}]}", "pages[0].items[0]", "not an object", "an item is null"},
		{HEAD "'pages':[{'items':[" ITEM ",true]}]}", "pages[0].items[1]", "not an object", "the second item is true"},

		{HEAD "'pages':[{'items':[{}]}]}", "pages[0].items[0].key", "missing", "an item without key"},
		{HEAD "'pages':[{'items':[{'label':'L','Key':'K'}]}]}", "pages[0].items[0].key", "missing", "key under another name"},
		{HEAD "'pages':[{'items':[{'key':''}]}]}", "pages[0].items[0].key", "empty", "an empty key"},
		{HEAD "'pages':[{'items':[{'key':5}]}]}", "pages[0].items[0].key", "not a text", "key is a number"},
		{HEAD "'pages':[{'items':[{'key':null}]}]}", "pages[0].items[0].key", "not a text", "key is null"},
		{HEAD "'pages':[{'items':[{'key':['K']}]}]}", "pages[0].items[0].key", "not a text", "key is an array"},
		{HEAD "'pages':[" PAGE ",{'items':[" ITEM "," ITEM ",{'label':'L'}]}]}", "pages[1].items[2].key", "missing", "the third item of the second page has no key"},

		{MEMBERS("'label':5"), "pages[0].items[0].label", "not a text", "label is a number"},
		{MEMBERS("'label':null"), "pages[0].items[0].label", "not a text", "label is null"},
		{MEMBERS("'label':{'de':'L'}"), "pages[0].items[0].label", "not a text", "label is an object"},
		{MEMBERS("'unit':5"), "pages[0].items[0].unit", "not a text", "unit is a number"},
		{MEMBERS("'unit':null"), "pages[0].items[0].unit", "not a text", "unit is null"},
		{MEMBERS("'unit':false"), "pages[0].items[0].unit", "not a text", "unit is false"},

		{MEMBERS("'dec':-1"), "pages[0].items[0].dec", "not an integer 0 to 3", "dec -1"},
		{MEMBERS("'dec':4"), "pages[0].items[0].dec", "not an integer 0 to 3", "dec 4"},
		{MEMBERS("'dec':1.5"), "pages[0].items[0].dec", "not an integer 0 to 3", "dec with a fraction"},
		{MEMBERS("'dec':2.0"), "pages[0].items[0].dec", "not an integer 0 to 3", "dec 2.0"},
		{MEMBERS("'dec':1e0"), "pages[0].items[0].dec", "not an integer 0 to 3", "dec with an exponent"},
		{MEMBERS("'dec':'1'"), "pages[0].items[0].dec", "not an integer 0 to 3", "dec is a text"},
		{MEMBERS("'dec':null"), "pages[0].items[0].dec", "not an integer 0 to 3", "dec is null"},
		{MEMBERS("'dec':true"), "pages[0].items[0].dec", "not an integer 0 to 3", "dec is true"},
		{MEMBERS("'dec':256"), "pages[0].items[0].dec", "not an integer 0 to 3", "dec 256, which is 0 in eight bit"},
		{MEMBERS("'dec':4294967297"), "pages[0].items[0].dec", "not an integer 0 to 3", "dec 2^32 + 1"},
		{MEMBERS("'dec':99999999999999999999"), "pages[0].items[0].dec", "not an integer 0 to 3", "dec beyond 64 bit"},

		{MEMBERS("'widget':5"), "pages[0].items[0].widget", "not a text", "widget is a number"},
		{MEMBERS("'widget':null"), "pages[0].items[0].widget", "not a text", "widget is null"},
		{MEMBERS("'widget':true"), "pages[0].items[0].widget", "not a text", "widget is true"},
		{MEMBERS("'widget':['arc']"), "pages[0].items[0].widget", "not a text", "widget is an array"},

		{MEMBERS("'scale':0"), "pages[0].items[0].scale", "zero", "scale 0"},
		{MEMBERS("'scale':0.0"), "pages[0].items[0].scale", "zero", "scale 0.0"},
		{MEMBERS("'scale':-0"), "pages[0].items[0].scale", "zero", "scale -0"},
		{MEMBERS("'scale':0e5"), "pages[0].items[0].scale", "zero", "scale 0e5"},
		{MEMBERS("'scale':1e-999"), "pages[0].items[0].scale", "zero", "a scale too small for a double"},
		{MEMBERS("'scale':1e999"), "pages[0].items[0].scale", "not finite", "a scale too large for a double"},
		{MEMBERS("'scale':-1e999"), "pages[0].items[0].scale", "not finite", "a negative scale too large for a double"},
		{MEMBERS("'scale':'1'"), "pages[0].items[0].scale", "not a number", "scale is a text"},
		{MEMBERS("'scale':null"), "pages[0].items[0].scale", "not a number", "scale is null"},
		{MEMBERS("'scale':true"), "pages[0].items[0].scale", "not a number", "scale is true"},
		{MEMBERS("'scale':[1]"), "pages[0].items[0].scale", "not a number", "scale is an array"},

		{MEMBERS("'min':5,'max':5"), "pages[0].items[0].min", "min is not below max", "min equals max"},
		{MEMBERS("'min':6,'max':5"), "pages[0].items[0].min", "min is not below max", "min above max"},
		{MEMBERS("'max':-1,'min':0"), "pages[0].items[0].min", "min is not below max", "min above max, max named first"},
		{MEMBERS("'min':0,'max':-0.0"), "pages[0].items[0].min", "min is not below max", "0 and minus 0"},
		{MEMBERS("'widget':'arc','min':1,'max':1"), "pages[0].items[0].min", "min is not below max", "an arc with min equal to max"},

		{MEMBERS("'min':'0'"), "pages[0].items[0].min", "not a number", "min is a text"},
		{MEMBERS("'min':null"), "pages[0].items[0].min", "not a number", "min is null"},
		{MEMBERS("'min':1e999"), "pages[0].items[0].min", "not finite", "min is too large for a double"},
		{MEMBERS("'min':-1e999,'max':0"), "pages[0].items[0].min", "not finite", "min is too small for a double"},
		{MEMBERS("'max':'9'"), "pages[0].items[0].max", "not a number", "max is a text"},
		{MEMBERS("'max':true"), "pages[0].items[0].max", "not a number", "max is true"},
		{MEMBERS("'min':0,'max':1e999"), "pages[0].items[0].max", "not finite", "max is too large for a double"},
		{MEMBERS("'warn_lo':'1'"), "pages[0].items[0].warn_lo", "not a number", "warn_lo is a text"},
		{MEMBERS("'warn_lo':[]"), "pages[0].items[0].warn_lo", "not a number", "warn_lo is an array"},
		{MEMBERS("'warn_lo':-1e999"), "pages[0].items[0].warn_lo", "not finite", "warn_lo is not finite"},
		{MEMBERS("'warn_hi':'1'"), "pages[0].items[0].warn_hi", "not a number", "warn_hi is a text"},
		{MEMBERS("'warn_hi':null"), "pages[0].items[0].warn_hi", "not a number", "warn_hi is null"},
		{MEMBERS("'warn_hi':1e999"), "pages[0].items[0].warn_hi", "not finite", "warn_hi is not finite"},
		{MEMBERS("'crit_lo':'1'"), "pages[0].items[0].crit_lo", "not a number", "crit_lo is a text"},
		{MEMBERS("'crit_lo':{}"), "pages[0].items[0].crit_lo", "not a number", "crit_lo is an object"},
		{MEMBERS("'crit_lo':-1e999"), "pages[0].items[0].crit_lo", "not finite", "crit_lo is not finite"},
		{MEMBERS("'crit_hi':'1'"), "pages[0].items[0].crit_hi", "not a number", "crit_hi is a text"},
		{MEMBERS("'crit_hi':false"), "pages[0].items[0].crit_hi", "not a number", "crit_hi is false"},
		{MEMBERS("'crit_hi':1e999"), "pages[0].items[0].crit_hi", "not finite", "crit_hi is not finite"},

		{MEMBERS("'map':[]"), "pages[0].items[0].map", "not an object", "map is an array"},
		{MEMBERS("'map':['1','an']"), "pages[0].items[0].map", "not an object", "map is an array of texts"},
		{MEMBERS("'map':'x'"), "pages[0].items[0].map", "not an object", "map is a text"},
		{MEMBERS("'map':5"), "pages[0].items[0].map", "not an object", "map is a number"},
		{MEMBERS("'map':null"), "pages[0].items[0].map", "not an object", "map is null"},
		{MEMBERS("'widget':'state','map':true"), "pages[0].items[0].map", "not an object", "map of a state widget is true"},
		{MEMBERS("'map':{'1':5}"), "pages[0].items[0].map[0]", "not a text", "a map text is a number"},
		{MEMBERS("'map':{'1':null}"), "pages[0].items[0].map[0]", "not a text", "a map text is null"},
		{MEMBERS("'map':{'1':true}"), "pages[0].items[0].map[0]", "not a text", "a map text is true"},
		{MEMBERS("'map':{'1':{'de':'an'}}"), "pages[0].items[0].map[0]", "not a text", "a map text is an object"},
		{MEMBERS("'map':{'1':'an','0':['aus']}"), "pages[0].items[0].map[1]", "not a text", "the second map text is an array"},
		{MEMBERS("'map':{'1':'an','0':'aus','*':0}"), "pages[0].items[0].map[2]", "not a text", "the third map text is a number"},
	};

	REFUSALS(cases);

	check(strlen(times(45, "0")) == 45 && accepted(with_members(piece("'scale':1.%s", times(45, "0")))) && first_item()->scale == 1,
	      "a scale of 47 characters is a number");
	check(refused(with_members(piece("'scale':1.%s", times(46, "0"))), "pages[0].items[0].scale", "not a number"),
	      "refused: a scale of 48 characters, which the JSON reader does not take for a number");
	check(refused(with_members(piece("'min':%s", times(48, "1"))), "pages[0].items[0].min", "not a number"), "refused: a min of 48 digits");
}

// A missing member is looked up at index -1. What lies before the room of the reader must not stand in for it.
static void test_missing_members(void)
{
	const char *json;

	// Before the tokens lies a text token that says "wican-display-layout": the name of this layout
	json = doc("{'name':'wican-display-layout','v':1," PAGES "}");
	work[-1] = (json_token_t){JSON_STRING, 9, 20, 0, 1};
	check(strncmp(json + 9, "wican-display-layout", 20) == 0 && refused(json, "format", "not wican-display-layout"),
	      "missing: format is not taken from what lies before the tokens");

	// And now the number 1: that of the member x
	json = doc("{" FORMAT ",'x':1," PAGES "}");
	work[-1] = (json_token_t){JSON_NUMBER, 37, 1, 0, 1};
	check(strncmp(json + 33, "\"x\":1,", 6) == 0 && refused(json, "v", "not an integer"), "missing: v is not taken from what lies before the tokens");

	// An array with one element
	work[-1] = (json_token_t){JSON_ARRAY, 0, 2, 1, 2};
	check(refused(doc("{" FORMAT ",'v':1}"), "pages", "missing"), "missing: pages is not taken from what lies before the tokens");
	check(refused(with_pages("{'title':'T'}"), "pages[0].items", "missing"), "missing: items is not taken from what lies before the tokens");

	// An empty text
	work[-1] = (json_token_t){JSON_STRING, 0, 0, 0, 1};
	check(refused(with_item("'label':'L'"), "pages[0].items[0].key", "missing"), "missing: a key is not taken from what lies before the tokens");
	check(accepted(doc(HEAD PAGES "}")) && is_plain(first_item(), "K") && !box.layout.pages[0].hidden && TEXT_IS(box.layout.name, ""),
	      "missing: optional members are not taken from what lies before the tokens");

	work[-1] = (json_token_t){JSON_TRUE, 0, 4, 0, 1};
	check(accepted(doc(HEAD PAGES "}")) && !box.layout.pages[0].hidden, "missing: a page without hidden is not hidden, whatever lies before the tokens");

	work[-1] = front_guard;
}

static void test_control_characters(void)
{
	static const char *const escapes[] = {"\\b", "\\f", "\\n", "\\r", "\\t", "\\u0000", "\\u007f", "\\u007F", "\177"};
	static const struct
	{
		const char *format;
		const char *path;
	} fields[] = {
		{HEAD "'name':'%s'," PAGES "}", "name"},
		{HEAD "'profile_hint':'%s'," PAGES "}", "profile_hint"},
		{HEAD "'pages':[{'title':'%s','items':[" ITEM "]}]}", "pages[0].title"},
		{HEAD "'pages':[{'items':[{'key':'%s'}]}]}", "pages[0].items[0].key"},
		{MEMBERS("'label':'%s'"), "pages[0].items[0].label"},
		{MEMBERS("'unit':'%s'"), "pages[0].items[0].unit"},
		{MEMBERS("'map':{'%s':'t'}"), "pages[0].items[0].map[0]"},
		{MEMBERS("'map':{'r':'%s'}"), "pages[0].items[0].map[0]"},
		{MEMBERS("'map':{'0':'a','1':'b','%s':'t'}"), "pages[0].items[0].map[2]"},
		{MEMBERS("'map':{'0':'a','1':'%s'}"), "pages[0].items[0].map[1]"},
	};
	char what[160];
	size_t i, f;
	int c;
	int wrong = 0;

	for(c = 1; c < 0x20; c++)
	{
		if(!refused(doc(MEMBERS("'label':'a\\u%04xb'"), (unsigned)c), "pages[0].items[0].label", "control character")) wrong++;
	}
	check(wrong == 0, "control: each of \\u0001 to \\u001f in a label is refused");

	for(i = 0; i < sizeof(escapes) / sizeof(escapes[0]); i++)
	{
		snprintf(what, sizeof(what), "control: %s in a label is refused", i == 8 ? "the byte 0x7F" : escapes[i]);
		check(refused(doc(MEMBERS("'label':'ab%scd'"), escapes[i]), "pages[0].items[0].label", "control character"), what);
	}
	check(refused(doc(MEMBERS("'label':'\\tabc'")), "pages[0].items[0].label", "control character"), "control: a control character at the beginning of a text");
	check(refused(doc(MEMBERS("'label':'abc\\t'")), "pages[0].items[0].label", "control character"), "control: a control character at the end of a text");
	check(refused(doc(MEMBERS("'label':'\\n'")), "pages[0].items[0].label", "control character"), "control: a control character alone");

	for(f = 0; f < sizeof(fields) / sizeof(fields[0]); f++)
	{
		wrong = 0;
		// A line break, the zero character, and the delete character written out
		if(!refused(doc(fields[f].format, "a\\nb"), fields[f].path, "control character")) wrong++;
		if(!refused(doc(fields[f].format, "\\u0000"), fields[f].path, "control character")) wrong++;
		if(!refused(doc(fields[f].format, "a\177"), fields[f].path, "control character")) wrong++;
		snprintf(what, sizeof(what), "control: refused at %s (%d of 10 texts)", fields[f].path, (int)f + 1);
		check(wrong == 0, what);
	}

	check(accepted(doc(MEMBERS("'label':'\\u0020\\u007e \\u0080\\u00a0'"))) && TEXT_IS(first_item()->label, " ~ \302\200\302\240"),
	      "control: the characters next to the control characters are accepted: space, tilde, U+0080");
	check(accepted(doc(MEMBERS("'label':'a\\\\nb\\/\\\"'"))) && TEXT_IS(first_item()->label, "a\\nb/\""),
	      "control: an escaped backslash, slash and quote are no control characters");
}

static void test_surrogates(void)
{
	static const refusal_t cases[] = {
		{MEMBERS("'label':'\\ud83d'"), "pages[0].items[0].label", "half a surrogate pair", "a first half alone"},
		{MEMBERS("'label':'ab\\ud83d'"), "pages[0].items[0].label", "half a surrogate pair", "a first half at the end of a text"},
		{MEMBERS("'label':'\\ud83dabcdefgh'"), "pages[0].items[0].label", "half a surrogate pair", "a first half followed by text"},
		{MEMBERS("'label':'\\ud83d\\u0041'"), "pages[0].items[0].label", "half a surrogate pair", "a first half followed by another character"},
		{MEMBERS("'label':'\\ud83d\\ud83d'"), "pages[0].items[0].label", "half a surrogate pair", "two first halves"},
		{MEMBERS("'label':'\\ude00'"), "pages[0].items[0].label", "half a surrogate pair", "a second half alone"},
		{MEMBERS("'label':'\\ude00\\ud83d'"), "pages[0].items[0].label", "half a surrogate pair", "the halves in the wrong order"},
		{MEMBERS("'label':'\\ude00abcdefghijkl'"), "pages[0].items[0].label", "half a surrogate pair", "a second half followed by text"},
		{HEAD "'name':'\\udbff'," PAGES "}", "name", "half a surrogate pair", "half a pair in the name"},
		{HEAD "'pages':[{'items':[{'key':'K\\udc00'}]}]}", "pages[0].items[0].key", "half a surrogate pair", "half a pair in a key"},
		{MEMBERS("'map':{'\\ud800':'t'}"), "pages[0].items[0].map[0]", "half a surrogate pair", "half a pair in a map value"},
		{MEMBERS("'map':{'r':'\\udfff'}"), "pages[0].items[0].map[0]", "half a surrogate pair", "half a pair in a map text"},
	};

	REFUSALS(cases);
	check(accepted(doc(MEMBERS("'label':'a\\ud83d\\ude00b\\uD83D\\uDE00'"))) && TEXT_IS(first_item()->label, "a\360\237\230\200b\360\237\230\200"),
	      "surrogates: a whole pair is one character of four bytes");
	check(accepted(doc(MEMBERS("'label':'\\ud7ff\\ue000\\uffff'"))) && TEXT_IS(first_item()->label, "\355\237\277\356\200\200\357\277\277"),
	      "surrogates: the characters next to the surrogates are accepted");
}

static bool long_name_refused(void)
{
	return refused(doc(HEAD "'name':'%s'," PAGES "}", letters(33)), "name", "too long");
}

static bool long_hint_refused(void)
{
	return refused(doc(HEAD "'profile_hint':'%s'," PAGES "}", letters(33)), "profile_hint", "too long");
}

static bool long_key_refused(void)
{
	return refused(with_item(piece("'key':'%s'", letters(33))), "pages[0].items[0].key", "too long");
}

static bool long_escaped_key_refused(void)
{
	return refused(with_item(piece("'key':'%s\\ud83d\\ude00'", letters(32))), "pages[0].items[0].key", "too long");
}

// Every text at its limit and one byte above. The fields the reader has spare room for come first.
static void test_text_limits(void)
{
	const layout_t *layout = &box.layout;
	const layout_item_t *item = first_item();
	const char *at, *above;

	at = letters(24);
	above = letters(25);
	check(accepted(with_page(piece("'title':'%s','items':[" ITEM "]", at))) && TEXT_IS(layout->pages[0].title, at), "limits: a title of 24 bytes is accepted");
	check(refused(with_page(piece("'title':'%s','items':[" ITEM "]", above)), "pages[0].title", "too long"), "limits: a title of 25 bytes is refused");
	check(accepted(with_members(piece("'label':'%s'", at))) && TEXT_IS(item->label, at) && !item->has_unit && TEXT_IS(item->unit, ""),
	      "limits: a label of 24 bytes is accepted and does not reach into the unit");
	check(refused(with_members(piece("'label':'%s'", above)), "pages[0].items[0].label", "too long"), "limits: a label of 25 bytes is refused");
	check(refused(with_members(piece("'label':'%s'", letters(150))), "pages[0].items[0].label", "too long"), "limits: a label of 150 bytes is refused");

	at = letters(8);
	above = letters(9);
	check(accepted(with_members(piece("'unit':'%s'", at))) && TEXT_IS(item->unit, at) && item->has_unit, "limits: a unit of 8 bytes is accepted");
	check(refused(with_members(piece("'unit':'%s'", above)), "pages[0].items[0].unit", "too long"), "limits: a unit of 9 bytes is refused");

	at = letters(11);
	above = letters(12);
	check(accepted(with_members(piece("'map':{'%s':'t'}", at))) && map_is(item, 0, at, "t"), "limits: a map value of 11 bytes is accepted");
	check(refused(with_members(piece("'map':{'%s':'t'}", above)), "pages[0].items[0].map[0]", "too long"), "limits: a map value of 12 bytes is refused");
	check(refused(with_members(piece("'map':{'a':'t','%s':'t'}", above)), "pages[0].items[0].map[1]", "too long"), "limits: the second map value of 12 bytes is refused");

	at = letters(23);
	above = letters(24);
	check(accepted(with_members(piece("'map':{'r':'%s'}", at))) && map_is(item, 0, "r", at), "limits: a map text of 23 bytes is accepted");
	check(refused(with_members(piece("'map':{'r':'%s'}", above)), "pages[0].items[0].map[0]", "too long"), "limits: a map text of 24 bytes is refused");
	check(accepted(with_members(piece("'map':{'%s':'%s','*':'x'}", letters(11), at))) && map_is(item, 0, letters(11), at) && map_is(item, 1, "*", "x"),
	      "limits: a map value and its text at their limits do not reach into each other or the next entry");

	// These fill the room the reader has for a text: one byte more would be written behind it
	at = letters(32);
	check(accepted(doc(HEAD "'name':'%s','profile_hint':'p'," PAGES "}", at)) && TEXT_IS(layout->name, at) && TEXT_IS(layout->profile_hint, "p"),
	      "limits: a name of 32 bytes is accepted");
	check(in_child(long_name_refused), "limits: a name of 33 bytes is refused");
	check(accepted(doc(HEAD "'profile_hint':'%s'," PAGES "}", at)) && TEXT_IS(layout->profile_hint, at), "limits: a profile hint of 32 bytes is accepted");
	check(in_child(long_hint_refused), "limits: a profile hint of 33 bytes is refused");
	check(accepted(with_item(piece("'key':'%s','label':'l'", at))) && TEXT_IS(item->key, at) && TEXT_IS(item->label, "l"), "limits: a key of 32 bytes is accepted");
	check(in_child(long_key_refused), "limits: a key of 33 bytes is refused");
	check(in_child(long_escaped_key_refused), "limits: a key of 32 bytes and a character of four bytes is refused");
	check(accepted(with_item("'key':'k'")) && TEXT_IS(item->key, "k"), "limits: a key of one byte is accepted");

	check(LAYOUT_TITLE_SIZE == 25 && LAYOUT_UNIT_SIZE == 9 && LAYOUT_NAME_SIZE == 33 && LAYOUT_MAP_RAW_SIZE == 12 && LAYOUT_MAP_TEXT_SIZE == 24 &&
	      sizeof(item->key) == 33, "limits: the sizes of the header");
}

static void test_utf8(void)
{
	const layout_item_t *item = first_item();
	const char *at;

	at = times(12, "\303\244");
	check(accepted(with_members(piece("'label':'%s'", at))) && TEXT_IS(item->label, at), "utf-8: a label of 12 characters of two bytes is 24 bytes and accepted");
	check(refused(with_members(piece("'label':'%sa'", at)), "pages[0].items[0].label", "too long"), "utf-8: with one byte more it is refused");
	check(refused(with_members(piece("'label':'%s\303\244'", letters(23))), "pages[0].items[0].label", "too long"),
	      "utf-8: a character that would be cut in half at the limit makes the label too long");
	check(refused(with_members(piece("'label':'a%s'", at)), "pages[0].items[0].label", "too long"), "utf-8: bytes count, not characters");

	check(accepted(with_members(piece("'label':'%s'", times(12, "\\u00e4")))) && TEXT_IS(item->label, at),
	      "utf-8: 12 escaped characters of two bytes are 24 bytes: the text counts, not how it is written");
	check(refused(with_members(piece("'label':'%s'", times(13, "\\u00e4"))), "pages[0].items[0].label", "too long"), "utf-8: 13 of them are too long");
	check(accepted(with_members(piece("'label':'%s'", times(24, "\\u0041")))) && TEXT_IS(item->label, "AAAAAAAAAAAAAAAAAAAAAAAA"),
	      "utf-8: 24 escaped letters are a label of 24 bytes");
	check(refused(with_members(piece("'label':'%s'", times(25, "\\u0041"))), "pages[0].items[0].label", "too long"), "utf-8: 25 escaped letters are too long");
	check(accepted(with_members(piece("'label':'%s'", times(8, "\\u20ac")))) && TEXT_IS(item->label, times(8, "\342\202\254")),
	      "utf-8: 8 escaped characters of three bytes are 24 bytes");
	check(refused(with_members(piece("'label':'%s'", times(9, "\\u20ac"))), "pages[0].items[0].label", "too long"), "utf-8: 9 of them are too long");
	check(accepted(with_members(piece("'label':'%s'", times(6, "\\ud83d\\ude00")))) && TEXT_IS(item->label, times(6, "\360\237\230\200")),
	      "utf-8: 6 surrogate pairs are 24 bytes");
	check(refused(with_members(piece("'label':'%s'", times(7, "\\ud83d\\ude00"))), "pages[0].items[0].label", "too long"), "utf-8: 7 of them are too long");
	check(accepted(with_members(piece("'label':'%s\\ud83d\\ude00'", letters(20)))) && item->label[20] == '\360' && item->label[23] == '\200' && item->label[24] == '\0',
	      "utf-8: a surrogate pair that ends exactly at the limit is accepted");
	check(refused(with_members(piece("'label':'%s\\ud83d\\ude00'", letters(21))), "pages[0].items[0].label", "too long"),
	      "utf-8: a surrogate pair that reaches one byte over the limit is refused");

	check(accepted(with_members("'unit':'\302\260C/100m'")) && TEXT_IS(item->unit, "\302\260C/100m"), "utf-8: a unit of 8 bytes with a character of two bytes");
	check(refused(with_members("'unit':'\302\260C/100km'"), "pages[0].items[0].unit", "too long"), "utf-8: with 9 bytes it is refused");
	check(accepted(with_members("'unit':'\\u00b0C/100m'")) && TEXT_IS(item->unit, "\302\260C/100m"), "utf-8: the same unit with an escape");
	check(accepted(with_item("'key':'\303\226l \342\202\254 \360\237\230\200'")) && TEXT_IS(item->key, "\303\226l \342\202\254 \360\237\230\200"),
	      "utf-8: a key with characters of two, three and four bytes");
	check(accepted(with_members("'map':{'\303\244':'\303\266\303\274'}")) && map_is(item, 0, "\303\244", "\303\266\303\274"), "utf-8: a map in UTF-8");

	// Bytes that are no UTF-8: the reader passes them on and stays inside its room
	check(accepted(with_members("'label':'a\377\200b\303'")) && TEXT_IS(item->label, "a\377\200b\303"), "utf-8: bytes that are no UTF-8 are passed on unchanged");
	check(refused(with_members(piece("'label':'%s'", times(25, "\377"))), "pages[0].items[0].label", "too long"), "utf-8: 25 such bytes are too long");
	check(accepted(with_members(piece("'label':'%s'", times(24, "\200")))) && TEXT_IS(item->label, times(24, "\200")), "utf-8: 24 such bytes are accepted");
}

static void test_counts(void)
{
	const layout_t *layout = &box.layout;
	size_t length;
	int i, wrong, count;

	length = 0;
	for(i = 0; i < 12; i++) length += (size_t)snprintf(part + length, sizeof(part) - length, "%s{'title':'T%d','items':[{'key':'P%d'}]}", i ? "," : "", i, i);
	check(accepted(with_pages(part)) && layout->page_count == 12, "counts: 12 pages are accepted");
	wrong = 0;
	for(i = 0; i < 12; i++)
	{
		char title[8], key[8];

		snprintf(title, sizeof(title), "T%d", i);
		snprintf(key, sizeof(key), "P%d", i);
		if(!TEXT_IS(layout->pages[i].title, title) || layout->pages[i].item_count != 1 || !is_plain(&layout->pages[i].items[0], key)) wrong++;
	}
	check(wrong == 0, "counts: each of the 12 pages is read into its place");
	snprintf(part + length, sizeof(part) - length, ",{'items':[{'key':'P12'}]}");
	check(refused(with_pages(part), "pages", "too many pages"), "counts: 13 pages are refused");
	check(LAYOUT_PAGES_MAX == 12, "counts: the limit of the header is 12 pages");

	length = 0;
	for(i = 0; i < 6; i++) length += (size_t)snprintf(part + length, sizeof(part) - length, "%s{'key':'I%d','dec':%d}", i ? "," : "", i, i % 4);
	check(accepted(with_items(part)) && layout->page_count == 1 && layout->pages[0].item_count == 6, "counts: 6 items on a page are accepted");
	wrong = 0;
	for(i = 0; i < 6; i++)
	{
		char key[8];

		snprintf(key, sizeof(key), "I%d", i);
		if(!item_is(&layout->pages[0].items[i], key, "", NULL, i % 4, LAYOUT_WIDGET_NUMBER, 1)) wrong++;
	}
	check(wrong == 0, "counts: each of the 6 items is read into its place");
	snprintf(part + length, sizeof(part) - length, ",{'key':'I6'}");
	check(refused(with_items(part), "pages[0].items", "too many items"), "counts: 7 items on a page are refused");
	check(LAYOUT_ITEMS_MAX == 6, "counts: the limit of the header is 6 items");

	length = (size_t)snprintf(part, sizeof(part), "'map':{");
	for(i = 0; i < 8; i++) length += (size_t)snprintf(part + length, sizeof(part) - length, "%s'%d':'text %d'", i ? "," : "", i, i);
	snprintf(part + length, sizeof(part) - length, "}");
	check(accepted(with_members(part)) && first_item()->map_count == 8, "counts: a map of 8 entries is accepted");
	wrong = 0;
	for(i = 0; i < 8; i++)
	{
		char raw[8], shown[16];

		snprintf(raw, sizeof(raw), "%d", i);
		snprintf(shown, sizeof(shown), "text %d", i);
		if(!map_is(first_item(), i, raw, shown)) wrong++;
	}
	check(wrong == 0, "counts: each of the 8 entries is read into its place");
	snprintf(part + length, sizeof(part) - length, ",'8':'text 8'}");
	check(refused(with_members(part), "pages[0].items[0].map", "too many entries"), "counts: a map of 9 entries is refused");
	check(LAYOUT_MAP_MAX == 8, "counts: the limit of the header is 8 entries");

	// Every count there is: pages 1 to 12, items 1 to 6, map entries 0 to 8, each thing in its place
	wrong = 0;
	for(count = 1; count <= 12; count++)
	{
		length = 0;
		for(i = 0; i < count; i++) length += (size_t)snprintf(part + length, sizeof(part) - length, "%s{'title':'T%d','items':[{'key':'P%d'}]}", i ? "," : "", i, i);
		if(!accepted(with_pages(part)) || layout->page_count != count || !bytes_are(&layout->pages[count], (size_t)(12 - count) * sizeof(layout_page_t), 0)) wrong++;
		for(i = 0; i < count; i++)
		{
			char title[8], key[8];

			snprintf(title, sizeof(title), "T%d", i);
			snprintf(key, sizeof(key), "P%d", i);
			if(!TEXT_IS(layout->pages[i].title, title) || layout->pages[i].item_count != 1 || !is_plain(&layout->pages[i].items[0], key)) wrong++;
		}
	}
	check(wrong == 0, "counts: layouts of 1 to 12 pages are read with that many pages, each in its place, the rest zero");
	wrong = 0;
	for(count = 1; count <= 6; count++)
	{
		length = 0;
		for(i = 0; i < count; i++) length += (size_t)snprintf(part + length, sizeof(part) - length, "%s{'key':'I%d','dec':%d}", i ? "," : "", i, i % 4);
		if(!accepted(with_items(part)) || layout->pages[0].item_count != count || !bytes_are(&layout->pages[0].items[count], (size_t)(6 - count) * sizeof(layout_item_t), 0)) wrong++;
		for(i = 0; i < count; i++)
		{
			char key[8];

			snprintf(key, sizeof(key), "I%d", i);
			if(!item_is(&layout->pages[0].items[i], key, "", NULL, i % 4, LAYOUT_WIDGET_NUMBER, 1)) wrong++;
		}
	}
	check(wrong == 0, "counts: pages of 1 to 6 items are read with that many items, each in its place, the rest zero");
	wrong = 0;
	for(count = 0; count <= 8; count++)
	{
		length = (size_t)snprintf(part, sizeof(part), "'map':{");
		for(i = 0; i < count; i++) length += (size_t)snprintf(part + length, sizeof(part) - length, "%s'%d':'text %d'", i ? "," : "", i, i);
		snprintf(part + length, sizeof(part) - length, "}");
		if(!accepted(with_members(part)) || first_item()->map_count != count || !bytes_are(&first_item()->map[count], (size_t)(8 - count) * sizeof(layout_map_t), 0)) wrong++;
		for(i = 0; i < count; i++)
		{
			char raw[8], shown[16];

			snprintf(raw, sizeof(raw), "%d", i);
			snprintf(shown, sizeof(shown), "text %d", i);
			if(!map_is(first_item(), i, raw, shown)) wrong++;
		}
	}
	check(wrong == 0, "counts: maps of 0 to 8 entries are read with that many entries, each in its place, the rest zero");
}

// The largest layout: 12 pages of 6 items, each with every member and a full map
static void test_largest(void)
{
	const layout_t *layout = &box.layout;
	int p, i, m, wrong;
	int tokens;

	text_length = 0;
	emit("{\"format\":\"wican-display-layout\",\"v\":1,\"name\":\"n\",\"profile_hint\":\"h\",\"pages\":[");
	for(p = 0; p < 12; p++)
	{
		emit("%s{\"title\":\"t\",\"hidden\":false,\"items\":[", p ? "," : "");
		for(i = 0; i < 6; i++)
		{
			emit("%s{\"key\":\"K%d_%d\",\"label\":\"\",\"unit\":\"\",\"dec\":0,\"widget\":\"state\",\"scale\":1,\"min\":0,\"max\":1,"
			     "\"warn_lo\":0,\"warn_hi\":0,\"crit_lo\":0,\"crit_hi\":%d,\"map\":{", i ? "," : "", p, i, p * 6 + i);
			for(m = 0; m < 8; m++) emit("%s\"%d\":\"%d\"", m ? "," : "", m, m);
			emit("}}");
		}
		emit("]}");
	}
	emit("]}");

	// 11 tokens of the layout, 7 of each page, 43 of each item: 11 + 12 * (7 + 6 * 43)
	tokens = json_parse(text, text_length, work, LAYOUT_TOKENS);
	check(tokens == 3191 && text_length <= LAYOUT_TEXT_MAX, "largest: the largest layout has 3191 tokens and fits into the text limit");
	check(LAYOUT_TOKENS >= 3191 && LAYOUT_TOKENS == 4096, "largest: LAYOUT_TOKENS is enough for it");
	check(accepted(text) && layout->page_count == 12, "largest: it is accepted");
	wrong = 0;
	for(p = 0; p < 12; p++)
	{
		if(layout->pages[p].item_count != 6) wrong++;
		for(i = 0; i < 6; i++)
		{
			const layout_item_t *item = &layout->pages[p].items[i];
			char key[16];

			snprintf(key, sizeof(key), "K%d_%d", p, i);
			if(!item_is(item, key, "", "", 0, LAYOUT_WIDGET_STATE, 1) || !limits_are(item, 0, 1, 0, 0, 0, p * 6 + i) || item->map_count != 8) wrong++;
			for(m = 0; m < 8; m++)
			{
				char digit[2] = {(char)('0' + m), '\0'};

				if(!map_is(item, m, digit, digit)) wrong++;
			}
		}
	}
	check(wrong == 0, "largest: every one of the 72 items is read into its place");

	// The place of a problem in the last map entry of the last item of the last page fits into the report
	text[text_length - 8] = '\t';
	check(text[text_length - 9] == '"' && refused_length(text, text_length, "", "not valid JSON"), "largest: a tab in its last map text is no JSON");
	memcpy(&text[text_length - 8], "\\n\"}}]}]}", 9);
	check(refused_length(text, text_length + 1, "pages[11].items[5].map[7]", "control character"),
	      "largest: a problem in the last entry of the last map is named with its place");
}

static void test_text_length(void)
{
	size_t length;

	with_item("'key':'K','label':'L'");
	length = strlen(text);
	// Spaces behind the layout are still JSON. The zero in the last byte keeps a reader that does not look
	// at the length inside the buffer, where a check finds it.
	memset(text + length, ' ', sizeof(text) - length);
	text[sizeof(text) - 1] = '\0';
	start();
	check(parse(text, LAYOUT_TEXT_MAX) && report_is("", "", 0, "", "") && TEXT_IS(first_item()->label, "L"), "length: a text of 16384 bytes is accepted");
	check(refused_length(text, LAYOUT_TEXT_MAX + 1, "", "text too long"), "length: a text of 16385 bytes is refused");
	check(refused_length(text, sizeof(text), "", "text too long"), "length: a text of 65536 bytes is refused");
	check(LAYOUT_TEXT_MAX == 16384, "length: the limit of the header is 16384 bytes");
	// A text that is too long is not read at all: it is too long, whatever else is wrong with it
	text[LAYOUT_TEXT_MAX] = '}';
	check(refused_length(text, LAYOUT_TEXT_MAX + 1, "", "text too long"), "length: a text of 16385 bytes that is no JSON is refused as too long, not as broken");
	check(refused_length(text + 1, LAYOUT_TEXT_MAX + 1, "", "text too long"), "length: likewise one that does not begin like JSON");
	text[LAYOUT_TEXT_MAX] = ' ';

	// The text has no zero at its end: only `length` bytes are read
	memset(text + length, '}', 16);
	start();
	check(parse(text, length) && TEXT_IS(first_item()->label, "L"), "length: what stands behind the given length is not read");
	check(refused_length(text, length + 1, "", "not valid JSON"), "length: one byte more of it is read if the length says so");
	check(refused_length(text, length - 1, "", "not valid JSON"), "length: one byte less cuts the layout");
}

static void test_tokens(void)
{
	const char *json = doc(HEAD PAGES "}");
	json_token_t behind;

	// The layout, three keys with their values, a page, "items" and its array, an item, "key" and its text
	memset(work, 0x5A, LAYOUT_TOKENS * sizeof(json_token_t));
	start();
	memset(&reported, GUARD, sizeof(reported));
	check(layout_parse(json, strlen(json), &box.layout, &reported.report, work, 13) && is_plain(first_item(), "K") && report_is("", "", 0, "", ""),
	      "tokens: a layout of 13 tokens is read with room for 13");
	behind = work[13];
	check(behind.start == 0x5A5A5A5A && behind.length == 0x5A5A5A5A, "tokens: nothing is written behind the room");

	start();
	memcpy(&snapshot, &box, sizeof(box));
	memset(&reported, GUARD, sizeof(reported));
	check(!layout_parse(json, strlen(json), &box.layout, &reported.report, work, 12) && report_is("", "not valid JSON", 0, "", "") &&
	      memcmp(&snapshot, &box, sizeof(box)) == 0, "tokens: with room for 12 it is treated like broken JSON");
	memset(&reported, GUARD, sizeof(reported));
	check(!layout_parse(json, strlen(json), &box.layout, &reported.report, work, 0) && report_is("", "not valid JSON", 0, "", "") &&
	      memcmp(&snapshot, &box, sizeof(box)) == 0, "tokens: with no room at all as well");
}

// What the reader left in the token room from the text before is not part of the next text
static void test_stale_tokens(void)
{
	static char json[256];

	snprintf(json, sizeof(json), "%s", doc(HEAD "'pages':[" PAGE "," PAGE "]}"));
	check(accepted(json) && box.layout.page_count == 2, "stale: a layout of two pages is read");
	check(refused_length(json, 0, "", "not valid JSON"), "stale: length 0 in the same memory is no layout, the tokens of the text before are not used");
	check(accepted(json), "stale: the layout is read again");
	json[0] = ' ';
	check(refused_length(json, 1, "", "not valid JSON"), "stale: a space in the same memory is no layout");
	json[0] = '{';
	check(accepted(json), "stale: the layout is read once more");
	check(refused_length(json, strlen(json) - 1, "", "not valid JSON"), "stale: without its last byte it is no layout");
}

// layout_parse() without a report: true if the text was refused and the layout stayed as it was
static bool refused_unreported(const char *json, size_t length)
{
	start();
	memcpy(&snapshot, &box, sizeof(box));
	return !layout_parse(json, length, &box.layout, NULL, work, LAYOUT_TOKENS) && memcmp(&snapshot, &box, sizeof(box)) == 0;
}

static bool read_without_report(void)
{
	const char *json = with_members("'label':'L'");

	start();
	memset(&reported, GUARD, sizeof(reported));
	return layout_parse(json, strlen(json), &box.layout, NULL, work, LAYOUT_TOKENS) && item_is(first_item(), "K", "L", NULL, 0, LAYOUT_WIDGET_NUMBER, 1) &&
	       guards_intact();
}

static bool warned_without_report(void)
{
	const char *json = with_members("'label':'L','widget':'gauge','min':1");

	start();
	memset(&reported, GUARD, sizeof(reported));
	return layout_parse(json, strlen(json), &box.layout, NULL, work, LAYOUT_TOKENS) && item_is(first_item(), "K", "L", NULL, 0, LAYOUT_WIDGET_NUMBER, 1) &&
	       limits_are(first_item(), 1, NONE, NONE, NONE, NONE, NONE) && guards_intact();
}

static bool warned_with_report(void)
{
	return warned(with_members("'label':'L','widget':'gauge','min':1"), 1, "pages[0].items[0].widget", "unknown widget, shown as number") &&
	       item_is(first_item(), "K", "L", NULL, 0, LAYOUT_WIDGET_NUMBER, 1) && limits_are(first_item(), 1, NONE, NONE, NONE, NONE, NONE);
}

static bool refused_without_report(void)
{
	const char *json = with_items("{'key':'A','widget':'gauge'},{'key':'B','label':5}");

	return refused_unreported(json, strlen(json));
}

static bool no_object_without_report(void)
{
	return refused_unreported("[]", 2);
}

static bool no_json_without_report(void)
{
	return refused_unreported("{", 1);
}

static bool too_long_without_report(void)
{
	memset(text, ' ', sizeof(text));
	return refused_unreported(text, LAYOUT_TEXT_MAX + 1);
}

// The first test of all: the reader itself runs a second time without a report when it fills the layout,
// so that a reader that needs one does not get far in any other test.
static void test_without_report(void)
{
	check(in_child(read_without_report), "report: without a report a layout is read all the same");
	check(in_child(warned_without_report), "report: without a report a layout with a warning is read all the same");
	check(in_child(warned_with_report), "report: with a report a layout with a warning is read and filled");
	check(in_child(refused_without_report), "report: without a report a layout is refused all the same and the layout before stays");
	check(in_child(no_object_without_report), "report: without a report a text that is no object is refused all the same");
	check(in_child(no_json_without_report), "report: without a report a text that is no JSON is refused all the same");
	check(in_child(too_long_without_report), "report: without a report a text that is too long is refused all the same");
}

static void test_report(void)
{
	const char *json;
	layout_report_t *report = &reported.report;

	// The first problem in the order of the text is named
	check(refused(with_pages(PAGE ",{'items':[{'key':'K','label':5}," ITEM ",{'label':'L'}]},{'items':[]}"), "pages[1].items[0].label", "not a text"),
	      "report: of several problems the first in the order of pages and items is named");
	check(refused(with_pages("{'items':[" ITEM ",{'key':'K','dec':7}]},{'title':5,'items':[" ITEM "]}"), "pages[0].items[1].dec", "not an integer 0 to 3"),
	      "report: a problem of an item is named before one of the page behind it");
	check(refused(with_pages("{'title':'T','items':[" ITEM "]},{'title':'T','items':[" ITEM "]},{'title':'T','items':[" ITEM "],'hidden':2}"),
	      "pages[2].hidden", "not a boolean"), "report: after the items of two pages the place is the third page again, not an item");
	check(refused(with_pages("{'items':[{'key':'K','map':{'a':'b'}},{'key':'K','unit':5}]}"), "pages[0].items[1].unit", "not a text"),
	      "report: after a map the place is the next item again, not an entry of the map");

	// A report is written anew by every call
	json = with_members("'dec':9");
	start();
	parse(json, strlen(json));
	check(report_is("pages[0].items[0].dec", "not an integer 0 to 3", 0, "", ""), "report: a refused layout names its problem");
	json = doc(HEAD PAGES "}");
	check(layout_parse(json, strlen(json), &box.layout, report, work, LAYOUT_TOKENS) && report_is("", "", 0, "", ""),
	      "report: the next accepted layout leaves nothing of the problem before");
	json = with_members("'widget':'gauge'");
	check(layout_parse(json, strlen(json), &box.layout, report, work, LAYOUT_TOKENS) &&
	      report_is("", "", 1, "pages[0].items[0].widget", "unknown widget, shown as number"), "report: a layout with a warning");
	json = doc(HEAD PAGES "}");
	check(layout_parse(json, strlen(json), &box.layout, report, work, LAYOUT_TOKENS) && report_is("", "", 0, "", ""),
	      "report: the next layout without one leaves nothing of the warning before");
	json = with_members("'widget':'gauge'");
	layout_parse(json, strlen(json), &box.layout, report, work, LAYOUT_TOKENS);
	check(!layout_parse("[]", 2, &box.layout, report, work, LAYOUT_TOKENS) && report_is("", "not a JSON object", 0, "", ""),
	      "report: a refused text leaves nothing of the warning before");


	// A layout that was read stays when the next one is refused, and goes completely when the next one is accepted
	json = with_pages(PAGE "," PAGE "," PAGE);
	check(accepted(json) && box.layout.page_count == 3, "report: a layout of three pages is read");
	memcpy(&snapshot, &box, sizeof(box));
	json = with_pages(PAGE ",{'items':[{'key':'K'},{'key':'OTHER','label':'" "abcdefghijklmnopqrstuvwxy" "'}]}");
	check(!parse(json, strlen(json)) && memcmp(&snapshot, &box, sizeof(box)) == 0 && report_is("pages[1].items[1].label", "too long", 0, "", ""),
	      "report: a layout with a problem on its second page leaves the layout before untouched");
	json = doc(HEAD "'pages':[{'items':[{'key':'ONE'}]}]}");
	check(parse(json, strlen(json)) && box.layout.page_count == 1 && is_plain(first_item(), "ONE") &&
	      bytes_are(&box.layout.pages[1], 11 * sizeof(layout_page_t), 0), "report: a shorter layout leaves nothing of the one before");
}

// Of several problems the one that is reported is the first in the order of the header, not of the text
static void test_problem_order(void)
{
	static const struct
	{
		const char *name;
		const char *wrong;
		const char *right;
		const char *problem;
	} members[] = {
		{"key", "5", "'K'", "not a text"},
		{"label", "5", "'L'", "not a text"},
		{"unit", "5", "'u'", "not a text"},
		{"dec", "9", "1", "not an integer 0 to 3"},
		{"widget", "5", "'arc'", "not a text"},
		{"scale", "'x'", "2", "not a number"},
		{"min", "'x'", "3", "not a number"},
		{"max", "'x'", "4", "not a number"},
		{"warn_lo", "'x'", "5", "not a number"},
		{"warn_hi", "'x'", "6", "not a number"},
		{"crit_lo", "'x'", "7", "not a number"},
		{"crit_hi", "'x'", "8", "not a number"},
		{"map", "5", "{'1':'an'}", "not an object"},
	};
	static const struct
	{
		const char *json;
		const char *path;
		const char *problem;
	} layouts[] = {
		{"{'pages':5,'profile_hint':5,'name':5,'v':'x','format':5}", "format", "not wican-display-layout"},
		{"{'pages':5,'profile_hint':5,'name':5,'v':'x'," FORMAT "}", "v", "not an integer"},
		{"{'pages':5,'profile_hint':5,'name':5,'v':2," FORMAT "}", "v", "layout of a newer display"},
		{"{'pages':5,'profile_hint':5,'name':5,'v':0," FORMAT "}", "v", "below 1"},
		{"{'pages':5,'profile_hint':5,'name':5,'v':1," FORMAT "}", "name", "not a text"},
		{"{'pages':5,'profile_hint':5,'name':'N','v':1," FORMAT "}", "profile_hint", "not a text"},
		{"{'pages':5,'profile_hint':'H','name':'N','v':1," FORMAT "}", "pages", "not an array"},
		{"{'profile_hint':5,'name':5,'v':1," FORMAT "}", "name", "not a text"},
		{"{'profile_hint':5,'name':'N','v':1," FORMAT "}", "profile_hint", "not a text"},
		{"{'profile_hint':'H','name':'N','v':1," FORMAT "}", "pages", "missing"},
		{"{'pages':[{'hidden':5,'title':5}],'v':1," FORMAT "}", "pages[0].title", "not a text"},
		{"{'pages':[{'hidden':5,'title':'T'}],'v':1," FORMAT "}", "pages[0].hidden", "not a boolean"},
		{"{'pages':[{'hidden':false,'title':'T'}],'v':1," FORMAT "}", "pages[0].items", "missing"},
		{"{'pages':[{'items':[],'hidden':5}],'v':1," FORMAT "}", "pages[0].hidden", "not a boolean"},
		{"{'pages':[{'items':[{'map':5,'crit_hi':'x','scale':0,'widget':5,'dec':9,'unit':5,'label':5}]}],'v':1," FORMAT "}", "pages[0].items[0].key", "missing"},
		{"{'pages':[{'items':5,'hidden':5,'title':5}],'v':1," FORMAT "}", "pages[0].title", "not a text"},
		{"{'pages':[{'items':5,'hidden':5,'title':'T'}],'v':1," FORMAT "}", "pages[0].hidden", "not a boolean"},
		{"{'pages':[{'items':5,'hidden':true,'title':'T'}],'v':1," FORMAT "}", "pages[0].items", "not an array"},
		{"{'pages':[{'items':[{'key':5}],'hidden':5,'title':'T'}],'v':1," FORMAT "}", "pages[0].hidden", "not a boolean"},
		{"{'pages':[{'items':[{'key':5}],'hidden':true,'title':'T'}],'v':1," FORMAT "}", "pages[0].items[0].key", "not a text"},
	};
	const int count = (int)(sizeof(members) / sizeof(members[0]));
	char item_text[512], path[48];
	int first, m, wrong = 0;
	size_t i, length;

	for(i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++)
	{
		if(!refused(doc("%s", layouts[i].json), layouts[i].path, layouts[i].problem)) wrong++;
	}
	check(wrong == 0, "order: of several problems the first is reported in the order format, v, name, profile_hint, pages, and of a page title, hidden, items");

	// An item whose members stand in the text from the map to the key. Those before `first` are right.
	wrong = 0;
	for(first = 0; first < count; first++)
	{
		length = 0;
		for(m = count - 1; m >= 0; m--)
		{
			length += (size_t)snprintf(item_text + length, sizeof(item_text) - length, "'%s':%s%s", members[m].name, m < first ? members[m].right : members[m].wrong, m > 0 ? "," : "");
		}
		snprintf(path, sizeof(path), "pages[0].items[0].%s", members[first].name);
		if(!refused(with_item(item_text), path, members[first].problem)) wrong++;
	}
	check(count == 13 && wrong == 0,
	      "order: of several problems of an item the first is reported in the order key, label, unit, dec, widget, scale, min, max, warn_lo, warn_hi, crit_lo, crit_hi, map");
	check(refused(with_item("'map':5,'crit_hi':'x','crit_lo':'x','warn_hi':'x','warn_lo':'x','max':2,'min':3,'key':'K'"), "pages[0].items[0].min", "min is not below max"),
	      "order: a range in the wrong order is reported before a limit that is no number");
	check(refused(with_item("'max':2,'min':3,'scale':0,'key':'K'"), "pages[0].items[0].scale", "zero"), "order: a scale of zero is reported before the range");
	check(refused(with_item("'scale':0,'widget':5,'dec':9,'label':5,'key':''"), "pages[0].items[0].key", "empty"), "order: an empty key is reported before everything else of the item");
	check(refused(with_item("'map':{'a':5,'b\\n':'x'},'key':'K'"), "pages[0].items[0].map[0]", "not a text"), "order: of the entries of a map the first with a problem is reported");
	check(refused(with_item("'map':{'a\\n':5},'key':'K'"), "pages[0].items[0].map[0]", "control character"), "order: the value of a map entry is looked at before its text");
	check(refused(with_item("'map':{'1':'a','2':'b','3':'c','4':'d','5':'e','6':'f','7':'g','8':'h','9\\n':5},'key':'K'"), "pages[0].items[0].map", "too many entries"),
	      "order: a map with too many entries is refused before its entries are looked at");
	check(refused(with_items(ITEM "," ITEM "," ITEM "," ITEM "," ITEM "," ITEM ",5"), "pages[0].items", "too many items"),
	      "order: a page with too many items is refused before its items are looked at");
}

static void test_warnings(void)
{
	static char file[4096];
	static const char *const unknown[] = {"gauge", "", "Arc", "ARC", "ar", "arcs", "arc ", " bar", "numbers", "stat", "states", "number\\u0000", "*"};
	const layout_item_t *item = first_item();
	const layout_page_t *page = &box.layout.pages[1];
	char what[160];
	size_t i;

	for(i = 0; i < sizeof(unknown) / sizeof(unknown[0]); i++)
	{
		snprintf(what, sizeof(what), "warning: the unknown widget \"%s\" becomes number", unknown[i]);
		check(warned(with_members(piece("'widget':'%s','min':0,'max':1,'map':{'1':'an'}", unknown[i])), 1, "pages[0].items[0].widget", "unknown widget, shown as number") &&
		      item->widget == LAYOUT_WIDGET_NUMBER && limits_are(item, 0, 1, NONE, NONE, NONE, NONE) && item->map_count == 1, what);
	}

	check(warned(with_members("'widget':'arc'"), 1, "pages[0].items[0].widget", "arc or bar without min and max, shown as number") &&
	      item->widget == LAYOUT_WIDGET_NUMBER, "warning: an arc without min and max becomes number");
	check(warned(with_members("'widget':'arc','min':0"), 1, "pages[0].items[0].widget", "arc or bar without min and max, shown as number") &&
	      item->widget == LAYOUT_WIDGET_NUMBER && limits_are(item, 0, NONE, NONE, NONE, NONE, NONE), "warning: an arc without max becomes number, min is kept");
	check(warned(with_members("'widget':'arc','max':9"), 1, "pages[0].items[0].widget", "arc or bar without min and max, shown as number") &&
	      item->widget == LAYOUT_WIDGET_NUMBER && limits_are(item, NONE, 9, NONE, NONE, NONE, NONE), "warning: an arc without min becomes number, max is kept");
	check(warned(with_members("'widget':'bar'"), 1, "pages[0].items[0].widget", "arc or bar without min and max, shown as number") &&
	      item->widget == LAYOUT_WIDGET_NUMBER, "warning: a bar without min and max becomes number");
	check(warned(with_members("'widget':'bar','min':0"), 1, "pages[0].items[0].widget", "arc or bar without min and max, shown as number") &&
	      item->widget == LAYOUT_WIDGET_NUMBER, "warning: a bar without max becomes number");
	check(warned(with_members("'widget':'bar','max':9"), 1, "pages[0].items[0].widget", "arc or bar without min and max, shown as number") &&
	      item->widget == LAYOUT_WIDGET_NUMBER, "warning: a bar without min becomes number");
	check(warned(with_members("'widget':'arc','warn_lo':0,'warn_hi':9,'crit_lo':0,'crit_hi':9"), 1, "pages[0].items[0].widget",
	      "arc or bar without min and max, shown as number") && item->widget == LAYOUT_WIDGET_NUMBER, "warning: limits are no range for an arc");

	check(warned(with_members("'widget':'state'"), 1, "pages[0].items[0].widget", "state without map, shown as number") &&
	      item->widget == LAYOUT_WIDGET_NUMBER && item->map_count == 0, "warning: a state widget without map becomes number");
	check(warned(with_members("'widget':'state','map':{}"), 1, "pages[0].items[0].widget", "state without map, shown as number") &&
	      item->widget == LAYOUT_WIDGET_NUMBER && item->map_count == 0, "warning: a state widget with an empty map becomes number");
	check(warned(with_members("'widget':'state','min':0,'max':1"), 1, "pages[0].items[0].widget", "state without map, shown as number") &&
	      item->widget == LAYOUT_WIDGET_NUMBER, "warning: a range is no map for a state widget");
	check(accepted(with_members("'widget':'arc','min':0,'max':1")) && accepted(with_members("'widget':'bar','min':0,'max':1")) &&
	      accepted(with_members("'widget':'state','map':{'*':'x'}")) && accepted(with_members("'widget':'number'")) && accepted(with_members("'map':{}")) &&
	      accepted(with_members("'dec':1")), "warning: none for the widgets that have what they need, for number and for no widget");

	// Several: counted, the first one told
	check(warned(with_items("{'key':'A','widget':'state'},{'key':'B','widget':'gauge'},{'key':'C','widget':'bar'}"), 3, "pages[0].items[0].widget",
	      "state without map, shown as number"), "warning: three warnings are counted, the first is told");
	check(warned(with_pages(PAGE ",{'items':[" ITEM ",{'key':'B','widget':'gauge'},{'key':'C','widget':'arc','min':1}]}," PAGE), 2, "pages[1].items[1].widget",
	      "unknown widget, shown as number"), "warning: the first warning is the first in the order of the text");
	check(warned(with_pages(PAGE "," PAGE ",{'items':[" ITEM "," ITEM ",{'key':'C','widget':'arc','min':1}]}"), 1, "pages[2].items[2].widget",
	      "arc or bar without min and max, shown as number"), "warning: its place is the widget of the item, on any page");

	check(read_fixture("fixtures/layout_warnings.json", file, sizeof(file)), "warning: the fixture is there");
	check(warned(file, 6, "pages[0].items[1].widget", "unknown widget, shown as number"), "warning: the fixture has six, the first is the unknown widget");
	check(box.layout.pages[0].items[0].widget == LAYOUT_WIDGET_NUMBER && box.layout.pages[0].items[1].widget == LAYOUT_WIDGET_NUMBER &&
	      page->items[0].widget == LAYOUT_WIDGET_NUMBER && page->items[1].widget == LAYOUT_WIDGET_NUMBER && page->items[2].widget == LAYOUT_WIDGET_NUMBER &&
	      page->items[3].widget == LAYOUT_WIDGET_NUMBER && page->items[4].widget == LAYOUT_WIDGET_NUMBER && page->item_count == 5,
	      "warning: every widget of the fixture became number");
	check(limits_are(&box.layout.pages[0].items[1], 0, 10, NONE, NONE, NONE, NONE) && limits_are(&page->items[0], 0, NONE, NONE, NONE, NONE, NONE) &&
	      limits_are(&page->items[1], NONE, 100, NONE, NONE, NONE, NONE), "warning: what the items of the fixture have of a range is kept");

	// A layout that is refused has no warnings: nothing was accepted
	check(refused(with_items("{'key':'A','widget':'gauge'},{'key':'B','dec':5}"), "pages[0].items[1].dec", "not an integer 0 to 3"),
	      "warning: a refused layout reports no warning, also if one came before the problem");
	check(refused(with_items("{'key':'A','widget':'gauge','min':2,'max':1}"), "pages[0].items[0].min", "min is not below max"),
	      "warning: an unknown widget does not excuse a range that is wrong");
}

/*
 * layout_parse against a second description of the format: layouts made at random, written as text and
 * kept as the layout_t they stand for
 */

typedef struct
{
	const char *json;       // as it stands between the quotes
	const char *value;      // what it means
} sample_t;

typedef struct
{
	int number;
	int kind;               // 0: whole, 1: thousandths, 2: times 100, 3: hundredths
} spec_t;

static const sample_t sample_keys[] = {
	{"K", "K"}, {"ENGINE_RPM", "ENGINE_RPM"}, {"@BATT_V", "@BATT_V"}, {"abcdefghijklmnopqrstuvwxyzabcdef", "abcdefghijklmnopqrstuvwxyzabcdef"},
	{"\\u00d6lstand", "\303\226lstand"}, {"a b", "a b"}, {"*", "*"},
};
static const sample_t sample_labels[] = {
	{"", ""}, {"Drehzahl", "Drehzahl"}, {"K\303\274hlwasser", "K\303\274hlwasser"}, {"K\\u00fchlwasser", "K\303\274hlwasser"},
	{"abcdefghijklmnopqrstuvwx", "abcdefghijklmnopqrstuvwx"}, {"\\ud83d\\ude00", "\360\237\230\200"}, {"a\\\"b\\\\c\\/d", "a\"b\\c/d"},
	{"\303\244\303\244\303\244\303\244\303\244\303\244\303\244\303\244\303\244\303\244\303\244\303\244", "\303\244\303\244\303\244\303\244\303\244\303\244\303\244\303\244\303\244\303\244\303\244\303\244"},
};
static const sample_t sample_units[] = {
	{"", ""}, {"\302\260C", "\302\260C"}, {"1/min", "1/min"}, {"%", "%"}, {"12345678", "12345678"}, {"\\u00b5V", "\302\265V"},
};
static const sample_t sample_names[] = {
	{"", ""}, {"W906 OM651 Standard", "W906 OM651 Standard"}, {"abcdefghijklmnopqrstuvwxyzabcdef", "abcdefghijklmnopqrstuvwxyzabcdef"},
	{"Gr\\u00f6\\u00dfen", "Gr\303\266\303\237en"},
};
static const sample_t sample_raws[] = {
	{"0", "0"}, {"1", "1"}, {"on", "on"}, {"off", "off"}, {"*", "*"}, {"2,5", "2,5"}, {"12345678901", "12345678901"}, {"", ""},
};
static const sample_t sample_texts[] = {
	{"inaktiv", "inaktiv"}, {"aktiv", "aktiv"}, {"l\\u00e4uft", "l\303\244uft"}, {"", ""}, {"abcdefghijklmnopqrstuvw", "abcdefghijklmnopqrstuvw"},
};

#define SAMPLE(samples) (&samples[random_below((int)(sizeof(samples) / sizeof(samples[0])))])

static int model_warnings;
static bool model_rich;         // every item has every member and a full map
static char model_warning_path[48];
static char model_warning[64];

// The text layout_to_json() has to write for the layout of the model, put together from what the model
// chose and not from the layout_t: the members of an item lie in slots in the order of the header, and a
// number is written by its digits, as a whole number of thousandths.
typedef struct
{
	char *text;
	size_t size;
	size_t length;
} builder_t;

static char canon[65536];
static char canon_pages_text[65536];
static char canon_items_text[8192];
static char canon_name_text[96];
static char canon_hint_text[96];
static char slot_texts[13][700];
static builder_t canon_pages, canon_items, canon_name, canon_hint, slots[13];

static void build(builder_t *builder, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void build_start(builder_t *builder, char *room, size_t size)
{
	builder->text = room;
	builder->size = size;
	builder->length = 0;
	room[0] = '\0';
}

static void build(builder_t *builder, const char *format, ...)
{
	va_list arguments;

	if(builder->length >= builder->size) return;
	va_start(arguments, format);
	builder->length += (size_t)vsnprintf(builder->text + builder->length, builder->size - builder->length, format, arguments);
	va_end(arguments);
}

static void build_byte(builder_t *builder, char byte)
{
	if(builder->length + 1 < builder->size)
	{
		builder->text[builder->length] = byte;
		builder->text[builder->length + 1] = '\0';
	}
	builder->length++;
}

// A text between quotes. The texts of the model have quotes and backslashes and no control characters.
static void build_text(builder_t *builder, const char *value)
{
	build_byte(builder, '"');
	for(; *value != '\0'; value++)
	{
		if(*value == '"' || *value == '\\') build_byte(builder, '\\');
		build_byte(builder, *value);
	}
	build_byte(builder, '"');
}

static spec_t random_spec(void)
{
	spec_t spec;

	spec.number = random_below(200001) - 100000;
	spec.kind = random_below(4);
	return spec;
}

// The double nearest to the number as it is written
static double spec_value(spec_t spec)
{
	switch(spec.kind)
	{
		case 0:  return spec.number;
		case 1:  return spec.number / 1000.0;
		case 2:  return spec.number * 100.0;
		default: return spec.number / 100.0;
	}
}

// The number in thousandths, which is a whole number for every kind
static long long spec_thousandths(spec_t spec)
{
	switch(spec.kind)
	{
		case 0:  return spec.number * 1000LL;
		case 1:  return spec.number;
		case 2:  return spec.number * 100000LL;
		default: return spec.number * 10LL;
	}
}

// As layout_to_json() has to write it: the whole part, then the decimals up to the last one that is not zero
static void build_spec(builder_t *builder, spec_t spec)
{
	long long thousandths = spec_thousandths(spec);
	long long magnitude = thousandths < 0 ? -thousandths : thousandths;
	int decimals = (int)(magnitude % 1000);

	build(builder, "%s%lld", thousandths < 0 ? "-" : "", magnitude / 1000);
	if(decimals == 0) return;
	if(decimals % 100 == 0) build(builder, ".%d", decimals / 100);
	else if(decimals % 10 == 0) build(builder, ".%02d", decimals / 10);
	else build(builder, ".%03d", decimals);
}

static void emit_spec(spec_t spec)
{
	int magnitude = abs(spec.number);

	switch(spec.kind)
	{
		case 0:  emit("%d", spec.number); break;
		case 1:  emit("%s%d.%03d", spec.number < 0 ? "-" : "", magnitude / 1000, magnitude % 1000); break;
		case 2:  emit("%d%s", spec.number, random_below(2) ? "e2" : "E+2"); break;
		default: emit("%de-2", spec.number); break;
	}
}

static void emit_space(void)
{
	static const char *const spaces[] = {"", "", "", "", " ", "\n", "\t ", "\r\n  "};

	emit("%s", spaces[random_below(8)]);
}

// Now and then a member the format does not know, with names and values that look like known ones
static void emit_unknown(void)
{
	static const char *const unknown[] = {
		"\"x\":1,", "\"later\":{\"key\":\"Z\",\"hidden\":5,\"items\":[1,[2],{\"dec\":9}]},", "\"Key\":\"Z\",", "\"keys\":null,", "\"\":true,",
		"\"min \":\"a\",", "\"Widget\":\"arc\",", "\"title2\":[],",
	};

	if(random_below(5) == 0) emit("%s", unknown[random_below(8)]);
}

static void emit_member(bool *first, const char *name)
{
	if(!*first) emit(",");
	*first = false;
	emit_space();
	emit_unknown();
	emit("\"%s\"", name);
	emit_space();
	emit(":");
	emit_space();
}

static void model_warn(int page, int item, const char *warning)
{
	if(model_warnings++ > 0) return;
	snprintf(model_warning_path, sizeof(model_warning_path), "pages[%d].items[%d].widget", page, item);
	snprintf(model_warning, sizeof(model_warning), "%s", warning);
}

static void emit_limit(bool *first, const char *name, layout_limit_t *limit, spec_t spec, builder_t *slot)
{
	emit_member(first, name);
	emit_spec(spec);
	limit->set = true;
	limit->value = spec_value(spec);
	build(slot, ",\"%s\":", name);
	build_spec(slot, spec);
}

// One item, written to the text and put into the layout that is expected
static void model_item(int p, int i)
{
	static const char *const widgets[] = {"number", "arc", "bar", "state", "gauge", ""};
	layout_item_t *item = &expected.pages[p].items[i];
	const sample_t *sample;
	spec_t low = random_spec();
	spec_t high = random_spec();
	int order[13];
	int widget = -1;
	bool first = true;
	int m, n;

	// The members in an order of their own: each one is swapped with one before it
	for(m = 0; m < 13; m++)
	{
		n = random_below(m + 1);
		if(n != m) order[m] = order[n];
		order[n] = m;
	}
	// min has to be below max if both are there
	if(spec_value(low) > spec_value(high))
	{
		spec_t swap = low;

		low = high;
		high = swap;
	}

	// The slots: key, label, unit, scale, dec, widget, min, max, warn_lo, warn_hi, crit_lo, crit_hi, map
	for(m = 0; m < 13; m++) build_start(&slots[m], slot_texts[m], sizeof(slot_texts[m]));

	item->scale = 1;
	emit("{");
	for(m = 0; m < 13; m++)
	{
		// The key is always there, of the others about every second one
		if(order[m] != 0 && !model_rich && random_below(2) == 0) continue;

		switch(order[m])
		{
			case 0:
				sample = SAMPLE(sample_keys);
				emit_member(&first, "key");
				emit("\"%s\"", sample->json);
				strcpy(item->key, sample->value);
				build(&slots[0], "{\"key\":");
				build_text(&slots[0], sample->value);
				break;
			case 1:
				sample = SAMPLE(sample_labels);
				emit_member(&first, "label");
				emit("\"%s\"", sample->json);
				strcpy(item->label, sample->value);
				if(sample->value[0] != '\0')
				{
					build(&slots[1], ",\"label\":");
					build_text(&slots[1], sample->value);
				}
				break;
			case 2:
				sample = SAMPLE(sample_units);
				emit_member(&first, "unit");
				emit("\"%s\"", sample->json);
				strcpy(item->unit, sample->value);
				item->has_unit = true;
				build(&slots[2], ",\"unit\":");
				build_text(&slots[2], sample->value);
				break;
			case 3:
				item->decimals = (uint8_t)random_below(4);
				emit_member(&first, "dec");
				emit("%d", item->decimals);
				if(item->decimals != 0) build(&slots[4], ",\"dec\":%d", item->decimals);
				break;
			case 4:
				widget = random_below(6);
				emit_member(&first, "widget");
				emit("\"%s\"", widgets[widget]);
				break;
			case 5:
			{
				spec_t scale = random_spec();

				if(scale.number == 0) scale.number = 1;
				emit_member(&first, "scale");
				emit_spec(scale);
				item->scale = spec_value(scale);
				// A scale of 1 is not written, however it was spelt
				if(spec_thousandths(scale) != 1000)
				{
					build(&slots[3], ",\"scale\":");
					build_spec(&slots[3], scale);
				}
				break;
			}
			case 6:
				// A range needs two different numbers
				if(spec_value(low) < spec_value(high) || !expected.pages[p].items[i].max.set) emit_limit(&first, "min", &item->min, low, &slots[6]);
				break;
			case 7:
				if(spec_value(low) < spec_value(high) || !expected.pages[p].items[i].min.set) emit_limit(&first, "max", &item->max, high, &slots[7]);
				break;
			case 8:  emit_limit(&first, "warn_lo", &item->warn_lo, random_spec(), &slots[8]); break;
			case 9:  emit_limit(&first, "warn_hi", &item->warn_hi, random_spec(), &slots[9]); break;
			case 10: emit_limit(&first, "crit_lo", &item->crit_lo, random_spec(), &slots[10]); break;
			case 11: emit_limit(&first, "crit_hi", &item->crit_hi, random_spec(), &slots[11]); break;
			default:
				// None, a few, or all eight
				item->map_count = (uint8_t)(model_rich || random_below(4) == 0 ? 8 : random_below(4));
				emit_member(&first, "map");
				emit("{");
				for(n = 0; n < item->map_count; n++)
				{
					if(n > 0) emit(",");
					emit_space();
					sample = SAMPLE(sample_raws);
					emit("\"%s\"", sample->json);
					strcpy(item->map[n].raw, sample->value);
					build(&slots[12], "%s", n > 0 ? "," : ",\"map\":{");
					build_text(&slots[12], sample->value);
					emit_space();
					emit(":");
					sample = SAMPLE(sample_texts);
					emit("\"%s\"", sample->json);
					strcpy(item->map[n].text, sample->value);
					build(&slots[12], ":");
					build_text(&slots[12], sample->value);
				}
				if(item->map_count > 0) build(&slots[12], "}");
				emit_space();
				emit("}");
				break;
		}
	}
	emit_space();
	emit("}");

	// What the header says about the widgets
	item->widget = LAYOUT_WIDGET_NUMBER;
	if(widget == 4 || widget == 5) model_warn(p, i, "unknown widget, shown as number");
	if(widget == 1 || widget == 2)
	{
		if(item->min.set && item->max.set) item->widget = widget == 1 ? LAYOUT_WIDGET_ARC : LAYOUT_WIDGET_BAR;
		else model_warn(p, i, "arc or bar without min and max, shown as number");
	}
	if(widget == 3)
	{
		if(item->map_count > 0) item->widget = LAYOUT_WIDGET_STATE;
		else model_warn(p, i, "state without map, shown as number");
	}

	// Only a widget that stayed what it was is written
	if(item->widget != LAYOUT_WIDGET_NUMBER) build(&slots[5], ",\"widget\":\"%s\"", widgets[widget]);
	if(i > 0) build(&canon_items, ",");
	for(m = 0; m < 13; m++) build(&canon_items, "%s", slot_texts[m]);
	build(&canon_items, "}");
}

// A whole layout in `text` (text_length bytes) and in `expected`
static void model_layout(void)
{
	const sample_t *sample;
	int order[5];
	bool first = true;
	int m, n, p, i;

	memset(&expected, 0, sizeof(expected));
	model_warnings = 0;
	model_rich = random_below(6) == 0;
	model_warning_path[0] = '\0';
	model_warning[0] = '\0';
	text_length = 0;
	build_start(&canon_pages, canon_pages_text, sizeof(canon_pages_text));
	build_start(&canon_name, canon_name_text, sizeof(canon_name_text));
	build_start(&canon_hint, canon_hint_text, sizeof(canon_hint_text));

	for(m = 0; m < 5; m++)
	{
		n = random_below(m + 1);
		if(n != m) order[m] = order[n];
		order[n] = m;
	}

	emit_space();
	emit("{");
	for(m = 0; m < 5; m++)
	{
		switch(order[m])
		{
			case 0:
				emit_member(&first, "format");
				emit("\"wican-display-layout\"");
				break;
			case 1:
				emit_member(&first, "v");
				emit("1");
				break;
			case 2:
				if(random_below(2)) break;
				sample = SAMPLE(sample_names);
				emit_member(&first, "name");
				emit("\"%s\"", sample->json);
				strcpy(expected.name, sample->value);
				if(sample->value[0] != '\0')
				{
					build(&canon_name, ",\"name\":");
					build_text(&canon_name, sample->value);
				}
				break;
			case 3:
				if(random_below(2)) break;
				sample = SAMPLE(sample_names);
				emit_member(&first, "profile_hint");
				emit("\"%s\"", sample->json);
				strcpy(expected.profile_hint, sample->value);
				if(sample->value[0] != '\0')
				{
					build(&canon_hint, ",\"profile_hint\":");
					build_text(&canon_hint, sample->value);
				}
				break;
			default:
				expected.page_count = (uint8_t)(random_below(4) == 0 ? 12 : 1 + random_below(12));
				emit_member(&first, "pages");
				emit("[");
				for(p = 0; p < expected.page_count; p++)
				{
					layout_page_t *page = &expected.pages[p];
					bool first_of_page = true;
					int title = random_below(3);
					int hidden = random_below(3);

					if(p > 0) emit(",");
					emit_space();
					emit("{");
					// The title before or behind the items, or not at all
					if(title == 1)
					{
						sample = SAMPLE(sample_labels);
						emit_member(&first_of_page, "title");
						emit("\"%s\"", sample->json);
						strcpy(page->title, sample->value);
					}
					if(hidden > 0)
					{
						page->hidden = hidden == 2;
						emit_member(&first_of_page, "hidden");
						emit("%s", page->hidden ? "true" : "false");
					}
					page->item_count = (uint8_t)(random_below(5) == 0 ? 6 : 1 + random_below(6));
					emit_member(&first_of_page, "items");
					emit("[");
					build_start(&canon_items, canon_items_text, sizeof(canon_items_text));
					for(i = 0; i < page->item_count; i++)
					{
						if(i > 0) emit(",");
						emit_space();
						model_item(p, i);
					}
					emit_space();
					emit("]");
					if(title == 2)
					{
						sample = SAMPLE(sample_labels);
						emit_member(&first_of_page, "title");
						emit("\"%s\"", sample->json);
						strcpy(page->title, sample->value);
					}
					emit_space();
					emit("}");

					// The title first, wherever it stood, and only one that says something
					build(&canon_pages, "%s", p > 0 ? ",{" : "{");
					if(page->title[0] != '\0')
					{
						build(&canon_pages, "\"title\":");
						build_text(&canon_pages, page->title);
						build(&canon_pages, ",");
					}
					build(&canon_pages, "%s\"items\":[%s]}", page->hidden ? "\"hidden\":true," : "", canon_items_text);
				}
				emit_space();
				emit("]");
				break;
		}
	}
	emit_space();
	emit("}");
	emit_space();

	// gcc refuses a snprintf() that might cut unless the caller looks at what it returns
	if(snprintf(canon, sizeof(canon), "{\"format\":\"wican-display-layout\",\"v\":1%s%s,\"pages\":[%s]}", canon_name_text, canon_hint_text, canon_pages_text) >= (int)sizeof(canon))
	{
		printf("FAIL the generated layout text does not fit its room in the test\n");
		exit(1);
	}
}

static bool same_limit(const layout_limit_t *a, const layout_limit_t *b)
{
	return a->set == b->set && a->value == b->value;
}

// Field by field: the bytes between the fields belong to nobody. Texts with all their bytes, so that
// nothing may stand behind a text either, and maps with all their entries.
static bool same_layout(const layout_t *a, const layout_t *b)
{
	int p, i;

	if(memcmp(a->name, b->name, sizeof(a->name)) != 0 || memcmp(a->profile_hint, b->profile_hint, sizeof(a->profile_hint)) != 0) return false;
	if(a->page_count != b->page_count) return false;
	for(p = 0; p < LAYOUT_PAGES_MAX; p++)
	{
		const layout_page_t *pa = &a->pages[p];
		const layout_page_t *pb = &b->pages[p];

		if(memcmp(pa->title, pb->title, sizeof(pa->title)) != 0 || pa->hidden != pb->hidden || pa->item_count != pb->item_count) return false;
		for(i = 0; i < LAYOUT_ITEMS_MAX; i++)
		{
			const layout_item_t *ia = &pa->items[i];
			const layout_item_t *ib = &pb->items[i];

			if(memcmp(ia->key, ib->key, sizeof(ia->key)) != 0 || memcmp(ia->label, ib->label, sizeof(ia->label)) != 0 ||
			   memcmp(ia->unit, ib->unit, sizeof(ia->unit)) != 0 || ia->has_unit != ib->has_unit || ia->decimals != ib->decimals ||
			   ia->widget != ib->widget || ia->scale != ib->scale || ia->map_count != ib->map_count ||
			   memcmp(ia->map, ib->map, sizeof(ia->map)) != 0) return false;
			if(!same_limit(&ia->min, &ib->min) || !same_limit(&ia->max, &ib->max) || !same_limit(&ia->warn_lo, &ib->warn_lo) ||
			   !same_limit(&ia->warn_hi, &ib->warn_hi) || !same_limit(&ia->crit_lo, &ib->crit_lo) || !same_limit(&ia->crit_hi, &ib->crit_hi)) return false;
		}
	}
	return true;
}

/*
 * layout_to_json
 */

#define WRITTEN_ROOM    200000      // more than the longest text there is
#define WRITTEN_GUARD   0x7E
#define W_HEAD          "{'format':'wican-display-layout','v':1,"

// The last byte stays zero: a text that was left without its end is then a failed check, not a run off the room
static unsigned char written_room[16 + WRITTEN_ROOM + 64 + 1];
static char *const written = (char *)written_room + 16;
static layout_t kept;

// The text of a layout in a buffer of `size` bytes. The 16 bytes before the buffer and everything from
// out[size] up to out[reach + 64] must stay as they were; -2 if they did not.
static int write_watched(const layout_t *layout, size_t size, size_t reach)
{
	int result;

	if(reach < size) reach = size;
	memset(written_room, WRITTEN_GUARD, 16 + reach + 64);
	result = layout_to_json(layout, written, size);
	if(!bytes_are(written_room, 16, WRITTEN_GUARD) || !bytes_are(written + size, reach - size + 64, WRITTEN_GUARD))
	{
		printf("  a byte outside of the %lu bytes was written\n", (unsigned long)size);
		return -2;
	}
	return result;
}

// With all the room there is
static int write_all(const layout_t *layout)
{
	return write_watched(layout, WRITTEN_ROOM, WRITTEN_ROOM);
}

// The layout is written as exactly this text, and its length is returned. The room is that of the text and
// a kilobyte more: enough, and quick to prepare in a loop.
static bool written_is(const layout_t *layout, const char *wanted)
{
	size_t length = strlen(wanted);
	int result = write_watched(layout, length + 1024, length + 1024);

	return result == (int)length && strcmp(written, wanted) == 0;
}

static bool writes(const layout_t *layout, const char *wanted)
{
	if(!written_is(layout, wanted))
	{
		// Without a zero at its end if the call wrote none
		written[strlen(wanted) + 1023] = '\0';
		printf("  expected %.600s (%lu)\n  got      %.600s\n", wanted, (unsigned long)strlen(wanted), written);
		return false;
	}
	return true;
}

// The layout cannot be written: -1 and an empty text
static bool writes_nothing(const layout_t *layout)
{
	return write_all(layout) == -1 && written[0] == '\0';
}

// A text of the test with ' for ", four can be in use at a time
static const char *q(const char *json)
{
	static char texts[4][4096];
	static int next;
	char *out = texts[next++ % 4];

	snprintf(out, sizeof(texts[0]), "%s", json);
	return quoted(out);
}

// The text of a layout of one page whose items are written like this
static const char *page_of(const char *items)
{
	static char wanted[8192];

	snprintf(wanted, sizeof(wanted), W_HEAD "'pages':[{'items':[%s]}]}", items);
	return quoted(wanted);
}

// The layout comes back from its own text: written into LAYOUT_TEXT_MAX + 1 bytes, read without a problem
// and without a warning, and the same field by field with every byte of its texts. Its text is left in
// `written`.
static bool comes_back(const layout_t *layout)
{
	int length;

	kept = *layout;
	length = write_watched(&kept, LAYOUT_TEXT_MAX + 1, LAYOUT_TEXT_MAX + 1);
	if(length < 0 || !accepted_with(written, (size_t)length, 0, "", "") || !same_layout(&box.layout, &kept))
	{
		printf("  does not come back: %.600s (%d)\n", length >= 0 ? written : "", length);
		return false;
	}
	return true;
}

static void test_parse_model(void)
{
	// What may stand in place of a byte of a layout
	static const char replacements[] = "\"\\{}[]:,0123456789.-eEtfnu' \n\tAZaz*\177";
	int round;
	int read = 0, long_ones = 0, wrong = 0, wrong_long = 0;
	int broken_accepted = 0, broken_refused = 0, wrong_broken = 0;
	int wrong_written = 0, wrong_back = 0, back = 0, too_long = 0, longer = 0;

	random_seed(20261003);
	for(round = 0; round < 3000; round++)
	{
		size_t position;
		char before;
		bool result;

		model_layout();

		// The layout of the model is written as the text of the model, and that text is read back as the layout
		if(!written_is(&expected, canon))
		{
			if(wrong_written++ == 0) printf("  round %d: expected %.2000s\n", round, canon);
		}
		else if(strlen(canon) > LAYOUT_TEXT_MAX) too_long++;
		else if(comes_back(&expected)) back++;
		else wrong_back++;
		// Texts and members are never written longer than they were read
		if(strlen(canon) > text_length) longer++;

		if(text_length > LAYOUT_TEXT_MAX)
		{
			long_ones++;
			if(!refused_length(text, text_length, "", "text too long")) wrong_long++;
			continue;
		}

		read++;
		start();
		if(!parse(text, text_length) || !report_is("", "", model_warnings, model_warning_path, model_warning) || !guards_intact() ||
		   !same_layout(&box.layout, &expected))
		{
			if(wrong++ == 0)
			{
				printf("  round %d: %.*s\n", round, (int)text_length, text);
				show_report();
			}
		}

		// The same text with one byte replaced: whatever it is now, it is read as a whole or not at all
		position = (size_t)random_below((int)text_length);
		before = text[position];
		text[position] = replacements[random_below((int)sizeof(replacements) - 1)];
		start();
		memcpy(&snapshot, &box, sizeof(box));
		result = parse(text, text_length);
		if(result)
		{
			broken_accepted++;
			if(!TEXT_IS(reported.report.path, "") || !TEXT_IS(reported.report.problem, "") || !guards_intact() ||
			   box.layout.page_count < 1 || box.layout.page_count > LAYOUT_PAGES_MAX) wrong_broken++;
		}
		else
		{
			broken_refused++;
			if(memcmp(&snapshot, &box, sizeof(box)) != 0 || reported.report.problem[0] == '\0' || reported.report.warnings != 0 || !guards_intact())
			{
				if(wrong_broken++ == 0) printf("  round %d, byte %lu was '%c': %.*s\n", round, (unsigned long)position, before, (int)text_length, text);
			}
		}
	}
	check(read > 2500 && wrong == 0, "model: more than 2500 layouts made at random are read as the layout they describe, with their warnings");
	check(long_ones > 20 && wrong_long == 0, "model: those that came out longer than 16384 bytes are refused as too long");
	check(broken_accepted > 100 && broken_refused > 1500 && wrong_broken == 0,
	      "model: each of them with one byte replaced is accepted without a problem or refused with one, and a refused one leaves the layout untouched");
	check(wrong_written == 0, "model: layout_to_json() writes each of the 3000 layouts as the text the model puts together for it, byte for byte");
	check(back > 2500 && wrong_back == 0 && back + too_long == 3000,
	      "model: layout_parse() reads each of these texts back as the same layout, without a warning, unless it is longer than 16384 bytes");
	check(longer == 0, "model: none of these texts is longer than the text the layout was read from");
}

// Every beginning of a layout is refused and leaves the layout as it was
static void test_cut_texts(void)
{
	static char file[4096];
	size_t length, cut;
	int wrong = 0;

	check(read_fixture("fixtures/layout_every_member.json", file, sizeof(file)), "cut: the fixture is there");
	length = strlen(file);
	for(cut = 0; cut < length; cut++)
	{
		if(!refused_length(file, cut, "", "not valid JSON")) wrong++;
	}
	check(length > 800 && wrong == 0, "cut: every beginning of the fixture with every member is refused as broken JSON and leaves the layout untouched");
	check(accepted(file), "cut: the whole fixture is accepted");
}

/*
 * The built-in layout
 */

static const struct
{
	const char *title;
	const char *keys[LAYOUT_ITEMS_MAX + 1];
} default_pages[] = {
	{"Motor", {"ENGINE_RPM", "COOLANT_TMP", "ENGINE_OIL_TEMP", "ACCEL_PEDAL", NULL}},
	{"Ladeluft", {"BOOST_PRESSURE", "BOOST_PRESSURE_LP", "CHARGE_AIR_TEMP_PRE_IC", "CHARGE_AIR_TEMP_POST_IC", "INTAKE_AIR_TMP", "WASTEGATE", NULL}},
	{"Abgas", {"EGT_PRE_TURBO", "EGT_POST_EGR_COOLER", "EGT_PRE_CAT", "EGT_PRE_DPF", "EGT_PRE_SCR", "EXHAUST_BACK_PRESSURE", NULL}},
	{"DPF", {"DPF_SOOT_MASS", "DPF_SOOT_SIM", "DPF_ASH", "DPF_DIFF_PRESSURE", "DPF_KM_SINCE_REGEN", "DPF_REGEN_STATUS", NULL}},
	{"Kraftstoff", {"RAIL_PRESSURE", "INJECTION_QUANTITY", "FUEL_TEMP", "FUEL_L", "LAMBDA", "AIR_MASS_PER_STROKE", NULL}},
	{"AGR/Luft", {"EGR_RATE", "EGR_VALVE", "THROTTLE", "INTAKE_AIR_PRESSURE", "BARO_PRESSURE", NULL}},
	{"Sonstiges", {"OIL_LEVEL", "ECU_DISTANCE", NULL}},
};

// How often the layout that was read names a key
static int times_named(const char *key)
{
	int count = 0;
	int p, i;

	for(p = 0; p < box.layout.page_count && p < LAYOUT_PAGES_MAX; p++)
	{
		for(i = 0; i < box.layout.pages[p].item_count && i < LAYOUT_ITEMS_MAX; i++)
		{
			if(TEXT_IS(box.layout.pages[p].items[i].key, key)) count++;
		}
	}
	return count;
}

static const layout_item_t *item_named(const char *key)
{
	int p, i;

	for(p = 0; p < box.layout.page_count && p < LAYOUT_PAGES_MAX; p++)
	{
		for(i = 0; i < box.layout.pages[p].item_count && i < LAYOUT_ITEMS_MAX; i++)
		{
			if(TEXT_IS(box.layout.pages[p].items[i].key, key)) return &box.layout.pages[p].items[i];
		}
	}
	// An item nothing is right for
	return &made.pages[LAYOUT_PAGES_MAX - 1].items[LAYOUT_ITEMS_MAX - 1];
}

static bool shows_text(const layout_item_t *item, const value_t *value, const char *wanted);

static const char *catalog_unit(const char *name)
{
	int index = catalog_find(&catalog, name);

	return index < 0 ? "not in the catalogue" : catalog.entries[index].unit;
}

static void test_default_layout(void)
{
	static char file[LAYOUT_TEXT_MAX + 1];
	static char profile[16384];
	static json_token_t profile_tokens[1024];
	const layout_t *layout = &box.layout;
	const layout_item_t *item;
	int names = 0, once = 0, items = 0;
	int wrong_pages = 0, labelled = 0, limits = 0, units = 0;
	int shown = 0, waiting = 0, unavailable = 0;
	int pids, p, i, count;

	check(read_fixture("../layouts/w906_default.json", file, sizeof(file)), "default: display/layouts/w906_default.json is there and not longer than 16384 bytes");
	check(accepted(file), "default: it is accepted without a problem and without a warning");
	check(TEXT_IS(layout->name, "W906 OM651 Standard") && TEXT_IS(layout->profile_hint, "W906"), "default: name and profile hint");

	check(layout->page_count == 7, "default: seven pages");
	for(p = 0; p < 7; p++)
	{
		const layout_page_t *page = &layout->pages[p];

		if(!TEXT_IS(page->title, default_pages[p].title) || page->hidden) wrong_pages++;
		for(i = 0; default_pages[p].keys[i] != NULL; i++)
		{
			if(i >= page->item_count || !TEXT_IS(page->items[i].key, default_pages[p].keys[i])) wrong_pages++;
		}
		if(page->item_count != i) wrong_pages++;
		for(i = 0; i < page->item_count && i < LAYOUT_ITEMS_MAX; i++)
		{
			item = &page->items[i];
			items++;
			if(item->label[0] != '\0') labelled++;
			if(item->warn_lo.set || item->warn_hi.set || item->crit_lo.set || item->crit_hi.set) limits++;
			if(item->scale != 1) limits++;
		}
	}
	check(wrong_pages == 0, "default: the pages Motor, Ladeluft, Abgas, DPF, Kraftstoff, AGR/Luft, Sonstiges with their values in order, none hidden");
	check(items == 35 && labelled == 35, "default: 35 items, each with a label");
	check(limits == 0, "default: no warn or crit limits and no scale");

	// Every value of the vehicle profile exactly once
	check(read_fixture("../../vehicle_profiles/mercedes/sprinter_w906_om651.json", profile, sizeof(profile)), "default: the vehicle profile is there");
	count = json_parse(profile, strlen(profile), profile_tokens, 1024);
	pids = count > 0 ? json_member(profile, profile_tokens, 0, "pids") : -1;
	for(p = 0; pids >= 0 && json_element(profile_tokens, pids, p) >= 0; p++)
	{
		int parameters = json_member(profile, profile_tokens, json_element(profile_tokens, pids, p), "parameters");
		int key = parameters + 1;

		for(i = 0; parameters >= 0 && i < profile_tokens[parameters].size; i++)
		{
			char name[VALUE_NAME_SIZE];

			names++;
			if(json_text(profile, &profile_tokens[key], name, sizeof(name)) && times_named(name) == 1) once++;
			key += 1 + profile_tokens[key + 1].skip;
		}
	}
	check(names == 35 && once == 35, "default: each of the 35 values of the profile is named exactly once");

	item = item_named("ENGINE_RPM");
	check(item_is(item, "ENGINE_RPM", "Drehzahl", "1/min", 0, LAYOUT_WIDGET_ARC, 1) && limits_are(item, 0, 5000, NONE, NONE, NONE, NONE),
	      "default: the engine speed is an arc from 0 to 5000");
	item = item_named("ACCEL_PEDAL");
	check(item_is(item, "ACCEL_PEDAL", "Fahrpedal", "%", 0, LAYOUT_WIDGET_BAR, 1) && limits_are(item, 0, 100, NONE, NONE, NONE, NONE),
	      "default: the pedal is a bar from 0 to 100");
	item = item_named("DPF_REGEN_STATUS");
	check(item->widget == LAYOUT_WIDGET_STATE && item->map_count == 2 && map_is(item, 0, "1", "inaktiv") && map_is(item, 1, "*", "aktiv") && !item->has_unit,
	      "default: the regeneration is a state: 1 is inaktiv, everything else aktiv");
	check(item_is(item_named("COOLANT_TMP"), "COOLANT_TMP", "K\303\274hlwasser", "\302\260C", 0, LAYOUT_WIDGET_NUMBER, 1) &&
	      item_is(item_named("DPF_SOOT_MASS"), "DPF_SOOT_MASS", "Ru\303\237 gemessen", "g", 1, LAYOUT_WIDGET_NUMBER, 1) &&
	      item_is(item_named("LAMBDA"), "LAMBDA", "Lambda", NULL, 2, LAYOUT_WIDGET_NUMBER, 1), "default: labels, units and decimals of three values");

	// With the catalogue the adapter sends for this profile
	check(read_fixture("../../tools/w906/fixtures/car_config_w906.json", profile, sizeof(profile)), "default: the configuration of the adapter is there");
	catalog_init(&catalog);
	check(layout_suits(layout, &catalog) && layout_first_page(layout, &catalog) == 0 && layout_step_page(layout, &catalog, 0, 1) == 1 &&
	      layout_step_page(layout, &catalog, 6, 1) == 6, "default: before the catalogue is loaded the layout suits and all pages are there");
	check(catalog_apply_config(&catalog, profile, strlen(profile), catalog_work, CATALOG_TOKENS) && catalog.count == 36, "default: the catalogue of the W906 is loaded");
	values_init(&values);
	for(p = 0; p < 7; p++)
	{
		if(layout_page_shown(layout, p, &catalog)) shown++;
		for(i = 0; i < layout->pages[p].item_count && i < LAYOUT_ITEMS_MAX; i++)
		{
			item = &layout->pages[p].items[i];
			if(layout_item_state(item, &catalog, &values, 0) == LAYOUT_ITEM_NO_VALUE) waiting++;
			// The units of the layout are those of the adapter, but for the engine speed
			if(strcmp(layout_item_unit(item, &catalog), catalog_unit(item->key)) == 0) units++;
		}
	}
	check(layout_suits(layout, &catalog) && shown == 7 && waiting == 35, "default: with it the layout suits, all pages are shown, every item waits for its value");
	check(units == 34 && strcmp(layout_item_unit(item_named("ENGINE_RPM"), &catalog), "1/min") == 0 && strcmp(catalog_unit("ENGINE_RPM"), "RPM") == 0,
	      "default: the units are those of the adapter, only the engine speed is shown in 1/min instead of RPM");

	check(read_fixture("fixtures/layout_values.json", profile, sizeof(profile)) &&
	      values_apply(&values, profile, strlen(profile), -1, 1000, values_work, VALUES_TOKENS) == VALUES_RENEWED, "default: values arrive");
	check(shows_text(item_named("ENGINE_RPM"), values_find(&values, "ENGINE_RPM"), "813") &&
	      shows_text(item_named("COOLANT_TMP"), values_find(&values, "COOLANT_TMP"), "88") &&
	      shows_text(item_named("ENGINE_OIL_TEMP"), values_find(&values, "ENGINE_OIL_TEMP"), "95") &&
	      shows_text(item_named("ACCEL_PEDAL"), values_find(&values, "ACCEL_PEDAL"), "12") &&
	      shows_text(item_named("BOOST_PRESSURE"), values_find(&values, "BOOST_PRESSURE"), "1009") &&
	      shows_text(item_named("RAIL_PRESSURE"), values_find(&values, "RAIL_PRESSURE"), "312") &&
	      shows_text(item_named("ECU_DISTANCE"), values_find(&values, "ECU_DISTANCE"), "187432"), "default: values without decimals");
	check(shows_text(item_named("DPF_SOOT_MASS"), values_find(&values, "DPF_SOOT_MASS"), "4,4") &&
	      shows_text(item_named("OIL_LEVEL"), values_find(&values, "OIL_LEVEL"), "67,3") &&
	      shows_text(item_named("LAMBDA"), values_find(&values, "LAMBDA"), "1,36"), "default: values with one and two decimals");
	check(shows_text(item_named("DPF_REGEN_STATUS"), values_find(&values, "DPF_REGEN_STATUS"), "inaktiv"), "default: regeneration status 1 is shown as inaktiv");
	check(layout_item_state(item_named("ENGINE_RPM"), &catalog, &values, 1000) == LAYOUT_ITEM_LIVE &&
	      layout_item_state(item_named("FUEL_L"), &catalog, &values, 1000) == LAYOUT_ITEM_NO_VALUE &&
	      layout_item_level(item_named("ENGINE_RPM"), values_find(&values, "ENGINE_RPM")) == 0, "default: a value that arrived is live and within its limits");

	// Another vehicle
	check(load_catalog("{'SPEED':{'unit':'km/h'},'RPM':{},'ENGINE_RPM':{}}"), "default: the catalogue of another vehicle");
	for(p = 0; p < 7; p++)
	{
		for(i = 0; i < layout->pages[p].item_count && i < LAYOUT_ITEMS_MAX; i++)
		{
			if(layout_item_state(&layout->pages[p].items[i], &catalog, &values, 60000) == LAYOUT_ITEM_UNAVAILABLE) unavailable++;
		}
	}
	check(!layout_suits(layout, &catalog) && layout_first_page(layout, &catalog) == 0 && layout_step_page(layout, &catalog, 0, 1) == 0 && unavailable == 34,
	      "default: with it the layout does not suit, only the page with the engine speed is shown, 34 values are not available");
}

/*
 * Pages
 */

// In a child process: a function that takes page -1 for a page reads before the layout
static bool page_before_the_first_not_shown(void)
{
	load_catalog("{'A':{}}");
	make_layout(2);
	add_item(0, "A");
	add_item(1, "A");
	return !layout_page_shown(&made, -1, &catalog);
}

static void test_page_shown(void)
{
	layout_item_t *item;

	check(load_catalog("{'A':{},'B':{}}") && catalog.count == 3, "shown: a catalogue with two values");
	make_layout(3);
	add_item(0, "A");
	add_item(1, "X");
	add_item(2, "B");
	made.pages[2].hidden = true;
	// A fourth page that would be shown if it belonged to the layout
	add_item(3, "A");
	check(layout_page_shown(&made, 0, &catalog), "shown: a page that is not hidden and has a value of the catalogue");
	check(!layout_page_shown(&made, 1, &catalog), "shown: not a page whose only value is not in the catalogue");
	check(!layout_page_shown(&made, 2, &catalog), "shown: not a hidden page, although its value is in the catalogue");
	check(!layout_page_shown(&made, 3, &catalog), "shown: not the page behind the last one");
	check(in_child(page_before_the_first_not_shown), "shown: not the page before the first one");
	check(!layout_page_shown(&made, 4, &catalog) && !layout_page_shown(&made, LAYOUT_PAGES_MAX, &catalog) &&
	      !layout_page_shown(&made, INT_MAX, &catalog) && !layout_page_shown(&made, INT_MIN, &catalog), "shown: no page outside of the layout");

	make_layout(4);
	add_item(0, "X");
	add_item(0, "Y");
	add_item(0, "B");
	add_item(1, "A");
	add_item(1, "X");
	add_item(2, "X");
	add_item(2, "Y");
	add_item(2, "Z");
	add_item(2, "a");
	add_item(2, "AA");
	add_item(2, "");
	add_item(3, "X");
	// An item behind the last one of page 3 that is in the catalogue
	item = add_item(3, "A");
	made.pages[3].item_count = 1;
	check(layout_page_shown(&made, 0, &catalog), "shown: a page of which only the last value is in the catalogue");
	check(layout_page_shown(&made, 1, &catalog), "shown: a page of which only the first value is in the catalogue");
	check(!layout_page_shown(&made, 3, &catalog) && strcmp(item->key, "A") == 0, "shown: what stands behind the last item of a page does not count");
	check(!layout_page_shown(&made, 2, &catalog), "shown: not a page with six values of which none is in the catalogue, names are compared completely");

	make_layout(2);
	add_item(0, "@BATT_V");
	add_item(1, "@BATT");
	check(layout_page_shown(&made, 0, &catalog) && !layout_page_shown(&made, 1, &catalog), "shown: the battery voltage is a value of the catalogue");

	// Not loaded yet
	make_layout(4);
	add_item(0, "A");
	add_item(1, "X");
	add_item(2, "A");
	made.pages[2].hidden = true;
	add_item(3, "X");
	made.pages[3].hidden = true;
	catalog_init(&catalog);
	check(layout_page_shown(&made, 0, &catalog) && layout_page_shown(&made, 1, &catalog),
	      "shown: with a catalogue that holds nothing but the battery every page that is not hidden counts");
	check(!layout_page_shown(&made, 2, &catalog) && !layout_page_shown(&made, 3, &catalog), "shown: a hidden page does not count then either");
	check(!layout_page_shown(&made, 4, &catalog) && !layout_page_shown(&made, -1, &catalog), "shown: nor a page outside of the layout");
	memset(&catalog, 0, sizeof(catalog));
	check(layout_page_shown(&made, 1, &catalog) && !layout_page_shown(&made, 3, &catalog), "shown: a catalogue without any entry is not loaded either");

	// One entry that is not the battery: loaded
	memset(&catalog, 0, sizeof(catalog));
	strcpy(catalog.entries[0].name, "A");
	catalog.count = 1;
	check(layout_page_shown(&made, 0, &catalog) && !layout_page_shown(&made, 1, &catalog), "shown: a catalogue with one entry that is not the battery is loaded");
	catalog_init(&catalog);
	strcpy(catalog.entries[1].name, "B");
	catalog.count = 2;
	check(!layout_page_shown(&made, 0, &catalog) && !layout_page_shown(&made, 1, &catalog), "shown: a catalogue with the battery and one value is loaded");
	// What stands behind the last entry of the catalogue does not make it loaded
	catalog.count = 1;
	check(layout_page_shown(&made, 1, &catalog), "shown: an entry behind the last one of the catalogue does not count");
	// Names next to that of the battery are values
	catalog_init(&catalog);
	strcpy(catalog.entries[1].name, "@BATT_V2");
	catalog.count = 2;
	check(!layout_page_shown(&made, 1, &catalog), "shown: a value whose name begins like that of the battery makes the catalogue loaded");
	strcpy(catalog.entries[1].name, "@BATT_");
	check(!layout_page_shown(&made, 1, &catalog), "shown: a value whose name is the beginning of that of the battery makes the catalogue loaded");
	strcpy(catalog.entries[1].name, "@batt_v");
	check(!layout_page_shown(&made, 1, &catalog), "shown: a value named like the battery in other letters makes the catalogue loaded");
	strcpy(catalog.entries[0].name, "A");
	strcpy(catalog.entries[1].name, CATALOG_BATTERY);
	check(layout_page_shown(&made, 0, &catalog) && !layout_page_shown(&made, 1, &catalog), "shown: a catalogue is loaded wherever its battery entry stands");
}

// The rule of layout_step_page a second time, from the header: over all pages, the nearest shown one on each side
static int model_step(const bool *shown, int count, int page, int direction)
{
	int below = -1, above = -1;
	int i;

	for(i = 0; i < count; i++)
	{
		if(!shown[i]) continue;
		if(i < page) below = i;
		if(i > page && above < 0) above = i;
	}
	if(direction > 0 && above >= 0) return above;
	if(direction < 0 && below >= 0) return below;
	if(page >= 0 && page < count && shown[page]) return page;
	return direction > 0 ? below : above;
}

// A layout of pages that are shown (S), hidden (H), without a value of the catalogue (M), or both (B).
// The catalogue has to hold the value A.
static void make_pages(const char *kinds)
{
	int count = (int)strlen(kinds);
	int i;

	make_layout(count);
	for(i = 0; i < count; i++)
	{
		add_item(i, kinds[i] == 'S' || kinds[i] == 'H' ? "A" : "X");
		made.pages[i].hidden = kinds[i] == 'H' || kinds[i] == 'B';
	}
}

static int step(const char *kinds, int page, int direction)
{
	make_pages(kinds);
	return layout_step_page(&made, &catalog, page, direction);
}

static int first(const char *kinds)
{
	make_pages(kinds);
	return layout_first_page(&made, &catalog);
}

static void test_page_steps(void)
{
	static const char kinds[] = "SHMB";
	char pattern[8];
	bool shown[8];
	int count, code, page, direction, loaded, i;
	int wrong_step = 0, wrong_first = 0, wrong_shown = 0, tried = 0;

	check(load_catalog("{'A':{}}"), "step: a catalogue with the value A");

	check(first("SSS") == 0 && first("HSS") == 1 && first("MHS") == 2 && first("BMHS") == 3, "first: the first page that is shown");
	check(first("") == -1 && first("H") == -1 && first("M") == -1 && first("HMB") == -1, "first: -1 if no page is shown");

	check(step("SSS", 0, 1) == 1 && step("SSS", 1, 1) == 2, "step: +1 goes to the next page");
	check(step("SSS", 2, -1) == 1 && step("SSS", 1, -1) == 0, "step: -1 goes to the page before");
	check(step("SSS", 2, 1) == 2, "step: +1 at the last page stays there, no wrap");
	check(step("SSS", 0, -1) == 0, "step: -1 at the first page stays there, no wrap");
	check(step("SHS", 0, 1) == 2 && step("SHS", 2, -1) == 0, "step: a hidden page is skipped");
	check(step("SMS", 0, 1) == 2 && step("SMS", 2, -1) == 0, "step: a page whose values are all missing is skipped");
	check(step("SHMBS", 0, 1) == 4 && step("SHMBS", 4, -1) == 0, "step: several pages that are not shown are skipped");
	check(step("SSH", 1, 1) == 1 && step("SSM", 1, 1) == 1, "step: +1 at the last shown page stays there, pages behind it that are not shown do not count");
	check(step("HSS", 1, -1) == 1 && step("MSS", 1, -1) == 1, "step: -1 at the first shown page stays there");

	check(step("SHS", 1, 1) == 2 && step("SHS", 1, -1) == 0, "step: from a page that is not shown to the nearest shown one in the direction");
	check(step("SSHHSS", 2, 1) == 4 && step("SSHHSS", 3, -1) == 1, "step: the nearest, not the last or the first");
	check(step("SSHH", 3, 1) == 1 && step("SSHH", 2, 1) == 1, "step: from a page that is not shown with none in the direction to the nearest in the other direction");
	check(step("HHSS", 0, -1) == 2 && step("HHSS", 1, -1) == 2, "step: likewise backwards");
	check(step("SSMM", 3, 1) == 1 && step("MMSS", 0, -1) == 2, "step: likewise from a page whose values are all missing");
	check(step("HH", 0, 1) == -1 && step("HH", 1, -1) == -1 && step("M", 0, 1) == -1 && step("HMB", 1, -1) == -1, "step: -1 if no page is shown");
	check(step("", 0, 1) == -1 && step("", 0, -1) == -1, "step: -1 in a layout without pages");

	check(step("HSH", 1, 1) == 1 && step("HSH", 1, -1) == 1, "step: with one shown page every step stays there");
	check(step("HSH", 0, 1) == 1 && step("HSH", 0, -1) == 1 && step("HSH", 2, 1) == 1 && step("HSH", 2, -1) == 1,
	      "step: with one shown page every step from another page leads there");
	check(step("S", 0, 1) == 0 && step("S", 0, -1) == 0, "step: a layout of one page");

	check(step("SSS", -1, 1) == 0 && step("SSS", 3, -1) == 2, "step: from a page next to the layout into it");
	check(step("HSSH", -1, -1) == 1 && step("HSSH", 4, 1) == 2, "step: from a page next to the layout against the direction to the nearest shown one");
	check(step("HSSH", -5, 1) == 1 && step("HSSH", -5, -1) == 1 && step("HSSH", 9, 1) == 2 && step("HSSH", 9, -1) == 2,
	      "step: from a page far outside of the layout to the nearest shown one");
	check(step("HSSH", INT_MIN, 1) == 1 && step("HSSH", INT_MIN, -1) == 1 && step("HSSH", INT_MAX, 1) == 2 && step("HSSH", INT_MAX, -1) == 2,
	      "step: also from the smallest and the largest page number there is");
	check(step("HH", -3, 1) == -1 && step("HH", 7, -1) == -1 && step("", INT_MAX, 1) == -1 && step("", INT_MIN, -1) == -1,
	      "step: -1 from outside if no page is shown");

	check(step("SSS", 0, 2) == 1 && step("SSS", 0, 100) == 1 && step("SSS", 1, INT_MAX) == 2, "step: of a larger direction only the sign counts");
	check(step("SSS", 2, -2) == 1 && step("SSS", 2, -100) == 1 && step("SSS", 1, INT_MIN) == 0, "step: of a smaller direction only the sign counts");
	check(step("SSS", 0, 0) == 1 && step("SSS", 2, 0) == 2 && step("SHH", 2, 0) == 0, "step: direction 0 is +1");

	// With a catalogue that is not loaded only hidden pages are left out
	catalog_init(&catalog);
	check(step("SMS", 0, 1) == 1 && step("SMS", 2, -1) == 1 && first("MSS") == 0, "step: before the catalogue is loaded a page whose values are missing is not skipped");
	check(step("SBS", 0, 1) == 2 && step("SHS", 2, -1) == 0 && first("HBM") == 2, "step: a hidden page is skipped then as well");

	// Every layout of up to six pages, from every page in and next to it, in both directions
	for(loaded = 0; loaded < 2; loaded++)
	{
		if(loaded) load_catalog("{'A':{}}");
		else catalog_init(&catalog);

		for(count = 0; count <= 6; count++)
		{
			for(code = 0; code < 1 << (2 * count); code++)
			{
				for(i = 0; i < count; i++)
				{
					pattern[i] = kinds[(code >> (2 * i)) & 3];
					shown[i] = pattern[i] == 'S' || (pattern[i] == 'M' && !loaded);
				}
				pattern[count] = '\0';
				make_pages(pattern);

				for(i = 0; i < count; i++)
				{
					if(layout_page_shown(&made, i, &catalog) != shown[i]) wrong_shown++;
				}
				for(i = 0; i < count && !shown[i]; i++) continue;
				if(layout_first_page(&made, &catalog) != (i < count ? i : -1)) wrong_first++;

				for(page = -2; page <= count + 1; page++)
				{
					for(direction = -1; direction <= 1; direction += 2)
					{
						tried++;
						if(layout_step_page(&made, &catalog, page, direction) != model_step(shown, count, page, direction))
						{
							if(wrong_step++ == 0) printf("  \"%s\" from %d by %d, catalogue %s\n", pattern, page, direction, loaded ? "loaded" : "not loaded");
						}
					}
				}
			}
		}
	}
	check(tried == 211168 && wrong_step == 0, "step: all 211168 steps in the 5461 layouts of up to six pages, with and without a catalogue, are those of the model");
	check(wrong_shown == 0 && wrong_first == 0, "step: in all of them the shown pages and the first page are those of the model");

	// Layouts of 7 to 12 pages made at random, from every page in and next to them, in both directions
	random_seed(20261011);
	wrong_step = 0;
	wrong_first = 0;
	wrong_shown = 0;
	for(code = 0; code < 4000; code++)
	{
		char long_pattern[LAYOUT_PAGES_MAX + 1];
		bool long_shown[LAYOUT_PAGES_MAX];

		count = 7 + random_below(6);
		loaded = random_below(4) > 0;
		if(loaded) load_catalog("{'A':{}}");
		else catalog_init(&catalog);
		// Few shown pages in every third layout: then the steps are long and lead to every page
		direction = random_below(3) == 0 ? 8 : 2;
		for(i = 0; i < count; i++)
		{
			long_pattern[i] = random_below(direction) == 0 ? 'S' : kinds[1 + random_below(3)];
			long_shown[i] = long_pattern[i] == 'S' || (long_pattern[i] == 'M' && !loaded);
		}
		long_pattern[count] = '\0';
		make_pages(long_pattern);

		for(i = 0; i < count; i++)
		{
			if(layout_page_shown(&made, i, &catalog) != long_shown[i]) wrong_shown++;
		}
		for(i = 0; i < count && !long_shown[i]; i++) continue;
		if(layout_first_page(&made, &catalog) != (i < count ? i : -1)) wrong_first++;
		for(page = -2; page <= count + 1; page++)
		{
			for(direction = -1; direction <= 1; direction += 2)
			{
				if(layout_step_page(&made, &catalog, page, direction) != model_step(long_shown, count, page, direction))
				{
					if(wrong_step++ == 0) printf("  \"%s\" from %d by %d, catalogue %s\n", long_pattern, page, direction, loaded ? "loaded" : "not loaded");
				}
			}
		}
	}
	check(wrong_step == 0, "step: every step in 4000 layouts of 7 to 12 pages made at random is that of the model");
	check(wrong_shown == 0 && wrong_first == 0, "step: in all of these the shown pages and the first page are those of the model");

	// A full layout page by page
	load_catalog("{'A':{}}");
	make_pages("SSSSSSSSSSSS");
	wrong_step = 0;
	for(page = 0; page < 12; page++)
	{
		if(!layout_page_shown(&made, page, &catalog)) wrong_step++;
		if(layout_step_page(&made, &catalog, page, 1) != (page < 11 ? page + 1 : 11)) wrong_step++;
		if(layout_step_page(&made, &catalog, page, -1) != (page > 0 ? page - 1 : 0)) wrong_step++;
	}
	check(wrong_step == 0, "step: each of the twelve pages of a full layout is shown and is reached from its neighbours");
	wrong_step = 0;
	for(page = 0; page < 12; page++)
	{
		char one[LAYOUT_PAGES_MAX + 1] = "HHHHHHHHHHHH";

		one[page] = 'S';
		if(first(one) != page || step(one, 0, 1) != page || step(one, 11, -1) != page || step(one, 11, 1) != page || step(one, 0, -1) != page) wrong_step++;
	}
	check(wrong_step == 0, "step: each of the twelve pages is found if it is the only one shown, from both ends and in both directions");

	// Twelve pages
	load_catalog("{'A':{}}");
	check(step("SSSSSSSSSSSS", 10, 1) == 11 && step("SSSSSSSSSSSS", 11, 1) == 11 && step("SSSSSSSSSSSS", 12, 1) == 11 &&
	      step("SSSSSSSSSSSS", 12, -1) == 11 && step("SSSSSSSSSSSS", 13, -1) == 11, "step: at and behind the last page of a full layout");
	check(step("HHHHHHHHHHHS", 0, 1) == 11 && step("SHHHHHHHHHHH", 11, -1) == 0 && step("HHHHHHHHHHHS", 0, -1) == 11 &&
	      step("SHHHHHHHHHHH", 11, 1) == 0 && first("HHHHHHHHHHHS") == 11, "step: across a full layout");
	check(step("HHHHHHHHHHHH", 5, 1) == -1 && first("HHHHHHHHHHHH") == -1, "step: a full layout of which no page is shown");
}

static void test_suits(void)
{
	static const char *const pool[] = {"P0", "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8", "P9"};
	int round, wrong = 0, suiting = 0;

	check(load_catalog("{'A':{},'B':{},'C':{}}"), "suits: a catalogue with A, B and C");

	make_layout(1);
	add_item(0, "A");
	check(layout_suits(&made, &catalog), "suits: one key of one is in the catalogue");
	add_item(0, "X");
	check(layout_suits(&made, &catalog), "suits: one key of two is half");
	add_item(0, "Y");
	check(!layout_suits(&made, &catalog), "suits: one key of three is less than half");
	add_item(0, "B");
	check(layout_suits(&made, &catalog), "suits: two keys of four are half");
	add_item(0, "Z");
	check(!layout_suits(&made, &catalog), "suits: two keys of five are less than half");
	add_item(0, "C");
	check(layout_suits(&made, &catalog), "suits: three keys of six are half");

	make_layout(1);
	add_item(0, "X");
	check(!layout_suits(&made, &catalog), "suits: not if the only key is not in the catalogue");

	// The same key several times counts once
	make_layout(1);
	add_item(0, "A");
	add_item(0, "A");
	add_item(0, "A");
	add_item(0, "X");
	add_item(0, "Y");
	check(!layout_suits(&made, &catalog), "suits: a key of the catalogue named three times is one of three distinct keys");
	make_layout(1);
	add_item(0, "X");
	add_item(0, "X");
	add_item(0, "X");
	add_item(0, "A");
	add_item(0, "B");
	check(layout_suits(&made, &catalog), "suits: a missing key named three times is one of three distinct keys");
	make_layout(3);
	add_item(0, "A");
	add_item(1, "X");
	add_item(1, "Y");
	add_item(2, "A");
	add_item(2, "A");
	check(!layout_suits(&made, &catalog), "suits: a key counts once also if it stands on several pages");
	make_layout(2);
	add_item(0, "X");
	add_item(0, "A");
	add_item(1, "X");
	add_item(1, "X");
	check(layout_suits(&made, &catalog), "suits: a missing key counts once also if it stands on several pages");
	make_layout(2);
	add_item(0, "X");
	add_item(0, "Y");
	add_item(0, "A");
	add_item(1, "Y");
	add_item(1, "B");
	check(layout_suits(&made, &catalog), "suits: a key is known again if it is not the first of its page");

	// Keys are compared as a whole
	make_layout(1);
	add_item(0, "AA");
	add_item(0, "A");
	check(layout_suits(&made, &catalog), "suits: a key that is the beginning of the key before it is a key of its own");
	make_layout(2);
	add_item(0, "A");
	add_item(1, "AA");
	add_item(1, "AB");
	check(!layout_suits(&made, &catalog), "suits: a key that begins with a key before it is a key of its own");
	make_layout(1);
	add_item(0, "a");
	add_item(0, "A ");
	add_item(0, "B");
	check(!layout_suits(&made, &catalog), "suits: keys in other letters or with a space are not those of the catalogue");

	// Hidden pages belong to the layout
	make_layout(2);
	add_item(0, "X");
	add_item(0, "Y");
	add_item(0, "Z");
	made.pages[0].hidden = true;
	add_item(1, "A");
	check(!layout_suits(&made, &catalog), "suits: the keys of a hidden page count");
	make_layout(2);
	add_item(0, "X");
	add_item(1, "A");
	add_item(1, "B");
	add_item(1, "C");
	made.pages[1].hidden = true;
	check(layout_suits(&made, &catalog), "suits: also those that are in the catalogue");

	// What does not belong to the layout
	make_layout(1);
	add_item(0, "A");
	add_item(1, "X");
	add_item(1, "Y");
	check(layout_suits(&made, &catalog), "suits: a page behind the last one does not count");
	make_layout(1);
	add_item(0, "A");
	add_item(0, "X");
	add_item(0, "Y");
	made.pages[0].item_count = 1;
	check(layout_suits(&made, &catalog), "suits: an item behind the last one does not count");

	make_layout(1);
	add_item(0, "@BATT_V");
	add_item(0, "X");
	check(layout_suits(&made, &catalog), "suits: the battery voltage is a key of the catalogue");
	make_layout(0);
	check(layout_suits(&made, &catalog), "suits: a layout without keys has none that is missing");

	// Not loaded yet
	make_layout(1);
	add_item(0, "X");
	add_item(0, "Y");
	catalog_init(&catalog);
	check(layout_suits(&made, &catalog), "suits: every layout suits a catalogue that holds nothing but the battery");
	memset(&catalog, 0, sizeof(catalog));
	check(layout_suits(&made, &catalog), "suits: and one without any entry");
	memset(&catalog, 0, sizeof(catalog));
	strcpy(catalog.entries[0].name, "A");
	catalog.count = 1;
	check(!layout_suits(&made, &catalog), "suits: a catalogue with one entry that is not the battery is loaded");

	// Layouts made at random from ten keys, of which the catalogue has some
	random_seed(20261004);
	for(round = 0; round < 5000; round++)
	{
		bool in_catalog[10], in_layout[10];
		int distinct = 0, found = 0;
		int pages = 1 + random_below(12);
		int p, i, k;

		catalog_init(&catalog);
		memset(in_layout, 0, sizeof(in_layout));
		// At least one, so that the catalogue is loaded
		for(k = 0; k < 10; k++)
		{
			in_catalog[k] = k == 0 || random_below(3) == 0;
			if(in_catalog[k]) strcpy(catalog.entries[catalog.count++].name, pool[k]);
		}
		make_layout(pages);
		for(p = 0; p < pages; p++)
		{
			int items = 1 + random_below(pages > 2 ? 2 : 6);

			for(i = 0; i < items; i++)
			{
				k = random_below(10);
				add_item(p, pool[k]);
				in_layout[k] = true;
			}
		}
		for(k = 0; k < 10; k++)
		{
			if(in_layout[k]) distinct++;
			if(in_layout[k] && in_catalog[k]) found++;
		}
		if(layout_suits(&made, &catalog) != (2 * found >= distinct)) wrong++;
		if(2 * found >= distinct) suiting++;
	}
	check(wrong == 0 && suiting > 500 && suiting < 4500, "suits: 5000 layouts made at random suit exactly if half of their distinct keys are in the catalogue");

	// Every count of distinct keys a layout can have, with none, just less than half, half, just more than
	// half and all of them in the catalogue. The catalogue holds as many other values again.
	wrong = 0;
	for(round = 1; round <= LAYOUT_PAGES_MAX * LAYOUT_ITEMS_MAX; round++)
	{
		const int found_counts[] = {0, (round - 1) / 2, round / 2, (round + 1) / 2, round / 2 + 1, round};
		int f, k;

		make_layout((round + 5) / 6);
		for(k = 0; k < round; k++)
		{
			char key[8];

			snprintf(key, sizeof(key), "K%02d", k);
			add_item(k / 6, key);
		}
		for(f = 0; f < 6; f++)
		{
			int found = found_counts[f] > round ? round : found_counts[f];

			catalog_init(&catalog);
			// The keys that are found are the last of the layout, the others have names next to them
			for(k = 0; k < 20; k++) snprintf(catalog.entries[catalog.count++].name, sizeof(catalog.entries[0].name), "K%02dx", k);
			for(k = round - found; k < round; k++) snprintf(catalog.entries[catalog.count++].name, sizeof(catalog.entries[0].name), "K%02d", k);
			if(layout_suits(&made, &catalog) != (2 * found >= round))
			{
				if(wrong++ == 0) printf("  %d keys, %d of them in the catalogue\n", round, found);
			}
		}
	}
	check(wrong == 0, "suits: for each count of 1 to 72 distinct keys the layout suits from half of them on, not with one less");
}

/*
 * layout_from_catalog
 */

// A catalogue with the battery and `others` further values V000, V001, ...
static void fill_catalog(int others)
{
	int i;

	catalog_init(&catalog);
	for(i = 0; i < others; i++)
	{
		catalog_entry_t *entry = &catalog.entries[catalog.count++];

		snprintf(entry->name, sizeof(entry->name), "V%03d", i);
		snprintf(entry->unit, sizeof(entry->unit), "u%d", i);
	}
}

// An item as layout_from_catalog makes it
static bool is_generated(const layout_item_t *item, const char *key, const char *label)
{
	return item_is(item, key, label, NULL, 1, LAYOUT_WIDGET_NUMBER, 1) && limits_are(item, NONE, NONE, NONE, NONE, NONE, NONE) && item->map_count == 0;
}

static void from_catalog(void)
{
	start();
	layout_from_catalog(&catalog, &box.layout);
}

// In a child process: a function that opens a thirteenth page works behind the layout
static bool entry_49_finds_no_room(void)
{
	fill_catalog(48);
	from_catalog();
	return box.layout.page_count == 12 && box.layout.pages[11].item_count == 4 && guards_intact();
}

static void test_from_catalog(void)
{
	const layout_t *layout = &box.layout;
	int others, wrong = 0, wrong_back = 0;

	// No entry at all
	memset(&catalog, 0, sizeof(catalog));
	from_catalog();
	check(layout->page_count == 0 && bytes_are(layout, sizeof(*layout), 0) && guards_intact(), "from catalogue: a catalogue without entries gives a layout without pages");

	// The battery alone
	catalog_init(&catalog);
	from_catalog();
	check(layout->page_count == 1 && layout->pages[0].item_count == 1 && guards_intact(), "from catalogue: one entry gives one page with one item");
	check(TEXT_IS(layout->pages[0].title, "Werte 1") && !layout->pages[0].hidden, "from catalogue: the page is called Werte 1 and is not hidden");
	check(TEXT_IS(layout->pages[0].items[0].key, "@BATT_V"), "from catalogue: the key is the name of the entry");
	check(TEXT_IS(layout->pages[0].items[0].label, "Batt V"), "from catalogue: the label is made by fmt_label()");
	check(!layout->pages[0].items[0].has_unit && TEXT_IS(layout->pages[0].items[0].unit, "") &&
	      strcmp(layout_item_unit(&layout->pages[0].items[0], &catalog), "V") == 0, "from catalogue: the item has no unit of its own, that of the catalogue is shown");
	check(layout->pages[0].items[0].widget == LAYOUT_WIDGET_NUMBER, "from catalogue: the widget is number");
	check(layout->pages[0].items[0].decimals == 1, "from catalogue: one decimal");
	check(layout->pages[0].items[0].scale == 1 && limits_are(&layout->pages[0].items[0], NONE, NONE, NONE, NONE, NONE, NONE) &&
	      layout->pages[0].items[0].map_count == 0, "from catalogue: scale 1, no range, no limits, no map");
	check(TEXT_IS(layout->name, "") && TEXT_IS(layout->profile_hint, "") && bytes_are(&layout->pages[0].items[1], 5 * sizeof(layout_item_t), 0) &&
	      bytes_are(&layout->pages[1], 11 * sizeof(layout_page_t), 0), "from catalogue: no name, no profile hint, everything behind the item is zero");

	// Four entries: one page
	fill_catalog(3);
	from_catalog();
	check(layout->page_count == 1 && layout->pages[0].item_count == 4, "from catalogue: four entries fill one page");
	check(is_generated(&layout->pages[0].items[0], "V000", "V000") && is_generated(&layout->pages[0].items[1], "V001", "V001") &&
	      is_generated(&layout->pages[0].items[2], "V002", "V002"), "from catalogue: the entries in the order of the catalogue");
	check(is_generated(&layout->pages[0].items[3], "@BATT_V", "Batt V"), "from catalogue: the battery, the first entry of the catalogue, comes last");

	// Five entries: a second page
	fill_catalog(4);
	from_catalog();
	check(layout->page_count == 2 && layout->pages[0].item_count == 4 && layout->pages[1].item_count == 1, "from catalogue: the fifth entry opens a second page");
	check(is_generated(&layout->pages[0].items[0], "V000", "V000") && is_generated(&layout->pages[0].items[3], "V003", "V003") &&
	      is_generated(&layout->pages[1].items[0], "@BATT_V", "Batt V"), "from catalogue: four values on the first page, the battery on the second");
	check(TEXT_IS(layout->pages[0].title, "Werte 1") && TEXT_IS(layout->pages[1].title, "Werte 2") && !layout->pages[1].hidden, "from catalogue: Werte 1 and Werte 2");

	// 48 entries fill the layout, the battery is its last item
	fill_catalog(47);
	from_catalog();
	check(layout->page_count == 12 && layout->pages[11].item_count == 4 && is_generated(&layout->pages[11].items[2], "V046", "V046") &&
	      is_generated(&layout->pages[11].items[3], "@BATT_V", "Batt V") && guards_intact(), "from catalogue: 48 entries fill 12 pages, the battery is the last item");
	check(TEXT_IS(layout->pages[8].title, "Werte 9") && TEXT_IS(layout->pages[9].title, "Werte 10") && TEXT_IS(layout->pages[11].title, "Werte 12"),
	      "from catalogue: Werte 9, Werte 10 and Werte 12");
	// With 49 it finds no room any more
	check(in_child(entry_49_finds_no_room), "from catalogue: of 49 entries 48 are taken and nothing is written behind the layout");
	fill_catalog(48);
	from_catalog();
	check(layout->page_count == 12 && layout->pages[11].item_count == 4 && is_generated(&layout->pages[11].items[3], "V047", "V047") && guards_intact(),
	      "from catalogue: of 49 entries the battery, which comes last, finds no room");

	// A full catalogue
	fill_catalog(CATALOG_MAX - 1);
	from_catalog();
	check(catalog.count == CATALOG_MAX && layout->page_count == 12 && guards_intact(), "from catalogue: a full catalogue gives 12 pages and nothing behind the layout is written");
	check(is_generated(&layout->pages[0].items[0], "V000", "V000") && is_generated(&layout->pages[11].items[3], "V047", "V047") && layout->pages[11].item_count == 4,
	      "from catalogue: they hold the first 48 values");

	// The battery is last wherever it stands, and a catalogue made by hand need not have one
	fill_catalog(5);
	catalog.entries[0] = catalog.entries[3];
	strcpy(catalog.entries[3].name, CATALOG_BATTERY);
	from_catalog();
	check(layout->page_count == 2 && is_generated(&layout->pages[0].items[0], "V002", "V002") && is_generated(&layout->pages[0].items[1], "V000", "V000") &&
	      is_generated(&layout->pages[0].items[2], "V001", "V001") && is_generated(&layout->pages[0].items[3], "V003", "V003") &&
	      is_generated(&layout->pages[1].items[0], "V004", "V004") && is_generated(&layout->pages[1].items[1], "@BATT_V", "Batt V") && layout->pages[1].item_count == 2,
	      "from catalogue: the battery comes last also if it stands in the middle of the catalogue");
	strcpy(catalog.entries[3].name, "@BATT");
	from_catalog();
	check(layout->page_count == 2 && is_generated(&layout->pages[0].items[3], "@BATT", "Batt") && is_generated(&layout->pages[1].items[1], "V004", "V004") &&
	      layout->pages[1].item_count == 2, "from catalogue: without a battery entry the entries stand in their order");

	// Labels
	catalog_init(&catalog);
	strcpy(catalog.entries[1].name, "ENGINE_OIL_TEMP");
	strcpy(catalog.entries[2].name, "abcdefghijklmnopqrstuvwx");
	strcpy(catalog.entries[3].name, "abcdefghijklmnopqrstuvwxy");
	strcpy(catalog.entries[4].name, "ABCDEFGHIJKLMNOPQRSTUVWXYZ_ABCDE");
	strcpy(catalog.entries[5].name, "_A_B_C_D_E_F_G_H_I_J_K_L_");
	strcpy(catalog.entries[5].unit, "abcdefghijk");
	catalog.count = 6;
	from_catalog();
	check(is_generated(&layout->pages[0].items[0], "ENGINE_OIL_TEMP", "Engine Oil Temp"), "from catalogue: ENGINE_OIL_TEMP is labelled Engine Oil Temp");
	check(is_generated(&layout->pages[0].items[1], "abcdefghijklmnopqrstuvwx", "Abcdefghijklmnopqrstuvwx"), "from catalogue: a label of 24 bytes fits");
	check(is_generated(&layout->pages[0].items[2], "abcdefghijklmnopqrstuvwxy", ""), "from catalogue: a label of 25 bytes does not fit, the item has none");
	check(is_generated(&layout->pages[0].items[3], "ABCDEFGHIJKLMNOPQRSTUVWXYZ_ABCDE", ""), "from catalogue: a name of 32 bytes is a key, its label does not fit");
	check(is_generated(&layout->pages[1].items[0], "_A_B_C_D_E_F_G_H_I_J_K_L_", "A B C D E F G H I J K L"),
	      "from catalogue: a name of 25 bytes whose label has 23 gets it: the label is measured, not the name");
	check(strcmp(layout_item_unit(&layout->pages[1].items[0], &catalog), "abcdefghijk") == 0, "from catalogue: a unit of 11 bytes stays in the catalogue and is shown from there");

	// Every count of entries: item n stands on page n / 4, the battery behind the others
	for(others = 0; others < CATALOG_MAX; others++)
	{
		int total = others + 1 > 48 ? 48 : others + 1;
		int n;

		fill_catalog(others);
		from_catalog();
		if(layout->page_count != (total + 3) / 4 || !guards_intact()) wrong++;
		for(n = 0; n < total; n++)
		{
			const layout_page_t *page = &layout->pages[n / 4];
			char key[16], title[16];	// room for any int: gcc refuses a snprintf() that might cut

			snprintf(key, sizeof(key), "V%03d", n);
			snprintf(title, sizeof(title), "Werte %d", n / 4 + 1);
			if(!TEXT_IS(page->title, title) || page->hidden) wrong++;
			if(n == others ? !is_generated(&page->items[n % 4], "@BATT_V", "Batt V") : !is_generated(&page->items[n % 4], key, key)) wrong++;
			if((n == total - 1 || n % 4 == 3) && page->item_count != n % 4 + 1) wrong++;
		}
		// Last: it reads the layout anew
		if(!comes_back(layout)) wrong_back++;
	}
	check(wrong == 0, "from catalogue: for every count of entries up to a full catalogue the items stand four to a page, the battery last");
	check(wrong_back == 0, "from catalogue: each of these layouts is written by layout_to_json() and read back by layout_parse() as the same layout");
}

// The rule for the names a second time: a name can be a key if it has a byte and none of its bytes is one of these
#define CONTROLS "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f\x7f"

static bool model_usable(const char *name)
{
	return name[0] != '\0' && name[strcspn(name, CONTROLS)] == '\0';
}

// A catalogue made by hand with these names
static void name_catalog(const char *const *names, int count)
{
	int i;

	memset(&catalog, 0, sizeof(catalog));
	for(i = 0; i < count; i++) strcpy(catalog.entries[i].name, names[i]);
	catalog.count = count;
}

#define NAMES(...) name_catalog((const char *const[]){__VA_ARGS__}, (int)(sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *)))

static void test_from_catalog_names(void)
{
	static const char pool[] = "_@ \"\\~\x01\x1f\x7f\x80\xc3\xa4";
	static char file[4096];
	const layout_t *layout = &box.layout;
	const layout_page_t *page = &layout->pages[0];
	char name[4];
	size_t c;
	int round, e, k, n;
	int wrong = 0, wrong_items = 0, wrong_text = 0, without_pages = 0, full = 0;

	// What layout_parse() would not take as a key
	NAMES("@BATT_V", "", "Z");
	from_catalog();
	check(layout->page_count == 1 && page->item_count == 2 && is_generated(&page->items[0], "Z", "Z") && is_generated(&page->items[1], "@BATT_V", "Batt V"),
	      "from catalogue: an entry with an empty name is left out");
	for(c = 0; c < strlen(CONTROLS); c++)
	{
		name[0] = CONTROLS[c];
		name[1] = '\0';
		NAMES("@BATT_V", name, "Z");
		from_catalog();
		if(layout->page_count != 1 || page->item_count != 2 || !is_generated(&page->items[0], "Z", "Z") || !is_generated(&page->items[1], "@BATT_V", "Batt V")) wrong++;
	}
	check(strlen(CONTROLS) == 32 && wrong == 0, "from catalogue: a name that is one of the bytes 0x01 to 0x1f or 0x7f is left out");
	NAMES("@BATT_V", "\nAB", "A\nB", "AB\n", "\x7f" "AB", "A\x7f" "B", "AB\x7f", "AB");
	from_catalog();
	check(layout->page_count == 1 && page->item_count == 2 && is_generated(&page->items[0], "AB", "Ab") && is_generated(&page->items[1], "@BATT_V", "Batt V"),
	      "from catalogue: a control character at the beginning, in the middle or at the end of a name leaves it out");
	NAMES(" ", "!", "~", "\x80", "\xff");
	from_catalog();
	check(layout->page_count == 2 && page->item_count == 4 && is_generated(&page->items[0], " ", " ") && is_generated(&page->items[1], "!", "!") &&
	      is_generated(&page->items[2], "~", "~") && is_generated(&page->items[3], "\x80", "\x80") && is_generated(&layout->pages[1].items[0], "\xff", "\xff"),
	      "from catalogue: the bytes next to the control characters are taken: space, !, ~, 0x80 and 0xff");
	NAMES("A\"B\\C");
	from_catalog();
	check(is_generated(&page->items[0], "A\"B\\C", "A\"b\\c"), "from catalogue: a name with a quote and a backslash is taken");

	// What is left out takes no room and makes no page
	NAMES("", "A", "\n", "B", "\x7f", "C", "", "D", "\x01", "E");
	from_catalog();
	check(layout->page_count == 2 && page->item_count == 4 && is_generated(&page->items[0], "A", "A") && is_generated(&page->items[3], "D", "D") &&
	      layout->pages[1].item_count == 1 && is_generated(&layout->pages[1].items[0], "E", "E") && bytes_are(&layout->pages[1].items[1], 5 * sizeof(layout_item_t), 0),
	      "from catalogue: a name that is left out takes no place on a page");
	NAMES("", "\n", "\x7f");
	from_catalog();
	check(layout->page_count == 0 && bytes_are(layout, sizeof(*layout), 0) && guards_intact(), "from catalogue: a catalogue without a name that can be a key gives a layout without pages");
	NAMES("A", "B", "C", "D", "", "\n");
	from_catalog();
	check(layout->page_count == 1 && page->item_count == 4 && bytes_are(&layout->pages[1], 11 * sizeof(layout_page_t), 0),
	      "from catalogue: names that are left out open no page behind a full one");
	NAMES("A", "B", "BEHIND");
	catalog.count = 2;
	from_catalog();
	check(layout->page_count == 1 && page->item_count == 2 && is_generated(&page->items[1], "B", "B") && bytes_are(&page->items[2], 4 * sizeof(layout_item_t), 0),
	      "from catalogue: an entry behind the last one of the catalogue is not taken");
	NAMES("A", "B", CATALOG_BATTERY);
	catalog.count = 2;
	from_catalog();
	check(layout->page_count == 1 && page->item_count == 2 && is_generated(&page->items[1], "B", "B"), "from catalogue: nor a battery entry behind the last one");

	// A label that does not fit leaves nothing in the field
	NAMES("abcdefghijklmnopqrstuvwxy", "ABCDEFGHIJKLMNOPQRSTUVWXYZ_ABCDE", "abcdefghijklmnopqrstuvwx_y");
	from_catalog();
	check(bytes_are(page->items[0].label, sizeof(page->items[0].label), 0) && bytes_are(page->items[1].label, sizeof(page->items[1].label), 0),
	      "from catalogue: a label that does not fit leaves every byte of the field zero");
	check(is_generated(&page->items[2], "abcdefghijklmnopqrstuvwx_y", "") && bytes_are(page->items[2].label, sizeof(page->items[2].label), 0),
	      "from catalogue: also the last byte, where a label of 24 bytes and a word more leaves its space");
	check(comes_back(layout), "from catalogue: a layout with keys that have no label is read back as the same layout");

	// The texts
	catalog_init(&catalog);
	from_catalog();
	check(writes(layout, "{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":[{\"title\":\"Werte 1\",\"items\":[{\"key\":\"@BATT_V\",\"label\":\"Batt V\",\"dec\":1}]}]}"),
	      "from catalogue: the text of the layout of a fresh catalogue: one page with the battery, its label and one decimal");
	NAMES("@BATT_V", "ENGINE_RPM", "COOLANT_TMP", "abcdefghijklmnopqrstuvwxy", "A\"B");
	from_catalog();
	check(writes(layout, "{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":[{\"title\":\"Werte 1\",\"items\":["
	                     "{\"key\":\"ENGINE_RPM\",\"label\":\"Engine Rpm\",\"dec\":1},{\"key\":\"COOLANT_TMP\",\"label\":\"Coolant Tmp\",\"dec\":1},"
	                     "{\"key\":\"abcdefghijklmnopqrstuvwxy\",\"dec\":1},{\"key\":\"A\\\"B\",\"label\":\"A\\\"b\",\"dec\":1}]},"
	                     "{\"title\":\"Werte 2\",\"items\":[{\"key\":\"@BATT_V\",\"label\":\"Batt V\",\"dec\":1}]}]}"),
	      "from catalogue: the text of a layout of two pages: titles, keys, labels where they fit, one decimal, nothing else");

	check(read_fixture("../../tools/w906/fixtures/car_config_w906.json", file, sizeof(file)), "from catalogue: the configuration of the adapter is there");
	catalog_init(&catalog);
	check(catalog_apply_config(&catalog, file, strlen(file), catalog_work, CATALOG_TOKENS), "from catalogue: the catalogue of the W906 is loaded");
	from_catalog();
	check(layout->page_count == 9 && is_generated(&page->items[0], "ENGINE_RPM", "Engine Rpm") && layout->pages[8].item_count == 4 &&
	      is_generated(&layout->pages[8].items[3], "@BATT_V", "Batt V"), "from catalogue: the 36 values of the W906 give nine pages, the engine speed first, the battery last");
	check(comes_back(layout) && strlen(written) <= LAYOUT_GENERATED_TEXT_MAX, "from catalogue: that layout is written and read back as the same layout");

	// The longest text: 48 names of 24 quotes or backslashes, which are written with two bytes in the key and
	// in the label, and 8 underscores, which the label does not have. With 25 such bytes there is no label.
	memset(&catalog, 0, sizeof(catalog));
	for(e = 0; e < 48; e++)
	{
		for(k = 0; k < 24; k++) catalog.entries[e].name[k] = (e >> (k % 6)) & 1 ? '"' : '\\';
		memset(catalog.entries[e].name + 24, '_', 8);
	}
	catalog.count = 48;
	from_catalog();
	kept = *layout;
	// 50 bytes around the pages, 11 commas between them. A page: 30 bytes around its four items (31 from
	// "Werte 10" on) and three commas. An item: 7 + 58 for the key, 9 + 50 for the label, 8 for the decimals, a brace.
	check(write_watched(&kept, LAYOUT_TEXT_MAX + 1, LAYOUT_TEXT_MAX + 1) == 50 + 11 + 9 * 33 + 3 * 34 + 48 * (65 + 59 + 8 + 1) && strlen(written) == 6844 &&
	      LAYOUT_GENERATED_TEXT_MAX == 6844, "from catalogue: the longest text of a generated layout has 6844 bytes, which is LAYOUT_GENERATED_TEXT_MAX");
	check(LAYOUT_GENERATED_TEXT_MAX < LAYOUT_TEXT_MAX && comes_back(&kept), "from catalogue: it fits into LAYOUT_TEXT_MAX + 1 bytes and is read back as the same layout");
	for(e = 0; e < 48; e++) catalog.entries[e].name[24] = '"';
	from_catalog();
	kept = *layout;
	check(write_all(&kept) == 50 + 11 + 9 * 33 + 3 * 34 + 48 * (66 + 8 + 1) && comes_back(&kept), "from catalogue: with one quote more the label does not fit and the text is shorter");

	// Catalogues made at random, also with names that cannot be keys: the layout is the one of the rule, its
	// text is short enough and is read back
	random_seed(20261005);
	for(round = 0; round < 1000; round++)
	{
		int order[CATALOG_MAX + 1];
		int count = random_below(5) == 0 ? CATALOG_MAX : random_below(CATALOG_MAX + 1);
		int battery = -1;

		memset(&catalog, 0, sizeof(catalog));
		catalog.count = count;
		if(count > 0 && random_below(4) > 0) battery = random_below(count);
		for(e = 0; e < count; e++)
		{
			char *bytes = catalog.entries[e].name;
			int length = random_below(8) == 0 ? random_below(33) : random_below(10);

			// Mostly letters, so that most names can be keys
			for(k = 0; k < length; k++) bytes[k] = random_below(40) == 0 ? pool[random_below((int)sizeof(pool) - 1)] : (char)('A' + random_below(26));
			if(e == battery) strcpy(bytes, CATALOG_BATTERY);
		}

		// The rule of the header: what can be a key in the order of the catalogue, the battery last, 48 at most
		n = 0;
		for(e = 0; e < count; e++)
		{
			if(e != battery && model_usable(catalog.entries[e].name)) order[n++] = e;
		}
		if(battery >= 0) order[n++] = battery;
		if(n > 48) n = 48;
		if(n == 48) full++;

		from_catalog();
		if(layout->page_count != (n + 3) / 4 || !guards_intact()) wrong_items++;
		for(k = 0; k < n && k < LAYOUT_PAGES_MAX * 4; k++)
		{
			const layout_item_t *made_item = &layout->pages[k / 4].items[k % 4];
			const char *key = catalog.entries[order[k]].name;
			char label[LAYOUT_TITLE_SIZE];

			if(!fmt_label(key, label, sizeof(label))) label[0] = '\0';
			if(!is_generated(made_item, key, label) || !bytes_are(made_item->label + strlen(label), sizeof(made_item->label) - strlen(label), 0)) wrong_items++;
			if((k == n - 1 || k % 4 == 3) && layout->pages[k / 4].item_count != k % 4 + 1) wrong_items++;
		}

		if(n == 0)
		{
			without_pages++;
			if(!written_is(layout, "{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":[]}")) wrong_text++;
		}
		else if(!comes_back(layout) || strlen(written) > LAYOUT_GENERATED_TEXT_MAX)
		{
			if(wrong_text++ == 0) printf("  round %d\n", round);
		}
	}
	check(wrong_items == 0 && full > 150, "from catalogue: 1000 catalogues made at random give the names that can be keys in their order, the battery last, 48 at most");
	check(wrong_text == 0 && without_pages > 2,
	      "from catalogue: each of their layouts is written in at most LAYOUT_GENERATED_TEXT_MAX bytes and read back as the same layout, or has no page");
}

/*
 * Items
 */

static value_t value_of(value_kind_t kind, double number)
{
	value_t value;

	memset(&value, 0, sizeof(value));
	strcpy(value.name, "K");
	value.kind = kind;
	value.number = number;
	return value;
}

static value_t number_value;
static const value_t *number(double n)
{
	number_value = value_of(VALUE_NUMBER, n);
	return &number_value;
}

static const value_t *on(void)
{
	static value_t value;

	value = value_of(VALUE_ON, 0);
	return &value;
}

static const value_t *off(void)
{
	static value_t value;

	value = value_of(VALUE_OFF, 0);
	return &value;
}

// The text of an item in a buffer of `size` bytes between guard bytes: `wanted` if it is to fit, else false
// and an empty text, and never a byte outside
static bool text_sized(const layout_item_t *item, const value_t *value, size_t size, bool fits, const char *wanted)
{
	unsigned char buffer[4 + ROOM + 8];
	char *out = (char *)buffer + 4;
	bool result;

	memset(buffer, GUARD, sizeof(buffer));
	result = layout_item_text(item, value, out, size);
	if(!bytes_are(buffer, 4, GUARD) || !bytes_are(buffer + 4 + size, sizeof(buffer) - 4 - size, GUARD) || result != fits ||
	   (size > 0 && strcmp(out, fits ? wanted : "") != 0))
	{
		buffer[sizeof(buffer) - 1] = '\0';
		printf("  expected %s \"%s\", got %s \"%s\"\n", fits ? "true" : "false", fits ? wanted : "", result ? "true" : "false", size > 0 ? out : "");
		return false;
	}
	return true;
}

static bool shows_text(const layout_item_t *item, const value_t *value, const char *wanted)
{
	return text_sized(item, value, ROOM, true, wanted);
}

static bool shows_nothing(const layout_item_t *item, const value_t *value)
{
	return text_sized(item, value, ROOM, false, "");
}

static layout_item_t item;

static void make_item(layout_widget_t widget, int decimals, double scale)
{
	memset(&item, 0, sizeof(item));
	strcpy(item.key, "K");
	item.widget = widget;
	item.decimals = (uint8_t)decimals;
	item.scale = scale;
}

static void map_add(const char *raw, const char *shown)
{
	strcpy(item.map[item.map_count].raw, raw);
	strcpy(item.map[item.map_count].text, shown);
	item.map_count++;
}

static void set_limit(layout_limit_t *limit, double value)
{
	limit->set = true;
	limit->value = value;
}

// true if a state widget whose map names only this raw text shows its text for the value
static bool hits(const value_t *value, const char *raw)
{
	char out[ROOM];

	make_item(LAYOUT_WIDGET_STATE, 3, 1);
	map_add(raw, "hit");
	return layout_item_text(&item, value, out, sizeof(out)) && strcmp(out, "hit") == 0;
}

// No value: the pointer is NULL
static bool number_without_value(void)
{
	make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
	return shows_nothing(&item, NULL);
}

static bool number_without_value_and_room(void)
{
	make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
	return text_sized(&item, NULL, 1, false, "") && text_sized(&item, NULL, 0, false, "");
}

static bool state_without_value(void)
{
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("1", "inaktiv");
	map_add("*", "aktiv");
	return shows_nothing(&item, NULL) && text_sized(&item, NULL, 1, false, "") && text_sized(&item, NULL, 0, false, "");
}

static bool level_without_value(void)
{
	make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
	set_limit(&item.warn_lo, 0);
	set_limit(&item.crit_lo, 0);
	set_limit(&item.warn_hi, 0);
	set_limit(&item.crit_hi, 0);
	return layout_item_level(&item, NULL) == 0 && layout_item_level(&item, number(0)) == 2;
}

static void test_item_text_numbers(void)
{
	static const layout_widget_t widgets[] = {LAYOUT_WIDGET_NUMBER, LAYOUT_WIDGET_ARC, LAYOUT_WIDGET_BAR};
	static const char *const names[] = {"number", "arc", "bar"};
	char what[160];
	int w, round, wrong = 0;

	for(w = 0; w < 3; w++)
	{
		make_item(widgets[w], 0, 1);
		map_add("12", "a map");
		map_add("*", "a map");
		snprintf(what, sizeof(what), "text: a %s widget shows the value by fmt_number(), without decimals, whatever its map says", names[w]);
		check(shows_text(&item, number(12), "12") && shows_text(&item, number(12.5), "13") && shows_text(&item, number(-0.4), "0"), what);
		item.decimals = 1;
		snprintf(what, sizeof(what), "text: a %s widget with one decimal", names[w]);
		check(shows_text(&item, number(12), "12,0") && shows_text(&item, number(443.65), "443,7") && shows_text(&item, number(-0.04), "0,0"), what);
		item.decimals = 2;
		snprintf(what, sizeof(what), "text: a %s widget with two decimals", names[w]);
		check(shows_text(&item, number(12.345), "12,35") && shows_text(&item, number(-7), "-7,00"), what);
		item.decimals = 3;
		snprintf(what, sizeof(what), "text: a %s widget with three decimals", names[w]);
		check(shows_text(&item, number(0.0625), "0,063") && shows_text(&item, number(1234567), "1234567,000"), what);

		make_item(widgets[w], 0, 0.5);
		snprintf(what, sizeof(what), "text: a %s widget shows the value times its scale", names[w]);
		check(shows_text(&item, number(3801), "1901") && shows_text(&item, number(3), "2") && shows_text(&item, number(-3), "-2"), what);
		make_item(widgets[w], 2, -0.25);
		snprintf(what, sizeof(what), "text: a %s widget with a negative scale and decimals", names[w]);
		check(shows_text(&item, number(10), "-2,50") && shows_text(&item, number(-1), "0,25") && shows_text(&item, number(0), "0,00"), what);

		make_item(widgets[w], 0, 1);
		snprintf(what, sizeof(what), "text: a %s widget shows on as 1 and off as 0", names[w]);
		check(shows_text(&item, on(), "1") && shows_text(&item, off(), "0"), what);
		make_item(widgets[w], 1, 2.5);
		snprintf(what, sizeof(what), "text: a %s widget shows on and off times its scale", names[w]);
		check(shows_text(&item, on(), "2,5") && shows_text(&item, off(), "0,0"), what);
	}

	// What is none of the enum
	make_item((layout_widget_t)4, 1, 2);
	map_add("5", "five");
	map_add("*", "star");
	check(shows_text(&item, number(5), "10,0") && shows_text(&item, on(), "2,0"), "text: the widget behind the last one of the enum shows the number, it asks no map");
	item.widget = (layout_widget_t)255;
	check(shows_text(&item, number(5), "10,0"), "text: the widget 255 shows the number");
	item.widget = (layout_widget_t)-1;
	check(shows_text(&item, number(5), "10,0"), "text: the widget -1 shows the number");
	make_item(LAYOUT_WIDGET_NUMBER, 0, 3);
	number_value = value_of((value_kind_t)3, 7);
	check(shows_text(&item, &number_value, "0"), "text: a value of the kind behind the last one of the enum counts as off, whatever its number");
	number_value = value_of((value_kind_t)-1, 7);
	check(shows_text(&item, &number_value, "0"), "text: a value of the kind -1 counts as off");
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("off", "aus");
	map_add("on", "an");
	map_add("7", "sieben");
	number_value = value_of((value_kind_t)3, 7);
	check(shows_text(&item, &number_value, "aus"), "text: a state widget finds a value of a kind that is none of the enum under off");
	make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
	set_limit(&item.warn_lo, 0);
	set_limit(&item.crit_hi, 1);
	check(layout_item_level(&item, &number_value) == 1, "text: such a value has the level of the number 0");

	make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
	check(in_child(number_without_value), "text: false and an empty text without a value");
	check(in_child(number_without_value_and_room), "text: without a value and with little or no room nothing is written outside");
	check(text_sized(&item, number(1234), 5, true, "1234"), "text: a text of four bytes fits into five");
	check(text_sized(&item, number(1234), 4, false, ""), "text: it does not fit into four: false and an empty text");
	check(text_sized(&item, number(1234), 1, false, "") && text_sized(&item, number(1234), 0, false, ""), "text: with room for one byte or none nothing is written outside");
	check(shows_nothing(&item, number(1e12)) && shows_nothing(&item, number(INFINITY)) && shows_nothing(&item, number(-INFINITY)) && shows_nothing(&item, number(NAN)),
	      "text: false and an empty text for a value fmt_number() refuses");
	check(shows_text(&item, number(999999999999.0), "999999999999"), "text: the largest whole number fmt_number() prints");
	make_item(LAYOUT_WIDGET_NUMBER, 4, 1);
	check(shows_text(&item, number(1.23456), "1,235"), "text: an item with four decimals, which no layout that was read has, is shown with three as fmt_number() does");
	item.decimals = 6;
	check(shows_text(&item, number(1.23456), "1,235"), "text: likewise with six decimals");
	item.decimals = 255;
	check(shows_text(&item, number(-0.5), "-0,500"), "text: likewise with 255 decimals");
	make_item(LAYOUT_WIDGET_NUMBER, 0, 1000);
	check(shows_text(&item, number(999999999), "999999999000") && shows_nothing(&item, number(1e9)), "text: what counts for fmt_number() is the value times the scale");
	make_item(LAYOUT_WIDGET_NUMBER, 0, 1e300);
	check(shows_nothing(&item, number(1e300)) && shows_nothing(&item, number(-1e300)) && shows_text(&item, number(0), "0"), "text: a product that is not finite is refused");
	make_item(LAYOUT_WIDGET_NUMBER, 0, 0.001);
	check(shows_text(&item, number(1e12), "1000000000"), "text: a value fmt_number() would refuse is shown if the scale brings it into its range");

	// Any value, scale and count of decimals
	random_seed(20261006);
	for(round = 0; round < 20000; round++)
	{
		static const double scales[] = {1, 0.001, 0.1, 0.5, 2, 10, -1, -0.01, 1000};
		char wanted[ROOM];
		double value = (random_below(2000001) - 1000000) / (double[]){1, 4, 10, 1000}[random_below(4)];
		double scale = scales[random_below(9)];
		int decimals = random_below(4);

		make_item(widgets[random_below(3)], decimals, scale);
		if(!fmt_number(value * scale, decimals, wanted, sizeof(wanted)) || !shows_text(&item, number(value), wanted)) wrong++;
	}
	check(wrong == 0, "text: 20000 values with a scale and decimals at random are shown as fmt_number() prints their product");
}

static void test_item_text_states(void)
{
	char raw[32];
	int round, wrong = 0, wrong_next = 0;

	// The map of the header
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("1", "inaktiv");
	map_add("*", "aktiv");
	check(shows_text(&item, number(1), "inaktiv"), "state: the text of the map entry whose value is that of the item");
	check(shows_text(&item, number(0), "aktiv") && shows_text(&item, number(2), "aktiv") && shows_text(&item, number(-1), "aktiv") &&
	      shows_text(&item, number(11), "aktiv") && shows_text(&item, number(1.5), "aktiv") && shows_text(&item, number(1.001), "aktiv"),
	      "state: the text of the * entry for every other value");
	check(shows_text(&item, on(), "aktiv") && shows_text(&item, off(), "aktiv"), "state: on is not the number 1 for a map, it is the text on");
	check(in_child(state_without_value), "state: false and an empty text without a value, whatever the room");
	check(shows_text(&item, number(1e12), "aktiv") && shows_text(&item, number(INFINITY), "aktiv"), "state: a value that cannot be printed is one of the other values");

	// Without a * entry
	make_item(LAYOUT_WIDGET_STATE, 1, 1);
	map_add("1", "inaktiv");
	check(shows_text(&item, number(1), "inaktiv") && shows_text(&item, number(2), "2,0") && shows_text(&item, number(0), "0,0"),
	      "state: without a * entry every other value is shown as a number widget does, with the decimals of the item");
	check(shows_text(&item, on(), "1,0") && shows_text(&item, off(), "0,0"), "state: on and off are then shown as 1 and 0");
	check(shows_nothing(&item, number(1e12)) && shows_nothing(&item, number(NAN)), "state: a value that cannot be printed then gives false and an empty text");
	make_item(LAYOUT_WIDGET_STATE, 2, 10);
	map_add("5", "five");
	map_add("*", "other");
	check(shows_text(&item, number(5), "five") && shows_text(&item, number(50), "other") && shows_text(&item, number(0.5), "other") && shows_text(&item, on(), "other"),
	      "state: the * entry is found whatever the scale and the decimals of the item are");
	make_item(LAYOUT_WIDGET_STATE, 0, 10);
	map_add("5", "five");
	check(shows_text(&item, number(5), "five") && shows_text(&item, number(0.5), "5") && shows_text(&item, number(50), "500"),
	      "state: the map is asked for the value as it arrived, a number is shown times the scale");

	// on and off
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("on", "l\303\244uft");
	map_add("off", "steht");
	check(shows_text(&item, on(), "l\303\244uft") && shows_text(&item, off(), "steht"), "state: on and off are found under on and off");
	check(shows_text(&item, number(1), "1") && shows_text(&item, number(0), "0"), "state: the numbers 1 and 0 are not on and off");
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("1", "eins");
	map_add("0", "null");
	check(shows_text(&item, on(), "1") && shows_text(&item, off(), "0"), "state: on and off are not found under 1 and 0");
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("off", "aus");
	map_add("*", "sonst");
	check(shows_text(&item, off(), "aus") && shows_text(&item, on(), "sonst"), "state: on falls to the * entry if only off is named");

	// The order of the entries
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("*", "sonst");
	map_add("1", "eins");
	check(shows_text(&item, number(1), "eins") && shows_text(&item, number(2), "sonst"), "state: an entry for the value counts before the * entry, wherever that stands");
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("1", "first");
	map_add("1", "second");
	map_add("*", "star");
	map_add("*", "second star");
	check(shows_text(&item, number(1), "first") && shows_text(&item, number(2), "star"), "state: of two entries for the same value the first counts");
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("0", "a");
	map_add("1", "b");
	map_add("2", "c");
	map_add("3", "d");
	map_add("4", "e");
	map_add("5", "f");
	map_add("6", "g");
	map_add("7", "h");
	check(shows_text(&item, number(0), "a") && shows_text(&item, number(1), "b") && shows_text(&item, number(2), "c") && shows_text(&item, number(3), "d") &&
	      shows_text(&item, number(4), "e") && shows_text(&item, number(5), "f") && shows_text(&item, number(6), "g") && shows_text(&item, number(7), "h") &&
	      shows_text(&item, number(8), "8"), "state: each of eight entries is found, from the first to the last");
	item.map_count = 7;
	check(shows_text(&item, number(6), "g") && shows_text(&item, number(7), "7"), "state: an entry behind the last one of the map does not count");
	strcpy(item.map[7].raw, "*");
	check(shows_text(&item, number(9), "9"), "state: nor a * entry behind the last one");
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	check(shows_text(&item, number(5), "5") && shows_text(&item, on(), "1"), "state: a state widget without map entries shows the number");

	// Values are compared as a whole
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("10", "ten");
	map_add("on ", "on with a space");
	map_add("o", "o");
	map_add("", "empty");
	check(shows_text(&item, number(10), "ten") && shows_text(&item, number(1), "1") && shows_text(&item, number(100), "100") && shows_text(&item, number(0), "0"),
	      "state: 1 and 100 are not 10");
	check(shows_text(&item, on(), "1") && shows_text(&item, off(), "0"), "state: on is not the beginning of another value");
	check(shows_nothing(&item, number(INFINITY)) && shows_nothing(&item, number(1e12)), "state: a value that cannot be printed is not the empty value");
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("*", "");
	check(shows_text(&item, number(3), ""), "state: an empty map text is shown as an empty text, with true");

	// How a number is named
	check(hits(number(1), "1") && hits(number(0), "0") && hits(number(7), "7") && hits(number(-1), "-1") && hits(number(255), "255"),
	      "state: a whole number is named without decimals");
	check(hits(number(100), "100") && !hits(number(100), "1") && hits(number(10), "10") && hits(number(1000000), "1000000") && hits(number(-20), "-20"),
	      "state: zeros in front of the comma belong to the number");
	check(hits(number(0), "0") && hits(number(-0.0), "0"), "state: zero and minus zero are 0");
	check(hits(number(2.5), "2,5") && hits(number(-2.5), "-2,5") && hits(number(0.5), "0,5") && hits(number(10.5), "10,5"),
	      "state: a number with one decimal is named with a comma and that decimal");
	check(hits(number(2.25), "2,25") && hits(number(0.75), "0,75") && hits(number(100.25), "100,25"), "state: with two decimals");
	check(hits(number(2.125), "2,125") && hits(number(0.375), "0,375") && hits(number(-0.125), "-0,125"), "state: with three decimals");
	check(hits(number(2.05), "2,05") && hits(number(2.005), "2,005") && hits(number(20.5), "20,5") && hits(number(0.05), "0,05"),
	      "state: zeros between the comma and the last decimal stay");
	check(!hits(number(2.5), "2,50") && !hits(number(2.5), "2,500") && !hits(number(2.5), "2.5") && !hits(number(2), "2,") && !hits(number(2), "2,0") &&
	      !hits(number(2), "2,000"), "state: trailing zeros, a bare comma and a point are not how a number is named");
	check(hits(number(2.0004), "2") && hits(number(2.0006), "2,001") && hits(number(1.9996), "2") && hits(number(0.0625), "0,063"),
	      "state: a number is rounded to three decimals before it is named");
	check(hits(number(0.0004), "0") && hits(number(-0.0004), "0"), "state: a number that rounds to zero is 0, without sign");
	check(hits(number(12345678901.0), "12345678901") && hits(number(-1234567890.0), "-1234567890") && hits(number(-1234567.25), "-1234567,25") &&
	      hits(number(12345678.5), "12345678,5"), "state: numbers whose names fill the 11 bytes of a map value");
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("12345678901", "eleven digits");
	map_add("*", "other");
	check(shows_text(&item, number(123456789012.0), "other") && shows_text(&item, number(-999999999999.875), "other") &&
	      shows_text(&item, number(12345678901.5), "other"), "state: a number whose name is longer than a map value can be is one of the other values");

	// Room
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("1", "abcdefghijklmnopqrstuvw");
	map_add("*", "xyz");
	check(text_sized(&item, number(1), 24, true, "abcdefghijklmnopqrstuvw"), "state: a map text of 23 bytes fits into 24");
	check(text_sized(&item, number(1), 23, false, ""), "state: it does not fit into 23: false and an empty text, not the number");
	check(text_sized(&item, number(2), 4, true, "xyz") && text_sized(&item, number(2), 3, false, ""), "state: likewise the text of the * entry");
	check(text_sized(&item, number(1), 1, false, "") && text_sized(&item, number(1), 0, false, ""), "state: with room for one byte or none nothing is written outside");
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("*", "");
	check(text_sized(&item, number(1), 1, true, "") && text_sized(&item, number(1), 0, false, ""), "state: an empty map text fits into one byte, not into none");

	// Numbers of up to three decimals: found under the name the header describes, and under no other
	random_seed(20261007);
	for(round = 0; round < 20000; round++)
	{
		int64_t thousandths = (int64_t)random_below(20000001) - 10000000;
		int64_t magnitude;
		size_t length;

		if(random_below(3) == 0) thousandths = thousandths / 1000 * 1000;
		if(random_below(4) == 0) thousandths = thousandths / 100 * 100;
		magnitude = thousandths < 0 ? -thousandths : thousandths;
		// Written with three decimals, then the zeros at the end and a comma at the end taken away
		length = (size_t)snprintf(raw, sizeof(raw), "%s%lld,%03lld", thousandths < 0 ? "-" : "", (long long)(magnitude / 1000), (long long)(magnitude % 1000));
		while(raw[length - 1] == '0') raw[--length] = '\0';
		if(raw[length - 1] == ',') raw[--length] = '\0';

		if(!hits(number((double)thousandths / 1000.0), raw)) wrong++;
		if(hits(number((double)(thousandths + 1) / 1000.0), raw)) wrong_next++;
	}
	check(wrong == 0, "state: 20000 numbers of up to three decimals are found under their name: no decimals if there are none, else without trailing zeros");
	check(wrong_next == 0, "state: the number one thousandth above each of them is not found under that name");
}

// Every size of the buffer, for the texts of numbers and of maps
static void test_item_text_room(void)
{
	static const struct
	{
		layout_widget_t widget;
		int decimals;
		double scale;
		double number;
		const char *wanted;
		const char *what;
	} cases[] = {
		{LAYOUT_WIDGET_NUMBER, 0, 1, 7, "7", "room: a number of one digit with every size from 0 to 64: it needs two bytes"},
		{LAYOUT_WIDGET_NUMBER, 3, 1, 1234567, "1234567,000", "room: a number of 11 bytes with every size: it needs 12"},
		{LAYOUT_WIDGET_ARC, 2, -1, 7, "-7,00", "room: a negative number of an arc with every size: it needs 6 bytes"},
		{LAYOUT_WIDGET_BAR, 3, 1, -999999999999.0, "-999999999999,000", "room: the longest number of a bar, 17 bytes, with every size: it needs 18"},
		{LAYOUT_WIDGET_STATE, 1, 1, 1, "abcdefghijklmnopqrstuvw", "room: a map text of 23 bytes with every size: it needs 24"},
		{LAYOUT_WIDGET_STATE, 1, 1, 2, "", "room: an empty map text with every size: it needs one byte"},
		{LAYOUT_WIDGET_STATE, 1, 1, 3, "xyz", "room: the text of the * entry with every size: it needs four bytes"},
	};
	size_t i, size;

	for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		int wrong = 0;

		make_item(cases[i].widget, cases[i].decimals, cases[i].scale);
		map_add("1", "abcdefghijklmnopqrstuvw");
		map_add("2", "");
		map_add("*", "xyz");
		for(size = 0; size <= ROOM; size++)
		{
			if(!text_sized(&item, number(cases[i].number), size, size > strlen(cases[i].wanted), cases[i].wanted)) wrong++;
		}
		check(wrong == 0, cases[i].what);
	}

	// A state widget that falls back to the number
	make_item(LAYOUT_WIDGET_STATE, 2, 1);
	map_add("1", "eins");
	for(i = 0, size = 0; size <= ROOM; size++)
	{
		if(!text_sized(&item, number(12.5), size, size > 5, "12,50")) i++;
	}
	check(i == 0, "room: a state widget that shows the number with every size: 12,50 needs 6 bytes");
}

// The rule of layout_item_level a second time: the highest level of a limit the number has reached
static int model_level(const layout_item_t *of, double shown)
{
	const struct
	{
		const layout_limit_t *limit;
		int direction;
		int level;
	} limits[] = {
		{&of->warn_lo, -1, 1}, {&of->warn_hi, 1, 1}, {&of->crit_lo, -1, 2}, {&of->crit_hi, 1, 2},
	};
	int level = 0;
	int i;

	for(i = 0; i < 4; i++)
	{
		bool reached = limits[i].direction < 0 ? shown <= limits[i].limit->value : shown >= limits[i].limit->value;

		if(limits[i].limit->set && reached && limits[i].level > level) level = limits[i].level;
	}
	return level;
}

static void test_item_level(void)
{
	static const double candidates[] = {-1, 0, 1};
	static const double scales[] = {1, 2, 0.5, -1};
	static const double numbers[] = {-2, -1, -0.5, 0, 0.5, 1, 2};
	int code, s, n, k;
	int wrong = 0, tried = 0;

	make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
	check(layout_item_level(&item, number(5)) == 0 && layout_item_level(&item, number(-1e300)) == 0 && layout_item_level(&item, on()) == 0, "level: 0 without limits");

	set_limit(&item.warn_hi, 100);
	check(layout_item_level(&item, number(nextafter(100, 0))) == 0, "level: 0 just below the upper warn limit");
	check(layout_item_level(&item, number(100)) == 1, "level: 1 at the upper warn limit");
	check(layout_item_level(&item, number(nextafter(100, 200))) == 1 && layout_item_level(&item, number(1e300)) == 1, "level: 1 beyond the upper warn limit");
	check(layout_item_level(&item, number(-1e300)) == 0, "level: 0 far below it");

	make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
	set_limit(&item.warn_lo, 10);
	check(layout_item_level(&item, number(nextafter(10, 20))) == 0, "level: 0 just above the lower warn limit");
	check(layout_item_level(&item, number(10)) == 1, "level: 1 at the lower warn limit");
	check(layout_item_level(&item, number(nextafter(10, 0))) == 1 && layout_item_level(&item, number(-1e300)) == 1, "level: 1 beyond the lower warn limit");
	check(layout_item_level(&item, number(1e300)) == 0, "level: 0 far above it");

	make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
	set_limit(&item.crit_hi, 120);
	check(layout_item_level(&item, number(nextafter(120, 0))) == 0, "level: 0 just below the upper crit limit if there is no warn limit");
	check(layout_item_level(&item, number(120)) == 2, "level: 2 at the upper crit limit");
	check(layout_item_level(&item, number(nextafter(120, 200))) == 2, "level: 2 beyond the upper crit limit");

	make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
	set_limit(&item.crit_lo, 5);
	check(layout_item_level(&item, number(nextafter(5, 9))) == 0, "level: 0 just above the lower crit limit if there is no warn limit");
	check(layout_item_level(&item, number(5)) == 2, "level: 2 at the lower crit limit");
	check(layout_item_level(&item, number(nextafter(5, 0))) == 2, "level: 2 beyond the lower crit limit");

	make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
	set_limit(&item.warn_lo, 10);
	set_limit(&item.warn_hi, 100);
	set_limit(&item.crit_lo, 5);
	set_limit(&item.crit_hi, 120);
	check(layout_item_level(&item, number(50)) == 0 && layout_item_level(&item, number(nextafter(10, 20))) == 0 && layout_item_level(&item, number(nextafter(100, 0))) == 0,
	      "level: 0 between the warn limits");
	check(layout_item_level(&item, number(100)) == 1 && layout_item_level(&item, number(nextafter(120, 0))) == 1, "level: 1 from the upper warn limit to just below the crit limit");
	check(layout_item_level(&item, number(10)) == 1 && layout_item_level(&item, number(nextafter(5, 9))) == 1, "level: 1 from the lower warn limit to just above the crit limit");
	check(layout_item_level(&item, number(120)) == 2 && layout_item_level(&item, number(5)) == 2, "level: 2 at the crit limits, which lie beyond the warn limits");

	// A crit limit inside the warn limits
	make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
	set_limit(&item.warn_hi, 100);
	set_limit(&item.crit_hi, 50);
	check(layout_item_level(&item, number(60)) == 2 && layout_item_level(&item, number(100)) == 2 && layout_item_level(&item, number(49)) == 0,
	      "level: a crit limit that is reached counts, wherever the warn limit lies");

	// The scale
	make_item(LAYOUT_WIDGET_NUMBER, 0, 0.5);
	set_limit(&item.warn_hi, 100);
	set_limit(&item.crit_lo, -10);
	check(layout_item_level(&item, number(150)) == 0 && layout_item_level(&item, number(nextafter(200, 0))) == 0, "level: the scaled value is compared: 150 times 0.5 is below 100");
	check(layout_item_level(&item, number(200)) == 1, "level: 200 times 0.5 is at the limit 100");
	check(layout_item_level(&item, number(-15)) == 0 && layout_item_level(&item, number(-20)) == 2, "level: -20 times 0.5 is at the limit -10, -15 is not");
	make_item(LAYOUT_WIDGET_NUMBER, 0, 2);
	set_limit(&item.warn_hi, 100);
	check(layout_item_level(&item, number(49.5)) == 0 && layout_item_level(&item, number(50)) == 1 && layout_item_level(&item, number(99)) == 1,
	      "level: 50 times 2 is at the limit 100");
	make_item(LAYOUT_WIDGET_NUMBER, 0, -1);
	set_limit(&item.warn_hi, 100);
	check(layout_item_level(&item, number(-100)) == 1 && layout_item_level(&item, number(100)) == 0 && layout_item_level(&item, number(-99)) == 0,
	      "level: with a negative scale the upper limit is reached from below");

	// on and off
	make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
	set_limit(&item.warn_hi, 1);
	check(layout_item_level(&item, on()) == 1 && layout_item_level(&item, off()) == 0, "level: on is the number 1, off is below it");
	make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
	set_limit(&item.crit_lo, 0);
	check(layout_item_level(&item, off()) == 2 && layout_item_level(&item, on()) == 0, "level: off is the number 0, on is above it");
	make_item(LAYOUT_WIDGET_NUMBER, 0, 10);
	set_limit(&item.warn_hi, 10);
	set_limit(&item.crit_hi, 11);
	check(layout_item_level(&item, on()) == 1 && layout_item_level(&item, off()) == 0, "level: on and off are scaled as well");

	// No value
	check(in_child(level_without_value), "level: 0 without a value, also if the number 0 is beyond every limit");

	// A limit that is not set
	make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
	item.warn_lo.value = 5;
	item.warn_hi.value = 5;
	item.crit_lo.value = 5;
	item.crit_hi.value = 5;
	check(layout_item_level(&item, number(5)) == 0 && layout_item_level(&item, number(9)) == 0 && layout_item_level(&item, number(1)) == 0,
	      "level: a limit that is not set does not count, whatever its value");

	// Every widget
	make_item(LAYOUT_WIDGET_STATE, 0, 1);
	map_add("*", "x");
	set_limit(&item.crit_hi, 3);
	check(layout_item_level(&item, number(3)) == 2 && layout_item_level(&item, number(2)) == 0, "level: a state widget has levels as well");
	item.widget = LAYOUT_WIDGET_ARC;
	check(layout_item_level(&item, number(3)) == 2, "level: and an arc");
	item.widget = LAYOUT_WIDGET_BAR;
	check(layout_item_level(&item, number(3)) == 2, "level: and a bar");

	// Every combination of limits at -1, 0, 1 or not set, with four scales and nine values
	for(code = 0; code < 256; code++)
	{
		layout_limit_t *limits[4];

		make_item(LAYOUT_WIDGET_NUMBER, 0, 1);
		limits[0] = &item.warn_lo;
		limits[1] = &item.warn_hi;
		limits[2] = &item.crit_lo;
		limits[3] = &item.crit_hi;
		for(k = 0; k < 4; k++)
		{
			int choice = (code >> (2 * k)) & 3;

			// A limit that is not set keeps a value that would count
			if(choice < 3) set_limit(limits[k], candidates[choice]);
			else limits[k]->value = 0;
		}
		for(s = 0; s < 4; s++)
		{
			item.scale = scales[s];
			for(n = 0; n < 7; n++)
			{
				tried++;
				if(layout_item_level(&item, number(numbers[n])) != model_level(&item, numbers[n] * scales[s])) wrong++;
			}
			tried += 2;
			if(layout_item_level(&item, on()) != model_level(&item, scales[s])) wrong++;
			if(layout_item_level(&item, off()) != model_level(&item, 0 * scales[s])) wrong++;
		}
	}
	check(tried == 9216 && wrong == 0, "level: all 9216 combinations of four limits, four scales and nine values give the level of the model");

	// Limits, scale, widget and value at random
	random_seed(20261009);
	wrong = 0;
	for(code = 0; code < 20000; code++)
	{
		layout_limit_t *limits[4];
		double scale = (random_below(13) - 6) / 2.0;
		double value = (random_below(81) - 40) / 4.0;
		int kind = random_below(6);

		make_item((layout_widget_t)random_below(4), random_below(4), scale == 0 ? 1 : scale);
		limits[0] = &item.warn_lo;
		limits[1] = &item.warn_hi;
		limits[2] = &item.crit_lo;
		limits[3] = &item.crit_hi;
		for(k = 0; k < 4; k++)
		{
			// Most of them set. One that is not set keeps a value
			limits[k]->value = (random_below(41) - 20) / 2.0;
			limits[k]->set = random_below(4) > 0;
		}
		if(kind == 0) n = layout_item_level(&item, on()) != model_level(&item, item.scale);
		else if(kind == 1) n = layout_item_level(&item, off()) != model_level(&item, 0 * item.scale);
		else n = layout_item_level(&item, number(value)) != model_level(&item, value * item.scale);
		if(n) wrong++;
	}
	check(wrong == 0, "level: 20000 items with limits, scale, widget, decimals and value at random have the level of the model");
}

static void test_item_unit(void)
{
	layout_item_t *first_of_page;

	check(load_catalog("{'A':{'unit':'bar'},'B':{},'LONG':{'unit':'abcdefghijk'}}"), "unit: a catalogue with units");
	make_layout(1);
	first_of_page = add_item(0, "A");
	check(strcmp(layout_item_unit(first_of_page, &catalog), "bar") == 0, "unit: an item without a unit shows that of the catalogue");
	strcpy(first_of_page->unit, "kPa");
	check(strcmp(layout_item_unit(first_of_page, &catalog), "bar") == 0, "unit: what stands in the unit of the item does not count if it has none");
	first_of_page->has_unit = true;
	check(strcmp(layout_item_unit(first_of_page, &catalog), "kPa") == 0, "unit: an item with a unit shows its own, not that of the catalogue");
	check(layout_item_unit(first_of_page, &catalog) == first_of_page->unit, "unit: it is the text of the item itself");
	first_of_page->unit[0] = '\0';
	check(strcmp(layout_item_unit(first_of_page, &catalog), "") == 0, "unit: an item with an empty unit shows none, although the catalogue has one");

	make_layout(1);
	first_of_page = add_item(0, "B");
	check(strcmp(layout_item_unit(first_of_page, &catalog), "") == 0, "unit: empty if the catalogue has none for the key");
	make_layout(1);
	first_of_page = add_item(0, "X");
	check(strcmp(layout_item_unit(first_of_page, &catalog), "") == 0, "unit: empty if the key is not in the catalogue");
	first_of_page->has_unit = true;
	strcpy(first_of_page->unit, "V");
	check(strcmp(layout_item_unit(first_of_page, &catalog), "V") == 0, "unit: the unit of the item also if the key is not in the catalogue");
	make_layout(1);
	first_of_page = add_item(0, "@BATT_V");
	check(strcmp(layout_item_unit(first_of_page, &catalog), "V") == 0, "unit: the battery voltage has the unit V from the catalogue");
	make_layout(1);
	first_of_page = add_item(0, "LONG");
	check(strcmp(layout_item_unit(first_of_page, &catalog), "abcdefghijk") == 0, "unit: a unit of the catalogue of 11 bytes is passed on whole");
	make_layout(1);
	first_of_page = add_item(0, "a");
	check(strcmp(layout_item_unit(first_of_page, &catalog), "") == 0, "unit: names are compared exactly");
	catalog_init(&catalog);
	make_layout(1);
	first_of_page = add_item(0, "A");
	check(strcmp(layout_item_unit(first_of_page, &catalog), "") == 0, "unit: empty before the catalogue is loaded");

	check(load_catalog("{'A':{'unit':'bar'}}"), "unit: a catalogue with the unit bar");
	make_layout(1);
	first_of_page = add_item(0, "A");
	first_of_page->has_unit = true;
	{
		int length, wrong = 0;

		for(length = 0; length <= 8; length++)
		{
			strcpy(first_of_page->unit, letters(length));
			if(strcmp(layout_item_unit(first_of_page, &catalog), letters(length)) != 0) wrong++;
		}
		check(wrong == 0, "unit: an own unit of 0 to 8 bytes is shown whole, not that of the catalogue");
	}
}

static void test_item_state(void)
{
	layout_item_t *a, *b, *c, *x, *battery;

	make_layout(1);
	a = add_item(0, "A");
	b = add_item(0, "B");
	c = add_item(0, "C");
	x = add_item(0, "X");
	battery = add_item(0, "@BATT_V");

	check(load_catalog("{'A':{},'B':{},'C':{}}") && load_values("{'A':1,'B':'on','X':5}", 1000), "state of an item: a catalogue with A, B and C, values for A, B and X");

	check(layout_item_state(a, &catalog, &values, 1000) == LAYOUT_ITEM_LIVE, "state of an item: live when the value has just arrived");
	check(layout_item_state(a, &catalog, &values, 1000 + VALUE_FRESH_MS - 1) == LAYOUT_ITEM_LIVE, "state of an item: live one millisecond before it grows old");
	check(layout_item_state(a, &catalog, &values, 1000 + VALUE_FRESH_MS) == LAYOUT_ITEM_OLD, "state of an item: old after 3 s");
	check(layout_item_state(a, &catalog, &values, 1000 + VALUE_KEPT_MS - 1) == LAYOUT_ITEM_OLD, "state of an item: old one millisecond before it is gone");
	check(layout_item_state(a, &catalog, &values, 1000 + VALUE_KEPT_MS) == LAYOUT_ITEM_NO_VALUE, "state of an item: no value after 10 s, the key is in the catalogue");
	check(layout_item_state(a, &catalog, &values, UINT64_MAX) == LAYOUT_ITEM_NO_VALUE, "state of an item: no value at the end of time");
	check(layout_item_state(a, &catalog, &values, 0) == LAYOUT_ITEM_LIVE, "state of an item: live if the clock stepped back");
	check(layout_item_state(b, &catalog, &values, 1000) == LAYOUT_ITEM_LIVE && layout_item_state(b, &catalog, &values, 5000) == LAYOUT_ITEM_OLD,
	      "state of an item: a binary sensor is a value as well");
	check(layout_item_state(c, &catalog, &values, 1000) == LAYOUT_ITEM_NO_VALUE, "state of an item: no value if the key is in the catalogue and never arrived");
	check(layout_item_state(battery, &catalog, &values, 1000) == LAYOUT_ITEM_NO_VALUE, "state of an item: the battery voltage is in the catalogue");

	// A key the catalogue does not have
	check(layout_item_state(x, &catalog, &values, 1000) == LAYOUT_ITEM_LIVE && layout_item_state(x, &catalog, &values, 4000) == LAYOUT_ITEM_OLD,
	      "state of an item: a value that is there is live or old, also if its key is not in the catalogue");
	check(layout_item_state(x, &catalog, &values, 11000) == LAYOUT_ITEM_UNAVAILABLE, "state of an item: unavailable once it is gone and the key is not in the catalogue");
	x = add_item(0, "Y");
	check(layout_item_state(x, &catalog, &values, 1000) == LAYOUT_ITEM_UNAVAILABLE, "state of an item: unavailable if the key is not in the catalogue and never arrived");
	strcpy(x->key, "a");
	check(layout_item_state(x, &catalog, &values, 1000) == LAYOUT_ITEM_UNAVAILABLE, "state of an item: names are compared exactly");

	// Not loaded yet
	strcpy(x->key, "Y");
	catalog_init(&catalog);
	check(layout_item_state(x, &catalog, &values, 1000) == LAYOUT_ITEM_NO_VALUE && layout_item_state(c, &catalog, &values, 1000) == LAYOUT_ITEM_NO_VALUE,
	      "state of an item: no value instead of unavailable while the catalogue holds nothing but the battery");
	check(layout_item_state(a, &catalog, &values, 1000) == LAYOUT_ITEM_LIVE && layout_item_state(a, &catalog, &values, 4000) == LAYOUT_ITEM_OLD &&
	      layout_item_state(a, &catalog, &values, 11000) == LAYOUT_ITEM_NO_VALUE, "state of an item: live, old and no value then as well");
	memset(&catalog, 0, sizeof(catalog));
	check(layout_item_state(x, &catalog, &values, 1000) == LAYOUT_ITEM_NO_VALUE, "state of an item: no value with a catalogue without any entry");
	memset(&catalog, 0, sizeof(catalog));
	strcpy(catalog.entries[0].name, "A");
	catalog.count = 1;
	check(layout_item_state(x, &catalog, &values, 1000) == LAYOUT_ITEM_UNAVAILABLE && layout_item_state(battery, &catalog, &values, 1000) == LAYOUT_ITEM_UNAVAILABLE,
	      "state of an item: a catalogue with one entry that is not the battery is loaded");

	// No values at all
	check(load_catalog("{'A':{}}"), "state of an item: a catalogue with A");
	values_init(&values);
	check(layout_item_state(a, &catalog, &values, 0) == LAYOUT_ITEM_NO_VALUE && layout_item_state(b, &catalog, &values, 0) == LAYOUT_ITEM_UNAVAILABLE,
	      "state of an item: without any values the catalogue alone decides");

	// Keys, ages and catalogues at random
	random_seed(20261012);
	{
		static const uint64_t ages[] = {0, 1, VALUE_FRESH_MS - 1, VALUE_FRESH_MS, VALUE_FRESH_MS + 1, VALUE_KEPT_MS - 1, VALUE_KEPT_MS, VALUE_KEPT_MS + 1, 3600000};
		int round, k, wrong = 0;
		int states[4] = {0, 0, 0, 0};

		for(round = 0; round < 6000; round++)
		{
			layout_item_state_t wanted, got;
			char key[9];
			uint64_t seen = 5000 + (uint64_t)random_below(100000);
			uint64_t age = ages[random_below(9)];
			bool loaded = random_below(4) > 0;
			bool in_catalog = random_below(2) == 0;
			bool has_value = random_below(3) > 0;
			bool clock_back = random_below(8) == 0;
			int length = 1 + random_below(8);

			for(k = 0; k < length; k++) key[k] = (char)('A' + random_below(26));
			key[length] = '\0';
			make_layout(1);
			x = add_item(0, key);
			strcpy(x->label, "Label");

			catalog_init(&catalog);
			if(loaded) strcpy(catalog.entries[catalog.count++].name, "OTHER");
			if(in_catalog) strcpy(catalog.entries[catalog.count++].name, key);
			values_init(&values);
			strcpy(values.items[0].name, "ANOTHER");
			values.items[0].seen_ms = seen;
			values.count = 1;
			if(has_value)
			{
				strcpy(values.items[1].name, key);
				values.items[1].kind = random_below(3) == 0 ? VALUE_ON : VALUE_NUMBER;
				values.items[1].seen_ms = seen;
				values.count = 2;
			}

			// The rules of the header: a value that is there is live or old by its age, whatever the catalogue
			// says. Without one the key waits if the catalogue has it or is not loaded.
			if(has_value && (clock_back || age < VALUE_FRESH_MS)) wanted = LAYOUT_ITEM_LIVE;
			else if(has_value && age < VALUE_KEPT_MS) wanted = LAYOUT_ITEM_OLD;
			else if(in_catalog || !loaded) wanted = LAYOUT_ITEM_NO_VALUE;
			else wanted = LAYOUT_ITEM_UNAVAILABLE;

			got = layout_item_state(x, &catalog, &values, clock_back ? seen - 1 - (uint64_t)random_below(5000) : seen + age);
			if(got != wanted)
			{
				if(wrong++ == 0) printf("  round %d: key %s, state %d, expected %d\n", round, key, (int)got, (int)wanted);
			}
			states[wanted]++;
		}
		check(wrong == 0 && states[0] > 500 && states[1] > 500 && states[2] > 500 && states[3] > 300,
		      "state of an item: 6000 items with keys, ages and catalogues at random have the state of the rules, each state hundreds of times");
	}
}

// A layout of one page with the item K, built by hand
static layout_item_t *one_item(void)
{
	make_layout(1);
	return add_item(0, "K");
}

static void test_to_json_examples(void)
{
	static char file[4096], wanted[4096];
	layout_item_t *it;
	int length;

	one_item();
	check(writes(&made, "{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":[{\"items\":[{\"key\":\"K\"}]}]}"),
	      "to_json: the smallest layout is format, v, pages, items and key, without whitespace");
	check(write_all(&made) == 73 && written[73] == '\0' && written[72] == '}', "to_json: returns the length of the text, 73 bytes, and ends it with a zero");
	memcpy(&expected, &made, sizeof(made));
	write_all(&made);
	check(memcmp(&expected, &made, sizeof(made)) == 0, "to_json: does not change the layout");

	check(read_fixture("fixtures/layout_example.json", file, sizeof(file)) && accepted(file) &&
	      read_fixture("fixtures/layout_written_example.json", wanted, sizeof(wanted)), "to_json: the example of the header and its text as the display writes it are there");
	check(writes(&box.layout, wanted), "to_json: the example of the header is written on one line, without \"dec\":0 and with the members in their order");
	check(strlen(wanted) == 666 && comes_back(&box.layout), "to_json: the 666 bytes of the example are read back as the same layout");
	length = write_all(&kept);
	memcpy(file, written, sizeof(file));
	check(accepted(file) && writes(&box.layout, file) && length == 666, "to_json: a layout read from the text of the display is written as that text again");

	check(read_fixture("fixtures/layout_every_member.json", file, sizeof(file)) && accepted(file) &&
	      read_fixture("fixtures/layout_written_every_member.json", wanted, sizeof(wanted)), "to_json: the fixture with every member and its text as the display writes it are there");
	check(writes(&box.layout, wanted), "to_json: every member: escapes written out, defaults and unknown members gone, 22e-1 as 2.2 and -1E3 as -1000");
	check(comes_back(&box.layout), "to_json: the text of the fixture with every member is read back as the same layout");

	// One item with everything, in the order of the header
	it = one_item();
	strcpy(it->label, "L");
	strcpy(it->unit, "u");
	it->has_unit = true;
	it->scale = 0.5;
	it->decimals = 2;
	it->widget = LAYOUT_WIDGET_STATE;
	set_limit(&it->min, 1);
	set_limit(&it->max, 2);
	set_limit(&it->warn_lo, 3);
	set_limit(&it->warn_hi, 4);
	set_limit(&it->crit_lo, 5);
	set_limit(&it->crit_hi, 6);
	strcpy(it->map[0].raw, "a");
	strcpy(it->map[0].text, "b");
	strcpy(it->map[1].raw, "c");
	strcpy(it->map[1].text, "d");
	it->map_count = 2;
	strcpy(made.name, "N");
	strcpy(made.profile_hint, "H");
	strcpy(made.pages[0].title, "T");
	made.pages[0].hidden = true;
	check(writes(&made, q(W_HEAD "'name':'N','profile_hint':'H','pages':[{'title':'T','hidden':true,'items':[{'key':'K','label':'L','unit':'u',"
	                      "'scale':0.5,'dec':2,'widget':'state','min':1,'max':2,'warn_lo':3,'warn_hi':4,'crit_lo':5,'crit_hi':6,'map':{'a':'b','c':'d'}}]}]}")),
	      "to_json: every member of layout, page and item, each in its place: key, label, unit, scale, dec, widget, min, max, warn_lo, warn_hi, crit_lo, crit_hi, map");
	check(comes_back(&made), "to_json: a layout with every member is read back as the same layout");
}

static void test_to_json_layout_and_page(void)
{
	one_item();
	strcpy(made.name, "W906");
	check(writes(&made, q(W_HEAD "'name':'W906','pages':[{'items':[{'key':'K'}]}]}")), "to_json: a name is written behind v, a profile hint that is empty is not");
	one_item();
	strcpy(made.profile_hint, "OM651");
	check(writes(&made, q(W_HEAD "'profile_hint':'OM651','pages':[{'items':[{'key':'K'}]}]}")), "to_json: a profile hint is written, a name that is empty is not");
	strcpy(made.name, "W906");
	check(writes(&made, q(W_HEAD "'name':'W906','profile_hint':'OM651','pages':[{'items':[{'key':'K'}]}]}")), "to_json: the name stands before the profile hint");
	strcpy(made.name, "X");
	strcpy(made.profile_hint, "X");
	strcpy(made.pages[0].title, "X");
	strcpy(made.pages[0].items[0].key, "X");
	strcpy(made.pages[0].items[0].label, "X");
	check(writes(&made, q(W_HEAD "'name':'X','profile_hint':'X','pages':[{'title':'X','items':[{'key':'X','label':'X'}]}]}")),
	      "to_json: name, profile hint, title, key and label are each written also if they are the same text");

	one_item();
	strcpy(made.pages[0].title, "Motor");
	check(writes(&made, q(W_HEAD "'pages':[{'title':'Motor','items':[{'key':'K'}]}]}")), "to_json: a title is written before the items");
	one_item();
	made.pages[0].hidden = true;
	check(writes(&made, q(W_HEAD "'pages':[{'hidden':true,'items':[{'key':'K'}]}]}")), "to_json: a hidden page says so before its items, a title that is empty is not written");
	strcpy(made.pages[0].title, "Motor");
	check(writes(&made, q(W_HEAD "'pages':[{'title':'Motor','hidden':true,'items':[{'key':'K'}]}]}")), "to_json: the title stands before hidden");

	// Several items and pages
	make_layout(1);
	add_item(0, "A");
	add_item(0, "B");
	check(writes(&made, page_of("{'key':'A'},{'key':'B'}")), "to_json: two items are separated by a comma, in their order");
	add_item(0, "C");
	add_item(0, "D");
	add_item(0, "E");
	add_item(0, "F");
	check(writes(&made, page_of("{'key':'A'},{'key':'B'},{'key':'C'},{'key':'D'},{'key':'E'},{'key':'F'}")), "to_json: six items of a page");
	made.pages[0].item_count = 5;
	check(writes(&made, page_of("{'key':'A'},{'key':'B'},{'key':'C'},{'key':'D'},{'key':'E'}")), "to_json: an item behind the last one of its page is not written");

	make_layout(2);
	add_item(0, "A");
	add_item(1, "B");
	made.pages[1].hidden = true;
	strcpy(made.pages[1].title, "Zwei");
	check(writes(&made, q(W_HEAD "'pages':[{'items':[{'key':'A'}]},{'title':'Zwei','hidden':true,'items':[{'key':'B'}]}]}")),
	      "to_json: two pages are separated by a comma, each with its own title, hidden and items");
	add_item(2, "C");
	check(writes(&made, q(W_HEAD "'pages':[{'items':[{'key':'A'}]},{'title':'Zwei','hidden':true,'items':[{'key':'B'}]}]}")),
	      "to_json: a page behind the last one is not written");
	make_layout(12);
	add_item(0, "0");
	add_item(1, "1");
	add_item(2, "2");
	add_item(3, "3");
	add_item(4, "4");
	add_item(5, "5");
	add_item(6, "6");
	add_item(7, "7");
	add_item(8, "8");
	add_item(9, "9");
	add_item(10, "a");
	add_item(11, "b");
	check(writes(&made, q(W_HEAD "'pages':[{'items':[{'key':'0'}]},{'items':[{'key':'1'}]},{'items':[{'key':'2'}]},{'items':[{'key':'3'}]},"
	                      "{'items':[{'key':'4'}]},{'items':[{'key':'5'}]},{'items':[{'key':'6'}]},{'items':[{'key':'7'}]},{'items':[{'key':'8'}]},"
	                      "{'items':[{'key':'9'}]},{'items':[{'key':'a'}]},{'items':[{'key':'b'}]}]}")), "to_json: twelve pages");
	check(comes_back(&made), "to_json: twelve pages are read back as the same layout");

	// What layout_parse() would not take is written as it stands
	make_layout(0);
	add_item(0, "A");
	check(writes(&made, q(W_HEAD "'pages':[]}")) && refused(written, "pages", "empty"), "to_json: a layout without pages is written with an empty list, which layout_parse() refuses");
	make_layout(1);
	check(writes(&made, q(W_HEAD "'pages':[{'items':[]}]}")) && refused(written, "pages[0].items", "empty"),
	      "to_json: a page without items is written with an empty list, which layout_parse() refuses");
	make_layout(1);
	add_item(0, "");
	check(writes(&made, page_of("{'key':''}")) && refused(written, "pages[0].items[0].key", "empty"), "to_json: an empty key is written as it stands, which layout_parse() refuses");
}

static void test_to_json_items(void)
{
	layout_item_t *it;

	it = one_item();
	strcpy(it->label, "Drehzahl");
	check(writes(&made, page_of("{'key':'K','label':'Drehzahl'}")), "to_json: a label is written behind the key");

	it = one_item();
	strcpy(it->unit, "bar");
	it->has_unit = true;
	check(writes(&made, page_of("{'key':'K','unit':'bar'}")), "to_json: the unit of an item that has one");
	it = one_item();
	it->has_unit = true;
	check(writes(&made, page_of("{'key':'K','unit':''}")) && comes_back(&made), "to_json: an empty unit of an item that has one is written too, and read back as one");
	it = one_item();
	strcpy(it->unit, "bar");
	check(writes(&made, page_of("{'key':'K'}")), "to_json: what stands in the unit of an item that has none of its own is not written");

	it = one_item();
	it->scale = 0.001;
	check(writes(&made, page_of("{'key':'K','scale':0.001}")), "to_json: a scale that is not 1");
	it->scale = -1;
	check(writes(&made, page_of("{'key':'K','scale':-1}")), "to_json: the scale -1 is not the scale 1");
	it->scale = nextafter(1, 2);
	check(writes(&made, page_of("{'key':'K','scale':1.0000000000000002}")) && comes_back(&made), "to_json: the scale next to 1 is written with 17 digits and read back");
	it->scale = 0;
	check(writes(&made, page_of("{'key':'K','scale':0}")) && refused(written, "pages[0].items[0].scale", "zero"), "to_json: a scale of 0 is written as it stands, which layout_parse() refuses");

	it = one_item();
	it->decimals = 1;
	check(writes(&made, page_of("{'key':'K','dec':1}")), "to_json: one decimal");
	it->decimals = 3;
	check(writes(&made, page_of("{'key':'K','dec':3}")) && comes_back(&made), "to_json: three decimals");
	it->decimals = 4;
	check(writes(&made, page_of("{'key':'K','dec':4}")) && refused(written, "pages[0].items[0].dec", "not an integer 0 to 3"),
	      "to_json: four decimals are written as they stand, which layout_parse() refuses");
	it->decimals = 10;
	check(writes(&made, page_of("{'key':'K','dec':10}")), "to_json: a count of decimals of two digits, the tens first");
	it->decimals = 255;
	check(writes(&made, page_of("{'key':'K','dec':255}")), "to_json: the largest count of decimals a layout can hold");
	it->decimals = 120;
	check(writes(&made, page_of("{'key':'K','dec':120}")), "to_json: a count of decimals of three different digits");

	it = one_item();
	it->widget = LAYOUT_WIDGET_ARC;
	set_limit(&it->min, 0);
	set_limit(&it->max, 5000);
	check(writes(&made, page_of("{'key':'K','widget':'arc','min':0,'max':5000}")) && comes_back(&made), "to_json: an arc with its range");
	it->widget = LAYOUT_WIDGET_BAR;
	check(writes(&made, page_of("{'key':'K','widget':'bar','min':0,'max':5000}")) && comes_back(&made), "to_json: a bar with its range");
	it->widget = LAYOUT_WIDGET_NUMBER;
	check(writes(&made, page_of("{'key':'K','min':0,'max':5000}")) && comes_back(&made), "to_json: a number widget has no member widget and keeps its range");
	it = one_item();
	it->widget = LAYOUT_WIDGET_STATE;
	strcpy(it->map[0].raw, "1");
	strcpy(it->map[0].text, "an");
	it->map_count = 1;
	check(writes(&made, page_of("{'key':'K','widget':'state','map':{'1':'an'}}")) && comes_back(&made), "to_json: a state widget with its map");
	it->widget = LAYOUT_WIDGET_NUMBER;
	check(writes(&made, page_of("{'key':'K','map':{'1':'an'}}")) && comes_back(&made), "to_json: a number widget keeps its map");
	it->widget = (layout_widget_t)4;
	check(writes(&made, page_of("{'key':'K','map':{'1':'an'}}")), "to_json: the widget behind the last one of the enum is written like a number widget");
	it->widget = (layout_widget_t)255;
	check(writes(&made, page_of("{'key':'K','map':{'1':'an'}}")), "to_json: the widget 255 is written like a number widget");
	it->widget = (layout_widget_t)-1;
	check(writes(&made, page_of("{'key':'K','map':{'1':'an'}}")), "to_json: the widget -1 is written like a number widget");
	it = one_item();
	it->widget = LAYOUT_WIDGET_ARC;
	check(writes(&made, page_of("{'key':'K','widget':'arc'}")) && warned(written, 1, "pages[0].items[0].widget", "arc or bar without min and max, shown as number"),
	      "to_json: an arc without a range is written as it stands, which layout_parse() puts right with a warning");
	it->widget = LAYOUT_WIDGET_BAR;
	set_limit(&it->max, 9);
	check(writes(&made, page_of("{'key':'K','widget':'bar','max':9}")) && warned(written, 1, "pages[0].items[0].widget", "arc or bar without min and max, shown as number"),
	      "to_json: a bar with half a range is written as it stands");
	it = one_item();
	it->widget = LAYOUT_WIDGET_STATE;
	check(writes(&made, page_of("{'key':'K','widget':'state'}")) && warned(written, 1, "pages[0].items[0].widget", "state without map, shown as number"),
	      "to_json: a state widget without a map is written as it stands, which layout_parse() puts right with a warning");

	// Each limit alone, in its own member
	it = one_item();
	set_limit(&it->min, 3);
	check(writes(&made, page_of("{'key':'K','min':3}")) && comes_back(&made), "to_json: min alone");
	it = one_item();
	set_limit(&it->max, -3);
	check(writes(&made, page_of("{'key':'K','max':-3}")) && comes_back(&made), "to_json: max alone");
	it = one_item();
	set_limit(&it->warn_lo, 10);
	check(writes(&made, page_of("{'key':'K','warn_lo':10}")) && comes_back(&made), "to_json: warn_lo alone");
	it = one_item();
	set_limit(&it->warn_hi, 100);
	check(writes(&made, page_of("{'key':'K','warn_hi':100}")) && comes_back(&made), "to_json: warn_hi alone");
	it = one_item();
	set_limit(&it->crit_lo, 5);
	check(writes(&made, page_of("{'key':'K','crit_lo':5}")) && comes_back(&made), "to_json: crit_lo alone");
	it = one_item();
	set_limit(&it->crit_hi, 120);
	check(writes(&made, page_of("{'key':'K','crit_hi':120}")) && comes_back(&made), "to_json: crit_hi alone");
	it = one_item();
	set_limit(&it->min, 0);
	set_limit(&it->warn_lo, 0);
	set_limit(&it->crit_hi, 0);
	check(writes(&made, page_of("{'key':'K','min':0,'warn_lo':0,'crit_hi':0}")) && comes_back(&made), "to_json: a limit of 0 is a limit and is written");
	it = one_item();
	it->min.value = 1;
	it->max.value = 2;
	it->warn_lo.value = 3;
	it->warn_hi.value = 4;
	it->crit_lo.value = 5;
	it->crit_hi.value = 6;
	check(writes(&made, page_of("{'key':'K'}")), "to_json: a limit that is not set is not written, whatever its value");

	// Maps
	it = one_item();
	strcpy(it->map[0].raw, "1");
	strcpy(it->map[0].text, "an");
	check(writes(&made, page_of("{'key':'K'}")), "to_json: a map without entries is not written, whatever stands in its first entry");
	it = one_item();
	strcpy(it->map[0].raw, "");
	strcpy(it->map[0].text, "leer");
	strcpy(it->map[1].raw, "*");
	strcpy(it->map[1].text, "");
	strcpy(it->map[2].raw, "a");
	strcpy(it->map[2].text, "x");
	strcpy(it->map[3].raw, "a");
	strcpy(it->map[3].text, "y");
	it->map_count = 4;
	check(writes(&made, page_of("{'key':'K','map':{'':'leer','*':'','a':'x','a':'y'}}")) && comes_back(&made),
	      "to_json: a map with an empty value, an empty text and the same value twice, in its order");
	it->map_count = 3;
	check(writes(&made, page_of("{'key':'K','map':{'':'leer','*':'','a':'x'}}")), "to_json: an entry behind the last one of a map is not written");
	it = one_item();
	strcpy(it->map[0].raw, "0");
	strcpy(it->map[1].raw, "1");
	strcpy(it->map[2].raw, "2");
	strcpy(it->map[3].raw, "3");
	strcpy(it->map[4].raw, "4");
	strcpy(it->map[5].raw, "5");
	strcpy(it->map[6].raw, "6");
	strcpy(it->map[7].raw, "7");
	strcpy(it->map[0].text, "a");
	strcpy(it->map[1].text, "b");
	strcpy(it->map[2].text, "c");
	strcpy(it->map[3].text, "d");
	strcpy(it->map[4].text, "e");
	strcpy(it->map[5].text, "f");
	strcpy(it->map[6].text, "g");
	strcpy(it->map[7].text, "h");
	it->map_count = 8;
	it->widget = LAYOUT_WIDGET_STATE;
	check(writes(&made, page_of("{'key':'K','widget':'state','map':{'0':'a','1':'b','2':'c','3':'d','4':'e','5':'f','6':'g','7':'h'}}")) && comes_back(&made),
	      "to_json: a map of eight entries");

	// Every count of map entries
	{
		static char wanted[1024];
		int count, m, wrong = 0;
		size_t length;

		for(count = 0; count <= 8; count++)
		{
			it = one_item();
			length = (size_t)snprintf(wanted, sizeof(wanted), "{'key':'K'%s", count > 0 ? ",'map':{" : "");
			for(m = 0; m < count; m++)
			{
				snprintf(it->map[m].raw, sizeof(it->map[m].raw), "%d", m);
				snprintf(it->map[m].text, sizeof(it->map[m].text), "text %d", m);
				length += (size_t)snprintf(wanted + length, sizeof(wanted) - length, "%s'%d':'text %d'", m ? "," : "", m, m);
			}
			snprintf(wanted + length, sizeof(wanted) - length, "%s}", count > 0 ? "}" : "");
			it->map_count = (uint8_t)count;
			if(!written_is(&made, page_of(wanted)) || !comes_back(&made)) wrong++;
		}
		check(wrong == 0, "to_json: maps of 0 to 8 entries are written with each entry in its place and read back");
	}
}

static void test_to_json_texts(void)
{
	layout_item_t *it;

	// The same text, which JSON does not take as it is, in each of the eight places a layout has
	make_layout(1);
	it = add_item(0, "a\"b\\c\n");
	strcpy(made.name, "a\"b\\c\n");
	strcpy(made.profile_hint, "a\"b\\c\n");
	strcpy(made.pages[0].title, "a\"b\\c\n");
	strcpy(it->label, "a\"b\\c\n");
	strcpy(it->unit, "a\"b\\c\n");
	it->has_unit = true;
	strcpy(it->map[0].raw, "a\"b\\c\n");
	strcpy(it->map[0].text, "a\"b\\c\n");
	it->map_count = 1;
	check(writes(&made, "{\"format\":\"wican-display-layout\",\"v\":1,\"name\":\"a\\\"b\\\\c\\u000a\",\"profile_hint\":\"a\\\"b\\\\c\\u000a\",\"pages\":["
	                    "{\"title\":\"a\\\"b\\\\c\\u000a\",\"items\":[{\"key\":\"a\\\"b\\\\c\\u000a\",\"label\":\"a\\\"b\\\\c\\u000a\",\"unit\":\"a\\\"b\\\\c\\u000a\","
	                    "\"map\":{\"a\\\"b\\\\c\\u000a\":\"a\\\"b\\\\c\\u000a\"}}]}]}"),
	      "to_json: quote, backslash and line break are escaped in name, profile hint, title, key, label, unit, map value and map text");

	make_layout(1);
	add_item(0, "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f");
	check(writes(&made, "{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":[{\"items\":[{\"key\":\""
	                    "\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\u0008\\u0009\\u000a\\u000b\\u000c\\u000d\\u000e\\u000f"
	                    "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f\"}]}]}"),
	      "to_json: each of the bytes 0x01 to 0x1f becomes \\u00xx with lower case digits");
	check(refused(written, "pages[0].items[0].key", "control character"), "to_json: a key with control characters is written as JSON, which layout_parse() refuses");

	make_layout(1);
	add_item(0, " !#/[]~\x7f\x80\xc3\xa4\xff");
	check(writes(&made, "{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":[{\"items\":[{\"key\":\" !#/[]~\x7f\x80\xc3\xa4\xff\"}]}]}"),
	      "to_json: the bytes next to those that are escaped are passed on: space, !, #, slash, brackets, ~, 0x7f, 0x80 and above");
	make_layout(1);
	add_item(0, "\"\"\\\\");
	check(writes(&made, "{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":[{\"items\":[{\"key\":\"\\\"\\\"\\\\\\\\\"}]}]}") && comes_back(&made),
	      "to_json: a key of two quotes and two backslashes gets four backslashes more and is read back");
	make_layout(1);
	add_item(0, "\303\226l \342\202\254 \360\237\230\200");
	check(writes(&made, "{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":[{\"items\":[{\"key\":\"\303\226l \342\202\254 \360\237\230\200\"}]}]}") && comes_back(&made),
	      "to_json: characters of two, three and four bytes are passed on and read back");
	make_layout(1);
	add_item(0, "a\377\200b\303");
	check(comes_back(&made), "to_json: bytes that are no UTF-8 are read back as they are");

	// What stands behind the end of a text is not written
	make_layout(1);
	it = add_item(0, "K");
	memset(it->key + 2, 'x', sizeof(it->key) - 2);
	strcpy(it->label, "L");
	memset(it->label + 2, 'x', sizeof(it->label) - 2);
	strcpy(it->unit, "u");
	memset(it->unit + 2, 'x', sizeof(it->unit) - 2);
	it->has_unit = true;
	strcpy(it->map[0].raw, "r");
	memset(it->map[0].raw + 2, 'x', sizeof(it->map[0].raw) - 2);
	strcpy(it->map[0].text, "t");
	memset(it->map[0].text + 2, 'x', sizeof(it->map[0].text) - 2);
	it->map_count = 1;
	strcpy(made.name, "N");
	memset(made.name + 2, 'x', sizeof(made.name) - 2);
	strcpy(made.profile_hint, "H");
	memset(made.profile_hint + 2, 'x', sizeof(made.profile_hint) - 2);
	strcpy(made.pages[0].title, "T");
	memset(made.pages[0].title + 2, 'x', sizeof(made.pages[0].title) - 2);
	check(writes(&made, q(W_HEAD "'name':'N','profile_hint':'H','pages':[{'title':'T','items':[{'key':'K','label':'L','unit':'u','map':{'r':'t'}}]}]}")),
	      "to_json: what stands in a field behind the end of its text is not written");
	memset(made.name + 1, 'x', sizeof(made.name) - 1);
	memset(made.profile_hint + 1, 'x', sizeof(made.profile_hint) - 1);
	memset(made.pages[0].title + 1, 'x', sizeof(made.pages[0].title) - 1);
	memset(it->label + 1, 'x', sizeof(it->label) - 1);
	made.name[0] = '\0';
	made.profile_hint[0] = '\0';
	made.pages[0].title[0] = '\0';
	it->label[0] = '\0';
	check(writes(&made, page_of("{'key':'K','unit':'u','map':{'r':'t'}}")), "to_json: a text is empty if its first byte is zero, whatever follows");

	// Texts at their limits
	make_layout(1);
	it = add_item(0, letters(32));
	strcpy(made.name, letters(32));
	strcpy(made.profile_hint, letters(32));
	strcpy(made.pages[0].title, letters(24));
	strcpy(it->label, letters(24));
	strcpy(it->unit, letters(8));
	it->has_unit = true;
	strcpy(it->map[0].raw, letters(11));
	strcpy(it->map[0].text, letters(23));
	it->map_count = 1;
	check(writes(&made, q(W_HEAD "'name':'abcdefghijklmnopqrstuvwxyzabcdef','profile_hint':'abcdefghijklmnopqrstuvwxyzabcdef','pages':[{'title':'abcdefghijklmnopqrstuvwx',"
	                      "'items':[{'key':'abcdefghijklmnopqrstuvwxyzabcdef','label':'abcdefghijklmnopqrstuvwx','unit':'abcdefgh',"
	                      "'map':{'abcdefghijk':'abcdefghijklmnopqrstuvw'}}]}]}")) && comes_back(&made),
	      "to_json: every text at the limit of its field is written whole and read back");
}

// Bytes at random in a text field, of any length the field holds: letters, the bytes that are written with
// a backslash and their neighbours, and now and then any byte but zero
static void random_field(char *field, size_t size)
{
	static const char pool[] = "abcXYZ019 !\"#[\\]/~_@*,:{}\x7e\x80\xc3\xa4\xff";
	int length = random_below(5) == 0 ? (int)size - 1 : random_below((int)size);
	int i;

	memset(field, 0, size);
	if(random_below(8) == 0) length = 0;
	for(i = 0; i < length; i++) field[i] = random_below(12) == 0 ? (char)(1 + random_below(255)) : pool[random_below((int)sizeof(pool) - 1)];
}

// A text as layout_to_json() has to write it, with the escapes for every byte that needs one
static void build_bytes(builder_t *builder, const char *value)
{
	build_byte(builder, '"');
	for(; *value != '\0'; value++)
	{
		unsigned byte = (unsigned char)*value;

		if(byte <= 31) build(builder, "\\u%04x", byte);
		else if(byte == 34 || byte == 92) build(builder, "\\%c", (int)byte);
		else build_byte(builder, *value);
	}
	build_byte(builder, '"');
}

static bool has_control(const char *value)
{
	return value[strcspn(value, CONTROLS)] != '\0';
}

// Layouts whose texts are bytes at random: written as the rules of the header say, and read back if
// layout_parse() takes such texts
static void test_to_json_random_texts(void)
{
	static char wanted_text[4096];
	builder_t wanted;
	int round, wrong = 0, wrong_back = 0, back = 0, with_control = 0;

	random_seed(20261010);
	for(round = 0; round < 3000; round++)
	{
		layout_item_t *it = one_item();
		layout_page_t *page = &made.pages[0];
		bool readable;

		random_field(made.name, sizeof(made.name));
		random_field(made.profile_hint, sizeof(made.profile_hint));
		random_field(page->title, sizeof(page->title));
		page->hidden = random_below(3) == 0;
		random_field(it->key, sizeof(it->key));
		random_field(it->label, sizeof(it->label));
		random_field(it->unit, sizeof(it->unit));
		it->has_unit = random_below(2) == 0;
		it->map_count = (uint8_t)random_below(3);
		random_field(it->map[0].raw, sizeof(it->map[0].raw));
		random_field(it->map[0].text, sizeof(it->map[0].text));
		random_field(it->map[1].raw, sizeof(it->map[1].raw));
		random_field(it->map[1].text, sizeof(it->map[1].text));

		build_start(&wanted, wanted_text, sizeof(wanted_text));
		build(&wanted, "{\"format\":\"wican-display-layout\",\"v\":1");
		if(strlen(made.name) > 0)
		{
			build(&wanted, ",\"name\":");
			build_bytes(&wanted, made.name);
		}
		if(strlen(made.profile_hint) > 0)
		{
			build(&wanted, ",\"profile_hint\":");
			build_bytes(&wanted, made.profile_hint);
		}
		build(&wanted, ",\"pages\":[{");
		if(strlen(page->title) > 0)
		{
			build(&wanted, "\"title\":");
			build_bytes(&wanted, page->title);
			build(&wanted, ",");
		}
		build(&wanted, "%s\"items\":[{\"key\":", page->hidden ? "\"hidden\":true," : "");
		build_bytes(&wanted, it->key);
		if(strlen(it->label) > 0)
		{
			build(&wanted, ",\"label\":");
			build_bytes(&wanted, it->label);
		}
		if(it->has_unit)
		{
			build(&wanted, ",\"unit\":");
			build_bytes(&wanted, it->unit);
		}
		if(it->map_count > 0)
		{
			build(&wanted, ",\"map\":{");
			build_bytes(&wanted, it->map[0].raw);
			build(&wanted, ":");
			build_bytes(&wanted, it->map[0].text);
			if(it->map_count > 1)
			{
				build(&wanted, ",");
				build_bytes(&wanted, it->map[1].raw);
				build(&wanted, ":");
				build_bytes(&wanted, it->map[1].text);
			}
			build(&wanted, "}");
		}
		build(&wanted, "}]}]}");

		if(!written_is(&made, wanted_text) && wrong++ == 0) printf("  round %d: expected %.1500s\n  got      %.1500s\n", round, wanted_text, written);

		// What layout_parse() takes: no control character in any text that is written, and a key
		readable = strlen(it->key) > 0 && !has_control(made.name) && !has_control(made.profile_hint) && !has_control(page->title) && !has_control(it->key) &&
		           !has_control(it->label) && !(it->has_unit && has_control(it->unit));
		if(it->map_count > 0 && (has_control(it->map[0].raw) || has_control(it->map[0].text))) readable = false;
		if(it->map_count > 1 && (has_control(it->map[1].raw) || has_control(it->map[1].text))) readable = false;
		if(!readable)
		{
			with_control++;
			continue;
		}
		// What is not written does not come back: the unit of an item that has none, map entries behind the last
		if(!it->has_unit) memset(it->unit, 0, sizeof(it->unit));
		if(it->map_count < 2) memset(&it->map[1], 0, sizeof(it->map[1]));
		if(it->map_count < 1) memset(&it->map[0], 0, sizeof(it->map[0]));
		if(comes_back(&made)) back++;
		else wrong_back++;
	}
	check(wrong == 0, "to_json: 3000 layouts with texts of bytes at random are written with the escapes of the header, byte for byte");
	check(wrong_back == 0 && back > 600 && with_control > 600, "to_json: those of them without a control character are read back as the same layout");
}

static void test_to_json_numbers(void)
{
	static const struct
	{
		double number;
		const char *text;
		const char *what;
	} cases[] = {
		{0, "0", "0"},
		{-0.0, "-0", "minus zero keeps its sign"},
		{1, "1", "1"},
		{-1, "-1", "-1"},
		{5000, "5000", "a whole number has no point"},
		{0.5, "0.5", "a half"},
		{-2.5, "-2.5", "-2.5"},
		{0.001, "0.001", "a thousandth"},
		{0.1, "0.1", "a tenth is 0.1 and not the 17 digits 0.10000000000000001"},
		{0.3, "0.3", "0.3"},
		{2.2, "2.2", "2.2"},
		{4.35, "4.35", "4.35"},
		{0.07, "0.07", "0.07"},
		{123456.789, "123456.789", "nine digits"},
		{12345678.5, "12345678.5", "eight digits and a half"},
		{100000, "100000", "a hundred thousand is written out"},
		{1e14, "100000000000000", "1e14 has 15 digits and is written out"},
		{999999999999999.0, "999999999999999", "the largest whole number of 15 digits"},
		{1e15, "1e+15", "1e15 gets an exponent"},
		{1e16, "1e+16", "1e16"},
		{1e22, "1e+22", "1e22"},
		{1e23, "1e+23", "1e23, whose double lies below it"},
		{1e300, "1e+300", "1e300"},
		{0.0001, "0.0001", "a ten thousandth is written out"},
		{0.00001, "1e-05", "a hundred thousandth gets an exponent"},
		{-2.5e-7, "-2.5e-07", "a small negative number"},
		{0.1 + 0.2, "0.30000000000000004", "the sum of 0.1 and 0.2 is not 0.3 and needs 17 digits"},
		{1.0 / 3, "0.33333333333333331", "a third needs 17 digits"},
		{2.0 / 3, "0.66666666666666663", "two thirds need 17 digits"},
		{100.0 / 3, "33.333333333333336", "a hundred thirds"},
		{9007199254740992.0, "9007199254740992", "2^53 needs 16 digits and gets 17"},
		{123456789012345678.0, "1.2345678901234568e+17", "a whole number of 18 digits"},
		{DBL_MAX, "1.7976931348623157e+308", "the largest double"},
		{-DBL_MAX, "-1.7976931348623157e+308", "the smallest double, the longest text of 24 bytes"},
		{DBL_MIN, "2.2250738585072014e-308", "the smallest normal double"},
		{4.9406564584124654e-324, "4.94065645841247e-324", "the smallest double above zero comes back from 15 digits"},
	};
	static const double not_finite[] = {NAN, INFINITY, -INFINITY};
	static const char *const not_finite_names[] = {"not a number", "infinite", "minus infinite"};
	char what[160], wanted[128];
	layout_item_t *it;
	size_t i;
	int k, n, round;
	int wrong = 0, wrong_back = 0, short_ones = 0, long_ones = 0;

	for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		double back;

		it = one_item();
		set_limit(&it->crit_hi, cases[i].number);
		snprintf(wanted, sizeof(wanted), "{'key':'K','crit_hi':%s}", cases[i].text);
		snprintf(what, sizeof(what), "to_json: number: %s", cases[i].what);
		// The text is the one of the table, and the C library reads the table's text as the number of the table
		back = strtod(cases[i].text, NULL);
		check(writes(&made, page_of(wanted)) && memcmp(&back, &cases[i].number, sizeof(back)) == 0, what);
	}
	check(sizeof(cases) / sizeof(cases[0]) == 36, "to_json: number: 36 numbers with their texts");

	// Read back by layout_parse(), bit by bit
	it = one_item();
	set_limit(&it->warn_lo, -0.0);
	set_limit(&it->warn_hi, 0.1 + 0.2);
	set_limit(&it->crit_lo, 4.9406564584124654e-324);
	set_limit(&it->crit_hi, -DBL_MAX);
	it->scale = 1.0 / 3;
	memcpy(&expected, &made, sizeof(made));
	check(writes(&made, page_of("{'key':'K','scale':0.33333333333333331,'warn_lo':-0,'warn_hi':0.30000000000000004,'crit_lo':4.94065645841247e-324,"
	                            "'crit_hi':-1.7976931348623157e+308}")) && accepted(written) &&
	      memcmp(&first_item()->scale, &expected.pages[0].items[0].scale, sizeof(double)) == 0 &&
	      memcmp(&first_item()->warn_lo.value, &expected.pages[0].items[0].warn_lo.value, sizeof(double)) == 0 && signbit(first_item()->warn_lo.value) &&
	      memcmp(&first_item()->warn_hi.value, &expected.pages[0].items[0].warn_hi.value, sizeof(double)) == 0 &&
	      memcmp(&first_item()->crit_lo.value, &expected.pages[0].items[0].crit_lo.value, sizeof(double)) == 0 &&
	      memcmp(&first_item()->crit_hi.value, &expected.pages[0].items[0].crit_hi.value, sizeof(double)) == 0,
	      "to_json: number: layout_parse() reads the same doubles back, bit by bit, also minus zero and the ends of the range");

	// What is not finite has no text: in each of the seven places a number can stand
	for(n = 0; n < 3; n++)
	{
		int refused_places = 0;

		for(k = 0; k < 7; k++)
		{
			layout_limit_t *places[6];

			it = one_item();
			add_item(0, "LAST");
			places[0] = &it->min;
			places[1] = &it->max;
			places[2] = &it->warn_lo;
			places[3] = &it->warn_hi;
			places[4] = &it->crit_lo;
			places[5] = &it->crit_hi;
			if(k < 6) set_limit(places[k], not_finite[n]);
			else it->scale = not_finite[n];
			if(writes_nothing(&made)) refused_places++;
		}
		snprintf(what, sizeof(what), "to_json: number: %s as scale, min, max or one of the four limits: -1 and an empty text, however much room there is", not_finite_names[n]);
		check(refused_places == 7, what);
	}
	it = one_item();
	it->min.value = NAN;
	it->max.value = INFINITY;
	it->warn_lo.value = -INFINITY;
	it->warn_hi.value = NAN;
	it->crit_lo.value = INFINITY;
	it->crit_hi.value = NAN;
	check(writes(&made, page_of("{'key':'K'}")), "to_json: number: the value of a limit that is not set does not matter, also if it is not finite");
	make_layout(2);
	add_item(0, "A");
	it = add_item(1, "B");
	set_limit(&it->crit_hi, NAN);
	check(writes_nothing(&made), "to_json: number: one number that is not finite in the last item of the last page refuses the whole layout");
	check(write_watched(&made, 10, 200) == -1 && written[0] == '\0' && write_watched(&made, 0, 200) == -1 && (unsigned char)written[0] == WRITTEN_GUARD,
	      "to_json: number: likewise with little room and with none");
	for(k = 0, n = 0; n <= 300; n++)
	{
		if(write_watched(&made, (size_t)n, 300) != -1 || (n > 0 && written[0] != '\0')) k++;
	}
	check(k == 0, "to_json: number: with every size from 0 to 300, also those the rest of the text would fit into: -1 and an empty text");
	// The smallest layout with a number: without the number its text has 84 bytes
	it = one_item();
	set_limit(&it->crit_hi, INFINITY);
	for(k = 0, n = 0; n <= 200; n++)
	{
		if(write_watched(&made, (size_t)n, 200) != -1 || (n > 0 && written[0] != '\0')) k++;
	}
	check(k == 0, "to_json: number: likewise the smallest layout with a number, with every size from 0 to 200");
	set_limit(&it->crit_hi, 7);
	check(writes(&made, page_of("{'key':'K','crit_hi':7}")) && strlen(written) == 85, "to_json: number: with the number 7 in its place that layout has 85 bytes");
	make_layout(2);
	add_item(0, "A");
	it = add_item(1, "B");
	set_limit(&it->crit_hi, 7);
	check(writes(&made, q(W_HEAD "'pages':[{'items':[{'key':'A'}]},{'items':[{'key':'B','crit_hi':7}]}]}")) && strlen(written) == 109,
	      "to_json: number: after the number is put right the layout is written, in 109 bytes");

	// Any double: the text is that of the rule of the header, and it is read back as the same double
	random_seed(20261008);
	for(round = 0; round < 8000; round++)
	{
		char fifteen[40], seventeen[40];
		const char *rule;
		uint64_t bits = ((uint64_t)random_next() << 40) ^ ((uint64_t)random_next() << 20) ^ random_next();
		double number, back;

		switch(random_below(4))
		{
			// Any bit pattern
			case 0:  memcpy(&number, &bits, sizeof(number)); break;
			// A number as a human writes it: up to nine digits, up to four of them decimals
			case 1:  number = (double)(random_below(2000001) - 1000000) / (double[]){1, 10, 100, 1000, 10000}[random_below(5)]; break;
			// The result of a division
			case 2:  number = (double)(random_below(2001) - 1000) / (double)(1 + random_below(1000)); break;
			// Large and small
			default: number = (double)(random_below(2000001) - 1000000) * pow(10, random_below(601) - 300); break;
		}
		if(!isfinite(number)) continue;

		it = one_item();
		set_limit(&it->warn_hi, number);
		snprintf(fifteen, sizeof(fifteen), "%.15g", number);
		snprintf(seventeen, sizeof(seventeen), "%.17g", number);
		back = strtod(fifteen, NULL);
		rule = memcmp(&back, &number, sizeof(number)) == 0 ? fifteen : seventeen;
		if(rule == fifteen) short_ones++;
		else long_ones++;
		snprintf(wanted, sizeof(wanted), "{'key':'K','warn_hi':%s}", rule);
		if(!written_is(&made, page_of(wanted)) && wrong++ == 0) printf("  round %d: expected %s, got %.200s\n", round, rule, written);

		memcpy(&expected, &made, sizeof(made));
		if(!accepted(written) || memcmp(&first_item()->warn_hi.value, &expected.pages[0].items[0].warn_hi.value, sizeof(double)) != 0) wrong_back++;
	}
	check(wrong == 0 && short_ones > 2000 && long_ones > 2000,
	      "to_json: number: 8000 doubles are written with 15 digits if those are read back as the same double, else with 17");
	check(wrong_back == 0, "to_json: number: layout_parse() reads each of them back as the same double, bit by bit");
}

static bool written_without_buffer(void)
{
	one_item();
	return layout_to_json(&made, NULL, 0) == -1;
}

static void test_to_json_room(void)
{
	static char file[4096], wanted[4096];
	size_t length, size;
	int wrong = 0;

	check(read_fixture("fixtures/layout_example.json", file, sizeof(file)) && accepted(file) &&
	      read_fixture("fixtures/layout_written_example.json", wanted, sizeof(wanted)), "to_json: room: the example of the header is read");
	kept = box.layout;
	length = strlen(wanted);

	// Every size up to more than enough. Watched are all bytes from the end of the room to behind the whole text.
	for(size = 0; size <= length + 40; size++)
	{
		int result = write_watched(&kept, size, length + 40);

		if(size <= length ? result != -1 || (size > 0 && written[0] != '\0') : result != (int)length || strcmp(written, wanted) != 0)
		{
			if(wrong++ == 0) printf("  size %lu: %d\n", (unsigned long)size, result);
		}
	}
	check(length == 666 && wrong == 0,
	      "to_json: room: with every size from 0 to more than enough: the text and its length, or -1 and an empty text, and no byte behind the room is touched");

	check(write_watched(&kept, length + 1, length + 40) == (int)length && strcmp(written, wanted) == 0, "to_json: room: the text fits into its length and one byte");
	check(write_watched(&kept, length, length + 40) == -1 && written[0] == '\0', "to_json: room: with one byte less: -1 and an empty text");
	check(write_watched(&kept, 1, length + 40) == -1 && written[0] == '\0', "to_json: room: with one byte: -1 and an empty text");
	check(write_watched(&kept, 0, length + 40) == -1 && (unsigned char)written[0] == WRITTEN_GUARD, "to_json: room: with no room: -1 and nothing is written");
	check(in_child(written_without_buffer), "to_json: room: with no room and no buffer: -1");

	// Texts of every length from 73 to 139 bytes: a key of 1 to 32 bytes, with and without a label of 1 to 24
	wrong = 0;
	for(size = 1; size <= 32 + 24; size++)
	{
		size_t key_length = size <= 32 ? size : 32;
		size_t label_length = size <= 32 ? 0 : size - 32;
		layout_item_t *it;

		make_layout(1);
		it = add_item(0, letters((int)key_length));
		strcpy(it->label, letters((int)label_length));
		// 72 bytes around the key, 11 around a label
		length = 72 + key_length + (label_length > 0 ? 11 + label_length : 0);
		if(write_watched(&made, length + 1, length + 40) != (int)length || strlen(written) != length || written[length - 1] != '}') wrong++;
		if(write_watched(&made, length, length + 40) != -1 || written[0] != '\0') wrong++;
		if(write_watched(&made, length + 2, length + 40) != (int)length) wrong++;
	}
	check(wrong == 0, "to_json: room: texts of every length from 73 to 139 bytes need their length and one byte, no more and no less");
}

// The longest text there is: every text full of bytes that are written with six, every number the longest
static void test_to_json_largest(void)
{
	static const char control[] = "\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f\x1f";
	const double longest = -DBL_MAX;
	int p, i, m;
	int result;

	make_layout(12);
	strcpy(made.name, control);
	strcpy(made.profile_hint, control);
	for(p = 0; p < 12; p++)
	{
		memcpy(made.pages[p].title, control, 24);
		made.pages[p].hidden = true;
		for(i = 0; i < 6; i++)
		{
			layout_item_t *it = add_item(p, control);

			memcpy(it->label, control, 24);
			memcpy(it->unit, control, 8);
			it->has_unit = true;
			it->scale = longest;
			it->decimals = 255;
			it->widget = LAYOUT_WIDGET_STATE;
			set_limit(&it->min, longest);
			set_limit(&it->max, longest);
			set_limit(&it->warn_lo, longest);
			set_limit(&it->warn_hi, longest);
			set_limit(&it->crit_lo, longest);
			set_limit(&it->crit_hi, longest);
			for(m = 0; m < 8; m++)
			{
				memcpy(it->map[m].raw, control, 11);
				memcpy(it->map[m].text, control, 23);
			}
			it->map_count = 8;
		}
	}
	result = write_all(&made);
	// The layout: 38 bytes up to the version, name and profile hint with 8 and 16 bytes of member and 194 of
	// text each, 12 bytes around the pages and 11 commas between them.
	// A page: 9 + 146 for the title, a comma, 14 for hidden, 11 around the items and 5 commas between them.
	// An item: 7 + 194 for the key, 9 + 146 for the label, 8 + 50 for the unit, 9 + 24 for the scale, 7 + 3
	// for the decimals, 17 for the widget, 2 * (7 + 24) for the range, 4 * (11 + 24) for the limits, 9 around
	// the map, 8 * (68 + 1 + 140) for its entries with 7 commas, and a brace.
	check(result == 38 + 2 * 194 + 8 + 16 + 12 + 11 + 12 * (9 + 146 + 1 + 14 + 11 + 5) + 72 * (201 + 155 + 58 + 33 + 10 + 17 + 62 + 140 + 9 + 8 * 209 + 7 + 1) &&
	      result == 172985 && strlen(written) == 172985, "to_json: the longest text a layout can have, every byte of its texts written as six, has 172985 bytes");
	check(write_watched(&made, 172985, 172985) == -1 && written[0] == '\0' && write_watched(&made, 172986, 172986) == 172985,
	      "to_json: the longest text needs exactly its length and one byte");
	check(json_parse(written, 172985, work, LAYOUT_TOKENS) == 3191, "to_json: the longest text is JSON of 3191 tokens");
}

// LAYOUT_TEXT_MAX + 1 bytes are not enough for every layout layout_parse() accepts: numbers come back longer
static void test_to_json_longer_than_read(void)
{
	const layout_t *layout = &box.layout;
	int p, i, m;
	int result;

	// 72 items with seven numbers of four bytes each, 25 of them with a full map
	text_length = 0;
	emit("{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":[");
	for(p = 0; p < 12; p++)
	{
		emit("%s{\"items\":[", p ? "," : "");
		for(i = 0; i < 6; i++)
		{
			emit("%s{\"key\":\"K\",\"scale\":1e14,\"min\":1e14,\"max\":2e14,\"warn_lo\":1e14,\"warn_hi\":1e14,\"crit_lo\":1e14,\"crit_hi\":1e14", i ? "," : "");
			if(p * 6 + i < 25)
			{
				emit(",\"map\":{");
				for(m = 0; m < 8; m++) emit("%s\"%dbcdefghijk\":\"abcdefghijklmnopqrstuvw\"", m ? "," : "", m);
				emit("}");
			}
			emit("}");
		}
		emit("]}");
	}
	emit("]}");

	// 50 bytes around the pages, 12 bytes around the items of each page, 71 commas, 106 bytes for each item
	// and 328 for each map
	check(text_length == 50 + 12 * 12 + 71 + 72 * 106 + 25 * 328 && text_length == 16097 && text_length <= LAYOUT_TEXT_MAX,
	      "to_json: longer: a layout of 16097 bytes with 504 numbers that are written as 1e14 or 2e14");
	check(accepted(text) && layout->page_count == 12 && layout->pages[11].items[5].scale == 1e14 && layout->pages[11].items[5].max.value == 2e14,
	      "to_json: longer: layout_parse() accepts it");
	kept = *layout;
	// Each of the 504 numbers is written out with 15 digits: 11 bytes more
	result = write_watched(&kept, LAYOUT_TEXT_MAX + 1, 16097 + 504 * 11);
	check(result == -1 && written[0] == '\0', "to_json: longer: its text does not fit into LAYOUT_TEXT_MAX + 1 bytes");
	result = write_all(&kept);
	check(result == 16097 + 504 * 11 && result == 21641 && strstr(written, "{\"key\":\"K\",\"scale\":100000000000000,\"min\":100000000000000,\"max\":200000000000000,") != NULL,
	      "to_json: longer: with enough room it has 21641 bytes: every number came back 11 bytes longer than it was written");
	check(refused_length(written, 21641, "", "text too long"), "to_json: longer: layout_parse() refuses that text as too long");
}

static void test_to_json_default_layout(void)
{
	static const char begin[] = "{\"format\":\"wican-display-layout\",\"v\":1,\"name\":\"W906 OM651 Standard\",\"profile_hint\":\"W906\",\"pages\":["
	                            "{\"title\":\"Motor\",\"items\":[{\"key\":\"ENGINE_RPM\",\"label\":\"Drehzahl\",\"unit\":\"1/min\",\"widget\":\"arc\",\"min\":0,\"max\":5000},"
	                            "{\"key\":\"COOLANT_TMP\",\"label\":\"K\303\274hlwasser\",\"unit\":\"\302\260C\"},";
	static char file[LAYOUT_TEXT_MAX + 1];
	int length;

	check(read_fixture("../layouts/w906_default.json", file, sizeof(file)) && accepted(file), "to_json: default: the built-in layout is read");
	check(comes_back(&box.layout), "to_json: default: its text as the display writes it is read back as the same layout, without a warning");
	length = write_watched(&kept, LAYOUT_TEXT_MAX + 1, LAYOUT_TEXT_MAX + 1);
	check(length > 2000 && length < (int)strlen(file) && strncmp(written, begin, strlen(begin)) == 0,
	      "to_json: default: it fits into LAYOUT_TEXT_MAX + 1 bytes, is shorter than the file and begins with the engine speed");
}

int main(void)
{
	memset(work_between_guards, 0x5A, sizeof(work_between_guards));
	work_between_guards[0] = front_guard;
	work_between_guards[1] = front_guard;

	test_without_report();
	test_example();
	test_defaults();
	test_every_member();
	test_ignored_members();
	test_widgets();
	test_refused_text();
	test_missing_members();
	test_control_characters();
	test_surrogates();
	test_text_limits();
	test_utf8();
	test_counts();
	test_largest();
	test_text_length();
	test_stale_tokens();
	test_report();
	test_problem_order();
	test_warnings();
	test_page_shown();
	test_default_layout();
	test_page_steps();
	test_suits();
	test_from_catalog();
	test_from_catalog_names();
	test_item_text_numbers();
	test_item_text_states();
	test_item_text_room();
	test_item_level();
	test_item_unit();
	test_item_state();
	test_to_json_examples();
	test_to_json_layout_and_page();
	test_to_json_items();
	test_to_json_texts();
	test_to_json_random_texts();
	test_to_json_numbers();
	test_to_json_room();
	test_to_json_largest();
	test_to_json_longer_than_read();
	test_to_json_default_layout();

	// After the examples, so that a rule that is broken is named by its example first
	test_parse_model();
	test_cut_texts();

	// Last: it fills the room of the reader anew
	test_tokens();
	check(work[LAYOUT_TOKENS].start == 0x5A5A5A5A && work[LAYOUT_TOKENS + 1].start == 0x5A5A5A5A &&
	      memcmp(&work_between_guards[0], &front_guard, sizeof(front_guard)) == 0 && memcmp(&work_between_guards[1], &front_guard, sizeof(front_guard)) == 0,
	      "no call wrote before or behind the LAYOUT_TOKENS tokens it was given");
	return test_end();
}
