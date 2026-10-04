"""Mutations of display/components/core/texts.c, see ../redproof.py."""

F = "components/core/texts.c"
H = "components/core/texts.h"
T = "test_texts"

# The lines of the switches as they stand in the source: name of the member, what is returned
VIEW_TEXTS = [
    ("NO_WIFI", '"WiCAN nicht gefunden – schläft, stromlos oder außer Reichweite"'),
    ("CONNECTING", '"Verbinde mit WiCAN …"'),
    ("FOREIGN", '"Fremdes WiCAN – nicht gekoppelt"'),
    ("NO_API", '"WiCAN-Firmware ohne Display-API – nur Live-Werte"'),
    ("AUTOPID_OFF", '"AutoPID nicht aktiv"'),
    ("STARTING", '"WiCAN startet …"'),
    ("SCAN", '"Live-Werte angehalten (Fehlerspeicher-Scan)"'),
    ("ECU_OFFLINE", '"Zündung aus – Motorsteuergerät offline"'),
    ("LIVE", '""'),
]
VIEW_WORDS = [
    ("NO_WIFI", '"no_wifi"'), ("CONNECTING", '"connecting"'), ("FOREIGN", '"foreign"'), ("NO_API", '"no_api"'),
    ("AUTOPID_OFF", '"autopid_off"'), ("STARTING", '"starting"'), ("SCAN", '"scan"'), ("ECU_OFFLINE", '"ecu_offline"'), ("LIVE", '"live"'),
]
BLOCK_TEXTS = [
    ("ALLOWED", '""'),
    ("FOREIGN", '"Fremdes WiCAN – nicht gekoppelt"'),
    ("NO_API", '"WiCAN-Firmware ohne Display-API"'),
    ("AUTOPID_OFF", '"AutoPID nicht aktiv"'),
    ("STARTING", '"WiCAN startet noch"'),
    ("NOT_SUPPORTED", '"Profil ohne Fehlerspeicher"'),
    ("BUSY", '"Scan läuft bereits"'),
    ("ECU_OFFLINE", '"Zündung aus – Motorsteuergerät offline"'),
    ("ENGINE_RUNNING", '"Motor läuft – nur bei Motor aus"'),
    ("RPM_UNKNOWN", '"Drehzahl nicht lesbar"'),
    ("NO_LIST", '"Erst lesen, dann löschen"'),
    ("LIST_OLD", '"Liste veraltet – erneut lesen"'),
    ("NO_CODES", '"Keine Fehler zu löschen"'),
    ("BUTTON_STUCK", '"Knopf klemmt – Löschen gesperrt"'),
]
REASONS = [
    ("busy", "Scan läuft bereits (anderes Gerät)"),
    ("ecu_offline", "Motorsteuergerät offline – Zündung an?"),
    ("engine_running", "Motor läuft – nur bei Motor aus"),
    ("engine_state_unknown", "Drehzahl nicht lesbar – nichts gelöscht"),
    ("not_supported", "Profil ohne Fehlerspeicher"),
    ("out_of_memory", "WiCAN: Speicher knapp – erneut lesen"),
    ("result_serialize_failed", "WiCAN: Speicher knapp – erneut lesen"),
    ("stale_seq", "Liste veraltet – erneut lesen"),
    ("read_required", "Liste veraltet – erneut lesen"),
    ("nothing_to_clear", "Keine Fehler zu löschen"),
    ("expired", "Auftrag verfallen – nichts gesendet"),
    ("not_ready", "WiCAN startet noch"),
    ("forbidden", "WiCAN lehnt die Anfrage ab"),
    ("bad_request", "WiCAN versteht die Anfrage nicht"),
    ("internal", "WiCAN: interner Fehler – erneut lesen"),
    ("no_answer", "Keine Antwort vom WiCAN"),
    ("restarted", "WiCAN neu gestartet – Ergebnis verloren"),
    ("superseded", "Von einem anderen Scan überholt"),
]


def view_line(name, returned):
    return "\t\t" + ("case CONN_VIEW_%s:" % name).ljust(28) + "return %s;\n" % returned


def block_line(name, returned):
    return "\t\t" + ("case DTC_FLOW_%s:" % name).ljust(30) + "return %s;\n" % returned


def reason_line(word, text):
    return '\t{"%s", "%s"},\n' % (word, text)


def other(entries, index):
    """What the entry behind this one returns; a text that differs from the one of the entry itself."""
    for step in range(1, len(entries)):
        candidate = entries[(index + step) % len(entries)][1]
        if candidate != entries[index][1]:
            return candidate
    raise ValueError("all entries return the same")


MUTATIONS = []

# every text and every word: the member forgotten (it then gets what is said for a number that is no
# member), and the text of another member
for index, (name, returned) in enumerate(VIEW_TEXTS):
    MUTATIONS.append(("texts_view_%s_forgotten" % name.lower(), T, F, view_line(name, returned), ""))
    MUTATIONS.append(("texts_view_%s_other_text" % name.lower(), T, F, view_line(name, returned), view_line(name, other(VIEW_TEXTS, index))))
for index, (name, returned) in enumerate(VIEW_WORDS):
    MUTATIONS.append(("texts_view_word_%s_forgotten" % name.lower(), T, F, view_line(name, returned), ""))
    MUTATIONS.append(("texts_view_word_%s_other_word" % name.lower(), T, F, view_line(name, returned), view_line(name, other(VIEW_WORDS, index))))
for index, (name, returned) in enumerate(BLOCK_TEXTS):
    MUTATIONS.append(("texts_block_%s_forgotten" % name.lower(), T, F, block_line(name, returned), ""))
    MUTATIONS.append(("texts_block_%s_other_text" % name.lower(), T, F, block_line(name, returned), block_line(name, other(BLOCK_TEXTS, index))))
for index, (word, text) in enumerate(REASONS):
    MUTATIONS.append(("texts_reason_%s_forgotten" % word, T, F, reason_line(word, text), ""))
    MUTATIONS.append(("texts_reason_%s_other_text" % word, T, F, reason_line(word, text), reason_line(word, other(REASONS, index))))

VIEW_DEFAULT = '\t\tdefault:                    return "WiCAN antwortet nicht";'
WORD_DEFAULT = '\t\tdefault:                    return "no_answer";'
BLOCK_DEFAULT = '\t\tdefault:                      return "WiCAN nicht erreichbar";'
HEAT_DEFAULT = '\t\tdefault:                return "off";'
COMPARE = "\t\tif(strcmp(reason, REASONS[i].word) == 0) return REASONS[i].text;"
LOOP = "\tfor(size_t i = 0; i < sizeof(REASONS) / sizeof(REASONS[0]); i++)"
NOTHING = "\tif(state == NULL || state->dtc.phase == WICAN_DTC_QUEUED || state->dtc.total == 0) return 0;"
FULL = "\tif(state->dtc.step >= state->dtc.total) return 1000;"
DIVIDE = "\treturn (int)((uint64_t)state->dtc.step * 1000u / state->dtc.total);"
START = "\tring_t ring = {RING_RED, 0};"
YELLOW = "\t\tcase CONN_VIEW_CONNECTING:\n\t\tcase CONN_VIEW_STARTING:\n\t\t\tring.kind = RING_YELLOW;\n"
GREY = "\t\tcase CONN_VIEW_NO_API:\n\t\tcase CONN_VIEW_AUTOPID_OFF:\n\t\tcase CONN_VIEW_ECU_OFFLINE:\n\t\t\tring.kind = RING_GREY;\n"
SCAN = "\t\tcase CONN_VIEW_SCAN:\n\t\t\tring.kind = RING_PROGRESS;\n\t\t\tring.permille = progress(state);\n"
CRIT = "\t\t\tif(level >= 2) ring.kind = RING_RED;"
WARN = "\t\t\telse if(level == 1 || old) ring.kind = RING_YELLOW;"
WELL = "\t\t\telse ring.kind = RING_NONE;"

MUTATIONS += [
    # what is no member of an enum
    ("texts_view_no_answer_is_silent", T, F, VIEW_DEFAULT, VIEW_DEFAULT.replace('"WiCAN antwortet nicht"', '""')),
    ("texts_view_unknown_is_no_wifi", T, F,
     VIEW_DEFAULT, VIEW_DEFAULT.replace('"WiCAN antwortet nicht"', '"WiCAN nicht gefunden – schläft, stromlos oder außer Reichweite"')),
    ("texts_view_no_answer_other_words", T, F, VIEW_DEFAULT, VIEW_DEFAULT.replace("antwortet nicht", "antwortet nicht mehr")),
    ("texts_view_word_unknown_is_live", T, F, WORD_DEFAULT, WORD_DEFAULT.replace('"no_answer"', '"live"')),
    ("texts_view_word_no_answer_other_word", T, F, WORD_DEFAULT, WORD_DEFAULT.replace('"no_answer"', '"noanswer"')),
    ("texts_block_unknown_is_allowed", T, F, BLOCK_DEFAULT, BLOCK_DEFAULT.replace('"WiCAN nicht erreichbar"', '""')),
    ("texts_block_no_adapter_other_words", T, F, BLOCK_DEFAULT, BLOCK_DEFAULT.replace("nicht erreichbar", "antwortet nicht")),

    # the bytes of the characters
    ("texts_hyphen_for_the_dash", T, F,
     view_line("FOREIGN", '"Fremdes WiCAN – nicht gekoppelt"'), view_line("FOREIGN", '"Fremdes WiCAN - nicht gekoppelt"')),
    ("texts_three_dots_for_the_ellipsis", T, F, view_line("STARTING", '"WiCAN startet …"'), view_line("STARTING", '"WiCAN startet ..."')),
    ("texts_umlaut_as_two_letters", T, F,
     block_line("NO_CODES", '"Keine Fehler zu löschen"'), block_line("NO_CODES", '"Keine Fehler zu loeschen"')),
    ("texts_sharp_s_as_two_letters", T, F,
     view_line("NO_WIFI", '"WiCAN nicht gefunden – schläft, stromlos oder außer Reichweite"'),
     view_line("NO_WIFI", '"WiCAN nicht gefunden – schläft, stromlos oder ausser Reichweite"')),
    ("texts_middle_dot_in_a_text", T, F,
     block_line("LIST_OLD", '"Liste veraltet – erneut lesen"'), block_line("LIST_OLD", '"Liste veraltet · erneut lesen"')),
    ("texts_text_too_long", T, F,
     view_line("NO_WIFI", '"WiCAN nicht gefunden – schläft, stromlos oder außer Reichweite"'),
     view_line("NO_WIFI", '"WiCAN nicht gefunden – der Adapter schläft, ist stromlos oder außer Reichweite"')),
    ("texts_limit_changed", T, H, "#define TEXT_MAX    79", "#define TEXT_MAX    80"),

    # text_reason
    ("texts_reason_null_is_empty", T, F, "\tif(reason == NULL) return NULL;", '\tif(reason == NULL) return "";'),
    ("texts_reason_null_not_handled", T, F, "\tif(reason == NULL) return NULL;\n", ""),
    ("texts_reason_unknown_is_empty", T, F, "\t}\n\treturn NULL;\n}\n\nconst char *text_heat_word", '\t}\n\treturn "";\n}\n\nconst char *text_heat_word'),
    ("texts_reason_unknown_is_itself", T, F, "\t}\n\treturn NULL;\n}\n\nconst char *text_heat_word", "\t}\n\treturn reason;\n}\n\nconst char *text_heat_word"),
    ("texts_reason_by_beginning_of_word", T, F, COMPARE, COMPARE.replace("strcmp(reason, REASONS[i].word)", "strncmp(reason, REASONS[i].word, strlen(REASONS[i].word))")),
    ("texts_reason_by_beginning_of_reason", T, F, COMPARE, COMPARE.replace("strcmp(reason, REASONS[i].word)", "strncmp(reason, REASONS[i].word, strlen(reason))")),
    ("texts_reason_by_place_in_memory", T, F, COMPARE, COMPARE.replace("strcmp(reason, REASONS[i].word) == 0", "reason == REASONS[i].word")),
    ("texts_reason_first_letter_ignored", T, F,
     COMPARE, COMPARE.replace("strcmp(reason, REASONS[i].word) == 0", "reason[0] != '\\0' && strcmp(reason + 1, REASONS[i].word + 1) == 0")),
    ("texts_reason_returns_word", T, F, COMPARE, COMPARE.replace("return REASONS[i].text", "return REASONS[i].word")),
    ("texts_reason_first_skipped", T, F, LOOP, LOOP.replace("size_t i = 0", "size_t i = 1")),
    ("texts_reason_last_skipped", T, F, LOOP, LOOP.replace("sizeof(REASONS) / sizeof(REASONS[0])", "sizeof(REASONS) / sizeof(REASONS[0]) - 1")),
    ("texts_reason_next_text", T, F, COMPARE, COMPARE.replace("return REASONS[i].text", "return REASONS[i > 0 ? i - 1 : 0].text")),

    # text_heat_word
    ("texts_heat_normal_forgotten", T, F, '\t\tcase GUARD_HEAT_NORMAL: return "normal";\n', ""),
    ("texts_heat_dim_forgotten", T, F, '\t\tcase GUARD_HEAT_DIM:    return "dim";\n', ""),
    ("texts_heat_words_swapped", T, F,
     '\t\tcase GUARD_HEAT_NORMAL: return "normal";\n\t\tcase GUARD_HEAT_DIM:    return "dim";\n',
     '\t\tcase GUARD_HEAT_NORMAL: return "dim";\n\t\tcase GUARD_HEAT_DIM:    return "normal";\n'),
    ("texts_heat_unknown_is_normal", T, F, HEAT_DEFAULT, HEAT_DEFAULT.replace('"off"', '"normal"')),
    ("texts_heat_unknown_is_dim", T, F, HEAT_DEFAULT, HEAT_DEFAULT.replace('"off"', '"dim"')),
    ("texts_heat_off_other_word", T, F, HEAT_DEFAULT, HEAT_DEFAULT.replace('"off"', '"Off"')),

    # the progress of a scan
    ("texts_progress_without_state_crashes", T, F, NOTHING, NOTHING.replace("state == NULL || ", "")),
    ("texts_progress_while_queued", T, F, NOTHING, NOTHING.replace(" || state->dtc.phase == WICAN_DTC_QUEUED", "")),
    ("texts_progress_only_while_queued", T, F, NOTHING, NOTHING.replace("phase == WICAN_DTC_QUEUED", "phase != WICAN_DTC_QUEUED")),
    ("texts_progress_only_while_running", T, F, NOTHING, NOTHING.replace("phase == WICAN_DTC_QUEUED", "phase != WICAN_DTC_RUNNING")),
    ("texts_progress_not_while_idle", T, F,
     NOTHING, NOTHING.replace("phase == WICAN_DTC_QUEUED", "phase <= WICAN_DTC_QUEUED")),
    ("texts_progress_total_0_is_full", T, F, NOTHING, NOTHING.replace(" || state->dtc.total == 0", "")),
    ("texts_progress_total_1_is_nothing", T, F, NOTHING, NOTHING.replace("state->dtc.total == 0", "state->dtc.total <= 1")),
    ("texts_progress_step_0_only_with_total_0", T, F, NOTHING, NOTHING.replace("state->dtc.total == 0", "state->dtc.step == 0")),
    ("texts_progress_above_1000", T, F, FULL + "\n", ""),
    ("texts_progress_ends_at_999", T, F, FULL, FULL.replace("return 1000", "return 999")),
    ("texts_progress_full_one_step_early", T, F, FULL, FULL.replace("state->dtc.step >= state->dtc.total", "state->dtc.step + 1 >= state->dtc.total")),
    ("texts_progress_full_only_behind_the_end", T, F,
     FULL, "\tif(state->dtc.step > state->dtc.total) return 1000;\n\tif(state->dtc.step == state->dtc.total) return 999;"),
    ("texts_progress_in_32_bit", T, F, DIVIDE, "\treturn (int)(state->dtc.step * 1000u / state->dtc.total);"),
    ("texts_progress_percent", T, F, DIVIDE, DIVIDE.replace("1000u", "100u")),
    ("texts_progress_rounded_up", T, F,
     DIVIDE, "\treturn (int)(((uint64_t)state->dtc.step * 1000u + state->dtc.total - 1) / state->dtc.total);"),
    ("texts_progress_rounded_to_nearest", T, F,
     DIVIDE, "\treturn (int)(((uint64_t)state->dtc.step * 1000u + state->dtc.total / 2) / state->dtc.total);"),
    ("texts_progress_what_is_left", T, F, DIVIDE, "\treturn 1000 - (int)((uint64_t)state->dtc.step * 1000u / state->dtc.total);"),

    # the ring
    ("texts_ring_unknown_view_is_nothing", T, F, START, "\tring_t ring = {RING_NONE, 0};"),
    ("texts_ring_unknown_view_is_grey", T, F, START, "\tring_t ring = {RING_GREY, 0};"),
    ("texts_ring_starts_with_permille", T, F, START, "\tring_t ring = {RING_RED, 1000};"),
    ("texts_ring_connecting_red", T, F, YELLOW, YELLOW.replace("\t\tcase CONN_VIEW_CONNECTING:\n", "")),
    ("texts_ring_starting_red", T, F, YELLOW, YELLOW.replace("\t\tcase CONN_VIEW_STARTING:\n", "")),
    ("texts_ring_connecting_grey", T, F, YELLOW, YELLOW.replace("RING_YELLOW", "RING_GREY")),
    ("texts_ring_no_api_red", T, F, GREY, GREY.replace("\t\tcase CONN_VIEW_NO_API:\n", "")),
    ("texts_ring_autopid_off_red", T, F, GREY, GREY.replace("\t\tcase CONN_VIEW_AUTOPID_OFF:\n", "")),
    ("texts_ring_ecu_offline_red", T, F, GREY, GREY.replace("\t\tcase CONN_VIEW_ECU_OFFLINE:\n", "")),
    ("texts_ring_grey_is_yellow", T, F, GREY, GREY.replace("RING_GREY", "RING_YELLOW")),
    ("texts_ring_ecu_offline_yellow", T, F,
     YELLOW + "\t\t\tbreak;\n\n" + GREY,
     YELLOW.replace("\t\tcase CONN_VIEW_STARTING:\n", "\t\tcase CONN_VIEW_STARTING:\n\t\tcase CONN_VIEW_ECU_OFFLINE:\n") + "\t\t\tbreak;\n\n" +
     GREY.replace("\t\tcase CONN_VIEW_ECU_OFFLINE:\n", "")),
    ("texts_ring_no_wifi_grey", T, F, GREY, "\t\tcase CONN_VIEW_NO_WIFI:\n" + GREY),
    ("texts_ring_no_answer_grey", T, F, GREY, "\t\tcase CONN_VIEW_NO_ANSWER:\n" + GREY),
    ("texts_ring_foreign_yellow", T, F, YELLOW, "\t\tcase CONN_VIEW_FOREIGN:\n" + YELLOW),
    ("texts_ring_scan_red", T, F, SCAN, SCAN.replace("RING_PROGRESS", "RING_RED")),
    ("texts_ring_scan_yellow", T, F, SCAN, SCAN.replace("RING_PROGRESS", "RING_YELLOW")),
    ("texts_ring_scan_without_progress", T, F, SCAN, SCAN.replace("ring.permille = progress(state);", "(void)progress(state);")),
    ("texts_ring_scan_always_full", T, F, SCAN, SCAN.replace("ring.permille = progress(state);", "ring.permille = progress(state) > 0 ? 1000 : 0;")),
    ("texts_ring_progress_for_every_view", T, F, START, START + "\n\n\tif(view != CONN_VIEW_SCAN) ring.permille = progress(state);"),
    ("texts_ring_live_red", T, F, CRIT + "\n" + WARN + "\n" + WELL + "\n", "\t\t\t(void)level;\n\t\t\t(void)old;\n"),
    ("texts_ring_level_3_is_nothing", T, F, CRIT, CRIT.replace("level >= 2", "level == 2")),
    ("texts_ring_level_2_is_yellow", T, F, CRIT, CRIT.replace("level >= 2", "level > 2")),
    ("texts_ring_level_1_is_red", T, F, CRIT, CRIT.replace("level >= 2", "level >= 1")),
    ("texts_ring_level_ignored", T, F, CRIT, CRIT.replace("level >= 2", "0")),
    ("texts_ring_crit_is_yellow", T, F, CRIT, CRIT.replace("RING_RED", "RING_YELLOW")),
    ("texts_ring_old_is_red", T, F, CRIT, CRIT.replace("level >= 2", "level >= 2 || old")),
    ("texts_ring_crit_not_while_old", T, F, CRIT, CRIT.replace("level >= 2", "level >= 2 && !old")),
    ("texts_ring_old_ignored", T, F, WARN, WARN.replace("level == 1 || old", "level == 1 || (old && level == 1)")),
    ("texts_ring_level_1_ignored", T, F, WARN, WARN.replace("level == 1 || old", "old")),
    ("texts_ring_warn_only_when_old", T, F, WARN, WARN.replace("level == 1 || old", "level == 1 && old")),
    ("texts_ring_level_0_is_yellow", T, F, WARN, WARN.replace("level == 1 || old", "level >= 0 || old")),
    ("texts_ring_negative_level_is_yellow", T, F, WARN, WARN.replace("level == 1 || old", "level != 0 || old")),
    ("texts_ring_warn_is_grey", T, F, WARN, WARN.replace("RING_YELLOW", "RING_GREY")),
    ("texts_ring_all_well_is_yellow", T, F, WELL, WELL.replace("RING_NONE", "RING_YELLOW")),
    ("texts_ring_live_falls_to_default", T, F, WELL + "\n\t\t\tbreak;\n", WELL + "\n\t\t\tring.kind = RING_RED;\n\t\t\tbreak;\n"),
]
