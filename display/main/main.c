/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "bootloader_random.h"
#include "platform.h"
#include "board.h"
#include "store.h"

/*
 * The start of the display, and the task that carries out what the app asks for (platform.h).
 *
 * Written without the board and without a compiler for it: every call was read in the sources of ESP-IDF
 * v5.5.2. What only the board can decide is marked CHECK.
 *
 * The start, in this order, and what happens when a step fails. "Gives up" means: the reason is logged, and
 * after GIVE_UP_MS the display restarts and tries again. A firmware that runs for the first time after an
 * update (update_pending) gives up on EVERY failed step: it has not passed, and the restart is what makes
 * the boot loader take the update back.
 *   1 the boot loader    which slot runs, whether it waits to be confirmed, whether the last update was taken
 *                        back. Asked first: the steps below need to know whether an update is pending.
 *   2 store_init()       fails: the display goes on with what is built in. Every read then finds nothing,
 *                        every write fails and says so; the access point gets a new password with every start.
 *   3 the access point   its password, made once. Before the board, because the random numbers need the ADC
 *                        that the temperature sensor of the board takes afterwards (see ap_password_make()).
 *   4 board_init()       fails (the port expander does not answer): no panel, no knob. Gives up.
 *   5 the knob           held through KNOB_HELD_READINGS readings: safe mode. A reading that fails is not held.
 *   6 guard_start()      with the reason of the reset and the counter in the RTC memory
 *   7 the rooms          app_t, the tokens, the jobs: once, from the external RAM. Fails: gives up.
 *   8 what is stored     read into the room of a job (kept_t), which nothing uses before the tasks run
 *   9 app_init()         cannot fail: what it cannot read counts as not stored
 *  10 board_panel_start(), screen_start()
 *                        fail: no picture. Gives up. From here on the screen task runs the app.
 *  11 net_start()        fails: the display runs without a network and says "no WiFi"; the web server is not
 *                        started on top of a network that did not come up.
 *  12 web_start()        fails: the display runs without its web interface.
 * The app comes before the panel because the screen task needs it from its first round on; nothing is to be
 * seen before that anyway, the backlight is dark until the screen task switches it on.
 */

#define TAG "main"

/*
 * The task of this file is not the one that calls app_main(): that one has the stack and the core that
 * sdkconfig gives it (3584 bytes and core 0 unless set otherwise), and both matter here.
 *
 * The stack: this task writes blobs of 16 KB to the NVS and lets the boot loader support check a whole image
 * (esp_ota_set_boot_partition()).
 *
 * The core: the interrupts of the panel run on the core of the task that calls board_panel_start() (board.c),
 * and LVGL and the screen task follow them there (screen.c). Core 0 is where the WiFi driver and the timer of
 * ESP-IDF have their tasks unless sdkconfig says otherwise; the panel gets the other one. The notes on
 * performance of esp_lvgl_port (docs/performance.md) name the second core for the main task as a gain as well.
 * CHECK: the picture stands still and the knob answers at once while the WiFi carries traffic. If the picture
 * is better with everything on core 0, it is this one number.
 * CHECK: uxTaskGetStackHighWaterMark() of this task after a layout was stored and after "Vorherige Version".
 *
 * The priority is below LVGL (4) and the screen task (6): what this task does can wait for a picture.
 */
#define MAIN_STACK          8192
#define MAIN_PRIORITY       3
#define PANEL_CORE          (configNUMBER_OF_CORES - 1)

#define SECOND_MS           1000u
// CHECK: the browser has the answer to POST /api/reboot before the display is gone. If not, this is too short.
#define RESTART_MS          500u
// The network task has not come round for this long: it is stuck (every_second()). Its longest turn that
// is in order: a query for the adapter (3 s), a network left (2.5 s), a request with the lookup of a name
// before it (4 s and what lwIP takes for the lookup, which net.c has a CHECK for) - far below this.
#define NET_STALL_MS        60000u
// Long enough to read the log, and to keep a display that cannot start from restarting without a pause
#define GIVE_UP_MS          5000u
#define KNOB_HELD_READINGS  5
#define KNOB_HELD_GAP_MS    20u

// The password of the own access point: typed off the screen of the display into a phone, so without the
// characters that look like another one (0 O, 1 l I). 31 characters, 10 of them: 49 bits.
#define AP_PASSWORD_LENGTH  10
#define AP_PASSWORD_MIN     8           // WPA2 takes nothing shorter: a stored one below this is made anew
static const char ap_password_set[] = "abcdefghjkmnpqrstuvwxyz23456789";

// Room for the stored settings. More than settings_to_json() writes (SETTINGS_JSON_SIZE): a newer firmware
// may have stored more members, and after its update was taken back this firmware reads that text.
#define SETTINGS_ROOM       512
// Room for the catalogue as JSON. app_web.h reckons 18433 bytes for the largest catalogue whose texts hold no
// control characters; one that is larger is not stored, as it is not served.
#define CATALOG_ROOM        20480
#define BOUND_ROOM          sizeof(((const poll_t *)0)->bound_id)

// Which slot an upload began to write, as the label of the partition: see upload_left_behind(). Not a key of
// store.h, and in STORE_DATA: a factory reset must not make the remains of an upload a "previous version".
#define KEY_UPLOAD          "upload"

/*
 * What the flash holds of the app, as it is stored: read into one of these at the start, and filled by
 * platform_events() with what an event names, at the moment the event is taken. A length of -1 means that
 * the text could not be made.
 */
typedef struct
{
	char settings[SETTINGS_ROOM];
	int settings_length;
	net_profile_t profiles[NET_PROFILES_MAX];
	int profile_count;
	char bound[BOUND_ROOM];
	int bound_length;
	char layout[LAYOUT_TEXT_MAX + 1];
	int layout_length;
	char catalog[CATALOG_ROOM];
	int catalog_length;
	char old[POLL_TEXT_SIZE];
	int old_length;
} kept_t;

typedef struct
{
	uint32_t events;
	kept_t kept;
} job_t;

// 43 KB each. A job waits while the one before it is written to the flash, which takes longer than a request
// of the browser. There is one room more than these: the one platform_events() fills next (`spare`).
#define JOBS                4

#define RESTART_EVENTS      (APP_EVENT_REBOOT | APP_EVENT_FACTORY_RESET | APP_EVENT_PREVIOUS_FIRMWARE | \
                             APP_EVENT_INSTALL_FIRMWARE)

// display/layouts/w906_default.json as main/CMakeLists.txt embeds it (EMBED_FILES): the build names the two
// ends after the file, and puts no zero behind it
extern const char builtin_start[] __asm__("_binary_w906_default_json_start");
extern const char builtin_end[] __asm__("_binary_w906_default_json_end");

app_t *platform_app;
platform_info_t platform_info;

static SemaphoreHandle_t app_lock;
static QueueHandle_t free_jobs;         // job_t *: the rooms nobody uses
static QueueHandle_t waiting_jobs;      // job_t *: in the order the events came
static job_t *spare;                    // the room platform_events() fills next; under the lock of the app
static atomic_int jobs_out;             // jobs that went into waiting_jobs and are not carried out yet
static char *layout_room;               // LAYOUT_TEXT_MAX + 1 bytes: the stored layout while it becomes the one before
static const esp_partition_t *other_slot;   // the app slot that does not run
static bool update_pending;             // as the boot loader said at the start
static atomic_bool upload_complete;     // set by the web server, read by this task
static atomic_bool restarting;          // set by this task, read by the screen task (platform_restarting())
static bool net_watched;                // the network task runs: every_second() looks whether it still turns
static uint32_t net_seen;               // net_turns() as it was last seen to change
static uint64_t net_seen_ms;            // and when
static char ap_ssid[NET_SSID_SIZE];
static char ap_password[NET_PASSWORD_SIZE];

/*
 * The counter of guard.h. This section is in the RTC memory and is neither loaded nor cleared when the
 * firmware starts (esp_attr.h, sections.ld.in): it holds what the start before left there, and anything
 * after a loss of power - which is why guard.h checks it. Only this task touches it.
 * CHECK: three panics in a row, each in the first minute, end in the safe mode; pulling the plug in between
 * starts the count anew.
 */
static RTC_NOINIT_ATTR guard_memory_t guard_memory;
static bool alive_due = true;                   // guard_alive() is still to be called
static uint64_t alive_at_ms = GUARD_ALIVE_MS;   // and when: time is counted from the start of the chip

void platform_lock(void)
{
	xSemaphoreTake(app_lock, portMAX_DELAY);
}

void platform_unlock(void)
{
	xSemaphoreGive(app_lock);
}

uint64_t platform_now_ms(void)
{
	return (uint64_t)esp_timer_get_time() / 1000;
}

/*
 * Called under the lock by whoever called into the app. What the events name is copied here and now, so a
 * later change of the app (a preview after a save) does not reach the flash. The copies are at most a layout
 * of 16 KB within the external RAM and a catalogue written as JSON: computing, and nothing that waits.
 *
 * The copies go into the spare room, and the spare room into the queue if one of the JOBS is free: that one
 * is the spare room from then on. If none is free - the main task is behind by all of them - the spare room
 * keeps what it has and takes what comes on top. Nothing ever stays with the app, where a later call would
 * find the events but not what they named when they were raised (a save followed by an apply would store
 * the preview), and not the order they came in.
 */
void platform_events(void)
{
	app_t *app = platform_app;
	kept_t *kept = &spare->kept;
	uint32_t events;
	job_t *room;

	// Nothing raised and nothing waiting for a room: the usual case, at every reading of the knob
	if(app->events == 0 && spare->events == 0)
	{
		return;
	}
	events = app_take_events(app);

	if(events & APP_EVENT_STORE_SETTINGS)
	{
		kept->settings_length = settings_to_json(&app->settings, kept->settings, sizeof(kept->settings));
	}
	if(events & APP_EVENT_STORE_WIFI)
	{
		memcpy(kept->profiles, app->profiles, sizeof(kept->profiles));
		kept->profile_count = app->profile_count;
	}
	if(events & APP_EVENT_STORE_BOUND)
	{
		kept->bound_length = (int)strnlen(app->poll.bound_id, sizeof(kept->bound) - 1);
		memcpy(kept->bound, app->poll.bound_id, (size_t)kept->bound_length);
	}
	if(events & APP_EVENT_STORE_LAYOUT)
	{
		kept->layout_length = (int)app->layout_length;
		memcpy(kept->layout, app->layout_text, app->layout_length);
	}
	if(events & APP_EVENT_STORE_CATALOG)
	{
		kept->catalog_length = catalog_to_json(&app->poll.catalog, kept->catalog, sizeof(kept->catalog));
	}
	if(events & APP_EVENT_STORE_OLD)
	{
		kept->old_length = (int)strnlen(app->poll.old_text, sizeof(kept->old) - 1);
		memcpy(kept->old, app->poll.old_text, (size_t)kept->old_length);
	}

	// Only when the spare room held events already: a later event of a kind has replaced the copy of the
	// earlier one above, as its write would replace the other's in the flash. The two events of the layout
	// undo each other, and one job has no order for them (carry_out()): the later one alone counts, or a
	// reset followed by a save would erase the save.
	if(events & APP_EVENT_STORE_LAYOUT)
	{
		spare->events &= ~APP_EVENT_ERASE_LAYOUT;
	}
	else if(events & APP_EVENT_ERASE_LAYOUT)
	{
		spare->events &= ~APP_EVENT_STORE_LAYOUT;
	}
	spare->events |= events;

	if(xQueueReceive(free_jobs, &room, 0) == pdTRUE)
	{
		// Counted before the main task can have it. The queue has as many places as there are jobs.
		atomic_fetch_add(&jobs_out, 1);
		xQueueSend(waiting_jobs, &spare, 0);
		spare = room;
		spare->events = 0;
	}
}

/*
 * Called WITHOUT the lock. "Carried out" is: no event waits for a room, and every job is back from the main
 * task. Once a restart is under way that is never the case again (restart()): whoever waits here then
 * waits until the display is gone, and begins nothing new before.
 */
void platform_stored(void)
{
	for(;;)
	{
		bool waits;

		// What found no room goes into the queue as soon as there is one, also when nobody raises anything
		platform_lock();
		platform_events();
		waits = spare->events != 0;
		platform_unlock();
		if(!waits && atomic_load(&jobs_out) == 0)
		{
			return;
		}
		vTaskDelay(1);
	}
}

bool platform_restarting(void)
{
	return atomic_load(&restarting);
}

/*
 * Called by the web server WITHOUT the lock, before it erases the other slot: this writes to the flash, and
 * has written when it returns true. false: there is no record, and the web server must not touch the slot -
 * what an upload left there would count as the previous version after a restart (upload_left_behind()).
 */
bool platform_upload_begun(void)
{
	atomic_store(&upload_complete, false);
	if(other_slot == NULL || !store_write(STORE_DATA, KEY_UPLOAD, other_slot->label, strlen(other_slot->label)))
	{
		ESP_LOGE(TAG, "upload: its begin cannot be recorded, so it does not begin");
		return false;
	}
	return true;
}

void platform_upload_complete(void)
{
	atomic_store(&upload_complete, true);
}

/*
 * true while the other slot holds what an upload left there - half a firmware, or a whole one that nobody
 * confirmed at the knob - and not the version before the running one.
 *
 * The record names the slot that was written. It ends at the first start of a firmware that runs from that
 * very slot: then the upload was installed, and the other slot is the one it was installed from. So the
 * record outlasts every restart in between, a failed installation as well, and needs no step that could be
 * lost between setting the boot partition and the restart.
 */
static bool upload_left_behind(const esp_partition_t *running)
{
	char label[sizeof(running->label)];

	if(store_read_text(STORE_DATA, KEY_UPLOAD, label, sizeof(label)) < 0)
	{
		return false;
	}
	if(strcmp(label, running->label) != 0)
	{
		return true;
	}
	if(!store_erase(STORE_DATA, KEY_UPLOAD))
	{
		ESP_LOGE(TAG, "upload: the record of the installed firmware is not removed");
	}
	return false;
}

/*
 * Whether a slot begins like a firmware of this project: the application description is where it has to be
 * and names the project. This is not the check sum of the image. That one is checked when the slot is made
 * the one to boot (esp_ota_set_boot_partition()) and again by the boot loader before it starts the image;
 * checking it here as well would read and hash megabytes at every start, for a row of a menu.
 * CHECK: if esp_image_verify() over the other slot turns out to take no time worth mentioning, it belongs
 * here, and "Vorherige Version" is then only offered for an image that is whole.
 */
static bool holds_firmware(const esp_partition_t *slot)
{
	const char *project = esp_app_get_description()->project_name;
	esp_app_desc_t description;

	return slot != NULL && esp_ota_get_partition_description(slot, &description) == ESP_OK &&
	       strncmp(description.project_name, project, sizeof(description.project_name)) == 0;
}

/*
 * The reasons esp_reset_reason() can give on the ESP32-S3 (esp_system/port/soc/esp32s3/reset_reason.c).
 * The reset pin of the chip counts as power on there: the chip cannot tell the two apart.
 */
static const char *reset_word(esp_reset_reason_t reason)
{
	switch(reason)
	{
		case ESP_RST_POWERON: return "poweron";
		case ESP_RST_SW: return "software";
		case ESP_RST_PANIC: return "panic";
		case ESP_RST_INT_WDT: return "int_wdt";
		case ESP_RST_TASK_WDT: return "task_wdt";
		case ESP_RST_WDT: return "wdt";
		case ESP_RST_BROWNOUT: return "brownout";
		case ESP_RST_USB: return "usb";
		case ESP_RST_DEEPSLEEP: return "deepsleep";
		default: return "unknown";
	}
}

/*
 * What the reason means for the counter of crashes. A restart the firmware asked for, and one the USB
 * interface asked for (the reset after flashing), is no crash and leaves the RTC memory as it was. Everything
 * else that is not the power coming on counts as a crash, also what has no name (guard.h: a safe mode too
 * many locks nobody out).
 * CHECK: what the log names after the reset button, after a panic, after the plug was pulled, and after
 * the engine was started with the display on the vehicle battery (a brown-out counts as a crash).
 */
static guard_reset_t guard_reset_of(esp_reset_reason_t reason)
{
	switch(reason)
	{
		case ESP_RST_POWERON: return GUARD_RESET_POWER_ON;
		case ESP_RST_SW:
		case ESP_RST_USB: return GUARD_RESET_SOFTWARE;
		default: return GUARD_RESET_CRASH;
	}
}

/*
 * CHECK: a knob that is pressed before the power comes on and kept pressed until the picture shows gives the
 * safe mode (the readings are taken a second or two after power on: boot loader and memory test come first).
 * CHECK: the log line of start() says "safe mode 0" after a start without a hand on the knob. The switch is
 * read here while the expander still keeps the panel without power (board.c, EXPANDER_IDLE): if the pull-up
 * of the switch hangs on that rail, it reads pressed now, and every start is a safe mode. Then the readings
 * have to move behind board_panel_start().
 */
static bool knob_held(void)
{
	for(int i = 0; i < KNOB_HELD_READINGS; i++)
	{
		bool pressed = false;

		if(i > 0)
		{
			vTaskDelay(pdMS_TO_TICKS(KNOB_HELD_GAP_MS));
		}
		if(!board_button(&pressed) || !pressed)
		{
			return false;
		}
	}
	return true;
}

/*
 * The password of the own access point, read or made. It is made before the WiFi runs, and without the WiFi
 * the random numbers of the chip are only true ones while the entropy source of the boot loader is switched
 * on (ESP-IDF, "Random Number Generation"). That source is the SAR ADC and has to be off again before
 * anything else uses the ADC: so this stands before board_init(), which starts the temperature sensor. The
 * boot loader has switched the source on and off in the same way before it started this firmware.
 * CHECK: after a first start (or a factory reset) the temperature on the info page is as plausible as after
 * any other start.
 */
static void ap_password_make(void)
{
	if(store_read_text(STORE_CFG, STORE_KEY_AP_PASSWORD, ap_password, sizeof(ap_password)) >= AP_PASSWORD_MIN)
	{
		return;
	}

	bootloader_random_enable();
	for(int i = 0; i < AP_PASSWORD_LENGTH; i++)
	{
		ap_password[i] = ap_password_set[esp_random() % (sizeof(ap_password_set) - 1)];
	}
	bootloader_random_disable();
	ap_password[AP_PASSWORD_LENGTH] = '\0';

	// If this fails the password is another one after the next start; the screen shows the one that is valid
	store_write(STORE_CFG, STORE_KEY_AP_PASSWORD, ap_password, AP_PASSWORD_LENGTH);
}

// "WiCAN-Display-" and the last four digits of the address the display has as a station
static void ap_ssid_make(void)
{
	uint8_t mac[6] = { 0 };

	if(esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
	{
		ESP_LOGE(TAG, "no MAC address: the access point is named without it");
	}
	snprintf(ap_ssid, sizeof(ap_ssid), "WiCAN-Display-%02X%02X", mac[4], mac[5]);
}

/*
 * Reads what is stored. What is not there, does not fit or cannot be read stays NULL in *boot: the app
 * counts that as not stored. Everything here is copied or read by app_init(), nothing of it has to stay.
 */
static void read_stored(kept_t *kept, bool previous_layout, app_boot_t *boot)
{
	int length;

	if(store_read_text(STORE_CFG, STORE_KEY_SETTINGS, kept->settings, sizeof(kept->settings)) > 0)
	{
		boot->settings_json = kept->settings;
	}

	// The list as it is in the app, as many entries as were in use
	length = store_read(STORE_CFG, STORE_KEY_WIFI, kept->profiles, sizeof(kept->profiles));
	if(length > 0 && (size_t)length % sizeof(net_profile_t) == 0)
	{
		boot->profiles = kept->profiles;
		boot->profile_count = (int)((size_t)length / sizeof(net_profile_t));
	}

	if(store_read_text(STORE_CFG, STORE_KEY_BOUND, kept->bound, sizeof(kept->bound)) > 0)
	{
		boot->bound_id = kept->bound;
	}

	// After a crash that followed a save: the layout before it, and none if there was none before it. Never
	// the one that was just stored.
	length = store_read_text(STORE_DATA, previous_layout ? STORE_KEY_LAYOUT_PREV : STORE_KEY_LAYOUT, kept->layout,
	                         sizeof(kept->layout));
	if(length > 0)
	{
		boot->layout_text = kept->layout;
		boot->layout_length = (size_t)length;
	}

	length = store_read_text(STORE_DATA, STORE_KEY_CATALOG, kept->catalog, sizeof(kept->catalog));
	if(length > 0)
	{
		boot->catalog_json = kept->catalog;
		boot->catalog_length = (size_t)length;
	}

	length = store_read_text(STORE_DATA, STORE_KEY_DTC_OLD, kept->old, sizeof(kept->old));
	if(length > 0)
	{
		boot->old_text = kept->old;
		boot->old_length = (size_t)length;
	}
}

// The display cannot go on. The restart is one the firmware asked for: it is no crash to guard.h, and it is
// what takes back an update that is not confirmed.
static void __attribute__((noreturn)) give_up(void)
{
	ESP_LOGE(TAG, "restart in %u s", (unsigned)(GIVE_UP_MS / 1000));
	// If the screen task runs already, it switches the backlight off (restart() says why)
	atomic_store(&restarting, true);
	vTaskDelay(pdMS_TO_TICKS(GIVE_UP_MS));
	esp_restart();
}

// A step of the start failed. needed: the display is of no use without it.
static void step_failed(const char *step, esp_err_t err, bool needed)
{
	ESP_LOGE(TAG, "%s failed: %s", step, esp_err_to_name(err));
	if(needed || update_pending)
	{
		give_up();
	}
}

static void start(void)
{
	const esp_app_desc_t *description = esp_app_get_description();
	const esp_partition_t *running = esp_ota_get_running_partition();
	const esp_reset_reason_t reason = esp_reset_reason();
	esp_ota_img_states_t state;
	esp_lcd_panel_handle_t panel = NULL;
	guard_start_t guard;
	app_boot_t boot = { 0 };
	json_token_t *tokens;
	job_t *jobs;
	esp_err_t err;

	ESP_LOGI(TAG, "%s %s in %s, last reset: %s", description->project_name, description->version, running->label,
	         reset_word(reason));
	strlcpy(platform_info.slot, running->label, sizeof(platform_info.slot));
	strlcpy(platform_info.reset, reset_word(reason), sizeof(platform_info.reset));

	/*
	 * What the boot loader says, from the two records of the partition "otadata". A record names a slot and
	 * its state; a slot no record names has none (ESP_ERR_NOT_FOUND).
	 * - Pending: the boot loader started this slot for the first time after it was made the one to boot. Any
	 *   restart before esp_ota_mark_app_valid_cancel_rollback() makes it start the other slot again. A
	 *   firmware that was flashed over USB together with the empty otadata of the build is never pending: the
	 *   boot loader gives the slot it starts a record as valid then (bootloader_utility.c,
	 *   set_actual_ota_seq(): there is no factory app), and the other slot has none.
	 * - Taken back: the record of the other slot says that it was started and not confirmed (aborted: the
	 *   boot loader writes that at the start after the one that was not confirmed), or marked as bad
	 *   (invalid: esp_ota_mark_app_invalid_rollback(), which nothing here calls). It says so until the next
	 *   upload begins (esp_ota_begin() erases that record) or the slot is made the one to boot again. The
	 *   version before a confirmed update is valid, and stays so.
	 */
	other_slot = esp_ota_get_next_update_partition(NULL);
	update_pending = esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
	boot.update_pending = update_pending;
	boot.rolled_back = other_slot != NULL && esp_ota_get_state_partition(other_slot, &state) == ESP_OK &&
	                   (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED);

	err = store_init();
	if(err != ESP_OK)
	{
		step_failed("store", err, false);
	}
	ap_password_make();
	ap_ssid_make();

	err = board_init();
	if(err != ESP_OK)
	{
		step_failed("board", err, true);
	}
	guard = guard_start(&guard_memory, guard_reset_of(reason), knob_held());
	boot.safe_mode = guard.safe_mode;
	/*
	 * "Vorherige Version" is the version that ran before this one, and nothing else that lies in the other
	 * slot: not what an upload left there, and never an update that was taken back. That one is a whole
	 * firmware of this project as well, and its upload was installed, so neither of the other two questions
	 * finds anything wrong with it - only the boot loader knows that it was started and nobody confirmed it.
	 */
	boot.previous_firmware = !upload_left_behind(running) && !boot.rolled_back && holds_firmware(other_slot);
	ESP_LOGI(TAG, "safe mode %d, layout before the last %d, update pending %d, taken back %d, previous firmware %d",
	         guard.safe_mode, guard.previous_layout, boot.update_pending, boot.rolled_back, boot.previous_firmware);

	// Once, and never given back. The jobs start out zeroed; the one behind the JOBS is the spare room.
	platform_app = heap_caps_malloc(sizeof(app_t), MALLOC_CAP_SPIRAM);
	tokens = heap_caps_malloc(LAYOUT_TOKENS * sizeof(json_token_t), MALLOC_CAP_SPIRAM);
	jobs = heap_caps_calloc(JOBS + 1, sizeof(job_t), MALLOC_CAP_SPIRAM);
	layout_room = heap_caps_malloc(LAYOUT_TEXT_MAX + 1, MALLOC_CAP_SPIRAM);
	app_lock = xSemaphoreCreateMutex();
	free_jobs = xQueueCreate(JOBS, sizeof(job_t *));
	waiting_jobs = xQueueCreate(JOBS, sizeof(job_t *));
	if(platform_app == NULL || tokens == NULL || jobs == NULL || layout_room == NULL || app_lock == NULL ||
	   free_jobs == NULL || waiting_jobs == NULL)
	{
		step_failed("memory", ESP_ERR_NO_MEM, true);
	}
	for(int i = 0; i < JOBS; i++)
	{
		job_t *job = &jobs[i];

		xQueueSend(free_jobs, &job, 0);
	}
	spare = &jobs[JOBS];

	// No event can come before the tasks run: until then the room of the first job holds what was stored
	read_stored(&jobs[0].kept, guard.previous_layout, &boot);
	// The build knows one text for the firmware, the version of the application description: `git describe`
	// as long as the project has no version.txt. It is the version and the git text of the app.
	boot.version = description->version;
	boot.git = description->version;
	boot.builtin_layout = builtin_start;
	boot.builtin_length = (size_t)(builtin_end - builtin_start);
	boot.work = tokens;
	boot.work_count = LAYOUT_TOKENS;
	app_init(platform_app, &boot, platform_now_ms());

	err = board_panel_start(&panel);
	if(err != ESP_OK)
	{
		step_failed("panel", err, true);
	}
	// From the task that started the panel: the screen follows the interrupts of the panel to its core
	err = screen_start(panel);
	if(err != ESP_OK)
	{
		step_failed("screen", err, true);
	}

	err = net_start(ap_ssid, ap_password);
	if(err != ESP_OK)
	{
		step_failed("network", err, false);
		return;
	}
	net_seen_ms = platform_now_ms();
	net_watched = true;
	err = web_start();
	if(err != ESP_OK)
	{
		step_failed("web server", err, false);
	}
}

// Stores a text or a list as it was copied. One without anything in it (the last network was forgotten) is
// stored as no value at all: the start reads that as not stored. A text that could not be made is not stored.
static void keep(store_space_t space, const char *key, const void *data, int length)
{
	if(length < 0)
	{
		ESP_LOGE(TAG, "%s: nothing to store, the text had no room", key);
	}
	else if(length > 0)
	{
		store_write(space, key, data, (size_t)length);
	}
	else if(!store_erase(space, key))
	{
		ESP_LOGE(TAG, "%s: not removed", key);
	}
}

/*
 * What is stored as layout becomes the one before it, then the new one is stored; without a stored layout
 * there is no one before it either. In this order: if the power fails between the two writes, both hold the
 * layout that was in use before. guard.h is told, and its call of guard_alive() waits a full GUARD_ALIVE_MS
 * from now: a crash within that time starts the display with the layout before this one.
 */
static void store_layout(const kept_t *kept, uint64_t now_ms)
{
	int before = store_read(STORE_DATA, STORE_KEY_LAYOUT, layout_room, LAYOUT_TEXT_MAX);

	keep(STORE_DATA, STORE_KEY_LAYOUT_PREV, layout_room, before < 0 ? 0 : before);
	if(store_write(STORE_DATA, STORE_KEY_LAYOUT, kept->layout, (size_t)kept->layout_length))
	{
		guard_layout_stored(&guard_memory);
		alive_at_ms = now_ms + GUARD_ALIVE_MS;
		alive_due = true;
	}
}

// Makes the other slot the one the boot loader starts next. esp_ota_set_boot_partition() checks the whole
// image first and changes nothing if it is damaged.
static void boot_other_slot(void)
{
	esp_err_t err = other_slot != NULL ? esp_ota_set_boot_partition(other_slot) : ESP_ERR_NOT_FOUND;

	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "the other slot is not the one to boot: %s", esp_err_to_name(err));
	}
}

/*
 * Carries out one job: storing first, in the order app.h lists the events, then what changes the firmware.
 * The restart some events end with is the caller's (restart()). Nothing of this is done under the lock. The
 * contents are not logged: a list of networks holds passwords.
 */
static void carry_out(const job_t *job, uint64_t now_ms)
{
	const kept_t *kept = &job->kept;
	uint32_t events = job->events;

	ESP_LOGI(TAG, "events 0x%04" PRIx32, events);

	if(events & APP_EVENT_STORE_SETTINGS)
	{
		keep(STORE_CFG, STORE_KEY_SETTINGS, kept->settings, kept->settings_length);
	}
	if(events & APP_EVENT_STORE_WIFI)
	{
		keep(STORE_CFG, STORE_KEY_WIFI, kept->profiles, kept->profile_count * (int)sizeof(net_profile_t));
	}
	if(events & APP_EVENT_STORE_BOUND)
	{
		keep(STORE_CFG, STORE_KEY_BOUND, kept->bound, kept->bound_length);
	}
	if(events & APP_EVENT_STORE_LAYOUT)
	{
		store_layout(kept, now_ms);
	}
	if(events & APP_EVENT_ERASE_LAYOUT)
	{
		// The one before it stays
		keep(STORE_DATA, STORE_KEY_LAYOUT, NULL, 0);
	}
	if(events & APP_EVENT_STORE_CATALOG)
	{
		keep(STORE_DATA, STORE_KEY_CATALOG, kept->catalog, kept->catalog_length);
	}
	if(events & APP_EVENT_STORE_OLD)
	{
		keep(STORE_DATA, STORE_KEY_DTC_OLD, kept->old, kept->old_length);
	}

	if(events & APP_EVENT_MARK_VALID)
	{
		esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();

		if(err != ESP_OK)
		{
			ESP_LOGE(TAG, "the firmware is not marked as good: %s", esp_err_to_name(err));
		}
	}
	if((events & APP_EVENT_FACTORY_RESET) && !store_erase_space(STORE_CFG))
	{
		ESP_LOGE(TAG, "factory reset: the settings are not erased");
	}
	if(events & APP_EVENT_PREVIOUS_FIRMWARE)
	{
		boot_other_slot();
	}
	if(events & APP_EVENT_INSTALL_FIRMWARE)
	{
		// Only what the web server has reported whole, with its check sum right, since the last upload began
		if(atomic_load(&upload_complete))
		{
			boot_other_slot();
		}
		else
		{
			ESP_LOGE(TAG, "no complete upload to install");
		}
	}
}

// The main task is done with a job: its room is free again
static void job_done(job_t *job)
{
	xQueueSend(free_jobs, &job, 0);
	atomic_fetch_sub(&jobs_out, 1);
}

/*
 * The restart: the one a job asked for (`job`, carried out and still held by this task), or the one this
 * task decided on itself (NULL). Also when a slot could not be made the one to boot: the display was told to
 * restart, and the version that then runs again tells what became of it.
 *
 * First the wait of RESTART_MS. In it the answer of the web server to the request that asked for the
 * restart leaves, and the screen task switches the backlight off: esp_restart() resets the two cores and
 * some of the peripherals, the PWM of the backlight is not among them (esp_system, system_internal.c of the
 * ESP32-S3), and the panel would be lit, without its signals, until the next start has come to board_init().
 * From the first line on the web server and the network task begin nothing new: the count of the jobs that
 * are out is raised by one that never comes back (platform_stored()). A request that is answered now could
 * raise what nobody carries out any more.
 *
 * Then what was raised in the meantime is carried out, all of it but another wait: a setting stored at the
 * knob in that half second, the answer to "Update in Ordnung?" that came just when its time had run out
 * (app.h raises the restart and still takes the answer). Not after a factory reset, neither the one that
 * asked for this restart nor one that comes up among the others: nothing is stored after a factory reset,
 * it would write back what was erased.
 */
static void __attribute__((noreturn)) restart(job_t *job)
{
	bool reset = job != NULL && (job->events & APP_EVENT_FACTORY_RESET) != 0;

	atomic_fetch_add(&jobs_out, 1);
	atomic_store(&restarting, true);
	vTaskDelay(pdMS_TO_TICKS(RESTART_MS));
	if(job != NULL)
	{
		job_done(job);
	}
	while(!reset)
	{
		// What the app still holds, and what found no room: there is one now
		platform_lock();
		platform_events();
		platform_unlock();
		if(xQueueReceive(waiting_jobs, &job, 0) != pdTRUE)
		{
			break;
		}
		carry_out(job, platform_now_ms());
		reset = (job->events & APP_EVENT_FACTORY_RESET) != 0;
		job_done(job);
	}
	esp_restart();
}

/*
 * What the info page shows of the device, and the temperature for guard.h. board_temperature() is the one
 * function of the board this task calls after the start; it shares nothing with those of the screen task.
 */
static void every_second(uint64_t now_ms)
{
	app_platform_t told =
	{
		.heap = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
		.heap_min = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
		.psram = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
		.psram_min = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
	};
	int celsius = 0;
	bool measured = board_temperature(&celsius);

	platform_lock();
	// The network task writes its part under the lock: read there as well
	told.ssid = platform_info.ssid;
	told.ip = platform_info.ip;
	told.rssi = platform_info.rssi;
	told.ap_ssid = platform_info.ap_ssid;
	told.ap_password = platform_info.ap_password;
	told.slot = platform_info.slot;
	told.reset = platform_info.reset;
	told.reconnects = platform_info.reconnects;
	app_platform(platform_app, &told);
	app_temperature(platform_app, celsius, measured);
	platform_events();
	platform_unlock();

	if(alive_due && now_ms >= alive_at_ms)
	{
		guard_alive(&guard_memory);
		alive_due = false;
	}

	/*
	 * The network task waits in the HTTP client and in the mDNS component, and neither ends its wait by a
	 * clock of its own (net.c names the places): a peer that never finishes its answer, or a task that does
	 * not keep its promise, holds it for good. It holds no lock then, so screen and knob live on - but no
	 * network is joined or left, the access point cannot be switched, and the poll never hears that its
	 * request got no answer. Nothing but a restart ends that. It is one the firmware asks for: no crash to
	 * guard.h, so a peer that does this again and again does not drive the display into the safe mode.
	 */
	if(net_watched)
	{
		uint32_t turns = net_turns();

		if(turns != net_seen)
		{
			net_seen = turns;
			net_seen_ms = now_ms;
		}
		else if(now_ms - net_seen_ms >= NET_STALL_MS)
		{
			ESP_LOGE(TAG, "the network task has not come round for %u s", (unsigned)(NET_STALL_MS / 1000));
			restart(NULL);
		}
	}
}

static void main_task(void *arg)
{
	uint64_t second_ms = 0;     // when every_second() is due next

	start();
	for(;;)
	{
		uint64_t now_ms = platform_now_ms();
		job_t *job;

		// One tick more than is left: a wait of FreeRTOS ends with a tick, and a wait of no tick would spin
		if(xQueueReceive(waiting_jobs, &job, now_ms < second_ms ? pdMS_TO_TICKS(second_ms - now_ms) + 1 : 0) == pdTRUE)
		{
			carry_out(job, platform_now_ms());
			if(job->events & RESTART_EVENTS)
			{
				restart(job);
			}
			job_done(job);
		}

		now_ms = platform_now_ms();
		if(now_ms >= second_ms)
		{
			every_second(now_ms);
			second_ms = now_ms + SECOND_MS;
		}
	}
}

void app_main(void)
{
	if(xTaskCreatePinnedToCore(main_task, "platform", MAIN_STACK, NULL, MAIN_PRIORITY, NULL, PANEL_CORE) != pdPASS)
	{
		ESP_LOGE(TAG, "no memory for the task of the platform");
		// Nothing runs now and nothing will: only a restart can change that, and it is what takes back an
		// update that is not confirmed
		give_up();
	}
}
