/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "app.h"

#define ADDRESS_PREFIX  "http://"

// The row "Löschen" of the clear dialog (nav.h)
#define ROW_CLEAR       1

// The time of the module at now_ms. The calls come from two tasks, each with its own reading of the time:
// one that lies before the latest seen lets no time pass.
static uint64_t time_at(const app_t *app, uint64_t now_ms)
{
	return now_ms > app->clock_ms ? now_ms : app->clock_ms;
}

static uint64_t advance(app_t *app, uint64_t now_ms)
{
	app->clock_ms = time_at(app, now_ms);
	return app->clock_ms;
}

// A time before the stored one counts as no time passed: the web server writes times of its own
static uint64_t passed(uint64_t now_ms, uint64_t since_ms)
{
	return now_ms > since_ms ? now_ms - since_ms : 0;
}

// A line of the info page while it is written
typedef struct
{
	char *text;
	bool cut;       // a text did not fit: the line ends there, also for what would still fit behind it
} line_t;

// Appends a text to the one in `out`: as many whole characters as fit. A text that is NULL is empty.
// Returns false if something of it was left out.
static bool append(char *out, size_t size, const char *text)
{
	size_t used = strlen(out);
	size_t room = size - 1 - used;
	size_t length;
	bool whole = true;

	if(text == NULL) return true;

	length = strlen(text);
	if(length > room)
	{
		// The first byte left out must not be the middle of a character
		length = room;
		while(length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80) length--;
		whole = false;
	}
	memcpy(&out[used], text, length);
	out[used + length] = '\0';
	return whole;
}

// The next part of an info line. Behind a cut nothing follows: a line is the beginning of its text, not what
// happens to fit of it.
static void put(line_t *line, const char *text)
{
	if(!line->cut && !append(line->text, APP_INFO_SIZE, text)) line->cut = true;
}

// A text that is not there is shown as the dash, like a value that is not there
static void put_or_dash(line_t *line, const char *text)
{
	put(line, text != NULL && text[0] != '\0' ? text : SCENE_DASH);
}

// In 64 bit: every number of the info lines, whatever its type
static void put_number(line_t *line, int64_t number)
{
	// The longest is "-9223372036854775808"
	char digits[21];
	size_t first = sizeof(digits) - 1;
	uint64_t rest = number < 0 ? 0 - (uint64_t)number : (uint64_t)number;

	digits[first] = '\0';
	do
	{
		digits[--first] = (char)('0' + rest % 10);
		rest /= 10;
	}
	while(rest > 0);
	if(number < 0) digits[--first] = '-';
	put(line, &digits[first]);
}

// As much of a text of the platform as its field holds
static void copy_text(char *out, size_t size, const char *text)
{
	out[0] = '\0';
	append(out, size, text);
}

// A brightness the settings can hold: what is stored has to be read again at the next start
static int percent_of(int value)
{
	if(value < SETTINGS_BRIGHTNESS_MIN) return SETTINGS_BRIGHTNESS_MIN;
	if(value > SETTINGS_BRIGHTNESS_MAX) return SETTINGS_BRIGHTNESS_MAX;
	return value;
}

// The lines for the screen follow what the poll holds
static void rebuild_lines(app_t *app)
{
	const poll_t *poll = &app->poll;

	app->list_lines = poll->has_list ? dtc_view_list(&poll->list, app->list, DTC_VIEW_LINES_MAX) : 0;
	if(poll->has_list) dtc_summarize(&poll->list, &app->summary);
	// The list an outcome is compared with became the old list when its clear was accepted
	app->cleared_lines = poll->has_cleared ? dtc_view_cleared(&poll->old, &poll->cleared, app->cleared, DTC_VIEW_LINES_MAX) : 0;
	app->old_lines = poll->has_old ? dtc_view_list(&poll->old, app->old, DTC_VIEW_LINES_MAX) : 0;
}

// What the poll asks to be done
static void take_poll_events(app_t *app)
{
	uint32_t events = poll_take_events(&app->poll);

	if(events & POLL_EVENT_BOUND) app->events |= APP_EVENT_STORE_BOUND;
	if(events & POLL_EVENT_CATALOG) app->events |= APP_EVENT_STORE_CATALOG;
	if(events & POLL_EVENT_OLD) app->events |= APP_EVENT_STORE_OLD;
	if(events & POLL_EVENT_LISTS) rebuild_lines(app);
}

// The question of the browser is over: what it asked for is not kept, least of all a password
static void drop_asked(app_t *app)
{
	memset(&app->wifi_asked, 0, sizeof(app->wifi_asked));
	app->has_wifi_asked = false;
	app->ask_detail[0] = '\0';
}

// The knob confirmed that the network of the browser is stored
static void store_wifi(app_t *app, uint64_t now)
{
	const web_wifi_request_t *asked = &app->wifi_asked;
	const char *password = asked->password;
	int count;

	if(!app->has_wifi_asked) return;

	if(!asked->has_password)
	{
		// The browser never gets a stored password, so it cannot send it back with another host
		// Of two entries with the same SSID the first counts, as for net_store()
		password = "";
		for(int i = app->profile_count - 1; i >= 0; i--)
		{
			if(strcmp(app->profiles[i].ssid, asked->ssid) == 0) password = app->profiles[i].password;
		}
	}
	count = net_store(app->profiles, app->profile_count, asked->ssid, password, asked->host);
	if(count < 0) return;

	app->profile_count = count;
	link_profiles(&app->link, app->profiles, app->profile_count, now);
	// The address of the adapter is forgotten with the old list: the poll has to know before it asks again
	app_net(app, now);
	app->events |= APP_EVENT_STORE_WIFI;
}

// The next info line, beginning with `label`
static line_t add_line(app_t *app, const char *label)
{
	line_t line = {app->info[app->info_count++], false};

	put(&line, label);
	return line;
}

static void make_info(app_t *app)
{
	static const char *const sources[] = {"gespeichert", "eingebaut", "erzeugt", "Vorschau"};
	const wican_state_t *state = conn_state(&app->poll.conn);
	line_t line;

	memset(app->info, 0, sizeof(app->info));
	app->info_count = 0;
	if(app->rolled_back) add_line(app, "Update nicht übernommen – vorherige Version aktiv");

	line = add_line(app, "WLAN: ");
	put_or_dash(&line, app->ssid);
	if(app->ssid[0] != '\0')
	{
		put(&line, " (");
		put_number(&line, app->rssi);
		put(&line, " dBm)");
	}

	line = add_line(app, "Adresse: ");
	put_or_dash(&line, app->ip);

	line = add_line(app, "WiCAN: ");
	put_or_dash(&line, app_host(app));

	line = add_line(app, "WiCAN-ID: ");
	put_or_dash(&line, app->poll.bound_id);

	line = add_line(app, "WiCAN-Firmware: ");
	put_or_dash(&line, state != NULL ? state->fw : NULL);

	line = add_line(app, "Version: ");
	put_or_dash(&line, app->version);
	put(&line, " (");
	put_or_dash(&line, app->slot);
	put(&line, ")");

	line = add_line(app, "Ansichten: ");
	put_or_dash(&line, app->layout.name);
	put(&line, " (");
	// In the order of app_layout_source_t; what is no source has no word
	put_or_dash(&line, (unsigned)app->source < sizeof(sources) / sizeof(sources[0]) ? sources[app->source] : NULL);
	put(&line, ")");

	line = add_line(app, "Speicher: ");
	put_number(&line, app->heap);
	put(&line, " frei, min. ");
	put_number(&line, app->heap_min);

	line = add_line(app, "PSRAM: ");
	put_number(&line, app->psram);
	put(&line, " frei, min. ");
	put_number(&line, app->psram_min);

	line = add_line(app, "HTTP: ");
	put_number(&line, app->poll.http_ok);
	put(&line, " ok, ");
	put_number(&line, app->poll.http_failed);
	put(&line, " Fehler");

	line = add_line(app, "Neuverbindungen: ");
	put_number(&line, app->reconnects);

	line = add_line(app, "Temperatur: ");
	if(app->has_temp)
	{
		put_number(&line, app->temp_c);
		put(&line, " °C");
	}
	else
	{
		put(&line, SCENE_DASH);
	}

	line = add_line(app, "Letzter Neustart: ");
	put_or_dash(&line, app->reset);
}

// Whether the display has something to show that changes: without it the standby rule lets the screen go dark
static bool showing(const app_t *app, const nav_world_t *world, uint64_t now)
{
	const values_t *values = &app->poll.values;
	conn_view_t view = conn_view(&app->poll.conn, now);

	if(app->nav.screen != NAV_PAGES || nav_overlay(world) != NAV_OVER_NONE) return true;
	if(view == CONN_VIEW_LIVE || view == CONN_VIEW_SCAN) return true;
	if(view != CONN_VIEW_NO_API) return false;

	// A firmware without the API tells nothing about the ignition. Its values are the sign of a vehicle that
	// runs: without this the screen would go dark on the road.
	for(int i = 0; i < values->count; i++)
	{
		if(values_age(&values->items[i], now) != VALUE_AGE_GONE) return true;
	}
	return false;
}

static int backlight(const app_t *app, const nav_world_t *world, uint64_t now)
{
	settings_t settings = app->settings;

	// The brightness being set is seen at once, long before it is stored
	if(app->brightness_preview >= 0)
	{
		if(settings.night_mode) settings.night = (uint8_t)app->brightness_preview;
		else settings.brightness = (uint8_t)app->brightness_preview;
	}
	return guard_brightness(app->heat, settings_backlight(&settings, showing(app, world, now), passed(now, app->last_input_ms)));
}

// An input arrived. true if it is not passed on: nobody acts on a screen he cannot see. It restarts the idle
// time, which wakes a screen that is dark by the standby rule; one the heat keeps dark stays dark.
static bool in_the_dark(app_t *app, const nav_world_t *world, uint64_t now)
{
	bool dark = backlight(app, world, now) == 0;

	app->last_input_ms = now;
	return dark;
}

// A fault memory request of the display is under way: handed to the flow and not ended
static bool under_way(const app_t *app)
{
	dtc_flow_phase_t phase = app->poll.flow.phase;

	return phase == DTC_FLOW_READ_SENT || phase == DTC_FLOW_READING || phase == DTC_FLOW_CLEAR_SENT || phase == DTC_FLOW_CLEARING;
}

// The one place where the app learns that a request of the display to the fault memory has ended (app.h): the
// tick, before nav follows the flow. That moment counts as an input for the two idle times and for nothing
// else - who reads and waits is to see the outcome, however long ago the last hand was at the display.
static void follow_request(app_t *app, const nav_world_t *world, uint64_t now)
{
	bool ended = app->requesting && !under_way(app);

	app->requesting = under_way(app);
	if(!ended) return;

	// The input that acts on nothing (nav.h): the outcome stays for NAV_IDLE_MS unless somebody acts
	nav_swipe(&app->nav, 0, world, now);
	// A screen dark by the standby rule lights up. One the heat keeps dark is woken by nothing, and it is not to
	// light up for this outcome when the heat lets go: its idle time stays what the last hand left.
	if(app->heat != GUARD_HEAT_OFF) app->last_input_ms = now;
}

void app_choose_layout(app_t *app)
{
	const catalog_t *catalog = &app->poll.catalog;
	app_layout_source_t source = app->has_builtin && layout_suits(&app->builtin, catalog) ? APP_LAYOUT_BUILTIN : APP_LAYOUT_GENERATED;

	if(source == APP_LAYOUT_BUILTIN)
	{
		app->layout = app->builtin;
		memcpy(app->layout_text, app->builtin_text, app->builtin_length);
		app->layout_text[app->builtin_length] = '\0';
	}
	else
	{
		layout_from_catalog(catalog, &app->layout);
		// A text that could not be written is an empty one
		layout_to_json(&app->layout, app->layout_text, sizeof(app->layout_text));
	}
	app->layout_length = strlen(app->layout_text);

	// Other views: the page shown before means nothing in them. The same views made anew (the catalogue grew)
	// must not throw the driver back to the first page.
	if(source != app->source) app->nav.page = layout_first_page(&app->layout, catalog);
	app->source = source;
}

void app_init(app_t *app, const app_boot_t *boot, uint64_t now_ms)
{
	nav_world_t world;

	memset(app, 0, sizeof(*app));
	app->clock_ms = now_ms;
	app->last_input_ms = now_ms;
	app->work = boot->work;
	app->work_count = boot->work_count;
	app->version = boot->version;
	app->git = boot->git;
	app->safe_mode = boot->safe_mode;
	app->update_pending = boot->update_pending;
	// Behind the largest time there is no moment at which the update could be found unconfirmed
	app->update_until_ms = now_ms > UINT64_MAX - APP_UPDATE_CONFIRM_MS ? UINT64_MAX : now_ms + APP_UPDATE_CONFIRM_MS;
	app->previous_firmware = boot->previous_firmware;
	app->rolled_back = boot->rolled_back;
	app->brightness_preview = -1;
	for(int i = 0; i < APP_INFO_LINES; i++) app->info_lines[i] = app->info[i];

	settings_defaults(&app->settings);
	if(boot->settings_json != NULL)
	{
		settings_from_json(&app->settings, boot->settings_json, strlen(boot->settings_json), NULL, 0, app->work, app->work_count);
	}

	if(boot->profiles != NULL && boot->profile_count > 0 && boot->profile_count <= NET_PROFILES_MAX)
	{
		app->profile_count = boot->profile_count;
		memcpy(app->profiles, boot->profiles, (size_t)boot->profile_count * sizeof(app->profiles[0]));
		// What was read from the flash may be damaged: every text gets its end
		for(int i = 0; i < app->profile_count; i++)
		{
			app->profiles[i].ssid[NET_SSID_SIZE - 1] = '\0';
			app->profiles[i].password[NET_PASSWORD_SIZE - 1] = '\0';
			app->profiles[i].host[NET_HOST_SIZE - 1] = '\0';
		}
	}

	poll_init(&app->poll, boot->bound_id);
	poll_stored(&app->poll, boot->catalog_json, boot->catalog_length, boot->old_text, boot->old_length, app->work, app->work_count);
	// The lines of the stored old list
	take_poll_events(app);
	link_init(&app->link, app->profiles, app->profile_count, app->safe_mode, now_ms);
	knob_init(&app->knob, app->settings.reverse);
	hold_init(&app->hold);
	access_init(&app->access);

	if(boot->builtin_layout != NULL && layout_parse(boot->builtin_layout, boot->builtin_length, &app->builtin, NULL, app->work, app->work_count))
	{
		app->has_builtin = true;
		app->builtin_text = boot->builtin_layout;
		app->builtin_length = boot->builtin_length;
	}
	// In safe mode the stored layout is not even read: it may be what crashed the display
	if(!app->safe_mode && boot->layout_text != NULL &&
	   layout_parse(boot->layout_text, boot->layout_length, &app->layout, NULL, app->work, app->work_count))
	{
		app->source = APP_LAYOUT_STORED;
		// layout_parse() takes no text that is longer than the room here
		memcpy(app->layout_text, boot->layout_text, boot->layout_length);
		app->layout_length = boot->layout_length;
	}
	else
	{
		app_choose_layout(app);
	}
	app->catalog_sum = catalog_checksum(&app->poll.catalog);

	make_info(app);
	app_world(app, &world, now_ms);
	nav_init(&app->nav, &world, now_ms);
}

void app_button(app_t *app, bool pressed, bool read_ok, uint64_t now_ms)
{
	uint64_t now = advance(app, now_ms);
	bool was_pressed = knob_is_pressed(&app->knob);
	nav_world_t world;
	knob_event_t knob_event;
	hold_event_t hold_event;
	bool on_action;

	app_world(app, &world, now);
	// Under an overlay nobody sees the dialog: a hold must not go on there
	on_action = app->nav.screen == NAV_DTC_CONFIRM && app->nav.row == ROW_CLEAR && nav_overlay(&world) == NAV_OVER_NONE;
	knob_event = knob_sample(&app->knob, pressed, read_ok, now);
	hold_event = hold_sample(&app->hold, pressed, read_ok, on_action, now);

	// A press begins. What the knob reports of it later is dropped if it began in the dark.
	if(!was_pressed && knob_is_pressed(&app->knob)) app->woke = in_the_dark(app, &world, now);

	if(knob_event != KNOB_NONE && !app->woke)
	{
		app_do(app, knob_event == KNOB_SHORT ? nav_short(&app->nav, &world, now) : nav_long(&app->nav, &world, now), now);
	}
	app_do(app, nav_hold(&app->nav, hold_event, &world, now), now);
}

void app_encoder(app_t *app, int counts, uint64_t now_ms)
{
	uint64_t now = advance(app, now_ms);
	int detents = knob_turn(&app->knob, counts, now);
	nav_world_t world;

	if(detents == 0) return;

	app_world(app, &world, now);
	if(in_the_dark(app, &world, now)) return;

	if(app->nav.screen == NAV_DTC_CONFIRM) hold_activity(&app->hold, now);
	app_do(app, nav_turn(&app->nav, detents, &world, now), now);
}

void app_tap(app_t *app, int row, uint64_t now_ms)
{
	uint64_t now = advance(app, now_ms);
	nav_world_t world;

	app_world(app, &world, now);
	if(in_the_dark(app, &world, now)) return;

	if(app->nav.screen == NAV_DTC_CONFIRM) hold_activity(&app->hold, now);
	app_do(app, nav_tap(&app->nav, row, &world, now), now);
}

void app_swipe(app_t *app, int dx, int dy, uint64_t now_ms)
{
	uint64_t now = advance(app, now_ms);
	// In the two dialogs the knob alone moves the focus: a finger that slips must not put it on the answer
	// that acts
	bool dialog = app->nav.screen == NAV_DTC_CONFIRM || app->nav.screen == NAV_CONFIRM;
	nav_world_t world;
	nav_do_t what;

	app_world(app, &world, now);
	if(in_the_dark(app, &world, now)) return;

	if(app->nav.screen == NAV_DTC_CONFIRM) hold_activity(&app->hold, now);
	// The finger goes where the content goes: up brings the rows below, left the next page
	if(dx == 0 && dy != 0 && !dialog && nav_rows(&app->nav, &world) > 0) what = nav_turn(&app->nav, dy < 0 ? APP_SWIPE_ROWS : -APP_SWIPE_ROWS, &world, now);
	else what = nav_swipe(&app->nav, dx < 0 ? 1 : dx > 0 ? -1 : 0, &world, now);
	app_do(app, what, now);
}

void app_tick(app_t *app, uint64_t now_ms)
{
	uint64_t now = advance(app, now_ms);
	nav_world_t world;

	app_world(app, &world, now);
	follow_request(app, &world, now);
	app_do(app, nav_tick(&app->nav, &world, now), now);

	// The boot loader takes back an update that was not confirmed before the restart. Asked for once: the
	// platform restarts when it has carried out what waits.
	if(app->update_pending && !app->update_given_up && now >= app->update_until_ms)
	{
		app->update_given_up = true;
		app->events |= APP_EVENT_REBOOT;
	}
	// An upload that stalls must not lock the display, which takes no input while it runs
	if(app->uploading && passed(now, app->upload_ms) >= APP_UPLOAD_IDLE_MS) app->uploading = false;
	// The question ended by its time or with the release, which nobody reports
	if(world.asking == ACCESS_ASK_NONE) drop_asked(app);
	make_info(app);
}

void app_temperature(app_t *app, int celsius, bool valid)
{
	nav_world_t world;

	app->heat = guard_heat(app->heat, celsius, valid);
	app->has_temp = valid;
	if(valid) app->temp_c = celsius;

	if(app->heat != GUARD_HEAT_OFF) return;

	// A press that is under way began on a screen its hand could see and ends on one it cannot. Below it
	// the question it was meant for is refused, the dialog it stood in is left: what it would act on is no
	// longer what it was made for. The knob's report of that press is dropped, as of one begun in the dark.
	// Set whether a press is under way or not: without one nothing reads this before the next press begins,
	// which decides anew.
	app->woke = true;

	// Nobody sees a question on a screen the heat switched off, and nobody may answer one there. What the
	// browser asks is refused and dropped: it learns that at once, and nothing waits for a press in the dark.
	if(access_asking(&app->access, app->clock_ms) != ACCESS_ASK_NONE) app_do(app, NAV_DO_ASK_REFUSE, app->clock_ms);
	// The clear dialog and the dialog of the settings are left as by "Abbrechen", whatever lies over them
	app_world(app, &world, app->clock_ms);
	app_do(app, nav_cancel(&app->nav, &world, app->clock_ms), app->clock_ms);
}

void app_platform(app_t *app, const app_platform_t *platform)
{
	copy_text(app->ssid, sizeof(app->ssid), platform->ssid);
	copy_text(app->ip, sizeof(app->ip), platform->ip);
	copy_text(app->ap_ssid, sizeof(app->ap_ssid), platform->ap_ssid);
	copy_text(app->ap_password, sizeof(app->ap_password), platform->ap_password);
	copy_text(app->slot, sizeof(app->slot), platform->slot);
	copy_text(app->reset, sizeof(app->reset), platform->reset);
	app->rssi = platform->rssi;
	app->heap = platform->heap;
	app->heap_min = platform->heap_min;
	app->psram = platform->psram;
	app->psram_min = platform->psram_min;
	app->reconnects = platform->reconnects;
}

void app_net(app_t *app, uint64_t now_ms)
{
	uint64_t now = advance(app, now_ms);
	conn_view_t view;
	uint32_t sum;

	poll_wifi(&app->poll, link_up(&app->link), now);
	view = conn_view(&app->poll.conn, now);
	link_answering(&app->link, view != CONN_VIEW_NO_ANSWER && view != CONN_VIEW_CONNECTING, now);
	take_poll_events(app);

	sum = catalog_checksum(&app->poll.catalog);
	if(sum != app->catalog_sum)
	{
		app->catalog_sum = sum;
		// What the user stored or is looking at stays, whatever the vehicle is
		if(app->source != APP_LAYOUT_STORED && app->source != APP_LAYOUT_PREVIEW) app_choose_layout(app);
	}
}

const char *app_host(const app_t *app)
{
	return link_host(&app->link);
}

void app_do(app_t *app, nav_do_t what, uint64_t now_ms)
{
	uint64_t now = advance(app, now_ms);
	nav_world_t world;

	switch(what)
	{
		case NAV_DO_READ:
			poll_read(&app->poll, now);
			take_poll_events(app);
			// Begun between two ticks: its end is seen also if it comes before the next one
			if(under_way(app)) app->requesting = true;
			break;

		case NAV_DO_HOLD_OPEN:
			if(hold_open(&app->hold, now)) break;
			// The switch hangs: the dialog must not offer what it cannot confirm
			app_world(app, &world, now);
			nav_hold(&app->nav, HOLD_STUCK, &world, now);
			break;

		case NAV_DO_HOLD_CLOSE:
			hold_close(&app->hold);
			break;

		case NAV_DO_CLEAR:
			poll_clear(&app->poll, hold_is_stuck(&app->hold), now);
			take_poll_events(app);
			// As for a read
			if(under_way(app)) app->requesting = true;
			break;

		case NAV_DO_DISMISS:
			poll_dismiss(&app->poll);
			take_poll_events(app);
			break;

		case NAV_DO_BRIGHTNESS:
			app->brightness_preview = percent_of(app->nav.value);
			break;

		case NAV_DO_SETTINGS_STORE:
			if(app->settings.night_mode) app->settings.night = (uint8_t)percent_of(app->nav.value);
			else app->settings.brightness = (uint8_t)percent_of(app->nav.value);
			app->brightness_preview = -1;
			app->events |= APP_EVENT_STORE_SETTINGS;
			break;

		case NAV_DO_NIGHT_TOGGLE:
			app->settings.night_mode = !app->settings.night_mode;
			app->events |= APP_EVENT_STORE_SETTINGS;
			break;

		case NAV_DO_REVERSE_TOGGLE:
			app->settings.reverse = !app->settings.reverse;
			knob_set_reverse(&app->knob, app->settings.reverse);
			app->events |= APP_EVENT_STORE_SETTINGS;
			break;

		case NAV_DO_AP_TOGGLE:
			link_ap_request(&app->link, !link_ap_on(&app->link), now);
			break;

		case NAV_DO_RELEASE_ON:
			access_open(&app->access, now);
			break;

		case NAV_DO_RELEASE_OFF:
			access_close(&app->access, now);
			drop_asked(app);
			break;

		case NAV_DO_ASK_CONFIRM:
			switch(access_confirm(&app->access, now))
			{
				case ACCESS_ASK_WIFI:
					store_wifi(app, now);
					break;
				case ACCESS_ASK_FIRMWARE:
					app->events |= APP_EVENT_INSTALL_FIRMWARE;
					break;
				case ACCESS_ASK_RESET:
					app->events |= APP_EVENT_FACTORY_RESET;
					break;
				// The press came too soon or too late: a question that still waits keeps what it asks for
				default:
					return;
			}
			drop_asked(app);
			break;

		case NAV_DO_ASK_REFUSE:
			access_refuse(&app->access, now);
			drop_asked(app);
			break;

		case NAV_DO_UPDATE_OK:
			app->update_pending = false;
			app->events |= APP_EVENT_MARK_VALID;
			break;

		case NAV_DO_REBOOT:
			app->events |= APP_EVENT_REBOOT;
			break;

		case NAV_DO_PREVIOUS_FIRMWARE:
			// The dialog may have been opened before an upload overwrote the other slot: what is there now is a
			// firmware nobody confirmed
			if(app->previous_firmware) app->events |= APP_EVENT_PREVIOUS_FIRMWARE;
			break;

		case NAV_DO_FACTORY_RESET:
			app->events |= APP_EVENT_FACTORY_RESET;
			break;

		// NAV_DO_NOTHING, and what is no action of nav.h
		default:
			break;
	}
}

void app_world(const app_t *app, nav_world_t *world, uint64_t now_ms)
{
	uint64_t now = time_at(app, now_ms);
	const poll_t *poll = &app->poll;

	// Every byte, also those between the fields
	memset(world, 0, sizeof(*world));
	world->layout = &app->layout;
	world->catalog = &poll->catalog;
	world->flow = poll->flow.phase;
	world->can_read = dtc_flow_read_block(&poll->flow, &poll->conn, &poll->values, &poll->catalog, now) == DTC_FLOW_ALLOWED;
	world->can_clear = dtc_flow_clear_block(&poll->flow, &poll->conn, &poll->values, &poll->catalog, hold_is_stuck(&app->hold), now) == DTC_FLOW_ALLOWED;
	world->list_lines = app->list_lines;
	world->cleared_lines = app->cleared_lines;
	world->old_lines = app->old_lines;
	world->info_lines = app->info_count;
	world->asking = access_asking(&app->access, now);
	world->release_open = access_is_open(&app->access, now);
	world->update_pending = app->update_pending;
	world->uploading = app->uploading;
	world->previous_firmware = app->previous_firmware;
	world->ap_kept = link_ap_kept(&app->link);
	world->night_mode = app->settings.night_mode;
	world->brightness = app->settings.night_mode ? app->settings.night : app->settings.brightness;
}

void app_scene(const app_t *app, scene_t *scene, uint64_t now_ms)
{
	uint64_t now = time_at(app, now_ms);
	const poll_t *poll = &app->poll;
	char address[sizeof(ADDRESS_PREFIX) + sizeof(app->ip)] = "";
	nav_world_t world;
	scene_input_t input;

	app_world(app, &world, now);
	if(app->ip[0] != '\0')
	{
		append(address, sizeof(address), ADDRESS_PREFIX);
		append(address, sizeof(address), app->ip);
	}

	memset(&input, 0, sizeof(input));
	input.nav = &app->nav;
	input.world = &world;
	input.conn = &poll->conn;
	input.values = &poll->values;
	input.flow = &poll->flow;
	input.read_block = dtc_flow_read_block(&poll->flow, &poll->conn, &poll->values, &poll->catalog, now);
	input.clear_block = dtc_flow_clear_block(&poll->flow, &poll->conn, &poll->values, &poll->catalog, hold_is_stuck(&app->hold), now);
	if(poll->has_list)
	{
		input.list = app->list;
		input.summary = &app->summary;
	}
	if(poll->has_cleared) input.cleared = app->cleared;
	if(poll->has_old) input.old = app->old;
	input.hold = &app->hold;
	input.access = &app->access;
	input.info = app->info_lines;
	input.address = address;
	input.ap_on = link_ap_on(&app->link);
	input.ap_ssid = app->ap_ssid;
	input.ap_password = app->ap_password;
	input.reverse = app->settings.reverse;
	input.ask_detail = app->ask_detail;
	input.upload_percent = app->upload_percent;
	// Rounded up: 0 only when no time is left
	if(app->update_pending) input.update_left_s = (uint32_t)((passed(app->update_until_ms, now) + 999) / 1000);
	input.safe_mode = app->safe_mode;
	input.heat = app->heat;
	input.now_ms = now;
	scene_build(&input, scene);
}

int app_backlight(const app_t *app, uint64_t now_ms)
{
	uint64_t now = time_at(app, now_ms);
	nav_world_t world;

	app_world(app, &world, now);
	return backlight(app, &world, now);
}

bool app_busy(const app_t *app)
{
	return under_way(app) || app->nav.screen == NAV_DTC_CONFIRM || app->uploading;
}

uint32_t app_take_events(app_t *app)
{
	uint32_t events = app->events;

	app->events = 0;
	return events;
}
