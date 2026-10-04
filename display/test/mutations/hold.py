"""Mutations of display/components/core/hold.c, see ../redproof.py."""

F = "components/core/hold.c"
H = "components/core/hold.h"
T = "test_hold"

CLOCK = "\treturn hold->clock_ms + (now_ms > hold->last_ms ? now_ms - hold->last_ms : 0);"
STUCK = "\tif(pressed && now - hold->pressed_since_ms >= HOLD_STUCK_MS) hold->stuck = true;"
STUCK_BLOCK = "\tif(hold->stuck)\n\t{\n\t\thold_close(hold);\n\t\treturn HOLD_STUCK;\n\t}\n"
RELEASED = "\tif(!pressed && now - hold->released_since_ms >= HOLD_RELEASED_MS && now - hold->opened_ms >= HOLD_RELEASED_MS)"
COUNTS = "\tif(pressed && on_action && hold->seen_released)"
IDLE = "\tif(now - hold->last_input_ms >= HOLD_IDLE_MS)"
CONFIRM = "\tif(hold->holding && now - hold->held_since_ms >= HOLD_CONFIRM_MS)"
UNSEEN = "\tif(!read_ok || hold->failed) hold->released_since_ms = now;"

MUTATIONS = [
    # the time of the module
    ("hold_time_steps_back_with_the_caller", T, F, CLOCK, "\treturn hold->clock_ms + (now_ms - hold->last_ms);"),
    ("hold_time_stands_until_caught_up", T, F, "\thold->last_ms = now_ms;", "\tif(now_ms > hold->last_ms) hold->last_ms = now_ms;"),
    ("hold_time_of_previous_call_not_stored", T, F, "\thold->last_ms = now_ms;\n", ""),
    ("hold_time_does_not_advance", T, F, "\thold->clock_ms = clock_at(hold, now_ms);\n", ""),
    ("hold_sample_uses_time_of_caller", T, F,
     "\tuint64_t now = advance(hold, now_ms);\n\n\tif(!read_ok)", "\tuint64_t now = now_ms;\n\n\tif(!read_ok)"),
    ("hold_open_uses_time_of_caller", T, F,
     "\tuint64_t now = advance(hold, now_ms);\n\n\tif(hold->stuck)", "\tuint64_t now = now_ms;\n\n\tif(hold->stuck)"),
    ("hold_activity_uses_time_of_caller", T, F, "\thold->last_input_ms = advance(hold, now_ms);", "\thold->last_input_ms = now_ms;"),
    ("hold_permille_uses_time_of_caller", T, F,
     "\theld = clock_at(hold, now_ms) - hold->held_since_ms;", "\theld = now_ms - hold->held_since_ms;"),
    ("hold_permille_uses_time_of_last_call", T, F,
     "\theld = clock_at(hold, now_ms) - hold->held_since_ms;", "\t(void)now_ms;\n\theld = hold->clock_ms - hold->held_since_ms;"),

    ("hold_time_step_counted_in_32_bit", T, F, CLOCK, "\treturn hold->clock_ms + (now_ms > hold->last_ms ? (uint32_t)(now_ms - hold->last_ms) : 0);"),
    ("hold_time_compared_in_32_bit", T, F,
     CLOCK, "\treturn hold->clock_ms + ((uint32_t)now_ms > (uint32_t)hold->last_ms || now_ms > hold->last_ms ? now_ms - hold->last_ms : 0);"),
    ("hold_step_back_restarts_the_release", T, F,
     "\thold->last_ms = now_ms;", "\tif(now_ms < hold->last_ms) hold->released_since_ms = hold->clock_ms;\n\thold->last_ms = now_ms;"),

    # hold_init, hold_open, hold_close
    ("hold_init_keeps_stuck", T, F,
     "\tmemset(hold, 0, sizeof(*hold));", "\tbool stuck = hold->stuck;\n\n\tmemset(hold, 0, sizeof(*hold));\n\thold->stuck = stuck;"),
    ("hold_init_keeps_switch", T, F,
     "\tmemset(hold, 0, sizeof(*hold));", "\thold->open = false;\n\thold->stuck = false;\n\thold->holding = false;"),
    ("hold_init_release_begins_late", T, F,
     "\tmemset(hold, 0, sizeof(*hold));", "\tmemset(hold, 0, sizeof(*hold));\n\thold->released_since_ms = 500;"),
    ("hold_init_focus_on_action", T, F,
     "\tmemset(hold, 0, sizeof(*hold));", "\tmemset(hold, 0, sizeof(*hold));\n\thold->on_action = true;"),
    ("hold_init_keeps_hold", T, F,
     "\tmemset(hold, 0, sizeof(*hold));", "\tbool holding = hold->holding;\n\n\tmemset(hold, 0, sizeof(*hold));\n\thold->holding = holding;"),
    ("hold_init_keeps_dialog", T, F,
     "\tmemset(hold, 0, sizeof(*hold));", "\tbool open = hold->open;\n\n\tmemset(hold, 0, sizeof(*hold));\n\thold->open = open;"),
    ("hold_open_while_stuck", T, F, "\tif(hold->stuck) return false;\n\n\thold->open = true;", "\thold->open = true;"),
    ("hold_open_unlocks", T, F, "\tif(hold->stuck) return false;\n\n\thold->open = true;", "\thold->stuck = false;\n\thold->open = true;"),
    ("hold_open_does_not_open", T, F, "\thold->open = true;\n", ""),
    ("hold_open_keeps_release_seen", T, F, "\thold->open = true;\n\thold->seen_released = false;\n", "\thold->open = true;\n"),
    ("hold_open_keeps_hold", T, F, "\thold->seen_released = false;\n\thold->holding = false;\n", "\thold->seen_released = false;\n"),
    ("hold_open_time_not_stored", T, F, "\thold->opened_ms = now;\n", ""),
    ("hold_open_idle_time_not_restarted", T, F, "\thold->opened_ms = now;\n\thold->last_input_ms = now;\n", "\thold->opened_ms = now;\n"),
    ("hold_open_always_false", T, F, "\thold->last_input_ms = now;\n\treturn true;", "\thold->last_input_ms = now;\n\treturn false;"),
    ("hold_close_stays_open", T, F, "\thold->open = false;\n\thold->holding = false;\n", "\thold->holding = false;\n"),
    ("hold_close_keeps_hold", T, F, "\thold->open = false;\n\thold->holding = false;\n", "\thold->open = false;\n"),

    # the reading
    ("hold_failed_reading_trusted", T, F, "\tif(!read_ok) pressed = false;\n", "\t(void)read_ok;\n"),
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
    ("hold_press_time_not_stored", T, F,
     "\t\tif(pressed) hold->pressed_since_ms = now;\n\t\telse hold->released_since_ms = now;", "\t\tif(!pressed) hold->released_since_ms = now;"),
    ("hold_release_time_not_stored", T, F,
     "\t\tif(pressed) hold->pressed_since_ms = now;\n\t\telse hold->released_since_ms = now;", "\t\tif(pressed) hold->pressed_since_ms = now;"),
    ("hold_focus_change_is_no_input", T, F, "\t\thold->on_action = on_action;\n\t\thold->last_input_ms = now;\n", "\t\thold->on_action = on_action;\n"),
    ("hold_focus_change_while_pressed_is_no_input", T, F,
     "\t\thold->on_action = on_action;\n\t\thold->last_input_ms = now;\n", "\t\thold->on_action = on_action;\n\t\tif(!pressed) hold->last_input_ms = now;\n"),
    ("hold_focus_not_followed_by_failed_reading", T, F, "\tif(on_action != hold->on_action)", "\tif(on_action != hold->on_action && read_ok)"),
    ("hold_focus_not_stored", T, F, "\t\thold->on_action = on_action;\n", ""),
    ("hold_focus_leaving_is_no_input", T, F, "\tif(on_action != hold->on_action)", "\tif(on_action && !hold->on_action)"),
    ("hold_focus_coming_is_no_input", T, F,
     "\t\thold->on_action = on_action;\n\t\thold->last_input_ms = now;\n", "\t\thold->on_action = on_action;\n\t\tif(!on_action) hold->last_input_ms = now;\n"),
    ("hold_every_reading_is_input", T, F, "\tif(on_action != hold->on_action)", "\tif(1)"),

    # a failed reading is no release that was seen
    ("hold_failed_readings_are_a_release", T, F, UNSEEN + "\n", ""),
    ("hold_release_counted_from_failed_reading", T, F, UNSEEN, "\tif(!read_ok) hold->released_since_ms = now;"),
    ("hold_failed_reading_completes_release", T, F, UNSEEN, "\tif(hold->failed) hold->released_since_ms = now;"),
    ("hold_failed_reading_not_remembered", T, F, "\thold->failed = !read_ok;\n", ""),
    ("hold_failed_reading_never_forgotten", T, F, "\thold->failed = !read_ok;", "\tif(!read_ok) hold->failed = true;"),
    ("hold_failed_reading_takes_release_back", T, F, UNSEEN, UNSEEN + "\n\tif(!read_ok) hold->seen_released = false;"),
    ("hold_failed_reading_only_restarts_long_release", T, F,
     UNSEEN, "\tif((!read_ok || hold->failed) && now - hold->released_since_ms >= HOLD_RELEASED_MS) hold->released_since_ms = now;"),
    ("hold_init_previous_reading_failed", T, F,
     "\tmemset(hold, 0, sizeof(*hold));", "\tmemset(hold, 0, sizeof(*hold));\n\thold->failed = true;"),

    ("hold_open_forgets_failed_reading", T, F, "\thold->opened_ms = now;\n", "\thold->opened_ms = now;\n\thold->failed = false;\n"),
    ("hold_failed_reading_remembered_only_in_dialog", T, F, "\thold->failed = !read_ok;", "\tif(hold->open) hold->failed = !read_ok;"),
    ("hold_activity_forgets_failed_reading", T, F,
     "\thold->last_input_ms = advance(hold, now_ms);\n\thold->holding = false;\n",
     "\thold->last_input_ms = advance(hold, now_ms);\n\thold->holding = false;\n\thold->failed = false;\n"),

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
    ("hold_stuck_after_confirmed", T, F,
     STUCK, STUCK.replace(">= HOLD_STUCK_MS)", ">= HOLD_STUCK_MS && !(hold->holding && now - hold->held_since_ms >= HOLD_CONFIRM_MS))")),
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
     "\telse\n\t{\n\t\thold->holding = false;\n\t}", "\telse\n\t{\n\t\tif(hold->holding) hold->seen_released = false;\n\t\thold->holding = false;\n\t}"),

    # the hold
    ("hold_counts_without_focus", T, F, COUNTS, "\tif(pressed && hold->seen_released)"),
    ("hold_counts_without_release", T, F, COUNTS, "\tif(pressed && on_action)"),
    ("hold_counts_while_released", T, F, COUNTS, "\tif(on_action && hold->seen_released)"),
    ("hold_restarts_with_every_reading", T, F, "\t\tif(!hold->holding)\n\t\t{", "\t\tif(1)\n\t\t{"),
    ("hold_not_broken", T, F, "\telse\n\t{\n\t\thold->holding = false;\n\t}", "\telse\n\t{\n\t\t(void)now;\n\t}"),
    ("hold_not_broken_by_focus", T, F,
     "\telse\n\t{\n\t\thold->holding = false;\n\t}", "\telse\n\t{\n\t\tif(!pressed) hold->holding = false;\n\t}"),
    ("hold_not_broken_by_release", T, F,
     "\telse\n\t{\n\t\thold->holding = false;\n\t}", "\telse\n\t{\n\t\tif(pressed) hold->holding = false;\n\t}"),
    ("hold_counted_from_the_press", T, F, "\t\t\thold->held_since_ms = now;", "\t\t\thold->held_since_ms = hold->pressed_since_ms;"),
    ("hold_start_not_stored", T, F, "\t\t\thold->held_since_ms = now;\n", ""),
    ("hold_start_is_input", T, F, "\t\t\thold->held_since_ms = now;\n", "\t\t\thold->held_since_ms = now;\n\t\t\thold->last_input_ms = now;\n"),
    ("hold_needs_new_press_after_a_break", T, F,
     "\t\tif(!hold->holding)\n\t\t{", "\t\tif(!hold->holding && hold->pressed_since_ms == now)\n\t\t{"),

    # idle time
    ("hold_idle_one_ms_late", T, F, IDLE, "\tif(now - hold->last_input_ms > HOLD_IDLE_MS)"),
    ("hold_idle_time_changed", T, H, "#define HOLD_IDLE_MS        15000u", "#define HOLD_IDLE_MS        14999u"),
    ("hold_idle_never", T, F, IDLE, "\tif(0 && now - hold->last_input_ms >= HOLD_IDLE_MS)"),
    ("hold_idle_counted_from_dialog", T, F, IDLE, "\tif(now - hold->opened_ms >= HOLD_IDLE_MS)"),
    ("hold_idle_not_while_pressed", T, F, IDLE, "\tif(!pressed && now - hold->last_input_ms >= HOLD_IDLE_MS)"),
    ("hold_cancelled_reported_again", T, F, "\t\thold_close(hold);\n\t\treturn HOLD_CANCELLED;", "\t\treturn HOLD_CANCELLED;"),
    ("hold_cancelled_after_confirmed", T, F,
     IDLE, "\tif(now - hold->last_input_ms >= HOLD_IDLE_MS && !(hold->holding && now - hold->held_since_ms >= HOLD_CONFIRM_MS))"),

    # confirmation
    ("hold_confirm_one_ms_late", T, F, CONFIRM, "\tif(hold->holding && now - hold->held_since_ms > HOLD_CONFIRM_MS)"),
    ("hold_confirm_one_ms_early", T, F, CONFIRM, "\tif(hold->holding && now - hold->held_since_ms >= HOLD_CONFIRM_MS - 1)"),
    ("hold_confirm_time_changed", T, H, "#define HOLD_CONFIRM_MS     3000u", "#define HOLD_CONFIRM_MS     2999u"),
    ("hold_confirm_never", T, F, CONFIRM, "\tif(0 && now - hold->held_since_ms >= HOLD_CONFIRM_MS)"),
    ("hold_confirm_without_hold", T, F, CONFIRM, "\tif(now - hold->held_since_ms >= HOLD_CONFIRM_MS)"),
    ("hold_confirm_counted_from_the_press", T, F, CONFIRM, "\tif(hold->holding && now - hold->pressed_since_ms >= HOLD_CONFIRM_MS)"),
    ("hold_confirm_needs_more_than_3300_ms_of_dialog", T, F,
     CONFIRM, "\tif(hold->holding && now - hold->held_since_ms >= HOLD_CONFIRM_MS && now - hold->opened_ms > HOLD_CONFIRM_MS + HOLD_RELEASED_MS)"),
    ("hold_confirm_not_in_last_ms_before_stuck", T, F,
     CONFIRM, "\tif(hold->holding && now - hold->held_since_ms >= HOLD_CONFIRM_MS && now - hold->pressed_since_ms < HOLD_STUCK_MS - 1)"),
    ("hold_confirmed_reported_again", T, F, "\t\thold_close(hold);\n\t\treturn HOLD_CONFIRMED;", "\t\treturn HOLD_CONFIRMED;"),
    ("hold_progress_not_reported", T, F, "\treturn hold->holding ? HOLD_PROGRESS : HOLD_WAITING;", "\treturn HOLD_WAITING;"),
    ("hold_progress_always_reported", T, F, "\treturn hold->holding ? HOLD_PROGRESS : HOLD_WAITING;", "\treturn HOLD_PROGRESS;"),
    ("hold_progress_while_pressed", T, F, "\treturn hold->holding ? HOLD_PROGRESS : HOLD_WAITING;", "\treturn pressed ? HOLD_PROGRESS : HOLD_WAITING;"),

    # hold_activity
    ("hold_activity_does_not_restart_idle_time", T, F, "\thold->last_input_ms = advance(hold, now_ms);", "\tadvance(hold, now_ms);"),
    ("hold_activity_does_not_break_hold", T, F, "\thold->last_input_ms = advance(hold, now_ms);\n\thold->holding = false;\n", "\thold->last_input_ms = advance(hold, now_ms);\n"),
    ("hold_activity_counts_as_release", T, F,
     "\thold->last_input_ms = advance(hold, now_ms);\n\thold->holding = false;\n",
     "\thold->last_input_ms = advance(hold, now_ms);\n\thold->holding = false;\n\thold->seen_released = true;\n"),
    ("hold_activity_breaks_the_press", T, F,
     "\thold->last_input_ms = advance(hold, now_ms);\n\thold->holding = false;\n",
     "\thold->last_input_ms = advance(hold, now_ms);\n\thold->holding = false;\n\thold->pressed_since_ms = hold->last_input_ms;\n"),

    ("hold_activity_restarts_the_release", T, F,
     "\thold->last_input_ms = advance(hold, now_ms);\n\thold->holding = false;\n",
     "\thold->last_input_ms = advance(hold, now_ms);\n\thold->holding = false;\n\thold->released_since_ms = hold->last_input_ms;\n"),
    ("hold_activity_ignored_without_dialog", T, F,
     "\thold->last_input_ms = advance(hold, now_ms);\n\thold->holding = false;\n",
     "\tif(!hold->open) return;\n\thold->last_input_ms = advance(hold, now_ms);\n\thold->holding = false;\n"),

    # hold_permille
    ("hold_permille_without_hold", T, F, "\tif(!hold->holding) return 0;\n", ""),
    ("hold_permille_above_1000", T, F, "\tif(held >= HOLD_CONFIRM_MS) return 1000;\n", ""),
    ("hold_permille_ends_at_999", T, F, "\tif(held >= HOLD_CONFIRM_MS) return 1000;", "\tif(held >= HOLD_CONFIRM_MS) return 999;"),
    ("hold_permille_full_one_ms_early", T, F, "\tif(held >= HOLD_CONFIRM_MS) return 1000;", "\tif(held >= HOLD_CONFIRM_MS - 1) return 1000;"),
    ("hold_permille_percent", T, F, "\treturn (int)(held * 1000 / HOLD_CONFIRM_MS);", "\treturn (int)(held * 100 / HOLD_CONFIRM_MS);"),
    ("hold_permille_rounded_up", T, F,
     "\treturn (int)(held * 1000 / HOLD_CONFIRM_MS);", "\treturn (int)((held * 1000 + HOLD_CONFIRM_MS - 1) / HOLD_CONFIRM_MS);"),
    ("hold_permille_rounded_to_nearest", T, F,
     "\treturn (int)(held * 1000 / HOLD_CONFIRM_MS);", "\treturn (int)((held * 1000 + HOLD_CONFIRM_MS / 2) / HOLD_CONFIRM_MS);"),
]
