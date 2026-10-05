"""Mutations of display/main/main.c, see ../redproof.py. The simulation is ../main_sim.c."""

F = "main/main.c"
S = "main_sim"

# platform_events(): what an event names is copied when the event is taken
COPY_WIFI = "\t\tmemcpy(kept->profiles, app->profiles, sizeof(kept->profiles));\n"
COPY_WIFI_COUNT = "\t\tkept->profile_count = app->profile_count;\n"
COPY_BOUND = "\t\tmemcpy(kept->bound, app->poll.bound_id, (size_t)kept->bound_length);\n"
COPY_LAYOUT_LENGTH = "\t\tkept->layout_length = (int)app->layout_length;"
COPY_CATALOG = "\t\tkept->catalog_length = catalog_to_json(&app->poll.catalog, kept->catalog, sizeof(kept->catalog));\n"
COPY_OLD = "\t\tmemcpy(kept->old, app->poll.old_text, (size_t)kept->old_length);\n"
SAVE_UNDOES_RESET = "\t\tspare->events &= ~APP_EVENT_ERASE_LAYOUT;\n"
RESET_UNDOES_SAVE = "\t\tspare->events &= ~APP_EVENT_STORE_LAYOUT;\n"
SPARE_ADDS = "\tspare->events |= events;\n"
JOB_COUNTED = "\t\t// Counted before the main task can have it. The queue has as many places as there are jobs.\n" \
              "\t\tatomic_fetch_add(&jobs_out, 1);\n"

# platform_stored()
STORED_HANDS_ON = "\t\tplatform_lock();\n\t\tplatform_events();\n\t\twaits = spare->events != 0;\n"
STORED_WAITS = "\t\tif(!waits && atomic_load(&jobs_out) == 0)\n"

# platform_upload_begun(), upload_left_behind(), holds_firmware()
BEGUN_CLEARS = "\tatomic_store(&upload_complete, false);\n"
BEGUN_REFUSES = "\t\tESP_LOGE(TAG, \"upload: its begin cannot be recorded, so it does not begin\");\n\t\treturn false;\n"
RECORD_READ = "\tif(store_read_text(STORE_DATA, KEY_UPLOAD, label, sizeof(label)) < 0)"
RECORD_OTHER = "if(strcmp(label, running->label) != 0)"
HOLDS = "\treturn slot != NULL && esp_ota_get_partition_description(slot, &description) == ESP_OK &&\n" \
        "\t       strncmp(description.project_name, project, sizeof(description.project_name)) == 0;"

# The start
USB_NO_CRASH = "\t\tcase ESP_RST_SW:\n\t\tcase ESP_RST_USB: return GUARD_RESET_SOFTWARE;"
KNOB_GAP = "\t\t\tvTaskDelay(pdMS_TO_TICKS(KNOB_HELD_GAP_MS));\n"
KNOB_READING = "\t\tif(!board_button(&pressed) || !pressed)"
KNOB_HELD = "\treturn true;\n}\n\n/*\n * The password"
RANDOM_ON = "\tbootloader_random_enable();\n"
RANDOM_OFF = "\tbootloader_random_disable();\n"
PASSWORD_STORED = "\tstore_write(STORE_CFG, STORE_KEY_AP_PASSWORD, ap_password, AP_PASSWORD_LENGTH);\n"
READ_SETTINGS = "\t\tboot->settings_json = kept->settings;\n"
READ_WIFI = "\t\tboot->profiles = kept->profiles;\n"
READ_BOUND = "\t\tboot->bound_id = kept->bound;\n"
READ_LAYOUT_WHICH = "previous_layout ? STORE_KEY_LAYOUT_PREV : STORE_KEY_LAYOUT"
READ_LAYOUT_LENGTH = "boot->layout_length = (size_t)length;"
READ_CATALOG = "\t\tboot->catalog_json = kept->catalog;\n"
READ_OLD = "\t\tboot->old_text = kept->old;\n"
GIVE_UP_DARK = "\tatomic_store(&restarting, true);\n\tvTaskDelay(pdMS_TO_TICKS(GIVE_UP_MS));\n"
PENDING_GIVES_UP = "if(needed || update_pending)"
PENDING = "state == ESP_OTA_IMG_PENDING_VERIFY;"
ROLLED_BACK = "(state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED)"
BOARD_NEEDED = "step_failed(\"board\", err, true);"
PREVIOUS = "boot.previous_firmware = !upload_left_behind(running) && holds_firmware(other_slot);"
ROOM_APP = "\tplatform_app = heap_caps_malloc(sizeof(app_t), MALLOC_CAP_SPIRAM);\n"
ROOM_JOBS = "\tjobs = heap_caps_calloc(JOBS + 1, sizeof(job_t), MALLOC_CAP_SPIRAM);\n"
NO_NETWORK = "\t\tstep_failed(\"network\", err, false);\n\t\treturn;\n"
WATCHED = "\tnet_seen_ms = platform_now_ms();\n\tnet_watched = true;\n"

# keep(), store_layout(), carry_out()
KEEP_EMPTY = "\telse if(length > 0)\n"
PREV_WRITTEN = "\tkeep(STORE_DATA, STORE_KEY_LAYOUT_PREV, layout_room, before < 0 ? 0 : before);\n"
LAYOUT_WRITTEN = "\tif(store_write(STORE_DATA, STORE_KEY_LAYOUT, kept->layout, (size_t)kept->layout_length))\n"
GUARD_TOLD = "\t\tguard_layout_stored(&guard_memory);\n"
ALIVE_LATER = "\t\talive_at_ms = now_ms + GUARD_ALIVE_MS;\n"
DO_SETTINGS = "\t\tkeep(STORE_CFG, STORE_KEY_SETTINGS, kept->settings, kept->settings_length);\n"
DO_WIFI = "\t\tkeep(STORE_CFG, STORE_KEY_WIFI, kept->profiles, kept->profile_count * (int)sizeof(net_profile_t));\n"
DO_BOUND = "\t\tkeep(STORE_CFG, STORE_KEY_BOUND, kept->bound, kept->bound_length);\n"
DO_ERASE = "\t\tkeep(STORE_DATA, STORE_KEY_LAYOUT, NULL, 0);\n"
DO_CATALOG = "\t\tkeep(STORE_DATA, STORE_KEY_CATALOG, kept->catalog, kept->catalog_length);\n"
DO_OLD = "\t\tkeep(STORE_DATA, STORE_KEY_DTC_OLD, kept->old, kept->old_length);\n"
DO_VALID = "esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();"
DO_RESET = "if((events & APP_EVENT_FACTORY_RESET) && !store_erase_space(STORE_CFG))"
DO_PREVIOUS = "\tif(events & APP_EVENT_PREVIOUS_FIRMWARE)\n\t{\n\t\tboot_other_slot();\n\t}\n"
DO_INSTALL = "if(atomic_load(&upload_complete))"
JOB_BACK = "\txQueueSend(free_jobs, &job, 0);\n\tatomic_fetch_sub(&jobs_out, 1);\n"

# restart()
RESTART_RESET = "\tbool reset = job != NULL && (job->events & APP_EVENT_FACTORY_RESET) != 0;\n"
RESTART_HEAD = "\tatomic_fetch_add(&jobs_out, 1);\n\tatomic_store(&restarting, true);\n\tvTaskDelay(pdMS_TO_TICKS(RESTART_MS));\n"
RESTART_REST = "\twhile(!reset)\n"
RESTART_TAKES = "\t\tplatform_lock();\n\t\tplatform_events();\n\t\tplatform_unlock();\n" \
                "\t\tif(xQueueReceive(waiting_jobs, &job, 0) != pdTRUE)\n"
RESTART_SECOND_RESET = "\t\treset = (job->events & APP_EVENT_FACTORY_RESET) != 0;\n"

# every_second(), main_task()
TOLD_SLOT = "\ttold.slot = platform_info.slot;\n"
TOLD_TEMPERATURE = "\tapp_temperature(platform_app, celsius, measured);\n"
ALIVE = "\t\tguard_alive(&guard_memory);\n"
ALIVE_DUE = "if(alive_due && now_ms >= alive_at_ms)"
NET_WATCHED = "\tif(net_watched)\n"
NET_TURNED = "\t\tif(turns != net_seen)\n"
NET_STALLED = "\t\telse if(now_ms - net_seen_ms >= NET_STALL_MS)\n"
CARRY_OUT = "\t\t\tcarry_out(job, platform_now_ms());\n\t\t\tif(job->events & RESTART_EVENTS)\n"

MUTATIONS = [
    # what an event names, copied when it is taken
    ("main_wifi_list_not_copied", S, F, COPY_WIFI, ""),
    ("main_wifi_count_not_copied", S, F, COPY_WIFI_COUNT, ""),
    ("main_bound_not_copied", S, F, COPY_BOUND, ""),
    ("main_layout_copied_short", S, F, COPY_LAYOUT_LENGTH, "\t\tkept->layout_length = (int)app->layout_length - 1;"),
    ("main_catalog_not_copied", S, F, COPY_CATALOG, ""),
    ("main_old_list_not_copied", S, F, COPY_OLD, ""),
    ("main_layout_read_from_the_app_when_stored", S, F, LAYOUT_WRITTEN,
     "\tif(store_write(STORE_DATA, STORE_KEY_LAYOUT, platform_app->layout_text, platform_app->layout_length))\n"),
    ("main_reset_then_save_erases_the_save", S, F, SAVE_UNDOES_RESET, ""),
    ("main_save_then_reset_stores_the_save", S, F, RESET_UNDOES_SAVE, ""),
    ("main_later_call_forgets_what_found_no_room", S, F, SPARE_ADDS, "\tspare->events = events;\n"),
    ("main_job_not_counted", S, F, JOB_COUNTED, ""),

    # platform_stored()
    ("main_stored_does_not_hand_on", S, F, STORED_HANDS_ON, "\t\tplatform_lock();\n\t\twaits = spare->events != 0;\n"),
    ("main_stored_waits_for_nothing", S, F, STORED_WAITS, "\t\tif(waits || !waits)\n"),
    ("main_job_never_back", S, F, JOB_BACK, "\txQueueSend(free_jobs, &job, 0);\n"),

    # the record of an upload
    ("main_begun_keeps_complete", S, F, BEGUN_CLEARS, ""),
    ("main_begun_without_its_record", S, F, BEGUN_REFUSES,
     "\t\tESP_LOGE(TAG, \"upload: its begin cannot be recorded, so it does not begin\");\n\t\treturn true;\n"),
    ("main_upload_record_never_read", S, F, RECORD_READ, "\tif(true)"),
    ("main_upload_record_inverted", S, F, RECORD_OTHER, "if(strcmp(label, running->label) == 0)"),
    ("main_upload_record_not_asked", S, F, PREVIOUS,
     "boot.previous_firmware = ((void)upload_left_behind, holds_firmware(other_slot));"),
    ("main_every_slot_holds_a_firmware", S, F, HOLDS, "\t(void)project; (void)description;\n\treturn slot != NULL;"),

    # the start
    ("main_usb_reset_is_a_crash", S, F, USB_NO_CRASH, "\t\tcase ESP_RST_SW: return GUARD_RESET_SOFTWARE;"),
    ("main_knob_readings_without_a_gap", S, F, KNOB_GAP, "\t\t\t(void)0;\n"),
    ("main_knob_failed_reading_counts_as_held", S, F, KNOB_READING, "\t\tif(board_button(&pressed) && !pressed)"),
    ("main_knob_never_held", S, F, KNOB_HELD, "\treturn false;\n}\n\n/*\n * The password"),
    ("main_knob_read_once", S, F, "#define KNOB_HELD_READINGS  5\n", "#define KNOB_HELD_READINGS  1\n"),
    ("main_password_without_entropy", S, F, RANDOM_ON, ""),
    ("main_entropy_source_left_on", S, F, RANDOM_OFF, ""),
    ("main_password_not_stored", S, F, PASSWORD_STORED, ""),
    ("main_ssid_from_other_bytes", S, F, "mac[4], mac[5]);", "mac[0], mac[1]);"),
    ("main_settings_not_read", S, F, READ_SETTINGS, ""),
    ("main_wifi_not_read", S, F, READ_WIFI, ""),
    ("main_bound_not_read", S, F, READ_BOUND, ""),
    ("main_previous_layout_ignored", S, F, READ_LAYOUT_WHICH, "STORE_KEY_LAYOUT"),
    ("main_layout_read_short", S, F, READ_LAYOUT_LENGTH, "boot->layout_length = (size_t)length - 1;"),
    ("main_catalog_not_read", S, F, READ_CATALOG, ""),
    ("main_old_list_not_read", S, F, READ_OLD, ""),
    ("main_give_up_at_once", S, F, GIVE_UP_DARK, "\tatomic_store(&restarting, true);\n"),
    ("main_give_up_with_the_light_on", S, F, GIVE_UP_DARK, "\tvTaskDelay(pdMS_TO_TICKS(GIVE_UP_MS));\n"),
    ("main_pending_update_goes_on", S, F, PENDING_GIVES_UP, "if(needed)"),
    ("main_pending_never", S, F, PENDING, "false;"),
    ("main_rolled_back_never", S, F, ROLLED_BACK, "false"),
    ("main_board_failure_goes_on", S, F, BOARD_NEEDED, "step_failed(\"board\", err, false);"),
    ("main_app_in_the_internal_ram", S, F, ROOM_APP,
     "\tplatform_app = heap_caps_malloc(sizeof(app_t), MALLOC_CAP_INTERNAL);\n"),
    ("main_jobs_not_zeroed", S, F, ROOM_JOBS,
     "\tjobs = heap_caps_malloc((JOBS + 1) * sizeof(job_t), MALLOC_CAP_SPIRAM);\n"),
    ("main_task_on_the_first_core", S, F, "#define PANEL_CORE          (configNUMBER_OF_CORES - 1)\n",
     "#define PANEL_CORE          0\n"),
    ("main_web_started_without_network", S, F, NO_NETWORK, "\t\tstep_failed(\"network\", err, false);\n"),
    ("main_network_not_watched", S, F, WATCHED, "\tnet_seen_ms = platform_now_ms();\n"),

    # storing
    ("main_empty_list_stored_as_a_value", S, F, KEEP_EMPTY, "\telse if(length >= 0)\n"),
    ("main_prev_not_written", S, F, PREV_WRITTEN, "\t(void)before;\n"),
    ("main_prev_not_erased_when_none", S, F, PREV_WRITTEN,
     "\tkeep(STORE_DATA, STORE_KEY_LAYOUT_PREV, layout_room, before);\n"),
    ("main_guard_not_told_of_layout", S, F, GUARD_TOLD, ""),
    ("main_alive_not_postponed", S, F, ALIVE_LATER, ""),
    ("main_settings_not_stored", S, F, DO_SETTINGS, ""),
    ("main_wifi_not_stored", S, F, DO_WIFI, ""),
    ("main_bound_not_stored", S, F, DO_BOUND, ""),
    ("main_layout_not_erased", S, F, DO_ERASE, ""),
    ("main_catalog_not_stored", S, F, DO_CATALOG, ""),
    ("main_old_list_not_stored", S, F, DO_OLD, ""),
    ("main_not_marked_valid", S, F, DO_VALID, "esp_err_t err = ESP_OK;"),
    ("main_factory_reset_erases_nothing", S, F, DO_RESET, "if(false)"),
    ("main_previous_firmware_not_booted", S, F, DO_PREVIOUS, ""),
    ("main_install_without_complete", S, F, DO_INSTALL, "if(true)"),
    ("main_stored_under_the_lock", S, F, CARRY_OUT,
     "\t\t\tplatform_lock();\n\t\t\tcarry_out(job, platform_now_ms());\n\t\t\tplatform_unlock();\n"
     "\t\t\tif(job->events & RESTART_EVENTS)\n"),

    # the restart
    ("main_no_restart", S, F, CARRY_OUT, "\t\t\tcarry_out(job, platform_now_ms());\n\t\t\tif(false)\n"),
    ("main_restart_at_once", S, F, RESTART_HEAD,
     "\tatomic_fetch_add(&jobs_out, 1);\n\tatomic_store(&restarting, true);\n"),
    ("main_restart_with_the_light_on", S, F, RESTART_HEAD,
     "\tatomic_fetch_add(&jobs_out, 1);\n\tvTaskDelay(pdMS_TO_TICKS(RESTART_MS));\n"),
    ("main_restart_holds_nobody_back", S, F, RESTART_HEAD,
     "\tatomic_store(&restarting, true);\n\tvTaskDelay(pdMS_TO_TICKS(RESTART_MS));\n"),
    ("main_restart_carries_out_nothing_more", S, F, RESTART_REST, "\twhile(reset && !reset)\n"),
    ("main_restart_loses_what_found_no_room", S, F, RESTART_TAKES,
     "\t\tif(xQueueReceive(waiting_jobs, &job, 0) != pdTRUE)\n"),
    ("main_stored_behind_a_factory_reset", S, F, RESTART_RESET, "\tbool reset = false;\n"),
    ("main_stored_behind_a_second_factory_reset", S, F, RESTART_SECOND_RESET, "\t\treset = false;\n"),

    # once a second
    ("main_slot_not_told", S, F, TOLD_SLOT, ""),
    ("main_temperature_not_told", S, F, TOLD_TEMPERATURE, "\t(void)measured;\n"),
    ("main_guard_alive_never", S, F, ALIVE, ""),
    ("main_guard_alive_at_once", S, F, ALIVE_DUE, "if(alive_due)"),
    ("main_watched_without_a_network", S, F, NET_WATCHED, "\tif(net_watched || true)\n"),
    ("main_turns_not_looked_at", S, F, NET_TURNED, "\t\tif(false)\n"),
    ("main_stall_not_noticed", S, F, NET_STALLED, "\t\telse if(false)\n"),
    ("main_stall_after_six_seconds", S, F, "#define NET_STALL_MS        60000u\n", "#define NET_STALL_MS        6000u\n"),
]
