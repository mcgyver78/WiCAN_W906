"""Mutations of display/components/core/dtc_flow.c, see ../redproof.py."""

F = "components/core/dtc_flow.c"
H = "components/core/dtc_flow.h"
T = "test_dtc_flow"

PASSED = "\treturn now_ms > since_ms ? now_ms - since_ms : 0;"
CLEARING = "\treturn flow->phase == DTC_FLOW_CLEAR_SENT || flow->phase == DTC_FLOW_CLEARING;"
UNDER_WAY = "\treturn flow->phase == DTC_FLOW_READ_SENT || flow->phase == DTC_FLOW_READING || clearing(flow);"
UNANSWERED = "\treturn (flow->phase == DTC_FLOW_READ_SENT || flow->phase == DTC_FLOW_CLEAR_SENT) && flow->posted;"
REASON = "\twhile(reason != NULL && length + 1 < sizeof(flow->reason) && reason[length] != '\\0')"
GIVE_UP = "\tflow->to_send = DTC_FLOW_SEND_NOTHING;\n\tif(clearing(flow)) flow->phase = DTC_FLOW_UNKNOWN;\n\telse fail(flow, reason);\n"
DROP = "\tflow->phase = DTC_FLOW_IDLE;\n\tflow->read_seq = 0;\n\tflow->list_count = 0;\n\tflow->list_end_ms = 0;\n"
INIT = "\tmemset(flow, 0, sizeof(*flow));\n"

B_ADAPTER = "\tif(view == CONN_VIEW_NO_WIFI || view == CONN_VIEW_CONNECTING || view == CONN_VIEW_NO_ANSWER) return DTC_FLOW_NO_ADAPTER;\n"
B_FOREIGN = "\tif(view == CONN_VIEW_FOREIGN) return DTC_FLOW_FOREIGN;\n"
B_NO_API = "\tif(view == CONN_VIEW_NO_API) return DTC_FLOW_NO_API;\n"
B_OFF = "\tif(view == CONN_VIEW_AUTOPID_OFF) return DTC_FLOW_AUTOPID_OFF;\n"
B_STARTING = "\t// Also while AutoPID is starting: conn allows no command then\n\tif(!conn_dtc_allowed(conn)) return DTC_FLOW_STARTING;\n"
B_SUPPORTED = "\t// From here on there is a state: conn_dtc_allowed() needs one\n\tif(!conn_state(conn)->dtc.supported) return DTC_FLOW_NOT_SUPPORTED;\n"
B_BUSY = "\tif(view == CONN_VIEW_SCAN || under_way(flow)) return DTC_FLOW_BUSY;\n"
B_OFFLINE = "\tif(view == CONN_VIEW_ECU_OFFLINE) return DTC_FLOW_ECU_OFFLINE;\n"

E_CATALOG = "\tif(catalog_find(catalog, DTC_FLOW_RPM_NAME) < 0) return DTC_FLOW_ALLOWED;\n"
E_FRESH = "\tfresh = values_age(value, now_ms) == VALUE_AGE_FRESH && value->kind == VALUE_NUMBER;"
E_RUNNING = "\tif(fresh && !(value->number < DTC_FLOW_RPM_LIMIT)) return DTC_FLOW_ENGINE_RUNNING;\n"
E_UNKNOWN = "\tif(!fresh || (newer && value->seen_ms <= than_ms)) return DTC_FLOW_RPM_UNKNOWN;\n"
READ_BLOCK = "\treturn block != DTC_FLOW_ALLOWED ? block : engine_block(values, catalog, false, 0, now_ms);"

C_ADAPTER = "\tif(block != DTC_FLOW_ALLOWED) return block;\n\n\t// The list is void"
C_LIST = "\tlist = flow->phase == DTC_FLOW_LIST && state->boot == flow->boot && state->dtc.seq == flow->read_seq;\n"
C_ENGINE = "\tblock = engine_block(values, catalog, list, flow->list_end_ms, now_ms);\n\tif(block != DTC_FLOW_ALLOWED) return block;\n\n"
C_NO_LIST = "\tif(!list) return DTC_FLOW_NO_LIST;\n"
C_OLD = "\tif(passed(now_ms, flow->list_end_ms) > DTC_FLOW_LIST_MS) return DTC_FLOW_LIST_OLD;\n"
C_CODES = "\tif(flow->list_count == 0) return DTC_FLOW_NO_CODES;\n"
C_STUCK = "\tif(button_stuck) return DTC_FLOW_BUTTON_STUCK;\n"

BEGIN = ("\tflow->phase = phase;\n\tflow->to_send = send;\n\tflow->boot = state->boot;\n\tflow->seq_before = state->dtc.seq;\n"
         "\tflow->seq = 0;\n\tflow->posted = false;\n\tflow->rounds_without_answer = 0;\n")
READ = ("\tif(block != DTC_FLOW_ALLOWED) return block;\n\n\tdrop_list(flow);\n"
        "\tbegin(flow, conn_state(conn), DTC_FLOW_READ_SENT, DTC_FLOW_SEND_READ);\n")
CLEAR = "\tif(block != DTC_FLOW_ALLOWED) return block;\n\n\tbegin(flow, conn_state(conn), DTC_FLOW_CLEAR_SENT, DTC_FLOW_SEND_CLEAR);\n"
CLEAR_ASKS = "\tdtc_flow_block_t block = dtc_flow_clear_block(flow, conn, values, catalog, button_stuck, now_ms);"

TAKE_FIRST = "\tdtc_flow_send_t send = flow->to_send;\n\n\tflow->to_send = DTC_FLOW_SEND_NOTHING;\n"
TAKE_LATE = ("\tif(send == DTC_FLOW_SEND_CLEAR && passed(now_ms, flow->list_end_ms) > DTC_FLOW_LIST_MS)\n"
             "\t{\n\t\tflow->phase = DTC_FLOW_LIST;\n\t\tsend = DTC_FLOW_SEND_NOTHING;\n\t}\n")
TAKE_SEQ = "\tif(seq != NULL) *seq = send == DTC_FLOW_SEND_CLEAR ? flow->read_seq : 0;"

P_MATCH = "\tif((flow->phase != DTC_FLOW_READ_SENT && flow->phase != DTC_FLOW_CLEAR_SENT) || flow->to_send != DTC_FLOW_SEND_NOTHING || flow->posted) return;"
P_ACCEPTED = "\tif(status == 202 && seq != 0)"
P_PHASE = "\t\tflow->phase = clearing(flow) ? DTC_FLOW_CLEARING : DTC_FLOW_READING;"
P_REFUSED = "\telse if(status != 0 && status != 202)\n\t{\n\t\tfail(flow, reason);\n\t}"

S_IGNORED = "\tif(state == NULL || flow->phase == DTC_FLOW_FAILED || flow->phase == DTC_FLOW_UNKNOWN) return;"
S_BOOT = "\tif(state->boot != flow->boot)"
S_RESTART = "\t\tif(under_way(flow)) give_up(flow, \"restarted\");\n\t\telse drop_list(flow);\n"
S_SAME = "\t\tif(state->dtc.seq == flow->seq_before)"
S_ROUNDS = "\t\t\tif(++flow->rounds_without_answer < DTC_FLOW_NO_ANSWER_ROUNDS) return;"
S_NOT_ARRIVED = "\t\t\tif(clear) flow->phase = DTC_FLOW_LIST;\n\t\t\telse fail(flow, \"no_answer\");\n\t\t\treturn;\n"
S_OTHERS = "\t\tif(state->dtc.seq == 0 || !state->dtc.has_request || !state->dtc.from_http || state->dtc.clear != clear)"
S_NOT_FOUND = S_OTHERS + "\n\t\t{\n\t\t\tgive_up(flow, \"superseded\");\n\t\t\treturn;\n\t\t}\n"
S_ADOPT = "\t\tflow->seq = state->dtc.seq;\n\t\tflow->phase = clear ? DTC_FLOW_CLEARING : DTC_FLOW_READING;\n"
S_ACCEPTED = "\tif(flow->phase == DTC_FLOW_READING || flow->phase == DTC_FLOW_CLEARING)"
S_OWN = "\t\tif(state->dtc.seq == flow->seq)\n\t\t{\n\t\t\tif(state->dtc.phase == WICAN_DTC_ERROR) fail(flow, state->dtc.reason);\n\t\t}\n"
S_COMMENT = "\t\t// A later request. The own one is over; its result may still be there to be fetched.\n"
S_LATER = "\t\telse if(state->dtc.result_seq != flow->seq)\n\t\t{\n\t\t\tgive_up(flow, \"superseded\");\n\t\t}\n"
S_LIST = "\telse if(flow->phase == DTC_FLOW_LIST && state->dtc.seq != flow->read_seq)\n\t{\n\t\tdrop_list(flow);\n\t}\n"

R_AGE = "\tuint64_t age_ms = (uint64_t)age_s * 1000;\n\n\tif(result_seq != flow->seq) return;\n"
R_READ = "\tif(flow->phase == DTC_FLOW_READING && !clear)"
R_LIST = ("\t\tflow->phase = DTC_FLOW_LIST;\n\t\tflow->read_seq = result_seq;\n\t\tflow->list_count = count;\n"
          "\t\tflow->list_end_ms = passed(now_ms, age_ms);\n")
R_CLEAR = "\telse if(flow->phase == DTC_FLOW_CLEARING && clear)"

LOST = "\tif(under_way(flow)) give_up(flow, \"no_answer\");"
DISMISS = "\tif(!under_way(flow)) drop_list(flow);"
LEFT = "\tif(flow->phase != DTC_FLOW_LIST || age_ms >= DTC_FLOW_LIST_MS) return 0;"
ROUNDED = "\treturn (uint32_t)((DTC_FLOW_LIST_MS - age_ms + 999) / 1000);"

MUTATIONS = [
    # time
    ("flow_time_steps_back_with_the_caller", T, F, PASSED, "\treturn now_ms - since_ms;"),

    # which phases are a request under way
    ("flow_clear_sent_is_no_clear", T, F, CLEARING, "\treturn flow->phase == DTC_FLOW_CLEARING;"),
    ("flow_clearing_is_no_clear", T, F, CLEARING, "\treturn flow->phase == DTC_FLOW_CLEAR_SENT;"),
    ("flow_read_sent_not_under_way", T, F, UNDER_WAY, UNDER_WAY.replace("flow->phase == DTC_FLOW_READ_SENT || ", "")),
    ("flow_reading_not_under_way", T, F, UNDER_WAY, UNDER_WAY.replace("flow->phase == DTC_FLOW_READING || ", "")),
    ("flow_clear_not_under_way", T, F, UNDER_WAY, UNDER_WAY.replace(" || clearing(flow)", "")),
    ("flow_list_is_under_way", T, F, UNDER_WAY, UNDER_WAY.replace("clearing(flow)", "clearing(flow) || flow->phase == DTC_FLOW_LIST")),
    ("flow_failure_is_under_way", T, F, UNDER_WAY, UNDER_WAY.replace("clearing(flow)", "clearing(flow) || flow->phase == DTC_FLOW_FAILED")),
    ("flow_post_under_way_is_unanswered", T, F, UNANSWERED, UNANSWERED.replace(" && flow->posted", "")),
    ("flow_unanswered_read_not_followed", T, F, UNANSWERED, "\treturn flow->phase == DTC_FLOW_CLEAR_SENT && flow->posted;"),
    ("flow_unanswered_clear_not_followed", T, F, UNANSWERED, "\treturn flow->phase == DTC_FLOW_READ_SENT && flow->posted;"),

    # a failure and its reason
    ("flow_reason_not_stored", T, F, "\t\tflow->reason[length] = reason[length];\n", ""),
    ("flow_reason_cut_one_byte_early", T, F, REASON, REASON.replace("length + 1 < sizeof(flow->reason)", "length + 2 < sizeof(flow->reason)")),
    ("flow_reason_first_byte_only", T, F, REASON, REASON.replace("length + 1 < sizeof(flow->reason)", "length < 1")),
    ("flow_reason_cut_at_space", T, F, REASON, REASON.replace("reason[length] != '\\0'", "reason[length] != '\\0' && reason[length] != ' '")),
    ("flow_reason_cut_at_byte_above_127", T, F, REASON, REASON.replace("reason[length] != '\\0'", "reason[length] != '\\0' && (reason[length] & 0x80) == 0")),
    ("flow_missing_reason_keeps_the_old", T, F, REASON, "\tif(reason == NULL) length = strlen(flow->reason);\n" + REASON),
    ("flow_reason_not_ended", T, F, "\tflow->reason[length] = '\\0';\n\tflow->phase = DTC_FLOW_FAILED;", "\tflow->phase = DTC_FLOW_FAILED;"),
    ("flow_failure_keeps_phase", T, F, "\tflow->reason[length] = '\\0';\n\tflow->phase = DTC_FLOW_FAILED;", "\tflow->reason[length] = '\\0';"),

    # a request that cannot be followed any more
    ("flow_withdrawn_request_handed_out", T, F, GIVE_UP, GIVE_UP.replace("\tflow->to_send = DTC_FLOW_SEND_NOTHING;\n", "")),
    ("flow_given_up_clear_is_a_failure", T, F, GIVE_UP, "\tflow->to_send = DTC_FLOW_SEND_NOTHING;\n\tfail(flow, reason);\n"),
    ("flow_given_up_read_is_unknown", T, F, GIVE_UP, "\t(void)reason;\n\tflow->to_send = DTC_FLOW_SEND_NOTHING;\n\tflow->phase = DTC_FLOW_UNKNOWN;\n"),
    ("flow_given_up_clear_back_to_list", T, F, GIVE_UP, GIVE_UP.replace("flow->phase = DTC_FLOW_UNKNOWN;", "flow->phase = DTC_FLOW_LIST;")),
    ("flow_given_up_clear_is_idle", T, F, GIVE_UP, GIVE_UP.replace("flow->phase = DTC_FLOW_UNKNOWN;", "flow->phase = DTC_FLOW_IDLE;")),

    # dropping the list
    ("flow_drop_keeps_phase", T, F, DROP, DROP.replace("\tflow->phase = DTC_FLOW_IDLE;\n", "")),
    ("flow_drop_keeps_number", T, F, DROP, DROP.replace("\tflow->read_seq = 0;\n", "")),
    ("flow_drop_keeps_count", T, F, DROP, DROP.replace("\tflow->list_count = 0;\n", "")),
    ("flow_drop_keeps_time", T, F, DROP, DROP.replace("\tflow->list_end_ms = 0;\n", "")),

    # init
    ("flow_init_does_nothing", T, F, INIT, "\t(void)flow;\n"),
    ("flow_init_keeps_request", T, F, INIT, "\tdtc_flow_send_t send = flow->to_send;\n\n" + INIT + "\tflow->to_send = send;\n"),
    ("flow_init_keeps_list", T, F, INIT, "\tuint32_t read_seq = flow->read_seq;\n\n" + INIT + "\tflow->read_seq = read_seq;\n"),
    ("flow_init_keeps_count", T, F, INIT, "\tuint32_t count = flow->list_count;\n\n" + INIT + "\tflow->list_count = count;\n"),
    ("flow_init_only_phase_and_request", T, F, INIT, "\tflow->phase = DTC_FLOW_IDLE;\n\tflow->to_send = DTC_FLOW_SEND_NOTHING;\n\tflow->read_seq = 0;\n\tflow->list_count = 0;\n"),
    ("flow_init_keeps_phase", T, F, INIT, "\tdtc_flow_phase_t phase = flow->phase;\n\n" + INIT + "\tflow->phase = phase;\n"),

    # what stands against a command: the adapter
    ("flow_no_wifi_is_not_no_adapter", T, F, B_ADAPTER, B_ADAPTER.replace("view == CONN_VIEW_NO_WIFI || ", "")),
    ("flow_connecting_is_not_no_adapter", T, F, B_ADAPTER, B_ADAPTER.replace("view == CONN_VIEW_CONNECTING || ", "")),
    ("flow_no_answer_is_not_no_adapter", T, F, B_ADAPTER, B_ADAPTER.replace(" || view == CONN_VIEW_NO_ANSWER", "")),
    ("flow_foreign_not_named", T, F, B_FOREIGN, ""),
    ("flow_no_api_not_named", T, F, B_NO_API, ""),
    ("flow_autopid_off_not_named", T, F, B_OFF, ""),
    ("flow_commands_before_conn_allows", T, F, B_STARTING, ""),
    ("flow_starting_only_by_view", T, F, B_STARTING, B_STARTING.replace("!conn_dtc_allowed(conn)", "view == CONN_VIEW_STARTING")),
    ("flow_without_fault_memory_table", T, F, B_SUPPORTED, ""),
    ("flow_foreign_scan_not_busy", T, F, B_BUSY, B_BUSY.replace("view == CONN_VIEW_SCAN || ", "")),
    ("flow_own_request_not_busy", T, F, B_BUSY, B_BUSY.replace("under_way(flow)", "(under_way(flow) && false)")),
    ("flow_never_busy", T, F, B_BUSY, "\t(void)flow;\n"),
    ("flow_with_ignition_off", T, F, B_OFFLINE, ""),
    ("flow_starting_before_foreign", T, F, B_FOREIGN + B_NO_API + B_OFF + B_STARTING, B_STARTING + B_FOREIGN + B_NO_API + B_OFF),
    ("flow_starting_before_no_api", T, F, B_NO_API + B_OFF + B_STARTING, B_STARTING + B_NO_API + B_OFF),
    ("flow_starting_before_autopid_off", T, F, B_OFF + B_STARTING, B_STARTING + B_OFF),
    ("flow_profile_before_starting", T, F, B_STARTING + B_SUPPORTED, B_SUPPORTED + B_STARTING),
    ("flow_busy_before_profile", T, F, B_SUPPORTED + B_BUSY, B_BUSY + B_SUPPORTED),
    ("flow_busy_before_starting", T, F, B_STARTING + B_SUPPORTED + B_BUSY, B_BUSY + B_STARTING + B_SUPPORTED),
    ("flow_ignition_before_busy", T, F, B_BUSY + B_OFFLINE, B_OFFLINE + B_BUSY),
    ("flow_ignition_before_profile", T, F, B_SUPPORTED + B_BUSY + B_OFFLINE, B_OFFLINE + B_SUPPORTED + B_BUSY),

    # what stands against a command: the engine
    ("flow_engine_speed_asked_without_catalog_entry", T, F, E_CATALOG, "\t(void)catalog;\n"),
    ("flow_old_engine_speed_is_fresh", T, F, E_FRESH, E_FRESH.replace("== VALUE_AGE_FRESH", "!= VALUE_AGE_GONE")),
    ("flow_switch_is_a_speed", T, F, E_FRESH, E_FRESH.replace(" && value->kind == VALUE_NUMBER", "")),
    ("flow_off_is_a_speed", T, F, E_FRESH, E_FRESH.replace("value->kind == VALUE_NUMBER", "value->kind != VALUE_ON")),
    ("flow_on_is_a_speed", T, F, E_FRESH, E_FRESH.replace("value->kind == VALUE_NUMBER", "value->kind != VALUE_OFF")),
    ("flow_running_engine_not_seen", T, F, E_RUNNING, ""),
    ("flow_engine_stands_at_the_limit", T, F, E_RUNNING, E_RUNNING.replace("!(value->number < DTC_FLOW_RPM_LIMIT)", "!(value->number <= DTC_FLOW_RPM_LIMIT)")),
    ("flow_no_number_is_an_engine_that_stands", T, F, E_RUNNING, E_RUNNING.replace("!(value->number < DTC_FLOW_RPM_LIMIT)", "value->number >= DTC_FLOW_RPM_LIMIT")),
    ("flow_rpm_limit_higher", T, H, "#define DTC_FLOW_RPM_LIMIT      50.0", "#define DTC_FLOW_RPM_LIMIT      50.005"),
    ("flow_rpm_limit_lower", T, H, "#define DTC_FLOW_RPM_LIMIT      50.0", "#define DTC_FLOW_RPM_LIMIT      49.995"),
    ("flow_rpm_name_changed", T, H, "#define DTC_FLOW_RPM_NAME       \"ENGINE_RPM\"", "#define DTC_FLOW_RPM_NAME       \"ENGINE_RPm\""),
    ("flow_old_running_engine_is_running", T, F, E_RUNNING,
     E_RUNNING.replace("if(fresh && ", "if(value != NULL && value->kind == VALUE_NUMBER && values_age(value, now_ms) != VALUE_AGE_GONE && ")),
    ("flow_old_engine_speed_allows", T, F, E_UNKNOWN, E_UNKNOWN.replace("!fresh || ", "value == NULL || value->kind != VALUE_NUMBER || ")),
    ("flow_engine_speed_from_before_the_read", T, F, E_UNKNOWN, E_UNKNOWN.replace("value->seen_ms <= than_ms", "value->seen_ms <= than_ms && false")),
    ("flow_engine_speed_from_the_end_of_the_read", T, F, E_UNKNOWN, E_UNKNOWN.replace("value->seen_ms <= than_ms", "value->seen_ms < than_ms")),
    ("flow_engine_speed_one_ms_later_needed", T, F, E_UNKNOWN, E_UNKNOWN.replace("value->seen_ms <= than_ms", "value->seen_ms <= than_ms + 1")),
    ("flow_engine_speed_always_newer_than", T, F, E_UNKNOWN, E_UNKNOWN.replace("(newer && ", "((newer || !newer) && ")),
    ("flow_unknown_before_running", T, F, E_RUNNING + E_UNKNOWN, E_UNKNOWN + E_RUNNING),
    ("flow_read_without_engine", T, F, READ_BLOCK, "\t(void)values;\n\t(void)catalog;\n\treturn block;"),
    ("flow_read_engine_before_ignition", T, F, READ_BLOCK,
     "\treturn block != DTC_FLOW_ALLOWED && block != DTC_FLOW_ECU_OFFLINE ? block : engine_block(values, catalog, false, 0, now_ms);"),

    # what stands against a clear
    ("flow_clear_during_scan", T, F, C_ADAPTER, C_ADAPTER.replace("block != DTC_FLOW_ALLOWED", "block != DTC_FLOW_ALLOWED && block != DTC_FLOW_BUSY")),
    ("flow_clear_with_ignition_off", T, F, C_ADAPTER, C_ADAPTER.replace("block != DTC_FLOW_ALLOWED", "block != DTC_FLOW_ALLOWED && block != DTC_FLOW_ECU_OFFLINE")),
    ("flow_clear_before_conn_allows", T, F, C_ADAPTER, C_ADAPTER.replace("block != DTC_FLOW_ALLOWED", "block != DTC_FLOW_ALLOWED && block != DTC_FLOW_STARTING")),
    ("flow_clear_without_fault_memory_table", T, F, C_ADAPTER, C_ADAPTER.replace("block != DTC_FLOW_ALLOWED", "block != DTC_FLOW_ALLOWED && block != DTC_FLOW_NOT_SUPPORTED")),
    ("flow_list_in_any_phase", T, F, C_LIST, C_LIST.replace("flow->phase == DTC_FLOW_LIST && ", "flow->read_seq != 0 && ")),
    ("flow_list_after_restart", T, F, C_LIST, C_LIST.replace("state->boot == flow->boot && ", "")),
    ("flow_list_after_foreign_request", T, F, C_LIST, C_LIST.replace(" && state->dtc.seq == flow->read_seq", "")),
    ("flow_list_by_result_number", T, F, C_LIST, C_LIST.replace("state->dtc.seq == flow->read_seq", "state->dtc.result_seq == flow->read_seq")),
    ("flow_clear_engine_as_for_a_read", T, F, C_ENGINE, C_ENGINE.replace("catalog, list, flow->list_end_ms", "catalog, false, flow->list_end_ms")),
    ("flow_clear_engine_newer_without_list", T, F, C_ENGINE, C_ENGINE.replace("catalog, list, flow->list_end_ms", "catalog, true, flow->list_end_ms")),
    ("flow_clear_engine_newer_than_nothing", T, F, C_ENGINE, C_ENGINE.replace("catalog, list, flow->list_end_ms", "catalog, list, 0")),
    ("flow_clear_without_engine", T, F, C_ENGINE, "\t(void)values;\n\t(void)catalog;\n"),
    ("flow_clear_without_list", T, F, C_NO_LIST, ""),
    ("flow_no_list_before_engine", T, F, C_ENGINE + C_NO_LIST, C_NO_LIST + C_ENGINE),
    ("flow_old_list_cleared", T, F, C_OLD, ""),
    ("flow_list_old_one_ms_early", T, F, C_OLD, C_OLD.replace("> DTC_FLOW_LIST_MS", ">= DTC_FLOW_LIST_MS")),
    ("flow_list_old_one_ms_late", T, F, C_OLD, C_OLD.replace("> DTC_FLOW_LIST_MS", "> DTC_FLOW_LIST_MS + 1")),
    ("flow_list_time_shorter", T, H, "#define DTC_FLOW_LIST_MS        (600u * 1000u)", "#define DTC_FLOW_LIST_MS        (600u * 1000u - 1u)"),
    ("flow_list_time_longer", T, H, "#define DTC_FLOW_LIST_MS        (600u * 1000u)", "#define DTC_FLOW_LIST_MS        (600u * 1000u + 1u)"),
    ("flow_old_list_before_engine", T, F, C_ENGINE + C_NO_LIST + C_OLD, "\tif(list && passed(now_ms, flow->list_end_ms) > DTC_FLOW_LIST_MS) return DTC_FLOW_LIST_OLD;\n" + C_ENGINE + C_NO_LIST),
    ("flow_old_list_before_no_list", T, F, C_NO_LIST + C_OLD, C_OLD + C_NO_LIST),
    ("flow_empty_list_cleared", T, F, C_CODES, ""),
    ("flow_codes_counted_by_state", T, F, C_CODES, C_CODES.replace("flow->list_count == 0", "state->dtc.count == 0")),
    ("flow_list_with_one_code_not_cleared", T, F, C_CODES, C_CODES.replace("flow->list_count == 0", "flow->list_count <= 1")),
    ("flow_no_codes_before_old_list", T, F, C_OLD + C_CODES, C_CODES + C_OLD),
    ("flow_clear_with_stuck_button", T, F, C_STUCK, "\t(void)button_stuck;\n"),
    ("flow_stuck_button_before_no_codes", T, F, C_CODES + C_STUCK, C_STUCK + C_CODES),
    ("flow_stuck_button_before_no_list", T, F, C_NO_LIST + C_OLD + C_CODES + C_STUCK, C_STUCK + C_NO_LIST + C_OLD + C_CODES),
    ("flow_reasons_in_another_order", T, H, "\tDTC_FLOW_NO_LIST,           // clear: no list of an own read (phase is not LIST)\n\tDTC_FLOW_LIST_OLD, ",
     "\tDTC_FLOW_LIST_OLD,\n\tDTC_FLOW_NO_LIST, "),

    # a request begins
    ("flow_begin_keeps_phase", T, F, BEGIN, BEGIN.replace("\tflow->phase = phase;\n", "\t(void)phase;\n")),
    ("flow_begin_nothing_to_send", T, F, BEGIN, BEGIN.replace("\tflow->to_send = send;\n", "\t(void)send;\n")),
    ("flow_begin_boot_not_noted", T, F, BEGIN, BEGIN.replace("\tflow->boot = state->boot;\n", "")),
    ("flow_begin_number_not_noted", T, F, BEGIN, BEGIN.replace("\tflow->seq_before = state->dtc.seq;\n", "")),
    ("flow_begin_result_number_noted", T, F, BEGIN, BEGIN.replace("flow->seq_before = state->dtc.seq;", "flow->seq_before = state->dtc.result_seq;")),
    ("flow_begin_keeps_own_number", T, F, BEGIN, BEGIN.replace("\tflow->seq = 0;\n", "")),
    ("flow_begin_keeps_answer", T, F, BEGIN, BEGIN.replace("\tflow->posted = false;\n", "")),
    ("flow_begin_keeps_states_counted", T, F, BEGIN, BEGIN.replace("\tflow->rounds_without_answer = 0;\n", "")),
    ("flow_read_whatever_stands_against", T, F, READ, READ.replace("\tif(block != DTC_FLOW_ALLOWED) return block;\n\n", "\tif(block == DTC_FLOW_ENGINE_RUNNING) block = DTC_FLOW_ALLOWED;\n\tif(block != DTC_FLOW_ALLOWED) return block;\n\n")),
    ("flow_refused_read_drops_list", T, F, READ, "\tdrop_list(flow);\n\tif(block != DTC_FLOW_ALLOWED) return block;\n\n\tbegin(flow, conn_state(conn), DTC_FLOW_READ_SENT, DTC_FLOW_SEND_READ);\n"),
    ("flow_read_keeps_list", T, F, READ, READ.replace("\tdrop_list(flow);\n", "")),
    ("flow_read_in_phase_of_clear", T, F, READ, READ.replace("DTC_FLOW_READ_SENT, DTC_FLOW_SEND_READ", "DTC_FLOW_CLEAR_SENT, DTC_FLOW_SEND_READ")),
    ("flow_read_sends_clear", T, F, READ, READ.replace("DTC_FLOW_READ_SENT, DTC_FLOW_SEND_READ", "DTC_FLOW_READ_SENT, DTC_FLOW_SEND_CLEAR")),
    ("flow_clear_whatever_stands_against", T, F, CLEAR, CLEAR.replace("\tif(block != DTC_FLOW_ALLOWED) return block;\n\n", "\tif(block == DTC_FLOW_NO_CODES) block = DTC_FLOW_ALLOWED;\n\tif(block != DTC_FLOW_ALLOWED) return block;\n\n")),
    ("flow_clear_of_old_list_sent", T, F, CLEAR, CLEAR.replace("\tif(block != DTC_FLOW_ALLOWED) return block;\n\n", "\tif(block == DTC_FLOW_LIST_OLD) block = DTC_FLOW_ALLOWED;\n\tif(block != DTC_FLOW_ALLOWED) return block;\n\n")),
    ("flow_clear_sent_although_reported_refused", T, F, CLEAR, "\tif(block != DTC_FLOW_ALLOWED && block != DTC_FLOW_BUTTON_STUCK) return block;\n\n\tbegin(flow, conn_state(conn), DTC_FLOW_CLEAR_SENT, DTC_FLOW_SEND_CLEAR);\n\tif(block != DTC_FLOW_ALLOWED) return block;\n"),
    ("flow_clear_drops_list", T, F, CLEAR, CLEAR.replace("\tbegin(flow,", "\tdrop_list(flow);\n\tbegin(flow,")),
    ("flow_clear_in_phase_of_read", T, F, CLEAR, CLEAR.replace("DTC_FLOW_CLEAR_SENT, DTC_FLOW_SEND_CLEAR", "DTC_FLOW_READ_SENT, DTC_FLOW_SEND_CLEAR")),
    ("flow_clear_sends_read", T, F, CLEAR, CLEAR.replace("DTC_FLOW_CLEAR_SENT, DTC_FLOW_SEND_CLEAR", "DTC_FLOW_CLEAR_SENT, DTC_FLOW_SEND_READ")),
    ("flow_clear_does_not_ask_the_button", T, F, CLEAR_ASKS, CLEAR_ASKS.replace("button_stuck, now_ms", "false && button_stuck, now_ms")),

    # handing out
    ("flow_request_handed_out_again", T, F, TAKE_FIRST, "\tdtc_flow_send_t send = flow->to_send;\n\n"),
    ("flow_nothing_handed_out", T, F, TAKE_FIRST, "\tdtc_flow_send_t send = DTC_FLOW_SEND_NOTHING;\n\n\tflow->to_send = DTC_FLOW_SEND_NOTHING;\n"),
    ("flow_nothing_without_place_for_number", T, F, TAKE_FIRST, "\tdtc_flow_send_t send = seq != NULL ? flow->to_send : DTC_FLOW_SEND_NOTHING;\n\n\tflow->to_send = DTC_FLOW_SEND_NOTHING;\n"),
    ("flow_late_clear_handed_out", T, F, TAKE_LATE, "\t(void)now_ms;\n"),
    ("flow_late_clear_one_ms_early", T, F, TAKE_LATE, TAKE_LATE.replace("> DTC_FLOW_LIST_MS", ">= DTC_FLOW_LIST_MS")),
    ("flow_late_clear_one_ms_late", T, F, TAKE_LATE, TAKE_LATE.replace("> DTC_FLOW_LIST_MS", "> DTC_FLOW_LIST_MS + 1")),
    ("flow_late_read_not_handed_out", T, F, TAKE_LATE, TAKE_LATE.replace("send == DTC_FLOW_SEND_CLEAR && ", "send != DTC_FLOW_SEND_NOTHING && ")),
    ("flow_late_clear_stays_under_way", T, F, TAKE_LATE, TAKE_LATE.replace("\t\tflow->phase = DTC_FLOW_LIST;\n", "")),
    ("flow_late_clear_drops_list", T, F, TAKE_LATE, TAKE_LATE.replace("\t\tflow->phase = DTC_FLOW_LIST;\n", "\t\tdrop_list(flow);\n")),
    ("flow_late_clear_is_a_failure", T, F, TAKE_LATE, TAKE_LATE.replace("\t\tflow->phase = DTC_FLOW_LIST;\n", "\t\tflow->phase = DTC_FLOW_FAILED;\n")),
    ("flow_late_clear_handed_out_from_list", T, F, TAKE_LATE, TAKE_LATE.replace("\t\tsend = DTC_FLOW_SEND_NOTHING;\n", "")),
    ("flow_late_clear_waits_on", T, F, TAKE_LATE, TAKE_LATE.replace("\t\tsend = DTC_FLOW_SEND_NOTHING;\n", "\t\tflow->to_send = send;\n\t\tsend = DTC_FLOW_SEND_NOTHING;\n")),
    ("flow_late_clear_by_time_of_caller_only", T, F, TAKE_LATE, TAKE_LATE.replace("passed(now_ms, flow->list_end_ms) > DTC_FLOW_LIST_MS", "now_ms > DTC_FLOW_LIST_MS")),
    ("flow_clear_without_number", T, F, TAKE_SEQ, "\tif(seq != NULL) *seq = 0;"),
    ("flow_number_of_list_with_everything", T, F, TAKE_SEQ, "\tif(seq != NULL) *seq = flow->read_seq;"),
    ("flow_number_not_written", T, F, TAKE_SEQ, "\t(void)seq;"),
    ("flow_number_only_with_clear", T, F, TAKE_SEQ, "\tif(seq != NULL && send == DTC_FLOW_SEND_CLEAR) *seq = flow->read_seq;"),
    ("flow_clear_with_number_of_request", T, F, TAKE_SEQ, TAKE_SEQ.replace("flow->read_seq : 0", "flow->seq : 0")),
    ("flow_clear_with_number_plus_one", T, F, TAKE_SEQ, TAKE_SEQ.replace("flow->read_seq : 0", "flow->read_seq + 1 : 0")),

    # the answer to the POST
    ("flow_answer_in_any_phase", T, F, P_MATCH, "\tif(flow->to_send != DTC_FLOW_SEND_NOTHING || flow->posted) return;"),
    ("flow_answer_to_read_ignored", T, F, P_MATCH, P_MATCH.replace("(flow->phase != DTC_FLOW_READ_SENT && flow->phase != DTC_FLOW_CLEAR_SENT)", "flow->phase != DTC_FLOW_CLEAR_SENT")),
    ("flow_answer_to_clear_ignored", T, F, P_MATCH, P_MATCH.replace("(flow->phase != DTC_FLOW_READ_SENT && flow->phase != DTC_FLOW_CLEAR_SENT)", "flow->phase != DTC_FLOW_READ_SENT")),
    ("flow_answer_before_request_taken", T, F, P_MATCH, P_MATCH.replace(" || flow->to_send != DTC_FLOW_SEND_NOTHING", "")),
    ("flow_second_answer_counts", T, F, P_MATCH, P_MATCH.replace(" || flow->posted", "")),
    ("flow_answer_not_noted", T, F, "\tflow->posted = true;\n", ""),
    ("flow_accepted_without_number", T, F, P_ACCEPTED, "\tif(status == 202)"),
    ("flow_any_status_with_number_accepted", T, F, P_ACCEPTED, "\tif(seq != 0)"),
    ("flow_200_accepted", T, F, P_ACCEPTED, "\tif((status == 200 || status == 202) && seq != 0)"),
    ("flow_all_2xx_accepted", T, F, P_ACCEPTED, "\tif(status >= 200 && status < 300 && seq != 0)"),
    ("flow_accepted_only_with_higher_number", T, F, P_ACCEPTED, "\tif(status == 202 && seq > flow->seq_before)"),
    ("flow_accepted_number_not_stored", T, F, "\t\tflow->seq = seq;\n", ""),
    ("flow_accepted_clear_is_reading", T, F, P_PHASE, "\t\tflow->phase = DTC_FLOW_READING;"),
    ("flow_accepted_read_is_clearing", T, F, P_PHASE, "\t\tflow->phase = DTC_FLOW_CLEARING;"),
    ("flow_accepted_stays_sent", T, F, P_PHASE + "\n", ""),
    ("flow_no_answer_is_a_failure", T, F, P_REFUSED, P_REFUSED.replace("status != 0 && status != 202", "status != 202")),
    ("flow_202_without_number_is_a_failure", T, F, P_REFUSED, P_REFUSED.replace("status != 0 && status != 202", "status != 0")),
    ("flow_negative_status_is_no_answer", T, F, P_REFUSED, P_REFUSED.replace("status != 0 && status != 202", "status > 0 && status != 202")),
    ("flow_only_4xx_and_5xx_are_refusals", T, F, P_REFUSED, P_REFUSED.replace("status != 0 && status != 202", "status >= 400")),
    ("flow_refusal_ignored", T, F, P_REFUSED, P_REFUSED.replace("fail(flow, reason);", "(void)reason;")),
    ("flow_refused_clear_back_to_list", T, F, P_REFUSED, P_REFUSED.replace("fail(flow, reason);", "if(clearing(flow)) flow->phase = DTC_FLOW_LIST;\n\t\telse fail(flow, reason);")),
    ("flow_refused_clear_drops_list", T, F, P_REFUSED, P_REFUSED.replace("fail(flow, reason);", "if(clearing(flow)) drop_list(flow);\n\t\tfail(flow, reason);")),
    ("flow_refused_clear_is_unknown", T, F, P_REFUSED, P_REFUSED.replace("fail(flow, reason);", "give_up(flow, reason);")),
    ("flow_refusal_without_reason_of_body", T, F, P_REFUSED, P_REFUSED.replace("fail(flow, reason);", "fail(flow, reason != NULL && reason[0] != '\\0' ? \"refused\" : reason);")),

    # the states: what is ignored, a restart
    ("flow_missing_state_is_a_lost_adapter", T, F, S_IGNORED,
     "\tif(state == NULL)\n\t{\n\t\tif(under_way(flow)) give_up(flow, \"no_answer\");\n\t\telse drop_list(flow);\n\t\treturn;\n\t}\n\tif(flow->phase == DTC_FLOW_FAILED || flow->phase == DTC_FLOW_UNKNOWN) return;"),
    ("flow_failure_dropped_by_restart", T, F, S_IGNORED, S_IGNORED.replace(" || flow->phase == DTC_FLOW_FAILED", "")),
    ("flow_unknown_dropped_by_restart", T, F, S_IGNORED, S_IGNORED.replace(" || flow->phase == DTC_FLOW_UNKNOWN", "")),
    ("flow_cleared_ignores_states", T, F, S_IGNORED, S_IGNORED.replace("flow->phase == DTC_FLOW_UNKNOWN", "flow->phase == DTC_FLOW_UNKNOWN || flow->phase == DTC_FLOW_CLEARED")),
    ("flow_list_ignores_states", T, F, S_IGNORED, S_IGNORED.replace("flow->phase == DTC_FLOW_UNKNOWN", "flow->phase == DTC_FLOW_UNKNOWN || flow->phase == DTC_FLOW_LIST")),
    ("flow_restart_not_seen", T, F, S_BOOT, "\tif(0)"),
    ("flow_restart_only_to_higher_boot_number", T, F, S_BOOT, "\tif(state->boot > flow->boot)"),
    ("flow_restart_only_to_lower_boot_number", T, F, S_BOOT, "\tif(state->boot < flow->boot)"),
    ("flow_restart_not_seen_with_same_number", T, F, S_BOOT, "\tif(state->boot != flow->boot && (!under_way(flow) || state->dtc.seq != flow->seq))"),
    ("flow_restart_request_dropped_silently", T, F, S_RESTART, "\t\tdrop_list(flow);\n"),
    ("flow_restart_keeps_list_and_outcome", T, F, S_RESTART, "\t\tif(under_way(flow)) give_up(flow, \"restarted\");\n"),
    ("flow_restart_keeps_outcome_of_clear", T, F, S_RESTART, "\t\tif(under_way(flow)) give_up(flow, \"restarted\");\n\t\telse if(flow->phase == DTC_FLOW_LIST) drop_list(flow);\n"),
    ("flow_restart_keeps_list", T, F, S_RESTART, "\t\tif(under_way(flow)) give_up(flow, \"restarted\");\n\t\telse if(flow->phase == DTC_FLOW_CLEARED) drop_list(flow);\n"),
    ("flow_restart_only_of_accepted_requests", T, F, S_RESTART,
     "\t\tif(flow->phase == DTC_FLOW_READING || flow->phase == DTC_FLOW_CLEARING) give_up(flow, \"restarted\");\n\t\telse if(!under_way(flow)) drop_list(flow);\n"),
    ("flow_restart_reason_changed", T, F, S_RESTART, S_RESTART.replace("\"restarted\"", "\"no_answer\"")),

    # the states: a POST without an answer
    ("flow_number_of_before_never_decides", T, F, S_ROUNDS, "\t\t\tif(++flow->rounds_without_answer > 0) return;"),
    ("flow_states_not_counted", T, F, S_ROUNDS, "\t\t\tif(flow->rounds_without_answer + 1 < DTC_FLOW_NO_ANSWER_ROUNDS) return;"),
    ("flow_one_state_more_needed", T, F, S_ROUNDS, S_ROUNDS.replace("< DTC_FLOW_NO_ANSWER_ROUNDS", "<= DTC_FLOW_NO_ANSWER_ROUNDS")),
    ("flow_no_answer_rounds_fewer", T, H, "#define DTC_FLOW_NO_ANSWER_ROUNDS 2 ", "#define DTC_FLOW_NO_ANSWER_ROUNDS 1 "),
    ("flow_no_answer_rounds_more", T, H, "#define DTC_FLOW_NO_ANSWER_ROUNDS 2 ", "#define DTC_FLOW_NO_ANSWER_ROUNDS 3 "),
    ("flow_clear_not_arrived_is_a_failure", T, F, S_NOT_ARRIVED, "\t\t\tfail(flow, \"no_answer\");\n\t\t\treturn;\n"),
    ("flow_clear_not_arrived_is_unknown", T, F, S_NOT_ARRIVED, S_NOT_ARRIVED.replace("if(clear) flow->phase = DTC_FLOW_LIST;", "if(clear) flow->phase = DTC_FLOW_UNKNOWN;")),
    ("flow_clear_not_arrived_drops_list", T, F, S_NOT_ARRIVED, S_NOT_ARRIVED.replace("if(clear) flow->phase = DTC_FLOW_LIST;", "if(clear) drop_list(flow);")),
    ("flow_read_not_arrived_is_a_list", T, F, S_NOT_ARRIVED, "\t\t\tflow->phase = DTC_FLOW_LIST;\n\t\t\treturn;\n"),
    ("flow_read_not_arrived_is_idle", T, F, S_NOT_ARRIVED, S_NOT_ARRIVED.replace("else fail(flow, \"no_answer\");", "else flow->phase = DTC_FLOW_IDLE;")),
    ("flow_not_arrived_reason_changed", T, F, S_NOT_ARRIVED, S_NOT_ARRIVED.replace("\"no_answer\"", "\"superseded\"")),
    ("flow_not_arrived_then_followed_on", T, F, S_NOT_ARRIVED, S_NOT_ARRIVED.replace("\t\t\treturn;\n", "")),
    ("flow_number_of_before_is_number_of_list", T, F, S_SAME, "\t\tif(state->dtc.seq == flow->read_seq)"),
    ("flow_lower_number_counts_as_before", T, F, S_SAME, "\t\tif(state->dtc.seq <= flow->seq_before)"),
    ("flow_number_zero_is_a_request", T, F, S_OTHERS, S_OTHERS.replace("state->dtc.seq == 0 || ", "")),
    ("flow_request_without_action_is_own", T, F, S_OTHERS, S_OTHERS.replace("!state->dtc.has_request || ", "")),
    ("flow_request_over_mqtt_is_own", T, F, S_OTHERS, S_OTHERS.replace("!state->dtc.from_http || ", "")),
    ("flow_request_with_other_action_is_own", T, F, S_OTHERS, S_OTHERS.replace(" || state->dtc.clear != clear", "")),
    ("flow_only_next_number_is_own", T, F, S_OTHERS, S_OTHERS.replace("state->dtc.seq == 0 || ", "state->dtc.seq != flow->seq_before + 1 || ")),
    ("flow_foreign_request_reason_changed", T, F, S_NOT_FOUND, S_NOT_FOUND.replace("\"superseded\"", "\"no_answer\"")),
    ("flow_foreign_request_is_a_failure_for_clear", T, F, S_NOT_FOUND, S_NOT_FOUND.replace("give_up(flow, \"superseded\");", "flow->to_send = DTC_FLOW_SEND_NOTHING;\n\t\t\tfail(flow, \"superseded\");")),
    ("flow_foreign_request_sends_clear_back_to_list", T, F, S_NOT_FOUND, S_NOT_FOUND.replace("give_up(flow, \"superseded\");", "if(clear) flow->phase = DTC_FLOW_LIST;\n\t\t\telse give_up(flow, \"superseded\");")),
    ("flow_foreign_request_adopted_after_all", T, F, S_NOT_FOUND, S_NOT_FOUND.replace("\t\t\treturn;\n", "")),
    ("flow_found_number_not_stored", T, F, S_ADOPT, S_ADOPT.replace("\t\tflow->seq = state->dtc.seq;\n", "")),
    ("flow_found_number_is_result_number", T, F, S_ADOPT, S_ADOPT.replace("flow->seq = state->dtc.seq;", "flow->seq = state->dtc.result_seq;")),
    ("flow_found_clear_is_reading", T, F, S_ADOPT, S_ADOPT.replace("clear ? DTC_FLOW_CLEARING : DTC_FLOW_READING", "DTC_FLOW_READING")),
    ("flow_found_read_is_clearing", T, F, S_ADOPT, S_ADOPT.replace("clear ? DTC_FLOW_CLEARING : DTC_FLOW_READING", "DTC_FLOW_CLEARING")),
    ("flow_found_request_stays_sent", T, F, S_ADOPT, S_ADOPT.replace("\t\tflow->phase = clear ? DTC_FLOW_CLEARING : DTC_FLOW_READING;\n", "")),

    # the states: an accepted request, a list
    ("flow_accepted_read_not_followed", T, F, S_ACCEPTED, "\tif(flow->phase == DTC_FLOW_CLEARING)"),
    ("flow_accepted_clear_not_followed", T, F, S_ACCEPTED, "\tif(flow->phase == DTC_FLOW_READING)"),
    ("flow_error_of_own_request_ignored", T, F, S_OWN, S_OWN.replace("\t\t\tif(state->dtc.phase == WICAN_DTC_ERROR) fail(flow, state->dtc.reason);\n", "")),
    ("flow_error_of_own_clear_is_unknown", T, F, S_OWN, S_OWN.replace("fail(flow, state->dtc.reason)", "give_up(flow, state->dtc.reason)")),
    ("flow_error_without_reason_of_state", T, F, S_OWN, S_OWN.replace("fail(flow, state->dtc.reason)", "fail(flow, \"\")")),
    ("flow_error_without_reason_ignored", T, F, S_OWN, S_OWN.replace("state->dtc.phase == WICAN_DTC_ERROR", "state->dtc.phase == WICAN_DTC_ERROR && state->dtc.reason[0] != '\\0'")),
    ("flow_error_only_from_http", T, F, S_OWN, S_OWN.replace("state->dtc.phase == WICAN_DTC_ERROR", "state->dtc.phase == WICAN_DTC_ERROR && state->dtc.from_http")),
    ("flow_done_is_an_error", T, F, S_OWN, S_OWN.replace("state->dtc.phase == WICAN_DTC_ERROR", "state->dtc.phase >= WICAN_DTC_DONE")),
    ("flow_idle_is_an_error", T, F, S_OWN, S_OWN.replace("state->dtc.phase == WICAN_DTC_ERROR", "(state->dtc.phase == WICAN_DTC_ERROR || state->dtc.phase == WICAN_DTC_IDLE)")),
    ("flow_error_of_any_request_is_own", T, F, S_OWN + S_COMMENT + S_LATER,
     "\t\tif(state->dtc.phase == WICAN_DTC_ERROR)\n\t\t{\n\t\t\tfail(flow, state->dtc.reason);\n\t\t}\n"
     "\t\telse if(state->dtc.seq != flow->seq && state->dtc.result_seq != flow->seq)\n\t\t{\n\t\t\tgive_up(flow, \"superseded\");\n\t\t}\n"),
    ("flow_later_request_always_supersedes", T, F, S_LATER, "\t\telse\n\t\t{\n\t\t\tgive_up(flow, \"superseded\");\n\t\t}\n"),
    ("flow_later_request_never_supersedes", T, F, S_LATER, ""),
    ("flow_later_request_any_result_waited_for", T, F, S_LATER, S_LATER.replace("state->dtc.result_seq != flow->seq", "state->dtc.result_seq == 0")),
    ("flow_later_request_only_higher_number", T, F, S_LATER, S_LATER.replace("else if(state->dtc.result_seq != flow->seq)", "else if(state->dtc.seq > flow->seq && state->dtc.result_seq != flow->seq)")),
    ("flow_superseded_only_by_mqtt", T, F, S_LATER, S_LATER.replace("state->dtc.result_seq != flow->seq)", "state->dtc.result_seq != flow->seq && !state->dtc.from_http)")),
    ("flow_superseded_only_with_other_result", T, F, S_LATER, S_LATER.replace("state->dtc.result_seq != flow->seq)", "state->dtc.result_seq != flow->seq && state->dtc.result_seq != 0)")),
    ("flow_superseded_reason_changed", T, F, S_LATER, S_LATER.replace("\"superseded\"", "\"restarted\"")),
    ("flow_superseded_clear_is_a_failure", T, F, S_LATER, S_LATER.replace("give_up(flow, \"superseded\");", "fail(flow, \"superseded\");")),
    ("flow_list_outlasts_foreign_request", T, F, S_LIST, ""),
    ("flow_list_dropped_only_by_higher_number", T, F, S_LIST, S_LIST.replace("state->dtc.seq != flow->read_seq", "state->dtc.seq > flow->read_seq")),
    ("flow_list_dropped_by_result_number", T, F, S_LIST, S_LIST.replace("state->dtc.seq != flow->read_seq", "state->dtc.result_seq != flow->read_seq")),
    ("flow_list_dropped_only_by_running_scan", T, F, S_LIST,
     S_LIST.replace("state->dtc.seq != flow->read_seq", "state->dtc.seq != flow->read_seq && state->dtc.phase != WICAN_DTC_DONE && state->dtc.phase != WICAN_DTC_ERROR")),
    ("flow_cleared_dropped_by_other_request", T, F, S_LIST, S_LIST.replace("flow->phase == DTC_FLOW_LIST && state->dtc.seq != flow->read_seq", "flow->phase != DTC_FLOW_IDLE && state->dtc.seq != flow->seq")),
    ("flow_foreign_request_is_a_failure_for_list", T, F, S_LIST, S_LIST.replace("drop_list(flow);", "fail(flow, \"superseded\");")),

    # a result
    ("flow_result_age_in_32_bit", T, F, R_AGE, R_AGE.replace("(uint64_t)age_s * 1000", "age_s * 1000")),
    ("flow_result_age_in_ms", T, F, R_AGE, R_AGE.replace("(uint64_t)age_s * 1000", "age_s")),
    ("flow_result_with_any_number", T, F, R_AGE, R_AGE.replace("\tif(result_seq != flow->seq) return;\n", "")),
    ("flow_result_with_number_of_before", T, F, R_AGE, R_AGE.replace("result_seq != flow->seq", "result_seq != flow->seq && result_seq != flow->seq_before")),
    ("flow_result_of_clear_makes_list", T, F, R_READ, "\tif(flow->phase == DTC_FLOW_READING)"),
    ("flow_result_before_read_accepted", T, F, R_READ, "\tif((flow->phase == DTC_FLOW_READING || flow->phase == DTC_FLOW_READ_SENT) && !clear)"),
    ("flow_result_in_any_phase_makes_list", T, F, R_READ, "\tif(!clearing(flow) && !clear)"),
    ("flow_result_makes_no_list", T, F, R_LIST, R_LIST.replace("\t\tflow->phase = DTC_FLOW_LIST;\n", "")),
    ("flow_list_number_not_stored", T, F, R_LIST, R_LIST.replace("\t\tflow->read_seq = result_seq;\n", "")),
    ("flow_list_count_not_stored", T, F, R_LIST, R_LIST.replace("\t\tflow->list_count = count;\n", "\t\t(void)count;\n")),
    ("flow_list_time_not_stored", T, F, R_LIST, R_LIST.replace("\t\tflow->list_end_ms = passed(now_ms, age_ms);\n", "\t\t(void)now_ms;\n\t\t(void)age_ms;\n")),
    ("flow_list_ended_when_result_came", T, F, R_LIST, R_LIST.replace("passed(now_ms, age_ms)", "now_ms + 0 * age_ms")),
    ("flow_list_ended_before_zero", T, F, R_LIST, R_LIST.replace("passed(now_ms, age_ms)", "now_ms - age_ms")),
    ("flow_list_end_later_by_age", T, F, R_LIST, R_LIST.replace("passed(now_ms, age_ms)", "now_ms + age_ms")),
    ("flow_result_of_read_is_cleared", T, F, R_CLEAR, "\telse if(flow->phase == DTC_FLOW_CLEARING)"),
    ("flow_result_before_clear_accepted", T, F, R_CLEAR, "\telse if(clearing(flow) && clear)"),
    ("flow_cleared_only_without_age", T, F, R_CLEAR, "\telse if(flow->phase == DTC_FLOW_CLEARING && clear && age_s == 0)"),
    ("flow_list_only_with_count_of_16_bit", T, F, R_READ, "\tif(flow->phase == DTC_FLOW_READING && !clear && count < 65536)"),
    ("flow_result_never_cleared", T, F, "\t\tflow->phase = DTC_FLOW_CLEARED;\n", ""),
    ("flow_cleared_goes_idle", T, F, "\t\tflow->phase = DTC_FLOW_CLEARED;\n", "\t\tflow->phase = DTC_FLOW_IDLE;\n"),

    # large numbers and times beyond 32 bit
    ("flow_restart_boot_compared_in_16_bit", T, F, S_BOOT, "\tif((uint16_t)state->boot != (uint16_t)flow->boot)"),
    ("flow_list_boot_compared_in_16_bit", T, F, C_LIST, C_LIST.replace("state->boot == flow->boot", "(uint16_t)state->boot == (uint16_t)flow->boot")),
    ("flow_list_number_compared_in_16_bit", T, F, C_LIST, C_LIST.replace("state->dtc.seq == flow->read_seq", "(uint16_t)state->dtc.seq == (uint16_t)flow->read_seq")),
    ("flow_number_before_compared_in_16_bit", T, F, S_SAME, "\t\tif((uint16_t)state->dtc.seq == (uint16_t)flow->seq_before)"),
    ("flow_own_number_compared_in_16_bit", T, F, S_OWN, S_OWN.replace("state->dtc.seq == flow->seq", "(uint16_t)state->dtc.seq == (uint16_t)flow->seq")),
    ("flow_later_result_compared_in_16_bit", T, F, S_LATER, S_LATER.replace("state->dtc.result_seq != flow->seq", "(uint16_t)state->dtc.result_seq != (uint16_t)flow->seq")),
    ("flow_list_followed_in_16_bit", T, F, S_LIST, S_LIST.replace("state->dtc.seq != flow->read_seq", "(uint16_t)state->dtc.seq != (uint16_t)flow->read_seq")),
    ("flow_result_number_compared_in_16_bit", T, F, R_AGE, R_AGE.replace("result_seq != flow->seq", "(uint16_t)result_seq != (uint16_t)flow->seq")),
    ("flow_begin_boot_stored_in_16_bit", T, F, BEGIN, BEGIN.replace("flow->boot = state->boot;", "flow->boot = (uint16_t)state->boot;")),
    ("flow_begin_number_stored_in_16_bit", T, F, BEGIN, BEGIN.replace("flow->seq_before = state->dtc.seq;", "flow->seq_before = (uint16_t)state->dtc.seq;")),
    ("flow_accepted_number_stored_in_16_bit", T, F, "\t\tflow->seq = seq;\n", "\t\tflow->seq = (uint16_t)seq;\n"),
    ("flow_accepted_number_zero_in_16_bit", T, F, P_ACCEPTED, "\tif(status == 202 && (uint16_t)seq != 0)"),
    ("flow_status_compared_in_8_bit", T, F, P_ACCEPTED, "\tif((uint8_t)status == 202 && seq != 0)"),
    ("flow_found_number_stored_in_16_bit", T, F, S_ADOPT, S_ADOPT.replace("flow->seq = state->dtc.seq;", "flow->seq = (uint16_t)state->dtc.seq;")),
    ("flow_found_number_zero_in_16_bit", T, F, S_OTHERS, S_OTHERS.replace("state->dtc.seq == 0 || ", "(uint16_t)state->dtc.seq == 0 || ")),
    ("flow_list_number_stored_in_16_bit", T, F, R_LIST, R_LIST.replace("flow->read_seq = result_seq;", "flow->read_seq = (uint16_t)result_seq;")),
    ("flow_clear_number_handed_out_in_16_bit", T, F, TAKE_SEQ, TAKE_SEQ.replace("flow->read_seq : 0", "(uint16_t)flow->read_seq : 0")),
    ("flow_codes_counted_in_16_bit", T, F, C_CODES, C_CODES.replace("flow->list_count == 0", "(uint16_t)flow->list_count == 0")),
    ("flow_result_age_in_16_bit", T, F, R_AGE, R_AGE.replace("(uint64_t)age_s * 1000", "(uint64_t)(uint16_t)age_s * 1000")),
    ("flow_list_old_counted_in_32_bit", T, F, C_OLD, C_OLD.replace("passed(now_ms, flow->list_end_ms)", "(uint32_t)passed(now_ms, flow->list_end_ms)")),
    ("flow_list_old_compared_in_32_bit", T, F, C_OLD, C_OLD.replace("passed(now_ms, flow->list_end_ms)", "passed((uint32_t)now_ms, (uint32_t)flow->list_end_ms)")),
    ("flow_late_clear_counted_in_32_bit", T, F, TAKE_LATE, TAKE_LATE.replace("passed(now_ms, flow->list_end_ms)", "(uint32_t)passed(now_ms, flow->list_end_ms)")),
    ("flow_late_clear_compared_in_32_bit", T, F, TAKE_LATE, TAKE_LATE.replace("passed(now_ms, flow->list_end_ms)", "passed((uint32_t)now_ms, (uint32_t)flow->list_end_ms)")),
    ("flow_seconds_left_counted_in_32_bit", T, F, "\tuint64_t age_ms = passed(now_ms, flow->list_end_ms);", "\tuint64_t age_ms = (uint32_t)passed(now_ms, flow->list_end_ms);"),
    ("flow_seconds_left_compared_in_32_bit", T, F, "\tuint64_t age_ms = passed(now_ms, flow->list_end_ms);", "\tuint64_t age_ms = passed((uint32_t)now_ms, (uint32_t)flow->list_end_ms);"),
    ("flow_engine_speed_time_compared_in_32_bit", T, F, E_UNKNOWN, E_UNKNOWN.replace("value->seen_ms <= than_ms", "(uint32_t)value->seen_ms <= (uint32_t)than_ms")),
    ("flow_list_end_stored_in_32_bit", T, F, R_LIST, R_LIST.replace("passed(now_ms, age_ms)", "(uint32_t)passed(now_ms, age_ms)")),

    # the list a clear came back to
    ("flow_list_dropped_only_after_an_answer", T, F, S_LIST, S_LIST.replace("state->dtc.seq != flow->read_seq", "state->dtc.seq != flow->read_seq && flow->posted")),
    ("flow_restart_drops_list_only_after_an_answer", T, F, S_RESTART, "\t\tif(under_way(flow)) give_up(flow, \"restarted\");\n\t\telse if(flow->posted || flow->phase != DTC_FLOW_LIST) drop_list(flow);\n"),
    ("flow_seconds_left_only_after_an_answer", T, F, LEFT, LEFT.replace("flow->phase != DTC_FLOW_LIST || ", "flow->phase != DTC_FLOW_LIST || !flow->posted || ")),
    ("flow_dismiss_only_after_an_answer", T, F, DISMISS, "\tif(!under_way(flow) && (flow->posted || flow->phase != DTC_FLOW_LIST)) drop_list(flow);"),

    # lost and dismissed
    ("flow_lost_adapter_ignored", T, F, LOST, "\t(void)flow;"),
    ("flow_lost_adapter_drops_list", T, F, LOST, LOST + "\n\telse drop_list(flow);"),
    ("flow_lost_adapter_only_ends_accepted", T, F, LOST, "\tif(flow->phase == DTC_FLOW_READING || flow->phase == DTC_FLOW_CLEARING) give_up(flow, \"no_answer\");"),
    ("flow_lost_adapter_only_ends_sent", T, F, LOST, "\tif(flow->phase == DTC_FLOW_READ_SENT || flow->phase == DTC_FLOW_CLEAR_SENT) give_up(flow, \"no_answer\");"),
    ("flow_lost_reason_changed", T, F, LOST, LOST.replace("\"no_answer\"", "\"restarted\"")),
    ("flow_dismissed_while_under_way", T, F, DISMISS, "\tdrop_list(flow);"),
    ("flow_dismiss_ignored", T, F, DISMISS, "\t(void)flow;"),
    ("flow_dismiss_only_a_list", T, F, DISMISS, "\tif(flow->phase == DTC_FLOW_LIST) drop_list(flow);"),
    ("flow_dismiss_not_a_failure", T, F, DISMISS, "\tif(!under_way(flow) && flow->phase != DTC_FLOW_FAILED) drop_list(flow);"),
    ("flow_dismiss_not_unknown", T, F, DISMISS, "\tif(!under_way(flow) && flow->phase != DTC_FLOW_UNKNOWN) drop_list(flow);"),
    ("flow_dismiss_not_cleared", T, F, DISMISS, "\tif(!under_way(flow) && flow->phase != DTC_FLOW_CLEARED) drop_list(flow);"),
    ("flow_dismiss_while_waiting_to_be_taken", T, F, DISMISS, "\tif(!under_way(flow) || flow->to_send != DTC_FLOW_SEND_NOTHING) drop_list(flow);"),

    # the time left
    ("flow_seconds_left_in_any_phase", T, F, LEFT, "\tif(flow->read_seq == 0 || age_ms >= DTC_FLOW_LIST_MS) return 0;"),
    ("flow_seconds_left_after_the_end", T, F, LEFT, "\tif(flow->phase != DTC_FLOW_LIST) return 0;"),
    ("flow_seconds_left_zero_in_last_second", T, F, LEFT, LEFT.replace("age_ms >= DTC_FLOW_LIST_MS", "age_ms + 1000 > DTC_FLOW_LIST_MS")),
    ("flow_seconds_left_of_empty_list_zero", T, F, LEFT, LEFT.replace("age_ms >= DTC_FLOW_LIST_MS", "age_ms >= DTC_FLOW_LIST_MS || flow->list_count == 0")),
    ("flow_seconds_left_rounded_down", T, F, ROUNDED, ROUNDED.replace(" + 999", "")),
    ("flow_seconds_left_rounded_to_nearest", T, F, ROUNDED, ROUNDED.replace(" + 999", " + 500")),
    ("flow_seconds_left_one_more", T, F, ROUNDED, ROUNDED.replace(" + 999", " + 1000")),
    ("flow_seconds_left_in_ms", T, F, ROUNDED, "\treturn (uint32_t)(DTC_FLOW_LIST_MS - age_ms);"),
    ("flow_seconds_left_time_of_caller_steps_back", T, F, "\tuint64_t age_ms = passed(now_ms, flow->list_end_ms);", "\tuint64_t age_ms = now_ms - flow->list_end_ms;"),
]
