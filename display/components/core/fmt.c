/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdint.h>
#include "fmt.h"

#define DECIMALS_MAX    3
#define MAGNITUDE_LIMIT 1e12

bool fmt_number(double value, int decimals, char *out, size_t size)
{
	// Twice 10^decimals: the value is counted in halves of its last decimal
	static const double scales[DECIMALS_MAX + 1] = {2, 20, 200, 2000};
	// The longest text is "-1000000000000,000": just below the limit, rounded up
	char reversed[18];
	size_t length = 0;
	double magnitude;
	uint64_t number;
	bool negative;

	if(size == 0) return false;
	out[0] = '\0';

	if(decimals < 0) decimals = 0;
	if(decimals > DECIMALS_MAX) decimals = DECIMALS_MAX;

	magnitude = value < 0 ? -value : value;
	// Written this way round it refuses NaN as well, which compares false with everything
	if(!(magnitude < MAGNITUDE_LIMIT)) return false;

	// The product is below 2e15, so its whole part is exact. The halves are rounded as whole numbers, an odd
	// count goes up. Not with doubles: (uint64_t)(product + 0.5) rounds the largest number below 0.5 up to
	// 1, and a difference "product - whole part" may be merged with the product by the compiler (fused
	// multiply-add) and then gives other digits near a half.
	number = (uint64_t)(magnitude * scales[decimals]);
	number = (number + 1) / 2;

	// A value that rounds to zero has no sign
	negative = value < 0 && number != 0;

	// From the last digit to the first
	for(int i = 0; i < decimals; i++)
	{
		reversed[length++] = (char)('0' + number % 10);
		number /= 10;
	}
	if(decimals > 0) reversed[length++] = ',';
	do
	{
		reversed[length++] = (char)('0' + number % 10);
		number /= 10;
	}
	while(number > 0);
	if(negative) reversed[length++] = '-';

	if(length + 1 > size) return false;

	for(size_t i = 0; i < length; i++) out[i] = reversed[length - 1 - i];
	out[length] = '\0';
	return true;
}

bool fmt_label(const char *name, char *out, size_t size)
{
	size_t length = 0;
	// The next byte is the first of a word
	bool first = true;

	if(size == 0) return false;

	for(; *name != '\0'; name++)
	{
		char c = *name;

		if(c == '_' || c == '@')
		{
			first = true;
			continue;
		}

		// One space between two words, however many breaks there were, and none before the first word.
		// Breaks at the end are never followed by a word and so leave nothing. There is room for the
		// space: it takes the place kept for the terminating zero.
		if(first && length > 0) out[length++] = ' ';
		if(length + 1 >= size) break;

		// Only ASCII letters: every other byte, also of a UTF-8 character, is passed on
		if(first && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
		if(!first && c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		out[length++] = c;
		first = false;
	}

	// Left early: there was no room for a byte and the terminating zero
	if(*name != '\0') length = 0;
	out[length] = '\0';
	return *name == '\0';
}
