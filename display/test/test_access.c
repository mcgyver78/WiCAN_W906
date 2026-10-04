/*
 * Host test for display/components/core/access.c. Run "make test_access && ./test_access" in display/test.
 * redproof.py removes or weakens every rule once (mutations/access.py) and expects this test to fail.
 *
 * The scenes below all use the same times: the release is given at 1000 and ends at 601000. A question is
 * asked at 2000 and waits until 62000; it renews the release, which then ends at 602000.
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
// functions never get there: a question renews the release for ten minutes and waits for one. So the end
// of the release is written into the struct by hand.
static void asked_under_a_release_until(uint64_t release_end)
{
	asked(ACCESS_ASK_WIFI);
	acc.open_until_ms = release_end;
}

static void test_constants(void)
{
	check(ACCESS_OPEN_MS == 600000 && ACCESS_CONFIRM_MS == 60000, "the release lasts 600000 ms, a question waits 60000 ms");
}

static void test_init(void)
{
	// Whatever stood in the memory before
	acc.open = true;
	acc.open_until_ms = 999999;
	acc.clock_ms = 5000;
	acc.asking = ACCESS_ASK_RESET;
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

	check(asked(ACCESS_ASK_FIRMWARE) == 1 && access_asking(&acc, 2000) == ACCESS_ASK_FIRMWARE && access_confirm(&acc, 3000) == ACCESS_ASK_FIRMWARE,
	      "the question for the firmware is shown and confirmed as that");
	check(asked(ACCESS_ASK_RESET) == 1 && access_asking(&acc, 2000) == ACCESS_ASK_RESET && access_confirm(&acc, 3000) == ACCESS_ASK_RESET,
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
	released();
	access_close(&acc, 1500);
	check(access_ask(&acc, ACCESS_ASK_WIFI, 2000) == 0, "a question after the release was switched off is refused");

	released();
	check(access_ask(&acc, ACCESS_ASK_NONE, 2000) == 0, "ACCESS_ASK_NONE is no question: refused");
	check(access_ask(&acc, (access_ask_t)4, 2000) == 0, "the value behind the last of the enum is no question: refused");
	check(access_ask(&acc, (access_ask_t)99, 2000) == 0 && access_ask(&acc, (access_ask_t)-1, 2000) == 0, "99 and -1 are no questions: refused");
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
	access_confirm(&acc, 3000);
	check(access_ticket(&acc, UINT32_MAX, 3000) == ACCESS_TICKET_UNKNOWN && access_ticket(&acc, 0, 3000) == ACCESS_TICKET_UNKNOWN &&
	      access_ticket(&acc, 2, 3000) == ACCESS_TICKET_UNKNOWN, "with one ticket given the numbers 0, 2 and 2^32-1 are unknown");
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

	// No test can ask for 4294967294 tickets: the number of the last one is written into the struct by hand
	released();
	acc.ticket = UINT32_MAX - 1;
	check(access_ask(&acc, ACCESS_ASK_WIFI, 2000) == UINT32_MAX, "the ticket after 4294967294 is 4294967295");
	access_confirm(&acc, 3000);
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
	access_confirm(&acc, 3000);
	check(access_ask(&acc, ACCESS_ASK_WIFI, 4000) == 0x80000000u && access_ticket(&acc, 0x7FFFFFFFu, 4000) == ACCESS_TICKET_CONFIRMED,
	      "the numbers count on across 2^31");
}

static void test_refuse(void)
{
	asked(ACCESS_ASK_WIFI);
	access_refuse(&acc, 3000);
	check(access_ticket(&acc, 1, 3000) == ACCESS_TICKET_REFUSED, "the question refused at the device: its ticket is refused");
	check(access_asking(&acc, 3000) == ACCESS_ASK_NONE && access_ask_seconds_left(&acc, 3000) == 0, "after the refusal nothing waits");
	check(access_confirm(&acc, 3000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 3000) == ACCESS_TICKET_REFUSED, "a refused question cannot be confirmed");
	check(access_is_open(&acc, 601999) && !access_is_open(&acc, 602000), "refusing a question neither ends nor renews the release");
	check(access_ask(&acc, ACCESS_ASK_RESET, 3000) == 2, "after a refusal the next question is accepted");

	asked(ACCESS_ASK_WIFI);
	access_confirm(&acc, 3000);
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
	check(access_confirm(&acc, 3000) == ACCESS_ASK_NONE, "the knob pressed after the release was switched off confirms nothing");
	access_open(&acc, 4000);
	check(access_asking(&acc, 4000) == ACCESS_ASK_NONE && access_confirm(&acc, 4000) == ACCESS_ASK_NONE && access_ticket(&acc, 1, 4000) == ACCESS_TICKET_REFUSED,
	      "a new release does not bring back the question of the one that was switched off");

	asked(ACCESS_ASK_WIFI);
	access_close(&acc, 62000);
	check(access_ticket(&acc, 1, 62000) == ACCESS_TICKET_EXPIRED, "switched off 60000 ms after the question: it had expired before, it was not refused");

	asked(ACCESS_ASK_WIFI);
	access_confirm(&acc, 3000);
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

/*
 * The same rules written a second time for the walk below: with times that are counted down instead of
 * points in time that are compared, with the events worked off in the order they happen, and with a list
 * of all tickets instead of the ends of the last two.
 */
#define WALKS           1500
#define WALK_STEPS      800

typedef struct
{
	uint64_t seen;              // the latest time of a call that may change something
	bool open;
	uint64_t open_left;         // ms the release still lasts, counted from `seen`
	access_ask_t waiting;
	uint64_t waiting_left;      // ms the question still waits, counted from `seen`
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

// Lets the time pass up to now_ms and works off what runs out on the way, the earlier event first
static void model_pass(model_t *model, uint64_t now_ms)
{
	uint64_t passed = now_ms > model->seen ? now_ms - model->seen : 0;

	model->seen += passed;
	for(;;)
	{
		bool question_due = model->waiting != ACCESS_ASK_NONE && model->waiting_left <= passed;
		bool release_due = model->open && model->open_left <= passed;

		if(question_due && (!release_due || model->waiting_left <= model->open_left))
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
	if(model->open) model->open_left -= passed;
	if(model->waiting != ACCESS_ASK_NONE) model->waiting_left -= passed;
}

static void model_open(model_t *model, uint64_t now_ms)
{
	model_pass(model, now_ms);
	model->open = true;
	model->open_left = model_room(model->seen, 600000);
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
	model->newest_end = ACCESS_TICKET_WAITING;
	return number;
}

static access_ask_t model_confirm(model_t *model, uint64_t now_ms)
{
	access_ask_t question;

	model_pass(model, now_ms);
	question = model->waiting;
	if(question != ACCESS_ASK_NONE) model_end_question(model, ACCESS_TICKET_CONFIRMED);
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
#define WRONG_CONFIRMED     2       // a question was confirmed twice, too late or under no release
#define WRONG_WRITTEN       4       // a change was accepted while the release was closed
#define WRONG_NUMBER        8       // a ticket number was 0 or was given twice
#define WRONG_REPLACED      16      // a second question took the place of one that waited
#define WRONG_COVERAGE      32      // the walks did not reach what they are meant to reach

typedef struct
{
	long confirmed, too_late, expired, refused_by_switch, questions, second_questions, no_questions;
	long written, not_written, ended_by_itself, steps_back, wraps_of_the_number, at_the_largest_time, previous_known;
} walk_counts_t;

static walk_counts_t walk_counts;

// Module and model asked the same things for the time `when`: true if every answer is the same
static bool same_answers(const access_t *walked, const model_t *model, uint64_t when, uint32_t extra_number)
{
	uint32_t newest = model_given > 0 ? model_numbers[model_given - 1] : model_number_before;
	const uint32_t numbers[] = {newest, newest - 1, newest - 2, newest + 1, 0, 1, UINT32_MAX, extra_number};
	model_t at = *model;
	bool same;
	size_t i;

	model_pass(&at, when);
	same = access_is_open(walked, when) == at.open &&
	       access_seconds_left(walked, when) == (at.open ? model_whole_seconds(at.open_left) : 0) &&
	       access_asking(walked, when) == at.waiting &&
	       access_ask_seconds_left(walked, when) == (at.waiting != ACCESS_ASK_NONE ? model_whole_seconds(at.waiting_left) : 0);
	for(i = 0; i < COUNT(numbers); i++)
	{
		access_ticket_t expected = model_ticket(&at, numbers[i]);

		if(access_ticket(walked, numbers[i], when) != expected) same = false;
		if(i == 1 && expected != ACCESS_TICKET_UNKNOWN) walk_counts.previous_known++;
	}
	return same;
}

// One walk: calls in a random order with times that mostly go on a little, often land on or next to the
// moment something runs out, sometimes jump far and sometimes step back. Returns the WRONG_ bits.
static int walk(uint32_t walk_seed)
{
	static const uint64_t starts[] = {0, 0, 1000, 86400000, 4294967296ull - 30000, 4294967296ull - 30000, 4294967296ull + 5, 1099511627776ull,
	                                  UINT64_MAX - 3000000, UINT64_MAX - 700000};
	static const uint32_t numbers_before[] = {0, 0, 0, 41, 0x7FFFFFF0u, 0xFFFFFFF0u, 0xFFFFFFFEu, 0xFFFFFFFFu};
	static const uint64_t small_steps[] = {0, 0, 1, 7, 100, 999, 1000, 1001, 5000};
	static const uint64_t limit_steps[] = {59999, 60000, 60001, 539999, 540000, 540001, 599999, 600000, 600001};
	static const uint64_t huge_steps[] = {3600000, 4294967296ull - 1, 4294967296ull + 5000};
	static const access_ask_t questions[] = {ACCESS_ASK_WIFI, ACCESS_ASK_WIFI, ACCESS_ASK_FIRMWARE, ACCESS_ASK_FIRMWARE, ACCESS_ASK_RESET, ACCESS_ASK_RESET,
	                                         ACCESS_ASK_NONE, (access_ask_t)4, (access_ask_t)99, (access_ask_t)-1};
	static uint32_t given[WALK_STEPS];
	access_t walked;
	model_t model;
	// The rules of safety are followed here a third time, with as little as they need: the latest time of
	// a call, when the release was given or renewed, and the last ticket
	uint64_t now, clock = 0, renewed_at = 0, asked_at = 0;
	uint32_t waiting_ticket = 0;
	access_ask_t waiting_question = ACCESS_ASK_NONE;
	bool released_now = false, confirmable = false;
	int given_count = 0, wrong = 0, at_the_end = 0, step;

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
		model_t before;
		bool same = true;
		int probe;

		if(pace < 3) now = earlier(now, 5);
		else if(pace < 4) now = 0;
		else if(pace < 6) now = earlier(now, 700000);
		else if(pace < 20)
		{
			// The moment the question or the release runs out, or 1 ms before or behind it
			uint64_t end = later(model.seen, walk_random(2) != 0 ? model.waiting_left : model.open_left);
			uint32_t around = walk_random(3);

			now = around == 0 ? earlier(end, 1) : around == 1 ? end : later(end, 1);
		}
		else if(pace < 86) now = later(now, small_steps[walk_random(COUNT(small_steps))]);
		else if(pace < 97) now = later(now, limit_steps[walk_random(COUNT(limit_steps))]);
		else if(pace < 98) now = later(now, huge_steps[walk_random(COUNT(huge_steps))]);

		// The model at this moment before the call, for the counts
		before = model;
		model_pass(&before, now);

		if(operation < 86)
		{
			if(now < clock) walk_counts.steps_back++;
			else clock = now;
			if(clock == UINT64_MAX)
			{
				walk_counts.at_the_largest_time++;
				at_the_end++;
			}
			if(model.waiting != ACCESS_ASK_NONE && before.waiting == ACCESS_ASK_NONE) walk_counts.expired++;
			if(model.open && !before.open) walk_counts.ended_by_itself++;
			if(released_now && clock - renewed_at >= 600000) released_now = false;
		}

		if(operation < 14)
		{
			access_open(&walked, now);
			model_open(&model, now);
			released_now = true;
			renewed_at = clock;
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
			uint32_t number = access_ask(&walked, question, now);
			int i;

			same = number == model_ask(&model, question, now);
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
		else if(operation < 81)
		{
			access_ask_t question = access_confirm(&walked, now);

			same = question == model_confirm(&model, now);
			if(question != ACCESS_ASK_NONE)
			{
				// Only the question of the last ticket, once, within its time, under a release that holds
				if(!confirmable || question != waiting_question || clock - asked_at >= 60000 || !released_now) wrong |= WRONG_CONFIRMED;
				confirmable = false;
				walk_counts.confirmed++;
			}
			else if(before.newest_end == ACCESS_TICKET_EXPIRED)
			{
				walk_counts.too_late++;
			}
			what = "confirm";
		}
		else if(operation < 86)
		{
			access_refuse(&walked, now);
			model_refuse(&model, now);
			confirmable = false;
			what = "refuse";
		}

		// After every call: the time seen, and the same answers for now, for the moments around the two ends
		// and for some other time before or after
		if(!same || walked.clock_ms != model.seen)
		{
			printf("  walk %lu, step %d, %s at %llu ms: the answer or the time seen differs from the model\n",
			       (unsigned long)walk_seed, step, what, (unsigned long long)now);
			wrong |= WRONG_ANSWER;
		}
		for(probe = 0; probe < 4 && wrong == 0; probe++)
		{
			uint64_t when = now;

			if(probe == 1) when = later(earlier(later(model.seen, model.waiting_left), 1), walk_random(3));
			if(probe == 2) when = later(earlier(later(model.seen, model.open_left), 1), walk_random(3));
			if(probe == 3) when = walk_random(2) != 0 ? earlier(now, walk_random(2000)) : later(now, walk_random(700000));
			if(!same_answers(&walked, &model, when, walk_random(8) == 0 ? walk_random_state : waiting_ticket))
			{
				printf("  walk %lu, step %d, after %s at %llu ms: asked for %llu ms, the answers differ from the model\n",
				       (unsigned long)walk_seed, step, what, (unsigned long long)now, (unsigned long long)when);
				wrong |= WRONG_ANSWER;
			}
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
		printf("  walks: %ld questions, %ld confirmed, %ld pressed too late, %ld left to expire, %ld refused by the switch-off, %ld second questions, "
		       "%ld refused otherwise, %ld changes accepted, %ld refused, %ld releases ended by themselves, %ld steps back, %ld wraps of the number, "
		       "%ld calls at the largest time, %ld answers about the ticket before the last\n",
		       walk_counts.questions, walk_counts.confirmed, walk_counts.too_late, walk_counts.expired, walk_counts.refused_by_switch,
		       walk_counts.second_questions, walk_counts.no_questions, walk_counts.written, walk_counts.not_written, walk_counts.ended_by_itself,
		       walk_counts.steps_back, walk_counts.wraps_of_the_number, walk_counts.at_the_largest_time, walk_counts.previous_known);
		if(walk_counts.questions < 30000 || walk_counts.confirmed < 10000 || walk_counts.too_late < 10000 || walk_counts.expired < 10000 ||
		   walk_counts.refused_by_switch < 1000 || walk_counts.second_questions < 10000 || walk_counts.no_questions < 10000 ||
		   walk_counts.written < 30000 || walk_counts.not_written < 30000 || walk_counts.ended_by_itself < 10000 || walk_counts.steps_back < 50000 ||
		   walk_counts.wraps_of_the_number < 100 || walk_counts.at_the_largest_time < 2000 || walk_counts.previous_known < 1000000) wrong |= WRONG_COVERAGE;
		fflush(stdout);
		_exit(wrong);
	}
	ended = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status);
	wrong = ended ? WEXITSTATUS(status) : 0xFF;
	check(ended, "1500 random walks of up to 800 calls each: no crash and no hang");
	check((wrong & WRONG_COVERAGE) == 0, "the walks reach every kind of end, refusal, step back, the wrap of the ticket number and the largest time in numbers");
	check((wrong & WRONG_ANSWER) == 0, "in the walks every answer after every call is the one of the model");
	check((wrong & WRONG_CONFIRMED) == 0, "in the walks the knob confirms a question at most once per ticket, never after its time, never under a release that ended or was switched off");
	check((wrong & WRONG_WRITTEN) == 0, "in the walks no change and no question is accepted while the release is closed");
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
	test_clock();
	test_largest_time();
	test_walk_against_the_model();
	return test_end();
}
