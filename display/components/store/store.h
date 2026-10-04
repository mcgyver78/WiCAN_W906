/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __STORE_H__
#define __STORE_H__

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

/*
 * What the display keeps over a restart, in the NVS partition. A firmware update leaves it alone.
 *
 *   STORE_CFG   what a factory reset erases: "settings" (JSON, settings.h), "wifi" (net_profile_t list),
 *               "bound" (id of the adapter), "ap_pass" (password of the own access point)
 *   STORE_DATA  what a factory reset keeps: "layout" and "layout_prev" (JSON, layout.h), "catalog" (JSON,
 *               catalog.h), "dtc_list" and "dtc_old" (result texts of the adapter)
 *
 * A write to the flash stops the picture for a moment and wears the flash: callers write only when the
 * user stores something, or by the rules of guard.h. A value that is stored already is not written again
 * (NVS compares it).
 */

typedef enum
{
	STORE_CFG,
	STORE_DATA,
} store_space_t;

#define STORE_KEY_SETTINGS      "settings"
#define STORE_KEY_WIFI          "wifi"
#define STORE_KEY_BOUND         "bound"
#define STORE_KEY_AP_PASSWORD   "ap_pass"
#define STORE_KEY_LAYOUT        "layout"
#define STORE_KEY_LAYOUT_PREV   "layout_prev"
#define STORE_KEY_CATALOG       "catalog"
#define STORE_KEY_DTC_LIST      "dtc_list"
#define STORE_KEY_DTC_OLD       "dtc_old"

// Has to succeed before anything else. A partition that cannot be read (no free page, or written by a
// newer NVS version) is erased and started empty: the display then comes up with its defaults.
esp_err_t store_init(void);

// Reads a value. Returns its length in bytes, or -1 if there is none, it does not fit into `size`, or the
// read failed (out is then untouched).
int store_read(store_space_t space, const char *key, void *out, size_t size);

// Reads a text: as store_read, but the text needs size - 1 bytes at most and is zero terminated.
// Returns the length without the zero, -1 as above (out is then an empty text if size is not 0).
int store_read_text(store_space_t space, const char *key, char *out, size_t size);

// Writes a value and commits it. Returns false if that failed (the old value then stays).
bool store_write(store_space_t space, const char *key, const void *data, size_t length);

// Removes a value. Returns true if it is gone afterwards (also if there was none).
bool store_erase(store_space_t space, const char *key);

// Removes every value of a space (the factory reset erases STORE_CFG)
bool store_erase_space(store_space_t space);

#endif
