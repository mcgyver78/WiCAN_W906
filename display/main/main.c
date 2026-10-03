/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"

#define TAG "main"

void app_main(void)
{
	const esp_app_desc_t *app = esp_app_get_description();
	esp_ota_img_states_t state;

	ESP_LOGI(TAG, "%s %s", app->project_name, app->version);

	// Rollback is enabled from the first version on, so that the partition table and the boot flow
	// never have to change in the field. Until the self test and the confirmation on the device
	// exist, an image that reaches this point counts as good.
	if(esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
	   state == ESP_OTA_IMG_PENDING_VERIFY)
	{
		esp_ota_mark_app_valid_cancel_rollback();
	}
}
