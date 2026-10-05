/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <limits.h>
#include "nav.h"

// The rows of the screens, numbered as in the table of nav.h
enum
{
	MENU_DTC, MENU_BRIGHTNESS, MENU_NIGHT, MENU_WEB, MENU_INFO, MENU_SETTINGS, MENU_BACK,
};

enum
{
	DTC_READ, DTC_VIEW, DTC_OLD, DTC_BACK,
};

enum
{
	SETTINGS_REVERSE, SETTINGS_AP, SETTINGS_REBOOT, SETTINGS_PREVIOUS, SETTINGS_RESET, SETTINGS_BACK,
};

// Both dialogs: the focus starts on the answer that does nothing
enum
{
	CHOICE_CANCEL, CHOICE_ACT, CHOICE_ROWS,
};

// NAV_DTC_LIST: the rows behind the lines of the list
enum
{
	LIST_READ, LIST_CLEAR, LIST_BACK, LIST_ROWS,
};

#define WEB_RELEASE     0
#define END_ROWS        1   // NAV_DTC_CLEARED: Fertig, NAV_DTC_OLD: Zurück

// More lines than this would leave the rows behind them without a number
#define LINES_MAX       (INT_MAX - LIST_ROWS)

// The time of the module: it follows the caller, but a clock that steps back lets no time pass. With the
// time of the caller a step backwards would turn "since the last input" into a huge time.
static void advance(nav_t *nav, uint64_t now_ms)
{
	if(now_ms > nav->clock_ms) nav->clock_ms = now_ms;
}

// Every input restarts the idle time, also one that is ignored: somebody is at the display
static void note_input(nav_t *nav, uint64_t now_ms)
{
	advance(nav, now_ms);
	nav->last_input_ms = nav->clock_ms;
}

static void enter(nav_t *nav, nav_screen_t screen, int row)
{
	nav->screen = screen;
	nav->row = row;
	// Nothing waits to be carried out but in the dialog of the settings, which sets it after this
	nav->confirm = NAV_DO_NOTHING;
}

static int lines_of(int lines)
{
	if(lines < 0) return 0;
	return lines > LINES_MAX ? LINES_MAX : lines;
}

// A brightness that can be stored: below the lower limit nobody could read the display to set it back
static int brightness_of(int64_t value)
{
	if(value < SETTINGS_BRIGHTNESS_MIN) return SETTINGS_BRIGHTNESS_MIN;
	if(value > SETTINGS_BRIGHTNESS_MAX) return SETTINGS_BRIGHTNESS_MAX;
	return (int)value;
}

static bool under_way(dtc_flow_phase_t flow)
{
	return flow == DTC_FLOW_READ_SENT || flow == DTC_FLOW_READING || flow == DTC_FLOW_CLEAR_SENT || flow == DTC_FLOW_CLEARING;
}

// The screen for what the own request left behind: its list, the outcome of its clear or its failure.
// NAV_DTC if there is nothing to look at.
static nav_screen_t outcome_screen(dtc_flow_phase_t flow)
{
	switch(flow)
	{
		case DTC_FLOW_LIST:     return NAV_DTC_LIST;
		case DTC_FLOW_CLEARED:  return NAV_DTC_CLEARED;
		case DTC_FLOW_FAILED:
		case DTC_FLOW_UNKNOWN:  return NAV_DTC_FAILED;
		default:                return NAV_DTC;
	}
}

static bool page_shown(const nav_t *nav, const nav_world_t *world)
{
	return layout_page_shown(world->layout, nav->page, world->catalog);
}

// The shown page nearest to the one that is not shown any more, the one behind it if two are equally near.
// -1 if the layout shows none.
static int nearest_page(const nav_t *nav, const nav_world_t *world)
{
	for(int distance = 1; distance <= LAYOUT_PAGES_MAX; distance++)
	{
		if(layout_page_shown(world->layout, nav->page + distance, world->catalog)) return nav->page + distance;
		if(layout_page_shown(world->layout, nav->page - distance, world->catalog)) return nav->page - distance;
	}
	return -1;
}

// A page that is not shown any more has no neighbours to count detents from: the turn only finds a page again
static void turn_pages(nav_t *nav, int detents, const nav_world_t *world)
{
	int direction = detents < 0 ? -1 : 1;

	if(!page_shown(nav, world))
	{
		nav->page = nearest_page(nav, world);
		return;
	}
	for(; detents != 0; detents -= direction)
	{
		int next = layout_step_page(world->layout, world->catalog, nav->page, direction);

		// The hard end. The detents that are left change nothing, and there may be billions of them.
		if(next == nav->page) break;
		nav->page = next;
	}
}

// In 64 bit: the focus plus any number of detents
static void move_focus(nav_t *nav, int detents, const nav_world_t *world)
{
	int64_t row = (int64_t)nav->row + detents;
	int last = nav_rows(nav, world) - 1;

	if(row > last) row = last;
	if(row < 0) row = 0;
	nav->row = (int)row;
}

// "Lesen" and "Erneut lesen"
static nav_do_t start_read(nav_t *nav, const nav_world_t *world)
{
	if(!world->can_read) return NAV_DO_NOTHING;

	enter(nav, NAV_DTC_BUSY, 0);
	return NAV_DO_READ;
}

// From the clear dialog back to the row it was opened with
static void close_dialog(nav_t *nav, const nav_world_t *world)
{
	enter(nav, NAV_DTC_LIST, lines_of(world->list_lines) + LIST_CLEAR);
}

// The dialog of the settings. Not while the own request is under way: a restart then would leave a read
// without its list and a clear without its outcome.
static void ask(nav_t *nav, nav_do_t action, const nav_world_t *world)
{
	if(under_way(world->flow)) return;

	enter(nav, NAV_CONFIRM, CHOICE_CANCEL);
	nav->confirm = action;
}

// One level back: the long press, and the rows "Zurück" and "Abbrechen" of the screens that have one
static nav_do_t back(nav_t *nav, const nav_world_t *world)
{
	switch(nav->screen)
	{
		case NAV_PAGES:
			nav->page = layout_first_page(world->layout, world->catalog);
			break;
		case NAV_MENU:
		case NAV_DTC_BUSY:
			// A request that is under way goes on, the ring shows its progress
			enter(nav, NAV_PAGES, 0);
			break;
		case NAV_DTC:
			enter(nav, NAV_MENU, MENU_DTC);
			break;
		case NAV_DTC_LIST:
		case NAV_DTC_CLEARED:
		case NAV_DTC_FAILED:
		case NAV_DTC_OLD:
			// Without a dismiss: the outcome stays to be looked at again
			enter(nav, NAV_DTC, 0);
			break;
		case NAV_DTC_CONFIRM:
			// Keeping the knob pressed is how clearing is confirmed here, and that is a long press as well
			break;
		case NAV_BRIGHTNESS:
			enter(nav, NAV_MENU, MENU_BRIGHTNESS);
			return NAV_DO_SETTINGS_STORE;
		case NAV_WEB:
			enter(nav, NAV_MENU, MENU_WEB);
			break;
		case NAV_INFO:
			enter(nav, NAV_MENU, MENU_INFO);
			break;
		case NAV_SETTINGS:
			enter(nav, NAV_MENU, MENU_SETTINGS);
			break;
		case NAV_CONFIRM:
			// To the row the dialog was opened with
			enter(nav, NAV_SETTINGS, nav->confirm == NAV_DO_REBOOT ? SETTINGS_REBOOT :
			      nav->confirm == NAV_DO_PREVIOUS_FIRMWARE ? SETTINGS_PREVIOUS : SETTINGS_RESET);
			break;
	}
	return NAV_DO_NOTHING;
}

static nav_do_t press_menu(nav_t *nav, const nav_world_t *world)
{
	switch(nav->row)
	{
		case MENU_DTC:
			enter(nav, under_way(world->flow) ? NAV_DTC_BUSY : NAV_DTC, 0);
			break;
		case MENU_BRIGHTNESS:
			enter(nav, NAV_BRIGHTNESS, 0);
			nav->value = brightness_of(world->brightness);
			break;
		case MENU_NIGHT:
			return NAV_DO_NIGHT_TOGGLE;
		case MENU_WEB:
			enter(nav, NAV_WEB, 0);
			break;
		case MENU_INFO:
			enter(nav, NAV_INFO, 0);
			break;
		case MENU_SETTINGS:
			enter(nav, NAV_SETTINGS, 0);
			break;
		case MENU_BACK:
			return back(nav, world);
	}
	return NAV_DO_NOTHING;
}

static nav_do_t press_dtc(nav_t *nav, const nav_world_t *world)
{
	nav_screen_t outcome = outcome_screen(world->flow);

	switch(nav->row)
	{
		case DTC_READ:
			return start_read(nav, world);
		case DTC_VIEW:
			if(outcome != NAV_DTC) enter(nav, outcome, 0);
			break;
		case DTC_OLD:
			if(world->old_lines > 0) enter(nav, NAV_DTC_OLD, 0);
			break;
		case DTC_BACK:
			return back(nav, world);
	}
	return NAV_DO_NOTHING;
}

static nav_do_t press_list(nav_t *nav, const nav_world_t *world)
{
	// Counted from the first row behind the lines; negative on a line
	switch(nav->row - lines_of(world->list_lines))
	{
		case LIST_READ:
			return start_read(nav, world);
		case LIST_CLEAR:
			if(!world->can_clear) break;
			enter(nav, NAV_DTC_CONFIRM, CHOICE_CANCEL);
			return NAV_DO_HOLD_OPEN;
		case LIST_BACK:
			return back(nav, world);
	}
	return NAV_DO_NOTHING;
}

static nav_do_t press_settings(nav_t *nav, const nav_world_t *world)
{
	switch(nav->row)
	{
		case SETTINGS_REVERSE:
			return NAV_DO_REVERSE_TOGGLE;
		case SETTINGS_AP:
			// An access point that stays on whatever is asked (link.h) has nothing to switch
			if(!world->ap_kept) return NAV_DO_AP_TOGGLE;
			break;
		case SETTINGS_REBOOT:
			ask(nav, NAV_DO_REBOOT, world);
			break;
		case SETTINGS_PREVIOUS:
			if(world->previous_firmware) ask(nav, NAV_DO_PREVIOUS_FIRMWARE, world);
			break;
		case SETTINGS_RESET:
			ask(nav, NAV_DO_FACTORY_RESET, world);
			break;
		case SETTINGS_BACK:
			return back(nav, world);
	}
	return NAV_DO_NOTHING;
}

// A short press on the screen itself, with nothing lying over it
static nav_do_t press(nav_t *nav, const nav_world_t *world)
{
	nav_do_t asked = nav->confirm;

	switch(nav->screen)
	{
		case NAV_PAGES:
			enter(nav, NAV_MENU, MENU_DTC);
			break;
		case NAV_MENU:
			return press_menu(nav, world);
		case NAV_DTC:
			return press_dtc(nav, world);
		case NAV_DTC_BUSY:
			// A scan cannot be cancelled
			break;
		case NAV_DTC_LIST:
			return press_list(nav, world);
		case NAV_DTC_CONFIRM:
			// "Löschen" is confirmed by the hold alone (nav_hold): a press there is its beginning at most
			if(nav->row != CHOICE_CANCEL) break;
			close_dialog(nav, world);
			return NAV_DO_HOLD_CLOSE;
		case NAV_DTC_CLEARED:
			if(nav->row != lines_of(world->cleared_lines)) break;
			enter(nav, NAV_DTC, 0);
			return NAV_DO_DISMISS;
		case NAV_DTC_FAILED:
			enter(nav, NAV_DTC, 0);
			return NAV_DO_DISMISS;
		case NAV_DTC_OLD:
			if(nav->row == lines_of(world->old_lines)) return back(nav, world);
			break;
		case NAV_BRIGHTNESS:
		case NAV_INFO:
			return back(nav, world);
		case NAV_WEB:
			if(nav->row != WEB_RELEASE) return back(nav, world);
			return world->release_open ? NAV_DO_RELEASE_OFF : NAV_DO_RELEASE_ON;
		case NAV_SETTINGS:
			return press_settings(nav, world);
		case NAV_CONFIRM:
			if(nav->row == CHOICE_CANCEL) return back(nav, world);
			enter(nav, NAV_PAGES, 0);
			return asked;
	}
	return NAV_DO_NOTHING;
}

// A short press on what lies over the screen. The knob alone answers there: nav_tap() never comes here.
static nav_do_t press_overlay(nav_overlay_t overlay)
{
	if(overlay == NAV_OVER_ASK) return NAV_DO_ASK_CONFIRM;
	if(overlay == NAV_OVER_UPDATE) return NAV_DO_UPDATE_OK;
	return NAV_DO_NOTHING;
}

void nav_init(nav_t *nav, const nav_world_t *world, uint64_t now_ms)
{
	nav->screen = NAV_PAGES;
	nav->page = layout_first_page(world->layout, world->catalog);
	nav->row = 0;
	nav->value = 0;
	nav->confirm = NAV_DO_NOTHING;
	nav->last_input_ms = now_ms;
	nav->clock_ms = now_ms;
}

nav_overlay_t nav_overlay(const nav_world_t *world)
{
	if(world->uploading) return NAV_OVER_UPLOAD;
	if(world->asking != ACCESS_ASK_NONE) return NAV_OVER_ASK;
	if(world->update_pending) return NAV_OVER_UPDATE;
	return NAV_OVER_NONE;
}

int nav_rows(const nav_t *nav, const nav_world_t *world)
{
	switch(nav->screen)
	{
		case NAV_MENU:          return NAV_MENU_ROWS;
		case NAV_DTC:           return NAV_DTC_ROWS;
		case NAV_DTC_LIST:      return lines_of(world->list_lines) + LIST_ROWS;
		case NAV_DTC_CLEARED:   return lines_of(world->cleared_lines) + END_ROWS;
		case NAV_DTC_OLD:       return lines_of(world->old_lines) + END_ROWS;
		case NAV_WEB:           return NAV_WEB_ROWS;
		case NAV_INFO:          return lines_of(world->info_lines);
		case NAV_SETTINGS:      return NAV_SETTINGS_ROWS;
		case NAV_DTC_CONFIRM:
		case NAV_CONFIRM:       return CHOICE_ROWS;
		// The value pages, the progress, the failure and the brightness have none
		default:                return 0;
	}
}

bool nav_row_acts(const nav_t *nav, int row, const nav_world_t *world)
{
	// The rules of a press are asked themselves: a list of them kept for this question could say something else
	nav_t tried = *nav;

	if(row < 0 || row >= nav_rows(nav, world)) return false;

	tried.row = row;
	return press(&tried, world) != NAV_DO_NOTHING || tried.screen != nav->screen;
}

nav_do_t nav_turn(nav_t *nav, int detents, const nav_world_t *world, uint64_t now_ms)
{
	note_input(nav, now_ms);
	if(nav_overlay(world) != NAV_OVER_NONE) return NAV_DO_NOTHING;

	if(nav->screen == NAV_PAGES)
	{
		turn_pages(nav, detents, world);
		return NAV_DO_NOTHING;
	}
	if(nav->screen == NAV_BRIGHTNESS)
	{
		// In 64 bit: any number of detents
		nav->value = brightness_of(nav->value + (int64_t)detents * NAV_BRIGHTNESS_STEP);
		return NAV_DO_BRIGHTNESS;
	}
	move_focus(nav, detents, world);
	return NAV_DO_NOTHING;
}

nav_do_t nav_short(nav_t *nav, const nav_world_t *world, uint64_t now_ms)
{
	nav_overlay_t overlay = nav_overlay(world);

	note_input(nav, now_ms);
	if(overlay != NAV_OVER_NONE) return press_overlay(overlay);
	return press(nav, world);
}

nav_do_t nav_long(nav_t *nav, const nav_world_t *world, uint64_t now_ms)
{
	nav_overlay_t overlay = nav_overlay(world);

	note_input(nav, now_ms);
	if(overlay == NAV_OVER_ASK) return NAV_DO_ASK_REFUSE;
	if(overlay != NAV_OVER_NONE) return NAV_DO_NOTHING;
	return back(nav, world);
}

nav_do_t nav_tap(nav_t *nav, int row, const nav_world_t *world, uint64_t now_ms)
{
	note_input(nav, now_ms);
	// What lies over the screen is answered with the knob alone, the questions as well: a touch happens too
	// easily for what they ask, and none of them has a row a finger could mean
	if(nav_overlay(world) != NAV_OVER_NONE) return NAV_DO_NOTHING;

	// The failure has no rows, and a touch anywhere acknowledges it
	if(nav->screen != NAV_DTC_FAILED)
	{
		// A screen without rows has no row that exists
		if(row < 0 || row >= nav_rows(nav, world)) return NAV_DO_NOTHING;
		// What the two dialogs ask for is confirmed with the knob alone, and the knob alone moves their focus:
		// a touch happens too easily
		if((nav->screen == NAV_DTC_CONFIRM || nav->screen == NAV_CONFIRM) && row != CHOICE_CANCEL) return NAV_DO_NOTHING;
		nav->row = row;
	}
	return press(nav, world);
}

nav_do_t nav_swipe(nav_t *nav, int direction, const nav_world_t *world, uint64_t now_ms)
{
	note_input(nav, now_ms);
	if(nav->screen != NAV_PAGES || direction == 0 || nav_overlay(world) != NAV_OVER_NONE) return NAV_DO_NOTHING;

	turn_pages(nav, direction < 0 ? -1 : 1, world);
	return NAV_DO_NOTHING;
}

nav_do_t nav_hold(nav_t *nav, hold_event_t event, const nav_world_t *world, uint64_t now_ms)
{
	advance(nav, now_ms);
	if(nav->screen != NAV_DTC_CONFIRM) return NAV_DO_NOTHING;

	// The caller keeps a hold from completing under an overlay. One that is reported all the same was not
	// seen by the user and clears nothing.
	if(event == HOLD_CONFIRMED && nav_overlay(world) == NAV_OVER_NONE)
	{
		enter(nav, NAV_DTC_BUSY, 0);
		return NAV_DO_CLEAR;
	}
	// The dialog of hold.h is closed after each of these
	if(event == HOLD_CONFIRMED || event == HOLD_CANCELLED || event == HOLD_STUCK) close_dialog(nav, world);
	return NAV_DO_NOTHING;
}

nav_do_t nav_cancel(nav_t *nav, const nav_world_t *world, uint64_t now_ms)
{
	// No input: nobody is at a screen that cannot be seen
	advance(nav, now_ms);

	if(nav->screen == NAV_DTC_CONFIRM)
	{
		close_dialog(nav, world);
		return NAV_DO_HOLD_CLOSE;
	}
	if(nav->screen == NAV_CONFIRM) return back(nav, world);
	return NAV_DO_NOTHING;
}

nav_do_t nav_tick(nav_t *nav, const nav_world_t *world, uint64_t now_ms)
{
	nav_screen_t screen = nav->screen;
	nav_screen_t outcome = outcome_screen(world->flow);
	int last;

	advance(nav, now_ms);

	if(screen == NAV_DTC_CONFIRM && (world->flow != DTC_FLOW_LIST || !world->can_clear))
	{
		if(world->flow == DTC_FLOW_LIST) close_dialog(nav, world);
		else enter(nav, NAV_DTC, 0);
		return NAV_DO_HOLD_CLOSE;
	}
	if(screen == NAV_DTC_BUSY && !under_way(world->flow))
	{
		enter(nav, outcome, 0);
		return NAV_DO_NOTHING;
	}
	// What a screen showed is gone
	if(((screen == NAV_DTC_LIST || screen == NAV_DTC_CLEARED || screen == NAV_DTC_FAILED) && screen != outcome) ||
	   (screen == NAV_DTC_OLD && world->old_lines <= 0))
	{
		enter(nav, NAV_DTC, 0);
		return NAV_DO_NOTHING;
	}
	if(screen == NAV_PAGES && !page_shown(nav, world))
	{
		nav->page = nearest_page(nav, world);
		return NAV_DO_NOTHING;
	}

	last = nav_rows(nav, world) - 1;
	if(last < 0) last = 0;
	if(nav->row > last)
	{
		nav->row = last;
		return NAV_DO_NOTHING;
	}

	// The value pages are where this leads, and the two screens with a request of their own end by
	// themselves: the progress with the request, the clear dialog with the idle time of hold.h
	if(nav->clock_ms - nav->last_input_ms < NAV_IDLE_MS || nav_overlay(world) != NAV_OVER_NONE) return NAV_DO_NOTHING;
	if(screen == NAV_DTC_BUSY || screen == NAV_DTC_CONFIRM) return NAV_DO_NOTHING;

	// The own request and its outcome are kept: no dismiss
	enter(nav, NAV_PAGES, 0);
	return screen == NAV_BRIGHTNESS ? NAV_DO_SETTINGS_STORE : NAV_DO_NOTHING;
}
