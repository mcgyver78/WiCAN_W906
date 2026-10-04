"""Mutations of display/components/core/touch.c, see ../redproof.py."""

F = "components/core/touch.c"
H = "components/core/touch.h"
T = "test_touch"

ADVANCE = "\tif(now_ms > touch->clock_ms) touch->clock_ms = now_ms;"
NOW = "\tuint64_t now = advance(touch, now_ms);"
ON_SCREEN = "\treturn x >= 0 && x < TOUCH_SIZE && y >= 0 && y < TOUCH_SIZE;"
DISTANCE = "\treturn a > b ? a - b : b - a;"
LASTED = "\tuint64_t lasted = now - touch->start_ms;"
TAP = "\tif(!touch->moved && lasted <= TOUCH_TAP_MS) return TOUCH_TAP;"
LATE = "\tif(lasted > TOUCH_SWIPE_MS) return TOUCH_NONE;"
SWIPE_X = "\tif(far_x >= TOUCH_SWIPE_MOVE && far_x >= 2 * far_y)"
SWIPE_Y = "\tif(far_y >= TOUCH_SWIPE_MOVE && far_y >= 2 * far_x)"
DIRECTION_X = "\t\treturn touch->last_x < touch->start_x ? TOUCH_SWIPE_LEFT : TOUCH_SWIPE_RIGHT;"
DIRECTION_Y = "\t\treturn touch->last_y < touch->start_y ? TOUCH_SWIPE_UP : TOUCH_SWIPE_DOWN;"
SMEAR = "\treturn TOUCH_NONE;\n}\n\nvoid touch_init"
INIT = "\tmemset(touch, 0, sizeof(*touch));"
OUTSIDE = "\tif(down && !on_screen(x, y)) read_ok = false;"
FAILED = "\tif(!read_ok)\n\t{\n\t\tif(!touch->failing)"
FAILING = "\t\t\ttouch->failing = true;\n"
FAILING_SINCE = "\t\t\ttouch->failing_since_ms = now;\n"
LOST = "\t\tif(now - touch->failing_since_ms >= TOUCH_LOST_MS) touch->down = false;"
GOOD = "\ttouch->failing = false;\n"
BEGIN = "\t\tif(!touch->down)\n\t\t{\n\t\t\ttouch->down = true;\n"
START_POINT = "\t\t\ttouch->start_x = x;\n\t\t\ttouch->start_y = y;\n"
START_TIME = "\t\t\ttouch->start_ms = now;\n"
MOVED = "\t\tif(distance(x, touch->start_x) > TOUCH_TAP_MOVE || distance(y, touch->start_y) > TOUCH_TAP_MOVE)"
MOVED_SET = "\t\t{\n\t\t\ttouch->moved = true;\n\t\t}\n"
LAST_POINT = "\t\ttouch->last_x = x;\n\t\ttouch->last_y = y;\n"
ROW_BROKEN = "\t\ttouch->lifted = 0;\n"
NO_TOUCH = "\tif(!touch->down) return TOUCH_NONE;\n"
COUNTED = "\ttouch->lifted++;\n"
LIFTED = "\tif(touch->lifted < TOUCH_LIFT_READINGS) return TOUCH_NONE;"
ENDED = "\ttouch->down = false;\n\tevent = judge(touch, now);"
POINT = "\tif(event == TOUCH_TAP)"
X_OUT = "\t\tif(x_out != NULL) *x_out = touch->start_x;"
Y_OUT = "\t\tif(y_out != NULL) *y_out = touch->start_y;"
IS_DOWN = "\treturn touch->down;"

MUTATIONS = [
    # the time of the module
    ("touch_time_steps_back_with_the_caller", T, F, ADVANCE, "\ttouch->clock_ms = now_ms;"),
    ("touch_time_does_not_advance", T, F, ADVANCE, ADVANCE.replace("touch->clock_ms)", "touch->clock_ms && false)")),
    ("touch_time_stands_after_the_first_reading", T, F, ADVANCE, "\tif(touch->clock_ms == 0) touch->clock_ms = now_ms;"),
    ("touch_sample_uses_time_of_caller", T, F, NOW, "\tuint64_t now = (advance(touch, now_ms), now_ms);"),
    ("touch_time_counted_in_32_bit", T, F, LASTED, "\tuint64_t lasted = (uint32_t)(now - touch->start_ms);"),
    ("touch_time_compared_in_32_bit", T, F, ADVANCE, "\tif((uint32_t)now_ms > (uint32_t)touch->clock_ms) touch->clock_ms = now_ms;"),

    # touch_init
    ("touch_init_does_nothing", T, F, INIT, "\t(void)touch;"),
    ("touch_init_keeps_touch", T, F, INIT, "\ttouch->clock_ms = 0;"),
    ("touch_init_keeps_time", T, F,
     INIT, "\tuint64_t clock_ms = touch->clock_ms;\n\n\tmemset(touch, 0, sizeof(*touch));\n\ttouch->clock_ms = clock_ms;"),

    # a failed reading says nothing
    ("touch_failed_reading_with_finger_trusted", T, F, FAILED, FAILED.replace("if(!read_ok)", "if(!read_ok && !(down && on_screen(x, y)))")),
    ("touch_failed_reading_without_finger_trusted", T, F, FAILED, FAILED.replace("if(!read_ok)", "if(!read_ok && down)")),
    ("touch_failed_reading_ends_touch", T, F, LOST, "\t\ttouch->down = false;"),
    ("touch_failed_reading_breaks_the_row", T, F, LOST, "\t\ttouch->lifted = 0;\n" + LOST),
    ("touch_failed_reading_counts_for_the_lift", T, F, LOST, LOST + "\n\t\tif(touch->down) touch->lifted++;"),
    ("touch_failed_reading_forgives_movement", T, F, LOST, "\t\ttouch->moved = false;\n" + LOST),
    ("touch_failed_reading_moves_last_point", T, F, LOST, "\t\ttouch->last_x = touch->start_x;\n" + LOST),
    ("touch_failed_reading_restarts_time", T, F, LOST, "\t\ttouch->start_ms = now;\n" + LOST),

    # a point outside the screen counts as a failed reading
    ("touch_x_below_0_accepted", T, F, ON_SCREEN, ON_SCREEN.replace("x >= 0 && ", "")),
    ("touch_x_of_minus_1_accepted", T, F, ON_SCREEN, ON_SCREEN.replace("x >= 0", "x >= -1")),
    ("touch_x_of_0_refused", T, F, ON_SCREEN, ON_SCREEN.replace("x >= 0", "x > 0")),
    ("touch_x_above_479_accepted", T, F, ON_SCREEN, ON_SCREEN.replace("x < TOUCH_SIZE && ", "")),
    ("touch_x_of_480_accepted", T, F, ON_SCREEN, ON_SCREEN.replace("x < TOUCH_SIZE", "x <= TOUCH_SIZE")),
    ("touch_x_of_479_refused", T, F, ON_SCREEN, ON_SCREEN.replace("x < TOUCH_SIZE", "x < TOUCH_SIZE - 1")),
    ("touch_y_below_0_accepted", T, F, ON_SCREEN, ON_SCREEN.replace(" && y >= 0", "")),
    ("touch_y_of_minus_1_accepted", T, F, ON_SCREEN, ON_SCREEN.replace("y >= 0", "y >= -1")),
    ("touch_y_of_0_refused", T, F, ON_SCREEN, ON_SCREEN.replace("y >= 0", "y > 0")),
    ("touch_y_above_479_accepted", T, F, ON_SCREEN, ON_SCREEN.replace(" && y < TOUCH_SIZE", "")),
    ("touch_y_of_480_accepted", T, F, ON_SCREEN, ON_SCREEN.replace("y < TOUCH_SIZE", "y <= TOUCH_SIZE")),
    ("touch_y_of_479_refused", T, F, ON_SCREEN, ON_SCREEN.replace("y < TOUCH_SIZE", "y < TOUCH_SIZE - 1")),
    ("touch_size_one_larger", T, H, "#define TOUCH_SIZE          480", "#define TOUCH_SIZE          481"),
    ("touch_size_one_smaller", T, H, "#define TOUCH_SIZE          480", "#define TOUCH_SIZE          479"),
    ("touch_point_outside_accepted", T, F, OUTSIDE, "\tif(down && !on_screen(x, y) && false) read_ok = false;"),
    ("touch_point_outside_counts_as_no_finger", T, F, OUTSIDE, "\tif(down && !on_screen(x, y)) down = false;"),
    ("touch_point_outside_skipped", T, F, OUTSIDE, "\tif(down && !on_screen(x, y)) return TOUCH_NONE;"),
    ("touch_point_checked_without_finger", T, F, OUTSIDE, "\tif(!on_screen(x, y)) read_ok = false;"),
    ("touch_point_outside_accepted_inside_a_touch", T, F, OUTSIDE, "\tif(down && !touch->down && !on_screen(x, y)) read_ok = false;"),
    ("touch_point_outside_starts_a_touch", T, F, OUTSIDE, "\tif(down && touch->down && !on_screen(x, y)) read_ok = false;"),

    # readings that fail for TOUCH_LOST_MS
    ("touch_lost_one_ms_late", T, F, LOST, LOST.replace(">= TOUCH_LOST_MS", "> TOUCH_LOST_MS")),
    ("touch_lost_one_ms_early", T, F, LOST, LOST.replace(">= TOUCH_LOST_MS", ">= TOUCH_LOST_MS - 1")),
    ("touch_lost_time_shorter", T, H, "#define TOUCH_LOST_MS       300u", "#define TOUCH_LOST_MS       299u"),
    ("touch_lost_time_longer", T, H, "#define TOUCH_LOST_MS       300u", "#define TOUCH_LOST_MS       301u"),
    ("touch_lost_never", T, F, LOST + "\n", ""),
    ("touch_lost_only_at_the_limit", T, F, LOST, LOST.replace(">= TOUCH_LOST_MS", "== TOUCH_LOST_MS")),
    ("touch_lost_time_counted_in_32_bit", T, F, LOST, LOST.replace("now - touch->failing_since_ms >=", "(uint32_t)(now - touch->failing_since_ms) >=")),
    ("touch_lost_counted_from_every_failed_reading", T, F, FAILED, FAILED.replace("if(!touch->failing)", "if(1)")),
    ("touch_lost_row_not_remembered", T, F, FAILING, ""),
    ("touch_lost_start_not_stored", T, F, FAILING_SINCE, ""),
    ("touch_lost_start_is_time_of_caller", T, F, FAILING_SINCE, "\t\t\ttouch->failing_since_ms = now_ms;\n"),
    ("touch_lost_counted_from_the_start_of_the_touch", T, F, LOST, LOST.replace("touch->failing_since_ms", "touch->start_ms")),
    ("touch_lost_good_reading_does_not_end_the_row", T, F, GOOD, ""),
    ("touch_lost_only_a_finger_ends_the_row", T, F, GOOD, "\tif(down) touch->failing = false;\n"),
    ("touch_lost_only_no_finger_ends_the_row", T, F, GOOD, "\tif(!down) touch->failing = false;\n"),
    ("touch_lost_touch_is_judged", T, F,
     LOST, "\t\tif(touch->down && now - touch->failing_since_ms >= TOUCH_LOST_MS)\n\t\t{\n\t\t\ttouch->down = false;\n\t\t\treturn judge(touch, now);\n\t\t}"),
    ("touch_lost_only_while_not_moved", T, F, LOST, LOST.replace("if(now", "if(!touch->moved && now")),
    ("touch_lost_not_behind_a_missing_reading", T, F, LOST, LOST.replace("if(now", "if(touch->lifted == 0 && now")),

    # a touch begins
    ("touch_start_not_marked", T, F, BEGIN, BEGIN.replace("\t\t\ttouch->down = true;\n", "")),
    ("touch_start_with_every_reading", T, F, BEGIN, BEGIN.replace("if(!touch->down)", "if(1)")),
    ("touch_start_keeps_movement", T, F, "\t\t\ttouch->moved = false;\n", ""),
    ("touch_start_x_not_stored", T, F, START_POINT, "\t\t\ttouch->start_y = y;\n"),
    ("touch_start_y_not_stored", T, F, START_POINT, "\t\t\ttouch->start_x = x;\n"),
    ("touch_start_x_and_y_swapped", T, F, START_POINT, "\t\t\ttouch->start_x = y;\n\t\t\ttouch->start_y = x;\n"),
    ("touch_start_x_of_0_not_stored", T, F, START_POINT, START_POINT.replace("\t\t\ttouch->start_x = x;", "\t\t\tif(x != 0) touch->start_x = x;")),
    ("touch_start_y_of_0_not_stored", T, F, START_POINT, START_POINT.replace("\t\t\ttouch->start_y = y;", "\t\t\tif(y != 0) touch->start_y = y;")),
    ("touch_start_time_not_stored", T, F, START_TIME, ""),
    ("touch_start_time_of_caller", T, F, START_TIME, "\t\t\ttouch->start_ms = now_ms;\n"),
    ("touch_start_time_follows_the_finger", T, F, LAST_POINT, "\t\ttouch->start_ms = now;\n" + LAST_POINT),
    ("touch_start_time_follows_the_moving_finger", T, F, MOVED_SET, MOVED_SET.replace("touch->moved = true;", "touch->moved = true;\n\t\t\ttouch->start_ms = now;")),

    # the tap range
    ("touch_tap_range_x_one_pixel_small", T, F, MOVED, MOVED.replace("start_x) > TOUCH_TAP_MOVE", "start_x) >= TOUCH_TAP_MOVE")),
    ("touch_tap_range_x_one_pixel_wide", T, F, MOVED, MOVED.replace("start_x) > TOUCH_TAP_MOVE", "start_x) > TOUCH_TAP_MOVE + 1")),
    ("touch_tap_range_y_one_pixel_small", T, F, MOVED, MOVED.replace("start_y) > TOUCH_TAP_MOVE", "start_y) >= TOUCH_TAP_MOVE")),
    ("touch_tap_range_y_one_pixel_wide", T, F, MOVED, MOVED.replace("start_y) > TOUCH_TAP_MOVE", "start_y) > TOUCH_TAP_MOVE + 1")),
    ("touch_tap_range_smaller", T, H, "#define TOUCH_TAP_MOVE      20", "#define TOUCH_TAP_MOVE      19"),
    ("touch_tap_range_wider", T, H, "#define TOUCH_TAP_MOVE      20", "#define TOUCH_TAP_MOVE      21"),
    ("touch_tap_range_x_not_checked", T, F, MOVED, "\t\tif(distance(y, touch->start_y) > TOUCH_TAP_MOVE)"),
    ("touch_tap_range_y_not_checked", T, F, MOVED, "\t\tif(distance(x, touch->start_x) > TOUCH_TAP_MOVE)"),
    ("touch_tap_range_both_axes_needed", T, F, MOVED, MOVED.replace("||", "&&")),
    ("touch_tap_range_sum_of_axes", T, F, MOVED, "\t\tif(distance(x, touch->start_x) + distance(y, touch->start_y) > TOUCH_TAP_MOVE)"),
    ("touch_movement_never_noticed", T, F, MOVED_SET, "\t\t{\n\t\t}\n"),
    ("touch_movement_forgotten_when_back", T, F, MOVED_SET, MOVED_SET + "\t\telse\n\t\t{\n\t\t\ttouch->moved = false;\n\t\t}\n"),
    ("touch_distance_only_to_larger_values", T, F, DISTANCE, "\treturn a - b;"),
    ("touch_distance_only_to_smaller_values", T, F, DISTANCE, "\treturn b - a;"),
    ("touch_last_x_not_stored", T, F, LAST_POINT, "\t\ttouch->last_y = y;\n"),
    ("touch_last_y_not_stored", T, F, LAST_POINT, "\t\ttouch->last_x = x;\n"),
    ("touch_last_x_and_y_swapped", T, F, LAST_POINT, "\t\ttouch->last_x = y;\n\t\ttouch->last_y = x;\n"),
    ("touch_last_x_only_stored_when_moved", T, F, LAST_POINT, LAST_POINT.replace("\t\ttouch->last_x = x;", "\t\tif(touch->moved) touch->last_x = x;")),
    ("touch_last_point_is_the_furthest", T, F,
     LAST_POINT, "\t\tif(distance(x, touch->start_x) >= distance(touch->last_x, touch->start_x)) touch->last_x = x;\n\t\ttouch->last_y = y;\n"),

    # the lift
    ("touch_single_missing_readings_add_up", T, F, ROW_BROKEN, ""),
    ("touch_missing_readings_add_up_while_moving", T, F, ROW_BROKEN, "\t\tif(!touch->moved) touch->lifted = 0;\n"),
    ("touch_missing_reading_forgives_movement", T, F, ROW_BROKEN, "\t\tif(touch->lifted > 0) touch->moved = false;\n" + ROW_BROKEN),
    ("touch_missing_reading_moves_start_x", T, F, ROW_BROKEN, "\t\tif(touch->lifted > 0) touch->start_x = x;\n" + ROW_BROKEN),
    ("touch_missing_reading_moves_start_y", T, F, ROW_BROKEN, "\t\tif(touch->lifted > 0) touch->start_y = y;\n" + ROW_BROKEN),
    ("touch_missing_reading_restarts_time", T, F, ROW_BROKEN, "\t\tif(touch->lifted > 0) touch->start_ms = now;\n" + ROW_BROKEN),
    ("touch_lift_without_touch", T, F, NO_TOUCH, ""),
    ("touch_lift_not_counted", T, F, COUNTED, ""),
    ("touch_lift_with_first_missing_reading", T, F, LIFTED, LIFTED.replace("< TOUCH_LIFT_READINGS", "< TOUCH_LIFT_READINGS - 1")),
    ("touch_lift_with_third_missing_reading", T, F, LIFTED, LIFTED.replace("< TOUCH_LIFT_READINGS", "<= TOUCH_LIFT_READINGS")),
    ("touch_lift_readings_fewer", T, H, "#define TOUCH_LIFT_READINGS 2", "#define TOUCH_LIFT_READINGS 1"),
    ("touch_lift_readings_more", T, H, "#define TOUCH_LIFT_READINGS 2", "#define TOUCH_LIFT_READINGS 3"),
    ("touch_lift_with_first_missing_reading_of_a_long_touch", T, F,
     LIFTED, LIFTED.replace("TOUCH_LIFT_READINGS)", "TOUCH_LIFT_READINGS && now - touch->start_ms <= TOUCH_SWIPE_MS)")),
    ("touch_lift_keeps_touch", T, F, ENDED, "\tevent = judge(touch, now);"),
    ("touch_lift_restarts_the_count_only", T, F, ENDED, "\ttouch->lifted = 0;\n\tevent = judge(touch, now);"),

    # tap
    ("touch_tap_one_ms_short", T, F, TAP, TAP.replace("<= TOUCH_TAP_MS", "< TOUCH_TAP_MS")),
    ("touch_tap_one_ms_long", T, F, TAP, TAP.replace("<= TOUCH_TAP_MS", "<= TOUCH_TAP_MS + 1")),
    ("touch_tap_time_shorter", T, H, "#define TOUCH_TAP_MS        600u", "#define TOUCH_TAP_MS        599u"),
    ("touch_tap_time_longer", T, H, "#define TOUCH_TAP_MS        600u", "#define TOUCH_TAP_MS        601u"),
    ("touch_tap_time_not_checked", T, F, TAP, "\tif(!touch->moved) return TOUCH_TAP;"),
    ("touch_tap_within_the_time_of_a_swipe", T, F, TAP, TAP.replace("TOUCH_TAP_MS", "TOUCH_SWIPE_MS")),
    ("touch_tap_movement_not_checked", T, F, TAP, "\tif(lasted <= TOUCH_TAP_MS) return TOUCH_TAP;"),
    ("touch_tap_judged_by_the_last_point", T, F, TAP, "\tif(far_x <= TOUCH_TAP_MOVE && far_y <= TOUCH_TAP_MOVE && lasted <= TOUCH_TAP_MS) return TOUCH_TAP;"),
    ("touch_tap_never", T, F, TAP, TAP.replace("if(!touch->moved", "if(false && !touch->moved")),
    ("touch_tap_needs_time", T, F, TAP, TAP.replace("lasted <= TOUCH_TAP_MS", "lasted > 0 && lasted <= TOUCH_TAP_MS")),
    ("touch_tap_of_a_quick_way_out_and_back", T, F,
     TAP, TAP.replace("if(!touch->moved", "if((!touch->moved || (far_x <= TOUCH_TAP_MOVE && far_y <= TOUCH_TAP_MOVE && lasted < TOUCH_LOST_MS / 3))")),
    ("touch_tap_of_a_very_long_touch", T, F, TAP, TAP.replace("lasted <= TOUCH_TAP_MS", "(lasted <= TOUCH_TAP_MS || lasted > 100000)")),

    # swipe
    ("touch_swipe_one_ms_short", T, F, LATE, LATE.replace("> TOUCH_SWIPE_MS", ">= TOUCH_SWIPE_MS")),
    ("touch_swipe_one_ms_long", T, F, LATE, LATE.replace("> TOUCH_SWIPE_MS", "> TOUCH_SWIPE_MS + 1")),
    ("touch_swipe_time_shorter", T, H, "#define TOUCH_SWIPE_MS      1000u", "#define TOUCH_SWIPE_MS      999u"),
    ("touch_swipe_time_longer", T, H, "#define TOUCH_SWIPE_MS      1000u", "#define TOUCH_SWIPE_MS      1001u"),
    ("touch_swipe_time_not_checked", T, F, LATE + "\n", ""),
    ("touch_swipe_within_the_time_of_a_tap", T, F, LATE, LATE.replace("TOUCH_SWIPE_MS", "TOUCH_TAP_MS")),
    ("touch_swipe_time_not_checked_for_long_ways_x", T, F, LATE, LATE.replace("TOUCH_SWIPE_MS)", "TOUCH_SWIPE_MS && far_x < 2 * TOUCH_SWIPE_MOVE)")),
    ("touch_swipe_time_not_checked_for_long_ways_y", T, F, LATE, LATE.replace("TOUCH_SWIPE_MS)", "TOUCH_SWIPE_MS && far_y < 2 * TOUCH_SWIPE_MOVE)")),
    ("touch_swipe_time_not_checked_beyond_an_hour", T, F, LATE, LATE.replace("TOUCH_SWIPE_MS)", "TOUCH_SWIPE_MS && lasted < 3600000)")),
    ("touch_swipe_x_needs_time", T, F, SWIPE_X, SWIPE_X.replace("if(far_x", "if(lasted > 0 && far_x")),
    ("touch_swipe_y_needs_time", T, F, SWIPE_Y, SWIPE_Y.replace("if(far_y", "if(lasted > 0 && far_y")),
    ("touch_swipe_x_way_from_start_y", T, F,
     "\tint far_x = distance(touch->last_x, touch->start_x);", "\tint far_x = distance(touch->last_x, touch->start_y);"),
    ("touch_swipe_y_way_from_start_x", T, F,
     "\tint far_y = distance(touch->last_y, touch->start_y);", "\tint far_y = distance(touch->last_y, touch->start_x);"),
    ("touch_swipe_x_one_pixel_long", T, F, SWIPE_X, SWIPE_X.replace("far_x >= TOUCH_SWIPE_MOVE", "far_x > TOUCH_SWIPE_MOVE")),
    ("touch_swipe_x_one_pixel_short", T, F, SWIPE_X, SWIPE_X.replace("far_x >= TOUCH_SWIPE_MOVE", "far_x >= TOUCH_SWIPE_MOVE - 1")),
    ("touch_swipe_y_one_pixel_long", T, F, SWIPE_Y, SWIPE_Y.replace("far_y >= TOUCH_SWIPE_MOVE", "far_y > TOUCH_SWIPE_MOVE")),
    ("touch_swipe_y_one_pixel_short", T, F, SWIPE_Y, SWIPE_Y.replace("far_y >= TOUCH_SWIPE_MOVE", "far_y >= TOUCH_SWIPE_MOVE - 1")),
    ("touch_swipe_way_shorter", T, H, "#define TOUCH_SWIPE_MOVE    60", "#define TOUCH_SWIPE_MOVE    59"),
    ("touch_swipe_way_longer", T, H, "#define TOUCH_SWIPE_MOVE    60", "#define TOUCH_SWIPE_MOVE    61"),
    ("touch_swipe_x_way_not_checked", T, F, SWIPE_X, "\tif(far_x >= 2 * far_y)"),
    ("touch_swipe_y_way_not_checked", T, F, SWIPE_Y, "\tif(far_y >= 2 * far_x)"),
    ("touch_swipe_x_way_of_the_tap_range", T, F, SWIPE_X, SWIPE_X.replace("TOUCH_SWIPE_MOVE", "TOUCH_TAP_MOVE")),
    ("touch_swipe_y_way_of_the_tap_range", T, F, SWIPE_Y, SWIPE_Y.replace("TOUCH_SWIPE_MOVE", "TOUCH_TAP_MOVE")),
    ("touch_swipe_x_other_axis_not_checked", T, F, SWIPE_X, "\tif(far_x >= TOUCH_SWIPE_MOVE)"),
    ("touch_swipe_y_other_axis_not_checked", T, F, SWIPE_Y, "\tif(far_y >= TOUCH_SWIPE_MOVE)"),
    ("touch_swipe_x_more_than_twice", T, F, SWIPE_X, SWIPE_X.replace("far_x >= 2 * far_y", "far_x > 2 * far_y")),
    ("touch_swipe_x_just_below_twice", T, F, SWIPE_X, SWIPE_X.replace("far_x >= 2 * far_y", "far_x + 1 >= 2 * far_y")),
    ("touch_swipe_x_as_far_as_the_other_axis", T, F, SWIPE_X, SWIPE_X.replace("far_x >= 2 * far_y", "far_x >= far_y")),
    ("touch_swipe_y_more_than_twice", T, F, SWIPE_Y, SWIPE_Y.replace("far_y >= 2 * far_x", "far_y > 2 * far_x")),
    ("touch_swipe_y_just_below_twice", T, F, SWIPE_Y, SWIPE_Y.replace("far_y >= 2 * far_x", "far_y + 1 >= 2 * far_x")),
    ("touch_swipe_y_as_far_as_the_other_axis", T, F, SWIPE_Y, SWIPE_Y.replace("far_y >= 2 * far_x", "far_y >= far_x")),
    ("touch_swipe_x_more_than_twice_at_a_long_way", T, F, SWIPE_X, SWIPE_X.replace("far_x >= 2 * far_y", "far_x >= 2 * far_y + (far_x > 3 * TOUCH_SWIPE_MOVE)")),
    ("touch_swipe_y_more_than_twice_at_a_long_way", T, F, SWIPE_Y, SWIPE_Y.replace("far_y >= 2 * far_x", "far_y >= 2 * far_x + (far_y > 3 * TOUCH_SWIPE_MOVE)")),
    ("touch_swipe_never_horizontal", T, F, SWIPE_X, SWIPE_X.replace("if(far_x", "if(false && far_x")),
    ("touch_swipe_never_vertical", T, F, SWIPE_Y, SWIPE_Y.replace("if(far_y", "if(false && far_y")),
    ("touch_swipe_left_and_right_swapped", T, F, DIRECTION_X, DIRECTION_X.replace("last_x < touch->start_x", "last_x > touch->start_x")),
    ("touch_swipe_up_and_down_swapped", T, F, DIRECTION_Y, DIRECTION_Y.replace("last_y < touch->start_y", "last_y > touch->start_y")),
    ("touch_swipe_left_judged_from_start_y", T, F, DIRECTION_X, DIRECTION_X.replace("touch->start_x", "touch->start_y")),
    ("touch_swipe_up_judged_from_start_x", T, F, DIRECTION_Y, DIRECTION_Y.replace("touch->start_y", "touch->start_x")),
    ("touch_swipe_always_right", T, F, DIRECTION_X, "\t\treturn TOUCH_SWIPE_RIGHT;"),
    ("touch_swipe_always_left", T, F, DIRECTION_X, "\t\treturn TOUCH_SWIPE_LEFT;"),
    ("touch_swipe_always_down", T, F, DIRECTION_Y, "\t\treturn TOUCH_SWIPE_DOWN;"),
    ("touch_swipe_always_up", T, F, DIRECTION_Y, "\t\treturn TOUCH_SWIPE_UP;"),
    ("touch_swipe_horizontal_reported_as_vertical", T, F,
     DIRECTION_X, DIRECTION_X.replace("TOUCH_SWIPE_LEFT : TOUCH_SWIPE_RIGHT", "TOUCH_SWIPE_UP : TOUCH_SWIPE_DOWN")),
    ("touch_swipe_vertical_reported_as_horizontal", T, F,
     DIRECTION_Y, DIRECTION_Y.replace("TOUCH_SWIPE_UP : TOUCH_SWIPE_DOWN", "TOUCH_SWIPE_LEFT : TOUCH_SWIPE_RIGHT")),
    ("touch_smear_is_a_tap", T, F, SMEAR, SMEAR.replace("TOUCH_NONE", "TOUCH_TAP")),

    # the tap point
    ("touch_tap_point_x_is_last_point", T, F, X_OUT, X_OUT.replace("touch->start_x", "touch->last_x")),
    ("touch_tap_point_y_is_last_point", T, F, Y_OUT, Y_OUT.replace("touch->start_y", "touch->last_y")),
    ("touch_tap_point_x_and_y_swapped", T, F,
     X_OUT + "\n" + Y_OUT, "\t\tif(x_out != NULL) *x_out = touch->start_y;\n\t\tif(y_out != NULL) *y_out = touch->start_x;"),
    ("touch_tap_point_x_not_written", T, F, X_OUT, X_OUT.replace("x_out != NULL", "x_out != NULL && false")),
    ("touch_tap_point_y_not_written", T, F, Y_OUT, Y_OUT.replace("y_out != NULL", "y_out != NULL && false")),
    ("touch_tap_point_x_null_not_checked", T, F, X_OUT, "\t\t*x_out = touch->start_x;"),
    ("touch_tap_point_y_null_not_checked", T, F, Y_OUT, "\t\t*y_out = touch->start_y;"),
    ("touch_tap_point_x_needs_place_for_y", T, F, X_OUT, X_OUT.replace("x_out != NULL", "x_out != NULL && y_out != NULL")),
    ("touch_tap_point_y_needs_place_for_x", T, F, Y_OUT, Y_OUT.replace("y_out != NULL", "y_out != NULL && x_out != NULL")),
    ("touch_point_written_for_swipes", T, F, POINT, "\tif(event != TOUCH_NONE)"),
    ("touch_point_written_with_every_lift", T, F, POINT, "\tif(1)"),

    # touch_is_down
    ("touch_is_down_always_false", T, F, IS_DOWN, "\treturn touch->down && false;"),
    ("touch_is_down_always_true", T, F, IS_DOWN, "\treturn touch->down || true;"),
    ("touch_is_down_false_at_a_missing_reading", T, F, IS_DOWN, "\treturn touch->down && touch->lifted == 0;"),
    ("touch_is_down_false_while_failing", T, F, IS_DOWN, "\treturn touch->down && !touch->failing;"),
    ("touch_is_down_false_after_a_minute", T, F, IS_DOWN, "\treturn touch->down && touch->clock_ms - touch->start_ms <= 60000;"),
    ("touch_is_down_false_at_the_time_0", T, F, IS_DOWN, "\treturn touch->down && touch->clock_ms > 0;"),

    # the values of the gestures
    ("touch_event_none_and_tap_swapped", T, H, "\tTOUCH_NONE,\n\tTOUCH_TAP,\n", "\tTOUCH_TAP,\n\tTOUCH_NONE,\n"),
    ("touch_event_left_and_right_swapped", T, H, "\tTOUCH_SWIPE_LEFT,\n\tTOUCH_SWIPE_RIGHT,\n", "\tTOUCH_SWIPE_RIGHT,\n\tTOUCH_SWIPE_LEFT,\n"),
    ("touch_event_up_and_down_swapped", T, H, "\tTOUCH_SWIPE_UP,\n\tTOUCH_SWIPE_DOWN,\n", "\tTOUCH_SWIPE_DOWN,\n\tTOUCH_SWIPE_UP,\n"),
]
