/*
 * Stand-in for components/bootloader_support/include/bootloader_random.h of ESP-IDF v5.5.2: the entropy
 * source that makes esp_random() a true one while the WiFi is off.
 */
#ifndef __SIM_BOOTLOADER_RANDOM_H__
#define __SIM_BOOTLOADER_RANDOM_H__

void bootloader_random_enable(void);
void bootloader_random_disable(void);

#endif
