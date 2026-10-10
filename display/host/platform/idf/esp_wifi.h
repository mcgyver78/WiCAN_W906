/*
 * Stand-in for the WiFi driver of ESP-IDF v5.5.2 as display/main/net.c uses it: components/esp_wifi/include/
 * esp_wifi.h (functions, ESP_ERR_WIFI_STATE, the configuration of esp_wifi_init()), esp_wifi_types_generic.h
 * (modes, events, configurations), local/esp_wifi_types_native.h (wifi_sta_list_t) and esp_wifi_default.h
 * (the two default interfaces). Enumerations have the numbers of those files. Of each structure only the
 * leading members net.c or net_sim.c touch are here, in their order; wifi_init_config_t has the one member
 * net.c sets, and its default as the Kconfig of the driver has it (CONFIG_ESP_WIFI_NVS_ENABLED=y).
 *
 * What the driver DOES is not in a header: net_sim.c is the model of that, and says what it rests on.
 */
#ifndef __SIM_ESP_WIFI_H__
#define __SIM_ESP_WIFI_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"

#define ESP_ERR_WIFI_STATE      (ESP_ERR_WIFI_BASE + 6)

typedef enum
{
	WIFI_MODE_NULL = 0,
	WIFI_MODE_STA,
	WIFI_MODE_AP,
	WIFI_MODE_APSTA,
} wifi_mode_t;

typedef enum
{
	WIFI_IF_STA = 0,
	WIFI_IF_AP,
} wifi_interface_t;

typedef enum
{
	WIFI_AUTH_OPEN = 0,
	WIFI_AUTH_WEP,
	WIFI_AUTH_WPA_PSK,
	WIFI_AUTH_WPA2_PSK,
} wifi_auth_mode_t;

typedef enum
{
	WIFI_STORAGE_FLASH,
	WIFI_STORAGE_RAM,
} wifi_storage_t;

// net.c passes NULL: all channels, the defaults of the driver
typedef struct wifi_scan_config_t wifi_scan_config_t;

typedef struct
{
	uint8_t bssid[6];
	uint8_t ssid[33];
	uint8_t primary;
	int second;
	int8_t rssi;
	wifi_auth_mode_t authmode;
} wifi_ap_record_t;

typedef struct
{
	uint8_t ssid[32];
	uint8_t password[64];
	uint8_t ssid_len;
	uint8_t channel;
	wifi_auth_mode_t authmode;
	uint8_t ssid_hidden;
	uint8_t max_connection;
} wifi_ap_config_t;

typedef struct
{
	uint8_t ssid[32];
	uint8_t password[64];
} wifi_sta_config_t;

typedef union
{
	wifi_ap_config_t ap;
	wifi_sta_config_t sta;
} wifi_config_t;

// Without the list itself: net.c counts the stations and looks at none
typedef struct wifi_sta_list_t
{
	int num;
} wifi_sta_list_t;

typedef enum
{
	WIFI_EVENT_SCAN_DONE            = 1,
	WIFI_EVENT_STA_DISCONNECTED     = 5,
	WIFI_EVENT_AP_STACONNECTED      = 14,
	WIFI_EVENT_AP_STADISCONNECTED   = 15,
} wifi_event_t;

ESP_EVENT_DECLARE_BASE(WIFI_EVENT);

typedef struct
{
	uint8_t ssid[32];
	uint8_t ssid_len;
	uint8_t bssid[6];
	uint8_t reason;
	int8_t rssi;
} wifi_event_sta_disconnected_t;

typedef struct
{
	int nvs_enable;
} wifi_init_config_t;

#define WIFI_INIT_CONFIG_DEFAULT() { .nvs_enable = 1 }

// esp_wifi_types_generic.h: the station sleeps between beacons unless it is told not to
typedef enum
{
	WIFI_PS_NONE,
	WIFI_PS_MIN_MODEM,
	WIFI_PS_MAX_MODEM,
} wifi_ps_type_t;

esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_storage(wifi_storage_t storage);
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type);

// esp_wifi_types_generic.h: what an interface speaks, as bits
#define WIFI_PROTOCOL_11B   0x1
#define WIFI_PROTOCOL_11G   0x2
#define WIFI_PROTOCOL_11N   0x4

esp_err_t esp_wifi_set_protocol(wifi_interface_t interface, uint8_t protocol_bitmap);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *ap_records);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *sta);
esp_err_t esp_wifi_sta_get_rssi(int *rssi);
esp_netif_t *esp_netif_create_default_wifi_ap(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);

#endif
