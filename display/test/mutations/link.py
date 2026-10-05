"""Mutations of display/components/core/link.c, see ../redproof.py."""

F = "components/core/link.c"
H = "components/core/link.h"
T = "test_link"

ADVANCE = "\tif(now_ms > link->clock_ms) link->clock_ms = now_ms;"
LATER = "\treturn link->clock_ms > UINT64_MAX - ms ? UINT64_MAX : link->clock_ms + ms;"
LISTED = "\tif(profile_count < 0 || profile_count > NET_PROFILES_MAX) return 0;"
TAKE_LOOP = "\twhile(length < LINK_HOST_SIZE && text[length] != '\\0') length++;"
TAKE_CHECK = "\tif(length == 0 || length == LINK_HOST_SIZE) return false;"
TAKE_COPY = "\tmemcpy(link->host, text, length + 1);"
START_PHASE = "\tlink->phase = link->profile_count > 0 ? LINK_WAITING : LINK_IDLE;"
START_AT_ONCE = "\tlink->wait_step = 0;\n\tlink->wait_until_ms = link->clock_ms;\n"
WAIT = "\tlink->wait_until_ms = later(link, SCAN_WAITS[link->wait_step]);"
WAIT_STEP = "\tif(link->wait_step < SCAN_WAIT_COUNT - 1) link->wait_step++;"
FAILED = "\tif(link->changed) start_over(link);\n\telse if(link->tries >= LINK_JOIN_TRIES) wait_for_scan(link);"
ENDED_GUARD = "\tif(!link->finding) return;\n\n"
ENDED_TAKE = "\tif(take_host(link, host)) located(link, true);\n\telse link->find_at_ms = later(link, LINK_FIND_RETRY_MS);"
DUE_UP = "\t\tif(!link->host_from_query || !link->no_answer) return false;"
DUE_SILENT = "\t\tif(link->clock_ms - link->no_answer_since_ms < LINK_FIND_AGAIN_MS) return false;"
DUE_AT = "\treturn link->clock_ms >= link->find_at_ms;"
INIT_WANTED = "\tlink->ap_wanted = safe_mode || link->profile_count == 0;"
TIMEOUT = ("\tif(link->phase == LINK_JOINING && link->busy && link->clock_ms - link->action_since_ms >= LINK_JOIN_TIMEOUT_MS) "
           "join_failed(link);")
# Safe mode and a display without a stored profile keep the access point on: one rule (link_ap_kept()), asked
# by the idle close and by link_ap_request()
KEPT = "\treturn link->ap_forced || link->profile_count == 0;"
NOT_KEPT = "!link_ap_kept(link)"
AP_IDLE = ("\tif(link->ap_on && !link_ap_kept(link) && link->ap_clients == 0 &&\n"
           "\t   link->clock_ms - link->ap_idle_since_ms >= LINK_AP_IDLE_MS)\n\t{\n\t\tlink->ap_wanted = false;\n\t}\n")
AP_ORDER = ("\tif(link->ap_wanted != link->ap_on)\n\t{\n\t\tlink->ap_on = link->ap_wanted;\n"
            "\t\tif(!link->ap_on) return LINK_DO_AP_OFF;\n\n"
            "\t\t// It opens without a client, whatever was reported while it was closed\n"
            "\t\tlink->ap_clients = 0;\n\t\tlink->ap_idle_since_ms = link->clock_ms;\n\t\treturn LINK_DO_AP_ON;\n\t}\n")
BUSY = "\tif(link->busy) return LINK_DO_NOTHING;\n"
LEAVE = "\t\tlink->phase = LINK_LEAVING;\n\t\tlink->busy = true;\n\t\treturn LINK_DO_LEAVE;"
SCAN_DUE = "\tif(link->phase == LINK_WAITING && link->clock_ms >= link->wait_until_ms)"
SCAN = "\t\tlink->phase = LINK_SCANNING;\n\t\tlink->busy = true;\n\t\treturn LINK_DO_SCAN;"
JOIN = "\t\tlink->tries++;\n\t\tlink->action_since_ms = link->clock_ms;\n\t\tlink->busy = true;\n\t\treturn LINK_DO_JOIN;"
FIND = "\t\tlink->finding = true;\n\t\tlink->busy = true;\n\t\treturn LINK_DO_FIND;"
SCANNED_GUARD = "\tif(link->phase != LINK_SCANNING) return;\n\n\tlink->busy = false;\n"
SEEN_MAX = "\tif(seen_count > LINK_SEEN_MAX) seen_count = LINK_SEEN_MAX;\n"
CHOOSE = "\tlink->profile = net_choose(link->profiles, link->profile_count, seen, seen_count);"
CHOSEN = "\t\tlink->phase = LINK_JOINING;\n\t\tlink->tries = 0;\n"
NOT_CHOSEN = "\telse if(link->profile_count == 0) start_over(link);\n\telse wait_for_scan(link);"
JOINED_GUARD = "\tif(link->phase != LINK_JOINING || !link->busy) return;\n"
JOINED = "\tlink->phase = LINK_JOINED;\n\tlink->busy = false;\n"
JOINED_CHANGED = "\tif(link->changed) return;\n\n\tprofile = &link->profiles[link->profile];"
RULE_GIVEN = "\t\tcase NET_HOST_GIVEN:    known = take_host(link, profile->host); break;"
RULE_GATEWAY = "\t\tcase NET_HOST_GATEWAY:  known = take_host(link, gateway); break;"
RULE_QUERY = "\t\tdefault:                break;"
JOINED_UP = "\tif(known) located(link, false);\n\telse link->find_at_ms = link->clock_ms;"
JOIN_FAILED = "\tif(link->phase == LINK_JOINING && link->busy) join_failed(link);"
LOST = "\tif(link->phase == LINK_JOINED || link->phase == LINK_UP) start_over(link);"
LEFT = "\tif(link->phase == LINK_LEAVING) start_over(link);"
SILENT = "\tif(!answering && !link->no_answer) link->no_answer_since_ms = link->clock_ms;"
PROFILES_TAKEN = ("\tlink->profiles = profiles;\n\tlink->profile_count = listed(profile_count);\n"
                  "\tif(link->profile_count == 0) link->ap_wanted = true;\n")
IN_NETWORK = "\tif(link->phase == LINK_JOINED || link->phase == LINK_UP || (link->phase == LINK_JOINING && link->busy))"
VOID = ("\t\tlink->changed = true;\n\t\tlink->profile = -1;\n\t\tlink->host[0] = '\\0';\n"
        "\t\tif(link->phase == LINK_UP) link->phase = LINK_JOINED;\n")
PROFILES_SCANNING = "\telse if(link->phase == LINK_SCANNING) link->wait_step = 0;"
PROFILES_ELSE = "\telse if(link->phase != LINK_LEAVING) start_over(link);"
REQUEST = "\tlink->ap_wanted = on || link_ap_kept(link);"
CLIENTS_NEGATIVE = "\tif(clients < 0) clients = 0;\n"
CLIENTS_LEFT = "\tif(clients == 0 && link->ap_clients > 0) link->ap_idle_since_ms = link->clock_ms;"


def without_time(function, body):
    """The call of advance() at the start of one function, by the text that follows it."""
    return ("link_time_of_%s_ignored" % function, T, F, "\tadvance(link, now_ms);\n" + body, "\t(void)now_ms;\n" + body)


MUTATIONS = [
    # the time
    ("link_time_steps_back_with_the_caller", T, F, ADVANCE, "\tlink->clock_ms = now_ms;"),
    ("link_time_stands", T, F, ADVANCE, "\t(void)link;\n\t(void)now_ms;"),
    ("link_time_only_steps_back", T, F, ADVANCE, "\tif(now_ms < link->clock_ms) link->clock_ms = now_ms;"),
    without_time("next", "\n\t// Before everything else"),
    without_time("scanned", "\tif(link->phase != LINK_SCANNING) return;"),
    without_time("joined", "\tif(link->phase != LINK_JOINING || !link->busy) return;"),
    without_time("join_failed", JOIN_FAILED),
    without_time("lost", "\t// A query under way is given up"),
    without_time("left", LEFT),
    without_time("found", "\tquery_ended(link, host);"),
    without_time("not_found", "\tquery_ended(link, NULL);"),
    without_time("answering", "\t// The silence counts"),
    without_time("profiles", "\tlink->profiles = profiles;\n\tlink->profile_count = listed(profile_count);\n\tif("),
    without_time("ap_request", REQUEST),
    without_time("ap_clients", CLIENTS_NEGATIVE),
    ("link_time_of_init_ignored", T, F, "\tlink->clock_ms = now_ms;\n\tlink->profiles = profiles;", "\t(void)now_ms;\n\tlink->profiles = profiles;"),
    ("link_wait_wraps_behind_the_largest_time", T, F, LATER, "\treturn link->clock_ms + ms;"),
    ("link_wait_behind_the_largest_time_ends_at_once", T, F, LATER, LATER.replace("? UINT64_MAX :", "? link->clock_ms :")),

    # the list of profiles
    ("link_count_of_5_is_a_list", T, F, LISTED, LISTED.replace("> NET_PROFILES_MAX", "> NET_PROFILES_MAX + 1")),
    ("link_count_of_4_is_no_list", T, F, LISTED, LISTED.replace("> NET_PROFILES_MAX", ">= NET_PROFILES_MAX")),
    ("link_negative_count_is_a_list", T, F, LISTED, "\tif(profile_count > NET_PROFILES_MAX) return 0;"),
    ("link_count_above_limit_is_full_list", T, F, LISTED, LISTED.replace("return 0;", "return profile_count < 0 ? 0 : NET_PROFILES_MAX;")),
    ("link_init_count_not_checked", T, F,
     "\tlink->profile_count = listed(profile_count);\n\tlink->ap_forced", "\tlink->profile_count = profile_count;\n\tlink->ap_forced"),
    ("link_profiles_count_not_checked", T, F,
     "\tlink->profile_count = listed(profile_count);\n\tif(link->profile_count == 0)", "\tlink->profile_count = profile_count;\n\tif(link->profile_count == 0)"),
    ("link_init_list_not_stored", T, F, "\tlink->profiles = profiles;\n\tlink->profile_count = listed(profile_count);\n\tlink->ap_forced",
     "\t(void)profiles;\n\tlink->profile_count = listed(profile_count);\n\tlink->ap_forced"),

    # an address
    ("link_host_null_not_checked", T, F, "\tif(text == NULL) return false;\n", ""),
    ("link_host_of_39_refused", T, F, TAKE_CHECK, TAKE_CHECK.replace("length == LINK_HOST_SIZE", "length >= LINK_HOST_SIZE - 1")),
    ("link_host_of_41_taken", T, F, TAKE_LOOP, TAKE_LOOP.replace("length < LINK_HOST_SIZE", "length <= LINK_HOST_SIZE")),
    ("link_host_too_long_taken", T, F, TAKE_CHECK, "\tif(length == 0) return false;"),
    ("link_host_too_long_cut", T, F, TAKE_CHECK + "\n\n" + TAKE_COPY,
     "\tif(length == 0) return false;\n\tif(length == LINK_HOST_SIZE) length--;\n\n\tmemcpy(link->host, text, length);\n\tlink->host[length] = '\\0';"),
    ("link_host_empty_taken", T, F, TAKE_CHECK, "\tif(length == LINK_HOST_SIZE) return false;"),
    ("link_host_of_one_byte_refused", T, F, TAKE_CHECK, TAKE_CHECK.replace("length == 0", "length <= 1")),
    ("link_host_end_not_copied", T, F, TAKE_COPY, "\tmemcpy(link->host, text, length);"),
    ("link_host_first_byte_only", T, F, TAKE_COPY, "\tlink->host[0] = text[0];\n\tlink->host[1] = '\\0';"),
    ("link_host_taken_but_refused", T, F, TAKE_COPY + "\n\treturn true;", TAKE_COPY + "\n\treturn false;"),
    ("link_host_high_bit_lost", T, F, TAKE_COPY, TAKE_COPY + "\n\tfor(length = 0; link->host[length] != '\\0'; length++) link->host[length] &= 0x7F;"),

    # starting over
    ("link_start_scans_without_profile", T, F, START_PHASE, "\tlink->phase = LINK_WAITING;"),
    ("link_start_idle_with_profiles", T, F, START_PHASE, "\tlink->phase = LINK_IDLE;"),
    ("link_start_idle_with_one_profile", T, F, START_PHASE, START_PHASE.replace("profile_count > 0", "profile_count > 1")),
    ("link_start_keeps_profile", T, F, START_PHASE + "\n\tlink->profile = -1;\n", START_PHASE + "\n"),
    ("link_start_keeps_action", T, F, "\tlink->profile = -1;\n\tlink->busy = false;\n\tlink->finding = false;", "\tlink->profile = -1;\n\tlink->finding = false;"),
    ("link_start_keeps_query", T, F, "\tlink->busy = false;\n\tlink->finding = false;\n\tlink->changed = false;", "\tlink->busy = false;\n\tlink->changed = false;"),
    ("link_start_keeps_leave_due", T, F, "\tlink->finding = false;\n\tlink->changed = false;\n", "\tlink->finding = false;\n"),
    ("link_start_keeps_host", T, F, "\tlink->host[0] = '\\0';\n\tlink->wait_step = 0;", "\tlink->wait_step = 0;"),
    ("link_start_keeps_wait", T, F, START_AT_ONCE, "\tlink->wait_until_ms = link->clock_ms;\n"),
    ("link_start_scan_not_at_once", T, F, START_AT_ONCE, "\tlink->wait_step = 0;\n"),
    ("link_start_scan_after_first_wait", T, F, START_AT_ONCE, "\tlink->wait_step = 0;\n\tlink->wait_until_ms = later(link, SCAN_WAITS[0]);\n"),

    # the waits
    ("link_wait_always_the_first", T, F, WAIT, WAIT.replace("SCAN_WAITS[link->wait_step]", "SCAN_WAITS[0]")),
    ("link_wait_always_the_last", T, F, WAIT, WAIT.replace("SCAN_WAITS[link->wait_step]", "SCAN_WAITS[SCAN_WAIT_COUNT - 1]")),
    ("link_wait_none", T, F, WAIT, WAIT.replace("SCAN_WAITS[link->wait_step]", "0 * SCAN_WAITS[link->wait_step]")),
    ("link_wait_does_not_grow", T, F, WAIT_STEP + "\n", ""),
    ("link_wait_grows_behind_the_table", T, F, WAIT_STEP, WAIT_STEP.replace("SCAN_WAIT_COUNT - 1", "SCAN_WAIT_COUNT")),
    ("link_wait_stops_growing_early", T, F, WAIT_STEP, WAIT_STEP.replace("SCAN_WAIT_COUNT - 1", "SCAN_WAIT_COUNT - 2")),
    ("link_wait_starts_anew_after_the_last", T, F, WAIT_STEP, "\tlink->wait_step = (link->wait_step + 1) % SCAN_WAIT_COUNT;"),
    ("link_wait_first_changed", T, H, "{2000u, 5000u, 10000u, 30000u}", "{1999u, 5000u, 10000u, 30000u}"),
    ("link_wait_second_changed", T, H, "{2000u, 5000u, 10000u, 30000u}", "{2000u, 5001u, 10000u, 30000u}"),
    ("link_wait_third_changed", T, H, "{2000u, 5000u, 10000u, 30000u}", "{2000u, 5000u, 9999u, 30000u}"),
    ("link_wait_last_changed", T, H, "{2000u, 5000u, 10000u, 30000u}", "{2000u, 5000u, 10000u, 30001u}"),
    ("link_wait_one_more_step", T, H, "{2000u, 5000u, 10000u, 30000u}", "{2000u, 5000u, 10000u, 30000u, 30000u}"),
    ("link_wait_keeps_profile", T, F, "\tlink->phase = LINK_WAITING;\n\tlink->profile = -1;\n", "\tlink->phase = LINK_WAITING;\n"),
    ("link_wait_phase_not_set", T, F, "\tlink->phase = LINK_WAITING;\n\tlink->profile = -1;\n", "\tlink->profile = -1;\n"),

    # a join that failed
    ("link_join_one_attempt", T, F, FAILED, FAILED.replace("else if(link->tries >= LINK_JOIN_TRIES) wait_for_scan(link);", "else wait_for_scan(link);")),
    ("link_join_three_attempts", T, F, FAILED, FAILED.replace("tries >= LINK_JOIN_TRIES", "tries > LINK_JOIN_TRIES")),
    ("link_join_attempts_without_end", T, F, FAILED, "\tif(link->changed) start_over(link);"),
    ("link_join_tries_changed", T, H, "#define LINK_JOIN_TRIES         2", "#define LINK_JOIN_TRIES         3"),
    ("link_join_tries_one", T, H, "#define LINK_JOIN_TRIES         2", "#define LINK_JOIN_TRIES         1"),
    ("link_join_failed_ignores_changed_list", T, F, FAILED, "\tif(link->tries >= LINK_JOIN_TRIES) wait_for_scan(link);"),
    ("link_join_failed_changed_list_waits", T, F, FAILED, FAILED.replace("if(link->changed) start_over(link);", "if(link->changed) wait_for_scan(link);")),
    ("link_join_failed_scans_at_once", T, F, FAILED, FAILED.replace("wait_for_scan(link);", "start_over(link);")),
    ("link_join_failed_stays_under_way", T, F, "\tlink->busy = false;\n\t// The list changed during the attempt", "\t// The list changed during the attempt"),
    ("link_join_tries_not_counted", T, F, JOIN, JOIN.replace("\t\tlink->tries++;\n", "")),
    ("link_join_tries_not_reset_by_scan", T, F, CHOSEN, "\t\tlink->phase = LINK_JOINING;\n"),
    ("link_join_handed_out_again", T, F, JOIN, JOIN.replace("\t\tlink->busy = true;\n", "")),

    # the time limit of a join
    ("link_join_timeout_never", T, F, TIMEOUT + "\n", ""),
    ("link_join_timeout_one_ms_late", T, F, TIMEOUT, TIMEOUT.replace(">= LINK_JOIN_TIMEOUT_MS", "> LINK_JOIN_TIMEOUT_MS")),
    ("link_join_timeout_shorter", T, H, "#define LINK_JOIN_TIMEOUT_MS    15000u", "#define LINK_JOIN_TIMEOUT_MS    14999u"),
    ("link_join_timeout_longer", T, H, "#define LINK_JOIN_TIMEOUT_MS    15000u", "#define LINK_JOIN_TIMEOUT_MS    15001u"),
    ("link_join_timeout_for_every_action", T, F, TIMEOUT, TIMEOUT.replace("link->phase == LINK_JOINING && link->busy &&", "link->busy &&")),
    ("link_join_timeout_counted_from_first_attempt", T, F, JOIN, JOIN.replace("\t\tlink->action_since_ms = link->clock_ms;\n", "\t\tif(link->tries == 1) link->action_since_ms = link->clock_ms;\n")),
    ("link_join_timeout_start_not_stored", T, F, JOIN, JOIN.replace("\t\tlink->action_since_ms = link->clock_ms;\n", "")),
    ("link_join_timeout_behind_the_access_point", T, F, TIMEOUT + "\n\n" + AP_IDLE + AP_ORDER, AP_IDLE + AP_ORDER + "\n" + TIMEOUT + "\n"),
    ("link_join_timeout_needs_no_other_work", T, F, TIMEOUT, TIMEOUT.replace("link->busy &&", "link->busy && link->ap_wanted == link->ap_on &&")),
    ("link_join_timeout_only_once", T, F, TIMEOUT, TIMEOUT.replace("link->busy &&", "link->busy && link->tries == 1 &&")),

    # link_next: one action at a time
    ("link_next_action_handed_out_again", T, F, BUSY, ""),
    ("link_next_access_point_waits_for_action", T, F, "\tif(link->ap_wanted != link->ap_on)\n\t{", "\tif(link->ap_wanted != link->ap_on && !link->busy)\n\t{"),
    ("link_next_leave_never", T, F, "\tif(link->changed)\n\t{", "\tif(link->changed && false)\n\t{"),
    ("link_next_leave_phase_not_set", T, F, LEAVE, LEAVE.replace("\t\tlink->phase = LINK_LEAVING;\n", "")),
    ("link_next_leave_not_under_way", T, F, LEAVE, LEAVE.replace("\t\tlink->busy = true;\n", "")),
    ("link_next_scan_one_ms_late", T, F, SCAN_DUE, SCAN_DUE.replace("clock_ms >= link->wait_until_ms", "clock_ms > link->wait_until_ms")),
    ("link_next_scan_without_wait", T, F, SCAN_DUE, "\tif(link->phase == LINK_WAITING)"),
    ("link_next_scan_in_every_phase", T, F, SCAN_DUE, "\tif(link->clock_ms >= link->wait_until_ms)"),
    ("link_next_scan_while_idle", T, F, SCAN_DUE, SCAN_DUE.replace("link->phase == LINK_WAITING &&", "(link->phase == LINK_WAITING || link->phase == LINK_IDLE) &&")),
    ("link_next_scan_while_up", T, F, SCAN_DUE, SCAN_DUE.replace("link->phase == LINK_WAITING &&", "(link->phase == LINK_WAITING || link->phase == LINK_UP) &&")),
    ("link_next_scan_phase_not_set", T, F, SCAN, SCAN.replace("\t\tlink->phase = LINK_SCANNING;\n", "")),
    ("link_next_scan_not_under_way", T, F, SCAN, SCAN.replace("\t\tlink->busy = true;\n", "")),
    ("link_next_scan_never", T, F, SCAN_DUE, SCAN_DUE.replace("link->phase == LINK_WAITING &&", "link->phase == LINK_WAITING && false &&")),
    ("link_next_join_never", T, F, "\tif(link->phase == LINK_JOINING)\n\t{", "\tif(link->phase == LINK_JOINING && false)\n\t{"),
    ("link_next_find_not_a_query", T, F, FIND, FIND.replace("\t\tlink->finding = true;\n", "")),
    ("link_next_find_handed_out_again", T, F, FIND, FIND.replace("\t\tlink->busy = true;\n", "")),
    ("link_next_find_before_leave", T, F, "\tif(link->changed)\n\t{", "\tif(link->changed && !query_due(link))\n\t{"),

    # link_profile
    ("link_profile_only_while_joining", T, F, "\treturn link->profile;", "\treturn link->phase == LINK_JOINING ? link->profile : -1;"),
    ("link_profile_not_while_up", T, F, "\treturn link->profile;", "\treturn link->phase == LINK_UP ? -1 : link->profile;"),
    ("link_profile_first_when_none", T, F, "\treturn link->profile;", "\treturn link->profile < 0 ? 0 : link->profile;"),

    # link_scanned
    ("link_scanned_taken_without_scan", T, F, SCANNED_GUARD, "\tlink->busy = false;\n"),
    ("link_scanned_taken_while_waiting", T, F, SCANNED_GUARD, SCANNED_GUARD.replace("if(link->phase != LINK_SCANNING)", "if(link->phase != LINK_SCANNING && link->phase != LINK_WAITING)")),
    ("link_scanned_stays_under_way", T, F, SCANNED_GUARD, "\tif(link->phase != LINK_SCANNING) return;\n\n"),
    ("link_scanned_looks_at_every_network", T, F, SEEN_MAX, ""),
    ("link_scanned_looks_at_21_networks", T, H, "#define LINK_SEEN_MAX           20", "#define LINK_SEEN_MAX           21"),
    ("link_scanned_looks_at_19_networks", T, H, "#define LINK_SEEN_MAX           20", "#define LINK_SEEN_MAX           19"),
    ("link_scanned_always_looks_at_20_networks", T, F, SEEN_MAX, "\tif(seen_count > 0) seen_count = LINK_SEEN_MAX;\n"),
    ("link_scanned_first_profile_only", T, F, CHOOSE, CHOOSE.replace("link->profile_count, seen", "link->profile_count > 0, seen")),
    ("link_scanned_last_profile_not_looked_at", T, F, CHOOSE, CHOOSE.replace("link->profile_count, seen", "link->profile_count - 1, seen")),
    ("link_scanned_first_network_not_looked_at", T, F, CHOOSE, CHOOSE.replace("seen, seen_count);", "seen + (seen_count > 0), seen_count - (seen_count > 0));")),
    ("link_scanned_joins_first_profile_anyway", T, F, CHOOSE, CHOOSE + "\n\tif(link->profile < 0 && seen_count > 0 && link->profile_count > 0) link->profile = 0;"),
    ("link_scanned_no_join", T, F, CHOSEN, "\t\tlink->tries = 0;\n"),
    ("link_scanned_waits_without_profile", T, F, NOT_CHOSEN, "\telse wait_for_scan(link);"),
    ("link_scanned_no_wait", T, F, NOT_CHOSEN, "\telse start_over(link);"),
    ("link_scanned_idle_with_one_profile", T, F, NOT_CHOSEN, NOT_CHOSEN.replace("profile_count == 0", "profile_count <= 1")),

    # link_joined
    ("link_joined_taken_without_join", T, F, JOINED_GUARD, ""),
    ("link_joined_taken_before_join_handed_out", T, F, JOINED_GUARD, "\tif(link->phase != LINK_JOINING) return;\n"),
    ("link_joined_taken_for_every_action", T, F, JOINED_GUARD, "\tif(!link->busy) return;\n"),
    ("link_joined_phase_not_set", T, F, JOINED, "\tlink->busy = false;\n"),
    ("link_joined_stays_under_way", T, F, JOINED, "\tlink->phase = LINK_JOINED;\n"),
    ("link_joined_ignores_changed_list", T, F, JOINED_CHANGED, "\tprofile = &link->profiles[link->profile];"),
    ("link_joined_first_profile", T, F, JOINED_CHANGED, JOINED_CHANGED.replace("&link->profiles[link->profile];", "&link->profiles[0];")),
    ("link_joined_given_host_ignored", T, F, RULE_GIVEN, RULE_GIVEN.replace("take_host(link, profile->host)", "take_host(link, gateway)")),
    ("link_joined_given_host_queried", T, F, RULE_GIVEN, RULE_GIVEN.replace("known = take_host(link, profile->host); ", "")),
    ("link_joined_gateway_ignored", T, F, RULE_GATEWAY, RULE_GATEWAY.replace("known = take_host(link, gateway); ", "(void)gateway; ")),
    ("link_joined_gateway_in_every_network", T, F, RULE_QUERY, "\t\tdefault:                known = take_host(link, gateway); break;"),
    ("link_joined_gateway_after_damaged_host", T, F, RULE_GIVEN, RULE_GIVEN.replace("take_host(link, profile->host)", "take_host(link, profile->host) || take_host(link, gateway)")),
    ("link_joined_given_counts_as_queried", T, F, JOINED_UP, JOINED_UP.replace("located(link, false)", "located(link, true)")),
    ("link_joined_never_up", T, F, JOINED_UP, JOINED_UP.replace("if(known)", "if(known && false)")),
    ("link_joined_query_not_planned", T, F, JOINED_UP, "\tif(known) located(link, false);"),
    ("link_joined_query_after_retry_time", T, F, JOINED_UP, JOINED_UP.replace("= link->clock_ms;", "= later(link, LINK_FIND_RETRY_MS);")),

    # link_join_failed, link_lost, link_left
    ("link_join_failed_taken_for_every_action", T, F, JOIN_FAILED, "\tif(link->busy) join_failed(link);"),
    ("link_join_failed_taken_while_scanning", T, F, JOIN_FAILED, "\tif((link->phase == LINK_JOINING || link->phase == LINK_SCANNING) && link->busy) join_failed(link);"),
    ("link_join_failed_never_taken", T, F, JOIN_FAILED, "\tif(link->phase == LINK_JOINING && link->busy && false) join_failed(link);"),
    ("link_lost_only_while_up", T, F, LOST, "\tif(link->phase == LINK_UP) start_over(link);"),
    ("link_lost_only_while_not_up", T, F, LOST, "\tif(link->phase == LINK_JOINED) start_over(link);"),
    ("link_lost_while_joining", T, F, LOST, LOST.replace("link->phase == LINK_UP)", "link->phase == LINK_UP || link->phase == LINK_JOINING)")),
    ("link_lost_while_leaving", T, F, LOST, LOST.replace("link->phase == LINK_UP)", "link->phase == LINK_UP || link->phase == LINK_LEAVING)")),
    ("link_lost_in_every_phase", T, F, LOST, "\tstart_over(link);"),
    ("link_lost_waits_before_scan", T, F, LOST, LOST.replace("start_over(link);", "wait_for_scan(link);")),
    ("link_lost_query_still_under_way", T, F, LOST, LOST.replace(") start_over(link);", ")\n\t{\n\t\tbool finding = link->finding;\n\n\t\tstart_over(link);\n\t\tlink->finding = finding;\n\t\tlink->busy = finding;\n\t}")),
    ("link_left_taken_without_leave", T, F, LEFT, "\tstart_over(link);"),
    ("link_left_taken_while_in_network", T, F, LEFT, "\tif(link->phase == LINK_LEAVING || link->phase == LINK_JOINED || link->phase == LINK_UP) start_over(link);"),
    ("link_left_never_taken", T, F, LEFT, "\tif(link->phase == LINK_LEAVING && false) start_over(link);"),
    ("link_left_waits_before_scan", T, F, LEFT, "\tif(link->phase == LINK_LEAVING)\n\t{\n\t\tstart_over(link);\n\t\tif(link->phase == LINK_WAITING) wait_for_scan(link);\n\t}"),

    # the query
    ("link_found_taken_without_query", T, F, ENDED_GUARD, ""),
    ("link_found_taken_while_up", T, F, ENDED_GUARD, "\tif(!link->finding && link->phase != LINK_UP) return;\n\n"),
    ("link_found_query_not_ended", T, F, ENDED_GUARD + "\tlink->finding = false;\n", ENDED_GUARD),
    ("link_found_stays_under_way", T, F, "\tlink->finding = false;\n\tlink->busy = false;\n\t// The network is about", "\tlink->finding = false;\n\t// The network is about"),
    ("link_found_ignores_changed_list", T, F, "\tif(link->changed) return;\n\n\tif(take_host", "\tif(take_host"),
    ("link_found_address_ignored", T, F, "\tquery_ended(link, host);", "\t(void)host;\n\tquery_ended(link, NULL);"),
    ("link_found_counts_as_given", T, F, ENDED_TAKE, ENDED_TAKE.replace("located(link, true)", "located(link, false)")),
    ("link_found_retry_at_once", T, F, ENDED_TAKE, ENDED_TAKE.replace("later(link, LINK_FIND_RETRY_MS)", "link->clock_ms")),
    ("link_found_retry_never_planned", T, F, ENDED_TAKE, "\tif(take_host(link, host)) located(link, true);"),
    ("link_found_retry_shorter", T, H, "#define LINK_FIND_RETRY_MS      10000u", "#define LINK_FIND_RETRY_MS      9999u"),
    ("link_found_retry_longer", T, H, "#define LINK_FIND_RETRY_MS      10000u", "#define LINK_FIND_RETRY_MS      10001u"),
    ("link_found_retry_one_ms_late", T, F, DUE_AT, "\treturn link->clock_ms > link->find_at_ms;"),
    ("link_found_retry_without_wait", T, F, DUE_AT, "\treturn true;"),
    ("link_up_phase_not_set", T, F, "\tlink->phase = LINK_UP;\n\tlink->host_from_query = from_query;", "\tlink->host_from_query = from_query;"),
    ("link_query_in_every_phase", T, F, "\telse if(link->phase != LINK_JOINED)\n\t{\n\t\treturn false;\n\t}\n", ""),
    ("link_query_while_waiting", T, F, "\telse if(link->phase != LINK_JOINED)", "\telse if(link->phase != LINK_JOINED && link->phase != LINK_WAITING)"),

    # an adapter that does not answer
    ("link_silent_every_address_queried", T, F, DUE_UP, "\t\tif(!link->no_answer) return false;"),
    ("link_silent_queried_without_silence", T, F, DUE_UP, "\t\tif(!link->host_from_query) return false;"),
    ("link_silent_never_queried", T, F, "\tlink->host_from_query = from_query;", "\tlink->host_from_query = from_query && false;"),
    ("link_silent_origin_not_stored", T, F, "\tlink->host_from_query = from_query;", "\tif(from_query) link->host_from_query = true;"),
    ("link_silent_one_ms_late", T, F, DUE_SILENT, DUE_SILENT.replace("< LINK_FIND_AGAIN_MS", "<= LINK_FIND_AGAIN_MS")),
    ("link_silent_time_shorter", T, H, "#define LINK_FIND_AGAIN_MS      60000u", "#define LINK_FIND_AGAIN_MS      59999u"),
    ("link_silent_time_longer", T, H, "#define LINK_FIND_AGAIN_MS      60000u", "#define LINK_FIND_AGAIN_MS      60001u"),
    ("link_silent_queried_at_once", T, F, DUE_SILENT + "\n", ""),
    ("link_silent_counted_from_last_report", T, F, SILENT, "\tif(!answering) link->no_answer_since_ms = link->clock_ms;"),
    ("link_silent_start_not_stored", T, F, SILENT + "\n", ""),
    ("link_silent_answer_does_not_end_it", T, F, "\tlink->no_answer = !answering;", "\tif(!answering) link->no_answer = true;"),
    ("link_silent_not_stored", T, F, "\tlink->no_answer = !answering;", "\t(void)answering;"),
    ("link_silent_reversed", T, F, "\tlink->no_answer = !answering;", "\tlink->no_answer = answering;"),
    ("link_silent_kept_over_new_address", T, F, "\t// What was reported about another address or in another network says nothing about this one\n\tlink->no_answer = false;\n", ""),
    ("link_silent_kept_over_queried_address_only", T, F, "\tlink->no_answer = false;\n}", "\tif(!from_query) link->no_answer = false;\n}"),
    ("link_silent_old_address_forgotten", T, F, FIND, FIND.replace("\t\tlink->finding = true;\n", "\t\tlink->finding = true;\n\t\tif(link->phase == LINK_UP)\n\t\t{\n\t\t\tlink->phase = LINK_JOINED;\n\t\t\tlink->host[0] = '\\0';\n\t\t}\n")),

    # link_init
    ("link_init_keeps_memory", T, F, "\tmemset(link, 0, sizeof(*link));\n", ""),
    ("link_init_safe_mode_ignored", T, F, "\tlink->ap_forced = safe_mode;", "\tlink->ap_forced = false;"),
    ("link_init_always_safe_mode", T, F, "\tlink->ap_forced = safe_mode;", "\tlink->ap_forced = true;"),
    ("link_init_no_access_point_without_profile", T, F, INIT_WANTED, "\tlink->ap_wanted = safe_mode;"),
    ("link_init_no_access_point_in_safe_mode", T, F, INIT_WANTED, "\tlink->ap_wanted = link->profile_count == 0;"),
    ("link_init_access_point_always", T, F, INIT_WANTED, "\tlink->ap_wanted = true;"),
    ("link_init_access_point_counts_as_ordered", T, F, INIT_WANTED, INIT_WANTED + "\n\tlink->ap_on = link->ap_wanted;"),

    # the own access point
    ("link_ap_never_closes", T, F, AP_IDLE, ""),
    ("link_ap_closes_one_ms_late", T, F, AP_IDLE, AP_IDLE.replace(">= LINK_AP_IDLE_MS", "> LINK_AP_IDLE_MS")),
    ("link_ap_idle_time_shorter", T, H, "(600u * 1000u)", "(600u * 1000u - 1u)"),
    ("link_ap_idle_time_longer", T, H, "(600u * 1000u)", "(600u * 1000u + 1u)"),
    # the idle close with a rule of its own instead of the one of link_ap_kept()
    ("link_ap_closes_in_safe_mode", T, F, AP_IDLE, AP_IDLE.replace(NOT_KEPT, "link->profile_count > 0")),
    ("link_ap_closes_without_profile", T, F, AP_IDLE, AP_IDLE.replace(NOT_KEPT, "!link->ap_forced")),
    ("link_ap_closes_although_kept", T, F, AP_IDLE, AP_IDLE.replace(NOT_KEPT + " && ", "")),
    ("link_ap_closes_with_clients", T, F, AP_IDLE, AP_IDLE.replace(" link->ap_clients == 0 &&", "")),
    ("link_ap_closes_with_one_client", T, F, AP_IDLE, AP_IDLE.replace("link->ap_clients == 0", "link->ap_clients <= 1")),
    ("link_ap_closes_before_it_opens", T, F, AP_IDLE, AP_IDLE.replace("if(link->ap_on && ", "if(")),
    ("link_ap_closing_ordered_at_once", T, F, AP_IDLE, AP_IDLE.replace("\t\tlink->ap_wanted = false;\n", "\t\tlink->ap_wanted = false;\n\t\tlink->ap_on = false;\n")),
    ("link_ap_order_repeated", T, F, AP_ORDER, AP_ORDER.replace("\t\tlink->ap_on = link->ap_wanted;\n\t\tif(!link->ap_on)", "\t\tif(!link->ap_wanted)")),
    ("link_ap_orders_swapped", T, F, AP_ORDER, AP_ORDER.replace("if(!link->ap_on) return LINK_DO_AP_OFF;", "if(link->ap_on) return LINK_DO_AP_OFF;")),
    ("link_ap_never_ordered_off", T, F, AP_ORDER, AP_ORDER.replace("if(link->ap_wanted != link->ap_on)", "if(link->ap_wanted && !link->ap_on)")),
    ("link_ap_never_ordered_on", T, F, AP_ORDER, AP_ORDER.replace("if(link->ap_wanted != link->ap_on)", "if(!link->ap_wanted && link->ap_on)")),
    ("link_ap_keeps_clients_when_it_opens", T, F, AP_ORDER, AP_ORDER.replace("\t\tlink->ap_clients = 0;\n", "")),
    ("link_ap_idle_time_not_started_when_it_opens", T, F, AP_ORDER, AP_ORDER.replace("\t\tlink->ap_idle_since_ms = link->clock_ms;\n", "")),
    ("link_ap_blocks_the_rest", T, F, AP_ORDER, AP_ORDER.replace("\t\tlink->ap_on = link->ap_wanted;\n", "\t\tlink->ap_on = link->ap_wanted;\n\t\tlink->wait_until_ms = later(link, 1);\n\t\tlink->find_at_ms = later(link, 1);\n")),
    ("link_ap_on_reports_the_wish", T, F, "\treturn link->ap_on;", "\treturn link->ap_wanted;"),
    ("link_ap_request_ignored", T, F, REQUEST, "\t(void)on;"),
    # the request with a rule of its own instead of the one of link_ap_kept()
    ("link_ap_request_off_in_safe_mode", T, F, REQUEST, "\tlink->ap_wanted = on || link->profile_count == 0;"),
    ("link_ap_request_off_without_profile", T, F, REQUEST, "\tlink->ap_wanted = on || link->ap_forced;"),
    ("link_ap_request_off_although_kept", T, F, REQUEST, "\tlink->ap_wanted = on;"),
    ("link_ap_request_off_ignored", T, F, REQUEST, "\tif(on) link->ap_wanted = true;"),
    ("link_ap_request_on_ignored", T, F, REQUEST, "\tif(!on) " + REQUEST.strip()),
    ("link_ap_request_starts_idle_time", T, F, REQUEST, REQUEST + "\n\tif(on) link->ap_idle_since_ms = link->clock_ms;"),
    ("link_ap_clients_negative_stored", T, F, CLIENTS_NEGATIVE, ""),
    ("link_ap_clients_negative_counts_as_one", T, F, CLIENTS_NEGATIVE, "\tif(clients < 0) clients = 1;\n"),
    ("link_ap_clients_every_report_restarts", T, F, CLIENTS_LEFT, "\tif(clients == 0) link->ap_idle_since_ms = link->clock_ms;"),
    ("link_ap_clients_leaving_does_not_restart", T, F, CLIENTS_LEFT + "\n", ""),
    ("link_ap_clients_one_leaving_restarts_only", T, F, CLIENTS_LEFT, CLIENTS_LEFT.replace("link->ap_clients > 0", "link->ap_clients == 1")),
    ("link_ap_clients_not_stored", T, F, "\tlink->ap_clients = clients;\n}", "}"),
    ("link_ap_clients_only_growing", T, F, "\tlink->ap_clients = clients;\n}", "\tif(clients > link->ap_clients) link->ap_clients = clients;\n}"),

    # link_ap_kept: safe mode, or no stored profile - whatever is asked, whether it is on already or not
    ("link_ap_kept_not_in_safe_mode", T, F, KEPT, "\treturn link->profile_count == 0;"),
    ("link_ap_kept_not_without_profile", T, F, KEPT, "\treturn link->ap_forced;"),
    ("link_ap_kept_only_in_safe_mode_without_profile", T, F, KEPT, KEPT.replace("||", "&&")),
    ("link_ap_kept_never", T, F, KEPT, "\t(void)link;\n\treturn false;"),
    ("link_ap_kept_always", T, F, KEPT, "\t(void)link;\n\treturn true;"),
    ("link_ap_kept_with_one_profile", T, F, KEPT, KEPT.replace("profile_count == 0", "profile_count <= 1")),
    ("link_ap_kept_unless_the_list_is_full", T, F, KEPT, KEPT.replace("profile_count == 0", "profile_count < NET_PROFILES_MAX")),
    ("link_ap_kept_while_it_is_on", T, F, KEPT, "\treturn link->ap_on;"),
    ("link_ap_kept_while_it_is_wanted", T, F, KEPT, "\treturn link->ap_wanted;"),
    ("link_ap_kept_only_once_it_is_on", T, F, KEPT, "\treturn link->ap_on && (link->ap_forced || link->profile_count == 0);"),
    ("link_ap_kept_by_a_client", T, F, KEPT, KEPT.replace(";", " || link->ap_clients > 0;")),
    ("link_ap_kept_while_in_no_network", T, F, KEPT, KEPT.replace("link->profile_count == 0", "link->phase == LINK_IDLE || link->phase == LINK_WAITING")),
    ("link_ap_kept_only_while_idle", T, F, KEPT, KEPT.replace("link->profile_count == 0", "link->phase == LINK_IDLE")),

    # link_profiles
    ("link_profiles_list_not_stored", T, F, PROFILES_TAKEN, PROFILES_TAKEN.replace("\tlink->profiles = profiles;\n", "\t(void)profiles;\n")),
    ("link_profiles_count_not_stored", T, F, PROFILES_TAKEN, PROFILES_TAKEN.replace("\tlink->profile_count = listed(profile_count);\n", "\t(void)profile_count;\n")),
    ("link_profiles_no_access_point_without_profile", T, F, PROFILES_TAKEN, PROFILES_TAKEN.replace("\tif(link->profile_count == 0) link->ap_wanted = true;\n", "")),
    ("link_profiles_access_point_always", T, F, PROFILES_TAKEN, PROFILES_TAKEN.replace("if(link->profile_count == 0) link->ap_wanted", "link->ap_wanted")),
    ("link_profiles_access_point_off_with_profile", T, F, PROFILES_TAKEN, PROFILES_TAKEN.replace("if(link->profile_count == 0) link->ap_wanted = true;", "link->ap_wanted = link->profile_count == 0 || link->ap_forced;")),
    ("link_profiles_no_leave_when_not_up", T, F, IN_NETWORK, IN_NETWORK.replace("link->phase == LINK_JOINED || ", "")),
    ("link_profiles_no_leave_when_up", T, F, IN_NETWORK, IN_NETWORK.replace("link->phase == LINK_UP || ", "")),
    ("link_profiles_no_leave_when_joining", T, F, IN_NETWORK, "\tif(link->phase == LINK_JOINED || link->phase == LINK_UP)"),
    ("link_profiles_leave_before_join_handed_out", T, F, IN_NETWORK, IN_NETWORK.replace("(link->phase == LINK_JOINING && link->busy)", "link->phase == LINK_JOINING")),
    ("link_profiles_leave_due_not_stored", T, F, VOID, VOID.replace("\t\tlink->changed = true;\n", "")),
    ("link_profiles_keeps_profile", T, F, VOID, VOID.replace("\t\tlink->profile = -1;\n", "")),
    ("link_profiles_keeps_host", T, F, VOID, VOID.replace("\t\tlink->host[0] = '\\0';\n", "")),
    ("link_profiles_stays_up", T, F, VOID, VOID.replace("\t\tif(link->phase == LINK_UP) link->phase = LINK_JOINED;\n", "")),
    ("link_profiles_gives_up_query", T, F, VOID, VOID + "\t\tif(link->finding) link->busy = link->finding = false;\n"),
    ("link_profiles_leave_only_without_profile", T, F, VOID, VOID.replace("link->changed = true;", "link->changed = link->profile_count == 0;")),
    ("link_profiles_scan_keeps_wait", T, F, PROFILES_SCANNING, "\telse if(link->phase == LINK_SCANNING) (void)0;"),
    ("link_profiles_second_scan", T, F, PROFILES_SCANNING + "\n", ""),
    ("link_profiles_scan_ends_idle", T, F, PROFILES_SCANNING, "\telse if(link->phase == LINK_SCANNING && link->profile_count > 0) link->wait_step = 0;"),
    ("link_profiles_scan_during_leave", T, F, PROFILES_ELSE, "\telse start_over(link);"),
    ("link_profiles_no_start_over", T, F, PROFILES_ELSE + "\n", ""),
    ("link_profiles_start_over_only_when_idle", T, F, PROFILES_ELSE, "\telse if(link->phase == LINK_IDLE) start_over(link);"),
    ("link_profiles_wait_goes_on", T, F, PROFILES_ELSE, "\telse if(link->phase != LINK_LEAVING && link->phase != LINK_WAITING) start_over(link);"),
    ("link_profiles_join_goes_on", T, F, PROFILES_ELSE, "\telse if(link->phase != LINK_LEAVING && link->phase != LINK_JOINING) start_over(link);"),
    ("link_profiles_idle_stays_idle", T, F, PROFILES_ELSE, "\telse if(link->phase != LINK_LEAVING && link->phase != LINK_IDLE) start_over(link);"),

    # what the caller sees
    ("link_up_also_when_joined", T, F, "\treturn link->phase == LINK_UP;", "\treturn link->phase == LINK_UP || link->phase == LINK_JOINED;"),
    ("link_up_only_when_queried", T, F, "\treturn link->phase == LINK_UP;", "\treturn link->phase == LINK_UP && link->host_from_query;"),
    ("link_host_always_empty", T, F, "\treturn link->host;", "\treturn link->host + strlen(link->host);"),
    ("link_host_only_when_queried", T, F, "\treturn link->host;", "\treturn link->host_from_query ? link->host : \"\";"),
]
