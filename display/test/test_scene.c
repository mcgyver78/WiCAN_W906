/*
 * Host test for display/components/core/scene.c. Run "make test_scene && ./test_scene" in display/test.
 * redproof.py removes or weakens every rule once (mutations/scene.py) and expects this test to fail.
 *
 * Four kinds of checks:
 *   - screens: the inputs of one screen, built with the real modules where that is short and by hand where a
 *     state is hard to reach, and the scene the header gives for them, written down by hand in
 *     fixtures/scene_*.txt in the format of scene_dump() and compared byte for byte
 *   - single rules and their boundaries, field by field
 *   - what holds for every scene (sound()): every scene of this test is built twice, between guard bytes over
 *     memory filled with 0xAA and over zeros, and both have to be the same memory
 *   - made-up inputs of every kind, compared with the rules of the header written a second way (tables by
 *     screen for kind, title, note, lines, choices and overlay, a search for the window of a list, and the
 *     ring of a value page from the age of each of its values)
 * Every group runs in a child process: a crash or a hang of the module is then a failed check.
 *
 * The time of all scenes is NOW. The adapter is "a1b2c3d4e5f6", the views are fixtures/scene_layout.json,
 * the profile is fixtures/scene_car_config.json, the values are fixtures/scene_values.json, seen 500 ms ago.
 */
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#include "test.h"
#include "scene.h"

#define COUNT(a)    ((int)(sizeof(a) / sizeof((a)[0])))
#define FILL        0xAA
#define NOW         1000000u
#define ID          "a1b2c3d4e5f6"
#define TOOLS       "../../tools/w906/fixtures/"
#define GUARD       64

#define HINT        "ca. 35 s – Live-Werte pausieren"
#define SCAN_NOTE   "Live-Werte angehalten (Fehlerspeicher-Scan)"
#define NO_API_NOTE "WiCAN-Firmware ohne Display-API – nur Live-Werte"
#define SAFE_NOTE   "Sicherer Modus – eingebaute Ansichten"
#define HOT_NOTE    "Zu heiß – Anzeige gedimmt"
#define NO_PAGE     "Keine Ansicht mit verfügbaren Werten"

static json_token_t work[LAYOUT_TOKENS];
static char file_text[16384];
static char base_values[1024];
static char what[512];

// What the scenes are made from
static layout_t layout, hidden_layout, scratch;
static catalog_t catalog, unloaded;
static values_t values;
static wican_state_t adapter;
static conn_t conn;
static dtc_flow_t flow;
static hold_t hold;
static access_t gate;
static nav_t nav;
static nav_world_t world;
static scene_input_t input;

// The fault memory lists of the fixtures
static dtc_result_t result, result_before;
static dtc_line_t mixed_lines[DTC_VIEW_LINES_MAX], empty_lines[8], codes_lines[32], cleared_lines[32];
static int mixed_count, empty_count, codes_count, cleared_count;
static dtc_summary_t mixed_summary;

static const char *const INFO[] = {
	"WLAN: Werkstatt (-61 dBm)", "Adresse: 192.168.88.37", "WiCAN: a1b2c3d4e5f6", "Firmware: w906-display 0.1",
	"Ansichten: W906 OM651 Standard", "Speicher: 61000 frei", "Neustarts: 3",
};

// The scene under test lies between bytes the module is not told of
static struct
{
	unsigned char front[GUARD];
	scene_t scene;
	unsigned char behind[GUARD];
} box;
static const scene_t *const scene = &box.scene;
static scene_t zeroed;

// The same for the text of scene_dump()
static struct
{
	char front[GUARD];
	char text[SCENE_DUMP_SIZE + 16];
	char behind[GUARD];
} dump_box;
static char *const dumped = dump_box.text;

// Counted over all scenes of a group and checked at its end
static int built, unsound, unset, outside, setup_failures;

/* Helpers ---------------------------------------------------------------------------------------------- */

// Every byte is still the one the memory was filled with: the first is, and each is the one before it
static bool all_bytes(const void *memory, size_t size, unsigned char byte)
{
	const unsigned char *bytes = memory;

	return size == 0 || (bytes[0] == byte && memcmp(bytes, bytes + 1, size - 1) == 0);
}

// A text field of a scene: it ends inside the field, and nothing is left behind its end. Two scenes that
// show the same are the same memory.
static bool text_ok(const char *text, size_t size)
{
	const char *end = memchr(text, '\0', size);

	return end != NULL && all_bytes(end, size - (size_t)(end - text), 0);
}

#define TEXT_OK(field)  text_ok(field, sizeof(field))

// A text for a field of the inputs. One that does not fit would be written over what lies behind the field,
// and the test would go on with inputs nobody meant: it counts as an input that could not be made.
static void set_text(char *field, size_t size, const char *text)
{
	if(strlen(text) >= size)
	{
		printf("  \"%s\" does not fit into a field of %lu bytes\n", text, (unsigned long)size);
		setup_failures++;
		text = "";
	}
	strcpy(field, text);
}

#define SET(field, text)    set_text(field, sizeof(field), text)
#define NEED(condition, text) do { if(wrong == NULL && !(condition)) wrong = (text); } while(0)

// What the header says of every scene, whatever it shows. Prints what is missing.
static bool sound(const scene_t *s)
{
	const char *wrong = NULL;
	bool no_dots = s->dots == 0 && s->dot == -1;
	bool no_rows = s->row_count == 0 && s->first == 0 && s->total == 0;
	bool no_options = s->options[0][0] == '\0' && s->options[1][0] == '\0' && s->option == 0;
	int focused = 0;

	NEED((unsigned)s->kind <= SCENE_LEVEL, "kind is no member of its enum");
	NEED((unsigned)s->ring.kind <= RING_PROGRESS, "ring is no member of its enum");
	NEED(s->ring.permille >= 0 && s->ring.permille <= 1000 && (s->ring.kind == RING_PROGRESS || s->ring.permille == 0), "ring.permille");
	NEED(s->ring.kind != RING_PROGRESS || (s->kind != SCENE_PROGRESS && (s->kind != SCENE_CHOICE || s->permille == -1)), "the ring is a second arc next to the one of the screen");
	NEED(TEXT_OK(s->title) && TEXT_OK(s->note) && TEXT_OK(s->big) && TEXT_OK(s->options[0]) && TEXT_OK(s->options[1]), "a text of the scene has no end, or bytes behind it");

	NEED(s->item_count >= 0 && s->item_count <= LAYOUT_ITEMS_MAX, "item_count");
	for(int i = 0; i < LAYOUT_ITEMS_MAX; i++)
	{
		const scene_item_t *item = &s->items[i];

		if(i >= s->item_count)
		{
			NEED(all_bytes(item, sizeof(*item), 0), "an item that is not used is not zero");
			continue;
		}
		NEED(TEXT_OK(item->label) && TEXT_OK(item->text) && TEXT_OK(item->unit), "a text of an item has no end, or bytes behind it");
		NEED((unsigned)item->tone <= SCENE_TONE_ALARM && (unsigned)item->widget <= LAYOUT_WIDGET_STATE, "tone or widget of an item is no member of its enum");
		NEED(item->permille >= -1 && item->permille <= 1000, "permille of an item");
		NEED(item->permille == -1 || item->widget == LAYOUT_WIDGET_ARC || item->widget == LAYOUT_WIDGET_BAR, "permille of an item that is no arc and no bar");
		NEED(item->text[0] != '\0' || item->widget == LAYOUT_WIDGET_STATE, "an item without a text");
	}
	NEED(s->dots >= 0 && s->dot >= -1 && (s->dot < s->dots || s->dot == -1), "dots and dot");

	NEED(s->row_count >= 0 && s->row_count <= SCENE_ROWS_MAX, "row_count");
	for(int i = 0; i < SCENE_ROWS_MAX; i++)
	{
		const scene_row_t *row = &s->rows[i];

		if(i >= s->row_count)
		{
			NEED(all_bytes(row, sizeof(*row), 0), "a row that is not used is not zero");
			continue;
		}
		NEED(TEXT_OK(row->text) && TEXT_OK(row->detail), "a text of a row has no end, or bytes behind it");
		NEED((unsigned)row->kind <= SCENE_ROW_SUB, "kind of a row is no member of its enum");
		NEED(row->kind == SCENE_ROW_ACTION || row->enabled, "a line that is not enabled");
		if(row->focus) focused++;
	}
	NEED(focused <= 1, "more than one row has the focus");
	NEED(s->first >= 0 && s->total >= 0 && s->first <= s->total - s->row_count, "the visible rows are not rows of the screen");
	NEED(s->row_count == (s->total < SCENE_ROWS_MAX ? s->total : SCENE_ROWS_MAX), "fewer rows are visible than there is room for");

	NEED(s->line_count >= 0 && s->line_count <= SCENE_LINES_MAX, "line_count");
	for(int i = 0; i < SCENE_LINES_MAX; i++)
	{
		if(i >= s->line_count) NEED(all_bytes(s->lines[i], sizeof(s->lines[i]), 0), "a line that is not used is not empty");
		else NEED(TEXT_OK(s->lines[i]), "a line has no end, or bytes behind it");
	}
	NEED(s->permille >= -1 && s->permille <= 1000, "permille");
	NEED(s->option == 0 || s->option == 1, "option");

	NEED((unsigned)s->over <= SCENE_OVER_UPDATE, "over is no member of its enum");
	NEED(s->over_line_count >= 0 && s->over_line_count <= 3, "over_line_count");
	for(int i = 0; i < 3; i++)
	{
		if(i >= s->over_line_count) NEED(all_bytes(s->over_lines[i], sizeof(s->over_lines[i]), 0), "an overlay line that is not used is not empty");
		else NEED(TEXT_OK(s->over_lines[i]), "an overlay line has no end, or bytes behind it");
	}
	NEED(s->over_permille >= -1 && s->over_permille <= 1000, "over_permille");

	// What a kind does not use: texts empty, counts 0, permille -1, no dots
	switch(s->kind)
	{
		case SCENE_VALUES:
			NEED(no_rows && s->line_count == 0 && s->big[0] == '\0' && s->permille == -1 && no_options, "a value page with fields it does not use");
			break;
		case SCENE_NOTICE:
			NEED(s->item_count == 0 && no_rows && s->big[0] == '\0' && s->permille == -1 && no_options, "a notice with fields it does not use");
			break;
		case SCENE_LIST:
			NEED(s->item_count == 0 && no_dots && s->big[0] == '\0' && s->permille == -1 && no_options, "a list with fields it does not use");
			break;
		case SCENE_PROGRESS:
			NEED(s->item_count == 0 && no_dots && no_rows && no_options, "a progress with fields it does not use");
			NEED(s->big[0] != '\0' && s->permille >= 0, "a progress without its big text or its permille");
			break;
		case SCENE_CHOICE:
			NEED(s->item_count == 0 && no_dots && no_rows && s->big[0] == '\0', "a choice with fields it does not use");
			NEED(s->options[0][0] != '\0' && s->options[1][0] != '\0', "a choice without its two answers");
			break;
		case SCENE_LEVEL:
			NEED(s->item_count == 0 && no_dots && no_rows && s->line_count == 0 && no_options, "a level with fields it does not use");
			NEED(s->big[0] != '\0' && s->permille >= 0, "a level without its big text or its permille");
			break;
	}
	switch(s->over)
	{
		case SCENE_OVER_NONE:
			NEED(s->over_line_count == 0 && s->over_permille == -1, "no overlay, but lines or a progress of one");
			break;
		case SCENE_OVER_UPLOAD:
			NEED(s->over_line_count == 2 && s->over_permille >= 0, "an upload without its two lines or its progress");
			break;
		case SCENE_OVER_ASK:
			NEED((s->over_line_count == 2 || s->over_line_count == 3) && s->over_permille == -1, "a question without its lines, or with a progress");
			break;
		case SCENE_OVER_UPDATE:
			NEED(s->over_line_count == 3 && s->over_permille == -1, "the update question without its three lines, or with a progress");
			break;
	}

	if(wrong != NULL) printf("  a scene is not sound: %s\n", wrong);
	return wrong == NULL;
}

// Builds the scene of `input` twice: between guard bytes over memory the module did not clear, and over zeros
static void build(void)
{
	memset(&box, FILL, sizeof(box));
	scene_build(&input, &box.scene);
	memset(&zeroed, 0, sizeof(zeroed));
	scene_build(&input, &zeroed);

	built++;
	if(!all_bytes(box.front, sizeof(box.front), FILL) || !all_bytes(box.behind, sizeof(box.behind), FILL))
	{
		printf("  bytes outside of the scene were written\n");
		outside++;
	}
	if(memcmp(&box.scene, &zeroed, sizeof(zeroed)) != 0)
	{
		printf("  the scene depends on what the memory held before\n");
		unset++;
	}
	if(!sound(&box.scene)) unsound++;
}

// What holds for every scene of a group, checked at its end
static void check_all(const char *group)
{
	snprintf(what, sizeof(what), "%s: the inputs could be made with the real modules", group);
	check(setup_failures == 0, what);
	snprintf(what, sizeof(what), "%s: no scene wrote a byte outside of its scene_t", group);
	check(built > 0 && outside == 0, what);
	snprintf(what, sizeof(what), "%s: every field is set on every call - a scene built over 0xAA is the same memory as one built over zeros", group);
	check(built > 0 && unset == 0, what);
	snprintf(what, sizeof(what), "%s: in every scene texts end in their fields, counts and numbers are in their ranges, and what a kind does not use is empty, 0 or -1", group);
	check(built > 0 && unsound == 0, what);
}

// scene_dump() into a room of `size` bytes between guard bytes. Returns what it returned, or -2 if it wrote
// outside of the room.
static int dump_into(const scene_t *s, size_t size)
{
	int length;

	memset(&dump_box, FILL, sizeof(dump_box));
	length = scene_dump(s, dumped, size);
	if(!all_bytes(dump_box.front, sizeof(dump_box.front), FILL) || !all_bytes(dumped + size, sizeof(dump_box.text) - size + sizeof(dump_box.behind), FILL))
	{
		printf("  the dump wrote outside of its room of %lu bytes\n", (unsigned long)size);
		return -2;
	}
	return length;
}

// The dump of a scene is the text of fixtures/scene_<name>.txt, byte for byte
static bool dump_is_file(const scene_t *s, const char *name)
{
	char path[160];
	int length = dump_into(s, SCENE_DUMP_SIZE);
	size_t wanted;

	snprintf(path, sizeof(path), "fixtures/scene_%s.txt", name);
	if(!read_fixture(path, file_text, sizeof(file_text) - 1)) return false;
	// Every line of a dump ends with a line break; read_fixture() took the last one away
	strcat(file_text, "\n");
	wanted = strlen(file_text);

	if(length == (int)wanted && memcmp(dumped, file_text, wanted + 1) == 0) return true;
	// As many bytes as were returned: a dump without its zero must not be read to its end
	printf("  %s wants (%lu bytes)\n%s  the scene is (%d bytes)\n%.*s", path, (unsigned long)wanted, file_text, length, length >= 0 ? length : 0, dumped);
	return false;
}

// The scene of `input` is the one of the fixture
static void screen(const char *name, const char *rule)
{
	build();
	snprintf(what, sizeof(what), "scene_%s.txt: %s", name, rule);
	check(dump_is_file(scene, name), what);
}

static bool row_is(int index, bool focus, scene_row_kind_t kind, const char *text, const char *detail, bool enabled)
{
	const scene_row_t *row = &scene->rows[index];

	return index < scene->row_count && row->focus == focus && row->kind == kind && strcmp(row->text, text) == 0 &&
	       strcmp(row->detail, detail) == 0 && row->enabled == enabled;
}

static bool item_is(int index, const char *label, const char *text, const char *unit, scene_tone_t tone, layout_widget_t widget, int permille)
{
	const scene_item_t *item = &scene->items[index];

	return index < scene->item_count && strcmp(item->label, label) == 0 && strcmp(item->text, text) == 0 && strcmp(item->unit, unit) == 0 &&
	       item->tone == tone && item->widget == widget && item->permille == permille;
}

// The text lines are exactly these; NULL ends the list
static bool lines_are(const char *first, const char *second, const char *third, const char *fourth)
{
	const char *wanted[SCENE_LINES_MAX] = {first, second, third, fourth};
	int count = 0;

	while(count < SCENE_LINES_MAX && wanted[count] != NULL) count++;
	if(scene->line_count != count) return false;
	for(int i = 0; i < count; i++)
	{
		if(strcmp(scene->lines[i], wanted[i]) != 0) return false;
	}
	return true;
}

static bool over_is(scene_over_t over, int permille, const char *first, const char *second, const char *third)
{
	const char *wanted[3] = {first, second, third};
	int count = 0;

	while(count < 3 && wanted[count] != NULL) count++;
	if(scene->over != over || scene->over_permille != permille || scene->over_line_count != count) return false;
	for(int i = 0; i < count; i++)
	{
		if(strcmp(scene->over_lines[i], wanted[i]) != 0) return false;
	}
	return true;
}

static bool ring_is(ring_kind_t kind, int permille)
{
	return scene->ring.kind == kind && scene->ring.permille == permille;
}

// Title, note and kind
static bool head_is(scene_kind_t kind, const char *title, const char *note)
{
	return scene->kind == kind && strcmp(scene->title, title) == 0 && strcmp(scene->note, note) == 0;
}

/* The inputs ------------------------------------------------------------------------------------------- */

// An answer of GET /autopid_data that arrived `age_ms` ago. Without a pass counter: every answer renews.
static void seen(const char *json, uint64_t age_ms)
{
	if(values_apply(&values, json, strlen(json), -1, NOW - age_ms, work, COUNT(work)) != VALUES_RENEWED) setup_failures++;
}

static void expect_view(conn_view_t view)
{
	if(conn_view(&conn, NOW) != view) setup_failures++;
}

// The adapter answered GET /api/state with `adapter` 4.9 seconds after the display joined its network
static void answer(conn_view_t view)
{
	conn_init(&conn, ID);
	conn_wifi(&conn, true, NOW - 5000);
	if(conn_next(&conn, NOW - 5000) != CONN_ASK_STATE) setup_failures++;
	conn_got_state(&conn, CONN_GOT_OK, &adapter, NOW - 4900);
	expect_view(view);
}

static void view_no_wifi(void)
{
	conn_init(&conn, ID);
	expect_view(CONN_VIEW_NO_WIFI);
}

static void view_connecting(void)
{
	conn_init(&conn, ID);
	conn_wifi(&conn, true, NOW - 1000);
	expect_view(CONN_VIEW_CONNECTING);
}

// Three rounds without an answer, the time of grace over
static void view_no_answer(void)
{
	conn_init(&conn, ID);
	conn_wifi(&conn, true, NOW - 20000);
	for(int round = 0; round < 3; round++)
	{
		uint64_t ago = 20000 - (uint64_t)round * 5000;

		if(conn_next(&conn, NOW - ago) != CONN_ASK_STATE) setup_failures++;
		conn_got_state(&conn, CONN_GOT_FAILED, NULL, NOW - ago + 100);
	}
	expect_view(CONN_VIEW_NO_ANSWER);
}

static void view_foreign(void)
{
	SET(adapter.id, "ffffffffffff");
	answer(CONN_VIEW_FOREIGN);
}

static void view_no_api(void)
{
	conn_init(&conn, ID);
	conn_wifi(&conn, true, NOW - 5000);
	if(conn_next(&conn, NOW - 5000) != CONN_ASK_STATE) setup_failures++;
	conn_got_state(&conn, CONN_GOT_NOT_FOUND, NULL, NOW - 4900);
	expect_view(CONN_VIEW_NO_API);
}

static void view_autopid_off(void)
{
	adapter.autopid = WICAN_AUTOPID_OFF;
	answer(CONN_VIEW_AUTOPID_OFF);
}

static void view_starting(void)
{
	adapter.autopid = WICAN_AUTOPID_STARTING;
	answer(CONN_VIEW_STARTING);
}

// A scan of the adapter: request `seq`, at step `step` of `total`
static void scan(wican_dtc_phase_t phase, uint32_t seq, bool clear, uint32_t step, uint32_t total, const char *name)
{
	adapter.dtc.phase = phase;
	adapter.dtc.has_request = true;
	adapter.dtc.clear = clear;
	adapter.dtc.from_http = true;
	adapter.dtc.seq = seq;
	adapter.dtc.step = step;
	adapter.dtc.total = total;
	SET(adapter.dtc.name, name);
}

// Somebody reads the fault memory, control unit 5 of 18
static void view_scan(void)
{
	scan(WICAN_DTC_RUNNING, 41, false, 5, 18, "N30/4 ESP");
	answer(CONN_VIEW_SCAN);
}

static void view_ecu_offline(void)
{
	adapter.ecu_online = false;
	answer(CONN_VIEW_ECU_OFFLINE);
}

static void view_live(void)
{
	answer(CONN_VIEW_LIVE);
}

// Every view of the connection, with what texts.h says of it
static const struct
{
	conn_view_t view;
	const char *name;
	void (*make)(void);
	ring_kind_t ring;
	int permille;
	const char *text;
	bool values;        // the value pages are shown in this view
} VIEWS[] = {
	{CONN_VIEW_NO_WIFI, "NO_WIFI", view_no_wifi, RING_RED, 0, "WiCAN nicht gefunden – schläft, stromlos oder außer Reichweite", false},
	{CONN_VIEW_CONNECTING, "CONNECTING", view_connecting, RING_YELLOW, 0, "Verbinde mit WiCAN …", false},
	{CONN_VIEW_NO_ANSWER, "NO_ANSWER", view_no_answer, RING_RED, 0, "WiCAN antwortet nicht", false},
	{CONN_VIEW_FOREIGN, "FOREIGN", view_foreign, RING_RED, 0, "Fremdes WiCAN – nicht gekoppelt", false},
	{CONN_VIEW_NO_API, "NO_API", view_no_api, RING_GREY, 0, NO_API_NOTE, true},
	{CONN_VIEW_AUTOPID_OFF, "AUTOPID_OFF", view_autopid_off, RING_GREY, 0, "AutoPID nicht aktiv", false},
	{CONN_VIEW_STARTING, "STARTING", view_starting, RING_YELLOW, 0, "WiCAN startet …", false},
	{CONN_VIEW_SCAN, "SCAN", view_scan, RING_PROGRESS, 277, SCAN_NOTE, true},
	{CONN_VIEW_ECU_OFFLINE, "ECU_OFFLINE", view_ecu_offline, RING_GREY, 0, "Zündung aus – Motorsteuergerät offline", false},
	{CONN_VIEW_LIVE, "LIVE", view_live, RING_NONE, 0, "", true},
};

// Everything in its place: the adapter live with the ignition on, every value fresh, the first value page,
// no request, nothing over the screen
static void stage(void)
{
	memset(&adapter, 0, sizeof(adapter));
	SET(adapter.id, ID);
	adapter.boot = 77;
	adapter.up_s = 600;
	adapter.autopid = WICAN_AUTOPID_RUN;
	adapter.pids = 13;
	adapter.ecu_online = true;
	adapter.pass = 5;
	adapter.rx_age_ms = 140;
	adapter.batt_mv = 12400;
	adapter.sleep_in_s = -1;
	adapter.dtc.supported = true;
	view_live();

	values_init(&values);
	seen(base_values, 500);
	dtc_flow_init(&flow);
	hold_init(&hold);
	access_init(&gate);

	memset(&world, 0, sizeof(world));
	world.layout = &layout;
	world.catalog = &catalog;
	world.flow = DTC_FLOW_IDLE;
	world.can_read = true;
	world.asking = ACCESS_ASK_NONE;
	world.brightness = 80;

	memset(&nav, 0, sizeof(nav));
	nav.screen = NAV_PAGES;
	nav.confirm = NAV_DO_NOTHING;

	memset(&input, 0, sizeof(input));
	input.nav = &nav;
	input.world = &world;
	input.conn = &conn;
	input.values = &values;
	input.flow = &flow;
	input.read_block = DTC_FLOW_ALLOWED;
	input.clear_block = DTC_FLOW_NO_LIST;
	input.hold = &hold;
	input.access = &gate;
	input.address = "http://192.168.88.37";
	input.ap_ssid = "WiCAN-Display";
	input.ap_password = "geheim1234";
	input.heat = GUARD_HEAT_NORMAL;
	input.now_ms = NOW;
}

// The stage with another screen in front
static void stage_on(nav_screen_t on, int row)
{
	stage();
	nav.screen = on;
	nav.row = row;
}

// The list of the own read is there: `lines`, read 48 seconds ago, so that 9:12 are left to clear it
static void have_list(const dtc_line_t *lines, int count)
{
	input.list = lines;
	world.list_lines = count;
	world.flow = DTC_FLOW_LIST;
	flow.phase = DTC_FLOW_LIST;
	flow.boot = 77;
	flow.seq = 41;
	flow.read_seq = 41;
	flow.list_count = 10;
	flow.list_end_ms = NOW - 48000;
}

// The list of the fixture dtc_view_mixed.json in front, clearing allowed
static void stage_list(int row)
{
	stage_on(NAV_DTC_LIST, row);
	have_list(mixed_lines, mixed_count);
	world.can_clear = true;
	input.clear_block = DTC_FLOW_ALLOWED;
	input.summary = &mixed_summary;
}

// The clear dialog, opened 5 seconds ago, the switch seen released, and pressed on "Löschen" since `held_ms`
// (0: not pressed)
static void stage_dialog(int row, uint64_t held_ms)
{
	stage_list(mixed_count + 1);
	nav.screen = NAV_DTC_CONFIRM;
	nav.row = row;
	if(!hold_open(&hold, NOW - 5000)) setup_failures++;
	if(hold_sample(&hold, false, true, row == 1, NOW - 4000) != HOLD_WAITING) setup_failures++;
	if(held_ms > 0 && hold_sample(&hold, true, true, true, NOW - held_ms) != HOLD_PROGRESS) setup_failures++;
}

// The own request is under way
static void stage_busy(dtc_flow_phase_t phase, uint32_t seq)
{
	stage_on(NAV_DTC_BUSY, 0);
	world.flow = phase;
	world.can_read = false;
	input.read_block = DTC_FLOW_BUSY;
	flow.phase = phase;
	flow.boot = 77;
	flow.seq = seq;
}

static void stage_failed(dtc_flow_phase_t phase, const char *reason)
{
	stage_on(NAV_DTC_FAILED, 0);
	world.flow = phase;
	flow.phase = phase;
	SET(flow.reason, reason);
}

// A layout of one page "Probe" with one value X, to be changed by hand. The stage has to be set before.
static layout_item_t *probe(void)
{
	layout_item_t *item = &scratch.pages[0].items[0];

	memset(&scratch, 0, sizeof(scratch));
	scratch.page_count = 1;
	SET(scratch.pages[0].title, "Probe");
	scratch.pages[0].item_count = 1;
	SET(item->key, "X");
	item->scale = 1;
	world.layout = &scratch;
	nav.page = 0;
	return item;
}

// One more value of the probe
static layout_item_t *probe_more(const char *key)
{
	layout_item_t *item = &scratch.pages[0].items[scratch.pages[0].item_count++];

	SET(item->key, key);
	item->scale = 1;
	return item;
}

static void limit(layout_limit_t *of, double value)
{
	of->set = true;
	of->value = value;
}

static bool parse_result(const char *path, dtc_result_t *into)
{
	return read_fixture(path, file_text, sizeof(file_text)) && dtc_result_parse(file_text, strlen(file_text), into, work, COUNT(work));
}

// Read once, before the groups run
static void test_inputs(void)
{
	static const char hidden[] = "{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":["
	                             "{\"title\":\"A\",\"hidden\":true,\"items\":[{\"key\":\"ENGINE_RPM\"}]},"
	                             "{\"title\":\"B\",\"hidden\":true,\"items\":[{\"key\":\"COOLANT_TMP\"}]}]}";
	layout_report_t report;
	bool read;

	read = read_fixture("fixtures/scene_layout.json", file_text, sizeof(file_text)) &&
	       layout_parse(file_text, strlen(file_text), &layout, &report, work, COUNT(work));
	check(read && report.warnings == 0 && layout.page_count == 8, "fixture scene_layout.json is a layout of eight pages, taken without a warning");
	check(layout_parse(hidden, strlen(hidden), &hidden_layout, NULL, work, COUNT(work)), "a layout of two hidden pages can be read");

	catalog_init(&unloaded);
	catalog_init(&catalog);
	read = read_fixture("fixtures/scene_car_config.json", file_text, sizeof(file_text)) &&
	       catalog_apply_config(&catalog, file_text, strlen(file_text), work, COUNT(work));
	check(read && catalog.count == 14 && catalog.dropped == 0, "fixture scene_car_config.json is a profile of 13 values, with the battery voltage the catalogue has 14");
	check(layout_first_page(&layout, &catalog) == 0 && layout_page_shown(&layout, 5, &catalog) && !layout_page_shown(&layout, 6, &catalog) &&
	      !layout_page_shown(&layout, 7, &catalog) && layout_page_shown(&layout, 7, &unloaded),
	      "with that profile the layout shows its pages 0 to 5; the hidden page 6 and page 7 of foreign values are shown by no knob, page 7 only before the profile arrived");

	check(read_fixture("fixtures/scene_values.json", base_values, sizeof(base_values)), "fixture scene_values.json can be read");

	read = parse_result("fixtures/dtc_view_mixed.json", &result);
	mixed_count = read ? dtc_view_list(&result, mixed_lines, COUNT(mixed_lines)) : 0;
	dtc_summarize(&result, &mixed_summary);
	check(mixed_count == 17 && mixed_summary.codes == 10 && mixed_summary.ecus_with_codes == 3,
	      "fixture dtc_view_mixed.json gives a list of 17 lines with 10 trouble codes in 3 control units");
	read = parse_result(TOOLS "dtc_result_read_empty.json", &result);
	empty_count = read ? dtc_view_list(&result, empty_lines, COUNT(empty_lines)) : 0;
	check(empty_count == 2, "fixture dtc_result_read_empty.json gives a list of 2 lines");
	read = parse_result(TOOLS "dtc_result_read_codes.json", &result_before);
	codes_count = read ? dtc_view_list(&result_before, codes_lines, COUNT(codes_lines)) : 0;
	check(codes_count == 12, "fixture dtc_result_read_codes.json gives a list of 12 lines");
	read = read && parse_result(TOOLS "dtc_result_clear.json", &result);
	cleared_count = read ? dtc_view_cleared(&result_before, &result, cleared_lines, COUNT(cleared_lines)) : 0;
	check(cleared_count == 7, "fixtures dtc_result_read_codes.json and dtc_result_clear.json give an outcome of 7 lines");
}

/* The value pages -------------------------------------------------------------------------------------- */

static void test_values_screens(void)
{
	static const char *const names[] = {"values_six", "values_one", "values_two", "values_three", "values_four", "values_five"};
	static const char *const rules[] = {
		"six fresh values within their limits: arc, number, scaled number, bar with a label made from its key, state, battery",
		"a page of one value, an arc",
		"a page of two values, a bar and a state",
		"a page of three numbers, units from the catalogue, a label made from the key",
		"a page of four values, one of every widget",
		"a page of five values: one the profile does not have, a bar, a state whose map does not name the value",
	};

	for(int page = 0; page < COUNT(names); page++)
	{
		stage();
		nav.page = page;
		screen(names[page], rules[page]);
	}

	stage();
	values_init(&values);
	seen("{\"ENGINE_RPM\":812,\"BOOST_PRESSURE\":1013}", 500);
	seen("{\"@BATT_V\":14.1}", 2999);
	seen("{\"COOLANT_TMP\":88.4}", 3000);
	seen("{\"ACCEL_PEDAL\":12.5}", 9999);
	seen("{\"DPF_REGEN_STATUS\":1}", 10000);
	screen("values_ages", "values 500, 2999, 3000, 9999 and 10000 ms old: fresh ones as they are, old ones dimmed, a gone one a dash without unit and range; the ring is yellow");

	stage();
	values_init(&values);
	screen("values_none", "no value at all: every value a dimmed dash without unit, arc and bar without a position; the ring is yellow - a page of dashes is not one on which all is well");
	stage();
	values_init(&values);
	seen(base_values, 10000);
	nav.page = 4;
	screen("values_all_gone", "every value seen 10000 ms ago: all of them have become dashes, and the ring stays yellow as it was while they were old");
	stage();
	seen("{\"COOLANT_TMP\":88.4}", 10000);
	nav.page = 1;
	screen("values_one_gone", "a page of one value that is gone, while other values are fresh: a dash, the ring yellow");
	stage();
	values_init(&values);
	nav.page = 5;
	screen("values_five_gone", "a page of dashes and of one value the profile does not have: the ring is yellow for the dashes");

	stage();
	world.catalog = &unloaded;
	nav.page = 7;
	screen("values_unloaded", "before the profile arrived a page of foreign values is one of seven and shows dashes: nobody knows yet that the profile lacks them, the ring is yellow");
	stage();
	nav.page = 7;
	screen("values_foreign", "values the profile does not have are n. v., dimmed, without unit; their page is still shown but no dot is lit; the ring is off - nothing is missing");
	stage();
	nav.page = 6;
	screen("values_hidden", "a hidden page the knob is still on is shown, with no dot lit");

	stage();
	seen("{\"ENGINE_RPM\":4000,\"COOLANT_TMP\":104.6,\"@BATT_V\":11.8}", 500);
	screen("values_warn", "values at a warn limit from above and from below: tone warn, the ring yellow; a value below its limit that rounds to it stays normal");
	stage();
	seen("{\"ENGINE_RPM\":4500,\"COOLANT_TMP\":115,\"@BATT_V\":11,\"ACCEL_PEDAL\":120}", 500);
	screen("values_alarm", "values at a crit limit from above and from below: tone alarm, the ring red; a bar beyond its range is full");
	stage();
	seen("{\"COOLANT_TMP\":120}", 5000);
	screen("values_old_alarm", "an old value beyond its crit limit is dimmed like every old value, and the ring is red: the value is still shown");
	stage();
	seen("{\"COOLANT_TMP\":120}", 10000);
	screen("values_gone_alarm", "a value beyond its crit limit that is gone is a dash: the ring is yellow for the missing value, not red for its last number");
	stage();
	seen("{\"ENGINE_RPM\":1000000000000,\"BOOST_PRESSURE\":1000000000000000}", 500);
	screen("values_no_text", "a fresh value too large to be printed counts as no value: a dimmed dash without unit and range; it raises no level, and the ring is yellow for the dash");

	stage();
	view_scan();
	seen("{\"ENGINE_RPM\":4500,\"COOLANT_TMP\":115,\"@BATT_V\":11,\"ACCEL_PEDAL\":120}", 500);
	screen("values_scan", "during a scan every value is dimmed, also one beyond a limit; the ring shows the progress, the note tells why");
	stage();
	scan(WICAN_DTC_QUEUED, 41, false, 0, 0, "");
	answer(CONN_VIEW_SCAN);
	nav.page = 2;
	screen("values_scan_queued", "a queued scan dims the values as well, the ring is an arc of nothing");
	stage();
	view_no_api();
	nav.page = 3;
	screen("values_no_api", "a firmware without the API: the values in their tones, the ring grey, the note tells of it");

	stage();
	view_scan();
	nav.page = 1;
	input.safe_mode = true;
	input.heat = GUARD_HEAT_DIM;
	screen("values_safe_mode", "safe mode, too hot and a scan at once: the note is that of the safe mode");
	stage();
	view_no_api();
	nav.page = 1;
	input.heat = GUARD_HEAT_DIM;
	screen("values_hot", "too hot with a firmware without the API: the note is that of the heat");
}

static void test_items(void)
{
	static const struct
	{
		const char *key;
		const char *label;
	} labels[] = {
		{"ENGINE_OIL_TEMP", "Engine Oil Temp"},
		{"@BATT_V", "Batt V"},
		{"x", "X"},
		// 24 bytes are the longest label there is room for
		{"ABCDEFGHIJKLMNOPQRSTUVWX", "Abcdefghijklmnopqrstuvwx"},
		{"A_B_C_D_E_F_G_H_I_J_K_LM", "A B C D E F G H I J K Lm"},
		{"ABCDEFGHIJKLMNOPQRSTUVWXY", ""},
		{"A_B_C_D_E_F_G_H_I_J_K_L_M", ""},
		{"DPF_KM_SINCE_REGENERATION_TOTAL", ""},
	};
	static const struct
	{
		uint64_t age_ms;
		const char *text;
		ring_kind_t ring;
		const char *rule;
	} gone[] = {
		{0, "88", RING_NONE, "fresh, the ring off"},
		{2999, "88", RING_NONE, "still fresh, the ring off"},
		{3000, "88", RING_YELLOW, "old, the ring yellow"},
		{9999, "88", RING_YELLOW, "still old and shown, the ring yellow"},
		{10000, SCENE_DASH, RING_YELLOW, "gone, a dash, and the ring stays yellow"},
		{10001, SCENE_DASH, RING_YELLOW, "a dash, the ring yellow"},
		{600000, SCENE_DASH, RING_YELLOW, "a dash for ten minutes, the ring still yellow"},
	};
	// ENGINE_RPM is fresh, FUEL_L old, COOLANT_TMP gone; the profile has neither TRANS_TEMP nor TURBO_SPEED
	static const struct
	{
		const char *first, *first_text;
		const char *second, *second_text;
		ring_kind_t ring;
		const char *rule;
	} pairs[] = {
		{"ENGINE_RPM", "812", "TRANS_TEMP", SCENE_UNAVAILABLE, RING_NONE, "a fresh one and one the profile does not provide"},
		{"TRANS_TEMP", SCENE_UNAVAILABLE, "ENGINE_RPM", "812", RING_NONE, "one the profile does not provide and a fresh one"},
		{"TRANS_TEMP", SCENE_UNAVAILABLE, "TURBO_SPEED", SCENE_UNAVAILABLE, RING_NONE, "both not provided by the profile"},
		{"COOLANT_TMP", SCENE_DASH, "TRANS_TEMP", SCENE_UNAVAILABLE, RING_YELLOW, "a dash and one the profile does not provide"},
		{"TRANS_TEMP", SCENE_UNAVAILABLE, "COOLANT_TMP", SCENE_DASH, RING_YELLOW, "one the profile does not provide and a dash"},
		{"COOLANT_TMP", SCENE_DASH, "ENGINE_RPM", "812", RING_YELLOW, "a dash and a fresh one"},
		{"ENGINE_RPM", "812", "COOLANT_TMP", SCENE_DASH, RING_YELLOW, "a fresh one and a dash"},
		{"FUEL_L", "43", "TRANS_TEMP", SCENE_UNAVAILABLE, RING_YELLOW, "an old one and one the profile does not provide"},
		{"TRANS_TEMP", SCENE_UNAVAILABLE, "FUEL_L", "43", RING_YELLOW, "one the profile does not provide and an old one"},
		{"COOLANT_TMP", SCENE_DASH, "FUEL_L", "43", RING_YELLOW, "a dash and an old one"},
		{"COOLANT_TMP", SCENE_DASH, "COOLANT_TMP", SCENE_DASH, RING_YELLOW, "both dashes"},
	};
	layout_item_t *item, *second;

	for(int i = 0; i < COUNT(labels); i++)
	{
		stage();
		item = probe();
		SET(item->key, labels[i].key);
		build();
		snprintf(what, sizeof(what), "a value without label, key \"%s\": the label is \"%s\"%s", labels[i].key, labels[i].label,
		         labels[i].label[0] == '\0' ? ", empty because the readable name does not fit, and no byte of it is left behind" : ", made from the key");
		check(scene->item_count == 1 && strcmp(scene->items[0].label, labels[i].label) == 0 && TEXT_OK(scene->items[0].label), what);
	}

	stage();
	item = probe();
	SET(item->label, "Ladedruck Niederdruck ä");
	SET(scratch.pages[0].title, "Abgasnachbehandlung äö");
	seen("{\"X\":7}", 500);
	build();
	check(strlen(item->label) == 24 && strlen(scratch.pages[0].title) == 24 && head_is(SCENE_VALUES, "Abgasnachbehandlung äö", "") &&
	      item_is(0, "Ladedruck Niederdruck ä", "7", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1),
	      "the longest title and the longest label of a layout, 24 bytes each, are passed on whole");
	SET(item->label, "x");
	build();
	check(item_is(0, "x", "7", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1), "a label of one byte is the label, not the name made from the key");

	// The unit
	stage();
	item = probe();
	SET(item->key, "COOLANT_TMP");
	build();
	check(item_is(0, "Coolant Tmp", "88", "°C", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1), "a value without a unit of its own has the unit of the catalogue");
	item->has_unit = true;
	SET(item->unit, "Grad Cel");
	build();
	check(item_is(0, "Coolant Tmp", "88", "Grad Cel", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1), "a unit of its own, the longest of 8 bytes, goes before the one of the catalogue");
	item->unit[0] = '\0';
	build();
	check(item_is(0, "Coolant Tmp", "88", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1), "an empty unit of its own is no unit, whatever the catalogue says");
	item->has_unit = false;
	SET(catalog.entries[catalog_find(&catalog, "COOLANT_TMP")].unit, "Grad Celsiu");
	build();
	check(item_is(0, "Coolant Tmp", "88", "Grad Celsiu", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1), "the longest unit of a catalogue, 11 bytes, is passed on whole");
	SET(catalog.entries[catalog_find(&catalog, "COOLANT_TMP")].unit, "°C");
	item->widget = LAYOUT_WIDGET_STATE;
	build();
	check(item_is(0, "Coolant Tmp", "88", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_STATE, -1), "a state widget has no unit, also when its text is a number because no map entry fits");
	item->has_unit = true;
	SET(item->unit, "bar");
	build();
	check(item_is(0, "Coolant Tmp", "88", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_STATE, -1), "a state widget has no unit, also when the layout gives it one");
	item->widget = LAYOUT_WIDGET_NUMBER;
	seen("{\"COOLANT_TMP\":88.4}", 10000);
	build();
	check(item_is(0, "Coolant Tmp", SCENE_DASH, "", SCENE_TONE_DIM, LAYOUT_WIDGET_NUMBER, -1), "a dash has no unit, also when the layout gives the value one");
	SET(item->key, "TRANS_TEMP");
	build();
	check(item_is(0, "Trans Temp", SCENE_UNAVAILABLE, "", SCENE_TONE_DIM, LAYOUT_WIDGET_NUMBER, -1), "n. v. has no unit, also when the layout gives the value one");
	check(strcmp(SCENE_DASH, "–") == 0 && strcmp(SCENE_UNAVAILABLE, "n. v.") == 0, "the dash is an en dash, the text for a value the profile does not provide is \"n. v.\"");

	// The text
	stage();
	item = probe();
	item->widget = LAYOUT_WIDGET_STATE;
	item->map_count = 1;
	SET(item->map[0].raw, "*");
	SET(item->map[0].text, "Regeneration aktiv äö");
	seen("{\"X\":3}", 500);
	build();
	check(strlen(item->map[0].text) == 23 && item_is(0, "X", "Regeneration aktiv äö", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_STATE, -1),
	      "the longest text of a state, 23 bytes, is passed on whole");
	item->map[0].text[0] = '\0';
	build();
	check(item_is(0, "X", "", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_STATE, -1), "a state whose map gives an empty text shows an empty text, not a dash");
	stage();
	item = probe();
	item->decimals = 3;
	seen("{\"X\":-999999999999.9994}", 500);
	build();
	check(item_is(0, "X", "-999999999999,999", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1), "the longest number that can be printed is passed on whole");
	seen("{\"X\":1e999}", 500);
	build();
	check(item_is(0, "X", SCENE_DASH, "", SCENE_TONE_DIM, LAYOUT_WIDGET_NUMBER, -1) && ring_is(RING_YELLOW, 0), "a value that is not finite is a dimmed dash, and the ring is yellow for it");

	// The widget
	stage();
	item = probe();
	limit(&item->min, 0);
	limit(&item->max, 100);
	seen("{\"X\":50}", 500);
	item->widget = (layout_widget_t)4;
	build();
	check(item_is(0, "X", "50", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1), "a widget behind the last of the enum is shown as a number, without a position in a range");
	item->widget = (layout_widget_t)-1;
	build();
	check(item_is(0, "X", "50", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1), "a widget before the first of the enum is shown as a number");
	item->widget = (layout_widget_t)(256 + LAYOUT_WIDGET_ARC);
	build();
	check(item_is(0, "X", "50", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_NUMBER, -1), "a widget whose low byte is that of an arc is no arc");

	// The tone
	stage();
	item = probe();
	limit(&item->warn_hi, 60);
	limit(&item->crit_hi, 80);
	second = probe_more("Y");
	seen("{\"X\":59,\"Y\":1}", 500);
	build();
	check(scene->items[0].tone == SCENE_TONE_NORMAL && ring_is(RING_NONE, 0), "a fresh value below its warn limit: tone normal, the ring off");
	seen("{\"X\":60}", 500);
	build();
	check(scene->items[0].tone == SCENE_TONE_WARN && scene->items[1].tone == SCENE_TONE_NORMAL && ring_is(RING_YELLOW, 0), "a fresh value at its warn limit: tone warn, the ring yellow, the value next to it normal");
	seen("{\"X\":80}", 500);
	build();
	check(scene->items[0].tone == SCENE_TONE_ALARM && scene->items[1].tone == SCENE_TONE_NORMAL && ring_is(RING_RED, 0), "a fresh value at its crit limit: tone alarm, the ring red");
	seen("{\"X\":60}", 3000);
	build();
	check(scene->items[0].tone == SCENE_TONE_DIM && ring_is(RING_YELLOW, 0), "an old value at its warn limit: tone dim, the ring yellow");
	seen("{\"X\":59}", 3000);
	build();
	check(scene->items[0].tone == SCENE_TONE_DIM && ring_is(RING_YELLOW, 0), "an old value within its limits: tone dim, the ring yellow because it is old");
	seen("{\"X\":80}", 9999);
	build();
	check(scene->items[0].tone == SCENE_TONE_DIM && strcmp(scene->items[0].text, "80") == 0 && ring_is(RING_RED, 0), "an old value at its crit limit: tone dim, the ring red");
	seen("{\"X\":80}", 10000);
	build();
	check(scene->items[0].tone == SCENE_TONE_DIM && strcmp(scene->items[0].text, SCENE_UNAVAILABLE) == 0 && ring_is(RING_NONE, 0),
	      "a value at its crit limit that is gone, of a key the profile does not have: n. v., and the ring is off");
	seen("{\"X\":1e12}", 500);
	build();
	check(scene->items[0].tone == SCENE_TONE_DIM && strcmp(scene->items[0].text, SCENE_DASH) == 0 && ring_is(RING_YELLOW, 0),
	      "a fresh value beyond its crit limit that cannot be printed: a dash; the ring is yellow for the dash, not red for the number");
	seen("{\"X\":1e12}", 3000);
	build();
	check(strcmp(scene->items[0].text, SCENE_DASH) == 0 && ring_is(RING_YELLOW, 0), "an old value that cannot be printed is a dash, and the ring is yellow");

	// A limit counts whatever the widget
	stage();
	item = probe();
	item->widget = LAYOUT_WIDGET_STATE;
	item->map_count = 1;
	SET(item->map[0].raw, "*");
	SET(item->map[0].text, "zu hoch");
	limit(&item->warn_hi, 60);
	limit(&item->crit_hi, 80);
	second = probe_more("Y");
	seen("{\"X\":80,\"Y\":1}", 500);
	build();
	check(item_is(0, "X", "zu hoch", "", SCENE_TONE_ALARM, LAYOUT_WIDGET_STATE, -1) && ring_is(RING_RED, 0), "a state at its crit limit: tone alarm, the ring red");
	seen("{\"X\":60}", 500);
	build();
	check(item_is(0, "X", "zu hoch", "", SCENE_TONE_WARN, LAYOUT_WIDGET_STATE, -1) && ring_is(RING_YELLOW, 0), "a state at its warn limit: tone warn, the ring yellow");
	seen("{\"X\":59}", 5000);
	build();
	check(item_is(0, "X", "zu hoch", "", SCENE_TONE_DIM, LAYOUT_WIDGET_STATE, -1) && ring_is(RING_YELLOW, 0), "an old state within its limits: tone dim, the ring yellow");

	// The limits are those of the shown value
	stage();
	item = probe();
	item->scale = 0.001;
	limit(&item->warn_hi, 1.5);
	limit(&item->crit_hi, 1.9);
	seen("{\"X\":1900}", 500);
	build();
	check(item_is(0, "X", "2", "", SCENE_TONE_ALARM, LAYOUT_WIDGET_NUMBER, -1) && ring_is(RING_RED, 0), "a scaled value, 1900 shown as 1.9, at its crit limit of 1.9: tone alarm, the ring red");
	seen("{\"X\":1500}", 500);
	build();
	check(scene->items[0].tone == SCENE_TONE_WARN && ring_is(RING_YELLOW, 0), "a scaled value, 1500 shown as 1.5, at its warn limit of 1.5: tone warn, the ring yellow");
	seen("{\"X\":1400}", 500);
	build();
	check(scene->items[0].tone == SCENE_TONE_NORMAL && ring_is(RING_NONE, 0), "a scaled value, 1400 shown as 1.4, below its warn limit of 1.5: tone normal, the ring off - the raw number is far beyond the limit");

	// The worst level of the page, whichever value has it
	stage();
	item = probe();
	limit(&item->warn_hi, 60);
	limit(&item->crit_hi, 80);
	second = probe_more("Y");
	limit(&second->warn_lo, 5);
	limit(&second->crit_lo, 2);
	seen("{\"X\":60,\"Y\":2}", 500);
	build();
	check(scene->items[0].tone == SCENE_TONE_WARN && scene->items[1].tone == SCENE_TONE_ALARM && ring_is(RING_RED, 0), "the first value at its warn limit, the second at its crit limit: the ring is red");
	seen("{\"X\":80,\"Y\":5}", 500);
	build();
	check(scene->items[0].tone == SCENE_TONE_ALARM && scene->items[1].tone == SCENE_TONE_WARN && ring_is(RING_RED, 0), "the first value at its crit limit, the second at its warn limit: the ring is red");
	seen("{\"X\":59,\"Y\":5}", 500);
	build();
	check(scene->items[0].tone == SCENE_TONE_NORMAL && scene->items[1].tone == SCENE_TONE_WARN && ring_is(RING_YELLOW, 0), "only the second value at its warn limit: the ring is yellow");
	seen("{\"X\":59,\"Y\":6}", 500);
	seen("{\"Y\":6}", 3000);
	build();
	check(scene->items[0].tone == SCENE_TONE_NORMAL && scene->items[1].tone == SCENE_TONE_DIM && ring_is(RING_YELLOW, 0), "only the second value old: the ring is yellow");
	seen("{\"X\":59}", 3000);
	seen("{\"Y\":6}", 500);
	build();
	check(scene->items[0].tone == SCENE_TONE_DIM && scene->items[1].tone == SCENE_TONE_NORMAL && ring_is(RING_YELLOW, 0), "only the first value old: the ring is yellow");

	// Other screens do not look at the values
	seen("{\"X\":80,\"Y\":1}", 500);
	nav.screen = NAV_MENU;
	build();
	check(ring_is(RING_NONE, 0), "the menu over a page with a value beyond its crit limit: the ring is off, the page is not on the screen");
	nav.screen = NAV_PAGES;
	seen("{\"X\":80}", 4000);
	nav.screen = NAV_BRIGHTNESS;
	build();
	check(ring_is(RING_NONE, 0), "the brightness over a page with an old value: the ring is off");
	nav.screen = NAV_PAGES;
	nav.page = -1;
	build();
	check(scene->kind == SCENE_NOTICE && ring_is(RING_NONE, 0), "no page: the ring is off, whatever the values are");

	// The scan and the firmware without the API
	stage();
	item = probe();
	limit(&item->crit_hi, 80);
	seen("{\"X\":80}", 500);
	view_no_api();
	build();
	check(scene->items[0].tone == SCENE_TONE_ALARM && ring_is(RING_GREY, 0), "a firmware without the API: a value at its crit limit has the tone alarm, the ring stays grey");
	stage();
	item = probe();
	limit(&item->warn_hi, 80);
	seen("{\"X\":80}", 500);
	view_scan();
	build();
	check(scene->items[0].tone == SCENE_TONE_DIM && strcmp(scene->items[0].text, "80") == 0, "during a scan a value at its warn limit is dimmed");
	seen("{\"X\":1}", 500);
	build();
	check(scene->items[0].tone == SCENE_TONE_DIM && strcmp(scene->items[0].text, "1") == 0, "during a scan a value within its limits is dimmed");

	// What the ring takes for old: an old value and a dash, not what the profile does not provide
	for(int i = 0; i < COUNT(gone); i++)
	{
		stage();
		item = probe();
		SET(item->key, "COOLANT_TMP");
		seen("{\"COOLANT_TMP\":88.4}", gone[i].age_ms);
		build();
		snprintf(what, sizeof(what), "a page of one value of the profile, seen %lu ms ago: %s", (unsigned long)gone[i].age_ms, gone[i].rule);
		check(scene->item_count == 1 && strcmp(scene->items[0].text, gone[i].text) == 0 && ring_is(gone[i].ring, 0), what);
	}
	stage();
	item = probe();
	SET(item->key, "COOLANT_TMP");
	values_init(&values);
	build();
	check(strcmp(scene->items[0].text, SCENE_DASH) == 0 && ring_is(RING_YELLOW, 0), "a page of one value of the profile that never arrived: a dash, the ring yellow");
	SET(item->key, "TRANS_TEMP");
	build();
	check(strcmp(scene->items[0].text, SCENE_UNAVAILABLE) == 0 && ring_is(RING_NONE, 0), "a page of one value the profile does not provide: n. v., the ring off - nothing is missing");
	world.catalog = &unloaded;
	build();
	check(strcmp(scene->items[0].text, SCENE_DASH) == 0 && ring_is(RING_YELLOW, 0), "the same value before the profile arrived: a dash, the ring yellow");

	// Whichever value of the page it is
	for(int i = 0; i < COUNT(pairs); i++)
	{
		stage();
		seen("{\"COOLANT_TMP\":88.4}", 10000);
		seen("{\"FUEL_L\":43}", 5000);
		item = probe();
		SET(item->key, pairs[i].first);
		second = probe_more(pairs[i].second);
		build();
		snprintf(what, sizeof(what), "a page of two values, %s: the ring is %s", pairs[i].rule, pairs[i].ring == RING_YELLOW ? "yellow" : "off");
		check(scene->item_count == 2 && strcmp(scene->items[0].text, pairs[i].first_text) == 0 && strcmp(scene->items[1].text, pairs[i].second_text) == 0 && ring_is(pairs[i].ring, 0), what);
	}

	// A limit goes before a dash, as it goes before an old value
	stage();
	seen("{\"COOLANT_TMP\":88.4}", 10000);
	item = probe();
	SET(item->key, "ENGINE_RPM");
	limit(&item->warn_hi, 800);
	second = probe_more("COOLANT_TMP");
	build();
	check(scene->items[0].tone == SCENE_TONE_WARN && strcmp(scene->items[1].text, SCENE_DASH) == 0 && ring_is(RING_YELLOW, 0), "a value at its warn limit next to a dash: the ring is yellow");
	limit(&item->crit_hi, 812);
	build();
	check(scene->items[0].tone == SCENE_TONE_ALARM && strcmp(scene->items[1].text, SCENE_DASH) == 0 && ring_is(RING_RED, 0), "a value at its crit limit next to a dash: the ring is red");

	// Only in the view LIVE the ring tells of the values, and only on their page
	stage();
	item = probe();
	SET(item->key, "COOLANT_TMP");
	values_init(&values);
	view_no_api();
	build();
	check(strcmp(scene->items[0].text, SCENE_DASH) == 0 && ring_is(RING_GREY, 0), "a page of one dash with a firmware without the API: the ring stays grey");
	view_scan();
	build();
	check(strcmp(scene->items[0].text, SCENE_DASH) == 0 && ring_is(RING_PROGRESS, 277), "a page of one dash during a scan: the ring is the progress of the scan");
	stage();
	item = probe();
	SET(item->key, "COOLANT_TMP");
	values_init(&values);
	nav.screen = NAV_MENU;
	build();
	check(ring_is(RING_NONE, 0), "the menu over a page of one dash: the ring is off, the page is not on the screen");
	nav.screen = NAV_INFO;
	build();
	check(ring_is(RING_NONE, 0), "the info over a page of one dash: the ring is off");

	// Whatever the widget, and whatever else the display has to say
	stage();
	item = probe();
	SET(item->key, "DPF_REGEN_STATUS");
	item->widget = LAYOUT_WIDGET_STATE;
	seen("{\"DPF_REGEN_STATUS\":1}", 10000);
	build();
	check(item_is(0, "Dpf Regen Status", SCENE_DASH, "", SCENE_TONE_DIM, LAYOUT_WIDGET_STATE, -1) && ring_is(RING_YELLOW, 0), "a page of one state that is gone: a dash, the ring yellow");
	input.safe_mode = true;
	build();
	check(ring_is(RING_YELLOW, 0) && strcmp(scene->note, SAFE_NOTE) == 0, "a page of one dash in safe mode: the ring is yellow all the same");
	input.safe_mode = false;
	input.heat = GUARD_HEAT_DIM;
	build();
	check(ring_is(RING_YELLOW, 0) && strcmp(scene->note, HOT_NOTE) == 0, "a page of one dash when it is too hot: the ring is yellow all the same");
	stage();
	item = probe();
	SET(item->key, "BOOST_PRESSURE");
	SET(item->label, "Ladedruck");
	item->scale = 0.001;
	item->decimals = 2;
	item->has_unit = true;
	SET(item->unit, "bar");
	item->map_count = 1;
	SET(item->map[0].raw, "*");
	SET(item->map[0].text, "da");
	world.night_mode = true;
	seen("{\"BOOST_PRESSURE\":1013}", 10000);
	build();
	check(item_is(0, "Ladedruck", SCENE_DASH, "", SCENE_TONE_DIM, LAYOUT_WIDGET_NUMBER, -1) && ring_is(RING_YELLOW, 0),
	      "a page of one value with a label, a scale, decimals, a unit and a map that is gone, in night mode: a dash, the ring yellow");
}

static void test_ranges(void)
{
	static const struct
	{
		const char *value;
		double scale;
		layout_widget_t widget;
		bool has_min;
		double min;
		bool has_max;
		double max;
		int permille;
		const char *rule;
	} ranges[] = {
		{"50", 1, LAYOUT_WIDGET_ARC, true, 0, true, 100, 500, "an arc, 50 of 0..100"},
		{"50", 1, LAYOUT_WIDGET_BAR, true, 0, true, 100, 500, "a bar, 50 of 0..100"},
		{"0", 1, LAYOUT_WIDGET_ARC, true, 0, true, 100, 0, "a value at min"},
		{"-0.5", 1, LAYOUT_WIDGET_ARC, true, 0, true, 100, 0, "a value just below min"},
		{"-1000000000", 1, LAYOUT_WIDGET_BAR, true, 0, true, 100, 0, "a value far below min"},
		{"100", 1, LAYOUT_WIDGET_ARC, true, 0, true, 100, 1000, "a value at max"},
		{"100.5", 1, LAYOUT_WIDGET_ARC, true, 0, true, 100, 1000, "a value just above max"},
		{"99999999999", 1, LAYOUT_WIDGET_BAR, true, 0, true, 100, 1000, "a value far above max"},
		{"0.5", 1, LAYOUT_WIDGET_ARC, true, 0, true, 100, 5, "0.5 of 0..100"},
		{"99.5", 1, LAYOUT_WIDGET_ARC, true, 0, true, 100, 995, "99.5 of 0..100"},
		{"99.9990234375", 1, LAYOUT_WIDGET_ARC, true, 0, true, 100, 999, "999.99 permille are rounded down to 999"},
		{"0.0009765625", 1, LAYOUT_WIDGET_ARC, true, 0, true, 100, 0, "0.0098 permille are rounded down to 0"},
		{"0.125", 1, LAYOUT_WIDGET_ARC, true, 0, true, 100, 1, "1.25 permille are rounded down to 1"},
		{"0.1875", 1, LAYOUT_WIDGET_ARC, true, 0, true, 100, 1, "1.875 permille are rounded down to 1"},
		{"33", 1, LAYOUT_WIDGET_BAR, true, 0, true, 99, 333, "33 of 0..99"},
		{"25", 1, LAYOUT_WIDGET_ARC, true, 20, true, 30, 500, "25 of 20..30"},
		{"21", 1, LAYOUT_WIDGET_ARC, true, 20, true, 30, 100, "21 of 20..30"},
		{"20", 1, LAYOUT_WIDGET_ARC, true, 20, true, 30, 0, "20 of 20..30"},
		{"19", 1, LAYOUT_WIDGET_ARC, true, 20, true, 30, 0, "19 of 20..30"},
		{"30", 1, LAYOUT_WIDGET_ARC, true, 20, true, 30, 1000, "30 of 20..30"},
		{"31", 1, LAYOUT_WIDGET_ARC, true, 20, true, 30, 1000, "31 of 20..30"},
		{"0", 1, LAYOUT_WIDGET_BAR, true, -40, true, 40, 500, "0 of -40..40"},
		{"-20", 1, LAYOUT_WIDGET_BAR, true, -40, true, 40, 250, "-20 of -40..40"},
		{"-40", 1, LAYOUT_WIDGET_BAR, true, -40, true, 40, 0, "-40 of -40..40"},
		{"40", 1, LAYOUT_WIDGET_BAR, true, -40, true, 40, 1000, "40 of -40..40"},
		{"-75", 1, LAYOUT_WIDGET_ARC, true, -100, true, -50, 500, "-75 of -100..-50"},
		{"100", 0.5, LAYOUT_WIDGET_ARC, true, 0, true, 100, 500, "100 with scale 0.5 of 0..100: the shown value counts"},
		{"30", 2, LAYOUT_WIDGET_ARC, true, 0, true, 100, 600, "30 with scale 2 of 0..100"},
		{"25", -1, LAYOUT_WIDGET_ARC, true, -100, true, 0, 750, "25 with scale -1 of -100..0"},
		{"\"on\"", 1, LAYOUT_WIDGET_BAR, true, 0, true, 1, 1000, "on of 0..1: on is 1"},
		{"\"off\"", 1, LAYOUT_WIDGET_BAR, true, 0, true, 1, 0, "off of 0..1: off is 0"},
		{"\"on\"", 1, LAYOUT_WIDGET_BAR, true, 0, true, 2, 500, "on of 0..2"},
		{"\"off\"", 1, LAYOUT_WIDGET_BAR, true, -1, true, 1, 500, "off of -1..1"},
		{"\"on\"", 50, LAYOUT_WIDGET_ARC, true, 0, true, 100, 500, "on with scale 50 of 0..100"},
		{"50", 1, LAYOUT_WIDGET_ARC, false, 0, true, 100, -1, "an arc without min has no range"},
		{"50", 1, LAYOUT_WIDGET_ARC, true, 0, false, 100, -1, "an arc without max has no range"},
		{"50", 1, LAYOUT_WIDGET_BAR, false, 0, false, 100, -1, "a bar without min and max has no range"},
		{"50", 1, LAYOUT_WIDGET_ARC, true, 50, true, 50, -1, "min equal to max is no range"},
		{"50", 1, LAYOUT_WIDGET_ARC, true, 100, true, 0, -1, "min above max is no range"},
		{"50", 1, LAYOUT_WIDGET_NUMBER, true, 0, true, 100, -1, "a number with min and max has no position"},
		{"50", 1, LAYOUT_WIDGET_STATE, true, 0, true, 100, -1, "a state with min and max has no position"},
		{"50", 1, LAYOUT_WIDGET_ARC, true, 0, true, 1e306, 0, "50 of 0..1e306 is 0: the calculation stays within a double"},
		{"50", 1, LAYOUT_WIDGET_ARC, true, -1e306, true, 1e306, -1, "limits of -1e306 and 1e306 are no range: the calculation leaves the numbers of a double"},
		{"50", 1, LAYOUT_WIDGET_ARC, true, -1.7e308, true, 1.7e308, -1, "limits of -1.7e308 and 1.7e308 are no range"},
		{"50", 1, LAYOUT_WIDGET_BAR, true, -1e306, true, 100, -1, "a min of -1e306 is no range, whatever max is"},
		{"0", 1, LAYOUT_WIDGET_ARC, true, -1.7e305, true, 3.4e305, 333, "0 of -1.7e305..3.4e305 is a third: a thousand times the distance to min is still a number of a double"},
		{"0", 1, LAYOUT_WIDGET_ARC, true, -1.8e305, true, 3.4e305, -1, "a min of -1.8e305 is no range: a thousand times the distance to it is no number of a double"},
		{"50", 1, LAYOUT_WIDGET_ARC, true, -1e292, true, 1.7976931348623157e308, -1, "limits whose distance is no number of a double are no range, although the value lies next to min"},
	};
	layout_item_t *item;

	for(int i = 0; i < COUNT(ranges); i++)
	{
		char json[64];

		stage();
		item = probe();
		item->scale = ranges[i].scale;
		item->widget = ranges[i].widget;
		item->min.set = ranges[i].has_min;
		item->min.value = ranges[i].min;
		item->max.set = ranges[i].has_max;
		item->max.value = ranges[i].max;
		snprintf(json, sizeof(json), "{\"X\":%s}", ranges[i].value);
		seen(json, 500);
		build();
		snprintf(what, sizeof(what), "permille of a value: %s -> %d", ranges[i].rule, ranges[i].permille);
		check(scene->item_count == 1 && scene->items[0].permille == ranges[i].permille && scene->items[0].widget == ranges[i].widget &&
		      strcmp(scene->items[0].text, SCENE_DASH) != 0, what);
	}

	stage();
	item = probe();
	item->widget = LAYOUT_WIDGET_ARC;
	limit(&item->min, 0);
	limit(&item->max, 100);
	seen("{\"X\":50}", 3000);
	build();
	check(scene->items[0].permille == 500 && scene->items[0].tone == SCENE_TONE_DIM, "an old value has its position in the range");
	seen("{\"X\":50}", 10000);
	build();
	check(scene->items[0].permille == -1 && scene->items[0].widget == LAYOUT_WIDGET_ARC, "a value that is gone has no position in the range, and stays an arc");
	seen("{\"X\":1e12}", 500);
	build();
	check(scene->items[0].permille == -1, "a value that cannot be printed has no position in the range");
	seen("{\"X\":50}", 500);
	item->min.value = (double)NAN;
	build();
	check(scene->items[0].permille == -1, "a min that is no number is no range");
	item->min.value = 0;
	item->max.value = (double)NAN;
	build();
	check(scene->items[0].permille == -1, "a max that is no number is no range");
	item->min.value = (double)-INFINITY;
	item->max.value = (double)INFINITY;
	build();
	check(scene->items[0].permille == -1, "limits that are not finite are no range");
	item->min.value = 0;
	build();
	check(scene->items[0].permille == -1, "a max that is not finite is no range, although min is 0");
	view_scan();
	item->min.value = 0;
	item->max.value = 100;
	build();
	check(scene->items[0].permille == 500 && scene->items[0].tone == SCENE_TONE_DIM, "during a scan a value keeps its position in the range");

	// A kind that values.h does not know counts as off for the text (layout.h), and so it does for the range
	stage();
	item = probe();
	item->widget = LAYOUT_WIDGET_BAR;
	limit(&item->min, -50);
	limit(&item->max, 50);
	seen("{\"X\":25}", 500);
	for(int i = 0; i < values.count; i++)
	{
		if(strcmp(values.items[i].name, "X") == 0) values.items[i].kind = (value_kind_t)3;
	}
	build();
	check(item_is(0, "X", "0", "", SCENE_TONE_NORMAL, LAYOUT_WIDGET_BAR, 500), "a value whose kind is none of values.h is shown as off: the text 0, and the position of 0 in the range, not that of its number");
}

/* The notices on the value pages ----------------------------------------------------------------------- */

static void test_notices(void)
{
	static const struct
	{
		int32_t batt_mv;
		const char *line;
	} batteries[] = {
		{12400, "Bordnetz 12,4 V"}, {12449, "Bordnetz 12,4 V"}, {12450, "Bordnetz 12,5 V"}, {12999, "Bordnetz 13,0 V"}, {9949, "Bordnetz 9,9 V"},
		{9950, "Bordnetz 10,0 V"}, {0, "Bordnetz 0,0 V"}, {49, "Bordnetz 0,0 V"}, {50, "Bordnetz 0,1 V"}, {1, "Bordnetz 0,0 V"}, {100000, "Bordnetz 100,0 V"},
		{INT32_MAX, "Bordnetz 2147483,6 V"}, {-1, NULL}, {-2, NULL}, {INT32_MIN, NULL},
	};
	static const int no_pages[] = {-1, 8, 9, 100, -2, INT_MIN, INT_MAX, 256, 65536, -256};

	stage();
	view_no_wifi();
	screen("notice_no_wifi", "in no network: the notice of it, no title, the ring red, the dots of the pages");
	stage();
	view_connecting();
	screen("notice_connecting", "no answer yet: the notice of it, the ring yellow");
	stage();
	view_no_answer();
	nav.page = 3;
	screen("notice_no_answer", "three rounds without an answer: the notice of it, the ring red, the dot of the page the knob is on");
	stage();
	view_foreign();
	screen("notice_foreign", "another adapter answers: the notice of it, the ring red");
	stage();
	view_autopid_off();
	screen("notice_autopid_off", "AutoPID is off: the notice of it, the ring grey");
	stage();
	view_starting();
	screen("notice_starting", "the adapter starts: the notice of it, the ring yellow");
	stage();
	view_ecu_offline();
	screen("notice_ecu_offline", "ignition off: the notice of it and the battery voltage of the state, the ring grey");
	stage();
	adapter.batt_mv = -1;
	view_ecu_offline();
	screen("notice_ecu_offline_no_battery", "ignition off and no battery voltage measured: the notice alone");

	stage();
	nav.page = -1;
	screen("notice_no_page", "live, but the layout shows no page the knob is on: the notice of it, the ring off, no dot lit");
	stage();
	view_scan();
	nav.page = -1;
	screen("notice_no_page_scan", "no page during a scan: the notice of it, the note of the scan, the ring its progress");
	stage();
	view_no_api();
	nav.page = -1;
	screen("notice_no_page_no_api", "no page with a firmware without the API: the notice of it, the note of the firmware");
	stage();
	view_no_wifi();
	nav.page = -1;
	screen("notice_no_page_no_wifi", "no page and no network: the notice of the network, not that of the page");
	stage();
	world.layout = &hidden_layout;
	nav.page = -1;
	screen("notice_no_views", "a layout of hidden pages only: the notice of it, and no dots at all");
	stage();
	view_ecu_offline();
	input.heat = GUARD_HEAT_DIM;
	screen("notice_hot", "too hot with the ignition off: the note of the heat under the notice");
	stage();
	view_no_wifi();
	input.safe_mode = true;
	screen("notice_safe_mode", "safe mode and no network: the note of the safe mode under the notice");

	for(int i = 0; i < COUNT(batteries); i++)
	{
		stage();
		adapter.batt_mv = batteries[i].batt_mv;
		view_ecu_offline();
		build();
		snprintf(what, sizeof(what), "ignition off with a battery voltage of %ld mV: %s%s", (long)batteries[i].batt_mv, batteries[i].line != NULL ? batteries[i].line : "no line of it",
		         batteries[i].line != NULL ? ", rounded to a tenth of a volt" : "");
		check(head_is(SCENE_NOTICE, "", "") && lines_are("Zündung aus – Motorsteuergerät offline", batteries[i].line, NULL, NULL), what);
	}

	// Only the notice of the ignition has the voltage
	for(int i = 0; i < COUNT(VIEWS); i++)
	{
		if(VIEWS[i].values || VIEWS[i].view == CONN_VIEW_ECU_OFFLINE) continue;

		stage();
		VIEWS[i].make();
		build();
		snprintf(what, sizeof(what), "the value pages in the view %s: a notice with the text of the view as its only line, without title, the ring of the view", VIEWS[i].name);
		check(head_is(SCENE_NOTICE, "", "") && lines_are(VIEWS[i].text, NULL, NULL, NULL) && ring_is(VIEWS[i].ring, 0) && scene->dots == 6 && scene->dot == 0, what);
	}

	// Pages that are none
	for(int i = 0; i < COUNT(no_pages); i++)
	{
		stage();
		nav.page = no_pages[i];
		build();
		snprintf(what, sizeof(what), "page %d of a layout of eight pages: the notice that there is no view, six dots, none lit", no_pages[i]);
		check(head_is(SCENE_NOTICE, "", "") && lines_are(NO_PAGE, NULL, NULL, NULL) && scene->dots == 6 && scene->dot == -1 && scene->item_count == 0, what);
	}
	stage();
	nav.page = 7;
	build();
	check(scene->kind == SCENE_VALUES && strcmp(scene->title, "Fremd") == 0 && scene->item_count == 2, "page 7 of a layout of eight pages is its last page and is shown");
	stage();
	world.layout = &hidden_layout;
	nav.page = 1;
	build();
	check(scene->kind == SCENE_VALUES && strcmp(scene->title, "B") == 0 && scene->dots == 0 && scene->dot == -1, "the last page of a layout of two hidden pages is shown, without dots");
	nav.page = 2;
	build();
	check(head_is(SCENE_NOTICE, "", "") && lines_are(NO_PAGE, NULL, NULL, NULL), "page 2 of a layout of two pages: the notice that there is no view");
	scratch.page_count = 0;
	world.layout = &scratch;
	nav.page = 0;
	build();
	check(head_is(SCENE_NOTICE, "", "") && lines_are(NO_PAGE, NULL, NULL, NULL) && scene->dots == 0 && scene->dot == -1, "page 0 of a layout without pages: the notice that there is no view");

	// The dots
	for(int page = 0; page < 8; page++)
	{
		stage();
		nav.page = page;
		build();
		snprintf(what, sizeof(what), "page %d of the layout: six dots, %s", page, page < 6 ? "the one of the page lit" : "none lit, the page is not in the rotation");
		check(scene->dots == 6 && scene->dot == (page < 6 ? page : -1), what);
	}
	stage();
	world.catalog = &unloaded;
	nav.page = 6;
	build();
	check(scene->dots == 7 && scene->dot == -1, "before the profile arrived the hidden page has no dot among the seven");
	nav.page = 5;
	build();
	check(scene->dots == 7 && scene->dot == 5, "before the profile arrived page 5 is the sixth of seven");
	stage();
	layout.pages[2].hidden = true;
	nav.page = 3;
	build();
	check(scene->dots == 5 && scene->dot == 2, "with page 2 hidden, page 3 is the third of five: the dot is the position among the shown pages, not the number of the page");
	nav.page = 1;
	build();
	check(scene->dots == 5 && scene->dot == 1, "with page 2 hidden, page 1 is the second of five");
	layout.pages[2].hidden = false;

	// The largest layout: twelve pages
	stage();
	probe();
	scratch.page_count = LAYOUT_PAGES_MAX;
	for(int page = 1; page < LAYOUT_PAGES_MAX; page++) scratch.pages[page] = scratch.pages[0];
	SET(scratch.pages[11].title, "Zwölfte");
	world.catalog = &unloaded;
	nav.page = 11;
	build();
	check(LAYOUT_PAGES_MAX == 12 && head_is(SCENE_VALUES, "Zwölfte", "") && scene->dots == 12 && scene->dot == 11, "the last page of a layout of twelve pages: twelve dots, the last one lit");
	nav.page = 8;
	build();
	check(head_is(SCENE_VALUES, "Probe", "") && scene->dots == 12 && scene->dot == 8, "the ninth page of a layout of twelve pages: the ninth dot");
	scratch.pages[9].hidden = true;
	nav.page = 11;
	build();
	check(scene->dots == 11 && scene->dot == 10, "with the tenth of twelve pages hidden the last page is the eleventh of eleven");
	nav.page = 12;
	build();
	check(head_is(SCENE_NOTICE, "", "") && lines_are(NO_PAGE, NULL, NULL, NULL) && scene->dots == 11 && scene->dot == -1, "page 12 of a layout of twelve pages: the notice that there is no view");

	// The note: the first that applies
	for(int i = 0; i < COUNT(VIEWS); i++)
	{
		const char *note = VIEWS[i].view == CONN_VIEW_SCAN || VIEWS[i].view == CONN_VIEW_NO_API ? VIEWS[i].text : "";

		stage();
		VIEWS[i].make();
		build();
		snprintf(what, sizeof(what), "the note of the value pages in the view %s: %s", VIEWS[i].name, note[0] != '\0' ? "the text of the view" : "empty");
		check(strcmp(scene->note, note) == 0, what);
		input.heat = GUARD_HEAT_DIM;
		build();
		snprintf(what, sizeof(what), "the note of the value pages in the view %s when it is too hot: that of the heat", VIEWS[i].name);
		check(strcmp(scene->note, HOT_NOTE) == 0, what);
		input.safe_mode = true;
		build();
		snprintf(what, sizeof(what), "the note of the value pages in the view %s in safe mode when it is too hot: that of the safe mode", VIEWS[i].name);
		check(strcmp(scene->note, SAFE_NOTE) == 0, what);
		input.heat = GUARD_HEAT_NORMAL;
		build();
		snprintf(what, sizeof(what), "the note of the value pages in the view %s in safe mode: that of the safe mode", VIEWS[i].name);
		check(strcmp(scene->note, SAFE_NOTE) == 0, what);
	}
	stage();
	input.heat = GUARD_HEAT_OFF;
	build();
	check(strcmp(scene->note, HOT_NOTE) == 0, "the display switched off for heat has the note of the heat");
	input.heat = (guard_heat_t)3;
	build();
	check(strcmp(scene->note, HOT_NOTE) == 0, "a heat level behind the last of the enum has the note of the heat");
	input.heat = (guard_heat_t)-1;
	build();
	check(strcmp(scene->note, HOT_NOTE) == 0, "a heat level before the first of the enum has the note of the heat");
	input.heat = (guard_heat_t)256;
	build();
	check(strcmp(scene->note, HOT_NOTE) == 0, "a heat level whose low byte is that of normal is not normal");

	// The notes belong to the value pages
	stage_on(NAV_MENU, 0);
	input.safe_mode = true;
	input.heat = GUARD_HEAT_DIM;
	view_scan();
	build();
	check(head_is(SCENE_LIST, "Menü", ""), "the menu has no note, in safe mode, when it is too hot and during a scan");
}

/* The menu and the screens behind it ------------------------------------------------------------------- */

static void test_menu(void)
{
	static const int numbers[] = {0, 5, 100, 101, -1, INT_MAX, INT_MIN};
	static const char *const texts[] = {"0 %", "5 %", "100 %", "101 %", "-1 %", "2147483647 %", "-2147483648 %"};
	bool enabled = true;

	stage_on(NAV_MENU, 0);
	screen("menu_top", "the menu with the focus on its first row: rows 0 to 4 of 7, brightness 80, night mode off, web access locked");
	stage_on(NAV_MENU, 3);
	view_no_wifi();
	world.brightness = 35;
	world.night_mode = true;
	world.release_open = true;
	screen("menu_middle", "the menu with the focus on row 3: rows 1 to 5, the focus in the middle; night mode on, web access released; the ring is that of the connection");
	stage_on(NAV_MENU, 6);
	screen("menu_end", "the menu with the focus on its last row: rows 2 to 6");

	for(int i = 0; i < COUNT(numbers); i++)
	{
		stage_on(NAV_MENU, 0);
		world.brightness = numbers[i];
		build();
		snprintf(what, sizeof(what), "the menu shows a brightness of %d as \"%s\"", numbers[i], texts[i]);
		check(row_is(1, false, SCENE_ROW_ACTION, "Helligkeit", texts[i], true), what);
	}
	stage_on(NAV_MENU, 1);
	world.night_mode = true;
	build();
	check(row_is(2, false, SCENE_ROW_ACTION, "Nachtmodus", "an", true) && row_is(3, false, SCENE_ROW_ACTION, "Web-Zugriff", "gesperrt", true),
	      "night mode on with the web access locked: each row tells its own state");
	world.night_mode = false;
	world.release_open = true;
	build();
	check(row_is(2, false, SCENE_ROW_ACTION, "Nachtmodus", "aus", true) && row_is(3, false, SCENE_ROW_ACTION, "Web-Zugriff", "frei", true),
	      "night mode off with the web access released: each row tells its own state");
	access_open(&gate, NOW);
	world.release_open = false;
	build();
	check(row_is(3, false, SCENE_ROW_ACTION, "Web-Zugriff", "gesperrt", true), "the menu tells the release as the world names it, as the knob will act on it");

	for(int focus = 0; focus <= 6; focus += 6)
	{
		stage_on(NAV_MENU, focus);
		view_no_wifi();
		world.can_read = false;
		world.brightness = 0;
		world.flow = DTC_FLOW_UNKNOWN;
		input.read_block = DTC_FLOW_NO_ADAPTER;
		input.safe_mode = true;
		input.heat = GUARD_HEAT_OFF;
		input.address = NULL;
		build();
		enabled = enabled && scene->row_count == 5;
		for(int i = 0; i < scene->row_count; i++) enabled = enabled && scene->rows[i].enabled && scene->rows[i].kind == SCENE_ROW_ACTION;
	}
	check(enabled, "every row of the menu is enabled, whatever may be read, shown or reached: what a row leads to tells why nothing can be done there");
}

// What a list holds, in the words of the clear dialog and of the start of the fault memory
static const struct
{
	uint32_t codes;
	int units;
	const char *line;
} SUMS[] = {
	{0, 0, "0 Fehler in 0 Steuergeräten"}, {1, 1, "1 Fehler in 1 Steuergerät"}, {2, 1, "2 Fehler in 1 Steuergerät"}, {2, 2, "2 Fehler in 2 Steuergeräten"},
	{128, 24, "128 Fehler in 24 Steuergeräten"}, {4294967295u, 24, "4294967295 Fehler in 24 Steuergeräten"}, {1, -1, "1 Fehler in -1 Steuergeräten"},
	{7, 11, "7 Fehler in 11 Steuergeräten"}, {3, INT_MAX, "3 Fehler in 2147483647 Steuergeräten"},
};

static void test_dtc(void)
{
	static const struct
	{
		dtc_flow_phase_t phase;
		const char *name;
		bool view;
	} phases[] = {
		{DTC_FLOW_IDLE, "IDLE", false}, {DTC_FLOW_READ_SENT, "READ_SENT", false}, {DTC_FLOW_READING, "READING", false}, {DTC_FLOW_LIST, "LIST", true},
		{DTC_FLOW_CLEAR_SENT, "CLEAR_SENT", false}, {DTC_FLOW_CLEARING, "CLEARING", false}, {DTC_FLOW_CLEARED, "CLEARED", true},
		{DTC_FLOW_FAILED, "FAILED", true}, {DTC_FLOW_UNKNOWN, "UNKNOWN", true}, {(dtc_flow_phase_t)9, "9", false}, {(dtc_flow_phase_t)-1, "-1", false},
		{(dtc_flow_phase_t)(256 + DTC_FLOW_LIST), "259", false},
	};
	static const struct
	{
		dtc_flow_block_t block;
		const char *text;
	} blocks[] = {
		{DTC_FLOW_ALLOWED, ""}, {DTC_FLOW_NO_ADAPTER, "WiCAN nicht erreichbar"}, {DTC_FLOW_STARTING, "WiCAN startet noch"},
		{DTC_FLOW_NOT_SUPPORTED, "Profil ohne Fehlerspeicher"}, {DTC_FLOW_BUSY, "Scan läuft bereits"}, {DTC_FLOW_RPM_UNKNOWN, "Drehzahl nicht lesbar"},
		{(dtc_flow_block_t)99, "WiCAN nicht erreichbar"},
	};
	// Where things stand, by the phase of the flow; without a summary
	static const struct
	{
		dtc_flow_phase_t phase;
		const char *name;
		const char *line;
	} stands[] = {
		{DTC_FLOW_IDLE, "IDLE", "Noch nicht gelesen"}, {DTC_FLOW_READ_SENT, "READ_SENT", "Lesen läuft …"}, {DTC_FLOW_READING, "READING", "Lesen läuft …"},
		{DTC_FLOW_LIST, "LIST", "Liste gelesen"}, {DTC_FLOW_CLEAR_SENT, "CLEAR_SENT", "Löschen läuft …"}, {DTC_FLOW_CLEARING, "CLEARING", "Löschen läuft …"},
		{DTC_FLOW_CLEARED, "CLEARED", "Gelöscht"}, {DTC_FLOW_FAILED, "FAILED", "Letzter Auftrag fehlgeschlagen"}, {DTC_FLOW_UNKNOWN, "UNKNOWN", "Stand des Löschens unbekannt"},
		{(dtc_flow_phase_t)9, "9", "Noch nicht gelesen"}, {(dtc_flow_phase_t)-1, "-1", "Noch nicht gelesen"}, {(dtc_flow_phase_t)(256 + DTC_FLOW_LIST), "259", "Noch nicht gelesen"},
		{(dtc_flow_phase_t)(256 + DTC_FLOW_READING), "258", "Noch nicht gelesen"}, {(dtc_flow_phase_t)(256 + DTC_FLOW_UNKNOWN), "264", "Noch nicht gelesen"},
	};
	static const int no_rows[] = {-1, 4, INT_MAX, INT_MIN};
	dtc_summary_t summary = {0, 0, 0, 0};

	stage_on(NAV_DTC, 0);
	screen("dtc_start", "the fault memory before anything was read: reading is offered, nothing to look at, and the line says that nothing was read");
	stage_on(NAV_DTC, 2);
	world.can_read = false;
	input.read_block = DTC_FLOW_ENGINE_RUNNING;
	world.flow = DTC_FLOW_LIST;
	flow.phase = DTC_FLOW_LIST;
	input.summary = &mixed_summary;
	world.old_lines = codes_count;
	input.old = codes_lines;
	screen("dtc_blocked", "the fault memory with the engine running: reading is not offered and the note tells why; a list and an old list can be looked at, and the line says what the list holds");
	stage_on(NAV_DTC, 3);
	view_ecu_offline();
	world.can_read = false;
	input.read_block = DTC_FLOW_ECU_OFFLINE;
	screen("dtc_offline", "the fault memory with the ignition off: only the way back does something, the ring is grey");
	stage_on(NAV_DTC, 0);
	world.flow = flow.phase = DTC_FLOW_READING;
	flow.seq = 41;
	world.can_read = false;
	input.read_block = DTC_FLOW_BUSY;
	view_scan();
	screen("dtc_reading", "the fault memory while the own read runs: the line says so, nothing is offered; this screen has no arc of its own, so the ring shows the progress");
	stage_on(NAV_DTC, 3);
	world.flow = flow.phase = DTC_FLOW_CLEARING;
	flow.seq = 42;
	world.can_read = false;
	input.read_block = DTC_FLOW_BUSY;
	scan(WICAN_DTC_RUNNING, 42, true, 4, 18, "N15/5 Wählhebelmodul");
	answer(CONN_VIEW_SCAN);
	screen("dtc_clearing", "the fault memory while the own clear runs: the line says so, the ring shows the progress");
	stage_on(NAV_DTC, 1);
	have_list(mixed_lines, mixed_count);
	screen("dtc_list_no_summary", "the fault memory with a list and no summary of it: \"Liste gelesen\"");
	stage_on(NAV_DTC, 1);
	have_list(codes_lines, codes_count);
	summary.codes = 1;
	summary.ecus_with_codes = 1;
	input.summary = &summary;
	screen("dtc_list_one", "the fault memory with a list of one trouble code in one control unit: \"1 Steuergerät\", as the clear dialog says it");
	stage_on(NAV_DTC, 1);
	world.flow = flow.phase = DTC_FLOW_CLEARED;
	world.cleared_lines = cleared_count;
	input.cleared = cleared_lines;
	world.old_lines = codes_count;
	input.old = codes_lines;
	screen("dtc_cleared", "the fault memory after the own clear: \"Gelöscht\"; the outcome and the list before it can be looked at");
	stage_failed(DTC_FLOW_FAILED, "engine_running");
	nav.screen = NAV_DTC;
	nav.row = 1;
	world.can_read = false;
	input.read_block = DTC_FLOW_ENGINE_RUNNING;
	screen("dtc_failed", "the fault memory after a request that failed: the line says that it failed, not why - the failure can be looked at");
	stage_failed(DTC_FLOW_UNKNOWN, "");
	nav.screen = NAV_DTC;
	nav.row = 1;
	view_no_answer();
	world.can_read = false;
	input.read_block = DTC_FLOW_NO_ADAPTER;
	screen("dtc_unknown", "the fault memory after a clear whose outcome is not known: the line says so, the ring is that of the connection");

	for(int i = 0; i < COUNT(phases); i++)
	{
		stage_on(NAV_DTC, 0);
		world.flow = phases[i].phase;
		// The phase of the flow itself says the opposite: the world decides, as it does for the knob
		flow.phase = phases[i].view ? DTC_FLOW_IDLE : DTC_FLOW_LIST;
		build();
		snprintf(what, sizeof(what), "\"Liste ansehen\" with the world in the phase %s: %s", phases[i].name, phases[i].view ? "enabled" : "disabled");
		check(row_is(1, false, SCENE_ROW_ACTION, "Liste ansehen", "", phases[i].view) && row_is(0, true, SCENE_ROW_ACTION, "Lesen", "", true), what);
	}
	for(int i = 0; i < COUNT(stands); i++)
	{
		stage_on(NAV_DTC, 0);
		flow.phase = stands[i].phase;
		// The world says another phase, and a reason is left in the flow: the line follows the phase of the flow
		world.flow = stands[i].phase == DTC_FLOW_FAILED ? DTC_FLOW_IDLE : DTC_FLOW_FAILED;
		SET(flow.reason, "busy");
		build();
		snprintf(what, sizeof(what), "the line of the fault memory with the flow in the phase %s, without a summary: \"%s\", above the four rows", stands[i].name, stands[i].line);
		check(head_is(SCENE_LIST, "Fehlerspeicher", "") && lines_are(stands[i].line, NULL, NULL, NULL) && scene->total == 4 && scene->row_count == 4, what);
		input.summary = &mixed_summary;
		build();
		snprintf(what, sizeof(what), "the line of the fault memory with the flow in the phase %s, with a summary: %s", stands[i].name,
		         stands[i].phase == DTC_FLOW_LIST ? "what the list holds" : "the same, the summary is not told");
		check(lines_are(stands[i].phase == DTC_FLOW_LIST ? "10 Fehler in 3 Steuergeräten" : stands[i].line, NULL, NULL, NULL), what);
	}
	for(int i = 0; i < COUNT(SUMS); i++)
	{
		stage_on(NAV_DTC, 0);
		have_list(mixed_lines, mixed_count);
		summary.codes = SUMS[i].codes;
		summary.ecus_with_codes = SUMS[i].units;
		// The other numbers of a summary are not the ones the line names
		summary.ecus_not_ok = 5;
		summary.ecus_clean = 9;
		input.summary = &summary;
		build();
		snprintf(what, sizeof(what), "the line of the fault memory names the numbers of the list as the clear dialog does: \"%s\"", SUMS[i].line);
		check(lines_are(SUMS[i].line, NULL, NULL, NULL), what);
	}
	stage_on(NAV_DTC, 0);
	world.flow = DTC_FLOW_LIST;
	world.list_lines = mixed_count;
	input.list = mixed_lines;
	input.summary = &mixed_summary;
	build();
	check(lines_are("Noch nicht gelesen", NULL, NULL, NULL) && row_is(1, false, SCENE_ROW_ACTION, "Liste ansehen", "", true),
	      "a list, its lines and its summary named by the world while the flow is idle: the line is that of the flow, the row follows the world");
	stage_on(NAV_DTC, 0);
	flow.phase = DTC_FLOW_LIST;
	world.can_read = false;
	input.read_block = DTC_FLOW_ENGINE_RUNNING;
	input.clear_block = DTC_FLOW_NO_CODES;
	build();
	check(head_is(SCENE_LIST, "Fehlerspeicher", "Motor läuft – nur bei Motor aus") && lines_are("Liste gelesen", NULL, NULL, NULL),
	      "the line of the fault memory stands next to the note: the note tells why nothing can be read, the line what was read");
	stage_failed(DTC_FLOW_FAILED, "");
	nav.screen = NAV_DTC;
	build();
	check(lines_are("Letzter Auftrag fehlgeschlagen", NULL, NULL, NULL), "the line of the fault memory after a request that failed without a reason: that it failed");
	stage_on(NAV_DTC, 0);
	world.old_lines = codes_count;
	input.old = codes_lines;
	world.cleared_lines = cleared_count;
	input.cleared = cleared_lines;
	build();
	check(lines_are("Noch nicht gelesen", NULL, NULL, NULL) && row_is(2, false, SCENE_ROW_ACTION, "Zuletzt gelöscht", "", true),
	      "an idle flow next to an old list and the lines of an outcome: the line follows the phase");
	stage_on(NAV_DTC, 0);
	flow.seq = 41;
	flow.read_seq = 41;
	flow.list_count = 10;
	flow.list_end_ms = NOW - 48000;
	build();
	check(lines_are("Noch nicht gelesen", NULL, NULL, NULL), "an idle flow that still holds the numbers of a list: the line follows the phase, not the numbers");
	for(int i = 0; i < COUNT(no_rows); i++)
	{
		stage_on(NAV_DTC, no_rows[i]);
		build();
		snprintf(what, sizeof(what), "the fault memory with the focus on row %d, which is none: the line is there all the same", no_rows[i]);
		check(lines_are("Noch nicht gelesen", NULL, NULL, NULL) && scene->total == 4, what);
	}
	stage_on(NAV_DTC, 0);
	have_list(mixed_lines, mixed_count);
	world.can_clear = true;
	input.clear_block = DTC_FLOW_ALLOWED;
	build();
	check(lines_are("Liste gelesen", NULL, NULL, NULL), "a list without a summary that may be cleared: \"Liste gelesen\"");

	for(int i = 0; i < COUNT(blocks); i++)
	{
		stage_on(NAV_DTC, 0);
		input.read_block = blocks[i].block;
		build();
		snprintf(what, sizeof(what), "the note of the fault memory for the block %d of a read: \"%s\"", (int)blocks[i].block, blocks[i].text);
		check(head_is(SCENE_LIST, "Fehlerspeicher", blocks[i].text), what);
	}

	stage_on(NAV_DTC, 0);
	world.can_read = false;
	build();
	check(row_is(0, true, SCENE_ROW_ACTION, "Lesen", "", false) && scene->note[0] == '\0', "\"Lesen\" follows can_read of the world, not the block: disabled without a note");
	world.can_read = true;
	input.read_block = DTC_FLOW_BUSY;
	input.clear_block = DTC_FLOW_ENGINE_RUNNING;
	build();
	check(row_is(0, true, SCENE_ROW_ACTION, "Lesen", "", true) && strcmp(scene->note, "Scan läuft bereits") == 0, "the note of the fault memory is the block of a read, not that of a clear");

	stage_on(NAV_DTC, 0);
	world.old_lines = 1;
	build();
	check(row_is(2, false, SCENE_ROW_ACTION, "Zuletzt gelöscht", "", true), "\"Zuletzt gelöscht\" with an old list of one line: enabled, also without the lines themselves");
	world.old_lines = 0;
	input.old = codes_lines;
	build();
	check(row_is(2, false, SCENE_ROW_ACTION, "Zuletzt gelöscht", "", false), "\"Zuletzt gelöscht\" with an old list of no line: disabled");
	world.old_lines = -1;
	build();
	check(row_is(2, false, SCENE_ROW_ACTION, "Zuletzt gelöscht", "", false), "\"Zuletzt gelöscht\" with an old list of -1 lines: disabled");
	world.old_lines = INT_MAX;
	build();
	check(row_is(2, false, SCENE_ROW_ACTION, "Zuletzt gelöscht", "", true) && scene->total == 4, "\"Zuletzt gelöscht\" with the largest number of lines: enabled, and the screen still has four rows");
	world.list_lines = 17;
	world.cleared_lines = 7;
	input.list = mixed_lines;
	input.cleared = cleared_lines;
	build();
	check(scene->total == 4 && scene->row_count == 4, "the fault memory has four rows, whatever lists there are");
}

static void test_busy(void)
{
	static const struct
	{
		dtc_flow_phase_t phase;
		const char *name;
		const char *title;
		bool under_way;
	} phases[] = {
		{DTC_FLOW_IDLE, "IDLE", "Fehlerspeicher", false}, {DTC_FLOW_READ_SENT, "READ_SENT", "Fehlerspeicher lesen", true},
		{DTC_FLOW_READING, "READING", "Fehlerspeicher lesen", true}, {DTC_FLOW_LIST, "LIST", "Fehlerspeicher", false},
		{DTC_FLOW_CLEAR_SENT, "CLEAR_SENT", "Fehlerspeicher löschen", true}, {DTC_FLOW_CLEARING, "CLEARING", "Fehlerspeicher löschen", true},
		{DTC_FLOW_CLEARED, "CLEARED", "Fehlerspeicher", false}, {DTC_FLOW_FAILED, "FAILED", "Fehlerspeicher", false},
		{DTC_FLOW_UNKNOWN, "UNKNOWN", "Fehlerspeicher", false}, {(dtc_flow_phase_t)9, "9", "Fehlerspeicher", false},
		{(dtc_flow_phase_t)-1, "-1", "Fehlerspeicher", false}, {(dtc_flow_phase_t)(256 + DTC_FLOW_READING), "258", "Fehlerspeicher", false},
	};
	static const struct
	{
		uint32_t step, total;
		const char *big;
		int permille;
	} steps[] = {
		{1, 18, "1/18", 55}, {9, 18, "9/18", 500}, {17, 18, "17/18", 944}, {18, 18, "18/18", 1000}, {19, 18, "19/18", 1000}, {1, 1, "1/1", 1000},
		{1, 0, "1/0", 0}, {5, 0, "5/0", 0}, {1, 3, "1/3", 333}, {2, 3, "2/3", 666}, {1, 1001, "1/1001", 0}, {999, 1000, "999/1000", 999},
		{4294967295u, 4294967295u, "4294967295/4294967295", 1000}, {4294967294u, 4294967295u, "4294967294/4294967295", 999},
		{4294967, 4294967295u, "4294967/4294967295", 0}, {4294968, 4294967295u, "4294968/4294967295", 1}, {2147483648u, 4294967295u, "2147483648/4294967295", 500},
		{4294967295u, 1, "4294967295/1", 1000},
	};

	stage_busy(DTC_FLOW_READ_SENT, 0);
	screen("busy_read_sent", "a read was sent and not answered yet: no step, \"Auftrag gesendet\", the hint, an empty progress");
	stage_busy(DTC_FLOW_READING, 41);
	scan(WICAN_DTC_QUEUED, 41, false, 0, 0, "");
	answer(CONN_VIEW_SCAN);
	screen("busy_read_queued", "the own read is queued: still \"Auftrag gesendet\"; the ring is off - an arc of nothing at the edge would be a second one");
	stage_busy(DTC_FLOW_READING, 41);
	scan(WICAN_DTC_RUNNING, 41, false, 0, 18, "");
	answer(CONN_VIEW_SCAN);
	screen("busy_read_engine", "the own read runs at step 0: \"0/18\" and \"Prüfe Motor …\"; the ring is off");
	stage_busy(DTC_FLOW_READING, 41);
	view_scan();
	screen("busy_read_running", "the own read at control unit 5 of 18: \"5/18\", its short name, 277 permille - on the arc of the screen, the ring is off");
	stage_busy(DTC_FLOW_READING, 41);
	scan(WICAN_DTC_RUNNING, 41, false, 17, 18, "N2/14 Rückhaltesystem (SRS)");
	answer(CONN_VIEW_SCAN);
	screen("busy_read_last", "the own read at control unit 17 of 18: the short name is what stands in the parentheses; the ring is off");
	stage_busy(DTC_FLOW_READING, 41);
	scan(WICAN_DTC_DONE, 41, false, 18, 18, "");
	adapter.dtc.result_seq = 41;
	answer(CONN_VIEW_LIVE);
	screen("busy_read_done", "the own read is done and its result on its way: \"18/18\", full, no name - not back to \"Auftrag gesendet\"");
	stage_busy(DTC_FLOW_READING, 41);
	scan(WICAN_DTC_RUNNING, 42, false, 3, 18, "N10 SAM");
	answer(CONN_VIEW_SCAN);
	screen("busy_read_overtaken", "the adapter runs another request than the own one: no step of it is shown as the own progress, not by the ring either");
	stage_busy(DTC_FLOW_CLEAR_SENT, 0);
	scan(WICAN_DTC_DONE, 41, false, 18, 18, "");
	answer(CONN_VIEW_LIVE);
	screen("busy_clear_sent", "a clear was sent and not answered yet: the title of the clear, \"Auftrag gesendet\"");
	stage_busy(DTC_FLOW_CLEARING, 42);
	scan(WICAN_DTC_RUNNING, 42, true, 4, 18, "N15/5 Wählhebelmodul");
	answer(CONN_VIEW_SCAN);
	screen("busy_clear_running", "the own clear at control unit 4 of 18: the title of the clear, the name without its component designation; the ring is off");
	stage_busy(DTC_FLOW_LIST, 41);
	screen("busy_over", "the progress while no request is under way any more: a title without action, no step, no line");

	for(int i = 0; i < COUNT(phases); i++)
	{
		// The world says the opposite: the title follows the flow, whose number and reason are shown with it
		stage_busy(phases[i].phase, 0);
		world.flow = phases[i].under_way ? DTC_FLOW_IDLE : DTC_FLOW_READING;
		build();
		snprintf(what, sizeof(what), "the progress in the phase %s: title \"%s\", %s", phases[i].name, phases[i].title,
		         phases[i].under_way ? "\"Auftrag gesendet\" and the hint" : "no line");
		check(head_is(SCENE_PROGRESS, phases[i].title, "") && strcmp(scene->big, "…") == 0 && scene->permille == 0 &&
		      (phases[i].under_way ? lines_are("Auftrag gesendet", HINT, NULL, NULL) : lines_are(NULL, NULL, NULL, NULL)), what);
	}

	// Whose request the state shows, and in which phase of the flow
	for(int phase = DTC_FLOW_IDLE; phase <= DTC_FLOW_UNKNOWN; phase++)
	{
		bool accepted = phase == DTC_FLOW_READING || phase == DTC_FLOW_CLEARING;

		stage_busy((dtc_flow_phase_t)phase, 41);
		view_scan();
		build();
		snprintf(what, sizeof(what), "the adapter runs request 41 at 5/18 and the flow has the number 41 in phase %d: %s", phase,
		         accepted ? "the step is shown" : "no step is shown, the request is not an accepted one");
		check(strcmp(scene->big, accepted ? "5/18" : "…") == 0 && scene->permille == (accepted ? 277 : 0), what);
	}
	for(int state = WICAN_DTC_IDLE; state <= WICAN_DTC_ERROR + 1; state++)
	{
		bool shown = state == WICAN_DTC_RUNNING || state == WICAN_DTC_DONE;

		stage_busy(DTC_FLOW_CLEARING, 41);
		scan((wican_dtc_phase_t)state, 41, true, 5, 18, "N30/4 ESP");
		answer(state == WICAN_DTC_QUEUED || state == WICAN_DTC_RUNNING ? CONN_VIEW_SCAN : CONN_VIEW_LIVE);
		build();
		snprintf(what, sizeof(what), "the own clear with the state of the adapter in phase %d: %s", state, shown ? "its step is shown" : "\"Auftrag gesendet\"");
		check(head_is(SCENE_PROGRESS, "Fehlerspeicher löschen", "") && strcmp(scene->big, shown ? "5/18" : "…") == 0 && scene->permille == (shown ? 277 : 0) &&
		      lines_are(shown ? "ESP" : "Auftrag gesendet", HINT, NULL, NULL), what);
	}
	stage_busy(DTC_FLOW_READING, 40);
	view_scan();
	build();
	check(strcmp(scene->big, "…") == 0 && lines_are("Auftrag gesendet", HINT, NULL, NULL), "the adapter runs request 41, the own one is 40: no step");
	stage_busy(DTC_FLOW_CLEARING, 40);
	view_scan();
	build();
	check(head_is(SCENE_PROGRESS, "Fehlerspeicher löschen", "") && strcmp(scene->big, "…") == 0 && lines_are("Auftrag gesendet", HINT, NULL, NULL),
	      "the adapter runs a read with the number 41, the own request is the clear 40: the title of the own clear, no step");
	stage_busy(DTC_FLOW_READING, 40);
	scan(WICAN_DTC_RUNNING, 41, true, 5, 18, "N30/4 ESP");
	answer(CONN_VIEW_SCAN);
	build();
	check(head_is(SCENE_PROGRESS, "Fehlerspeicher lesen", "") && strcmp(scene->big, "…") == 0, "the adapter runs a clear with the number 41, the own request is the read 40: the title of the own read, no step");
	stage_busy(DTC_FLOW_READING, 41);
	scan(WICAN_DTC_RUNNING, 41, true, 5, 18, "N30/4 ESP");
	answer(CONN_VIEW_SCAN);
	build();
	check(head_is(SCENE_PROGRESS, "Fehlerspeicher lesen", "") && strcmp(scene->big, "5/18") == 0, "the title is that of the own request, a read, also if the state calls the request with its number a clear");
	stage_busy(DTC_FLOW_READING, 42);
	view_scan();
	build();
	check(strcmp(scene->big, "…") == 0 && scene->permille == 0, "the adapter runs request 41, the own one is 42: no step");
	stage_busy(DTC_FLOW_READING, 41 + 256);
	view_scan();
	build();
	check(strcmp(scene->big, "…") == 0 && scene->permille == 0, "the adapter runs request 41, the own one is 297: a number is its whole number");
	stage_busy(DTC_FLOW_READING, 41);
	view_scan();
	view_no_wifi();
	build();
	check(strcmp(scene->big, "…") == 0 && lines_are("Auftrag gesendet", HINT, NULL, NULL) && ring_is(RING_RED, 0), "the own read without a state of the adapter: no step, no crash");
	stage_busy(DTC_FLOW_READING, 41);
	view_no_api();
	build();
	check(strcmp(scene->big, "…") == 0 && lines_are("Auftrag gesendet", HINT, NULL, NULL), "the own read with a firmware that has no state: no step");

	for(int i = 0; i < COUNT(steps); i++)
	{
		stage_busy(DTC_FLOW_READING, 41);
		scan(WICAN_DTC_RUNNING, 41, false, steps[i].step, steps[i].total, "N10 SAM");
		answer(CONN_VIEW_SCAN);
		build();
		snprintf(what, sizeof(what), "the own read at step %lu of %lu: \"%s\", %d permille, the ring off", (unsigned long)steps[i].step, (unsigned long)steps[i].total, steps[i].big, steps[i].permille);
		check(strcmp(scene->big, steps[i].big) == 0 && scene->permille == steps[i].permille && lines_are("SAM", HINT, NULL, NULL) && ring_is(RING_NONE, 0), what);
	}
	stage_busy(DTC_FLOW_READING, 41);
	scan(WICAN_DTC_RUNNING, 41, false, 0, 0, "N3/28 Motorelektronik (CDID3)");
	answer(CONN_VIEW_SCAN);
	build();
	check(strcmp(scene->big, "0/0") == 0 && scene->permille == 0 && lines_are("Prüfe Motor …", HINT, NULL, NULL), "the own read at step 0 of 0: \"0/0\", the engine check whatever name the state has, 0 permille");
	stage_busy(DTC_FLOW_CLEARING, 42);
	scan(WICAN_DTC_RUNNING, 42, true, 0, 18, "N3/28 Motorelektronik (CDID3)");
	answer(CONN_VIEW_SCAN);
	build();
	check(head_is(SCENE_PROGRESS, "Fehlerspeicher löschen", "") && strcmp(scene->big, "0/18") == 0 && scene->permille == 0 && lines_are("Prüfe Motor …", HINT, NULL, NULL),
	      "the own clear at step 0: the engine check, as before a read");
	stage_busy(DTC_FLOW_READING, 41);
	scan(WICAN_DTC_RUNNING, 41, false, 1, 18, "");
	answer(CONN_VIEW_SCAN);
	build();
	check(strcmp(scene->big, "1/18") == 0 && lines_are("", HINT, NULL, NULL), "the own read at step 1 without a name: an empty line in its place, the hint stays the second line");
	stage_busy(DTC_FLOW_READING, 41);
	scan(WICAN_DTC_RUNNING, 41, false, 2, 18, "N73 Elektronisches Zündschloss mit sehr langem Namen (EZS-ABC)");
	answer(CONN_VIEW_SCAN);
	build();
	check(strlen(adapter.dtc.name) == 63 && lines_are("EZS-ABC", HINT, NULL, NULL), "the longest name of a state, 63 bytes: its short name");
	stage_busy(DTC_FLOW_READING, 41);
	scan(WICAN_DTC_RUNNING, 41, false, 2, 18, "Steuergerät für die Überwachung des Reifendrucks vorn rechts");
	answer(CONN_VIEW_SCAN);
	build();
	check(strlen(adapter.dtc.name) == 63 && lines_are("Steuergerät für die Überwachung des Reifendrucks vorn rechts", HINT, NULL, NULL),
	      "a name of 63 bytes without component designation and parentheses is passed on whole");

	// The screen has an arc of its own: the ring never is a second one, and every other ring stays
	for(int v = 0; v < COUNT(VIEWS); v++)
	{
		bool scanning = VIEWS[v].view == CONN_VIEW_SCAN;

		stage_busy(DTC_FLOW_READING, 41);
		VIEWS[v].make();
		build();
		snprintf(what, sizeof(what), "the progress in the view %s: %s", VIEWS[v].name, scanning ? "the ring is off, not a second arc" : "the ring of the view");
		check(scene->kind == SCENE_PROGRESS && ring_is(scanning ? RING_NONE : VIEWS[v].ring, 0), what);
	}
	stage_busy(DTC_FLOW_LIST, 41);
	scan(WICAN_DTC_RUNNING, 42, false, 3, 18, "N10 SAM");
	answer(CONN_VIEW_SCAN);
	build();
	check(head_is(SCENE_PROGRESS, "Fehlerspeicher", "") && scene->permille == 0 && ring_is(RING_NONE, 0),
	      "the progress screen with no request under way while somebody else scans: the ring is off there as well, the screen still has its arc");
	stage_busy(DTC_FLOW_READING, 41);
	view_scan();
	input.safe_mode = true;
	input.heat = GUARD_HEAT_DIM;
	build();
	check(scene->permille == 277 && ring_is(RING_NONE, 0), "the progress during a scan in safe mode and when it is too hot: the ring is off all the same");
	input.safe_mode = false;
	input.heat = GUARD_HEAT_NORMAL;
	world.night_mode = true;
	world.release_open = true;
	build();
	check(scene->permille == 277 && ring_is(RING_NONE, 0), "the progress during a scan in night mode with the web access released: the ring is off all the same");
}

/* Lists ------------------------------------------------------------------------------------------------ */

static void test_list_screens(void)
{
	static const char *const holes[] = {"oben", NULL, "unten"};

	stage_on(NAV_DTC_LIST, 0);
	have_list(empty_lines, empty_count);
	input.clear_block = DTC_FLOW_NO_CODES;
	screen("list_short", "a list without trouble codes: its two lines and the three choices fill the five rows; clearing is not offered and the note tells why");

	stage_list(0);
	screen("list_top", "a list of 17 lines with the focus on its head: rows 0 to 4 of 20, kinds by the lines; the note tells how long clearing is possible");
	stage_list(2);
	screen("list_second", "the focus on row 2: still rows 0 to 4");
	stage_list(3);
	screen("list_third", "the focus on row 3: rows 1 to 5, the focus in the middle");
	stage_list(9);
	screen("list_middle", "the focus on row 9: rows 7 to 11");
	stage_list(16);
	screen("list_last_line", "the focus on the last line: rows 14 to 18, the first two choices below it");
	stage_list(17);
	screen("list_read", "the focus on \"Erneut lesen\": rows 15 to 19, the last five");
	stage_list(18);
	flow.list_end_ms = NOW - 599001;
	screen("list_clear", "the focus on \"Fehler löschen\" with less than a second left: still the last five rows, 0:01");
	stage_list(19);
	world.can_clear = false;
	input.clear_block = DTC_FLOW_LIST_OLD;
	screen("list_back", "the focus on \"Zurück\" of a list that grew old: clearing is not offered, the note tells why, reading is");
	stage_list(18);
	world.can_read = false;
	world.can_clear = false;
	input.read_block = DTC_FLOW_ENGINE_RUNNING;
	input.clear_block = DTC_FLOW_ENGINE_RUNNING;
	screen("list_blocked", "a list with the engine running: neither reading nor clearing is offered, the focus stays where it is");
	stage_list(25);
	screen("list_beyond", "the focus beyond the last row of a list: the last five rows, none of them has the focus");
	stage_list(1);
	input.list = NULL;
	world.can_clear = false;
	input.clear_block = DTC_FLOW_NO_LIST;
	screen("list_no_lines", "a list the world counts 17 lines of, without the lines: the three choices are all rows");

	stage_on(NAV_DTC_CLEARED, 0);
	world.flow = DTC_FLOW_CLEARED;
	flow.phase = DTC_FLOW_CLEARED;
	world.cleared_lines = cleared_count;
	input.cleared = cleared_lines;
	screen("cleared_top", "the outcome of a clear with the focus on its head: rows 0 to 4 of 8");
	nav.row = 7;
	screen("cleared_done", "the outcome of a clear with the focus on \"Fertig\": rows 3 to 7");
	nav.row = 0;
	input.cleared = NULL;
	screen("cleared_no_lines", "the outcome of a clear without its lines: \"Fertig\" is the only row");

	stage_on(NAV_DTC_OLD, 0);
	world.old_lines = codes_count;
	input.old = codes_lines;
	screen("old_top", "the list before the last clear with the focus on its head: rows 0 to 4 of 13");
	nav.row = 12;
	screen("old_back", "the list before the last clear with the focus on \"Zurück\": rows 8 to 12");

	stage_on(NAV_INFO, 0);
	world.info_lines = COUNT(INFO);
	input.info = INFO;
	screen("info_top", "the info with the focus on its first line: rows 0 to 4 of 7, all of them lines");
	nav.row = 6;
	screen("info_end", "the info with the focus on its last line: rows 2 to 6");
	input.info = NULL;
	screen("info_empty", "the info without its texts: no row, although the world counts 7");

	stage_on(NAV_INFO, 1);
	world.info_lines = COUNT(holes);
	input.info = holes;
	build();
	check(scene->total == 3 && row_is(0, false, SCENE_ROW_LINE, "oben", "", true) && row_is(1, true, SCENE_ROW_LINE, "", "", true) && row_is(2, false, SCENE_ROW_LINE, "unten", "", true),
	      "an info text that is NULL is an empty line between its neighbours");
}

static void test_list_rules(void)
{
	static const struct
	{
		dtc_line_kind_t kind;
		scene_row_kind_t row;
		const char *name;
	} kinds[] = {
		{DTC_LINE_HEAD, SCENE_ROW_HEAD, "HEAD becomes a head"}, {DTC_LINE_ECU, SCENE_ROW_LINE, "ECU becomes a line"}, {DTC_LINE_CODE, SCENE_ROW_SUB, "CODE becomes an indented line"},
		{DTC_LINE_NOTE, SCENE_ROW_LINE, "NOTE becomes a line"}, {DTC_LINE_PROBLEM, SCENE_ROW_LINE, "PROBLEM becomes a line"}, {DTC_LINE_CLEAN, SCENE_ROW_LINE, "CLEAN becomes a line"},
		{(dtc_line_kind_t)6, SCENE_ROW_LINE, "a kind behind the last becomes a line"}, {(dtc_line_kind_t)-1, SCENE_ROW_LINE, "a kind before the first becomes a line"},
		{(dtc_line_kind_t)(256 + DTC_LINE_CODE), SCENE_ROW_LINE, "a kind whose low byte is that of CODE becomes a line"},
	};
	static const struct
	{
		dtc_flow_block_t block;
		const char *text;
	} blocks[] = {
		{DTC_FLOW_ALLOWED, ""}, {DTC_FLOW_NO_ADAPTER, "WiCAN nicht erreichbar"}, {DTC_FLOW_BUSY, "Scan läuft bereits"}, {DTC_FLOW_NO_LIST, "Erst lesen, dann löschen"},
		{DTC_FLOW_NO_CODES, "Keine Fehler zu löschen"}, {DTC_FLOW_BUTTON_STUCK, "Knopf klemmt – Löschen gesperrt"},
	};
	static const struct
	{
		uint64_t age_ms;
		const char *note;
	} ages[] = {
		{0, "Löschen möglich: 10:00"}, {1, "Löschen möglich: 10:00"}, {1000, "Löschen möglich: 9:59"}, {1001, "Löschen möglich: 9:59"}, {48000, "Löschen möglich: 9:12"},
		{539000, "Löschen möglich: 1:01"}, {540000, "Löschen möglich: 1:00"}, {540001, "Löschen möglich: 1:00"}, {541000, "Löschen möglich: 0:59"},
		{590000, "Löschen möglich: 0:10"}, {590001, "Löschen möglich: 0:10"}, {591000, "Löschen möglich: 0:09"}, {599000, "Löschen möglich: 0:01"},
		{599999, "Löschen möglich: 0:01"}, {600000, "Löschen möglich: 0:00"}, {900000, "Löschen möglich: 0:00"},
	};
	static dtc_line_t some[3];

	for(int i = 0; i < COUNT(kinds); i++)
	{
		stage_on(NAV_DTC_LIST, 1);
		memset(some, 0, sizeof(some));
		some[0].kind = kinds[i].kind;
		SET(some[0].text, "Text");
		SET(some[0].detail, "Detail");
		have_list(some, 1);
		build();
		snprintf(what, sizeof(what), "a line of a list: %s, with its text and its detail, enabled", kinds[i].name);
		check(scene->total == 4 && row_is(0, false, kinds[i].row, "Text", "Detail", true) && row_is(1, true, SCENE_ROW_ACTION, "Erneut lesen", "", true), what);
	}

	// The longest line a list can have
	stage_on(NAV_DTC_OLD, 0);
	memset(some, 0, sizeof(some));
	some[0].kind = DTC_LINE_ECU;
	memset(some[0].text, 'T', sizeof(some[0].text) - 1);
	memset(some[0].detail, 'D', sizeof(some[0].detail) - 1);
	world.old_lines = 1;
	input.old = some;
	build();
	check(strlen(some[0].text) == 47 && strlen(some[0].detail) == 39 && row_is(0, true, SCENE_ROW_LINE, some[0].text, some[0].detail, true) && row_is(1, false, SCENE_ROW_ACTION, "Zurück", "", true),
	      "the longest line of a list, 47 bytes of text and 39 of detail, is passed on whole");

	for(int i = 0; i < COUNT(blocks); i++)
	{
		stage_list(0);
		world.can_clear = false;
		input.clear_block = blocks[i].block;
		// The block of a read is another one: the note of a list tells why it cannot be cleared
		input.read_block = DTC_FLOW_ENGINE_RUNNING;
		build();
		snprintf(what, sizeof(what), "the note of a list that cannot be cleared for the block %d: \"%s\"", (int)blocks[i].block, blocks[i].text);
		check(head_is(SCENE_LIST, "Fehlerspeicher", blocks[i].text) && row_is(4, false, SCENE_ROW_LINE, "4 Codes nicht übertragen", "", true), what);
	}
	stage_list(18);
	input.clear_block = DTC_FLOW_ENGINE_RUNNING;
	build();
	check(strcmp(scene->note, "Löschen möglich: 9:12") == 0 && row_is(3, true, SCENE_ROW_ACTION, "Fehler löschen", "", true),
	      "\"Fehler löschen\" and the note follow can_clear of the world, not the block");
	world.can_clear = false;
	world.can_read = false;
	build();
	check(row_is(2, false, SCENE_ROW_ACTION, "Erneut lesen", "", false) && row_is(3, true, SCENE_ROW_ACTION, "Fehler löschen", "", false) && row_is(4, false, SCENE_ROW_ACTION, "Zurück", "", true),
	      "a list that can neither be read again nor cleared: both choices disabled, the way back enabled");
	world.can_read = true;
	build();
	check(row_is(2, false, SCENE_ROW_ACTION, "Erneut lesen", "", true) && row_is(3, true, SCENE_ROW_ACTION, "Fehler löschen", "", false), "\"Erneut lesen\" follows can_read, \"Fehler löschen\" can_clear");
	world.can_read = false;
	world.can_clear = true;
	build();
	check(row_is(2, false, SCENE_ROW_ACTION, "Erneut lesen", "", false) && row_is(3, true, SCENE_ROW_ACTION, "Fehler löschen", "", true), "\"Fehler löschen\" follows can_clear, \"Erneut lesen\" can_read");

	for(int i = 0; i < COUNT(ages); i++)
	{
		stage_list(0);
		flow.list_end_ms = NOW - ages[i].age_ms;
		build();
		snprintf(what, sizeof(what), "the note of a list read %lu ms ago: \"%s\"", (unsigned long)ages[i].age_ms, ages[i].note);
		check(strcmp(scene->note, ages[i].note) == 0, what);
	}
	stage_list(0);
	flow.list_end_ms = NOW + 5000;
	build();
	check(strcmp(scene->note, "Löschen möglich: 10:00") == 0, "a list that ended after the time of the scene: no time has passed, 10:00 are left");
	stage_list(0);
	flow.list_count = 0;
	build();
	check(strcmp(scene->note, "Löschen möglich: 9:12") == 0, "clearing allowed by the world while the flow counts no trouble code: the time left - whether it may be cleared is not decided here");
	stage_list(18);
	flow.phase = DTC_FLOW_IDLE;
	build();
	check(strcmp(scene->note, "Löschen möglich: 0:00") == 0 && row_is(3, true, SCENE_ROW_ACTION, "Fehler löschen", "", true),
	      "clearing allowed by the world while the flow has no list: the time left is that of the flow, 0:00, and the row stays enabled as the knob will act on it");
	stage_list(18);
	flow.list_end_ms = NOW - 700000;
	build();
	check(strcmp(scene->note, "Löschen möglich: 0:00") == 0 && row_is(3, true, SCENE_ROW_ACTION, "Fehler löschen", "", true),
	      "clearing allowed by the world while the time of the flow is over: 0:00, and the row stays enabled");
}

// The window of a list a second way: the rows around the focus, moved until they are rows of the screen
static int window_model(int focus, int total)
{
	int first = focus < INT_MIN + 2 ? INT_MIN : focus - 2;

	if(first > total) first = total;
	while(first > 0 && first + SCENE_ROWS_MAX > total) first--;
	while(first < 0) first = first < -1000 ? -1000 : first + 1;
	return first;
}

static void test_windows(void)
{
	static const int far[] = {INT_MIN, INT_MIN + 1, INT_MIN + 2, -1000000, INT_MAX - 2, INT_MAX - 1, INT_MAX, 1000000};
	static const char *texts[14];
	static char numbered[14][16];	// room for any int: gcc refuses a snprintf() that might cut
	int wrong_window = 0, wrong_rows = 0, wrong_focus = 0, compared = 0;

	for(int i = 0; i < COUNT(texts); i++)
	{
		snprintf(numbered[i], sizeof(numbered[i]), "Z%d", i);
		texts[i] = numbered[i];
	}

	// The info: lines alone
	for(int lines = 0; lines <= COUNT(texts); lines++)
	{
		for(int focus = -4; focus <= COUNT(texts) + 4; focus++)
		{
			int first = window_model(focus, lines);
			int visible = lines < SCENE_ROWS_MAX ? lines : SCENE_ROWS_MAX;

			stage_on(NAV_INFO, focus);
			world.info_lines = lines;
			input.info = texts;
			build();
			compared++;
			if(scene->total != lines || scene->first != first || scene->row_count != visible)
			{
				printf("  info of %d lines, focus %d: first %d of %d, %d visible - wanted first %d\n", lines, focus, scene->first, scene->total, scene->row_count, first);
				wrong_window++;
				continue;
			}
			for(int i = 0; i < visible; i++)
			{
				if(strcmp(scene->rows[i].text, texts[first + i]) != 0 || scene->rows[i].kind != SCENE_ROW_LINE || scene->rows[i].detail[0] != '\0' || !scene->rows[i].enabled) wrong_rows++;
				if(scene->rows[i].focus != (first + i == focus)) wrong_focus++;
			}
		}
	}
	check(compared == 345 && wrong_window == 0, "an info of 0 to 14 lines with the focus from -4 to 18: first, total and the number of visible rows are those of a window searched row by row");
	check(wrong_rows == 0, "in each of those windows the visible rows are the lines from `first` on, in their order, as enabled lines without detail");
	check(wrong_focus == 0, "in each of those windows exactly the row the focus is on has it, and none if the focus is on no row");

	// The lists: lines and choices behind them
	wrong_window = wrong_rows = wrong_focus = compared = 0;
	for(int lines = 0; lines <= 9; lines++)
	{
		for(int focus = -3; focus <= 14; focus++)
		{
			static const nav_screen_t screens[] = {NAV_DTC_LIST, NAV_DTC_CLEARED, NAV_DTC_OLD};
			static const char *const choices[][3] = {{"Erneut lesen", "Fehler löschen", "Zurück"}, {"Fertig"}, {"Zurück"}};
			static const int choice_counts[] = {3, 1, 1};

			for(int s = 0; s < COUNT(screens); s++)
			{
				int total = lines + choice_counts[s];
				int first = window_model(focus, total);
				int visible = total < SCENE_ROWS_MAX ? total : SCENE_ROWS_MAX;

				stage_on(screens[s], focus);
				world.list_lines = world.cleared_lines = world.old_lines = lines;
				input.list = input.cleared = input.old = mixed_lines;
				world.can_clear = true;
				build();
				compared++;
				if(scene->total != total || scene->first != first || scene->row_count != visible)
				{
					printf("  screen %d of %d lines, focus %d: first %d of %d, %d visible - wanted first %d of %d\n", (int)screens[s], lines, focus, scene->first, scene->total, scene->row_count, first, total);
					wrong_window++;
					continue;
				}
				for(int i = 0; i < visible; i++)
				{
					int index = first + i;
					const scene_row_t *row = &scene->rows[i];

					if(index < lines ? (strcmp(row->text, mixed_lines[index].text) != 0 || strcmp(row->detail, mixed_lines[index].detail) != 0 || row->kind == SCENE_ROW_ACTION)
					                 : (strcmp(row->text, choices[s][index - lines]) != 0 || row->detail[0] != '\0' || row->kind != SCENE_ROW_ACTION)) wrong_rows++;
					if(row->focus != (index == focus)) wrong_focus++;
				}
			}
		}
	}
	check(compared == 540 && wrong_window == 0, "list, outcome and old list of 0 to 9 lines with the focus from -3 to 14: first, total and the number of visible rows are those of a window searched row by row");
	check(wrong_rows == 0, "in each of those windows the visible rows are the lines and behind them the choices of the screen, from `first` on");
	check(wrong_focus == 0, "in each of those windows exactly the row the focus is on has it");

	// A focus far away
	wrong_window = 0;
	for(int i = 0; i < COUNT(far); i++)
	{
		stage_list(far[i]);
		build();
		if(scene->total != 20 || scene->first != (far[i] < 0 ? 0 : 15) || scene->row_count != 5 || scene->rows[0].focus || scene->rows[4].focus) wrong_window++;
		stage_on(NAV_MENU, far[i]);
		build();
		if(scene->total != 7 || scene->first != (far[i] < 0 ? 0 : 2) || scene->row_count != 5 || scene->rows[0].focus || scene->rows[4].focus) wrong_window++;
		stage_on(NAV_INFO, far[i]);
		world.info_lines = 3;
		input.info = texts;
		build();
		if(scene->total != 3 || scene->first != 0 || scene->row_count != 3 || scene->rows[0].focus || scene->rows[2].focus) wrong_window++;
	}
	check(wrong_window == 0, "a focus at the ends of the numbers, on a list, on the menu and on an info of three lines: the first or the last rows, none with the focus");
	stage_list(-1);
	build();
	check(head_is(SCENE_LIST, "Fehlerspeicher", "Löschen möglich: 9:12") && scene->first == 0 && row_is(0, false, SCENE_ROW_HEAD, "10 Fehler", "8 Steuergeräte · 36 s", true),
	      "a focus before the first row of a list: title, note and rows are as with the focus on a row");

	// Lines the world counts but nobody brought
	stage_on(NAV_DTC_LIST, 2);
	world.list_lines = INT_MAX;
	build();
	check(scene->total == 3 && scene->first == 0 && scene->row_count == 3 && row_is(2, true, SCENE_ROW_ACTION, "Zurück", "", true), "a list of the largest number of lines without the lines: three rows, the choices");
	stage_on(NAV_DTC_OLD, 0);
	world.old_lines = INT_MAX;
	build();
	check(scene->total == 1 && scene->row_count == 1 && row_is(0, true, SCENE_ROW_ACTION, "Zurück", "", true), "an old list of the largest number of lines without the lines: one row, the way back");
	stage_on(NAV_INFO, 0);
	world.info_lines = INT_MAX;
	build();
	check(scene->total == 0 && scene->row_count == 0 && scene->first == 0, "an info of the largest number of lines without the texts: no row");
	stage_on(NAV_DTC_LIST, 1);
	world.list_lines = -5;
	input.list = mixed_lines;
	build();
	check(scene->total == 3 && scene->row_count == 3 && row_is(0, false, SCENE_ROW_ACTION, "Erneut lesen", "", true) && row_is(1, true, SCENE_ROW_ACTION, "Fehler löschen", "", false),
	      "a list of -5 lines counts as one of none: three rows, the choices");

	// The other lists do not show the lines of this one
	stage_on(NAV_DTC_LIST, 0);
	world.list_lines = 2;
	world.cleared_lines = 7;
	world.old_lines = 12;
	input.list = empty_lines;
	input.cleared = cleared_lines;
	input.old = codes_lines;
	build();
	check(scene->total == 5 && row_is(0, true, SCENE_ROW_HEAD, "0 Fehler", "18 Steuergeräte · 35 s", true), "the list shows the lines of the own read, whatever other lists there are");
	nav.screen = NAV_DTC_CLEARED;
	build();
	check(scene->total == 8 && row_is(0, true, SCENE_ROW_HEAD, "Gelöscht 3 von 5", "verbleibend 2", true), "the outcome shows the lines of the clear, whatever other lists there are");
	nav.screen = NAV_DTC_OLD;
	build();
	check(scene->total == 13 && row_is(0, true, SCENE_ROW_HEAD, "5 Fehler", "18 Steuergeräte · 34 s", true), "the old list shows the lines before the last clear, whatever other lists there are");
	check(strcmp(scene->title, "Zuletzt gelöscht") == 0 && scene->note[0] == '\0', "the old list has its title and no note, also while clearing is not allowed");
}

/* The dialogs ------------------------------------------------------------------------------------------ */

static void test_clear_dialog(void)
{
	static const int rows[] = {0, 1, 2, 5, -1, INT_MAX, INT_MIN};
	static const int options[] = {0, 1, 1, 1, 0, 1, 0};
	static const struct
	{
		uint64_t held_ms;
		int permille;
	} holds[] = {
		{1, 0}, {2, 0}, {3, 1}, {100, 33}, {1500, 500}, {2997, 999}, {2999, 999}, {3000, 1000}, {3500, 1000},
	};
	dtc_summary_t summary = {0, 0, 0, 0};

	stage_dialog(0, 0);
	screen("clear_cancel", "the clear dialog as it opens: what is cleared, the two warnings, the focus on \"Abbrechen\", the hold empty, the note how to confirm");
	stage_dialog(1, 100);
	screen("clear_hold_begun", "the clear dialog with the knob held on \"Löschen\" for 100 ms: 33 permille");
	stage_dialog(1, 1500);
	screen("clear_hold_half", "the clear dialog with the knob held on \"Löschen\" for 1500 ms: half way");
	stage_dialog(1, 3000);
	screen("clear_hold_full", "the clear dialog with the knob held on \"Löschen\" for 3000 ms: full");
	stage_dialog(0, 0);
	input.summary = NULL;
	screen("clear_no_summary", "the clear dialog without a summary: the line of the numbers is left out, the warnings stay");
	stage_dialog(1, 0);
	summary.codes = 1;
	summary.ecus_with_codes = 1;
	input.summary = &summary;
	screen("clear_one_unit", "the clear dialog for one trouble code in one control unit: \"1 Steuergerät\"; the focus on \"Löschen\", not held");
	stage_dialog(1, 2997);
	view_scan();
	screen("clear_scan", "the clear dialog with the knob held for 2997 ms while a scan runs: the hold is the only arc, the ring is off");
	for(int v = 0; v < COUNT(VIEWS); v++)
	{
		bool scanning = VIEWS[v].view == CONN_VIEW_SCAN;

		stage_dialog(0, 0);
		VIEWS[v].make();
		build();
		snprintf(what, sizeof(what), "the clear dialog in the view %s: %s", VIEWS[v].name, scanning ? "the ring is off, not a second arc next to the hold" : "the ring of the view");
		check(scene->kind == SCENE_CHOICE && scene->permille == 0 && ring_is(scanning ? RING_NONE : VIEWS[v].ring, 0), what);
	}
	for(int i = 0; i < COUNT(rows); i++)
	{
		stage_dialog(0, 0);
		view_scan();
		nav.row = rows[i];
		// A scan of somebody else: the list may not be cleared any more, and nav_tick() will leave the dialog
		world.can_clear = false;
		input.clear_block = DTC_FLOW_BUSY;
		build();
		snprintf(what, sizeof(what), "the clear dialog during a scan with the focus on row %d: the ring is off wherever the focus is, and whether or not the list may still be cleared", rows[i]);
		check(scene->kind == SCENE_CHOICE && ring_is(RING_NONE, 0), what);
	}

	for(int i = 0; i < COUNT(SUMS); i++)
	{
		stage_dialog(0, 0);
		summary.codes = SUMS[i].codes;
		summary.ecus_with_codes = SUMS[i].units;
		// The other numbers of a summary are not the ones the dialog names
		summary.ecus_not_ok = 5;
		summary.ecus_clean = 9;
		input.summary = &summary;
		build();
		snprintf(what, sizeof(what), "the clear dialog names its numbers: \"%s\"", SUMS[i].line);
		check(lines_are(SUMS[i].line, "Betrifft alle Steuergeräte, auch SRS und ESP.", "Zündung an, Motor aus, Fahrzeug steht.", NULL), what);
	}
	for(int i = 0; i < COUNT(rows); i++)
	{
		stage_dialog(0, 0);
		nav.row = rows[i];
		build();
		snprintf(what, sizeof(what), "the clear dialog with the focus on row %d: the answer in focus is \"%s\"", rows[i], options[i] == 0 ? "Abbrechen" : "Löschen");
		check(scene->kind == SCENE_CHOICE && scene->option == options[i] && strcmp(scene->options[0], "Abbrechen") == 0 && strcmp(scene->options[1], "Löschen") == 0, what);
	}
	for(int i = 0; i < COUNT(holds); i++)
	{
		stage_dialog(1, holds[i].held_ms);
		build();
		snprintf(what, sizeof(what), "the clear dialog with the knob held for %lu ms: %d permille", (unsigned long)holds[i].held_ms, holds[i].permille);
		check(scene->permille == holds[i].permille && scene->option == 1, what);
	}
	stage_dialog(1, 1500);
	if(hold_sample(&hold, true, true, true, NOW) != HOLD_PROGRESS) setup_failures++;
	input.now_ms = NOW - 600;
	build();
	check(scene->permille == 500, "the clear dialog at a time before the last reading of the switch: no time has passed, the hold stands where it was");
	input.now_ms = NOW + 600;
	build();
	check(scene->permille == 700, "the clear dialog 600 ms after the last reading of the switch: the hold has gone on by 200 permille");
	stage_dialog(1, 1500);
	nav.row = 0;
	build();
	check(scene->permille == 500 && scene->option == 0, "the focus back on \"Abbrechen\" before the switch was read again: the hold is what hold.h says, 500 permille");
	stage_dialog(1, 1500);
	hold_close(&hold);
	build();
	check(scene->permille == 0 && scene->kind == SCENE_CHOICE, "the clear dialog after the hold was closed: 0 permille, not -1 - this choice has a hold");
	stage_dialog(1, 1500);
	nav.screen = NAV_CONFIRM;
	nav.confirm = NAV_DO_REBOOT;
	build();
	check(scene->permille == -1 && scene->note[0] == '\0' && scene->line_count == 0, "another dialog while the knob is held: no hold, no note and no line of the clear dialog");
}

static void test_failed(void)
{
	static const struct
	{
		const char *reason;
		const char *text;
	} reasons[] = {
		{"busy", "Scan läuft bereits (anderes Gerät)"}, {"ecu_offline", "Motorsteuergerät offline – Zündung an?"},
		{"engine_state_unknown", "Drehzahl nicht lesbar – nichts gelöscht"}, {"result_serialize_failed", "WiCAN: Speicher knapp – erneut lesen"},
		{"no_answer", "Keine Antwort vom WiCAN"}, {"restarted", "WiCAN neu gestartet – Ergebnis verloren"}, {"superseded", "Von einem anderen Scan überholt"},
		{"expired", "Auftrag verfallen – nichts gesendet"}, {"Busy", "Busy"}, {"not_ready ", "not_ready "}, {"not_read", "not_read"},
		{"a reason of the longest size 31", "a reason of the longest size 31"}, {"Grund mit Umlauten äöü", "Grund mit Umlauten äöü"},
	};
	static const struct
	{
		dtc_flow_phase_t phase;
		const char *name;
		const char *title;
	} others[] = {
		{DTC_FLOW_IDLE, "IDLE", "Fehlerspeicher"}, {DTC_FLOW_READ_SENT, "READ_SENT", "Fehlerspeicher"}, {DTC_FLOW_READING, "READING", "Fehlerspeicher"},
		{DTC_FLOW_LIST, "LIST", "Fehlerspeicher"}, {DTC_FLOW_CLEAR_SENT, "CLEAR_SENT", "Fehlerspeicher"}, {DTC_FLOW_CLEARING, "CLEARING", "Fehlerspeicher"},
		{DTC_FLOW_CLEARED, "CLEARED", "Fehlerspeicher"}, {(dtc_flow_phase_t)9, "9", "Fehlerspeicher"}, {(dtc_flow_phase_t)-1, "-1", "Fehlerspeicher"},
		{(dtc_flow_phase_t)(256 + DTC_FLOW_FAILED), "263", "Fehlerspeicher"}, {(dtc_flow_phase_t)(256 + DTC_FLOW_UNKNOWN), "264", "Fehlerspeicher"},
	};
	static const int32_t sleeps[] = {-1, 1, 2, 60, INT32_MAX, INT32_MIN};

	stage_failed(DTC_FLOW_FAILED, "engine_running");
	screen("failed_reason", "a request that failed with a reason the display has a text for: that text, and how to go on");
	stage_failed(DTC_FLOW_FAILED, "http_500");
	screen("failed_word", "a request that failed with a reason the display has no text for: the word itself");
	stage_failed(DTC_FLOW_FAILED, "not_ready");
	screen("failed_starting", "a request the adapter was not ready for while it is not about to sleep: it is still starting");
	stage_failed(DTC_FLOW_FAILED, "not_ready");
	adapter.sleep_in_s = 0;
	view_live();
	screen("failed_sleeping", "a request the adapter was not ready for while it is about to sleep: it switches off");
	stage_failed(DTC_FLOW_FAILED, "");
	screen("failed_no_reason", "a request that failed without a reason: an empty line in its place");
	stage_failed(DTC_FLOW_UNKNOWN, "restarted");
	view_no_answer();
	screen("failed_unknown", "a clear whose outcome is not known: its own title and text, whatever reason is left in the flow");
	stage_failed(DTC_FLOW_IDLE, "engine_running");
	screen("failed_over", "the failure screen while nothing has failed: a title without action, no line, still how to go on");

	for(int i = 0; i < COUNT(reasons); i++)
	{
		stage_failed(DTC_FLOW_FAILED, reasons[i].reason);
		// The world says another phase: the reason is the one of the flow, and so is the phase it belongs to
		world.flow = DTC_FLOW_IDLE;
		adapter.sleep_in_s = 0;
		view_live();
		build();
		snprintf(what, sizeof(what), "a request that failed with the reason \"%s\": \"%s\"", reasons[i].reason, reasons[i].text);
		check(head_is(SCENE_NOTICE, "Fehlgeschlagen", "Knopf drücken") && lines_are(reasons[i].text, NULL, NULL, NULL), what);
	}
	for(int i = 0; i < COUNT(others); i++)
	{
		stage_failed(others[i].phase, "engine_running");
		world.flow = DTC_FLOW_FAILED;
		build();
		snprintf(what, sizeof(what), "the failure screen with the flow in the phase %s: title \"%s\", no line", others[i].name, others[i].title);
		check(head_is(SCENE_NOTICE, others[i].title, "Knopf drücken") && lines_are(NULL, NULL, NULL, NULL), what);
	}
	for(int i = 0; i < COUNT(sleeps); i++)
	{
		stage_failed(DTC_FLOW_FAILED, "not_ready");
		adapter.sleep_in_s = sleeps[i];
		view_live();
		build();
		snprintf(what, sizeof(what), "not ready with an adapter that says sleep_in_s %ld: it is still starting", (long)sleeps[i]);
		check(lines_are("WiCAN startet noch", NULL, NULL, NULL), what);
	}
	// Texts of the adapter are passed on as they are
	stage_failed(DTC_FLOW_FAILED, "a\x01\xFF\tb");
	build();
	check(lines_are("a\x01\xFF\tb", NULL, NULL, NULL), "a reason word with a control character and a byte that is no UTF-8 is shown as it came");
	stage_busy(DTC_FLOW_READING, 41);
	scan(WICAN_DTC_RUNNING, 41, false, 2, 18, "N10 S\x01\xFFM");
	answer(CONN_VIEW_SCAN);
	build();
	check(lines_are("S\x01\xFFM", HINT, NULL, NULL), "the name of a control unit with a control character and a byte that is no UTF-8 is shown as it came, without its component designation");

	stage_failed(DTC_FLOW_FAILED, "not_ready");
	view_no_wifi();
	build();
	check(lines_are("WiCAN startet noch", NULL, NULL, NULL) && ring_is(RING_RED, 0), "not ready without a state of the adapter: the text of the reason, no crash");
	stage_failed(DTC_FLOW_FAILED, "not_ready");
	adapter.sleep_in_s = 0;
	view_ecu_offline();
	build();
	check(lines_are("WiCAN schaltet ab – später erneut lesen", NULL, NULL, NULL) && ring_is(RING_GREY, 0), "not ready, about to sleep and the ignition off: it switches off");
	stage_failed(DTC_FLOW_FAILED, "not_ready");
	adapter.sleep_in_s = 0;
	view_autopid_off();
	build();
	check(lines_are("WiCAN schaltet ab – später erneut lesen", NULL, NULL, NULL), "not ready, about to sleep and AutoPID off: it switches off");
	stage_failed(DTC_FLOW_FAILED, "busy");
	adapter.sleep_in_s = 0;
	view_live();
	build();
	check(lines_are("Scan läuft bereits (anderes Gerät)", NULL, NULL, NULL), "another reason while the adapter is about to sleep: its own text");
	stage_failed(DTC_FLOW_FAILED, "engine_running");
	adapter.dtc.phase = WICAN_DTC_ERROR;
	SET(adapter.dtc.reason, "busy");
	view_live();
	build();
	check(lines_are("Motor läuft – nur bei Motor aus", NULL, NULL, NULL), "a failed request while the state of the adapter names another reason, that of a later request: the reason of the own one");
	stage_failed(DTC_FLOW_UNKNOWN, "not_ready");
	adapter.sleep_in_s = 0;
	view_live();
	build();
	check(head_is(SCENE_NOTICE, "Stand unbekannt", "Knopf drücken") && lines_are("Stand des Löschens unbekannt – bitte erneut lesen", NULL, NULL, NULL),
	      "an unknown outcome while the adapter is about to sleep, with a reason left in the flow: the text of the unknown outcome");
}

static void test_brightness(void)
{
	static const struct
	{
		int value;
		const char *big;
		int permille;
	} levels[] = {
		{80, "80 %", 800}, {5, "5 %", 50}, {100, "100 %", 1000}, {99, "99 %", 990}, {101, "101 %", 1000}, {1, "1 %", 10}, {0, "0 %", 0}, {-1, "-1 %", 0},
		{214748365, "214748365 %", 1000}, {INT_MAX, "2147483647 %", 1000}, {INT_MIN, "-2147483648 %", 0}, {-214748365, "-214748365 %", 0},
	};

	stage_on(NAV_BRIGHTNESS, 0);
	nav.value = 80;
	world.brightness = 35;
	screen("brightness_day", "the brightness being set by day: the value of the knob, not the one in use; how to change and store it");
	stage_on(NAV_BRIGHTNESS, 0);
	nav.value = 5;
	world.night_mode = true;
	screen("brightness_night", "the brightness being set in night mode: the title tells which one it is");

	stage_on(NAV_BRIGHTNESS, 0);
	nav.value = 100;
	world.night_mode = true;
	world.brightness = 100;
	build();
	check(head_is(SCENE_LEVEL, "Helligkeit (Nacht)", "Drehen zum Ändern, Drücken zum Speichern") && strcmp(scene->big, "100 %") == 0, "the brightness of the night at 100: the title of the night, whatever the value");
	nav.value = 5;
	world.night_mode = false;
	world.brightness = 5;
	build();
	check(head_is(SCENE_LEVEL, "Helligkeit", "Drehen zum Ändern, Drücken zum Speichern") && strcmp(scene->big, "5 %") == 0, "the brightness of the day at 5: the title of the day, whatever the value");

	stage_on(NAV_BRIGHTNESS, 0);
	nav.value = 80;
	view_scan();
	build();
	check(scene->kind == SCENE_LEVEL && scene->permille == 800 && ring_is(RING_PROGRESS, 277),
	      "the brightness while a scan runs: the ring shows the progress - only the progress screen and the clear dialog go without it");

	for(int i = 0; i < COUNT(levels); i++)
	{
		stage_on(NAV_BRIGHTNESS, 0);
		nav.value = levels[i].value;
		build();
		snprintf(what, sizeof(what), "a brightness of %d: \"%s\" and %d permille", levels[i].value, levels[i].big, levels[i].permille);
		check(head_is(SCENE_LEVEL, "Helligkeit", "Drehen zum Ändern, Drücken zum Speichern") && strcmp(scene->big, levels[i].big) == 0 && scene->permille == levels[i].permille, what);
	}
}

static void test_web(void)
{
	static const struct
	{
		uint64_t open_ms;       // how long the release is open
		const char *detail;
	} times[] = {
		{0, "an – noch 10:00"}, {1, "an – noch 10:00"}, {1000, "an – noch 9:59"}, {48000, "an – noch 9:12"}, {540000, "an – noch 1:00"}, {540001, "an – noch 1:00"},
		{541000, "an – noch 0:59"}, {590000, "an – noch 0:10"}, {591000, "an – noch 0:09"}, {599000, "an – noch 0:01"}, {599999, "an – noch 0:01"}, {600000, "aus"}, {700000, "aus"},
	};
	// The access point of the stage is "WiCAN-Display" with the password "geheim1234"
	static const struct
	{
		const char *address;
		bool ap_on;
		const char *lines[3];
		const char *rule;
	} firsts[] = {
		{NULL, false, {"Kein WLAN", NULL, NULL}, "without an address (NULL) and without the own access point: \"Kein WLAN\""},
		{"", false, {"Kein WLAN", NULL, NULL}, "with an empty address and without the own access point: \"Kein WLAN\""},
		{NULL, true, {"WLAN: WiCAN-Display", "Passwort: geheim1234", NULL}, "without an address (NULL) and with the own access point: its name and password, two lines"},
		{"", true, {"WLAN: WiCAN-Display", "Passwort: geheim1234", NULL}, "with an empty address and with the own access point: its name and password, two lines"},
		{"http://192.168.4.1", true, {"http://192.168.4.1", "WLAN: WiCAN-Display", "Passwort: geheim1234"}, "with an address and with the own access point: the address, then name and password"},
		{"http://192.168.88.37", false, {"http://192.168.88.37", NULL, NULL}, "with an address and without the own access point: the address alone"},
	};

	stage_on(NAV_WEB, 0);
	screen("web_closed", "the web access while it is locked, in a network: the release is off, the address of the display");
	stage_on(NAV_WEB, 1);
	access_open(&gate, NOW - 48000);
	screen("web_open", "the web access released 48 seconds ago: the release is on for 9:12, the focus on the way back");
	stage_on(NAV_WEB, 0);
	view_no_wifi();
	input.address = NULL;
	screen("web_no_wifi", "the web access in no network and without the own access point: \"Kein WLAN\" in place of the address");
	stage_on(NAV_WEB, 0);
	input.address = "http://192.168.4.1";
	input.ap_on = true;
	screen("web_hotspot", "the web access with the own access point: the address, then its name and its password");
	stage_on(NAV_WEB, 0);
	view_no_wifi();
	access_open(&gate, NOW);
	input.address = "";
	input.ap_on = true;
	screen("web_hotspot_alone", "the web access with the own access point and without an address: name and password of the network to join, and no \"Kein WLAN\" above them; released just now for 10:00");

	for(int i = 0; i < COUNT(times); i++)
	{
		stage_on(NAV_WEB, 0);
		access_open(&gate, NOW - times[i].open_ms);
		// The world says the opposite: the time left is that of access.h, and with it whether it is on
		world.release_open = times[i].detail[1] == 'u';
		build();
		snprintf(what, sizeof(what), "the release, switched on %lu ms ago: \"%s\"", (unsigned long)times[i].open_ms, times[i].detail);
		check(row_is(0, true, SCENE_ROW_ACTION, "Freigabe", times[i].detail, true) && row_is(1, false, SCENE_ROW_ACTION, "Zurück", "", true), what);
	}
	stage_on(NAV_WEB, 0);
	access_open(&gate, NOW - 48000);
	access_close(&gate, NOW - 1000);
	build();
	check(row_is(0, true, SCENE_ROW_ACTION, "Freigabe", "aus", true), "a release that was switched off again: \"aus\"");
	stage_on(NAV_WEB, 0);
	access_open(&gate, NOW - 48000);
	input.now_ms = NOW - 60000;
	build();
	check(row_is(0, true, SCENE_ROW_ACTION, "Freigabe", "an – noch 10:00", true), "a release at a time before it was switched on: no time has passed, 10:00 are left");

	stage_on(NAV_WEB, 0);
	gate.open = true;
	gate.open_until_ms = NOW + 4000000;
	build();
	check(row_is(0, true, SCENE_ROW_ACTION, "Freigabe", "an – noch 66:40", true), "a release made by hand that lasts 4000 seconds: the time is what access.h says, 66:40");

	// The first line: the address, "Kein WLAN" only where nothing else can be told
	for(int i = 0; i < COUNT(firsts); i++)
	{
		stage_on(NAV_WEB, 0);
		input.address = firsts[i].address;
		input.ap_on = firsts[i].ap_on;
		build();
		snprintf(what, sizeof(what), "the lines of the web access %s", firsts[i].rule);
		check(lines_are(firsts[i].lines[0], firsts[i].lines[1], firsts[i].lines[2], NULL) && scene->total == 2 && scene->row_count == 2, what);
	}
	stage_on(NAV_WEB, 0);
	input.address = NULL;
	input.ap_on = true;
	input.ap_ssid = NULL;
	input.ap_password = NULL;
	build();
	check(lines_are("WLAN: ", "Passwort: ", NULL, NULL), "the own access point without an address, a name and a password: its two lines with nothing behind the colon, and still no \"Kein WLAN\"");
	stage_on(NAV_WEB, 1);
	input.address = NULL;
	build();
	check(lines_are("Kein WLAN", NULL, NULL, NULL) && row_is(1, true, SCENE_ROW_ACTION, "Zurück", "", true), "without an address and without the own access point, the focus on the way back: \"Kein WLAN\" wherever the focus is");
	input.ap_ssid = NULL;
	input.ap_password = NULL;
	world.release_open = true;
	world.night_mode = true;
	build();
	check(lines_are("Kein WLAN", NULL, NULL, NULL), "without an address and without the own access point, which has no name and no password either: \"Kein WLAN\"");
	stage_on(NAV_WEB, 0);
	view_no_wifi();
	input.address = NULL;
	input.ap_on = true;
	input.safe_mode = true;
	build();
	check(lines_are("WLAN: WiCAN-Display", "Passwort: geheim1234", NULL, NULL), "the own access point without an address, in safe mode and out of reach of the adapter: its two lines, no \"Kein WLAN\"");

	stage_on(NAV_WEB, 0);
	input.ap_on = false;
	input.ap_ssid = "WiCAN-Display";
	build();
	check(lines_are("http://192.168.88.37", NULL, NULL, NULL), "without the own access point its name and password are not shown, although they are known");
	input.ap_on = true;
	input.ap_ssid = NULL;
	input.ap_password = NULL;
	build();
	check(lines_are("http://192.168.88.37", "WLAN: ", "Passwort: ", NULL), "the own access point without a name and a password (NULL): the two lines with nothing behind the colon");
	input.ap_ssid = "";
	input.ap_password = "";
	build();
	check(lines_are("http://192.168.88.37", "WLAN: ", "Passwort: ", NULL), "the own access point with an empty name and password: the two lines with nothing behind the colon");
	input.ap_ssid = "Netz";
	input.ap_password = "Wort";
	input.address = "x";
	build();
	check(lines_are("x", "WLAN: Netz", "Passwort: Wort", NULL) && scene->total == 2 && scene->row_count == 2, "an address of one byte is an address; name and password each on their own line");
}

static void test_settings(void)
{
	bool enabled = true;

	stage_on(NAV_SETTINGS, 0);
	screen("settings_top", "the settings with the focus on their first row: rows 0 to 4 of 6; direction normal, hotspot off, no previous firmware");
	stage_on(NAV_SETTINGS, 2);
	input.reverse = true;
	input.ap_on = true;
	world.previous_firmware = true;
	screen("settings_reversed", "the settings with the focus on row 2: direction reversed, hotspot on, a previous firmware to start");
	stage_on(NAV_SETTINGS, 5);
	screen("settings_end", "the settings with the focus on their last row: rows 1 to 5");

	stage_on(NAV_SETTINGS, 0);
	input.reverse = true;
	build();
	check(row_is(0, true, SCENE_ROW_ACTION, "Drehrichtung", "umgekehrt", true) && row_is(1, false, SCENE_ROW_ACTION, "Hotspot", "aus", true) && row_is(3, false, SCENE_ROW_ACTION, "Vorherige Version", "", false),
	      "direction reversed alone: the hotspot stays off, the previous firmware disabled");
	input.reverse = false;
	input.ap_on = true;
	build();
	check(row_is(0, true, SCENE_ROW_ACTION, "Drehrichtung", "normal", true) && row_is(1, false, SCENE_ROW_ACTION, "Hotspot", "an", true) && row_is(3, false, SCENE_ROW_ACTION, "Vorherige Version", "", false),
	      "hotspot on alone: the direction stays normal, the previous firmware disabled");
	input.ap_on = false;
	world.previous_firmware = true;
	build();
	check(row_is(0, true, SCENE_ROW_ACTION, "Drehrichtung", "normal", true) && row_is(1, false, SCENE_ROW_ACTION, "Hotspot", "aus", true) && row_is(3, false, SCENE_ROW_ACTION, "Vorherige Version", "", true) &&
	      row_is(2, false, SCENE_ROW_ACTION, "Neustart", "", true) && row_is(4, false, SCENE_ROW_ACTION, "Werkseinstellungen", "", true),
	      "a previous firmware alone: its row is enabled, restart and factory reset are always");
	world.update_pending = true;
	build();
	check(row_is(3, false, SCENE_ROW_ACTION, "Vorherige Version", "", true) && scene->over == SCENE_OVER_UPDATE, "a previous firmware while the update question lies over the settings: its row stays enabled");

	for(int focus = 0; focus <= 5; focus += 5)
	{
		stage_on(NAV_SETTINGS, focus);
		view_no_wifi();
		world.can_read = false;
		world.previous_firmware = true;
		input.safe_mode = true;
		input.heat = GUARD_HEAT_OFF;
		input.address = NULL;
		input.ap_on = true;
		build();
		enabled = enabled && scene->row_count == 5;
		for(int i = 0; i < scene->row_count; i++) enabled = enabled && scene->rows[i].enabled && scene->rows[i].kind == SCENE_ROW_ACTION;
	}
	check(enabled, "every row of the settings is enabled in safe mode, without a network and when it is too hot");
}

static void test_confirm(void)
{
	static const int rows[] = {0, 1, 2, -1, INT_MAX, INT_MIN};
	static const int options[] = {0, 1, 1, 0, 1, 0};
	static const nav_do_t others[] = {NAV_DO_NOTHING, NAV_DO_READ, NAV_DO_CLEAR, NAV_DO_UPDATE_OK, (nav_do_t)19, (nav_do_t)-1, (nav_do_t)(256 + NAV_DO_REBOOT), (nav_do_t)(256 + NAV_DO_FACTORY_RESET)};

	stage_on(NAV_CONFIRM, 0);
	nav.confirm = NAV_DO_REBOOT;
	screen("confirm_reboot", "the question before a restart: the focus on \"Abbrechen\", no hold, no line");
	stage_on(NAV_CONFIRM, 1);
	nav.confirm = NAV_DO_PREVIOUS_FIRMWARE;
	screen("confirm_previous", "the question before the previous firmware is started: the focus on \"Ausführen\"");
	stage_on(NAV_CONFIRM, 0);
	nav.confirm = NAV_DO_FACTORY_RESET;
	screen("confirm_reset", "the question before a factory reset: what is erased and what stays");
	stage_on(NAV_CONFIRM, 0);
	nav.confirm = NAV_DO_REBOOT;
	view_scan();
	screen("confirm_scan", "the question before a restart while a scan runs: this dialog has no hold and no arc, the ring shows the progress");

	for(int i = 0; i < COUNT(rows); i++)
	{
		stage_on(NAV_CONFIRM, rows[i]);
		nav.confirm = NAV_DO_REBOOT;
		build();
		snprintf(what, sizeof(what), "a dialog of the settings with the focus on row %d: the answer in focus is \"%s\"", rows[i], options[i] == 0 ? "Abbrechen" : "Ausführen");
		check(head_is(SCENE_CHOICE, "Neu starten?", "") && scene->option == options[i] && strcmp(scene->options[0], "Abbrechen") == 0 && strcmp(scene->options[1], "Ausführen") == 0 &&
		      scene->permille == -1 && scene->line_count == 0, what);
	}
	for(int i = 0; i < COUNT(others); i++)
	{
		stage_on(NAV_CONFIRM, 0);
		nav.confirm = others[i];
		build();
		snprintf(what, sizeof(what), "a dialog of the settings for the action %d, which is none of the three: no title, no line, the two answers", (int)others[i]);
		check(head_is(SCENE_CHOICE, "", "") && scene->line_count == 0 && strcmp(scene->options[0], "Abbrechen") == 0 && strcmp(scene->options[1], "Ausführen") == 0 && scene->permille == -1, what);
	}
	stage_on(NAV_CONFIRM, 0);
	nav.confirm = NAV_DO_PREVIOUS_FIRMWARE;
	build();
	check(head_is(SCENE_CHOICE, "Vorherige Version starten?", "") && scene->line_count == 0, "the question before the previous firmware has no line");
	for(int i = 0; i < COUNT(rows); i++)
	{
		stage_on(NAV_CONFIRM, rows[i]);
		nav.confirm = NAV_DO_FACTORY_RESET;
		build();
		snprintf(what, sizeof(what), "the question before a factory reset with the focus on row %d: its two lines, the answer in focus is \"%s\"", rows[i], options[i] == 0 ? "Abbrechen" : "Ausführen");
		check(head_is(SCENE_CHOICE, "Werkseinstellungen?", "") && lines_are("WLAN, Kopplung und Einstellungen werden gelöscht.", "Die Ansichten bleiben.", NULL, NULL) && scene->option == options[i], what);
	}
}

/* What lies over a screen ------------------------------------------------------------------------------ */

// The screen below, as it is without the overlay
static scene_t below;

static bool below_is_kept(void)
{
	scene_t under = box.scene;

	under.over = SCENE_OVER_NONE;
	memset(under.over_lines, 0, sizeof(under.over_lines));
	under.over_line_count = 0;
	under.over_permille = -1;
	return memcmp(&under, &below, sizeof(below)) == 0;
}

static void test_overlays(void)
{
	static const struct
	{
		int percent;
		const char *line;
		int permille;
	} uploads[] = {
		{0, "0 %", 0}, {1, "1 %", 10}, {42, "42 %", 420}, {99, "99 %", 990}, {100, "100 %", 1000}, {101, "100 %", 1000}, {-1, "0 %", 0},
		{INT_MAX, "100 %", 1000}, {INT_MIN, "0 %", 0}, {214748365, "100 %", 1000}, {356, "100 %", 1000},
	};
	static const struct
	{
		access_ask_t asking;
		const char *line;
	} questions[] = {
		{ACCESS_ASK_WIFI, "WLAN speichern?"}, {ACCESS_ASK_FIRMWARE, "Firmware installieren?"}, {ACCESS_ASK_RESET, "Werkseinstellungen?"},
		{(access_ask_t)4, ""}, {(access_ask_t)-1, ""}, {(access_ask_t)(256 + ACCESS_ASK_WIFI), ""},
	};
	static const struct
	{
		uint64_t asked_ms;      // how long ago the browser asked
		const char *line;
	} waits[] = {
		{0, "Drücken = ja · lang = nein (60 s)"}, {1, "Drücken = ja · lang = nein (60 s)"}, {1000, "Drücken = ja · lang = nein (59 s)"}, {18000, "Drücken = ja · lang = nein (42 s)"},
		{50000, "Drücken = ja · lang = nein (10 s)"}, {50001, "Drücken = ja · lang = nein (10 s)"}, {51000, "Drücken = ja · lang = nein (9 s)"},
		{59999, "Drücken = ja · lang = nein (1 s)"}, {60000, "Drücken = ja · lang = nein (0 s)"},
	};
	static const struct
	{
		uint32_t left_s;
		const char *line;
	} updates[] = {
		{0, "sonst alte Version in 0:00"}, {1, "sonst alte Version in 0:01"}, {9, "sonst alte Version in 0:09"}, {10, "sonst alte Version in 0:10"}, {59, "sonst alte Version in 0:59"},
		{60, "sonst alte Version in 1:00"}, {61, "sonst alte Version in 1:01"}, {252, "sonst alte Version in 4:12"}, {300, "sonst alte Version in 5:00"}, {599, "sonst alte Version in 9:59"},
		{600, "sonst alte Version in 10:00"}, {3599, "sonst alte Version in 59:59"}, {3600, "sonst alte Version in 60:00"}, {4294967295u, "sonst alte Version in 71582788:15"},
	};
	static const nav_screen_t screens[] = {
		NAV_PAGES, NAV_MENU, NAV_DTC, NAV_DTC_BUSY, NAV_DTC_LIST, NAV_DTC_CONFIRM, NAV_DTC_CLEARED, NAV_DTC_FAILED, NAV_DTC_OLD, NAV_BRIGHTNESS, NAV_WEB, NAV_INFO,
		NAV_SETTINGS, NAV_CONFIRM,
	};
	int kept = 0, laid = 0;

	stage();
	world.uploading = true;
	input.upload_percent = 42;
	screen("over_upload", "a firmware upload over a value page: the page as without it, the two lines and the progress of the upload");
	stage_on(NAV_MENU, 0);
	world.release_open = true;
	access_open(&gate, NOW - 30000);
	if(access_ask(&gate, ACCESS_ASK_WIFI, NOW - 18000) == 0) setup_failures++;
	world.asking = ACCESS_ASK_WIFI;
	input.ask_detail = "Werkstatt";
	screen("over_ask_wifi", "the browser asks to store a network, over the menu: the question, the name of the network, how to answer and how long");
	stage_on(NAV_WEB, 1);
	access_open(&gate, NOW - 100000);
	if(access_ask(&gate, ACCESS_ASK_FIRMWARE, NOW - 59001) == 0) setup_failures++;
	world.asking = ACCESS_ASK_FIRMWARE;
	world.release_open = true;
	input.ask_detail = "w906-display 0.2";
	screen("over_ask_firmware", "the browser asks to install a firmware, over the web access: the question, the version, one second left; the release was renewed by the question");
	stage_on(NAV_SETTINGS, 0);
	access_open(&gate, NOW - 100000);
	if(access_ask(&gate, ACCESS_ASK_RESET, NOW) == 0) setup_failures++;
	world.asking = ACCESS_ASK_RESET;
	screen("over_ask_reset", "the browser asks for a factory reset, over the settings: the question and how to answer, no line of detail");
	stage();
	nav.page = 1;
	world.update_pending = true;
	input.update_left_s = 252;
	screen("over_update", "the question after an update over a value page: the question, how to answer, and when the old version comes back");
	stage_dialog(1, 1500);
	world.uploading = true;
	world.asking = ACCESS_ASK_WIFI;
	world.update_pending = true;
	input.upload_percent = 100;
	input.ask_detail = "Werkstatt";
	input.update_left_s = 252;
	screen("over_upload_first", "an upload, a question of the browser and the update question at once, over the clear dialog: the upload goes first; the dialog below is as it was");
	stage();
	view_no_wifi();
	world.asking = ACCESS_ASK_WIFI;
	world.update_pending = true;
	input.ask_detail = "";
	input.update_left_s = 252;
	screen("over_ask_second", "a question of the browser and the update question at once: the question of the browser goes first; an empty detail is no line; no question waits in access.h, so 0 s");

	for(int i = 0; i < COUNT(uploads); i++)
	{
		stage();
		world.uploading = true;
		input.upload_percent = uploads[i].percent;
		build();
		snprintf(what, sizeof(what), "an upload at %d percent: \"%s\" and %d permille", uploads[i].percent, uploads[i].line, uploads[i].permille);
		check(over_is(SCENE_OVER_UPLOAD, uploads[i].permille, "Firmware wird übertragen", uploads[i].line, NULL), what);
	}
	for(int i = 0; i < COUNT(questions); i++)
	{
		stage();
		access_open(&gate, NOW - 100000);
		if(access_ask(&gate, ACCESS_ASK_RESET, NOW - 18000) == 0) setup_failures++;
		world.asking = questions[i].asking;
		input.ask_detail = "Detail";
		build();
		snprintf(what, sizeof(what), "the question %d of the world: \"%s\" as the first line, whatever question waits in access.h", (int)questions[i].asking, questions[i].line);
		check(over_is(SCENE_OVER_ASK, -1, questions[i].line, "Detail", "Drücken = ja · lang = nein (42 s)"), what);
	}
	for(int i = 0; i < COUNT(waits); i++)
	{
		stage();
		access_open(&gate, NOW - 100000);
		if(access_ask(&gate, ACCESS_ASK_WIFI, NOW - waits[i].asked_ms) == 0) setup_failures++;
		world.asking = ACCESS_ASK_WIFI;
		build();
		snprintf(what, sizeof(what), "a question asked %lu ms ago: \"%s\"", (unsigned long)waits[i].asked_ms, waits[i].line);
		check(over_is(SCENE_OVER_ASK, -1, "WLAN speichern?", waits[i].line, NULL), what);
	}
	stage();
	access_open(&gate, NOW - 100000);
	if(access_ask(&gate, ACCESS_ASK_WIFI, NOW - 18000) == 0) setup_failures++;
	world.asking = ACCESS_ASK_WIFI;
	input.now_ms = NOW - 50000;
	build();
	check(over_is(SCENE_OVER_ASK, -1, "WLAN speichern?", "Drücken = ja · lang = nein (60 s)", NULL), "a question at a time before it was asked: no time has passed, 60 s are left");
	stage();
	world.asking = ACCESS_ASK_FIRMWARE;
	input.ask_detail = NULL;
	build();
	check(over_is(SCENE_OVER_ASK, -1, "Firmware installieren?", "Drücken = ja · lang = nein (0 s)", NULL), "a question without a detail (NULL): two lines");
	input.ask_detail = "x";
	build();
	check(over_is(SCENE_OVER_ASK, -1, "Firmware installieren?", "x", "Drücken = ja · lang = nein (0 s)"), "a question with a detail of one byte: three lines");
	for(int i = 0; i < COUNT(updates); i++)
	{
		stage();
		world.update_pending = true;
		input.update_left_s = updates[i].left_s;
		build();
		snprintf(what, sizeof(what), "the update question with %lu seconds left: \"%s\"", (unsigned long)updates[i].left_s, updates[i].line);
		check(over_is(SCENE_OVER_UPDATE, -1, "Update in Ordnung?", "Knopf drücken oder Bildschirm berühren", updates[i].line), what);
	}

	// What does not lie over the screen leaves nothing
	stage();
	input.upload_percent = 42;
	input.ask_detail = "Werkstatt";
	input.update_left_s = 252;
	access_open(&gate, NOW - 100000);
	if(access_ask(&gate, ACCESS_ASK_WIFI, NOW - 18000) == 0) setup_failures++;
	build();
	check(over_is(SCENE_OVER_NONE, -1, NULL, NULL, NULL), "nothing over the screen, whatever percent, detail, seconds and waiting question there are: no overlay, no line, no progress");
	world.update_pending = true;
	world.uploading = true;
	build();
	check(over_is(SCENE_OVER_UPLOAD, 420, "Firmware wird übertragen", "42 %", NULL), "an upload and the update question at once: the upload");
	world.uploading = false;
	world.asking = ACCESS_ASK_RESET;
	build();
	check(over_is(SCENE_OVER_ASK, -1, "Werkseinstellungen?", "Werkstatt", "Drücken = ja · lang = nein (42 s)"), "a question and the update question at once: the question");
	world.asking = ACCESS_ASK_NONE;
	build();
	check(over_is(SCENE_OVER_UPDATE, -1, "Update in Ordnung?", "Knopf drücken oder Bildschirm berühren", "sonst alte Version in 4:12"), "the update question alone");

	// Every overlay over every screen, live and during a scan: the screen below is filled as without it
	for(int s = 0; s < 2 * COUNT(screens); s++)
	{
		for(int over = 1; over <= 3; over++)
		{
			stage_dialog(1, 1500);
			if(s >= COUNT(screens)) view_scan();
			nav.screen = screens[s % COUNT(screens)];
			nav.row = 1;
			nav.value = 55;
			nav.confirm = NAV_DO_FACTORY_RESET;
			world.previous_firmware = true;
			world.release_open = true;
			world.night_mode = true;
			world.cleared_lines = cleared_count;
			world.old_lines = codes_count;
			world.info_lines = COUNT(INFO);
			input.cleared = cleared_lines;
			input.old = codes_lines;
			input.info = INFO;
			input.upload_percent = 42;
			input.ask_detail = "Werkstatt";
			input.update_left_s = 252;
			build();
			below = box.scene;

			world.uploading = over == 1;
			world.asking = over == 2 ? ACCESS_ASK_WIFI : ACCESS_ASK_NONE;
			world.update_pending = over == 3;
			build();
			if(scene->over == (scene_over_t)over && scene->over_line_count >= 2) laid++;
			if(below_is_kept()) kept++;
			else printf("  screen %d under overlay %d is not the screen without it\n", (int)screens[s % COUNT(screens)], over);
		}
	}
	check(laid == 84, "each of the three overlays lies over each of the 14 screens, live and during a scan");
	check(kept == 84, "under each overlay each of the 14 screens is filled as without it, byte for byte, the ring as well");
}

/* Every screen in every view, and what is no screen ---------------------------------------------------- */

static void test_screens_and_views(void)
{
	static const struct
	{
		nav_screen_t on;
		scene_kind_t kind;
		const char *title;
		bool own_arc;       // the screen has an arc of its own
	} screens[] = {
		{NAV_MENU, SCENE_LIST, "Menü", false}, {NAV_DTC, SCENE_LIST, "Fehlerspeicher", false}, {NAV_DTC_BUSY, SCENE_PROGRESS, "Fehlerspeicher", true},
		{NAV_DTC_LIST, SCENE_LIST, "Fehlerspeicher", false}, {NAV_DTC_CONFIRM, SCENE_CHOICE, "Fehler löschen?", true}, {NAV_DTC_CLEARED, SCENE_LIST, "Gelöscht", false},
		{NAV_DTC_FAILED, SCENE_NOTICE, "Fehlerspeicher", false}, {NAV_DTC_OLD, SCENE_LIST, "Zuletzt gelöscht", false}, {NAV_BRIGHTNESS, SCENE_LEVEL, "Helligkeit", false},
		{NAV_WEB, SCENE_LIST, "Web-Zugriff", false}, {NAV_INFO, SCENE_LIST, "Info", false}, {NAV_SETTINGS, SCENE_LIST, "Einstellungen", false},
		{NAV_CONFIRM, SCENE_CHOICE, "", false},
	};
	static const int no_screens[] = {14, 15, -1, 100, INT_MAX, INT_MIN, 256, 256 + NAV_MENU, 65536 + NAV_DTC_LIST, 256 + NAV_DTC_BUSY, 256 + NAV_DTC_CONFIRM};
	int wrong = 0, wrong_ring = 0, wrong_dots = 0, progress = 0, taken = 0;

	for(int s = 0; s < COUNT(screens); s++)
	{
		for(int v = 0; v < COUNT(VIEWS); v++)
		{
			stage_on(screens[s].on, 0);
			VIEWS[v].make();
			build();
			if(!(scene->kind == screens[s].kind && strcmp(scene->title, screens[s].title) == 0))
			{
				printf("  screen %d in the view %s: kind %d, title \"%s\"\n", (int)screens[s].on, VIEWS[v].name, (int)scene->kind, scene->title);
				wrong++;
			}
			if(VIEWS[v].ring == RING_PROGRESS && screens[s].own_arc)
			{
				if(!ring_is(RING_NONE, 0)) wrong_ring++;
				taken++;
			}
			else if(!ring_is(VIEWS[v].ring, VIEWS[v].permille)) wrong_ring++;
			if(scene->ring.kind == RING_PROGRESS) progress++;
			if(scene->dots != 0 || scene->dot != -1 || scene->item_count != 0) wrong_dots++;
		}
	}
	check(wrong == 0, "each of the 13 screens behind the menu has its kind and its title in each of the 10 views of the connection");
	check(wrong_ring == 0, "on each of those screens the ring is the one texts.h gives for the view, with level 0 and nothing old - but off instead of the progress on the two screens with an arc of their own");
	check(taken == 2 && progress == 11, "of the 13 screens in the view of a scan the progress screen and the clear dialog have no ring, the other 11 the progress of the scan");
	check(wrong_dots == 0, "none of those screens has dots or values");

	for(int i = 0; i < COUNT(no_screens); i++)
	{
		stage_on((nav_screen_t)no_screens[i], 1);
		have_list(mixed_lines, mixed_count);
		world.info_lines = COUNT(INFO);
		input.info = INFO;
		input.safe_mode = true;
		nav.value = 50;
		nav.confirm = NAV_DO_REBOOT;
		build();
		snprintf(what, sizeof(what), "the screen %d, which is none of nav.h: a notice without any text, the ring of the connection", no_screens[i]);
		check(head_is(SCENE_NOTICE, "", "") && scene->line_count == 0 && scene->dots == 0 && scene->dot == -1 && ring_is(RING_NONE, 0), what);
		world.update_pending = true;
		input.update_left_s = 61;
		view_no_wifi();
		build();
		snprintf(what, sizeof(what), "the screen %d, which is none of nav.h, under the update question: the overlay lies over the empty notice", no_screens[i]);
		check(head_is(SCENE_NOTICE, "", "") && over_is(SCENE_OVER_UPDATE, -1, "Update in Ordnung?", "Knopf drücken oder Bildschirm berühren", "sonst alte Version in 1:01") && ring_is(RING_RED, 0), what);
		stage_on((nav_screen_t)no_screens[i], 1);
		view_scan();
		build();
		snprintf(what, sizeof(what), "the screen %d, which is none of nav.h, during a scan: it has no arc of its own, the ring is the progress", no_screens[i]);
		check(head_is(SCENE_NOTICE, "", "") && ring_is(RING_PROGRESS, 277), what);
	}

	// The value pages in the views that show values
	for(int v = 0; v < COUNT(VIEWS); v++)
	{
		if(!VIEWS[v].values) continue;

		stage();
		VIEWS[v].make();
		build();
		snprintf(what, sizeof(what), "the value pages in the view %s: the values of the page, with its title", VIEWS[v].name);
		check(scene->kind == SCENE_VALUES && strcmp(scene->title, "Motor") == 0 && scene->item_count == 6 && ring_is(VIEWS[v].ring, VIEWS[v].permille) &&
		      item_is(0, "Drehzahl", "812", "1/min", VIEWS[v].view == CONN_VIEW_SCAN ? SCENE_TONE_DIM : SCENE_TONE_NORMAL, LAYOUT_WIDGET_ARC, 162), what);
	}

	// The time of the scene is the time of every part of it
	stage();
	seen("{\"ENGINE_RPM\":812}", 2000);
	input.now_ms = NOW + 999;
	build();
	check(scene->items[0].tone == SCENE_TONE_NORMAL, "a value seen 2999 ms before the time of the scene is fresh");
	input.now_ms = NOW + 1000;
	build();
	check(scene->items[0].tone == SCENE_TONE_DIM && strcmp(scene->items[0].text, "812") == 0, "a value seen 3000 ms before the time of the scene is old");
	input.now_ms = NOW + 8000;
	build();
	check(strcmp(scene->items[0].text, SCENE_DASH) == 0, "a value seen 10000 ms before the time of the scene is gone");
	stage();
	conn_init(&conn, ID);
	conn_wifi(&conn, true, NOW - 14999);
	for(int round = 0; round < 3; round++)
	{
		uint64_t ago = 14000 - (uint64_t)round * 5000;

		if(conn_next(&conn, NOW - ago) != CONN_ASK_STATE) setup_failures++;
		conn_got_state(&conn, CONN_GOT_FAILED, NULL, NOW - ago + 100);
	}
	build();
	check(lines_are("Verbinde mit WiCAN …", NULL, NULL, NULL) && ring_is(RING_YELLOW, 0), "three failed rounds 14999 ms after the network came up: still connecting, the time of grace is not over");
	input.now_ms = NOW + 1;
	build();
	check(lines_are("WiCAN antwortet nicht", NULL, NULL, NULL) && ring_is(RING_RED, 0), "three failed rounds 15000 ms after the network came up: no answer");
}

/* Texts that do not fit -------------------------------------------------------------------------------- */

// A text of `length` bytes: ASCII letters, with the character `special` at its end
static const char *long_text(size_t length, const char *special)
{
	static char text[400];
	size_t tail = strlen(special);

	for(size_t i = 0; i < length - tail; i++) text[i] = (char)('a' + i % 26);
	memcpy(&text[length - tail], special, tail + 1);
	return text;
}

static void test_cuts(void)
{
	static const struct
	{
		size_t length;          // of the whole text
		const char *special;    // its last character
		size_t kept;            // bytes of it that fit into a field of SCENE_TEXT_SIZE
		const char *rule;
	} cuts[] = {
		{94, "", 94, "94 bytes fit"},
		{95, "", 95, "95 bytes fit, the field is full"},
		{96, "", 95, "of 96 bytes the first 95 are kept"},
		{300, "", 95, "of 300 bytes the first 95 are kept"},
		{95, "ä", 95, "a character of two bytes that ends with the field fits"},
		{96, "ä", 94, "a character of two bytes that would lose its second byte is left out whole"},
		{97, "ä", 95, "a character of two bytes behind the room is left out, the field is full"},
		{95, "–", 95, "a character of three bytes that ends with the field fits"},
		{96, "–", 93, "a character of three bytes that would lose its last byte is left out whole"},
		{97, "–", 94, "a character of three bytes that would lose two bytes is left out whole"},
		{98, "–", 95, "a character of three bytes behind the room is left out, the field is full"},
		{95, "\xF0\x9F\x9A\x90", 95, "a character of four bytes that ends with the field fits"},
		{96, "\xF0\x9F\x9A\x90", 92, "a character of four bytes that would lose its last byte is left out whole"},
		{97, "\xF0\x9F\x9A\x90", 93, "a character of four bytes that would lose two bytes is left out whole"},
		{98, "\xF0\x9F\x9A\x90", 94, "a character of four bytes that would lose three bytes is left out whole"},
	};
	// wanted: room for kept and what stands before it - gcc refuses a snprintf() that might cut
	static char kept[400], wanted[400 + 16];
	const char *texts[1];

	for(int i = 0; i < COUNT(cuts); i++)
	{
		const char *text = long_text(cuts[i].length, cuts[i].special);

		memcpy(kept, text, cuts[i].kept);
		kept[cuts[i].kept] = '\0';

		// A row of the info
		stage_on(NAV_INFO, 0);
		texts[0] = text;
		world.info_lines = 1;
		input.info = texts;
		build();
		snprintf(what, sizeof(what), "a text of the info, cut at a character: %s", cuts[i].rule);
		check(scene->row_count == 1 && strcmp(scene->rows[0].text, kept) == 0, what);

		// A text line: the address
		stage_on(NAV_WEB, 0);
		input.address = text;
		build();
		snprintf(what, sizeof(what), "an address, cut at a character: %s", cuts[i].rule);
		check(scene->line_count == 1 && strcmp(scene->lines[0], kept) == 0, what);

		// A line of an overlay: what the browser asks for
		stage();
		world.asking = ACCESS_ASK_WIFI;
		input.ask_detail = text;
		build();
		snprintf(what, sizeof(what), "the detail of a question, cut at a character: %s", cuts[i].rule);
		check(scene->over_line_count == 3 && strcmp(scene->over_lines[1], kept) == 0 && strcmp(scene->over_lines[2], "Drücken = ja · lang = nein (0 s)") == 0, what);
	}

	// Behind a text that stands before it: name and password of the access point
	for(int i = 0; i < COUNT(cuts); i++)
	{
		const char *text = long_text(cuts[i].length - 6, cuts[i].special);

		stage_on(NAV_WEB, 0);
		input.ap_on = true;
		input.ap_ssid = text;
		input.ap_password = "kurz";
		build();
		memcpy(kept, text, cuts[i].kept - 6);
		kept[cuts[i].kept - 6] = '\0';
		snprintf(wanted, sizeof(wanted), "WLAN: %s", kept);
		snprintf(what, sizeof(what), "the name of the access point behind \"WLAN: \", cut at a character: %s", cuts[i].rule);
		check(scene->line_count == 3 && strcmp(scene->lines[1], wanted) == 0 && strcmp(scene->lines[2], "Passwort: kurz") == 0, what);

		text = long_text(cuts[i].length - 10, cuts[i].special);
		input.ap_ssid = "kurz";
		input.ap_password = text;
		build();
		memcpy(kept, text, cuts[i].kept - 10);
		kept[cuts[i].kept - 10] = '\0';
		snprintf(wanted, sizeof(wanted), "Passwort: %s", kept);
		snprintf(what, sizeof(what), "the password of the access point behind \"Passwort: \", cut at a character: %s", cuts[i].rule);
		check(scene->line_count == 3 && strcmp(scene->lines[1], "WLAN: kurz") == 0 && strcmp(scene->lines[2], wanted) == 0, what);
	}

	// Bytes that continue no character
	stage_on(NAV_WEB, 0);
	memset(kept, 0x80, 300);
	kept[300] = '\0';
	input.address = kept;
	build();
	check(scene->lines[0][0] == '\0' && scene->line_count == 1, "an address of 300 bytes that all continue a character that never began: nothing of it is kept, no byte before the text is read");
	memset(kept, 0xC3, 300);
	build();
	check(strlen(scene->lines[0]) == 95 && (unsigned char)scene->lines[0][94] == 0xC3, "an address of 300 bytes that all begin a character: 95 of them are kept");
}

/* scene_dump() ----------------------------------------------------------------------------------------- */

static scene_t made;

// A scene nobody built: every part of it in use
static void make_scene(void)
{
	memset(&made, 0, sizeof(made));
	made.kind = SCENE_LIST;
	made.ring.kind = RING_PROGRESS;
	made.ring.permille = 277;
	strcpy(made.title, "Titel");
	strcpy(made.note, "Notiz");
	made.item_count = 2;
	strcpy(made.items[0].label, "Drehzahl");
	strcpy(made.items[0].text, "812");
	strcpy(made.items[0].unit, "1/min");
	made.items[0].tone = SCENE_TONE_NORMAL;
	made.items[0].widget = LAYOUT_WIDGET_ARC;
	made.items[0].permille = 162;
	strcpy(made.items[1].label, "Tank");
	made.items[1].tone = SCENE_TONE_ALARM;
	made.items[1].widget = LAYOUT_WIDGET_BAR;
	made.items[1].permille = -1;
	made.dots = 7;
	made.dot = 1;
	made.row_count = 2;
	made.rows[0].kind = SCENE_ROW_ACTION;
	strcpy(made.rows[0].text, "Lesen");
	made.rows[0].enabled = true;
	made.rows[0].focus = true;
	made.rows[1].kind = SCENE_ROW_SUB;
	strcpy(made.rows[1].text, "P242F-FA");
	strcpy(made.rows[1].detail, "gespeichert");
	made.first = 3;
	made.total = 12;
	made.line_count = 2;
	strcpy(made.lines[0], "Erste Zeile");
	strcpy(made.big, "5/18");
	made.permille = 277;
	strcpy(made.options[0], "Abbrechen");
	strcpy(made.options[1], "Löschen");
	made.option = 1;
	made.over = SCENE_OVER_ASK;
	made.over_line_count = 2;
	strcpy(made.over_lines[0], "WLAN speichern?");
	strcpy(made.over_lines[1], "Werkstatt");
	made.over_permille = 420;
}

// A scene with nothing in it
static void make_empty(void)
{
	memset(&made, 0, sizeof(made));
	made.dot = -1;
	made.permille = -1;
	made.over_permille = -1;
}

// The dump of `made` is `head` and then exactly `text`
static bool dump_is(const char *head, const char *text)
{
	char wanted[1024];
	int length = dump_into(&made, SCENE_DUMP_SIZE);

	snprintf(wanted, sizeof(wanted), "%s%s", head, text);
	if(length == (int)strlen(wanted) && strcmp(dumped, wanted) == 0) return true;
	printf("  the dump wanted\n%s  is (%d)\n%.*s", wanted, length, length >= 0 ? length : 0, dumped);
	return false;
}

#define EMPTY_HEAD  "kind: values\nring: none\ntitle:\nnote:\n"

// A dump of texts without their zero, put together here
static char filled[SCENE_DUMP_SIZE];

// Appends `text` and `count` times the byte `fill`
static void add(const char *text, char fill, size_t count)
{
	size_t length = strlen(filled);

	if(length + strlen(text) + count >= sizeof(filled))
	{
		setup_failures++;
		return;
	}
	strcpy(&filled[length], text);
	length += strlen(text);
	memset(&filled[length], fill, count);
	filled[length + count] = '\0';
}

static void test_dump(void)
{
	static const char *const kinds[] = {"values", "notice", "list", "progress", "choice", "level", "?", "?", "?"};
	static const int kind_numbers[] = {0, 1, 2, 3, 4, 5, 6, -1, 256};
	static const char *const rings[] = {"none", "yellow", "grey", "red", "progress 5", "?", "?", "?"};
	static const int ring_numbers[] = {0, 1, 2, 3, 4, 5, -1, 256 + 4};
	static const char *const tones[] = {"normal", "dim", "warn", "alarm", "?", "?", "?"};
	static const int tone_numbers[] = {0, 1, 2, 3, 4, -1, 257};
	static const char *const widgets[] = {"number", "arc", "bar", "state", "?", "?", "?"};
	static const int widget_numbers[] = {0, 1, 2, 3, 4, -1, 258};
	static const char *const row_kinds[] = {"action", "head", "line", "sub", "?", "?", "?"};
	static const int row_numbers[] = {0, 1, 2, 3, 4, -1, 259};
	static const char *const overs[] = {"upload", "ask", "update", "?", "?", "?"};
	static const int over_numbers[] = {1, 2, 3, 4, -1, 256};
	static const int numbers[] = {0, 1, -1, 7, 1000, -1000, INT_MAX, INT_MIN};
	static const char *const digits[] = {"0", "1", "-1", "7", "1000", "-1000", "2147483647", "-2147483648"};
	static char full[1024];
	static char text[1200];
	scene_t *heap;
	size_t length;
	bool same, empty;

	make_scene();
	check(dump_is_file(&made, "dump_full"), "scene_dump_full.txt: the dump of a scene with every field in use, in the order of the struct");
	make_empty();
	check(dump_is_file(&made, "dump_empty"), "scene_dump_empty.txt: a scene with nothing in it is its kind, its ring and an empty title and note, with no blank behind the colons");
	memset(&made, 0, sizeof(made));
	check(dump_is_file(&made, "dump_zeros"), "scene_dump_zeros.txt: a scene of zeros has a dot 0 of no dots, a permille of 0 and an overlay progress of 0, and those are written");

	// What is written does not depend on the kind: every field that is in use, whatever the scene is
	for(int i = 0; i < COUNT(kind_numbers); i++)
	{
		make_scene();
		made.kind = (scene_kind_t)kind_numbers[i];
		snprintf(text, sizeof(text), "kind: %s\nring: progress 277\ntitle: Titel\nnote: Notiz\nitem: Drehzahl | 812 | 1/min | normal | arc | 162\nitem: Tank |  |  | alarm | bar | -1\ndots: 2/7\n"
		         "row: > action | Lesen |  | enabled\nrow: - sub | P242F-FA | gespeichert | disabled\nfirst: 3\ntotal: 12\nline: Erste Zeile\nline:\nbig: 5/18\npermille: 277\n"
		         "option: - Abbrechen\noption: > Löschen\nover: ask\nover_line: WLAN speichern?\nover_line: Werkstatt\nover_permille: 420\n", kinds[i]);
		snprintf(what, sizeof(what), "dump: a scene of the kind %d with every field in use is written with every field, whatever its kind uses", kind_numbers[i]);
		check(dump_is("", text), what);
	}

	// Every size from none to more than enough
	make_scene();
	// Counted by hand in the fixture: 21 lines, each with its line break
	length = 416;
	check(dump_into(&made, SCENE_DUMP_SIZE) == (int)length && strlen(dumped) == length, "the dump of the scene with every field in use has 416 bytes, and that is what is returned");
	memcpy(full, dumped, sizeof(full));
	full[sizeof(full) - 1] = '\0';
	same = empty = true;
	for(size_t size = 0; size <= length + 40; size++)
	{
		int returned = dump_into(&made, size);

		if(size > length)
		{
			if(returned != (int)length || memcmp(dumped, full, length + 1) != 0) same = false;
		}
		else
		{
			// -2: a byte outside of the room was written
			if(returned != -1) same = false;
			if(size > 0 && dumped[0] != '\0') empty = false;
		}
	}
	check(same, "the dump in rooms of 0 to 456 bytes: -1 while the text and its zero do not fit, from 417 bytes on the text and its length, and never a byte outside the room");
	check(empty, "a dump that does not fit leaves an empty text in every room of 1 to 416 bytes");
	check(dump_into(&made, length) == -1 && dumped[0] == '\0', "a dump of 416 bytes does not fit into a room of 416 bytes");
	check(dump_into(&made, length + 1) == (int)length && dumped[length - 1] == '\n' && dumped[length] == '\0', "a dump of 416 bytes fits into a room of 417 bytes and ends with a line break and its zero");
	check(dump_into(&made, 0) == -1 && scene_dump(&made, NULL, 0) == -1, "a dump without a room (0 bytes, also NULL): -1, and nothing is written");
	make_empty();
	check(dump_into(&made, 37) == -1 && dump_into(&made, 38) == 37, "the dump of a scene with nothing in it has 37 bytes and needs a room of 38");
	made.kind = SCENE_LIST;
	made.ring.kind = RING_RED;
	check(dump_into(&made, 34) == -1 && dump_into(&made, 35) == 34 && strcmp(dumped, "kind: list\nring: red\ntitle:\nnote:\n") == 0,
	      "the smallest dump there is, a list with a red ring and nothing else, has 34 bytes and needs a room of 35");

	// The words of the enums
	for(int i = 0; i < COUNT(kind_numbers); i++)
	{
		make_empty();
		made.kind = (scene_kind_t)kind_numbers[i];
		snprintf(text, sizeof(text), "kind: %s\nring: none\ntitle:\nnote:\n", kinds[i]);
		snprintf(what, sizeof(what), "dump: the kind %d is written as \"%s\"", kind_numbers[i], kinds[i]);
		check(dump_is("", text), what);
	}
	for(int i = 0; i < COUNT(ring_numbers); i++)
	{
		make_empty();
		made.ring.kind = (ring_kind_t)ring_numbers[i];
		made.ring.permille = 5;
		snprintf(text, sizeof(text), "kind: values\nring: %s\ntitle:\nnote:\n", rings[i]);
		snprintf(what, sizeof(what), "dump: the ring %d with a permille of 5 is written as \"%s\"", ring_numbers[i], rings[i]);
		check(dump_is("", text), what);
	}
	for(int i = 0; i < COUNT(tone_numbers); i++)
	{
		make_empty();
		made.item_count = 1;
		made.items[0].tone = (scene_tone_t)tone_numbers[i];
		snprintf(text, sizeof(text), "item:  |  |  | %s | number | 0\n", tones[i]);
		snprintf(what, sizeof(what), "dump: the tone %d is written as \"%s\"; an item without texts keeps its bars", tone_numbers[i], tones[i]);
		check(dump_is(EMPTY_HEAD, text), what);
	}
	for(int i = 0; i < COUNT(widget_numbers); i++)
	{
		make_empty();
		made.item_count = 1;
		made.items[0].widget = (layout_widget_t)widget_numbers[i];
		made.items[0].permille = -1;
		snprintf(text, sizeof(text), "item:  |  |  | normal | %s | -1\n", widgets[i]);
		snprintf(what, sizeof(what), "dump: the widget %d is written as \"%s\"", widget_numbers[i], widgets[i]);
		check(dump_is(EMPTY_HEAD, text), what);
	}
	for(int i = 0; i < COUNT(row_numbers); i++)
	{
		make_empty();
		made.row_count = 1;
		made.rows[0].kind = (scene_row_kind_t)row_numbers[i];
		made.total = 1;
		snprintf(text, sizeof(text), "row: - %s |  |  | disabled\nfirst: 0\ntotal: 1\n", row_kinds[i]);
		snprintf(what, sizeof(what), "dump: the row kind %d is written as \"%s\"; a row without texts keeps its bars", row_numbers[i], row_kinds[i]);
		check(dump_is(EMPTY_HEAD, text), what);
	}
	for(int i = 0; i < COUNT(over_numbers); i++)
	{
		make_empty();
		made.over = (scene_over_t)over_numbers[i];
		snprintf(text, sizeof(text), "over: %s\n", overs[i]);
		snprintf(what, sizeof(what), "dump: the overlay %d is written as \"%s\"", over_numbers[i], overs[i]);
		check(dump_is(EMPTY_HEAD, text), what);
	}

	// Numbers
	for(int i = 0; i < COUNT(numbers); i++)
	{
		make_empty();
		made.ring.kind = RING_PROGRESS;
		made.ring.permille = numbers[i];
		made.item_count = 1;
		made.items[0].permille = numbers[i];
		made.first = numbers[i];
		made.total = numbers[i];
		made.permille = numbers[i];
		made.over_permille = numbers[i];
		made.dots = numbers[i];
		snprintf(text, sizeof(text), "kind: values\nring: progress %s\ntitle:\nnote:\nitem:  |  |  | normal | number | %s\n%s%s%s%s%s%s%s%s%s%s%s%s%s", digits[i], digits[i],
		         numbers[i] != 0 ? "dots: 0/" : "", numbers[i] != 0 ? digits[i] : "", numbers[i] != 0 ? "\n" : "",
		         numbers[i] != 0 ? "first: " : "", numbers[i] != 0 ? digits[i] : "", numbers[i] != 0 ? "\ntotal: " : "", numbers[i] != 0 ? digits[i] : "", numbers[i] != 0 ? "\n" : "",
		         numbers[i] != -1 ? "permille: " : "", numbers[i] != -1 ? digits[i] : "", numbers[i] != -1 ? "\nover_permille: " : "", numbers[i] != -1 ? digits[i] : "", numbers[i] != -1 ? "\n" : "");
		snprintf(what, sizeof(what), "dump: the number %d in every field that is a number is written as \"%s\", or left out where it says that the field is not used", numbers[i], digits[i]);
		check(dump_is("", text), what);
	}
	make_empty();
	made.dots = 3;
	made.dot = 2;
	check(dump_is(EMPTY_HEAD, "dots: 3/3\n"), "dump: the last of three dots is written as 3/3");
	made.dot = 0;
	check(dump_is(EMPTY_HEAD, "dots: 1/3\n"), "dump: the first of three dots is written as 1/3");
	made.dot = -1;
	check(dump_is(EMPTY_HEAD, "dots: 0/3\n"), "dump: no dot of three is written as 0/3");
	made.dots = 0;
	made.dot = 0;
	check(dump_is(EMPTY_HEAD, "dots: 1/0\n"), "dump: a dot 0 of no dots is written, only dot -1 of no dots is left out");
	made.dot = -2;
	check(dump_is(EMPTY_HEAD, "dots: -1/0\n"), "dump: a dot -2 of no dots is written as -1/0");
	made.dot = INT_MAX;
	check(dump_is(EMPTY_HEAD, "dots: 2147483648/0\n"), "dump: the largest dot is written as 2147483648, one more than it");
	made.dot = INT_MIN;
	check(dump_is(EMPTY_HEAD, "dots: -2147483647/0\n"), "dump: the smallest dot is written as -2147483647");
	make_empty();
	made.first = 2;
	check(dump_is(EMPTY_HEAD, "first: 2\ntotal: 0\n"), "dump: first and total are written when only first is not 0");
	made.first = 0;
	made.total = 2;
	check(dump_is(EMPTY_HEAD, "first: 0\ntotal: 2\n"), "dump: first and total are written when only total is not 0");
	make_empty();
	made.permille = 0;
	check(dump_is(EMPTY_HEAD, "permille: 0\n"), "dump: a permille of 0 is written, the overlay progress of -1 next to it is not");
	make_empty();
	made.over_permille = 0;
	check(dump_is(EMPTY_HEAD, "over_permille: 0\n"), "dump: an overlay progress of 0 is written, the permille of -1 next to it is not");
	make_empty();
	made.permille = -2;
	made.over_permille = -2;
	check(dump_is(EMPTY_HEAD, "permille: -2\nover_permille: -2\n"), "dump: a permille and an overlay progress of -2 are written, only -1 is left out");
	make_empty();
	made.ring.kind = RING_RED;
	made.ring.permille = 500;
	check(dump_is("", "kind: values\nring: red\ntitle:\nnote:\n"), "dump: the permille of a ring that is no progress is not written");

	// Texts
	make_empty();
	strcpy(made.title, "T");
	strcpy(made.note, "N");
	check(dump_is("", "kind: values\nring: none\ntitle: T\nnote: N\n"), "dump: title and note of one byte stand behind a blank");
	make_empty();
	strcpy(made.big, "B");
	check(dump_is(EMPTY_HEAD, "big: B\n"), "dump: a big text of one byte is written");
	make_empty();
	strcpy(made.options[0], "A");
	check(dump_is(EMPTY_HEAD, "option: > A\noption: -\n"), "dump: both options are written when only the first has a text, the empty one without a blank at its end; option 0 marks the first");
	make_empty();
	strcpy(made.options[1], "B");
	made.option = 1;
	check(dump_is(EMPTY_HEAD, "option: -\noption: > B\n"), "dump: both options are written when only the second has a text; option 1 marks the second");
	made.option = 2;
	check(dump_is(EMPTY_HEAD, "option: -\noption: - B\n"), "dump: an option 2 marks neither answer");
	made.option = -1;
	check(dump_is(EMPTY_HEAD, "option: -\noption: - B\n"), "dump: an option -1 marks neither answer");
	make_empty();
	made.option = 1;
	check(dump_is(EMPTY_HEAD, ""), "dump: without a text in either option no option is written, whatever the focus is");
	make_empty();
	made.line_count = 3;
	strcpy(made.lines[1], "zwei");
	strcpy(made.lines[3], "vier");
	check(dump_is(EMPTY_HEAD, "line:\nline: zwei\nline:\n"), "dump: three lines are written as three, the empty ones without a blank, a fourth that is not counted is not written");
	made.line_count = 4;
	check(dump_is(EMPTY_HEAD, "line:\nline: zwei\nline:\nline: vier\n"), "dump: four lines are written as four");
	made.line_count = 5;
	strcpy(made.big, "nicht");
	check(dump_is(EMPTY_HEAD, "line:\nline: zwei\nline:\nline: vier\nbig: nicht\n"), "dump: a count of 5 lines writes the four there is room for, and not the field behind them as a line");
	made.line_count = INT_MAX;
	check(dump_is(EMPTY_HEAD, "line:\nline: zwei\nline:\nline: vier\nbig: nicht\n"), "dump: the largest count of lines writes the four there is room for");
	made.line_count = -1;
	check(dump_is(EMPTY_HEAD, "big: nicht\n"), "dump: a count of -1 lines writes none");
	made.line_count = INT_MIN;
	check(dump_is(EMPTY_HEAD, "big: nicht\n"), "dump: the smallest count of lines writes none");
	make_empty();
	made.over_line_count = 4;
	strcpy(made.over_lines[0], "eins");
	strcpy(made.over_lines[2], "drei");
	check(dump_is(EMPTY_HEAD, "over_line: eins\nover_line:\nover_line: drei\n"), "dump: a count of 4 overlay lines writes the three there is room for, without an overlay being named");
	made.over_line_count = 2;
	check(dump_is(EMPTY_HEAD, "over_line: eins\nover_line:\n"), "dump: two overlay lines are written as two");
	made.over_line_count = -1;
	check(dump_is(EMPTY_HEAD, ""), "dump: a count of -1 overlay lines writes none");
	make_empty();
	made.item_count = 7;
	for(int i = 0; i < LAYOUT_ITEMS_MAX; i++) snprintf(made.items[i].label, sizeof(made.items[i].label), "W%d", i);
	strcpy(text, "");
	for(int i = 0; i < LAYOUT_ITEMS_MAX; i++) snprintf(text + strlen(text), sizeof(text) - strlen(text), "item: W%d |  |  | normal | number | 0\n", i);
	check(dump_is(EMPTY_HEAD, text), "dump: a count of 7 items writes the six there is room for, in their order");
	made.item_count = -1;
	check(dump_is(EMPTY_HEAD, ""), "dump: a count of -1 items writes none");
	made.item_count = 1;
	check(dump_is(EMPTY_HEAD, "item: W0 |  |  | normal | number | 0\n"), "dump: a count of 1 item writes the first");
	make_empty();
	made.row_count = 6;
	for(int i = 0; i < SCENE_ROWS_MAX; i++) snprintf(made.rows[i].text, sizeof(made.rows[i].text), "R%d", i);
	made.rows[2].focus = true;
	made.rows[3].enabled = true;
	strcpy(made.rows[4].detail, "D");
	strcpy(made.lines[0], "nicht");
	check(dump_is(EMPTY_HEAD, "row: - action | R0 |  | disabled\nrow: - action | R1 |  | disabled\nrow: > action | R2 |  | disabled\nrow: - action | R3 |  | enabled\nrow: - action | R4 | D | disabled\n"),
	      "dump: a count of 6 rows writes the five there is room for; focus, enabled and detail each belong to their row");
	made.row_count = -1;
	check(dump_is(EMPTY_HEAD, ""), "dump: a count of -1 rows writes none");

	// Texts without their zero, and the largest dump there is
	heap = malloc(sizeof(*heap));
	if(heap == NULL) return;
	memset(heap, 'x', sizeof(*heap));
	heap->kind = SCENE_PROGRESS;
	heap->ring.kind = RING_PROGRESS;
	heap->ring.permille = INT_MIN;
	heap->item_count = INT_MAX;
	for(int i = 0; i < LAYOUT_ITEMS_MAX; i++)
	{
		heap->items[i].tone = SCENE_TONE_NORMAL;
		heap->items[i].widget = LAYOUT_WIDGET_NUMBER;
		heap->items[i].permille = INT_MIN;
	}
	heap->dots = INT_MIN;
	heap->dot = INT_MIN;
	heap->row_count = INT_MAX;
	for(int i = 0; i < SCENE_ROWS_MAX; i++)
	{
		heap->rows[i].kind = SCENE_ROW_ACTION;
		heap->rows[i].enabled = false;
		heap->rows[i].focus = true;
	}
	heap->first = INT_MIN;
	heap->total = INT_MIN;
	heap->line_count = INT_MAX;
	heap->permille = INT_MIN;
	heap->option = 0;
	heap->over = SCENE_OVER_UPLOAD;
	heap->over_line_count = INT_MAX;
	heap->over_permille = INT_MIN;
	// Counted by hand: 15 + 27 + 104 + 103, six items of 106, 30, five rows of 167, 19 + 19, four lines of 103, 46 + 22,
	// two options of 51, 13, three overlay lines of 108, 27
	check(SCENE_DUMP_SIZE == 4096 && dump_into(heap, SCENE_DUMP_SIZE) == 2734 && strlen(dumped) == 2734,
	      "the largest dump there is - every text fills its field without a zero, every count is beyond its array, the longest words and numbers - has 2734 bytes, and SCENE_DUMP_SIZE is 4096");
	filled[0] = '\0';
	add("kind: progress\nring: progress -2147483648\ntitle: ", 'x', 96);
	add("\nnote: ", 'x', 96);
	for(int i = 0; i < LAYOUT_ITEMS_MAX; i++)
	{
		add("\nitem: ", 'x', 25);
		add(" | ", 'x', 24);
		add(" | ", 'x', 12);
		add(" | normal | number | -2147483648", 'x', 0);
	}
	add("\ndots: -2147483647/-2147483648", 'x', 0);
	for(int i = 0; i < SCENE_ROWS_MAX; i++)
	{
		add("\nrow: > action | ", 'x', 96);
		add(" | ", 'x', 40);
		add(" | disabled", 'x', 0);
	}
	add("\nfirst: -2147483648\ntotal: -2147483648", 'x', 0);
	for(int i = 0; i < SCENE_LINES_MAX; i++) add("\nline: ", 'x', 96);
	add("\nbig: ", 'x', 40);
	add("\npermille: -2147483648\noption: > ", 'x', 40);
	add("\noption: - ", 'x', 40);
	add("\nover: upload", 'x', 0);
	for(int i = 0; i < 3; i++) add("\nover_line: ", 'x', 96);
	add("\nover_permille: -2147483648\n", 'x', 0);
	check(strcmp(dumped, filled) == 0, "in the largest dump every text without its zero ends with its field - title, note, lines and row texts 96 bytes, labels 25, values 24, units 12, details, big and options 40 - and nothing of the field behind it is written");
	check(dump_into(heap, 2734) == -1 && dump_into(heap, 2735) == 2734, "the largest dump needs a room of 2735 bytes");
	free(heap);

	// Texts without their zero in front of fields whose first byte is not 0: the largest scene has a 0 there
	make_empty();
	made.row_count = 1;
	made.total = 1;
	memset(made.rows[0].text, 't', sizeof(made.rows[0].text));
	memset(made.rows[0].detail, 'd', sizeof(made.rows[0].detail));
	made.rows[0].enabled = true;
	made.rows[0].focus = true;
	memset(made.big, 'b', sizeof(made.big));
	made.permille = 0x01010101;
	memset(made.options[1], 'o', sizeof(made.options[1]));
	made.option = 1;
	filled[0] = '\0';
	add(EMPTY_HEAD "row: > action | ", 't', 96);
	add(" | ", 'd', 40);
	add(" | enabled\nfirst: 0\ntotal: 1\nbig: ", 'b', 40);
	add("\npermille: 16843009\noption: -\noption: > ", 'o', 40);
	add("\n", 'o', 0);
	check(dump_into(&made, SCENE_DUMP_SIZE) == (int)strlen(filled) && strcmp(dumped, filled) == 0,
	      "dump: a detail, a big text and an option without their zero end with their fields, also when the field behind them begins with another byte than 0");

	// A bool that holds neither 0 nor 1
	make_empty();
	made.row_count = 3;
	made.total = 3;
	memset(&made.rows[0].focus, 0x02, sizeof(made.rows[0].focus));
	memset(&made.rows[0].enabled, 0x80, sizeof(made.rows[0].enabled));
	memset(&made.rows[1].enabled, 0x02, sizeof(made.rows[1].enabled));
	memset(&made.rows[2].focus, 0xFF, sizeof(made.rows[2].focus));
	check(dump_is(EMPTY_HEAD, "row: > action |  |  | enabled\nrow: - action |  |  | enabled\nrow: > action |  |  | disabled\nfirst: 0\ntotal: 3\n"),
	      "dump: a focus or an enabled that holds another byte than 0 and 1 counts as true, each for itself");
}

/* Made-up inputs --------------------------------------------------------------------------------------- */

static uint32_t random_state;

static uint32_t random_number(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static int pick(int count)
{
	return (int)(random_number() % (uint32_t)count);
}

// A number of every kind: small ones around 0, the ends, numbers whose low byte is small
static int any_number(void)
{
	static const int strange[] = {INT_MIN, INT_MIN + 1, INT_MAX, INT_MAX - 1, -1, 256, 257, 65536, -256, 1000, 100, 101};

	return pick(4) == 0 ? strange[pick(COUNT(strange))] : pick(24) - 3;
}

static const char *any_text(void)
{
	static const char *const texts[] = {NULL, "", "x", "Werkstatt", "http://192.168.88.37", "Ein Text mit Umlauten äöüß – und einem Strich",
	                                    "ein sehr langer Text, der in kein Feld einer Szene passt und deshalb an einem Zeichen abgeschnitten werden muss äöü äöü äöü äöü äöü äöü äöü"};

	return texts[pick(COUNT(texts))];
}

// The rules of the header a second way: tables by screen, and the window searched row by row.

// When a choice does something
typedef enum
{
	ALWAYS,
	IF_READ,        // can_read
	IF_CLEAR,       // can_clear
	IF_OUTCOME,     // the own request left something to look at
	IF_OLD,         // a list from before the last clear is stored
	IF_PREVIOUS,    // the other slot holds a firmware
} when_t;

// The choices of the screens, in their order
static const struct
{
	nav_screen_t on;
	const char *text;
	when_t when;
} CHOICES[] = {
	{NAV_MENU, "Fehlerspeicher", ALWAYS}, {NAV_MENU, "Helligkeit", ALWAYS}, {NAV_MENU, "Nachtmodus", ALWAYS}, {NAV_MENU, "Web-Zugriff", ALWAYS}, {NAV_MENU, "Info", ALWAYS},
	{NAV_MENU, "Einstellungen", ALWAYS}, {NAV_MENU, "Zurück", ALWAYS},
	{NAV_DTC, "Lesen", IF_READ}, {NAV_DTC, "Liste ansehen", IF_OUTCOME}, {NAV_DTC, "Zuletzt gelöscht", IF_OLD}, {NAV_DTC, "Zurück", ALWAYS},
	{NAV_DTC_LIST, "Erneut lesen", IF_READ}, {NAV_DTC_LIST, "Fehler löschen", IF_CLEAR}, {NAV_DTC_LIST, "Zurück", ALWAYS},
	{NAV_DTC_CLEARED, "Fertig", ALWAYS},
	{NAV_DTC_OLD, "Zurück", ALWAYS},
	{NAV_WEB, "Freigabe", ALWAYS}, {NAV_WEB, "Zurück", ALWAYS},
	{NAV_SETTINGS, "Drehrichtung", ALWAYS}, {NAV_SETTINGS, "Hotspot", ALWAYS}, {NAV_SETTINGS, "Neustart", ALWAYS}, {NAV_SETTINGS, "Vorherige Version", IF_PREVIOUS},
	{NAV_SETTINGS, "Werkseinstellungen", ALWAYS}, {NAV_SETTINGS, "Zurück", ALWAYS},
};

// By the phase of a request, in the order of dtc_flow_phase_t; a phase that is none counts as the first
static const bool OUTCOMES[] = {false, false, false, true, false, false, true, true, true};
static const char *const BUSY_TITLES[] = {
	"Fehlerspeicher", "Fehlerspeicher lesen", "Fehlerspeicher lesen", "Fehlerspeicher", "Fehlerspeicher löschen", "Fehlerspeicher löschen", "Fehlerspeicher", "Fehlerspeicher",
	"Fehlerspeicher",
};
static const char *const FAILED_TITLES[] = {
	"Fehlerspeicher", "Fehlerspeicher", "Fehlerspeicher", "Fehlerspeicher", "Fehlerspeicher", "Fehlerspeicher", "Fehlerspeicher", "Fehlgeschlagen", "Stand unbekannt",
};
static const int FAILED_LINES[] = {0, 0, 0, 0, 0, 0, 0, 1, 1};
// Where the fault memory stands; NULL: what the list holds
static const char *const DTC_STANDS[] = {
	"Noch nicht gelesen", "Lesen läuft …", "Lesen läuft …", NULL, "Löschen läuft …", "Löschen läuft …", "Gelöscht", "Letzter Auftrag fehlgeschlagen",
	"Stand des Löschens unbekannt",
};
static const int BUSY_LINES[] = {0, 2, 2, 0, 2, 2, 0, 0, 0};

static bool holds(when_t when)
{
	switch(when)
	{
		case IF_READ:       return world.can_read;
		case IF_CLEAR:      return world.can_clear;
		case IF_OUTCOME:    return OUTCOMES[(unsigned)world.flow < (unsigned)COUNT(OUTCOMES) ? (unsigned)world.flow : 0];
		case IF_OLD:        return world.old_lines > 0;
		case IF_PREVIOUS:   return world.previous_firmware;
		default:            return true;
	}
}

static bool has_text(const char *text)
{
	return text != NULL && text[0] != '\0';
}

static bool begins(const char *text, const char *with)
{
	return strncmp(text, with, strlen(with)) == 0;
}

// What is wrong with the title, the note and the lines of the scene, NULL if nothing
static const char *model_texts(scene_kind_t kind, conn_view_t view)
{
	static char summed[64];
	unsigned phase = (unsigned)flow.phase < 9 ? (unsigned)flow.phase : 0;
	const char *title = "";
	const char *note = "";
	// The first line where the tables give its text, NULL where only the number of lines is compared
	const char *first = NULL;
	int lines = 0;

	switch(nav.screen)
	{
		case NAV_PAGES:
			if(kind == SCENE_VALUES) title = world.layout->pages[nav.page].title;
			else lines = view == CONN_VIEW_ECU_OFFLINE && conn_state(&conn)->batt_mv >= 0 ? 2 : 1;
			note = input.safe_mode ? SAFE_NOTE : input.heat != GUARD_HEAT_NORMAL ? HOT_NOTE : view == CONN_VIEW_SCAN ? SCAN_NOTE : view == CONN_VIEW_NO_API ? NO_API_NOTE : "";
			break;
		case NAV_MENU:
			title = "Menü";
			break;
		case NAV_DTC:
			title = "Fehlerspeicher";
			note = text_block(input.read_block);
			lines = 1;
			first = DTC_STANDS[phase];
			if(first == NULL && input.summary == NULL) first = "Liste gelesen";
			if(first == NULL)
			{
				snprintf(summed, sizeof(summed), "%lu Fehler in %d Steuergerät%s", (unsigned long)input.summary->codes, input.summary->ecus_with_codes,
				         input.summary->ecus_with_codes == 1 ? "" : "en");
				first = summed;
			}
			break;
		case NAV_DTC_BUSY:
			title = BUSY_TITLES[phase];
			lines = BUSY_LINES[phase];
			break;
		case NAV_DTC_LIST:
			title = "Fehlerspeicher";
			note = world.can_clear ? NULL : text_block(input.clear_block);
			break;
		case NAV_DTC_CONFIRM:
			title = "Fehler löschen?";
			note = "Auf Löschen drehen, Knopf 3 s halten";
			lines = input.summary != NULL ? 3 : 2;
			break;
		case NAV_DTC_CLEARED:
			title = "Gelöscht";
			break;
		case NAV_DTC_FAILED:
			title = FAILED_TITLES[phase];
			note = "Knopf drücken";
			lines = FAILED_LINES[phase];
			break;
		case NAV_DTC_OLD:
			title = "Zuletzt gelöscht";
			break;
		case NAV_BRIGHTNESS:
			title = world.night_mode ? "Helligkeit (Nacht)" : "Helligkeit";
			note = "Drehen zum Ändern, Drücken zum Speichern";
			break;
		case NAV_WEB:
			title = "Web-Zugriff";
			// A line for the address, two for the own access point, and one that says so where there is neither
			lines = (has_text(input.address) ? 1 : 0) + (input.ap_on ? 2 : 0);
			if(lines == 0)
			{
				lines = 1;
				first = "Kein WLAN";
			}
			break;
		case NAV_INFO:
			title = "Info";
			break;
		case NAV_SETTINGS:
			title = "Einstellungen";
			break;
		case NAV_CONFIRM:
			title = nav.confirm == NAV_DO_REBOOT ? "Neu starten?" : nav.confirm == NAV_DO_PREVIOUS_FIRMWARE ? "Vorherige Version starten?" :
			        nav.confirm == NAV_DO_FACTORY_RESET ? "Werkseinstellungen?" : "";
			lines = nav.confirm == NAV_DO_FACTORY_RESET ? 2 : 0;
			break;
		// What is no screen has no text
		default:
			break;
	}

	if(strcmp(scene->title, title) != 0) return "the title is not the one of the screen";
	if(note != NULL && strcmp(scene->note, note) != 0) return "the note is not the one of the screen";
	if(note == NULL && !begins(scene->note, "Löschen möglich: ")) return "the note of a list that may be cleared does not tell the time left";
	if(scene->line_count != lines) return "the number of lines is not the one of the screen";
	if(first != NULL && strcmp(scene->lines[0], first) != 0) return "the first line is not the one of the screen";
	if(nav.screen == NAV_WEB && has_text(input.address) && (scene->lines[0][0] == '\0' || !begins(input.address, scene->lines[0]))) return "the first line of the web access is not the address";
	if(nav.screen == NAV_WEB && input.ap_on && (!begins(scene->lines[lines - 2], "WLAN: ") || !begins(scene->lines[lines - 1], "Passwort: ")))
	{
		return "the last lines of the web access are not name and password of the own access point";
	}
	return NULL;
}

// What the values of the page shown say to the ring, from the age of each value and from the catalogue:
// *level is the worst level of a value that is shown, *old whether a value is old or missed
static void model_page(int *level, bool *old)
{
	const layout_page_t *page = &world.layout->pages[nav.page];
	char text[SCENE_VALUE_SIZE];

	for(int i = 0; i < page->item_count; i++)
	{
		const layout_item_t *item = &page->items[i];
		const value_t *value = values_find(&values, item->key);
		value_age_t age = values_age(value, input.now_ms);
		bool there = age != VALUE_AGE_GONE && layout_item_text(item, value, text, sizeof(text));
		// A value that is gone is only missed if the profile has it, or may have it: `unloaded` is the
		// catalogue before the profile arrived
		bool missed = !there && (age != VALUE_AGE_GONE || world.catalog == &unloaded || catalog_find(world.catalog, item->key) >= 0);

		if(there && layout_item_level(item, value) > *level) *level = layout_item_level(item, value);
		if(missed || (there && age == VALUE_AGE_OLD)) *old = true;
	}
}

// What is wrong with what lies over the scene, NULL if nothing
static const char *model_overlay(void)
{
	const char *first = "";
	int lines = 0;

	switch(scene->over)
	{
		case SCENE_OVER_UPLOAD:
			first = "Firmware wird übertragen";
			lines = 2;
			break;
		case SCENE_OVER_ASK:
			first = world.asking == ACCESS_ASK_WIFI ? "WLAN speichern?" : world.asking == ACCESS_ASK_FIRMWARE ? "Firmware installieren?" : world.asking == ACCESS_ASK_RESET ? "Werkseinstellungen?" : "";
			lines = has_text(input.ask_detail) ? 3 : 2;
			break;
		case SCENE_OVER_UPDATE:
			first = "Update in Ordnung?";
			lines = 3;
			break;
		case SCENE_OVER_NONE:
			break;
	}
	if(scene->over_line_count != lines) return "the number of overlay lines is not the one of the overlay";
	if(lines > 0 && strcmp(scene->over_lines[0], first) != 0) return "the first overlay line is not the one of the overlay";
	if(scene->over == SCENE_OVER_ASK && !begins(scene->over_lines[lines - 1], "Drücken = ja · lang = nein (")) return "the last line of a question does not tell how to answer";
	if(scene->over == SCENE_OVER_UPDATE && !begins(scene->over_lines[2], "sonst alte Version in ")) return "the last line of the update question does not tell the time left";
	return NULL;
}

// Returns what is wrong with the scene, NULL if nothing
static const char *model(void)
{
	static const scene_kind_t kinds[] = {
		SCENE_VALUES, SCENE_LIST, SCENE_LIST, SCENE_PROGRESS, SCENE_LIST, SCENE_CHOICE, SCENE_LIST, SCENE_NOTICE, SCENE_LIST, SCENE_LEVEL, SCENE_LIST, SCENE_LIST,
		SCENE_LIST, SCENE_CHOICE,
	};
	nav_screen_t on = nav.screen;
	conn_view_t view = conn_view(&conn, input.now_ms);
	bool known = (unsigned)on <= NAV_CONFIRM;
	bool values_view = view == CONN_VIEW_LIVE || view == CONN_VIEW_SCAN || view == CONN_VIEW_NO_API;
	scene_kind_t kind = known ? kinds[on] : SCENE_NOTICE;
	scene_over_t over = world.uploading ? SCENE_OVER_UPLOAD : world.asking != ACCESS_ASK_NONE ? SCENE_OVER_ASK : world.update_pending ? SCENE_OVER_UPDATE : SCENE_OVER_NONE;
	const char *wrong;
	int dots = 0, dot = -1, level = 0;
	bool old = false;
	ring_t ring;

	if(on == NAV_PAGES && (!values_view || nav.page < 0 || nav.page >= world.layout->page_count)) kind = SCENE_NOTICE;
	if(scene->kind != kind) return "the kind is not the one of the screen";
	if(scene->over != over) return "the overlay is not the one of the world";
	wrong = model_texts(kind, view);
	if(wrong == NULL) wrong = model_overlay();
	if(wrong != NULL) return wrong;

	if(on == NAV_PAGES)
	{
		for(int i = 0; i < world.layout->page_count; i++)
		{
			if(!layout_page_shown(world.layout, i, world.catalog)) continue;
			if(i == nav.page) dot = dots;
			dots++;
		}
	}
	if(scene->dots != dots || scene->dot != dot) return "the dots are not those of the layout";

	// The ring: on a value page by its values; a progress and the dialog with the hold have their own arc
	if(kind == SCENE_VALUES) model_page(&level, &old);
	ring = ring_state(view, conn_state(&conn), level, old);
	if(ring.kind == RING_PROGRESS && (kind == SCENE_PROGRESS || (kind == SCENE_CHOICE && on != NAV_CONFIRM)))
	{
		ring.kind = RING_NONE;
		ring.permille = 0;
	}
	if(scene->ring.kind != ring.kind || scene->ring.permille != ring.permille) return "the ring is not that of the connection and of the values on the page";

	if(kind == SCENE_LIST)
	{
		const void *lines = on == NAV_DTC_LIST ? (const void *)input.list : on == NAV_DTC_CLEARED ? (const void *)input.cleared : on == NAV_DTC_OLD ? (const void *)input.old :
		                    on == NAV_INFO ? (const void *)input.info : NULL;
		int first_choice = 0, choices = 0, total, first, focused = 0;

		for(int i = COUNT(CHOICES) - 1; i >= 0; i--)
		{
			if(CHOICES[i].on != on) continue;
			first_choice = i;
			choices++;
		}
		total = lines != NULL ? nav_rows(&nav, &world) : choices;
		first = window_model(nav.row, total);

		if(scene->total != total || scene->first != first) return "the window of the list is not the one searched row by row";
		for(int i = 0; i < scene->row_count; i++)
		{
			const scene_row_t *row = &scene->rows[i];
			int choice = first + i - (total - choices);

			if(row->focus != (first + i == nav.row)) return "the focus is not on the row of the knob";
			if((row->kind == SCENE_ROW_ACTION) != (choice >= 0)) return "the choices are not the last rows";
			if(choice >= 0 && strcmp(row->text, CHOICES[first_choice + choice].text) != 0) return "a choice is not the one of the table";
			if(row->enabled != (choice < 0 || holds(CHOICES[first_choice + choice].when))) return "a row is enabled although it does nothing, or the other way round";
			if(row->focus) focused++;
		}
		if(focused != (nav.row >= first && nav.row < first + scene->row_count ? 1 : 0)) return "the focus is visible although it is on no visible row, or the other way round";
	}
	if(kind == SCENE_CHOICE)
	{
		if(scene->option != (nav.row > 0 ? 1 : 0)) return "the answer in focus is not the one of the knob";
		if(scene->permille != (on == NAV_CONFIRM ? -1 : hold_permille(&hold, input.now_ms))) return "the hold is not the one of the clear dialog";
		if(strcmp(scene->options[0], "Abbrechen") != 0 || strcmp(scene->options[1], on == NAV_CONFIRM ? "Ausführen" : "Löschen") != 0) return "the answers are not those of the dialog";
	}
	if(kind == SCENE_LEVEL && scene->permille != (nav.value < 0 ? 0 : nav.value > 100 ? 1000 : nav.value * 10)) return "the level is not the brightness";
	if(kind == SCENE_PROGRESS && scene->line_count != 0 && strcmp(scene->lines[scene->line_count - 1], HINT) != 0) return "the last line of a progress is not the hint";
	return NULL;
}

static void test_made_up(void)
{
	static const uint32_t seeds[] = {1, 2, 3, 20261004, 0xC0FFEE, 0xDEADBEEF, 77, 4096};
	static const char *const names[] = {"", "N30/4 ESP", "N2/14 Rückhaltesystem (SRS)", "Radio", "N10", "(", "N73 Elektronisches Zündschloss mit sehr langem Namen (EZS-ABC)"};
	static const char *const reasons[] = {"", "busy", "not_ready", "engine_running", "http_500", "restarted", "a reason of the longest size 31"};
	static const layout_t *const layouts[] = {&layout, &hidden_layout, &scratch};
	static int screens[16], views[10], overs[4], kinds[6];
	// What the arc of a screen and the lines of fault memory and web access depend on
	static int stands[9];
	int own_arcs = 0, summed = 0, unsummed = 0, hotspots = 0, no_networks = 0;
	int differences = 0, dumps = 0, scenes = 0;
	bool reached = true;

	for(int s = 0; s < COUNT(seeds); s++)
	{
		random_state = seeds[s];
		for(int n = 0; n < 4000; n++)
		{
			const char *wrong;
			int view = pick(COUNT(VIEWS));
			int length;

			stage();
			// The adapter and the connection
			adapter.batt_mv = pick(3) == 0 ? -1 : pick(30000);
			adapter.sleep_in_s = pick(3) - 1;
			scan((wican_dtc_phase_t)pick(6), 40 + (uint32_t)pick(3), pick(2) == 0, pick(4) == 0 ? random_number() : (uint32_t)pick(20),
			     pick(4) == 0 ? random_number() : (uint32_t)pick(20), names[pick(COUNT(names))]);
			if(view != CONN_VIEW_SCAN && view != CONN_VIEW_LIVE && view != CONN_VIEW_ECU_OFFLINE) VIEWS[view].make();
			else
			{
				// A scan, live or the ignition off: whichever the made-up state says
				adapter.ecu_online = pick(2) == 0;
				conn_init(&conn, ID);
				conn_wifi(&conn, true, NOW - 5000);
				if(conn_next(&conn, NOW - 5000) != CONN_ASK_STATE) setup_failures++;
				conn_got_state(&conn, CONN_GOT_OK, &adapter, NOW - 4900);
			}
			views[conn_view(&conn, NOW)]++;

			// The values: the fresh ones of the stage, or old ones, or none
			if(pick(4) == 0) values_init(&values);
			if(pick(4) == 0) seen("{\"ENGINE_RPM\":4600,\"COOLANT_TMP\":110,\"ACCEL_PEDAL\":\"on\",\"@BATT_V\":1e300}", (uint64_t)pick(12000));

			// The navigation
			nav.screen = (nav_screen_t)(pick(8) == 0 ? any_number() : pick(3) == 0 ? (int)NAV_PAGES : pick(14));
			nav.page = pick(3) == 0 ? any_number() : pick(8);
			nav.row = any_number();
			nav.value = any_number();
			nav.confirm = (nav_do_t)(pick(3) == 0 ? any_number() : (int)NAV_DO_REBOOT + pick(3));

			// The world
			world.layout = layouts[pick(COUNT(layouts))];
			if(world.layout == &scratch)
			{
				layout_item_t *item = probe();

				item->widget = (layout_widget_t)any_number();
				item->min.set = pick(4) != 0;
				item->max.set = pick(4) != 0;
				item->min.value = pick(3) == 0 ? (double)NAN : pick(7) - 3;
				item->max.value = pick(3) == 0 ? 1e308 : pick(7) - 3;
				item->scale = pick(3) == 0 ? -1e300 : 1;
				seen("{\"X\":2}", (uint64_t)pick(12000));
				scratch.page_count = (uint8_t)pick(2);
				nav.page = pick(2) == 0 ? 0 : any_number();
			}
			world.catalog = pick(4) == 0 ? &unloaded : &catalog;
			world.flow = (dtc_flow_phase_t)(pick(4) == 0 ? any_number() : pick(9));
			world.can_read = pick(2) == 0;
			world.can_clear = pick(2) == 0;
			world.asking = (access_ask_t)(pick(3) == 0 ? any_number() : 0);
			world.release_open = pick(2) == 0;
			world.update_pending = pick(3) == 0;
			world.uploading = pick(5) == 0;
			world.previous_firmware = pick(2) == 0;
			world.night_mode = pick(2) == 0;
			world.brightness = any_number();

			// The lists: with their lines, or counted without them - then the count may be anything
			if(pick(3) != 0)
			{
				input.list = mixed_lines;
				world.list_lines = pick(mixed_count + 1);
			}
			else world.list_lines = any_number();
			if(pick(3) != 0)
			{
				input.cleared = cleared_lines;
				world.cleared_lines = pick(cleared_count + 1);
			}
			else world.cleared_lines = any_number();
			if(pick(3) != 0)
			{
				input.old = codes_lines;
				world.old_lines = pick(codes_count + 1);
			}
			else world.old_lines = any_number();
			if(pick(3) != 0)
			{
				input.info = INFO;
				world.info_lines = pick(COUNT(INFO) + 1);
			}
			else world.info_lines = any_number();

			// The request
			flow.phase = (dtc_flow_phase_t)(pick(4) == 0 ? any_number() : pick(9));
			flow.seq = 40 + (uint32_t)pick(3);
			flow.list_end_ms = pick(2) == 0 ? NOW - (uint64_t)pick(700000) : NOW + 1000;
			SET(flow.reason, reasons[pick(COUNT(reasons))]);
			input.read_block = (dtc_flow_block_t)any_number();
			input.clear_block = (dtc_flow_block_t)any_number();
			input.summary = pick(2) == 0 ? &mixed_summary : NULL;
			if(pick(2) == 0)
			{
				hold_open(&hold, NOW - 5000);
				hold_sample(&hold, false, true, true, NOW - 4000);
				hold_sample(&hold, true, true, true, NOW - (uint64_t)pick(4000));
			}
			if(pick(2) == 0) access_open(&gate, NOW - (uint64_t)pick(700000));
			if(pick(2) == 0) access_ask(&gate, ACCESS_ASK_WIFI, NOW - (uint64_t)pick(70000));

			// The rest
			input.address = any_text();
			input.ap_on = pick(2) == 0;
			input.ap_ssid = any_text();
			input.ap_password = any_text();
			input.reverse = pick(2) == 0;
			input.ask_detail = any_text();
			input.upload_percent = any_number();
			input.update_left_s = pick(4) == 0 ? random_number() : (uint32_t)pick(700);
			input.safe_mode = pick(4) == 0;
			input.heat = (guard_heat_t)(pick(3) == 0 ? any_number() : 0);
			input.now_ms = pick(8) == 0 ? NOW - (uint64_t)pick(100000) : NOW + (uint64_t)pick(20000);

			build();
			scenes++;
			wrong = model();
			if(wrong != NULL)
			{
				if(differences < 10) printf("  seed %lu, scene %d, screen %d: %s\n", (unsigned long)seeds[s], n, (int)nav.screen, wrong);
				differences++;
			}
			length = dump_into(scene, SCENE_DUMP_SIZE);
			if(length < 34 || length > 2734 || strlen(dumped) != (size_t)length)
			{
				if(dumps < 3) printf("  seed %lu, scene %d: the dump returned %d\n", (unsigned long)seeds[s], n, length);
				dumps++;
			}

			screens[(unsigned)nav.screen <= NAV_CONFIRM ? (int)nav.screen : 14]++;
			overs[scene->over]++;
			kinds[scene->kind]++;

			if((nav.screen == NAV_DTC_BUSY || nav.screen == NAV_DTC_CONFIRM) && conn_view(&conn, input.now_ms) == CONN_VIEW_SCAN) own_arcs++;
			if(nav.screen == NAV_DTC)
			{
				stands[(unsigned)flow.phase < 9 ? (unsigned)flow.phase : 0]++;
				if(flow.phase == DTC_FLOW_LIST && input.summary != NULL) summed++;
				if(flow.phase == DTC_FLOW_LIST && input.summary == NULL) unsummed++;
			}
			if(nav.screen == NAV_WEB && !has_text(input.address) && input.ap_on) hotspots++;
			if(nav.screen == NAV_WEB && !has_text(input.address) && !input.ap_on) no_networks++;
		}
	}
	for(int i = 0; i < 15; i++) reached = reached && screens[i] >= 500;
	for(int i = 0; i < COUNT(views); i++) reached = reached && views[i] >= 500;
	for(int i = 0; i < COUNT(overs); i++) reached = reached && overs[i] >= 500;
	for(int i = 0; i < COUNT(kinds); i++) reached = reached && kinds[i] >= 500;
	if(!reached)
	{
		for(int i = 0; i < 15; i++) printf("  screen %d: %d scenes\n", i, screens[i]);
		for(int i = 0; i < COUNT(views); i++) printf("  view %d: %d scenes\n", i, views[i]);
		for(int i = 0; i < COUNT(overs); i++) printf("  overlay %d: %d scenes\n", i, overs[i]);
		for(int i = 0; i < COUNT(kinds); i++) printf("  kind %d: %d scenes\n", i, kinds[i]);
	}
	check(scenes == 32000 && reached, "32000 made-up inputs reach every screen and what is none, every view of the connection, every overlay and every kind of scene, each at least 500 times");
	reached = own_arcs >= 50 && summed >= 20 && unsummed >= 20 && hotspots >= 50 && no_networks >= 50;
	for(int i = 0; i < COUNT(stands); i++) reached = reached && stands[i] >= 50;
	if(!reached)
	{
		printf("  progress screen or clear dialog during a scan: %d\n", own_arcs);
		for(int i = 0; i < COUNT(stands); i++) printf("  fault memory in phase %d: %d scenes\n", i, stands[i]);
		printf("  fault memory with a list and a summary: %d, without: %d\n", summed, unsummed);
		printf("  web access without an address, with the own access point: %d, without: %d\n", hotspots, no_networks);
	}
	check(reached, "the made-up inputs reach the two screens with an arc of their own during a scan, the web access without an address with and without the own access point "
	      "and the fault memory in every phase, each at least 50 times, a list with and without a summary at least 20 times");
	check(differences == 0, "the scenes of the made-up inputs have the kind, the title, the note, the lines, the overlay, the dots, the ring, the window, the focus, the choices, the answers and the level the rules give "
	      "when they are followed a second way");
	check(dumps == 0, "each of those scenes can be written as a text of 34 to 2734 bytes, the smallest and the largest dump there is");
}

// The values of the made-up value pages: as they usually are, and beyond a limit of fixtures/scene_layout.json
// or not to be printed
static const struct
{
	const char *key;
	const char *usual;
	const char *strange;
} VALUES[] = {
	{"ENGINE_RPM", "812", "4600"}, {"COOLANT_TMP", "88.4", "110"}, {"BOOST_PRESSURE", "1013", "1e15"}, {"ACCEL_PEDAL", "12.5", "\"on\""},
	{"DPF_REGEN_STATUS", "1", "7"}, {"@BATT_V", "14.1", "11.5"}, {"FUEL_L", "43", "4"}, {"GLOW_PLUG", "\"off\"", "\"on\""}, {"OIL_LEVEL", "61.25", "1e300"},
	{"LAMBDA", "1.337", "-1e12"}, {"ENGINE_OIL_TEMP", "94", "1e999"}, {"EGT_PRE_TURBO", "412", "900"}, {"EGT_PRE_DPF", "288.6", "1e12"},
	{"DPF_SOOT_MASS", "11.3", "55"}, {"TRANS_TEMP", "70", "1e13"}, {"X", "2", "1e300"},
};

// Value pages alone: every value for itself fresh, old, gone or never seen, at the limits of each age, within
// its limits, beyond them or not to be printed; the pages of the fixture or a page of one to six values with
// limits of its own; the profile loaded or not; mostly live, where the ring tells of the values
static void test_made_up_pages(void)
{
	static const uint32_t seeds[] = {5, 906, 20261004, 0xABCDEF};
	static const uint64_t ages[] = {0, 1, 2999, 3000, 3001, 9999, 10000, 10001, 60000};
	static int rings[5];
	int differences = 0, scenes = 0, dashes = 0, lacking = 0, back = 0;
	bool reached;

	for(int s = 0; s < COUNT(seeds); s++)
	{
		random_state = seeds[s];
		for(int n = 0; n < 4000; n++)
		{
			const char *wrong;
			bool shown = false, dash = false, unavailable = false;

			stage();
			values_init(&values);
			for(int i = 0; i < COUNT(VALUES); i++)
			{
				char json[64];

				// Never seen
				if(pick(5) == 0) continue;

				snprintf(json, sizeof(json), "{\"%s\":%s}", VALUES[i].key, pick(4) == 0 ? VALUES[i].strange : VALUES[i].usual);
				seen(json, pick(2) == 0 ? ages[pick(COUNT(ages))] : (uint64_t)pick(13000));
			}

			if(pick(2) == 0) nav.page = pick(8);
			else
			{
				layout_item_t *item = probe();
				int count = 1 + pick(LAYOUT_ITEMS_MAX);

				for(int i = 0; i < count; i++)
				{
					// One value more than the table has: TURBO_SPEED never arrives, and the profile does not have it
					int which = pick(COUNT(VALUES) + 1);

					if(i > 0) item = probe_more("");
					SET(item->key, which < COUNT(VALUES) ? VALUES[which].key : "TURBO_SPEED");
					item->widget = (layout_widget_t)pick(4);
					// Every value of the table that can be printed lies beyond these
					if(pick(6) == 0) limit(&item->warn_hi, 0);
					if(pick(12) == 0) limit(&item->crit_hi, 0);
				}
			}
			world.catalog = pick(4) == 0 ? &unloaded : &catalog;
			switch(pick(8))
			{
				case 0:
					view_no_api();
					break;
				case 1:
					view_scan();
					break;
				default:
					break;
			}
			input.safe_mode = pick(8) == 0;
			input.heat = pick(8) == 0 ? GUARD_HEAT_DIM : GUARD_HEAT_NORMAL;
			// Mostly the time the values were seen by; a later one; one before them, the clock of another task
			if(pick(8) == 0) input.now_ms = NOW + (uint64_t)pick(12000);
			if(pick(16) == 0)
			{
				input.now_ms = NOW - (uint64_t)pick(70000);
				back++;
			}

			build();
			scenes++;
			wrong = scene->kind == SCENE_VALUES ? model() : "a value page is no value page";
			if(wrong != NULL)
			{
				if(differences < 10) printf("  seed %lu, page %d: %s\n", (unsigned long)seeds[s], n, wrong);
				differences++;
			}

			for(int i = 0; i < scene->item_count; i++)
			{
				if(strcmp(scene->items[i].text, SCENE_DASH) == 0) dash = true;
				else if(strcmp(scene->items[i].text, SCENE_UNAVAILABLE) == 0) unavailable = true;
				else shown = true;
			}
			if(conn_view(&conn, input.now_ms) != CONN_VIEW_LIVE) rings[scene->ring.kind == RING_GREY ? RING_GREY : RING_PROGRESS]++;
			else
			{
				rings[scene->ring.kind]++;
				if(dash && !shown) dashes++;
				if(unavailable && scene->ring.kind == RING_NONE) lacking++;
			}
		}
	}

	reached = rings[RING_NONE] >= 500 && rings[RING_YELLOW] >= 500 && rings[RING_RED] >= 500 && rings[RING_GREY] >= 500 && rings[RING_PROGRESS] >= 500 && dashes >= 200 && lacking >= 200 &&
	          back >= 500;
	if(!reached)
	{
		printf("  rings off, yellow, grey, red, progress: %d, %d, %d, %d, %d\n", rings[RING_NONE], rings[RING_YELLOW], rings[RING_GREY], rings[RING_RED], rings[RING_PROGRESS]);
		printf("  live pages of nothing but dashes: %d; with a value the profile lacks and the ring off: %d; at a time before the values: %d\n", dashes, lacking, back);
	}
	check(scenes == 16000 && reached, "16000 made-up value pages reach the ring off, yellow and red in the view LIVE, grey without the API and the progress during a scan, the clock stepping back, "
	      "each at least 500 times, live pages of nothing but dashes and live pages with a value the profile lacks under a ring that is off at least 200 times");
	check(differences == 0, "each of the made-up value pages has the ring that the age of each of its values, the catalogue and the limits give when they are followed a second way, "
	      "and title, note and dots as the tables of the screens say");
}

/* In child processes ----------------------------------------------------------------------------------- */

// Runs a group of checks in a child process. The checks print as always; the parent learns whether one of
// them failed and whether the child came to its end.
static void in_child(void (*group)(void), const char *name, unsigned seconds)
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
		check_all(name);
		fflush(stdout);
		_exit(test_failures == 0 ? 0 : 10);
	}
	if(child < 0 || waitpid(child, &status, 0) != child) status = -1;
	// Its failed checks are printed already
	if(status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 10) test_failures++;
	snprintf(what, sizeof(what), "%s: the checks ran to their end - no crash, no call that never returns", name);
	check(status != -1 && WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 10), what);
}

static void group_values(void)
{
	test_values_screens();
	test_items();
	test_ranges();
	test_notices();
}

static void group_menu(void)
{
	test_menu();
	test_dtc();
	test_busy();
	test_brightness();
	test_web();
	test_settings();
	test_confirm();
}

static void group_lists(void)
{
	test_list_screens();
	test_list_rules();
	test_windows();
	test_clear_dialog();
	test_failed();
}

static void group_overlays(void)
{
	test_overlays();
	test_screens_and_views();
	test_cuts();
}

// scene_dump() builds no scene: nothing to count
static void group_dump(void)
{
	stage();
	build();
	test_dump();
}

int main(void)
{
	test_inputs();
	if(test_failures != 0) return test_end();

	in_child(group_values, "value pages", 60);
	in_child(group_menu, "menu, fault memory start, progress, brightness, web access, settings and their dialogs", 60);
	in_child(group_lists, "lists, clear dialog and failure", 60);
	in_child(group_overlays, "overlays, screens in every view and long texts", 60);
	in_child(group_dump, "dump", 60);
	in_child(test_made_up, "made-up inputs", 120);
	in_child(test_made_up_pages, "made-up value pages", 120);
	return test_end();
}
