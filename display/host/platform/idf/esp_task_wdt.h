/*
 * Stand-in for components/esp_system/include/esp_task_wdt.h of ESP-IDF v5.5.2: the two calls
 * display/main/screen.c makes. esp_task_wdt_add(NULL) subscribes the task that calls it; it fails with
 * ESP_ERR_INVALID_STATE without a watchdog and with ESP_ERR_INVALID_ARG for a task that is subscribed
 * already. esp_task_wdt_reset() by a task that is not subscribed fails with ESP_ERR_NOT_FOUND
 * (esp_system/task_wdt/task_wdt.c).
 */
#ifndef __SIM_ESP_TASK_WDT_H__
#define __SIM_ESP_TASK_WDT_H__

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t esp_task_wdt_add(TaskHandle_t task_handle);
esp_err_t esp_task_wdt_reset(void);

#endif
