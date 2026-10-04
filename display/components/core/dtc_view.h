/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __DTC_VIEW_H__
#define __DTC_VIEW_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "dtc_model.h"

/*
 * The fault memory as lines for the screen and for the text export in the browser. German texts, UTF-8.
 * The codes are shown as the adapter reports them; there are no plain texts for them in this version.
 *
 * A list (dtc_view_list), in this order:
 *   HEAD     "3 Fehler"                      detail "18 Steuergeräte · 35 s"
 *   then every control unit with at least one listed or omitted code, in the order of the result:
 *   ECU      "Motorelektronik"               detail "7E0 · 2 Fehler"
 *   CODE     "P242F-FA"                      detail "aktiv" | "gespeichert" | "Status 68" | ""
 *   NOTE     "4 Codes nicht übertragen"      (if the adapter left codes out, "1 Code nicht übertragen")
 *   then every control unit whose status is not ok, in the order of the result:
 *   PROBLEM  "ESP"                           detail "keine Antwort" | "Antwort ausstehend" |
 *                                            "unvollständig" | "abgelehnt (NRC 22)" | "unbekannter Status"
 *   then, if there is at least one control unit with status ok and without a code:
 *   CLEAN    "15 Steuergeräte ohne Fehler"   ("1 Steuergerät ohne Fehler")
 *   and, if the result was cut (dtc_result_t.cut):
 *   NOTE     "Liste unvollständig"
 *
 * Words: "Fehler" for one and for several; "1 Steuergerät", else "Steuergeräte". The number of the head is
 * dtc_summary_t.codes, the control units are all of the result, the seconds are duration_ms rounded
 * (500 ms up). The number of an ECU line counts its listed plus omitted codes (2^32-1 if it is more, like
 * the head). A control unit with codes whose status is not ok appears in both groups.
 * The detail of a code: UDS with "active" true "aktiv", false "gespeichert"; without "active" (KWP, or
 * any other value of dtc_code_t.active) "Status " and the status text, or empty if there is no status text.
 * The NRC is written with two capital hexadecimal digits. A status that is no member of dtc_ecu_status_t
 * is not ok and an "unbekannter Status".
 *
 * A result is taken as dtc_result_parse() delivers it: its counts and indexes are trusted, what the adapter
 * wrote into its texts is not. In a name, an id, a code and a status text every byte that is no part of a
 * well-formed UTF-8 character and every control character (below 0x20, and 0x7F) is shown as '?': every
 * line is UTF-8 and stays one line, whatever the adapter sent.
 */

// Enough for every result dtc_result_t can hold. The longest outcome of a clear has 226 lines: the head, 24
// control units that did not confirm, 24 with codes, 128 codes, 24 notes of omitted codes, 24 with a status
// that is not ok, and the note of a cut result. The longest list has 202.
#define DTC_VIEW_LINES_MAX  226
#define DTC_VIEW_TEXT_SIZE  48
#define DTC_VIEW_DETAIL_SIZE 40

typedef enum
{
	DTC_LINE_HEAD,
	DTC_LINE_ECU,
	DTC_LINE_CODE,
	DTC_LINE_NOTE,
	DTC_LINE_PROBLEM,
	DTC_LINE_CLEAN,
} dtc_line_kind_t;

typedef struct
{
	dtc_line_kind_t kind;
	char text[DTC_VIEW_TEXT_SIZE];
	char detail[DTC_VIEW_DETAIL_SIZE];
} dtc_line_t;

// The name of a control unit for a list: without its first word if that word contains a digit (the
// component designation, "N3/28 Motorelektronik (CDID3)" -> "Motorelektronik"), and without a last pair of
// parentheses with what is in it and the blanks before and behind it ("N2/14 Rückhaltesystem (SRS)" ->
// "Rückhaltesystem"). A rule that would leave an empty text is skipped ("N30/4 (ESP)" -> "(ESP)",
// "N10" -> "N10"). The rules are applied in this order, the second to what the first left. A pair is an
// opening parenthesis and the next closing one, and it is the last if nothing but blanks follows it; the
// first word ends at the first blank and goes with the blanks behind it. Cut at a character boundary of
// UTF-8 if it does not fit; what is no well-formed UTF-8 and control characters become '?' as in a line.
// With size 0 nothing is written.
void dtc_plain_name(const char *name, char *out, size_t size);

// Fills `lines` with the list of a result. Returns the number of lines. If there are more than `max`, the
// last line that fits is the NOTE "Liste gekürzt" and the rest is left out. With max 0 or less nothing is
// written and 0 is returned. Texts that do not fit their field are cut at a character boundary.
int dtc_view_list(const dtc_result_t *result, dtc_line_t *lines, int max);

// The outcome of a clear. `before` is the list that was read, `after` the result of the clear run (it
// contains only what is left). In this order:
//   HEAD     "Gelöscht 3 von 3"              detail "verbleibend 0"     (dtc_clear_summarize)
//   PROBLEM  "ESP"                           detail "Löschen nicht bestätigt"   for every control unit of
//                                            `after` with "cleared" false
//   then the ECU, CODE, NOTE and PROBLEM lines of `after` as in a list, without its HEAD and CLEAN lines.
// Returns the number of lines; too many lines are handled as in dtc_view_list.
int dtc_view_cleared(const dtc_result_t *before, const dtc_result_t *after, dtc_line_t *lines, int max);

// The lines as plain text for the export: one line each, ended by "\n". HEAD, ECU, PROBLEM, CLEAN and
// NOTE lines start in the first column, CODE lines are indented by two blanks; text and detail are joined
// by " - " if the detail is not empty. Returns the length, -1 if it does not fit (out is then an empty
// string; with size 0 nothing is written and -1 is returned, whatever the count). A count below 1 gives an
// empty text. The lines are taken as dtc_view_list() and dtc_view_cleared() fill them.
int dtc_view_export(const dtc_line_t *lines, int count, char *out, size_t size);

#endif
