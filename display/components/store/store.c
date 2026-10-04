/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define TAG "store"

static const char *space_name(store_space_t space)
{
	return space == STORE_CFG ? "cfg" : "data";
}

esp_err_t store_init(void)
{
	esp_err_t err = nvs_flash_init();

	if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_LOGW(TAG, "NVS cannot be read (%s): erased, starting with defaults", esp_err_to_name(err));
		err = nvs_flash_erase();
		if(err == ESP_OK)
		{
			err = nvs_flash_init();
		}
	}
	return err;
}

int store_read(store_space_t space, const char *key, void *out, size_t size)
{
	nvs_handle_t handle;
	size_t length = 0;
	int result = -1;

	if(key == NULL || out == NULL || nvs_open(space_name(space), NVS_READONLY, &handle) != ESP_OK)
	{
		return -1;
	}
	// The first call only asks for the length, so that a value that does not fit leaves `out` untouched
	if(nvs_get_blob(handle, key, NULL, &length) == ESP_OK && length <= size &&
	   nvs_get_blob(handle, key, out, &length) == ESP_OK)
	{
		result = (int)length;
	}
	nvs_close(handle);
	return result;
}

int store_read_text(store_space_t space, const char *key, char *out, size_t size)
{
	int length;

	if(out == NULL || size == 0)
	{
		return -1;
	}
	length = store_read(space, key, out, size - 1);
	out[length < 0 ? 0 : length] = '\0';
	return length;
}

bool store_write(store_space_t space, const char *key, const void *data, size_t length)
{
	nvs_handle_t handle;
	bool ok;

	if(key == NULL || data == NULL || nvs_open(space_name(space), NVS_READWRITE, &handle) != ESP_OK)
	{
		return false;
	}
	ok = nvs_set_blob(handle, key, data, length) == ESP_OK && nvs_commit(handle) == ESP_OK;
	nvs_close(handle);
	if(!ok)
	{
		ESP_LOGE(TAG, "writing %s/%s (%u bytes) failed", space_name(space), key, (unsigned)length);
	}
	return ok;
}

bool store_erase(store_space_t space, const char *key)
{
	nvs_handle_t handle;
	esp_err_t err;

	if(key == NULL || nvs_open(space_name(space), NVS_READWRITE, &handle) != ESP_OK)
	{
		return false;
	}
	err = nvs_erase_key(handle, key);
	if(err == ESP_OK)
	{
		err = nvs_commit(handle);
	}
	nvs_close(handle);
	return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

bool store_erase_space(store_space_t space)
{
	nvs_handle_t handle;
	bool ok;

	if(nvs_open(space_name(space), NVS_READWRITE, &handle) != ESP_OK)
	{
		return false;
	}
	ok = nvs_erase_all(handle) == ESP_OK && nvs_commit(handle) == ESP_OK;
	nvs_close(handle);
	return ok;
}
