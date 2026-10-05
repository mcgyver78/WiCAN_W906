/*
 * Stand-in for components/freertos/FreeRTOS-Kernel/include/freertos/task.h of ESP-IDF v5.5.2, and for
 * xTaskCreatePinnedToCore() of esp_additions/include/freertos/idf_additions.h. See FreeRTOS.h. The two
 * macros of the task notifications are those of that file, the enumeration has its members in their order.
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
BaseType_t xTaskDelayUntil(TickType_t *const pxPreviousWakeTime, const TickType_t xTimeIncrement);
TickType_t xTaskGetTickCount(void);

#define tskDEFAULT_INDEX_TO_NOTIFY  (0)

typedef enum
{
	eNoAction = 0,
	eSetBits,
	eIncrement,
	eSetValueWithOverwrite,
	eSetValueWithoutOverwrite,
} eNotifyAction;

BaseType_t xTaskGenericNotify(TaskHandle_t xTaskToNotify, UBaseType_t uxIndexToNotify, uint32_t ulValue,
                              eNotifyAction eAction, uint32_t *pulPreviousNotificationValue);
uint32_t ulTaskGenericNotifyTake(UBaseType_t uxIndexToWaitOn, BaseType_t xClearCountOnExit, TickType_t xTicksToWait);

#define xTaskNotifyGive(xTaskToNotify) \
	xTaskGenericNotify((xTaskToNotify), (tskDEFAULT_INDEX_TO_NOTIFY), (0), eIncrement, NULL)
#define ulTaskNotifyTake(xClearCountOnExit, xTicksToWait) \
	ulTaskGenericNotifyTake((tskDEFAULT_INDEX_TO_NOTIFY), (xClearCountOnExit), (xTicksToWait))

#endif
