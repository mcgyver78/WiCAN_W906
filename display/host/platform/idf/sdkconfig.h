/*
 * Stand-in for the sdkconfig.h the build of the firmware makes (ESP-IDF v5.5.2, from the Kconfig files of
 * its components and display/sdkconfig.defaults). Only what display/main and the stand-ins next to this
 * file ask for; see display/host/platform/Makefile.
 */
#ifndef __SIM_SDKCONFIG_H__
#define __SIM_SDKCONFIG_H__

// display/sdkconfig.defaults
#define CONFIG_FREERTOS_HZ                  100
#define CONFIG_ESP_TASK_WDT_PANIC           1
#define CONFIG_LV_USE_BUILTIN_MALLOC        1
#define CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES    512
// components/esp_system/Kconfig: the task watchdog is started with the system unless sdkconfig says no
#define CONFIG_ESP_TASK_WDT_INIT            1
// components/freertos/Kconfig: two cores unless CONFIG_FREERTOS_UNICORE, which the display does not set
#define CONFIG_FREERTOS_NUMBER_OF_CORES     2
// components/esp_http_server/Kconfig: the defaults
#define CONFIG_HTTPD_MAX_REQ_HDR_LEN        1024
#define CONFIG_HTTPD_MAX_URI_LEN            512

#endif
