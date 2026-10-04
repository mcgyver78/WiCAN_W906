"""Mutations of display/components/core/layout.c, see ../redproof.py."""

F = "components/core/layout.c"
H = "components/core/layout.h"
T = "test_layout"

# layout_parse
TOO_LONG = "\tif(length > LAYOUT_TEXT_MAX) return refuse(&reader, \"\", \"text too long\");"
NO_JSON = "\tif(json_parse(json, length, work, work_count) < 0) return refuse(&reader, \"\", \"not valid JSON\");"
NO_OBJECT = "\tif(work[0].type != JSON_OBJECT) return refuse(&reader, \"\", \"not a JSON object\");"
CLEAR_REPORT = "\tif(report != NULL) memset(report, 0, sizeof(*report));"
CHECK_RUN = "\tif(!read_layout(&reader, NULL))"
FILL_RUN = "\treader.report = NULL;\n\tmemset(layout, 0, sizeof(*layout));\n\tread_layout(&reader, layout);\n"
FORMAT = "\tif(format < 0 || !json_text_is(r->json, &tokens[format], LAYOUT_FORMAT)) return refuse(r, \"format\", \"not \" LAYOUT_FORMAT);"
VERSION = "\tif(version < 0 || !json_integer(r->json, &tokens[version], &number)) return refuse(r, \"v\", \"not an integer\");"
NEWER = "\tif(number > LAYOUT_VERSION) return refuse(r, \"v\", \"layout of a newer display\");"
BELOW = "\tif(number < 1) return refuse(r, \"v\", \"below 1\");"
NAME = "\tif(!read_text(r, 0, \"name\", layout != NULL ? layout->name : NULL, LAYOUT_NAME_SIZE)) return false;"
HINT = "\tif(!read_text(r, 0, \"profile_hint\", layout != NULL ? layout->profile_hint : NULL, LAYOUT_NAME_SIZE)) return false;"
PAGES_MISSING = "\tif(pages < 0) return refuse(r, \"pages\", \"missing\");"
PAGES_ARRAY = "\tif(tokens[pages].type != JSON_ARRAY) return refuse(r, \"pages\", \"not an array\");"
PAGES_EMPTY = "\tif(tokens[pages].size == 0) return refuse(r, \"pages\", \"empty\");"
PAGES_MANY = "\tif(tokens[pages].size > LAYOUT_PAGES_MAX) return refuse(r, \"pages\", \"too many pages\");"
PAGE_LOOP = "\t\tif(!read_page(r, index, layout != NULL ? &layout->pages[r->page] : NULL)) return false;\n\t\tindex += tokens[index].skip;"
PAGE_COUNT = "\tif(layout != NULL) layout->page_count = (uint8_t)tokens[pages].size;"

# a page
PAGE_OBJECT = "\tif(tokens[object].type != JSON_OBJECT) return refuse(r, \"\", \"not an object\");\n\tif(!read_text(r, object, \"title\""
TITLE = "\tif(!read_text(r, object, \"title\", page != NULL ? page->title : NULL, LAYOUT_TITLE_SIZE)) return false;"
HIDDEN = "\tif(hidden >= 0 && tokens[hidden].type != JSON_TRUE && tokens[hidden].type != JSON_FALSE)"
HIDDEN_BLOCK = HIDDEN + "\n\t{\n\t\treturn refuse(r, \"hidden\", \"not a boolean\");\n\t}\n"
ITEMS_MISSING = "\tif(items < 0) return refuse(r, \"items\", \"missing\");"
ITEMS_ARRAY = "\tif(tokens[items].type != JSON_ARRAY) return refuse(r, \"items\", \"not an array\");"
ITEMS_EMPTY = "\tif(tokens[items].size == 0) return refuse(r, \"items\", \"empty\");"
ITEMS_MANY = "\tif(tokens[items].size > LAYOUT_ITEMS_MAX) return refuse(r, \"items\", \"too many items\");"
ITEM_LOOP = "\t\tif(!read_item(r, index, page != NULL ? &page->items[r->item] : NULL)) return false;\n\t\tindex += tokens[index].skip;"
HIDDEN_SET = "\t\tpage->hidden = hidden >= 0 && tokens[hidden].type == JSON_TRUE;"
ITEM_COUNT = "\t\tpage->item_count = (uint8_t)tokens[items].size;"

# an item
ITEM_OBJECT = "\tif(tokens[object].type != JSON_OBJECT) return refuse(r, \"\", \"not an object\");\n\n\tindex = member(r, object, \"key\");"
KEY_MISSING = "\tif(index < 0) return refuse(r, \"key\", \"missing\");"
KEY_EMPTY = "\tif(tokens[index].type == JSON_STRING && tokens[index].length == 0) return refuse(r, \"key\", \"empty\");"
KEY = "\tif(!read_text(r, object, \"key\", item != NULL ? item->key : NULL, VALUE_NAME_SIZE)) return false;"
LABEL = "\tif(!read_text(r, object, \"label\", item != NULL ? item->label : NULL, LAYOUT_TITLE_SIZE)) return false;"
UNIT = "\tif(!read_text(r, object, \"unit\", item != NULL ? item->unit : NULL, LAYOUT_UNIT_SIZE)) return false;"
DEC = "\tif(index >= 0 && (!json_integer(r->json, &tokens[index], &decimals) || decimals < 0 || decimals > 3))"
WIDGET_TEXT = "\t\tif(tokens[index].type != JSON_STRING) return refuse(r, \"widget\", \"not a text\");"
ARC = "\t\tif(json_text_is(r->json, &tokens[index], \"arc\")) widget = LAYOUT_WIDGET_ARC;"
BAR = "\t\telse if(json_text_is(r->json, &tokens[index], \"bar\")) widget = LAYOUT_WIDGET_BAR;"
STATE = "\t\telse if(json_text_is(r->json, &tokens[index], \"state\")) widget = LAYOUT_WIDGET_STATE;"
UNKNOWN = "\t\telse if(!json_text_is(r->json, &tokens[index], \"number\")) warn(r, \"widget\", \"unknown widget, shown as number\");"
SCALE = "\tif(!read_number(r, object, \"scale\", &scale)) return false;"
SCALE_ZERO = "\tif(scale.set && scale.value == 0) return refuse(r, \"scale\", \"zero\");"
RANGE = "\tif(!read_number(r, object, \"min\", &min) || !read_number(r, object, \"max\", &max)) return false;"
RANGE_ORDER = "\tif(min.set && max.set && !(min.value < max.value)) return refuse(r, \"min\", \"min is not below max\");"
WARN = "\tif(!read_number(r, object, \"warn_lo\", &warn_lo) || !read_number(r, object, \"warn_hi\", &warn_hi)) return false;"
CRIT = "\tif(!read_number(r, object, \"crit_lo\", &crit_lo) || !read_number(r, object, \"crit_hi\", &crit_hi)) return false;"
MAP = "\tif(!read_map(r, member(r, object, \"map\"), item != NULL ? item->map : NULL, &map_count)) return false;"
NO_RANGE = "\tif((widget == LAYOUT_WIDGET_ARC || widget == LAYOUT_WIDGET_BAR) && !(min.set && max.set))"
NO_RANGE_BLOCK = NO_RANGE + "\n\t{\n\t\twarn(r, \"widget\", \"arc or bar without min and max, shown as number\");\n\t\twidget = LAYOUT_WIDGET_NUMBER;\n\t}\n"
NO_MAP = "\tif(widget == LAYOUT_WIDGET_STATE && map_count == 0)"
NO_MAP_BLOCK = NO_MAP + "\n\t{\n\t\twarn(r, \"widget\", \"state without map, shown as number\");\n\t\twidget = LAYOUT_WIDGET_NUMBER;\n\t}\n"
HAS_UNIT = "\t\titem->has_unit = member(r, object, \"unit\") >= 0;"
TAKE = "\tlimit->set = read->set;\n\tlimit->value = read->value;"
# A layout cleared without the bytes that belong to no field: of the layout itself, of its pages, of its items.
# The last one fills each item with ones before it clears its fields: so nothing but the bytes between the
# fields of the items is left, whatever the memory held.
CLEAR_FIELDS = ("memset(layout->name, 0, sizeof(layout->name));\n\tmemset(layout->profile_hint, 0, sizeof(layout->profile_hint));\n"
                "\tmemset(layout->pages, 0, sizeof(layout->pages));\n\tlayout->page_count = 0;")
CLEAR_PAGE_FIELDS = ("memset(layout, 0, offsetof(layout_t, pages));\n\tfor(int p = 0; p < LAYOUT_PAGES_MAX; p++)\n\t{\n"
                     "\t\tmemset(layout->pages[p].title, 0, sizeof(layout->pages[p].title));\n\t\tlayout->pages[p].hidden = false;\n"
                     "\t\tmemset(layout->pages[p].items, 0, sizeof(layout->pages[p].items));\n\t\tlayout->pages[p].item_count = 0;\n\t}\n"
                     "\tmemset(&layout->page_count, 0, sizeof(*layout) - offsetof(layout_t, page_count));")
CLEAR_ITEM_FIELDS = ("memset(layout, 0, sizeof(*layout));\n\tfor(int p = 0; p < LAYOUT_PAGES_MAX; p++)\n\t{\n\t\tfor(int i = 0; i < LAYOUT_ITEMS_MAX; i++)\n\t\t{\n"
                     "\t\t\tlayout_item_t *cleared = &layout->pages[p].items[i];\n\n"
                     "\t\t\tmemset(cleared, 1, sizeof(*cleared));\n"
                     "\t\t\tmemset(cleared->key, 0, sizeof(cleared->key));\n\t\t\tmemset(cleared->label, 0, sizeof(cleared->label));\n"
                     "\t\t\tmemset(cleared->unit, 0, sizeof(cleared->unit));\n\t\t\tcleared->has_unit = false;\n\t\t\tcleared->decimals = 0;\n"
                     "\t\t\tcleared->widget = LAYOUT_WIDGET_NUMBER;\n\t\t\tcleared->scale = 0;\n\t\t\tmemset(&cleared->min, 0, 6 * sizeof(layout_limit_t));\n"
                     "\t\t\tmemset(cleared->map, 0, sizeof(cleared->map));\n\t\t\tcleared->map_count = 0;\n\t\t}\n\t}")
TAKE_MIN = "\t\ttake_limit(&item->min, &min);"
TAKE_MAX = "\t\ttake_limit(&item->max, &max);"
TAKE_WARN_LO = "\t\ttake_limit(&item->warn_lo, &warn_lo);"
TAKE_WARN_HI = "\t\ttake_limit(&item->warn_hi, &warn_hi);"
TAKE_CRIT_LO = "\t\ttake_limit(&item->crit_lo, &crit_lo);"
TAKE_CRIT_HI = "\t\ttake_limit(&item->crit_hi, &crit_hi);"

# numbers, texts, maps
NUMBER_SET = "\tnumber->set = index >= 0;\n\tnumber->value = 0;\n\tif(index < 0) return true;"
NOT_A_NUMBER = "\tif(!json_number(r->json, &r->tokens[index], &number->value)) return refuse(r, key, \"not a number\");"
NOT_FINITE = "\tif(!isfinite(number->value)) return refuse(r, key, \"not finite\");"
PART = "\t\tpart.length = s[pos] != '\\\\' ? 1 : s[pos + 1] != 'u' ? 2 : 6;"
ZERO = "\t\t\tif(memcmp(&s[pos], \"\\\\u0000\", 6) == 0) return \"control character\";"
PAIR = "\t\t\tpart.length = 12;\n\t\t\tif(!json_text(r->json, &part, bytes, sizeof(bytes))) return \"half a surrogate pair\";"
CONTROL = "\t\tif((unsigned char)bytes[0] < 0x20 || bytes[0] == 0x7F) return \"control character\";"
LONG = "\t\tif(length + count + 1 > size) return \"too long\";"
TEXT_TYPE = "\tif(r->tokens[index].type != JSON_STRING) return refuse(r, key, \"not a text\");"
MAP_OBJECT = "\tif(r->tokens[object].type != JSON_OBJECT) return refuse(r, \"map\", \"not an object\");"
MAP_MANY = "\tif(r->tokens[object].size > LAYOUT_MAP_MAX) return refuse(r, \"map\", \"too many entries\");"
MAP_LOOP = "\tfor(r->entry = 0; r->entry < r->tokens[object].size; r->entry++, key += 2)"
MAP_RAW = "\t\tconst char *problem = text_of(r, key, text, LAYOUT_MAP_RAW_SIZE);"
MAP_TEXT_TYPE = "\t\tif(r->tokens[key + 1].type != JSON_STRING) return refuse(r, \"\", \"not a text\");"
MAP_TEXT = "\t\tproblem = text_of(r, key + 1, text, LAYOUT_MAP_TEXT_SIZE);"
MAP_COUNT = "\t*count = (uint8_t)r->tokens[object].size;"

# the report
PATH_TOP = "\tif(r->page < 0) snprintf(path, size, \"%s\", member);"
PATH_PAGE = "\telse if(r->item < 0) snprintf(path, size, \"pages[%d]%s%s\", r->page, dot, member);"
PATH_ITEM = "\telse if(r->entry < 0) snprintf(path, size, \"pages[%d].items[%d]%s%s\", r->page, r->item, dot, member);"
PATH_ENTRY = "\telse snprintf(path, size, \"pages[%d].items[%d].map[%d]\", r->page, r->item, r->entry);"
FIRST_WARNING = "\tif(r->report->warnings++ > 0) return;"

# pages
LOADED = "\t\tif(strcmp(catalog->entries[i].name, CATALOG_BATTERY) != 0) return true;"
IN_LAYOUT = "\tif(page < 0 || page >= layout->page_count) return false;"
IS_HIDDEN = "\tif(shown->hidden) return false;"
NOT_LOADED = "\tif(!catalog_loaded(catalog)) return true;\n\n\tfor(int i = 0; i < shown->item_count; i++)"
ANY_KEY = "\t\tif(catalog_find(catalog, shown->items[i].key) >= 0) return true;\n\t}\n\treturn false;"
FIRST = "\tfor(int i = 0; i < layout->page_count; i++)\n\t{\n\t\tif(layout_page_shown(layout, i, catalog)) return i;\n\t}\n\treturn -1;\n}\n\nint layout_step_page"
STEP = "\tint step = direction < 0 ? -1 : 1;"
CLAMP_LOW = "\tif(page < 0) page = -1;"
CLAMP_HIGH = "\tif(page > layout->page_count) page = layout->page_count;"
FORWARD = "\tfor(int i = page + step; i >= 0 && i < layout->page_count; i += step)"
STAY = "\tif(layout_page_shown(layout, page, catalog)) return page;"
BACKWARD = "\tfor(int i = page - step; i >= 0 && i < layout->page_count; i -= step)"

# layout_suits
SUITS_NOT_LOADED = "\tif(!catalog_loaded(catalog)) return true;\n\n\tfor(int p = 0; p < layout->page_count; p++)\n\t{\n\t\tfor(int i = 0; i < layout->pages[p].item_count; i++)"
REPEATED = "\t\t\tif(key_repeated(layout, p, i)) continue;"
FOUND = "\t\t\tif(catalog_find(catalog, layout->pages[p].items[i].key) >= 0) found++;"
HALF = "\treturn 2 * found >= distinct;"
REPEAT_PAGES = "\tfor(int p = 0; p <= page; p++)"
REPEAT_COUNT = "\t\tint count = p < page ? layout->pages[p].item_count : item;"
REPEAT_SAME = "\t\t\tif(strcmp(layout->pages[p].items[i].key, key) == 0) return true;"

# layout_from_catalog
USABLE = "\tif(!usable_key(entry->name)) return;"
USABLE_EMPTY = "\tif(name[0] == '\\0') return false;\n\n\tfor(; *name != '\\0'; name++)"
USABLE_CONTROL = "\t\tif(c < 0x20 || c == 0x7F) return false;"
NEW_PAGE = "\tif(layout->page_count == 0 || layout->pages[layout->page_count - 1].item_count == ITEMS_PER_PAGE)"
NO_ROOM = "\t\tif(layout->page_count == LAYOUT_PAGES_MAX) return;"
PAGE_TITLE = "\t\tsnprintf(page->title, sizeof(page->title), \"Werte %d\", layout->page_count);"
GENERATED_KEY = "\tstrcpy(item->key, entry->name);"
GENERATED_LABEL = "\tif(!fmt_label(entry->name, item->label, sizeof(item->label))) memset(item->label, 0, sizeof(item->label));"
GENERATED_DECIMALS = "\titem->decimals = 1;\n\titem->scale = 1;"
BATTERY_FIND = "\tint battery = catalog_find(catalog, CATALOG_BATTERY);"
GENERATED_CLEAR = "\tmemset(layout, 0, sizeof(*layout));\n\tfor(int i = 0; i < catalog->count; i++)"
OTHERS = "\t\tif(i != battery) add_item(layout, &catalog->entries[i]);"
BATTERY_LAST = "\tif(battery >= 0) add_item(layout, &catalog->entries[battery]);"

# an item on the screen
LIVE = "\t\tcase VALUE_AGE_FRESH: return LAYOUT_ITEM_LIVE;"
OLD = "\t\tcase VALUE_AGE_OLD:   return LAYOUT_ITEM_OLD;"
WAITING = "\tif(!catalog_loaded(catalog) || catalog_find(catalog, item->key) >= 0) return LAYOUT_ITEM_NO_VALUE;"
NUMBER_OF = "\tif(value->kind == VALUE_NUMBER) return value->number;\n\treturn value->kind == VALUE_ON ? 1 : 0;"
RAW_SWITCH = "\t\tstrcpy(out, value->kind == VALUE_ON ? \"on\" : \"off\");"
RAW_NUMBER = "\tif(!fmt_number(value->number, 3, out, size)) return false;"
RAW_ZEROS = "\twhile(out[length - 1] == '0') length--;"
RAW_COMMA = "\tif(out[length - 1] == ',') length--;"
RAW_END = "\tout[length] = '\\0';\n\treturn true;\n}\n\nstatic const layout_map_t *map_entry"
ENTRY_LOOP = "\tfor(int i = 0; i < item->map_count; i++)\n\t{\n\t\tif(strcmp(item->map[i].raw, raw) == 0) return &item->map[i];"
TEXT_NO_ROOM = "\tif(size == 0) return false;\n\tout[0] = '\\0';\n\tif(value == NULL) return false;"
IS_STATE = "\tif(item->widget == LAYOUT_WIDGET_STATE)\n\t{\n\t\t// A number that needs"
BY_VALUE = "\t\tif(raw_text(value, raw, sizeof(raw))) entry = map_entry(item, raw);"
BY_STAR = "\t\tif(entry == NULL) entry = map_entry(item, \"*\");"
ENTRY_FITS = "\t\t\tif(strlen(entry->text) + 1 > size) return false;"
AS_NUMBER = "\treturn fmt_number(number_of(value) * item->scale, item->decimals, out, size);"
BEYOND = "\treturn (low->set && number <= low->value) || (high->set && number >= high->value);"
LEVEL_NO_VALUE = "\tif(value == NULL) return 0;"
LEVEL_NUMBER = "\tnumber = number_of(value) * item->scale;"
LEVEL_CRIT = "\tif(beyond(number, &item->crit_lo, &item->crit_hi)) return 2;"
LEVEL_WARN = "\tif(beyond(number, &item->warn_lo, &item->warn_hi)) return 1;"
OWN_UNIT = "\tif(item->has_unit) return item->unit;"
CATALOG_UNIT = "\treturn index < 0 ? \"\" : catalog->entries[index].unit;"

# layout_to_json
PUT_CHAR = "\tif(writer->length < writer->size) writer->out[writer->length] = c;\n\twriter->length++;"
ESCAPE_CONTROL = "\t\tif(c < 0x20)\n\t\t{\n\t\t\tput(writer, \"\\\\u00\");\n\t\t\tput_char(writer, hex[c >> 4]);\n\t\t\tput_char(writer, hex[c & 0x0F]);\n\t\t\tcontinue;\n\t\t}\n"
ESCAPE = "\t\tif(c == '\"' || c == '\\\\') put_char(writer, '\\\\');"
SMALL = "\t\treversed[length++] = (char)('0' + number % 10);\n\t\tnumber /= 10;"
READS_BACK = "\treturn json_parse(text, strlen(text), &token, 1) == 1 && json_number(text, &token, &back) && back == number;"
FIFTEEN = "\tsnprintf(text, sizeof(text), \"%.15g\", number);\n\tif(!reads_back(text, number))"
SEVENTEEN = "\t\tsnprintf(text, sizeof(text), \"%.17g\", number);\n\t\tif(!reads_back(text, number))"
FAILED = "\t\t\twriter->failed = true;\n\t\t\treturn;"
LIMIT_SET = "\tif(!limit->set) return;"
W_LABEL = "\tif(item->label[0] != '\\0') put_text(writer, \",\\\"label\\\":\", item->label);"
W_UNIT = "\tif(item->has_unit) put_text(writer, \",\\\"unit\\\":\", item->unit);"
W_SCALE = "\tif(item->scale != 1)\n\t{\n\t\tput(writer, \",\\\"scale\\\":\");\n\t\tput_number(writer, item->scale);\n\t}\n"
W_DEC = "\tif(item->decimals != 0)\n\t{\n\t\tput(writer, \",\\\"dec\\\":\");\n\t\tput_small(writer, item->decimals);\n\t}\n"
W_ARC = "\t\tcase LAYOUT_WIDGET_ARC:   put(writer, \",\\\"widget\\\":\\\"arc\\\"\"); break;"
W_BAR = "\t\tcase LAYOUT_WIDGET_BAR:   put(writer, \",\\\"widget\\\":\\\"bar\\\"\"); break;"
W_STATE = "\t\tcase LAYOUT_WIDGET_STATE: put(writer, \",\\\"widget\\\":\\\"state\\\"\"); break;"
W_WIDGET = ("\tswitch(item->widget)\n\t{\n" + W_ARC + "\n" + W_BAR + "\n" + W_STATE + "\n"
            "\t\t// A number widget is what the reader makes of an item without the member\n\t\tdefault:                  break;\n\t}\n")
W_MIN = "\tput_limit(writer, \",\\\"min\\\":\", &item->min);"
W_MAX = "\tput_limit(writer, \",\\\"max\\\":\", &item->max);"
W_WARN_LO = "\tput_limit(writer, \",\\\"warn_lo\\\":\", &item->warn_lo);"
W_WARN_HI = "\tput_limit(writer, \",\\\"warn_hi\\\":\", &item->warn_hi);"
W_CRIT_LO = "\tput_limit(writer, \",\\\"crit_lo\\\":\", &item->crit_lo);"
W_CRIT_HI = "\tput_limit(writer, \",\\\"crit_hi\\\":\", &item->crit_hi);"
W_MAP = "\tif(item->map_count > 0)"
W_MAP_LOOP = "\t\tfor(int i = 0; i < item->map_count; i++)\n\t\t{\n\t\t\tif(i > 0) put_char(writer, ',');\n\t\t\tput_string(writer, item->map[i].raw);"
W_MAP_TEXT = "\t\t\tput_char(writer, ':');\n\t\t\tput_string(writer, item->map[i].text);"
W_TITLE = "\tif(page->title[0] != '\\0')\n\t{\n\t\tput_text(writer, \"\\\"title\\\":\", page->title);\n\t\tput_char(writer, ',');\n\t}\n"
W_HIDDEN = "\tif(page->hidden) put(writer, \"\\\"hidden\\\":true,\");"
W_ITEMS = "\tfor(int i = 0; i < page->item_count; i++)\n\t{\n\t\tif(i > 0) put_char(writer, ',');\n\t\tput_item(writer, &page->items[i]);"
W_HEAD = "\tput(&writer, \"{\\\"format\\\":\\\"\" LAYOUT_FORMAT \"\\\",\\\"v\\\":\");\n\tput_small(&writer, LAYOUT_VERSION);"
W_NAME = "\tif(layout->name[0] != '\\0') put_text(&writer, \",\\\"name\\\":\", layout->name);"
W_HINT = "\tif(layout->profile_hint[0] != '\\0') put_text(&writer, \",\\\"profile_hint\\\":\", layout->profile_hint);"
W_PAGES = "\tfor(int p = 0; p < layout->page_count; p++)\n\t{\n\t\tif(p > 0) put_char(&writer, ',');\n\t\tput_page(&writer, &layout->pages[p]);"
W_NO_ROOM = "\tif(size == 0) return -1;\n\tif(writer.failed || writer.length >= size)\n\t{\n\t\tout[0] = '\\0';\n\t\treturn -1;\n\t}\n"
W_END = "\tout[writer.length] = '\\0';\n\treturn (int)writer.length;"

MUTATIONS = [
    # the numbers of the header
    ("layout_text_limit_one_less", T, H, "#define LAYOUT_TEXT_MAX     16384", "#define LAYOUT_TEXT_MAX     16383"),
    ("layout_text_limit_one_more", T, H, "#define LAYOUT_TEXT_MAX     16384", "#define LAYOUT_TEXT_MAX     16385"),
    ("layout_version_two", T, H, "#define LAYOUT_VERSION      1", "#define LAYOUT_VERSION      2"),
    ("layout_format_other_name", T, H, "#define LAYOUT_FORMAT       \"wican-display-layout\"", "#define LAYOUT_FORMAT       \"wican-display-layouts\""),
    ("layout_tokens_one_less", T, H, "#define LAYOUT_TOKENS       4096", "#define LAYOUT_TOKENS       4095"),
    ("layout_generated_text_one_less", T, H, "#define LAYOUT_GENERATED_TEXT_MAX   6844", "#define LAYOUT_GENERATED_TEXT_MAX   6843"),
    ("layout_generated_text_one_more", T, H, "#define LAYOUT_GENERATED_TEXT_MAX   6844", "#define LAYOUT_GENERATED_TEXT_MAX   6845"),

    # layout_parse: the text as a whole
    ("layout_long_text_accepted", T, F, TOO_LONG + "\n", ""),
    ("layout_text_at_limit_refused", T, F, TOO_LONG, TOO_LONG.replace("length > LAYOUT_TEXT_MAX", "length >= LAYOUT_TEXT_MAX")),
    ("layout_text_one_above_limit_accepted", T, F, TOO_LONG, TOO_LONG.replace("length > LAYOUT_TEXT_MAX", "length > LAYOUT_TEXT_MAX + 1")),
    ("layout_long_text_called_broken", T, F, TOO_LONG, TOO_LONG.replace("text too long", "not valid JSON")),
    ("layout_length_checked_after_parsing", T, F,
     TOO_LONG + "\n" + NO_JSON + "\n", NO_JSON + "\n" + TOO_LONG + "\n"),
    ("layout_broken_json_accepted", T, F, NO_JSON + "\n", "\tjson_parse(json, length, work, work_count);\n"),
    ("layout_too_many_tokens_accepted", T, F, NO_JSON, NO_JSON.replace("work_count) < 0)", "work_count) == JSON_SYNTAX)")),
    ("layout_reads_behind_length", T, F, NO_JSON, NO_JSON.replace("json_parse(json, length,", "json_parse(json, length + 1,")),
    ("layout_length_ignored", T, F, NO_JSON, NO_JSON.replace("json_parse(json, length,", "json_parse(json, strlen(json),")),
    ("layout_tokens_behind_room", T, F, NO_JSON, NO_JSON.replace("work, work_count)", "work, work_count + 1)")),
    ("layout_room_for_tokens_ignored", T, F, NO_JSON, NO_JSON.replace("work, work_count)", "work, work_count > 0 ? LAYOUT_TOKENS : 0)")),
    ("layout_no_object_accepted", T, F, NO_OBJECT + "\n", ""),
    ("layout_array_accepted", T, F, NO_OBJECT, NO_OBJECT.replace("work[0].type != JSON_OBJECT", "work[0].type != JSON_OBJECT && work[0].type != JSON_ARRAY")),
    ("layout_no_object_called_broken", T, F, NO_OBJECT, NO_OBJECT.replace("not a JSON object", "not valid JSON")),
    ("layout_filled_while_checked", T, F, CHECK_RUN, "\tif(!read_layout(&reader, layout))"),
    ("layout_cleared_before_checked", T, F, CHECK_RUN, "\tmemset(layout, 0, sizeof(*layout));\n" + CHECK_RUN),
    ("layout_not_filled", T, F, FILL_RUN, "\treader.report = NULL;\n\tmemset(layout, 0, sizeof(*layout));\n"),
    ("layout_not_cleared_before_filled", T, F, FILL_RUN, "\treader.report = NULL;\n\tread_layout(&reader, layout);\n"),
    ("layout_only_head_cleared", T, F,
     "\tmemset(layout, 0, sizeof(*layout));\n\tread_layout(&reader, layout);", "\tmemset(layout, 0, sizeof(layout->name));\n\tread_layout(&reader, layout);"),
    ("layout_accepted_returns_false", T, F, "\tread_layout(&reader, layout);\n\treturn true;", "\tread_layout(&reader, layout);\n\treturn false;"),

    # the report
    ("layout_report_not_cleared", T, F, CLEAR_REPORT + "\n", ""),
    ("layout_report_only_problem_cleared", T, F, CLEAR_REPORT, "\tif(report != NULL) memset(report, 0, sizeof(report->path) + sizeof(report->problem));"),
    ("layout_report_needed", T, F, CLEAR_REPORT, "\tmemset(report, 0, sizeof(*report));"),
    ("layout_refusal_needs_report", T, F,
     "\tif(r->report != NULL)\n\t{\n\t\twrite_path(r, member, r->report->path, sizeof(r->report->path));",
     "\t{\n\t\twrite_path(r, member, r->report->path, sizeof(r->report->path));"),
    ("layout_warning_needs_report", T, F, "\tif(r->report == NULL) return;\n", ""),
    ("layout_refused_keeps_warning_count", T, F, "\t\t\treport->warnings = 0;\n", ""),
    ("layout_refused_keeps_warning_path", T, F, "\t\t\treport->warning_path[0] = '\\0';\n", ""),
    ("layout_refused_keeps_warning_text", T, F, "\t\t\treport->warning[0] = '\\0';\n", ""),
    ("layout_refused_reset_needs_no_report", T, F,
     "\t\tif(report != NULL)\n\t\t{\n\t\t\treport->warnings = 0;", "\t\t{\n\t\t\treport->warnings = 0;"),
    ("layout_refusal_without_path", T, F, "\t\twrite_path(r, member, r->report->path, sizeof(r->report->path));\n", "\t\t(void)member;\n"),
    ("layout_refusal_without_problem", T, F,
     "\t\tsnprintf(r->report->problem, sizeof(r->report->problem), \"%s\", problem);\n", "\t\t(void)problem;\n"),
    ("layout_refusal_returns_true", T, F,
     "\t\tsnprintf(r->report->problem, sizeof(r->report->problem), \"%s\", problem);\n\t}\n\treturn false;",
     "\t\tsnprintf(r->report->problem, sizeof(r->report->problem), \"%s\", problem);\n\t}\n\treturn true;"),
    ("layout_every_warning_told", T, F, FIRST_WARNING, "\tr->report->warnings++;"),
    ("layout_last_warning_told", T, F, FIRST_WARNING, "\tr->report->warnings++;\n\tr->report->warning[0] = '\\0';"),
    ("layout_warnings_not_counted", T, F, FIRST_WARNING, "\tif(r->report->warnings > 0) return;\n\tr->report->warnings = 1;"),
    ("layout_warning_without_path", T, F,
     "\twrite_path(r, member, r->report->warning_path, sizeof(r->report->warning_path));\n", "\t(void)member;\n"),
    ("layout_warning_without_text", T, F,
     "\tsnprintf(r->report->warning, sizeof(r->report->warning), \"%s\", warning);\n", "\t(void)warning;\n"),
    ("layout_warning_path_in_problem_path", T, F,
     "\twrite_path(r, member, r->report->warning_path, sizeof(r->report->warning_path));",
     "\twrite_path(r, member, r->report->path, sizeof(r->report->path));"),
    ("layout_path_without_dot", T, F, "\tconst char *dot = member[0] != '\\0' ? \".\" : \"\";", "\tconst char *dot = \"\";"),
    ("layout_path_always_with_dot", T, F, "\tconst char *dot = member[0] != '\\0' ? \".\" : \"\";", "\tconst char *dot = \".\";"),
    ("layout_path_of_page_for_top_member", T, F, PATH_TOP, "\tif(r->page < -1) snprintf(path, size, \"%s\", member);"),
    ("layout_path_of_item_for_page_member", T, F, PATH_PAGE, PATH_PAGE.replace("r->item < 0", "r->item < -1")),
    ("layout_path_of_entry_for_item_member", T, F, PATH_ITEM, PATH_ITEM.replace("r->entry < 0", "r->entry < -1")),
    ("layout_path_of_item_for_entry", T, F, PATH_ITEM, PATH_ITEM.replace("r->entry < 0", "r->entry <= 0")),
    ("layout_path_counts_pages_from_one", T, F, PATH_ITEM, PATH_ITEM.replace("r->page, r->item, dot", "r->page + 1, r->item, dot")),
    ("layout_path_counts_items_from_one", T, F, PATH_ITEM, PATH_ITEM.replace("r->page, r->item, dot", "r->page, r->item + 1, dot")),
    ("layout_path_page_and_item_swapped", T, F, PATH_ITEM, PATH_ITEM.replace("r->page, r->item, dot", "r->item, r->page, dot")),
    ("layout_path_of_page_counts_from_one", T, F, PATH_PAGE, PATH_PAGE.replace("r->page, dot", "r->page + 1, dot")),
    ("layout_path_of_entry_counts_from_one", T, F, PATH_ENTRY, PATH_ENTRY.replace("r->item, r->entry)", "r->item, r->entry + 1)")),
    ("layout_path_of_entry_names_item_twice", T, F, PATH_ENTRY, PATH_ENTRY.replace("r->item, r->entry)", "r->item, r->item)")),
    ("layout_item_place_kept_after_page", T, F, "\tr->item = -1;\n", ""),
    ("layout_entry_place_kept_after_map", T, F, "\tr->entry = -1;\n", ""),

    # the order in which problems are found
    ("layout_order_version_before_format", T, F, FORMAT + "\n" + VERSION, VERSION + "\n" + FORMAT),
    ("layout_order_hint_before_name", T, F, NAME + "\n" + HINT, HINT + "\n" + NAME),
    ("layout_order_pages_before_name", T, F, NAME + "\n" + HINT + "\n\n" + PAGES_MISSING + "\n" + PAGES_ARRAY + "\n",
     PAGES_MISSING + "\n" + PAGES_ARRAY + "\n" + NAME + "\n" + HINT + "\n\n"),
    ("layout_order_missing_pages_before_name", T, F, NAME + "\n" + HINT + "\n\n" + PAGES_MISSING + "\n", PAGES_MISSING + "\n" + NAME + "\n" + HINT + "\n\n"),
    ("layout_order_missing_items_before_title", T, F, TITLE + "\n" + HIDDEN_BLOCK + ITEMS_MISSING + "\n", ITEMS_MISSING + "\n" + TITLE + "\n" + HIDDEN_BLOCK),
    ("layout_order_hidden_before_title", T, F, TITLE + "\n" + HIDDEN_BLOCK, HIDDEN_BLOCK + TITLE + "\n"),
    ("layout_order_items_before_hidden", T, F, HIDDEN_BLOCK + ITEMS_MISSING + "\n" + ITEMS_ARRAY + "\n", ITEMS_MISSING + "\n" + ITEMS_ARRAY + "\n" + HIDDEN_BLOCK),
    ("layout_order_label_before_key", T, F, KEY + "\n" + LABEL, LABEL + "\n" + KEY),
    ("layout_order_unit_before_label", T, F, LABEL + "\n" + UNIT, UNIT + "\n" + LABEL),
    ("layout_order_scale_before_key", T, F, KEY + "\n", SCALE + "\n" + KEY + "\n"),
    ("layout_order_max_before_min", T, F, RANGE, "\tif(!read_number(r, object, \"max\", &max) || !read_number(r, object, \"min\", &min)) return false;"),
    ("layout_order_warn_hi_before_warn_lo", T, F,
     WARN, "\tif(!read_number(r, object, \"warn_hi\", &warn_hi) || !read_number(r, object, \"warn_lo\", &warn_lo)) return false;"),
    ("layout_order_crit_hi_before_crit_lo", T, F,
     CRIT, "\tif(!read_number(r, object, \"crit_hi\", &crit_hi) || !read_number(r, object, \"crit_lo\", &crit_lo)) return false;"),
    ("layout_order_crit_before_warn", T, F, WARN + "\n" + CRIT, CRIT + "\n" + WARN),
    ("layout_order_limits_before_range_order", T, F, RANGE_ORDER + "\n" + WARN, WARN + "\n" + RANGE_ORDER),
    ("layout_order_range_before_scale_zero", T, F, SCALE_ZERO + "\n" + RANGE + "\n" + RANGE_ORDER, RANGE + "\n" + RANGE_ORDER + "\n" + SCALE_ZERO),
    ("layout_order_map_before_limits", T, F, CRIT + "\n\n" + MAP, MAP + "\n" + CRIT + "\n"),
    ("layout_order_map_text_before_its_value", T, F,
     "\t\tif(problem != NULL) return refuse(r, \"\", problem);\n\t\tif(map != NULL) strcpy(map[r->entry].raw, text);\n\n" + MAP_TEXT_TYPE + "\n",
     MAP_TEXT_TYPE + "\n\t\tif(problem != NULL) return refuse(r, \"\", problem);\n\t\tif(map != NULL) strcpy(map[r->entry].raw, text);\n\n"),
    ("layout_order_map_entries_before_their_count", T, F, MAP_MANY + "\n", ""),
    ("layout_order_empty_key_after_label", T, F, KEY_EMPTY + "\n" + KEY + "\n" + LABEL + "\n", KEY + "\n" + LABEL + "\n" + KEY_EMPTY + "\n"),

    # format, version, name
    ("layout_missing_format_accepted", T, F, FORMAT, FORMAT.replace("format < 0 || !json_text_is", "format >= 0 && !json_text_is")),
    ("layout_format_not_compared", T, F, FORMAT, FORMAT.replace("format < 0 || !json_text_is(r->json, &tokens[format], LAYOUT_FORMAT)", "format < 0")),
    ("layout_format_taken_from_before_the_tokens", T, F, FORMAT, FORMAT.replace("format < 0 || ", "")),
    ("layout_format_any_text", T, F, FORMAT, FORMAT.replace("!json_text_is(r->json, &tokens[format], LAYOUT_FORMAT)", "tokens[format].type != JSON_STRING")),
    ("layout_missing_version_accepted", T, F,
     VERSION, "\tnumber = 1;\n\tif(version >= 0 && !json_integer(r->json, &tokens[version], &number)) return refuse(r, \"v\", \"not an integer\");"),
    ("layout_version_taken_from_before_the_tokens", T, F, VERSION, VERSION.replace("version < 0 || ", "")),
    ("layout_newer_version_accepted", T, F, NEWER + "\n", ""),
    ("layout_own_version_refused", T, F, NEWER, NEWER.replace("number > LAYOUT_VERSION", "number >= LAYOUT_VERSION")),
    ("layout_next_version_accepted", T, F, NEWER, NEWER.replace("number > LAYOUT_VERSION", "number > LAYOUT_VERSION + 1")),
    ("layout_newer_version_called_below", T, F, NEWER, NEWER.replace("layout of a newer display", "below 1")),
    ("layout_version_compared_in_32_bit", T, F, NEWER, NEWER.replace("number > LAYOUT_VERSION", "(int32_t)number > LAYOUT_VERSION")),
    ("layout_version_compared_in_8_bit", T, F, NEWER, NEWER.replace("number > LAYOUT_VERSION", "(uint8_t)number > LAYOUT_VERSION")),
    ("layout_version_compared_in_16_bit", T, F, NEWER, NEWER.replace("number > LAYOUT_VERSION", "(uint16_t)number > LAYOUT_VERSION")),
    ("layout_version_below_one_compared_in_8_bit", T, F, BELOW, BELOW.replace("number < 1", "(int8_t)number < 1")),
    ("layout_version_below_one_compared_in_16_bit", T, F, BELOW, BELOW.replace("number < 1", "(int16_t)number < 1")),
    ("layout_version_below_one_compared_in_32_bit", T, F, BELOW, BELOW.replace("number < 1", "(int32_t)number < 1")),
    ("layout_version_zero_accepted", T, F, BELOW, BELOW.replace("number < 1", "number < 0")),
    ("layout_version_below_one_accepted", T, F, BELOW + "\n", ""),
    ("layout_name_not_read", T, F, NAME + "\n", ""),
    ("layout_name_read_into_hint", T, F, NAME, NAME.replace("layout->name : NULL", "layout->profile_hint : NULL")),
    ("layout_name_limit_one_less", T, F, NAME, NAME.replace("LAYOUT_NAME_SIZE", "LAYOUT_NAME_SIZE - 1")),
    ("layout_name_limit_one_more", T, F, NAME, NAME.replace("LAYOUT_NAME_SIZE", "LAYOUT_NAME_SIZE + 1")),
    ("layout_hint_not_read", T, F, HINT + "\n", ""),
    ("layout_hint_read_into_name", T, F, HINT, HINT.replace("layout->profile_hint : NULL", "layout->name : NULL")),
    ("layout_hint_limit_one_less", T, F, HINT, HINT.replace("LAYOUT_NAME_SIZE", "LAYOUT_NAME_SIZE - 1")),
    ("layout_hint_limit_one_more", T, F, HINT, HINT.replace("LAYOUT_NAME_SIZE", "LAYOUT_NAME_SIZE + 1")),

    # pages
    ("layout_missing_pages_called_no_array", T, F, PAGES_MISSING + "\n", ""),
    ("layout_pages_of_any_type", T, F, PAGES_ARRAY + "\n", ""),
    ("layout_pages_object_accepted", T, F, PAGES_ARRAY, PAGES_ARRAY.replace("!= JSON_ARRAY", "!= JSON_ARRAY && tokens[pages].type != JSON_OBJECT")),
    ("layout_no_page_accepted", T, F, PAGES_EMPTY + "\n", ""),
    ("layout_one_page_refused", T, F, PAGES_EMPTY, PAGES_EMPTY.replace("size == 0", "size <= 1")),
    ("layout_13_pages_accepted", T, F, PAGES_MANY, PAGES_MANY.replace("> LAYOUT_PAGES_MAX", "> LAYOUT_PAGES_MAX + 1")),
    ("layout_12_pages_refused", T, F, PAGES_MANY, PAGES_MANY.replace("> LAYOUT_PAGES_MAX", ">= LAYOUT_PAGES_MAX")),
    ("layout_any_count_of_pages_accepted", T, F, PAGES_MANY + "\n", ""),
    ("layout_pages_not_stepped", T, F, PAGE_LOOP, PAGE_LOOP.replace("\n\t\tindex += tokens[index].skip;", "")),
    ("layout_pages_stepped_by_one_token", T, F, PAGE_LOOP, PAGE_LOOP.replace("index += tokens[index].skip;", "index += 1;")),
    ("layout_every_page_read_into_the_first", T, F, PAGE_LOOP, PAGE_LOOP.replace("&layout->pages[r->page]", "&layout->pages[0]")),
    ("layout_page_count_not_set", T, F, PAGE_COUNT + "\n", ""),
    ("layout_11_pages_lose_one", T, F, PAGE_COUNT, PAGE_COUNT.replace("(uint8_t)tokens[pages].size;", "(uint8_t)(tokens[pages].size == 11 ? 10 : tokens[pages].size);")),
    ("layout_page_count_one_less", T, F, PAGE_COUNT, PAGE_COUNT.replace("(uint8_t)tokens[pages].size;", "(uint8_t)(tokens[pages].size - 1);")),
    ("layout_page_no_object_accepted", T, F,
     PAGE_OBJECT, "\tif(!read_text(r, object, \"title\""),
    ("layout_page_array_accepted", T, F,
     PAGE_OBJECT, PAGE_OBJECT.replace("tokens[object].type != JSON_OBJECT", "tokens[object].type != JSON_OBJECT && tokens[object].type != JSON_ARRAY")),
    ("layout_title_not_read", T, F, TITLE + "\n", ""),
    ("layout_title_limit_one_less", T, F, TITLE, TITLE.replace("LAYOUT_TITLE_SIZE", "LAYOUT_TITLE_SIZE - 1")),
    ("layout_title_limit_one_more", T, F, TITLE, TITLE.replace("LAYOUT_TITLE_SIZE", "LAYOUT_TITLE_SIZE + 1")),
    ("layout_hidden_of_any_type", T, F, HIDDEN + "\n\t{\n\t\treturn refuse(r, \"hidden\", \"not a boolean\");\n\t}\n", ""),
    ("layout_hidden_null_accepted", T, F, HIDDEN, HIDDEN.replace("!= JSON_FALSE)", "!= JSON_FALSE && tokens[hidden].type != JSON_NULL)")),
    ("layout_hidden_number_accepted", T, F, HIDDEN, HIDDEN.replace("!= JSON_FALSE)", "!= JSON_FALSE && tokens[hidden].type != JSON_NUMBER)")),
    ("layout_hidden_true_refused", T, F, HIDDEN, "\tif(hidden >= 0 && tokens[hidden].type != JSON_FALSE)"),
    ("layout_hidden_false_refused", T, F, HIDDEN, "\tif(hidden >= 0 && tokens[hidden].type != JSON_TRUE)"),
    ("layout_hidden_taken_from_before_the_tokens", T, F, HIDDEN_SET, HIDDEN_SET.replace("hidden >= 0 && ", "")),
    ("layout_hidden_never", T, F, HIDDEN_SET, "\t\tpage->hidden = false;"),
    ("layout_hidden_whenever_named", T, F, HIDDEN_SET, "\t\tpage->hidden = hidden >= 0;"),
    ("layout_hidden_inverted", T, F, HIDDEN_SET, HIDDEN_SET.replace("== JSON_TRUE", "== JSON_FALSE")),
    ("layout_missing_items_called_no_array", T, F, ITEMS_MISSING + "\n", ""),
    ("layout_items_of_any_type", T, F, ITEMS_ARRAY + "\n", ""),
    ("layout_no_item_accepted", T, F, ITEMS_EMPTY + "\n", ""),
    ("layout_one_item_refused", T, F, ITEMS_EMPTY, ITEMS_EMPTY.replace("size == 0", "size <= 1")),
    ("layout_7_items_accepted", T, F, ITEMS_MANY, ITEMS_MANY.replace("> LAYOUT_ITEMS_MAX", "> LAYOUT_ITEMS_MAX + 1")),
    ("layout_6_items_refused", T, F, ITEMS_MANY, ITEMS_MANY.replace("> LAYOUT_ITEMS_MAX", ">= LAYOUT_ITEMS_MAX")),
    ("layout_items_limited_like_pages", T, F, ITEMS_MANY, ITEMS_MANY.replace("> LAYOUT_ITEMS_MAX", "> LAYOUT_PAGES_MAX")),
    ("layout_items_stepped_by_one_token", T, F, ITEM_LOOP, ITEM_LOOP.replace("index += tokens[index].skip;", "index += 1;")),
    ("layout_every_item_read_into_the_first", T, F, ITEM_LOOP, ITEM_LOOP.replace("&page->items[r->item]", "&page->items[0]")),
    ("layout_item_count_not_set", T, F, ITEM_COUNT + "\n", ""),
    ("layout_5_items_lose_one", T, F, ITEM_COUNT, ITEM_COUNT.replace("(uint8_t)tokens[items].size;", "(uint8_t)(tokens[items].size == 5 ? 4 : tokens[items].size);")),
    ("layout_item_count_one_more", T, F, ITEM_COUNT, ITEM_COUNT.replace("(uint8_t)tokens[items].size;", "(uint8_t)(tokens[items].size + 1);")),

    # an item: key, label, unit
    ("layout_item_no_object_accepted", T, F, ITEM_OBJECT, "\tindex = member(r, object, \"key\");"),
    ("layout_item_array_called_without_key", T, F,
     ITEM_OBJECT, ITEM_OBJECT.replace("tokens[object].type != JSON_OBJECT", "tokens[object].type != JSON_OBJECT && tokens[object].type != JSON_ARRAY")),
    ("layout_missing_key_accepted", T, F, KEY_MISSING + "\n", ""),
    ("layout_missing_key_called_empty", T, F, KEY_MISSING, KEY_MISSING.replace("\"missing\"", "\"empty\"")),
    ("layout_empty_key_accepted", T, F, KEY_EMPTY + "\n", ""),
    ("layout_key_of_one_byte_refused", T, F, KEY_EMPTY, KEY_EMPTY.replace("length == 0", "length <= 1")),
    ("layout_key_not_read", T, F, KEY + "\n", ""),
    ("layout_key_limit_one_less", T, F, KEY, KEY.replace("VALUE_NAME_SIZE", "VALUE_NAME_SIZE - 1")),
    ("layout_key_limit_one_more", T, F, KEY, KEY.replace("VALUE_NAME_SIZE", "VALUE_NAME_SIZE + 1")),
    ("layout_key_limit_of_a_label", T, F, KEY, KEY.replace("VALUE_NAME_SIZE", "LAYOUT_TITLE_SIZE")),
    ("layout_label_not_read", T, F, LABEL + "\n", ""),
    ("layout_label_read_into_key", T, F, LABEL, LABEL.replace("item->label : NULL", "item->key : NULL")),
    ("layout_label_limit_one_less", T, F, LABEL, LABEL.replace("LAYOUT_TITLE_SIZE", "LAYOUT_TITLE_SIZE - 1")),
    ("layout_label_limit_one_more", T, F, LABEL, LABEL.replace("LAYOUT_TITLE_SIZE", "LAYOUT_TITLE_SIZE + 1")),
    ("layout_label_limit_of_a_name", T, F, LABEL, LABEL.replace("LAYOUT_TITLE_SIZE", "LAYOUT_NAME_SIZE")),
    ("layout_unit_not_read", T, F, UNIT + "\n", ""),
    ("layout_unit_limit_one_less", T, F, UNIT, UNIT.replace("LAYOUT_UNIT_SIZE", "LAYOUT_UNIT_SIZE - 1")),
    ("layout_unit_limit_one_more", T, F, UNIT, UNIT.replace("LAYOUT_UNIT_SIZE", "LAYOUT_UNIT_SIZE + 1")),
    ("layout_has_unit_never", T, F, HAS_UNIT, "\t\titem->has_unit = false;"),
    ("layout_has_unit_always", T, F, HAS_UNIT, "\t\titem->has_unit = true;"),
    ("layout_empty_unit_is_no_unit", T, F, HAS_UNIT, "\t\titem->has_unit = item->unit[0] != '\\0';"),
    ("layout_text_of_any_type", T, F, TEXT_TYPE + "\n", ""),
    ("layout_text_null_is_no_text", T, F, TEXT_TYPE, "\tif(r->tokens[index].type == JSON_NULL) return true;\n" + TEXT_TYPE),
    ("layout_text_not_taken_over", T, F, "\tif(out != NULL) strcpy(out, text);\n\treturn true;", "\t(void)out;\n\treturn true;"),

    # texts: length, control characters, surrogates
    ("layout_text_one_byte_too_few", T, F, LONG, LONG.replace("length + count + 1 > size", "length + count + 2 > size")),
    ("layout_text_length_counted_in_characters", T, F, LONG, LONG.replace("length + count + 1 > size", "length + 2 > size")),
    ("layout_text_length_counted_in_source_bytes", T, F, LONG, "\t\tif(token->length + 1 > size) return \"too long\";"),
    ("layout_long_text_called_control", T, F, LONG, LONG.replace("\"too long\"", "\"control character\"")),
    ("layout_control_characters_accepted", T, F, CONTROL + "\n", ""),
    ("layout_unit_separator_accepted", T, F, CONTROL, CONTROL.replace("< 0x20", "< 0x1F")),
    ("layout_space_refused", T, F, CONTROL, CONTROL.replace("< 0x20", "<= 0x20")),
    ("layout_delete_accepted", T, F, CONTROL, CONTROL.replace(" || bytes[0] == 0x7F", "")),
    ("layout_tilde_refused", T, F, CONTROL, CONTROL.replace("bytes[0] == 0x7F", "bytes[0] >= 0x7E")),
    ("layout_high_bytes_refused", T, F, CONTROL, CONTROL.replace("(unsigned char)bytes[0] < 0x20", "bytes[0] < 0x20 || (unsigned char)bytes[0] >= 0xF0")),
    ("layout_only_line_breaks_refused", T, F, CONTROL, CONTROL.replace("(unsigned char)bytes[0] < 0x20", "bytes[0] == '\\n' || bytes[0] == '\\r'")),
    ("layout_control_called_too_long", T, F, CONTROL, CONTROL.replace("\"control character\"", "\"too long\"")),
    ("layout_zero_called_half_a_pair", T, F, ZERO + "\n", ""),
    ("layout_every_refused_escape_called_control", T, F, ZERO, "\t\t\tif(s[pos] == '\\\\') return \"control character\";"),
    ("layout_pair_not_tried", T, F, PAIR, "\t\t\treturn \"half a surrogate pair\";"),
    ("layout_half_pair_called_control", T, F, PAIR, PAIR.replace("\"half a surrogate pair\"", "\"control character\"")),
    ("layout_escapes_taken_as_two_bytes", T, F, PART, "\t\tpart.length = s[pos] != '\\\\' ? 1 : 2;"),
    ("layout_escapes_taken_byte_by_byte", T, F, PART, "\t\tpart.length = 1;"),
    ("layout_text_of_one_character", T, F, "\t\tlength += count;\n\t}\n\treturn NULL;", "\t}\n\treturn NULL;"),

    # an item: decimals, widget, numbers
    ("layout_decimals_of_any_kind", T, F, DEC + "\n\t{\n\t\treturn refuse(r, \"dec\", \"not an integer 0 to 3\");\n\t}\n",
     "\tif(index >= 0) json_integer(r->json, &tokens[index], &decimals);\n"),
    ("layout_negative_decimals_accepted", T, F, DEC, DEC.replace(" || decimals < 0", "")),
    ("layout_4_decimals_accepted", T, F, DEC, DEC.replace("decimals > 3", "decimals > 4")),
    ("layout_3_decimals_refused", T, F, DEC, DEC.replace("decimals > 3", "decimals > 2")),
    ("layout_0_decimals_refused", T, F, DEC, DEC.replace("decimals < 0", "decimals < 1")),
    ("layout_decimals_compared_in_8_bit", T, F, DEC, DEC.replace("decimals < 0 || decimals > 3", "(int8_t)decimals < 0 || (uint8_t)decimals > 3")),
    ("layout_decimals_not_integer_accepted", T, F,
     DEC, "\tif(index >= 0 && json_integer(r->json, &tokens[index], &decimals) && (decimals < 0 || decimals > 3))"),
    ("layout_decimals_not_stored", T, F, "\t\titem->decimals = (uint8_t)decimals;\n", ""),
    ("layout_widget_of_any_type", T, F, WIDGET_TEXT + "\n", ""),
    ("layout_arc_unknown", T, F, ARC, "\t\tif(false) widget = LAYOUT_WIDGET_ARC;"),
    ("layout_arc_becomes_bar", T, F, ARC, ARC.replace("widget = LAYOUT_WIDGET_ARC", "widget = LAYOUT_WIDGET_BAR")),
    ("layout_bar_unknown", T, F, BAR, "\t\telse if(false) widget = LAYOUT_WIDGET_BAR;"),
    ("layout_bar_becomes_arc", T, F, BAR, BAR.replace("widget = LAYOUT_WIDGET_BAR", "widget = LAYOUT_WIDGET_ARC")),
    ("layout_state_unknown", T, F, STATE, "\t\telse if(false) widget = LAYOUT_WIDGET_STATE;"),
    ("layout_number_unknown", T, F, UNKNOWN, "\t\telse warn(r, \"widget\", \"unknown widget, shown as number\");"),
    ("layout_unknown_widget_without_warning", T, F, UNKNOWN + "\n", ""),
    ("layout_unknown_widget_refused", T, F,
     UNKNOWN, "\t\telse if(!json_text_is(r->json, &tokens[index], \"number\")) return refuse(r, \"widget\", \"unknown widget\");"),
    ("layout_widget_not_stored", T, F, "\t\titem->widget = widget;\n", ""),
    ("layout_scale_not_read", T, F, SCALE + "\n", "\tscale.set = false;\n\tscale.value = 0;\n"),
    ("layout_scale_zero_accepted", T, F, SCALE_ZERO + "\n", ""),
    ("layout_negative_scale_refused", T, F, SCALE_ZERO, SCALE_ZERO.replace("scale.value == 0", "scale.value <= 0")),
    ("layout_small_scale_refused", T, F, SCALE_ZERO, SCALE_ZERO.replace("scale.value == 0", "(scale.value < 0.01 && scale.value > -0.01)")),
    ("layout_scale_default_zero", T, F, "\t\titem->scale = scale.set ? scale.value : 1;", "\t\titem->scale = scale.value;"),
    ("layout_scale_of_minus_one_becomes_one", T, F,
     "\t\titem->scale = scale.set ? scale.value : 1;", "\t\titem->scale = scale.set && scale.value != -1 ? scale.value : 1;"),
    ("layout_scale_loses_its_sign", T, F, "\t\titem->scale = scale.set ? scale.value : 1;", "\t\titem->scale = scale.set ? fabs(scale.value) : 1;"),
    ("layout_scale_always_one", T, F, "\t\titem->scale = scale.set ? scale.value : 1;", "\t\titem->scale = 1;"),
    ("layout_min_and_max_not_read", T, F, RANGE + "\n", "\tmin.set = false;\n\tmin.value = 0;\n\tmax = min;\n"),
    ("layout_max_read_as_min", T, F, RANGE, RANGE.replace("\"max\", &max", "\"min\", &max")),
    ("layout_equal_range_accepted", T, F, RANGE_ORDER, RANGE_ORDER.replace("!(min.value < max.value)", "min.value > max.value")),
    ("layout_wrong_range_accepted", T, F, RANGE_ORDER + "\n", ""),
    ("layout_range_only_checked_for_arc_and_bar", T, F, RANGE_ORDER, RANGE_ORDER.replace("if(min.set", "if(widget != LAYOUT_WIDGET_NUMBER && min.set")),
    ("layout_range_order_named_at_max", T, F, RANGE_ORDER, RANGE_ORDER.replace("refuse(r, \"min\",", "refuse(r, \"max\",")),
    ("layout_single_min_refused", T, F, RANGE_ORDER, RANGE_ORDER.replace("min.set && max.set && ", "")),
    ("layout_min_not_stored", T, F, TAKE_MIN + "\n", ""),
    ("layout_max_not_stored", T, F, TAKE_MAX + "\n", ""),
    ("layout_max_stored_as_min", T, F, TAKE_MAX, "\t\ttake_limit(&item->max, &min);"),
    ("layout_warn_limits_not_read", T, F, WARN + "\n", "\twarn_lo.set = false;\n\twarn_lo.value = 0;\n\twarn_hi = warn_lo;\n"),
    ("layout_warn_limits_swapped", T, F, WARN, WARN.replace("\"warn_lo\", &warn_lo) || !read_number(r, object, \"warn_hi\", &warn_hi)",
                                                           "\"warn_hi\", &warn_lo) || !read_number(r, object, \"warn_lo\", &warn_hi)")),
    ("layout_crit_limits_not_read", T, F, CRIT + "\n", "\tcrit_lo.set = false;\n\tcrit_lo.value = 0;\n\tcrit_hi = crit_lo;\n"),
    ("layout_crit_read_from_warn", T, F, CRIT, CRIT.replace("\"crit_lo\"", "\"warn_lo\"").replace("\"crit_hi\"", "\"warn_hi\"")),
    ("layout_warn_lo_not_stored", T, F, TAKE_WARN_LO + "\n", ""),
    ("layout_warn_hi_not_stored", T, F, TAKE_WARN_HI + "\n", ""),
    ("layout_crit_lo_not_stored", T, F, TAKE_CRIT_LO + "\n", ""),
    ("layout_crit_hi_not_stored", T, F, TAKE_CRIT_HI + "\n", ""),
    ("layout_crit_hi_stored_as_crit_lo", T, F, TAKE_CRIT_LO, "\t\ttake_limit(&item->crit_lo, &crit_hi);"),
    # every byte: a limit taken over as a whole struct brings along what the stack held between its fields. What
    # that is depends on the compiler: these are red where the locals lie on the stack and it held something
    # (seen with clang on arm64 at every optimisation level, see test_every_byte()).
    ("layout_limits_taken_as_structs", T, F, TAKE, "\t*limit = *read;"),
    ("layout_set_limits_taken_as_structs", T, F, TAKE, "\tif(read->set) *limit = *read;"),
    ("layout_unset_limits_taken_as_structs", T, F, TAKE, "\tif(!read->set) *limit = *read;\n" + TAKE),
    ("layout_min_taken_as_struct", T, F, TAKE_MIN, "\t\titem->min = min;"),
    ("layout_max_taken_as_struct", T, F, TAKE_MAX, "\t\titem->max = max;"),
    ("layout_warn_lo_taken_as_struct", T, F, TAKE_WARN_LO, "\t\titem->warn_lo = warn_lo;"),
    ("layout_warn_hi_taken_as_struct", T, F, TAKE_WARN_HI, "\t\titem->warn_hi = warn_hi;"),
    ("layout_crit_lo_taken_as_struct", T, F, TAKE_CRIT_LO, "\t\titem->crit_lo = crit_lo;"),
    ("layout_crit_hi_taken_as_struct", T, F, TAKE_CRIT_HI, "\t\titem->crit_hi = crit_hi;"),
    ("layout_limit_taken_byte_for_byte", T, F, TAKE, "\tmemcpy(limit, read, sizeof(*limit));"),
    ("layout_text_taken_with_its_room", T, F, "\tif(out != NULL) strcpy(out, text);", "\tif(out != NULL) memcpy(out, text, size);"),
    # every byte: a layout that is cleared field by field keeps what the memory held between the fields. These
    # do not depend on the stack: the layout of the test is full of bytes that are not zero before each call.
    ("layout_parse_clears_only_the_fields", T, F, "\tmemset(layout, 0, sizeof(*layout));\n\tread_layout(&reader, layout);", "\t" + CLEAR_FIELDS + "\n\tread_layout(&reader, layout);"),
    ("layout_parse_leaves_bytes_between_fields_of_pages", T, F,
     "\tmemset(layout, 0, sizeof(*layout));\n\tread_layout(&reader, layout);", "\t" + CLEAR_PAGE_FIELDS + "\n\tread_layout(&reader, layout);"),
    ("layout_parse_leaves_bytes_between_fields_of_items", T, F,
     "\tmemset(layout, 0, sizeof(*layout));\n\tread_layout(&reader, layout);", "\t" + CLEAR_ITEM_FIELDS + "\n\tread_layout(&reader, layout);"),
    ("layout_generated_clears_only_the_fields", T, F, GENERATED_CLEAR, GENERATED_CLEAR.replace("memset(layout, 0, sizeof(*layout));", CLEAR_FIELDS)),
    ("layout_generated_leaves_bytes_between_fields_of_items", T, F, GENERATED_CLEAR, GENERATED_CLEAR.replace("memset(layout, 0, sizeof(*layout));", CLEAR_ITEM_FIELDS)),
    ("layout_limit_never_set", T, F, "\tlimit->set = read->set;\n", ""),
    ("layout_limit_without_value", T, F, "\tlimit->value = read->value;\n", ""),
    ("layout_limit_always_set", T, F, "\tlimit->set = read->set;", "\tlimit->set = true;"),
    ("layout_number_of_any_type_is_zero", T, F, NOT_A_NUMBER, "\tjson_number(r->json, &r->tokens[index], &number->value);"),
    ("layout_infinite_number_accepted", T, F, NOT_FINITE + "\n", ""),
    ("layout_infinite_called_no_number", T, F, NOT_FINITE, NOT_FINITE.replace("\"not finite\"", "\"not a number\"")),
    ("layout_large_number_refused", T, F, NOT_FINITE, NOT_FINITE.replace("!isfinite(number->value)", "!(fabs(number->value) < 1e300)")),
    ("layout_missing_number_is_set", T, F, NUMBER_SET, NUMBER_SET.replace("number->set = index >= 0;", "number->set = true;")),
    ("layout_number_never_set", T, F, NUMBER_SET, NUMBER_SET.replace("number->set = index >= 0;", "number->set = false;")),
    ("layout_missing_number_keeps_value", T, F, NUMBER_SET, NUMBER_SET.replace("\tnumber->value = 0;\n", "")),

    # an item: the map and what a widget needs
    ("layout_map_not_read", T, F, MAP, MAP.replace("member(r, object, \"map\")", "-1")),
    ("layout_map_of_any_type_is_empty", T, F, MAP_OBJECT, "\tif(r->tokens[object].type != JSON_OBJECT) return true;"),
    ("layout_map_array_accepted", T, F, MAP_OBJECT, MAP_OBJECT.replace("!= JSON_OBJECT", "!= JSON_OBJECT && r->tokens[object].type != JSON_ARRAY")),
    ("layout_9_map_entries_accepted", T, F, MAP_MANY, MAP_MANY.replace("> LAYOUT_MAP_MAX", "> LAYOUT_MAP_MAX + 1")),
    ("layout_8_map_entries_refused", T, F, MAP_MANY, MAP_MANY.replace("> LAYOUT_MAP_MAX", ">= LAYOUT_MAP_MAX")),
    ("layout_map_entries_stepped_by_one_token", T, F, MAP_LOOP, MAP_LOOP.replace("key += 2", "key += 1")),
    ("layout_map_last_entry_not_read", T, F, MAP_LOOP, MAP_LOOP.replace("r->entry < r->tokens[object].size", "r->entry < r->tokens[object].size - 1")),
    ("layout_map_value_limit_one_less", T, F, MAP_RAW, MAP_RAW.replace("LAYOUT_MAP_RAW_SIZE", "LAYOUT_MAP_RAW_SIZE - 1")),
    ("layout_map_value_limit_one_more", T, F, MAP_RAW, MAP_RAW.replace("LAYOUT_MAP_RAW_SIZE", "LAYOUT_MAP_RAW_SIZE + 1")),
    ("layout_map_value_not_checked", T, F, "\t\tif(problem != NULL) return refuse(r, \"\", problem);\n\t\tif(map != NULL) strcpy(map[r->entry].raw, text);",
     "\t\t(void)problem;\n\t\tif(map != NULL) strcpy(map[r->entry].raw, text);"),
    ("layout_map_value_not_taken_over", T, F, "\t\tif(map != NULL) strcpy(map[r->entry].raw, text);\n", ""),
    ("layout_map_text_of_any_type", T, F, MAP_TEXT_TYPE + "\n", ""),
    ("layout_map_text_limit_one_less", T, F, MAP_TEXT, MAP_TEXT.replace("LAYOUT_MAP_TEXT_SIZE", "LAYOUT_MAP_TEXT_SIZE - 1")),
    ("layout_map_text_limit_one_more", T, F, MAP_TEXT, MAP_TEXT.replace("LAYOUT_MAP_TEXT_SIZE", "LAYOUT_MAP_TEXT_SIZE + 1")),
    ("layout_map_text_is_the_value", T, F, MAP_TEXT, MAP_TEXT.replace("key + 1", "key")),
    ("layout_map_text_not_taken_over", T, F, "\t\tif(map != NULL) strcpy(map[r->entry].text, text);\n", ""),
    ("layout_map_text_not_checked", T, F,
     "\t\tif(problem != NULL) return refuse(r, \"\", problem);\n\t\tif(map != NULL) strcpy(map[r->entry].text, text);",
     "\t\t(void)problem;\n\t\tif(map != NULL) strcpy(map[r->entry].text, text);"),
    ("layout_map_count_not_set", T, F, MAP_COUNT + "\n", ""),
    ("layout_map_of_7_entries_loses_one", T, F, MAP_COUNT, "\t*count = (uint8_t)(r->tokens[object].size == 7 ? 6 : r->tokens[object].size);"),
    ("layout_map_count_at_most_one", T, F, MAP_COUNT, "\t*count = r->tokens[object].size > 0;"),
    ("layout_map_count_not_stored", T, F, "\t\titem->map_count = map_count;\n", ""),
    ("layout_arc_without_range_stays", T, F, NO_RANGE_BLOCK, ""),
    ("layout_arc_without_range_not_warned", T, F,
     "\t\twarn(r, \"widget\", \"arc or bar without min and max, shown as number\");\n", ""),
    ("layout_arc_without_range_warned_only", T, F,
     "\t\twarn(r, \"widget\", \"arc or bar without min and max, shown as number\");\n\t\twidget = LAYOUT_WIDGET_NUMBER;",
     "\t\twarn(r, \"widget\", \"arc or bar without min and max, shown as number\");"),
    ("layout_arc_with_one_end_stays", T, F, NO_RANGE, NO_RANGE.replace("!(min.set && max.set)", "!(min.set || max.set)")),
    ("layout_arc_with_min_stays", T, F, NO_RANGE, NO_RANGE.replace("!(min.set && max.set)", "!min.set")),
    ("layout_arc_with_max_stays", T, F, NO_RANGE, NO_RANGE.replace("!(min.set && max.set)", "!max.set")),
    ("layout_bar_without_range_stays", T, F, NO_RANGE, NO_RANGE.replace("(widget == LAYOUT_WIDGET_ARC || widget == LAYOUT_WIDGET_BAR)", "widget == LAYOUT_WIDGET_ARC")),
    ("layout_only_bar_needs_range", T, F, NO_RANGE, NO_RANGE.replace("(widget == LAYOUT_WIDGET_ARC || widget == LAYOUT_WIDGET_BAR)", "widget == LAYOUT_WIDGET_BAR")),
    ("layout_state_needs_range", T, F, NO_RANGE, NO_RANGE.replace("(widget == LAYOUT_WIDGET_ARC || widget == LAYOUT_WIDGET_BAR)", "widget != LAYOUT_WIDGET_NUMBER")),
    ("layout_state_without_map_stays", T, F, NO_MAP_BLOCK, ""),
    ("layout_state_without_map_not_warned", T, F, "\t\twarn(r, \"widget\", \"state without map, shown as number\");\n", ""),
    ("layout_state_without_map_warned_only", T, F,
     "\t\twarn(r, \"widget\", \"state without map, shown as number\");\n\t\twidget = LAYOUT_WIDGET_NUMBER;",
     "\t\twarn(r, \"widget\", \"state without map, shown as number\");"),
    ("layout_state_with_one_entry_becomes_number", T, F, NO_MAP, NO_MAP.replace("map_count == 0", "map_count <= 1")),
    ("layout_arc_needs_map", T, F, NO_MAP, NO_MAP.replace("widget == LAYOUT_WIDGET_STATE", "widget != LAYOUT_WIDGET_NUMBER")),

    # which pages are shown
    ("layout_catalogue_with_battery_only_is_loaded", T, F, LOADED, "\t\treturn true;"),
    ("layout_catalogue_never_loaded", T, F, LOADED, "\t\tif(false) return true;"),
    ("layout_catalogue_loaded_by_its_first_entry", T, F, LOADED, "\t\tif(strcmp(catalog->entries[0].name, CATALOG_BATTERY) != 0) return true;"),
    ("layout_catalogue_loaded_by_longer_names", T, F, LOADED, "\t\tif(strncmp(catalog->entries[i].name, CATALOG_BATTERY, strlen(CATALOG_BATTERY)) != 0) return true;"),
    ("layout_catalogue_entry_behind_last_counts", T, F,
     "\tfor(int i = 0; i < catalog->count; i++)\n\t{\n\t\tif(strcmp(catalog->entries[i].name, CATALOG_BATTERY)",
     "\tfor(int i = 0; i <= catalog->count; i++)\n\t{\n\t\tif(strcmp(catalog->entries[i].name, CATALOG_BATTERY)"),
    ("layout_page_behind_last_shown", T, F, IN_LAYOUT, IN_LAYOUT.replace("page >= layout->page_count", "page > layout->page_count")),
    ("layout_page_minus_one_shown", T, F, IN_LAYOUT, IN_LAYOUT.replace("page < 0", "page < -1")),
    ("layout_page_0_not_shown", T, F, IN_LAYOUT, IN_LAYOUT.replace("page < 0", "page <= 0")),
    ("layout_last_page_not_shown", T, F, IN_LAYOUT, IN_LAYOUT.replace("page >= layout->page_count", "page >= layout->page_count - 1")),
    ("layout_page_limit_is_that_of_the_format", T, F, IN_LAYOUT, IN_LAYOUT.replace("page >= layout->page_count", "page >= LAYOUT_PAGES_MAX")),
    ("layout_hidden_page_shown", T, F, IS_HIDDEN + "\n", ""),
    ("layout_page_7_never_shown", T, F, IS_HIDDEN, "\tif(shown->hidden || page == 7) return false;"),
    ("layout_hidden_page_shown_without_catalogue", T, F,
     IS_HIDDEN + "\n\tif(!catalog_loaded(catalog)) return true;", "\tif(!catalog_loaded(catalog)) return true;\n" + IS_HIDDEN),
    ("layout_pages_need_the_catalogue", T, F, NOT_LOADED, NOT_LOADED.replace("\tif(!catalog_loaded(catalog)) return true;\n\n", "")),
    ("layout_page_without_values_shown", T, F, ANY_KEY, ANY_KEY.replace("return false;", "return true;")),
    ("layout_page_needs_first_value", T, F, NOT_LOADED, NOT_LOADED.replace("i < shown->item_count", "i < 1")),
    ("layout_page_last_value_not_asked", T, F, NOT_LOADED, NOT_LOADED.replace("i < shown->item_count", "i < shown->item_count - 1")),
    ("layout_page_value_behind_last_asked", T, F, NOT_LOADED, NOT_LOADED.replace("i < shown->item_count", "i <= shown->item_count")),
    ("layout_page_needs_all_values", T, F, ANY_KEY, "\t\tif(catalog_find(catalog, shown->items[i].key) < 0) return false;\n\t}\n\treturn true;"),
    ("layout_first_page_is_page_0", T, F, FIRST, FIRST.replace("if(layout_page_shown(layout, i, catalog)) return i;", "(void)catalog;\n\t\treturn i;")),
    ("layout_first_page_is_the_last_shown", T, F,
     FIRST, FIRST.replace("\tfor(int i = 0; i < layout->page_count; i++)", "\tfor(int i = layout->page_count - 1; i >= 0; i--)")),
    ("layout_first_page_none_is_0", T, F, FIRST, FIRST.replace("\treturn -1;\n}\n\nint layout_step_page", "\treturn 0;\n}\n\nint layout_step_page")),
    ("layout_first_page_skips_page_10", T, F,
     FIRST, FIRST.replace("if(layout_page_shown(layout, i, catalog)) return i;", "if(i != 10 && layout_page_shown(layout, i, catalog)) return i;")),
    ("layout_first_page_skips_page_0", T, F, FIRST, FIRST.replace("for(int i = 0; i <", "for(int i = 1; i <")),

    # the knob
    ("layout_direction_0_is_back", T, F, STEP, "\tint step = direction <= 0 ? -1 : 1;"),
    ("layout_direction_not_reduced_to_its_sign", T, F, STEP, "\tint step = direction == 0 ? 1 : direction;"),
    ("layout_direction_inverted", T, F, STEP, "\tint step = direction < 0 ? 1 : -1;"),
    ("layout_always_forward", T, F, STEP, "\tint step = ((void)direction, 1);"),
    ("layout_page_far_before_not_clamped", T, F, CLAMP_LOW + "\n", ""),
    ("layout_page_far_behind_not_clamped", T, F, CLAMP_HIGH + "\n", ""),
    ("layout_page_before_clamped_to_0", T, F, CLAMP_LOW, "\tif(page < 0) page = 0;"),
    ("layout_page_behind_clamped_to_last", T, F, CLAMP_HIGH, "\tif(page >= layout->page_count) page = layout->page_count - 1;"),
    ("layout_step_wraps", T, F,
     FORWARD, "\tfor(int i = (page + step + layout->page_count) % (layout->page_count ? layout->page_count : 1), n = 0; n < layout->page_count && i >= 0; "
              "i = (i + step + layout->page_count) % layout->page_count, n++)"),
    ("layout_step_takes_next_page_shown_or_not", T, F,
     FORWARD + "\n\t{\n\t\tif(layout_page_shown(layout, i, catalog)) return i;", FORWARD + "\n\t{\n\t\treturn i;"),
    ("layout_step_looks_one_page_ahead_only", T, F, FORWARD, "\tfor(int i = page + step; i >= 0 && i < layout->page_count && i == page + step; i += step)"),
    ("layout_step_does_not_reach_last_page", T, F, FORWARD, FORWARD.replace("i < layout->page_count;", "i < layout->page_count - 1;")),
    ("layout_step_does_not_reach_page_0", T, F, FORWARD, FORWARD.replace("i >= 0 &&", "i > 0 &&")),
    ("layout_step_never_stays", T, F, STAY + "\n", ""),
    ("layout_step_stays_on_a_hidden_page", T, F, STAY, "\tif(page >= 0 && page < layout->page_count) return page;"),
    ("layout_step_stays_before_it_looks_ahead", T, F,
     FORWARD + "\n\t{\n\t\tif(layout_page_shown(layout, i, catalog)) return i;\n\t}\n" + STAY + "\n",
     STAY + "\n" + FORWARD + "\n\t{\n\t\tif(layout_page_shown(layout, i, catalog)) return i;\n\t}\n"),
    ("layout_step_skips_page_9_looking_back", T, F,
     BACKWARD + "\n\t{\n\t\tif(layout_page_shown(layout, i, catalog)) return i;", BACKWARD + "\n\t{\n\t\tif(i != 9 && layout_page_shown(layout, i, catalog)) return i;"),
    ("layout_step_skips_page_5_looking_ahead", T, F,
     FORWARD + "\n\t{\n\t\tif(layout_page_shown(layout, i, catalog)) return i;", FORWARD + "\n\t{\n\t\tif(i != 5 && layout_page_shown(layout, i, catalog)) return i;"),
    ("layout_step_never_looks_back", T, F, BACKWARD, "\tfor(int i = page - step; false; i -= step)"),
    ("layout_step_looks_back_from_the_far_end", T, F,
     BACKWARD, "\tfor(int i = step > 0 ? 0 : layout->page_count - 1; i >= 0 && i < layout->page_count && i != page; i += step)"),
    ("layout_step_none_is_0", T, F,
     BACKWARD + "\n\t{\n\t\tif(layout_page_shown(layout, i, catalog)) return i;\n\t}\n\treturn -1;",
     BACKWARD + "\n\t{\n\t\tif(layout_page_shown(layout, i, catalog)) return i;\n\t}\n\treturn 0;"),

    # layout_suits
    ("layout_suits_needs_the_catalogue", T, F, SUITS_NOT_LOADED, SUITS_NOT_LOADED.replace("\tif(!catalog_loaded(catalog)) return true;\n\n", "")),
    ("layout_suits_more_than_half", T, F, HALF, "\treturn 2 * found > distinct;"),
    ("layout_suits_not_with_half_of_30_keys", T, F, HALF, "\treturn 2 * found >= distinct + (distinct == 30);"),
    ("layout_suits_with_a_third", T, F, HALF, "\treturn 3 * found >= distinct;"),
    ("layout_suits_with_any_key", T, F, HALF, "\treturn found > 0 || distinct == 0;"),
    ("layout_suits_always", T, F, HALF, "\treturn 2 * found >= distinct || distinct > 0;"),
    ("layout_suits_counts_every_item", T, F, REPEATED, "\t\t\t(void)key_repeated(layout, p, i);"),
    ("layout_suits_counts_found_keys_again", T, F,
     REPEATED + "\n\t\t\tdistinct++;\n" + FOUND, FOUND + "\n" + REPEATED + "\n\t\t\tdistinct++;"),
    ("layout_suits_repeats_only_on_the_same_page", T, F, REPEAT_PAGES, "\tfor(int p = page; p <= page; p++)"),
    ("layout_suits_repeats_only_on_other_pages", T, F, REPEAT_PAGES, "\tfor(int p = 0; p < page; p++)"),
    ("layout_suits_repeat_looks_at_first_item_only", T, F, REPEAT_COUNT, "\t\tint count = p < page ? 1 : item;"),
    ("layout_suits_repeat_by_beginning", T, F, REPEAT_SAME, "\t\t\tif(strncmp(layout->pages[p].items[i].key, key, strlen(key)) == 0) return true;"),
    ("layout_suits_repeat_by_beginning_of_the_one_before", T, F,
     REPEAT_SAME, "\t\t\tif(strncmp(layout->pages[p].items[i].key, key, strlen(layout->pages[p].items[i].key)) == 0) return true;"),
    ("layout_suits_ignores_hidden_pages", T, F, REPEATED, "\t\t\tif(layout->pages[p].hidden || key_repeated(layout, p, i)) continue;"),
    ("layout_suits_last_item_of_a_page_not_counted", T, F,
     SUITS_NOT_LOADED, SUITS_NOT_LOADED.replace("i < layout->pages[p].item_count", "i < layout->pages[p].item_count - 1")),
    ("layout_suits_first_page_only", T, F, SUITS_NOT_LOADED, SUITS_NOT_LOADED.replace("p < layout->page_count", "p < 1")),
    ("layout_suits_page_behind_last_counted", T, F, SUITS_NOT_LOADED, SUITS_NOT_LOADED.replace("p < layout->page_count", "p <= layout->page_count")),
    ("layout_suits_found_never_counted", T, F, FOUND + "\n", ""),

    # layout_from_catalog
    ("layout_generated_takes_every_name", T, F, USABLE, "\t(void)usable_key(entry->name);"),
    ("layout_generated_takes_empty_names", T, F, USABLE_EMPTY, "\tfor(; *name != '\\0'; name++)"),
    ("layout_generated_takes_control_characters", T, F, USABLE_CONTROL, "\t\t(void)c;"),
    ("layout_generated_takes_unit_separator", T, F, USABLE_CONTROL, USABLE_CONTROL.replace("c < 0x20", "c < 0x1F")),
    ("layout_generated_refuses_space", T, F, USABLE_CONTROL, USABLE_CONTROL.replace("c < 0x20", "c <= 0x20")),
    ("layout_generated_takes_delete", T, F, USABLE_CONTROL, USABLE_CONTROL.replace(" || c == 0x7F", "")),
    ("layout_generated_refuses_high_bytes", T, F, USABLE_CONTROL, USABLE_CONTROL.replace("c == 0x7F", "c >= 0x7F")),
    ("layout_generated_looks_at_first_byte_only", T, F, USABLE_CONTROL + "\n\t}\n\treturn true;", USABLE_CONTROL + "\n\t\tbreak;\n\t}\n\treturn true;"),
    ("layout_generated_5_per_page", T, F, "#define ITEMS_PER_PAGE  4", "#define ITEMS_PER_PAGE  5"),
    ("layout_generated_3_per_page", T, F, "#define ITEMS_PER_PAGE  4", "#define ITEMS_PER_PAGE  3"),
    ("layout_generated_one_page_only", T, F, NEW_PAGE, "\tif(layout->page_count == 0)"),
    ("layout_generated_page_per_item", T, F, NEW_PAGE, "\tif(true)"),
    ("layout_generated_13_pages", T, F, NO_ROOM, "\t\tif(layout->page_count == LAYOUT_PAGES_MAX + 1) return;"),
    ("layout_generated_11_pages", T, F, NO_ROOM, "\t\tif(layout->page_count == LAYOUT_PAGES_MAX - 1) return;"),
    ("layout_generated_no_title", T, F, PAGE_TITLE + "\n", ""),
    ("layout_generated_titles_count_from_0", T, F, PAGE_TITLE, PAGE_TITLE.replace("layout->page_count);", "layout->page_count - 1);")),
    ("layout_generated_title_other_word", T, F, PAGE_TITLE, PAGE_TITLE.replace("Werte %d", "Seite %d")),
    ("layout_generated_no_key", T, F, GENERATED_KEY + "\n", ""),
    ("layout_generated_no_label", T, F, GENERATED_LABEL + "\n", ""),
    ("layout_generated_label_is_the_name", T, F, GENERATED_LABEL, "\tif(strlen(entry->name) < sizeof(item->label)) strcpy(item->label, entry->name);"),
    ("layout_generated_cut_label_left_behind", T, F, GENERATED_LABEL, "\tfmt_label(entry->name, item->label, sizeof(item->label));"),
    ("layout_generated_label_one_byte_shorter", T, F, GENERATED_LABEL, GENERATED_LABEL.replace("item->label, sizeof(item->label)))", "item->label, sizeof(item->label) - 1))")),
    ("layout_generated_no_decimals", T, F, GENERATED_DECIMALS, "\titem->scale = 1;"),
    ("layout_generated_two_decimals", T, F, GENERATED_DECIMALS, "\titem->decimals = 2;\n\titem->scale = 1;"),
    ("layout_generated_scale_zero", T, F, GENERATED_DECIMALS, "\titem->decimals = 1;"),
    ("layout_generated_has_unit", T, F, GENERATED_DECIMALS, GENERATED_DECIMALS + "\n\titem->has_unit = true;"),
    ("layout_generated_takes_unit_of_catalogue", T, F,
     GENERATED_DECIMALS, GENERATED_DECIMALS + "\n\tif(strlen(entry->unit) < sizeof(item->unit)) strcpy(item->unit, entry->unit);"),
    ("layout_generated_not_cleared", T, F, GENERATED_CLEAR, "\tlayout->page_count = 0;\n\tfor(int i = 0; i < catalog->count; i++)"),
    ("layout_generated_battery_in_its_place", T, F, OTHERS, "\t\tadd_item(layout, &catalog->entries[i]);"),
    ("layout_generated_battery_twice", T, F, BATTERY_LAST, "\tif(battery >= 0) add_item(layout, &catalog->entries[battery]);\n\tif(battery > 0) add_item(layout, &catalog->entries[battery]);"),
    ("layout_generated_battery_left_out", T, F, BATTERY_LAST + "\n", ""),
    ("layout_generated_battery_only_if_first", T, F, BATTERY_LAST, "\tif(battery == 0) add_item(layout, &catalog->entries[battery]);"),
    ("layout_generated_battery_is_entry_0", T, F, BATTERY_FIND, "\tint battery = catalog->count > 0 ? 0 : -1;"),
    ("layout_generated_last_entry_left_out", T, F, GENERATED_CLEAR, GENERATED_CLEAR.replace("i < catalog->count", "i < catalog->count - 1")),
    ("layout_generated_entry_behind_last_taken", T, F, GENERATED_CLEAR, GENERATED_CLEAR.replace("i < catalog->count", "i <= catalog->count && i < CATALOG_MAX")),

    # the state of an item
    ("layout_fresh_value_is_old", T, F, LIVE, LIVE.replace("return LAYOUT_ITEM_LIVE;", "return LAYOUT_ITEM_OLD;")),
    ("layout_old_value_is_live", T, F, OLD, OLD.replace("return LAYOUT_ITEM_OLD;", "return LAYOUT_ITEM_LIVE;")),
    ("layout_old_value_is_gone", T, F, OLD + "\n", ""),
    ("layout_old_value_is_live_for_some_keys", T, F, OLD, OLD.replace("return LAYOUT_ITEM_OLD;", "return item->key[0] == 'Q' ? LAYOUT_ITEM_LIVE : LAYOUT_ITEM_OLD;")),
    ("layout_state_at_time_0", T, F, "\tswitch(values_age(values_find(values, item->key), now_ms))", "\t(void)now_ms;\n\tswitch(values_age(values_find(values, item->key), 0))"),
    ("layout_value_needs_the_catalogue", T, F,
     "\tswitch(values_age(values_find(values, item->key), now_ms))",
     "\tswitch(values_age(catalog_find(catalog, item->key) >= 0 ? values_find(values, item->key) : NULL, now_ms))"),
    ("layout_missing_key_always_waits", T, F, WAITING, "\tif(catalog != NULL) return LAYOUT_ITEM_NO_VALUE;"),
    ("layout_missing_key_unavailable_before_catalogue", T, F, WAITING, WAITING.replace("!catalog_loaded(catalog) || ", "")),
    ("layout_known_key_unavailable", T, F, WAITING, WAITING.replace(" || catalog_find(catalog, item->key) >= 0", "")),
    ("layout_no_value_and_unavailable_swapped", T, F, WAITING, WAITING.replace("if(!catalog_loaded(catalog) || catalog_find(catalog, item->key) >= 0)", "if(catalog_loaded(catalog) && catalog_find(catalog, item->key) < 0)")),

    # the text of an item
    ("layout_text_without_room_written", T, F, TEXT_NO_ROOM, TEXT_NO_ROOM.replace("\tif(size == 0) return false;\n", "")),
    ("layout_text_without_value_not_emptied", T, F, TEXT_NO_ROOM, "\tif(size == 0) return false;\n\tif(value == NULL) return false;\n\tout[0] = '\\0';"),
    ("layout_text_without_value_read", T, F, TEXT_NO_ROOM, TEXT_NO_ROOM.replace("\n\tif(value == NULL) return false;", "")),
    ("layout_text_without_value_true", T, F, TEXT_NO_ROOM, TEXT_NO_ROOM.replace("if(value == NULL) return false;", "if(value == NULL) return true;")),
    ("layout_text_scale_ignored", T, F, AS_NUMBER, AS_NUMBER.replace("number_of(value) * item->scale", "number_of(value)")),
    ("layout_text_divided_by_scale", T, F, AS_NUMBER, AS_NUMBER.replace("number_of(value) * item->scale", "number_of(value) / item->scale")),
    ("layout_text_decimals_above_3_are_none", T, F, AS_NUMBER, AS_NUMBER.replace("item->decimals, out, size", "item->decimals > 3 ? 0 : item->decimals, out, size")),
    ("layout_text_decimals_taken_modulo_4", T, F, AS_NUMBER, AS_NUMBER.replace("item->decimals, out, size", "item->decimals & 3, out, size")),
    ("layout_text_decimals_ignored", T, F, AS_NUMBER, AS_NUMBER.replace("item->decimals, out, size", "0, out, size")),
    ("layout_text_one_decimal_more", T, F, AS_NUMBER, AS_NUMBER.replace("item->decimals, out, size", "item->decimals + 1, out, size")),
    ("layout_text_room_one_less", T, F, AS_NUMBER, AS_NUMBER.replace("out, size);", "out, size - 1);")),
    ("layout_on_is_0", T, F, NUMBER_OF, NUMBER_OF.replace("== VALUE_ON ? 1 : 0", "== VALUE_ON ? 0 : 1")),
    ("layout_off_is_1", T, F, NUMBER_OF, NUMBER_OF.replace("== VALUE_ON ? 1 : 0", "== VALUE_ON ? 1 : 1")),
    ("layout_switch_shows_its_number_field", T, F, NUMBER_OF, "\treturn value->number;"),
    ("layout_number_is_0", T, F, NUMBER_OF, NUMBER_OF.replace("return value->number;", "return 0;")),
    ("layout_unknown_widget_asks_the_map", T, F, IS_STATE, IS_STATE.replace("item->widget == LAYOUT_WIDGET_STATE", "item->widget >= LAYOUT_WIDGET_STATE")),
    ("layout_unknown_kind_counts_as_on", T, F, NUMBER_OF, NUMBER_OF.replace("== VALUE_ON ? 1 : 0", "== VALUE_OFF ? 0 : 1")),
    ("layout_unknown_kind_named_on", T, F, RAW_SWITCH, "\t\tstrcpy(out, value->kind == VALUE_OFF ? \"off\" : \"on\");"),
    ("layout_unknown_kind_named_by_its_number", T, F,
     "\tif(value->kind != VALUE_NUMBER)\n\t{\n\t\tstrcpy(out,", "\tif(value->kind == VALUE_ON || value->kind == VALUE_OFF)\n\t{\n\t\tstrcpy(out,"),
    ("layout_generated_cut_label_keeps_its_last_byte", T, F,
     GENERATED_LABEL, GENERATED_LABEL.replace("memset(item->label, 0, sizeof(item->label));", "memset(item->label, 0, sizeof(item->label) - 1);")),
    ("layout_arc_asks_the_map", T, F, IS_STATE, IS_STATE.replace("item->widget == LAYOUT_WIDGET_STATE", "item->widget != LAYOUT_WIDGET_NUMBER")),
    ("layout_every_widget_asks_the_map", T, F, IS_STATE, IS_STATE.replace("item->widget == LAYOUT_WIDGET_STATE", "true")),
    ("layout_state_never_asks_the_map", T, F, IS_STATE, IS_STATE.replace("item->widget == LAYOUT_WIDGET_STATE", "false")),
    ("layout_state_value_not_looked_up", T, F, BY_VALUE, "\t\t(void)raw_text(value, raw, sizeof(raw));"),
    ("layout_state_star_not_looked_up", T, F, BY_STAR + "\n", ""),
    ("layout_state_star_only_without_scale", T, F, BY_STAR, "\t\tif(entry == NULL && item->scale == 1) entry = map_entry(item, \"*\");"),
    ("layout_state_star_only_without_decimals", T, F, BY_STAR, "\t\tif(entry == NULL && item->decimals == 0) entry = map_entry(item, \"*\");"),
    ("layout_state_value_only_without_scale", T, F, BY_VALUE, BY_VALUE.replace("if(raw_text(", "if(item->scale == 1 && raw_text(")),
    ("layout_state_value_only_without_decimals", T, F, BY_VALUE, BY_VALUE.replace("if(raw_text(", "if(item->decimals == 0 && raw_text(")),
    ("layout_state_star_before_value", T, F, BY_VALUE + "\n" + BY_STAR, "\t\tentry = map_entry(item, \"*\");\n\t\tif(entry == NULL && raw_text(value, raw, sizeof(raw))) entry = map_entry(item, raw);"),
    ("layout_state_star_only_for_printable_values", T, F, BY_VALUE + "\n" + BY_STAR,
     "\t\tif(raw_text(value, raw, sizeof(raw)))\n\t\t{\n\t\t\tentry = map_entry(item, raw);\n\t\t\tif(entry == NULL) entry = map_entry(item, \"*\");\n\t\t}"),
    ("layout_state_unprintable_value_is_the_empty_one", T, F, BY_VALUE, "\t\traw_text(value, raw, sizeof(raw));\n\t\tentry = map_entry(item, raw);"),
    ("layout_state_room_for_values_one_less", T, F, BY_VALUE, BY_VALUE.replace("sizeof(raw))", "sizeof(raw) - 1)")),
    ("layout_state_text_room_one_less", T, F, ENTRY_FITS, ENTRY_FITS.replace("+ 1 > size", "+ 2 > size")),
    ("layout_state_text_room_not_checked", T, F, ENTRY_FITS + "\n", ""),
    ("layout_state_text_cut", T, F, ENTRY_FITS + "\n\t\t\tstrcpy(out, entry->text);", "\t\t\tsnprintf(out, size, \"%s\", entry->text);"),
    ("layout_state_text_too_long_shows_number", T, F, ENTRY_FITS, "\t\t\tif(strlen(entry->text) + 1 > size) return fmt_number(number_of(value) * item->scale, item->decimals, out, size);"),
    ("layout_state_text_not_written", T, F, "\t\t\tstrcpy(out, entry->text);\n", ""),
    ("layout_state_text_returns_false", T, F, "\t\t\tstrcpy(out, entry->text);\n\t\t\treturn true;", "\t\t\tstrcpy(out, entry->text);\n\t\t\treturn false;"),
    ("layout_state_fifth_entry_not_asked", T, F,
     ENTRY_LOOP, ENTRY_LOOP.replace("if(strcmp(item->map[i].raw, raw) == 0) return", "if(i != 4 && strcmp(item->map[i].raw, raw) == 0) return")),
    ("layout_state_first_entry_only", T, F, ENTRY_LOOP, ENTRY_LOOP.replace("i < item->map_count", "i < item->map_count && i < 1")),
    ("layout_state_last_entry_not_asked", T, F, ENTRY_LOOP, ENTRY_LOOP.replace("i < item->map_count", "i < item->map_count - 1")),
    ("layout_state_entry_behind_last_asked", T, F, ENTRY_LOOP, ENTRY_LOOP.replace("i < item->map_count", "i <= item->map_count && i < LAYOUT_MAP_MAX")),
    ("layout_state_last_of_equal_entries", T, F, ENTRY_LOOP, ENTRY_LOOP.replace("for(int i = 0; i < item->map_count; i++)", "for(int i = item->map_count - 1; i >= 0; i--)")),
    ("layout_state_value_by_beginning", T, F, ENTRY_LOOP, ENTRY_LOOP.replace("strcmp(item->map[i].raw, raw) == 0", "strncmp(item->map[i].raw, raw, strlen(raw)) == 0")),
    ("layout_state_value_by_beginning_of_entry", T, F,
     ENTRY_LOOP, ENTRY_LOOP.replace("strcmp(item->map[i].raw, raw) == 0", "strncmp(item->map[i].raw, raw, strlen(item->map[i].raw)) == 0")),
    ("layout_state_on_is_1", T, F, RAW_SWITCH, "\t\tstrcpy(out, value->kind == VALUE_ON ? \"1\" : \"0\");"),
    ("layout_state_on_and_off_swapped", T, F, RAW_SWITCH, "\t\tstrcpy(out, value->kind == VALUE_ON ? \"off\" : \"on\");"),
    ("layout_state_off_is_on", T, F, RAW_SWITCH, "\t\tstrcpy(out, \"on\");"),
    ("layout_state_switch_named_by_number", T, F, "\tif(value->kind != VALUE_NUMBER)\n\t{\n\t\tstrcpy(out,", "\tif(false)\n\t{\n\t\tstrcpy(out,"),
    ("layout_state_number_with_2_decimals", T, F, RAW_NUMBER, RAW_NUMBER.replace("value->number, 3, out", "value->number, 2, out")),
    ("layout_state_number_with_1_decimal", T, F, RAW_NUMBER, RAW_NUMBER.replace("value->number, 3, out", "value->number, 1, out")),
    ("layout_state_zeros_kept", T, F, RAW_ZEROS + "\n", ""),
    ("layout_state_one_zero_removed", T, F, RAW_ZEROS, "\tif(out[length - 1] == '0') length--;"),
    ("layout_state_comma_kept", T, F, RAW_COMMA + "\n", ""),
    ("layout_state_zeros_before_comma_removed", T, F, RAW_COMMA, "\tif(out[length - 1] == ',') length--;\n\twhile(length > 1 && out[length - 1] == '0') length--;"),
    ("layout_state_number_not_cut", T, F, RAW_END, RAW_END.replace("\tout[length] = '\\0';\n", "")),
    ("layout_state_scaled_value_looked_up", T, F,
     BY_VALUE, "\t\tvalue_t scaled = *value;\n\n\t\tscaled.number *= item->scale;\n\t\tif(raw_text(&scaled, raw, sizeof(raw))) entry = map_entry(item, raw);"),

    # the level of an item
    ("layout_level_without_value_read", T, F, LEVEL_NO_VALUE + "\n", ""),
    ("layout_level_without_value_is_2", T, F, LEVEL_NO_VALUE, "\tif(value == NULL) return 2;"),
    ("layout_level_scale_ignored", T, F, LEVEL_NUMBER, "\tnumber = number_of(value);"),
    ("layout_level_divided_by_scale", T, F, LEVEL_NUMBER, "\tnumber = number_of(value) / item->scale;"),
    ("layout_level_no_crit", T, F, LEVEL_CRIT + "\n", ""),
    ("layout_level_no_warn", T, F, LEVEL_WARN + "\n", ""),
    ("layout_level_crit_is_1", T, F, LEVEL_CRIT, LEVEL_CRIT.replace("return 2;", "return 1;")),
    ("layout_level_warn_is_2", T, F, LEVEL_WARN, LEVEL_WARN.replace("return 1;", "return 2;")),
    ("layout_level_warn_before_crit", T, F, LEVEL_CRIT + "\n" + LEVEL_WARN, LEVEL_WARN + "\n" + LEVEL_CRIT),
    ("layout_level_crit_uses_warn_lo", T, F, LEVEL_CRIT, LEVEL_CRIT.replace("&item->crit_lo", "&item->warn_lo")),
    ("layout_level_crit_uses_warn_hi", T, F, LEVEL_CRIT, LEVEL_CRIT.replace("&item->crit_hi", "&item->warn_hi")),
    ("layout_level_warn_uses_crit_lo", T, F, LEVEL_WARN, LEVEL_WARN.replace("&item->warn_lo", "&item->crit_lo")),
    ("layout_level_warn_uses_crit_hi", T, F, LEVEL_WARN, LEVEL_WARN.replace("&item->warn_hi", "&item->crit_hi")),
    ("layout_level_low_and_high_swapped", T, F, LEVEL_WARN, LEVEL_WARN.replace("&item->warn_lo, &item->warn_hi", "&item->warn_hi, &item->warn_lo")),
    ("layout_level_at_lower_limit_is_fine", T, F, BEYOND, BEYOND.replace("number <= low->value", "number < low->value")),
    ("layout_level_at_upper_limit_is_fine", T, F, BEYOND, BEYOND.replace("number >= high->value", "number > high->value")),
    ("layout_level_unset_lower_limit_counts", T, F, BEYOND, BEYOND.replace("(low->set && number <= low->value)", "(number <= low->value)")),
    ("layout_level_unset_upper_limit_counts", T, F, BEYOND, BEYOND.replace("(high->set && number >= high->value)", "(number >= high->value)")),
    ("layout_level_no_lower_limit", T, F, BEYOND, "\treturn low != NULL && high->set && number >= high->value;"),
    ("layout_level_no_upper_limit", T, F, BEYOND, "\treturn high != NULL && low->set && number <= low->value;"),
    ("layout_level_warn_ignored_for_one_scale", T, F,
     LEVEL_WARN, LEVEL_WARN.replace("if(beyond(", "if(!(item->crit_lo.set && item->crit_hi.set && item->warn_lo.set && item->warn_hi.set && item->scale == 3) && beyond(")),
    ("layout_level_needs_both_limits", T, F, BEYOND, BEYOND.replace(") || (", ") && (")),
    ("layout_level_upper_needs_lower_set", T, F, BEYOND, BEYOND.replace("(high->set && number", "(low->set && high->set && number")),

    # the unit of an item
    ("layout_unit_always_from_catalogue", T, F, OWN_UNIT + "\n", ""),
    ("layout_unit_of_8_bytes_from_the_catalogue", T, F, OWN_UNIT, "\tif(item->has_unit && strlen(item->unit) < 8) return item->unit;"),
    ("layout_unit_own_only_if_not_empty", T, F, OWN_UNIT, "\tif(item->has_unit && item->unit[0] != '\\0') return item->unit;"),
    ("layout_unit_own_whenever_there_is_text", T, F, OWN_UNIT, "\tif(item->has_unit || item->unit[0] != '\\0') return item->unit;"),
    ("layout_unit_of_first_entry", T, F, CATALOG_UNIT, "\treturn index < 0 ? \"\" : catalog->entries[0].unit;"),
    ("layout_unit_missing_key_takes_first_entry", T, F, CATALOG_UNIT, "\treturn catalog->entries[index < 0 ? 0 : index].unit;"),
    ("layout_unit_is_the_class", T, F, CATALOG_UNIT, "\treturn index < 0 ? \"\" : catalog->entries[index].value_class;"),

    # layout_to_json: room
    ("layout_written_behind_the_room", T, F, PUT_CHAR, PUT_CHAR.replace("writer->length < writer->size", "writer->length <= writer->size")),
    ("layout_written_without_looking_at_the_room", T, F, PUT_CHAR, "\tif(writer->size > 0) writer->out[writer->length] = c;\n\twriter->length++;"),
    ("layout_written_without_room_for_the_zero", T, F, W_NO_ROOM, W_NO_ROOM.replace("writer.length >= size", "writer.length > size")),
    ("layout_written_needs_two_bytes_more", T, F, W_NO_ROOM, W_NO_ROOM.replace("writer.length >= size", "writer.length + 1 >= size")),
    ("layout_written_too_long_not_emptied", T, F, W_NO_ROOM, W_NO_ROOM.replace("\t\tout[0] = '\\0';\n", "")),
    ("layout_written_too_long_returns_length", T, F, W_NO_ROOM, W_NO_ROOM.replace("\t\tout[0] = '\\0';\n\t\treturn -1;", "\t\tout[0] = '\\0';\n\t\treturn (int)writer.length;")),
    ("layout_written_without_room_writes_zero", T, F, W_NO_ROOM, W_NO_ROOM.replace("\tif(size == 0) return -1;\n", "")),
    ("layout_written_failed_number_ignored_with_little_room", T, F,
     W_NO_ROOM, W_NO_ROOM.replace("writer.failed || writer.length >= size", "(writer.failed && size > 100) || writer.length >= size")),
    ("layout_written_failed_number_ignored", T, F, W_NO_ROOM, W_NO_ROOM.replace("writer.failed || ", "")),
    ("layout_written_text_of_74_bytes_does_not_fit", T, F, W_NO_ROOM, W_NO_ROOM.replace("writer.length >= size", "writer.length >= size || writer.length == 74")),
    ("layout_written_long_text_without_zero", T, F, W_END, W_END.replace("out[writer.length] = '\\0';", "if(writer.length < 100000) out[writer.length] = '\\0';")),
    ("layout_written_without_zero", T, F, W_END, "\treturn (int)writer.length;"),
    ("layout_written_returns_length_with_zero", T, F, W_END, W_END.replace("return (int)writer.length;", "return (int)writer.length + 1;")),
    ("layout_written_returns_0", T, F, W_END, W_END.replace("return (int)writer.length;", "return 0;")),

    # layout_to_json: texts
    ("layout_written_control_characters_as_they_are", T, F, ESCAPE_CONTROL, "\t\t(void)hex;\n"),
    ("layout_written_unit_separator_as_it_is", T, F, ESCAPE_CONTROL, ESCAPE_CONTROL.replace("c < 0x20", "c < 0x1F")),
    ("layout_written_space_escaped", T, F, ESCAPE_CONTROL, ESCAPE_CONTROL.replace("c < 0x20", "c <= 0x20")),
    ("layout_written_delete_escaped", T, F, ESCAPE_CONTROL, ESCAPE_CONTROL.replace("c < 0x20", "c < 0x20 || c == 0x7F")),
    ("layout_written_high_bytes_escaped", T, F, ESCAPE_CONTROL, ESCAPE_CONTROL.replace("c < 0x20", "c < 0x20 || c >= 0x80")),
    ("layout_written_escape_in_upper_case", T, F, "\tstatic const char hex[] = \"0123456789abcdef\";", "\tstatic const char hex[] = \"0123456789ABCDEF\";"),
    ("layout_written_escape_digits_swapped", T, F,
     ESCAPE_CONTROL, ESCAPE_CONTROL.replace("put_char(writer, hex[c >> 4]);\n\t\t\tput_char(writer, hex[c & 0x0F]);", "put_char(writer, hex[c & 0x0F]);\n\t\t\tput_char(writer, hex[c >> 4]);")),
    ("layout_written_escape_with_one_zero_less", T, F, ESCAPE_CONTROL, ESCAPE_CONTROL.replace("\"\\\\u00\"", "\"\\\\u0\"")),
    ("layout_written_escape_keeps_the_byte", T, F, ESCAPE_CONTROL, ESCAPE_CONTROL.replace("\t\t\tcontinue;\n", "")),
    ("layout_written_quote_not_escaped", T, F, ESCAPE, ESCAPE.replace("c == '\"' || ", "")),
    ("layout_written_backslash_not_escaped", T, F, ESCAPE, ESCAPE.replace(" || c == '\\\\'", "")),
    ("layout_written_slash_escaped", T, F, ESCAPE, ESCAPE.replace("c == '\"' ||", "c == '\"' || c == '/' ||")),
    ("layout_written_text_without_opening_quote", T, F,
     "\tput_char(writer, '\"');\n\tfor(; *text != '\\0'; text++)\n\t{\n\t\tunsigned char c", "\tfor(; *text != '\\0'; text++)\n\t{\n\t\tunsigned char c"),
    ("layout_written_text_without_closing_quote", T, F,
     "\t\tput_char(writer, *text);\n\t}\n\tput_char(writer, '\"');", "\t\tput_char(writer, *text);\n\t}"),

    # layout_to_json: numbers
    ("layout_written_decimals_last_digit_only", T, F, SMALL + "\n\t}\n\twhile(number > 0);", SMALL + "\n\t}\n\twhile(false);"),
    ("layout_written_decimals_digits_reversed", T, F,
     "\twhile(length > 0) put_char(writer, reversed[--length]);", "\tfor(int i = 0; i < length; i++) put_char(writer, reversed[i]);"),
    ("layout_written_number_always_15_digits", T, F, FIFTEEN, "\tsnprintf(text, sizeof(text), \"%.15g\", number);\n\tif(false)"),
    ("layout_written_number_always_17_digits", T, F, FIFTEEN, "\tsnprintf(text, sizeof(text), \"%.15g\", number);\n\tif(true)"),
    ("layout_written_number_16_digits", T, F, SEVENTEEN, SEVENTEEN.replace("%.17g", "%.16g")),
    ("layout_written_number_14_digits_first", T, F, FIFTEEN, FIFTEEN.replace("%.15g", "%.14g")),
    ("layout_written_number_16_digits_first", T, F, FIFTEEN, FIFTEEN.replace("%.15g", "%.16g")),
    ("layout_written_number_with_fixed_decimals", T, F, FIFTEEN, FIFTEEN.replace("%.15g", "%.15f")),
    ("layout_written_number_not_finite_written", T, F, FAILED, "\t\t\twriter->failed = false;"),
    ("layout_written_number_not_finite_left_out", T, F, FAILED, "\t\t\treturn;"),
    ("layout_written_number_read_back_as_any_number", T, F, READS_BACK, READS_BACK.replace("back == number", "(back == number || back != number)")),
    ("layout_written_number_read_back_roughly", T, F, READS_BACK, READS_BACK.replace("back == number", "fabs(back - number) <= fabs(number) * 1e-14")),
    ("layout_written_number_not_read_back_as_json", T, F,
     READS_BACK, "\treturn json_parse(text, strlen(text), &token, 1) != JSON_TOO_MANY && sscanf(text, \"%lf\", &back) == 1 && back == number;"),
    ("layout_written_unset_limit_written", T, F, LIMIT_SET + "\n", ""),
    ("layout_written_limit_of_0_left_out", T, F, LIMIT_SET, "\tif(!limit->set || limit->value == 0) return;"),

    # layout_to_json: the members of an item
    ("layout_written_empty_label_written", T, F, W_LABEL, W_LABEL.replace("if(item->label[0] != '\\0') ", "")),
    ("layout_written_label_left_out", T, F, W_LABEL + "\n", ""),
    ("layout_written_label_behind_unit", T, F, W_LABEL + "\n" + W_UNIT, W_UNIT + "\n" + W_LABEL),
    ("layout_written_unit_always", T, F, W_UNIT, W_UNIT.replace("if(item->has_unit) ", "")),
    ("layout_written_unit_only_if_not_empty", T, F, W_UNIT, W_UNIT.replace("if(item->has_unit)", "if(item->has_unit && item->unit[0] != '\\0')")),
    ("layout_written_unit_whenever_there_is_text", T, F, W_UNIT, W_UNIT.replace("if(item->has_unit)", "if(item->has_unit || item->unit[0] != '\\0')")),
    ("layout_written_unit_left_out", T, F, W_UNIT + "\n", ""),
    ("layout_written_unit_left_out_by_its_first_byte", T, F, W_UNIT, W_UNIT.replace("if(item->has_unit)", "if(item->has_unit && item->unit[0] != '#')")),
    ("layout_written_label_left_out_by_its_first_byte", T, F, W_LABEL, W_LABEL.replace("if(item->label[0] != '\\0')", "if(item->label[0] != '\\0' && item->label[0] != '#')")),
    ("layout_written_scale_1_written", T, F, W_SCALE, W_SCALE.replace("if(item->scale != 1)", "if(true)")),
    ("layout_written_scale_left_out", T, F, W_SCALE, ""),
    ("layout_written_negative_scale_left_out", T, F, W_SCALE, W_SCALE.replace("item->scale != 1)", "item->scale != 1 && item->scale != -1)")),
    ("layout_written_scale_near_1_left_out", T, F, W_SCALE, W_SCALE.replace("item->scale != 1)", "fabs(item->scale - 1) > 1e-9)")),
    ("layout_written_scale_behind_decimals", T, F, W_SCALE + W_DEC, W_DEC + W_SCALE),
    ("layout_written_decimals_0_written", T, F, W_DEC, W_DEC.replace("if(item->decimals != 0)", "if(true)")),
    ("layout_written_decimals_left_out", T, F, W_DEC, ""),
    ("layout_written_decimals_above_3_left_out", T, F, W_DEC, W_DEC.replace("item->decimals != 0)", "item->decimals != 0 && item->decimals <= 3)")),
    ("layout_written_arc_left_out", T, F, W_ARC + "\n", ""),
    ("layout_written_arc_as_bar", T, F, W_ARC, W_ARC.replace("\\\"arc\\\"", "\\\"bar\\\"")),
    ("layout_written_bar_left_out", T, F, W_BAR + "\n", ""),
    ("layout_written_state_left_out", T, F, W_STATE + "\n", ""),
    ("layout_written_state_only_with_a_map", T, F, W_STATE, W_STATE.replace("case LAYOUT_WIDGET_STATE: put(", "case LAYOUT_WIDGET_STATE: if(item->map_count > 0) put(")),
    ("layout_written_arc_only_with_a_range", T, F, W_ARC, W_ARC.replace("case LAYOUT_WIDGET_ARC:   put(", "case LAYOUT_WIDGET_ARC:   if(item->min.set && item->max.set) put(")),
    ("layout_written_bar_only_with_a_range", T, F, W_BAR, W_BAR.replace("case LAYOUT_WIDGET_BAR:   put(", "case LAYOUT_WIDGET_BAR:   if(item->min.set && item->max.set) put(")),
    ("layout_written_number_widget_written", T, F,
     "\t\tdefault:                  break;", "\t\tcase LAYOUT_WIDGET_NUMBER: put(writer, \",\\\"widget\\\":\\\"number\\\"\"); break;\n\t\tdefault:                  break;"),
    ("layout_written_unknown_widget_as_state", T, F,
     W_STATE + "\n\t\t// A number widget is what the reader makes of an item without the member\n\t\tdefault:                  break;",
     "\t\tcase LAYOUT_WIDGET_NUMBER: break;\n\t\tdefault:                  put(writer, \",\\\"widget\\\":\\\"state\\\"\"); break;"),
    ("layout_written_widget_behind_range", T, F, W_WIDGET + W_MIN + "\n" + W_MAX + "\n", W_MIN + "\n" + W_MAX + "\n" + W_WIDGET),
    ("layout_written_widget_before_decimals", T, F, W_DEC + W_WIDGET, W_WIDGET + W_DEC),
    ("layout_written_min_left_out", T, F, W_MIN + "\n", ""),
    ("layout_written_max_left_out", T, F, W_MAX + "\n", ""),
    ("layout_written_min_as_max", T, F, W_MIN, W_MIN.replace("&item->min", "&item->max")),
    ("layout_written_max_as_min", T, F, W_MAX, W_MAX.replace("&item->max", "&item->min")),
    ("layout_written_max_before_min", T, F, W_MIN + "\n" + W_MAX, W_MAX + "\n" + W_MIN),
    ("layout_written_warn_lo_left_out", T, F, W_WARN_LO + "\n", ""),
    ("layout_written_warn_hi_left_out", T, F, W_WARN_HI + "\n", ""),
    ("layout_written_crit_lo_left_out", T, F, W_CRIT_LO + "\n", ""),
    ("layout_written_crit_hi_left_out", T, F, W_CRIT_HI + "\n", ""),
    ("layout_written_warn_limits_swapped", T, F, W_WARN_LO + "\n" + W_WARN_HI, W_WARN_HI + "\n" + W_WARN_LO),
    ("layout_written_warn_hi_from_crit_hi", T, F, W_WARN_HI, W_WARN_HI.replace("&item->warn_hi", "&item->crit_hi")),
    ("layout_written_crit_lo_from_warn_lo", T, F, W_CRIT_LO, W_CRIT_LO.replace("&item->crit_lo", "&item->warn_lo")),
    ("layout_written_crit_before_warn", T, F,
     W_WARN_LO + "\n" + W_WARN_HI + "\n" + W_CRIT_LO + "\n" + W_CRIT_HI, W_CRIT_LO + "\n" + W_CRIT_HI + "\n" + W_WARN_LO + "\n" + W_WARN_HI),
    ("layout_written_empty_map_written", T, F, W_MAP, "\tif(true)"),
    ("layout_written_map_left_out", T, F, W_MAP, "\tif(false)"),
    ("layout_written_map_of_one_entry_left_out", T, F, W_MAP, "\tif(item->map_count > 1)"),
    ("layout_written_map_only_of_state", T, F, W_MAP, "\tif(item->map_count > 0 && item->widget == LAYOUT_WIDGET_STATE)"),
    ("layout_written_map_first_entry_only", T, F, W_MAP_LOOP, W_MAP_LOOP.replace("i < item->map_count", "i < 1")),
    ("layout_written_map_last_entry_left_out", T, F, W_MAP_LOOP, W_MAP_LOOP.replace("i < item->map_count", "i < item->map_count - 1")),
    ("layout_written_map_all_eight_entries", T, F, W_MAP_LOOP, W_MAP_LOOP.replace("i < item->map_count", "i < LAYOUT_MAP_MAX")),
    ("layout_written_map_seventh_entry_left_out", T, F,
     W_MAP_LOOP, W_MAP_LOOP.replace("i < item->map_count; i++)", "i < item->map_count; i += 1 + (i == 5 && item->map_count == 7))")),
    ("layout_written_map_without_commas", T, F, W_MAP_LOOP, W_MAP_LOOP.replace("\t\t\tif(i > 0) put_char(writer, ',');\n", "")),
    ("layout_written_map_first_value_everywhere", T, F, W_MAP_LOOP, W_MAP_LOOP.replace("item->map[i].raw", "item->map[0].raw")),
    ("layout_written_map_text_is_the_value", T, F, W_MAP_TEXT, W_MAP_TEXT.replace("item->map[i].text", "item->map[i].raw")),
    ("layout_written_map_in_reverse", T, F,
     W_MAP_LOOP, W_MAP_LOOP.replace("for(int i = 0; i < item->map_count; i++)", "for(int i = item->map_count - 1; i >= 0; i--)").replace("if(i > 0)", "if(i < item->map_count - 1)")),

    # layout_to_json: layout and pages
    ("layout_written_version_left_out", T, F, W_HEAD, W_HEAD.replace("\n\tput_small(&writer, LAYOUT_VERSION);", "\n\tput_char(&writer, '0');")),
    ("layout_written_empty_name_written", T, F, W_NAME, W_NAME.replace("if(layout->name[0] != '\\0') ", "")),
    ("layout_written_name_left_out", T, F, W_NAME + "\n", ""),
    ("layout_written_name_left_out_if_it_is_the_hint", T, F,
     W_NAME, W_NAME.replace("layout->name[0] != '\\0'", "layout->name[0] != '\\0' && strcmp(layout->name, layout->profile_hint) != 0")),
    ("layout_written_name_is_the_hint", T, F, W_NAME, W_NAME.replace("\", layout->name);", "\", layout->profile_hint);")),
    ("layout_written_empty_hint_written", T, F, W_HINT, W_HINT.replace("if(layout->profile_hint[0] != '\\0') ", "")),
    ("layout_written_hint_left_out", T, F, W_HINT + "\n", ""),
    ("layout_written_hint_before_name", T, F, W_NAME + "\n" + W_HINT, W_HINT + "\n" + W_NAME),
    ("layout_written_hint_only_with_name", T, F, W_HINT, W_HINT.replace("if(layout->profile_hint[0] != '\\0')", "if(layout->profile_hint[0] != '\\0' && layout->name[0] != '\\0')")),
    ("layout_written_first_page_only", T, F, W_PAGES, W_PAGES.replace("p < layout->page_count", "p < layout->page_count && p < 1")),
    ("layout_written_last_page_left_out", T, F, W_PAGES, W_PAGES.replace("p < layout->page_count", "p < layout->page_count - 1")),
    ("layout_written_page_behind_last", T, F, W_PAGES, W_PAGES.replace("p < layout->page_count", "p <= layout->page_count && p < LAYOUT_PAGES_MAX")),
    ("layout_written_pages_without_commas", T, F, W_PAGES, W_PAGES.replace("\t\tif(p > 0) put_char(&writer, ',');\n", "")),
    ("layout_written_first_page_everywhere", T, F, W_PAGES, W_PAGES.replace("&layout->pages[p]", "&layout->pages[0]")),
    ("layout_written_empty_title_written", T, F, W_TITLE, W_TITLE.replace("\tif(page->title[0] != '\\0')\n", "")),
    ("layout_written_title_left_out", T, F, W_TITLE, ""),
    ("layout_written_title_behind_hidden", T, F, W_TITLE + W_HIDDEN + "\n", W_HIDDEN + "\n" + W_TITLE),
    ("layout_written_hidden_left_out", T, F, W_HIDDEN + "\n", ""),
    ("layout_written_hidden_false_written", T, F, W_HIDDEN, "\tput(writer, page->hidden ? \"\\\"hidden\\\":true,\" : \"\\\"hidden\\\":false,\");"),
    ("layout_written_hidden_inverted", T, F, W_HIDDEN, W_HIDDEN.replace("if(page->hidden)", "if(!page->hidden)")),
    ("layout_written_first_item_only", T, F, W_ITEMS, W_ITEMS.replace("i < page->item_count", "i < page->item_count && i < 1")),
    ("layout_written_last_item_left_out", T, F, W_ITEMS, W_ITEMS.replace("i < page->item_count", "i < page->item_count - 1")),
    ("layout_written_item_behind_last", T, F, W_ITEMS, W_ITEMS.replace("i < page->item_count", "i <= page->item_count && i < LAYOUT_ITEMS_MAX")),
    ("layout_written_items_without_commas", T, F, W_ITEMS, W_ITEMS.replace("\t\tif(i > 0) put_char(writer, ',');\n", "")),
    ("layout_written_first_item_everywhere", T, F, W_ITEMS, W_ITEMS.replace("&page->items[i]", "&page->items[0]")),
]
