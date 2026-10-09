/*
 * Runs display/main/screen.c on a PC: the file is included as it is, and its two tasks run against the real
 * core and a device made of stand-ins - a knob, a glass, a backlight, LVGL, the panel, FreeRTOS. "make" in
 * this directory builds and runs it; redproof.py breaks screen.c in one place at a time (mutations/screen.py)
 * and expects a check to fail.
 *
 * One thread, and no clock of the PC: a run is the same on every machine. What stands for the device, and
 * what it rests on (ESP-IDF v5.5.2 and LVGL 9.5.0, read, not run):
 *   FreeRTOS        the scheduler is the simulation. The screen task has the higher priority: it runs
 *                   whenever it is due, and everything else happens while it sleeps in xTaskDelayUntil().
 *                   That function is the one of tasks.c, with the wrap of the tick count; a tick is 10 ms.
 *                   A wake can be made to come late.
 *   the screen task is entered once for a start and left for good at one of its waits when the time of the
 *                   scenario is over: what it knows it keeps in the variables of its function. So a scenario
 *                   says beforehand what the world does and when (`world`), lets the display run, and then
 *                   looks at what each round did (`rounds`) - and every round is looked at when it ends
 *                   (round_over()), in every scenario.
 *   the drawing task
 *                   runs its first round when it is created (it is above the task of main.c, which creates
 *                   it) and then whenever its sleep is over or the screen task has woken it, while that one
 *                   sleeps. Its function is entered for each round and left at its wait: it keeps nothing
 *                   in its variables. What stands before its loop therefore runs once on the device and
 *                   with every round here - the stand-in of the watchdog knows that. A round takes no time.
 *                   A frame that takes long is not drawn by that function: for a span of time the scenario
 *                   names, the lock of LVGL simply belongs to the drawing task, as it does while
 *                   lv_timer_handler() draws.
 *   the two locks   each knows who holds it. Taking one twice, giving back one that is not held, a call of
 *                   LVGL or of the drawing code without the lock of LVGL, a call into the app without the
 *                   lock of the app, a reading of the board or a call of LVGL under the lock of the app, a
 *                   task that sleeps with a lock: failed checks. The lock of LVGL is asked for with a
 *                   time: if the drawing task holds it, that time passes. The lock of the app can be held
 *                   by another task for a while: that time passes as well.
 *   the clock       stands still while a task works, and moves where one waits or is held up
 *   LVGL            lv_timer_handler() asks its clock, flushes two areas through the callback of screen.c
 *                   when something was given to it since the last frame, and returns what the scenario
 *                   says. Everything else is a counted call.
 *   the drawing code (components/ui)
 *                   ui.h is the real header, so that screen.c is built against what it promises; ui_show()
 *                   and ui_row_at() are stand-ins that remember what they were given
 *   the board       a switch that is pressed for spans of time, counts of the encoder that arrive at a
 *                   time, fingers that move over the glass, readings that fail, a backlight that remembers
 *                   what it was set to
 *   the app         the real one. app_button() and the other six calls of the screen task go through
 *                   wrappers that note what screen.c hands over; app_scene() is called a second time when
 *                   a round is over, to compare with what LVGL was given
 *
 * What this cannot see, in short: nothing of ESP-IDF, FreeRTOS or LVGL runs, so every "the scheduler does"
 * above is the model. No task is ever interrupted in the middle of a function, except by the frame described
 * above; there is one core. How long anything takes on the board - app_scene(), ui_show(), a transfer on the
 * I2C bus, an erase of the flash - is not known here: a scenario can say that something takes long, not
 * whether it does. Stacks, the real drawing code and the picture are not here at all.
 */
#include <setjmp.h>
#include <stdint.h>
#include <stdbool.h>
#include "sim.h"

// screen.c calls these of the core; here it calls the wrappers further down instead, which look at what
// it hands over and call the real ones. Not static: a screen.c that leaves one of the calls out is to end in
// a failed check, not in a warning about a function nobody uses.
#include "app.h"

void sim_app_button(app_t *app, bool pressed, bool read_ok, uint64_t now_ms);
void sim_app_encoder(app_t *app, int counts, uint64_t now_ms);
void sim_app_tap(app_t *app, int row, uint64_t now_ms);
void sim_app_swipe(app_t *app, int dx, int dy, uint64_t now_ms);
void sim_app_tick(app_t *app, uint64_t now_ms);
void sim_app_scene(const app_t *app, scene_t *made, uint64_t now_ms);
int sim_app_backlight(const app_t *app, uint64_t now_ms);

#define app_button sim_app_button
#define app_encoder sim_app_encoder
#define app_tap sim_app_tap
#define app_swipe sim_app_swipe
#define app_tick sim_app_tick
#define app_scene sim_app_scene
#define app_backlight sim_app_backlight
#include "screen.c"
#undef app_button
#undef app_encoder
#undef app_tap
#undef app_swipe
#undef app_tick
#undef app_scene
#undef app_backlight

/* What a scenario says of the world -------------------------------------------------------------------- */

#define TICK_US     (1000000u / configTICK_RATE_HZ)
// When the screen task of a start has its first round, in ms: on a tick
#define T0          1000u
#define NEVER       UINT64_MAX
#define PRESSES     4
#define TURNS       40
#define FINGERS     4

typedef struct
{
	uint64_t from_ms;
	uint64_t to_ms;         // not included
} span_t;

// A step of screen_start() that fails
typedef enum
{
	FAIL_NOTHING,
	FAIL_LOCK,              // no memory for the lock of LVGL
	FAIL_POOL,              // no memory for the heap of LVGL
	FAIL_POOL_REFUSED,      // LVGL does not take the heap
	FAIL_BUFFER,            // no memory for the buffer LVGL draws into
	FAIL_DISPLAY,           // LVGL makes no display
	FAIL_DRAWING_TASK,
	FAIL_SCREEN_TASK,
} fail_t;

static struct
{
	span_t press[PRESSES];          // the switch of the knob is pressed
	span_t switch_fails;            // its readings fail
	struct
	{
		uint64_t at_ms;             // counts of the encoder: there with the first reading from then on
		int counts;
	} turn[TURNS];
	struct
	{
		span_t on;                  // a finger on the glass, going from one point to the other
		int x0, y0, x1, y1;
	} finger[FINGERS];
	span_t touch_fails;             // the readings of the touch controller fail
	int row;                        // what the drawing code says lies under a tap
	span_t frame;                   // the drawing task is in the middle of a frame: it holds the lock of LVGL
	uint32_t handler_says;          // what lv_timer_handler() returns: ms until LVGL has something due
	uint64_t stall_at_ms;           // the task is held up between its look at the clock and the reading of
	uint64_t stall_ms;              // the switch that is due then, for so long (the flash is erased)
	uint64_t late_at_ms;            // the wake that is due then comes so much later
	uint64_t late_ms;
	uint64_t lock_at_ms;            // another task holds the lock of the app when it is asked for then,
	uint64_t lock_ms;               // for so long
	uint64_t show_at_ms;            // ui_show() takes so long when it is called then
	uint64_t show_ms;
	uint64_t restart_from_ms;       // platform_restarting() says so
	fail_t fail;
	esp_err_t watchdog;             // what the task watchdog answers a task that subscribes
} world;

static bool within(span_t span, uint64_t ms)
{
	return ms >= span.from_ms && ms < span.to_ms;
}

/* What happened ---------------------------------------------------------------------------------------- */

// The tasks: who runs, and who holds a lock
enum { TASK_NONE, TASK_MAIN, TASK_DRAWING, TASK_SCREEN, TASKS };

// The task of main.c, which calls screen_start() (MAIN_PRIORITY there), and the web server of ESP-IDF
#define MAIN_TASK_PRIORITY  3
#define HTTPD_PRIORITY      5

struct tskTaskControlBlock
{
	TaskFunction_t code;
	UBaseType_t priority;
	bool subscribed;                // to the task watchdog
	int feeds;                      // esp_task_wdt_reset()
};

struct QueueDefinition
{
	int owner;                      // a mutex: the task that holds it
};

// One round of the screen task: from one wait to the next
typedef struct
{
	uint64_t at_ms;                 // its first look at the clock
	int switch_reads;               // board_button()
	uint64_t read_ms;               // when
	bool board_ok, board_pressed;   // what the board said
	int encoder_reads;
	int counts;                     // what the encoder had
	int touch_reads;
	int told;                       // calls into the app
	uint64_t told_ms;               // the time they came with
	int told_switch;                // app_button()
	bool told_ok, told_pressed;
	bool observed;                  // what hold.h made of it: the reading has observed the switch
	int told_counts_calls;          // app_encoder()
	int told_counts;
	int taps;                       // app_tap()
	int tap_row;
	int swipes;                     // app_swipe()
	int dx, dy;
	int ticks;                      // app_tick()
	int wanted;                     // what app_backlight() returned
	bool restarting;                // what platform_restarting() said
	int feeds;                      // esp_task_wdt_reset()
	bool refused;                   // it asked for the lock of LVGL and did not get it
	int lookups;                    // ui_row_at()
	int lookup_x, lookup_y;
	int shows;                      // ui_show()
	int wakes;                      // xTaskNotifyGive() to the drawing task
	int lights;                     // board_backlight()
	bool slept;                     // xTaskDelayUntil() had it wait
	bool frame;                     // the drawing task was in the middle of a frame in that sleep
	int drawn;                      // rounds of the drawing task in that sleep
} round_t;

#define ROUNDS      4096

static round_t rounds[ROUNDS];
static int round_count;
static round_t *this_round;         // of the screen task; NULL while it does not run

static uint64_t now_us;
static uint64_t run_until_us;
static jmp_buf out;
static bool ran;
static int running = TASK_MAIN;
static struct tskTaskControlBlock task[TASKS];
static int tasks_made;
static struct QueueDefinition lvgl_lock;
static int locks_made;
static bool app_locked;
static bool events_owed;            // the app was called and platform_events() has not taken what it raised
static uint32_t events_seen;
static uint32_t pressed_reads;      // readings of the switch that read pressed

static struct
{
	jmp_buf out;
	int rounds;                     // times its function was entered
	TickType_t wait;                // ticks it asked to sleep for at most, the last time
	uint64_t due_us;                // when that sleep is over
	int woken;                      // by the screen task, and not run since
} drawing;

struct esp_lcd_panel_t
{
	int copies;                     // esp_lcd_panel_draw_bitmap()
};

struct _lv_display_t
{
	void *user_data;
	void *buffer;
	uint32_t buffer_size;
	lv_display_flush_cb_t flush;
};

static struct esp_lcd_panel_t the_panel;
static struct _lv_display_t the_display;

static struct
{
	int inits;
	int pools;
	int displays;
	int ui_inits;
	lv_tick_get_cb_t tick;
	bool dirty;                     // something was given to LVGL that it has not drawn
	int frames;                     // lv_timer_handler() drew
	int handler_calls;
	bool flushing;
	lv_area_t area;                 // the one being flushed
	int readies;                    // lv_display_flush_ready()
} lv;

static scene_t shown;               // what ui_show() was given last
static bool shown_any;
static int shows;
static int light = -1;              // what the backlight was set to last; -1: never
static int lights;
static bool tap_open;               // a row was looked up and the app has not been told
static int tap_open_row;
static int tap_open_round;

#define HEAP_BLOCKS 2

static struct
{
	void *block;
	size_t size;
} heap[HEAP_BLOCKS];
static int heap_count;

static round_t *round_now(void)
{
	// What is called outside a round of the screen task is noted in a round nobody looks at
	static round_t nowhere;

	return this_round != NULL ? this_round : &nowhere;
}

static uint64_t now_ms(void)
{
	return now_us / 1000;
}

static void passes_ms(uint64_t ms)
{
	now_us += ms * 1000;
}

/* The rest of the platform ----------------------------------------------------------------------------- */

app_t *platform_app;
platform_info_t platform_info;

void platform_lock(void)
{
	CHECK(!app_locked);
	if(now_ms() >= world.lock_at_ms)
	{
		passes_ms(world.lock_ms);
		world.lock_at_ms = NEVER;
	}
	app_locked = true;
}

void platform_unlock(void)
{
	CHECK(app_locked);
	// What the app raised is taken before the lock is given back (platform.h)
	CHECK(!events_owed);
	app_locked = false;
}

uint64_t platform_now_ms(void)
{
	round_t *round = round_now();

	if(round->at_ms == 0)
	{
		round->at_ms = now_ms();
	}
	return now_ms();
}

void platform_events(void)
{
	CHECK(app_locked);
	events_owed = false;
	events_seen |= app_take_events(platform_app);
}

bool platform_restarting(void)
{
	round_now()->restarting = now_ms() >= world.restart_from_ms;
	return round_now()->restarting;
}

const char *esp_err_to_name(esp_err_t code)
{
	(void)code;
	return "ERR";
}

int64_t esp_timer_get_time(void)
{
	return (int64_t)now_us;
}

static void heap_reset(void)
{
	while(heap_count > 0)
	{
		free(heap[--heap_count].block);
	}
}

void *heap_caps_malloc(size_t size, uint32_t caps)
{
	// First the heap of LVGL, then the buffer it draws into. Both in the external RAM: the internal one
	// is for the WiFi (screen.c).
	CHECK(caps == MALLOC_CAP_SPIRAM);
	if(!CHECK(heap_count < HEAP_BLOCKS) || world.fail == (heap_count == 0 ? FAIL_POOL : FAIL_BUFFER))
	{
		return NULL;
	}
	heap[heap_count].block = sim_alloc(size);
	heap[heap_count].size = size;
	return heap[heap_count++].block;
}

/* The board -------------------------------------------------------------------------------------------- */

bool board_button(bool *pressed)
{
	round_t *round = round_now();

	// A transfer on the I2C bus, which can take its 50 ms: before the lock of the app is taken (screen.c)
	CHECK(!app_locked);
	if(now_ms() >= world.stall_at_ms)
	{
		passes_ms(world.stall_ms);
		world.stall_at_ms = NEVER;
	}
	round->switch_reads++;
	round->read_ms = now_ms();
	round->board_ok = !within(world.switch_fails, now_ms());
	if(!round->board_ok)
	{
		return false;
	}
	round->board_pressed = false;
	for(int i = 0; i < PRESSES; i++)
	{
		round->board_pressed = round->board_pressed || within(world.press[i], now_ms());
	}
	pressed_reads += round->board_pressed ? 1 : 0;
	*pressed = round->board_pressed;
	return true;
}

int board_encoder(void)
{
	round_t *round = round_now();

	CHECK(!app_locked);
	round->encoder_reads++;
	// What was counted since the last call, and counted is counted: nothing is handed out twice
	for(int i = 0; i < TURNS; i++)
	{
		if(now_ms() >= world.turn[i].at_ms)
		{
			round->counts += world.turn[i].counts;
			world.turn[i].at_ms = NEVER;
		}
	}
	return round->counts;
}

bool board_touch(bool *down, int *x, int *y)
{
	const uint64_t ms = now_ms();

	CHECK(!app_locked);
	round_now()->touch_reads++;
	if(within(world.touch_fails, ms))
	{
		return false;
	}
	*down = false;
	for(int i = 0; i < FINGERS; i++)
	{
		if(within(world.finger[i].on, ms))
		{
			const int64_t done = (int64_t)(ms - world.finger[i].on.from_ms);
			const int64_t whole = (int64_t)(world.finger[i].on.to_ms - world.finger[i].on.from_ms);

			*down = true;
			*x = world.finger[i].x0 + (int)((world.finger[i].x1 - world.finger[i].x0) * done / whole);
			*y = world.finger[i].y0 + (int)((world.finger[i].y1 - world.finger[i].y0) * done / whole);
		}
	}
	return true;
}

void board_backlight(int percent)
{
	round_now()->lights++;
	lights++;
	light = percent;
}

/* The app, as the screen task calls it ------------------------------------------------------------------ */

// Every call into the app: under its lock, and with one time for all a round tells it - that of its reading
// (sim_app_button() looks at that)
static void app_called(const app_t *app, uint64_t ms, bool raises)
{
	round_t *round = round_now();

	CHECK(app == platform_app && app_locked && this_round != NULL);
	if(round->told++ == 0)
	{
		round->told_ms = ms;
	}
	CHECK(ms == round->told_ms);
	events_owed = events_owed || raises;
}

void sim_app_button(app_t *app, bool pressed, bool read_ok, uint64_t ms)
{
	round_t *round = round_now();

	round->told_switch++;
	round->told_ok = read_ok;
	round->told_pressed = pressed;
	app_called(app, ms, true);
	// A reading that counts is the one the board took in this round, and it was taken at the time it is told
	// with: not before that time, and - what the scenarios look at - not long after it
	CHECK(round->switch_reads == 1);
	if(read_ok)
	{
		CHECK(round->board_ok && round->read_ms >= ms && pressed == round->board_pressed);
	}
	app_button(app, pressed, read_ok, ms);
}

void sim_app_encoder(app_t *app, int counts, uint64_t ms)
{
	round_t *round = round_now();

	round->told_counts_calls++;
	round->told_counts = counts;
	app_called(app, ms, true);
	// What the encoder had in this round, once
	CHECK(round->encoder_reads == 1 && counts == round->counts);
	app_encoder(app, counts, ms);
}

void sim_app_tap(app_t *app, int row, uint64_t ms)
{
	round_t *round = round_now();

	round->taps++;
	round->tap_row = row;
	app_called(app, ms, true);
	// A tap the drawing code was asked about in the round before this one, with the row it named, once
	CHECK(tap_open && row == tap_open_row && tap_open_round == round_count - 2);
	tap_open = false;
	app_tap(app, row, ms);
}

void sim_app_swipe(app_t *app, int dx, int dy, uint64_t ms)
{
	round_t *round = round_now();

	round->swipes++;
	round->dx = dx;
	round->dy = dy;
	app_called(app, ms, true);
	app_swipe(app, dx, dy, ms);
}

void sim_app_tick(app_t *app, uint64_t ms)
{
	round_now()->ticks++;
	app_called(app, ms, true);
	app_tick(app, ms);
}

void sim_app_scene(const app_t *app, scene_t *made, uint64_t ms)
{
	app_called(app, ms, false);
	app_scene(app, made, ms);
}

int sim_app_backlight(const app_t *app, uint64_t ms)
{
	app_called(app, ms, false);
	round_now()->wanted = app_backlight(app, ms);
	return round_now()->wanted;
}

/* LVGL, the drawing code and the panel ------------------------------------------------------------------ */

// Nothing of LVGL can be called by two tasks at once: a call has the lock of LVGL, or comes before the
// two tasks exist
static void lvgl_called(void)
{
	CHECK(tasks_made == 0 ? running == TASK_MAIN : lvgl_lock.owner == running);
	// ... and what LVGL does can take long: never under the lock of the app (platform.h)
	CHECK(!app_locked);
}

void lv_init(void)
{
	lvgl_called();
	lv.inits++;
}

void lv_tick_set_cb(lv_tick_get_cb_t callback)
{
	lvgl_called();
	lv.tick = callback;
}

lv_mem_pool_t lv_mem_add_pool(void *memory, size_t bytes)
{
	lvgl_called();
	// The whole block that was allocated for it, of the size LVGL was built to take (sdkconfig)
	if(!CHECK(lv.inits == 1 && memory != NULL && heap_count == 1 && memory == heap[0].block))
	{
		return NULL;
	}
	CHECK(bytes == heap[0].size && bytes == 512u * 1024u);
	lv.pools++;
	return world.fail == FAIL_POOL_REFUSED ? NULL : memory;
}

lv_display_t *lv_display_create(int32_t width, int32_t height)
{
	lvgl_called();
	// From the heap that was added: the one lv_init() had is too small for a screen (sdkconfig.defaults)
	CHECK(lv.pools == 1);
	CHECK(width == 480 && height == 480);
	lv.displays++;
	memset(&the_display, 0, sizeof(the_display));
	return world.fail == FAIL_DISPLAY ? NULL : &the_display;
}

void lv_display_set_user_data(lv_display_t *display, void *data)
{
	lvgl_called();
	if(CHECK(display == &the_display))
	{
		display->user_data = data;
	}
}

void *lv_display_get_user_data(lv_display_t *display)
{
	lvgl_called();
	return CHECK(display == &the_display) ? display->user_data : NULL;
}

void lv_display_set_buffers(lv_display_t *display, void *first, void *second, uint32_t size,
                            lv_display_render_mode_t mode)
{
	lvgl_called();
	// One buffer, the block that was allocated for it and not a byte more; whole lines of RGB565, a tenth of
	// the screen at least (screen.c names where that comes from), drawn in parts
	if(CHECK(display == &the_display && first != NULL && heap_count == 2 && first == heap[1].block && size == heap[1].size))
	{
		display->buffer = first;
		display->buffer_size = size;
	}
	CHECK(second == NULL && mode == LV_DISPLAY_RENDER_MODE_PARTIAL);
	CHECK(size % (480 * 2) == 0 && size >= 480 * 480 * 2 / 10);
}

void lv_display_set_flush_cb(lv_display_t *display, lv_display_flush_cb_t callback)
{
	lvgl_called();
	if(CHECK(display == &the_display))
	{
		display->flush = callback;
	}
}

void lv_display_flush_ready(lv_display_t *display)
{
	lvgl_called();
	CHECK(display == &the_display && lv.flushing);
	lv.readies++;
}

esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x_start, int y_start, int x_end, int y_end,
                                    const void *pixels)
{
	// The panel screen_start() was given, the buffer of LVGL, and the area LVGL named - whose last column
	// and row are included, while those of this call are not (esp_lcd_panel_ops.h)
	CHECK(lv.flushing && panel == &the_panel && pixels == the_display.buffer);
	CHECK(x_start == lv.area.x1 && y_start == lv.area.y1 && x_end == lv.area.x2 + 1 && y_end == lv.area.y2 + 1);
	the_panel.copies++;
	return ESP_OK;
}

// LVGL has drawn an area into its buffer and hands it to the display
static void flush(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
	const int copies = the_panel.copies;
	const int readies = lv.readies;

	lv.area = (lv_area_t){ x1, y1, x2, y2 };
	lv.flushing = true;
	the_display.flush(&the_display, &lv.area, the_display.buffer);
	lv.flushing = false;
	// Copied to the panel, and LVGL told that its buffer is free again: it waits for that
	CHECK(the_panel.copies == copies + 1 && lv.readies == readies + 1);
}

uint32_t lv_timer_handler(void)
{
	lvgl_called();
	lv.handler_calls++;
	// The drawing task finds the screen of ui_init(), not an empty one (screen.c)
	CHECK(lv.ui_inits == 1);
	// The clock of LVGL is the one of the display, in ms
	CHECK(lv.tick != NULL && lv.tick() == (uint32_t)now_ms());
	if(lv.dirty && CHECK(the_display.flush != NULL && the_display.buffer != NULL))
	{
		// As many lines from the top as the buffer holds, and a part somewhere in the middle
		flush(0, 0, 479, (int32_t)(the_display.buffer_size / (480 * 2)) - 1);
		flush(12, 345, 111, 378);
		lv.frames++;
	}
	lv.dirty = false;
	return world.handler_says;
}

void ui_init(lv_display_t *display)
{
	lvgl_called();
	CHECK(display == &the_display && lv.pools == 1 && display->buffer != NULL && display->flush != NULL);
	lv.ui_inits++;
	lv.dirty = true;
}

void ui_show(const scene_t *given_scene)
{
	lvgl_called();
	// Only a scene that differs from the one LVGL has (screen.c: laying out and measuring is what takes long)
	CHECK(!shown_any || memcmp(given_scene, &shown, sizeof(shown)) != 0);
	memcpy(&shown, given_scene, sizeof(shown));
	shown_any = true;
	shows++;
	round_now()->shows++;
	lv.dirty = true;
	if(now_ms() >= world.show_at_ms)
	{
		passes_ms(world.show_ms);
		world.show_at_ms = NEVER;
	}
}

int ui_row_at(int x, int y)
{
	round_t *round = round_now();

	lvgl_called();
	// Asked before a new scene is laid out: the row is one of the scene the finger saw (screen.c)
	CHECK(round->shows == 0);
	// One tap at a time: the one before it was told to the app
	CHECK(!tap_open);
	round->lookups++;
	round->lookup_x = x;
	round->lookup_y = y;
	tap_open = true;
	tap_open_row = world.row;
	tap_open_round = round_count - 1;
	return world.row;
}

/* FreeRTOS --------------------------------------------------------------------------------------------- */

BaseType_t xPortGetCoreID(void)
{
	// The task of main.c runs on the second core (PANEL_CORE there)
	return 1;
}

TickType_t xTaskGetTickCount(void)
{
	return (TickType_t)(now_us / TICK_US);
}

QueueHandle_t xQueueCreateMutex(const uint8_t type)
{
	(void)type;
	locks_made++;
	return world.fail == FAIL_LOCK ? NULL : &lvgl_lock;
}

BaseType_t xQueueSemaphoreTake(QueueHandle_t queue, TickType_t wait)
{
	if(!CHECK(queue == &lvgl_lock))
	{
		return pdFALSE;
	}
	// Under the lock of the app nobody waits for anything (platform.h)
	CHECK(!app_locked);
	// A mutex of FreeRTOS that its holder asks for again never comes free
	if(!CHECK(lvgl_lock.owner != running))
	{
		return pdFALSE;
	}
	if(lvgl_lock.owner != TASK_NONE)
	{
		// The drawing task is in the middle of a frame: the screen task waits as long as it said it would,
		// and gets the lock if the frame ends in that time
		const uint64_t free_us = world.frame.to_ms * 1000;
		const uint64_t patience_us = wait == portMAX_DELAY ? NEVER : (now_us / TICK_US + wait) * TICK_US;

		// The other way round - the drawing task finds the lock with a screen task that sleeps - it would
		// wait for ever
		if(!CHECK(running == TASK_SCREEN && lvgl_lock.owner == TASK_DRAWING))
		{
			return pdFALSE;
		}
		if(free_us > patience_us)
		{
			now_us = patience_us > now_us ? patience_us : now_us;
			round_now()->refused = true;
			return pdFALSE;
		}
		now_us = free_us > now_us ? free_us : now_us;
		world.frame.from_ms = NEVER;
		drawing.due_us = now_us;
	}
	lvgl_lock.owner = running;
	return pdTRUE;
}

BaseType_t xQueueGenericSend(QueueHandle_t queue, const void *const item, TickType_t wait, const BaseType_t position)
{
	(void)wait;
	(void)position;
	// A mutex is given back, by the task that holds it
	if(CHECK(queue == &lvgl_lock && item == NULL && lvgl_lock.owner == running))
	{
		lvgl_lock.owner = TASK_NONE;
	}
	return pdTRUE;
}

esp_err_t esp_task_wdt_add(TaskHandle_t handle)
{
	// Each of the two tasks subscribes itself
	CHECK(handle == NULL && (running == TASK_DRAWING || running == TASK_SCREEN));
	// The function of the drawing task is entered for every round here and once on the device (see the head
	// of this file): what stands before its loop counts once
	if(running == TASK_DRAWING && drawing.rounds > 1)
	{
		return ESP_OK;
	}
	// The watchdog refuses a task it has already
	if(!CHECK(!task[running].subscribed))
	{
		return ESP_ERR_INVALID_ARG;
	}
	if(world.watchdog != ESP_OK)
	{
		return world.watchdog;
	}
	task[running].subscribed = true;
	return ESP_OK;
}

esp_err_t esp_task_wdt_reset(void)
{
	if(world.watchdog != ESP_OK)
	{
		return ESP_ERR_NOT_FOUND;
	}
	// The watchdog refuses a task it does not know, and would never notice that it hangs
	if(!CHECK(task[running].subscribed))
	{
		return ESP_ERR_NOT_FOUND;
	}
	task[running].feeds++;
	if(running == TASK_SCREEN)
	{
		round_now()->feeds++;
	}
	return ESP_OK;
}

// One round of the drawing task: its function is entered, and left at its wait
static void drawing_round(void)
{
	static int before;

	before = running;
	running = TASK_DRAWING;
	drawing.rounds++;
	if(setjmp(drawing.out) == 0)
	{
		task[TASK_DRAWING].code(NULL);
	}
	running = before;
	// It sleeps with nothing in its hands
	CHECK(lvgl_lock.owner != TASK_DRAWING && !app_locked);
	// A wait of FreeRTOS ends with a tick. One of no tick would not be a wait: the task would turn for
	// ever, and nothing below it on its core would run again.
	if(!CHECK(drawing.wait > 0))
	{
		drawing.wait = 1;
	}
	drawing.due_us = drawing.wait == portMAX_DELAY ? NEVER : (now_us / TICK_US + drawing.wait) * TICK_US;
}

uint32_t ulTaskGenericNotifyTake(UBaseType_t index, BaseType_t clear, TickType_t wait)
{
	(void)clear;
	// The wait of the drawing task: its round is over
	NEED(running == TASK_DRAWING && index == tskDEFAULT_INDEX_TO_NOTIFY);
	drawing.wait = wait;
	longjmp(drawing.out, 1);
}

BaseType_t xTaskGenericNotify(TaskHandle_t handle, UBaseType_t index, uint32_t value, eNotifyAction action,
                              uint32_t *previous)
{
	(void)value;
	(void)previous;
	// The screen task wakes the drawing task
	CHECK(running == TASK_SCREEN && handle == &task[TASK_DRAWING] && task[TASK_DRAWING].code != NULL);
	CHECK(index == tskDEFAULT_INDEX_TO_NOTIFY && action == eIncrement);
	round_now()->wakes++;
	drawing.woken++;
	return pdPASS;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t code, const char *const name, const uint32_t stack, void *const arg,
                                   UBaseType_t priority, TaskHandle_t *const created, const BaseType_t core)
{
	// The task whose handle screen.c keeps is the one it wakes: the drawing task
	const int which = created != NULL ? TASK_DRAWING : TASK_SCREEN;

	(void)name;
	(void)stack;
	(void)arg;
	// On the core of the task that started the panel, where its interrupts are (screen.c)
	CHECK(core == xPortGetCoreID());
	// Everything of LVGL the start has to do is done: from here on it belongs to the two tasks
	CHECK(lv.ui_inits == 1);
	if(!CHECK(task[which].code == NULL) || world.fail == (which == TASK_DRAWING ? FAIL_DRAWING_TASK : FAIL_SCREEN_TASK))
	{
		return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
	}
	task[which].code = code;
	task[which].priority = priority;
	tasks_made++;
	if(created != NULL)
	{
		*created = &task[which];
	}
	if(which == TASK_DRAWING)
	{
		// Above the task that creates it: it runs at once, until it waits
		CHECK(priority > MAIN_TASK_PRIORITY);
		drawing_round();
	}
	else
	{
		// Drawing never delays a reading, and neither does a request the web server answers (screen.c).
		// It has its first round when run() enters it.
		CHECK(task[TASK_DRAWING].code != NULL && priority > task[TASK_DRAWING].priority && priority > HTTPD_PRIORITY);
	}
	return pdPASS;
}

// A round of the screen task begins
static void round_begins(void)
{
	NEED(round_count < ROUNDS);
	this_round = &rounds[round_count++];
	memset(this_round, 0, sizeof(*this_round));
}

// A round of the screen task is over: it waits now. What holds for every round, whatever the scenario.
static void round_over(void)
{
	static scene_t truth;
	static int light_before = -1;
	round_t *round = this_round;
	const int index = round_count - 1;
	int wanted;

	if(index == 0)
	{
		light_before = -1;
	}
	// It sleeps with nothing in its hands
	CHECK(!app_locked && lvgl_lock.owner != TASK_SCREEN);
	// One reading of the switch and of the encoder, the app told of each once, and the watchdog fed
	CHECK(round->switch_reads == 1 && round->encoder_reads == 1);
	CHECK(round->told_switch == 1 && round->told_counts_calls == 1);
	CHECK(world.watchdog != ESP_OK || round->feeds == 1);
	// The touch controller every 40 ms, the tick of the app five times a second
	CHECK(round->touch_reads == (index % 2 == 0 ? 1 : 0));
	CHECK(round->ticks == (index % 10 == 0 ? 1 : 0));
	// A gesture ends with a reading of the touch controller, and is told then and never again
	CHECK(round->swipes <= round->touch_reads && round->taps <= 1);
	round->observed = platform_app->hold.watched;

	// LVGL has the scene the app makes of this very round - unless the drawing task had the lock
	app_scene(platform_app, &truth, round->told_ms);
	CHECK(round->refused || (shown_any && memcmp(&truth, &shown, sizeof(shown)) == 0));
	// ... and the drawing task is woken when it was given a scene, and only then
	CHECK(round->wakes == round->shows);

	// The backlight is what the app wants, and dark in every round that begins when the display is about to
	// restart (main.c says why); the board is told when that changes, and only then
	wanted = (round->restarting || round->at_ms >= world.restart_from_ms) ? 0 : round->wanted;
	CHECK(light == wanted && round->lights == (wanted != light_before ? 1 : 0));
	light_before = light;
	this_round = NULL;
}

// The screen task sleeps until then: the rest of the world has the processor
static void sleeps_until(uint64_t wake_us, round_t *round)
{
	for(;;)
	{
		const bool in_frame = lvgl_lock.owner == TASK_DRAWING;
		// The frame the scenario names begins or ends, or the drawing task has a round: what comes first
		const uint64_t frame_us = (in_frame ? world.frame.to_ms : world.frame.from_ms) * 1000;
		const uint64_t round_us = in_frame ? NEVER : drawing.woken > 0 ? now_us : drawing.due_us;
		const bool frame_next = world.frame.from_ms != NEVER && frame_us <= round_us;
		const uint64_t next_us = frame_next ? frame_us : round_us;

		// What is due with the wake itself waits: the screen task goes first
		if(next_us >= wake_us)
		{
			break;
		}
		now_us = next_us > now_us ? next_us : now_us;
		if(!frame_next)
		{
			drawing.woken = 0;
			round->drawn++;
			drawing_round();
		}
		else if(!in_frame)
		{
			// lv_timer_handler() draws what it was given, for as long as the scenario says
			lvgl_lock.owner = TASK_DRAWING;
			lv.dirty = false;
			round->frame = true;
		}
		else
		{
			// The frame is done: the drawing task gives the lock back and looks what else there is
			lvgl_lock.owner = TASK_NONE;
			world.frame.from_ms = NEVER;
			drawing.due_us = now_us;
			round->frame = true;
		}
	}
	round->frame = round->frame || lvgl_lock.owner == TASK_DRAWING;
}

BaseType_t xTaskDelayUntil(TickType_t *const previous, const TickType_t increment)
{
	const TickType_t count = xTaskGetTickCount();
	const TickType_t wake = *previous + increment;
	round_t *round = this_round;
	BaseType_t delay;

	NEED(running == TASK_SCREEN && round != NULL);
	// tasks.c asserts this
	CHECK(increment > 0);
	// tasks.c: the task waits if the time to wake lies ahead of the tick count, whichever of the two has
	// wrapped since the time before
	if(count < *previous)
	{
		delay = wake < *previous && wake > count ? pdTRUE : pdFALSE;
	}
	else
	{
		delay = wake < *previous || wake > count ? pdTRUE : pdFALSE;
	}
	*previous = wake;

	round_over();
	round->slept = delay == pdTRUE;
	if(delay == pdTRUE)
	{
		// As many ticks from now as the wake lies ahead
		const uint64_t wake_us = (now_us / TICK_US + (TickType_t)(wake - count)) * TICK_US;

		running = TASK_NONE;
		sleeps_until(wake_us, round);
		running = TASK_SCREEN;
		now_us = wake_us;
		if(wake_us / 1000 >= world.late_at_ms)
		{
			// Nobody ran in between: the flash was erased
			passes_ms(world.late_ms);
			world.late_at_ms = NEVER;
		}
		// What the screen task handed over was drawn while it slept, not when the sleep of the drawing task
		// was over at last (screen.c) - unless a frame was being drawn already
		CHECK(round->frame || !lv.dirty);
	}
	if(now_us >= run_until_us)
	{
		longjmp(out, 1);
	}
	round_begins();
	return delay;
}

/* A start ---------------------------------------------------------------------------------------------- */

extern const char builtin_start[] __asm__("_binary_w906_default_json_start");
extern const char builtin_end[] __asm__("_binary_w906_default_json_end");

static void state(void)
{
	printf("   %d rounds, now %llu ms, running %d, lock of LVGL %d, shows %d, light %d\n", round_count,
	       (unsigned long long)now_ms(), running, lvgl_lock.owner, shows, light);
}

/*
 * The power comes on: a display that has not called screen_start() yet. Everything is as a chip has it after
 * a reset - the heap empty, the variables of screen.c as they are written there (a variable that is added to
 * screen.c has to be added here), a new app - and the world is quiet: nobody touches anything, nothing
 * fails, nothing takes time. The scenario says what is to happen, then run(). The time is the one at which
 * the screen task will have its first round.
 */
static void power_on_at(uint64_t ms)
{
	static json_token_t work[LAYOUT_TOKENS];
	app_boot_t boot;

	heap_reset();
	memset(&scene, 0, sizeof(scene));
	memset(&given, 0, sizeof(given));
	lvgl_mutex = NULL;
	lvgl_handle = NULL;

	memset(&world, 0, sizeof(world));
	for(int i = 0; i < PRESSES; i++)
	{
		world.press[i].from_ms = NEVER;
	}
	for(int i = 0; i < TURNS; i++)
	{
		world.turn[i].at_ms = NEVER;
	}
	for(int i = 0; i < FINGERS; i++)
	{
		world.finger[i].on.from_ms = NEVER;
	}
	world.switch_fails.from_ms = world.touch_fails.from_ms = world.frame.from_ms = NEVER;
	world.stall_at_ms = world.late_at_ms = world.lock_at_ms = world.show_at_ms = world.restart_from_ms = NEVER;
	world.row = -1;
	world.handler_says = LV_NO_TIMER_READY;
	world.watchdog = ESP_OK;

	round_count = 0;
	this_round = NULL;
	now_us = ms * 1000;
	run_until_us = 0;
	ran = false;
	running = TASK_MAIN;
	memset(task, 0, sizeof(task));
	tasks_made = 0;
	lvgl_lock.owner = TASK_NONE;
	locks_made = 0;
	app_locked = false;
	events_owed = false;
	events_seen = 0;
	pressed_reads = 0;
	memset(&drawing, 0, sizeof(drawing));
	memset(&the_panel, 0, sizeof(the_panel));
	memset(&the_display, 0, sizeof(the_display));
	memset(&lv, 0, sizeof(lv));
	shown_any = false;
	shows = 0;
	light = -1;
	lights = 0;
	tap_open = false;
	sim_log_wanted = "backlight";
	sim_log_found = 0;

	if(platform_app == NULL)
	{
		platform_app = sim_alloc(sizeof(app_t));
	}
	memset(&boot, 0, sizeof(boot));
	boot.version = "0.1.0";
	boot.git = "sim";
	boot.builtin_layout = builtin_start;
	boot.builtin_length = (size_t)(builtin_end - builtin_start);
	boot.work = work;
	boot.work_count = LAYOUT_TOKENS;
	app_init(platform_app, &boot, now_ms());
}

static void power_on(void)
{
	power_on_at(T0);
}

// What the rounds of a run add up to
static struct
{
	int taps, lookups, swipes, refused;
	int counts;                     // the app was told of
	uint64_t least_gap_ms;          // between two readings of the switch that follow each other
	uint64_t most_gap_ms;
} sum;

// The display starts and runs for so long: screen_start(), and the screen task from its first round to
// the wait at which the time is over. Once for a start (see the head of this file).
static void run(uint64_t ms)
{
	NEED(!ran);
	ran = true;
	memset(&sum, 0, sizeof(sum));
	if(!CHECK(screen_start(&the_panel) == ESP_OK && task[TASK_SCREEN].code != NULL))
	{
		return;
	}
	run_until_us = now_us + ms * 1000;
	if(setjmp(out) == 0)
	{
		running = TASK_SCREEN;
		round_begins();
		task[TASK_SCREEN].code(NULL);
	}
	running = TASK_MAIN;
	this_round = NULL;

	sum.least_gap_ms = UINT64_MAX;
	for(int i = 0; i < round_count; i++)
	{
		sum.taps += rounds[i].taps;
		sum.lookups += rounds[i].lookups;
		sum.swipes += rounds[i].swipes;
		sum.refused += rounds[i].refused ? 1 : 0;
		sum.counts += rounds[i].told_counts;
		if(i > 0)
		{
			const uint64_t gap_ms = rounds[i].read_ms - rounds[i - 1].read_ms;

			sum.least_gap_ms = gap_ms < sum.least_gap_ms ? gap_ms : sum.least_gap_ms;
			sum.most_gap_ms = gap_ms > sum.most_gap_ms ? gap_ms : sum.most_gap_ms;
		}
	}
}

// The round that began at this time. If there is none, that is a failed check, and what the scenario then
// looks at is a round in which nothing happened.
static const round_t *round_of(uint64_t ms)
{
	static const round_t none;
	const bool a_round_began_at_the_time_the_scenario_names = false;

	for(int i = 0; i < round_count; i++)
	{
		if(rounds[i].at_ms == ms)
		{
			return &rounds[i];
		}
	}
	printf("   no round began at %llu ms\n", (unsigned long long)ms);
	CHECK(a_round_began_at_the_time_the_scenario_names);
	return &none;
}

/* The scenarios ---------------------------------------------------------------------------------------- */

// What screen_start() needs and does not get ends it, before anything that would need it
static void test_start_fails(void)
{
	power_on();
	CHECK(screen_start(NULL) == ESP_ERR_INVALID_ARG);
	CHECK(locks_made == 0 && lv.inits == 0 && heap_count == 0 && tasks_made == 0);

	power_on();
	world.fail = FAIL_LOCK;
	CHECK(screen_start(&the_panel) == ESP_ERR_NO_MEM && lv.inits == 0 && tasks_made == 0);

	power_on();
	world.fail = FAIL_POOL;
	CHECK(screen_start(&the_panel) == ESP_ERR_NO_MEM && lv.inits == 1 && lv.pools == 0 && lv.displays == 0 && tasks_made == 0);

	power_on();
	world.fail = FAIL_POOL_REFUSED;
	CHECK(screen_start(&the_panel) == ESP_ERR_NO_MEM && lv.displays == 0 && tasks_made == 0);

	power_on();
	world.fail = FAIL_BUFFER;
	CHECK(screen_start(&the_panel) == ESP_ERR_NO_MEM && lv.displays == 0 && tasks_made == 0);

	power_on();
	world.fail = FAIL_DISPLAY;
	CHECK(screen_start(&the_panel) == ESP_ERR_NO_MEM && lv.ui_inits == 0 && tasks_made == 0);

	power_on();
	world.fail = FAIL_DRAWING_TASK;
	CHECK(screen_start(&the_panel) == ESP_ERR_NO_MEM && tasks_made == 0);

	power_on();
	world.fail = FAIL_SCREEN_TASK;
	CHECK(screen_start(&the_panel) == ESP_ERR_NO_MEM && tasks_made == 1);
}

// A start: LVGL is attached to the panel, and the drawing task has drawn the screen of ui_init() before
// the screen task has its first round
static void test_start(void)
{
	power_on();
	CHECK(screen_start(&the_panel) == ESP_OK);
	CHECK(locks_made == 1 && lv.inits == 1 && lv.pools == 1 && lv.displays == 1 && lv.ui_inits == 1 && tasks_made == 2);
	CHECK(lv.tick != NULL && the_display.flush != NULL && the_display.user_data == &the_panel);
	// A sixth of the screen: 80 lines of 480 pixels of two bytes
	CHECK(the_display.buffer_size == 76800);
	CHECK(drawing.rounds == 1 && task[TASK_DRAWING].subscribed && task[TASK_DRAWING].feeds == 1);
	CHECK(lv.handler_calls == 1 && lv.frames == 1 && the_panel.copies == 2 && lv.readies == 2);
	CHECK(round_count == 0 && shows == 0 && lights == 0);
}

// A display nobody touches, for a second: the rhythm of the two tasks
static void test_rhythm(void)
{
	power_on();
	run(1000);
	// Fifty readings of the switch, 20 ms apart, the first at once; each told to the app as it was read
	CHECK(round_count == 50 && rounds[0].read_ms == T0 && sum.least_gap_ms == 20 && sum.most_gap_ms == 20);
	CHECK(pressed_reads == 0 && rounds[49].told_ok && !rounds[49].told_pressed && rounds[49].told_ms == T0 + 980);
	CHECK(task[TASK_SCREEN].subscribed && task[TASK_SCREEN].feeds == 50);
	// The first round gives LVGL its first scene and switches the backlight on, and the log says so. A scene
	// and a brightness that stay are given once.
	CHECK(rounds[0].shows == 1 && shows == 1 && rounds[0].lights == 1 && lights == 1 && light > 0 && sim_log_found == 1);
	// The drawing task: the frame at its start, the one it was woken for, and a look every 50 ms while LVGL
	// has nothing due - 19 of them in this second. Fed with every round.
	CHECK(drawing.rounds == 21 && task[TASK_DRAWING].feeds == 21 && lv.handler_calls == 21 && lv.frames == 2);
	CHECK(rounds[0].drawn == 1 && drawing.wait == 5);
}

// A round that came late or took long is not made up for: no two readings back to back
static void test_late(void)
{
	// A wake that comes 90 ms late (the flash was erased): the reading behind it is a whole round away from
	// the next
	power_on();
	world.late_at_ms = T0 + 200;
	world.late_ms = 90;
	run(600);
	CHECK(sum.most_gap_ms == 110 && sum.least_gap_ms == 20);
	CHECK(round_of(T0 + 290)->read_ms == T0 + 290 && round_of(T0 + 310)->read_ms == T0 + 310);

	// A round that takes 35 ms (the drawing code lays out a new page): the next one follows at once, and the
	// one behind it comes with the second tick from then - at least a tick later, at most a round
	power_on();
	world.turn[0].at_ms = T0 + 200;
	world.turn[0].counts = KNOB_COUNTS_PER_DETENT;
	world.show_at_ms = T0 + 200;
	world.show_ms = 35;
	run(600);
	CHECK(sum.most_gap_ms == 35 && sum.least_gap_ms == 15);
	CHECK(round_of(T0 + 200)->shows == 1 && !round_of(T0 + 200)->slept);
	CHECK(round_of(T0 + 235)->read_ms == T0 + 235 && round_of(T0 + 250)->read_ms == T0 + 250 && round_of(T0 + 270)->read_ms == T0 + 270);

	// The tick count of FreeRTOS wraps, after 497 days: the rhythm stays
	power_on_at(UINT64_C(42949672960) - 100);
	run(300);
	CHECK(round_count == 15 && rounds[5].read_ms == UINT64_C(42949672960) && sum.least_gap_ms == 20 && sum.most_gap_ms == 20);
}

// How long the drawing task sleeps: as long as LVGL says nothing is due, 50 ms at most, and never no tick
// at all
static void test_drawing_sleeps(void)
{
	power_on();
	world.handler_says = 0;
	run(100);
	CHECK(drawing.wait == 1);
	// Every tick then: the first round, the one it was woken for, and nine more
	CHECK(drawing.rounds == 11);

	power_on();
	world.handler_says = 7;
	run(100);
	CHECK(drawing.wait == 1);

	power_on();
	world.handler_says = 30;
	run(100);
	CHECK(drawing.wait == 3);

	power_on();
	world.handler_says = 51;
	run(100);
	CHECK(drawing.wait == 5);
}

// A press through the real app: what it changes is with LVGL in the round in which the app has it, wherever
// between two ticks of the app it falls (round_over() compares in every round)
static void test_press(void)
{
	for(unsigned phase = 0; phase < 10; phase++)
	{
		const uint64_t down_ms = T0 + 200 + 20 * phase;

		// A short press on a value page: the menu
		power_on();
		world.press[0] = (span_t){ down_ms, down_ms + 100 };
		run(600);
		CHECK(pressed_reads == 5 && shows == 2 && shown.kind == SCENE_LIST);

		// ... and a long one in the menu: back to the page, which is the first scene again
		power_on();
		world.press[0] = (span_t){ down_ms, down_ms + 100 };
		world.press[1] = (span_t){ down_ms + 300, down_ms + 1300 };
		run(1800);
		CHECK(pressed_reads == 55 && shows == 3 && shown.kind != SCENE_LIST);
	}
}

// A detent of the knob: the counts go to the app in the round the encoder had them, once, and the next
// page is with LVGL in that round
static void test_turn(void)
{
	power_on();
	world.turn[0].at_ms = T0 + 300;
	world.turn[0].counts = KNOB_COUNTS_PER_DETENT;
	run(600);
	CHECK(round_of(T0 + 300)->told_counts == KNOB_COUNTS_PER_DETENT && sum.counts == KNOB_COUNTS_PER_DETENT);
	CHECK(round_of(T0 + 300)->shows == 1 && shows == 2);

	// Counts that came while a round was under way are those of the next one
	power_on();
	world.turn[0].at_ms = T0 + 301;
	world.turn[0].counts = -(KNOB_COUNTS_PER_DETENT - 1);
	world.turn[1].at_ms = T0 + 315;
	world.turn[1].counts = -1;
	run(600);
	CHECK(round_of(T0 + 300)->told_counts == 0 && round_of(T0 + 320)->told_counts == -KNOB_COUNTS_PER_DETENT && sum.counts == -KNOB_COUNTS_PER_DETENT);
}

// A reading that was held up between its time and the bus has observed nothing: the app is told that it
// failed. One that took as long as a reading may take counts.
static void test_held_up(void)
{
	power_on();
	world.stall_at_ms = T0 + 100;
	world.stall_ms = 30;
	run(300);
	CHECK(round_of(T0 + 100)->read_ms == T0 + 130 && round_of(T0 + 100)->board_ok && !round_of(T0 + 100)->told_ok);
	// ... which is what the clear dialog goes by: a hold does not go on across it (hold.h)
	CHECK(round_of(T0 + 80)->observed && !round_of(T0 + 100)->observed);
	// The next round follows at once, and its reading counts
	CHECK(round_of(T0 + 130)->told_ok && round_of(T0 + 130)->observed && round_of(T0 + 150)->told_ok);

	power_on();
	world.stall_at_ms = T0 + 100;
	world.stall_ms = 10;
	run(300);
	CHECK(round_of(T0 + 100)->read_ms == T0 + 110 && round_of(T0 + 100)->told_ok && round_of(T0 + 100)->observed);

	power_on();
	world.stall_at_ms = T0 + 100;
	world.stall_ms = 11;
	run(300);
	CHECK(round_of(T0 + 100)->read_ms == T0 + 111 && !round_of(T0 + 100)->told_ok);

	// A reading the board could not take is told as it is
	power_on();
	world.switch_fails = (span_t){ T0 + 100, T0 + 140 };
	run(300);
	CHECK(round_of(T0 + 80)->told_ok && !round_of(T0 + 100)->told_ok && !round_of(T0 + 120)->told_ok && round_of(T0 + 140)->told_ok);
}

// Another task holds the lock of the app for a while: the app hears the time of the reading, not the time
// at which the screen task got the lock (app.h)
static void test_lock_of_the_app(void)
{
	power_on();
	world.lock_at_ms = T0 + 100;
	world.lock_ms = 40;
	world.press[0] = (span_t){ T0 + 100, T0 + 300 };
	run(300);
	CHECK(round_of(T0 + 100)->read_ms == T0 + 100 && round_of(T0 + 100)->told_ms == T0 + 100);
	CHECK(round_of(T0 + 100)->told_ok && round_of(T0 + 100)->told_pressed);
	// The round took 40 ms: the next one follows at once, and the one after it 20 ms later
	CHECK(round_of(T0 + 140)->read_ms == T0 + 140 && round_of(T0 + 160)->read_ms == T0 + 160);
}

// The drawing task is in the middle of a frame and holds the lock of LVGL: the screen task does not wait
// for it. The readings go on, and the scene is given when the lock is free - the one of that round.
static void test_frame(void)
{
	power_on();
	world.frame = (span_t){ T0 + 190, T0 + 290 };
	world.turn[0].at_ms = T0 + 200;
	world.turn[0].counts = KNOB_COUNTS_PER_DETENT;
	run(600);
	CHECK(sum.least_gap_ms == 20 && sum.most_gap_ms == 20);
	CHECK(round_of(T0 + 200)->refused && round_of(T0 + 280)->refused && sum.refused == 5);
	CHECK(round_of(T0 + 300)->shows == 1 && shows == 2);
	// What LVGL is to draw next is drawn as soon as the screen task sleeps
	CHECK(round_of(T0 + 300)->drawn == 1 && lv.frames == 3);
}

// A tap: the drawing code is asked under the lock of LVGL which row lies at the point where the finger went
// down, and the app is told in the round after that one - once
static void test_tap(void)
{
	// A tap that hits no row is one all the same
	power_on();
	world.finger[0].on = (span_t){ T0 + 200, T0 + 300 };
	world.finger[0].x0 = world.finger[0].x1 = 100;
	world.finger[0].y0 = world.finger[0].y1 = 300;
	run(600);
	// The finger counts as lifted with the second reading of the touch controller that finds none
	CHECK(round_of(T0 + 360)->lookups == 1 && round_of(T0 + 360)->lookup_x == 100 && round_of(T0 + 360)->lookup_y == 300);
	// Nothing new to draw in that round, so nobody was woken (round_over() looks)
	CHECK(round_of(T0 + 360)->shows == 0);
	CHECK(round_of(T0 + 380)->taps == 1 && round_of(T0 + 380)->tap_row == -1 && sum.taps == 1 && sum.lookups == 1);

	// ... it wakes a screen that went dark for want of anything to show (app.h: after a minute, as the
	// settings are at the start). The board hears of dark and of lit once each, and so does the log.
	power_on();
	world.finger[0].on = (span_t){ T0 + 61000, T0 + 61100 };
	world.finger[0].x0 = world.finger[0].x1 = 100;
	world.finger[0].y0 = world.finger[0].y1 = 300;
	run(62000);
	CHECK(round_of(T0 + 30000)->wanted > 0 && round_of(T0 + 61000)->wanted == 0 && round_of(T0 + 61160)->lookups == 1);
	CHECK(round_of(T0 + 61180)->taps == 1 && round_of(T0 + 61180)->tap_row == -1 && round_of(T0 + 61180)->lights == 1);
	CHECK(light > 0 && lights == 3 && sim_log_found == 3);

	// The row the drawing code names is the row the app hears
	power_on();
	world.row = 2;
	world.finger[0].on = (span_t){ T0 + 200, T0 + 300 };
	world.finger[0].x0 = world.finger[0].x1 = 240;
	world.finger[0].y0 = world.finger[0].y1 = 150;
	run(600);
	CHECK(round_of(T0 + 380)->taps == 1 && round_of(T0 + 380)->tap_row == 2 && sum.taps == 1 && sum.lookups == 1);

	// The drawing task holds the lock when the finger is lifted: the tap waits for a round that gets it
	power_on();
	world.row = 1;
	world.finger[0].on = (span_t){ T0 + 200, T0 + 300 };
	world.finger[0].x0 = world.finger[0].x1 = 240;
	world.finger[0].y0 = world.finger[0].y1 = 150;
	world.frame = (span_t){ T0 + 350, T0 + 450 };
	run(700);
	CHECK(round_of(T0 + 360)->refused && round_of(T0 + 440)->refused && sum.refused == 5);
	CHECK(round_of(T0 + 460)->lookups == 1 && round_of(T0 + 480)->taps == 1 && round_of(T0 + 480)->tap_row == 1);
	CHECK(sum.taps == 1 && sum.lookups == 1 && sum.least_gap_ms == 20 && sum.most_gap_ms == 20);

	// A new scene in the round of the lookup: the row is asked for first, it is one of the scene the finger
	// saw (ui_row_at() looks). The knob goes to and fro meanwhile: another page in every round.
	power_on();
	world.finger[0].on = (span_t){ T0 + 200, T0 + 300 };
	world.finger[0].x0 = world.finger[0].x1 = 240;
	world.finger[0].y0 = world.finger[0].y1 = 150;
	for(unsigned i = 0; i < 8; i++)
	{
		world.turn[i].at_ms = T0 + 300 + 20 * i;
		world.turn[i].counts = i % 2 == 0 ? KNOB_COUNTS_PER_DETENT : -KNOB_COUNTS_PER_DETENT;
	}
	run(600);
	CHECK(round_of(T0 + 360)->lookups == 1 && round_of(T0 + 360)->shows == 1 && sum.taps == 1 && sum.lookups == 1);

	// Readings of the touch controller that fail while the finger is down say nothing: it is one tap, not
	// one before them and one behind
	power_on();
	world.finger[0].on = (span_t){ T0 + 200, T0 + 500 };
	world.finger[0].x0 = world.finger[0].x1 = 240;
	world.finger[0].y0 = world.finger[0].y1 = 150;
	world.touch_fails = (span_t){ T0 + 280, T0 + 400 };
	run(900);
	CHECK(round_of(T0 + 560)->lookups == 1 && sum.taps == 1 && sum.lookups == 1);
}

// The four swipes as app.h wants them: dx below 0 for a finger that went left, dy below 0 for one that
// went up. Each is told once, in the round whose reading ended it.
static void test_swipe(void)
{
	static const struct
	{
		int x0, y0, x1, y1;
		int dx, dy;
	} way[FINGERS] =
	{
		{ 300, 240, 100, 240, -1, 0 },
		{ 100, 240, 300, 240, 1, 0 },
		{ 240, 300, 240, 100, 0, -1 },
		{ 240, 100, 240, 300, 0, 1 },
	};

	power_on();
	for(unsigned i = 0; i < FINGERS; i++)
	{
		world.finger[i].on = (span_t){ T0 + 200 + 400 * i, T0 + 400 + 400 * i };
		world.finger[i].x0 = way[i].x0;
		world.finger[i].y0 = way[i].y0;
		world.finger[i].x1 = way[i].x1;
		world.finger[i].y1 = way[i].y1;
	}
	run(2000);
	for(unsigned i = 0; i < FINGERS; i++)
	{
		const round_t *round = round_of(T0 + 440 + 400 * i);

		CHECK(round->swipes == 1 && round->dx == way[i].dx && round->dy == way[i].dy);
	}
	CHECK(sum.swipes == 4 && sum.taps == 0 && sum.lookups == 0);
}

// What the knob asks the platform to store is taken in the round in which it was asked for: the night mode,
// switched on in the menu
static void test_events(void)
{
	power_on();
	world.press[0] = (span_t){ T0 + 100, T0 + 200 };
	world.turn[0].at_ms = T0 + 400;
	world.turn[0].counts = 2 * KNOB_COUNTS_PER_DETENT;
	world.press[1] = (span_t){ T0 + 600, T0 + 700 };
	run(1000);
	CHECK(platform_app->settings.night_mode && events_seen == APP_EVENT_STORE_SETTINGS && platform_app->events == 0);
}

// The backlight: dark from the round on that finds the display about to restart, and the log says so
static void test_backlight(void)
{
	power_on();
	world.restart_from_ms = T0 + 210;
	run(500);
	CHECK(round_of(T0 + 200)->lights == 0 && round_of(T0 + 220)->lights == 1 && light == 0 && lights == 2);
	CHECK(sim_log_found == 2);

	// A brightness that changes while the screen stays lit is set and not logged: menu, Helligkeit, a detent
	power_on();
	world.press[0] = (span_t){ T0 + 100, T0 + 200 };
	world.turn[0].at_ms = T0 + 400;
	world.turn[0].counts = KNOB_COUNTS_PER_DETENT;
	world.press[1] = (span_t){ T0 + 600, T0 + 700 };
	world.turn[1].at_ms = T0 + 1000;
	world.turn[1].counts = KNOB_COUNTS_PER_DETENT;
	run(1400);
	CHECK(shown.kind == SCENE_LEVEL && round_of(T0 + 1000)->lights == 1 && lights == 2 && light > round_of(T0)->wanted);
	CHECK(sim_log_found == 1);
}

// A task the watchdog does not take says so in the log
static void test_watchdog(void)
{
	power_on();
	world.watchdog = ESP_ERR_NO_MEM;
	sim_log_wanted = "is not watched";
	run(100);
	CHECK(sim_log_found == 2 && !task[TASK_DRAWING].subscribed && !task[TASK_SCREEN].subscribed);
}

int main(void)
{
	sim_state = state;

	test_start_fails();
	test_start();
	test_rhythm();
	test_late();
	test_drawing_sleeps();
	test_press();
	test_turn();
	test_held_up();
	test_lock_of_the_app();
	test_frame();
	test_tap();
	test_swipe();
	test_events();
	test_backlight();
	test_watchdog();

	heap_reset();
	return sim_end();
}
