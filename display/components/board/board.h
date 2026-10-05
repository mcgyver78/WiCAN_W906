/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __BOARD_H__
#define __BOARD_H__

#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_types.h"

/*
 * The hardware of the Elecrow CrowPanel 2.1 inch HMI ESP32 Rotary Display (ESP32-S3R8, 16 MB flash, 8 MB
 * octal PSRAM, round 480x480 IPS panel with ST7701S, capacitive touch, rotary knob with press switch).
 *
 * Everything in this component is READ from the schematic of the maker (Main V1.0) and from the maker's
 * examples. Nothing was measured: the board was not there when this was written. Places that can only be
 * decided with the board in hand are marked CHECK in board.c.
 *
 * Wiring (schematic):
 *   I2C            SDA 38, SCL 39; touch controller at 0x15, port expander PCF8574 at 0x21
 *   expander       P0 touch reset, P2 touch interrupt (input), P3 LCD power, P4 LCD reset,
 *                  P5 switch of the knob (input, pressed = low); P1, P6, P7 unused.
 *                  The interrupt output of the expander is not connected: switch and touch are polled.
 *   panel init     3-wire SPI, 9 bit: CS 16, SCK 2, SDA 1
 *   panel data     RGB565 parallel: DE 40, VSYNC 7, HSYNC 15, PCLK 41,
 *                  blue 5, 45, 48, 47, 21; green 14, 13, 12, 11, 10, 9; red 46, 3, 8, 18, 17
 *                  (the names of the schematic; which colour of a pixel is sent on which: board_pins.h)
 *   backlight      GPIO 6, PWM, high = on
 *   encoder        A 42, B 4, pull-ups on the board, no debounce capacitors
 *   console        native USB (USB serial JTAG); GPIO 43 (UART0 TX) drives a LED of the board
 */

#define BOARD_WIDTH     480
#define BOARD_HEIGHT    480

// I2C bus, port expander with all outputs in their idle state, encoder counter, backlight (dark),
// temperature sensor. Has to succeed before anything else of this component is used. An expander that
// does not answer is an error: without it the panel stays dark and the switch cannot be read.
esp_err_t board_init(void);

// Powers and resets the panel through the expander, sends the init sequence of the ST7701S and starts the
// RGB panel (frame buffer in the PSRAM, bounce buffer in the internal RAM). The backlight stays dark.
esp_err_t board_panel_start(esp_lcd_panel_handle_t *panel);

// 0 = dark, 100 = full. Values outside are clamped.
void board_backlight(int percent);

// One reading of the switch of the knob. Returns false if the reading failed (*pressed is then untouched).
bool board_button(bool *pressed);

// Counts of the encoder since the last call, positive or negative (see knob.h for what a detent is)
int board_encoder(void);

// One reading of the touch controller. Returns false if the reading failed. *down tells whether a finger
// is on the screen; x and y (0 to 479, origin top left as the panel shows it) are only set if it is.
bool board_touch(bool *down, int *x, int *y);

// Temperature of the chip in degrees C. Returns false if it could not be read.
bool board_temperature(int *celsius);

#endif
