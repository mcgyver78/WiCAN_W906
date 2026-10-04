/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "dtc_view.h"

#define DOT " \xC2\xB7 "

// The lines of a caller. What does not fit goes to `spare`, so that nobody has to ask for room first.
typedef struct
{
	dtc_line_t *lines;
	int max;
	int count;
	dtc_line_t spare;
} list_t;

// Bytes of the well-formed UTF-8 character at the beginning of a text, 0 if there is none. The zero at the
// end of the text continues no character: nothing behind it is read.
static size_t character(const unsigned char *text)
{
	unsigned char first = text[0];
	size_t count = first < 0x80 ? 1 : first < 0xC2 ? 0 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : first < 0xF5 ? 4 : 0;
	unsigned char low = 0x80;
	unsigned char high = 0xBF;

	// The second byte tells a character from a longer way to write a shorter one, from half a surrogate pair
	// and from a number behind the end of Unicode
	if(first == 0xE0) low = 0xA0;
	if(first == 0xED) high = 0x9F;
	if(first == 0xF0) low = 0x90;
	if(first == 0xF4) high = 0x8F;

	for(size_t i = 1; i < count; i++)
	{
		if(text[i] < low || text[i] > high) return 0;
		low = 0x80;
		high = 0xBF;
	}
	return count;
}

// Appends the first `length` bytes of a text to the text in `out`: as many whole characters as fit. The
// adapter is not trusted with what it sends: a byte that is no part of a well-formed character would break
// the text for the screen and for the browser, a control character would make two lines of one. Both
// become '?'. Behind the `length` bytes stands the end of the text, a blank or a parenthesis, so no
// character reaches over them.
static void append(char *out, size_t size, const char *text, size_t length)
{
	size_t used = strlen(out);
	size_t done = 0;

	while(done < length)
	{
		const unsigned char *bytes = (const unsigned char *)&text[done];
		size_t count = character(bytes);
		bool shown = count > 0 && bytes[0] >= 0x20 && bytes[0] != 0x7F;

		if(!shown) count = 1;
		if(used + count + 1 > size) break;

		if(shown) memcpy(&out[used], bytes, count);
		else out[used] = '?';
		used += count;
		done += count;
	}
	out[used] = '\0';
}

static void append_text(char *out, size_t size, const char *text)
{
	append(out, size, text, strlen(text));
}

static void append_number(char *out, size_t size, uint32_t number)
{
	char digits[12];

	snprintf(digits, sizeof(digits), "%" PRIu32, number);
	append_text(out, size, digits);
}

void dtc_plain_name(const char *name, char *out, size_t size)
{
	const char *text = name;
	size_t length = strlen(name);
	size_t word = strcspn(name, " ");
	size_t rest = word + strspn(name + word, " ");
	size_t end;

	if(size == 0) return;

	// The first word if it is a component designation. A rule that would leave nothing is skipped.
	if(strcspn(name, "0123456789") < word && rest < length)
	{
		text = name + rest;
		length -= rest;
	}

	// A pair of parentheses at the end of what is left, and the blanks around it
	end = length;
	while(end > 0 && text[end - 1] == ' ') end--;
	if(end > 0 && text[end - 1] == ')')
	{
		// It belongs to the nearest opening parenthesis before it, unless another closing one comes first
		end--;
		while(end > 0 && text[end - 1] != '(' && text[end - 1] != ')') end--;
		if(end > 0 && text[end - 1] == '(')
		{
			end--;
			while(end > 0 && text[end - 1] == ' ') end--;
			if(end > 0) length = end;
		}
	}

	out[0] = '\0';
	append(out, size, text, length);
}

// The next line. If there is no room for it, the last line that fits tells so.
static dtc_line_t *add(list_t *list, dtc_line_kind_t kind)
{
	dtc_line_t *line = &list->spare;

	if(list->count < list->max)
	{
		line = &list->lines[list->count];
		list->count++;
	}
	else if(list->max > 0)
	{
		dtc_line_t *last = &list->lines[list->max - 1];

		last->kind = DTC_LINE_NOTE;
		strcpy(last->text, "Liste gekürzt");
		last->detail[0] = '\0';
	}

	line->kind = kind;
	line->text[0] = '\0';
	line->detail[0] = '\0';
	return line;
}

// The detail of a code. A status byte says more than a guess, so only a clear true or false is put in words.
static void code_detail(const dtc_code_t *code, char *out, size_t size)
{
	if(code->active == 1)
	{
		append_text(out, size, "aktiv");
	}
	else if(code->active == 0)
	{
		append_text(out, size, "gespeichert");
	}
	else if(code->status[0] != '\0')
	{
		append_text(out, size, "Status ");
		append_text(out, size, code->status);
	}
}

static void problem_detail(const dtc_ecu_t *ecu, char *out, size_t size)
{
	char nrc[24];

	switch(ecu->status)
	{
		case DTC_ECU_NO_RESPONSE:
			append_text(out, size, "keine Antwort");
			break;

		case DTC_ECU_PENDING_TIMEOUT:
			append_text(out, size, "Antwort ausstehend");
			break;

		case DTC_ECU_INCOMPLETE:
			append_text(out, size, "unvollständig");
			break;

		case DTC_ECU_NRC:
			snprintf(nrc, sizeof(nrc), "abgelehnt (NRC %02X)", (unsigned)ecu->nrc);
			append_text(out, size, nrc);
			break;

		default:
			append_text(out, size, "unbekannter Status");
			break;
	}
}

// The control units with codes, the ones that did not answer as they should, how many are clean (`clean`
// 0: not told) and whether something is missing
static void add_result(list_t *list, const dtc_result_t *result, int clean)
{
	dtc_line_t *line;

	for(int i = 0; i < result->ecu_count; i++)
	{
		const dtc_ecu_t *ecu = &result->ecus[i];
		uint32_t codes = ecu->omitted > UINT32_MAX - ecu->code_count ? UINT32_MAX : ecu->omitted + ecu->code_count;

		if(codes == 0) continue;

		line = add(list, DTC_LINE_ECU);
		dtc_plain_name(ecu->name, line->text, sizeof(line->text));
		append_text(line->detail, sizeof(line->detail), ecu->id);
		append_text(line->detail, sizeof(line->detail), DOT);
		append_number(line->detail, sizeof(line->detail), codes);
		append_text(line->detail, sizeof(line->detail), " Fehler");

		for(int k = 0; k < ecu->code_count; k++)
		{
			const dtc_code_t *code = &result->codes[ecu->first_code + k];

			line = add(list, DTC_LINE_CODE);
			append_text(line->text, sizeof(line->text), code->code);
			code_detail(code, line->detail, sizeof(line->detail));
		}

		if(ecu->omitted > 0)
		{
			line = add(list, DTC_LINE_NOTE);
			append_number(line->text, sizeof(line->text), ecu->omitted);
			append_text(line->text, sizeof(line->text), ecu->omitted == 1 ? " Code nicht übertragen" : " Codes nicht übertragen");
		}
	}

	for(int i = 0; i < result->ecu_count; i++)
	{
		const dtc_ecu_t *ecu = &result->ecus[i];

		if(ecu->status == DTC_ECU_OK) continue;

		line = add(list, DTC_LINE_PROBLEM);
		dtc_plain_name(ecu->name, line->text, sizeof(line->text));
		problem_detail(ecu, line->detail, sizeof(line->detail));
	}

	if(clean > 0)
	{
		line = add(list, DTC_LINE_CLEAN);
		append_number(line->text, sizeof(line->text), (uint32_t)clean);
		append_text(line->text, sizeof(line->text), clean == 1 ? " Steuergerät ohne Fehler" : " Steuergeräte ohne Fehler");
	}

	if(result->cut)
	{
		line = add(list, DTC_LINE_NOTE);
		append_text(line->text, sizeof(line->text), "Liste unvollständig");
	}
}

int dtc_view_list(const dtc_result_t *result, dtc_line_t *lines, int max)
{
	list_t list = {.lines = lines, .max = max, .count = 0};
	dtc_summary_t summary;
	dtc_line_t *line;

	dtc_summarize(result, &summary);

	line = add(&list, DTC_LINE_HEAD);
	append_number(line->text, sizeof(line->text), summary.codes);
	append_text(line->text, sizeof(line->text), " Fehler");
	append_number(line->detail, sizeof(line->detail), (uint32_t)result->ecu_count);
	append_text(line->detail, sizeof(line->detail), result->ecu_count == 1 ? " Steuergerät" : " Steuergeräte");
	append_text(line->detail, sizeof(line->detail), DOT);
	// Not (duration_ms + 500) / 1000: the sum does not fit for the last half second of the range
	append_number(line->detail, sizeof(line->detail), result->duration_ms / 1000u + (result->duration_ms % 1000u >= 500u ? 1u : 0u));
	append_text(line->detail, sizeof(line->detail), " s");

	add_result(&list, result, summary.ecus_clean);
	return list.count;
}

int dtc_view_cleared(const dtc_result_t *before, const dtc_result_t *after, dtc_line_t *lines, int max)
{
	list_t list = {.lines = lines, .max = max, .count = 0};
	dtc_clear_summary_t summary;
	dtc_line_t *line;

	dtc_clear_summarize(before, after, &summary);

	line = add(&list, DTC_LINE_HEAD);
	append_text(line->text, sizeof(line->text), "Gelöscht ");
	append_number(line->text, sizeof(line->text), summary.cleared);
	append_text(line->text, sizeof(line->text), " von ");
	append_number(line->text, sizeof(line->text), summary.before);
	append_text(line->detail, sizeof(line->detail), "verbleibend ");
	append_number(line->detail, sizeof(line->detail), summary.remaining);

	for(int i = 0; i < after->ecu_count; i++)
	{
		const dtc_ecu_t *ecu = &after->ecus[i];

		if(ecu->cleared != 0) continue;

		line = add(&list, DTC_LINE_PROBLEM);
		dtc_plain_name(ecu->name, line->text, sizeof(line->text));
		append_text(line->detail, sizeof(line->detail), "Löschen nicht bestätigt");
	}

	add_result(&list, after, 0);
	return list.count;
}

// Appends a text if there is room for it and for the closing zero. The length is counted on either way,
// so that nothing shorter slips in behind what did not fit.
static size_t put(char *out, size_t size, size_t length, const char *text)
{
	size_t count = strlen(text);

	if(length + count < size) memcpy(&out[length], text, count);
	return length + count;
}

int dtc_view_export(const dtc_line_t *lines, int count, char *out, size_t size)
{
	size_t length = 0;

	if(size == 0) return -1;

	for(int i = 0; i < count; i++)
	{
		const dtc_line_t *line = &lines[i];

		if(line->kind == DTC_LINE_CODE) length = put(out, size, length, "  ");
		length = put(out, size, length, line->text);
		if(line->detail[0] != '\0')
		{
			length = put(out, size, length, " - ");
			length = put(out, size, length, line->detail);
		}
		length = put(out, size, length, "\n");
	}

	if(length >= size)
	{
		out[0] = '\0';
		return -1;
	}
	out[length] = '\0';
	return (int)length;
}
