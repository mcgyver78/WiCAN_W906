/*
 * Host test for display/components/core/access.c. Run "make test_access && ./test_access" in display/test.
 * redproof.py removes or weakens every rule once (mutations/access.py) and expects this test to fail.
 *
 * The scenes below all use the same times: the release is given at 1000 and ends at 601000, and at
 * 1801000 at the latest, whatever renews it. A question is asked at 2000 and waits until 62000; the knob
 * counts from 3500 on. The question renews the release, which then ends at 602000.
 */
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "test.h"
#include "access.h"

#define COUNT(list)     (sizeof(list) / sizeof((list)[0]))

// Not "access": that is a function of unistd.h
static access_t acc;

// The text of a check that is made for each of the three questions
static char text[300];

static void released(void)
{
	access_init(&acc);
	access_open(&acc, 1000);
}

static uint32_t asked(access_ask_t ask)
{
	released();
	return access_ask(&acc, ask, 2000);
}

// The scene of asked() with a release that ends at `release_end`. With the times of the header the
// functions get there only at the latest end of a release, half an hour after it was given (see
// test_question_at_the_latest_end()): everywhere else a question renews the release for ten minutes and
// waits for one. So the end of the release is written into the struct by hand here.
static void asked_under_a_release_until(uint64_t release_end)
{
	asked(ACCESS_ASK_WIFI);
	acc.open_until_ms = release_end;
}

// The release of released(), renewed by changes at 400000 and 900000: it ends at 1500000 if nothing else
// comes, and at 1801000 at the latest
static void released_long_ago(void)
{
	released();
	access_write(&acc, 400000);
	access_write(&acc, 900000);
}

// The same, renewed once more at 1450000. That renewal would reach to 2050000: the release ends at its
// latest end, 1801000.
static void released_up_to_the_latest_end(void)
{
	released_long_ago();
	access_write(&acc, 1450000);
}

// The fields of two structs are the same. Not compared as memory: the gaps between the fields hold anything.
static bool same_fields(const access_t *one, const access_t *other)
{
	return one->open == other->open && one->open_until_ms == other->open_until_ms && one->open_max_ms == other->open_max_ms &&
	       one->clock_ms == other->clock_ms && one->asking == other->asking && one->asking_since_ms == other->asking_since_ms &&
	       one->asking_until_ms == other->asking_until_ms && one->ticket == other->ticket && one->ticket_end == other->ticket_end &&
	       one->previous_end == other->previous_end;
}

static void test_constants(void)
{
	check(ACCESS_OPEN_MS == 600000 && ACCESS_CONFIRM_MS == 60000, "the release lasts 600000 ms, a question waits 60000 ms");
	check(ACCESS_OPEN_MAX_MS == 1800000, "the release lasts 1800000 ms at the most");
	check(ACCESS_ASK_SHOWN_MS == 1500, "the knob counts 1500 ms after a question was asked");
}

static void test_init(void)
{
	// Whatever stood in the memory before
	acc.open = true;
	acc.open_until_ms = 999999;
	acc.open_max_ms = 999999;
	acc.clock_ms = 5000;
	acc.asking = ACCESS_ASK_RESET;
	acc.asking_since_ms = 1000;
	acc.asking_until_ms = 999999;
	acc.ticket = 77;
	acc.ticket_end = ACCESS_TICKET_WAITING;
	acc.previous_end = ACCESS_TICKET_CONFIRMED;
	access_init(&acc);
	check(!access_is_open(&acc, 0) && !access_is_open(&acc, 6000) && access_seconds_left(&acc, 6000) == 0,
	      "after the start the release is closed, whatever stood in the memory");
	check(access_asking(&acc, 6000) == ACCESS_ASK_NONE && access_ask_seconds_left(&acc, 6000) == 0, "after the start nothing is asked");
	check(acc.ticket == 0, "after the start the number of the last ticket is 0: none yet");
	check(access_ticket(&acc, 0, 6000) == ACCESS_TICKET_UNKNOWN, "after the start the ticket 0 is unknown");
	check(access_ticket(&acc, 77, 6000) == ACCESS_TICKET_UNKNOWN && access_ticket(&acc, 76, 6000) == ACCESS_TICKET_UNKNOWN &&
	      access_ticket(&acc, 1, 6000) == ACCESS_TICKET_UNKNOWN, "after the start the tickets from before it are unknown");
	check(access_ticket(&acc, UINT32_MAX, 6000) == ACCESS_TICKET_UNKNOWN, "after the start the ticket 2^32-1 is unknown: no ticket came before the first");
	check(!access_write(&acc, 6000), "after the start a change is refused");
	check(access_ask(&acc, ACCESS_ASK_WIFI, 6000) == 0, "after the start a question is refused");
	check(access_confirm(&acc, 6000) == ACCESS_ASK_NONE, "after the start the knob confirms nothing");

	acc.clock_ms = 5000;
	acc.ticket = 77;
	access_init(&acc);
	access_open(&acc, 1000);
	check(access_is_open(&acc, 600999) && !access_is_open(&acc, 601000), "after the start the time begins anew: a release given at 1000 ends at 601000");
	check(access_ask(&acc, ACCESS_ASK_WIFI, 2000) == 1, "after the start the tickets begin anew with 1");
}

static void test_release(void)
{
	access_init(&acc);
	check(!access_is_open(&acc, 500) && !access_write(&acc, 500), "without a release every change is refused");

	released();
	check(access_is_open(&acc, 1000), "the release holds from the moment it is given");
	check(access_seconds_left(&acc, 1000) == 600, "a release just given has 600 s left");
	check(access_seconds_left(&acc, 1001) == 600, "599999 ms left are rounded up to 600 s");
	check(access_seconds_left(&acc, 2000) == 599 && access_seconds_left(&acc, 2001) == 599, "599000 ms left are 599 s, 598999 ms as well");
	check(access_seconds_left(&acc, 599999) == 2 && access_seconds_left(&acc, 600000) == 1, "1001 ms left are 2 s, 1000 ms are 1 s");
	check(access_is_open(&acc, 600999) && access_seconds_left(&acc, 600999) == 1, "1 ms before its end the release holds, 1 s left");
	check(!access_is_open(&acc, 601000), "600000 ms after it was given the release has ended by itself, without a call at that moment");
	check(access_seconds_left(&acc, 601000) == 0 && access_seconds_left(&acc, 601001) == 0 && access_seconds_left(&acc, 5000000) == 0,
	      "a release that ended has 0 s left");

	released();
	check(!access_write(&acc, 601000), "a change 600000 ms after the release was given is refused");
	check(!access_is_open(&acc, 601000) && access_seconds_left(&acc, 601001) == 0 && !access_write(&acc, 601001),
	      "a refused change changes nothing: the release stays closed");

	released();
	check(access_write(&acc, 600999), "a change in the last millisecond of the release is accepted");
	check(access_seconds_left(&acc, 600999) == 600, "after an accepted change 600 s are left again");
	check(access_is_open(&acc, 1200998) && !access_is_open(&acc, 1200999), "after an accepted change the release lasts 600000 ms from that change");

	released();
	access_write(&acc, 300000);
	access_write(&acc, 300001);
	check(access_is_open(&acc, 900000) && !access_is_open(&acc, 900001), "of two accepted changes the later one counts");

	released();
	access_open(&acc, 400000);
	check(access_seconds_left(&acc, 400000) == 600 && access_is_open(&acc, 999999) && !access_is_open(&acc, 1000000),
	      "the release given again while it holds: the time starts anew");

	released();
	access_open(&acc, 700000);
	check(access_is_open(&acc, 700000) && access_is_open(&acc, 1299999) && !access_is_open(&acc, 1300000),
	      "the release given again after it ended: it holds for 600000 ms again");
	check(access_write(&acc, 700000), "under the release given again a change is accepted");

	released();
	access_close(&acc, 2000);
	check(!access_is_open(&acc, 2000) && access_seconds_left(&acc, 2000) == 0, "the release switched off at the device is closed at once, 0 s left");
	check(!access_write(&acc, 2000), "after the release was switched off a change is refused");
	access_open(&acc, 3000);
	check(access_is_open(&acc, 602999) && !access_is_open(&acc, 603000), "switched on again after it was switched off: it holds for 600000 ms");
	check(access_write(&acc, 3000), "switched on again after it was switched off: a change is accepted");

	access_init(&acc);
	access_close(&acc, 1000);
	check(!access_is_open(&acc, 1000) && !access_write(&acc, 1000), "switched off while it was closed: it stays closed");
}

static void test_question(void)
{
	check(asked(ACCESS_ASK_WIFI) == 1, "the first ticket has the number 1");
	check(access_asking(&acc, 2000) == ACCESS_ASK_WIFI, "the question that waits is the one that was asked");
	check(access_ticket(&acc, 1, 2000) == ACCESS_TICKET_WAITING, "the ticket of a question that waits is waiting");
	check(access_ask_seconds_left(&acc, 2000) == 60, "a question just asked has 60 s left");
	check(access_ask_seconds_left(&acc, 2001) == 60 && access_ask_seconds_left(&acc, 3000) == 59 && access_ask_seconds_left(&acc, 3001) == 59,
	      "59999 ms left for the knob are rounded up to 60 s, 59000 and 58999 ms are 59 s");
	check(access_ask_seconds_left(&acc, 60999) == 2 && access_ask_seconds_left(&acc, 61000) == 1, "1001 ms left for the knob are 2 s, 1000 ms are 1 s");
	check(access_asking(&acc, 61999) == ACCESS_ASK_WIFI && access_ask_seconds_left(&acc, 61999) == 1 && access_ticket(&acc, 1, 61999) == ACCESS_TICKET_WAITING,
	      "1 ms before its time is over the question waits, 1 s left");
	check(access_asking(&acc, 62000) == ACCESS_ASK_NONE && access_ask_seconds_left(&acc, 62000) == 0,
	      "60000 ms after it was asked nothing waits any more, 0 s left");
	check(access_ticket(&acc, 1, 62000) == ACCESS_TICKET_EXPIRED, "60000 ms after it was asked the ticket has expired, without a call at that moment");
	check(access_is_open(&acc, 62000), "the release goes on when a question expires");
	check(access_is_open(&acc, 601999) && !access_is_open(&acc, 602000), "an accepted question counts as a change: the release lasts 600000 ms from it");

	asked(ACCESS_ASK_WIFI);
	check(access_confirm(&acc, 61999) == ACCESS_ASK_WIFI, "the knob pressed in the last millisecond confirms the question");
	check(access_ticket(&acc, 1, 61999) == ACCESS_TICKET_CONFIRMED, "the ticket of a confirmed question is confirmed");
	check(access_ticket(&acc, 1, 62000) == ACCESS_TICKET_CONFIRMED && access_ticket(&acc, 1, 5000000) == ACCESS_TICKET_CONFIRMED,
	      "a confirmed ticket stays confirmed when the times are over");
	check(access_asking(&acc, 61999) == ACCESS_ASK_NONE && access_ask_seconds_left(&acc, 61999) == 0, "after the confirmation nothing waits");
	check(access_confirm(&acc, 61999) == ACCESS_ASK_NONE, "a question is confirmed exactly once: the second press confirms nothing");
	check(access_ticket(&acc, 1, 61999) == ACCESS_TICKET_CONFIRMED, "a press of the knob while nothing waits leaves the confirmed ticket confirmed");

	asked(ACCESS_ASK_WIFI);
	check(access_confirm(&acc, 62000) == ACCESS_ASK_NONE, "the knob pressed 60000 ms after the question confirms nothing");
	check(access_ticket(&acc, 1, 62000) == ACCESS_TICKET_EXPIRED, "the knob pressed too late: the ticket has expired");

	released();
	check(access_confirm(&acc, 2000) == ACCESS_ASK_NONE, "the knob pressed while nothing was asked confirms nothing");

	check(asked(ACCESS_ASK_FIRMWARE) == 1 && access_asking(&acc, 2000) == ACCESS_ASK_FIRMWARE && access_confirm(&acc, 4000) == ACCESS_ASK_FIRMWARE,
	      "the question for the firmware is shown and confirmed as that");
	check(asked(ACCESS_ASK_RESET) == 1 && access_asking(&acc, 2000) == ACCESS_ASK_RESET && access_confirm(&acc, 4000) == ACCESS_ASK_RESET,
	      "the question for the factory reset is shown and confirmed as that");

	asked(ACCESS_ASK_WIFI);
	access_confirm(&acc, 50000);
	check(access_is_open(&acc, 601999) && !access_is_open(&acc, 602000), "the press of the knob is no change through the web: it neither renews nor ends the release");
}

static void test_question_refused(void)
{
	access_init(&acc);
	check(access_ask(&acc, ACCESS_ASK_WIFI, 1000) == 0, "without a release a question is refused");
	check(access_asking(&acc, 1000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 1000) == ACCESS_TICKET_UNKNOWN, "a refused question does not wait and has no ticket");

	released();
	check(access_ask(&acc, ACCESS_ASK_WIFI, 601000) == 0, "a question 600000 ms after the release was given is refused");
	check(!access_is_open(&acc, 601000), "a question refused because the release ended does not bring it back");
	released();
	check(access_ask(&acc, ACCESS_ASK_WIFI, 600999) == 1, "a question in the last millisecond of the release is accepted");
	check(access_asking(&acc, 660998) == ACCESS_ASK_WIFI && access_asking(&acc, 660999) == ACCESS_ASK_NONE && access_is_open(&acc, 660999),
	      "a question in the last millisecond of the release waits its 60000 ms: it is not cut at the end the release had before");
	released();
	access_close(&acc, 1500);
	check(access_ask(&acc, ACCESS_ASK_WIFI, 2000) == 0, "a question after the release was switched off is refused");

	released();
	check(access_ask(&acc, ACCESS_ASK_NONE, 2000) == 0, "ACCESS_ASK_NONE is no question: refused");
	check(access_ask(&acc, (access_ask_t)4, 2000) == 0, "the value behind the last of the enum is no question: refused");
	check(access_ask(&acc, (access_ask_t)99, 2000) == 0 && access_ask(&acc, (access_ask_t)-1, 2000) == 0, "99 and -1 are no questions: refused");
	check(access_ask(&acc, (access_ask_t)257, 2000) == 0 && access_ask(&acc, (access_ask_t)258, 2000) == 0 && access_ask(&acc, (access_ask_t)259, 2000) == 0 &&
	      access_ask(&acc, (access_ask_t)65537, 2000) == 0 && access_ask(&acc, (access_ask_t)65538, 2000) == 0 && access_ask(&acc, (access_ask_t)65539, 2000) == 0 &&
	      access_ask(&acc, (access_ask_t)0x40000001, 2000) == 0,
	      "257 to 259, 65537 to 65539 and 2^30+1 are no questions, although they are 1, 2 or 3 in 8 or 16 bit: refused");
	check(access_asking(&acc, 2000) == ACCESS_ASK_NONE, "after questions that are none nothing waits");
	check(access_ask(&acc, ACCESS_ASK_WIFI, 2000) == 1, "refused questions take no ticket number");

	released();
	access_ask(&acc, ACCESS_ASK_NONE, 300000);
	check(!access_is_open(&acc, 601000), "a question that is none is no change: the release is not renewed");

	asked(ACCESS_ASK_WIFI);
	check(access_ask(&acc, ACCESS_ASK_RESET, 30000) == 0, "another question while one waits is refused");
	check(access_asking(&acc, 30000) == ACCESS_ASK_WIFI && access_ticket(&acc, 1, 30000) == ACCESS_TICKET_WAITING, "the first question stays when a second is refused");
	check(access_ask_seconds_left(&acc, 30000) == 32 && access_asking(&acc, 61999) == ACCESS_ASK_WIFI && access_asking(&acc, 62000) == ACCESS_ASK_NONE,
	      "the first question keeps its time when a second is refused");
	check(access_ticket(&acc, 2, 30000) == ACCESS_TICKET_UNKNOWN, "the refused second question has no ticket");
	check(access_is_open(&acc, 601999) && !access_is_open(&acc, 602000), "a question refused because another waits is no change: the release is not renewed");
	check(access_ask(&acc, ACCESS_ASK_WIFI, 30000) == 0, "the same question again while it waits is refused as well");
	check(access_confirm(&acc, 30000) == ACCESS_ASK_WIFI, "after a refused second question the knob confirms the first");
}

static void test_tickets(void)
{
	asked(ACCESS_ASK_WIFI);
	access_confirm(&acc, 3500);
	check(access_ticket(&acc, UINT32_MAX, 3500) == ACCESS_TICKET_UNKNOWN && access_ticket(&acc, 0, 3500) == ACCESS_TICKET_UNKNOWN &&
	      access_ticket(&acc, 2, 3500) == ACCESS_TICKET_UNKNOWN, "with one ticket given the numbers 0, 2 and 2^32-1 are unknown");
	check(access_ask(&acc, ACCESS_ASK_RESET, 4000) == 2, "the second ticket has the number 2");
	check(access_ticket(&acc, 2, 4000) == ACCESS_TICKET_WAITING, "the second ticket waits");
	check(access_ticket(&acc, 1, 4000) == ACCESS_TICKET_CONFIRMED, "the ticket before the last one is still known: confirmed");
	check(access_ticket(&acc, 0, 4000) == ACCESS_TICKET_UNKNOWN && access_ticket(&acc, 3, 4000) == ACCESS_TICKET_UNKNOWN,
	      "with two tickets given the numbers 0 and 3 are unknown");
	access_refuse(&acc, 5000);
	check(access_ticket(&acc, 1, 5000) == ACCESS_TICKET_CONFIRMED && access_ticket(&acc, 2, 5000) == ACCESS_TICKET_REFUSED,
	      "the ticket before the last one stays known when the last one has ended");
	check(access_ask(&acc, ACCESS_ASK_FIRMWARE, 6000) == 3, "the third ticket has the number 3");
	check(access_ticket(&acc, 1, 6000) == ACCESS_TICKET_UNKNOWN, "two newer tickets were given: the first one is unknown");
	check(access_ticket(&acc, 2, 6000) == ACCESS_TICKET_REFUSED && access_ticket(&acc, 3, 6000) == ACCESS_TICKET_WAITING,
	      "of three tickets the second is known as refused, the third waits");
	check(access_ticket(&acc, 0, 6000) == ACCESS_TICKET_UNKNOWN && access_ticket(&acc, 4, 6000) == ACCESS_TICKET_UNKNOWN &&
	      access_ticket(&acc, UINT32_MAX, 6000) == ACCESS_TICKET_UNKNOWN, "with three tickets given the numbers 0, 4 and 2^32-1 are unknown");
	check(access_ticket(&acc, 0x80000003u, 6000) == ACCESS_TICKET_UNKNOWN && access_ticket(&acc, 0x80000002u, 6000) == ACCESS_TICKET_UNKNOWN &&
	      access_ticket(&acc, 0x10003u, 6000) == ACCESS_TICKET_UNKNOWN && access_ticket(&acc, 0x10002u, 6000) == ACCESS_TICKET_UNKNOWN &&
	      access_ticket(&acc, 0x103u, 6000) == ACCESS_TICKET_UNKNOWN && access_ticket(&acc, 0x102u, 6000) == ACCESS_TICKET_UNKNOWN,
	      "ticket numbers are compared in all 32 bit: numbers that differ from the last two only in bit 31, 16 or 8 are unknown");

	// No test can ask for 4294967294 tickets: the number of the last one is written into the struct by hand
	released();
	acc.ticket = UINT32_MAX - 1;
	check(access_ask(&acc, ACCESS_ASK_WIFI, 2000) == UINT32_MAX, "the ticket after 4294967294 is 4294967295");
	check(access_confirm(&acc, 3499) == ACCESS_ASK_NONE && access_ticket(&acc, UINT32_MAX, 3499) == ACCESS_TICKET_WAITING,
	      "the ticket 2^32-1 is not confirmed by a press that comes too soon either");
	access_confirm(&acc, 3500);
	check(access_ask(&acc, ACCESS_ASK_RESET, 4000) == 1, "after the ticket 2^32-1 comes 1, never 0");
	check(access_ticket(&acc, 1, 4000) == ACCESS_TICKET_WAITING && access_ticket(&acc, UINT32_MAX, 4000) == ACCESS_TICKET_CONFIRMED,
	      "the ticket before number 1 is 2^32-1: still known");
	check(access_ticket(&acc, 0, 4000) == ACCESS_TICKET_UNKNOWN, "0 is no ticket, also between 2^32-1 and 1");
	check(access_ticket(&acc, UINT32_MAX - 1, 4000) == ACCESS_TICKET_UNKNOWN && access_ticket(&acc, 2, 4000) == ACCESS_TICKET_UNKNOWN,
	      "around the wrap the numbers 2^32-2 and 2 are unknown");
	access_refuse(&acc, 5000);
	check(access_ask(&acc, ACCESS_ASK_FIRMWARE, 6000) == 2, "after the wrap the numbers count on: 2");
	check(access_ticket(&acc, 1, 6000) == ACCESS_TICKET_REFUSED && access_ticket(&acc, UINT32_MAX, 6000) == ACCESS_TICKET_UNKNOWN &&
	      access_ticket(&acc, 0, 6000) == ACCESS_TICKET_UNKNOWN, "two tickets after the wrap: 2^32-1 is forgotten, 0 stays unknown");

	released();
	acc.ticket = 0x7FFFFFFEu;
	check(access_ask(&acc, ACCESS_ASK_WIFI, 2000) == 0x7FFFFFFFu, "the ticket after 2^31-2 is 2^31-1");
	access_confirm(&acc, 3500);
	check(access_ask(&acc, ACCESS_ASK_WIFI, 4000) == 0x80000000u && access_ticket(&acc, 0x7FFFFFFFu, 4000) == ACCESS_TICKET_CONFIRMED,
	      "the numbers count on across 2^31");
	check(access_ticket(&acc, 0x80000000u, 4000) == ACCESS_TICKET_WAITING && access_ticket(&acc, 0, 4000) == ACCESS_TICKET_UNKNOWN &&
	      access_ticket(&acc, UINT32_MAX, 4000) == ACCESS_TICKET_UNKNOWN, "with the tickets 2^31-1 and 2^31 given the numbers 0 and 2^32-1 are unknown: bit 31 counts");
}

static void test_refuse(void)
{
	asked(ACCESS_ASK_WIFI);
	access_refuse(&acc, 3000);
	check(access_ticket(&acc, 1, 3000) == ACCESS_TICKET_REFUSED, "the question refused at the device: its ticket is refused");
	check(access_asking(&acc, 3000) == ACCESS_ASK_NONE && access_ask_seconds_left(&acc, 3000) == 0, "after the refusal nothing waits");
	check(access_confirm(&acc, 4000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 4000) == ACCESS_TICKET_REFUSED, "a refused question cannot be confirmed");
	check(access_is_open(&acc, 601999) && !access_is_open(&acc, 602000), "refusing a question neither ends nor renews the release");
	check(access_ask(&acc, ACCESS_ASK_RESET, 4000) == 2, "after a refusal the next question is accepted");

	asked(ACCESS_ASK_WIFI);
	access_confirm(&acc, 3500);
	access_refuse(&acc, 4000);
	check(access_ticket(&acc, 1, 4000) == ACCESS_TICKET_CONFIRMED, "refusing while nothing waits: a confirmed ticket stays confirmed");

	asked(ACCESS_ASK_WIFI);
	access_refuse(&acc, 62000);
	check(access_ticket(&acc, 1, 62000) == ACCESS_TICKET_EXPIRED, "refused 60000 ms after it was asked: nothing waited any more, the ticket has expired");

	released();
	access_refuse(&acc, 2000);
	check(access_ticket(&acc, 0, 2000) == ACCESS_TICKET_UNKNOWN && access_ask(&acc, ACCESS_ASK_WIFI, 2000) == 1 &&
	      access_ticket(&acc, 1, 2000) == ACCESS_TICKET_WAITING, "refusing before anything was asked: nothing happens");
}

static void test_switched_off_with_a_question(void)
{
	asked(ACCESS_ASK_WIFI);
	access_close(&acc, 3000);
	check(access_ticket(&acc, 1, 3000) == ACCESS_TICKET_REFUSED, "the release switched off: the question that waits is refused");
	check(access_asking(&acc, 3000) == ACCESS_ASK_NONE && access_ask_seconds_left(&acc, 3000) == 0, "after the release was switched off nothing waits");
	check(access_confirm(&acc, 4000) == ACCESS_ASK_NONE, "the knob pressed after the release was switched off confirms nothing");
	access_open(&acc, 4000);
	check(access_asking(&acc, 4000) == ACCESS_ASK_NONE && access_confirm(&acc, 4000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 4000) == ACCESS_TICKET_REFUSED,
	      "a new release does not bring back the question of the one that was switched off");

	asked(ACCESS_ASK_WIFI);
	access_close(&acc, 62000);
	check(access_ticket(&acc, 1, 62000) == ACCESS_TICKET_EXPIRED, "switched off 60000 ms after the question: it had expired before, it was not refused");

	asked(ACCESS_ASK_WIFI);
	access_confirm(&acc, 3500);
	access_close(&acc, 4000);
	check(access_ticket(&acc, 1, 4000) == ACCESS_TICKET_CONFIRMED, "switched off after the confirmation: the ticket stays confirmed");

	asked(ACCESS_ASK_WIFI);
	access_open(&acc, 30000);
	check(access_asking(&acc, 30000) == ACCESS_ASK_WIFI && access_ticket(&acc, 1, 30000) == ACCESS_TICKET_WAITING,
	      "the release given again while a question waits: the question stays");
	check(access_ask_seconds_left(&acc, 30000) == 32 && access_asking(&acc, 61999) == ACCESS_ASK_WIFI && access_asking(&acc, 62000) == ACCESS_ASK_NONE,
	      "the release given again while a question waits: the question keeps its time");

	asked(ACCESS_ASK_WIFI);
	check(access_write(&acc, 30000), "a change while a question waits is accepted");
	check(access_asking(&acc, 30000) == ACCESS_ASK_WIFI && access_ask_seconds_left(&acc, 30000) == 32 && access_asking(&acc, 62000) == ACCESS_ASK_NONE,
	      "a change while a question waits leaves the question and its time alone");
}

// Nobody calls at the moment the time of a question runs out. Whatever is called first afterwards, the
// ticket has expired then, and it is still known so when the next one was given.
static void test_ends_noticed_late(void)
{
	asked(ACCESS_ASK_WIFI);
	check(access_ask(&acc, ACCESS_ASK_RESET, 100000) == 2, "a question long after the one before ran out is accepted");
	check(access_ticket(&acc, 1, 100000) == ACCESS_TICKET_EXPIRED && access_ticket(&acc, 2, 100000) == ACCESS_TICKET_WAITING,
	      "the ticket that ran out unnoticed is known as expired when it is the one before the last");

	asked(ACCESS_ASK_WIFI);
	access_write(&acc, 100000);
	check(access_ticket(&acc, 1, 100000) == ACCESS_TICKET_EXPIRED && access_asking(&acc, 100000) == ACCESS_ASK_NONE, "a change long after the question ran out: expired");
	asked(ACCESS_ASK_WIFI);
	access_open(&acc, 100000);
	check(access_ticket(&acc, 1, 100000) == ACCESS_TICKET_EXPIRED && access_asking(&acc, 100000) == ACCESS_ASK_NONE, "the release given again long after the question ran out: expired");
	asked(ACCESS_ASK_WIFI);
	access_close(&acc, 100000);
	check(access_ticket(&acc, 1, 100000) == ACCESS_TICKET_EXPIRED, "the release switched off long after the question ran out: expired, not refused");
	asked(ACCESS_ASK_WIFI);
	access_refuse(&acc, 100000);
	check(access_ticket(&acc, 1, 100000) == ACCESS_TICKET_EXPIRED, "refused long after the question ran out: expired, not refused");
	asked(ACCESS_ASK_WIFI);
	check(access_confirm(&acc, 100000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 100000) == ACCESS_TICKET_EXPIRED, "the knob pressed long after the question ran out: expired");

	asked(ACCESS_ASK_WIFI);
	check(access_ticket(&acc, 1, 602000) == ACCESS_TICKET_EXPIRED && access_ticket(&acc, 1, 5000000) == ACCESS_TICKET_EXPIRED,
	      "asked after the release ended as well: the question ran out first, expired");
	access_open(&acc, 700000);
	check(access_ticket(&acc, 1, 700000) == ACCESS_TICKET_EXPIRED, "the first call after question and release ended: the question ran out first, expired");
	check(access_ask(&acc, ACCESS_ASK_RESET, 700001) == 2 && access_ticket(&acc, 1, 700001) == ACCESS_TICKET_EXPIRED,
	      "question and release ended unnoticed: known as expired when it is the ticket before the last");

	asked(ACCESS_ASK_WIFI);
	access_close(&acc, 3000);
	access_open(&acc, 4000);
	check(access_ask(&acc, ACCESS_ASK_RESET, 5000) == 2 && access_ticket(&acc, 1, 5000) == ACCESS_TICKET_REFUSED,
	      "a ticket refused by the switch-off is known as refused when it is the one before the last");
}

static void test_release_ends_with_a_question(void)
{
	static const struct
	{
		access_ask_t ask;
		const char *rule;
	} questions[] = {
		{ACCESS_ASK_WIFI, "the release ends by itself while the question for the WiFi data waits: refused, nothing left to confirm"},
		{ACCESS_ASK_FIRMWARE, "the release ends by itself while the question for the firmware waits: refused, nothing left to confirm"},
		{ACCESS_ASK_RESET, "the release ends by itself while the question for the factory reset waits: refused, nothing left to confirm"},
	};
	size_t i;

	for(i = 0; i < COUNT(questions); i++)
	{
		asked(questions[i].ask);
		acc.open_until_ms = 30000;
		check(access_asking(&acc, 29999) == questions[i].ask && access_asking(&acc, 30000) == ACCESS_ASK_NONE &&
		      access_ticket(&acc, 1, 30000) == ACCESS_TICKET_REFUSED && access_confirm(&acc, 30000) == ACCESS_ASK_NONE, questions[i].rule);
	}

	asked_under_a_release_until(30000);
	check(access_is_open(&acc, 29999) && access_asking(&acc, 29999) == ACCESS_ASK_WIFI && access_ticket(&acc, 1, 29999) == ACCESS_TICKET_WAITING,
	      "1 ms before the release ends by itself the question waits");
	check(!access_is_open(&acc, 30000) && access_asking(&acc, 30000) == ACCESS_ASK_NONE && access_ask_seconds_left(&acc, 30000) == 0,
	      "the release ends by itself: the question does not wait any more");
	check(access_ticket(&acc, 1, 30000) == ACCESS_TICKET_REFUSED, "the release ends by itself: the ticket of the question that waited is refused");
	check(access_ticket(&acc, 1, 62000) == ACCESS_TICKET_REFUSED && access_ticket(&acc, 1, 5000000) == ACCESS_TICKET_REFUSED,
	      "a question refused by the end of the release stays refused when its own time is over as well");
	check(access_confirm(&acc, 30000) == ACCESS_ASK_NONE, "the knob pressed when the release has ended by itself confirms nothing");
	check(access_ticket(&acc, 1, 30000) == ACCESS_TICKET_REFUSED, "the knob pressed after the release ended: the ticket is refused");

	asked_under_a_release_until(30000);
	access_open(&acc, 40000);
	check(access_asking(&acc, 40000) == ACCESS_ASK_NONE && access_confirm(&acc, 40000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 40000) == ACCESS_TICKET_REFUSED,
	      "a new release does not bring back the question of the one that ended by itself");

	asked_under_a_release_until(30000);
	access_open(&acc, 700000);
	check(access_ticket(&acc, 1, 700000) == ACCESS_TICKET_REFUSED, "release and question ended unnoticed, the release first: refused");
	check(access_ask(&acc, ACCESS_ASK_RESET, 700000) == 2 && access_ticket(&acc, 1, 700000) == ACCESS_TICKET_REFUSED,
	      "release and question ended unnoticed, the release first: known as refused when it is the ticket before the last");

	asked_under_a_release_until(61999);
	check(access_ticket(&acc, 1, 61999) == ACCESS_TICKET_REFUSED && access_ticket(&acc, 1, 62000) == ACCESS_TICKET_REFUSED,
	      "the release ends 1 ms before the time of the question: refused");
	asked_under_a_release_until(62000);
	check(access_ticket(&acc, 1, 61999) == ACCESS_TICKET_WAITING && access_ticket(&acc, 1, 62000) == ACCESS_TICKET_EXPIRED &&
	      access_ticket(&acc, 1, 700000) == ACCESS_TICKET_EXPIRED, "release and question run out in the same millisecond: expired");
	asked_under_a_release_until(62001);
	check(access_ticket(&acc, 1, 62000) == ACCESS_TICKET_EXPIRED && access_ticket(&acc, 1, 62001) == ACCESS_TICKET_EXPIRED,
	      "the question runs out 1 ms before the release ends: expired");

	asked_under_a_release_until(30000);
	check(access_ask_seconds_left(&acc, 2000) == 28 && access_ask_seconds_left(&acc, 2001) == 28 && access_ask_seconds_left(&acc, 3000) == 27 &&
	      access_ask_seconds_left(&acc, 29000) == 1 && access_ask_seconds_left(&acc, 29999) == 1,
	      "the release ends 28000 ms after the question: 28 s are left to press the knob, not 60");
	asked_under_a_release_until(61999);
	check(access_ask_seconds_left(&acc, 2000) == 60 && access_ask_seconds_left(&acc, 2999) == 59 && access_ask_seconds_left(&acc, 3000) == 59,
	      "the release ends 1 ms before the time of the question: 59999 ms are left to press the knob, and 59000 ms at 2999");
	asked_under_a_release_until(62000);
	check(access_ask_seconds_left(&acc, 2000) == 60 && access_ask_seconds_left(&acc, 3000) == 59 && access_ask_seconds_left(&acc, 3001) == 59,
	      "release and question run out in the same millisecond: the seconds left to press the knob are those of both");
	asked_under_a_release_until(62001);
	check(access_ask_seconds_left(&acc, 2000) == 60 && access_ask_seconds_left(&acc, 3000) == 59 && access_ask_seconds_left(&acc, 3001) == 59,
	      "the release ends 1 ms after the time of the question: the seconds left to press the knob are those of the question");

	// Other times than those of the header, written into the struct by hand: the seconds are not cut short
	released();
	acc.open_until_ms = 1000 + 100000000ull;
	check(access_seconds_left(&acc, 1000) == 100000 && access_seconds_left(&acc, 1001) == 100000 && access_seconds_left(&acc, 2001) == 99999,
	      "a release that lasts 100000000 ms has 100000 s left: more than 16 bit hold");
	asked(ACCESS_ASK_WIFI);
	acc.open_until_ms = 2000 + 100000000ull;
	acc.asking_until_ms = 2000 + 70000000ull;
	check(access_ask_seconds_left(&acc, 2000) == 70000 && access_ask_seconds_left(&acc, 3001) == 69999,
	      "a question that waits 70000000 ms has 70000 s left: more than 16 bit hold");
	released();
	acc.open_until_ms = 1000 + 4294967295000ull;
	check(access_seconds_left(&acc, 1000) == 4294967295u && access_seconds_left(&acc, 1001) == 4294967295u && access_seconds_left(&acc, 2001) == 4294967294u,
	      "a release that lasts 4294967295000 ms has 2^32-1 s left: the milliseconds are counted in 64 bit, the seconds in all 32");
	asked(ACCESS_ASK_WIFI);
	acc.open_until_ms = 2000 + 4294967295000ull;
	acc.asking_until_ms = 2000 + 4294967295000ull;
	check(access_ask_seconds_left(&acc, 2000) == 4294967295u && access_ask_seconds_left(&acc, 2001) == 4294967295u && access_ask_seconds_left(&acc, 3001) == 4294967294u,
	      "a question that waits 4294967295000 ms has 2^32-1 s left: the milliseconds are counted in 64 bit, the seconds in all 32");
}

static const char *question_text(const char *name, const char *rule)
{
	snprintf(text, sizeof(text), "the question for %s %s", name, rule);
	return text;
}

// What holds for one question holds for each of the three
static void test_every_question(void)
{
	static const struct
	{
		access_ask_t ask;
		const char *name;
	} kinds[] = {{ACCESS_ASK_WIFI, "the WiFi data"}, {ACCESS_ASK_FIRMWARE, "the firmware"}, {ACCESS_ASK_RESET, "the factory reset"}};
	size_t i;

	for(i = 0; i < COUNT(kinds); i++)
	{
		access_ask_t ask = kinds[i].ask;
		const char *name = kinds[i].name;

		asked(ask);
		check(access_ask_seconds_left(&acc, 2000) == 60 && access_asking(&acc, 61999) == ask && access_asking(&acc, 62000) == ACCESS_ASK_NONE &&
		      access_ticket(&acc, 1, 61999) == ACCESS_TICKET_WAITING && access_ticket(&acc, 1, 62000) == ACCESS_TICKET_EXPIRED,
		      question_text(name, "waits 60000 ms and has expired then"));
		check(access_seconds_left(&acc, 2000) == 600 && access_is_open(&acc, 601999) && !access_is_open(&acc, 602000),
		      question_text(name, "renews the release: 600 s are left while it waits, the release ends 600000 ms after the question"));

		asked(ask);
		check(access_confirm(&acc, 2000) == ACCESS_ASK_NONE && access_confirm(&acc, 3499) == ACCESS_ASK_NONE && access_asking(&acc, 3499) == ask &&
		      access_ticket(&acc, 1, 3499) == ACCESS_TICKET_WAITING && access_ask_seconds_left(&acc, 3499) == 59,
		      question_text(name, "is not confirmed by a press in the millisecond it was asked nor by one 1499 ms later: it waits on with its time"));
		check(access_confirm(&acc, 3500) == ask && access_ticket(&acc, 1, 3500) == ACCESS_TICKET_CONFIRMED && access_asking(&acc, 3500) == ACCESS_ASK_NONE,
		      question_text(name, "is confirmed by a press 1500 ms after it was asked: the ticket is confirmed, nothing waits"));
		check(access_is_open(&acc, 3500) && access_is_open(&acc, 601999) && !access_is_open(&acc, 602000),
		      question_text(name, "confirmed: the release goes on as it was"));

		asked(ask);
		access_refuse(&acc, 2000);
		check(access_asking(&acc, 2000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 2000) == ACCESS_TICKET_REFUSED && access_confirm(&acc, 4000) == ACCESS_ASK_NONE &&
		      access_ticket(&acc, 1, 4000) == ACCESS_TICKET_REFUSED,
		      question_text(name, "refused at the device in the millisecond it was asked: nothing waits, the ticket is refused, a later press confirms nothing"));
		check(access_is_open(&acc, 4000) && access_is_open(&acc, 601999) && !access_is_open(&acc, 602000),
		      question_text(name, "refused at the device: the release goes on as it was"));

		asked(ask);
		access_close(&acc, 3000);
		check(!access_is_open(&acc, 3000) && !access_write(&acc, 3000) && access_asking(&acc, 3000) == ACCESS_ASK_NONE &&
		      access_ticket(&acc, 1, 3000) == ACCESS_TICKET_REFUSED && access_confirm(&acc, 4000) == ACCESS_ASK_NONE,
		      question_text(name, "waits when the release is switched off: closed, the ticket refused, a press confirms nothing"));

		asked(ask);
		check(access_write(&acc, 30000) && access_asking(&acc, 30000) == ask && access_ask_seconds_left(&acc, 30000) == 32 &&
		      access_is_open(&acc, 629999) && !access_is_open(&acc, 630000),
		      question_text(name, "waits when a change arrives: the change is accepted and renews the release, the question keeps its time"));

		asked(ask);
		check(access_ask(&acc, ACCESS_ASK_WIFI, 3000) == 0 && access_ask(&acc, ACCESS_ASK_FIRMWARE, 3000) == 0 && access_ask(&acc, ACCESS_ASK_RESET, 3000) == 0 &&
		      access_asking(&acc, 3000) == ask && access_ticket(&acc, 1, 3000) == ACCESS_TICKET_WAITING && access_ticket(&acc, 2, 3000) == ACCESS_TICKET_UNKNOWN,
		      question_text(name, "stays when each of the three is asked while it waits: they are refused and get no ticket"));

		asked(ask);
		access_open(&acc, 30000);
		check(access_asking(&acc, 30000) == ask && access_ask_seconds_left(&acc, 30000) == 32 && access_asking(&acc, 62000) == ACCESS_ASK_NONE &&
		      access_is_open(&acc, 629999) && !access_is_open(&acc, 630000),
		      question_text(name, "stays with its time when the release is given again, the release starts anew"));
	}
}

// Ticket 1 has ended as `first`, confirmed or refused at the device, and ticket 2 waits since 4000
static void second_waits(access_ticket_t first)
{
	asked(ACCESS_ASK_WIFI);
	if(first == ACCESS_TICKET_CONFIRMED) access_confirm(&acc, 3500);
	else access_refuse(&acc, 3500);
	access_ask(&acc, ACCESS_ASK_RESET, 4000);
}

static bool tickets_are(access_ticket_t first, access_ticket_t second, uint64_t now_ms)
{
	return access_ticket(&acc, 1, now_ms) == first && access_ticket(&acc, 2, now_ms) == second;
}

// What became of the ticket before the last one is not touched by anything that happens to the last one
static void test_ticket_before_the_last(void)
{
	second_waits(ACCESS_TICKET_REFUSED);
	check(tickets_are(ACCESS_TICKET_REFUSED, ACCESS_TICKET_WAITING, 4000), "the ticket before the last was refused, the last waits: reported so");
	check(access_confirm(&acc, 5500) == ACCESS_ASK_RESET && tickets_are(ACCESS_TICKET_REFUSED, ACCESS_TICKET_CONFIRMED, 5500),
	      "the last ticket confirmed: the one before stays refused");

	second_waits(ACCESS_TICKET_CONFIRMED);
	check(access_confirm(&acc, 4500) == ACCESS_ASK_NONE && tickets_are(ACCESS_TICKET_CONFIRMED, ACCESS_TICKET_WAITING, 4500),
	      "a press that came too soon for the last ticket: it waits on, the one before stays confirmed");
	access_refuse(&acc, 5000);
	check(tickets_are(ACCESS_TICKET_CONFIRMED, ACCESS_TICKET_REFUSED, 5000), "the last ticket refused at the device: the one before stays confirmed");

	second_waits(ACCESS_TICKET_CONFIRMED);
	access_close(&acc, 5000);
	check(tickets_are(ACCESS_TICKET_CONFIRMED, ACCESS_TICKET_REFUSED, 5000), "the last ticket refused by the switch-off: the one before stays confirmed");

	second_waits(ACCESS_TICKET_CONFIRMED);
	check(tickets_are(ACCESS_TICKET_CONFIRMED, ACCESS_TICKET_WAITING, 63999) && tickets_are(ACCESS_TICKET_CONFIRMED, ACCESS_TICKET_EXPIRED, 64000),
	      "the last ticket expires: the one before stays confirmed");
	access_write(&acc, 100000);
	check(tickets_are(ACCESS_TICKET_CONFIRMED, ACCESS_TICKET_EXPIRED, 100000), "the last ticket expired and a change came since: the one before stays confirmed");

	second_waits(ACCESS_TICKET_CONFIRMED);
	acc.open_until_ms = 30000;
	check(tickets_are(ACCESS_TICKET_CONFIRMED, ACCESS_TICKET_REFUSED, 30000), "the last ticket refused by the end of the release: the one before stays confirmed");

	released_up_to_the_latest_end();
	access_ask(&acc, ACCESS_ASK_WIFI, 1700000);
	access_confirm(&acc, 1710000);
	access_ask(&acc, ACCESS_ASK_RESET, 1750000);
	check(tickets_are(ACCESS_TICKET_CONFIRMED, ACCESS_TICKET_WAITING, 1800999) && tickets_are(ACCESS_TICKET_CONFIRMED, ACCESS_TICKET_REFUSED, 1801000),
	      "the last ticket refused by the latest end of the release: the one before stays confirmed");
	access_open(&acc, 1900000);
	check(tickets_are(ACCESS_TICKET_CONFIRMED, ACCESS_TICKET_REFUSED, 1900000), "the latest end of the release passed unnoticed while the last ticket waited: the one before stays confirmed");
	released_up_to_the_latest_end();
	access_ask(&acc, ACCESS_ASK_WIFI, 1700000);
	access_confirm(&acc, 1710000);
	access_ask(&acc, ACCESS_ASK_RESET, 1720000);
	access_refuse(&acc, 1730000);
	access_write(&acc, 1850000);
	check(tickets_are(ACCESS_TICKET_CONFIRMED, ACCESS_TICKET_REFUSED, 1850000), "the latest end of the release passed after both tickets had ended: both stay as they ended");

	second_waits(ACCESS_TICKET_CONFIRMED);
	access_open(&acc, 5000);
	check(tickets_are(ACCESS_TICKET_CONFIRMED, ACCESS_TICKET_WAITING, 5000), "the release given again while the last ticket waits: the one before stays confirmed");

	second_waits(ACCESS_TICKET_CONFIRMED);
	access_write(&acc, 5000);
	check(tickets_are(ACCESS_TICKET_CONFIRMED, ACCESS_TICKET_WAITING, 5000), "a change while the last ticket waits: the one before stays confirmed");

	asked(ACCESS_ASK_WIFI);
	check(access_ask(&acc, ACCESS_ASK_FIRMWARE, 70000) == 2 && access_confirm(&acc, 71500) == ACCESS_ASK_FIRMWARE &&
	      tickets_are(ACCESS_TICKET_EXPIRED, ACCESS_TICKET_CONFIRMED, 71500), "the last ticket confirmed: the one before stays expired");

	asked(ACCESS_ASK_WIFI);
	access_confirm(&acc, 3500);
	access_close(&acc, 4000);
	access_open(&acc, 5000);
	check(access_is_open(&acc, 5000) && access_write(&acc, 5000) && access_ticket(&acc, 1, 5000) == ACCESS_TICKET_CONFIRMED,
	      "the release given again after a ticket was confirmed and the release switched off: it holds, the ticket stays confirmed");
}

// More questions than a display ever sees, and enough to count across 2^16: the odd ones are confirmed,
// the even ones refused at the device. They take 140000 s, far longer than a release lasts at the most:
// it is switched on again before each of them.
static void test_many_tickets(void)
{
	int wrong_number = 0, wrong_ticket = 0;
	uint32_t n;

	access_init(&acc);
	for(n = 1; n <= 70000; n++)
	{
		uint64_t now = 2000 + (uint64_t)n * 2000;

		access_open(&acc, now);
		if(access_ask(&acc, ACCESS_ASK_WIFI, now) != n) wrong_number++;
		if(access_ticket(&acc, n, now) != ACCESS_TICKET_WAITING || access_ticket(&acc, n + 1, now) != ACCESS_TICKET_UNKNOWN) wrong_ticket++;
		if(n >= 2 && access_ticket(&acc, n - 1, now) != (n % 2 == 0 ? ACCESS_TICKET_CONFIRMED : ACCESS_TICKET_REFUSED)) wrong_ticket++;
		if(n >= 3 && access_ticket(&acc, n - 2, now) != ACCESS_TICKET_UNKNOWN) wrong_ticket++;

		if(n % 2 == 1)
		{
			if(access_confirm(&acc, now + 1500) != ACCESS_ASK_WIFI) wrong_ticket++;
		}
		else
		{
			access_refuse(&acc, now + 5);
		}
	}
	check(wrong_number == 0, "70000 questions in a row get the numbers 1 to 70000: each is one more than the one before");
	check(wrong_ticket == 0, "with each of 70000 questions the last ticket waits, the one before is known as it ended, the one before that and the next one are unknown");
}

// The struct is part of the header: its fields are as the last call that changed them left them
static void test_fields(void)
{
	released();
	check(acc.open && acc.open_until_ms == 601000 && acc.open_max_ms == 1801000 && acc.clock_ms == 1000 && acc.asking == ACCESS_ASK_NONE && acc.ticket == 0 &&
	      acc.ticket_end == ACCESS_TICKET_UNKNOWN && acc.previous_end == ACCESS_TICKET_UNKNOWN,
	      "the fields after the release was given at 1000: open until 601000 and until 1801000 at the latest, the time seen 1000, nothing asked, no ticket");
	access_ask(&acc, ACCESS_ASK_FIRMWARE, 2000);
	check(acc.open && acc.open_until_ms == 602000 && acc.open_max_ms == 1801000 && acc.clock_ms == 2000 && acc.asking == ACCESS_ASK_FIRMWARE &&
	      acc.asking_since_ms == 2000 && acc.asking_until_ms == 62000 && acc.ticket == 1 && acc.ticket_end == ACCESS_TICKET_WAITING &&
	      acc.previous_end == ACCESS_TICKET_UNKNOWN,
	      "the fields after a question at 2000: it waits since 2000 until 62000 with the ticket 1, the release is open until 602000, its latest end as it was");
	check(access_asking(&acc, 700000) == ACCESS_ASK_NONE && !access_is_open(&acc, 700000) && access_ticket(&acc, 1, 700000) == ACCESS_TICKET_EXPIRED &&
	      acc.open && acc.clock_ms == 2000 && acc.asking == ACCESS_ASK_FIRMWARE && acc.ticket_end == ACCESS_TICKET_WAITING,
	      "the functions that only ask leave the fields alone: what ran out since the last call still stands there");
	access_confirm(&acc, 3000);
	check(acc.open && acc.open_until_ms == 602000 && acc.open_max_ms == 1801000 && acc.clock_ms == 3000 && acc.asking == ACCESS_ASK_FIRMWARE &&
	      acc.asking_since_ms == 2000 && acc.asking_until_ms == 62000 && acc.ticket == 1 && acc.ticket_end == ACCESS_TICKET_WAITING &&
	      acc.previous_end == ACCESS_TICKET_UNKNOWN,
	      "the fields after a press of the knob at 3000, too soon: the time seen is 3000, every other field is as the question left it");
	access_confirm(&acc, 3500);
	check(acc.open && acc.open_until_ms == 602000 && acc.open_max_ms == 1801000 && acc.clock_ms == 3500 && acc.asking == ACCESS_ASK_NONE && acc.ticket == 1 &&
	      acc.ticket_end == ACCESS_TICKET_CONFIRMED && acc.previous_end == ACCESS_TICKET_UNKNOWN,
	      "the fields after the press of the knob at 3500: nothing asked, the ticket 1 confirmed, the release as it was");
	access_ask(&acc, ACCESS_ASK_RESET, 4000);
	check(acc.asking == ACCESS_ASK_RESET && acc.asking_since_ms == 4000 && acc.asking_until_ms == 64000 && acc.ticket == 2 && acc.ticket_end == ACCESS_TICKET_WAITING &&
	      acc.previous_end == ACCESS_TICKET_CONFIRMED && acc.open_until_ms == 604000 && acc.open_max_ms == 1801000 && acc.clock_ms == 4000,
	      "the fields after a second question at 4000: the ticket 2 waits since 4000, the end of the one before is kept");
	access_write(&acc, 700000);
	check(!acc.open && acc.clock_ms == 700000 && acc.asking == ACCESS_ASK_NONE && acc.ticket == 2 && acc.ticket_end == ACCESS_TICKET_EXPIRED &&
	      acc.previous_end == ACCESS_TICKET_CONFIRMED,
	      "the fields after a refused change at 700000: its time is taken over and what ran out by then is noted - closed, the ticket 2 expired");
	access_open(&acc, 800000);
	check(acc.open && acc.open_until_ms == 1400000 && acc.open_max_ms == 2600000 && acc.clock_ms == 800000,
	      "the fields after the release was given again at 800000: open until 1400000, and until 2600000 at the latest");
	access_ask(&acc, ACCESS_ASK_WIFI, 800000);
	access_close(&acc, 800001);
	check(!acc.open && acc.clock_ms == 800001 && acc.asking == ACCESS_ASK_NONE && acc.ticket == 3 && acc.ticket_end == ACCESS_TICKET_REFUSED &&
	      acc.previous_end == ACCESS_TICKET_EXPIRED,
	      "the fields after the switch-off while the ticket 3 waited: closed, nothing asked, the ticket 3 refused, the one before expired");

	released_long_ago();
	access_write(&acc, 1300000);
	check(acc.open && acc.open_until_ms == 1801000 && acc.open_max_ms == 1801000 && acc.clock_ms == 1300000,
	      "the fields after a change 501000 ms before the latest end of the release: open until that end, not behind it");
}

static void test_clock(void)
{
	access_init(&acc);
	access_open(&acc, 10000);
	check(access_is_open(&acc, 5000) && access_seconds_left(&acc, 5000) == 600 && access_seconds_left(&acc, 0) == 600,
	      "asked with a time before the release was given: no time passed");
	check(access_write(&acc, 5000), "a change with a time before the previous call is accepted");
	check(access_is_open(&acc, 609999) && !access_is_open(&acc, 610000),
	      "a change with a time before the previous call: the release lasts 600000 ms from the latest time seen");

	access_init(&acc);
	access_open(&acc, 10000);
	access_open(&acc, 5000);
	check(access_is_open(&acc, 609999) && !access_is_open(&acc, 610000),
	      "the release given again with a time before the previous call: it lasts 600000 ms from the latest time seen");

	released();
	access_write(&acc, 50000);
	check(access_ask(&acc, ACCESS_ASK_WIFI, 2000) == 1 && access_asking(&acc, 109999) == ACCESS_ASK_WIFI && access_asking(&acc, 110000) == ACCESS_ASK_NONE,
	      "a question with a time before the previous call: it waits 60000 ms from the latest time seen");
	check(access_is_open(&acc, 649999) && !access_is_open(&acc, 650000),
	      "a question with a time before the previous call: the release lasts 600000 ms from the latest time seen");

	asked(ACCESS_ASK_WIFI);
	check(access_asking(&acc, 0) == ACCESS_ASK_WIFI && access_ask_seconds_left(&acc, 0) == 60 && access_ticket(&acc, 1, 0) == ACCESS_TICKET_WAITING,
	      "asked with a time before the question: no time passed");
	access_write(&acc, 61999);
	check(access_confirm(&acc, 0) == ACCESS_ASK_WIFI, "the knob pressed with a time before the previous call: no time passed, still in time");

	asked(ACCESS_ASK_WIFI);
	access_write(&acc, 62000);
	check(access_asking(&acc, 3000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 3000) == ACCESS_TICKET_EXPIRED,
	      "a question that ran out does not come back when the time steps back");
	check(access_confirm(&acc, 3000) == ACCESS_ASK_NONE, "the knob pressed with a time before the question ran out, after it ran out: nothing is confirmed");

	released();
	access_write(&acc, 601000);
	check(!access_is_open(&acc, 2000) && !access_write(&acc, 2000), "a release that ended does not come back when the time steps back");

	// Every call that gets the struct to change takes its time over, also when it refuses
	access_init(&acc);
	access_write(&acc, 50000);
	access_open(&acc, 1000);
	check(access_is_open(&acc, 649999) && !access_is_open(&acc, 650000), "the time of a refused change counts as seen");
	access_init(&acc);
	access_ask(&acc, ACCESS_ASK_WIFI, 50000);
	access_open(&acc, 1000);
	check(access_is_open(&acc, 649999) && !access_is_open(&acc, 650000), "the time of a refused question counts as seen");
	released();
	access_ask(&acc, ACCESS_ASK_NONE, 50000);
	access_write(&acc, 2000);
	check(access_is_open(&acc, 649999) && !access_is_open(&acc, 650000), "the time of a question that is none counts as seen");
	access_init(&acc);
	access_close(&acc, 50000);
	access_open(&acc, 1000);
	check(access_is_open(&acc, 649999) && !access_is_open(&acc, 650000), "the time of a switch-off counts as seen");
	access_init(&acc);
	access_confirm(&acc, 50000);
	access_open(&acc, 1000);
	check(access_is_open(&acc, 649999) && !access_is_open(&acc, 650000), "the time of a press of the knob counts as seen");
	access_init(&acc);
	access_refuse(&acc, 50000);
	access_open(&acc, 1000);
	check(access_is_open(&acc, 649999) && !access_is_open(&acc, 650000), "the time of a refusal at the device counts as seen");

	// The functions that only ask get a const struct
	asked(ACCESS_ASK_WIFI);
	check(!access_is_open(&acc, 602000) && access_seconds_left(&acc, 602000) == 0 && access_asking(&acc, 602000) == ACCESS_ASK_NONE &&
	      access_ask_seconds_left(&acc, 602000) == 0 && access_ticket(&acc, 1, 602000) == ACCESS_TICKET_EXPIRED,
	      "asked for a time after everything ended: closed, nothing waits, expired");
	check(access_asking(&acc, 61999) == ACCESS_ASK_WIFI && access_confirm(&acc, 61999) == ACCESS_ASK_WIFI && access_write(&acc, 601999),
	      "the functions that only ask do not store their time: a call with an earlier time finds everything as it was then");

	// The clock is one of 64 bit
	access_init(&acc);
	access_open(&acc, 4294967296ull - 300000);
	check(access_is_open(&acc, 4294967296ull + 299999) && !access_is_open(&acc, 4294967296ull + 300000) && access_seconds_left(&acc, 4294967296ull) == 300,
	      "a release across 2^32 ms ends 600000 ms after it was given");
	check(access_ask(&acc, ACCESS_ASK_WIFI, 4294967296ull - 30000) == 1 && access_asking(&acc, 4294967296ull + 29999) == ACCESS_ASK_WIFI &&
	      access_asking(&acc, 4294967296ull + 30000) == ACCESS_ASK_NONE && access_ask_seconds_left(&acc, 4294967296ull) == 30,
	      "a question across 2^32 ms waits 60000 ms");
}

static void test_largest_time(void)
{
	access_init(&acc);
	access_open(&acc, UINT64_MAX - 600000);
	check(access_seconds_left(&acc, UINT64_MAX - 600000) == 600 && access_is_open(&acc, UINT64_MAX - 1) && !access_is_open(&acc, UINT64_MAX),
	      "a release given 600000 ms before the largest time ends there");

	access_init(&acc);
	access_open(&acc, UINT64_MAX - 600001);
	check(access_is_open(&acc, UINT64_MAX - 2) && !access_is_open(&acc, UINT64_MAX - 1), "a release given 600001 ms before the largest time ends 1 ms before it");

	access_init(&acc);
	access_open(&acc, UINT64_MAX - 599999);
	check(access_is_open(&acc, UINT64_MAX - 599999) && access_seconds_left(&acc, UINT64_MAX - 599999) == 600 && access_is_open(&acc, UINT64_MAX - 1) &&
	      !access_is_open(&acc, UINT64_MAX), "a release given 599999 ms before the largest time ends there: the end that would be 2^64 does not wrap to 0");
	check(access_write(&acc, UINT64_MAX - 599999) && access_is_open(&acc, UINT64_MAX - 1),
	      "a change 599999 ms before the largest time renews the release up to there: the end that would be 2^64 does not wrap to 0");

	access_init(&acc);
	access_open(&acc, UINT64_MAX - 1000);
	check(access_is_open(&acc, UINT64_MAX - 1000) && access_is_open(&acc, UINT64_MAX - 1), "a release given 1000 ms before the largest time holds up to there: no sum wraps");
	check(access_seconds_left(&acc, UINT64_MAX - 1000) == 1 && access_seconds_left(&acc, UINT64_MAX - 1) == 1,
	      "a release given 1000 ms before the largest time has 1 s left: it ends there");
	check(!access_is_open(&acc, UINT64_MAX) && access_seconds_left(&acc, UINT64_MAX) == 0, "at the largest time the release has ended: nothing lasts beyond it");
	check(access_write(&acc, UINT64_MAX - 1), "a change 1 ms before the largest time is accepted");
	check(access_is_open(&acc, UINT64_MAX - 1) && !access_write(&acc, UINT64_MAX), "a change at the largest time is refused");

	access_init(&acc);
	access_open(&acc, UINT64_MAX);
	check(!access_is_open(&acc, UINT64_MAX) && !access_write(&acc, UINT64_MAX) && access_ask(&acc, ACCESS_ASK_WIFI, UINT64_MAX) == 0,
	      "a release given at the largest time has ended at once");
	check(!access_is_open(&acc, 0) && !access_write(&acc, 0), "after the largest time was seen every time is one before it: the release stays ended");

	access_init(&acc);
	access_open(&acc, UINT64_MAX - 100000);
	check(access_ask(&acc, ACCESS_ASK_WIFI, UINT64_MAX - 30000) == 1 && access_ask_seconds_left(&acc, UINT64_MAX - 30000) == 30 &&
	      access_asking(&acc, UINT64_MAX - 1) == ACCESS_ASK_WIFI, "a question 30000 ms before the largest time waits up to there");
	check(access_asking(&acc, UINT64_MAX) == ACCESS_ASK_NONE && access_ask_seconds_left(&acc, UINT64_MAX) == 0, "at the largest time no question waits");
	check(access_ticket(&acc, 1, UINT64_MAX) == ACCESS_TICKET_EXPIRED, "question and release end together at the largest time: expired");
	check(access_confirm(&acc, UINT64_MAX - 1) == ACCESS_ASK_WIFI, "the knob pressed 1 ms before the largest time confirms");

	access_init(&acc);
	access_open(&acc, UINT64_MAX - 100000);
	access_ask(&acc, ACCESS_ASK_WIFI, UINT64_MAX - 30000);
	check(access_confirm(&acc, UINT64_MAX) == ACCESS_ASK_NONE && access_ticket(&acc, 1, UINT64_MAX) == ACCESS_TICKET_EXPIRED,
	      "the knob pressed at the largest time confirms nothing");

	access_init(&acc);
	access_open(&acc, UINT64_MAX - 100000);
	check(access_ask(&acc, ACCESS_ASK_WIFI, UINT64_MAX - 59999) == 1 && access_ask_seconds_left(&acc, UINT64_MAX - 59999) == 60 &&
	      access_asking(&acc, UINT64_MAX - 1) == ACCESS_ASK_WIFI && access_asking(&acc, UINT64_MAX) == ACCESS_ASK_NONE,
	      "a question 59999 ms before the largest time waits up to there: the end that would be 2^64 does not wrap to 0");
	access_init(&acc);
	access_open(&acc, UINT64_MAX - 100000);
	check(access_ask(&acc, ACCESS_ASK_WIFI, UINT64_MAX - 60000) == 1 && access_asking(&acc, UINT64_MAX - 1) == ACCESS_ASK_WIFI &&
	      access_ticket(&acc, 1, UINT64_MAX) == ACCESS_TICKET_EXPIRED, "a question 60000 ms before the largest time waits up to there and expires there");
	access_init(&acc);
	access_open(&acc, UINT64_MAX - 100000);
	check(access_ask(&acc, ACCESS_ASK_WIFI, UINT64_MAX - 60001) == 1 && access_asking(&acc, UINT64_MAX - 2) == ACCESS_ASK_WIFI &&
	      access_asking(&acc, UINT64_MAX - 1) == ACCESS_ASK_NONE && access_is_open(&acc, UINT64_MAX - 1),
	      "a question 60001 ms before the largest time expires 1 ms before it, the release goes on");

	access_init(&acc);
	access_open(&acc, UINT64_MAX - 100000);
	access_ask(&acc, ACCESS_ASK_WIFI, UINT64_MAX - 70000);
	check(access_ticket(&acc, 1, UINT64_MAX - 10001) == ACCESS_TICKET_WAITING && access_ticket(&acc, 1, UINT64_MAX - 10000) == ACCESS_TICKET_EXPIRED &&
	      access_is_open(&acc, UINT64_MAX - 10000), "a question 70000 ms before the largest time expires after 60000 ms, the release goes on");

	// The time of the caller wraps around: every time is then one before the latest seen
	access_init(&acc);
	access_open(&acc, UINT64_MAX - 1000);
	access_write(&acc, UINT64_MAX - 500);
	check(access_write(&acc, 0) && access_is_open(&acc, 499) && access_seconds_left(&acc, 499) == 1,
	      "the time of the caller wraps from the largest to 0: no time passed, the release holds");
}

// The release must not live for ever: it ends 1800000 ms after it was switched on at the device, whatever
// was written since
static void test_latest_end(void)
{
	uint64_t now;
	int refused = 0;

	released_long_ago();
	check(access_seconds_left(&acc, 900000) == 600 && access_is_open(&acc, 1499999) && !access_is_open(&acc, 1500000),
	      "a release renewed 901000 ms before its latest end lasts 600000 ms from that change: the latest end is not reached");

	released_long_ago();
	check(access_write(&acc, 1200999) && access_is_open(&acc, 1800998) && !access_is_open(&acc, 1800999),
	      "a change 600001 ms before the latest end of the release: it lasts 600000 ms from the change, 1 ms short of the latest end");
	released_long_ago();
	check(access_write(&acc, 1201000) && access_seconds_left(&acc, 1201000) == 600 && access_is_open(&acc, 1800999) && !access_is_open(&acc, 1801000),
	      "a change 600000 ms before the latest end of the release: it lasts 600000 ms from the change, up to the latest end");
	released_long_ago();
	check(access_write(&acc, 1201001) && access_is_open(&acc, 1800999) && !access_is_open(&acc, 1801000),
	      "a change 599999 ms before the latest end of the release: the renewal does not reach beyond it, the release ends 1800000 ms after it was given");
	check(access_seconds_left(&acc, 1201001) == 600 && access_seconds_left(&acc, 1202000) == 599 && access_seconds_left(&acc, 1800000) == 1 &&
	      access_seconds_left(&acc, 1800999) == 1 && access_seconds_left(&acc, 1801000) == 0,
	      "the seconds left are those up to the latest end where it comes before the 600000 ms of the last change");

	released_up_to_the_latest_end();
	check(access_seconds_left(&acc, 1450000) == 351 && access_seconds_left(&acc, 1450001) == 351 && access_seconds_left(&acc, 1451000) == 350,
	      "after a change 351000 ms before the latest end of the release 351 s are left, not 600");
	check(access_is_open(&acc, 1800999) && !access_is_open(&acc, 1801000) && !access_is_open(&acc, 2049999) && !access_is_open(&acc, 2050000),
	      "1800000 ms after it was given the release has ended by itself, without a call at that moment, although the last change is younger than 600000 ms");
	check(!access_write(&acc, 1801000) && !access_is_open(&acc, 1801000), "a change at the latest end of the release is refused");
	check(access_ask(&acc, ACCESS_ASK_WIFI, 1801000) == 0 && access_asking(&acc, 1801000) == ACCESS_ASK_NONE, "a question at the latest end of the release is refused");
	released_up_to_the_latest_end();
	check(access_write(&acc, 1800999) && access_seconds_left(&acc, 1800999) == 1 && !access_is_open(&acc, 1801000) && !access_write(&acc, 1801000),
	      "a change in the last millisecond before the latest end is accepted and gains nothing: the release ends 1 ms later");

	released();
	check(access_write(&acc, 541000) && access_write(&acc, 1081000) && access_write(&acc, 1621000) && !access_write(&acc, 2161000),
	      "a change every 540000 ms does not keep the release open: three are accepted, the fourth comes after the latest end and is refused");
	released();
	for(now = 2000; now < 1801000; now += 1000)
	{
		if(!access_write(&acc, now)) refused++;
	}
	check(refused == 0 && access_is_open(&acc, 1800999) && !access_is_open(&acc, 1801000) && !access_write(&acc, 1801000),
	      "a change every second: each is accepted up to 1800000 ms after the release was given, none from then on");

	// An accepted question is a change
	released_long_ago();
	check(access_ask(&acc, ACCESS_ASK_WIFI, 1200999) == 1 && access_is_open(&acc, 1800998) && !access_is_open(&acc, 1800999),
	      "a question 600001 ms before the latest end of the release renews it for 600000 ms, 1 ms short of the latest end");
	released_long_ago();
	check(access_ask(&acc, ACCESS_ASK_WIFI, 1201001) == 1 && access_seconds_left(&acc, 1202000) == 599 && access_is_open(&acc, 1800999) && !access_is_open(&acc, 1801000),
	      "a question 599999 ms before the latest end of the release renews it up to that end, not beyond");

	// Only the switch at the device moves the latest end
	released_long_ago();
	access_ask(&acc, ACCESS_ASK_WIFI, 1300000);
	access_confirm(&acc, 1310000);
	access_ask(&acc, ACCESS_ASK_RESET, 1320000);
	access_refuse(&acc, 1330000);
	access_ask(&acc, ACCESS_ASK_FIRMWARE, 1340000);
	access_ask(&acc, ACCESS_ASK_RESET, 1350000);
	access_ask(&acc, ACCESS_ASK_NONE, 1360000);
	check(access_ticket(&acc, 3, 1401000) == ACCESS_TICKET_EXPIRED && access_write(&acc, 1700000) && access_is_open(&acc, 1800999) && !access_is_open(&acc, 1801000),
	      "questions, a press of the knob, a refusal at the device and a question that expires do not move the latest end of the release");
	released();
	access_ask(&acc, ACCESS_ASK_WIFI, 2000);
	access_confirm(&acc, 3500);
	access_ask(&acc, ACCESS_ASK_RESET, 4000);
	access_refuse(&acc, 5000);
	access_ask(&acc, ACCESS_ASK_FIRMWARE, 6000);
	access_confirm(&acc, 6500);
	access_ask(&acc, ACCESS_ASK_RESET, 7000);
	access_ask(&acc, ACCESS_ASK_NONE, 8000);
	access_write(&acc, 9000);
	check(access_ticket(&acc, 3, 66000) == ACCESS_TICKET_EXPIRED && access_write(&acc, 500000) && access_write(&acc, 1000000) && access_write(&acc, 1500000) &&
	      access_seconds_left(&acc, 1500000) == 301 && access_is_open(&acc, 1800999) && !access_is_open(&acc, 1801000),
	      "questions, presses in time and too soon, a refusal at the device, refused questions, a change while a question waits and a question that expires, "
	      "all early in the release, do not bring its latest end nearer");

	access_init(&acc);
	access_open(&acc, 0);
	check(access_write(&acc, 500000) && access_write(&acc, 1000000) && access_write(&acc, 1500000) && access_seconds_left(&acc, 1500000) == 300 &&
	      access_is_open(&acc, 1799999) && !access_is_open(&acc, 1800000), "a release given at the time 0 ends at 1800000 at the latest");

	released_up_to_the_latest_end();
	access_open(&acc, 1700000);
	check(access_seconds_left(&acc, 1700000) == 600 && access_is_open(&acc, 2299999) && !access_is_open(&acc, 2300000),
	      "the release given again 101000 ms before its latest end: it lasts 600000 ms from then, the latest end it had is gone");
	check(access_write(&acc, 2200000) && access_write(&acc, 2700000) && access_write(&acc, 3200000) && access_seconds_left(&acc, 3200000) == 300 &&
	      access_is_open(&acc, 3499999) && !access_is_open(&acc, 3500000),
	      "the release given again at 1700000 ends at 3500000 at the latest: both times started anew");
	released_long_ago();
	access_open(&acc, 1000000);
	access_open(&acc, 1100000);
	check(access_write(&acc, 1600000) && access_write(&acc, 2100000) && access_write(&acc, 2600000) && access_is_open(&acc, 2899999) && !access_is_open(&acc, 2900000),
	      "the release given again twice while it holds: its latest end is 1800000 ms after the last time");

	released_up_to_the_latest_end();
	access_open(&acc, 1900000);
	check(access_is_open(&acc, 1900000) && access_seconds_left(&acc, 1900000) == 600 && access_is_open(&acc, 2499999) && !access_is_open(&acc, 2500000),
	      "the release given again after its latest end: it holds for 600000 ms again");
	check(access_write(&acc, 2400000) && access_write(&acc, 2900000) && access_write(&acc, 3400000) && access_is_open(&acc, 3699999) && !access_is_open(&acc, 3700000),
	      "the release given again after its latest end: it lasts 1800000 ms from then at the most");
	released_up_to_the_latest_end();
	access_close(&acc, 1700000);
	access_open(&acc, 1750000);
	check(access_is_open(&acc, 2349999) && !access_is_open(&acc, 2350000) && access_write(&acc, 2300000) && access_write(&acc, 2800000) && access_write(&acc, 3300000) &&
	      access_is_open(&acc, 3549999) && !access_is_open(&acc, 3550000),
	      "switched off and on again shortly before the latest end: 600000 ms from then, and 1800000 ms at the most");

	// The latest time seen counts, as everywhere
	access_init(&acc);
	access_open(&acc, 10000);
	access_open(&acc, 5000);
	check(access_write(&acc, 500000) && access_write(&acc, 1000000) && access_write(&acc, 1500000) && access_is_open(&acc, 1809999) && !access_is_open(&acc, 1810000),
	      "the release given again with a time before the previous call: its latest end is 1800000 ms after the latest time seen");
	access_init(&acc);
	access_write(&acc, 50000);
	access_open(&acc, 1000);
	check(access_write(&acc, 600000) && access_write(&acc, 1100000) && access_write(&acc, 1600000) && access_is_open(&acc, 1849999) && !access_is_open(&acc, 1850000),
	      "the release given with a time before a refused change: its latest end is 1800000 ms after the time of that change");
	released_up_to_the_latest_end();
	check(access_is_open(&acc, 0) && access_seconds_left(&acc, 0) == 351 && access_write(&acc, 0) && access_seconds_left(&acc, 0) == 351,
	      "a change with a time before the previous call, 351000 ms before the latest end by the latest time seen: 351 s are left before and after it");

	access_init(&acc);
	access_open(&acc, 4294967296ull - 900000);
	check(access_write(&acc, 4294967296ull - 400000) && access_write(&acc, 4294967296ull + 100000) && access_write(&acc, 4294967296ull + 600000) &&
	      access_seconds_left(&acc, 4294967296ull + 600000) == 300 && access_is_open(&acc, 4294967296ull + 899999) && !access_is_open(&acc, 4294967296ull + 900000),
	      "a release across 2^32 ms ends 1800000 ms after it was given at the latest");

	// Nothing lasts beyond the largest time
	access_init(&acc);
	access_open(&acc, UINT64_MAX - 1800001);
	check(access_write(&acc, UINT64_MAX - 1300000) && access_write(&acc, UINT64_MAX - 800000) && access_write(&acc, UINT64_MAX - 300000) &&
	      access_is_open(&acc, UINT64_MAX - 2) && !access_is_open(&acc, UINT64_MAX - 1),
	      "a release given 1800001 ms before the largest time and renewed ever since ends 1 ms before it");
	access_init(&acc);
	access_open(&acc, UINT64_MAX - 1800000);
	check(access_write(&acc, UINT64_MAX - 1300000) && access_write(&acc, UINT64_MAX - 800000) && access_write(&acc, UINT64_MAX - 300000) &&
	      access_seconds_left(&acc, UINT64_MAX - 300000) == 300 && access_is_open(&acc, UINT64_MAX - 1) && !access_is_open(&acc, UINT64_MAX),
	      "a release given 1800000 ms before the largest time and renewed ever since ends there");
	access_init(&acc);
	access_open(&acc, UINT64_MAX - 1799999);
	check(access_write(&acc, UINT64_MAX - 1300000) && access_write(&acc, UINT64_MAX - 800000) && access_write(&acc, UINT64_MAX - 300000) &&
	      access_seconds_left(&acc, UINT64_MAX - 300000) == 300 && access_is_open(&acc, UINT64_MAX - 1) && !access_is_open(&acc, UINT64_MAX),
	      "a release given 1799999 ms before the largest time and renewed ever since ends there: the latest end that would be 2^64 does not wrap to 0");
	access_init(&acc);
	access_open(&acc, UINT64_MAX - 700000);
	check(access_seconds_left(&acc, UINT64_MAX - 700000) == 600 && access_write(&acc, UINT64_MAX - 200000) && access_seconds_left(&acc, UINT64_MAX - 200000) == 200 &&
	      access_is_open(&acc, UINT64_MAX - 1), "a release given 700000 ms before the largest time: its latest end lies behind the largest time and cuts nothing short");
}

static void test_question_at_the_latest_end(void)
{
	static const struct
	{
		access_ask_t ask;
		const char *rule;
	} questions[] = {
		{ACCESS_ASK_WIFI, "the latest end of the release comes while the question for the WiFi data waits: refused, nothing left to confirm"},
		{ACCESS_ASK_FIRMWARE, "the latest end of the release comes while the question for the firmware waits: refused, nothing left to confirm"},
		{ACCESS_ASK_RESET, "the latest end of the release comes while the question for the factory reset waits: refused, nothing left to confirm"},
	};
	size_t i;

	for(i = 0; i < COUNT(questions); i++)
	{
		released_up_to_the_latest_end();
		check(access_ask(&acc, questions[i].ask, 1750000) == 1 && access_asking(&acc, 1800999) == questions[i].ask && access_asking(&acc, 1801000) == ACCESS_ASK_NONE &&
		      access_ticket(&acc, 1, 1801000) == ACCESS_TICKET_REFUSED && access_confirm(&acc, 1801000) == ACCESS_ASK_NONE, questions[i].rule);
	}

	released_up_to_the_latest_end();
	check(access_ask(&acc, ACCESS_ASK_WIFI, 1750000) == 1 && access_is_open(&acc, 1800999) && access_asking(&acc, 1800999) == ACCESS_ASK_WIFI &&
	      access_ticket(&acc, 1, 1800999) == ACCESS_TICKET_WAITING, "a question asked 51000 ms before the latest end of the release is accepted and waits up to 1 ms before it");
	check(!access_is_open(&acc, 1801000) && access_asking(&acc, 1801000) == ACCESS_ASK_NONE && access_ask_seconds_left(&acc, 1801000) == 0,
	      "the latest end of the release comes while a question waits: closed, the question does not wait any more");
	check(access_ticket(&acc, 1, 1801000) == ACCESS_TICKET_REFUSED, "the latest end of the release comes while a question waits: its ticket is refused, as at the ordinary end");
	check(access_ticket(&acc, 1, 1810000) == ACCESS_TICKET_REFUSED && access_ticket(&acc, 1, 5000000) == ACCESS_TICKET_REFUSED,
	      "a question refused by the latest end of the release stays refused when its own time is over as well");
	check(access_seconds_left(&acc, 1750000) == 51 && access_ask_seconds_left(&acc, 1750000) == 51 && access_ask_seconds_left(&acc, 1750001) == 51 &&
	      access_ask_seconds_left(&acc, 1751000) == 50 && access_ask_seconds_left(&acc, 1800000) == 1 && access_ask_seconds_left(&acc, 1800999) == 1,
	      "a question asked 51000 ms before the latest end of the release: 51 s are left to press the knob, as long as the release holds");
	check(access_confirm(&acc, 1801000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 1801000) == ACCESS_TICKET_REFUSED,
	      "the knob pressed at the latest end of the release confirms nothing: the ticket is refused");

	released_up_to_the_latest_end();
	access_ask(&acc, ACCESS_ASK_WIFI, 1750000);
	check(access_confirm(&acc, 1800999) == ACCESS_ASK_WIFI && access_ticket(&acc, 1, 1801000) == ACCESS_TICKET_CONFIRMED && !access_is_open(&acc, 1801000),
	      "the knob pressed in the last millisecond before the latest end of the release confirms the question");

	released_up_to_the_latest_end();
	access_ask(&acc, ACCESS_ASK_WIFI, 1750000);
	check(access_write(&acc, 1760000) && access_seconds_left(&acc, 1760000) == 41 && access_ask_seconds_left(&acc, 1760000) == 41 &&
	      access_ticket(&acc, 1, 1800999) == ACCESS_TICKET_WAITING && !access_is_open(&acc, 1801000) && access_ticket(&acc, 1, 1801000) == ACCESS_TICKET_REFUSED,
	      "a change while a question waits before the latest end of the release does not move that end: 41 s left for both, then closed and refused");

	released_up_to_the_latest_end();
	access_ask(&acc, ACCESS_ASK_WIFI, 1750000);
	check(access_confirm(&acc, 1750500) == ACCESS_ASK_NONE && access_write(&acc, 1751000) && access_seconds_left(&acc, 1751000) == 50 &&
	      access_ask_seconds_left(&acc, 1751000) == 50 && !access_is_open(&acc, 1801000) && access_ticket(&acc, 1, 1801000) == ACCESS_TICKET_REFUSED,
	      "a press that came too soon and a change after it do not move the latest end of the release: 50 s left for both, then closed and refused");

	released_up_to_the_latest_end();
	access_ask(&acc, ACCESS_ASK_WIFI, 1750000);
	access_open(&acc, 1760000);
	check(access_asking(&acc, 1809999) == ACCESS_ASK_WIFI && access_ask_seconds_left(&acc, 1760000) == 50 && access_ticket(&acc, 1, 1810000) == ACCESS_TICKET_EXPIRED &&
	      access_is_open(&acc, 2359999) && !access_is_open(&acc, 2360000),
	      "the release given again while a question waits shortly before its latest end: that end is gone, the question waits its own time and expires");
	check(access_write(&acc, 2300000) && access_write(&acc, 2800000) && access_write(&acc, 3300000) && access_is_open(&acc, 3559999) && !access_is_open(&acc, 3560000),
	      "the release given again while a question waits: its latest end is 1800000 ms after it was given again");
	released_up_to_the_latest_end();
	access_ask(&acc, ACCESS_ASK_WIFI, 1700000);
	access_confirm(&acc, 1710000);
	access_open(&acc, 1720000);
	check(access_ticket(&acc, 1, 1720000) == ACCESS_TICKET_CONFIRMED && access_is_open(&acc, 2319999) && !access_is_open(&acc, 2320000) && access_write(&acc, 2300000) &&
	      access_write(&acc, 2800000) && access_write(&acc, 3300000) && access_is_open(&acc, 3519999) && !access_is_open(&acc, 3520000),
	      "the release given again after a ticket was confirmed shortly before its latest end: both times start anew");

	released_up_to_the_latest_end();
	access_ask(&acc, ACCESS_ASK_WIFI, 1750000);
	access_open(&acc, 2000000);
	check(access_asking(&acc, 2000000) == ACCESS_ASK_NONE && access_confirm(&acc, 2000000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 2000000) == ACCESS_TICKET_REFUSED,
	      "the latest end of the release and the time of the question passed unnoticed, the release first: refused, and a new release does not bring the question back");
	check(access_ask(&acc, ACCESS_ASK_RESET, 2000000) == 2 && access_ticket(&acc, 1, 2000000) == ACCESS_TICKET_REFUSED,
	      "a ticket refused by the latest end of the release is known as refused when it is the one before the last");

	released_up_to_the_latest_end();
	access_ask(&acc, ACCESS_ASK_WIFI, 1741001);
	check(access_ticket(&acc, 1, 1800999) == ACCESS_TICKET_WAITING && access_ticket(&acc, 1, 1801000) == ACCESS_TICKET_REFUSED &&
	      access_ticket(&acc, 1, 1801001) == ACCESS_TICKET_REFUSED && access_ask_seconds_left(&acc, 1741001) == 60 && access_ask_seconds_left(&acc, 1742000) == 59,
	      "a question asked 59999 ms before the latest end of the release: the release ends 1 ms before the time of the question, refused");
	released_up_to_the_latest_end();
	access_ask(&acc, ACCESS_ASK_WIFI, 1741000);
	check(access_ticket(&acc, 1, 1800999) == ACCESS_TICKET_WAITING && access_ticket(&acc, 1, 1801000) == ACCESS_TICKET_EXPIRED &&
	      access_ticket(&acc, 1, 5000000) == ACCESS_TICKET_EXPIRED && !access_is_open(&acc, 1801000),
	      "a question asked 60000 ms before the latest end of the release: both run out in the same millisecond, expired");
	released_up_to_the_latest_end();
	access_ask(&acc, ACCESS_ASK_WIFI, 1740999);
	check(access_ticket(&acc, 1, 1800998) == ACCESS_TICKET_WAITING && access_ticket(&acc, 1, 1800999) == ACCESS_TICKET_EXPIRED && access_is_open(&acc, 1800999) &&
	      access_ticket(&acc, 1, 1801000) == ACCESS_TICKET_EXPIRED,
	      "a question asked 60001 ms before the latest end of the release expires 1 ms before it, while the release still holds");

	released_up_to_the_latest_end();
	check(access_ask(&acc, ACCESS_ASK_RESET, 1799501) == 1 && access_confirm(&acc, 1800999) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 1800999) == ACCESS_TICKET_WAITING &&
	      access_confirm(&acc, 1801000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 1801000) == ACCESS_TICKET_REFUSED,
	      "a question asked 1499 ms before the latest end of the release is accepted, but no press confirms it: too soon while the release holds, refused when it has ended");
	released_up_to_the_latest_end();
	check(access_ask(&acc, ACCESS_ASK_RESET, 1799499) == 1 && access_confirm(&acc, 1800998) == ACCESS_ASK_NONE && access_confirm(&acc, 1800999) == ACCESS_ASK_RESET,
	      "a question asked 1501 ms before the latest end of the release is confirmed by a press in the last millisecond of the release, not by one before");
	released_up_to_the_latest_end();
	check(access_ask(&acc, ACCESS_ASK_WIFI, 1800999) == 1 && access_ask_seconds_left(&acc, 1800999) == 1 && access_ticket(&acc, 1, 1800999) == ACCESS_TICKET_WAITING &&
	      access_ticket(&acc, 1, 1801000) == ACCESS_TICKET_REFUSED,
	      "a question in the last millisecond before the latest end of the release is accepted, waits 1 ms and is refused then");

	access_init(&acc);
	access_open(&acc, 4294967296ull - 1750000);
	access_write(&acc, 4294967296ull - 1250000);
	access_write(&acc, 4294967296ull - 750000);
	access_write(&acc, 4294967296ull - 250000);
	check(access_ask(&acc, ACCESS_ASK_WIFI, 4294967296ull - 1000) == 1 && access_ask_seconds_left(&acc, 4294967296ull - 1000) == 51 &&
	      access_ticket(&acc, 1, 4294967296ull + 49999) == ACCESS_TICKET_WAITING && access_ticket(&acc, 1, 4294967296ull + 50000) == ACCESS_TICKET_REFUSED,
	      "a question 51000 ms before the latest end of a release across 2^32 ms is refused at that end");
}

// A question must not be confirmed by a press that was meant for the screen below it
static void test_press_too_soon(void)
{
	asked(ACCESS_ASK_WIFI);
	check(access_confirm(&acc, 2000) == ACCESS_ASK_NONE, "the knob pressed in the millisecond the question is asked confirms nothing: that press was meant for the screen below");
	check(access_asking(&acc, 2000) == ACCESS_ASK_WIFI && access_ticket(&acc, 1, 2000) == ACCESS_TICKET_WAITING && access_ask_seconds_left(&acc, 2000) == 60,
	      "after a press that came too soon the question waits on, with its ticket and its time");

	asked(ACCESS_ASK_WIFI);
	check(access_confirm(&acc, 3499) == ACCESS_ASK_NONE && access_asking(&acc, 3499) == ACCESS_ASK_WIFI && access_ticket(&acc, 1, 3499) == ACCESS_TICKET_WAITING,
	      "the knob pressed 1499 ms after the question was asked confirms nothing, the question waits on");
	asked(ACCESS_ASK_WIFI);
	check(access_confirm(&acc, 3500) == ACCESS_ASK_WIFI && access_ticket(&acc, 1, 3500) == ACCESS_TICKET_CONFIRMED && access_asking(&acc, 3500) == ACCESS_ASK_NONE,
	      "the knob pressed 1500 ms after the question was asked confirms it");
	asked(ACCESS_ASK_WIFI);
	check(access_confirm(&acc, 3501) == ACCESS_ASK_WIFI && access_ticket(&acc, 1, 3501) == ACCESS_TICKET_CONFIRMED, "the knob pressed 1501 ms after the question was asked confirms it");

	asked(ACCESS_ASK_WIFI);
	check(access_confirm(&acc, 2500) == ACCESS_ASK_NONE && access_confirm(&acc, 3000) == ACCESS_ASK_NONE && access_confirm(&acc, 3499) == ACCESS_ASK_NONE &&
	      access_confirm(&acc, 3500) == ACCESS_ASK_WIFI,
	      "presses that came too soon neither use the question up nor make it wait anew: the press 1500 ms after it was asked confirms it");
	check(access_ticket(&acc, 1, 3500) == ACCESS_TICKET_CONFIRMED && access_confirm(&acc, 3500) == ACCESS_ASK_NONE && access_confirm(&acc, 5000) == ACCESS_ASK_NONE,
	      "a question confirmed after presses that came too soon is confirmed exactly once");

	asked(ACCESS_ASK_WIFI);
	access_confirm(&acc, 3000);
	check(access_ask_seconds_left(&acc, 3000) == 59 && access_asking(&acc, 61999) == ACCESS_ASK_WIFI && access_asking(&acc, 62000) == ACCESS_ASK_NONE &&
	      access_ticket(&acc, 1, 62000) == ACCESS_TICKET_EXPIRED, "a press that came too soon gives the question no new time: it expires 60000 ms after it was asked");
	check(access_is_open(&acc, 3000) && access_seconds_left(&acc, 3000) == 599 && access_is_open(&acc, 601999) && !access_is_open(&acc, 602000),
	      "a press that came too soon neither renews nor ends the release");
	check(access_ticket(&acc, 2, 3000) == ACCESS_TICKET_UNKNOWN && access_ask(&acc, ACCESS_ASK_RESET, 3000) == 0 && access_asking(&acc, 3000) == ACCESS_ASK_WIFI,
	      "after a press that came too soon the question still stands in the way of another one");

	asked(ACCESS_ASK_WIFI);
	access_confirm(&acc, 3000);
	check(access_write(&acc, 2500) && access_is_open(&acc, 602999) && !access_is_open(&acc, 603000), "the time of a press that came too soon counts as seen");
	asked(ACCESS_ASK_WIFI);
	access_confirm(&acc, 3000);
	check(access_confirm(&acc, 62000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 62000) == ACCESS_TICKET_EXPIRED,
	      "a question pressed too soon and then too late: expired");

	// What does not wait for the knob to count
	asked(ACCESS_ASK_WIFI);
	access_refuse(&acc, 2000);
	check(access_ticket(&acc, 1, 2000) == ACCESS_TICKET_REFUSED && access_asking(&acc, 2000) == ACCESS_ASK_NONE,
	      "the question refused at the device in the millisecond it was asked: refused at once");
	asked(ACCESS_ASK_WIFI);
	access_refuse(&acc, 3499);
	check(access_ticket(&acc, 1, 3499) == ACCESS_TICKET_REFUSED && access_confirm(&acc, 3500) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 3500) == ACCESS_TICKET_REFUSED,
	      "the question refused at the device 1499 ms after it was asked: refused, the press 1 ms later confirms nothing");
	asked(ACCESS_ASK_WIFI);
	access_confirm(&acc, 3000);
	access_refuse(&acc, 3000);
	check(access_ticket(&acc, 1, 3000) == ACCESS_TICKET_REFUSED && access_confirm(&acc, 4000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 4000) == ACCESS_TICKET_REFUSED,
	      "the question refused at the device after a press that came too soon: refused");
	asked(ACCESS_ASK_WIFI);
	access_close(&acc, 2000);
	check(access_ticket(&acc, 1, 2000) == ACCESS_TICKET_REFUSED && !access_is_open(&acc, 2000),
	      "the release switched off in the millisecond a question was asked: closed and refused at once");

	access_init(&acc);
	access_open(&acc, 0);
	check(access_ask(&acc, ACCESS_ASK_WIFI, 0) == 1 && access_confirm(&acc, 0) == ACCESS_ASK_NONE && access_confirm(&acc, 1499) == ACCESS_ASK_NONE &&
	      access_asking(&acc, 1499) == ACCESS_ASK_WIFI && access_confirm(&acc, 1500) == ACCESS_ASK_WIFI,
	      "a question asked at the time 0: the knob counts from 1500 on, not sooner");

	// Every question counts from the moment it was asked itself
	asked(ACCESS_ASK_WIFI);
	access_confirm(&acc, 3500);
	check(access_ask(&acc, ACCESS_ASK_RESET, 4000) == 2 && access_confirm(&acc, 5499) == ACCESS_ASK_NONE && access_asking(&acc, 5499) == ACCESS_ASK_RESET &&
	      access_confirm(&acc, 5500) == ACCESS_ASK_RESET, "a second question counts from the moment it was asked itself: no press confirms it sooner than 1500 ms after that");
	asked(ACCESS_ASK_WIFI);
	check(access_ask(&acc, ACCESS_ASK_RESET, 100000) == 2 && access_confirm(&acc, 100000) == ACCESS_ASK_NONE && access_confirm(&acc, 101499) == ACCESS_ASK_NONE &&
	      access_ticket(&acc, 2, 101499) == ACCESS_TICKET_WAITING && access_confirm(&acc, 101500) == ACCESS_ASK_RESET,
	      "a question asked long after the one before ran out: no press confirms it sooner than 1500 ms after it was asked");

	asked(ACCESS_ASK_WIFI);
	access_refuse(&acc, 3000);
	check(access_ask(&acc, ACCESS_ASK_RESET, 4000) == 2 && access_confirm(&acc, 5499) == ACCESS_ASK_NONE && access_ticket(&acc, 2, 5499) == ACCESS_TICKET_WAITING &&
	      access_confirm(&acc, 5500) == ACCESS_ASK_RESET,
	      "a question after one that was refused at the device: no press confirms it sooner than 1500 ms after it was asked");
	asked(ACCESS_ASK_WIFI);
	access_close(&acc, 3000);
	access_open(&acc, 3500);
	check(access_ask(&acc, ACCESS_ASK_RESET, 4000) == 2 && access_confirm(&acc, 5499) == ACCESS_ASK_NONE && access_confirm(&acc, 5500) == ACCESS_ASK_RESET,
	      "a question after one that was refused by the switch-off: no press confirms it sooner than 1500 ms after it was asked");

	// A press shortly after a question ended otherwise finds nothing, and changes nothing
	asked(ACCESS_ASK_WIFI);
	access_refuse(&acc, 2500);
	check(access_confirm(&acc, 3000) == ACCESS_ASK_NONE && access_asking(&acc, 3000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 3000) == ACCESS_TICKET_REFUSED &&
	      access_may_ask(&acc, ACCESS_ASK_RESET, 3000) == ACCESS_ALLOWED && access_confirm(&acc, 3500) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 3500) == ACCESS_TICKET_REFUSED,
	      "the knob pressed 500 ms after the question was refused at the device, 1000 ms after it was asked: nothing waits, the ticket stays refused");
	asked(ACCESS_ASK_WIFI);
	access_close(&acc, 2500);
	access_open(&acc, 2600);
	check(access_confirm(&acc, 3000) == ACCESS_ASK_NONE && access_asking(&acc, 3000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 3000) == ACCESS_TICKET_REFUSED &&
	      access_is_open(&acc, 3000) && access_may_ask(&acc, ACCESS_ASK_RESET, 3000) == ACCESS_ALLOWED,
	      "the knob pressed shortly after the release was switched off and on again, 1000 ms after the question: nothing waits, the ticket stays refused");

	// What happens while it waits does not make it wait anew, and does not let the knob count sooner
	asked(ACCESS_ASK_WIFI);
	check(access_ask(&acc, ACCESS_ASK_RESET, 3000) == 0 && access_confirm(&acc, 3499) == ACCESS_ASK_NONE && access_confirm(&acc, 3500) == ACCESS_ASK_WIFI,
	      "a second question that is refused does not move the moment the knob counts for the first");
	asked(ACCESS_ASK_WIFI);
	check(access_ask(&acc, ACCESS_ASK_NONE, 3000) == 0 && access_confirm(&acc, 3499) == ACCESS_ASK_NONE && access_confirm(&acc, 3500) == ACCESS_ASK_WIFI,
	      "a question that is none does not move the moment the knob counts for the one that waits");
	asked(ACCESS_ASK_WIFI);
	check(access_write(&acc, 3000) && access_confirm(&acc, 3499) == ACCESS_ASK_NONE && access_confirm(&acc, 3500) == ACCESS_ASK_WIFI,
	      "a change does not move the moment the knob counts for the question that waits");
	asked(ACCESS_ASK_WIFI);
	access_open(&acc, 3000);
	check(access_confirm(&acc, 3499) == ACCESS_ASK_NONE && access_confirm(&acc, 3500) == ACCESS_ASK_WIFI,
	      "the release given again does not move the moment the knob counts for the question that waits");

	// The time is the latest one seen
	asked(ACCESS_ASK_WIFI);
	check(access_confirm(&acc, 0) == ACCESS_ASK_NONE && access_asking(&acc, 2000) == ACCESS_ASK_WIFI && access_confirm(&acc, 1999) == ACCESS_ASK_NONE,
	      "the knob pressed with a time before the question was asked: no time passed, too soon");
	asked(ACCESS_ASK_WIFI);
	access_write(&acc, 3499);
	check(access_confirm(&acc, 0) == ACCESS_ASK_NONE && access_asking(&acc, 3499) == ACCESS_ASK_WIFI,
	      "the knob pressed with a time before the previous call, 1499 ms after the question by the latest time seen: too soon");
	asked(ACCESS_ASK_WIFI);
	access_write(&acc, 3500);
	check(access_confirm(&acc, 0) == ACCESS_ASK_WIFI, "the knob pressed with a time before the previous call, 1500 ms after the question by the latest time seen: confirmed");
	released();
	access_write(&acc, 50000);
	check(access_ask(&acc, ACCESS_ASK_WIFI, 2000) == 1 && access_confirm(&acc, 51499) == ACCESS_ASK_NONE && access_confirm(&acc, 51500) == ACCESS_ASK_WIFI,
	      "a question with a time before the previous call waits since the latest time seen: the knob counts 1500 ms after that");

	access_init(&acc);
	access_open(&acc, 4294967296ull - 300000);
	check(access_ask(&acc, ACCESS_ASK_WIFI, 4294967296ull - 1000) == 1 && access_confirm(&acc, 4294967296ull - 1) == ACCESS_ASK_NONE &&
	      access_confirm(&acc, 4294967296ull + 499) == ACCESS_ASK_NONE && access_confirm(&acc, 4294967296ull + 500) == ACCESS_ASK_WIFI,
	      "a question asked 1000 ms before 2^32 ms: the knob counts 1500 ms after it, not sooner on either side of 2^32");

	access_init(&acc);
	access_open(&acc, 4294967296ull + 1000);
	check(access_ask(&acc, ACCESS_ASK_WIFI, 4294967296ull + 2000) == 1 && access_confirm(&acc, 4294967296ull + 3499) == ACCESS_ASK_NONE &&
	      access_confirm(&acc, 4294967296ull + 3500) == ACCESS_ASK_WIFI, "a question asked later than 2^32 ms: the knob counts 1500 ms after it, not sooner");

	access_init(&acc);
	access_open(&acc, UINT64_MAX - 100000);
	check(access_ask(&acc, ACCESS_ASK_WIFI, UINT64_MAX - 1000) == 1 && access_confirm(&acc, UINT64_MAX - 1) == ACCESS_ASK_NONE &&
	      access_ticket(&acc, 1, UINT64_MAX - 1) == ACCESS_TICKET_WAITING && access_confirm(&acc, UINT64_MAX) == ACCESS_ASK_NONE &&
	      access_ticket(&acc, 1, UINT64_MAX) == ACCESS_TICKET_EXPIRED,
	      "a question asked 1000 ms before the largest time is never confirmed: too soon up to there, expired there - no sum wraps");
	access_init(&acc);
	access_open(&acc, UINT64_MAX - 100000);
	check(access_ask(&acc, ACCESS_ASK_WIFI, UINT64_MAX - 1501) == 1 && access_confirm(&acc, UINT64_MAX - 2) == ACCESS_ASK_NONE &&
	      access_confirm(&acc, UINT64_MAX - 1) == ACCESS_ASK_WIFI, "a question asked 1501 ms before the largest time is confirmed by a press 1 ms before it, not by one before");
}

static bool may_ask_each(access_refusal_t expected, uint64_t now_ms)
{
	return access_may_ask(&acc, ACCESS_ASK_WIFI, now_ms) == expected && access_may_ask(&acc, ACCESS_ASK_FIRMWARE, now_ms) == expected &&
	       access_may_ask(&acc, ACCESS_ASK_RESET, now_ms) == expected;
}

// What is no question: ACCESS_ASK_NONE, values behind the enum, and values that are a question in 8 or 16 bit only
static bool may_ask_none(access_refusal_t expected, uint64_t now_ms)
{
	static const int none[] = {ACCESS_ASK_NONE, 4, 99, -1, 257, 258, 259, 65537, 65538, 65539, 0x40000001};
	size_t i;

	for(i = 0; i < COUNT(none); i++)
	{
		if(access_may_ask(&acc, (access_ask_t)none[i], now_ms) != expected) return false;
	}
	return true;
}

// What access_ask() would do, told without doing it
static void test_may_ask(void)
{
	access_t before;

	access_init(&acc);
	check(may_ask_each(ACCESS_CLOSED, 1000), "after the start each of the three questions would be refused: closed");
	check(access_ask(&acc, ACCESS_ASK_WIFI, 1000) == 0, "what is foretold as closed is refused then");

	released();
	check(may_ask_each(ACCESS_ALLOWED, 1000) && may_ask_each(ACCESS_ALLOWED, 2000), "under the release each of the three questions is allowed");
	check(access_ask(&acc, ACCESS_ASK_RESET, 2000) == 1, "what is foretold as allowed gets its ticket then");
	check(may_ask_each(ACCESS_ASKING, 2000) && may_ask_each(ACCESS_ASKING, 30000), "while a question waits each of the three would be refused: asking, also the one that waits");
	check(access_ask(&acc, ACCESS_ASK_WIFI, 30000) == 0 && access_asking(&acc, 30000) == ACCESS_ASK_RESET, "what is foretold as asking is refused then, the first question stays");

	released();
	check(may_ask_none(ACCESS_BAD_QUESTION, 2000),
	      "under the release ACCESS_ASK_NONE, 4, 99, -1 and what is a question in 8 or 16 bit only are no questions: bad question");
	check(access_ask(&acc, ACCESS_ASK_NONE, 2000) == 0 && access_ask(&acc, (access_ask_t)4, 2000) == 0, "what is foretold as a bad question is refused then");

	// Where two reasons hold
	access_init(&acc);
	check(may_ask_none(ACCESS_CLOSED, 1000), "the release is closed and what is asked is no question: closed is named, it goes first");
	asked(ACCESS_ASK_WIFI);
	check(may_ask_none(ACCESS_BAD_QUESTION, 30000), "a question waits and what is asked is no question: bad question is named, it goes before asking");

	// The release
	released();
	check(may_ask_each(ACCESS_ALLOWED, 600999) && may_ask_each(ACCESS_CLOSED, 601000) && may_ask_each(ACCESS_CLOSED, 5000000),
	      "in the last millisecond of the release a question is allowed, 600000 ms after it was given it is closed, without a call at that moment");
	released();
	access_close(&acc, 2000);
	check(may_ask_each(ACCESS_CLOSED, 2000), "after the release was switched off a question would be refused: closed");
	access_open(&acc, 3000);
	check(may_ask_each(ACCESS_ALLOWED, 3000), "after the release was switched on again a question is allowed");
	released_up_to_the_latest_end();
	check(may_ask_each(ACCESS_ALLOWED, 1800999) && may_ask_each(ACCESS_CLOSED, 1801000),
	      "in the last millisecond before the latest end of the release a question is allowed, at that end it is closed");

	// The question that waits
	asked(ACCESS_ASK_WIFI);
	check(may_ask_each(ACCESS_ASKING, 61999) && may_ask_each(ACCESS_ALLOWED, 62000),
	      "up to the last millisecond of a question another would be refused as asking; when its time is over another is allowed, without a call at that moment");
	check(may_ask_each(ACCESS_ALLOWED, 601999) && may_ask_each(ACCESS_CLOSED, 602000), "when the release renewed by a question has ended another question would be refused: closed");
	asked(ACCESS_ASK_WIFI);
	access_confirm(&acc, 3000);
	check(may_ask_each(ACCESS_ASKING, 3000), "after a press that came too soon the question still waits: asking");
	access_confirm(&acc, 3500);
	check(may_ask_each(ACCESS_ALLOWED, 3500), "after the confirmation another question is allowed");
	asked(ACCESS_ASK_WIFI);
	access_refuse(&acc, 3000);
	check(may_ask_each(ACCESS_ALLOWED, 3000), "after the refusal at the device another question is allowed");
	asked(ACCESS_ASK_WIFI);
	access_close(&acc, 3000);
	check(may_ask_each(ACCESS_CLOSED, 3000), "the release switched off while a question waited: closed, not asking");
	asked_under_a_release_until(30000);
	check(may_ask_each(ACCESS_ASKING, 29999) && may_ask_each(ACCESS_CLOSED, 30000), "the release ends by itself while a question waits: closed from then on, not asking");

	// Nothing changes
	asked(ACCESS_ASK_WIFI);
	before = acc;
	check(access_may_ask(&acc, ACCESS_ASK_RESET, 30000) == ACCESS_ASKING && access_may_ask(&acc, ACCESS_ASK_NONE, 30000) == ACCESS_BAD_QUESTION &&
	      access_may_ask(&acc, ACCESS_ASK_RESET, 100000) == ACCESS_ALLOWED && access_may_ask(&acc, ACCESS_ASK_RESET, 700000) == ACCESS_CLOSED && same_fields(&acc, &before),
	      "asking what a question would do changes no field, whatever the answer: not the time seen, and what ran out by then is not noted");
	released();
	before = acc;
	check(access_may_ask(&acc, ACCESS_ASK_WIFI, 300000) == ACCESS_ALLOWED && same_fields(&acc, &before) && access_asking(&acc, 300000) == ACCESS_ASK_NONE &&
	      access_ticket(&acc, 1, 300000) == ACCESS_TICKET_UNKNOWN && access_is_open(&acc, 600999) && !access_is_open(&acc, 601000),
	      "a question foretold as allowed is not asked by that: nothing waits, no ticket is given, the release is not renewed");
	released();
	check(access_may_ask(&acc, ACCESS_ASK_WIFI, 601000) == ACCESS_CLOSED && access_may_ask(&acc, ACCESS_ASK_WIFI, 2000) == ACCESS_ALLOWED &&
	      access_ask(&acc, ACCESS_ASK_WIFI, 2000) == 1, "the time of the foretelling is not stored: a later call with an earlier time finds everything as it was then");

	// A time before the latest one seen
	access_init(&acc);
	access_open(&acc, 10000);
	check(may_ask_each(ACCESS_ALLOWED, 5000) && may_ask_each(ACCESS_ALLOWED, 0), "asked with a time before the release was given: no time passed, allowed");
	released();
	access_write(&acc, 601000);
	check(may_ask_each(ACCESS_CLOSED, 2000), "a release that ended does not come back for the foretelling when the time steps back");
	asked(ACCESS_ASK_WIFI);
	access_write(&acc, 61999);
	check(may_ask_each(ACCESS_ASKING, 0), "foretold with a time before the previous call while a question waits: no time passed, asking");
	access_write(&acc, 62000);
	check(may_ask_each(ACCESS_ALLOWED, 3000), "a question that ran out does not wait again for the foretelling when the time steps back");

	// The largest time
	access_init(&acc);
	access_open(&acc, UINT64_MAX - 1000);
	check(may_ask_each(ACCESS_ALLOWED, UINT64_MAX - 1) && may_ask_each(ACCESS_CLOSED, UINT64_MAX), "1 ms before the largest time a question is allowed, at the largest time it is closed");
	access_init(&acc);
	access_open(&acc, 4294967296ull - 300000);
	check(may_ask_each(ACCESS_ALLOWED, 4294967296ull + 299999) && may_ask_each(ACCESS_CLOSED, 4294967296ull + 300000),
	      "under a release across 2^32 ms a question is allowed up to its last millisecond");
}

/*
 * The same rules written a second time for the walk below: with times that are counted down instead of
 * points in time that are compared, with the two ends of the release kept apart and looked at when time
 * passes instead of one end that is cut short when it is renewed, with the time a question has waited
 * counted up, with the events worked off in the order they happen, and with a list of all tickets instead
 * of the ends of the last two.
 */
#define WALKS           1500
#define WALK_STEPS      800

typedef struct
{
	uint64_t seen;              // the latest time of a call that may change something
	bool open;
	uint64_t open_left;         // ms the release still lasts if no change comes, counted from `seen`
	uint64_t latest_left;       // ms it lasts at the most, whatever comes, counted from `seen`
	access_ask_t waiting;
	uint64_t waiting_left;      // ms the question still waits, counted from `seen`
	uint64_t waited;            // ms it has waited up to `seen`
	access_ticket_t newest_end; // what became of the newest ticket
} model_t;

static uint32_t model_numbers[WALK_STEPS];      // every ticket given in this walk
static access_ticket_t model_ends[WALK_STEPS];  // how each ended, except the newest
static int model_given;
static uint32_t model_number_before;            // the number the first ticket of the walk follows

// How long something lasts that begins at `seen`: its time, but not beyond the largest time
static uint64_t model_room(uint64_t seen, uint64_t duration)
{
	uint64_t room = UINT64_MAX - seen;

	return duration < room ? duration : room;
}

static void model_end_question(model_t *model, access_ticket_t end)
{
	model->waiting = ACCESS_ASK_NONE;
	model->newest_end = end;
}

// ms until the release ends: at the earlier of its two ends
static uint64_t model_release_left(const model_t *model)
{
	return model->open_left < model->latest_left ? model->open_left : model->latest_left;
}

// ms left to press the knob: the question ends with its own time or with the release
static uint64_t model_knob_left(const model_t *model)
{
	uint64_t release_left = model_release_left(model);

	return model->waiting_left < release_left ? model->waiting_left : release_left;
}

// Lets the time pass up to now_ms and works off what runs out on the way, the earlier event first
static void model_pass(model_t *model, uint64_t now_ms)
{
	uint64_t passed = now_ms > model->seen ? now_ms - model->seen : 0;

	model->seen += passed;
	for(;;)
	{
		bool question_due = model->waiting != ACCESS_ASK_NONE && model->waiting_left <= passed;
		bool release_due = model->open && model_release_left(model) <= passed;

		if(question_due && (!release_due || model->waiting_left <= model_release_left(model)))
		{
			model_end_question(model, ACCESS_TICKET_EXPIRED);
		}
		else if(release_due)
		{
			model->open = false;
			if(model->waiting != ACCESS_ASK_NONE) model_end_question(model, ACCESS_TICKET_REFUSED);
		}
		else
		{
			break;
		}
	}
	if(model->open)
	{
		model->open_left -= passed;
		model->latest_left -= passed;
	}
	if(model->waiting != ACCESS_ASK_NONE)
	{
		model->waiting_left -= passed;
		model->waited += passed;
	}
}

static void model_open(model_t *model, uint64_t now_ms)
{
	model_pass(model, now_ms);
	model->open = true;
	model->open_left = model_room(model->seen, 600000);
	model->latest_left = model_room(model->seen, 1800000);
}

static void model_close(model_t *model, uint64_t now_ms)
{
	model_pass(model, now_ms);
	model->open = false;
	if(model->waiting != ACCESS_ASK_NONE) model_end_question(model, ACCESS_TICKET_REFUSED);
}

static bool model_write(model_t *model, uint64_t now_ms)
{
	model_pass(model, now_ms);
	if(!model->open) return false;
	model->open_left = model_room(model->seen, 600000);
	return true;
}

static uint32_t model_ask(model_t *model, access_ask_t ask, uint64_t now_ms)
{
	uint32_t number;

	model_pass(model, now_ms);
	switch(ask)
	{
		case ACCESS_ASK_WIFI:
		case ACCESS_ASK_FIRMWARE:
		case ACCESS_ASK_RESET:
			break;
		default:
			return 0;
	}
	if(model->waiting != ACCESS_ASK_NONE) return 0;
	if(!model_write(model, now_ms)) return 0;

	if(model_given > 0) model_ends[model_given - 1] = model->newest_end;
	number = (model_given > 0 ? model_numbers[model_given - 1] : model_number_before) + 1;
	if(number == 0) number = 1;
	model_numbers[model_given++] = number;
	model->waiting = ask;
	model->waiting_left = model_room(model->seen, 60000);
	model->waited = 0;
	model->newest_end = ACCESS_TICKET_WAITING;
	return number;
}

// What a question would be told at now_ms. The model is not changed: time passes in a copy.
static access_refusal_t model_may_ask(const model_t *model, access_ask_t ask, uint64_t now_ms)
{
	model_t at = *model;

	model_pass(&at, now_ms);
	if(!at.open) return ACCESS_CLOSED;
	switch(ask)
	{
		case ACCESS_ASK_WIFI:
		case ACCESS_ASK_FIRMWARE:
		case ACCESS_ASK_RESET:
			return at.waiting != ACCESS_ASK_NONE ? ACCESS_ASKING : ACCESS_ALLOWED;
		default:
			return ACCESS_BAD_QUESTION;
	}
}

static access_ask_t model_confirm(model_t *model, uint64_t now_ms)
{
	access_ask_t question;

	model_pass(model, now_ms);
	question = model->waiting;
	// Too soon: the press is not for this question, which waits on
	if(question == ACCESS_ASK_NONE || model->waited < 1500) return ACCESS_ASK_NONE;
	model_end_question(model, ACCESS_TICKET_CONFIRMED);
	return question;
}

static void model_refuse(model_t *model, uint64_t now_ms)
{
	model_pass(model, now_ms);
	if(model->waiting != ACCESS_ASK_NONE) model_end_question(model, ACCESS_TICKET_REFUSED);
}

static uint32_t model_whole_seconds(uint64_t ms)
{
	return (uint32_t)(ms / 1000) + (ms % 1000 != 0 ? 1 : 0);
}

static access_ticket_t model_ticket(const model_t *at, uint32_t number)
{
	if(model_given >= 1 && model_numbers[model_given - 1] == number) return at->newest_end;
	if(model_given >= 2 && model_numbers[model_given - 2] == number) return model_ends[model_given - 2];
	return ACCESS_TICKET_UNKNOWN;
}

static uint32_t walk_random_state;

static uint32_t walk_random(uint32_t below)
{
	walk_random_state = walk_random_state * 1664525u + 1013904223u;
	return (walk_random_state >> 8) % below;
}

static uint64_t later(uint64_t time, uint64_t by)
{
	return time > UINT64_MAX - by ? UINT64_MAX : time + by;
}

static uint64_t earlier(uint64_t time, uint64_t by)
{
	return time > by ? time - by : 0;
}

// What a walk found wrong, as bits of the exit status of the child
#define WRONG_ANSWER        1       // an answer differs from the model
#define WRONG_CONFIRMED     2       // a question was confirmed twice, too soon, too late or under no release
#define WRONG_WRITTEN       4       // a change was accepted while the release was closed or had to have ended
#define WRONG_NUMBER        8       // a ticket number was 0 or was given twice
#define WRONG_REPLACED      16      // a second question took the place of one that waited
#define WRONG_COVERAGE      32      // the walks did not reach what they are meant to reach
#define WRONG_FORETOLD      64      // access_may_ask() changed a field, or access_ask() then did something else

typedef struct
{
	long confirmed, too_soon, too_late, expired, refused_by_the_end, refused_by_switch, questions, second_questions, no_questions;
	long written, not_written, ended_by_itself, ended_at_the_latest, kept_open, steps_back, wraps_of_the_number, at_the_largest_time, previous_known;
	long foretold[4], closed_and_none, waiting_and_none;
} walk_counts_t;

static walk_counts_t walk_counts;

// Module and model asked the same things for the time `when`: true if every answer is the same
static bool same_answers(const access_t *walked, const model_t *model, uint64_t when, uint32_t extra_number)
{
	uint32_t newest = model_given > 0 ? model_numbers[model_given - 1] : model_number_before;
	const uint32_t numbers[] = {newest, newest - 1, newest - 2, newest + 1, 0, 1, UINT32_MAX, extra_number};
	access_ask_t asks[2];
	model_t at = *model;
	bool same;
	size_t i;

	// One of the three questions and one that is none (ACCESS_ASK_NONE or 4), other ones each time
	asks[0] = (access_ask_t)(ACCESS_ASK_WIFI + walk_random(3));
	asks[1] = (access_ask_t)(walk_random(2) * 4);
	model_pass(&at, when);
	same = access_is_open(walked, when) == at.open &&
	       access_seconds_left(walked, when) == (at.open ? model_whole_seconds(model_release_left(&at)) : 0) &&
	       access_asking(walked, when) == at.waiting &&
	       access_ask_seconds_left(walked, when) == (at.waiting != ACCESS_ASK_NONE ? model_whole_seconds(model_knob_left(&at)) : 0);
	for(i = 0; i < COUNT(numbers); i++)
	{
		access_ticket_t expected = model_ticket(&at, numbers[i]);

		if(access_ticket(walked, numbers[i], when) != expected) same = false;
		if(i == 1 && expected != ACCESS_TICKET_UNKNOWN) walk_counts.previous_known++;
	}
	for(i = 0; i < COUNT(asks); i++)
	{
		access_refusal_t expected = model_may_ask(model, asks[i], when);

		if(access_may_ask(walked, asks[i], when) != expected) same = false;
		walk_counts.foretold[expected]++;
		if(i == 1 && !at.open) walk_counts.closed_and_none++;
		if(i == 1 && at.waiting != ACCESS_ASK_NONE) walk_counts.waiting_and_none++;
	}
	return same;
}

// One walk: calls in a random order with times that mostly go on a little, often land on or next to the
// moment something runs out or the knob begins to count, sometimes jump far and sometimes step back. Now
// and then a release is kept open by a change every 9 minutes and a question is asked shortly before its
// latest end. Returns the WRONG_ bits.
static int walk(uint32_t walk_seed)
{
	static const uint64_t starts[] = {0, 0, 1000, 86400000, 4294967296ull - 30000, 4294967296ull - 30000, 4294967296ull + 5, 1099511627776ull,
	                                  UINT64_MAX - 3000000, UINT64_MAX - 700000};
	static const uint32_t numbers_before[] = {0, 0, 0, 41, 0x7FFFFFF0u, 0xFFFFFFF0u, 0xFFFFFFFEu, 0xFFFFFFFFu};
	static const uint64_t small_steps[] = {0, 0, 1, 7, 100, 999, 1000, 1001, 1499, 1500, 1501, 5000};
	static const uint64_t limit_steps[] = {59999, 60000, 60001, 539999, 540000, 540001, 599999, 600000, 600001};
	static const uint64_t huge_steps[] = {1799999, 1800000, 1800001, 3600000, 4294967296ull - 1, 4294967296ull + 5000};
	// How long before the latest end of the release a call lands: in its last milliseconds, where a question
	// cannot be shown long enough any more, where it cannot wait its time any more, and where a renewal just
	// reaches that end or would reach beyond it
	static const uint64_t before_the_latest_end[] = {1, 2, 1499, 1500, 1501, 30000, 59999, 60000, 60001, 599999, 600000, 600001};
	static const access_ask_t questions[] = {ACCESS_ASK_WIFI, ACCESS_ASK_WIFI, ACCESS_ASK_FIRMWARE, ACCESS_ASK_FIRMWARE, ACCESS_ASK_RESET, ACCESS_ASK_RESET,
	                                         ACCESS_ASK_NONE, (access_ask_t)4, (access_ask_t)99, (access_ask_t)-1};
	static uint32_t given[WALK_STEPS];
	access_t walked;
	model_t model;
	// The rules of safety are followed here a third time, with as little as they need: the latest time of
	// a call, when the release was given and when it was renewed, and the last ticket
	uint64_t now, clock = 0, given_at = 0, renewed_at = 0, asked_at = 0;
	uint32_t waiting_ticket = 0;
	access_ask_t waiting_question = ACCESS_ASK_NONE;
	bool released_now = false, confirmable = false;
	int given_count = 0, wrong = 0, at_the_end = 0, keeping = 0, step;

	walk_random_state = walk_seed;
	now = starts[walk_random(COUNT(starts))];
	model_number_before = numbers_before[walk_random(COUNT(numbers_before))];
	model_given = 0;
	memset(&model, 0, sizeof(model));
	access_init(&walked);
	// No walk is long enough to count up to the wrap of the numbers: the last one is written in by hand
	walked.ticket = model_number_before;

	// At the largest time nothing more can happen: a walk that got there ends after a few more calls
	for(step = 0; step < WALK_STEPS && wrong == 0 && at_the_end < 30; step++)
	{
		uint32_t operation = walk_random(100);
		uint32_t pace = walk_random(100);
		const char *what = "nothing";
		access_t after_the_call;
		model_t before;
		bool same = true;
		int probe;

		if(keeping > 1)
		{
			// A device in the WiFi writes every 9 minutes
			keeping--;
			now = later(model.seen, 540000);
			operation = 17;
			walk_counts.kept_open++;
		}
		else if(keeping == 1)
		{
			// and asks a question shortly before the latest end of the release it kept open
			keeping = 0;
			now = earlier(later(model.seen, model.latest_left), before_the_latest_end[walk_random(COUNT(before_the_latest_end))]);
			operation = 35;
		}
		else if(pace < 3) now = earlier(now, 5);
		else if(pace < 4) now = 0;
		else if(pace < 6) now = earlier(now, 700000);
		else if(pace < 24)
		{
			// The moment the question or the release runs out or the knob begins to count, or a moment shortly
			// before the latest end of the release - and 1 ms before or behind each
			uint64_t latest_end = later(model.seen, model.latest_left);
			uint64_t moment;
			uint32_t around;

			switch(walk_random(6))
			{
				case 0: moment = later(model.seen, model.waiting_left); break;
				case 1: moment = later(model.seen, model.open_left); break;
				case 2: moment = latest_end; break;
				case 3:
				case 4: moment = later(model.seen, model.waited < 1500 ? 1500 - model.waited : 0); break;
				default: moment = earlier(latest_end, before_the_latest_end[walk_random(COUNT(before_the_latest_end))]); break;
			}
			around = walk_random(3);
			now = around == 0 ? earlier(moment, 1) : around == 1 ? moment : later(moment, 1);
		}
		else if(pace < 87) now = later(now, small_steps[walk_random(COUNT(small_steps))]);
		else if(pace < 97) now = later(now, limit_steps[walk_random(COUNT(limit_steps))]);
		else if(pace < 98) now = later(now, huge_steps[walk_random(COUNT(huge_steps))]);

		// The model at this moment before the call, for the counts
		before = model;
		model_pass(&before, now);

		if(operation < 90)
		{
			if(now < clock) walk_counts.steps_back++;
			else clock = now;
			if(clock == UINT64_MAX)
			{
				walk_counts.at_the_largest_time++;
				at_the_end++;
			}
			if(model.waiting != ACCESS_ASK_NONE && before.waiting == ACCESS_ASK_NONE)
			{
				if(before.newest_end == ACCESS_TICKET_EXPIRED) walk_counts.expired++;
				else walk_counts.refused_by_the_end++;
			}
			if(model.open && !before.open)
			{
				walk_counts.ended_by_itself++;
				if(model.latest_left < model.open_left) walk_counts.ended_at_the_latest++;
			}
			if(released_now && (clock - renewed_at >= 600000 || clock - given_at >= 1800000)) released_now = false;
		}

		if(operation < 14)
		{
			access_open(&walked, now);
			model_open(&model, now);
			released_now = true;
			given_at = clock;
			renewed_at = clock;
			if(walk_random(8) == 0) keeping = 4;
			what = "open";
		}
		else if(operation < 17)
		{
			access_close(&walked, now);
			model_close(&model, now);
			if(before.waiting != ACCESS_ASK_NONE) walk_counts.refused_by_switch++;
			released_now = false;
			confirmable = false;
			what = "close";
		}
		else if(operation < 35)
		{
			bool written = access_write(&walked, now);

			same = written == model_write(&model, now);
			if(written && !released_now) wrong |= WRONG_WRITTEN;
			if(written) renewed_at = clock;
			if(written) walk_counts.written++;
			else walk_counts.not_written++;
			what = "write";
		}
		else if(operation < 63)
		{
			access_ask_t question = questions[walk_random(COUNT(questions))];
			access_ask_t waited = access_asking(&walked, now);
			uint32_t left = access_ask_seconds_left(&walked, now);
			access_t untouched = walked;
			access_refusal_t foretold = access_may_ask(&walked, question, now);
			bool as_the_model = foretold == model_may_ask(&model, question, now);
			uint32_t number;
			int i;

			if(!same_fields(&walked, &untouched)) wrong |= WRONG_FORETOLD;
			number = access_ask(&walked, question, now);
			if((foretold == ACCESS_ALLOWED) != (number != 0)) wrong |= WRONG_FORETOLD;
			same = as_the_model && number == model_ask(&model, question, now);
			if(waited != ACCESS_ASK_NONE)
			{
				// One waited: it has to wait on as it was, and the second gets no ticket
				if(number != 0 || access_asking(&walked, now) != waited || access_ask_seconds_left(&walked, now) != left ||
				   access_ticket(&walked, waiting_ticket, now) != ACCESS_TICKET_WAITING) wrong |= WRONG_REPLACED;
				walk_counts.second_questions++;
			}
			else if(number == 0)
			{
				// No ticket: then nothing may wait
				if(access_asking(&walked, now) != ACCESS_ASK_NONE) wrong |= WRONG_NUMBER;
				walk_counts.no_questions++;
			}
			if(number != 0)
			{
				for(i = 0; i < given_count; i++)
				{
					if(given[i] == number) wrong |= WRONG_NUMBER;
				}
				given[given_count++] = number;
				if(!released_now) wrong |= WRONG_WRITTEN;
				if(number == 1 && model_number_before != 0) walk_counts.wraps_of_the_number++;
				waiting_ticket = number;
				waiting_question = question;
				asked_at = clock;
				renewed_at = clock;
				confirmable = true;
				walk_counts.questions++;
			}
			what = "ask";
		}
		else if(operation < 85)
		{
			access_ask_t question = access_confirm(&walked, now);

			same = question == model_confirm(&model, now);
			if(question != ACCESS_ASK_NONE)
			{
				// Only the question of the last ticket, once, not too soon, within its time, under a release that holds
				if(!confirmable || question != waiting_question || clock - asked_at < 1500 || clock - asked_at >= 60000 || !released_now) wrong |= WRONG_CONFIRMED;
				confirmable = false;
				walk_counts.confirmed++;
			}
			else if(before.waiting != ACCESS_ASK_NONE)
			{
				walk_counts.too_soon++;
			}
			else if(before.newest_end == ACCESS_TICKET_EXPIRED)
			{
				walk_counts.too_late++;
			}
			what = "confirm";
		}
		else if(operation < 90)
		{
			access_refuse(&walked, now);
			model_refuse(&model, now);
			confirmable = false;
			what = "refuse";
		}

		// After every call: the time seen, and the same answers for now, for the moments around the end of the
		// question and one of the two ends of the release, and for some other time before or after. The
		// questions asked for that change no field.
		if(!same || walked.clock_ms != model.seen)
		{
			printf("  walk %lu, step %d, %s at %llu ms: the answer or the time seen differs from the model\n",
			       (unsigned long)walk_seed, step, what, (unsigned long long)now);
			wrong |= WRONG_ANSWER;
		}
		after_the_call = walked;
		for(probe = 0; probe < 4 && wrong == 0; probe++)
		{
			uint64_t when = now;

			if(probe == 1) when = later(earlier(later(model.seen, model.waiting_left), 1), walk_random(3));
			if(probe == 2)
			{
				uint64_t end = later(model.seen, walk_random(2) != 0 ? model.open_left : model.latest_left);

				when = later(earlier(end, 1), walk_random(3));
			}
			if(probe == 3) when = walk_random(2) != 0 ? earlier(now, walk_random(2000)) : later(now, walk_random(700000));
			if(!same_answers(&walked, &model, when, walk_random(8) == 0 ? walk_random_state : waiting_ticket))
			{
				printf("  walk %lu, step %d, after %s at %llu ms: asked for %llu ms, the answers differ from the model\n",
				       (unsigned long)walk_seed, step, what, (unsigned long long)now, (unsigned long long)when);
				wrong |= WRONG_ANSWER;
			}
		}
		if(!same_fields(&walked, &after_the_call))
		{
			printf("  walk %lu, step %d, after %s at %llu ms: a function that only asks changed a field\n",
			       (unsigned long)walk_seed, step, what, (unsigned long long)now);
			wrong |= WRONG_ANSWER;
		}
	}
	return wrong;
}

static void test_walk_against_the_model(void)
{
	int status = 0, wrong = 0;
	uint32_t walk_seed;
	bool ended;
	pid_t child;

	// In a child process: a crash or a hang of the module is then a failed check here, not the end of the test
	fflush(stdout);
	child = fork();
	if(child == 0)
	{
		alarm(120);
		for(walk_seed = 1; walk_seed <= WALKS; walk_seed++) wrong |= walk(walk_seed);
		printf("  walks: %ld questions, %ld confirmed, %ld pressed too soon, %ld pressed too late, %ld left to expire, %ld refused by the end of the release, "
		       "%ld refused by the switch-off, %ld second questions, %ld refused otherwise, %ld changes accepted, %ld refused, %ld releases ended by themselves, "
		       "%ld of them at the latest end, %ld changes of a device that writes every 9 minutes, %ld steps back, %ld wraps of the number, "
		       "%ld calls at the largest time, %ld answers about the ticket before the last, foretold %ld allowed, %ld closed, %ld bad question, %ld asking, "
		       "%ld no question under a closed release, %ld no question while one waits\n",
		       walk_counts.questions, walk_counts.confirmed, walk_counts.too_soon, walk_counts.too_late, walk_counts.expired, walk_counts.refused_by_the_end,
		       walk_counts.refused_by_switch, walk_counts.second_questions, walk_counts.no_questions, walk_counts.written, walk_counts.not_written,
		       walk_counts.ended_by_itself, walk_counts.ended_at_the_latest, walk_counts.kept_open, walk_counts.steps_back, walk_counts.wraps_of_the_number,
		       walk_counts.at_the_largest_time, walk_counts.previous_known, walk_counts.foretold[ACCESS_ALLOWED], walk_counts.foretold[ACCESS_CLOSED],
		       walk_counts.foretold[ACCESS_BAD_QUESTION], walk_counts.foretold[ACCESS_ASKING], walk_counts.closed_and_none, walk_counts.waiting_and_none);
		if(walk_counts.questions < 30000 || walk_counts.confirmed < 10000 || walk_counts.too_soon < 5000 || walk_counts.too_late < 10000 || walk_counts.expired < 10000 ||
		   walk_counts.refused_by_the_end < 1000 || walk_counts.refused_by_switch < 1000 || walk_counts.second_questions < 10000 || walk_counts.no_questions < 10000 ||
		   walk_counts.written < 30000 || walk_counts.not_written < 30000 || walk_counts.ended_by_itself < 10000 || walk_counts.ended_at_the_latest < 3000 ||
		   walk_counts.kept_open < 10000 || walk_counts.steps_back < 50000 || walk_counts.wraps_of_the_number < 100 || walk_counts.at_the_largest_time < 2000 ||
		   walk_counts.previous_known < 1000000 || walk_counts.foretold[ACCESS_ALLOWED] < 300000 || walk_counts.foretold[ACCESS_CLOSED] < 300000 ||
		   walk_counts.foretold[ACCESS_BAD_QUESTION] < 300000 || walk_counts.foretold[ACCESS_ASKING] < 100000 || walk_counts.closed_and_none < 300000 ||
		   walk_counts.waiting_and_none < 100000) wrong |= WRONG_COVERAGE;
		fflush(stdout);
		_exit(wrong);
	}
	ended = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status);
	wrong = ended ? WEXITSTATUS(status) : 0xFF;
	check(ended, "1500 random walks of up to 800 calls each: no crash and no hang");
	check((wrong & WRONG_COVERAGE) == 0, "the walks reach every kind of end, refusal and foretelling, the press too soon, the latest end of the release, step back, the wrap of the ticket number and the largest time in numbers");
	check((wrong & WRONG_ANSWER) == 0, "in the walks every answer after every call is the one of the model, and no function that only asks changes a field");
	check((wrong & WRONG_CONFIRMED) == 0, "in the walks the knob confirms a question at most once per ticket, never sooner than 1500 ms after it was asked, never after its time, never under a release that ended or was switched off");
	check((wrong & WRONG_WRITTEN) == 0, "in the walks no change and no question is accepted while the release is closed, none later than 600000 ms after the last one and none later than 1800000 ms after the release was given");
	check((wrong & WRONG_FORETOLD) == 0, "in the walks access_may_ask() changes no field, and access_ask() with the same time gives a ticket exactly if it said allowed");
	check((wrong & WRONG_NUMBER) == 0, "in the walks no ticket number is 0 and none is given twice");
	check((wrong & WRONG_REPLACED) == 0, "in the walks a second question never takes the place of one that waits");
}

int main(void)
{
	test_constants();
	test_init();
	test_release();
	test_question();
	test_question_refused();
	test_tickets();
	test_refuse();
	test_switched_off_with_a_question();
	test_ends_noticed_late();
	test_release_ends_with_a_question();
	test_every_question();
	test_ticket_before_the_last();
	test_many_tickets();
	test_fields();
	test_clock();
	test_largest_time();
	test_latest_end();
	test_question_at_the_latest_end();
	test_press_too_soon();
	test_may_ask();
	test_walk_against_the_model();
	return test_end();
}
