/*
 * Stand-in for components/esp_system/include/esp_system.h of ESP-IDF v5.5.2, and for the one macro of
 * components/esp_common/include/esp_attr.h that display/main/main.c gets through it.
 */
#ifndef __SIM_ESP_SYSTEM_H__
#define __SIM_ESP_SYSTEM_H__

#include <stdint.h>
#include "esp_err.h"

// esp_attr.h puts such a variable into the section .rtc_noinit, which a start neither loads nor clears. On
// the PC it is a variable like any other: main_sim.c keeps it over its starts and spoils it where the
// power was lost.
#define RTC_NOINIT_ATTR

// All sixteen, in the order of esp_system.h: main.c turns a reason into a word and into a kind of reset
typedef enum
{
	ESP_RST_UNKNOWN,
	ESP_RST_POWERON,
	ESP_RST_EXT,
	ESP_RST_SW,
	ESP_RST_PANIC,
	ESP_RST_INT_WDT,
	ESP_RST_TASK_WDT,
	ESP_RST_WDT,
	ESP_RST_DEEPSLEEP,
	ESP_RST_BROWNOUT,
	ESP_RST_SDIO,
	ESP_RST_USB,
	ESP_RST_JTAG,
	ESP_RST_EFUSE,
	ESP_RST_PWR_GLITCH,
	ESP_RST_CPU_LOCKUP,
} esp_reset_reason_t;

void esp_restart(void) __attribute__((__noreturn__));
esp_reset_reason_t esp_reset_reason(void);

#endif
