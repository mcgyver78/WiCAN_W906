/*
 * Stand-in for components/esp_netif/include/esp_netif.h, esp_netif_types.h and esp_netif_ip_addr.h of
 * ESP-IDF v5.5.2: addresses, the event of a new address, and the three functions display/main/net.c calls.
 * An address is four bytes in the order they are sent, as on the device.
 */
#ifndef __SIM_ESP_NETIF_H__
#define __SIM_ESP_NETIF_H__

#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"

struct esp_ip6_addr
{
	uint32_t addr[4];
	uint8_t zone;
};

struct esp_ip4_addr
{
	uint32_t addr;
};

typedef struct esp_ip4_addr esp_ip4_addr_t;
typedef struct esp_ip6_addr esp_ip6_addr_t;

typedef struct _ip_addr
{
	union
	{
		esp_ip6_addr_t ip6;
		esp_ip4_addr_t ip4;
	} u_addr;
	uint8_t type;
} esp_ip_addr_t;

#define ESP_IPADDR_TYPE_V4      0U

typedef struct esp_netif_obj esp_netif_t;

// The first of ip_event_t
typedef enum
{
	IP_EVENT_STA_GOT_IP,
} ip_event_t;

ESP_EVENT_DECLARE_BASE(IP_EVENT);

typedef struct
{
	esp_ip4_addr_t ip;
	esp_ip4_addr_t netmask;
	esp_ip4_addr_t gw;
} esp_netif_ip_info_t;

esp_err_t esp_netif_init(void);
esp_err_t esp_netif_get_ip_info(esp_netif_t *esp_netif, esp_netif_ip_info_t *ip_info);
char *esp_ip4addr_ntoa(const esp_ip4_addr_t *addr, char *buf, int buflen);

#endif
