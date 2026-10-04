"""Mutations of display/components/core/access.c, see ../redproof.py."""

F = "components/core/access.c"
H = "components/core/access.h"
T = "test_access"

AFTER = "\treturn now > UINT64_MAX - duration_ms ? UINT64_MAX : now + duration_ms;"
SECONDS = "\treturn (uint32_t)((ms + 999) / 1000);"
WAITS = "\tif(access->asking == ACCESS_ASK_NONE) return;\n\n"
ENDS = "\taccess->asking = ACCESS_ASK_NONE;\n\taccess->ticket_end = end;\n"
CLOCK = "\tif(now_ms > access->clock_ms) access->clock_ms = now_ms;\n"
EXPIRES = ("\tif(access->clock_ms >= access->asking_until_ms && access->asking_until_ms <= access->open_until_ms)\n"
           "\t{\n\t\tend_question(access, ACCESS_TICKET_EXPIRED);\n\t}\n")
RELEASE_ENDS = ("\tif(access->clock_ms >= access->open_until_ms)\n"
                "\t{\n\t\tend_question(access, ACCESS_TICKET_REFUSED);\n\t\taccess->open = false;\n\t}\n")
AT = "\tsettle(&state, now_ms);\n\treturn state;"
INIT = "\tmemset(access, 0, sizeof(*access));"
OPEN = "\tsettle(access, now_ms);\n\taccess->open = true;\n\taccess->open_until_ms = after(access->clock_ms, ACCESS_OPEN_MS);\n"
CLOSE = "\tsettle(access, now_ms);\n\tend_question(access, ACCESS_TICKET_REFUSED);\n\taccess->open = false;\n"
IS_OPEN = "\treturn at(access, now_ms).open;"
SECONDS_LEFT = "\treturn state.open ? seconds_rounded_up(state.open_until_ms - state.clock_ms) : 0;"
WRITE = ("\tsettle(access, now_ms);\n\tif(!access->open) return false;\n\n"
         "\taccess->open_until_ms = after(access->clock_ms, ACCESS_OPEN_MS);\n\treturn true;\n")
ASK_SETTLE = "\tsettle(access, now_ms);\n"
ASK_STATE = "\tif(!access->open || access->asking != ACCESS_ASK_NONE) return 0;\n"
ASK_KINDS = "\tif(ask != ACCESS_ASK_WIFI && ask != ACCESS_ASK_FIRMWARE && ask != ACCESS_ASK_RESET) return 0;\n"
ASK_PREVIOUS = "\taccess->previous_end = access->ticket_end;\n"
ASK_NUMBER = "\taccess->ticket = access->ticket == UINT32_MAX ? 1 : access->ticket + 1;\n"
ASK_WAITING = "\taccess->ticket_end = ACCESS_TICKET_WAITING;\n"
ASK_KIND = "\taccess->asking = ask;\n"
ASK_UNTIL = "\taccess->asking_until_ms = after(access->clock_ms, ACCESS_CONFIRM_MS);\n"
ASK_RENEWS = "\taccess->open_until_ms = after(access->clock_ms, ACCESS_OPEN_MS);\n\treturn access->ticket;\n"
ASKING = "\treturn at(access, now_ms).asking;"
ASK_SECONDS = "\treturn state.asking != ACCESS_ASK_NONE ? seconds_rounded_up(state.asking_until_ms - state.clock_ms) : 0;"
CONFIRM = "\tsettle(access, now_ms);\n\tconfirmed = access->asking;\n\tend_question(access, ACCESS_TICKET_CONFIRMED);\n\treturn confirmed;\n"
REFUSE = "\tsettle(access, now_ms);\n\tend_question(access, ACCESS_TICKET_REFUSED);\n}"
TICKET_STATE = "\taccess_t state = at(access, now_ms);\n\n\t// While no ticket"
TICKET_LAST = "\tif(ticket == state.ticket) return state.ticket_end;\n"
TICKET_BEFORE = "\tif(ticket == (state.ticket == 1 ? UINT32_MAX : state.ticket - 1)) return state.previous_end;\n"
TICKET_UNKNOWN = "\treturn ACCESS_TICKET_UNKNOWN;"
RENEW = "access->open_until_ms = after(access->clock_ms, ACCESS_OPEN_MS);"

MUTATIONS = [
    # the time
    ("access_time_steps_back_with_the_caller", T, F, CLOCK, "\taccess->clock_ms = now_ms;\n"),
    ("access_time_stands_still", T, F, CLOCK, "\t(void)now_ms;\n"),
    ("access_asking_functions_ignore_their_time", T, F, AT, "\t(void)now_ms;\n\tsettle(&state, state.clock_ms);\n\treturn state;"),
    ("access_asking_functions_see_no_end", T, F, AT, "\t(void)now_ms;\n\treturn state;"),
    ("access_open_ignores_its_time", T, F, OPEN, OPEN.replace("\tsettle(access, now_ms);\n", "\t(void)now_ms;\n")),
    ("access_close_ignores_its_time", T, F, CLOSE, CLOSE.replace("\tsettle(access, now_ms);\n", "\t(void)now_ms;\n")),
    ("access_write_ignores_its_time", T, F, WRITE, WRITE.replace("\tsettle(access, now_ms);\n", "\t(void)now_ms;\n")),
    ("access_ask_ignores_its_time", T, F, ASK_SETTLE + ASK_STATE, "\t(void)now_ms;\n" + ASK_STATE),
    ("access_confirm_ignores_its_time", T, F, CONFIRM, CONFIRM.replace("\tsettle(access, now_ms);\n", "\t(void)now_ms;\n")),
    ("access_refuse_ignores_its_time", T, F, REFUSE, REFUSE.replace("\tsettle(access, now_ms);\n", "\t(void)now_ms;\n")),
    ("access_refused_change_does_not_take_its_time_over", T, F,
     "\tsettle(access, now_ms);\n\tif(!access->open) return false;\n", "\tif(!at(access, now_ms).open) return false;\n\tsettle(access, now_ms);\n"),
    ("access_refused_question_does_not_take_its_time_over", T, F,
     ASK_SETTLE + ASK_STATE + ASK_KINDS,
     "\tif(!at(access, now_ms).open || at(access, now_ms).asking != ACCESS_ASK_NONE) return 0;\n" + ASK_KINDS + ASK_SETTLE),
    ("access_question_that_is_none_does_not_take_its_time_over", T, F, ASK_SETTLE + ASK_STATE + ASK_KINDS, ASK_KINDS + ASK_SETTLE + ASK_STATE),

    # the largest time
    ("access_end_wraps_around", T, F, AFTER, "\treturn now + duration_ms;"),
    ("access_end_before_the_largest_time", T, F, AFTER, AFTER.replace("? UINT64_MAX :", "? UINT64_MAX - 1 :")),
    ("access_end_at_the_largest_time_too_early", T, F, AFTER, AFTER.replace("now > UINT64_MAX - duration_ms", "now >= UINT64_MAX - duration_ms - 1")),

    # access_init
    ("access_init_keeps_the_release", T, F, INIT,
     "\tbool open = access->open;\n\tuint64_t until = access->open_until_ms;\n\n" + INIT + "\n\taccess->open = open;\n\taccess->open_until_ms = until;"),
    ("access_init_keeps_the_time", T, F, INIT, "\tuint64_t clock = access->clock_ms;\n\n" + INIT + "\n\taccess->clock_ms = clock;"),
    ("access_init_keeps_the_question", T, F, INIT, "\taccess_ask_t asking = access->asking;\n\n" + INIT + "\n\taccess->asking = asking;"),
    ("access_init_keeps_the_ticket_number", T, F, INIT, "\tuint32_t ticket = access->ticket;\n\n" + INIT + "\n\taccess->ticket = ticket;"),
    ("access_init_keeps_the_end_of_the_last_ticket", T, F, INIT,
     "\taccess_ticket_t end = access->ticket_end;\n\n" + INIT + "\n\taccess->ticket_end = end;"),
    ("access_init_keeps_the_end_of_the_ticket_before", T, F, INIT,
     "\taccess_ticket_t end = access->previous_end;\n\n" + INIT + "\n\taccess->previous_end = end;"),

    # the release
    ("access_open_does_not_open", T, F, OPEN, OPEN.replace("\taccess->open = true;\n", "")),
    ("access_open_keeps_the_old_end", T, F, OPEN, OPEN.replace("\t" + RENEW + "\n", "")),
    ("access_open_again_does_not_start_the_time_anew", T, F, OPEN,
     "\tsettle(access, now_ms);\n\tif(!access->open) " + RENEW + "\n\taccess->open = true;\n"),
    ("access_open_time_1_ms_longer", T, H, "#define ACCESS_OPEN_MS      (600u * 1000u)", "#define ACCESS_OPEN_MS      (600u * 1000u + 1u)"),
    ("access_open_time_1_ms_shorter", T, H, "#define ACCESS_OPEN_MS      (600u * 1000u)", "#define ACCESS_OPEN_MS      (600u * 1000u - 1u)"),
    ("access_release_ends_1_ms_late", T, F, RELEASE_ENDS, RELEASE_ENDS.replace(">=", ">")),
    ("access_release_never_ends", T, F, RELEASE_ENDS, RELEASE_ENDS.replace("if(access->clock_ms", "if(0 && access->clock_ms")),
    ("access_release_end_leaves_it_open", T, F, RELEASE_ENDS, RELEASE_ENDS.replace("\t\taccess->open = false;\n", "")),
    ("access_close_leaves_it_open", T, F, CLOSE, CLOSE.replace("\taccess->open = false;\n", "")),
    ("access_is_open_sees_no_end", T, F, IS_OPEN, "\t(void)now_ms;\n\treturn access->open;"),
    ("access_seconds_rounded_down", T, F, SECONDS, SECONDS.replace("ms + 999", "ms")),
    ("access_seconds_rounded_to_nearest", T, F, SECONDS, SECONDS.replace("ms + 999", "ms + 500")),
    ("access_seconds_one_too_many_at_full_seconds", T, F, SECONDS, SECONDS.replace("ms + 999", "ms + 1000")),
    ("access_seconds_left_of_a_closed_release", T, F, SECONDS_LEFT, "\treturn seconds_rounded_up(state.open_until_ms - state.clock_ms);"),
    ("access_seconds_left_are_those_of_the_question", T, F, SECONDS_LEFT, SECONDS_LEFT.replace("state.open_until_ms", "state.asking_until_ms")),

    # a change
    ("access_write_while_closed", T, F, WRITE, WRITE.replace("\tif(!access->open) return false;\n\n", "")),
    ("access_write_does_not_renew", T, F, WRITE, WRITE.replace("\t" + RENEW + "\n", "")),
    ("access_write_reports_refused", T, F, WRITE, WRITE.replace("\treturn true;", "\treturn false;")),
    ("access_write_refused_while_a_question_waits", T, F,
     "\tif(!access->open) return false;\n\n", "\tif(!access->open || access->asking != ACCESS_ASK_NONE) return false;\n\n"),

    # a question
    ("access_ask_without_release", T, F, ASK_STATE, "\tif(access->asking != ACCESS_ASK_NONE) return 0;\n"),
    ("access_ask_replaces_the_question_that_waits", T, F, ASK_STATE, "\tif(!access->open) return 0;\n"),
    ("access_ask_none_is_a_question", T, F, ASK_KINDS, "\tif(ask > ACCESS_ASK_RESET) return 0;\n"),
    ("access_ask_values_behind_the_enum_are_questions", T, F, ASK_KINDS, "\tif(ask == ACCESS_ASK_NONE) return 0;\n"),
    ("access_ask_wifi_refused", T, F, ASK_KINDS, ASK_KINDS.replace("ask != ACCESS_ASK_WIFI && ", "")),
    ("access_ask_firmware_refused", T, F, ASK_KINDS, ASK_KINDS.replace("ask != ACCESS_ASK_FIRMWARE && ", "")),
    ("access_ask_reset_refused", T, F, ASK_KINDS, ASK_KINDS.replace(" && ask != ACCESS_ASK_RESET", "")),
    ("access_ask_does_not_renew", T, F, ASK_RENEWS, "\treturn access->ticket;\n"),
    ("access_refused_second_question_renews", T, F, ASK_STATE,
     "\tif(!access->open) return 0;\n\tif(access->asking != ACCESS_ASK_NONE)\n\t{\n\t\t" + RENEW + "\n\t\treturn 0;\n\t}\n"),
    ("access_question_that_is_none_renews", T, F, ASK_KINDS,
     ASK_KINDS.replace(" return 0;\n", "\n\t{\n\t\t" + RENEW + "\n\t\treturn 0;\n\t}\n")),
    ("access_confirm_time_1_ms_longer", T, H, "#define ACCESS_CONFIRM_MS   (60u * 1000u)", "#define ACCESS_CONFIRM_MS   (60u * 1000u + 1u)"),
    ("access_confirm_time_1_ms_shorter", T, H, "#define ACCESS_CONFIRM_MS   (60u * 1000u)", "#define ACCESS_CONFIRM_MS   (60u * 1000u - 1u)"),
    ("access_question_time_not_set", T, F, ASK_UNTIL, ""),
    ("access_question_waits_as_long_as_the_release", T, F, ASK_UNTIL, ASK_UNTIL.replace("ACCESS_CONFIRM_MS", "ACCESS_OPEN_MS")),
    ("access_question_kind_not_stored", T, F, ASK_KIND, "\taccess->asking = ACCESS_ASK_WIFI;\n"),
    ("access_ask_returns_no_ticket", T, F, ASK_RENEWS, ASK_RENEWS.replace("return access->ticket;", "return 0;")),

    # ticket numbers
    ("access_ticket_0_after_the_largest", T, F, ASK_NUMBER, "\taccess->ticket = access->ticket + 1;\n"),
    ("access_ticket_wraps_at_2_31", T, F, ASK_NUMBER, ASK_NUMBER.replace("== UINT32_MAX", ">= 0x7FFFFFFFu")),
    ("access_ticket_number_not_counted", T, F, ASK_NUMBER, ""),
    ("access_ticket_number_counted_by_two", T, F, ASK_NUMBER, ASK_NUMBER.replace("access->ticket + 1", "access->ticket + 2")),
    ("access_ticket_number_starts_again_at_1", T, F, ASK_NUMBER, "\taccess->ticket = 1;\n"),
    ("access_end_of_the_ticket_before_not_kept", T, F, ASK_PREVIOUS, ""),
    ("access_new_ticket_does_not_wait", T, F, ASK_WAITING, ""),

    # how a question ends by itself
    ("access_question_never_expires", T, F, EXPIRES, ""),
    ("access_question_expires_1_ms_late", T, F, EXPIRES, EXPIRES.replace("access->clock_ms >= access->asking_until_ms", "access->clock_ms > access->asking_until_ms")),
    ("access_question_that_ran_out_is_refused", T, F, EXPIRES, EXPIRES.replace("ACCESS_TICKET_EXPIRED", "ACCESS_TICKET_REFUSED")),
    ("access_question_expires_although_the_release_ended_first", T, F, EXPIRES, EXPIRES.replace(" && access->asking_until_ms <= access->open_until_ms", "")),
    ("access_question_refused_when_both_end_together", T, F, EXPIRES, EXPIRES.replace("access->asking_until_ms <= access->open_until_ms", "access->asking_until_ms < access->open_until_ms")),
    ("access_release_end_looked_at_first", T, F, EXPIRES + RELEASE_ENDS, RELEASE_ENDS + EXPIRES),
    ("access_release_end_keeps_the_question", T, F, RELEASE_ENDS, RELEASE_ENDS.replace("\t\tend_question(access, ACCESS_TICKET_REFUSED);\n", "")),
    ("access_release_end_lets_the_question_expire", T, F, RELEASE_ENDS, RELEASE_ENDS.replace("ACCESS_TICKET_REFUSED", "ACCESS_TICKET_EXPIRED")),
    ("access_ended_ticket_ends_again", T, F, WAITS, ""),
    ("access_ended_question_still_waits", T, F, ENDS, "\taccess->ticket_end = end;\n"),
    ("access_end_of_the_question_not_stored", T, F, ENDS, "\taccess->asking = ACCESS_ASK_NONE;\n\t(void)end;\n"),

    # switched off, confirmed, refused
    ("access_close_keeps_the_question", T, F, CLOSE, CLOSE.replace("\tend_question(access, ACCESS_TICKET_REFUSED);\n", "")),
    ("access_close_lets_the_question_expire", T, F, CLOSE, CLOSE.replace("ACCESS_TICKET_REFUSED", "ACCESS_TICKET_EXPIRED")),
    ("access_confirm_returns_nothing", T, F, CONFIRM, CONFIRM.replace("\treturn confirmed;", "\treturn confirmed == ACCESS_ASK_NONE ? confirmed : ACCESS_ASK_NONE;")),
    ("access_confirm_again_and_again", T, F, CONFIRM, CONFIRM.replace("\tend_question(access, ACCESS_TICKET_CONFIRMED);\n", "")),
    ("access_confirmed_ticket_is_refused", T, F, CONFIRM, CONFIRM.replace("ACCESS_TICKET_CONFIRMED", "ACCESS_TICKET_REFUSED")),
    ("access_confirm_renews_the_release", T, F, CONFIRM,
     CONFIRM.replace("\treturn confirmed;", "\tif(confirmed != ACCESS_ASK_NONE) " + RENEW + "\n\treturn confirmed;")),
    ("access_confirm_ends_the_release", T, F, CONFIRM, CONFIRM.replace("\treturn confirmed;", "\tif(confirmed != ACCESS_ASK_NONE) access->open = false;\n\treturn confirmed;")),
    ("access_refuse_does_nothing", T, F, REFUSE, "\tsettle(access, now_ms);\n}"),
    ("access_refused_ticket_has_expired", T, F, REFUSE, REFUSE.replace("ACCESS_TICKET_REFUSED", "ACCESS_TICKET_EXPIRED")),
    ("access_refuse_ends_the_release", T, F, REFUSE, REFUSE.replace("\n}", "\n\taccess->open = false;\n}")),
    ("access_refuse_renews_the_release", T, F, REFUSE, REFUSE.replace("\n}", "\n\t" + RENEW + "\n}")),

    # what the display shows
    ("access_asking_sees_no_end", T, F, ASKING, "\t(void)now_ms;\n\treturn access->asking;"),
    ("access_ask_seconds_left_while_nothing_waits", T, F, ASK_SECONDS, "\treturn seconds_rounded_up(state.asking_until_ms - state.clock_ms);"),
    ("access_ask_seconds_left_are_those_of_the_release", T, F, ASK_SECONDS, ASK_SECONDS.replace("state.asking_until_ms", "state.open_until_ms")),

    # access_ticket
    ("access_ticket_sees_no_end", T, F, TICKET_STATE, "\taccess_t state = *access;\n\n\t(void)now_ms;\n\t// While no ticket"),
    ("access_ticket_last_one_unknown", T, F, TICKET_LAST, ""),
    ("access_ticket_every_number_is_the_last", T, F, TICKET_LAST, "\tif(ticket != 0) return state.ticket_end;\n"),
    ("access_ticket_before_the_last_unknown", T, F, TICKET_BEFORE, ""),
    ("access_ticket_before_1_is_0", T, F, TICKET_BEFORE, "\tif(ticket == state.ticket - 1) return state.previous_end;\n"),
    ("access_ticket_two_before_the_last_known", T, F, TICKET_BEFORE,
     "\tif(ticket == (state.ticket == 1 ? UINT32_MAX : state.ticket - 1) || ticket == state.ticket - 2) return state.previous_end;\n"),
    ("access_ticket_before_the_last_answers_for_every_older", T, F, TICKET_UNKNOWN, "\treturn ticket < state.ticket ? state.previous_end : ACCESS_TICKET_UNKNOWN;"),
    ("access_ticket_unknown_is_expired", T, F, TICKET_UNKNOWN, "\treturn ACCESS_TICKET_EXPIRED;"),
    ("access_ticket_last_end_for_the_one_before", T, F, TICKET_BEFORE, TICKET_BEFORE.replace("state.previous_end", "state.ticket_end")),
]
