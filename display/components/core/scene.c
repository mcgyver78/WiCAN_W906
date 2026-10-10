/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <math.h>
#include <string.h>
#include "scene.h"
#include "fmt.h"

#define COUNT(a)    ((int)(sizeof(a) / sizeof((a)[0])))

// A row to choose. Whether it does something now is not written down here: nav.h is asked (nav_row_acts())
typedef struct
{
	const char *text;
	const char *detail;
} choice_t;

// Of scene_dump()
typedef struct
{
	char *out;
	size_t size;
	size_t length;      // of the whole text, also when it does not fit
} writer_t;

// Appends a text to the one in `out`: as many whole characters as fit. A text that is NULL is empty.
static void append(char *out, size_t size, const char *text)
{
	size_t used = strlen(out);
	size_t room = size - 1 - used;
	size_t length;

	if(text == NULL) return;

	length = strlen(text);
	if(length > room)
	{
		// The first byte left out must not be the middle of a character
		length = room;
		while(length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80) length--;
	}
	memcpy(&out[used], text, length);
	out[used + length] = '\0';
}

// In 64 bit: every number of the inputs, whatever its type
static void append_number(char *out, size_t size, int64_t number)
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
	append(out, size, &digits[first]);
}

static void append_percent(char *out, size_t size, int percent)
{
	append_number(out, size, percent);
	append(out, size, " %");
}

// Minutes and seconds, "9:12"
static void append_time(char *out, size_t size, uint32_t seconds)
{
	append_number(out, size, seconds / 60);
	append(out, size, ":");
	append_number(out, size, seconds % 60 / 10);
	append_number(out, size, seconds % 10);
}

static void set_title(scene_t *scene, const char *text)
{
	append(scene->title, sizeof(scene->title), text);
}

static void set_note(scene_t *scene, const char *text)
{
	append(scene->note, sizeof(scene->note), text);
}

// The next text line, still empty. No screen has more than SCENE_LINES_MAX of them.
static char *add_line(scene_t *scene)
{
	return scene->lines[scene->line_count++];
}

static void add_text(scene_t *scene, const char *text)
{
	append(add_line(scene), SCENE_TEXT_SIZE, text);
}

static char *add_over_line(scene_t *scene)
{
	return scene->over_lines[scene->over_line_count++];
}

static void add_over_text(scene_t *scene, const char *text)
{
	append(add_over_line(scene), SCENE_TEXT_SIZE, text);
}

// Where the shown value lies in the range of an arc or a bar, -1 if the item has no range
static int range_permille(const layout_item_t *item, const value_t *value)
{
	double shown, span, part;

	// Written this way round a limit that is no number is no range either
	if(!item->min.set || !item->max.set || !(item->min.value < item->max.value)) return -1;

	// A statement of its own: merged with the subtraction below (fused multiply-add) the product would not
	// be the number the text shows
	shown = (value->kind == VALUE_NUMBER ? value->number : value->kind == VALUE_ON ? 1 : 0) * item->scale;
	span = item->max.value - item->min.value;
	part = (shown - item->min.value) * 1000 / span;

	// Limits so far apart that a double cannot calculate with them. The width is looked at by itself:
	// divided by one that is infinite, every value would lie at the lower end.
	if(!isfinite(span) || !isfinite(part)) return -1;
	if(part <= 0) return 0;
	if(part >= 1000) return 1000;
	return (int)part;
}

// One value of a page. level, old: what the page says to the ring - the worst level of a value that is
// shown, and whether a value is old or missing.
static void build_item(const scene_input_t *input, conn_view_t view, const layout_item_t *item, scene_item_t *out, int *level, bool *old)
{
	const catalog_t *catalog = input->world->catalog;
	const value_t *value = values_find(input->values, item->key);
	// During a scan the adapter delivers no values, for 35 s as measured on the vehicle: what the display holds
	// stands still and stays on the page as an old value, however long that takes. Judged by its age it would
	// be a dash from VALUE_KEPT_MS on, which is most of the scan.
	layout_item_state_t state = view == CONN_VIEW_SCAN && value != NULL ? LAYOUT_ITEM_OLD : layout_item_state(item, catalog, input->values, input->now_ms);
	// A value that has no text (not finite, too large) is shown like one that is missing
	bool shown = (state == LAYOUT_ITEM_LIVE || state == LAYOUT_ITEM_OLD) && layout_item_text(item, value, out->text, sizeof(out->text));
	int item_level;

	// A label that does not fit stays empty, also behind its first byte: fmt_label() leaves there what it
	// had written
	if(item->label[0] != '\0') append(out->label, sizeof(out->label), item->label);
	else if(!fmt_label(item->key, out->label, sizeof(out->label))) memset(out->label, 0, sizeof(out->label));

	// What is no widget is shown as a number, the way layout.h makes its text
	out->widget = LAYOUT_WIDGET_NUMBER;
	if(item->widget == LAYOUT_WIDGET_ARC || item->widget == LAYOUT_WIDGET_BAR || item->widget == LAYOUT_WIDGET_STATE) out->widget = item->widget;
	out->tone = SCENE_TONE_DIM;
	out->permille = -1;

	if(!shown)
	{
		// A dash is missed only where a value was: one the display holds was delivered on this connection, and
		// its page must not have the ring of a page on which all is well. A value the vehicle never answers is
		// a dash as well, but nothing went missing - its page would keep the yellow ring for ever. What the
		// profile does not provide is missed by nobody.
		if(value != NULL && state != LAYOUT_ITEM_UNAVAILABLE) *old = true;
		append(out->text, sizeof(out->text), state == LAYOUT_ITEM_UNAVAILABLE ? SCENE_UNAVAILABLE : SCENE_DASH);
		return;
	}

	item_level = layout_item_level(item, value);
	if(item_level > *level) *level = item_level;
	if(state == LAYOUT_ITEM_OLD) *old = true;

	if(state == LAYOUT_ITEM_LIVE)
	{
		out->tone = item_level >= 2 ? SCENE_TONE_ALARM : item_level == 1 ? SCENE_TONE_WARN : SCENE_TONE_NORMAL;
	}
	if(out->widget != LAYOUT_WIDGET_STATE) append(out->unit, sizeof(out->unit), layout_item_unit(item, catalog));
	if(out->widget == LAYOUT_WIDGET_ARC || out->widget == LAYOUT_WIDGET_BAR) out->permille = range_permille(item, value);
}

static void build_pages(const scene_input_t *input, conn_view_t view, const wican_state_t *state, scene_t *scene, int *level, bool *old)
{
	const nav_world_t *world = input->world;
	const layout_t *layout = world->layout;
	const layout_page_t *page;
	int shown = input->nav->page;

	for(int i = 0; i < layout->page_count; i++)
	{
		if(!layout_page_shown(layout, i, world->catalog)) continue;

		if(i == shown) scene->dot = scene->dots;
		scene->dots++;
	}

	if(input->safe_mode) set_note(scene, "Sicherer Modus – eingebaute Ansichten");
	else if(input->heat != GUARD_HEAT_NORMAL) set_note(scene, "Zu heiß – Anzeige gedimmt");
	else if(view == CONN_VIEW_SCAN || view == CONN_VIEW_NO_API) set_note(scene, text_view(view));

	if(view != CONN_VIEW_LIVE && view != CONN_VIEW_SCAN && view != CONN_VIEW_NO_API)
	{
		scene->kind = SCENE_NOTICE;
		add_text(scene, text_view(view));
		// In this view there is a state: conn.h takes the ignition from it
		if(view == CONN_VIEW_ECU_OFFLINE && state->batt_mv >= 0)
		{
			// Rounded to a tenth of a volt, without floating point
			uint32_t tenths = ((uint32_t)state->batt_mv + 50) / 100;
			char *line = add_line(scene);

			append(line, SCENE_TEXT_SIZE, "Bordnetz ");
			append_number(line, SCENE_TEXT_SIZE, tenths / 10);
			append(line, SCENE_TEXT_SIZE, ",");
			append_number(line, SCENE_TEXT_SIZE, tenths % 10);
			append(line, SCENE_TEXT_SIZE, " V");
		}
		return;
	}
	if(shown < 0 || shown >= layout->page_count)
	{
		scene->kind = SCENE_NOTICE;
		add_text(scene, "Keine Ansicht mit verfügbaren Werten");
		return;
	}

	page = &layout->pages[shown];
	scene->kind = SCENE_VALUES;
	set_title(scene, page->title);
	scene->item_count = page->item_count;
	for(int i = 0; i < page->item_count; i++) build_item(input, view, &page->items[i], &scene->items[i], level, old);
}

// The rows of a screen: the lines of a fault memory list (`lines`) or of the info (`texts`), then its
// choices. Visible are the rows around the focus.
static void build_rows(const scene_input_t *input, const dtc_line_t *lines, const char *const *texts,
                       const choice_t *choices, int choice_count, scene_t *scene)
{
	const nav_t *nav = input->nav;
	// How many lines there are is the matter of nav.h; without the lines themselves there are none
	int total = lines != NULL || texts != NULL ? nav_rows(nav, input->world) : choice_count;
	int line_count = total - choice_count;
	// In 64 bit: the focus may be any number
	int64_t first = (int64_t)nav->row - SCENE_ROWS_MAX / 2;

	if(first > total - SCENE_ROWS_MAX) first = total - SCENE_ROWS_MAX;
	if(first < 0) first = 0;

	scene->kind = SCENE_LIST;
	scene->total = total;
	scene->first = (int)first;
	for(int index = scene->first; index < total && scene->row_count < SCENE_ROWS_MAX; index++)
	{
		scene_row_t *row = &scene->rows[scene->row_count++];

		row->kind = SCENE_ROW_LINE;
		row->enabled = true;
		row->focus = index == nav->row;
		if(index >= line_count)
		{
			const choice_t *choice = &choices[index - line_count];

			row->kind = SCENE_ROW_ACTION;
			// Enabled is what a press acts on: the rules of the press are asked, none is kept here. nav.h counts
			// the choices behind the lines the world names, also when the scene has no texts for them.
			row->enabled = nav_row_acts(nav, index - line_count + (nav_rows(nav, input->world) - choice_count), input->world);
			append(row->text, sizeof(row->text), choice->text);
			append(row->detail, sizeof(row->detail), choice->detail);
		}
		else if(lines != NULL)
		{
			if(lines[index].kind == DTC_LINE_HEAD) row->kind = SCENE_ROW_HEAD;
			if(lines[index].kind == DTC_LINE_CODE) row->kind = SCENE_ROW_SUB;
			append(row->text, sizeof(row->text), lines[index].text);
			append(row->detail, sizeof(row->detail), lines[index].detail);
		}
		else
		{
			append(row->text, sizeof(row->text), texts[index]);
		}
	}
}

static void build_menu(const scene_input_t *input, scene_t *scene)
{
	const nav_world_t *world = input->world;
	char brightness[SCENE_SHORT_SIZE] = "";
	const choice_t choices[] = {
		{"Fehlerspeicher", ""},
		{"Helligkeit", brightness},
		{"Nachtmodus", world->night_mode ? "an" : "aus"},
		{"Web-Zugriff", world->release_open ? "frei" : "gesperrt"},
		{"Info", ""},
		{"Einstellungen", ""},
		{"Zurück", ""},
	};

	append_percent(brightness, sizeof(brightness), world->brightness);
	set_title(scene, "Menü");
	build_rows(input, NULL, NULL, choices, COUNT(choices), scene);
}

// What the list of the own read holds: "3 Fehler in 2 Steuergeräten"
static void add_summary(scene_t *scene, const dtc_summary_t *summary)
{
	char *line = add_line(scene);

	append_number(line, SCENE_TEXT_SIZE, summary->codes);
	append(line, SCENE_TEXT_SIZE, " Fehler in ");
	append_number(line, SCENE_TEXT_SIZE, summary->ecus_with_codes);
	append(line, SCENE_TEXT_SIZE, summary->ecus_with_codes == 1 ? " Steuergerät" : " Steuergeräten");
}

static void build_dtc(const scene_input_t *input, scene_t *scene)
{
	const choice_t choices[] = {
		{"Lesen", ""},
		{"Liste ansehen", ""},
		// Not "gelöscht": the adapter can accept a clear and refuse it afterwards, at its own engine check
		{"Liste vor dem Löschen", ""},
		{"Zurück", ""},
	};

	set_title(scene, "Fehlerspeicher");
	set_note(scene, text_block(input->read_block));
	// Where the own request stands: the rows only tell what can be done
	switch(input->flow->phase)
	{
		case DTC_FLOW_READ_SENT:
		case DTC_FLOW_READING:
			add_text(scene, "Lesen läuft …");
			break;
		case DTC_FLOW_LIST:
			if(input->summary != NULL) add_summary(scene, input->summary);
			else add_text(scene, "Liste gelesen");
			break;
		case DTC_FLOW_CLEAR_SENT:
		case DTC_FLOW_CLEARING:
			add_text(scene, "Löschen läuft …");
			break;
		case DTC_FLOW_CLEARED:
			add_text(scene, "Gelöscht");
			break;
		case DTC_FLOW_FAILED:
			add_text(scene, "Letzter Auftrag fehlgeschlagen");
			break;
		case DTC_FLOW_UNKNOWN:
			add_text(scene, "Stand des Löschens unbekannt");
			break;
		// DTC_FLOW_IDLE, and what is no phase: nav.h takes that for idle as well
		default:
			add_text(scene, "Noch nicht gelesen");
			break;
	}
	build_rows(input, NULL, NULL, choices, COUNT(choices), scene);
}

static void build_busy(const scene_input_t *input, conn_view_t view, const wican_state_t *state, scene_t *scene)
{
	const dtc_flow_t *flow = input->flow;
	bool accepted = flow->phase == DTC_FLOW_READING || flow->phase == DTC_FLOW_CLEARING;
	bool reading = flow->phase == DTC_FLOW_READ_SENT || flow->phase == DTC_FLOW_READING;
	// A read waits for an adapter that is out of sight (dtc_flow.h); a clear never does
	bool paused = reading && (view == CONN_VIEW_NO_WIFI || view == CONN_VIEW_CONNECTING || view == CONN_VIEW_NO_ANSWER);

	scene->kind = SCENE_PROGRESS;
	scene->permille = 0;
	if(!reading && flow->phase != DTC_FLOW_CLEAR_SENT && flow->phase != DTC_FLOW_CLEARING)
	{
		// The request is over and nav_tick() has not left the screen yet: nothing is under way to tell of
		set_title(scene, "Fehlerspeicher");
		append(scene->big, sizeof(scene->big), "…");
		return;
	}

	set_title(scene, reading ? "Fehlerspeicher lesen" : "Fehlerspeicher löschen");
	if(paused)
	{
		// The state the display still holds is from before the pause: its step would stand there as if the scan
		// stood still, and "ca. 35 s" would promise an end nobody knows. What is known is that the connection
		// is interrupted and that the display waits for the adapter - not what the adapter does meanwhile: it
		// may scan, hold the result of a scan that is done, never have got the read, be asleep or switched
		// off. One line, the same whether the read was accepted or its POST got no answer.
		append(scene->big, sizeof(scene->big), "…");
		add_text(scene, "Verbindung unterbrochen");
		add_text(scene, "Warte auf WiCAN");
		return;
	}
	// Done counts as well: until the result is fetched the scan shows as complete, not as never begun
	if(accepted && state != NULL && state->dtc.seq == flow->seq && (state->dtc.phase == WICAN_DTC_RUNNING || state->dtc.phase == WICAN_DTC_DONE))
	{
		const wican_dtc_t *dtc = &state->dtc;

		append_number(scene->big, sizeof(scene->big), dtc->step);
		append(scene->big, sizeof(scene->big), "/");
		append_number(scene->big, sizeof(scene->big), dtc->total);
		// Before the first control unit the adapter looks at the engine speed
		if(dtc->step == 0) add_text(scene, "Prüfe Motor …");
		else dtc_short_name(dtc->name, add_line(scene), SCENE_TEXT_SIZE);
		// 64 bit: the step times 1000 does not fit into 32
		if(dtc->total != 0) scene->permille = dtc->step >= dtc->total ? 1000 : (int)((uint64_t)dtc->step * 1000u / dtc->total);
	}
	else
	{
		append(scene->big, sizeof(scene->big), "…");
		add_text(scene, "Auftrag gesendet");
	}
	add_text(scene, "ca. 35 s – Live-Werte pausieren");
}

static void build_dtc_list(const scene_input_t *input, scene_t *scene)
{
	const choice_t choices[] = {
		{"Erneut lesen", ""},
		{"Fehler löschen", ""},
		{"Zurück", ""},
	};

	set_title(scene, "Fehlerspeicher");
	if(input->world->can_clear)
	{
		set_note(scene, "Löschen möglich: ");
		append_time(scene->note, sizeof(scene->note), dtc_flow_seconds_left(input->flow, input->now_ms));
	}
	else
	{
		set_note(scene, text_block(input->clear_block));
	}
	build_rows(input, input->list, NULL, choices, COUNT(choices), scene);
}

// A question with two answers. A focus that is on neither is shown on the nearer one.
static void build_choice(const scene_input_t *input, const char *act, scene_t *scene)
{
	scene->kind = SCENE_CHOICE;
	append(scene->options[0], sizeof(scene->options[0]), "Abbrechen");
	append(scene->options[1], sizeof(scene->options[1]), act);
	scene->option = input->nav->row > 0 ? 1 : 0;
}

static void build_clear_dialog(const scene_input_t *input, scene_t *scene)
{
	set_title(scene, "Fehler löschen?");
	if(input->summary != NULL) add_summary(scene, input->summary);
	add_text(scene, "Betrifft alle Steuergeräte, auch SRS und ESP.");
	add_text(scene, "Zündung an, Motor aus, Fahrzeug steht.");
	set_note(scene, "Auf Löschen drehen, Knopf 3 s halten");
	build_choice(input, "Löschen", scene);
	scene->permille = hold_permille(input->hold, input->now_ms);
}

static void build_failed(const scene_input_t *input, const wican_state_t *state, scene_t *scene)
{
	const dtc_flow_t *flow = input->flow;
	const char *text = text_reason(flow->reason);

	scene->kind = SCENE_NOTICE;
	set_note(scene, "Knopf drücken");
	if(flow->phase == DTC_FLOW_FAILED)
	{
		// A reason this firmware has no text for is still better told than hidden
		if(text == NULL) text = flow->reason;
		// The adapter gives this reason while it starts and when it is about to sleep: its state tells which
		if(strcmp(flow->reason, "not_ready") == 0 && state != NULL && state->sleep_in_s == 0) text = "WiCAN schaltet ab – später erneut lesen";
		set_title(scene, "Fehlgeschlagen");
		add_text(scene, text);
	}
	else if(flow->phase == DTC_FLOW_UNKNOWN)
	{
		set_title(scene, "Stand unbekannt");
		add_text(scene, "Stand des Löschens unbekannt – bitte erneut lesen");
	}
	else
	{
		set_title(scene, "Fehlerspeicher");
	}
}

static void build_brightness(const scene_input_t *input, scene_t *scene)
{
	// In 64 bit: the value may be any number
	int64_t permille = (int64_t)input->nav->value * 10;

	if(permille < 0) permille = 0;
	if(permille > 1000) permille = 1000;

	scene->kind = SCENE_LEVEL;
	set_title(scene, input->world->night_mode ? "Helligkeit (Nacht)" : "Helligkeit");
	append_percent(scene->big, sizeof(scene->big), input->nav->value);
	scene->permille = (int)permille;
	set_note(scene, "Drehen zum Ändern, Drücken zum Speichern");
}

static void build_web(const scene_input_t *input, scene_t *scene)
{
	uint32_t seconds = access_seconds_left(input->access, input->now_ms);
	char release[SCENE_SHORT_SIZE] = "aus";
	const choice_t choices[] = {
		{"Freigabe", release},
		{"Zurück", ""},
	};
	bool has_address = input->address != NULL && input->address[0] != '\0';
	char *line;

	if(seconds > 0)
	{
		release[0] = '\0';
		append(release, sizeof(release), "an – noch ");
		append_time(release, sizeof(release), seconds);
	}

	set_title(scene, "Web-Zugriff");
	if(has_address) add_text(scene, input->address);
	// With the own access point a phone joins the network named below: it is not to read that there is none
	else if(!input->ap_on) add_text(scene, "Kein WLAN");
	if(input->ap_on)
	{
		// What a phone needs to join the access point
		line = add_line(scene);
		append(line, SCENE_TEXT_SIZE, "WLAN: ");
		append(line, SCENE_TEXT_SIZE, input->ap_ssid);
		line = add_line(scene);
		append(line, SCENE_TEXT_SIZE, "Passwort: ");
		append(line, SCENE_TEXT_SIZE, input->ap_password);
	}
	build_rows(input, NULL, NULL, choices, COUNT(choices), scene);
}

static void build_settings(const scene_input_t *input, scene_t *scene)
{
	const choice_t choices[] = {
		{"Drehrichtung", input->reverse ? "umgekehrt" : "normal"},
		{"Hotspot", input->ap_on ? "an" : "aus"},
		{"Neustart", ""},
		{"Vorherige Version", ""},
		{"Werkseinstellungen", ""},
		{"Zurück", ""},
	};

	set_title(scene, "Einstellungen");
	build_rows(input, NULL, NULL, choices, COUNT(choices), scene);
}

static void build_confirm(const scene_input_t *input, scene_t *scene)
{
	switch(input->nav->confirm)
	{
		case NAV_DO_REBOOT:
			set_title(scene, "Neu starten?");
			break;
		case NAV_DO_PREVIOUS_FIRMWARE:
			set_title(scene, "Vorherige Version starten?");
			break;
		case NAV_DO_FACTORY_RESET:
			set_title(scene, "Werkseinstellungen?");
			add_text(scene, "WLAN, Kopplung und Einstellungen werden gelöscht.");
			add_text(scene, "Die Ansichten bleiben.");
			break;
		// Nothing that this dialog asks for: no question that could be put in words
		default:
			break;
	}
	build_choice(input, "Ausführen", scene);
}

// What lies over the screen
static void build_overlay(const scene_input_t *input, scene_t *scene)
{
	access_ask_t asking = input->world->asking;
	int percent = input->upload_percent;
	char *line;

	switch(nav_overlay(input->world))
	{
		case NAV_OVER_UPLOAD:
			if(percent < 0) percent = 0;
			if(percent > 100) percent = 100;

			scene->over = SCENE_OVER_UPLOAD;
			add_over_text(scene, "Firmware wird übertragen");
			append_percent(add_over_line(scene), SCENE_TEXT_SIZE, percent);
			scene->over_permille = percent * 10;
			break;

		case NAV_OVER_ASK:
			scene->over = SCENE_OVER_ASK;
			// A question this firmware does not know has no words, but can be answered all the same
			add_over_text(scene, asking == ACCESS_ASK_WIFI ? "WLAN speichern?" : asking == ACCESS_ASK_FIRMWARE ? "Firmware installieren?" :
			              asking == ACCESS_ASK_RESET ? "Werkseinstellungen?" : "");
			if(input->ask_detail != NULL && input->ask_detail[0] != '\0') add_over_text(scene, input->ask_detail);
			line = add_over_line(scene);
			append(line, SCENE_TEXT_SIZE, "Drücken = ja · lang = nein (");
			append_number(line, SCENE_TEXT_SIZE, access_ask_seconds_left(input->access, input->now_ms));
			append(line, SCENE_TEXT_SIZE, " s)");
			break;

		case NAV_OVER_UPDATE:
			scene->over = SCENE_OVER_UPDATE;
			// The knob alone answers (nav.h): the screen must not invite a touch that does nothing
			add_over_text(scene, "Update in Ordnung?");
			add_over_text(scene, "Knopf drücken");
			line = add_over_line(scene);
			append(line, SCENE_TEXT_SIZE, "sonst alte Version in ");
			append_time(line, SCENE_TEXT_SIZE, input->update_left_s);
			break;

		case NAV_OVER_NONE:
			break;
	}
}

void scene_build(const scene_input_t *input, scene_t *scene)
{
	static const choice_t done[] = {{"Fertig", ""}};
	static const choice_t back[] = {{"Zurück", ""}};
	conn_view_t view = conn_view(input->conn, input->now_ms);
	const wican_state_t *state = conn_state(input->conn);
	// What the values of a page say to the ring
	int level = 0;
	bool old = false;

	// Every byte, also those between the fields: two scenes of the same screen are the same memory
	memset(scene, 0, sizeof(*scene));
	scene->dot = -1;
	scene->permille = -1;
	scene->over_permille = -1;

	switch(input->nav->screen)
	{
		case NAV_PAGES:
			build_pages(input, view, state, scene, &level, &old);
			break;
		case NAV_MENU:
			build_menu(input, scene);
			break;
		case NAV_DTC:
			build_dtc(input, scene);
			break;
		case NAV_DTC_BUSY:
			build_busy(input, view, state, scene);
			break;
		case NAV_DTC_LIST:
			build_dtc_list(input, scene);
			break;
		case NAV_DTC_CONFIRM:
			build_clear_dialog(input, scene);
			break;
		case NAV_DTC_CLEARED:
			set_title(scene, "Gelöscht");
			build_rows(input, input->cleared, NULL, done, COUNT(done), scene);
			break;
		case NAV_DTC_FAILED:
			build_failed(input, state, scene);
			break;
		case NAV_DTC_OLD:
			set_title(scene, "Vor dem Löschen");
			build_rows(input, input->old, NULL, back, COUNT(back), scene);
			break;
		case NAV_BRIGHTNESS:
			build_brightness(input, scene);
			break;
		case NAV_WEB:
			build_web(input, scene);
			break;
		case NAV_INFO:
			set_title(scene, "Info");
			build_rows(input, NULL, input->info, NULL, 0, scene);
			break;
		case NAV_SETTINGS:
			build_settings(input, scene);
			break;
		case NAV_CONFIRM:
			build_confirm(input, scene);
			break;
		// No screen of nav.h: nothing is known that could be shown
		default:
			scene->kind = SCENE_NOTICE;
			break;
	}

	scene->ring = ring_state(view, state, level, old);
	// These two screens have an arc of their own, and two arcs on one screen would be read as one
	if((input->nav->screen == NAV_DTC_BUSY || input->nav->screen == NAV_DTC_CONFIRM) && scene->ring.kind == RING_PROGRESS)
	{
		scene->ring.kind = RING_NONE;
		scene->ring.permille = 0;
	}
	build_overlay(input, scene);
}

static void put_char(writer_t *writer, char c)
{
	// What does not fit is only counted
	if(writer->length < writer->size) writer->out[writer->length] = c;
	writer->length++;
}

static void put(writer_t *writer, const char *text)
{
	for(; *text != '\0'; text++) put_char(writer, *text);
}

// A text field of a scene: it ends with its zero, or with the field if somebody left the zero out
static void put_field(writer_t *writer, const char *text, size_t size)
{
	for(size_t i = 0; i < size && text[i] != '\0'; i++) put_char(writer, text[i]);
}

// In 64 bit: the position of a dot is its index plus one
static void put_number(writer_t *writer, int64_t number)
{
	char digits[21] = "";

	append_number(digits, sizeof(digits), number);
	put(writer, digits);
}

// "name: text" and the end of the line. An empty text leaves no blank behind the colon.
static void put_text_line(writer_t *writer, const char *name, const char *text, size_t size)
{
	put(writer, name);
	if(text[0] != '\0') put_char(writer, ' ');
	put_field(writer, text, size);
	put_char(writer, '\n');
}

static void put_number_line(writer_t *writer, const char *name, int64_t number)
{
	put(writer, name);
	put_char(writer, ' ');
	put_number(writer, number);
	put_char(writer, '\n');
}

// A bool of a scene that nobody filled may hold any byte, and such a bool must not be read as one
static bool flag(const bool *field)
{
	return *(const unsigned char *)field != 0;
}

// The word for a member of an enum, "?" for what is none
static const char *word(const char *const *words, int count, unsigned member)
{
	return member < (unsigned)count ? words[member] : "?";
}

// What a count says, within the room its array has. One below 0 needs no care: nothing is counted up to it.
static int within(int count, int max)
{
	return count > max ? max : count;
}

int scene_dump(const scene_t *scene, char *out, size_t size)
{
	static const char *const kinds[] = {"values", "notice", "list", "progress", "choice", "level"};
	static const char *const rings[] = {"none", "yellow", "grey", "red", "progress"};
	static const char *const tones[] = {"normal", "dim", "warn", "alarm"};
	static const char *const widgets[] = {"number", "arc", "bar", "state"};
	static const char *const row_kinds[] = {"action", "head", "line", "sub"};
	static const char *const overs[] = {"none", "upload", "ask", "update"};
	writer_t writer = {out, size, 0};

	put(&writer, "kind: ");
	put(&writer, word(kinds, COUNT(kinds), (unsigned)scene->kind));
	put(&writer, "\nring: ");
	put(&writer, word(rings, COUNT(rings), (unsigned)scene->ring.kind));
	if(scene->ring.kind == RING_PROGRESS)
	{
		put_char(&writer, ' ');
		put_number(&writer, scene->ring.permille);
	}
	put_char(&writer, '\n');
	put_text_line(&writer, "title:", scene->title, sizeof(scene->title));
	put_text_line(&writer, "note:", scene->note, sizeof(scene->note));

	for(int i = 0; i < within(scene->item_count, LAYOUT_ITEMS_MAX); i++)
	{
		const scene_item_t *item = &scene->items[i];

		put(&writer, "item: ");
		put_field(&writer, item->label, sizeof(item->label));
		put(&writer, " | ");
		put_field(&writer, item->text, sizeof(item->text));
		put(&writer, " | ");
		put_field(&writer, item->unit, sizeof(item->unit));
		put(&writer, " | ");
		put(&writer, word(tones, COUNT(tones), (unsigned)item->tone));
		put(&writer, " | ");
		put(&writer, word(widgets, COUNT(widgets), (unsigned)item->widget));
		put(&writer, " | ");
		put_number(&writer, item->permille);
		put_char(&writer, '\n');
	}
	if(scene->dots != 0 || scene->dot != -1)
	{
		put(&writer, "dots: ");
		put_number(&writer, (int64_t)scene->dot + 1);
		put_char(&writer, '/');
		put_number(&writer, scene->dots);
		put_char(&writer, '\n');
	}

	for(int i = 0; i < within(scene->row_count, SCENE_ROWS_MAX); i++)
	{
		const scene_row_t *row = &scene->rows[i];

		put(&writer, flag(&row->focus) ? "row: > " : "row: - ");
		put(&writer, word(row_kinds, COUNT(row_kinds), (unsigned)row->kind));
		put(&writer, " | ");
		put_field(&writer, row->text, sizeof(row->text));
		put(&writer, " | ");
		put_field(&writer, row->detail, sizeof(row->detail));
		put(&writer, flag(&row->enabled) ? " | enabled\n" : " | disabled\n");
	}
	if(scene->first != 0 || scene->total != 0)
	{
		put_number_line(&writer, "first:", scene->first);
		put_number_line(&writer, "total:", scene->total);
	}

	for(int i = 0; i < within(scene->line_count, SCENE_LINES_MAX); i++) put_text_line(&writer, "line:", scene->lines[i], sizeof(scene->lines[i]));
	if(scene->big[0] != '\0') put_text_line(&writer, "big:", scene->big, sizeof(scene->big));
	if(scene->permille != -1) put_number_line(&writer, "permille:", scene->permille);
	if(scene->options[0][0] != '\0' || scene->options[1][0] != '\0')
	{
		for(int i = 0; i < 2; i++) put_text_line(&writer, scene->option == i ? "option: >" : "option: -", scene->options[i], sizeof(scene->options[i]));
	}

	if(scene->over != SCENE_OVER_NONE)
	{
		put(&writer, "over: ");
		put(&writer, word(overs, COUNT(overs), (unsigned)scene->over));
		put_char(&writer, '\n');
	}
	for(int i = 0; i < within(scene->over_line_count, COUNT(scene->over_lines)); i++)
	{
		put_text_line(&writer, "over_line:", scene->over_lines[i], sizeof(scene->over_lines[i]));
	}
	if(scene->over_permille != -1) put_number_line(&writer, "over_permille:", scene->over_permille);

	if(size == 0) return -1;
	if(writer.length >= size)
	{
		out[0] = '\0';
		return -1;
	}
	out[writer.length] = '\0';
	return (int)writer.length;
}
