"""Mutations of display/components/core/guard.c, see ../redproof.py."""

F = "components/core/guard.c"
H = "components/core/guard.h"
T = "test_guard"

CHECK = "\treturn memory->magic ^ memory->crashes ^ memory->layout_fresh ^ 0xFFFFFFFFu;\n"
KNOWN = "\treturn memory->magic == GUARD_MAGIC && memory->check == check_of(memory);\n"
STORE_MAGIC = "\tmemory->magic = GUARD_MAGIC;\n"
STORE_CRASHES = "\tmemory->crashes = crashes;\n"
STORE_FRESH = "\tmemory->layout_fresh = layout_fresh ? GUARD_MAGIC : 0;\n"
STORE_CHECK = "\tmemory->check = check_of(memory);\n"
CRASHED = "\tbool crashed = reset != GUARD_RESET_POWER_ON && reset != GUARD_RESET_SOFTWARE;\n"
START_KNOWN = "\tbool known = reset != GUARD_RESET_POWER_ON && is_known(memory);\n"
START_CRASHES = "\tuint32_t crashes = known ? memory->crashes : 0;\n"
START_FRESH = "\tbool fresh = known && memory->layout_fresh == GUARD_MAGIC;\n"
COUNT = "\tif(crashed && crashes < UINT32_MAX) crashes++;\n"
SAFE = "\tstart.safe_mode = knob_held || crashes >= GUARD_CRASHES;\n"
PREVIOUS = "\tstart.previous_layout = crashed && fresh;\n"
START_STORE = "\tstore(memory, crashes, fresh && !crashed);\n"
ALIVE = "\tstore(memory, 0, false);\n"
STORED = "\tstore(memory, is_known(memory) ? memory->crashes : 0, true);\n"

HEAT_LEVEL = "\tif(before != GUARD_HEAT_NORMAL && before != GUARD_HEAT_DIM) before = GUARD_HEAT_OFF;\n"
HEAT_FAILED = "\tif(!valid) return before;\n"
HEAT_OFF = "\tif(temp_c >= GUARD_TEMP_OFF_C) return GUARD_HEAT_OFF;\n"
HEAT_STAY_OFF = "\tif(before == GUARD_HEAT_OFF && temp_c >= GUARD_TEMP_OFF_C - GUARD_TEMP_BACK_C) return GUARD_HEAT_OFF;\n"
HEAT_DIM = "\tif(temp_c >= GUARD_TEMP_DIM_C) return GUARD_HEAT_DIM;\n"
HEAT_STAY_DIM = "\tif(before != GUARD_HEAT_NORMAL && temp_c >= GUARD_TEMP_DIM_C - GUARD_TEMP_BACK_C) return GUARD_HEAT_DIM;\n"

B_START = "\tint limit = 0;\n"
B_NORMAL = "\tif(heat == GUARD_HEAT_NORMAL) limit = 100;\n"
B_DIM = "\tif(heat == GUARD_HEAT_DIM) limit = GUARD_DIM_PERCENT;\n"
B_LIMIT = "\tif(wanted > limit) wanted = limit;\n"
B_FLOOR = "\treturn wanted < 0 ? 0 : wanted;\n"

C_INIT = "\tmemset(guard, 0, sizeof(*guard));\n"
C_HAS = "\tguard->has_stored = has_stored;\n"
C_SUM = "\tguard->stored_sum = stored_sum;\n"
C_CONNECTED = "\tguard->written = false;\n\tguard->has_seen = false;\n"
C_SEEN = "\tif(!guard->has_seen || sum != guard->seen_sum)\n"
C_SEEN_BLOCK = "\t{\n\t\tguard->has_seen = true;\n\t\tguard->seen_sum = sum;\n\t\tguard->seen_since_ms = now_ms;\n\t}\n"
C_GATE = "\tif(!complete || guard->written) return false;\n"
C_SAME = "\tif(guard->has_stored && sum == guard->stored_sum) return false;\n"
C_REST = "\tif(now_ms < guard->seen_since_ms || now_ms - guard->seen_since_ms < GUARD_CATALOG_REST_MS) return false;\n"
C_DONE = "\tguard->stored_sum = sum;\n\tguard->has_stored = true;\n\tguard->written = true;\n\treturn true;\n"

MUTATIONS = [
    # the check word
    ("guard_check_without_magic", T, F, CHECK, "\treturn memory->crashes ^ memory->layout_fresh ^ 0xFFFFFFFFu;\n"),
    ("guard_check_without_counter", T, F, CHECK, "\treturn memory->magic ^ memory->layout_fresh ^ 0xFFFFFFFFu;\n"),
    ("guard_check_without_mark", T, F, CHECK, "\treturn memory->magic ^ memory->crashes ^ 0xFFFFFFFFu;\n"),
    ("guard_check_not_turned", T, F, CHECK, "\treturn memory->magic ^ memory->crashes ^ memory->layout_fresh;\n"),
    ("guard_check_one_bit_not_turned", T, F, CHECK, CHECK.replace("0xFFFFFFFFu", "0x7FFFFFFFu")),
    ("guard_check_added", T, F, CHECK, "\treturn (memory->magic + memory->crashes + memory->layout_fresh) ^ 0xFFFFFFFFu;\n"),

    # what counts as a counter of this firmware
    ("guard_magic_not_looked_at", T, F, KNOWN, "\treturn memory->check == check_of(memory);\n"),
    ("guard_check_not_looked_at", T, F, KNOWN, "\treturn memory->magic == GUARD_MAGIC;\n"),
    ("guard_magic_or_check_is_enough", T, F, KNOWN, KNOWN.replace("&&", "||")),
    ("guard_memory_never_known", T, F, KNOWN, KNOWN.replace(";\n", " && false;\n")),
    ("guard_magic_low_bits_only", T, F, KNOWN, KNOWN.replace("memory->magic == GUARD_MAGIC", "(memory->magic & 0xFFFFFFu) == (GUARD_MAGIC & 0xFFFFFFu)")),
    ("guard_magic_high_bits_only", T, F, KNOWN, KNOWN.replace("memory->magic == GUARD_MAGIC", "(memory->magic >> 8) == (GUARD_MAGIC >> 8)")),
    ("guard_check_low_bits_only", T, F, KNOWN, KNOWN.replace("memory->check == check_of(memory)", "(memory->check & 0xFFFFu) == (check_of(memory) & 0xFFFFu)")),
    ("guard_check_high_bits_only", T, F, KNOWN, KNOWN.replace("memory->check == check_of(memory)", "(memory->check >> 4) == (check_of(memory) >> 4)")),
    ("guard_magic_changed", T, H, "#define GUARD_MAGIC             0x57444731u", "#define GUARD_MAGIC             0x57444732u"),

    # the memory is left valid
    ("guard_store_magic_not_set", T, F, STORE_MAGIC, ""),
    ("guard_store_counter_not_set", T, F, STORE_CRASHES, "\t(void)crashes;\n"),
    ("guard_store_mark_not_set", T, F, STORE_FRESH, "\t(void)layout_fresh;\n"),
    ("guard_store_mark_is_one", T, F, STORE_FRESH, "\tmemory->layout_fresh = layout_fresh ? 1 : 0;\n"),
    ("guard_store_no_mark_is_all_ones", T, F, STORE_FRESH, "\tmemory->layout_fresh = layout_fresh ? GUARD_MAGIC : 0xFFFFFFFFu;\n"),
    ("guard_store_check_not_set", T, F, STORE_CHECK, ""),
    ("guard_store_check_before_the_mark", T, F, STORE_FRESH + STORE_CHECK, STORE_CHECK + STORE_FRESH),
    ("guard_store_check_before_the_counter", T, F, STORE_CRASHES + STORE_FRESH + STORE_CHECK, STORE_FRESH + STORE_CHECK + STORE_CRASHES),

    # guard_start: the reset reason
    ("guard_unknown_reset_is_no_crash", T, F, CRASHED, "\tbool crashed = reset == GUARD_RESET_CRASH;\n"),
    ("guard_software_counts_as_crash", T, F, CRASHED, "\tbool crashed = reset != GUARD_RESET_POWER_ON;\n"),
    ("guard_power_on_counts_as_crash", T, F, CRASHED, "\tbool crashed = reset != GUARD_RESET_SOFTWARE;\n"),
    ("guard_crash_never_seen", T, F, CRASHED, CRASHED.replace(";\n", " && false;\n")),
    ("guard_unknown_reset_is_power_on", T, F,
     START_KNOWN, "\tbool known = (reset == GUARD_RESET_SOFTWARE || reset == GUARD_RESET_CRASH) && is_known(memory);\n"),
    ("guard_power_on_trusts_memory", T, F, START_KNOWN, "\tbool known = is_known(memory);\n"),
    ("guard_software_restart_forgets_memory", T, F, START_KNOWN, "\tbool known = crashed && is_known(memory);\n"),
    ("guard_damaged_memory_trusted", T, F, START_KNOWN, "\tbool known = reset != GUARD_RESET_POWER_ON;\n"),
    ("guard_damaged_counter_trusted", T, F, START_CRASHES, "\tuint32_t crashes = reset != GUARD_RESET_POWER_ON ? memory->crashes : 0;\n"),
    ("guard_damaged_mark_trusted", T, F, START_FRESH, "\tbool fresh = reset != GUARD_RESET_POWER_ON && memory->layout_fresh == GUARD_MAGIC;\n"),
    ("guard_unknown_counter_starts_at_one", T, F, START_CRASHES, "\tuint32_t crashes = known ? memory->crashes : 1;\n"),
    ("guard_counter_always_starts_at_zero", T, F, START_CRASHES, "\tuint32_t crashes = known && false ? memory->crashes : 0;\n"),
    ("guard_mark_of_any_value", T, F, START_FRESH, "\tbool fresh = known && memory->layout_fresh != 0;\n"),
    ("guard_mark_never_read", T, F, START_FRESH, START_FRESH.replace(";\n", " && false;\n")),

    # guard_start: the counter
    ("guard_crash_not_counted", T, F, COUNT, ""),
    ("guard_crash_counted_twice", T, F, COUNT, "\tif(crashed && crashes < UINT32_MAX) crashes += 2;\n"),
    ("guard_counter_wraps", T, F, COUNT, "\tif(crashed) crashes++;\n"),
    ("guard_counter_stops_one_early", T, F, COUNT, "\tif(crashed && crashes < UINT32_MAX - 1) crashes++;\n"),
    ("guard_counter_stops_at_the_limit", T, F, COUNT, "\tif(crashed && crashes < GUARD_CRASHES) crashes++;\n"),
    ("guard_every_start_counted", T, F, COUNT, "\tif(crashes < UINT32_MAX) crashes++;\n"),
    ("guard_knob_counted_as_crash", T, F, COUNT, "\tif((crashed || knob_held) && crashes < UINT32_MAX) crashes++;\n"),
    ("guard_software_restart_resets_counter", T, F, COUNT, COUNT + "\tif(reset == GUARD_RESET_SOFTWARE) crashes = 0;\n"),
    ("guard_counter_not_stored", T, F, START_STORE, "\tstore(memory, known ? memory->crashes : 0, fresh && !crashed);\n"),
    ("guard_start_stores_nothing", T, F, START_STORE, ""),
    ("guard_start_stores_only_after_crash", T, F, START_STORE, "\tif(crashed) store(memory, crashes, false);\n"),

    # guard_start: safe mode
    ("guard_safe_mode_one_crash_late", T, F, SAFE, SAFE.replace(">= GUARD_CRASHES", "> GUARD_CRASHES")),
    ("guard_safe_mode_one_crash_early", T, F, SAFE, SAFE.replace(">= GUARD_CRASHES", ">= GUARD_CRASHES - 1")),
    ("guard_safe_mode_limit_changed", T, H, "#define GUARD_CRASHES           3", "#define GUARD_CRASHES           4"),
    ("guard_safe_mode_only_at_the_limit", T, F, SAFE, SAFE.replace(">= GUARD_CRASHES", "== GUARD_CRASHES")),
    ("guard_safe_mode_never_by_counter", T, F, SAFE, "\tstart.safe_mode = knob_held;\n"),
    ("guard_safe_mode_never_by_knob", T, F, SAFE, "\t(void)knob_held;\n\tstart.safe_mode = crashes >= GUARD_CRASHES;\n"),
    ("guard_safe_mode_needs_knob_and_counter", T, F, SAFE, SAFE.replace("||", "&&")),
    ("guard_safe_mode_by_knob_not_after_crash", T, F, SAFE, SAFE.replace("knob_held ||", "(knob_held && !crashed) ||")),
    ("guard_safe_mode_by_knob_only_after_power_on", T, F, SAFE, SAFE.replace("knob_held ||", "(knob_held && reset == GUARD_RESET_POWER_ON) ||")),
    ("guard_safe_mode_by_counter_only_after_crash", T, F, SAFE, SAFE.replace("crashes >= GUARD_CRASHES", "(crashed && crashes >= GUARD_CRASHES)")),
    ("guard_safe_mode_before_the_crash_is_counted", T, F, COUNT + "\n" + SAFE, SAFE + COUNT),

    # guard_start: the layout mark
    ("guard_previous_layout_without_crash", T, F, PREVIOUS, "\tstart.previous_layout = fresh;\n"),
    ("guard_previous_layout_without_mark", T, F, PREVIOUS, "\tstart.previous_layout = crashed;\n"),
    ("guard_previous_layout_never", T, F, PREVIOUS, PREVIOUS.replace(";\n", " && false;\n")),
    ("guard_previous_layout_not_in_safe_mode", T, F, PREVIOUS, PREVIOUS.replace(";\n", " && !start.safe_mode;\n")),
    ("guard_previous_layout_only_at_first_crash", T, F, PREVIOUS, PREVIOUS.replace(";\n", " && crashes == 1;\n")),
    ("guard_previous_layout_only_for_real_crash", T, F, PREVIOUS, "\tstart.previous_layout = reset == GUARD_RESET_CRASH && fresh;\n"),
    ("guard_mark_kept_after_use", T, F, START_STORE, "\tstore(memory, crashes, fresh);\n"),
    ("guard_mark_dropped_by_software_restart", T, F, START_STORE, "\tstore(memory, crashes, false);\n"),
    ("guard_mark_set_by_crash", T, F, START_STORE, "\tstore(memory, crashes, fresh || crashed);\n"),
    ("guard_mark_kept_in_safe_mode", T, F, START_STORE, "\tstore(memory, crashes, fresh && (!crashed || start.safe_mode));\n"),

    # guard_alive, guard_layout_stored
    ("guard_alive_does_nothing", T, F, ALIVE, "\t(void)memory;\n"),
    ("guard_alive_keeps_counter", T, F, ALIVE, "\tstore(memory, is_known(memory) ? memory->crashes : 0, false);\n"),
    ("guard_alive_keeps_mark", T, F, ALIVE, "\tstore(memory, 0, is_known(memory) && memory->layout_fresh == GUARD_MAGIC);\n"),
    ("guard_alive_sets_mark", T, F, ALIVE, "\tstore(memory, 0, true);\n"),
    ("guard_alive_only_with_valid_memory", T, F, ALIVE, "\tif(is_known(memory)) store(memory, 0, false);\n"),
    ("guard_alive_counter_one", T, F, ALIVE, "\tstore(memory, 1, false);\n"),
    ("guard_layout_stored_does_nothing", T, F, STORED, "\t(void)memory;\n"),
    ("guard_layout_stored_sets_no_mark", T, F, STORED, STORED.replace("true", "false")),
    ("guard_layout_stored_resets_counter", T, F, STORED, "\tstore(memory, 0, true);\n"),
    ("guard_layout_stored_trusts_damaged_counter", T, F, STORED, "\tstore(memory, memory->crashes, true);\n"),
    ("guard_layout_stored_only_with_valid_memory", T, F, STORED, "\tif(is_known(memory)) store(memory, memory->crashes, true);\n"),
    ("guard_layout_stored_counts_a_crash", T, F, STORED, "\tstore(memory, is_known(memory) ? memory->crashes + 1 : 0, true);\n"),

    # heat
    ("guard_heat_invalid_level_kept", T, F, HEAT_LEVEL, ""),
    ("guard_heat_invalid_level_is_normal", T, F,
     HEAT_LEVEL, "\tif(before != GUARD_HEAT_DIM && before != GUARD_HEAT_OFF) before = GUARD_HEAT_NORMAL;\n"),
    ("guard_heat_invalid_level_is_dim", T, F,
     HEAT_LEVEL, "\tif(before != GUARD_HEAT_NORMAL && before != GUARD_HEAT_OFF) before = GUARD_HEAT_DIM;\n"),
    ("guard_heat_invalid_level_only_checked_with_reading", T, F, HEAT_LEVEL + HEAT_FAILED, HEAT_FAILED + HEAT_LEVEL),
    ("guard_heat_failed_reading_used", T, F, HEAT_FAILED, "\t(void)valid;\n"),
    ("guard_heat_failed_reading_is_off", T, F, HEAT_FAILED, "\tif(!valid) return GUARD_HEAT_OFF;\n"),
    ("guard_heat_failed_reading_is_dim", T, F, HEAT_FAILED, "\tif(!valid) return GUARD_HEAT_DIM;\n"),
    ("guard_heat_failed_reading_is_normal", T, F, HEAT_FAILED, "\tif(!valid) return GUARD_HEAT_NORMAL;\n"),
    ("guard_heat_failed_reading_used_when_hot", T, F, HEAT_FAILED, "\tif(!valid && temp_c < GUARD_TEMP_OFF_C) return before;\n"),
    ("guard_heat_off_one_degree_late", T, F, HEAT_OFF, HEAT_OFF.replace(">=", ">")),
    ("guard_heat_off_one_degree_early", T, F, HEAT_OFF, HEAT_OFF.replace("GUARD_TEMP_OFF_C)", "GUARD_TEMP_OFF_C - 1)")),
    ("guard_heat_off_limit_changed", T, H, "#define GUARD_TEMP_OFF_C        85", "#define GUARD_TEMP_OFF_C        86"),
    ("guard_heat_never_off_from_below", T, F, HEAT_OFF, ""),
    ("guard_heat_off_only_from_dim", T, F, HEAT_OFF, "\tif(temp_c >= GUARD_TEMP_OFF_C && before != GUARD_HEAT_NORMAL) return GUARD_HEAT_OFF;\n"),
    ("guard_heat_off_lifted_one_degree_late", T, F, HEAT_STAY_OFF, HEAT_STAY_OFF.replace("GUARD_TEMP_BACK_C)", "GUARD_TEMP_BACK_C - 1)")),
    ("guard_heat_off_lifted_one_degree_early", T, F, HEAT_STAY_OFF, HEAT_STAY_OFF.replace(">=", ">")),
    ("guard_heat_off_lifted_at_once", T, F, HEAT_STAY_OFF, ""),
    ("guard_heat_off_never_lifted", T, F, HEAT_STAY_OFF, "\tif(before == GUARD_HEAT_OFF) return GUARD_HEAT_OFF;\n"),
    ("guard_heat_off_from_80_at_every_level", T, F, HEAT_STAY_OFF, "\tif(temp_c >= GUARD_TEMP_OFF_C - GUARD_TEMP_BACK_C) return GUARD_HEAT_OFF;\n"),
    ("guard_heat_off_from_80_when_dimmed", T, F, HEAT_STAY_OFF, HEAT_STAY_OFF.replace("before == GUARD_HEAT_OFF", "before != GUARD_HEAT_NORMAL")),
    ("guard_heat_dim_one_degree_late", T, F, HEAT_DIM, HEAT_DIM.replace(">=", ">")),
    ("guard_heat_dim_one_degree_early", T, F, HEAT_DIM, HEAT_DIM.replace("GUARD_TEMP_DIM_C)", "GUARD_TEMP_DIM_C - 1)")),
    ("guard_heat_dim_limit_changed", T, H, "#define GUARD_TEMP_DIM_C        75", "#define GUARD_TEMP_DIM_C        76"),
    ("guard_heat_never_dim_from_normal", T, F, HEAT_DIM, ""),
    ("guard_heat_dim_lifted_one_degree_late", T, F, HEAT_STAY_DIM, HEAT_STAY_DIM.replace("GUARD_TEMP_BACK_C)", "GUARD_TEMP_BACK_C - 1)")),
    ("guard_heat_dim_lifted_one_degree_early", T, F, HEAT_STAY_DIM, HEAT_STAY_DIM.replace(">=", ">")),
    ("guard_heat_dim_lifted_at_once", T, F, HEAT_STAY_DIM, ""),
    ("guard_heat_dim_never_lifted", T, F, HEAT_STAY_DIM, "\tif(before != GUARD_HEAT_NORMAL) return GUARD_HEAT_DIM;\n"),
    ("guard_heat_dim_from_70_at_every_level", T, F, HEAT_STAY_DIM, "\tif(temp_c >= GUARD_TEMP_DIM_C - GUARD_TEMP_BACK_C) return GUARD_HEAT_DIM;\n"),
    ("guard_heat_off_falls_to_normal_below_75", T, F, HEAT_STAY_DIM, HEAT_STAY_DIM.replace("before != GUARD_HEAT_NORMAL", "before == GUARD_HEAT_DIM")),
    ("guard_heat_dim_falls_to_normal_below_75", T, F, HEAT_STAY_DIM, HEAT_STAY_DIM.replace("before != GUARD_HEAT_NORMAL", "before == GUARD_HEAT_OFF")),
    ("guard_heat_back_changed", T, H, "#define GUARD_TEMP_BACK_C       5", "#define GUARD_TEMP_BACK_C       4"),

    # brightness
    ("guard_brightness_normal_not_limited", T, F, B_NORMAL, B_NORMAL.replace("limit = 100", "limit = 1000")),
    ("guard_brightness_normal_limit_99", T, F, B_NORMAL, B_NORMAL.replace("limit = 100", "limit = 99")),
    ("guard_brightness_normal_is_off", T, F, B_NORMAL, ""),
    ("guard_brightness_dim_not_limited", T, F, B_DIM, B_DIM.replace("limit = GUARD_DIM_PERCENT", "limit = 100")),
    ("guard_brightness_dim_is_off", T, F, B_DIM, ""),
    ("guard_brightness_dim_percent_changed", T, H, "#define GUARD_DIM_PERCENT       30", "#define GUARD_DIM_PERCENT       31"),
    ("guard_brightness_off_is_dimmed", T, F, B_START, "\tint limit = GUARD_DIM_PERCENT;\n"),
    ("guard_brightness_off_is_one", T, F, B_START, "\tint limit = 1;\n"),
    ("guard_brightness_invalid_level_not_limited", T, F,
     B_START + "\n" + B_NORMAL, "\tint limit = 100;\n\n\tif(heat == GUARD_HEAT_OFF) limit = 0;\n"),
    ("guard_brightness_always_the_limit", T, F, B_LIMIT, "\twanted = limit;\n"),
    ("guard_brightness_not_limited", T, F, B_LIMIT, "\t(void)limit;\n"),
    ("guard_brightness_negative_kept", T, F, B_FLOOR, "\treturn wanted;\n"),
    ("guard_brightness_at_least_one", T, F, B_FLOOR, "\treturn wanted < 1 ? 1 : wanted;\n"),
    ("guard_brightness_negative_is_the_limit", T, F, B_FLOOR, "\treturn wanted < 0 ? limit : wanted;\n"),

    # the catalogue: start and connection
    ("guard_catalog_init_keeps_everything", T, F, C_INIT, ""),
    ("guard_catalog_init_keeps_written", T, F, C_INIT, "\tguard->has_seen = false;\n"),
    ("guard_catalog_init_keeps_seen", T, F, C_INIT, "\tguard->written = false;\n"),
    ("guard_catalog_init_never_stored", T, F, C_HAS, "\tguard->has_stored = has_stored && false;\n"),
    ("guard_catalog_init_always_stored", T, F, C_HAS, "\tguard->has_stored = has_stored || true;\n"),
    ("guard_catalog_init_sum_ignored", T, F, C_SUM, "\t(void)stored_sum;\n"),
    ("guard_catalog_connected_does_nothing", T, F, C_CONNECTED, "\t(void)guard;\n"),
    ("guard_catalog_connected_allows_no_write", T, F, C_CONNECTED, "\tguard->has_seen = false;\n"),
    ("guard_catalog_connected_keeps_rest_time", T, F, C_CONNECTED, "\tguard->written = false;\n"),
    ("guard_catalog_connected_forgets_stored", T, F, C_CONNECTED, C_CONNECTED + "\tguard->has_stored = false;\n"),

    # the catalogue: the rest time
    ("guard_catalog_change_does_not_restart", T, F, C_SEEN, "\tif(!guard->has_seen)\n"),
    ("guard_catalog_restarts_every_round", T, F, C_SEEN, "\tif(!guard->has_seen || sum != guard->seen_sum || complete)\n"),
    ("guard_catalog_first_round_not_noted", T, F, C_SEEN, "\tif(guard->has_seen && sum != guard->seen_sum)\n"),
    ("guard_catalog_seen_not_noted", T, F, C_SEEN_BLOCK, C_SEEN_BLOCK.replace("\t\tguard->has_seen = true;\n", "")),
    ("guard_catalog_seen_sum_not_noted", T, F, C_SEEN_BLOCK, C_SEEN_BLOCK.replace("\t\tguard->seen_sum = sum;\n", "")),
    ("guard_catalog_seen_time_not_noted", T, F, C_SEEN_BLOCK, C_SEEN_BLOCK.replace("\t\tguard->seen_since_ms = now_ms;\n", "")),
    ("guard_catalog_not_followed_while_incomplete", T, F, C_SEEN + C_SEEN_BLOCK + "\n" + C_GATE, C_GATE + C_SEEN + C_SEEN_BLOCK + "\n"),
    ("guard_catalog_incomplete_restarts_rest", T, F,
     C_GATE, "\tif(!complete)\n\t{\n\t\tguard->has_seen = false;\n\t\treturn false;\n\t}\n\tif(guard->written) return false;\n"),
    ("guard_catalog_rest_one_ms_late", T, F, C_REST, C_REST.replace("< GUARD_CATALOG_REST_MS", "<= GUARD_CATALOG_REST_MS")),
    ("guard_catalog_rest_one_ms_early", T, F, C_REST, C_REST.replace("< GUARD_CATALOG_REST_MS", "< GUARD_CATALOG_REST_MS - 1")),
    ("guard_catalog_rest_time_changed", T, H, "#define GUARD_CATALOG_REST_MS   30000u", "#define GUARD_CATALOG_REST_MS   30001u"),
    ("guard_catalog_no_rest", T, F, C_REST, ""),
    ("guard_catalog_step_back_is_a_long_time", T, F, C_REST, "\tif(now_ms - guard->seen_since_ms < GUARD_CATALOG_REST_MS) return false;\n"),
    ("guard_catalog_step_back_is_rest_enough", T, F,
     C_REST, "\tif(now_ms >= guard->seen_since_ms && now_ms - guard->seen_since_ms < GUARD_CATALOG_REST_MS) return false;\n"),

    # the catalogue: what else keeps it from being written
    ("guard_catalog_incomplete_is_written", T, F, C_GATE, "\t(void)complete;\n\tif(guard->written) return false;\n"),
    ("guard_catalog_written_again_and_again", T, F, C_GATE, "\tif(!complete) return false;\n"),
    ("guard_catalog_stored_sum_is_written", T, F, C_SAME, ""),
    ("guard_catalog_stored_sum_looked_at_without_catalogue", T, F, C_SAME, "\tif(sum == guard->stored_sum) return false;\n"),
    ("guard_catalog_never_written_over_stored", T, F, C_SAME, "\tif(guard->has_stored) return false;\n"),
    ("guard_catalog_due_never", T, F, C_DONE, C_DONE.replace("return true", "return false")),
    ("guard_catalog_new_sum_not_stored", T, F, C_DONE, C_DONE.replace("\tguard->stored_sum = sum;\n", "")),
    ("guard_catalog_stays_without_stored", T, F, C_DONE, C_DONE.replace("\tguard->has_stored = true;\n", "")),
    ("guard_catalog_write_not_noted", T, F, C_DONE, C_DONE.replace("\tguard->written = true;\n", "")),
    ("guard_catalog_rest_restarts_after_write", T, F, C_DONE, C_DONE.replace("\tguard->written = true;\n", "\tguard->has_seen = false;\n")),
]
