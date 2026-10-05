/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "platform.h"
#include "board.h"
#include "ui.h"
#include "touch.h"

/*
 * The screen task: reads the knob, its switch and the touch controller, passes the readings to the app, and
 * hands the scene of the app to the drawing code. It decides nothing.
 *
 * Written without the board and without a compiler for it: every call was read in the sources of ESP-IDF
 * v5.5.2, of esp_lvgl_port 2.9.0 and of LVGL 9.5.0. What only the board can decide is marked CHECK.
 *
 * Two tasks touch LVGL, under the lock of esp_lvgl_port: the task of the port, which draws
 * (lv_timer_handler()), and this one, which changes what is to be drawn (ui_show()). Nobody else does.
 *
 * The rhythm. The switch is read every 20 ms, and hold.h breaks a hold when two readings lie more than
 * HOLD_GAP_MS (200 ms) apart. A reading that comes late can therefore only break a hold, never confirm one -
 * but a hold that breaks while the driver keeps the knob pressed is a display that does not do what it is
 * told. What keeps drawing from delaying the readings:
 * - Drawing is the work of the other task, and that one has the lower priority on the same core: this task
 *   interrupts it every 20 ms, in the middle of a frame as well.
 * - This task never waits for the lock of LVGL. The port holds it for a whole frame; if it is taken, the
 *   scene (or the tap that asks for its row) waits for a later round, and the readings go on.
 * - Nothing blocks under the lock of the app: the board is read before the lock is taken, LVGL is given the
 *   scene after it was given back.
 * - A round that came late is not made up for: readings in a burst would be no rhythm either, and two of
 *   them within a millisecond would be no debouncing (knob.h).
 * What this file cannot make sure, and nothing of it was measured:
 * - how long ui_show() takes. It runs in this task: it lays the scene out and measures its texts with the
 *   fonts of TinyTTF. A scene that is the one shown costs a comparison.
 *   CHECK: log the longest time between two readings while turning through all screens and while a fault
 *   memory list is shown; it has to stay far below 200 ms. If it does not, ui_show() has to move to a task
 *   of its own.
 * - the lock of the app: the network task and the web server hold it while they read an answer of 16 KB or
 *   check a layout. That is computing, not waiting, and the lock passes the priority of this task on to
 *   whoever holds it.
 * - the I2C bus: a transfer that hangs waits for BOARD_I2C_TIMEOUT_MS (50 ms), and a round has up to two.
 *   The readings fail then anyway.
 * - the flash: while it is written or erased (a layout or a list is stored, a firmware is uploaded) the
 *   cache is off and no task of either core runs, this one neither. How long one erase takes is a matter of
 *   the flash chip of the board.
 *   CHECK: keep the knob pressed in the clear dialog while a layout is saved from the browser. The ring may
 *   start again; it must not complete earlier than after three seconds of holding.
 */

#define TAG "screen"

// screen_start() gives LVGL its heap, of the size sdkconfig names. Without these two settings there is no
// such size, and LVGL would stop at the first screen with the little it has.
#if LV_USE_STDLIB_MALLOC != LV_STDLIB_BUILTIN || LV_MEM_POOL_EXPAND_SIZE == 0
#error "sdkconfig: CONFIG_LV_USE_BUILTIN_MALLOC and CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES are needed"
#endif

#define ROUND_MS            20      // one reading of the switch (knob.h, hold.h)
#define TICK_ROUNDS         10      // app_tick() five times a second
#define TOUCH_ROUNDS        2       // the touch controller every 40 ms (touch.h: about every 30 ms)

// Above the task of LVGL, so that drawing never delays a reading, and above the web server of ESP-IDF (5)
#define SCREEN_PRIORITY     6
#define LVGL_PRIORITY       4       // the default of the port
/*
 * CHECK: uxTaskGetStackHighWaterMark() of both tasks after every kind of screen was shown.
 * The task of LVGL draws: from lv_timer_handler() down to a glyph that TinyTTF makes from its outline
 * (stb_truetype, with a scan line of 500 bytes on the stack) it is a deep way. The port takes 7168 bytes
 * by default, for fonts that are bitmaps. 16 KB of the internal RAM is a guess on the safe side; a stack
 * that is too small is a panic in every start, and no safe mode ends that.
 * This task measures texts and moves objects, and calls into the app.
 */
#define LVGL_STACK          16384
#define SCREEN_STACK        8192

// The lock of LVGL is asked for, not waited for. 0 would mean "wait for ever" to the port; 1 ms is no tick
// at all at 100 ticks a second, and one tick at 1000.
#define LVGL_TRY_MS         1

/*
 * Lines of the buffer LVGL draws into before the port copies them into the frame buffer: a sixth of the
 * screen, 76800 bytes, in the external RAM. The notes of the port call a tenth the least and see little gain
 * beyond a quarter - measured with a buffer in the internal RAM, which this one is not: the internal RAM is
 * for the WiFi.
 * CHECK: turning through the pages feels immediate. If it does not, the buffer can go to the internal RAM
 * (flags.buff_spiram 0) at the price of 77 KB there.
 */
#define DRAW_LINES          80

// Too large for the stack of the task. Written under the lock of the app, drawn after it; only this task
// uses it.
static scene_t scene;

// How app.h wants a swipe: dx below 0 for a finger that went left, dy below 0 for one that went up
static void pass_swipe(app_t *app, touch_event_t gesture, uint64_t now_ms)
{
	switch(gesture)
	{
		case TOUCH_SWIPE_LEFT: app_swipe(app, -1, 0, now_ms); break;
		case TOUCH_SWIPE_RIGHT: app_swipe(app, 1, 0, now_ms); break;
		case TOUCH_SWIPE_UP: app_swipe(app, 0, -1, now_ms); break;
		case TOUCH_SWIPE_DOWN: app_swipe(app, 0, 1, now_ms); break;
		default: break;
	}
}

static void screen_task(void *arg)
{
	app_t *app = platform_app;
	TickType_t woken = xTaskGetTickCount();
	touch_t touch;
	unsigned round = 0;
	bool was_pressed = false;
	bool show = false;          // a scene is due: the time for it came or an input was passed on
	bool tap_waits = false;     // a tap whose row has not been looked up yet
	bool row_waits = false;     // its row, which the app has not been told yet
	int tap_x = 0;
	int tap_y = 0;
	int row = -1;
	int light = -1;             // what the backlight was set to; -1: nothing yet

	touch_init(&touch);
	for(;;)
	{
		// Read before anything can wait: the time of the reading, not of the lock (app.h)
		uint64_t now_ms = platform_now_ms();
		bool tick = round % TICK_ROUNDS == 0;
		touch_event_t gesture = TOUCH_NONE;
		bool pressed = false;
		bool button_ok = board_button(&pressed);
		int counts = board_encoder();
		int percent;

		if(round % TOUCH_ROUNDS == 0)
		{
			bool down = false;
			int x = 0;
			int y = 0;
			/*
			 * CHECK (board.c): board_touch() succeeds with and without a finger. If it only succeeds while
			 * a finger is on the screen, the controller sleeps in between; touch.h then drops every touch
			 * after TOUCH_LOST_MS, and a failed reading has to count as "no finger" here. Not before that
			 * was seen: it hides a controller that is gone.
			 */
			bool touch_ok = board_touch(&down, &x, &y);

			gesture = touch_sample(&touch, touch_ok, down, x, y, now_ms, &tap_x, &tap_y);
			if(gesture == TOUCH_TAP)
			{
				tap_waits = true;
			}
		}
		round++;

		// What changes the picture is shown at once, not with the next tick: a detent, the switch going
		// down or up, a tap, a swipe
		if(tick || counts != 0 || (button_ok && pressed != was_pressed) || row_waits ||
		   (gesture != TOUCH_NONE && gesture != TOUCH_TAP))
		{
			show = true;
		}
		if(button_ok)
		{
			was_pressed = pressed;
		}

		platform_lock();
		app_button(app, pressed, button_ok, now_ms);
		app_encoder(app, counts, now_ms);
		if(row_waits)
		{
			// Also a tap that hit no row (-1): it wakes a dark screen and answers "Update in Ordnung?"
			app_tap(app, row, now_ms);
			row_waits = false;
		}
		pass_swipe(app, gesture, now_ms);
		if(tick)
		{
			app_tick(app, now_ms);
		}
		if(show)
		{
			// Made anew in every round until LVGL has taken it: the one that is drawn is never an old one
			app_scene(app, &scene, now_ms);
		}
		percent = app_backlight(app, now_ms);
		platform_events();
		platform_unlock();

		if(percent != light)
		{
			board_backlight(percent);
			light = percent;
		}

		/*
		 * The one place that touches LVGL. The tap first: its row is one of the scene that is laid out
		 * now, the one the finger saw, and the app hears of it in the next round.
		 * CHECK: a tap lands on the row under the finger, at the top and at the bottom of a list.
		 */
		if((tap_waits || show) && lvgl_port_lock(LVGL_TRY_MS))
		{
			if(tap_waits)
			{
				row = ui_row_at(tap_x, tap_y);
				row_waits = true;
				tap_waits = false;
			}
			if(show)
			{
				ui_show(&scene);
				show = false;
			}
			lvgl_port_unlock();
		}

		if(xTaskDelayUntil(&woken, pdMS_TO_TICKS(ROUND_MS)) == pdFALSE)
		{
			// Late: the next round is a whole one from now
			woken = xTaskGetTickCount();
		}
	}
}

/*
 * Called by the task that called board_panel_start(): the interrupts of the panel run on its core (board.c),
 * and the task of LVGL and the screen task are put there too, so that the copy of a line into the bounce
 * buffer and the drawing do not use the external RAM from two cores at once.
 *
 * What a failed start has taken is not given back: main.c restarts the display.
 */
esp_err_t screen_start(esp_lcd_panel_handle_t panel)
{
	const BaseType_t core = xPortGetCoreID();
	const lvgl_port_cfg_t port_config =
	{
		.task_priority = LVGL_PRIORITY,
		.task_stack = LVGL_STACK,
		.task_affinity = core,
		.task_max_sleep_ms = 500,   // the default of the port; a change of the scene wakes its task at once
		.timer_period_ms = 5,       // the default of the port
	};
	/*
	 * The panel of board.c is the RGB panel of esp_lcd itself (the ST7701 driver hands it out with some of
	 * its functions replaced), which is what lvgl_port_add_disp_rgb() needs: it registers its callback at
	 * that handle. No panel IO: the port only asks for one for other kinds of panels. LVGL draws parts of
	 * the screen into its own buffer, and the port copies them into the one frame buffer of the panel.
	 * CHECK (board.c): the picture is upright and not mirrored. If not: .rotation here.
	 */
	const lvgl_port_display_cfg_t display_config =
	{
		.panel_handle = panel,
		.buffer_size = BOARD_WIDTH * DRAW_LINES,
		.hres = BOARD_WIDTH,
		.vres = BOARD_HEIGHT,
		.color_format = LV_COLOR_FORMAT_RGB565,
		.flags.buff_spiram = 1,
	};
	// Bounce buffers, as board.c starts the panel. Not "avoid tearing": that draws straight into the frame
	// buffers and needs two of them.
	const lvgl_port_display_rgb_cfg_t rgb_config =
	{
		.flags.bb_mode = 1,
	};
	lv_display_t *display = NULL;
	lv_mem_pool_t added;
	void *pool;

	ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "no panel");
	// Creates the task of LVGL and returns when lv_init() has run in it
	ESP_RETURN_ON_ERROR(lvgl_port_init(&port_config), TAG, "LVGL port");

	// The heap of LVGL: lv_init() had the small array of sdkconfig, everything after it gets this
	pool = heap_caps_malloc(LV_MEM_POOL_EXPAND_SIZE, MALLOC_CAP_SPIRAM);
	ESP_RETURN_ON_FALSE(pool != NULL, ESP_ERR_NO_MEM, TAG, "no memory for the heap of LVGL");

	// In one piece: the task of LVGL begins to draw as soon as there is a display and the lock is free, and
	// it is to find the screen of ui_init() then, not an empty one
	lvgl_port_lock(0);
	added = lv_mem_add_pool(pool, LV_MEM_POOL_EXPAND_SIZE);
	if(added != NULL)
	{
		display = lvgl_port_add_disp_rgb(&display_config, &rgb_config);
	}
	if(display != NULL)
	{
		ui_init(display);
	}
	lvgl_port_unlock();
	ESP_RETURN_ON_FALSE(added != NULL, ESP_ERR_NO_MEM, TAG, "LVGL does not take its heap");
	ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_NO_MEM, TAG, "LVGL display");

	ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(screen_task, "screen", SCREEN_STACK, NULL, SCREEN_PRIORITY, NULL,
	                                            core) == pdPASS,
	                    ESP_ERR_NO_MEM, TAG, "no memory for the screen task");
	return ESP_OK;
}
