 /* This file is part of the WiCAN project.
 *
 * Copyright (C) 2022  Meatpi Electronics.
 * Written by Ali Slim <ali@meatpi.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef GW_CONFIG_h
#define GW_CONFIG_h

#include "esp_err.h"

#if HARDWARE_VER == WICAN_PRO
#define FS_MOUNT_POINT              "/fatfs"
#define TX_GPIO_NUM             	2
#define RX_GPIO_NUM             	1
#define CAN_STDBY_GPIO_NUM			38

#define SDCARD_CLK                  21                 
#define SDCARD_CMD                  47
#define SDCARD_D0                   14
#define SDCARD_D1                   13
#define SDCARD_D2                   12
#define SDCARD_D3                   48

#define OBD_RESET_PIN           (GPIO_NUM_41)
#define OBD_LED_EN_PIN          (GPIO_NUM_42)
#define OBD_READY_PIN           (GPIO_NUM_7)    // High = Active, Low = Sleep
// #define CONNECTED_LED_GPIO_NUM		41  //NC pin
// #define ACTIVE_LED_GPIO_NUM			41  //NC pin
// #define BLE_EN_PIN_NUM				42  //NC pin
// #define PWR_LED_GPIO_NUM			41  //NC pin

#define BUTTON_GPIO_NUM			    8
#define IMU_INT_GPIO_NUM			3

#elif HARDWARE_VER == WICAN_DIY_ESP32
// Self-built board: ESP32-WROOM-32 + SN65HVD230 (or TJA1051T), see DIY_ESP32.md.
// Strapping pins (0, 2, 5, 12, 15), flash pins (6-11) and UART0 (1, 3) are avoided.
// At runtime this board behaves like a V300 (project name contains no "usb"/"hv210"),
// so the battery divider must match the V300 one: 100k / 16k.
#define FS_MOUNT_POINT              "/littlefs"
#define TX_GPIO_NUM             	21
#define RX_GPIO_NUM             	22
#define CAN_STDBY_GPIO_NUM			23  // SN65HVD230 Rs / TJA1051T S: high = standby/silent
#define CONNECTED_LED_GPIO_NUM		25
#define ACTIVE_LED_GPIO_NUM			26
#define PWR_LED_GPIO_NUM			27
#define VOLTAGE_ADC_CHANNEL			ADC_CHANNEL_6   // GPIO34, input only

#else

#define FS_MOUNT_POINT              "/littlefs"
#define TX_GPIO_NUM             	0
#define RX_GPIO_NUM             	3
#define CONNECTED_LED_GPIO_NUM		8
#define ACTIVE_LED_GPIO_NUM			9
#define BLE_EN_PIN_NUM				5
#define PWR_LED_GPIO_NUM			7
#define CAN_STDBY_GPIO_NUM			6

#endif

#ifndef VOLTAGE_ADC_CHANNEL
#define VOLTAGE_ADC_CHANNEL			ADC_CHANNEL_4
#endif

esp_err_t hw_config_get_device_id(char *uid);

#endif
