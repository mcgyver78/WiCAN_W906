/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __FMT_H__
#define __FMT_H__

#include <stddef.h>
#include <stdbool.h>

/*
 * Texts for numbers and names on the display. German conventions: decimal comma, no thousands separator.
 */

// value rounded to `decimals` places (0 to 3; other values count as 0 or 3), half away from zero, with a
// decimal comma: 12.345 with 2 -> "12,35", -0.04 with 1 -> "0,0" (never "-0"), 1234567 with 0 -> "1234567".
// Rounded is the product of the value and 10^decimals as a double. So 443.65 with 1 -> "443,7" as the number
// reads, although the double nearest to 443.65 lies just below it. That holds for most such numbers, not for
// all: 1.005 with 2 -> "1,00".
// Returns false and an empty string if the value is not finite, its magnitude is 1e12 or more, or the text
// does not fit. The limit is that of the value, not of the rounded number: 999999999999.6 with 0 ->
// "1000000000000".
bool fmt_number(double value, int decimals, char *out, size_t size);

// A readable label for a value name without one: "ENGINE_OIL_TEMP" -> "Engine Oil Temp", "DPF_KM_SINCE_REGEN"
// -> "Dpf Km Since Regen", "@BATT_V" -> "Batt V". Underscores and '@' become word breaks, several in a row
// one space, none at the ends; the first letter of a word upper case, the rest lower case; other bytes are
// passed on. A word that starts with something else has no upper case letter: "2ND_GEAR" -> "2nd Gear".
// Returns false and an empty string if it does not fit.
bool fmt_label(const char *name, char *out, size_t size);

#endif
