/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __BOARD_PINS_H__
#define __BOARD_PINS_H__

/*
 * Every pin, address, frequency and timing of the board, each with the source it comes from. Only board.c
 * includes this file.
 *
 * Nothing here was measured. The sources, all in the repository of the maker
 * (Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen, commit faf8ecf):
 *   [sch]     Eagle_SCH&PCB/ESP32_Display_2.1(K)_Main_V1.0, as an earlier reading of it recorded it
 *   [sketch]  factory_soucecode/RotaryScreen_2_1/RotaryScreen_2_1.ino, the firmware the board ships with
 *   [yaml]    example/esphome/Lesson02 to Lesson05; Lesson05/21-touch.yaml is the latest
 * and the libraries these two use:
 *   [gfx]     Arduino_GFX v1.6.7: Arduino_RGB_Display.h, Arduino_ESP32RGBPanel.cpp
 *   [esphome] ESPHome 2026.7.4: components st7701s, cst816, pcf8574, i2c
 *   [cst8xx]  Adafruit_CST8XX_Library 1.1.1
 * What has to be looked at on the board is marked CHECK in board.c.
 */

/* ------------------------------------------------------------------ I2C */

#define BOARD_I2C_SDA                   38      // [sch] [sketch] [yaml]
#define BOARD_I2C_SCL                   39      // [sch] [sketch] [yaml]
// [sketch] calls Wire.begin() without a frequency, which is 100 kHz in the Arduino core 3.3.8 it is built
// with; [yaml] names none either, which is 50 kHz in [esphome]. [sch] has pull-ups of 10 k: no source runs
// this bus at 400 kHz
#define BOARD_I2C_HZ                    100000
// Glitches shorter than this many clock cycles of the I2C unit are ignored. 7 is the value i2c_master.h of
// ESP-IDF calls typical, and the one the Arduino core of [sketch] sets
#define BOARD_I2C_GLITCH_CNT            7
// Longest wait for one transfer. The longest one here, five bytes from the touch controller, takes less than
// 1 ms at 100 kHz; the time is for a bus that is held by something else
#define BOARD_I2C_TIMEOUT_MS            50

/* -------------------------------------------------- port expander PCF8574 */

#define BOARD_EXPANDER_ADDR             0x21    // [sch] A0 high, A1 and A2 low; [sketch] [yaml]
#define BOARD_EXP_TOUCH_RESET           (1 << 0)    // P0, output [sch] [sketch] [yaml]
#define BOARD_EXP_LCD_POWER             (1 << 3)    // P3, output [sch] [sketch] [yaml]
#define BOARD_EXP_LCD_RESET             (1 << 4)    // P4, output [sch] [sketch] [yaml]
#define BOARD_EXP_SWITCH                (1 << 5)    // P5, input [sch] [sketch]
// P2 is the interrupt line of the touch controller and P1, P6, P7 are not connected [sch]: none of them is
// ever driven
#define BOARD_EXP_OUTPUTS               (BOARD_EXP_TOUCH_RESET | BOARD_EXP_LCD_POWER | BOARD_EXP_LCD_RESET)

// Level of P3 that powers the panel. [sch]: P3 drives the gate of a P-MOSFET between 3.3 V and VDD_LCD,
// low = on. [sketch] holds P3 high during its panel init and ends with it low; [yaml] holds it high
#define BOARD_LCD_POWER_ON              0
// [sketch] waits this long after it has written P3
#define BOARD_LCD_POWER_SETTLE_MS       100
// Reset of the panel: P4 low for 120 ms, then 120 ms until the first command [sketch]. [yaml] takes 100 ms
// for each
#define BOARD_LCD_RESET_LOW_MS          120
#define BOARD_LCD_RESET_WAIT_MS         120
// Reset of the touch controller: P0 low for 120 ms, then 120 ms [sketch]. The driver of [esphome] takes 5 ms
// and 30 ms
#define BOARD_TOUCH_RESET_LOW_MS        120
#define BOARD_TOUCH_RESET_WAIT_MS       120

/* ------------------------------------- panel, commands (3-wire SPI, 9 bit) */

#define BOARD_PANEL_SPI_CS              16      // [sch] [sketch] [yaml]
#define BOARD_PANEL_SPI_SCK             2       // [sch] [sketch] [yaml]
#define BOARD_PANEL_SPI_SDA             1       // [sch] [sketch] [yaml]

// Wait after "sleep out" and after "display on". [gfx] and [yaml] wait 100 ms and 50 ms; the driver waits
// in ticks and may end up to one tick early, so the first one is the 120 ms the default sequence of
// esp_lcd_st7701 has there
#define BOARD_PANEL_SLEEP_OUT_MS        120
#define BOARD_PANEL_DISPLAY_ON_MS       50

// Order of red and blue in the panel, the bit BGR of the command 36h. See the CHECK at the init sequence
#define BOARD_PANEL_ELE_ORDER           LCD_RGB_ELEMENT_ORDER_RGB
// What the command 3Ah is given: 18 bit (60h) in [gfx] and in [yaml], although the bus has 16 lines. The
// lowest bit of red and of blue is not connected at the panel [sch]
#define BOARD_PANEL_COLMOD_BITS         18

/* -------------------------------------------- panel, picture (RGB565 bus) */

#define BOARD_PANEL_DE                  40      // [sch] [sketch] [yaml]
#define BOARD_PANEL_VSYNC               7       // [sch] [sketch] [yaml]
#define BOARD_PANEL_HSYNC               15      // [sch] [sketch] [yaml]
#define BOARD_PANEL_PCLK                41      // [sch] [sketch] [yaml]
// From bit 0 of a pixel upwards, a pixel being RGB565 in the byte order of the chip: this is the order
// [gfx] builds for "native endian", and the one [esphome] ends up with after it has swapped the two bytes
// of its big-endian pixels. GPIO 3, 45 and 46 are strapping pins; they are only driven after the start
#define BOARD_PANEL_DATA_PINS \
	5, 45, 48, 47, 21,          /* blue,  panel B1 to B5  [sch] [sketch] [yaml] */ \
	14, 13, 12, 11, 10, 9,      /* green, panel G0 to G5  [sch] [sketch] [yaml] */ \
	46, 3, 8, 18, 17            /* red,   panel R1 to R5  [sch] [sketch] [yaml] */

// The timing of [sketch], the firmware every board is shipped with: [gfx] hands these numbers to
// esp_lcd_new_rgb_panel() unchanged, so they are a set for the driver used here. The set of [yaml]
// (18 MHz, falling edge, 20 / 10 / 10 and 8 / 10 / 10, bounce buffer of 10 lines) is the alternative: it
// is only known to work together with the sdkconfig of the maker's ESPHome lessons (code and constants
// run from the PSRAM, 240 MHz), and a community build that used it without them got a sheared picture.
// See the CHECK at the RGB panel
#define BOARD_PANEL_PCLK_HZ             12000000
#define BOARD_PANEL_PCLK_ACTIVE_NEG     0       // data changes on the rising edge
#define BOARD_PANEL_HSYNC_FRONT         10
#define BOARD_PANEL_HSYNC_PULSE         4
#define BOARD_PANEL_HSYNC_BACK          20
#define BOARD_PANEL_VSYNC_FRONT         10
#define BOARD_PANEL_VSYNC_PULSE         4
#define BOARD_PANEL_VSYNC_BACK          20
// Lines in each of the two bounce buffers in the internal RAM, as in [sketch]: 2 * 480 * 20 * 2 bytes =
// 38.4 KB. The frame buffer (480 * 480 * 2 bytes) has to be a multiple of it
#define BOARD_PANEL_BOUNCE_LINES        20
// Frame buffers in the PSRAM, 460800 bytes each. One, as [esphome] and [gfx] have it. Two are needed to
// let LVGL draw straight into them ("avoid tearing" of esp_lvgl_port)
#define BOARD_PANEL_FRAME_BUFFERS       1

/* ------------------------------------------------------------ backlight */

#define BOARD_BACKLIGHT_GPIO            6       // [sch] through 1 k to an NPN transistor, high = on; [sketch] [yaml]
#define BOARD_BACKLIGHT_HZ              5000    // [sketch] pwmFreq. [yaml] takes 19531 Hz
// [sketch] pwmResolution: 8 bit, and 255 as its brightest value. The two belong together
#define BOARD_BACKLIGHT_RESOLUTION      LEDC_TIMER_8_BIT
#define BOARD_BACKLIGHT_DUTY_FULL       255

/* -------------------------------------------------------------- encoder */

#define BOARD_ENCODER_A                 42      // [sch] [sketch] [yaml]; pull-up of 10 k on the board
#define BOARD_ENCODER_B                 4       // [sch] [sketch] [yaml]; pull-up of 10 k on the board
// Pulses shorter than this are ignored. The filter of the counter ends at 1023 cycles of the 80 MHz clock
// (12.7 us), so it takes spikes away and not the bouncing of a contact; the example of ESP-IDF has 1 us
#define BOARD_ENCODER_GLITCH_NS         10000
// Where the hardware counter starts again from 0, in both directions; the driver adds the lost counts up.
// Its documentation asks for limits as far apart as possible; the counter has 16 bit
#define BOARD_ENCODER_LIMIT             32000

/* ------------------------------------------------------ touch controller */

#define BOARD_TOUCH_ADDR                0x15    // [sch] [sketch] [yaml]
// Register layout as [cst8xx] and [esphome] read it; both drivers agree
#define BOARD_TOUCH_REG_POINTS          0x02    // number of fingers, followed by
                                                // 03h x high (low 4 bits), 04h x low, 05h y high, 06h y low
#define BOARD_TOUCH_REG_CHIP_ID         0xAA    // [cst8xx] CST8XX_REG_CHIPTYPE, [esphome] REG_FACTORY_ID
#define BOARD_TOUCH_ID_CST826           0x11    // the only id [cst8xx] accepts; CST826 in [esphome]
#define BOARD_TOUCH_MAX_POINTS          5       // [cst8xx]: a count above this is no finger
// [sketch] takes 20 off every y it reads; [yaml] takes nothing off. See the CHECK at board_touch()
#define BOARD_TOUCH_Y_OFFSET            20

/* ----------------------------------------------------------- temperature */

// The range the sensor of the chip measures best (error below 1 C, table of ESP-IDF). The driver moves on
// to another range by itself when a reading leaves it, up to 125 C
#define BOARD_TEMPERATURE_MIN           -10
#define BOARD_TEMPERATURE_MAX           80

#endif
