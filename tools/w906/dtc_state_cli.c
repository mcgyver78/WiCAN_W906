/*
 * Line-oriented front end of main/dtc_state.c. test_api_contract.py sends it the same calls as the rules
 * of mock_wican.py and compares the answers, so that the stand-in for the adapter cannot drift away from
 * the rules of the firmware.
 * Build in tools/w906:
 *   cc -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I../../main ../../main/dtc_state.c dtc_state_cli.c -o dtc_state_cli
 *
 * One command per line on stdin, one line per command on stdout: the result, then the state as JSON.
 *   init SEED                            ok
 *   begin read|clear mqtt|http SEQ NOW   accepted N, or busy N, read_required N, stale_seq N, nothing_to_clear N
 *   pickup NOW                           run, or no
 *   progress STEP TOTAL NAME             ok   (NAME is the rest of the line, without it the name is NULL)
 *   error REASON NOW                     ok   (REASON is one word)
 *   done COUNT NOW                       ok
 *   json NOW                             ok
 * Words are separated by single blanks, numbers are decimal digits only. NOW is milliseconds since boot;
 * init and progress show the state at the last NOW given (0 after init). The state starts as after "init 1".
 * A line that is not understood is answered with "? ..." and changes nothing; the exit status is then 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "dtc_state.h"

#define LINE_SIZE       2048
#define JSON_SIZE       4096
#define RESULT_SIZE     64

// The state keeps the pointers to a name and a reason. In the firmware they are constants, here they have
// to outlive the line they came in.
typedef struct kept_text
{
	struct kept_text *next;
	char text[];
} kept_text_t;

static kept_text_t *kept = NULL;

static const char *keep(const char *text)
{
	size_t size = strlen(text) + 1;
	kept_text_t *entry = malloc(sizeof(*entry) + size);

	if(entry == NULL)
	{
		fprintf(stderr, "out of memory\n");
		exit(2);
	}
	memcpy(entry->text, text, size);
	entry->next = kept;
	kept = entry;
	return entry->text;
}

// Only when no state points to them any more: after an init and at the end
static void drop_kept(void)
{
	while(kept != NULL)
	{
		kept_text_t *next = kept->next;

		free(kept);
		kept = next;
	}
}

// Next word of the line, NULL at its end
static char *next_word(char **rest)
{
	char *word = *rest;
	char *blank;

	if(*word == '\0') return NULL;

	blank = strchr(word, ' ');
	if(blank == NULL)
	{
		*rest = word + strlen(word);
	}
	else
	{
		*blank = '\0';
		*rest = blank + 1;
	}
	return word;
}

// Decimal digits only, up to `max`. Not strtoull(): it also takes blanks, a sign and "0x".
static bool to_number(const char *word, uint64_t max, uint64_t *value)
{
	uint64_t number = 0;

	if(word == NULL || *word == '\0') return false;

	for(; *word != '\0'; word++)
	{
		uint64_t digit;

		if(*word < '0' || *word > '9') return false;
		digit = (uint64_t)(*word - '0');
		// number * 10 + digit would be above max
		if(digit > max || number > (max - digit) / 10u) return false;
		number = number * 10u + digit;
	}
	*value = number;
	return true;
}

static bool is_word(const char *word, const char *text)
{
	return word != NULL && strcmp(word, text) == 0;
}

// Runs one command. Returns false if the line is not understood; `result` then says why.
static bool run(dtc_state_t *s, uint64_t *now_ms, char *line, char *result, size_t size)
{
	char *rest = line;
	const char *command = next_word(&rest);
	uint64_t first = 0, second = 0;

	snprintf(result, size, "ok");

	if(is_word(command, "init"))
	{
		if(!to_number(next_word(&rest), UINT32_MAX, &first) || *rest != '\0')
		{
			snprintf(result, size, "? init SEED");
			return false;
		}
		dtc_state_init(s, (uint32_t)first);
		drop_kept();
		*now_ms = 0;
		return true;
	}

	if(is_word(command, "begin"))
	{
		const char *action = next_word(&rest);
		const char *source = next_word(&rest);
		bool clear = is_word(action, "clear");
		bool http = is_word(source, "http");
		uint32_t seq = 0;
		const char *reason;

		if((!clear && !is_word(action, "read")) || (!http && !is_word(source, "mqtt")) ||
		   !to_number(next_word(&rest), UINT32_MAX, &first) || !to_number(next_word(&rest), UINT64_MAX, &second) ||
		   *rest != '\0')
		{
			snprintf(result, size, "? begin read|clear mqtt|http SEQ NOW");
			return false;
		}
		*now_ms = second;
		// No reason means accepted
		reason = dtc_accept_reason(dtc_state_try_begin(s, clear, http ? DTC_SRC_HTTP : DTC_SRC_MQTT, (uint32_t)first,
		                                               *now_ms, &seq));
		snprintf(result, size, "%s %" PRIu32, reason != NULL ? reason : "accepted", seq);
		return true;
	}

	if(is_word(command, "pickup"))
	{
		if(!to_number(next_word(&rest), UINT64_MAX, &first) || *rest != '\0')
		{
			snprintf(result, size, "? pickup NOW");
			return false;
		}
		*now_ms = first;
		snprintf(result, size, "%s", dtc_state_pickup(s, *now_ms) ? "run" : "no");
		return true;
	}

	if(is_word(command, "progress"))
	{
		if(!to_number(next_word(&rest), UINT8_MAX, &first) || !to_number(next_word(&rest), UINT8_MAX, &second))
		{
			snprintf(result, size, "? progress STEP TOTAL NAME");
			return false;
		}
		dtc_state_progress(s, (uint8_t)first, (uint8_t)second, *rest != '\0' ? keep(rest) : NULL);
		return true;
	}

	if(is_word(command, "error"))
	{
		const char *reason = next_word(&rest);

		if(reason == NULL || *reason == '\0' || !to_number(next_word(&rest), UINT64_MAX, &first) || *rest != '\0')
		{
			snprintf(result, size, "? error REASON NOW");
			return false;
		}
		*now_ms = first;
		dtc_state_error(s, keep(reason), *now_ms);
		return true;
	}

	if(is_word(command, "done"))
	{
		if(!to_number(next_word(&rest), UINT16_MAX, &first) || !to_number(next_word(&rest), UINT64_MAX, &second) ||
		   *rest != '\0')
		{
			snprintf(result, size, "? done COUNT NOW");
			return false;
		}
		*now_ms = second;
		dtc_state_done(s, (uint16_t)first, *now_ms);
		return true;
	}

	if(is_word(command, "json"))
	{
		if(!to_number(next_word(&rest), UINT64_MAX, &first) || *rest != '\0')
		{
			snprintf(result, size, "? json NOW");
			return false;
		}
		*now_ms = first;
		return true;
	}

	snprintf(result, size, "? unknown command");
	return false;
}

int main(void)
{
	static char line[LINE_SIZE];
	static char json[JSON_SIZE];
	char result[RESULT_SIZE];
	dtc_state_t state;
	uint64_t now_ms = 0;
	int status = 0;

	dtc_state_init(&state, 1);

	while(fgets(line, sizeof(line), stdin) != NULL)
	{
		size_t length = strlen(line);

		if(length > 0 && line[length - 1] == '\n')
		{
			line[length - 1] = '\0';
		}
		else if(length == sizeof(line) - 1)
		{
			// The rest of the line must not be read as the next command
			int c;

			do
			{
				c = getchar();
			}
			while(c != EOF && c != '\n');
			printf("? line too long\n");
			fflush(stdout);
			status = 1;
			continue;
		}

		if(!run(&state, &now_ms, line, result, sizeof(result)))
		{
			printf("%s\n", result);
			status = 1;
		}
		else
		{
			if(dtc_state_json(&state, true, now_ms, json, sizeof(json)) < 0) snprintf(json, sizeof(json), "overflow");
			printf("%s %s\n", result, json);
		}
		// A test may wait for each answer before it sends the next command
		fflush(stdout);
	}

	drop_kept();
	return status;
}
