/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "catalog.h"

#define FNV_OFFSET  2166136261u
#define FNV_PRIME   16777619u

typedef struct
{
	char *out;
	size_t size;
	size_t length;      // of the whole text, also when it does not fit
} writer_t;

void catalog_init(catalog_t *catalog)
{
	memset(catalog, 0, sizeof(*catalog));
	strcpy(catalog->entries->name, CATALOG_BATTERY);
	strcpy(catalog->entries->unit, "V");
	catalog->count = 1;
}

int catalog_find(const catalog_t *catalog, const char *name)
{
	const catalog_entry_t *entry = catalog->entries;

	for(int i = 0; i < catalog->count; i++, entry++)
	{
		if(strcmp(entry->name, name) == 0) return i;
	}
	return -1;
}

// The entry with that name, appended if the catalogue does not know it. NULL if there is no room for it.
static catalog_entry_t *entry_for(catalog_t *catalog, const char *name)
{
	int index = catalog_find(catalog, name);
	catalog_entry_t *entry;

	if(index >= 0) return catalog->entries + index;
	if(catalog->count >= CATALOG_MAX)
	{
		catalog->dropped++;
		return NULL;
	}
	entry = catalog->entries + catalog->count++;
	memset(entry, 0, sizeof(*entry));
	strcpy(entry->name, name);
	return entry;
}

// Text of a string token with the escapes resolved. What does not fit is cut: out gets the longest
// beginning that ends between two characters and that json_text() returns.
static void text_cut(const char *json, const json_token_t *token, char *out, size_t size)
{
	const char *s = json + token->start;
	json_token_t part = *token;
	uint32_t fits = 0;
	uint32_t pos = 0;

	while(pos < token->length)
	{
		// One step is one byte or one escape as a whole
		if(s[pos] != '\\') pos += 1;
		else if(s[pos + 1] == 'u') pos += 6;
		else pos += 2;

		// Not between the bytes of one character
		if(pos < token->length && ((unsigned char)s[pos] & 0xC0) == 0x80) continue;

		// A beginning that ends between the halves of a surrogate pair is refused, the next one may be fine again
		part.length = pos;
		if(json_text(json, &part, out, size)) fits = pos;
	}
	part.length = fits;
	json_text(json, &part, out, size);
}

// Unit or class of an entry of the text, empty if it has none
static void member_text(const char *json, const json_token_t *tokens, int object, const char *key, char *out, size_t size)
{
	int index = json_member(json, tokens, object, key);

	if(index < 0 || tokens[index].type != JSON_STRING) out[0] = '\0';
	else text_cut(json, &tokens[index], out, size);
}

// true if the text has a member with that name whose value is an object
static bool is_named(const char *json, const json_token_t *tokens, const char *name)
{
	int key = 1;

	for(int i = 0; i < tokens[0].size; i++)
	{
		int details = key + 1;

		if(tokens[details].type == JSON_OBJECT && json_text_is(json, &tokens[key], name)) return true;
		key = details + tokens[details].skip;
	}
	return false;
}

// Takes over the members of the text that are objects: known names are updated, new ones appended.
// stored: the text is one of catalog_to_json, which says itself what belongs to the profile.
static void take_entries(catalog_t *catalog, const char *json, const json_token_t *tokens, bool stored)
{
	int key = 1;

	for(int i = 0; i < tokens[0].size; i++)
	{
		const json_token_t *name_token = &tokens[key];
		int details = key + 1;
		char name[VALUE_NAME_SIZE];
		catalog_entry_t *entry;

		key = details + tokens[details].skip;

		if(tokens[details].type != JSON_OBJECT) continue;
		if(!json_text(json, name_token, name, sizeof(name)))
		{
			catalog->dropped++;
			continue;
		}
		entry = entry_for(catalog, name);
		if(entry == NULL) continue;

		member_text(json, tokens, details, "unit", entry->unit, sizeof(entry->unit));
		member_text(json, tokens, details, "class", entry->value_class, sizeof(entry->value_class));
		entry->in_profile = !stored || tokens[json_member(json, tokens, details, "profile")].type == JSON_TRUE;
	}
}

bool catalog_apply_config(catalog_t *catalog, const char *json, size_t length, json_token_t *work, int work_count)
{
	int count = json_parse(json, length, work, work_count);
	catalog_entry_t *entry = catalog->entries;
	catalog_entry_t *kept = catalog->entries;

	if(count < 0 || work[0].type != JSON_OBJECT) return false;

	// What the new profile does not name goes before the new names come, so that a different profile finds room
	for(int i = 0; i < catalog->count; i++, entry++)
	{
		entry->in_profile = is_named(json, work, entry->name);
		if(entry->in_profile || entry->delivered || strcmp(entry->name, CATALOG_BATTERY) == 0) *kept++ = *entry;
	}
	catalog->count = (int)(kept - catalog->entries);

	take_entries(catalog, json, work, false);
	return true;
}

void catalog_note_values(catalog_t *catalog, const values_t *values)
{
	const value_t *value = values->items;

	for(int i = 0; i < values->count; i++, value++)
	{
		catalog_entry_t *entry = entry_for(catalog, value->name);

		if(entry != NULL) entry->delivered = true;
	}
}

static void put_char(writer_t *writer, char c)
{
	// What does not fit is only counted
	if(writer->length < writer->size) writer->out[writer->length] = c;
	writer->length++;
}

static void put(writer_t *writer, const char *text)
{
	for(; *text != '\0'; text++) put_char(writer, *text);
}

static void put_string(writer_t *writer, const char *text)
{
	static const char hex[] = "0123456789abcdef";

	put_char(writer, '"');
	for(; *text != '\0'; text++)
	{
		unsigned char c = (unsigned char)*text;

		if(c < 0x20)
		{
			put(writer, "\\u00");
			put_char(writer, hex[c >> 4]);
			put_char(writer, hex[c & 0x0F]);
			continue;
		}
		if(c == '"' || c == '\\') put_char(writer, '\\');
		put_char(writer, *text);
	}
	put_char(writer, '"');
}

int catalog_to_json(const catalog_t *catalog, char *out, size_t size)
{
	const catalog_entry_t *entry = catalog->entries;
	writer_t writer = {out, size, 0};

	put_char(&writer, '{');
	for(int i = 0; i < catalog->count; i++, entry++)
	{
		if(i > 0) put_char(&writer, ',');
		put_string(&writer, entry->name);
		put(&writer, ":{\"unit\":");
		put_string(&writer, entry->unit);
		put(&writer, ",\"class\":");
		put_string(&writer, entry->value_class);
		put(&writer, ",\"profile\":");
		put(&writer, entry->in_profile ? "true" : "false");
		put(&writer, ",\"delivered\":");
		put(&writer, entry->delivered ? "true" : "false");
		put_char(&writer, '}');
	}
	put_char(&writer, '}');

	if(size == 0) return -1;
	if(writer.length >= size)
	{
		out[0] = '\0';
		return -1;
	}
	out[writer.length] = '\0';
	return (int)writer.length;
}

bool catalog_from_json(catalog_t *catalog, const char *json, size_t length, json_token_t *work, int work_count)
{
	int count = json_parse(json, length, work, work_count);
	int key = 1;

	if(count < 0 || work[0].type != JSON_OBJECT) return false;

	// Nothing may change before the whole text is known to be a stored catalogue
	for(int i = 0; i < work[0].size; i++)
	{
		int details = key + 1;
		int unit = json_member(json, work, details, "unit");
		int value_class = json_member(json, work, details, "class");
		int profile = json_member(json, work, details, "profile");

		if(unit < 0 || work[unit].type != JSON_STRING) return false;
		if(value_class < 0 || work[value_class].type != JSON_STRING) return false;
		if(profile < 0 || (work[profile].type != JSON_TRUE && work[profile].type != JSON_FALSE)) return false;
		key = details + work[details].skip;
	}

	catalog_init(catalog);
	take_entries(catalog, json, work, true);
	return true;
}

static uint32_t hash_byte(uint32_t hash, unsigned char byte)
{
	return (hash ^ byte) * FNV_PRIME;
}

// With the terminating zero: "ab" and "c" must not give the same as "a" and "bc"
static uint32_t hash_text(uint32_t hash, const char *text)
{
	do
	{
		hash = hash_byte(hash, (unsigned char)*text);
	}
	while(*text++ != '\0');
	return hash;
}

uint32_t catalog_checksum(const catalog_t *catalog)
{
	const catalog_entry_t *entry = catalog->entries;
	uint32_t hash = FNV_OFFSET;

	for(int i = 0; i < catalog->count; i++, entry++)
	{
		hash = hash_text(hash, entry->name);
		hash = hash_text(hash, entry->unit);
		hash = hash_text(hash, entry->value_class);
		hash = hash_byte(hash, entry->in_profile ? 1 : 0);
	}
	return hash;
}
