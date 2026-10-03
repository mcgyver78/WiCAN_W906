#!/usr/bin/env python3
"""Shows that the host tests really guard the rules they claim to guard.

A test that stays green when the rule is removed proves nothing. Every mutation below removes or
weakens one rule in the source (a few change an example in API.md or in a fixture instead, which
the test has to compare); the test named with it must then FAIL in at least one check. The
run is red when

  - the unchanged source does not pass its test,
  - a mutation no longer applies (the text is not found exactly once) or does not compile,
  - a mutation leaves the test green,
  - a mutated test aborts without a failed check (a crash names no rule),
  - a mutated test does not end (an endless loop names no rule either; it is stopped after
    --timeout seconds instead of blocking the CI job for hours).

  python3 redproof.py              all mutations
  python3 redproof.py --only NAME  one mutation
  python3 redproof.py --list
  python3 redproof.py --selftest   counter-check of this script: it runs itself with a change
                                   without effect, with a mutation that does not apply and with
                                   one that makes the test hang and expects exit status 1, and
                                   with a real mutation and expects 0
"""
import argparse
import concurrent.futures
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[2]
WORKFLOW = ".github/workflows/w906-tools.yml"

# Seconds a build or a test may take. The tests end within a second or two; the limit is only there for
# a mutation that turns a loop into an endless one.
TIMEOUT_S = 120

# Run in tools/w906. The workflow has to contain the same command text (checked below); apart from
# that this script builds and runs the unchanged test itself before it mutates anything.
TESTS = {
    "dtc_state": {
        "build": "cc -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all "
                 "-I../../main ../../main/dtc_state.c dtc_state_test.c -o dtc_state_test",
        "run": "./dtc_state_test",
    },
    "dtc_api": {
        "build": "cc -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all "
                 "-I../../main ../../main/dtc_api.c dtc_api_test.c -o dtc_api_test",
        "run": "./dtc_api_test",
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
    # (the type change "size_t len" -> "uint8_t len" does the same, but gcc rejects it: -Wsign-compare)
    ('json_length_in_8_bit', 'dtc_state', STATE_C,
     '\tout->buf[out->len++] = c;',
     '\tout->buf[out->len++ & 0xFFu] = c;'),
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

# Found by the third review round: inputs the walk and the examples did not produce (clock at 0 and
# far behind, seq_out NULL, every byte value, buffers of 64 KiB, crashes and a hang in the module).
# Most of them are noticed by the walk only, so they also show when the walk is weakened.
FOUND_BY_REVIEW_3 = [
    ('time_finished_at_0_is_no_read', 'dtc_state', STATE_C,
     '\t\tif(s->phase != DTC_STATE_DONE) return DTC_REJECT_READ_REQUIRED;',
     '\t\tif(s->phase != DTC_STATE_DONE || s->finished_ms == 0) return DTC_REJECT_READ_REQUIRED;'),
    ('time_queued_at_0_not_busy', 'dtc_state', STATE_C,
     '\tif(dtc_state_busy(s)) return DTC_REJECT_BUSY;',
     '\tif(dtc_state_busy(s) && s->queued_ms != 0) return DTC_REJECT_BUSY;'),
    ('time_clock_before_queue_time', 'dtc_state', STATE_C,
     '\t\tif(s->clear) return DTC_REJECT_READ_REQUIRED;',
     '\t\tif(s->clear || now_ms < s->queued_ms) return DTC_REJECT_READ_REQUIRED;'),
    ('request_without_seq_out_keeps_step', 'dtc_state', STATE_C,
     '\ts->step = 0;\n',
     '\tif(seq_out != NULL) s->step = 0;\n'),
    ('clear_seq_bit_20_ignored', 'dtc_state', STATE_C,
     '\t\tif(seq != s->seq) return DTC_REJECT_STALE_SEQ;',
     '\t\tif(((seq ^ s->seq) & ~0x00100000u) != 0) return DTC_REJECT_STALE_SEQ;'),
    ('clear_of_32768_codes_refused', 'dtc_state', STATE_C,
     '\t\tif(s->result_count == 0) return DTC_REJECT_NOTHING_TO_CLEAR;',
     '\t\tif((s->result_count & 0x7FFF) == 0) return DTC_REJECT_NOTHING_TO_CLEAR;'),
    ('json_bytes_80_to_9f_dropped', 'dtc_state', STATE_C,
     '\t\tif(c < 0x20) continue;',
     '\t\tif(c < 0x20 || (c >= 0x80 && c < 0xA0)) continue;'),
    ('error_with_empty_reason_ignored', 'dtc_state', STATE_C,
     '\tif(s->phase != DTC_STATE_RUNNING) return;\n\n\tfinish(s, DTC_STATE_ERROR, reason, now_ms);',
     "\tif(s->phase != DTC_STATE_RUNNING || (reason != NULL && reason[0] == '\\0')) return;\n\n\tfinish(s, DTC_STATE_ERROR, reason, now_ms);"),
    ('json_longer_than_500_refused', 'dtc_state', STATE_C,
     '\tif(out->len + 1 >= out->size)',
     '\tif(out->len + 1 >= out->size || out->len > 500)'),
    ('json_number_printed_signed', 'dtc_state', STATE_C,
     '\tsnprintf(digits, sizeof(digits), "%" PRIu32, value);',
     '\tsnprintf(digits, sizeof(digits), "%" PRId32, value);'),
    ('json_buffer_size_in_16_bit', 'dtc_state', STATE_C,
     '\tjson_out_t out = {buf, size, 0, false};',
     '\tjson_out_t out = {buf, (uint16_t)size, 0, false};'),
    ('accepted_with_a_value_outside_the_enum', 'dtc_state', STATE_C,
     '\tif(seq_out != NULL) *seq_out = s->seq;\n\treturn DTC_ACCEPTED;',
     '\tif(seq_out != NULL) *seq_out = s->seq;\n\tif(clear && src == DTC_SRC_MQTT && s->seq > 0x100000u) return (dtc_accept_t)7;\n\treturn DTC_ACCEPTED;'),
    ('abort_seq_out_unguarded_on_rejection', 'dtc_state', STATE_C,
     '\tif(seq_out != NULL) *seq_out = s->seq;\n\n\t// Commands during a scan',
     '\t*seq_out = s->seq;\n\n\t// Commands during a scan'),
    ('abort_seq_out_unguarded_on_acceptance', 'dtc_state', STATE_C,
     '\tif(seq_out != NULL) *seq_out = s->seq;\n\treturn DTC_ACCEPTED;',
     '\t*seq_out = s->seq;\n\treturn DTC_ACCEPTED;'),
    ('abort_init_keeps_state_with_reason', 'dtc_state', STATE_C,
     '\tmemset(s, 0, sizeof(*s));',
     '\tif(s->reason == NULL) memset(s, 0, sizeof(*s));'),
    ('abort_json_null_text_unguarded', 'dtc_state', STATE_C,
     '\tif(text == NULL) return;\n\n',
     ''),
    ('abort_json_zero_behind_buffer', 'dtc_state', STATE_C,
     "\tbuf[out.len] = '\\0';",
     "\tbuf[size] = '\\0';"),
    ('hang_put_raw_does_not_advance', 'dtc_state', STATE_C,
     "\twhile(*text != '\\0') put_char(out, *text++);",
     "\twhile(*text != '\\0') put_char(out, *text);"),
    ('clock_1ms_behind_counts_as_ages', 'dtc_state', STATE_C,
     '\treturn now_ms >= then_ms ? now_ms - then_ms : 0;',
     '\treturn now_ms + 1 >= then_ms ? now_ms - then_ms : 0;'),
    ('expiry_clock_far_behind_expires', 'dtc_state', STATE_C,
     'elapsed_ms(now_ms, s->queued_ms) > DTC_HTTP_EXPIRY_MS',
     '(now_ms > s->queued_ms ? now_ms - s->queued_ms : s->queued_ms - now_ms) > DTC_HTTP_EXPIRY_MS'),
    ('clear_clock_far_behind_refused', 'dtc_state', STATE_C,
     'elapsed_ms(now_ms, s->finished_ms) > DTC_CLEAR_MAX_AGE_MS',
     '(now_ms > s->finished_ms ? now_ms - s->finished_ms : s->finished_ms - now_ms) > DTC_CLEAR_MAX_AGE_MS'),
    ('json_apostrophe_escaped', 'dtc_state', STATE_C,
     '\t\tif(c == \'"\' || c == \'\\\\\') put_char(out, \'\\\\\');',
     '\t\tif(c == \'"\' || c == \'\\\\\' || c == \'\\\'\') put_char(out, \'\\\\\');'),
    ('json_number_printed_signed_3', 'dtc_state', STATE_C,
     '"%" PRIu32, value);',
     '"%" PRId32, value);'),
    ('json_size_in_16_bit', 'dtc_state', STATE_C,
     '\tjson_out_t out = {buf, size, 0, false};',
     '\tjson_out_t out = {buf, size & 0xFFFFu, 0, false};'),
    ('json_null_name_or_reason_read', 'dtc_state', STATE_C,
     '\tif(text == NULL) return;\n',
     ''),
]
MUTATIONS += FOUND_BY_REVIEW_3

API_C = "main/dtc_api.c"
API_H = "main/dtc_api.h"
API_MD = "tools/w906/API.md"
FIXTURES = "tools/w906/fixtures/"

HEADER = '\tif(header == NULL || strcmp(header, "1") != 0) return request;'
HOST = "\tif(!dtc_api_host_allowed(host)) return request;"
BAD_REQUEST = '\trequest.status = 400;\n\trequest.reason = "bad_request";\n'
ACTION = '\tif(!find_parameter(query, "action", &value, &length)) return request;'
SEQ = '\t\tif(!find_parameter(query, "seq", &value, &length)) return request;'
SEQ_FORM = "\t\tif(!parse_seq(value, length, &seq)) return request;"
READ = '\telse if(!value_is(value, length, "read")) return request;'
VALUE_IS = "\treturn length == strlen(text) && memcmp(value, text, length) == 0;"
NAME_IS = "\t\tif(found_length == name_length && memcmp(query, name, name_length) == 0)"
NEXT = "\t\tquery += parameter_length;\n\t\tif(*query == '&') query++;\n"
SEQ_DIGIT = "\t\tif(!is_digit(text[i])) return false;"
SEQ_SUM = "\t\tvalue = value * 10u + (uint64_t)(text[i] - '0');"
SEQ_RANGE = "\tif(value < 1 || value > DTC_SEQ_MAX) return false;"
SEQ_LENGTH = "\tif(length > 10) return false;"
NO_HOST = "\tif(host == NULL) return false;"
HOST_REST = "\treturn rest != NULL && is_port_or_end(rest);"
NUMBER_DIGITS = "\t\twhile(digits < 3 && is_digit(*text))"
NUMBER_RANGE = "\t\tif(digits == 0 || value > 255u) return NULL;"
ID_LENGTH = "\tif(id_length < 1 || id_length > 32) return NULL;"
LOCAL = '\treturn skip_word(text + id_length, ".local");'
PORT = "\treturn digits >= 1 && digits <= 5 && text[digits] == '\\0';"
IS_DIGIT = "\treturn c >= '0' && c <= '9';"
IS_HEX = "\treturn is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');"
ANY_CASE = "\t\tif(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');\n"
NOT_READY = "\tif(!ready) return 503;"
STATUS = "\treturn result == DTC_ACCEPTED ? 202 : 409;"
ACCEPTED = "\tif(status == 202)"
LIMIT = "\tif(value > API_NUMBER_MAX) value = API_NUMBER_MAX;\n"
API_CONTROL = "\t\tif(c < 0x20) continue;"
ESCAPE = "\t\tif(c == '\"' || c == '\\\\') put_char(out, '\\\\');\n"
NONE = '\tif(value < 0) put_raw(out, "-1");\n\telse put_number(out, (uint32_t)value);'
NO_VOLTAGE = "\tif(millivolts < 0)"
TENTHS = "\ttenths = ((uint32_t)millivolts + 50u) / 100u;"
DECIMAL = "\tput_char(out, '.');\n\tput_char(out, (char)('0' + tenths % 10u));\n"
DTC_OBJECT = "\tput_raw(&out, (dtc_json != NULL && dtc_json[0] != '\\0') ? dtc_json : \"{}\");"
ECU = '\tput_raw(&out, status->ecu_online ? "online" : "offline");'
BODY_EMPTY = "\tjson_out_t out = {body, size, 0, false};\n\n\tif(size == 0) return -1;"
STATE_EMPTY = "\tjson_out_t out = {buf, size, 0, false};\n\n\tif(size == 0) return -1;"
OVERFLOW = "\t\tout->buf[0] = '\\0';\n\t\treturn -1;"
DEFER = "\treturn scan_busy && overdue_ms <= DTC_API_SLEEP_DEFER_MS;"


def unescaped(field):
    return ("api_%s_not_escaped" % field, "dtc_api", API_C,
            "\tput_escaped(&out, status->%s);" % field,
            '\tput_raw(&out, status->%s != NULL ? status->%s : "");' % (field, field))


API_MUTATIONS = [
    # Who may ask: the header
    ("api_header_not_checked", "dtc_api", API_C, HEADER,
     '\tif(0 && (header == NULL || strcmp(header, "1") != 0)) return request;'),
    ("api_header_may_be_missing", "dtc_api", API_C, HEADER,
     '\tif(header != NULL && strcmp(header, "1") != 0) return request;'),
    ("api_header_any_value", "dtc_api", API_C, HEADER,
     "\tif(header == NULL) return request;"),
    ("api_header_may_be_empty", "dtc_api", API_C, HEADER,
     '\tif(header == NULL || (header[0] != \'\\0\' && strcmp(header, "1") != 0)) return request;'),
    ("api_header_start_is_enough", "dtc_api", API_C, HEADER,
     "\tif(header == NULL || header[0] != '1') return request;"),
    ("api_header_end_is_enough", "dtc_api", API_C, HEADER,
     "\tif(header == NULL || header[0] == '\\0' || header[strlen(header) - 1] != '1') return request;"),

    # Who may ask: the host
    ("api_host_not_checked", "dtc_api", API_C, HOST, HOST.replace("if(", "if(0 && ")),
    ("api_host_may_be_missing", "dtc_api", API_C, HOST, HOST.replace("if(", "if(host != NULL && ")),
    ("api_host_missing_is_allowed", "dtc_api", API_C, NO_HOST, "\tif(host == NULL) return true;"),
    ("api_host_empty_is_allowed", "dtc_api", API_C, NO_HOST,
     NO_HOST + "\n\tif(host[0] == '\\0') return true;"),
    ("api_host_ipv6_is_allowed", "dtc_api", API_C, NO_HOST,
     NO_HOST + "\n\tif(host[0] == '[') return true;"),
    ("api_host_rest_not_checked", "dtc_api", API_C, HOST_REST,
     "\treturn rest != NULL && (is_port_or_end(rest) || true);"),
    ("api_host_trailing_dot", "dtc_api", API_C,
     "\tif(*text == '\\0') return true;",
     "\tif(*text == '\\0' || (text[0] == '.' && text[1] == '\\0')) return true;"),
    ("api_host_three_numbers", "dtc_api", API_C, "part < 4;", "part < 3;"),
    ("api_host_five_numbers", "dtc_api", API_C, "part < 4;", "part < 5;"),
    ("api_host_number_not_limited", "dtc_api", API_C, NUMBER_RANGE, "\t\tif(digits == 0) return NULL;"),
    ("api_host_number_256", "dtc_api", API_C, NUMBER_RANGE, NUMBER_RANGE.replace("> 255u", "> 256u")),
    ("api_host_number_255_refused", "dtc_api", API_C, NUMBER_RANGE, NUMBER_RANGE.replace("> 255u", ">= 255u")),
    ("api_host_number_may_be_missing", "dtc_api", API_C, NUMBER_RANGE, "\t\tif(value > 255u) return NULL;"),
    ("api_host_number_any_length", "dtc_api", API_C, NUMBER_DIGITS, "\t\twhile(is_digit(*text))"),
    ("api_host_number_4_digits", "dtc_api", API_C, NUMBER_DIGITS, NUMBER_DIGITS.replace("< 3", "< 4")),
    ("api_host_number_2_digits", "dtc_api", API_C, NUMBER_DIGITS, NUMBER_DIGITS.replace("< 3", "< 2")),
    ("api_host_any_separator", "dtc_api", API_C,
     "\t\t\tif(*text != '.') return NULL;", "\t\t\tif(*text == '\\0') return NULL;"),
    ("api_host_port_any_length", "dtc_api", API_C, PORT, PORT.replace(" && digits <= 5", "")),
    ("api_host_port_6_digits", "dtc_api", API_C, PORT, PORT.replace("<= 5", "<= 6")),
    ("api_host_port_4_digits", "dtc_api", API_C, PORT, PORT.replace("<= 5", "<= 4")),
    ("api_host_port_may_be_empty", "dtc_api", API_C, PORT, PORT.replace("digits >= 1 && ", "")),
    ("api_host_text_behind_port", "dtc_api", API_C, PORT, PORT.replace(" && text[digits] == '\\0'", "")),
    ("api_host_slash_like_colon", "dtc_api", API_C,
     "\tif(*text != ':') return false;", "\tif(*text != ':' && *text != '/') return false;"),
    ("api_host_name_without_wican", "dtc_api", API_C,
     '\ttext = skip_word(text, "wican_");', '\ttext = skip_word(text, "");'),
    ("api_host_name_without_underscore", "dtc_api", API_C,
     '\ttext = skip_word(text, "wican_");', '\ttext = skip_word(text, "wican");'),
    ("api_host_name_without_local", "dtc_api", API_C, LOCAL, "\treturn text + id_length;"),
    ("api_host_name_local_optional", "dtc_api", API_C, LOCAL,
     '\treturn skip_word(text + id_length, ".local") != NULL ? skip_word(text + id_length, ".local") : text + id_length;'),
    ("api_host_name_local_shortened", "dtc_api", API_C, LOCAL, LOCAL.replace(".local", ".loca")),
    ("api_host_name_case_sensitive", "dtc_api", API_C, ANY_CASE, ""),
    ("api_host_id_may_be_empty", "dtc_api", API_C, ID_LENGTH, "\tif(id_length > 32) return NULL;"),
    ("api_host_id_any_length", "dtc_api", API_C, ID_LENGTH, "\tif(id_length < 1) return NULL;"),
    ("api_host_id_33_characters", "dtc_api", API_C, ID_LENGTH, ID_LENGTH.replace("> 32", "> 33")),
    ("api_host_id_32_refused", "dtc_api", API_C, ID_LENGTH, ID_LENGTH.replace("> 32", ">= 32")),
    ("api_host_id_any_letter", "dtc_api", API_C, IS_HEX, IS_HEX.replace("c <= 'f'", "c <= 'z'")),
    ("api_host_id_letter_g", "dtc_api", API_C, IS_HEX, IS_HEX.replace("c <= 'F'", "c <= 'G'")),
    ("api_host_id_upper_case_refused", "dtc_api", API_C, IS_HEX,
     "\treturn is_digit(c) || (c >= 'a' && c <= 'f');"),
    ("api_host_id_backtick", "dtc_api", API_C, IS_HEX, IS_HEX.replace("c >= 'a'", "c >= '`'")),
    ("api_host_id_at_sign", "dtc_api", API_C, IS_HEX, IS_HEX.replace("c >= 'A'", "c >= '@'")),
    ("api_digit_colon", "dtc_api", API_C, IS_DIGIT, IS_DIGIT.replace("c <= '9'", "c <= ':'")),
    ("api_digit_slash", "dtc_api", API_C, IS_DIGIT, IS_DIGIT.replace("c >= '0'", "c >= '/'")),
    ("api_digit_9_refused", "dtc_api", API_C, IS_DIGIT, IS_DIGIT.replace("c <= '9'", "c < '9'")),

    # The order of the checks and what a refused request carries
    ("api_bad_request_before_forbidden", "dtc_api", API_C, HEADER,
     '\tif(query == NULL || strstr(query, "action=") == NULL)\n\t{\n\t\trequest.status = 400;\n'
     '\t\trequest.reason = "bad_request";\n\t\treturn request;\n\t}\n' + HEADER),
    ("api_foreign_host_is_bad_request", "dtc_api", API_C, HOST + "\n\n" + BAD_REQUEST, BAD_REQUEST + HOST + "\n"),
    ("api_forbidden_status_changed", "dtc_api", API_C, '{403, "forbidden", false, 0}', '{401, "forbidden", false, 0}'),
    ("api_forbidden_text_changed", "dtc_api", API_C, '{403, "forbidden", false, 0}', '{403, "denied", false, 0}'),
    ("api_bad_request_status_changed", "dtc_api", API_C, "\trequest.status = 400;", "\trequest.status = 404;"),
    ("api_bad_request_text_changed", "dtc_api", API_C, '"bad_request"', '"bad request"'),
    ("api_refused_clear_keeps_action", "dtc_api", API_C, SEQ, "\t\trequest.clear = true;\n" + SEQ),
    ("api_success_keeps_reason", "dtc_api", API_C, "\trequest.reason = NULL;\n", ""),

    # What is asked: the action
    ("api_action_missing_is_read", "dtc_api", API_C, ACTION,
     ACTION.replace(" return request;", '\n\t{\n\t\tvalue = "read";\n\t\tlength = 4;\n\t}')),
    ("api_action_unknown_is_read", "dtc_api", API_C, READ + "\n", ""),
    ("api_action_start_is_enough", "dtc_api", API_C, VALUE_IS,
     "\treturn length >= strlen(text) && memcmp(value, text, strlen(text)) == 0;"),
    ("api_action_shortened", "dtc_api", API_C, VALUE_IS,
     "\treturn length <= strlen(text) && memcmp(value, text, length) == 0;"),
    ("api_action_upper_case", "dtc_api", API_C, READ,
     '\telse if(!value_is(value, length, "read") && !value_is(value, length, "READ")) return request;'),
    ("api_last_action_counts", "dtc_api", API_C,
     "\t\t\treturn true;\n\t\t}\n\n" + NEXT + "\t}\n\treturn false;\n",
     "\t\t}\n\n" + NEXT + "\t}\n\treturn *value != NULL;\n"),
    ("api_only_first_parameter_read", "dtc_api", API_C, NEXT, "\t\tbreak;\n"),
    ("api_name_start_is_enough", "dtc_api", API_C, NAME_IS, NAME_IS.replace("found_length == ", "found_length >= ")),
    ("api_name_without_value_skipped", "dtc_api", API_C, NAME_IS,
     NAME_IS.replace(" == 0)", " == 0 && query[name_length] == '=')")),
    ("api_name_inside_a_value", "dtc_api", API_C,
     "\t\tquery += parameter_length;\n", "\t\tquery += found_length < parameter_length ? found_length + 1 : parameter_length;\n"),

    # What is asked: the number of a clear
    ("api_clear_without_seq", "dtc_api", API_C, SEQ + "\n" + SEQ_FORM,
     '\t\tif(find_parameter(query, "seq", &value, &length) && !parse_seq(value, length, &seq)) return request;'),
    ("api_clear_with_malformed_seq", "dtc_api", API_C, SEQ_FORM, "\t\t(void)parse_seq(value, length, &seq);"),
    ("api_read_with_malformed_seq_refused", "dtc_api", API_C, READ,
     '\telse\n\t{\n\t\tif(!value_is(value, length, "read")) return request;\n'
     '\t\tif(find_parameter(query, "seq", &value, &length) && !parse_seq(value, length, &seq)) return request;\n\t}'),
    ("api_read_returns_seq", "dtc_api", API_C, READ,
     '\telse\n\t{\n\t\tif(!value_is(value, length, "read")) return request;\n'
     '\t\tif(find_parameter(query, "seq", &value, &length) && parse_seq(value, length, &seq)) request.seq = seq;\n\t}'),
    ("api_seq_not_returned", "dtc_api", API_C, "\t\trequest.seq = seq;\n", ""),
    ("api_seq_0_accepted", "dtc_api", API_C, SEQ_RANGE, "\tif(value > DTC_SEQ_MAX) return false;"),
    ("api_seq_not_limited", "dtc_api", API_C, SEQ_RANGE, "\tif(value < 1) return false;"),
    ("api_seq_2147483648_accepted", "dtc_api", API_C, SEQ_RANGE, SEQ_RANGE.replace("> DTC_SEQ_MAX", "> DTC_SEQ_MAX + 1u")),
    ("api_seq_2147483647_refused", "dtc_api", API_C, SEQ_RANGE, SEQ_RANGE.replace("> DTC_SEQ_MAX", ">= DTC_SEQ_MAX")),
    ("api_seq_1_refused", "dtc_api", API_C, SEQ_RANGE, SEQ_RANGE.replace("value < 1", "value <= 1")),
    ("api_seq_wraps_at_32_bit", "dtc_api", API_C, SEQ_SUM,
     "\t\tvalue = (uint32_t)(value * 10u + (uint64_t)(text[i] - '0'));"),
    ("api_seq_any_length", "dtc_api", API_C, SEQ_LENGTH + "\n", ""),
    ("api_seq_11_digits", "dtc_api", API_C, SEQ_LENGTH, SEQ_LENGTH.replace("> 10", "> 11")),
    ("api_seq_10_digits_refused", "dtc_api", API_C, SEQ_LENGTH, SEQ_LENGTH.replace("> 10", ">= 10")),
    ("api_seq_other_characters_skipped", "dtc_api", API_C, SEQ_DIGIT, "\t\tif(!is_digit(text[i])) continue;"),
    ("api_seq_with_plus_sign", "dtc_api", API_C, SEQ_DIGIT,
     "\t\tif(i == 0 && text[i] == '+') continue;\n" + SEQ_DIGIT),
    ("api_seq_ends_at_first_other", "dtc_api", API_C, SEQ_DIGIT, "\t\tif(!is_digit(text[i])) break;"),

    # Status of the answer
    ("api_not_ready_not_checked", "dtc_api", API_C, NOT_READY, "\tif(0 && !ready) return 503;"),
    ("api_not_ready_after_rejection", "dtc_api", API_C, NOT_READY, "\tif(!ready && result == DTC_ACCEPTED) return 503;"),
    ("api_not_ready_accepts", "dtc_api", API_C, NOT_READY, "\tif(!ready && result != DTC_ACCEPTED) return 503;"),
    ("api_not_ready_status_changed", "dtc_api", API_C, NOT_READY, "\tif(!ready) return 500;"),
    ("api_accepted_status_changed", "dtc_api", API_C, STATUS, STATUS.replace("202", "200")),
    ("api_rejected_status_changed", "dtc_api", API_C, STATUS, STATUS.replace("409", "400")),
    ("api_only_busy_is_rejected", "dtc_api", API_C, STATUS, "\treturn result == DTC_REJECT_BUSY ? 409 : 202;"),
    ("api_nothing_to_clear_accepted", "dtc_api", API_C, STATUS,
     "\treturn (result == DTC_ACCEPTED || result == DTC_REJECT_NOTHING_TO_CLEAR) ? 202 : 409;"),
    ("api_unknown_result_accepted", "dtc_api", API_C, STATUS,
     "\treturn (result == DTC_ACCEPTED || result > DTC_REJECT_NOTHING_TO_CLEAR) ? 202 : 409;"),

    # Body of the answer
    ("api_body_never_accepted", "dtc_api", API_C, ACCEPTED, "\tif(0 && status == 202)"),
    ("api_body_every_2xx_accepted", "dtc_api", API_C, ACCEPTED, "\tif(status >= 200 && status < 300)"),
    ("api_body_status_low_byte", "dtc_api", API_C, ACCEPTED, "\tif((status & 0xFF) == 202)"),
    ("api_body_every_other_accepted", "dtc_api", API_C, ACCEPTED, "\tif(status != 409)"),
    ("api_body_accepted_with_reason", "dtc_api", API_C,
     '\t\tput_raw(&out, "{\\"accepted\\":true");\n',
     '\t\tput_raw(&out, "{\\"accepted\\":true");\n\t\tif(reason != NULL)\n\t\t{\n'
     '\t\t\tput_raw(&out, ",\\"reason\\":\\"");\n\t\t\tput_escaped(&out, reason);\n'
     "\t\t\tput_char(&out, '\"');\n\t\t}\n"),
    ("api_body_reason_not_escaped", "dtc_api", API_C,
     "\t\tput_escaped(&out, reason);", '\t\tput_raw(&out, reason != NULL ? reason : "");'),
    ("api_body_seq_always_0", "dtc_api", API_C, "\tput_number(&out, seq);", "\tput_number(&out, 0 * seq);"),
    ("api_body_seq_in_16_bit", "dtc_api", API_C, "\tput_number(&out, seq);", "\tput_number(&out, seq & 0xFFFFu);"),
    ("api_body_with_blank", "dtc_api", API_C, '",\\"seq\\":"', '", \\"seq\\":"'),
    ("api_body_size_too_small", "dtc_api", API_H,
     "#define DTC_API_BODY_SIZE       96", "#define DTC_API_BODY_SIZE       63"),
    ("api_body_0_byte_buffer_written", "dtc_api", API_C, BODY_EMPTY, BODY_EMPTY.replace("size == 0", "0 && size == 0")),
    ("api_body_1_byte_buffer_not_emptied", "dtc_api", API_C, BODY_EMPTY, BODY_EMPTY.replace("size == 0", "size <= 1")),

    # Texts and numbers of both answers
    ("api_missing_text_written_as_null", "dtc_api", API_C,
     "\tif(text == NULL) return;", '\tif(text == NULL) text = "null";'),
    ("api_json_not_escaped", "dtc_api", API_C, ESCAPE, ""),
    ("api_json_quote_not_escaped", "dtc_api", API_C, ESCAPE, ESCAPE.replace("c == '\"' || ", "")),
    ("api_json_backslash_not_escaped", "dtc_api", API_C, ESCAPE, ESCAPE.replace(" || c == '\\\\'", "")),
    ("api_json_control_characters", "dtc_api", API_C, API_CONTROL + "\n", ""),
    ("api_json_control_boundary", "dtc_api", API_C, API_CONTROL, "\t\tif(c < 0x1f) continue;"),
    ("api_json_blank_dropped", "dtc_api", API_C, API_CONTROL, "\t\tif(c <= 0x20) continue;"),
    ("api_json_escape_character_kept", "dtc_api", API_C, API_CONTROL, "\t\tif(c < 0x20 && c != 0x1b) continue;"),
    ("api_json_utf8_dropped", "dtc_api", API_C, API_CONTROL, "\t\tif(c < 0x20 || c >= 0x80) continue;"),
    ("api_json_del_dropped", "dtc_api", API_C, API_CONTROL, "\t\tif(c < 0x20 || c == 0x7f) continue;"),
    ("api_number_not_limited", "dtc_api", API_C, LIMIT, ""),
    ("api_number_2147483648_written", "dtc_api", API_C, LIMIT, LIMIT.replace("value > API_NUMBER_MAX", "value > API_NUMBER_MAX + 1u")),
    ("api_number_limit_one_less", "dtc_api", API_C,
     "#define API_NUMBER_MAX  0x7FFFFFFFu", "#define API_NUMBER_MAX  0x7FFFFFFEu"),
    ("api_number_limit_32_bit", "dtc_api", API_C,
     "#define API_NUMBER_MAX  0x7FFFFFFFu", "#define API_NUMBER_MAX  0xFFFFFFFEu"),
    ("api_number_in_16_bit", "dtc_api", API_C, LIMIT, LIMIT + "\tvalue &= 0xFFFFu;\n"),
    ("api_number_0_not_written", "dtc_api", API_C,
     "\tdo\n\t{\n\t\tdigits[count++] = (char)('0' + value % 10u);\n\t\tvalue /= 10u;\n\t}\n\twhile(value != 0);\n",
     "\twhile(value != 0)\n\t{\n\t\tdigits[count++] = (char)('0' + value % 10u);\n\t\tvalue /= 10u;\n\t}\n"),
    ("api_number_digits_reversed", "dtc_api", API_C,
     "\twhile(count > 0) put_char(out, digits[--count]);",
     "\tfor(size_t i = 0; i < count; i++) put_char(out, digits[i]);"),

    # GET /api/state
    unescaped("id"),
    unescaped("fw"),
    unescaped("git"),
    unescaped("autopid"),
    unescaped("mqtt"),
    ("api_version_changed", "dtc_api", API_C, '{\\"api\\":1,', '{\\"api\\":2,'),
    ("api_state_with_blank", "dtc_api", API_C, '",\\"up\\":"', '", \\"up\\":"'),
    ("api_state_field_left_out", "dtc_api", API_C,
     '\tput_raw(&out, ",\\"heap_min\\":");\n\tput_number(&out, status->heap_min);\n', ""),
    ("api_state_field_order", "dtc_api", API_C,
     '\tput_raw(&out, ",\\"heap\\":");\n\tput_number(&out, status->heap);\n'
     '\tput_raw(&out, ",\\"heap_min\\":");\n\tput_number(&out, status->heap_min);\n',
     '\tput_raw(&out, ",\\"heap_min\\":");\n\tput_number(&out, status->heap_min);\n'
     '\tput_raw(&out, ",\\"heap\\":");\n\tput_number(&out, status->heap);\n'),
    ("api_state_heap_is_heap_min", "dtc_api", API_C,
     "\tput_number(&out, status->heap);", "\tput_number(&out, status->heap_min);"),
    ("api_state_up_is_boot", "dtc_api", API_C,
     "\tput_number(&out, status->up_s);", "\tput_number(&out, status->boot);"),
    ("api_state_pass_is_pids", "dtc_api", API_C,
     "\tput_number(&out, status->pass);", "\tput_number(&out, status->pids);"),
    ("api_state_ecu_always_online", "dtc_api", API_C, ECU, ECU.replace("status->ecu_online ?", "(status->ecu_online || true) ?")),
    ("api_state_ecu_texts_swapped", "dtc_api", API_C, ECU, ECU.replace('"online" : "offline"', '"offline" : "online"')),
    ("api_state_ecu_as_boolean", "dtc_api", API_C,
     '\tput_raw(&out, ",\\"ecu\\":\\"");\n' + ECU + '\n\tput_raw(&out, "\\",\\"pass\\":");',
     '\tput_raw(&out, ",\\"ecu\\":");\n' + ECU.replace('"online" : "offline"', '"true" : "false"')
     + '\n\tput_raw(&out, ",\\"pass\\":");'),
    ("api_none_not_checked", "dtc_api", API_C, NONE, "\tput_number(out, (uint32_t)value);"),
    ("api_none_only_for_minus_1", "dtc_api", API_C, NONE, NONE.replace("value < 0", "value == -1")),
    ("api_none_for_0", "dtc_api", API_C, NONE, NONE.replace("value < 0", "value <= 0")),
    ("api_rx_age_without_none", "dtc_api", API_C,
     "\tput_number_or_none(&out, status->rx_age_ms);", "\tput_number(&out, (uint32_t)status->rx_age_ms);"),
    ("api_sleep_in_without_none", "dtc_api", API_C,
     "\tput_number_or_none(&out, status->sleep_in_s);", "\tput_number(&out, (uint32_t)status->sleep_in_s);"),
    ("api_voltage_negative_not_checked", "dtc_api", API_C, NO_VOLTAGE, "\tif(0 && millivolts < 0)"),
    ("api_voltage_none_only_for_minus_1", "dtc_api", API_C, NO_VOLTAGE, "\tif(millivolts == -1)"),
    ("api_voltage_none_for_0", "dtc_api", API_C, NO_VOLTAGE, "\tif(millivolts <= 0)"),
    ("api_voltage_none_with_decimal", "dtc_api", API_C,
     '\t\tput_raw(out, "-1");\n\t\treturn;', '\t\tput_raw(out, "-1.0");\n\t\treturn;'),
    ("api_voltage_not_rounded", "dtc_api", API_C, TENTHS, "\ttenths = (uint32_t)millivolts / 100u;"),
    ("api_voltage_half_rounded_down", "dtc_api", API_C, TENTHS, TENTHS.replace("+ 50u", "+ 49u")),
    ("api_voltage_49_rounded_up", "dtc_api", API_C, TENTHS, TENTHS.replace("+ 50u", "+ 51u")),
    ("api_voltage_always_rounded_up", "dtc_api", API_C, TENTHS, TENTHS.replace("+ 50u", "+ 99u")),
    ("api_voltage_without_decimal", "dtc_api", API_C, DECIMAL, ""),
    ("api_voltage_with_comma", "dtc_api", API_C, "\tput_char(out, '.');", "\tput_char(out, ',');"),
    ("api_voltage_in_16_bit", "dtc_api", API_C, TENTHS, TENTHS.replace("(uint32_t)millivolts", "(uint16_t)millivolts")),
    ("api_dtc_missing_written_as_null", "dtc_api", API_C, DTC_OBJECT, DTC_OBJECT.replace('"{}"', '"null"')),
    ("api_dtc_empty_written_as_nothing", "dtc_api", API_C, DTC_OBJECT, DTC_OBJECT.replace(" && dtc_json[0] != '\\0'", "")),
    ("api_dtc_escaped_again", "dtc_api", API_C, DTC_OBJECT, DTC_OBJECT.replace("put_raw", "put_escaped")),
    ("api_state_0_byte_buffer_written", "dtc_api", API_C, STATE_EMPTY, STATE_EMPTY.replace("size == 0", "0 && size == 0")),
    ("api_state_1_byte_buffer_not_emptied", "dtc_api", API_C, STATE_EMPTY, STATE_EMPTY.replace("size == 0", "size <= 1")),

    # The buffer of both answers
    ("api_json_truncated_silently", "dtc_api", API_C, "\t\tout->overflow = true;\n", ""),
    ("api_json_writes_behind_the_buffer", "dtc_api", API_C,
     "\t\tout->overflow = true;\n\t\treturn;\n", "\t\tout->overflow = true;\n"),
    ("api_json_no_room_for_zero", "dtc_api", API_C,
     "\tif(out->len + 1 >= out->size)", "\tif(out->len >= out->size)"),
    ("api_json_one_byte_wasted", "dtc_api", API_C,
     "\tif(out->len + 1 >= out->size)", "\tif(out->len + 2 >= out->size)"),
    ("api_json_no_terminating_zero", "dtc_api", API_C, "\tout->buf[out->len] = '\\0';\n", ""),
    ("api_json_truncated_text_left", "dtc_api", API_C, OVERFLOW, "\t\treturn -1;"),
    ("api_json_overflow_returns_0", "dtc_api", API_C, OVERFLOW, OVERFLOW.replace("return -1;", "return 0;")),
    ("api_json_length_in_8_bit", "dtc_api", API_C,
     "\tout->buf[out->len++] = c;", "\tout->buf[out->len++ & 0xFFu] = c;"),
    ("api_json_length_in_10_bit", "dtc_api", API_C,
     "\tout->buf[out->len++] = c;", "\tout->buf[out->len++ & 0x3FFu] = c;"),
    ("api_json_returned_length_in_8_bit", "dtc_api", API_C,
     "\treturn (int)out->len;", "\treturn (int)(out->len & 0xFFu);"),
    ("api_json_returned_length_in_10_bit", "dtc_api", API_C,
     "\treturn (int)out->len;", "\treturn (int)(out->len & 0x3FFu);"),

    # Sleep
    ("api_sleep_never_waits", "dtc_api", API_C, DEFER, DEFER.replace("return ", "return 0 && ")),
    ("api_sleep_waits_without_scan", "dtc_api", API_C, DEFER, DEFER.replace("scan_busy", "(scan_busy || true)")),
    ("api_sleep_waits_for_ever", "dtc_api", API_C, DEFER,
     "\treturn scan_busy && (overdue_ms <= DTC_API_SLEEP_DEFER_MS || true);"),
    ("api_sleep_wait_boundary", "dtc_api", API_C, DEFER, DEFER.replace("<=", "<")),
    ("api_sleep_wait_in_seconds", "dtc_api", API_C, DEFER,
     DEFER.replace("overdue_ms <= DTC_API_SLEEP_DEFER_MS", "overdue_ms / 1000u <= DTC_API_SLEEP_DEFER_MS / 1000u")),
    ("api_sleep_wait_in_32_bit", "dtc_api", API_C, DEFER, DEFER.replace("overdue_ms", "(uint32_t)overdue_ms")),
    ("api_sleep_waits_late_without_scan", "dtc_api", API_C, DEFER,
     "\treturn scan_busy == (overdue_ms <= DTC_API_SLEEP_DEFER_MS);"),
    ("api_sleep_wait_limit_changed", "dtc_api", API_H,
     "#define DTC_API_SLEEP_DEFER_MS  (60u * 1000u)", "#define DTC_API_SLEEP_DEFER_MS  (61u * 1000u)"),

    # The contract and the fixtures are really compared: a changed example has to be noticed
    ("api_md_state_example_changed", "dtc_api", API_MD, '"batt_v":12.4,"sleep_in_s":-1', '"batt_v":12.40,"sleep_in_s":-1'),
    ("api_md_body_changed", "dtc_api", API_MD,
     '`{"accepted":false,"reason":"stale_seq","seq":42}`', '`{"accepted":false,"reason":"stale","seq":42}`'),
    ("api_fixture_body_empty", "dtc_api", FIXTURES + "api_body_accepted.json", '{"accepted":true,"seq":43}', ""),
]
API_MUTATIONS += [("api_fixture_state_%s" % name, "dtc_api", FIXTURES + "api_state_%s.json" % name,
                   '{"api":1,', '{"api":2,')
                  for name in ("example", "offline", "starting", "scan", "limits", "empty")]
API_MUTATIONS += [("api_fixture_body_%s" % name, "dtc_api", FIXTURES + "api_body_%s.json" % name,
                   '{"accepted":', '{"Accepted":')
                  for name in ("accepted", "busy", "read_required", "stale_seq", "nothing_to_clear", "not_ready",
                               "forbidden", "bad_request")]
MUTATIONS += API_MUTATIONS

# Found by trying further changes: the test of that time did not notice these
QUERY_START = "\tsize_t name_length = strlen(name);\n\n\tif(query == NULL) return false;\n\n\twhile(*query != '\\0')"
API_FOUND_BY_HUNT = [
    ("api_long_host_allowed", "dtc_api", API_C, NO_HOST, NO_HOST + "\n\tif(strlen(host) > 100) return true;"),
    ("api_host_name_twice", "dtc_api", API_C, LOCAL,
     '\ttext = skip_word(text + id_length, ".local");\n'
     '\treturn (text != NULL && skip_word(text, ".local") != NULL) ? skip_word(text, ".local") : text;'),
    ("api_header_not_needed_for_name", "dtc_api", API_C, HEADER,
     '\tif((header == NULL || strcmp(header, "1") != 0) && skip_mdns_name(host != NULL ? host : "") == NULL) return request;'),
    ("api_header_not_needed_with_port", "dtc_api", API_C, HEADER,
     '\tif((header == NULL || strcmp(header, "1") != 0) && (host == NULL || strchr(host, \':\') == NULL)) return request;'),
    ("api_query_only_start_searched", "dtc_api", API_C, QUERY_START,
     "\tsize_t name_length = strlen(name);\n\tconst char *start = query;\n\n\tif(query == NULL) return false;\n\n"
     "\twhile(*query != '\\0' && query - start < 64)"),
    ("api_query_only_8_parameters", "dtc_api", API_C, QUERY_START,
     "\tsize_t name_length = strlen(name);\n\tint parameters = 0;\n\n\tif(query == NULL) return false;\n\n"
     "\twhile(*query != '\\0' && parameters++ < 8)"),
    ("api_text_cut_at_64", "dtc_api", API_C,
     "\tif(text == NULL) return;\n\n\tfor(; *text != '\\0'; text++)",
     "\tconst char *start = text;\n\n\tif(text == NULL) return;\n\n\tfor(; *text != '\\0' && text - start < 64; text++)"),
    ("api_dtc_control_characters_dropped", "dtc_api", API_C, DTC_OBJECT,
     "\tif(dtc_json == NULL || dtc_json[0] == '\\0') dtc_json = \"{}\";\n\tfor(; *dtc_json != '\\0'; dtc_json++)\n\t{\n"
     "\t\tif((unsigned char)*dtc_json >= 0x20) put_char(&out, *dtc_json);\n\t}"),
    ("api_state_size_in_16_bit", "dtc_api", API_C,
     "\tjson_out_t out = {buf, size, 0, false};", "\tjson_out_t out = {buf, (uint16_t)size, 0, false};"),
    ("api_body_size_in_16_bit", "dtc_api", API_C,
     "\tjson_out_t out = {body, size, 0, false};", "\tjson_out_t out = {body, (uint16_t)size, 0, false};"),
    ("api_json_ends_at_4000", "dtc_api", API_C,
     "\tif(out->len + 1 >= out->size)", "\tif(out->len + 1 >= out->size || out->len >= 4000)"),
    ("api_json_length_in_16_bit", "dtc_api", API_C,
     "\tout->buf[out->len++] = c;", "\tout->buf[out->len++ & 0xFFFFu] = c;"),
    ("api_json_returned_length_in_16_bit", "dtc_api", API_C,
     "\treturn (int)out->len;", "\treturn (int)(out->len & 0xFFFFu);"),
]
MUTATIONS += API_FOUND_BY_HUNT

# Found by the review as changes the test of that time did not notice, each named after its kind:
# a rule that holds for one input only, a field that depends on another one, a buffer that is checked on one
# path only, a length or a time counted in too few bits
ESCAPED = ("\tif(text == NULL) return;\n\n\tfor(; *text != '\\0'; text++)\n\t{\n\t\tunsigned char c = (unsigned char)*text;\n\n"
           + API_CONTROL + "\n" + ESCAPE)
RX_AGE = "\tput_number_or_none(&out, status->rx_age_ms);"
VOLTAGE = "\tput_volts(&out, status->batt_mv);"
PIDS = "\tput_number(&out, status->pids);"
QUERY_NULL = "\tif(query == NULL) return false;"


def in_bits(name, source, counter, bits):
    """The counter of a length shortened to `bits` bit."""
    return ("api_%s_length_in_%d_bit" % (name, bits), "dtc_api", API_C, source,
            source.replace(counter, "(%s & 0x%Xu)" % (counter, (1 << bits) - 1)))


API_FOUND_BY_REVIEW = [
    # No mutation of the lists above made the examples of foreign hosts fail (example.com, localhost, ::1 ...)
    ("api_host_other_text_allowed", "dtc_api", API_C, HOST_REST, "\treturn rest == NULL || is_port_or_end(rest);"),

    # Rules and boundaries without a mutation so far
    ("api_unknown_parameter_refused", "dtc_api", API_C, "\t\tquery += parameter_length;\n",
     "\t\tif(found_length != 0 && found_length != 3 && found_length != 6) return false;\n\t\tquery += parameter_length;\n"),
    ("api_success_status_not_set", "dtc_api", API_C, "\trequest.status = 0;\n", ""),
    ("api_clear_not_reported", "dtc_api", API_C, "\t\trequest.clear = true;\n", ""),
    ("api_sleep_waits_60001_ms", "dtc_api", API_C, DEFER,
     DEFER.replace("DTC_API_SLEEP_DEFER_MS", "DTC_API_SLEEP_DEFER_MS + 1u")),

    # Header and host depend on each other
    ("api_header_not_needed_with_port_8080", "dtc_api", API_C, HEADER,
     '\tif((header == NULL || strcmp(header, "1") != 0) && (host == NULL || strstr(host, ":8080") == NULL)) return request;'),
    ("api_header_not_needed_in_10_net", "dtc_api", API_C, HEADER,
     '\tif((header == NULL || strcmp(header, "1") != 0) && (host == NULL || strncmp(host, "10.", 3) != 0)) return request;'),
    ("api_request_refuses_port_65535", "dtc_api", API_C, HOST,
     '\tif(!dtc_api_host_allowed(host) || strstr(host, ":65535") != NULL) return request;'),

    # A name with a blank behind it
    ("api_name_with_blank_behind", "dtc_api", API_C, NAME_IS,
     NAME_IS.replace("found_length == name_length &&",
                     "(found_length == name_length || (found_length == name_length + 1 && query[name_length] == ' ')) &&")),

    # Escaping depends on the place in the text
    ("api_json_first_character_not_escaped", "dtc_api", API_C, ESCAPED,
     "\tconst char *start = text;\n\n"
     + ESCAPED.replace("if(c == '\"' || c == '\\\\')", "if((c == '\"' || c == '\\\\') && text != start)")),
    ("api_json_last_character_not_escaped", "dtc_api", API_C, ESCAPE,
     ESCAPE.replace("if(c == '\"' || c == '\\\\')", "if((c == '\"' || c == '\\\\') && text[1] != '\\0')")),
    ("api_json_doubled_character_escaped_once", "dtc_api", API_C, ESCAPE,
     ESCAPE.replace("if(c == '\"' || c == '\\\\')", "if((c == '\"' || c == '\\\\') && text[1] != (char)c)")),
    ("api_json_last_control_character_kept", "dtc_api", API_C, API_CONTROL,
     "\t\tif(c < 0x20 && text[1] != '\\0') continue;"),

    # A field depends on another field
    ("api_state_rx_age_none_while_offline", "dtc_api", API_C, RX_AGE,
     "\tput_number_or_none(&out, (status->ecu_online || status->rx_age_ms < 100000) ? status->rx_age_ms : -1);"),
    ("api_state_voltage_none_at_boot", "dtc_api", API_C, VOLTAGE,
     "\tput_volts(&out, (status->up_s == 0 && status->batt_mv > 13000) ? -1 : status->batt_mv);"),
    ("api_state_pids_0_without_autopid", "dtc_api", API_C, PIDS,
     "\tput_number(&out, (status->autopid != NULL || status->pids < 99u) ? status->pids : 0);"),
    ("api_state_ecu_online_by_pass", "dtc_api", API_C, ECU,
     ECU.replace("status->ecu_online ?", "(status->ecu_online || status->pass > 3000000000u) ?")),

    # The buffer is checked on one path only
    ("api_body_accepted_not_bounded", "dtc_api", API_C,
     '\t\tput_raw(&out, "{\\"accepted\\":true");\n',
     '\t\tmemcpy(out.buf, "{\\"accepted\\":true", 16);\n\t\tout.len = 16;\n'),
    ("api_voltage_none_not_bounded", "dtc_api", API_C,
     '\t\tput_raw(out, "-1");\n\t\treturn;',
     "\t\tout->buf[out->len++] = '-';\n\t\tout->buf[out->len++] = '1';\n\t\treturn;"),
    ("api_json_truncated_in_large_buffer", "dtc_api", API_C,
     "\tif(out->overflow)\n\t{", "\tif(out->overflow && out->size < 2048)\n\t{"),
    ("api_json_truncated_text_left_in_large_buffer", "dtc_api", API_C, OVERFLOW,
     "\t\tif(out->size < 4096) out->buf[0] = '\\0';\n\t\treturn -1;"),

    # A length or a time counted in too few bits, a limit nobody wrote down
    in_bits("host_id", ID_LENGTH, "id_length", 8),
    in_bits("host_id", ID_LENGTH, "id_length", 16),
    ("api_host_port_length_in_8_bit", "dtc_api", API_C, PORT, PORT.replace("digits <= 5", "(digits & 0xFFu) <= 5")),
    ("api_host_port_length_in_16_bit", "dtc_api", API_C, PORT, PORT.replace("digits <= 5", "(digits & 0xFFFFu) <= 5")),
    in_bits("action", VALUE_IS, "length", 8),
    in_bits("action", VALUE_IS, "length", 16),
    ("api_seq_length_in_8_bit", "dtc_api", API_C, SEQ_LENGTH, "\tlength &= 0xFFu;\n" + SEQ_LENGTH),
    ("api_seq_length_in_16_bit", "dtc_api", API_C, SEQ_LENGTH, "\tlength &= 0xFFFFu;\n" + SEQ_LENGTH),
    ("api_name_length_in_8_bit", "dtc_api", API_C, NAME_IS,
     NAME_IS.replace("found_length == name_length", "(found_length & 0xFFu) == name_length")),
    ("api_header_length_in_8_bit", "dtc_api", API_C, HEADER,
     "\tif(header == NULL || header[0] != '1' || (strlen(header) & 0xFFu) != 1) return request;"),
    ("api_header_length_in_16_bit", "dtc_api", API_C, HEADER,
     "\tif(header == NULL || header[0] != '1' || (strlen(header) & 0xFFFFu) != 1) return request;"),
    ("api_query_longer_than_256_not_read", "dtc_api", API_C, QUERY_NULL,
     "\tif(query == NULL || strlen(query) > 256) return false;"),
    ("api_query_only_64_parameters", "dtc_api", API_C, QUERY_START,
     "\tsize_t name_length = strlen(name);\n\tint parameters = 0;\n\n\tif(query == NULL) return false;\n\n"
     "\twhile(*query != '\\0' && parameters++ < 64)"),
    ("api_sleep_wait_in_40_bit", "dtc_api", API_C, DEFER, DEFER.replace("overdue_ms", "(overdue_ms & 0xFFFFFFFFFFull)")),
    ("api_sleep_wait_in_63_bit", "dtc_api", API_C, DEFER,
     DEFER.replace("overdue_ms", "(overdue_ms & 0x7FFFFFFFFFFFFFFFull)")),

    # A value between the examples: a long text in one field, a range of voltages, a status, an answer of
    # the scan state, a number together with a status
    ("api_state_long_git_dropped", "dtc_api", API_C, "\tput_escaped(&out, status->git);",
     '\tput_escaped(&out, (status->git != NULL && strlen(status->git) > 400) ? "" : status->git);'),
    ("api_voltage_wrong_between_30_and_90_volts", "dtc_api", API_C, TENTHS,
     TENTHS + "\n\tif(millivolts > 30000 && millivolts < 90000) tenths = 300u;"),
    ("api_not_ready_ignored_for_unknown_result", "dtc_api", API_C, NOT_READY,
     "\tif(!ready && result <= DTC_REJECT_NOTHING_TO_CLEAR) return 503;"),
    ("api_body_226_accepted", "dtc_api", API_C, ACCEPTED, "\tif(status == 202 || status == 226)"),
    ("api_body_small_seq_dropped_for_503", "dtc_api", API_C, "\tput_number(&out, seq);",
     "\tput_number(&out, (status == 503 && seq < 10) ? 0 : seq);"),
]
MUTATIONS += API_FOUND_BY_REVIEW

# For --selftest only
SELFTEST_MUTATIONS = {
    "noop": ("selftest_without_effect", "dtc_state", STATE_C,
             '#include "dtc_state.h"\n', '#include "dtc_state.h"\n// change without effect\n'),
    "missing": ("selftest_text_not_found", "dtc_state", STATE_C,
                "this text is not in the source", ""),
    "hang": ("selftest_endless_loop", "dtc_state", STATE_C,
             "\tmemset(s, 0, sizeof(*s));\n", "\tmemset(s, 0, sizeof(*s));\n\tfor(;;)\n\t{\n\t}\n"),
}


def shell(command, cwd, timeout):
    """Returns the finished process, or None if it had to be stopped after `timeout` seconds."""
    # A mutated JSON writer may print bytes that are not UTF-8.
    # A session of its own: the shell and the program it started are stopped together.
    process = subprocess.Popen(command, shell=True, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               encoding="utf-8", errors="replace", start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.communicate()
        return None
    return subprocess.CompletedProcess(command, process.returncode, stdout, stderr)


def copy_sources(target):
    """The files a host test needs, at the same relative paths."""
    (target / "main").mkdir(parents=True)
    for source in sorted((REPO / "main").glob("dtc_*.[ch]")):
        shutil.copy(source, target / "main" / source.name)
    shutil.copytree(REPO / "tools" / "w906", target / "tools" / "w906",
                    ignore=shutil.ignore_patterns("__pycache__", "*_test"))


def run_test(tree, test, timeout):
    """Returns (state, detail) with state 'green', 'red', 'aborted' or 'broken'."""
    cwd = tree / "tools" / "w906"
    build = shell(TESTS[test]["build"], cwd, TIMEOUT_S)
    if build is None:
        return "broken", "the compiler did not end within %d s" % TIMEOUT_S
    if build.returncode != 0:
        return "broken", "does not compile:\n" + build.stderr.strip()
    run = shell(TESTS[test]["run"], cwd, timeout)
    if run is None:
        return "aborted", "the test did not end within %d s and was stopped" % timeout
    failed = [line[5:] for line in run.stdout.splitlines() if line.startswith("FAIL ")]
    if run.returncode == 0:
        if failed:
            return "broken", "the test printed FAIL but exited with 0"
        return "green", ""
    if failed:
        # Name an example if one failed: the walk against the model fails for almost every mutation
        named = [check for check in failed if "random walks" not in check]
        return "red", "%d checks failed, first: %s" % (len(failed), (named or failed)[0])
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


def evaluate(mutation, test=None, timeout=TIMEOUT_S):
    """Returns (state, detail) of the test after the mutation, in a fresh copy of the sources.
    Without a mutation the unchanged sources are tested with `test`."""
    with tempfile.TemporaryDirectory(prefix="redproof-") as directory:
        tree = pathlib.Path(directory)
        copy_sources(tree)
        if mutation is not None:
            error = mutate(tree, mutation)
            if error is not None:
                return "broken", error
        return run_test(tree, mutation[1] if mutation is not None else test, timeout)


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


def report(mutations, timeout, jobs=1):
    """Runs the mutations, `jobs` at a time, returns the number of problems."""
    problems = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        results = pool.map(lambda mutation: evaluate(mutation, timeout=timeout), mutations)
        states = list(zip(mutations, results))
    for mutation, (state, detail) in states:
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
        (["--selftest-mutation", "hang", "--timeout", "5"], 1, "ABORTED selftest_endless_loop",
         "a mutation that makes the test hang must be stopped and make the run fail"),
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
    parser.add_argument("--timeout", metavar="SECONDS", type=int, default=TIMEOUT_S,
                        help="stop a mutated test that runs longer (default %d)" % TIMEOUT_S)
    parser.add_argument("--jobs", metavar="N", type=int, default=os.cpu_count() or 2, help="mutations run at a time")
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

    problems = report(mutations, arguments.timeout, arguments.jobs)
    print("%d mutations, %d red, %d problems" % (len(mutations), len(mutations) - problems, problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
