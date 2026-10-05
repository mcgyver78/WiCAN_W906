"""Mutations of display/components/core/nav.c, see ../redproof.py."""

F = "components/core/nav.c"
H = "components/core/nav.h"
T = "test_nav"

CLOCK = "\tif(now_ms > nav->clock_ms) nav->clock_ms = now_ms;"
NOTE = "\tadvance(nav, now_ms);\n\tnav->last_input_ms = nav->clock_ms;\n"
ENTER = "\tnav->screen = screen;\n\tnav->row = row;\n"
LINES_LIMIT = "\treturn lines > LINES_MAX ? LINES_MAX : lines;"
TOO_DARK = "\tif(value < SETTINGS_BRIGHTNESS_MIN) return SETTINGS_BRIGHTNESS_MIN;\n"
TOO_BRIGHT = "\tif(value > SETTINGS_BRIGHTNESS_MAX) return SETTINGS_BRIGHTNESS_MAX;\n"
UNDER_WAY = "\treturn flow == DTC_FLOW_READ_SENT || flow == DTC_FLOW_READING || flow == DTC_FLOW_CLEAR_SENT || flow == DTC_FLOW_CLEARING;"
OUTCOME_LIST = "\t\tcase DTC_FLOW_LIST:     return NAV_DTC_LIST;\n"
OUTCOME_CLEARED = "\t\tcase DTC_FLOW_CLEARED:  return NAV_DTC_CLEARED;\n"
OUTCOME_FAILED = "\t\tcase DTC_FLOW_FAILED:\n\t\tcase DTC_FLOW_UNKNOWN:  return NAV_DTC_FAILED;\n"
NEAR_LOOP = "\tfor(int distance = 1; distance <= LAYOUT_PAGES_MAX; distance++)"
NEAR_BEHIND = "\t\tif(layout_page_shown(world->layout, nav->page + distance, world->catalog)) return nav->page + distance;\n"
NEAR_BEFORE = "\t\tif(layout_page_shown(world->layout, nav->page - distance, world->catalog)) return nav->page - distance;\n"
TURN_DIRECTION = "\tint direction = detents < 0 ? -1 : 1;"
TURN_LOST = "\tif(!page_shown(nav, world))\n\t{\n\t\tnav->page = nearest_page(nav, world);\n\t\treturn;\n\t}\n\tfor(; detents"
TURN_LOOP = "\tfor(; detents != 0; detents -= direction)"
TURN_END = "\t\tif(next == nav->page) break;\n"
FOCUS = "\tint64_t row = (int64_t)nav->row + detents;"
FOCUS_LAST = "\tint last = nav_rows(nav, world) - 1;\n\n\tif(row > last) row = last;"
FOCUS_TOP = "\tif(row > last) row = last;\n"
FOCUS_BOTTOM = "\tif(row < 0) row = 0;\n"
READ = "\tif(!world->can_read) return NAV_DO_NOTHING;\n\n\tenter(nav, NAV_DTC_BUSY, 0);\n\treturn NAV_DO_READ;"
CLOSE_DIALOG = "\tenter(nav, NAV_DTC_LIST, lines_of(world->list_lines) + LIST_CLEAR);"
ASK = "\tenter(nav, NAV_CONFIRM, CHOICE_CANCEL);\n\tnav->confirm = action;\n"
ASK_FREE = "\tif(under_way(world->flow)) return;\n\n\tenter(nav, NAV_CONFIRM"

BACK_PAGES = "\t\t\tnav->page = layout_first_page(world->layout, world->catalog);\n\t\t\tbreak;"
BACK_MENU = "\t\tcase NAV_MENU:\n\t\tcase NAV_DTC_BUSY:\n"
BACK_TO_PAGES = "\t\t\t// A request that is under way goes on, the ring shows its progress\n\t\t\tenter(nav, NAV_PAGES, 0);\n\t\t\tbreak;"
BACK_DTC = "\t\tcase NAV_DTC:\n\t\t\tenter(nav, NAV_MENU, MENU_DTC);\n\t\t\tbreak;"
BACK_OUTCOME = "\t\t\t// Without a dismiss: the outcome stays to be looked at again\n\t\t\tenter(nav, NAV_DTC, 0);\n\t\t\tbreak;"
BACK_DIALOG = "\t\t\t// Keeping the knob pressed is how clearing is confirmed here, and that is a long press as well\n\t\t\tbreak;"
BACK_BRIGHTNESS = "\t\t\tenter(nav, NAV_MENU, MENU_BRIGHTNESS);\n\t\t\treturn NAV_DO_SETTINGS_STORE;"
BACK_WEB = "\t\t\tenter(nav, NAV_MENU, MENU_WEB);"
BACK_INFO = "\t\t\tenter(nav, NAV_MENU, MENU_INFO);"
BACK_SETTINGS = "\t\t\tenter(nav, NAV_MENU, MENU_SETTINGS);"
BACK_ASK = ("\t\t\tenter(nav, NAV_SETTINGS, nav->confirm == NAV_DO_REBOOT ? SETTINGS_REBOOT :\n"
            "\t\t\t      nav->confirm == NAV_DO_PREVIOUS_FIRMWARE ? SETTINGS_PREVIOUS : SETTINGS_RESET);")

MENU_ROWS = "\tMENU_DTC, MENU_BRIGHTNESS, MENU_NIGHT, MENU_WEB, MENU_INFO, MENU_SETTINGS, MENU_BACK,"
DTC_ROWS = "\tDTC_READ, DTC_VIEW, DTC_OLD, DTC_BACK,"
SETTINGS_ROWS = "\tSETTINGS_REVERSE, SETTINGS_AP, SETTINGS_REBOOT, SETTINGS_PREVIOUS, SETTINGS_RESET, SETTINGS_BACK,"
CHOICE = "\tCHOICE_CANCEL, CHOICE_ACT, CHOICE_ROWS,"
LIST_ROWS = "\tLIST_READ, LIST_CLEAR, LIST_BACK, LIST_ROWS,"

MENU_FAULTS = "\t\t\tenter(nav, under_way(world->flow) ? NAV_DTC_BUSY : NAV_DTC, 0);"
MENU_LEVEL = "\t\t\tenter(nav, NAV_BRIGHTNESS, 0);\n\t\t\tnav->value = brightness_of(world->brightness);\n"
DTC_VIEW = "\t\t\tif(outcome != NAV_DTC) enter(nav, outcome, 0);"
DTC_OLD = "\t\t\tif(world->old_lines > 0) enter(nav, NAV_DTC_OLD, 0);"
LIST_ROW = "\tswitch(nav->row - lines_of(world->list_lines))"
LIST_CLEAR = "\t\t\tif(!world->can_clear) break;\n\t\t\tenter(nav, NAV_DTC_CONFIRM, CHOICE_CANCEL);\n\t\t\treturn NAV_DO_HOLD_OPEN;"
# The own access point that stays on whatever is asked (link.h) is not switched: the row does nothing then
SET_AP = "\t\t\tif(!world->ap_kept) return NAV_DO_AP_TOGGLE;\n\t\t\tbreak;"
AP_KEPT_ROW = "nav->screen == NAV_SETTINGS && nav->row == SETTINGS_AP && world->ap_kept"
SET_REVERSE = "\t\t\treturn NAV_DO_REVERSE_TOGGLE;"
SET_REBOOT = "\t\t\task(nav, NAV_DO_REBOOT, world);\n\t\t\tbreak;"
SET_PREVIOUS = "\t\t\tif(world->previous_firmware) ask(nav, NAV_DO_PREVIOUS_FIRMWARE, world);"
SET_RESET = "\t\t\task(nav, NAV_DO_FACTORY_RESET, world);\n\t\t\tbreak;"
ACTS_ROW = "\tif(row < 0 || row >= nav_rows(nav, world)) return false;\n"
ACTS_TRY = "\ttried.row = row;\n"
ACTS = "\treturn press(&tried, world) != NAV_DO_NOTHING || tried.screen != nav->screen;"

PRESS_PAGES = "\t\t\tenter(nav, NAV_MENU, MENU_DTC);\n\t\t\tbreak;\n\t\tcase NAV_MENU:\n\t\t\treturn press_menu(nav, world);"
PRESS_DIALOG = "\t\t\tif(nav->row != CHOICE_CANCEL) break;\n\t\t\tclose_dialog(nav, world);\n\t\t\treturn NAV_DO_HOLD_CLOSE;"
PRESS_CLEARED = "\t\t\tif(nav->row != lines_of(world->cleared_lines)) break;\n\t\t\tenter(nav, NAV_DTC, 0);\n\t\t\treturn NAV_DO_DISMISS;"
PRESS_FAILED = "\t\tcase NAV_DTC_FAILED:\n\t\t\tenter(nav, NAV_DTC, 0);\n\t\t\treturn NAV_DO_DISMISS;"
PRESS_OLD = "\t\t\tif(nav->row == lines_of(world->old_lines)) return back(nav, world);"
PRESS_PLAIN = "\t\tcase NAV_BRIGHTNESS:\n\t\tcase NAV_INFO:\n\t\t\treturn back(nav, world);"
PRESS_WEB = ("\t\t\tif(nav->row != WEB_RELEASE) return back(nav, world);\n"
             "\t\t\treturn world->release_open ? NAV_DO_RELEASE_OFF : NAV_DO_RELEASE_ON;")
PRESS_ASK = "\t\t\tif(nav->row == CHOICE_CANCEL) return back(nav, world);\n\t\t\tenter(nav, NAV_PAGES, 0);\n\t\t\treturn asked;"
OVER_ASK = "\tif(overlay == NAV_OVER_ASK) return NAV_DO_ASK_CONFIRM;\n"
OVER_UPDATE = "\tif(overlay == NAV_OVER_UPDATE) return NAV_DO_UPDATE_OK;\n"

INIT_PAGE = "\tnav->page = layout_first_page(world->layout, world->catalog);\n\tnav->row = 0;\n"
IS_UPLOAD = "\tif(world->uploading) return NAV_OVER_UPLOAD;\n"
IS_ASK = "\tif(world->asking != ACCESS_ASK_NONE) return NAV_OVER_ASK;\n"
IS_UPDATE = "\tif(world->update_pending) return NAV_OVER_UPDATE;\n"

ROWS_LIST = "\t\tcase NAV_DTC_LIST:      return lines_of(world->list_lines) + LIST_ROWS;"
ROWS_CLEARED = "\t\tcase NAV_DTC_CLEARED:   return lines_of(world->cleared_lines) + END_ROWS;"
ROWS_OLD = "\t\tcase NAV_DTC_OLD:       return lines_of(world->old_lines) + END_ROWS;"
ROWS_INFO = "\t\tcase NAV_INFO:          return lines_of(world->info_lines);"
ROWS_CHOICE = "\t\tcase NAV_DTC_CONFIRM:\n\t\tcase NAV_CONFIRM:       return CHOICE_ROWS;"

TURN_HEAD = "\tnote_input(nav, now_ms);\n\tif(nav_overlay(world) != NAV_OVER_NONE) return NAV_DO_NOTHING;\n\n\tif(nav->screen == NAV_PAGES)"
TURN_PAGES = "\tif(nav->screen == NAV_PAGES)\n\t{\n\t\tturn_pages(nav, detents, world);\n\t\treturn NAV_DO_NOTHING;\n\t}\n"
TURN_LEVEL = "\t\tnav->value = brightness_of(nav->value + (int64_t)detents * NAV_BRIGHTNESS_STEP);\n\t\treturn NAV_DO_BRIGHTNESS;"
SHORT = "\tnote_input(nav, now_ms);\n\tif(overlay != NAV_OVER_NONE) return press_overlay(overlay);\n\treturn press(nav, world);"
LONG = ("\tnote_input(nav, now_ms);\n\tif(overlay == NAV_OVER_ASK) return NAV_DO_ASK_REFUSE;\n"
        "\tif(overlay != NAV_OVER_NONE) return NAV_DO_NOTHING;\n\treturn back(nav, world);")
TAP_OVER = "\tif(nav_overlay(world) != NAV_OVER_NONE) return NAV_DO_NOTHING;\n\n\t// The failure"
TAP_HEAD = ("\tnote_input(nav, now_ms);\n"
            "\t// What lies over the screen is answered with the knob alone, the questions as well: a touch happens too\n"
            "\t// easily for what they ask, and none of them has a row a finger could mean\n" + TAP_OVER)
TAP_ASKED = "nav_overlay(world) == NAV_OVER_ASK"
TAP_ON_A_ROW = "row >= 0 && row < nav_rows(nav, world)"
TAP_FAILED = "\tif(nav->screen != NAV_DTC_FAILED)\n\t{"
TAP_ROW = "\t\tif(row < 0 || row >= nav_rows(nav, world)) return NAV_DO_NOTHING;\n"
TAP_FOCUS = "\t\tnav->row = row;\n\t}\n\treturn press(nav, world);"
TAP_BODY = TAP_HEAD + " has no rows, and a touch anywhere acknowledges it\n" + TAP_FAILED + "\n\t\t// A screen without rows has no row that exists\n" + TAP_ROW
TAP_DIALOG = "\t\tif((nav->screen == NAV_DTC_CONFIRM || nav->screen == NAV_CONFIRM) && row != CHOICE_CANCEL) return NAV_DO_NOTHING;\n"
TAP_KNOB_ALONE = ("\t\t// What the two dialogs ask for is confirmed with the knob alone, and the knob alone moves their focus:\n"
                  "\t\t// a touch happens too easily\n" + TAP_DIALOG)
SWIPE = ("\tnote_input(nav, now_ms);\n"
         "\tif(nav->screen != NAV_PAGES || direction == 0 || nav_overlay(world) != NAV_OVER_NONE) return NAV_DO_NOTHING;\n\n"
         "\tturn_pages(nav, direction < 0 ? -1 : 1, world);")
HOLD_HEAD = "\tadvance(nav, now_ms);\n\tif(nav->screen != NAV_DTC_CONFIRM) return NAV_DO_NOTHING;\n"
HOLD_CLEAR = "\tif(event == HOLD_CONFIRMED && nav_overlay(world) == NAV_OVER_NONE)\n\t{\n\t\tenter(nav, NAV_DTC_BUSY, 0);\n\t\treturn NAV_DO_CLEAR;\n\t}"
HOLD_ENDS = "\tif(event == HOLD_CONFIRMED || event == HOLD_CANCELLED || event == HOLD_STUCK) close_dialog(nav, world);"
CANCEL_TIME = "\t// No input: nobody is at a screen that cannot be seen\n\tadvance(nav, now_ms);\n"
CANCEL_CLEAR = "\tif(nav->screen == NAV_DTC_CONFIRM)\n\t{\n\t\tclose_dialog(nav, world);\n\t\treturn NAV_DO_HOLD_CLOSE;\n\t}\n"
CANCEL_ASK = "\tif(nav->screen == NAV_CONFIRM) return back(nav, world);\n"

TICK_TIME = "\tadvance(nav, now_ms);\n\n\tif(screen == NAV_DTC_CONFIRM"
TICK_DIALOG_IF = "\tif(screen == NAV_DTC_CONFIRM && (world->flow != DTC_FLOW_LIST || !world->can_clear))"
TICK_DIALOG_TO = "\t\tif(world->flow == DTC_FLOW_LIST) close_dialog(nav, world);\n\t\telse enter(nav, NAV_DTC, 0);\n\t\treturn NAV_DO_HOLD_CLOSE;"
TICK_DIALOG = TICK_DIALOG_IF + "\n\t{\n" + TICK_DIALOG_TO + "\n\t}\n"
TICK_BUSY_IF = "\tif(screen == NAV_DTC_BUSY && !under_way(world->flow))"
TICK_BUSY = TICK_BUSY_IF + "\n\t{\n\t\tenter(nav, outcome, 0);\n\t\treturn NAV_DO_NOTHING;\n\t}\n"
TICK_GONE_IF = ("\tif(((screen == NAV_DTC_LIST || screen == NAV_DTC_CLEARED || screen == NAV_DTC_FAILED) && screen != outcome) ||\n"
                "\t   (screen == NAV_DTC_OLD && world->old_lines <= 0))")
TICK_GONE = TICK_GONE_IF + "\n\t{\n\t\tenter(nav, NAV_DTC, 0);\n\t\treturn NAV_DO_NOTHING;\n\t}\n"
TICK_PAGE_IF = "\tif(screen == NAV_PAGES && !page_shown(nav, world))"
TICK_PAGE = TICK_PAGE_IF + "\n\t{\n\t\tnav->page = nearest_page(nav, world);\n\t\treturn NAV_DO_NOTHING;\n\t}\n"
TICK_FOCUS_FLOOR = "\tif(last < 0) last = 0;\n"
TICK_FOCUS_IF = "\tif(nav->row > last)"
TICK_FOCUS = TICK_FOCUS_IF + "\n\t{\n\t\tnav->row = last;\n\t\treturn NAV_DO_NOTHING;\n\t}\n"
TICK_IDLE = "\tif(nav->clock_ms - nav->last_input_ms < NAV_IDLE_MS || nav_overlay(world) != NAV_OVER_NONE) return NAV_DO_NOTHING;"
TICK_OWN = "\tif(screen == NAV_DTC_BUSY || screen == NAV_DTC_CONFIRM) return NAV_DO_NOTHING;\n"
TICK_RETURN = "\tenter(nav, NAV_PAGES, 0);\n\treturn screen == NAV_BRIGHTNESS ? NAV_DO_SETTINGS_STORE : NAV_DO_NOTHING;"
IDLE_OVER = "nav->clock_ms - nav->last_input_ms >= NAV_IDLE_MS"

MUTATIONS = [
    # the time of the module
    ("nav_time_steps_back_with_the_caller", T, F, CLOCK, "\tnav->clock_ms = now_ms;"),
    ("nav_time_does_not_advance", T, F, CLOCK, "\t(void)nav;\n\t(void)now_ms;"),
    ("nav_input_at_the_time_of_the_caller", T, F, NOTE, "\tadvance(nav, now_ms);\n\tnav->last_input_ms = now_ms;\n"),
    ("nav_input_does_not_restart_idle_time", T, F, NOTE, "\tadvance(nav, now_ms);\n"),
    ("nav_input_does_not_move_the_time", T, F, NOTE, "\t(void)now_ms;\n\tnav->last_input_ms = nav->clock_ms;\n"),
    ("nav_hold_does_not_move_the_time", T, F, HOLD_HEAD, "\t(void)now_ms;\n\tif(nav->screen != NAV_DTC_CONFIRM) return NAV_DO_NOTHING;\n"),
    ("nav_hold_moves_the_time_only_in_the_dialog", T, F,
     HOLD_HEAD, "\tif(nav->screen != NAV_DTC_CONFIRM) return NAV_DO_NOTHING;\n\tadvance(nav, now_ms);\n"),
    ("nav_hold_counts_as_input", T, F, HOLD_HEAD, "\tnote_input(nav, now_ms);\n\tif(nav->screen != NAV_DTC_CONFIRM) return NAV_DO_NOTHING;\n"),
    ("nav_tick_does_not_move_the_time", T, F, TICK_TIME, "\t(void)now_ms;\n\n\tif(screen == NAV_DTC_CONFIRM"),
    ("nav_tick_counts_as_input", T, F, TICK_TIME, "\tnote_input(nav, now_ms);\n\n\tif(screen == NAV_DTC_CONFIRM"),
    ("nav_turn_is_no_input", T, F, TURN_HEAD, TURN_HEAD.replace("note_input", "advance")),
    ("nav_turn_under_overlay_is_no_input", T, F,
     TURN_HEAD, "\tadvance(nav, now_ms);\n\tif(nav_overlay(world) != NAV_OVER_NONE) return NAV_DO_NOTHING;\n\n\tnote_input(nav, now_ms);\n\tif(nav->screen == NAV_PAGES)"),
    ("nav_short_is_no_input", T, F, SHORT, SHORT.replace("note_input", "advance")),
    ("nav_long_is_no_input", T, F, LONG, LONG.replace("note_input", "advance")),
    ("nav_tap_is_no_input", T, F, TAP_HEAD, TAP_HEAD.replace("note_input", "advance")),
    ("nav_tap_under_an_overlay_is_no_input", T, F,
     TAP_HEAD, TAP_HEAD.replace("note_input", "advance").replace("return NAV_DO_NOTHING;\n\n", "return NAV_DO_NOTHING;\n\n\tnote_input(nav, now_ms);\n")),
    ("nav_ignored_tap_is_no_input", T, F,
     TAP_BODY, "\tuint64_t last_input_ms = nav->last_input_ms;\n\n" +
     TAP_BODY.replace(TAP_ROW, "\t\tif(row < 0 || row >= nav_rows(nav, world))\n\t\t{\n\t\t\tnav->last_input_ms = last_input_ms;\n\t\t\treturn NAV_DO_NOTHING;\n\t\t}\n")),
    ("nav_swipe_is_no_input", T, F, SWIPE, SWIPE.replace("note_input", "advance")),
    ("nav_ignored_swipe_is_no_input", T, F,
     SWIPE, SWIPE.replace("note_input", "advance").replace("return NAV_DO_NOTHING;\n\n", "return NAV_DO_NOTHING;\n\n\tnote_input(nav, now_ms);\n")),

    # nav_init
    ("nav_init_keeps_screen", T, F, "\tnav->screen = NAV_PAGES;\n\tnav->page", "\tnav->page"),
    ("nav_init_first_page_of_the_layout", T, F, INIT_PAGE, "\tnav->page = world->layout->page_count > 0 ? 0 : -1;\n\tnav->row = 0;\n"),
    ("nav_init_keeps_page", T, F, INIT_PAGE, "\tif(!page_shown(nav, world)) nav->page = layout_first_page(world->layout, world->catalog);\n\tnav->row = 0;\n"),
    ("nav_init_keeps_row", T, F, "\tnav->row = 0;\n\tnav->value = 0;\n", "\tnav->value = 0;\n"),
    ("nav_init_keeps_value", T, F, "\tnav->row = 0;\n\tnav->value = 0;\n", "\tnav->row = 0;\n"),
    ("nav_init_keeps_what_waits", T, F, "\tnav->confirm = NAV_DO_NOTHING;\n\tnav->last_input_ms = now_ms;\n", "\tnav->last_input_ms = now_ms;\n"),
    ("nav_init_keeps_last_input", T, F, "\tnav->last_input_ms = now_ms;\n\tnav->clock_ms = now_ms;\n", "\tnav->clock_ms = now_ms;\n"),
    ("nav_init_keeps_time", T, F, "\tnav->last_input_ms = now_ms;\n\tnav->clock_ms = now_ms;\n", "\tnav->last_input_ms = now_ms;\n"),
    ("nav_init_time_never_backwards", T, F,
     "\tnav->last_input_ms = now_ms;\n\tnav->clock_ms = now_ms;\n", "\tnav->last_input_ms = now_ms;\n\tif(nav->clock_ms < now_ms) nav->clock_ms = now_ms;\n"),

    # nav_overlay
    ("nav_overlay_upload_missing", T, F, IS_UPLOAD, ""),
    ("nav_overlay_question_missing", T, F, IS_ASK, ""),
    ("nav_overlay_update_missing", T, F, IS_UPDATE, ""),
    ("nav_overlay_question_before_upload", T, F, IS_UPLOAD + IS_ASK, IS_ASK + IS_UPLOAD),
    ("nav_overlay_update_before_question", T, F, IS_ASK + IS_UPDATE, IS_UPDATE + IS_ASK),
    ("nav_overlay_update_before_upload", T, F, IS_UPLOAD + IS_ASK + IS_UPDATE, IS_ASK + IS_UPDATE + IS_UPLOAD),
    ("nav_overlay_only_questions_of_the_enum", T, F, IS_ASK, IS_ASK.replace("world->asking != ACCESS_ASK_NONE", "world->asking != ACCESS_ASK_NONE && world->asking <= ACCESS_ASK_RESET")),
    ("nav_overlay_wifi_is_no_question", T, F, IS_ASK, IS_ASK.replace("world->asking != ACCESS_ASK_NONE", "world->asking != ACCESS_ASK_NONE && world->asking != ACCESS_ASK_WIFI")),
    ("nav_overlay_firmware_is_no_question", T, F, IS_ASK, IS_ASK.replace("world->asking != ACCESS_ASK_NONE", "world->asking != ACCESS_ASK_NONE && world->asking != ACCESS_ASK_FIRMWARE")),
    ("nav_overlay_reset_is_no_question", T, F, IS_ASK, IS_ASK.replace("world->asking != ACCESS_ASK_NONE", "world->asking != ACCESS_ASK_NONE && world->asking != ACCESS_ASK_RESET")),

    # nav_rows and the lines of the world
    ("nav_rows_menu_one_less", T, F, "return NAV_MENU_ROWS;", "return NAV_MENU_ROWS - 1;"),
    ("nav_rows_menu_changed", T, H, "#define NAV_MENU_ROWS           7", "#define NAV_MENU_ROWS           8"),
    ("nav_rows_dtc_one_more", T, F, "return NAV_DTC_ROWS;", "return NAV_DTC_ROWS + 1;"),
    ("nav_rows_dtc_changed", T, H, "#define NAV_DTC_ROWS            4", "#define NAV_DTC_ROWS            3"),
    ("nav_rows_web_one_more", T, F, "return NAV_WEB_ROWS;", "return NAV_WEB_ROWS + 1;"),
    ("nav_rows_web_changed", T, H, "#define NAV_WEB_ROWS            2", "#define NAV_WEB_ROWS            1"),
    ("nav_rows_settings_one_less", T, F, "return NAV_SETTINGS_ROWS;", "return NAV_SETTINGS_ROWS - 1;"),
    ("nav_rows_settings_changed", T, H, "#define NAV_SETTINGS_ROWS       6", "#define NAV_SETTINGS_ROWS       7"),
    ("nav_rows_list_without_lines", T, F, ROWS_LIST, ROWS_LIST.replace("lines_of(world->list_lines) + ", "")),
    ("nav_rows_list_by_outcome", T, F, ROWS_LIST, ROWS_LIST.replace("list_lines", "cleared_lines")),
    ("nav_rows_list_two_behind", T, F, LIST_ROWS, "\tLIST_READ, LIST_CLEAR, LIST_ROWS, LIST_BACK,"),
    ("nav_rows_outcome_by_list", T, F, ROWS_CLEARED, ROWS_CLEARED.replace("cleared_lines", "list_lines")),
    ("nav_rows_outcome_without_fertig", T, F, ROWS_CLEARED, ROWS_CLEARED.replace(" + END_ROWS", "")),
    ("nav_rows_old_by_outcome", T, F, ROWS_OLD, ROWS_OLD.replace("old_lines", "cleared_lines")),
    ("nav_rows_old_two_behind", T, F, ROWS_OLD, ROWS_OLD.replace("END_ROWS", "END_ROWS + 1")),
    ("nav_rows_info_by_old", T, F, ROWS_INFO, ROWS_INFO.replace("info_lines", "old_lines")),
    ("nav_rows_info_one_more", T, F, ROWS_INFO, ROWS_INFO.replace(";", " + 1;")),
    ("nav_rows_clear_dialog_none", T, F, ROWS_CHOICE, "\t\tcase NAV_CONFIRM:       return CHOICE_ROWS;"),
    ("nav_rows_settings_dialog_none", T, F, ROWS_CHOICE, "\t\tcase NAV_DTC_CONFIRM:   return CHOICE_ROWS;"),
    ("nav_rows_dialogs_three", T, F, CHOICE, "\tCHOICE_CANCEL, CHOICE_ACT, CHOICE_OTHER, CHOICE_ROWS,"),
    ("nav_rows_without_rows_one", T, F, "\t\tdefault:                return 0;", "\t\tdefault:                return 1;"),
    ("nav_lines_negative_kept", T, F, "\tif(lines < 0) return 0;\n", ""),
    ("nav_lines_minus_one_kept", T, F, "\tif(lines < 0) return 0;", "\tif(lines < -1) return 0;"),
    ("nav_lines_zero_is_one", T, F, "\tif(lines < 0) return 0;", "\tif(lines <= 0) return lines == 0 ? 1 : 0;"),
    ("nav_lines_not_limited", T, F, LINES_LIMIT, "\treturn lines;"),
    ("nav_lines_limit_one_lower", T, F, "#define LINES_MAX       (INT_MAX - LIST_ROWS)", "#define LINES_MAX       (INT_MAX - LIST_ROWS - 1)"),
    ("nav_lines_limit_one_higher", T, F, "#define LINES_MAX       (INT_MAX - LIST_ROWS)", "#define LINES_MAX       (INT_MAX - LIST_ROWS + 1)"),
    ("nav_lines_limit_small", T, F, "#define LINES_MAX       (INT_MAX - LIST_ROWS)", "#define LINES_MAX       100000"),

    # entering a screen
    ("nav_enter_keeps_focus", T, F, ENTER, "\tnav->screen = screen;\n\t(void)row;\n"),
    ("nav_enter_keeps_what_waits", T, F, "\tnav->confirm = NAV_DO_NOTHING;\n}\n\nstatic int lines_of", "}\n\nstatic int lines_of"),

    # the value pages
    ("nav_pages_turn_direction_ignored", T, F, TURN_DIRECTION, "\tint direction = 1;"),
    ("nav_pages_turn_direction_reversed", T, F, TURN_DIRECTION, "\tint direction = detents < 0 ? 1 : -1;"),
    ("nav_pages_turn_one_page_at_most", T, F, TURN_LOOP, "\tfor(; detents != 0; detents = 0)"),
    ("nav_pages_turn_one_page_too_few", T, F, TURN_LOOP, "\tfor(detents -= direction; detents != 0; detents -= direction)"),
    ("nav_pages_turn_never_ends", T, F, TURN_END, ""),
    ("nav_pages_turn_wraps", T, F,
     TURN_END, "\t\tif(next == nav->page && detents == 1) next = layout_first_page(world->layout, world->catalog);\n" + TURN_END),
    ("nav_pages_turn_passes_no_page", T, F,
     "\t\tint next = layout_step_page(world->layout, world->catalog, nav->page, direction);", "\t\tint next = nav->page + direction;\n\n\t\tif(!layout_page_shown(world->layout, next, world->catalog)) break;"),
    ("nav_pages_turn_does_nothing", T, F, TURN_PAGES, "\tif(nav->screen == NAV_PAGES) return NAV_DO_NOTHING;\n"),
    ("nav_pages_lost_page_steps", T, F, TURN_LOST, "\tfor(; detents"),
    ("nav_pages_lost_page_found_then_steps", T, F, TURN_LOST, TURN_LOST.replace("\t\treturn;\n", "")),
    ("nav_pages_lost_page_not_found_by_turn", T, F, TURN_LOST, TURN_LOST.replace("\t\tnav->page = nearest_page(nav, world);\n", "")),
    ("nav_pages_lost_page_found_only_by_real_turn", T, F, TURN_LOST, TURN_LOST.replace("if(!page_shown(nav, world))", "if(!page_shown(nav, world) && detents != 0)")),
    ("nav_pages_lost_page_first_page", T, F,
     TURN_LOST, TURN_LOST.replace("nearest_page(nav, world)", "layout_first_page(world->layout, world->catalog)")),
    ("nav_pages_lost_page_in_direction", T, F,
     TURN_LOST, TURN_LOST.replace("nearest_page(nav, world)", "layout_step_page(world->layout, world->catalog, nav->page, direction)")),
    ("nav_pages_nearest_before_wins", T, F, NEAR_BEHIND + NEAR_BEFORE, NEAR_BEFORE + NEAR_BEHIND),
    ("nav_pages_nearest_only_behind", T, F, NEAR_BEFORE, ""),
    ("nav_pages_nearest_only_before", T, F, NEAR_BEHIND, ""),
    ("nav_pages_nearest_behind_whatever_the_distance", T, F,
     NEAR_LOOP, "\tfor(int i = nav->page + 1; i < LAYOUT_PAGES_MAX; i++)\n\t{\n\t\tif(layout_page_shown(world->layout, i, world->catalog)) return i;\n\t}\n" + NEAR_LOOP),
    ("nav_pages_nearest_one_page_short", T, F, NEAR_LOOP, NEAR_LOOP.replace("distance <= LAYOUT_PAGES_MAX", "distance < LAYOUT_PAGES_MAX")),
    ("nav_pages_nearest_next_door_only", T, F, NEAR_LOOP, NEAR_LOOP.replace("distance <= LAYOUT_PAGES_MAX", "distance <= 1")),
    ("nav_pages_nearest_skips_next_door", T, F, NEAR_LOOP, NEAR_LOOP.replace("distance = 1", "distance = 2")),
    ("nav_pages_none_is_page_0", T, F, "\treturn -1;\n}\n\n// A page that is not shown any more has", "\treturn 0;\n}\n\n// A page that is not shown any more has"),
    ("nav_pages_short_does_nothing", T, F, PRESS_PAGES, PRESS_PAGES.replace("\t\t\tenter(nav, NAV_MENU, MENU_DTC);\n", "")),
    ("nav_pages_short_focus_on_last_row", T, F, PRESS_PAGES, PRESS_PAGES.replace("MENU_DTC", "MENU_BACK")),
    ("nav_pages_short_goes_to_first_page", T, F, PRESS_PAGES, PRESS_PAGES.replace("\t\t\tbreak;", "\t\t\tnav->page = layout_first_page(world->layout, world->catalog);\n\t\t\tbreak;")),
    ("nav_pages_long_does_nothing", T, F, BACK_PAGES, "\t\t\t(void)world;\n\t\t\tbreak;"),
    ("nav_pages_long_page_0", T, F, BACK_PAGES, "\t\t\tnav->page = world->layout->page_count > 0 ? 0 : -1;\n\t\t\tbreak;"),
    ("nav_pages_long_opens_menu", T, F, BACK_PAGES, "\t\t\t(void)world;\n\t\t\tenter(nav, NAV_MENU, MENU_DTC);\n\t\t\tbreak;"),
    ("nav_swipe_any_number_of_pages", T, F, SWIPE, SWIPE.replace("direction < 0 ? -1 : 1", "direction")),
    ("nav_swipe_reversed", T, F, SWIPE, SWIPE.replace("direction < 0 ? -1 : 1", "direction < 0 ? 1 : -1")),
    ("nav_swipe_back_ignored", T, F, SWIPE, SWIPE.replace("direction == 0", "direction <= 0")),
    ("nav_swipe_without_direction_turns", T, F, SWIPE, SWIPE.replace(" || direction == 0", "")),
    ("nav_swipe_on_every_screen", T, F, SWIPE, SWIPE.replace("nav->screen != NAV_PAGES || ", "")),
    ("nav_swipe_under_an_overlay", T, F, SWIPE, SWIPE.replace(" || nav_overlay(world) != NAV_OVER_NONE", "")),
    ("nav_swipe_under_a_question", T, F, SWIPE, SWIPE.replace("nav_overlay(world) != NAV_OVER_NONE", "(nav_overlay(world) != NAV_OVER_NONE && nav_overlay(world) != NAV_OVER_ASK)")),
    ("nav_swipe_never", T, F, SWIPE, SWIPE.replace("\tturn_pages(nav, direction < 0 ? -1 : 1, world);", "\t(void)world;")),

    # turning
    ("nav_turn_under_an_overlay", T, F, TURN_HEAD, TURN_HEAD.replace("\tif(nav_overlay(world) != NAV_OVER_NONE) return NAV_DO_NOTHING;\n", "")),
    ("nav_turn_under_a_question", T, F, TURN_HEAD, TURN_HEAD.replace("!= NAV_OVER_NONE)", "!= NAV_OVER_NONE && nav_overlay(world) != NAV_OVER_ASK)")),
    ("nav_turn_under_the_update_question", T, F, TURN_HEAD, TURN_HEAD.replace("!= NAV_OVER_NONE)", "!= NAV_OVER_NONE && nav_overlay(world) != NAV_OVER_UPDATE)")),
    ("nav_turn_under_an_upload", T, F, TURN_HEAD, TURN_HEAD.replace("!= NAV_OVER_NONE)", "!= NAV_OVER_NONE && nav_overlay(world) != NAV_OVER_UPLOAD)")),
    ("nav_focus_in_32_bit", T, F, FOCUS, "\tint64_t row = nav->row + detents;"),
    ("nav_focus_one_row_per_turn", T, F, FOCUS, "\tint64_t row = (int64_t)nav->row + (detents > 0) - (detents < 0);"),
    ("nav_focus_reversed", T, F, FOCUS, "\tint64_t row = (int64_t)nav->row - detents;"),
    ("nav_focus_not_moved", T, F, FOCUS, "\tint64_t row = nav->row + (int64_t)detents * 0;"),
    ("nav_focus_no_end", T, F, FOCUS_TOP, "\t(void)last;\n"),
    ("nav_focus_no_beginning", T, F, FOCUS_BOTTOM, ""),
    ("nav_focus_wraps_at_the_end", T, F, FOCUS_TOP, "\tif(row > last) row = 0;\n"),
    ("nav_focus_wraps_at_the_beginning", T, F, FOCUS_BOTTOM, "\tif(row < 0) row = last > 0 ? last : 0;\n"),
    ("nav_focus_one_row_behind_the_end", T, F, FOCUS_LAST, "\tint last = nav_rows(nav, world);\n\n\tif(row > last) row = last;"),
    ("nav_focus_ends_one_row_early", T, F, FOCUS_LAST, "\tint last = nav_rows(nav, world) - 2;\n\n\tif(row > last) row = last;"),
    ("nav_brightness_turn_moves_nothing", T, F, TURN_LEVEL, "\t\treturn NAV_DO_BRIGHTNESS;"),
    ("nav_brightness_turn_not_reported", T, F, TURN_LEVEL, TURN_LEVEL.replace("return NAV_DO_BRIGHTNESS;", "return NAV_DO_NOTHING;")),
    ("nav_brightness_turn_reported_when_changed", T, F,
     TURN_LEVEL, "\t\tint before = nav->value;\n\n" + TURN_LEVEL.replace("return NAV_DO_BRIGHTNESS;", "return nav->value != before ? NAV_DO_BRIGHTNESS : NAV_DO_NOTHING;")),
    ("nav_brightness_turn_stored_at_once", T, F, TURN_LEVEL, TURN_LEVEL.replace("return NAV_DO_BRIGHTNESS;", "return NAV_DO_SETTINGS_STORE;")),
    ("nav_brightness_one_percent_per_detent", T, F, TURN_LEVEL, TURN_LEVEL.replace(" * NAV_BRIGHTNESS_STEP", "")),
    ("nav_brightness_step_changed", T, H, "#define NAV_BRIGHTNESS_STEP     5", "#define NAV_BRIGHTNESS_STEP     4"),
    ("nav_brightness_in_32_bit", T, F, TURN_LEVEL, TURN_LEVEL.replace("(int64_t)detents", "detents")),
    ("nav_brightness_reversed", T, F, TURN_LEVEL, TURN_LEVEL.replace("nav->value + ", "nav->value - ")),
    ("nav_brightness_from_the_world", T, F, TURN_LEVEL, TURN_LEVEL.replace("nav->value + ", "world->brightness + ")),
    ("nav_brightness_no_lower_limit", T, F, TOO_DARK, "\tif(value < INT_MIN) return INT_MIN;\n"),
    ("nav_brightness_no_upper_limit", T, F, TOO_BRIGHT, "\tif(value > INT_MAX) return INT_MAX;\n"),
    ("nav_brightness_one_below_the_limit", T, F, TOO_DARK, TOO_DARK.replace("value < SETTINGS_BRIGHTNESS_MIN", "value < SETTINGS_BRIGHTNESS_MIN - 1")),
    ("nav_brightness_one_above_the_limit", T, F, TOO_BRIGHT, TOO_BRIGHT.replace("value > SETTINGS_BRIGHTNESS_MAX", "value > SETTINGS_BRIGHTNESS_MAX + 1")),
    ("nav_brightness_dark_allowed", T, F, TOO_DARK, "\tif(value < 0) return 0;\n"),
    ("nav_brightness_limits_at_the_step", T, F, TOO_DARK, TOO_DARK.replace("value < SETTINGS_BRIGHTNESS_MIN", "value <= SETTINGS_BRIGHTNESS_MIN + 1")),

    # the menu
    ("nav_menu_rows_0_and_1_swapped", T, F, MENU_ROWS, "\tMENU_BRIGHTNESS, MENU_DTC, MENU_NIGHT, MENU_WEB, MENU_INFO, MENU_SETTINGS, MENU_BACK,"),
    ("nav_menu_rows_1_and_2_swapped", T, F, MENU_ROWS, "\tMENU_DTC, MENU_NIGHT, MENU_BRIGHTNESS, MENU_WEB, MENU_INFO, MENU_SETTINGS, MENU_BACK,"),
    ("nav_menu_rows_2_and_3_swapped", T, F, MENU_ROWS, "\tMENU_DTC, MENU_BRIGHTNESS, MENU_WEB, MENU_NIGHT, MENU_INFO, MENU_SETTINGS, MENU_BACK,"),
    ("nav_menu_rows_3_and_4_swapped", T, F, MENU_ROWS, "\tMENU_DTC, MENU_BRIGHTNESS, MENU_NIGHT, MENU_INFO, MENU_WEB, MENU_SETTINGS, MENU_BACK,"),
    ("nav_menu_rows_4_and_5_swapped", T, F, MENU_ROWS, "\tMENU_DTC, MENU_BRIGHTNESS, MENU_NIGHT, MENU_WEB, MENU_SETTINGS, MENU_INFO, MENU_BACK,"),
    ("nav_menu_rows_5_and_6_swapped", T, F, MENU_ROWS, "\tMENU_DTC, MENU_BRIGHTNESS, MENU_NIGHT, MENU_WEB, MENU_INFO, MENU_BACK, MENU_SETTINGS,"),
    ("nav_menu_faults_never_progress", T, F, MENU_FAULTS, "\t\t\tenter(nav, NAV_DTC, 0);"),
    ("nav_menu_faults_always_progress", T, F, MENU_FAULTS, "\t\t\tenter(nav, NAV_DTC_BUSY, 0);"),
    ("nav_menu_faults_progress_unless_idle", T, F, MENU_FAULTS, "\t\t\tenter(nav, world->flow != DTC_FLOW_IDLE ? NAV_DTC_BUSY : NAV_DTC, 0);"),
    ("nav_menu_faults_focus_on_list", T, F, MENU_FAULTS, MENU_FAULTS.replace(", 0);", ", DTC_VIEW);")),
    ("nav_under_way_not_read_sent", T, F, UNDER_WAY, UNDER_WAY.replace("flow == DTC_FLOW_READ_SENT || ", "")),
    ("nav_under_way_not_reading", T, F, UNDER_WAY, UNDER_WAY.replace("flow == DTC_FLOW_READING || ", "")),
    ("nav_under_way_not_clear_sent", T, F, UNDER_WAY, UNDER_WAY.replace("flow == DTC_FLOW_CLEAR_SENT || ", "")),
    ("nav_under_way_not_clearing", T, F, UNDER_WAY, UNDER_WAY.replace(" || flow == DTC_FLOW_CLEARING", "")),
    ("nav_under_way_with_list", T, F, UNDER_WAY, UNDER_WAY.replace(";", " || flow == DTC_FLOW_LIST;")),
    ("nav_under_way_with_unknown", T, F, UNDER_WAY, UNDER_WAY.replace(";", " || flow == DTC_FLOW_UNKNOWN;")),
    ("nav_under_way_outside_the_enum", T, F, UNDER_WAY, UNDER_WAY.replace(";", " || flow > DTC_FLOW_UNKNOWN;")),
    ("nav_menu_brightness_not_taken", T, F, MENU_LEVEL, "\t\t\tenter(nav, NAV_BRIGHTNESS, 0);\n"),
    ("nav_menu_brightness_not_limited", T, F, MENU_LEVEL, MENU_LEVEL.replace("brightness_of(world->brightness)", "world->brightness")),
    ("nav_menu_brightness_set_at_once", T, F, MENU_LEVEL + "\t\t\tbreak;", MENU_LEVEL + "\t\t\treturn NAV_DO_BRIGHTNESS;"),
    ("nav_menu_night_not_toggled", T, F, "\t\t\treturn NAV_DO_NIGHT_TOGGLE;", "\t\t\tbreak;"),
    ("nav_menu_night_leaves_menu", T, F, "\t\t\treturn NAV_DO_NIGHT_TOGGLE;", "\t\t\tenter(nav, NAV_PAGES, 0);\n\t\t\treturn NAV_DO_NIGHT_TOGGLE;"),
    ("nav_menu_web_not_opened", T, F, "\t\t\tenter(nav, NAV_WEB, 0);\n", ""),
    ("nav_menu_web_focus_on_back", T, F, "\t\t\tenter(nav, NAV_WEB, 0);\n", "\t\t\tenter(nav, NAV_WEB, 1);\n"),
    ("nav_menu_web_opens_release", T, F, "\t\t\tenter(nav, NAV_WEB, 0);\n\t\t\tbreak;", "\t\t\tenter(nav, NAV_WEB, 0);\n\t\t\treturn NAV_DO_RELEASE_ON;"),
    ("nav_menu_info_not_opened", T, F, "\t\t\tenter(nav, NAV_INFO, 0);\n", ""),
    ("nav_menu_settings_not_opened", T, F, "\t\t\tenter(nav, NAV_SETTINGS, 0);\n", ""),
    ("nav_menu_settings_focus_on_restart", T, F, "\t\t\tenter(nav, NAV_SETTINGS, 0);\n", "\t\t\tenter(nav, NAV_SETTINGS, SETTINGS_REBOOT);\n"),
    ("nav_menu_long_does_nothing", T, F, BACK_MENU, "\t\tcase NAV_MENU:\n\t\t\tbreak;\n\t\tcase NAV_DTC_BUSY:\n"),
    ("nav_menu_back_to_first_page", T, F, BACK_TO_PAGES, BACK_TO_PAGES.replace("\t\t\tbreak;", "\t\t\tnav->page = layout_first_page(world->layout, world->catalog);\n\t\t\tbreak;")),

    # the fault memory
    ("nav_dtc_rows_0_and_1_swapped", T, F, DTC_ROWS, "\tDTC_VIEW, DTC_READ, DTC_OLD, DTC_BACK,"),
    ("nav_dtc_rows_1_and_2_swapped", T, F, DTC_ROWS, "\tDTC_READ, DTC_OLD, DTC_VIEW, DTC_BACK,"),
    ("nav_dtc_rows_2_and_3_swapped", T, F, DTC_ROWS, "\tDTC_READ, DTC_VIEW, DTC_BACK, DTC_OLD,"),
    ("nav_read_without_permission", T, F, READ, READ.replace("\tif(!world->can_read) return NAV_DO_NOTHING;\n\n", "\t(void)world;\n")),
    ("nav_read_when_clear_allowed", T, F, READ, READ.replace("!world->can_read", "!world->can_read && !world->can_clear")),
    ("nav_read_without_progress", T, F, READ, READ.replace("\tenter(nav, NAV_DTC_BUSY, 0);\n", "\t(void)nav;\n")),
    ("nav_read_not_asked_for", T, F, READ, READ.replace("return NAV_DO_READ;", "return NAV_DO_NOTHING;")),
    ("nav_read_progress_without_permission", T, F, READ, "\tenter(nav, NAV_DTC_BUSY, 0);\n\treturn world->can_read ? NAV_DO_READ : NAV_DO_NOTHING;"),
    ("nav_dtc_view_enters_always", T, F, DTC_VIEW, "\t\t\tenter(nav, outcome, 0);"),
    ("nav_dtc_view_never", T, F, DTC_VIEW, "\t\t\t(void)outcome;"),
    ("nav_dtc_view_dismisses", T, F, DTC_VIEW + "\n\t\t\tbreak;", DTC_VIEW + "\n\t\t\treturn outcome != NAV_DTC ? NAV_DO_DISMISS : NAV_DO_NOTHING;"),
    ("nav_outcome_list_missing", T, F, OUTCOME_LIST, ""),
    ("nav_outcome_cleared_missing", T, F, OUTCOME_CLEARED, ""),
    ("nav_outcome_list_and_cleared_swapped", T, F,
     OUTCOME_LIST + OUTCOME_CLEARED, "\t\tcase DTC_FLOW_LIST:     return NAV_DTC_CLEARED;\n\t\tcase DTC_FLOW_CLEARED:  return NAV_DTC_LIST;\n"),
    ("nav_outcome_failed_missing", T, F, OUTCOME_FAILED, "\t\tcase DTC_FLOW_UNKNOWN:  return NAV_DTC_FAILED;\n"),
    ("nav_outcome_unknown_missing", T, F, OUTCOME_FAILED, "\t\tcase DTC_FLOW_FAILED:   return NAV_DTC_FAILED;\n"),
    ("nav_outcome_idle_is_failure", T, F, OUTCOME_FAILED, "\t\tcase DTC_FLOW_IDLE:\n" + OUTCOME_FAILED),
    ("nav_outcome_outside_the_enum_is_failure", T, F,
     "\t\tdefault:                return NAV_DTC;\n\t}\n}\n\nstatic bool page_shown", "\t\tdefault:                return flow > DTC_FLOW_UNKNOWN ? NAV_DTC_FAILED : NAV_DTC;\n\t}\n}\n\nstatic bool page_shown"),
    ("nav_dtc_old_without_a_list", T, F, DTC_OLD, "\t\t\tenter(nav, NAV_DTC_OLD, 0);"),
    ("nav_dtc_old_with_negative_lines", T, F, DTC_OLD, DTC_OLD.replace("world->old_lines > 0", "world->old_lines != 0")),
    ("nav_dtc_old_needs_two_lines", T, F, DTC_OLD, DTC_OLD.replace("world->old_lines > 0", "world->old_lines > 1")),
    ("nav_dtc_old_by_list", T, F, DTC_OLD, DTC_OLD.replace("world->old_lines > 0", "world->list_lines > 0")),
    ("nav_dtc_old_never", T, F, DTC_OLD + "\n", ""),
    ("nav_dtc_long_to_pages", T, F, BACK_DTC, BACK_DTC.replace("enter(nav, NAV_MENU, MENU_DTC);", "enter(nav, NAV_PAGES, 0);")),
    ("nav_dtc_long_does_nothing", T, F, BACK_DTC, "\t\tcase NAV_DTC:\n\t\t\tbreak;"),
    ("nav_dtc_long_focus_on_back", T, F, BACK_DTC, BACK_DTC.replace("MENU_DTC", "MENU_BACK")),
    ("nav_busy_long_does_nothing", T, F,
     BACK_MENU + BACK_TO_PAGES, "\t\tcase NAV_MENU:\n" + BACK_TO_PAGES + "\n\t\tcase NAV_DTC_BUSY:\n\t\t\tbreak;"),
    ("nav_busy_long_to_fault_memory", T, F,
     BACK_MENU + BACK_TO_PAGES, "\t\tcase NAV_MENU:\n" + BACK_TO_PAGES + "\n\t\tcase NAV_DTC_BUSY:\n\t\t\tenter(nav, NAV_DTC, 0);\n\t\t\tbreak;"),
    ("nav_busy_long_dismisses", T, F,
     BACK_MENU + BACK_TO_PAGES, "\t\tcase NAV_MENU:\n" + BACK_TO_PAGES + "\n\t\tcase NAV_DTC_BUSY:\n\t\t\tenter(nav, NAV_PAGES, 0);\n\t\t\treturn NAV_DO_DISMISS;"),
    ("nav_busy_short_leaves", T, F, "\t\t\t// A scan cannot be cancelled\n\t\t\tbreak;", "\t\t\treturn back(nav, world);"),
    ("nav_busy_short_reads_again", T, F, "\t\t\t// A scan cannot be cancelled\n\t\t\tbreak;", "\t\t\treturn start_read(nav, world);"),

    # the list
    ("nav_list_rows_read_and_clear_swapped", T, F, LIST_ROWS, "\tLIST_CLEAR, LIST_READ, LIST_BACK, LIST_ROWS,"),
    ("nav_list_rows_clear_and_back_swapped", T, F, LIST_ROWS, "\tLIST_READ, LIST_BACK, LIST_CLEAR, LIST_ROWS,"),
    ("nav_list_lines_not_counted", T, F, LIST_ROW, "\tswitch(nav->row)"),
    ("nav_list_lines_not_limited", T, F, LIST_ROW, "\tswitch(nav->row - (world->list_lines > 0 ? world->list_lines : 0))"),
    ("nav_list_negative_lines_counted", T, F, LIST_ROW, "\tswitch(nav->row - (world->list_lines < -1 ? -1 : lines_of(world->list_lines)))"),
    ("nav_list_last_line_reads", T, F, LIST_ROW, "\tswitch(nav->row - lines_of(world->list_lines) + (world->list_lines > 0 && nav->row == world->list_lines - 1 ? 1 : 0))"),
    ("nav_clear_without_permission", T, F, LIST_CLEAR, LIST_CLEAR.replace("\t\t\tif(!world->can_clear) break;\n", "")),
    ("nav_clear_when_read_allowed", T, F, LIST_CLEAR, LIST_CLEAR.replace("!world->can_clear", "!world->can_clear && !world->can_read")),
    ("nav_clear_dialog_not_entered", T, F, LIST_CLEAR, LIST_CLEAR.replace("\t\t\tenter(nav, NAV_DTC_CONFIRM, CHOICE_CANCEL);\n", "")),
    ("nav_clear_dialog_focus_on_clear", T, F, LIST_CLEAR, LIST_CLEAR.replace("CHOICE_CANCEL", "CHOICE_ACT")),
    ("nav_clear_dialog_hold_not_opened", T, F, LIST_CLEAR, LIST_CLEAR.replace("return NAV_DO_HOLD_OPEN;", "break;")),
    ("nav_clear_at_once", T, F,
     LIST_CLEAR, LIST_CLEAR.replace("\t\t\tenter(nav, NAV_DTC_CONFIRM, CHOICE_CANCEL);\n\t\t\treturn NAV_DO_HOLD_OPEN;", "\t\t\tenter(nav, NAV_DTC_BUSY, 0);\n\t\t\treturn NAV_DO_CLEAR;")),
    ("nav_list_long_does_nothing", T, F,
     "\t\tcase NAV_DTC_LIST:\n\t\tcase NAV_DTC_CLEARED:\n\t\tcase NAV_DTC_FAILED:\n", "\t\tcase NAV_DTC_LIST:\n\t\t\tbreak;\n\t\tcase NAV_DTC_CLEARED:\n\t\tcase NAV_DTC_FAILED:\n"),
    ("nav_cleared_long_does_nothing", T, F,
     "\t\tcase NAV_DTC_LIST:\n\t\tcase NAV_DTC_CLEARED:\n\t\tcase NAV_DTC_FAILED:\n", "\t\tcase NAV_DTC_CLEARED:\n\t\t\tbreak;\n\t\tcase NAV_DTC_LIST:\n\t\tcase NAV_DTC_FAILED:\n"),
    ("nav_failed_long_does_nothing", T, F,
     "\t\tcase NAV_DTC_LIST:\n\t\tcase NAV_DTC_CLEARED:\n\t\tcase NAV_DTC_FAILED:\n", "\t\tcase NAV_DTC_FAILED:\n\t\t\tbreak;\n\t\tcase NAV_DTC_LIST:\n\t\tcase NAV_DTC_CLEARED:\n"),
    ("nav_old_long_does_nothing", T, F,
     "\t\tcase NAV_DTC_FAILED:\n\t\tcase NAV_DTC_OLD:\n", "\t\tcase NAV_DTC_OLD:\n\t\t\tbreak;\n\t\tcase NAV_DTC_FAILED:\n"),
    ("nav_outcome_long_dismisses", T, F, BACK_OUTCOME, BACK_OUTCOME.replace("\t\t\tbreak;", "\t\t\treturn NAV_DO_DISMISS;")),
    ("nav_outcome_long_to_menu", T, F, BACK_OUTCOME, BACK_OUTCOME.replace("enter(nav, NAV_DTC, 0);", "enter(nav, NAV_MENU, MENU_DTC);")),
    ("nav_outcome_long_focus_on_list", T, F, BACK_OUTCOME, BACK_OUTCOME.replace("enter(nav, NAV_DTC, 0);", "enter(nav, NAV_DTC, DTC_VIEW);")),

    # the clear dialog
    ("nav_dialog_short_on_clear_cancels", T, F, PRESS_DIALOG, PRESS_DIALOG.replace("\t\t\tif(nav->row != CHOICE_CANCEL) break;\n", "")),
    ("nav_dialog_short_on_clear_clears", T, F,
     PRESS_DIALOG, PRESS_DIALOG.replace("if(nav->row != CHOICE_CANCEL) break;", "if(nav->row != CHOICE_CANCEL)\n\t\t\t{\n\t\t\t\tenter(nav, NAV_DTC_BUSY, 0);\n\t\t\t\treturn NAV_DO_CLEAR;\n\t\t\t}")),
    ("nav_dialog_cancel_does_nothing", T, F, PRESS_DIALOG, "\t\t\tbreak;"),
    ("nav_dialog_cancel_stays", T, F, PRESS_DIALOG, PRESS_DIALOG.replace("\t\t\tclose_dialog(nav, world);\n", "")),
    ("nav_dialog_cancel_keeps_hold_open", T, F, PRESS_DIALOG, PRESS_DIALOG.replace("return NAV_DO_HOLD_CLOSE;", "break;")),
    ("nav_dialog_cancel_dismisses", T, F, PRESS_DIALOG, PRESS_DIALOG.replace("return NAV_DO_HOLD_CLOSE;", "return NAV_DO_DISMISS;")),
    ("nav_dialog_closed_focus_on_first_line", T, F, CLOSE_DIALOG, "\t(void)world;\n\tenter(nav, NAV_DTC_LIST, 0);"),
    ("nav_dialog_closed_focus_on_back", T, F, CLOSE_DIALOG, CLOSE_DIALOG.replace("LIST_CLEAR", "LIST_BACK")),
    ("nav_dialog_closed_focus_on_read", T, F, CLOSE_DIALOG, CLOSE_DIALOG.replace("LIST_CLEAR", "LIST_READ")),
    ("nav_dialog_closed_lines_not_limited", T, F, CLOSE_DIALOG, CLOSE_DIALOG.replace("lines_of(world->list_lines)", "world->list_lines")),
    ("nav_dialog_closed_to_fault_memory", T, F, CLOSE_DIALOG, "\t(void)world;\n\tenter(nav, NAV_DTC, 0);"),
    ("nav_dialog_long_cancels", T, F, BACK_DIALOG, "\t\t\tclose_dialog(nav, world);\n\t\t\treturn NAV_DO_HOLD_CLOSE;"),
    ("nav_dialog_long_leaves_silently", T, F, BACK_DIALOG, "\t\t\tclose_dialog(nav, world);\n\t\t\tbreak;"),
    ("nav_dialog_long_on_cancel_cancels", T, F,
     BACK_DIALOG, "\t\t\tif(nav->row != CHOICE_CANCEL) break;\n\t\t\tclose_dialog(nav, world);\n\t\t\treturn NAV_DO_HOLD_CLOSE;"),
    ("nav_hold_on_every_screen", T, F, HOLD_HEAD, "\tadvance(nav, now_ms);\n"),
    ("nav_hold_on_the_list", T, F, HOLD_HEAD, HOLD_HEAD.replace("nav->screen != NAV_DTC_CONFIRM", "nav->screen != NAV_DTC_CONFIRM && nav->screen != NAV_DTC_LIST")),
    ("nav_hold_in_the_other_dialog", T, F, HOLD_HEAD, HOLD_HEAD.replace("nav->screen != NAV_DTC_CONFIRM", "nav->screen != NAV_DTC_CONFIRM && nav->screen != NAV_CONFIRM")),
    ("nav_hold_clears_under_an_overlay", T, F, HOLD_CLEAR, HOLD_CLEAR.replace(" && nav_overlay(world) == NAV_OVER_NONE", "")),
    ("nav_hold_clears_under_an_upload", T, F, HOLD_CLEAR, HOLD_CLEAR.replace("nav_overlay(world) == NAV_OVER_NONE", "(nav_overlay(world) == NAV_OVER_NONE || nav_overlay(world) == NAV_OVER_UPLOAD)")),
    ("nav_hold_clears_under_a_question", T, F, HOLD_CLEAR, HOLD_CLEAR.replace("nav_overlay(world) == NAV_OVER_NONE", "(nav_overlay(world) == NAV_OVER_NONE || nav_overlay(world) == NAV_OVER_ASK)")),
    ("nav_hold_clears_under_the_update_question", T, F, HOLD_CLEAR, HOLD_CLEAR.replace("nav_overlay(world) == NAV_OVER_NONE", "(nav_overlay(world) == NAV_OVER_NONE || nav_overlay(world) == NAV_OVER_UPDATE)")),
    ("nav_hold_progress_clears", T, F, HOLD_CLEAR, HOLD_CLEAR.replace("event == HOLD_CONFIRMED", "(event == HOLD_CONFIRMED || event == HOLD_PROGRESS)")),
    ("nav_hold_cancelled_clears", T, F, HOLD_CLEAR, HOLD_CLEAR.replace("event == HOLD_CONFIRMED", "(event == HOLD_CONFIRMED || event == HOLD_CANCELLED)")),
    ("nav_hold_unknown_event_clears", T, F, HOLD_CLEAR, HOLD_CLEAR.replace("event == HOLD_CONFIRMED", "(event == HOLD_CONFIRMED || event > HOLD_STUCK)")),
    ("nav_hold_confirmed_only_on_clear_row", T, F, HOLD_CLEAR, HOLD_CLEAR.replace("event == HOLD_CONFIRMED", "event == HOLD_CONFIRMED && nav->row == CHOICE_ACT")),
    ("nav_hold_confirmed_only_while_allowed", T, F, HOLD_CLEAR, HOLD_CLEAR.replace("event == HOLD_CONFIRMED", "event == HOLD_CONFIRMED && world->can_clear")),
    ("nav_hold_confirmed_clears_nothing", T, F, HOLD_CLEAR, HOLD_CLEAR.replace("return NAV_DO_CLEAR;", "return NAV_DO_NOTHING;")),
    ("nav_hold_confirmed_without_progress", T, F, HOLD_CLEAR, HOLD_CLEAR.replace("\t\tenter(nav, NAV_DTC_BUSY, 0);\n", "")),
    ("nav_hold_confirmed_back_to_list", T, F, HOLD_CLEAR, HOLD_CLEAR.replace("enter(nav, NAV_DTC_BUSY, 0);", "close_dialog(nav, world);")),
    ("nav_hold_confirmed_ignored", T, F, HOLD_CLEAR + "\n", ""),
    ("nav_hold_cancelled_stays", T, F, HOLD_ENDS, HOLD_ENDS.replace(" || event == HOLD_CANCELLED", "")),
    ("nav_hold_stuck_stays", T, F, HOLD_ENDS, HOLD_ENDS.replace(" || event == HOLD_STUCK", "")),
    ("nav_hold_confirmed_under_overlay_stays", T, F, HOLD_ENDS, HOLD_ENDS.replace("event == HOLD_CONFIRMED || ", "")),
    ("nav_hold_every_event_closes", T, F, HOLD_ENDS, "\tclose_dialog(nav, world);"),
    ("nav_hold_waiting_closes", T, F, HOLD_ENDS, HOLD_ENDS.replace("event == HOLD_STUCK", "event == HOLD_STUCK || event == HOLD_WAITING")),
    ("nav_hold_progress_closes", T, F, HOLD_ENDS, HOLD_ENDS.replace("event == HOLD_STUCK", "event == HOLD_STUCK || event == HOLD_PROGRESS")),
    ("nav_hold_unknown_event_closes", T, F, HOLD_ENDS, HOLD_ENDS.replace("event == HOLD_STUCK", "event >= HOLD_STUCK")),
    ("nav_hold_closing_asks_to_close", T, F,
     HOLD_ENDS + "\n\treturn NAV_DO_NOTHING;", HOLD_ENDS.replace("close_dialog(nav, world);", "\n\t{\n\t\tclose_dialog(nav, world);\n\t\treturn NAV_DO_HOLD_CLOSE;\n\t}") + "\n\treturn NAV_DO_NOTHING;"),

    # outcome, failure, old list
    ("nav_cleared_dismissed_on_every_row", T, F, PRESS_CLEARED, PRESS_CLEARED.replace("\t\t\tif(nav->row != lines_of(world->cleared_lines)) break;\n", "")),
    ("nav_cleared_fertig_by_list_lines", T, F, PRESS_CLEARED, PRESS_CLEARED.replace("cleared_lines", "list_lines")),
    ("nav_cleared_lines_not_limited", T, F, PRESS_CLEARED, PRESS_CLEARED.replace("lines_of(world->cleared_lines)", "world->cleared_lines")),
    ("nav_cleared_fertig_and_beyond", T, F, PRESS_CLEARED, PRESS_CLEARED.replace("nav->row != lines_of", "nav->row < lines_of")),
    ("nav_cleared_not_dismissed", T, F, PRESS_CLEARED, PRESS_CLEARED.replace("return NAV_DO_DISMISS;", "break;")),
    ("nav_cleared_dismissed_stays", T, F, PRESS_CLEARED, PRESS_CLEARED.replace("\t\t\tenter(nav, NAV_DTC, 0);\n", "")),
    ("nav_cleared_fertig_to_menu", T, F, PRESS_CLEARED, PRESS_CLEARED.replace("enter(nav, NAV_DTC, 0);", "enter(nav, NAV_MENU, MENU_DTC);")),
    ("nav_failed_not_dismissed", T, F, PRESS_FAILED, PRESS_FAILED.replace("return NAV_DO_DISMISS;", "break;")),
    ("nav_failed_short_does_nothing", T, F, PRESS_FAILED, "\t\tcase NAV_DTC_FAILED:\n\t\t\tbreak;"),
    ("nav_failed_dismissed_stays", T, F, PRESS_FAILED, PRESS_FAILED.replace("\t\t\tenter(nav, NAV_DTC, 0);\n", "")),
    ("nav_failed_short_reads_again", T, F, PRESS_FAILED, "\t\tcase NAV_DTC_FAILED:\n\t\t\treturn start_read(nav, world);"),
    ("nav_old_back_on_every_row", T, F, PRESS_OLD, "\t\t\tif(nav->row >= 0) return back(nav, world);"),
    ("nav_old_back_never", T, F, PRESS_OLD + "\n", ""),
    ("nav_old_back_by_list_lines", T, F, PRESS_OLD, PRESS_OLD.replace("old_lines", "list_lines")),
    ("nav_old_back_and_beyond", T, F, PRESS_OLD, PRESS_OLD.replace("nav->row == lines_of", "nav->row >= lines_of")),
    ("nav_old_lines_not_limited", T, F, PRESS_OLD, PRESS_OLD.replace("lines_of(world->old_lines)", "world->old_lines")),
    ("nav_old_back_dismisses", T, F, PRESS_OLD, PRESS_OLD.replace("return back(nav, world);", "\n\t\t\t{\n\t\t\t\tenter(nav, NAV_DTC, 0);\n\t\t\t\treturn NAV_DO_DISMISS;\n\t\t\t}")),

    # brightness, web access, info
    ("nav_brightness_short_does_nothing", T, F, PRESS_PLAIN, "\t\tcase NAV_BRIGHTNESS:\n\t\t\tbreak;\n\t\tcase NAV_INFO:\n\t\t\treturn back(nav, world);"),
    ("nav_info_short_does_nothing", T, F, PRESS_PLAIN, "\t\tcase NAV_INFO:\n\t\t\tbreak;\n\t\tcase NAV_BRIGHTNESS:\n\t\t\treturn back(nav, world);"),
    ("nav_info_short_only_on_first_line", T, F, PRESS_PLAIN, "\t\tcase NAV_INFO:\n\t\t\tif(nav->row > 0) break;\n\t\t\treturn back(nav, world);\n\t\tcase NAV_BRIGHTNESS:\n\t\t\treturn back(nav, world);"),
    ("nav_brightness_not_stored", T, F, BACK_BRIGHTNESS, BACK_BRIGHTNESS.replace("return NAV_DO_SETTINGS_STORE;", "break;")),
    ("nav_brightness_left_with_set", T, F, BACK_BRIGHTNESS, BACK_BRIGHTNESS.replace("NAV_DO_SETTINGS_STORE", "NAV_DO_BRIGHTNESS")),
    ("nav_brightness_stored_stays", T, F, BACK_BRIGHTNESS, BACK_BRIGHTNESS.replace("\t\t\tenter(nav, NAV_MENU, MENU_BRIGHTNESS);\n", "")),
    ("nav_brightness_back_focus_on_first_row", T, F, BACK_BRIGHTNESS, BACK_BRIGHTNESS.replace("MENU_BRIGHTNESS", "MENU_DTC")),
    ("nav_brightness_back_to_pages", T, F, BACK_BRIGHTNESS, BACK_BRIGHTNESS.replace("enter(nav, NAV_MENU, MENU_BRIGHTNESS);", "enter(nav, NAV_PAGES, 0);")),
    ("nav_brightness_stored_value_reset", T, F, BACK_BRIGHTNESS, BACK_BRIGHTNESS.replace("\t\t\treturn", "\t\t\tnav->value = 0;\n\t\t\treturn")),
    ("nav_web_back_focus_on_first_row", T, F, BACK_WEB, BACK_WEB.replace("MENU_WEB", "MENU_DTC")),
    ("nav_web_back_to_pages", T, F, BACK_WEB, "\t\t\tenter(nav, NAV_PAGES, 0);"),
    ("nav_web_long_closes_release", T, F, BACK_WEB + "\n\t\t\tbreak;", BACK_WEB + "\n\t\t\treturn NAV_DO_RELEASE_OFF;"),
    ("nav_web_release_on_every_row", T, F, PRESS_WEB, PRESS_WEB.replace("\t\t\tif(nav->row != WEB_RELEASE) return back(nav, world);\n", "")),
    ("nav_web_rows_swapped", T, F, "#define WEB_RELEASE     0", "#define WEB_RELEASE     1"),
    ("nav_web_release_reversed", T, F, PRESS_WEB, PRESS_WEB.replace("NAV_DO_RELEASE_OFF : NAV_DO_RELEASE_ON", "NAV_DO_RELEASE_ON : NAV_DO_RELEASE_OFF")),
    ("nav_web_release_only_opened", T, F, PRESS_WEB, PRESS_WEB.replace("world->release_open ? NAV_DO_RELEASE_OFF : NAV_DO_RELEASE_ON", "world->release_open ? NAV_DO_RELEASE_ON : NAV_DO_RELEASE_ON")),
    ("nav_web_release_only_closed", T, F, PRESS_WEB, PRESS_WEB.replace("world->release_open ? NAV_DO_RELEASE_OFF : NAV_DO_RELEASE_ON", "world->release_open ? NAV_DO_RELEASE_OFF : NAV_DO_RELEASE_OFF")),
    ("nav_web_release_by_night_mode", T, F, PRESS_WEB, PRESS_WEB.replace("world->release_open", "world->night_mode")),
    ("nav_web_release_leaves", T, F, PRESS_WEB, PRESS_WEB.replace("\t\t\treturn world", "\t\t\tenter(nav, NAV_MENU, MENU_WEB);\n\t\t\treturn world")),
    ("nav_info_back_focus_on_first_row", T, F, BACK_INFO, BACK_INFO.replace("MENU_INFO", "MENU_DTC")),
    ("nav_info_back_to_pages", T, F, BACK_INFO, "\t\t\tenter(nav, NAV_PAGES, 0);"),

    # the settings and their dialog
    ("nav_settings_rows_0_and_1_swapped", T, F, SETTINGS_ROWS, "\tSETTINGS_AP, SETTINGS_REVERSE, SETTINGS_REBOOT, SETTINGS_PREVIOUS, SETTINGS_RESET, SETTINGS_BACK,"),
    ("nav_settings_rows_1_and_2_swapped", T, F, SETTINGS_ROWS, "\tSETTINGS_REVERSE, SETTINGS_REBOOT, SETTINGS_AP, SETTINGS_PREVIOUS, SETTINGS_RESET, SETTINGS_BACK,"),
    ("nav_settings_rows_2_and_3_swapped", T, F, SETTINGS_ROWS, "\tSETTINGS_REVERSE, SETTINGS_AP, SETTINGS_PREVIOUS, SETTINGS_REBOOT, SETTINGS_RESET, SETTINGS_BACK,"),
    ("nav_settings_rows_3_and_4_swapped", T, F, SETTINGS_ROWS, "\tSETTINGS_REVERSE, SETTINGS_AP, SETTINGS_REBOOT, SETTINGS_RESET, SETTINGS_PREVIOUS, SETTINGS_BACK,"),
    ("nav_settings_rows_4_and_5_swapped", T, F, SETTINGS_ROWS, "\tSETTINGS_REVERSE, SETTINGS_AP, SETTINGS_REBOOT, SETTINGS_PREVIOUS, SETTINGS_BACK, SETTINGS_RESET,"),
    ("nav_settings_reverse_not_toggled", T, F, SET_REVERSE, "\t\t\tbreak;"),
    ("nav_settings_ap_not_toggled", T, F, SET_AP, "\t\t\tbreak;"),
    ("nav_settings_reverse_toggles_night", T, F, SET_REVERSE, "\t\t\treturn NAV_DO_NIGHT_TOGGLE;"),
    # the row Hotspot while the access point is kept on: nothing but the focus and the idle time
    ("nav_settings_ap_toggled_while_kept", T, F, SET_AP, "\t\t\treturn NAV_DO_AP_TOGGLE;"),
    ("nav_settings_ap_toggled_only_while_kept", T, F, SET_AP, SET_AP.replace("!world->ap_kept", "world->ap_kept")),
    ("nav_settings_ap_kept_only_while_no_request_is_under_way", T, F, SET_AP, SET_AP.replace("!world->ap_kept", "!world->ap_kept || under_way(world->flow)")),
    ("nav_settings_ap_kept_only_without_previous_firmware", T, F, SET_AP, SET_AP.replace("!world->ap_kept", "!world->ap_kept || world->previous_firmware")),
    ("nav_settings_ap_kept_leads_back", T, F, SET_AP, SET_AP.replace("\t\t\tbreak;", "\t\t\treturn back(nav, world);")),
    ("nav_settings_ap_kept_toggles_the_direction", T, F, SET_AP, SET_AP.replace("\t\t\tbreak;", "\t\t\treturn NAV_DO_REVERSE_TOGGLE;")),
    ("nav_settings_ap_kept_keeps_the_direction_too", T, F, SET_REVERSE, "\t\t\tif(!world->ap_kept) return NAV_DO_REVERSE_TOGGLE;\n\t\t\tbreak;"),
    ("nav_settings_ap_kept_keeps_the_restart_too", T, F, SET_REBOOT, "\t\t\tif(!world->ap_kept) ask(nav, NAV_DO_REBOOT, world);\n\t\t\tbreak;"),
    ("nav_settings_ap_kept_keeps_the_way_back_too", T, F,
     "\t\tcase SETTINGS_BACK:\n\t\t\treturn back(nav, world);", "\t\tcase SETTINGS_BACK:\n\t\t\tif(world->ap_kept) break;\n\t\t\treturn back(nav, world);"),
    ("nav_settings_ap_kept_press_is_no_input", T, F,
     SHORT, SHORT.replace("\tnote_input(nav, now_ms);\n", "\tif(!(" + AP_KEPT_ROW + ")) note_input(nav, now_ms);\n")),
    ("nav_settings_ap_kept_press_takes_the_time_but_is_no_input", T, F,
     SHORT, SHORT.replace("\tnote_input(nav, now_ms);\n", "\tif(" + AP_KEPT_ROW + ") advance(nav, now_ms);\n\telse note_input(nav, now_ms);\n")),
    ("nav_settings_ap_kept_tap_moves_no_focus", T, F,
     TAP_FOCUS, "\t\tif(nav->screen == NAV_SETTINGS && row == SETTINGS_AP && world->ap_kept) return NAV_DO_NOTHING;\n" + TAP_FOCUS),
    ("nav_settings_ap_kept_for_the_knob_alone", T, F,
     TAP_FOCUS, TAP_FOCUS.replace("\treturn press(nav, world);",
                                  "\tif(nav->screen == NAV_SETTINGS && nav->row == SETTINGS_AP && world->ap_kept) return NAV_DO_AP_TOGGLE;\n\treturn press(nav, world);")),
    ("nav_settings_restart_at_once", T, F, SET_REBOOT, "\t\t\treturn NAV_DO_REBOOT;"),
    ("nav_settings_restart_asks_for_reset", T, F, SET_REBOOT, SET_REBOOT.replace("NAV_DO_REBOOT", "NAV_DO_FACTORY_RESET")),
    ("nav_settings_restart_no_dialog", T, F, SET_REBOOT, "\t\t\tbreak;"),
    ("nav_settings_previous_without_firmware", T, F, SET_PREVIOUS, "\t\t\task(nav, NAV_DO_PREVIOUS_FIRMWARE, world);"),
    ("nav_settings_previous_never", T, F, SET_PREVIOUS + "\n", ""),
    ("nav_settings_previous_at_once", T, F, SET_PREVIOUS, "\t\t\tif(world->previous_firmware) return NAV_DO_PREVIOUS_FIRMWARE;"),
    ("nav_settings_previous_asks_for_restart", T, F, SET_PREVIOUS, SET_PREVIOUS.replace("NAV_DO_PREVIOUS_FIRMWARE", "NAV_DO_REBOOT")),
    ("nav_settings_reset_at_once", T, F, SET_RESET, "\t\t\treturn NAV_DO_FACTORY_RESET;"),
    ("nav_settings_reset_asks_for_restart", T, F, SET_RESET, SET_RESET.replace("NAV_DO_FACTORY_RESET", "NAV_DO_REBOOT")),
    ("nav_settings_reset_needs_previous_firmware", T, F, SET_RESET, "\t\t\tif(world->previous_firmware) ask(nav, NAV_DO_FACTORY_RESET, world);\n\t\t\tbreak;"),
    ("nav_settings_back_focus_on_first_row", T, F, BACK_SETTINGS, BACK_SETTINGS.replace("MENU_SETTINGS", "MENU_DTC")),
    ("nav_settings_back_to_pages", T, F, BACK_SETTINGS, "\t\t\tenter(nav, NAV_PAGES, 0);"),
    # ... not while the own request is under way
    ("nav_ask_while_under_way", T, F, ASK_FREE, "\t(void)world;\n\tenter(nav, NAV_CONFIRM"),
    ("nav_ask_restart_while_under_way", T, F, ASK_FREE, ASK_FREE.replace("under_way(world->flow))", "under_way(world->flow) && action != NAV_DO_REBOOT)")),
    ("nav_ask_previous_while_under_way", T, F, ASK_FREE, ASK_FREE.replace("under_way(world->flow))", "under_way(world->flow) && action != NAV_DO_PREVIOUS_FIRMWARE)")),
    ("nav_ask_reset_while_under_way", T, F, ASK_FREE, ASK_FREE.replace("under_way(world->flow))", "under_way(world->flow) && action != NAV_DO_FACTORY_RESET)")),
    ("nav_ask_refused_only_during_a_clear", T, F, ASK_FREE, ASK_FREE.replace("under_way(world->flow)", "world->flow == DTC_FLOW_CLEAR_SENT || world->flow == DTC_FLOW_CLEARING")),
    ("nav_ask_refused_only_during_a_read", T, F, ASK_FREE, ASK_FREE.replace("under_way(world->flow)", "world->flow == DTC_FLOW_READ_SENT || world->flow == DTC_FLOW_READING")),
    ("nav_ask_refused_only_once_the_adapter_accepted", T, F, ASK_FREE, ASK_FREE.replace("under_way(world->flow)", "world->flow == DTC_FLOW_READING || world->flow == DTC_FLOW_CLEARING")),
    ("nav_ask_refused_only_until_the_adapter_accepted", T, F, ASK_FREE, ASK_FREE.replace("under_way(world->flow)", "world->flow == DTC_FLOW_READ_SENT || world->flow == DTC_FLOW_CLEAR_SENT")),
    ("nav_ask_refused_unless_idle", T, F, ASK_FREE, ASK_FREE.replace("under_way(world->flow)", "world->flow != DTC_FLOW_IDLE")),
    ("nav_ask_refused_with_a_list", T, F, ASK_FREE, ASK_FREE.replace("under_way(world->flow)", "under_way(world->flow) || world->flow == DTC_FLOW_LIST")),
    ("nav_ask_refused_while_reading_is_forbidden", T, F, ASK_FREE, ASK_FREE.replace("under_way(world->flow)", "!world->can_read")),
    ("nav_ask_refused_leads_to_the_progress", T, F, ASK_FREE, ASK_FREE.replace("return;", "\n\t{\n\t\tenter(nav, NAV_DTC_BUSY, 0);\n\t\treturn;\n\t}")),
    ("nav_ask_focus_on_execute", T, F, ASK, ASK.replace("CHOICE_CANCEL", "CHOICE_ACT")),
    ("nav_ask_action_not_stored", T, F, ASK, "\tenter(nav, NAV_CONFIRM, CHOICE_CANCEL);\n\t(void)action;\n"),
    ("nav_ask_action_stored_before_entering", T, F, ASK, "\tnav->confirm = action;\n\tenter(nav, NAV_CONFIRM, CHOICE_CANCEL);\n"),
    ("nav_ask_cancel_executes", T, F, PRESS_ASK, PRESS_ASK.replace("\t\t\tif(nav->row == CHOICE_CANCEL) return back(nav, world);\n", "")),
    ("nav_ask_execute_cancels", T, F, PRESS_ASK, "\t\t\t(void)asked;\n\t\t\treturn back(nav, world);"),
    ("nav_ask_rows_swapped", T, F, CHOICE, "\tCHOICE_ACT, CHOICE_CANCEL, CHOICE_ROWS,"),
    ("nav_ask_execute_stays", T, F, PRESS_ASK, PRESS_ASK.replace("\t\t\tenter(nav, NAV_PAGES, 0);\n", "")),
    ("nav_ask_execute_to_settings", T, F, PRESS_ASK, PRESS_ASK.replace("enter(nav, NAV_PAGES, 0);", "enter(nav, NAV_SETTINGS, 0);")),
    ("nav_ask_execute_returns_nothing", T, F, PRESS_ASK, PRESS_ASK.replace("return asked;", "(void)asked;\n\t\t\tbreak;")),
    ("nav_ask_execute_always_restarts", T, F, PRESS_ASK, PRESS_ASK.replace("return asked;", "return asked != NAV_DO_NOTHING ? NAV_DO_REBOOT : NAV_DO_NOTHING;")),
    ("nav_ask_execute_needs_previous_firmware", T, F,
     PRESS_ASK, PRESS_ASK.replace("return asked;", "return asked == NAV_DO_PREVIOUS_FIRMWARE && !world->previous_firmware ? NAV_DO_NOTHING : asked;")),
    ("nav_ask_back_to_first_row", T, F, BACK_ASK, "\t\t\tenter(nav, NAV_SETTINGS, SETTINGS_REVERSE);"),
    ("nav_ask_back_restart_and_previous_swapped", T, F,
     BACK_ASK, BACK_ASK.replace("? SETTINGS_REBOOT :", "? SETTINGS_PREVIOUS :").replace("? SETTINGS_PREVIOUS : SETTINGS_RESET", "? SETTINGS_REBOOT : SETTINGS_RESET")),
    ("nav_ask_back_previous_and_reset_swapped", T, F, BACK_ASK, BACK_ASK.replace("? SETTINGS_PREVIOUS : SETTINGS_RESET", "? SETTINGS_RESET : SETTINGS_PREVIOUS")),
    ("nav_ask_back_to_menu", T, F, BACK_ASK, "\t\t\tenter(nav, NAV_MENU, MENU_SETTINGS);"),
    ("nav_ask_long_executes", T, F,
     BACK_ASK + "\n\t\t\tbreak;", "\t\t\tif(nav->row == CHOICE_ACT)\n\t\t\t{\n\t\t\t\tnav_do_t asked = nav->confirm;\n\n\t\t\t\tenter(nav, NAV_PAGES, 0);\n\t\t\t\treturn asked;\n\t\t\t}\n" + BACK_ASK + "\n\t\t\tbreak;"),

    # short and long press under an overlay
    ("nav_short_under_an_upload", T, F, SHORT, SHORT.replace("overlay != NAV_OVER_NONE", "overlay != NAV_OVER_NONE && overlay != NAV_OVER_UPLOAD")),
    ("nav_short_under_a_question", T, F, SHORT, SHORT.replace("overlay != NAV_OVER_NONE", "overlay != NAV_OVER_NONE && overlay != NAV_OVER_ASK")),
    ("nav_short_under_the_update_question", T, F, SHORT, SHORT.replace("overlay != NAV_OVER_NONE", "overlay != NAV_OVER_NONE && overlay != NAV_OVER_UPDATE")),
    ("nav_short_answers_and_acts", T, F, SHORT, SHORT.replace("return press_overlay(overlay);", "\n\t{\n\t\tpress(nav, world);\n\t\treturn press_overlay(overlay);\n\t}")),
    ("nav_question_not_confirmed", T, F, OVER_ASK, ""),
    ("nav_question_refused_by_short", T, F, OVER_ASK, OVER_ASK.replace("NAV_DO_ASK_CONFIRM", "NAV_DO_ASK_REFUSE")),
    ("nav_update_not_confirmed", T, F, OVER_UPDATE, ""),
    ("nav_question_and_update_swapped", T, F,
     OVER_ASK + OVER_UPDATE, "\tif(overlay == NAV_OVER_ASK) return NAV_DO_UPDATE_OK;\n\tif(overlay == NAV_OVER_UPDATE) return NAV_DO_ASK_CONFIRM;\n"),
    ("nav_upload_confirms_update", T, F, OVER_UPDATE, "\tif(overlay == NAV_OVER_UPDATE || overlay == NAV_OVER_UPLOAD) return NAV_DO_UPDATE_OK;\n"),
    ("nav_long_does_not_refuse", T, F, LONG, LONG.replace("\tif(overlay == NAV_OVER_ASK) return NAV_DO_ASK_REFUSE;\n", "")),
    ("nav_long_confirms", T, F, LONG, LONG.replace("NAV_DO_ASK_REFUSE", "NAV_DO_ASK_CONFIRM")),
    ("nav_long_refuses_the_update", T, F, LONG, LONG.replace("if(overlay == NAV_OVER_ASK)", "if(overlay == NAV_OVER_ASK || overlay == NAV_OVER_UPDATE)")),
    ("nav_long_confirms_the_update", T, F, LONG, LONG.replace("if(overlay != NAV_OVER_NONE) return NAV_DO_NOTHING;", "if(overlay != NAV_OVER_NONE) return overlay == NAV_OVER_UPDATE ? NAV_DO_UPDATE_OK : NAV_DO_NOTHING;")),
    ("nav_long_under_an_upload", T, F, LONG, LONG.replace("if(overlay != NAV_OVER_NONE)", "if(overlay == NAV_OVER_UPDATE)")),
    ("nav_long_under_the_update_question", T, F, LONG, LONG.replace("if(overlay != NAV_OVER_NONE)", "if(overlay == NAV_OVER_UPLOAD)")),
    ("nav_long_refuses_and_goes_back", T, F, LONG, LONG.replace("if(overlay == NAV_OVER_ASK) return NAV_DO_ASK_REFUSE;", "if(overlay == NAV_OVER_ASK)\n\t{\n\t\tback(nav, world);\n\t\treturn NAV_DO_ASK_REFUSE;\n\t}")),

    # taps
    ("nav_tap_under_an_overlay_reaches_the_screen", T, F, TAP_OVER, "\n\t// The failure"),
    ("nav_tap_under_an_upload_reaches_the_screen", T, F, TAP_OVER, TAP_OVER.replace("!= NAV_OVER_NONE)", "!= NAV_OVER_NONE && nav_overlay(world) != NAV_OVER_UPLOAD)")),
    ("nav_tap_under_a_question_reaches_the_screen", T, F, TAP_OVER, TAP_OVER.replace("!= NAV_OVER_NONE)", "!= NAV_OVER_NONE && nav_overlay(world) != NAV_OVER_ASK)")),
    ("nav_tap_under_the_update_question_reaches_the_screen", T, F, TAP_OVER, TAP_OVER.replace("!= NAV_OVER_NONE)", "!= NAV_OVER_NONE && nav_overlay(world) != NAV_OVER_UPDATE)")),
    ("nav_tap_under_an_overlay_moves_focus", T, F, TAP_OVER, "\tif(nav_overlay(world) != NAV_OVER_NONE && " + TAP_ON_A_ROW + ") nav->row = row;\n" + TAP_OVER),
    # no touch answers a question: the knob alone does
    ("nav_tap_answers_as_a_short_press", T, F, TAP_OVER, TAP_OVER.replace("return NAV_DO_NOTHING;", "return press_overlay(nav_overlay(world));")),
    ("nav_tap_confirms_what_the_browser_asks", T, F, TAP_OVER, "\tif(" + TAP_ASKED + ") return NAV_DO_ASK_CONFIRM;\n" + TAP_OVER),
    ("nav_tap_confirms_the_network", T, F, TAP_OVER, "\tif(" + TAP_ASKED + " && world->asking == ACCESS_ASK_WIFI) return NAV_DO_ASK_CONFIRM;\n" + TAP_OVER),
    ("nav_tap_confirms_the_firmware", T, F, TAP_OVER, "\tif(" + TAP_ASKED + " && world->asking == ACCESS_ASK_FIRMWARE) return NAV_DO_ASK_CONFIRM;\n" + TAP_OVER),
    ("nav_tap_confirms_the_factory_reset", T, F, TAP_OVER, "\tif(" + TAP_ASKED + " && world->asking == ACCESS_ASK_RESET) return NAV_DO_ASK_CONFIRM;\n" + TAP_OVER),
    ("nav_tap_confirms_a_question_outside_the_enum", T, F, TAP_OVER, "\tif(" + TAP_ASKED + " && world->asking > ACCESS_ASK_RESET) return NAV_DO_ASK_CONFIRM;\n" + TAP_OVER),
    ("nav_tap_confirms_on_a_row", T, F, TAP_OVER, "\tif(" + TAP_ASKED + " && " + TAP_ON_A_ROW + ") return NAV_DO_ASK_CONFIRM;\n" + TAP_OVER),
    ("nav_tap_confirms_beside_the_rows", T, F, TAP_OVER, "\tif(" + TAP_ASKED + " && !(" + TAP_ON_A_ROW + ")) return NAV_DO_ASK_CONFIRM;\n" + TAP_OVER),
    ("nav_tap_confirms_on_the_focused_row", T, F, TAP_OVER, "\tif(" + TAP_ASKED + " && row == nav->row) return NAV_DO_ASK_CONFIRM;\n" + TAP_OVER),
    ("nav_tap_confirms_over_the_pages_only", T, F, TAP_OVER, "\tif(" + TAP_ASKED + " && nav->screen == NAV_PAGES) return NAV_DO_ASK_CONFIRM;\n" + TAP_OVER),
    ("nav_tap_refuses_what_the_browser_asks", T, F, TAP_OVER, "\tif(" + TAP_ASKED + ") return NAV_DO_ASK_REFUSE;\n" + TAP_OVER),
    ("nav_tap_says_the_update_is_in_order", T, F, TAP_OVER, "\tif(nav_overlay(world) == NAV_OVER_UPDATE) return NAV_DO_UPDATE_OK;\n" + TAP_OVER),
    ("nav_tap_beside_the_rows_says_the_update_is_in_order", T, F,
     TAP_OVER, "\tif(nav_overlay(world) == NAV_OVER_UPDATE && !(" + TAP_ON_A_ROW + ")) return NAV_DO_UPDATE_OK;\n" + TAP_OVER),
    ("nav_tap_on_a_row_says_the_update_is_in_order", T, F,
     TAP_OVER, "\tif(nav_overlay(world) == NAV_OVER_UPDATE && " + TAP_ON_A_ROW + ") return NAV_DO_UPDATE_OK;\n" + TAP_OVER),
    ("nav_tap_on_failure_ignored", T, F, TAP_FAILED, "\tif(nav->screen != NAV_DTC_FAILED || nav_rows(nav, world) == 0)\n\t{"),
    ("nav_tap_on_every_screen_without_rows", T, F, TAP_FAILED, "\tif(nav_rows(nav, world) > 0)\n\t{"),
    ("nav_tap_on_brightness_stores", T, F, TAP_FAILED, "\tif(nav->screen != NAV_DTC_FAILED && nav->screen != NAV_BRIGHTNESS)\n\t{"),
    ("nav_tap_on_pages_opens_menu", T, F, TAP_FAILED, "\tif(nav->screen != NAV_DTC_FAILED && nav->screen != NAV_PAGES)\n\t{"),
    ("nav_tap_negative_row_taken", T, F, TAP_ROW, TAP_ROW.replace("row < 0 || ", "")),
    ("nav_tap_row_behind_the_last_taken", T, F, TAP_ROW, TAP_ROW.replace("row >= nav_rows", "row > nav_rows")),
    ("nav_tap_last_row_ignored", T, F, TAP_ROW, TAP_ROW.replace("row >= nav_rows(nav, world)", "row >= nav_rows(nav, world) - 1")),
    ("nav_tap_first_row_ignored", T, F, TAP_ROW, TAP_ROW.replace("row < 0", "row <= 0")),
    ("nav_tap_every_row_taken", T, F, TAP_ROW, "\t\tif(nav_rows(nav, world) == 0) return NAV_DO_NOTHING;\n"),
    ("nav_tap_does_not_move_focus", T, F, TAP_FOCUS, TAP_FOCUS.replace("\t\tnav->row = row;\n", "")),
    ("nav_tap_only_moves_focus", T, F, TAP_FOCUS, TAP_FOCUS.replace("\t\tnav->row = row;\n", "\t\tnav->row = row;\n\t\treturn NAV_DO_NOTHING;\n")),
    ("nav_tap_on_focused_row_only_acts", T, F, TAP_FOCUS, TAP_FOCUS.replace("\t\tnav->row = row;\n", "\t\tif(nav->row != row)\n\t\t{\n\t\t\tnav->row = row;\n\t\t\treturn NAV_DO_NOTHING;\n\t\t}\n")),

    # taps in the two dialogs
    ("nav_dialog_taps_reach_both_answers", T, F, TAP_DIALOG, ""),
    ("nav_dialog_tap_moves_focus_to_clear", T, F, TAP_DIALOG, TAP_DIALOG.replace("(nav->screen == NAV_DTC_CONFIRM || nav->screen == NAV_CONFIRM)", "nav->screen == NAV_CONFIRM")),
    ("nav_dialog_tap_executes", T, F, TAP_DIALOG, TAP_DIALOG.replace("(nav->screen == NAV_DTC_CONFIRM || nav->screen == NAV_CONFIRM)", "nav->screen == NAV_DTC_CONFIRM")),
    ("nav_dialog_tap_does_not_cancel", T, F, TAP_DIALOG, TAP_DIALOG.replace(" && row != CHOICE_CANCEL", "")),
    ("nav_dialog_tap_on_cancel_ignored_tap_on_action_taken", T, F, TAP_DIALOG, TAP_DIALOG.replace("row != CHOICE_CANCEL", "row == CHOICE_CANCEL")),
    ("nav_dialog_tap_on_focused_answer_acts", T, F, TAP_DIALOG, TAP_DIALOG.replace("row != CHOICE_CANCEL", "row != CHOICE_CANCEL && row != nav->row")),
    ("nav_dialog_tap_ignored_only_from_cancel", T, F, TAP_DIALOG, TAP_DIALOG.replace("row != CHOICE_CANCEL", "row != CHOICE_CANCEL && nav->row == CHOICE_CANCEL")),
    ("nav_dialog_tap_moves_the_focus", T, F, TAP_DIALOG, TAP_DIALOG.replace("return NAV_DO_NOTHING;", "\n\t\t{\n\t\t\tnav->row = row;\n\t\t\treturn NAV_DO_NOTHING;\n\t\t}")),
    ("nav_dialog_tap_executes_only_the_restart", T, F,
     TAP_DIALOG, TAP_DIALOG.replace("nav->screen == NAV_CONFIRM)", "(nav->screen == NAV_CONFIRM && nav->confirm != NAV_DO_REBOOT))")),
    ("nav_dialog_tap_executes_the_previous_firmware", T, F,
     TAP_DIALOG, TAP_DIALOG.replace("nav->screen == NAV_CONFIRM)", "(nav->screen == NAV_CONFIRM && nav->confirm != NAV_DO_PREVIOUS_FIRMWARE))")),
    ("nav_dialog_tap_executes_the_factory_reset", T, F,
     TAP_DIALOG, TAP_DIALOG.replace("nav->screen == NAV_CONFIRM)", "(nav->screen == NAV_CONFIRM && nav->confirm != NAV_DO_FACTORY_RESET))")),
    ("nav_dialog_ignored_tap_is_no_input", T, F,
     TAP_BODY + TAP_KNOB_ALONE, "\tuint64_t last_input_ms = nav->last_input_ms;\n\n" + TAP_BODY +
     TAP_KNOB_ALONE.replace("return NAV_DO_NOTHING;", "\n\t\t{\n\t\t\tnav->last_input_ms = last_input_ms;\n\t\t\treturn NAV_DO_NOTHING;\n\t\t}")),

    # nav_cancel
    ("nav_cancel_counts_as_input", T, F, CANCEL_TIME, CANCEL_TIME.replace("advance(nav, now_ms)", "note_input(nav, now_ms)")),
    ("nav_cancel_does_not_move_the_time", T, F, CANCEL_TIME, "\t(void)now_ms;\n"),
    ("nav_cancel_at_the_time_of_the_caller", T, F, CANCEL_TIME, "\tnav->clock_ms = now_ms;\n"),
    ("nav_cancel_leaves_the_clear_dialog_open", T, F, CANCEL_CLEAR, ""),
    ("nav_cancel_does_not_ask_to_close_the_hold", T, F, CANCEL_CLEAR, CANCEL_CLEAR.replace("NAV_DO_HOLD_CLOSE", "NAV_DO_NOTHING")),
    ("nav_cancel_clears", T, F, CANCEL_CLEAR, CANCEL_CLEAR.replace("\t\tclose_dialog(nav, world);\n\t\treturn NAV_DO_HOLD_CLOSE;", "\t\tenter(nav, NAV_DTC_BUSY, 0);\n\t\treturn NAV_DO_CLEAR;")),
    ("nav_cancel_clear_dialog_to_the_fault_memory", T, F, CANCEL_CLEAR, CANCEL_CLEAR.replace("close_dialog(nav, world);", "enter(nav, NAV_DTC, 0);")),
    ("nav_cancel_clear_dialog_only_from_the_action", T, F, CANCEL_CLEAR, CANCEL_CLEAR.replace("nav->screen == NAV_DTC_CONFIRM", "nav->screen == NAV_DTC_CONFIRM && nav->row == CHOICE_ACT")),
    ("nav_cancel_leaves_the_dialog_of_the_settings_open", T, F, CANCEL_ASK, ""),
    ("nav_cancel_dialog_of_the_settings_only_from_the_action", T, F, CANCEL_ASK, CANCEL_ASK.replace("nav->screen == NAV_CONFIRM", "nav->screen == NAV_CONFIRM && nav->row == CHOICE_ACT")),
    ("nav_cancel_carries_out_what_the_dialog_asks", T, F,
     CANCEL_ASK, "\tif(nav->screen == NAV_CONFIRM)\n\t{\n\t\tnav_do_t asked = nav->confirm;\n\n\t\tback(nav, world);\n\t\treturn asked;\n\t}\n"),
    ("nav_cancel_dialog_of_the_settings_to_the_pages", T, F, CANCEL_ASK, "\tif(nav->screen == NAV_CONFIRM) enter(nav, NAV_PAGES, 0);\n"),
    ("nav_cancel_keeps_what_waits", T, F, CANCEL_ASK, "\tif(nav->screen == NAV_CONFIRM)\n\t{\n\t\tnav->screen = NAV_SETTINGS;\n\t\tnav->row = SETTINGS_RESET;\n\t}\n"),
    ("nav_cancel_goes_back_from_every_screen", T, F, CANCEL_ASK, "\treturn back(nav, world);\n"),
    ("nav_cancel_not_under_an_overlay", T, F, CANCEL_TIME, CANCEL_TIME + "\tif(nav_overlay(world) != NAV_OVER_NONE) return NAV_DO_NOTHING;\n"),
    ("nav_cancel_not_under_an_upload", T, F, CANCEL_TIME, CANCEL_TIME + "\tif(nav_overlay(world) == NAV_OVER_UPLOAD) return NAV_DO_NOTHING;\n"),
    ("nav_cancel_not_under_a_question", T, F, CANCEL_TIME, CANCEL_TIME + "\tif(nav_overlay(world) == NAV_OVER_ASK) return NAV_DO_NOTHING;\n"),
    ("nav_cancel_not_under_the_update_question", T, F, CANCEL_TIME, CANCEL_TIME + "\tif(nav_overlay(world) == NAV_OVER_UPDATE) return NAV_DO_NOTHING;\n"),

    # nav_tick: the clear dialog
    ("nav_tick_dialog_stays", T, F, TICK_DIALOG, ""),
    ("nav_tick_dialog_stays_without_list", T, F, TICK_DIALOG_IF, "\tif(screen == NAV_DTC_CONFIRM && !world->can_clear)"),
    ("nav_tick_dialog_stays_when_forbidden", T, F, TICK_DIALOG_IF, "\tif(screen == NAV_DTC_CONFIRM && world->flow != DTC_FLOW_LIST)"),
    ("nav_tick_dialog_needs_both", T, F, TICK_DIALOG_IF, TICK_DIALOG_IF.replace("||", "&&")),
    ("nav_tick_dialog_stays_with_outcome", T, F, TICK_DIALOG_IF, TICK_DIALOG_IF.replace("world->flow != DTC_FLOW_LIST", "(world->flow != DTC_FLOW_LIST && world->flow != DTC_FLOW_CLEARED)")),
    ("nav_tick_dialog_stays_under_an_overlay", T, F, TICK_DIALOG_IF, TICK_DIALOG_IF.replace("screen == NAV_DTC_CONFIRM", "screen == NAV_DTC_CONFIRM && nav_overlay(world) == NAV_OVER_NONE")),
    ("nav_tick_dialog_always_to_fault_memory", T, F, TICK_DIALOG_TO, TICK_DIALOG_TO.replace("if(world->flow == DTC_FLOW_LIST) close_dialog(nav, world);\n\t\telse enter", "enter")),
    ("nav_tick_dialog_always_to_list", T, F, TICK_DIALOG_TO, TICK_DIALOG_TO.replace("\t\telse enter(nav, NAV_DTC, 0);\n", "\t\telse close_dialog(nav, world);\n")),
    ("nav_tick_dialog_to_list_while_forbidden", T, F, TICK_DIALOG_TO, TICK_DIALOG_TO.replace("if(world->flow == DTC_FLOW_LIST)", "if(!world->can_clear)")),
    ("nav_tick_dialog_list_focus_on_first_line", T, F, TICK_DIALOG_TO, TICK_DIALOG_TO.replace("close_dialog(nav, world);", "enter(nav, NAV_DTC_LIST, 0);")),
    ("nav_tick_dialog_fault_memory_focus_on_list", T, F, TICK_DIALOG_TO, TICK_DIALOG_TO.replace("enter(nav, NAV_DTC, 0);", "enter(nav, NAV_DTC, DTC_VIEW);")),
    ("nav_tick_dialog_hold_not_closed", T, F, TICK_DIALOG_TO, TICK_DIALOG_TO.replace("NAV_DO_HOLD_CLOSE", "NAV_DO_NOTHING")),
    ("nav_tick_dialog_waits_while_idle", T, F, TICK_DIALOG_IF, TICK_DIALOG_IF.replace("screen == NAV_DTC_CONFIRM", "screen == NAV_DTC_CONFIRM && !(" + IDLE_OVER + ")")),

    # nav_tick: the progress
    ("nav_tick_busy_stays", T, F, TICK_BUSY, ""),
    ("nav_tick_busy_leaves_while_under_way", T, F, TICK_BUSY_IF, "\tif(screen == NAV_DTC_BUSY && world->flow != DTC_FLOW_READING)"),
    ("nav_tick_busy_only_ends_with_a_list", T, F, TICK_BUSY_IF, "\tif(screen == NAV_DTC_BUSY && world->flow == DTC_FLOW_LIST)"),
    ("nav_tick_busy_stays_when_idle", T, F, TICK_BUSY_IF, "\tif(screen == NAV_DTC_BUSY && !under_way(world->flow) && world->flow != DTC_FLOW_IDLE)"),
    ("nav_tick_busy_stays_outside_the_enum", T, F, TICK_BUSY_IF, "\tif(screen == NAV_DTC_BUSY && !under_way(world->flow) && world->flow <= DTC_FLOW_UNKNOWN)"),
    ("nav_tick_busy_stays_under_an_overlay", T, F, TICK_BUSY_IF, "\tif(screen == NAV_DTC_BUSY && !under_way(world->flow) && nav_overlay(world) == NAV_OVER_NONE)"),
    ("nav_tick_busy_ends_in_fault_memory", T, F, TICK_BUSY, TICK_BUSY.replace("enter(nav, outcome, 0);", "enter(nav, NAV_DTC, 0);")),
    ("nav_tick_busy_ends_in_pages", T, F, TICK_BUSY, TICK_BUSY.replace("enter(nav, outcome, 0);", "enter(nav, NAV_PAGES, 0);")),
    ("nav_tick_busy_end_focus_on_last_row", T, F, TICK_BUSY, TICK_BUSY.replace("enter(nav, outcome, 0);", "enter(nav, outcome, 0);\n\t\tnav->row = nav_rows(nav, world) > 0 ? nav_rows(nav, world) - 1 : 0;")),
    ("nav_tick_busy_end_dismisses", T, F, TICK_BUSY, TICK_BUSY.replace("return NAV_DO_NOTHING;", "return outcome == NAV_DTC_FAILED ? NAV_DO_DISMISS : NAV_DO_NOTHING;")),
    ("nav_tick_busy_end_waits_while_idle", T, F, TICK_BUSY_IF, "\tif(screen == NAV_DTC_BUSY && !under_way(world->flow) && !(" + IDLE_OVER + "))"),
    ("nav_tick_busy_end_restarts_idle_time", T, F, TICK_BUSY, TICK_BUSY.replace("enter(nav, outcome, 0);", "enter(nav, outcome, 0);\n\t\tnav->last_input_ms = nav->clock_ms;")),

    # nav_tick: what a screen showed is gone
    ("nav_tick_gone_never", T, F, TICK_GONE, ""),
    ("nav_tick_list_stays", T, F, TICK_GONE_IF, TICK_GONE_IF.replace("screen == NAV_DTC_LIST || ", "")),
    ("nav_tick_cleared_stays", T, F, TICK_GONE_IF, TICK_GONE_IF.replace("screen == NAV_DTC_CLEARED || ", "")),
    ("nav_tick_failed_stays", T, F, TICK_GONE_IF, TICK_GONE_IF.replace(" || screen == NAV_DTC_FAILED", "")),
    ("nav_tick_old_stays", T, F, TICK_GONE_IF, TICK_GONE_IF.replace(" ||\n\t   (screen == NAV_DTC_OLD && world->old_lines <= 0)", "")),
    ("nav_tick_old_stays_with_negative_lines", T, F, TICK_GONE_IF, TICK_GONE_IF.replace("world->old_lines <= 0", "world->old_lines == 0")),
    ("nav_tick_old_stays_without_lines", T, F, TICK_GONE_IF, TICK_GONE_IF.replace("world->old_lines <= 0", "world->old_lines < 0")),
    ("nav_tick_old_gone_with_one_line", T, F, TICK_GONE_IF, TICK_GONE_IF.replace("world->old_lines <= 0", "world->old_lines <= 1")),
    ("nav_tick_old_follows_the_flow", T, F, TICK_GONE_IF, TICK_GONE_IF.replace("world->old_lines <= 0", "(world->old_lines <= 0 || under_way(world->flow))")),
    ("nav_tick_list_stays_while_under_way", T, F, TICK_GONE_IF, TICK_GONE_IF.replace("screen != outcome)", "screen != outcome && !under_way(world->flow))")),
    ("nav_tick_gone_stays_under_an_overlay", T, F, TICK_GONE_IF, TICK_GONE_IF.replace("\tif(((screen", "\tif(nav_overlay(world) == NAV_OVER_NONE && (((screen").replace("<= 0))", "<= 0)))")),
    ("nav_tick_gone_only_with_focus_on_first_row", T, F, TICK_GONE_IF, TICK_GONE_IF.replace("\tif(((screen", "\tif(nav->row == 0 && (((screen").replace("<= 0))", "<= 0)))")),
    ("nav_tick_gone_to_menu", T, F, TICK_GONE, TICK_GONE.replace("enter(nav, NAV_DTC, 0);", "enter(nav, NAV_MENU, MENU_DTC);")),
    ("nav_tick_gone_to_outcome", T, F, TICK_GONE, TICK_GONE.replace("enter(nav, NAV_DTC, 0);", "enter(nav, screen == NAV_DTC_OLD ? NAV_DTC : outcome, 0);")),
    ("nav_tick_gone_keeps_focus", T, F, TICK_GONE, TICK_GONE.replace("enter(nav, NAV_DTC, 0);", "enter(nav, NAV_DTC, nav->row < NAV_DTC_ROWS ? nav->row : 0);")),
    ("nav_tick_gone_dismisses", T, F, TICK_GONE, TICK_GONE.replace("return NAV_DO_NOTHING;", "return NAV_DO_DISMISS;")),
    ("nav_tick_gone_then_idle_at_once", T, F, TICK_GONE, TICK_GONE.replace("\t\treturn NAV_DO_NOTHING;\n", "\t\tscreen = NAV_DTC;\n")),
    ("nav_tick_gone_waits_while_idle", T, F, TICK_GONE_IF, TICK_GONE_IF.replace("\tif(((screen", "\tif(!(" + IDLE_OVER + ") && (((screen").replace("<= 0))", "<= 0)))")),

    # nav_tick: the page
    ("nav_tick_page_not_followed", T, F, TICK_PAGE, ""),
    ("nav_tick_page_followed_below_other_screens", T, F, TICK_PAGE_IF, "\tif(!page_shown(nav, world))"),
    ("nav_tick_page_first_page", T, F, TICK_PAGE, TICK_PAGE.replace("nearest_page(nav, world)", "layout_first_page(world->layout, world->catalog)")),
    ("nav_tick_page_next_page", T, F, TICK_PAGE, TICK_PAGE.replace("nearest_page(nav, world)", "layout_step_page(world->layout, world->catalog, nav->page, 1)")),
    ("nav_tick_page_no_page", T, F, TICK_PAGE, TICK_PAGE.replace("nearest_page(nav, world)", "-1")),
    ("nav_tick_page_stays_under_an_overlay", T, F, TICK_PAGE_IF, "\tif(screen == NAV_PAGES && !page_shown(nav, world) && nav_overlay(world) == NAV_OVER_NONE)"),
    ("nav_tick_page_only_when_none", T, F, TICK_PAGE_IF, "\tif(screen == NAV_PAGES && nav->page < 0 && !page_shown(nav, world))"),
    ("nav_tick_page_only_when_lost", T, F, TICK_PAGE_IF, "\tif(screen == NAV_PAGES && nav->page >= 0 && !page_shown(nav, world))"),

    # nav_tick: the focus
    ("nav_tick_focus_stays_beyond", T, F, TICK_FOCUS, ""),
    ("nav_tick_focus_to_first_row", T, F, TICK_FOCUS, TICK_FOCUS.replace("nav->row = last;", "nav->row = 0;")),
    ("nav_tick_focus_negative_without_rows", T, F, TICK_FOCUS_FLOOR, ""),
    ("nav_tick_focus_moved_from_last_row", T, F, TICK_FOCUS_IF, "\tif(nav->row >= last && last > 0)"),
    ("nav_tick_focus_last_row_blocks_idle", T, F, TICK_FOCUS_IF, "\tif(nav->row >= last)"),
    ("nav_tick_focus_one_beyond_allowed", T, F, TICK_FOCUS_IF, "\tif(nav->row > last + 1)"),
    ("nav_tick_focus_stays_under_an_overlay", T, F, TICK_FOCUS_IF, "\tif(nav->row > last && nav_overlay(world) == NAV_OVER_NONE)"),
    ("nav_tick_focus_then_idle_at_once", T, F, TICK_FOCUS, TICK_FOCUS.replace("\t\treturn NAV_DO_NOTHING;\n", "")),
    ("nav_tick_focus_waits_while_idle", T, F, TICK_FOCUS_IF, "\tif(nav->row > last && !(" + IDLE_OVER + "))"),
    ("nav_tick_focus_before_gone", T, F,
     TICK_GONE + TICK_PAGE + "\n\tlast = nav_rows(nav, world) - 1;\n" + TICK_FOCUS_FLOOR + TICK_FOCUS,
     "\tlast = nav_rows(nav, world) - 1;\n" + TICK_FOCUS_FLOOR + TICK_FOCUS + TICK_GONE + TICK_PAGE),

    # nav_tick: the idle time
    ("nav_idle_one_ms_late", T, F, TICK_IDLE, TICK_IDLE.replace("< NAV_IDLE_MS", "<= NAV_IDLE_MS")),
    ("nav_idle_one_ms_early", T, F, TICK_IDLE, TICK_IDLE.replace("< NAV_IDLE_MS", "< NAV_IDLE_MS - 1")),
    ("nav_idle_time_shorter", T, H, "#define NAV_IDLE_MS             120000u", "#define NAV_IDLE_MS             119999u"),
    ("nav_idle_time_longer", T, H, "#define NAV_IDLE_MS             120000u", "#define NAV_IDLE_MS             120001u"),
    ("nav_idle_never", T, F, TICK_IDLE, "\tif(nav->clock_ms >= nav->last_input_ms || nav_overlay(world) != NAV_OVER_NONE) return NAV_DO_NOTHING;"),
    ("nav_idle_at_the_time_of_the_caller", T, F, TICK_IDLE, TICK_IDLE.replace("nav->clock_ms - nav->last_input_ms", "now_ms - nav->last_input_ms")),
    ("nav_idle_counted_from_start", T, F, TICK_IDLE, TICK_IDLE.replace("nav->clock_ms - nav->last_input_ms", "nav->clock_ms")),
    ("nav_idle_under_an_overlay", T, F, TICK_IDLE, TICK_IDLE.replace(" || nav_overlay(world) != NAV_OVER_NONE", "")),
    ("nav_idle_under_an_upload", T, F, TICK_IDLE, TICK_IDLE.replace("nav_overlay(world) != NAV_OVER_NONE", "(nav_overlay(world) != NAV_OVER_NONE && nav_overlay(world) != NAV_OVER_UPLOAD)")),
    ("nav_idle_under_a_question", T, F, TICK_IDLE, TICK_IDLE.replace("nav_overlay(world) != NAV_OVER_NONE", "(nav_overlay(world) != NAV_OVER_NONE && nav_overlay(world) != NAV_OVER_ASK)")),
    ("nav_idle_under_the_update_question", T, F, TICK_IDLE, TICK_IDLE.replace("nav_overlay(world) != NAV_OVER_NONE", "(nav_overlay(world) != NAV_OVER_NONE && nav_overlay(world) != NAV_OVER_UPDATE)")),
    ("nav_idle_overlay_restarts_idle_time", T, F,
     TICK_IDLE, "\tif(nav_overlay(world) != NAV_OVER_NONE) nav->last_input_ms = nav->clock_ms;\n" + TICK_IDLE),
    ("nav_idle_leaves_progress", T, F, TICK_OWN, "\tif(screen == NAV_DTC_CONFIRM) return NAV_DO_NOTHING;\n"),
    ("nav_idle_leaves_clear_dialog", T, F, TICK_OWN, "\tif(screen == NAV_DTC_BUSY) return NAV_DO_NOTHING;\n"),
    ("nav_idle_stays_in_other_dialog", T, F, TICK_OWN, "\tif(screen == NAV_DTC_BUSY || screen == NAV_DTC_CONFIRM || screen == NAV_CONFIRM) return NAV_DO_NOTHING;\n"),
    ("nav_idle_stays_in_fault_memory", T, F, TICK_OWN, "\tif(screen >= NAV_DTC && screen <= NAV_DTC_OLD) return NAV_DO_NOTHING;\n"),
    ("nav_idle_stays_on_brightness", T, F, TICK_OWN, "\tif(screen == NAV_DTC_BUSY || screen == NAV_DTC_CONFIRM || screen == NAV_BRIGHTNESS) return NAV_DO_NOTHING;\n"),
    ("nav_idle_does_not_return", T, F, TICK_RETURN, "\treturn NAV_DO_NOTHING;"),
    ("nav_idle_returns_to_menu", T, F, TICK_RETURN, TICK_RETURN.replace("enter(nav, NAV_PAGES, 0);", "enter(nav, screen == NAV_MENU ? NAV_PAGES : NAV_MENU, 0);")),
    ("nav_idle_one_level_back", T, F, TICK_RETURN, "\treturn back(nav, world);"),
    ("nav_idle_returns_to_first_page", T, F, TICK_RETURN, TICK_RETURN.replace("enter(nav, NAV_PAGES, 0);", "enter(nav, NAV_PAGES, 0);\n\tnav->page = layout_first_page(world->layout, world->catalog);")),
    ("nav_idle_brightness_not_stored", T, F, TICK_RETURN, TICK_RETURN.replace("screen == NAV_BRIGHTNESS ? NAV_DO_SETTINGS_STORE : NAV_DO_NOTHING", "NAV_DO_NOTHING")),
    ("nav_idle_always_stores", T, F, TICK_RETURN, TICK_RETURN.replace("screen == NAV_BRIGHTNESS ? NAV_DO_SETTINGS_STORE : NAV_DO_NOTHING", "NAV_DO_SETTINGS_STORE")),
    ("nav_idle_dismisses_outcome", T, F, TICK_RETURN, TICK_RETURN.replace(": NAV_DO_NOTHING", ": screen == NAV_DTC_CLEARED ? NAV_DO_DISMISS : NAV_DO_NOTHING")),
    ("nav_idle_dismisses_failure", T, F, TICK_RETURN, TICK_RETURN.replace(": NAV_DO_NOTHING", ": screen == NAV_DTC_FAILED ? NAV_DO_DISMISS : NAV_DO_NOTHING")),
    ("nav_idle_executes_dialog", T, F, TICK_RETURN, "\tlast = (int)nav->confirm;\n" + TICK_RETURN.replace(": NAV_DO_NOTHING", ": screen == NAV_CONFIRM ? (nav_do_t)last : NAV_DO_NOTHING")),

    # nav_row_acts: a row acts exactly when a short press on it does something
    ("nav_acts_row_that_does_not_exist", T, F, ACTS_ROW, ""),
    ("nav_acts_row_before_the_first", T, F, ACTS_ROW, "\tif(row >= nav_rows(nav, world)) return false;\n"),
    ("nav_acts_row_behind_the_last", T, F, ACTS_ROW, "\tif(row < 0) return false;\n"),
    ("nav_acts_one_row_behind_the_last", T, F, ACTS_ROW, "\tif(row < 0 || row > nav_rows(nav, world)) return false;\n"),
    ("nav_acts_first_row_does_not_exist", T, F, ACTS_ROW, "\tif(row <= 0 || row >= nav_rows(nav, world)) return false;\n"),
    ("nav_acts_for_the_row_in_focus", T, F, ACTS_TRY, ""),
    ("nav_acts_only_by_action", T, F, ACTS, "\treturn press(&tried, world) != NAV_DO_NOTHING;"),
    ("nav_acts_only_by_screen", T, F, ACTS, "\tpress(&tried, world);\n\treturn tried.screen != nav->screen;"),
    ("nav_acts_always", T, F, ACTS, "\tpress(&tried, world);\n\treturn true;"),
    ("nav_acts_never", T, F, ACTS, "\tpress(&tried, world);\n\treturn false;"),
    ("nav_acts_not_under_an_overlay", T, F, ACTS_ROW, "\tif(nav_overlay(world) != NAV_OVER_NONE || row < 0 || row >= nav_rows(nav, world)) return false;\n"),
    # the knob is asked, not the finger: a tap does nothing on the second answer of a dialog, a press does
    ("nav_acts_as_a_tap_would", T, F,
     ACTS_ROW, ACTS_ROW + "\tif((nav->screen == NAV_DTC_CONFIRM || nav->screen == NAV_CONFIRM) && row != CHOICE_CANCEL) return false;\n"),
    ("nav_acts_lines_of_the_list_too", T, F, ACTS, ACTS.replace(";", " || nav->screen == NAV_DTC_LIST;")),
    ("nav_acts_info_lines_do_not", T, F, ACTS_ROW, ACTS_ROW + "\tif(nav->screen == NAV_INFO) return false;\n"),
    # the row Hotspot acts by what the press does, as every row: not by a rule kept for this question
    ("nav_acts_hotspot_also_while_kept", T, F, ACTS, ACTS.replace(";", " || (nav->screen == NAV_SETTINGS && row == SETTINGS_AP);")),
    ("nav_acts_hotspot_never", T, F, ACTS_ROW, ACTS_ROW + "\tif(nav->screen == NAV_SETTINGS && row == SETTINGS_AP) return false;\n"),
    ("nav_acts_kept_hotspot_under_an_overlay", T, F,
     ACTS, ACTS.replace(";", " || (nav->screen == NAV_SETTINGS && row == SETTINGS_AP && nav_overlay(world) != NAV_OVER_NONE);")),
    ("nav_acts_no_row_of_the_settings_while_kept", T, F, ACTS_ROW, ACTS_ROW + "\tif(nav->screen == NAV_SETTINGS && world->ap_kept) return false;\n"),
]
