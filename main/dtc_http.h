/*
 * This file is part of the WiCAN project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __DTC_HTTP_H__
#define __DTC_HTTP_H__

#include "esp_err.h"
#include "esp_http_server.h"

// Registers GET /api/state, POST /api/dtc and GET /api/dtc/result (tools/w906/API.md).
// device_id has to stay valid, the pointer is kept.
esp_err_t dtc_http_register(httpd_handle_t server, const char *device_id);

#endif
