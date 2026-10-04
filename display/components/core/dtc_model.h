/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __DTC_MODEL_H__
#define __DTC_MODEL_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "json.h"

/*
 * The result of a fault memory scan as the WiCAN delivers it with GET /api/dtc/result (the same text as
 * the retained MQTT message). Format: W906.md, section "Fehlerspeicher"; examples:
 * tools/w906/fixtures/dtc_result_*.json.
 *
 *   {"state":"done","action":"read"|"clear","duration_ms":N,"dtc_count":N,"ecus":[
 *     {"name":"N3/28 Motorelektronik (CDID3)","id":"7E0","protocol":"UDS"|"KWP",
 *      "cleared":true|false (optional),"status":"ok"|"no_response"|"pending_timeout"|"incomplete"|"nrc_XX",
 *      "dtcs":[{"code":"P242F-FA","status":"68","active":true|false (UDS only)}, ...],
 *      "dtcs_omitted":N (optional)}, ...]}
 */

#define DTC_ECUS_MAX        24
#define DTC_CODES_MAX       128
#define DTC_RESULT_TOKENS   2048    // tokens dtc_result_parse() needs for the largest result (5 KB of text)

typedef enum
{
	DTC_ECU_OK,
	DTC_ECU_NO_RESPONSE,
	DTC_ECU_PENDING_TIMEOUT,
	DTC_ECU_INCOMPLETE,
	DTC_ECU_NRC,            // negative response, the code is in nrc
	DTC_ECU_OTHER,          // a status text this firmware does not know
} dtc_ecu_status_t;

typedef struct
{
	char code[16];
	char status[8];
	int8_t active;          // 1, 0, or -1 if the member is absent (KWP)
} dtc_code_t;

typedef struct
{
	char name[64];
	char short_name[24];    // see dtc_short_name()
	char id[8];
	bool uds;
	dtc_ecu_status_t status;
	uint8_t nrc;            // 0 unless the status is DTC_ECU_NRC
	int8_t cleared;         // 1, 0, or -1 if the member is absent
	uint16_t first_code;    // index into codes
	uint16_t code_count;
	uint32_t omitted;       // "dtcs_omitted", 0 if absent
} dtc_ecu_t;

typedef struct
{
	bool clear;
	uint32_t duration_ms;
	uint32_t dtc_count;     // as reported, may exceed the codes listed (omitted ones)
	dtc_ecu_t ecus[DTC_ECUS_MAX];
	int ecu_count;
	dtc_code_t codes[DTC_CODES_MAX];
	int code_count;
	bool cut;               // more control units or codes than there is room for; the rest was left out
} dtc_result_t;

// Returns false and leaves *result untouched if the text is not a finished result: broken JSON, "state"
// other than "done", "action" or "protocol" with a text the format does not know, a required member
// missing, a member of the wrong type. A number has to be written without fraction and exponent and be
// from 0 to 2^32-1, else it is of the wrong type. Members the format does not know are ignored.
// Texts longer than their field are cut at a character boundary of UTF-8. A text also ends before an
// escape json_text() refuses (\u0000, half a surrogate pair).
// "nrc_22" becomes DTC_ECU_NRC with nrc 0x22 (two hexadecimal digits, capital or small letters); any other
// unknown status text DTC_ECU_OTHER.
// work, work_count: room for the JSON reader (json.h), provided by the caller because it is too large for
// a task stack. A text that needs more tokens than that is treated like broken JSON.
bool dtc_result_parse(const char *json, size_t length, dtc_result_t *result, json_token_t *work, int work_count);

// A short name for the progress display. The text in the last pair of parentheses if there is one
// ("N2/14 Rückhaltesystem (SRS)" -> "SRS"), else the name without its first word if that word contains a
// digit ("N30/4 ESP" -> "ESP", "A1 Kombiinstrument" -> "Kombiinstrument"), else the whole name
// ("Radio" -> "Radio"). Cut at a character boundary if it does not fit.
// A pair is an opening parenthesis and the next closing one, the first word ends at the first blank. A rule
// that would leave an empty text is skipped ("N10" -> "N10", "N10 SAM ()" -> "SAM ()"). With size 0 nothing
// is written.
void dtc_short_name(const char *name, char *out, size_t size);

typedef struct
{
	int ecus_with_codes;    // control units with at least one listed or omitted code
	int ecus_not_ok;        // control units whose status is not ok
	int ecus_clean;         // status ok and no code
	uint32_t codes;         // listed plus omitted, 2^32-1 if it is more
} dtc_summary_t;

void dtc_summarize(const dtc_result_t *result, dtc_summary_t *summary);

typedef struct
{
	uint32_t before;        // codes in the list before the clear (listed plus omitted)
	uint32_t remaining;     // codes in the result of the clear
	uint32_t cleared;       // before minus remaining, 0 if more remain than there were
	int unconfirmed;        // control units of the clear result with "cleared":false
} dtc_clear_summary_t;

// What a clear achieved: `before` is the list that was read, `after` the result of the clear run (it
// contains only what is left).
void dtc_clear_summarize(const dtc_result_t *before, const dtc_result_t *after, dtc_clear_summary_t *summary);

#endif
