/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "ota_check.h"

#define IMAGE_MAGIC         0xE9
#define CHIP_ID_OFFSET      12
#define DESCRIPTION_OFFSET  32
#define DESCRIPTION_MAGIC   0xABCD5432u
#define VERSION_OFFSET      (DESCRIPTION_OFFSET + 16)
#define PROJECT_OFFSET      (DESCRIPTION_OFFSET + 48)
#define TEXT_SIZE           32

// Copies a zero terminated text, as much of it as fits
static void copy_cut(const char *text, char *out, size_t size)
{
	size_t length = strlen(text);

	if(length > size - 1)
	{
		length = size - 1;
		// text[length] is the first byte left out. If it continues a UTF-8 character, the bytes of that
		// character before it go as well.
		while(length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80) length--;
	}
	memcpy(out, text, length);
	out[length] = '\0';
}

ota_check_t ota_check(const uint8_t *data, size_t length, uint32_t file_size, uint32_t slot_size,
                      char *version, size_t version_size)
{
	const char *image_version, *project;
	ota_check_t result = OTA_CHECK_OK;
	uint32_t magic;

	if(version_size == 0) version = NULL;
	if(version != NULL) version[0] = '\0';

	if(length < OTA_CHECK_BYTES) return OTA_CHECK_TOO_SHORT;
	if(data[0] != IMAGE_MAGIC) return OTA_CHECK_NO_IMAGE;
	if((data[CHIP_ID_OFFSET] | (data[CHIP_ID_OFFSET + 1] << 8)) != OTA_CHIP_ESP32S3) return OTA_CHECK_WRONG_CHIP;

	magic = (uint32_t)data[DESCRIPTION_OFFSET] | ((uint32_t)data[DESCRIPTION_OFFSET + 1] << 8) |
	        ((uint32_t)data[DESCRIPTION_OFFSET + 2] << 16) | ((uint32_t)data[DESCRIPTION_OFFSET + 3] << 24);
	if(magic != DESCRIPTION_MAGIC) return OTA_CHECK_NO_DESCRIPTION;

	// Without a terminating zero the texts would be read into whatever follows them
	image_version = (const char *)&data[VERSION_OFFSET];
	project = (const char *)&data[PROJECT_OFFSET];
	if(memchr(image_version, '\0', TEXT_SIZE) == NULL || memchr(project, '\0', TEXT_SIZE) == NULL)
	{
		return OTA_CHECK_NO_DESCRIPTION;
	}

	if(strcmp(project, OTA_PROJECT_NAME) != 0)
	{
		result = OTA_CHECK_WRONG_PROJECT;
	}
	else if(file_size == 0 || file_size > slot_size)
	{
		return OTA_CHECK_TOO_LARGE;
	}

	if(version != NULL) copy_cut(image_version, version, version_size);
	return result;
}
