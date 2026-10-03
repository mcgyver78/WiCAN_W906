/*
 * Host test for display/components/core/json.c. Run "make test_json && ./test_json" in display/test.
 * redproof.py removes or weakens every rule once (mutations/json.py) and expects this test to fail.
 */
#include <stdlib.h>
#include "test.h"
#include "json.h"

#define MAX_TOKENS 64

static json_token_t tokens[MAX_TOKENS];

static int parse(const char *text)
{
	return json_parse(text, strlen(text), tokens, MAX_TOKENS);
}

static bool text_of(const char *json, int index, const char *expected)
{
	char out[64];

	return json_text(json, &tokens[index], out, sizeof(out)) && strcmp(out, expected) == 0;
}

static void test_values(void)
{
	const char *json;
	double number = 0;
	int64_t integer = 0;

	check(parse("true") == 1 && tokens[0].type == JSON_TRUE, "true");
	check(parse("false") == 1 && tokens[0].type == JSON_FALSE, "false");
	check(parse("null") == 1 && tokens[0].type == JSON_NULL, "null");
	check(parse(" \t\r\n 12 \n") == 1 && tokens[0].type == JSON_NUMBER && tokens[0].start == 5 && tokens[0].length == 2,
	      "whitespace around a value is skipped, the token covers the value only");

	json = "\"abc\"";
	check(parse(json) == 1 && tokens[0].type == JSON_STRING && tokens[0].start == 1 && tokens[0].length == 3 &&
	      tokens[0].skip == 1 && tokens[0].size == 0, "a string token covers the text between the quotes");
	check(parse("\"\"") == 1 && tokens[0].length == 0 && text_of("\"\"", 0, ""), "empty string");

	json = "-12.5e1";
	check(parse(json) == 1 && json_number(json, &tokens[0], &number) && number == -125.0, "number with fraction and exponent");
	json = "0";
	check(parse(json) == 1 && json_number(json, &tokens[0], &number) && number == 0.0 &&
	      json_integer(json, &tokens[0], &integer) && integer == 0, "zero");
	json = "1E+2";
	check(parse(json) == 1 && json_number(json, &tokens[0], &number) && number == 100.0, "exponent with capital E and plus");
	json = "2e-1";
	check(parse(json) == 1 && json_number(json, &tokens[0], &number) && number > 0.19 && number < 0.21, "negative exponent");
}

static void test_syntax(void)
{
	static const char *const wrong[] = {
		"", " ", "tru", "truee", "nul", "fals", "True", "-", "+1", "01", "1.", ".5", "1e", "1e+", "--1", "1 2",
		"\"abc", "\"a\nb\"", "\"\\x\"", "\"\\u12\"", "\"\\u12G4\"", "\"\\u123G\"", "\"\\uG234\"", "\"\\", "\"a\"b",
		"trxe", "txue", "faxse", "nuxl",
		"{", "}", "{\"a\"}", "{\"a\":}", "{\"a\":1,}", "{a:1}", "{\"a\" 1}", "{\"a\":1 \"b\":2}", "{1:2}", "{,}",
		"[", "]", "[1,]", "[,1]", "[1 2]", "[1,,2]", "[1}", "{\"a\":1]", "{} x", "[] 1", "{}{}",
	};
	char nul_inside[] = {'"', 'a', '\0', 'b', '"'};
	char nul_escaped[] = {'"', '\\', '\0', '"'};
	size_t i;

	for(i = 0; i < sizeof(wrong) / sizeof(wrong[0]); i++)
	{
		char what[80];

		snprintf(what, sizeof(what), "not JSON: %.50s", wrong[i]);
		check(parse(wrong[i]) == JSON_SYNTAX, what);
	}
	check(json_parse(nul_inside, sizeof(nul_inside), tokens, MAX_TOKENS) == JSON_SYNTAX, "not JSON: zero byte in a string");
	check(json_parse(nul_escaped, sizeof(nul_escaped), tokens, MAX_TOKENS) == JSON_SYNTAX, "not JSON: backslash before a zero byte");
	check(json_parse("12", 1, tokens, MAX_TOKENS) == 1 && tokens[0].length == 1, "only `length` bytes of the text are read");
	check(json_parse("[1]x", 3, tokens, MAX_TOKENS) == 2, "text behind `length` is not looked at");
}

static void test_containers(void)
{
	const char *json = "{\"a\":1,\"b\":[true,{\"c\":null},\"x\"],\"d\":{}}";
	int count = parse(json);
	int b, d;

	check(count == 12, "tokens: object, keys and values in the order of the text");
	check(tokens[0].type == JSON_OBJECT && tokens[0].size == 3 && tokens[0].skip == 12 &&
	      tokens[0].start == 0 && tokens[0].length == strlen(json), "object: three members, covers the whole text");
	check(json_member(json, tokens, 0, "a") == 2 && tokens[2].type == JSON_NUMBER, "member a");
	b = json_member(json, tokens, 0, "b");
	check(b == 4 && tokens[b].type == JSON_ARRAY && tokens[b].size == 3 && tokens[b].skip == 6, "member b is an array of three");
	check(json_element(tokens, b, 0) == 5 && tokens[5].type == JSON_TRUE, "element 0");
	check(json_element(tokens, b, 1) == 6 && tokens[6].type == JSON_OBJECT && tokens[6].size == 1 && tokens[6].skip == 3, "element 1 is an object");
	check(json_member(json, tokens, 6, "c") == 8 && tokens[8].type == JSON_NULL, "member of the nested object");
	check(json_element(tokens, b, 2) == 9 && text_of(json, 9, "x"), "element 2 follows the nested object");
	check(json_element(tokens, b, 3) == -1 && json_element(tokens, b, -1) == -1, "no element behind the last or before the first");
	d = json_member(json, tokens, 0, "d");
	check(d == 11 && tokens[d].type == JSON_OBJECT && tokens[d].size == 0 && tokens[d].skip == 1 && tokens[d].length == 2, "empty object");
	check(json_member(json, tokens, 0, "c") == -1, "a member of a nested object is not a member of the outer one");
	check(json_member(json, tokens, 0, "x") == -1 && json_member(json, tokens, 0, "") == -1, "unknown member");
	check(json_member(json, tokens, b, "a") == -1 && json_member(json, tokens, 2, "a") == -1 && json_member(json, tokens, -1, "a") == -1,
	      "only an object has members");
	check(json_element(tokens, 0, 0) == -1 && json_element(tokens, 2, 0) == -1 && json_element(tokens, -1, 0) == -1,
	      "only an array has elements");

	json = "[\"a\",5]";
	check(parse(json) == 3 && json_member(json, tokens, 0, "a") == -1, "two array elements are not a key and its value");

	json = "{\"k\":1,\"k\":2}";
	check(parse(json) == 5 && json_member(json, tokens, 0, "k") == 2, "of two members with the same key the first counts");
	json = "{\"ab\":1,\"a\":2,\"abc\":3}";
	check(parse(json) == 7 && json_member(json, tokens, 0, "a") == 4 && json_member(json, tokens, 0, "abc") == 6,
	      "a key is compared completely, not by its beginning");
	json = "{\"\\u0061\":7}";
	check(parse(json) == 3 && json_member(json, tokens, 0, "a") == 2, "an escaped key is found by its text");
	check(parse("[ ]") == 1 && tokens[0].size == 0 && parse(" { } ") == 1 && tokens[0].type == JSON_OBJECT, "empty containers with spaces");
	check(parse("[[],[[]],{}]") == 5 && tokens[0].size == 3 && tokens[2].skip == 2 && json_element(tokens, 0, 2) == 4, "nested empty containers");
}

static void test_limits(void)
{
	char deep[64];
	json_token_t few[5];
	int i;

	// Two tokens behind the room given to the reader show whether it writes there
	memset(few, 0x5A, sizeof(few));
	check(json_parse("[1,2]", 5, few, 3) == 3 && few[3].start == 0x5A5A5A5A, "exactly as many tokens as there is room for");
	memset(few, 0x5A, sizeof(few));
	check(json_parse("[1,2,3]", 7, few, 3) == JSON_TOO_MANY && few[3].start == 0x5A5A5A5A && few[4].start == 0x5A5A5A5A,
	      "one token more than there is room for: an error, nothing written behind the room");
	memset(few, 0x5A, sizeof(few));
	check(json_parse("1", 1, few, 0) == JSON_TOO_MANY && few[0].start == 0x5A5A5A5A, "no room at all: nothing written");

	memset(deep, 0, sizeof(deep));
	for(i = 0; i < JSON_MAX_DEPTH; i++) deep[i] = '[';
	for(i = 0; i < JSON_MAX_DEPTH; i++) deep[JSON_MAX_DEPTH + i] = ']';
	check(parse(deep) == JSON_MAX_DEPTH, "containers nested to the limit");
	memset(deep, 0, sizeof(deep));
	for(i = 0; i < JSON_MAX_DEPTH + 1; i++) deep[i] = '[';
	for(i = 0; i < JSON_MAX_DEPTH + 1; i++) deep[JSON_MAX_DEPTH + 1 + i] = ']';
	check(parse(deep) == JSON_TOO_DEEP, "containers nested one deeper than the limit");
	check(JSON_MAX_DEPTH == 8, "the limit is 8 levels");
	check(parse("[[[[[[[1]]]]]]]") == 8 && parse("{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":{\"g\":{\"h\":1}}}}}}}}") == 17,
	      "a scalar inside the deepest container does not count as a level");
}

static void test_text(void)
{
	const char *json;
	char out[16];

	json = "\"a\\\"b\\\\c\\/d\\be\\ff\\ng\\rh\\ti\"";
	check(parse(json) == 1 && text_of(json, 0, "a\"b\\c/d\be\ff\ng\rh\ti"), "every simple escape");
	json = "\"\\u00e4\\u00DF\\u20AC\"";
	check(parse(json) == 1 && text_of(json, 0, "\303\244\303\237\342\202\254"), "\\u escapes become UTF-8 of 2 and 3 bytes");
	json = "\"\\uD83D\\uDE00\"";
	check(parse(json) == 1 && text_of(json, 0, "\360\237\230\200"), "a surrogate pair becomes UTF-8 of 4 bytes");
	json = "\"\\u007F\\u0080\\u07FF\\u0800\\uFFFF\"";
	check(parse(json) == 1 && text_of(json, 0, "\177\302\200\337\277\340\240\200\357\277\277"), "boundaries of the UTF-8 lengths");
	json = "\"Rückhaltesystem\"";
	check(parse(json) == 1 && text_of(json, 0, "Rückhaltesystem"), "UTF-8 in the text is passed on");

	json = "\"\\u0000\"";
	check(parse(json) == 1 && !json_text(json, &tokens[0], out, sizeof(out)) && out[0] == '\0', "\\u0000 is refused");
	json = "\"\\uD83D\"";
	check(parse(json) == 1 && !json_text(json, &tokens[0], out, sizeof(out)), "half a surrogate pair is refused");
	json = "\"\\uD83Dx\"";
	check(parse(json) == 1 && !json_text(json, &tokens[0], out, sizeof(out)), "a high surrogate followed by text is refused");
	json = "\"\\uD83D\\u0041\"";
	check(parse(json) == 1 && !json_text(json, &tokens[0], out, sizeof(out)), "a high surrogate followed by another character is refused");
	json = "\"\\uDE00\"";
	check(parse(json) == 1 && !json_text(json, &tokens[0], out, sizeof(out)), "a low surrogate alone is refused");

	json = "\"abcde\"";
	parse(json);
	memset(out, 'x', sizeof(out));
	check(json_text(json, &tokens[0], out, 6) && strcmp(out, "abcde") == 0 && out[6] == 'x', "text fits exactly with its terminating zero");
	memset(out, 'x', sizeof(out));
	check(!json_text(json, &tokens[0], out, 5) && out[0] == '\0' && out[5] == 'x', "text one byte too long: false and an empty string");
	out[0] = 'x';
	check(!json_text(json, &tokens[0], out, 0) && out[0] == 'x', "no room at all: nothing is written");
	json = "\"ab\\u00e4\"";
	parse(json);
	memset(out, 'x', sizeof(out));
	check(!json_text(json, &tokens[0], out, 4) && out[0] == '\0' && out[4] == 'x', "a character of two bytes is not cut in half");
	check(json_text(json, &tokens[0], out, 5) && strcmp(out, "ab\303\244") == 0, "it fits with one byte more");

	json = "12";
	parse(json);
	check(!json_text(json, &tokens[0], out, sizeof(out)) && out[0] == '\0', "a number is no text");

	json = "[\"abc\",\"ab\",\"abcd\",\"\",\"a\\u0062c\",12]";
	parse(json);
	check(json_text_is(json, &tokens[1], "abc"), "text is the expected one");
	check(!json_text_is(json, &tokens[2], "abc") && !json_text_is(json, &tokens[3], "abc"), "shorter and longer text differ");
	check(json_text_is(json, &tokens[4], "") && !json_text_is(json, &tokens[4], "a") && !json_text_is(json, &tokens[1], ""), "empty text");
	check(json_text_is(json, &tokens[5], "abc"), "escaped text is compared resolved");
	check(!json_text_is(json, &tokens[6], "12"), "a number is not a text with the same characters");
}

static void test_numbers(void)
{
	const char *json = "[42,-7,3.0,1e3,9223372036854775807,-9223372036854775808,9223372036854775808,-9223372036854775809,\"5\",true,"
	                   "123456789012345678901234567890123456789012345678,-0]";
	double number = 0;
	int64_t integer = 0;

	check(parse(json) == 13, "numbers parse");
	check(json_integer(json, &tokens[1], &integer) && integer == 42, "integer");
	check(json_integer(json, &tokens[2], &integer) && integer == -7, "negative integer");
	check(!json_integer(json, &tokens[3], &integer), "a fraction is no integer");
	check(!json_integer(json, &tokens[4], &integer), "an exponent is no integer");
	check(json_integer(json, &tokens[5], &integer) && integer == INT64_MAX, "largest integer");
	check(json_integer(json, &tokens[6], &integer) && integer == INT64_MIN, "smallest integer");
	check(!json_integer(json, &tokens[7], &integer), "one above the largest");
	check(!json_integer(json, &tokens[8], &integer), "one below the smallest");
	check(!json_integer(json, &tokens[9], &integer) && !json_number(json, &tokens[9], &number), "a string is no number");
	check(!json_integer(json, &tokens[10], &integer) && !json_number(json, &tokens[10], &number), "true is no number");
	check(json_number(json, &tokens[3], &number) && number == 3.0, "number with a fraction");
	check(json_number(json, &tokens[7], &number) && number > 9.2e18, "a number too large for an integer is still a number");
	check(!json_number(json, &tokens[11], &number), "a number of 48 digits is refused");
	check(json_integer(json, &tokens[12], &integer) && integer == 0, "minus zero is zero");

	json = "12345678901234567890123456789012345678901234567";
	check(parse(json) == 1 && json_number(json, &tokens[0], &number) && number > 1e46, "a number of 47 characters is accepted");
}

static void test_real_answers(void)
{
	static char text[4096];
	static json_token_t many[512];
	double number = 0;
	int count, index;

	// Answers of the adapter as the contract describes them
	check(read_fixture("../../tools/w906/fixtures/dtc_state_running_umlaut.json", text, sizeof(text)), "fixture of the scan state");
	count = json_parse(text, strlen(text), many, 512);
	check(count == 25 && many[0].type == JSON_OBJECT && many[0].size == 12, "scan state: 12 members");
	index = json_member(text, many, 0, "name");
	check(index > 0 && json_text_is(text, &many[index], "N2/14 Rückhaltesystem (SRS)"), "scan state: name with umlaut");
	index = json_member(text, many, 0, "ecu");
	check(index > 0 && json_number(text, &many[index], &number) && number == 17, "scan state: step");
	index = json_member(text, many, 0, "supported");
	check(index > 0 && many[index].type == JSON_TRUE, "scan state: supported");
}

int main(void)
{
	test_values();
	test_syntax();
	test_containers();
	test_limits();
	test_text();
	test_numbers();
	test_real_answers();
	return test_end();
}
