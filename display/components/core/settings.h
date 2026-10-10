/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "json.h"

/*
 * What the user sets at the display itself or in the browser, apart from the views and the WiFi.
 * Stored on the display as the JSON text below (so a later firmware can add members):
 *
 *   {"brightness":80,"night":25,"night_mode":false,"reverse":false,"standby_s":60}
 */

#define SETTINGS_BRIGHTNESS_MIN 5       // percent; 0 would be a dark display nobody can set back
#define SETTINGS_BRIGHTNESS_MAX 100
#define SETTINGS_STANDBY_MAX    3600
#define SETTINGS_JSON_SIZE      96      // enough for settings_to_json()
#define SETTINGS_TOKENS         32      // tokens the reader needs

typedef struct
{
	uint8_t brightness;     // percent, by day
	uint8_t night;          // percent, in night mode
	bool night_mode;
	bool reverse;           // direction of the knob
	uint16_t standby_s;     // backlight off after this many seconds without input while there is nothing to
	                        // show (see settings_backlight), 0 = never. For a display on permanent power.
} settings_t;

// brightness 80, night 25, night_mode false, reverse false, standby_s 60
void settings_defaults(settings_t *settings);

// Changes *settings by the members of a JSON object; members that are missing keep their value, members
// the format does not know are ignored. Returns true if the text was taken. Returns false and changes
// nothing if the text is not a JSON object or a known member is of the wrong type or out of range:
// brightness and night integers from SETTINGS_BRIGHTNESS_MIN to SETTINGS_BRIGHTNESS_MAX, standby_s an
// integer from 0 to SETTINGS_STANDBY_MAX, night_mode and reverse booleans. `bad` (may be NULL) then
// receives the name of the first such member in the order of the text, or an empty text if the text as a
// whole is refused; it is an empty text when the text was taken. A name that does not fit into bad_size
// bytes is cut, with bad_size 0 nothing is written. Of two members with the same name the last counts, and
// both have to be of the right type and in range. An integer is a number written without fraction and
// exponent: 80.0 and 8e1 are of the wrong type.
// A text that needs more than work_count tokens is refused as a whole.
bool settings_from_json(settings_t *settings, const char *json, size_t length, char *bad, size_t bad_size,
                        json_token_t *work, int work_count);

// The text above, without whitespace, members in that order. Returns the length, -1 if it does not fit
// (out is then an empty string; with size 0 nothing is written).
int settings_to_json(const settings_t *settings, char *out, size_t size);

// The brightness the user wants right now, 0 to 100, before the limit by heat (guard.h):
//   - 0 if standby_s is not 0, `showing` is false and idle_ms is standby_s seconds or more
//   - else `night` in night mode, else `brightness`; a value below SETTINGS_BRIGHTNESS_MIN or above
//     SETTINGS_BRIGHTNESS_MAX (settings that were not read by settings_from_json) counts as that limit
// showing: the display has something to show that changes (values of a running vehicle, a fault memory
//          scan, a dialog). Without it - no WiFi, adapter asleep, ignition off - the screen goes dark
//          after the standby time, so that a display on permanent power does not light the cab all night.
// idle_ms: time since the last input at the knob or the screen, or since what the caller counts as one (app.h:
//          the end of a fault memory request of the display)
int settings_backlight(const settings_t *settings, bool showing, uint64_t idle_ms);

#endif
