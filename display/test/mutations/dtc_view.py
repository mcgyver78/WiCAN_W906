"""Mutations of display/components/core/dtc_view.c, see ../redproof.py."""

F = "components/core/dtc_view.c"
H = "components/core/dtc_view.h"
T = "test_dtc_view"

BYTES = "\tsize_t count = first < 0x80 ? 1 : first < 0xC2 ? 0 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : first < 0xF5 ? 4 : 0;"
SECOND = ("\tif(first == 0xE0) low = 0xA0;\n\tif(first == 0xED) high = 0x9F;\n\tif(first == 0xF0) low = 0x90;\n"
          "\tif(first == 0xF4) high = 0x8F;\n")
FOLLOW = "\tfor(size_t i = 1; i < count; i++)"
RANGE = "\t\tif(text[i] < low || text[i] > high) return 0;"
SHOWN = "\t\tbool shown = count > 0 && bytes[0] >= 0x20 && bytes[0] != 0x7F;"
FITS = "\t\tif(used + count + 1 > size) break;"
COPY = "\t\tif(shown) memcpy(&out[used], bytes, count);\n\t\telse out[used] = '?';\n"
DESIGNATION = '\tif(strcspn(name, "0123456789") < word && rest < length)'
TRAILING = "\tend = length;\n\twhile(end > 0 && text[end - 1] == ' ') end--;\n"
CLOSING = "\tif(end > 0 && text[end - 1] == ')')"
BACK = "\t\twhile(end > 0 && text[end - 1] != '(' && text[end - 1] != ')') end--;"
OPENING = "\t\tif(end > 0 && text[end - 1] == '(')"
BEFORE = "\t\t\tend--;\n\t\t\twhile(end > 0 && text[end - 1] == ' ') end--;\n"
SHORTEN = "\t\t\tif(end > 0) length = end;"
ROOM = "\tif(list->count < list->max)"
FULL = "\telse if(list->max > 0)"
LAST = "\t\tdtc_line_t *last = &list->lines[list->max - 1];\n\n\t\tlast->kind = DTC_LINE_NOTE;\n\t\tstrcpy(last->text, \"Liste gekürzt\");\n\t\tlast->detail[0] = '\\0';\n"
FRESH = "\tline->kind = kind;\n\tline->text[0] = '\\0';\n\tline->detail[0] = '\\0';\n"
ACTIVE = "\tif(code->active == 1)"
STORED = "\telse if(code->active == 0)"
STATUS = "\telse if(code->status[0] != '\\0')"
NRC = '\t\t\tsnprintf(nrc, sizeof(nrc), "abgelehnt (NRC %02X)", (unsigned)ecu->nrc);'
CODES = "\t\tuint32_t codes = ecu->omitted > UINT32_MAX - ecu->code_count ? UINT32_MAX : ecu->omitted + ecu->code_count;"
NO_CODES = "\t\tif(codes == 0) continue;"
ECU_NAME = "\t\tline = add(list, DTC_LINE_ECU);\n\t\tdtc_plain_name(ecu->name, line->text, sizeof(line->text));\n"
ECU_ID = "\t\tappend_text(line->detail, sizeof(line->detail), ecu->id);\n"
ECU_DOT = "\t\tappend_text(line->detail, sizeof(line->detail), DOT);\n"
ECU_NUMBER = "\t\tappend_number(line->detail, sizeof(line->detail), codes);\n"
ECU_WORD = '\t\tappend_text(line->detail, sizeof(line->detail), " Fehler");\n'
CODE_LOOP = "\t\tfor(int k = 0; k < ecu->code_count; k++)"
CODE_AT = "\t\t\tconst dtc_code_t *code = &result->codes[ecu->first_code + k];"
CODE_TEXT = "\t\t\tappend_text(line->text, sizeof(line->text), code->code);\n"
CODE_DETAIL = "\t\t\tcode_detail(code, line->detail, sizeof(line->detail));\n"
OMITTED = "\t\tif(ecu->omitted > 0)"
OMITTED_NUMBER = "\t\t\tappend_number(line->text, sizeof(line->text), ecu->omitted);\n"
OMITTED_WORD = '\t\t\tappend_text(line->text, sizeof(line->text), ecu->omitted == 1 ? " Code nicht übertragen" : " Codes nicht übertragen");'
OK = "\t\tif(ecu->status == DTC_ECU_OK) continue;"
PROBLEM = ("\t\tline = add(list, DTC_LINE_PROBLEM);\n\t\tdtc_plain_name(ecu->name, line->text, sizeof(line->text));\n"
           "\t\tproblem_detail(ecu, line->detail, sizeof(line->detail));\n")
CODE_BLOCK = CODE_LOOP + "\n\t\t{\n" + CODE_AT + "\n\n\t\t\tline = add(list, DTC_LINE_CODE);\n" + CODE_TEXT + CODE_DETAIL + "\t\t}\n"
OMITTED_BLOCK = OMITTED + "\n\t\t{\n\t\t\tline = add(list, DTC_LINE_NOTE);\n" + OMITTED_NUMBER + OMITTED_WORD + "\n\t\t}\n"
PROBLEM_LOOP = "\tfor(int i = 0; i < result->ecu_count; i++)\n\t{\n\t\tconst dtc_ecu_t *ecu = &result->ecus[i];\n\n" + OK + "\n\n" + PROBLEM + "\t}\n"
CLEAN = "\tif(clean > 0)"
CLEAN_NUMBER = "\t\tappend_number(line->text, sizeof(line->text), (uint32_t)clean);\n"
CLEAN_WORD = '\t\tappend_text(line->text, sizeof(line->text), clean == 1 ? " Steuergerät ohne Fehler" : " Steuergeräte ohne Fehler");'
CLEAN_BLOCK = "\tif(clean > 0)\n\t{\n\t\tline = add(list, DTC_LINE_CLEAN);\n" + CLEAN_NUMBER + CLEAN_WORD + "\n\t}\n"
CUT_BLOCK = ('\tif(result->cut)\n\t{\n\t\tline = add(list, DTC_LINE_NOTE);\n'
             '\t\tappend_text(line->text, sizeof(line->text), "Liste unvollständig");\n\t}\n')
HEAD_NUMBER = "\tappend_number(line->text, sizeof(line->text), summary.codes);\n"
HEAD_WORD = '\tappend_text(line->text, sizeof(line->text), " Fehler");\n'
HEAD_UNITS = "\tappend_number(line->detail, sizeof(line->detail), (uint32_t)result->ecu_count);\n"
HEAD_UNITS_WORD = '\tappend_text(line->detail, sizeof(line->detail), result->ecu_count == 1 ? " Steuergerät" : " Steuergeräte");'
HEAD_DOT = "\tappend_text(line->detail, sizeof(line->detail), DOT);\n\t// Not (duration_ms"
SECONDS = "\tappend_number(line->detail, sizeof(line->detail), result->duration_ms / 1000u + (result->duration_ms % 1000u >= 500u ? 1u : 0u));"
LIST_BODY = "\tadd_result(&list, result, summary.ecus_clean);"
CLEARED_WORD = '\tappend_text(line->text, sizeof(line->text), "Gelöscht ");\n'
CLEARED_NUMBER = "\tappend_number(line->text, sizeof(line->text), summary.cleared);\n"
CLEARED_OF = '\tappend_text(line->text, sizeof(line->text), " von ");\n'
CLEARED_BEFORE = "\tappend_number(line->text, sizeof(line->text), summary.before);\n"
REMAINING_WORD = '\tappend_text(line->detail, sizeof(line->detail), "verbleibend ");\n'
REMAINING = "\tappend_number(line->detail, sizeof(line->detail), summary.remaining);\n"
UNCONFIRMED_LOOP = "\tfor(int i = 0; i < after->ecu_count; i++)\n\t{\n\t\tconst dtc_ecu_t *ecu = &after->ecus[i];"
CONFIRMED = "\t\tif(ecu->cleared != 0) continue;"
UNCONFIRMED = ("\t\tline = add(&list, DTC_LINE_PROBLEM);\n\t\tdtc_plain_name(ecu->name, line->text, sizeof(line->text));\n"
               '\t\tappend_text(line->detail, sizeof(line->detail), "Löschen nicht bestätigt");\n')
UNCONFIRMED_BLOCK = UNCONFIRMED_LOOP + "\n\n" + CONFIRMED + "\n\n" + UNCONFIRMED + "\t}\n"
CLEARED_BODY = "\tadd_result(&list, after, 0);"
PUT = "\tif(length + count < size) memcpy(&out[length], text, count);\n\treturn length + count;"
NO_ROOM = "\tif(size == 0) return -1;\n"
INDENT = '\t\tif(line->kind == DTC_LINE_CODE) length = put(out, size, length, "  ");'
EXPORT_TEXT = "\t\tlength = put(out, size, length, line->text);\n"
EXPORT_DETAIL = "\t\tif(line->detail[0] != '\\0')"
EXPORT_JOIN = '\t\t\tlength = put(out, size, length, " - ");\n'
EXPORT_DETAIL_TEXT = "\t\t\tlength = put(out, size, length, line->detail);\n"
EXPORT_BREAK = '\t\tlength = put(out, size, length, "\\n");\n'
EXPORT_LOOP = "\tfor(int i = 0; i < count; i++)\n\t{\n\t\tconst dtc_line_t *line = &lines[i];"
TOO_LONG = "\tif(length >= size)\n\t{\n\t\tout[0] = '\\0';\n\t\treturn -1;\n\t}\n"
EXPORT_END = "\tout[length] = '\\0';\n\treturn (int)length;"


def status_case(member, words):
    return '\t\tcase DTC_ECU_%s:\n\t\t\tappend_text(out, size, "%s");\n\t\t\tbreak;\n\n' % (member, words)


NO_RESPONSE = status_case("NO_RESPONSE", "keine Antwort")
PENDING = status_case("PENDING_TIMEOUT", "Antwort ausstehend")
INCOMPLETE = status_case("INCOMPLETE", "unvollständig")
UNKNOWN = '\t\tdefault:\n\t\t\tappend_text(out, size, "unbekannter Status");'

MUTATIONS = [
    # what is a character of UTF-8
    ("dtc_view_utf8_byte_80_is_ascii", T, F, BYTES, BYTES.replace("first < 0x80 ? 1", "first <= 0x80 ? 1")),
    ("dtc_view_utf8_tilde_begins_two", T, F, BYTES, BYTES.replace("first < 0x80 ? 1", "first < 0x7E ? 1")),
    ("dtc_view_utf8_c0_begins_a_character", T, F, BYTES, BYTES.replace("first < 0xC2 ? 0", "first < 0xC0 ? 0")),
    ("dtc_view_utf8_c1_begins_a_character", T, F, BYTES, BYTES.replace("first < 0xC2 ? 0", "first < 0xC1 ? 0")),
    ("dtc_view_utf8_c2_begins_none", T, F, BYTES, BYTES.replace("first < 0xC2 ? 0", "first < 0xC3 ? 0")),
    ("dtc_view_utf8_continuation_begins_a_character", T, F, BYTES, BYTES.replace("first < 0xC2 ? 0", "first < 0x80 ? 0")),
    ("dtc_view_utf8_e0_begins_two", T, F, BYTES, BYTES.replace("first < 0xE0 ? 2", "first < 0xE1 ? 2")),
    ("dtc_view_utf8_df_begins_three", T, F, BYTES, BYTES.replace("first < 0xE0 ? 2", "first < 0xDF ? 2")),
    ("dtc_view_utf8_f0_begins_three", T, F, BYTES, BYTES.replace("first < 0xF0 ? 3", "first < 0xF1 ? 3")),
    ("dtc_view_utf8_ef_begins_four", T, F, BYTES, BYTES.replace("first < 0xF0 ? 3", "first < 0xEF ? 3")),
    ("dtc_view_utf8_f5_begins_a_character", T, F, BYTES, BYTES.replace("first < 0xF5 ? 4", "first < 0xF6 ? 4")),
    ("dtc_view_utf8_f4_begins_none", T, F, BYTES, BYTES.replace("first < 0xF5 ? 4", "first < 0xF4 ? 4")),
    ("dtc_view_utf8_every_high_byte_begins_four", T, F, BYTES, BYTES.replace("first < 0xF5 ? 4 : 0", "4")),
    ("dtc_view_utf8_no_four_bytes", T, F, BYTES, BYTES.replace("first < 0xF5 ? 4 : 0", "0")),
    ("dtc_view_utf8_overlong_three_accepted", T, F, SECOND, SECOND.replace("\tif(first == 0xE0) low = 0xA0;\n", "")),
    ("dtc_view_utf8_e0_second_from_9f", T, F, SECOND, SECOND.replace("low = 0xA0", "low = 0x9F")),
    ("dtc_view_utf8_e0_second_from_a1", T, F, SECOND, SECOND.replace("low = 0xA0", "low = 0xA1")),
    ("dtc_view_utf8_surrogates_accepted", T, F, SECOND, SECOND.replace("\tif(first == 0xED) high = 0x9F;\n", "")),
    ("dtc_view_utf8_ed_second_up_to_a0", T, F, SECOND, SECOND.replace("high = 0x9F", "high = 0xA0")),
    ("dtc_view_utf8_ed_second_up_to_9e", T, F, SECOND, SECOND.replace("high = 0x9F", "high = 0x9E")),
    ("dtc_view_utf8_ec_treated_as_ed", T, F, SECOND, SECOND.replace("first == 0xED", "first >= 0xEC && first <= 0xED")),
    ("dtc_view_utf8_ee_treated_as_ed", T, F, SECOND, SECOND.replace("first == 0xED", "first >= 0xED && first <= 0xEE")),
    ("dtc_view_utf8_overlong_four_accepted", T, F, SECOND, SECOND.replace("\tif(first == 0xF0) low = 0x90;\n", "")),
    ("dtc_view_utf8_f0_second_from_8f", T, F, SECOND, SECOND.replace("low = 0x90", "low = 0x8F")),
    ("dtc_view_utf8_f0_second_from_91", T, F, SECOND, SECOND.replace("low = 0x90", "low = 0x91")),
    ("dtc_view_utf8_beyond_unicode_accepted", T, F, SECOND, SECOND.replace("\tif(first == 0xF4) high = 0x8F;\n", "")),
    ("dtc_view_utf8_f4_second_up_to_90", T, F, SECOND, SECOND.replace("high = 0x8F", "high = 0x90")),
    ("dtc_view_utf8_f4_second_up_to_8e", T, F, SECOND, SECOND.replace("high = 0x8F", "high = 0x8E")),
    ("dtc_view_utf8_f3_treated_as_f4", T, F, SECOND, SECOND.replace("first == 0xF4", "first >= 0xF3")),
    ("dtc_view_utf8_e1_treated_as_e0", T, F, SECOND, SECOND.replace("first == 0xE0", "first <= 0xE1")),
    ("dtc_view_utf8_f1_treated_as_f0", T, F, SECOND, SECOND.replace("first == 0xF0", "first == 0xF0 || first == 0xF1")),
    ("dtc_view_utf8_following_bytes_unchecked", T, F, RANGE, "\t\t(void)low;\n\t\t(void)high;"),
    ("dtc_view_utf8_second_byte_unchecked", T, F, FOLLOW, FOLLOW.replace("size_t i = 1", "size_t i = 2")),
    ("dtc_view_utf8_last_byte_unchecked", T, F, FOLLOW, FOLLOW.replace("i < count", "i + 1 < count")),
    ("dtc_view_utf8_byte_80_continues_nothing", T, F, RANGE, RANGE.replace("text[i] < low", "text[i] <= low")),
    ("dtc_view_utf8_byte_bf_continues_nothing", T, F, RANGE, RANGE.replace("text[i] > high", "text[i] >= high")),
    ("dtc_view_utf8_low_bytes_continue", T, F, RANGE, RANGE.replace("text[i] < low", "low > high")),
    ("dtc_view_utf8_high_bytes_continue", T, F, RANGE, RANGE.replace("text[i] > high", "low > high")),
    ("dtc_view_utf8_narrow_range_for_every_byte", T, F, "\t\tlow = 0x80;\n\t\thigh = 0xBF;\n", ""),
    ("dtc_view_utf8_low_limit_for_every_byte", T, F, "\t\tlow = 0x80;\n\t\thigh = 0xBF;\n", "\t\thigh = 0xBF;\n"),
    ("dtc_view_utf8_high_limit_for_every_byte", T, F, "\t\tlow = 0x80;\n\t\thigh = 0xBF;\n", "\t\tlow = 0x80;\n"),

    # what the adapter sent is not trusted
    ("dtc_view_broken_bytes_passed_on", T, F, SHOWN, "\t\tbool shown = bytes[0] >= 0x20 && bytes[0] != 0x7F;\n\n\t\tif(count == 0) count = 1;"),
    ("dtc_view_control_characters_passed_on", T, F, SHOWN, SHOWN.replace(" && bytes[0] >= 0x20 && bytes[0] != 0x7F", "")),
    ("dtc_view_low_control_characters_passed_on", T, F, SHOWN, SHOWN.replace(" && bytes[0] >= 0x20", "")),
    ("dtc_view_byte_1f_passed_on", T, F, SHOWN, SHOWN.replace("bytes[0] >= 0x20", "bytes[0] >= 0x1F")),
    ("dtc_view_line_break_passed_on", T, F, SHOWN, SHOWN.replace("bytes[0] >= 0x20", "(bytes[0] >= 0x20 || bytes[0] == '\\n')")),
    ("dtc_view_blank_not_shown", T, F, SHOWN, SHOWN.replace("bytes[0] >= 0x20", "bytes[0] > 0x20")),
    ("dtc_view_byte_7f_passed_on", T, F, SHOWN, SHOWN.replace(" && bytes[0] != 0x7F", "")),
    ("dtc_view_tilde_not_shown", T, F, SHOWN, SHOWN.replace("bytes[0] != 0x7F", "bytes[0] < 0x7E")),
    ("dtc_view_high_characters_not_shown", T, F, SHOWN, SHOWN.replace("bytes[0] != 0x7F", "bytes[0] < 0x7F")),
    ("dtc_view_broken_byte_left_out", T, F, COPY, "\t\tif(shown) memcpy(&out[used], bytes, count);\n\t\telse used--;\n"),
    ("dtc_view_broken_byte_is_a_blank", T, F, COPY, COPY.replace("'?'", "' '")),
    ("dtc_view_whole_broken_character_is_one_mark", T, F,
     "\t\tif(!shown) count = 1;", "\t\tif(!shown)\n\t\t{\n\t\t\tcount = 1;\n\t\t\twhile(((unsigned char)text[done + 1] & 0xC0) == 0x80) done++;\n\t\t}"),

    # texts that do not fit
    ("dtc_view_text_one_byte_too_long", T, F, FITS, FITS.replace("used + count + 1 > size", "used + count > size")),
    ("dtc_view_text_one_byte_too_short", T, F, FITS, FITS.replace("used + count + 1 > size", "used + count + 1 >= size")),
    ("dtc_view_character_cut_apart", T, F, FITS, "\t\tif(used + count + 1 > size) count = size - 1 - used;\n\t\tif(count == 0) break;"),
    ("dtc_view_room_of_the_character_not_counted", T, F, FITS, FITS.replace("used + count + 1 > size", "used + 2 > size")),
    ("dtc_view_text_without_end", T, F, "\t\tdone += count;\n\t}\n\tout[used] = '\\0';\n", "\t\tdone += count;\n\t}\n"),
    ("dtc_view_text_not_appended", T, F, "\tsize_t used = strlen(out);\n\tsize_t done = 0;", "\tsize_t used = 0;\n\tsize_t done = 0;"),
    ("dtc_view_number_signed", T, F, 'snprintf(digits, sizeof(digits), "%" PRIu32, number);', 'snprintf(digits, sizeof(digits), "%" PRId32, (int32_t)number);'),
    ("dtc_view_number_hexadecimal", T, F, 'snprintf(digits, sizeof(digits), "%" PRIu32, number);', 'snprintf(digits, sizeof(digits), "%" PRIX32, number);'),
    ("dtc_view_text_size_changed", T, H, "#define DTC_VIEW_TEXT_SIZE  48", "#define DTC_VIEW_TEXT_SIZE  47"),
    ("dtc_view_detail_size_changed", T, H, "#define DTC_VIEW_DETAIL_SIZE 40", "#define DTC_VIEW_DETAIL_SIZE 41"),
    ("dtc_view_lines_max_as_before", T, H, "#define DTC_VIEW_LINES_MAX  226", "#define DTC_VIEW_LINES_MAX  220"),
    ("dtc_view_lines_max_one_less", T, H, "#define DTC_VIEW_LINES_MAX  226", "#define DTC_VIEW_LINES_MAX  225"),

    # dtc_plain_name
    ("dtc_view_name_written_into_no_room", T, F, "\tif(size == 0) return;\n\n\t// The first word", "\t// The first word"),
    ("dtc_view_name_room_of_1_not_written", T, F, "\tif(size == 0) return;\n\n\t// The first word", "\tif(size <= 1) return;\n\n\t// The first word"),
    ("dtc_view_name_designation_kept", T, F, DESIGNATION, DESIGNATION.replace('strcspn(name, "0123456789") < word && rest < length', "0")),
    ("dtc_view_name_first_word_always_left_out", T, F, DESIGNATION, DESIGNATION.replace('strcspn(name, "0123456789") < word && ', "")),
    ("dtc_view_name_designation_left_out_alone", T, F, DESIGNATION, DESIGNATION.replace(" && rest < length", "")),
    ("dtc_view_name_digit_anywhere", T, F, DESIGNATION, DESIGNATION.replace("< word", "< length")),
    ("dtc_view_name_digit_0_unknown", T, F, DESIGNATION, DESIGNATION.replace('"0123456789"', '"123456789"')),
    ("dtc_view_name_digit_9_unknown", T, F, DESIGNATION, DESIGNATION.replace('"0123456789"', '"012345678"')),
    ("dtc_view_name_digit_5_unknown", T, F, DESIGNATION, DESIGNATION.replace('"0123456789"', '"012346789"')),
    ("dtc_view_name_slash_is_a_digit", T, F, DESIGNATION, DESIGNATION.replace('"0123456789"', '"/0123456789"')),
    ("dtc_view_name_colon_is_a_digit", T, F, DESIGNATION, DESIGNATION.replace('"0123456789"', '"0123456789:"')),
    ("dtc_view_name_digit_not_in_last_place", T, F, DESIGNATION, DESIGNATION.replace("< word &&", "+ 1 < word &&")),
    ("dtc_view_name_blanks_behind_designation_kept", T, F, "\t\ttext = name + rest;\n\t\tlength -= rest;", "\t\ttext = name + word;\n\t\tlength -= word;"),
    ("dtc_view_name_one_blank_behind_designation", T, F,
     "\tsize_t rest = word + strspn(name + word, \" \");", "\tsize_t rest = word + (name[word] == ' ' ? 1 : 0);"),
    ("dtc_view_name_word_ends_at_parenthesis", T, F, '\tsize_t word = strcspn(name, " ");', '\tsize_t word = strcspn(name, " (");'),
    ("dtc_view_name_parentheses_kept", T, F, CLOSING, CLOSING.replace("text[end - 1] == ')'", "0")),
    ("dtc_view_name_blanks_behind_parentheses_count", T, F, TRAILING, "\tend = length;\n"),
    ("dtc_view_name_one_blank_behind_parentheses", T, F, TRAILING, "\tend = length;\n\tif(end > 0 && text[end - 1] == ' ') end--;\n"),
    ("dtc_view_name_trailing_blanks_always_left_out", T, F, TRAILING, TRAILING + "\tlength = end;\n"),
    ("dtc_view_name_any_last_character_closes", T, F, CLOSING, CLOSING.replace(" && text[end - 1] == ')'", "")),
    ("dtc_view_name_pair_over_a_closing_parenthesis", T, F, BACK, BACK.replace(" && text[end - 1] != ')'", "")),
    ("dtc_view_name_pair_from_the_first_opening_parenthesis", T, F,
     BACK, BACK + "\n\t\tif(end > 0 && text[end - 1] == '(' && memchr(text, ')', end) == NULL) end = strcspn(text, \"(\") + 1;"),
    ("dtc_view_name_closing_without_opening", T, F, OPENING, OPENING.replace(" && text[end - 1] == '('", "")),
    ("dtc_view_name_blanks_before_parentheses_kept", T, F, BEFORE, "\t\t\tend--;\n"),
    ("dtc_view_name_one_blank_before_parentheses", T, F, BEFORE, "\t\t\tend--;\n\t\t\tif(end > 0 && text[end - 1] == ' ') end--;\n"),
    ("dtc_view_name_opening_parenthesis_kept", T, F, BEFORE, "\t\t\twhile(end > 1 && text[end - 2] == ' ') end--;\n"),
    ("dtc_view_name_parentheses_left_out_alone", T, F, SHORTEN, SHORTEN.replace("if(end > 0) ", "")),
    ("dtc_view_name_one_character_before_parentheses_kept_with_them", T, F, SHORTEN, SHORTEN.replace("end > 0", "end > 1")),
    ("dtc_view_name_parentheses_leave_nothing_behind_designation", T, F, SHORTEN, SHORTEN.replace("end > 0", "end > 0 || text != name")),
    ("dtc_view_name_word_with_parenthesis_kept", T, F,
     DESIGNATION, DESIGNATION.replace('strcspn(name, "0123456789") < word', 'strcspn(name, "0123456789") < word && strcspn(name, "(") >= word')),
    ("dtc_view_name_not_emptied_first", T, F, "\tout[0] = '\\0';\n\tappend(out, size, text, length);", "\tappend(out, size, text, length);"),
    ("dtc_view_name_not_cleaned", T, F,
     "\tout[0] = '\\0';\n\tappend(out, size, text, length);",
     "\tif(length > size - 1) length = size - 1;\n\tmemcpy(out, text, length);\n\tout[length] = '\\0';"),

    # room for the lines
    ("dtc_view_line_behind_the_room", T, F, ROOM, ROOM.replace("list->count < list->max", "list->count <= list->max")),
    ("dtc_view_last_line_of_room_unused", T, F, ROOM, ROOM.replace("list->count < list->max", "list->count + 1 < list->max")),
    ("dtc_view_note_written_into_no_room", T, F, FULL, "\telse"),
    ("dtc_view_no_note_in_a_room_of_1", T, F, FULL, FULL.replace("list->max > 0", "list->max > 1")),
    ("dtc_view_cut_list_without_note", T, F, LAST, "\t\tlist->count = list->max;\n"),
    ("dtc_view_cut_list_one_line_shorter", T, F, LAST, LAST + "\t\tlist->count = list->max - 1;\n"),
    ("dtc_view_cut_note_of_another_kind", T, F, LAST, LAST.replace("\t\tlast->kind = DTC_LINE_NOTE;\n", "")),
    ("dtc_view_cut_note_other_words", T, F, LAST, LAST.replace("Liste gekürzt", "Liste gekuerzt")),
    ("dtc_view_cut_note_as_unfinished_result", T, F, LAST, LAST.replace("Liste gekürzt", "Liste unvollständig")),
    ("dtc_view_cut_note_keeps_detail", T, F, LAST, LAST.replace("\t\tlast->detail[0] = '\\0';\n", "")),
    ("dtc_view_cut_note_in_the_first_line", T, F, LAST, LAST.replace("list->lines[list->max - 1]", "list->lines[0]")),
    ("dtc_view_cut_note_one_line_early", T, F, LAST, LAST.replace("list->lines[list->max - 1]", "list->lines[list->max > 1 ? list->max - 2 : 0]")),
    ("dtc_view_line_without_kind", T, F, FRESH, FRESH.replace("\tline->kind = kind;\n", "\t(void)kind;\n")),
    ("dtc_view_line_text_not_emptied", T, F, FRESH, FRESH.replace("\tline->text[0] = '\\0';\n", "")),
    ("dtc_view_line_detail_not_emptied", T, F, FRESH, FRESH.replace("\tline->detail[0] = '\\0';\n", "")),
    ("dtc_view_count_of_the_room", T, F, "\tadd_result(&list, result, summary.ecus_clean);\n\treturn list.count;", "\tadd_result(&list, result, summary.ecus_clean);\n\treturn max > 0 ? max : 0;"),
    ("dtc_view_count_of_all_lines", T, F, LAST, LAST + "\t\tlist->count++;\n"),

    # the detail of a code
    ("dtc_view_code_any_active_is_active", T, F, ACTIVE, ACTIVE.replace("active == 1", "active != 0")),
    ("dtc_view_code_positive_is_active", T, F, ACTIVE, ACTIVE.replace("active == 1", "active > 0")),
    ("dtc_view_code_never_active", T, F, ACTIVE, ACTIVE.replace("code->active == 1", "0")),
    ("dtc_view_code_active_only_without_status", T, F, ACTIVE, ACTIVE.replace("code->active == 1", "code->active == 1 && code->status[0] == '\\0'")),
    ("dtc_view_code_absent_is_stored", T, F, STORED, STORED.replace("active == 0", "active <= 0")),
    ("dtc_view_code_never_stored", T, F, STORED, STORED.replace("code->active == 0", "0")),
    ("dtc_view_code_words_swapped", T, F,
     '\t\tappend_text(out, size, "aktiv");\n\t}\n' + STORED + '\n\t{\n\t\tappend_text(out, size, "gespeichert");',
     '\t\tappend_text(out, size, "gespeichert");\n\t}\n' + STORED + '\n\t{\n\t\tappend_text(out, size, "aktiv");'),
    ("dtc_view_code_active_other_word", T, F, '\t\tappend_text(out, size, "aktiv");', '\t\tappend_text(out, size, "Aktiv");'),
    ("dtc_view_code_stored_other_word", T, F, '\t\tappend_text(out, size, "gespeichert");', '\t\tappend_text(out, size, "passiv");'),
    ("dtc_view_code_status_word_without_text", T, F, STATUS, "\telse"),
    ("dtc_view_code_status_never", T, F, STATUS, STATUS.replace("code->status[0] != '\\0'", "0")),
    ("dtc_view_code_status_also_when_active", T, F,
     ACTIVE, "\tif(code->status[0] != '\\0')\n\t{\n\t\tappend_text(out, size, \"Status \");\n\t\tappend_text(out, size, code->status);\n\t}\n\telse if(code->active == 1)"),
    ("dtc_view_code_status_without_word", T, F, '\t\tappend_text(out, size, "Status ");\n', ""),
    ("dtc_view_code_status_without_blank", T, F, '\t\tappend_text(out, size, "Status ");', '\t\tappend_text(out, size, "Status");'),
    ("dtc_view_code_status_without_text", T, F, "\t\tappend_text(out, size, code->status);\n", ""),
    ("dtc_view_code_status_shows_code", T, F, "\t\tappend_text(out, size, code->status);", "\t\tappend_text(out, size, code->code);"),

    # the words for a status
    ("dtc_view_no_response_unknown", T, F, NO_RESPONSE, ""),
    ("dtc_view_no_response_other_words", T, F, NO_RESPONSE, NO_RESPONSE.replace("keine Antwort", "Keine Antwort")),
    ("dtc_view_pending_unknown", T, F, PENDING, ""),
    ("dtc_view_pending_other_words", T, F, PENDING, PENDING.replace("Antwort ausstehend", "keine Antwort")),
    ("dtc_view_incomplete_unknown", T, F, INCOMPLETE, ""),
    ("dtc_view_incomplete_other_words", T, F, INCOMPLETE, INCOMPLETE.replace("unvollständig", "unvollstaendig")),
    ("dtc_view_status_words_swapped", T, F,
     NO_RESPONSE + PENDING, status_case("NO_RESPONSE", "Antwort ausstehend") + status_case("PENDING_TIMEOUT", "keine Antwort")),
    ("dtc_view_nrc_small_letters", T, F, NRC, NRC.replace("%02X", "%02x")),
    ("dtc_view_nrc_one_digit", T, F, NRC, NRC.replace("%02X", "%X")),
    ("dtc_view_nrc_padded_with_blank", T, F, NRC, NRC.replace("%02X", "%2X")),
    ("dtc_view_nrc_decimal", T, F, NRC, NRC.replace("%02X", "%02u")),
    ("dtc_view_nrc_low_digit_only", T, F, NRC, NRC.replace("(unsigned)ecu->nrc", "(unsigned)ecu->nrc & 0x0Fu")),
    ("dtc_view_nrc_other_words", T, F, NRC, NRC.replace("abgelehnt (NRC %02X)", "abgelehnt (NRC 0x%02X)")),
    ("dtc_view_nrc_without_number", T, F, NRC + "\n\t\t\tappend_text(out, size, nrc);", NRC + '\n\t\t\tappend_text(out, size, "abgelehnt");'),
    ("dtc_view_nrc_unknown", T, F, "\t\tcase DTC_ECU_NRC:\n", "\t\tcase DTC_ECU_NRC:\n\t\t\tappend_text(out, size, \"unbekannter Status\");\n\t\t\tbreak;\n\n\t\tcase DTC_ECU_OTHER:\n"),
    ("dtc_view_unknown_status_other_words", T, F, UNKNOWN, UNKNOWN.replace("unbekannter Status", "unbekannt")),
    ("dtc_view_unknown_status_without_words", T, F, UNKNOWN, '\t\tdefault:\n\t\t\tappend_text(out, size, "");'),

    # the control units with codes
    ("dtc_view_codes_of_unit_wrap", T, F, CODES, "\t\tuint32_t codes = ecu->omitted + ecu->code_count;"),
    ("dtc_view_codes_of_unit_at_most_omitted", T, F, CODES, CODES.replace("UINT32_MAX : ecu->omitted + ecu->code_count", "UINT32_MAX : ecu->omitted")),
    ("dtc_view_codes_of_unit_largest_one_early", T, F, CODES, CODES.replace("ecu->omitted > UINT32_MAX - ecu->code_count", "ecu->omitted + 1 > UINT32_MAX - ecu->code_count")),
    ("dtc_view_codes_of_unit_listed_only", T, F, CODES, "\t\tuint32_t codes = ecu->code_count;"),
    ("dtc_view_codes_of_unit_omitted_only", T, F, CODES, "\t\tuint32_t codes = ecu->omitted;"),
    ("dtc_view_unit_without_codes_listed", T, F, NO_CODES + "\n", ""),
    ("dtc_view_unit_with_one_code_not_listed", T, F, NO_CODES, NO_CODES.replace("codes == 0", "codes <= 1")),
    ("dtc_view_unit_with_omitted_codes_only_not_listed", T, F, NO_CODES, NO_CODES.replace("codes == 0", "ecu->code_count == 0")),
    ("dtc_view_unit_not_ok_not_listed_with_codes", T, F, NO_CODES, NO_CODES.replace("codes == 0", "codes == 0 || ecu->status != DTC_ECU_OK")),
    ("dtc_view_units_first_skipped", T, F,
     "\tfor(int i = 0; i < result->ecu_count; i++)\n\t{\n\t\tconst dtc_ecu_t *ecu = &result->ecus[i];\n\t\tuint32_t codes",
     "\tfor(int i = 1; i < result->ecu_count; i++)\n\t{\n\t\tconst dtc_ecu_t *ecu = &result->ecus[i];\n\t\tuint32_t codes"),
    ("dtc_view_units_last_skipped", T, F,
     "\tfor(int i = 0; i < result->ecu_count; i++)\n\t{\n\t\tconst dtc_ecu_t *ecu = &result->ecus[i];\n\t\tuint32_t codes",
     "\tfor(int i = 0; i < result->ecu_count - 1; i++)\n\t{\n\t\tconst dtc_ecu_t *ecu = &result->ecus[i];\n\t\tuint32_t codes"),
    ("dtc_view_units_one_more", T, F,
     "\tfor(int i = 0; i < result->ecu_count; i++)\n\t{\n\t\tconst dtc_ecu_t *ecu = &result->ecus[i];\n\t\tuint32_t codes",
     "\tfor(int i = 0; i <= result->ecu_count; i++)\n\t{\n\t\tconst dtc_ecu_t *ecu = &result->ecus[i];\n\t\tuint32_t codes"),
    ("dtc_view_units_backwards", T, F,
     "\tfor(int i = 0; i < result->ecu_count; i++)\n\t{\n\t\tconst dtc_ecu_t *ecu = &result->ecus[i];\n\t\tuint32_t codes",
     "\tfor(int i = 0; i < result->ecu_count; i++)\n\t{\n\t\tconst dtc_ecu_t *ecu = &result->ecus[result->ecu_count - 1 - i];\n\t\tuint32_t codes"),
    ("dtc_view_unit_line_of_another_kind", T, F, ECU_NAME, ECU_NAME.replace("DTC_LINE_ECU", "DTC_LINE_HEAD")),
    ("dtc_view_unit_whole_name", T, F,
     ECU_NAME, ECU_NAME.replace("dtc_plain_name(ecu->name, line->text, sizeof(line->text));", "append_text(line->text, sizeof(line->text), ecu->name);")),
    ("dtc_view_unit_short_name", T, F, ECU_NAME, ECU_NAME.replace("dtc_plain_name(ecu->name", "dtc_short_name(ecu->name")),
    ("dtc_view_unit_name_one_byte_shorter", T, F, ECU_NAME, ECU_NAME.replace("sizeof(line->text));", "sizeof(line->text) - 1);")),
    ("dtc_view_unit_without_id", T, F, ECU_ID, ""),
    ("dtc_view_unit_name_for_id", T, F, ECU_ID, ECU_ID.replace("ecu->id", "ecu->name")),
    ("dtc_view_unit_without_dot", T, F, ECU_DOT, ECU_DOT.replace("DOT", '" "')),
    ("dtc_view_dot_is_a_hyphen", T, F, '#define DOT " \\xC2\\xB7 "', '#define DOT " - "'),
    ("dtc_view_dot_is_a_bullet", T, F, '#define DOT " \\xC2\\xB7 "', '#define DOT " \\xE2\\x80\\xA2 "'),
    ("dtc_view_dot_without_blanks", T, F, '#define DOT " \\xC2\\xB7 "', '#define DOT "\\xC2\\xB7"'),
    ("dtc_view_unit_without_number", T, F, ECU_NUMBER, ""),
    ("dtc_view_unit_number_of_listed_codes", T, F, ECU_NUMBER, ECU_NUMBER.replace("codes);", "ecu->code_count);")),
    ("dtc_view_unit_number_of_omitted_codes", T, F, ECU_NUMBER, ECU_NUMBER.replace("codes);", "ecu->omitted);")),
    ("dtc_view_unit_other_word", T, F, ECU_WORD, ECU_WORD.replace('" Fehler"', '" Codes"')),
    ("dtc_view_unit_word_without_blank", T, F, ECU_WORD, ECU_WORD.replace('" Fehler"', '"Fehler"')),
    ("dtc_view_codes_one_more", T, F, CODE_LOOP, CODE_LOOP.replace("k < ecu->code_count", "k <= ecu->code_count")),
    ("dtc_view_codes_last_skipped", T, F, CODE_LOOP, CODE_LOOP.replace("k < ecu->code_count", "k + 1 < ecu->code_count")),
    ("dtc_view_codes_first_skipped", T, F, CODE_LOOP, CODE_LOOP.replace("int k = 0", "int k = 1")),
    ("dtc_view_codes_first_only", T, F, CODE_LOOP, CODE_LOOP.replace("k < ecu->code_count", "k < ecu->code_count && k < 1")),
    ("dtc_view_codes_from_the_first_of_the_result", T, F, CODE_AT, CODE_AT.replace("ecu->first_code + k", "k")),
    ("dtc_view_codes_first_of_unit_every_time", T, F, CODE_AT, CODE_AT.replace("ecu->first_code + k", "ecu->first_code")),
    ("dtc_view_codes_backwards", T, F, CODE_AT, CODE_AT.replace("ecu->first_code + k", "ecu->first_code + ecu->code_count - 1 - k")),
    ("dtc_view_code_line_of_another_kind", T, F, "\t\t\tline = add(list, DTC_LINE_CODE);", "\t\t\tline = add(list, DTC_LINE_NOTE);"),
    ("dtc_view_code_without_text", T, F, CODE_TEXT, ""),
    ("dtc_view_code_shows_status", T, F, CODE_TEXT, CODE_TEXT.replace("code->code", "code->status")),
    ("dtc_view_code_without_detail", T, F, CODE_DETAIL, CODE_DETAIL.replace("code_detail(", "if(0) code_detail(")),
    ("dtc_view_code_detail_in_text", T, F, CODE_DETAIL, CODE_DETAIL.replace("line->detail, sizeof(line->detail)", "line->text, sizeof(line->text)")),
    ("dtc_view_omitted_not_told", T, F, OMITTED, OMITTED.replace("ecu->omitted > 0", "0")),
    ("dtc_view_one_omitted_not_told", T, F, OMITTED, OMITTED.replace("ecu->omitted > 0", "ecu->omitted > 1")),
    ("dtc_view_omitted_told_without_any", T, F, OMITTED, OMITTED.replace("ecu->omitted > 0", "1")),
    ("dtc_view_omitted_only_without_listed", T, F, OMITTED, OMITTED.replace("ecu->omitted > 0", "ecu->omitted > 0 && ecu->code_count == 0")),
    ("dtc_view_omitted_before_the_codes", T, F, CODE_BLOCK + "\n" + OMITTED_BLOCK, OMITTED_BLOCK + "\n" + CODE_BLOCK),
    ("dtc_view_omitted_line_of_another_kind", T, F, "\t\t\tline = add(list, DTC_LINE_NOTE);\n" + OMITTED_NUMBER, "\t\t\tline = add(list, DTC_LINE_CODE);\n" + OMITTED_NUMBER),
    ("dtc_view_omitted_without_number", T, F, OMITTED_NUMBER, ""),
    ("dtc_view_omitted_number_of_all_codes", T, F, OMITTED_NUMBER, OMITTED_NUMBER.replace("ecu->omitted);", "codes);")),
    ("dtc_view_omitted_always_several", T, F, OMITTED_WORD, OMITTED_WORD.replace("ecu->omitted == 1", "0")),
    ("dtc_view_omitted_always_one", T, F, OMITTED_WORD, OMITTED_WORD.replace("ecu->omitted == 1", "1")),
    ("dtc_view_omitted_two_are_one", T, F, OMITTED_WORD, OMITTED_WORD.replace("ecu->omitted == 1", "ecu->omitted <= 2")),
    ("dtc_view_omitted_words_swapped", T, F, OMITTED_WORD, OMITTED_WORD.replace("ecu->omitted == 1", "ecu->omitted != 1")),
    ("dtc_view_omitted_other_words", T, F, OMITTED_WORD, OMITTED_WORD.replace(" Codes nicht übertragen", " Codes fehlen")),
    ("dtc_view_omitted_one_other_words", T, F, OMITTED_WORD, OMITTED_WORD.replace(" Code nicht übertragen", " Fehler nicht übertragen")),

    # the control units that are not ok
    ("dtc_view_problem_for_every_unit", T, F, OK + "\n", ""),
    ("dtc_view_problem_for_ok_units", T, F, OK, OK.replace("ecu->status == DTC_ECU_OK", "ecu->status != DTC_ECU_OK")),
    ("dtc_view_problem_not_with_codes", T, F, OK, OK.replace("ecu->status == DTC_ECU_OK", "ecu->status == DTC_ECU_OK || ecu->code_count > 0")),
    ("dtc_view_problem_not_with_omitted_codes", T, F, OK, OK.replace("ecu->status == DTC_ECU_OK", "ecu->status == DTC_ECU_OK || ecu->omitted > 0")),
    ("dtc_view_problem_only_for_known_status", T, F, OK, OK.replace("ecu->status == DTC_ECU_OK", "ecu->status == DTC_ECU_OK || ecu->status > DTC_ECU_OTHER")),
    ("dtc_view_problem_not_for_other_status", T, F, OK, OK.replace("ecu->status == DTC_ECU_OK", "ecu->status == DTC_ECU_OK || ecu->status == DTC_ECU_OTHER")),
    ("dtc_view_problem_line_of_another_kind", T, F, PROBLEM, PROBLEM.replace("DTC_LINE_PROBLEM", "DTC_LINE_NOTE")),
    ("dtc_view_problem_whole_name", T, F,
     PROBLEM, PROBLEM.replace("dtc_plain_name(ecu->name, line->text, sizeof(line->text));", "append_text(line->text, sizeof(line->text), ecu->name);")),
    ("dtc_view_problem_short_name", T, F, PROBLEM, PROBLEM.replace("dtc_plain_name(ecu->name", "dtc_short_name(ecu->name")),
    ("dtc_view_problem_without_detail", T, F, PROBLEM, PROBLEM.replace("problem_detail(", "if(0) problem_detail(")),
    ("dtc_view_problem_detail_in_text", T, F, PROBLEM, PROBLEM.replace("problem_detail(ecu, line->detail, sizeof(line->detail))", "problem_detail(ecu, line->text, sizeof(line->text))")),
    ("dtc_view_problems_within_the_units", T, F,
     "\t\tif(ecu->omitted > 0)\n\t\t{\n\t\t\tline = add(list, DTC_LINE_NOTE);\n" + OMITTED_NUMBER + OMITTED_WORD + "\n\t\t}\n\t}\n\n"
     "\tfor(int i = 0; i < result->ecu_count; i++)\n\t{\n\t\tconst dtc_ecu_t *ecu = &result->ecus[i];\n\n" + OK + "\n",
     "\t\tif(ecu->omitted > 0)\n\t\t{\n\t\t\tline = add(list, DTC_LINE_NOTE);\n" + OMITTED_NUMBER + OMITTED_WORD + "\n\t\t}\n\n" + OK + "\n"),

    # the clean control units and the note of a cut result
    ("dtc_view_clean_line_without_clean_units", T, F, CLEAN, CLEAN.replace("clean > 0", "clean >= 0")),
    ("dtc_view_clean_line_not_for_one", T, F, CLEAN, CLEAN.replace("clean > 0", "clean > 1")),
    ("dtc_view_clean_line_never", T, F, CLEAN, CLEAN.replace("clean > 0", "0 > clean")),
    ("dtc_view_clean_line_of_another_kind", T, F, "\t\tline = add(list, DTC_LINE_CLEAN);", "\t\tline = add(list, DTC_LINE_NOTE);"),
    ("dtc_view_clean_without_number", T, F, CLEAN_NUMBER, ""),
    ("dtc_view_clean_number_of_all_units", T, F, CLEAN_NUMBER, CLEAN_NUMBER.replace("(uint32_t)clean", "(uint32_t)result->ecu_count")),
    ("dtc_view_clean_always_several", T, F, CLEAN_WORD, CLEAN_WORD.replace("clean == 1", "0")),
    ("dtc_view_clean_always_one", T, F, CLEAN_WORD, CLEAN_WORD.replace("clean == 1", "1")),
    ("dtc_view_clean_two_are_one", T, F, CLEAN_WORD, CLEAN_WORD.replace("clean == 1", "clean <= 2")),
    ("dtc_view_clean_other_words", T, F, CLEAN_WORD, CLEAN_WORD.replace(" Steuergeräte ohne Fehler", " Steuergeräte fehlerfrei")),
    ("dtc_view_clean_one_other_words", T, F, CLEAN_WORD, CLEAN_WORD.replace(" Steuergerät ohne Fehler", " Steuergerät fehlerfrei")),
    ("dtc_view_clean_behind_the_cut_note", T, F, CLEAN_BLOCK + "\n" + CUT_BLOCK, CUT_BLOCK + "\n" + CLEAN_BLOCK),
    ("dtc_view_clean_before_the_problems", T, F, PROBLEM_LOOP + "\n" + CLEAN_BLOCK, CLEAN_BLOCK + "\n" + PROBLEM_LOOP),
    ("dtc_view_cut_result_not_told", T, F, CUT_BLOCK, CUT_BLOCK.replace("if(result->cut)", "if(0)")),
    ("dtc_view_whole_result_called_cut", T, F, CUT_BLOCK, CUT_BLOCK.replace("if(result->cut)", "if(!result->cut)")),
    ("dtc_view_cut_result_told_only_with_clean_units", T, F, CUT_BLOCK, CUT_BLOCK.replace("if(result->cut)", "if(result->cut && clean > 0)")),
    ("dtc_view_cut_result_note_of_another_kind", T, F, CUT_BLOCK, CUT_BLOCK.replace("DTC_LINE_NOTE", "DTC_LINE_PROBLEM")),
    ("dtc_view_cut_result_other_words", T, F, CUT_BLOCK, CUT_BLOCK.replace("Liste unvollständig", "Liste gekürzt")),

    # the head of a list
    ("dtc_view_head_of_another_kind", T, F, "\tline = add(&list, DTC_LINE_HEAD);\n" + HEAD_NUMBER, "\tline = add(&list, DTC_LINE_NOTE);\n" + HEAD_NUMBER),
    ("dtc_view_head_without_number", T, F, HEAD_NUMBER, ""),
    ("dtc_view_head_number_of_the_adapter", T, F, HEAD_NUMBER, HEAD_NUMBER.replace("summary.codes", "result->dtc_count")),
    ("dtc_view_head_number_of_listed_codes", T, F, HEAD_NUMBER, HEAD_NUMBER.replace("summary.codes", "(uint32_t)result->code_count")),
    ("dtc_view_head_number_of_units_with_codes", T, F, HEAD_NUMBER, HEAD_NUMBER.replace("summary.codes", "(uint32_t)summary.ecus_with_codes")),
    ("dtc_view_head_other_word", T, F, HEAD_WORD, HEAD_WORD.replace('" Fehler"', '" Fehlercodes"')),
    ("dtc_view_head_without_units", T, F, HEAD_UNITS, ""),
    ("dtc_view_head_units_with_codes", T, F, HEAD_UNITS, HEAD_UNITS.replace("(uint32_t)result->ecu_count", "(uint32_t)summary.ecus_with_codes")),
    ("dtc_view_head_units_answering", T, F, HEAD_UNITS, HEAD_UNITS.replace("(uint32_t)result->ecu_count", "(uint32_t)(result->ecu_count - summary.ecus_not_ok)")),
    ("dtc_view_head_units_always_several", T, F, HEAD_UNITS_WORD, HEAD_UNITS_WORD.replace("result->ecu_count == 1", "0")),
    ("dtc_view_head_units_always_one", T, F, HEAD_UNITS_WORD, HEAD_UNITS_WORD.replace("result->ecu_count == 1", "1")),
    ("dtc_view_head_no_unit_is_one", T, F, HEAD_UNITS_WORD, HEAD_UNITS_WORD.replace("result->ecu_count == 1", "result->ecu_count <= 1")),
    ("dtc_view_head_two_units_are_one", T, F, HEAD_UNITS_WORD, HEAD_UNITS_WORD.replace("result->ecu_count == 1", "result->ecu_count >= 1 && result->ecu_count <= 2")),
    ("dtc_view_head_units_other_word", T, F, HEAD_UNITS_WORD, HEAD_UNITS_WORD.replace('" Steuergeräte"', '" Steuergeraete"')),
    ("dtc_view_head_one_unit_other_word", T, F, HEAD_UNITS_WORD, HEAD_UNITS_WORD.replace('" Steuergerät" :', '" Gerät" :')),
    ("dtc_view_head_without_dot", T, F, HEAD_DOT, HEAD_DOT.replace("DOT", '", "')),
    ("dtc_view_head_seconds_rounded_down", T, F, SECONDS, SECONDS.replace(" + (result->duration_ms % 1000u >= 500u ? 1u : 0u)", "")),
    ("dtc_view_head_seconds_rounded_up", T, F, SECONDS, SECONDS.replace("% 1000u >= 500u", "% 1000u >= 1u")),
    ("dtc_view_head_seconds_500_ms_down", T, F, SECONDS, SECONDS.replace("% 1000u >= 500u", "% 1000u > 500u")),
    ("dtc_view_head_seconds_499_ms_up", T, F, SECONDS, SECONDS.replace("% 1000u >= 500u", "% 1000u >= 499u")),
    ("dtc_view_head_seconds_sum_in_32_bit", T, F, SECONDS, "\tappend_number(line->detail, sizeof(line->detail), (result->duration_ms + 500u) / 1000u);"),
    ("dtc_view_head_milliseconds", T, F, SECONDS, "\tappend_number(line->detail, sizeof(line->detail), result->duration_ms);"),
    ("dtc_view_head_without_unit_of_time", T, F, SECONDS + '\n\tappend_text(line->detail, sizeof(line->detail), " s");\n', SECONDS + "\n"),
    ("dtc_view_head_seconds_spelled_out", T, F, SECONDS + '\n\tappend_text(line->detail, sizeof(line->detail), " s");', SECONDS + '\n\tappend_text(line->detail, sizeof(line->detail), " Sekunden");'),
    ("dtc_view_list_without_units", T, F, LIST_BODY + "\n", ""),
    ("dtc_view_list_without_clean_line", T, F, LIST_BODY, LIST_BODY.replace("summary.ecus_clean", "0")),
    ("dtc_view_list_clean_counts_all_ok", T, F, LIST_BODY, LIST_BODY.replace("summary.ecus_clean", "result->ecu_count - summary.ecus_not_ok")),
    ("dtc_view_list_clean_counts_all_without_codes", T, F, LIST_BODY, LIST_BODY.replace("summary.ecus_clean", "result->ecu_count - summary.ecus_with_codes")),

    # the outcome of a clear
    ("dtc_view_cleared_head_of_another_kind", T, F, "\tline = add(&list, DTC_LINE_HEAD);\n" + CLEARED_WORD, "\tline = add(&list, DTC_LINE_NOTE);\n" + CLEARED_WORD),
    ("dtc_view_cleared_other_word", T, F, CLEARED_WORD, CLEARED_WORD.replace("Gelöscht ", "Geloescht ")),
    ("dtc_view_cleared_without_number", T, F, CLEARED_NUMBER, ""),
    ("dtc_view_cleared_shows_remaining", T, F, CLEARED_NUMBER, CLEARED_NUMBER.replace("summary.cleared", "summary.remaining")),
    ("dtc_view_cleared_numbers_swapped", T, F,
     CLEARED_NUMBER + CLEARED_OF + CLEARED_BEFORE, CLEARED_NUMBER.replace("summary.cleared", "summary.before") + CLEARED_OF + CLEARED_BEFORE.replace("summary.before", "summary.cleared")),
    ("dtc_view_cleared_other_word_between", T, F, CLEARED_OF, CLEARED_OF.replace('" von "', '" / "')),
    ("dtc_view_cleared_without_number_before", T, F, CLEARED_BEFORE, ""),
    ("dtc_view_cleared_of_the_remaining", T, F, CLEARED_BEFORE, CLEARED_BEFORE.replace("summary.before", "summary.remaining")),
    ("dtc_view_cleared_remaining_other_word", T, F, REMAINING_WORD, REMAINING_WORD.replace("verbleibend ", "übrig ")),
    ("dtc_view_cleared_remaining_without_number", T, F, REMAINING, ""),
    ("dtc_view_cleared_remaining_shows_cleared", T, F, REMAINING, REMAINING.replace("summary.remaining", "summary.cleared")),
    ("dtc_view_cleared_remaining_in_text", T, F,
     REMAINING_WORD + REMAINING, REMAINING_WORD.replace("line->detail, sizeof(line->detail)", "line->text, sizeof(line->text)") +
     REMAINING.replace("line->detail, sizeof(line->detail)", "line->text, sizeof(line->text)")),
    ("dtc_view_cleared_summary_of_itself", T, F,
     "\tdtc_clear_summarize(before, after, &summary);", "\t(void)before;\n\tdtc_clear_summarize(after, after, &summary);"),
    ("dtc_view_cleared_summary_reversed", T, F, "\tdtc_clear_summarize(before, after, &summary);", "\tdtc_clear_summarize(after, before, &summary);"),
    ("dtc_view_unconfirmed_never_told", T, F, CONFIRMED, CONFIRMED.replace("ecu->cleared != 0", "1")),
    ("dtc_view_unconfirmed_also_when_absent", T, F, CONFIRMED, CONFIRMED.replace("ecu->cleared != 0", "ecu->cleared > 0")),
    ("dtc_view_unconfirmed_only_absent_and_true_exempt", T, F, CONFIRMED, CONFIRMED.replace("ecu->cleared != 0", "ecu->cleared == 1 || ecu->cleared == -1")),
    ("dtc_view_unconfirmed_is_the_confirmed", T, F, CONFIRMED, CONFIRMED.replace("ecu->cleared != 0", "ecu->cleared != 1")),
    ("dtc_view_unconfirmed_only_with_codes", T, F, CONFIRMED, CONFIRMED.replace("ecu->cleared != 0", "ecu->cleared != 0 || ecu->code_count == 0")),
    ("dtc_view_unconfirmed_only_without_codes", T, F, CONFIRMED, CONFIRMED.replace("ecu->cleared != 0", "ecu->cleared != 0 || ecu->code_count > 0")),
    ("dtc_view_unconfirmed_of_the_list_before", T, F, UNCONFIRMED_LOOP, UNCONFIRMED_LOOP.replace("after->", "before->")),
    ("dtc_view_unconfirmed_first_skipped", T, F, UNCONFIRMED_LOOP, UNCONFIRMED_LOOP.replace("int i = 0", "int i = 1")),
    ("dtc_view_unconfirmed_last_skipped", T, F, UNCONFIRMED_LOOP, UNCONFIRMED_LOOP.replace("i < after->ecu_count", "i + 1 < after->ecu_count")),
    ("dtc_view_unconfirmed_one_more", T, F, UNCONFIRMED_LOOP, UNCONFIRMED_LOOP.replace("i < after->ecu_count", "i <= after->ecu_count")),
    ("dtc_view_unconfirmed_line_of_another_kind", T, F, UNCONFIRMED, UNCONFIRMED.replace("DTC_LINE_PROBLEM", "DTC_LINE_NOTE")),
    ("dtc_view_unconfirmed_whole_name", T, F,
     UNCONFIRMED, UNCONFIRMED.replace("dtc_plain_name(ecu->name, line->text, sizeof(line->text));", "append_text(line->text, sizeof(line->text), ecu->name);")),
    ("dtc_view_unconfirmed_other_words", T, F, UNCONFIRMED, UNCONFIRMED.replace("Löschen nicht bestätigt", "nicht gelöscht")),
    ("dtc_view_unconfirmed_without_words", T, F, UNCONFIRMED, UNCONFIRMED.replace('\t\tappend_text(line->detail, sizeof(line->detail), "Löschen nicht bestätigt");\n', "")),
    ("dtc_view_unconfirmed_behind_the_units", T, F, UNCONFIRMED_BLOCK + "\n" + CLEARED_BODY + "\n", CLEARED_BODY + "\n\n" + UNCONFIRMED_BLOCK),
    ("dtc_view_cleared_units_of_the_list_before", T, F, CLEARED_BODY, CLEARED_BODY.replace("after", "before")),
    ("dtc_view_cleared_with_clean_line", T, F, CLEARED_BODY, CLEARED_BODY.replace("after, 0", "after, 1")),
    ("dtc_view_cleared_without_units", T, F, CLEARED_BODY + "\n", ""),

    # the export
    ("dtc_view_export_written_into_no_room", T, F, NO_ROOM, ""),
    ("dtc_view_export_no_room_is_empty_text", T, F, NO_ROOM, "\tif(size == 0) return 0;\n"),
    ("dtc_view_export_room_of_1_refused", T, F, NO_ROOM, "\tif(size <= 1) return -1;\n"),
    ("dtc_view_export_one_byte_wasted", T, F, PUT, PUT.replace("length + count < size", "length + count + 1 < size")),
    ("dtc_view_export_written_behind_room", T, F, PUT, PUT.replace("if(length + count < size) ", "(void)size;\n\t")),
    ("dtc_view_export_short_text_slips_in", T, F, PUT, "\tif(length + count >= size) return length;\n\tmemcpy(&out[length], text, count);\n\treturn length + count;"),
    ("dtc_view_export_code_not_indented", T, F, INDENT + "\n", ""),
    ("dtc_view_export_code_indented_by_one", T, F, INDENT, INDENT.replace('"  "', '" "')),
    ("dtc_view_export_code_indented_by_tab", T, F, INDENT, INDENT.replace('"  "', '"\\t"')),
    ("dtc_view_export_everything_indented", T, F, INDENT, INDENT.replace("if(line->kind == DTC_LINE_CODE) ", "")),
    ("dtc_view_export_all_but_codes_indented", T, F, INDENT, INDENT.replace("line->kind == DTC_LINE_CODE", "line->kind != DTC_LINE_CODE")),
    ("dtc_view_export_notes_indented", T, F, INDENT, INDENT.replace("line->kind == DTC_LINE_CODE", "line->kind == DTC_LINE_CODE || line->kind == DTC_LINE_NOTE")),
    ("dtc_view_export_from_codes_on_indented", T, F, INDENT, INDENT.replace("line->kind == DTC_LINE_CODE", "line->kind >= DTC_LINE_CODE")),
    ("dtc_view_export_up_to_codes_indented", T, F, INDENT, INDENT.replace("line->kind == DTC_LINE_CODE", "line->kind <= DTC_LINE_CODE && line->kind >= DTC_LINE_ECU")),
    ("dtc_view_export_without_text", T, F, EXPORT_TEXT, ""),
    ("dtc_view_export_detail_first", T, F, EXPORT_TEXT, EXPORT_TEXT.replace("line->text", "line->detail")),
    ("dtc_view_export_hyphen_without_detail", T, F, EXPORT_DETAIL, "\t\tif(1)"),
    ("dtc_view_export_without_detail", T, F, EXPORT_DETAIL, "\t\tif(0)"),
    ("dtc_view_export_detail_only_with_text", T, F, EXPORT_DETAIL, EXPORT_DETAIL.replace("line->detail[0] != '\\0'", "line->detail[0] != '\\0' && line->text[0] != '\\0'")),
    ("dtc_view_export_detail_only_of_heads", T, F, EXPORT_DETAIL, EXPORT_DETAIL.replace("line->detail[0] != '\\0'", "line->detail[0] != '\\0' && line->kind == DTC_LINE_HEAD")),
    ("dtc_view_export_without_hyphen", T, F, EXPORT_JOIN, EXPORT_JOIN.replace('" - "', '" "')),
    ("dtc_view_export_dash_for_hyphen", T, F, EXPORT_JOIN, EXPORT_JOIN.replace('" - "', '" \\xE2\\x80\\x93 "')),
    ("dtc_view_export_hyphen_without_blanks", T, F, EXPORT_JOIN, EXPORT_JOIN.replace('" - "', '"-"')),
    ("dtc_view_export_without_detail_text", T, F, EXPORT_DETAIL_TEXT, ""),
    ("dtc_view_export_text_for_detail", T, F, EXPORT_DETAIL_TEXT, EXPORT_DETAIL_TEXT.replace("line->detail", "line->text")),
    ("dtc_view_export_without_line_breaks", T, F, EXPORT_BREAK, ""),
    ("dtc_view_export_carriage_returns", T, F, EXPORT_BREAK, EXPORT_BREAK.replace('"\\n"', '"\\r\\n"')),
    ("dtc_view_export_no_break_behind_last_line", T, F, EXPORT_BREAK, "\t\tif(i + 1 < count) length = put(out, size, length, \"\\n\");\n"),
    ("dtc_view_export_one_line_more", T, F, EXPORT_LOOP, EXPORT_LOOP.replace("i < count", "i <= count")),
    ("dtc_view_export_last_line_missing", T, F, EXPORT_LOOP, EXPORT_LOOP.replace("i < count", "i + 1 < count")),
    ("dtc_view_export_first_line_missing", T, F, EXPORT_LOOP, EXPORT_LOOP.replace("int i = 0", "int i = 1")),
    ("dtc_view_export_first_line_every_time", T, F, EXPORT_LOOP, EXPORT_LOOP.replace("&lines[i]", "&lines[0]")),
    ("dtc_view_export_terminator_behind_room", T, F, TOO_LONG, TOO_LONG.replace("length >= size", "length > size")),
    ("dtc_view_export_too_long_not_emptied", T, F, TOO_LONG, TOO_LONG.replace("\t\tout[0] = '\\0';\n", "")),
    ("dtc_view_export_too_long_returns_0", T, F, TOO_LONG, TOO_LONG.replace("return -1;", "return 0;")),
    ("dtc_view_export_too_long_returns_length", T, F, TOO_LONG, TOO_LONG.replace("return -1;", "return (int)length;")),
    ("dtc_view_export_without_end", T, F, EXPORT_END, "\treturn (int)length;"),
    ("dtc_view_export_length_with_terminator", T, F, EXPORT_END, EXPORT_END.replace("return (int)length;", "return (int)length + 1;")),
    ("dtc_view_export_returns_count", T, F, EXPORT_END, EXPORT_END.replace("return (int)length;", "return count > 0 ? count : 0;")),
]
