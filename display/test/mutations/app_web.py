"""Mutations of display/components/core/app_web.c, see ../redproof.py."""

F = "components/core/app_web.c"
H = "components/core/app_web.h"
T = "test_app_web"

TIME_AT = "\treturn now_ms > app->clock_ms ? now_ms : app->clock_ms;"
ADVANCE = "\tapp->clock_ms = time_at(app, now_ms);\n\treturn app->clock_ms;"
PASSED = "\treturn now_ms > since_ms ? now_ms - since_ms : 0;"
SECONDS = "\tuint64_t whole = ms / 1000;"
CLAMP = "\treturn whole > UINT32_MAX ? UINT32_MAX : (uint32_t)whole;"
TOO_LARGE = "\t\tstatus = 500;\n\t\twritten = web_error_body(\"too_large\", out, APP_WEB_OUT_SIZE);\n"
TEXT_FITS = "\tif(text_length == 0 || text_length >= APP_WEB_OUT_SIZE) return answer(200, -1, out, length);"
NO_BODY = "\t*body_length = 0;\n\treturn \"\";"
DROP = "\tmemset(&app->wifi_asked, 0, sizeof(app->wifi_asked));\n\tapp->has_wifi_asked = false;\n"
MAY_ASK = "\taccess_refusal_t refusal = access_may_ask(&app->access, question, now);"
R_LOCKED = "\tif(refusal == ACCESS_CLOSED) return refuse(403, \"locked\", out, length);\n"
R_BUSY = "\tif(busy) return refuse(409, \"busy\", out, length);\n"
R_ASKING = "\tif(refusal != ACCESS_ALLOWED) return refuse(409, \"asking\", out, length);\n"
R_HOT_WHY = "\t// A question on a dark screen would wait for nobody\n"
R_HOT = "\tif(screen_unseen(app)) return refuse(409, \"hot\", out, length);\n\treturn 0;"
UNDER_WAY = "\treturn phase == DTC_FLOW_READ_SENT || phase == DTC_FLOW_READING || phase == DTC_FLOW_CLEAR_SENT || phase == DTC_FLOW_CLEARING;"
UNSEEN = "\treturn app->heat == GUARD_HEAT_OFF;"
DETAIL = "\tsnprintf(app->ask_detail, sizeof(app->ask_detail), \"%s\", detail);\n"
ASK = "\treturn answer(202, web_asked_json(access_ask(&app->access, question, now), out, APP_WEB_OUT_SIZE), out, length);"
REQ_OPEN = "\trequest->release_open = access_is_open(&app->access, time_at(app, now_ms));\n"
REQ_BUSY = "\trequest->busy = app_busy(app);\n"
GET_TIME = "\tuint64_t now = time_at(app, now_ms);\n\tconst poll_t *poll = &app->poll;"
VALUES = "\t\t\twritten = web_values_json(&poll->values, text_view_word(conn_view(&poll->conn, now)), now, out, APP_WEB_OUT_SIZE);"
DTC_LAST = ("\t\t\twritten = web_dtc_last_json(poll->has_list ? poll->list_text : NULL, seconds(passed(now, poll->flow.list_end_ms)),\n"
            "\t\t\t                            poll->has_old ? poll->old_text : NULL, out, APP_WEB_OUT_SIZE);")
WIFI_LIST = "\t\t\twritten = web_wifi_json(app->profiles, app->profile_count, app->ssid, app->seen, app->seen_count, out, APP_WEB_OUT_SIZE);"
TICKET = "\t\t\twritten = web_ticket_json(ticket, access_ticket(&app->access, ticket, now), access_ask_seconds_left(&app->access, now), out, APP_WEB_OUT_SIZE);"
SEEN_NONE = "\tif(seen == NULL || count < 0) count = 0;\n"
SEEN_MAX = "\tif(count > LINK_SEEN_MAX) count = LINK_SEEN_MAX;\n"
SEEN_COPY = "\tfor(int i = 0; i < count; i++) app->seen[i] = seen[i];\n\tapp->seen_count = count;"
L_ROUTES = ("\tif(route != WEB_ROUTE_LAYOUT_CHECK && route != WEB_ROUTE_LAYOUT_APPLY && route != WEB_ROUTE_LAYOUT_SAVE && route != WEB_ROUTE_LAYOUT_RESET)\n"
            "\t{\n\t\treturn refuse(404, \"not_found\", out, length);\n\t}\n")
L_WRITE = "\tif(route != WEB_ROUTE_LAYOUT_CHECK && !access_write(&app->access, advance(app, now_ms))) return refuse(403, \"locked\", out, length);"
L_ERASE = "\t\tapp->events = (app->events & ~APP_EVENT_STORE_LAYOUT) | APP_EVENT_ERASE_LAYOUT;\n"
L_RESET = ("\t\tapp_choose_layout(app);\n"
           "\t\t// A save that still waits to be taken is undone by this reset: the later of the two alone counts (app.h)\n" + L_ERASE)
L_REFUSED = "\t\t\treturn answer(400, web_layout_report_json(false, &report, NULL, NULL, out, APP_WEB_OUT_SIZE), out, length);"
L_CHECK = ("\t\tif(route == WEB_ROUTE_LAYOUT_CHECK)\n\t\t{\n"
           "\t\t\treturn answer(200, web_layout_report_json(true, &report, &app->checked, catalog, out, APP_WEB_OUT_SIZE), out, length);\n\t\t}\n")
L_TAKE = "\t\tapp->layout = app->checked;\n"
L_TEXT = "\t\tmemcpy(app->layout_text, body, body_length);\n"
L_END = "\t\tapp->layout_text[body_length] = '\\0';\n"
L_LENGTH = "\t\tapp->layout_length = body_length;\n"
L_PREVIEW = "\t\tapp->source = APP_LAYOUT_PREVIEW;\n"
L_STORE = "\t\t\tapp->events = (app->events & ~APP_EVENT_ERASE_LAYOUT) | APP_EVENT_STORE_LAYOUT;\n"
L_SAVE = ("\t\tif(route == WEB_ROUTE_LAYOUT_SAVE)\n\t\t{\n\t\t\tapp->source = APP_LAYOUT_STORED;\n"
          "\t\t\t// ... and a reset that still waits by this save: the platform would erase what it has just stored\n" + L_STORE + "\t\t}\n")
L_BODY = "\t\tbody = body_of(body, &body_length);\n\t\tif(!layout_parse("
L_PAGE = ("\tif(route == WEB_ROUTE_LAYOUT_RESET || !layout_page_shown(&app->layout, app->nav.page, catalog)) "
          "app->nav.page = layout_first_page(&app->layout, catalog);\n")
L_FIRST = "app->nav.page = layout_first_page(&app->layout, catalog);\n"
L_KEPT = "!layout_page_shown(&app->layout, app->nav.page, catalog)"
L_REPORT = "\treturn answer(200, web_layout_report_json(true, &report, &app->layout, catalog, out, APP_WEB_OUT_SIZE), out, length);"
W_ROUTES = "\tif(route != WEB_ROUTE_WIFI_STORE && route != WEB_ROUTE_WIFI_FORGET) return refuse(404, \"not_found\", out, length);\n"
W_TIME = "\tnow = advance(app, now_ms);\n\tbody = body_of(body, &body_length);\n"
W_BUSY = "app->uploading || request_under_way(app)"
W_MAY = "\t\tstatus = ask_refused(app, ACCESS_ASK_WIFI, " + W_BUSY + ", out, length, now);\n\t\tif(status != 0) return status;\n"
W_PARSE = "\t\tif(!web_wifi_parse(body, body_length, &app->wifi_asked, app->work, app->work_count)) return refuse(400, \"body\", out, length);\n"
W_HAS = "\t\tapp->has_wifi_asked = true;\n"
W_ASK = "\t\treturn ask(app, ACCESS_ASK_WIFI, app->wifi_asked.ssid, out, length, now);"
F_BUSY_WHY = "\t// The display leaves its network with every network that is forgotten, whichever is named\n"
F_LOCKED = "\tif(!access_write(&app->access, now)) return refuse(403, \"locked\", out, length);\n"
F_WRITE = F_LOCKED + F_BUSY_WHY
F_BUSY = "\tif(request_under_way(app)) return refuse(409, \"busy\", out, length);\n"
F_FORGET = "\n\tleft = net_forget(app->profiles, app->profile_count, ssid);\n"
F_PARSE = "\tif(!web_forget_parse(body, body_length, ssid, app->work, app->work_count)) return refuse(400, \"body\", out, length);\n"
F_FOUND = "\tif(left == app->profile_count) return refuse(404, \"not_found\", out, length);\n"
F_COUNT = "\tapp->profile_count = left;\n"
F_LINK = "\tlink_profiles(&app->link, app->profiles, app->profile_count, now);\n"
F_NET = "\tapp_net(app, now);\n\tapp->events |= APP_EVENT_STORE_WIFI;\n"
S_WRITE = "\tif(!access_write(&app->access, advance(app, now_ms))) return refuse(403, \"locked\", out, length);\n\n\tbody = body_of(body, &body_length);\n\tif(!settings_from_json"
S_REFUSED = "\t\treturn answer(400, snprintf(out, APP_WEB_OUT_SIZE, \"{\\\"error\\\":\\\"body\\\",\\\"member\\\":\\\"%s\\\"}\", member), out, length);"
S_KNOB = "\tknob_set_reverse(&app->knob, app->settings.reverse);\n"
S_EVENT = "\tapp->events |= APP_EVENT_STORE_SETTINGS;\n"
S_ANSWER = "\treturn answer(200, settings_to_json(&app->settings, out, APP_WEB_OUT_SIZE), out, length);"
A_ROUTES = "\tif(route != WEB_ROUTE_REBOOT && route != WEB_ROUTE_RESET) return refuse(404, \"not_found\", out, length);\n"
A_TIME = "\tnow = advance(app, now_ms);\n\tif(route == WEB_ROUTE_REBOOT)"
B_WRITE = "\t\tif(!access_write(&app->access, now)) return refuse(403, \"locked\", out, length);\n"
B_WHY = "\t\t// A restart would leave a read without its list and a clear without its outcome\n"
B_BUSY = "\t\tif(app_busy(app)) return refuse(409, \"busy\", out, length);\n"
B_DO = "\t\tapp_do(app, NAV_DO_REBOOT, now);\n"
RESET_MAY = "\tstatus = ask_refused(app, ACCESS_ASK_RESET, app_busy(app), out, length, now);\n\tif(status != 0) return status;\n"
RESET_ASK = "\tdrop_network(app);\n\treturn ask(app, ACCESS_ASK_RESET, \"\", out, length, now);"
U_WORDS = "{\"\", \"too_short\", \"no_image\", \"wrong_chip\", \"no_description\", \"wrong_project\", \"too_large\"}"
U_TIME = "\tuint64_t now = advance(app, now_ms);\n\t// Zero in every byte"
U_WRITE = "!access_write(&app->access, now) || access_seconds_left(&app->access, now) < APP_WEB_UPLOAD_LEFT_S"
U_LOCKED = "\tif(" + U_WRITE + ") return refuse(403, \"locked\", out, length);\n"
U_BUSY_WHY = "\t// While the running firmware is not confirmed the other slot is what the boot loader goes back to\n"
U_BUSY = "\tif(app_busy(app) || app->update_pending) return refuse(409, \"busy\", out, length);\n"
U_ASKING_WHY = "\t// A firmware question of an earlier upload must not be confirmed for a slot that is being rewritten\n"
U_ASKING = "\tif(access_asking(&app->access, now) != ACCESS_ASK_NONE) return refuse(409, \"asking\", out, length);\n"
U_HOT_WHY = "\t// The question at the end of the upload could not be seen on a dark screen: no megabytes for that either\n"
U_HOT = "\tif(screen_unseen(app)) return refuse(409, \"hot\", out, length);\n\n\tcheck = "
U_CHECK = "\tcheck = ota_check(first, first != NULL ? first_length : 0, file_size, slot_size, version, sizeof(version));\n"
U_REFUSED = "\tif(check != OTA_CHECK_OK) return refuse(422, words[check], out, length);\n"
U_RUNS = "\tapp->uploading = true;\n\tapp->upload_percent = 0;\n\tapp->upload_ms = now;\n"
U_VERSION = "\tmemcpy(app->upload_version, version, sizeof(version));\n"
U_PREVIOUS = "\tapp->previous_firmware = false;\n"
U_GO = "\tout[0] = '\\0';\n\t*length = 0;\n\treturn 0;"
P_TIME = "\tuint64_t now = advance(app, now_ms);\n\tuint64_t percent"
P_PERCENT = "\tuint64_t percent = file_size > 0 ? (uint64_t)written * 100 / file_size : 0;"
P_RUNS = "\tif(!app->uploading) return;\n"
P_SET = "\tapp->upload_percent = percent > 100 ? 100 : (int)percent;\n\tapp->upload_ms = now;"
E_TIME = "\tuint64_t now = advance(app, now_ms);\n\tbool running"
E_OVER = "\tapp->uploading = false;\n\t// An upload the display has ended by itself"
E_BROKEN = "\tif(!ok || !running) return refuse(500, \"upload\", out, length);\n"
E_MAY = "\tstatus = ask_refused(app, ACCESS_ASK_FIRMWARE, false, out, length, now);\n\tif(status != 0) return status;\n"
E_ASK = "\tdrop_network(app);\n\treturn ask(app, ACCESS_ASK_FIRMWARE, app->upload_version, out, length, now);"


def info(name, old, new):
    """A field of GET /api/info"""
    return ("app_web_info_" + name, T, F, old, new)


MUTATIONS = [
    # the constants
    ("app_web_room_one_byte_larger", T, H, "#define APP_WEB_OUT_SIZE    20480", "#define APP_WEB_OUT_SIZE    20481"),
    ("app_web_room_one_byte_smaller", T, H, "#define APP_WEB_OUT_SIZE    20480", "#define APP_WEB_OUT_SIZE    20479"),
    ("app_web_upload_needs_299_s", T, H, "#define APP_WEB_UPLOAD_LEFT_S 300", "#define APP_WEB_UPLOAD_LEFT_S 299"),
    ("app_web_upload_needs_301_s", T, H, "#define APP_WEB_UPLOAD_LEFT_S 300", "#define APP_WEB_UPLOAD_LEFT_S 301"),

    # the time
    ("app_web_time_of_the_caller", T, F, TIME_AT, "\treturn now_ms > app->clock_ms ? now_ms : now_ms;"),
    ("app_web_time_of_the_app_alone", T, F, TIME_AT, "\treturn now_ms > app->clock_ms ? app->clock_ms : app->clock_ms;"),
    ("app_web_time_not_taken_over", T, F, ADVANCE, "\treturn time_at(app, now_ms);"),
    ("app_web_time_taken_over_as_sent", T, F, ADVANCE, "\tapp->clock_ms = now_ms;\n\treturn app->clock_ms;"),
    ("app_web_age_of_a_time_ahead", T, F, PASSED, "\treturn now_ms - since_ms;"),
    ("app_web_age_always_zero", T, F, PASSED, "\treturn now_ms > since_ms ? 0 : 0;"),
    ("app_web_age_from_time_zero", T, F, PASSED, "\treturn now_ms > since_ms ? now_ms : 0;"),
    ("app_web_seconds_of_1024_ms", T, F, SECONDS, "\tuint64_t whole = ms / 1024;"),
    ("app_web_seconds_rounded_up", T, F, SECONDS, "\tuint64_t whole = (ms + 999) / 1000;"),
    ("app_web_seconds_rounded", T, F, SECONDS, "\tuint64_t whole = (ms + 500) / 1000;"),
    ("app_web_seconds_are_milliseconds", T, F, SECONDS, "\tuint64_t whole = ms;"),
    ("app_web_seconds_wrap_around", T, F, CLAMP, "\treturn (uint32_t)whole;"),
    ("app_web_seconds_stop_at_31_bit", T, F, CLAMP, "\treturn whole > INT32_MAX ? UINT32_MAX : (uint32_t)whole;"),
    ("app_web_seconds_stop_one_early", T, F, CLAMP, "\treturn whole >= UINT32_MAX - 1 ? UINT32_MAX : (uint32_t)whole;"),

    # an answer that does not fit
    ("app_web_no_room_is_empty_200", T, F, TOO_LARGE, "\t\tout[0] = '\\0';\n\t\twritten = 0;\n"),
    ("app_web_no_room_is_200", T, F, TOO_LARGE, "\t\twritten = web_error_body(\"too_large\", out, APP_WEB_OUT_SIZE);\n"),
    ("app_web_no_room_is_413", T, F, "\t\tstatus = 500;\n\t\twritten = web_error_body(\"too_large\"", "\t\tstatus = 413;\n\t\twritten = web_error_body(\"too_large\""),
    ("app_web_no_room_other_word", T, F, "web_error_body(\"too_large\", out, APP_WEB_OUT_SIZE);", "web_error_body(\"too_long\", out, APP_WEB_OUT_SIZE);"),
    ("app_web_length_not_told", T, F, "\t*length = (size_t)written;\n\treturn status;", "\t(void)length;\n\treturn status;"),
    ("app_web_length_with_the_zero", T, F, "\t*length = (size_t)written;\n\treturn status;", "\t*length = (size_t)written + 1;\n\treturn status;"),
    ("app_web_refusal_always_400", T, F, "\treturn answer(status, web_error_body(error, out, APP_WEB_OUT_SIZE), out, length);",
     "\treturn answer(status > 0 ? 400 : 400, web_error_body(error, out, APP_WEB_OUT_SIZE), out, length);"),
    ("app_web_text_empty_is_answered", T, F, TEXT_FITS, "\tif(text_length >= APP_WEB_OUT_SIZE) return answer(200, -1, out, length);"),
    ("app_web_text_of_room_size_is_answered", T, F, TEXT_FITS, "\tif(text_length == 0 || text_length > APP_WEB_OUT_SIZE) return answer(200, -1, out, length);"),
    ("app_web_text_of_any_size_is_answered", T, F, TEXT_FITS, "\tif(text_length == 0) return answer(200, -1, out, length);"),
    ("app_web_text_one_below_room_refused", T, F, TEXT_FITS, "\tif(text_length == 0 || text_length >= APP_WEB_OUT_SIZE - 1) return answer(200, -1, out, length);"),
    ("app_web_text_over_layout_size_refused", T, F, TEXT_FITS, "\tif(text_length == 0 || text_length >= LAYOUT_TEXT_MAX) return answer(200, -1, out, length);"),
    ("app_web_text_without_end", T, F, "\tout[text_length] = '\\0';\n\t*length = text_length;", "\t*length = text_length;"),
    ("app_web_text_length_not_told", T, F, "\tout[text_length] = '\\0';\n\t*length = text_length;", "\tout[text_length] = '\\0';"),
    ("app_web_text_copied_without_last_byte", T, F, "\tmemcpy(out, text, text_length);\n\tout[text_length]", "\tmemcpy(out, text, text_length - 1);\n\tout[text_length]"),

    # a request without a body
    ("app_web_no_body_is_empty_object", T, F, NO_BODY, "\t*body_length = 2;\n\treturn \"{}\";"),
    ("app_web_no_body_read_as_one", T, F, "\tif(body != NULL) return body;\n\n\t*body_length = 0;", "\treturn body;\n\n\t*body_length = 0;"),
    ("app_web_layout_without_body_read", T, F, "\t\tbody = body_of(body, &body_length);\n\t\tif(!layout_parse", "\t\tif(!layout_parse"),
    ("app_web_wifi_without_body_read", T, F, W_TIME, "\tnow = advance(app, now_ms);\n"),
    ("app_web_settings_without_body_read", T, F, "\n\tbody = body_of(body, &body_length);\n\tif(!settings_from_json", "\n\tif(!settings_from_json"),
    ("app_web_upload_without_bytes_read", T, F, U_CHECK, "\tcheck = ota_check(first, first_length, file_size, slot_size, version, sizeof(version));\n"),

    ("app_web_layout_body_read_to_its_zero", T, F, "\t\tif(!layout_parse(body, body_length, &app->checked, &report, app->work, app->work_count))",
     "\t\tbody_length = strlen(body);\n\t\tif(!layout_parse(body, body_length, &app->checked, &report, app->work, app->work_count))"),
    ("app_web_layout_text_copied_to_its_zero", T, F, "\t\tmemcpy(app->layout_text, body, body_length);\n\t\tapp->layout_text[body_length] = '\\0';\n",
     "\t\tmemcpy(app->layout_text, body, body_length + 4);\n"),
    ("app_web_wifi_body_read_to_its_zero", T, F, "\tnow = advance(app, now_ms);\n\tbody = body_of(body, &body_length);\n",
     "\tnow = advance(app, now_ms);\n\tbody = body_of(body, &body_length);\n\tbody_length = strlen(body);\n"),
    ("app_web_settings_body_read_to_its_zero", T, F, "\n\tbody = body_of(body, &body_length);\n\tif(!settings_from_json",
     "\n\tbody = body_of(body, &body_length);\n\tbody_length = strlen(body);\n\tif(!settings_from_json"),

    # what an earlier question left behind
    ("app_web_dropped_network_stays_in_memory", T, F, DROP, "\tapp->has_wifi_asked = false;\n"),
    ("app_web_dropped_network_stays_marked", T, F, DROP, "\tmemset(&app->wifi_asked, 0, sizeof(app->wifi_asked));\n"),
    ("app_web_dropped_network_keeps_password", T, F, DROP, "\tmemset(app->wifi_asked.ssid, 0, sizeof(app->wifi_asked.ssid));\n\tapp->has_wifi_asked = false;\n"),
    ("app_web_dropped_network_keeps_host", T, F, "\tmemset(&app->wifi_asked, 0, sizeof(app->wifi_asked));", "\tmemset(&app->wifi_asked, 0, sizeof(app->wifi_asked) - sizeof(app->wifi_asked.host));"),
    ("app_web_reset_keeps_network", T, F, RESET_ASK, "\treturn ask(app, ACCESS_ASK_RESET, \"\", out, length, now);"),
    ("app_web_firmware_keeps_network", T, F, E_ASK, "\treturn ask(app, ACCESS_ASK_FIRMWARE, app->upload_version, out, length, now);"),

    # the refusals of a question, in their order
    ("app_web_ask_closed_not_refused", T, F, R_LOCKED, ""),
    ("app_web_ask_closed_is_asking", T, F, R_LOCKED, "\tif(refusal == ACCESS_CLOSED) return refuse(409, \"asking\", out, length);\n"),
    ("app_web_ask_closed_is_401", T, F, R_LOCKED, "\tif(refusal == ACCESS_CLOSED) return refuse(401, \"locked\", out, length);\n"),
    ("app_web_ask_busy_before_locked", T, F, R_LOCKED + R_BUSY, R_BUSY + R_LOCKED),
    ("app_web_ask_busy_not_refused", T, F, R_BUSY, "\tif(busy && false) return refuse(409, \"busy\", out, length);\n"),
    ("app_web_ask_busy_is_asking", T, F, R_BUSY, "\tif(busy) return refuse(409, \"asking\", out, length);\n"),
    ("app_web_ask_busy_is_503", T, F, R_BUSY, "\tif(busy) return refuse(503, \"busy\", out, length);\n"),
    ("app_web_ask_asking_before_busy", T, F, R_BUSY + R_ASKING, R_ASKING + R_BUSY),
    ("app_web_ask_asking_not_refused", T, F, R_ASKING, ""),
    ("app_web_ask_asking_is_busy", T, F, R_ASKING, "\tif(refusal != ACCESS_ALLOWED) return refuse(409, \"busy\", out, length);\n"),
    ("app_web_ask_asking_is_locked", T, F, R_ASKING, "\tif(refusal != ACCESS_ALLOWED) return refuse(403, \"locked\", out, length);\n"),
    # a question nobody can see
    ("app_web_ask_on_a_dark_screen", T, F, R_HOT, "\treturn 0;"),
    ("app_web_ask_hot_is_busy", T, F, R_HOT, R_HOT.replace("\"hot\"", "\"busy\"")),
    ("app_web_ask_hot_is_asking", T, F, R_HOT, R_HOT.replace("\"hot\"", "\"asking\"")),
    ("app_web_ask_hot_is_503", T, F, R_HOT, R_HOT.replace("refuse(409,", "refuse(503,")),
    ("app_web_ask_hot_before_asking", T, F, R_ASKING + R_HOT_WHY + R_HOT, R_HOT.replace("\treturn 0;", "") + R_ASKING + "\treturn 0;"),
    ("app_web_ask_hot_before_busy", T, F, R_BUSY + R_ASKING + R_HOT_WHY + R_HOT, R_HOT.replace("\treturn 0;", "") + R_BUSY + R_ASKING + "\treturn 0;"),
    ("app_web_ask_hot_before_locked", T, F, R_LOCKED + R_BUSY + R_ASKING + R_HOT_WHY + R_HOT, R_HOT.replace("\treturn 0;", "") + R_LOCKED + R_BUSY + R_ASKING + "\treturn 0;"),
    ("app_web_ask_network_on_a_dark_screen", T, F, R_HOT, R_HOT.replace("screen_unseen(app)", "screen_unseen(app) && question != ACCESS_ASK_WIFI")),
    ("app_web_ask_firmware_on_a_dark_screen", T, F, R_HOT, R_HOT.replace("screen_unseen(app)", "screen_unseen(app) && question != ACCESS_ASK_FIRMWARE")),
    ("app_web_ask_reset_on_a_dark_screen", T, F, R_HOT, R_HOT.replace("screen_unseen(app)", "screen_unseen(app) && question != ACCESS_ASK_RESET")),
    ("app_web_dark_also_when_dimmed", T, F, UNSEEN, "\treturn app->heat != GUARD_HEAT_NORMAL;"),
    ("app_web_dark_never", T, F, UNSEEN, "\treturn app->heat == GUARD_HEAT_OFF && app->temp_c < -1000;"),
    ("app_web_dark_by_the_last_temperature", T, F, UNSEEN, "\treturn app->temp_c >= GUARD_TEMP_OFF_C;"),
    ("app_web_dark_only_behind_a_reading_that_succeeded", T, F, UNSEEN, "\treturn app->heat == GUARD_HEAT_OFF && app->has_temp;"),
    # a fault memory request under way
    ("app_web_under_way_not_while_a_read_waits_for_its_answer", T, F, UNDER_WAY, UNDER_WAY.replace("phase == DTC_FLOW_READ_SENT || ", "")),
    ("app_web_under_way_not_while_reading", T, F, UNDER_WAY, UNDER_WAY.replace("phase == DTC_FLOW_READING || ", "")),
    ("app_web_under_way_not_while_a_clear_waits_for_its_answer", T, F, UNDER_WAY, UNDER_WAY.replace("phase == DTC_FLOW_CLEAR_SENT || ", "")),
    ("app_web_under_way_not_while_clearing", T, F, UNDER_WAY, UNDER_WAY.replace(" || phase == DTC_FLOW_CLEARING", "")),
    ("app_web_under_way_while_a_list_is_shown", T, F, UNDER_WAY, UNDER_WAY.replace(";", " || phase == DTC_FLOW_LIST;")),
    ("app_web_under_way_while_an_outcome_is_shown", T, F, UNDER_WAY, UNDER_WAY.replace(";", " || phase == DTC_FLOW_CLEARED;")),
    ("app_web_under_way_after_a_failure", T, F, UNDER_WAY, UNDER_WAY.replace(";", " || phase == DTC_FLOW_FAILED;")),
    ("app_web_under_way_with_an_unknown_outcome", T, F, UNDER_WAY, UNDER_WAY.replace(";", " || phase == DTC_FLOW_UNKNOWN;")),
    ("app_web_under_way_whenever_something_was_read", T, F, UNDER_WAY, "\treturn phase != DTC_FLOW_IDLE;"),
    ("app_web_ask_firmware_over_network_question", T, F, R_ASKING,
     "\tif(refusal != ACCESS_ALLOWED && !(question == ACCESS_ASK_FIRMWARE && access_asking(&app->access, now) == ACCESS_ASK_WIFI)) return refuse(409, \"asking\", out, length);\n"),
    ("app_web_ask_judged_as_no_question", T, F, MAY_ASK, "\taccess_refusal_t refusal = access_may_ask(&app->access, question == ACCESS_ASK_NONE ? question : ACCESS_ASK_NONE, now);"),
    ("app_web_ask_judged_at_time_of_release", T, F, MAY_ASK, "\taccess_refusal_t refusal = access_may_ask(&app->access, question, now < app->access.clock_ms ? now : app->access.clock_ms);"),
    ("app_web_ask_judged_at_time_zero", T, F, MAY_ASK, "\taccess_refusal_t refusal = access_may_ask(&app->access, question, now - now);"),
    ("app_web_ask_judged_one_ms_late", T, F, MAY_ASK, "\taccess_refusal_t refusal = access_may_ask(&app->access, question, now + 1);"),
    ("app_web_ask_without_detail", T, F, DETAIL, "\t(void)detail;\n"),
    ("app_web_ask_detail_always_empty", T, F, DETAIL, "\tsnprintf(app->ask_detail, sizeof(app->ask_detail), \"%s\", detail + strlen(detail));\n"),
    ("app_web_ask_detail_cut_behind_31_bytes", T, F, DETAIL, "\tsnprintf(app->ask_detail, sizeof(app->ask_detail) - 8, \"%s\", detail);\n"),
    ("app_web_ask_detail_cut", T, F, DETAIL, "\tsnprintf(app->ask_detail, sizeof(app->ask_detail) / 2, \"%s\", detail);\n"),
    ("app_web_ask_detail_appended", T, F, DETAIL, "\tsnprintf(app->ask_detail + strlen(app->ask_detail), sizeof(app->ask_detail) - strlen(app->ask_detail), \"%s\", detail);\n"),
    ("app_web_ask_answered_200", T, F, "\treturn answer(202, web_asked_json(", "\treturn answer(200, web_asked_json("),
    ("app_web_ask_at_time_zero", T, F, ASK, "\treturn answer(202, web_asked_json(access_ask(&app->access, question, now - now), out, APP_WEB_OUT_SIZE), out, length);"),
    ("app_web_ask_one_ms_late", T, F, ASK, "\treturn answer(202, web_asked_json(access_ask(&app->access, question, now + 1), out, APP_WEB_OUT_SIZE), out, length);"),
    ("app_web_ask_always_reset", T, F, ASK, "\treturn answer(202, web_asked_json(access_ask(&app->access, question != ACCESS_ASK_NONE ? ACCESS_ASK_RESET : question, now), out, APP_WEB_OUT_SIZE), out, length);"),
    ("app_web_ask_always_firmware", T, F, ASK, "\treturn answer(202, web_asked_json(access_ask(&app->access, question != ACCESS_ASK_NONE ? ACCESS_ASK_FIRMWARE : question, now), out, APP_WEB_OUT_SIZE), out, length);"),
    ("app_web_ask_ticket_of_answer_one_more", T, F, ASK, "\treturn answer(202, web_asked_json(access_ask(&app->access, question, now) + 1, out, APP_WEB_OUT_SIZE), out, length);"),
    ("app_web_ask_not_asked", T, F, ASK, "\t(void)question;\n\t(void)now;\n\treturn answer(202, web_asked_json(app->access.ticket + 1, out, APP_WEB_OUT_SIZE), out, length);"),

    ("app_web_ask_counts_as_input", T, F, DETAIL, DETAIL + "\tapp->last_input_ms = now;\n"),
    ("app_web_ask_leaves_the_screen", T, F, DETAIL, DETAIL + "\tapp->nav.screen = NAV_PAGES;\n"),
    ("app_web_ask_closes_clear_dialog", T, F, DETAIL, DETAIL + "\tif(app->nav.screen == NAV_DTC_CONFIRM) app->nav.screen = NAV_DTC_LIST;\n"),

    # app_web_request()
    ("app_web_request_release_not_told", T, F, REQ_OPEN, "\t(void)now_ms;\n"),
    ("app_web_request_release_always_open", T, F, REQ_OPEN, "\t(void)now_ms;\n\trequest->release_open = true;\n"),
    ("app_web_request_release_always_closed", T, F, REQ_OPEN, "\t(void)now_ms;\n\trequest->release_open = false;\n"),
    ("app_web_request_release_at_time_of_caller", T, F, REQ_OPEN, "\trequest->release_open = access_is_open(&app->access, now_ms);\n"),
    ("app_web_request_release_at_time_of_app", T, F, REQ_OPEN, "\t(void)now_ms;\n\trequest->release_open = access_is_open(&app->access, app->clock_ms);\n"),
    ("app_web_request_release_as_last_noted", T, F, REQ_OPEN, "\t(void)now_ms;\n\trequest->release_open = app->access.open;\n"),
    ("app_web_request_busy_not_told", T, F, REQ_BUSY, ""),
    ("app_web_request_never_busy", T, F, REQ_BUSY, "\trequest->busy = false;\n"),
    ("app_web_request_busy_by_upload_alone", T, F, REQ_BUSY, "\trequest->busy = app->uploading;\n"),
    ("app_web_request_busy_by_dialog_alone", T, F, REQ_BUSY, "\trequest->busy = app->nav.screen == NAV_DTC_CONFIRM;\n"),
    ("app_web_request_busy_while_list_shown", T, F, REQ_BUSY, "\trequest->busy = app_busy(app) || app->poll.has_list;\n"),
    ("app_web_request_busy_while_open", T, F, REQ_BUSY, "\trequest->busy = app_busy(app) || request->release_open;\n"),
    ("app_web_request_clears_length", T, F, REQ_BUSY, "\trequest->busy = app_busy(app);\n\trequest->has_length = false;\n"),
    ("app_web_request_writes_slot_size", T, F, REQ_BUSY, "\trequest->busy = app_busy(app);\n\trequest->slot_size = 0;\n"),

    # GET /api/info
    info("version_is_git", "\tinfo.version = app->version;", "\tinfo.version = app->git;"),
    info("git_is_version", "\tinfo.git = app->git;", "\tinfo.git = app->version;"),
    info("slot_is_reset", "\tinfo.slot = app->slot;", "\tinfo.slot = app->reset;"),
    info("without_reset", "\tinfo.reset = app->reset;\n", ""),
    info("up_not_told", "\tinfo.up_s = seconds(now);\n", ""),
    info("up_in_milliseconds", "\tinfo.up_s = seconds(now);", "\tinfo.up_s = (uint32_t)now;"),
    info("up_by_time_of_app", "\tinfo.up_s = seconds(now);", "\tinfo.up_s = seconds(app->clock_ms);"),
    info("safe_mode_is_rolled_back", "\tinfo.safe_mode = app->safe_mode;", "\tinfo.safe_mode = app->rolled_back;"),
    info("rolled_back_is_safe_mode", "\tinfo.rolled_back = app->rolled_back;", "\tinfo.rolled_back = app->safe_mode;"),
    info("update_pending_is_rolled_back", "\tinfo.update_pending = app->update_pending;", "\tinfo.update_pending = app->rolled_back;"),
    info("update_not_pending_when_given_up", "\tinfo.update_pending = app->update_pending;", "\tinfo.update_pending = app->update_pending && !app->update_given_up;"),
    info("ssid_only_with_adapter", "\tinfo.ssid = app->ssid;", "\tinfo.ssid = link_up(&app->link) ? app->ssid : \"\";"),
    info("rssi_only_in_a_network", "\tinfo.rssi = app->rssi;", "\tinfo.rssi = app->ssid[0] != '\\0' ? app->rssi : 0;"),
    info("ip_only_in_a_network", "\tinfo.ip = app->ip;", "\tinfo.ip = app->ssid[0] != '\\0' ? app->ip : \"\";"),
    info("without_update_pending", "\tinfo.update_pending = app->update_pending;\n", ""),
    info("heap_is_heap_min", "\tinfo.heap = app->heap;", "\tinfo.heap = app->heap_min;"),
    info("heap_min_is_heap", "\tinfo.heap_min = app->heap_min;", "\tinfo.heap_min = app->heap;"),
    info("psram_is_heap", "\tinfo.psram = app->psram;", "\tinfo.psram = app->heap;"),
    info("psram_min_is_psram", "\tinfo.psram_min = app->psram_min;", "\tinfo.psram_min = app->psram;"),
    info("without_temperature", "\tinfo.temp_c = app->temp_c;\n", ""),
    info("temperature_only_while_read", "\tinfo.temp_c = app->temp_c;", "\tinfo.temp_c = app->has_temp ? app->temp_c : 0;"),
    info("heat_always_normal", "\tinfo.heat = text_heat_word(app->heat);", "\tinfo.heat = text_heat_word(GUARD_HEAT_NORMAL);"),
    info("heat_without_way_back", "\tinfo.heat = text_heat_word(app->heat);", "\tinfo.heat = text_heat_word(guard_heat(GUARD_HEAT_NORMAL, app->temp_c, true));"),
    info("release_not_told", "\tinfo.release_open = access_is_open(&app->access, now);\n", ""),
    info("release_as_last_noted", "\tinfo.release_open = access_is_open(&app->access, now);", "\tinfo.release_open = app->access.open;"),
    info("release_left_not_told", "\tinfo.release_left_s = access_seconds_left(&app->access, now);\n", ""),
    info("release_left_of_question", "\tinfo.release_left_s = access_seconds_left(&app->access, now);", "\tinfo.release_left_s = access_ask_seconds_left(&app->access, now);"),
    info("ssid_is_ap_ssid", "\tinfo.ssid = app->ssid;", "\tinfo.ssid = app->ap_ssid;"),
    info("without_ip", "\tinfo.ip = app->ip;\n", ""),
    info("ip_is_host", "\tinfo.ip = app->ip;", "\tinfo.ip = app_host(app);"),
    info("without_rssi", "\tinfo.rssi = app->rssi;\n", ""),
    info("rssi_positive", "\tinfo.rssi = app->rssi;", "\tinfo.rssi = -app->rssi;"),
    info("ap_not_told", "\tinfo.ap_on = link_ap_on(&app->link);\n", ""),
    info("ap_as_wanted", "\tinfo.ap_on = link_ap_on(&app->link);", "\tinfo.ap_on = app->link.ap_wanted;"),
    info("ap_ssid_is_ssid", "\tinfo.ap_ssid = app->ap_ssid;", "\tinfo.ap_ssid = app->ssid;"),
    info("ap_ssid_is_ap_password", "\tinfo.ap_ssid = app->ap_ssid;", "\tinfo.ap_ssid = app->ap_password;"),
    info("host_is_ip", "\tinfo.wican_host = app_host(app);", "\tinfo.wican_host = app->ip;"),
    info("host_of_first_profile", "\tinfo.wican_host = app_host(app);", "\tinfo.wican_host = app->profiles[0].host;"),
    info("without_id", "\tinfo.wican_id = app->poll.bound_id;\n", ""),
    info("id_of_state", "\tinfo.wican_id = app->poll.bound_id;", "\tinfo.wican_id = state != NULL ? state->id : \"\";"),
    info("without_fw", "\tif(state != NULL) info.wican_fw = state->fw;\n", "\t(void)state;\n"),
    info("fw_is_git_of_adapter", "\tif(state != NULL) info.wican_fw = state->fw;", "\tif(state != NULL) info.wican_fw = state->git;"),
    info("view_at_time_zero", "\tinfo.view = text_view_word(conn_view(&app->poll.conn, now));", "\tinfo.view = text_view_word(conn_view(&app->poll.conn, 0));"),
    info("view_always_live", "\tinfo.view = text_view_word(conn_view(&app->poll.conn, now));", "\tinfo.view = text_view_word(CONN_VIEW_LIVE);"),
    info("view_as_text_of_screen", "\tinfo.view = text_view_word(conn_view(&app->poll.conn, now));", "\tinfo.view = text_view(conn_view(&app->poll.conn, now));"),
    info("without_layout_name", "\tinfo.layout_name = app->layout.name;\n", ""),
    info("layout_name_of_builtin", "\tinfo.layout_name = app->layout.name;", "\tinfo.layout_name = app->builtin.name;"),
    info("source_words_swapped", "{\"stored\", \"builtin\", \"generated\", \"preview\"}", "{\"builtin\", \"stored\", \"generated\", \"preview\"}"),
    info("source_generated_is_preview", "{\"stored\", \"builtin\", \"generated\", \"preview\"}", "{\"stored\", \"builtin\", \"preview\", \"generated\"}"),
    info("source_preview_without_word", "\tif((unsigned)app->source < sizeof(sources) / sizeof(sources[0])) info.layout_source", "\tif((unsigned)app->source < sizeof(sources) / sizeof(sources[0]) - 1) info.layout_source"),
    info("source_stored_without_word", "\tif((unsigned)app->source < sizeof(sources) / sizeof(sources[0])) info.layout_source",
         "\tif(app->source != APP_LAYOUT_STORED && (unsigned)app->source < sizeof(sources) / sizeof(sources[0])) info.layout_source"),
    info("source_beyond_enum_is_preview", "\tif((unsigned)app->source < sizeof(sources) / sizeof(sources[0])) info.layout_source = sources[app->source];",
         "\tinfo.layout_source = sources[(unsigned)app->source < sizeof(sources) / sizeof(sources[0]) ? app->source : APP_LAYOUT_PREVIEW];"),
    info("source_below_enum_is_stored", "\tif((unsigned)app->source < sizeof(sources) / sizeof(sources[0])) info.layout_source = sources[app->source];",
         "\tinfo.layout_source = sources[(int)app->source < 0 ? 0 : (unsigned)app->source < sizeof(sources) / sizeof(sources[0]) ? app->source : 0];"),
    info("http_ok_is_failed", "\tinfo.http_ok = app->poll.http_ok;", "\tinfo.http_ok = app->poll.http_failed;"),
    info("http_failed_is_ok", "\tinfo.http_failed = app->poll.http_failed;", "\tinfo.http_failed = app->poll.http_ok;"),
    info("without_reconnects", "\tinfo.reconnects = app->reconnects;\n", ""),
    info("without_settings", "\tinfo.settings = settings;\n", ""),
    info("settings_not_written", "\tsettings_to_json(&app->settings, settings, sizeof(settings));\n", "\tsettings[0] = '\\0';\n"),
    info("settings_room_too_small", "\tsettings_to_json(&app->settings, settings, sizeof(settings));", "\tsettings_to_json(&app->settings, settings, sizeof(settings) / 2);"),
    info("room_one_byte_smaller", "\treturn web_info_json(&info, out, APP_WEB_OUT_SIZE);", "\treturn web_info_json(&info, out, APP_WEB_OUT_SIZE - 1);"),
    info("room_one_byte_larger", "\treturn web_info_json(&info, out, APP_WEB_OUT_SIZE);", "\treturn web_info_json(&info, out, APP_WEB_OUT_SIZE + 1);"),
    info("room_of_700_bytes", "\treturn web_info_json(&info, out, APP_WEB_OUT_SIZE);", "\treturn web_info_json(&info, out, 700);"),

    # the reading routes
    ("app_web_get_time_of_caller", T, F, GET_TIME, "\tuint64_t now = now_ms;\n\tconst poll_t *poll = &app->poll;"),
    ("app_web_get_time_of_app", T, F, GET_TIME, "\tuint64_t now = now_ms > app->clock_ms ? app->clock_ms : app->clock_ms;\n\tconst poll_t *poll = &app->poll;"),
    ("app_web_get_takes_time_over", T, F, GET_TIME, "\tuint64_t now = advance(app, now_ms);\n\tconst poll_t *poll = &app->poll;"),
    ("app_web_get_info_not_served", T, F, "\t\tcase WEB_ROUTE_INFO:\n\t\t\twritten = info_json(app, out, now);\n\t\t\tbreak;\n",
     "\t\tcase WEB_ROUTE_INFO:\n\t\t\tinfo_json(app, out, now);\n\t\t\treturn refuse(404, \"not_found\", out, length);\n"),
    ("app_web_get_catalog_is_values", T, F, "\t\t\twritten = catalog_to_json(&poll->catalog, out, APP_WEB_OUT_SIZE);\n\t\t\tbreak;\n\n\t\tcase WEB_ROUTE_VALUES:\n",
     "\t\tcase WEB_ROUTE_VALUES:\n"),
    ("app_web_get_catalog_room_smaller", T, F, "\t\t\twritten = catalog_to_json(&poll->catalog, out, APP_WEB_OUT_SIZE);", "\t\t\twritten = catalog_to_json(&poll->catalog, out, APP_WEB_OUT_SIZE - 1);"),
    ("app_web_get_catalog_room_larger", T, F, "\t\t\twritten = catalog_to_json(&poll->catalog, out, APP_WEB_OUT_SIZE);", "\t\t\twritten = catalog_to_json(&poll->catalog, out, APP_WEB_OUT_SIZE + 1);"),
    ("app_web_get_catalog_room_double", T, F, "\t\t\twritten = catalog_to_json(&poll->catalog, out, APP_WEB_OUT_SIZE);", "\t\t\twritten = catalog_to_json(&poll->catalog, out, 2 * APP_WEB_OUT_SIZE);"),
    ("app_web_get_values_at_time_zero", T, F, VALUES, "\t\t\twritten = web_values_json(&poll->values, text_view_word(conn_view(&poll->conn, now)), 0, out, APP_WEB_OUT_SIZE);"),
    ("app_web_get_values_at_time_of_app", T, F, VALUES, "\t\t\twritten = web_values_json(&poll->values, text_view_word(conn_view(&poll->conn, now)), app->clock_ms, out, APP_WEB_OUT_SIZE);"),
    ("app_web_get_values_view_at_time_zero", T, F, VALUES, "\t\t\twritten = web_values_json(&poll->values, text_view_word(conn_view(&poll->conn, 0)), now, out, APP_WEB_OUT_SIZE);"),
    ("app_web_get_values_view_one_ms_late", T, F, VALUES, "\t\t\twritten = web_values_json(&poll->values, text_view_word(conn_view(&poll->conn, now + 1)), now, out, APP_WEB_OUT_SIZE);"),
    ("app_web_get_values_only_in_a_network", T, F, VALUES,
     "\t\t\twritten = web_values_json(&poll->values, text_view_word(conn_view(&poll->conn, now)), poll->wifi ? now : UINT64_MAX, out, APP_WEB_OUT_SIZE);"),
    ("app_web_get_values_view_always_live", T, F, VALUES, "\t\t\twritten = web_values_json(&poll->values, text_view_word(CONN_VIEW_LIVE), now, out, APP_WEB_OUT_SIZE);"),
    ("app_web_get_values_without_view", T, F, VALUES, "\t\t\twritten = web_values_json(&poll->values, NULL, now, out, APP_WEB_OUT_SIZE);"),
    ("app_web_get_layout_is_builtin_text", T, F, "\t\t\treturn answer_text(app->layout_text, app->layout_length, out, length);", "\t\t\treturn answer_text(app->builtin_text, app->builtin_length, out, length);"),
    ("app_web_get_dtc_list_not_held", T, F, DTC_LAST, DTC_LAST.replace("poll->has_list ? poll->list_text : NULL", "poll->list_text")),
    ("app_web_get_dtc_old_not_held", T, F, DTC_LAST, DTC_LAST.replace("poll->has_old ? poll->old_text : NULL", "poll->old_text")),
    ("app_web_get_dtc_never_a_list", T, F, DTC_LAST, DTC_LAST.replace("poll->has_list ? poll->list_text : NULL", "NULL")),
    ("app_web_get_dtc_never_an_old_list", T, F, DTC_LAST, DTC_LAST.replace("poll->has_old ? poll->old_text : NULL", "NULL")),
    ("app_web_get_dtc_lists_swapped", T, F, DTC_LAST, DTC_LAST.replace("poll->has_list ? poll->list_text : NULL", "poll->has_old ? poll->old_text : NULL", 1).replace(
        "\t\t\t                            poll->has_old ? poll->old_text : NULL", "\t\t\t                            poll->has_list ? poll->list_text : NULL")),
    ("app_web_get_dtc_list_while_old", T, F, DTC_LAST, DTC_LAST.replace("poll->has_list ? poll->list_text : NULL", "poll->has_old ? poll->list_text : NULL")),
    ("app_web_get_dtc_old_while_list", T, F, DTC_LAST, DTC_LAST.replace("poll->has_old ? poll->old_text : NULL", "poll->has_list ? poll->old_text : NULL")),
    ("app_web_get_dtc_old_only_after_own_request", T, F, DTC_LAST, DTC_LAST.replace("poll->has_old ? poll->old_text : NULL", "poll->has_old && poll->flow.phase != DTC_FLOW_IDLE ? poll->old_text : NULL")),
    ("app_web_get_dtc_list_only_while_shown", T, F, DTC_LAST, DTC_LAST.replace("poll->has_list ? poll->list_text : NULL", "poll->flow.phase == DTC_FLOW_LIST ? poll->list_text : NULL")),
    ("app_web_get_dtc_age_from_acceptance", T, F, DTC_LAST, DTC_LAST.replace("poll->flow.list_end_ms", "poll->flow.accepted_ms")),
    ("app_web_get_dtc_age_in_milliseconds", T, F, DTC_LAST, DTC_LAST.replace("seconds(passed(now, poll->flow.list_end_ms))", "(uint32_t)passed(now, poll->flow.list_end_ms)")),
    ("app_web_get_wifi_current_is_ap", T, F, WIFI_LIST, WIFI_LIST.replace("app->ssid", "app->ap_ssid")),
    ("app_web_get_wifi_current_is_first_profile", T, F, WIFI_LIST, WIFI_LIST.replace("app->ssid", "app->profiles[0].ssid")),
    ("app_web_get_wifi_without_profiles", T, F, WIFI_LIST, WIFI_LIST.replace("app->profile_count", "0")),
    ("app_web_get_wifi_without_last_profile", T, F, WIFI_LIST, WIFI_LIST.replace("app->profile_count", "app->profile_count - 1")),
    ("app_web_get_wifi_profiles_of_link", T, F, WIFI_LIST, WIFI_LIST.replace("app->profile_count", "app->link.profile != -1 ? 1 : 0")),
    ("app_web_get_wifi_without_seen", T, F, WIFI_LIST, WIFI_LIST.replace("app->seen_count", "0")),
    ("app_web_get_wifi_seen_only_in_a_network", T, F, WIFI_LIST, WIFI_LIST.replace("app->seen_count", "link_up(&app->link) ? app->seen_count : 0")),
    ("app_web_get_wifi_current_only_with_adapter", T, F, WIFI_LIST, WIFI_LIST.replace("app->ssid", "link_up(&app->link) ? app->ssid : \"\"")),
    ("app_web_get_wifi_without_last_seen", T, F, WIFI_LIST, WIFI_LIST.replace("app->seen_count", "app->seen_count - 1")),
    ("app_web_get_ticket_number_one_more", T, F, TICKET, TICKET.replace("web_ticket_json(ticket,", "web_ticket_json(ticket + 1,")),
    ("app_web_get_ticket_always_last", T, F, TICKET, TICKET.replace("access_ticket(&app->access, ticket, now)", "access_ticket(&app->access, app->access.ticket, now)")),
    ("app_web_get_ticket_at_time_zero", T, F, TICKET, TICKET.replace("access_ticket(&app->access, ticket, now)", "access_ticket(&app->access, ticket, 0)")),
    ("app_web_get_ticket_as_last_noted", T, F, TICKET, TICKET.replace("access_ticket(&app->access, ticket, now)", "(ticket == app->access.ticket ? app->access.ticket_end : access_ticket(&app->access, ticket, now))")),
    ("app_web_get_ticket_without_time_left", T, F, TICKET, TICKET.replace("access_ask_seconds_left(&app->access, now)", "0")),
    ("app_web_get_ticket_left_at_time_zero", T, F, TICKET, TICKET.replace("access_ask_seconds_left(&app->access, now)", "access_ask_seconds_left(&app->access, 0)")),
    ("app_web_get_ticket_left_of_release", T, F, TICKET, TICKET.replace("access_ask_seconds_left(&app->access, now)", "access_seconds_left(&app->access, now)")),
    ("app_web_get_other_route_is_400", T, F, "\t\tdefault:\n\t\t\treturn refuse(404, \"not_found\", out, length);", "\t\tdefault:\n\t\t\treturn refuse(400, \"not_found\", out, length);"),
    ("app_web_get_other_route_other_word", T, F, "\t\tdefault:\n\t\t\treturn refuse(404, \"not_found\", out, length);", "\t\tdefault:\n\t\t\treturn refuse(404, \"route\", out, length);"),
    ("app_web_get_other_route_is_info", T, F, "\t\tdefault:\n\t\t\treturn refuse(404, \"not_found\", out, length);", "\t\tdefault:\n\t\t\twritten = info_json(app, out, now);\n\t\t\tbreak;"),
    ("app_web_get_answers_201", T, F, "\treturn answer(200, written, out, length);\n}\n\nvoid app_web_seen", "\treturn answer(201, written, out, length);\n}\n\nvoid app_web_seen"),

    # the networks of a scan
    ("app_web_seen_without_list_read", T, F, SEEN_NONE, "\tif(count < 0) count = 0;\n"),
    ("app_web_seen_negative_count_kept", T, F, SEEN_NONE, "\tif(seen == NULL) count = 0;\n"),
    ("app_web_seen_minus_one_kept", T, F, SEEN_NONE, "\tif(seen == NULL || count < -1) count = 0;\n"),
    ("app_web_seen_not_limited", T, F, SEEN_MAX, ""),
    ("app_web_seen_one_more_kept", T, F, SEEN_MAX, "\tif(count > LINK_SEEN_MAX + 1) count = LINK_SEEN_MAX + 1;\n"),
    ("app_web_seen_one_less_kept", T, F, SEEN_MAX, "\tif(count > LINK_SEEN_MAX - 1) count = LINK_SEEN_MAX - 1;\n"),
    ("app_web_seen_too_many_are_none", T, F, SEEN_MAX, "\tif(count > LINK_SEEN_MAX) count = 0;\n"),
    ("app_web_seen_last_ones_kept", T, F, "\tfor(int i = 0; i < count; i++) app->seen[i] = seen[i];", "\tfor(int i = 0; i < count; i++) app->seen[i] = seen[count - 1 - i];"),
    ("app_web_seen_first_one_repeated", T, F, "\tfor(int i = 0; i < count; i++) app->seen[i] = seen[i];", "\tfor(int i = 0; i < count; i++) app->seen[i] = seen[0];"),
    ("app_web_seen_one_more_copied", T, F, "\tfor(int i = 0; i < count; i++) app->seen[i] = seen[i];", "\tfor(int i = 0; i <= count; i++) app->seen[i] = seen[i];"),
    ("app_web_seen_not_copied", T, F, "\tfor(int i = 0; i < count; i++) app->seen[i] = seen[i];\n", ""),
    ("app_web_seen_count_not_kept", T, F, SEEN_COPY, "\tfor(int i = 0; i < count; i++) app->seen[i] = seen[i];"),
    ("app_web_seen_count_only_grows", T, F, SEEN_COPY, "\tfor(int i = 0; i < count; i++) app->seen[i] = seen[i];\n\tif(count > app->seen_count) app->seen_count = count;"),
    ("app_web_seen_shorter_list_keeps_count", T, F, SEEN_COPY,
     "\tfor(int i = 0; i < count; i++) app->seen[i] = seen[i];\n\tapp->seen_count = count > app->seen_count || count == 0 ? count : app->seen_count;"),
    ("app_web_seen_added_to_old", T, F, SEEN_COPY,
     "\tfor(int i = 0; i < count && app->seen_count < LINK_SEEN_MAX; i++) app->seen[app->seen_count++] = seen[i];"),

    # the layout
    ("app_web_layout_serves_every_route", T, F, L_ROUTES, ""),
    ("app_web_layout_check_not_served", T, F, L_ROUTES, L_ROUTES.replace("route != WEB_ROUTE_LAYOUT_CHECK && ", "")),
    ("app_web_layout_apply_not_served", T, F, L_ROUTES, L_ROUTES.replace("route != WEB_ROUTE_LAYOUT_APPLY && ", "")),
    ("app_web_layout_save_not_served", T, F, L_ROUTES, L_ROUTES.replace("route != WEB_ROUTE_LAYOUT_SAVE && ", "")),
    ("app_web_layout_reset_not_served", T, F, L_ROUTES, L_ROUTES.replace(" && route != WEB_ROUTE_LAYOUT_RESET", "")),
    ("app_web_layout_serves_get", T, F, L_ROUTES, L_ROUTES.replace("route != WEB_ROUTE_LAYOUT_CHECK && ", "route != WEB_ROUTE_LAYOUT && route != WEB_ROUTE_LAYOUT_CHECK && ")),
    ("app_web_layout_other_route_is_400", T, F, L_ROUTES, L_ROUTES.replace("404", "400")),
    ("app_web_layout_without_release", T, F, L_WRITE, "\tif(route != WEB_ROUTE_LAYOUT_CHECK) advance(app, now_ms);"),
    ("app_web_layout_only_save_needs_release", T, F, L_WRITE, L_WRITE.replace("route != WEB_ROUTE_LAYOUT_CHECK", "route == WEB_ROUTE_LAYOUT_SAVE")),
    ("app_web_layout_apply_needs_no_release", T, F, L_WRITE, L_WRITE.replace("route != WEB_ROUTE_LAYOUT_CHECK", "route != WEB_ROUTE_LAYOUT_CHECK && route != WEB_ROUTE_LAYOUT_APPLY")),
    ("app_web_layout_reset_needs_no_release", T, F, L_WRITE, L_WRITE.replace("route != WEB_ROUTE_LAYOUT_CHECK", "route != WEB_ROUTE_LAYOUT_CHECK && route != WEB_ROUTE_LAYOUT_RESET")),
    ("app_web_layout_check_needs_release", T, F, L_WRITE, L_WRITE.replace("route != WEB_ROUTE_LAYOUT_CHECK && ", "")),
    ("app_web_layout_check_takes_time_over", T, F, L_WRITE, "\tadvance(app, now_ms);\n" + L_WRITE),
    ("app_web_layout_release_not_renewed", T, F, L_WRITE, L_WRITE.replace("!access_write(", "!access_is_open(")),
    ("app_web_layout_release_one_ms_late", T, F, L_WRITE, L_WRITE.replace("advance(app, now_ms))", "advance(app, now_ms) + 1)")),
    ("app_web_layout_reset_gives_release_again", T, F, L_WRITE, L_WRITE + "\n\tif(route == WEB_ROUTE_LAYOUT_RESET) access_open(&app->access, app->clock_ms);"),
    ("app_web_layout_release_renewed_twice", T, F, L_WRITE, L_WRITE + "\n\tif(route != WEB_ROUTE_LAYOUT_CHECK) access_write(&app->access, app->clock_ms + 1);"),
    ("app_web_layout_time_not_taken_over", T, F, L_WRITE, L_WRITE.replace("advance(app, now_ms)", "time_at(app, now_ms)")),
    ("app_web_layout_time_of_caller", T, F, L_WRITE, L_WRITE.replace("advance(app, now_ms)", "now_ms")),
    ("app_web_layout_locked_is_401", T, F, L_WRITE, L_WRITE.replace("403", "401")),
    ("app_web_layout_locked_other_word", T, F, L_WRITE, L_WRITE.replace("\"locked\"", "\"closed\"")),
    ("app_web_layout_reset_keeps_views", T, F, L_RESET, L_RESET.replace("\t\tapp_choose_layout(app);\n", "")),
    ("app_web_layout_reset_without_event", T, F, L_ERASE, ""),
    ("app_web_layout_reset_event_replaces", T, F, L_ERASE, "\t\tapp->events = APP_EVENT_ERASE_LAYOUT;\n"),
    ("app_web_layout_reset_event_after_preview_only", T, F, L_RESET, "\t\tif(app->source != APP_LAYOUT_PREVIEW)\n" + L_ERASE.replace("\t\tapp", "\t\t\tapp") + "\t\tapp_choose_layout(app);\n"),
    ("app_web_layout_reset_stores", T, F, L_ERASE, "\t\tapp->events = (app->events & ~APP_EVENT_ERASE_LAYOUT) | APP_EVENT_STORE_LAYOUT;\n"),
    ("app_web_layout_reset_stores_as_well", T, F, L_ERASE, "\t\tapp->events |= APP_EVENT_ERASE_LAYOUT | APP_EVENT_STORE_LAYOUT;\n"),
    ("app_web_layout_reset_event_only_if_stored", T, F, L_RESET, "\t\tif(app->source == APP_LAYOUT_STORED)\n" + L_ERASE.replace("\t\tapp", "\t\t\tapp") + "\t\tapp_choose_layout(app);\n"),
    ("app_web_layout_reset_always_builtin", T, F, L_RESET,
     "\t\tapp_choose_layout(app);\n\t\tif(app->has_builtin)\n\t\t{\n\t\t\tapp->layout = app->builtin;\n\t\t\tapp->source = APP_LAYOUT_BUILTIN;\n\t\t}\n" + L_ERASE),
    # of a save and a reset between two takes the later alone counts
    ("app_web_layout_reset_leaves_the_save_that_waits", T, F, L_ERASE, "\t\tapp->events |= APP_EVENT_ERASE_LAYOUT;\n"),
    ("app_web_layout_reset_takes_every_store_back", T, F, L_ERASE, L_ERASE.replace("~APP_EVENT_STORE_LAYOUT", "~(APP_EVENT_STORE_LAYOUT | APP_EVENT_STORE_SETTINGS | APP_EVENT_STORE_BOUND)")),
    ("app_web_layout_reset_takes_the_restart_back", T, F, L_ERASE, L_ERASE.replace("~APP_EVENT_STORE_LAYOUT", "~(APP_EVENT_STORE_LAYOUT | APP_EVENT_REBOOT)")),
    ("app_web_layout_reset_behind_a_save_does_nothing", T, F, L_ERASE, "\t\tif(app->events & APP_EVENT_STORE_LAYOUT) app->events &= ~APP_EVENT_STORE_LAYOUT;\n\t\telse app->events |= APP_EVENT_ERASE_LAYOUT;\n"),
    ("app_web_layout_reset_behind_a_save_is_dropped", T, F, L_ERASE, "\t\tif(!(app->events & APP_EVENT_STORE_LAYOUT)) app->events |= APP_EVENT_ERASE_LAYOUT;\n"),
    ("app_web_layout_refused_save_takes_the_reset_back", T, F, L_BODY, "\t\tif(route == WEB_ROUTE_LAYOUT_SAVE) app->events &= ~APP_EVENT_ERASE_LAYOUT;\n" + L_BODY),
    ("app_web_layout_preview_takes_the_reset_back", T, F, L_BODY, "\t\tif(route == WEB_ROUTE_LAYOUT_APPLY) app->events &= ~APP_EVENT_ERASE_LAYOUT;\n" + L_BODY),
    ("app_web_layout_check_takes_the_reset_back", T, F, L_BODY, "\t\tif(route == WEB_ROUTE_LAYOUT_CHECK) app->events &= ~APP_EVENT_ERASE_LAYOUT;\n" + L_BODY),
    ("app_web_layout_locked_reset_takes_the_save_back", T, F, L_WRITE, "\tif(route == WEB_ROUTE_LAYOUT_RESET) app->events &= ~APP_EVENT_STORE_LAYOUT;\n" + L_WRITE),
    ("app_web_layout_locked_save_takes_the_reset_back", T, F, L_WRITE, "\tif(route == WEB_ROUTE_LAYOUT_SAVE) app->events &= ~APP_EVENT_ERASE_LAYOUT;\n" + L_WRITE),
    ("app_web_layout_refused_is_200", T, F, L_REFUSED, L_REFUSED.replace("answer(400,", "answer(200,")),
    ("app_web_layout_refused_is_422", T, F, L_REFUSED, L_REFUSED.replace("answer(400,", "answer(422,")),
    ("app_web_layout_refused_is_word", T, F, L_REFUSED, "\t\t\treturn refuse(400, \"body\", out, length);"),
    ("app_web_layout_check_applies", T, F, L_CHECK, ""),
    ("app_web_layout_check_is_201", T, F, L_CHECK, L_CHECK.replace("answer(200,", "answer(201,")),
    ("app_web_layout_check_reports_views_in_use", T, F, L_CHECK, L_CHECK.replace("&app->checked, catalog", "&app->layout, catalog")),
    ("app_web_layout_check_without_warnings", T, F, L_CHECK, L_CHECK.replace("\t\t\treturn answer", "\t\t\treport.warnings = 0;\n\t\t\treturn answer")),
    ("app_web_layout_not_taken", T, F, L_TAKE, ""),
    ("app_web_layout_taken_only_when_saved", T, F, L_TAKE, "\t\tif(route == WEB_ROUTE_LAYOUT_SAVE) app->layout = app->checked;\n"),
    ("app_web_layout_read_with_half_the_tokens", T, F, "\t\tif(!layout_parse(body, body_length, &app->checked, &report, app->work, app->work_count))",
     "\t\tif(!layout_parse(body, body_length, &app->checked, &report, app->work, app->work_count / 2))"),
    ("app_web_layout_text_not_kept", T, F, L_TEXT, ""),
    ("app_web_layout_text_kept_only_when_saved", T, F, L_TEXT, "\t\tif(route == WEB_ROUTE_LAYOUT_SAVE) memcpy(app->layout_text, body, body_length);\n"),
    ("app_web_layout_text_without_last_byte", T, F, L_TEXT, "\t\tmemcpy(app->layout_text, body, body_length - 1);\n"),
    ("app_web_layout_text_without_end", T, F, L_END, ""),
    ("app_web_layout_text_ends_one_early", T, F, L_END, "\t\tapp->layout_text[body_length - 1] = '\\0';\n"),
    ("app_web_layout_length_not_kept", T, F, L_LENGTH, ""),
    ("app_web_layout_length_one_more", T, F, L_LENGTH, "\t\tapp->layout_length = body_length + 1;\n"),
    ("app_web_layout_applied_keeps_source", T, F, L_PREVIEW, ""),
    ("app_web_layout_applied_is_stored", T, F, L_PREVIEW, "\t\tapp->source = APP_LAYOUT_STORED;\n"),
    ("app_web_layout_applied_is_generated", T, F, L_PREVIEW, "\t\tapp->source = APP_LAYOUT_GENERATED;\n"),
    ("app_web_layout_saved_is_preview", T, F, L_SAVE, L_SAVE.replace("\t\t\tapp->source = APP_LAYOUT_STORED;\n", "")),
    ("app_web_layout_saved_without_event", T, F, L_STORE, ""),
    ("app_web_layout_saved_event_replaces", T, F, L_STORE, "\t\t\tapp->events = APP_EVENT_STORE_LAYOUT;\n"),
    ("app_web_layout_saved_touches_check_sum", T, F, L_SAVE, L_SAVE.replace("\t\t\tapp->source = APP_LAYOUT_STORED;\n", "\t\t\tapp->source = APP_LAYOUT_STORED;\n\t\t\tapp->catalog_sum = 0;\n")),
    ("app_web_layout_saved_erases", T, F, L_STORE, "\t\t\tapp->events = (app->events & ~APP_EVENT_STORE_LAYOUT) | APP_EVENT_ERASE_LAYOUT;\n"),
    ("app_web_layout_saved_erases_as_well", T, F, L_STORE, "\t\t\tapp->events |= APP_EVENT_STORE_LAYOUT | APP_EVENT_ERASE_LAYOUT;\n"),
    ("app_web_layout_saved_leaves_the_reset_that_waits", T, F, L_STORE, "\t\t\tapp->events |= APP_EVENT_STORE_LAYOUT;\n"),
    ("app_web_layout_saved_takes_every_store_back", T, F, L_STORE, L_STORE.replace("~APP_EVENT_ERASE_LAYOUT", "~(APP_EVENT_ERASE_LAYOUT | APP_EVENT_STORE_SETTINGS | APP_EVENT_STORE_BOUND)")),
    ("app_web_layout_saved_takes_the_restart_back", T, F, L_STORE, L_STORE.replace("~APP_EVENT_ERASE_LAYOUT", "~(APP_EVENT_ERASE_LAYOUT | APP_EVENT_REBOOT)")),
    ("app_web_layout_saved_behind_a_reset_does_nothing", T, F, L_STORE, "\t\t\tif(app->events & APP_EVENT_ERASE_LAYOUT) app->events &= ~APP_EVENT_ERASE_LAYOUT;\n\t\t\telse app->events |= APP_EVENT_STORE_LAYOUT;\n"),
    ("app_web_layout_saved_behind_a_reset_is_dropped", T, F, L_STORE, "\t\t\tif(!(app->events & APP_EVENT_ERASE_LAYOUT)) app->events |= APP_EVENT_STORE_LAYOUT;\n"),
    ("app_web_layout_applied_is_stored_with_event", T, F, L_SAVE, L_SAVE.replace("if(route == WEB_ROUTE_LAYOUT_SAVE)", "if(route != WEB_ROUTE_LAYOUT_CHECK)")),
    ("app_web_layout_applied_raises_event", T, F, L_SAVE, L_SAVE.replace(L_STORE + "\t\t}\n", "\t\t}\n" + L_STORE[1:])),
    ("app_web_layout_page_stays", T, F, L_PAGE, ""),
    ("app_web_layout_page_zero", T, F, L_PAGE, L_PAGE.replace(L_FIRST, "app->nav.page = 0;\n")),
    ("app_web_layout_page_left_to_tick", T, F, L_PAGE, L_PAGE.replace(L_FIRST, "app->nav.page = -1;\n")),
    ("app_web_layout_page_stays_on_reset", T, F, L_PAGE, L_PAGE.replace("route == WEB_ROUTE_LAYOUT_RESET || ", "")),
    # a layout from the browser keeps the page shown where it shows one at that position: the rule taken back
    ("app_web_layout_first_page_always", T, F, L_PAGE, "\t" + L_FIRST),
    ("app_web_layout_first_page_when_saved", T, F, L_PAGE, L_PAGE.replace("route == WEB_ROUTE_LAYOUT_RESET", "route != WEB_ROUTE_LAYOUT_APPLY")),
    ("app_web_layout_first_page_when_applied", T, F, L_PAGE, L_PAGE.replace("route == WEB_ROUTE_LAYOUT_RESET", "route != WEB_ROUTE_LAYOUT_SAVE")),
    # ... a page that is there but not shown is no page to stay on, and without one the first page is shown, not the nearest
    ("app_web_layout_page_kept_if_position_exists", T, F, L_PAGE, L_PAGE.replace(L_KEPT, "(app->nav.page < 0 || app->nav.page >= app->layout.page_count)")),
    ("app_web_layout_page_kept_if_not_hidden", T, F,
     L_PAGE, L_PAGE.replace(L_KEPT, "(app->nav.page < 0 || app->nav.page >= app->layout.page_count || app->layout.pages[app->nav.page].hidden)")),
    ("app_web_layout_page_kept_if_next_is_shown", T, F, L_PAGE, L_PAGE.replace("app->nav.page, catalog)", "app->nav.page + 1, catalog)")),
    ("app_web_layout_page_kept_if_first_is_shown", T, F, L_PAGE, L_PAGE.replace("app->nav.page, catalog)", "0, catalog)")),
    ("app_web_layout_nearest_page_instead_of_first", T, F, L_PAGE, L_PAGE.replace(L_FIRST, "app->nav.page = layout_step_page(&app->layout, catalog, app->nav.page, 1);\n")),
    ("app_web_layout_page_of_checked_layout", T, F, L_PAGE, L_PAGE.replace(L_FIRST, "app->nav.page = layout_first_page(&app->checked, catalog);\n")),
    ("app_web_layout_moves_the_focus", T, F, L_PAGE, L_PAGE + "\tapp->nav.row = 0;\n"),
    ("app_web_layout_leaves_the_menu", T, F, L_PAGE, L_PAGE + "\tif(route == WEB_ROUTE_LAYOUT_SAVE) app->nav.screen = NAV_PAGES;\n"),
    ("app_web_layout_applied_leaves_the_menu", T, F, L_PAGE, L_PAGE + "\tif(route == WEB_ROUTE_LAYOUT_APPLY) app->nav.screen = NAV_PAGES;\n"),
    ("app_web_layout_counts_as_input", T, F, L_PAGE, L_PAGE + "\tapp->last_input_ms = app->clock_ms;\n"),
    ("app_web_layout_page_only_on_value_pages", T, F, L_PAGE, "\tif(app->nav.screen == NAV_PAGES)\n\t{\n\t" + L_PAGE + "\t}\n"),
    ("app_web_layout_page_kept_only_on_value_pages", T, F, L_PAGE, L_PAGE.replace(L_KEPT, "(app->nav.screen != NAV_PAGES || " + L_KEPT + ")")),
    ("app_web_layout_page_ignores_hidden", T, F, L_PAGE, L_PAGE.replace(L_FIRST, "app->nav.page = app->layout.page_count > 0 ? 0 : -1;\n")),
    ("app_web_layout_reports_checked_layout", T, F, L_REPORT, L_REPORT.replace("&app->layout, catalog", "&app->checked, catalog")),
    ("app_web_layout_reports_as_refused", T, F, L_REPORT, L_REPORT.replace("web_layout_report_json(true,", "web_layout_report_json(false,")),
    ("app_web_layout_answers_201", T, F, L_REPORT, L_REPORT.replace("answer(200,", "answer(201,")),
    ("app_web_layout_answers_ok", T, F, L_REPORT, "\treturn answer_text(OK_BODY, sizeof(OK_BODY) - 1, out, length);"),

    # POST /api/wifi
    ("app_web_wifi_serves_every_route", T, F, W_ROUTES, ""),
    ("app_web_wifi_store_not_served", T, F, W_ROUTES, "\tif(route != WEB_ROUTE_WIFI_FORGET) return refuse(404, \"not_found\", out, length);\n"),
    ("app_web_wifi_forget_not_served", T, F, W_ROUTES, "\tif(route != WEB_ROUTE_WIFI_STORE) return refuse(404, \"not_found\", out, length);\n"),
    ("app_web_wifi_serves_list", T, F, W_ROUTES, "\tif(route != WEB_ROUTE_WIFI && route != WEB_ROUTE_WIFI_STORE && route != WEB_ROUTE_WIFI_FORGET) return refuse(404, \"not_found\", out, length);\n"),
    ("app_web_wifi_time_not_taken_over", T, F, W_TIME, W_TIME.replace("advance(app, now_ms)", "time_at(app, now_ms)")),
    ("app_web_wifi_time_of_caller", T, F, W_TIME, W_TIME.replace("advance(app, now_ms)", "now_ms")),
    ("app_web_wifi_other_route_takes_time", T, F, W_ROUTES, "\tadvance(app, now_ms);\n" + W_ROUTES),
    ("app_web_wifi_asked_without_looking", T, F, W_MAY, "\t\tstatus = 0;\n\t\tif(status != 0) return status;\n"),
    ("app_web_wifi_asked_during_upload", T, F, W_MAY, W_MAY.replace("app->uploading", "false")),
    ("app_web_wifi_not_asked_while_busy", T, F, W_MAY, W_MAY.replace(W_BUSY, "app_busy(app)")),
    ("app_web_wifi_asked_while_a_request_is_under_way", T, F, W_MAY, W_MAY.replace(W_BUSY, "app->uploading")),
    ("app_web_wifi_asked_while_a_request_is_under_way_unless_uploading", T, F, W_MAY, W_MAY.replace(W_BUSY, "app->uploading && request_under_way(app)")),
    ("app_web_wifi_not_asked_under_update", T, F, W_MAY, W_MAY.replace("app->uploading", "app->uploading || app->update_pending")),
    ("app_web_wifi_not_asked_during_dialog", T, F, W_MAY, W_MAY.replace("app->uploading", "app->uploading || app->nav.screen == NAV_DTC_CONFIRM")),
    ("app_web_wifi_judged_one_ms_late", T, F, W_MAY, W_MAY.replace("out, length, now);", "out, length, now + 1);")),
    ("app_web_wifi_body_before_busy_and_asking", T, F, W_MAY, W_MAY.replace(
        "\t\tif(status != 0)", "\t\tif(status == 409 && json_parse(body, body_length, app->work, app->work_count) < 0) return refuse(400, \"body\", out, length);\n\t\tif(status != 0)")),
    ("app_web_wifi_body_before_locked", T, F, W_MAY, W_MAY.replace(
        "\t\tif(status != 0)", "\t\tif(status == 403 && json_parse(body, body_length, app->work, app->work_count) < 0) return refuse(400, \"body\", out, length);\n\t\tif(status != 0)")),
    ("app_web_wifi_renews_release_when_refused", T, F, W_MAY, "\t\taccess_write(&app->access, now);\n" + W_MAY),
    ("app_web_wifi_bad_body_is_422", T, F, W_PARSE, W_PARSE.replace("refuse(400,", "refuse(422,")),
    ("app_web_wifi_bad_body_other_word", T, F, W_PARSE, W_PARSE.replace("\"body\"", "\"json\"")),
    ("app_web_wifi_bad_body_asked", T, F, W_PARSE, "\t\tweb_wifi_parse(body, body_length, &app->wifi_asked, app->work, app->work_count);\n"),
    ("app_web_wifi_bad_body_empties_request", T, F, W_PARSE, "\t\tmemset(&app->wifi_asked, 0, sizeof(app->wifi_asked));\n" + W_PARSE),
    ("app_web_wifi_bad_body_marks_request", T, F, W_PARSE, "\t\tapp->has_wifi_asked = true;\n" + W_PARSE),
    ("app_web_wifi_bad_body_renews_release", T, F, W_PARSE, W_PARSE.replace("\t\tif(!web_wifi_parse(", "\t\taccess_write(&app->access, now);\n\t\tif(!web_wifi_parse(")),
    ("app_web_wifi_read_as_forget_request", T, F, W_PARSE, W_PARSE.replace("web_wifi_parse(body, body_length, &app->wifi_asked,", "web_forget_parse(body, body_length, app->wifi_asked.ssid,")),
    ("app_web_wifi_not_marked_as_asked", T, F, W_HAS, ""),
    ("app_web_wifi_password_not_kept", T, F, W_HAS, "\t\tapp->has_wifi_asked = true;\n\t\tmemset(app->wifi_asked.password, 0, sizeof(app->wifi_asked.password));\n"),
    ("app_web_wifi_always_with_password", T, F, W_HAS, "\t\tapp->has_wifi_asked = true;\n\t\tapp->wifi_asked.has_password = true;\n"),
    ("app_web_wifi_host_not_kept", T, F, W_HAS, "\t\tapp->has_wifi_asked = true;\n\t\tapp->wifi_asked.host[0] = '\\0';\n"),
    ("app_web_wifi_detail_is_host", T, F, W_ASK, W_ASK.replace("app->wifi_asked.ssid", "app->wifi_asked.host")),
    ("app_web_wifi_detail_is_password", T, F, W_ASK, W_ASK.replace("app->wifi_asked.ssid", "app->wifi_asked.password")),
    ("app_web_wifi_detail_empty", T, F, W_ASK, W_ASK.replace("app->wifi_asked.ssid", "\"\"")),
    ("app_web_wifi_asked_as_reset", T, F, W_ASK, W_ASK.replace("ACCESS_ASK_WIFI", "ACCESS_ASK_RESET")),
    ("app_web_wifi_stored_at_once", T, F, W_ASK,
     "\t\tstatus = ask(app, ACCESS_ASK_WIFI, app->wifi_asked.ssid, out, length, now);\n\t\tapp_do(app, NAV_DO_ASK_CONFIRM, now + ACCESS_ASK_SHOWN_MS);\n\t\treturn status;"),

    # POST /api/wifi/forget
    ("app_web_forget_without_release", T, F, F_WRITE, F_BUSY_WHY),
    ("app_web_forget_release_not_renewed", T, F, F_WRITE, F_WRITE.replace("!access_write(", "!access_is_open(")),
    ("app_web_forget_release_one_ms_late", T, F, F_WRITE, F_WRITE.replace("now))", "now + 1))")),
    ("app_web_forget_needs_knob", T, F, F_WRITE, "\tstatus = ask_refused(app, ACCESS_ASK_WIFI, false, out, length, now);\n\tif(status != 0) return status;\n" + F_BUSY_WHY),
    ("app_web_forget_refused_while_busy", T, F, F_BUSY, F_BUSY.replace("request_under_way(app)", "app_busy(app)")),
    # not while a fault memory request of the display is under way
    ("app_web_forget_while_a_request_is_under_way", T, F, F_BUSY, ""),
    ("app_web_forget_busy_is_asking", T, F, F_BUSY, F_BUSY.replace("\"busy\"", "\"asking\"")),
    ("app_web_forget_busy_is_503", T, F, F_BUSY, F_BUSY.replace("refuse(409,", "refuse(503,")),
    ("app_web_forget_busy_before_locked", T, F, F_WRITE + F_BUSY, F_BUSY + F_LOCKED),
    ("app_web_forget_body_before_busy", T, F, F_BUSY + F_PARSE, F_PARSE + F_BUSY),
    ("app_web_forget_not_found_before_busy", T, F, F_BUSY + F_PARSE + F_FORGET + F_FOUND, F_PARSE + F_FORGET + F_FOUND + F_BUSY),
    ("app_web_forget_refused_in_the_clear_dialog", T, F, F_BUSY, F_BUSY.replace("request_under_way(app)", "request_under_way(app) || app->nav.screen == NAV_DTC_CONFIRM")),
    ("app_web_forget_refused_during_an_upload", T, F, F_BUSY, F_BUSY.replace("request_under_way(app)", "request_under_way(app) || app->uploading")),
    ("app_web_forget_refused_on_a_dark_screen", T, F, F_BUSY, F_BUSY.replace("request_under_way(app)", "request_under_way(app) || screen_unseen(app)")),
    ("app_web_forget_refused_while_a_question_waits", T, F, F_BUSY, F_BUSY.replace("request_under_way(app)", "request_under_way(app) || access_asking(&app->access, now) != ACCESS_ASK_NONE")),
    ("app_web_forget_bad_body_is_404", T, F, F_PARSE, F_PARSE.replace("refuse(400, \"body\"", "refuse(404, \"not_found\"")),
    ("app_web_forget_bad_body_forgets_first", T, F, F_PARSE, F_PARSE.replace("return refuse(400, \"body\", out, length);", "memcpy(ssid, app->profiles[0].ssid, sizeof(ssid));")),
    ("app_web_forget_long_names_refused", T, F, F_PARSE,
     "\tif(!web_forget_parse(body, body_length, ssid, app->work, app->work_count) || strlen(ssid) > 16) return refuse(400, \"body\", out, length);\n"),
    ("app_web_forget_unknown_is_ok", T, F, F_FOUND, ""),
    ("app_web_forget_unknown_is_400", T, F, F_FOUND, F_FOUND.replace("refuse(404, \"not_found\"", "refuse(400, \"body\"")),
    ("app_web_forget_unknown_before_bad_body", T, F, F_PARSE, "\tif(!web_forget_parse(body, body_length, ssid, app->work, app->work_count)) return refuse(404, \"not_found\", out, length);\n"),
    ("app_web_forget_last_network_refused", T, F, F_FOUND, "\tif(left == app->profile_count || left == 0) return refuse(404, \"not_found\", out, length);\n"),
    ("app_web_forget_count_stays", T, F, F_COUNT, ""),
    ("app_web_forget_link_not_told", T, F, F_LINK, ""),
    ("app_web_forget_link_told_old_count", T, F, F_LINK, "\tlink_profiles(&app->link, app->profiles, app->profile_count + 1, now);\n"),
    ("app_web_forget_link_told_one_ms_late", T, F, F_LINK, "\tlink_profiles(&app->link, app->profiles, app->profile_count, now + 1);\n"),
    ("app_web_forget_poll_told_one_ms_late", T, F, F_NET, "\tapp_net(app, now + 1);\n\tapp->events |= APP_EVENT_STORE_WIFI;\n"),
    ("app_web_forget_link_told_only_for_own_network", T, F, F_LINK, "\tif(strcmp(ssid, app->ssid) == 0) link_profiles(&app->link, app->profiles, app->profile_count, now);\n"),
    ("app_web_forget_poll_not_told", T, F, F_NET, "\tapp->events |= APP_EVENT_STORE_WIFI;\n"),
    ("app_web_forget_without_event", T, F, F_NET, "\tapp_net(app, now);\n"),
    ("app_web_forget_event_replaces", T, F, F_NET, "\tapp_net(app, now);\n\tapp->events = APP_EVENT_STORE_WIFI;\n"),
    ("app_web_forget_closes_release", T, F, F_NET, F_NET + "\tif(app->profile_count == 0) access_close(&app->access, now);\n"),
    ("app_web_forget_drops_question", T, F, F_NET, F_NET + "\tdrop_network(app);\n"),
    ("app_web_forget_refuses_question", T, F, F_NET, F_NET + "\taccess_refuse(&app->access, now);\n"),
    ("app_web_forget_forgets_binding", T, F, F_NET, F_NET + "\tif(app->profile_count == 0) app->poll.bound_id[0] = '\\0';\n"),
    ("app_web_forget_stores_settings", T, F, F_NET, "\tapp_net(app, now);\n\tapp->events |= APP_EVENT_STORE_SETTINGS;\n"),
    ("app_web_forget_restarts", T, F, F_NET, "\tapp_net(app, now);\n\tapp->events |= APP_EVENT_STORE_WIFI | APP_EVENT_REBOOT;\n"),
    ("app_web_forget_answers_empty_object", T, F, "\tapp->events |= APP_EVENT_STORE_WIFI;\n\treturn answer_text(OK_BODY, sizeof(OK_BODY) - 1, out, length);",
     "\tapp->events |= APP_EVENT_STORE_WIFI;\n\treturn answer_text(\"{}\", 2, out, length);"),

    # POST /api/settings
    ("app_web_settings_without_release", T, F, S_WRITE, "\tadvance(app, now_ms);\n\n\tbody = body_of(body, &body_length);\n\tif(!settings_from_json"),
    ("app_web_settings_release_not_renewed", T, F, S_WRITE, S_WRITE.replace("!access_write(", "!access_is_open(")),
    ("app_web_settings_release_one_ms_late", T, F, S_WRITE, S_WRITE.replace("advance(app, now_ms))", "advance(app, now_ms) + 1)")),
    ("app_web_settings_time_not_taken_over", T, F, S_WRITE, S_WRITE.replace("advance(app, now_ms)", "time_at(app, now_ms)")),
    ("app_web_settings_time_of_caller", T, F, S_WRITE, S_WRITE.replace("advance(app, now_ms)", "now_ms")),
    ("app_web_settings_locked_is_401", T, F, S_WRITE, S_WRITE.replace("403", "401")),
    ("app_web_settings_refused_is_422", T, F, S_REFUSED, S_REFUSED.replace("answer(400,", "answer(422,")),
    ("app_web_settings_refused_without_member", T, F, S_REFUSED, "\t\treturn refuse(400, \"body\", out, length);"),
    ("app_web_settings_refused_member_empty", T, F, S_REFUSED, S_REFUSED.replace(", member), out, length);", ", \"\"), out, length);")),
    ("app_web_settings_member_name_cut", T, F, "settings_from_json(&app->settings, body, body_length, member, sizeof(member),", "settings_from_json(&app->settings, body, body_length, member, sizeof(member) / 2,"),
    ("app_web_settings_refused_is_ok", T, F, S_REFUSED, "\t\treturn answer_text(OK_BODY, sizeof(OK_BODY) - 1, out, length);"),
    ("app_web_settings_knob_not_told", T, F, S_KNOB, ""),
    ("app_web_settings_knob_told_opposite", T, F, S_KNOB, "\tknob_set_reverse(&app->knob, !app->settings.reverse);\n"),
    ("app_web_settings_knob_not_told_while_pressed", T, F, S_KNOB, "\tif(!knob_is_pressed(&app->knob)) knob_set_reverse(&app->knob, app->settings.reverse);\n"),
    ("app_web_settings_knob_only_reversed", T, F, S_KNOB, "\tif(app->settings.reverse) knob_set_reverse(&app->knob, true);\n"),
    ("app_web_settings_without_event", T, F, S_EVENT, ""),
    ("app_web_settings_event_only_when_changed", T, F, S_EVENT, "\tif(body_length > 2) app->events |= APP_EVENT_STORE_SETTINGS;\n"),
    ("app_web_settings_event_replaces", T, F, S_EVENT, "\tapp->events = APP_EVENT_STORE_SETTINGS;\n"),
    ("app_web_settings_count_as_input", T, F, S_EVENT, S_EVENT + "\tapp->last_input_ms = app->clock_ms;\n"),
    ("app_web_settings_end_brightness_being_set", T, F, S_EVENT, S_EVENT + "\tapp->brightness_preview = -1;\n"),
    ("app_web_settings_give_release_again", T, F, S_EVENT, S_EVENT + "\taccess_open(&app->access, app->clock_ms);\n"),
    ("app_web_settings_stores_wifi", T, F, S_EVENT, "\tapp->events |= APP_EVENT_STORE_WIFI;\n"),
    ("app_web_settings_restarts", T, F, S_EVENT, "\tapp->events |= APP_EVENT_STORE_SETTINGS | APP_EVENT_REBOOT;\n"),
    ("app_web_settings_answers_ok", T, F, S_ANSWER, "\treturn answer_text(OK_BODY, sizeof(OK_BODY) - 1, out, length);"),
    ("app_web_settings_answers_202", T, F, S_ANSWER, S_ANSWER.replace("answer(200,", "answer(202,")),
    ("app_web_settings_changes_night_mode", T, F, S_EVENT, "\tapp->settings.night_mode = !app->settings.night_mode;\n" + S_EVENT),

    # POST /api/reboot and POST /api/reset
    ("app_web_action_serves_every_route", T, F, A_ROUTES, ""),
    ("app_web_action_reboot_not_served", T, F, A_ROUTES, "\tif(route != WEB_ROUTE_RESET) return refuse(404, \"not_found\", out, length);\n"),
    ("app_web_action_reset_not_served", T, F, A_ROUTES, "\tif(route != WEB_ROUTE_REBOOT) return refuse(404, \"not_found\", out, length);\n"),
    ("app_web_action_serves_ota", T, F, A_ROUTES, "\tif(route != WEB_ROUTE_REBOOT && route != WEB_ROUTE_RESET && route != WEB_ROUTE_OTA) return refuse(404, \"not_found\", out, length);\n"),
    ("app_web_action_other_route_takes_time", T, F, A_ROUTES, "\tadvance(app, now_ms);\n" + A_ROUTES),
    ("app_web_action_time_not_taken_over", T, F, A_TIME, A_TIME.replace("advance(app, now_ms)", "time_at(app, now_ms)")),
    ("app_web_action_time_of_caller", T, F, A_TIME, A_TIME.replace("advance(app, now_ms)", "now_ms")),
    ("app_web_reboot_without_release", T, F, B_WRITE, ""),
    ("app_web_reboot_release_not_renewed", T, F, B_WRITE, B_WRITE.replace("!access_write(", "!access_is_open(")),
    ("app_web_reboot_release_one_ms_late", T, F, B_WRITE, B_WRITE.replace("now))", "now + 1))")),
    ("app_web_reboot_while_busy", T, F, B_BUSY, ""),
    ("app_web_reboot_busy_before_locked", T, F, B_WRITE + B_WHY + B_BUSY, B_BUSY + B_WRITE),
    ("app_web_reboot_busy_by_upload_alone", T, F, B_BUSY, B_BUSY.replace("app_busy(app)", "app->uploading")),
    ("app_web_reboot_busy_by_request_alone", T, F, B_BUSY, B_BUSY.replace("app_busy(app)", "(app_busy(app) && !app->uploading)")),
    ("app_web_reboot_during_dialog", T, F, B_BUSY, B_BUSY.replace("app_busy(app)", "(app_busy(app) && app->nav.screen != NAV_DTC_CONFIRM)")),
    ("app_web_reboot_refused_while_asking", T, F, B_BUSY, B_BUSY + "\t\tif(access_asking(&app->access, now) != ACCESS_ASK_NONE) return refuse(409, \"asking\", out, length);\n"),
    ("app_web_reboot_busy_is_asking", T, F, B_BUSY, B_BUSY.replace("\"busy\"", "\"asking\"")),
    ("app_web_reboot_not_asked_for", T, F, B_DO, ""),
    ("app_web_reboot_one_ms_late", T, F, B_DO, "\t\tapp_do(app, NAV_DO_REBOOT, now + 1);\n"),
    ("app_web_reboot_gives_update_up", T, F, B_DO, B_DO + "\t\tapp->update_given_up = true;\n"),
    ("app_web_reboot_confirms_update", T, F, B_DO, B_DO + "\t\tif(app->update_pending) app_do(app, NAV_DO_UPDATE_OK, now);\n"),
    ("app_web_reboot_closes_release", T, F, B_DO, B_DO + "\t\taccess_close(&app->access, now);\n"),
    ("app_web_reboot_is_factory_reset", T, F, B_DO, "\t\tapp_do(app, NAV_DO_FACTORY_RESET, now);\n"),
    ("app_web_reboot_is_previous_firmware", T, F, B_DO, "\t\tapp_do(app, NAV_DO_PREVIOUS_FIRMWARE, now);\n"),
    ("app_web_reboot_needs_knob", T, F, B_DO, "\t\treturn ask(app, ACCESS_ASK_RESET, \"\", out, length, now);\n"),
    ("app_web_reset_asked_without_looking", T, F, RESET_MAY, "\tstatus = 0;\n\tif(status != 0) return status;\n"),
    ("app_web_reset_asked_while_busy", T, F, RESET_MAY, RESET_MAY.replace("app_busy(app)", "false")),
    ("app_web_reset_busy_by_upload_alone", T, F, RESET_MAY, RESET_MAY.replace("app_busy(app)", "app->uploading")),
    ("app_web_reset_during_dialog", T, F, RESET_MAY, RESET_MAY.replace("app_busy(app)", "(app_busy(app) && app->nav.screen != NAV_DTC_CONFIRM)")),
    ("app_web_reset_asked_during_upload", T, F, RESET_MAY, RESET_MAY.replace("app_busy(app)", "(app_busy(app) && !app->uploading)")),
    ("app_web_reset_renews_release_when_refused", T, F, RESET_MAY, "\taccess_write(&app->access, now);\n" + RESET_MAY),
    ("app_web_reset_judged_one_ms_late", T, F, RESET_MAY, RESET_MAY.replace("out, length, now);", "out, length, now + 1);")),
    ("app_web_reset_not_asked_under_update", T, F, RESET_MAY, RESET_MAY.replace("app_busy(app)", "app_busy(app) || app->update_pending")),
    ("app_web_reset_with_detail", T, F, RESET_ASK, RESET_ASK.replace("\"\"", "\"alles\"")),
    # through a copy: printing a text into its own room is undefined, and the C libraries differ (macOS keeps
    # the text, glibc empties it - there the mutant changed nothing)
    ("app_web_reset_keeps_detail", T, F, RESET_ASK,
     "\tchar kept[sizeof(app->ask_detail)];\n\n\tdrop_network(app);\n\tmemcpy(kept, app->ask_detail, sizeof(kept));\n"
     "\treturn ask(app, ACCESS_ASK_RESET, kept, out, length, now);"),
    ("app_web_reset_asked_as_firmware", T, F, RESET_ASK, RESET_ASK.replace("ACCESS_ASK_RESET", "ACCESS_ASK_FIRMWARE")),
    ("app_web_reset_asked_as_wifi", T, F, RESET_ASK, RESET_ASK.replace("ACCESS_ASK_RESET", "ACCESS_ASK_WIFI")),
    ("app_web_reset_carried_out_at_once", T, F, RESET_ASK, "\tapp->events |= APP_EVENT_FACTORY_RESET;\n" + RESET_ASK),

    # the begin of an upload
    ("app_web_upload_words_swapped", T, F, U_WORDS, "{\"\", \"too_short\", \"wrong_chip\", \"no_image\", \"no_description\", \"wrong_project\", \"too_large\"}"),
    ("app_web_upload_words_shifted", T, F, U_WORDS, "{\"too_short\", \"no_image\", \"wrong_chip\", \"no_description\", \"wrong_project\", \"too_large\", \"\"}"),
    ("app_web_upload_word_of_project_is_description", T, F, U_WORDS, "{\"\", \"too_short\", \"no_image\", \"wrong_chip\", \"no_description\", \"no_description\", \"too_large\"}"),
    ("app_web_upload_word_too_large_is_too_short", T, F, U_WORDS, "{\"\", \"too_short\", \"no_image\", \"wrong_chip\", \"no_description\", \"wrong_project\", \"too_short\"}"),
    ("app_web_upload_time_not_taken_over", T, F, U_TIME, U_TIME.replace("advance(app, now_ms)", "time_at(app, now_ms)")),
    ("app_web_upload_time_of_caller", T, F, U_TIME, U_TIME.replace("advance(app, now_ms)", "now_ms")),
    ("app_web_upload_release_not_renewed", T, F, U_LOCKED, U_LOCKED.replace("!access_write(&app->access, now) || ", "")),
    ("app_web_upload_release_only_looked_at", T, F, U_LOCKED, U_LOCKED.replace("!access_write(", "!access_is_open(")),
    ("app_web_upload_release_one_ms_late", T, F, U_LOCKED, U_LOCKED.replace("!access_write(&app->access, now)", "!access_write(&app->access, now + 1)")),
    ("app_web_upload_time_left_judged_first", T, F, U_LOCKED,
     U_LOCKED.replace(U_WRITE, "access_seconds_left(&app->access, now) < APP_WEB_UPLOAD_LEFT_S || !access_write(&app->access, now)")),
    ("app_web_upload_time_left_not_asked", T, F, U_LOCKED, U_LOCKED.replace(" || access_seconds_left(&app->access, now) < APP_WEB_UPLOAD_LEFT_S", "")),
    ("app_web_upload_needs_more_than_300_s", T, F, U_LOCKED, U_LOCKED.replace("< APP_WEB_UPLOAD_LEFT_S", "<= APP_WEB_UPLOAD_LEFT_S")),
    ("app_web_upload_time_left_of_question", T, F, U_LOCKED, U_LOCKED.replace("access_seconds_left(", "access_ask_seconds_left(")),
    ("app_web_upload_time_left_to_latest_end", T, F, U_LOCKED, U_LOCKED.replace("access_seconds_left(&app->access, now) < APP_WEB_UPLOAD_LEFT_S", "app->access.open_max_ms - now < APP_WEB_UPLOAD_LEFT_S * 1000u")),
    ("app_web_upload_short_release_is_busy", T, F, U_LOCKED,
     "\tif(!access_write(&app->access, now)) return refuse(403, \"locked\", out, length);\n\tif(access_seconds_left(&app->access, now) < APP_WEB_UPLOAD_LEFT_S) return refuse(409, \"busy\", out, length);\n"),
    ("app_web_upload_while_busy", T, F, U_BUSY, U_BUSY.replace("app_busy(app) || app->update_pending", "app->update_pending")),
    ("app_web_upload_while_update_waits", T, F, U_BUSY, U_BUSY.replace("app_busy(app) || app->update_pending", "app_busy(app)")),
    ("app_web_upload_while_update_given_up", T, F, U_BUSY, U_BUSY.replace("app->update_pending", "(app->update_pending && !app->update_given_up)")),
    ("app_web_upload_not_after_rollback", T, F, U_BUSY, U_BUSY.replace("app->update_pending", "app->update_pending || app->rolled_back")),
    ("app_web_upload_update_waits_behind_asking", T, F, U_BUSY + U_ASKING_WHY + U_ASKING,
     "\tif(app_busy(app)) return refuse(409, \"busy\", out, length);\n" + U_ASKING + "\tif(app->update_pending) return refuse(409, \"busy\", out, length);\n"),
    ("app_web_upload_update_waits_only_with_previous", T, F, U_BUSY, U_BUSY.replace("app->update_pending", "(app->update_pending && app->previous_firmware)")),
    ("app_web_upload_busy_never", T, F, U_BUSY, U_BUSY.replace("app_busy(app) || app->update_pending", "(app_busy(app) && app->update_pending)")),
    ("app_web_upload_update_waits_is_asking", T, F, U_BUSY, "\tif(app_busy(app)) return refuse(409, \"busy\", out, length);\n\tif(app->update_pending) return refuse(409, \"asking\", out, length);\n"),
    ("app_web_upload_busy_before_locked", T, F, U_LOCKED + U_BUSY_WHY + U_BUSY, U_BUSY + U_LOCKED),
    ("app_web_upload_over_running_upload", T, F, U_BUSY, U_BUSY.replace("app_busy(app)", "(app_busy(app) && !app->uploading)")),
    ("app_web_upload_busy_by_upload_alone", T, F, U_BUSY, U_BUSY.replace("app_busy(app)", "app->uploading")),
    ("app_web_upload_during_dialog", T, F, U_BUSY, U_BUSY.replace("app_busy(app)", "(app_busy(app) && app->nav.screen != NAV_DTC_CONFIRM)")),
    ("app_web_upload_busy_is_asking", T, F, U_BUSY, U_BUSY.replace("\"busy\"", "\"asking\"")),
    ("app_web_upload_while_asking", T, F, U_ASKING, ""),
    ("app_web_upload_asking_before_busy", T, F, U_BUSY + U_ASKING_WHY + U_ASKING, U_ASKING + U_BUSY),
    ("app_web_upload_asking_only_firmware", T, F, U_ASKING, U_ASKING.replace("!= ACCESS_ASK_NONE", "== ACCESS_ASK_FIRMWARE")),
    ("app_web_upload_asking_not_firmware", T, F, U_ASKING, U_ASKING.replace("access_asking(&app->access, now) != ACCESS_ASK_NONE", "(access_asking(&app->access, now) | 2) == ACCESS_ASK_RESET")),
    ("app_web_upload_asking_judged_one_ms_late", T, F, U_ASKING, U_ASKING.replace("access_asking(&app->access, now)", "access_asking(&app->access, now + 1)")),
    ("app_web_upload_asking_is_busy", T, F, U_ASKING, U_ASKING.replace("\"asking\"", "\"busy\"")),
    ("app_web_upload_file_before_asking", T, F,
     U_ASKING + U_HOT_WHY + U_HOT + U_CHECK[len("\tcheck = "):] + U_REFUSED, U_CHECK + U_REFUSED + "\n" + U_ASKING + U_HOT.replace("\n\n\tcheck = ", "\n")),
    # no upload for a question nobody could see
    ("app_web_upload_begins_on_a_dark_screen", T, F, U_HOT, "\n\tcheck = "),
    ("app_web_upload_hot_is_busy", T, F, U_HOT, U_HOT.replace("\"hot\"", "\"busy\"")),
    ("app_web_upload_hot_is_503", T, F, U_HOT, U_HOT.replace("refuse(409,", "refuse(503,")),
    ("app_web_upload_hot_before_asking", T, F, U_ASKING + U_HOT_WHY + U_HOT, U_HOT.replace("\n\n\tcheck = ", "\n") + U_ASKING + "\n\tcheck = "),
    ("app_web_upload_hot_before_busy", T, F,
     U_BUSY + U_ASKING_WHY + U_ASKING + U_HOT_WHY + U_HOT, U_HOT.replace("\n\n\tcheck = ", "\n") + U_BUSY + U_ASKING + "\n\tcheck = "),
    ("app_web_upload_hot_before_locked", T, F, U_LOCKED, "\tif(screen_unseen(app)) return refuse(409, \"hot\", out, length);\n" + U_LOCKED),
    ("app_web_upload_file_before_hot", T, F,
     U_HOT_WHY + U_HOT + U_CHECK[len("\tcheck = "):] + U_REFUSED, "\n" + U_CHECK + U_REFUSED + U_HOT.replace("\n\n\tcheck = ", "\n")),
    ("app_web_upload_hot_does_not_renew_the_release", T, F, U_LOCKED, "\tif(access_is_open(&app->access, now) && screen_unseen(app)) return refuse(409, \"hot\", out, length);\n" + U_LOCKED),
    ("app_web_upload_sizes_swapped", T, F, U_CHECK, U_CHECK.replace("file_size, slot_size", "slot_size, file_size")),
    ("app_web_upload_size_not_judged", T, F, U_CHECK, U_CHECK.replace("file_size, slot_size", "file_size - file_size + 1, slot_size")),
    ("app_web_upload_always_enough_bytes", T, F, U_CHECK, U_CHECK.replace("first != NULL ? first_length : 0", "first != NULL ? first_length - first_length + OTA_CHECK_BYTES : 0")),
    ("app_web_upload_file_not_judged", T, F, U_REFUSED, U_REFUSED.replace("check != OTA_CHECK_OK", "check != OTA_CHECK_OK && words[check][0] == '!'")),
    ("app_web_upload_other_project_taken", T, F, U_REFUSED, U_REFUSED.replace("check != OTA_CHECK_OK", "check != OTA_CHECK_OK && check != OTA_CHECK_WRONG_PROJECT")),
    ("app_web_upload_too_large_taken", T, F, U_REFUSED, U_REFUSED.replace("check != OTA_CHECK_OK", "check != OTA_CHECK_OK && check != OTA_CHECK_TOO_LARGE")),
    ("app_web_upload_other_chip_taken", T, F, U_REFUSED, U_REFUSED.replace("check != OTA_CHECK_OK", "check != OTA_CHECK_OK && check != OTA_CHECK_WRONG_CHIP")),
    ("app_web_upload_refused_is_400", T, F, U_REFUSED, U_REFUSED.replace("422", "400")),
    ("app_web_upload_refused_word_body", T, F, U_REFUSED, U_REFUSED.replace("words[check]", "words[check][0] != '!' ? \"body\" : \"\"")),
    ("app_web_upload_refused_keeps_version", T, F, U_REFUSED, "\tif(check != OTA_CHECK_OK) memcpy(app->upload_version, version, sizeof(version));\n" + U_REFUSED),
    ("app_web_upload_refused_drops_previous", T, F, U_REFUSED, U_REFUSED.replace("\tif(check != OTA_CHECK_OK) return", "\tif(check != OTA_CHECK_OK) app->previous_firmware = false;\n\tif(check != OTA_CHECK_OK) return")),
    ("app_web_upload_does_not_run", T, F, U_RUNS, "\tapp->upload_percent = 0;\n\tapp->upload_ms = now;\n"),
    ("app_web_upload_keeps_percent", T, F, U_RUNS, "\tapp->uploading = true;\n\tapp->upload_ms = now;\n"),
    ("app_web_upload_begins_at_100", T, F, U_RUNS, "\tapp->uploading = true;\n\tapp->upload_percent = 100;\n\tapp->upload_ms = now;\n"),
    ("app_web_upload_without_time", T, F, U_RUNS, "\tapp->uploading = true;\n\tapp->upload_percent = 0;\n"),
    ("app_web_upload_time_as_sent", T, F, U_RUNS, "\tapp->uploading = true;\n\tapp->upload_percent = 0;\n\tapp->upload_ms = now_ms;\n"),
    ("app_web_upload_empty_version_keeps_old", T, F, U_VERSION, "\tif(version[0] != '\\0') memcpy(app->upload_version, version, sizeof(version));\n"),
    ("app_web_upload_version_not_kept", T, F, U_VERSION, ""),
    ("app_web_upload_version_over_old_one", T, F, U_VERSION, "\tmemcpy(app->upload_version, version, strlen(version) + 1);\n"),
    ("app_web_upload_version_cut", T, F, U_VERSION, "\tmemcpy(app->upload_version, version, sizeof(version) / 2);\n\tapp->upload_version[sizeof(version) / 2] = '\\0';\n"),
    ("app_web_upload_version_is_detail_at_once", T, F, U_VERSION, U_VERSION + "\tmemcpy(app->ask_detail, version, sizeof(version));\n"),
    ("app_web_upload_keeps_previous", T, F, U_PREVIOUS, ""),
    ("app_web_upload_asks_at_once", T, F, U_PREVIOUS, U_PREVIOUS + "\taccess_ask(&app->access, ACCESS_ASK_FIRMWARE, now);\n"),
    ("app_web_upload_installs_at_once", T, F, U_PREVIOUS, U_PREVIOUS + "\tapp->events |= APP_EVENT_INSTALL_FIRMWARE;\n"),
    ("app_web_upload_body_not_emptied", T, F, U_GO, "\t*length = 0;\n\treturn 0;"),
    ("app_web_upload_length_not_told", T, F, U_GO, "\tout[0] = '\\0';\n\treturn 0;"),
    ("app_web_upload_begins_with_200", T, F, U_GO, "\treturn answer_text(OK_BODY, sizeof(OK_BODY) - 1, out, length);"),

    # the bytes of an upload
    ("app_web_progress_time_not_taken_over", T, F, P_TIME, P_TIME.replace("advance(app, now_ms)", "time_at(app, now_ms)")),
    ("app_web_progress_time_of_caller", T, F, P_TIME, P_TIME.replace("advance(app, now_ms)", "now_ms")),
    ("app_web_progress_in_32_bit", T, F, P_PERCENT, P_PERCENT.replace("(uint64_t)written * 100 / file_size", "(uint64_t)(written * 100u) / file_size")),
    ("app_web_progress_rounded_up", T, F, P_PERCENT, P_PERCENT.replace("(uint64_t)written * 100 / file_size", "((uint64_t)written * 100 + file_size - 1) / file_size")),
    ("app_web_progress_rounded", T, F, P_PERCENT, P_PERCENT.replace("(uint64_t)written * 100 / file_size", "((uint64_t)written * 100 + file_size / 2) / file_size")),
    ("app_web_progress_in_permille", T, F, P_PERCENT, P_PERCENT.replace("* 100 /", "* 1000 /")),
    ("app_web_progress_without_size_is_100", T, F, P_PERCENT, P_PERCENT.replace(": 0;", ": 100;")),
    ("app_web_progress_divides_by_no_size", T, F, P_PERCENT, P_PERCENT.replace("file_size > 0", "(file_size > 0 || written > 0)")),
    ("app_web_progress_size_one_is_none", T, F, P_PERCENT, P_PERCENT.replace("file_size > 0", "file_size > 1")),
    ("app_web_progress_of_file_left", T, F, P_PERCENT, P_PERCENT.replace("(uint64_t)written * 100 / file_size", "(uint64_t)(written < file_size ? file_size - written : 0) * 100 / file_size")),
    ("app_web_progress_brings_upload_back", T, F, P_RUNS, ""),
    ("app_web_progress_starts_upload", T, F, P_RUNS, "\tapp->uploading = true;\n"),
    ("app_web_progress_ignored_while_running", T, F, P_RUNS, "\tif(app->uploading) return;\n"),
    ("app_web_progress_above_100", T, F, P_SET, P_SET.replace("percent > 100 ? 100 : (int)percent", "(int)percent")),
    ("app_web_progress_stays_at_100", T, F, P_SET, P_SET.replace("\tapp->upload_percent = ", "\tif(app->upload_percent < 100) app->upload_percent = ")),
    ("app_web_progress_stops_at_101", T, F, P_SET, P_SET.replace("percent > 100 ? 100 : (int)percent", "percent > 101 ? 100 : (int)percent")),
    ("app_web_progress_stops_at_99", T, F, P_SET, P_SET.replace("percent > 100 ? 100 : (int)percent", "percent > 99 ? 99 : (int)percent")),
    ("app_web_progress_percent_not_kept", T, F, P_SET, "\tapp->upload_ms = percent > 100 ? now : now;"),
    ("app_web_progress_percent_only_grows", T, F, P_SET, P_SET.replace("\tapp->upload_percent = ", "\tif((int)percent > app->upload_percent) app->upload_percent = ")),
    ("app_web_progress_time_not_renewed", T, F, P_SET, "\tapp->upload_percent = percent > 100 ? 100 : (int)percent;\n\t(void)now;"),
    ("app_web_progress_time_renewed_as_sent", T, F, P_SET, P_SET.replace("app->upload_ms = now;", "app->upload_ms = now > 0 ? now_ms : now_ms;")),
    ("app_web_progress_time_only_with_new_percent", T, F, P_SET,
     "\tif(app->upload_percent != (percent > 100 ? 100 : (int)percent)) app->upload_ms = now;\n\tapp->upload_percent = percent > 100 ? 100 : (int)percent;"),
    ("app_web_progress_time_only_with_more_bytes", T, F, P_SET, P_SET.replace("\tapp->upload_ms = now;", "\tif((int)percent >= app->upload_percent) app->upload_ms = now;").replace(
        "\tapp->upload_percent = percent > 100 ? 100 : (int)percent;\n\tif(", "\tif(") + "\n\tapp->upload_percent = percent > 100 ? 100 : (int)percent;"),
    ("app_web_progress_renews_release", T, F, P_SET, P_SET + "\n\taccess_write(&app->access, now);"),

    # the end of an upload
    ("app_web_end_time_not_taken_over", T, F, E_TIME, E_TIME.replace("advance(app, now_ms)", "time_at(app, now_ms)")),
    ("app_web_end_time_of_caller", T, F, E_TIME, E_TIME.replace("advance(app, now_ms)", "now_ms")),
    ("app_web_end_leaves_upload_running", T, F, E_OVER, "\t// An upload the display has ended by itself"),
    ("app_web_end_broken_upload_runs_on", T, F, E_OVER, "\tif(ok) app->uploading = false;\n\t// An upload the display has ended by itself"),
    ("app_web_end_complete_upload_runs_on", T, F, E_OVER, "\tif(!ok) app->uploading = false;\n\t// An upload the display has ended by itself"),
    ("app_web_end_asks_for_ended_upload", T, F, E_BROKEN, "\tif(!ok || (!running && false)) return refuse(500, \"upload\", out, length);\n"),
    ("app_web_end_asks_for_broken_upload", T, F, E_BROKEN, "\tif(!running || (!ok && false)) return refuse(500, \"upload\", out, length);\n"),
    ("app_web_end_asks_for_broken_upload_at_100", T, F, E_BROKEN, "\tif(!running || (!ok && app->upload_percent < 100)) return refuse(500, \"upload\", out, length);\n"),
    ("app_web_end_asks_only_at_100", T, F, E_BROKEN, "\tif(!ok || !running || app->upload_percent < 100) return refuse(500, \"upload\", out, length);\n"),
    ("app_web_end_asks_always", T, F, E_BROKEN, "\tif((!ok || !running) && false) return refuse(500, \"upload\", out, length);\n"),
    ("app_web_end_broken_is_400", T, F, E_BROKEN, E_BROKEN.replace("500", "400")),
    ("app_web_end_broken_other_word", T, F, E_BROKEN, E_BROKEN.replace("\"upload\"", "\"broken\"")),
    ("app_web_end_broken_closes_release", T, F, E_BROKEN, "\tif(!ok) access_close(&app->access, now);\n" + E_BROKEN),
    ("app_web_end_broken_raises_restart", T, F, E_BROKEN, "\tif(!ok && running) app->events |= APP_EVENT_REBOOT;\n" + E_BROKEN),
    ("app_web_end_broken_drops_question", T, F, E_BROKEN, "\tif(!ok || !running) access_refuse(&app->access, now);\n" + E_BROKEN),
    ("app_web_end_asked_without_looking", T, F, E_MAY, "\tstatus = 0;\n\tif(status != 0) return status;\n"),
    ("app_web_end_refused_while_busy", T, F, E_MAY, E_MAY.replace("false", "app_busy(app)")),
    ("app_web_end_release_given_again", T, F, E_MAY, "\taccess_open(&app->access, now);\n" + E_MAY),
    ("app_web_end_replaces_question", T, F, E_MAY, "\taccess_refuse(&app->access, now);\n" + E_MAY),
    ("app_web_end_judged_one_ms_late", T, F, E_MAY, E_MAY.replace("out, length, now);", "out, length, now + 1);")),
    ("app_web_end_without_version", T, F, E_ASK, E_ASK.replace("app->upload_version", "\"\"")),
    ("app_web_end_version_of_running_firmware", T, F, E_ASK, E_ASK.replace("app->upload_version", "app->version")),
    ("app_web_end_keeps_detail", T, F, E_ASK, E_ASK.replace("app->upload_version", "app->ask_detail")),
    ("app_web_end_asked_as_reset", T, F, E_ASK, E_ASK.replace("ACCESS_ASK_FIRMWARE", "ACCESS_ASK_RESET")),
    ("app_web_end_asked_as_wifi", T, F, E_ASK, E_ASK.replace("ACCESS_ASK_FIRMWARE", "ACCESS_ASK_WIFI")),
    ("app_web_end_installs_at_once", T, F, E_ASK, "\tapp->events |= APP_EVENT_INSTALL_FIRMWARE;\n" + E_ASK),
    ("app_web_end_installs_without_question", T, F, E_ASK, "\tapp->events |= APP_EVENT_INSTALL_FIRMWARE;\n\treturn answer_text(OK_BODY, sizeof(OK_BODY) - 1, out, length);"),
    ("app_web_end_previous_firmware_back", T, F, E_ASK, "\tapp->previous_firmware = true;\n" + E_ASK),
    ("app_web_end_broken_previous_firmware_back", T, F, E_BROKEN, E_BROKEN.replace("\tif(!ok || !running) return", "\tif(!ok) app->previous_firmware = true;\n\tif(!ok || !running) return")),
]
