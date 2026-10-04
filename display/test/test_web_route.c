/*
 * Host test for display/components/core/web_route.c. Run "make test_web_route && ./test_web_route" in
 * display/test. redproof.py removes or weakens every rule once (mutations/web_route.py) and expects this
 * test to fail.
 *
 * Every example starts from a request that passes all checks of its route: sent to the address of the
 * display, with the header, a Content-Length of 1, the release open, the display idle. Then one thing is
 * changed.
 *
 * The module reads texts that come from the network. Every call of it is made in a child process: a
 * change that lets it read behind the end of a text, use a pointer that is NULL or never come back is
 * then a failed check, and not the end of the test without one.
 */
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "test.h"
#include "web_route.h"
// Only for LAYOUT_TEXT_MAX: the limit of a layout body is written a second time in web_route.h
#include "layout.h"

#define COUNT(list)     (sizeof(list) / sizeof((list)[0]))
#define GUARD           0x5A
#define SLOT            4194304u    // an app slot of 4 MB, as in display/partitions.csv

// The text of a check that is made for every line of a list
static char text[400];

// `bytes` for a check text: what is no printable ASCII is written as \xNN, so that every check stays one line
static const char *shown(const char *bytes)
{
	static char texts[2][700];
	static int next = 0;
	char *out = texts[next++ % 2];
	size_t length = 0;

	for(; *bytes != '\0' && length + 5 < sizeof(texts[0]); bytes++)
	{
		unsigned char byte = (unsigned char)*bytes;

		if(byte >= 0x20 && byte <= 0x7E) out[length++] = (char)byte;
		else length += (size_t)snprintf(out + length, 5, "\\x%02X", byte);
	}
	out[length] = '\0';
	return out;
}

static web_request_t request(web_method_t method, const char *path, const char *query)
{
	web_request_t r;

	memset(&r, 0, sizeof(r));
	r.method = method;
	r.path = path;
	r.query = query;
	r.header = "1";
	r.host = "192.168.4.1";
	r.has_length = true;
	r.length = 1;
	r.release_open = true;
	r.busy = false;
	r.slot_size = SLOT;
	return r;
}

static bool same_text(const char *a, const char *b)
{
	return (a == NULL && b == NULL) || (a != NULL && b != NULL && strcmp(a, b) == 0);
}

static bool goes_on(web_decision_t decision, web_route_t route, bool changes, bool knob, uint32_t ticket)
{
	return decision.route == route && decision.status == 0 && decision.error == NULL && decision.changes == changes && decision.knob == knob &&
	       decision.ticket == ticket;
}

// A refusal carries nothing but its status and its word
static bool is_refused(web_decision_t decision, int status, const char *word)
{
	return decision.route == WEB_ROUTE_NONE && decision.status == status && same_text(decision.error, word) && !decision.changes && !decision.knob &&
	       decision.ticket == 0;
}

#define SCENE_GOOD      0
#define SCENE_WRONG     10
#define SCENE_SHORT     20      // seconds for a scene of a few calls
#define SCENE_LONG      120     // seconds for a scene of many thousand calls

static bool scene_died = false;

// Runs `scene` in a child process: a crash or a hang there is a failed check here and not the end of the
// test. Returns what the scene returned, -1 if it did not get that far within `seconds`. A scene never
// returns 1: with that the sanitizers end a program on Linux.
// After a scene that did not get to its end the test ends at the next one: the module crashes or hangs,
// the scenes that follow would wait for it again, each for its seconds, and redproof.py does not wait
// that long.
static int in_child(int (*scene)(void), unsigned seconds)
{
	int status = 0;
	pid_t child;

	if(scene_died)
	{
		check(false, "a scene crashed or hung: the scenes behind it are not run, the test ends here");
		exit(test_end());
	}

	fflush(stdout);
	child = fork();
	if(child == 0)
	{
		int result;

		alarm(seconds);
		result = scene();
		fflush(stdout);
		_exit(result);
	}
	if(child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) == 1)
	{
		scene_died = true;
		return -1;
	}
	return WEXITSTATUS(status);
}

/* ------------------------------------------------------------------------------------------------ */
/* What may be NULL                                                                                   */
/* ------------------------------------------------------------------------------------------------ */

static int scene_no_path(void)
{
	static const web_method_t methods[] = {WEB_GET, WEB_PUT, WEB_POST, WEB_OTHER};
	size_t i;

	for(i = 0; i < COUNT(methods); i++)
	{
		web_request_t r = request(methods[i], NULL, "");

		if(!is_refused(web_route(&r), 404, "not_found")) return SCENE_WRONG;
	}
	return SCENE_GOOD;
}

static int scene_no_query(void)
{
	web_request_t get = request(WEB_GET, "/api/info", NULL);
	web_request_t post = request(WEB_POST, "/api/settings", NULL);

	return goes_on(web_route(&get), WEB_ROUTE_INFO, false, false, 0) && goes_on(web_route(&post), WEB_ROUTE_SETTINGS, true, false, 0) ? SCENE_GOOD : SCENE_WRONG;
}

static int scene_no_query_where_one_is_needed(void)
{
	web_request_t layout = request(WEB_PUT, "/api/layout", NULL);
	web_request_t ticket = request(WEB_GET, "/api/ticket", NULL);

	return is_refused(web_route(&layout), 400, "query") && is_refused(web_route(&ticket), 400, "query") ? SCENE_GOOD : SCENE_WRONG;
}

static int scene_no_header(void)
{
	web_request_t post = request(WEB_POST, "/api/settings", "");
	web_request_t put = request(WEB_PUT, "/api/layout", "mode=check");

	post.header = NULL;
	put.header = NULL;
	return is_refused(web_route(&post), 403, "header") && is_refused(web_route(&put), 403, "header") ? SCENE_GOOD : SCENE_WRONG;
}

static int scene_no_host(void)
{
	web_request_t get = request(WEB_GET, "/api/info", "");
	web_request_t post = request(WEB_POST, "/api/settings", "");

	get.host = NULL;
	post.host = NULL;
	return !web_host_allowed(NULL) && is_refused(web_route(&get), 403, "host") && is_refused(web_route(&post), 403, "host") ? SCENE_GOOD : SCENE_WRONG;
}

static int scene_no_word(void)
{
	unsigned char buffer[WEB_ERROR_SIZE + 8];
	size_t size, i;

	// Every size from none to WEB_ERROR_SIZE: an empty text where there is room for one, nothing else touched
	for(size = 0; size <= WEB_ERROR_SIZE; size++)
	{
		memset(buffer, GUARD, sizeof(buffer));
		if(web_error_body(NULL, (char *)buffer, size) != -1 || buffer[0] != (size > 0 ? '\0' : GUARD)) return SCENE_WRONG;
		for(i = 1; i < sizeof(buffer); i++)
		{
			if(buffer[i] != GUARD) return SCENE_WRONG;
		}
	}
	return SCENE_GOOD;
}

static int scene_no_buffer(void)
{
	return web_error_body("busy", NULL, 0) == -1 && web_error_body("locked", NULL, 0) == -1 && web_error_body(NULL, NULL, 0) == -1 ? SCENE_GOOD : SCENE_WRONG;
}

// A path of the list with a method that has no line there: the module holds no line of the list then, and
// must not use one
static int scene_no_line(void)
{
	web_request_t put = request(WEB_PUT, "/api/info", "");
	web_request_t other = request(WEB_OTHER, "/api/reboot", "");
	web_request_t get = request(WEB_GET, "/api/layout/reset", "");

	return is_refused(web_route(&put), 405, "method") && is_refused(web_route(&other), 405, "method") && is_refused(web_route(&get), 405, "method") ? SCENE_GOOD
	                                                                                                                                                   : SCENE_WRONG;
}

static void test_null(void)
{
	check(in_child(scene_no_path, SCENE_SHORT) == SCENE_GOOD, "a NULL path counts as an unknown path: 404 not_found with every method");
	check(in_child(scene_no_query, SCENE_SHORT) == SCENE_GOOD, "a NULL query is no query: a route that takes none goes on");
	check(in_child(scene_no_query_where_one_is_needed, SCENE_SHORT) == SCENE_GOOD, "a NULL query where a mode or an id is needed: 400 query");
	check(in_child(scene_no_header, SCENE_SHORT) == SCENE_GOOD, "a NULL header is a missing header: 403 header for a request that is not a GET");
	check(in_child(scene_no_host, SCENE_SHORT) == SCENE_GOOD, "a NULL host is a missing Host header: not allowed, 403 host");
	check(in_child(scene_no_word, SCENE_SHORT) == SCENE_GOOD, "a NULL word has no body: -1 and an empty text with every size from 1 on, nothing written with size 0");
	check(in_child(scene_no_buffer, SCENE_SHORT) == SCENE_GOOD, "with size 0 the buffer may be NULL: -1, nothing written");
	check(in_child(scene_no_line, SCENE_SHORT) == SCENE_GOOD, "a path of the list with a method that has no line there is refused before a line is used: 405 method");
}

/* ------------------------------------------------------------------------------------------------ */
/* Where a text ends                                                                                  */
/* ------------------------------------------------------------------------------------------------ */

// The bytes behind the zero of a text are not part of it. Here they hold what the text lacks, or what
// spoils it: a reader that runs over the end takes a text that has to be refused, or refuses one that has
// to be taken, and that is a failed check on every platform - no sanitizer has to catch the read. Where it
// does catch it, or where the reader runs on and on, the child process ends and the check fails as well.
#define READ_REFUSED    20
#define READ_TAKEN      30      // a host or a mode; a ticket: plus its number, which is below 10 here

typedef enum
{
	AS_TICKET,      // the query of GET /api/ticket
	AS_MODE,        // the query of PUT /api/layout
	AS_HOST,
} read_as_t;

static const char *scene_text;
static read_as_t scene_as;

static int scene_read(void)
{
	web_decision_t decision;
	web_request_t r;

	switch(scene_as)
	{
		case AS_TICKET:
			r = request(WEB_GET, "/api/ticket", scene_text);
			decision = web_route(&r);
			if(is_refused(decision, 400, "query")) return READ_REFUSED;
			return decision.ticket < 10 && goes_on(decision, WEB_ROUTE_TICKET, false, false, decision.ticket) ? READ_TAKEN + (int)decision.ticket : SCENE_WRONG;
		case AS_MODE:
			r = request(WEB_PUT, "/api/layout", scene_text);
			decision = web_route(&r);
			if(is_refused(decision, 400, "query")) return READ_REFUSED;
			return goes_on(decision, WEB_ROUTE_LAYOUT_SAVE, true, false, 0) ? READ_TAKEN : SCENE_WRONG;
		default:
			return web_host_allowed(scene_text) ? READ_TAKEN : READ_REFUSED;
	}
}

static int read_as(read_as_t as, const char *bytes)
{
	scene_as = as;
	scene_text = bytes;
	return in_child(scene_read, SCENE_SHORT);
}

#define CUT_NAME        1       // a cut name of the display was allowed
#define CUT_ADDRESS     2       // a cut address was allowed
#define CUT_MODE        4       // a cut mode was taken
#define CUT_ID          8       // a cut id was taken

// Each text with a zero in the place of one of its bytes: what follows the zero is the rest of a good text.
// Returns SCENE_GOOD or 16 plus the CUT_ bits.
static int scene_cut_short(void)
{
	static const char name[] = "wican-display.local", address[] = "1.2.3.4:80", mode[] = "mode=save", id[] = "id=7";
	char cut[sizeof(name)];
	web_request_t r;
	int wrong = 0;
	size_t at;

	for(at = 0; at < sizeof(name) - 1; at++)
	{
		memcpy(cut, name, sizeof(name));
		cut[at] = '\0';
		if(web_host_allowed(cut)) wrong |= CUT_NAME;
	}
	// Not at the colon and not at the last digit: "1.2.3.4" is an address, "1.2.3.4:8" one with a port
	for(at = 0; at < sizeof(address) - 1; at++)
	{
		if(at == 7 || at == 9) continue;

		memcpy(cut, address, sizeof(address));
		cut[at] = '\0';
		if(web_host_allowed(cut)) wrong |= CUT_ADDRESS;
	}
	for(at = 0; at < sizeof(mode) - 1; at++)
	{
		memcpy(cut, mode, sizeof(mode));
		cut[at] = '\0';
		r = request(WEB_PUT, "/api/layout", cut);
		if(!is_refused(web_route(&r), 400, "query")) wrong |= CUT_MODE;
	}
	for(at = 0; at < sizeof(id) - 1; at++)
	{
		memcpy(cut, id, sizeof(id));
		cut[at] = '\0';
		r = request(WEB_GET, "/api/ticket", cut);
		if(!is_refused(web_route(&r), 400, "query")) wrong |= CUT_ID;
	}
	return wrong != 0 ? 16 + wrong : SCENE_GOOD;
}

static void test_behind_the_end(void)
{
	static const char id_alone[] = "id\0" "7";
	static const char id_without_number[] = "id=\0" "7";
	static const char mode_cut[] = "mode=\0" "save";
	static const char name_cut[] = "wican-display.loca\0" ":80";
	static const char name_cut_twice[] = "wican-display.loca\0";
	static const char name_without_local[] = "wican-display\0" "local";
	static const char address_cut[] = "1.2.3\0" "4";
	static const char address_without_last[] = "1.2.3.\0" "4";
	static const char port_cut[] = "1.2.3.4:\0" "80";
	static const char no_host[] = "\0" "ican-display.local";
	static const char id_and_digit[] = "id=7\0" "8";
	static const char id_and_letter[] = "id=7\0" "x";
	static const char mode_and_letter[] = "mode=save\0" "d";
	static const char name_and_letter[] = "wican-display.local\0" "x";
	static const char name_and_two_letters[] = "wican-display.local\0" "xx";
	static const char name_port_and_digit[] = "wican-display.local:65535\0" "0";
	static const char address_and_digit[] = "1.2.3.25\0" "6";
	static const char address_and_number[] = "1.2.3.4\0" ".5";
	static const char address_port_and_digit[] = "1.2.3.4:65535\0" "0";
	static const char address_port_and_letter[] = "1.2.3.4:80\0" "x";
	int result;

	check(read_as(AS_TICKET, id_alone) == READ_REFUSED, "the query \"id\" ends at its zero: a number behind the end is not read");
	check(read_as(AS_TICKET, id_without_number) == READ_REFUSED, "the query \"id=\" ends at its zero: a number behind the end is not read");
	check(read_as(AS_MODE, mode_cut) == READ_REFUSED, "the query \"mode=\" ends at its zero: a mode behind the end is not read");
	check(read_as(AS_HOST, name_cut) == READ_REFUSED, "the host \"wican-display.loca\" ends at its zero: a port behind the end is not read");
	check(read_as(AS_HOST, name_cut_twice) == READ_REFUSED,
	      "the host \"wican-display.loca\" ends at its zero: a second zero behind the end is not taken for the end of the name");
	check(read_as(AS_HOST, name_without_local) == READ_REFUSED, "the host \"wican-display\" ends at its zero: the rest of the name behind the end is not read");
	check(read_as(AS_HOST, address_cut) == READ_REFUSED, "the host \"1.2.3\" ends at its zero: a fourth number behind the end is not read");
	check(read_as(AS_HOST, address_without_last) == READ_REFUSED, "the host \"1.2.3.\" ends at its zero: a fourth number behind the end is not read");
	check(read_as(AS_HOST, port_cut) == READ_REFUSED, "the host \"1.2.3.4:\" ends at its zero: a port behind the end is not read");
	check(read_as(AS_HOST, no_host) == READ_REFUSED, "an empty host ends at its zero: the rest of the name behind the end is not read");

	// A text that is taken stays what it is, whatever stands behind its end
	check(read_as(AS_TICKET, id_and_digit) == READ_TAKEN + 7, "the query \"id=7\" ends at its zero: a digit behind the end does not make it the ticket 78");
	check(read_as(AS_TICKET, id_and_letter) == READ_TAKEN + 7, "the query \"id=7\" ends at its zero: a letter behind the end does not spoil it");
	check(read_as(AS_MODE, mode_and_letter) == READ_TAKEN, "the query \"mode=save\" ends at its zero: a letter behind the end does not spoil it");
	check(read_as(AS_HOST, name_and_letter) == READ_TAKEN, "the host \"wican-display.local\" ends at its zero: a letter behind the end does not spoil it");
	check(read_as(AS_HOST, name_and_two_letters) == READ_TAKEN, "the host \"wican-display.local\" ends at its zero: two letters behind the end do not spoil it");
	check(read_as(AS_HOST, name_port_and_digit) == READ_TAKEN, "the host \"wican-display.local:65535\" ends at its zero: a sixth digit behind the end does not spoil it");
	check(read_as(AS_HOST, address_and_digit) == READ_TAKEN, "the host \"1.2.3.25\" ends at its zero: a digit behind the end does not make its last number 256");
	check(read_as(AS_HOST, address_and_number) == READ_TAKEN, "the host \"1.2.3.4\" ends at its zero: a fifth number behind the end does not spoil it");
	check(read_as(AS_HOST, address_port_and_digit) == READ_TAKEN, "the host \"1.2.3.4:65535\" ends at its zero: a sixth digit behind the end does not spoil it");
	check(read_as(AS_HOST, address_port_and_letter) == READ_TAKEN, "the host \"1.2.3.4:80\" ends at its zero: a letter behind the end does not spoil it");

	result = in_child(scene_cut_short, SCENE_SHORT);
	check(result != -1 && (result & CUT_NAME) == 0, "the name of the display cut short at each of its 19 bytes is refused, whatever stands behind the end");
	check(result != -1 && (result & CUT_ADDRESS) == 0, "the address 1.2.3.4:80 cut short within its numbers or behind its colon is refused, whatever stands behind the end");
	check(result != -1 && (result & CUT_MODE) == 0, "the query mode=save cut short at each of its 9 bytes names no mode, whatever stands behind the end");
	check(result != -1 && (result & CUT_ID) == 0, "the query id=7 cut short at each of its 4 bytes names no ticket, whatever stands behind the end");
}

/* ------------------------------------------------------------------------------------------------ */
/* The list of the header                                                                             */
/* ------------------------------------------------------------------------------------------------ */

typedef struct
{
	const char *name;
	web_method_t method;
	const char *path;
	const char *query;      // the query the route needs, "" if it takes none
	const char *bad_query;  // one it does not take
	web_route_t route;
	bool changes;
	bool knob;
	bool rests;             // refused while the display is busy
	uint32_t limit;         // the largest Content-Length, 0 for a GET: it has none
	uint32_t ticket;
} row_t;

// Written down from the comment of the header, line by line
static const row_t rows[] = {
	{"GET /",                      WEB_GET,  "/",                 "",           "x=1",        WEB_ROUTE_PAGE,         false, false, false, 0,     0},
	{"GET /api/info",              WEB_GET,  "/api/info",         "",           "x=1",        WEB_ROUTE_INFO,         false, false, false, 0,     0},
	{"GET /api/catalog",           WEB_GET,  "/api/catalog",      "",           "x=1",        WEB_ROUTE_CATALOG,      false, false, false, 0,     0},
	{"GET /api/values",            WEB_GET,  "/api/values",       "",           "x=1",        WEB_ROUTE_VALUES,       false, false, false, 0,     0},
	{"GET /api/layout",            WEB_GET,  "/api/layout",       "",           "mode=check", WEB_ROUTE_LAYOUT,       false, false, false, 0,     0},
	{"PUT /api/layout?mode=check", WEB_PUT,  "/api/layout",       "mode=check", "",           WEB_ROUTE_LAYOUT_CHECK, false, false, false, 16384, 0},
	{"PUT /api/layout?mode=apply", WEB_PUT,  "/api/layout",       "mode=apply", "mode=appl",  WEB_ROUTE_LAYOUT_APPLY, true,  false, false, 16384, 0},
	{"PUT /api/layout?mode=save",  WEB_PUT,  "/api/layout",       "mode=save",  "mode=saved", WEB_ROUTE_LAYOUT_SAVE,  true,  false, false, 16384, 0},
	{"POST /api/layout/reset",     WEB_POST, "/api/layout/reset", "",           "mode=save",  WEB_ROUTE_LAYOUT_RESET, true,  false, false, 512,   0},
	{"GET /api/dtc/last",          WEB_GET,  "/api/dtc/last",     "",           "x=1",        WEB_ROUTE_DTC_LAST,     false, false, false, 0,     0},
	{"GET /api/wifi",              WEB_GET,  "/api/wifi",         "",           "x=1",        WEB_ROUTE_WIFI,         false, false, false, 0,     0},
	{"POST /api/wifi",             WEB_POST, "/api/wifi",         "",           "x=1",        WEB_ROUTE_WIFI_STORE,   true,  true,  false, 512,   0},
	{"POST /api/wifi/forget",      WEB_POST, "/api/wifi/forget",  "",           "x=1",        WEB_ROUTE_WIFI_FORGET,  true,  false, false, 512,   0},
	{"POST /api/settings",         WEB_POST, "/api/settings",     "",           "x=1",        WEB_ROUTE_SETTINGS,     true,  false, false, 512,   0},
	{"POST /api/reboot",           WEB_POST, "/api/reboot",       "",           "x=1",        WEB_ROUTE_REBOOT,       true,  false, true,  512,   0},
	{"POST /api/reset",            WEB_POST, "/api/reset",        "",           "x=1",        WEB_ROUTE_RESET,        true,  true,  true,  512,   0},
	{"POST /api/ota",              WEB_POST, "/api/ota",          "",           "x=1",        WEB_ROUTE_OTA,          true,  true,  true,  SLOT,  0},
	{"GET /api/ticket?id=7",       WEB_GET,  "/api/ticket",       "id=7",       "",           WEB_ROUTE_TICKET,       false, false, false, 0,     7},
};

static web_request_t request_of(const row_t *row)
{
	return request(row->method, row->path, row->query);
}

static bool row_goes_on(const row_t *row, const web_request_t *r)
{
	return goes_on(web_route(r), row->route, row->changes, row->knob, row->ticket);
}

static const char *row_text(const row_t *row, const char *rule)
{
	snprintf(text, sizeof(text), "%s %s", row->name, rule);
	return text;
}

static bool has_route(web_method_t method, const char *path)
{
	size_t i;

	for(i = 0; i < COUNT(rows); i++)
	{
		if(rows[i].method == method && strcmp(rows[i].path, path) == 0) return true;
	}
	return false;
}

static void test_rows(void)
{
	static const web_method_t methods[] = {WEB_GET, WEB_PUT, WEB_POST, WEB_OTHER, (web_method_t)4, (web_method_t)99, (web_method_t)-1};
	size_t i, k;

	check(COUNT(rows) == 18, "the list of the header has 18 lines");

	for(i = 0; i < COUNT(rows); i++)
	{
		const row_t *row = &rows[i];
		bool get = row->method == WEB_GET;
		web_request_t r = request_of(row);
		int others = 0, wrong = 0;

		check(row_goes_on(row, &r), row_text(row, "goes on to its handler with its route, whether it changes something and whether it needs the knob"));

		for(k = 0; k < COUNT(methods); k++)
		{
			if(has_route(methods[k], row->path)) continue;

			r = request_of(row);
			r.method = methods[k];
			others++;
			if(!is_refused(web_route(&r), 405, "method")) wrong++;
		}
		check(others >= 5 && wrong == 0, row_text(row, "with a method that has no route at this path: 405 method"));

		r = request_of(row);
		r.host = "evil.example";
		check(is_refused(web_route(&r), 403, "host"), row_text(row, "with the Host of another site: 403 host"));
		r = request_of(row);
		r.host = "";
		check(is_refused(web_route(&r), 403, "host"), row_text(row, "with an empty Host: 403 host"));

		r = request_of(row);
		r.header = "0";
		if(get) check(row_goes_on(row, &r), row_text(row, "with the header \"0\": a GET needs no header, it goes on"));
		else check(is_refused(web_route(&r), 403, "header"), row_text(row, "with the header \"0\": 403 header"));
		r = request_of(row);
		r.header = "";
		if(get) check(row_goes_on(row, &r), row_text(row, "with an empty header: a GET needs no header, it goes on"));
		else check(is_refused(web_route(&r), 403, "header"), row_text(row, "with an empty header: 403 header"));

		r = request_of(row);
		r.release_open = false;
		if(row->changes) check(is_refused(web_route(&r), 403, "locked"), row_text(row, "while the release is closed: 403 locked"));
		else check(row_goes_on(row, &r), row_text(row, "while the release is closed: it changes nothing, it goes on"));

		r = request_of(row);
		r.query = row->bad_query;
		check(is_refused(web_route(&r), 400, "query"), row_text(row, "with a query it does not take: 400 query"));

		r = request_of(row);
		r.has_length = false;
		if(get) check(row_goes_on(row, &r), row_text(row, "without Content-Length: a GET needs none, it goes on"));
		else check(is_refused(web_route(&r), 411, "length"), row_text(row, "without Content-Length: 411 length"));

		r = request_of(row);
		r.length = get ? UINT32_MAX : row->limit;
		if(get) check(row_goes_on(row, &r), row_text(row, "with a Content-Length of 2^32-1: a GET has no limit, it goes on"));
		else check(row_goes_on(row, &r), row_text(row, "with a Content-Length at its limit: it goes on"));
		if(!get)
		{
			r = request_of(row);
			r.length = row->limit + 1;
			check(is_refused(web_route(&r), 413, "too_large"), row_text(row, "with a Content-Length 1 above its limit: 413 too_large"));
			r = request_of(row);
			r.length = UINT32_MAX;
			check(is_refused(web_route(&r), 413, "too_large"), row_text(row, "with a Content-Length of 2^32-1: 413 too_large"));
			r = request_of(row);
			r.length = 0;
			if(row->route == WEB_ROUTE_OTA) check(is_refused(web_route(&r), 413, "too_large"), row_text(row, "with a Content-Length of 0: an empty firmware, 413 too_large"));
			else check(row_goes_on(row, &r), row_text(row, "with a Content-Length of 0: it goes on"));
		}

		r = request_of(row);
		r.busy = true;
		if(row->rests) check(is_refused(web_route(&r), 409, "busy"), row_text(row, "while the display is busy: 409 busy"));
		else check(row_goes_on(row, &r), row_text(row, "while the display is busy: it goes on"));

		r = request_of(row);
		r.slot_size = 0;
		if(row->route == WEB_ROUTE_OTA) check(is_refused(web_route(&r), 413, "too_large"), row_text(row, "with a slot of 0 bytes: 413 too_large"));
		else check(row_goes_on(row, &r), row_text(row, "with a slot of 0 bytes: the slot does not matter, it goes on"));
	}
}

static void test_paths(void)
{
	static const char *const unknown[] = {
		"", "/api", "/api/", "//", "/index.html", " /", "/ ", "api/info", "/api/info/", "/api/infox", "/api/inf", "/API/INFO", "/Api/info", "/api/Info",
		"/api/info?x=1", "/api/info#", "/api//info", "/api/catalog/", "/api/values/", "/api/layout/", "/api/layout/rese", "/api/layout/reset/",
		"/api/layout/check", "/api/dtc", "/api/dtc/", "/api/dtc/last/", "/api/dtc/read", "/api/dtc/clear", "/api/wifi/", "/api/wifi/forge", "/api/wifi/forget/",
		"/api/setting", "/api/settings/", "/api/reboot/", "/api/reboo", "/api/reset/", "/api/resetx", "/api/ota/", "/api/OTA", "/api/ticket/", "/api/ticket/7",
		"/api/tickets", "/API/TICKET", "/api/info\n", "/%61pi/info", "/api/info/..", "/./api/info", "/api/state", "/api/dtc/result",
	};
	static const web_method_t methods[] = {WEB_GET, WEB_PUT, WEB_POST, WEB_OTHER, (web_method_t)99};
	size_t i, k;

	for(i = 0; i < COUNT(unknown); i++)
	{
		int wrong = 0;

		for(k = 0; k < COUNT(methods); k++)
		{
			web_request_t r = request(methods[k], unknown[i], "");

			if(!is_refused(web_route(&r), 404, "not_found")) wrong++;
		}
		snprintf(text, sizeof(text), "the path \"%s\" is none of the list: 404 not_found with every method", shown(unknown[i]));
		check(wrong == 0, text);
	}
}

/* ------------------------------------------------------------------------------------------------ */
/* The order of the checks                                                                            */
/* ------------------------------------------------------------------------------------------------ */

typedef struct
{
	int status;
	const char *word;
} refusal_t;

// In the order of the header
static const refusal_t refusals[] = {
	{404, "not_found"}, {405, "method"}, {403, "host"}, {403, "header"}, {403, "locked"}, {400, "query"}, {411, "length"}, {413, "too_large"}, {409, "busy"},
};

// Makes POST /api/reboot fail the check `which` of the list above
static void spoil(web_request_t *r, size_t which)
{
	switch(which)
	{
		case 0: r->path = "/api/rebootx"; break;
		case 1: r->method = WEB_PUT; break;
		case 2: r->host = "evil.example"; break;
		case 3: r->header = "2"; break;
		case 4: r->release_open = false; break;
		case 5: r->query = "x=1"; break;
		case 6: r->has_length = false; break;
		case 7: r->length = 513; break;
		default: r->busy = true; break;
	}
}

static void test_order(void)
{
	web_request_t r = request(WEB_POST, "/api/reboot", "");
	size_t first, second, i;

	check(COUNT(refusals) == 9, "the header names 9 refusals");
	check(goes_on(web_route(&r), WEB_ROUTE_REBOOT, true, false, 0), "the request the order is tried with goes on while nothing is wrong with it");

	for(first = 0; first < COUNT(refusals); first++)
	{
		r = request(WEB_POST, "/api/reboot", "");
		spoil(&r, first);
		snprintf(text, sizeof(text), "a request that fails nothing but the check for %s: %d %s", refusals[first].word, refusals[first].status, refusals[first].word);
		check(is_refused(web_route(&r), refusals[first].status, refusals[first].word), text);

		for(second = first + 1; second < COUNT(refusals); second++)
		{
			r = request(WEB_POST, "/api/reboot", "");
			spoil(&r, second);
			spoil(&r, first);
			snprintf(text, sizeof(text), "a request that fails the checks for %s and for %s: %s goes first", refusals[first].word, refusals[second].word,
			         refusals[first].word);
			check(is_refused(web_route(&r), refusals[first].status, refusals[first].word), text);
		}

		r = request(WEB_POST, "/api/reboot", "");
		for(i = first; i < COUNT(refusals); i++) spoil(&r, i);
		snprintf(text, sizeof(text), "a request that fails the check for %s and all behind it: %s", refusals[first].word, refusals[first].word);
		check(is_refused(web_route(&r), refusals[first].status, refusals[first].word), text);
	}

	// The same order where the route is named by the query or has its own limit
	r = request(WEB_PUT, "/api/layout", "mode=bogus");
	r.release_open = false;
	check(is_refused(web_route(&r), 403, "locked"), "PUT /api/layout with an unknown mode while the release is closed: locked goes before query");
	r = request(WEB_PUT, "/api/layout", "");
	r.release_open = false;
	check(is_refused(web_route(&r), 403, "locked"), "PUT /api/layout without a mode while the release is closed: locked goes before query");
	r = request(WEB_PUT, "/api/layout", "mode=check&x=1");
	r.release_open = false;
	check(is_refused(web_route(&r), 403, "locked"), "PUT /api/layout with mode=check and more while the release is closed: only exactly mode=check is spared the release");
	r = request(WEB_PUT, "/api/layout", "mode=bogus");
	r.header = "2";
	r.release_open = false;
	check(is_refused(web_route(&r), 403, "header"), "PUT /api/layout with an unknown mode, closed and without the header: header goes first");
	r = request(WEB_PUT, "/api/layout", "mode=bogus");
	r.has_length = false;
	check(is_refused(web_route(&r), 400, "query"), "PUT /api/layout with an unknown mode and without Content-Length: query goes before length");
	r = request(WEB_PUT, "/api/layout", "mode=bogus");
	r.length = 16385;
	check(is_refused(web_route(&r), 400, "query"), "PUT /api/layout with an unknown mode and a body too large: query goes before too_large");
	r = request(WEB_PUT, "/api/layout", "mode=check");
	r.release_open = false;
	r.length = 16385;
	check(is_refused(web_route(&r), 413, "too_large"), "PUT /api/layout?mode=check with a body too large while the release is closed: 413, the release is not asked for");

	r = request(WEB_GET, "/api/ticket", "id=0");
	r.host = "evil.example";
	check(is_refused(web_route(&r), 403, "host"), "GET /api/ticket with a bad id and a foreign Host: host goes before query");
	r = request(WEB_OTHER, "/api/ticket", "id=0");
	r.host = "evil.example";
	check(is_refused(web_route(&r), 405, "method"), "another method on /api/ticket with a bad id and a foreign Host: method goes first");

	r = request(WEB_POST, "/api/ota", "");
	r.length = 0;
	r.busy = true;
	check(is_refused(web_route(&r), 413, "too_large"), "an empty firmware while the display is busy: too_large goes before busy");
	r = request(WEB_POST, "/api/ota", "");
	r.length = 0;
	r.has_length = false;
	check(is_refused(web_route(&r), 411, "length"), "a firmware without Content-Length, the length field 0: length goes before too_large");
	r = request(WEB_POST, "/api/ota", "");
	r.length = SLOT + 1;
	r.release_open = false;
	check(is_refused(web_route(&r), 403, "locked"), "a firmware too large while the release is closed: locked goes before too_large");
}

/* ------------------------------------------------------------------------------------------------ */
/* Query, header, Content-Length                                                                      */
/* ------------------------------------------------------------------------------------------------ */

static void test_layout_query(void)
{
	static const char *const refused[] = {
		"mode=check&mode=save", "mode=save&mode=save", "mode=", "mode", "x=1&mode=save", "mode=save&x=1", "mode=save&", "&mode=save", "mode=saved", "mode=sav",
		"MODE=save", "mode=SAVE", "Mode=Save", "mode=Check", "mode=APPLY", " mode=save", "mode=save ", "mode= save", "mode =save", "mode==save", "?mode=save",
		"mode=save#", "mode=%73ave", "mode=check,apply", "mode=reset", "mode=1", "=save", "save", "check", "id=7",
		"mode=checkx", "mode=check&", "mode=check&x=1", "mode=check&mode=check", "mode=applyx", "mode=apply&", "mode=apply&x=1", "mode=apply&mode=save",
		"mode=appl", "mode=chec", "mode=savex", "mode=check ", "mode=apply ", "mode=apply#", "mode=check#", "mod", "m",
	};
	web_request_t r;
	size_t i;

	r = request(WEB_PUT, "/api/layout", "mode=check");
	check(goes_on(web_route(&r), WEB_ROUTE_LAYOUT_CHECK, false, false, 0), "the query mode=check names the check: nothing changes");
	r = request(WEB_PUT, "/api/layout", "mode=apply");
	check(goes_on(web_route(&r), WEB_ROUTE_LAYOUT_APPLY, true, false, 0), "the query mode=apply names the apply: a change");
	r = request(WEB_PUT, "/api/layout", "mode=save");
	check(goes_on(web_route(&r), WEB_ROUTE_LAYOUT_SAVE, true, false, 0), "the query mode=save names the save: a change");

	for(i = 0; i < COUNT(refused); i++)
	{
		r = request(WEB_PUT, "/api/layout", refused[i]);
		snprintf(text, sizeof(text), "PUT /api/layout with the query \"%s\" is not exactly one mode: 400 query", shown(refused[i]));
		check(is_refused(web_route(&r), 400, "query"), text);
	}
}

static void test_ticket_query(void)
{
	static const struct
	{
		const char *query;
		uint32_t ticket;
	} taken[] = {
		{"id=1", 1}, {"id=7", 7}, {"id=9", 9}, {"id=10", 10}, {"id=4294967295", 4294967295u}, {"id=4294967294", 4294967294u}, {"id=1000000000", 1000000000u},
		{"id=2147483648", 2147483648u}, {"id=01", 1}, {"id=0000000001", 1}, {"id=0000000042", 42}, {"id=0123456789", 123456789u}, {"id=999999999", 999999999u},
	};
	static const char *const refused[] = {
		"id=0", "id=00", "id=0000000000", "id=4294967296", "id=4294967300", "id=9999999999", "id=5000000000", "id=00000000001", "id=04294967295",
		"id=18446744073709551617", "id=", "id", "id=1&", "id=1&id=1", "id=1&id=2", "id=1&x=1", "x=1&id=1", "&id=1", "ID=1", "Id=1", "iD=1", "id=+1", "id=-1",
		"id= 1", "id=1 ", " id=1", "id =1", "id==1", "id=1.0", "id=1e3", "id=0x10", "id=a", "id=1a", "id=%31", "id=\331\241", "?id=1", "id=1#", "id=1;", "id:1",
		"1", "ticket=1", "mode=check", "id=:", "id=/", "id=1:", "id=1/", "id=:1", "id=/1", "i", "d=1", "=1", "id1", "idx1", "id=1=", "id=1,2", "id=1\n",
	};
	web_request_t r;
	size_t i;

	for(i = 0; i < COUNT(taken); i++)
	{
		r = request(WEB_GET, "/api/ticket", taken[i].query);
		snprintf(text, sizeof(text), "GET /api/ticket with the query \"%s\" asks for the ticket %lu", taken[i].query, (unsigned long)taken[i].ticket);
		check(goes_on(web_route(&r), WEB_ROUTE_TICKET, false, false, taken[i].ticket), text);
	}
	for(i = 0; i < COUNT(refused); i++)
	{
		r = request(WEB_GET, "/api/ticket", refused[i]);
		snprintf(text, sizeof(text), "GET /api/ticket with the query \"%s\" names no ticket: 400 query", shown(refused[i]));
		check(is_refused(web_route(&r), 400, "query"), text);
	}
}

static void test_query_length(void)
{
	static const char *const others[] = {"x", "x=1", "=", "&", "?", " ", "0", "mode=check", "mode=save", "id=7", "id=1&mode=save", "\001"};
	char longest[WEB_QUERY_MAX + 1], longer[WEB_QUERY_MAX + 2], id_longest[WEB_QUERY_MAX + 1], id_longer[WEB_QUERY_MAX + 2];
	char mode_longest[WEB_QUERY_MAX + 1], mode_longer[WEB_QUERY_MAX + 2], huge[2001];
	size_t i, k;
	int wrong_other = 0, wrong_longest = 0, wrong_longer = 0, wrong_huge = 0;
	web_request_t r;

	check(WEB_QUERY_MAX == 63, "a query string has at most 63 bytes");

	memset(longest, 'x', sizeof(longest) - 1);
	longest[sizeof(longest) - 1] = '\0';
	memset(longer, 'x', sizeof(longer) - 1);
	longer[sizeof(longer) - 1] = '\0';
	memset(huge, 'x', sizeof(huge) - 1);
	huge[sizeof(huge) - 1] = '\0';
	// "id=" and zeros with a 7 at the end, "mode=save" and blanks: what a careless reader would still take
	memset(id_longest, '0', sizeof(id_longest) - 1);
	memcpy(id_longest, "id=", 3);
	id_longest[sizeof(id_longest) - 2] = '7';
	id_longest[sizeof(id_longest) - 1] = '\0';
	memset(id_longer, '0', sizeof(id_longer) - 1);
	memcpy(id_longer, "id=", 3);
	id_longer[sizeof(id_longer) - 2] = '7';
	id_longer[sizeof(id_longer) - 1] = '\0';
	memset(mode_longest, '&', sizeof(mode_longest) - 1);
	memcpy(mode_longest, "mode=save", 9);
	mode_longest[sizeof(mode_longest) - 1] = '\0';
	memset(mode_longer, '&', sizeof(mode_longer) - 1);
	memcpy(mode_longer, "mode=save", 9);
	mode_longer[sizeof(mode_longer) - 1] = '\0';

	for(i = 0; i < COUNT(rows); i++)
	{
		for(k = 0; k < COUNT(others); k++)
		{
			r = request_of(&rows[i]);
			r.query = others[k];
			if(strcmp(others[k], rows[i].query) == 0 || (rows[i].method == WEB_PUT && strncmp(others[k], "mode=", 5) == 0 && strlen(others[k]) < 11)) continue;
			if(!is_refused(web_route(&r), 400, "query")) wrong_other++;
		}
		r = request_of(&rows[i]);
		r.query = longest;
		if(!is_refused(web_route(&r), 400, "query")) wrong_longest++;
		r.query = rows[i].method == WEB_PUT ? mode_longest : id_longest;
		if(!is_refused(web_route(&r), 400, "query")) wrong_longest++;
		r.query = longer;
		if(!is_refused(web_route(&r), 400, "query")) wrong_longer++;
		r.query = rows[i].method == WEB_PUT ? mode_longer : id_longer;
		if(!is_refused(web_route(&r), 400, "query")) wrong_longer++;
		r.query = huge;
		if(!is_refused(web_route(&r), 400, "query")) wrong_huge++;
	}
	check(wrong_other == 0, "on every route a query it does not take is refused, also the query of another route: 400 query");
	check(wrong_longest == 0, "on every route a query of 63 bytes is refused: none that long fits a route");
	check(wrong_longer == 0, "on every route a query of 64 bytes is refused: longer than WEB_QUERY_MAX");
	check(wrong_huge == 0, "on every route a query of 2000 bytes is refused");
}

static void test_header(void)
{
	static const char *const wrong[] = {"", "0", "2", "11", "10", "01", "1 ", " 1", "1\n", "1\r\n", "1,1", "1;", "true", "yes", "one", "\357\274\221", "-1", "+1", "1.0"};
	int passed_one = 0, passed_longer = 0;
	unsigned value;
	web_request_t r;
	size_t i;

	check(strcmp(WEB_HEADER_NAME, "X-Display") == 0, "the header is called X-Display");
	r = request(WEB_POST, "/api/settings", "");
	check(goes_on(web_route(&r), WEB_ROUTE_SETTINGS, true, false, 0), "a POST with the header \"1\" goes on");
	for(i = 0; i < COUNT(wrong); i++)
	{
		r = request(WEB_POST, "/api/settings", "");
		r.header = wrong[i];
		snprintf(text, sizeof(text), "a POST with the header value \"%s\" is not exactly \"1\": 403 header", shown(wrong[i]));
		check(is_refused(web_route(&r), 403, "header"), text);
	}
	r = request(WEB_PUT, "/api/layout", "mode=check");
	r.header = "2";
	check(is_refused(web_route(&r), 403, "header"), "PUT /api/layout?mode=check changes nothing but is no GET: it needs the header");

	// Every other value of one byte, and every byte in front of the 1 and behind it
	for(value = 1; value <= 255; value++)
	{
		char one[2] = {(char)value, '\0'}, front[3] = {(char)value, '1', '\0'}, behind[3] = {'1', (char)value, '\0'};

		r = request(WEB_POST, "/api/settings", "");
		r.header = one;
		if(value != '1' && !is_refused(web_route(&r), 403, "header")) passed_one++;
		if(value == '1' && !goes_on(web_route(&r), WEB_ROUTE_SETTINGS, true, false, 0)) passed_one++;
		r.header = front;
		if(!is_refused(web_route(&r), 403, "header")) passed_longer++;
		r.header = behind;
		if(!is_refused(web_route(&r), 403, "header")) passed_longer++;
	}
	check(passed_one == 0, "of the 255 header values of one byte only \"1\" passes: no other digit, no control character, no byte that differs from it in one bit");
	check(passed_longer == 0, "no header value of two bytes passes that has the 1 in front or behind, whatever the other byte is");
}

static void test_length(void)
{
	web_request_t r;

	check(WEB_BODY_SMALL_MAX == 512 && WEB_BODY_LAYOUT_MAX == 16384, "a small body has at most 512 bytes, a layout 16384");
	check(WEB_BODY_LAYOUT_MAX == LAYOUT_TEXT_MAX, "the limit of a layout body is the longest text layout_parse() takes");

	r = request(WEB_POST, "/api/layout/reset", "");
	r.length = 513;
	check(is_refused(web_route(&r), 413, "too_large"), "the reset of the layout sends no layout: its body is a small one, 513 bytes are too many");

	r = request(WEB_POST, "/api/ota", "");
	r.length = 1;
	check(goes_on(web_route(&r), WEB_ROUTE_OTA, true, true, 0), "a firmware of 1 byte is not empty: it goes on");
	r.slot_size = 1;
	check(goes_on(web_route(&r), WEB_ROUTE_OTA, true, true, 0), "a firmware of 1 byte for a slot of 1 byte goes on");
	r.length = 2;
	check(is_refused(web_route(&r), 413, "too_large"), "a firmware of 2 bytes for a slot of 1 byte: 413 too_large");
	r = request(WEB_POST, "/api/ota", "");
	r.slot_size = UINT32_MAX;
	r.length = UINT32_MAX;
	check(goes_on(web_route(&r), WEB_ROUTE_OTA, true, true, 0), "a firmware of 2^32-1 bytes for a slot of 2^32-1 bytes goes on");
	r.slot_size = UINT32_MAX - 1;
	check(is_refused(web_route(&r), 413, "too_large"), "a firmware of 2^32-1 bytes for a slot of 2^32-2 bytes: 413 too_large");
	r = request(WEB_POST, "/api/ota", "");
	r.length = 16385;
	check(goes_on(web_route(&r), WEB_ROUTE_OTA, true, true, 0), "a firmware larger than a layout goes on: its limit is the slot");
	r = request(WEB_POST, "/api/ota", "");
	r.slot_size = 100;
	r.length = 101;
	check(is_refused(web_route(&r), 413, "too_large"), "a firmware of 101 bytes for a slot of 100 bytes: 413 too_large, although a small body may have 512");

	r = request(WEB_POST, "/api/settings", "");
	r.slot_size = UINT32_MAX;
	r.length = 513;
	check(is_refused(web_route(&r), 413, "too_large"), "the size of the slot is the limit of the firmware only: settings of 513 bytes are too large");
	r = request(WEB_PUT, "/api/layout", "mode=save");
	r.slot_size = UINT32_MAX;
	r.length = 16385;
	check(is_refused(web_route(&r), 413, "too_large"), "the size of the slot is the limit of the firmware only: a layout of 16385 bytes is too large");

	r = request(WEB_POST, "/api/settings", "");
	r.has_length = false;
	r.length = 0;
	check(is_refused(web_route(&r), 411, "length"), "without Content-Length the length field is not looked at: 411 also when it holds 0");
}

/* ------------------------------------------------------------------------------------------------ */
/* The Host header                                                                                    */
/* ------------------------------------------------------------------------------------------------ */

static void test_host(void)
{
	static const struct
	{
		const char *host;
		bool allowed;
		const char *rule;
	} hosts[] = {
		{"192.168.4.1", true, "an IPv4 address"},
		{"192.168.4.1:80", true, "an IPv4 address with a port"},
		{"0.0.0.0", true, "four times 0"},
		{"255.255.255.255", true, "four times 255"},
		{"10.0.0.7:8080", true, "an address with a 0 alone as a number and a port of four digits"},
		{"1.2.3.4:0", true, "a port of one digit"},
		{"1.2.3.4:65535", true, "a port of five digits"},
		{"1.2.3.4:99999", true, "a port of five digits above 65535: digits are counted, not weighed"},
		{"1.2.3.4:00080", true, "a port of five digits with zeros in front"},
		{"100.200.10.20", true, "numbers of three and two digits with zeros behind"},
		{"wican-display.local", true, "the name of the display"},
		{"WICAN-DISPLAY.LOCAL", true, "the name in upper case"},
		{"Wican-Display.Local", true, "the name in mixed case"},
		{"wican-display.locaL", true, "the name with its last letter in upper case"},
		{"wican-display.local:80", true, "the name with a port"},
		{"wIcAn-dIsPlAy.LoCaL:8080", true, "the name in mixed case with a port"},

		{"", false, "an empty text"},
		{"256.1.1.1", false, "a first number of 256"},
		{"1.256.1.1", false, "a second number of 256"},
		{"1.1.256.1", false, "a third number of 256"},
		{"1.1.1.256", false, "a last number of 256"},
		{"1.1.1.260", false, "a number of 260"},
		{"1.1.1.300", false, "a number of 300"},
		{"999.1.1.1", false, "a number of 999"},
		{"1.1.1.1000", false, "a number of four digits"},
		{"1.1.1.4294967297", false, "a number that is 1 in 32 bit"},
		{"1.1.1.18446744073709551617", false, "a number that is 1 in 64 bit"},
		{"1.2.3", false, "three numbers"},
		{"1.2", false, "two numbers"},
		{"1", false, "one number"},
		{"1.2.3.4.5", false, "five numbers"},
		{"1.2.3.4.5.6.7.8", false, "two addresses with a dot between"},
		{"1.2.3.4.1.2.3.4:80", false, "two addresses with a dot between and a port"},
		{"1.2.3.4:80.1.2.3.4", false, "an address behind the port of another"},
		{"wican-display.local.1.2.3.4", false, "the name continued with an address"},
		{"1.2.3.4.wican-display.local", false, "an address continued with the name"},
		{"wican-display.local.wican-display.local", false, "the name twice with a dot between"},
		{"wican-display.localwican-display.local", false, "the name twice"},
		{"1.2.3.4.", false, "an address with a dot behind it"},
		{".1.2.3.4", false, "an address with a dot in front"},
		{"1..2.3", false, "an address with an empty number"},
		{"1.2.3.", false, "an address whose last number is missing"},
		{"...", false, "three dots"},
		{"1,2,3,4", false, "numbers with commas"},
		{"1 2 3 4", false, "numbers with blanks between"},
		{"1234", false, "digits without dots"},
		{"01.2.3.4", false, "a zero in front of the first number"},
		{"1.2.3.04", false, "a zero in front of the last number"},
		{"00.0.0.0", false, "two zeros as a number"},
		{"1.2.3.00", false, "two zeros as the last number"},
		{"001.2.3.4", false, "two zeros in front of a number"},
		{"1.2.3.010", false, "a number that would be octal"},
		{"+1.2.3.4", false, "a plus sign in front"},
		{"1.+2.3.4", false, "a plus sign in front of the second number"},
		{"-1.2.3.4", false, "a minus sign in front"},
		{"1.2.3.-4", false, "a minus sign in front of the last number"},
		{" 1.2.3.4", false, "a blank in front"},
		{"1.2.3.4 ", false, "a blank behind"},
		{"1. 2.3.4", false, "a blank inside"},
		{"1.2.3.4\t", false, "a tab behind"},
		{"1.2.3.4\n", false, "a line feed behind"},
		{"1.2.3.4\r\n", false, "a line end behind"},
		{"1.2.3.4:", false, "a colon without a port"},
		{"1.2.3.4:123456", false, "a port of six digits"},
		{"1.2.3.4:80:80", false, "two ports"},
		{"1.2.3.4::80", false, "two colons"},
		{"1.2.3.4:8a", false, "a letter in the port"},
		{"1.2.3.4:a", false, "a letter as the port"},
		{"1.2.3.4:+80", false, "a sign in the port"},
		{"1.2.3.4:-1", false, "a negative port"},
		{"1.2.3.4: 80", false, "a blank in front of the port"},
		{"1.2.3.4:80 ", false, "a blank behind the port"},
		{"1.2.3.4:80/", false, "a slash behind the port"},
		{":80", false, "a port alone"},
		{":", false, "a colon alone"},
		{"1.2.3.4.evil.example", false, "an address continued as a foreign name"},
		{"1.2.3.4evil", false, "letters directly behind an address"},
		{"1.2.3.4a", false, "a letter behind an address"},
		{"1.2.3.4/", false, "a slash behind an address"},
		{"1.2.3.4@evil.example", false, "an address as the user of a foreign name"},
		{"evil.example@1.2.3.4", false, "a foreign name as the user of an address"},
		{"0x7f.0.0.1", false, "a hexadecimal number"},
		{"1.2.3.0x4", false, "a hexadecimal last number"},
		{"192.168.4.1.local", false, "an address with .local"},
		{"\357\274\221.2.3.4", false, "a digit of another script"},
		{"\261.2.3.4", false, "a digit with its highest bit set"},
		{"[::1]", false, "an IPv6 address in brackets"},
		{"::1", false, "an IPv6 address"},
		{"wican-display.local.", false, "the name with a dot behind it"},
		{"wican-display.localx", false, "the name with a letter behind it"},
		{"wican-display.local.evil.example", false, "the name continued as a foreign name"},
		{"wican-display.locale", false, "a name that begins with the name"},
		{"wican-display.loca", false, "the name without its last letter"},
		{"wican-display", false, "the name without .local"},
		{"wican-display.", false, "the name with a dot but without local"},
		{"wican-display.lan", false, "the name in another domain"},
		{"wican-display:80", false, "the name without .local, with a port"},
		{"xwican-display.local", false, "the name with a letter in front"},
		{"evil.wican-display.local", false, "the name as the end of a longer one"},
		{"ican-display.local", false, "the name without its first letter"},
		{"wican_display.local", false, "the name with an underscore"},
		{"wicandisplay.local", false, "the name without the hyphen"},
		{"wican-display,local", false, "the name with a comma for the dot"},
		{"wican-display.local ", false, "the name with a blank behind"},
		{" wican-display.local", false, "the name with a blank in front"},
		{"wican-display.local/", false, "the name with a slash behind"},
		{"wican-display.local:", false, "the name with a colon but no port"},
		{"wican-display.local:123456", false, "the name with a port of six digits"},
		{"wican-display.local:x", false, "the name with a letter as the port"},
		{"wican-display.local:80x", false, "the name with a letter behind the port"},
		{"wican-display.local:80:80", false, "the name with two ports"},
		{"wican\rdisplay.local", false, "the name with a carriage return for the hyphen: the hyphen without its lower-case bit"},
		{"wican-display\016local", false, "the name with the byte 0x0E for the dot: the dot without its lower-case bit"},
		{"wican\255display.local", false, "the name with the hyphen and the highest bit set"},
		{"\367ican-display.local", false, "the name with the highest bit set in its first letter"},
		{"wican-display.loca\314", false, "the name with the highest bit set in its last letter"},
		{"7ican-display.local", false, "the name with a 7 for the w: the w without bit 6"},
		{"wican-display.loca\014", false, "the name with a form feed for the l: the l without its bits 5 and 6"},
		{"wican-display.loc\001l", false, "the name with the byte 0x01 for the a"},
		{"w\304\261can-display.local", false, "the name with a dotless i"},
		{"wican-display.local\303\274", false, "the name with an umlaut behind"},
		{"wican", false, "the beginning of the name"},
		{"w", false, "one letter"},
		{"localhost", false, "localhost"},
		{"evil.example", false, "a foreign name"},
		{"evil.example:80", false, "a foreign name with a port"},
		{"wican_a1b2c3d4e5f6.local", false, "the name of the adapter"},
		{"display.local", false, "another .local name"},
	};
	size_t i;

	check(strcmp(WEB_HOST_NAME, "wican-display") == 0, "the display answers to the name wican-display");
	for(i = 0; i < COUNT(hosts); i++)
	{
		snprintf(text, sizeof(text), "Host %s: %s", hosts[i].allowed ? "allowed" : "refused", hosts[i].rule);
		check(web_host_allowed(hosts[i].host) == hosts[i].allowed, text);
	}
}

/*
 * The grammar of the Host header written a second time: the text is split at its colon and at its dots
 * and the pieces are judged by their length and content, where the module walks along it once.
 *
 *   host    = (address | name) [":" port]
 *   address = number "." number "." number "." number
 *   number  = "0" | digit-1-to-9 [digit [digit]], at most 255
 *   name    = "wican-display.local", every letter in lower or upper case
 *   port    = 1 to 5 digits
 */
static bool model_number(const char *piece, size_t length)
{
	char digits[4];

	if(length < 1 || length > 3) return false;
	memcpy(digits, piece, length);
	digits[length] = '\0';
	if(strspn(digits, "0123456789") != length) return false;
	if(length > 1 && digits[0] == '0') return false;
	return atoi(digits) <= 255;
}

static bool model_host(const char *host)
{
	static const char lower[] = "wican-display.local", upper[] = "WICAN-DISPLAY.LOCAL";
	const char *colon, *piece;
	size_t front, i;
	int dots = 0;

	if(host == NULL) return false;

	colon = strchr(host, ':');
	front = colon != NULL ? (size_t)(colon - host) : strlen(host);
	if(colon != NULL)
	{
		size_t port = strlen(colon + 1);

		if(port < 1 || port > 5 || strspn(colon + 1, "0123456789") != port) return false;
	}

	if(front == sizeof(lower) - 1)
	{
		bool name = true;

		for(i = 0; i < front; i++)
		{
			if(host[i] != lower[i] && host[i] != upper[i]) name = false;
		}
		if(name) return true;
	}

	piece = host;
	for(i = 0; i <= front; i++)
	{
		if(i < front && host[i] != '.') continue;

		if(!model_number(piece, (size_t)(&host[i] - piece))) return false;
		piece = &host[i + 1];
		if(i < front) dots++;
	}
	return dots == 3;
}

static uint32_t random_state;

static uint32_t random_below(uint32_t below)
{
	random_state = random_state * 1664525u + 1013904223u;
	return (random_state >> 8) % below;
}

#define HOSTS_DIFFER        2       // module and grammar disagree about a host
#define HOSTS_COUNTS        4       // not exactly the spellings of the name are allowed that have to be
#define HOSTS_COVERAGE      8       // too few hosts of a kind were tried

static long hosts_tried, hosts_allowed, hosts_different;

static bool host_tried(const char *host)
{
	bool allowed = web_host_allowed(host);

	hosts_tried++;
	if(allowed) hosts_allowed++;
	if(allowed != model_host(host))
	{
		if(hosts_different++ < 5) printf("  the host \"%s\" is %s, the grammar says otherwise\n", host, allowed ? "allowed" : "refused");
	}
	return allowed;
}

// Hosts made from valid ones by one changed, one removed or one added byte, addresses and names put together
// from pieces of every kind, and plain noise: each is judged by the module and by the grammar above
static int scene_hosts(void)
{
	static const char *const bases[] = {
		"wican-display.local", "wican-display.local:80", "WICAN-DISPLAY.LOCAL:65535", "192.168.4.1", "192.168.4.1:8080", "0.0.0.0", "255.255.255.255:65535",
		"10.0.0.255", "1.2.3.4:0", "249.250.199.200:12345",
	};
	static const char *const numbers[] = {
		"0", "1", "2", "9", "10", "19", "99", "100", "101", "199", "200", "249", "250", "254", "255", "256", "259", "260", "299", "300", "999", "1000", "00", "01",
		"000", "001", "010", "", "+1", "-1", " 1", "1 ", "a", "1a", "0x1", "2550", "0255",
	};
	static const char *const dots[] = {".", ".", ".", ".", ".", ".", ".", ".", ".", "", "..", ",", ":", " .", "-"};
	static const char *const ports[] = {
		"", "", "", ":0", ":1", ":80", ":8080", ":65535", ":65536", ":99999", ":00000", ":100000", ":123456", ":", ":a", ":8a", ":+8", ":-8", ": 8", ":8 ", ":80:80",
		"::80", ".", " ", "x", ".evil.example", ".local", "/", ":80/", "\n",
	};
	static const char *const fronts[] = {"", "", "", "", " ", "x", "evil.", "www.", ".", "-"};
	static const char *const joints[] = {"", ".", ":", " ", ",", "/", "@", "-"};
	static const char letters[] = "0123456789.:-wicandsplyoWICANDSPLYO \t+";
	static const char name[] = "wican-display.local";
	long single_name = 0, spellings = 0, addresses = 0, addresses_allowed = 0, pairs_allowed = 0;
	char host[160];
	size_t b, second, joint, at;
	unsigned value;
	long i;

	random_state = 20261004;

	// One byte of a valid host changed into every other value, removed, or one of every value put in
	for(b = 0; b < COUNT(bases); b++)
	{
		size_t length = strlen(bases[b]);

		host_tried(bases[b]);
		for(at = 0; at < length; at++)
		{
			for(value = 1; value <= 255; value++)
			{
				if(value == (unsigned char)bases[b][at]) continue;

				strcpy(host, bases[b]);
				host[at] = (char)value;
				if(host_tried(host) && b == 0) single_name++;
			}
			memcpy(host, bases[b], at);
			strcpy(host + at, bases[b] + at + 1);
			host_tried(host);
		}
		for(at = 0; at <= length; at++)
		{
			for(value = 1; value <= 255; value++)
			{
				memcpy(host, bases[b], at);
				host[at] = (char)value;
				strcpy(host + at + 1, bases[b] + at);
				host_tried(host);
			}
		}
	}

	// Two valid hosts one behind the other, with nothing or with one byte between them: never a host. What
	// follows a name or an address has to be a port of digits, and none of these hosts is one.
	for(b = 0; b < COUNT(bases); b++)
	{
		for(second = 0; second < COUNT(bases); second++)
		{
			for(joint = 0; joint < COUNT(joints); joint++)
			{
				snprintf(host, sizeof(host), "%s%s%s", bases[b], joints[joint], bases[second]);
				if(host_tried(host)) pairs_allowed++;
			}
		}
	}

	// Every spelling of the name in upper and lower case: 17 letters, 131072 spellings
	for(i = 0; i < 131072; i++)
	{
		int letter = 0;

		for(at = 0; at < sizeof(name); at++)
		{
			host[at] = name[at];
			if(name[at] >= 'a' && name[at] <= 'z')
			{
				if(i & (1L << letter)) host[at] = (char)(name[at] - 'a' + 'A');
				letter++;
			}
		}
		if(host_tried(host)) spellings++;
		// and each with something in front or behind
		if(i % 16 == 0)
		{
			char longer[200];

			snprintf(longer, sizeof(longer), "%s%s%s", fronts[random_below(COUNT(fronts))], host, ports[random_below(COUNT(ports))]);
			host_tried(longer);
		}
	}

	// Addresses put together from numbers, dots and ports of every kind
	for(i = 0; i < 400000; i++)
	{
		int parts = random_below(12) == 0 ? 3 + (int)random_below(3) : 4;
		bool good = random_below(4) != 0;
		int length = 0, part;

		length += snprintf(host + length, sizeof(host) - (size_t)length, "%s", good ? "" : fronts[random_below(COUNT(fronts))]);
		for(part = 0; part < parts; part++)
		{
			// Mostly numbers that are allowed, so that single defects are tried and not only heaps of them
			const char *number = numbers[random_below(good || random_below(3) != 0 ? 15 : COUNT(numbers))];
			const char *dot = part == 0 ? "" : dots[random_below(good || random_below(3) != 0 ? 9 : COUNT(dots))];

			length += snprintf(host + length, sizeof(host) - (size_t)length, "%s%s", dot, number);
		}
		snprintf(host + length, sizeof(host) - (size_t)length, "%s", ports[random_below(good ? 11 : COUNT(ports))]);
		addresses++;
		if(host_tried(host)) addresses_allowed++;
	}

	// Noise of the bytes hosts are made of
	for(i = 0; i < 200000; i++)
	{
		size_t length = random_below(24);

		for(at = 0; at < length; at++) host[at] = letters[random_below(sizeof(letters) - 1)];
		host[length] = '\0';
		host_tried(host);
	}

	printf("  hosts: %ld tried, %ld allowed, %ld addresses put together, %ld of them allowed\n", hosts_tried, hosts_allowed, addresses, addresses_allowed);
	return (hosts_different != 0 ? HOSTS_DIFFER : 0) | (single_name != 17 || spellings != 131072 || pairs_allowed != 0 ? HOSTS_COUNTS : 0) |
	       (hosts_tried < 700000 || addresses_allowed < 50000 || addresses - addresses_allowed < 50000 ? HOSTS_COVERAGE : 0);
}

static void test_hosts_against_the_grammar(void)
{
	int result = in_child(scene_hosts, SCENE_LONG);

	check(result != -1, "hosts made of valid ones, of pieces of every kind and of noise: no crash and no hang");
	check(result != -1 && (result & HOSTS_COVERAGE) == 0, "the hosts tried are many, and of the addresses put together many are allowed and many are not");
	check(result != -1 && (result & HOSTS_DIFFER) == 0, "every host tried is allowed exactly if the grammar of the header allows it");
	check(result != -1 && (result & HOSTS_COUNTS) == 0,
	      "all 131072 spellings of the name in upper and lower case are allowed, of the hosts that differ from it in one byte exactly 17: the other case of each "
	      "letter, and of two valid hosts put one behind the other none");
}

/* ------------------------------------------------------------------------------------------------ */
/* Every combination against a second writing of the rules                                            */
/* ------------------------------------------------------------------------------------------------ */

static uint32_t model_ticket(const char *query)
{
	const char *number;
	size_t digits;

	if(strlen(query) < 4 || memcmp(query, "id=", 3) != 0) return 0;

	number = query + 3;
	digits = strspn(number, "0123456789");
	if(number[digits] != '\0' || digits > 10) return 0;
	while(*number == '0') number++;
	// Compared as texts: of two numbers with ten digits the larger one comes later in the alphabet
	if(*number == '\0' || (strlen(number) == 10 && strcmp(number, "4294967295") > 0)) return 0;
	return (uint32_t)strtoul(number, NULL, 10);
}

#define QUERIES_DIFFER      2       // module and second writing disagree about a query
#define QUERIES_COUNTS      4       // not exactly the neighbours of a query are taken that have to be
#define QUERIES_COVERAGE    8       // too few queries of a kind were tried

static long queries_tried, queries_taken, queries_different;

// GET /api/ticket with `query`: true if a ticket is named. Module and second writing have to agree.
static bool ticket_tried(const char *query)
{
	web_request_t r = request(WEB_GET, "/api/ticket", query);
	web_decision_t got = web_route(&r);
	uint32_t expected = model_ticket(query);

	queries_tried++;
	if(expected != 0) queries_taken++;
	if(!(expected != 0 ? goes_on(got, WEB_ROUTE_TICKET, false, false, expected) : is_refused(got, 400, "query")))
	{
		if(queries_different++ < 5) printf("  the query \"%s\" on /api/ticket: status %d, ticket %lu, expected the ticket %lu\n", shown(query), got.status,
		                                   (unsigned long)got.ticket, (unsigned long)expected);
	}
	return got.status == 0;
}

// PUT /api/layout with `query`: true if a mode is named. No query made here is one.
static bool mode_tried(const char *query)
{
	web_request_t r = request(WEB_PUT, "/api/layout", query);
	web_decision_t got = web_route(&r);

	queries_tried++;
	if(!is_refused(got, 400, "query"))
	{
		if(queries_different++ < 5) printf("  the query \"%s\" on PUT /api/layout: status %d, route %d, expected 400\n", shown(query), got.status, (int)got.route);
	}
	return got.status == 0;
}

// Every query that differs from `base` in one byte: one changed into every other value, one removed, one of
// every value put in. Returns how many of them are taken.
static long neighbours_tried(const char *base, bool (*tried)(const char *query))
{
	size_t length = strlen(base), at;
	unsigned value;
	char query[40];
	long taken = 0;

	for(at = 0; at < length; at++)
	{
		for(value = 1; value <= 255; value++)
		{
			if(value == (unsigned char)base[at]) continue;

			strcpy(query, base);
			query[at] = (char)value;
			if(tried(query)) taken++;
		}
		memcpy(query, base, at);
		strcpy(query + at, base + at + 1);
		if(tried(query)) taken++;
	}
	for(at = 0; at <= length; at++)
	{
		for(value = 1; value <= 255; value++)
		{
			memcpy(query, base, at);
			query[at] = (char)value;
			strcpy(query + at + 1, base + at);
			if(tried(query)) taken++;
		}
	}
	return taken;
}

// The queries next to the valid ones, and ids put together from pieces of every kind
static int scene_queries(void)
{
	static const char *const modes[] = {"mode=check", "mode=apply", "mode=save"};
	static const char *const names[] = {"ID=", "Id=", "id", "id==", "id:", " id=", "x=1&id=", "&id=", "", "ticket="};
	static const char *const numbers[] = {
		"1", "7", "9", "10", "99", "4294967295", "4294967294", "4294967296", "4294967300", "4300000000", "5000000000", "9999999999", "1000000000", "0999999999",
		"0", "00", "0000000000", "0000000001", "00000000001", "0000000010", "04294967295", "004294967295", "10000000000", "18446744073709551616",
		"18446744073709551617", "", "-1", "+1", " 1", "1 ", "1a", "a", "0x1", "1.0", "1e3", "1:", "1/", ":", "/",
	};
	static const char *const ends[] = {"&", "&x=1", "&id=2", " ", "#", ";", "=", "\n"};
	long next_to_modes = 0, next_to_seven, next_to_largest, next_to_ten_digits, put_together_taken;
	char query[80];
	size_t m;
	long i;

	random_state = 4294967295u;

	for(m = 0; m < COUNT(modes); m++) next_to_modes += neighbours_tried(modes[m], mode_tried);

	// "id=7": the 7 may become 1 to 6, 8 or 9 (8), and a digit may be put in front of it or behind it (20)
	next_to_seven = neighbours_tried("id=7", ticket_tried);
	// "id=4294967295": each digit may become every smaller one, a larger one is above 2^32-1 - as many as the
	// digits add up to, 4+2+9+4+9+6+7+2+9+5 = 57. One digit less is always a ticket (10). An eleventh digit is
	// refused wherever it is put.
	next_to_largest = neighbours_tried("id=4294967295", ticket_tried);
	// "id=0000000001": the first zero may become 1 to 4, 5000000001 is too large (4); each of the other eight
	// zeros 1 to 9 (72); the 1 may become 2 to 9 (8); one zero less is ticket 1 (9), the 1 removed is no ticket;
	// an eleventh digit is refused.
	next_to_ten_digits = neighbours_tried("id=0000000001", ticket_tried);

	// Two of three have the right name, two of three end where the number ends
	queries_taken = 0;
	for(i = 0; i < 300000; i++)
	{
		const char *name = random_below(3) != 0 ? "id=" : names[random_below(COUNT(names))];
		const char *end = random_below(3) != 0 ? "" : ends[random_below(COUNT(ends))];

		if(random_below(3) == 0)
		{
			// A number of 1 to 12 digits of its own
			char digits[13];
			size_t length = 1 + random_below(12), at;

			for(at = 0; at < length; at++) digits[at] = (char)('0' + random_below(at == 0 ? 5 : 10));
			digits[length] = '\0';
			snprintf(query, sizeof(query), "%s%s%s", name, digits, end);
		}
		else
		{
			snprintf(query, sizeof(query), "%s%s%s", name, numbers[random_below(COUNT(numbers))], end);
		}
		ticket_tried(query);
	}
	put_together_taken = queries_taken;

	printf("  queries: %ld tried, next to a mode %ld taken, next to id=7 %ld, next to id=4294967295 %ld, next to id=0000000001 %ld, of 300000 ids put together %ld taken\n",
	       queries_tried, next_to_modes, next_to_seven, next_to_largest, next_to_ten_digits, put_together_taken);
	return (queries_different != 0 ? QUERIES_DIFFER : 0) |
	       (next_to_modes != 0 || next_to_seven != 28 || next_to_largest != 67 || next_to_ten_digits != 93 ? QUERIES_COUNTS : 0) |
	       (put_together_taken < 30000 || 300000 - put_together_taken < 100000 ? QUERIES_COVERAGE : 0);
}

static void test_queries_against_the_second_writing(void)
{
	int result = in_child(scene_queries, SCENE_LONG);

	check(result != -1, "queries next to the valid ones and ids put together from pieces of every kind: no crash and no hang");
	check(result != -1 && (result & QUERIES_COVERAGE) == 0, "of the ids put together many name a ticket and many do not");
	check(result != -1 && (result & QUERIES_DIFFER) == 0, "every query tried names a ticket exactly if the rule of the header written a second time says so, and names that ticket");
	check(result != -1 && (result & QUERIES_COUNTS) == 0,
	      "of the queries that differ in one byte none names a mode, 28 name a ticket next to id=7, 67 next to id=4294967295 and 93 next to id=0000000001");
}

static web_decision_t model_refused(int status, const char *word)
{
	web_decision_t decision;

	memset(&decision, 0, sizeof(decision));
	decision.route = WEB_ROUTE_NONE;
	decision.status = status;
	decision.error = word;
	return decision;
}

// The rules of the header in another shape than the module has them: the list as comparisons and switches
// instead of a table, the checks one after the other in the order of the comment
static web_decision_t model_route(const web_request_t *r)
{
	enum { NOWHERE, ROOT, INFO, CATALOG, VALUES, LAYOUT, LAYOUT_RESET, DTC_LAST, WIFI, WIFI_FORGET, SETTINGS, REBOOT, RESET, OTA, TICKET } place = NOWHERE;
	const char *query = r->query == NULL ? "" : r->query;
	bool get = r->method == WEB_GET, put = r->method == WEB_PUT, post = r->method == WEB_POST;
	bool query_wrong = query[0] != '\0';
	web_decision_t decision = model_refused(0, NULL);
	uint32_t limit = 512;

	if(r->path == NULL) place = NOWHERE;
	else if(strcmp(r->path, "/") == 0) place = ROOT;
	else if(strcmp(r->path, "/api/info") == 0) place = INFO;
	else if(strcmp(r->path, "/api/catalog") == 0) place = CATALOG;
	else if(strcmp(r->path, "/api/values") == 0) place = VALUES;
	else if(strcmp(r->path, "/api/layout") == 0) place = LAYOUT;
	else if(strcmp(r->path, "/api/layout/reset") == 0) place = LAYOUT_RESET;
	else if(strcmp(r->path, "/api/dtc/last") == 0) place = DTC_LAST;
	else if(strcmp(r->path, "/api/wifi") == 0) place = WIFI;
	else if(strcmp(r->path, "/api/wifi/forget") == 0) place = WIFI_FORGET;
	else if(strcmp(r->path, "/api/settings") == 0) place = SETTINGS;
	else if(strcmp(r->path, "/api/reboot") == 0) place = REBOOT;
	else if(strcmp(r->path, "/api/reset") == 0) place = RESET;
	else if(strcmp(r->path, "/api/ota") == 0) place = OTA;
	else if(strcmp(r->path, "/api/ticket") == 0) place = TICKET;
	if(place == NOWHERE) return model_refused(404, "not_found");

	switch(place)
	{
		case ROOT:          if(get) decision.route = WEB_ROUTE_PAGE; break;
		case INFO:          if(get) decision.route = WEB_ROUTE_INFO; break;
		case CATALOG:       if(get) decision.route = WEB_ROUTE_CATALOG; break;
		case VALUES:        if(get) decision.route = WEB_ROUTE_VALUES; break;
		case LAYOUT:        if(get) decision.route = WEB_ROUTE_LAYOUT; if(put) decision.route = WEB_ROUTE_LAYOUT_SAVE; break;
		case LAYOUT_RESET:  if(post) decision.route = WEB_ROUTE_LAYOUT_RESET; break;
		case DTC_LAST:      if(get) decision.route = WEB_ROUTE_DTC_LAST; break;
		case WIFI:          if(get) decision.route = WEB_ROUTE_WIFI; if(post) decision.route = WEB_ROUTE_WIFI_STORE; break;
		case WIFI_FORGET:   if(post) decision.route = WEB_ROUTE_WIFI_FORGET; break;
		case SETTINGS:      if(post) decision.route = WEB_ROUTE_SETTINGS; break;
		case REBOOT:        if(post) decision.route = WEB_ROUTE_REBOOT; break;
		case RESET:         if(post) decision.route = WEB_ROUTE_RESET; break;
		case OTA:           if(post) decision.route = WEB_ROUTE_OTA; break;
		case TICKET:        if(get) decision.route = WEB_ROUTE_TICKET; break;
		default:            break;
	}
	if(decision.route == WEB_ROUTE_NONE) return model_refused(405, "method");

	if(!model_host(r->host)) return model_refused(403, "host");
	if(!get && (r->header == NULL || strlen(r->header) != 1 || r->header[0] != '1')) return model_refused(403, "header");

	if(place == LAYOUT && put)
	{
		// Without a mode it stays what it was taken for above, a change
		limit = 16384;
		query_wrong = false;
		if(strcmp(query, "mode=check") == 0) decision.route = WEB_ROUTE_LAYOUT_CHECK;
		else if(strcmp(query, "mode=apply") == 0) decision.route = WEB_ROUTE_LAYOUT_APPLY;
		else if(strcmp(query, "mode=save") != 0) query_wrong = true;
	}
	if(place == TICKET)
	{
		decision.ticket = model_ticket(query);
		query_wrong = decision.ticket == 0;
	}

	switch(decision.route)
	{
		case WEB_ROUTE_WIFI_STORE:
		case WEB_ROUTE_RESET:
		case WEB_ROUTE_OTA:
			decision.knob = true;
			decision.changes = true;
			break;
		case WEB_ROUTE_LAYOUT_APPLY:
		case WEB_ROUTE_LAYOUT_SAVE:
		case WEB_ROUTE_LAYOUT_RESET:
		case WEB_ROUTE_WIFI_FORGET:
		case WEB_ROUTE_SETTINGS:
		case WEB_ROUTE_REBOOT:
			decision.changes = true;
			break;
		default:
			break;
	}
	if(decision.changes && !r->release_open) return model_refused(403, "locked");
	if(query_wrong) return model_refused(400, "query");
	if(!get)
	{
		if(!r->has_length) return model_refused(411, "length");
		if(decision.route == WEB_ROUTE_OTA)
		{
			if(r->length < 1 || r->length > r->slot_size) return model_refused(413, "too_large");
		}
		else if(r->length > limit)
		{
			return model_refused(413, "too_large");
		}
	}
	if(r->busy && (decision.route == WEB_ROUTE_OTA || decision.route == WEB_ROUTE_REBOOT || decision.route == WEB_ROUTE_RESET)) return model_refused(409, "busy");
	return decision;
}

#define GRID_DIFFERS        2       // a decision differs from the second writing of the rules
#define GRID_FOREIGN        4       // a request with a Host that is not allowed went on
#define GRID_NO_HEADER      8       // a request that is not a GET went on without the header "1"
#define GRID_LOCKED         16      // a change went on while the release was closed
#define GRID_BODY           32      // a body above the limit of its route, or no announced body, went on
#define GRID_COVERAGE       64      // not every route and every refusal occurred
#define GRID_WORD           128     // a word of a refusal does not fit into WEB_ERROR_SIZE bytes

static int scene_grid(void)
{
	static const web_method_t methods[] = {WEB_GET, WEB_PUT, WEB_POST, WEB_OTHER, (web_method_t)4, (web_method_t)-1};
	static const char *const paths[] = {
		NULL, "", "/", "/api/info", "/api/catalog", "/api/values", "/api/layout", "/api/layout/reset", "/api/dtc/last", "/api/wifi", "/api/wifi/forget",
		"/api/settings", "/api/reboot", "/api/reset", "/api/ota", "/api/ticket", "/api", "/api/", "/api/info/", "/API/INFO", "/api/dtc", "/api/layout/",
		"/index.html", "//", "/api/otax", "/api/wifi/forge", "api/info", "/api/ticket?id=1",
	};
	static const char *const queries[] = {NULL, "", "mode=check", "mode=apply", "mode=save", "mode=bogus", "id=1", "id=4294967295", "id=0", "x=1", "mode=check&id=1"};
	static const char *const headers[] = {NULL, "1", "0", "1 "};
	static const char *const hosts[] = {NULL, "192.168.4.1", "wican-display.local:80", "evil.example"};
	static const uint32_t slots[] = {0, 1000, SLOT};
	static const uint32_t lengths[] = {0, 1, 512, 513, 16384, 16385, 999, 1000, 1001, SLOT, SLOT + 1, UINT32_MAX};
	long went_on[WEB_ROUTE_TICKET + 1], refused_as[COUNT(refusals)], total = 0, different = 0;
	size_t m, p, q, h, o, s, l, i;
	int flags, result = 0;
	char body[WEB_ERROR_SIZE];

	memset(went_on, 0, sizeof(went_on));
	memset(refused_as, 0, sizeof(refused_as));

	for(m = 0; m < COUNT(methods); m++)
	for(p = 0; p < COUNT(paths); p++)
	for(q = 0; q < COUNT(queries); q++)
	for(h = 0; h < COUNT(headers); h++)
	for(o = 0; o < COUNT(hosts); o++)
	for(s = 0; s < COUNT(slots); s++)
	for(l = 0; l < COUNT(lengths); l++)
	for(flags = 0; flags < 8; flags++)
	{
		web_request_t r;
		web_decision_t got, expected;
		bool get = methods[m] == WEB_GET;

		// Without Content-Length its value does not matter: two values show that
		if((flags & 1) == 0 && l > 1) continue;

		memset(&r, 0, sizeof(r));
		r.method = methods[m];
		r.path = paths[p];
		r.query = queries[q];
		r.header = headers[h];
		r.host = hosts[o];
		r.has_length = (flags & 1) != 0;
		r.length = lengths[l];
		r.release_open = (flags & 2) != 0;
		r.busy = (flags & 4) != 0;
		r.slot_size = slots[s];

		got = web_route(&r);
		expected = model_route(&r);
		total++;
		if(got.route != expected.route || got.status != expected.status || !same_text(got.error, expected.error) || got.changes != expected.changes ||
		   got.knob != expected.knob || got.ticket != expected.ticket)
		{
			if(different++ < 5)
			{
				printf("  method %d, path %s, query %s, header %s, host %s, length %s %lu, release %s, %s, slot %lu:\n  module %d %d %s, model %d %d %s\n",
				       (int)methods[m], paths[p] ? paths[p] : "(null)", queries[q] ? queries[q] : "(null)", headers[h] ? headers[h] : "(null)",
				       hosts[o] ? hosts[o] : "(null)", r.has_length ? "sent" : "not sent", (unsigned long)r.length, r.release_open ? "open" : "closed",
				       r.busy ? "busy" : "idle", (unsigned long)r.slot_size, (int)got.route, got.status, got.error ? got.error : "-", (int)expected.route,
				       expected.status, expected.error ? expected.error : "-");
			}
			result |= GRID_DIFFERS;
		}

		if(got.status == 0)
		{
			// What must hold for everything that reaches a handler, whatever the route
			if(got.route > WEB_ROUTE_NONE && got.route <= WEB_ROUTE_TICKET) went_on[got.route]++;
			if(o != 1 && o != 2) result |= GRID_FOREIGN;
			if(!get && h != 1) result |= GRID_NO_HEADER;
			if(!r.release_open && (got.changes || got.knob || (!get && got.route != WEB_ROUTE_LAYOUT_CHECK))) result |= GRID_LOCKED;
			if(!get && (!r.has_length || r.length > (got.route == WEB_ROUTE_OTA ? r.slot_size : 16384u))) result |= GRID_BODY;
		}
		else
		{
			for(i = 0; i < COUNT(refusals); i++)
			{
				if(got.status != refusals[i].status || !same_text(got.error, refusals[i].word)) continue;
				// Its body once: the words are the same every time
				if(refused_as[i]++ == 0 && web_error_body(got.error, body, sizeof(body)) < 0) result |= GRID_WORD;
			}
		}
	}

	printf("  grid: %ld requests, %ld differ\n", total, different);
	if(total != 4967424) result |= GRID_COVERAGE;
	for(i = WEB_ROUTE_PAGE; i <= WEB_ROUTE_TICKET; i++)
	{
		if(went_on[i] == 0) result |= GRID_COVERAGE;
	}
	for(i = 0; i < COUNT(refusals); i++)
	{
		if(refused_as[i] == 0) result |= GRID_COVERAGE;
	}
	return result;
}

static void test_grid_against_the_model(void)
{
	int result = in_child(scene_grid, SCENE_LONG);

	check(result != -1, "every combination of 6 methods, 28 paths, 11 queries, 4 headers, 4 hosts, 3 slots, 14 cases of Content-Length, the release open or "
	                    "closed and the display busy or idle: no crash and no hang");
	check(result != -1 && (result & GRID_COVERAGE) == 0, "the combinations are 4967424 requests, every route goes on in some and every refusal occurs");
	check(result != -1 && (result & GRID_DIFFERS) == 0, "in the combinations every decision is the one of the rules written a second time");
	check(result != -1 && (result & GRID_FOREIGN) == 0, "in the combinations no request reaches a handler without a Host that is allowed");
	check(result != -1 && (result & GRID_NO_HEADER) == 0, "in the combinations no request that is not a GET reaches a handler without the header \"1\"");
	check(result != -1 && (result & GRID_LOCKED) == 0, "in the combinations no change reaches a handler while the release is closed");
	check(result != -1 && (result & GRID_BODY) == 0, "in the combinations no request that is not a GET reaches a handler without Content-Length or with one above every limit");
	check(result != -1 && (result & GRID_WORD) == 0, "in the combinations the word of every refusal has a body that fits into WEB_ERROR_SIZE bytes");
}

/* ------------------------------------------------------------------------------------------------ */
/* The body of a refusal                                                                              */
/* ------------------------------------------------------------------------------------------------ */

#define BEFORE  8
#define ROOM    300
#define BEHIND  16

static void test_error_body(void)
{
	// Written by hand from the comment of the header; the u umlaut is the two bytes of UTF-8
	static const struct
	{
		const char *word;
		const char *body;
		int length;
	} bodies[] = {
		{"not_found", "{\"error\":\"not_found\"}", 21},
		{"method", "{\"error\":\"method\"}", 18},
		{"host", "{\"error\":\"host\"}", 16},
		{"header", "{\"error\":\"header\"}", 18},
		{"locked", "{\"error\":\"locked\",\"hint\":\"Am Display: Men\303\274 > Web-Zugriff freigeben\"}", 69},
		{"query", "{\"error\":\"query\"}", 17},
		{"length", "{\"error\":\"length\"}", 18},
		{"too_large", "{\"error\":\"too_large\"}", 21},
		{"busy", "{\"error\":\"busy\"}", 16},
		{"asking", "{\"error\":\"asking\"}", 18},
		{"x", "{\"error\":\"x\"}", 13},
		{"", "{\"error\":\"\"}", 12},
		{"locke", "{\"error\":\"locke\"}", 17},
		{"lockedx", "{\"error\":\"lockedx\"}", 19},
		{"Locked", "{\"error\":\"Locked\"}", 18},
		{"LOCKED", "{\"error\":\"LOCKED\"}", 18},
		{" locked", "{\"error\":\" locked\"}", 19},
		{"unlocked", "{\"error\":\"unlocked\"}", 20},
		{"a\"b\\c", "{\"error\":\"a\"b\\c\"}", 17},
		{"100%s%d%n", "{\"error\":\"100%s%d%n\"}", 21},
	};
	unsigned char buffer[BEFORE + ROOM + BEHIND];
	char *out = (char *)buffer + BEFORE;
	char word[260], expected[280];
	size_t i, size, k;

	check(WEB_ERROR_SIZE == 96, "the body of a refusal needs at most 96 bytes");

	for(i = 0; i < COUNT(bodies); i++)
	{
		size_t length = strlen(bodies[i].body);
		bool fits = true, exact = true, short_refused = true, short_empty = true, none = true, guards = true;

		memset(buffer, GUARD, sizeof(buffer));
		snprintf(text, sizeof(text), "the body of the word \"%s\" is %s, %d bytes", bodies[i].word, shown(bodies[i].body), bodies[i].length);
		check(web_error_body(bodies[i].word, out, WEB_ERROR_SIZE) == bodies[i].length && strcmp(out, bodies[i].body) == 0 && (size_t)bodies[i].length == length, text);

		// Every size of buffer from none to far more than needed, with guard bytes around it
		for(size = 0; size <= ROOM; size++)
		{
			int result;

			memset(buffer, GUARD, sizeof(buffer));
			result = web_error_body(bodies[i].word, out, size);
			for(k = 0; k < BEFORE; k++) if(buffer[k] != GUARD) guards = false;
			for(k = BEFORE + size; k < sizeof(buffer); k++) if(buffer[k] != GUARD) guards = false;
			if(size > length + 1 && !(result == bodies[i].length && strcmp(out, bodies[i].body) == 0)) fits = false;
			if(size == length + 1 && !(result == bodies[i].length && strcmp(out, bodies[i].body) == 0)) exact = false;
			if(size <= length && result != -1) short_refused = false;
			if(size <= length && size > 0 && out[0] != '\0') short_empty = false;
			if(size == 0 && (unsigned char)out[0] != GUARD) none = false;
		}
		snprintf(text, sizeof(text), "the body of \"%s\" into a buffer with room to spare: the whole text and its length", bodies[i].word);
		check(fits, text);
		snprintf(text, sizeof(text), "the body of \"%s\" into a buffer of exactly its length and the zero: the whole text and its length", bodies[i].word);
		check(exact, text);
		snprintf(text, sizeof(text), "the body of \"%s\" into every buffer that is too small: -1", bodies[i].word);
		check(short_refused, text);
		snprintf(text, sizeof(text), "the body of \"%s\" into every buffer that is too small: an empty text", bodies[i].word);
		check(short_empty, text);
		snprintf(text, sizeof(text), "the body of \"%s\" into a buffer of size 0: nothing is written", bodies[i].word);
		check(none, text);
		snprintf(text, sizeof(text), "the body of \"%s\" into buffers of every size: no byte in front of the buffer or behind its size is touched", bodies[i].word);
		check(guards, text);
	}

	// The longest word that fits into WEB_ERROR_SIZE bytes has 83: 12 bytes around it and the zero
	memset(word, 'w', 83);
	word[83] = '\0';
	snprintf(expected, sizeof(expected), "{\"error\":\"%s\"}", word);
	memset(buffer, GUARD, sizeof(buffer));
	check(web_error_body(word, out, WEB_ERROR_SIZE) == 95 && strcmp(out, expected) == 0 && buffer[BEFORE + WEB_ERROR_SIZE] == GUARD,
	      "a word of 83 bytes fills WEB_ERROR_SIZE bytes to the last");
	memset(word, 'w', 84);
	word[84] = '\0';
	memset(buffer, GUARD, sizeof(buffer));
	check(web_error_body(word, out, WEB_ERROR_SIZE) == -1 && out[0] == '\0' && buffer[BEFORE + WEB_ERROR_SIZE] == GUARD,
	      "a word of 84 bytes does not fit into WEB_ERROR_SIZE bytes: -1 and an empty text");
	memset(word, 'w', 259);
	word[259] = '\0';
	snprintf(expected, sizeof(expected), "{\"error\":\"%s\"}", word);
	memset(buffer, GUARD, sizeof(buffer));
	check(web_error_body(word, out, 272) == 271 && strcmp(out, expected) == 0 && buffer[BEFORE + 272] == GUARD, "a word of 259 bytes into 272 bytes: the whole body");
	memset(buffer, GUARD, sizeof(buffer));
	check(web_error_body(word, out, 271) == -1 && out[0] == '\0' && buffer[BEFORE + 271] == GUARD, "a word of 259 bytes into 271 bytes: -1 and an empty text");
}

// The examples, one call sequence and one expected outcome each. The child prints its checks itself and
// returns whether one of them failed.
static int scene_examples(void)
{
	test_rows();
	test_paths();
	test_order();
	test_layout_query();
	test_ticket_query();
	test_query_length();
	test_header();
	test_length();
	test_host();
	test_error_body();
	return test_failures != 0 ? SCENE_WRONG : SCENE_GOOD;
}

static void test_examples(void)
{
	int result = in_child(scene_examples, SCENE_LONG);

	check(result != -1, "the examples, from the list of the header to the body of a refusal, run to their end: no crash and no hang");
	// What failed in the child was printed there and counts here
	if(result == SCENE_WRONG) test_failures++;
}

int main(void)
{
	test_null();
	test_behind_the_end();
	test_examples();
	test_hosts_against_the_grammar();
	test_queries_against_the_second_writing();
	test_grid_against_the_model();
	return test_end();
}
