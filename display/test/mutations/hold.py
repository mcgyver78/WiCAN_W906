"""Mutations of display/components/core/hold.c, see ../redproof.py."""

F = "components/core/hold.c"
H = "components/core/hold.h"
T = "test_hold"

CLOCK = "\treturn hold->clock_ms + (now_ms > hold->last_ms ? now_ms - hold->last_ms : 0);"
HOLDING = "\treturn hold->open && hold->pressed && hold->seen_released;"
CLOSE = "void hold_close(hold_t *hold)\n{\n\thold->open = false;\n}"
UNSEEN = "\tbool unseen = !hold->watched || now - hold->sampled_ms > HOLD_GAP_MS;"
WATCHED = "\thold->watched = read_ok;\n"
SAMPLED = "\thold->sampled_ms = now;\n"
CHANGED = "\t\tif(pressed) hold->pressed_since_ms = now;\n\t\telse hold->released_since_ms = now;"
RESTART = "\tif(unseen) hold->released_since_ms = now;"
BACK = "\tif(unseen || (pressed && !on_action)) hold->seen_released = false;"
STUCK = "\tif(pressed && now - hold->pressed_since_ms >= HOLD_STUCK_MS) hold->stuck = true;"
STUCK_BLOCK = "\tif(hold->stuck)\n\t{\n\t\thold_close(hold);\n\t\treturn HOLD_STUCK;\n\t}\n"
RELEASED = "\tif(!pressed && now - hold->released_since_ms >= HOLD_RELEASED_MS && now - hold->opened_ms >= HOLD_RELEASED_MS)"
IDLE = "\tif(now - hold->last_input_ms >= HOLD_IDLE_MS)"
CONFIRM = "\tif(holding(hold) && now - hold->pressed_since_ms >= HOLD_CONFIRM_MS)"
PROGRESS = "\treturn holding(hold) ? HOLD_PROGRESS : HOLD_WAITING;"
ACTIVITY = "\thold->last_input_ms = advance(hold, now_ms);\n"
TURN = "\tif(hold->pressed) hold->seen_released = false;\n"
PERMILLE = "\theld = clock_at(hold, now_ms) - hold->pressed_since_ms;"

MUTATIONS = [
    # the time of the module
    ("hold_time_steps_back_with_the_caller", T, F, CLOCK, "\treturn hold->clock_ms + (now_ms - hold->last_ms);"),
    ("hold_time_stands_until_caught_up", T, F, "\thold->last_ms = now_ms;", "\tif(now_ms > hold->last_ms) hold->last_ms = now_ms;"),
    ("hold_time_of_previous_call_not_stored", T, F, "\thold->last_ms = now_ms;\n", ""),
    ("hold_time_does_not_advance", T, F, "\thold->clock_ms = clock_at(hold, now_ms);\n", ""),
    ("hold_sample_uses_time_of_caller", T, F,
     "\tuint64_t now = advance(hold, now_ms);\n\t// Nothing was observed", "\tuint64_t now = now_ms;\n\t// Nothing was observed"),
    ("hold_open_uses_time_of_caller", T, F,
     "\tuint64_t now = advance(hold, now_ms);\n\n\tif(hold->stuck)", "\tuint64_t now = now_ms;\n\n\tif(hold->stuck)"),
    ("hold_activity_uses_time_of_caller", T, F, ACTIVITY, "\thold->last_input_ms = now_ms;\n"),
    ("hold_permille_uses_time_of_caller", T, F, PERMILLE, "\theld = now_ms - hold->pressed_since_ms;"),
    ("hold_permille_uses_time_of_last_call", T, F, PERMILLE, "\t(void)now_ms;\n\theld = hold->clock_ms - hold->pressed_since_ms;"),

    ("hold_time_step_counted_in_32_bit", T, F, CLOCK, "\treturn hold->clock_ms + (now_ms > hold->last_ms ? (uint32_t)(now_ms - hold->last_ms) : 0);"),
    ("hold_time_compared_in_32_bit", T, F,
     CLOCK, "\treturn hold->clock_ms + ((uint32_t)now_ms > (uint32_t)hold->last_ms || now_ms > hold->last_ms ? now_ms - hold->last_ms : 0);"),
    ("hold_step_back_restarts_the_release", T, F,
     "\thold->last_ms = now_ms;", "\tif(now_ms < hold->last_ms) hold->released_since_ms = hold->clock_ms;\n\thold->last_ms = now_ms;"),

    # hold_init, hold_open, hold_close
    ("hold_init_keeps_stuck", T, F,
     "\tmemset(hold, 0, sizeof(*hold));", "\tbool stuck = hold->stuck;\n\n\tmemset(hold, 0, sizeof(*hold));\n\thold->stuck = stuck;"),
    ("hold_init_keeps_switch", T, F,
     "\tmemset(hold, 0, sizeof(*hold));", "\thold->open = false;\n\thold->stuck = false;\n\thold->watched = false;"),
    ("hold_init_focus_on_action", T, F,
     "\tmemset(hold, 0, sizeof(*hold));", "\tmemset(hold, 0, sizeof(*hold));\n\thold->on_action = true;"),
    ("hold_init_keeps_dialog", T, F,
     "\tmemset(hold, 0, sizeof(*hold));", "\tbool open = hold->open;\n\n\tmemset(hold, 0, sizeof(*hold));\n\thold->open = open;"),
    ("hold_init_keeps_reading", T, F,
     "\tmemset(hold, 0, sizeof(*hold));", "\tbool watched = hold->watched;\n\n\tmemset(hold, 0, sizeof(*hold));\n\thold->watched = watched;"),
    ("hold_init_is_a_reading", T, F,
     "\tmemset(hold, 0, sizeof(*hold));", "\tmemset(hold, 0, sizeof(*hold));\n\thold->watched = true;"),
    ("hold_open_while_stuck", T, F, "\tif(hold->stuck) return false;\n\n\thold->open = true;", "\thold->open = true;"),
    ("hold_open_unlocks", T, F, "\tif(hold->stuck) return false;\n\n\thold->open = true;", "\thold->stuck = false;\n\thold->open = true;"),
    ("hold_open_does_not_open", T, F, "\thold->open = true;\n", ""),
    ("hold_open_keeps_release_seen", T, F, "\thold->open = true;\n\thold->seen_released = false;\n", "\thold->open = true;\n"),
    ("hold_open_time_not_stored", T, F, "\thold->opened_ms = now;\n", ""),
    ("hold_open_idle_time_not_restarted", T, F, "\thold->opened_ms = now;\n\thold->last_input_ms = now;\n", "\thold->opened_ms = now;\n"),
    ("hold_open_always_false", T, F, "\thold->last_input_ms = now;\n\treturn true;", "\thold->last_input_ms = now;\n\treturn false;"),
    ("hold_open_is_a_reading", T, F, "\thold->opened_ms = now;\n", "\thold->opened_ms = now;\n\thold->sampled_ms = now;\n"),
    ("hold_open_restarts_the_released_time_at_the_next_reading", T, F, "\thold->opened_ms = now;\n", "\thold->opened_ms = now;\n\thold->watched = false;\n"),
    ("hold_open_forgets_failed_reading", T, F, "\thold->opened_ms = now;\n", "\thold->opened_ms = now;\n\thold->watched = true;\n"),
    ("hold_close_stays_open", T, F, CLOSE, "void hold_close(hold_t *hold)\n{\n\t(void)hold;\n}"),
    ("hold_close_forgets_the_switch", T, F, CLOSE, "void hold_close(hold_t *hold)\n{\n\thold->open = false;\n\thold->pressed = false;\n}"),

    # a hold is going on
    ("hold_goes_on_without_dialog", T, F, HOLDING, "\treturn hold->pressed && hold->seen_released;"),
    ("hold_goes_on_while_released", T, F, HOLDING, "\treturn hold->open && hold->seen_released;"),
    ("hold_goes_on_without_release", T, F, HOLDING, "\treturn hold->open && hold->pressed;"),

    # the reading
    ("hold_failed_reading_trusted", T, F, "\tif(!read_ok) pressed = false;\n", ""),
    ("hold_failed_reading_keeps_last_state", T, F, "\tif(!read_ok) pressed = false;", "\tif(!read_ok) pressed = hold->pressed;"),
    ("hold_failed_reading_skipped", T, F, "\tif(!read_ok) pressed = false;", "\tif(!read_ok) return HOLD_WAITING;"),
    ("hold_failed_reading_counts_as_pressed", T, F, "\tif(!read_ok) pressed = false;", "\tif(!read_ok) pressed = true;"),
    ("hold_failed_reading_does_not_break_long_press", T, F,
     "\tif(!read_ok) pressed = false;", "\tif(!read_ok && !(hold->pressed && now - hold->pressed_since_ms >= HOLD_STUCK_MS)) pressed = false;"),
    ("hold_failed_reading_while_pressed_is_no_input", T, F,
     "\t\thold->pressed = pressed;\n\t\thold->last_input_ms = now;\n", "\t\thold->pressed = pressed;\n\t\tif(read_ok) hold->last_input_ms = now;\n"),
    ("hold_switch_change_is_no_input", T, F, "\t\thold->pressed = pressed;\n\t\thold->last_input_ms = now;\n", "\t\thold->pressed = pressed;\n"),
    ("hold_only_press_is_input", T, F,
     "\t\thold->pressed = pressed;\n\t\thold->last_input_ms = now;\n", "\t\thold->pressed = pressed;\n\t\tif(pressed) hold->last_input_ms = now;\n"),
    ("hold_only_release_is_input", T, F,
     "\t\thold->pressed = pressed;\n\t\thold->last_input_ms = now;\n", "\t\thold->pressed = pressed;\n\t\tif(!pressed) hold->last_input_ms = now;\n"),
    ("hold_switch_state_not_stored", T, F, "\t\thold->pressed = pressed;\n", ""),
    ("hold_press_time_not_stored", T, F, CHANGED, "\t\tif(!pressed) hold->released_since_ms = now;"),
    ("hold_release_time_not_stored", T, F, CHANGED, "\t\tif(pressed) hold->pressed_since_ms = now;"),
    ("hold_focus_change_is_no_input", T, F, "\t\thold->on_action = on_action;\n\t\thold->last_input_ms = now;\n", "\t\thold->on_action = on_action;\n"),
    ("hold_focus_change_while_pressed_is_no_input", T, F,
     "\t\thold->on_action = on_action;\n\t\thold->last_input_ms = now;\n", "\t\thold->on_action = on_action;\n\t\tif(!pressed) hold->last_input_ms = now;\n"),
    ("hold_focus_not_followed_by_failed_reading", T, F, "\tif(on_action != hold->on_action)", "\tif(on_action != hold->on_action && read_ok)"),
    ("hold_focus_not_stored", T, F, "\t\thold->on_action = on_action;\n", ""),
    ("hold_focus_leaving_is_no_input", T, F, "\tif(on_action != hold->on_action)", "\tif(on_action && !hold->on_action)"),
    ("hold_focus_coming_is_no_input", T, F,
     "\t\thold->on_action = on_action;\n\t\thold->last_input_ms = now;\n", "\t\thold->on_action = on_action;\n\t\tif(!on_action) hold->last_input_ms = now;\n"),
    ("hold_every_reading_is_input", T, F, "\tif(on_action != hold->on_action)", "\tif(1)"),

    # a failed reading has observed nothing
    ("hold_failed_readings_are_a_release", T, F, WATCHED, "\thold->watched = true;\n"),
    ("hold_failed_reading_forgotten", T, F, WATCHED, "\tif(read_ok) hold->watched = true;\n"),
    ("hold_reading_never_counts_as_observed", T, F, WATCHED, ""),
    ("hold_failed_reading_takes_nothing_back", T, F, UNSEEN, "\tbool unseen = now - hold->sampled_ms > HOLD_GAP_MS;"),
    ("hold_failed_reading_keeps_release_that_was_seen", T, F, UNSEEN, "\tbool unseen = (!hold->watched && !hold->seen_released) || now - hold->sampled_ms > HOLD_GAP_MS;"),
    ("hold_release_counted_from_failed_reading", T, F, RESTART, "\tif(!read_ok) hold->released_since_ms = now;"),
    ("hold_failed_reading_remembered_only_in_dialog", T, F, WATCHED, "\tif(hold->open) hold->watched = read_ok;\n"),
    ("hold_activity_forgets_failed_reading", T, F, ACTIVITY, ACTIVITY + "\thold->watched = true;\n"),

    # readings that are missing
    ("hold_gap_never", T, F, UNSEEN, "\tbool unseen = !hold->watched;"),
    ("hold_gap_one_ms_early", T, F, UNSEEN, UNSEEN.replace("> HOLD_GAP_MS", ">= HOLD_GAP_MS")),
    ("hold_gap_one_ms_late", T, F, UNSEEN, UNSEEN.replace("> HOLD_GAP_MS", "> HOLD_GAP_MS + 1")),
    ("hold_gap_time_changed", T, H, "#define HOLD_GAP_MS         200u ", "#define HOLD_GAP_MS         201u "),
    ("hold_gap_is_the_time_to_confirm", T, F, UNSEEN, UNSEEN.replace("> HOLD_GAP_MS", ">= HOLD_CONFIRM_MS")),
    ("hold_gap_in_the_time_of_the_caller", T, F, UNSEEN, UNSEEN.replace("now - hold->sampled_ms", "now_ms - hold->sampled_ms")),
    ("hold_gap_counted_in_32_bit", T, F, UNSEEN, UNSEEN.replace("now - hold->sampled_ms", "(uint32_t)(now - hold->sampled_ms)")),
    ("hold_gap_counted_from_the_press", T, F, UNSEEN, UNSEEN.replace("now - hold->sampled_ms", "now - hold->pressed_since_ms")),
    ("hold_time_of_reading_not_stored", T, F, SAMPLED, ""),
    ("hold_gap_does_not_restart_released_time", T, F, RESTART, "\tif(!hold->watched) hold->released_since_ms = now;"),
    ("hold_gap_restarts_released_time_only_while_unseen", T, F, RESTART, "\tif(unseen && !hold->seen_released) hold->released_since_ms = now;"),
    ("hold_gap_takes_nothing_back", T, F, BACK, "\tif(pressed && !on_action) hold->seen_released = false;"),
    ("hold_gap_takes_release_back_only_from_a_press", T, F, BACK, "\tif((unseen && pressed) || (pressed && !on_action)) hold->seen_released = false;"),
    ("hold_gap_takes_release_back_only_from_a_released_switch", T, F, BACK, "\tif((unseen && !pressed) || (pressed && !on_action)) hold->seen_released = false;"),
    ("hold_gap_restarts_idle_time", T, F, RESTART, "\tif(unseen) hold->released_since_ms = hold->last_input_ms = now;"),
    ("hold_gap_breaks_the_press", T, F, RESTART, "\tif(unseen) hold->released_since_ms = hold->pressed_since_ms = now;"),
    ("hold_activity_is_a_reading", T, F, ACTIVITY, ACTIVITY + "\thold->sampled_ms = hold->last_input_ms;\n"),

    # a press with the focus elsewhere
    ("hold_counts_without_focus", T, F, BACK, "\tif(unseen) hold->seen_released = false;"),
    ("hold_focus_away_takes_release_back_while_released", T, F, BACK, "\tif(unseen || !on_action) hold->seen_released = false;"),
    ("hold_focus_leaving_does_not_break", T, F, BACK, "\tif(unseen || (pressed && !on_action && now == hold->pressed_since_ms)) hold->seen_released = false;"),
    ("hold_press_begun_away_counts", T, F, BACK, "\tif(unseen || (pressed && !on_action && now != hold->pressed_since_ms)) hold->seen_released = false;"),
    ("hold_focus_away_only_restarts_the_hold", T, F, BACK, "\tif(pressed && !on_action) hold->pressed_since_ms = now;\n\tif(unseen) hold->seen_released = false;"),

    # a switch that hangs
    ("hold_stuck_one_ms_late", T, F, STUCK, STUCK.replace(">= HOLD_STUCK_MS", "> HOLD_STUCK_MS")),
    ("hold_stuck_time_changed", T, H, "#define HOLD_STUCK_MS       20000u", "#define HOLD_STUCK_MS       19999u"),
    ("hold_stuck_never", T, F, STUCK + "\n", ""),
    ("hold_stuck_only_with_dialog", T, F, STUCK, STUCK.replace("if(pressed &&", "if(hold->open && pressed &&")),
    ("hold_stuck_only_without_dialog", T, F, STUCK, STUCK.replace("if(pressed &&", "if(!hold->open && pressed &&")),
    ("hold_stuck_counted_from_last_input", T, F, STUCK, STUCK.replace("hold->pressed_since_ms", "hold->last_input_ms")),
    ("hold_stuck_while_released", T, F, STUCK, STUCK.replace("if(pressed &&", "if(")),
    ("hold_stuck_reported_once", T, F,
     STUCK + "\n" + STUCK_BLOCK,
     "\tif(pressed && now - hold->pressed_since_ms >= HOLD_STUCK_MS && !hold->stuck)\n\t{\n\t\thold->stuck = true;\n\t\thold_close(hold);\n\t\treturn HOLD_STUCK;\n\t}\n"),
    ("hold_stuck_not_reported_when_released", T, F, STUCK_BLOCK, STUCK_BLOCK.replace("if(hold->stuck)", "if(hold->stuck && pressed)")),
    ("hold_stuck_forgotten_when_released", T, F, STUCK, "\tif(!pressed) hold->stuck = false;\n" + STUCK),
    ("hold_stuck_leaves_dialog_open", T, F, STUCK_BLOCK, STUCK_BLOCK.replace("\t\thold_close(hold);\n", "")),
    ("hold_stuck_after_cancelled", T, F,
     STUCK, STUCK.replace(">= HOLD_STUCK_MS)", ">= HOLD_STUCK_MS && !(hold->open && now - hold->last_input_ms >= HOLD_IDLE_MS))")),
    ("hold_is_stuck_always_false", T, F, "\treturn hold->stuck;", "\treturn hold->stuck && false;"),

    # no dialog
    ("hold_counts_without_dialog", T, F, "\tif(!hold->open) return HOLD_WAITING;\n", ""),

    # the release before the press
    ("hold_released_one_ms_late", T, F, RELEASED, RELEASED.replace("released_since_ms >= HOLD_RELEASED_MS", "released_since_ms > HOLD_RELEASED_MS")),
    ("hold_released_since_open_one_ms_late", T, F, RELEASED, RELEASED.replace("opened_ms >= HOLD_RELEASED_MS", "opened_ms > HOLD_RELEASED_MS")),
    ("hold_released_time_changed", T, H, "#define HOLD_RELEASED_MS    300u", "#define HOLD_RELEASED_MS    299u"),
    ("hold_released_one_ms_late_with_focus_away", T, F,
     RELEASED, RELEASED.replace("released_since_ms >= HOLD_RELEASED_MS", "released_since_ms >= HOLD_RELEASED_MS + (on_action ? 0u : 1u)")),
    ("hold_release_before_dialog_counts", T, F, RELEASED, RELEASED.replace(" && now - hold->opened_ms >= HOLD_RELEASED_MS", "")),
    ("hold_release_counted_from_dialog_only", T, F, RELEASED, RELEASED.replace("now - hold->released_since_ms >= HOLD_RELEASED_MS && ", "")),
    ("hold_release_either_time_enough", T, F,
     RELEASED, "\tif(!pressed && (now - hold->released_since_ms >= HOLD_RELEASED_MS || now - hold->opened_ms >= HOLD_RELEASED_MS))"),
    ("hold_release_seen_while_pressed", T, F, RELEASED, RELEASED.replace("if(!pressed && now", "if(now")),
    ("hold_release_seen_at_once", T, F, RELEASED, "\tif(!pressed)"),
    ("hold_release_never_seen", T, F, "\t\thold->seen_released = true;\n", "\t\t(void)now;\n"),
    ("hold_release_needed_before_every_press", T, F,
     CHANGED, "\t\tif(pressed) hold->pressed_since_ms = now;\n\t\telse\n\t\t{\n\t\t\thold->released_since_ms = now;\n\t\t\thold->seen_released = false;\n\t\t}"),

    # idle time
    ("hold_idle_one_ms_late", T, F, IDLE, "\tif(now - hold->last_input_ms > HOLD_IDLE_MS)"),
    ("hold_idle_time_changed", T, H, "#define HOLD_IDLE_MS        15000u", "#define HOLD_IDLE_MS        14999u"),
    ("hold_idle_never", T, F, IDLE, "\tif(0 && now - hold->last_input_ms >= HOLD_IDLE_MS)"),
    ("hold_idle_counted_from_dialog", T, F, IDLE, "\tif(now - hold->opened_ms >= HOLD_IDLE_MS)"),
    ("hold_idle_not_while_pressed", T, F, IDLE, "\tif(!pressed && now - hold->last_input_ms >= HOLD_IDLE_MS)"),
    ("hold_cancelled_reported_again", T, F, "\t\thold_close(hold);\n\t\treturn HOLD_CANCELLED;", "\t\treturn HOLD_CANCELLED;"),

    # confirmation
    ("hold_confirm_one_ms_late", T, F, CONFIRM, CONFIRM.replace(">= HOLD_CONFIRM_MS", "> HOLD_CONFIRM_MS")),
    ("hold_confirm_one_ms_early", T, F, CONFIRM, CONFIRM.replace(">= HOLD_CONFIRM_MS", ">= HOLD_CONFIRM_MS - 1")),
    ("hold_confirm_time_changed", T, H, "#define HOLD_CONFIRM_MS     3000u", "#define HOLD_CONFIRM_MS     2999u"),
    ("hold_confirm_never", T, F, CONFIRM, "\tif(0 && now - hold->pressed_since_ms >= HOLD_CONFIRM_MS)"),
    ("hold_confirm_without_hold", T, F, CONFIRM, "\tif(now - hold->pressed_since_ms >= HOLD_CONFIRM_MS)"),
    ("hold_confirm_counted_from_the_release", T, F, CONFIRM, CONFIRM.replace("hold->pressed_since_ms", "hold->released_since_ms")),
    ("hold_confirm_counted_from_the_dialog", T, F, CONFIRM, CONFIRM.replace("hold->pressed_since_ms", "hold->opened_ms")),
    ("hold_confirm_needs_more_than_3300_ms_of_dialog", T, F,
     CONFIRM, CONFIRM.replace(">= HOLD_CONFIRM_MS)", ">= HOLD_CONFIRM_MS && now - hold->opened_ms > HOLD_CONFIRM_MS + HOLD_RELEASED_MS)")),
    ("hold_confirmed_reported_again", T, F, "\t\thold_close(hold);\n\t\treturn HOLD_CONFIRMED;", "\t\treturn HOLD_CONFIRMED;"),
    ("hold_progress_not_reported", T, F, PROGRESS, "\treturn HOLD_WAITING;"),
    ("hold_progress_always_reported", T, F, PROGRESS, "\treturn HOLD_PROGRESS;"),
    ("hold_progress_while_pressed", T, F, PROGRESS, "\treturn pressed ? HOLD_PROGRESS : HOLD_WAITING;"),

    # hold_activity
    ("hold_activity_does_not_restart_idle_time", T, F, ACTIVITY, "\tadvance(hold, now_ms);\n"),
    ("hold_activity_does_not_break_hold", T, F, TURN, ""),
    ("hold_activity_restarts_the_hold", T, F, TURN, "\tif(hold->pressed) hold->pressed_since_ms = hold->last_input_ms;\n"),
    ("hold_activity_takes_release_back_while_released", T, F, TURN, "\thold->seen_released = false;\n"),
    ("hold_activity_counts_as_release", T, F, TURN, "\thold->seen_released = true;\n"),
    ("hold_activity_breaks_the_press", T, F, TURN, TURN + "\thold->pressed_since_ms = hold->last_input_ms;\n"),
    ("hold_activity_restarts_the_release", T, F, TURN, TURN + "\thold->released_since_ms = hold->last_input_ms;\n"),
    ("hold_activity_ignored_without_dialog", T, F, ACTIVITY, "\tif(!hold->open) return;\n" + ACTIVITY),

    # hold_permille
    ("hold_permille_without_hold", T, F, "\tif(!holding(hold)) return 0;\n", ""),
    ("hold_permille_counted_from_the_release", T, F, PERMILLE, PERMILLE.replace("hold->pressed_since_ms", "hold->released_since_ms")),
    ("hold_permille_counted_from_the_dialog", T, F, PERMILLE, PERMILLE.replace("hold->pressed_since_ms", "hold->opened_ms")),
    ("hold_permille_above_1000", T, F, "\tif(held >= HOLD_CONFIRM_MS) return 1000;\n", ""),
    ("hold_permille_ends_at_999", T, F, "\tif(held >= HOLD_CONFIRM_MS) return 1000;", "\tif(held >= HOLD_CONFIRM_MS) return 999;"),
    ("hold_permille_full_one_ms_early", T, F, "\tif(held >= HOLD_CONFIRM_MS) return 1000;", "\tif(held >= HOLD_CONFIRM_MS - 1) return 1000;"),
    ("hold_permille_percent", T, F, "\treturn (int)(held * 1000 / HOLD_CONFIRM_MS);", "\treturn (int)(held * 100 / HOLD_CONFIRM_MS);"),
    ("hold_permille_rounded_up", T, F,
     "\treturn (int)(held * 1000 / HOLD_CONFIRM_MS);", "\treturn (int)((held * 1000 + HOLD_CONFIRM_MS - 1) / HOLD_CONFIRM_MS);"),
    ("hold_permille_rounded_to_nearest", T, F,
     "\treturn (int)(held * 1000 / HOLD_CONFIRM_MS);", "\treturn (int)((held * 1000 + HOLD_CONFIRM_MS / 2) / HOLD_CONFIRM_MS);"),
]
