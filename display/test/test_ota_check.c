/*
 * Host test for display/components/core/ota_check.c. Run "make test_ota_check && ./test_ota_check" in
 * display/test. redproof.py removes or weakens every rule once (mutations/ota_check.py) and expects this
 * test to fail.
 */
#include <stdint.h>
#include <stdlib.h>
#include "test.h"
#include "ota_check.h"

#define GUARD       0x5A
#define SLOT        0x400000u       // an app slot of the display, see display/partitions.csv
#define FILE_SIZE   1500000u
#define VERSION     "1.2.0-3-g4f5a6b7"

// The first 112 bytes of a firmware of the display, byte for byte as the layout in ota_check.h says
static const uint8_t display_image[OTA_CHECK_BYTES] = {
	// 0: image header
	0xE9,                                           // magic
	0x06,                                           // number of segments
	0x02, 0x4F,                                     // flash mode, size and speed
	0x9C, 0x12, 0x37, 0x40,                         // entry address
	0xEE, 0x11, 0x22, 0x33,                         // pin settings
	0x09, 0x00,                                     // 12: chip id 0x0009
	0x44,                                           // deprecated
	0x55, 0x66, 0x63, 0x77,                         // chip revisions
	0x88, 0x99, 0xAA, 0xBB,                         // reserved
	0x01,                                           // hash appended
	// 24: header of the first segment
	0x20, 0x00, 0x10, 0x3C, 0x54, 0x6D, 0x03, 0xCC,
	// 32: application description, magic 0xABCD5432
	0x32, 0x54, 0xCD, 0xAB,
	0xDD, 0xEE, 0xFF, 0x12,                         // secure version
	0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x21, // reserved
	// 48: version, 32 bytes
	'1', '.', '2', '.', '0', '-', '3', '-', 'g', '4', 'f', '5', 'a', '6', 'b', '7',
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	// 80: project name, 32 bytes
	'w', 'i', 'c', 'a', 'n', '-', 'd', 'i', 's', 'p', 'l', 'a', 'y', 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

#define AT_MAGIC        0
#define AT_CHIP         12
#define AT_DESCRIPTION  32
#define AT_VERSION      48
#define AT_PROJECT      80
#define TEXT_BYTES      32

// The image under test in a block of exactly its size: the address sanitizer stops every read behind it
static uint8_t *image;

static void fresh_image(void)
{
	memcpy(image, display_image, OTA_CHECK_BYTES);
}

// A text field of 32 bytes: the text, the rest zero
static void put_text(int at, const char *text)
{
	memset(&image[at], 0, TEXT_BYTES);
	memcpy(&image[at], text, strlen(text));
}

static void fill_text(int at, char byte)
{
	memset(&image[at], byte, TEXT_BYTES);
}

static ota_check_t result_of(void)
{
	return ota_check(image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, NULL, 0);
}

// Result and version text of a call that starts with a version buffer full of old text
static char version[64];
// Whether every such call gave the same result without a buffer for the version
static bool same_without_buffer = true;

static ota_check_t checked(size_t length, uint32_t file_size, uint32_t slot_size)
{
	ota_check_t result;

	memset(version, GUARD, sizeof(version));
	result = ota_check(image, length, file_size, slot_size, version, sizeof(version));
	if(ota_check(image, length, file_size, slot_size, NULL, 0) != result || ota_check(image, length, file_size, slot_size, version, 0) != result)
	{
		same_without_buffer = false;
	}
	return result;
}

static void test_ok(void)
{
	uint8_t longer[300];

	fresh_image();
	check(checked(OTA_CHECK_BYTES, FILE_SIZE, SLOT) == OTA_CHECK_OK, "the first 112 bytes of a firmware of the display are accepted");
	check(strcmp(version, VERSION) == 0, "the version text of an accepted image is copied");
	check(ota_check(image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, NULL, 0) == OTA_CHECK_OK, "no buffer for the version: the image is accepted all the same");
	check(OTA_CHECK_BYTES == 112, "112 bytes are needed for the check");
	check(strcmp(OTA_PROJECT_NAME, "wican-display") == 0 && OTA_CHIP_ESP32S3 == 9, "project name and chip of the display");

	memset(longer, 0xFF, sizeof(longer));
	memcpy(longer, display_image, OTA_CHECK_BYTES);
	memset(version, GUARD, sizeof(version));
	check(ota_check(longer, sizeof(longer), FILE_SIZE, SLOT, version, sizeof(version)) == OTA_CHECK_OK && strcmp(version, VERSION) == 0,
	      "more than 112 bytes given: the rest is not looked at");
}

static void test_too_short(void)
{
	uint8_t longer[300];
	bool all_short = true, all_ok = true;
	size_t length;

	fresh_image();
	check(checked(OTA_CHECK_BYTES - 1, FILE_SIZE, SLOT) == OTA_CHECK_TOO_SHORT, "111 bytes are too short");
	check(version[0] == '\0', "too short: the version is an empty text");
	check(checked(0, FILE_SIZE, SLOT) == OTA_CHECK_TOO_SHORT && version[0] == '\0', "no bytes are too short");
	check(checked(1, FILE_SIZE, SLOT) == OTA_CHECK_TOO_SHORT && checked(24, FILE_SIZE, SLOT) == OTA_CHECK_TOO_SHORT &&
	      checked(32, FILE_SIZE, SLOT) == OTA_CHECK_TOO_SHORT && checked(80, FILE_SIZE, SLOT) == OTA_CHECK_TOO_SHORT, "a part of the header is too short");

	memset(longer, 0xFF, sizeof(longer));
	memcpy(longer, display_image, OTA_CHECK_BYTES);
	for(length = 0; length <= sizeof(longer); length++)
	{
		ota_check_t result = ota_check(longer, length, FILE_SIZE, SLOT, NULL, 0);

		if(length < 112 && result != OTA_CHECK_TOO_SHORT) all_short = false;
		if(length >= 112 && result != OTA_CHECK_OK) all_ok = false;
	}
	check(all_short, "every length from 0 to 111 is too short");
	check(all_ok, "every length from 112 to 300 is accepted");
}

static void test_no_image(void)
{
	bool all = true;
	int value;

	fresh_image();
	image[AT_MAGIC] = 0x00;
	check(checked(OTA_CHECK_BYTES, FILE_SIZE, SLOT) == OTA_CHECK_NO_IMAGE, "a file that starts with a zero byte is no image");
	check(version[0] == '\0', "no image: the version is an empty text");
	image[AT_MAGIC] = 0xE8;
	check(result_of() == OTA_CHECK_NO_IMAGE, "0xE8 as first byte is no image");
	image[AT_MAGIC] = 0xEA;
	check(result_of() == OTA_CHECK_NO_IMAGE, "0xEA as first byte is no image");
	image[AT_MAGIC] = 0x69;
	check(result_of() == OTA_CHECK_NO_IMAGE, "0x69 as first byte, 0xE9 without its highest bit, is no image");

	for(value = 0; value <= 255; value++)
	{
		image[AT_MAGIC] = (uint8_t)value;
		if((result_of() == OTA_CHECK_NO_IMAGE) != (value != 0xE9)) all = false;
	}
	check(all, "of all 256 first bytes only 0xE9 is an image");

	fresh_image();
	image[1] = 0xE9;
	image[AT_MAGIC] = 0x06;
	check(result_of() == OTA_CHECK_NO_IMAGE, "the magic byte in the second place is no image");
}

static void test_wrong_chip(void)
{
	int others = 0, right = 0;
	long value;

	fresh_image();
	image[AT_CHIP] = 0x00;
	check(checked(OTA_CHECK_BYTES, FILE_SIZE, SLOT) == OTA_CHECK_WRONG_CHIP, "chip id 0, an ESP32, is the wrong chip");
	check(version[0] == '\0', "wrong chip: the version is an empty text");
	image[AT_CHIP] = 0x05;
	check(result_of() == OTA_CHECK_WRONG_CHIP, "chip id 5, an ESP32-C3, is the wrong chip");
	image[AT_CHIP] = 0x02;
	check(result_of() == OTA_CHECK_WRONG_CHIP, "chip id 2, an ESP32-S2, is the wrong chip");
	image[AT_CHIP] = 0x09;
	image[AT_CHIP + 1] = 0x01;
	check(result_of() == OTA_CHECK_WRONG_CHIP, "chip id 0x0109 is the wrong chip: the second byte counts");
	image[AT_CHIP] = 0x00;
	image[AT_CHIP + 1] = 0x09;
	check(result_of() == OTA_CHECK_WRONG_CHIP, "chip id 0x0900 is the wrong chip: the first byte is the low one");

	for(value = 0; value <= 0xFFFF; value++)
	{
		ota_check_t result;

		image[AT_CHIP] = (uint8_t)(value & 0xFF);
		image[AT_CHIP + 1] = (uint8_t)(value >> 8);
		result = result_of();
		if(result == OTA_CHECK_WRONG_CHIP) others++;
		if(result == OTA_CHECK_OK && value == 0x0009) right++;
	}
	check(others == 65535 && right == 1, "of all 65536 chip ids only 0x0009 is accepted");
}

static void test_no_description(void)
{
	uint8_t longer[300];
	bool all = true;
	int at, value;

	fresh_image();
	image[AT_DESCRIPTION] = 0x33;
	check(checked(OTA_CHECK_BYTES, FILE_SIZE, SLOT) == OTA_CHECK_NO_DESCRIPTION, "another first byte of the magic number: no description");
	check(version[0] == '\0', "no description: the version is an empty text");
	for(at = AT_DESCRIPTION; at < AT_DESCRIPTION + 4; at++)
	{
		for(value = 0; value <= 255; value++)
		{
			fresh_image();
			if(value == display_image[at]) continue;
			image[at] = (uint8_t)value;
			if(result_of() != OTA_CHECK_NO_DESCRIPTION) all = false;
		}
	}
	check(all, "every other value in each of the four bytes of the magic number: no description");
	fresh_image();
	image[AT_DESCRIPTION] = 0xAB;
	image[AT_DESCRIPTION + 1] = 0xCD;
	image[AT_DESCRIPTION + 2] = 0x54;
	image[AT_DESCRIPTION + 3] = 0x32;
	check(result_of() == OTA_CHECK_NO_DESCRIPTION, "the magic number with its high byte first: no description");

	fresh_image();
	fill_text(AT_VERSION, 'v');
	check(checked(OTA_CHECK_BYTES, FILE_SIZE, SLOT) == OTA_CHECK_NO_DESCRIPTION && version[0] == '\0',
	      "a version of 32 bytes without a terminating zero: no description, no version");
	put_text(AT_PROJECT, "");
	check(checked(OTA_CHECK_BYTES, FILE_SIZE, SLOT) == OTA_CHECK_NO_DESCRIPTION && version[0] == '\0',
	      "a version of 32 bytes without a terminating zero in front of an empty project name: no description");
	put_text(AT_PROJECT, "wican-display");
	image[AT_VERSION + TEXT_BYTES - 1] = 0;
	check(checked(OTA_CHECK_BYTES, FILE_SIZE, SLOT) == OTA_CHECK_OK && strlen(version) == 31 && strspn(version, "v") == 31,
	      "a version of 31 bytes and a zero is accepted and copied");
	put_text(AT_VERSION, "");
	check(checked(OTA_CHECK_BYTES, FILE_SIZE, SLOT) == OTA_CHECK_OK && version[0] == '\0', "an empty version is accepted");

	fresh_image();
	fill_text(AT_PROJECT, 'p');
	check(checked(OTA_CHECK_BYTES, FILE_SIZE, SLOT) == OTA_CHECK_NO_DESCRIPTION && version[0] == '\0',
	      "a project name of 32 bytes without a terminating zero: no description");
	memcpy(&image[AT_PROJECT], "wican-display", 13);
	check(result_of() == OTA_CHECK_NO_DESCRIPTION, "a project name without a terminating zero that begins with the right name: no description");
	image[AT_PROJECT + TEXT_BYTES - 1] = 0;
	check(checked(OTA_CHECK_BYTES, FILE_SIZE, SLOT) == OTA_CHECK_WRONG_PROJECT && strcmp(version, VERSION) == 0,
	      "a project name of 31 bytes and a zero is a description, of another project");

	fresh_image();
	fill_text(AT_VERSION, 'v');
	fill_text(AT_PROJECT, 'p');
	check(result_of() == OTA_CHECK_NO_DESCRIPTION, "version and project name without a terminating zero: no description");

	// More of the file than needed, with a zero right behind the project name
	memset(longer, 0, sizeof(longer));
	memcpy(longer, display_image, OTA_CHECK_BYTES);
	memset(&longer[AT_PROJECT], 'p', TEXT_BYTES);
	memset(version, GUARD, sizeof(version));
	check(ota_check(longer, sizeof(longer), FILE_SIZE, SLOT, version, sizeof(version)) == OTA_CHECK_NO_DESCRIPTION && version[0] == '\0',
	      "a project name without a zero in its 32 bytes but with one behind them: no description");
}

static void test_wrong_project(void)
{
	static const char *const others[] = {
		"wican-fw_obd_v300_421", "", "w", "wican", "wican-displa", "wican-display2", "wican-display ", "Wican-display",
		"WICAN-DISPLAY", "wican_display", " wican-display", "xwican-display", "wican-displaz",
	};
	bool all = true;
	size_t i;

	fresh_image();
	put_text(AT_PROJECT, "wican-fw_obd_v300_421");
	put_text(AT_VERSION, "v4.21");
	check(checked(OTA_CHECK_BYTES, FILE_SIZE, SLOT) == OTA_CHECK_WRONG_PROJECT, "the firmware of the WiCAN is the wrong project");
	check(strcmp(version, "v4.21") == 0, "wrong project: the version text of the image is copied");

	for(i = 0; i < sizeof(others) / sizeof(others[0]); i++)
	{
		fresh_image();
		put_text(AT_PROJECT, others[i]);
		if(result_of() != OTA_CHECK_WRONG_PROJECT)
		{
			printf("  project \"%s\" is not refused\n", others[i]);
			all = false;
		}
	}
	check(all, "no name, a shorter, a longer, a name in another case or with another sign is the wrong project");

	fresh_image();
	memcpy(&image[AT_PROJECT], "wican-display\0junk behind it", 28);
	check(result_of() == OTA_CHECK_OK, "bytes behind the terminating zero of the project name do not count");
}

static void test_too_large(void)
{
	fresh_image();
	check(checked(OTA_CHECK_BYTES, SLOT, SLOT) == OTA_CHECK_OK && strcmp(version, VERSION) == 0, "a file as large as the slot is accepted");
	check(checked(OTA_CHECK_BYTES, SLOT + 1, SLOT) == OTA_CHECK_TOO_LARGE, "a file one byte larger than the slot is too large");
	check(version[0] == '\0', "too large: the version is an empty text");
	check(checked(OTA_CHECK_BYTES, SLOT - 1, SLOT) == OTA_CHECK_OK, "a file one byte smaller than the slot is accepted");
	check(checked(OTA_CHECK_BYTES, 0, SLOT) == OTA_CHECK_TOO_LARGE && version[0] == '\0', "an empty file is refused as too large");
	check(checked(OTA_CHECK_BYTES, 1, SLOT) == OTA_CHECK_OK, "a file of one byte is not empty");
	check(checked(OTA_CHECK_BYTES, UINT32_MAX, SLOT) == OTA_CHECK_TOO_LARGE, "the largest file size is too large for the slot");
	check(checked(OTA_CHECK_BYTES, UINT32_MAX, UINT32_MAX) == OTA_CHECK_OK && checked(OTA_CHECK_BYTES, UINT32_MAX, UINT32_MAX - 1) == OTA_CHECK_TOO_LARGE,
	      "the largest file size fits into a slot of that size and not into one a byte smaller");
	check(checked(OTA_CHECK_BYTES, 0x80000000u, 0x7FFFFFFFu) == OTA_CHECK_TOO_LARGE && checked(OTA_CHECK_BYTES, 0x7FFFFFFFu, 0x80000000u) == OTA_CHECK_OK,
	      "sizes around 2^31 are compared without a sign");
	check(checked(OTA_CHECK_BYTES, 0, 0) == OTA_CHECK_TOO_LARGE && checked(OTA_CHECK_BYTES, 1, 0) == OTA_CHECK_TOO_LARGE, "nothing fits into a slot of no size");
}

// Images in which several checks fail at once
static void test_order(void)
{
	fresh_image();
	image[AT_MAGIC] = 0x00;
	image[AT_CHIP] = 0x05;
	image[AT_DESCRIPTION] = 0x00;
	put_text(AT_PROJECT, "wican-fw_obd_v300_421");
	check(checked(OTA_CHECK_BYTES - 1, SLOT + 1, SLOT) == OTA_CHECK_TOO_SHORT && version[0] == '\0', "everything wrong and too short: too short");
	check(checked(OTA_CHECK_BYTES, SLOT + 1, SLOT) == OTA_CHECK_NO_IMAGE && version[0] == '\0', "everything wrong but long enough: no image");
	image[AT_MAGIC] = 0xE9;
	check(checked(OTA_CHECK_BYTES, SLOT + 1, SLOT) == OTA_CHECK_WRONG_CHIP && version[0] == '\0', "an image with everything else wrong: wrong chip");
	image[AT_CHIP] = 0x09;
	check(checked(OTA_CHECK_BYTES, SLOT + 1, SLOT) == OTA_CHECK_NO_DESCRIPTION && version[0] == '\0',
	      "the right chip, no description, another project name and too large: no description");
	image[AT_DESCRIPTION] = 0x32;
	check(checked(OTA_CHECK_BYTES, SLOT + 1, SLOT) == OTA_CHECK_WRONG_PROJECT && strcmp(version, VERSION) == 0,
	      "the wrong project and too large: wrong project, with its version");
	check(checked(OTA_CHECK_BYTES, 0, SLOT) == OTA_CHECK_WRONG_PROJECT && strcmp(version, VERSION) == 0, "the wrong project and empty: wrong project");

	fresh_image();
	check(checked(0, 0, SLOT) == OTA_CHECK_TOO_SHORT, "an empty file with no bytes given: too short");
	check(checked(OTA_CHECK_BYTES - 1, SLOT + 1, SLOT) == OTA_CHECK_TOO_SHORT, "too short and too large: too short");
	image[AT_MAGIC] = 0x00;
	check(checked(OTA_CHECK_BYTES - 1, FILE_SIZE, SLOT) == OTA_CHECK_TOO_SHORT, "too short and no image: too short");
	check(checked(OTA_CHECK_BYTES, SLOT + 1, SLOT) == OTA_CHECK_NO_IMAGE, "no image and too large: no image");
	image[AT_CHIP] = 0x05;
	check(checked(OTA_CHECK_BYTES, FILE_SIZE, SLOT) == OTA_CHECK_NO_IMAGE, "no image and the wrong chip: no image");

	fresh_image();
	image[AT_CHIP] = 0x05;
	check(checked(OTA_CHECK_BYTES, SLOT + 1, SLOT) == OTA_CHECK_WRONG_CHIP, "the wrong chip and too large: wrong chip");
	fill_text(AT_VERSION, 'v');
	check(result_of() == OTA_CHECK_WRONG_CHIP, "the wrong chip and a version without a terminating zero: wrong chip");
	fresh_image();
	image[AT_CHIP] = 0x05;
	put_text(AT_PROJECT, "wican-fw_obd_v300_421");
	check(result_of() == OTA_CHECK_WRONG_CHIP, "the wrong chip and the wrong project: wrong chip");

	fresh_image();
	fill_text(AT_PROJECT, 'p');
	check(checked(OTA_CHECK_BYTES, SLOT + 1, SLOT) == OTA_CHECK_NO_DESCRIPTION, "a project name without a terminating zero and too large: no description");
	fresh_image();
	fill_text(AT_VERSION, 'v');
	put_text(AT_PROJECT, "wican-fw_obd_v300_421");
	check(checked(OTA_CHECK_BYTES, FILE_SIZE, SLOT) == OTA_CHECK_NO_DESCRIPTION && version[0] == '\0',
	      "a version without a terminating zero and the wrong project: no description");
	fresh_image();
	fill_text(AT_VERSION, 'v');
	check(checked(OTA_CHECK_BYTES, 0, SLOT) == OTA_CHECK_NO_DESCRIPTION, "a version without a terminating zero and empty: no description");
}

// One byte of the image changed to every other value. Which result that has to give follows from the
// layout: the place of the byte says which field it belongs to.
static void test_every_byte(void)
{
	int wrong_fields = 0, wrong_rest = 0, wrong_version = 0, wrong_project = 0, count = 0;
	int at, value;

	for(at = 0; at < OTA_CHECK_BYTES; at++)
	{
		for(value = 0; value <= 255; value++)
		{
			ota_check_t expected = OTA_CHECK_OK, result;
			char expected_version[TEXT_BYTES + 1];

			if(value == display_image[at]) continue;
			fresh_image();
			image[at] = (uint8_t)value;
			count++;

			// What a reader of the 32 bytes finds up to the first zero
			memset(expected_version, 0, sizeof(expected_version));
			memcpy(expected_version, &image[AT_VERSION], TEXT_BYTES);

			if(at == AT_MAGIC) expected = OTA_CHECK_NO_IMAGE;
			else if(at == AT_CHIP || at == AT_CHIP + 1) expected = OTA_CHECK_WRONG_CHIP;
			else if(at >= AT_DESCRIPTION && at < AT_DESCRIPTION + 4) expected = OTA_CHECK_NO_DESCRIPTION;
			// The name and the zero that ends it, 14 bytes
			else if(at >= AT_PROJECT && at < AT_PROJECT + 14) expected = OTA_CHECK_WRONG_PROJECT;
			if(expected != OTA_CHECK_OK && expected != OTA_CHECK_WRONG_PROJECT) expected_version[0] = '\0';

			result = checked(OTA_CHECK_BYTES, FILE_SIZE, SLOT);
			if(result != expected || strcmp(version, expected_version) != 0)
			{
				if(at >= AT_VERSION && at < AT_VERSION + TEXT_BYTES) wrong_version++;
				else if(at >= AT_PROJECT) wrong_project++;
				else if(expected != OTA_CHECK_OK) wrong_fields++;
				else wrong_rest++;
			}
		}
	}
	check(count == 112 * 255, "every byte of the image is changed to each of its 255 other values");
	check(wrong_fields == 0, "a changed byte of the magic byte, the chip id or the magic number of the description gives the result of its check");
	check(wrong_version == 0, "a changed byte of the version changes the copied version up to its first zero and nothing else");
	check(wrong_project == 0, "a changed byte of the project name or its zero is the wrong project, a byte behind them changes nothing");
	check(wrong_rest == 0, "a changed byte in no field of the layout changes nothing");
}

// The version of the image copied into a buffer of `size` bytes with guard bytes around it
static bool version_is(const char *text, size_t size, const char *expected)
{
	unsigned char buffer[4 + 64 + 8];
	char *out = (char *)buffer + 4;
	ota_check_t result;
	size_t i;

	fresh_image();
	put_text(AT_VERSION, text);
	memset(buffer, GUARD, sizeof(buffer));
	result = ota_check(image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, out, size);
	if(result != OTA_CHECK_OK) return false;

	for(i = 0; i < 4; i++) if(buffer[i] != GUARD) return false;
	for(i = 4 + size; i < sizeof(buffer); i++) if(buffer[i] != GUARD) return false;
	// With no room nothing may be written, so there is nothing to compare
	if(size == 0) return true;
	if(strcmp(out, expected) != 0)
	{
		printf("  \"%s\" into %lu bytes: expected \"%s\", got \"%.60s\"\n", text, (unsigned long)size, expected, out);
		return false;
	}
	return true;
}

static void test_version(void)
{
	unsigned char guard[8];

	check(version_is(VERSION, 64, VERSION) && version_is(VERSION, 18, VERSION), "the version fits into a larger buffer");
	check(version_is(VERSION, 17, VERSION), "the version fits exactly with its terminating zero");
	check(version_is(VERSION, 16, "1.2.0-3-g4f5a6b"), "a version one byte too long is cut by one byte");
	check(version_is(VERSION, 9, "1.2.0-3-") && version_is(VERSION, 2, "1"), "a version is cut to what fits");
	check(version_is(VERSION, 1, ""), "a buffer of one byte takes an empty text");
	check(version_is(VERSION, 0, ""), "no room for the version: nothing is written");
	check(version_is("1234567890123456789012345678901", 32, "1234567890123456789012345678901") &&
	      version_is("1234567890123456789012345678901", 31, "123456789012345678901234567890"), "the longest version, 31 bytes, fits into 32 bytes and is cut for 31");

	fresh_image();
	check(ota_check(image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, NULL, 16) == OTA_CHECK_OK, "no buffer for the version but a size: the image is accepted all the same");
	put_text(AT_PROJECT, "wican-fw_obd_v300_421");
	check(ota_check(image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, NULL, 16) == OTA_CHECK_WRONG_PROJECT &&
	      ota_check(image, OTA_CHECK_BYTES - 1, FILE_SIZE, SLOT, NULL, 16) == OTA_CHECK_TOO_SHORT, "no buffer for the version with another result");

	// The image of another project gets its version cut the same way
	memset(guard, GUARD, sizeof(guard));
	check(ota_check(image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, (char *)guard, 4) == OTA_CHECK_WRONG_PROJECT && strcmp((char *)guard, "1.2") == 0 && guard[4] == GUARD,
	      "the version of the wrong project is cut to what fits");

	// A result without a version and no room: nothing is written
	memset(guard, GUARD, sizeof(guard));
	check(ota_check(image, OTA_CHECK_BYTES - 1, FILE_SIZE, SLOT, (char *)guard, 0) == OTA_CHECK_TOO_SHORT && guard[0] == GUARD,
	      "too short and no room for the version: nothing is written");
	check(ota_check(image, OTA_CHECK_BYTES - 1, FILE_SIZE, SLOT, (char *)guard, 1) == OTA_CHECK_TOO_SHORT && guard[0] == '\0' && guard[1] == GUARD,
	      "too short and one byte for the version: an empty text, nothing behind it");
}

static void test_version_utf8(void)
{
	// "abäcd"
	check(version_is("ab\303\244cd", 7, "ab\303\244cd"), "a version with a character of two bytes fits");
	check(version_is("ab\303\244cd", 6, "ab\303\244c") && version_is("ab\303\244cd", 5, "ab\303\244"), "cut behind a character of two bytes");
	check(version_is("ab\303\244cd", 4, "ab"), "a character of two bytes is not cut in half");
	check(version_is("ab\303\244cd", 3, "ab") && version_is("ab\303\244cd", 2, "a"), "cut in front of a character of two bytes");

	// "x€y"
	check(version_is("x\342\202\254y", 6, "x\342\202\254y") && version_is("x\342\202\254y", 5, "x\342\202\254"), "a character of three bytes fits");
	check(version_is("x\342\202\254y", 4, "x"), "a character of three bytes is not cut behind its second byte");
	check(version_is("x\342\202\254y", 3, "x"), "a character of three bytes is not cut behind its first byte");
	check(version_is("x\342\202\254y", 2, "x"), "cut in front of a character of three bytes");

	// A van and "z"
	check(version_is("\360\237\232\220z", 6, "\360\237\232\220z") && version_is("\360\237\232\220z", 5, "\360\237\232\220"), "a character of four bytes fits");
	check(version_is("\360\237\232\220z", 4, ""), "a character of four bytes is not cut behind its third byte");
	check(version_is("\360\237\232\220z", 3, "") && version_is("\360\237\232\220z", 2, ""), "a character of four bytes is not cut behind its second or first byte");

	check(version_is("\303\244\303\266\303\274", 7, "\303\244\303\266\303\274") && version_is("\303\244\303\266\303\274", 6, "\303\244\303\266") &&
	      version_is("\303\244\303\266\303\274", 5, "\303\244\303\266") && version_is("\303\244\303\266\303\274", 4, "\303\244"), "characters of two bytes in a row are cut between them");

	// Not UTF-8: the bytes are passed on, a cut never leaves a continuation byte in front of it standing alone
	check(version_is("ab\303", 4, "ab\303") && version_is("\200\200\200", 4, "\200\200\200") && version_is("\377\376", 3, "\377\376"), "a version that is no UTF-8 is copied as it is when it fits");
	check(version_is("\200\200\200\200", 3, "") && version_is("a\200\200\200", 3, ""), "no cut in front of a continuation byte, also where the bytes are no UTF-8");
	check(version_is("ab\303", 3, "ab") && version_is("a\377\377\377", 3, "a\377"), "bytes that are no continuation bytes are cut like single characters");
	check(version_is("ab\200\200\200\200\200", 7, "a") && version_is("ab\200\200\200\200\200\200\200", 9, "a"),
	      "a run of five or seven continuation bytes is not cut anywhere, however long it is");
	check(version_is("ab\200c", 3, "a") && version_is("ab\277c", 3, "a"), "0x80 and 0xBF, the first and the last continuation byte, are not cut off from the byte in front of them");
	check(version_is("ab\177c", 3, "ab") && version_is("ab\300c", 3, "ab"), "0x7F and 0xC0, the bytes next to the continuation bytes, start a character");
}

// Every length of version in every size of buffer, for an image of the display and for another project
static void test_version_lengths(void)
{
	static const char digits[] = "0123456789abcdefghijklmnopqrstuv";
	unsigned char buffer[4 + 64 + 8];
	char *out = (char *)buffer + 4;
	char text[TEXT_BYTES], expected[TEXT_BYTES];
	int wrong_result = 0, wrong_text = 0, wrong_guard = 0, wrong_empty = 0, count = 0, other;
	size_t length, size, kept, i;

	for(other = 0; other <= 1; other++)
	{
		for(length = 0; length < TEXT_BYTES; length++)
		{
			memcpy(text, digits, length);
			text[length] = '\0';
			fresh_image();
			put_text(AT_VERSION, text);
			if(other) put_text(AT_PROJECT, "wican-fw_obd_v300_421");

			for(size = 0; size <= 40; size++)
			{
				// As much of the text as fits in front of the terminating zero
				kept = size == 0 ? 0 : (length < size - 1 ? length : size - 1);
				memcpy(expected, text, kept);
				expected[kept] = '\0';

				memset(buffer, GUARD, sizeof(buffer));
				if(ota_check(image, OTA_CHECK_BYTES, FILE_SIZE, SLOT, out, size) != (other ? OTA_CHECK_WRONG_PROJECT : OTA_CHECK_OK)) wrong_result++;
				if(size > 0 && strcmp(out, expected) != 0) wrong_text++;
				for(i = 0; i < 4; i++) if(buffer[i] != GUARD) wrong_guard++;
				for(i = 4 + size; i < sizeof(buffer); i++) if(buffer[i] != GUARD) wrong_guard++;

				// The same image as a file that is too short: no version
				memset(buffer, GUARD, sizeof(buffer));
				if(ota_check(image, OTA_CHECK_BYTES - 1, FILE_SIZE, SLOT, out, size) != OTA_CHECK_TOO_SHORT) wrong_result++;
				if(size > 0 && out[0] != '\0') wrong_empty++;
				for(i = 0; i < 4; i++) if(buffer[i] != GUARD) wrong_guard++;
				for(i = 4 + size; i < sizeof(buffer); i++) if(buffer[i] != GUARD) wrong_guard++;
				count++;
			}
		}
	}
	check(count == 2 * 32 * 41, "every version of 0 to 31 bytes is copied into every buffer of 0 to 40 bytes");
	check(wrong_result == 0, "the result does not depend on the length of the version or the size of its buffer");
	check(wrong_text == 0, "of every length of version as much is copied as fits, with a terminating zero");
	check(wrong_empty == 0, "a result without a version leaves an empty text in a buffer of every size");
	check(wrong_guard == 0, "no version of any length is written outside a buffer of any size");
}

/*
 * The checks written a second time from the layout in ota_check.h, byte by byte and without the helpers of
 * ota_check.c, for the random images below. `text` gets the version a result has.
 */
static ota_check_t model_check(const uint8_t *data, size_t length, uint32_t file_size, uint32_t slot_size, char *text)
{
	static const uint8_t magic[4] = {0x32, 0x54, 0xCD, 0xAB};
	static const uint8_t name[14] = {'w', 'i', 'c', 'a', 'n', '-', 'd', 'i', 's', 'p', 'l', 'a', 'y', 0};
	int version_length = -1, i;
	bool project_ends = false;

	text[0] = '\0';
	if(length < 112) return OTA_CHECK_TOO_SHORT;
	if(data[0] != 0xE9) return OTA_CHECK_NO_IMAGE;
	if(data[12] != 0x09 || data[13] != 0x00) return OTA_CHECK_WRONG_CHIP;
	for(i = 0; i < 4; i++)
	{
		if(data[32 + i] != magic[i]) return OTA_CHECK_NO_DESCRIPTION;
	}
	for(i = 31; i >= 0; i--)
	{
		if(data[48 + i] == 0) version_length = i;
		if(data[80 + i] == 0) project_ends = true;
	}
	if(version_length < 0 || !project_ends) return OTA_CHECK_NO_DESCRIPTION;

	// The name and the zero behind it, 14 bytes
	if(memcmp(&data[80], name, 14) == 0 && (file_size == 0 || file_size > slot_size)) return OTA_CHECK_TOO_LARGE;
	memcpy(text, &data[48], (size_t)version_length);
	text[version_length] = '\0';
	return memcmp(&data[80], name, 14) == 0 ? OTA_CHECK_OK : OTA_CHECK_WRONG_PROJECT;
}

static uint32_t random_state = 20261003;

static uint32_t random_next(void)
{
	random_state = random_state * 1664525u + 1013904223u;
	return random_state >> 8;
}

// One of `count` values, the first of them in `share` of 100 cases
static uint32_t pick(const uint32_t *values, uint32_t count, uint32_t share)
{
	if(random_next() % 100 < share) return values[0];
	return values[random_next() % count];
}

// Images put together field by field: every field is mostly right and sometimes wrong in one of several
// ways, the bytes between the fields are anything. Every answer is compared with the model.
static void test_random_images(void)
{
	static const uint32_t magics[] = {0xE9, 0x00, 0xE8, 0xEA, 0x69, 0xFF};
	static const uint32_t chips[] = {0x0009, 0x0000, 0x0005, 0x0002, 0x0109, 0x0900, 0x8009, 0x000D, 0x0008};
	static const uint32_t descriptions[] = {0xABCD5432u, 0x00000000u, 0x3254CDABu, 0xABCD5433u, 0xABCD1432u, 0xAB4D5432u, 0x2BCD5432u, 0xFFFFFFFFu};
	static const uint32_t sizes[] = {FILE_SIZE, 0, 1, 111, 112, SLOT - 1, SLOT, SLOT + 1, 0x7FFFFFFFu, 0x80000000u, UINT32_MAX};
	static const uint32_t slots[] = {SLOT, 0, 1, FILE_SIZE, 0x7FFFFFFFu, UINT32_MAX};
	static const size_t lengths[] = {OTA_CHECK_BYTES, 0, 1, 80, 111, 113, 200, 300};
	static const size_t rooms[] = {64, 0, 1, 2, 5, 17, 32, 33};
	static const char *const projects[] = {
		"wican-display", "wican-fw_obd_v300_421", "", "w", "wican-displa", "wican-display2", "Wican-display", "wican_display", "esp-idf",
	};
	static uint8_t data[300];
	unsigned char buffer[4 + 64 + 8];
	char *out = (char *)buffer + 4;
	char expected[TEXT_BYTES];
	int results[OTA_CHECK_TOO_LARGE + 1] = {0, 0, 0, 0, 0, 0, 0};
	int wrong_result = 0, wrong_text = 0, wrong_guard = 0, shown = 0;
	long step;

	for(step = 0; step < 400000; step++)
	{
		uint32_t value, file_size, slot_size, style = random_next() % 10;
		size_t length, room, kept, i, text_length;
		ota_check_t result, model;
		const char *name;

		// The bytes that belong to no field: anything, all zero or all ones
		for(i = 0; i < sizeof(data); i++) data[i] = style == 0 ? 0x00 : style == 1 ? 0xFF : (uint8_t)random_next();

		data[AT_MAGIC] = (uint8_t)pick(magics, sizeof(magics) / sizeof(magics[0]), 90);
		value = pick(chips, sizeof(chips) / sizeof(chips[0]), 90);
		data[AT_CHIP] = (uint8_t)(value & 0xFF);
		data[AT_CHIP + 1] = (uint8_t)(value >> 8);
		value = pick(descriptions, sizeof(descriptions) / sizeof(descriptions[0]), 90);
		for(i = 0; i < 4; i++) data[AT_DESCRIPTION + i] = (uint8_t)(value >> (8 * i));

		// A version of 0 to 31 characters, in one of ten cases without a terminating zero. What stands behind
		// the zero is left as it is.
		text_length = random_next() % TEXT_BYTES;
		if(random_next() % 10 == 0) text_length = TEXT_BYTES;
		for(i = 0; i < text_length; i++) data[AT_VERSION + i] = (uint8_t)('!' + random_next() % 94);
		if(text_length < TEXT_BYTES) data[AT_VERSION + text_length] = 0;

		// A project name, mostly the right one, in one of ten cases without a terminating zero in the rest of
		// its field
		name = projects[random_next() % 100 < 75 ? 0 : random_next() % (sizeof(projects) / sizeof(projects[0]))];
		text_length = strlen(name);
		memcpy(&data[AT_PROJECT], name, text_length);
		if(random_next() % 10 == 0)
		{
			for(i = text_length; i < TEXT_BYTES; i++) data[AT_PROJECT + i] = (uint8_t)(1 + random_next() % 255);
		}
		else
		{
			data[AT_PROJECT + text_length] = 0;
		}

		file_size = pick(sizes, sizeof(sizes) / sizeof(sizes[0]), 80);
		slot_size = pick(slots, sizeof(slots) / sizeof(slots[0]), 85);
		length = lengths[random_next() % 100 < 85 ? 0 : random_next() % (sizeof(lengths) / sizeof(lengths[0]))];
		room = rooms[random_next() % 100 < 50 ? 0 : random_next() % (sizeof(rooms) / sizeof(rooms[0]))];

		model = model_check(data, length, file_size, slot_size, expected);
		kept = strlen(expected);
		if(room > 0 && kept > room - 1) kept = room - 1;
		expected[kept] = '\0';

		memset(buffer, GUARD, sizeof(buffer));
		result = ota_check(data, length, file_size, slot_size, out, room);
		results[model]++;
		if(result != model)
		{
			if(shown++ < 5) printf("  step %ld: expected result %d, got %d\n", step, (int)model, (int)result);
			wrong_result++;
		}
		else if(room > 0 && strcmp(out, expected) != 0)
		{
			if(shown++ < 5) printf("  step %ld: result %d, expected version \"%s\"\n", step, (int)model, expected);
			wrong_text++;
		}
		for(i = 0; i < 4; i++) if(buffer[i] != GUARD) wrong_guard++;
		for(i = 4 + room; i < sizeof(buffer); i++) if(buffer[i] != GUARD) wrong_guard++;
	}

	printf("  random images: %d ok, %d too short, %d no image, %d wrong chip, %d no description, %d wrong project, %d too large\n",
	       results[OTA_CHECK_OK], results[OTA_CHECK_TOO_SHORT], results[OTA_CHECK_NO_IMAGE], results[OTA_CHECK_WRONG_CHIP],
	       results[OTA_CHECK_NO_DESCRIPTION], results[OTA_CHECK_WRONG_PROJECT], results[OTA_CHECK_TOO_LARGE]);
	check(results[OTA_CHECK_OK] > 20000 && results[OTA_CHECK_TOO_SHORT] > 5000 && results[OTA_CHECK_NO_IMAGE] > 5000 && results[OTA_CHECK_WRONG_CHIP] > 5000 &&
	      results[OTA_CHECK_NO_DESCRIPTION] > 5000 && results[OTA_CHECK_WRONG_PROJECT] > 5000 && results[OTA_CHECK_TOO_LARGE] > 5000,
	      "the random images reach every result more than 5000 times");
	check(wrong_result == 0, "400000 random images: every result is the one of the checks written a second time");
	check(wrong_text == 0, "400000 random images: the version is the one of the image where the result has one, cut to the buffer, and empty otherwise");
	check(wrong_guard == 0, "400000 random images: nothing is written outside the buffer for the version");
}

int main(void)
{
	image = malloc(OTA_CHECK_BYTES);
	if(image == NULL) return 2;

	test_ok();
	test_too_short();
	test_no_image();
	test_wrong_chip();
	test_no_description();
	test_wrong_project();
	test_too_large();
	test_order();
	test_every_byte();
	test_version();
	test_version_utf8();
	test_version_lengths();
	test_random_images();
	check(same_without_buffer, "every result is the same without a buffer or without room for the version");

	free(image);
	return test_end();
}
