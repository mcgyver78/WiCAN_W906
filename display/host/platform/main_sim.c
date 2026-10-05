/*
 * Runs display/main/main.c on a PC: the file is included as it is and started again and again, as a chip
 * would start it, against the real core and a device made of stand-ins. "make" in this directory builds and
 * runs it; redproof.py breaks main.c in one place at a time (mutations/main.py) and expects a check to fail.
 * It runs in this directory: a catalogue and a fault memory list come from the files of the other tests.
 *
 * What stands for the device, and what it rests on (ESP-IDF v5.5.2, read, not run):
 *   the flash       store.h as an array of records that outlasts a start; it can be broken as a whole
 *   the boot loader two app slots, no factory app, and the two records of "otadata" as app_update/esp_ota_ops.c
 *                   and bootloader_support/src/bootloader_utility.c keep them with
 *                   CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE: a record names a slot by its number ((number - 1)
 *                   modulo the two slots) and holds its state. The state of a slot is that of the first
 *                   record that names it; a slot no record names has none (ESP_ERR_NOT_FOUND). The boot
 *                   loader starts the slot of the record with the highest number among those not marked as
 *                   bad, the first slot if there is none, and the other slot if the image of the one it
 *                   chose is not whole. esp_ota_set_boot_partition() checks the image
 *                   and writes the next number that names the slot, as new, into the other record; the boot
 *                   loader makes new pending at the first start, and pending aborted at the start after that -
 *                   so that it starts the slot of the other record again;
 *                   esp_ota_mark_app_valid_cancel_rollback() makes the record with the highest number valid.
 *                   With both records erased (as flashed over USB) the boot loader writes a valid record for
 *                   the slot it starts. esp_ota_begin(), which web.c calls, erases the record that is not
 *                   the one in use unless it names the running slot. A record is written whole or not at
 *                   all: its check sum is not here.
 *   the RTC memory  the counter of guard.h is a variable that is kept from start to start, and filled with
 *                   something else where the power was lost
 *   the heap        what a start allocates is given back with the next one. A block that was not asked for
 *                   zeroed is not zero (sim.h).
 *   FreeRTOS        one task, that of main.c. Where it would sleep - a queue is empty and a wait was asked
 *                   for - the rest of the world gets its turn: the next step of the script of the scenario,
 *                   which plays the screen task, the network task and the web server. Without a step the
 *                   start ends there ("idle"). The lock is a queue of one place, as in FreeRTOS; taking it
 *                   twice and giving it back unheld are failed checks, and so is everything that waits or
 *                   writes to the flash while it is held (platform.h).
 *   the clock       stands still while the task works and moves by what a wait was asked for
 *   the board, the screen, the network, the web server
 *                   counted calls with a result the scenario sets
 *
 * What this cannot see, in short: nothing of ESP-IDF runs, so every claim about it is the reading above.
 * There is one thread, so no two tasks ever meet in the middle of a function. Stacks, priorities, cores and
 * real time are not there at all, and neither are the board, the screen task, the network task and the web
 * server: each of those is a counted call here.
 */
#include <setjmp.h>
#include <stdint.h>
#include <stdbool.h>
#include "sim.h"
#include "main.c"
#include "app_web.h"

/* What outlasts a restart ---------------------------------------------------------------------------- */

#define STORE_RECORDS   16
// More than the largest value main.c keeps (the catalogue, CATALOG_ROOM)
#define STORE_ROOM      32768

typedef struct
{
	bool used;
	store_space_t space;
	char key[16];
	size_t length;
	unsigned char data[STORE_ROOM];
} record_t;

static record_t flash[STORE_RECORDS];
static bool store_broken;                   // the partition cannot be read or written
static esp_partition_t slots[2] = { { .label = "ota_0" }, { .label = "ota_1" } };
static int running_slot;
static bool image_in[2] = { true, false };  // a whole firmware of this project lies there
static int marked_valid;

// A record of otadata (esp_ota_select_entry_t, without its label and its check sum)
typedef struct
{
	uint32_t number;                        // ota_seq; all ones: the record is erased
	uint32_t state;                         // esp_ota_img_states_t
} ota_record_t;

#define OTA_ERASED      { UINT32_MAX, ESP_OTA_IMG_UNDEFINED }
#define NO_RECORD       -2                  // no state of a slot: none of esp_ota_img_states_t as an int

static ota_record_t otadata[2] = { OTA_ERASED, OTA_ERASED };

/* One start ------------------------------------------------------------------------------------------ */

enum { OUT_IDLE = 1, OUT_RESTART };

static jmp_buf out;
static uint64_t fake_us;
static void (*script)(void);                // what the rest of the world does when the task sleeps next
static void (*in_restart_wait)(void);       // ... and in the half second before a restart
static TaskFunction_t task;
static esp_reset_reason_t reason = ESP_RST_POWERON;
static bool button_pressed;
static bool button_fails_once;
static int button_reads;
static esp_err_t board_result;
static esp_err_t net_result;
static int screens, nets, webs;
static int random_on, random_off, randoms, randoms_while_on;
static char net_ssid[64];
static char net_password[80];
static uint32_t seed = 12345;
static bool net_frozen;                     // the network task does not come round any more
static uint32_t net_count;
static jmp_buf *stored_probe;               // set while somebody asks whether platform_stored() would wait
static int restart_waits;                   // waits of RESTART_MS
static bool dark_in_wait;                   // platform_restarting() said so during the last of them
static bool dark_in_give_up;                // ... and during a wait of GIVE_UP_MS
static bool held_at_restart;                // platform_stored() would still wait when the chip restarts

// A queue of FreeRTOS. A mutex is a queue of one place there as well; here it is taken while `count` is 1.
struct QueueDefinition
{
	size_t item;
	int capacity;
	int count;
	int head;
	unsigned char *data;
};

static bool lock_held(void)
{
	return app_lock != NULL && app_lock->count != 0;
}

const char *esp_err_to_name(esp_err_t code)
{
	(void)code;
	return "error";
}

/* The heap: given back as a whole with the next start */

#define HEAP_BLOCKS     16

static void *heap_blocks[HEAP_BLOCKS];
static int heap_count;

static void *heap_take(size_t size, bool zeroed)
{
	void *block;

	NEED(heap_count < HEAP_BLOCKS);
	block = sim_alloc(size);
	if(zeroed)
	{
		memset(block, 0, size);
	}
	heap_blocks[heap_count++] = block;
	return block;
}

static void heap_reset(void)
{
	while(heap_count > 0)
	{
		free(heap_blocks[--heap_count]);
	}
}

void *heap_caps_malloc(size_t size, uint32_t caps)
{
	// app_t and the rooms next to it are far too large for the internal RAM (platform.h)
	CHECK(caps == MALLOC_CAP_SPIRAM);
	return heap_take(size, false);
}

void *heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
	CHECK(caps == MALLOC_CAP_SPIRAM);
	return heap_take(n * size, true);
}

size_t heap_caps_get_free_size(uint32_t caps)
{
	return caps == MALLOC_CAP_INTERNAL ? 111 : 333;
}

size_t heap_caps_get_minimum_free_size(uint32_t caps)
{
	return caps == MALLOC_CAP_INTERNAL ? 99 : 222;
}

/* FreeRTOS */

QueueHandle_t xQueueGenericCreate(const UBaseType_t length, const UBaseType_t item, const uint8_t type)
{
	QueueHandle_t queue = heap_take(sizeof(*queue), true);

	(void)type;
	queue->item = item;
	queue->capacity = (int)length;
	queue->data = heap_take((size_t)length * item, true);
	return queue;
}

QueueHandle_t xQueueCreateMutex(const uint8_t type)
{
	return xQueueGenericCreate(1, 1, type);
}

BaseType_t xQueueSemaphoreTake(QueueHandle_t queue, TickType_t wait)
{
	(void)wait;
	// With one task a lock that is taken twice would never come free
	CHECK(queue->count == 0);
	queue->count = 1;
	return pdTRUE;
}

BaseType_t xQueueGenericSend(QueueHandle_t queue, const void *const item, TickType_t wait, const BaseType_t position)
{
	(void)wait;
	(void)position;
	if(item == NULL)
	{
		// A mutex is given back
		CHECK(queue->count == 1);
		queue->count = 0;
		return pdTRUE;
	}
	// main.c does not look at what a send returns: its queues have a place for every job there is
	if(!CHECK(queue->count < queue->capacity))
	{
		return pdFALSE;
	}
	memcpy(&queue->data[(size_t)((queue->head + queue->count) % queue->capacity) * queue->item], item, queue->item);
	queue->count++;
	return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *const buffer, TickType_t wait)
{
	for(;;)
	{
		void (*step)(void);

		if(queue->count > 0)
		{
			memcpy(buffer, &queue->data[(size_t)queue->head * queue->item], queue->item);
			queue->head = (queue->head + 1) % queue->capacity;
			queue->count--;
			return pdTRUE;
		}
		if(wait == 0)
		{
			return pdFALSE;
		}
		// The task of main.c would sleep now, and it must not sleep on the lock
		CHECK(!lock_held());
		if(script == NULL)
		{
			longjmp(out, OUT_IDLE);
		}
		step = script;
		script = NULL;
		step();
		if(queue->count == 0)
		{
			fake_us += (uint64_t)wait * (1000000 / configTICK_RATE_HZ);
			return pdFALSE;
		}
	}
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t code, const char *const name, const uint32_t stack, void *const arg,
                                   UBaseType_t priority, TaskHandle_t *const created, const BaseType_t core)
{
	(void)name;
	(void)stack;
	(void)arg;
	(void)priority;
	(void)created;
	// The second core: the first is where the WiFi driver has its tasks (main.c)
	CHECK(core == 1);
	task = code;
	return pdPASS;
}

void vTaskDelay(const TickType_t ticks)
{
	CHECK(!lock_held());
	// A wait of one tick is the loop of platform_stored(): whoever asked is told that it waits
	if(stored_probe != NULL && ticks == 1)
	{
		longjmp(*stored_probe, 1);
	}
	if(ticks == pdMS_TO_TICKS(GIVE_UP_MS))
	{
		dark_in_give_up = platform_restarting();
	}
	if(ticks == pdMS_TO_TICKS(RESTART_MS))
	{
		void (*step)(void) = in_restart_wait;

		restart_waits++;
		dark_in_wait = platform_restarting();
		in_restart_wait = NULL;
		if(step != NULL)
		{
			step();
		}
	}
	fake_us += (uint64_t)ticks * (1000000 / configTICK_RATE_HZ);
}

int64_t esp_timer_get_time(void)
{
	return (int64_t)fake_us;
}

/* The chip */

uint32_t esp_random(void)
{
	randoms++;
	if(random_on > random_off)
	{
		randoms_while_on++;
	}
	seed = seed * 1103515245u + 12345u;
	return seed >> 3;
}

void bootloader_random_enable(void)
{
	random_on++;
}

void bootloader_random_disable(void)
{
	random_off++;
}

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type)
{
	CHECK(type == ESP_MAC_WIFI_STA);
	memcpy(mac, "\x10\x20\x30\x40\xEE\x0F", 6);
	return ESP_OK;
}

esp_reset_reason_t esp_reset_reason(void)
{
	return reason;
}

static bool stored_waits(void);

void esp_restart(void)
{
	// What the web server or the network task would find if it asked at this very moment. Not before the
	// rooms are there: a start that gave up earlier has nobody who could ask.
	held_at_restart = app_lock != NULL && spare != NULL && stored_waits();
	longjmp(out, OUT_RESTART);
}

const esp_app_desc_t *esp_app_get_description(void)
{
	static const esp_app_desc_t description = { .version = "display-v0.1.0-3-g1a2b3c4", .project_name = "wican-display" };

	return &description;
}

/* store.h */

static record_t *find(store_space_t space, const char *key)
{
	for(int i = 0; i < STORE_RECORDS; i++)
	{
		if(flash[i].used && flash[i].space == space && strcmp(flash[i].key, key) == 0)
		{
			return &flash[i];
		}
	}
	return NULL;
}

esp_err_t store_init(void)
{
	return store_broken ? ESP_FAIL : ESP_OK;
}

int store_read(store_space_t space, const char *key, void *data, size_t size)
{
	record_t *record = store_broken ? NULL : find(space, key);

	if(record == NULL || record->length > size)
	{
		return -1;
	}
	memcpy(data, record->data, record->length);
	return (int)record->length;
}

int store_read_text(store_space_t space, const char *key, char *data, size_t size)
{
	int length = store_read(space, key, data, size - 1);

	data[length < 0 ? 0 : length] = '\0';
	return length;
}

bool store_write(store_space_t space, const char *key, const void *data, size_t length)
{
	record_t *record = find(space, key);

	// A write to the flash stops every task for a moment: never under the lock (platform.h)
	CHECK(!lock_held());
	if(store_broken || data == NULL)
	{
		return false;
	}
	for(int i = 0; record == NULL && i < STORE_RECORDS; i++)
	{
		if(!flash[i].used)
		{
			record = &flash[i];
		}
	}
	NEED(record != NULL && strlen(key) < sizeof(record->key));
	if(!CHECK(length <= STORE_ROOM))
	{
		return false;
	}
	record->used = true;
	record->space = space;
	strcpy(record->key, key);
	memcpy(record->data, data, length);
	record->length = length;
	return true;
}

bool store_erase(store_space_t space, const char *key)
{
	record_t *record = find(space, key);

	CHECK(!lock_held());
	if(record != NULL && !store_broken)
	{
		record->used = false;
	}
	return !store_broken;
}

bool store_erase_space(store_space_t space)
{
	CHECK(!lock_held());
	for(int i = 0; i < STORE_RECORDS && !store_broken; i++)
	{
		if(flash[i].space == space)
		{
			flash[i].used = false;
		}
	}
	return !store_broken;
}

/* board.h and the other parts of the platform */

esp_err_t board_init(void)
{
	return board_result;
}

esp_err_t board_panel_start(esp_lcd_panel_handle_t *panel)
{
	*panel = (esp_lcd_panel_handle_t)&slots;
	return ESP_OK;
}

bool board_button(bool *pressed)
{
	button_reads++;
	if(button_fails_once && button_reads == 3)
	{
		return false;
	}
	*pressed = button_pressed;
	return true;
}

bool board_temperature(int *celsius)
{
	*celsius = 47;
	return true;
}

esp_err_t screen_start(esp_lcd_panel_handle_t panel)
{
	CHECK(panel != NULL);
	// The screen task runs the app from its first round on: it has to be there
	CHECK(platform_app != NULL && platform_app->work != NULL);
	screens++;
	return ESP_OK;
}

esp_err_t net_start(const char *ssid, const char *password)
{
	nets++;
	snprintf(net_ssid, sizeof(net_ssid), "%s", ssid);
	snprintf(net_password, sizeof(net_password), "%s", password);
	return net_result;
}

uint32_t net_turns(void)
{
	if(!net_frozen)
	{
		net_count++;
	}
	return net_count;
}

esp_err_t web_start(void)
{
	webs++;
	return ESP_OK;
}

/* The boot loader, and what the firmware asks it (see the head of this file) */

static int slot_of(const esp_partition_t *partition)
{
	return (int)(partition - slots);
}

static bool record_names(const ota_record_t *record, int slot)
{
	return record->number != UINT32_MAX && (int)((record->number - 1) % 2) == slot;
}

// bootloader_common_ota_select_valid(): written, and not marked as bad
static bool record_valid(const ota_record_t *record)
{
	return record->number != UINT32_MAX && record->state != ESP_OTA_IMG_INVALID && record->state != ESP_OTA_IMG_ABORTED;
}

// bootloader_common_get_active_otadata(): of the valid records the one with the higher number; -1: none
static int record_in_use(void)
{
	if(record_valid(&otadata[0]) && record_valid(&otadata[1]))
	{
		return otadata[0].number >= otadata[1].number ? 0 : 1;
	}
	return record_valid(&otadata[0]) ? 0 : record_valid(&otadata[1]) ? 1 : -1;
}

// The record esp_ota_get_state_partition() takes the state of a slot from: the first that names it; -1: none
static int record_of(int slot)
{
	return record_names(&otadata[0], slot) ? 0 : record_names(&otadata[1], slot) ? 1 : -1;
}

// What esp_ota_get_state_partition() says of a slot, NO_RECORD if no record names it
static int state_of(int slot)
{
	return record_of(slot) < 0 ? NO_RECORD : (int)otadata[record_of(slot)].state;
}

// The record of a slot is given a state, as a firmware could do it that calls
// esp_ota_mark_app_invalid_rollback(); the slot has to have a record
static void set_state_of(int slot, esp_ota_img_states_t state)
{
	NEED(record_of(slot) >= 0);
	otadata[record_of(slot)].state = state;
}

// bootloader_utility_get_selected_boot_partition() and bootloader_utility_load_boot_image()
static void boot_loader(void)
{
	bool initial = otadata[0].number == UINT32_MAX && otadata[1].number == UINT32_MAX;
	int first = 0;
	int used;

	// A slot that was started and not confirmed is not started again
	for(int i = 0; i < 2; i++)
	{
		if(otadata[i].state == ESP_OTA_IMG_PENDING_VERIFY)
		{
			otadata[i].state = ESP_OTA_IMG_ABORTED;
		}
	}
	used = record_in_use();
	if(used >= 0)
	{
		first = (int)((otadata[used].number - 1) % 2);
		if(otadata[used].state == ESP_OTA_IMG_NEW)
		{
			otadata[used].state = ESP_OTA_IMG_PENDING_VERIFY;
		}
	}
	// The slot chosen if its image is whole, else the other one
	running_slot = image_in[first] ? first : 1 - first;
	NEED(image_in[running_slot]);
	if(initial)
	{
		// set_actual_ota_seq()
		otadata[0] = (ota_record_t){ (uint32_t)running_slot + 1, ESP_OTA_IMG_VALID };
	}
}

// As flashed over USB (idf.py flash): a firmware in the first slot and the empty otadata of the build. What
// lay in the other slot before lies there still.
static void flashed_by_usb(void)
{
	otadata[0] = otadata[1] = (ota_record_t)OTA_ERASED;
	image_in[0] = true;
}

const esp_partition_t *esp_ota_get_running_partition(void)
{
	return &slots[running_slot];
}

const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *from)
{
	(void)from;
	return &slots[1 - running_slot];
}

esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition, esp_ota_img_states_t *state)
{
	const int record = record_of(slot_of(partition));

	if(record < 0)
	{
		return ESP_ERR_NOT_FOUND;
	}
	*state = (esp_ota_img_states_t)otadata[record].state;
	return ESP_OK;
}

esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition, esp_app_desc_t *description)
{
	if(!image_in[slot_of(partition)])
	{
		return ESP_ERR_NOT_FOUND;
	}
	*description = *esp_app_get_description();
	return ESP_OK;
}

// esp_rewrite_ota_data(): the slot is the one to start next, as new
static void set_boot(int slot)
{
	const int used = record_in_use();
	// The lowest number that names the slot
	uint32_t number = (uint32_t)(slot + 1) % 2;

	if(used < 0)
	{
		otadata[0] = (ota_record_t){ (uint32_t)slot + 1, ESP_OTA_IMG_NEW };
		return;
	}
	// The first such number that is not below the one in use, into the other record
	while(otadata[used].number > number)
	{
		number += 2;
	}
	otadata[1 - used] = (ota_record_t){ number, ESP_OTA_IMG_NEW };
}

esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition)
{
	// Reads and hashes the whole image, then writes otadata
	CHECK(!lock_held());
	if(!image_in[slot_of(partition)])
	{
		return ESP_ERR_OTA_VALIDATE_FAILED;
	}
	set_boot(slot_of(partition));
	return ESP_OK;
}

esp_err_t esp_ota_mark_app_valid_cancel_rollback(void)
{
	const int used = record_in_use();

	CHECK(!lock_held());
	if(used < 0)
	{
		return ESP_FAIL;
	}
	otadata[used].state = ESP_OTA_IMG_VALID;
	marked_valid++;
	return ESP_OK;
}

// What esp_ota_begin() does to otadata when web.c begins to write the other slot
// (esp_ota_invalidate_inactive_ota_data_slot()): the record that is not in use is erased, unless it names
// the running slot. The slot itself is erased as the writing goes.
static void ota_begin(void)
{
	const int used = record_in_use();

	if(used >= 0 && otadata[1 - used].number != UINT32_MAX && !record_names(&otadata[1 - used], running_slot))
	{
		otadata[1 - used] = (ota_record_t)OTA_ERASED;
	}
}

// An update lies in the other slot and was made the one to boot: the next start is its first
static void update_installed(void)
{
	image_in[1 - running_slot] = true;
	set_boot(1 - running_slot);
}

// A start that gave up before it made the app leaves none. The checks behind it look at an empty one then:
// they fail on what they do not find there, and the simulation goes on to its end.
static app_t no_app;

static int ended(int how)
{
	if(platform_app == NULL)
	{
		memset(&no_app, 0, sizeof(no_app));
		no_app.version = "";
		no_app.git = "";
		platform_app = &no_app;
	}
	return how;
}

/*
 * A start of the firmware, in the slot the boot loader chooses; returns how it ended: OUT_IDLE (the task of
 * main.c sleeps and the scenario has no step left) or OUT_RESTART. The flash, the slots, otadata and the
 * memory of guard.h stay; everything else is
 * as a chip has it after a reset: the heap empty, the variables of main.c as they are written there. A
 * variable that is added to main.c has to be added here.
 */
static int boot(esp_reset_reason_t why, void (*steps)(void))
{
	boot_loader();
	if(why == ESP_RST_POWERON)
	{
		memset(&guard_memory, 0xA5, sizeof(guard_memory));
	}

	heap_reset();
	platform_app = NULL;
	memset(&platform_info, 0, sizeof(platform_info));
	app_lock = NULL;
	free_jobs = NULL;
	waiting_jobs = NULL;
	spare = NULL;
	atomic_store(&jobs_out, 0);
	layout_room = NULL;
	other_slot = NULL;
	update_pending = false;
	atomic_store(&upload_complete, false);
	atomic_store(&restarting, false);
	net_watched = false;
	net_seen = 0;
	net_seen_ms = 0;
	memset(ap_ssid, 0, sizeof(ap_ssid));
	memset(ap_password, 0, sizeof(ap_password));
	alive_due = true;
	alive_at_ms = GUARD_ALIVE_MS;

	fake_us = 0;
	reason = why;
	script = steps;
	in_restart_wait = NULL;
	task = NULL;
	stored_probe = NULL;
	button_reads = 0;
	screens = nets = webs = 0;
	random_on = random_off = randoms = randoms_while_on = 0;
	net_count = 0;
	restart_waits = 0;
	dark_in_wait = dark_in_give_up = held_at_restart = false;

	switch(setjmp(out))
	{
		case 0:
			app_main();
			NEED(task != NULL);
			task(NULL);
			// The task of main.c never returns
			return 0;
		case OUT_IDLE:
			return ended(OUT_IDLE);
		default:
			return ended(OUT_RESTART);
	}
}

/* What the other tasks do ------------------------------------------------------------------------------ */

static char layout_a[LAYOUT_TEXT_MAX + 1];
static char layout_b[LAYOUT_TEXT_MAX + 1];
static char web_out[APP_WEB_OUT_SIZE];

// Somebody called into the app, and the app raised these
static void raise_events(uint32_t events)
{
	platform_lock();
	platform_app->events |= events;
	platform_events();
	platform_unlock();
}

// The screen task: something was done at the knob
static void do_it(nav_do_t what)
{
	platform_lock();
	app_do(platform_app, what, platform_now_ms());
	platform_events();
	platform_unlock();
}

// The web server: a request for the layout, with the release given
static int layout_call(web_route_t route, const char *text)
{
	size_t length = 0;
	int status;

	platform_lock();
	app_do(platform_app, NAV_DO_RELEASE_ON, platform_now_ms());
	status = app_web_layout(platform_app, route, text, text != NULL ? strlen(text) : 0, web_out, &length, platform_now_ms());
	platform_events();
	platform_unlock();
	return status;
}

static int save(const char *text)
{
	return layout_call(WEB_ROUTE_LAYOUT_SAVE, text);
}

// The main task is behind by all of its jobs: no room is free
static void fill_queue(void)
{
	for(int i = 0; i < JOBS; i++)
	{
		raise_events(APP_EVENT_STORE_SETTINGS);
	}
}

static bool stored_is(store_space_t space, const char *key, const char *text)
{
	record_t *record = find(space, key);

	return record != NULL && record->length == strlen(text) && memcmp(record->data, text, record->length) == 0;
}

// Whether what is stored holds the text somewhere: a record has a length and no zero at its end
static bool stored_has(store_space_t space, const char *key, const char *text)
{
	record_t *record = find(space, key);
	size_t length = strlen(text);

	for(size_t at = 0; record != NULL && at + length <= record->length; at++)
	{
		if(memcmp(&record->data[at], text, length) == 0)
		{
			return true;
		}
	}
	return false;
}

// Whether the web server or the network task would be held back by platform_stored() right now
static bool stored_waits(void)
{
	static jmp_buf probe;

	if(setjmp(probe) != 0)
	{
		stored_probe = NULL;
		// It waits without the lock
		CHECK(!lock_held());
		return true;
	}
	stored_probe = &probe;
	platform_stored();
	stored_probe = NULL;
	CHECK(!lock_held());
	return false;
}

static uint64_t until_ms;

static void step_wait(void)
{
	if(platform_now_ms() < until_ms)
	{
		script = step_wait;
	}
}

// What found no room is handed on with the next call of anybody (the screen task calls every 20 ms)
static void step_next_call(void)
{
	raise_events(0);
}

/* The scenarios ---------------------------------------------------------------------------------------- */

static char first_password[80];

static void test_first_start(void)
{
	CHECK(boot(ESP_RST_POWERON, NULL) == OUT_IDLE);
	// As flashed over USB: the boot loader has named the slot it started as valid, and the other one not at all
	CHECK(running_slot == 0 && state_of(0) == ESP_OTA_IMG_VALID && state_of(1) == NO_RECORD);
	CHECK(screens == 1 && nets == 1 && webs == 1);
	CHECK(strcmp(net_ssid, "WiCAN-Display-EE0F") == 0);
	CHECK(strlen(net_password) == 10 && strspn(net_password, "abcdefghjkmnpqrstuvwxyz23456789") == 10);
	CHECK(random_on == 1 && random_off == 1 && randoms == 10 && randoms_while_on == 10);
	CHECK(stored_is(STORE_CFG, STORE_KEY_AP_PASSWORD, net_password));
	CHECK(strcmp(platform_info.slot, "ota_0") == 0 && strcmp(platform_info.reset, "poweron") == 0);
	CHECK(strcmp(platform_app->slot, "ota_0") == 0 && strcmp(platform_app->reset, "poweron") == 0);
	CHECK(platform_app->heap == 111 && platform_app->heap_min == 99 && platform_app->psram == 333 &&
	      platform_app->psram_min == 222);
	CHECK(platform_app->has_temp && platform_app->temp_c == 47);
	CHECK(platform_app->has_builtin && platform_app->layout_length > 0 && platform_app->source != APP_LAYOUT_STORED);
	CHECK(!platform_app->safe_mode && !platform_app->update_pending && !platform_app->previous_firmware &&
	      !platform_app->rolled_back);
	CHECK(strcmp(platform_app->version, "display-v0.1.0-3-g1a2b3c4") == 0);
	CHECK(!platform_restarting());
	snprintf(first_password, sizeof(first_password), "%s", net_password);

	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(strcmp(net_password, first_password) == 0 && random_on == 0 && randoms == 0);
	CHECK(strcmp(platform_info.reset, "software") == 0);
}

static void step_night(void)
{
	do_it(NAV_DO_NIGHT_TOGGLE);
}

static void test_settings(void)
{
	CHECK(boot(ESP_RST_SW, step_night) == OUT_IDLE);
	CHECK(platform_app->settings.night_mode);
	CHECK(stored_has(STORE_CFG, STORE_KEY_SETTINGS, "\"night_mode\":true"));
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(platform_app->settings.night_mode);
}

static void step_reset_layout(void)
{
	CHECK(stored_is(STORE_DATA, STORE_KEY_LAYOUT, layout_b) && stored_is(STORE_DATA, STORE_KEY_LAYOUT_PREV, layout_a));
	CHECK(layout_call(WEB_ROUTE_LAYOUT_RESET, NULL) == 200);
}

static void step_save_b(void)
{
	CHECK(stored_is(STORE_DATA, STORE_KEY_LAYOUT, layout_a) && find(STORE_DATA, STORE_KEY_LAYOUT_PREV) == NULL);
	CHECK(guard_memory.layout_fresh == GUARD_MAGIC);
	CHECK(save(layout_b) == 200);
	script = step_reset_layout;
}

static void step_save_a(void)
{
	CHECK(save(layout_a) == 200);
	script = step_save_b;
}

static void step_save_only_b(void)
{
	CHECK(save(layout_b) == 200);
}

static void step_save_a_then_b(void)
{
	CHECK(save(layout_a) == 200);
	CHECK(save(layout_b) == 200);
}

static void test_layout(void)
{
	CHECK(boot(ESP_RST_SW, step_save_a) == OUT_IDLE);
	// After the reset: the stored layout is gone, the one before it stays
	CHECK(find(STORE_DATA, STORE_KEY_LAYOUT) == NULL && stored_is(STORE_DATA, STORE_KEY_LAYOUT_PREV, layout_a));

	// A save without a stored layout leaves none before it
	CHECK(boot(ESP_RST_SW, step_save_only_b) == OUT_IDLE);
	CHECK(stored_is(STORE_DATA, STORE_KEY_LAYOUT, layout_b) && find(STORE_DATA, STORE_KEY_LAYOUT_PREV) == NULL);
	CHECK(boot(ESP_RST_SW, step_save_a_then_b) == OUT_IDLE);
	CHECK(stored_is(STORE_DATA, STORE_KEY_LAYOUT, layout_b) && stored_is(STORE_DATA, STORE_KEY_LAYOUT_PREV, layout_a));
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(platform_app->source == APP_LAYOUT_STORED && strcmp(platform_app->layout_text, layout_b) == 0);

	// A crash shortly after a save: the layout before it
	CHECK(boot(ESP_RST_SW, step_save_a_then_b) == OUT_IDLE);
	CHECK(boot(ESP_RST_PANIC, NULL) == OUT_IDLE);
	CHECK(platform_app->source == APP_LAYOUT_STORED && strcmp(platform_app->layout_text, layout_a) == 0);
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(strcmp(platform_app->layout_text, layout_b) == 0);
}

static void step_wifi(void)
{
	platform_lock();
	platform_app->profile_count = net_store(platform_app->profiles, 0, "Werkstatt", "geheim123", "");
	platform_unlock();
	CHECK(platform_app->profile_count == 1);
	raise_events(APP_EVENT_STORE_WIFI);
}

static void step_forget(void)
{
	CHECK(platform_app->profile_count == 1 && strcmp(platform_app->profiles[0].ssid, "Werkstatt") == 0);
	CHECK(strcmp(platform_app->profiles[0].password, "geheim123") == 0);
	platform_app->profile_count = 0;
	raise_events(APP_EVENT_STORE_WIFI);
}

static void test_wifi(void)
{
	CHECK(boot(ESP_RST_SW, step_wifi) == OUT_IDLE);
	CHECK(find(STORE_CFG, STORE_KEY_WIFI) != NULL && find(STORE_CFG, STORE_KEY_WIFI)->length == sizeof(net_profile_t));
	// The last network is forgotten: no value at all, which the start reads as none stored
	CHECK(boot(ESP_RST_SW, step_forget) == OUT_IDLE);
	CHECK(find(STORE_CFG, STORE_KEY_WIFI) == NULL);
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(platform_app->profile_count == 0);
}

// The web server begins to write the other slot (web.c): the record of main.c first, then esp_ota_begin()
static void upload_begins(void)
{
	if(CHECK(platform_upload_begun()))
	{
		ota_begin();
	}
}

static void step_begin(void)
{
	upload_begins();
}

static void step_install_incomplete(void)
{
	upload_begins();
	raise_events(APP_EVENT_INSTALL_FIRMWARE);
}

static void step_install(void)
{
	upload_begins();
	platform_upload_complete();
	raise_events(APP_EVENT_INSTALL_FIRMWARE);
}

static void step_install_stale(void)
{
	upload_begins();
	platform_upload_complete();
	upload_begins();
	raise_events(APP_EVENT_INSTALL_FIRMWARE);
}

static void step_confirm(void)
{
	do_it(NAV_DO_UPDATE_OK);
}

static void step_previous(void)
{
	raise_events(APP_EVENT_PREVIOUS_FIRMWARE);
}

static void test_firmware(void)
{
	/*
	 * The other slot holds a firmware that was written over USB and never went through an update: no record
	 * of otadata names it, nobody took it back, and it can be started.
	 */
	image_in[1] = true;
	CHECK(boot(ESP_RST_SW, step_begin) == OUT_IDLE);
	CHECK(state_of(0) == ESP_OTA_IMG_VALID && state_of(1) == NO_RECORD);
	CHECK(platform_app->previous_firmware && !platform_app->rolled_back);   // it was one when the display started
	CHECK(stored_is(STORE_DATA, "upload", "ota_1"));
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(!platform_app->previous_firmware);                // an upload began: not after any restart
	CHECK(boot(ESP_RST_PANIC, NULL) == OUT_IDLE);
	CHECK(!platform_app->previous_firmware);

	// Not reported complete: nothing is made the one to boot, the display restarts as it was told
	CHECK(boot(ESP_RST_SW, step_install_incomplete) == OUT_RESTART);
	CHECK(state_of(1) == NO_RECORD && fake_us >= 500000);
	CHECK(restart_waits == 1 && dark_in_wait && held_at_restart);
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(running_slot == 0 && !platform_app->previous_firmware && !platform_app->update_pending);

	// A second upload began after a complete one: that one is not there any more
	CHECK(boot(ESP_RST_SW, step_install_stale) == OUT_RESTART);
	CHECK(state_of(1) == NO_RECORD);

	// Installed: it runs once, to be confirmed, and what it was installed from is the version before it
	CHECK(boot(ESP_RST_SW, step_install) == OUT_RESTART);
	CHECK(state_of(1) == ESP_OTA_IMG_NEW);
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(running_slot == 1 && state_of(1) == ESP_OTA_IMG_PENDING_VERIFY && state_of(0) == ESP_OTA_IMG_VALID);
	CHECK(platform_app->update_pending && platform_app->previous_firmware && !platform_app->rolled_back);
	CHECK(find(STORE_DATA, "upload") == NULL);
	CHECK(strcmp(platform_info.slot, "ota_1") == 0);

	/*
	 * Not confirmed, restarted: the boot loader takes it back and marks it as aborted. What lies in the other
	 * slot now begins like a firmware of this project, and its upload was installed - and it is never the
	 * previous version, after whatever restart.
	 */
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(running_slot == 0 && state_of(0) == ESP_OTA_IMG_VALID && state_of(1) == ESP_OTA_IMG_ABORTED);
	CHECK(!platform_app->update_pending && platform_app->rolled_back && !platform_app->previous_firmware);
	CHECK(boot(ESP_RST_PANIC, NULL) == OUT_IDLE);
	CHECK(platform_app->rolled_back && !platform_app->previous_firmware);

	// The next upload begins: esp_ota_begin() erases the mark of the boot loader, and from here on the record
	// of the upload says that the slot holds no previous version
	CHECK(boot(ESP_RST_SW, step_begin) == OUT_IDLE);
	CHECK(state_of(1) == NO_RECORD && stored_is(STORE_DATA, "upload", "ota_1"));
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(!platform_app->rolled_back && !platform_app->previous_firmware);

	/*
	 * Installed, and confirmed this time: the usual case. The version before the last update lies in the
	 * other slot, valid, and is the previous version from then on.
	 */
	CHECK(boot(ESP_RST_SW, step_install) == OUT_RESTART);
	CHECK(boot(ESP_RST_SW, step_confirm) == OUT_IDLE);
	CHECK(running_slot == 1 && marked_valid == 1 && state_of(1) == ESP_OTA_IMG_VALID && state_of(0) == ESP_OTA_IMG_VALID);
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(running_slot == 1 && !platform_app->update_pending && !platform_app->rolled_back &&
	      platform_app->previous_firmware);
	CHECK(boot(ESP_RST_POWERON, NULL) == OUT_IDLE);
	CHECK(platform_app->previous_firmware);

	// A slot without a firmware is never offered as the previous version
	image_in[0] = false;
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(running_slot == 1 && !platform_app->previous_firmware);
	image_in[0] = true;

	// "Vorherige Version": the boot loader starts it as it starts an update, to be confirmed like one, and
	// the version it was started from is the previous one meanwhile
	CHECK(boot(ESP_RST_SW, step_previous) == OUT_RESTART);
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(running_slot == 0 && state_of(0) == ESP_OTA_IMG_PENDING_VERIFY && state_of(1) == ESP_OTA_IMG_VALID);
	CHECK(platform_app->update_pending && platform_app->previous_firmware);
	// Nobody confirms it: it was taken back as an update is, and is not offered a second time
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(running_slot == 1 && state_of(0) == ESP_OTA_IMG_ABORTED);
	CHECK(platform_app->rolled_back && !platform_app->previous_firmware);

	// A slot that a firmware marked as bad itself (esp_ota_mark_app_invalid_rollback(), which this one never
	// calls) is one that was taken back as well
	set_state_of(0, ESP_OTA_IMG_INVALID);
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(running_slot == 1 && platform_app->rolled_back && !platform_app->previous_firmware);

	flashed_by_usb();
}

static void step_factory(void)
{
	CHECK(find(STORE_CFG, STORE_KEY_SETTINGS) != NULL && find(STORE_CFG, STORE_KEY_AP_PASSWORD) != NULL);
	raise_events(APP_EVENT_FACTORY_RESET | APP_EVENT_STORE_SETTINGS);
	// Raised behind the factory reset: never stored, it would write back what was erased
	raise_events(APP_EVENT_STORE_SETTINGS);
}

static void test_factory_reset(void)
{
	CHECK(boot(ESP_RST_SW, step_factory) == OUT_RESTART);
	CHECK(find(STORE_CFG, STORE_KEY_SETTINGS) == NULL && find(STORE_CFG, STORE_KEY_AP_PASSWORD) == NULL);
	CHECK(find(STORE_DATA, STORE_KEY_LAYOUT) != NULL);
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(!platform_app->settings.night_mode && strcmp(net_password, first_password) != 0 && strlen(net_password) == 10);
}

static void step_take_fifth(void)
{
	// The four jobs are carried out, and the fifth event still waits for somebody to call
	CHECK(platform_app->events == 0 && find(STORE_CFG, STORE_KEY_BOUND) == NULL);
	raise_events(0);
	CHECK(platform_app->events == 0);
}

static void step_flood(void)
{
	fill_queue();
	CHECK(platform_app->events == 0);
	strcpy(platform_app->poll.bound_id, "a1b2c3d4e5f6");
	raise_events(APP_EVENT_STORE_BOUND);
	// No room in the queue: taken all the same, with a copy of what it names (platform.h) ...
	CHECK(platform_app->events == 0 && find(STORE_CFG, STORE_KEY_BOUND) == NULL);
	// ... so that a later change of the app does not reach the flash
	strcpy(platform_app->poll.bound_id, "ffffffffffff");
	script = step_take_fifth;
}

static void test_queue(void)
{
	CHECK(boot(ESP_RST_SW, step_flood) == OUT_IDLE);
	CHECK(stored_is(STORE_CFG, STORE_KEY_BOUND, "a1b2c3d4e5f6"));
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(strcmp(platform_app->poll.bound_id, "a1b2c3d4e5f6") == 0);
}

// app_web.h, with the main task behind by all of its jobs: a save followed by an apply stores the save
static void step_save_then_apply(void)
{
	fill_queue();
	CHECK(layout_call(WEB_ROUTE_LAYOUT_SAVE, layout_a) == 200);
	CHECK(layout_call(WEB_ROUTE_LAYOUT_APPLY, layout_b) == 200);
	CHECK(platform_app->source == APP_LAYOUT_PREVIEW && strcmp(platform_app->layout_text, layout_b) == 0);
	script = step_next_call;
}

// ... a reset followed by a save does not erase the save
static void step_reset_then_save(void)
{
	fill_queue();
	CHECK(layout_call(WEB_ROUTE_LAYOUT_RESET, NULL) == 200);
	CHECK(layout_call(WEB_ROUTE_LAYOUT_SAVE, layout_b) == 200);
	script = step_next_call;
}

// ... and a save followed by a reset leaves no stored layout
static void step_save_then_reset(void)
{
	fill_queue();
	CHECK(layout_call(WEB_ROUTE_LAYOUT_SAVE, layout_a) == 200);
	CHECK(layout_call(WEB_ROUTE_LAYOUT_RESET, NULL) == 200);
	script = step_next_call;
}

static void test_copies(void)
{
	CHECK(boot(ESP_RST_SW, step_save_then_apply) == OUT_IDLE);
	CHECK(stored_is(STORE_DATA, STORE_KEY_LAYOUT, layout_a));

	CHECK(boot(ESP_RST_SW, step_reset_then_save) == OUT_IDLE);
	CHECK(stored_is(STORE_DATA, STORE_KEY_LAYOUT, layout_b));

	// Of the two only the later one counts: the save that was taken back has not become the one before
	CHECK(boot(ESP_RST_SW, step_save_then_reset) == OUT_IDLE);
	CHECK(find(STORE_DATA, STORE_KEY_LAYOUT) == NULL && stored_is(STORE_DATA, STORE_KEY_LAYOUT_PREV, layout_a));
}

// What is raised before the display is gone is carried out before it
static void step_reboot_then_night(void)
{
	raise_events(APP_EVENT_REBOOT);
	do_it(NAV_DO_NIGHT_TOGGLE);
}

static void step_reboot_then_confirm(void)
{
	raise_events(APP_EVENT_REBOOT);
	do_it(NAV_DO_UPDATE_OK);
}

// ... but nothing behind a factory reset, also one that comes second
static void step_reboot_reset_night(void)
{
	raise_events(APP_EVENT_REBOOT);
	raise_events(APP_EVENT_FACTORY_RESET);
	do_it(NAV_DO_NIGHT_TOGGLE);
}

// In the half second of the wait: the screen is dark, nobody begins a request, and the knob stores more
// than there are rooms for - the job of the restart holds one of them
static void wait_flood(void)
{
	CHECK(platform_restarting());
	CHECK(stored_waits());
	strcpy(platform_app->poll.bound_id, "0123456789ab");
	for(int i = 0; i < JOBS - 1; i++)
	{
		raise_events(APP_EVENT_STORE_SETTINGS);
	}
	raise_events(APP_EVENT_STORE_BOUND);
}

static void step_reboot_then_flood(void)
{
	raise_events(APP_EVENT_REBOOT);
	in_restart_wait = wait_flood;
}

static void test_restart(void)
{
	bool night;

	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	night = platform_app->settings.night_mode;
	CHECK(boot(ESP_RST_SW, step_reboot_then_night) == OUT_RESTART && fake_us >= 500000);
	CHECK(restart_waits == 1 && dark_in_wait && held_at_restart);
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(platform_app->settings.night_mode == !night);

	// "Update in Ordnung?" answered just when its time had run out: the update stays
	update_installed();
	marked_valid = 0;
	CHECK(boot(ESP_RST_SW, step_reboot_then_confirm) == OUT_RESTART && running_slot == 1);
	CHECK(marked_valid == 1);
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE && running_slot == 1 && !platform_app->update_pending && !platform_app->rolled_back);
	flashed_by_usb();
	marked_valid = 0;

	// What found no room during the wait gets one when the job of the restart is done
	CHECK(boot(ESP_RST_SW, step_reboot_then_flood) == OUT_RESTART);
	CHECK(restart_waits == 1 && held_at_restart);
	CHECK(stored_is(STORE_CFG, STORE_KEY_BOUND, "0123456789ab"));

	CHECK(boot(ESP_RST_SW, step_reboot_reset_night) == OUT_RESTART);
	CHECK(find(STORE_CFG, STORE_KEY_SETTINGS) == NULL && find(STORE_CFG, STORE_KEY_BOUND) == NULL);
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
}

static void step_stored_after(void)
{
	CHECK(!stored_waits());
}

static void step_stored(void)
{
	CHECK(!stored_waits());
	raise_events(APP_EVENT_STORE_SETTINGS);
	// A job is out
	CHECK(stored_waits());
	script = step_stored_after;
}

// The four jobs are done, and the fifth event still lies where it found no room. Nobody else calls:
// whoever waits hands it on, and is let through when it is carried out.
static void step_stored_hands_on(void)
{
	CHECK(!stored_is(STORE_CFG, STORE_KEY_BOUND, "c0ffee123456") && waiting_jobs->count == 0);
	CHECK(stored_waits());
	CHECK(waiting_jobs->count == 1);
	script = step_stored_after;
}

static void step_stored_spare(void)
{
	fill_queue();
	strcpy(platform_app->poll.bound_id, "c0ffee123456");
	raise_events(APP_EVENT_STORE_BOUND);
	CHECK(stored_waits());
	script = step_stored_hands_on;
}

// platform_stored() lets nobody through before all that was named is carried out
static void test_stored(void)
{
	CHECK(boot(ESP_RST_SW, step_stored) == OUT_IDLE);
	CHECK(boot(ESP_RST_SW, step_stored_spare) == OUT_IDLE);
	CHECK(stored_is(STORE_CFG, STORE_KEY_BOUND, "c0ffee123456"));
}

static void step_begin_broken(void)
{
	store_broken = true;
	CHECK(!platform_upload_begun());
	store_broken = false;
	CHECK(platform_upload_begun());
}

// An upload whose begin cannot be recorded does not begin (platform.h)
static void test_upload_record(void)
{
	CHECK(boot(ESP_RST_SW, step_begin_broken) == OUT_IDLE);
	CHECK(stored_is(STORE_DATA, "upload", "ota_1"));
}

// A file another test keeps, as a text without the line break at its end
static void fixture(const char *path, char *text, size_t size)
{
	FILE *source = fopen(path, "rb");
	size_t length;

	NEED(source != NULL);
	length = fread(text, 1, size, source);
	fclose(source);
	NEED(length > 0 && length < size);
	while(length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r'))
	{
		length--;
	}
	text[length] = '\0';
}

static char catalog_text[CATALOG_ROOM];     // a catalogue as the display stores it
static char catalog_kept[CATALOG_ROOM];     // ... and as the app writes the one it then holds
static int catalog_count;
static char old_list[POLL_TEXT_SIZE];       // a result of the adapter: the list before a clear

// The network task: the adapter named its values, and it accepted a clear
static void step_lists(void)
{
	platform_lock();
	NEED(catalog_from_json(&platform_app->poll.catalog, catalog_text, strlen(catalog_text), platform_app->work,
	                       platform_app->work_count));
	catalog_count = platform_app->poll.catalog.count;
	NEED(catalog_count > 1 && catalog_to_json(&platform_app->poll.catalog, catalog_kept, sizeof(catalog_kept)) > 0);
	strcpy(platform_app->poll.old_text, old_list);
	platform_app->events |= APP_EVENT_STORE_CATALOG | APP_EVENT_STORE_OLD;
	platform_events();
	platform_unlock();
}

// What the display learned from the adapter is stored as it was when the event was raised, and is back
// after a restart
static void test_lists(void)
{
	fixture("../../test/fixtures/catalog_stored.json", catalog_text, sizeof(catalog_text));
	fixture("../../../tools/w906/fixtures/dtc_result_read_codes.json", old_list, sizeof(old_list));

	CHECK(boot(ESP_RST_SW, step_lists) == OUT_IDLE);
	CHECK(stored_is(STORE_DATA, STORE_KEY_CATALOG, catalog_kept));
	CHECK(stored_is(STORE_DATA, STORE_KEY_DTC_OLD, old_list));
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE);
	CHECK(platform_app->poll.catalog.count == catalog_count);
	CHECK(platform_app->poll.has_old && strcmp(platform_app->poll.old_text, old_list) == 0);
}

static void step_frozen(void)
{
	net_frozen = true;
	until_ms = 200000;
	script = step_wait;
}

// The network task stands still: the display restarts after NET_STALL_MS, and not before
static void test_net_watch(void)
{
	net_frozen = false;
	until_ms = 200000;
	CHECK(boot(ESP_RST_SW, step_wait) == OUT_IDLE && platform_now_ms() >= 200000);
	CHECK(boot(ESP_RST_SW, step_frozen) == OUT_RESTART && fake_us >= 60000000 && fake_us < 63000000);
	// A restart nobody's job asked for: the screen is dark and nobody begins a request all the same
	CHECK(restart_waits == 1 && dark_in_wait && held_at_restart);
	net_frozen = false;
	// ... and not at all when the network did not start
	net_result = ESP_FAIL;
	net_frozen = true;
	until_ms = 200000;
	CHECK(boot(ESP_RST_SW, step_wait) == OUT_IDLE && platform_now_ms() >= 200000);
	net_result = ESP_OK;
	net_frozen = false;
}

static void step_wait_70(void)
{
	if(platform_now_ms() < 70000)
	{
		script = step_wait_70;
		return;
	}
	// guard_alive() was due at 60 s: it waits, because a layout was stored at 30 s
	CHECK(guard_memory.layout_fresh == GUARD_MAGIC);
	until_ms = 100000;
	script = step_wait;
}

static void step_save_late(void)
{
	CHECK(save(layout_b) == 200);
	script = step_wait_70;
}

static void step_wait_then_save(void)
{
	script = platform_now_ms() < 30000 ? step_wait_then_save : step_save_late;
}

static void test_guard(void)
{
	CHECK(boot(ESP_RST_POWERON, NULL) == OUT_IDLE);
	CHECK(boot(ESP_RST_PANIC, NULL) == OUT_IDLE && !platform_app->safe_mode);
	CHECK(boot(ESP_RST_BROWNOUT, NULL) == OUT_IDLE && !platform_app->safe_mode);
	CHECK(boot(ESP_RST_TASK_WDT, NULL) == OUT_IDLE && platform_app->safe_mode);
	// No crash, and none forgotten
	CHECK(boot(ESP_RST_USB, NULL) == OUT_IDLE && platform_app->safe_mode && guard_memory.crashes == 3);
	until_ms = 57000;
	CHECK(boot(ESP_RST_SW, step_wait) == OUT_IDLE && guard_memory.crashes == 3 && platform_now_ms() < 60000);
	until_ms = 61500;
	CHECK(boot(ESP_RST_SW, step_wait) == OUT_IDLE && guard_memory.crashes == 0);
	CHECK(boot(ESP_RST_UNKNOWN, NULL) == OUT_IDLE && guard_memory.crashes == 1 && !platform_app->safe_mode);

	// A layout stored after 30 s: guard_alive() waits a whole minute from then
	until_ms = 0;
	CHECK(boot(ESP_RST_SW, step_wait_then_save) == OUT_IDLE);
	CHECK(guard_memory.layout_fresh == 0 && guard_memory.crashes == 0 && platform_now_ms() >= 100000);

	// The knob held through all readings of the start: safe mode. A reading that fails is not held.
	button_pressed = true;
	CHECK(boot(ESP_RST_POWERON, NULL) == OUT_IDLE && platform_app->safe_mode && button_reads == 5 && fake_us >= 80000);
	button_fails_once = true;
	CHECK(boot(ESP_RST_POWERON, NULL) == OUT_IDLE && !platform_app->safe_mode);
	button_pressed = false;
	button_fails_once = false;
}

static void test_failures(void)
{
	// Without the board there is no picture: the display says why and tries again
	board_result = ESP_FAIL;
	CHECK(boot(ESP_RST_SW, NULL) == OUT_RESTART && fake_us >= 5000000 && screens == 0 && nets == 0);
	CHECK(dark_in_give_up);
	board_result = ESP_OK;

	// Without the network it runs, and starts no web server on top of nothing
	net_result = ESP_FAIL;
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE && screens == 1 && nets == 1 && webs == 0);
	// The same while an update waits to be confirmed: it has not passed
	update_installed();
	CHECK(boot(ESP_RST_SW, NULL) == OUT_RESTART && running_slot == 1);
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE && running_slot == 0 && platform_app->rolled_back);
	net_result = ESP_OK;
	flashed_by_usb();

	// Without the store it runs with what is built in, and the access point has a password all the same
	store_broken = true;
	CHECK(boot(ESP_RST_SW, NULL) == OUT_IDLE && screens == 1 && webs == 1 && strlen(net_password) == 10);
	CHECK(platform_app->source != APP_LAYOUT_STORED && platform_app->profile_count == 0);
	store_broken = false;
}

int main(void)
{
	// Two layouts the browser could send: the built-in one as the build embeds it, and the same under
	// another name
	size_t length = (size_t)(builtin_end - builtin_start);
	char *name;

	NEED(length > 0 && length <= LAYOUT_TEXT_MAX);
	memcpy(layout_a, builtin_start, length);
	layout_a[length] = '\0';
	memcpy(layout_b, layout_a, length + 1);
	name = strstr(layout_b, "W906 OM651 Standard");
	NEED(name != NULL);
	memcpy(name, "Another name it has", 19);

	test_first_start();
	test_settings();
	test_layout();
	test_wifi();
	test_firmware();
	test_factory_reset();
	test_queue();
	test_copies();
	test_restart();
	test_stored();
	test_upload_record();
	test_lists();
	test_net_watch();
	test_guard();
	test_failures();

	heap_reset();
	return sim_end();
}
