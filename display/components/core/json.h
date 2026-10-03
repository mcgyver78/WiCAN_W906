/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __JSON_H__
#define __JSON_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Small JSON reader for the answers of the WiCAN and for the layout of the views.
 * Strict (RFC 8259) and without allocation: the caller provides the token array, the tokens refer to
 * the text, which has to stay. The same code runs on the display and in the host tests.
 */

typedef enum
{
	JSON_OBJECT,
	JSON_ARRAY,
	JSON_STRING,
	JSON_NUMBER,
	JSON_TRUE,
	JSON_FALSE,
	JSON_NULL,
} json_type_t;

typedef struct
{
	json_type_t type;
	uint32_t start;     // first byte of the value; of a string the first byte behind the quote
	uint32_t length;    // bytes of the value; of a string without the quotes
	uint16_t size;      // members of an object or elements of an array, else 0
	uint16_t skip;      // tokens of this value including itself: index + skip is what follows it
} json_token_t;

#define JSON_SYNTAX         (-1)    // not valid JSON, or something other than whitespace behind it
#define JSON_TOO_MANY       (-2)    // more values than max_tokens, or more than 65535 in one container
#define JSON_TOO_DEEP       (-3)    // containers nested deeper than JSON_MAX_DEPTH

#define JSON_MAX_DEPTH      8

// Tokens in the order of the text: an object is followed by key, value, key, value, an array by its
// elements. Returns the number of tokens (the document is tokens[0]) or one of the errors above.
int json_parse(const char *text, size_t length, json_token_t *tokens, int max_tokens);

// Index of the value of member `key` of the object at index `object`, -1 if it has none or is no
// object. The first member with that key counts.
int json_member(const char *text, const json_token_t *tokens, int object, const char *key);

// Index of element `position` (from 0) of the array at index `array`, -1 if there is none.
int json_element(const json_token_t *tokens, int array, int position);

// Text of a string as UTF-8 with the escapes resolved, zero terminated. false if the token is no
// string, the text does not fit, or it contains \u0000 or half a surrogate pair.
bool json_text(const char *text, const json_token_t *token, char *out, size_t size);

// true if the token is a string with exactly this text (escapes resolved)
bool json_text_is(const char *text, const json_token_t *token, const char *expected);

// false if the token is no number
bool json_number(const char *text, const json_token_t *token, double *value);

// A number without fraction and exponent that fits into 64 bit, false for everything else
bool json_integer(const char *text, const json_token_t *token, int64_t *value);

#endif
