"""Mutations of display/components/core/conn.c, see ../redproof.py."""

F = "components/core/conn.c"
H = "components/core/conn.h"
T = "test_conn"

PASSED = "\treturn now_ms > since_ms ? now_ms - since_ms : 0;"
SCANNING = "\treturn conn->has_state && (conn->state.dtc.phase == WICAN_DTC_QUEUED || conn->state.dtc.phase == WICAN_DTC_RUNNING);"
F_RESULT = "\tif(asked < CONN_ASK_RESULT && conn->want_result) return CONN_ASK_RESULT;"
F_CATALOG = "\tif(asked < CONN_ASK_CATALOG && conn->want_catalog && !conn->foreign && !scanning(conn)) return CONN_ASK_CATALOG;"
F_COMMENT = "\t// The catalogue waits for the end of a polling pass, and a scan stops the polling\n"
F_VALUES = "\tif(asked < CONN_ASK_VALUES && conn->want_values) return CONN_ASK_VALUES;"
ROUND_OVER = "\tconn->asked = CONN_ASK_NOTHING;\n\tconn->failed_rounds = 0;\n\tconn->next_round_ms = conn->round_start_ms + CONN_ROUND_MS;\n"
ENDED = "\treturn conn->round_start_ms + passed(now_ms, conn->round_start_ms);"
FAILED = "\tconn->asked = CONN_ASK_NOTHING;\n\tconn->good_rounds = 0;\n\tif(conn->failed_rounds < INT_MAX) conn->failed_rounds++;\n"
WAIT = "\tconn->next_round_ms = ended(conn, now_ms) + BACKOFF[(conn->failed_rounds < BACKOFF_COUNT ? conn->failed_rounds : BACKOFF_COUNT) - 1];"
ENDS = "\tif(!conn->asking || conn->asked != asked) return false;"
INIT = "\tmemset(conn, 0, sizeof(*conn));\n"
BOUND = "\tif(bound_id != NULL) copy_text(conn->bound_id, sizeof(conn->bound_id), bound_id);"
SAME = "\tif(up == conn->wifi) return;\n"
IDLE = "\tif(!conn->wifi || conn->asking) return CONN_ASK_NOTHING;"
DUE = "\t\tif(now_ms < conn->next_round_ms) return CONN_ASK_NOTHING;"
RECHECK = "\t\tif(conn->no_api && passed(now_ms, conn->no_api_since_ms) < CONN_NO_API_RECHECK_MS) ask = following(conn, CONN_ASK_STATE);"
ANOTHER = "\t\tif(state->boot != conn->state.boot || strcmp(state->id, conn->state.id) != 0) replaced(conn);"
PIDS = "\t\telse if(state->pids != conn->state.pids) conn->want_catalog = true;"
BIND = "\tif(conn->bound_id[0] == '\\0' && state->id[0] != '\\0')"
FOREIGN = "\tconn->foreign = strcmp(state->id, conn->bound_id) != 0;"
GOOD = "\tif(conn->good_rounds < INT_MAX) conn->good_rounds++;"
WANT_RESULT = "\tconn->want_result = !conn->foreign && state->dtc.result_seq != 0 && state->dtc.result_seq != conn->fetched_result_seq;"
WANT_VALUES = "\tconn->want_values = !conn->foreign && state->autopid == WICAN_AUTOPID_RUN && state->ecu_online && !scanning(conn);"
OK_STATE = "\tif(got == CONN_GOT_OK && state != NULL)"
RESULT_FAILED = "\tif(got != CONN_GOT_OK && got != CONN_GOT_NOT_FOUND)\n\t{\n\t\tfailed(conn, now_ms);\n\t\treturn;\n\t}\n\tconn->fetched_result_seq"
CATALOG_FAILED = "\tif(got != CONN_GOT_OK && got != CONN_GOT_NOT_FOUND)\n\t{\n\t\tfailed(conn, now_ms);\n\t\treturn;\n\t}\n\tif(got == CONN_GOT_OK) conn->want_catalog = false;"
VALUES_FAILED = "\tif(got != CONN_GOT_OK)\n\t{\n\t\tfailed(conn, now_ms);\n\t\treturn;\n\t}\n"
V_WIFI = "\tif(!conn->wifi) return CONN_VIEW_NO_WIFI;\n"
V_NO_ANSWER = "\tif(conn->failed_rounds >= CONN_FAILED_ROUNDS && passed(now_ms, conn->wifi_since_ms) >= CONN_GRACE_MS) return CONN_VIEW_NO_ANSWER;\n"
V_CONNECTING = "\tif(!conn->has_state && !conn->no_api) return CONN_VIEW_CONNECTING;\n"
V_FOREIGN = "\tif(conn->foreign) return CONN_VIEW_FOREIGN;\n"
V_NO_API = "\tif(conn->no_api) return CONN_VIEW_NO_API;\n"
V_OFF = "\tif(conn->state.autopid == WICAN_AUTOPID_OFF) return CONN_VIEW_AUTOPID_OFF;\n"
V_STARTING = ("\t// Also a value this display does not know: nothing says that AutoPID runs\n"
              "\tif(conn->state.autopid != WICAN_AUTOPID_RUN) return CONN_VIEW_STARTING;\n")
V_SCAN = "\tif(scanning(conn)) return CONN_VIEW_SCAN;\n"
V_OFFLINE = "\tif(!conn->state.ecu_online) return CONN_VIEW_ECU_OFFLINE;\n"
V_COMMENT = "\t// A 404 of /api/state is an answer as well\n"
ALLOWED = ("\treturn conn->good_rounds >= CONN_DTC_MIN_ROUNDS && !conn->foreign && conn->state.autopid == WICAN_AUTOPID_RUN &&\n"
           "\t       conn->state.up_s >= CONN_DTC_MIN_UP_S;")
TAKE_BIND = "\tif(!conn->bind_pending || id == NULL || strlen(conn->bound_id) >= size) return false;"

MUTATIONS = [
    # time
    ("conn_time_steps_back_with_the_caller", T, F, PASSED, "\treturn now_ms - since_ms;"),
    ("conn_end_of_round_before_its_start", T, F, ENDED, "\t(void)conn;\n\treturn now_ms;"),
    ("conn_end_of_round_is_its_start", T, F, ENDED, "\t(void)now_ms;\n\treturn conn->round_start_ms;"),

    # what a scan is
    ("conn_scan_of_the_firmware_before_counts", T, F, SCANNING, SCANNING.replace("conn->has_state && ", "")),
    ("conn_queued_is_no_scan", T, F, SCANNING, SCANNING.replace("conn->state.dtc.phase == WICAN_DTC_QUEUED || ", "")),
    ("conn_running_is_no_scan", T, F, SCANNING, SCANNING.replace(" || conn->state.dtc.phase == WICAN_DTC_RUNNING", "")),
    ("conn_done_is_a_scan", T, F, SCANNING, SCANNING.replace("WICAN_DTC_RUNNING)", "WICAN_DTC_RUNNING || conn->state.dtc.phase == WICAN_DTC_DONE)")),
    ("conn_error_is_a_scan", T, F, SCANNING, SCANNING.replace("WICAN_DTC_RUNNING)", "WICAN_DTC_RUNNING || conn->state.dtc.phase == WICAN_DTC_ERROR)")),

    # the order within a round
    ("conn_result_never_asked", T, F, F_RESULT + "\n", ""),
    ("conn_result_asked_again_in_the_round", T, F, F_RESULT, F_RESULT.replace("asked < CONN_ASK_RESULT", "asked <= CONN_ASK_RESULT")),
    ("conn_result_behind_the_catalog", T, F, F_RESULT + "\n" + F_COMMENT + F_CATALOG + "\n", F_COMMENT + F_CATALOG + "\n" + F_RESULT + "\n"),
    ("conn_catalog_never_asked", T, F, F_CATALOG + "\n", ""),
    ("conn_catalog_asked_again_in_the_round", T, F, F_CATALOG, F_CATALOG.replace("asked < CONN_ASK_CATALOG", "asked <= CONN_ASK_CATALOG")),
    ("conn_catalog_every_round", T, F, F_CATALOG, F_CATALOG.replace("conn->want_catalog && ", "")),
    ("conn_catalog_from_foreign_adapter", T, F, F_CATALOG, F_CATALOG.replace(" && !conn->foreign", "")),
    ("conn_catalog_during_scan", T, F, F_CATALOG, F_CATALOG.replace(" && !scanning(conn)", "")),
    ("conn_catalog_only_while_autopid_runs", T, F, F_CATALOG, F_CATALOG.replace("!scanning(conn))", "!scanning(conn) && (!conn->has_state || conn->state.autopid == WICAN_AUTOPID_RUN))")),
    ("conn_catalog_only_with_ecu_online", T, F, F_CATALOG, F_CATALOG.replace("!scanning(conn))", "!scanning(conn) && (!conn->has_state || conn->state.ecu_online))")),
    ("conn_values_never_asked", T, F, F_VALUES + "\n", ""),
    ("conn_values_asked_again_in_the_round", T, F, F_VALUES, F_VALUES.replace("asked < CONN_ASK_VALUES", "asked <= CONN_ASK_VALUES")),
    ("conn_values_every_round", T, F, F_VALUES, F_VALUES.replace(" && conn->want_values", "")),
    ("conn_values_before_catalog", T, F, F_CATALOG + "\n" + F_VALUES + "\n", F_VALUES + "\n" + F_CATALOG + "\n"),

    # the end of a round
    ("conn_round_ends_with_first_answer", T, F, "\tif(following(conn, conn->asked) != CONN_ASK_NOTHING) return;\n", ""),
    ("conn_round_end_not_noted", T, F, ROUND_OVER, ROUND_OVER.replace("\tconn->asked = CONN_ASK_NOTHING;\n", "")),
    ("conn_answered_round_keeps_failed_count", T, F, ROUND_OVER, ROUND_OVER.replace("\tconn->failed_rounds = 0;\n", "")),
    ("conn_next_round_at_once", T, F, ROUND_OVER, ROUND_OVER.replace("\tconn->next_round_ms = conn->round_start_ms + CONN_ROUND_MS;\n", "")),
    ("conn_next_round_one_ms_early", T, F, ROUND_OVER, ROUND_OVER.replace("+ CONN_ROUND_MS;", "+ CONN_ROUND_MS - 1;")),
    ("conn_round_time_shorter", T, H, "#define CONN_ROUND_MS           1000u", "#define CONN_ROUND_MS           999u"),
    ("conn_round_time_longer", T, H, "#define CONN_ROUND_MS           1000u", "#define CONN_ROUND_MS           1001u"),

    # a failed round
    ("conn_failed_round_goes_on", T, F, FAILED, FAILED.replace("\tconn->asked = CONN_ASK_NOTHING;\n", "")),
    ("conn_failed_round_keeps_good_rounds", T, F, FAILED, FAILED.replace("\tconn->good_rounds = 0;\n", "")),
    ("conn_failed_rounds_counted_to_one", T, F, FAILED, FAILED.replace("conn->failed_rounds < INT_MAX", "conn->failed_rounds < 1")),
    ("conn_failed_rounds_counted_to_two", T, F, FAILED, FAILED.replace("conn->failed_rounds < INT_MAX", "conn->failed_rounds < 2")),
    ("conn_failed_rounds_counted_to_three", T, F, FAILED, FAILED.replace("conn->failed_rounds < INT_MAX", "conn->failed_rounds < 3")),
    ("conn_failed_rounds_overflow", T, F, FAILED, FAILED.replace("if(conn->failed_rounds < INT_MAX) ", "")),
    ("conn_wait_counted_from_start_of_round", T, F, WAIT, "\t(void)now_ms;\n" + WAIT.replace("ended(conn, now_ms)", "conn->round_start_ms")),
    ("conn_wait_always_the_first", T, F, WAIT, "\tconn->next_round_ms = ended(conn, now_ms) + BACKOFF[0];"),
    ("conn_wait_never_the_last", T, F, WAIT, WAIT.replace("conn->failed_rounds < BACKOFF_COUNT ? conn->failed_rounds : BACKOFF_COUNT) - 1]",
                                                         "conn->failed_rounds < BACKOFF_COUNT - 1 ? conn->failed_rounds : BACKOFF_COUNT - 1) - 1]")),
    ("conn_wait_one_step_ahead", T, F, WAIT, WAIT.replace("conn->failed_rounds < BACKOFF_COUNT ? conn->failed_rounds : BACKOFF_COUNT) - 1]",
                                                         "conn->failed_rounds < BACKOFF_COUNT - 1 ? conn->failed_rounds + 1 : BACKOFF_COUNT) - 1]")),
    ("conn_first_wait_changed", T, H, "{1000u, 2000u, 5000u, 10000u}", "{1001u, 2000u, 5000u, 10000u}"),
    ("conn_second_wait_changed", T, H, "{1000u, 2000u, 5000u, 10000u}", "{1000u, 1999u, 5000u, 10000u}"),
    ("conn_third_wait_changed", T, H, "{1000u, 2000u, 5000u, 10000u}", "{1000u, 2000u, 5001u, 10000u}"),
    ("conn_last_wait_changed", T, H, "{1000u, 2000u, 5000u, 10000u}", "{1000u, 2000u, 5000u, 9999u}"),
    ("conn_fifth_wait_added", T, H, "{1000u, 2000u, 5000u, 10000u}", "{1000u, 2000u, 5000u, 10000u, 20000u}"),

    # which end belongs to which request
    ("conn_any_end_ends_the_request", T, F, ENDS, "\t(void)asked;\n\tif(!conn->asking) return false;"),
    ("conn_end_without_request_counts", T, F, ENDS, "\tif(conn->asked != asked) return false;"),
    ("conn_request_never_ends", T, F, "\tconn->asking = false;\n\treturn true;", "\treturn true;"),

    # another adapter
    ("conn_restart_not_reported", T, F, "\tconn->restarted = true;\n", ""),
    ("conn_restart_keeps_catalog", T, F, "\tconn->restarted = true;\n\tconn->want_catalog = true;\n", "\tconn->restarted = true;\n"),
    ("conn_restart_keeps_fetched_results", T, F, "\tconn->want_catalog = true;\n\tconn->fetched_result_seq = 0;\n}\n\nvoid conn_init", "\tconn->want_catalog = true;\n}\n\nvoid conn_init"),

    # init
    ("conn_init_keeps_wifi", T, F, INIT, "\tbool wifi = conn->wifi;\n\n" + INIT + "\tconn->wifi = wifi;\n"),
    ("conn_init_keeps_restart", T, F, INIT, "\tbool restarted = conn->restarted;\n\n" + INIT + "\tconn->restarted = restarted;\n"),
    ("conn_init_keeps_id_to_take", T, F, INIT, "\tbool pending = conn->bind_pending;\n\n" + INIT + "\tconn->bind_pending = pending;\n"),
    ("conn_init_keeps_state", T, F, INIT, "\tbool has_state = conn->has_state;\n\n" + INIT + "\tconn->has_state = has_state;\n"),
    ("conn_init_keeps_request", T, F, INIT, "\tbool asking = conn->asking;\n\n" + INIT + "\tconn->asking = asking;\n"),
    ("conn_init_keeps_binding", T, F, INIT, "\tmemset((char *)conn + sizeof(conn->bound_id), 0, sizeof(*conn) - sizeof(conn->bound_id));\n"),
    ("conn_init_only_the_binding", T, F, INIT, "\tconn->bound_id[0] = '\\0';\n\tconn->wifi = false;\n"),
    ("conn_init_not_bound", T, F, BOUND + "\n", "\t(void)bound_id;\n"),
    ("conn_init_writes_behind_the_id", T, F, BOUND, BOUND.replace("sizeof(conn->bound_id)", "101")),
    ("conn_init_id_cut_shorter", T, F, BOUND, BOUND.replace("sizeof(conn->bound_id)", "sizeof(conn->bound_id) - 1")),

    # WiFi
    ("conn_wifi_same_value_starts_over", T, F, SAME, ""),
    ("conn_wifi_leaving_ignored", T, F, SAME, "\tif(up == conn->wifi || !up) return;\n"),
    ("conn_wifi_not_stored", T, F, "\tconn->wifi = up;\n", ""),
    ("conn_wifi_time_not_stored", T, F, "\tconn->wifi_since_ms = now_ms;\n", "\t(void)now_ms;\n"),
    ("conn_wifi_keeps_request", T, F, "\tconn->asking = false;\n\tconn->asked = CONN_ASK_NOTHING;\n\tconn->next_round_ms = 0;\n", "\tconn->asked = CONN_ASK_NOTHING;\n\tconn->next_round_ms = 0;\n"),
    ("conn_wifi_keeps_round", T, F, "\tconn->asking = false;\n\tconn->asked = CONN_ASK_NOTHING;\n\tconn->next_round_ms = 0;\n", "\tconn->asking = false;\n\tconn->next_round_ms = 0;\n"),
    ("conn_wifi_keeps_wait", T, F, "\tconn->asking = false;\n\tconn->asked = CONN_ASK_NOTHING;\n\tconn->next_round_ms = 0;\n", "\tconn->asking = false;\n\tconn->asked = CONN_ASK_NOTHING;\n"),
    ("conn_wifi_first_round_after_a_second", T, F, "\tconn->next_round_ms = 0;\n", "\tconn->next_round_ms = now_ms + CONN_ROUND_MS;\n"),
    ("conn_wifi_keeps_failed_rounds", T, F, "\tconn->next_round_ms = 0;\n\tconn->failed_rounds = 0;\n", "\tconn->next_round_ms = 0;\n"),
    ("conn_wifi_keeps_good_rounds", T, F, "\tconn->failed_rounds = 0;\n\tconn->good_rounds = 0;\n\tconn->has_state = false;\n", "\tconn->failed_rounds = 0;\n\tconn->has_state = false;\n"),
    ("conn_wifi_keeps_state", T, F, "\tconn->good_rounds = 0;\n\tconn->has_state = false;\n\tconn->no_api = false;\n", "\tconn->good_rounds = 0;\n\tconn->no_api = false;\n"),
    ("conn_wifi_keeps_no_api", T, F, "\tconn->has_state = false;\n\tconn->no_api = false;\n\tconn->want_catalog = true;\n", "\tconn->has_state = false;\n\tconn->want_catalog = true;\n"),
    ("conn_wifi_keeps_catalog", T, F, "\tconn->no_api = false;\n\tconn->want_catalog = true;\n", "\tconn->no_api = false;\n"),
    ("conn_wifi_keeps_fetched_results", T, F, "\tconn->want_catalog = true;\n\tconn->fetched_result_seq = 0;\n}\n\nconn_ask_t", "\tconn->want_catalog = true;\n}\n\nconn_ask_t"),
    ("conn_wifi_forgets_restart", T, F, "\tconn->wifi = up;\n", "\tconn->wifi = up;\n\tconn->restarted = false;\n"),
    ("conn_wifi_forgets_id_to_take", T, F, "\tconn->wifi = up;\n", "\tconn->wifi = up;\n\tconn->bind_pending = false;\n"),
    ("conn_wifi_forgets_binding", T, F, "\tconn->wifi = up;\n", "\tconn->wifi = up;\n\tif(!conn->bind_pending) conn->bound_id[0] = '\\0';\n"),

    # what to send
    ("conn_asks_without_wifi", T, F, IDLE, "\tif(conn->asking) return CONN_ASK_NOTHING;"),
    ("conn_request_handed_out_again", T, F, IDLE, "\tif(!conn->wifi) return CONN_ASK_NOTHING;"),
    ("conn_round_not_awaited", T, F, DUE + "\n", ""),
    ("conn_round_one_ms_late", T, F, DUE, DUE.replace("now_ms < conn->next_round_ms", "now_ms <= conn->next_round_ms")),
    ("conn_round_start_not_stored", T, F, "\t\tconn->round_start_ms = now_ms;\n", ""),
    ("conn_round_start_never_steps_back", T, F, "\t\tconn->round_start_ms = now_ms;\n", "\t\tif(now_ms > conn->round_start_ms) conn->round_start_ms = now_ms;\n"),
    ("conn_no_api_state_every_round", T, F, RECHECK + "\n", ""),
    ("conn_no_api_state_never_again", T, F, RECHECK, RECHECK.replace(" && passed(now_ms, conn->no_api_since_ms) < CONN_NO_API_RECHECK_MS", "")),
    ("conn_no_api_recheck_one_ms_late", T, F, RECHECK, RECHECK.replace("< CONN_NO_API_RECHECK_MS", "<= CONN_NO_API_RECHECK_MS")),
    ("conn_no_api_recheck_time_shorter", T, H, "#define CONN_NO_API_RECHECK_MS  30000u", "#define CONN_NO_API_RECHECK_MS  29999u"),
    ("conn_no_api_recheck_time_longer", T, H, "#define CONN_NO_API_RECHECK_MS  30000u", "#define CONN_NO_API_RECHECK_MS  30001u"),
    ("conn_rounds_without_state_with_api", T, F, RECHECK, RECHECK.replace("conn->no_api && ", "")),
    ("conn_no_api_rounds_skip_catalog", T, F, RECHECK, RECHECK.replace("ask = following(conn, CONN_ASK_STATE);", "ask = CONN_ASK_VALUES;")),
    ("conn_no_api_recheck_counted_from_round", T, F, "\t\tconn->no_api_since_ms = ended(conn, now_ms);", "\t\tconn->no_api_since_ms = conn->round_start_ms;"),
    ("conn_request_not_noted", T, F, "\tconn->asked = ask;\n\tconn->asking = true;\n", "\tconn->asking = true;\n"),
    ("conn_request_not_under_way", T, F, "\tconn->asked = ask;\n\tconn->asking = true;\n", "\tconn->asked = ask;\n"),

    # an answered state
    ("conn_first_answer_is_a_restart", T, F, "\tif(conn->has_state)\n\t{\n" + ANOTHER, "\tif(1)\n\t{\n" + ANOTHER),
    ("conn_boot_number_ignored", T, F, ANOTHER, ANOTHER.replace("state->boot != conn->state.boot || ", "")),
    ("conn_other_id_is_no_restart", T, F, ANOTHER, ANOTHER.replace(" || strcmp(state->id, conn->state.id) != 0", "")),
    ("conn_higher_boot_number_only", T, F, ANOTHER, ANOTHER.replace("state->boot != conn->state.boot", "state->boot > conn->state.boot")),
    ("conn_pids_ignored", T, F, PIDS + "\n", ""),
    ("conn_more_pids_only", T, F, PIDS, PIDS.replace("state->pids != conn->state.pids", "state->pids > conn->state.pids")),
    ("conn_pids_change_is_a_restart", T, F, PIDS, "\t\telse if(state->pids != conn->state.pids) replaced(conn);"),
    ("conn_state_after_404_is_no_restart", T, F, "\telse if(conn->no_api) replaced(conn);\n", ""),
    ("conn_state_after_404_keeps_catalog", T, F, "\telse if(conn->no_api) replaced(conn);", "\telse if(conn->no_api) conn->restarted = true;"),
    ("conn_binds_again", T, F, BIND, "\tif(state->id[0] != '\\0')"),
    ("conn_binds_to_no_id", T, F, BIND, "\tif(conn->bound_id[0] == '\\0')"),
    ("conn_bound_id_not_stored", T, F, "\t\tcopy_text(conn->bound_id, sizeof(conn->bound_id), state->id);\n", ""),
    ("conn_bound_id_cut", T, F, "\t\tcopy_text(conn->bound_id, sizeof(conn->bound_id), state->id);", "\t\tcopy_text(conn->bound_id, sizeof(conn->bound_id) - 1, state->id);"),
    ("conn_bind_not_reported", T, F, "\t\tconn->bind_pending = true;\n", ""),
    ("conn_never_foreign", T, F, FOREIGN, "\tconn->foreign = false;"),
    ("conn_id_compared_by_beginning_of_bound", T, F, FOREIGN, "\tconn->foreign = strncmp(state->id, conn->bound_id, strlen(conn->bound_id)) != 0;"),
    ("conn_id_compared_by_beginning_of_answer", T, F, FOREIGN, "\tconn->foreign = strncmp(state->id, conn->bound_id, strlen(state->id)) != 0;"),
    ("conn_foreign_decided_before_binding", T, F,
     "\t// An adapter without an id cannot be told from others: nothing to bind to\n",
     "\tconn->foreign = conn->bound_id[0] != '\\0' && strcmp(state->id, conn->bound_id) != 0;\n\tif(conn->foreign) return;\n"),
    ("conn_state_not_stored", T, F, "\tconn->state = *state;\n", ""),
    ("conn_state_not_known", T, F, "\tconn->state = *state;\n\tconn->has_state = true;\n", "\tconn->state = *state;\n"),
    ("conn_state_keeps_no_api", T, F, "\tconn->has_state = true;\n\tconn->no_api = false;\n", "\tconn->has_state = true;\n"),
    ("conn_good_rounds_not_counted", T, F, GOOD + "\n", ""),
    ("conn_good_rounds_counted_to_one", T, F, GOOD, GOOD.replace("< INT_MAX", "< 1")),
    ("conn_good_rounds_overflow", T, F, GOOD, "\tconn->good_rounds++;"),
    ("conn_result_from_foreign_adapter", T, F, WANT_RESULT, WANT_RESULT.replace("!conn->foreign && ", "")),
    ("conn_result_zero_is_a_result", T, F, WANT_RESULT, WANT_RESULT.replace("state->dtc.result_seq != 0 && ", "")),
    ("conn_result_fetched_every_round", T, F, WANT_RESULT, WANT_RESULT.replace(" && state->dtc.result_seq != conn->fetched_result_seq", "")),
    ("conn_result_only_with_higher_number", T, F, WANT_RESULT, WANT_RESULT.replace("state->dtc.result_seq != conn->fetched_result_seq", "state->dtc.result_seq > conn->fetched_result_seq")),
    ("conn_result_only_when_done", T, F, WANT_RESULT, WANT_RESULT.replace("state->dtc.result_seq != 0 &&", "state->dtc.result_seq != 0 && state->dtc.phase == WICAN_DTC_DONE &&")),
    ("conn_result_not_during_scan", T, F, WANT_RESULT, WANT_RESULT.replace("state->dtc.result_seq != 0 &&", "state->dtc.result_seq != 0 && state->dtc.phase != WICAN_DTC_RUNNING &&")),
    ("conn_values_from_foreign_adapter", T, F, WANT_VALUES, WANT_VALUES.replace("!conn->foreign && ", "")),
    ("conn_values_while_autopid_off", T, F, WANT_VALUES, WANT_VALUES.replace("state->autopid == WICAN_AUTOPID_RUN", "state->autopid != WICAN_AUTOPID_STARTING")),
    ("conn_values_while_autopid_starting", T, F, WANT_VALUES, WANT_VALUES.replace("state->autopid == WICAN_AUTOPID_RUN", "state->autopid != WICAN_AUTOPID_OFF")),
    ("conn_values_with_ecu_offline", T, F, WANT_VALUES, WANT_VALUES.replace(" && state->ecu_online", "")),
    ("conn_values_during_scan", T, F, WANT_VALUES, WANT_VALUES.replace(" && !scanning(conn)", "")),

    # the end of the state request
    ("conn_state_end_not_matched", T, F, "\tif(!ends(conn, CONN_ASK_STATE)) return;\n", "\tconn->asking = false;\n"),
    ("conn_state_without_state_taken", T, F, OK_STATE, "\tif(got == CONN_GOT_OK)"),
    ("conn_failed_state_taken", T, F, OK_STATE, "\tif(got != CONN_GOT_NOT_FOUND && state != NULL)"),
    ("conn_unknown_outcome_is_404", T, F, "\telse if(got == CONN_GOT_NOT_FOUND)", "\telse if(got != CONN_GOT_FAILED)"),
    ("conn_404_is_failed", T, F, "\telse if(got == CONN_GOT_NOT_FOUND)", "\telse if(0)"),
    ("conn_404_after_state_is_no_restart", T, F, "\t\tif(conn->has_state) replaced(conn);\n", ""),
    ("conn_404_is_always_a_restart", T, F, "\t\tif(conn->has_state) replaced(conn);", "\t\treplaced(conn);"),
    ("conn_404_keeps_state", T, F, "\t\tconn->has_state = false;\n\t\tconn->no_api = true;\n", "\t\tconn->no_api = true;\n"),
    ("conn_404_not_noted", T, F, "\t\tconn->has_state = false;\n\t\tconn->no_api = true;\n", "\t\tconn->has_state = false;\n"),
    ("conn_404_time_not_stored", T, F, "\t\tconn->no_api_since_ms = ended(conn, now_ms);\n", ""),
    ("conn_404_keeps_foreign", T, F, "\t\tconn->foreign = false;\n", ""),
    ("conn_404_keeps_good_rounds", T, F, "\t\tconn->foreign = false;\n\t\tconn->good_rounds = 0;\n", "\t\tconn->foreign = false;\n"),
    ("conn_404_keeps_result_wanted", T, F, "\t\tconn->want_result = false;\n", ""),
    ("conn_404_values_as_before", T, F, "\t\tconn->want_values = true;\n", ""),
    ("conn_404_no_values", T, F, "\t\tconn->want_values = true;\n", "\t\tconn->want_values = false;\n"),
    ("conn_failed_state_round_goes_on", T, F, "\t\tfailed(conn, now_ms);\n\t\treturn;\n\t}\n\tanswered(conn);\n}\n\nvoid conn_got_result", "\t\tfailed(conn, now_ms);\n\t}\n\tanswered(conn);\n}\n\nvoid conn_got_result"),
    ("conn_state_answer_not_counted", T, F, "\t\tfailed(conn, now_ms);\n\t\treturn;\n\t}\n\tanswered(conn);\n}\n\nvoid conn_got_result", "\t\tfailed(conn, now_ms);\n\t\treturn;\n\t}\n}\n\nvoid conn_got_result"),

    # the end of the other requests
    ("conn_result_end_not_matched", T, F, "\tif(!ends(conn, CONN_ASK_RESULT)) return;\n", "\tconn->asking = false;\n"),
    ("conn_result_204_is_failed", T, F, RESULT_FAILED, RESULT_FAILED.replace("got != CONN_GOT_OK && got != CONN_GOT_NOT_FOUND", "got != CONN_GOT_OK")),
    ("conn_result_failed_counts_as_fetched", T, F, RESULT_FAILED, RESULT_FAILED.replace("got != CONN_GOT_OK && got != CONN_GOT_NOT_FOUND", "got != CONN_GOT_OK && got != CONN_GOT_NOT_FOUND && got != CONN_GOT_FAILED")),
    ("conn_result_unknown_outcome_counts", T, F, RESULT_FAILED, RESULT_FAILED.replace("got != CONN_GOT_OK && got != CONN_GOT_NOT_FOUND", "got == CONN_GOT_FAILED")),
    ("conn_result_failed_round_goes_on", T, F, RESULT_FAILED, "\t(void)now_ms;\n\tif(got != CONN_GOT_OK && got != CONN_GOT_NOT_FOUND)\n\t{\n\t\tconn->good_rounds = 0;\n\t\tanswered(conn);\n\t\treturn;\n\t}\n\tconn->fetched_result_seq"),
    ("conn_result_not_noted_as_fetched", T, F, "\tconn->fetched_result_seq = conn->state.dtc.result_seq;\n", ""),
    ("conn_result_204_not_noted_as_fetched", T, F, "\tconn->fetched_result_seq = conn->state.dtc.result_seq;", "\tif(got == CONN_GOT_OK) conn->fetched_result_seq = conn->state.dtc.result_seq;"),
    ("conn_result_noted_by_request_number", T, F, "\tconn->fetched_result_seq = conn->state.dtc.result_seq;", "\tconn->fetched_result_seq = conn->state.dtc.seq;"),
    ("conn_catalog_end_not_matched", T, F, "\tif(!ends(conn, CONN_ASK_CATALOG)) return;\n", "\tconn->asking = false;\n"),
    ("conn_catalog_404_is_failed", T, F, CATALOG_FAILED, CATALOG_FAILED.replace("got != CONN_GOT_OK && got != CONN_GOT_NOT_FOUND", "got != CONN_GOT_OK")),
    ("conn_catalog_failed_round_goes_on", T, F, CATALOG_FAILED, CATALOG_FAILED.replace("got != CONN_GOT_OK && got != CONN_GOT_NOT_FOUND", "0")),
    ("conn_catalog_unknown_outcome_counts", T, F, CATALOG_FAILED, CATALOG_FAILED.replace("got != CONN_GOT_OK && got != CONN_GOT_NOT_FOUND", "got == CONN_GOT_FAILED")),
    ("conn_catalog_404_is_done", T, F, CATALOG_FAILED, CATALOG_FAILED.replace("\tif(got == CONN_GOT_OK) conn->want_catalog = false;", "\tconn->want_catalog = false;")),
    ("conn_catalog_never_done", T, F, CATALOG_FAILED, CATALOG_FAILED.replace("\tif(got == CONN_GOT_OK) conn->want_catalog = false;", "\t(void)got;")),
    ("conn_values_end_not_matched", T, F, "\tif(!ends(conn, CONN_ASK_VALUES)) return;\n", "\tconn->asking = false;\n"),
    ("conn_values_404_is_no_failure", T, F, VALUES_FAILED, VALUES_FAILED.replace("got != CONN_GOT_OK", "got == CONN_GOT_FAILED")),
    ("conn_values_failed_is_no_failure", T, F, VALUES_FAILED, VALUES_FAILED.replace("got != CONN_GOT_OK", "got == CONN_GOT_NOT_FOUND")),
    ("conn_values_never_fail", T, F, VALUES_FAILED, "\t(void)got;\n\t(void)now_ms;\n"),

    # the view
    ("conn_view_without_wifi", T, F, V_WIFI, ""),
    ("conn_view_no_answer_never", T, F, V_NO_ANSWER, "\t(void)now_ms;\n"),
    ("conn_view_no_answer_one_round_late", T, F, V_NO_ANSWER, V_NO_ANSWER.replace("conn->failed_rounds >= CONN_FAILED_ROUNDS", "conn->failed_rounds > CONN_FAILED_ROUNDS")),
    ("conn_view_no_answer_only_at_three", T, F, V_NO_ANSWER, V_NO_ANSWER.replace("conn->failed_rounds >= CONN_FAILED_ROUNDS", "conn->failed_rounds == CONN_FAILED_ROUNDS")),
    ("conn_failed_rounds_limit_lower", T, H, "#define CONN_FAILED_ROUNDS      3 ", "#define CONN_FAILED_ROUNDS      2 "),
    ("conn_failed_rounds_limit_higher", T, H, "#define CONN_FAILED_ROUNDS      3 ", "#define CONN_FAILED_ROUNDS      4 "),
    ("conn_view_no_answer_without_failures", T, F, V_NO_ANSWER, V_NO_ANSWER.replace("conn->failed_rounds >= CONN_FAILED_ROUNDS && ", "")),
    ("conn_view_no_grace", T, F, V_NO_ANSWER, "\t(void)now_ms;\n" + V_NO_ANSWER.replace(" && passed(now_ms, conn->wifi_since_ms) >= CONN_GRACE_MS", "")),
    ("conn_view_grace_one_ms_longer", T, F, V_NO_ANSWER, V_NO_ANSWER.replace(">= CONN_GRACE_MS", "> CONN_GRACE_MS")),
    ("conn_grace_time_shorter", T, H, "#define CONN_GRACE_MS           15000u", "#define CONN_GRACE_MS           14999u"),
    ("conn_grace_time_longer", T, H, "#define CONN_GRACE_MS           15000u", "#define CONN_GRACE_MS           15001u"),
    ("conn_view_grace_only_before_first_answer", T, F, V_NO_ANSWER,
     V_NO_ANSWER.replace("passed(now_ms, conn->wifi_since_ms) >= CONN_GRACE_MS", "(conn->has_state || passed(now_ms, conn->wifi_since_ms) >= CONN_GRACE_MS)")),
    ("conn_view_connecting_before_no_answer", T, F, V_NO_ANSWER + V_COMMENT + V_CONNECTING, V_COMMENT + V_CONNECTING + V_NO_ANSWER),
    ("conn_view_no_answer_behind_foreign", T, F, V_NO_ANSWER + V_COMMENT + V_CONNECTING + V_FOREIGN, V_COMMENT + V_CONNECTING + V_FOREIGN + V_NO_ANSWER),
    ("conn_view_no_answer_behind_no_api", T, F, V_NO_ANSWER + V_COMMENT + V_CONNECTING + V_FOREIGN + V_NO_API, V_COMMENT + V_CONNECTING + V_FOREIGN + V_NO_API + V_NO_ANSWER),
    ("conn_view_never_connecting", T, F, V_CONNECTING, ""),
    ("conn_view_404_is_no_answer_yet", T, F, V_CONNECTING, "\tif(!conn->has_state) return CONN_VIEW_CONNECTING;\n"),
    ("conn_view_never_foreign", T, F, V_FOREIGN, ""),
    ("conn_view_foreign_behind_autopid", T, F, V_FOREIGN + V_NO_API + V_OFF + V_STARTING, V_NO_API + V_OFF + V_STARTING + V_FOREIGN),
    ("conn_view_foreign_behind_scan", T, F, V_FOREIGN + V_NO_API + V_OFF + V_STARTING + V_SCAN, V_NO_API + V_OFF + V_STARTING + V_SCAN + V_FOREIGN),
    ("conn_view_foreign_behind_ignition", T, F, V_FOREIGN + V_NO_API + V_OFF + V_STARTING + V_SCAN + V_OFFLINE, V_NO_API + V_OFF + V_STARTING + V_SCAN + V_OFFLINE + V_FOREIGN),
    ("conn_view_never_no_api", T, F, V_NO_API, ""),
    ("conn_view_never_autopid_off", T, F, V_OFF, ""),
    ("conn_view_off_is_starting", T, F, V_OFF, "\tif(conn->state.autopid == WICAN_AUTOPID_OFF) return CONN_VIEW_STARTING;\n"),
    ("conn_view_never_starting", T, F, V_STARTING, ""),
    ("conn_view_unknown_autopid_is_live", T, F, V_STARTING, V_STARTING.replace("!= WICAN_AUTOPID_RUN", "== WICAN_AUTOPID_STARTING")),
    ("conn_view_scan_before_autopid", T, F, V_OFF + V_STARTING + V_SCAN, V_SCAN + V_OFF + V_STARTING),
    ("conn_view_scan_before_starting", T, F, V_STARTING + V_SCAN, V_SCAN + V_STARTING),
    ("conn_view_ignition_before_autopid", T, F, V_OFF + V_STARTING + V_SCAN + V_OFFLINE, V_OFFLINE + V_OFF + V_STARTING + V_SCAN),
    ("conn_view_never_scan", T, F, V_SCAN, ""),
    ("conn_view_ignition_before_scan", T, F, V_SCAN + V_OFFLINE, V_OFFLINE + V_SCAN),
    ("conn_view_never_ecu_offline", T, F, V_OFFLINE, ""),
    ("conn_view_never_live", T, F, "\treturn CONN_VIEW_LIVE;", "\treturn CONN_VIEW_ECU_OFFLINE;"),

    # state and permission
    ("conn_state_never_null", T, F, "\treturn conn->has_state ? &conn->state : NULL;", "\treturn &conn->state;"),
    ("conn_state_always_null", T, F, "\treturn conn->has_state ? &conn->state : NULL;", "\treturn conn->has_state && false ? &conn->state : NULL;"),
    ("conn_dtc_allowed_for_foreign_adapter", T, F, ALLOWED, ALLOWED.replace("!conn->foreign && ", "")),
    ("conn_dtc_allowed_while_autopid_starting", T, F, ALLOWED, ALLOWED.replace("conn->state.autopid == WICAN_AUTOPID_RUN", "conn->state.autopid != WICAN_AUTOPID_OFF")),
    ("conn_dtc_allowed_while_autopid_off", T, F, ALLOWED, ALLOWED.replace("conn->state.autopid == WICAN_AUTOPID_RUN", "conn->state.autopid != WICAN_AUTOPID_STARTING")),
    ("conn_dtc_allowed_right_after_boot", T, F, ALLOWED, ALLOWED.replace(" &&\n\t       conn->state.up_s >= CONN_DTC_MIN_UP_S", "")),
    ("conn_dtc_allowed_one_second_late", T, F, ALLOWED, ALLOWED.replace("conn->state.up_s >= CONN_DTC_MIN_UP_S", "conn->state.up_s > CONN_DTC_MIN_UP_S")),
    ("conn_dtc_up_time_shorter", T, H, "#define CONN_DTC_MIN_UP_S       15u", "#define CONN_DTC_MIN_UP_S       14u"),
    ("conn_dtc_up_time_longer", T, H, "#define CONN_DTC_MIN_UP_S       15u", "#define CONN_DTC_MIN_UP_S       16u"),
    ("conn_dtc_allowed_without_rounds", T, F, ALLOWED, ALLOWED.replace("conn->good_rounds >= CONN_DTC_MIN_ROUNDS", "conn->has_state")),
    ("conn_dtc_allowed_one_round_late", T, F, ALLOWED, ALLOWED.replace("conn->good_rounds >= CONN_DTC_MIN_ROUNDS", "conn->good_rounds > CONN_DTC_MIN_ROUNDS")),
    ("conn_dtc_rounds_fewer", T, H, "#define CONN_DTC_MIN_ROUNDS     2 ", "#define CONN_DTC_MIN_ROUNDS     1 "),
    ("conn_dtc_rounds_more", T, H, "#define CONN_DTC_MIN_ROUNDS     2 ", "#define CONN_DTC_MIN_ROUNDS     3 "),
    ("conn_dtc_allowed_only_with_ecu_online", T, F, ALLOWED, ALLOWED.replace("!conn->foreign && ", "!conn->foreign && conn->state.ecu_online && ")),
    ("conn_dtc_allowed_not_during_scan", T, F, ALLOWED, ALLOWED.replace("!conn->foreign && ", "!conn->foreign && !scanning(conn) && ")),

    # what does not matter
    ("conn_result_only_with_fault_memory", T, F, WANT_RESULT, WANT_RESULT.replace("state->dtc.result_seq != 0 &&", "state->dtc.result_seq != 0 && state->dtc.supported &&")),
    ("conn_values_only_with_fault_memory", T, F, WANT_VALUES, WANT_VALUES.replace("state->ecu_online", "state->ecu_online && state->dtc.supported")),
    ("conn_values_only_after_15_s", T, F, WANT_VALUES, WANT_VALUES.replace("state->ecu_online", "state->ecu_online && state->up_s >= CONN_DTC_MIN_UP_S")),
    ("conn_dtc_allowed_only_with_fault_memory", T, F, ALLOWED, ALLOWED.replace("!conn->foreign && ", "!conn->foreign && conn->state.dtc.supported && ")),
    ("conn_dtc_allowed_only_between_requests", T, F, ALLOWED, ALLOWED.replace("!conn->foreign && ", "!conn->foreign && !conn->asking && ")),
    ("conn_view_no_answer_only_between_requests", T, F, V_NO_ANSWER, V_NO_ANSWER.replace("conn->failed_rounds >= CONN_FAILED_ROUNDS", "!conn->asking && conn->failed_rounds >= CONN_FAILED_ROUNDS")),
    ("conn_failed_round_forgets_fetched_result", T, F, FAILED, FAILED + "\tconn->fetched_result_seq = 0;\n"),
    ("conn_failed_round_asks_catalog_again", T, F, FAILED, FAILED + "\tconn->want_catalog = true;\n"),
    ("conn_failed_round_drops_state", T, F, FAILED, FAILED + "\tconn->has_state = false;\n"),
    ("conn_uptime_back_is_a_restart", T, F, ANOTHER, ANOTHER.replace("state->boot != conn->state.boot", "state->boot != conn->state.boot || state->up_s < conn->state.up_s")),
    ("conn_restart_resets_good_rounds", T, F, "\tconn->restarted = true;\n", "\tconn->restarted = true;\n\tconn->good_rounds = 0;\n"),
    ("conn_round_start_on_the_grid", T, F, "\t\tconn->round_start_ms = now_ms;\n", "\t\tconn->round_start_ms = conn->next_round_ms > 0 ? conn->next_round_ms : now_ms;\n"),
    ("conn_answered_round_takes_one_failed_off", T, F, ROUND_OVER, ROUND_OVER.replace("\tconn->failed_rounds = 0;\n", "\tif(conn->failed_rounds > 0) conn->failed_rounds--;\n")),

    # large numbers, long ids and times beyond 32 bit
    ("conn_round_time_compared_in_32_bit", T, F, DUE, DUE.replace("now_ms < conn->next_round_ms", "(uint32_t)now_ms < (uint32_t)conn->next_round_ms")),
    ("conn_next_round_stored_in_32_bit", T, F, ROUND_OVER, ROUND_OVER.replace("conn->round_start_ms + CONN_ROUND_MS;", "(uint32_t)(conn->round_start_ms + CONN_ROUND_MS);")),
    ("conn_wait_stored_in_32_bit", T, F, WAIT, WAIT.replace("ended(conn, now_ms) + BACKOFF", "(uint32_t)ended(conn, now_ms) + BACKOFF")),
    ("conn_grace_counted_in_32_bit", T, F, V_NO_ANSWER, V_NO_ANSWER.replace("passed(now_ms, conn->wifi_since_ms) >= CONN_GRACE_MS", "(uint32_t)passed(now_ms, conn->wifi_since_ms) >= CONN_GRACE_MS")),
    ("conn_wifi_time_stored_in_32_bit", T, F, "\tconn->wifi_since_ms = now_ms;\n", "\tconn->wifi_since_ms = (uint32_t)now_ms;\n"),
    ("conn_no_api_recheck_counted_in_32_bit", T, F, RECHECK, RECHECK.replace("passed(now_ms, conn->no_api_since_ms) < CONN_NO_API_RECHECK_MS", "(uint32_t)passed(now_ms, conn->no_api_since_ms) < CONN_NO_API_RECHECK_MS")),
    ("conn_404_time_stored_in_32_bit", T, F, "\t\tconn->no_api_since_ms = ended(conn, now_ms);", "\t\tconn->no_api_since_ms = (uint32_t)ended(conn, now_ms);"),
    ("conn_end_of_round_counted_in_32_bit", T, F, ENDED, "\treturn conn->round_start_ms + (uint32_t)passed(now_ms, conn->round_start_ms);"),
    ("conn_good_rounds_compared_in_8_bit", T, F, ALLOWED, ALLOWED.replace("conn->good_rounds >= CONN_DTC_MIN_ROUNDS", "(uint8_t)conn->good_rounds >= CONN_DTC_MIN_ROUNDS")),
    ("conn_failed_rounds_compared_in_8_bit", T, F, V_NO_ANSWER, V_NO_ANSWER.replace("conn->failed_rounds >= CONN_FAILED_ROUNDS", "(uint8_t)conn->failed_rounds >= CONN_FAILED_ROUNDS")),
    ("conn_wait_begins_anew_after_256_failed_rounds", T, F, WAIT,
     WAIT.replace("(conn->failed_rounds < BACKOFF_COUNT ? conn->failed_rounds : BACKOFF_COUNT)",
                  "(conn->failed_rounds % 256 != 0 && conn->failed_rounds % 256 < BACKOFF_COUNT ? conn->failed_rounds % 256 : BACKOFF_COUNT)")),
    ("conn_binds_only_to_id_that_begins_with_a_letter", T, F, BIND, "\tif(conn->bound_id[0] == '\\0' && state->id[0] > ' ')"),
    ("conn_id_cut_at_space", T, F, "\twhile(length + 1 < size && text[length] != '\\0')", "\twhile(length + 1 < size && text[length] != '\\0' && text[length] != ' ')"),
    ("conn_id_cut_at_byte_above_127", T, F, "\twhile(length + 1 < size && text[length] != '\\0')", "\twhile(length + 1 < size && text[length] != '\\0' && (text[length] & 0x80) == 0)"),
    ("conn_id_compared_without_case", T, F, FOREIGN, "\tconn->foreign = strcasecmp(state->id, conn->bound_id) != 0;"),
    ("conn_id_compared_up_to_31_bytes", T, F, FOREIGN, "\tconn->foreign = strncmp(state->id, conn->bound_id, 31) != 0;"),
    ("conn_restart_id_compared_up_to_31_bytes", T, F, ANOTHER, ANOTHER.replace("strcmp(state->id, conn->state.id) != 0", "strncmp(state->id, conn->state.id, 31) != 0")),
    ("conn_boot_compared_in_16_bit", T, F, ANOTHER, ANOTHER.replace("state->boot != conn->state.boot", "(uint16_t)state->boot != (uint16_t)conn->state.boot")),
    ("conn_restart_only_with_lower_uptime", T, F, ANOTHER, ANOTHER.replace("state->boot != conn->state.boot", "(state->boot != conn->state.boot && state->up_s <= conn->state.up_s)")),
    ("conn_pids_compared_in_8_bit", T, F, PIDS, PIDS.replace("state->pids != conn->state.pids", "(uint8_t)state->pids != (uint8_t)conn->state.pids")),
    ("conn_pids_to_zero_is_no_change", T, F, PIDS, PIDS.replace("state->pids != conn->state.pids", "state->pids != conn->state.pids && state->pids != 0")),
    ("conn_pids_from_zero_is_no_change", T, F, PIDS, PIDS.replace("state->pids != conn->state.pids", "state->pids != conn->state.pids && conn->state.pids != 0")),
    ("conn_result_number_compared_in_16_bit", T, F, WANT_RESULT, WANT_RESULT.replace("state->dtc.result_seq != conn->fetched_result_seq", "(uint16_t)state->dtc.result_seq != (uint16_t)conn->fetched_result_seq")),
    ("conn_result_number_zero_in_16_bit", T, F, WANT_RESULT, WANT_RESULT.replace("state->dtc.result_seq != 0 &&", "(uint16_t)state->dtc.result_seq != 0 &&")),
    ("conn_result_number_stored_in_16_bit", T, F, "\tconn->fetched_result_seq = conn->state.dtc.result_seq;", "\tconn->fetched_result_seq = (uint16_t)conn->state.dtc.result_seq;"),
    ("conn_result_only_of_request_over_http", T, F, WANT_RESULT, WANT_RESULT.replace("state->dtc.result_seq != 0 &&", "state->dtc.result_seq != 0 && state->dtc.from_http &&")),
    ("conn_result_only_of_last_request", T, F, WANT_RESULT, WANT_RESULT.replace("state->dtc.result_seq != 0 &&", "state->dtc.result_seq != 0 && state->dtc.result_seq == state->dtc.seq &&")),
    ("conn_result_only_with_codes", T, F, WANT_RESULT, WANT_RESULT.replace("state->dtc.result_seq != 0 &&", "state->dtc.result_seq != 0 && state->dtc.count != 0 &&")),
    ("conn_values_only_with_pids", T, F, WANT_VALUES, WANT_VALUES.replace("state->ecu_online", "state->ecu_online && state->pids != 0")),
    ("conn_values_with_unknown_autopid", T, F, WANT_VALUES, WANT_VALUES.replace("state->autopid == WICAN_AUTOPID_RUN", "state->autopid != WICAN_AUTOPID_OFF && state->autopid != WICAN_AUTOPID_STARTING")),
    ("conn_dtc_allowed_with_unknown_autopid", T, F, ALLOWED, ALLOWED.replace("conn->state.autopid == WICAN_AUTOPID_RUN", "conn->state.autopid != WICAN_AUTOPID_OFF && conn->state.autopid != WICAN_AUTOPID_STARTING")),
    ("conn_uptime_compared_in_8_bit", T, F, ALLOWED, ALLOWED.replace("conn->state.up_s >= CONN_DTC_MIN_UP_S", "(uint8_t)conn->state.up_s >= CONN_DTC_MIN_UP_S")),
    ("conn_id_written_with_padding", T, F, "\tstrcpy(id, conn->bound_id);", "\tstrncpy(id, conn->bound_id, size);"),

    # news to take
    ("conn_restart_reported_again", T, F, "\tconn->restarted = false;\n", ""),
    ("conn_restart_never_handed_out", T, F, "\treturn restarted;", "\treturn restarted && false;"),
    ("conn_id_handed_out_without_binding", T, F, TAKE_BIND, TAKE_BIND.replace("!conn->bind_pending || ", "")),
    ("conn_id_written_to_null", T, F, TAKE_BIND, TAKE_BIND.replace("id == NULL || ", "")),
    ("conn_id_without_room_for_the_zero", T, F, TAKE_BIND, TAKE_BIND.replace("strlen(conn->bound_id) >= size", "strlen(conn->bound_id) > size")),
    ("conn_id_room_one_byte_more_needed", T, F, TAKE_BIND, TAKE_BIND.replace("strlen(conn->bound_id) >= size", "strlen(conn->bound_id) + 1 >= size")),
    ("conn_id_without_room", T, F, TAKE_BIND, "\t(void)size;\n" + TAKE_BIND.replace(" || strlen(conn->bound_id) >= size", "")),
    ("conn_id_not_written", T, F, "\tstrcpy(id, conn->bound_id);\n", ""),
    ("conn_id_handed_out_again", T, F, "\tconn->bind_pending = false;\n\treturn true;", "\treturn true;"),
    ("conn_id_refused_is_dropped", T, F, TAKE_BIND, "\tif(conn->bind_pending && id != NULL && strlen(conn->bound_id) >= size) conn->bind_pending = false;\n" + TAKE_BIND),
]
