/*
 * Stand-in for include/mdns.h of the component espressif/mdns 1.13.1 (repository espressif/esp-protocols,
 * tag mdns-v1.13.1): a result of a query as far as display/main/net.c reads it - the next result and the
 * list of addresses - and the four functions it calls. The other members of mdns_result_t are left out.
 */
#ifndef __SIM_MDNS_H__
#define __SIM_MDNS_H__

#include <stddef.h>
#include <stdint.h>
#include "esp_netif.h"

typedef struct mdns_ip_addr_s
{
	esp_ip_addr_t addr;
	struct mdns_ip_addr_s *next;
} mdns_ip_addr_t;

typedef struct mdns_result_s
{
	struct mdns_result_s *next;
	mdns_ip_addr_t *addr;
} mdns_result_t;

esp_err_t mdns_init(void);
esp_err_t mdns_hostname_set(const char *hostname);
esp_err_t mdns_query_ptr(const char *service_type, const char *proto, uint32_t timeout, size_t max_results,
                         mdns_result_t **results);
void mdns_query_results_free(mdns_result_t *results);

#endif
