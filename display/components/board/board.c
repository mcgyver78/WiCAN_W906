/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "board.h"
#include "board_pins.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "driver/temperature_sensor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_st7701.h"

/*
 * Written without the board: every call was read in the sources of ESP-IDF v5.5.2 and of the components
 * pinned in idf_component.yml, and everything about the board in the sources board_pins.h names. What only
 * the board can decide is marked CHECK, with what to look at and what to change.
 *
 * Tasks: board_init() and board_panel_start() are called once, one after the other, when the display
 * starts. Everything after that belongs to the task that owns the screen. The I2C driver serialises its
 * transfers by itself; nothing else here has a lock. One thing runs beside that task: the timer that asks
 * the panel driver for its restart (panel_restart_start()), from the task of esp_timer.
 */

#define TAG "board"

// The panel is driven through bounce buffers (see panel_create()), and that mode leans on two settings of
// display/sdkconfig.defaults: one that has to be set and one that must not be. With either of them wrong
// the picture shifts some day and nothing tells why.
#if !CONFIG_ESP32S3_DATA_CACHE_LINE_64B
#error "sdkconfig: CONFIG_ESP32S3_DATA_CACHE_LINE_64B is needed, see display/sdkconfig.defaults"
#endif
#if CONFIG_LCD_RGB_RESTART_IN_VSYNC
#error "sdkconfig: CONFIG_LCD_RGB_RESTART_IN_VSYNC leaves the picture shifted with ESP-IDF v5.5.2, see display/sdkconfig.defaults"
#endif
// The driver fills the two bounce buffers in turn and starts every frame with the first (panel_create())
_Static_assert(BOARD_HEIGHT % (2 * BOARD_PANEL_BOUNCE_LINES) == 0,
               "a frame has to be an even number of bounce buffers, see BOARD_PANEL_BOUNCE_LINES");

// Level of the outputs of the expander while nothing is going on: both resets released, panel without power
#define EXPANDER_IDLE   (BOARD_EXP_TOUCH_RESET | BOARD_EXP_LCD_RESET | (BOARD_LCD_POWER_ON ? 0 : BOARD_EXP_LCD_POWER))

static bool init_called;
static i2c_master_dev_handle_t expander_dev;
static i2c_master_dev_handle_t touch_dev;
static uint8_t expander_written = 0xFF;     // what the expander holds: all pins high after its power-on
static pcnt_unit_handle_t encoder_unit;
static int encoder_seen;                    // count at the last board_encoder()
static temperature_sensor_handle_t tsens;
static esp_lcd_panel_handle_t panel_started;

/*
 * Init sequence of the ST7701S for this panel: st7701_type5_init_operations of Arduino_GFX v1.6.7
 * (src/display/Arduino_RGB_Display.h), the one the firmware of the maker names. Lesson04 of the maker's
 * ESPHome examples sends the same bytes after a software reset; Lesson02 and 03 leave the three C0h to C2h
 * of page 0 out, Lesson05 has two bytes more in E2h.
 *
 * Not in this table: 36h (order of red and blue) and 3Ah (pixel format). The driver sends both by itself
 * ahead of the table, from BOARD_PANEL_ELE_ORDER and BOARD_PANEL_COLMOD_BITS, and warns about a table that
 * sends them again. The software reset is sent by esp_lcd_panel_reset().
 *
 * CHECK: red, green and blue areas show in their colour. What leaves the chip is what the firmware of the
 * maker sends: 36h = 08h (BGR), and the red of a pixel on the lines the schematic calls blue (that
 * firmware exchanges red and blue of every pixel in software before it sends it; here the pins are named
 * in that order instead, BOARD_PANEL_DATA_PINS). The ESPHome lessons of the maker have both the other way
 * round, red on the lines called red and 36h = 00h (written by ESPHome after the sequence, with command
 * set 2 selected). That is the same picture only if the ST7701S obeys the bit BGR while it is fed through
 * the RGB bus, and no source says that it does; the first of those lessons carries the note "colors are
 * off". If red and blue are exchanged: BOARD_PANEL_ELE_ORDER to LCD_RGB_ELEMENT_ORDER_RGB. If that
 * changes nothing, the panel does not obey the bit: put it back, and exchange the two groups of five in
 * BOARD_PANEL_DATA_PINS.
 * CHECK: grey steps are even and nothing is washed out or inverted. If not, this is not the sequence of
 * the panel that is built in: compare with the other st7701_type*_init_operations of Arduino_GFX.
 */
#define PANEL_CMD(command, ...) \
	{ (command), (const uint8_t []){ __VA_ARGS__ }, sizeof((const uint8_t []){ __VA_ARGS__ }), 0 }

static const st7701_lcd_init_cmd_t panel_init_cmds[] =
{
	PANEL_CMD(0xFF, 0x77, 0x01, 0x00, 0x00, 0x10),      // command set 2, page 0
	PANEL_CMD(0xC0, 0x3B, 0x00),
	PANEL_CMD(0xC1, 0x0B, 0x02),
	PANEL_CMD(0xC2, 0x00, 0x02),
	PANEL_CMD(0xCC, 0x10),
	PANEL_CMD(0xCD, 0x08),
	PANEL_CMD(0xB0, 0x02, 0x13, 0x1B, 0x0D, 0x10, 0x05, 0x08, 0x07, 0x07, 0x24, 0x04, 0x11, 0x0E, 0x2C, 0x33, 0x1D),
	PANEL_CMD(0xB1, 0x05, 0x13, 0x1B, 0x0D, 0x11, 0x05, 0x08, 0x07, 0x07, 0x24, 0x04, 0x11, 0x0E, 0x2C, 0x33, 0x1D),
	PANEL_CMD(0xFF, 0x77, 0x01, 0x00, 0x00, 0x11),      // page 1
	PANEL_CMD(0xB0, 0x5D),
	PANEL_CMD(0xB1, 0x43),
	PANEL_CMD(0xB2, 0x81),
	PANEL_CMD(0xB3, 0x80),
	PANEL_CMD(0xB5, 0x43),
	PANEL_CMD(0xB7, 0x85),
	PANEL_CMD(0xB8, 0x20),
	PANEL_CMD(0xC1, 0x78),
	PANEL_CMD(0xC2, 0x78),
	PANEL_CMD(0xD0, 0x88),
	PANEL_CMD(0xE0, 0x00, 0x00, 0x02),
	PANEL_CMD(0xE1, 0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20),
	PANEL_CMD(0xE2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	PANEL_CMD(0xE3, 0x00, 0x00, 0x11, 0x00),
	PANEL_CMD(0xE4, 0x22, 0x00),
	PANEL_CMD(0xE5, 0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	PANEL_CMD(0xE6, 0x00, 0x00, 0x11, 0x00),
	PANEL_CMD(0xE7, 0x22, 0x00),
	PANEL_CMD(0xE8, 0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	PANEL_CMD(0xEB, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00),
	PANEL_CMD(0xED, 0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF, 0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF),
	PANEL_CMD(0xEF, 0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F),
	PANEL_CMD(0xFF, 0x77, 0x01, 0x00, 0x00, 0x13),      // page 3
	PANEL_CMD(0xEF, 0x08),
	PANEL_CMD(0xFF, 0x77, 0x01, 0x00, 0x00, 0x00),      // command set 2 off
	{ 0x11, NULL, 0, BOARD_PANEL_SLEEP_OUT_MS },        // sleep out
	{ 0x29, NULL, 0, BOARD_PANEL_DISPLAY_ON_MS },       // display on
};

// Waits at least `ms`: a delay of FreeRTOS ends with a tick, and the first tick may be about to end
static void wait_ms(uint32_t ms)
{
	vTaskDelay(pdMS_TO_TICKS(ms) + 1);
}

/*
 * Writes the outputs of the expander. A PCF8574 has no direction register: a pin is an input for as long
 * as a 1 is written to it, and a 0 drives it low. So every write carries a 1 for every pin that is not an
 * output of this board. A 0 on P5 would hold the switch of the knob "pressed" for good, and the switch
 * confirms the clearing of the fault memory (hold.h).
 */
static esp_err_t expander_write(uint8_t outputs)
{
	uint8_t byte = (uint8_t)(outputs | ~BOARD_EXP_OUTPUTS);
	esp_err_t err = i2c_master_transmit(expander_dev, &byte, 1, BOARD_I2C_TIMEOUT_MS);

	if(err == ESP_OK)
	{
		expander_written = byte;
	}
	return err;
}

static esp_err_t expander_set(uint8_t pin, bool high)
{
	return expander_write(high ? (expander_written | pin) : (expander_written & ~pin));
}

/*
 * Resets what hangs on one output of the expander.
 * CHECK: both resets are active low (schematic, firmware and ESPHome lessons of the maker agree). A panel
 * that stays dark although its power is right, or a touch controller that never answers, may be held in
 * its reset: then the level is the other way round for that pin.
 */
static esp_err_t expander_reset_pulse(uint8_t pin, uint32_t low_ms, uint32_t after_ms)
{
	ESP_RETURN_ON_ERROR(expander_set(pin, false), TAG, "port expander: reset low");
	wait_ms(low_ms);
	ESP_RETURN_ON_ERROR(expander_set(pin, true), TAG, "port expander: reset high");
	wait_ms(after_ms);
	return ESP_OK;
}

/*
 * CHECK: the backlight is dark after board_init() and gets brighter with a higher percentage. If it is
 * the other way round, flags.output_invert of the channel has to be set.
 * CHECK: at 1 % it still glows evenly, and nothing flickers or whistles at any step. If it does:
 * BOARD_BACKLIGHT_HZ (the ESPHome lessons of the maker take 19531 Hz, the firmware 5000 Hz).
 */
static esp_err_t backlight_init(void)
{
	const ledc_timer_config_t timer_config =
	{
		.speed_mode = LEDC_LOW_SPEED_MODE,
		.duty_resolution = BOARD_BACKLIGHT_RESOLUTION,
		.timer_num = LEDC_TIMER_0,
		.freq_hz = BOARD_BACKLIGHT_HZ,
		.clk_cfg = LEDC_AUTO_CLK,
	};
	const ledc_channel_config_t channel_config =
	{
		.gpio_num = BOARD_BACKLIGHT_GPIO,
		.speed_mode = LEDC_LOW_SPEED_MODE,
		.channel = LEDC_CHANNEL_0,
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = LEDC_TIMER_0,
		.duty = 0,
	};

	ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "backlight: timer");
	ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG, "backlight: channel");
	return ESP_OK;
}

/*
 * The bus as the firmware of the maker has it (Arduino core 3.3.8, the same driver below): 100 kHz, the
 * pull-ups of the chip on in addition to the 10 k of the board.
 * CHECK: board_init() gets past this, so the expander answers at 0x21. If it does not, a scan of the bus
 * (i2c_master_probe() over all addresses) tells whether the pins or the address are wrong.
 * CHECK: no errors of the tag "i2c.master" in the log while the knob is used and the picture runs. If
 * there are: BOARD_I2C_HZ down to 50000, the value the ESPHome lessons of the maker run with.
 */
static esp_err_t bus_init(void)
{
	const i2c_master_bus_config_t bus_config =
	{
		.i2c_port = -1,
		.sda_io_num = BOARD_I2C_SDA,
		.scl_io_num = BOARD_I2C_SCL,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = BOARD_I2C_GLITCH_CNT,
		.flags.enable_internal_pullup = 1,
	};
	const i2c_device_config_t expander_config =
	{
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = BOARD_EXPANDER_ADDR,
		.scl_speed_hz = BOARD_I2C_HZ,
	};
	const i2c_device_config_t touch_config =
	{
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = BOARD_TOUCH_ADDR,
		.scl_speed_hz = BOARD_I2C_HZ,
	};
	i2c_master_bus_handle_t bus;

	ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &bus), TAG, "I2C bus");
	// Asked before anything is written: without the expander the panel has neither power nor reset, and
	// nothing else would report it
	ESP_RETURN_ON_ERROR(i2c_master_probe(bus, BOARD_EXPANDER_ADDR, BOARD_I2C_TIMEOUT_MS), TAG,
	                    "port expander does not answer at 0x%02x", BOARD_EXPANDER_ADDR);
	ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &expander_config, &expander_dev), TAG, "port expander");
	ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &touch_config, &touch_dev), TAG, "touch controller");
	return ESP_OK;
}

/*
 * A touch controller that does not answer is no error of board_init(): everything can be done with the
 * knob alone. It is reported in the log, and board_touch() fails from then on.
 * CHECK: the log names the id 0x11 (CST826, the only one the driver of the maker's firmware accepts). With
 * another id the registers board_touch() reads may mean something else: compare with the data sheet of
 * that controller.
 * CHECK: if the log says here that the controller does not answer, and touches work all the same once the
 * panel runs: the controller is fed through the switch on P3, which is still off at this point. Then
 * this reset has to move into board_panel_start(), behind the power.
 */
static esp_err_t touch_reset(void)
{
	const uint8_t reg = BOARD_TOUCH_REG_CHIP_ID;
	uint8_t id;

	ESP_RETURN_ON_ERROR(expander_reset_pulse(BOARD_EXP_TOUCH_RESET, BOARD_TOUCH_RESET_LOW_MS, BOARD_TOUCH_RESET_WAIT_MS),
	                    TAG, "touch controller: reset");
	if(i2c_master_transmit_receive(touch_dev, &reg, 1, &id, 1, BOARD_I2C_TIMEOUT_MS) != ESP_OK)
	{
		ESP_LOGW(TAG, "touch controller does not answer at 0x%02x after its reset", BOARD_TOUCH_ADDR);
	}
	else if(id != BOARD_TOUCH_ID_CST826)
	{
		ESP_LOGW(TAG, "touch controller has the id 0x%02x, not 0x%02x of the CST826", id, BOARD_TOUCH_ID_CST826);
	}
	else
	{
		ESP_LOGI(TAG, "touch controller CST826, id 0x%02x", id);
	}
	return ESP_OK;
}

/*
 * Every edge of both channels counts, the other channel gives the direction: four counts for one period
 * of the encoder. A contact that bounces counts forwards and backwards by the same amount, so the bouncing
 * cancels itself as long as the other channel rests; the board has no capacitors that would smooth it.
 *
 * The hardware counter starts again from 0 at either limit. With accum_count and a watch point on each
 * limit the driver adds what is lost there in its interrupt, and pcnt_unit_get_count() returns the sum.
 *
 * CHECK: one detent is KNOB_COUNTS_PER_DETENT (4, knob.h) counts: log the sum of board_encoder() over ten
 * detents, which should be 40. With 20, the encoder has a detent on every half period and the constant in
 * knob.h is 2.
 * CHECK: turning clockwise counts up. If it counts down, that is the setting "reverse" of knob.h; nothing
 * changes here. The sources disagree about it.
 * CHECK: the sum returns to its start when the knob is turned ten detents forth and ten back, slowly and
 * fast. If it drifts, counts are lost or made up: look at both channels with an oscilloscope before
 * changing BOARD_ENCODER_GLITCH_NS.
 */
static esp_err_t encoder_init(void)
{
	const pcnt_unit_config_t unit_config =
	{
		.low_limit = -BOARD_ENCODER_LIMIT,
		.high_limit = BOARD_ENCODER_LIMIT,
		.flags.accum_count = 1,
	};
	const pcnt_glitch_filter_config_t filter_config =
	{
		.max_glitch_ns = BOARD_ENCODER_GLITCH_NS,
	};
	const pcnt_chan_config_t a_config =
	{
		.edge_gpio_num = BOARD_ENCODER_A,
		.level_gpio_num = BOARD_ENCODER_B,
	};
	const pcnt_chan_config_t b_config =
	{
		.edge_gpio_num = BOARD_ENCODER_B,
		.level_gpio_num = BOARD_ENCODER_A,
	};
	pcnt_channel_handle_t a;
	pcnt_channel_handle_t b;

	ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_config, &encoder_unit), TAG, "encoder: counter");
	ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(encoder_unit, &filter_config), TAG, "encoder: glitch filter");
	ESP_RETURN_ON_ERROR(pcnt_new_channel(encoder_unit, &a_config, &a), TAG, "encoder: channel A");
	ESP_RETURN_ON_ERROR(pcnt_new_channel(encoder_unit, &b_config, &b), TAG, "encoder: channel B");
	ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE),
	                    TAG, "encoder: edges of A");
	ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE),
	                    TAG, "encoder: level of B");
	ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE),
	                    TAG, "encoder: edges of B");
	ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE),
	                    TAG, "encoder: level of A");
	ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(encoder_unit, BOARD_ENCODER_LIMIT), TAG, "encoder: upper limit");
	ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(encoder_unit, -BOARD_ENCODER_LIMIT), TAG, "encoder: lower limit");
	ESP_RETURN_ON_ERROR(pcnt_unit_enable(encoder_unit), TAG, "encoder: enable");
	ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(encoder_unit), TAG, "encoder: clear");
	ESP_RETURN_ON_ERROR(pcnt_unit_start(encoder_unit), TAG, "encoder: start");
	return ESP_OK;
}

static esp_err_t temperature_init(void)
{
	const temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(BOARD_TEMPERATURE_MIN, BOARD_TEMPERATURE_MAX);

	ESP_RETURN_ON_ERROR(temperature_sensor_install(&config, &tsens), TAG, "temperature sensor");
	ESP_RETURN_ON_ERROR(temperature_sensor_enable(tsens), TAG, "temperature sensor: enable");
	return ESP_OK;
}

esp_err_t board_init(void)
{
	// What a failed attempt has taken (I2C unit, counter, pins) is not given back: only a restart helps
	ESP_RETURN_ON_FALSE(!init_called, ESP_ERR_INVALID_STATE, TAG, "board_init() called twice");
	init_called = true;

	// The backlight first, so that it is dark whatever fails after it
	ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");
	ESP_RETURN_ON_ERROR(bus_init(), TAG, "I2C");
	// After a restart of the chip alone the expander still holds what the firmware before left in it
	ESP_RETURN_ON_ERROR(expander_write(EXPANDER_IDLE), TAG, "port expander: idle state");
	ESP_RETURN_ON_ERROR(touch_reset(), TAG, "touch controller");
	ESP_RETURN_ON_ERROR(encoder_init(), TAG, "encoder");
	ESP_RETURN_ON_ERROR(temperature_init(), TAG, "temperature sensor");
	return ESP_OK;
}

/*
 * The RGB panel on top of the command bus. Nothing of it is left behind when it fails.
 *
 * The frame buffer lies in the PSRAM. The DMA does not read it there: an interrupt copies it, a few lines
 * at a time, into one of two bounce buffers in the internal RAM, and the DMA sends those. This is the mode
 * the maker's firmware and ESPHome drive this board in; the timing is the one of the maker's firmware
 * (board_pins.h). No event callback is registered, neither here nor in main/screen.c: they are free for
 * whoever wants to draw in step with the picture (esp_lcd_rgb_panel_register_event_callbacks() sets all
 * of them at once).
 *
 * The two interrupts of the panel run on the core of the task that calls this. Espressif advises to run
 * the LVGL task on the same core, so that two cores do not share the PSRAM while a line is copied.
 *
 * Which of the two bounce buffers is to be filled next, the driver knows by counting the interrupts of
 * the DMA. When some of them are lost - they were held up for longer than one buffer lasts, 0.86 ms with
 * this timing: the flash is written, interrupts are off for long - the count no longer fits the picture.
 * It is set right in the vertical blanking, together with a restart of the DMA at the first buffer:
 * ESP-IDF v5.5.2 does that when it has counted too few interrupts in a frame, and when it was asked to
 * (esp_lcd_rgb_panel_restart(); panel_restart_start() below asks all the time). With
 * CONFIG_LCD_RGB_RESTART_IN_VSYNC it restarts the DMA in every blanking but never sets the count right
 * (esp_lcd_panel_rgb.c, lcd_rgb_panel_try_restart_transmission(); ESP-IDF issue 19070, mended on master
 * only). After one lost interrupt every buffer is then filled while it is sent, and the picture stays
 * shifted. So that option is off, and this file does not compile with it.
 *
 * CHECK: the picture stands still, straight and whole, also with WiFi connected and traffic on it. A
 * picture that is sheared, shifted sideways or torn into bands is the timing or the bandwidth of the
 * PSRAM. Then try the set of the maker's ESPHome lessons: BOARD_PANEL_PCLK_HZ 18000000,
 * BOARD_PANEL_PCLK_ACTIVE_NEG 1, HSYNC front porch 20, pulse 10, back porch 10, VSYNC 8, 10, 10,
 * BOARD_PANEL_BOUNCE_LINES 10 - and with it what those lessons set: CONFIG_SPIRAM_FETCH_INSTRUCTIONS,
 * CONFIG_SPIRAM_RODATA.
 * CHECK: single pixels that sparkle, or edges with a coloured fringe: the panel takes the data on the
 * other edge of the pixel clock, flip BOARD_PANEL_PCLK_ACTIVE_NEG.
 * CHECK: the picture is upright and not mirrored. If it is not: esp_lcd_panel_mirror() or
 * esp_lcd_panel_swap_xy() on the panel at the end of this function (the RGB driver then turns every block
 * while it copies it into the frame buffer; the ST7701 driver passes the call on), and the same turn for
 * the coordinates in board_touch(). The rotation of the LVGL display does not turn the pixels.
 * CHECK: while the flash is written (a layout or the settings stored, a firmware update) the picture may
 * stop or show garbage, because the PSRAM cannot be read then. It has to be whole again within a moment,
 * every time: store a layout twenty times, upload a firmware, start the WiFi. A picture that stays
 * shifted up or down by BOARD_PANEL_BOUNCE_LINES lines, flickering at the left edge of every such band,
 * is the count described above: see whether the timer of panel_restart_start() runs. The cross-check, to
 * be seen once: with CONFIG_LCD_RGB_RESTART_IN_VSYNC=y and its #error taken out the shift should stay
 * after some of these writes (expected from the code of the driver, never seen here).
 * CHECK: moving content does not tear. If it does, BOARD_PANEL_FRAME_BUFFERS 2 changes nothing by itself:
 * main/screen.c would have to let LVGL draw straight into the two frame buffers
 * (esp_lcd_rgb_panel_get_frame_buffer()) and hand the finished one to esp_lcd_panel_draw_bitmap(), which
 * then shows it from the next frame on instead of copying.
 * CHECK: heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) after an hour with WiFi, the web page and
 * an upload. The two bounce buffers take 38.4 KB of the internal RAM, twice what the design counted
 * with. If it gets tight: the set of the ESPHome lessons above has buffers of half the size.
 */
static esp_err_t panel_create(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t *panel)
{
	const esp_lcd_rgb_panel_config_t rgb_config =
	{
		.clk_src = LCD_CLK_SRC_PLL160M,
		.timings =
		{
			.pclk_hz = BOARD_PANEL_PCLK_HZ,
			.h_res = BOARD_WIDTH,
			.v_res = BOARD_HEIGHT,
			.hsync_pulse_width = BOARD_PANEL_HSYNC_PULSE,
			.hsync_back_porch = BOARD_PANEL_HSYNC_BACK,
			.hsync_front_porch = BOARD_PANEL_HSYNC_FRONT,
			.vsync_pulse_width = BOARD_PANEL_VSYNC_PULSE,
			.vsync_back_porch = BOARD_PANEL_VSYNC_BACK,
			.vsync_front_porch = BOARD_PANEL_VSYNC_FRONT,
			.flags.pclk_active_neg = BOARD_PANEL_PCLK_ACTIVE_NEG,
		},
		.data_width = 16,
		.num_fbs = BOARD_PANEL_FRAME_BUFFERS,
		.bounce_buffer_size_px = BOARD_WIDTH * BOARD_PANEL_BOUNCE_LINES,
		.hsync_gpio_num = BOARD_PANEL_HSYNC,
		.vsync_gpio_num = BOARD_PANEL_VSYNC,
		.de_gpio_num = BOARD_PANEL_DE,
		.pclk_gpio_num = BOARD_PANEL_PCLK,
		.disp_gpio_num = -1,
		.data_gpio_nums = { BOARD_PANEL_DATA_PINS },
		.flags.fb_in_psram = 1,
	};
	// Not const: the driver takes it through a pointer that is not
	st7701_vendor_config_t vendor_config =
	{
		.init_cmds = panel_init_cmds,
		.init_cmds_size = sizeof(panel_init_cmds) / sizeof(panel_init_cmds[0]),
		.rgb_config = &rgb_config,
	};
	const esp_lcd_panel_dev_config_t dev_config =
	{
		.reset_gpio_num = -1,   // the reset line hangs on the expander: board_panel_start()
		.rgb_ele_order = BOARD_PANEL_ELE_ORDER,
		.bits_per_pixel = BOARD_PANEL_COLMOD_BITS,
		.vendor_config = &vendor_config,
	};
	esp_err_t err;

	ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7701(io, &dev_config, panel), TAG, "panel: RGB bus and frame buffer");
	// Without a reset pin this is the software reset of the ST7701S, as the ESPHome lessons send it
	err = esp_lcd_panel_reset(*panel);
	if(err == ESP_OK)
	{
		// The init sequence, then the RGB signals start
		err = esp_lcd_panel_init(*panel);
	}
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "panel: init sequence failed: %s", esp_err_to_name(err));
		esp_lcd_panel_del(*panel);
		*panel = NULL;
	}
	return err;
}

// Only sets a flag in the driver, under its spinlock: the restart itself is done by the interrupt of the
// next vertical blanking
static void panel_restart_cb(void *arg)
{
	esp_lcd_rgb_panel_restart(arg);
}

/*
 * Asks the driver, more often than a frame lasts, to start the DMA again at the first bounce buffer and to
 * count the buffers anew (see panel_create()). This is what CONFIG_LCD_RGB_RESTART_IN_VSYNC is meant to do
 * and in ESP-IDF v5.5.2 does only by half, and what ESPHome does for this board from its loop. The handle
 * of the ST7701 driver is the one of the RGB panel with some of its functions replaced, so the call takes
 * it. The timer is never stopped: the panel lives as long as the firmware runs.
 */
static esp_err_t panel_restart_start(esp_lcd_panel_handle_t panel)
{
	const esp_timer_create_args_t timer_args =
	{
		.callback = panel_restart_cb,
		.arg = panel,
		.dispatch_method = ESP_TIMER_TASK,
		.name = "panel restart",
	};
	esp_timer_handle_t timer;
	esp_err_t err;

	ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &timer), TAG, "panel: timer of the restart");
	err = esp_timer_start_periodic(timer, (uint64_t)BOARD_PANEL_RESTART_MS * 1000);
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "panel: timer of the restart does not start: %s", esp_err_to_name(err));
		esp_timer_delete(timer);
	}
	return err;
}

esp_err_t board_panel_start(esp_lcd_panel_handle_t *panel)
{
	const spi_line_config_t line_config =
	{
		.cs_io_type = IO_TYPE_GPIO,
		.cs_gpio_num = BOARD_PANEL_SPI_CS,
		.scl_io_type = IO_TYPE_GPIO,
		.scl_gpio_num = BOARD_PANEL_SPI_SCK,
		.sda_io_type = IO_TYPE_GPIO,
		.sda_gpio_num = BOARD_PANEL_SPI_SDA,
	};
	// Clock idle low, data taken on the rising edge: what the ST7701 driver is used with
	const esp_lcd_panel_io_3wire_spi_config_t io_config = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line_config, 0);
	esp_lcd_panel_io_handle_t io;
	esp_err_t err;

	ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "no place for the panel handle");
	ESP_RETURN_ON_FALSE(expander_dev != NULL, ESP_ERR_INVALID_STATE, TAG, "board_init() has not succeeded");
	ESP_RETURN_ON_FALSE(panel_started == NULL, ESP_ERR_INVALID_STATE, TAG, "the panel is started already");
	*panel = NULL;

	/*
	 * CHECK: with the panel running, measure VDD_LCD (pin 5 of the panel connector) with P3 low and with
	 * P3 high. The schematic says low = on; the maker's firmware runs its init with P3 high and ends low,
	 * its ESPHome lessons stay high, and all of them show a picture, so the switch may be without effect.
	 * If the panel stays dark with the backlight on, or the voltage is there with P3 high only:
	 * BOARD_LCD_POWER_ON to 1.
	 */
	ESP_RETURN_ON_ERROR(expander_set(BOARD_EXP_LCD_POWER, BOARD_LCD_POWER_ON), TAG, "port expander: panel power");
	wait_ms(BOARD_LCD_POWER_SETTLE_MS);
	ESP_RETURN_ON_ERROR(expander_reset_pulse(BOARD_EXP_LCD_RESET, BOARD_LCD_RESET_LOW_MS, BOARD_LCD_RESET_WAIT_MS),
	                    TAG, "panel: reset");

	// Bit-banged on three pins of their own. It is kept, so that esp_lcd_panel_disp_on_off() can still
	// send its command later
	ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_3wire_spi(&io_config, &io), TAG, "panel: command bus");
	err = panel_create(io, panel);
	if(err != ESP_OK)
	{
		esp_lcd_panel_io_del(io);
		return err;
	}
	err = panel_restart_start(*panel);
	if(err != ESP_OK)
	{
		esp_lcd_panel_del(*panel);
		*panel = NULL;
		esp_lcd_panel_io_del(io);
		return err;
	}
	panel_started = *panel;
	return ESP_OK;
}

// One task only: the functions of the driver that set a duty are not safe against each other
void board_backlight(int percent)
{
	if(percent < 0)
	{
		percent = 0;
	}
	if(percent > 100)
	{
		percent = 100;
	}
	if(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)percent * BOARD_BACKLIGHT_DUTY_FULL / 100) != ESP_OK ||
	   ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) != ESP_OK)
	{
		ESP_LOGE(TAG, "backlight: %d %% not set", percent);
	}
}

/*
 * CHECK: reads false with the knob released and true while it is pressed (schematic: pull-up of 10 k,
 * the switch to ground; the firmware of the maker takes low as pressed). The other way round would be
 * the comparison below.
 */
bool board_button(bool *pressed)
{
	uint8_t pins;

	if(pressed == NULL || expander_dev == NULL || i2c_master_receive(expander_dev, &pins, 1, BOARD_I2C_TIMEOUT_MS) != ESP_OK)
	{
		return false;
	}
	*pressed = (pins & BOARD_EXP_SWITCH) == 0;
	return true;
}

int board_encoder(void)
{
	int count;
	int turned;

	if(encoder_unit == NULL || pcnt_unit_get_count(encoder_unit, &count) != ESP_OK)
	{
		return 0;
	}
	turned = count - encoder_seen;
	encoder_seen = count;
	return turned;
}

/*
 * What "the reading failed" means: the controller did not answer. The I2C driver returns the same error
 * for a controller that says no and for a bus that hangs, so the two cannot be told apart here.
 *
 * Some controllers of this family (CST816S) sleep until a finger wakes them, and do not answer while they
 * sleep. This one is taken not to, from two things in the sources: the firmware of the maker asks for the
 * number of fingers whenever LVGL wants the pointer, and its driver (Adafruit_CST8XX) returns what is in
 * its buffer without looking whether the transfer worked, which after a missing answer would be the
 * register number 2 it has just sent: two fingers. And the driver of ESPHome gives up on a controller it
 * cannot ask for its id. So here an idle controller answers "no finger" (true, *down false), and false
 * means that the controller is gone.
 *
 * CHECK: board_touch() returns true all the time, finger or not. If it only returns true while a finger
 * is on the screen, the controller does sleep: then the caller has to take false as "no finger", and a
 * controller that is gone cannot be noticed any more. Do not copy that into the caller before it is seen.
 * CHECK: a finger at the top edge reads y near 0, at the bottom edge near 479, left x near 0, right near
 * 479, and a touch lands where the picture is. The firmware of the maker takes BOARD_TOUCH_Y_OFFSET (20)
 * off y, its ESPHome lessons take nothing off. If touches land 20 pixels too high, the offset is 0. If an
 * axis runs the other way or x and y are exchanged, it is turned here, below.
 */
bool board_touch(bool *down, int *x, int *y)
{
	const uint8_t reg = BOARD_TOUCH_REG_POINTS;
	uint8_t data[5];    // fingers, x high, x low, y high, y low
	int raw_x;
	int raw_y;

	if(down == NULL || x == NULL || y == NULL || touch_dev == NULL ||
	   i2c_master_transmit_receive(touch_dev, &reg, 1, data, sizeof(data), BOARD_I2C_TIMEOUT_MS) != ESP_OK)
	{
		return false;
	}
	if(data[0] < 1 || data[0] > BOARD_TOUCH_MAX_POINTS)
	{
		*down = false;
		return true;
	}
	// The upper bits of the two high bytes carry the kind of event and the number of the finger
	raw_x = ((data[1] & 0x0F) << 8) | data[2];
	raw_y = (((data[3] & 0x0F) << 8) | data[4]) - BOARD_TOUCH_Y_OFFSET;
	*x = raw_x < 0 ? 0 : (raw_x >= BOARD_WIDTH ? BOARD_WIDTH - 1 : raw_x);
	*y = raw_y < 0 ? 0 : (raw_y >= BOARD_HEIGHT ? BOARD_HEIGHT - 1 : raw_y);
	*down = true;
	return true;
}

bool board_temperature(int *celsius)
{
	float value;

	if(celsius == NULL || tsens == NULL || temperature_sensor_get_celsius(tsens, &value) != ESP_OK)
	{
		return false;
	}
	*celsius = (int)(value < 0 ? value - 0.5f : value + 0.5f);
	return true;
}
