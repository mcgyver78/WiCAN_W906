/*
 * Stand-in for components/esp_lcd/include/esp_lcd_panel_ops.h of ESP-IDF v5.5.2: the one call
 * display/main/screen.c makes on the panel. x_start and y_start are the first pixel of the window, x_end and
 * y_end the first one behind it: they are NOT included, as that file says. For the RGB panel of the board
 * the call copies the pixels into the frame buffer and returns (esp_lcd_panel_rgb.c).
 */
#ifndef __SIM_ESP_LCD_PANEL_OPS_H__
#define __SIM_ESP_LCD_PANEL_OPS_H__

#include "esp_err.h"
#include "esp_lcd_types.h"

esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x_start, int y_start, int x_end, int y_end,
                                    const void *color_data);

#endif
