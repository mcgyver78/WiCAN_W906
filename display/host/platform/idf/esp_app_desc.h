/*
 * Stand-in for components/esp_app_format/include/esp_app_desc.h of ESP-IDF v5.5.2: the description every
 * firmware carries, with all members of that file in their order (256 bytes).
 */
#ifndef __SIM_ESP_APP_DESC_H__
#define __SIM_ESP_APP_DESC_H__

#include <stdint.h>

typedef struct
{
	uint32_t magic_word;
	uint32_t secure_version;
	uint32_t reserv1[2];
	char version[32];
	char project_name[32];
	char time[16];
	char date[16];
	char idf_ver[32];
	uint8_t app_elf_sha256[32];
	uint16_t min_efuse_blk_rev_full;
	uint16_t max_efuse_blk_rev_full;
	uint8_t mmu_page_size;
	uint8_t reserv3[3];
	uint32_t reserv2[18];
} esp_app_desc_t;

const esp_app_desc_t *esp_app_get_description(void);

#endif
