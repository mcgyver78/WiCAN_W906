/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>
#include "json.h"

typedef struct
{
	const char *text;
	size_t length;
	size_t pos;
	json_token_t *tokens;
	int max;
	int count;
	int error;
} parser_t;

static bool parse_value(parser_t *p, int depth);

static bool fail(parser_t *p, int error)
{
	if(p->error == 0) p->error = error;
	return false;
}

static void skip_space(parser_t *p)
{
	while(p->pos < p->length)
	{
		char c = p->text[p->pos];

		if(c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
		p->pos++;
	}
}

static bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static int hex_value(char c)
{
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int add_token(parser_t *p, json_type_t type, size_t start)
{
	json_token_t *token;

	if(p->count >= p->max)
	{
		fail(p, JSON_TOO_MANY);
		return -1;
	}
	token = &p->tokens[p->count];
	token->type = type;
	token->start = (uint32_t)start;
	token->length = 0;
	token->size = 0;
	token->skip = 1;
	return p->count++;
}

// p->pos is at the opening quote
static bool parse_string(parser_t *p)
{
	int index = add_token(p, JSON_STRING, p->pos + 1);

	if(index < 0) return false;

	p->pos++;
	while(p->pos < p->length)
	{
		unsigned char c = (unsigned char)p->text[p->pos];

		if(c == '"')
		{
			p->tokens[index].length = (uint32_t)(p->pos - p->tokens[index].start);
			p->pos++;
			return true;
		}
		if(c < 0x20) return fail(p, JSON_SYNTAX);
		if(c == '\\')
		{
			char escape;

			if(p->pos + 1 >= p->length) return fail(p, JSON_SYNTAX);
			escape = p->text[p->pos + 1];
			if(escape == 'u')
			{
				if(p->pos + 5 >= p->length) return fail(p, JSON_SYNTAX);
				for(int i = 2; i <= 5; i++)
				{
					if(hex_value(p->text[p->pos + i]) < 0) return fail(p, JSON_SYNTAX);
				}
				p->pos += 6;
				continue;
			}
			if(strchr("\"\\/bfnrt", escape) == NULL || escape == '\0') return fail(p, JSON_SYNTAX);
			p->pos += 2;
			continue;
		}
		p->pos++;
	}
	return fail(p, JSON_SYNTAX);
}

static bool parse_number(parser_t *p)
{
	size_t start = p->pos;
	int index = add_token(p, JSON_NUMBER, start);

	if(index < 0) return false;

	if(p->pos < p->length && p->text[p->pos] == '-') p->pos++;
	if(p->pos >= p->length || !is_digit(p->text[p->pos])) return fail(p, JSON_SYNTAX);
	if(p->text[p->pos] == '0')
	{
		p->pos++;
	}
	else
	{
		while(p->pos < p->length && is_digit(p->text[p->pos])) p->pos++;
	}
	if(p->pos < p->length && p->text[p->pos] == '.')
	{
		p->pos++;
		if(p->pos >= p->length || !is_digit(p->text[p->pos])) return fail(p, JSON_SYNTAX);
		while(p->pos < p->length && is_digit(p->text[p->pos])) p->pos++;
	}
	if(p->pos < p->length && (p->text[p->pos] == 'e' || p->text[p->pos] == 'E'))
	{
		p->pos++;
		if(p->pos < p->length && (p->text[p->pos] == '+' || p->text[p->pos] == '-')) p->pos++;
		if(p->pos >= p->length || !is_digit(p->text[p->pos])) return fail(p, JSON_SYNTAX);
		while(p->pos < p->length && is_digit(p->text[p->pos])) p->pos++;
	}
	p->tokens[index].length = (uint32_t)(p->pos - start);
	return true;
}

static bool parse_word(parser_t *p, const char *word, json_type_t type)
{
	size_t length = strlen(word);
	int index;

	if(p->length - p->pos < length || memcmp(&p->text[p->pos], word, length) != 0) return fail(p, JSON_SYNTAX);
	index = add_token(p, type, p->pos);
	if(index < 0) return false;
	p->tokens[index].length = (uint32_t)length;
	p->pos += length;
	return true;
}

// p->pos is at the opening bracket or brace
static bool parse_container(parser_t *p, int depth, bool object)
{
	char closing = object ? '}' : ']';
	size_t start = p->pos;
	int index;

	if(depth >= JSON_MAX_DEPTH) return fail(p, JSON_TOO_DEEP);
	index = add_token(p, object ? JSON_OBJECT : JSON_ARRAY, start);
	if(index < 0) return false;

	p->pos++;
	skip_space(p);
	if(p->pos < p->length && p->text[p->pos] == closing)
	{
		p->pos++;
	}
	else
	{
		for(;;)
		{
			if(p->tokens[index].size == UINT16_MAX) return fail(p, JSON_TOO_MANY);

			if(object)
			{
				skip_space(p);
				if(p->pos >= p->length || p->text[p->pos] != '"') return fail(p, JSON_SYNTAX);
				if(!parse_string(p)) return false;
				skip_space(p);
				if(p->pos >= p->length || p->text[p->pos] != ':') return fail(p, JSON_SYNTAX);
				p->pos++;
			}
			if(!parse_value(p, depth + 1)) return false;
			p->tokens[index].size++;

			skip_space(p);
			if(p->pos >= p->length) return fail(p, JSON_SYNTAX);
			if(p->text[p->pos] == closing)
			{
				p->pos++;
				break;
			}
			if(p->text[p->pos] != ',') return fail(p, JSON_SYNTAX);
			p->pos++;
		}
	}

	if(p->count - index > UINT16_MAX) return fail(p, JSON_TOO_MANY);
	p->tokens[index].skip = (uint16_t)(p->count - index);
	p->tokens[index].length = (uint32_t)(p->pos - start);
	return true;
}

static bool parse_value(parser_t *p, int depth)
{
	skip_space(p);
	if(p->pos >= p->length) return fail(p, JSON_SYNTAX);

	switch(p->text[p->pos])
	{
		case '{': return parse_container(p, depth, true);
		case '[': return parse_container(p, depth, false);
		case '"': return parse_string(p);
		case 't': return parse_word(p, "true", JSON_TRUE);
		case 'f': return parse_word(p, "false", JSON_FALSE);
		case 'n': return parse_word(p, "null", JSON_NULL);
		default:  return parse_number(p);
	}
}

int json_parse(const char *text, size_t length, json_token_t *tokens, int max_tokens)
{
	parser_t p = {text, length, 0, tokens, max_tokens, 0, 0};

	// Offsets are stored in 32 bit
	if(length > UINT32_MAX) return JSON_SYNTAX;

	if(!parse_value(&p, 0)) return p.error;
	skip_space(&p);
	if(p.pos != p.length) return JSON_SYNTAX;
	return p.count;
}

int json_member(const char *text, const json_token_t *tokens, int object, const char *key)
{
	int index = object + 1;

	if(object < 0 || tokens[object].type != JSON_OBJECT) return -1;

	for(int i = 0; i < tokens[object].size; i++)
	{
		int value = index + 1;

		if(json_text_is(text, &tokens[index], key)) return value;
		index = value + tokens[value].skip;
	}
	return -1;
}

int json_element(const json_token_t *tokens, int array, int position)
{
	int index = array + 1;

	if(array < 0 || tokens[array].type != JSON_ARRAY || position < 0 || position >= tokens[array].size) return -1;

	for(int i = 0; i < position; i++) index += tokens[index].skip;
	return index;
}

// Next character of a string as UTF-8 bytes. Returns the number of bytes written to out (1 to 4),
// 0 at the end, -1 for \u0000 or half a surrogate pair. *pos counts from the start of the token.
static int next_character(const char *text, const json_token_t *token, uint32_t *pos, char out[4])
{
	const char *s = text + token->start;
	uint32_t code;
	char c;

	if(*pos >= token->length) return 0;

	c = s[(*pos)++];
	if(c != '\\')
	{
		out[0] = c;
		return 1;
	}

	c = s[(*pos)++];
	switch(c)
	{
		case 'b': out[0] = '\b'; return 1;
		case 'f': out[0] = '\f'; return 1;
		case 'n': out[0] = '\n'; return 1;
		case 'r': out[0] = '\r'; return 1;
		case 't': out[0] = '\t'; return 1;
		case 'u': break;
		default:  out[0] = c; return 1;
	}

	code = 0;
	for(int i = 0; i < 4; i++) code = code * 16 + (uint32_t)hex_value(s[(*pos)++]);

	if(code >= 0xDC00 && code <= 0xDFFF) return -1;
	if(code >= 0xD800 && code <= 0xDBFF)
	{
		uint32_t low = 0;

		// The second half has to follow at once
		if(*pos + 6 > token->length || s[*pos] != '\\' || s[*pos + 1] != 'u') return -1;
		for(int i = 2; i < 6; i++) low = low * 16 + (uint32_t)hex_value(s[*pos + i]);
		if(low < 0xDC00 || low > 0xDFFF) return -1;
		*pos += 6;
		code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
	}

	if(code == 0) return -1;
	if(code < 0x80)
	{
		out[0] = (char)code;
		return 1;
	}
	if(code < 0x800)
	{
		out[0] = (char)(0xC0 | (code >> 6));
		out[1] = (char)(0x80 | (code & 0x3F));
		return 2;
	}
	if(code < 0x10000)
	{
		out[0] = (char)(0xE0 | (code >> 12));
		out[1] = (char)(0x80 | ((code >> 6) & 0x3F));
		out[2] = (char)(0x80 | (code & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (code >> 18));
	out[1] = (char)(0x80 | ((code >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((code >> 6) & 0x3F));
	out[3] = (char)(0x80 | (code & 0x3F));
	return 4;
}

bool json_text(const char *text, const json_token_t *token, char *out, size_t size)
{
	uint32_t pos = 0;
	size_t length = 0;
	char bytes[4];
	int count;

	if(size == 0) return false;
	out[0] = '\0';
	if(token->type != JSON_STRING) return false;

	while((count = next_character(text, token, &pos, bytes)) > 0)
	{
		if(length + (size_t)count + 1 > size)
		{
			out[0] = '\0';
			return false;
		}
		memcpy(&out[length], bytes, (size_t)count);
		length += (size_t)count;
	}
	if(count < 0)
	{
		out[0] = '\0';
		return false;
	}
	out[length] = '\0';
	return true;
}

bool json_text_is(const char *text, const json_token_t *token, const char *expected)
{
	uint32_t pos = 0;
	char bytes[4];
	int count;

	if(token->type != JSON_STRING) return false;

	while((count = next_character(text, token, &pos, bytes)) > 0)
	{
		for(int i = 0; i < count; i++)
		{
			if(*expected == '\0' || *expected != bytes[i]) return false;
			expected++;
		}
	}
	return count == 0 && *expected == '\0';
}

bool json_number(const char *text, const json_token_t *token, double *value)
{
	char copy[48];

	// The grammar was checked while parsing; a number longer than this is not a measurement
	if(token->type != JSON_NUMBER || token->length >= sizeof(copy)) return false;

	memcpy(copy, text + token->start, token->length);
	copy[token->length] = '\0';
	*value = strtod(copy, NULL);
	return true;
}

bool json_integer(const char *text, const json_token_t *token, int64_t *value)
{
	const char *s = text + token->start;
	uint64_t limit = (uint64_t)INT64_MAX;
	uint64_t number = 0;
	bool negative;
	uint32_t i = 0;

	if(token->type != JSON_NUMBER) return false;

	negative = s[0] == '-';
	if(negative)
	{
		i = 1;
		limit++;
	}
	for(; i < token->length; i++)
	{
		uint64_t digit;

		// A fraction or an exponent
		if(!is_digit(s[i])) return false;
		digit = (uint64_t)(s[i] - '0');
		if(number > (limit - digit) / 10) return false;
		number = number * 10 + digit;
	}
	*value = negative ? (int64_t)(0 - number) : (int64_t)number;
	return true;
}
