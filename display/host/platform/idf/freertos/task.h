/*
 * Stand-in for components/freertos/FreeRTOS-Kernel/include/freertos/task.h of ESP-IDF v5.5.2, and for
 * xTaskCreatePinnedToCore() of esp_additions/include/freertos/idf_additions.h. See FreeRTOS.h.
 */
#ifndef __SIM_FREERTOS_TASK_H__
#define __SIM_FREERTOS_TASK_H__

#include "freertos/FreeRTOS.h"

typedef struct tskTaskControlBlock *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode, const char *const pcName, const uint32_t usStackDepth,
                       void *const pvParameters, UBaseType_t uxPriority, TaskHandle_t *const pxCreatedTask);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pxTaskCode, const char *const pcName, const uint32_t ulStackDepth,
                                   void *const pvParameters, UBaseType_t uxPriority,
                                   TaskHandle_t *const pxCreatedTask, const BaseType_t xCoreID);
void vTaskDelay(const TickType_t xTicksToDelay);

#endif
