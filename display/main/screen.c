/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"
#include "platform.h"
#include "board.h"
#include "ui.h"
#include "touch.h"

/*
 * The screen task: reads the knob, its switch and the touch controller, passes the readings to the app, and
 * hands the scene of the app to the drawing code. It decides nothing.
 *
 * Written without the board: every call was read in the sources of ESP-IDF v5.5.2 and of LVGL 9.5.0. What
 * only the board can decide is marked CHECK.
 *
 * LVGL is attached to the panel here, in a few lines, not through the component espressif/esp_lvgl_port:
 * with that component in the build the component manager solved LVGL a second time (it names the registry
 * of its lvgl dependency) and fetched 9.6.0~1 over the pinned 9.5.0 - seen in the CI on 2026-10-05, three
 * builds. What it would do for this panel is little: a tick, a task that calls lv_timer_handler(), a
 * lock, and a flush that copies into the frame buffer of the RGB panel.
 *
 * Two tasks touch LVGL, under one lock: the task below that draws (lv_timer_handler()), and the screen
 * task, which changes what is to be drawn (ui_show()). Nobody else does.
 *
 * The rhythm. The switch is read every 20 ms, and hold.h breaks a hold when two readings lie more than
 * HOLD_GAP_MS (200 ms) apart. A reading that comes late can therefore only break a hold, never confirm one -
 * but a hold that breaks while the driver keeps the knob pressed is a display that does not do what it is
 * told. What keeps drawing from delaying the readings:
 * - Drawing is the work of the other task, and that one has the lower priority on the same core: this task
 *   interrupts it every 20 ms, in the middle of a frame as well.
 * - This task never waits for the lock of LVGL. The drawing task holds it for a whole frame; if it is taken, the
 *   scene (or the tap that asks for its row) waits for a later round, and the readings go on.
 * - Nothing blocks under the lock of the app: the board is read before the lock is taken, LVGL is given the
 *   scene after it was given back.
 * - A round that came late is not made up for: readings in a burst would be no rhythm either, and two of
 *   them within a millisecond would be no debouncing (knob.h).
 * What this file cannot make sure, and nothing of it was measured:
 * - how long app_scene() and ui_show() take. Both run in this task. The scene is made in every round, under
 *   the lock of the app (see screen_task() for why); ui_show() is called for a scene that differs from the
 *   one before: it lays the scene out and measures its texts with the fonts of TinyTTF.
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
 *
 * Both tasks are watched by the task watchdog of ESP-IDF, as the idle tasks are: display/sdkconfig.defaults
 * says what for, and has the CHECK that goes with it.
 */

#define TAG "screen"

// screen_start() gives LVGL its heap, of the size sdkconfig names. Without these two settings there is no
// such size, and LVGL would stop at the first screen with the little it has.
#if LV_USE_STDLIB_MALLOC != LV_STDLIB_BUILTIN || LV_MEM_POOL_EXPAND_SIZE == 0
#error "sdkconfig: CONFIG_LV_USE_BUILTIN_MALLOC and CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES are needed"
#endif

// Without the panic the watchdog only prints: a drawing task that spins would freeze the picture for good,
// and with it everything the main task does, which has the lower priority on the same core
#if !CONFIG_ESP_TASK_WDT_INIT || !CONFIG_ESP_TASK_WDT_PANIC
#error "sdkconfig: CONFIG_ESP_TASK_WDT_PANIC is needed, see display/sdkconfig.defaults"
#endif

#define ROUND_MS            20      // one reading of the switch (knob.h, hold.h)
#define TICK_ROUNDS         10      // app_tick() five times a second
#define TOUCH_ROUNDS        2       // the touch controller every 40 ms (touch.h: about every 30 ms)
// A reading of the switch takes one transfer of a byte, well under a millisecond at 100 kHz. One that took
// longer than this was held up in the middle, and when in that time the switch was read nobody knows: it
// does not count (screen_task()). Not measured on the board - if the knob loses presses there while nothing
// writes to the flash, the readings take longer than assumed here.
#define READ_MS             10

// Above the task of LVGL, so that drawing never delays a reading, and above the web server of ESP-IDF (5)
#define SCREEN_PRIORITY     6
#define LVGL_PRIORITY       4
/*
 * CHECK: uxTaskGetStackHighWaterMark() of both tasks after every kind of screen was shown.
 * The task of LVGL draws: from lv_timer_handler() down to a glyph that TinyTTF makes from its outline
 * (stb_truetype, with a scan line of 500 bytes on the stack) it is a deep way. 16 KB of the internal RAM
 * is a guess on the safe side; a stack
 * that is too small is a panic in every start, and no safe mode ends that.
 * This task measures texts and moves objects, and calls into the app.
 */
#define LVGL_STACK          16384
#define SCREEN_STACK        8192

// The lock of LVGL is asked for, not waited for: 1 ms is no tick at all at 100 ticks a second, and one tick
// at 1000.
#define LVGL_TRY_MS         1
// The drawing task sleeps as long as LVGL says nothing is due, within these bounds. At 100 ticks a second
// (sdkconfig.defaults) the lower one changes nothing: every sleep below a tick is one tick, 10 ms.
#define LVGL_SLEEP_MIN_MS   5
#define LVGL_SLEEP_MAX_MS   50

/*
 * Lines of the buffer LVGL draws into before they are copied into the frame buffer: a sixth of the screen,
 * 76800 bytes, in the external RAM. Espressif's notes on such buffers call a tenth the least and see little
 * gain beyond a quarter - measured with a buffer in the internal RAM, which this one is not: the internal
 * RAM is for the WiFi.
 * CHECK: turning through the pages feels immediate. If it does not, the buffer can go to the internal RAM
 * (MALLOC_CAP_INTERNAL in screen_start()) at the price of 77 KB there.
 */
#define DRAW_LINES          80

// Too large for the stack of the task. Written under the lock of the app, drawn after it; only this task
// uses it.
static scene_t scene;
// The scene LVGL was given last. scene.h fills every byte of a scene, and two that show the same are the
// same memory: comparing the two tells whether there is anything new to draw.
static scene_t given;

// The lock of LVGL: held by the drawing task for a frame, asked for by the screen task
static SemaphoreHandle_t lvgl_mutex;
// The drawing task, for the screen task to wake it
static TaskHandle_t lvgl_handle;

// The task that calls this is watched from now on: it has to come round (esp_task_wdt_reset()) within the
// time of the watchdog, or the display ends in a panic and starts anew
static void watched(const char *name)
{
	esp_err_t err = esp_task_wdt_add(NULL);

	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "the task %s is not watched: %s", name, esp_err_to_name(err));
	}
}

// The clock of LVGL: milliseconds since the start, from the clock every task uses
static uint32_t lvgl_tick(void)
{
	return (uint32_t)(esp_timer_get_time() / 1000);
}

// LVGL has drawn a part of the screen into its buffer: copied into the frame buffer of the RGB panel at
// once (esp_lcd copies with the CPU), so the buffer is free again when this returns
static void lvgl_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
	esp_lcd_panel_handle_t panel = lv_display_get_user_data(display);

	esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels);
	lv_display_flush_ready(display);
}

/*
 * The only place that draws. Between two calls of lv_timer_handler() the task sleeps for as long as LVGL
 * says nothing is due - or until the screen task has handed over another scene: LVGL stops the timer of
 * its display after every refresh (lv_display_refr_timer(), lv_refr.c) and starts it again when something
 * is to be drawn, but that tells nobody who sleeps (lv_timer_resume() only calls a resume callback, and
 * none is set here). Without the wake every new scene would wait for the rest of a sleep, 50 ms at most.
 */
static void lvgl_task(void *unused)
{
	(void)unused;
	watched("lvgl");
	for(;;)
	{
		uint32_t sleep_ms = LVGL_SLEEP_MIN_MS;

		esp_task_wdt_reset();
		if(xSemaphoreTake(lvgl_mutex, portMAX_DELAY) == pdTRUE)
		{
			sleep_ms = lv_timer_handler();
			xSemaphoreGive(lvgl_mutex);
		}
		if(sleep_ms < LVGL_SLEEP_MIN_MS) sleep_ms = LVGL_SLEEP_MIN_MS;
		if(sleep_ms > LVGL_SLEEP_MAX_MS) sleep_ms = LVGL_SLEEP_MAX_MS;
		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleep_ms) > 0 ? pdMS_TO_TICKS(sleep_ms) : 1);
	}
}

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
	bool tap_waits = false;     // a tap whose row has not been looked up yet
	bool row_waits = false;     // its row, which the app has not been told yet
	int tap_x = 0;
	int tap_y = 0;
	int row = -1;
	int light = -1;             // what the backlight was set to; -1: nothing yet
	bool given_any = false;     // LVGL was given a scene: before that there is none to compare with

	touch_init(&touch);
	watched("screen");
	for(;;)
	{
		// Read before anything can wait: the time of the reading, not of the lock (app.h)
		uint64_t now_ms = platform_now_ms();
		bool tick = round % TICK_ROUNDS == 0;
		touch_event_t gesture = TOUCH_NONE;
		bool pressed = false;
		bool button_ok = board_button(&pressed);
		int counts = board_encoder();
		bool show;                  // the scene is another one than LVGL was given last
		int percent;

		// The time above is the time of the reading only if the reading followed it at once. If the task was
		// held up in between (the flash was erased: nobody runs), the switch was read later than its time
		// says, a press that began in between is dated early, and a hold (hold.h) would be complete that
		// much before its three seconds. Such a reading has observed nothing, like one that failed.
		if(platform_now_ms() - now_ms > READ_MS)
		{
			button_ok = false;
		}
		esp_task_wdt_reset();

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

		platform_lock();
		app_button(app, pressed, button_ok, now_ms);
		app_encoder(app, counts, now_ms);
		if(row_waits)
		{
			// Also a tap that hit no row (-1): it is an input, which wakes a dark screen. It answers no
			// question (nav.h: under a question a tap does nothing)
			app_tap(app, row, now_ms);
			row_waits = false;
		}
		pass_swipe(app, gesture, now_ms);
		if(tick)
		{
			app_tick(app, now_ms);
		}
		// Made anew in every round, and what it shows decides whether there is something to draw - not which
		// input came. This task cannot know that: a press counts one reading after its edge (knob.h), a long
		// press and the outcome of a hold come without any edge, a value arrives with the network task. And
		// the scene that is drawn is never an old one.
		app_scene(app, &scene, now_ms);
		percent = app_backlight(app, now_ms);
		platform_events();
		platform_unlock();
		show = !given_any || memcmp(&scene, &given, sizeof(scene)) != 0;

		// Dark before a restart: the chip leaves the backlight as it is when it restarts (main.c)
		if(platform_restarting())
		{
			percent = 0;
		}
		if(percent != light)
		{
			// A screen that stays dark has more than one possible cause, and from outside they all look the
			// same. With this line the log tells whether the app wants it dark (standby, heat: the info page
			// has the temperature) or the board does not do what it is told (board.c: polarity, panel).
			if(light < 0 || (percent == 0) != (light == 0))
			{
				ESP_LOGI(TAG, "backlight %d %%", percent);
			}
			board_backlight(percent);
			light = percent;
		}

		/*
		 * The one place that touches LVGL. The tap first: its row is one of the scene that is laid out
		 * now, the one the finger saw, and the app hears of it in the next round.
		 * CHECK: a tap lands on the row under the finger, at the top and at the bottom of a list.
		 */
		if((tap_waits || show) && xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(LVGL_TRY_MS)) == pdTRUE)
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
				memcpy(&given, &scene, sizeof(given));
				given_any = true;
			}
			xSemaphoreGive(lvgl_mutex);
			if(show)
			{
				// The drawing task sleeps while LVGL has nothing due (lvgl_task())
				xTaskNotifyGive(lvgl_handle);
			}
		}

		// The next round is due ROUND_MS after this one began, whenever that was. xTaskDelayUntil() keeps to
		// its own plan instead: after a wake that came late it returns at once the next time, and that would
		// be two readings within a millisecond, one round after the late one. Behind a round that took
		// longer than ROUND_MS the call returns at once and the next round begins one to two ticks later
		// (the tick count is whole ticks): never two readings back to back.
		xTaskDelayUntil(&woken, pdMS_TO_TICKS(ROUND_MS));
		woken = xTaskGetTickCount();
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
	const size_t draw_bytes = (size_t)BOARD_WIDTH * DRAW_LINES * 2;     // RGB565
	lv_display_t *display;
	void *pool;
	void *draw;

	ESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, "no panel");
	lvgl_mutex = xSemaphoreCreateMutex();
	ESP_RETURN_ON_FALSE(lvgl_mutex != NULL, ESP_ERR_NO_MEM, TAG, "no memory for the lock of LVGL");

	lv_init();
	lv_tick_set_cb(lvgl_tick);

	// The heap of LVGL: lv_init() had the small array of sdkconfig, everything after it gets this
	pool = heap_caps_malloc(LV_MEM_POOL_EXPAND_SIZE, MALLOC_CAP_SPIRAM);
	ESP_RETURN_ON_FALSE(pool != NULL, ESP_ERR_NO_MEM, TAG, "no memory for the heap of LVGL");
	ESP_RETURN_ON_FALSE(lv_mem_add_pool(pool, LV_MEM_POOL_EXPAND_SIZE) != NULL, ESP_ERR_NO_MEM, TAG,
	                    "LVGL does not take its heap");

	/*
	 * LVGL draws parts of the screen into its own buffer, and lvgl_flush() copies them into the one frame
	 * buffer of the panel (board.c: bounce buffers, no second frame buffer). RGB565 as the panel takes it;
	 * CONFIG_LV_COLOR_DEPTH_16 makes that the format of the display.
	 * CHECK (board.c): the picture is upright and not mirrored. If not: board.c turns the panel and the
	 * touch points (its CHECK there says how); lv_display_set_rotation() alone does not turn the pixels
	 * that lvgl_flush() copies.
	 */
	draw = heap_caps_malloc(draw_bytes, MALLOC_CAP_SPIRAM);
	ESP_RETURN_ON_FALSE(draw != NULL, ESP_ERR_NO_MEM, TAG, "no memory for the buffer LVGL draws into");
	display = lv_display_create(BOARD_WIDTH, BOARD_HEIGHT);
	ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_NO_MEM, TAG, "LVGL display");
	lv_display_set_user_data(display, panel);
	lv_display_set_buffers(display, draw, NULL, draw_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_flush_cb(display, lvgl_flush);

	// Before the drawing task exists: it is to find the screen of ui_init(), not an empty one
	ui_init(display);

	ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_STACK, NULL, LVGL_PRIORITY, &lvgl_handle,
	                                            core) == pdPASS,
	                    ESP_ERR_NO_MEM, TAG, "no memory for the task of LVGL");
	ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(screen_task, "screen", SCREEN_STACK, NULL, SCREEN_PRIORITY, NULL,
	                                            core) == pdPASS,
	                    ESP_ERR_NO_MEM, TAG, "no memory for the screen task");
	return ESP_OK;
}
