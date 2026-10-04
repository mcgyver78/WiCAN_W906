/*
 * Host test for display/components/core/fmt.c. Run "make test_fmt && ./test_fmt" in display/test.
 * redproof.py removes or weakens every rule once (mutations/fmt.py) and expects this test to fail.
 */
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include "test.h"
#include "fmt.h"

#define GUARD   0x5A
#define BEFORE  4       // guard bytes in front of a buffer
#define ROOM    64      // largest buffer tried, behind it more guard bytes
#define BEHIND  8

static uint32_t random_state = 20261003;

static uint32_t random_next(void)
{
	random_state = random_state * 1664525u + 1013904223u;
	return random_state >> 8;
}

static bool number_is(double value, int decimals, const char *expected)
{
	char out[ROOM];

	memset(out, GUARD, sizeof(out));
	if(!fmt_number(value, decimals, out, sizeof(out)) || strcmp(out, expected) != 0)
	{
		out[sizeof(out) - 1] = '\0';
		printf("  %.17g with %d: expected \"%s\", got \"%s\"\n", value, decimals, expected, out);
		return false;
	}
	return true;
}

static bool number_refused(double value, int decimals)
{
	char out[ROOM];

	memset(out, GUARD, sizeof(out));
	return !fmt_number(value, decimals, out, sizeof(out)) && out[0] == '\0';
}

static bool label_is(const char *name, const char *expected)
{
	char out[ROOM];

	memset(out, GUARD, sizeof(out));
	if(!fmt_label(name, out, sizeof(out)) || strcmp(out, expected) != 0)
	{
		out[sizeof(out) - 1] = '\0';
		printf("  \"%s\": expected \"%s\", got \"%s\"\n", name, expected, out);
		return false;
	}
	return true;
}

// A size around the one a text of `length` bytes needs, and sometimes any size up to ROOM
static size_t random_size(size_t length)
{
	uint32_t pick = random_next() % 8;

	if(pick >= 6) return random_next() % (ROOM + 1);
	return length + pick > 2 ? length + pick - 2 : 0;
}

// What a buffer of `size` bytes has to hold after a call that returned `result`, if the full text is
// `expected`: the text if it fits with its zero, an empty text if not, nothing at all without room, and
// never a byte outside. `buffer` has BEFORE guard bytes, then the buffer, then guard bytes up to its end.
static bool sized_right(const unsigned char *buffer, size_t size, bool result, const char *expected)
{
	const char *out = (const char *)buffer + BEFORE;
	bool fits = strlen(expected) + 1 <= size;
	size_t k;

	for(k = 0; k < BEFORE; k++) if(buffer[k] != GUARD) return false;
	for(k = BEFORE + size; k < BEFORE + ROOM + BEHIND; k++) if(buffer[k] != GUARD) return false;
	if(result != fits) return false;
	if(fits) return strcmp(out, expected) == 0;
	return size == 0 || out[0] == '\0';
}

static void test_number_examples(void)
{
	check(number_is(12.345, 2, "12,35"), "12.345 with 2 decimals is 12,35");
	check(number_is(-0.04, 1, "0,0"), "-0.04 with 1 decimal is 0,0, never -0,0");
	check(number_is(1234567, 0, "1234567"), "1234567 with 0 decimals has no thousands separator");

	check(number_is(0, 0, "0"), "zero without decimals");
	check(number_is(0, 1, "0,0") && number_is(0, 3, "0,000"), "zero with decimals");
	check(number_is(-0.0, 2, "0,00"), "minus zero has no sign");
	check(number_is(5, 3, "5,000"), "a whole number gets all its decimals");
	check(number_is(0.007, 3, "0,007") && number_is(12.003, 3, "12,003") && number_is(1.05, 2, "1,05"),
	      "zeros between the comma and the first decimal are kept");
	check(number_is(0.3, 1, "0,3") && number_is(0.25, 2, "0,25"), "a zero stands in front of the comma");
	check(number_is(7, 0, "7") && number_is(42.4, 0, "42"), "no comma without decimals");
	check(number_is(1234567.891, 2, "1234567,89"), "no thousands separator in front of decimals");
	check(number_is(1000, 0, "1000") && number_is(10, 1, "10,0") && number_is(100.5, 1, "100,5"), "zeros inside a number are kept");
	check(number_is(-12.5, 1, "-12,5") && number_is(-1, 0, "-1") && number_is(-1234.5678, 3, "-1234,568"), "negative numbers carry a minus");
	check(number_is(67.31, 2, "67,31") && number_is(1008.83, 1, "1008,8") && number_is(21.25, 2, "21,25") && number_is(187432, 0, "187432"),
	      "values as the adapter sends them");
}

static void test_number_rounding(void)
{
	check(number_is(0.5, 0, "1"), "0.5 rounds away from zero to 1");
	check(number_is(1.5, 0, "2"), "1.5 rounds to 2");
	check(number_is(2.5, 0, "3"), "2.5 rounds to 3, not to the even 2");
	check(number_is(-0.5, 0, "-1"), "-0.5 rounds away from zero to -1");
	check(number_is(-1.5, 0, "-2") && number_is(-2.5, 0, "-3"), "-1.5 and -2.5 round away from zero");
	check(number_is(0.25, 1, "0,3") && number_is(0.75, 1, "0,8") && number_is(-0.25, 1, "-0,3"), "half of the first decimal rounds away from zero");
	check(number_is(0.125, 2, "0,13") && number_is(0.375, 2, "0,38") && number_is(-0.625, 2, "-0,63"), "half of the second decimal rounds away from zero");
	check(number_is(0.0625, 3, "0,063") && number_is(2.0625, 3, "2,063") && number_is(-0.0625, 3, "-0,063"), "half of the third decimal rounds away from zero");

	check(number_is(0.4, 0, "0") && number_is(0.6, 0, "1") && number_is(2.44, 1, "2,4") && number_is(2.46, 1, "2,5"), "below half down, above half up");
	check(number_is(nextafter(0.5, 0), 0, "0"), "the largest number below 0.5 rounds to 0");
	check(number_is(nextafter(1.5, 0), 0, "1") && number_is(nextafter(2.5, 0), 0, "2"), "the largest numbers below 1.5 and 2.5 round down");
	check(number_is(nextafter(0.5, 1), 0, "1") && number_is(nextafter(-0.5, -1), 0, "-1"), "the smallest number above 0.5 rounds up");
	// The same next to a half of the last decimal. The products are the doubles 0.49999999999999994, 2.4999999999999996,
	// 12.499999999999998 and 62.499999999999993.
	check(number_is(nextafter(0.05, 0), 1, "0,0") && number_is(nextafter(0.25, 0), 1, "0,2") && number_is(nextafter(-0.25, 0), 1, "-0,2"),
	      "the largest numbers below 0.05 and 0.25 round down with 1 decimal");
	check(number_is(nextafter(0.125, 0), 2, "0,12") && number_is(nextafter(0.0625, 0), 3, "0,062"),
	      "the largest numbers below 0.125 and 0.0625 round down with 2 and 3 decimals");
	check(number_is(nextafter(0.25, 1), 1, "0,3") && number_is(nextafter(0.125, 1), 2, "0,13") && number_is(nextafter(0.0625, 1), 3, "0,063"),
	      "the smallest numbers above these halves round up");

	// Numbers whose nearest double lies just below the half. What is rounded is the product as a double, and
	// 443.65 * 10 is the double 4436.5: the exact product 4436.4999999999997726 is nearer to it than to the
	// double below. Likewise 123.5, 111.5, 267.5, 1.5, 1000.5 and 4.5.
	check(number_is(443.65, 1, "443,7") && number_is(-443.65, 1, "-443,7") && number_is(12.35, 1, "12,4") && number_is(0.15, 1, "0,2"),
	      "443.65, 12.35 and 0.15 with 1 decimal round up as the numbers read: the product is the half");
	check(number_is(1.115, 2, "1,12") && number_is(2.675, 2, "2,68") && number_is(-0.045, 2, "-0,05") && number_is(1.0005, 3, "1,001"),
	      "1.115, 2.675 and 0.045 with 2 decimals and 1.0005 with 3 round up: the product is the half");
	// Here the product stays below the half: 1.005 * 100 is the double 100.49999999999998579, and 28.499999999999996447,
	// 2007.4999999999997726 and 31852.499999999996362
	check(number_is(1.005, 2, "1,00") && number_is(0.285, 2, "0,28") && number_is(2.0075, 3, "2,007") && number_is(-318.525, 2, "-318,52"),
	      "1.005, 0.285 and 318.525 with 2 decimals and 2.0075 with 3 round down: the product is below the half");

	check(number_is(9.5, 0, "10") && number_is(99.96, 1, "100,0") && number_is(0.9996, 3, "1,000") && number_is(-9.96, 1, "-10,0"),
	      "rounding up carries into a new digit");

	check(number_is(-0.4, 0, "0"), "-0.4 without decimals is 0, never -0");
	check(number_is(-0.004, 2, "0,00") && number_is(-0.0004, 3, "0,000"), "a negative value that rounds to zero has no sign with 2 and 3 decimals");
	check(number_is(nextafter(-0.5, 0), 0, "0"), "the negative number nearest to -0.5 above it rounds to 0 without sign");
	check(number_is(-0.06, 1, "-0,1") && number_is(-0.6, 0, "-1") && number_is(-0.0006, 3, "-0,001"),
	      "a negative value that does not round to zero keeps its sign");
	check(number_is(-5e-324, 3, "0,000") && number_is(5e-324, 0, "0") && number_is(DBL_MIN, 3, "0,000") && number_is(1e-300, 1, "0,0"),
	      "the smallest numbers are zero");
}

static void test_number_decimals(void)
{
	check(number_is(3.14159265, -1, "3"), "-1 decimals count as 0");
	check(number_is(3.14159265, 0, "3"), "0 decimals");
	check(number_is(3.14159265, 1, "3,1"), "1 decimal");
	check(number_is(3.14159265, 2, "3,14"), "2 decimals");
	check(number_is(3.14159265, 3, "3,142"), "3 decimals");
	check(number_is(3.14159265, 4, "3,142"), "4 decimals count as 3");
	check(number_is(3.14159265, 5, "3,142"), "5 decimals count as 3");
	check(number_is(3.14159265, INT_MAX, "3,142") && number_is(3.14159265, INT_MIN, "3"), "the largest and smallest count of decimals count as 3 and 0");
	check(number_is(2.71828, -1, "3") && number_is(-2.71828, -7, "-3"), "a negative count of decimals still rounds");
}

static void test_number_limits(void)
{
	static const double beyond[] = {1000000000000.0001220703125, 1e13, -1e13, 1e15, 1e19, -1e19, 1e300, DBL_MAX, -DBL_MAX, INFINITY, -INFINITY, NAN, -NAN};
	double below = nextafter(1e12, 0);
	bool at_limit = true, above = true;
	char out[ROOM];
	int decimals;
	size_t i;

	check(number_is(999999999999.0, 0, "999999999999"), "the largest whole number below the limit");
	check(number_is(999999999999.999, 3, "999999999999,999") && number_is(-999999999999.999, 3, "-999999999999,999"), "twelve digits and three decimals");
	check(number_is(999999999999.6, 0, "1000000000000"), "999999999999.6 with 0 decimals is 1000000000000: the limit is that of the value");
	check(number_is(999999999999.5, 0, "1000000000000") && number_is(-999999999999.5, 0, "-1000000000000"), "half below the limit rounds up to 13 digits");
	check(number_is(below, 3, "1000000000000,000") && number_is(-below, 3, "-1000000000000,000"), "the number nearest to the limit below it is printed");
	check(number_is(below, 0, "1000000000000") && number_is(below, 1, "1000000000000,0") && number_is(below, 2, "1000000000000,00"),
	      "the number nearest to the limit with fewer decimals");
	check(number_refused(1e12, 0) && number_refused(1e12, 3), "1e12 is refused with an empty string");
	check(number_refused(-1e12, 0) && number_refused(-1e12, 3), "-1e12 is refused");
	for(decimals = -1; decimals <= 5; decimals++)
	{
		if(!number_refused(1e12, decimals) || !number_refused(-1e12, decimals)) at_limit = false;
	}
	check(at_limit, "1e12 and -1e12 are refused with every count of decimals from -1 to 5");
	check(number_refused(nextafter(1e12, 2e12), 0) && number_refused(1e15, 0) && number_refused(1e19, 0) && number_refused(-1e19, 2) &&
	      number_refused(1e300, 0) && number_refused(DBL_MAX, 3) && number_refused(-DBL_MAX, 3), "larger magnitudes are refused");
	check(number_refused(NAN, 0) && number_refused(NAN, 3) && number_refused(-NAN, 1), "NaN is refused with an empty string");
	check(number_refused(INFINITY, 0) && number_refused(INFINITY, 3), "infinity is refused");
	check(number_refused(-INFINITY, 0) && number_refused(-INFINITY, 2), "minus infinity is refused");
	for(decimals = -1; decimals <= 5; decimals++)
	{
		for(i = 0; i < sizeof(beyond) / sizeof(beyond[0]); i++)
		{
			if(!number_refused(beyond[i], decimals)) above = false;
		}
	}
	check(above, "larger magnitudes, infinities and NaN are refused with every count of decimals from -1 to 5");

	memset(out, GUARD, sizeof(out));
	check(!fmt_number(NAN, 1, out, 0) && !fmt_number(1e12, 1, out, 0) && (unsigned char)out[0] == GUARD, "a refused value and no room: nothing is written");
}

// Every size of buffer from none to more than needed, with guard bytes around it
static void test_number_sizes(void)
{
	static const struct
	{
		double value;
		int decimals;
		const char *text;
	} cases[] = {
		{0, 0, "0"}, {7, 0, "7"}, {-7, 0, "-7"}, {12.345, 2, "12,35"}, {-0.04, 1, "0,0"}, {-12.5, 1, "-12,5"},
		{1234567, 0, "1234567"}, {0.0625, 3, "0,063"}, {999999999999.0, 0, "999999999999"},
		{-999999999999.999, 3, "-999999999999,999"}, {-999999999999.9999, 3, "-1000000000000,000"},
	};
	bool fits = true, exact = true, short_refused = true, short_empty = true, none = true, guards = true;
	size_t i, size, k;

	for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		size_t length = strlen(cases[i].text);

		for(size = 0; size <= length + 3; size++)
		{
			unsigned char buffer[BEFORE + ROOM + BEHIND];
			char *out = (char *)buffer + BEFORE;
			bool result;

			memset(buffer, GUARD, sizeof(buffer));
			result = fmt_number(cases[i].value, cases[i].decimals, out, size);

			for(k = 0; k < BEFORE; k++) if(buffer[k] != GUARD) guards = false;
			for(k = BEFORE + size; k < sizeof(buffer); k++) if(buffer[k] != GUARD) guards = false;

			if(size > length + 1 && !(result && strcmp(out, cases[i].text) == 0)) fits = false;
			if(size == length + 1 && !(result && strcmp(out, cases[i].text) == 0)) exact = false;
			if(size <= length && result) short_refused = false;
			if(size <= length && size > 0 && out[0] != '\0') short_empty = false;
			if(size == 0 && (unsigned char)out[0] != GUARD) none = false;
		}
	}
	check(fits, "a number fits into a buffer larger than needed");
	check(exact, "a number fits exactly with its terminating zero");
	check(short_refused, "a number one byte or more too long is refused");
	check(short_empty, "a refused number leaves an empty string");
	check(none, "no room at all: nothing is written");
	check(guards, "no byte is written outside the buffer of a number");
}

// The expected text from whole numbers only: the digits of `number` with a comma in front of the last
// `decimals` of them
static void model_number(int64_t number, int decimals, char *out, size_t size)
{
	unsigned long long magnitude = (unsigned long long)(number < 0 ? -number : number);
	unsigned long long scale = 1;
	int i;

	for(i = 0; i < decimals; i++) scale *= 10;
	if(decimals == 0) snprintf(out, size, "%s%llu", number < 0 ? "-" : "", magnitude);
	else snprintf(out, size, "%s%llu,%0*llu", number < 0 ? "-" : "", magnitude / scale, decimals, magnitude % scale);
}

static bool model_agrees(double value, int decimals, int64_t number, int *shown)
{
	char expected[ROOM], out[ROOM];

	model_number(number, decimals, expected, sizeof(expected));
	memset(out, GUARD, sizeof(out));
	if(fmt_number(value, decimals, out, sizeof(out)) && strcmp(out, expected) == 0) return true;

	if((*shown)++ < 5)
	{
		out[sizeof(out) - 1] = '\0';
		printf("  %.17g with %d: expected \"%s\", got \"%s\"\n", value, decimals, expected, out);
	}
	return false;
}

static void test_number_model(void)
{
	static const double scales[4] = {1, 10, 100, 1000};
	static const int64_t limits[4] = {1000000000000LL, 10000000000000LL, 100000000000000LL, 1000000000000000LL};
	unsigned char buffer[BEFORE + ROOM + BEHIND];
	char expected[ROOM];
	int wrong, wrong_sized = 0, shown = 0, decimals, i;
	int64_t number;
	size_t size;

	// number / scale is the double nearest to a number with exactly `decimals` decimals
	wrong = 0;
	for(decimals = 0; decimals <= 3; decimals++)
	{
		for(number = -12000; number <= 12000; number++)
		{
			if(!model_agrees((double)number / scales[decimals], decimals, number, &shown)) wrong++;
		}
	}
	check(wrong == 0, "every number from -12000 to 12000 in units of the last decimal is printed digit for digit");

	wrong = 0;
	for(number = 0; number <= 5000; number++)
	{
		if(!model_agrees((double)number + 0.5, 0, number + 1, &shown)) wrong++;
		if(!model_agrees(-((double)number + 0.5), 0, -(number + 1), &shown)) wrong++;
	}
	check(wrong == 0, "every half from 0.5 to 5000.5 and from -0.5 to -5000.5 rounds away from zero");

	// Numbers of every length up to the limit
	wrong = 0;
	for(i = 0; i < 200000; i++)
	{
		int64_t limit, range = 10;
		int digits;

		decimals = (int)(random_next() % 4);
		limit = limits[decimals];
		digits = 1 + (int)(random_next() % 15);
		while(--digits > 0 && range < limit) range *= 10;
		number = (int64_t)((((uint64_t)random_next() << 40) | ((uint64_t)random_next() << 16) | random_next()) % (uint64_t)range);
		if(random_next() % 2) number = -number;
		if(!model_agrees((double)number / scales[decimals], decimals, number, &shown)) wrong++;

		// The same number into a buffer of another size
		model_number(number, decimals, expected, sizeof(expected));
		size = random_size(strlen(expected));
		memset(buffer, GUARD, sizeof(buffer));
		if(!sized_right(buffer, size, fmt_number((double)number / scales[decimals], decimals, (char *)buffer + BEFORE, size), expected)) wrong_sized++;
	}
	check(wrong == 0, "200000 random numbers of 1 to 15 digits are printed digit for digit");
	check(wrong_sized == 0, "each of them with a buffer of a random size: printed if it fits with its zero, refused and empty if not, nothing outside");

	// A number plus less than half of its last decimal rounds to that number. The parts are multiples of
	// 1/64, so the sum is exact.
	wrong = 0;
	for(i = 0; i < 200000; i++)
	{
		int part = (int)(random_next() % 59) - 29;
		bool negative = random_next() % 2;
		double value;

		decimals = (int)(random_next() % 4);
		number = (int64_t)(random_next() % 1000000000u);
		value = ((double)number + part / 64.0) / scales[decimals];
		if(!model_agrees(negative ? -value : value, decimals, negative ? -number : number, &shown)) wrong++;
	}
	check(wrong == 0, "200000 random numbers with up to 29/64 of the last decimal more or less round to the nearest");

	// Exact halves of every size: an odd number divided by 2^(decimals + 1) has a 5 behind its last decimal
	// and nothing behind that, and it is a double. In halves of the last decimal it is the odd number
	// times 5^decimals; half away from zero is the whole number above.
	wrong = 0;
	for(i = 0; i < 200000; i++)
	{
		static const int64_t fives[4] = {1, 5, 25, 125};
		int64_t odd;
		int bits;
		double value;

		decimals = (int)(random_next() % 4);
		// Up to 39 bits in front of the comma: below 2^39, which is below 1e12
		bits = 1 + (int)(random_next() % (39 + decimals + 1));
		odd = (int64_t)((((uint64_t)random_next() << 24) | random_next()) & ((1ull << bits) - 1)) | 1;
		value = ldexp((double)odd, -(decimals + 1));
		number = odd * fives[decimals] / 2 + 1;
		if(random_next() % 2)
		{
			value = -value;
			number = -number;
		}
		if(!model_agrees(value, decimals, number, &shown)) wrong++;
	}
	check(wrong == 0, "200000 random exact halves of every size and with every count of decimals round away from zero");

	// The doubles next to such a half on either side. They are no halves any more, but their product may be
	// one: the product as a double decides, rounded half away from zero by the C library.
	wrong = 0;
	for(i = 0; i < 200000; i++)
	{
		int64_t odd;
		int bits;
		double value;

		decimals = (int)(random_next() % 4);
		bits = 1 + (int)(random_next() % (39 + decimals + 1));
		odd = (int64_t)((((uint64_t)random_next() << 24) | random_next()) & ((1ull << bits) - 1)) | 1;
		value = ldexp((double)odd, -(decimals + 1));
		value = nextafter(value, random_next() % 2 ? 0 : 1e12);
		if(random_next() % 2) value = -value;
		if(!model_agrees(value, decimals, llround(value * scales[decimals]), &shown)) wrong++;
	}
	check(wrong == 0, "200000 random doubles next to an exact half on either side: the product as a double decides");

	// Numbers that end in a 5 behind the last decimal shown, as a text would give them: the doubles lie next
	// to the half on either side. The expected number is the product as a double, rounded half away from
	// zero by the C library.
	wrong = 0;
	for(i = 0; i < 200000; i++)
	{
		double value;

		decimals = 1 + (int)(random_next() % 3);
		number = (int64_t)(random_next() % 100000000u) * 10 + 5;
		value = (double)number / (scales[decimals] * 10);
		if(random_next() % 2) value = -value;
		if(!model_agrees(value, decimals, llround(value * scales[decimals]), &shown)) wrong++;
	}
	check(wrong == 0, "200000 random numbers with a 5 behind the last decimal: the product as a double decides, rounded half away from zero");

	// Any double below the limit
	wrong = 0;
	for(i = 0; i < 200000; i++)
	{
		double value = ldexp((double)(((uint64_t)random_next() << 29) ^ ((uint64_t)random_next() << 8) ^ random_next()), -53);

		// A number below 1 times 2^-52 to 2^39, which is below 1e12
		value = ldexp(value, (int)(random_next() % 92) - 52);
		if(random_next() % 2) value = -value;
		decimals = (int)(random_next() % 4);
		if(!model_agrees(value, decimals, llround(value * scales[decimals]), &shown)) wrong++;
	}
	check(wrong == 0, "200000 random doubles of every size below the limit give the digits of the rounded product");
}

static void test_label_examples(void)
{
	check(label_is("ENGINE_OIL_TEMP", "Engine Oil Temp"), "ENGINE_OIL_TEMP becomes Engine Oil Temp");
	check(label_is("DPF_KM_SINCE_REGEN", "Dpf Km Since Regen"), "DPF_KM_SINCE_REGEN becomes Dpf Km Since Regen");
	check(label_is("@BATT_V", "Batt V"), "@BATT_V becomes Batt V");
	check(label_is("2ND_GEAR", "2nd Gear"), "2ND_GEAR becomes 2nd Gear");
	check(label_is("LAMBDA", "Lambda") && label_is("FUEL_L", "Fuel L") && label_is("BOOST_PRESSURE_LP", "Boost Pressure Lp"), "names of the vehicle profile");

	check(label_is("A_B", "A B"), "an underscore becomes a space");
	check(label_is("A@B", "A B"), "an @ becomes a space");
	check(label_is("A__B", "A B") && label_is("A___B", "A B"), "several underscores in a row become one space");
	check(label_is("A@@B", "A B") && label_is("A_@B", "A B") && label_is("A@_@B", "A B"), "mixed breaks in a row become one space");
	check(label_is("_A", "A") && label_is("@A", "A") && label_is("__@A", "A"), "no space at the start");
	check(label_is("A_", "A") && label_is("A@", "A") && label_is("A_@_", "A"), "no space at the end");
	check(label_is("@@ab__cd@", "Ab Cd"), "breaks at both ends and in the middle");
	check(label_is("", "") && label_is("_", "") && label_is("@", "") && label_is("__@@_", ""), "a name without a word gives an empty label");

	check(label_is("engine_rpm", "Engine Rpm"), "the first letter of every word becomes upper case");
	check(label_is("eNGINE_rPM", "Engine Rpm") && label_is("ENGINE", "Engine"), "the other letters become lower case");
	check(label_is("a", "A") && label_is("z", "Z") && label_is("m_n", "M N"), "first and last letter of the alphabet as first letter");
	check(label_is("xA", "Xa") && label_is("xZ", "Xz") && label_is("AZ", "Az"), "first and last letter of the alphabet as other letter");
	check(label_is("Abc", "Abc") && label_is("A", "A"), "a label that is already right stays");
	check(label_is("a_bc_def_ghij_klmno_pqrstu_vwxyz", "A Bc Def Ghij Klmno Pqrstu Vwxyz") && label_is("A_BC_DEF_GHIJ_KLMNO_PQRSTU_VWXYZ", "A Bc Def Ghij Klmno Pqrstu Vwxyz"),
	      "every word of a long name gets its upper case letter, however far behind");
	check(label_is("diesel_particulate_filter_differential_pressure_sensor_voltage", "Diesel Particulate Filter Differential Pressure Sensor Voltage"),
	      "a name of 62 bytes in lower case: the words at its end get their upper case letter as well");
	check(label_is("DIESEL_PARTICULATE_FILTER_DIFFERENTIAL_PRESSURE_SENSOR_VOLTAGE", "Diesel Particulate Filter Differential Pressure Sensor Voltage"),
	      "a name of 62 bytes in upper case: the letters at its end become lower case as well");

	check(label_is("O2_SENSOR_1", "O2 Sensor 1") && label_is("0-100_KMH", "0-100 Kmh"), "digits and signs are passed on");
	check(label_is("1ST", "1st") && label_is("_9aB", "9ab"), "a word that starts with a digit has no upper case letter");
	check(label_is("A B", "A b") && label_is("a-b.c", "A-b.c"), "a space or another sign is no word break");
	check(label_is("`", "`") && label_is("{", "{") && label_is("x`", "X`") && label_is("x{", "X{") && label_is("x[", "X["),
	      "the signs next to the letters in ASCII are passed on");
	check(label_is("\303\226L_TEMP", "\303\226l Temp") && label_is("\303\266l", "\303\266l") && label_is("K\303\234HLER", "K\303\234hler"),
	      "the bytes of a UTF-8 character are passed on");
	check(label_is("\377A_\200b\337", "\377a \200b\337") && label_is("\300\301", "\300\301") && label_is("\341\372", "\341\372"),
	      "bytes above 127 are passed on, also where they are no UTF-8");
}

static void test_label_bytes(void)
{
	static const char lower[] = "abcdefghijklmnopqrstuvwxyz";
	static const char upper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	bool right = true;
	int c;

	// Every byte once as the first and once as the second of a word
	for(c = 1; c <= 255; c++)
	{
		char name[3] = {(char)c, (char)c, '\0'};
		char expected[3] = {(char)c, (char)c, '\0'};
		const char *found;

		if(c == '_' || c == '@')
		{
			expected[0] = '\0';
		}
		else if(c < 128 && (found = strchr(lower, c)) != NULL)
		{
			expected[0] = upper[found - lower];
		}
		else if(c < 128 && (found = strchr(upper, c)) != NULL)
		{
			expected[1] = lower[found - upper];
		}
		if(!label_is(name, expected)) right = false;
	}
	check(right, "every byte from 1 to 255 as first and as other byte of a word");
}

static void test_label_sizes(void)
{
	static const struct
	{
		const char *name;
		const char *text;
	} cases[] = {
		{"ENGINE_OIL_TEMP", "Engine Oil Temp"}, {"A", "A"}, {"A_B", "A B"}, {"AB_", "Ab"}, {"_AB", "Ab"},
		{"__A__B__", "A B"}, {"", ""}, {"__", ""}, {"K\303\234HLER_TEMP", "K\303\234hler Temp"},
		// 62 bytes: with its zero it fills the largest buffer but one byte
		{"DIESEL_PARTICULATE_FILTER_DIFFERENTIAL_PRESSURE_SENSOR_VOLTAGE", "Diesel Particulate Filter Differential Pressure Sensor Voltage"},
	};
	bool fits = true, exact = true, short_refused = true, short_empty = true, none = true, guards = true;
	size_t i, size, k;

	for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		size_t length = strlen(cases[i].text);

		for(size = 0; size <= length + 3; size++)
		{
			unsigned char buffer[BEFORE + ROOM + BEHIND];
			char *out = (char *)buffer + BEFORE;
			bool result;

			memset(buffer, GUARD, sizeof(buffer));
			result = fmt_label(cases[i].name, out, size);

			for(k = 0; k < BEFORE; k++) if(buffer[k] != GUARD) guards = false;
			for(k = BEFORE + size; k < sizeof(buffer); k++) if(buffer[k] != GUARD) guards = false;

			if(size > length + 1 && !(result && strcmp(out, cases[i].text) == 0)) fits = false;
			if(size == length + 1 && !(result && strcmp(out, cases[i].text) == 0)) exact = false;
			if(size <= length && result) short_refused = false;
			if(size <= length && size > 0 && out[0] != '\0') short_empty = false;
			if(size == 0 && (unsigned char)out[0] != GUARD) none = false;
		}
	}
	check(fits, "a label fits into a buffer larger than needed");
	check(exact, "a label fits exactly with its terminating zero, breaks at the ends need no room");
	check(short_refused, "a label one byte or more too long is refused");
	check(short_empty, "a refused label leaves an empty string");
	check(none, "no room at all for a label: nothing is written");
	check(guards, "no byte is written outside the buffer of a label");
}

// Written differently from the single pass in fmt.c: split into words, change their letters, join
static void model_label(const char *name, char *out)
{
	static const char lower[] = "abcdefghijklmnopqrstuvwxyz";
	static const char upper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	char copy[ROOM];
	char *word;

	strcpy(copy, name);
	out[0] = '\0';
	for(word = strtok(copy, "_@"); word != NULL; word = strtok(NULL, "_@"))
	{
		size_t i;

		for(i = 0; word[i] != '\0'; i++)
		{
			const char *found = strchr(i == 0 ? lower : upper, word[i]);

			if(found != NULL) word[i] = i == 0 ? upper[found - lower] : lower[found - upper];
		}
		if(out[0] != '\0') strcat(out, " ");
		strcat(out, word);
	}
}

static void test_label_model(void)
{
	static const char alphabet[] = {'a', 'B', '_', '@', '7'};
	char name[8], expected[ROOM];
	unsigned char buffer[BEFORE + ROOM + BEHIND];
	char *out = (char *)buffer + BEFORE;
	int length, count = 0, wrong = 0, wrong_exact = 0, wrong_short = 0, wrong_guard = 0, wrong_long = 0, wrong_sized = 0, i;
	size_t size, k;
	long code, codes;

	// Every name of up to 7 characters made of a lower and an upper case letter, both breaks and a digit
	for(length = 0; length <= 7; length++)
	{
		codes = 1;
		for(i = 0; i < length; i++) codes *= (long)sizeof(alphabet);
		for(code = 0; code < codes; code++)
		{
			long rest = code;

			for(i = 0; i < length; i++)
			{
				name[i] = alphabet[rest % (long)sizeof(alphabet)];
				rest /= (long)sizeof(alphabet);
			}
			name[length] = '\0';
			model_label(name, expected);
			count++;

			memset(buffer, GUARD, sizeof(buffer));
			if(!fmt_label(name, out, ROOM) || strcmp(out, expected) != 0)
			{
				if(wrong++ < 5) printf("  \"%s\": expected \"%s\"\n", name, expected);
			}

			size = strlen(expected) + 1;
			memset(buffer, GUARD, sizeof(buffer));
			if(!fmt_label(name, out, size) || strcmp(out, expected) != 0) wrong_exact++;
			for(k = BEFORE + size; k < sizeof(buffer); k++) if(buffer[k] != GUARD) wrong_guard++;

			size--;
			memset(buffer, GUARD, sizeof(buffer));
			if(fmt_label(name, out, size) || (size > 0 && out[0] != '\0')) wrong_short++;
			for(k = BEFORE + size; k < sizeof(buffer); k++) if(buffer[k] != GUARD) wrong_guard++;
		}
	}
	check(count == 97656, "all 97656 names of up to 7 characters from a, B, _, @ and 7 are tried");

	// Longer names, also with bytes that are passed on
	for(i = 0; i < 50000; i++)
	{
		static const char more[] = {'a', 'q', 'B', 'Z', '_', '_', '@', '7', ' ', '-', '\303', '\244'};
		char longer[41];
		int n;

		length = 8 + (int)(random_next() % 33);
		for(n = 0; n < length; n++) longer[n] = more[random_next() % sizeof(more)];
		longer[length] = '\0';
		model_label(longer, expected);

		memset(buffer, GUARD, sizeof(buffer));
		if(!fmt_label(longer, out, ROOM) || strcmp(out, expected) != 0)
		{
			if(wrong_long++ < 5) printf("  \"%s\": expected \"%s\"\n", longer, expected);
		}

		// The same name into a buffer of another size
		size = random_size(strlen(expected));
		memset(buffer, GUARD, sizeof(buffer));
		if(!sized_right(buffer, size, fmt_label(longer, out, size), expected)) wrong_sized++;
	}
	check(wrong_long == 0, "50000 random names of 8 to 40 bytes give the label of the split and joined words");
	check(wrong_sized == 0, "each of them with a buffer of a random size: the label if it fits with its zero, refused and empty if not, nothing outside");
	check(wrong == 0, "every such name gives the label of the split and joined words");
	check(wrong_exact == 0, "every such label fits into exactly its length and a zero");
	check(wrong_short == 0, "every such label is refused with one byte less");
	check(wrong_guard == 0, "no such label is written behind its buffer");
}

int main(void)
{
	test_number_examples();
	test_number_rounding();
	test_number_decimals();
	test_number_limits();
	test_number_sizes();
	test_number_model();
	test_label_examples();
	test_label_bytes();
	test_label_sizes();
	test_label_model();
	return test_end();
}
