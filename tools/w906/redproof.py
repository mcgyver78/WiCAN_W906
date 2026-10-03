#!/usr/bin/env python3
"""Shows that the host tests really guard the rules they claim to guard.

A test that stays green when the rule is removed proves nothing. Every mutation below removes or
weakens one rule in the source; the test named with it must then FAIL in at least one check. The
run is red when

  - the unchanged source does not pass its test,
  - a mutation no longer applies (the text is not found exactly once) or does not compile,
  - a mutation leaves the test green,
  - a mutated test aborts without a failed check (a crash names no rule).

  python3 redproof.py              all mutations
  python3 redproof.py --only NAME  one mutation
  python3 redproof.py --list
  python3 redproof.py --selftest   counter-check of this script: it runs itself with a change
                                   without effect and with a mutation that does not apply and
                                   expects exit status 1, and with a real mutation and expects 0
"""
import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[2]
WORKFLOW = ".github/workflows/w906-tools.yml"

# Run in tools/w906. The workflow has to contain the same command text (checked below); apart from
# that this script builds and runs the unchanged test itself before it mutates anything.
TESTS = {
    "dtc_state": {
        "build": "cc -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all "
                 "-I../../main ../../main/dtc_state.c dtc_state_test.c -o dtc_state_test",
        "run": "./dtc_state_test",
    },
}

STATE_C = "main/dtc_state.c"
STATE_H = "main/dtc_state.h"

BOUND = "\tif(clear && src == DTC_SRC_HTTP)"
NOT_DONE = "\t\tif(s->phase != DTC_STATE_DONE) return DTC_REJECT_READ_REQUIRED;"
WAS_CLEAR = "\t\tif(s->clear) return DTC_REJECT_READ_REQUIRED;"
TOO_OLD = "\t\tif(elapsed_ms(now_ms, s->finished_ms) > DTC_CLEAR_MAX_AGE_MS) return DTC_REJECT_READ_REQUIRED;"
STALE = "\t\tif(seq != s->seq) return DTC_REJECT_STALE_SEQ;"
EMPTY = "\t\tif(s->result_count == 0) return DTC_REJECT_NOTHING_TO_CLEAR;"
EXPIRED = "\tif(s->src == DTC_SRC_HTTP && elapsed_ms(now_ms, s->queued_ms) > DTC_HTTP_EXPIRY_MS)"
NOT_QUEUED = "\tif(s->phase != DTC_STATE_QUEUED) return false;"
NOT_RUNNING = "\tif(s->phase != DTC_STATE_RUNNING) return;\n\n"
PROGRESS = "\ts->step = step;"
ERROR = "\tfinish(s, DTC_STATE_ERROR, reason, now_ms);"
DONE = "\tfinish(s, DTC_STATE_DONE, NULL, now_ms);"
STORE = "\ts->result_seq = s->seq;\n\ts->result_count = dtc_count;\n"
ELAPSED = "\treturn now_ms >= then_ms ? now_ms - then_ms : 0;"
CONTROL = "\t\tif(c < 0x20) continue;"
FINISHED = "\tbool finished = s->phase == DTC_STATE_DONE || s->phase == DTC_STATE_ERROR;"
EXPIRE = '\t\tfinish(s, DTC_STATE_ERROR, "expired", now_ms);\n'


def also(phase):
    return "\tif(s->phase != DTC_STATE_RUNNING && s->phase != %s) return;\n\n" % phase


# (name, test, file, text in the source, replacement)
MUTATIONS = [
    # Accepting a request
    ("busy_not_checked", "dtc_state", STATE_C,
     "\tif(dtc_state_busy(s)) return DTC_REJECT_BUSY;",
     "\tif(0 && dtc_state_busy(s)) return DTC_REJECT_BUSY;"),
    ("http_clear_unbound", "dtc_state", STATE_C, BOUND,
     "\tif(0 && clear && src == DTC_SRC_HTTP)"),
    ("mqtt_clear_bound_too", "dtc_state", STATE_C, BOUND,
     "\tif(clear)"),
    ("read_bound_too", "dtc_state", STATE_C, BOUND,
     "\tif(src == DTC_SRC_HTTP)"),
    ("clear_without_finished_read", "dtc_state", STATE_C, NOT_DONE,
     NOT_DONE.replace("if(", "if(0 && ")),
    ("clear_after_clear", "dtc_state", STATE_C, WAS_CLEAR,
     WAS_CLEAR.replace("if(", "if(0 && ")),
    ("clear_after_mqtt_clear", "dtc_state", STATE_C, WAS_CLEAR,
     WAS_CLEAR.replace("if(s->clear)", "if(s->clear && s->src == DTC_SRC_HTTP)")),
    ("clear_age_not_checked", "dtc_state", STATE_C, TOO_OLD,
     TOO_OLD.replace("if(", "if(0 && ")),
    ("clear_age_boundary", "dtc_state", STATE_C, TOO_OLD,
     TOO_OLD.replace(" > DTC_CLEAR_MAX_AGE_MS", " >= DTC_CLEAR_MAX_AGE_MS")),
    ("clear_age_only_after_http_read", "dtc_state", STATE_C, TOO_OLD,
     TOO_OLD.replace("if(", "if(s->src == DTC_SRC_HTTP && ")),
    ("clear_seq_not_compared", "dtc_state", STATE_C, STALE,
     STALE.replace("if(", "if(0 && ")),
    ("clear_seq_low_bits_only", "dtc_state", STATE_C, STALE,
     STALE.replace("seq != s->seq", "(seq & 0xFFFFFFu) != (s->seq & 0xFFFFFFu)")),
    ("clear_seq_only_after_http_read", "dtc_state", STATE_C, STALE,
     STALE.replace("if(", "if(s->src == DTC_SRC_HTTP && ")),
    ("clear_of_empty_list", "dtc_state", STATE_C, EMPTY,
     EMPTY.replace("if(", "if(0 && ")),
    ("clear_of_single_code_refused", "dtc_state", STATE_C, EMPTY,
     EMPTY.replace("== 0", "< 2")),
    ("clear_of_empty_list_after_mqtt_read", "dtc_state", STATE_C, EMPTY,
     EMPTY.replace("if(", "if(s->src == DTC_SRC_HTTP && ")),
    ("reason_order_seq_before_age", "dtc_state", STATE_C,
     TOO_OLD + "\n" + STALE, STALE + "\n" + TOO_OLD),
    ("reason_order_count_before_seq", "dtc_state", STATE_C,
     STALE + "\n" + EMPTY, EMPTY + "\n" + STALE),
    ("seq_out_not_set_on_rejection", "dtc_state", STATE_C,
     "\tif(seq_out != NULL) *seq_out = s->seq;\n\n\t// Commands during a scan",
     "\t// Commands during a scan"),
    ("request_keeps_old_step", "dtc_state", STATE_C, "\ts->step = 0;\n", ""),
    ("request_keeps_old_total", "dtc_state", STATE_C, "\ts->total = 0;\n", ""),
    ("request_keeps_old_reason", "dtc_state", STATE_C,
     "\ts->total = 0;\n\ts->reason = NULL;\n", "\ts->total = 0;\n"),
    ("action_not_stored", "dtc_state", STATE_C, "\ts->clear = clear;\n", ""),
    ("source_not_stored", "dtc_state", STATE_C, "\ts->src = src;\n", ""),

    # Sequence numbers
    ("seq_not_masked", "dtc_state", STATE_C,
     "\ts->next_seq = seed & DTC_SEQ_MAX;", "\ts->next_seq = seed;"),
    ("seq_zero_at_boot", "dtc_state", STATE_C,
     "\tif(s->next_seq == 0) s->next_seq = 1;", "\tif(0 && s->next_seq == 0) s->next_seq = 1;"),
    ("seq_no_wrap", "dtc_state", STATE_C,
     "\treturn seq >= DTC_SEQ_MAX ? 1 : seq + 1;", "\treturn seq + 1;"),
    ("seq_max_changed", "dtc_state", STATE_H,
     "#define DTC_SEQ_MAX             0x7FFFFFFFu", "#define DTC_SEQ_MAX             0xFFFFFFFFu"),

    # Limits and time
    ("clear_age_limit_changed", "dtc_state", STATE_H,
     "#define DTC_CLEAR_MAX_AGE_MS    (600u * 1000u)", "#define DTC_CLEAR_MAX_AGE_MS    (601u * 1000u)"),
    ("http_expiry_limit_changed", "dtc_state", STATE_H,
     "#define DTC_HTTP_EXPIRY_MS      (20u * 1000u)", "#define DTC_HTTP_EXPIRY_MS      (21u * 1000u)"),
    ("older_clock_counts_as_ages", "dtc_state", STATE_C, ELAPSED,
     "\treturn now_ms - then_ms;"),
    ("time_in_32_bit", "dtc_state", STATE_C, ELAPSED,
     "\treturn now_ms >= then_ms ? (uint32_t)(now_ms - then_ms) : 0;"),

    # Pickup and expiry
    ("http_request_never_expires", "dtc_state", STATE_C, EXPIRED,
     EXPIRED.replace("if(", "if(0 && ")),
    ("expiry_boundary", "dtc_state", STATE_C, EXPIRED,
     EXPIRED.replace(" > DTC_HTTP_EXPIRY_MS", " >= DTC_HTTP_EXPIRY_MS")),
    ("mqtt_request_expires_too", "dtc_state", STATE_C, EXPIRED,
     EXPIRED.replace("s->src == DTC_SRC_HTTP", "(s->src == DTC_SRC_HTTP || true)")),
    ("expiry_text_changed", "dtc_state", STATE_C, '"expired"', '"timeout"'),
    ("expiry_drops_result_seq", "dtc_state", STATE_C, EXPIRE, EXPIRE + "\t\ts->result_seq = 0;\n"),
    ("expiry_drops_result_count", "dtc_state", STATE_C, EXPIRE, EXPIRE + "\t\ts->result_count = 0;\n"),
    ("pickup_without_request", "dtc_state", STATE_C, NOT_QUEUED,
     NOT_QUEUED.replace("if(", "if(0 && ")),
    ("pickup_again_after_error", "dtc_state", STATE_C, NOT_QUEUED,
     "\tif(s->phase != DTC_STATE_QUEUED && s->phase != DTC_STATE_ERROR) return false;"),
    ("pickup_while_running", "dtc_state", STATE_C, NOT_QUEUED,
     "\tif(s->phase != DTC_STATE_QUEUED && s->phase != DTC_STATE_RUNNING) return false;"),
    ("pickup_does_not_start", "dtc_state", STATE_C,
     "\ts->phase = DTC_STATE_RUNNING;\n\treturn true;", "\treturn true;"),

    # Progress, error and done belong to a running scan
    ("progress_in_any_phase", "dtc_state", STATE_C, NOT_RUNNING + PROGRESS, PROGRESS),
    ("progress_while_queued", "dtc_state", STATE_C, NOT_RUNNING + PROGRESS, also("DTC_STATE_QUEUED") + PROGRESS),
    ("progress_after_done", "dtc_state", STATE_C, NOT_RUNNING + PROGRESS, also("DTC_STATE_DONE") + PROGRESS),
    ("progress_after_error", "dtc_state", STATE_C, NOT_RUNNING + PROGRESS, also("DTC_STATE_ERROR") + PROGRESS),
    ("error_in_any_phase", "dtc_state", STATE_C, NOT_RUNNING + ERROR, ERROR),
    ("error_while_queued", "dtc_state", STATE_C, NOT_RUNNING + ERROR, also("DTC_STATE_QUEUED") + ERROR),
    ("error_after_error", "dtc_state", STATE_C, NOT_RUNNING + ERROR, also("DTC_STATE_ERROR") + ERROR),
    ("error_after_done", "dtc_state", STATE_C, NOT_RUNNING + ERROR, also("DTC_STATE_DONE") + ERROR),
    ("done_in_any_phase", "dtc_state", STATE_C, NOT_RUNNING + DONE, DONE),
    ("done_while_queued", "dtc_state", STATE_C, NOT_RUNNING + DONE, also("DTC_STATE_QUEUED") + DONE),
    ("done_after_error", "dtc_state", STATE_C, NOT_RUNNING + DONE, also("DTC_STATE_ERROR") + DONE),
    ("done_after_done", "dtc_state", STATE_C, NOT_RUNNING + DONE, also("DTC_STATE_DONE") + DONE),
    ("finish_keeps_name", "dtc_state", STATE_C,
     "\ts->reason = reason;\n\ts->name = NULL;\n", "\ts->reason = reason;\n"),
    ("error_keeps_name", "dtc_state", STATE_C,
     "\ts->name = NULL;\n\ts->finished_ms = now_ms;",
     "\tif(phase == DTC_STATE_DONE) s->name = NULL;\n\ts->finished_ms = now_ms;"),
    ("result_seq_not_stored", "dtc_state", STATE_C, "\ts->result_seq = s->seq;\n", ""),
    ("result_count_not_stored", "dtc_state", STATE_C,
     "\ts->result_count = dtc_count;\n", "\t(void)dtc_count;\n"),
    ("result_count_in_8_bit", "dtc_state", STATE_C,
     "\ts->result_count = dtc_count;\n", "\ts->result_count = (uint8_t)dtc_count;\n"),
    ("result_of_a_clear_not_stored", "dtc_state", STATE_C, STORE,
     "\tif(!s->clear)\n\t{\n\t" + STORE.replace("\n\t", "\n\t\t") + "\t}\n"),
    ("init_does_not_reset", "dtc_state", STATE_C, "\tmemset(s, 0, sizeof(*s));\n", ""),

    # JSON
    ("json_truncated_silently", "dtc_state", STATE_C, "\t\tout->overflow = true;\n", ""),
    ("json_no_room_for_zero", "dtc_state", STATE_C,
     "\tif(out->len + 1 >= out->size)", "\tif(out->len >= out->size)"),
    ("json_no_terminating_zero", "dtc_state", STATE_C, "\tbuf[out.len] = '\\0';\n", ""),
    ("json_not_escaped", "dtc_state", STATE_C,
     "\t\tif(c == '\"' || c == '\\\\') put_char(out, '\\\\');\n", ""),
    ("json_control_characters", "dtc_state", STATE_C, CONTROL + "\n", ""),
    ("json_control_boundary", "dtc_state", STATE_C, CONTROL, "\t\tif(c < 0x1f) continue;"),
    ("json_utf8_dropped", "dtc_state", STATE_C, CONTROL, "\t\tif(c < 0x20 || c >= 0x80) continue;"),
    ("json_reason_not_escaped", "dtc_state", STATE_C,
     "\tput_escaped(&out, s->reason);", "\tput_raw(&out, s->reason != NULL ? s->reason : \"\");"),
    ("json_number_in_16_bit", "dtc_state", STATE_C,
     '"%" PRIu32, value);', '"%" PRIu32, value & 0xFFFFu);'),
    ("json_count_in_8_bit", "dtc_state", STATE_C,
     "\tput_number(&out, s->result_count);", "\tput_number(&out, (uint8_t)s->result_count);"),
    ("json_age_divided_by_1024", "dtc_state", STATE_C, "/ 1000u) : 0);", "/ 1024u) : 0);"),
    ("json_age_while_not_finished", "dtc_state", STATE_C, FINISHED, "\tbool finished = true;"),
    ("json_no_age_after_error", "dtc_state", STATE_C, FINISHED,
     "\tbool finished = s->phase == DTC_STATE_DONE;"),
    ("json_action_without_request", "dtc_state", STATE_C,
     "\tbool requested = s->seq != 0;", "\tbool requested = true;"),
    ("json_field_left_out", "dtc_state", STATE_C,
     "\tput_raw(&out, \",\\\"count\\\":\");\n\tput_number(&out, s->result_count);\n", ""),
    ("reject_text_changed", "dtc_state", STATE_C, 'return "stale_seq";', 'return "stale";'),
]

# Found by two review rounds as changes the test of that time did not notice
FOUND_BY_REVIEW = [
    ('queued_field_in_32_bit', 'dtc_state', STATE_H,
     '\tuint64_t queued_ms;',
     '\tuint32_t queued_ms;'),
    ('finished_field_in_32_bit', 'dtc_state', STATE_H,
     '\tuint64_t finished_ms;',
     '\tuint32_t finished_ms;'),
    ('error_time_in_32_bit', 'dtc_state', STATE_C,
     '\tfinish(s, DTC_STATE_ERROR, reason, now_ms);',
     '\tfinish(s, DTC_STATE_ERROR, reason, (uint32_t)now_ms);'),
    ('expiry_time_in_32_bit', 'dtc_state', STATE_C,
     '\t\tfinish(s, DTC_STATE_ERROR, "expired", now_ms);',
     '\t\tfinish(s, DTC_STATE_ERROR, "expired", (uint32_t)now_ms);'),
    ('json_writes_behind_the_buffer', 'dtc_state', STATE_C,
     '\t\tout->overflow = true;\n\t\treturn;\n',
     '\t\tout->overflow = true;\n'),
    ('json_1_byte_buffer_not_emptied', 'dtc_state', STATE_C,
     '\tif(size == 0) return -1;',
     '\tif(size <= 1) return -1;'),
    ('seq_after_in_16_bit', 'dtc_state', STATE_C,
     'static uint32_t seq_after(uint32_t seq)',
     'static uint16_t seq_after(uint32_t seq)'),
    ('seq_wraps_one_early', 'dtc_state', STATE_C,
     '\treturn seq >= DTC_SEQ_MAX ? 1 : seq + 1;',
     '\treturn seq >= DTC_SEQ_MAX - 1 ? 1 : seq + 1;'),
    ('clear_seq_bit_31_ignored', 'dtc_state', STATE_C,
     '\t\tif(seq != s->seq) return DTC_REJECT_STALE_SEQ;',
     '\t\tif((seq & DTC_SEQ_MAX) != s->seq) return DTC_REJECT_STALE_SEQ;'),
    ('json_action_missing_for_seq_1', 'dtc_state', STATE_C,
     '\tbool requested = s->seq != 0;',
     '\tbool requested = s->seq > 1;'),
    ('json_age_while_idle', 'dtc_state', STATE_C,
     '\tbool finished = s->phase == DTC_STATE_DONE || s->phase == DTC_STATE_ERROR;',
     '\tbool finished = s->phase == DTC_STATE_DONE || s->phase == DTC_STATE_ERROR || s->phase == DTC_STATE_IDLE;'),
    ('mqtt_clear_expires', 'dtc_state', STATE_C,
     '\tif(s->src == DTC_SRC_HTTP && elapsed_ms(now_ms, s->queued_ms) > DTC_HTTP_EXPIRY_MS)',
     '\tif((s->src == DTC_SRC_HTTP || s->clear) && elapsed_ms(now_ms, s->queued_ms) > DTC_HTTP_EXPIRY_MS)'),
    ('empty_read_keeps_old_result_seq', 'dtc_state', STATE_C,
     '\ts->result_seq = s->seq;',
     '\tif(dtc_count != 0) s->result_seq = s->seq;'),
    ('json_length_in_8_bit', 'dtc_state', STATE_C,
     '\tsize_t len;',
     '\tuint8_t len;'),
    ('json_escape_character_kept', 'dtc_state', STATE_C,
     '\t\tif(c < 0x20) continue;',
     '\t\tif(c < 0x20 && c != 0x1b) continue;'),
    ('reason_after_clear_without_codes', 'dtc_state', STATE_C,
     '\t\tif(s->clear) return DTC_REJECT_READ_REQUIRED;',
     '\t\tif(s->clear && s->result_count != 0) return DTC_REJECT_READ_REQUIRED;'),
    ('error_resets_step', 'dtc_state', STATE_C,
     '\tfinish(s, DTC_STATE_ERROR, reason, now_ms);',
     '\tfinish(s, DTC_STATE_ERROR, reason, now_ms);\n\ts->step = 0;'),
    ('clear_of_256_codes_refused', 'dtc_state', STATE_C,
     '\t\tif(s->result_count == 0) return DTC_REJECT_NOTHING_TO_CLEAR;',
     '\t\tif((uint8_t)s->result_count == 0) return DTC_REJECT_NOTHING_TO_CLEAR;'),
    ('mqtt_during_http_scan_accepted', 'dtc_state', STATE_C,
     '\tif(dtc_state_busy(s)) return DTC_REJECT_BUSY;',
     '\tif(dtc_state_busy(s) && !(s->src == DTC_SRC_HTTP && src == DTC_SRC_MQTT)) return DTC_REJECT_BUSY;'),
    ('read_during_clear_accepted', 'dtc_state', STATE_C,
     '\tif(dtc_state_busy(s)) return DTC_REJECT_BUSY;',
     '\tif(dtc_state_busy(s) && (clear || !s->clear)) return DTC_REJECT_BUSY;'),
    ('busy_resets_progress', 'dtc_state', STATE_C,
     '\tif(dtc_state_busy(s)) return DTC_REJECT_BUSY;',
     '\ts->step = 0;\n\ts->total = 0;\n\tif(dtc_state_busy(s)) return DTC_REJECT_BUSY;'),
    ('unbound_clear_of_empty_list_refused', 'dtc_state', STATE_C,
     '\t\tif(s->result_count == 0) return DTC_REJECT_NOTHING_TO_CLEAR;\n\t}\n',
     '\t}\n\tif(clear && s->phase == DTC_STATE_DONE && s->result_count == 0) return DTC_REJECT_NOTHING_TO_CLEAR;\n'),
    ('queued_time_in_32_bit', 'dtc_state', STATE_C,
     '\ts->queued_ms = now_ms;',
     '\ts->queued_ms = (uint32_t)now_ms;'),
    ('finished_time_in_32_bit', 'dtc_state', STATE_C,
     '\ts->finished_ms = now_ms;',
     '\ts->finished_ms = (uint32_t)now_ms;'),
    ('empty_result_keeps_old_count', 'dtc_state', STATE_C,
     '\ts->result_count = dtc_count;\n',
     '\tif(dtc_count != 0) s->result_count = dtc_count;\n'),
    ('failed_read_drops_result', 'dtc_state', STATE_C,
     '\tfinish(s, DTC_STATE_ERROR, reason, now_ms);',
     '\tfinish(s, DTC_STATE_ERROR, reason, now_ms);\n\tif(!s->clear)\n\t{\n\t\ts->result_seq = 0;\n\t\ts->result_count = 0;\n\t}'),
    ('seq_wraps_at_16_bit', 'dtc_state', STATE_C,
     '\treturn seq >= DTC_SEQ_MAX ? 1 : seq + 1;',
     '\treturn seq >= 0xFFFFu ? 1 : seq + 1;'),
    ('seq_repeats_after_error', 'dtc_state', STATE_C,
     '\ts->next_seq = seq_after(s->next_seq);',
     '\tif(s->phase != DTC_STATE_ERROR) s->next_seq = seq_after(s->next_seq);'),
    ('seq_out_only_busy_or_done', 'dtc_state', STATE_C,
     '\tif(seq_out != NULL) *seq_out = s->seq;\n\n\t// Commands during a scan',
     '\tif(seq_out != NULL && (dtc_state_busy(s) || s->phase == DTC_STATE_DONE)) *seq_out = s->seq;\n\n\t// Commands during a scan'),
    ('read_keeps_old_step', 'dtc_state', STATE_C,
     '\ts->step = 0;\n',
     '\tif(clear) s->step = 0;\n'),
    ('clear_keeps_old_reason', 'dtc_state', STATE_C,
     '\ts->total = 0;\n\ts->reason = NULL;\n',
     '\ts->total = 0;\n\tif(!clear) s->reason = NULL;\n'),
    ('clear_picked_up_again', 'dtc_state', STATE_C,
     '\tif(s->phase != DTC_STATE_QUEUED) return false;',
     '\tif(!s->clear && s->phase != DTC_STATE_QUEUED) return false;'),
    ('progress_of_queued_clear', 'dtc_state', STATE_C,
     '\tif(s->phase != DTC_STATE_RUNNING) return;\n\n\ts->step = step;',
     '\tif(!s->clear && s->phase != DTC_STATE_RUNNING) return;\n\n\ts->step = step;'),
    ('error_of_queued_clear', 'dtc_state', STATE_C,
     '\tif(s->phase != DTC_STATE_RUNNING) return;\n\n\tfinish(s, DTC_STATE_ERROR, reason, now_ms);',
     '\tif(!s->clear && s->phase != DTC_STATE_RUNNING) return;\n\n\tfinish(s, DTC_STATE_ERROR, reason, now_ms);'),
    ('done_of_queued_clear', 'dtc_state', STATE_C,
     '\tif(s->phase != DTC_STATE_RUNNING) return;\n\n\tfinish(s, DTC_STATE_DONE, NULL, now_ms);',
     '\tif(!s->clear && s->phase != DTC_STATE_RUNNING) return;\n\n\tfinish(s, DTC_STATE_DONE, NULL, now_ms);'),
    ('progress_null_keeps_name', 'dtc_state', STATE_C,
     '\ts->name = name;',
     '\tif(name != NULL) s->name = name;'),
    ('json_age_in_idle', 'dtc_state', STATE_C,
     '\tbool finished = s->phase == DTC_STATE_DONE || s->phase == DTC_STATE_ERROR;',
     '\tbool finished = !dtc_state_busy(s);'),
    ('refused_clear_drops_reason', 'dtc_state', STATE_C,
     '\tif(clear && src == DTC_SRC_HTTP)',
     '\ts->reason = NULL;\n\tif(clear && src == DTC_SRC_HTTP)'),
    ('refused_clear_overwrites_source', 'dtc_state', STATE_C,
     '\tif(clear && src == DTC_SRC_HTTP)',
     '\ts->src = src;\n\tif(clear && src == DTC_SRC_HTTP)'),
    ('unbound_clear_age_checked', 'dtc_state', STATE_C,
     '\tif(clear && src == DTC_SRC_HTTP)',
     '\tif(clear && elapsed_ms(now_ms, s->finished_ms) > DTC_CLEAR_MAX_AGE_MS) return DTC_REJECT_READ_REQUIRED;\n\tif(clear && src == DTC_SRC_HTTP)'),
    ('unbound_clear_after_clear_refused', 'dtc_state', STATE_C,
     '\tif(clear && src == DTC_SRC_HTTP)',
     '\tif(clear && s->clear) return DTC_REJECT_READ_REQUIRED;\n\tif(clear && src == DTC_SRC_HTTP)'),
    ('unbound_clear_after_error_refused', 'dtc_state', STATE_C,
     '\tif(clear && src == DTC_SRC_HTTP)',
     '\tif(clear && (src == DTC_SRC_HTTP || s->phase == DTC_STATE_ERROR))'),
]
MUTATIONS += FOUND_BY_REVIEW

# For --selftest only
SELFTEST_MUTATIONS = {
    "noop": ("selftest_without_effect", "dtc_state", STATE_C,
             '#include "dtc_state.h"\n', '#include "dtc_state.h"\n// change without effect\n'),
    "missing": ("selftest_text_not_found", "dtc_state", STATE_C,
                "this text is not in the source", ""),
}


def shell(command, cwd):
    # A mutated JSON writer may print bytes that are not UTF-8
    return subprocess.run(command, shell=True, cwd=cwd, capture_output=True,
                          encoding="utf-8", errors="replace")


def copy_sources(target):
    """The files a host test needs, at the same relative paths."""
    (target / "main").mkdir(parents=True)
    for source in sorted((REPO / "main").glob("dtc_*.[ch]")):
        shutil.copy(source, target / "main" / source.name)
    shutil.copytree(REPO / "tools" / "w906", target / "tools" / "w906",
                    ignore=shutil.ignore_patterns("__pycache__", "*_test"))


def run_test(tree, test):
    """Returns (state, detail) with state 'green', 'red', 'aborted' or 'broken'."""
    cwd = tree / "tools" / "w906"
    build = shell(TESTS[test]["build"], cwd)
    if build.returncode != 0:
        return "broken", "does not compile:\n" + build.stderr.strip()
    run = shell(TESTS[test]["run"], cwd)
    failed = [line[5:] for line in run.stdout.splitlines() if line.startswith("FAIL ")]
    if run.returncode == 0:
        if failed:
            return "broken", "the test printed FAIL but exited with 0"
        return "green", ""
    if failed:
        return "red", "%d checks failed, first: %s" % (len(failed), failed[0])
    last = (run.stderr.strip().splitlines() or run.stdout.strip().splitlines() or ["no output"])[-1]
    return "aborted", "exit %d without a failed check: %s" % (run.returncode, last)


def mutate(tree, mutation):
    """Applies one mutation. Returns an error text or None."""
    _, _, file, old, new = mutation
    path = tree / file
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        return "does not apply: the text occurs %d times in %s, expected once" % (count, file)
    path.write_text(text.replace(old, new), encoding="utf-8")
    return None


def evaluate(mutation, test=None):
    """Returns (state, detail) of the test after the mutation, in a fresh copy of the sources.
    Without a mutation the unchanged sources are tested with `test`."""
    with tempfile.TemporaryDirectory(prefix="redproof-") as directory:
        tree = pathlib.Path(directory)
        copy_sources(tree)
        if mutation is not None:
            error = mutate(tree, mutation)
            if error is not None:
                return "broken", error
        return run_test(tree, mutation[1] if mutation is not None else test)


def check_workflow():
    """The workflow has to contain the command text used here."""
    path = REPO / WORKFLOW
    if not path.exists():
        return ["%s not found" % WORKFLOW]
    text = path.read_text(encoding="utf-8")
    problems = []
    for name, test in TESTS.items():
        command = "%s && %s" % (test["build"], test["run"])
        if command not in text:
            problems.append("%s does not contain the command of the %s test used here:\n  %s"
                            % (WORKFLOW, name, command))
    return problems


def report(mutations):
    """Runs the mutations, returns the number of problems."""
    problems = 0
    for mutation in mutations:
        state, detail = evaluate(mutation)
        if state == "red":
            print("RED     %-36s %s" % (mutation[0], detail))
        elif state == "green":
            print("GREEN   %-36s the test does not notice this change" % mutation[0])
            problems += 1
        elif state == "aborted":
            print("ABORTED %-36s %s" % (mutation[0], detail))
            problems += 1
        else:
            print("BROKEN  %-36s %s" % (mutation[0], detail))
            problems += 1
    return problems


def selftest():
    """Runs this script as the CI does and looks at its exit status."""
    cases = [
        (["--selftest-mutation", "noop"], 1, "GREEN   selftest_without_effect",
         "a change without effect must make the run fail"),
        (["--selftest-mutation", "missing"], 1, "BROKEN  selftest_text_not_found",
         "a mutation that does not apply must make the run fail"),
        (["--only", MUTATIONS[0][0]], 0, "RED     " + MUTATIONS[0][0],
         "a removed rule must be reported red and the run must pass"),
    ]
    ok = True
    for arguments, status, line, rule in cases:
        run = subprocess.run([sys.executable, str(pathlib.Path(__file__).resolve())] + arguments,
                             capture_output=True, encoding="utf-8", errors="replace")
        good = run.returncode == status and line in run.stdout
        print("%s exit %d, expected %d and the line '%s': %s"
              % ("PASS" if good else "FAIL", run.returncode, status, line.strip(), rule))
        if not good:
            print(run.stdout + run.stderr)
            ok = False
    print("selftest %s" % ("OK" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--only", metavar="NAME", help="run a single mutation")
    parser.add_argument("--list", action="store_true", help="list the mutations")
    parser.add_argument("--selftest", action="store_true", help="counter-check of this script")
    parser.add_argument("--selftest-mutation", choices=sorted(SELFTEST_MUTATIONS), help=argparse.SUPPRESS)
    arguments = parser.parse_args()

    if arguments.list:
        for mutation in MUTATIONS:
            print(mutation[0])
        return 0

    names = [mutation[0] for mutation in MUTATIONS]
    if len(set(names)) != len(names):
        print("duplicate mutation names")
        return 1

    if arguments.selftest:
        return selftest()

    for problem in check_workflow():
        print(problem)
        return 1

    for test in TESTS:
        state, detail = evaluate(None, test)
        if state != "green":
            print("the unchanged %s test is not green: %s" % (test, detail))
            return 1
    print("unchanged sources: green")

    mutations = MUTATIONS
    if arguments.selftest_mutation:
        mutations = [SELFTEST_MUTATIONS[arguments.selftest_mutation]]
    elif arguments.only:
        mutations = [mutation for mutation in MUTATIONS if mutation[0] == arguments.only]
        if not mutations:
            print("no mutation named %s" % arguments.only)
            return 1

    problems = report(mutations)
    print("%d mutations, %d red, %d problems" % (len(mutations), len(mutations) - problems, problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
