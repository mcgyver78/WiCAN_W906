/*
 * This file is part of the WiCAN project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/*
 * HTTP API for standalone clients, see tools/w906/API.md. The decisions and all texts come from
 * dtc_api.c and dtc_state.c, which are tested on the host. This file only moves header fields,
 * the query and device values between the HTTP server and those functions. Nothing here waits
 * for the scan: the HTTP server is a single task.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "autopid.h"
#include "config_server.h"
#include "mqtt.h"
#include "sleep_mode.h"
#include "dtc_state.h"
#include "dtc_api.h"
#include "dtc_http.h"

#define TAG                     "dtc_http"

#define STATE_JSON_SIZE         768
#define DTC_JSON_SIZE           320
#define HOST_SIZE               64
#define QUERY_SIZE              64

static const char *api_device_id = "";
static uint32_t api_boot_id = 0;

static const char *status_text(int status)
{
	switch(status)
	{
		case 202: return "202 Accepted";
		case 400: return "400 Bad Request";
		case 403: return "403 Forbidden";
		case 409: return "409 Conflict";
		default:  return "503 Service Unavailable";
	}
}

static esp_err_t send_json(httpd_req_t *req, const char *status, const char *body)
{
	httpd_resp_set_status(req, status);
	httpd_resp_set_type(req, "application/json");
	httpd_resp_set_hdr(req, "Cache-Control", "no-store");
	return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_answer(httpd_req_t *req, int status, const char *reason, uint32_t seq)
{
	char body[DTC_API_BODY_SIZE];

	if(dtc_api_body(status, reason, seq, body, sizeof(body)) < 0) body[0] = '\0';
	return send_json(req, status_text(status), body);
}

// Value of a request header, NULL if it is absent or does not fit
static const char *request_header(httpd_req_t *req, const char *name, char *value, size_t size)
{
	size_t length = httpd_req_get_hdr_value_len(req, name);

	if(length == 0 || length >= size) return NULL;
	if(httpd_req_get_hdr_value_str(req, name, value, size) != ESP_OK) return NULL;
	return value;
}

static esp_err_t state_handler(httpd_req_t *req)
{
	// Not on the stack of the HTTP server task and not on the heap, the answer has to come even when
	// memory is short. The server is a single task, so the buffers are never used twice at a time.
	static char json[STATE_JSON_SIZE];
	static char dtc_json[DTC_JSON_SIZE];
	dtc_api_status_t status;
	float voltage = 0;

	if(autopid_dtc_state_json(dtc_json, DTC_JSON_SIZE) < 0) dtc_json[0] = '\0';

	status.id = api_device_id;
	status.fw = esp_app_get_description()->version;
	status.git = GIT_SHA;
	status.boot = api_boot_id;
	status.up_s = (uint32_t)(esp_timer_get_time() / 1000000);
	status.autopid = autopid_loop_state();
	status.pids = autopid_pid_count();
	status.ecu_online = autopid_get_ecu_status();
	status.pass = autopid_pass_counter();
	status.rx_age_ms = autopid_rx_age_ms();
	status.mqtt = config_server_mqtt_en_config() != 1 ? "off" : mqtt_connected() ? "connected" : "disconnected";
	status.batt_mv = sleep_mode_get_voltage(&voltage) == 1 && voltage >= 0 ? (int32_t)(voltage * 1000.0f) : -1;
	status.sleep_in_s = sleep_mode_seconds_to_sleep();
	status.heap = esp_get_free_heap_size();
	status.heap_min = esp_get_minimum_free_heap_size();

	if(dtc_api_state_json(&status, dtc_json, json, STATE_JSON_SIZE) < 0)
	{
		// Cannot happen with the sizes above; an empty object is still an answer
		return send_json(req, "200 OK", "{}");
	}
	return send_json(req, "200 OK", json);
}

static esp_err_t dtc_handler(httpd_req_t *req)
{
	char marker[8];
	char host[HOST_SIZE];
	char query[QUERY_SIZE];
	const char *query_text = NULL;
	dtc_api_request_t request;
	dtc_accept_t accepted;
	uint32_t seq = 0;
	bool ready;
	int status;

	if(httpd_req_get_url_query_len(req) > 0 && httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
	{
		query_text = query;
	}

	request = dtc_api_parse_request(request_header(req, "X-WiCAN-DTC", marker, sizeof(marker)),
	                                request_header(req, "Host", host, sizeof(host)), query_text);
	if(request.status != 0)
	{
		return send_answer(req, request.status, request.reason, 0);
	}

	ready = strcmp(autopid_loop_state(), "run") == 0;
	if(!ready)
	{
		return send_answer(req, dtc_api_status(false, DTC_ACCEPTED), "not_ready", 0);
	}

	accepted = autopid_dtc_request(request.clear, DTC_SRC_HTTP, request.seq, &seq);
	status = dtc_api_status(true, accepted);
	ESP_LOGI(TAG, "%s over HTTP: %d %s", request.clear ? "clear_dtc" : "read_dtc", status,
	         accepted == DTC_ACCEPTED ? "accepted" : dtc_accept_reason(accepted));
	return send_answer(req, status, dtc_accept_reason(accepted), seq);
}

static esp_err_t result_handler(httpd_req_t *req)
{
	char number[12];
	char *result = NULL;
	uint32_t seq = 0;
	esp_err_t sent;
	int found;

	if(strcmp(autopid_loop_state(), "run") != 0)
	{
		return send_answer(req, 503, "not_ready", 0);
	}

	found = autopid_dtc_result_dup(&result, &seq);
	if(found < 0)
	{
		return send_answer(req, 503, "not_ready", 0);
	}
	if(found == 0)
	{
		httpd_resp_set_status(req, "204 No Content");
		httpd_resp_set_hdr(req, "Cache-Control", "no-store");
		return httpd_resp_send(req, NULL, 0);
	}

	snprintf(number, sizeof(number), "%lu", (unsigned long)seq);
	httpd_resp_set_hdr(req, "X-DTC-Seq", number);
	sent = send_json(req, "200 OK", result);
	free(result);
	return sent;
}

esp_err_t dtc_http_register(httpd_handle_t server, const char *device_id)
{
	static const httpd_uri_t handlers[] = {
		{.uri = "/api/state",      .method = HTTP_GET,  .handler = state_handler},
		{.uri = "/api/dtc",        .method = HTTP_POST, .handler = dtc_handler},
		{.uri = "/api/dtc/result", .method = HTTP_GET,  .handler = result_handler},
	};

	if(device_id != NULL) api_device_id = device_id;
	if(api_boot_id == 0)
	{
		// Lets a client tell a restart of the adapter from a lost connection
		api_boot_id = esp_random() & DTC_SEQ_MAX;
		if(api_boot_id == 0) api_boot_id = 1;
	}

	for(size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++)
	{
		esp_err_t result = httpd_register_uri_handler(server, &handlers[i]);

		if(result != ESP_OK && result != ESP_ERR_HTTPD_HANDLER_EXISTS)
		{
			ESP_LOGE(TAG, "Failed to register %s: %s", handlers[i].uri, esp_err_to_name(result));
			return result;
		}
	}
	return ESP_OK;
}
