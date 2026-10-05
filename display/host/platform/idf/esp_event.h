/*
 * Stand-in for components/esp_event/include/esp_event.h and esp_event_base.h of ESP-IDF v5.5.2: the
 * default event loop as far as display/main/net.c registers with it. The simulation is the loop: it keeps
 * the handler and calls it.
 */
#ifndef __SIM_ESP_EVENT_H__
#define __SIM_ESP_EVENT_H__

#include <stdint.h>
#include "esp_err.h"

typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id,
                                    void *event_data);

#define ESP_EVENT_DECLARE_BASE(id)  extern esp_event_base_t const id
#define ESP_EVENT_DEFINE_BASE(id)   esp_event_base_t const id = #id
#define ESP_EVENT_ANY_ID            -1

esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_register(esp_event_base_t event_base, int32_t event_id,
                                     esp_event_handler_t event_handler, void *event_handler_arg);

#endif
