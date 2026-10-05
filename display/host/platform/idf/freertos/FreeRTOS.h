/*
 * Stand-in for FreeRTOS as ESP-IDF v5.5.2 has it (components/freertos): the types of the port to the
 * ESP32-S3 (FreeRTOS-Kernel/portable/xtensa/include/freertos/portmacro.h) and its xPortGetCoreID(), the
 * constants of projdefs.h and the two numbers FreeRTOSConfig.h takes from sdkconfig. The simulations are
 * the scheduler: each runs the tasks of one file of the platform and decides itself what happens while a
 * task waits.
 */
#ifndef __SIM_FREERTOS_H__
#define __SIM_FREERTOS_H__

#include <stdint.h>
#include <stddef.h>
#include "sdkconfig.h"

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;

#define pdFALSE                 ((BaseType_t)0)
#define pdTRUE                  ((BaseType_t)1)
#define pdPASS                  (pdTRUE)
// What a task that could not be created for lack of memory is answered with
#define errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY   (-1)
#define portMAX_DELAY           ((TickType_t)0xffffffffUL)

#define configTICK_RATE_HZ      CONFIG_FREERTOS_HZ
#define configNUMBER_OF_CORES   CONFIG_FREERTOS_NUMBER_OF_CORES

#define pdMS_TO_TICKS(xTimeInMs) \
	((TickType_t)(((TickType_t)(xTimeInMs) * (TickType_t)configTICK_RATE_HZ) / (TickType_t)1000U))

// The core the calling task runs on
BaseType_t xPortGetCoreID(void);

#endif
