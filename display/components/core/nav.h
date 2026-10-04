/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __NAV_H__
#define __NAV_H__

#include <stdint.h>
#include <stdbool.h>
#include "layout.h"
#include "catalog.h"
#include "dtc_flow.h"
#include "access.h"
#include "hold.h"
#include "settings.h"

/*
 * Which screen the display shows and what the knob does there. Pure logic: the user interface draws what
 * this struct says, passes every input on and carries out what comes back.
 *
 * The grammar of the knob, the same everywhere, usable without looking:
 *   turn         on the value pages: one detent = one page, hard ends, no wrap
 *                everywhere else: moves the focus over the rows of the screen, hard ends
 *   short press  on the value pages: opens the menu; everywhere else: acts on the focused row
 *   long press   one level back; on a value page: back to the first page
 * The value pages are passive: no page in the rotation of the knob has an action.
 * Touch is an addition: a swipe on a value page is a turn of one detent, a tap on a row is focus plus
 * short press. Everything works with the knob alone.
 *
 * Screens and their rows (the focus starts on the row marked *):
 *   NAV_PAGES        the value pages; `page` is the one shown, -1 if the layout has none to show
 *   NAV_MENU         *0 Fehlerspeicher, 1 Helligkeit, 2 Nachtmodus, 3 Web-Zugriff, 4 Info, 5 Einstellungen,
 *                    6 Zurück
 *   NAV_DTC          *0 Lesen, 1 Liste ansehen, 2 Zuletzt gelöscht, 3 Zurück
 *   NAV_DTC_BUSY     no rows: progress of the own request
 *   NAV_DTC_LIST     *0 .. lines-1 the lines of the list, then Erneut lesen, Fehler löschen, Zurück
 *   NAV_DTC_CONFIRM  *0 Abbrechen, 1 Löschen
 *   NAV_DTC_CLEARED  *0 .. lines-1 the lines of the outcome, then Fertig
 *   NAV_DTC_FAILED   no rows: the reason; a short press acknowledges
 *   NAV_DTC_OLD      *0 .. lines-1 the lines of the list before the last clear, then Zurück
 *   NAV_BRIGHTNESS   no rows: turning changes the brightness
 *   NAV_WEB          *0 Freigabe an/aus, 1 Zurück
 *   NAV_INFO         *0 .. lines-1 lines to scroll, no action
 *   NAV_SETTINGS     *0 Drehrichtung, 1 Hotspot, 2 Neustart, 3 Vorherige Version, 4 Werkseinstellungen,
 *                    5 Zurück
 *   NAV_CONFIRM      *0 Abbrechen, 1 Ausführen - for Neustart, Vorherige Version, Werkseinstellungen
 *
 * Something can lie over every screen and takes every input (nav_overlay):
 *   NAV_OVER_UPLOAD  a firmware upload runs: every input is ignored
 *   NAV_OVER_ASK     the browser asks for something that needs the knob (access.h): a short press or a tap
 *                    confirms, a long press refuses, turning and swiping do nothing
 *   NAV_OVER_UPDATE  the firmware started for the first time after an update and asks "Update in
 *                    Ordnung?": a short press or a tap says yes; everything else is ignored
 * In this order if several apply.
 */

#define NAV_IDLE_MS             120000u // without input every screen but the value pages returns to them
#define NAV_BRIGHTNESS_STEP     5       // percent per detent

#define NAV_MENU_ROWS           7
#define NAV_DTC_ROWS            4
#define NAV_WEB_ROWS            2
#define NAV_SETTINGS_ROWS       6

typedef enum
{
	NAV_PAGES,
	NAV_MENU,
	NAV_DTC,
	NAV_DTC_BUSY,
	NAV_DTC_LIST,
	NAV_DTC_CONFIRM,
	NAV_DTC_CLEARED,
	NAV_DTC_FAILED,
	NAV_DTC_OLD,
	NAV_BRIGHTNESS,
	NAV_WEB,
	NAV_INFO,
	NAV_SETTINGS,
	NAV_CONFIRM,
} nav_screen_t;

typedef enum
{
	NAV_OVER_NONE,
	NAV_OVER_UPLOAD,
	NAV_OVER_ASK,
	NAV_OVER_UPDATE,
} nav_overlay_t;

// What the caller has to carry out. One per call.
typedef enum
{
	NAV_DO_NOTHING,
	NAV_DO_READ,                // dtc_flow_read()
	NAV_DO_HOLD_OPEN,           // the clear dialog opened: hold_open()
	NAV_DO_HOLD_CLOSE,          // the clear dialog was left: hold_close()
	NAV_DO_CLEAR,               // the hold was confirmed: dtc_flow_clear()
	NAV_DO_DISMISS,             // dtc_flow_dismiss()
	NAV_DO_BRIGHTNESS,          // set the brightness to nav_t.value at once, without storing
	NAV_DO_SETTINGS_STORE,      // the brightness screen was left: store nav_t.value
	NAV_DO_NIGHT_TOGGLE,
	NAV_DO_REVERSE_TOGGLE,
	NAV_DO_AP_TOGGLE,           // own access point on / off
	NAV_DO_RELEASE_ON,          // access_open()
	NAV_DO_RELEASE_OFF,         // access_close()
	NAV_DO_ASK_CONFIRM,         // access_confirm() and carry out what it returns
	NAV_DO_ASK_REFUSE,          // access_refuse()
	NAV_DO_UPDATE_OK,           // mark the running firmware as good
	NAV_DO_REBOOT,
	NAV_DO_PREVIOUS_FIRMWARE,   // start the firmware in the other slot
	NAV_DO_FACTORY_RESET,
} nav_do_t;

// What the navigation needs to know about the rest of the display. Filled anew for every call.
// A number of lines below 0 counts as 0, one above INT_MAX - 3 as INT_MAX - 3: the rows behind the lines
// keep a number.
typedef struct
{
	const layout_t *layout;         // the views in use
	const catalog_t *catalog;
	dtc_flow_phase_t flow;          // phase of the own fault memory request; a value that is no member of
	                                // the enum counts as DTC_FLOW_IDLE
	bool can_read;                  // dtc_flow_read_block() is DTC_FLOW_ALLOWED
	bool can_clear;                 // dtc_flow_clear_block() is DTC_FLOW_ALLOWED
	int list_lines;                 // lines of the list of the own read (dtc_view_list), 0 if there is none
	int cleared_lines;              // lines of the outcome of the own clear (dtc_view_cleared)
	int old_lines;                  // lines of the list before the last clear, 0 if none is stored
	int info_lines;
	access_ask_t asking;            // access_asking(); every value but ACCESS_ASK_NONE is a question
	bool release_open;              // access_is_open()
	bool update_pending;            // the running firmware waits for "Update in Ordnung?"
	bool uploading;                 // a firmware upload runs
	bool previous_firmware;         // the other slot holds a firmware that can be started
	bool night_mode;
	int brightness;                 // the one in use: settings.night in night mode, else settings.brightness
} nav_world_t;

typedef struct
{
	nav_screen_t screen;
	int page;                   // value page shown, -1 if there is none
	int row;                    // focused row of the screen, 0 on screens without rows
	int value;                  // NAV_BRIGHTNESS: the brightness being set
	nav_do_t confirm;           // NAV_CONFIRM: what "Ausführen" does (REBOOT, PREVIOUS_FIRMWARE, FACTORY_RESET);
	                            // NAV_DO_NOTHING on every other screen
	uint64_t last_input_ms;
	uint64_t clock_ms;          // the latest time seen: it follows the calls but never runs backwards
} nav_t;

// The value pages, on the first page the layout shows (-1 if none); row and value 0. Counts as an input for
// the idle time.
void nav_init(nav_t *nav, const nav_world_t *world, uint64_t now_ms);

// What lies over the screen, see above
nav_overlay_t nav_overlay(const nav_world_t *world);

// Rows of the screen shown: the numbers of the table above, with the lines of the world for the lists
int nav_rows(const nav_t *nav, const nav_world_t *world);

/*
 * The inputs. Each returns what the caller has to carry out and counts as input for the idle time, also
 * when it is ignored. Under an overlay the screen below does not change.
 *
 * NAV_PAGES
 *   turn n        n times layout_step_page() in that direction; if `page` is -1 or not shown any more,
 *                 the nearest page the layout shows instead, however many detents and in whatever direction:
 *                 the shown page with the smallest distance to `page`, of two equally near ones the one
 *                 behind it, -1 if none is shown
 *   short         -> NAV_MENU
 *   long          the first page the layout shows
 * NAV_MENU
 *   short on      0 -> the fault memory: NAV_DTC_BUSY if the own request is under way (READ_SENT, READING,
 *                 CLEAR_SENT, CLEARING), else NAV_DTC;  1 -> NAV_BRIGHTNESS with value = world brightness,
 *                 kept within the limits named below;
 *                 2 -> NAV_DO_NIGHT_TOGGLE;  3 -> NAV_WEB;  4 -> NAV_INFO;  5 -> NAV_SETTINGS;  6 -> NAV_PAGES
 *   long          -> NAV_PAGES
 * NAV_DTC
 *   short on      0: if can_read -> NAV_DO_READ and NAV_DTC_BUSY, else nothing
 *                 1: by the phase of the flow: LIST -> NAV_DTC_LIST, CLEARED -> NAV_DTC_CLEARED,
 *                    FAILED or UNKNOWN -> NAV_DTC_FAILED, else nothing
 *                 2: if old_lines > 0 -> NAV_DTC_OLD, else nothing
 *                 3 -> NAV_MENU (focus on row 0)
 *   long          -> NAV_MENU (focus on row 0)
 * NAV_DTC_BUSY
 *   short, turn   nothing (a scan cannot be cancelled)
 *   long          -> NAV_PAGES; the request goes on, the ring shows its progress
 * NAV_DTC_LIST    rows: list_lines, then Erneut lesen, Fehler löschen, Zurück
 *   short on      a line: nothing;  Erneut lesen: as "Lesen" above;
 *                 Fehler löschen: if can_clear -> NAV_DTC_CONFIRM and NAV_DO_HOLD_OPEN, else nothing;
 *                 Zurück -> NAV_DTC
 *   long          -> NAV_DTC
 * NAV_DTC_CONFIRM the knob switch belongs to hold.h here: the caller samples it and reports the outcome
 *                 with nav_hold(). Long presses are not a way back on this screen. While an overlay lies
 *                 over it, the caller passes on_action false to hold_sample().
 *   turn          moves the focus between 0 Abbrechen and 1 Löschen (the caller also calls hold_activity())
 *   short on      0 -> NAV_DTC_LIST (focus on Fehler löschen) and NAV_DO_HOLD_CLOSE;  1: nothing
 *   long          nothing
 *   tap on        0 as short; 1: nothing (clearing cannot be confirmed by touch)
 * NAV_DTC_CLEARED rows: cleared_lines, then Fertig
 *   short on      a line: nothing;  Fertig -> NAV_DO_DISMISS and NAV_DTC
 *   long          -> NAV_DTC (the outcome stays to be looked at again)
 * NAV_DTC_FAILED
 *   short         -> NAV_DO_DISMISS and NAV_DTC
 *   long          -> NAV_DTC
 * NAV_DTC_OLD     rows: old_lines, then Zurück
 *   short on      a line: nothing;  Zurück -> NAV_DTC
 *   long          -> NAV_DTC
 * NAV_BRIGHTNESS
 *   turn n        value += n * NAV_BRIGHTNESS_STEP, kept within SETTINGS_BRIGHTNESS_MIN and
 *                 SETTINGS_BRIGHTNESS_MAX -> NAV_DO_BRIGHTNESS (also when the value did not change)
 *   short, long   -> NAV_MENU (focus on row 1) and NAV_DO_SETTINGS_STORE
 * NAV_WEB
 *   short on      0 -> NAV_DO_RELEASE_OFF if release_open, else NAV_DO_RELEASE_ON;  1 -> NAV_MENU (row 3)
 *   long          -> NAV_MENU (row 3)
 * NAV_INFO
 *   short, long   -> NAV_MENU (row 4)
 * NAV_SETTINGS
 *   short on      0 -> NAV_DO_REVERSE_TOGGLE;  1 -> NAV_DO_AP_TOGGLE;
 *                 2 -> NAV_CONFIRM for NAV_DO_REBOOT;
 *                 3: if previous_firmware -> NAV_CONFIRM for NAV_DO_PREVIOUS_FIRMWARE, else nothing;
 *                 4 -> NAV_CONFIRM for NAV_DO_FACTORY_RESET;  5 -> NAV_MENU (row 5)
 *   long          -> NAV_MENU (row 5)
 * NAV_CONFIRM
 *   short on      0 -> NAV_SETTINGS (focus on the row it came from);
 *                 1 -> the action in `confirm`, and NAV_PAGES
 *   long          -> NAV_SETTINGS (focus on the row it came from)
 *
 * Turning on a screen with rows moves the focus by n and keeps it within 0 and rows - 1. Where a screen is
 * entered and no focus is named above, the focus is on row 0. A short press while the focus lies beyond the
 * last row (a list became shorter and nav_tick() has not run yet) does nothing.
 */
nav_do_t nav_turn(nav_t *nav, int detents, const nav_world_t *world, uint64_t now_ms);
nav_do_t nav_short(nav_t *nav, const nav_world_t *world, uint64_t now_ms);
nav_do_t nav_long(nav_t *nav, const nav_world_t *world, uint64_t now_ms);

// A tap on row `row` of the screen: the focus goes there, then as a short press. A row that does not
// exist is ignored. On NAV_PAGES and on screens without rows a tap is ignored (row is not looked at),
// except NAV_DTC_FAILED, where it counts as a short press.
nav_do_t nav_tap(nav_t *nav, int row, const nav_world_t *world, uint64_t now_ms);

// A swipe: on NAV_PAGES a turn of one detent in that direction (positive = next page; 0 is ignored), on
// every other screen ignored
nav_do_t nav_swipe(nav_t *nav, int direction, const nav_world_t *world, uint64_t now_ms);

// What hold_sample() reported while NAV_DTC_CONFIRM is shown: HOLD_CONFIRMED -> NAV_DO_CLEAR and
// NAV_DTC_BUSY; HOLD_CANCELLED or HOLD_STUCK -> NAV_DTC_LIST (focus on Fehler löschen), nothing to carry
// out (the dialog of hold.h is closed already). Everything else, and every event on another screen:
// nothing. Does not count as input. Under an overlay HOLD_CONFIRMED counts as HOLD_CANCELLED: what the user
// could not see is not confirmed.
nav_do_t nav_hold(nav_t *nav, hold_event_t event, const nav_world_t *world, uint64_t now_ms);

/*
 * Called about five times a second. Follows what happened without the user, in this order, the first that
 * applies:
 *   - NAV_DTC_CONFIRM and the list is gone or may not be cleared any more (flow not LIST, or not
 *     can_clear) -> NAV_DTC_LIST if the flow is still LIST, else NAV_DTC; NAV_DO_HOLD_CLOSE
 *   - NAV_DTC_BUSY and the request ended: LIST -> NAV_DTC_LIST, CLEARED -> NAV_DTC_CLEARED, FAILED or
 *     UNKNOWN -> NAV_DTC_FAILED, IDLE -> NAV_DTC
 *   - NAV_DTC_LIST and the flow is not LIST -> NAV_DTC;  NAV_DTC_CLEARED and the flow is not CLEARED ->
 *     NAV_DTC;  NAV_DTC_FAILED and the flow is neither FAILED nor UNKNOWN -> NAV_DTC;
 *     NAV_DTC_OLD and old_lines is 0 -> NAV_DTC
 *   - NAV_PAGES: if `page` is -1 or not shown any more, the nearest page the layout shows (a layout was
 *     stored, or the catalogue arrived)
 *   - the focus lies beyond the last row (a list became shorter): it goes to the last row
 *   - no input for NAV_IDLE_MS on a screen other than NAV_PAGES, NAV_DTC_BUSY and NAV_DTC_CONFIRM, and no
 *     overlay: -> NAV_PAGES; from NAV_BRIGHTNESS with NAV_DO_SETTINGS_STORE. The own request and its outcome
 *     are kept.
 * Only the last rule waits for an overlay to go, the others follow what happened below it as well. A screen
 * entered here has the focus on row 0, NAV_DTC_LIST entered from NAV_DTC_CONFIRM on Fehler löschen.
 * A time before the one of an earlier call counts as no time passed.
 */
nav_do_t nav_tick(nav_t *nav, const nav_world_t *world, uint64_t now_ms);

#endif
