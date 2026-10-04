"""Mutations of display/components/core/ota_check.c, see ../redproof.py."""

F = "components/core/ota_check.c"
H = "components/core/ota_check.h"
T = "test_ota_check"

SHORT = "\tif(length < OTA_CHECK_BYTES) return OTA_CHECK_TOO_SHORT;\n"
IMAGE = "\tif(data[0] != IMAGE_MAGIC) return OTA_CHECK_NO_IMAGE;\n"
CHIP = "\tif((data[CHIP_ID_OFFSET] | (data[CHIP_ID_OFFSET + 1] << 8)) != OTA_CHIP_ESP32S3) return OTA_CHECK_WRONG_CHIP;\n"
MAGIC = "\tif(magic != DESCRIPTION_MAGIC) return OTA_CHECK_NO_DESCRIPTION;"
ZEROS = "\tif(memchr(image_version, '\\0', TEXT_SIZE) == NULL || memchr(project, '\\0', TEXT_SIZE) == NULL)"
PROJECT = "\tif(strcmp(project, OTA_PROJECT_NAME) != 0)\n\t{"
LARGE = "\telse if(file_size == 0 || file_size > slot_size)\n\t{\n\t\treturn OTA_CHECK_TOO_LARGE;\n\t}\n"
CUT = "\t\twhile(length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80) length--;"

MUTATIONS = [
    # too short
    ("ota_112_bytes_too_short", T, F, SHORT, "\tif(length <= OTA_CHECK_BYTES) return OTA_CHECK_TOO_SHORT;\n"),
    ("ota_111_bytes_long_enough", T, F, SHORT, "\tif(length < OTA_CHECK_BYTES - 1) return OTA_CHECK_TOO_SHORT;\n"),
    ("ota_length_not_checked", T, F, SHORT, "\t(void)length;\n"),
    ("ota_long_block_too_short", T, F, SHORT, "\tif(length < OTA_CHECK_BYTES || length > 300) return OTA_CHECK_TOO_SHORT;\n"),
    ("ota_check_bytes_changed", T, H, "#define OTA_CHECK_BYTES     112", "#define OTA_CHECK_BYTES     113"),

    # magic byte of the image
    ("ota_image_magic_not_checked", T, F, IMAGE, ""),
    ("ota_image_magic_other", T, F, "#define IMAGE_MAGIC         0xE9", "#define IMAGE_MAGIC         0xEA"),
    ("ota_image_magic_high_bit_ignored", T, F, "\tif(data[0] != IMAGE_MAGIC)", "\tif((data[0] | 0x80) != IMAGE_MAGIC)"),
    ("ota_image_magic_in_second_byte_accepted", T, F, "\tif(data[0] != IMAGE_MAGIC)", "\tif(data[0] != IMAGE_MAGIC && data[1] != IMAGE_MAGIC)"),

    # chip
    ("ota_chip_not_checked", T, F, CHIP, ""),
    ("ota_chip_one_byte_later", T, F, "#define CHIP_ID_OFFSET      12", "#define CHIP_ID_OFFSET      13"),
    ("ota_chip_one_byte_earlier", T, F, "#define CHIP_ID_OFFSET      12", "#define CHIP_ID_OFFSET      11"),
    ("ota_chip_high_byte_ignored", T, F, "\tif((data[CHIP_ID_OFFSET] | (data[CHIP_ID_OFFSET + 1] << 8)) != OTA_CHIP_ESP32S3)", "\tif(data[CHIP_ID_OFFSET] != OTA_CHIP_ESP32S3)"),
    ("ota_chip_low_byte_ignored", T, F,
     "\tif((data[CHIP_ID_OFFSET] | (data[CHIP_ID_OFFSET + 1] << 8)) != OTA_CHIP_ESP32S3)", "\tif((data[CHIP_ID_OFFSET + 1] << 8) != (OTA_CHIP_ESP32S3 & 0xFF00))"),
    ("ota_chip_high_byte_first", T, F,
     "\tif((data[CHIP_ID_OFFSET] | (data[CHIP_ID_OFFSET + 1] << 8)) != OTA_CHIP_ESP32S3)", "\tif((data[CHIP_ID_OFFSET + 1] | (data[CHIP_ID_OFFSET] << 8)) != OTA_CHIP_ESP32S3)"),
    ("ota_chip_other", T, H, "#define OTA_CHIP_ESP32S3    0x0009", "#define OTA_CHIP_ESP32S3    0x0005"),
    ("ota_chip_esp32_accepted", T, F,
     "\tif((data[CHIP_ID_OFFSET] | (data[CHIP_ID_OFFSET + 1] << 8)) != OTA_CHIP_ESP32S3)",
     "\tif((data[CHIP_ID_OFFSET] | (data[CHIP_ID_OFFSET + 1] << 8)) != OTA_CHIP_ESP32S3 && (data[CHIP_ID_OFFSET] | data[CHIP_ID_OFFSET + 1]) != 0)"),

    # application description
    ("ota_description_magic_not_checked", T, F, MAGIC, "\t(void)magic;"),
    ("ota_description_magic_other", T, F, "#define DESCRIPTION_MAGIC   0xABCD5432u", "#define DESCRIPTION_MAGIC   0xABCD5433u"),
    ("ota_description_one_byte_later", T, F, "#define DESCRIPTION_OFFSET  32", "#define DESCRIPTION_OFFSET  33"),
    ("ota_description_magic_byte_0_ignored", T, F, MAGIC, "\tif((magic ^ DESCRIPTION_MAGIC) & 0xFFFFFF00u) return OTA_CHECK_NO_DESCRIPTION;"),
    ("ota_description_magic_byte_1_ignored", T, F, MAGIC, "\tif((magic ^ DESCRIPTION_MAGIC) & 0xFFFF00FFu) return OTA_CHECK_NO_DESCRIPTION;"),
    ("ota_description_magic_byte_2_ignored", T, F, MAGIC, "\tif((magic ^ DESCRIPTION_MAGIC) & 0xFF00FFFFu) return OTA_CHECK_NO_DESCRIPTION;"),
    ("ota_description_magic_byte_3_ignored", T, F, MAGIC, "\tif((magic ^ DESCRIPTION_MAGIC) & 0x00FFFFFFu) return OTA_CHECK_NO_DESCRIPTION;"),
    ("ota_description_magic_high_byte_first", T, F,
     "\tmagic = (uint32_t)data[DESCRIPTION_OFFSET] | ((uint32_t)data[DESCRIPTION_OFFSET + 1] << 8) |\n"
     "\t        ((uint32_t)data[DESCRIPTION_OFFSET + 2] << 16) | ((uint32_t)data[DESCRIPTION_OFFSET + 3] << 24);",
     "\tmagic = (uint32_t)data[DESCRIPTION_OFFSET + 3] | ((uint32_t)data[DESCRIPTION_OFFSET + 2] << 8) |\n"
     "\t        ((uint32_t)data[DESCRIPTION_OFFSET + 1] << 16) | ((uint32_t)data[DESCRIPTION_OFFSET] << 24);"),
    ("ota_version_without_zero_accepted", T, F, ZEROS, "\tif(memchr(project, '\\0', TEXT_SIZE) == NULL)"),
    ("ota_project_without_zero_accepted", T, F, ZEROS, "\tif(memchr(image_version, '\\0', TEXT_SIZE) == NULL)"),
    ("ota_project_zero_searched_behind_its_field", T, F,
     ZEROS, "\tif(memchr(image_version, '\\0', TEXT_SIZE) == NULL || memchr(project, '\\0', length - PROJECT_OFFSET) == NULL)"),
    ("ota_texts_without_zero_accepted_unless_both", T, F,
     ZEROS, "\tif(memchr(image_version, '\\0', TEXT_SIZE) == NULL && memchr(project, '\\0', TEXT_SIZE) == NULL)"),
    ("ota_text_of_31_bytes_refused", T, F, "#define TEXT_SIZE           32", "#define TEXT_SIZE           31"),
    ("ota_text_zero_searched_behind_the_field", T, F, "#define TEXT_SIZE           32", "#define TEXT_SIZE           33"),
    ("ota_text_without_zero_is_wrong_project", T, F,
     ZEROS + "\n\t{\n\t\treturn OTA_CHECK_NO_DESCRIPTION;", ZEROS + "\n\t{\n\t\treturn OTA_CHECK_WRONG_PROJECT;"),
    ("ota_version_one_byte_later", T, F, "#define VERSION_OFFSET      (DESCRIPTION_OFFSET + 16)", "#define VERSION_OFFSET      (DESCRIPTION_OFFSET + 17)"),
    ("ota_version_one_byte_earlier", T, F, "#define VERSION_OFFSET      (DESCRIPTION_OFFSET + 16)", "#define VERSION_OFFSET      (DESCRIPTION_OFFSET + 15)"),

    # project
    ("ota_project_not_checked", T, F, PROJECT, "\tif(0)\n\t{"),
    ("ota_project_beginning_accepted", T, F, PROJECT, "\tif(strncmp(project, OTA_PROJECT_NAME, strlen(project)) != 0)\n\t{"),
    ("ota_project_with_more_behind_accepted", T, F, PROJECT, "\tif(strncmp(project, OTA_PROJECT_NAME, strlen(OTA_PROJECT_NAME)) != 0)\n\t{"),
    ("ota_project_name_other", T, H, '#define OTA_PROJECT_NAME    "wican-display"', '#define OTA_PROJECT_NAME    "wican-fw_obd_v300_421"'),
    ("ota_project_one_byte_later", T, F, "#define PROJECT_OFFSET      (DESCRIPTION_OFFSET + 48)", "#define PROJECT_OFFSET      (DESCRIPTION_OFFSET + 49)"),
    ("ota_project_one_byte_earlier", T, F, "#define PROJECT_OFFSET      (DESCRIPTION_OFFSET + 48)", "#define PROJECT_OFFSET      (DESCRIPTION_OFFSET + 47)"),

    # size of the file
    ("ota_size_not_checked", T, F, "\telse if(file_size == 0 || file_size > slot_size)", "\telse if(0 && (file_size == 0 || file_size > slot_size))"),
    ("ota_file_as_large_as_slot_refused", T, F, "\telse if(file_size == 0 || file_size > slot_size)", "\telse if(file_size == 0 || file_size >= slot_size)"),
    ("ota_file_one_byte_too_large_accepted", T, F, "\telse if(file_size == 0 || file_size > slot_size)", "\telse if(file_size == 0 || file_size > slot_size + 1)"),
    ("ota_empty_file_accepted", T, F, "\telse if(file_size == 0 || file_size > slot_size)", "\telse if(file_size > slot_size)"),
    ("ota_file_of_one_byte_refused", T, F, "\telse if(file_size == 0 || file_size > slot_size)", "\telse if(file_size <= 1 || file_size > slot_size)"),
    ("ota_larger_file_accepted", T, F, "\telse if(file_size == 0 || file_size > slot_size)", "\telse if(file_size == 0 || (0 && file_size > slot_size))"),
    ("ota_size_checked_only_with_buffer", T, F,
     "\telse if(file_size == 0 || file_size > slot_size)", "\telse if((file_size == 0 || file_size > slot_size) && version != NULL)"),
    ("ota_sizes_compared_with_sign", T, F,
     "\telse if(file_size == 0 || file_size > slot_size)", "\telse if(file_size == 0 || (int32_t)file_size > (int32_t)slot_size)"),

    # no check depends on a byte or a parameter that is not its own
    ("ota_image_magic_skipped_without_segments", T, F, "\tif(data[0] != IMAGE_MAGIC)", "\tif(data[0] != IMAGE_MAGIC && data[1] != 0)"),
    ("ota_chip_skipped_without_hash", T, F, CHIP, CHIP.replace("!= OTA_CHIP_ESP32S3)", "!= OTA_CHIP_ESP32S3 && data[23] != 0)")),
    ("ota_description_skipped_with_many_segments", T, F, MAGIC, "\tif(magic != DESCRIPTION_MAGIC && data[1] < 16) return OTA_CHECK_NO_DESCRIPTION;"),
    ("ota_description_skipped_without_secure_version", T, F,
     MAGIC, "\tif(magic != DESCRIPTION_MAGIC && (data[36] | data[37] | data[38] | data[39]) != 0) return OTA_CHECK_NO_DESCRIPTION;"),
    ("ota_project_not_checked_for_empty_version", T, F, PROJECT, "\tif(strcmp(project, OTA_PROJECT_NAME) != 0 && image_version[0] != '\\0')\n\t{"),
    ("ota_project_without_zero_accepted_for_empty_version", T, F,
     ZEROS, ZEROS.replace("memchr(project, '\\0', TEXT_SIZE) == NULL", "(memchr(project, '\\0', TEXT_SIZE) == NULL && image_version[0] != '\\0')")),
    ("ota_size_checked_only_with_112_bytes", T, F,
     "\telse if(file_size == 0 || file_size > slot_size)", "\telse if((file_size == 0 || file_size > slot_size) && length == OTA_CHECK_BYTES)"),
    ("ota_size_not_checked_for_empty_version", T, F,
     "\telse if(file_size == 0 || file_size > slot_size)", "\telse if((file_size == 0 || file_size > slot_size) && image_version[0] != '\\0')"),
    ("ota_size_not_checked_for_short_version", T, F,
     "\telse if(file_size == 0 || file_size > slot_size)", "\telse if((file_size == 0 || file_size > slot_size) && strlen(image_version) > 3)"),
    ("ota_version_not_emptied_in_buffer_of_5", T, F,
     "\tif(version != NULL) version[0] = '\\0';", "\tif(version != NULL && version_size != 5) version[0] = '\\0';"),
    ("ota_version_of_30_bytes_lost", T, F,
     "\tif(version != NULL) copy_cut(image_version, version, version_size);",
     "\tif(version != NULL && strlen(image_version) != 30) copy_cut(image_version, version, version_size);"),

    # order of the checks
    ("ota_order_size_before_length", T, F, SHORT, "\tif(file_size == 0 || file_size > slot_size) return OTA_CHECK_TOO_LARGE;\n" + SHORT),
    ("ota_order_image_before_length", T, F, SHORT + IMAGE, IMAGE + SHORT),
    ("ota_order_chip_before_image", T, F, IMAGE + CHIP, CHIP + IMAGE),
    ("ota_order_size_before_chip", T, F, CHIP, "\tif(file_size == 0 || file_size > slot_size) return OTA_CHECK_TOO_LARGE;\n" + CHIP),
    ("ota_order_size_before_description", T, F, MAGIC, "\tif(file_size == 0 || file_size > slot_size) return OTA_CHECK_TOO_LARGE;\n" + MAGIC),
    ("ota_order_size_before_text_zeros", T, F, ZEROS, "\tif(file_size == 0 || file_size > slot_size) return OTA_CHECK_TOO_LARGE;\n" + ZEROS),
    ("ota_order_size_before_project", T, F,
     PROJECT, "\tif(file_size == 0 || file_size > slot_size)\n\t{\n\t\treturn OTA_CHECK_TOO_LARGE;\n\t}\n" + PROJECT),
    ("ota_order_description_before_chip", T, F,
     CHIP, "\tif(data[DESCRIPTION_OFFSET] != (DESCRIPTION_MAGIC & 0xFF)) return OTA_CHECK_NO_DESCRIPTION;\n" + CHIP),

    # version
    ("ota_version_not_emptied", T, F, "\tif(version != NULL) version[0] = '\\0';\n", ""),
    ("ota_version_zero_size_writes", T, F, "\tif(version_size == 0) version = NULL;\n", "\tif(version_size == 0) version_size = 1;\n"),
    ("ota_version_without_buffer_refused", T, F, "\tif(version_size == 0) version = NULL;\n", "\tif(version_size == 0) return OTA_CHECK_TOO_SHORT;\n"),
    ("ota_version_copied_when_too_large", T, F, LARGE, "\telse if(file_size == 0 || file_size > slot_size)\n\t{\n\t\tresult = OTA_CHECK_TOO_LARGE;\n\t}\n"),
    ("ota_version_not_copied_for_wrong_project", T, F, "\t\tresult = OTA_CHECK_WRONG_PROJECT;", "\t\treturn OTA_CHECK_WRONG_PROJECT;"),
    ("ota_version_not_copied", T, F, "\tif(version != NULL) copy_cut(", "\tif(0 && version != NULL) copy_cut("),
    ("ota_version_is_project_name", T, F, "copy_cut(image_version, version, version_size);", "copy_cut(project, version, version_size);"),
    ("ota_wrong_project_reported_as_ok", T, F, "\tif(version != NULL) copy_cut(image_version, version, version_size);\n\treturn result;", "\tif(version != NULL) copy_cut(image_version, version, version_size);\n\treturn result == OTA_CHECK_WRONG_PROJECT ? OTA_CHECK_OK : result;"),

    # cutting the version
    ("ota_cut_overflows_by_one", T, F, "\tif(length > size - 1)\n", "\tif(length > size)\n"),
    ("ota_cut_one_byte_too_many", T, F, "\t\tlength = size - 1;\n", "\t\tlength = size - 2;\n"),
    ("ota_cut_not_cut", T, F, "\t\tlength = size - 1;\n", "\t\tlength = size;\n"),
    ("ota_cut_in_a_character", T, F, CUT + "\n", ""),
    ("ota_cut_in_a_character_in_buffers_from_20", T, F, CUT, CUT.replace("length > 0", "length > 0 && size < 20")),
    ("ota_cut_only_one_byte_back", T, F, CUT, "\t\tif(length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80) length--;"),
    ("ota_cut_three_bytes_back_at_most", T, F,
     CUT, "\t\tfor(int back = 0; back < 3 && length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80; back++) length--;"),
    ("ota_cut_looks_at_last_byte_kept", T, F, CUT, "\t\twhile(length > 0 && ((unsigned char)text[length - 1] & 0xC0) == 0x80) length--;"),
    ("ota_cut_every_high_byte_continues", T, F, CUT, "\t\twhile(length > 0 && ((unsigned char)text[length] & 0x80) == 0x80) length--;"),
    ("ota_cut_first_bytes_continue", T, F, CUT, "\t\twhile(length > 0 && ((unsigned char)text[length] & 0xC0) == 0xC0) length--;"),
    ("ota_cut_upper_continuation_bytes_missed", T, F, CUT, "\t\twhile(length > 0 && ((unsigned char)text[length] & 0xE0) == 0x80) length--;"),
    ("ota_cut_text_not_copied", T, F, "\tmemcpy(out, text, length);\n", ""),
    ("ota_cut_not_terminated", T, F, "\tmemcpy(out, text, length);\n\tout[length] = '\\0';\n", "\tmemcpy(out, text, length);\n"),
]
