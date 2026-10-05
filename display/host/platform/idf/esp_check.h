/*
 * Stand-in for components/esp_common/include/esp_check.h of ESP-IDF v5.5.2: the two macros display/main
 * uses, as that file has them without CONFIG_COMPILER_OPTIMIZATION_CHECKS_SILENT (the log line, then the
 * return).
 */
#ifndef __SIM_ESP_CHECK_H__
#define __SIM_ESP_CHECK_H__

#include "esp_err.h"
#include "esp_log.h"

#define ESP_RETURN_ON_ERROR(x, log_tag, format, ...) do { \
		esp_err_t err_rc_ = (x); \
		if(err_rc_ != ESP_OK) \
		{ \
			ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
			return err_rc_; \
		} \
	} while(0)

#define ESP_RETURN_ON_FALSE(a, err_code, log_tag, format, ...) do { \
		if(!(a)) \
		{ \
			ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
			return err_code; \
		} \
	} while(0)

#endif
