/*
 * Stand-in for components/esp_common/include/esp_err.h of ESP-IDF v5.5.2: the type and the codes
 * display/main uses, with the numbers of that file.
 */
#ifndef __SIM_ESP_ERR_H__
#define __SIM_ESP_ERR_H__

typedef int esp_err_t;

#define ESP_OK                  0
#define ESP_FAIL                -1
#define ESP_ERR_NO_MEM          0x101
#define ESP_ERR_INVALID_ARG     0x102
#define ESP_ERR_INVALID_STATE   0x103
#define ESP_ERR_NOT_FOUND       0x105
#define ESP_ERR_WIFI_BASE       0x3000

const char *esp_err_to_name(esp_err_t code);

#endif
