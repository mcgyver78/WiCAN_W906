/*
 * Stand-in for components/esp_hw_support/include/esp_mac.h of ESP-IDF v5.5.2: the one address
 * display/main/main.c reads. The other members of esp_mac_type_t are left out.
 */
#ifndef __SIM_ESP_MAC_H__
#define __SIM_ESP_MAC_H__

#include <stdint.h>
#include "esp_err.h"

typedef enum
{
	ESP_MAC_WIFI_STA,
} esp_mac_type_t;

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type);

#endif
