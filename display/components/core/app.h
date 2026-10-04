/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __APP_H__
#define __APP_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "json.h"
#include "poll.h"
#include "link.h"
#include "nav.h"
#include "knob.h"
#include "hold.h"
#include "access.h"
#include "settings.h"
#include "layout.h"
#include "dtc_view.h"
#include "guard.h"
#include "scene.h"
#include "web_json.h"

/*
 * The display as a whole: every module of this directory wired together, still without hardware. The
 * platform below it (on the device: tasks, WiFi, HTTP, flash; on a PC: sockets and a script) only passes
 * on what happened and carries out what is asked. So the behaviour of the device can be played on a PC
 * against tools/w906/mock_wican.py (display/host).
 *
 * One lock: the platform holds it around every call of this module and around every direct call to
 * app->link and app->poll. Requests to the adapter are sent WITHOUT the lock.
 *
 * Who calls what:
 *   the task that owns the screen   app_button() and app_encoder() every 20 ms, app_tap(), app_swipe(),
 *                                   app_tick() about five times a second, app_scene(), app_backlight()
 *   the task that talks to the      link_next() and the link_* reports on app->link, poll_prepare() and
 *   network                         poll_apply() on app->poll, and app_net() after every report to the link,
 *                                   after every poll_prepare() that handed out a request and after every
 *                                   poll_apply() - before the lock is given back. A request is only sent
 *                                   while app_host() is not empty.
 *   both                            app_take_events()
 *   the web server                  app_web.h
 *
 * Time is milliseconds since the start. Each task reads it before it waits for the lock, so a call may come
 * with a time before that of the call before it: such a time counts as no time passed (clock_ms).
 */

#define APP_UPDATE_CONFIRM_MS   (300u * 1000u)  // an update nobody confirmed is taken back after this
#define APP_UPLOAD_IDLE_MS      (30u * 1000u)   // a firmware upload that brings nothing for this long is over
#define APP_INFO_LINES          14
#define APP_INFO_SIZE           64
#define APP_SWIPE_ROWS          3               // rows a vertical swipe moves the focus of a list

// What the platform has to carry out, as bits taken with app_take_events(). Storing comes before
// everything else, and all of it before the next request to the adapter is sent.
#define APP_EVENT_STORE_SETTINGS    0x0001u     // settings_to_json(&app->settings) -> STORE_KEY_SETTINGS
#define APP_EVENT_STORE_WIFI        0x0002u     // app->profiles, app->profile_count -> STORE_KEY_WIFI
#define APP_EVENT_STORE_BOUND       0x0004u     // app->poll.bound_id -> STORE_KEY_BOUND
#define APP_EVENT_STORE_LAYOUT      0x0008u     // what is stored as layout becomes layout_prev, then
                                                // app->layout_text -> STORE_KEY_LAYOUT; guard_layout_stored()
#define APP_EVENT_ERASE_LAYOUT      0x0010u     // remove STORE_KEY_LAYOUT (the one before it stays)
#define APP_EVENT_STORE_CATALOG     0x0020u     // catalog_to_json(&app->poll.catalog) -> STORE_KEY_CATALOG
#define APP_EVENT_STORE_OLD         0x0040u     // app->poll.old_text -> STORE_KEY_DTC_OLD (the adapter accepted
                                                // a clear of that list, or may have: poll.h)
#define APP_EVENT_MARK_VALID        0x0080u     // the running firmware is good: no rollback any more
#define APP_EVENT_REBOOT            0x0100u
#define APP_EVENT_FACTORY_RESET     0x0200u     // erase STORE_CFG, then restart
#define APP_EVENT_PREVIOUS_FIRMWARE 0x0400u     // boot the other slot, then restart
#define APP_EVENT_INSTALL_FIRMWARE  0x0800u     // boot the uploaded firmware, then restart
// The two events of the layout are raised by app_web.h alone.

typedef enum
{
	APP_LAYOUT_STORED,      // what the user stored
	APP_LAYOUT_BUILTIN,     // the built-in views (W906)
	APP_LAYOUT_GENERATED,   // made from the catalogue: the vehicle is not the one the built-in views are for
	APP_LAYOUT_PREVIEW,     // sent by the browser to look at, not stored
} app_layout_source_t;

// What the platform read at the start. Texts may be NULL: nothing stored. settings_json and bound_id end
// with their zero, the other texts have a length.
typedef struct
{
	const char *version;            // of this firmware; has to stay
	const char *git;                // has to stay
	bool safe_mode;                 // guard_start()
	bool update_pending;            // the firmware runs for the first time after an update
	bool previous_firmware;         // the other slot holds a firmware that can be started
	bool rolled_back;               // the last update was taken back
	const char *settings_json;
	const net_profile_t *profiles;  // as stored; NULL or a count outside 0 to NET_PROFILES_MAX counts as none,
	int profile_count;              // and texts without a terminating zero are cut
	const char *bound_id;
	const char *layout_text;        // the stored layout to use (the one before it if guard_start() says so)
	size_t layout_length;
	const char *builtin_layout;     // display/layouts/w906_default.json; has to stay
	size_t builtin_length;
	const char *catalog_json;
	size_t catalog_length;
	const char *old_text;           // the fault memory list before the last clear
	size_t old_length;
	json_token_t *work;             // LAYOUT_TOKENS tokens; they stay with the app for every text it reads
	int work_count;
} app_boot_t;

// What only the platform knows, for the info page. Texts are copied, as much of each as its field holds, cut
// at a character boundary of UTF-8; a text that is NULL counts as empty.
typedef struct
{
	const char *ssid;               // network the display is in, empty if in none
	const char *ip;
	int rssi;
	const char *ap_ssid;            // own access point
	const char *ap_password;
	const char *slot;               // running app slot
	const char *reset;              // reason of the last reset as a word
	uint32_t heap, heap_min, psram, psram_min;
	uint32_t reconnects;
} app_platform_t;

// app_t holds pointers into itself (link.profiles, info_lines): it is filled in its place by app_init() and
// never copied or moved.
typedef struct
{
	poll_t poll;                    // the conversation with the adapter (values, catalogue, fault memory)
	link_t link;                    // the WiFi side
	nav_t nav;
	knob_t knob;
	hold_t hold;
	access_t access;
	settings_t settings;
	net_profile_t profiles[NET_PROFILES_MAX];
	int profile_count;

	layout_t layout;                // the views in use
	app_layout_source_t source;
	layout_t builtin;               // parsed once at the start
	bool has_builtin;
	const char *builtin_text;       // its text, the one of app_boot_t
	size_t builtin_length;
	layout_t stored;                // what the user stored, to return to from a preview
	bool has_stored;
	layout_t checked;               // room for a layout the browser sent, until it is taken (app_web.h): a
	                                // layout_t is too large for a stack
	char layout_text[LAYOUT_TEXT_MAX + 1];  // the text `layout` was read from; layout_to_json() for a
	                                        // generated one (LAYOUT_GENERATED_TEXT_MAX bytes at most)
	size_t layout_length;
	uint32_t catalog_sum;           // check sum of the catalogue the choice of the layout was made with

	dtc_line_t list[DTC_VIEW_LINES_MAX];    // the lines of poll.list, poll.cleared and poll.old; each count is 0
	int list_lines;                         // while the poll does not hold its result
	dtc_line_t cleared[DTC_VIEW_LINES_MAX];
	int cleared_lines;
	dtc_line_t old[DTC_VIEW_LINES_MAX];
	int old_lines;
	dtc_summary_t summary;          // of poll.list

	const char *version, *git;
	bool safe_mode;
	bool update_pending;
	uint64_t update_until_ms;
	bool previous_firmware;
	bool rolled_back;
	bool uploading;                 // a firmware upload runs (app_web.h)
	int upload_percent;
	uint64_t upload_ms;             // when it began or last brought something
	char upload_version[SCENE_SHORT_SIZE];  // the version of the firmware being uploaded, for the question at
	                                        // the end of the upload (app_web.h)
	char ask_detail[SCENE_SHORT_SIZE];      // what the browser asked for: the SSID, the firmware version
	web_wifi_request_t wifi_asked;  // the network the browser wants stored, while its question waits
	bool has_wifi_asked;
	web_seen_t seen[LINK_SEEN_MAX]; // the networks of the last scan, for the browser
	int seen_count;
	guard_heat_t heat;
	int temp_c;                     // the last reading that succeeded
	bool has_temp;                  // the latest reading succeeded
	int brightness_preview;         // while the brightness screen is shown: the value being set, else -1
	uint64_t last_input_ms;
	bool woke;                      // the press that is going on began on a dark screen: it only woke it
	uint64_t clock_ms;              // the latest time seen: it follows the calls but never runs backwards
	uint32_t events;
	json_token_t *work;
	int work_count;

	char ssid[NET_SSID_SIZE], ip[40], ap_ssid[NET_SSID_SIZE], ap_password[NET_PASSWORD_SIZE];
	char slot[16], reset[24];
	int rssi;
	uint32_t heap, heap_min, psram, psram_min, reconnects;
	char info[APP_INFO_LINES][APP_INFO_SIZE];
	const char *info_lines[APP_INFO_LINES];
	int info_count;                 // lines of the info page in use
} app_t;

/*
 * The start. app_t is large (262232 bytes on a 64 bit host: the three layouts and the room for a fourth
 * 140480, the lines of the three fault memory lists 62376, the poll 39472, the layout text 16385; 262136
 * bytes calculated for a 32 bit target that aligns 64 bit numbers to 8 bytes, not measured on the device):
 * static storage or the external RAM, never a stack.
 * - settings: settings_defaults(), then the stored text if settings_from_json() takes it
 * - profiles, bound id, catalogue and old list as stored (poll_init(), poll_stored()); link_init() with the
 *   profiles and safe_mode; knob with settings.reverse; hold, access and nav in their start states
 * - the built-in layout is parsed (has_builtin false if that fails or there is none: then the views are
 *   made from the catalogue, layout_from_catalog(), whenever the built-in ones would be used)
 * - the layout in use: the stored text if there is one, safe_mode is false and layout_parse() takes it
 *   (source STORED, has_stored); else the choice below. In safe mode the stored text is not even read: it
 *   may be what crashed the display.
 * - update_pending: the question "Update in Ordnung?" is shown; without an answer for
 *   APP_UPDATE_CONFIRM_MS the display restarts (APP_EVENT_REBOOT), and the boot loader takes the update back
 * - the lines of the stored old list and the info lines are there from the start; no event is raised
 *
 * The choice of the layout while the source is not STORED and not PREVIEW, made at the start and whenever
 * the check sum of the catalogue changed (app_net()): the built-in layout if layout_suits() it, else
 * layout_from_catalog(). layout_text follows the layout in use. When the choice changes the source the value
 * pages start at the first page of the new layout; when it does not, the page shown stays (nav_tick() finds
 * the nearest if it is not shown any more).
 */
void app_init(app_t *app, const app_boot_t *boot, uint64_t now_ms);

// The choice above, made now, whatever the source is (app_web.h: the stored layout was forgotten)
void app_choose_layout(app_t *app);

/*
 * A dark screen. When the backlight is off by the standby rule (app_backlight() gives 0 although the heat
 * allows light), the first input - a press, a detent, a tap, a swipe - only wakes the screen: it restarts
 * the idle time and is not passed on to nav. Nobody acts on a screen he cannot see. A question of the
 * browser, an upload and an unconfirmed update count as something to show: the screen lights up for them.
 * A press is the reading with which the switch begins to count as pressed (knob.h); what the knob reports
 * of a press that woke the screen, short or long, is dropped. The idle time restarts with every press,
 * detent, tap and swipe, on a dark screen and on a lit one.
 * While the heat has switched the backlight off, nothing can wake it, and every input is passed on.
 */

// One reading of the switch of the knob (board, every 20 ms). Feeds knob_sample() and hold_sample() (with
// on_action true while the clear dialog shows and its focus is on "Löschen" and no overlay lies over it).
// KNOB_SHORT and KNOB_LONG go to nav_short() / nav_long(), also while the clear dialog shows: nav.h does
// nothing with them there but for a short press on "Abbrechen" (hold.h reads the switch), and under an
// overlay they are the answer to what lies over the dialog. Then what hold_sample() reported goes to
// nav_hold(). Whatever nav returns is carried out (see app_do()).
void app_button(app_t *app, bool pressed, bool read_ok, uint64_t now_ms);

// New counts of the encoder (board). knob_turn() makes detents of them; those go to nav_turn(), and to
// hold_activity() while the clear dialog shows. Counts that make no detent are no input.
void app_encoder(app_t *app, int counts, uint64_t now_ms);

// A tap on row `row` of the screen (the drawing code knows where the rows are): nav_tap(). In the clear
// dialog also hold_activity().
void app_tap(app_t *app, int row, uint64_t now_ms);

// A swipe: dx is negative (finger went left: next page), positive (previous page) or 0; dy is negative
// (finger went up: the focus of a list moves APP_SWIPE_ROWS rows down), positive (up) or 0. Vertical (dx is
// 0, dy is not) on a screen with rows: nav_turn(). Every other swipe: nav_swipe() with the direction of dx,
// which nav ignores unless a value page shows. In the clear dialog also hold_activity().
void app_swipe(app_t *app, int dx, int dy, uint64_t now_ms);

// About five times a second: nav_tick(), the time of an unconfirmed update (APP_EVENT_REBOOT with every tick
// from APP_UPDATE_CONFIRM_MS after the start on, until it is confirmed), the info lines, and the end of a
// firmware upload that has brought nothing for APP_UPLOAD_IDLE_MS (as app_web_upload_end() with ok false:
// `uploading` becomes false - an upload that stalls must not lock the display, which takes no input while it
// runs). What the browser asked for (wifi_asked, ask_detail) is dropped when no question waits any more.
void app_tick(app_t *app, uint64_t now_ms);

/*
 * The info lines (NAV_INFO), made by app_init() and app_tick(), in this order. A text that is empty is
 * written as SCENE_DASH; a line that does not fit APP_INFO_SIZE is cut at a character boundary of UTF-8:
 * it is the beginning of the whole line, nothing of what comes behind the cut is written.
 *   "Update nicht übernommen – vorherige Version aktiv"    only if rolled_back
 *   "WLAN: Werkstatt (-61 dBm)"                 ssid and rssi; "WLAN: –" in no network
 *   "Adresse: 192.168.1.77"                     ip
 *   "WiCAN: 192.168.1.50"                       app_host()
 *   "WiCAN-ID: a1b2c3d4e5f6"                    poll.bound_id
 *   "WiCAN-Firmware: 4.21"                      fw of the last state of the adapter (conn_state())
 *   "Version: 0.1.0 (ota_0)"                    version and slot
 *   "Ansichten: W906 OM651 Standard (eingebaut)"    name and source of the layout in use: "gespeichert",
 *                                               "eingebaut", "erzeugt", "Vorschau"
 *   "Speicher: 182340 frei, min. 151200"        heap, heap_min
 *   "PSRAM: 7340032 frei, min. 7100416"         psram, psram_min
 *   "HTTP: 12345 ok, 7 Fehler"                  poll.http_ok, poll.http_failed
 *   "Neuverbindungen: 2"                        reconnects
 *   "Temperatur: 47 °C"                         temp_c; "Temperatur: –" while has_temp is false
 *   "Letzter Neustart: poweron"                 reset
 */

// A reading of the chip temperature (guard_heat()). temp_c is the last reading that succeeded, has_temp
// tells whether the latest did.
void app_temperature(app_t *app, int celsius, bool valid);

// What the platform knows about itself, about once a second
void app_platform(app_t *app, const app_platform_t *platform);

/*
 * Called by the network task after every report to app->link, after every poll_prepare() that handed out a
 * request and after every poll_apply():
 * - poll_wifi() follows link_up(): up when the link is up, down when it is not
 * - link_answering() with the view of the connection
 * - the events of the poll become events of the app (BOUND, CATALOG, OLD); on POLL_EVENT_LISTS the lines
 *   and the summary are rebuilt (dtc_view_list(), dtc_view_cleared() with the old list as the one before,
 *   dtc_view_list() of the old list)
 * - the layout is chosen anew if the check sum of the catalogue changed (see app_init())
 * Whoever changes the stored networks (link_profiles()) calls it as well: the poll must not hand out a
 * request for an adapter whose address was just forgotten.
 */
void app_net(app_t *app, uint64_t now_ms);

// Address or name of the adapter for the requests (link_host()). The text lies in the app and changes with
// it: the platform copies it before it gives the lock back.
const char *app_host(const app_t *app);

/*
 * What nav asks for, carried out here (also used by app_web.h). Where it touches the poll (read, clear,
 * dismiss), the events of the poll are taken at once, as app_net() does: the lines on the screen must not
 * wait for the network task. NAV_DO_NOTHING and what is no member of the enum: nothing.
 *   NAV_DO_READ               poll_read()
 *   NAV_DO_HOLD_OPEN          hold_open(); if it refuses (the switch hangs) the dialog is left again
 *                             (nav_hold() with HOLD_STUCK)
 *   NAV_DO_HOLD_CLOSE         hold_close()
 *   NAV_DO_CLEAR              poll_clear() with hold_is_stuck()
 *   NAV_DO_DISMISS            poll_dismiss()
 *   NAV_DO_BRIGHTNESS         brightness_preview = nav.value, kept within SETTINGS_BRIGHTNESS_MIN and
 *                             SETTINGS_BRIGHTNESS_MAX
 *   NAV_DO_SETTINGS_STORE     nav.value, kept within the same limits, becomes settings.night in night mode,
 *                             else settings.brightness; brightness_preview -1; APP_EVENT_STORE_SETTINGS
 *   NAV_DO_NIGHT_TOGGLE       settings.night_mode; APP_EVENT_STORE_SETTINGS
 *   NAV_DO_REVERSE_TOGGLE     settings.reverse, knob_set_reverse(); APP_EVENT_STORE_SETTINGS
 *   NAV_DO_AP_TOGGLE          link_ap_request() with the opposite of link_ap_on()
 *   NAV_DO_RELEASE_ON / OFF   access_open() / access_close(). Taking the release back refuses a question
 *                             that waits: what the browser asked for is dropped with it. Giving it again
 *                             leaves a question that waits, and what it asks for, as they are.
 *   NAV_DO_ASK_CONFIRM        access_confirm(): WIFI -> the network asked for is stored (net_store(); without
 *                             a password in the request, with the password stored for that SSID, or as an
 *                             open network if there is none), link_profiles(), app_net(),
 *                             APP_EVENT_STORE_WIFI - nothing of it if no network was asked for or net_store()
 *                             refuses it; FIRMWARE -> APP_EVENT_INSTALL_FIRMWARE;
 *                             RESET -> APP_EVENT_FACTORY_RESET; ACCESS_ASK_NONE (the press came in the first
 *                             ACCESS_ASK_SHOWN_MS, or the question is over) -> nothing is carried out and
 *                             nothing is dropped: a question that still waits keeps what it asks for. With
 *                             a question that is confirmed what the browser asked for is dropped.
 *   NAV_DO_ASK_REFUSE         access_refuse(); what the browser asked for is dropped
 *   NAV_DO_UPDATE_OK          update_pending false; APP_EVENT_MARK_VALID
 *   NAV_DO_REBOOT             APP_EVENT_REBOOT
 *   NAV_DO_PREVIOUS_FIRMWARE  APP_EVENT_PREVIOUS_FIRMWARE, but only while previous_firmware: the dialog of
 *                             nav may have been opened before an upload overwrote the other slot
 *   NAV_DO_FACTORY_RESET      APP_EVENT_FACTORY_RESET
 */
void app_do(app_t *app, nav_do_t what, uint64_t now_ms);

// The world as nav sees it right now (nav.h), filled from the state of the app: every byte of it is written
void app_world(const app_t *app, nav_world_t *world, uint64_t now_ms);

// What is on the screen (scene_build() with everything of the app). The lines of a list, of an outcome and
// of the old list are passed only while the poll holds their result, the summary only with the list. The
// address is "http://" and the ip of app_platform(), none without an ip. update_left_s is rounded up.
void app_scene(const app_t *app, scene_t *scene, uint64_t now_ms);

// The brightness of the backlight, 0 to 100: settings_backlight() - with the value being set while the
// brightness screen shows - limited by guard_brightness().
// "showing" for the standby rule: the view of the connection is LIVE or SCAN, or it is NO_API and at least
// one value is not gone (a firmware without the API tells nothing about the ignition; its values are the sign
// of a vehicle that runs), or a screen other than the value pages is shown, or an overlay lies over it.
int app_backlight(const app_t *app, uint64_t now_ms);

// true while the web interface must not restart the display or replace its firmware: a fault memory request
// of the display is under way (the flow is READ_SENT, READING, CLEAR_SENT or CLEARING), the clear dialog
// shows, or a firmware upload runs
bool app_busy(const app_t *app);

// The events raised since the last call; they are cleared by it
uint32_t app_take_events(app_t *app);

#endif
