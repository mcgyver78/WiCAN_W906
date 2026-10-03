/*
 * Host test for main/dtc_api.c, the decisions and texts of the HTTP API for standalone clients (API.md).
 * Run in tools/w906 (the answers are compared with the files in fixtures/ and the fixtures with API.md):
 *   cc -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I../../main ../../main/dtc_api.c dtc_api_test.c -o dtc_api_test && ./dtc_api_test
 *
 * Two kinds of checks:
 *   - examples: one input, one expected outcome, named after the rule (they are the specification)
 *   - sweeps: many generated inputs (every byte at every place of a valid text, every combination of a
 *     list of parameters, every number around a limit, every length up to 600 and around 65536,
 *     pseudo-random values in all fields at once), each compared with a second, independent
 *     implementation of the rule further down in this file or with an expectation computed another way
 *     (they catch the edges of the character classes and the combinations no example thinks of)
 * redproof.py applies changes that remove or weaken a rule and expects this test to fail for each.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dtc_api.h"

// The test takes about a second. If a loop of the module does not end, the alarm ends the test with a
// signal after this many seconds; without it the CI job would wait for hours. Shorter than the limit
// of redproof.py, so that it is always the alarm that ends a mutated test there.
#ifndef WATCHDOG_S
#define WATCHDOG_S      60
#endif

#define GUARD           32
#define STATE_BUFFER    4096
#define SWEEP_TEXT      128
// For answers and inputs longer than any counter of 16 bit
#define BIG             80000
#define BIG_TEXT        70000

static int failures = 0;

static void check(int condition, const char *what)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	// Nothing may get lost if a later check crashes
	fflush(stdout);
	if(!condition) failures++;
}

static int same_text(const char *a, const char *b)
{
	return (a == NULL && b == NULL) || (a != NULL && b != NULL && strcmp(a, b) == 0);
}

// For the result of snprintf(): an expected text that was cut off must not be compared with anything
static int fits(int written, size_t size)
{
	return written >= 0 && (size_t)written < size;
}

// A sweep makes thousands of comparisons. It shows its first differences and only counts the rest.
static int quiet = 0;
static int sweep_wrong = 0;

static void sweep_begin(void)
{
	sweep_wrong = 0;
	quiet = 0;
}

static void sweep_count(int good)
{
	if(!good) sweep_wrong++;
	quiet = sweep_wrong >= 3;
}

static int sweep_end(void)
{
	quiet = 0;
	return sweep_wrong == 0;
}

// A text of a sweep can hold any byte and can be longer than 65536 bytes
static void print_text(const char *label, const char *text)
{
	size_t shown = 0;

	printf("  %s ", label);
	if(text == NULL) printf("(none)");
	for(; text != NULL && *text != '\0' && shown < 200; text++, shown++)
	{
		unsigned char c = (unsigned char)*text;

		if(c >= 0x20 && c < 0x7F && c != '\\') putchar(c);
		else printf("\\x%02X", (unsigned)c);
	}
	if(text != NULL && *text != '\0') printf(" ... (%lu bytes)", (unsigned long)(shown + strlen(text)));
	printf("\n");
}

// Reads a file without the line end at its end. Returns 0 if it cannot be read.
static int read_file(const char *path, char *text, size_t size)
{
	size_t length;
	FILE *file;

	text[0] = '\0';
	file = fopen(path, "rb");
	if(file == NULL)
	{
		printf("  cannot open %s (run the test in tools/w906)\n", path);
		return 0;
	}
	length = fread(text, 1, size - 1, file);
	fclose(file);
	while(length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) length--;
	text[length] = '\0';
	return 1;
}

static int read_fixture(const char *name, char *text, size_t size)
{
	char path[128];

	snprintf(path, sizeof(path), "fixtures/%s", name);
	return read_file(path, text, size);
}

// Variants of a text for the sweeps. Each returns 0 when `at` is behind the last variant of its kind.
static int with_byte_replaced(char *out, const char *text, size_t at, int byte)
{
	if(at >= strlen(text)) return 0;
	strcpy(out, text);
	out[at] = (char)byte;
	return 1;
}

static int with_byte_inserted(char *out, const char *text, size_t at, int byte)
{
	if(at > strlen(text)) return 0;
	memcpy(out, text, at);
	out[at] = (char)byte;
	strcpy(out + at + 1, text + at);
	return 1;
}

static int with_byte_removed(char *out, const char *text, size_t at)
{
	if(at >= strlen(text)) return 0;
	memcpy(out, text, at);
	strcpy(out + at, text + at + 1);
	return 1;
}

static int cut_off(char *out, const char *text, size_t at)
{
	if(at >= strlen(text)) return 0;
	memcpy(out, text, at);
	out[at] = '\0';
	return 1;
}

/* ------------------------------------------------------------------------------------------------ */
/* The fixtures are the examples of API.md                                                            */
/* ------------------------------------------------------------------------------------------------ */

static char api_md[65536];

static int api_md_has(const char *before, const char *fixture, const char *after)
{
	char text[1024], wanted[1100];

	if(!read_fixture(fixture, text, sizeof(text)) || text[0] == '\0') return 0;
	if(!fits(snprintf(wanted, sizeof(wanted), "%s%s%s", before, text, after), sizeof(wanted))) return 0;
	if(strstr(api_md, wanted) != NULL) return 1;

	printf("  API.md does not contain %s\n", wanted);
	return 0;
}

static void test_fixtures_are_the_contract(void)
{
	// Without the file every check below fails
	read_file("API.md", api_md, sizeof(api_md));
	check(api_md_has("```json\n", "api_state_example.json", "\n```"),
	      "fixture api_state_example.json is the example of GET /api/state in API.md");
	check(api_md_has("| 202 | `", "api_body_accepted.json", "` |"), "fixture api_body_accepted.json is the 202 body of API.md");
	check(api_md_has("| 409 | `", "api_body_busy.json", "` |"), "fixture api_body_busy.json is a 409 body of API.md");
	check(api_md_has("| 409 | `", "api_body_read_required.json", "` |"),
	      "fixture api_body_read_required.json is a 409 body of API.md");
	check(api_md_has("| 409 | `", "api_body_stale_seq.json", "` |"), "fixture api_body_stale_seq.json is a 409 body of API.md");
	check(api_md_has("| 409 | `", "api_body_nothing_to_clear.json", "` |"),
	      "fixture api_body_nothing_to_clear.json is a 409 body of API.md");
	check(api_md_has("| 503 | `", "api_body_not_ready.json", "` |"), "fixture api_body_not_ready.json is the 503 body of API.md");
	check(api_md_has("| 403 | `", "api_body_forbidden.json", "` |"), "fixture api_body_forbidden.json is the 403 body of API.md");
	check(api_md_has("| 400 | `", "api_body_bad_request.json", "` |"), "fixture api_body_bad_request.json is the 400 body of API.md");
}

/* ------------------------------------------------------------------------------------------------ */
/* Host header                                                                                        */
/* ------------------------------------------------------------------------------------------------ */

#define ID_32   "0123456789abcdefABCDEF0123456789"

typedef struct
{
	const char *host;
	bool allowed;
	const char *what;
} host_example_t;

static const host_example_t host_examples[] = {
	{"192.168.80.1", true, "host 192.168.80.1: an IPv4 address is allowed"},
	{"0.0.0.0", true, "host 0.0.0.0: every number 0"},
	{"255.255.255.255", true, "host 255.255.255.255: every number 255"},
	{"1.2.3.4", true, "host 1.2.3.4: numbers of one digit"},
	{"10.20.30.40", true, "host 10.20.30.40: numbers of two digits"},
	{"007.010.001.000", true, "host 007.010.001.000: a number of three digits may start with zeros"},
	{"192.168.80.1:80", true, "host 192.168.80.1:80: the port is ignored"},
	{"192.168.80.1:8", true, "host with a port of 1 digit"},
	{"192.168.80.1:0", true, "host with port 0"},
	{"192.168.80.1:65535", true, "host with a port of 5 digits"},
	{"192.168.80.1:99999", true, "host with port 99999: 5 digits, the value is not looked at"},
	{"wican_a1b2c3d4e5f6.local", true, "host wican_<id>.local: the mDNS name of the adapter is allowed"},
	{"WiCAN_a1b2c3d4e5f6.local", true, "host WiCAN_<id>.local: written like the name of the access point"},
	{"WICAN_A1B2C3D4E5F6.LOCAL", true, "host WICAN_<ID>.LOCAL: upper case"},
	{"wIcAn_a1B2c3.LoCaL", true, "host wIcAn_<id>.LoCaL: mixed case"},
	{"wican_0.local", true, "host with an id of 1 character"},
	{"wican_" ID_32 ".local", true, "host with an id of 32 characters"},
	{"wican_a1b2c3d4e5f6.local:80", true, "host wican_<id>.local:80: the port is ignored"},
	{"wican_a1b2c3d4e5f6.local:65535", true, "host wican_<id>.local with a port of 5 digits"},

	{NULL, false, "host missing is refused"},
	{"", false, "host empty is refused"},
	{" ", false, "host of one blank is refused"},
	{"192.168.80", false, "host 192.168.80: three numbers are refused"},
	{"192.168.80.1.5", false, "host 192.168.80.1.5: five numbers are refused"},
	{"192.168.80.1.192.168.80.1", false, "host of an address twice is refused"},
	{"192.168.80.256", false, "host 192.168.80.256: the fourth number is above 255"},
	{"192.168.256.1", false, "host 192.168.256.1: the third number is above 255"},
	{"192.256.80.1", false, "host 192.256.80.1: the second number is above 255"},
	{"256.168.80.1", false, "host 256.168.80.1: the first number is above 255"},
	{"192.168.80.999", false, "host 192.168.80.999 is refused"},
	{"192.168.80.0001", false, "host 192.168.80.0001: a number of four digits is refused"},
	{"0192.168.80.1", false, "host 0192.168.80.1: a first number of four digits is refused"},
	{"192.168.80.", false, "host 192.168.80.: the last number is missing"},
	{"192.168.80.1.", false, "host 192.168.80.1.: a trailing dot is refused"},
	{".168.80.1", false, "host .168.80.1: the first number is missing"},
	{"192..80.1", false, "host 192..80.1: a number in the middle is missing"},
	{"192.168.80.1.example.com", false, "host 192.168.80.1.example.com: a foreign name that starts like an address"},
	{"192.168.80.1example.com", false, "host 192.168.80.1example.com is refused"},
	{"example.com", false, "host example.com: a foreign name is refused"},
	{"example.com:80", false, "host example.com:80: a foreign name with port is refused"},
	{"localhost", false, "host localhost is refused"},
	{"192.168.80.1 ", false, "host with a blank behind the address is refused"},
	{" 192.168.80.1", false, "host with a blank in front of the address is refused"},
	{"192.168.80.1\r\n", false, "host with a line end behind the address is refused"},
	{"192.168.80.1:", false, "host 192.168.80.1: with a colon and no port is refused"},
	{"192.168.80.1:123456", false, "host with a port of 6 digits is refused"},
	{"192.168.80.1:80:80", false, "host with two ports is refused"},
	{"192.168.80.1:8a", false, "host with a letter in the port is refused"},
	{"192.168.80.1:-1", false, "host with a negative port is refused"},
	{"192.168.80.1:+80", false, "host with a sign in the port is refused"},
	{"192.168.80.1: 80", false, "host with a blank in the port is refused"},
	{"192.168.80.1/", false, "host with a slash behind the address is refused"},
	{"192.168.80.1:80/", false, "host with a slash behind the port is refused"},
	{"http://192.168.80.1", false, "host that is a URL is refused"},
	{"user@192.168.80.1", false, "host with a user name is refused"},
	{"0x7f.0.0.1", false, "host 0x7f.0.0.1: hex is refused"},
	{"127.1", false, "host 127.1: the short form of an address is refused"},
	{"2130706433", false, "host 2130706433: an address as one number is refused"},
	{"192.168.80.-1", false, "host with a negative number is refused"},
	{"192.168.80.+1", false, "host with a sign in a number is refused"},
	{"192,168,80,1", false, "host with commas is refused"},
	{":80", false, "host that is only a port is refused"},
	{"[::1]", false, "host [::1]: an IPv6 address is refused"},
	{"::1", false, "host ::1 is refused"},
	{"[fe80::1]:80", false, "host [fe80::1]:80: an IPv6 address with port is refused"},
	{"[::ffff:192.168.80.1]", false, "host [::ffff:192.168.80.1]: an IPv4 address inside an IPv6 address is refused"},
	{"::ffff:192.168.80.1", false, "host ::ffff:192.168.80.1 is refused"},
	{"wican.local", false, "host wican.local is refused"},
	{"wican_.local", false, "host wican_.local: a name without id is refused"},
	{"wican_a1b2c3d4e5f6", false, "host wican_<id> without .local is refused"},
	{"wican_a1b2c3d4e5f6.local.", false, "host wican_<id>.local. with a trailing dot is refused"},
	{"wican_a1b2c3d4e5f6.local.example.com", false, "host wican_<id>.local.example.com: a foreign name that starts like the adapter"},
	{"wican_a1b2c3d4e5f6.local.local", false, "host wican_<id>.local.local is refused"},
	{"wican_a1b2c3d4e5f6.locale", false, "host wican_<id>.locale is refused"},
	{"wican_a1b2c3d4e5f6.loca", false, "host wican_<id>.loca is refused"},
	{"wican_a1b2c3d4e5f6.example.com", false, "host wican_<id>.example.com is refused"},
	{"wican_a1b2c3d4e5f6local", false, "host wican_<id>local without the dot is refused"},
	{"wican_g1b2.local", false, "host wican_g1b2.local: g is not a hex digit"},
	{"wican_G1B2.local", false, "host wican_G1B2.local: G is not a hex digit"},
	{"wican_a1b2-c3d4.local", false, "host with a dash in the id is refused"},
	{"wican_a1b2.c3d4.local", false, "host with a dot in the id is refused"},
	{"wican_" ID_32 "0.local", false, "host with an id of 33 characters is refused"},
	{"xwican_a1b2.local", false, "host xwican_<id>.local is refused"},
	{"a1b2c3d4e5f6.local", false, "host <id>.local without wican_ is refused"},
	{"evil.wican_a1b2.local", false, "host evil.wican_<id>.local is refused"},
	{"wicana1b2.local", false, "host wican<id>.local without the underscore is refused"},
	{"wican-a1b2.local", false, "host wican-<id>.local is refused"},
	{"wican_a1b2.local:", false, "host wican_<id>.local: with a colon and no port is refused"},
	{"wican_a1b2.local:123456", false, "host wican_<id>.local with a port of 6 digits is refused"},
	{"wican_a1b2 .local", false, "host with a blank in the name is refused"},
	{"192.168.80.1wican_a1b2.local", false, "host of an address followed by the name is refused"},
	{"wican_a1b2.local192.168.80.1", false, "host of the name followed by an address is refused"},
};

static void test_host_examples(void)
{
	size_t i;

	for(i = 0; i < sizeof(host_examples) / sizeof(host_examples[0]); i++)
	{
		check(dtc_api_host_allowed(host_examples[i].host) == host_examples[i].allowed, host_examples[i].what);
	}
}

// The rule of the header once more: from the end of the text to its start, with the functions of the C library
static bool model_host_allowed(const char *host)
{
	char text[SWEEP_TEXT];
	char *colon, *part;
	size_t length, i;
	int numbers;

	if(host == NULL || strlen(host) >= sizeof(text)) return false;
	strcpy(text, host);

	colon = strrchr(text, ':');
	if(colon != NULL)
	{
		length = strlen(colon + 1);
		if(length < 1 || length > 5 || strspn(colon + 1, "0123456789") != length) return false;
		*colon = '\0';
	}

	length = strlen(text);
	if(length >= 13 && length <= 44)
	{
		bool name = strspn(text + 6, "0123456789abcdefABCDEF") == length - 12;

		for(i = 0; i < 6; i++)
		{
			name = name && (text[i] == "wican_"[i] || text[i] == "WICAN_"[i]);
			name = name && (text[length - 6 + i] == ".local"[i] || text[length - 6 + i] == ".LOCAL"[i]);
		}
		if(name) return true;
	}

	part = text;
	for(numbers = 1; ; numbers++)
	{
		char *dot = strchr(part, '.');
		char number[4];

		length = dot != NULL ? (size_t)(dot - part) : strlen(part);
		if(length < 1 || length > 3) return false;
		memcpy(number, part, length);
		number[length] = '\0';
		if(strspn(number, "0123456789") != length || atoi(number) > 255) return false;
		if(dot == NULL) break;
		part = dot + 1;
	}
	return numbers == 4;
}

static int host_answer_is(const char *host, bool allowed)
{
	if(dtc_api_host_allowed(host) == allowed) return 1;
	if(!quiet) print_text(allowed ? "refused, but has to be allowed:" : "allowed, but has to be refused:", host);
	return 0;
}

static int host_as_model(const char *host)
{
	return host_answer_is(host, model_host_allowed(host));
}

// Allowed hosts the sweeps start from
static const char *const host_seeds[] = {"192.168.80.1", "10.0.0.255:65535", "9.87.6.54:1", "wican_a1b2c3d4e5f6.local",
                                         "WICAN_0F.LOCAL:8080", "wican_" ID_32 ".local:80"};
#define HOST_SEEDS  (sizeof(host_seeds) / sizeof(host_seeds[0]))

static void test_host_sweeps(void)
{
	static const char *const pieces[] = {"192.168.80.1", "wican_a1b2.local", "WICAN_0F.LOCAL", ":80", ":", ".", "1", "255",
	                                     "256", "80", "wican_", "a1b2", ".local", "[", "]", "example.com", " ", "0", "::", "x"};
	const size_t count = sizeof(pieces) / sizeof(pieces[0]);
	char text[SWEEP_TEXT], number[16];
	int byte, digits, zeros;
	unsigned value;
	size_t seed, at, i, a, b, c;

	sweep_begin();
	for(a = 0; a < count; a++)
	{
		sweep_count(host_as_model(pieces[a]));
		for(b = 0; b < count; b++)
		{
			snprintf(text, sizeof(text), "%s%s", pieces[a], pieces[b]);
			sweep_count(host_as_model(text));
			for(c = 0; c < count; c++)
			{
				snprintf(text, sizeof(text), "%s%s%s", pieces[a], pieces[b], pieces[c]);
				sweep_count(host_as_model(text));
			}
		}
	}
	check(sweep_end(), "host sweep: every text put together from 1 to 3 out of 20 pieces, module and model agree");

	sweep_begin();
	for(seed = 0; seed < HOST_SEEDS; seed++)
	{
		for(byte = 1; byte <= 255; byte++)
		{
			for(at = 0; with_byte_replaced(text, host_seeds[seed], at, byte); at++) sweep_count(host_as_model(text));
		}
	}
	check(sweep_end(), "host sweep: every byte in place of every character of 6 allowed hosts, module and model agree");

	sweep_begin();
	for(seed = 0; seed < HOST_SEEDS; seed++)
	{
		for(byte = 1; byte <= 255; byte++)
		{
			for(at = 0; with_byte_inserted(text, host_seeds[seed], at, byte); at++) sweep_count(host_as_model(text));
		}
	}
	check(sweep_end(), "host sweep: every byte inserted at every place of 6 allowed hosts, module and model agree");

	sweep_begin();
	for(seed = 0; seed < HOST_SEEDS; seed++)
	{
		for(at = 0; with_byte_removed(text, host_seeds[seed], at); at++) sweep_count(host_as_model(text));
		for(at = 0; cut_off(text, host_seeds[seed], at); at++) sweep_count(host_as_model(text));
	}
	check(sweep_end(), "host sweep: every character removed and every cut of 6 allowed hosts, module and model agree");

	// Expectation without the model: a number is good if it has at most 3 digits and is at most 255
	sweep_begin();
	for(at = 0; at < 4; at++)
	{
		for(zeros = 0; zeros <= 3; zeros++)
		{
			for(value = 0; value <= 1100; value++)
			{
				snprintf(number, sizeof(number), "%s%u", zeros == 3 ? "000" : zeros == 2 ? "00" : zeros == 1 ? "0" : "", value);
				snprintf(text, sizeof(text), "%s.%s.%s.%s", at == 0 ? number : "192", at == 1 ? number : "168",
				         at == 2 ? number : "80", at == 3 ? number : "1");
				sweep_count(host_answer_is(text, strlen(number) <= 3 && value <= 255));
			}
		}
	}
	check(sweep_end(), "host sweep: the numbers 0..1100 with 0 to 3 zeros in front at each of the four places, good up to 255 and 3 digits");

	sweep_begin();
	for(digits = 0; digits <= 8; digits++)
	{
		for(i = 0; i < 3; i++)
		{
			snprintf(text, sizeof(text), "%s:%.*s", i == 2 ? "wican_a1b2c3d4e5f6.local" : "192.168.80.1", digits,
			         i == 0 ? "00000000" : "98765432");
			sweep_count(host_answer_is(text, digits >= 1 && digits <= 5));
		}
	}
	check(sweep_end(), "host sweep: ports of 0 to 8 digits, good with 1 to 5");

	sweep_begin();
	for(digits = 0; digits <= 40; digits++)
	{
		for(i = 0; i < 3; i++)
		{
			snprintf(text, sizeof(text), "wican_%.*s.local", digits,
			         i == 0 ? "0000000000000000000000000000000000000000" :
			         i == 1 ? "ffffffffffffffffffffffffffffffffffffffff" : "A9A9A9A9A9A9A9A9A9A9A9A9A9A9A9A9A9A9A9A9");
			sweep_count(host_answer_is(text, digits >= 1 && digits <= 32));
		}
	}
	check(sweep_end(), "host sweep: ids of 0 to 40 characters, good with 1 to 32");
}

/* ------------------------------------------------------------------------------------------------ */
/* Request                                                                                            */
/* ------------------------------------------------------------------------------------------------ */

#define HOST    "192.168.80.1"

static const char *reason_of(int status)
{
	return status == 403 ? "forbidden" : status == 400 ? "bad_request" : NULL;
}

static int request_is(const char *header, const char *host, const char *query, int status, bool clear, uint32_t seq)
{
	dtc_api_request_t request = dtc_api_parse_request(header, host, query);

	if(request.status == status && same_text(request.reason, reason_of(status)) && request.clear == clear &&
	   request.seq == seq) return 1;
	if(quiet) return 0;

	print_text("header", header);
	print_text("host  ", host);
	print_text("query ", query);
	printf("  got      status %d, reason %s, clear %d, seq %lu\n  expected status %d, reason %s, clear %d, seq %lu\n",
	       request.status, request.reason != NULL ? request.reason : "(none)", (int)request.clear,
	       (unsigned long)request.seq, status, reason_of(status) != NULL ? reason_of(status) : "(none)", (int)clear,
	       (unsigned long)seq);
	return 0;
}

typedef struct
{
	const char *header;
	const char *host;
	const char *query;
	int status;
	bool clear;
	uint32_t seq;
	const char *what;
} request_example_t;

static const request_example_t request_examples[] = {
	// Who may ask
	{"1", HOST, "action=read", 0, false, 0, "request: header 1, host an address, action=read is a read"},
	{"1", "wican_a1b2c3d4e5f6.local", "action=read", 0, false, 0, "request: host wican_<id>.local may ask"},
	{"1", "192.168.80.1:80", "action=clear&seq=43", 0, true, 43, "request: host with port may ask"},
	{NULL, HOST, "action=read", 403, false, 0, "request without the header X-WiCAN-DTC is forbidden"},
	{"", HOST, "action=read", 403, false, 0, "request with an empty header is forbidden"},
	{"0", HOST, "action=read", 403, false, 0, "request with header 0 is forbidden"},
	{"2", HOST, "action=read", 403, false, 0, "request with header 2 is forbidden"},
	{"11", HOST, "action=read", 403, false, 0, "request with header 11 is forbidden"},
	{"1 ", HOST, "action=read", 403, false, 0, "request with a blank behind the 1 of the header is forbidden"},
	{" 1", HOST, "action=read", 403, false, 0, "request with a blank in front of the 1 of the header is forbidden"},
	{"01", HOST, "action=read", 403, false, 0, "request with header 01 is forbidden"},
	{"1.0", HOST, "action=read", 403, false, 0, "request with header 1.0 is forbidden"},
	{"true", HOST, "action=read", 403, false, 0, "request with header true is forbidden"},
	{"1", NULL, "action=read", 403, false, 0, "request without Host is forbidden"},
	{"1", "", "action=read", 403, false, 0, "request with an empty Host is forbidden"},
	{"1", "example.com", "action=read", 403, false, 0, "request with a foreign Host is forbidden"},
	{"1", "192.168.80.1.example.com", "action=read", 403, false, 0, "request with a Host that starts like an address is forbidden"},
	{"1", "[::1]", "action=read", 403, false, 0, "request with an IPv6 Host is forbidden"},

	{NULL, "wican_a1b2c3d4e5f6.local", "action=read", 403, false, 0, "request without the header is forbidden for the name of the adapter too"},
	{NULL, "192.168.80.1:80", "action=clear&seq=43", 403, false, 0, "request without the header is forbidden for an address with port too"},

	// forbidden is decided before bad_request, and a refused request carries neither action nor number
	{NULL, HOST, NULL, 403, false, 0, "request without header and without query: forbidden, not bad_request"},
	{NULL, HOST, "action=bogus", 403, false, 0, "request without header and with an unknown action: forbidden"},
	{"1", "example.com", "action=bogus", 403, false, 0, "request with a foreign Host and an unknown action: forbidden"},
	{"1", NULL, "", 403, false, 0, "request without Host and with an empty query: forbidden"},
	{NULL, NULL, NULL, 403, false, 0, "request with nothing at all: forbidden"},
	{NULL, HOST, "action=clear&seq=43", 403, false, 0, "a forbidden clear carries neither the action nor the number"},
	{"1", "example.com", "action=clear&seq=43", 403, false, 0, "a clear from a foreign Host carries neither the action nor the number"},
	{"1", HOST, "action=clear&seq=0", 400, false, 0, "a clear with a bad number carries no action"},
	{"1", HOST, "action=bogus&seq=43", 400, false, 0, "an unknown action carries no number"},

	// action
	{"1", HOST, NULL, 400, false, 0, "request without query is a bad request"},
	{"1", HOST, "", 400, false, 0, "request with an empty query is a bad request"},
	{"1", HOST, "seq=43", 400, false, 0, "request without action is a bad request"},
	{"1", HOST, "action", 400, false, 0, "action without a value is a bad request"},
	{"1", HOST, "action=", 400, false, 0, "action with an empty value is a bad request"},
	{"1", HOST, "action=bogus", 400, false, 0, "action=bogus is a bad request"},
	{"1", HOST, "action=READ", 400, false, 0, "action=READ is a bad request, the value is compared exactly"},
	{"1", HOST, "action=Clear&seq=43", 400, false, 0, "action=Clear is a bad request"},
	{"1", HOST, "Action=read", 400, false, 0, "Action=read is a bad request, the name is compared exactly"},
	{"1", HOST, "action=readx", 400, false, 0, "action=readx is a bad request"},
	{"1", HOST, "action=rea", 400, false, 0, "action=rea is a bad request"},
	{"1", HOST, "action=r", 400, false, 0, "action=r is a bad request"},
	{"1", HOST, "action=clearx&seq=43", 400, false, 0, "action=clearx is a bad request"},
	{"1", HOST, "action=clea&seq=43", 400, false, 0, "action=clea is a bad request"},
	{"1", HOST, "action=c&seq=43", 400, false, 0, "action=c is a bad request"},
	{"1", HOST, "action=read ", 400, false, 0, "action=read with a blank behind it is a bad request"},
	{"1", HOST, "action= read", 400, false, 0, "action=read with a blank in front of the value is a bad request"},
	{"1", HOST, " action=read", 400, false, 0, "a blank in front of the name action is a bad request"},
	{"1", HOST, "action =read", 400, false, 0, "a blank behind the name action is a bad request"},
	{"1", HOST, "action==read", 400, false, 0, "action==read is a bad request"},
	{"1", HOST, "action=read=1", 400, false, 0, "action=read=1 is a bad request"},
	{"1", HOST, "action=%72ead", 400, false, 0, "action=%72ead is a bad request, nothing is percent-decoded"},
	{"1", HOST, "action=read;x=1", 400, false, 0, "a semicolon does not separate parameters"},
	{"1", HOST, "?action=read", 400, false, 0, "a question mark in front of action makes another name"},
	{"1", HOST, "xaction=read", 400, false, 0, "xaction=read is a bad request"},
	{"1", HOST, "actionx=read", 400, false, 0, "actionx=read is a bad request"},
	{"1", HOST, "actio=read", 400, false, 0, "actio=read is a bad request"},
	{"1", HOST, "x=action=read", 400, false, 0, "action=read inside the value of another parameter does not count"},
	{"1", HOST, "x=action&action=read", 0, false, 0, "the word action as the value of another parameter is not the action"},

	// The first action counts
	{"1", HOST, "action=read&action=clear&seq=43", 0, false, 0, "read then clear: the first action counts, it is a read"},
	{"1", HOST, "action=clear&action=read&seq=43", 0, true, 43, "clear then read: the first action counts, it is a clear"},
	{"1", HOST, "action=bogus&action=read", 400, false, 0, "an unknown first action is a bad request although a good one follows"},
	{"1", HOST, "action&action=read", 400, false, 0, "a first action without value is a bad request although a good one follows"},
	{"1", HOST, "action=read&action=bogus", 0, false, 0, "a good first action counts although an unknown one follows"},

	// Unknown parameters and the order
	{"1", HOST, "seq=43&action=clear", 0, true, 43, "seq in front of action: the order of the parameters does not matter"},
	{"1", HOST, "x=1&action=read&y=2", 0, false, 0, "unknown parameters around a read are ignored"},
	{"1", HOST, "x=1&action=clear&y=2&seq=43&z=3", 0, true, 43, "unknown parameters around a clear are ignored"},
	{"1", HOST, "x&action=read", 0, false, 0, "an unknown parameter without value is ignored"},
	{"1", HOST, "&&action=read&&", 0, false, 0, "empty parameters are ignored"},
	{"1", HOST, "=5&action=clear&=6&seq=43", 0, true, 43, "parameters without a name are ignored"},
	{"1", HOST, "action=clear&sequence=5&seq=43", 0, true, 43, "the parameter sequence is not seq"},
	{"1", HOST, "action=clear&se=5&seq=43", 0, true, 43, "the parameter se is not seq"},
	{"1", HOST, "action=clear&xseq=5&seq=43", 0, true, 43, "the parameter xseq is not seq"},
	{"1", HOST, "action=clear&x=seq=5&seq=43", 0, true, 43, "seq=5 inside the value of another parameter does not count"},
	{"1", HOST, "action=clear&seq2=43", 400, false, 0, "a clear with seq2 instead of seq is a bad request"},
	{"1", HOST, "action=clear&SEQ=43", 400, false, 0, "a clear with SEQ instead of seq is a bad request"},
	{"1", HOST, "action=clear&x=seq=43", 400, false, 0, "a clear with seq only inside another value is a bad request"},

	// The first seq counts
	{"1", HOST, "action=clear&seq=43&seq=44", 0, true, 43, "seq=43 then seq=44: the first seq counts"},
	{"1", HOST, "action=clear&seq=44&seq=43", 0, true, 44, "seq=44 then seq=43: the first seq counts"},
	{"1", HOST, "seq=43&action=clear&seq=x", 0, true, 43, "a good first seq counts although a malformed one follows"},
	{"1", HOST, "action=clear&seq=x&seq=43", 400, false, 0, "a malformed first seq is a bad request although a good one follows"},
	{"1", HOST, "action=clear&seq&seq=43", 400, false, 0, "a first seq without value is a bad request although a good one follows"},

	// seq of a clear
	{"1", HOST, "action=clear&seq=1", 0, true, 1, "clear with seq=1, the smallest number"},
	{"1", HOST, "action=clear&seq=2147483647", 0, true, 2147483647u, "clear with seq=2147483647, the largest number"},
	{"1", HOST, "action=clear&seq=2147483648", 400, false, 0, "clear with seq=2147483648 is a bad request"},
	{"1", HOST, "action=clear&seq=0", 400, false, 0, "clear with seq=0 is a bad request"},
	{"1", HOST, "action=clear", 400, false, 0, "clear without seq is a bad request"},
	{"1", HOST, "action=clear&seq", 400, false, 0, "clear with seq without a value is a bad request"},
	{"1", HOST, "action=clear&seq=", 400, false, 0, "clear with an empty seq is a bad request"},
	{"1", HOST, "action=clear&seq=0000000043", 0, true, 43, "clear with a seq of 10 digits, zeros in front"},
	{"1", HOST, "action=clear&seq=00000000043", 400, false, 0, "clear with a seq of 11 digits is a bad request although its value is 43"},
	{"1", HOST, "action=clear&seq=0000000000", 400, false, 0, "clear with a seq of 10 zeros is a bad request"},
	{"1", HOST, "action=clear&seq=4294967296", 400, false, 0, "clear with seq=2^32 is a bad request, not 0"},
	{"1", HOST, "action=clear&seq=4294967339", 400, false, 0, "clear with seq=2^32+43 is a bad request, not 43"},
	{"1", HOST, "action=clear&seq=9999999999", 400, false, 0, "clear with seq=9999999999 is a bad request"},
	{"1", HOST, "action=clear&seq=18446744073709551659", 400, false, 0, "clear with seq=2^64+43 is a bad request, not 43"},
	{"1", HOST, "action=clear&seq=+43", 400, false, 0, "clear with seq=+43 is a bad request"},
	{"1", HOST, "action=clear&seq=-43", 400, false, 0, "clear with seq=-43 is a bad request"},
	{"1", HOST, "action=clear&seq= 43", 400, false, 0, "clear with a blank in front of the number is a bad request"},
	{"1", HOST, "action=clear&seq=43 ", 400, false, 0, "clear with a blank behind the number is a bad request"},
	{"1", HOST, "action=clear&seq=43.0", 400, false, 0, "clear with seq=43.0 is a bad request"},
	{"1", HOST, "action=clear&seq=0x2b", 400, false, 0, "clear with seq=0x2b is a bad request"},
	{"1", HOST, "action=clear&seq=43a", 400, false, 0, "clear with seq=43a is a bad request"},
	{"1", HOST, "action=clear&seq=a43", 400, false, 0, "clear with seq=a43 is a bad request"},
	{"1", HOST, "action=clear&seq=4e1", 400, false, 0, "clear with seq=4e1 is a bad request"},
	{"1", HOST, "action=clear&seq=%343", 400, false, 0, "clear with seq=%343 is a bad request, nothing is percent-decoded"},
	{"1", HOST, "action=clear&seq=43=", 400, false, 0, "clear with seq=43= is a bad request"},

	// seq of a read
	{"1", HOST, "action=read&seq=43", 0, false, 0, "read with seq=43: the number is ignored and returned as 0"},
	{"1", HOST, "seq=43&action=read", 0, false, 0, "read with seq in front: the number is ignored"},
	{"1", HOST, "action=read&seq=abc", 0, false, 0, "read with a malformed seq is still a read"},
	{"1", HOST, "action=read&seq=", 0, false, 0, "read with an empty seq is still a read"},
	{"1", HOST, "action=read&seq=0", 0, false, 0, "read with seq=0 is still a read"},
	{"1", HOST, "action=read&seq=99999999999", 0, false, 0, "read with a seq of 11 digits is still a read"},
};

static void test_request_examples(void)
{
	size_t i;

	for(i = 0; i < sizeof(request_examples) / sizeof(request_examples[0]); i++)
	{
		const request_example_t *e = &request_examples[i];

		check(request_is(e->header, e->host, e->query, e->status, e->clear, e->seq), e->what);
	}
}

// The rules of a request once more: the query is cut into pieces in a copy and compared with the functions
// of the C library, the number is read by strtoull() after its form was checked
static int model_request(const char *header, const char *host, const char *query, bool *clear, uint32_t *seq)
{
	char copy[SWEEP_TEXT];
	char *piece, *next;
	const char *action = NULL, *number = NULL;
	unsigned long long value;
	size_t length;

	*clear = false;
	*seq = 0;

	if(header == NULL || header[0] != '1' || header[1] != '\0' || !model_host_allowed(host)) return 403;
	if(query == NULL || strlen(query) >= sizeof(copy)) return 400;
	strcpy(copy, query);

	for(piece = copy; piece != NULL; piece = next)
	{
		char *equals;

		next = strchr(piece, '&');
		if(next != NULL) *next++ = '\0';
		equals = strchr(piece, '=');
		if(equals != NULL) *equals++ = '\0';

		if(action == NULL && strcmp(piece, "action") == 0) action = equals != NULL ? equals : "";
		if(number == NULL && strcmp(piece, "seq") == 0) number = equals != NULL ? equals : "";
	}

	if(action == NULL) return 400;
	if(strcmp(action, "read") == 0) return 0;
	if(strcmp(action, "clear") != 0 || number == NULL) return 400;

	length = strlen(number);
	if(length < 1 || length > 10 || strspn(number, "0123456789") != length) return 400;
	value = strtoull(number, NULL, 10);
	if(value < 1 || value > 2147483647ull) return 400;

	*clear = true;
	*seq = (uint32_t)value;
	return 0;
}

static int request_as_model(const char *header, const char *host, const char *query)
{
	bool clear;
	uint32_t seq;
	int status = model_request(header, host, query, &clear, &seq);

	return request_is(header, host, query, status, clear, seq);
}

static void test_request_sweeps(void)
{
	static const char *const pieces[] = {
		"action=read", "action=clear", "action=", "action", "action=bogus", "action=readx", "action=clea", "Action=read",
		"xaction=read", "actionx=clear", "seq=7", "seq=0", "seq=", "seq", "seq=2147483647", "seq=2147483648",
		"seq=0000000042", "seq=00000000042", "seq=4294967303", "seq=7x", "seq=-7", "Seq=7", "xseq=7", "seqx=7", "x=1", "",
		"x=action=read", "x=seq=7", "=", "action=clear=", "seq=7=",
	};
	static const char *const queries[] = {"action=read", "action=clear&seq=1234567890", "seq=7&action=clear",
	                                      "x=1&action=clear&y=2&seq=2147483647", "action=read&seq=x"};
	static const unsigned long long numbers[] = {
		0, 1, 2, 9, 10, 99, 100, 65535, 65536, 999999999, 1000000000, 2147483646, 2147483647, 2147483648ull,
		2147483649ull, 4294967295ull, 4294967296ull, 4294967297ull, 6442450943ull, 9999999999ull, 10000000000ull,
		10000000001ull, 18446744073709551615ull,
	};
	static const char *const headers[] = {NULL, "", "0", "1", "11", "true"};
	static const char *const hosts[] = {NULL, "", "example.com", "example.com:80", "192.168.80.1", "192.168.80.1:80",
	                                    "wican_a1b2c3d4e5f6.local", "WiCAN_a1b2c3d4e5f6.local:80", "192.168.80.1.example.com",
	                                    "[::1]:80"};
	static const char *const asked[] = {NULL, "", "action=read", "action=clear&seq=43", "action=clear", "action=bogus",
	                                    "seq=43", "action=read&seq=x"};
	const size_t count = sizeof(pieces) / sizeof(pieces[0]);
	char text[SWEEP_TEXT];
	int byte;
	size_t a, b, c, i, at;

	sweep_begin();
	for(a = 0; a < sizeof(headers) / sizeof(headers[0]); a++)
	{
		for(b = 0; b < sizeof(hosts) / sizeof(hosts[0]); b++)
		{
			for(c = 0; c < sizeof(asked) / sizeof(asked[0]); c++) sweep_count(request_as_model(headers[a], hosts[b], asked[c]));
		}
	}
	check(sweep_end(), "request sweep: every combination of 6 headers, 10 hosts and 8 queries, module and model agree");

	sweep_begin();
	for(a = 0; a < count; a++)
	{
		sweep_count(request_as_model("1", HOST, pieces[a]));
		for(b = 0; b < count; b++)
		{
			snprintf(text, sizeof(text), "%s&%s", pieces[a], pieces[b]);
			sweep_count(request_as_model("1", HOST, text));
			for(c = 0; c < count; c++)
			{
				snprintf(text, sizeof(text), "%s&%s&%s", pieces[a], pieces[b], pieces[c]);
				sweep_count(request_as_model("1", HOST, text));
			}
		}
	}
	check(sweep_end(), "request sweep: every query of 1 to 3 out of 31 parameters, module and model agree");

	sweep_begin();
	for(i = 0; i < sizeof(queries) / sizeof(queries[0]); i++)
	{
		for(byte = 1; byte <= 255; byte++)
		{
			for(at = 0; with_byte_replaced(text, queries[i], at, byte); at++) sweep_count(request_as_model("1", HOST, text));
			for(at = 0; with_byte_inserted(text, queries[i], at, byte); at++) sweep_count(request_as_model("1", HOST, text));
		}
		for(at = 0; with_byte_removed(text, queries[i], at); at++) sweep_count(request_as_model("1", HOST, text));
		for(at = 0; cut_off(text, queries[i], at); at++) sweep_count(request_as_model("1", HOST, text));
	}
	check(sweep_end(), "request sweep: every byte in place of and in front of every character of 5 queries, "
	      "every character removed, every cut: module and model agree");

	// Expectation without the model: only the one text "1" opens the door
	sweep_begin();
	for(byte = 1; byte <= 255; byte++)
	{
		text[0] = (char)byte;
		text[1] = '\0';
		sweep_count(request_is(text, HOST, "action=read", byte == '1' ? 0 : 403, false, 0));
		text[1] = '1';
		text[2] = '\0';
		sweep_count(request_is(text, HOST, "action=read", 403, false, 0));
		text[0] = '1';
		text[1] = (char)byte;
		sweep_count(request_is(text, HOST, "action=read", 403, false, 0));
	}
	check(sweep_end(), "request sweep: every byte as the header, in front of and behind its 1: only the text 1 is taken");

	// Expectation without the model: a name with one more byte is the name of an unknown parameter, the
	// action and the number come from the parameters behind it. '&' ends a parameter and '=' a name.
	sweep_begin();
	for(byte = 1; byte <= 255; byte++)
	{
		if(byte == '&') continue;
		snprintf(text, sizeof(text), "%caction=bogus&%cseq=5&action=clear&seq=43", byte, byte);
		sweep_count(request_is("1", HOST, text, 0, true, 43));
		if(byte == '=') continue;
		snprintf(text, sizeof(text), "action%c=bogus&seq%c=5&action=clear&seq=43", byte, byte);
		sweep_count(request_is("1", HOST, text, 0, true, 43));
	}
	check(sweep_end(), "request sweep: every byte in front of and behind the names action and seq makes an unknown parameter");

	// Expectation without the model: the number as the test wrote it
	sweep_begin();
	for(i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++)
	{
		bool good = numbers[i] >= 1 && numbers[i] <= 2147483647ull;

		snprintf(text, sizeof(text), "action=clear&seq=%llu", numbers[i]);
		sweep_count(request_is("1", HOST, text, good ? 0 : 400, good, good ? (uint32_t)numbers[i] : 0));
		snprintf(text, sizeof(text), "action=clear&seq=%010llu", numbers[i]);
		sweep_count(request_is("1", HOST, text, good ? 0 : 400, good, good ? (uint32_t)numbers[i] : 0));
		snprintf(text, sizeof(text), "action=clear&seq=%011llu", numbers[i]);
		sweep_count(request_is("1", HOST, text, 400, false, 0));
		snprintf(text, sizeof(text), "action=read&seq=%llu", numbers[i]);
		sweep_count(request_is("1", HOST, text, 0, false, 0));
	}
	check(sweep_end(), "request sweep: 23 numbers around the limits as they are, with 10 and with 11 digits: "
	      "1..2147483647 with at most 10 digits is taken, a read ignores all of them");
}

// Host and header together. Each is checked on its own above; a request has to take exactly the hosts
// dtc_api_host_allowed() takes, and no host may stand in for the header X-WiCAN-DTC.
static int request_host_is(const char *host, bool allowed)
{
	return request_is("1", host, "action=clear&seq=43", allowed ? 0 : 403, allowed, allowed ? 43 : 0) &&
	       request_is("1", host, "action=read", allowed ? 0 : 403, false, 0);
}

static int request_needs_header(const char *host)
{
	return request_is(NULL, host, "action=clear&seq=43", 403, false, 0) && request_is("", host, "action=read", 403, false, 0) &&
	       request_is("0", host, "action=read", 403, false, 0);
}

static void test_request_hosts(void)
{
	char text[SWEEP_TEXT];
	size_t i, at;
	int byte;

	sweep_begin();
	for(i = 0; i < sizeof(host_examples) / sizeof(host_examples[0]); i++)
	{
		sweep_count(request_host_is(host_examples[i].host, host_examples[i].allowed));
	}
	check(sweep_end(), "request: each of the host examples is taken or refused as the Host of a request like on its own");

	sweep_begin();
	for(i = 0; i < sizeof(host_examples) / sizeof(host_examples[0]); i++) sweep_count(request_needs_header(host_examples[i].host));
	check(sweep_end(), "request: with none of the host examples a request is taken without the header");

	sweep_begin();
	for(i = 0; i < HOST_SEEDS; i++)
	{
		for(byte = 1; byte <= 255; byte++)
		{
			for(at = 0; with_byte_replaced(text, host_seeds[i], at, byte); at++)
			{
				sweep_count(request_host_is(text, model_host_allowed(text)));
				sweep_count(request_needs_header(text));
			}
		}
	}
	check(sweep_end(), "request sweep: every byte in place of every character of 6 allowed hosts: with the header "
	      "as the model of the host says, without it forbidden");
}

/* ------------------------------------------------------------------------------------------------ */
/* Status and body of the answer                                                                      */
/* ------------------------------------------------------------------------------------------------ */

static void test_status(void)
{
	int result;

	check(dtc_api_status(true, DTC_ACCEPTED) == 202, "status: an accepted request is 202");
	check(dtc_api_status(true, DTC_REJECT_BUSY) == 409, "status: busy is 409");
	check(dtc_api_status(true, DTC_REJECT_READ_REQUIRED) == 409, "status: read_required is 409");
	check(dtc_api_status(true, DTC_REJECT_STALE_SEQ) == 409, "status: stale_seq is 409");
	check(dtc_api_status(true, DTC_REJECT_NOTHING_TO_CLEAR) == 409, "status: nothing_to_clear is 409");
	check(dtc_api_status(true, (dtc_accept_t)99) == 409, "status: an answer of the scan state the API does not know is 409, never 202");
	check(dtc_api_status(false, DTC_ACCEPTED) == 503, "status: not ready is 503 even if the scan state would accept");
	check(dtc_api_status(false, DTC_REJECT_BUSY) == 503, "status: not ready is 503, not the 409 of busy");
	check(dtc_api_status(false, DTC_REJECT_READ_REQUIRED) == 503, "status: not ready is 503, not the 409 of read_required");
	check(dtc_api_status(false, DTC_REJECT_STALE_SEQ) == 503, "status: not ready is 503, not the 409 of stale_seq");
	check(dtc_api_status(false, DTC_REJECT_NOTHING_TO_CLEAR) == 503, "status: not ready is 503, not the 409 of nothing_to_clear");
	check(dtc_api_status(false, (dtc_accept_t)99) == 503, "status: not ready is 503 for an answer the API does not know too");

	sweep_begin();
	for(result = 0; result <= 300; result++)
	{
		sweep_count(dtc_api_status(false, (dtc_accept_t)result) == 503);
		sweep_count(dtc_api_status(true, (dtc_accept_t)result) == (result == DTC_ACCEPTED ? 202 : 409));
	}
	check(sweep_end(), "status sweep: the answers 0 to 300 of the scan state: 503 while not ready, else 202 for accepted only");
}

// What goes in and what has to come out of every text field: quote and backslash escaped, the bytes below
// 0x20 dropped, blank, 0x7F, UTF-8 (ü) and single bytes above 0x7F passed on
#define TEXT_IN     "a\"b\\c\td\ne\001f\037g\rh\033i j\177k\303\274l\200m\377n"
#define TEXT_OUT    "a\\\"b\\\\cdefghi j\177k\303\274l\200m\377n"

static int body_is(int status, const char *reason, uint32_t seq, const char *expected)
{
	char body[DTC_API_BODY_SIZE + GUARD];
	int length, clean = 1;
	size_t i;

	// A missing terminating zero shows up as a difference instead of a read behind the buffer
	memset(body, '~', sizeof(body));
	length = dtc_api_body(status, reason, seq, body, DTC_API_BODY_SIZE);
	for(i = DTC_API_BODY_SIZE; i < sizeof(body); i++) clean = clean && body[i] == '~';
	body[sizeof(body) - 1] = '\0';

	if(clean && length == (int)strlen(expected) && strcmp(body, expected) == 0) return 1;
	if(quiet) return 0;

	printf("  got      %d %s\n  expected %d %s%s\n", length, body, (int)strlen(expected), expected,
	       clean ? "" : "\n  and bytes behind the buffer were written");
	return 0;
}

static int body_is_fixture(int status, const char *reason, uint32_t seq, const char *name)
{
	char expected[256];

	return read_fixture(name, expected, sizeof(expected)) && body_is(status, reason, seq, expected);
}

static void test_body(void)
{
	static const int not_accepted[] = {0, 1, 200, 201, 203, 204, 302, 400, 403, 409, 500, 503, -202, 458, 65738};
	const char *longest = "{\"accepted\":false,\"reason\":\"nothing_to_clear\",\"seq\":2147483647}";
	char what[128];
	int status;
	size_t i;

	check(body_is_fixture(202, NULL, 43, "api_body_accepted.json"), "body 202: accepted, with the number of the new request");
	check(body_is_fixture(409, "busy", 42, "api_body_busy.json"), "body 409 busy, with the number of the scan in the way");
	check(body_is_fixture(409, "read_required", 42, "api_body_read_required.json"), "body 409 read_required");
	check(body_is_fixture(409, "stale_seq", 42, "api_body_stale_seq.json"), "body 409 stale_seq");
	check(body_is_fixture(409, "nothing_to_clear", 42, "api_body_nothing_to_clear.json"), "body 409 nothing_to_clear");
	check(body_is_fixture(503, "not_ready", 0, "api_body_not_ready.json"), "body 503 not_ready");
	check(body_is_fixture(403, "forbidden", 0, "api_body_forbidden.json"), "body 403 forbidden");
	check(body_is_fixture(400, "bad_request", 0, "api_body_bad_request.json"), "body 400 bad_request");

	check(body_is(202, "busy", 43, "{\"accepted\":true,\"seq\":43}"), "body 202: a reason is not written");
	check(body_is(202, "", 43, "{\"accepted\":true,\"seq\":43}"), "body 202: an empty reason is not written either");
	for(i = 0; i < sizeof(not_accepted) / sizeof(not_accepted[0]); i++)
	{
		snprintf(what, sizeof(what), "body %d: only 202 says accepted", not_accepted[i]);
		check(body_is(not_accepted[i], "why", 43, "{\"accepted\":false,\"reason\":\"why\",\"seq\":43}"), what);
	}
	sweep_begin();
	for(status = -70000; status <= 70000; status++)
	{
		if(status != 202) sweep_count(body_is(status, "why", 43, "{\"accepted\":false,\"reason\":\"why\",\"seq\":43}"));
	}
	check(sweep_end(), "body sweep: every status from -70000 to 70000 but 202 says not accepted");

	check(body_is(409, NULL, 42, "{\"accepted\":false,\"reason\":\"\",\"seq\":42}"), "body: a missing reason is an empty text");
	check(body_is(409, "", 42, "{\"accepted\":false,\"reason\":\"\",\"seq\":42}"), "body: an empty reason stays empty");
	check(body_is(409, TEXT_IN, 42, "{\"accepted\":false,\"reason\":\"" TEXT_OUT "\",\"seq\":42}"),
	      "body: the reason is escaped like a JSON string");
	check(body_is(409, "\"busy\\", 42, "{\"accepted\":false,\"reason\":\"\\\"busy\\\\\",\"seq\":42}"),
	      "body: a quote at the start and a backslash at the end of the reason are escaped");
	check(body_is(409, "\rbusy\n", 42, "{\"accepted\":false,\"reason\":\"busy\",\"seq\":42}"),
	      "body: a line end at the start and at the end of the reason is dropped");

	check(body_is(202, NULL, 0, "{\"accepted\":true,\"seq\":0}"), "body 202 with number 0");
	check(body_is(202, NULL, 1, "{\"accepted\":true,\"seq\":1}"), "body 202 with number 1");
	check(body_is(202, NULL, 10, "{\"accepted\":true,\"seq\":10}"), "body 202 with number 10");
	check(body_is(202, NULL, 1000000000u, "{\"accepted\":true,\"seq\":1000000000}"), "body 202 with number 1000000000");
	check(body_is(202, NULL, 2147483647u, "{\"accepted\":true,\"seq\":2147483647}"), "body 202 with the largest number 2147483647");
	check(body_is(202, NULL, 2147483648u, "{\"accepted\":true,\"seq\":2147483647}"),
	      "body 202 with number 2147483648: written as 2147483647, API.md has no number from 2^31 on");
	check(body_is(409, "busy", 4294967295u, "{\"accepted\":false,\"reason\":\"busy\",\"seq\":2147483647}"),
	      "body 409 with number 4294967295: written as 2147483647");
	check(body_is(409, "busy", 2147483647u, "{\"accepted\":false,\"reason\":\"busy\",\"seq\":2147483647}"),
	      "body 409 with the largest number 2147483647");

	check(strlen(longest) + 1 <= DTC_API_BODY_SIZE && body_is(409, "nothing_to_clear", 2147483647u, longest),
	      "body: the longest answer of API.md fits into DTC_API_BODY_SIZE");
}

// An answer into every buffer size from 0 to some bytes more than needed: the state if `status` is given,
// else the body for `body_status`. Returns the number of sizes with a wrong outcome: the complete text if it
// fits with its terminating zero, else -1 and an empty text, with size 0 nothing at all, and never a byte
// from buf[size] on.
static int wrong_sizes(const dtc_api_status_t *status, const char *dtc_json, int body_status, const char *expected)
{
	static char buf[STATE_BUFFER + GUARD];
	const size_t needed = strlen(expected) + 1;
	int wrong = 0;
	size_t size, i;

	if(needed + 3 > STATE_BUFFER) return 1;
	for(size = 0; size <= needed + 3; size++)
	{
		int length, good;

		memset(buf, 0x5A, sizeof(buf));
		length = status != NULL ? dtc_api_state_json(status, dtc_json, buf, size) : dtc_api_body(body_status, "busy", 42, buf, size);

		if(size >= needed) good = length == (int)(needed - 1) && memcmp(buf, expected, needed) == 0;
		else good = length == -1 && (size == 0 || buf[0] == '\0');
		for(i = size; i < sizeof(buf); i++) good = good && buf[i] == 0x5A;

		if(!good)
		{
			if(wrong++ < 5) printf("  wrong outcome for a buffer of %lu bytes, %lu are needed: %d\n", (unsigned long)size,
			                       (unsigned long)needed, length);
		}
	}
	return wrong;
}

static void test_body_buffer(void)
{
	const char *expected = "{\"accepted\":false,\"reason\":\"busy\",\"seq\":42}";
	const int length = (int)strlen(expected);
	char body[128];

	memset(body, 'x', sizeof(body));
	check(dtc_api_body(409, "busy", 42, body, sizeof(body)) == length && strcmp(body, expected) == 0,
	      "body returns its length");
	memset(body, 'x', sizeof(body));
	check(dtc_api_body(409, "busy", 42, body, (size_t)length + 1) == length && body[length] == '\0' &&
	      strcmp(body, expected) == 0 && body[length + 1] == 'x', "body fits exactly with one byte for the terminating zero");
	memset(body, 'x', sizeof(body));
	check(dtc_api_body(409, "busy", 42, body, (size_t)length) == -1 && body[0] == '\0' && body[length] == 'x',
	      "body one byte too long: -1 and an empty text, not a truncated object, nothing behind the buffer");
	memset(body, 'x', sizeof(body));
	check(dtc_api_body(409, "busy", 42, body, 1) == -1 && body[0] == '\0' && body[1] == 'x',
	      "body into a 1 byte buffer: -1 and an empty text");
	memset(body, 'x', sizeof(body));
	check(dtc_api_body(409, "busy", 42, body, 0) == -1 && body[0] == 'x', "body into a 0 byte buffer: -1, nothing is written");
	check(wrong_sizes(NULL, NULL, 409, expected) == 0, "body into every buffer size from 0 to 3 bytes more than needed");
	check(wrong_sizes(NULL, NULL, 202, "{\"accepted\":true,\"seq\":42}") == 0,
	      "body 202 into every buffer size from 0 to 3 bytes more than needed");
}

/* ------------------------------------------------------------------------------------------------ */
/* GET /api/state                                                                                     */
/* ------------------------------------------------------------------------------------------------ */

#define IDLE_DTC    "{\"supported\":true,\"state\":\"idle\",\"action\":\"\",\"src\":\"\",\"seq\":0,\"ecu\":0,\"total\":0," \
                    "\"name\":\"\",\"reason\":\"\",\"age_s\":0,\"count\":0,\"result_seq\":0}"

// The example of API.md
static const dtc_api_status_t example = {
	.id = "a1b2c3d4e5f6", .fw = "4.21", .git = "w906-v1.4.0-9-g0123abc", .boot = 1234567890, .up_s = 812,
	.autopid = "run", .pids = 35, .ecu_online = true, .pass = 1234, .rx_age_ms = 140, .mqtt = "connected",
	.batt_mv = 12400, .sleep_in_s = -1, .heap = 61000, .heap_min = 48000,
};

// The same example field by field, as it has to be written
#define FIELDS  17
static const char *const field_names[FIELDS] = {"api", "id", "fw", "git", "boot", "up", "autopid", "pids", "ecu", "pass",
                                                "rx_age_ms", "mqtt", "batt_v", "sleep_in_s", "heap", "heap_min", "dtc"};
static const char *const example_values[FIELDS] = {"1", "\"a1b2c3d4e5f6\"", "\"4.21\"", "\"w906-v1.4.0-9-g0123abc\"",
                                                   "1234567890", "812", "\"run\"", "35", "\"online\"", "1234", "140",
                                                   "\"connected\"", "12.4", "-1", "61000", "48000", IDLE_DTC};

static int state_is(const dtc_api_status_t *status, const char *dtc_json, const char *expected)
{
	static char json[STATE_BUFFER + GUARD];
	int length, clean = 1;
	size_t i;

	// A missing terminating zero shows up as a difference instead of a read behind the buffer
	memset(json, '~', sizeof(json));
	length = dtc_api_state_json(status, dtc_json, json, STATE_BUFFER);
	for(i = STATE_BUFFER; i < sizeof(json); i++) clean = clean && json[i] == '~';
	json[sizeof(json) - 1] = '\0';

	if(clean && length == (int)strlen(expected) && strcmp(json, expected) == 0) return 1;
	if(quiet) return 0;

	printf("  got      %d %.1500s\n  expected %d %.1500s%s\n", length, json, (int)strlen(expected), expected,
	       clean ? "" : "\n  and bytes behind the buffer were written");
	return 0;
}

// An answer put together from its fields, each given as it has to be written. Returns 0 if it does not fit.
static int expected_fields(char *expected, size_t size, const char *const values[FIELDS])
{
	size_t length = 0;
	int i;

	for(i = 0; i < FIELDS; i++)
	{
		length += (size_t)snprintf(expected + length, size - length, "%s\"%s\":%s", i == 0 ? "{" : ",", field_names[i], values[i]);
		if(length + 2 > size) return 0;
	}
	expected[length] = '}';
	expected[length + 1] = '\0';
	return 1;
}

// The example of API.md with the one field `field` written as `value`. Returns 0 if it does not fit.
static int expected_state(char *expected, size_t size, const char *field, const char *value)
{
	const char *values[FIELDS];
	int i;

	for(i = 0; i < FIELDS; i++) values[i] = strcmp(field_names[i], field) == 0 ? value : example_values[i];
	return expected_fields(expected, size, values);
}

// The answer has to be the example of API.md, with the one field `field` written as `value`
static int field_is(const dtc_api_status_t *status, const char *dtc_json, const char *field, const char *value)
{
	static char expected[STATE_BUFFER];

	return expected_state(expected, sizeof(expected), field, value) && state_is(status, dtc_json, expected);
}

static int state_is_fixture(const dtc_api_status_t *status, const char *dtc_fixture, const char *name)
{
	char dtc_json[512], expected[1024];

	if(dtc_fixture != NULL && !read_fixture(dtc_fixture, dtc_json, sizeof(dtc_json))) return 0;
	return read_fixture(name, expected, sizeof(expected)) && state_is(status, dtc_fixture != NULL ? dtc_json : NULL, expected);
}

// The same answer into every buffer size around its length: each fixture ends in another field
// (-1 for none, the longest numbers, empty texts) when the buffer is too small
static int fixture_in_every_size(const dtc_api_status_t *status, const char *dtc_fixture, const char *name)
{
	char dtc_json[512], expected[1024];

	if(dtc_fixture != NULL && !read_fixture(dtc_fixture, dtc_json, sizeof(dtc_json))) return 0;
	return read_fixture(name, expected, sizeof(expected)) &&
	       wrong_sizes(status, dtc_fixture != NULL ? dtc_json : NULL, 0, expected) == 0;
}

static void test_state_fixtures(void)
{
	dtc_api_status_t status;

	check(state_is_fixture(&example, "dtc_state_idle.json", "api_state_example.json"),
	      "state: the example of API.md, every field in its place");
	check(field_is(&example, IDLE_DTC, "", ""), "state: the example put together field by field is the same text");

	status = example;
	status.up_s = 4500;
	status.ecu_online = false;
	status.pass = 2710;
	status.rx_age_ms = 95000;
	status.mqtt = "disconnected";
	status.batt_mv = 12149;
	status.sleep_in_s = 87;
	status.heap = 58200;
	status.heap_min = 47100;
	check(state_is_fixture(&status, "dtc_state_error.json", "api_state_offline.json"),
	      "state: ignition off, counting down to sleep, after a clear that failed");
	check(fixture_in_every_size(&status, "dtc_state_error.json", "api_state_offline.json"),
	      "state with the ignition off into every buffer size from 0 to 3 bytes more than needed");

	status = example;
	status.boot = 7;
	status.up_s = 0;
	status.autopid = "starting";
	status.pids = 0;
	status.ecu_online = false;
	status.pass = 0;
	status.rx_age_ms = -1;
	status.mqtt = "off";
	status.batt_mv = -1;
	status.heap = 112000;
	status.heap_min = 111000;
	check(state_is_fixture(&status, "dtc_state_unsupported.json", "api_state_starting.json"),
	      "state: right after boot, nothing answered and nothing measured yet");
	check(fixture_in_every_size(&status, "dtc_state_unsupported.json", "api_state_starting.json"),
	      "state right after boot, three times -1, into every buffer size from 0 to 3 bytes more than needed");

	status = example;
	status.id = "0123456789ab";
	status.git = "w906-v1.4.0";
	status.boot = 41;
	status.up_s = 3600;
	status.pass = 3391;
	status.rx_age_ms = 0;
	status.mqtt = "off";
	status.batt_mv = 14350;
	status.heap = 60000;
	status.heap_min = 47999;
	check(state_is_fixture(&status, "dtc_state_running_umlaut.json", "api_state_scan.json"),
	      "state: during a scan, the UTF-8 name of the control unit unchanged");
	check(fixture_in_every_size(&status, "dtc_state_running_umlaut.json", "api_state_scan.json"),
	      "state during a scan into every buffer size from 0 to 3 bytes more than needed");

	status = example;
	status.id = "ffffffffffff";
	status.git = "w906-v1.4.0-9-g0123abc-dirty";
	status.boot = 0xFFFFFFFFu;
	status.up_s = 0xFFFFFFFFu;
	status.pids = 0xFFFFFFFFu;
	status.pass = 0xFFFFFFFFu;
	status.rx_age_ms = INT32_MAX;
	status.batt_mv = INT32_MAX;
	status.sleep_in_s = INT32_MAX;
	status.heap = 0xFFFFFFFFu;
	status.heap_min = 0xFFFFFFFFu;
	check(state_is_fixture(&status, "dtc_state_limits.json", "api_state_limits.json"),
	      "state: every number at its limit, none is written above 2147483647");
	check(fixture_in_every_size(&status, "dtc_state_limits.json", "api_state_limits.json"),
	      "state with every number at its limit into every buffer size from 0 to 3 bytes more than needed");

	memset(&status, 0, sizeof(status));
	check(state_is_fixture(&status, NULL, "api_state_empty.json"),
	      "state: no texts, all numbers 0 and no scan state: empty texts, 0, 0.0 and an empty object");
	check(fixture_in_every_size(&status, NULL, "api_state_empty.json"),
	      "state without texts and without scan state into every buffer size from 0 to 3 bytes more than needed");
}

static void set_unsigned(dtc_api_status_t *status, int field, uint32_t value)
{
	switch(field)
	{
		case 0:  status->boot = value; break;
		case 1:  status->up_s = value; break;
		case 2:  status->pids = value; break;
		case 3:  status->pass = value; break;
		case 4:  status->heap = value; break;
		default: status->heap_min = value; break;
	}
}

static void test_state_numbers(void)
{
	static const char *const unsigned_fields[] = {"boot", "up", "pids", "pass", "heap", "heap_min"};
	static const uint32_t values[] = {0, 1, 9, 10, 11, 99, 100, 101, 255, 256, 1000, 65535, 65536, 1234567890, 999999999,
	                                  1000000000, 2147483646, 2147483647, 2147483648u, 2147483649u, 3000000000u, 4294967295u};
	static const int32_t signed_values[] = {INT32_MIN, -2147483647, -1000, -2, -1, 0, 1, 9, 10, 140, 65536, 1000000000,
	                                        2147483646, INT32_MAX};
	dtc_api_status_t status;
	char text[32], what[128];
	int field;
	size_t i;

	for(field = 0; field < 6; field++)
	{
		status = example;
		set_unsigned(&status, field, 2147483647u);
		snprintf(what, sizeof(what), "state: %s 2147483647 is written as it is", unsigned_fields[field]);
		check(field_is(&status, IDLE_DTC, unsigned_fields[field], "2147483647"), what);

		set_unsigned(&status, field, 2147483648u);
		snprintf(what, sizeof(what), "state: %s 2147483648 is written as 2147483647", unsigned_fields[field]);
		check(field_is(&status, IDLE_DTC, unsigned_fields[field], "2147483647"), what);

		set_unsigned(&status, field, 0);
		snprintf(what, sizeof(what), "state: %s 0 is written as 0", unsigned_fields[field]);
		check(field_is(&status, IDLE_DTC, unsigned_fields[field], "0"), what);

		// Expectation by the printf of the C library
		sweep_begin();
		for(i = 0; i < sizeof(values) / sizeof(values[0]); i++)
		{
			set_unsigned(&status, field, values[i]);
			snprintf(text, sizeof(text), "%lu", (unsigned long)(values[i] > 2147483647u ? 2147483647u : values[i]));
			sweep_count(field_is(&status, IDLE_DTC, unsigned_fields[field], text));
		}
		snprintf(what, sizeof(what), "state: %s with 22 numbers from 0 to 4294967295, decimal and at most 2147483647",
		         unsigned_fields[field]);
		check(sweep_end(), what);
	}

	status = example;
	status.rx_age_ms = -1;
	check(field_is(&status, IDLE_DTC, "rx_age_ms", "-1"), "state: rx_age_ms -1 (nothing answered since boot) is written as -1");
	status.rx_age_ms = -2;
	check(field_is(&status, IDLE_DTC, "rx_age_ms", "-1"), "state: rx_age_ms -2 is written as -1");
	status.rx_age_ms = INT32_MIN;
	check(field_is(&status, IDLE_DTC, "rx_age_ms", "-1"), "state: the smallest rx_age_ms is written as -1");
	status.rx_age_ms = 0;
	check(field_is(&status, IDLE_DTC, "rx_age_ms", "0"), "state: rx_age_ms 0 is written as 0");
	status.rx_age_ms = INT32_MAX;
	check(field_is(&status, IDLE_DTC, "rx_age_ms", "2147483647"), "state: the largest rx_age_ms 2147483647");

	status = example;
	status.sleep_in_s = -1;
	check(field_is(&status, IDLE_DTC, "sleep_in_s", "-1"), "state: sleep_in_s -1 (not counting down) is written as -1");
	status.sleep_in_s = -2;
	check(field_is(&status, IDLE_DTC, "sleep_in_s", "-1"), "state: sleep_in_s -2 is written as -1");
	status.sleep_in_s = INT32_MIN;
	check(field_is(&status, IDLE_DTC, "sleep_in_s", "-1"), "state: the smallest sleep_in_s is written as -1");
	status.sleep_in_s = 0;
	check(field_is(&status, IDLE_DTC, "sleep_in_s", "0"), "state: sleep_in_s 0 is written as 0");
	status.sleep_in_s = INT32_MAX;
	check(field_is(&status, IDLE_DTC, "sleep_in_s", "2147483647"), "state: the largest sleep_in_s 2147483647");

	sweep_begin();
	for(i = 0; i < sizeof(signed_values) / sizeof(signed_values[0]); i++)
	{
		snprintf(text, sizeof(text), "%ld", signed_values[i] < 0 ? -1L : (long)signed_values[i]);
		status = example;
		status.rx_age_ms = signed_values[i];
		sweep_count(field_is(&status, IDLE_DTC, "rx_age_ms", text));
		status = example;
		status.sleep_in_s = signed_values[i];
		sweep_count(field_is(&status, IDLE_DTC, "sleep_in_s", text));
	}
	check(sweep_end(), "state: rx_age_ms and sleep_in_s with 14 numbers from the smallest to the largest, -1 for every negative one");

	status = example;
	status.ecu_online = false;
	check(field_is(&status, IDLE_DTC, "ecu", "\"offline\""), "state: ecu offline");
	status.ecu_online = true;
	check(field_is(&status, IDLE_DTC, "ecu", "\"online\""), "state: ecu online");
}

typedef struct
{
	int32_t millivolts;
	const char *text;
	const char *what;
} voltage_example_t;

static const voltage_example_t voltage_examples[] = {
	{12449, "12.4", "state: 12449 mV is 12.4, rounded down"},
	{12450, "12.5", "state: 12450 mV is 12.5, rounded up from the half"},
	{12400, "12.4", "state: 12400 mV is 12.4"},
	{12500, "12.5", "state: 12500 mV is 12.5"},
	{12549, "12.5", "state: 12549 mV is 12.5"},
	{12550, "12.6", "state: 12550 mV is 12.6"},
	{0, "0.0", "state: 0 mV is 0.0, not -1 and not 0"},
	{1, "0.0", "state: 1 mV is 0.0"},
	{49, "0.0", "state: 49 mV is 0.0"},
	{50, "0.1", "state: 50 mV is 0.1"},
	{949, "0.9", "state: 949 mV is 0.9"},
	{950, "1.0", "state: 950 mV is 1.0, the rounding carries into the volts"},
	{9949, "9.9", "state: 9949 mV is 9.9"},
	{9950, "10.0", "state: 9950 mV is 10.0"},
	{14000, "14.0", "state: 14000 mV is 14.0, the decimal is written when it is 0"},
	{99949, "99.9", "state: 99949 mV is 99.9"},
	{99950, "100.0", "state: 99950 mV is 100.0"},
	{2147483647, "2147483.6", "state: the largest voltage 2147483647 mV is 2147483.6"},
	{2147483599, "2147483.6", "state: 2147483599 mV is 2147483.6"},
	{-1, "-1", "state: -1 mV (not measured) is written as -1"},
	{-2, "-1", "state: -2 mV is written as -1"},
	{-49, "-1", "state: -49 mV is written as -1, not as -0.0"},
	{-12400, "-1", "state: -12400 mV is written as -1"},
	{INT32_MIN, "-1", "state: the smallest voltage is written as -1"},
};

// The voltage by another way to the same rule: the digit of the tenths, raised if the rest is 50 mV or more
static void model_volts(char *text, size_t size, int32_t millivolts)
{
	long volts = millivolts / 1000;
	int tenth = millivolts % 1000 / 100;

	if(millivolts < 0)
	{
		snprintf(text, size, "-1");
		return;
	}
	if(millivolts % 100 >= 50) tenth++;
	if(tenth == 10)
	{
		tenth = 0;
		volts++;
	}
	snprintf(text, size, "%ld.%d", volts, tenth);
}

static void test_state_voltage(void)
{
	dtc_api_status_t status = example;
	char text[64];
	int32_t millivolts;
	size_t i;

	for(i = 0; i < sizeof(voltage_examples) / sizeof(voltage_examples[0]); i++)
	{
		status.batt_mv = voltage_examples[i].millivolts;
		check(field_is(&status, IDLE_DTC, "batt_v", voltage_examples[i].text), voltage_examples[i].what);
	}

	sweep_begin();
	for(millivolts = 0; millivolts <= 20000; millivolts++)
	{
		model_volts(text, sizeof(text), millivolts);
		status.batt_mv = millivolts;
		sweep_count(field_is(&status, IDLE_DTC, "batt_v", text));
	}
	check(sweep_end(), "state: every voltage from 0 to 20000 mV is rounded to the nearest tenth of a volt");

	// From there to the largest one in steps of a hundredth of the value: no range of voltages is left out,
	// and the digits below the tenth take every value on the way
	sweep_begin();
	for(millivolts = 20000; millivolts <= INT32_MAX - millivolts / 100 - 7; millivolts += millivolts / 100 + 7)
	{
		model_volts(text, sizeof(text), millivolts);
		status.batt_mv = millivolts;
		sweep_count(field_is(&status, IDLE_DTC, "batt_v", text));
	}
	check(sweep_end(), "state: voltages from 20 V to the largest one are rounded to the nearest tenth of a volt");
}

static void set_text(dtc_api_status_t *status, int field, const char *text)
{
	switch(field)
	{
		case 0:  status->id = text; break;
		case 1:  status->fw = text; break;
		case 2:  status->git = text; break;
		case 3:  status->autopid = text; break;
		default: status->mqtt = text; break;
	}
}

static void test_state_texts(void)
{
	static const char *const text_fields[] = {"id", "fw", "git", "autopid", "mqtt"};
	dtc_api_status_t status;
	char in[8], out[16], written[4], what[128];
	int field, byte;

	for(field = 0; field < 5; field++)
	{
		status = example;
		set_text(&status, field, NULL);
		snprintf(what, sizeof(what), "state: a missing %s is an empty text", text_fields[field]);
		check(field_is(&status, IDLE_DTC, text_fields[field], "\"\""), what);

		set_text(&status, field, "");
		snprintf(what, sizeof(what), "state: an empty %s stays empty", text_fields[field]);
		check(field_is(&status, IDLE_DTC, text_fields[field], "\"\""), what);

		set_text(&status, field, "a\"b");
		snprintf(what, sizeof(what), "state: a quote in %s is escaped", text_fields[field]);
		check(field_is(&status, IDLE_DTC, text_fields[field], "\"a\\\"b\""), what);

		set_text(&status, field, "a\\b");
		snprintf(what, sizeof(what), "state: a backslash in %s is escaped", text_fields[field]);
		check(field_is(&status, IDLE_DTC, text_fields[field], "\"a\\\\b\""), what);

		set_text(&status, field, "a\037b\001c\nd");
		snprintf(what, sizeof(what), "state: bytes below 0x20 in %s are dropped", text_fields[field]);
		check(field_is(&status, IDLE_DTC, text_fields[field], "\"abcd\""), what);

		set_text(&status, field, "a b\177c");
		snprintf(what, sizeof(what), "state: blank (0x20) and 0x7F in %s are kept", text_fields[field]);
		check(field_is(&status, IDLE_DTC, text_fields[field], "\"a b\177c\""), what);

		set_text(&status, field, "W\303\244hlhebel");
		snprintf(what, sizeof(what), "state: UTF-8 in %s is passed on", text_fields[field]);
		check(field_is(&status, IDLE_DTC, text_fields[field], "\"W\303\244hlhebel\""), what);

		set_text(&status, field, TEXT_IN);
		snprintf(what, sizeof(what), "state: %s with everything at once", text_fields[field]);
		check(field_is(&status, IDLE_DTC, text_fields[field], "\"" TEXT_OUT "\""), what);

		set_text(&status, field, "\"a\"");
		snprintf(what, sizeof(what), "state: a quote at the start and at the end of %s is escaped", text_fields[field]);
		check(field_is(&status, IDLE_DTC, text_fields[field], "\"\\\"a\\\"\""), what);

		set_text(&status, field, "\ra\n");
		snprintf(what, sizeof(what), "state: a line end at the start and at the end of %s is dropped", text_fields[field]);
		check(field_is(&status, IDLE_DTC, text_fields[field], "\"a\""), what);

		// Expectation written out here: below 0x20 nothing, a backslash in front of quote and backslash.
		// The byte stands between two letters, alone (first and last at once) and twice in a row.
		sweep_begin();
		for(byte = 1; byte <= 255; byte++)
		{
			if(byte < 0x20) written[0] = '\0';
			else snprintf(written, sizeof(written), "%s%c", (byte == '"' || byte == '\\') ? "\\" : "", byte);

			snprintf(in, sizeof(in), "x%cy", byte);
			snprintf(out, sizeof(out), "\"x%sy\"", written);
			set_text(&status, field, in);
			sweep_count(field_is(&status, IDLE_DTC, text_fields[field], out));

			snprintf(in, sizeof(in), "%c", byte);
			snprintf(out, sizeof(out), "\"%s\"", written);
			set_text(&status, field, in);
			sweep_count(field_is(&status, IDLE_DTC, text_fields[field], out));

			snprintf(in, sizeof(in), "%c%c", byte, byte);
			snprintf(out, sizeof(out), "\"%s%s\"", written, written);
			set_text(&status, field, in);
			sweep_count(field_is(&status, IDLE_DTC, text_fields[field], out));
		}
		snprintf(what, sizeof(what), "state: every byte 1..255 in %s, between letters, alone and twice", text_fields[field]);
		check(sweep_end(), what);
	}
}

static void test_state_dtc(void)
{
	static char long_dtc[1601], expected[STATE_BUFFER];
	int length;

	check(field_is(&example, NULL, "dtc", "{}"), "state: without a scan state the dtc field is an empty object");
	check(field_is(&example, "", "dtc", "{}"), "state: with an empty scan state the dtc field is an empty object");
	check(field_is(&example, "{\"name\":\"a\\\"b\\\\c \303\274\"}", "dtc", "{\"name\":\"a\\\"b\\\\c \303\274\"}"),
	      "state: the scan state is passed on as it is, it is not escaped a second time");
	check(field_is(&example, "{\"a\":\"b\tc\001d\037e\"}\n", "dtc", "{\"a\":\"b\tc\001d\037e\"}\n"),
	      "state: bytes below 0x20 in the scan state are passed on too, the text is not touched");
	check(field_is(&example, "x", "dtc", "x"), "state: a scan state of one character is passed on");

	memset(long_dtc, 'A', sizeof(long_dtc) - 1);
	long_dtc[0] = '{';
	long_dtc[sizeof(long_dtc) - 2] = '}';
	check(field_is(&example, long_dtc, "dtc", long_dtc), "state: an answer of more than 1600 bytes is complete");
	length = dtc_api_state_json(&example, long_dtc, expected, sizeof(expected));
	check(length > 1800 && length == (int)strlen(expected), "state: the length of a long answer is returned");
}

static void test_state_buffer(void)
{
	static char expected[1024], buf[1024];
	int length;

	// If the example did not fit, the empty text makes every check below fail
	if(!expected_state(expected, sizeof(expected), "", "")) expected[0] = '\0';
	length = (int)strlen(expected);

	memset(buf, 'x', sizeof(buf));
	check(dtc_api_state_json(&example, IDLE_DTC, buf, sizeof(buf)) == length && strcmp(buf, expected) == 0,
	      "state returns its length");
	memset(buf, 'x', sizeof(buf));
	check(dtc_api_state_json(&example, IDLE_DTC, buf, (size_t)length + 1) == length && buf[length] == '\0' &&
	      strcmp(buf, expected) == 0 && buf[length + 1] == 'x', "state fits exactly with one byte for the terminating zero");
	memset(buf, 'x', sizeof(buf));
	check(dtc_api_state_json(&example, IDLE_DTC, buf, (size_t)length) == -1 && buf[0] == '\0' && buf[length] == 'x',
	      "state one byte too long: -1 and an empty text, not a truncated object, nothing behind the buffer");
	memset(buf, 'x', sizeof(buf));
	check(dtc_api_state_json(&example, IDLE_DTC, buf, 1) == -1 && buf[0] == '\0' && buf[1] == 'x',
	      "state into a 1 byte buffer: -1 and an empty text");
	memset(buf, 'x', sizeof(buf));
	check(dtc_api_state_json(&example, IDLE_DTC, buf, 0) == -1 && buf[0] == 'x', "state into a 0 byte buffer: -1, nothing is written");
	check(wrong_sizes(&example, IDLE_DTC, 0, expected) == 0, "state into every buffer size from 0 to 3 bytes more than needed");
}

/* ------------------------------------------------------------------------------------------------ */
/* All fields at once                                                                                 */
/* ------------------------------------------------------------------------------------------------ */

// The checks above change one field of the example at a time. Here every field gets a pseudo-random value
// at the same time and the answer is compared with a second way of writing it: the printf of the C library
// for the numbers, a look at the bits for the characters. A field that depends on another one shows up here.
static uint32_t walk_random_state;

static uint32_t walk_random(uint32_t below)
{
	walk_random_state = walk_random_state * 1664525u + 1013904223u;
	return (walk_random_state >> 8) % below;
}

static uint32_t walk_number(void)
{
	static const uint32_t numbers[] = {0, 1, 9, 10, 35, 99, 812, 65535, 65536, 1234567890, 2147483647, 2147483648u,
	                                   3000000001u, 4294967295u};
	uint32_t high;

	if(walk_random(4) != 0) return numbers[walk_random(sizeof(numbers) / sizeof(numbers[0]))];
	// Two statements: in one expression the compiler chooses which half is drawn first
	high = walk_random(65536);
	return (high << 16) | walk_random(65536);
}

static int32_t walk_signed(void)
{
	static const int32_t numbers[] = {INT32_MIN, -12400, -2, -1, 0, 1, 49, 50, 140, 12449, 12450, 14350, 99950, 100000,
	                                  2147483599, INT32_MAX};
	uint32_t high;
	int32_t number;

	if(walk_random(4) != 0) return numbers[walk_random(sizeof(numbers) / sizeof(numbers[0]))];
	high = walk_random(32768);
	number = (int32_t)((high << 16) | walk_random(65536));
	return walk_random(4) == 0 ? -number : number;
}

static const char *walk_text(void)
{
	static const char *const texts[] = {NULL, "", "run", "off", "starting", "connected", "4.21", "a1b2c3d4e5f6", "a\"b", "\\",
	                                    "\"", "\n", "x\ty\001", "W\303\244hlhebel", "w906-v1.4.0-9-g0123abc"};

	return texts[walk_random(sizeof(texts) / sizeof(texts[0]))];
}

static void model_text(char *out, size_t size, const char *text)
{
	size_t length = 0;

	out[length++] = '"';
	for(; text != NULL && *text != '\0' && length + 4 < size; text++)
	{
		if(strchr("\"\\", *text) != NULL) out[length++] = '\\';
		// From 0x20 on one of the three high bits is set
		if((*text & 0xE0) != 0) out[length++] = *text;
	}
	out[length++] = '"';
	out[length] = '\0';
}

static void model_number(char *text, size_t size, uint32_t number)
{
	snprintf(text, size, "%lu", (unsigned long)(number > 2147483647u ? 2147483647u : number));
}

static void model_number_or_none(char *text, size_t size, int32_t number)
{
	snprintf(text, size, "%ld", number < 0 ? -1L : (long)number);
}

static void test_state_walk(void)
{
	static const char *const scans[] = {NULL, "", "{}", IDLE_DTC};
	static char expected[STATE_BUFFER];
	char written[FIELDS][64];
	const char *values[FIELDS];
	dtc_api_status_t status;
	const char *dtc_json;
	int step, i;

	walk_random_state = 906;
	sweep_begin();
	for(step = 0; step < 5000; step++)
	{
		status.id = walk_text();
		status.fw = walk_text();
		status.git = walk_text();
		status.boot = walk_number();
		status.up_s = walk_number();
		status.autopid = walk_text();
		status.pids = walk_number();
		status.ecu_online = walk_random(2) != 0;
		status.pass = walk_number();
		status.rx_age_ms = walk_signed();
		status.mqtt = walk_text();
		status.batt_mv = walk_signed();
		status.sleep_in_s = walk_signed();
		status.heap = walk_number();
		status.heap_min = walk_number();
		dtc_json = scans[walk_random(sizeof(scans) / sizeof(scans[0]))];

		// In the order of field_names
		snprintf(written[0], sizeof(written[0]), "1");
		model_text(written[1], sizeof(written[1]), status.id);
		model_text(written[2], sizeof(written[2]), status.fw);
		model_text(written[3], sizeof(written[3]), status.git);
		model_number(written[4], sizeof(written[4]), status.boot);
		model_number(written[5], sizeof(written[5]), status.up_s);
		model_text(written[6], sizeof(written[6]), status.autopid);
		model_number(written[7], sizeof(written[7]), status.pids);
		snprintf(written[8], sizeof(written[8]), "%s", status.ecu_online ? "\"online\"" : "\"offline\"");
		model_number(written[9], sizeof(written[9]), status.pass);
		model_number_or_none(written[10], sizeof(written[10]), status.rx_age_ms);
		model_text(written[11], sizeof(written[11]), status.mqtt);
		model_volts(written[12], sizeof(written[12]), status.batt_mv);
		model_number_or_none(written[13], sizeof(written[13]), status.sleep_in_s);
		model_number(written[14], sizeof(written[14]), status.heap);
		model_number(written[15], sizeof(written[15]), status.heap_min);
		for(i = 0; i < FIELDS - 1; i++) values[i] = written[i];
		values[FIELDS - 1] = (dtc_json != NULL && dtc_json[0] != '\0') ? dtc_json : "{}";

		sweep_count(expected_fields(expected, sizeof(expected), values) && state_is(&status, dtc_json, expected));
	}
	check(sweep_end(), "state walk: 5000 answers with pseudo-random values in all fields at once, module and model agree");
}

// The body the same way: every combination of a status, a number and a reason
static void test_body_combinations(void)
{
	static const int states[] = {0, 200, 202, 204, 400, 403, 409, 500, 503};
	static const uint32_t numbers[] = {0, 1, 7, 42, 43, 65535, 65536, 2147483647, 2147483648u, 4294967295u};
	static const char *const reasons[] = {NULL, "", "busy", "not_ready", "nothing_to_clear", "a\"b\\c\n"};
	char reason[64], number[16], expected[DTC_API_BODY_SIZE + 64];
	size_t a, b, c;

	sweep_begin();
	for(a = 0; a < sizeof(states) / sizeof(states[0]); a++)
	{
		for(b = 0; b < sizeof(numbers) / sizeof(numbers[0]); b++)
		{
			for(c = 0; c < sizeof(reasons) / sizeof(reasons[0]); c++)
			{
				model_text(reason, sizeof(reason), reasons[c]);
				model_number(number, sizeof(number), numbers[b]);
				if(states[a] == 202) snprintf(expected, sizeof(expected), "{\"accepted\":true,\"seq\":%s}", number);
				else snprintf(expected, sizeof(expected), "{\"accepted\":false,\"reason\":%s,\"seq\":%s}", reason, number);
				sweep_count(body_is(states[a], reasons[c], numbers[b], expected));
			}
		}
	}
	check(sweep_end(), "body sweep: every combination of 9 status values, 10 numbers and 6 reasons, module and model agree");
}

/* ------------------------------------------------------------------------------------------------ */
/* Long inputs and long answers: nothing is cut off at a length nobody wrote down                     */
/* ------------------------------------------------------------------------------------------------ */

static void test_long_inputs(void)
{
	static const char *const text_fields[] = {"id", "fw", "git", "autopid", "mqtt"};
	static char in[BIG], out[BIG + GUARD], expected[BIG], long_value[BIG], value[512];
	const char *busy = "{\"accepted\":false,\"reason\":\"busy\",\"seq\":42}";
	dtc_api_status_t status;
	char what[128];
	int field, length, i, complete;

	memset(in, 'a', 300);
	strcpy(in + 300, ".example.com");
	check(!dtc_api_host_allowed(in), "host: a foreign name of 300 characters is refused");
	memset(in, '1', 300);
	in[300] = '\0';
	check(!dtc_api_host_allowed(in), "host: 300 digits are refused");
	strcpy(in, "wican_");
	memset(in + 6, 'a', 300);
	strcpy(in + 306, ".local");
	check(!dtc_api_host_allowed(in), "host: a name with an id of 300 characters is refused");
	strcpy(in, "192.168.80.1:");
	memset(in + 13, '8', 300);
	in[313] = '\0';
	check(!dtc_api_host_allowed(in), "host: an address with a port of 300 digits is refused");

	memset(in, 'x', 200);
	strcpy(in + 200, "&action=clear&seq=43");
	check(request_is("1", HOST, in, 0, true, 43), "request: action and seq are found behind an unknown parameter of 200 characters");
	for(i = 0; i < 40; i++) memcpy(in + 2 * i, "x&", 2);
	strcpy(in + 80, "action=clear&seq=43");
	check(request_is("1", HOST, in, 0, true, 43), "request: action and seq are found behind 40 unknown parameters");
	strcpy(in, "action=read");
	memset(in + 11, 'x', 300);
	in[311] = '\0';
	check(request_is("1", HOST, in, 400, false, 0), "request: an action of 300 characters that starts with read is a bad request");
	strcpy(in, "action=clear&seq=");
	memset(in + 17, '0', 300);
	strcpy(in + 317, "43");
	check(request_is("1", HOST, in, 400, false, 0), "request: a seq of 302 digits with the value 43 is a bad request");
	memset(in, '1', 300);
	in[300] = '\0';
	check(request_is(in, HOST, "action=read", 403, false, 0), "request: a header of 300 times 1 is forbidden");

	memset(in, 'A', 300);
	in[300] = '\0';
	complete = fits(snprintf(value, sizeof(value), "\"%s\"", in), sizeof(value));
	for(field = 0; field < 5; field++)
	{
		status = example;
		status.id = field == 0 ? in : example.id;
		status.fw = field == 1 ? in : example.fw;
		status.git = field == 2 ? in : example.git;
		status.autopid = field == 3 ? in : example.autopid;
		status.mqtt = field == 4 ? in : example.mqtt;
		snprintf(what, sizeof(what), "state: %s of 300 characters is written completely", text_fields[field]);
		check(complete && field_is(&status, IDLE_DTC, text_fields[field], value), what);
	}
	complete = fits(snprintf(expected, sizeof(expected), "{\"accepted\":false,\"reason\":\"%s\",\"seq\":42}", in), sizeof(expected));
	length = dtc_api_body(409, in, 42, out, BIG);
	check(complete && length == (int)strlen(expected) && strcmp(out, expected) == 0,
	      "body: a reason of 300 characters is written completely");

	// Longer than 65535 bytes
	memset(in, 'A', BIG_TEXT);
	in[0] = '{';
	in[BIG_TEXT - 1] = '}';
	in[BIG_TEXT] = '\0';
	memset(out, '~', sizeof(out));
	length = dtc_api_state_json(&example, in, out, BIG);
	out[sizeof(out) - 1] = '\0';
	check(expected_state(expected, sizeof(expected), "dtc", in) && length > BIG_TEXT && length == (int)strlen(expected) &&
	      strcmp(out, expected) == 0 && out[BIG] == '~', "state: an answer of more than 70000 bytes is complete and its length is returned");

	in[0] = 'A';
	in[BIG_TEXT - 1] = 'A';
	complete = fits(snprintf(long_value, sizeof(long_value), "\"%s\"", in), sizeof(long_value));
	for(field = 0; field < 5; field++)
	{
		status = example;
		status.id = field == 0 ? in : example.id;
		status.fw = field == 1 ? in : example.fw;
		status.git = field == 2 ? in : example.git;
		status.autopid = field == 3 ? in : example.autopid;
		status.mqtt = field == 4 ? in : example.mqtt;
		memset(out, '~', sizeof(out));
		length = dtc_api_state_json(&status, IDLE_DTC, out, BIG);
		out[sizeof(out) - 1] = '\0';
		snprintf(what, sizeof(what), "state: %s of 70000 characters is written completely", text_fields[field]);
		check(complete && expected_state(expected, sizeof(expected), text_fields[field], long_value) &&
		      length == (int)strlen(expected) && strcmp(out, expected) == 0 && out[BIG] == '~', what);
	}

	complete = fits(snprintf(expected, sizeof(expected), "{\"accepted\":false,\"reason\":\"%s\",\"seq\":42}", in), sizeof(expected));
	memset(out, '~', sizeof(out));
	length = dtc_api_body(409, in, 42, out, BIG);
	out[sizeof(out) - 1] = '\0';
	check(complete && length > BIG_TEXT && length == (int)strlen(expected) && strcmp(out, expected) == 0 && out[BIG] == '~',
	      "body: an answer of more than 70000 bytes is complete and its length is returned");

	// A buffer size that is 0 in 16 bit
	complete = expected_state(expected, sizeof(expected), "", "");
	memset(out, '~', sizeof(out));
	length = dtc_api_state_json(&example, IDLE_DTC, out, 65536);
	out[sizeof(out) - 1] = '\0';
	check(complete && length == (int)strlen(expected) && strcmp(out, expected) == 0, "state into a buffer of 65536 bytes");
	memset(out, '~', sizeof(out));
	length = dtc_api_body(409, "busy", 42, out, 65536);
	out[sizeof(out) - 1] = '\0';
	check(length == (int)strlen(busy) && strcmp(out, busy) == 0, "body into a buffer of 65536 bytes");
}

// The lengths of the sweeps below: every one up to 600, then those around 65536. A length that is counted
// in 8 or 16 bit is small again at 256 + n and at 65536 + n.
static size_t next_length(size_t length)
{
	return length == 600 ? 65530 : length + 1;
}

#define LAST_LENGTH     65580

static void test_length_sweeps(void)
{
	static char in[BIG];
	uint32_t number;
	size_t n, i;

	sweep_begin();
	for(n = 33; n <= LAST_LENGTH; n = next_length(n))
	{
		strcpy(in, "wican_");
		memset(in + 6, 'a', n);
		strcpy(in + 6 + n, ".local");
		sweep_count(host_answer_is(in, false));
	}
	check(sweep_end(), "host sweep: ids of 33 to 600 and of 65530 to 65580 characters are refused");

	sweep_begin();
	for(n = 6; n <= LAST_LENGTH; n = next_length(n))
	{
		strcpy(in, "192.168.80.1:");
		memset(in + 13, '8', n);
		in[13 + n] = '\0';
		sweep_count(host_answer_is(in, false));
		strcpy(in, "wican_a1b2.local:");
		memset(in + 17, '0', n);
		in[17 + n] = '\0';
		sweep_count(host_answer_is(in, false));
	}
	check(sweep_end(), "host sweep: ports of 6 to 600 and of 65530 to 65580 digits are refused");

	sweep_begin();
	for(n = 2; n <= LAST_LENGTH; n = next_length(n))
	{
		memset(in, '1', n);
		in[n] = '\0';
		sweep_count(request_is(in, HOST, "action=read", 403, false, 0));
	}
	check(sweep_end(), "request sweep: a header of 2 to 600 and of 65530 to 65580 times 1 is forbidden");

	sweep_begin();
	for(n = 0; n <= LAST_LENGTH; n = next_length(n))
	{
		memset(in, 'x', n);
		strcpy(in + n, "&action=clear&seq=43");
		sweep_count(request_is("1", HOST, in, 0, true, 43));
	}
	check(sweep_end(), "request sweep: action and seq are found behind an unknown parameter of 0 to 600 and of 65530 to 65580 characters");

	sweep_begin();
	for(n = 0; n <= 600; n++)
	{
		for(i = 0; i < n; i++) memcpy(in + 2 * i, "x&", 2);
		strcpy(in + 2 * n, "action=clear&seq=43");
		sweep_count(request_is("1", HOST, in, 0, true, 43));
	}
	for(i = 0; i < 33000; i++) memcpy(in + 2 * i, "x&", 2);
	strcpy(in + 66000, "action=clear&seq=43");
	sweep_count(request_is("1", HOST, in, 0, true, 43));
	check(sweep_end(), "request sweep: action and seq are found behind 0 to 600 and behind 33000 unknown parameters");

	sweep_begin();
	for(n = 1; n <= LAST_LENGTH; n = next_length(n))
	{
		strcpy(in, "action=read");
		memset(in + 11, 'x', n);
		in[11 + n] = '\0';
		sweep_count(request_is("1", HOST, in, 400, false, 0));
		strcpy(in, "action=clear");
		memset(in + 12, 'x', n);
		strcpy(in + 12 + n, "&seq=43");
		sweep_count(request_is("1", HOST, in, 400, false, 0));
	}
	check(sweep_end(), "request sweep: read and clear followed by 1 to 600 and by 65530 to 65580 characters are no action");

	sweep_begin();
	for(n = 1; n <= LAST_LENGTH; n = next_length(n))
	{
		strcpy(in, "action");
		memset(in + 6, 'x', n);
		strcpy(in + 6 + n, "=bogus&action=clear&seq=43");
		sweep_count(request_is("1", HOST, in, 0, true, 43));
		strcpy(in, "action=clear&seq");
		memset(in + 16, 'x', n);
		strcpy(in + 16 + n, "=5&seq=43");
		sweep_count(request_is("1", HOST, in, 0, true, 43));
	}
	check(sweep_end(), "request sweep: the names action and seq followed by 1 to 600 and by 65530 to 65580 characters are unknown parameters");

	// Expectation computed here: 43 with n zeros is 43 * 10^n, which is a number of a read up to n = 7
	sweep_begin();
	number = 43;
	for(n = 1; n <= LAST_LENGTH; n = next_length(n))
	{
		if(n <= 7) number *= 10;
		strcpy(in, "action=clear&seq=43");
		memset(in + 19, '0', n);
		in[19 + n] = '\0';
		sweep_count(request_is("1", HOST, in, n <= 7 ? 0 : 400, n <= 7, n <= 7 ? number : 0));
		// The same digits behind zeros: more than 10 digits, whatever their value
		strcpy(in, "action=clear&seq=");
		memset(in + 17, '0', n + 8);
		strcpy(in + 25 + n, "43");
		sweep_count(request_is("1", HOST, in, 400, false, 0));
	}
	check(sweep_end(), "request sweep: 43 followed by 1 to 600 and by 65530 to 65580 zeros is taken up to 430000000, "
	      "43 behind 9 and more zeros never");
}

// An answer that does not fit is refused in a large buffer like in a small one
static void test_large_buffers(void)
{
	static const size_t sizes[] = {1, 2, 64, DTC_API_BODY_SIZE, 255, 256, 257, 768, 1024, 2047, 2048, 4095, 4096, 4097,
	                               32767, 32768, 65535, 65536, 65537, 70000};
	static char in[BIG], out[BIG + GUARD];
	int length, good;
	size_t i, at;

	memset(in, 'A', BIG_TEXT);
	in[BIG_TEXT] = '\0';

	sweep_begin();
	for(i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
	{
		memset(out, '~', sizeof(out));
		length = dtc_api_state_json(&example, in, out, sizes[i]);
		good = length == -1 && out[0] == '\0';
		for(at = sizes[i]; at < sizeof(out); at++) good = good && out[at] == '~';
		if(!good && !quiet) printf("  wrong outcome for a buffer of %lu bytes: %d\n", (unsigned long)sizes[i], length);
		sweep_count(good);
	}
	check(sweep_end(), "state of more than 70000 bytes into 20 buffers of 1 to 70000 bytes: -1 and an empty text, "
	      "nothing behind the buffer");

	sweep_begin();
	for(i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
	{
		memset(out, '~', sizeof(out));
		length = dtc_api_body(409, in, 42, out, sizes[i]);
		good = length == -1 && out[0] == '\0';
		for(at = sizes[i]; at < sizeof(out); at++) good = good && out[at] == '~';
		if(!good && !quiet) printf("  wrong outcome for a buffer of %lu bytes: %d\n", (unsigned long)sizes[i], length);
		sweep_count(good);
	}
	check(sweep_end(), "body of more than 70000 bytes into 20 buffers of 1 to 70000 bytes: -1 and an empty text, "
	      "nothing behind the buffer");
}

/* ------------------------------------------------------------------------------------------------ */
/* Sleep                                                                                              */
/* ------------------------------------------------------------------------------------------------ */

static void test_defer_sleep(void)
{
	int bit;

	check(DTC_API_SLEEP_DEFER_MS == 60000u, "sleep waits for a scan for 60 s at most");
	check(dtc_api_defer_sleep(true, 0), "sleep waits while a scan is busy");
	check(dtc_api_defer_sleep(true, 1), "sleep waits 1 ms after it was due");
	check(dtc_api_defer_sleep(true, 59999), "sleep waits 59.999 s after it was due");
	check(dtc_api_defer_sleep(true, 60000), "sleep waits exactly 60 s after it was due");
	check(!dtc_api_defer_sleep(true, 60001), "sleep does not wait 60.001 s after it was due, although a scan is busy");
	check(!dtc_api_defer_sleep(true, 120000), "sleep does not wait 120 s after it was due");
	check(!dtc_api_defer_sleep(true, 4294967296ull), "sleep does not wait 2^32 ms after it was due");
	check(!dtc_api_defer_sleep(true, 4294967296ull + 5000), "sleep does not wait 2^32 ms + 5 s after it was due");
	check(!dtc_api_defer_sleep(true, UINT64_MAX), "sleep does not wait at the end of the clock");
	check(!dtc_api_defer_sleep(false, 0), "sleep does not wait without a scan");
	check(!dtc_api_defer_sleep(false, 1), "sleep does not wait without a scan, 1 ms after it was due");
	check(!dtc_api_defer_sleep(false, 60000), "sleep does not wait without a scan, 60 s after it was due");
	check(!dtc_api_defer_sleep(false, 60001), "sleep does not wait without a scan, 60.001 s after it was due");

	// A time that is shortened to its low bits is small again at a power of two
	sweep_begin();
	for(bit = 16; bit < 64; bit++)
	{
		sweep_count(!dtc_api_defer_sleep(true, 1ull << bit));
		sweep_count(!dtc_api_defer_sleep(true, (1ull << bit) + 60000u));
		sweep_count(!dtc_api_defer_sleep(false, 1ull << bit));
	}
	check(sweep_end(), "sleep does not wait 2^16 to 2^63 ms after it was due, with and without 60 s more");
}

int main(void)
{
	alarm(WATCHDOG_S);

	test_fixtures_are_the_contract();
	test_host_examples();
	test_host_sweeps();
	test_request_examples();
	test_request_sweeps();
	test_request_hosts();
	test_status();
	test_body();
	test_body_buffer();
	test_state_fixtures();
	test_state_numbers();
	test_state_voltage();
	test_state_texts();
	test_state_dtc();
	test_state_buffer();
	test_state_walk();
	test_body_combinations();
	test_long_inputs();
	test_length_sweeps();
	test_large_buffers();
	test_defer_sleep();

	printf("%s\n", failures ? "FAILED" : "OK");
	return failures ? 1 : 0;
}
