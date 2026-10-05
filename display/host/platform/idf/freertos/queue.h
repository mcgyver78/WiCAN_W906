/*
 * Stand-in for components/freertos/FreeRTOS-Kernel/include/freertos/queue.h of ESP-IDF v5.5.2: the two
 * macros display/main/main.c uses and the functions they and the macros of semphr.h end in. See FreeRTOS.h.
 */
#ifndef __SIM_FREERTOS_QUEUE_H__
#define __SIM_FREERTOS_QUEUE_H__

#include "freertos/FreeRTOS.h"

typedef struct QueueDefinition *QueueHandle_t;

#define queueSEND_TO_BACK       ((BaseType_t)0)
#define queueQUEUE_TYPE_BASE    ((uint8_t)0U)
#define queueQUEUE_TYPE_MUTEX   ((uint8_t)1U)

QueueHandle_t xQueueGenericCreate(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize,
                                  const uint8_t ucQueueType);
QueueHandle_t xQueueCreateMutex(const uint8_t ucQueueType);
BaseType_t xQueueGenericSend(QueueHandle_t xQueue, const void *const pvItemToQueue, TickType_t xTicksToWait,
                             const BaseType_t xCopyPosition);
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *const pvBuffer, TickType_t xTicksToWait);
BaseType_t xQueueSemaphoreTake(QueueHandle_t xQueue, TickType_t xTicksToWait);

#define xQueueCreate(uxQueueLength, uxItemSize) \
	xQueueGenericCreate((uxQueueLength), (uxItemSize), (queueQUEUE_TYPE_BASE))
#define xQueueSend(xQueue, pvItemToQueue, xTicksToWait) \
	xQueueGenericSend((xQueue), (pvItemToQueue), (xTicksToWait), queueSEND_TO_BACK)

#endif
