/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __OTA_CHECK_H__
#define __OTA_CHECK_H__

#include <stdint.h>
#include <stddef.h>

/*
 * Looks at the first bytes of a firmware file uploaded in the browser BEFORE anything is erased. The
 * firmware of the WiCAN itself, a file for another chip or any other file must never reach the app slot.
 * The complete check sum of the image is verified by the bootloader support of ESP-IDF at the end of the
 * upload; this is the early check.
 *
 * Layout of an ESP-IDF application image (little endian):
 *   0    magic 0xE9
 *   12   chip id, 16 bit: 0x0009 = ESP32-S3
 *   24   header of the first segment (8 bytes)
 *   32   application description: magic 0xABCD5432 (32 bit), at +16 the version (32 bytes, zero
 *        terminated), at +48 the project name (32 bytes, zero terminated)
 */

#define OTA_CHECK_BYTES     112     // bytes of the file needed for the check: 32 + 48 + 32
#define OTA_PROJECT_NAME    "wican-display"
#define OTA_CHIP_ESP32S3    0x0009

typedef enum
{
	OTA_CHECK_OK,
	OTA_CHECK_TOO_SHORT,        // fewer than OTA_CHECK_BYTES bytes given
	OTA_CHECK_NO_IMAGE,         // not an ESP-IDF image (magic)
	OTA_CHECK_WRONG_CHIP,
	OTA_CHECK_NO_DESCRIPTION,   // no application description where it has to be
	OTA_CHECK_WRONG_PROJECT,    // an image, but not of this display (for example the WiCAN firmware)
	OTA_CHECK_TOO_LARGE,        // the file is larger than the app slot, or empty
} ota_check_t;

// data: the first `length` bytes of the file; file_size: the size announced for the whole upload;
// slot_size: size of the app slot it would be written to. On OTA_CHECK_OK, and on OTA_CHECK_WRONG_PROJECT
// as well, the version text of the image is copied to `version` (cut if it does not fit; may be NULL).
// On every other result `version` becomes an empty text.
// The checks are made in the order of the enum, the first that fails is returned. A project name or
// version that is not zero terminated within its 32 bytes counts as OTA_CHECK_NO_DESCRIPTION.
ota_check_t ota_check(const uint8_t *data, size_t length, uint32_t file_size, uint32_t slot_size,
                      char *version, size_t version_size);

#endif
