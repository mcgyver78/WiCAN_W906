/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "layout.h"
#include "fmt.h"

// Of layout_from_catalog()
#define ITEMS_PER_PAGE  4

// Room for the longest text of the format: a key, a name or a profile hint
#define TEXT_SIZE       (VALUE_NAME_SIZE > LAYOUT_NAME_SIZE ? VALUE_NAME_SIZE : LAYOUT_NAME_SIZE)

// Of layout_to_json()
typedef struct
{
	char *out;
	size_t size;
	size_t length;      // of the whole text, also when it does not fit
	bool failed;        // a number has no text that is read back as itself
} writer_t;

typedef struct
{
	const char *json;
	const json_token_t *tokens;
	layout_report_t *report;    // NULL in the run that fills the layout: the run before it has said everything
	// Where the reader is, for the report: -1 outside of the pages, of the items of a page, of the entries of
	// a map. Small numbers, so that the compiler sees that the place fits into the path of the report.
	int8_t page;
	int8_t item;
	int8_t entry;
} reader_t;

// "pages[2].items[0].min" for `member` "min" of the item the reader is at. An empty `member` means the
// page or the item itself, or the whole text. An entry of a map is named by its position.
static void write_path(const reader_t *r, const char *member, char *path, size_t size)
{
	const char *dot = member[0] != '\0' ? "." : "";

	if(r->page < 0) snprintf(path, size, "%s", member);
	else if(r->item < 0) snprintf(path, size, "pages[%d]%s%s", r->page, dot, member);
	else if(r->entry < 0) snprintf(path, size, "pages[%d].items[%d]%s%s", r->page, r->item, dot, member);
	else snprintf(path, size, "pages[%d].items[%d].map[%d]", r->page, r->item, r->entry);
}

// Always false, so that a check can return with it
static bool refuse(const reader_t *r, const char *member, const char *problem)
{
	if(r->report != NULL)
	{
		write_path(r, member, r->report->path, sizeof(r->report->path));
		snprintf(r->report->problem, sizeof(r->report->problem), "%s", problem);
	}
	return false;
}

static void warn(const reader_t *r, const char *member, const char *warning)
{
	if(r->report == NULL) return;

	// Only the first one is told, the others are counted
	if(r->report->warnings++ > 0) return;
	write_path(r, member, r->report->warning_path, sizeof(r->report->warning_path));
	snprintf(r->report->warning, sizeof(r->report->warning), "%s", warning);
}

static int member(const reader_t *r, int object, const char *key)
{
	return json_member(r->json, r->tokens, object, key);
}

// Text of a string token in `out`, which has room for TEXT_SIZE bytes of which `size` may be used.
// Returns what is wrong with it, NULL if nothing. The text is taken character by character: json_text()
// does not tell a text that is too long from one it refuses for a \u0000 or half a surrogate pair.
static const char *text_of(const reader_t *r, int index, char *out, size_t size)
{
	const json_token_t *token = &r->tokens[index];
	const char *s = r->json + token->start;
	json_token_t part = *token;
	size_t length = 0;

	out[0] = '\0';
	for(uint32_t pos = 0; pos < token->length; pos += part.length)
	{
		char bytes[5];
		size_t count;

		// One byte or one escape
		part.start = token->start + pos;
		part.length = s[pos] != '\\' ? 1 : s[pos + 1] != 'u' ? 2 : 6;
		if(!json_text(r->json, &part, bytes, sizeof(bytes)))
		{
			if(memcmp(&s[pos], "\\u0000", 6) == 0) return "control character";
			// The first half of a surrogate pair: the second one has to follow at once. If the text ends
			// here json_text() finds its closing quote in place of the backslash.
			part.length = 12;
			if(!json_text(r->json, &part, bytes, sizeof(bytes))) return "half a surrogate pair";
		}
		// Control characters are single bytes, every byte of a longer character is 0x80 or above
		if((unsigned char)bytes[0] < 0x20 || bytes[0] == 0x7F) return "control character";

		count = strlen(bytes);
		if(length + count + 1 > size) return "too long";
		memcpy(&out[length], bytes, count + 1);
		length += count;
	}
	return NULL;
}

// The optional text `key` of an object. out may be NULL; without the member it stays as it is, which is empty.
static bool read_text(const reader_t *r, int object, const char *key, char *out, size_t size)
{
	int index = member(r, object, key);
	char text[TEXT_SIZE];
	const char *problem;

	if(index < 0) return true;
	if(r->tokens[index].type != JSON_STRING) return refuse(r, key, "not a text");
	problem = text_of(r, index, text, size);
	if(problem != NULL) return refuse(r, key, problem);
	if(out != NULL) strcpy(out, text);
	return true;
}

// The optional number `key` of an object
static bool read_number(const reader_t *r, int object, const char *key, layout_limit_t *number)
{
	int index = member(r, object, key);

	number->set = index >= 0;
	number->value = 0;
	if(index < 0) return true;
	if(!json_number(r->json, &r->tokens[index], &number->value)) return refuse(r, key, "not a number");
	// "1e999" is a number for the JSON reader
	if(!isfinite(number->value)) return refuse(r, key, "not finite");
	return true;
}

// The texts of a state widget, {"1":"inaktiv","*":"aktiv"}. object is -1 if the item has none; map may be NULL.
static bool read_map(reader_t *r, int object, layout_map_t *map, uint8_t *count)
{
	int key = object + 1;

	*count = 0;
	if(object < 0) return true;
	if(r->tokens[object].type != JSON_OBJECT) return refuse(r, "map", "not an object");
	if(r->tokens[object].size > LAYOUT_MAP_MAX) return refuse(r, "map", "too many entries");

	// A text has no tokens inside: the next key follows two tokens later
	for(r->entry = 0; r->entry < r->tokens[object].size; r->entry++, key += 2)
	{
		char text[TEXT_SIZE];
		const char *problem = text_of(r, key, text, LAYOUT_MAP_RAW_SIZE);

		if(problem != NULL) return refuse(r, "", problem);
		if(map != NULL) strcpy(map[r->entry].raw, text);

		if(r->tokens[key + 1].type != JSON_STRING) return refuse(r, "", "not a text");
		problem = text_of(r, key + 1, text, LAYOUT_MAP_TEXT_SIZE);
		if(problem != NULL) return refuse(r, "", problem);
		if(map != NULL) strcpy(map[r->entry].text, text);
	}
	r->entry = -1;
	*count = (uint8_t)r->tokens[object].size;
	return true;
}

// item may be NULL
static bool read_item(reader_t *r, int object, layout_item_t *item)
{
	const json_token_t *tokens = r->tokens;
	layout_limit_t scale, min, max, warn_lo, warn_hi, crit_lo, crit_hi;
	layout_widget_t widget = LAYOUT_WIDGET_NUMBER;
	int64_t decimals = 0;
	uint8_t map_count;
	int index;

	if(tokens[object].type != JSON_OBJECT) return refuse(r, "", "not an object");

	index = member(r, object, "key");
	if(index < 0) return refuse(r, "key", "missing");
	// Every escape stands for at least one byte: a text is empty only if nothing stands between its quotes
	if(tokens[index].type == JSON_STRING && tokens[index].length == 0) return refuse(r, "key", "empty");
	if(!read_text(r, object, "key", item != NULL ? item->key : NULL, VALUE_NAME_SIZE)) return false;
	if(!read_text(r, object, "label", item != NULL ? item->label : NULL, LAYOUT_TITLE_SIZE)) return false;
	if(!read_text(r, object, "unit", item != NULL ? item->unit : NULL, LAYOUT_UNIT_SIZE)) return false;

	index = member(r, object, "dec");
	if(index >= 0 && (!json_integer(r->json, &tokens[index], &decimals) || decimals < 0 || decimals > 3))
	{
		return refuse(r, "dec", "not an integer 0 to 3");
	}

	index = member(r, object, "widget");
	if(index >= 0)
	{
		if(tokens[index].type != JSON_STRING) return refuse(r, "widget", "not a text");
		if(json_text_is(r->json, &tokens[index], "arc")) widget = LAYOUT_WIDGET_ARC;
		else if(json_text_is(r->json, &tokens[index], "bar")) widget = LAYOUT_WIDGET_BAR;
		else if(json_text_is(r->json, &tokens[index], "state")) widget = LAYOUT_WIDGET_STATE;
		else if(!json_text_is(r->json, &tokens[index], "number")) warn(r, "widget", "unknown widget, shown as number");
	}

	if(!read_number(r, object, "scale", &scale)) return false;
	if(scale.set && scale.value == 0) return refuse(r, "scale", "zero");
	if(!read_number(r, object, "min", &min) || !read_number(r, object, "max", &max)) return false;
	if(min.set && max.set && !(min.value < max.value)) return refuse(r, "min", "min is not below max");
	if(!read_number(r, object, "warn_lo", &warn_lo) || !read_number(r, object, "warn_hi", &warn_hi)) return false;
	if(!read_number(r, object, "crit_lo", &crit_lo) || !read_number(r, object, "crit_hi", &crit_hi)) return false;

	if(!read_map(r, member(r, object, "map"), item != NULL ? item->map : NULL, &map_count)) return false;

	if((widget == LAYOUT_WIDGET_ARC || widget == LAYOUT_WIDGET_BAR) && !(min.set && max.set))
	{
		warn(r, "widget", "arc or bar without min and max, shown as number");
		widget = LAYOUT_WIDGET_NUMBER;
	}
	if(widget == LAYOUT_WIDGET_STATE && map_count == 0)
	{
		warn(r, "widget", "state without map, shown as number");
		widget = LAYOUT_WIDGET_NUMBER;
	}

	if(item != NULL)
	{
		item->has_unit = member(r, object, "unit") >= 0;
		item->decimals = (uint8_t)decimals;
		item->widget = widget;
		item->scale = scale.set ? scale.value : 1;
		item->min = min;
		item->max = max;
		item->warn_lo = warn_lo;
		item->warn_hi = warn_hi;
		item->crit_lo = crit_lo;
		item->crit_hi = crit_hi;
		item->map_count = map_count;
	}
	return true;
}

// page may be NULL
static bool read_page(reader_t *r, int object, layout_page_t *page)
{
	const json_token_t *tokens = r->tokens;
	int hidden = member(r, object, "hidden");
	int items = member(r, object, "items");
	int index = items + 1;

	if(tokens[object].type != JSON_OBJECT) return refuse(r, "", "not an object");
	if(!read_text(r, object, "title", page != NULL ? page->title : NULL, LAYOUT_TITLE_SIZE)) return false;
	if(hidden >= 0 && tokens[hidden].type != JSON_TRUE && tokens[hidden].type != JSON_FALSE)
	{
		return refuse(r, "hidden", "not a boolean");
	}
	if(items < 0) return refuse(r, "items", "missing");
	if(tokens[items].type != JSON_ARRAY) return refuse(r, "items", "not an array");
	if(tokens[items].size == 0) return refuse(r, "items", "empty");
	if(tokens[items].size > LAYOUT_ITEMS_MAX) return refuse(r, "items", "too many items");

	for(r->item = 0; r->item < tokens[items].size; r->item++)
	{
		if(!read_item(r, index, page != NULL ? &page->items[r->item] : NULL)) return false;
		index += tokens[index].skip;
	}
	r->item = -1;

	if(page != NULL)
	{
		page->hidden = hidden >= 0 && tokens[hidden].type == JSON_TRUE;
		page->item_count = (uint8_t)tokens[items].size;
	}
	return true;
}

// The whole text, which is a JSON object. layout may be NULL: then the text is only checked.
static bool read_layout(reader_t *r, layout_t *layout)
{
	const json_token_t *tokens = r->tokens;
	int format = member(r, 0, "format");
	int version = member(r, 0, "v");
	int pages = member(r, 0, "pages");
	int index = pages + 1;
	int64_t number;

	if(format < 0 || !json_text_is(r->json, &tokens[format], LAYOUT_FORMAT)) return refuse(r, "format", "not " LAYOUT_FORMAT);
	if(version < 0 || !json_integer(r->json, &tokens[version], &number)) return refuse(r, "v", "not an integer");
	if(number > LAYOUT_VERSION) return refuse(r, "v", "layout of a newer display");
	if(number < 1) return refuse(r, "v", "below 1");
	if(!read_text(r, 0, "name", layout != NULL ? layout->name : NULL, LAYOUT_NAME_SIZE)) return false;
	if(!read_text(r, 0, "profile_hint", layout != NULL ? layout->profile_hint : NULL, LAYOUT_NAME_SIZE)) return false;

	if(pages < 0) return refuse(r, "pages", "missing");
	if(tokens[pages].type != JSON_ARRAY) return refuse(r, "pages", "not an array");
	if(tokens[pages].size == 0) return refuse(r, "pages", "empty");
	if(tokens[pages].size > LAYOUT_PAGES_MAX) return refuse(r, "pages", "too many pages");

	for(r->page = 0; r->page < tokens[pages].size; r->page++)
	{
		if(!read_page(r, index, layout != NULL ? &layout->pages[r->page] : NULL)) return false;
		index += tokens[index].skip;
	}

	if(layout != NULL) layout->page_count = (uint8_t)tokens[pages].size;
	return true;
}

bool layout_parse(const char *json, size_t length, layout_t *layout, layout_report_t *report,
                  json_token_t *work, int work_count)
{
	reader_t reader = {json, work, report, -1, -1, -1};

	if(report != NULL) memset(report, 0, sizeof(*report));

	if(length > LAYOUT_TEXT_MAX) return refuse(&reader, "", "text too long");
	if(json_parse(json, length, work, work_count) < 0) return refuse(&reader, "", "not valid JSON");
	if(work[0].type != JSON_OBJECT) return refuse(&reader, "", "not a JSON object");

	// *layout has to stay untouched if the text is refused, and there is no room for a second layout to
	// work in. So the text is read twice: checked first, taken over then.
	if(!read_layout(&reader, NULL))
	{
		// Nothing was accepted, so nothing was put right either
		if(report != NULL)
		{
			report->warnings = 0;
			report->warning_path[0] = '\0';
			report->warning[0] = '\0';
		}
		return false;
	}

	reader.report = NULL;
	memset(layout, 0, sizeof(*layout));
	read_layout(&reader, layout);
	return true;
}

// Not loaded yet: the catalogue holds nothing but the entry catalog_init() makes
static bool catalog_loaded(const catalog_t *catalog)
{
	for(int i = 0; i < catalog->count; i++)
	{
		if(strcmp(catalog->entries[i].name, CATALOG_BATTERY) != 0) return true;
	}
	return false;
}

bool layout_page_shown(const layout_t *layout, int page, const catalog_t *catalog)
{
	const layout_page_t *shown;

	if(page < 0 || page >= layout->page_count) return false;

	shown = &layout->pages[page];
	if(shown->hidden) return false;
	if(!catalog_loaded(catalog)) return true;

	for(int i = 0; i < shown->item_count; i++)
	{
		if(catalog_find(catalog, shown->items[i].key) >= 0) return true;
	}
	return false;
}

int layout_first_page(const layout_t *layout, const catalog_t *catalog)
{
	for(int i = 0; i < layout->page_count; i++)
	{
		if(layout_page_shown(layout, i, catalog)) return i;
	}
	return -1;
}

int layout_step_page(const layout_t *layout, const catalog_t *catalog, int page, int direction)
{
	int step = direction < 0 ? -1 : 1;

	// A page outside of the layout is not shown, wherever it is: the search starts next to the layout
	if(page < 0) page = -1;
	if(page > layout->page_count) page = layout->page_count;

	for(int i = page + step; i >= 0 && i < layout->page_count; i += step)
	{
		if(layout_page_shown(layout, i, catalog)) return i;
	}
	if(layout_page_shown(layout, page, catalog)) return page;
	for(int i = page - step; i >= 0 && i < layout->page_count; i -= step)
	{
		if(layout_page_shown(layout, i, catalog)) return i;
	}
	return -1;
}

// true if an item before this one, on this page or on one before, has the same key
static bool key_repeated(const layout_t *layout, int page, int item)
{
	const char *key = layout->pages[page].items[item].key;

	for(int p = 0; p <= page; p++)
	{
		int count = p < page ? layout->pages[p].item_count : item;

		for(int i = 0; i < count; i++)
		{
			if(strcmp(layout->pages[p].items[i].key, key) == 0) return true;
		}
	}
	return false;
}

bool layout_suits(const layout_t *layout, const catalog_t *catalog)
{
	int distinct = 0;
	int found = 0;

	if(!catalog_loaded(catalog)) return true;

	for(int p = 0; p < layout->page_count; p++)
	{
		for(int i = 0; i < layout->pages[p].item_count; i++)
		{
			if(key_repeated(layout, p, i)) continue;
			distinct++;
			if(catalog_find(catalog, layout->pages[p].items[i].key) >= 0) found++;
		}
	}
	return 2 * found >= distinct;
}

// true if layout_parse() takes the name as a key: it is not empty and has no control character. A layout
// with another key could be shown, but never be stored again once the editor in the browser has it.
static bool usable_key(const char *name)
{
	if(name[0] == '\0') return false;

	for(; *name != '\0'; name++)
	{
		unsigned char c = (unsigned char)*name;

		if(c < 0x20 || c == 0x7F) return false;
	}
	return true;
}

// Appends an entry of the catalogue to the last page, or to a new one. What finds no room is left out.
static void add_item(layout_t *layout, const catalog_entry_t *entry)
{
	layout_page_t *page;
	layout_item_t *item;

	if(!usable_key(entry->name)) return;

	if(layout->page_count == 0 || layout->pages[layout->page_count - 1].item_count == ITEMS_PER_PAGE)
	{
		if(layout->page_count == LAYOUT_PAGES_MAX) return;
		page = &layout->pages[layout->page_count++];
		snprintf(page->title, sizeof(page->title), "Werte %d", layout->page_count);
	}
	page = &layout->pages[layout->page_count - 1];

	// Everything else stays zero: widget number, the unit of the catalogue, no limits. A label that is
	// too long stays empty, also behind its first byte: fmt_label() leaves there what it had written.
	item = &page->items[page->item_count++];
	strcpy(item->key, entry->name);
	if(!fmt_label(entry->name, item->label, sizeof(item->label))) memset(item->label, 0, sizeof(item->label));
	item->decimals = 1;
	item->scale = 1;
}

void layout_from_catalog(const catalog_t *catalog, layout_t *layout)
{
	int battery = catalog_find(catalog, CATALOG_BATTERY);

	memset(layout, 0, sizeof(*layout));
	for(int i = 0; i < catalog->count; i++)
	{
		if(i != battery) add_item(layout, &catalog->entries[i]);
	}
	if(battery >= 0) add_item(layout, &catalog->entries[battery]);
}

layout_item_state_t layout_item_state(const layout_item_t *item, const catalog_t *catalog,
                                      const values_t *values, uint64_t now_ms)
{
	switch(values_age(values_find(values, item->key), now_ms))
	{
		case VALUE_AGE_FRESH: return LAYOUT_ITEM_LIVE;
		case VALUE_AGE_OLD:   return LAYOUT_ITEM_OLD;
		default:              break;
	}
	if(!catalog_loaded(catalog) || catalog_find(catalog, item->key) >= 0) return LAYOUT_ITEM_NO_VALUE;
	return LAYOUT_ITEM_UNAVAILABLE;
}

// A binary sensor counts as 1 or 0
static double number_of(const value_t *value)
{
	if(value->kind == VALUE_NUMBER) return value->number;
	return value->kind == VALUE_ON ? 1 : 0;
}

// The value as the text a map names it by: "on", "off", "1", "2,5". false if it is a number that cannot
// be printed.
static bool raw_text(const value_t *value, char *out, size_t size)
{
	size_t length;

	if(value->kind != VALUE_NUMBER)
	{
		strcpy(out, value->kind == VALUE_ON ? "on" : "off");
		return true;
	}
	if(!fmt_number(value->number, 3, out, size)) return false;

	// "2,500" becomes "2,5" and "1,000" becomes "1". The comma stops the loop: zeros in front of it stay.
	length = strlen(out);
	while(out[length - 1] == '0') length--;
	if(out[length - 1] == ',') length--;
	out[length] = '\0';
	return true;
}

static const layout_map_t *map_entry(const layout_item_t *item, const char *raw)
{
	for(int i = 0; i < item->map_count; i++)
	{
		if(strcmp(item->map[i].raw, raw) == 0) return &item->map[i];
	}
	return NULL;
}

bool layout_item_text(const layout_item_t *item, const value_t *value, char *out, size_t size)
{
	if(size == 0) return false;
	out[0] = '\0';
	if(value == NULL) return false;

	if(item->widget == LAYOUT_WIDGET_STATE)
	{
		// A number that needs more room than the longest map value with the ",000" it lost has no entry
		char raw[LAYOUT_MAP_RAW_SIZE + 4];
		const layout_map_t *entry = NULL;

		if(raw_text(value, raw, sizeof(raw))) entry = map_entry(item, raw);
		if(entry == NULL) entry = map_entry(item, "*");
		if(entry != NULL)
		{
			if(strlen(entry->text) + 1 > size) return false;
			strcpy(out, entry->text);
			return true;
		}
	}
	return fmt_number(number_of(value) * item->scale, item->decimals, out, size);
}

static bool beyond(double number, const layout_limit_t *low, const layout_limit_t *high)
{
	return (low->set && number <= low->value) || (high->set && number >= high->value);
}

int layout_item_level(const layout_item_t *item, const value_t *value)
{
	double number;

	if(value == NULL) return 0;

	number = number_of(value) * item->scale;
	if(beyond(number, &item->crit_lo, &item->crit_hi)) return 2;
	if(beyond(number, &item->warn_lo, &item->warn_hi)) return 1;
	return 0;
}

const char *layout_item_unit(const layout_item_t *item, const catalog_t *catalog)
{
	int index;

	if(item->has_unit) return item->unit;

	index = catalog_find(catalog, item->key);
	return index < 0 ? "" : catalog->entries[index].unit;
}

static void put_char(writer_t *writer, char c)
{
	// What does not fit is only counted
	if(writer->length < writer->size) writer->out[writer->length] = c;
	writer->length++;
}

static void put(writer_t *writer, const char *text)
{
	for(; *text != '\0'; text++) put_char(writer, *text);
}

static void put_string(writer_t *writer, const char *text)
{
	static const char hex[] = "0123456789abcdef";

	put_char(writer, '"');
	for(; *text != '\0'; text++)
	{
		unsigned char c = (unsigned char)*text;

		if(c < 0x20)
		{
			put(writer, "\\u00");
			put_char(writer, hex[c >> 4]);
			put_char(writer, hex[c & 0x0F]);
			continue;
		}
		if(c == '"' || c == '\\') put_char(writer, '\\');
		put_char(writer, *text);
	}
	put_char(writer, '"');
}

// A member that is a text. `member` is what stands before it: comma or brace, name and colon.
static void put_text(writer_t *writer, const char *member, const char *text)
{
	put(writer, member);
	put_string(writer, text);
}

// Decimals and the version of the format
static void put_small(writer_t *writer, uint8_t number)
{
	char reversed[3];
	int length = 0;

	do
	{
		reversed[length++] = (char)('0' + number % 10);
		number /= 10;
	}
	while(number > 0);
	while(length > 0) put_char(writer, reversed[--length]);
}

// true if the JSON reader takes the text for exactly this number. It does not for "inf" and "nan", which
// are no JSON.
static bool reads_back(const char *text, double number)
{
	json_token_t token;
	double back;

	return json_parse(text, strlen(text), &token, 1) == 1 && json_number(text, &token, &back) && back == number;
}

// 15 digits bring back every number that was written with up to 15, as it was written: 0.1 and not
// 0.10000000000000001. 17 digits bring back every double. Which of the two it is, and whether the printf
// of this C library got the last digit right, is decided by reading the text back.
static void put_number(writer_t *writer, double number)
{
	// The longest text is "-1.7976931348623157e+308"
	char text[32];

	snprintf(text, sizeof(text), "%.15g", number);
	if(!reads_back(text, number))
	{
		snprintf(text, sizeof(text), "%.17g", number);
		if(!reads_back(text, number))
		{
			writer->failed = true;
			return;
		}
	}
	put(writer, text);
}

static void put_limit(writer_t *writer, const char *member, const layout_limit_t *limit)
{
	if(!limit->set) return;

	put(writer, member);
	put_number(writer, limit->value);
}

static void put_item(writer_t *writer, const layout_item_t *item)
{
	put_text(writer, "{\"key\":", item->key);
	if(item->label[0] != '\0') put_text(writer, ",\"label\":", item->label);
	if(item->has_unit) put_text(writer, ",\"unit\":", item->unit);
	if(item->scale != 1)
	{
		put(writer, ",\"scale\":");
		put_number(writer, item->scale);
	}
	if(item->decimals != 0)
	{
		put(writer, ",\"dec\":");
		put_small(writer, item->decimals);
	}
	switch(item->widget)
	{
		case LAYOUT_WIDGET_ARC:   put(writer, ",\"widget\":\"arc\""); break;
		case LAYOUT_WIDGET_BAR:   put(writer, ",\"widget\":\"bar\""); break;
		case LAYOUT_WIDGET_STATE: put(writer, ",\"widget\":\"state\""); break;
		// A number widget is what the reader makes of an item without the member
		default:                  break;
	}
	put_limit(writer, ",\"min\":", &item->min);
	put_limit(writer, ",\"max\":", &item->max);
	put_limit(writer, ",\"warn_lo\":", &item->warn_lo);
	put_limit(writer, ",\"warn_hi\":", &item->warn_hi);
	put_limit(writer, ",\"crit_lo\":", &item->crit_lo);
	put_limit(writer, ",\"crit_hi\":", &item->crit_hi);

	if(item->map_count > 0)
	{
		put(writer, ",\"map\":{");
		for(int i = 0; i < item->map_count; i++)
		{
			if(i > 0) put_char(writer, ',');
			put_string(writer, item->map[i].raw);
			put_char(writer, ':');
			put_string(writer, item->map[i].text);
		}
		put_char(writer, '}');
	}
	put_char(writer, '}');
}

// "items" is the last member of a page and always there: the members before it bring their comma along
static void put_page(writer_t *writer, const layout_page_t *page)
{
	put_char(writer, '{');
	if(page->title[0] != '\0')
	{
		put_text(writer, "\"title\":", page->title);
		put_char(writer, ',');
	}
	if(page->hidden) put(writer, "\"hidden\":true,");

	put(writer, "\"items\":[");
	for(int i = 0; i < page->item_count; i++)
	{
		if(i > 0) put_char(writer, ',');
		put_item(writer, &page->items[i]);
	}
	put(writer, "]}");
}

int layout_to_json(const layout_t *layout, char *out, size_t size)
{
	writer_t writer = {out, size, 0, false};

	put(&writer, "{\"format\":\"" LAYOUT_FORMAT "\",\"v\":");
	put_small(&writer, LAYOUT_VERSION);
	if(layout->name[0] != '\0') put_text(&writer, ",\"name\":", layout->name);
	if(layout->profile_hint[0] != '\0') put_text(&writer, ",\"profile_hint\":", layout->profile_hint);

	put(&writer, ",\"pages\":[");
	for(int p = 0; p < layout->page_count; p++)
	{
		if(p > 0) put_char(&writer, ',');
		put_page(&writer, &layout->pages[p]);
	}
	put(&writer, "]}");

	if(size == 0) return -1;
	if(writer.failed || writer.length >= size)
	{
		out[0] = '\0';
		return -1;
	}
	out[writer.length] = '\0';
	return (int)writer.length;
}
