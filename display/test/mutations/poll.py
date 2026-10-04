"""Mutations of display/components/core/poll.c, see ../redproof.py."""

F = "components/core/poll.c"
T = "test_poll"

FOLLOW_LIST = "\tbool list = poll->has_list && (phase == DTC_FLOW_LIST || phase == DTC_FLOW_CLEAR_SENT || phase == DTC_FLOW_CLEARING);"
FOLLOW_CLEARED = "\tbool cleared = poll->has_cleared && phase == DTC_FLOW_CLEARED;"
FOLLOW_EVENT = "\tif(list != poll->has_list || cleared != poll->has_cleared) poll->events |= POLL_EVENT_LISTS;\n"
WATCH_OUT = "\tbool out = view == CONN_VIEW_NO_WIFI || view == CONN_VIEW_NO_ANSWER;"
WATCH_LOSE = "\tif(out && !poll->lost) dtc_flow_lost(&poll->flow);\n"
WATCH_NOTE = "\tpoll->lost = out;\n"

STORED_CATALOG = "\tif(catalog_json != NULL && catalog_from_json(&poll->catalog, catalog_json, catalog_length, work, work_count))"
STORED_GUARD = "\t\tguard_catalog_init(&poll->catalog_guard, true, catalog_checksum(&poll->catalog));\n"
STORED_OLD = "\tif(old_text != NULL && old_length < sizeof(poll->old_text) && dtc_result_parse(old_text, old_length, &poll->old, work, work_count))"
STORED_COPY = "\t\tmemcpy(poll->old_text, old_text, old_length);\n"
STORED_END = "\t\tpoll->old_text[old_length] = '\\0';\n\t\tpoll->has_old = true;\n"
STORED_EVENT = "\t\tpoll->has_old = true;\n\t\tpoll->events |= POLL_EVENT_LISTS;\n"

WIFI_SAME = "\tif(up == poll->wifi) return;\n"
WIFI_END = "\tpoll->wifi = up;\n\tpoll->asking = false;\n"
WIFI_CONN = "\tconn_wifi(&poll->conn, up, now_ms);\n"
WIFI_JOINED = "\t\tvalues_clear(&poll->values);\n\t\tpoll->catalog_complete = false;\n\t\tguard_catalog_connected(&poll->catalog_guard);\n"
WIFI_AFTER = "\twatch(poll, now_ms);\n\tfollow(poll);\n}\n"

P_FREE = "\tif(poll->wifi && !poll->asking)"
P_TAKE = "\t\tdtc_flow_send_t send = dtc_flow_take(&poll->flow, &seq, now_ms);\n"
P_KINDS = ("\t\tif(send == DTC_FLOW_SEND_READ) kind = POLL_DTC_READ;\n"
           "\t\telse if(send == DTC_FLOW_SEND_CLEAR) kind = POLL_DTC_CLEAR;\n"
           "\t\telse kind = KINDS[conn_next(&poll->conn, now_ms)];\n")
P_POST = "\trequest->post = kind == POLL_DTC_READ || kind == POLL_DTC_CLEAR;"
P_CLEAR_PATH = "\tif(kind == POLL_DTC_CLEAR) snprintf(request->path, sizeof(request->path), \"/api/dtc?action=clear&seq=%\" PRIu32, seq);"
P_PATH = "\telse strcpy(request->path, PATHS[kind]);"
P_NONE = "\tif(kind == POLL_NONE) return false;\n"
P_OLD_IF = "\tif(kind == POLL_DTC_CLEAR)\n\t{\n\t\t// What is about"
P_OLD = "\t\tpoll->old = poll->list;\n"
P_OLD_TEXT = "\t\tstrcpy(poll->old_text, poll->list_text);\n"
P_OLD_FLAG = "\t\tpoll->has_old = true;\n\t\tpoll->events |= POLL_EVENT_OLD | POLL_EVENT_LISTS;\n"
P_OLD_EVENT = "\t\tpoll->events |= POLL_EVENT_OLD | POLL_EVENT_LISTS;\n"
P_RESULT_SEQ = "\t\tpoll->asked_result_seq = poll->conn.state.dtc.result_seq;\n"
P_RESULT_AGE = "\t\tpoll->asked_age_s = poll->conn.state.dtc.age_s;\n"
P_ASKING = "\tpoll->asking = true;\n"
P_ASKED = "\tpoll->asked = kind;\n"

FORGET_VALUES = "{\n\tvalues_clear(&poll->values);\n"
FORGET_COMPLETE = "\tpoll->catalog_complete = false;\n\tguard_catalog_connected(&poll->catalog_guard);\n\tpoll->events"
FORGET_EVENT = "\tpoll->events |= POLL_EVENT_FORGET | POLL_EVENT_LISTS;\n"
FORGET_LOST = "\tdtc_flow_lost(&poll->flow);\n\tif(poll->flow.phase == DTC_FLOW_LIST"
FORGET_DISMISS = "\tif(poll->flow.phase == DTC_FLOW_LIST || poll->flow.phase == DTC_FLOW_CLEARED) dtc_flow_dismiss(&poll->flow);\n"

BATTERY = "\tsnprintf(json, sizeof(json), \"{\\\"\" CATALOG_BATTERY \"\\\":%\" PRId32 \".%03\" PRId32 \"}\", millivolts / 1000, millivolts % 1000);"
BATTERY_APPLY = "\tvalues_apply(&poll->values, json, strlen(json), -1, now_ms, work, work_count);"

S_READ = "\tbool read = status == 200 && wican_state_parse(body, length, &state, work, work_count);"
S_FLOW = "\t\tdtc_flow_state(&poll->flow, &state, now_ms);\n"
S_GOT = "\t\tconn_got_state(&poll->conn, status == 404 ? CONN_GOT_NOT_FOUND : CONN_GOT_FAILED, NULL, now_ms);"
S_BIND = "\tif(conn_take_bind(&poll->conn, poll->bound_id, sizeof(poll->bound_id))) poll->events |= POLL_EVENT_BOUND;\n"
S_RESTART = "\tif(conn_take_restarted(&poll->conn)) forget(poll);\n"
S_BATTERY = "\tif(read && !poll->conn.foreign && state.batt_mv >= 0) battery(poll, state.batt_mv, now_ms, work, work_count);\n"
S_AFTER = (S_RESTART + "\t// Behind the forgetting: this voltage is one of the adapter that answers now. That of a foreign adapter\n"
           "\t// is no value of the vehicle the display belongs to.\n" + S_BATTERY)

R_ROOM = "\tdtc_result_t *room = poll->has_list ? &poll->cleared : &poll->list;"
R_OWN = "\tbool own = waits(&poll->flow) && poll->flow.seq == poll->asked_result_seq;"
R_NONE = "\tif(status == 0)\n\t{\n\t\tconn_got_result(&poll->conn, CONN_GOT_FAILED, now_ms);\n\t\treturn;\n\t}\n"
R_TAKEN = ("\tif(status == 200 && seq_header != NULL && strcmp(seq_header, number) == 0 && length < sizeof(poll->list_text) &&\n"
           "\t   dtc_result_parse(body, length, room, work, work_count))")
R_OK = "\t\tconn_got_result(&poll->conn, CONN_GOT_OK, now_ms);\n"
R_FLOW = "\t\tdtc_flow_result(&poll->flow, poll->asked_result_seq, room->clear, room->dtc_count, poll->asked_age_s, now_ms);\n"
R_CHANGED = "\t\tif(poll->flow.phase != before)"
R_TEXT = "\t\t\t\tmemcpy(poll->list_text, body, length);\n"
R_TEXT_END = "\t\t\t\tpoll->list_text[length] = '\\0';\n"
R_LIST = "\t\t\t\tpoll->has_list = true;\n"
R_CLEARED = "\t\t\t\tpoll->has_cleared = true;\n"
R_EVENT = "\t\t\tpoll->events |= POLL_EVENT_LISTS;\n"
R_GONE = "\t\tconn_got_result(&poll->conn, CONN_GOT_NOT_FOUND, now_ms);\n"
R_LOST = "\tif(own && waits(&poll->flow)) dtc_flow_lost(&poll->flow);\n"

C_200 = "\tif(status == 200)\n\t{\n\t\t// A body that cannot be used"
C_APPLY = "\t\tif(catalog_apply_config(&poll->catalog, body, length, work, work_count)) poll->catalog_complete = true;\n\t\tgot = CONN_GOT_OK;\n"
C_ABSENT = "\telse if(status == 404 || (status != 0 && state != NULL && state->autopid == WICAN_AUTOPID_OFF))"
C_NOT_FOUND = "\t\tgot = CONN_GOT_NOT_FOUND;\n"
C_GOT = "\tconn_got_catalog(&poll->conn, got, now_ms);\n"

V_APPLY = "\tif(status == 200) result = values_apply(&poll->values, body, length, state != NULL ? (int64_t)state->pass : -1, now_ms, work, work_count);"
V_NOTE = "\tif(result == VALUES_RENEWED) catalog_note_values(&poll->catalog, &poll->values);\n"
V_GOT = "\tconn_got_values(&poll->conn, result == VALUES_INVALID ? CONN_GOT_FAILED : CONN_GOT_OK, now_ms);"

D_PARSE = "\tif(json_parse(body, length, work, work_count) > 0)"
D_SEQ = "\t\tif(number < 0 || !json_integer(body, &work[number], &seq) || seq < 0 || seq > UINT32_MAX) seq = 0;"
D_REASON = "\t\tif(text >= 0) json_text(body, &work[text], reason, sizeof(reason));\n"
D_HTTP = "\tif(reason[0] == '\\0' && status != 202) snprintf(reason, sizeof(reason), \"http_%d\", status);\n"
D_POSTED = "\tdtc_flow_posted(&poll->flow, status, (uint32_t)seq, reason, now_ms);"

A_MATCH = "\tif(!poll->asking || request == NULL || request->kind != poll->asked) return;"
A_END = "\tpoll->asking = false;\n\t// What an HTTP client"
A_NEGATIVE = "\tif(status < 0) status = 0;\n"
A_BODY = "\tif(body == NULL)\n\t{\n\t\tbody = \"\";\n\t\tlength = 0;\n\t}\n"
A_KINDS = ("\telse if(poll->asked == POLL_CATALOG) got_catalog(poll, status, body, length, now_ms, work, work_count);\n"
           "\telse if(poll->asked == POLL_VALUES) got_values(poll, status, body, length, now_ms, work, work_count);\n")
A_POSTED = "\telse got_posted(poll, status, body, length, now_ms, work, work_count);"
A_AFTER = "\twatch(poll, now_ms);\n\tfollow(poll);\n\tif(guard_catalog_due"
A_GUARD = ("\tif(guard_catalog_due(&poll->catalog_guard, catalog_checksum(&poll->catalog), poll->catalog_complete, now_ms))\n"
           "\t{\n\t\tpoll->events |= POLL_EVENT_CATALOG;\n\t}\n")
A_COUNT = "\tif(status >= 200 && status <= 499) poll->http_ok++;\n\telse poll->http_failed++;\n"

READ = "\tdtc_flow_block_t block = dtc_flow_read(&poll->flow, &poll->conn, &poll->values, &poll->catalog, now_ms);"
READ_FOLLOW = "\t// A read drops the list that was shown\n\tfollow(poll);\n"
CLEAR = "\treturn dtc_flow_clear(&poll->flow, &poll->conn, &poll->values, &poll->catalog, button_stuck, now_ms);"
DISMISS = "\tdtc_flow_dismiss(&poll->flow);\n\tfollow(poll);\n"

MUTATIONS = [
    # which request, and where it goes
    ("poll_result_asked_as_state", T, F, "\t[CONN_ASK_RESULT] = POLL_RESULT,", "\t[CONN_ASK_RESULT] = POLL_STATE,"),
    ("poll_catalog_asked_as_values", T, F, "\t[CONN_ASK_CATALOG] = POLL_CATALOG,", "\t[CONN_ASK_CATALOG] = POLL_VALUES,"),
    ("poll_values_asked_as_catalog", T, F, "\t[CONN_ASK_VALUES] = POLL_VALUES,", "\t[CONN_ASK_VALUES] = POLL_CATALOG,"),
    ("poll_state_asked_as_nothing", T, F, "\t[CONN_ASK_STATE] = POLL_STATE,", "\t[CONN_ASK_STATE] = POLL_NONE,"),
    ("poll_path_state", T, F, "\t[POLL_STATE] = \"/api/state\",", "\t[POLL_STATE] = \"/api/state/\","),
    ("poll_path_result", T, F, "\t[POLL_RESULT] = \"/api/dtc/result\",", "\t[POLL_RESULT] = \"/api/dtc\","),
    ("poll_path_catalog", T, F, "\t[POLL_CATALOG] = \"/load_car_config\",", "\t[POLL_CATALOG] = \"/load_config\","),
    ("poll_path_values", T, F, "\t[POLL_VALUES] = \"/autopid_data\",", "\t[POLL_VALUES] = \"/autopid_data?\","),
    ("poll_path_read_without_action", T, F, "\t[POLL_DTC_READ] = \"/api/dtc?action=read\",", "\t[POLL_DTC_READ] = \"/api/dtc\","),
    ("poll_path_read_clears", T, F, "\t[POLL_DTC_READ] = \"/api/dtc?action=read\",", "\t[POLL_DTC_READ] = \"/api/dtc?action=clear\","),
    ("poll_path_none_not_empty", T, F, "\t[POLL_NONE] = \"\",", "\t[POLL_NONE] = \"/api/state\","),

    # what is shown follows the flow
    ("poll_list_dropped_while_clear_runs", T, F, FOLLOW_LIST,
     "\tbool list = poll->has_list && (phase == DTC_FLOW_LIST || phase == DTC_FLOW_CLEAR_SENT);"),
    ("poll_list_dropped_while_clear_waits", T, F, FOLLOW_LIST,
     "\tbool list = poll->has_list && (phase == DTC_FLOW_LIST || phase == DTC_FLOW_CLEARING);"),
    ("poll_list_dropped_while_shown", T, F, FOLLOW_LIST,
     "\tbool list = poll->has_list && (phase == DTC_FLOW_CLEAR_SENT || phase == DTC_FLOW_CLEARING);"),
    ("poll_list_kept_when_failed", T, F, FOLLOW_LIST, FOLLOW_LIST.replace("DTC_FLOW_CLEARING)", "DTC_FLOW_CLEARING || phase == DTC_FLOW_FAILED)")),
    ("poll_list_kept_when_unknown", T, F, FOLLOW_LIST, FOLLOW_LIST.replace("DTC_FLOW_CLEARING)", "DTC_FLOW_CLEARING || phase == DTC_FLOW_UNKNOWN)")),
    ("poll_list_kept_when_idle", T, F, FOLLOW_LIST, FOLLOW_LIST.replace("DTC_FLOW_CLEARING)", "DTC_FLOW_CLEARING || phase == DTC_FLOW_IDLE)")),
    ("poll_list_kept_when_read_again", T, F, FOLLOW_LIST, FOLLOW_LIST.replace("DTC_FLOW_CLEARING)", "DTC_FLOW_CLEARING || phase == DTC_FLOW_READ_SENT)")),
    ("poll_list_kept_when_cleared", T, F, FOLLOW_LIST, FOLLOW_LIST.replace("DTC_FLOW_CLEARING)", "DTC_FLOW_CLEARING || phase == DTC_FLOW_CLEARED)")),
    ("poll_list_never_dropped", T, F, FOLLOW_LIST, "\tbool list = poll->has_list;"),
    ("poll_cleared_never_dropped", T, F, FOLLOW_CLEARED, "\tbool cleared = poll->has_cleared;"),
    ("poll_cleared_kept_until_idle", T, F, FOLLOW_CLEARED, "\tbool cleared = poll->has_cleared && phase != DTC_FLOW_IDLE;"),
    ("poll_cleared_kept_when_read_again", T, F, FOLLOW_CLEARED,
     "\tbool cleared = poll->has_cleared && (phase == DTC_FLOW_CLEARED || phase == DTC_FLOW_READ_SENT);"),
    ("poll_cleared_always_dropped", T, F, FOLLOW_CLEARED, "\tbool cleared = false;"),
    ("poll_follow_without_event", T, F, FOLLOW_EVENT, ""),
    ("poll_follow_event_only_for_list", T, F, FOLLOW_EVENT, "\tif(list != poll->has_list) poll->events |= POLL_EVENT_LISTS;\n"),
    ("poll_follow_event_only_for_cleared", T, F, FOLLOW_EVENT, "\tif(cleared != poll->has_cleared) poll->events |= POLL_EVENT_LISTS;\n"),
    ("poll_follow_event_always", T, F, FOLLOW_EVENT, "\tpoll->events |= POLL_EVENT_LISTS;\n"),
    ("poll_follow_event_needs_both", T, F, FOLLOW_EVENT, FOLLOW_EVENT.replace("||", "&&")),
    ("poll_follow_event_replaces_others", T, F, FOLLOW_EVENT, FOLLOW_EVENT.replace("poll->events |= POLL_EVENT_LISTS", "poll->events = POLL_EVENT_LISTS")),
    ("poll_follow_event_is_forget", T, F, FOLLOW_EVENT, FOLLOW_EVENT.replace("POLL_EVENT_LISTS", "POLL_EVENT_FORGET")),
    ("poll_follow_list_not_noted", T, F, "\tpoll->has_list = list;\n", ""),
    ("poll_follow_cleared_not_noted", T, F, "\tpoll->has_cleared = cleared;\n", ""),

    # an adapter that cannot be reached
    ("poll_outage_without_wifi_ignored", T, F, WATCH_OUT, "\tbool out = view == CONN_VIEW_NO_ANSWER;"),
    ("poll_outage_without_answer_ignored", T, F, WATCH_OUT, "\tbool out = view == CONN_VIEW_NO_WIFI;"),
    ("poll_outage_also_connecting", T, F, WATCH_OUT, "\tbool out = view == CONN_VIEW_NO_WIFI || view == CONN_VIEW_NO_ANSWER || view == CONN_VIEW_CONNECTING;"),
    ("poll_outage_also_foreign", T, F, WATCH_OUT, "\tbool out = view == CONN_VIEW_NO_WIFI || view == CONN_VIEW_NO_ANSWER || view == CONN_VIEW_FOREIGN;"),
    ("poll_outage_seen_one_ms_early", T, F, "\tconn_view_t view = conn_view(&poll->conn, now_ms);", "\tconn_view_t view = conn_view(&poll->conn, now_ms + 1);"),
    ("poll_outage_seen_one_ms_late", T, F, "\tconn_view_t view = conn_view(&poll->conn, now_ms);", "\tconn_view_t view = conn_view(&poll->conn, now_ms > 0 ? now_ms - 1 : 0);"),
    ("poll_outage_loses_nothing", T, F, WATCH_LOSE, ""),
    ("poll_outage_not_noted", T, F, WATCH_NOTE, ""),
    ("poll_outage_never_ends", T, F, WATCH_NOTE, "\tif(out) poll->lost = true;\n"),
    ("poll_outage_noted_inverted", T, F, WATCH_NOTE, "\tpoll->lost = !out;\n"),

    # init
    ("poll_init_keeps_memory", T, F, "\tmemset(poll, 0, sizeof(*poll));\n", ""),
    ("poll_init_without_conn", T, F, "\tconn_init(&poll->conn, bound_id);\n", "\t(void)bound_id;\n"),
    ("poll_init_conn_unbound", T, F, "\tconn_init(&poll->conn, bound_id);\n", "\tconn_init(&poll->conn, NULL);\n\t(void)bound_id;\n"),
    ("poll_init_id_not_copied", T, F, "\tstrcpy(poll->bound_id, poll->conn.bound_id);\n", ""),
    ("poll_init_id_not_cut", T, F, "\tstrcpy(poll->bound_id, poll->conn.bound_id);\n", "\tif(bound_id != NULL) strcpy(poll->bound_id, bound_id);\n"),
    ("poll_init_without_catalog", T, F, "\tcatalog_init(&poll->catalog);\n", ""),

    # what was stored
    ("poll_stored_catalog_ignored", T, F, STORED_CATALOG, STORED_CATALOG.replace("catalog_json != NULL &&", "catalog_json == NULL &&")),
    ("poll_stored_catalog_behind_length", T, F, STORED_CATALOG, STORED_CATALOG.replace("catalog_length,", "catalog_length + 1,")),
    ("poll_stored_catalog_length_ignored", T, F, STORED_CATALOG, STORED_CATALOG.replace("catalog_length,", "catalog_length + strlen(catalog_json + catalog_length),")),
    ("poll_stored_catalog_tokens_behind_room", T, F, STORED_CATALOG, STORED_CATALOG.replace("work, work_count))", "work, work_count + 1))")),
    ("poll_stored_guard_not_told", T, F, STORED_GUARD, ""),
    ("poll_stored_guard_told_none", T, F, STORED_GUARD, STORED_GUARD.replace("true,", "false,")),
    ("poll_stored_guard_wrong_sum", T, F, STORED_GUARD, STORED_GUARD.replace("catalog_checksum(&poll->catalog)", "0")),
    ("poll_stored_guard_told_always", T, F, STORED_CATALOG + "\n\t{\n" + STORED_GUARD + "\t}\n",
     "\tif(catalog_json != NULL) catalog_from_json(&poll->catalog, catalog_json, catalog_length, work, work_count);\n" + STORED_GUARD.replace("\t\t", "\t")),
    ("poll_stored_old_ignored", T, F, STORED_OLD, STORED_OLD.replace("old_text != NULL &&", "old_text == NULL &&")),
    ("poll_stored_old_without_room_taken", T, F, STORED_OLD, STORED_OLD.replace("old_length < sizeof(poll->old_text) && ", "")),
    ("poll_stored_old_one_byte_too_long", T, F, STORED_OLD, STORED_OLD.replace("old_length < sizeof", "old_length <= sizeof")),
    ("poll_stored_old_one_byte_too_short", T, F, STORED_OLD, STORED_OLD.replace("old_length < sizeof(poll->old_text)", "old_length < sizeof(poll->old_text) - 1")),
    ("poll_stored_old_only_long", T, F, STORED_OLD, STORED_OLD.replace("old_length < sizeof", "old_length > 100 && old_length < sizeof")),
    ("poll_stored_old_not_checked", T, F, STORED_OLD, "\tif(old_text != NULL && old_length < sizeof(poll->old_text))"),
    ("poll_stored_old_behind_length", T, F, STORED_OLD, STORED_OLD.replace("old_text, old_length, &poll->old", "old_text, old_length + 1, &poll->old")),
    ("poll_stored_old_tokens_behind_room", T, F, STORED_OLD, STORED_OLD.replace("work, work_count))", "work, work_count + 1))")),
    ("poll_stored_old_text_not_copied", T, F, STORED_COPY, ""),
    ("poll_stored_old_text_one_byte_short", T, F, STORED_COPY, "\t\tmemcpy(poll->old_text, old_text, old_length - 1);\n"),
    ("poll_stored_old_text_not_ended", T, F, STORED_END, "\t\tpoll->has_old = true;\n"),
    ("poll_stored_old_not_flagged", T, F, STORED_END, "\t\tpoll->old_text[old_length] = '\\0';\n"),
    ("poll_stored_old_without_event", T, F, STORED_EVENT, "\t\tpoll->has_old = true;\n"),
    ("poll_stored_old_event_replaces_others", T, F, STORED_EVENT, "\t\tpoll->has_old = true;\n\t\tpoll->events = POLL_EVENT_LISTS;\n"),
    ("poll_stored_old_event_is_old", T, F, STORED_EVENT, "\t\tpoll->has_old = true;\n\t\tpoll->events |= POLL_EVENT_OLD;\n"),

    # the network
    ("poll_wifi_same_value_acts", T, F, WIFI_SAME, ""),
    ("poll_wifi_not_noted", T, F, WIFI_END, "\tpoll->asking = false;\n"),
    ("poll_wifi_request_stays_under_way", T, F, WIFI_END, "\tpoll->wifi = up;\n"),
    ("poll_wifi_conn_not_told", T, F, WIFI_CONN, ""),
    ("poll_wifi_conn_told_at_time_0", T, F, WIFI_CONN, "\tconn_wifi(&poll->conn, up, 0);\n"),
    ("poll_wifi_values_kept", T, F, WIFI_JOINED, WIFI_JOINED.replace("\t\tvalues_clear(&poll->values);\n", "")),
    ("poll_wifi_dropped_count_forgotten", T, F, WIFI_JOINED, WIFI_JOINED.replace("values_clear(&poll->values)", "values_init(&poll->values)")),
    ("poll_wifi_guard_forgets_what_is_stored", T, F, WIFI_JOINED,
     WIFI_JOINED.replace("guard_catalog_connected(&poll->catalog_guard)", "guard_catalog_init(&poll->catalog_guard, false, 0)")),
    ("poll_wifi_catalog_stays_complete", T, F, WIFI_JOINED, WIFI_JOINED.replace("\t\tpoll->catalog_complete = false;\n", "")),
    ("poll_wifi_guard_not_told", T, F, WIFI_JOINED, WIFI_JOINED.replace("\t\tguard_catalog_connected(&poll->catalog_guard);\n", "")),
    ("poll_wifi_forgets_when_lost", T, F, "\tif(up)\n\t{\n\t\t// The adapter may have restarted", "\tif(!up)\n\t{\n\t\t// The adapter may have restarted"),
    ("poll_wifi_forgets_always", T, F, "\tif(up)\n\t{\n\t\t// The adapter may have restarted", "\t{\n\t\t// The adapter may have restarted"),
    ("poll_wifi_flow_not_lost", T, F, WIFI_AFTER, "\tfollow(poll);\n}\n"),
    ("poll_wifi_lists_do_not_follow", T, F, WIFI_AFTER, "\twatch(poll, now_ms);\n}\n"),
    ("poll_wifi_follow_before_lost", T, F, WIFI_AFTER, "\tfollow(poll);\n\twatch(poll, now_ms);\n}\n"),

    # the next request
    ("poll_prepare_while_request_under_way", T, F, P_FREE, "\tif(poll->wifi)"),
    ("poll_prepare_only_while_request_under_way", T, F, P_FREE, "\tif(poll->wifi && poll->asking)"),
    ("poll_prepare_flow_never_asked", T, F, P_TAKE, "\t\tdtc_flow_send_t send = DTC_FLOW_SEND_NOTHING;\n"),
    ("poll_prepare_flow_asked_at_time_0", T, F, P_TAKE, "\t\tdtc_flow_send_t send = dtc_flow_take(&poll->flow, &seq, 0);\n"),
    ("poll_prepare_round_before_flow", T, F, P_TAKE + "\n" + P_KINDS,
     "\t\tkind = KINDS[conn_next(&poll->conn, now_ms)];\n\t\tif(kind == POLL_NONE)\n\t\t{\n\t" + P_TAKE + "\n"
     "\t\t\tif(send == DTC_FLOW_SEND_READ) kind = POLL_DTC_READ;\n\t\t\telse if(send == DTC_FLOW_SEND_CLEAR) kind = POLL_DTC_CLEAR;\n\t\t}\n"),
    ("poll_prepare_round_request_lost_behind_post", T, F, "\t\telse kind = KINDS[conn_next(&poll->conn, now_ms)];\n",
     "\t\telse kind = KINDS[conn_next(&poll->conn, now_ms)];\n\t\tif(send != DTC_FLOW_SEND_NOTHING) conn_next(&poll->conn, now_ms);\n"),
    ("poll_prepare_flow_asked_without_number", T, F, P_TAKE, P_TAKE.replace("&seq, now_ms", "NULL, now_ms")),
    ("poll_prepare_read_as_clear", T, F, "\t\tif(send == DTC_FLOW_SEND_READ) kind = POLL_DTC_READ;", "\t\tif(send == DTC_FLOW_SEND_READ) kind = POLL_DTC_CLEAR;"),
    ("poll_prepare_clear_as_read", T, F, "\t\telse if(send == DTC_FLOW_SEND_CLEAR) kind = POLL_DTC_CLEAR;", "\t\telse if(send == DTC_FLOW_SEND_CLEAR) kind = POLL_DTC_READ;"),
    ("poll_prepare_round_one_ms_early", T, F, "\t\telse kind = KINDS[conn_next(&poll->conn, now_ms)];", "\t\telse kind = KINDS[conn_next(&poll->conn, now_ms + 1)];"),
    ("poll_prepare_flow_asked_one_ms_late", T, F, P_TAKE, P_TAKE.replace("&seq, now_ms", "&seq, now_ms + 1")),
    ("poll_prepare_flow_asked_one_ms_early", T, F, P_TAKE, P_TAKE.replace("&seq, now_ms", "&seq, now_ms > 0 ? now_ms - 1 : 0")),
    ("poll_prepare_round_at_time_0", T, F, "\t\telse kind = KINDS[conn_next(&poll->conn, now_ms)];", "\t\telse kind = KINDS[conn_next(&poll->conn, 0)];"),
    ("poll_prepare_kind_not_written", T, F, "\trequest->kind = kind;\n", ""),
    ("poll_prepare_read_is_get", T, F, P_POST, "\trequest->post = kind == POLL_DTC_CLEAR;"),
    ("poll_prepare_clear_is_get", T, F, P_POST, "\trequest->post = kind == POLL_DTC_READ;"),
    ("poll_prepare_all_post", T, F, P_POST, "\trequest->post = kind != POLL_NONE;"),
    ("poll_prepare_post_not_written", T, F, P_POST + "\n", ""),
    ("poll_prepare_clear_without_number", T, F, P_CLEAR_PATH,
     "\tif(kind == POLL_DTC_CLEAR) snprintf(request->path, sizeof(request->path), \"/api/dtc?action=clear\");"),
    ("poll_prepare_clear_number_16_bit", T, F, P_CLEAR_PATH, P_CLEAR_PATH.replace("PRIu32, seq);", "PRIu32, seq & 0xFFFFu);")),
    ("poll_prepare_clear_number_31_bit", T, F, P_CLEAR_PATH, P_CLEAR_PATH.replace("PRIu32, seq);", "PRIu32, seq & 0x7FFFFFFFu);")),
    ("poll_prepare_clear_number_one_more", T, F, P_CLEAR_PATH, P_CLEAR_PATH.replace("PRIu32, seq);", "PRIu32, seq + 1);")),
    ("poll_prepare_clear_number_of_next_request", T, F, P_CLEAR_PATH, P_CLEAR_PATH.replace("PRIu32, seq);", "PRIu32, poll->flow.seq_before + 1);")),
    ("poll_prepare_clear_path_cut", T, F, P_CLEAR_PATH, P_CLEAR_PATH.replace("sizeof(request->path),", "sizeof(request->path) - 12,")),
    ("poll_prepare_path_of_nothing_kept", T, F, P_PATH, "\telse if(kind != POLL_NONE) strcpy(request->path, PATHS[kind]);"),
    ("poll_prepare_nothing_is_a_request", T, F, P_NONE, ""),
    ("poll_prepare_old_list_for_every_post", T, F, P_OLD_IF, "\tif(request->post)\n\t{\n\t\t// What is about"),
    ("poll_prepare_old_list_while_list_shown", T, F, P_OLD_IF, "\tif(poll->has_list)\n\t{\n\t\t// What is about"),
    ("poll_prepare_old_list_never", T, F, P_OLD_IF, "\tif(kind == POLL_DTC_CLEAR && !poll->has_list)\n\t{\n\t\t// What is about"),
    ("poll_prepare_old_struct_not_copied", T, F, P_OLD, ""),
    ("poll_prepare_old_struct_from_cleared", T, F, P_OLD, "\t\tpoll->old = poll->cleared;\n"),
    ("poll_prepare_old_text_not_copied", T, F, P_OLD_TEXT, ""),
    ("poll_prepare_old_not_flagged", T, F, P_OLD_FLAG, P_OLD_EVENT),
    ("poll_prepare_old_without_event", T, F, P_OLD_EVENT, ""),
    ("poll_prepare_old_event_without_lists", T, F, P_OLD_EVENT, "\t\tpoll->events |= POLL_EVENT_OLD;\n"),
    ("poll_prepare_old_event_only_lists", T, F, P_OLD_EVENT, "\t\tpoll->events |= POLL_EVENT_LISTS;\n"),
    ("poll_prepare_old_event_replaces_others", T, F, P_OLD_EVENT, "\t\tpoll->events = POLL_EVENT_OLD | POLL_EVENT_LISTS;\n"),
    ("poll_prepare_result_number_not_noted", T, F, P_RESULT_SEQ, ""),
    ("poll_prepare_result_number_of_request", T, F, P_RESULT_SEQ, "\t\tpoll->asked_result_seq = poll->conn.state.dtc.seq;\n"),
    ("poll_prepare_result_age_not_noted", T, F, P_RESULT_AGE, ""),
    ("poll_prepare_result_age_is_count", T, F, P_RESULT_AGE, "\t\tpoll->asked_age_s = poll->conn.state.dtc.count;\n"),
    ("poll_prepare_request_not_under_way", T, F, P_ASKING, ""),
    ("poll_prepare_kind_not_noted", T, F, P_ASKED, ""),

    # the adapter restarted or was replaced
    ("poll_forget_values_kept", T, F, FORGET_VALUES, "{\n"),
    ("poll_forget_dropped_count_forgotten", T, F, FORGET_VALUES, "{\n\tvalues_init(&poll->values);\n"),
    ("poll_forget_event_replaces_others", T, F, FORGET_EVENT, "\tpoll->events = POLL_EVENT_FORGET | POLL_EVENT_LISTS;\n"),
    ("poll_forget_unknown_dismissed", T, F, FORGET_DISMISS, FORGET_DISMISS.replace("DTC_FLOW_CLEARED)", "DTC_FLOW_CLEARED || poll->flow.phase == DTC_FLOW_UNKNOWN)")),
    ("poll_forget_catalog_stays_complete", T, F, FORGET_COMPLETE, "\tguard_catalog_connected(&poll->catalog_guard);\n\tpoll->events"),
    ("poll_forget_guard_not_told", T, F, FORGET_COMPLETE, "\tpoll->catalog_complete = false;\n\tpoll->events"),
    ("poll_forget_without_event", T, F, FORGET_EVENT, ""),
    ("poll_forget_event_without_lists", T, F, FORGET_EVENT, "\tpoll->events |= POLL_EVENT_FORGET;\n"),
    ("poll_forget_event_only_lists", T, F, FORGET_EVENT, "\tpoll->events |= POLL_EVENT_LISTS;\n"),
    ("poll_forget_flow_goes_on", T, F, FORGET_LOST, "\tif(poll->flow.phase == DTC_FLOW_LIST"),
    ("poll_forget_list_and_outcome_stay", T, F, FORGET_DISMISS, ""),
    ("poll_forget_outcome_stays", T, F, FORGET_DISMISS, "\tif(poll->flow.phase == DTC_FLOW_LIST) dtc_flow_dismiss(&poll->flow);\n"),
    ("poll_forget_list_stays", T, F, FORGET_DISMISS, "\tif(poll->flow.phase == DTC_FLOW_CLEARED) dtc_flow_dismiss(&poll->flow);\n"),
    ("poll_forget_failure_dismissed", T, F, FORGET_DISMISS, "\tdtc_flow_dismiss(&poll->flow);\n"),

    # the battery voltage
    ("poll_battery_millivolts_not_padded", T, F, BATTERY, BATTERY.replace("\".%03\" PRId32", "\".%\" PRId32")),
    ("poll_battery_two_decimals", T, F, BATTERY, BATTERY.replace("\".%03\" PRId32 \"}\", millivolts / 1000, millivolts % 1000", "\".%02\" PRId32 \"}\", millivolts / 1000, millivolts % 1000 / 10")),
    ("poll_battery_in_millivolts", T, F, BATTERY, BATTERY.replace("millivolts / 1000, millivolts % 1000", "millivolts, millivolts % 1000")),
    ("poll_battery_whole_volts", T, F, BATTERY, BATTERY.replace("millivolts / 1000, millivolts % 1000", "millivolts / 1000, millivolts % 1")),
    ("poll_battery_other_name", T, F, BATTERY, BATTERY.replace("\"{\\\"\" CATALOG_BATTERY \"\\\":%\"", "\"{\\\"BATT_V\\\":%\"")),
    ("poll_battery_with_pass_counter", T, F, BATTERY_APPLY, BATTERY_APPLY.replace("-1, now_ms", "0, now_ms")),
    ("poll_battery_seen_at_half_the_time", T, F, BATTERY_APPLY, BATTERY_APPLY.replace("-1, now_ms", "-1, now_ms / 2")),
    ("poll_battery_not_applied", T, F, BATTERY_APPLY, BATTERY_APPLY.replace("json, strlen(json),", "json, 0,")),

    # the answer to GET /api/state
    ("poll_state_any_status", T, F, S_READ, "\tbool read = wican_state_parse(body, length, &state, work, work_count);"),
    ("poll_state_any_2xx", T, F, S_READ, S_READ.replace("status == 200 &&", "status >= 200 && status < 300 &&")),
    ("poll_state_any_status_but_404", T, F, S_READ, S_READ.replace("status == 200 &&", "status != 404 && status != 0 &&")),
    ("poll_state_behind_length", T, F, S_READ, S_READ.replace("body, length, &state", "body, length + 1, &state")),
    ("poll_state_length_ignored", T, F, S_READ, S_READ.replace("body, length, &state", "body, length + strlen(body + length), &state")),
    ("poll_state_tokens_behind_room", T, F, S_READ, S_READ.replace("work, work_count);", "work, work_count + 1);")),
    ("poll_state_flow_not_told", T, F, S_FLOW, ""),
    ("poll_state_conn_not_told_ok", T, F, "\t\tconn_got_state(&poll->conn, CONN_GOT_OK, &state, now_ms);\n", ""),
    ("poll_state_failure_told_at_time_0", T, F, S_GOT, S_GOT.replace("NULL, now_ms);", "NULL, 0);")),
    ("poll_state_404_fails", T, F, S_GOT, "\t\tconn_got_state(&poll->conn, CONN_GOT_FAILED, NULL, now_ms);"),
    ("poll_state_every_failure_is_no_api", T, F, S_GOT, "\t\tconn_got_state(&poll->conn, CONN_GOT_NOT_FOUND, NULL, now_ms);"),
    ("poll_state_4xx_is_no_api", T, F, S_GOT, S_GOT.replace("status == 404 ?", "status >= 400 && status < 500 ?")),
    ("poll_state_no_answer_is_no_api", T, F, S_GOT, S_GOT.replace("status == 404 ?", "status == 404 || status == 0 ?")),
    ("poll_state_200_unreadable_is_no_api", T, F, S_GOT, S_GOT.replace("status == 404 ?", "status == 404 || status == 200 ?")),
    ("poll_state_bind_not_taken", T, F, S_BIND, ""),
    ("poll_state_bind_without_event", T, F, S_BIND, "\tconn_take_bind(&poll->conn, poll->bound_id, sizeof(poll->bound_id));\n"),
    ("poll_state_bind_event_always", T, F, S_BIND, "\tconn_take_bind(&poll->conn, poll->bound_id, sizeof(poll->bound_id));\n\tpoll->events |= POLL_EVENT_BOUND;\n"),
    ("poll_state_bind_room_too_small", T, F, S_BIND, S_BIND.replace("sizeof(poll->bound_id)))", "12))")),
    ("poll_state_bind_room_one_byte_short", T, F, S_BIND, S_BIND.replace("sizeof(poll->bound_id)))", "sizeof(poll->bound_id) - 1))")),
    ("poll_state_restart_ignored", T, F, S_RESTART, "\tconn_take_restarted(&poll->conn);\n\tif(poll->conn.restarted) forget(poll);\n"),
    ("poll_state_restart_not_taken", T, F, S_RESTART, "\tif(poll->conn.restarted) forget(poll);\n"),
    ("poll_state_forgets_always", T, F, S_RESTART, "\tconn_take_restarted(&poll->conn);\n\tforget(poll);\n"),
    ("poll_state_battery_before_forgetting", T, F, S_AFTER, S_BATTERY + S_RESTART),
    ("poll_state_battery_of_foreign_adapter", T, F, S_BATTERY, S_BATTERY.replace("read && !poll->conn.foreign &&", "read &&")),
    ("poll_state_battery_only_of_foreign_adapter", T, F, S_BATTERY, S_BATTERY.replace("!poll->conn.foreign", "poll->conn.foreign")),
    ("poll_state_battery_0_is_none", T, F, S_BATTERY, S_BATTERY.replace("state.batt_mv >= 0", "state.batt_mv > 0")),
    ("poll_state_battery_only_while_ecu_online", T, F, S_BATTERY, S_BATTERY.replace("state.batt_mv >= 0)", "state.batt_mv >= 0 && state.ecu_online)")),
    ("poll_state_battery_never", T, F, S_BATTERY, S_BATTERY.replace("state.batt_mv >= 0", "state.batt_mv < -1")),

    # the answer to GET /api/dtc/result
    ("poll_result_always_read_into_list", T, F, R_ROOM, "\tdtc_result_t *room = &poll->list;"),
    ("poll_result_always_read_into_cleared", T, F, R_ROOM, "\tdtc_result_t *room = &poll->cleared;"),
    ("poll_result_read_into_what_is_shown", T, F, R_ROOM, "\tdtc_result_t *room = poll->has_list ? &poll->list : &poll->cleared;"),
    ("poll_result_read_into_old", T, F, R_ROOM, "\tdtc_result_t *room = poll->has_list ? &poll->old : &poll->list;"),
    ("poll_result_own_whatever_number", T, F, R_OWN, "\tbool own = waits(&poll->flow);"),
    ("poll_result_never_own", T, F, R_OWN, "\tbool own = false;"),
    ("poll_result_no_answer_is_gone", T, F, R_NONE, ""),
    ("poll_result_no_answer_only_for_own", T, F, R_NONE, R_NONE.replace("if(status == 0)", "if(status == 0 && own)")),
    ("poll_result_no_answer_goes_on", T, F, R_NONE, "\tif(status == 0) conn_got_result(&poll->conn, CONN_GOT_FAILED, now_ms);\n"),
    ("poll_result_no_answer_told_at_time_0", T, F, R_NONE, R_NONE.replace("CONN_GOT_FAILED, now_ms", "CONN_GOT_FAILED, 0")),
    ("poll_result_no_answer_counts_as_fetched", T, F, R_NONE, R_NONE.replace("CONN_GOT_FAILED", "CONN_GOT_NOT_FOUND")),
    ("poll_result_204_is_no_answer", T, F, R_NONE, R_NONE.replace("status == 0", "status == 0 || status == 204")),
    ("poll_result_503_is_no_answer", T, F, R_NONE, R_NONE.replace("status == 0", "status == 0 || status == 503")),
    ("poll_result_every_failure_is_no_answer", T, F, R_NONE, R_NONE.replace("status == 0", "status != 200")),
    ("poll_result_any_status", T, F, R_TAKEN, R_TAKEN.replace("status == 200 && ", "")),
    ("poll_result_any_2xx", T, F, R_TAKEN, R_TAKEN.replace("status == 200 &&", "status >= 200 && status < 300 &&")),
    ("poll_result_only_long", T, F, R_TAKEN, R_TAKEN.replace("status == 200 &&", "status == 200 && length > 100 &&")),
    ("poll_result_header_not_above_number", T, F, R_TAKEN, R_TAKEN.replace("strcmp(seq_header, number) == 0", "strcmp(seq_header, number) <= 0")),
    ("poll_result_header_not_below_number", T, F, R_TAKEN, R_TAKEN.replace("strcmp(seq_header, number) == 0", "strcmp(seq_header, number) >= 0")),
    ("poll_result_header_optional", T, F, R_TAKEN, R_TAKEN.replace("seq_header != NULL && strcmp(seq_header, number) == 0", "(seq_header == NULL || strcmp(seq_header, number) == 0)")),
    ("poll_result_header_not_compared", T, F, R_TAKEN, R_TAKEN.replace(" && strcmp(seq_header, number) == 0", "")),
    ("poll_result_header_begins_with_number", T, F, R_TAKEN, R_TAKEN.replace("strcmp(seq_header, number) == 0", "strncmp(seq_header, number, strlen(number)) == 0")),
    ("poll_result_header_ends_with_number", T, F, R_TAKEN,
     R_TAKEN.replace("strcmp(seq_header, number) == 0", "strlen(seq_header) >= strlen(number) && strcmp(seq_header + strlen(seq_header) - strlen(number), number) == 0")),
    ("poll_result_header_with_blanks_and_zeros", T, F, R_TAKEN, R_TAKEN.replace("strcmp(seq_header, number) == 0", "strcmp(seq_header + strspn(seq_header, \" 0+\"), number) == 0")),
    ("poll_result_header_contains_number", T, F, R_TAKEN, R_TAKEN.replace("strcmp(seq_header, number) == 0", "strstr(seq_header, number) != NULL")),
    ("poll_result_without_room_taken", T, F, R_TAKEN, R_TAKEN.replace(" && length < sizeof(poll->list_text)", "")),
    ("poll_result_one_byte_too_long", T, F, R_TAKEN, R_TAKEN.replace("length < sizeof(poll->list_text)", "length <= sizeof(poll->list_text)")),
    ("poll_result_one_byte_too_short", T, F, R_TAKEN, R_TAKEN.replace("length < sizeof(poll->list_text)", "length < sizeof(poll->list_text) - 1")),
    ("poll_result_unreadable_taken", T, F, R_TAKEN, R_TAKEN.replace("dtc_result_parse(body, length, room, work, work_count))", "(dtc_result_parse(body, length, room, work, work_count) || true))")),
    ("poll_result_behind_length", T, F, R_TAKEN, R_TAKEN.replace("body, length, room", "body, length + 1, room")),
    ("poll_result_length_ignored", T, F, R_TAKEN, R_TAKEN.replace("body, length, room", "body, strlen(body), room")),
    ("poll_result_tokens_behind_room", T, F, R_TAKEN, R_TAKEN.replace("work, work_count))", "work, work_count + 1))")),
    ("poll_result_number_31_bit", T, F, "\tsnprintf(number, sizeof(number), \"%\" PRIu32, poll->asked_result_seq);",
     "\tsnprintf(number, sizeof(number), \"%\" PRIu32, poll->asked_result_seq & 0x7FFFFFFFu);"),
    ("poll_result_conn_not_told", T, F, R_OK, ""),
    ("poll_result_of_others_fails_round", T, F, R_OK, R_OK.replace("CONN_GOT_OK", "own ? CONN_GOT_OK : CONN_GOT_FAILED")),
    ("poll_result_taken_fails_round", T, F, R_OK, R_OK.replace("CONN_GOT_OK", "CONN_GOT_FAILED")),
    ("poll_result_flow_not_told", T, F, R_FLOW, ""),
    ("poll_result_flow_told_own_number", T, F, R_FLOW, R_FLOW.replace("poll->asked_result_seq, room->clear", "poll->flow.seq, room->clear")),
    ("poll_result_flow_told_read", T, F, R_FLOW, R_FLOW.replace("room->clear,", "false,")),
    ("poll_result_flow_told_clear", T, F, R_FLOW, R_FLOW.replace("room->clear,", "true,")),
    ("poll_result_flow_told_opposite_action", T, F, R_FLOW, R_FLOW.replace("room->clear,", "!room->clear,")),
    ("poll_result_flow_told_no_codes", T, F, R_FLOW, R_FLOW.replace("room->dtc_count,", "0,")),
    ("poll_result_flow_told_listed_codes", T, F, R_FLOW, R_FLOW.replace("room->dtc_count,", "(uint32_t)room->code_count,")),
    ("poll_result_flow_told_age_0", T, F, R_FLOW, R_FLOW.replace("poll->asked_age_s,", "0,")),
    ("poll_result_flow_told_at_time_0", T, F, R_FLOW, R_FLOW.replace("poll->asked_age_s, now_ms", "poll->asked_age_s, 0")),
    ("poll_result_shown_without_change", T, F, R_CHANGED, "\t\tif(poll->flow.phase != before || true)"),
    ("poll_result_list_text_not_copied", T, F, R_TEXT, ""),
    ("poll_result_list_text_one_byte_short", T, F, R_TEXT, "\t\t\t\tmemcpy(poll->list_text, body, length - 1);\n"),
    ("poll_result_list_text_not_ended", T, F, R_TEXT_END, ""),
    ("poll_result_list_not_flagged", T, F, R_LIST, ""),
    ("poll_result_cleared_not_flagged", T, F, R_CLEARED, ""),
    ("poll_result_without_event", T, F, R_EVENT, ""),
    ("poll_result_event_is_old", T, F, R_EVENT, "\t\t\tpoll->events |= POLL_EVENT_OLD;\n"),
    ("poll_result_gone_conn_not_told", T, F, R_GONE, ""),
    ("poll_result_gone_fails_round", T, F, R_GONE, R_GONE.replace("CONN_GOT_NOT_FOUND", "CONN_GOT_FAILED")),
    ("poll_result_gone_flow_waits_for_ever", T, F, R_LOST, "\t(void)own;\n"),
    ("poll_result_gone_flow_lost_whatever_it_waits_for", T, F, R_LOST, "\tif(own || waits(&poll->flow)) dtc_flow_lost(&poll->flow);\n"),
    ("poll_result_gone_flow_dismissed", T, F, R_LOST, R_LOST + "\tif(own) dtc_flow_dismiss(&poll->flow);\n"),

    # the answer to GET /load_car_config
    ("poll_catalog_any_answer_taken", T, F, C_200, "\tif(status != 0)\n\t{\n\t\t// A body that cannot be used"),
    ("poll_catalog_any_2xx_taken", T, F, C_200, "\tif(status >= 200 && status < 300)\n\t{\n\t\t// A body that cannot be used"),
    ("poll_catalog_unusable_asked_again", T, F, C_APPLY,
     "\t\tif(catalog_apply_config(&poll->catalog, body, length, work, work_count))\n\t\t{\n\t\t\tpoll->catalog_complete = true;\n\t\t\tgot = CONN_GOT_OK;\n\t\t}\n"),
    ("poll_catalog_unusable_not_found", T, F, C_APPLY,
     "\t\tgot = CONN_GOT_NOT_FOUND;\n\t\tif(catalog_apply_config(&poll->catalog, body, length, work, work_count))\n\t\t{\n\t\t\tpoll->catalog_complete = true;\n\t\t\tgot = CONN_GOT_OK;\n\t\t}\n"),
    ("poll_catalog_unusable_complete", T, F, C_APPLY,
     "\t\tcatalog_apply_config(&poll->catalog, body, length, work, work_count);\n\t\tpoll->catalog_complete = true;\n\t\tgot = CONN_GOT_OK;\n"),
    ("poll_catalog_complete_only_with_values", T, F, C_APPLY, C_APPLY.replace("work, work_count)) poll->catalog_complete", "work, work_count) && poll->catalog.count > 1) poll->catalog_complete")),
    ("poll_catalog_never_complete", T, F, C_APPLY, "\t\tcatalog_apply_config(&poll->catalog, body, length, work, work_count);\n\t\tgot = CONN_GOT_OK;\n"),
    ("poll_catalog_only_first_byte_read", T, F, C_APPLY, C_APPLY.replace("body, length, work", "body, length > 1 ? 1 : length, work")),
    ("poll_catalog_behind_length", T, F, C_APPLY, C_APPLY.replace("body, length, work", "body, length + 1, work")),
    ("poll_catalog_length_ignored", T, F, C_APPLY, C_APPLY.replace("body, length, work", "body, length + strlen(body + length), work")),
    ("poll_catalog_tokens_behind_room", T, F, C_APPLY, C_APPLY.replace("work, work_count))", "work, work_count + 1))")),
    ("poll_catalog_taken_fails_round", T, F, C_APPLY, C_APPLY.replace("got = CONN_GOT_OK;", "got = CONN_GOT_FAILED;")),
    ("poll_catalog_404_fails_round", T, F, C_ABSENT, "\telse if(status != 0 && state != NULL && state->autopid == WICAN_AUTOPID_OFF)"),
    ("poll_catalog_500_fails_round_while_off", T, F, C_ABSENT, C_ABSENT.replace("status != 0 && state != NULL", "status < 0 && state != NULL")),
    ("poll_catalog_every_4xx_is_absent", T, F, C_ABSENT, C_ABSENT.replace("status == 404 ||", "(status >= 400 && status < 500) ||")),
    ("poll_catalog_no_answer_is_absent_while_off", T, F, C_ABSENT, C_ABSENT.replace("status != 0 && ", "")),
    ("poll_catalog_absent_while_starting", T, F, C_ABSENT, C_ABSENT.replace("state->autopid == WICAN_AUTOPID_OFF", "state->autopid != WICAN_AUTOPID_RUN")),
    ("poll_catalog_absent_while_running", T, F, C_ABSENT, C_ABSENT.replace(" && state->autopid == WICAN_AUTOPID_OFF", "")),
    ("poll_catalog_absent_without_state", T, F, C_ABSENT, C_ABSENT.replace("state != NULL && state->autopid == WICAN_AUTOPID_OFF", "(state == NULL || state->autopid == WICAN_AUTOPID_OFF)")),
    ("poll_catalog_every_failure_is_absent", T, F, C_ABSENT, C_ABSENT.replace("state != NULL && state->autopid == WICAN_AUTOPID_OFF", "(state == NULL || state != NULL)")),
    ("poll_catalog_absent_not_asked_again", T, F, C_NOT_FOUND, "\t\tgot = CONN_GOT_OK;\n"),
    ("poll_catalog_conn_not_told", T, F, C_GOT, "\t(void)got;\n\t(void)now_ms;\n"),
    ("poll_catalog_conn_told_half_the_time", T, F, C_GOT, "\tconn_got_catalog(&poll->conn, got, now_ms / 2);\n"),

    # the answer to GET /autopid_data
    ("poll_values_any_answer_taken", T, F, V_APPLY, V_APPLY.replace("if(status == 200)", "if(status != 0)")),
    ("poll_values_any_2xx_taken", T, F, V_APPLY, V_APPLY.replace("if(status == 200)", "if(status >= 200 && status < 300)")),
    ("poll_values_without_pass_counter", T, F, V_APPLY, V_APPLY.replace("state != NULL ? (int64_t)state->pass : -1", "state != NULL ? -1 : -1")),
    ("poll_values_pass_0_without_state", T, F, V_APPLY, V_APPLY.replace("(int64_t)state->pass : -1", "(int64_t)state->pass : 0")),
    ("poll_values_pass_16_bit", T, F, V_APPLY, V_APPLY.replace("(int64_t)state->pass", "(int64_t)(uint16_t)state->pass")),
    ("poll_values_pass_one_more", T, F, V_APPLY, V_APPLY.replace("(int64_t)state->pass", "(int64_t)state->pass + 1")),
    ("poll_values_pass_is_boot", T, F, V_APPLY, V_APPLY.replace("(int64_t)state->pass", "(int64_t)state->boot")),
    ("poll_values_seen_at_time_0", T, F, V_APPLY, V_APPLY.replace("-1, now_ms,", "-1, 0,")),
    ("poll_values_behind_length", T, F, V_APPLY, V_APPLY.replace("body, length, state", "body, length + 1, state")),
    ("poll_values_length_ignored", T, F, V_APPLY, V_APPLY.replace("body, length, state", "body, length + strlen(body + length), state")),
    ("poll_values_tokens_behind_room", T, F, V_APPLY, V_APPLY.replace("work, work_count);", "work, work_count + 1);")),
    ("poll_values_not_noted_in_catalog", T, F, V_NOTE, ""),
    ("poll_values_noted_when_repeated", T, F, V_NOTE, "\tif(result != VALUES_INVALID) catalog_note_values(&poll->catalog, &poll->values);\n"),
    ("poll_values_noted_only_when_repeated", T, F, V_NOTE, "\tif(result == VALUES_REPEATED) catalog_note_values(&poll->catalog, &poll->values);\n"),
    ("poll_values_repeated_fails_round", T, F, V_GOT, V_GOT.replace("result == VALUES_INVALID ? CONN_GOT_FAILED : CONN_GOT_OK", "result == VALUES_RENEWED ? CONN_GOT_OK : CONN_GOT_FAILED")),
    ("poll_values_invalid_is_an_answer", T, F, V_GOT, V_GOT.replace("result == VALUES_INVALID ? CONN_GOT_FAILED : CONN_GOT_OK", "CONN_GOT_OK")),
    ("poll_values_always_fail_round", T, F, V_GOT, V_GOT.replace("result == VALUES_INVALID ? CONN_GOT_FAILED : CONN_GOT_OK", "CONN_GOT_FAILED")),
    ("poll_values_conn_not_told", T, F, V_GOT + "\n", ""),
    ("poll_values_conn_told_at_time_0", T, F, V_GOT, V_GOT.replace("CONN_GOT_OK, now_ms);", "CONN_GOT_OK, 0);")),

    # the answer to POST /api/dtc
    ("poll_post_body_not_read", T, F, D_PARSE, "\tif(json_parse(body, length, work, work_count) > 1000000)"),
    ("poll_post_body_behind_length", T, F, D_PARSE, "\tif(json_parse(body, length + 1, work, work_count) > 0)"),
    ("poll_post_body_length_ignored", T, F, D_PARSE, "\tif(json_parse(body, length + strlen(body + length), work, work_count) > 0)"),
    ("poll_post_tokens_behind_room", T, F, D_PARSE, "\tif(json_parse(body, length, work, work_count + 1) > 0)"),
    ("poll_post_number_other_member", T, F, "\t\tint number = json_member(body, work, 0, \"seq\");", "\t\tint number = json_member(body, work, 0, \"accepted\");"),
    ("poll_post_reason_other_member", T, F, "\t\tint text = json_member(body, work, 0, \"reason\");", "\t\tint text = json_member(body, work, 0, \"error\");"),
    ("poll_post_negative_number_taken", T, F, D_SEQ, D_SEQ.replace(" || seq < 0", "")),
    ("poll_post_number_above_32_bit_cut", T, F, D_SEQ, D_SEQ.replace(" || seq > UINT32_MAX", "")),
    ("poll_post_number_4294967295_refused", T, F, D_SEQ, D_SEQ.replace("seq > UINT32_MAX", "seq >= UINT32_MAX")),
    ("poll_post_number_above_31_bit_refused", T, F, D_SEQ, D_SEQ.replace("seq > UINT32_MAX", "seq > INT32_MAX")),
    ("poll_post_number_always_0", T, F, D_SEQ, "\t\tjson_integer(body, &work[number < 0 ? 0 : number], &seq);\n\t\tseq = 0;"),
    ("poll_post_reason_not_read", T, F, D_REASON, "\t\t(void)text;\n"),
    ("poll_post_reason_of_31_bytes_refused", T, F, D_REASON, D_REASON.replace("sizeof(reason))", "sizeof(reason) - 1)")),
    ("poll_post_refusal_without_reason_stays_so", T, F, D_HTTP, ""),
    ("poll_post_refusal_reason_replaced", T, F, D_HTTP, D_HTTP.replace("reason[0] == '\\0' && status != 202", "status != 202")),
    ("poll_post_refusal_reason_other_text", T, F, D_HTTP, D_HTTP.replace("\"http_%d\"", "\"http%d\"")),
    ("poll_post_refusal_reason_without_status", T, F, D_HTTP, D_HTTP.replace("\"http_%d\", status", "\"http_\"")),
    ("poll_post_200_is_accepted", T, F, D_POSTED, D_POSTED.replace("status, (uint32_t)seq", "status == 200 ? 202 : status, (uint32_t)seq")),
    ("poll_post_every_2xx_is_accepted", T, F, D_POSTED, D_POSTED.replace("status, (uint32_t)seq", "status >= 200 && status < 300 ? 202 : status, (uint32_t)seq")),
    ("poll_post_no_answer_is_a_refusal", T, F, D_POSTED, D_POSTED.replace("status, (uint32_t)seq", "status == 0 ? 500 : status, (uint32_t)seq")),
    ("poll_post_number_not_passed", T, F, D_POSTED, D_POSTED.replace("(uint32_t)seq", "0")),
    ("poll_post_number_16_bit", T, F, D_POSTED, D_POSTED.replace("(uint32_t)seq", "(uint16_t)seq")),
    ("poll_post_reason_not_passed", T, F, D_POSTED, D_POSTED.replace("reason, now_ms", "NULL, now_ms")),
    ("poll_post_flow_not_told", T, F, D_POSTED, "\t(void)seq;\n\t(void)now_ms;"),

    # which answer is taken, and what follows every answer
    ("poll_apply_without_request_under_way", T, F, A_MATCH, "\tif(request == NULL || request->kind != poll->asked) return;"),
    ("poll_apply_without_request", T, F, A_MATCH, "\tif(!poll->asking || (request != NULL && request->kind != poll->asked)) return;"),
    ("poll_apply_answer_of_other_kind", T, F, A_MATCH, "\tif(!poll->asking || request == NULL) return;"),
    ("poll_apply_request_stays_under_way", T, F, A_END, "\t// What an HTTP client"),
    ("poll_apply_negative_status_is_a_status", T, F, A_NEGATIVE, ""),
    ("poll_apply_status_below_100_is_none", T, F, A_NEGATIVE, "\tif(status < 100) status = 0;\n"),
    ("poll_apply_minus_one_is_a_status", T, F, A_NEGATIVE, "\tif(status < -1) status = 0;\n"),
    ("poll_apply_only_minus_one_is_none", T, F, A_NEGATIVE, "\tif(status == -1) status = 0;\n"),
    ("poll_apply_null_body_read", T, F, A_BODY, ""),
    ("poll_apply_values_and_catalog_swapped", T, F, A_KINDS,
     A_KINDS.replace("POLL_CATALOG) got_catalog", "POLL_VALUES) got_catalog").replace("POLL_VALUES) got_values", "POLL_CATALOG) got_values")),
    ("poll_apply_answer_to_clear_dropped", T, F, A_POSTED, "\telse if(poll->asked == POLL_DTC_READ) got_posted(poll, status, body, length, now_ms, work, work_count);"),
    ("poll_apply_answer_to_read_dropped", T, F, A_POSTED, "\telse if(poll->asked == POLL_DTC_CLEAR) got_posted(poll, status, body, length, now_ms, work, work_count);"),
    ("poll_apply_outage_not_watched", T, F, A_AFTER, "\tfollow(poll);\n\tif(guard_catalog_due"),
    ("poll_apply_outage_watched_only_after_state", T, F, A_AFTER, "\tif(poll->asked == POLL_STATE) watch(poll, now_ms);\n\tfollow(poll);\n\tif(guard_catalog_due"),
    ("poll_apply_lists_follow_only_after_state_or_result", T, F, A_AFTER, "\twatch(poll, now_ms);\n\tif(poll->asked <= POLL_RESULT) follow(poll);\n\tif(guard_catalog_due"),
    ("poll_apply_outage_watched_at_time_0", T, F, A_AFTER, "\twatch(poll, 0);\n\tfollow(poll);\n\tif(guard_catalog_due"),
    ("poll_apply_lists_do_not_follow", T, F, A_AFTER, "\twatch(poll, now_ms);\n\tif(guard_catalog_due"),
    ("poll_apply_follow_before_outage", T, F, A_AFTER, "\tfollow(poll);\n\twatch(poll, now_ms);\n\tif(guard_catalog_due"),
    ("poll_apply_catalog_never_due", T, F, A_GUARD, ""),
    ("poll_apply_catalog_due_only_after_state", T, F, A_GUARD, A_GUARD.replace("\tif(guard_catalog_due(", "\tif(poll->asked == POLL_STATE && guard_catalog_due(")),
    ("poll_apply_catalog_due_only_after_values", T, F, A_GUARD, A_GUARD.replace("\tif(guard_catalog_due(", "\tif(poll->asked == POLL_VALUES && guard_catalog_due(")),
    ("poll_apply_catalog_due_without_event", T, F, A_GUARD, A_GUARD.replace("\t\tpoll->events |= POLL_EVENT_CATALOG;\n", "\t\t(void)poll->events;\n")),
    ("poll_apply_catalog_event_is_lists", T, F, A_GUARD, A_GUARD.replace("POLL_EVENT_CATALOG", "POLL_EVENT_LISTS")),
    ("poll_apply_catalog_due_when_incomplete", T, F, A_GUARD, A_GUARD.replace("poll->catalog_complete, now_ms", "true, now_ms")),
    ("poll_apply_catalog_sum_constant", T, F, A_GUARD, A_GUARD.replace("catalog_checksum(&poll->catalog), poll->catalog_complete", "0, poll->catalog_complete")),
    ("poll_apply_catalog_sum_is_count", T, F, A_GUARD, A_GUARD.replace("catalog_checksum(&poll->catalog), poll->catalog_complete", "(uint32_t)poll->catalog.count, poll->catalog_complete")),
    ("poll_apply_catalog_due_one_ms_early", T, F, A_GUARD, A_GUARD.replace("poll->catalog_complete, now_ms", "poll->catalog_complete, now_ms + 1")),
    ("poll_apply_catalog_due_one_ms_late", T, F, A_GUARD, A_GUARD.replace("poll->catalog_complete, now_ms", "poll->catalog_complete, now_ms > 0 ? now_ms - 1 : 0")),
    ("poll_apply_catalog_due_at_time_0", T, F, A_GUARD, A_GUARD.replace("poll->catalog_complete, now_ms", "poll->catalog_complete, 0")),
    ("poll_apply_counts_500_as_ok", T, F, A_COUNT, A_COUNT.replace("status <= 499", "status <= 500")),
    ("poll_apply_counts_499_as_failed", T, F, A_COUNT, A_COUNT.replace("status <= 499", "status < 499")),
    ("poll_apply_counts_200_as_failed", T, F, A_COUNT, A_COUNT.replace("status >= 200", "status > 200")),
    ("poll_apply_counts_199_as_ok", T, F, A_COUNT, A_COUNT.replace("status >= 200", "status >= 199")),
    ("poll_apply_counts_no_answer_as_ok", T, F, A_COUNT, A_COUNT.replace("status >= 200 && status <= 499", "status <= 499")),
    ("poll_apply_counts_only_2xx_as_ok", T, F, A_COUNT, A_COUNT.replace("status <= 499", "status <= 299")),
    ("poll_apply_counters_swapped", T, F, A_COUNT, A_COUNT.replace("poll->http_ok++", "poll->http_failed++").replace("else poll->http_failed++", "else poll->http_ok++")),
    ("poll_apply_ok_not_counted", T, F, A_COUNT, A_COUNT.replace(" poll->http_ok++;", " (void)poll->http_ok;")),
    ("poll_apply_failed_not_counted", T, F, A_COUNT, A_COUNT.replace("\telse poll->http_failed++;\n", "")),

    # the events
    ("poll_events_not_cleared", T, F, "\tpoll->events = 0;\n", ""),
    ("poll_events_not_returned", T, F, "\tuint32_t events = poll->events;", "\tuint32_t events = 0;"),
    ("poll_events_returned_without_lists", T, F, "\tpoll->events = 0;\n\treturn events;", "\tpoll->events = 0;\n\treturn events & ~POLL_EVENT_LISTS;"),
    ("poll_events_returned_only_one", T, F, "\tpoll->events = 0;\n\treturn events;", "\tpoll->events = 0;\n\treturn events & (~events + 1);"),

    # what the user asks for
    ("poll_read_list_stays_shown", T, F, READ_FOLLOW, ""),
    ("poll_read_at_half_the_time", T, F, READ, READ.replace("&poll->catalog, now_ms);", "&poll->catalog, now_ms / 2);")),
    ("poll_read_block_only", T, F, READ, READ.replace("dtc_flow_read(", "dtc_flow_read_block(")),
    ("poll_read_says_allowed", T, F, READ_FOLLOW + "\treturn block;", READ_FOLLOW + "\t(void)block;\n\treturn DTC_FLOW_ALLOWED;"),
    ("poll_clear_stuck_switch_ignored", T, F, CLEAR, CLEAR.replace("button_stuck, now_ms", "button_stuck && now_ms == UINT64_MAX, now_ms")),
    ("poll_clear_at_half_the_time", T, F, CLEAR, CLEAR.replace("button_stuck, now_ms", "button_stuck, now_ms / 2")),
    ("poll_clear_block_only", T, F, CLEAR, CLEAR.replace("dtc_flow_clear(", "dtc_flow_clear_block(")),
    ("poll_dismiss_does_nothing", T, F, DISMISS, "\tfollow(poll);\n"),
    ("poll_dismiss_list_stays_shown", T, F, DISMISS, "\tdtc_flow_dismiss(&poll->flow);\n"),
]
