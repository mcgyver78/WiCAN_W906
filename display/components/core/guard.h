/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __GUARD_H__
#define __GUARD_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * What protects the display from itself: the safe mode after repeated crashes, the way back from a
 * layout that crashes it, dimming when the board gets too hot, and when the catalogue may be written to
 * the flash. Pure rules; the caller reads the reset reason and the temperature and does what is decided.
 */

/*
 * Safe mode. The counter lives in memory that survives a restart but not a loss of power (RTC memory), so
 * nothing is written to the flash. In safe mode the display uses the built-in views instead of the stored
 * ones and opens its own access point: a stored layout or WiFi setting that crashes the firmware locks
 * nobody out. (The rollback of the boot loader does not help there: the image itself is valid.)
 */

#define GUARD_MAGIC             0x57444731u     // "WDG1": the memory holds a counter of this firmware
#define GUARD_CRASHES           3               // crashes in a row, each within GUARD_ALIVE_MS after its start
#define GUARD_ALIVE_MS          60000u

typedef enum
{
	GUARD_RESET_POWER_ON,   // power came on, or anything else that leaves the RTC memory undefined
	GUARD_RESET_SOFTWARE,   // restart asked for by the firmware itself (menu, web, update)
	GUARD_RESET_CRASH,      // panic, watchdog, brown-out
} guard_reset_t;

typedef struct
{
	uint32_t magic;
	uint32_t crashes;       // starts in a row that ended in a crash before guard_alive()
	uint32_t layout_fresh;  // GUARD_MAGIC while a layout was stored less than GUARD_ALIVE_MS before
	uint32_t check;         // magic ^ crashes ^ layout_fresh ^ 0xFFFFFFFF: a damaged counter counts as none
} guard_memory_t;

typedef struct
{
	bool safe_mode;         // built-in views, access point on
	bool previous_layout;   // start with the layout stored before the last one (see guard_layout_stored)
} guard_start_t;

// Called once at the start, before the stored layout is read.
// - memory without the magic or with a wrong check, or GUARD_RESET_POWER_ON: the counter starts at 0 and
//   there is no layout mark
// - GUARD_RESET_CRASH: one more crash (the counter stops at its largest value). GUARD_RESET_SOFTWARE: the
//   counter and the layout mark stay. A reason that is no value of the enum counts as GUARD_RESET_CRASH.
// - safe_mode if the counter reached GUARD_CRASHES, or if the knob is held (knob_held: the switch read
//   pressed in every one of several successful readings during the start; the caller decides that)
// - previous_layout if this start follows a crash and a layout was stored shortly before it; that mark is
//   taken away by this call, so the next start uses what is stored then
// The memory is left valid (magic and check set, layout_fresh GUARD_MAGIC or 0).
guard_start_t guard_start(guard_memory_t *memory, guard_reset_t reset, bool knob_held);

// The firmware has run for GUARD_ALIVE_MS: the crash counter and the layout mark go back to 0.
// The caller calls it once, when the time is reached. The memory is left valid, whatever it held.
void guard_alive(guard_memory_t *memory);

// A layout was stored just now: if the display crashes before guard_alive() is called again, the next start
// uses the layout stored before it. The caller calls guard_alive() GUARD_ALIVE_MS after this, and not
// before: a call that was still due since the start takes the mark away as well, so it has to wait until
// then. The crash counter stays (a damaged one counts as 0); the memory is left valid.
void guard_layout_stored(guard_memory_t *memory);

/*
 * Heat. The maker allows 65 degrees C around the board; the chip is warmer than its surroundings. The
 * limits below are assumptions until they are measured on the board.
 */

#define GUARD_TEMP_DIM_C        75      // from here the backlight is limited to GUARD_DIM_PERCENT
#define GUARD_TEMP_OFF_C        85      // from here it is switched off
#define GUARD_TEMP_BACK_C       5       // a limit is lifted again this many degrees below where it began
#define GUARD_DIM_PERCENT       30

typedef enum
{
	GUARD_HEAT_NORMAL,
	GUARD_HEAT_DIM,
	GUARD_HEAT_OFF,
} guard_heat_t;

// The level after a new reading of the chip temperature, given the level before. Rising: DIM from
// GUARD_TEMP_DIM_C, OFF from GUARD_TEMP_OFF_C. Falling: OFF until below GUARD_TEMP_OFF_C -
// GUARD_TEMP_BACK_C, DIM until below GUARD_TEMP_DIM_C - GUARD_TEMP_BACK_C. A reading that failed
// (valid false) keeps the level. A level that is no value of the enum counts as GUARD_HEAT_OFF.
guard_heat_t guard_heat(guard_heat_t before, int temp_c, bool valid);

// The brightness to set, 0 to 100: `wanted` (clamped to 0..100) limited by the level: at most
// GUARD_DIM_PERCENT for GUARD_HEAT_DIM, 0 for GUARD_HEAT_OFF and for a level that is no value of the enum
int guard_brightness(guard_heat_t heat, int wanted);

/*
 * Writing the catalogue to the flash. A write to the flash can disturb the picture, and the flash wears.
 * The catalogue changes while a connection settles (the profile arrives, then the first values), so it is
 * stored only when it has come to rest, differs from what is stored, and at most once per connection.
 */

#define GUARD_CATALOG_REST_MS   30000u  // the check sum has to stand this long

typedef struct
{
	uint32_t stored_sum;        // check sum of the catalogue in the flash
	bool has_stored;
	uint32_t seen_sum;
	uint64_t seen_since_ms;
	bool has_seen;
	bool written;               // stored once on this connection
} guard_catalog_t;

// stored_sum: check sum of the catalogue read from the flash at the start; has_stored false if there was none
// (stored_sum is not looked at then). One write is allowed, as after guard_catalog_connected().
void guard_catalog_init(guard_catalog_t *guard, bool has_stored, uint32_t stored_sum);

// A new connection to the adapter began (conn_take_restarted, or WiFi came up): one write is allowed again
// and the rest time starts anew
void guard_catalog_connected(guard_catalog_t *guard);

// Called every round with the check sum of the catalogue in use and whether it is complete (the profile was
// loaded on this connection). Returns true exactly when the catalogue has to be stored now: it is
// complete, its check sum stands since GUARD_CATALOG_REST_MS, differs from the stored one (or none is
// stored), and nothing was written on this connection yet. The caller stores it; this call already counts
// it as stored. A time before seen_since_ms counts as no time passed.
// The check sum is followed while the catalogue is incomplete as well: the rest time can be over at the
// moment the catalogue becomes complete.
bool guard_catalog_due(guard_catalog_t *guard, uint32_t sum, bool complete, uint64_t now_ms);

#endif
