/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>
#include "app_web.h"
#include "texts.h"

#define OK_BODY     "{\"ok\":true}"

// The time of the app at now_ms: the web server reads its clock by itself, and a time that lies before the
// latest the app has seen lets no time pass (app.h)
static uint64_t time_at(const app_t *app, uint64_t now_ms)
{
	return now_ms > app->clock_ms ? now_ms : app->clock_ms;
}

static uint64_t advance(app_t *app, uint64_t now_ms)
{
	app->clock_ms = time_at(app, now_ms);
	return app->clock_ms;
}

static uint64_t passed(uint64_t now_ms, uint64_t since_ms)
{
	return now_ms > since_ms ? now_ms - since_ms : 0;
}

// Whole seconds, as many as the 32 bit of an answer hold
static uint32_t seconds(uint64_t ms)
{
	uint64_t whole = ms / 1000;

	return whole > UINT32_MAX ? UINT32_MAX : (uint32_t)whole;
}

// The answer whose body a writer left in `out`. `written` is what the writer returned: the length, or -1
// for a text that has no room.
static int answer(int status, int written, char *out, size_t *length)
{
	if(written < 0)
	{
		status = 500;
		written = web_error_body("too_large", out, APP_WEB_OUT_SIZE);
	}
	*length = (size_t)written;
	return status;
}

static int refuse(int status, const char *error, char *out, size_t *length)
{
	return answer(status, web_error_body(error, out, APP_WEB_OUT_SIZE), out, length);
}

// The answer with a text that is there already. An empty one is a text that could not be written.
static int answer_text(const char *text, size_t text_length, char *out, size_t *length)
{
	if(text_length == 0 || text_length >= APP_WEB_OUT_SIZE) return answer(200, -1, out, length);

	memcpy(out, text, text_length);
	out[text_length] = '\0';
	*length = text_length;
	return 200;
}

// A request may come without a body: that is an empty one
static const char *body_of(const char *body, size_t *body_length)
{
	if(body != NULL) return body;

	*body_length = 0;
	return "";
}

// The network of a question that is over must not be taken for the one of the next, and its password is
// not kept
static void drop_network(app_t *app)
{
	memset(&app->wifi_asked, 0, sizeof(app->wifi_asked));
	app->has_wifi_asked = false;
}

// Why the question cannot be asked now: the status of the refusal, with its body written, or 0 if it can.
// busy: the display cannot show a question, or must not be restarted by its answer.
static int ask_refused(const app_t *app, access_ask_t question, bool busy, char *out, size_t *length, uint64_t now)
{
	access_refusal_t refusal = access_may_ask(&app->access, question, now);

	// Who may not change anything learns nothing else
	if(refusal == ACCESS_CLOSED) return refuse(403, "locked", out, length);
	if(busy) return refuse(409, "busy", out, length);
	if(refusal != ACCESS_ALLOWED) return refuse(409, "asking", out, length);
	return 0;
}

// The question to the knob, which ask_refused() allowed at this time. `detail` is shown with it.
static int ask(app_t *app, access_ask_t question, const char *detail, char *out, size_t *length, uint64_t now)
{
	snprintf(app->ask_detail, sizeof(app->ask_detail), "%s", detail);
	return answer(202, web_asked_json(access_ask(&app->access, question, now), out, APP_WEB_OUT_SIZE), out, length);
}

void app_web_request(const app_t *app, web_request_t *request, uint64_t now_ms)
{
	request->release_open = access_is_open(&app->access, time_at(app, now_ms));
	request->busy = app_busy(app);
}

static int info_json(const app_t *app, char *out, uint64_t now)
{
	// In the order of app_layout_source_t
	static const char *const sources[] = {"stored", "builtin", "generated", "preview"};
	const wican_state_t *state = conn_state(&app->poll.conn);
	char settings[SETTINGS_JSON_SIZE];
	web_info_t info;

	memset(&info, 0, sizeof(info));
	// A text that could not be written is an empty one, and that becomes null
	settings_to_json(&app->settings, settings, sizeof(settings));

	info.version = app->version;
	info.git = app->git;
	info.slot = app->slot;
	info.reset = app->reset;
	info.up_s = seconds(now);
	info.safe_mode = app->safe_mode;
	info.rolled_back = app->rolled_back;
	info.update_pending = app->update_pending;
	info.heap = app->heap;
	info.heap_min = app->heap_min;
	info.psram = app->psram;
	info.psram_min = app->psram_min;
	info.temp_c = app->temp_c;
	info.heat = text_heat_word(app->heat);
	info.release_open = access_is_open(&app->access, now);
	info.release_left_s = access_seconds_left(&app->access, now);
	info.ssid = app->ssid;
	info.ip = app->ip;
	info.rssi = app->rssi;
	info.ap_on = link_ap_on(&app->link);
	info.ap_ssid = app->ap_ssid;
	info.wican_host = app_host(app);
	info.wican_id = app->poll.bound_id;
	if(state != NULL) info.wican_fw = state->fw;
	info.view = text_view_word(conn_view(&app->poll.conn, now));
	info.layout_name = app->layout.name;
	// What is no source has no word
	if((unsigned)app->source < sizeof(sources) / sizeof(sources[0])) info.layout_source = sources[app->source];
	info.http_ok = app->poll.http_ok;
	info.http_failed = app->poll.http_failed;
	info.reconnects = app->reconnects;
	info.settings = settings;
	return web_info_json(&info, out, APP_WEB_OUT_SIZE);
}

int app_web_get(app_t *app, web_route_t route, uint32_t ticket, char *out, size_t *length, uint64_t now_ms)
{
	uint64_t now = time_at(app, now_ms);
	const poll_t *poll = &app->poll;
	int written;

	switch(route)
	{
		case WEB_ROUTE_INFO:
			written = info_json(app, out, now);
			break;

		case WEB_ROUTE_CATALOG:
			written = catalog_to_json(&poll->catalog, out, APP_WEB_OUT_SIZE);
			break;

		case WEB_ROUTE_VALUES:
			written = web_values_json(&poll->values, text_view_word(conn_view(&poll->conn, now)), now, out, APP_WEB_OUT_SIZE);
			break;

		case WEB_ROUTE_LAYOUT:
			return answer_text(app->layout_text, app->layout_length, out, length);

		case WEB_ROUTE_DTC_LAST:
			// The texts mean something only while the poll holds their lists
			written = web_dtc_last_json(poll->has_list ? poll->list_text : NULL, seconds(passed(now, poll->flow.list_end_ms)),
			                            poll->has_old ? poll->old_text : NULL, out, APP_WEB_OUT_SIZE);
			break;

		case WEB_ROUTE_WIFI:
			written = web_wifi_json(app->profiles, app->profile_count, app->ssid, app->seen, app->seen_count, out, APP_WEB_OUT_SIZE);
			break;

		case WEB_ROUTE_TICKET:
			written = web_ticket_json(ticket, access_ticket(&app->access, ticket, now), access_ask_seconds_left(&app->access, now), out, APP_WEB_OUT_SIZE);
			break;

		default:
			return refuse(404, "not_found", out, length);
	}
	return answer(200, written, out, length);
}

void app_web_seen(app_t *app, const web_seen_t *seen, int count)
{
	if(seen == NULL || count < 0) count = 0;
	if(count > LINK_SEEN_MAX) count = LINK_SEEN_MAX;

	for(int i = 0; i < count; i++) app->seen[i] = seen[i];
	app->seen_count = count;
}

int app_web_layout(app_t *app, web_route_t route, const char *body, size_t body_length, char *out,
                   size_t *length, uint64_t now_ms)
{
	const catalog_t *catalog = &app->poll.catalog;
	layout_report_t report;

	if(route != WEB_ROUTE_LAYOUT_CHECK && route != WEB_ROUTE_LAYOUT_APPLY && route != WEB_ROUTE_LAYOUT_SAVE && route != WEB_ROUTE_LAYOUT_RESET)
	{
		return refuse(404, "not_found", out, length);
	}
	// The check changes nothing, not even the time the app has seen
	if(route != WEB_ROUTE_LAYOUT_CHECK && !access_write(&app->access, advance(app, now_ms))) return refuse(403, "locked", out, length);

	if(route == WEB_ROUTE_LAYOUT_RESET)
	{
		// The views the display chooses by itself were read or made without anything to put right
		memset(&report, 0, sizeof(report));
		app_choose_layout(app);
		app->events |= APP_EVENT_ERASE_LAYOUT;
	}
	else
	{
		body = body_of(body, &body_length);
		if(!layout_parse(body, body_length, &app->checked, &report, app->work, app->work_count))
		{
			return answer(400, web_layout_report_json(false, &report, NULL, NULL, out, APP_WEB_OUT_SIZE), out, length);
		}
		if(route == WEB_ROUTE_LAYOUT_CHECK)
		{
			return answer(200, web_layout_report_json(true, &report, &app->checked, catalog, out, APP_WEB_OUT_SIZE), out, length);
		}

		app->layout = app->checked;
		// layout_parse() takes no text that is longer than the room here
		memcpy(app->layout_text, body, body_length);
		app->layout_text[body_length] = '\0';
		app->layout_length = body_length;
		app->source = APP_LAYOUT_PREVIEW;
		if(route == WEB_ROUTE_LAYOUT_SAVE)
		{
			app->source = APP_LAYOUT_STORED;
			app->events |= APP_EVENT_STORE_LAYOUT;
		}
	}
	// The page shown before means nothing in other views, and nav_tick() would only leave one that is gone
	app->nav.page = layout_first_page(&app->layout, catalog);
	return answer(200, web_layout_report_json(true, &report, &app->layout, catalog, out, APP_WEB_OUT_SIZE), out, length);
}

int app_web_wifi(app_t *app, web_route_t route, const char *body, size_t body_length, char *out,
                 size_t *length, uint64_t now_ms)
{
	char ssid[NET_SSID_SIZE];
	uint64_t now;
	int status, left;

	if(route != WEB_ROUTE_WIFI_STORE && route != WEB_ROUTE_WIFI_FORGET) return refuse(404, "not_found", out, length);

	now = advance(app, now_ms);
	body = body_of(body, &body_length);

	if(route == WEB_ROUTE_WIFI_STORE)
	{
		// Under an upload the screen takes no input: the question would wait unseen
		status = ask_refused(app, ACCESS_ASK_WIFI, app->uploading, out, length, now);
		if(status != 0) return status;

		// No question waits: what stands in the room of the request is left over from one that is over. A
		// text that is refused leaves it as it is.
		if(!web_wifi_parse(body, body_length, &app->wifi_asked, app->work, app->work_count)) return refuse(400, "body", out, length);

		app->has_wifi_asked = true;
		return ask(app, ACCESS_ASK_WIFI, app->wifi_asked.ssid, out, length, now);
	}

	if(!access_write(&app->access, now)) return refuse(403, "locked", out, length);
	if(!web_forget_parse(body, body_length, ssid, app->work, app->work_count)) return refuse(400, "body", out, length);

	left = net_forget(app->profiles, app->profile_count, ssid);
	if(left == app->profile_count) return refuse(404, "not_found", out, length);

	app->profile_count = left;
	link_profiles(&app->link, app->profiles, app->profile_count, now);
	// The address of the adapter is forgotten with the old list: the poll has to know before it asks again
	app_net(app, now);
	app->events |= APP_EVENT_STORE_WIFI;
	return answer_text(OK_BODY, sizeof(OK_BODY) - 1, out, length);
}

int app_web_settings(app_t *app, const char *body, size_t body_length, char *out, size_t *length,
                     uint64_t now_ms)
{
	// The longest name of a member is "brightness"
	char member[16];

	if(!access_write(&app->access, advance(app, now_ms))) return refuse(403, "locked", out, length);

	body = body_of(body, &body_length);
	if(!settings_from_json(&app->settings, body, body_length, member, sizeof(member), app->work, app->work_count))
	{
		// The name is one of settings.h, never text out of the request: it needs no escape
		return answer(400, snprintf(out, APP_WEB_OUT_SIZE, "{\"error\":\"body\",\"member\":\"%s\"}", member), out, length);
	}

	knob_set_reverse(&app->knob, app->settings.reverse);
	app->events |= APP_EVENT_STORE_SETTINGS;
	return answer(200, settings_to_json(&app->settings, out, APP_WEB_OUT_SIZE), out, length);
}

int app_web_action(app_t *app, web_route_t route, char *out, size_t *length, uint64_t now_ms)
{
	uint64_t now;
	int status;

	if(route != WEB_ROUTE_REBOOT && route != WEB_ROUTE_RESET) return refuse(404, "not_found", out, length);

	now = advance(app, now_ms);
	if(route == WEB_ROUTE_REBOOT)
	{
		if(!access_write(&app->access, now)) return refuse(403, "locked", out, length);
		// A restart would leave a read without its list and a clear without its outcome
		if(app_busy(app)) return refuse(409, "busy", out, length);

		app_do(app, NAV_DO_REBOOT, now);
		return answer_text(OK_BODY, sizeof(OK_BODY) - 1, out, length);
	}

	status = ask_refused(app, ACCESS_ASK_RESET, app_busy(app), out, length, now);
	if(status != 0) return status;

	drop_network(app);
	return ask(app, ACCESS_ASK_RESET, "", out, length, now);
}

int app_web_upload_begin(app_t *app, const uint8_t *first, size_t first_length, uint32_t file_size,
                         uint32_t slot_size, char *out, size_t *length, uint64_t now_ms)
{
	// In the order of ota_check_t
	static const char *const words[] = {"", "too_short", "no_image", "wrong_chip", "no_description", "wrong_project", "too_large"};
	uint64_t now = advance(app, now_ms);
	// Zero in every byte: the room of the app is written whole from it
	char version[sizeof(app->upload_version)] = "";
	ota_check_t check;

	// The question at the end of the upload needs the release as well: nobody should send megabytes to be
	// told so afterwards
	if(!access_write(&app->access, now) || access_seconds_left(&app->access, now) < APP_WEB_UPLOAD_LEFT_S) return refuse(403, "locked", out, length);
	// While the running firmware is not confirmed the other slot is what the boot loader goes back to
	if(app_busy(app) || app->update_pending) return refuse(409, "busy", out, length);
	// A firmware question of an earlier upload must not be confirmed for a slot that is being rewritten
	if(access_asking(&app->access, now) != ACCESS_ASK_NONE) return refuse(409, "asking", out, length);

	check = ota_check(first, first != NULL ? first_length : 0, file_size, slot_size, version, sizeof(version));
	if(check != OTA_CHECK_OK) return refuse(422, words[check], out, length);

	app->uploading = true;
	app->upload_percent = 0;
	app->upload_ms = now;
	memcpy(app->upload_version, version, sizeof(version));
	// From here on the other slot is rewritten: what it holds is no version to go back to
	app->previous_firmware = false;
	out[0] = '\0';
	*length = 0;
	return 0;
}

void app_web_upload_progress(app_t *app, uint32_t written, uint32_t file_size, uint64_t now_ms)
{
	uint64_t now = advance(app, now_ms);
	uint64_t percent = file_size > 0 ? (uint64_t)written * 100 / file_size : 0;

	// An upload the display has ended (app_tick()) does not come back with the bytes that arrive late
	if(!app->uploading) return;

	app->upload_percent = percent > 100 ? 100 : (int)percent;
	app->upload_ms = now;
}

int app_web_upload_end(app_t *app, bool ok, char *out, size_t *length, uint64_t now_ms)
{
	uint64_t now = advance(app, now_ms);
	bool running = app->uploading;
	int status;

	app->uploading = false;
	// An upload the display has ended by itself (app_tick()) is over, whatever arrived since
	if(!ok || !running) return refuse(500, "upload", out, length);

	status = ask_refused(app, ACCESS_ASK_FIRMWARE, false, out, length, now);
	if(status != 0) return status;

	drop_network(app);
	return ask(app, ACCESS_ASK_FIRMWARE, app->upload_version, out, length, now);
}
