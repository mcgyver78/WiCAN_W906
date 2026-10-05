"""Mutations of display/main/screen.c, see ../redproof.py. The simulation is ../screen_sim.c."""

F = "main/screen.c"
S = "screen_sim"

# The rhythm of the screen task
WOKEN_ANEW = "\t\txTaskDelayUntil(&woken, pdMS_TO_TICKS(ROUND_MS));\n\t\twoken = xTaskGetTickCount();\n"

# The readings and what the app hears of them
HELD_UP = "\t\tif(platform_now_ms() - now_ms > READ_MS)\n\t\t{\n\t\t\tbutton_ok = false;\n\t\t}\n"
HELD_UP_LIMIT = "if(platform_now_ms() - now_ms > READ_MS)"
TOLD_SWITCH = "\t\tplatform_lock();\n\t\tapp_button(app, pressed, button_ok, now_ms);\n"
TOLD_COUNTS = "\t\tapp_encoder(app, counts, now_ms);\n"
TICKED = "\t\tif(tick)\n\t\t{\n\t\t\tapp_tick(app, now_ms);\n\t\t}\n"
GESTURE = "\t\ttouch_event_t gesture = TOUCH_NONE;\n"
TOUCH_SAMPLE = "gesture = touch_sample(&touch, touch_ok, down, x, y, now_ms, &tap_x, &tap_y);"
TOLD_SWIPE = "\t\tpass_swipe(app, gesture, now_ms);\n"
SWIPE_LEFT = "case TOUCH_SWIPE_LEFT: app_swipe(app, -1, 0, now_ms); break;"
SWIPE_UP = "case TOUCH_SWIPE_UP: app_swipe(app, 0, -1, now_ms); break;"
TAP_NOTED = "\t\t\t\ttap_waits = true;\n"
ROW_NOTED = "\t\t\t\trow_waits = true;\n"
ROW_WAITS = "\t\tif(row_waits)\n"
ROW_TOLD = "\t\t\trow_waits = false;\n"
TAP_LOOKED_UP = "\t\t\t\ttap_waits = false;\n"
ROW_AT = "row = ui_row_at(tap_x, tap_y);"
MADE = "\t\tapp_scene(app, &scene, now_ms);\n"
EVENTS = "\t\tplatform_events();\n\t\tplatform_unlock();\n"

# The scene, LVGL and its lock
SHOW = "show = !given_any || memcmp(&scene, &given, sizeof(scene)) != 0;"
ASKED = "\t\tif((tap_waits || show) && xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(LVGL_TRY_MS)) == pdTRUE)\n"
TAP_THEN_SCENE = "\t\t\tif(tap_waits)\n\t\t\t{\n\t\t\t\trow = ui_row_at(tap_x, tap_y);\n\t\t\t\trow_waits = true;\n" \
                 "\t\t\t\ttap_waits = false;\n\t\t\t}\n" \
                 "\t\t\tif(show)\n\t\t\t{\n\t\t\t\tui_show(&scene);\n\t\t\t\tmemcpy(&given, &scene, sizeof(given));\n" \
                 "\t\t\t\tgiven_any = true;\n\t\t\t}\n"
SCENE_THEN_TAP = "\t\t\tif(show)\n\t\t\t{\n\t\t\t\tui_show(&scene);\n\t\t\t\tmemcpy(&given, &scene, sizeof(given));\n" \
                 "\t\t\t\tgiven_any = true;\n\t\t\t}\n" \
                 "\t\t\tif(tap_waits)\n\t\t\t{\n\t\t\t\trow = ui_row_at(tap_x, tap_y);\n\t\t\t\trow_waits = true;\n" \
                 "\t\t\t\ttap_waits = false;\n\t\t\t}\n"
GIVEN = "\t\t\t\tmemcpy(&given, &scene, sizeof(given));\n"
GIVEN_BACK = "\t\t\txSemaphoreGive(lvgl_mutex);\n\t\t\tif(show)\n"
WOKEN = "\t\t\tif(show)\n\t\t\t{\n\t\t\t\t// The drawing task sleeps while LVGL has nothing due (lvgl_task())\n" \
        "\t\t\t\txTaskNotifyGive(lvgl_handle);\n\t\t\t}\n"

# The backlight
DARK = "\t\tif(platform_restarting())\n\t\t{\n\t\t\tpercent = 0;\n\t\t}\n"
LIGHT_CHANGED = "\t\tif(percent != light)\n"
LIGHT_LOGGED = "\t\t\tif(light < 0 || (percent == 0) != (light == 0))\n"
LIGHT_SET = "\t\t\tboard_backlight(percent);\n"
LIGHT_KEPT = "\t\t\tlight = percent;\n"

# The drawing task
DRAWING_FED = "\t\tesp_task_wdt_reset();\n\t\tif(xSemaphoreTake(lvgl_mutex, portMAX_DELAY) == pdTRUE)\n"
DRAWS = "\t\t\tsleep_ms = lv_timer_handler();\n\t\t\txSemaphoreGive(lvgl_mutex);\n"
SLEEP_MAX = "\t\tif(sleep_ms > LVGL_SLEEP_MAX_MS) sleep_ms = LVGL_SLEEP_MAX_MS;\n"
SLEEP_TICKS = "ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleep_ms) > 0 ? pdMS_TO_TICKS(sleep_ms) : 1);"
FLUSH = "esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels);"
FLUSH_READY = "\tlv_display_flush_ready(display);\n"
LVGL_CLOCK = "return (uint32_t)(esp_timer_get_time() / 1000);"

# The watchdog
SCREEN_FED = "\t\tesp_task_wdt_reset();\n\n\t\tif(round % TOUCH_ROUNDS == 0)\n"
NOT_WATCHED = "\t\tESP_LOGE(TAG, \"the task %s is not watched: %s\", name, esp_err_to_name(err));\n"

# screen_start()
NO_PANEL = "\tESP_RETURN_ON_FALSE(panel != NULL, ESP_ERR_INVALID_ARG, TAG, \"no panel\");\n"
NO_LOCK = "\tESP_RETURN_ON_FALSE(lvgl_mutex != NULL, ESP_ERR_NO_MEM, TAG, \"no memory for the lock of LVGL\");\n"
NO_POOL = "\tESP_RETURN_ON_FALSE(pool != NULL, ESP_ERR_NO_MEM, TAG, \"no memory for the heap of LVGL\");\n"
NO_BUFFER = "\tESP_RETURN_ON_FALSE(draw != NULL, ESP_ERR_NO_MEM, TAG, \"no memory for the buffer LVGL draws into\");\n"
NO_DISPLAY = "\tESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_NO_MEM, TAG, \"LVGL display\");\n"
NO_SCREEN_TASK = "core) == pdPASS,\n\t                    ESP_ERR_NO_MEM, TAG, \"no memory for the screen task\");"
POOL = "pool = heap_caps_malloc(LV_MEM_POOL_EXPAND_SIZE, MALLOC_CAP_SPIRAM);"
BUFFERS = "lv_display_set_buffers(display, draw, NULL, draw_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);"

MUTATIONS = [
    # the rhythm
    ("screen_round_of_40_ms", S, F, "#define ROUND_MS            20 ", "#define ROUND_MS            40 "),
    ("screen_app_ticked_in_every_round", S, F, "#define TICK_ROUNDS         10 ", "#define TICK_ROUNDS         1 "),
    ("screen_touch_read_in_every_round", S, F, "#define TOUCH_ROUNDS        2 ", "#define TOUCH_ROUNDS        1 "),
    ("screen_app_never_ticked", S, F, TICKED, "\t\t(void)tick;\n"),
    ("screen_late_round_made_up_for", S, F, WOKEN_ANEW, "\t\txTaskDelayUntil(&woken, pdMS_TO_TICKS(ROUND_MS));\n"),

    # the reading comes before anything that can wait, and one that was held up does not count
    ("screen_reading_held_up_counts", S, F, HELD_UP, ""),
    ("screen_reading_of_read_ms_dropped", S, F, HELD_UP_LIMIT, "if(platform_now_ms() - now_ms >= READ_MS)"),
    ("screen_reading_may_take_100_ms", S, F, "#define READ_MS             10\n", "#define READ_MS             100\n"),
    ("screen_switch_read_under_the_lock", S, F, TOLD_SWITCH,
     "\t\tplatform_lock();\n\t\tbutton_ok = board_button(&pressed) && button_ok;\n"
     "\t\tapp_button(app, pressed, button_ok, now_ms);\n"),
    ("screen_time_of_the_lock", S, F, TOLD_SWITCH,
     "\t\tplatform_lock();\n\t\tnow_ms = platform_now_ms();\n\t\tapp_button(app, pressed, button_ok, now_ms);\n"),
    ("screen_failed_reading_counts", S, F, TOLD_SWITCH,
     "\t\tplatform_lock();\n\t\tapp_button(app, pressed, button_ok || round > 0, now_ms);\n"),

    # no input is passed on twice, and none is lost
    ("screen_counts_not_told", S, F, TOLD_COUNTS, "\t\t(void)counts;\n"),
    ("screen_swipe_told_twice", S, F, GESTURE, "\t\tstatic touch_event_t gesture = TOUCH_NONE;\n"),
    ("screen_swipe_not_told", S, F, TOLD_SWIPE, "\t\t(void)pass_swipe;\n"),
    ("screen_swipe_left_is_right", S, F, SWIPE_LEFT, "case TOUCH_SWIPE_LEFT: app_swipe(app, 1, 0, now_ms); break;"),
    ("screen_swipe_up_is_down", S, F, SWIPE_UP, "case TOUCH_SWIPE_UP: app_swipe(app, 0, 1, now_ms); break;"),
    ("screen_failed_touch_reading_is_no_finger", S, F, TOUCH_SAMPLE,
     "gesture = touch_sample(&touch, touch_ok || round > 0, down, x, y, now_ms, &tap_x, &tap_y);"),

    # a tap is looked up under the lock of LVGL and told in the next round
    ("screen_tap_not_noted", S, F, TAP_NOTED, "\t\t\t\t(void)tap_waits;\n"),
    ("screen_row_of_a_tap_not_told", S, F, ROW_NOTED, ""),
    ("screen_tap_told_in_every_round", S, F, ROW_TOLD, ""),
    ("screen_tap_looked_up_in_every_round", S, F, TAP_LOOKED_UP, ""),
    ("screen_tap_without_a_row_dropped", S, F, ROW_WAITS, "\t\tif(row_waits && row >= 0)\n"),
    ("screen_tap_point_swapped", S, F, ROW_AT, "row = ui_row_at(tap_y, tap_x);"),
    ("screen_tap_looked_up_in_the_new_scene", S, F, TAP_THEN_SCENE, SCENE_THEN_TAP),

    # the scene: the one of this round, given when it differs, under the lock of LVGL and no other
    ("screen_scene_made_with_the_tick_only", S, F, MADE,
     "\t\tif(tick)\n\t\t{\n\t\t\tapp_scene(app, &scene, now_ms);\n\t\t}\n"),
    ("screen_scene_given_in_every_round", S, F, SHOW,
     "show = !given_any || memcmp(&scene, &given, sizeof(scene)) != 0 || round > 0;"),
    ("screen_new_scene_not_given", S, F, SHOW, "show = !given_any;"),
    ("screen_given_scene_not_remembered", S, F, GIVEN, ""),
    ("screen_lvgl_without_its_lock", S, F, ASKED, "\t\tif(tap_waits || show)\n"),
    ("screen_waits_for_the_lock_of_lvgl", S, F, "#define LVGL_TRY_MS         1\n", "#define LVGL_TRY_MS         100\n"),
    ("screen_waits_for_the_lock_of_lvgl_for_ever", S, F, ASKED,
     "\t\tif((tap_waits || show) && xSemaphoreTake(lvgl_mutex, portMAX_DELAY) == pdTRUE)\n"),
    ("screen_keeps_the_lock_of_lvgl", S, F, GIVEN_BACK, "\t\t\tif(show)\n"),
    ("screen_keeps_the_lock_of_the_app", S, F, EVENTS, "\t\tplatform_events();\n"),

    # the drawing task is woken exactly when it was given a scene
    ("screen_drawing_task_not_woken", S, F, WOKEN, ""),
    ("screen_drawing_task_woken_for_a_tap", S, F, WOKEN, "\t\t\txTaskNotifyGive(lvgl_handle);\n"),

    # what the app raised is taken before its lock is given back
    ("screen_events_not_taken", S, F, EVENTS, "\t\tplatform_unlock();\n"),
    ("screen_events_taken_behind_the_lock", S, F, EVENTS, "\t\tplatform_unlock();\n\t\tplatform_events();\n"),

    # the backlight
    ("screen_lit_while_restarting", S, F, DARK, ""),
    ("screen_backlight_set_in_every_round", S, F, LIGHT_CHANGED, "\t\tif(percent != light || round > 0)\n"),
    ("screen_backlight_never_set", S, F, LIGHT_SET, ""),
    ("screen_backlight_not_remembered", S, F, LIGHT_KEPT, ""),
    ("screen_backlight_never_logged", S, F, LIGHT_LOGGED, "\t\t\tif(light < -1)\n"),
    ("screen_backlight_logged_with_every_change", S, F, LIGHT_LOGGED, "\t\t\tif(light < 0 || percent != light)\n"),

    # the watchdog: both tasks are watched, and fed in every round
    ("screen_task_not_watched", S, F, "\twatched(\"screen\");\n", ""),
    ("screen_drawing_task_not_watched", S, F, "\twatched(\"lvgl\");\n", ""),
    ("screen_task_not_fed", S, F, SCREEN_FED, "\n\t\tif(round % TOUCH_ROUNDS == 0)\n"),
    ("screen_drawing_task_not_fed", S, F, DRAWING_FED, "\t\tif(xSemaphoreTake(lvgl_mutex, portMAX_DELAY) == pdTRUE)\n"),
    ("screen_not_watched_and_silent", S, F, NOT_WATCHED, "\t\t(void)name;\n"),

    # the drawing task
    ("screen_drawing_without_the_lock", S, F, DRAWING_FED, "\t\tesp_task_wdt_reset();\n\t\tif(sleep_ms > 0)\n"),
    ("screen_drawing_task_keeps_the_lock", S, F, DRAWS, "\t\t\tsleep_ms = lv_timer_handler();\n"),
    ("screen_drawing_task_sleeps_without_end", S, F, SLEEP_MAX, ""),
    ("screen_drawing_task_spins", S, F, SLEEP_TICKS,
     "ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleep_ms) > 0 ? pdMS_TO_TICKS(sleep_ms) : 0);"),
    ("screen_flush_a_column_short", S, F, FLUSH,
     "esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2, area->y2 + 1, pixels);"),
    ("screen_flush_a_row_short", S, F, FLUSH,
     "esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2, pixels);"),
    ("screen_flush_never_ready", S, F, FLUSH_READY, ""),
    ("screen_clock_of_lvgl_in_microseconds", S, F, LVGL_CLOCK, "return (uint32_t)(esp_timer_get_time());"),

    # the start
    ("screen_start_without_a_panel", S, F, NO_PANEL, ""),
    ("screen_start_without_the_lock", S, F, NO_LOCK, ""),
    ("screen_start_without_the_heap_of_lvgl", S, F, NO_POOL, ""),
    ("screen_start_without_the_buffer", S, F, NO_BUFFER, ""),
    ("screen_start_without_a_display", S, F, NO_DISPLAY, ""),
    ("screen_start_without_the_screen_task", S, F, NO_SCREEN_TASK,
     "core) != 77,\n\t                    ESP_ERR_NO_MEM, TAG, \"no memory for the screen task\");"),
    ("screen_heap_of_lvgl_in_the_internal_ram", S, F, POOL,
     "pool = heap_caps_malloc(LV_MEM_POOL_EXPAND_SIZE, MALLOC_CAP_INTERNAL);"),
    ("screen_buffer_given_as_twice_its_size", S, F, BUFFERS,
     "lv_display_set_buffers(display, draw, NULL, draw_bytes * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);"),
    ("screen_buffer_of_eight_lines", S, F, "#define DRAW_LINES          80\n", "#define DRAW_LINES          8\n"),
    ("screen_ui_not_built", S, F, "\tui_init(display);\n", ""),
    ("screen_tasks_on_the_first_core", S, F, "const BaseType_t core = xPortGetCoreID();", "const BaseType_t core = 0;"),
    ("screen_drawing_above_the_readings", S, F, "#define LVGL_PRIORITY       4\n", "#define LVGL_PRIORITY       7\n"),
    ("screen_below_the_web_server", S, F, "#define SCREEN_PRIORITY     6\n", "#define SCREEN_PRIORITY     5\n"),
]
