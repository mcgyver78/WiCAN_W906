/*
 * Host test for display/components/core/texts.c. Run "make test_texts && ./test_texts" in display/test.
 * redproof.py removes or weakens every rule once (mutations/texts.py) and expects this test to fail.
 *
 * The texts below are copied by hand from the comments of texts.h, not from texts.c.
 */
#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include <sys/wait.h>
#include "test.h"
#include "texts.h"

#define COUNT(a)    ((int)(sizeof(a) / sizeof((a)[0])))

static char what[320];

// Numbers far from the members of every enum of the module, and numbers whose low 8, 16, 24 or 31 bits are
// the view LIVE (9) or SCAN (7): a member is its whole number
static const int OUTSIDE[] = {
	-1, -2, -100, INT_MIN, INT_MAX, 1000, 255, 256, 65536, 256 + 9, 256 + 7, 65536 + 9, 65536 + 7, 16777216 + 9, 16777216 + 7, INT_MIN + 9, INT_MIN + 7,
};

/* What the header says ---------------------------------------------------------------------------------- */

static const struct
{
	conn_view_t view;
	const char *name;
	const char *text;
	const char *word;
	ring_kind_t ring;       // of CONN_VIEW_LIVE: with nothing to warn of
} VIEWS[] = {
	{CONN_VIEW_NO_WIFI, "NO_WIFI", "WiCAN nicht gefunden – schläft, stromlos oder außer Reichweite", "no_wifi", RING_RED},
	{CONN_VIEW_CONNECTING, "CONNECTING", "Verbinde mit WiCAN …", "connecting", RING_YELLOW},
	{CONN_VIEW_NO_ANSWER, "NO_ANSWER", "WiCAN antwortet nicht", "no_answer", RING_RED},
	{CONN_VIEW_FOREIGN, "FOREIGN", "Fremdes WiCAN – nicht gekoppelt", "foreign", RING_RED},
	{CONN_VIEW_NO_API, "NO_API", "WiCAN-Firmware ohne Display-API – nur Live-Werte", "no_api", RING_GREY},
	{CONN_VIEW_AUTOPID_OFF, "AUTOPID_OFF", "AutoPID nicht aktiv", "autopid_off", RING_GREY},
	{CONN_VIEW_STARTING, "STARTING", "WiCAN startet …", "starting", RING_YELLOW},
	{CONN_VIEW_SCAN, "SCAN", "Live-Werte angehalten (Fehlerspeicher-Scan)", "scan", RING_PROGRESS},
	{CONN_VIEW_ECU_OFFLINE, "ECU_OFFLINE", "Zündung aus – Motorsteuergerät offline", "ecu_offline", RING_GREY},
	{CONN_VIEW_LIVE, "LIVE", "", "live", RING_NONE},
};

static const struct
{
	dtc_flow_block_t block;
	const char *name;
	const char *text;
} BLOCKS[] = {
	{DTC_FLOW_ALLOWED, "ALLOWED", ""},
	{DTC_FLOW_NO_ADAPTER, "NO_ADAPTER", "WiCAN nicht erreichbar"},
	{DTC_FLOW_FOREIGN, "FOREIGN", "Fremdes WiCAN – nicht gekoppelt"},
	{DTC_FLOW_NO_API, "NO_API", "WiCAN-Firmware ohne Display-API"},
	{DTC_FLOW_AUTOPID_OFF, "AUTOPID_OFF", "AutoPID nicht aktiv"},
	{DTC_FLOW_STARTING, "STARTING", "WiCAN startet noch"},
	{DTC_FLOW_NOT_SUPPORTED, "NOT_SUPPORTED", "Profil ohne Fehlerspeicher"},
	{DTC_FLOW_BUSY, "BUSY", "Scan läuft bereits"},
	{DTC_FLOW_ECU_OFFLINE, "ECU_OFFLINE", "Zündung aus – Motorsteuergerät offline"},
	{DTC_FLOW_ENGINE_RUNNING, "ENGINE_RUNNING", "Motor läuft – nur bei Motor aus"},
	{DTC_FLOW_RPM_UNKNOWN, "RPM_UNKNOWN", "Drehzahl nicht lesbar"},
	{DTC_FLOW_NO_LIST, "NO_LIST", "Erst lesen, dann löschen"},
	{DTC_FLOW_LIST_OLD, "LIST_OLD", "Liste veraltet – erneut lesen"},
	{DTC_FLOW_NO_CODES, "NO_CODES", "Keine Fehler zu löschen"},
	{DTC_FLOW_BUTTON_STUCK, "BUTTON_STUCK", "Knopf klemmt – Löschen gesperrt"},
};

static const struct
{
	const char *word;
	const char *text;
} REASONS[] = {
	{"busy", "Scan läuft bereits (anderes Gerät)"},
	{"ecu_offline", "Motorsteuergerät offline – Zündung an?"},
	{"engine_running", "Motor läuft – nur bei Motor aus"},
	{"engine_state_unknown", "Drehzahl nicht lesbar – nichts gelöscht"},
	{"not_supported", "Profil ohne Fehlerspeicher"},
	{"out_of_memory", "WiCAN: Speicher knapp – erneut lesen"},
	{"result_serialize_failed", "WiCAN: Speicher knapp – erneut lesen"},
	{"stale_seq", "Liste veraltet – erneut lesen"},
	{"read_required", "Liste veraltet – erneut lesen"},
	{"nothing_to_clear", "Keine Fehler zu löschen"},
	{"expired", "Auftrag verfallen – nichts gesendet"},
	{"not_ready", "WiCAN startet noch"},
	{"forbidden", "WiCAN lehnt die Anfrage ab"},
	{"bad_request", "WiCAN versteht die Anfrage nicht"},
	{"internal", "WiCAN: interner Fehler – erneut lesen"},
	{"no_answer", "Keine Antwort vom WiCAN"},
	{"restarted", "WiCAN neu gestartet – Ergebnis verloren"},
	{"superseded", "Von einem anderen Scan überholt"},
};

// The reason words by where they come from, looked up by hand on 2026-10-04.
// tools/w906/API.md, "reason" of the dtc object in GET /api/state:
static const char *const REASONS_OF_STATE[] = {
	"ecu_offline", "engine_running", "engine_state_unknown", "not_supported", "out_of_memory", "result_serialize_failed", "expired", "internal",
};
// tools/w906/API.md, bodies of POST /api/dtc and GET /api/dtc/result:
static const char *const REASONS_OF_ANSWERS[] = {"busy", "read_required", "stale_seq", "nothing_to_clear", "not_ready", "forbidden", "bad_request"};
// main/dtc_state.c (the expiry and dtc_accept_reason), main/dtc_api.c (dtc_api_parse_request), main/dtc_http.c
// and main/autopid.c (dtc_fail, dtc_state_error, dtc_check_engine, dtc_publish_error):
static const char *const REASONS_OF_FIRMWARE[] = {
	"expired", "busy", "read_required", "stale_seq", "nothing_to_clear", "forbidden", "bad_request", "not_ready", "result_serialize_failed",
	"ecu_offline", "engine_state_unknown", "engine_running", "not_supported", "out_of_memory", "internal",
};
// display/components/core/dtc_flow.h, dtc_flow_t.reason:
static const char *const REASONS_OF_FLOW[] = {"no_answer", "restarted", "superseded"};

/* Helpers ---------------------------------------------------------------------------------------------- */

// The characters the font of the display has: ASCII from the blank to the tilde, the German letters, the en
// dash and the ellipsis. A text of them is UTF-8.
static bool in_font(const char *text)
{
	static const char *const letters[] = {
		"\xC3\x84", "\xC3\x96", "\xC3\x9C", "\xC3\xA4", "\xC3\xB6", "\xC3\xBC", "\xC3\x9F", "\xE2\x80\x93", "\xE2\x80\xA6",
	};

	while(*text != '\0')
	{
		size_t bytes = 0;

		if((unsigned char)*text >= 0x20 && (unsigned char)*text <= 0x7E) bytes = 1;
		for(int i = 0; bytes == 0 && i < COUNT(letters); i++)
		{
			if(strncmp(text, letters[i], strlen(letters[i])) == 0) bytes = strlen(letters[i]);
		}
		if(bytes == 0) return false;
		text += bytes;
	}
	return true;
}

// A word for the browser: small letters and underscores, at least one
static bool is_word(const char *text)
{
	return text[0] != '\0' && text[strspn(text, "abcdefghijklmnopqrstuvwxyz_")] == '\0';
}

static bool same(const char *text, const char *expected)
{
	if(text != NULL && strcmp(text, expected) == 0) return true;
	printf("  got \"%s\", expected \"%s\"\n", text != NULL ? text : "(NULL)", expected);
	return false;
}

static bool is_member(int value, int members)
{
	return value >= 0 && value < members;
}

static const char *ring_name(ring_kind_t kind)
{
	static const char *const names[] = {"none", "yellow", "grey", "red", "progress"};

	return (unsigned)kind < 5u ? names[kind] : "?";
}

// A state of the adapter with this scan in it. What else the state says is nothing to the ring: `rest`
// false leaves all of it zero, true fills it.
static const wican_state_t *scan(wican_dtc_phase_t phase, uint32_t step, uint32_t total, bool rest)
{
	static wican_state_t state;

	memset(&state, 0, sizeof(state));
	if(rest)
	{
		strcpy(state.id, "a1b2c3d4e5f6");
		state.boot = 1234567890;
		state.up_s = 812;
		state.autopid = WICAN_AUTOPID_RUN;
		state.pids = 35;
		state.ecu_online = true;
		state.pass = 1234;
		state.rx_age_ms = 140;
		state.mqtt = WICAN_MQTT_DISCONNECTED;
		state.batt_mv = 12400;
		state.sleep_in_s = 90;
		state.heap = 61000;
		state.heap_min = 48000;
		state.dtc.supported = true;
		state.dtc.has_request = true;
		state.dtc.clear = true;
		state.dtc.from_http = true;
		state.dtc.seq = 43;
		strcpy(state.dtc.name, "N30/4 ESP");
		strcpy(state.dtc.reason, "engine_running");
		state.dtc.age_s = 7;
		state.dtc.count = 3;
		state.dtc.result_seq = 42;
	}
	state.dtc.phase = phase;
	state.dtc.step = step;
	state.dtc.total = total;
	return &state;
}

// The state of line `s` of a table of scans: none, or the scan with the rest of the state empty or filled
#define STATE_OF(table, s, rest)    ((table)[s].none ? NULL : scan((table)[s].phase, (table)[s].step, (table)[s].total, (rest) != 0))

static bool ring_is(conn_view_t view, const wican_state_t *state, int level, bool old, ring_kind_t kind, int permille)
{
	ring_t ring = ring_state(view, state, level, old);

	if(ring.kind == kind && ring.permille == permille) return true;
	printf("  view %d, level %d, %s: ring %s with %d, expected %s with %d\n", (int)view, level, old ? "old" : "fresh", ring_name(ring.kind), ring.permille,
	       ring_name(kind), permille);
	return false;
}

/* Nothing given ---------------------------------------------------------------------------------------- */

// The calls the header allows without a reason, with a word that is none and without a state, in a child
// process: a crash is then a failed check here, not the end of the test
static void test_nothing_given(void)
{
	int status = 0;
	pid_t child;

	fflush(stdout);
	child = fork();
	if(child == 0)
	{
		int wrong = 0;

		alarm(30);
		if(text_reason(NULL) != NULL || text_reason("") != NULL || text_reason("no_such_reason") != NULL) wrong++;
		for(int view = -1; view <= CONN_VIEW_LIVE + 1; view++)
		{
			if(ring_state((conn_view_t)view, NULL, 0, false).permille != 0) wrong++;
		}
		_exit(wrong == 0 ? 0 : 10);
	}
	if(child < 0 || waitpid(child, &status, 0) != child) status = -1;
	check(status != -1 && WIFEXITED(status), "no reason (NULL), a word that is none and no state (NULL): no crash");
	check(status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0, "no reason, a word that is none and no state: no text, and no progress of a ring for any view");
}

/* The texts -------------------------------------------------------------------------------------------- */

static void test_view(void)
{
	bool good = true;

	check(COUNT(VIEWS) == CONN_VIEW_LIVE + 1, "the test knows every view: CONN_VIEW_LIVE is the last of 10");
	for(int i = 0; i < COUNT(VIEWS); i++)
	{
		snprintf(what, sizeof(what), "text of the view %s is \"%s\"", VIEWS[i].name, VIEWS[i].text);
		check(VIEWS[i].view == (conn_view_t)i && same(text_view(VIEWS[i].view), VIEWS[i].text), what);
		snprintf(what, sizeof(what), "word of the view %s is \"%s\"", VIEWS[i].name, VIEWS[i].word);
		check(same(text_view_word(VIEWS[i].view), VIEWS[i].word), what);
	}

	check(same(text_view(CONN_VIEW_NO_WIFI), "WiCAN nicht gefunden \xE2\x80\x93 schl\xC3\xA4" "ft, stromlos oder au\xC3\x9F" "er Reichweite"),
	      "the bytes of the text for NO_WIFI: the dash is U+2013, a-umlaut and sharp s are two bytes each");
	check(strlen(text_view(CONN_VIEW_NO_WIFI)) == 66, "the text for NO_WIFI, the longest of all, has 66 bytes");
	check(same(text_view(CONN_VIEW_CONNECTING), "Verbinde mit WiCAN \xE2\x80\xA6"), "the bytes of the text for CONNECTING: the ellipsis is the one character U+2026");
	check(same(text_view(CONN_VIEW_ECU_OFFLINE), "Z\xC3\xBCndung aus \xE2\x80\x93 Motorsteuerger\xC3\xA4t offline"),
	      "the bytes of the text for ECU_OFFLINE: u-umlaut and a-umlaut are two bytes each");

	for(int value = -300; value <= 300; value++)
	{
		if(is_member(value, COUNT(VIEWS))) continue;
		if(strcmp(text_view((conn_view_t)value), "WiCAN antwortet nicht") != 0 || strcmp(text_view_word((conn_view_t)value), "no_answer") != 0) good = false;
	}
	check(good, "every number from -300 to 300 that is no view has the text and the word of NO_ANSWER");
	check(same(text_view((conn_view_t)10), "WiCAN antwortet nicht") && same(text_view_word((conn_view_t)10), "no_answer"),
	      "the number behind the last view has the text and the word of NO_ANSWER, not the ones of LIVE");
	check(same(text_view((conn_view_t)-1), "WiCAN antwortet nicht") && same(text_view_word((conn_view_t)-1), "no_answer"),
	      "the number before the first view has the text and the word of NO_ANSWER, not the ones of NO_WIFI");
	good = true;
	for(int i = 0; i < COUNT(OUTSIDE); i++)
	{
		if(strcmp(text_view((conn_view_t)OUTSIDE[i]), "WiCAN antwortet nicht") != 0 || strcmp(text_view_word((conn_view_t)OUTSIDE[i]), "no_answer") != 0) good = false;
	}
	check(good, "the largest and the smallest number and others far from the views have the text and the word of NO_ANSWER");
}

static void test_block(void)
{
	bool good = true;

	check(COUNT(BLOCKS) == DTC_FLOW_BUTTON_STUCK + 1, "the test knows every block: DTC_FLOW_BUTTON_STUCK is the last of 15");
	for(int i = 0; i < COUNT(BLOCKS); i++)
	{
		snprintf(what, sizeof(what), "text of the block %s is \"%s\"", BLOCKS[i].name, BLOCKS[i].text);
		check(BLOCKS[i].block == (dtc_flow_block_t)i && same(text_block(BLOCKS[i].block), BLOCKS[i].text), what);
	}
	check(same(text_block(DTC_FLOW_BUTTON_STUCK), "Knopf klemmt \xE2\x80\x93 L\xC3\xB6schen gesperrt"),
	      "the bytes of the text for BUTTON_STUCK: the dash is U+2013, o-umlaut is two bytes");

	for(int value = -300; value <= 300; value++)
	{
		if(is_member(value, COUNT(BLOCKS))) continue;
		if(strcmp(text_block((dtc_flow_block_t)value), "WiCAN nicht erreichbar") != 0) good = false;
	}
	check(good, "every number from -300 to 300 that is no block has the text of NO_ADAPTER");
	check(same(text_block((dtc_flow_block_t)15), "WiCAN nicht erreichbar"), "the number behind the last block has the text of NO_ADAPTER, not the one of BUTTON_STUCK");
	check(same(text_block((dtc_flow_block_t)-1), "WiCAN nicht erreichbar"), "the number before the first block has the text of NO_ADAPTER, not the empty one of ALLOWED");
	good = true;
	for(int i = 0; i < COUNT(OUTSIDE); i++)
	{
		if(strcmp(text_block((dtc_flow_block_t)OUTSIDE[i]), "WiCAN nicht erreichbar") != 0) good = false;
	}
	check(good, "the largest and the smallest number and others far from the blocks have the text of NO_ADAPTER");
}

// true if every word of the list has a text
static bool all_have_texts(const char *const *words, int count)
{
	bool good = true;

	for(int i = 0; i < count; i++)
	{
		if(text_reason(words[i]) == NULL || text_reason(words[i])[0] == '\0')
		{
			printf("  \"%s\" has no text\n", words[i]);
			good = false;
		}
	}
	return good;
}

static void test_reason(void)
{
	static const char *const unknown[] = {
		"unknown", "timeout", "accepted", "ok", "done", "error", "no_wifi", "live", "BUSY", "Busy", "busy\n", "busy,expired", "b", "_", "-", " ",
		"Scan läuft bereits (anderes Gerät)", "no answer", "no-answer", "noanswer",
	};
	bool good = true;

	for(int i = 0; i < COUNT(REASONS); i++)
	{
		// From memory of its own: the word counts, not where it is stored
		char word[32];

		snprintf(word, sizeof(word), "%s", REASONS[i].word);
		snprintf(what, sizeof(what), "text of the reason %s is \"%s\"", REASONS[i].word, REASONS[i].text);
		check(same(text_reason(word), REASONS[i].text), what);
	}
	check(same(text_reason("engine_state_unknown"), "Drehzahl nicht lesbar \xE2\x80\x93 nichts gel\xC3\xB6scht"),
	      "the bytes of the text for engine_state_unknown: the dash is U+2013, o-umlaut is two bytes");

	check(all_have_texts(REASONS_OF_STATE, COUNT(REASONS_OF_STATE)), "every reason API.md names for the dtc object of the state has a text");
	check(all_have_texts(REASONS_OF_ANSWERS, COUNT(REASONS_OF_ANSWERS)), "every reason API.md names for the answers to the dtc requests has a text");
	check(all_have_texts(REASONS_OF_FIRMWARE, COUNT(REASONS_OF_FIRMWARE)), "every reason the firmware of the adapter writes (dtc_state.c, dtc_api.c, dtc_http.c, autopid.c) has a text");
	check(all_have_texts(REASONS_OF_FLOW, COUNT(REASONS_OF_FLOW)), "every reason dtc_flow.h adds has a text");

	check(text_reason(NULL) == NULL, "no reason (NULL) has no text");
	check(text_reason("") == NULL, "the empty reason has no text");
	for(int i = 0; i < COUNT(unknown); i++)
	{
		if(text_reason(unknown[i]) != NULL)
		{
			printf("  \"%s\" has the text \"%s\"\n", unknown[i], text_reason(unknown[i]));
			good = false;
		}
	}
	check(good, "words that are no reason, reasons in capital letters, with a line break or with other punctuation have no text");

	// Every word shortened, lengthened and changed
	good = true;
	for(int i = 0; i < COUNT(REASONS); i++)
	{
		char word[40];
		size_t length = strlen(REASONS[i].word);

		snprintf(word, sizeof(word), "%sx", REASONS[i].word);
		if(text_reason(word) != NULL) good = false;
		snprintf(word, sizeof(word), "%s ", REASONS[i].word);
		if(text_reason(word) != NULL) good = false;
		snprintf(word, sizeof(word), " %s", REASONS[i].word);
		if(text_reason(word) != NULL) good = false;
		snprintf(word, sizeof(word), "%.*s", (int)length - 1, REASONS[i].word);
		if(text_reason(word) != NULL) good = false;
		snprintf(word, sizeof(word), "%s", REASONS[i].word + 1);
		if(text_reason(word) != NULL) good = false;
		snprintf(word, sizeof(word), "%s", REASONS[i].word);
		word[length - 1]++;
		if(text_reason(word) != NULL) good = false;
		snprintf(word, sizeof(word), "%s", REASONS[i].word);
		word[0]--;
		if(text_reason(word) != NULL) good = false;
		if(!good)
		{
			printf("  a word next to \"%s\" has a text\n", REASONS[i].word);
			break;
		}
	}
	check(good, "a reason with a letter or a blank more, with its first or last letter missing or changed has no text");
}

static void test_heat(void)
{
	bool good = true;

	check(same(text_heat_word(GUARD_HEAT_NORMAL), "normal"), "word of the heat level NORMAL is \"normal\"");
	check(same(text_heat_word(GUARD_HEAT_DIM), "dim"), "word of the heat level DIM is \"dim\"");
	check(same(text_heat_word(GUARD_HEAT_OFF), "off"), "word of the heat level OFF is \"off\"");
	check(GUARD_HEAT_OFF == 2, "the test knows every heat level: GUARD_HEAT_OFF is the last of 3");
	for(int value = -300; value <= 300; value++)
	{
		if(is_member(value, 3)) continue;
		if(strcmp(text_heat_word((guard_heat_t)value), "off") != 0) good = false;
	}
	check(good, "every number from -300 to 300 that is no heat level has the word \"off\"");
	check(same(text_heat_word((guard_heat_t)3), "off") && same(text_heat_word((guard_heat_t)-1), "off"),
	      "the numbers behind the last and before the first heat level have the word \"off\", not \"normal\"");
	good = true;
	for(int i = 0; i < COUNT(OUTSIDE); i++)
	{
		if(strcmp(text_heat_word((guard_heat_t)OUTSIDE[i]), "off") != 0) good = false;
	}
	check(good, "the largest and the smallest number and others far from the heat levels have the word \"off\"");
}

// Every text the module can return fits the screen and the font
static void test_every_text(void)
{
	bool fit = true, font = true, words = true;
	size_t longest = 0;
	int texts = 0;

	check(TEXT_MAX == 79, "a text has at most 79 bytes");
	for(int value = -3; value <= 20; value++)
	{
		const char *all[] = {
			text_view((conn_view_t)value), text_block((dtc_flow_block_t)value), is_member(value, COUNT(REASONS)) ? text_reason(REASONS[value].word) : "",
		};

		for(int i = 0; i < COUNT(all); i++)
		{
			if(all[i] == NULL || strlen(all[i]) > TEXT_MAX)
			{
				printf("  a text for the number %d is missing or too long\n", value);
				fit = false;
				continue;
			}
			if(!in_font(all[i]))
			{
				printf("  \"%s\" has a character the font does not have\n", all[i]);
				font = false;
			}
			if(strlen(all[i]) > longest) longest = strlen(all[i]);
			texts++;
		}
		if(!is_word(text_view_word((conn_view_t)value)) || !is_word(text_heat_word((guard_heat_t)value))) words = false;
	}
	check(fit && texts == 72, "every text of a view, a block and a reason, also for numbers that are none, has at most TEXT_MAX bytes");
	check(longest == 66, "the longest of all texts has 66 bytes");
	check(font, "every text is UTF-8 of the characters the font has: ASCII, the German letters, the en dash and the ellipsis");
	check(words, "every word for the browser is made of small letters and underscores");
	check(!in_font("a\xC2\xB7") && !in_font("\xC3") && !in_font("a\nb") && !in_font("\x7F") && !in_font("e\xCC\x81") && in_font("~ \xC3\x9F\xE2\x80\xA6"),
	      "the check of the font itself: no middle dot, no half character, no control character, no combining accent");
}

/* The ring --------------------------------------------------------------------------------------------- */

static void test_ring(void)
{
	static const int levels[] = {INT_MIN, -1, 0, 1, 2, 3, INT_MAX};
	// What a live page shows, by the worst level of its values and whether one of them is old
	static const struct
	{
		int level;
		ring_kind_t fresh;
		ring_kind_t old;
		const char *name;
	} live[] = {
		{INT_MIN, RING_NONE, RING_YELLOW, "the smallest level there is counts as none: nothing, yellow if a value is old"},
		{-1, RING_NONE, RING_YELLOW, "level -1 counts as none: nothing, yellow if a value is old"},
		{0, RING_NONE, RING_YELLOW, "level 0: nothing, yellow if a value is old"},
		{1, RING_YELLOW, RING_YELLOW, "level 1: yellow, old or not"},
		{2, RING_RED, RING_RED, "level 2: red, old or not"},
		{3, RING_RED, RING_RED, "level 3 is no less than level 2: red, old or not"},
		{INT_MAX, RING_RED, RING_RED, "the largest level there is: red, old or not"},
	};
	// What a scan shows, by the state of the adapter
	static const struct
	{
		bool none;
		wican_dtc_phase_t phase;
		uint32_t step;
		uint32_t total;
		int permille;
		const char *name;
	} scans[] = {
		{true, WICAN_DTC_IDLE, 0, 0, 0, "without a state: 0"},
		{false, WICAN_DTC_QUEUED, 0, 0, 0, "queued, step 0 of 0: 0"},
		{false, WICAN_DTC_QUEUED, 5, 18, 0, "queued with counters left from a scan before, 5 of 18: 0"},
		{false, WICAN_DTC_QUEUED, 18, 18, 0, "queued with counters left from a scan before, 18 of 18: 0"},
		{false, WICAN_DTC_RUNNING, 0, 0, 0, "running, step 0 of 0: 0, nothing is divided"},
		{false, WICAN_DTC_RUNNING, 5, 0, 0, "running, step 5 of 0: 0, nothing is divided"},
		{false, WICAN_DTC_RUNNING, 0, 18, 0, "running, the engine check, step 0 of 18: 0"},
		{false, WICAN_DTC_RUNNING, 1, 18, 55, "running, step 1 of 18: 55"},
		{false, WICAN_DTC_RUNNING, 5, 18, 277, "running, step 5 of 18: 277"},
		{false, WICAN_DTC_RUNNING, 9, 18, 500, "running, step 9 of 18: 500"},
		{false, WICAN_DTC_RUNNING, 17, 18, 944, "running, step 17 of 18: 944"},
		{false, WICAN_DTC_RUNNING, 18, 18, 1000, "running, step 18 of 18: 1000"},
		{false, WICAN_DTC_RUNNING, 19, 18, 1000, "running, step 19 of 18: at most 1000"},
		{false, WICAN_DTC_RUNNING, 4294967295u, 18, 1000, "running, the largest step of 18: at most 1000"},
		{false, WICAN_DTC_RUNNING, 1, 1, 1000, "running, step 1 of 1: 1000"},
		{false, WICAN_DTC_RUNNING, 0, 1, 0, "running, step 0 of 1: 0"},
		{false, WICAN_DTC_RUNNING, 1, 3, 333, "running, step 1 of 3: 333, rounded down"},
		{false, WICAN_DTC_RUNNING, 2, 3, 666, "running, step 2 of 3: 666, rounded down"},
		{false, WICAN_DTC_RUNNING, 999, 1000, 999, "running, step 999 of 1000: 999"},
		{false, WICAN_DTC_RUNNING, 1, 1000, 1, "running, step 1 of 1000: 1"},
		{false, WICAN_DTC_RUNNING, 1, 1001, 0, "running, step 1 of 1001: 0"},
		{false, WICAN_DTC_RUNNING, 1000, 1001, 999, "running, step 1000 of 1001: 999, not yet 1000"},
		{false, WICAN_DTC_RUNNING, 4294967, 4294967295u, 0, "running, step 4294967 of 2^32-1: 0"},
		{false, WICAN_DTC_RUNNING, 4294968, 4294967295u, 1, "running, step 4294968 of 2^32-1: 1, the step times 1000 does not fit into 32 bit"},
		{false, WICAN_DTC_RUNNING, 2147483648u, 4294967295u, 500, "running, step 2^31 of 2^32-1: 500"},
		{false, WICAN_DTC_RUNNING, 3000000000u, 4000000000u, 750, "running, step 3000000000 of 4000000000: 750"},
		{false, WICAN_DTC_RUNNING, 4294967294u, 4294967295u, 999, "running, step 2^32-2 of 2^32-1: 999"},
		{false, WICAN_DTC_RUNNING, 4294967295u, 4294967295u, 1000, "running, step 2^32-1 of 2^32-1: 1000"},
		{false, WICAN_DTC_RUNNING, 1, 4294967295u, 0, "running, step 1 of 2^32-1: 0"},
		{false, WICAN_DTC_IDLE, 5, 18, 277, "idle, step 5 of 18: 277, only a queued scan is told apart"},
		{false, WICAN_DTC_DONE, 18, 18, 1000, "done, step 18 of 18: 1000"},
		{false, WICAN_DTC_ERROR, 3, 18, 166, "error, step 3 of 18: 166"},
	};
	bool good;

	// Every view that is not live and no scan: the same ring whatever else is given
	for(int v = 0; v < COUNT(VIEWS); v++)
	{
		if(VIEWS[v].view == CONN_VIEW_LIVE || VIEWS[v].view == CONN_VIEW_SCAN) continue;
		good = true;
		for(int l = 0; l < COUNT(levels); l++)
		{
			for(int old = 0; old <= 1; old++)
			{
				for(int s = 0; s < 2 * COUNT(scans); s++)
				{
					good = ring_is(VIEWS[v].view, STATE_OF(scans, s / 2, s % 2), levels[l], old != 0, VIEWS[v].ring, 0) && good;
				}
			}
		}
		snprintf(what, sizeof(what), "ring of the view %s is %s with permille 0, for every level, old or not, with every state and without one",
		         VIEWS[v].name, ring_name(VIEWS[v].ring));
		check(good, what);
	}

	// Live: the level and the age of the values decide, the state does not
	for(int l = 0; l < COUNT(live); l++)
	{
		good = true;
		for(int s = 0; s < 2 * COUNT(scans); s++)
		{
			const wican_state_t *state = STATE_OF(scans, s / 2, s % 2);

			good = ring_is(CONN_VIEW_LIVE, state, live[l].level, false, live[l].fresh, 0) && ring_is(CONN_VIEW_LIVE, state, live[l].level, true, live[l].old, 0) && good;
		}
		snprintf(what, sizeof(what), "ring of the view LIVE, %s; permille 0, with every state and without one", live[l].name);
		check(good, what);
	}

	// A scan: the state decides, level and age do not
	for(int s = 0; s < COUNT(scans); s++)
	{
		good = true;
		for(int l = 0; l < COUNT(levels); l++)
		{
			for(int old = 0; old <= 1; old++)
			{
				good = ring_is(CONN_VIEW_SCAN, STATE_OF(scans, s, 0), levels[l], old != 0, RING_PROGRESS, scans[s].permille) &&
				       ring_is(CONN_VIEW_SCAN, STATE_OF(scans, s, 1), levels[l], old != 0, RING_PROGRESS, scans[s].permille) && good;
			}
		}
		snprintf(what, sizeof(what), "ring of the view SCAN is the progress, %s; for every level, old or not, whatever else the state says", scans[s].name);
		check(good, what);
	}

	// Numbers that are no view
	good = true;
	for(int value = -300; value <= 300; value++)
	{
		if(is_member(value, COUNT(VIEWS))) continue;
		for(int l = 0; l < COUNT(levels); l++)
		{
			good = ring_is((conn_view_t)value, NULL, levels[l], false, RING_RED, 0) && ring_is((conn_view_t)value, scan(WICAN_DTC_RUNNING, 5, 18, true), levels[l], true, RING_RED, 0) && good;
		}
	}
	check(good, "ring of every number from -300 to 300 that is no view is red like NO_ANSWER, for every level, with and without a state");
	check(ring_is((conn_view_t)10, NULL, 0, false, RING_RED, 0) && ring_is((conn_view_t)-1, NULL, 0, false, RING_RED, 0),
	      "ring of the numbers behind the last and before the first view is red, not the nothing of a live page without warnings");
	good = true;
	for(int i = 0; i < COUNT(OUTSIDE); i++)
	{
		good = ring_is((conn_view_t)OUTSIDE[i], scan(WICAN_DTC_RUNNING, 5, 18, false), 0, false, RING_RED, 0) && good;
	}
	check(good, "ring of the largest and the smallest number and others far from the views is red");

	// The progress a second way, without dividing: the largest number whose product with the total is not
	// above the step times 1000
	good = true;
	for(uint32_t total = 1; total <= 40; total++)
	{
		for(uint32_t step = 0; step <= total + 2; step++)
		{
			int permille = 0;

			while(permille < 1000 && (uint64_t)(permille + 1) * total <= (uint64_t)step * 1000) permille++;
			good = ring_is(CONN_VIEW_SCAN, scan(WICAN_DTC_RUNNING, step, total, step % 2 == 0), 0, false, RING_PROGRESS, permille) && good;
		}
	}
	check(good, "progress of every step from 0 to two behind the end for totals of 1 to 40: step times 1000 by total, rounded down, at most 1000");
}

int main(void)
{
	test_nothing_given();
	test_view();
	test_block();
	test_reason();
	test_heat();
	test_every_text();
	test_ring();
	return test_end();
}
