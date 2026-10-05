/*
 * Stand-in for components/freertos/FreeRTOS-Kernel/include/freertos/event_groups.h of ESP-IDF v5.5.2: the
 * bits display/main/net.c passes the events of the driver on with. See FreeRTOS.h.
 */
#ifndef __SIM_FREERTOS_EVENT_GROUPS_H__
#define __SIM_FREERTOS_EVENT_GROUPS_H__

#include "freertos/FreeRTOS.h"

typedef TickType_t EventBits_t;
typedef struct EventGroupDef_t *EventGroupHandle_t;

EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToWaitFor,
                                const BaseType_t xClearOnExit, const BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait);
EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear);
EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet);

#endif
