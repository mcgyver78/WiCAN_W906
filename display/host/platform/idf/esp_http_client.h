/*
 * Stand-in for components/esp_http_client/include/esp_http_client.h of ESP-IDF v5.5.2: the events with
 * their numbers, the event as it is handed to the handler, and of the configuration the members
 * display/main/net.c sets, with `url`, which it must not set (net_sim.c looks at that). What the client DOES
 * with a request is the model in net_sim.c.
 */
#ifndef __SIM_ESP_HTTP_CLIENT_H__
#define __SIM_ESP_HTTP_CLIENT_H__

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct esp_http_client *esp_http_client_handle_t;

typedef enum
{
	HTTP_EVENT_ERROR = 0,
	HTTP_EVENT_ON_CONNECTED,
	HTTP_EVENT_HEADERS_SENT,
	HTTP_EVENT_HEADER_SENT = HTTP_EVENT_HEADERS_SENT,
	HTTP_EVENT_ON_HEADER,
	HTTP_EVENT_ON_DATA,
	HTTP_EVENT_ON_FINISH,
	HTTP_EVENT_DISCONNECTED,
	HTTP_EVENT_REDIRECT,
} esp_http_client_event_id_t;

typedef struct esp_http_client_event
{
	esp_http_client_event_id_t event_id;
	esp_http_client_handle_t client;
	void *data;
	int data_len;
	void *user_data;
	char *header_key;
	char *header_value;
} esp_http_client_event_t;

typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_t *evt);

typedef enum
{
	HTTP_METHOD_GET = 0,
	HTTP_METHOD_POST,
} esp_http_client_method_t;

typedef struct
{
	const char *url;
	const char *host;
	const char *path;
	int timeout_ms;
	bool disable_auto_redirect;
	int max_authorization_retries;
	http_event_handle_cb event_handler;
} esp_http_client_config_t;

typedef enum
{
	HttpStatus_Unauthorized = 401,
} HttpStatus_Code;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char *url);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client, int timeout_ms);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
esp_err_t esp_http_client_close(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);

#endif
