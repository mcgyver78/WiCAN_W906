/*
 * Stand-in for components/esp_timer/include/esp_timer.h of ESP-IDF v5.5.2: the clock, microseconds since
 * the start.
 */
#ifndef __SIM_ESP_TIMER_H__
#define __SIM_ESP_TIMER_H__

#include <stdint.h>

int64_t esp_timer_get_time(void);

#endif
