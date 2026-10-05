/*
 * Stand-in for components/heap/include/esp_heap_caps.h of ESP-IDF v5.5.2: the capabilities display/main
 * names, with the bits of that file, and the four functions it calls.
 */
#ifndef __SIM_ESP_HEAP_CAPS_H__
#define __SIM_ESP_HEAP_CAPS_H__

#include <stddef.h>
#include <stdint.h>

#define MALLOC_CAP_8BIT         (1 << 2)
#define MALLOC_CAP_SPIRAM       (1 << 10)
#define MALLOC_CAP_INTERNAL     (1 << 11)

void *heap_caps_malloc(size_t size, uint32_t caps);
void *heap_caps_calloc(size_t n, size_t size, uint32_t caps);
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);

#endif
