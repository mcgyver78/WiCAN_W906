/*
 * Stand-in for components/freertos/FreeRTOS-Kernel/include/freertos/semphr.h of ESP-IDF v5.5.2: a mutex is
 * a queue there, and the three macros display/main/main.c uses are those of that file. See FreeRTOS.h.
 */
#ifndef __SIM_FREERTOS_SEMPHR_H__
#define __SIM_FREERTOS_SEMPHR_H__

#include "freertos/queue.h"

typedef QueueHandle_t SemaphoreHandle_t;

#define semGIVE_BLOCK_TIME      ((TickType_t)0U)

#define xSemaphoreCreateMutex()                 xQueueCreateMutex(queueQUEUE_TYPE_MUTEX)
#define xSemaphoreTake(xSemaphore, xBlockTime)  xQueueSemaphoreTake((xSemaphore), (xBlockTime))
#define xSemaphoreGive(xSemaphore) \
	xQueueGenericSend((QueueHandle_t)(xSemaphore), NULL, semGIVE_BLOCK_TIME, queueSEND_TO_BACK)

#endif
