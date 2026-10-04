"""Mutations of display/components/core/scene.c, see ../redproof.py."""

F = "components/core/scene.c"
H = "components/core/scene.h"
T = "test_scene"

# append() and the numbers
ROOM = "\tsize_t room = size - 1 - used;"
NO_TEXT = "\tif(text == NULL) return;\n"
CUT = "\t\tlength = room;\n"
BOUNDARY = "\t\twhile(length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80) length--;\n"
DIGITS = "\tdo\n\t{\n\t\tdigits[--first] = (char)('0' + rest % 10);\n\t\trest /= 10;\n\t}\n\twhile(rest > 0);\n"
MAGNITUDE = "\tuint64_t rest = number < 0 ? 0 - (uint64_t)number : (uint64_t)number;"
MINUS = "\tif(number < 0) digits[--first] = '-';\n"
MINUTES = "\tappend_number(out, size, seconds / 60);\n"
TENS = "\tappend_number(out, size, seconds % 60 / 10);\n"
ONES = "\tappend_number(out, size, seconds % 10);\n"

# The range of an arc or a bar
RANGE = "\tif(!item->min.set || !item->max.set || !(item->min.value < item->max.value)) return -1;"
SHOWN = "\tshown = (value->kind == VALUE_NUMBER ? value->number : value->kind == VALUE_ON ? 1 : 0) * item->scale;"
PART = "\tpart = (shown - item->min.value) * 1000 / span;"
FINITE = "\tif(!isfinite(span) || !isfinite(part)) return -1;"
LOW = "\tif(part <= 0) return 0;\n"
HIGH = "\tif(part >= 1000) return 1000;\n"

# One value
IS_SHOWN = "\tbool shown = (state == LAYOUT_ITEM_LIVE || state == LAYOUT_ITEM_OLD) && layout_item_text(item, value, out->text, sizeof(out->text));"
LABEL = "\tif(item->label[0] != '\\0') append(out->label, sizeof(out->label), item->label);\n"
MADE_LABEL = "\telse if(!fmt_label(item->key, out->label, sizeof(out->label))) memset(out->label, 0, sizeof(out->label));"
WIDGET = "\tif(item->widget == LAYOUT_WIDGET_ARC || item->widget == LAYOUT_WIDGET_BAR || item->widget == LAYOUT_WIDGET_STATE) out->widget = item->widget;"
NOT_SHOWN = "\t\tappend(out->text, sizeof(out->text), state == LAYOUT_ITEM_UNAVAILABLE ? SCENE_UNAVAILABLE : SCENE_DASH);\n\t\treturn;"
IF_NOT_SHOWN = "\tif(!shown)\n\t{\n"
MISSED = "\t\tif(state != LAYOUT_ITEM_UNAVAILABLE) *old = true;\n"
LEVEL = "\tif(item_level > *level) *level = item_level;\n"
OLD = "\tif(state == LAYOUT_ITEM_OLD) *old = true;\n"
LIVE_TONE = "\tif(state == LAYOUT_ITEM_LIVE && view != CONN_VIEW_SCAN)"
TONE = "\t\tout->tone = item_level >= 2 ? SCENE_TONE_ALARM : item_level == 1 ? SCENE_TONE_WARN : SCENE_TONE_NORMAL;"
UNIT = "\tif(out->widget != LAYOUT_WIDGET_STATE) append(out->unit, sizeof(out->unit), layout_item_unit(item, catalog));"
PERMILLE = "\tif(out->widget == LAYOUT_WIDGET_ARC || out->widget == LAYOUT_WIDGET_BAR) out->permille = range_permille(item, value);"

# The value pages
DOT_SHOWN = "\t\tif(!layout_page_shown(layout, i, world->catalog)) continue;\n"
DOT = "\t\tif(i == shown) scene->dot = scene->dots;\n"
NOTE_SAFE = "\tif(input->safe_mode) set_note(scene, \"Sicherer Modus – eingebaute Ansichten\");\n"
NOTE_HEAT = "\telse if(input->heat != GUARD_HEAT_NORMAL) set_note(scene, \"Zu heiß – Anzeige gedimmt\");\n"
NOTE_VIEW = "\telse if(view == CONN_VIEW_SCAN || view == CONN_VIEW_NO_API) set_note(scene, text_view(view));\n"
VALUE_VIEWS = "\tif(view != CONN_VIEW_LIVE && view != CONN_VIEW_SCAN && view != CONN_VIEW_NO_API)"
BATTERY = "\t\tif(view == CONN_VIEW_ECU_OFFLINE && state->batt_mv >= 0)"
TENTHS = "\t\t\tuint32_t tenths = ((uint32_t)state->batt_mv + 50) / 100;"
NO_PAGE = "\tif(shown < 0 || shown >= layout->page_count)"
VIEW_BLOCK_HEAD = "\tif(view != CONN_VIEW_LIVE && view != CONN_VIEW_SCAN && view != CONN_VIEW_NO_API)\n\t{\n"

# Lists
TOTAL = "\tint total = lines != NULL || texts != NULL ? nav_rows(nav, input->world) : choice_count;"
FIRST = "\tint64_t first = (int64_t)nav->row - SCENE_ROWS_MAX / 2;"
FIRST_HIGH = "\tif(first > total - SCENE_ROWS_MAX) first = total - SCENE_ROWS_MAX;\n"
FIRST_LOW = "\tif(first < 0) first = 0;\n"
ROWS_LOOP = "\tfor(int index = scene->first; index < total && scene->row_count < SCENE_ROWS_MAX; index++)"
ROW_FOCUS = "\t\trow->focus = index == nav->row;"
IS_CHOICE = "\t\tif(index >= line_count)"
LINE_HEAD = "\t\t\tif(lines[index].kind == DTC_LINE_HEAD) row->kind = SCENE_ROW_HEAD;\n"
LINE_CODE = "\t\t\tif(lines[index].kind == DTC_LINE_CODE) row->kind = SCENE_ROW_SUB;\n"

# The menu and the fault memory
MENU_NIGHT = "\t\t{\"Nachtmodus\", world->night_mode ? \"an\" : \"aus\", true},\n"
MENU_WEB = "\t\t{\"Web-Zugriff\", world->release_open ? \"frei\" : \"gesperrt\", true},\n"
OUTCOME = "\tbool outcome = world->flow == DTC_FLOW_LIST || world->flow == DTC_FLOW_CLEARED || world->flow == DTC_FLOW_FAILED || world->flow == DTC_FLOW_UNKNOWN;"
DTC_READ = "\t\t{\"Lesen\", \"\", world->can_read},\n"
DTC_VIEW = "\t\t{\"Liste ansehen\", \"\", outcome},\n"
DTC_OLD = "\t\t{\"Zuletzt gelöscht\", \"\", world->old_lines > 0},\n"

# The progress
ACCEPTED = "\tbool accepted = flow->phase == DTC_FLOW_READING || flow->phase == DTC_FLOW_CLEARING;"
READING = "\tbool reading = flow->phase == DTC_FLOW_READ_SENT || flow->phase == DTC_FLOW_READING;"
UNDER_WAY = "\tif(!reading && flow->phase != DTC_FLOW_CLEAR_SENT && flow->phase != DTC_FLOW_CLEARING)"
OWN = "\tif(accepted && state != NULL && state->dtc.seq == flow->seq && (state->dtc.phase == WICAN_DTC_RUNNING || state->dtc.phase == WICAN_DTC_DONE))"
ENGINE = "\t\tif(dtc->step == 0) add_text(scene, \"Prüfe Motor …\");\n"
SHORT_NAME = "\t\telse dtc_short_name(dtc->name, add_line(scene), SCENE_TEXT_SIZE);"
STEP_PERMILLE = "\t\tif(dtc->total != 0) scene->permille = dtc->step >= dtc->total ? 1000 : (int)((uint64_t)dtc->step * 1000u / dtc->total);"
HINT = "\tadd_text(scene, \"ca. 35 s – Live-Werte pausieren\");\n"

# The list
LIST_READ = "\t\t{\"Erneut lesen\", \"\", world->can_read},\n"
LIST_CLEAR = "\t\t{\"Fehler löschen\", \"\", world->can_clear},\n"
LIST_NOTE = "\tif(world->can_clear)\n\t{\n\t\tset_note(scene, \"Löschen möglich: \");"
LIST_TIME = "\t\tappend_time(scene->note, sizeof(scene->note), dtc_flow_seconds_left(input->flow, input->now_ms));\n"
LIST_BLOCK = "\t\tset_note(scene, text_block(input->clear_block));"

# What a list holds, for the fault memory and the clear dialog
CODES = "\tappend_number(line, SCENE_TEXT_SIZE, summary->codes);\n"
WITH_CODES = "\tappend_number(line, SCENE_TEXT_SIZE, summary->ecus_with_codes);"
UNITS = "\tappend(line, SCENE_TEXT_SIZE, summary->ecus_with_codes == 1 ? \" Steuergerät\" : \" Steuergeräten\");"

# Where the fault memory stands
STAND = "\tswitch(input->flow->phase)\n"
STAND_READ = "\t\tcase DTC_FLOW_READ_SENT:\n\t\tcase DTC_FLOW_READING:\n\t\t\tadd_text(scene, \"Lesen läuft …\");\n\t\t\tbreak;\n"
STAND_LIST = ("\t\tcase DTC_FLOW_LIST:\n\t\t\tif(input->summary != NULL) add_summary(scene, input->summary);\n"
              "\t\t\telse add_text(scene, \"Liste gelesen\");\n\t\t\tbreak;\n")
STAND_SUMMARY = "\t\t\tif(input->summary != NULL) add_summary(scene, input->summary);\n\t\t\telse add_text(scene, \"Liste gelesen\");\n"
STAND_CLEAR = "\t\tcase DTC_FLOW_CLEAR_SENT:\n\t\tcase DTC_FLOW_CLEARING:\n\t\t\tadd_text(scene, \"Löschen läuft …\");\n\t\t\tbreak;\n"
STAND_CLEARED = "\t\tcase DTC_FLOW_CLEARED:\n\t\t\tadd_text(scene, \"Gelöscht\");\n\t\t\tbreak;\n"
STAND_FAILED = "\t\tcase DTC_FLOW_FAILED:\n\t\t\tadd_text(scene, \"Letzter Auftrag fehlgeschlagen\");\n\t\t\tbreak;\n"
STAND_UNKNOWN = "\t\tcase DTC_FLOW_UNKNOWN:\n\t\t\tadd_text(scene, \"Stand des Löschens unbekannt\");\n\t\t\tbreak;\n"
STAND_IDLE = "\t\tdefault:\n\t\t\tadd_text(scene, \"Noch nicht gelesen\");\n\t\t\tbreak;\n"
STAND_END = STAND_IDLE + "\t}\n"

# The dialogs
OPTION = "\tscene->option = input->nav->row > 0 ? 1 : 0;"
SUMMARY = "\tif(input->summary != NULL) add_summary(scene, input->summary);\n\tadd_text(scene, \"Betrifft alle"
WARN_ALL = "\tadd_text(scene, \"Betrifft alle Steuergeräte, auch SRS und ESP.\");\n"
WARN_ENGINE = "\tadd_text(scene, \"Zündung an, Motor aus, Fahrzeug steht.\");\n"
HOLD = "\tscene->permille = hold_permille(input->hold, input->now_ms);\n"

# The failure
WORD = "\t\tif(text == NULL) text = flow->reason;\n"
SLEEPING = "\t\tif(strcmp(flow->reason, \"not_ready\") == 0 && state != NULL && state->sleep_in_s == 0) text = \"WiCAN schaltet ab – später erneut lesen\";"
FAILED = "\tif(flow->phase == DTC_FLOW_FAILED)\n"
UNKNOWN = "\telse if(flow->phase == DTC_FLOW_UNKNOWN)\n"

# Brightness, web access, settings
LEVEL_VALUE = "\tint64_t permille = (int64_t)input->nav->value * 10;"
LEVEL_LOW = "\tif(permille < 0) permille = 0;\n"
LEVEL_HIGH = "\tif(permille > 1000) permille = 1000;\n"
LEVEL_TITLE = "\tset_title(scene, input->world->night_mode ? \"Helligkeit (Nacht)\" : \"Helligkeit\");"
LEVEL_BIG = "\tappend_percent(scene->big, sizeof(scene->big), input->nav->value);"
RELEASE = "\tuint32_t seconds = access_seconds_left(input->access, input->now_ms);"
RELEASE_ON = "\tif(seconds > 0)\n"
HAS_ADDRESS = "\tbool has_address = input->address != NULL && input->address[0] != '\\0';"
ADDRESS = "\tif(has_address) add_text(scene, input->address);\n"
NO_NETWORK = "\telse if(!input->ap_on) add_text(scene, \"Kein WLAN\");\n"
AP = "\tif(input->ap_on)\n\t{\n"
AP_NAME = "\t\tappend(line, SCENE_TEXT_SIZE, \"WLAN: \");\n\t\tappend(line, SCENE_TEXT_SIZE, input->ap_ssid);\n"
AP_PASSWORD = "\t\tappend(line, SCENE_TEXT_SIZE, \"Passwort: \");\n\t\tappend(line, SCENE_TEXT_SIZE, input->ap_password);\n"
SET_REVERSE = "\t\t{\"Drehrichtung\", input->reverse ? \"umgekehrt\" : \"normal\", true},\n"
SET_AP = "\t\t{\"Hotspot\", input->ap_on ? \"an\" : \"aus\", true},\n"
SET_PREVIOUS = "\t\t{\"Vorherige Version\", \"\", input->world->previous_firmware},\n"
ASK_REBOOT = "\t\tcase NAV_DO_REBOOT:\n\t\t\tset_title(scene, \"Neu starten?\");\n\t\t\tbreak;\n"
ASK_PREVIOUS = "\t\tcase NAV_DO_PREVIOUS_FIRMWARE:\n\t\t\tset_title(scene, \"Vorherige Version starten?\");\n\t\t\tbreak;\n"
ASK_RESET_ERASED = "\t\t\tadd_text(scene, \"WLAN, Kopplung und Einstellungen werden gelöscht.\");\n"
ASK_RESET_KEPT = "\t\t\tadd_text(scene, \"Die Ansichten bleiben.\");\n"

# What lies over the screen
UPLOAD_LOW = "\t\t\tif(percent < 0) percent = 0;\n"
UPLOAD_HIGH = "\t\t\tif(percent > 100) percent = 100;\n"
UPLOAD_TEXT = "\t\t\tadd_over_text(scene, \"Firmware wird übertragen\");\n"
UPLOAD_PERCENT = "\t\t\tappend_percent(add_over_line(scene), SCENE_TEXT_SIZE, percent);\n"
UPLOAD_PERMILLE = "\t\t\tscene->over_permille = percent * 10;\n"
QUESTION = ("\t\t\tadd_over_text(scene, asking == ACCESS_ASK_WIFI ? \"WLAN speichern?\" : asking == ACCESS_ASK_FIRMWARE ? \"Firmware installieren?\" :\n"
            "\t\t\t              asking == ACCESS_ASK_RESET ? \"Werkseinstellungen?\" : \"\");\n")
DETAIL = "\t\t\tif(input->ask_detail != NULL && input->ask_detail[0] != '\\0') add_over_text(scene, input->ask_detail);\n"
ANSWER = "\t\t\tappend(line, SCENE_TEXT_SIZE, \"Drücken = ja · lang = nein (\");\n"
ASK_LEFT = "\t\t\tappend_number(line, SCENE_TEXT_SIZE, access_ask_seconds_left(input->access, input->now_ms));\n"
UPDATE_1 = "\t\t\tadd_over_text(scene, \"Update in Ordnung?\");\n"
UPDATE_2 = "\t\t\tadd_over_text(scene, \"Knopf drücken oder Bildschirm berühren\");\n"
UPDATE_LEFT = "\t\t\tappend_time(line, SCENE_TEXT_SIZE, input->update_left_s);\n"

# scene_build()
CLEAR = "\tmemset(scene, 0, sizeof(*scene));\n"
RING = "\tscene->ring = ring_state(view, state, level, old);"
OWN_ARC = "\tif((input->nav->screen == NAV_DTC_BUSY || input->nav->screen == NAV_DTC_CONFIRM) && scene->ring.kind == RING_PROGRESS)"
RING_OFF = "\t\tscene->ring.kind = RING_NONE;\n"
OVERLAY = "\tbuild_overlay(input, scene);\n"
CLEARED = "\t\t\tset_title(scene, \"Gelöscht\");\n\t\t\tbuild_rows(input, input->cleared, NULL, done, COUNT(done), scene);"
OLD_LIST = "\t\t\tset_title(scene, \"Zuletzt gelöscht\");\n\t\t\tbuild_rows(input, input->old, NULL, back, COUNT(back), scene);"
INFO = "\t\t\tset_title(scene, \"Info\");\n\t\t\tbuild_rows(input, NULL, input->info, NULL, 0, scene);"
NO_SCREEN = "\t\tdefault:\n\t\t\tscene->kind = SCENE_NOTICE;\n\t\t\tbreak;"

# scene_dump()
PUT = "\tif(writer->length < writer->size) writer->out[writer->length] = c;\n"
FIELD = "\tfor(size_t i = 0; i < size && text[i] != '\\0'; i++) put_char(writer, text[i]);"
BLANK = "\tif(text[0] != '\\0') put_char(writer, ' ');\n"
FLAG = "\treturn *(const unsigned char *)field != 0;"
WORD_OF = "\treturn member < (unsigned)count ? words[member] : \"?\";"
WITHIN = "\treturn count > max ? max : count;"
RING_PERMILLE = "\tif(scene->ring.kind == RING_PROGRESS)\n"
DOTS = "\tif(scene->dots != 0 || scene->dot != -1)\n"
DOT_NUMBER = "\t\tput_number(&writer, (int64_t)scene->dot + 1);\n"
ROW_MARK = "\t\tput(&writer, flag(&row->focus) ? \"row: > \" : \"row: - \");"
ROW_ENABLED = "\t\tput(&writer, flag(&row->enabled) ? \" | enabled\\n\" : \" | disabled\\n\");"
WINDOW = "\tif(scene->first != 0 || scene->total != 0)\n"
LINES = "\tfor(int i = 0; i < within(scene->line_count, SCENE_LINES_MAX); i++) put_text_line(&writer, \"line:\", scene->lines[i], sizeof(scene->lines[i]));"
BIG = "\tif(scene->big[0] != '\\0') put_text_line(&writer, \"big:\", scene->big, sizeof(scene->big));"
SCENE_PERMILLE = "\tif(scene->permille != -1) put_number_line(&writer, \"permille:\", scene->permille);"
OPTIONS = "\tif(scene->options[0][0] != '\\0' || scene->options[1][0] != '\\0')\n"
OPTION_LINE = "\t\tfor(int i = 0; i < 2; i++) put_text_line(&writer, scene->option == i ? \"option: >\" : \"option: -\", scene->options[i], sizeof(scene->options[i]));"
OVER = "\tif(scene->over != SCENE_OVER_NONE)\n"
OVER_PERMILLE = "\tif(scene->over_permille != -1) put_number_line(&writer, \"over_permille:\", scene->over_permille);"
NO_ROOM = "\tif(size == 0) return -1;\n"
FITS = "\tif(writer.length >= size)\n"
EMPTY = "\t\tout[0] = '\\0';\n\t\treturn -1;"
END = "\tout[writer.length] = '\\0';\n\treturn (int)writer.length;"


def text(name, old, new):
    """A text of the screen that reads differently"""
    return (name, T, F, old, new)


MUTATIONS = [
    # the sizes and the texts the header names
    ("scene_h_text_one_byte_less", T, H, "#define SCENE_TEXT_SIZE     96", "#define SCENE_TEXT_SIZE     95"),
    ("scene_h_text_one_byte_more", T, H, "#define SCENE_TEXT_SIZE     96", "#define SCENE_TEXT_SIZE     97"),
    ("scene_h_short_one_byte_less", T, H, "#define SCENE_SHORT_SIZE    40", "#define SCENE_SHORT_SIZE    39"),
    ("scene_h_short_one_byte_more", T, H, "#define SCENE_SHORT_SIZE    40", "#define SCENE_SHORT_SIZE    41"),
    ("scene_h_value_one_byte_less", T, H, "#define SCENE_VALUE_SIZE    24", "#define SCENE_VALUE_SIZE    23"),
    ("scene_h_value_one_byte_more", T, H, "#define SCENE_VALUE_SIZE    24", "#define SCENE_VALUE_SIZE    25"),
    ("scene_h_six_rows", T, H, "#define SCENE_ROWS_MAX      5 ", "#define SCENE_ROWS_MAX      6 "),
    ("scene_h_five_lines", T, H, "#define SCENE_LINES_MAX     4", "#define SCENE_LINES_MAX     5"),
    ("scene_h_dash_is_a_hyphen", T, H, "#define SCENE_DASH          \"–\"", "#define SCENE_DASH          \"-\""),
    ("scene_h_unavailable_without_blank", T, H, "#define SCENE_UNAVAILABLE   \"n. v.\"", "#define SCENE_UNAVAILABLE   \"n.v.\""),
    ("scene_h_dump_size_smaller", T, H, "#define SCENE_DUMP_SIZE     4096", "#define SCENE_DUMP_SIZE     2734"),
    ("scene_h_dump_size_larger", T, H, "#define SCENE_DUMP_SIZE     4096", "#define SCENE_DUMP_SIZE     8192"),

    # texts that do not fit, and texts that are not there
    ("scene_text_fills_the_room_of_its_zero", T, F, ROOM, "\tsize_t room = size - used;"),
    ("scene_text_one_byte_short", T, F, ROOM, "\tsize_t room = size - 2 - used;"),
    ("scene_text_null_is_read", T, F, NO_TEXT, ""),
    ("scene_text_too_long_is_left_out", T, F, CUT, "\t\tlength = 0;\n"),
    ("scene_text_cut_one_byte_early", T, F, CUT, "\t\tlength = room > 0 ? room - 1 : 0;\n"),
    ("scene_text_cut_inside_a_character", T, F, BOUNDARY, ""),
    ("scene_text_cut_before_a_first_byte", T, F, BOUNDARY, "\t\twhile(length > 0 && (unsigned char)text[length] >= 0x80) length--;\n"),
    ("scene_text_cut_only_inside_two_bytes", T, F, BOUNDARY, "\t\tif(length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80) length--;\n"),
    ("scene_text_cut_looks_at_the_byte_before", T, F,
     BOUNDARY, "\t\twhile(length > 0 && ((unsigned char)text[length - 1] & 0xC0) == 0x80) length--;\n"),
    ("scene_text_cut_reads_before_the_text", T, F, BOUNDARY, "\t\twhile(((unsigned char)text[length] & 0xC0) == 0x80) length--;\n"),
    ("scene_text_second_part_replaces_the_first", T, F, "\tmemcpy(&out[used], text, length);\n\tout[used + length] = '\\0';",
     "\tmemcpy(out, text, length);\n\tout[length] = '\\0';"),

    # numbers
    ("scene_number_zero_has_no_digit", T, F, DIGITS, "\twhile(rest > 0)\n\t{\n\t\tdigits[--first] = (char)('0' + rest % 10);\n\t\trest /= 10;\n\t}\n"),
    ("scene_number_last_digit_only", T, F, DIGITS, "\tdigits[--first] = (char)('0' + rest % 10);\n"),
    ("scene_number_negative_not_turned", T, F, MAGNITUDE, "\tuint64_t rest = (uint64_t)number;"),
    ("scene_number_without_minus", T, F, MINUS, ""),
    ("scene_number_always_minus", T, F, MINUS, "\tdigits[--first] = '-';\n"),
    ("scene_number_zero_with_minus", T, F, MINUS, "\tif(number <= 0) digits[--first] = '-';\n"),
    ("scene_percent_without_blank", T, F, "\tappend(out, size, \" %\");", "\tappend(out, size, \"%\");"),
    ("scene_percent_without_sign", T, F, "\tappend(out, size, \" %\");", ""),
    ("scene_time_minutes_end_at_an_hour", T, F, MINUTES, "\tappend_number(out, size, seconds / 60 % 60);\n"),
    ("scene_time_minutes_rounded_up", T, F, MINUTES, "\tappend_number(out, size, (seconds + 59) / 60);\n"),
    ("scene_time_without_colon", T, F, "\tappend(out, size, \":\");\n", ""),
    ("scene_time_seconds_one_digit", T, F, TENS, "\tif(seconds % 60 >= 10) append_number(out, size, seconds % 60 / 10);\n"),
    ("scene_time_tens_of_all_seconds", T, F, TENS, "\tappend_number(out, size, seconds / 10 % 10);\n"),
    ("scene_time_without_tens", T, F, TENS, ""),
    ("scene_time_without_ones", T, F, ONES, ""),
    ("scene_time_seconds_not_of_a_minute", T, F,
     TENS + ONES, "\tappend_number(out, size, seconds % 100 / 10);\n" + ONES),

    # the range of an arc and a bar
    ("scene_range_without_min", T, F, RANGE, "\tif(!item->max.set || !(item->min.value < item->max.value)) return -1;"),
    ("scene_range_without_max", T, F, RANGE, "\tif(!item->min.set || !(item->min.value < item->max.value)) return -1;"),
    ("scene_range_min_above_max", T, F, RANGE, "\tif(!item->min.set || !item->max.set) return -1;"),
    ("scene_range_needs_one_limit", T, F, RANGE, "\tif((!item->min.set && !item->max.set) || !(item->min.value < item->max.value)) return -1;"),
    ("scene_range_raw_value", T, F, SHOWN, "\tshown = value->kind == VALUE_NUMBER ? value->number : value->kind == VALUE_ON ? 1 : 0;"),
    ("scene_range_on_is_zero", T, F, SHOWN, "\tshown = (value->kind == VALUE_NUMBER ? value->number : 0) * item->scale;"),
    ("scene_range_off_is_one", T, F, SHOWN, "\tshown = (value->kind == VALUE_NUMBER ? value->number : 1) * item->scale;"),
    ("scene_range_on_and_off_swapped", T, F, SHOWN, "\tshown = (value->kind == VALUE_NUMBER ? value->number : value->kind == VALUE_ON ? 0 : 1) * item->scale;"),
    ("scene_range_scale_not_for_on", T, F,
     SHOWN, "\tshown = value->kind == VALUE_NUMBER ? value->number * item->scale : value->kind == VALUE_ON ? 1 : 0;"),
    ("scene_range_unknown_kind_is_its_number", T, F,
     SHOWN, "\tshown = (value->kind == VALUE_ON ? 1 : value->kind == VALUE_OFF ? 0 : value->number) * item->scale;"),
    ("scene_range_counted_from_zero", T, F, PART, "\tpart = shown * 1000 / span;"),
    ("scene_range_divided_by_max", T, F, PART, "\tpart = (shown - item->min.value) * 1000 / item->max.value;"),
    ("scene_range_in_percent", T, F, PART, "\tpart = (shown - item->min.value) * 100 / span;"),
    ("scene_range_divided_first", T, F, PART, "\tpart = (shown - item->min.value) / span * 1000;"),
    ("scene_range_width_not_looked_at", T, F, FINITE, "\tif(!isfinite(part)) return -1;"),
    ("scene_range_part_not_looked_at", T, F, FINITE, "\tif(!isfinite(span)) return -1;"),
    ("scene_range_beyond_double_is_empty", T, F, FINITE, "\tif(!isfinite(span) || !isfinite(part)) return 0;"),
    ("scene_range_below_min_not_clamped", T, F, LOW, ""),
    ("scene_range_above_max_not_clamped", T, F, HIGH, ""),
    ("scene_range_full_one_early", T, F, HIGH, "\tif(part >= 999) return 1000;\n"),
    ("scene_range_first_permille_is_one", T, F, LOW, "\tif(part <= 0) return 0;\n\tif(part < 1) return 1;\n"),
    ("scene_range_rounded_to_nearest", T, F, "\treturn (int)part;", "\treturn (int)(part + 0.5);"),
    ("scene_range_rounded_up", T, F, "\treturn (int)part;", "\treturn (int)part + ((double)(int)part < part ? 1 : 0);"),

    # one value
    ("scene_item_gone_value_shown", T, F, IS_SHOWN, "\tbool shown = layout_item_text(item, value, out->text, sizeof(out->text));"),
    ("scene_item_old_value_is_a_dash", T, F,
     IS_SHOWN, "\tbool shown = state == LAYOUT_ITEM_LIVE && layout_item_text(item, value, out->text, sizeof(out->text));"),
    ("scene_item_fresh_value_is_a_dash", T, F,
     IS_SHOWN, "\tbool shown = state == LAYOUT_ITEM_OLD && layout_item_text(item, value, out->text, sizeof(out->text));"),
    ("scene_item_label_always_from_key", T, F, LABEL + "\telse if", "\tif"),
    ("scene_item_label_never_from_key", T, F, LABEL + MADE_LABEL, "\tappend(out->label, sizeof(out->label), item->label);"),
    ("scene_item_label_too_long_leaves_bytes", T, F, MADE_LABEL, "\telse fmt_label(item->key, out->label, sizeof(out->label));"),
    ("scene_item_label_too_long_is_the_key", T, F,
     MADE_LABEL, "\telse if(!fmt_label(item->key, out->label, sizeof(out->label)))\n\t{\n\t\tmemset(out->label, 0, sizeof(out->label));\n"
     "\t\tappend(out->label, sizeof(out->label), item->key);\n\t}"),
    ("scene_item_label_one_byte_less_room", T, F,
     MADE_LABEL, "\telse if(!fmt_label(item->key, out->label, sizeof(out->label) - 1)) memset(out->label, 0, sizeof(out->label));"),
    ("scene_item_widget_passed_on", T, F, "\tout->widget = LAYOUT_WIDGET_NUMBER;\n" + WIDGET, "\tout->widget = item->widget;"),
    ("scene_item_widget_by_low_byte", T, F,
     "\tout->widget = LAYOUT_WIDGET_NUMBER;\n" + WIDGET, "\tout->widget = (layout_widget_t)((unsigned)item->widget & 3u);"),
    ("scene_item_arc_is_a_number", T, F, WIDGET, "\tif(item->widget == LAYOUT_WIDGET_BAR || item->widget == LAYOUT_WIDGET_STATE) out->widget = item->widget;"),
    ("scene_item_bar_is_a_number", T, F, WIDGET, "\tif(item->widget == LAYOUT_WIDGET_ARC || item->widget == LAYOUT_WIDGET_STATE) out->widget = item->widget;"),
    ("scene_item_state_is_a_number", T, F, WIDGET, "\tif(item->widget == LAYOUT_WIDGET_ARC || item->widget == LAYOUT_WIDGET_BAR) out->widget = item->widget;"),
    ("scene_item_bar_is_an_arc", T, F,
     WIDGET, WIDGET + "\n\tif(item->widget == LAYOUT_WIDGET_BAR) out->widget = LAYOUT_WIDGET_ARC;"),
    ("scene_item_dash_not_dimmed", T, F, "\tout->tone = SCENE_TONE_DIM;\n", ""),
    ("scene_item_permille_zero_without_range", T, F, "\tout->tone = SCENE_TONE_DIM;\n\tout->permille = -1;\n", "\tout->tone = SCENE_TONE_DIM;\n"),
    ("scene_item_missing_is_unavailable", T, F, NOT_SHOWN, "\t\tappend(out->text, sizeof(out->text), SCENE_UNAVAILABLE);\n\t\treturn;"),
    ("scene_item_unavailable_is_a_dash", T, F, NOT_SHOWN, "\t\tappend(out->text, sizeof(out->text), SCENE_DASH);\n\t\treturn;"),
    ("scene_item_dash_and_unavailable_swapped", T, F,
     NOT_SHOWN, "\t\tappend(out->text, sizeof(out->text), state == LAYOUT_ITEM_UNAVAILABLE ? SCENE_DASH : SCENE_UNAVAILABLE);\n\t\treturn;"),
    ("scene_item_no_text_is_empty", T, F, NOT_SHOWN, "\t\treturn;"),
    ("scene_item_no_text_unavailable_by_catalog", T, F,
     NOT_SHOWN, "\t\tappend(out->text, sizeof(out->text), catalog_find(catalog, item->key) < 0 ? SCENE_UNAVAILABLE : SCENE_DASH);\n\t\treturn;"),
    ("scene_item_dash_has_unit", T, F,
     NOT_SHOWN, "\t\tappend(out->text, sizeof(out->text), state == LAYOUT_ITEM_UNAVAILABLE ? SCENE_UNAVAILABLE : SCENE_DASH);\n"
     "\t\tif(state != LAYOUT_ITEM_UNAVAILABLE) append(out->unit, sizeof(out->unit), layout_item_unit(item, catalog));\n\t\treturn;"),
    ("scene_item_unavailable_has_unit", T, F,
     NOT_SHOWN, "\t\tappend(out->text, sizeof(out->text), state == LAYOUT_ITEM_UNAVAILABLE ? SCENE_UNAVAILABLE : SCENE_DASH);\n"
     "\t\tif(state == LAYOUT_ITEM_UNAVAILABLE) append(out->unit, sizeof(out->unit), layout_item_unit(item, catalog));\n\t\treturn;"),
    ("scene_item_dash_raises_level", T, F,
     IF_NOT_SHOWN, "\tif(value != NULL && layout_item_level(item, value) > *level) *level = layout_item_level(item, value);\n" + IF_NOT_SHOWN),
    # a dash is missed, and the ring says so; what the profile does not provide is not
    ("scene_item_dash_is_not_missed", T, F, MISSED, ""),
    ("scene_item_unavailable_is_missed", T, F, MISSED, "\t\t*old = true;\n"),
    ("scene_item_missed_swapped", T, F, MISSED, "\t\tif(state == LAYOUT_ITEM_UNAVAILABLE) *old = true;\n"),
    ("scene_item_no_text_is_not_missed", T, F, MISSED, "\t\tif(state == LAYOUT_ITEM_NO_VALUE) *old = true;\n"),
    ("scene_item_gone_is_not_missed", T, F, MISSED, "\t\tif(state == LAYOUT_ITEM_LIVE || state == LAYOUT_ITEM_OLD) *old = true;\n"),
    ("scene_item_fresh_without_text_is_not_missed", T, F, MISSED, "\t\tif(state != LAYOUT_ITEM_UNAVAILABLE && state != LAYOUT_ITEM_LIVE) *old = true;\n"),
    ("scene_item_never_seen_is_not_missed", T, F, MISSED, "\t\tif(state != LAYOUT_ITEM_UNAVAILABLE && value != NULL) *old = true;\n"),
    ("scene_item_missed_only_with_profile", T, F,
     MISSED, "\t\tif(state != LAYOUT_ITEM_UNAVAILABLE && catalog_find(catalog, item->key) >= 0) *old = true;\n"),
    ("scene_item_missed_of_last_value", T, F, MISSED, "\t\t*old = state != LAYOUT_ITEM_UNAVAILABLE;\n"),
    ("scene_item_missed_only_with_label", T, F, MISSED, "\t\tif(state != LAYOUT_ITEM_UNAVAILABLE && item->label[0] != '\\0') *old = true;\n"),
    ("scene_item_missed_raises_level", T, F, MISSED, MISSED + "\t\tif(state != LAYOUT_ITEM_UNAVAILABLE && *level < 2) *level = 2;\n"),
    ("scene_item_missed_only_during_scan", T, F, MISSED, "\t\tif(state != LAYOUT_ITEM_UNAVAILABLE && view == CONN_VIEW_SCAN) *old = true;\n"),
    ("scene_item_missed_only_for_numbers", T, F, MISSED, "\t\tif(state != LAYOUT_ITEM_UNAVAILABLE && out->widget == LAYOUT_WIDGET_NUMBER) *old = true;\n"),
    ("scene_item_missed_not_for_a_state", T, F, MISSED, "\t\tif(state != LAYOUT_ITEM_UNAVAILABLE && item->widget != LAYOUT_WIDGET_STATE) *old = true;\n"),
    ("scene_item_missed_not_in_safe_mode", T, F, MISSED, "\t\tif(state != LAYOUT_ITEM_UNAVAILABLE && !input->safe_mode) *old = true;\n"),
    ("scene_item_missed_not_when_hot", T, F, MISSED, "\t\tif(state != LAYOUT_ITEM_UNAVAILABLE && input->heat == GUARD_HEAT_NORMAL) *old = true;\n"),
    ("scene_item_missed_toggles", T, F, MISSED, "\t\tif(state != LAYOUT_ITEM_UNAVAILABLE) *old = !*old;\n"),
    ("scene_item_unavailable_is_missed_once_seen", T, F, MISSED, "\t\tif(state == LAYOUT_ITEM_NO_VALUE || value != NULL) *old = true;\n"),
    ("scene_item_level_not_told", T, F, LEVEL, "\t(void)level;\n"),
    ("scene_item_level_of_last_value", T, F, LEVEL, "\t*level = item_level;\n"),
    ("scene_item_level_of_first_value", T, F, LEVEL, "\tif(*level == 0) *level = item_level;\n"),
    ("scene_item_level_only_while_fresh", T, F, LEVEL, "\tif(state == LAYOUT_ITEM_LIVE && item_level > *level) *level = item_level;\n"),
    ("scene_item_level_not_of_a_state", T, F, LEVEL, "\tif(item_level > *level && out->widget != LAYOUT_WIDGET_STATE) *level = item_level;\n"),
    ("scene_item_level_only_of_unscaled_value", T, F,
     "\titem_level = layout_item_level(item, value);", "\titem_level = item->scale == 1 ? layout_item_level(item, value) : 0;"),
    ("scene_item_old_not_told", T, F, OLD, "\t(void)old;\n"),
    ("scene_item_old_of_last_value", T, F, OLD, "\t*old = state == LAYOUT_ITEM_OLD;\n"),
    ("scene_item_every_value_old", T, F, OLD, "\t*old = true;\n"),
    ("scene_item_old_value_in_its_tone", T, F, LIVE_TONE, "\tif(view != CONN_VIEW_SCAN)"),
    ("scene_item_scan_does_not_dim", T, F, LIVE_TONE, "\t(void)view;\n\tif(state == LAYOUT_ITEM_LIVE)"),
    ("scene_item_no_api_dims", T, F, LIVE_TONE, "\tif(state == LAYOUT_ITEM_LIVE && view == CONN_VIEW_LIVE)"),
    ("scene_item_warn_is_alarm", T, F, TONE, "\t\tout->tone = item_level >= 1 ? SCENE_TONE_ALARM : SCENE_TONE_NORMAL;"),
    ("scene_item_alarm_is_warn", T, F, TONE, "\t\tout->tone = item_level >= 1 ? SCENE_TONE_WARN : SCENE_TONE_NORMAL;"),
    ("scene_item_warn_is_normal", T, F, TONE, "\t\tout->tone = item_level >= 2 ? SCENE_TONE_ALARM : SCENE_TONE_NORMAL;"),
    ("scene_item_alarm_is_normal", T, F, TONE, "\t\tout->tone = item_level == 1 ? SCENE_TONE_WARN : SCENE_TONE_NORMAL;"),
    ("scene_item_warn_and_alarm_swapped", T, F,
     TONE, "\t\tout->tone = item_level >= 2 ? SCENE_TONE_WARN : item_level == 1 ? SCENE_TONE_ALARM : SCENE_TONE_NORMAL;"),
    ("scene_item_state_has_unit", T, F, UNIT, "\tappend(out->unit, sizeof(out->unit), layout_item_unit(item, catalog));"),
    ("scene_item_no_unit", T, F, UNIT, ""),
    ("scene_item_unit_of_layout_only", T, F, UNIT, "\tif(out->widget != LAYOUT_WIDGET_STATE) append(out->unit, sizeof(out->unit), item->unit);"),
    ("scene_item_unit_of_catalog_only", T, F,
     UNIT, "\tif(out->widget != LAYOUT_WIDGET_STATE && catalog_find(catalog, item->key) >= 0) append(out->unit, sizeof(out->unit), "
     "catalog->entries[catalog_find(catalog, item->key)].unit);"),
    ("scene_item_arc_without_permille", T, F, PERMILLE, "\tif(out->widget == LAYOUT_WIDGET_BAR) out->permille = range_permille(item, value);"),
    ("scene_item_bar_without_permille", T, F, PERMILLE, "\tif(out->widget == LAYOUT_WIDGET_ARC) out->permille = range_permille(item, value);"),
    ("scene_item_number_with_permille", T, F, PERMILLE, "\tif(out->widget != LAYOUT_WIDGET_STATE) out->permille = range_permille(item, value);"),
    ("scene_item_state_with_permille", T, F, PERMILLE, "\tif(out->widget != LAYOUT_WIDGET_NUMBER) out->permille = range_permille(item, value);"),
    ("scene_item_scan_takes_permille", T, F,
     PERMILLE, "\tif(view != CONN_VIEW_SCAN && (out->widget == LAYOUT_WIDGET_ARC || out->widget == LAYOUT_WIDGET_BAR)) out->permille = range_permille(item, value);"),
    ("scene_item_old_value_without_permille", T, F,
     PERMILLE, "\tif(state == LAYOUT_ITEM_LIVE && (out->widget == LAYOUT_WIDGET_ARC || out->widget == LAYOUT_WIDGET_BAR)) out->permille = range_permille(item, value);"),

    # the value pages: dots, note, notices
    ("scene_dots_count_every_page", T, F, DOT_SHOWN, ""),
    ("scene_dots_count_pages_not_hidden", T, F, DOT_SHOWN, "\t\tif(layout->pages[i].hidden) continue;\n"),
    ("scene_dot_is_number_of_page", T, F, DOT, "\t\tif(i == shown) scene->dot = i;\n"),
    ("scene_dot_not_lit", T, F, DOT, ""),
    ("scene_dot_counts_from_one", T, F, DOT, "\t\tif(i == shown) scene->dot = scene->dots + 1;\n"),
    ("scene_dot_of_hidden_page_lit", T, F, DOT_SHOWN + "\n" + DOT, "\t\tif(i == shown) scene->dot = scene->dots;\n" + DOT_SHOWN + "\n"),
    ("scene_dots_of_eight_pages_only", T, F,
     "\tfor(int i = 0; i < layout->page_count; i++)\n\t{\n\t\tif(!layout_page_shown", "\tfor(int i = 0; i < layout->page_count && i < 8; i++)\n\t{\n\t\tif(!layout_page_shown"),
    ("scene_dots_not_counted", T, F, "\t\tscene->dots++;\n", ""),
    ("scene_note_safe_mode_missing", T, F, NOTE_SAFE + "\telse if(input->heat", "\tif(input->heat"),
    ("scene_note_heat_missing", T, F, NOTE_HEAT + "\telse if(view", "\telse if(view"),
    ("scene_note_view_missing", T, F, NOTE_VIEW, ""),
    ("scene_note_heat_before_safe_mode", T, F,
     NOTE_SAFE + NOTE_HEAT, "\tif(input->heat != GUARD_HEAT_NORMAL) set_note(scene, \"Zu heiß – Anzeige gedimmt\");\n"
     "\telse if(input->safe_mode) set_note(scene, \"Sicherer Modus – eingebaute Ansichten\");\n"),
    ("scene_note_view_before_heat", T, F,
     NOTE_HEAT + NOTE_VIEW, "\telse if(view == CONN_VIEW_SCAN || view == CONN_VIEW_NO_API) set_note(scene, text_view(view));\n"
     "\telse if(input->heat != GUARD_HEAT_NORMAL) set_note(scene, \"Zu heiß – Anzeige gedimmt\");\n"),
    ("scene_note_view_before_safe_mode", T, F,
     NOTE_SAFE + NOTE_HEAT + NOTE_VIEW, "\tif(view == CONN_VIEW_SCAN || view == CONN_VIEW_NO_API) set_note(scene, text_view(view));\n"
     "\telse if(input->safe_mode) set_note(scene, \"Sicherer Modus – eingebaute Ansichten\");\n"
     "\telse if(input->heat != GUARD_HEAT_NORMAL) set_note(scene, \"Zu heiß – Anzeige gedimmt\");\n"),
    ("scene_note_heat_only_when_dimmed", T, F, NOTE_HEAT, "\telse if(input->heat == GUARD_HEAT_DIM) set_note(scene, \"Zu heiß – Anzeige gedimmt\");\n"),
    ("scene_note_heat_only_when_off", T, F, NOTE_HEAT, "\telse if(input->heat == GUARD_HEAT_OFF) set_note(scene, \"Zu heiß – Anzeige gedimmt\");\n"),
    ("scene_note_heat_by_low_byte", T, F,
     NOTE_HEAT, "\telse if(((unsigned)input->heat & 0xFFu) != GUARD_HEAT_NORMAL) set_note(scene, \"Zu heiß – Anzeige gedimmt\");\n"),
    ("scene_note_not_for_scan", T, F, NOTE_VIEW, "\telse if(view == CONN_VIEW_NO_API) set_note(scene, text_view(view));\n"),
    ("scene_note_not_for_no_api", T, F, NOTE_VIEW, "\telse if(view == CONN_VIEW_SCAN) set_note(scene, text_view(view));\n"),
    ("scene_note_for_every_view", T, F, NOTE_VIEW, "\telse set_note(scene, text_view(view));\n"),
    ("scene_note_not_on_notices", T, F,
     NOTE_SAFE, "\tif(view != CONN_VIEW_LIVE && view != CONN_VIEW_SCAN && view != CONN_VIEW_NO_API) {}\n\telse if(input->safe_mode) "
     "set_note(scene, \"Sicherer Modus – eingebaute Ansichten\");\n"),
    text("scene_text_note_safe_mode", "\"Sicherer Modus – eingebaute Ansichten\"", "\"Sicherer Modus - eingebaute Ansichten\""),
    text("scene_text_note_heat", "\"Zu heiß – Anzeige gedimmt\"", "\"Zu heiss – Anzeige gedimmt\""),
    ("scene_pages_live_is_a_notice", T, F, VALUE_VIEWS, "\tif(view != CONN_VIEW_SCAN && view != CONN_VIEW_NO_API)"),
    ("scene_pages_scan_is_a_notice", T, F, VALUE_VIEWS, "\tif(view != CONN_VIEW_LIVE && view != CONN_VIEW_NO_API)"),
    ("scene_pages_no_api_is_a_notice", T, F, VALUE_VIEWS, "\tif(view != CONN_VIEW_LIVE && view != CONN_VIEW_SCAN)"),
    ("scene_pages_ignition_off_shows_values", T, F,
     VALUE_VIEWS, "\tif(view != CONN_VIEW_LIVE && view != CONN_VIEW_SCAN && view != CONN_VIEW_NO_API && view != CONN_VIEW_ECU_OFFLINE)"),
    ("scene_pages_starting_shows_values", T, F,
     VALUE_VIEWS, "\tif(view != CONN_VIEW_LIVE && view != CONN_VIEW_SCAN && view != CONN_VIEW_NO_API && view != CONN_VIEW_STARTING)"),
    ("scene_notice_is_no_notice", T, F,
     VIEW_BLOCK_HEAD + "\t\tscene->kind = SCENE_NOTICE;\n", VIEW_BLOCK_HEAD + "\t\tscene->kind = SCENE_VALUES;\n"),
    ("scene_notice_without_text", T, F, "\t\tadd_text(scene, text_view(view));\n", ""),
    ("scene_notice_text_as_title", T, F, "\t\tadd_text(scene, text_view(view));\n", "\t\tset_title(scene, text_view(view));\n"),
    ("scene_notice_battery_in_every_view", T, F, BATTERY, "\t\tif(state != NULL && state->batt_mv >= 0)"),
    ("scene_notice_battery_never", T, F, BATTERY, "\t\tif(false)"),
    ("scene_notice_battery_not_measured_shown", T, F, BATTERY, "\t\tif(view == CONN_VIEW_ECU_OFFLINE)"),
    ("scene_notice_battery_zero_is_none", T, F, BATTERY, "\t\tif(view == CONN_VIEW_ECU_OFFLINE && state->batt_mv > 0)"),
    ("scene_notice_battery_rounded_down", T, F, TENTHS, "\t\t\tuint32_t tenths = (uint32_t)state->batt_mv / 100;"),
    ("scene_notice_battery_rounded_up", T, F, TENTHS, "\t\t\tuint32_t tenths = ((uint32_t)state->batt_mv + 99) / 100;"),
    ("scene_notice_battery_half_rounded_down", T, F, TENTHS, "\t\t\tuint32_t tenths = ((uint32_t)state->batt_mv + 49) / 100;"),
    ("scene_notice_battery_in_whole_volts", T, F, TENTHS, "\t\t\tuint32_t tenths = ((uint32_t)state->batt_mv + 500) / 1000 * 10;"),
    ("scene_notice_battery_two_decimals", T, F,
     "\t\t\tappend_number(line, SCENE_TEXT_SIZE, tenths % 10);\n", "\t\t\tappend_number(line, SCENE_TEXT_SIZE, tenths % 10);\n"
     "\t\t\tappend_number(line, SCENE_TEXT_SIZE, (uint32_t)state->batt_mv / 10 % 10);\n"),
    ("scene_notice_battery_without_decimal", T, F, "\t\t\tappend_number(line, SCENE_TEXT_SIZE, tenths % 10);\n", ""),
    ("scene_notice_battery_volts_of_tenths", T, F,
     "\t\t\tappend_number(line, SCENE_TEXT_SIZE, tenths / 10);\n", "\t\t\tappend_number(line, SCENE_TEXT_SIZE, tenths / 10 % 100);\n"),
    text("scene_text_battery", "\"Bordnetz \"", "\"Batterie \""),
    text("scene_text_battery_point", "\t\t\tappend(line, SCENE_TEXT_SIZE, \",\");", "\t\t\tappend(line, SCENE_TEXT_SIZE, \".\");"),
    text("scene_text_battery_unit", "\t\t\tappend(line, SCENE_TEXT_SIZE, \" V\");", "\t\t\tappend(line, SCENE_TEXT_SIZE, \"V\");"),
    ("scene_pages_negative_page_shown", T, F, NO_PAGE, "\tif(shown >= layout->page_count)"),
    ("scene_pages_page_behind_layout_shown", T, F, NO_PAGE, "\tif(shown < 0 || shown > layout->page_count)"),
    ("scene_pages_page_beyond_room_only", T, F, NO_PAGE, "\tif(shown < 0 || shown >= LAYOUT_PAGES_MAX)"),
    ("scene_pages_last_page_is_none", T, F, NO_PAGE, "\tif(shown < 0 || shown >= layout->page_count - 1)"),
    ("scene_pages_first_page_is_none", T, F, NO_PAGE, "\tif(shown <= 0 || shown >= layout->page_count)"),
    ("scene_pages_page_not_shown_is_none", T, F, NO_PAGE, "\tif(scene->dot < 0)"),
    ("scene_pages_no_page_goes_first", T, F,
     VALUE_VIEWS, "\tif(view != CONN_VIEW_LIVE && view != CONN_VIEW_SCAN && view != CONN_VIEW_NO_API && shown >= 0 && shown < layout->page_count)"),
    text("scene_text_no_page", "\"Keine Ansicht mit verfügbaren Werten\"", "\"Keine Ansicht mit Werten\""),
    ("scene_pages_no_page_is_empty_page", T, F,
     "\t\tscene->kind = SCENE_NOTICE;\n\t\tadd_text(scene, \"Keine Ansicht mit verfügbaren Werten\");", "\t\tscene->kind = SCENE_VALUES;"),
    ("scene_pages_without_title", T, F, "\tset_title(scene, page->title);\n", ""),
    ("scene_pages_title_of_first_page", T, F, "\tset_title(scene, page->title);", "\tset_title(scene, layout->pages[0].title);"),
    ("scene_pages_values_of_first_page", T, F, "\tpage = &layout->pages[shown];", "\tpage = &layout->pages[0];"),
    ("scene_pages_items_not_counted", T, F, "\tscene->item_count = page->item_count;\n", ""),
    ("scene_pages_one_item_less", T, F,
     "\tfor(int i = 0; i < page->item_count; i++) build_item(", "\tfor(int i = 0; i < page->item_count - 1; i++) build_item("),
    ("scene_pages_every_item_is_the_first", T, F,
     "build_item(input, view, &page->items[i], &scene->items[i], level, old);", "build_item(input, view, &page->items[0], &scene->items[i], level, old);"),

    # lists: the window and the rows
    ("scene_rows_counted_without_lines", T, F, TOTAL, "\tint total = nav_rows(nav, input->world);"),
    ("scene_rows_never_any_lines", T, F, TOTAL, "\tint total = choice_count;"),
    ("scene_rows_info_without_texts_counted", T, F, TOTAL, "\tint total = lines != NULL || choices == NULL ? nav_rows(nav, input->world) : choice_count;"),
    ("scene_rows_lists_without_lines_counted", T, F, TOTAL, "\tint total = texts != NULL || choices != NULL ? nav_rows(nav, input->world) : choice_count;"),
    ("scene_rows_focus_at_the_top", T, F, FIRST, "\tint64_t first = (int64_t)nav->row;"),
    ("scene_rows_focus_second", T, F, FIRST, "\tint64_t first = (int64_t)nav->row - 1;"),
    ("scene_rows_focus_fourth", T, F, FIRST, "\tint64_t first = (int64_t)nav->row - 3;"),
    ("scene_rows_focus_at_the_bottom", T, F, FIRST, "\tint64_t first = (int64_t)nav->row - (SCENE_ROWS_MAX - 1);"),
    ("scene_rows_window_stands", T, F, FIRST, "\tint64_t first = 0;"),
    ("scene_rows_window_leaves_the_end", T, F, FIRST_HIGH, ""),
    ("scene_rows_window_ends_one_early", T, F, FIRST_HIGH, "\tif(first > total - SCENE_ROWS_MAX - 1) first = total - SCENE_ROWS_MAX - 1;\n"),
    ("scene_rows_window_ends_one_late", T, F, FIRST_HIGH, "\tif(first > total - SCENE_ROWS_MAX + 1) first = total - SCENE_ROWS_MAX + 1;\n"),
    ("scene_rows_window_ends_with_the_focus_far_away", T, F, FIRST_HIGH, "\tif(first >= total) first = total - SCENE_ROWS_MAX;\n"),
    ("scene_rows_window_leaves_the_start", T, F, FIRST_LOW, ""),
    ("scene_rows_window_starts_at_one", T, F, FIRST_LOW, "\tif(first < 1) first = 1;\n"),
    ("scene_rows_short_list_starts_below_zero", T, F, FIRST_HIGH + FIRST_LOW, FIRST_LOW + FIRST_HIGH),
    ("scene_rows_total_not_told", T, F, "\tscene->total = total;\n", ""),
    ("scene_rows_total_is_visible_rows", T, F, "\tscene->total = total;", "\tscene->total = total < SCENE_ROWS_MAX ? total : SCENE_ROWS_MAX;"),
    ("scene_rows_total_counts_lines_only", T, F, "\tscene->total = total;", "\tscene->total = line_count;"),
    ("scene_rows_first_not_told", T, F, "\tscene->first = (int)first;\n" + ROWS_LOOP, "\tfor(int index = (int)first; index < total && scene->row_count < SCENE_ROWS_MAX; index++)"),
    ("scene_rows_behind_the_last", T, F, ROWS_LOOP, "\tfor(int index = scene->first; scene->row_count < SCENE_ROWS_MAX; index++)"),
    ("scene_rows_four_visible", T, F, ROWS_LOOP, "\tfor(int index = scene->first; index < total && scene->row_count < SCENE_ROWS_MAX - 1; index++)"),
    ("scene_rows_six_visible", T, F, ROWS_LOOP, "\tfor(int index = scene->first; index < total && scene->row_count <= SCENE_ROWS_MAX; index++)"),
    ("scene_rows_last_row_left_out", T, F, ROWS_LOOP, "\tfor(int index = scene->first; index < total - 1 && scene->row_count < SCENE_ROWS_MAX; index++)"),
    ("scene_rows_start_at_the_top", T, F, ROWS_LOOP, "\tfor(int index = 0; index < total && scene->row_count < SCENE_ROWS_MAX; index++)"),
    ("scene_rows_line_is_no_line", T, F, "\t\trow->kind = SCENE_ROW_LINE;\n", ""),
    ("scene_rows_line_not_enabled", T, F, "\t\trow->enabled = true;\n", ""),
    ("scene_rows_no_focus", T, F, ROW_FOCUS + "\n", ""),
    ("scene_rows_focus_on_middle_row", T, F, ROW_FOCUS, "\t\trow->focus = scene->row_count - 1 == SCENE_ROWS_MAX / 2;"),
    ("scene_rows_focus_by_position_in_window", T, F, ROW_FOCUS, "\t\trow->focus = scene->row_count - 1 == nav->row;"),
    ("scene_rows_focus_beyond_on_last_row", T, F, ROW_FOCUS, "\t\trow->focus = index == nav->row || (index == total - 1 && nav->row >= total);"),
    ("scene_rows_focus_before_on_first_row", T, F, ROW_FOCUS, "\t\trow->focus = index == nav->row || (index == 0 && nav->row < 0);"),
    ("scene_rows_first_choice_is_a_line", T, F, IS_CHOICE, "\t\tif(index > line_count)"),
    ("scene_rows_last_line_is_a_choice", T, F,
     IS_CHOICE + "\n\t\t{\n\t\t\tconst choice_t *choice = &choices[index - line_count];", "\t\tif(index >= line_count - 1 && choice_count > 0)\n\t\t{\n"
     "\t\t\tconst choice_t *choice = &choices[index >= line_count ? index - line_count : 0];"),
    ("scene_rows_choice_is_a_line", T, F, "\t\t\trow->kind = SCENE_ROW_ACTION;\n", ""),
    ("scene_rows_choice_always_enabled", T, F, "\t\t\trow->enabled = choice->enabled;\n", ""),
    ("scene_rows_choice_never_enabled", T, F, "\t\t\trow->enabled = choice->enabled;", "\t\t\trow->enabled = false;"),
    ("scene_rows_choice_without_text", T, F, "\t\t\tappend(row->text, sizeof(row->text), choice->text);\n", ""),
    ("scene_rows_choice_without_detail", T, F, "\t\t\tappend(row->detail, sizeof(row->detail), choice->detail);\n", ""),
    ("scene_rows_choices_all_the_first", T, F, "\t\t\tconst choice_t *choice = &choices[index - line_count];", "\t\t\tconst choice_t *choice = &choices[0];"),
    ("scene_rows_head_is_a_line", T, F, LINE_HEAD, ""),
    ("scene_rows_code_is_a_line", T, F, LINE_CODE, ""),
    ("scene_rows_code_is_a_head", T, F, LINE_CODE, "\t\t\tif(lines[index].kind == DTC_LINE_CODE) row->kind = SCENE_ROW_HEAD;\n"),
    ("scene_rows_head_is_indented", T, F, LINE_HEAD, "\t\t\tif(lines[index].kind == DTC_LINE_HEAD) row->kind = SCENE_ROW_SUB;\n"),
    ("scene_rows_unit_is_a_head", T, F, LINE_HEAD, "\t\t\tif(lines[index].kind == DTC_LINE_HEAD || lines[index].kind == DTC_LINE_ECU) row->kind = SCENE_ROW_HEAD;\n"),
    ("scene_rows_note_is_indented", T, F, LINE_CODE, "\t\t\tif(lines[index].kind == DTC_LINE_CODE || lines[index].kind == DTC_LINE_NOTE) row->kind = SCENE_ROW_SUB;\n"),
    ("scene_rows_first_line_is_the_head", T, F, LINE_HEAD, "\t\t\tif(index == 0) row->kind = SCENE_ROW_HEAD;\n"),
    ("scene_rows_code_by_low_byte", T, F, LINE_CODE, "\t\t\tif(((unsigned)lines[index].kind & 0xFFu) == DTC_LINE_CODE) row->kind = SCENE_ROW_SUB;\n"),
    ("scene_rows_line_without_text", T, F, "\t\t\tappend(row->text, sizeof(row->text), lines[index].text);\n", ""),
    ("scene_rows_line_without_detail", T, F, "\t\t\tappend(row->detail, sizeof(row->detail), lines[index].detail);\n", ""),
    ("scene_rows_line_detail_as_text", T, F,
     "\t\t\tappend(row->text, sizeof(row->text), lines[index].text);", "\t\t\tappend(row->text, sizeof(row->text), lines[index].detail);"),
    ("scene_rows_lines_counted_from_window", T, F,
     "\t\t\tappend(row->text, sizeof(row->text), lines[index].text);", "\t\t\tappend(row->text, sizeof(row->text), lines[index - scene->first].text);"),
    ("scene_rows_info_without_text", T, F, "\t\t\tappend(row->text, sizeof(row->text), texts[index]);\n", ""),
    ("scene_rows_info_null_text_is_a_mark", T, F,
     "\t\t\tappend(row->text, sizeof(row->text), texts[index]);", "\t\t\tappend(row->text, sizeof(row->text), texts[index] != NULL ? texts[index] : \"?\");"),
    ("scene_rows_info_counted_from_window", T, F,
     "\t\t\tappend(row->text, sizeof(row->text), texts[index]);", "\t\t\tappend(row->text, sizeof(row->text), texts[index - scene->first]);"),
    ("scene_rows_is_no_list", T, F, "\tscene->kind = SCENE_LIST;\n", ""),

    # the menu
    text("scene_text_menu_title", "\tset_title(scene, \"Menü\");", "\tset_title(scene, \"Menu\");"),
    text("scene_text_menu_dtc", "\t\t{\"Fehlerspeicher\", \"\", true},", "\t\t{\"Fehler\", \"\", true},"),
    text("scene_text_menu_brightness", "\t\t{\"Helligkeit\", brightness, true},", "\t\t{\"Hell\", brightness, true},"),
    text("scene_text_menu_night", "{\"Nachtmodus\", world->night_mode", "{\"Nacht\", world->night_mode"),
    text("scene_text_menu_web", "{\"Web-Zugriff\", world->release_open", "{\"Web\", world->release_open"),
    text("scene_text_menu_info", "\t\t{\"Info\", \"\", true},", "\t\t{\"Infos\", \"\", true},"),
    text("scene_text_menu_settings", "\t\t{\"Einstellungen\", \"\", true},\n\t\t{\"Zurück\", \"\", true},\n\t};\n\n\tappend_percent",
         "\t\t{\"Einstellung\", \"\", true},\n\t\t{\"Zurück\", \"\", true},\n\t};\n\n\tappend_percent"),
    text("scene_text_menu_back", "\t\t{\"Einstellungen\", \"\", true},\n\t\t{\"Zurück\", \"\", true},\n\t};\n\n\tappend_percent",
         "\t\t{\"Einstellungen\", \"\", true},\n\t\t{\"Zurueck\", \"\", true},\n\t};\n\n\tappend_percent"),
    ("scene_menu_brightness_without_number", T, F, "\tappend_percent(brightness, sizeof(brightness), world->brightness);\n", ""),
    ("scene_menu_brightness_of_the_knob", T, F,
     "\tappend_percent(brightness, sizeof(brightness), world->brightness);", "\tappend_percent(brightness, sizeof(brightness), input->nav->value);"),
    ("scene_menu_brightness_clamped", T, F,
     "\tappend_percent(brightness, sizeof(brightness), world->brightness);",
     "\tappend_percent(brightness, sizeof(brightness), world->brightness < 0 ? 0 : world->brightness > 100 ? 100 : world->brightness);"),
    ("scene_menu_night_swapped", T, F, MENU_NIGHT, "\t\t{\"Nachtmodus\", world->night_mode ? \"aus\" : \"an\", true},\n"),
    ("scene_menu_night_always_off", T, F, MENU_NIGHT, "\t\t{\"Nachtmodus\", \"aus\", true},\n"),
    ("scene_menu_night_by_release", T, F, MENU_NIGHT, "\t\t{\"Nachtmodus\", world->release_open ? \"an\" : \"aus\", true},\n"),
    ("scene_menu_release_swapped", T, F, MENU_WEB, "\t\t{\"Web-Zugriff\", world->release_open ? \"gesperrt\" : \"frei\", true},\n"),
    ("scene_menu_release_always_locked", T, F, MENU_WEB, "\t\t{\"Web-Zugriff\", \"gesperrt\", true},\n"),
    ("scene_menu_release_by_access", T, F,
     MENU_WEB, "\t\t{\"Web-Zugriff\", access_is_open(input->access, input->now_ms) ? \"frei\" : \"gesperrt\", true},\n"),
    ("scene_menu_dtc_only_when_it_may_be_read", T, F,
     "\t\t{\"Fehlerspeicher\", \"\", true},", "\t\t{\"Fehlerspeicher\", \"\", world->can_read || world->flow != DTC_FLOW_IDLE},"),
    ("scene_menu_night_only_with_brightness", T, F, MENU_NIGHT, "\t\t{\"Nachtmodus\", world->night_mode ? \"an\" : \"aus\", world->brightness > 0},\n"),
    ("scene_menu_rows_swapped", T, F,
     MENU_NIGHT + MENU_WEB, MENU_WEB + MENU_NIGHT),
    ("scene_menu_first_rows_swapped", T, F,
     "\t\t{\"Fehlerspeicher\", \"\", true},\n\t\t{\"Helligkeit\", brightness, true},\n", "\t\t{\"Helligkeit\", brightness, true},\n\t\t{\"Fehlerspeicher\", \"\", true},\n"),
    ("scene_menu_last_rows_swapped", T, F,
     "\t\t{\"Info\", \"\", true},\n\t\t{\"Einstellungen\", \"\", true},\n", "\t\t{\"Einstellungen\", \"\", true},\n\t\t{\"Info\", \"\", true},\n"),

    # the fault memory
    text("scene_text_dtc_title", "\tset_title(scene, \"Fehlerspeicher\");\n\tset_note(scene, text_block(input->read_block));",
         "\tset_title(scene, \"Fehler\");\n\tset_note(scene, text_block(input->read_block));"),
    text("scene_text_dtc_read", "{\"Lesen\", \"\", world->can_read}", "{\"Lesen …\", \"\", world->can_read}"),
    text("scene_text_dtc_view", "{\"Liste ansehen\", \"\", outcome}", "{\"Liste\", \"\", outcome}"),
    text("scene_text_dtc_old", "{\"Zuletzt gelöscht\", \"\", world->old_lines > 0}", "{\"Zuletzt geloescht\", \"\", world->old_lines > 0}"),
    text("scene_text_dtc_back", DTC_OLD + "\t\t{\"Zurück\", \"\", true},", DTC_OLD + "\t\t{\"Zurueck\", \"\", true},"),
    ("scene_dtc_read_always_offered", T, F, DTC_READ, "\t\t{\"Lesen\", \"\", true},\n"),
    ("scene_dtc_read_by_block", T, F, DTC_READ, "\t\t{\"Lesen\", \"\", input->read_block == DTC_FLOW_ALLOWED},\n"),
    ("scene_dtc_read_by_clear", T, F, DTC_READ, "\t\t{\"Lesen\", \"\", world->can_clear},\n"),
    ("scene_dtc_view_not_for_list", T, F, OUTCOME, "\tbool outcome = world->flow == DTC_FLOW_CLEARED || world->flow == DTC_FLOW_FAILED || world->flow == DTC_FLOW_UNKNOWN;"),
    ("scene_dtc_view_not_for_cleared", T, F, OUTCOME, "\tbool outcome = world->flow == DTC_FLOW_LIST || world->flow == DTC_FLOW_FAILED || world->flow == DTC_FLOW_UNKNOWN;"),
    ("scene_dtc_view_not_for_failed", T, F, OUTCOME, "\tbool outcome = world->flow == DTC_FLOW_LIST || world->flow == DTC_FLOW_CLEARED || world->flow == DTC_FLOW_UNKNOWN;"),
    ("scene_dtc_view_not_for_unknown", T, F, OUTCOME, "\tbool outcome = world->flow == DTC_FLOW_LIST || world->flow == DTC_FLOW_CLEARED || world->flow == DTC_FLOW_FAILED;"),
    ("scene_dtc_view_while_reading", T, F,
     OUTCOME, "\tbool outcome = world->flow == DTC_FLOW_READING || world->flow == DTC_FLOW_LIST || world->flow == DTC_FLOW_CLEARED || "
     "world->flow == DTC_FLOW_FAILED || world->flow == DTC_FLOW_UNKNOWN;"),
    ("scene_dtc_view_while_clearing", T, F, OUTCOME, "\tbool outcome = world->flow >= DTC_FLOW_LIST && world->flow <= DTC_FLOW_UNKNOWN;"),
    ("scene_dtc_view_unless_idle", T, F, OUTCOME, "\tbool outcome = world->flow != DTC_FLOW_IDLE;"),
    ("scene_dtc_view_by_phase_of_flow", T, F,
     OUTCOME, "\tbool outcome = input->flow->phase == DTC_FLOW_LIST || input->flow->phase == DTC_FLOW_CLEARED || input->flow->phase == DTC_FLOW_FAILED || "
     "input->flow->phase == DTC_FLOW_UNKNOWN;"),
    ("scene_dtc_view_by_low_byte", T, F,
     OUTCOME, "\tunsigned phase = (unsigned)world->flow & 0xFFu;\n\tbool outcome = phase == DTC_FLOW_LIST || phase == DTC_FLOW_CLEARED || "
     "phase == DTC_FLOW_FAILED || phase == DTC_FLOW_UNKNOWN;"),
    ("scene_dtc_view_always", T, F, OUTCOME, "\tbool outcome = true;"),
    ("scene_dtc_view_by_lines", T, F, OUTCOME, "\tbool outcome = world->list_lines > 0;"),
    ("scene_dtc_old_always", T, F, DTC_OLD, "\t\t{\"Zuletzt gelöscht\", \"\", true},\n"),
    ("scene_dtc_old_without_lines", T, F, DTC_OLD, "\t\t{\"Zuletzt gelöscht\", \"\", world->old_lines >= 0},\n"),
    ("scene_dtc_old_with_negative_lines", T, F, DTC_OLD, "\t\t{\"Zuletzt gelöscht\", \"\", world->old_lines != 0},\n"),
    ("scene_dtc_old_needs_two_lines", T, F, DTC_OLD, "\t\t{\"Zuletzt gelöscht\", \"\", world->old_lines > 1},\n"),
    ("scene_dtc_old_needs_the_lines", T, F, DTC_OLD, "\t\t{\"Zuletzt gelöscht\", \"\", world->old_lines > 0 && input->old != NULL},\n"),
    ("scene_dtc_old_by_lines_of_outcome", T, F, DTC_OLD, "\t\t{\"Zuletzt gelöscht\", \"\", world->cleared_lines > 0},\n"),
    ("scene_dtc_old_by_lines_of_list", T, F, DTC_OLD, "\t\t{\"Zuletzt gelöscht\", \"\", world->list_lines > 0},\n"),
    ("scene_dtc_without_note", T, F, "\tset_note(scene, text_block(input->read_block));\n", ""),
    ("scene_dtc_note_of_clear", T, F, "\tset_note(scene, text_block(input->read_block));", "\tset_note(scene, text_block(input->clear_block));"),
    ("scene_dtc_note_only_when_blocked_by_world", T, F,
     "\tset_note(scene, text_block(input->read_block));", "\tif(!world->can_read) set_note(scene, text_block(input->read_block));"),
    ("scene_dtc_rows_swapped", T, F, DTC_VIEW + DTC_OLD, DTC_OLD + DTC_VIEW),
    ("scene_dtc_stand_by_world", T, F, STAND, "\tswitch(world->flow)\n"),
    ("scene_dtc_stand_by_low_byte", T, F, STAND, "\tswitch((dtc_flow_phase_t)((unsigned)input->flow->phase & 0xFFu))\n"),
    ("scene_dtc_stand_only_when_read_allowed", T, F, STAND, "\tif(world->can_read) switch(input->flow->phase)\n"),
    ("scene_dtc_stand_only_without_note", T, F, STAND, "\tif(scene->note[0] == '\\0') switch(input->flow->phase)\n"),
    ("scene_dtc_stand_summary_in_every_phase", T, F,
     STAND, "\tif(input->summary != NULL) add_summary(scene, input->summary);\n\telse switch(input->flow->phase)\n"),
    ("scene_dtc_stand_read_sent_is_idle", T, F, STAND_READ, STAND_READ.replace("\t\tcase DTC_FLOW_READ_SENT:\n", "")),
    ("scene_dtc_stand_reading_is_idle", T, F, STAND_READ, STAND_READ.replace("\t\tcase DTC_FLOW_READING:\n", "")),
    ("scene_dtc_stand_clear_sent_is_idle", T, F, STAND_CLEAR, STAND_CLEAR.replace("\t\tcase DTC_FLOW_CLEAR_SENT:\n", "")),
    ("scene_dtc_stand_clearing_is_idle", T, F, STAND_CLEAR, STAND_CLEAR.replace("\t\tcase DTC_FLOW_CLEARING:\n", "")),
    ("scene_dtc_stand_read_and_clear_swapped", T, F,
     STAND_READ + STAND_LIST + STAND_CLEAR,
     STAND_READ.replace("Lesen läuft", "Löschen läuft") + STAND_LIST + STAND_CLEAR.replace("Löschen läuft", "Lesen läuft")),
    ("scene_dtc_stand_clear_sent_is_a_read", T, F,
     STAND_READ + STAND_LIST + STAND_CLEAR,
     STAND_READ.replace("\t\tcase DTC_FLOW_READING:\n", "\t\tcase DTC_FLOW_READING:\n\t\tcase DTC_FLOW_CLEAR_SENT:\n") + STAND_LIST +
     STAND_CLEAR.replace("\t\tcase DTC_FLOW_CLEAR_SENT:\n", "")),
    ("scene_dtc_stand_list_is_idle", T, F, STAND_LIST, ""),
    ("scene_dtc_stand_list_is_cleared", T, F, STAND_LIST + STAND_CLEAR + STAND_CLEARED,
     STAND_CLEAR + "\t\tcase DTC_FLOW_LIST:\n" + STAND_CLEARED),
    ("scene_dtc_stand_summary_never", T, F, STAND_SUMMARY, "\t\t\tadd_text(scene, \"Liste gelesen\");\n"),
    ("scene_dtc_stand_summary_not_checked", T, F, STAND_SUMMARY, "\t\t\tadd_summary(scene, input->summary);\n"),
    ("scene_dtc_stand_summary_needs_codes", T, F,
     STAND_SUMMARY, "\t\t\tif(input->summary != NULL && input->summary->codes > 0) add_summary(scene, input->summary);\n"
     "\t\t\telse add_text(scene, \"Liste gelesen\");\n"),
    ("scene_dtc_stand_summary_needs_the_lines", T, F,
     STAND_SUMMARY, "\t\t\tif(input->summary != NULL && input->list != NULL) add_summary(scene, input->summary);\n"
     "\t\t\telse add_text(scene, \"Liste gelesen\");\n"),
    ("scene_dtc_stand_summary_is_the_number_of_codes", T, F,
     STAND_SUMMARY, "\t\t\tif(input->summary != NULL) append_number(add_line(scene), SCENE_TEXT_SIZE, input->summary->codes);\n"
     "\t\t\telse add_text(scene, \"Liste gelesen\");\n"),
    ("scene_dtc_stand_summary_and_words", T, F,
     STAND_SUMMARY, "\t\t\tif(input->summary != NULL) add_summary(scene, input->summary);\n\t\t\tadd_text(scene, \"Liste gelesen\");\n"),
    ("scene_dtc_stand_no_summary_no_line", T, F,
     STAND_SUMMARY, "\t\t\tif(input->summary != NULL) add_summary(scene, input->summary);\n"),
    ("scene_dtc_stand_cleared_is_idle", T, F, STAND_CLEARED, ""),
    ("scene_dtc_stand_failed_is_idle", T, F, STAND_FAILED, ""),
    ("scene_dtc_stand_unknown_is_idle", T, F, STAND_UNKNOWN, ""),
    ("scene_dtc_stand_unknown_is_failed", T, F, STAND_FAILED + STAND_UNKNOWN, "\t\tcase DTC_FLOW_UNKNOWN:\n" + STAND_FAILED),
    ("scene_dtc_stand_failed_and_unknown_swapped", T, F,
     STAND_FAILED + STAND_UNKNOWN,
     STAND_FAILED.replace("DTC_FLOW_FAILED", "DTC_FLOW_UNKNOWN") + STAND_UNKNOWN.replace("DTC_FLOW_UNKNOWN", "DTC_FLOW_FAILED")),
    ("scene_dtc_stand_failed_tells_the_reason", T, F,
     STAND_FAILED, "\t\tcase DTC_FLOW_FAILED:\n\t\t\tadd_text(scene, input->flow->reason);\n\t\t\tbreak;\n"),
    ("scene_dtc_stand_failed_has_two_lines", T, F,
     STAND_FAILED, "\t\tcase DTC_FLOW_FAILED:\n\t\t\tadd_text(scene, \"Letzter Auftrag fehlgeschlagen\");\n\t\t\tadd_text(scene, input->flow->reason);\n\t\t\tbreak;\n"),
    ("scene_dtc_stand_idle_has_no_line", T, F, STAND_IDLE, "\t\tdefault:\n\t\t\tbreak;\n"),
    ("scene_dtc_stand_no_phase_has_no_line", T, F,
     STAND_IDLE, "\t\tcase DTC_FLOW_IDLE:\n\t\t\tadd_text(scene, \"Noch nicht gelesen\");\n\t\t\tbreak;\n\t\tdefault:\n\t\t\tbreak;\n"),
    ("scene_dtc_stand_no_phase_has_an_empty_line", T, F,
     STAND_IDLE, "\t\tcase DTC_FLOW_IDLE:\n\t\t\tadd_text(scene, \"Noch nicht gelesen\");\n\t\t\tbreak;\n\t\tdefault:\n\t\t\tadd_text(scene, \"\");\n\t\t\tbreak;\n"),
    ("scene_dtc_stand_no_phase_is_unknown", T, F,
     STAND_UNKNOWN + "\t\t// DTC_FLOW_IDLE, and what is no phase: nav.h takes that for idle as well\n" + STAND_IDLE,
     "\t\tcase DTC_FLOW_IDLE:\n\t\t\tadd_text(scene, \"Noch nicht gelesen\");\n\t\t\tbreak;\n"
     "\t\tdefault:\n\t\t\tadd_text(scene, \"Stand des Löschens unbekannt\");\n\t\t\tbreak;\n"),
    ("scene_dtc_stand_failed_only_with_reason", T, F,
     STAND_FAILED, "\t\tcase DTC_FLOW_FAILED:\n\t\t\tadd_text(scene, input->flow->reason[0] != '\\0' ? \"Letzter Auftrag fehlgeschlagen\" : \"Noch nicht gelesen\");\n\t\t\tbreak;\n"),
    ("scene_dtc_stand_idle_with_old_list_is_cleared", T, F,
     STAND_IDLE, "\t\tdefault:\n\t\t\tadd_text(scene, world->old_lines > 0 ? \"Gelöscht\" : \"Noch nicht gelesen\");\n\t\t\tbreak;\n"),
    ("scene_dtc_stand_idle_with_numbers_is_list", T, F,
     STAND_IDLE, "\t\tdefault:\n\t\t\tadd_text(scene, input->flow->read_seq != 0 ? \"Liste gelesen\" : \"Noch nicht gelesen\");\n\t\t\tbreak;\n"),
    ("scene_dtc_stand_only_with_focus_on_a_row", T, F, STAND, "\tif(input->nav->row >= 0 && input->nav->row < COUNT(choices)) switch(input->flow->phase)\n"),
    ("scene_dtc_stand_list_only_while_not_clearable", T, F,
     STAND_SUMMARY, STAND_SUMMARY.replace("else add_text(scene, \"Liste gelesen\");", "else add_text(scene, world->can_clear ? \"\" : \"Liste gelesen\");")),
    ("scene_dtc_stand_twice", T, F, STAND_END, STAND_END + "\tadd_text(scene, \"\");\n"),
    ("scene_dtc_stand_as_note", T, F,
     STAND_IDLE, "\t\tdefault:\n\t\t\tif(scene->note[0] == '\\0') set_note(scene, \"Noch nicht gelesen\");\n\t\t\tadd_text(scene, \"Noch nicht gelesen\");\n\t\t\tbreak;\n"),
    text("scene_text_dtc_stand_idle", "\"Noch nicht gelesen\"", "\"Nicht gelesen\""),
    text("scene_text_dtc_stand_read", "\"Lesen läuft …\"", "\"Lesen läuft\""),
    text("scene_text_dtc_stand_list", "\"Liste gelesen\"", "\"Gelesen\""),
    text("scene_text_dtc_stand_clear", "\"Löschen läuft …\"", "\"Löschen läuft ...\""),
    text("scene_text_dtc_stand_cleared", "\t\t\tadd_text(scene, \"Gelöscht\");", "\t\t\tadd_text(scene, \"Geloescht\");"),
    text("scene_text_dtc_stand_failed", "\"Letzter Auftrag fehlgeschlagen\"", "\"Auftrag fehlgeschlagen\""),
    text("scene_text_dtc_stand_unknown", "\t\t\tadd_text(scene, \"Stand des Löschens unbekannt\");", "\t\t\tadd_text(scene, \"Stand unbekannt\");"),

    # the progress
    ("scene_busy_read_not_accepted", T, F, ACCEPTED, "\tbool accepted = flow->phase == DTC_FLOW_CLEARING;"),
    ("scene_busy_clear_not_accepted", T, F, ACCEPTED, "\tbool accepted = flow->phase == DTC_FLOW_READING;"),
    ("scene_busy_sent_counts_as_accepted", T, F, ACCEPTED, "\tbool accepted = true;"),
    ("scene_busy_read_sent_is_no_read", T, F, READING, "\tbool reading = flow->phase == DTC_FLOW_READING;"),
    ("scene_busy_reading_is_no_read", T, F, READING, "\tbool reading = flow->phase == DTC_FLOW_READ_SENT;"),
    ("scene_busy_list_is_a_read", T, F, READING, "\tbool reading = flow->phase >= DTC_FLOW_READ_SENT && flow->phase <= DTC_FLOW_LIST;"),
    ("scene_busy_phase_of_world", T, F, READING, "\tbool reading = input->world->flow == DTC_FLOW_READ_SENT || input->world->flow == DTC_FLOW_READING;"),
    ("scene_busy_clear_sent_is_over", T, F, UNDER_WAY, "\tif(!reading && flow->phase != DTC_FLOW_CLEARING)"),
    ("scene_busy_clearing_is_over", T, F, UNDER_WAY, "\tif(!reading && flow->phase != DTC_FLOW_CLEAR_SENT)"),
    ("scene_busy_cleared_is_under_way", T, F,
     UNDER_WAY, "\tif(!reading && flow->phase != DTC_FLOW_CLEAR_SENT && flow->phase != DTC_FLOW_CLEARING && flow->phase != DTC_FLOW_CLEARED)"),
    ("scene_busy_never_over", T, F, UNDER_WAY, "\tif(false)"),
    ("scene_busy_by_low_byte", T, F,
     READING, "\tbool reading = ((unsigned)flow->phase & 0xFFu) == DTC_FLOW_READ_SENT || ((unsigned)flow->phase & 0xFFu) == DTC_FLOW_READING;"),
    ("scene_busy_is_no_progress", T, F, "\tscene->kind = SCENE_PROGRESS;\n", ""),
    ("scene_busy_permille_unset", T, F, "\tscene->kind = SCENE_PROGRESS;\n\tscene->permille = 0;\n", "\tscene->kind = SCENE_PROGRESS;\n"),
    ("scene_busy_over_without_title", T, F, "\t\tset_title(scene, \"Fehlerspeicher\");\n\t\tappend(scene->big, sizeof(scene->big), \"…\");\n\t\treturn;",
     "\t\tappend(scene->big, sizeof(scene->big), \"…\");\n\t\treturn;"),
    ("scene_busy_over_without_big", T, F, "\t\tset_title(scene, \"Fehlerspeicher\");\n\t\tappend(scene->big, sizeof(scene->big), \"…\");\n\t\treturn;",
     "\t\tset_title(scene, \"Fehlerspeicher\");\n\t\treturn;"),
    ("scene_busy_over_with_hint", T, F, "\t\tset_title(scene, \"Fehlerspeicher\");\n\t\tappend(scene->big, sizeof(scene->big), \"…\");\n\t\treturn;",
     "\t\tset_title(scene, \"Fehlerspeicher\");\n\t\tappend(scene->big, sizeof(scene->big), \"…\");\n\t\tadd_text(scene, \"ca. 35 s – Live-Werte pausieren\");\n\t\treturn;"),
    text("scene_text_busy_over_title", "\t\tset_title(scene, \"Fehlerspeicher\");\n\t\tappend(scene->big", "\t\tset_title(scene, \"Fehlerspeicher …\");\n\t\tappend(scene->big"),
    text("scene_text_busy_read_title", "\"Fehlerspeicher lesen\"", "\"Fehlerspeicher wird gelesen\""),
    text("scene_text_busy_clear_title", "\"Fehlerspeicher löschen\"", "\"Fehlerspeicher wird gelöscht\""),
    ("scene_busy_title_of_request_in_state", T, F,
     "\tset_title(scene, reading ? \"Fehlerspeicher lesen\" : \"Fehlerspeicher löschen\");",
     "\tset_title(scene, (state != NULL && state->dtc.has_request ? !state->dtc.clear : reading) ? \"Fehlerspeicher lesen\" : \"Fehlerspeicher löschen\");"),
    ("scene_busy_titles_swapped", T, F,
     "\tset_title(scene, reading ? \"Fehlerspeicher lesen\" : \"Fehlerspeicher löschen\");", "\tset_title(scene, reading ? \"Fehlerspeicher löschen\" : \"Fehlerspeicher lesen\");"),
    ("scene_busy_state_not_checked", T, F,
     OWN, "\tif(accepted && state->dtc.seq == flow->seq && (state->dtc.phase == WICAN_DTC_RUNNING || state->dtc.phase == WICAN_DTC_DONE))"),
    ("scene_busy_step_of_any_request", T, F,
     OWN, "\tif(accepted && state != NULL && (state->dtc.phase == WICAN_DTC_RUNNING || state->dtc.phase == WICAN_DTC_DONE))"),
    ("scene_busy_step_of_later_request", T, F,
     OWN, "\tif(accepted && state != NULL && state->dtc.seq >= flow->seq && (state->dtc.phase == WICAN_DTC_RUNNING || state->dtc.phase == WICAN_DTC_DONE))"),
    ("scene_busy_step_of_earlier_request", T, F,
     OWN, "\tif(accepted && state != NULL && state->dtc.seq <= flow->seq && (state->dtc.phase == WICAN_DTC_RUNNING || state->dtc.phase == WICAN_DTC_DONE))"),
    ("scene_busy_number_by_low_byte", T, F,
     OWN, "\tif(accepted && state != NULL && (state->dtc.seq & 0xFFu) == (flow->seq & 0xFFu) && (state->dtc.phase == WICAN_DTC_RUNNING || state->dtc.phase == WICAN_DTC_DONE))"),
    ("scene_busy_done_is_sent", T, F, OWN, "\tif(accepted && state != NULL && state->dtc.seq == flow->seq && state->dtc.phase == WICAN_DTC_RUNNING)"),
    ("scene_busy_running_is_sent", T, F, OWN, "\tif(accepted && state != NULL && state->dtc.seq == flow->seq && state->dtc.phase == WICAN_DTC_DONE)"),
    ("scene_busy_queued_has_step", T, F,
     OWN, "\tif(accepted && state != NULL && state->dtc.seq == flow->seq && (state->dtc.phase == WICAN_DTC_QUEUED || state->dtc.phase == WICAN_DTC_RUNNING || state->dtc.phase == WICAN_DTC_DONE))"),
    ("scene_busy_error_has_step", T, F,
     OWN, "\tif(accepted && state != NULL && state->dtc.seq == flow->seq && state->dtc.phase >= WICAN_DTC_RUNNING)"),
    ("scene_busy_idle_has_step", T, F,
     OWN, "\tif(accepted && state != NULL && state->dtc.seq == flow->seq && state->dtc.phase != WICAN_DTC_QUEUED && state->dtc.phase != WICAN_DTC_ERROR)"),
    ("scene_busy_big_without_step", T, F, "\t\tappend_number(scene->big, sizeof(scene->big), dtc->step);\n", ""),
    ("scene_busy_big_without_total", T, F, "\t\tappend_number(scene->big, sizeof(scene->big), dtc->total);\n", ""),
    ("scene_busy_big_swapped", T, F,
     "\t\tappend_number(scene->big, sizeof(scene->big), dtc->step);\n\t\tappend(scene->big, sizeof(scene->big), \"/\");\n\t\tappend_number(scene->big, sizeof(scene->big), dtc->total);",
     "\t\tappend_number(scene->big, sizeof(scene->big), dtc->total);\n\t\tappend(scene->big, sizeof(scene->big), \"/\");\n\t\tappend_number(scene->big, sizeof(scene->big), dtc->step);"),
    text("scene_text_busy_slash", "\t\tappend(scene->big, sizeof(scene->big), \"/\");", "\t\tappend(scene->big, sizeof(scene->big), \" von \");"),
    ("scene_busy_step_zero_has_name", T, F, ENGINE + "\t\telse dtc_short_name", "\t\tdtc_short_name"),
    ("scene_busy_engine_check_always", T, F, ENGINE + SHORT_NAME, "\t\tadd_text(scene, \"Prüfe Motor …\");"),
    ("scene_busy_engine_check_at_step_one", T, F, ENGINE, "\t\tif(dtc->step <= 1) add_text(scene, \"Prüfe Motor …\");\n"),
    ("scene_busy_engine_check_only_before_a_read", T, F, ENGINE, "\t\tif(dtc->step == 0 && reading) add_text(scene, \"Prüfe Motor …\");\n"),
    ("scene_busy_engine_check_without_name", T, F, ENGINE, "\t\tif(dtc->step == 0 || dtc->name[0] == '\\0') add_text(scene, \"Prüfe Motor …\");\n"),
    text("scene_text_busy_engine", "\"Prüfe Motor …\"", "\"Prüfe Motor\""),
    ("scene_busy_whole_name", T, F, SHORT_NAME, "\t\telse add_text(scene, dtc->name);"),
    ("scene_busy_no_name_no_line", T, F, SHORT_NAME, "\t\telse if(dtc->name[0] != '\\0') dtc_short_name(dtc->name, add_line(scene), SCENE_TEXT_SIZE);"),
    ("scene_busy_name_in_short_room", T, F, SHORT_NAME, "\t\telse dtc_short_name(dtc->name, add_line(scene), SCENE_SHORT_SIZE);"),
    ("scene_busy_total_zero_divides", T, F,
     STEP_PERMILLE, "\t\tscene->permille = dtc->step >= dtc->total ? 1000 : (int)((uint64_t)dtc->step * 1000u / dtc->total);"),
    ("scene_busy_beyond_total_not_clamped", T, F,
     STEP_PERMILLE, "\t\tif(dtc->total != 0) scene->permille = (int)((uint64_t)dtc->step * 1000u / dtc->total);"),
    ("scene_busy_last_step_not_full", T, F,
     STEP_PERMILLE, "\t\tif(dtc->total != 0) scene->permille = dtc->step > dtc->total ? 1000 : (int)((uint64_t)dtc->step * 999u / dtc->total);"),
    ("scene_busy_permille_in_32_bit", T, F,
     STEP_PERMILLE, "\t\tif(dtc->total != 0) scene->permille = dtc->step >= dtc->total ? 1000 : (int)(dtc->step * 1000u / dtc->total);"),
    ("scene_busy_permille_in_percent", T, F,
     STEP_PERMILLE, "\t\tif(dtc->total != 0) scene->permille = dtc->step >= dtc->total ? 1000 : (int)((uint64_t)dtc->step * 100u / dtc->total) * 10;"),
    ("scene_busy_permille_rounded_up", T, F,
     STEP_PERMILLE, "\t\tif(dtc->total != 0) scene->permille = dtc->step >= dtc->total ? 1000 : (int)(((uint64_t)dtc->step * 1000u + dtc->total - 1) / dtc->total);"),
    ("scene_busy_permille_never", T, F, STEP_PERMILLE + "\n", ""),
    ("scene_busy_sent_without_big", T, F, "\t\tappend(scene->big, sizeof(scene->big), \"…\");\n\t\tadd_text(scene, \"Auftrag gesendet\");", "\t\tadd_text(scene, \"Auftrag gesendet\");"),
    ("scene_busy_sent_without_line", T, F, "\t\tadd_text(scene, \"Auftrag gesendet\");\n", ""),
    text("scene_text_busy_sent", "\"Auftrag gesendet\"", "\"Auftrag wird gesendet\""),
    text("scene_text_busy_sent_big", "\t\tappend(scene->big, sizeof(scene->big), \"…\");\n\t\tadd_text(scene, \"Auftrag gesendet\");",
         "\t\tappend(scene->big, sizeof(scene->big), \"...\");\n\t\tadd_text(scene, \"Auftrag gesendet\");"),
    text("scene_text_busy_over_big", "\t\tappend(scene->big, sizeof(scene->big), \"…\");\n\t\treturn;", "\t\tappend(scene->big, sizeof(scene->big), \"...\");\n\t\treturn;"),
    ("scene_busy_without_hint", T, F, HINT, ""),
    ("scene_busy_hint_first", T, F,
     "\tset_title(scene, reading ? \"Fehlerspeicher lesen\" : \"Fehlerspeicher löschen\");\n", "\tset_title(scene, reading ? \"Fehlerspeicher lesen\" : \"Fehlerspeicher löschen\");\n"
     "\tadd_text(scene, \"ca. 35 s – Live-Werte pausieren \");\n"),
    ("scene_busy_hint_only_while_sent", T, F,
     "\t\tadd_text(scene, \"Auftrag gesendet\");\n\t}\n" + HINT, "\t\tadd_text(scene, \"Auftrag gesendet\");\n\t\tadd_text(scene, \"ca. 35 s – Live-Werte pausieren\");\n\t}\n"),
    text("scene_text_busy_hint", "\"ca. 35 s – Live-Werte pausieren\"", "\"ca. 35 s - Live-Werte pausieren\""),

    # the list of the own read
    text("scene_text_list_title", "\tset_title(scene, \"Fehlerspeicher\");\n\tif(world->can_clear)", "\tset_title(scene, \"Liste\");\n\tif(world->can_clear)"),
    text("scene_text_list_read", "{\"Erneut lesen\", \"\", world->can_read}", "{\"Lesen\", \"\", world->can_read}"),
    text("scene_text_list_clear", "{\"Fehler löschen\", \"\", world->can_clear}", "{\"Löschen\", \"\", world->can_clear}"),
    text("scene_text_list_back", LIST_CLEAR + "\t\t{\"Zurück\", \"\", true},", LIST_CLEAR + "\t\t{\"Zurueck\", \"\", true},"),
    ("scene_list_read_always_offered", T, F, LIST_READ, "\t\t{\"Erneut lesen\", \"\", true},\n"),
    ("scene_list_read_by_clear", T, F, LIST_READ, "\t\t{\"Erneut lesen\", \"\", world->can_clear},\n"),
    ("scene_list_read_by_block", T, F, LIST_READ, "\t\t{\"Erneut lesen\", \"\", input->read_block == DTC_FLOW_ALLOWED},\n"),
    ("scene_list_clear_always_offered", T, F, LIST_CLEAR, "\t\t{\"Fehler löschen\", \"\", true},\n"),
    ("scene_list_clear_by_read", T, F, LIST_CLEAR, "\t\t{\"Fehler löschen\", \"\", world->can_read},\n"),
    ("scene_list_clear_by_block", T, F, LIST_CLEAR, "\t\t{\"Fehler löschen\", \"\", input->clear_block == DTC_FLOW_ALLOWED},\n"),
    ("scene_list_clear_only_with_time_left", T, F,
     LIST_CLEAR, "\t\t{\"Fehler löschen\", \"\", world->can_clear && dtc_flow_seconds_left(input->flow, input->now_ms) > 0},\n"),
    ("scene_list_rows_swapped", T, F, LIST_READ + LIST_CLEAR, LIST_CLEAR + LIST_READ),
    ("scene_list_note_by_block", T, F, LIST_NOTE, "\tif(input->clear_block == DTC_FLOW_ALLOWED)\n\t{\n\t\tset_note(scene, \"Löschen möglich: \");"),
    ("scene_list_note_always_time", T, F, LIST_NOTE, "\tif(true)\n\t{\n\t\tset_note(scene, \"Löschen möglich: \");"),
    ("scene_list_note_never_time", T, F, LIST_NOTE, "\tif(false)\n\t{\n\t\tset_note(scene, \"Löschen möglich: \");"),
    ("scene_list_note_needs_a_focus", T, F,
     LIST_NOTE, "\tif(world->can_clear && input->nav->row >= 0)\n\t{\n\t\tset_note(scene, \"Löschen möglich: \");"),
    ("scene_list_note_time_only_with_codes", T, F,
     LIST_NOTE, "\tif(world->can_clear && input->flow->list_count > 0)\n\t{\n\t\tset_note(scene, \"Löschen möglich: \");"),
    ("scene_list_note_by_time_left", T, F,
     LIST_NOTE, "\tif(world->can_clear && dtc_flow_seconds_left(input->flow, input->now_ms) > 0)\n\t{\n\t\tset_note(scene, \"Löschen möglich: \");"),
    text("scene_text_list_note", "\"Löschen möglich: \"", "\"Löschen möglich:\""),
    ("scene_list_note_without_time", T, F, LIST_TIME, ""),
    ("scene_list_note_time_of_release", T, F,
     LIST_TIME, "\t\tappend_time(scene->note, sizeof(scene->note), access_seconds_left(input->access, input->now_ms));\n"),
    ("scene_list_note_time_in_seconds", T, F,
     LIST_TIME, "\t\tappend_number(scene->note, sizeof(scene->note), dtc_flow_seconds_left(input->flow, input->now_ms));\n"),
    ("scene_list_note_time_at_zero", T, F,
     LIST_TIME, "\t\tappend_time(scene->note, sizeof(scene->note), dtc_flow_seconds_left(input->flow, 0));\n"),
    ("scene_list_note_no_block", T, F, LIST_BLOCK + "\n", ""),
    ("scene_list_note_block_of_read", T, F, LIST_BLOCK, "\t\tset_note(scene, text_block(input->read_block));"),
    ("scene_list_lines_of_old_list", T, F,
     "\tbuild_rows(input, input->list, NULL, choices, COUNT(choices), scene);", "\tbuild_rows(input, input->old, NULL, choices, COUNT(choices), scene);"),
    ("scene_list_lines_of_outcome", T, F,
     "\tbuild_rows(input, input->list, NULL, choices, COUNT(choices), scene);", "\tbuild_rows(input, input->cleared, NULL, choices, COUNT(choices), scene);"),

    # the dialogs
    text("scene_text_cancel", "\"Abbrechen\"", "\"Abbruch\""),
    ("scene_choice_is_no_choice", T, F, "\tscene->kind = SCENE_CHOICE;\n", ""),
    ("scene_choice_answers_swapped", T, F,
     "\tappend(scene->options[0], sizeof(scene->options[0]), \"Abbrechen\");\n\tappend(scene->options[1], sizeof(scene->options[1]), act);",
     "\tappend(scene->options[1], sizeof(scene->options[1]), \"Abbrechen\");\n\tappend(scene->options[0], sizeof(scene->options[0]), act);"),
    ("scene_choice_without_action", T, F, "\tappend(scene->options[1], sizeof(scene->options[1]), act);\n", "\t(void)act;\n"),
    ("scene_choice_focus_not_told", T, F, OPTION, "\t(void)input;"),
    ("scene_choice_focus_passed_on", T, F, OPTION, "\tscene->option = input->nav->row;"),
    ("scene_choice_focus_beyond_is_cancel", T, F, OPTION, "\tscene->option = input->nav->row == 1 ? 1 : 0;"),
    ("scene_choice_focus_before_is_action", T, F, OPTION, "\tscene->option = input->nav->row != 0 ? 1 : 0;"),
    ("scene_choice_focus_always_action", T, F, OPTION, "\t(void)input;\n\tscene->option = 1;"),
    ("scene_choice_focus_by_low_bit", T, F, OPTION, "\tscene->option = input->nav->row & 1;"),
    ("scene_choice_focus_zero_is_action", T, F, OPTION, "\tscene->option = input->nav->row >= 0 ? 1 : 0;"),
    text("scene_text_clear_title", "\"Fehler löschen?\"", "\"Löschen?\""),
    ("scene_clear_summary_not_checked", T, F, SUMMARY, SUMMARY.replace("if(input->summary != NULL) ", "")),
    ("scene_clear_summary_never", T, F, SUMMARY, "\tadd_text(scene, \"Betrifft alle"),
    ("scene_clear_summary_needs_codes", T, F,
     SUMMARY, SUMMARY.replace("if(input->summary != NULL) ", "if(input->summary != NULL && input->summary->codes > 0) ")),
    ("scene_clear_summary_is_the_number_of_codes", T, F,
     SUMMARY, SUMMARY.replace("add_summary(scene, input->summary);", "append_number(add_line(scene), SCENE_TEXT_SIZE, input->summary->codes);")),
    # the words for what a list holds, the same on the start of the fault memory and in the clear dialog
    ("scene_summary_without_codes", T, F, CODES, ""),
    ("scene_summary_without_units", T, F, WITH_CODES + "\n", ""),
    ("scene_summary_numbers_swapped", T, F,
     CODES + "\tappend(line, SCENE_TEXT_SIZE, \" Fehler in \");\n" + WITH_CODES,
     WITH_CODES + "\n\tappend(line, SCENE_TEXT_SIZE, \" Fehler in \");\n" + CODES.rstrip("\n")),
    ("scene_summary_units_not_ok", T, F, WITH_CODES, "\tappend_number(line, SCENE_TEXT_SIZE, summary->ecus_not_ok);"),
    ("scene_summary_units_clean", T, F, WITH_CODES, "\tappend_number(line, SCENE_TEXT_SIZE, summary->ecus_clean);"),
    ("scene_summary_codes_as_int", T, F, CODES, "\tappend_number(line, SCENE_TEXT_SIZE, (int)summary->codes);\n"),
    text("scene_text_summary_codes", "\" Fehler in \"", "\" Fehler, \""),
    ("scene_summary_always_plural", T, F, UNITS, "\tappend(line, SCENE_TEXT_SIZE, \" Steuergeräten\");"),
    ("scene_summary_always_singular", T, F, UNITS, "\tappend(line, SCENE_TEXT_SIZE, \" Steuergerät\");"),
    ("scene_summary_singular_up_to_one", T, F, UNITS, "\tappend(line, SCENE_TEXT_SIZE, summary->ecus_with_codes <= 1 ? \" Steuergerät\" : \" Steuergeräten\");"),
    ("scene_summary_singular_by_codes", T, F, UNITS, "\tappend(line, SCENE_TEXT_SIZE, summary->codes == 1 ? \" Steuergerät\" : \" Steuergeräten\");"),
    ("scene_summary_singular_by_last_digit", T, F, UNITS, "\tappend(line, SCENE_TEXT_SIZE, summary->ecus_with_codes % 10 == 1 ? \" Steuergerät\" : \" Steuergeräten\");"),
    ("scene_summary_plural_nominative", T, F, UNITS, "\tappend(line, SCENE_TEXT_SIZE, summary->ecus_with_codes == 1 ? \" Steuergerät\" : \" Steuergeräte\");"),
    ("scene_clear_without_first_warning", T, F, WARN_ALL, ""),
    ("scene_clear_without_second_warning", T, F, WARN_ENGINE, ""),
    ("scene_clear_warnings_swapped", T, F, WARN_ALL + WARN_ENGINE, WARN_ENGINE + WARN_ALL),
    ("scene_clear_warnings_only_with_summary", T, F,
     WARN_ALL, "\tif(input->summary != NULL) add_text(scene, \"Betrifft alle Steuergeräte, auch SRS und ESP.\");\n"),
    text("scene_text_clear_all", "\"Betrifft alle Steuergeräte, auch SRS und ESP.\"", "\"Betrifft alle Steuergeräte.\""),
    text("scene_text_clear_engine", "\"Zündung an, Motor aus, Fahrzeug steht.\"", "\"Zündung an, Motor aus.\""),
    text("scene_text_clear_note", "\"Auf Löschen drehen, Knopf 3 s halten\"", "\"Auf Löschen drehen, Knopf halten\""),
    ("scene_clear_without_note", T, F, "\tset_note(scene, \"Auf Löschen drehen, Knopf 3 s halten\");\n", ""),
    text("scene_text_clear_action", "\tbuild_choice(input, \"Löschen\", scene);", "\tbuild_choice(input, \"Ausführen\", scene);"),
    ("scene_clear_without_hold", T, F, HOLD, ""),
    ("scene_clear_hold_at_time_zero", T, F, HOLD, "\tscene->permille = hold_permille(input->hold, 0);\n"),
    ("scene_clear_hold_only_on_action", T, F, HOLD, "\tscene->permille = scene->option == 1 ? hold_permille(input->hold, input->now_ms) : -1;\n"),
    ("scene_clear_hold_zero_on_cancel", T, F, HOLD, "\tscene->permille = scene->option == 1 ? hold_permille(input->hold, input->now_ms) : 0;\n"),
    ("scene_clear_hold_full_or_nothing", T, F, HOLD, "\tscene->permille = hold_permille(input->hold, input->now_ms) >= 1000 ? 1000 : 0;\n"),
    text("scene_text_confirm_action", "\tbuild_choice(input, \"Ausführen\", scene);", "\tbuild_choice(input, \"OK\", scene);"),
    text("scene_text_confirm_reboot", "\"Neu starten?\"", "\"Neustart?\""),
    text("scene_text_confirm_previous", "\"Vorherige Version starten?\"", "\"Vorherige Version?\""),
    text("scene_text_confirm_reset", "\t\t\tset_title(scene, \"Werkseinstellungen?\");", "\t\t\tset_title(scene, \"Zurücksetzen?\");"),
    text("scene_text_confirm_reset_erased", "\"WLAN, Kopplung und Einstellungen werden gelöscht.\"", "\"WLAN und Einstellungen werden gelöscht.\""),
    text("scene_text_confirm_reset_kept", "\"Die Ansichten bleiben.\"", "\"Die Ansichten bleiben erhalten.\""),
    ("scene_confirm_reboot_without_title", T, F, ASK_REBOOT, "\t\tcase NAV_DO_REBOOT:\n\t\t\tbreak;\n"),
    ("scene_confirm_previous_without_title", T, F, ASK_PREVIOUS, "\t\tcase NAV_DO_PREVIOUS_FIRMWARE:\n\t\t\tbreak;\n"),
    ("scene_confirm_titles_swapped", T, F,
     ASK_REBOOT + ASK_PREVIOUS, "\t\tcase NAV_DO_PREVIOUS_FIRMWARE:\n\t\t\tset_title(scene, \"Neu starten?\");\n\t\t\tbreak;\n"
     "\t\tcase NAV_DO_REBOOT:\n\t\t\tset_title(scene, \"Vorherige Version starten?\");\n\t\t\tbreak;\n"),
    ("scene_confirm_reset_without_erased", T, F, ASK_RESET_ERASED, ""),
    ("scene_confirm_reset_without_kept", T, F, ASK_RESET_KEPT, ""),
    ("scene_confirm_reset_lines_swapped", T, F, ASK_RESET_ERASED + ASK_RESET_KEPT, ASK_RESET_KEPT + ASK_RESET_ERASED),
    ("scene_confirm_reset_kept_only_on_first_rows", T, F, ASK_RESET_KEPT, "\t\t\tif(input->nav->row <= 1) add_text(scene, \"Die Ansichten bleiben.\");\n"),
    ("scene_confirm_reset_lines_for_previous", T, F,
     "\t\t\tset_title(scene, \"Vorherige Version starten?\");\n\t\t\tbreak;\n", "\t\t\tset_title(scene, \"Vorherige Version starten?\");\n"
     "\t\t\tadd_text(scene, \"Die Ansichten bleiben.\");\n\t\t\tbreak;\n"),
    ("scene_confirm_unknown_is_reboot", T, F,
     "\t\tdefault:\n\t\t\tbreak;\n\t}\n\tbuild_choice(input, \"Ausführen\", scene);", "\t\tdefault:\n\t\t\tset_title(scene, \"Neu starten?\");\n\t\t\tbreak;\n\t}\n"
     "\tbuild_choice(input, \"Ausführen\", scene);"),
    ("scene_confirm_by_low_byte", T, F, "\tswitch(input->nav->confirm)", "\tswitch((nav_do_t)((unsigned)input->nav->confirm & 0xFFu))"),
    ("scene_confirm_has_hold", T, F,
     "\tbuild_choice(input, \"Ausführen\", scene);", "\tbuild_choice(input, \"Ausführen\", scene);\n\tscene->permille = hold_permille(input->hold, input->now_ms);"),

    # the failure
    ("scene_failed_is_no_notice", T, F, "\tscene->kind = SCENE_NOTICE;\n\tset_note(scene, \"Knopf drücken\");", "\tset_note(scene, \"Knopf drücken\");"),
    text("scene_text_failed_note", "\"Knopf drücken\"", "\"Knopf\""),
    ("scene_failed_note_only_when_failed", T, F,
     "\tscene->kind = SCENE_NOTICE;\n\tset_note(scene, \"Knopf drücken\");\n", "\tscene->kind = SCENE_NOTICE;\n"
     "\tif(flow->phase == DTC_FLOW_FAILED || flow->phase == DTC_FLOW_UNKNOWN) set_note(scene, \"Knopf drücken\");\n"),
    ("scene_failed_reason_of_state", T, F,
     "\tconst char *text = text_reason(flow->reason);", "\tconst char *text = text_reason(state != NULL && state->dtc.reason[0] != '\\0' ? state->dtc.reason : flow->reason);"),
    ("scene_failed_unknown_word_is_empty", T, F, WORD, ""),
    ("scene_failed_always_the_word", T, F, WORD, "\t\ttext = flow->reason;\n"),
    ("scene_failed_sleeping_for_every_reason", T, F,
     SLEEPING, "\t\tif(state != NULL && state->sleep_in_s == 0) text = \"WiCAN schaltet ab – später erneut lesen\";"),
    ("scene_failed_sleeping_never", T, F, SLEEPING, "\t\t(void)state;"),
    ("scene_failed_sleeping_state_not_checked", T, F,
     SLEEPING, "\t\tif(strcmp(flow->reason, \"not_ready\") == 0 && state->sleep_in_s == 0) text = \"WiCAN schaltet ab – später erneut lesen\";"),
    ("scene_failed_sleeping_without_state", T, F,
     SLEEPING, "\t\tif(strcmp(flow->reason, \"not_ready\") == 0 && (state == NULL || state->sleep_in_s == 0)) text = \"WiCAN schaltet ab – später erneut lesen\";"),
    ("scene_failed_sleeping_while_counting", T, F,
     SLEEPING, "\t\tif(strcmp(flow->reason, \"not_ready\") == 0 && state != NULL && state->sleep_in_s >= 0) text = \"WiCAN schaltet ab – später erneut lesen\";"),
    ("scene_failed_sleeping_while_not_counting", T, F,
     SLEEPING, "\t\tif(strcmp(flow->reason, \"not_ready\") == 0 && state != NULL && state->sleep_in_s <= 0) text = \"WiCAN schaltet ab – später erneut lesen\";"),
    ("scene_failed_sleeping_only_with_ignition", T, F,
     SLEEPING, "\t\tif(strcmp(flow->reason, \"not_ready\") == 0 && state != NULL && state->sleep_in_s == 0 && state->ecu_online) text = \"WiCAN schaltet ab – später erneut lesen\";"),
    ("scene_failed_sleeping_only_while_autopid_runs", T, F,
     SLEEPING, "\t\tif(strcmp(flow->reason, \"not_ready\") == 0 && state != NULL && state->sleep_in_s == 0 && state->autopid == WICAN_AUTOPID_RUN) text = \"WiCAN schaltet ab – später erneut lesen\";"),
    ("scene_failed_sleeping_by_beginning_of_word", T, F,
     SLEEPING, "\t\tif(strncmp(flow->reason, \"not_ready\", 9) == 0 && state != NULL && state->sleep_in_s == 0) text = \"WiCAN schaltet ab – später erneut lesen\";"),
    text("scene_text_failed_sleeping", "\"WiCAN schaltet ab – später erneut lesen\"", "\"WiCAN schaltet ab\""),
    text("scene_text_failed_title", "\"Fehlgeschlagen\"", "\"Fehler\""),
    ("scene_failed_without_title", T, F, "\t\tset_title(scene, \"Fehlgeschlagen\");\n", ""),
    ("scene_failed_without_reason", T, F, "\t\tset_title(scene, \"Fehlgeschlagen\");\n\t\tadd_text(scene, text);", "\t\tset_title(scene, \"Fehlgeschlagen\");"),
    ("scene_failed_empty_reason_no_line", T, F,
     "\t\tset_title(scene, \"Fehlgeschlagen\");\n\t\tadd_text(scene, text);", "\t\tset_title(scene, \"Fehlgeschlagen\");\n\t\tif(text[0] != '\\0') add_text(scene, text);"),
    ("scene_failed_phase_of_world", T, F, FAILED, "\tif(input->world->flow == DTC_FLOW_FAILED)\n"),
    ("scene_failed_in_every_phase", T, F, FAILED, "\tif(flow->phase != DTC_FLOW_UNKNOWN)\n"),
    ("scene_failed_by_low_byte", T, F, FAILED, "\tif(((unsigned)flow->phase & 0xFFu) == DTC_FLOW_FAILED)\n"),
    ("scene_failed_unknown_is_failed", T, F, FAILED, "\tif(flow->phase == DTC_FLOW_FAILED || flow->phase == DTC_FLOW_UNKNOWN)\n"),
    ("scene_failed_unknown_in_other_phases", T, F, UNKNOWN, "\telse if(flow->phase != DTC_FLOW_IDLE)\n"),
    ("scene_failed_unknown_by_low_byte", T, F, UNKNOWN, "\telse if(((unsigned)flow->phase & 0xFFu) == DTC_FLOW_UNKNOWN)\n"),
    ("scene_failed_unknown_is_other", T, F, UNKNOWN, "\telse if(false)\n"),
    ("scene_failed_cleared_is_unknown", T, F, UNKNOWN, "\telse if(flow->phase == DTC_FLOW_UNKNOWN || flow->phase == DTC_FLOW_CLEARED)\n"),
    text("scene_text_unknown_title", "\"Stand unbekannt\"", "\"Unbekannt\""),
    text("scene_text_unknown", "\"Stand des Löschens unbekannt – bitte erneut lesen\"", "\"Stand des Löschens unbekannt\""),
    ("scene_unknown_without_text", T, F, "\t\tadd_text(scene, \"Stand des Löschens unbekannt – bitte erneut lesen\");\n", ""),
    text("scene_text_failed_other_title", "\telse\n\t{\n\t\tset_title(scene, \"Fehlerspeicher\");\n\t}", "\telse\n\t{\n\t\tset_title(scene, \"Fehlgeschlagen\");\n\t}"),

    # the brightness
    ("scene_level_is_no_level", T, F, "\tscene->kind = SCENE_LEVEL;\n", ""),
    ("scene_level_permille_is_percent", T, F, LEVEL_VALUE, "\tint64_t permille = (int64_t)input->nav->value;"),
    ("scene_level_permille_in_32_bit", T, F, LEVEL_VALUE, "\tint64_t permille = (int)((unsigned)input->nav->value * 10u);"),
    ("scene_level_permille_of_brightness_in_use", T, F, LEVEL_VALUE, "\tint64_t permille = (int64_t)input->world->brightness * 10;"),
    ("scene_level_negative_not_clamped", T, F, LEVEL_LOW, ""),
    ("scene_level_above_full_not_clamped", T, F, LEVEL_HIGH, ""),
    ("scene_level_full_at_99", T, F, LEVEL_HIGH, "\tif(permille >= 990) permille = 1000;\n"),
    ("scene_level_empty_at_1", T, F, LEVEL_LOW, "\tif(permille <= 10) permille = 0;\n"),
    ("scene_level_permille_not_told", T, F, "\tscene->permille = (int)permille;\n", ""),
    ("scene_level_title_swapped", T, F, LEVEL_TITLE, "\tset_title(scene, input->world->night_mode ? \"Helligkeit\" : \"Helligkeit (Nacht)\");"),
    ("scene_level_title_by_value", T, F,
     LEVEL_TITLE, "\tset_title(scene, input->world->night_mode && input->nav->value < 50 ? \"Helligkeit (Nacht)\" : \"Helligkeit\");"),
    ("scene_level_title_always_day", T, F, LEVEL_TITLE, "\tset_title(scene, \"Helligkeit\");"),
    text("scene_text_level_night", "\"Helligkeit (Nacht)\"", "\"Helligkeit Nacht\""),
    text("scene_text_level_day", " : \"Helligkeit\");", " : \"Helligkeit (Tag)\");"),
    ("scene_level_big_missing", T, F, LEVEL_BIG + "\n", ""),
    ("scene_level_big_of_brightness_in_use", T, F, LEVEL_BIG, "\tappend_percent(scene->big, sizeof(scene->big), input->world->brightness);"),
    ("scene_level_big_clamped", T, F, LEVEL_BIG, "\tappend_percent(scene->big, sizeof(scene->big), (int)(permille / 10));"),
    text("scene_text_level_note", "\"Drehen zum Ändern, Drücken zum Speichern\"", "\"Drehen zum Ändern\""),
    ("scene_level_without_note", T, F, "\tset_note(scene, \"Drehen zum Ändern, Drücken zum Speichern\");\n", ""),

    # the web access
    text("scene_text_web_title", "\tset_title(scene, \"Web-Zugriff\");", "\tset_title(scene, \"Web\");"),
    text("scene_text_web_release", "\t\t{\"Freigabe\", release, true},", "\t\t{\"Freigeben\", release, true},"),
    text("scene_text_web_back", "\t\t{\"Freigabe\", release, true},\n\t\t{\"Zurück\", \"\", true},", "\t\t{\"Freigabe\", release, true},\n\t\t{\"Zurueck\", \"\", true},"),
    text("scene_text_web_off", "\tchar release[SCENE_SHORT_SIZE] = \"aus\";", "\tchar release[SCENE_SHORT_SIZE] = \"zu\";"),
    text("scene_text_web_on", "\"an – noch \"", "\"an - noch \""),
    ("scene_web_release_by_world", T, F, RELEASE_ON, "\tif(input->world->release_open)\n"),
    ("scene_web_release_always_on", T, F, RELEASE_ON, "\tif(true)\n"),
    ("scene_web_release_never_on", T, F, RELEASE_ON, "\tif(false)\n"),
    ("scene_web_release_off_in_last_second", T, F, RELEASE_ON, "\tif(seconds > 1)\n"),
    ("scene_web_release_time_at_zero", T, F, RELEASE, "\tuint32_t seconds = access_seconds_left(input->access, 0);"),
    ("scene_web_release_time_of_question", T, F, RELEASE, "\tuint32_t seconds = access_ask_seconds_left(input->access, input->now_ms);"),
    ("scene_web_release_time_of_list", T, F, RELEASE, "\tuint32_t seconds = dtc_flow_seconds_left(input->flow, input->now_ms);"),
    ("scene_web_release_time_capped", T, F,
     "\t\tappend_time(release, sizeof(release), seconds);", "\t\tappend_time(release, sizeof(release), seconds > 600 ? 600 : seconds);"),
    ("scene_web_release_without_time", T, F, "\t\tappend_time(release, sizeof(release), seconds);\n", ""),
    ("scene_web_release_time_in_seconds", T, F, "\t\tappend_time(release, sizeof(release), seconds);", "\t\tappend_number(release, sizeof(release), seconds);"),
    ("scene_web_release_off_kept_before_on", T, F, "\t\trelease[0] = '\\0';\n", ""),
    ("scene_web_address_null_is_read", T, F, HAS_ADDRESS, "\tbool has_address = input->address[0] != '\\0';"),
    ("scene_web_address_empty_is_shown", T, F, HAS_ADDRESS, "\tbool has_address = input->address != NULL;"),
    ("scene_web_address_never_shown", T, F, HAS_ADDRESS, "\tbool has_address = false;"),
    ("scene_web_address_needs_two_bytes", T, F,
     HAS_ADDRESS, "\tbool has_address = input->address != NULL && input->address[0] != '\\0' && input->address[1] != '\\0';"),
    ("scene_web_address_not_with_access_point", T, F, ADDRESS, "\tif(has_address && !input->ap_on) add_text(scene, input->address);\n"),
    ("scene_web_no_address_no_line", T, F, NO_NETWORK, ""),
    ("scene_web_no_network_above_access_point", T, F, NO_NETWORK, "\telse add_text(scene, \"Kein WLAN\");\n"),
    ("scene_web_no_network_only_above_access_point", T, F, NO_NETWORK, "\telse if(input->ap_on) add_text(scene, \"Kein WLAN\");\n"),
    ("scene_web_no_network_above_access_point_without_name", T, F,
     NO_NETWORK, "\telse if(!input->ap_on || input->ap_ssid == NULL || input->ap_ssid[0] == '\\0') add_text(scene, \"Kein WLAN\");\n"),
    ("scene_web_no_network_above_access_point_for_null", T, F,
     NO_NETWORK, "\telse if(!input->ap_on || input->address == NULL) add_text(scene, \"Kein WLAN\");\n"),
    ("scene_web_no_network_above_access_point_for_empty", T, F,
     NO_NETWORK, "\telse if(!input->ap_on || input->address != NULL) add_text(scene, \"Kein WLAN\");\n"),
    ("scene_web_no_network_above_access_point_while_locked", T, F,
     NO_NETWORK, "\telse if(!input->ap_on || seconds == 0) add_text(scene, \"Kein WLAN\");\n"),
    ("scene_web_no_network_above_access_point_while_released", T, F,
     NO_NETWORK, "\telse if(!input->ap_on || seconds > 0) add_text(scene, \"Kein WLAN\");\n"),
    ("scene_web_no_network_left_out_leaves_empty_line", T, F,
     NO_NETWORK, "\telse add_text(scene, input->ap_on ? \"\" : \"Kein WLAN\");\n"),
    ("scene_web_no_network_also_with_address", T, F,
     NO_NETWORK, "\tif(!input->ap_on) add_text(scene, \"Kein WLAN\");\n"),
    ("scene_web_no_network_only_with_focus_on_release", T, F, NO_NETWORK, "\telse if(!input->ap_on && input->nav->row == 0) add_text(scene, \"Kein WLAN\");\n"),
    ("scene_web_no_network_above_access_point_in_safe_mode", T, F, NO_NETWORK, "\telse if(!input->ap_on || input->safe_mode) add_text(scene, \"Kein WLAN\");\n"),
    ("scene_web_no_network_only_while_released", T, F, NO_NETWORK, "\telse if(!input->ap_on && seconds > 0) add_text(scene, \"Kein WLAN\");\n"),
    ("scene_web_no_network_behind_access_point", T, F,
     NO_NETWORK + "\tif(input->ap_on)\n\t{\n", "\tif(input->ap_on)\n\t{\n\t\tif(!has_address) add_text(scene, \"Kein WLAN\");\n"),
    text("scene_text_web_no_wifi", "\"Kein WLAN\"", "\"Kein Netz\""),
    ("scene_web_access_point_always_shown", T, F, AP, "\tif(true)\n\t{\n"),
    ("scene_web_access_point_never_shown", T, F, AP, "\tif(false)\n\t{\n"),
    ("scene_web_access_point_only_with_name", T, F, AP, "\tif(input->ap_on && input->ap_ssid != NULL && input->ap_ssid[0] != '\\0')\n\t{\n"),
    ("scene_web_access_point_only_without_address", T, F, AP, "\tif(input->ap_on && (input->address == NULL || input->address[0] == '\\0'))\n\t{\n"),
    text("scene_text_web_name", "\"WLAN: \"", "\"WLAN \""),
    text("scene_text_web_password", "\"Passwort: \"", "\"Passwort \""),
    ("scene_web_name_and_password_swapped", T, F,
     AP_NAME, "\t\tappend(line, SCENE_TEXT_SIZE, \"WLAN: \");\n\t\tappend(line, SCENE_TEXT_SIZE, input->ap_password);\n"),
    ("scene_web_password_is_name", T, F,
     AP_PASSWORD, "\t\tappend(line, SCENE_TEXT_SIZE, \"Passwort: \");\n\t\tappend(line, SCENE_TEXT_SIZE, input->ap_ssid);\n"),
    ("scene_web_without_name", T, F, AP_NAME, "\t\tappend(line, SCENE_TEXT_SIZE, \"WLAN: \");\n"),
    ("scene_web_without_password", T, F, AP_PASSWORD, "\t\tappend(line, SCENE_TEXT_SIZE, \"Passwort: \");\n"),
    ("scene_web_password_on_line_of_name", T, F,
     "\t\tline = add_line(scene);\n\t\tappend(line, SCENE_TEXT_SIZE, \"Passwort: \");", "\t\tappend(line, SCENE_TEXT_SIZE, \"Passwort: \");"),

    # the settings
    text("scene_text_settings_title", "\tset_title(scene, \"Einstellungen\");", "\tset_title(scene, \"Einstellung\");"),
    text("scene_text_settings_reverse", "{\"Drehrichtung\", input->reverse", "{\"Richtung\", input->reverse"),
    text("scene_text_settings_reversed", "\"umgekehrt\"", "\"andersherum\""),
    text("scene_text_settings_normal", "\"umgekehrt\" : \"normal\"", "\"umgekehrt\" : \"üblich\""),
    text("scene_text_settings_ap", "{\"Hotspot\", input->ap_on", "{\"Zugangspunkt\", input->ap_on"),
    text("scene_text_settings_reboot", "\t\t{\"Neustart\", \"\", true},", "\t\t{\"Neu starten\", \"\", true},"),
    text("scene_text_settings_previous", "{\"Vorherige Version\", \"\", input->world->previous_firmware}", "{\"Alte Version\", \"\", input->world->previous_firmware}"),
    text("scene_text_settings_reset", "\t\t{\"Werkseinstellungen\", \"\", true},", "\t\t{\"Zurücksetzen\", \"\", true},"),
    text("scene_text_settings_back", "\t\t{\"Werkseinstellungen\", \"\", true},\n\t\t{\"Zurück\", \"\", true},", "\t\t{\"Werkseinstellungen\", \"\", true},\n\t\t{\"Zurueck\", \"\", true},"),
    ("scene_settings_direction_swapped", T, F, SET_REVERSE, "\t\t{\"Drehrichtung\", input->reverse ? \"normal\" : \"umgekehrt\", true},\n"),
    ("scene_settings_direction_always_normal", T, F, SET_REVERSE, "\t\t{\"Drehrichtung\", \"normal\", true},\n"),
    ("scene_settings_direction_by_access_point", T, F, SET_REVERSE, "\t\t{\"Drehrichtung\", input->ap_on ? \"umgekehrt\" : \"normal\", true},\n"),
    ("scene_settings_access_point_swapped", T, F, SET_AP, "\t\t{\"Hotspot\", input->ap_on ? \"aus\" : \"an\", true},\n"),
    ("scene_settings_access_point_always_off", T, F, SET_AP, "\t\t{\"Hotspot\", \"aus\", true},\n"),
    ("scene_settings_access_point_by_direction", T, F, SET_AP, "\t\t{\"Hotspot\", input->reverse ? \"an\" : \"aus\", true},\n"),
    ("scene_settings_access_point_not_in_safe_mode", T, F, SET_AP, "\t\t{\"Hotspot\", input->ap_on ? \"an\" : \"aus\", !input->safe_mode},\n"),
    ("scene_settings_previous_always_offered", T, F, SET_PREVIOUS, "\t\t{\"Vorherige Version\", \"\", true},\n"),
    ("scene_settings_previous_never_offered", T, F, SET_PREVIOUS, "\t\t{\"Vorherige Version\", \"\", false},\n"),
    ("scene_settings_previous_not_during_update", T, F,
     SET_PREVIOUS, "\t\t{\"Vorherige Version\", \"\", input->world->previous_firmware && !input->world->update_pending},\n"),
    ("scene_settings_rows_swapped", T, F,
     "\t\t{\"Neustart\", \"\", true},\n" + SET_PREVIOUS, SET_PREVIOUS + "\t\t{\"Neustart\", \"\", true},\n"),
    ("scene_settings_first_rows_swapped", T, F, SET_REVERSE + SET_AP, SET_AP + SET_REVERSE),

    # what lies over the screen
    ("scene_upload_negative_not_clamped", T, F, UPLOAD_LOW, ""),
    ("scene_upload_above_full_not_clamped", T, F, UPLOAD_HIGH, ""),
    ("scene_upload_full_at_99", T, F, UPLOAD_HIGH, "\t\t\tif(percent >= 99) percent = 100;\n"),
    ("scene_upload_empty_at_1", T, F, UPLOAD_LOW, "\t\t\tif(percent <= 1) percent = 0;\n"),
    ("scene_upload_is_no_overlay", T, F, "\t\t\tscene->over = SCENE_OVER_UPLOAD;\n", ""),
    ("scene_upload_without_text", T, F, UPLOAD_TEXT, ""),
    text("scene_text_upload", "\"Firmware wird übertragen\"", "\"Firmware wird geladen\""),
    ("scene_upload_without_percent", T, F, UPLOAD_PERCENT, ""),
    ("scene_upload_percent_first", T, F, UPLOAD_TEXT + UPLOAD_PERCENT, UPLOAD_PERCENT + UPLOAD_TEXT),
    ("scene_upload_percent_as_given", T, F,
     UPLOAD_PERCENT, "\t\t\tappend_percent(add_over_line(scene), SCENE_TEXT_SIZE, input->upload_percent);\n"),
    ("scene_upload_without_permille", T, F, UPLOAD_PERMILLE, ""),
    ("scene_upload_permille_is_percent", T, F, UPLOAD_PERMILLE, "\t\t\tscene->over_permille = percent;\n"),
    ("scene_ask_is_no_overlay", T, F, "\t\t\tscene->over = SCENE_OVER_ASK;\n", ""),
    ("scene_ask_without_question", T, F, QUESTION, "\t\t\t(void)asking;\n"),
    ("scene_ask_wifi_and_firmware_swapped", T, F,
     QUESTION, "\t\t\tadd_over_text(scene, asking == ACCESS_ASK_FIRMWARE ? \"WLAN speichern?\" : asking == ACCESS_ASK_WIFI ? \"Firmware installieren?\" :\n"
     "\t\t\t              asking == ACCESS_ASK_RESET ? \"Werkseinstellungen?\" : \"\");\n"),
    ("scene_ask_unknown_is_reset", T, F,
     QUESTION, "\t\t\tadd_over_text(scene, asking == ACCESS_ASK_WIFI ? \"WLAN speichern?\" : asking == ACCESS_ASK_FIRMWARE ? \"Firmware installieren?\" :\n"
     "\t\t\t              \"Werkseinstellungen?\");\n"),
    ("scene_ask_unknown_is_wifi", T, F,
     QUESTION, "\t\t\tadd_over_text(scene, asking == ACCESS_ASK_RESET ? \"Werkseinstellungen?\" : asking == ACCESS_ASK_FIRMWARE ? \"Firmware installieren?\" :\n"
     "\t\t\t              \"WLAN speichern?\");\n"),
    ("scene_ask_reset_has_no_words", T, F,
     QUESTION, "\t\t\tadd_over_text(scene, asking == ACCESS_ASK_WIFI ? \"WLAN speichern?\" : asking == ACCESS_ASK_FIRMWARE ? \"Firmware installieren?\" : \"\");\n"),
    ("scene_ask_question_of_access", T, F,
     "\taccess_ask_t asking = input->world->asking;", "\taccess_ask_t asking = access_asking(input->access, input->now_ms);"),
    ("scene_ask_firmware_is_reset_while_locked", T, F,
     "\taccess_ask_t asking = input->world->asking;", "\taccess_ask_t asking = input->world->asking == ACCESS_ASK_FIRMWARE && !input->world->release_open ? ACCESS_ASK_RESET : input->world->asking;"),
    ("scene_ask_question_by_low_byte", T, F,
     "\taccess_ask_t asking = input->world->asking;", "\taccess_ask_t asking = (access_ask_t)((unsigned)input->world->asking & 0xFFu);"),
    text("scene_text_ask_wifi", "\"WLAN speichern?\"", "\"WLAN-Daten speichern?\""),
    text("scene_text_ask_firmware", "\"Firmware installieren?\"", "\"Firmware starten?\""),
    text("scene_text_ask_reset", " ? \"Werkseinstellungen?\" : \"\");", " ? \"Zurücksetzen?\" : \"\");"),
    ("scene_ask_detail_null_is_read", T, F, DETAIL, "\t\t\tif(input->ask_detail[0] != '\\0') add_over_text(scene, input->ask_detail);\n"),
    ("scene_ask_empty_detail_is_a_line", T, F, DETAIL, "\t\t\tif(input->ask_detail != NULL) add_over_text(scene, input->ask_detail);\n"),
    ("scene_ask_detail_always_a_line", T, F, DETAIL, "\t\t\tadd_over_text(scene, input->ask_detail);\n"),
    ("scene_ask_without_detail", T, F, DETAIL, ""),
    ("scene_ask_detail_needs_two_bytes", T, F,
     DETAIL, "\t\t\tif(input->ask_detail != NULL && input->ask_detail[0] != '\\0' && input->ask_detail[1] != '\\0') add_over_text(scene, input->ask_detail);\n"),
    ("scene_ask_detail_only_for_wifi", T, F,
     DETAIL, "\t\t\tif(asking == ACCESS_ASK_WIFI && input->ask_detail != NULL && input->ask_detail[0] != '\\0') add_over_text(scene, input->ask_detail);\n"),
    ("scene_ask_detail_last", T, F,
     DETAIL + "\t\t\tline = add_over_line(scene);\n" + ANSWER + ASK_LEFT + "\t\t\tappend(line, SCENE_TEXT_SIZE, \" s)\");\n",
     "\t\t\tline = add_over_line(scene);\n" + ANSWER + ASK_LEFT + "\t\t\tappend(line, SCENE_TEXT_SIZE, \" s)\");\n" + DETAIL),
    text("scene_text_ask_answer", "\"Drücken = ja · lang = nein (\"", "\"Drücken = ja, lang = nein (\""),
    ("scene_ask_without_answer", T, F, ANSWER, ""),
    ("scene_ask_answer_only_while_waiting", T, F,
     "\t\t\tline = add_over_line(scene);\n" + ANSWER, "\t\t\tif(access_ask_seconds_left(input->access, input->now_ms) == 0) break;\n\t\t\tline = add_over_line(scene);\n" + ANSWER),
    ("scene_ask_without_seconds", T, F, ASK_LEFT, ""),
    ("scene_ask_seconds_at_time_zero", T, F, ASK_LEFT, "\t\t\tappend_number(line, SCENE_TEXT_SIZE, access_ask_seconds_left(input->access, 0));\n"),
    ("scene_ask_seconds_of_release", T, F, ASK_LEFT, "\t\t\tappend_number(line, SCENE_TEXT_SIZE, access_seconds_left(input->access, input->now_ms));\n"),
    ("scene_ask_seconds_as_minutes", T, F, ASK_LEFT, "\t\t\tappend_time(line, SCENE_TEXT_SIZE, access_ask_seconds_left(input->access, input->now_ms));\n"),
    text("scene_text_ask_seconds", "\t\t\tappend(line, SCENE_TEXT_SIZE, \" s)\");", "\t\t\tappend(line, SCENE_TEXT_SIZE, \"s)\");"),
    ("scene_update_is_no_overlay", T, F, "\t\t\tscene->over = SCENE_OVER_UPDATE;\n", ""),
    ("scene_update_without_question", T, F, UPDATE_1, ""),
    ("scene_update_without_answer", T, F, UPDATE_2, ""),
    ("scene_update_lines_swapped", T, F, UPDATE_1 + UPDATE_2, UPDATE_2 + UPDATE_1),
    text("scene_text_update", "\"Update in Ordnung?\"", "\"Update gut?\""),
    text("scene_text_update_answer", "\"Knopf drücken oder Bildschirm berühren\"", "\"Knopf drücken\""),
    text("scene_text_update_else", "\"sonst alte Version in \"", "\"sonst alte Version in\""),
    ("scene_update_without_time", T, F, UPDATE_LEFT, ""),
    ("scene_update_time_in_seconds", T, F, UPDATE_LEFT, "\t\t\tappend_number(line, SCENE_TEXT_SIZE, input->update_left_s);\n"),
    ("scene_update_time_of_question", T, F,
     UPDATE_LEFT, "\t\t\tappend_time(line, SCENE_TEXT_SIZE, access_ask_seconds_left(input->access, input->now_ms));\n"),
    ("scene_overlay_never", T, F, OVERLAY, "\t(void)build_overlay;\n"),
    ("scene_overlay_only_on_pages", T, F, OVERLAY, "\tif(input->nav->screen == NAV_PAGES) build_overlay(input, scene);\n"),
    ("scene_overlay_not_on_clear_dialog", T, F, OVERLAY, "\tif(input->nav->screen != NAV_DTC_CONFIRM) build_overlay(input, scene);\n"),
    ("scene_overlay_hides_the_hold", T, F, OVERLAY, "\tbuild_overlay(input, scene);\n\tif(scene->over != SCENE_OVER_NONE && scene->kind == SCENE_CHOICE) scene->permille = -1;\n"),
    ("scene_overlay_hides_the_ring", T, F, OVERLAY, "\tbuild_overlay(input, scene);\n\tif(scene->over != SCENE_OVER_NONE) scene->ring.kind = RING_NONE;\n"),
    ("scene_overlay_takes_the_focus", T, F,
     OVERLAY, "\tbuild_overlay(input, scene);\n\tif(scene->over != SCENE_OVER_NONE) for(int i = 0; i < SCENE_ROWS_MAX; i++) scene->rows[i].focus = false;\n"),

    # scene_build()
    ("scene_build_keeps_what_was_there", T, F, CLEAR, ""),
    # every field is cleared, but not the bytes between the fields of an item and of a row
    ("scene_build_clears_fields_only", T, F,
     CLEAR, "\tmemset(scene->title, 0, sizeof(scene->title));\n\tmemset(scene->note, 0, sizeof(scene->note));\n"
     "\tfor(int i = 0; i < LAYOUT_ITEMS_MAX; i++)\n\t{\n\t\tmemset(scene->items[i].label, 0, sizeof(scene->items[i].label));\n"
     "\t\tmemset(scene->items[i].text, 0, sizeof(scene->items[i].text));\n\t\tmemset(scene->items[i].unit, 0, sizeof(scene->items[i].unit));\n"
     "\t\tscene->items[i].tone = SCENE_TONE_NORMAL;\n\t\tscene->items[i].widget = LAYOUT_WIDGET_NUMBER;\n\t\tscene->items[i].permille = 0;\n\t}\n"
     "\tfor(int i = 0; i < SCENE_ROWS_MAX; i++)\n\t{\n\t\tscene->rows[i].kind = SCENE_ROW_ACTION;\n\t\tmemset(scene->rows[i].text, 0, sizeof(scene->rows[i].text));\n"
     "\t\tmemset(scene->rows[i].detail, 0, sizeof(scene->rows[i].detail));\n\t\tscene->rows[i].enabled = false;\n\t\tscene->rows[i].focus = false;\n\t}\n"
     "\tmemset(scene->lines, 0, sizeof(scene->lines));\n\tmemset(scene->big, 0, sizeof(scene->big));\n"
     "\tmemset(scene->options, 0, sizeof(scene->options));\n\tmemset(scene->over_lines, 0, sizeof(scene->over_lines));\n\tscene->kind = SCENE_VALUES;\n"
     "\tscene->item_count = 0;\n\tscene->dots = 0;\n\tscene->row_count = 0;\n\tscene->first = 0;\n\tscene->total = 0;\n\tscene->line_count = 0;\n"
     "\tscene->option = 0;\n\tscene->over = SCENE_OVER_NONE;\n\tscene->over_line_count = 0;\n"),
    ("scene_build_keeps_the_answer_in_focus", T, F, CLEAR, "\tint option = scene->option;\n\n\tmemset(scene, 0, sizeof(*scene));\n\tscene->option = option;\n"),
    ("scene_build_keeps_the_last_overlay_line", T, F,
     CLEAR, "\tmemset(scene, 0, offsetof(scene_t, over_lines) + 2 * sizeof(scene->over_lines[0]));\n\tscene->over_line_count = 0;\n"),
    ("scene_build_dot_zero_without_dots", T, F, "\tscene->dot = -1;\n", ""),
    ("scene_build_permille_zero_when_unused", T, F, "\tscene->permille = -1;\n", ""),
    ("scene_build_over_permille_zero_when_unused", T, F, "\tscene->over_permille = -1;\n", ""),
    ("scene_build_ring_ignores_level", T, F, RING, "\tscene->ring = ring_state(view, state, 0, old);"),
    ("scene_build_ring_ignores_old", T, F, RING, "\tscene->ring = ring_state(view, state, level, false);"),
    ("scene_build_ring_without_state", T, F, RING, "\tscene->ring = ring_state(view, NULL, level, old);"),
    ("scene_build_ring_always_live", T, F, RING, "\tscene->ring = ring_state(CONN_VIEW_LIVE, state, level, old);"),
    ("scene_build_ring_missing", T, F, RING + "\n", ""),
    ("scene_build_ring_only_on_pages", T, F, RING, "\tif(input->nav->screen == NAV_PAGES) scene->ring = ring_state(view, state, level, old);"),
    # no second arc on the two screens that have one
    ("scene_ring_second_arc", T, F, OWN_ARC, "\tif(false)"),
    ("scene_ring_second_arc_on_progress", T, F, OWN_ARC, "\tif(input->nav->screen == NAV_DTC_CONFIRM && scene->ring.kind == RING_PROGRESS)"),
    ("scene_ring_second_arc_on_clear_dialog", T, F, OWN_ARC, "\tif(input->nav->screen == NAV_DTC_BUSY && scene->ring.kind == RING_PROGRESS)"),
    ("scene_ring_off_on_every_dialog", T, F,
     OWN_ARC, "\tif((input->nav->screen == NAV_DTC_BUSY || input->nav->screen == NAV_DTC_CONFIRM || input->nav->screen == NAV_CONFIRM) && scene->ring.kind == RING_PROGRESS)"),
    ("scene_ring_off_on_every_choice", T, F, OWN_ARC, "\tif((scene->kind == SCENE_PROGRESS || scene->kind == SCENE_CHOICE) && scene->ring.kind == RING_PROGRESS)"),
    ("scene_ring_off_on_brightness", T, F,
     OWN_ARC, "\tif((input->nav->screen == NAV_DTC_BUSY || input->nav->screen == NAV_DTC_CONFIRM || input->nav->screen == NAV_BRIGHTNESS) && scene->ring.kind == RING_PROGRESS)"),
    ("scene_ring_off_on_fault_memory", T, F,
     OWN_ARC, "\tif(input->nav->screen >= NAV_DTC && input->nav->screen <= NAV_DTC_CONFIRM && scene->ring.kind == RING_PROGRESS)"),
    ("scene_ring_off_behind_the_menu", T, F, OWN_ARC, "\tif(input->nav->screen != NAV_PAGES && scene->ring.kind == RING_PROGRESS)"),
    ("scene_ring_off_during_every_scan", T, F, OWN_ARC, "\tif(scene->ring.kind == RING_PROGRESS)"),
    ("scene_ring_every_ring_off", T, F, OWN_ARC, "\tif(input->nav->screen == NAV_DTC_BUSY || input->nav->screen == NAV_DTC_CONFIRM)"),
    ("scene_ring_grey_off_as_well", T, F,
     OWN_ARC, "\tif((input->nav->screen == NAV_DTC_BUSY || input->nav->screen == NAV_DTC_CONFIRM) && (scene->ring.kind == RING_PROGRESS || scene->ring.kind == RING_GREY))"),
    ("scene_ring_yellow_off_as_well", T, F,
     OWN_ARC, "\tif((input->nav->screen == NAV_DTC_BUSY || input->nav->screen == NAV_DTC_CONFIRM) && (scene->ring.kind == RING_PROGRESS || scene->ring.kind == RING_YELLOW))"),
    ("scene_ring_off_only_once_begun", T, F, OWN_ARC, OWN_ARC[:-1] + " && scene->ring.permille > 0)"),
    ("scene_ring_off_only_for_own_request", T, F, OWN_ARC, OWN_ARC[:-1] + " && state->dtc.seq == input->flow->seq)"),
    ("scene_ring_off_only_while_under_way", T, F,
     OWN_ARC, OWN_ARC[:-1] + " && (input->nav->screen == NAV_DTC_CONFIRM || input->flow->phase == DTC_FLOW_READING || input->flow->phase == DTC_FLOW_CLEARING))"),
    ("scene_ring_off_only_while_held", T, F,
     OWN_ARC, "\tif((input->nav->screen == NAV_DTC_BUSY || (input->nav->screen == NAV_DTC_CONFIRM && scene->permille > 0)) && scene->ring.kind == RING_PROGRESS)"),
    ("scene_ring_off_not_under_an_overlay", T, F, OWN_ARC, OWN_ARC[:-1] + " && nav_overlay(input->world) == NAV_OVER_NONE)"),
    ("scene_ring_off_only_with_focus_on_an_answer", T, F, OWN_ARC, OWN_ARC[:-1] + " && input->nav->row >= 0 && input->nav->row <= 1)"),
    ("scene_ring_off_not_in_safe_mode", T, F, OWN_ARC, OWN_ARC[:-1] + " && !input->safe_mode)"),
    ("scene_ring_off_on_dialog_only_if_clearable", T, F,
     OWN_ARC, "\tif((input->nav->screen == NAV_DTC_BUSY || (input->nav->screen == NAV_DTC_CONFIRM && input->world->can_clear)) && scene->ring.kind == RING_PROGRESS)"),
    ("scene_ring_off_only_with_request_number", T, F, OWN_ARC, OWN_ARC[:-1] + " && input->flow->seq != 0)"),
    ("scene_ring_off_not_when_scan_is_complete", T, F, OWN_ARC, OWN_ARC[:-1] + " && scene->ring.permille < 1000)"),
    ("scene_ring_off_only_if_arcs_differ", T, F, OWN_ARC, OWN_ARC[:-1] + " && scene->permille != scene->ring.permille)"),
    ("scene_ring_off_takes_permille_of_screen", T, F, RING_OFF + "\t\tscene->ring.permille = 0;\n", RING_OFF + "\t\tscene->ring.permille = scene->permille;\n"),
    ("scene_ring_off_by_low_byte", T, F,
     OWN_ARC, "\tif((((unsigned)input->nav->screen & 0xFFu) == NAV_DTC_BUSY || ((unsigned)input->nav->screen & 0xFFu) == NAV_DTC_CONFIRM) && scene->ring.kind == RING_PROGRESS)"),
    ("scene_ring_off_keeps_permille", T, F, RING_OFF + "\t\tscene->ring.permille = 0;\n", RING_OFF),
    ("scene_ring_arc_of_nothing", T, F, RING_OFF, ""),
    ("scene_ring_second_arc_is_yellow", T, F, RING_OFF, "\t\tscene->ring.kind = RING_YELLOW;\n"),
    ("scene_ring_second_arc_is_grey", T, F, RING_OFF, "\t\tscene->ring.kind = RING_GREY;\n"),
    ("scene_build_view_at_time_zero", T, F,
     "\tconn_view_t view = conn_view(input->conn, input->now_ms);", "\tconn_view_t view = conn_view(input->conn, 0);"),
    ("scene_build_unknown_screen_is_a_page", T, F, NO_SCREEN, "\t\tdefault:\n\t\t\tbreak;"),
    ("scene_build_unknown_screen_is_the_menu", T, F, NO_SCREEN, "\t\tdefault:\n\t\t\tbuild_menu(input, scene);\n\t\t\tbreak;"),
    ("scene_build_screen_by_low_byte", T, F, "\tswitch(input->nav->screen)", "\tswitch((nav_screen_t)((unsigned)input->nav->screen & 0xFFu))"),
    ("scene_build_menu_is_pages", T, F, "\t\tcase NAV_MENU:\n\t\t\tbuild_menu(input, scene);", "\t\tcase NAV_MENU:\n\t\t\t(void)build_menu;\n\t\t\tbuild_pages(input, view, state, scene, &level, &old);"),
    ("scene_build_dtc_and_list_swapped", T, F,
     "\t\tcase NAV_DTC:\n\t\t\tbuild_dtc(input, scene);\n\t\t\tbreak;\n\t\tcase NAV_DTC_BUSY:\n\t\t\tbuild_busy(input, state, scene);\n\t\t\tbreak;\n\t\tcase NAV_DTC_LIST:",
     "\t\tcase NAV_DTC_LIST:\n\t\t\tbuild_dtc(input, scene);\n\t\t\tbreak;\n\t\tcase NAV_DTC_BUSY:\n\t\t\tbuild_busy(input, state, scene);\n\t\t\tbreak;\n\t\tcase NAV_DTC:"),
    ("scene_build_busy_without_state", T, F, "\t\t\tbuild_busy(input, state, scene);", "\t\t\tbuild_busy(input, NULL, scene);"),
    ("scene_build_failed_without_state", T, F, "\t\t\tbuild_failed(input, state, scene);", "\t\t\tbuild_failed(input, NULL, scene);"),
    text("scene_text_cleared_title", "\t\t\tset_title(scene, \"Gelöscht\");", "\t\t\tset_title(scene, \"Geloescht\");"),
    text("scene_text_cleared_done", "{{\"Fertig\", \"\", true}}", "{{\"OK\", \"\", true}}"),
    ("scene_cleared_done_disabled", T, F, "{{\"Fertig\", \"\", true}}", "{{\"Fertig\", \"\", false}}"),
    ("scene_cleared_lines_of_old_list", T, F, CLEARED, "\t\t\tset_title(scene, \"Gelöscht\");\n\t\t\tbuild_rows(input, input->old, NULL, done, COUNT(done), scene);"),
    ("scene_cleared_lines_of_list", T, F, CLEARED, "\t\t\tset_title(scene, \"Gelöscht\");\n\t\t\tbuild_rows(input, input->list, NULL, done, COUNT(done), scene);"),
    ("scene_cleared_ends_with_back", T, F, CLEARED, "\t\t\t(void)done;\n\t\t\tset_title(scene, \"Gelöscht\");\n\t\t\tbuild_rows(input, input->cleared, NULL, back, COUNT(back), scene);"),
    text("scene_text_old_title", "\t\t\tset_title(scene, \"Zuletzt gelöscht\");", "\t\t\tset_title(scene, \"Gelöscht\");"),
    text("scene_text_old_back", "{{\"Zurück\", \"\", true}}", "{{\"Zurueck\", \"\", true}}"),
    ("scene_old_lines_of_outcome", T, F, OLD_LIST, "\t\t\tset_title(scene, \"Zuletzt gelöscht\");\n\t\t\tbuild_rows(input, input->cleared, NULL, back, COUNT(back), scene);"),
    ("scene_old_lines_of_list", T, F, OLD_LIST, "\t\t\tset_title(scene, \"Zuletzt gelöscht\");\n\t\t\tbuild_rows(input, input->list, NULL, back, COUNT(back), scene);"),
    ("scene_old_ends_with_done", T, F, OLD_LIST, "\t\t\t(void)back;\n\t\t\tset_title(scene, \"Zuletzt gelöscht\");\n\t\t\tbuild_rows(input, input->old, NULL, done, COUNT(done), scene);"),
    text("scene_text_info_title", "\"Info\");\n\t\t\tbuild_rows(input, NULL, input->info", "\"Infos\");\n\t\t\tbuild_rows(input, NULL, input->info"),
    ("scene_info_ends_with_back", T, F, INFO, "\t\t\tset_title(scene, \"Info\");\n\t\t\tbuild_rows(input, NULL, input->info, back, COUNT(back), scene);"),
    ("scene_info_without_title", T, F, INFO, "\t\t\tbuild_rows(input, NULL, input->info, NULL, 0, scene);"),

    # scene_dump(): the writer
    ("scene_dump_writes_behind_the_room", T, F, PUT, "\twriter->out[writer->length] = c;\n"),
    ("scene_dump_writes_one_byte_behind", T, F, PUT, "\tif(writer->length <= writer->size) writer->out[writer->length] = c;\n"),
    ("scene_dump_text_without_zero_read_on", T, F, FIELD, "\t(void)size;\n\tfor(size_t i = 0; text[i] != '\\0'; i++) put_char(writer, text[i]);"),
    ("scene_dump_text_one_byte_short", T, F, FIELD, "\tfor(size_t i = 0; i + 1 < size && text[i] != '\\0'; i++) put_char(writer, text[i]);"),
    ("scene_dump_text_with_its_zeros", T, F, FIELD, "\tfor(size_t i = 0; i < size; i++) put_char(writer, text[i]);"),
    ("scene_dump_blank_behind_empty_text", T, F, BLANK, "\tput_char(writer, ' ');\n"),
    ("scene_dump_no_blank_before_text", T, F, BLANK, ""),
    ("scene_dump_text_line_without_end", T, F,
     "\tput_field(writer, text, size);\n\tput_char(writer, '\\n');", "\tput_field(writer, text, size);\n\tif(text[0] != '\\0') put_char(writer, '\\n');"),
    ("scene_dump_number_line_without_end", T, F,
     "\tput_number(writer, number);\n\tput_char(writer, '\\n');", "\tput_number(writer, number);"),
    ("scene_dump_flag_only_one_is_true", T, F, FLAG, "\treturn *(const unsigned char *)field == 1;"),
    ("scene_dump_flag_by_low_bit", T, F, FLAG, "\treturn (*(const unsigned char *)field & 1u) != 0;"),
    ("scene_dump_flag_read_as_bool", T, F, FLAG, "\treturn *field;"),
    ("scene_dump_unknown_member_read_behind_words", T, F, WORD_OF, "\t(void)count;\n\treturn words[member];"),
    ("scene_dump_unknown_member_is_first_word", T, F, WORD_OF, "\treturn member < (unsigned)count ? words[member] : words[0];"),
    ("scene_dump_last_member_unknown", T, F, WORD_OF, "\treturn member + 1 < (unsigned)count ? words[member] : \"?\";"),
    ("scene_dump_member_behind_last_read", T, F, WORD_OF, "\treturn member <= (unsigned)count ? words[member] : \"?\";"),
    ("scene_dump_member_by_low_byte", T, F, WORD_OF, "\treturn (member & 0xFFu) < (unsigned)count ? words[member & 0xFFu] : \"?\";"),
    text("scene_dump_text_unknown", WORD_OF, "\treturn member < (unsigned)count ? words[member] : \"-\";"),
    ("scene_dump_count_beyond_array", T, F, WITHIN, "\t(void)max;\n\treturn count;"),
    ("scene_dump_count_one_beyond_array", T, F, WITHIN, "\treturn count > max + 1 ? max + 1 : count;"),
    ("scene_dump_count_beyond_array_is_none", T, F, WITHIN, "\treturn count > max ? 0 : count;"),
    ("scene_dump_full_array_is_one_less", T, F, WITHIN, "\treturn count >= max ? max - 1 : count;"),

    # scene_dump(): the words
    ("scene_dump_kinds_swapped", T, F, "{\"values\", \"notice\", \"list\", \"progress\", \"choice\", \"level\"}", "{\"notice\", \"values\", \"list\", \"progress\", \"choice\", \"level\"}"),
    ("scene_dump_kinds_list_and_progress_swapped", T, F,
     "{\"values\", \"notice\", \"list\", \"progress\", \"choice\", \"level\"}", "{\"values\", \"notice\", \"progress\", \"list\", \"choice\", \"level\"}"),
    ("scene_dump_kinds_choice_and_level_swapped", T, F,
     "{\"values\", \"notice\", \"list\", \"progress\", \"choice\", \"level\"}", "{\"values\", \"notice\", \"list\", \"progress\", \"level\", \"choice\"}"),
    ("scene_dump_rings_swapped", T, F, "{\"none\", \"yellow\", \"grey\", \"red\", \"progress\"}", "{\"none\", \"grey\", \"yellow\", \"red\", \"progress\"}"),
    ("scene_dump_rings_none_and_red_swapped", T, F, "{\"none\", \"yellow\", \"grey\", \"red\", \"progress\"}", "{\"red\", \"yellow\", \"grey\", \"none\", \"progress\"}"),
    text("scene_dump_text_ring_progress", "\"red\", \"progress\"}", "\"red\", \"arc\"}"),
    ("scene_dump_tones_swapped", T, F, "{\"normal\", \"dim\", \"warn\", \"alarm\"}", "{\"dim\", \"normal\", \"warn\", \"alarm\"}"),
    ("scene_dump_tones_warn_and_alarm_swapped", T, F, "{\"normal\", \"dim\", \"warn\", \"alarm\"}", "{\"normal\", \"dim\", \"alarm\", \"warn\"}"),
    ("scene_dump_widgets_swapped", T, F, "{\"number\", \"arc\", \"bar\", \"state\"}", "{\"number\", \"bar\", \"arc\", \"state\"}"),
    ("scene_dump_widgets_number_and_state_swapped", T, F, "{\"number\", \"arc\", \"bar\", \"state\"}", "{\"state\", \"arc\", \"bar\", \"number\"}"),
    ("scene_dump_row_kinds_swapped", T, F, "{\"action\", \"head\", \"line\", \"sub\"}", "{\"action\", \"line\", \"head\", \"sub\"}"),
    ("scene_dump_row_kinds_action_and_sub_swapped", T, F, "{\"action\", \"head\", \"line\", \"sub\"}", "{\"sub\", \"head\", \"line\", \"action\"}"),
    ("scene_dump_overlays_swapped", T, F, "{\"none\", \"upload\", \"ask\", \"update\"}", "{\"none\", \"ask\", \"upload\", \"update\"}"),
    ("scene_dump_overlays_ask_and_update_swapped", T, F, "{\"none\", \"upload\", \"ask\", \"update\"}", "{\"none\", \"upload\", \"update\", \"ask\"}"),

    # scene_dump(): the lines
    text("scene_dump_text_kind", "\tput(&writer, \"kind: \");", "\tput(&writer, \"kind:\");"),
    text("scene_dump_text_ring", "\tput(&writer, \"\\nring: \");", "\tput(&writer, \"\\nring \");"),
    ("scene_dump_kind_and_ring_on_one_line", T, F, "\tput(&writer, \"\\nring: \");", "\tput(&writer, \" ring: \");"),
    ("scene_dump_ring_permille_always", T, F, RING_PERMILLE, "\tif(true)\n"),
    ("scene_dump_ring_permille_never", T, F, RING_PERMILLE, "\tif(false)\n"),
    ("scene_dump_ring_permille_unless_zero", T, F, RING_PERMILLE, "\tif(scene->ring.permille != 0)\n"),
    ("scene_dump_ring_permille_unless_none", T, F, RING_PERMILLE, "\tif(scene->ring.kind != RING_NONE)\n"),
    ("scene_dump_ring_permille_of_scene", T, F,
     "\t\tput_number(&writer, scene->ring.permille);", "\t\tput_number(&writer, scene->permille);"),
    text("scene_dump_text_title", "\"title:\"", "\"titel:\""),
    text("scene_dump_text_note", "\"note:\"", "\"notes:\""),
    ("scene_dump_title_and_note_swapped", T, F,
     "\tput_text_line(&writer, \"title:\", scene->title, sizeof(scene->title));\n\tput_text_line(&writer, \"note:\", scene->note, sizeof(scene->note));",
     "\tput_text_line(&writer, \"note:\", scene->note, sizeof(scene->note));\n\tput_text_line(&writer, \"title:\", scene->title, sizeof(scene->title));"),
    ("scene_dump_note_only_with_text", T, F,
     "\tput_text_line(&writer, \"note:\", scene->note, sizeof(scene->note));", "\tif(scene->note[0] != '\\0') put_text_line(&writer, \"note:\", scene->note, sizeof(scene->note));"),
    ("scene_dump_title_only_with_text", T, F,
     "\tput_text_line(&writer, \"title:\", scene->title, sizeof(scene->title));", "\tif(scene->title[0] != '\\0') put_text_line(&writer, \"title:\", scene->title, sizeof(scene->title));"),
    text("scene_dump_text_item", "\t\tput(&writer, \"item: \");", "\t\tput(&writer, \"item \");"),
    ("scene_dump_item_without_label", T, F, "\t\tput_field(&writer, item->label, sizeof(item->label));\n", ""),
    ("scene_dump_item_without_text", T, F, "\t\tput_field(&writer, item->text, sizeof(item->text));\n", ""),
    ("scene_dump_item_without_unit", T, F, "\t\tput_field(&writer, item->unit, sizeof(item->unit));\n", ""),
    ("scene_dump_item_text_and_unit_swapped", T, F,
     "\t\tput_field(&writer, item->text, sizeof(item->text));\n\t\tput(&writer, \" | \");\n\t\tput_field(&writer, item->unit, sizeof(item->unit));",
     "\t\tput_field(&writer, item->unit, sizeof(item->unit));\n\t\tput(&writer, \" | \");\n\t\tput_field(&writer, item->text, sizeof(item->text));"),
    ("scene_dump_item_label_in_room_of_text", T, F, "\t\tput_field(&writer, item->label, sizeof(item->label));", "\t\tput_field(&writer, item->label, sizeof(item->text));"),
    ("scene_dump_item_text_in_room_of_label", T, F, "\t\tput_field(&writer, item->text, sizeof(item->text));", "\t\tput_field(&writer, item->text, sizeof(item->label));"),
    ("scene_dump_item_unit_in_room_of_text", T, F, "\t\tput_field(&writer, item->unit, sizeof(item->unit));", "\t\tput_field(&writer, item->unit, sizeof(item->text));"),
    ("scene_dump_item_without_tone", T, F, "\t\tput(&writer, word(tones, COUNT(tones), (unsigned)item->tone));\n\t\tput(&writer, \" | \");\n", "\t\t(void)tones;\n"),
    ("scene_dump_item_without_widget", T, F, "\t\tput(&writer, word(widgets, COUNT(widgets), (unsigned)item->widget));\n\t\tput(&writer, \" | \");\n", "\t\t(void)widgets;\n"),
    ("scene_dump_item_tone_and_widget_swapped", T, F,
     "\t\tput(&writer, word(tones, COUNT(tones), (unsigned)item->tone));\n\t\tput(&writer, \" | \");\n\t\tput(&writer, word(widgets, COUNT(widgets), (unsigned)item->widget));",
     "\t\tput(&writer, word(widgets, COUNT(widgets), (unsigned)item->widget));\n\t\tput(&writer, \" | \");\n\t\tput(&writer, word(tones, COUNT(tones), (unsigned)item->tone));"),
    ("scene_dump_item_without_permille", T, F, "\t\tput_number(&writer, item->permille);\n", ""),
    ("scene_dump_item_permille_only_if_set", T, F, "\t\tput_number(&writer, item->permille);\n", "\t\tif(item->permille != -1) put_number(&writer, item->permille);\n"),
    ("scene_dump_items_all_the_first", T, F, "\t\tconst scene_item_t *item = &scene->items[i];", "\t\tconst scene_item_t *item = &scene->items[0];"),
    ("scene_dump_items_not_limited", T, F,
     "\tfor(int i = 0; i < within(scene->item_count, LAYOUT_ITEMS_MAX); i++)", "\tfor(int i = 0; i < scene->item_count; i++)"),
    ("scene_dump_items_only_for_values", T, F,
     "\tfor(int i = 0; i < within(scene->item_count, LAYOUT_ITEMS_MAX); i++)", "\tfor(int i = 0; scene->kind == SCENE_VALUES && i < within(scene->item_count, LAYOUT_ITEMS_MAX); i++)"),
    ("scene_dump_items_limited_like_rows", T, F,
     "\tfor(int i = 0; i < within(scene->item_count, LAYOUT_ITEMS_MAX); i++)", "\tfor(int i = 0; i < within(scene->item_count, SCENE_ROWS_MAX); i++)"),
    ("scene_dump_dots_only_with_dots", T, F, DOTS, "\tif(scene->dots != 0)\n"),
    ("scene_dump_dots_only_with_dot", T, F, DOTS, "\tif(scene->dot != -1)\n"),
    ("scene_dump_dots_need_both", T, F, DOTS, "\tif(scene->dots != 0 && scene->dot != -1)\n"),
    ("scene_dump_dots_always", T, F, DOTS, "\tif(true)\n"),
    ("scene_dump_dots_left_out_for_dot_zero", T, F, DOTS, "\tif(scene->dots != 0 || scene->dot > 0 || scene->dot < -1)\n"),
    ("scene_dump_dots_left_out_for_negative_dots", T, F, DOTS, "\tif(scene->dots > 0 || scene->dot != -1)\n"),
    ("scene_dump_dot_counted_from_zero", T, F, DOT_NUMBER, "\t\tput_number(&writer, scene->dot);\n"),
    ("scene_dump_dot_plus_one_in_32_bit", T, F, DOT_NUMBER, "\t\tput_number(&writer, (int)((unsigned)scene->dot + 1u));\n"),
    ("scene_dump_dots_swapped", T, F,
     DOT_NUMBER + "\t\tput_char(&writer, '/');\n\t\tput_number(&writer, scene->dots);", "\t\tput_number(&writer, scene->dots);\n\t\tput_char(&writer, '/');\n" + DOT_NUMBER.rstrip("\n")),
    text("scene_dump_text_dots", "\t\tput(&writer, \"dots: \");", "\t\tput(&writer, \"dot: \");"),
    text("scene_dump_text_dots_slash", "\t\tput_char(&writer, '/');", "\t\tput_char(&writer, ':');"),
    ("scene_dump_row_focus_not_marked", T, F, ROW_MARK, "\t\tput(&writer, \"row: - \");"),
    ("scene_dump_row_marks_swapped", T, F, ROW_MARK, "\t\tput(&writer, flag(&row->focus) ? \"row: - \" : \"row: > \");"),
    ("scene_dump_row_mark_by_enabled", T, F, ROW_MARK, "\t\tput(&writer, flag(&row->enabled) ? \"row: > \" : \"row: - \");"),
    ("scene_dump_row_enabled_swapped", T, F, ROW_ENABLED, "\t\tput(&writer, flag(&row->enabled) ? \" | disabled\\n\" : \" | enabled\\n\");"),
    ("scene_dump_row_always_enabled", T, F, ROW_ENABLED, "\t\tput(&writer, \" | enabled\\n\");"),
    ("scene_dump_row_enabled_by_focus", T, F, ROW_ENABLED, "\t\tput(&writer, flag(&row->focus) ? \" | enabled\\n\" : \" | disabled\\n\");"),
    ("scene_dump_row_without_kind", T, F, "\t\tput(&writer, word(row_kinds, COUNT(row_kinds), (unsigned)row->kind));\n\t\tput(&writer, \" | \");\n", "\t\t(void)row_kinds;\n"),
    ("scene_dump_row_without_text", T, F, "\t\tput_field(&writer, row->text, sizeof(row->text));\n", ""),
    ("scene_dump_row_without_detail", T, F, "\t\tput_field(&writer, row->detail, sizeof(row->detail));\n", ""),
    ("scene_dump_row_text_and_detail_swapped", T, F,
     "\t\tput_field(&writer, row->text, sizeof(row->text));\n\t\tput(&writer, \" | \");\n\t\tput_field(&writer, row->detail, sizeof(row->detail));",
     "\t\tput_field(&writer, row->detail, sizeof(row->detail));\n\t\tput(&writer, \" | \");\n\t\tput_field(&writer, row->text, sizeof(row->text));"),
    ("scene_dump_row_text_in_room_of_detail", T, F, "\t\tput_field(&writer, row->text, sizeof(row->text));", "\t\tput_field(&writer, row->text, sizeof(row->detail));"),
    ("scene_dump_row_detail_in_room_of_text", T, F, "\t\tput_field(&writer, row->detail, sizeof(row->detail));", "\t\tput_field(&writer, row->detail, sizeof(row->text));"),
    ("scene_dump_rows_all_the_first", T, F, "\t\tconst scene_row_t *row = &scene->rows[i];", "\t\tconst scene_row_t *row = &scene->rows[0];"),
    ("scene_dump_rows_not_limited", T, F,
     "\tfor(int i = 0; i < within(scene->row_count, SCENE_ROWS_MAX); i++)", "\tfor(int i = 0; i < scene->row_count; i++)"),
    ("scene_dump_rows_only_for_lists", T, F,
     "\tfor(int i = 0; i < within(scene->row_count, SCENE_ROWS_MAX); i++)", "\tfor(int i = 0; scene->kind == SCENE_LIST && i < within(scene->row_count, SCENE_ROWS_MAX); i++)"),
    ("scene_dump_rows_limited_like_items", T, F,
     "\tfor(int i = 0; i < within(scene->row_count, SCENE_ROWS_MAX); i++)", "\tfor(int i = 0; i < within(scene->row_count, LAYOUT_ITEMS_MAX); i++)"),
    ("scene_dump_window_only_with_first", T, F, WINDOW, "\tif(scene->first != 0)\n"),
    ("scene_dump_window_only_with_total", T, F, WINDOW, "\tif(scene->total != 0)\n"),
    ("scene_dump_window_needs_both", T, F, WINDOW, "\tif(scene->first != 0 && scene->total != 0)\n"),
    ("scene_dump_window_always", T, F, WINDOW, "\tif(true)\n"),
    ("scene_dump_window_only_positive", T, F, WINDOW, "\tif(scene->first > 0 || scene->total > 0)\n"),
    ("scene_dump_window_only_with_rows", T, F, WINDOW, "\tif(scene->row_count != 0)\n"),
    ("scene_dump_window_swapped", T, F,
     "\t\tput_number_line(&writer, \"first:\", scene->first);\n\t\tput_number_line(&writer, \"total:\", scene->total);",
     "\t\tput_number_line(&writer, \"total:\", scene->total);\n\t\tput_number_line(&writer, \"first:\", scene->first);"),
    ("scene_dump_first_is_total", T, F, "\t\tput_number_line(&writer, \"first:\", scene->first);", "\t\tput_number_line(&writer, \"first:\", scene->total);"),
    ("scene_dump_total_is_first", T, F, "\t\tput_number_line(&writer, \"total:\", scene->total);", "\t\tput_number_line(&writer, \"total:\", scene->first);"),
    text("scene_dump_text_first", "\"first:\"", "\"from:\""),
    text("scene_dump_text_total", "\"total:\"", "\"of:\""),
    ("scene_dump_lines_not_limited", T, F, LINES, "\tfor(int i = 0; i < scene->line_count; i++) put_text_line(&writer, \"line:\", scene->lines[i], sizeof(scene->lines[i]));"),
    ("scene_dump_lines_not_for_level", T, F,
     LINES, "\tfor(int i = 0; scene->kind != SCENE_LEVEL && i < within(scene->line_count, SCENE_LINES_MAX); i++) put_text_line(&writer, \"line:\", scene->lines[i], sizeof(scene->lines[i]));"),
    ("scene_dump_lines_limited_like_overlay", T, F,
     LINES, "\tfor(int i = 0; i < within(scene->line_count, COUNT(scene->over_lines)); i++) put_text_line(&writer, \"line:\", scene->lines[i], sizeof(scene->lines[i]));"),
    ("scene_dump_lines_all_the_first", T, F,
     LINES, "\tfor(int i = 0; i < within(scene->line_count, SCENE_LINES_MAX); i++) put_text_line(&writer, \"line:\", scene->lines[0], sizeof(scene->lines[0]));"),
    ("scene_dump_lines_only_with_text", T, F,
     LINES, "\tfor(int i = 0; i < within(scene->line_count, SCENE_LINES_MAX); i++) if(scene->lines[i][0] != '\\0') put_text_line(&writer, \"line:\", scene->lines[i], sizeof(scene->lines[i]));"),
    text("scene_dump_text_line", "\"line:\"", "\"text:\""),
    ("scene_dump_big_also_when_empty", T, F, BIG, "\tput_text_line(&writer, \"big:\", scene->big, sizeof(scene->big));"),
    ("scene_dump_big_not_for_notice_and_choice", T, F, BIG, "\tif(scene->big[0] != '\\0' && scene->kind != SCENE_NOTICE && scene->kind != SCENE_CHOICE) put_text_line(&writer, \"big:\", scene->big, sizeof(scene->big));"),
    ("scene_dump_big_never", T, F, BIG + "\n", ""),
    ("scene_dump_big_in_room_of_title", T, F, BIG, "\tif(scene->big[0] != '\\0') put_text_line(&writer, \"big:\", scene->big, sizeof(scene->title));"),
    text("scene_dump_text_big", "\"big:\"", "\"large:\""),
    ("scene_dump_permille_always", T, F, SCENE_PERMILLE, "\tput_number_line(&writer, \"permille:\", scene->permille);"),
    ("scene_dump_permille_never", T, F, SCENE_PERMILLE + "\n", ""),
    ("scene_dump_permille_only_from_zero", T, F, SCENE_PERMILLE, "\tif(scene->permille >= 0) put_number_line(&writer, \"permille:\", scene->permille);"),
    ("scene_dump_permille_unless_zero", T, F, SCENE_PERMILLE, "\tif(scene->permille != -1 && scene->permille != 0) put_number_line(&writer, \"permille:\", scene->permille);"),
    ("scene_dump_permille_of_overlay", T, F, SCENE_PERMILLE, "\tif(scene->permille != -1) put_number_line(&writer, \"permille:\", scene->over_permille);"),
    text("scene_dump_text_permille", "\"permille:\"", "\"part:\""),
    ("scene_dump_options_only_with_first", T, F, OPTIONS, "\tif(scene->options[0][0] != '\\0')\n"),
    ("scene_dump_options_only_with_second", T, F, OPTIONS, "\tif(scene->options[1][0] != '\\0')\n"),
    ("scene_dump_options_need_both", T, F, OPTIONS, "\tif(scene->options[0][0] != '\\0' && scene->options[1][0] != '\\0')\n"),
    ("scene_dump_options_always", T, F, OPTIONS, "\tif(true)\n"),
    ("scene_dump_options_only_for_choice", T, F, OPTIONS, "\tif(scene->kind == SCENE_CHOICE)\n"),
    ("scene_dump_option_marks_swapped", T, F,
     OPTION_LINE, "\t\tfor(int i = 0; i < 2; i++) put_text_line(&writer, scene->option == i ? \"option: -\" : \"option: >\", scene->options[i], sizeof(scene->options[i]));"),
    ("scene_dump_option_never_marked", T, F,
     OPTION_LINE, "\t\tfor(int i = 0; i < 2; i++) put_text_line(&writer, \"option: -\", scene->options[i], sizeof(scene->options[i]));"),
    ("scene_dump_option_beyond_marks_second", T, F,
     OPTION_LINE, "\t\tfor(int i = 0; i < 2; i++) put_text_line(&writer, (scene->option > 0) == (i > 0) ? \"option: >\" : \"option: -\", scene->options[i], sizeof(scene->options[i]));"),
    ("scene_dump_option_before_marks_first", T, F,
     OPTION_LINE, "\t\tfor(int i = 0; i < 2; i++) put_text_line(&writer, (scene->option >= 1) == (i >= 1) && scene->option <= 1 ? \"option: >\" : \"option: -\", scene->options[i], sizeof(scene->options[i]));"),
    ("scene_dump_options_only_the_first", T, F,
     OPTION_LINE, "\t\tfor(int i = 0; i < 1; i++) put_text_line(&writer, scene->option == i ? \"option: >\" : \"option: -\", scene->options[i], sizeof(scene->options[i]));"),
    ("scene_dump_options_both_the_first", T, F,
     OPTION_LINE, "\t\tfor(int i = 0; i < 2; i++) put_text_line(&writer, scene->option == i ? \"option: >\" : \"option: -\", scene->options[0], sizeof(scene->options[0]));"),
    ("scene_dump_option_in_room_of_title", T, F,
     OPTION_LINE, "\t\tfor(int i = 0; i < 2; i++) put_text_line(&writer, scene->option == i ? \"option: >\" : \"option: -\", scene->options[i], sizeof(scene->title));"),
    ("scene_dump_overlay_none_written", T, F, OVER, "\tif(true)\n"),
    ("scene_dump_overlay_never", T, F, OVER, "\tif(false)\n"),
    ("scene_dump_overlay_only_known", T, F, OVER, "\tif(scene->over > SCENE_OVER_NONE && scene->over <= SCENE_OVER_UPDATE)\n"),
    ("scene_dump_overlay_only_with_lines", T, F, OVER, "\tif(scene->over != SCENE_OVER_NONE && scene->over_line_count > 0)\n"),
    text("scene_dump_text_over", "\t\tput(&writer, \"over: \");", "\t\tput(&writer, \"overlay: \");"),
    ("scene_dump_overlay_lines_not_limited", T, F,
     "\tfor(int i = 0; i < within(scene->over_line_count, COUNT(scene->over_lines)); i++)", "\tfor(int i = 0; i < scene->over_line_count; i++)"),
    ("scene_dump_overlay_lines_limited_like_lines", T, F,
     "\tfor(int i = 0; i < within(scene->over_line_count, COUNT(scene->over_lines)); i++)", "\tfor(int i = 0; i < within(scene->over_line_count, SCENE_LINES_MAX); i++)"),
    ("scene_dump_overlay_lines_only_with_overlay", T, F,
     "\tfor(int i = 0; i < within(scene->over_line_count, COUNT(scene->over_lines)); i++)",
     "\tfor(int i = 0; scene->over != SCENE_OVER_NONE && i < within(scene->over_line_count, COUNT(scene->over_lines)); i++)"),
    ("scene_dump_overlay_lines_all_the_first", T, F,
     "\t\tput_text_line(&writer, \"over_line:\", scene->over_lines[i], sizeof(scene->over_lines[i]));", "\t\tput_text_line(&writer, \"over_line:\", scene->over_lines[0], sizeof(scene->over_lines[0]));"),
    text("scene_dump_text_over_line", "\"over_line:\"", "\"over:\""),
    ("scene_dump_over_permille_always", T, F, OVER_PERMILLE, "\tput_number_line(&writer, \"over_permille:\", scene->over_permille);"),
    ("scene_dump_over_permille_never", T, F, OVER_PERMILLE + "\n", ""),
    ("scene_dump_over_permille_only_from_zero", T, F, OVER_PERMILLE, "\tif(scene->over_permille >= 0) put_number_line(&writer, \"over_permille:\", scene->over_permille);"),
    ("scene_dump_over_permille_only_for_upload", T, F,
     OVER_PERMILLE, "\tif(scene->over == SCENE_OVER_UPLOAD && scene->over_permille != -1) put_number_line(&writer, \"over_permille:\", scene->over_permille);"),
    ("scene_dump_over_permille_of_scene", T, F, OVER_PERMILLE, "\tif(scene->over_permille != -1) put_number_line(&writer, \"over_permille:\", scene->permille);"),
    text("scene_dump_text_over_permille", "\"over_permille:\"", "\"over_part:\""),

    # scene_dump(): the end
    ("scene_dump_no_room_is_written", T, F, NO_ROOM, ""),
    ("scene_dump_no_room_returns_length", T, F, NO_ROOM, "\tif(size == 0) return (int)writer.length;\n"),
    ("scene_dump_fits_without_its_zero", T, F, FITS, "\tif(writer.length > size)\n"),
    ("scene_dump_needs_one_byte_more", T, F, FITS, "\tif(writer.length + 1 >= size)\n"),
    ("scene_dump_never_fits", T, F, FITS, "\tif(true)\n"),
    ("scene_dump_too_long_keeps_its_beginning", T, F, EMPTY, "\t\tout[size - 1] = '\\0';\n\t\treturn -1;"),
    ("scene_dump_too_long_not_emptied", T, F, EMPTY, "\t\treturn -1;"),
    ("scene_dump_too_long_returns_length", T, F, EMPTY, "\t\tout[0] = '\\0';\n\t\treturn (int)writer.length;"),
    ("scene_dump_too_long_returns_zero", T, F, EMPTY, "\t\tout[0] = '\\0';\n\t\treturn 0;"),
    ("scene_dump_without_zero", T, F, END, "\treturn (int)writer.length;"),
    ("scene_dump_returns_length_with_zero", T, F, END, "\tout[writer.length] = '\\0';\n\treturn (int)writer.length + 1;"),
    ("scene_dump_returns_zero", T, F, END, "\tout[writer.length] = '\\0';\n\treturn 0;"),
]
