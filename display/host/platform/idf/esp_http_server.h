/*
 * Stand-in for components/esp_http_server/include/esp_http_server.h of ESP-IDF v5.5.2, with the methods of
 * components/http_parser/http_parser.h it hands on (their numbers are those of that file). A request has
 * the members display/main/web.c reads, the configuration those it sets and those web_sim.c looks at. What
 * the server DOES - how it reads a header, a length, a body - is the model in web_sim.c.
 */
#ifndef __SIM_ESP_HTTP_SERVER_H__
#define __SIM_ESP_HTTP_SERVER_H__

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include "sdkconfig.h"
#include "esp_err.h"

// http_parser.h: the first seven of HTTP_METHOD_MAP
enum http_method
{
	HTTP_DELETE     = 0,
	HTTP_GET        = 1,
	HTTP_HEAD       = 2,
	HTTP_POST       = 3,
	HTTP_PUT        = 4,
	HTTP_CONNECT    = 5,
	HTTP_OPTIONS    = 6,
};

#define ESP_ERR_HTTPD_BASE          (0xb000)
#define ESP_ERR_HTTPD_RESULT_TRUNC  (ESP_ERR_HTTPD_BASE + 4)
#define ESP_ERR_HTTPD_RESP_HDR      (ESP_ERR_HTTPD_BASE + 5)

typedef void *httpd_handle_t;
typedef enum http_method httpd_method_t;

#define HTTP_ANY INT_MAX

typedef esp_err_t (*httpd_open_func_t)(httpd_handle_t hd, int sockfd);
typedef bool (*httpd_uri_match_func_t)(const char *reference_uri, const char *uri_to_match, size_t match_upto);

typedef struct httpd_config
{
	size_t stack_size;
	size_t max_req_hdr_len;
	uint16_t max_open_sockets;
	uint16_t max_uri_handlers;
	uint16_t max_resp_headers;
	bool lru_purge_enable;
	uint16_t recv_wait_timeout;
	httpd_open_func_t open_fn;
	httpd_uri_match_func_t uri_match_fn;
} httpd_config_t;

#define HTTPD_DEFAULT_CONFIG() { \
		.stack_size         = 4096, \
		.max_req_hdr_len    = CONFIG_HTTPD_MAX_REQ_HDR_LEN, \
		.max_open_sockets   = 7, \
		.max_uri_handlers   = 8, \
		.max_resp_headers   = 8, \
		.lru_purge_enable   = false, \
		.recv_wait_timeout  = 5, \
		.open_fn = NULL, \
		.uri_match_fn = NULL \
	}

// content_len is a size_t there as well: 32 bit on the device, where the 64 bit the parser counted are cut
// (httpd_parse.c). web_sim.c fills it as the device would.
typedef struct httpd_req
{
	httpd_handle_t handle;
	int method;
	const char uri[CONFIG_HTTPD_MAX_URI_LEN + 1];
	size_t content_len;
} httpd_req_t;

typedef struct httpd_uri
{
	const char *uri;
	httpd_method_t method;
	esp_err_t (*handler)(httpd_req_t *r);
	void *user_ctx;
} httpd_uri_t;

typedef enum
{
	HTTPD_500_INTERNAL_SERVER_ERROR = 0,
	HTTPD_501_METHOD_NOT_IMPLEMENTED,
	HTTPD_505_VERSION_NOT_SUPPORTED,
	HTTPD_400_BAD_REQUEST,
	HTTPD_401_UNAUTHORIZED,
	HTTPD_403_FORBIDDEN,
	HTTPD_404_NOT_FOUND,
	HTTPD_405_METHOD_NOT_ALLOWED,
	HTTPD_408_REQ_TIMEOUT,
	HTTPD_411_LENGTH_REQUIRED,
	HTTPD_413_CONTENT_TOO_LARGE,
	HTTPD_414_URI_TOO_LONG,
	HTTPD_431_REQ_HDR_FIELDS_TOO_LARGE,
	HTTPD_ERR_CODE_MAX
} httpd_err_code_t;

typedef esp_err_t (*httpd_err_handler_func_t)(httpd_req_t *req, httpd_err_code_t error);

#define HTTPD_SOCK_ERR_TIMEOUT  -3

esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config);
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler);
esp_err_t httpd_register_err_handler(httpd_handle_t handle, httpd_err_code_t error,
                                     httpd_err_handler_func_t handler_fn);
bool httpd_uri_match_wildcard(const char *uri_template, const char *uri_to_match, size_t match_upto);
int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *field, char *val, size_t val_size);
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t buf_len);

#endif
