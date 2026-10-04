/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __APP_WEB_H__
#define __APP_WEB_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "app.h"
#include "web_route.h"
#include "web_json.h"
#include "ota_check.h"

/*
 * The web interface of the display on top of app.h: what each request does and answers (display/API.md).
 * The HTTP server of the platform reads the request, asks web_route() what it is - with
 * app_web_request() filling in what the display knows - and, if it is not refused there, calls the
 * function for the route. The platform holds the lock of app.h around every call.
 *
 * Every function returns the HTTP status and writes the body: JSON, zero terminated, *length bytes. `out`
 * has to have APP_WEB_OUT_SIZE bytes. An answer that does not fit is 500 {"error":"too_large"}.
 * Refusals have the body of web_error_body(). A `body` that is NULL is an empty one. A function that is
 * called with a route it does not serve answers 404 "not_found" and changes nothing.
 *
 * Time as in app.h: a time before the latest the app has seen counts as that one. Every function that can
 * change something takes its time over into app->clock_ms, also when it refuses; app_web_request(),
 * app_web_get() and the check of a layout change nothing, not even that (the check reads the layout into the
 * room app->checked, which is nobody's between two calls).
 *
 * Changes: a route that changes something calls access_write() first and answers 403 "locked" without it
 * (the release may have ended since web_route() looked). The release is renewed by that call, also when the
 * request is refused afterwards for another reason. A route that needs the knob asks access_may_ask()
 * instead and answers 403 "locked" or 409 "asking" by what it says, else access_ask() and 202 with
 * web_asked_json(). Where several refusals apply, the order is: 403 "locked", then 409 "busy", then 409
 * "asking", then the rest. While a firmware upload runs no question is asked (409 "busy"): the screen
 * shows the upload and takes no input, a question would wait unseen. A new question replaces what an
 * earlier one left behind (the network asked for, ask_detail) at once, also when it has no detail of its own.
 * A request that is refused leaves everything as it was, but for the release and the time named above - and
 * but for a running upload, which app_web_upload_end() ends whatever it answers.
 *
 * The events these functions raise name what is in the app at the moment they are carried out
 * (APP_EVENT_STORE_LAYOUT stores app->layout_text as it is then): the platform takes them and what they
 * name with app_take_events() before it gives the lock back, and stores before the next request is served.
 *
 * The page itself (WEB_ROUTE_PAGE) is an embedded file of the platform, not of this module. The platform
 * never sends an Access-Control-Allow header: the protection by WEB_HEADER_NAME rests on that.
 */

// Room for every answer but one: the layout text has at most 16384 bytes, the values 15062, a report 15001,
// the two fault memory lists 10447, the catalogue 18433 while its texts need no \u00xx. A catalogue whose
// names, units and classes are control characters would need 43777: it is answered 500 "too_large".
#define APP_WEB_OUT_SIZE    20480
#define APP_WEB_UPLOAD_LEFT_S 300   // a firmware upload only begins while the release lasts at least this long

// Fills release_open (access_is_open()), busy (app_busy()) and nothing else: method, path, query, headers,
// length and slot_size are the platform's.
void app_web_request(const app_t *app, web_request_t *request, uint64_t now_ms);

/*
 * The reading routes:
 *   WEB_ROUTE_INFO      web_info_json() from the state of the app and what app_platform() brought: "up" is
 *                       the time in whole seconds, temp_c the last reading that succeeded (0 before the
 *                       first), heat text_heat_word(), ap link_ap_on(), the host app_host(), the id
 *                       poll.bound_id, fw the one of the last state of the adapter (empty without one), view
 *                       text_view_word(), the source "stored", "builtin", "generated" or "preview" (empty for
 *                       a value that is none of app_layout_source_t), settings settings_to_json(). The
 *                       password of the own access point is not part of it.
 *   WEB_ROUTE_CATALOG   catalog_to_json()
 *   WEB_ROUTE_VALUES    web_values_json() with text_view_word() of the connection
 *   WEB_ROUTE_LAYOUT    the text of the layout in use (app->layout_text) as it is. An empty one (app.h: a
 *                       text that could not be written) is no answer: 500 "too_large"
 *   WEB_ROUTE_DTC_LAST  web_dtc_last_json(): the list of the own read if there is one (poll.has_list; its
 *                       age in whole seconds counted from app->poll.flow.list_end_ms, at most 2^32-1), and
 *                       the list before the last clear (poll.has_old)
 *   WEB_ROUTE_WIFI      web_wifi_json() with the stored networks, the network the display is in and the
 *                       networks of the last scan (app_web_seen())
 *   WEB_ROUTE_TICKET    web_ticket_json() for `ticket`
 * Status 200. Any other route: 404 "not_found".
 */
int app_web_get(app_t *app, web_route_t route, uint32_t ticket, char *out, size_t *length, uint64_t now_ms);

// The networks of the last scan, for WEB_ROUTE_WIFI (at most LINK_SEEN_MAX are kept, the first ones). The
// platform calls it, under the lock; it is no request and needs no release. seen NULL or a count below 0:
// none.
void app_web_seen(app_t *app, const web_seen_t *seen, int count);

/*
 * PUT /api/layout and POST /api/layout/reset. body: the layout text (not looked at for the reset).
 *   CHECK   layout_parse(): 200 with web_layout_report_json() if it is taken, 400 with the report if not.
 *           Nothing changes, the release is not needed.
 *   APPLY   as CHECK; a layout that is taken becomes the layout in use, source PREVIEW. It is not stored: a
 *           restart, a reset or a save of something else ends it.
 *   SAVE    as CHECK; a layout that is taken becomes the layout in use and the stored one, source STORED;
 *           APP_EVENT_STORE_LAYOUT.
 *   RESET   the stored layout is forgotten: the choice of app_init() is made anew (built-in or generated,
 *           app_choose_layout()); APP_EVENT_ERASE_LAYOUT. Answers 200 with the report of the layout then in
 *           use, without warnings.
 * APPLY, SAVE and RESET call access_write() before they look at the body. The text of a layout that is taken
 * becomes app->layout_text byte for byte.
 * After a layout changed the value pages start at its first page: this function sets app->nav.page to
 * layout_first_page() of the new layout (nav_tick() only leaves a page that is not shown any more), with
 * every APPLY and SAVE that is taken and every RESET, also when the views are the same as before.
 */
int app_web_layout(app_t *app, web_route_t route, const char *body, size_t body_length, char *out,
                   size_t *length, uint64_t now_ms);

/*
 * POST /api/wifi: 403 "locked"; 409 "busy" while a firmware upload runs (a fault memory request of the
 * display does not keep the question away: it is the knob that decides); 409 "asking"; then web_wifi_parse(),
 * 400 "body" if it refuses. A request without a password for an SSID that is stored keeps the stored
 * password; for an SSID that is not stored it means an open network - judged by what is stored when the knob
 * confirms, not when the browser asks.
 * access_ask(ACCESS_ASK_WIFI): the request is kept until the knob confirms it (app_do(), NAV_DO_ASK_CONFIRM)
 * and dropped when the question ends otherwise. ask_detail is the SSID.
 * POST /api/wifi/forget: web_forget_parse(); 400 "body" if it refuses; 404 "not_found" if no such network
 * is stored; else net_forget(), link_profiles(), app_net(), APP_EVENT_STORE_WIFI, 200 {"ok":true}. No knob:
 * forgetting a network locks nobody out for good - without any network the display opens its own access point.
 * The display leaves the network it is in with every network that is forgotten (link.h), also when another
 * one was named: a fault memory request of the display that is under way ends as lost (poll.h).
 */
int app_web_wifi(app_t *app, web_route_t route, const char *body, size_t body_length, char *out,
                 size_t *length, uint64_t now_ms);

// POST /api/settings: 403 "locked"; settings_from_json(); 400 {"error":"body","member":"<name>"} if it
// refuses (the name is empty if the text as a whole is refused); else the settings are in use at once
// (knob_set_reverse()), APP_EVENT_STORE_SETTINGS - also when the text changed nothing -, 200 with
// settings_to_json().
int app_web_settings(app_t *app, const char *body, size_t body_length, char *out, size_t *length,
                     uint64_t now_ms);

// POST /api/reboot: 403 "locked" without the release; 409 "busy" if app_busy(); else APP_EVENT_REBOOT
// (app_do()) and 200 {"ok":true}. A question that waits does not keep the restart away: it is lost with it.
// POST /api/reset: 403 "locked"; 409 "busy" if app_busy(); 409 "asking" if another question waits; else
// access_ask(ACCESS_ASK_RESET), with an empty ask_detail; 202 with web_asked_json().
int app_web_action(app_t *app, web_route_t route, char *out, size_t *length, uint64_t now_ms);

/*
 * POST /api/ota, in three steps, because the firmware is far larger than any buffer:
 *   begin     with the first bytes of the body (at least OTA_CHECK_BYTES, or all of it if it is shorter;
 *             `first` NULL: no bytes).
 *             403 "locked" without the release (access_write(), as every change), and also if it then ends
 *             in less than APP_WEB_UPLOAD_LEFT_S seconds (access_seconds_left(), which rounds up: 299001 ms
 *             are 300 s. The renewed release lasts ACCESS_OPEN_MS, so this only happens in the last minutes
 *             before its latest end, which no change moves; the question at the end of the upload needs the
 *             release, and nobody should send megabytes to be told so afterwards - the hint then says to give
 *             the release again);
 *             409 "busy" if app_busy(), which a running upload is as well, and while the running firmware
 *             still waits for "Update in Ordnung?" (update_pending: the other slot holds the version the boot
 *             loader goes back to if nobody confirms, and the upload would lie over that question for the
 *             whole of its time);
 *             409 "asking" while a question waits (a firmware question of an earlier upload must not be
 *             confirmed for a slot that is being rewritten);
 *             422 {"error":"<word>"} if ota_check() refuses, with the words "too_short", "no_image",
 *             "wrong_chip", "no_description", "wrong_project", "too_large" - NOTHING has been erased then,
 *             and nothing of the app has changed but the release and the time.
 *             Else 0: the platform may erase the other slot and write (the body is empty, *length 0). The
 *             upload runs from here on (app_busy(), the screen shows it with 0 percent), and the other slot
 *             no longer holds the version before this one: previous_firmware becomes false, so that
 *             "Vorherige Version" can never start an upload nobody confirmed. It stays false however the
 *             upload ends; the platform has to see to it that it is false after a restart as well, until an
 *             installed firmware was started.
 *   progress  bytes written so far, for the percent on the screen (written * 100 / file_size rounded down,
 *             at most 100, 0 for a file_size of 0); it renews the time APP_UPLOAD_IDLE_MS counts from. A
 *             call while no upload runs is ignored: one that app_tick() ended does not come back with bytes
 *             that arrive late.
 *   end       the upload is over (`uploading` false) in every case. ok false (the connection broke, a write
 *             failed, the check sum of the image is wrong): 500 {"error":"upload"}. The same if no upload
 *             runs, whatever ok says: it was ended by app_tick(), or never began, and what lies in the slot
 *             is not asked for. ok true: access_ask(ACCESS_ASK_FIRMWARE) with the version of the image as
 *             ask_detail; 202 with web_asked_json(). 403 "locked" if the release has ended meanwhile (409
 *             "asking" if a question waits, which cannot happen through this interface): nothing is asked
 *             then. The knob then raises APP_EVENT_INSTALL_FIRMWARE; without it the uploaded firmware is
 *             never started.
 *
 * The platform runs one upload at a time, calls end exactly once for every begin that returned 0, and stops
 * writing when it finds `uploading` false before the end (it looks under the lock, with every progress).
 */
int app_web_upload_begin(app_t *app, const uint8_t *first, size_t first_length, uint32_t file_size,
                         uint32_t slot_size, char *out, size_t *length, uint64_t now_ms);
void app_web_upload_progress(app_t *app, uint32_t written, uint32_t file_size, uint64_t now_ms);
int app_web_upload_end(app_t *app, bool ok, char *out, size_t *length, uint64_t now_ms);

#endif
