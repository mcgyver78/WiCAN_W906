/*
 * Host test for display/components/core/nav.c. Run "make test_nav && ./test_nav" in display/test.
 * redproof.py removes or weakens every rule once (mutations/nav.py) and expects this test to fail.
 *
 * Every group of checks runs in a child process: a module that crashes or never returns is then a failed
 * check and not the end of the test.
 *
 * The scenes of the examples all use the same world: the layout `views` with five pages - 0 Motor,
 * 1 Service (hidden), 2 DPF, 3 Anhänger (its value is in no catalogue), 4 Lader - and the catalogue
 * `cat_all`, with which the pages 0, 2 and 4 are shown. The display starts at 1000 ms on page 0, and every
 * call comes 100 ms after the one before unless a time is named. The screens are reached over page 2.
 */
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "test.h"
#include "nav.h"

#define PHASES      9                           // members of dtc_flow_phase_t
#define SCREENS     14                          // members of nav_screen_t
#define ACTIONS     19                          // members of nav_do_t
#define NO_PHASE    ((dtc_flow_phase_t)100)     // no member of the enum
#define NO_EVENT    ((hold_event_t)77)          // no member of the enum
#define LINES_MOST  (INT_MAX - 3)               // the most lines a list is taken to have
#define IDLE        120000                      // NAV_IDLE_MS, written down once more

static layout_t views;          // five pages, see above
static layout_t views_small;    // two pages: 0 Ruß (SOOT), 1 Drehzahl (RPM)
static layout_t views_full;     // twelve pages, 3 and 8 hidden; page i shows the value K<i>, page 6 also RPM
static layout_t views_none;     // no page
static layout_t views_hidden;   // three pages, all hidden

static catalog_t cat_new;       // as catalog_init() leaves it: not loaded yet
static catalog_t cat_engine;    // RPM, COOLANT, BOOST
static catalog_t cat_dpf;       // SOOT
static catalog_t cat_all;       // RPM, COOLANT, SOOT, BOOST
static catalog_t cat_other;     // SPEED: loaded, but none of the values the views name
static catalog_t cat_k;         // K0, K3, K5, K8, K11
static catalog_t cat_first;     // K0
static catalog_t cat_last;      // K11

#define LAYOUTS     5
#define CATALOGS    8

static const layout_t *const layouts[LAYOUTS] = {&views, &views_small, &views_full, &views_none, &views_hidden};
static const catalog_t *const catalogs[CATALOGS] = {&cat_new, &cat_engine, &cat_dpf, &cat_all, &cat_other, &cat_k, &cat_first, &cat_last};

// The pages in the rotation of the knob for every pair of the two lists above, written down by hand from the
// rule of layout.h: not hidden, and a value in the catalogue or the catalogue not loaded. Bit i is page i.
static const unsigned shown_pages[LAYOUTS][CATALOGS] =
{
	//             new    engine dpf    all    other  k      first  last
	/* views  */ {0x01D, 0x011, 0x004, 0x015, 0x000, 0x000, 0x000, 0x000},
	/* small  */ {0x003, 0x002, 0x001, 0x003, 0x000, 0x000, 0x000, 0x000},
	/* full   */ {0xEF7, 0x040, 0x000, 0x040, 0x000, 0x821, 0x001, 0x800},
	/* none   */ {0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000},
	/* hidden */ {0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000},
};

static const dtc_flow_phase_t phases[PHASES + 1] =
{
	DTC_FLOW_IDLE, DTC_FLOW_READ_SENT, DTC_FLOW_READING, DTC_FLOW_LIST, DTC_FLOW_CLEAR_SENT, DTC_FLOW_CLEARING,
	DTC_FLOW_CLEARED, DTC_FLOW_FAILED, DTC_FLOW_UNKNOWN, NO_PHASE,
};

static const char *const phase_names[PHASES + 1] =
{
	"idle", "read sent", "reading", "list", "clear sent", "clearing", "cleared", "failed", "unknown", "no phase",
};

static const char *const screen_names[SCREENS] =
{
	"pages", "menu", "dtc", "busy", "list", "clear dialog", "cleared", "failed", "old", "brightness", "web", "info",
	"settings", "confirm",
};

static nav_t nav;
static nav_world_t world;
static uint64_t now;

static void add_page(layout_t *layout, const char *title, bool hidden, const char *key, const char *second)
{
	layout_page_t *page = &layout->pages[layout->page_count++];

	strcpy(page->title, title);
	page->hidden = hidden;
	strcpy(page->items[0].key, key);
	page->items[0].scale = 1;
	page->item_count = 1;
	if(second != NULL)
	{
		strcpy(page->items[1].key, second);
		page->items[1].scale = 1;
		page->item_count = 2;
	}
}

static void add_entry(catalog_t *catalog, const char *name)
{
	catalog_entry_t *entry = &catalog->entries[catalog->count++];

	memset(entry, 0, sizeof(*entry));
	strcpy(entry->name, name);
	entry->in_profile = true;
}

static void make_views(void)
{
	int i;

	add_page(&views, "Motor", false, "RPM", "COOLANT");
	add_page(&views, "Service", true, "RPM", NULL);
	add_page(&views, "DPF", false, "SOOT", NULL);
	add_page(&views, "Anhänger", false, "TRAILER", NULL);
	add_page(&views, "Lader", false, "BOOST", "RPM");

	add_page(&views_small, "Ruß", false, "SOOT", NULL);
	add_page(&views_small, "Drehzahl", false, "RPM", NULL);

	for(i = 0; i < LAYOUT_PAGES_MAX; i++)
	{
		char key[8];

		snprintf(key, sizeof(key), "K%d", i);
		add_page(&views_full, key, i == 3 || i == 8, key, i == 6 ? "RPM" : NULL);
	}

	add_page(&views_hidden, "Eins", true, "RPM", NULL);
	add_page(&views_hidden, "Zwei", true, "SOOT", NULL);
	add_page(&views_hidden, "Drei", true, "K0", NULL);

	catalog_init(&cat_new);
	catalog_init(&cat_engine);
	add_entry(&cat_engine, "RPM");
	add_entry(&cat_engine, "COOLANT");
	add_entry(&cat_engine, "BOOST");
	catalog_init(&cat_dpf);
	add_entry(&cat_dpf, "SOOT");
	catalog_init(&cat_all);
	add_entry(&cat_all, "RPM");
	add_entry(&cat_all, "COOLANT");
	add_entry(&cat_all, "SOOT");
	add_entry(&cat_all, "BOOST");
	catalog_init(&cat_other);
	add_entry(&cat_other, "SPEED");
	catalog_init(&cat_k);
	add_entry(&cat_k, "K0");
	add_entry(&cat_k, "K3");
	add_entry(&cat_k, "K5");
	add_entry(&cat_k, "K8");
	add_entry(&cat_k, "K11");
	catalog_init(&cat_first);
	add_entry(&cat_first, "K0");
	catalog_init(&cat_last);
	add_entry(&cat_last, "K11");
}

// Nothing going on: no request, nothing read, nothing allowed, nothing lying over the screen
static void quiet_world(void)
{
	memset(&world, 0, sizeof(world));
	world.layout = &views;
	world.catalog = &cat_all;
	world.flow = DTC_FLOW_IDLE;
	world.asking = ACCESS_ASK_NONE;
	world.brightness = 80;
}

static void start_with(const layout_t *layout, const catalog_t *catalog)
{
	quiet_world();
	world.layout = layout;
	world.catalog = catalog;
	now = 1000;
	nav_init(&nav, &world, now);
}

static void start(void)
{
	start_with(&views, &cat_all);
}

static nav_do_t turn(int detents)
{
	return nav_turn(&nav, detents, &world, now += 100);
}

static nav_do_t press(void)
{
	return nav_short(&nav, &world, now += 100);
}

static nav_do_t long_press(void)
{
	return nav_long(&nav, &world, now += 100);
}

static nav_do_t tap(int row)
{
	return nav_tap(&nav, row, &world, now += 100);
}

static nav_do_t swipe(int direction)
{
	return nav_swipe(&nav, direction, &world, now += 100);
}

static nav_do_t held(hold_event_t event)
{
	return nav_hold(&nav, event, &world, now += 100);
}

static nav_do_t cancel(void)
{
	return nav_cancel(&nav, &world, now += 100);
}

static nav_do_t tick(void)
{
	return nav_tick(&nav, &world, now += 100);
}

// Whether a short press on that row of the screen shown would do something
static bool acts(int row)
{
	return nav_row_acts(&nav, row, &world);
}

static nav_do_t tick_at(uint64_t at_ms)
{
	now = at_ms;
	return nav_tick(&nav, &world, now);
}

static bool at(nav_screen_t screen, int row)
{
	return nav.screen == screen && nav.row == row;
}

static bool page_is(int page)
{
	return nav.screen == NAV_PAGES && nav.page == page && nav.row == 0;
}

// Nothing of what the display shows changed
static bool stays(const nav_t *before)
{
	return nav.screen == before->screen && nav.page == before->page && nav.row == before->row && nav.value == before->value &&
	       nav.confirm == before->confirm;
}

// The scenes: every screen reached by the inputs the header names, over page 2
static void open_menu(int row)
{
	start();
	turn(1);
	press();
	turn(row);
}

static void open_dtc(int row)
{
	open_menu(0);
	press();
	turn(row);
}

// A read that was asked for here and is under way
static void open_busy(void)
{
	open_dtc(0);
	world.can_read = true;
	press();
	world.can_read = false;
	world.flow = DTC_FLOW_READ_SENT;
}

static void open_list(int lines, int row)
{
	open_dtc(1);
	world.flow = DTC_FLOW_LIST;
	world.list_lines = lines;
	press();
	turn(row);
}

// The clear dialog over a list of `lines` lines, the focus on Abbrechen
static void open_clear_dialog(int lines)
{
	open_list(lines, lines + 1);
	world.can_clear = true;
	press();
}

static void open_cleared(int lines, int row)
{
	open_dtc(1);
	world.flow = DTC_FLOW_CLEARED;
	world.cleared_lines = lines;
	press();
	turn(row);
}

static void open_failed(dtc_flow_phase_t flow)
{
	open_dtc(1);
	world.flow = flow;
	press();
}

static void open_old(int lines, int row)
{
	open_dtc(2);
	world.old_lines = lines;
	press();
	turn(row);
}

static void open_brightness(int brightness)
{
	open_menu(1);
	world.brightness = brightness;
	press();
}

static void open_web(int row)
{
	open_menu(3);
	press();
	turn(row);
}

static void open_info(int lines, int row)
{
	open_menu(4);
	world.info_lines = lines;
	press();
	turn(row);
}

static void open_settings(int row)
{
	open_menu(5);
	press();
	turn(row);
}

// The dialog of the settings, opened with row 2 Neustart, 3 Vorherige Version or 4 Werkseinstellungen
static void open_ask(int row)
{
	open_settings(row);
	world.previous_firmware = true;
	press();
}

// A screen with a world in which it stays. last: the focus on its last row instead of one before it.
static void reach(nav_screen_t screen, bool last)
{
	switch(screen)
	{
		case NAV_PAGES:
			start();
			turn(1);
			break;
		case NAV_MENU:          open_menu(last ? 6 : 2); break;
		case NAV_DTC:           open_dtc(last ? 3 : 1); break;
		case NAV_DTC_BUSY:      open_busy(); break;
		case NAV_DTC_LIST:      open_list(3, last ? 5 : 1); break;
		case NAV_DTC_CONFIRM:
			open_clear_dialog(3);
			turn(last ? 1 : 0);
			break;
		case NAV_DTC_CLEARED:   open_cleared(2, last ? 2 : 1); break;
		case NAV_DTC_FAILED:    open_failed(DTC_FLOW_FAILED); break;
		case NAV_DTC_OLD:       open_old(2, last ? 2 : 1); break;
		case NAV_BRIGHTNESS:    open_brightness(60); break;
		case NAV_WEB:           open_web(last ? 1 : 0); break;
		case NAV_INFO:          open_info(4, last ? 3 : 1); break;
		case NAV_SETTINGS:      open_settings(last ? 5 : 3); break;
		case NAV_CONFIRM:
			open_ask(4);
			turn(last ? 1 : 0);
			break;
	}
}

static void set_overlay(nav_overlay_t overlay)
{
	world.uploading = overlay == NAV_OVER_UPLOAD;
	world.asking = overlay == NAV_OVER_ASK ? ACCESS_ASK_FIRMWARE : ACCESS_ASK_NONE;
	world.update_pending = overlay == NAV_OVER_UPDATE;
}

static void test_fixtures(void)
{
	// The focus reach() leaves, by screen
	static const int rows[2][SCREENS] =
	{
		{0, 2, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 3, 0},
		{0, 6, 3, 0, 5, 1, 2, 0, 2, 0, 1, 3, 5, 1},
	};
	int wrong = 0;

	check(NAV_IDLE_MS == 120000 && NAV_BRIGHTNESS_STEP == 5, "two minutes of idle time, 5 percent of brightness per detent");
	check(NAV_MENU_ROWS == 7 && NAV_DTC_ROWS == 4 && NAV_WEB_ROWS == 2 && NAV_SETTINGS_ROWS == 6,
	      "7 rows in the menu, 4 in the fault memory, 2 for the web access, 6 in the settings");
	check(SETTINGS_BRIGHTNESS_MIN == 5 && SETTINGS_BRIGHTNESS_MAX == 100 && LAYOUT_PAGES_MAX == 12,
	      "the limits this test is written for: brightness 5 to 100, up to 12 pages");

	for(int layout = 0; layout < LAYOUTS; layout++)
	{
		for(int catalog = 0; catalog < CATALOGS; catalog++)
		{
			for(int page = -1; page <= LAYOUT_PAGES_MAX; page++)
			{
				bool expected = page >= 0 && page < LAYOUT_PAGES_MAX && (shown_pages[layout][catalog] >> page & 1) != 0;

				if(layout_page_shown(layouts[layout], page, catalogs[catalog]) != expected) wrong++;
			}
		}
	}
	check(wrong == 0, "the views and catalogues of this test show the pages written down by hand");

	wrong = 0;
	for(int screen = 0; screen < SCREENS; screen++)
	{
		for(int last = 0; last < 2; last++)
		{
			reach((nav_screen_t)screen, last != 0);
			if(!at((nav_screen_t)screen, rows[last][screen]) || nav.page != 2 || nav_overlay(&world) != NAV_OVER_NONE)
			{
				printf("  %s: screen %d row %d page %d\n", screen_names[screen], (int)nav.screen, nav.row, nav.page);
				wrong++;
			}
		}
	}
	check(wrong == 0, "the scenes of this test: every screen is reached by the inputs the header names, with page 2 below it");
}

static void test_init(void)
{
	quiet_world();
	memset(&nav, 0xA5, sizeof(nav));
	nav_init(&nav, &world, 5000);
	check(nav.screen == NAV_PAGES && nav.page == 0, "after the start: the value pages on the first page, whatever stood in the memory");
	check(nav.row == 0 && nav.value == 0 && nav.confirm == NAV_DO_NOTHING, "after the start: row and value 0, nothing waits to be carried out");
	check(nav.clock_ms == 5000 && nav.last_input_ms == 5000, "after the start: its time is the latest time seen and the time of the last input");
	check(nav_rows(&nav, &world) == 0, "the value pages have no rows");

	start_with(&views, &cat_dpf);
	check(page_is(2), "a start with the pages 0 and 1 not shown: on page 2, the first that is");
	start_with(&views_full, &cat_last);
	check(page_is(11), "a start with only the last of twelve pages shown: on it");
	start_with(&views, &cat_other);
	check(page_is(-1), "a start with a catalogue that holds none of the values: no page, -1");
	start_with(&views_none, &cat_all);
	check(page_is(-1), "a start with a layout without pages: no page, -1");
	start_with(&views_hidden, &cat_new);
	check(page_is(-1), "a start with a layout whose pages are all hidden: no page, -1");

	start();
	turn(2);
	nav_init(&nav, &world, now);
	check(page_is(0), "a start while the last page is shown: the first page, not the one that stood in the memory");

	// A start in the middle of something
	reach(NAV_CONFIRM, true);
	world.catalog = &cat_dpf;
	nav_init(&nav, &world, 400);
	check(page_is(2) && nav.confirm == NAV_DO_NOTHING && nav.clock_ms == 400 && nav.last_input_ms == 400,
	      "a start out of a dialog and with an earlier time: the value pages, nothing to carry out, the time of the start");
}

static void test_overlay(void)
{
	quiet_world();
	check(nav_overlay(&world) == NAV_OVER_NONE, "no upload, no question, no update: nothing lies over the screen");
	world.uploading = true;
	check(nav_overlay(&world) == NAV_OVER_UPLOAD, "a firmware upload runs: the upload lies over the screen");
	quiet_world();
	world.asking = ACCESS_ASK_WIFI;
	check(nav_overlay(&world) == NAV_OVER_ASK, "the browser asks to store WiFi data: the question lies over the screen");
	world.asking = ACCESS_ASK_FIRMWARE;
	check(nav_overlay(&world) == NAV_OVER_ASK, "the browser asks to start the uploaded firmware: the question lies over the screen");
	world.asking = ACCESS_ASK_RESET;
	check(nav_overlay(&world) == NAV_OVER_ASK, "the browser asks for the factory reset: the question lies over the screen");
	world.asking = (access_ask_t)4;
	check(nav_overlay(&world) == NAV_OVER_ASK, "a question that is no member of the enum is a question");
	quiet_world();
	world.update_pending = true;
	check(nav_overlay(&world) == NAV_OVER_UPDATE, "the firmware waits for its confirmation: the update question lies over the screen");

	world.asking = ACCESS_ASK_WIFI;
	check(nav_overlay(&world) == NAV_OVER_ASK, "a question of the browser and the update question: the one of the browser goes first");
	world.uploading = true;
	check(nav_overlay(&world) == NAV_OVER_UPLOAD, "upload, question and update question at once: the upload goes first");
	world.asking = ACCESS_ASK_NONE;
	check(nav_overlay(&world) == NAV_OVER_UPLOAD, "upload and update question: the upload goes first");
	world.update_pending = false;
	world.asking = ACCESS_ASK_RESET;
	check(nav_overlay(&world) == NAV_OVER_UPLOAD, "upload and a question of the browser: the upload goes first");

	// Nothing else of the world lies over the screen
	quiet_world();
	world.flow = DTC_FLOW_CLEARING;
	world.can_read = world.can_clear = world.release_open = world.previous_firmware = world.night_mode = true;
	world.list_lines = world.cleared_lines = world.old_lines = world.info_lines = 3;
	check(nav_overlay(&world) == NAV_OVER_NONE, "a request under way, lists, an open release, night mode: nothing lies over the screen");
}

static void test_rows(void)
{
	start();
	press();
	check(nav_rows(&nav, &world) == 7, "the menu has 7 rows");
	press();
	check(nav_rows(&nav, &world) == 4, "the fault memory has 4 rows");
	open_busy();
	world.list_lines = world.cleared_lines = world.old_lines = world.info_lines = 9;
	check(nav_rows(&nav, &world) == 0, "the progress has no rows, whatever lists there are");

	open_list(5, 0);
	check(nav_rows(&nav, &world) == 8, "a list of 5 lines has 8 rows");
	world.list_lines = 0;
	check(nav_rows(&nav, &world) == 3, "a list without lines has the 3 rows Erneut lesen, Fehler löschen, Zurück");
	world.list_lines = 220;
	world.cleared_lines = world.old_lines = world.info_lines = 1;
	check(nav_rows(&nav, &world) == 223, "the rows of the list follow the lines of the world and of no other list");
	world.list_lines = -1;
	check(nav_rows(&nav, &world) == 3, "a list of -1 lines counts as one without lines");
	world.list_lines = INT_MIN;
	check(nav_rows(&nav, &world) == 3, "a list of the smallest number of lines counts as one without lines");
	world.list_lines = INT_MAX - 4;
	check(nav_rows(&nav, &world) == INT_MAX - 1, "a list of INT_MAX - 4 lines has three rows more");
	world.list_lines = INT_MAX - 3;
	check(nav_rows(&nav, &world) == INT_MAX, "a list of INT_MAX - 3 lines has INT_MAX rows");
	world.list_lines = INT_MAX - 2;
	check(nav_rows(&nav, &world) == INT_MAX, "a list of INT_MAX - 2 lines counts as one of INT_MAX - 3");
	world.list_lines = INT_MAX;
	check(nav_rows(&nav, &world) == INT_MAX, "a list of INT_MAX lines counts as one of INT_MAX - 3");

	open_clear_dialog(5);
	check(nav_rows(&nav, &world) == 2, "the clear dialog has 2 rows");

	open_cleared(4, 0);
	check(nav_rows(&nav, &world) == 5, "an outcome of 4 lines has 5 rows");
	world.cleared_lines = 0;
	world.list_lines = world.old_lines = world.info_lines = 7;
	check(nav_rows(&nav, &world) == 1, "an outcome without lines has the row Fertig, whatever other lists there are");
	world.cleared_lines = -7;
	check(nav_rows(&nav, &world) == 1, "an outcome of -7 lines counts as one without lines");
	world.cleared_lines = INT_MAX - 4;
	check(nav_rows(&nav, &world) == INT_MAX - 3, "an outcome of INT_MAX - 4 lines has one row more");
	world.cleared_lines = INT_MAX - 3;
	check(nav_rows(&nav, &world) == INT_MAX - 2, "an outcome of INT_MAX - 3 lines has INT_MAX - 2 rows");
	world.cleared_lines = INT_MAX;
	check(nav_rows(&nav, &world) == INT_MAX - 2, "an outcome of INT_MAX lines counts as one of INT_MAX - 3");

	open_failed(DTC_FLOW_FAILED);
	world.list_lines = world.cleared_lines = world.old_lines = world.info_lines = 9;
	check(nav_rows(&nav, &world) == 0, "the failure has no rows");

	open_old(2, 0);
	check(nav_rows(&nav, &world) == 3, "an old list of 2 lines has 3 rows");
	world.old_lines = 6;
	world.list_lines = world.cleared_lines = world.info_lines = 1;
	check(nav_rows(&nav, &world) == 7, "the rows of the old list follow its lines and those of no other list");
	world.old_lines = -3;
	check(nav_rows(&nav, &world) == 1, "an old list of -3 lines counts as one without lines: the row Zurück");
	world.old_lines = INT_MAX - 4;
	check(nav_rows(&nav, &world) == INT_MAX - 3, "an old list of INT_MAX - 4 lines has one row more");
	world.old_lines = INT_MAX;
	check(nav_rows(&nav, &world) == INT_MAX - 2, "an old list of INT_MAX lines counts as one of INT_MAX - 3");

	open_brightness(60);
	world.info_lines = 4;
	check(nav_rows(&nav, &world) == 0, "the brightness has no rows");
	open_web(0);
	check(nav_rows(&nav, &world) == 2, "the web access has 2 rows");

	open_info(9, 0);
	check(nav_rows(&nav, &world) == 9, "an info of 9 lines has 9 rows");
	world.info_lines = 0;
	world.list_lines = world.cleared_lines = world.old_lines = 5;
	check(nav_rows(&nav, &world) == 0, "an info without lines has no rows, whatever other lists there are");
	world.info_lines = -1;
	check(nav_rows(&nav, &world) == 0, "an info of -1 lines has no rows");
	world.info_lines = INT_MAX - 4;
	check(nav_rows(&nav, &world) == INT_MAX - 4, "an info of INT_MAX - 4 lines has as many rows");
	world.info_lines = INT_MAX - 3;
	check(nav_rows(&nav, &world) == INT_MAX - 3, "an info of INT_MAX - 3 lines has as many rows");
	world.info_lines = INT_MAX;
	check(nav_rows(&nav, &world) == INT_MAX - 3, "an info of INT_MAX lines counts as one of INT_MAX - 3");

	open_settings(0);
	check(nav_rows(&nav, &world) == 6, "the settings have 6 rows");
	open_ask(2);
	check(nav_rows(&nav, &world) == 2, "the dialog of the settings has 2 rows");
}

static void test_pages_turn(void)
{
	start();
	check(turn(1) == NAV_DO_NOTHING && page_is(2), "value pages, one detent: the next page, the hidden page 1 is passed over");
	check(turn(1) == NAV_DO_NOTHING && page_is(4), "value pages, one detent more: page 4, page 3 without a value in the catalogue is passed over");
	check(turn(1) == NAV_DO_NOTHING && page_is(4), "value pages, a detent on the last page: hard end, no wrap");
	check(turn(-1) == NAV_DO_NOTHING && page_is(2), "value pages, one detent back: the page before");
	check(turn(-1) == NAV_DO_NOTHING && page_is(0) && turn(-1) == NAV_DO_NOTHING && page_is(0), "value pages, a detent back on the first page: hard end, no wrap");
	check(turn(2) == NAV_DO_NOTHING && page_is(4), "value pages, two detents: two pages on");
	check(turn(-2) == NAV_DO_NOTHING && page_is(0), "value pages, two detents back: two pages back");
	check(turn(0) == NAV_DO_NOTHING && page_is(0), "value pages, a turn of no detent on the first page: it stays");
	turn(1);
	check(turn(0) == NAV_DO_NOTHING && page_is(2), "value pages, a turn of no detent on a page in the middle: it stays");
	check(turn(7) == NAV_DO_NOTHING && page_is(4), "value pages, more detents than pages: the last page");
	check(turn(-9) == NAV_DO_NOTHING && page_is(0), "value pages, more detents back than pages: the first page");
	check(turn(INT_MAX) == NAV_DO_NOTHING && page_is(4), "value pages, the largest number of detents: the last page");
	check(turn(INT_MIN) == NAV_DO_NOTHING && page_is(0), "value pages, the largest number of detents back: the first page");
	check(nav.value == 0 && nav.confirm == NAV_DO_NOTHING, "turning over the value pages changes neither value nor what waits to be carried out");

	start_with(&views, &cat_new);
	check(turn(1) == NAV_DO_NOTHING && page_is(2) && turn(1) == NAV_DO_NOTHING && page_is(3) && turn(1) == NAV_DO_NOTHING && page_is(4),
	      "catalogue not loaded yet: every page that is not hidden is in the rotation");
	check(turn(-2) == NAV_DO_NOTHING && page_is(2), "catalogue not loaded yet: two detents back over page 3");

	start_with(&views_full, &cat_k);
	check(page_is(0) && turn(1) == NAV_DO_NOTHING && page_is(5) && turn(1) == NAV_DO_NOTHING && page_is(11) && turn(1) == NAV_DO_NOTHING && page_is(11),
	      "twelve pages of which 0, 5 and 11 are shown: a detent each, hard end at the twelfth");
	check(turn(-2) == NAV_DO_NOTHING && page_is(0), "twelve pages of which three are shown: two detents back to the first");
	start_with(&views_full, &cat_new);
	check(turn(11) == NAV_DO_NOTHING && page_is(11), "twelve pages, two of them hidden: 11 detents end on the last page");
	check(turn(-9) == NAV_DO_NOTHING && page_is(0), "twelve pages, two of them hidden: 9 detents back are the first page");
	check(turn(9) == NAV_DO_NOTHING && page_is(11) && turn(-8) == NAV_DO_NOTHING && page_is(1), "twelve pages, two of them hidden: 8 detents back from the last are page 1");

	// No page to show
	start_with(&views, &cat_other);
	check(turn(1) == NAV_DO_NOTHING && page_is(-1) && turn(-1) == NAV_DO_NOTHING && page_is(-1) && turn(0) == NAV_DO_NOTHING && page_is(-1),
	      "no page shown at all: turning finds none, the page stays -1");
	world.catalog = &cat_all;
	check(turn(1) == NAV_DO_NOTHING && page_is(0), "no page, then the catalogue arrives: a detent goes to the first page");
	start_with(&views, &cat_other);
	world.catalog = &cat_all;
	check(turn(-1) == NAV_DO_NOTHING && page_is(0), "no page, then the catalogue arrives: a detent back goes to the first page as well");
	start_with(&views, &cat_other);
	world.catalog = &cat_all;
	check(turn(3) == NAV_DO_NOTHING && page_is(0), "no page, then the catalogue arrives: three detents go to the first page and no further");
	start_with(&views, &cat_other);
	world.catalog = &cat_all;
	check(turn(0) == NAV_DO_NOTHING && page_is(0), "no page, then the catalogue arrives: a turn of no detent finds the first page");
	start_with(&views, &cat_other);
	world.catalog = &cat_dpf;
	check(turn(1) == NAV_DO_NOTHING && page_is(2), "no page, then a catalogue for page 2 alone: the nearest page to none is the first that is shown");
	start_with(&views_full, &cat_other);
	world.catalog = &cat_last;
	check(turn(1) == NAV_DO_NOTHING && page_is(11), "no page, then a catalogue for the last of twelve pages: a detent goes there");
	start_with(&views_full, &cat_last);
	world.catalog = &cat_first;
	check(turn(1) == NAV_DO_NOTHING && page_is(0), "on the last of twelve pages, then only the first is shown: a detent forwards goes there");

	// The page shown is not shown any more
	start();
	turn(1);
	world.catalog = &cat_engine;
	check(turn(-1) == NAV_DO_NOTHING && page_is(4), "page 2 lost its values, 0 and 4 are equally near: a detent back goes to page 4, the one behind it");
	start();
	turn(1);
	world.catalog = &cat_engine;
	check(turn(1) == NAV_DO_NOTHING && page_is(4), "page 2 lost its values, 0 and 4 are equally near: a detent forwards goes to page 4");
	start();
	turn(1);
	world.catalog = &cat_engine;
	check(turn(-5) == NAV_DO_NOTHING && page_is(4), "page 2 lost its values: five detents back go to the nearest page and no further");
	check(turn(-1) == NAV_DO_NOTHING && page_is(0), "once a page is found again the detents count as before");

	start_with(&views_full, &cat_new);
	turn(2);
	world.catalog = &cat_k;
	check(turn(1) == NAV_DO_NOTHING && page_is(0), "page 2 not shown any more, page 0 two away and page 5 three: a detent forwards goes to page 0, the nearer one");
	start_with(&views_full, &cat_new);
	turn(7);
	world.catalog = &cat_k;
	check(turn(-1) == NAV_DO_NOTHING && page_is(11), "page 9 not shown any more, page 11 two away and page 5 four: a detent back goes to page 11, the nearer one");
	start_with(&views_full, &cat_new);
	turn(3);
	world.catalog = &cat_k;
	check(turn(-1) == NAV_DO_NOTHING && page_is(5), "page 4 not shown any more, page 5 next to it: a detent back goes to page 5");
	start_with(&views_full, &cat_new);
	turn(5);
	world.catalog = &cat_k;
	check(turn(1) == NAV_DO_NOTHING && page_is(5), "page 6 not shown any more, page 5 next to it: a detent forwards goes to page 5");

	// The layout replaced while a page is shown
	start();
	turn(2);
	world.layout = &views_small;
	check(turn(1) == NAV_DO_NOTHING && page_is(1), "a layout of two pages stored while page 4 is shown: a detent goes to its last page");
	start();
	turn(2);
	world.layout = &views_small;
	world.catalog = &cat_dpf;
	check(turn(-1) == NAV_DO_NOTHING && page_is(0), "a layout stored that shows page 0 alone while page 4 is shown: a detent goes there");
	start();
	turn(2);
	world.layout = &views_none;
	check(turn(1) == NAV_DO_NOTHING && page_is(-1), "a layout without pages stored while a page is shown: turning leaves no page, -1");
	start();
	turn(1);
	world.layout = &views_hidden;
	check(turn(-1) == NAV_DO_NOTHING && page_is(-1), "a layout whose pages are all hidden stored while a page is shown: no page, -1");
	start();
	turn(1);
	world.layout = &views_full;
	world.catalog = &cat_new;
	check(turn(1) == NAV_DO_NOTHING && page_is(4), "a layout stored that shows the page of the same number: the detent counts from it, over the hidden page 3");
}

static void test_pages_inputs(void)
{
	static const hold_event_t events[] = {HOLD_WAITING, HOLD_PROGRESS, HOLD_CONFIRMED, HOLD_CANCELLED, HOLD_STUCK, NO_EVENT};
	nav_t before;
	int wrong = 0;

	start();
	turn(1);
	check(press() == NAV_DO_NOTHING && at(NAV_MENU, 0) && nav.page == 2, "value pages, short press: the menu with the focus on Fehlerspeicher, the page is kept");
	start_with(&views, &cat_other);
	check(press() == NAV_DO_NOTHING && at(NAV_MENU, 0) && nav.page == -1, "no page shown, short press: the menu all the same");

	start();
	turn(2);
	check(long_press() == NAV_DO_NOTHING && page_is(0), "value pages, long press on the last page: back to the first page");
	check(long_press() == NAV_DO_NOTHING && page_is(0), "value pages, long press on the first page: it stays");
	start_with(&views, &cat_dpf);
	world.catalog = &cat_all;
	check(long_press() == NAV_DO_NOTHING && page_is(0), "value pages, long press after the catalogue grew: the first page the layout shows now");
	start();
	world.catalog = &cat_dpf;
	check(long_press() == NAV_DO_NOTHING && page_is(2), "value pages, long press on a first page that is not shown any more: the first that is");
	start_with(&views, &cat_other);
	check(long_press() == NAV_DO_NOTHING && page_is(-1), "no page shown, long press: no page, -1");
	start();
	turn(1);
	world.layout = &views_none;
	check(long_press() == NAV_DO_NOTHING && page_is(-1), "value pages, long press after a layout without pages was stored: no page, -1");

	start();
	turn(1);
	before = nav;
	check(tap(0) == NAV_DO_NOTHING && stays(&before) && tap(1) == NAV_DO_NOTHING && stays(&before) && tap(-1) == NAV_DO_NOTHING && stays(&before) &&
	      tap(INT_MAX) == NAV_DO_NOTHING && stays(&before) && tap(INT_MIN) == NAV_DO_NOTHING && stays(&before),
	      "value pages: a tap is ignored, whatever row it names");

	start();
	check(swipe(1) == NAV_DO_NOTHING && page_is(2), "value pages, a swipe forwards: the next page");
	check(swipe(-1) == NAV_DO_NOTHING && page_is(0), "value pages, a swipe back: the page before");
	check(swipe(-1) == NAV_DO_NOTHING && page_is(0), "value pages, a swipe back on the first page: hard end");
	check(swipe(5) == NAV_DO_NOTHING && page_is(2), "value pages, a swipe with direction 5: one page on, only the sign counts");
	check(swipe(INT_MAX) == NAV_DO_NOTHING && page_is(4) && swipe(1) == NAV_DO_NOTHING && page_is(4), "value pages, a swipe forwards on the last page: hard end");
	check(swipe(-7) == NAV_DO_NOTHING && page_is(2), "value pages, a swipe with direction -7: one page back");
	check(swipe(0) == NAV_DO_NOTHING && page_is(2), "value pages, a swipe without direction: ignored");
	check(swipe(INT_MIN) == NAV_DO_NOTHING && page_is(0), "value pages, a swipe with the smallest direction: one page back");
	start();
	turn(1);
	world.catalog = &cat_engine;
	check(swipe(0) == NAV_DO_NOTHING && page_is(2), "a swipe without direction is ignored also when the page is not shown any more");
	check(swipe(-1) == NAV_DO_NOTHING && page_is(4), "a swipe back on a page that is not shown any more: the nearest page, as with a turn");
	start_with(&views, &cat_other);
	world.catalog = &cat_all;
	check(swipe(1) == NAV_DO_NOTHING && page_is(0), "no page, then the catalogue arrives: a swipe goes to the first page");

	for(size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++)
	{
		start();
		turn(1);
		before = nav;
		if(held(events[i]) != NAV_DO_NOTHING || !stays(&before)) wrong++;
	}
	check(wrong == 0, "value pages: whatever the hold reports is nothing here");
}

static void test_menu(void)
{
	//                                                   idle     read sent     reading       list     clear sent    clearing      cleared  failed   unknown  no phase
	static const nav_screen_t expected[PHASES + 1] = {NAV_DTC, NAV_DTC_BUSY, NAV_DTC_BUSY, NAV_DTC, NAV_DTC_BUSY, NAV_DTC_BUSY, NAV_DTC, NAV_DTC, NAV_DTC, NAV_DTC};
	static const int brightness[][2] = {{80, 80}, {5, 5}, {100, 100}, {4, 5}, {101, 100}, {0, 5}, {-20, 5}, {255, 100}, {INT_MAX, 100}, {INT_MIN, 5}, {37, 37}};
	nav_t before;
	int wrong = 0;

	open_menu(0);
	check(turn(-1) == NAV_DO_NOTHING && at(NAV_MENU, 0), "menu, a detent back on the first row: hard end");
	check(turn(1) == NAV_DO_NOTHING && at(NAV_MENU, 1), "menu, one detent: the focus on the next row");
	check(turn(3) == NAV_DO_NOTHING && at(NAV_MENU, 4), "menu, three detents: the focus three rows on");
	check(turn(-2) == NAV_DO_NOTHING && at(NAV_MENU, 2), "menu, two detents back: the focus two rows back");
	check(turn(0) == NAV_DO_NOTHING && at(NAV_MENU, 2), "menu, a turn of no detent: the focus stays");
	check(turn(4) == NAV_DO_NOTHING && at(NAV_MENU, 6), "menu, four detents from row 2: the last row");
	check(turn(1) == NAV_DO_NOTHING && at(NAV_MENU, 6), "menu, a detent on the last row: hard end, no wrap");
	check(turn(-100) == NAV_DO_NOTHING && at(NAV_MENU, 0) && turn(100) == NAV_DO_NOTHING && at(NAV_MENU, 6), "menu, a hundred detents: the first and the last row");
	check(turn(INT_MAX) == NAV_DO_NOTHING && at(NAV_MENU, 6), "menu, the largest number of detents on the last row: it stays the last row");
	check(turn(INT_MIN) == NAV_DO_NOTHING && at(NAV_MENU, 0) && turn(INT_MIN) == NAV_DO_NOTHING && at(NAV_MENU, 0), "menu, the largest number of detents back: the first row");
	check(nav.page == 2, "turning in the menu leaves the page below it alone");

	for(int i = 0; i <= PHASES; i++)
	{
		open_menu(0);
		world.flow = phases[i];
		if(press() != NAV_DO_NOTHING || !at(expected[i], 0))
		{
			printf("  flow %s: screen %d row %d\n", phase_names[i], (int)nav.screen, nav.row);
			wrong++;
		}
	}
	check(wrong == 0, "menu, short press on Fehlerspeicher: the progress while the own request is under way (four phases), else the fault memory");

	wrong = 0;
	for(size_t i = 0; i < sizeof(brightness) / sizeof(brightness[0]); i++)
	{
		open_menu(1);
		world.brightness = brightness[i][0];
		if(press() != NAV_DO_NOTHING || !at(NAV_BRIGHTNESS, 0) || nav.value != brightness[i][1])
		{
			printf("  brightness %d: screen %d value %d\n", brightness[i][0], (int)nav.screen, nav.value);
			wrong++;
		}
	}
	check(wrong == 0, "menu, short press on Helligkeit: the brightness screen with the brightness in use, kept within 5 and 100");

	open_menu(2);
	before = nav;
	check(press() == NAV_DO_NIGHT_TOGGLE && stays(&before), "menu, short press on Nachtmodus: the night mode is toggled, the menu stays");
	open_menu(3);
	check(press() == NAV_DO_NOTHING && at(NAV_WEB, 0), "menu, short press on Web-Zugriff: the web access with the focus on the release");
	open_menu(4);
	world.info_lines = 3;
	check(press() == NAV_DO_NOTHING && at(NAV_INFO, 0), "menu, short press on Info: the info with the focus on its first line");
	open_menu(5);
	check(press() == NAV_DO_NOTHING && at(NAV_SETTINGS, 0), "menu, short press on Einstellungen: the settings with the focus on Drehrichtung");
	open_menu(6);
	check(press() == NAV_DO_NOTHING && page_is(2), "menu, short press on Zurück: the value pages, on the page shown before");
	open_menu(3);
	check(long_press() == NAV_DO_NOTHING && page_is(2), "menu, long press: the value pages, on the page shown before");

	open_menu(0);
	before = nav;
	before.row = 2;
	check(tap(2) == NAV_DO_NIGHT_TOGGLE && stays(&before), "menu, a tap on Nachtmodus: the focus goes there and the night mode is toggled");
	check(tap(6) == NAV_DO_NOTHING && page_is(2), "menu, a tap on Zurück: the value pages");
	open_menu(1);
	check(tap(0) == NAV_DO_NOTHING && at(NAV_DTC, 0), "menu, a tap on Fehlerspeicher with the focus elsewhere: the fault memory");
	open_menu(1);
	before = nav;
	check(tap(7) == NAV_DO_NOTHING && stays(&before) && tap(-1) == NAV_DO_NOTHING && stays(&before) && tap(INT_MAX) == NAV_DO_NOTHING && stays(&before) &&
	      tap(INT_MIN) == NAV_DO_NOTHING && stays(&before), "menu, a tap on a row that does not exist (7, -1, the largest, the smallest): ignored, the focus stays");
	check(swipe(1) == NAV_DO_NOTHING && stays(&before) && swipe(-1) == NAV_DO_NOTHING && stays(&before), "menu: a swipe is ignored, also the page below stays");
	check(held(HOLD_CONFIRMED) == NAV_DO_NOTHING && stays(&before) && held(HOLD_CANCELLED) == NAV_DO_NOTHING && stays(&before), "menu: whatever the hold reports is nothing here");
}

static void test_dtc(void)
{
	//                                                   idle     read sent  reading  list          clear sent clearing cleared          failed          unknown         no phase
	static const nav_screen_t expected[PHASES + 1] = {NAV_DTC, NAV_DTC, NAV_DTC, NAV_DTC_LIST, NAV_DTC, NAV_DTC, NAV_DTC_CLEARED, NAV_DTC_FAILED, NAV_DTC_FAILED, NAV_DTC};
	nav_t before;
	int wrong = 0;

	open_dtc(0);
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "fault memory, short press on Lesen while reading is not allowed: nothing");
	world.can_read = true;
	check(press() == NAV_DO_READ && at(NAV_DTC_BUSY, 0), "fault memory, short press on Lesen while reading is allowed: the read is asked for, the progress is shown");

	for(int i = 0; i <= PHASES; i++)
	{
		open_dtc(1);
		world.flow = phases[i];
		world.list_lines = world.cleared_lines = 2;
		// Where nothing is to be seen the focus stays on the row
		if(press() != NAV_DO_NOTHING || !at(expected[i], expected[i] == NAV_DTC ? 1 : 0))
		{
			printf("  flow %s: screen %d row %d\n", phase_names[i], (int)nav.screen, nav.row);
			wrong++;
		}
	}
	check(wrong == 0, "fault memory, short press on Liste ansehen: the list, the outcome of the clear or the failure by the phase of the flow, else nothing");

	open_dtc(2);
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "fault memory, short press on Zuletzt gelöscht without an old list: nothing");
	world.old_lines = -2;
	check(press() == NAV_DO_NOTHING && stays(&before), "fault memory, short press on Zuletzt gelöscht with -2 old lines: nothing");
	world.old_lines = 1;
	check(press() == NAV_DO_NOTHING && at(NAV_DTC_OLD, 0), "fault memory, short press on Zuletzt gelöscht with an old list of one line: the old list");

	open_dtc(3);
	check(press() == NAV_DO_NOTHING && at(NAV_MENU, 0) && nav.page == 2, "fault memory, short press on Zurück: the menu with the focus on Fehlerspeicher");
	open_dtc(2);
	check(long_press() == NAV_DO_NOTHING && at(NAV_MENU, 0), "fault memory, long press: the menu with the focus on Fehlerspeicher");

	open_dtc(0);
	check(turn(5) == NAV_DO_NOTHING && at(NAV_DTC, 3) && turn(-1) == NAV_DO_NOTHING && at(NAV_DTC, 2) && turn(-9) == NAV_DO_NOTHING && at(NAV_DTC, 0),
	      "fault memory: turning moves the focus over its four rows, hard ends");
	open_dtc(3);
	world.can_read = true;
	check(tap(0) == NAV_DO_READ && at(NAV_DTC_BUSY, 0), "fault memory, a tap on Lesen while reading is allowed: the read is asked for");
	open_dtc(0);
	before = nav;
	check(tap(4) == NAV_DO_NOTHING && stays(&before) && tap(-1) == NAV_DO_NOTHING && stays(&before), "fault memory, a tap on a row that does not exist: ignored");
	check(swipe(1) == NAV_DO_NOTHING && stays(&before), "fault memory: a swipe is ignored");
	check(tap(3) == NAV_DO_NOTHING && at(NAV_MENU, 0), "fault memory, a tap on Zurück: the menu");
}

static void test_busy(void)
{
	static const hold_event_t events[] = {HOLD_WAITING, HOLD_PROGRESS, HOLD_CONFIRMED, HOLD_CANCELLED, HOLD_STUCK, NO_EVENT};
	nav_t before;
	int wrong = 0;

	open_busy();
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "progress, short press: nothing, a scan cannot be cancelled");
	check(turn(1) == NAV_DO_NOTHING && stays(&before) && turn(-1) == NAV_DO_NOTHING && stays(&before) && turn(INT_MAX) == NAV_DO_NOTHING && stays(&before),
	      "progress, turning: nothing");
	check(tap(0) == NAV_DO_NOTHING && stays(&before) && tap(1) == NAV_DO_NOTHING && stays(&before), "progress: a tap is ignored");
	check(swipe(1) == NAV_DO_NOTHING && stays(&before) && swipe(-1) == NAV_DO_NOTHING && stays(&before), "progress: a swipe is ignored");
	for(size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++)
	{
		if(held(events[i]) != NAV_DO_NOTHING || !stays(&before)) wrong++;
	}
	check(wrong == 0, "progress: whatever the hold reports is nothing here");
	check(long_press() == NAV_DO_NOTHING && page_is(2), "progress, long press: the value pages, nothing is cancelled or dismissed");
	check(press() == NAV_DO_NOTHING && at(NAV_MENU, 0) && press() == NAV_DO_NOTHING && at(NAV_DTC_BUSY, 0),
	      "the fault memory opened again while the request is under way: the progress");
	world.can_read = true;
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "progress, short press while reading would be allowed: nothing");
}

static void test_list(void)
{
	nav_t before;

	// Three lines, then 3 Erneut lesen, 4 Fehler löschen, 5 Zurück
	open_list(3, 0);
	world.can_read = world.can_clear = true;
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "list, short press on its first line: nothing");
	turn(2);
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "list, short press on its last line: nothing");
	check(turn(-5) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 0) && turn(9) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 5) && turn(-1) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4),
	      "list: turning moves the focus over the lines and the three rows behind them, hard ends");

	open_list(3, 3);
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "list, short press on Erneut lesen while reading is not allowed: nothing");
	world.can_read = true;
	check(press() == NAV_DO_READ && at(NAV_DTC_BUSY, 0), "list, short press on Erneut lesen while reading is allowed: the read is asked for, the progress is shown");

	open_list(3, 4);
	world.can_read = true;
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "list, short press on Fehler löschen while clearing is not allowed: nothing");
	world.can_read = false;
	world.can_clear = true;
	check(press() == NAV_DO_HOLD_OPEN && at(NAV_DTC_CONFIRM, 0), "list, short press on Fehler löschen while clearing is allowed: the clear dialog opens with the focus on Abbrechen");

	open_list(3, 5);
	world.can_read = world.can_clear = true;
	check(press() == NAV_DO_NOTHING && at(NAV_DTC, 0), "list, short press on Zurück: the fault memory with the focus on Lesen, nothing is dismissed");
	open_list(3, 4);
	world.can_read = world.can_clear = true;
	check(long_press() == NAV_DO_NOTHING && at(NAV_DTC, 0), "list, long press on Fehler löschen: the fault memory, nothing is cleared or dismissed");
	check(turn(1) == NAV_DO_NOTHING && press() == NAV_DO_NOTHING && at(NAV_DTC_LIST, 0), "the list left by a long press is still there to be looked at");

	// Without lines: 0 Erneut lesen, 1 Fehler löschen, 2 Zurück
	open_list(0, 0);
	world.can_read = world.can_clear = true;
	check(press() == NAV_DO_READ && at(NAV_DTC_BUSY, 0), "a list without lines: its first row is Erneut lesen");
	open_list(-4, 1);
	world.can_read = world.can_clear = true;
	check(press() == NAV_DO_HOLD_OPEN && at(NAV_DTC_CONFIRM, 0), "a list of -4 lines: its second row is Fehler löschen");
	open_list(0, 2);
	world.can_read = world.can_clear = true;
	check(press() == NAV_DO_NOTHING && at(NAV_DTC, 0), "a list without lines: its third row is Zurück");
	open_list(1, 1);
	world.can_read = world.can_clear = true;
	check(press() == NAV_DO_READ && at(NAV_DTC_BUSY, 0), "a list of one line: its second row is Erneut lesen");

	open_list(3, 0);
	world.can_clear = true;
	check(tap(4) == NAV_DO_HOLD_OPEN && at(NAV_DTC_CONFIRM, 0), "list, a tap on Fehler löschen while clearing is allowed: the clear dialog opens");
	open_list(3, 0);
	world.can_read = world.can_clear = true;
	check(tap(2) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 2), "list, a tap on a line: the focus goes there, nothing else");
	check(tap(6) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 2) && tap(-1) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 2), "list, a tap behind the last row or before the first: ignored");
	check(swipe(1) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 2) && nav.page == 2, "list: a swipe is ignored");
	check(held(HOLD_CONFIRMED) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 2), "list: a confirmed hold without the dialog clears nothing");
	check(tap(5) == NAV_DO_NOTHING && at(NAV_DTC, 0), "list, a tap on Zurück: the fault memory");
	open_list(3, 0);
	world.can_read = true;
	check(tap(3) == NAV_DO_READ && at(NAV_DTC_BUSY, 0), "list, a tap on Erneut lesen while reading is allowed: the read is asked for");

	// The rows follow the world
	open_list(3, 4);
	world.can_read = world.can_clear = true;
	world.list_lines = 4;
	check(press() == NAV_DO_READ && at(NAV_DTC_BUSY, 0), "the list grew by a line: the row that was Fehler löschen is Erneut lesen now");
	open_list(5, 7);
	world.can_read = world.can_clear = true;
	world.list_lines = 2;
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "the list became shorter and the focus lies beyond its last row: a short press does nothing");
	check(turn(-1) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4), "the focus beyond the last row, one detent back: moved by one and kept within the rows, the last row");
	open_list(5, 7);
	world.list_lines = 2;
	check(turn(0) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4), "the focus beyond the last row, a turn of no detent: the last row");
	open_list(5, 7);
	world.list_lines = 2;
	check(turn(-4) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 3), "the focus beyond the last row, four detents back: counted from where the focus was");

	// The largest lists
	open_list(INT_MAX, 0);
	world.can_read = world.can_clear = true;
	check(turn(INT_MAX) == NAV_DO_NOTHING && at(NAV_DTC_LIST, INT_MAX - 1) && turn(INT_MAX) == NAV_DO_NOTHING && at(NAV_DTC_LIST, INT_MAX - 1),
	      "a list of INT_MAX lines, the largest number of detents twice: the focus on row INT_MAX - 1");
	check(turn(-1) == NAV_DO_NOTHING && press() == NAV_DO_HOLD_OPEN && at(NAV_DTC_CONFIRM, 0), "a list of INT_MAX lines: the row before the last is Fehler löschen");
	check(press() == NAV_DO_HOLD_CLOSE && at(NAV_DTC_LIST, INT_MAX - 2), "a list of INT_MAX lines: Abbrechen leads back to row INT_MAX - 2");
	check(turn(-1) == NAV_DO_NOTHING && press() == NAV_DO_READ && at(NAV_DTC_BUSY, 0), "a list of INT_MAX lines: row INT_MAX - 3 is Erneut lesen");
	open_list(INT_MAX, 0);
	check(turn(INT_MAX) == NAV_DO_NOTHING && press() == NAV_DO_NOTHING && at(NAV_DTC, 0), "a list of INT_MAX lines: its last row is Zurück");
	open_list(INT_MAX, 0);
	turn(INT_MAX);
	check(turn(INT_MIN) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 0), "a list of INT_MAX lines: the largest number of detents back from the last row is row 0");
	open_list(INT_MAX - 4, 0);
	world.can_read = true;
	check(turn(INT_MAX) == NAV_DO_NOTHING && at(NAV_DTC_LIST, INT_MAX - 2) && turn(-2) == NAV_DO_NOTHING && press() == NAV_DO_READ,
	      "a list of INT_MAX - 4 lines: its last row is INT_MAX - 2, Erneut lesen two before it");
}

static void test_clear_dialog(void)
{
	nav_t before;

	// A list of three lines: Fehler löschen is its row 4
	open_clear_dialog(3);
	check(turn(-1) == NAV_DO_NOTHING && at(NAV_DTC_CONFIRM, 0), "clear dialog, a detent back on Abbrechen: hard end");
	check(turn(1) == NAV_DO_NOTHING && at(NAV_DTC_CONFIRM, 1), "clear dialog, one detent: the focus on Löschen");
	check(turn(1) == NAV_DO_NOTHING && at(NAV_DTC_CONFIRM, 1), "clear dialog, a detent on Löschen: hard end");
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "clear dialog, short press on Löschen: nothing");
	check(long_press() == NAV_DO_NOTHING && stays(&before), "clear dialog, long press on Löschen: nothing, it is no way back here");
	check(tap(1) == NAV_DO_NOTHING && stays(&before), "clear dialog, a tap on Löschen: nothing, clearing cannot be confirmed by touch");
	check(tap(2) == NAV_DO_NOTHING && stays(&before) && tap(-1) == NAV_DO_NOTHING && stays(&before), "clear dialog, a tap on a row that does not exist: ignored");
	check(swipe(1) == NAV_DO_NOTHING && stays(&before) && swipe(-1) == NAV_DO_NOTHING && stays(&before), "clear dialog: a swipe is ignored");
	check(held(HOLD_WAITING) == NAV_DO_NOTHING && stays(&before), "clear dialog, the hold reports waiting: nothing");
	check(held(HOLD_PROGRESS) == NAV_DO_NOTHING && stays(&before), "clear dialog, the hold reports progress: nothing");
	check(held(NO_EVENT) == NAV_DO_NOTHING && stays(&before), "clear dialog, the hold reports no member of its enum: nothing");
	check(turn(-1) == NAV_DO_NOTHING && at(NAV_DTC_CONFIRM, 0), "clear dialog, one detent back: the focus on Abbrechen");
	before = nav;
	check(long_press() == NAV_DO_NOTHING && stays(&before), "clear dialog, long press on Abbrechen: nothing");
	check(press() == NAV_DO_HOLD_CLOSE && at(NAV_DTC_LIST, 4), "clear dialog, short press on Abbrechen: back to the list with the focus on Fehler löschen, the hold dialog is closed");

	open_clear_dialog(3);
	before = nav;
	check(tap(1) == NAV_DO_NOTHING && stays(&before) && at(NAV_DTC_CONFIRM, 0), "clear dialog, a tap on Löschen from Abbrechen: ignored, the focus stays on Abbrechen - the knob alone moves it");
	check(nav.last_input_ms == now, "clear dialog, a tap on Löschen that is ignored: an input for the idle time all the same");
	check(tap(0) == NAV_DO_HOLD_CLOSE && at(NAV_DTC_LIST, 4), "clear dialog, a tap on Abbrechen: back to the list, the hold dialog is closed");
	open_clear_dialog(3);
	turn(1);
	check(tap(0) == NAV_DO_HOLD_CLOSE && at(NAV_DTC_LIST, 4), "clear dialog, a tap on Abbrechen with the focus on Löschen: back to the list as well");
	open_clear_dialog(3);
	check(turn(7) == NAV_DO_NOTHING && at(NAV_DTC_CONFIRM, 1) && turn(INT_MIN) == NAV_DO_NOTHING && at(NAV_DTC_CONFIRM, 0) && turn(INT_MAX) == NAV_DO_NOTHING && at(NAV_DTC_CONFIRM, 1),
	      "clear dialog: any number of detents ends on one of its two rows");

	open_clear_dialog(3);
	turn(1);
	check(held(HOLD_CONFIRMED) == NAV_DO_CLEAR && at(NAV_DTC_BUSY, 0), "clear dialog, the hold is confirmed: the clear is asked for, the progress is shown");
	open_clear_dialog(3);
	turn(1);
	check(held(HOLD_CANCELLED) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4), "clear dialog, the hold is cancelled: back to the list with the focus on Fehler löschen, nothing to carry out");
	open_clear_dialog(3);
	turn(1);
	check(held(HOLD_STUCK) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4), "clear dialog, the switch hangs: back to the list with the focus on Fehler löschen, nothing to carry out");
	open_clear_dialog(3);
	check(held(HOLD_CONFIRMED) == NAV_DO_CLEAR && at(NAV_DTC_BUSY, 0), "clear dialog, the hold is confirmed: taken as hold.h reports it, hold.h watches the focus");
	open_clear_dialog(3);
	check(held(HOLD_CANCELLED) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4), "clear dialog, the hold is cancelled with the focus on Abbrechen: back to the list");

	open_clear_dialog(0);
	check(press() == NAV_DO_HOLD_CLOSE && at(NAV_DTC_LIST, 1), "the clear dialog over a list without lines: Abbrechen leads back to row 1");
	open_clear_dialog(3);
	world.list_lines = 6;
	check(held(HOLD_CANCELLED) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 7), "the list grew to 6 lines below the dialog: a cancelled hold leads back to row 7, Fehler löschen now");
	open_clear_dialog(3);
	world.list_lines = -1;
	check(tap(0) == NAV_DO_HOLD_CLOSE && at(NAV_DTC_LIST, 1), "the list counts -1 lines below the dialog: Abbrechen leads back to row 1");
	open_clear_dialog(3);
	world.can_clear = false;
	world.flow = DTC_FLOW_IDLE;
	turn(1);
	check(held(HOLD_CONFIRMED) == NAV_DO_CLEAR && at(NAV_DTC_BUSY, 0), "a confirmed hold is passed on also when the world forbids the clear by now: dtc_flow_clear() decides");
}

static void test_cleared(void)
{
	nav_t before;

	// Two lines, then 2 Fertig
	open_cleared(2, 0);
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "outcome of the clear, short press on a line: nothing");
	check(turn(1) == NAV_DO_NOTHING && at(NAV_DTC_CLEARED, 1) && press() == NAV_DO_NOTHING && at(NAV_DTC_CLEARED, 1), "outcome of the clear, short press on its last line: nothing");
	check(turn(5) == NAV_DO_NOTHING && at(NAV_DTC_CLEARED, 2), "outcome of the clear: turning ends on Fertig");
	check(press() == NAV_DO_DISMISS && at(NAV_DTC, 0), "outcome of the clear, short press on Fertig: dismissed, back to the fault memory");
	open_cleared(2, 2);
	check(long_press() == NAV_DO_NOTHING && at(NAV_DTC, 0), "outcome of the clear, long press on Fertig: back to the fault memory without a dismiss");
	check(turn(1) == NAV_DO_NOTHING && press() == NAV_DO_NOTHING && at(NAV_DTC_CLEARED, 0), "the outcome left by a long press is still there to be looked at");
	check(long_press() == NAV_DO_NOTHING && at(NAV_DTC, 0), "outcome of the clear, long press on a line: back to the fault memory");

	open_cleared(0, 0);
	check(press() == NAV_DO_DISMISS && at(NAV_DTC, 0), "an outcome without lines: its only row is Fertig");
	open_cleared(-3, 0);
	check(tap(0) == NAV_DO_DISMISS && at(NAV_DTC, 0), "an outcome of -3 lines: a tap on row 0 is Fertig");
	open_cleared(2, 0);
	before = nav;
	check(tap(3) == NAV_DO_NOTHING && stays(&before) && tap(-1) == NAV_DO_NOTHING && stays(&before), "outcome of the clear, a tap on a row that does not exist: ignored");
	check(swipe(-1) == NAV_DO_NOTHING && stays(&before), "outcome of the clear: a swipe is ignored");
	check(tap(1) == NAV_DO_NOTHING && at(NAV_DTC_CLEARED, 1), "outcome of the clear, a tap on a line: the focus goes there, nothing else");
	check(tap(2) == NAV_DO_DISMISS && at(NAV_DTC, 0), "outcome of the clear, a tap on Fertig: dismissed");

	open_cleared(4, 4);
	world.cleared_lines = 1;
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "the outcome became shorter and the focus lies beyond its last row: a short press dismisses nothing");
	open_cleared(1, 1);
	world.cleared_lines = 4;
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "the outcome grew: the row that was Fertig is a line now");
	open_cleared(INT_MAX, 0);
	check(turn(INT_MAX) == NAV_DO_NOTHING && at(NAV_DTC_CLEARED, INT_MAX - 3) && press() == NAV_DO_DISMISS, "an outcome of INT_MAX lines: its last row INT_MAX - 3 is Fertig");
}

static void test_failed(void)
{
	nav_t before;

	open_failed(DTC_FLOW_FAILED);
	before = nav;
	check(turn(1) == NAV_DO_NOTHING && stays(&before) && turn(-1) == NAV_DO_NOTHING && stays(&before), "failure, turning: nothing");
	check(swipe(1) == NAV_DO_NOTHING && stays(&before), "failure: a swipe is ignored");
	check(held(HOLD_CONFIRMED) == NAV_DO_NOTHING && stays(&before) && held(HOLD_STUCK) == NAV_DO_NOTHING && stays(&before), "failure: whatever the hold reports is nothing here");
	check(press() == NAV_DO_DISMISS && at(NAV_DTC, 0), "failure, short press: acknowledged, dismissed, back to the fault memory");
	open_failed(DTC_FLOW_UNKNOWN);
	check(press() == NAV_DO_DISMISS && at(NAV_DTC, 0), "unknown outcome of a clear, short press: dismissed, back to the fault memory");
	open_failed(DTC_FLOW_FAILED);
	check(long_press() == NAV_DO_NOTHING && at(NAV_DTC, 0), "failure, long press: back to the fault memory without a dismiss");
	check(turn(1) == NAV_DO_NOTHING && press() == NAV_DO_NOTHING && at(NAV_DTC_FAILED, 0), "the failure left by a long press is still there to be looked at");
	check(tap(0) == NAV_DO_DISMISS && at(NAV_DTC, 0), "failure, a tap on row 0: counts as a short press");
	open_failed(DTC_FLOW_FAILED);
	check(tap(5) == NAV_DO_DISMISS && at(NAV_DTC, 0), "failure, a tap on row 5: counts as a short press, the row is not looked at");
	open_failed(DTC_FLOW_FAILED);
	check(tap(-1) == NAV_DO_DISMISS && at(NAV_DTC, 0), "failure, a tap on row -1: counts as a short press");
	open_failed(DTC_FLOW_FAILED);
	check(tap(INT_MAX) == NAV_DO_DISMISS && at(NAV_DTC, 0), "failure, a tap on the largest row: counts as a short press, the focus is on row 0 afterwards");
}

static void test_old(void)
{
	nav_t before;

	// Two lines, then 2 Zurück
	open_old(2, 0);
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "old list, short press on a line: nothing");
	check(turn(1) == NAV_DO_NOTHING && at(NAV_DTC_OLD, 1) && press() == NAV_DO_NOTHING && at(NAV_DTC_OLD, 1), "old list, short press on its last line: nothing");
	check(turn(9) == NAV_DO_NOTHING && at(NAV_DTC_OLD, 2) && press() == NAV_DO_NOTHING && at(NAV_DTC, 0), "old list, short press on Zurück: back to the fault memory, nothing is dismissed");
	open_old(2, 1);
	check(long_press() == NAV_DO_NOTHING && at(NAV_DTC, 0), "old list, long press: back to the fault memory");
	open_old(2, 0);
	before = nav;
	check(tap(3) == NAV_DO_NOTHING && stays(&before) && swipe(1) == NAV_DO_NOTHING && stays(&before), "old list: a tap behind its rows and a swipe are ignored");
	check(tap(2) == NAV_DO_NOTHING && at(NAV_DTC, 0), "old list, a tap on Zurück: back to the fault memory");
	open_old(1, 1);
	world.old_lines = 5;
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "the old list grew: the row that was Zurück is a line now");
	open_old(5, 5);
	world.old_lines = 2;
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "the old list became shorter and the focus lies beyond its last row: a short press does nothing");
	open_old(INT_MAX, 0);
	check(turn(INT_MAX) == NAV_DO_NOTHING && at(NAV_DTC_OLD, INT_MAX - 3) && press() == NAV_DO_NOTHING && at(NAV_DTC, 0), "an old list of INT_MAX lines: its last row INT_MAX - 3 is Zurück");
}

static void test_brightness(void)
{
	nav_t before;

	open_brightness(60);
	check(turn(1) == NAV_DO_BRIGHTNESS && at(NAV_BRIGHTNESS, 0) && nav.value == 65, "brightness, one detent: 5 percent more, to be set at once");
	check(turn(-1) == NAV_DO_BRIGHTNESS && at(NAV_BRIGHTNESS, 0) && nav.value == 60, "brightness, one detent back: 5 percent less");
	check(turn(3) == NAV_DO_BRIGHTNESS && nav.value == 75, "brightness, three detents: 15 percent more");
	check(turn(-4) == NAV_DO_BRIGHTNESS && nav.value == 55, "brightness, four detents back: 20 percent less");
	check(turn(0) == NAV_DO_BRIGHTNESS && nav.value == 55, "brightness, a turn of no detent: the value stays and is to be set all the same");
	check(turn(9) == NAV_DO_BRIGHTNESS && nav.value == 100, "brightness, nine detents from 55: exactly 100");
	check(turn(1) == NAV_DO_BRIGHTNESS && nav.value == 100, "brightness, a detent at 100: it stays 100 and is to be set all the same");
	check(turn(-19) == NAV_DO_BRIGHTNESS && nav.value == 5, "brightness, 19 detents back from 100: exactly 5");
	check(turn(-1) == NAV_DO_BRIGHTNESS && nav.value == 5, "brightness, a detent back at 5: it stays 5");
	check(turn(INT_MAX) == NAV_DO_BRIGHTNESS && nav.value == 100, "brightness, the largest number of detents: 100");
	check(turn(INT_MIN) == NAV_DO_BRIGHTNESS && nav.value == 5, "brightness, the largest number of detents back: 5");
	check(at(NAV_BRIGHTNESS, 0) && nav.page == 2, "turning on the brightness screen changes neither focus nor page");

	open_brightness(97);
	check(turn(1) == NAV_DO_BRIGHTNESS && nav.value == 100, "brightness 97, one detent: kept at 100");
	open_brightness(96);
	check(turn(1) == NAV_DO_BRIGHTNESS && nav.value == 100, "brightness 96, one detent: 101 is kept at 100");
	open_brightness(94);
	check(turn(1) == NAV_DO_BRIGHTNESS && nav.value == 99, "brightness 94, one detent: 99");
	open_brightness(9);
	check(turn(-1) == NAV_DO_BRIGHTNESS && nav.value == 5, "brightness 9, one detent back: 4 is kept at 5");
	open_brightness(11);
	check(turn(-1) == NAV_DO_BRIGHTNESS && nav.value == 6, "brightness 11, one detent back: 6");

	open_brightness(60);
	turn(2);
	world.brightness = 30;
	check(press() == NAV_DO_SETTINGS_STORE && at(NAV_MENU, 1) && nav.value == 70, "brightness, short press: the value is to be stored, back to the menu with the focus on Helligkeit");
	open_brightness(60);
	turn(-3);
	check(long_press() == NAV_DO_SETTINGS_STORE && at(NAV_MENU, 1) && nav.value == 45, "brightness, long press: the value is to be stored, back to the menu with the focus on Helligkeit");
	open_brightness(60);
	check(press() == NAV_DO_SETTINGS_STORE && at(NAV_MENU, 1) && nav.value == 60, "brightness left without turning: the value in use is to be stored");

	open_brightness(60);
	before = nav;
	check(tap(0) == NAV_DO_NOTHING && stays(&before) && tap(1) == NAV_DO_NOTHING && stays(&before), "brightness: a tap is ignored");
	check(swipe(1) == NAV_DO_NOTHING && stays(&before) && swipe(-1) == NAV_DO_NOTHING && stays(&before), "brightness: a swipe is ignored and changes no value");
	check(held(HOLD_CONFIRMED) == NAV_DO_NOTHING && stays(&before), "brightness: whatever the hold reports is nothing here");
	world.brightness = 20;
	check(turn(1) == NAV_DO_BRIGHTNESS && nav.value == 65, "the brightness of the world changes while it is being set: the value being set counts");
}

static void test_web(void)
{
	nav_t before;

	open_web(0);
	before = nav;
	check(press() == NAV_DO_RELEASE_ON && stays(&before), "web access, short press on the release while it is closed: it is to be opened, the screen stays");
	world.release_open = true;
	check(press() == NAV_DO_RELEASE_OFF && stays(&before), "web access, short press on the release while it is open: it is to be closed, the screen stays");
	check(turn(1) == NAV_DO_NOTHING && at(NAV_WEB, 1) && turn(1) == NAV_DO_NOTHING && at(NAV_WEB, 1) && turn(-2) == NAV_DO_NOTHING && at(NAV_WEB, 0),
	      "web access: turning moves the focus over its two rows, hard ends");
	open_web(1);
	world.release_open = true;
	check(press() == NAV_DO_NOTHING && at(NAV_MENU, 3) && nav.page == 2, "web access, short press on Zurück: the menu with the focus on Web-Zugriff");
	open_web(0);
	check(long_press() == NAV_DO_NOTHING && at(NAV_MENU, 3), "web access, long press: the menu with the focus on Web-Zugriff, the release is not touched");
	open_web(1);
	before = nav;
	before.row = 0;
	check(tap(0) == NAV_DO_RELEASE_ON && stays(&before), "web access, a tap on the release: the focus goes there, it is to be opened");
	before.row = 0;
	check(tap(2) == NAV_DO_NOTHING && stays(&before) && tap(-1) == NAV_DO_NOTHING && stays(&before) && swipe(1) == NAV_DO_NOTHING && stays(&before),
	      "web access: a tap on a row that does not exist and a swipe are ignored");
	check(tap(1) == NAV_DO_NOTHING && at(NAV_MENU, 3), "web access, a tap on Zurück: the menu");
}

static void test_info(void)
{
	nav_t before;

	open_info(4, 0);
	check(turn(-1) == NAV_DO_NOTHING && at(NAV_INFO, 0) && turn(2) == NAV_DO_NOTHING && at(NAV_INFO, 2) && turn(5) == NAV_DO_NOTHING && at(NAV_INFO, 3),
	      "info: turning scrolls over its four lines, hard ends");
	check(press() == NAV_DO_NOTHING && at(NAV_MENU, 4) && nav.page == 2, "info, short press on its last line: the menu with the focus on Info");
	open_info(4, 0);
	check(press() == NAV_DO_NOTHING && at(NAV_MENU, 4), "info, short press on its first line: the menu with the focus on Info");
	open_info(4, 2);
	check(long_press() == NAV_DO_NOTHING && at(NAV_MENU, 4), "info, long press: the menu with the focus on Info");
	open_info(4, 0);
	check(tap(3) == NAV_DO_NOTHING && at(NAV_MENU, 4), "info, a tap on a line: as a short press, the menu");
	open_info(4, 1);
	before = nav;
	check(tap(4) == NAV_DO_NOTHING && stays(&before) && tap(-1) == NAV_DO_NOTHING && stays(&before) && swipe(1) == NAV_DO_NOTHING && stays(&before),
	      "info: a tap on a line that does not exist and a swipe are ignored");

	open_info(0, 0);
	before = nav;
	check(turn(1) == NAV_DO_NOTHING && stays(&before) && turn(-1) == NAV_DO_NOTHING && stays(&before), "an info without lines: turning does nothing, the focus stays 0");
	check(tap(0) == NAV_DO_NOTHING && stays(&before), "an info without lines: a tap is ignored");
	check(press() == NAV_DO_NOTHING && at(NAV_MENU, 4), "an info without lines, short press: the menu");
	open_info(6, 5);
	world.info_lines = 2;
	check(press() == NAV_DO_NOTHING && at(NAV_MENU, 4), "the info became shorter and the focus lies beyond its last line: a short press leads to the menu all the same");
	open_info(INT_MAX, 0);
	check(turn(INT_MAX) == NAV_DO_NOTHING && at(NAV_INFO, INT_MAX - 4), "an info of INT_MAX lines: the last line is INT_MAX - 4");
}

static void test_settings(void)
{
	static const nav_do_t actions[5] = {NAV_DO_NOTHING, NAV_DO_NOTHING, NAV_DO_REBOOT, NAV_DO_PREVIOUS_FIRMWARE, NAV_DO_FACTORY_RESET};
	//                                        idle  read sent reading list  clear sent clearing cleared failed unknown no phase
	static const bool asks[PHASES + 1] = {true, false,    false,  true, false,     false,   true,   true,  true,   true};
	nav_t before;
	int wrong = 0;

	open_settings(0);
	before = nav;
	check(press() == NAV_DO_REVERSE_TOGGLE && stays(&before), "settings, short press on Drehrichtung: the direction is toggled, the screen stays");
	open_settings(1);
	before = nav;
	check(press() == NAV_DO_AP_TOGGLE && stays(&before), "settings, short press on Hotspot: the access point is toggled, the screen stays");
	open_settings(2);
	check(press() == NAV_DO_NOTHING && at(NAV_CONFIRM, 0) && nav.confirm == NAV_DO_REBOOT, "settings, short press on Neustart: the dialog for the restart with the focus on Abbrechen, nothing to carry out yet");
	open_settings(3);
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "settings, short press on Vorherige Version without a firmware in the other slot: nothing");
	world.previous_firmware = true;
	check(press() == NAV_DO_NOTHING && at(NAV_CONFIRM, 0) && nav.confirm == NAV_DO_PREVIOUS_FIRMWARE, "settings, short press on Vorherige Version with a firmware in the other slot: the dialog for it");
	open_settings(4);
	check(press() == NAV_DO_NOTHING && at(NAV_CONFIRM, 0) && nav.confirm == NAV_DO_FACTORY_RESET, "settings, short press on Werkseinstellungen: the dialog for the factory reset");
	open_settings(5);
	world.previous_firmware = true;
	check(press() == NAV_DO_NOTHING && at(NAV_MENU, 5) && nav.page == 2, "settings, short press on Zurück: the menu with the focus on Einstellungen");
	open_settings(4);
	check(long_press() == NAV_DO_NOTHING && at(NAV_MENU, 5), "settings, long press on Werkseinstellungen: the menu with the focus on Einstellungen, nothing is reset");
	open_settings(0);
	check(turn(9) == NAV_DO_NOTHING && at(NAV_SETTINGS, 5) && turn(-2) == NAV_DO_NOTHING && at(NAV_SETTINGS, 3) && turn(-7) == NAV_DO_NOTHING && at(NAV_SETTINGS, 0),
	      "settings: turning moves the focus over its six rows, hard ends");
	check(tap(4) == NAV_DO_NOTHING && at(NAV_CONFIRM, 0) && nav.confirm == NAV_DO_FACTORY_RESET, "settings, a tap on Werkseinstellungen: the dialog, the reset itself is not carried out");
	open_settings(0);
	before = nav;
	check(tap(6) == NAV_DO_NOTHING && stays(&before) && tap(-1) == NAV_DO_NOTHING && stays(&before) && swipe(1) == NAV_DO_NOTHING && stays(&before),
	      "settings: a tap on a row that does not exist and a swipe are ignored");
	before.row = 3;
	check(tap(3) == NAV_DO_NOTHING && stays(&before), "settings, a tap on Vorherige Version without a firmware in the other slot: the focus goes there, nothing else");
	before.row = 1;
	check(tap(1) == NAV_DO_AP_TOGGLE && stays(&before), "settings, a tap on Hotspot: the access point is toggled");

	// An access point that stays on whatever is asked (link.h: safe mode, or no stored network) has nothing to
	// switch: the row does nothing, as Vorherige Version without a firmware
	open_settings(1);
	world.ap_kept = true;
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "settings, short press on Hotspot while the access point is kept on: nothing, the screen stays");
	check(nav.last_input_ms == now && nav.clock_ms == now, "settings, a short press on Hotspot that does nothing: an input for the idle time all the same");
	check(press() == NAV_DO_NOTHING && stays(&before), "settings, a second short press on the kept Hotspot: nothing again");
	world.ap_kept = false;
	check(press() == NAV_DO_AP_TOGGLE && stays(&before), "settings, short press on Hotspot when the access point is not kept any more: it is toggled");
	world.ap_kept = true;
	check(press() == NAV_DO_NOTHING && stays(&before), "settings, the access point is kept again: the next short press on Hotspot does nothing - the world of the call decides");
	open_settings(0);
	world.ap_kept = true;
	before = nav;
	before.row = 1;
	check(tap(1) == NAV_DO_NOTHING && stays(&before), "settings, a tap on Hotspot while the access point is kept on: the focus goes there, nothing else");
	check(nav.last_input_ms == now && nav.clock_ms == now, "settings, a tap on the kept Hotspot: an input for the idle time all the same");
	check(tap(1) == NAV_DO_NOTHING && stays(&before), "settings, a tap on the kept Hotspot with the focus on it: nothing either");
	open_settings(0);
	world.ap_kept = true;
	world.previous_firmware = true;
	before = nav;
	check(press() == NAV_DO_REVERSE_TOGGLE && stays(&before), "settings, the access point kept on: a short press on Drehrichtung toggles the direction as always");
	check(tap(2) == NAV_DO_NOTHING && at(NAV_CONFIRM, 0) && nav.confirm == NAV_DO_REBOOT && press() == NAV_DO_NOTHING && at(NAV_SETTINGS, 2),
	      "settings, the access point kept on: Neustart asks as always");
	check(tap(3) == NAV_DO_NOTHING && at(NAV_CONFIRM, 0) && nav.confirm == NAV_DO_PREVIOUS_FIRMWARE && press() == NAV_DO_NOTHING && at(NAV_SETTINGS, 3),
	      "settings, the access point kept on: Vorherige Version asks as always");
	check(tap(4) == NAV_DO_NOTHING && at(NAV_CONFIRM, 0) && nav.confirm == NAV_DO_FACTORY_RESET && press() == NAV_DO_NOTHING && at(NAV_SETTINGS, 4),
	      "settings, the access point kept on: Werkseinstellungen asks as always");
	check(turn(-3) == NAV_DO_NOTHING && at(NAV_SETTINGS, 1) && turn(1) == NAV_DO_NOTHING && at(NAV_SETTINGS, 2) && turn(-2) == NAV_DO_NOTHING && at(NAV_SETTINGS, 0),
	      "settings, the access point kept on: the focus moves onto Hotspot and over it as over every row");
	check(tap(5) == NAV_DO_NOTHING && at(NAV_MENU, 5), "settings, the access point kept on: Zurück leads to the menu as always");
	open_settings(1);
	world.ap_kept = true;
	check(long_press() == NAV_DO_NOTHING && at(NAV_MENU, 5), "settings, long press on the kept Hotspot: the menu, as from every row");
	// Only the settings have a row for the access point
	open_menu(1);
	world.ap_kept = true;
	check(press() == NAV_DO_NOTHING && at(NAV_BRIGHTNESS, 0), "menu, the access point kept on: row 1 there is Helligkeit and opens as always");
	open_web(1);
	world.ap_kept = true;
	check(tap(0) == NAV_DO_RELEASE_ON && at(NAV_WEB, 0) && press() == NAV_DO_RELEASE_ON && tap(1) == NAV_DO_NOTHING && at(NAV_MENU, 3),
	      "web access, the access point kept on: Freigabe and Zurück do what they always do");

	// The dialog for each of the three, opened with row 2, 3 and 4
	for(int row = 2; row <= 4; row++)
	{
		static const char *const texts[5][8] =
		{
			{NULL}, {NULL},
			{"dialog for the restart, turning: the focus moves between Abbrechen and Ausführen, hard ends",
			 "dialog for the restart, short press on Abbrechen: the settings with the focus on Neustart, nothing waits any more",
			 "dialog for the restart, long press on Ausführen: the settings with the focus on Neustart, nothing is carried out",
			 "dialog for the restart, short press on Ausführen: the restart is to be carried out, the value pages are shown",
			 "dialog for the restart, a tap on Ausführen: ignored, the focus stays on Abbrechen and the dialog waits on",
			 "dialog for the restart, a tap on Abbrechen: the settings with the focus on Neustart",
			 "dialog for the restart: a tap on a row that does not exist, a swipe and the hold are nothing",
			 "dialog for the restart, a tap on Ausführen with the focus on it: ignored as well, nothing is carried out"},
			{"dialog for the previous version, turning: the focus moves between Abbrechen and Ausführen, hard ends",
			 "dialog for the previous version, short press on Abbrechen: the settings with the focus on Vorherige Version, nothing waits any more",
			 "dialog for the previous version, long press on Ausführen: the settings with the focus on Vorherige Version, nothing is carried out",
			 "dialog for the previous version, short press on Ausführen: the other firmware is to be started, the value pages are shown",
			 "dialog for the previous version, a tap on Ausführen: ignored, the focus stays on Abbrechen and the dialog waits on",
			 "dialog for the previous version, a tap on Abbrechen: the settings with the focus on Vorherige Version",
			 "dialog for the previous version: a tap on a row that does not exist, a swipe and the hold are nothing",
			 "dialog for the previous version, a tap on Ausführen with the focus on it: ignored as well, nothing is carried out"},
			{"dialog for the factory reset, turning: the focus moves between Abbrechen and Ausführen, hard ends",
			 "dialog for the factory reset, short press on Abbrechen: the settings with the focus on Werkseinstellungen, nothing waits any more",
			 "dialog for the factory reset, long press on Ausführen: the settings with the focus on Werkseinstellungen, nothing is carried out",
			 "dialog for the factory reset, short press on Ausführen: the reset is to be carried out, the value pages are shown",
			 "dialog for the factory reset, a tap on Ausführen: ignored, the focus stays on Abbrechen and the dialog waits on",
			 "dialog for the factory reset, a tap on Abbrechen: the settings with the focus on Werkseinstellungen",
			 "dialog for the factory reset: a tap on a row that does not exist, a swipe and the hold are nothing",
			 "dialog for the factory reset, a tap on Ausführen with the focus on it: ignored as well, nothing is carried out"},
		};

		open_ask(row);
		check(turn(-1) == NAV_DO_NOTHING && at(NAV_CONFIRM, 0) && turn(1) == NAV_DO_NOTHING && at(NAV_CONFIRM, 1) && turn(1) == NAV_DO_NOTHING && at(NAV_CONFIRM, 1) &&
		      turn(-3) == NAV_DO_NOTHING && at(NAV_CONFIRM, 0) && nav.confirm == actions[row], texts[row][0]);
		check(press() == NAV_DO_NOTHING && at(NAV_SETTINGS, row) && nav.confirm == NAV_DO_NOTHING, texts[row][1]);
		open_ask(row);
		turn(1);
		check(long_press() == NAV_DO_NOTHING && at(NAV_SETTINGS, row) && nav.confirm == NAV_DO_NOTHING, texts[row][2]);
		open_ask(row);
		turn(1);
		check(press() == actions[row] && page_is(2) && nav.confirm == NAV_DO_NOTHING, texts[row][3]);
		open_ask(row);
		before = nav;
		check(tap(1) == NAV_DO_NOTHING && stays(&before) && at(NAV_CONFIRM, 0) && nav.confirm == actions[row], texts[row][4]);
		open_ask(row);
		turn(1);
		before = nav;
		check(tap(1) == NAV_DO_NOTHING && stays(&before) && at(NAV_CONFIRM, 1) && nav.confirm == actions[row], texts[row][7]);
		open_ask(row);
		turn(1);
		check(tap(0) == NAV_DO_NOTHING && at(NAV_SETTINGS, row) && nav.confirm == NAV_DO_NOTHING, texts[row][5]);
		open_ask(row);
		turn(1);
		before = nav;
		check(tap(2) == NAV_DO_NOTHING && stays(&before) && tap(-1) == NAV_DO_NOTHING && stays(&before) && swipe(1) == NAV_DO_NOTHING && stays(&before) &&
		      held(HOLD_CONFIRMED) == NAV_DO_NOTHING && stays(&before), texts[row][6]);
	}

	open_ask(4);
	check(long_press() == NAV_DO_NOTHING && at(NAV_SETTINGS, 4), "dialog for the factory reset, long press on Abbrechen: the settings");

	// While the own request is under way nothing leads to a restart
	open_settings(4);
	world.flow = DTC_FLOW_CLEARING;
	before = nav;
	check(press() == NAV_DO_NOTHING && stays(&before), "settings, short press on Werkseinstellungen while the own clear runs: nothing, no dialog opens");
	world.flow = DTC_FLOW_CLEARED;
	check(press() == NAV_DO_NOTHING && at(NAV_CONFIRM, 0) && nav.confirm == NAV_DO_FACTORY_RESET, "settings, short press on Werkseinstellungen when the clear has ended: the dialog opens");
	open_settings(0);
	world.flow = DTC_FLOW_READING;
	before = nav;
	before.row = 2;
	check(tap(2) == NAV_DO_NOTHING && stays(&before), "settings, a tap on Neustart while the own read runs: the focus goes there, nothing else");
	for(int i = 0; i <= PHASES; i++)
	{
		for(int row = 2; row <= 4; row++)
		{
			for(int by_tap = 0; by_tap < 2; by_tap++)
			{
				nav_do_t action;
				bool right;

				open_settings(by_tap ? 0 : row);
				world.previous_firmware = true;
				world.flow = phases[i];
				action = by_tap ? tap(row) : press();
				right = asks[i] ? at(NAV_CONFIRM, 0) && nav.confirm == actions[row] : at(NAV_SETTINGS, row) && nav.confirm == NAV_DO_NOTHING;
				if(action != NAV_DO_NOTHING || !right || nav.page != 2)
				{
					printf("  flow %s, row %d, %s: action %d, screen %d row %d, waits %d\n", phase_names[i], row, by_tap ? "tap" : "short press", (int)action, (int)nav.screen, nav.row, (int)nav.confirm);
					wrong++;
				}
			}
		}
	}
	check(wrong == 0, "settings, a short press and a tap on Neustart, Vorherige Version and Werkseinstellungen in every phase of the flow: the dialog opens, "
	                  "but not while the own request is under way (four phases) - then nothing waits and the settings stay");
	wrong = 0;
	for(int i = 0; i <= PHASES; i++)
	{
		open_settings(0);
		world.flow = phases[i];
		if(press() != NAV_DO_REVERSE_TOGGLE || turn(1) != NAV_DO_NOTHING || press() != NAV_DO_AP_TOGGLE || !at(NAV_SETTINGS, 1) || tap(5) != NAV_DO_NOTHING || !at(NAV_MENU, 5)) wrong++;
	}
	check(wrong == 0, "settings in every phase of the flow: Drehrichtung, Hotspot and Zurück do what they always do");
	wrong = 0;
	for(int i = 0; i <= PHASES; i++)
	{
		for(int by_tap = 0; by_tap < 2; by_tap++)
		{
			nav_do_t action;

			open_settings(by_tap ? 0 : 1);
			world.flow = phases[i];
			world.ap_kept = true;
			action = by_tap ? tap(1) : press();
			if(action != NAV_DO_NOTHING || !at(NAV_SETTINGS, 1) || nav.confirm != NAV_DO_NOTHING || nav.page != 2 || nav.last_input_ms != now)
			{
				printf("  flow %s, %s: action %d, screen %d row %d\n", phase_names[i], by_tap ? "tap" : "short press", (int)action, (int)nav.screen, nav.row);
				wrong++;
			}
		}
	}
	check(wrong == 0, "settings, a short press and a tap on Hotspot while the access point is kept on, in every phase of the flow: nothing but the focus and the idle time");
	open_ask(3);
	world.previous_firmware = false;
	check(turn(1) == NAV_DO_NOTHING && press() == NAV_DO_PREVIOUS_FIRMWARE && page_is(2),
	      "the firmware in the other slot is gone while its dialog is shown: Ausführen still returns what the dialog was opened for");
	open_ask(4);
	check(press() == NAV_DO_NOTHING && press() == NAV_DO_NOTHING && at(NAV_CONFIRM, 0) && nav.confirm == NAV_DO_FACTORY_RESET && long_press() == NAV_DO_NOTHING &&
	      turn(-2) == NAV_DO_NOTHING && press() == NAV_DO_NOTHING && at(NAV_CONFIRM, 0) && nav.confirm == NAV_DO_REBOOT,
	      "a dialog opened, left and opened again for something else: it asks for what it was opened with last");
	check(turn(1) == NAV_DO_NOTHING && press() == NAV_DO_REBOOT && press() == NAV_DO_NOTHING && at(NAV_MENU, 0) && nav.confirm == NAV_DO_NOTHING,
	      "after Ausführen nothing waits to be carried out: the next short press opens the menu");
}

// One input on every screen, with the focus in the middle and on the last row, while something lies over
// it. Returns how often the answer was not `expected`, something of the screen below changed, or the input
// did not count for the idle time.
static int under_overlay(nav_overlay_t overlay, nav_do_t (*input)(void), nav_do_t expected)
{
	int wrong = 0;

	for(int screen = 0; screen < SCREENS; screen++)
	{
		for(int last = 0; last < 2; last++)
		{
			nav_t before;
			nav_do_t action;

			reach((nav_screen_t)screen, last != 0);
			set_overlay(overlay);
			before = nav;
			action = input();
			if(action != expected || !stays(&before) || nav.last_input_ms != now || nav.clock_ms != now)
			{
				printf("  %s: action %d, screen %d row %d page %d\n", screen_names[screen], (int)action, (int)nav.screen, nav.row, nav.page);
				wrong++;
			}
		}
	}
	return wrong;
}

static nav_do_t turn_on(void)
{
	return turn(1);
}

static nav_do_t turn_back(void)
{
	return turn(-1);
}

static nav_do_t turn_none(void)
{
	return turn(0);
}

static nav_do_t tap_first(void)
{
	return tap(0);
}

static nav_do_t tap_second(void)
{
	return tap(1);
}

static nav_do_t tap_nowhere(void)
{
	return tap(-1);
}

static nav_do_t swipe_on(void)
{
	return swipe(1);
}

static nav_do_t swipe_back(void)
{
	return swipe(-1);
}

static void test_under_overlays(void)
{
	static const access_ask_t questions[] = {ACCESS_ASK_WIFI, ACCESS_ASK_FIRMWARE, ACCESS_ASK_RESET, (access_ask_t)9};
	int wrong = 0;

	check(under_overlay(NAV_OVER_UPLOAD, turn_on, NAV_DO_NOTHING) + under_overlay(NAV_OVER_UPLOAD, turn_back, NAV_DO_NOTHING) +
	      under_overlay(NAV_OVER_UPLOAD, turn_none, NAV_DO_NOTHING) == 0, "upload over every screen: turning is ignored, the screen below stays, the idle time starts anew");
	check(under_overlay(NAV_OVER_UPLOAD, press, NAV_DO_NOTHING) == 0, "upload over every screen: a short press is ignored");
	check(under_overlay(NAV_OVER_UPLOAD, long_press, NAV_DO_NOTHING) == 0, "upload over every screen: a long press is ignored");
	check(under_overlay(NAV_OVER_UPLOAD, tap_first, NAV_DO_NOTHING) + under_overlay(NAV_OVER_UPLOAD, tap_second, NAV_DO_NOTHING) +
	      under_overlay(NAV_OVER_UPLOAD, tap_nowhere, NAV_DO_NOTHING) == 0, "upload over every screen: a tap is ignored");
	check(under_overlay(NAV_OVER_UPLOAD, swipe_on, NAV_DO_NOTHING) + under_overlay(NAV_OVER_UPLOAD, swipe_back, NAV_DO_NOTHING) == 0,
	      "upload over every screen: a swipe is ignored");

	check(under_overlay(NAV_OVER_ASK, turn_on, NAV_DO_NOTHING) + under_overlay(NAV_OVER_ASK, turn_back, NAV_DO_NOTHING) +
	      under_overlay(NAV_OVER_ASK, turn_none, NAV_DO_NOTHING) == 0, "question over every screen: turning does nothing, the screen below stays");
	check(under_overlay(NAV_OVER_ASK, press, NAV_DO_ASK_CONFIRM) == 0, "question over every screen: a short press confirms it, the screen below stays");
	check(under_overlay(NAV_OVER_ASK, long_press, NAV_DO_ASK_REFUSE) == 0, "question over every screen: a long press refuses it, the screen below stays");
	check(under_overlay(NAV_OVER_ASK, tap_first, NAV_DO_NOTHING) + under_overlay(NAV_OVER_ASK, tap_second, NAV_DO_NOTHING) +
	      under_overlay(NAV_OVER_ASK, tap_nowhere, NAV_DO_NOTHING) == 0,
	      "question over every screen: a tap does nothing, whatever row it names - it neither confirms nor refuses, moves no focus, and is an input for the idle time");
	check(under_overlay(NAV_OVER_ASK, swipe_on, NAV_DO_NOTHING) + under_overlay(NAV_OVER_ASK, swipe_back, NAV_DO_NOTHING) == 0,
	      "question over every screen: a swipe does nothing");

	check(under_overlay(NAV_OVER_UPDATE, turn_on, NAV_DO_NOTHING) + under_overlay(NAV_OVER_UPDATE, turn_back, NAV_DO_NOTHING) +
	      under_overlay(NAV_OVER_UPDATE, turn_none, NAV_DO_NOTHING) == 0, "update question over every screen: turning is ignored, the screen below stays");
	check(under_overlay(NAV_OVER_UPDATE, press, NAV_DO_UPDATE_OK) == 0, "update question over every screen: a short press says yes, the screen below stays");
	check(under_overlay(NAV_OVER_UPDATE, long_press, NAV_DO_NOTHING) == 0, "update question over every screen: a long press is ignored");
	check(under_overlay(NAV_OVER_UPDATE, tap_first, NAV_DO_NOTHING) + under_overlay(NAV_OVER_UPDATE, tap_second, NAV_DO_NOTHING) +
	      under_overlay(NAV_OVER_UPDATE, tap_nowhere, NAV_DO_NOTHING) == 0,
	      "update question over every screen: a tap is ignored, whatever row it names - the knob alone says yes");
	check(under_overlay(NAV_OVER_UPDATE, swipe_on, NAV_DO_NOTHING) + under_overlay(NAV_OVER_UPDATE, swipe_back, NAV_DO_NOTHING) == 0,
	      "update question over every screen: a swipe is ignored");

	for(size_t i = 0; i < sizeof(questions) / sizeof(questions[0]); i++)
	{
		reach(NAV_MENU, false);
		world.asking = questions[i];
		if(press() != NAV_DO_ASK_CONFIRM || !at(NAV_MENU, 2) || long_press() != NAV_DO_ASK_REFUSE || !at(NAV_MENU, 2)) wrong++;
	}
	check(wrong == 0, "every question, also one that is no member of the enum, is confirmed by a short press and refused by a long one");

	reach(NAV_MENU, false);
	world.asking = ACCESS_ASK_WIFI;
	world.update_pending = true;
	check(press() == NAV_DO_ASK_CONFIRM && long_press() == NAV_DO_ASK_REFUSE && tap(0) == NAV_DO_NOTHING && at(NAV_MENU, 2),
	      "question and update question at once: the knob answers the question of the browser, a tap answers neither of the two");
	world.uploading = true;
	check(press() == NAV_DO_NOTHING && long_press() == NAV_DO_NOTHING && tap(0) == NAV_DO_NOTHING && at(NAV_MENU, 2),
	      "upload, question and update question at once: every input is ignored");
	world.uploading = false;
	world.asking = ACCESS_ASK_NONE;
	check(long_press() == NAV_DO_NOTHING && tap(2) == NAV_DO_NOTHING && press() == NAV_DO_UPDATE_OK && at(NAV_MENU, 2),
	      "the question gone, the update question left: a long press and a tap on the focused row do nothing, a short press says yes");
	world.update_pending = false;
	check(press() == NAV_DO_NIGHT_TOGGLE && at(NAV_MENU, 2) && long_press() == NAV_DO_NOTHING && page_is(2), "nothing lies over the screen any more: the inputs reach it again");
}

// No question of the display is confirmed by a tap, on whatever row: one check for each of them
static void test_no_tap_confirms(void)
{
	static const int rows[] = {INT_MIN, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, INT_MAX};
	static const struct
	{
		access_ask_t question;
		const char *rule;
	} asked[] = {
		{ACCESS_ASK_WIFI, "the question of the browser to store WiFi data, over every screen: no tap on any row confirms it, refuses it or changes the screen below"},
		{ACCESS_ASK_FIRMWARE, "the question of the browser to install the uploaded firmware, over every screen: no tap on any row confirms it, refuses it or changes the screen below"},
		{ACCESS_ASK_RESET, "the question of the browser for the factory reset, over every screen: no tap on any row confirms it, refuses it or changes the screen below"},
		{(access_ask_t)9, "a question that is no member of the enum, over every screen: no tap on any row confirms it either"},
	};
	static const struct
	{
		int row;
		const char *rule;
	} dialogs[] = {
		{2, "the dialog of the restart with the focus on either answer: no tap on any row restarts - one on Abbrechen leaves the dialog, every other one changes nothing"},
		{3, "the dialog of the previous version with the focus on either answer: no tap on any row starts it - one on Abbrechen leaves the dialog, every other one changes nothing"},
		{4, "the dialog of the factory reset with the focus on either answer: no tap on any row resets - one on Abbrechen leaves the dialog, every other one changes nothing"},
	};
	nav_t before;
	nav_do_t action;
	int wrong;

	// What the browser asks for
	for(size_t i = 0; i < sizeof(asked) / sizeof(asked[0]); i++)
	{
		wrong = 0;
		for(int screen = 0; screen < SCREENS; screen++)
		{
			for(size_t r = 0; r < 2 * sizeof(rows) / sizeof(rows[0]); r++)
			{
				reach((nav_screen_t)screen, r % 2 != 0);
				world.asking = asked[i].question;
				before = nav;
				action = tap(rows[r / 2]);
				if(action != NAV_DO_NOTHING || !stays(&before))
				{
					printf("  question %d over %s, tap on row %d: action %d, screen %d row %d\n", (int)asked[i].question, screen_names[screen], rows[r / 2], (int)action, (int)nav.screen, nav.row);
					wrong++;
				}
			}
		}
		check(wrong == 0, asked[i].rule);
	}

	// "Update in Ordnung?"
	wrong = 0;
	for(int screen = 0; screen < SCREENS; screen++)
	{
		for(size_t r = 0; r < 2 * sizeof(rows) / sizeof(rows[0]); r++)
		{
			reach((nav_screen_t)screen, r % 2 != 0);
			world.update_pending = true;
			before = nav;
			action = tap(rows[r / 2]);
			if(action != NAV_DO_NOTHING || !stays(&before))
			{
				printf("  update question over %s, tap on row %d: action %d, screen %d row %d\n", screen_names[screen], rows[r / 2], (int)action, (int)nav.screen, nav.row);
				wrong++;
			}
		}
	}
	check(wrong == 0, "the update question over every screen: no tap on any row says that the update is in order, or changes the screen below");

	// The clear of the fault memory
	wrong = 0;
	for(size_t r = 0; r < 2 * sizeof(rows) / sizeof(rows[0]); r++)
	{
		int row = rows[r / 2];

		open_clear_dialog(3);
		turn((int)(r % 2));
		before = nav;
		action = tap(row);
		if(row == 0 ? (action != NAV_DO_HOLD_CLOSE || !at(NAV_DTC_LIST, 4)) : (action != NAV_DO_NOTHING || !stays(&before)))
		{
			printf("  clear dialog with the focus on %d, tap on row %d: action %d, screen %d row %d\n", before.row, row, (int)action, (int)nav.screen, nav.row);
			wrong++;
		}
	}
	check(wrong == 0, "the clear dialog with the focus on either answer: no tap on any row clears or moves the focus - one on Abbrechen leaves the dialog, every other one changes nothing");

	// Restart, previous version and factory reset of the settings
	for(size_t i = 0; i < sizeof(dialogs) / sizeof(dialogs[0]); i++)
	{
		wrong = 0;
		for(size_t r = 0; r < 2 * sizeof(rows) / sizeof(rows[0]); r++)
		{
			int row = rows[r / 2];

			open_ask(dialogs[i].row);
			turn((int)(r % 2));
			before = nav;
			action = tap(row);
			if(row == 0 ? (action != NAV_DO_NOTHING || !at(NAV_SETTINGS, dialogs[i].row)) : (action != NAV_DO_NOTHING || !stays(&before)))
			{
				printf("  dialog of the settings row %d with the focus on %d, tap on row %d: action %d, screen %d row %d\n", dialogs[i].row, before.row, row, (int)action, (int)nav.screen, nav.row);
				wrong++;
			}
		}
		check(wrong == 0, dialogs[i].rule);
	}
}

// A dialog nobody can see is left without its answer
static void test_cancel(void)
{
	static const nav_overlay_t overlays[3] = {NAV_OVER_UPLOAD, NAV_OVER_ASK, NAV_OVER_UPDATE};
	static const char *const settings[3][2] =
	{
		{"the dialog of the restart, the focus on Abbrechen, is cancelled: the settings with the focus on Neustart, nothing to carry out, nothing waits any more",
		 "the dialog of the restart, the focus on Ausführen, is cancelled: the settings with the focus on Neustart - no restart"},
		{"the dialog of the previous version, the focus on Abbrechen, is cancelled: the settings with the focus on Vorherige Version, nothing to carry out",
		 "the dialog of the previous version, the focus on Ausführen, is cancelled: the settings with the focus on Vorherige Version - nothing is started"},
		{"the dialog of the factory reset, the focus on Abbrechen, is cancelled: the settings with the focus on Werkseinstellungen, nothing to carry out",
		 "the dialog of the factory reset, the focus on Ausführen, is cancelled: the settings with the focus on Werkseinstellungen - no reset"},
	};
	static const char *const under[3] =
	{
		"both dialogs are cancelled under an upload as well: the list and the hold to close, the settings",
		"both dialogs are cancelled under a question of the browser as well: the list and the hold to close, the settings",
		"both dialogs are cancelled under the update question as well: the list and the hold to close, the settings",
	};
	nav_t before;
	uint64_t input;
	int wrong = 0;
	bool same;

	// The clear dialog
	open_clear_dialog(3);
	turn(1);
	input = nav.last_input_ms;
	check(at(NAV_DTC_CONFIRM, 1) && cancel() == NAV_DO_HOLD_CLOSE && at(NAV_DTC_LIST, 4),
	      "the clear dialog, the focus on Löschen, is cancelled: back to the list with the focus on Fehler löschen, the hold dialog is to be closed");
	check(nav.last_input_ms == input && nav.clock_ms == now, "a dialog that is cancelled: no input for the idle time, and the time is taken over");
	open_clear_dialog(3);
	check(at(NAV_DTC_CONFIRM, 0) && cancel() == NAV_DO_HOLD_CLOSE && at(NAV_DTC_LIST, 4), "the clear dialog, the focus on Abbrechen, is cancelled: back to the list as well");
	open_clear_dialog(0);
	check(cancel() == NAV_DO_HOLD_CLOSE && at(NAV_DTC_LIST, 1), "the clear dialog over a list without lines is cancelled: the focus on Fehler löschen, row 1");

	// The dialog of the settings, for each of its three questions
	for(int row = 2; row <= 4; row++)
	{
		for(int focus = 0; focus < 2; focus++)
		{
			open_ask(row);
			turn(focus);
			input = nav.last_input_ms;
			check(at(NAV_CONFIRM, focus) && cancel() == NAV_DO_NOTHING && at(NAV_SETTINGS, row) && nav.confirm == NAV_DO_NOTHING && nav.last_input_ms == input && nav.clock_ms == now,
			      settings[row - 2][focus]);
		}
	}
	open_ask(4);
	turn(1);
	cancel();
	check(press() == NAV_DO_NOTHING && at(NAV_CONFIRM, 0) && nav.confirm == NAV_DO_FACTORY_RESET,
	      "a short press behind the cancelled dialog of the factory reset opens it anew, the focus on Abbrechen: what was cancelled is not carried out by the next press");

	// What lies over the dialog does not keep it
	for(int i = 0; i < 3; i++)
	{
		reach(NAV_DTC_CONFIRM, true);
		set_overlay(overlays[i]);
		same = cancel() == NAV_DO_HOLD_CLOSE && at(NAV_DTC_LIST, 4);
		reach(NAV_CONFIRM, true);
		set_overlay(overlays[i]);
		check(same && cancel() == NAV_DO_NOTHING && at(NAV_SETTINGS, 4) && nav.confirm == NAV_DO_NOTHING, under[i]);
	}

	// Every other screen stays
	for(int screen = 0; screen < SCREENS; screen++)
	{
		for(int last = 0; last < 2; last++)
		{
			nav_do_t action;

			if(screen == NAV_DTC_CONFIRM || screen == NAV_CONFIRM) continue;

			reach((nav_screen_t)screen, last != 0);
			before = nav;
			input = nav.last_input_ms;
			action = cancel();
			if(action != NAV_DO_NOTHING || !stays(&before) || nav.last_input_ms != input || nav.clock_ms != now)
			{
				printf("  %s: action %d, screen %d row %d page %d\n", screen_names[screen], (int)action, (int)nav.screen, nav.row, nav.page);
				wrong++;
			}
		}
	}
	check(wrong == 0, "on every screen that is no dialog a cancel changes nothing, asks for nothing, is no input and takes its time over");

	// A time before the latest seen
	reach(NAV_CONFIRM, true);
	input = now;
	check(nav_cancel(&nav, &world, 5) == NAV_DO_NOTHING && at(NAV_SETTINGS, 4) && nav.clock_ms == input, "a cancel with a time before the latest seen: the dialog is left, the time does not step back");
}

static void test_hold_under_overlays(void)
{
	static const nav_overlay_t overlays[3] = {NAV_OVER_UPLOAD, NAV_OVER_ASK, NAV_OVER_UPDATE};
	static const char *const confirmed[3] =
	{
		"a confirmed hold while an upload lies over the clear dialog: nothing is cleared, back to the list",
		"a confirmed hold while a question lies over the clear dialog: nothing is cleared, back to the list",
		"a confirmed hold while the update question lies over the clear dialog: nothing is cleared, back to the list",
	};
	static const char *const others[3] =
	{
		"under an upload a cancelled hold and a hanging switch lead back to the list, waiting and progress change nothing",
		"under a question a cancelled hold and a hanging switch lead back to the list, waiting and progress change nothing",
		"under the update question a cancelled hold and a hanging switch lead back to the list, waiting and progress change nothing",
	};

	for(int i = 0; i < 3; i++)
	{
		nav_t before;
		bool same;

		reach(NAV_DTC_CONFIRM, true);
		set_overlay(overlays[i]);
		check(held(HOLD_CONFIRMED) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4), confirmed[i]);

		reach(NAV_DTC_CONFIRM, true);
		set_overlay(overlays[i]);
		before = nav;
		same = held(HOLD_WAITING) == NAV_DO_NOTHING && stays(&before) && held(HOLD_PROGRESS) == NAV_DO_NOTHING && stays(&before);
		same = same && held(HOLD_CANCELLED) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4);
		reach(NAV_DTC_CONFIRM, true);
		set_overlay(overlays[i]);
		check(same && held(HOLD_STUCK) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4), others[i]);
	}
}

static void test_idle(void)
{
	int stayed = 0, returned = 0, stored = 0, pages = 0, never = 0;
	uint64_t input;
	nav_t before;

	for(int screen = 0; screen < SCREENS; screen++)
	{
		for(int last = 0; last < 2; last++)
		{
			bool excluded = screen == NAV_PAGES || screen == NAV_DTC_BUSY || screen == NAV_DTC_CONFIRM;

			reach((nav_screen_t)screen, last != 0);
			input = now;
			before = nav;
			if(tick_at(input + 119999) == NAV_DO_NOTHING && stays(&before)) stayed++;
			else printf("  %s: left after 119999 ms\n", screen_names[screen]);

			if(excluded)
			{
				if(tick_at(input + 120000) == NAV_DO_NOTHING && stays(&before) && tick_at(input + 10 * 120000) == NAV_DO_NOTHING && stays(&before) &&
				   tick_at(UINT64_MAX) == NAV_DO_NOTHING && stays(&before)) never++;
				else printf("  %s: left by the idle time\n", screen_names[screen]);
				continue;
			}
			if(tick_at(input + 120000) == (screen == NAV_BRIGHTNESS ? NAV_DO_SETTINGS_STORE : NAV_DO_NOTHING)) stored++;
			if(at(NAV_PAGES, 0) && nav.confirm == NAV_DO_NOTHING) returned++;
			else printf("  %s: screen %d row %d after 120000 ms\n", screen_names[screen], (int)nav.screen, nav.row);
			if(nav.page == 2 && nav.value == before.value) pages++;
		}
	}
	check(stayed == 2 * SCREENS, "119999 ms without input: every screen stays, with the focus in the middle and on the last row");
	check(returned == 2 * (SCREENS - 3), "120000 ms without input: every screen but the value pages, the progress and the clear dialog returns to the value pages");
	check(stored == 2 * (SCREENS - 3), "the return after the idle time: from the brightness screen the value is to be stored, from the others nothing is to be carried out");
	check(pages == 2 * (SCREENS - 3), "the return after the idle time: the page shown before is shown again, the value being set is kept for the store");
	check(never == 2 * 3, "the value pages, the progress and the clear dialog are not left by the idle time, however long");

	reach(NAV_DTC_CLEARED, false);
	input = now;
	check(tick_at(input + 120000) == NAV_DO_NOTHING && page_is(2), "the outcome of a clear is left by the idle time without a dismiss");
	world.flow = DTC_FLOW_CLEARED;
	check(press() == NAV_DO_NOTHING && press() == NAV_DO_NOTHING && turn(1) == NAV_DO_NOTHING && press() == NAV_DO_NOTHING && at(NAV_DTC_CLEARED, 0),
	      "the outcome left by the idle time is still there to be looked at");

	// Ticks let no idle time begin anew
	reach(NAV_SETTINGS, false);
	input = now;
	before = nav;
	stayed = 0;
	for(uint64_t passed = 200; passed < 120000; passed += 200)
	{
		if(tick_at(input + passed) == NAV_DO_NOTHING && stays(&before)) stayed++;
	}
	check(stayed == 599 && tick_at(input + 120000) == NAV_DO_NOTHING && page_is(2), "a tick every 200 ms: the screen stays 599 times and is left with the tick at 120000 ms");

	// Every input does, also one that is ignored
	reach(NAV_INFO, false);
	input = now;
	nav_turn(&nav, 1, &world, now = input + 100000);
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_INFO, 2) && tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2), "a turn starts the idle time anew: left 120000 ms after it");
	reach(NAV_INFO, false);
	input = now;
	nav_turn(&nav, 0, &world, now = input + 100000);
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_INFO, 1) && tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2), "a turn of no detent starts the idle time anew");
	reach(NAV_DTC, false);
	input = now;
	nav_short(&nav, &world, now = input + 100000);
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_DTC, 1) && tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2), "a short press that does nothing starts the idle time anew");
	open_settings(1);
	world.ap_kept = true;
	input = now;
	nav_short(&nav, &world, now = input + 100000);
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_SETTINGS, 1) && tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2),
	      "a short press on Hotspot while the access point is kept on does nothing and starts the idle time anew");
	open_settings(0);
	world.ap_kept = true;
	input = now;
	nav_tap(&nav, 1, &world, now = input + 100000);
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_SETTINGS, 1) && tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2),
	      "a tap on Hotspot while the access point is kept on moves the focus there and starts the idle time anew");
	reach(NAV_SETTINGS, false);
	input = now;
	nav_long(&nav, &world, now = input + 100000);
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_MENU, 5) && tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2), "a long press starts the idle time anew");
	reach(NAV_INFO, false);
	input = now;
	nav_tap(&nav, -1, &world, now = input + 100000);
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_INFO, 1) && tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2), "a tap on a row that does not exist starts the idle time anew");
	reach(NAV_BRIGHTNESS, false);
	input = now;
	nav_tap(&nav, 0, &world, now = input + 100000);
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_BRIGHTNESS, 0) && tick_at(input + 220000) == NAV_DO_SETTINGS_STORE && page_is(2), "a tap on a screen without rows starts the idle time anew");
	reach(NAV_CONFIRM, false);
	input = now;
	nav_tap(&nav, 1, &world, now = input + 100000);
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_CONFIRM, 0) && tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2), "a tap on Ausführen that is ignored starts the idle time anew");
	reach(NAV_INFO, false);
	input = now;
	nav_swipe(&nav, 1, &world, now = input + 100000);
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_INFO, 1) && tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2), "a swipe that is ignored starts the idle time anew");
	reach(NAV_INFO, false);
	input = now;
	nav_swipe(&nav, 0, &world, now = input + 100000);
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_INFO, 1) && tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2), "a swipe without direction starts the idle time anew");
	reach(NAV_PAGES, false);
	input = now;
	nav_swipe(&nav, 0, &world, now = input + 100000);
	check(nav.last_input_ms == input + 100000, "a swipe without direction on the value pages is an input as well");

	// The hold is none
	reach(NAV_DTC_LIST, false);
	input = now;
	nav_hold(&nav, HOLD_WAITING, &world, now = input + 100000);
	nav_hold(&nav, HOLD_CONFIRMED, &world, now = input + 110000);
	check(tick_at(input + 119999) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 1) && tick_at(input + 120000) == NAV_DO_NOTHING && page_is(2), "what the hold reports is no input: the idle time goes on");
	reach(NAV_DTC_CONFIRM, true);
	input = now;
	nav_hold(&nav, HOLD_CANCELLED, &world, now = input + 15000);
	check(at(NAV_DTC_LIST, 4) && tick_at(input + 119999) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4) && tick_at(input + 120000) == NAV_DO_NOTHING && page_is(2),
	      "the list after a cancelled hold is left 120000 ms after the last input, not after the hold");
	reach(NAV_DTC_CONFIRM, true);
	input = now;
	nav_hold(&nav, HOLD_CONFIRMED, &world, now = input + 4000);
	world.flow = DTC_FLOW_CLEAR_SENT;
	check(tick_at(input + 200000) == NAV_DO_NOTHING && at(NAV_DTC_BUSY, 0), "the progress of a clear is not left by the idle time");
	world.flow = DTC_FLOW_CLEARED;
	world.cleared_lines = 2;
	check(tick_at(input + 200000) == NAV_DO_NOTHING && at(NAV_DTC_CLEARED, 0) && tick_at(input + 200000) == NAV_DO_NOTHING && page_is(2),
	      "a request that ended after the idle time: its outcome is shown by one tick, the next returns to the value pages");

	// An overlay
	for(int overlay = NAV_OVER_UPLOAD; overlay <= NAV_OVER_UPDATE; overlay++)
	{
		static const char *const texts[4] =
		{
			NULL,
			"under an upload no screen is left by the idle time; the first tick after it is gone returns to the value pages",
			"under a question no screen is left by the idle time; the first tick after it is gone returns to the value pages",
			"under the update question no screen is left by the idle time; the first tick after it is gone returns to the value pages",
		};
		int wrong = 0;

		for(int screen = 0; screen < SCREENS; screen++)
		{
			if(screen == NAV_PAGES || screen == NAV_DTC_BUSY || screen == NAV_DTC_CONFIRM) continue;

			reach((nav_screen_t)screen, true);
			input = now;
			set_overlay((nav_overlay_t)overlay);
			before = nav;
			if(tick_at(input + 120000) != NAV_DO_NOTHING || !stays(&before) || tick_at(input + 500000) != NAV_DO_NOTHING || !stays(&before)) wrong++;
			set_overlay(NAV_OVER_NONE);
			if(tick_at(input + 500000) != (screen == NAV_BRIGHTNESS ? NAV_DO_SETTINGS_STORE : NAV_DO_NOTHING) || !page_is(2)) wrong++;
		}
		check(wrong == 0, texts[overlay]);
	}
	reach(NAV_SETTINGS, false);
	input = now;
	world.uploading = true;
	nav_long(&nav, &world, now = input + 100000);
	world.uploading = false;
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_SETTINGS, 3) && tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2), "a long press that an upload swallowed starts the idle time anew");
	reach(NAV_SETTINGS, false);
	input = now;
	world.asking = ACCESS_ASK_WIFI;
	nav_short(&nav, &world, now = input + 100000);
	world.asking = ACCESS_ASK_NONE;
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_SETTINGS, 3) && tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2), "a short press that answered a question starts the idle time anew");
	reach(NAV_SETTINGS, false);
	input = now;
	world.update_pending = true;
	nav_turn(&nav, 1, &world, now = input + 100000);
	world.update_pending = false;
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_SETTINGS, 3) && tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2), "a turn that the update question swallowed starts the idle time anew");
}

static void test_clock(void)
{
	uint64_t input;

	// The time steps back
	reach(NAV_MENU, false);
	input = now;
	check(tick_at(input + 100000) == NAV_DO_NOTHING && tick_at(500) == NAV_DO_NOTHING && tick_at(0) == NAV_DO_NOTHING && at(NAV_MENU, 2) && nav.clock_ms == input + 100000,
	      "the time steps back to before the last input: no time passed, the screen stays, the latest time seen stays");
	check(tick_at(input + 119999) == NAV_DO_NOTHING && at(NAV_MENU, 2) && tick_at(input + 120000) == NAV_DO_NOTHING && page_is(2),
	      "after the step back the screen is left when the time is 120000 ms past the last input again");

	reach(NAV_MENU, false);
	input = now;
	tick_at(input + 100000);
	nav_turn(&nav, 1, &world, now = input + 40000);
	check(nav.last_input_ms == input + 100000 && nav.clock_ms == input + 100000, "an input with a time before the latest one seen: it is taken to be made at the latest time seen");
	check(tick_at(input + 160000) == NAV_DO_NOTHING && tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_MENU, 3), "119999 ms after the latest time seen before that input: the screen stays");
	check(tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2), "120000 ms after the latest time seen before that input: the screen is left");

	// What the hold reports and the start are calls like the others
	reach(NAV_MENU, false);
	input = now;
	nav_hold(&nav, HOLD_WAITING, &world, now = input + 100000);
	check(nav.clock_ms == input + 100000 && nav.last_input_ms == input, "what the hold reports moves the latest time seen, not the time of the last input");
	nav_turn(&nav, 1, &world, now = input + 40000);
	check(tick_at(input + 219999) == NAV_DO_NOTHING && at(NAV_MENU, 3) && tick_at(input + 220000) == NAV_DO_NOTHING && page_is(2),
	      "an input with a time before the one of the hold: the idle time counts from the time of the hold");
	reach(NAV_MENU, false);
	input = now;
	nav_hold(&nav, HOLD_WAITING, &world, now = input + 100000);
	nav_hold(&nav, HOLD_WAITING, &world, now = input + 50000);
	check(nav.clock_ms == input + 100000, "the hold reported with an earlier time: the latest time seen stays");

	quiet_world();
	nav_init(&nav, &world, 300000);
	nav_short(&nav, &world, now = 1000);
	check(at(NAV_MENU, 0) && nav.last_input_ms == 300000 && tick_at(419999) == NAV_DO_NOTHING && at(NAV_MENU, 0) && tick_at(420000) == NAV_DO_NOTHING && page_is(0),
	      "an input with a time before the start: the idle time counts from the start");

	// The largest times
	quiet_world();
	nav_init(&nav, &world, UINT64_MAX - 200000);
	nav_short(&nav, &world, now = UINT64_MAX - 120000);
	check(tick_at(UINT64_MAX - 1) == NAV_DO_NOTHING && at(NAV_MENU, 0) && tick_at(UINT64_MAX) == NAV_DO_NOTHING && page_is(0), "an idle time that ends with the largest time: the screen is left then");
	quiet_world();
	nav_init(&nav, &world, UINT64_MAX - 200000);
	nav_short(&nav, &world, now = UINT64_MAX - 1000);
	check(tick_at(UINT64_MAX) == NAV_DO_NOTHING && at(NAV_MENU, 0) && tick_at(0) == NAV_DO_NOTHING && tick_at(119000) == NAV_DO_NOTHING && tick_at(300000) == NAV_DO_NOTHING && at(NAV_MENU, 0),
	      "the time of the caller wraps from the largest to 0: a step back, no time passes any more");
	check(turn(1) == NAV_DO_NOTHING && at(NAV_MENU, 1) && nav.last_input_ms == UINT64_MAX && long_press() == NAV_DO_NOTHING && page_is(0), "behind the wrap the inputs still work");

	// Time 0
	quiet_world();
	nav_init(&nav, &world, 0);
	nav_short(&nav, &world, now = 0);
	check(tick_at(119999) == NAV_DO_NOTHING && at(NAV_MENU, 0) && tick_at(120000) == NAV_DO_NOTHING && page_is(0), "an input at time 0: the screen is left at 120000");
}

static void test_tick_dialog(void)
{
	//                                                     idle     read sent reading  list             clear sent clearing cleared  failed   unknown  no phase
	static const nav_screen_t allowed[PHASES + 1] = {NAV_DTC, NAV_DTC, NAV_DTC, NAV_DTC_CONFIRM, NAV_DTC, NAV_DTC, NAV_DTC, NAV_DTC, NAV_DTC, NAV_DTC};
	static const nav_screen_t forbidden[PHASES + 1] = {NAV_DTC, NAV_DTC, NAV_DTC, NAV_DTC_LIST, NAV_DTC, NAV_DTC, NAV_DTC, NAV_DTC, NAV_DTC, NAV_DTC};
	nav_t before;
	int wrong = 0;

	reach(NAV_DTC_CONFIRM, true);
	before = nav;
	check(tick() == NAV_DO_NOTHING && stays(&before) && tick() == NAV_DO_NOTHING && stays(&before), "clear dialog, the list is there and may be cleared: ticks change nothing");
	world.can_clear = false;
	check(tick() == NAV_DO_HOLD_CLOSE && at(NAV_DTC_LIST, 4), "clear dialog, the list may not be cleared any more: back to the list with the focus on Fehler löschen, the hold dialog is to be closed");
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4), "the hold dialog is to be closed once: the next tick has nothing to carry out");

	for(int clear = 0; clear < 2; clear++)
	{
		for(int i = 0; i <= PHASES; i++)
		{
			nav_screen_t expected = clear ? allowed[i] : forbidden[i];
			int row = expected == NAV_DTC_CONFIRM ? 1 : expected == NAV_DTC_LIST ? 4 : 0;

			reach(NAV_DTC_CONFIRM, true);
			world.flow = phases[i];
			world.can_clear = clear != 0;
			if(tick() != (expected == NAV_DTC_CONFIRM ? NAV_DO_NOTHING : NAV_DO_HOLD_CLOSE) || !at(expected, row))
			{
				printf("  flow %s, can_clear %d: screen %d row %d\n", phase_names[i], clear, (int)nav.screen, nav.row);
				wrong++;
			}
		}
	}
	check(wrong == 0, "clear dialog, every phase with and without the clear allowed: it stays only with the list and the clear allowed, leads to the list while the flow is LIST, else to the fault memory");

	reach(NAV_DTC_CONFIRM, false);
	world.flow = DTC_FLOW_IDLE;
	world.list_lines = 0;
	check(tick() == NAV_DO_HOLD_CLOSE && at(NAV_DTC, 0) && tick() == NAV_DO_NOTHING && at(NAV_DTC, 0), "clear dialog, the list is gone: the fault memory with the focus on Lesen, the hold dialog is to be closed once");
	reach(NAV_DTC_CONFIRM, false);
	world.can_clear = false;
	world.list_lines = 7;
	check(tick() == NAV_DO_HOLD_CLOSE && at(NAV_DTC_LIST, 8), "clear dialog closed by a tick over a list that grew to 7 lines: the focus on row 8, Fehler löschen");
}

static void test_tick_busy(void)
{
	//                                                    idle     read sent     reading       list          clear sent    clearing      cleared          failed          unknown         no phase
	static const nav_screen_t expected[PHASES + 1] = {NAV_DTC, NAV_DTC_BUSY, NAV_DTC_BUSY, NAV_DTC_LIST, NAV_DTC_BUSY, NAV_DTC_BUSY, NAV_DTC_CLEARED, NAV_DTC_FAILED, NAV_DTC_FAILED, NAV_DTC};
	int wrong = 0;

	for(int i = 0; i <= PHASES; i++)
	{
		open_busy();
		world.flow = phases[i];
		world.list_lines = world.cleared_lines = 3;
		world.can_read = world.can_clear = true;
		if(tick() != NAV_DO_NOTHING || !at(expected[i], 0) || nav.page != 2)
		{
			printf("  flow %s: screen %d row %d\n", phase_names[i], (int)nav.screen, nav.row);
			wrong++;
		}
	}
	check(wrong == 0, "progress, a tick in every phase: it stays while the request is under way and leads to the list, the outcome, the failure or the fault memory when it ended");

	// A read that the flow refused: the old list is still there
	open_list(3, 3);
	world.can_read = true;
	press();
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC_LIST, 0), "a read asked for from the list that the flow did not begin: the next tick shows the list again, the focus on its first line");
	reach(NAV_DTC_CONFIRM, true);
	held(HOLD_CONFIRMED);
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC_LIST, 0), "a clear asked for that the flow did not begin: the next tick shows the list again");
}

static void test_tick_gone(void)
{
	int wrong = 0;

	for(int i = 0; i <= PHASES; i++)
	{
		bool stay = phases[i] == DTC_FLOW_LIST;

		reach(NAV_DTC_LIST, true);
		world.flow = phases[i];
		world.can_read = world.can_clear = true;
		if(tick() != NAV_DO_NOTHING || !at(stay ? NAV_DTC_LIST : NAV_DTC, stay ? 5 : 0))
		{
			printf("  list, flow %s: screen %d row %d\n", phase_names[i], (int)nav.screen, nav.row);
			wrong++;
		}
	}
	check(wrong == 0, "list, a tick in every phase: it stays while the flow is LIST, else the fault memory with the focus on Lesen");

	wrong = 0;
	for(int i = 0; i <= PHASES; i++)
	{
		bool stay = phases[i] == DTC_FLOW_CLEARED;

		reach(NAV_DTC_CLEARED, true);
		world.flow = phases[i];
		world.list_lines = 2;
		if(tick() != NAV_DO_NOTHING || !at(stay ? NAV_DTC_CLEARED : NAV_DTC, stay ? 2 : 0))
		{
			printf("  cleared, flow %s: screen %d row %d\n", phase_names[i], (int)nav.screen, nav.row);
			wrong++;
		}
	}
	check(wrong == 0, "outcome of the clear, a tick in every phase: it stays while the flow is CLEARED, else the fault memory, without a dismiss");

	wrong = 0;
	for(int i = 0; i <= PHASES; i++)
	{
		bool stay = phases[i] == DTC_FLOW_FAILED || phases[i] == DTC_FLOW_UNKNOWN;

		reach(NAV_DTC_FAILED, false);
		world.flow = phases[i];
		world.list_lines = world.cleared_lines = 2;
		if(tick() != NAV_DO_NOTHING || !at(stay ? NAV_DTC_FAILED : NAV_DTC, 0))
		{
			printf("  failed, flow %s: screen %d row %d\n", phase_names[i], (int)nav.screen, nav.row);
			wrong++;
		}
	}
	check(wrong == 0, "failure, a tick in every phase: it stays while the flow is FAILED or UNKNOWN, else the fault memory, without a dismiss");

	reach(NAV_DTC_OLD, true);
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC_OLD, 2), "old list, a tick while it is stored: it stays");
	world.old_lines = 1;
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC_OLD, 1), "old list with one line left, a tick: it stays, the focus goes to its last row");
	world.old_lines = 0;
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC, 0), "old list, a tick after it was dropped: the fault memory with the focus on Lesen");
	reach(NAV_DTC_OLD, false);
	world.old_lines = -1;
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC, 0), "old list, a tick while the world counts -1 lines: the fault memory");
	reach(NAV_DTC_OLD, false);
	world.flow = DTC_FLOW_READING;
	world.list_lines = world.cleared_lines = 0;
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC_OLD, 1), "old list: it does not depend on the flow or on the other lists");

	// The other screens do not follow the flow
	wrong = 0;
	for(int screen = 0; screen < SCREENS; screen++)
	{
		nav_t before;

		if(screen >= NAV_DTC_BUSY && screen <= NAV_DTC_OLD) continue;
		for(int i = 0; i <= PHASES; i++)
		{
			reach((nav_screen_t)screen, true);
			world.flow = phases[i];
			world.old_lines = world.list_lines = world.cleared_lines = 0;
			world.can_read = world.can_clear = true;
			before = nav;
			if(tick() != NAV_DO_NOTHING || !stays(&before)) wrong++;
		}
	}
	check(wrong == 0, "value pages, menu, fault memory, brightness, web access, info, settings and their dialog: a tick changes nothing, whatever the flow does");
}

static void test_tick_pages(void)
{
	start();
	turn(1);
	check(tick() == NAV_DO_NOTHING && page_is(2), "value pages, a tick while the page is shown: it stays");
	world.catalog = &cat_engine;
	check(tick() == NAV_DO_NOTHING && page_is(4), "value pages, the page lost its values, 0 and 4 equally near: a tick goes to page 4, the one behind it");
	world.catalog = &cat_dpf;
	check(tick() == NAV_DO_NOTHING && page_is(2), "value pages, only page 2 is shown now: a tick goes back to it");
	world.catalog = &cat_other;
	check(tick() == NAV_DO_NOTHING && page_is(-1) && tick() == NAV_DO_NOTHING && page_is(-1), "value pages, no page is shown any more: a tick leaves no page, -1");
	world.catalog = &cat_all;
	check(tick() == NAV_DO_NOTHING && page_is(0), "no page, then the catalogue arrives: a tick goes to the first page");

	start_with(&views, &cat_other);
	world.catalog = &cat_dpf;
	check(tick() == NAV_DO_NOTHING && page_is(2), "no page, then a catalogue for page 2 alone: a tick goes there");
	start_with(&views_full, &cat_other);
	world.catalog = &cat_last;
	check(tick() == NAV_DO_NOTHING && page_is(11), "no page, then a catalogue for the last of twelve pages: a tick goes there");
	start_with(&views_full, &cat_last);
	world.catalog = &cat_first;
	check(tick() == NAV_DO_NOTHING && page_is(0), "on the last of twelve pages, then only the first is shown: a tick goes there");

	start_with(&views_full, &cat_new);
	turn(2);
	world.catalog = &cat_k;
	check(tick() == NAV_DO_NOTHING && page_is(0), "page 2 not shown any more, page 0 two away and page 5 three: a tick goes to page 0");
	start_with(&views_full, &cat_new);
	turn(7);
	world.catalog = &cat_k;
	check(tick() == NAV_DO_NOTHING && page_is(11), "page 9 not shown any more, page 11 two away and page 5 four: a tick goes to page 11");
	start_with(&views_full, &cat_new);
	turn(5);
	world.catalog = &cat_k;
	check(tick() == NAV_DO_NOTHING && page_is(5), "page 6 not shown any more, page 5 next to it: a tick goes to page 5");

	start();
	turn(2);
	world.layout = &views_small;
	check(tick() == NAV_DO_NOTHING && page_is(1), "a layout of two pages stored while page 4 is shown: a tick goes to its last page");
	world.layout = &views_none;
	check(tick() == NAV_DO_NOTHING && page_is(-1), "a layout without pages stored: a tick leaves no page, -1");
	world.layout = &views_hidden;
	world.catalog = &cat_new;
	check(tick() == NAV_DO_NOTHING && page_is(-1), "a layout whose pages are all hidden: a tick leaves no page, -1");
	world.layout = &views;
	check(tick() == NAV_DO_NOTHING && page_is(0), "a layout with pages stored after one without: a tick goes to its first page");

	// Below a menu the page is left alone
	open_menu(2);
	world.catalog = &cat_engine;
	check(tick() == NAV_DO_NOTHING && at(NAV_MENU, 2) && nav.page == 2, "the page lost its values while the menu is shown: a tick leaves the page alone");
	check(long_press() == NAV_DO_NOTHING && nav.screen == NAV_PAGES && nav.page == 2 && tick() == NAV_DO_NOTHING && page_is(4), "back on the value pages the next tick goes to the nearest page");
}

static void test_tick_focus(void)
{
	nav_t before;

	open_list(5, 7);
	world.list_lines = 2;
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4), "the list became shorter, the focus beyond its last row: a tick moves it to the last row");
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4), "the focus on the last row of the list: a tick leaves it there");
	open_list(5, 4);
	world.list_lines = 2;
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4), "the list became shorter, the focus exactly on its last row now: a tick leaves it there");
	open_list(5, 5);
	world.list_lines = 2;
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4), "the list became shorter, the focus one behind its last row: a tick moves it to the last row");
	open_list(2, 4);
	world.list_lines = 9;
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC_LIST, 4), "the list grew: a tick leaves the focus where it is");

	open_cleared(4, 4);
	world.cleared_lines = 1;
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC_CLEARED, 1), "the outcome became shorter: a tick moves the focus to Fertig, its last row");
	open_old(6, 6);
	world.old_lines = 3;
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC_OLD, 3), "the old list became shorter: a tick moves the focus to Zurück, its last row");
	open_info(6, 5);
	world.info_lines = 2;
	check(tick() == NAV_DO_NOTHING && at(NAV_INFO, 1), "the info became shorter: a tick moves the focus to its last line");
	world.info_lines = 0;
	check(tick() == NAV_DO_NOTHING && at(NAV_INFO, 0), "the info lost all lines: a tick moves the focus to row 0");
	world.info_lines = -5;
	before = nav;
	check(tick() == NAV_DO_NOTHING && stays(&before), "an info of -5 lines with the focus on row 0: a tick changes nothing");
}

// Two rules of nav_tick apply at once: the first is followed, the other with the next tick
static void test_tick_order(void)
{
	uint64_t input;

	open_list(5, 7);
	world.flow = DTC_FLOW_IDLE;
	world.list_lines = 0;
	check(tick() == NAV_DO_NOTHING && at(NAV_DTC, 0), "the list is gone and the focus lies beyond its last row: the fault memory, not the last row of a list");

	reach(NAV_DTC_LIST, false);
	input = now;
	world.flow = DTC_FLOW_IDLE;
	check(tick_at(input + 120000) == NAV_DO_NOTHING && at(NAV_DTC, 0), "the list is gone and the idle time is over: the fault memory first");
	check(tick_at(input + 120000) == NAV_DO_NOTHING && page_is(2), "the next tick follows the idle time: the value pages");

	open_info(6, 5);
	input = now;
	world.info_lines = 2;
	check(tick_at(input + 120000) == NAV_DO_NOTHING && at(NAV_INFO, 1), "the focus lies beyond the last row and the idle time is over: the focus first");
	check(tick_at(input + 120000) == NAV_DO_NOTHING && page_is(2), "the tick after the focus was moved follows the idle time: the value pages");

	open_list(5, 7);
	input = now;
	world.flow = DTC_FLOW_CLEARED;
	world.list_lines = 1;
	check(tick_at(input + 130000) == NAV_DO_NOTHING && at(NAV_DTC, 0) && tick_at(input + 130000) == NAV_DO_NOTHING && page_is(2),
	      "the list gone, the focus beyond its last row and the idle time over: the fault memory, then the value pages");

	reach(NAV_DTC_CONFIRM, true);
	input = now;
	world.can_clear = false;
	check(tick_at(input + 120000) == NAV_DO_HOLD_CLOSE && at(NAV_DTC_LIST, 4), "the clear is forbidden and the idle time is over: the dialog is closed first, the list is shown");
	check(tick_at(input + 120000) == NAV_DO_NOTHING && page_is(2), "the tick after the dialog was closed follows the idle time: the value pages");

	reach(NAV_DTC_CONFIRM, true);
	input = now;
	world.flow = DTC_FLOW_IDLE;
	check(tick_at(input + 120000) == NAV_DO_HOLD_CLOSE && at(NAV_DTC, 0) && tick_at(input + 120000) == NAV_DO_NOTHING && page_is(2),
	      "the list gone below the dialog and the idle time over: the dialog is closed first, then the value pages");

	open_busy();
	input = now;
	world.flow = DTC_FLOW_LIST;
	world.list_lines = 3;
	check(tick_at(input + 120000) == NAV_DO_NOTHING && at(NAV_DTC_LIST, 0), "the request ended and the idle time is over: the list is shown first");
	check(tick_at(input + 120000) == NAV_DO_NOTHING && page_is(2), "the tick after the list was shown follows the idle time: the value pages");

	open_busy();
	input = now;
	world.flow = DTC_FLOW_IDLE;
	check(tick_at(input + 120000) == NAV_DO_NOTHING && at(NAV_DTC, 0) && tick_at(input + 120000) == NAV_DO_NOTHING && page_is(2),
	      "the request vanished and the idle time is over: the fault memory first, then the value pages");

	reach(NAV_DTC_OLD, true);
	input = now;
	world.old_lines = 0;
	check(tick_at(input + 120000) == NAV_DO_NOTHING && at(NAV_DTC, 0), "the old list is gone, the focus beyond its last row and the idle time over: the fault memory first");

	open_brightness(60);
	input = now;
	world.catalog = &cat_engine;
	check(tick_at(input + 120000) == NAV_DO_SETTINGS_STORE && nav.screen == NAV_PAGES && nav.page == 2, "the idle time is over while the page below the brightness lost its values: the value pages on that page first");
	check(tick_at(input + 120000) == NAV_DO_NOTHING && page_is(4), "the tick after the return goes to the nearest page that is shown");
}

static void test_tick_under_overlays(void)
{
	static const nav_overlay_t overlays[3] = {NAV_OVER_UPLOAD, NAV_OVER_ASK, NAV_OVER_UPDATE};
	static const char *const texts[3][5] =
	{
		{"under an upload the clear dialog is closed when the clear is forbidden",
		 "under an upload the progress leads to the list when the request ended",
		 "under an upload a list that is gone leads to the fault memory",
		 "under an upload a page that is not shown any more is replaced by the nearest",
		 "under an upload the focus beyond the last row goes to the last row"},
		{"under a question the clear dialog is closed when the clear is forbidden",
		 "under a question the progress leads to the list when the request ended",
		 "under a question a list that is gone leads to the fault memory",
		 "under a question a page that is not shown any more is replaced by the nearest",
		 "under a question the focus beyond the last row goes to the last row"},
		{"under the update question the clear dialog is closed when the clear is forbidden",
		 "under the update question the progress leads to the list when the request ended",
		 "under the update question a list that is gone leads to the fault memory",
		 "under the update question a page that is not shown any more is replaced by the nearest",
		 "under the update question the focus beyond the last row goes to the last row"},
	};

	for(int i = 0; i < 3; i++)
	{
		reach(NAV_DTC_CONFIRM, true);
		set_overlay(overlays[i]);
		world.can_clear = false;
		check(tick() == NAV_DO_HOLD_CLOSE && at(NAV_DTC_LIST, 4), texts[i][0]);

		open_busy();
		set_overlay(overlays[i]);
		world.flow = DTC_FLOW_LIST;
		world.list_lines = 2;
		check(tick() == NAV_DO_NOTHING && at(NAV_DTC_LIST, 0), texts[i][1]);

		reach(NAV_DTC_LIST, true);
		set_overlay(overlays[i]);
		world.flow = DTC_FLOW_IDLE;
		check(tick() == NAV_DO_NOTHING && at(NAV_DTC, 0), texts[i][2]);

		reach(NAV_PAGES, false);
		set_overlay(overlays[i]);
		world.catalog = &cat_engine;
		check(tick() == NAV_DO_NOTHING && page_is(4), texts[i][3]);

		open_info(6, 5);
		set_overlay(overlays[i]);
		world.info_lines = 2;
		check(tick() == NAV_DO_NOTHING && at(NAV_INFO, 1), texts[i][4]);
	}
}

/*
 * The rules a second time, in another shape, for the walk below: the table of the header as a table, the
 * time without input counted up instead of two points in time compared, the pages as the bits written down
 * by hand above instead of a layout, the row a dialog was opened with remembered instead of derived.
 */
typedef struct
{
	unsigned shown;                 // pages in the rotation of the knob
	dtc_flow_phase_t flow;
	bool can_read, can_clear, release_open, previous_firmware;
	bool ap_kept;                   // the own access point stays on whatever is asked
	int list, cleared, old, info;   // lines as the header counts them: within 0 and INT_MAX - 3
	int brightness;
	nav_overlay_t over;
} seen_t;

typedef struct
{
	nav_screen_t screen;
	int page, row, value;
	nav_do_t confirm;
	int came_from;                  // row of the settings the dialog was opened with
	uint64_t latest;                // the latest time seen
	uint64_t idle;                  // time without input
} model_t;

// Rows of a rule: a number, every row, or the n-th row behind the lines of a list (BEHIND - n)
enum
{
	ANY = -1, BEHIND = -10,
};

// Where a rule leads: a screen or nowhere; the focus there: a row, the row Fehler löschen of the list, or
// the row the dialog was opened with; the action: one of nav_do_t or what waits in the dialog
enum
{
	STAY = -1, CLEAR_ROW = -2, CAME_FROM = -3, ASKED = -4,
};

typedef enum
{
	ALWAYS, CAN_READ, CAN_CLEAR, UNDER_WAY, NOT_UNDER_WAY, HAS_LIST, HAS_CLEARED, HAS_FAILED, HAS_OLD, RELEASED, NOT_RELEASED, PREVIOUS_AND_NOT_UNDER_WAY,
	AP_NOT_KEPT,
} when_t;

typedef struct
{
	nav_screen_t screen;
	char input;                     // 's' short press, 'l' long press
	int row;
	when_t when;
	int to;
	int focus;
	int action;
	nav_do_t asks;                  // what waits in the dialog that is entered
} rule_t;

static const rule_t rules[] =
{
	{NAV_PAGES,       's', ANY,        ALWAYS,        NAV_MENU,        0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},

	{NAV_MENU,        's', 0,          UNDER_WAY,     NAV_DTC_BUSY,    0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_MENU,        's', 0,          NOT_UNDER_WAY, NAV_DTC,         0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_MENU,        's', 1,          ALWAYS,        NAV_BRIGHTNESS,  0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_MENU,        's', 2,          ALWAYS,        STAY,            0,         NAV_DO_NIGHT_TOGGLE,   NAV_DO_NOTHING},
	{NAV_MENU,        's', 3,          ALWAYS,        NAV_WEB,         0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_MENU,        's', 4,          ALWAYS,        NAV_INFO,        0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_MENU,        's', 5,          ALWAYS,        NAV_SETTINGS,    0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_MENU,        's', 6,          ALWAYS,        NAV_PAGES,       0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_MENU,        'l', ANY,        ALWAYS,        NAV_PAGES,       0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},

	{NAV_DTC,         's', 0,          CAN_READ,      NAV_DTC_BUSY,    0,         NAV_DO_READ,           NAV_DO_NOTHING},
	{NAV_DTC,         's', 1,          HAS_LIST,      NAV_DTC_LIST,    0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_DTC,         's', 1,          HAS_CLEARED,   NAV_DTC_CLEARED, 0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_DTC,         's', 1,          HAS_FAILED,    NAV_DTC_FAILED,  0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_DTC,         's', 2,          HAS_OLD,       NAV_DTC_OLD,     0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_DTC,         's', 3,          ALWAYS,        NAV_MENU,        0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_DTC,         'l', ANY,        ALWAYS,        NAV_MENU,        0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},

	{NAV_DTC_BUSY,    'l', ANY,        ALWAYS,        NAV_PAGES,       0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},

	{NAV_DTC_LIST,    's', BEHIND,     CAN_READ,      NAV_DTC_BUSY,    0,         NAV_DO_READ,           NAV_DO_NOTHING},
	{NAV_DTC_LIST,    's', BEHIND - 1, CAN_CLEAR,     NAV_DTC_CONFIRM, 0,         NAV_DO_HOLD_OPEN,      NAV_DO_NOTHING},
	{NAV_DTC_LIST,    's', BEHIND - 2, ALWAYS,        NAV_DTC,         0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_DTC_LIST,    'l', ANY,        ALWAYS,        NAV_DTC,         0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},

	{NAV_DTC_CONFIRM, 's', 0,          ALWAYS,        NAV_DTC_LIST,    CLEAR_ROW, NAV_DO_HOLD_CLOSE,     NAV_DO_NOTHING},

	{NAV_DTC_CLEARED, 's', BEHIND,     ALWAYS,        NAV_DTC,         0,         NAV_DO_DISMISS,        NAV_DO_NOTHING},
	{NAV_DTC_CLEARED, 'l', ANY,        ALWAYS,        NAV_DTC,         0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},

	{NAV_DTC_FAILED,  's', ANY,        ALWAYS,        NAV_DTC,         0,         NAV_DO_DISMISS,        NAV_DO_NOTHING},
	{NAV_DTC_FAILED,  'l', ANY,        ALWAYS,        NAV_DTC,         0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},

	{NAV_DTC_OLD,     's', BEHIND,     ALWAYS,        NAV_DTC,         0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_DTC_OLD,     'l', ANY,        ALWAYS,        NAV_DTC,         0,         NAV_DO_NOTHING,        NAV_DO_NOTHING},

	{NAV_BRIGHTNESS,  's', ANY,        ALWAYS,        NAV_MENU,        1,         NAV_DO_SETTINGS_STORE, NAV_DO_NOTHING},
	{NAV_BRIGHTNESS,  'l', ANY,        ALWAYS,        NAV_MENU,        1,         NAV_DO_SETTINGS_STORE, NAV_DO_NOTHING},

	{NAV_WEB,         's', 0,          RELEASED,      STAY,            0,         NAV_DO_RELEASE_OFF,    NAV_DO_NOTHING},
	{NAV_WEB,         's', 0,          NOT_RELEASED,  STAY,            0,         NAV_DO_RELEASE_ON,     NAV_DO_NOTHING},
	{NAV_WEB,         's', 1,          ALWAYS,        NAV_MENU,        3,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_WEB,         'l', ANY,        ALWAYS,        NAV_MENU,        3,         NAV_DO_NOTHING,        NAV_DO_NOTHING},

	{NAV_INFO,        's', ANY,        ALWAYS,        NAV_MENU,        4,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_INFO,        'l', ANY,        ALWAYS,        NAV_MENU,        4,         NAV_DO_NOTHING,        NAV_DO_NOTHING},

	{NAV_SETTINGS,    's', 0,          ALWAYS,        STAY,            0,         NAV_DO_REVERSE_TOGGLE, NAV_DO_NOTHING},
	{NAV_SETTINGS,    's', 1,          AP_NOT_KEPT,   STAY,            0,         NAV_DO_AP_TOGGLE,      NAV_DO_NOTHING},
	{NAV_SETTINGS,    's', 2,          NOT_UNDER_WAY, NAV_CONFIRM,     0,         NAV_DO_NOTHING,        NAV_DO_REBOOT},
	{NAV_SETTINGS,    's', 3,          PREVIOUS_AND_NOT_UNDER_WAY, NAV_CONFIRM, 0, NAV_DO_NOTHING,       NAV_DO_PREVIOUS_FIRMWARE},
	{NAV_SETTINGS,    's', 4,          NOT_UNDER_WAY, NAV_CONFIRM,     0,         NAV_DO_NOTHING,        NAV_DO_FACTORY_RESET},
	{NAV_SETTINGS,    's', 5,          ALWAYS,        NAV_MENU,        5,         NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_SETTINGS,    'l', ANY,        ALWAYS,        NAV_MENU,        5,         NAV_DO_NOTHING,        NAV_DO_NOTHING},

	{NAV_CONFIRM,     's', 0,          ALWAYS,        NAV_SETTINGS,    CAME_FROM, NAV_DO_NOTHING,        NAV_DO_NOTHING},
	{NAV_CONFIRM,     's', 1,          ALWAYS,        NAV_PAGES,       0,         ASKED,                 NAV_DO_NOTHING},
	{NAV_CONFIRM,     'l', ANY,        ALWAYS,        NAV_SETTINGS,    CAME_FROM, NAV_DO_NOTHING,        NAV_DO_NOTHING},
};

static bool model_under_way(const seen_t *seen)
{
	return seen->flow == DTC_FLOW_READ_SENT || seen->flow == DTC_FLOW_READING || seen->flow == DTC_FLOW_CLEAR_SENT || seen->flow == DTC_FLOW_CLEARING;
}

static bool model_when(const seen_t *seen, when_t when)
{
	switch(when)
	{
		case ALWAYS:        return true;
		case CAN_READ:      return seen->can_read;
		case CAN_CLEAR:     return seen->can_clear;
		case UNDER_WAY:     return model_under_way(seen);
		case NOT_UNDER_WAY: return !model_under_way(seen);
		case HAS_LIST:      return seen->flow == DTC_FLOW_LIST;
		case HAS_CLEARED:   return seen->flow == DTC_FLOW_CLEARED;
		case HAS_FAILED:    return seen->flow == DTC_FLOW_FAILED || seen->flow == DTC_FLOW_UNKNOWN;
		case HAS_OLD:       return seen->old > 0;
		case RELEASED:      return seen->release_open;
		case NOT_RELEASED:  return !seen->release_open;
		case PREVIOUS_AND_NOT_UNDER_WAY: return seen->previous_firmware && !model_under_way(seen);
		case AP_NOT_KEPT:   return !seen->ap_kept;
	}
	return false;
}

static int model_lines(nav_screen_t screen, const seen_t *seen)
{
	if(screen == NAV_DTC_LIST) return seen->list;
	if(screen == NAV_DTC_CLEARED) return seen->cleared;
	if(screen == NAV_DTC_OLD) return seen->old;
	if(screen == NAV_INFO) return seen->info;
	return 0;
}

static int model_rows(nav_screen_t screen, const seen_t *seen);

// Whether a short press on that row of the screen does something: a rule of the table fits it. Every rule of
// a short press leads somewhere or names an action.
static bool model_acts(nav_screen_t screen, int at_row, const seen_t *seen)
{
	int lines = model_lines(screen, seen);

	if(at_row < 0 || at_row >= model_rows(screen, seen)) return false;
	for(size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); i++)
	{
		const rule_t *rule = &rules[i];
		int row = rule->row <= BEHIND ? lines + (BEHIND - rule->row) : rule->row;

		if(rule->screen != screen || rule->input != 's') continue;
		if(rule->row != ANY && row != at_row) continue;
		if(model_when(seen, rule->when)) return true;
	}
	return false;
}

static int model_rows(nav_screen_t screen, const seen_t *seen)
{
	//                                  pages menu dtc busy list dialog cleared failed old brightness web info settings confirm
	static const int behind[SCREENS] = {0,    7,   4,  0,   3,   2,     1,      0,     1,  0,         2,  0,   6,       2};

	return model_lines(screen, seen) + behind[screen];
}

static bool model_shown(const seen_t *seen, int page)
{
	return page >= 0 && page < LAYOUT_PAGES_MAX && (seen->shown >> page & 1) != 0;
}

static int model_first(const seen_t *seen)
{
	for(int page = 0; page < LAYOUT_PAGES_MAX; page++)
	{
		if(model_shown(seen, page)) return page;
	}
	return -1;
}

static int model_nearest(const seen_t *seen, int from)
{
	int best = -1, best_cost = 0;

	for(int page = 0; page < LAYOUT_PAGES_MAX; page++)
	{
		// Twice the distance, and one less for a page behind: it wins against one that is as far before
		int cost = 2 * abs(page - from) - (page > from ? 1 : 0);

		if(!model_shown(seen, page)) continue;
		if(best < 0 || cost < best_cost)
		{
			best = page;
			best_cost = cost;
		}
	}
	return best;
}

static void model_init(model_t *model, const seen_t *seen, uint64_t now_ms)
{
	memset(model, 0, sizeof(*model));
	model->screen = NAV_PAGES;
	model->page = model_first(seen);
	model->confirm = NAV_DO_NOTHING;
	model->latest = now_ms;
}

static void model_time(model_t *model, uint64_t now_ms, bool input)
{
	if(now_ms > model->latest)
	{
		model->idle += now_ms - model->latest;
		model->latest = now_ms;
	}
	if(input) model->idle = 0;
}

// A short ('s') or long ('l') press on the screen itself: the first rule of the table that fits
static nav_do_t model_press(model_t *model, const seen_t *seen, char input)
{
	int lines = model_lines(model->screen, seen);

	if(input == 'l' && model->screen == NAV_PAGES)
	{
		model->page = model_first(seen);
		return NAV_DO_NOTHING;
	}
	for(size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); i++)
	{
		const rule_t *rule = &rules[i];
		int row = rule->row <= BEHIND ? lines + (BEHIND - rule->row) : rule->row;
		nav_do_t action = rule->action == ASKED ? model->confirm : (nav_do_t)rule->action;

		if(rule->screen != model->screen || rule->input != input) continue;
		if(rule->row != ANY && row != model->row) continue;
		if(!model_when(seen, rule->when)) continue;

		if(rule->to != STAY)
		{
			int focus = rule->focus == CLEAR_ROW ? seen->list + 1 : rule->focus == CAME_FROM ? model->came_from : rule->focus;

			if(rule->asks != NAV_DO_NOTHING) model->came_from = model->row;
			model->screen = (nav_screen_t)rule->to;
			model->row = focus;
			model->confirm = rule->asks;
			if(model->screen == NAV_BRIGHTNESS) model->value = seen->brightness < 5 ? 5 : seen->brightness > 100 ? 100 : seen->brightness;
		}
		return action;
	}
	return NAV_DO_NOTHING;
}

static nav_do_t model_turn(model_t *model, const seen_t *seen, int detents)
{
	if(seen->over != NAV_OVER_NONE) return NAV_DO_NOTHING;

	if(model->screen == NAV_PAGES)
	{
		int step = detents < 0 ? -1 : 1;
		long long left = detents < 0 ? -(long long)detents : (long long)detents;

		if(!model_shown(seen, model->page))
		{
			model->page = model_nearest(seen, model->page);
			return NAV_DO_NOTHING;
		}
		// Along the pages until the detents are used up or the pages end
		for(int page = model->page + step; page >= 0 && page < LAYOUT_PAGES_MAX && left > 0; page += step)
		{
			if(!model_shown(seen, page)) continue;
			model->page = page;
			left--;
		}
		return NAV_DO_NOTHING;
	}
	if(model->screen == NAV_BRIGHTNESS)
	{
		long long value = model->value + 5ll * detents;

		model->value = value < 5 ? 5 : value > 100 ? 100 : (int)value;
		return NAV_DO_BRIGHTNESS;
	}
	else
	{
		long long row = (long long)model->row + detents;
		long long last = (long long)model_rows(model->screen, seen) - 1;

		if(row > last) row = last;
		if(row < 0) row = 0;
		model->row = (int)row;
	}
	return NAV_DO_NOTHING;
}

static nav_do_t model_short(model_t *model, const seen_t *seen)
{
	if(seen->over == NAV_OVER_UPLOAD) return NAV_DO_NOTHING;
	if(seen->over == NAV_OVER_ASK) return NAV_DO_ASK_CONFIRM;
	if(seen->over == NAV_OVER_UPDATE) return NAV_DO_UPDATE_OK;
	return model_press(model, seen, 's');
}

static nav_do_t model_long(model_t *model, const seen_t *seen)
{
	if(seen->over == NAV_OVER_ASK) return NAV_DO_ASK_REFUSE;
	if(seen->over != NAV_OVER_NONE) return NAV_DO_NOTHING;
	return model_press(model, seen, 'l');
}

// The rows of a screen a finger reaches, counted from its first one: of the two dialogs the first answer alone
static int model_touchable(nav_screen_t screen, const seen_t *seen)
{
	return screen == NAV_DTC_CONFIRM || screen == NAV_CONFIRM ? 1 : model_rows(screen, seen);
}

static nav_do_t model_tap(model_t *model, const seen_t *seen, int row)
{
	// The knob alone answers what lies over the screen
	if(seen->over != NAV_OVER_NONE) return NAV_DO_NOTHING;

	if(model->screen != NAV_DTC_FAILED)
	{
		if(row < 0 || row >= model_touchable(model->screen, seen)) return NAV_DO_NOTHING;
		model->row = row;
	}
	return model_short(model, seen);
}

static nav_do_t model_swipe(model_t *model, const seen_t *seen, int direction)
{
	if(model->screen != NAV_PAGES || direction == 0) return NAV_DO_NOTHING;
	return model_turn(model, seen, direction > 0 ? 1 : -1);
}

static nav_do_t model_hold(model_t *model, const seen_t *seen, hold_event_t event)
{
	if(model->screen != NAV_DTC_CONFIRM) return NAV_DO_NOTHING;
	if(event != HOLD_CONFIRMED && event != HOLD_CANCELLED && event != HOLD_STUCK) return NAV_DO_NOTHING;

	if(event == HOLD_CONFIRMED && seen->over == NAV_OVER_NONE)
	{
		model->screen = NAV_DTC_BUSY;
		model->row = 0;
		return NAV_DO_CLEAR;
	}
	model->screen = NAV_DTC_LIST;
	model->row = seen->list + 1;
	return NAV_DO_NOTHING;
}

// Nobody can see the screen: a dialog is left as by its first answer, whatever lies over it
static nav_do_t model_cancel(model_t *model, const seen_t *seen)
{
	if(model->screen == NAV_DTC_CONFIRM)
	{
		model->screen = NAV_DTC_LIST;
		model->row = seen->list + 1;
		return NAV_DO_HOLD_CLOSE;
	}
	if(model->screen == NAV_CONFIRM)
	{
		model->screen = NAV_SETTINGS;
		model->row = model->came_from;
		model->confirm = NAV_DO_NOTHING;
	}
	return NAV_DO_NOTHING;
}

#define TICK_RULES  6

// Which rules of nav_tick apply, all of them looked at before one is followed
static void model_tick_rules(const model_t *model, const seen_t *seen, bool applies[TICK_RULES])
{
	nav_screen_t screen = model->screen;
	int last = model_rows(screen, seen) - 1;

	applies[0] = screen == NAV_DTC_CONFIRM && !(seen->flow == DTC_FLOW_LIST && seen->can_clear);
	applies[1] = screen == NAV_DTC_BUSY && !model_under_way(seen);
	applies[2] = (screen == NAV_DTC_LIST && seen->flow != DTC_FLOW_LIST) || (screen == NAV_DTC_CLEARED && seen->flow != DTC_FLOW_CLEARED) ||
	             (screen == NAV_DTC_FAILED && seen->flow != DTC_FLOW_FAILED && seen->flow != DTC_FLOW_UNKNOWN) || (screen == NAV_DTC_OLD && seen->old == 0);
	applies[3] = screen == NAV_PAGES && !model_shown(seen, model->page);
	applies[4] = model->row > (last > 0 ? last : 0);
	applies[5] = model->idle >= IDLE && screen != NAV_PAGES && screen != NAV_DTC_BUSY && screen != NAV_DTC_CONFIRM && seen->over == NAV_OVER_NONE;
}

// Returns what is to be carried out; *followed is the rule that was followed, -1 if none applied
static nav_do_t model_tick(model_t *model, const seen_t *seen, int *followed)
{
	bool applies[TICK_RULES];
	nav_screen_t screen = model->screen;
	int last = model_rows(screen, seen) - 1;
	int rule = 0;

	model_tick_rules(model, seen, applies);
	while(rule < TICK_RULES && !applies[rule]) rule++;
	*followed = rule < TICK_RULES ? rule : -1;

	switch(rule)
	{
		case 0:
			model->screen = seen->flow == DTC_FLOW_LIST ? NAV_DTC_LIST : NAV_DTC;
			model->row = seen->flow == DTC_FLOW_LIST ? seen->list + 1 : 0;
			return NAV_DO_HOLD_CLOSE;
		case 1:
			if(seen->flow == DTC_FLOW_LIST) model->screen = NAV_DTC_LIST;
			else if(seen->flow == DTC_FLOW_CLEARED) model->screen = NAV_DTC_CLEARED;
			else if(seen->flow == DTC_FLOW_FAILED || seen->flow == DTC_FLOW_UNKNOWN) model->screen = NAV_DTC_FAILED;
			else model->screen = NAV_DTC;
			model->row = 0;
			break;
		case 2:
			model->screen = NAV_DTC;
			model->row = 0;
			break;
		case 3:
			model->page = model_nearest(seen, model->page);
			break;
		case 4:
			model->row = last > 0 ? last : 0;
			break;
		case 5:
			model->screen = NAV_PAGES;
			model->row = 0;
			model->confirm = NAV_DO_NOTHING;
			return screen == NAV_BRIGHTNESS ? NAV_DO_SETTINGS_STORE : NAV_DO_NOTHING;
	}
	return NAV_DO_NOTHING;
}

/*
 * The walk: inputs, ticks and changes of the world in a random order, module and model compared after
 * every call, and next to that what must hold whatever the model says.
 */
#define WALKS       48
#define WALK_STEPS  40000

typedef enum
{
	CALL_TICK, CALL_TURN, CALL_SHORT, CALL_LONG, CALL_TAP, CALL_SWIPE, CALL_HOLD, CALL_INIT, CALL_CANCEL, CALLS,
} call_t;

static const char *const call_names[CALLS] = {"tick", "turn", "short", "long", "tap", "swipe", "hold", "init", "cancel"};

static struct
{
	// What the walk reached
	long calls[CALLS];
	long screens[SCREENS];              // calls that ended on the screen
	long actions[ACTIONS];              // calls that returned the action
	long followed[TICK_RULES];          // ticks that followed the rule
	long both[TICK_RULES][TICK_RULES];  // ticks at which the two rules applied at once
	long under[4];                      // inputs under each overlay (0: none)
	long steps_back, ends_of_idle, beyond, searches, no_page, huge_lists, strange_phases;
	// What must not happen
	long differences, clears, reads, dialogs, dangerous, touched, overlays, rows, values, trapped, switched;
	// What the walk reached of the rules for the dialogs
	long taps_ignored[2];               // taps on the second answer of the clear dialog and of the dialog of the settings
	long taps_under[4];                 // taps under each overlay (0: none)
	long touch_confirms;                // taps that returned what only the knob may: a confirmed question, update, clear, restart, firmware, reset
	long cancelled[2];                  // clear dialogs and dialogs of the settings that a cancel left
	long asks_refused;                  // short presses and taps on a row of the settings that asks first, while a request is under way
	// Rows asked whether a press on them would do something
	long acts_wrong;                    // answers other than the table gives
	long acts_asked[3];                 // rows that act, rows that do not, rows that do not exist
	long acts_refused;                  // rows of the settings that ask first and do not act because a request is under way
	// The own access point
	long ap_toggles[2];                 // short presses and taps on Hotspot that switched it
	long ap_refused[2];                 // short presses and taps on Hotspot while it is kept on
	long ap_kept_calls;                 // calls with the access point kept on
	long acts_kept;                     // times the row Hotspot was asked whether it acts while the access point is kept on
} walked;

static uint32_t walk_state;

/*
 * Never two rolls in one expression whose order C leaves open, as on both sides of an assignment: which side
 * is worked out first is the choice of the compiler, and a walk that depends on it would be another walk
 * with another one. Where an assignment needs two, its right side is rolled before it - the order gcc and
 * clang both had.
 */
static uint32_t walk_random(uint32_t below)
{
	walk_state = walk_state * 1664525u + 1013904223u;
	return (walk_state >> 8) % below;
}

static int walk_layout, walk_catalog;

// The world as the model sees it
static void see(seen_t *seen)
{
	static const int *const lines[4] = {&world.list_lines, &world.cleared_lines, &world.old_lines, &world.info_lines};
	int *const counted[4] = {&seen->list, &seen->cleared, &seen->old, &seen->info};

	seen->shown = shown_pages[walk_layout][walk_catalog];
	seen->flow = world.flow;
	seen->can_read = world.can_read;
	seen->can_clear = world.can_clear;
	seen->release_open = world.release_open;
	seen->previous_firmware = world.previous_firmware;
	seen->ap_kept = world.ap_kept;
	seen->brightness = world.brightness;
	seen->over = world.uploading ? NAV_OVER_UPLOAD : world.asking != ACCESS_ASK_NONE ? NAV_OVER_ASK : world.update_pending ? NAV_OVER_UPDATE : NAV_OVER_NONE;
	for(int i = 0; i < 4; i++)
	{
		*counted[i] = *lines[i] < 0 ? 0 : *lines[i] > LINES_MOST ? LINES_MOST : *lines[i];
	}
}

static void change_world(void)
{
	static const int lines[] = {0, 0, 1, 1, 2, 3, 3, 4, 5, 8, 13, 220, -1, -9, INT_MIN, INT_MAX, INT_MAX - 3, INT_MAX - 4};
	static const int brightness[] = {80, 25, 5, 100, 4, 101, 0, -3, 37, 255, INT_MAX, INT_MIN};
	int *const counts[4] = {&world.list_lines, &world.cleared_lines, &world.old_lines, &world.info_lines};
	int count;

	switch(walk_random(21))
	{
		case 0:
		case 1:
			world.flow = (dtc_flow_phase_t)walk_random(PHASES);
			break;
		case 2:
			// No member of the enum
			world.flow = walk_random(4) == 0 ? (dtc_flow_phase_t)(PHASES + walk_random(3)) : DTC_FLOW_LIST;
			break;
		case 3:
			world.can_read = !world.can_read;
			break;
		case 4:
			world.can_clear = !world.can_clear;
			break;
		case 5:
		case 6:
		case 7:
			count = lines[walk_random(sizeof(lines) / sizeof(lines[0]))];
			*counts[walk_random(4)] = count;
			break;
		case 8:
			// 4: no member of the enum
			world.asking = (access_ask_t)walk_random(5);
			break;
		case 9:
			world.update_pending = !world.update_pending;
			break;
		case 10:
			world.uploading = !world.uploading;
			break;
		case 11:
		case 12:
			walk_layout = (int)walk_random(LAYOUTS);
			world.layout = layouts[walk_layout];
			break;
		case 13:
		case 14:
			walk_catalog = (int)walk_random(CATALOGS);
			world.catalog = catalogs[walk_catalog];
			break;
		case 15:
			world.brightness = brightness[walk_random(sizeof(brightness) / sizeof(brightness[0]))];
			break;
		case 16:
			world.previous_firmware = !world.previous_firmware;
			break;
		case 17:
			world.release_open = !world.release_open;
			break;
		case 18:
			world.night_mode = !world.night_mode;
			break;
		case 19:
			// Safe mode begins or ends with a start; the last stored network goes and comes at any time
			world.ap_kept = !world.ap_kept;
			break;
		default:
			// A list of the kind the display really shows
			world.flow = DTC_FLOW_LIST;
			world.list_lines = 1 + (int)walk_random(9);
			world.can_clear = true;
			break;
	}
}

// A change that concerns the screen shown: its list becomes shorter than the focus is far, the flow moves
// on, the clear is forbidden or allowed
static void disturb(void)
{
	int *lines = nav.screen == NAV_DTC_LIST ? &world.list_lines : nav.screen == NAV_DTC_CLEARED ? &world.cleared_lines :
	             nav.screen == NAV_DTC_OLD ? &world.old_lines : &world.info_lines;
	uint32_t how = walk_random(6);

	if(how == 0 || how >= 3) *lines = nav.row > 0 ? (int)walk_random(nav.row > 12 ? 12 : (uint32_t)nav.row) : 0;
	if(how == 1 || how == 3) world.flow = (dtc_flow_phase_t)walk_random(PHASES);
	if(how == 2) world.can_clear = !world.can_clear;
}

// What the display would do with the action, and what the adapter does meanwhile
static void carry_out(nav_do_t action)
{
	switch(action)
	{
		case NAV_DO_READ:
			world.flow = DTC_FLOW_READ_SENT;
			world.can_read = world.can_clear = false;
			break;
		case NAV_DO_CLEAR:
			world.flow = DTC_FLOW_CLEAR_SENT;
			world.can_read = world.can_clear = false;
			break;
		case NAV_DO_DISMISS:
			world.flow = DTC_FLOW_IDLE;
			break;
		case NAV_DO_ASK_CONFIRM:
		case NAV_DO_ASK_REFUSE:
			world.asking = ACCESS_ASK_NONE;
			break;
		case NAV_DO_UPDATE_OK:
			world.update_pending = false;
			break;
		case NAV_DO_RELEASE_ON:
			world.release_open = true;
			break;
		case NAV_DO_RELEASE_OFF:
			world.release_open = false;
			break;
		case NAV_DO_BRIGHTNESS:
			world.brightness = nav.value;
			break;
		case NAV_DO_NIGHT_TOGGLE:
			world.night_mode = !world.night_mode;
			break;
		default:
			break;
	}

	if(walk_random(12) != 0) return;
	switch(world.flow)
	{
		case DTC_FLOW_READ_SENT:
			world.flow = DTC_FLOW_READING;
			break;
		case DTC_FLOW_READING:
			world.flow = walk_random(5) == 0 ? DTC_FLOW_FAILED : DTC_FLOW_LIST;
			world.list_lines = 1 + (int)walk_random(9);
			world.can_read = true;
			world.can_clear = walk_random(4) != 0;
			break;
		case DTC_FLOW_CLEAR_SENT:
			world.flow = DTC_FLOW_CLEARING;
			break;
		case DTC_FLOW_CLEARING:
			world.flow = walk_random(5) == 0 ? DTC_FLOW_UNKNOWN : DTC_FLOW_CLEARED;
			world.cleared_lines = 1 + (int)walk_random(5);
			world.can_read = true;
			break;
		default:
			break;
	}
}

// Is there a way to the value pages with the knob alone, as the world is, once nothing lies over the
// screen? Tried is every sequence of up to 6 inputs out of: long press, short press, a detent back, a
// detent forwards.
static bool way_back(const nav_t *from, uint64_t now_ms)
{
	static nav_t reached[512];
	nav_world_t free_world = world;
	int count = 1, level_end = 1, depth = 0;

	free_world.uploading = false;
	free_world.asking = ACCESS_ASK_NONE;
	free_world.update_pending = false;
	if(from->screen == NAV_PAGES) return true;

	reached[0] = *from;
	for(int done = 0; done < count && depth < 6; done++)
	{
		for(int input = 0; input < 4; input++)
		{
			nav_t next = reached[done];
			bool known = false;

			if(input == 0) nav_long(&next, &free_world, now_ms);
			else if(input == 1) nav_short(&next, &free_world, now_ms);
			else nav_turn(&next, input == 2 ? -1 : 1, &free_world, now_ms);

			if(next.screen == NAV_PAGES) return true;
			for(int i = 0; i < count && !known; i++)
			{
				known = reached[i].screen == next.screen && reached[i].row == next.row && reached[i].confirm == next.confirm;
			}
			if(!known && count < (int)(sizeof(reached) / sizeof(reached[0]))) reached[count++] = next;
		}
		if(done + 1 == level_end)
		{
			depth++;
			level_end = count;
		}
	}
	return false;
}

static void walk(uint32_t seed)
{
	static const int detents[] = {1, 1, 1, 1, -1, -1, -1, 2, -2, 3, -4, 0, 12, -12, 100, -100, INT_MAX, INT_MIN};
	static const int directions[] = {1, 1, -1, -1, 0, 5, -7, INT_MAX, INT_MIN};
	static const hold_event_t events[] = {HOLD_WAITING, HOLD_PROGRESS, HOLD_CONFIRMED, HOLD_CONFIRMED, HOLD_CANCELLED, HOLD_STUCK, NO_EVENT};
	model_t model;
	seen_t seen;
	uint64_t at_ms = 1000 + seed;

	walk_state = seed;
	quiet_world();
	walk_layout = 0;
	walk_catalog = 3;
	see(&seen);
	nav_init(&nav, &world, at_ms);
	model_init(&model, &seen, at_ms);

	for(long step = 0; step < WALK_STEPS; step++)
	{
		uint32_t pace = walk_random(100);
		uint32_t what = walk_random(100);
		call_t call = CALL_TICK;
		nav_do_t action = NAV_DO_NOTHING, expected = NAV_DO_NOTHING;
		hold_event_t event = HOLD_WAITING;
		int given = 0, followed = -1, rows, last, pressed_row;
		bool applies[TICK_RULES] = {false, false, false, false, false, false};
		bool input, same, entered, left, dangerous, beyond, under_way, in_dialog;
		bool end_of_idle = false;
		nav_t before;

		// The time: mostly the pace of the ticks, sometimes right to the end of the idle time, sometimes far
		// ahead, sometimes backwards
		if(pace < 55) at_ms += 200;
		else if(pace < 70) at_ms += walk_random(400);
		else if(pace < 78)
		{
			uint64_t left_ms = model.idle < IDLE ? IDLE - model.idle : 0;

			at_ms = model.latest + left_ms + walk_random(3);
			if(at_ms > 0) at_ms--;
			end_of_idle = true;
			walked.ends_of_idle++;
		}
		else if(pace < 86) at_ms += walk_random(150000);
		else if(pace < 94)
		{
			uint64_t back = walk_random(130000);

			at_ms = at_ms > back ? at_ms - back : 0;
			walked.steps_back++;
		}
		else if(pace < 98) at_ms = model.latest;
		else at_ms += 3600000ull * (1 + walk_random(48));

		// The world: now and then something changes, and what lies over the screen goes away again
		if(walk_random(100) < 9) change_world();
		if(walk_random(100) < (end_of_idle ? 50u : 3u))
		{
			// Mostly with a tick right behind it, so that two rules of the tick meet
			disturb();
			if(walk_random(3) != 0) what = 0;
		}
		if(walk_random(4) == 0) world.uploading = false;
		if(walk_random(6) == 0) world.asking = ACCESS_ASK_NONE;
		if(walk_random(6) == 0) world.update_pending = false;
		// In the clear dialog the hold reports more often than elsewhere
		if(nav.screen == NAV_DTC_CONFIRM && what >= 28 && walk_random(3) == 0) what = 95;
		// ... and in both dialogs somebody finds that nobody can see them
		if((nav.screen == NAV_DTC_CONFIRM || nav.screen == NAV_CONFIRM) && what >= 28 && walk_random(30) == 0) what = 98;
		see(&seen);
		rows = model_rows(model.screen, &seen);
		before = nav;

		if(what < 28)
		{
			call = CALL_TICK;
			model_tick_rules(&model, &seen, applies);
			model_time(&model, at_ms, false);
			action = nav_tick(&nav, &world, at_ms);
			expected = model_tick(&model, &seen, &followed);
		}
		else if(what < 50)
		{
			call = CALL_TURN;
			given = detents[walk_random(sizeof(detents) / sizeof(detents[0]))];
			model_time(&model, at_ms, true);
			action = nav_turn(&nav, given, &world, at_ms);
			expected = model_turn(&model, &seen, given);
		}
		else if(what < 68)
		{
			call = CALL_SHORT;
			model_time(&model, at_ms, true);
			action = nav_short(&nav, &world, at_ms);
			expected = model_short(&model, &seen);
		}
		else if(what < 73)
		{
			call = CALL_LONG;
			model_time(&model, at_ms, true);
			action = nav_long(&nav, &world, at_ms);
			expected = model_long(&model, &seen);
		}
		else if(what < 87)
		{
			uint32_t where = walk_random(12);

			// Mostly a row that exists, and of a long list one of its last
			call = CALL_TAP;
			if(where < 5) given = rows > 0 ? (int)walk_random(rows > 12 ? 12 : (uint32_t)rows) : 0;
			else if(where < 8) given = rows - 1 - (int)walk_random(4);
			else if(where == 8) given = rows;
			else if(where == 9) given = -1;
			else if(where == 10) given = INT_MAX;
			else given = INT_MIN;
			model_time(&model, at_ms, true);
			action = nav_tap(&nav, given, &world, at_ms);
			expected = model_tap(&model, &seen, given);
		}
		else if(what < 91)
		{
			call = CALL_SWIPE;
			given = directions[walk_random(sizeof(directions) / sizeof(directions[0]))];
			model_time(&model, at_ms, true);
			action = nav_swipe(&nav, given, &world, at_ms);
			expected = model_swipe(&model, &seen, given);
		}
		else if(what == 98)
		{
			call = CALL_CANCEL;
			model_time(&model, at_ms, false);
			action = nav_cancel(&nav, &world, at_ms);
			expected = model_cancel(&model, &seen);
		}
		else if(what < 99 || walk_random(30) != 0)
		{
			call = CALL_HOLD;
			event = events[walk_random(sizeof(events) / sizeof(events[0]))];
			model_time(&model, at_ms, false);
			action = nav_hold(&nav, event, &world, at_ms);
			expected = model_hold(&model, &seen, event);
		}
		else
		{
			call = CALL_INIT;
			nav_init(&nav, &world, at_ms);
			model_init(&model, &seen, at_ms);
		}
		input = call == CALL_TURN || call == CALL_SHORT || call == CALL_LONG || call == CALL_TAP || call == CALL_SWIPE;

		same = action == expected && nav.screen == model.screen && nav.page == model.page && nav.row == model.row && nav.value == model.value &&
		       nav.confirm == model.confirm && nav.clock_ms == model.latest && nav.clock_ms - nav.last_input_ms == model.idle &&
		       nav_rows(&nav, &world) == model_rows(model.screen, &seen) && nav_overlay(&world) == seen.over;
		if(!same)
		{
			printf("  walk %lu, step %ld, %s %d at %llu ms from screen %s row %d page %d, flow %d, overlay %d:\n"
			       "    module: action %d, screen %d, page %d, row %d, value %d, confirm %d, rows %d, clock %llu, idle %llu\n"
			       "    model:  action %d, screen %d, page %d, row %d, value %d, confirm %d, rows %d, clock %llu, idle %llu\n",
			       (unsigned long)seed, step, call_names[call], call == CALL_HOLD ? (int)event : given, (unsigned long long)at_ms,
			       screen_names[before.screen], before.row, before.page, (int)world.flow, (int)seen.over,
			       (int)action, (int)nav.screen, nav.page, nav.row, nav.value, (int)nav.confirm, nav_rows(&nav, &world),
			       (unsigned long long)nav.clock_ms, (unsigned long long)(nav.clock_ms - nav.last_input_ms),
			       (int)expected, (int)model.screen, model.page, model.row, model.value, (int)model.confirm, model_rows(model.screen, &seen),
			       (unsigned long long)model.latest, (unsigned long long)model.idle);
			walked.differences++;
			return;
		}

		// Which rows act, by the table: the row in focus, the ends of the screen, the rows around the end of the
		// lines of a list, and what lies beside the rows
		{
			int lines = model_lines(nav.screen, &seen);
			int all = model_rows(nav.screen, &seen);
			const int asked[] = {nav.row, 0, 1, 2, 3, 4, 5, 6, lines - 1, lines, lines + 1, lines + 2, all - 1, all, -1};

			for(size_t i = 0; i < sizeof(asked) / sizeof(asked[0]); i++)
			{
				bool expected_acts = model_acts(nav.screen, asked[i], &seen);

				if(nav_row_acts(&nav, asked[i], &world) != expected_acts)
				{
					if(walked.acts_wrong == 0) printf("  walk %lu, step %ld, screen %s, flow %d: row %d of %d acts %d by the table\n", (unsigned long)seed, step,
					                                  screen_names[nav.screen], (int)world.flow, asked[i], all, expected_acts);
					walked.acts_wrong++;
				}
				walked.acts_asked[asked[i] < 0 || asked[i] >= all ? 2 : expected_acts ? 0 : 1]++;
				if(nav.screen == NAV_SETTINGS && asked[i] >= 2 && asked[i] <= 4 && model_under_way(&seen) && (asked[i] != 3 || seen.previous_firmware)) walked.acts_refused++;
				if(nav.screen == NAV_SETTINGS && asked[i] == 1 && world.ap_kept) walked.acts_kept++;
			}
		}

		// What must hold whatever the model says. From here on only the module, the world and the call count.
		entered = nav.screen == NAV_DTC_CONFIRM && before.screen != NAV_DTC_CONFIRM;
		left = before.screen == NAV_DTC_CONFIRM && nav.screen != NAV_DTC_CONFIRM;
		dangerous = action == NAV_DO_REBOOT || action == NAV_DO_PREVIOUS_FIRMWARE || action == NAV_DO_FACTORY_RESET;
		// The row a short press or a tap acted on, -1 if the call was neither
		pressed_row = call == CALL_SHORT ? before.row : call == CALL_TAP && given >= 0 ? given : -1;
		last = model_rows(nav.screen, &seen) - 1;
		if(last < 0) last = 0;

		under_way = world.flow == DTC_FLOW_READ_SENT || world.flow == DTC_FLOW_READING || world.flow == DTC_FLOW_CLEAR_SENT || world.flow == DTC_FLOW_CLEARING;
		in_dialog = before.screen == NAV_DTC_CONFIRM || before.screen == NAV_CONFIRM;

		// Clearing: asked for only by a confirmed hold in the dialog with nothing lying over it
		if(action == NAV_DO_CLEAR && !(call == CALL_HOLD && event == HOLD_CONFIRMED && before.screen == NAV_DTC_CONFIRM && seen.over == NAV_OVER_NONE &&
		                               nav.screen == NAV_DTC_BUSY)) walked.clears++;
		// Reading: asked for only from the fault memory and from its list, while it is allowed. So a request
		// begins on one of three screens, and the progress follows: none begins below the dialog of the settings.
		if(action == NAV_DO_READ && !((before.screen == NAV_DTC || before.screen == NAV_DTC_LIST) && pressed_row >= 0 && world.can_read &&
		                              seen.over == NAV_OVER_NONE && nav.screen == NAV_DTC_BUSY)) walked.reads++;

		// The dialog: entered only from the list by a short press or tap on Fehler löschen while clearing is
		// allowed, and never left without the hold dialog being closed
		if(entered != (action == NAV_DO_HOLD_OPEN)) walked.dialogs++;
		if(entered && !(before.screen == NAV_DTC_LIST && world.can_clear && pressed_row == seen.list + 1 && seen.over == NAV_OVER_NONE)) walked.dialogs++;
		if(left && call != CALL_INIT && action != NAV_DO_HOLD_CLOSE &&
		   !(call == CALL_HOLD && (event == HOLD_CONFIRMED || event == HOLD_CANCELLED || event == HOLD_STUCK))) walked.dialogs++;
		if(action == NAV_DO_HOLD_CLOSE && !left) walked.dialogs++;
		// A cancel leaves both dialogs, whatever lies over them, and nothing else
		if(call == CALL_CANCEL)
		{
			if(in_dialog ? nav.screen == before.screen : !stays(&before)) walked.dialogs++;
			if(nav.last_input_ms != before.last_input_ms) walked.dialogs++;
			if(in_dialog) walked.cancelled[before.screen == NAV_CONFIRM]++;
		}

		// Restart, previous firmware and factory reset: only from a short press of the knob on Ausführen of their
		// dialog, never from a tap; and the dialog only from its row of the settings, while no request is under way
		if(dangerous && !(before.screen == NAV_CONFIRM && call == CALL_SHORT && before.row == 1 && seen.over == NAV_OVER_NONE && action == before.confirm &&
		                  nav.screen == NAV_PAGES)) walked.dangerous++;
		if(nav.screen == NAV_CONFIRM && before.screen != NAV_CONFIRM)
		{
			nav_do_t asked = pressed_row == 2 ? NAV_DO_REBOOT : pressed_row == 3 ? NAV_DO_PREVIOUS_FIRMWARE : NAV_DO_FACTORY_RESET;

			if(before.screen != NAV_SETTINGS || pressed_row < 2 || pressed_row > 4 || nav.confirm != asked || action != NAV_DO_NOTHING ||
			   seen.over != NAV_OVER_NONE || (pressed_row == 3 && !world.previous_firmware) || under_way) walked.dangerous++;
		}
		if((nav.screen == NAV_CONFIRM) != (nav.confirm != NAV_DO_NOTHING)) walked.dangerous++;
		if(before.screen == NAV_SETTINGS && pressed_row >= 2 && pressed_row <= 4 && under_way && seen.over == NAV_OVER_NONE)
		{
			if(nav.screen != NAV_SETTINGS || action != NAV_DO_NOTHING) walked.dangerous++;
			walked.asks_refused++;
		}

		// The own access point: switched only by a short press or a tap on Hotspot, with nothing lying over the
		// settings, and never while it stays on whatever is asked - then the row takes the focus and nothing else
		if(action == NAV_DO_AP_TOGGLE)
		{
			if(before.screen != NAV_SETTINGS || pressed_row != 1 || world.ap_kept || seen.over != NAV_OVER_NONE || !at(NAV_SETTINGS, 1)) walked.switched++;
			walked.ap_toggles[call == CALL_TAP]++;
		}
		if(before.screen == NAV_SETTINGS && pressed_row == 1 && world.ap_kept && seen.over == NAV_OVER_NONE)
		{
			if(action != NAV_DO_NOTHING || !at(NAV_SETTINGS, 1) || nav.confirm != NAV_DO_NOTHING || nav.last_input_ms != nav.clock_ms) walked.switched++;
			walked.ap_refused[call == CALL_TAP]++;
		}
		if(world.ap_kept) walked.ap_kept_calls++;

		// The focus of the two dialogs: moved by the knob alone, and a tap on the second answer changes nothing
		if(in_dialog && nav.screen == before.screen && nav.row != before.row && call != CALL_TURN) walked.touched++;
		if(in_dialog && call == CALL_TAP && given == 1 && seen.over == NAV_OVER_NONE)
		{
			if(action != NAV_DO_NOTHING || !stays(&before)) walked.touched++;
			walked.taps_ignored[before.screen == NAV_CONFIRM]++;
		}

		// Under an overlay an input changes nothing below it and returns only what the overlay names
		if(input && seen.over != NAV_OVER_NONE)
		{
			nav_do_t allowed = NAV_DO_NOTHING;

			// The knob alone answers: a tap returns nothing there
			if(seen.over == NAV_OVER_ASK && call == CALL_SHORT) allowed = NAV_DO_ASK_CONFIRM;
			if(seen.over == NAV_OVER_ASK && call == CALL_LONG) allowed = NAV_DO_ASK_REFUSE;
			if(seen.over == NAV_OVER_UPDATE && call == CALL_SHORT) allowed = NAV_DO_UPDATE_OK;
			if(action != allowed || !stays(&before)) walked.overlays++;
		}
		// No question is confirmed by a touch: not the one of the browser, not the update, not the clear, not
		// what the dialog of the settings asks
		if(call == CALL_TAP)
		{
			if(action == NAV_DO_ASK_CONFIRM || action == NAV_DO_UPDATE_OK || action == NAV_DO_CLEAR || dangerous) walked.touch_confirms++;
			walked.taps_under[seen.over]++;
		}
		if(seen.over != NAV_OVER_NONE && call == CALL_HOLD && action != NAV_DO_NOTHING) walked.overlays++;
		if(seen.over != NAV_OVER_NONE && call == CALL_TICK && action != NAV_DO_NOTHING && action != NAV_DO_HOLD_CLOSE) walked.overlays++;
		if((action == NAV_DO_ASK_CONFIRM || action == NAV_DO_ASK_REFUSE) && seen.over != NAV_OVER_ASK) walked.overlays++;
		if(action == NAV_DO_UPDATE_OK && seen.over != NAV_OVER_UPDATE) walked.overlays++;

		// The focus: never negative; beyond the last row only as long as nothing moved it since a list
		// became shorter, and never after a tick or a turn that reached the screen
		beyond = nav.row > last;
		if(nav.row < 0) walked.rows++;
		if(beyond && (call == CALL_TICK || (call == CALL_TURN && seen.over == NAV_OVER_NONE) || nav.screen != before.screen || nav.row != before.row)) walked.rows++;
		if(beyond) walked.beyond++;

		// The brightness that would be stored can be stored
		if((nav.screen == NAV_BRIGHTNESS || action == NAV_DO_SETTINGS_STORE || action == NAV_DO_BRIGHTNESS) && (nav.value < 5 || nav.value > 100)) walked.values++;
		if(action == NAV_DO_SETTINGS_STORE && !(before.screen == NAV_BRIGHTNESS && nav.screen != NAV_BRIGHTNESS)) walked.values++;

		// No screen without a way back
		if(step % 3 == 0)
		{
			walked.searches++;
			if(!way_back(&nav, at_ms)) walked.trapped++;
		}

		// What the walk reached
		walked.calls[call]++;
		walked.screens[nav.screen]++;
		if((int)action >= 0 && (int)action < ACTIONS) walked.actions[action]++;
		if(input) walked.under[seen.over]++;
		if(followed >= 0) walked.followed[followed]++;
		for(int first = 0; first < TICK_RULES; first++)
		{
			for(int second = first + 1; second < TICK_RULES; second++)
			{
				if(applies[first] && applies[second]) walked.both[first][second]++;
			}
		}
		if(nav.screen == NAV_PAGES && nav.page < 0) walked.no_page++;
		if(rows > 1000000) walked.huge_lists++;
		if((int)world.flow >= PHASES) walked.strange_phases++;

		carry_out(action);
	}
}

static void test_walk(void)
{
	bool reached = true;

	memset(&walked, 0, sizeof(walked));
	for(uint32_t seed = 1; seed <= WALKS; seed++) walk(seed * 7919u);

	printf("  calls:");
	for(int i = 0; i < CALLS; i++) printf(" %s %ld", call_names[i], walked.calls[i]);
	printf("\n  ended on:");
	for(int i = 0; i < SCREENS; i++) printf(" %s %ld", screen_names[i], walked.screens[i]);
	printf("\n  actions:");
	for(int i = 0; i < ACTIONS; i++) printf(" %ld", walked.actions[i]);
	printf("\n  tick rules followed:");
	for(int i = 0; i < TICK_RULES; i++) printf(" %ld", walked.followed[i]);
	printf("\n  two tick rules at once: 3+5 %ld, 3+6 %ld, 5+6 %ld\n", walked.both[2][4], walked.both[2][5], walked.both[4][5]);
	printf("  inputs under: nothing %ld, upload %ld, question %ld, update question %ld\n", walked.under[0], walked.under[1], walked.under[2], walked.under[3]);
	printf("  steps back %ld, ends of the idle time %ld, focus beyond the last row %ld, searches for a way back %ld, no page %ld, huge lists %ld, strange phases %ld\n",
	       walked.steps_back, walked.ends_of_idle, walked.beyond, walked.searches, walked.no_page, walked.huge_lists, walked.strange_phases);
	printf("  taps on the second answer: clear dialog %ld, dialog of the settings %ld; rows of the settings that ask first, pressed while a request is under way: %ld\n",
	       walked.taps_ignored[0], walked.taps_ignored[1], walked.asks_refused);
	printf("  taps under: nothing %ld, upload %ld, question %ld, update question %ld; dialogs left by a cancel: clear dialog %ld, dialog of the settings %ld\n",
	       walked.taps_under[0], walked.taps_under[1], walked.taps_under[2], walked.taps_under[3], walked.cancelled[0], walked.cancelled[1]);
	printf("  rows asked whether they act: %ld do, %ld do not, %ld do not exist; rows of the settings that do not because a request is under way: %ld\n",
	       walked.acts_asked[0], walked.acts_asked[1], walked.acts_asked[2], walked.acts_refused);
	printf("  the own access point: switched by %ld short presses and %ld taps on Hotspot, kept on under %ld short presses and %ld taps on it; kept on in %ld calls, "
	       "and the row asked %ld times whether it acts then\n",
	       walked.ap_toggles[0], walked.ap_toggles[1], walked.ap_refused[0], walked.ap_refused[1], walked.ap_kept_calls, walked.acts_kept);

	for(int i = 0; i < CALLS; i++) reached = reached && walked.calls[i] >= 200;
	for(int i = 0; i < SCREENS; i++) reached = reached && walked.screens[i] >= 1000;
	for(int i = 0; i < ACTIONS; i++) reached = reached && walked.actions[i] >= 60;
	for(int i = 0; i < TICK_RULES; i++) reached = reached && walked.followed[i] >= 40;
	for(int i = 0; i < 4; i++) reached = reached && walked.under[i] >= 2500;
	reached = reached && walked.both[2][4] >= 100 && walked.both[2][5] >= 25 && walked.both[4][5] >= 8;
	reached = reached && walked.steps_back >= 50000 && walked.ends_of_idle >= 50000 && walked.beyond >= 90 && walked.no_page >= 100000 &&
	          walked.huge_lists >= 2500 && walked.strange_phases >= 10000;
	reached = reached && walked.taps_ignored[0] >= 40 && walked.taps_ignored[1] >= 100 && walked.asks_refused >= 100;
	for(int i = 0; i < 4; i++) reached = reached && walked.taps_under[i] >= 500;
	reached = reached && walked.cancelled[0] >= 20 && walked.cancelled[1] >= 20;
	reached = reached && walked.acts_asked[0] >= 100000 && walked.acts_asked[1] >= 100000 && walked.acts_asked[2] >= 100000 && walked.acts_refused >= 1000;
	reached = reached && walked.ap_toggles[0] >= 200 && walked.ap_toggles[1] >= 100 && walked.ap_refused[0] >= 200 && walked.ap_refused[1] >= 100 &&
	          walked.ap_kept_calls >= 100000 && walked.acts_kept >= 10000;
	check(reached, "the walk reaches every screen, every action, every call, every rule of the tick and every pair of them that can apply at once, all overlays, "
	               "steps back of the time, the end of the idle time, lists that became shorter, no page at all, the largest lists and phases outside the enum, "
	               "taps on the second answer of both dialogs, taps under every overlay, both dialogs left by a cancel, the settings while a request is under way, "
	               "rows that act, do not act and do not exist, and short presses and taps on Hotspot with the access point kept on and not, in numbers");

	check(walked.differences == 0, "48 random walks of 40000 calls: screen, page, row, value, what waits, the times, the rows and the returned action are those of the model after every call");
	check(walked.clears == 0, "in the walk a clear is asked for only by a confirmed hold in the clear dialog with nothing lying over it, and the progress follows");
	check(walked.reads == 0, "in the walk a read is asked for only from the fault memory and from its list while reading is allowed, and the progress follows: "
	                         "no request begins while the dialog of the settings shows");
	check(walked.dialogs == 0, "in the walk the clear dialog is entered only from the list by a short press or tap on Fehler löschen while clearing is allowed, "
	                           "and is never left without the hold dialog being closed; a cancel leaves both dialogs under whatever lies over them, no other screen, and is no input");
	check(walked.dangerous == 0, "in the walk restart, previous firmware and factory reset are returned only from a short press of the knob on Ausführen of their dialog, "
	                             "which is entered only from their row of the settings and never while a request is under way");
	check(walked.switched == 0, "in the walk the own access point is switched only by a short press or a tap on Hotspot of the settings with nothing lying over them, "
	                            "and never while it is kept on: the row then takes the focus, counts as an input and does nothing else");
	check(walked.touched == 0, "in the walk the focus of the two dialogs is moved by the knob alone, and a tap on their second answer changes nothing");
	check(walked.touch_confirms == 0, "in the walk no tap ever returns what confirms a question: not the one of the browser, not the update, not the clear, not restart, previous firmware or factory reset");
	check(walked.overlays == 0, "in the walk an input under an overlay changes nothing below it and returns only what the overlay names; the hold returns nothing there, the tick at most the closing of the hold dialog");
	check(walked.rows == 0, "in the walk the focus is never negative and lies beyond the last row only while nothing moved it since a list became shorter, never after a tick or a turn");
	check(walked.values == 0, "in the walk the brightness being set stays within 5 and 100, and a store is asked for only when the brightness screen is left");
	check(walked.trapped == 0, "in the walk every screen has a way back to the value pages with at most 6 inputs of the knob");
	check(walked.acts_wrong == 0, "in the walk a row acts exactly when the table of the rules has a short press for it that applies: after every call, for the row in focus, "
	                              "the ends of the screen, the rows around the end of the lines and the rows beside the screen");
}

// What scene.h draws as enabled: a row acts exactly when a short press on it does something
static void test_acts(void)
{
	// In the order of `phases`: the own request left something to look at; it is sent or accepted and not ended
	static const bool outcome[PHASES + 1] = {false, false, false, true, false, false, true, true, true, false};
	static const bool under_way[PHASES + 1] = {false, true, true, false, true, true, false, false, false, false};
	nav_t before;
	int wrong = 0;

	open_menu(6);
	check(acts(2), "menu, Nachtmodus: a press there returns something to carry out and leads nowhere - the row acts");
	check(acts(3), "menu, Web-Zugriff: a press there carries nothing out but leads to another screen - the row acts");
	check(acts(0) && acts(1) && acts(4) && acts(5) && acts(6), "menu: every other row acts as well");
	before = nav;
	acts(1);
	check(stays(&before) && at(NAV_MENU, 6) && nav.last_input_ms == before.last_input_ms && nav.clock_ms == before.clock_ms,
	      "asking whether Helligkeit acts opens no brightness screen: the menu, its focus and the times are as they were");

	// The row asked for counts, not the one the knob is on
	open_dtc(3);
	check(acts(3) && !acts(0), "fault memory, the focus on Zurück, reading not allowed: Zurück acts, Lesen does not - the row that is asked for counts, not the one in focus");
	world.can_read = true;
	check(acts(0), "fault memory, reading allowed: Lesen acts");
	world.can_clear = true;
	world.can_read = false;
	check(!acts(0), "fault memory, clearing allowed but reading not: Lesen does not act");
	open_dtc(0);
	world.can_read = true;
	check(acts(0) && !acts(1) && !acts(2), "fault memory, the focus on Lesen, nothing read and nothing stored: Lesen acts, the two rows that lead to a list do not");
	for(int i = 0; i <= PHASES; i++)
	{
		open_dtc(0);
		world.flow = phases[i];
		if(acts(1) != outcome[i] || acts(2) || !acts(3)) wrong++;
		world.old_lines = 1;
		if(acts(1) != outcome[i] || !acts(2)) wrong++;
		world.old_lines = -1;
		if(acts(2)) wrong++;
	}
	check(wrong == 0, "fault memory in every phase of the flow: Liste ansehen acts while the request left a list, an outcome or a failure, "
	                  "Liste vor dem Löschen while a list is stored, Zurück always");

	open_list(3, 0);
	check(!acts(0) && !acts(1) && !acts(2), "list of three lines: a press on a line does nothing - no line acts");
	check(!acts(3) && !acts(4) && acts(5), "list, neither reading nor clearing allowed: Erneut lesen and Fehler löschen do not act, Zurück does");
	world.can_read = true;
	check(acts(3) && !acts(4), "list, reading allowed: Erneut lesen acts, Fehler löschen does not");
	world.can_read = false;
	world.can_clear = true;
	check(!acts(3) && acts(4), "list, clearing allowed: Fehler löschen acts, Erneut lesen does not");
	open_list(0, 0);
	world.can_read = true;
	check(acts(0) && !acts(1) && acts(2) && !acts(3), "list without lines: the rows are the three choices, Erneut lesen the first, and there is no fourth");

	open_cleared(2, 0);
	check(!acts(0) && !acts(1) && acts(2), "outcome of a clear with two lines: the lines do not act, Fertig does");
	open_old(2, 0);
	check(!acts(0) && !acts(1) && acts(2), "list before the last clear with two lines: the lines do not act, Zurück does");
	open_web(1);
	check(acts(0) && acts(1), "web access: the release acts whether it is given or not, and so does Zurück");
	world.release_open = true;
	check(acts(0), "web access with the release given: the row takes it back - it acts");
	open_info(4, 0);
	check(acts(0) && acts(3), "info: a press on any line leads back to the menu - every line acts");

	// A row that does not exist
	open_web(0);
	check(!acts(-1), "web access, row -1: no such row - it does not act, although a press with the focus anywhere but on the release leads back");
	check(!acts(2), "web access, row 2: no such row behind the two - it does not act");
	open_info(4, 0);
	check(!acts(4) && !acts(-1) && !acts(INT_MAX) && !acts(INT_MIN), "info of four lines: the rows 4, -1, the largest and the smallest number do not exist and do not act");
	start();
	turn(1);
	check(!acts(0), "value pages: a press opens the menu, but the pages have no rows - row 0 does not act");
	open_failed(DTC_FLOW_FAILED);
	check(!acts(0), "failure: a press acknowledges it, but the screen has no rows - row 0 does not act");
	open_brightness(60);
	check(!acts(0), "brightness: a press stores, but the screen has no rows - row 0 does not act");
	open_busy();
	check(!acts(0), "progress: no rows, and a press does nothing - row 0 does not act");

	// The two dialogs
	open_clear_dialog(3);
	check(acts(0) && !acts(1), "clear dialog: Abbrechen acts, Löschen does not - a press there is the beginning of the hold at most");
	open_ask(2);
	check(acts(0) && acts(1), "dialog of the settings: Abbrechen leads back, Ausführen returns the restart - both act");

	// The settings: what ends in a restart is not offered while the own request is under way
	wrong = 0;
	for(int i = 0; i <= PHASES; i++)
	{
		open_settings(0);
		world.flow = phases[i];
		if(!acts(0) || !acts(1) || !acts(5)) wrong++;
		if(acts(2) != !under_way[i] || acts(4) != !under_way[i] || acts(3)) wrong++;
		world.previous_firmware = true;
		if(acts(2) != !under_way[i] || acts(3) != !under_way[i] || acts(4) != !under_way[i]) wrong++;
		if(wrong != 0) printf("  flow %s: rows 2 to 4 act %d %d %d\n", phase_names[i], acts(2), acts(3), acts(4));
	}
	check(wrong == 0, "settings in every phase of the flow: Drehrichtung, Hotspot (its access point not kept on) and Zurück always act; Neustart and Werkseinstellungen "
	                  "unless the own request is under way (four phases); Vorherige Version unless it is, and only with a firmware in the other slot");
	open_settings(2);
	world.flow = DTC_FLOW_READING;
	before = nav;
	check(!acts(2) && press() == NAV_DO_NOTHING && stays(&before), "settings, Neustart while the own read runs: the row does not act, and the press on it does nothing");
	world.flow = DTC_FLOW_LIST;
	check(acts(2) && press() == NAV_DO_NOTHING && at(NAV_CONFIRM, 0), "settings, Neustart when the read has ended: the row acts, and the press on it opens the dialog");

	// The settings: an access point that stays on whatever is asked is not offered to be switched
	open_settings(1);
	world.ap_kept = true;
	before = nav;
	check(!acts(1) && press() == NAV_DO_NOTHING && stays(&before), "settings, Hotspot while the access point is kept on: the row does not act, and the press on it does nothing");
	world.ap_kept = false;
	check(acts(1) && press() == NAV_DO_AP_TOGGLE && stays(&before), "settings, Hotspot when the access point is not kept: the row acts, and the press on it toggles the access point");
	open_settings(5);
	world.ap_kept = true;
	check(!acts(1) && acts(0) && acts(2) && !acts(3) && acts(4) && acts(5),
	      "settings, the access point kept on, the focus on Zurück: of the rows that act otherwise Hotspot alone does not");
	wrong = 0;
	for(int i = 0; i <= PHASES; i++)
	{
		open_settings(0);
		world.flow = phases[i];
		world.ap_kept = true;
		world.previous_firmware = true;
		if(acts(1) || !acts(0) || !acts(5)) wrong++;
		if(acts(2) != !under_way[i] || acts(3) != !under_way[i] || acts(4) != !under_way[i]) wrong++;
		world.ap_kept = false;
		if(!acts(1)) wrong++;
	}
	check(wrong == 0, "settings in every phase of the flow: Hotspot does not act while the access point is kept on and acts when it is not, "
	                  "and the other rows act as without that");
	open_menu(0);
	world.ap_kept = true;
	check(acts(1), "menu, the access point kept on: row 1 there is Helligkeit and acts");
	open_web(0);
	world.ap_kept = true;
	check(acts(0) && acts(1), "web access, the access point kept on: both rows act - the release is not the access point");

	// What lies over the screen is not looked at: the screen below is drawn as without it
	for(int over = NAV_OVER_UPLOAD; over <= NAV_OVER_UPDATE; over++)
	{
		static const char *const texts[] = {
			"",
			"settings below an upload: the rows act as without it - Hotspot does, Vorherige Version without a firmware does not",
			"settings below a question of the browser: the rows act as without it",
			"settings below the update question: the rows act as without it",
		};

		open_settings(0);
		set_overlay((nav_overlay_t)over);
		check(nav_overlay(&world) == (nav_overlay_t)over && acts(1) && acts(2) && !acts(3) && acts(5), texts[over]);
	}
	wrong = 0;
	for(int over = NAV_OVER_UPLOAD; over <= NAV_OVER_UPDATE; over++)
	{
		open_settings(0);
		set_overlay((nav_overlay_t)over);
		world.ap_kept = true;
		if(nav_overlay(&world) != (nav_overlay_t)over || acts(1) || !acts(0) || !acts(2)) wrong++;
	}
	check(wrong == 0, "settings below an upload, a question and the update question, the access point kept on: Hotspot does not act, as without what lies over it");
}

// Runs a group of checks in a child process. The checks print as always; the parent learns whether one of
// them failed and whether the child came to its end.
static void in_child(void (*group)(void), unsigned seconds, const char *ended)
{
	int status = 0;
	pid_t child;

	fflush(stdout);
	child = fork();
	if(child == 0)
	{
		// A module that never returns ends the child here
		alarm(seconds);
		test_failures = 0;
		group();
		fflush(stdout);
		_exit(test_failures == 0 ? 0 : 10);
	}
	if(child < 0 || waitpid(child, &status, 0) != child) status = -1;
	// Its failed checks are printed already
	if(status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 10) test_failures++;
	check(status != -1 && WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 10), ended);
}

static void examples_of_the_screens(void)
{
	test_fixtures();
	test_init();
	test_overlay();
	test_rows();
	test_pages_turn();
	test_pages_inputs();
	test_menu();
	test_dtc();
	test_busy();
	test_list();
	test_clear_dialog();
	test_cleared();
	test_failed();
	test_old();
	test_brightness();
	test_web();
	test_info();
	test_settings();
	test_acts();
}

static void examples_of_overlays_and_time(void)
{
	test_under_overlays();
	test_no_tap_confirms();
	test_cancel();
	test_hold_under_overlays();
	test_idle();
	test_clock();
}

static void examples_of_the_tick(void)
{
	test_tick_dialog();
	test_tick_busy();
	test_tick_gone();
	test_tick_pages();
	test_tick_focus();
	test_tick_order();
	test_tick_under_overlays();
}

int main(void)
{
	make_views();
	in_child(examples_of_the_screens, 20, "the examples of the screens ran to their end: no crash, no call that never returns");
	in_child(examples_of_overlays_and_time, 20, "the examples of the overlays and of the time ran to their end: no crash, no call that never returns");
	in_child(examples_of_the_tick, 20, "the examples of the tick ran to their end: no crash, no call that never returns");
	in_child(test_walk, 60, "the walk ran to its end: no crash, no call that never returns");
	return test_end();
}
