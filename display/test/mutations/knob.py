"""Mutations of display/components/core/knob.c, see ../redproof.py."""

F = "components/core/knob.c"
H = "components/core/knob.h"
T = "test_knob"

CLOCK = "\tif(now_ms > knob->clock_ms) knob->clock_ms = now_ms;\n"
INIT = "\tmemset(knob, 0, sizeof(*knob));\n"
SAMPLE_NOW = "\tuint64_t now = advance(knob, now_ms);\n\tknob_event_t event = KNOB_NONE;\n"
TURN_NOW = "\tuint64_t now = advance(knob, now_ms);\n\tint detents;\n"
FAILED = "\tif(!read_ok) return KNOB_NONE;\n"
LONG = "\t\tif(pressed && !knob->long_sent && now - knob->pressed_since_ms >= KNOB_LONG_MS)\n"
LONG_BLOCK = LONG + "\t\t{\n\t\t\tknob->long_sent = true;\n\t\t\tevent = KNOB_LONG;\n\t\t}\n"
SAME = "\tif(pressed == knob->pressed)\n\t{\n\t\tknob->run = 0;\n"
ROW = "\telse if(++knob->run >= KNOB_DEBOUNCE)\n"
CHANGE = "\t\tknob->run = 0;\n\t\tknob->pressed = pressed;\n"
PRESS = "\t\tif(pressed) knob->pressed_since_ms = now;\n"
SHORT = "\t\telse if(!knob->long_sent && now - knob->pressed_since_ms < KNOB_LONG_MS) event = KNOB_SHORT;\n"
SEEN = "\tif(!pressed && !knob->pressed) knob->long_sent = false;\n"
FAULT = "\tif(counts > COUNTS_FAULT || counts < -COUNTS_FAULT)\n"
FAULT_BLOCK = FAULT + "\t{\n\t\tknob->rest = 0;\n\t\treturn 0;\n\t}\n"
ZERO = "\tif(counts == 0) return 0;\n"
DROP = "\tif(now - knob->last_count_ms >= KNOB_REST_MS) knob->rest = 0;\n"
COUNTED = "\tknob->last_count_ms = now;\n"
ADD = "\tcounts += knob->rest;\n"
DIVIDE = "\tdetents = counts / KNOB_COUNTS_PER_DETENT;\n\tknob->rest = counts % KNOB_COUNTS_PER_DETENT;\n"
SIGN = "\treturn knob->reverse ? -detents : detents;\n"
LONG_ANYWAY = ("\t\tif(knob->pressed && !knob->long_sent && now - knob->pressed_since_ms >= KNOB_LONG_MS)\n"
               "\t\t{\n\t\t\tknob->long_sent = true;\n\t\t\treturn KNOB_LONG;\n\t\t}\n")

MUTATIONS = [
    # the time of the module
    ("knob_time_steps_back_with_the_caller", T, F, CLOCK, "\tknob->clock_ms = now_ms;\n"),
    ("knob_time_does_not_advance", T, F, CLOCK, "\t(void)now_ms;\n"),
    ("knob_time_of_the_caller_used", T, F, "\treturn knob->clock_ms;\n", "\treturn now_ms;\n"),
    ("knob_sample_time_not_remembered", T, F,
     SAMPLE_NOW, SAMPLE_NOW.replace("advance(knob, now_ms)", "now_ms > knob->clock_ms ? now_ms : knob->clock_ms")),
    ("knob_sample_uses_time_of_caller", T, F, SAMPLE_NOW, SAMPLE_NOW.replace("advance(knob, now_ms)", "now_ms")),
    ("knob_turn_time_not_remembered", T, F,
     TURN_NOW, TURN_NOW.replace("advance(knob, now_ms)", "now_ms > knob->clock_ms ? now_ms : knob->clock_ms")),
    ("knob_turn_uses_time_of_caller", T, F, TURN_NOW, TURN_NOW.replace("advance(knob, now_ms)", "now_ms")),
    ("knob_failed_reading_gives_no_time", T, F,
     SAMPLE_NOW, SAMPLE_NOW.replace("advance(knob, now_ms)", "read_ok ? advance(knob, now_ms) : knob->clock_ms")),

    # knob_init, knob_set_reverse
    ("knob_init_keeps_everything", T, F, INIT, ""),
    ("knob_init_keeps_switch", T, F, INIT, "\tknob->run = 0;\n\tknob->clock_ms = 0;\n\tknob->rest = 0;\n"),
    ("knob_init_keeps_row", T, F, INIT, "\tknob->pressed = false;\n\tknob->clock_ms = 0;\n\tknob->rest = 0;\n"),
    ("knob_init_keeps_time", T, F, INIT, "\tknob->pressed = false;\n\tknob->run = 0;\n\tknob->rest = 0;\n"),
    ("knob_init_keeps_rest", T, F, INIT, "\tknob->pressed = false;\n\tknob->run = 0;\n\tknob->clock_ms = 0;\n"),
    ("knob_init_reverse_ignored", T, F, INIT + "\tknob->reverse = reverse;\n", INIT + "\t(void)reverse;\n"),
    ("knob_init_always_reverse", T, F, INIT + "\tknob->reverse = reverse;\n", INIT + "\tknob->reverse = reverse || true;\n"),
    ("knob_init_first_press_counts", T, F, "\tknob->long_sent = true;\n}\n", "}\n"),
    ("knob_set_reverse_does_nothing", T, F,
     "\tknob->reverse = reverse;\n}\n\nknob_event_t", "\t(void)knob;\n\t(void)reverse;\n}\n\nknob_event_t"),
    ("knob_set_reverse_only_switches_on", T, F,
     "\tknob->reverse = reverse;\n}\n\nknob_event_t", "\tif(reverse) knob->reverse = true;\n}\n\nknob_event_t"),
    ("knob_set_reverse_only_switches_off", T, F,
     "\tknob->reverse = reverse;\n}\n\nknob_event_t", "\tif(!reverse) knob->reverse = false;\n}\n\nknob_event_t"),
    ("knob_set_reverse_toggles", T, F,
     "\tknob->reverse = reverse;\n}\n\nknob_event_t", "\t(void)reverse;\n\tknob->reverse = !knob->reverse;\n}\n\nknob_event_t"),
    ("knob_set_reverse_drops_rest", T, F,
     "\tknob->reverse = reverse;\n}\n\nknob_event_t", "\tknob->reverse = reverse;\n\tknob->rest = 0;\n}\n\nknob_event_t"),
    ("knob_set_reverse_turns_rest", T, F,
     "\tknob->reverse = reverse;\n}\n\nknob_event_t",
     "\tif(reverse != knob->reverse) knob->rest = -knob->rest;\n\tknob->reverse = reverse;\n}\n\nknob_event_t"),
    ("knob_set_reverse_releases_switch", T, F,
     "\tknob->reverse = reverse;\n}\n\nknob_event_t", "\tknob->reverse = reverse;\n\tknob->pressed = false;\n}\n\nknob_event_t"),

    # failed readings
    ("knob_failed_reading_trusted", T, F, FAILED, "\t(void)read_ok;\n"),
    ("knob_failed_reading_counts_as_released", T, F, FAILED, "\tif(!read_ok) pressed = false;\n"),
    ("knob_failed_reading_counts_as_pressed", T, F, FAILED, "\tif(!read_ok) pressed = true;\n"),
    ("knob_failed_reading_counts_as_last_state", T, F, FAILED, "\tif(!read_ok) pressed = knob->pressed;\n"),
    ("knob_failed_reading_breaks_the_row", T, F, FAILED, "\tif(!read_ok)\n\t{\n\t\tknob->run = 0;\n\t\treturn KNOB_NONE;\n\t}\n"),
    ("knob_failed_reading_reports_long_press", T, F, FAILED, "\tif(!read_ok)\n\t{\n" + LONG_ANYWAY + "\t\treturn KNOB_NONE;\n\t}\n"),
    ("knob_failed_reading_is_a_release_seen", T, F,
     FAILED, "\tif(!read_ok)\n\t{\n\t\tif(!pressed && !knob->pressed) knob->long_sent = false;\n\t\treturn KNOB_NONE;\n\t}\n"),
    ("knob_failed_reading_while_released_trusted", T, F, FAILED, "\tif(!read_ok && knob->pressed) return KNOB_NONE;\n"),
    ("knob_failed_reading_while_pressed_trusted", T, F, FAILED, "\tif(!read_ok && !knob->pressed) return KNOB_NONE;\n"),

    # readings in a row
    ("knob_debounce_one_reading", T, H, "#define KNOB_DEBOUNCE           2", "#define KNOB_DEBOUNCE           1"),
    ("knob_debounce_three_readings", T, H, "#define KNOB_DEBOUNCE           2", "#define KNOB_DEBOUNCE           3"),
    ("knob_row_one_short", T, F, ROW, "\telse if(++knob->run >= KNOB_DEBOUNCE - 1)\n"),
    ("knob_row_one_more", T, F, ROW, "\telse if(++knob->run > KNOB_DEBOUNCE)\n"),
    ("knob_row_not_broken", T, F, SAME, "\tif(pressed == knob->pressed)\n\t{\n"),
    ("knob_row_broken_only_while_released", T, F, SAME, "\tif(pressed == knob->pressed)\n\t{\n\t\tif(!pressed) knob->run = 0;\n"),
    ("knob_row_broken_only_while_pressed", T, F, SAME, "\tif(pressed == knob->pressed)\n\t{\n\t\tif(pressed) knob->run = 0;\n"),
    ("knob_row_goes_on_after_change", T, F, CHANGE, "\t\tknob->pressed = pressed;\n"),
    ("knob_row_not_counted", T, F, ROW, "\telse if(knob->run >= KNOB_DEBOUNCE)\n"),
    ("knob_press_needs_one_reading", T, F, ROW, "\telse if(++knob->run >= KNOB_DEBOUNCE || pressed)\n"),
    ("knob_release_needs_one_reading", T, F, ROW, "\telse if(++knob->run >= KNOB_DEBOUNCE || !pressed)\n"),
    ("knob_state_not_stored", T, F, CHANGE, "\t\tknob->run = 0;\n"),
    ("knob_never_released", T, F, CHANGE, "\t\tknob->run = 0;\n\t\tif(pressed) knob->pressed = true;\n"),

    # the short press
    ("knob_press_time_not_stored", T, F, PRESS + SHORT, SHORT.replace("else if(!knob->long_sent", "if(!pressed && !knob->long_sent")),
    ("knob_press_time_from_caller", T, F, PRESS, "\t\tif(pressed) knob->pressed_since_ms = now_ms;\n"),
    ("knob_press_time_only_first_press", T, F, PRESS, "\t\tif(pressed && knob->pressed_since_ms == 0) knob->pressed_since_ms = now;\n"),
    ("knob_short_one_ms_late", T, F, SHORT, SHORT.replace("< KNOB_LONG_MS", "<= KNOB_LONG_MS")),
    ("knob_short_one_ms_early", T, F, SHORT, SHORT.replace("< KNOB_LONG_MS", "< KNOB_LONG_MS - 1")),
    ("knob_short_one_reading_early", T, F, SHORT, SHORT.replace("< KNOB_LONG_MS", "< KNOB_LONG_MS - 20")),
    ("knob_short_whatever_the_time", T, F, SHORT, "\t\telse if(!knob->long_sent) event = KNOB_SHORT;\n"),
    ("knob_short_for_press_found_at_start", T, F, SHORT, "\t\telse if(now - knob->pressed_since_ms < KNOB_LONG_MS) event = KNOB_SHORT;\n"),
    ("knob_short_never", T, F, SHORT, SHORT.replace("event = KNOB_SHORT", "event = KNOB_NONE")),
    ("knob_short_reported_as_long", T, F, SHORT, SHORT.replace("event = KNOB_SHORT", "event = KNOB_LONG")),
    ("knob_short_at_the_press", T, F,
     PRESS + SHORT, "\t\tif(pressed)\n\t\t{\n\t\t\tknob->pressed_since_ms = now;\n\t\t\tif(!knob->long_sent) event = KNOB_SHORT;\n\t\t}\n"),
    ("knob_release_after_long_press_reports_long", T, F,
     SHORT, SHORT + "\t\telse if(!pressed && !knob->long_sent) event = KNOB_LONG;\n"),

    # the long press
    ("knob_long_one_ms_late", T, F, LONG, LONG.replace(">= KNOB_LONG_MS", "> KNOB_LONG_MS")),
    ("knob_long_one_ms_early", T, F, LONG, LONG.replace(">= KNOB_LONG_MS", ">= KNOB_LONG_MS - 1")),
    ("knob_long_time_changed", T, H, "#define KNOB_LONG_MS            800u", "#define KNOB_LONG_MS            801u"),
    ("knob_long_never", T, F, LONG_BLOCK, LONG_BLOCK.replace("event = KNOB_LONG", "event = KNOB_NONE")),
    ("knob_long_reported_as_short", T, F, LONG_BLOCK, LONG_BLOCK.replace("event = KNOB_LONG", "event = KNOB_SHORT")),
    ("knob_long_reported_with_every_reading", T, F, LONG_BLOCK, LONG_BLOCK.replace("\t\t\tknob->long_sent = true;\n", "")),
    ("knob_long_sent_not_looked_at", T, F, LONG, LONG.replace("pressed && !knob->long_sent &&", "pressed &&")),
    ("knob_long_while_released", T, F, LONG, LONG.replace("pressed && !knob->long_sent &&", "!knob->long_sent &&")),
    ("knob_long_by_reading_that_reads_released", T, F,
     SAME, LONG_ANYWAY.replace("\t\t", "\t").replace("return KNOB_LONG", "event = KNOB_LONG") + SAME),
    ("knob_long_at_late_release", T, F,
     SHORT, SHORT + "\t\tif(!pressed && !knob->long_sent && event == KNOB_NONE) event = KNOB_LONG;\n"),
    ("knob_long_then_short_at_release", T, F, LONG_BLOCK, LONG_BLOCK.replace("\t\t\tknob->long_sent = true;\n", "\t\t\tknob->pressed_since_ms = now;\n")),

    # the press found at the start
    ("knob_release_seen_never", T, F, SEEN, ""),
    ("knob_release_seen_during_press", T, F, SEEN, "\tif(!pressed) knob->long_sent = false;\n"),
    ("knob_release_seen_only_at_the_release", T, F,
     SEEN, "\tif(!pressed && !knob->pressed && event != KNOB_NONE) knob->long_sent = false;\n"),
    ("knob_release_seen_only_between_presses", T, F,
     SEEN, "\tif(!pressed && !knob->pressed && knob->run == 0 && event == KNOB_NONE && knob->pressed_since_ms == 0) knob->long_sent = false;\n"),
    ("knob_release_seen_whatever_is_read", T, F, SEEN, "\tif(!knob->pressed) knob->long_sent = false;\n"),

    # knob_is_pressed
    ("knob_is_pressed_always_false", T, F, "\treturn knob->pressed;\n", "\treturn knob->pressed && false;\n"),
    ("knob_is_pressed_with_first_reading", T, F, "\treturn knob->pressed;\n", "\treturn knob->pressed || knob->run > 0;\n"),
    ("knob_is_pressed_not_during_bounce", T, F, "\treturn knob->pressed;\n", "\treturn knob->pressed && knob->run == 0;\n"),
    ("knob_is_pressed_not_after_long_press", T, F, "\treturn knob->pressed;\n", "\treturn knob->pressed && !knob->long_sent;\n"),

    # a fault of the counter
    ("knob_fault_from_1002", T, F, FAULT, FAULT.replace("counts > COUNTS_FAULT", "counts > COUNTS_FAULT + 1")),
    ("knob_fault_from_minus_1002", T, F, FAULT, FAULT.replace("counts < -COUNTS_FAULT", "counts < -COUNTS_FAULT - 1")),
    ("knob_fault_from_1000", T, F, FAULT, FAULT.replace("counts > COUNTS_FAULT", "counts >= COUNTS_FAULT")),
    ("knob_fault_from_minus_1000", T, F, FAULT, FAULT.replace("counts < -COUNTS_FAULT", "counts <= -COUNTS_FAULT")),
    ("knob_fault_forth_not_checked", T, F, FAULT, "\tif(counts < -COUNTS_FAULT)\n"),
    ("knob_fault_back_not_checked", T, F, FAULT, "\tif(counts > COUNTS_FAULT)\n"),
    ("knob_fault_keeps_rest", T, F, FAULT_BLOCK, FAULT + "\t{\n\t\treturn 0;\n\t}\n"),
    ("knob_fault_keeps_rest_against_it", T, F,
     FAULT_BLOCK, FAULT + "\t{\n\t\tif((counts > 0) == (knob->rest > 0)) knob->rest = 0;\n\t\treturn 0;\n\t}\n"),
    ("knob_fault_counted_as_limit", T, F,
     FAULT_BLOCK, "\tif(counts > COUNTS_FAULT) counts = COUNTS_FAULT;\n\tif(counts < -COUNTS_FAULT) counts = -COUNTS_FAULT;\n"),

    # the rest of a detent
    ("knob_zero_counts_are_a_count", T, F, ZERO, ""),
    ("knob_zero_counts_drop_rest", T, F, ZERO, "\tif(counts == 0)\n\t{\n\t\tknob->rest = 0;\n\t\treturn 0;\n\t}\n"),
    ("knob_rest_one_ms_late", T, F, DROP, DROP.replace(">= KNOB_REST_MS", "> KNOB_REST_MS")),
    ("knob_rest_one_ms_early", T, F, DROP, DROP.replace(">= KNOB_REST_MS", ">= KNOB_REST_MS - 1")),
    ("knob_rest_time_changed", T, H, "#define KNOB_REST_MS            500u", "#define KNOB_REST_MS            501u"),
    ("knob_rest_never_dropped", T, F, DROP, ""),
    ("knob_rest_always_dropped", T, F, DROP, "\tknob->rest = 0;\n"),
    ("knob_rest_back_not_dropped", T, F, DROP, DROP.replace(">= KNOB_REST_MS)", ">= KNOB_REST_MS && knob->rest > 0)")),
    ("knob_rest_forth_not_dropped", T, F, DROP, DROP.replace(">= KNOB_REST_MS)", ">= KNOB_REST_MS && knob->rest < 0)")),
    ("knob_rest_time_not_stored", T, F, COUNTED, ""),
    ("knob_rest_time_from_caller", T, F, COUNTED, "\tknob->last_count_ms = now_ms;\n"),
    ("knob_rest_time_from_first_count", T, F, COUNTED, "\tif(knob->rest == 0) knob->last_count_ms = now;\n"),
    ("knob_rest_dropped_by_change_of_direction", T, F,
     ADD, "\tif((counts > 0) != (knob->rest > 0)) knob->rest = 0;\n" + ADD),

    # detents
    ("knob_two_counts_per_detent", T, H, "#define KNOB_COUNTS_PER_DETENT  4", "#define KNOB_COUNTS_PER_DETENT  2"),
    ("knob_five_counts_per_detent", T, H, "#define KNOB_COUNTS_PER_DETENT  4", "#define KNOB_COUNTS_PER_DETENT  5"),
    ("knob_rest_not_added", T, F, ADD, ""),
    ("knob_rest_not_kept", T, F, DIVIDE, DIVIDE.replace("knob->rest = counts % KNOB_COUNTS_PER_DETENT", "knob->rest = 0")),
    ("knob_rest_keeps_whole_detents", T, F, DIVIDE, DIVIDE.replace("knob->rest = counts % KNOB_COUNTS_PER_DETENT", "knob->rest = counts")),
    ("knob_rest_loses_direction", T, F,
     DIVIDE, DIVIDE.replace("knob->rest = counts % KNOB_COUNTS_PER_DETENT", "knob->rest = (counts < 0 ? -counts : counts) % KNOB_COUNTS_PER_DETENT")),
    ("knob_detents_rounded_down", T, F,
     DIVIDE, "\tdetents = (counts - (counts < 0 ? KNOB_COUNTS_PER_DETENT - 1 : 0)) / KNOB_COUNTS_PER_DETENT;\n"
             "\tknob->rest = counts - detents * KNOB_COUNTS_PER_DETENT;\n"),
    ("knob_detents_rounded_to_nearest", T, F,
     DIVIDE, "\tdetents = (counts + (counts < 0 ? -KNOB_COUNTS_PER_DETENT / 2 : KNOB_COUNTS_PER_DETENT / 2)) / KNOB_COUNTS_PER_DETENT;\n"
             "\tknob->rest = counts - detents * KNOB_COUNTS_PER_DETENT;\n"),
    ("knob_detents_at_most_one_per_call", T, F,
     DIVIDE, DIVIDE + "\tif(detents > 1) detents = 1;\n\tif(detents < -1) detents = -1;\n"),

    # reverse
    ("knob_reverse_ignored", T, F, SIGN, "\treturn detents;\n"),
    ("knob_reverse_always", T, F, SIGN, "\treturn -detents;\n"),
    ("knob_reverse_only_forth", T, F, SIGN, "\treturn knob->reverse && detents > 0 ? -detents : detents;\n"),
    ("knob_reverse_only_back", T, F, SIGN, "\treturn knob->reverse && detents < 0 ? -detents : detents;\n"),
    ("knob_reverse_turns_the_counts", T, F,
     ADD + DIVIDE + SIGN, "\tif(knob->reverse) counts = -counts;\n" + ADD + DIVIDE + "\treturn detents;\n"),
]
