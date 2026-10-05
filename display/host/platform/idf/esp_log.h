/*
 * Stand-in for components/log/include/esp_log.h of ESP-IDF v5.5.2: the three macros display/main logs
 * with. A line goes to sim_log() of the simulation (sim.h), which prints it and can look for a text in it.
 * The format is checked against its arguments, as the compiler of the firmware does.
 */
#ifndef __SIM_ESP_LOG_H__
#define __SIM_ESP_LOG_H__

void sim_log(char level, const char *tag, const char *format, ...) __attribute__((format(printf, 3, 4)));

#define ESP_LOGE(tag, format, ...) do { sim_log('E', tag, format, ##__VA_ARGS__); } while(0)
#define ESP_LOGW(tag, format, ...) do { sim_log('W', tag, format, ##__VA_ARGS__); } while(0)
#define ESP_LOGI(tag, format, ...) do { sim_log('I', tag, format, ##__VA_ARGS__); } while(0)

#endif
