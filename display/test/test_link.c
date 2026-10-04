/*
 * Host test for display/components/core/link.c. Run "make test_link && ./test_link" in display/test.
 * redproof.py removes or weakens every rule once (mutations/link.py) and expects this test to fail.
 *
 * Two kinds of checks:
 *   - examples: one call sequence, one expected outcome, named after the rule of link.h
 *   - a walk: long pseudo-random call sequences in a child process. After every call the module is compared
 *     with a second implementation of the rules further down in this file (the model), and the actions it
 *     hands out with what the header promises about them, whatever the model says.
 *
 * The scenes of the examples all use the same times: the display starts at 0, the first scan is handed out
 * at 1000 and ends at 3000, the join is handed out at 3000 and succeeds at 5000, the query is handed out at
 * 5000 and finds the adapter at 6000.
 */
#include <limits.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include "test.h"
#include "link.h"

#define GATEWAY     "192.168.80.1"
#define ADAPTER     "192.168.1.77"      // what a query finds
#define MOVED       "192.168.1.78"      // what a later query finds

// A router (the service is queried), the access point of a WiCAN (the gateway), two networks with an
// address stored
static const net_profile_t HOME[1] = {{"Home", "homepassword", ""}};
static const net_profile_t WICAN[1] = {{"WiCAN_a1b2c3d4e5f6", "@meatpi#", ""}};
static const net_profile_t GARAGE[1] = {{"Garage", "", "192.168.1.50"}};
static const net_profile_t ALL[4] = {
	{"Home", "homepassword", ""}, {"WiCAN_a1b2c3d4e5f6", "@meatpi#", ""}, {"Garage", "", "192.168.1.50"}, {"Phone", "phonepass", "wican.local"},
};
static const net_profile_t REVERSED[3] = {{"Garage", "", "192.168.1.51"}, {"WiCAN_a1b2c3d4e5f6", "@meatpi#", ""}, {"Home", "homepassword", ""}};

static const char EVERYTHING[5][NET_SSID_SIZE] = {"Neighbour", "Phone", "Garage", "WiCAN_a1b2c3d4e5f6", "Home"};
static const char STRANGERS[2][NET_SSID_SIZE] = {"Neighbour", "FRITZ!Box 7590"};
static const char TWO[2][NET_SSID_SIZE] = {"Garage", "WiCAN_a1b2c3d4e5f6"};
static const char TWO_SWAPPED[2][NET_SSID_SIZE] = {"WiCAN_a1b2c3d4e5f6", "Garage"};
static const char ONLY_PHONE[2][NET_SSID_SIZE] = {"Neighbour", "Phone"};
static const char ONLY_HOME[2][NET_SSID_SIZE] = {"Neighbour", "Home"};
// A crowded place: the 20th network is "Garage", the 21st "Home", the 22nd the WiCAN
static const char CROWD[25][NET_SSID_SIZE] = {
	"Net01", "Net02", "Net03", "Net04", "Net05", "Net06", "Net07", "Net08", "Net09", "Net10",
	"Net11", "Net12", "Net13", "Net14", "Net15", "Net16", "Net17", "Net18", "Net19", "Garage",
	"Home", "WiCAN_a1b2c3d4e5f6", "Net23", "Net24", "Net25",
};

static link_t wifi;

static link_do_t next(uint64_t now_ms)
{
	return link_next(&wifi, now_ms);
}

// true if link_next has nothing to do at every `step` ms from `from` to `to`
static bool quiet(uint64_t from, uint64_t to, uint64_t step)
{
	uint64_t now;

	for(now = from; now <= to; now += step)
	{
		if(next(now) != LINK_DO_NOTHING) return false;
	}
	return true;
}

// What the caller sees of the link
static bool shows(link_phase_t phase, int profile, const char *host)
{
	return wifi.phase == phase && link_profile(&wifi) == profile && strcmp(link_host(&wifi), host) == 0 &&
	       link_up(&wifi) == (phase == LINK_UP);
}

// Every member but the time
static bool same_link(const link_t *a, const link_t *b)
{
	return a->phase == b->phase && a->profiles == b->profiles && a->profile_count == b->profile_count && a->profile == b->profile &&
	       a->tries == b->tries && a->wait_step == b->wait_step && a->wait_until_ms == b->wait_until_ms &&
	       a->action_since_ms == b->action_since_ms && a->busy == b->busy && a->finding == b->finding && a->find_at_ms == b->find_at_ms &&
	       a->host_from_query == b->host_from_query && strcmp(a->host, b->host) == 0 && a->no_answer == b->no_answer &&
	       a->no_answer_since_ms == b->no_answer_since_ms && a->ap_wanted == b->ap_wanted && a->ap_on == b->ap_on &&
	       a->ap_forced == b->ap_forced && a->ap_clients == b->ap_clients && a->ap_idle_since_ms == b->ap_idle_since_ms &&
	       a->changed == b->changed;
}

// A text of `length` bytes, at most 120
static const char *text_of(size_t length)
{
	static char text[121];

	memset(text, 'h', sizeof(text));
	text[length] = '\0';
	return text;
}

/* ------------------------------------------------------------------------------------------------ */
/* Scenes                                                                                             */
/* ------------------------------------------------------------------------------------------------ */

// The display started at 0 with these profiles, the first scan is handed out at 1000
static link_do_t scanning(const net_profile_t *profiles, int count)
{
	link_init(&wifi, profiles, count, false, 0);
	return next(1000);
}

// ... the scan saw every network of the examples and ended at 3000, the join is handed out at 3000
static link_do_t joining(const net_profile_t *profiles, int count)
{
	scanning(profiles, count);
	link_scanned(&wifi, EVERYTHING, 5, 3000);
	return next(3000);
}

// ... the join succeeded at 5000
static void joined(const net_profile_t *profiles, int count, const char *gateway)
{
	joining(profiles, count);
	link_joined(&wifi, gateway, 5000);
}

// In "Home", the query is handed out at 5000
static link_do_t finding(void)
{
	joined(HOME, 1, GATEWAY);
	return next(5000);
}

// ... and found the adapter at 6000
static void up_by_query(void)
{
	finding();
	link_found(&wifi, ADAPTER, 6000);
}

// The scan that is under way saw no stored network and ended at `now_ms`
static void scan_failed(uint64_t now_ms)
{
	link_scanned(&wifi, STRANGERS, 2, now_ms);
}

// A scan handed out at `now_ms` sees "Home", both attempts to join fail, the second 1000 ms later. Returns
// false if the actions are not handed out as expected.
static bool joins_fail(uint64_t now_ms)
{
	if(next(now_ms) != LINK_DO_SCAN) return false;
	link_scanned(&wifi, ONLY_HOME, 2, now_ms);
	if(next(now_ms) != LINK_DO_JOIN) return false;
	link_join_failed(&wifi, now_ms + 500);
	if(next(now_ms + 500) != LINK_DO_JOIN) return false;
	link_join_failed(&wifi, now_ms + 1000);
	return wifi.phase == LINK_WAITING;
}

/* ------------------------------------------------------------------------------------------------ */
/* Examples                                                                                           */
/* ------------------------------------------------------------------------------------------------ */

static void test_constants(void)
{
	static const uint32_t waits[] = LINK_SCAN_WAITS_MS;

	check(sizeof(waits) / sizeof(waits[0]) == 4 && waits[0] == 2000 && waits[1] == 5000 && waits[2] == 10000 && waits[3] == 30000,
	      "the waits between scans are 2 s, 5 s, 10 s and 30 s");
	check(LINK_JOIN_TRIES == 2 && LINK_JOIN_TIMEOUT_MS == 15000, "two attempts to join a network that was seen, each 15 s at most");
	check(LINK_FIND_RETRY_MS == 10000 && LINK_FIND_AGAIN_MS == 60000, "a query is repeated after 10 s, an address that is silent for 60 s is queried again");
	check(LINK_AP_IDLE_MS == 600000, "the own access point closes after 600 s without a client");
	check(LINK_HOST_SIZE == 40 && LINK_HOST_SIZE == NET_HOST_SIZE && LINK_SEEN_MAX == 20, "an address has 39 bytes at most, 20 networks of a scan are looked at");
}

static void test_start(void)
{
	memset(&wifi, 0xA5, sizeof(wifi));
	link_init(&wifi, HOME, 0, false, 0);
	check(shows(LINK_IDLE, -1, "") && !link_ap_on(&wifi), "no profile stored: idle after the start, whatever stood in the memory before");
	check(next(0) == LINK_DO_AP_ON && link_ap_on(&wifi), "no profile stored: the first link_next orders the own access point on");
	check(quiet(0, 3600000, 1000) && shows(LINK_IDLE, -1, "") && link_ap_on(&wifi), "no profile stored: nothing is scanned and the access point stays on, however long");

	memset(&wifi, 0xA5, sizeof(wifi));
	link_init(&wifi, HOME, 1, false, 0);
	check(shows(LINK_WAITING, -1, "") && !link_ap_on(&wifi), "a profile stored: waiting for the first scan after the start, whatever stood in the memory before");
	check(next(0) == LINK_DO_SCAN && shows(LINK_SCANNING, -1, ""), "a profile stored: the first link_next orders a scan");
	check(quiet(0, 3600000, 1000) && shows(LINK_SCANNING, -1, "") && !link_ap_on(&wifi),
	      "a scan is handed out once and has no time limit: nothing while it is under way, and no access point");

	link_init(&wifi, HOME, 1, true, 0);
	check(next(0) == LINK_DO_AP_ON && next(0) == LINK_DO_SCAN && next(0) == LINK_DO_NOTHING, "safe mode with a profile: the access point goes first, then the scan, each once");
	link_init(&wifi, HOME, 0, true, 0);
	check(next(0) == LINK_DO_AP_ON && next(0) == LINK_DO_NOTHING && shows(LINK_IDLE, -1, ""), "safe mode without a profile: the access point and nothing else");

	link_init(&wifi, ALL, 4, false, 0);
	check(shows(LINK_WAITING, -1, "") && next(0) == LINK_DO_SCAN, "a count of four is a list: a scan is ordered");
	link_init(&wifi, ALL, 5, false, 0);
	check(shows(LINK_IDLE, -1, "") && next(0) == LINK_DO_AP_ON && next(0) == LINK_DO_NOTHING, "a count of five is no list: as without a profile");
	link_init(&wifi, ALL, -1, false, 0);
	check(shows(LINK_IDLE, -1, "") && next(0) == LINK_DO_AP_ON && next(0) == LINK_DO_NOTHING, "a count of -1 is no list: as without a profile");
	link_init(&wifi, ALL, INT_MAX, false, 0);
	check(shows(LINK_IDLE, -1, "") && next(0) == LINK_DO_AP_ON && next(0) == LINK_DO_NOTHING, "the largest count is no list: as without a profile");
	link_init(&wifi, ALL, INT_MIN, false, 0);
	check(shows(LINK_IDLE, -1, "") && next(0) == LINK_DO_AP_ON && next(0) == LINK_DO_NOTHING, "the smallest count is no list: as without a profile");

	link_init(&wifi, HOME, 1, false, 50000);
	check(next(50000) == LINK_DO_SCAN, "started at a later time: the first scan at once");
	link_init(&wifi, HOME, 1, false, 50000);
	check(next(0) == LINK_DO_SCAN, "a first link_next with a time before the start: the scan at once as well");
}

static void test_scan(void)
{
	check(scanning(ALL, 4) == LINK_DO_SCAN, "the scene: a scan handed out at 1000");
	link_scanned(&wifi, NULL, 0, 3000);
	check(shows(LINK_WAITING, -1, "") && next(3000) == LINK_DO_NOTHING, "a scan that saw nothing at all (NULL, 0): no join, waiting");

	scanning(ALL, 4);
	link_scanned(&wifi, STRANGERS, 2, 3000);
	check(shows(LINK_WAITING, -1, "") && next(3000) == LINK_DO_NOTHING, "a scan that saw only strangers: no join, waiting");

	scanning(ALL, 4);
	link_scanned(&wifi, TWO, 2, 3000);
	check(shows(LINK_JOINING, 1, ""), "of two stored networks in range the one earlier in the list is joined next");
	check(next(3000) == LINK_DO_JOIN && shows(LINK_JOINING, 1, ""), "the join is ordered with the next link_next, link_profile names the profile");
	check(next(3000) == LINK_DO_NOTHING && next(17999) == LINK_DO_NOTHING && shows(LINK_JOINING, 1, ""), "the join is handed out once: nothing while it is under way");

	scanning(ALL, 4);
	link_scanned(&wifi, TWO_SWAPPED, 2, 3000);
	check(shows(LINK_JOINING, 1, ""), "the order of the networks in the scan does not matter");
	scanning(ALL, 4);
	link_scanned(&wifi, EVERYTHING, 5, 3000);
	check(shows(LINK_JOINING, 0, ""), "all stored networks in range: the first of the list");
	scanning(ALL, 4);
	link_scanned(&wifi, ONLY_PHONE, 2, 3000);
	check(shows(LINK_JOINING, 3, "") && next(3000) == LINK_DO_JOIN && link_profile(&wifi) == 3, "only the last of four stored networks in range: it is joined");
	scanning(REVERSED, 3);
	link_scanned(&wifi, EVERYTHING, 5, 3000);
	check(shows(LINK_JOINING, 0, "") && strcmp(wifi.profiles[link_profile(&wifi)].ssid, "Garage") == 0, "the priority is the order of the list of the caller, not the order of the scan");

	scanning(ALL, 4);
	link_scanned(&wifi, CROWD, 25, 3000);
	check(shows(LINK_JOINING, 2, ""), "25 networks seen: the 20th is looked at, the 21st and 22nd are not");
	scanning(ALL, 4);
	link_scanned(&wifi, CROWD, 19, 3000);
	check(shows(LINK_WAITING, -1, ""), "19 networks reported: the 20th of the array is not looked at");
	scanning(ALL, 4);
	link_scanned(&wifi, CROWD, 20, 3000);
	check(shows(LINK_JOINING, 2, ""), "20 networks reported: the 20th is looked at");
	scanning(ALL, 4);
	link_scanned(&wifi, CROWD, 21, 3000);
	check(shows(LINK_JOINING, 2, ""), "21 networks reported: the 21st is not looked at");
	scanning(ALL, 4);
	link_scanned(&wifi, CROWD, INT_MAX, 3000);
	check(shows(LINK_JOINING, 2, ""), "the largest count of networks: 20 are looked at");
	scanning(ALL, 4);
	link_scanned(&wifi, EVERYTHING, -1, 3000);
	check(shows(LINK_WAITING, -1, ""), "a negative count of networks: nothing seen");
	scanning(ALL, 4);
	link_scanned(&wifi, EVERYTHING, 0, 3000);
	check(shows(LINK_WAITING, -1, ""), "a count of 0 with an array: nothing seen");
	scanning(ALL, 4);
	link_scanned(&wifi, EVERYTHING, INT_MIN, 3000);
	check(shows(LINK_WAITING, -1, ""), "the smallest count of networks: nothing seen");

	// The join may be asked for much later than the scan ended: that is no attempt without an outcome
	scanning(HOME, 1);
	link_scanned(&wifi, EVERYTHING, 5, 3000);
	check(next(60000) == LINK_DO_JOIN && shows(LINK_JOINING, 0, ""), "a join that is handed out long after the scan is a first attempt");
	check(next(74999) == LINK_DO_NOTHING && shows(LINK_JOINING, 0, ""), "its time limit counts from when it was handed out");
}

static void test_waits(void)
{
	link_init(&wifi, HOME, 1, false, 0);
	next(0);
	scan_failed(1000);
	check(shows(LINK_WAITING, -1, ""), "a scan without a stored network: waiting");
	check(quiet(1000, 2999, 1), "1999 ms after the end of the first scan without a stored network: no scan");
	check(next(3000) == LINK_DO_SCAN, "2000 ms after the end of the first scan without a stored network: the next scan");
	scan_failed(4000);
	check(quiet(4000, 8999, 1), "4999 ms after the end of the second scan without a stored network: no scan");
	check(next(9000) == LINK_DO_SCAN, "5000 ms after the end of the second scan without a stored network: the next scan");
	scan_failed(10000);
	check(quiet(10000, 19999, 1), "9999 ms after the end of the third scan without a stored network: no scan");
	check(next(20000) == LINK_DO_SCAN, "10000 ms after the end of the third scan without a stored network: the next scan");
	scan_failed(21000);
	check(quiet(21000, 50999, 1), "29999 ms after the end of the fourth scan without a stored network: no scan");
	check(next(51000) == LINK_DO_SCAN, "30000 ms after the end of the fourth scan without a stored network: the next scan");
	scan_failed(52000);
	check(quiet(52000, 81999, 1), "29999 ms after the end of the fifth scan without a stored network: no scan");
	check(next(82000) == LINK_DO_SCAN, "30000 ms after the end of the fifth scan without a stored network: the next scan");
	scan_failed(83000);
	check(quiet(83000, 112999, 1) && next(113000) == LINK_DO_SCAN, "30000 ms after the sixth scan as well: the wait does not grow any more");

	// A scan that saw nothing at all counts like one that saw strangers
	link_init(&wifi, HOME, 1, false, 0);
	next(0);
	link_scanned(&wifi, NULL, 0, 1000);
	check(quiet(1000, 2999, 1) && next(3000) == LINK_DO_SCAN, "a scan that saw nothing at all: the next scan after 2000 ms");
	link_scanned(&wifi, NULL, 0, 4000);
	check(quiet(4000, 8999, 1) && next(9000) == LINK_DO_SCAN, "two scans that saw nothing at all: the next scan after 5000 ms");

	// The wait counts from the end of the scan, not from its start
	link_init(&wifi, HOME, 1, false, 0);
	next(0);
	scan_failed(7000);
	check(next(8999) == LINK_DO_NOTHING && next(9000) == LINK_DO_SCAN, "a scan that took 7000 ms: the wait counts from its end");

	// Joins that fail use the same waits
	link_init(&wifi, HOME, 1, false, 0);
	check(joins_fail(0), "the scene: a stored network in range, both attempts to join it fail, the second at 1000");
	check(shows(LINK_WAITING, -1, ""), "after the second failed attempt: waiting, no profile");
	check(quiet(1000, 2999, 1) && next(3000) == LINK_DO_SCAN, "the next scan 2000 ms after the end of the second failed attempt, not before");
	scan_failed(3000);
	check(quiet(3000, 7999, 1) && joins_fail(8000), "a scan without a stored network after failed joins: the next scan 5000 ms later");
	check(quiet(9000, 18999, 1) && joins_fail(19000), "failed joins after that: the next scan 10000 ms later");
	check(quiet(20000, 49999, 1) && joins_fail(50000), "failed joins a third time: the next scan 30000 ms later");
	check(quiet(51000, 80999, 1) && next(81000) == LINK_DO_SCAN, "failed joins a fourth time: 30000 ms again");
}

static void test_waits_start_anew(void)
{
	// Three scans without a stored network: the next wait would be 30 s
	link_init(&wifi, HOME, 1, false, 0);
	next(0);
	scan_failed(1000);
	next(3000);
	scan_failed(3000);
	next(8000);
	scan_failed(8000);
	check(next(17999) == LINK_DO_NOTHING && next(18000) == LINK_DO_SCAN, "the scene: the fourth scan follows 10000 ms after the third");
	link_scanned(&wifi, ONLY_HOME, 2, 19000);
	next(19000);
	link_join_failed(&wifi, 20000);
	check(next(20000) == LINK_DO_JOIN, "the scene: a network seen by the fourth scan, the first attempt failed");
	link_joined(&wifi, GATEWAY, 21000);
	link_lost(&wifi, 22000);
	check(next(22000) == LINK_DO_SCAN, "the network lost: a scan at once");
	scan_failed(23000);
	check(quiet(23000, 24999, 1) && next(25000) == LINK_DO_SCAN, "after a join that succeeded the waits start with 2000 ms again");
	scan_failed(26000);
	check(quiet(26000, 30999, 1) && next(31000) == LINK_DO_SCAN, "and go on with 5000 ms");

	// A scan that finds a network does not start them anew: only a join that succeeds does
	link_init(&wifi, HOME, 1, false, 0);
	next(0);
	scan_failed(1000);
	next(3000);
	scan_failed(3000);
	check(joins_fail(8000) && quiet(9000, 18999, 1) && next(19000) == LINK_DO_SCAN, "a network that is seen but cannot be joined does not start the waits anew: 10000 ms as the third wait");
}

static void test_join(void)
{
	check(joining(HOME, 1) == LINK_DO_JOIN && shows(LINK_JOINING, 0, ""), "the scene: a join handed out at 3000");
	link_join_failed(&wifi, 4000);
	check(shows(LINK_JOINING, 0, ""), "the first attempt failed: the profile stays to be joined");
	check(next(4000) == LINK_DO_JOIN && shows(LINK_JOINING, 0, ""), "the first attempt failed: the second is handed out at once");
	check(next(4000) == LINK_DO_NOTHING, "the second attempt is handed out once");
	link_join_failed(&wifi, 6000);
	check(shows(LINK_WAITING, -1, ""), "the second attempt failed: no third one, waiting for the next scan");
	check(next(6000) == LINK_DO_NOTHING && next(7999) == LINK_DO_NOTHING && next(8000) == LINK_DO_SCAN, "the second attempt failed: a new scan 2000 ms later");
	link_scanned(&wifi, EVERYTHING, 5, 9000);
	check(next(9000) == LINK_DO_JOIN, "the network seen again: a join again");
	link_join_failed(&wifi, 10000);
	check(next(10000) == LINK_DO_JOIN, "the attempts count from the scan: two again after a new scan");
	link_join_failed(&wifi, 11000);
	check(shows(LINK_WAITING, -1, "") && next(11000) == LINK_DO_NOTHING, "and no third one after the new scan either");

	joining(HOME, 1);
	link_join_failed(&wifi, 4000);
	next(4000);
	link_joined(&wifi, GATEWAY, 5000);
	check(shows(LINK_JOINED, 0, ""), "the second attempt succeeds: in the network");
}

static void test_join_timeout(void)
{
	joining(HOME, 1);
	check(next(17999) == LINK_DO_NOTHING && shows(LINK_JOINING, 0, ""), "a join without a report for 14999 ms: still under way");
	check(next(18000) == LINK_DO_JOIN && shows(LINK_JOINING, 0, ""), "a join without a report for 15000 ms counts as failed: the second attempt is handed out with the same call");
	check(next(32999) == LINK_DO_NOTHING && shows(LINK_JOINING, 0, ""), "the second attempt without a report for 14999 ms: still under way");
	check(next(33000) == LINK_DO_NOTHING && shows(LINK_WAITING, -1, ""), "the second attempt without a report for 15000 ms: failed for good, waiting");
	check(next(34999) == LINK_DO_NOTHING && next(35000) == LINK_DO_SCAN, "the wait counts from the call that found the attempt overdue");

	joining(HOME, 1);
	check(next(40000) == LINK_DO_JOIN, "an overdue attempt noticed late: failed then, the second attempt at once");
	check(next(54999) == LINK_DO_NOTHING && next(70000) == LINK_DO_NOTHING && shows(LINK_WAITING, -1, ""), "the time limit of the second attempt counts from when it was handed out");
	check(next(71999) == LINK_DO_NOTHING && next(72000) == LINK_DO_SCAN, "the wait after an attempt that was found overdue late counts from that call");

	joining(HOME, 1);
	link_joined(&wifi, GATEWAY, 18000);
	check(shows(LINK_JOINED, 0, ""), "a join reported 15000 ms after it began, before link_next counted it as failed: joined");
	joining(HOME, 1);
	link_join_failed(&wifi, 18000);
	check(next(18000) == LINK_DO_JOIN && next(18000) == LINK_DO_NOTHING && shows(LINK_JOINING, 0, ""),
	      "a failure reported 15000 ms after the join began is one failed attempt, not two");

	// The interface cannot tell which attempt a report belongs to
	joining(HOME, 1);
	next(18000);
	link_join_failed(&wifi, 18001);
	check(shows(LINK_WAITING, -1, ""), "a failure reported after the time limit ran out counts for the attempt under way then");

	// The time limit goes before the access point, which goes before everything else
	joining(HOME, 1);
	next(18000);
	link_ap_request(&wifi, true, 20000);
	check(next(33000) == LINK_DO_AP_ON && shows(LINK_WAITING, -1, ""), "the call that hands out the access point counts an overdue join as failed as well");
	check(next(34999) == LINK_DO_NOTHING && next(35000) == LINK_DO_SCAN, "and the wait counts from that call");
}

static void test_host_given(void)
{
	joined(GARAGE, 1, GATEWAY);
	check(shows(LINK_UP, 0, "192.168.1.50"), "a profile with an address: up at once with it, the gateway does not matter");
	check(quiet(5000, 700000, 1000), "a profile with an address: no query is ever ordered");

	joined(GARAGE, 1, NULL);
	check(shows(LINK_UP, 0, "192.168.1.50"), "a profile with an address needs no gateway text");

	scanning(ALL, 4);
	link_scanned(&wifi, ONLY_PHONE, 2, 3000);
	next(3000);
	link_joined(&wifi, GATEWAY, 5000);
	check(shows(LINK_UP, 3, "wican.local"), "a profile with a name: up at once with the name");
}

static void test_host_gateway(void)
{
	joined(WICAN, 1, GATEWAY);
	check(shows(LINK_UP, 0, GATEWAY), "the access point of a WiCAN: up at once, the adapter is the gateway");
	check(quiet(5000, 700000, 1000), "the access point of a WiCAN: no query is ever ordered");

	joined(WICAN, 1, NULL);
	check(shows(LINK_JOINED, 0, ""), "the access point of a WiCAN without a gateway text (NULL): joined, not up");
	check(next(5000) == LINK_DO_FIND, "the access point of a WiCAN without a gateway text (NULL): the service is queried");
	link_found(&wifi, ADAPTER, 6000);
	check(shows(LINK_UP, 0, ADAPTER), "the query in the access point of a WiCAN found the adapter: up with that address");

	joined(WICAN, 1, "");
	check(shows(LINK_JOINED, 0, "") && next(5000) == LINK_DO_FIND, "the access point of a WiCAN with an empty gateway text: the service is queried");
}

static void test_host_query(void)
{
	joined(HOME, 1, GATEWAY);
	check(shows(LINK_JOINED, 0, ""), "a router: joined, the adapter not located, the gateway is not taken for it");
	check(next(5000) == LINK_DO_FIND && shows(LINK_JOINED, 0, ""), "a router: the query is ordered at once");
	check(quiet(5000, 3600000, 1000) && shows(LINK_JOINED, 0, ""), "a query is handed out once and has no time limit");

	finding();
	link_found(&wifi, ADAPTER, 6000);
	check(shows(LINK_UP, 0, ADAPTER), "the query found the adapter: up with its address");
	check(quiet(6000, 700000, 1000) && shows(LINK_UP, 0, ADAPTER), "up: nothing more to do while nobody says the adapter is silent");

	finding();
	link_not_found(&wifi, 6000);
	check(shows(LINK_JOINED, 0, ""), "the query found nothing: the display stays in the network");
	check(quiet(6000, 15999, 1), "the query found nothing: not asked again for 9999 ms");
	check(next(16000) == LINK_DO_FIND, "the query found nothing: asked again after 10000 ms");
	link_not_found(&wifi, 16500);
	check(quiet(16500, 26499, 1) && next(26500) == LINK_DO_FIND, "the second query found nothing: asked again 10000 ms after its end");
	link_not_found(&wifi, 27000);
	check(quiet(27000, 36999, 1) && next(37000) == LINK_DO_FIND && shows(LINK_JOINED, 0, ""), "the wait between queries does not grow");
	link_found(&wifi, "wican.local", 38000);
	check(shows(LINK_UP, 0, "wican.local"), "a later query found the adapter: up");

	finding();
	link_found(&wifi, "", 6000);
	check(shows(LINK_JOINED, 0, "") && next(15999) == LINK_DO_NOTHING && next(16000) == LINK_DO_FIND, "an empty address counts as not found: asked again after 10000 ms");

	finding();
	link_found(&wifi, "caf\303\251.local", 6000);
	check(shows(LINK_UP, 0, "caf\303\251.local"), "an address with bytes above 127 is taken as it is");

	finding();
	link_found(&wifi, "x", 6000);
	check(shows(LINK_UP, 0, "x"), "an address of one byte is taken");
}

// Every length of the three texts an address can come from
static void test_host_lengths(void)
{
	static net_profile_t stored[2];
	link_t not_found, without_gateway;
	int wrong_taken = 0, wrong_refused = 0;
	size_t length;

	finding();
	link_not_found(&wifi, 6000);
	not_found = wifi;
	for(length = 0; length <= 120; length++)
	{
		const char *text = text_of(length);

		finding();
		link_found(&wifi, text, 6000);
		if(length >= 1 && length <= 39)
		{
			if(!shows(LINK_UP, 0, text) || wifi.busy || wifi.finding || !wifi.host_from_query) wrong_taken++;
		}
		else if(!same_link(&wifi, &not_found))
		{
			wrong_refused++;
		}
	}
	check(wrong_taken == 0, "an address of 1 to 39 bytes found by a query is taken byte for byte");
	check(wrong_refused == 0, "an address of 0 or of 40 to 120 bytes found by a query counts as not found, exactly as link_not_found");

	joined(WICAN, 1, NULL);
	without_gateway = wifi;
	wrong_taken = wrong_refused = 0;
	for(length = 0; length <= 120; length++)
	{
		const char *text = text_of(length);

		joined(WICAN, 1, text);
		if(length >= 1 && length <= 39)
		{
			if(!shows(LINK_UP, 0, text) || wifi.busy || wifi.host_from_query) wrong_taken++;
		}
		else if(!same_link(&wifi, &without_gateway))
		{
			wrong_refused++;
		}
	}
	check(wrong_taken == 0, "a gateway text of 1 to 39 bytes is taken byte for byte");
	check(wrong_refused == 0, "a gateway text of 0 or of 40 to 120 bytes is no gateway text: exactly as NULL");

	// The profile under test is the last one of its array: a read behind its host is a read behind the array
	wrong_taken = 0;
	for(length = 1; length <= 39; length++)
	{
		memset(stored, 0, sizeof(stored));
		strcpy(stored[1].ssid, "Home");
		strcpy(stored[1].host, text_of(length));
		joined(stored, 2, GATEWAY);
		if(!shows(LINK_UP, 1, text_of(length)) || wifi.host_from_query) wrong_taken++;
	}
	check(wrong_taken == 0, "an address of 1 to 39 bytes stored with the profile is taken byte for byte");

	memset(stored, 0, sizeof(stored));
	strcpy(stored[1].ssid, "Home");
	memset(stored[1].host, '7', sizeof(stored[1].host));
	joined(stored, 2, GATEWAY);
	check(shows(LINK_JOINED, 1, ""), "a stored address without a terminating zero is none: joined, not up");
	check(next(5000) == LINK_DO_FIND, "a stored address without a terminating zero: the service is queried");
	link_found(&wifi, ADAPTER, 6000);
	check(shows(LINK_UP, 1, ADAPTER), "the query for a profile with a damaged address found the adapter: up");

	memset(stored, 0, sizeof(stored));
	strcpy(stored[1].ssid, "WiCAN_a1b2c3d4e5f6");
	memset(stored[1].host, '7', sizeof(stored[1].host));
	joined(stored, 2, GATEWAY);
	check(shows(LINK_JOINED, 1, "") && next(5000) == LINK_DO_FIND, "a damaged address in the profile of a WiCAN access point: queried, the gateway is not taken either");

	// A shorter address behind a longer one
	finding();
	link_found(&wifi, "wican-in-the-sprinter.local", 6000);
	link_answering(&wifi, false, 7000);
	next(67000);
	link_found(&wifi, "10.0.0.7", 68000);
	check(shows(LINK_UP, 0, "10.0.0.7"), "a shorter address replaces a longer one completely");
}

static void test_silent_adapter(void)
{
	up_by_query();
	link_answering(&wifi, false, 7000);
	check(quiet(7000, 66999, 1) && shows(LINK_UP, 0, ADAPTER), "a queried address silent for 59999 ms: no query, still up");
	check(next(67000) == LINK_DO_FIND, "a queried address silent for 60000 ms: the service is queried again");
	check(shows(LINK_UP, 0, ADAPTER) && quiet(67000, 67999, 1) && shows(LINK_UP, 0, ADAPTER), "the old address stays in use while the query is under way");
	link_found(&wifi, MOVED, 68000);
	check(shows(LINK_UP, 0, MOVED), "the query found the adapter at another address: that one is in use");
	check(quiet(68000, 800000, 1000), "a new address ends the silence: no query until the adapter is reported silent again");
	link_answering(&wifi, false, 800000);
	check(quiet(800000, 859999, 1) && next(860000) == LINK_DO_FIND, "the new address silent for 60000 ms: queried again");

	// The silence was reported before the new address and not again after it
	up_by_query();
	link_answering(&wifi, false, 7000);
	next(67000);
	link_found(&wifi, ADAPTER, 68000);
	check(shows(LINK_UP, 0, ADAPTER) && quiet(68000, 77999, 1) && quiet(78000, 127999, 1000) && quiet(128000, 900000, 1000),
	      "the query found the same address again: the 60000 ms start anew with the next report of silence");

	up_by_query();
	link_answering(&wifi, false, 7000);
	next(67000);
	link_not_found(&wifi, 68000);
	check(shows(LINK_UP, 0, ADAPTER), "the query for a silent adapter found nothing: the old address stays in use");
	check(quiet(68000, 77999, 1), "the query for a silent adapter found nothing: not asked again for 9999 ms");
	check(next(78000) == LINK_DO_FIND, "the query for a silent adapter found nothing: asked again after 10000 ms");
	link_not_found(&wifi, 79000);
	link_answering(&wifi, true, 80000);
	check(quiet(80000, 900000, 1000), "the adapter answers again after a query that found nothing: no more queries");
	link_answering(&wifi, false, 900000);
	check(quiet(900000, 959999, 1) && next(960000) == LINK_DO_FIND, "silent again later: 60000 ms from the first report of it, not 10000");

	up_by_query();
	link_answering(&wifi, false, 7000);
	next(67000);
	link_found(&wifi, text_of(40), 68000);
	check(shows(LINK_UP, 0, ADAPTER) && next(77999) == LINK_DO_NOTHING && next(78000) == LINK_DO_FIND, "an address that is too long, found for a silent adapter: the old one stays, asked again after 10000 ms");

	up_by_query();
	link_answering(&wifi, false, 7000);
	link_answering(&wifi, false, 8000);
	link_answering(&wifi, false, 66000);
	check(next(66999) == LINK_DO_NOTHING && next(67000) == LINK_DO_FIND, "further reports of silence do not start the 60000 ms anew");

	up_by_query();
	link_answering(&wifi, false, 7000);
	link_answering(&wifi, true, 37000);
	link_answering(&wifi, false, 47000);
	check(quiet(47000, 106999, 1) && next(107000) == LINK_DO_FIND, "an answer between two silences: the 60000 ms count from the second one");

	up_by_query();
	link_answering(&wifi, true, 7000);
	check(quiet(7000, 900000, 1000), "an adapter that answers is never queried for");

	up_by_query();
	link_answering(&wifi, false, 7000);
	link_answering(&wifi, true, 66999);
	check(quiet(66999, 900000, 1000), "an answer 1 ms before the 60000 ms are over: no query");

	joined(GARAGE, 1, GATEWAY);
	link_answering(&wifi, false, 7000);
	check(quiet(7000, 900000, 1000) && shows(LINK_UP, 0, "192.168.1.50"), "an address stored with the profile is never replaced, however long the adapter is silent");

	joined(WICAN, 1, GATEWAY);
	link_answering(&wifi, false, 7000);
	check(quiet(7000, 900000, 1000) && shows(LINK_UP, 0, GATEWAY), "the gateway is never replaced, however long the adapter is silent");

	joined(WICAN, 1, NULL);
	next(5000);
	link_found(&wifi, ADAPTER, 6000);
	link_answering(&wifi, false, 7000);
	check(next(66999) == LINK_DO_NOTHING && next(67000) == LINK_DO_FIND, "an address queried in the access point of a WiCAN came from a query: asked again when silent");

	// Reports from before the adapter was located say nothing about it
	finding();
	link_answering(&wifi, false, 5500);
	link_found(&wifi, ADAPTER, 6000);
	check(quiet(6000, 900000, 1000), "silence reported before the adapter was located does not count");

	// Nor do reports about the adapter in the network before
	up_by_query();
	link_answering(&wifi, false, 7000);
	link_lost(&wifi, 8000);
	next(8000);
	link_scanned(&wifi, EVERYTHING, 5, 9000);
	next(9000);
	link_joined(&wifi, GATEWAY, 10000);
	next(10000);
	link_found(&wifi, ADAPTER, 11000);
	check(shows(LINK_UP, 0, ADAPTER) && quiet(11000, 900000, 1000), "silence reported in the network before does not count in the next one");

	// A given address after a queried one
	up_by_query();
	link_profiles(&wifi, GARAGE, 1, 7000);
	next(7000);
	link_left(&wifi, 8000);
	next(8000);
	link_scanned(&wifi, EVERYTHING, 5, 9000);
	next(9000);
	link_joined(&wifi, GATEWAY, 10000);
	link_answering(&wifi, false, 11000);
	check(shows(LINK_UP, 0, "192.168.1.50") && quiet(11000, 900000, 1000), "a stored address after a queried one is not queried for either");
}

static void test_lost(void)
{
	up_by_query();
	link_lost(&wifi, 7000);
	check(shows(LINK_WAITING, -1, ""), "the network lost while up: the host is forgotten, no profile, not up");
	check(next(7000) == LINK_DO_SCAN, "the network lost while up: a scan at once");
	scan_failed(8000);
	check(quiet(8000, 9999, 1) && next(10000) == LINK_DO_SCAN, "after the scan that follows a loss the waits start with 2000 ms");

	finding();
	link_lost(&wifi, 5500);
	check(shows(LINK_WAITING, -1, "") && next(5500) == LINK_DO_SCAN, "the network lost while the query is under way: a scan at once, the query is given up");
	link_found(&wifi, ADAPTER, 6000);
	check(shows(LINK_SCANNING, -1, ""), "the report of a query that was given up is ignored");
	link_scanned(&wifi, EVERYTHING, 5, 7000);
	next(7000);
	link_joined(&wifi, GATEWAY, 8000);
	check(next(8000) == LINK_DO_FIND, "after a query that was given up a new one is ordered in the next network");

	finding();
	link_not_found(&wifi, 6000);
	link_lost(&wifi, 7000);
	check(shows(LINK_WAITING, -1, "") && next(7000) == LINK_DO_SCAN, "the network lost while waiting to ask again: a scan at once");
	// The query that was planned for 16000 is never ordered: the scans go on with their waits
	scan_failed(7500);
	check(quiet(7500, 9499, 1) && next(9500) == LINK_DO_SCAN, "after the loss while waiting to ask again: the next scan after 2000 ms");
	scan_failed(10000);
	check(quiet(10000, 14999, 1) && next(15000) == LINK_DO_SCAN && quiet(15000, 30000, 100) && shows(LINK_SCANNING, -1, ""),
	      "no query is ordered outside a network, whatever was planned in the last one");

	finding();
	link_not_found(&wifi, 6000);
	link_lost(&wifi, 7000);
	next(7000);
	link_scanned(&wifi, EVERYTHING, 5, 8000);
	next(8000);
	link_joined(&wifi, GATEWAY, 9000);
	check(next(9000) == LINK_DO_FIND, "the query in the next network is ordered at once, whatever was planned in the last one");

	joined(GARAGE, 1, GATEWAY);
	link_lost(&wifi, 7000);
	check(shows(LINK_WAITING, -1, "") && next(7000) == LINK_DO_SCAN, "a network with a stored address lost: a scan at once");

	// A silent adapter is being queried for when the network goes
	up_by_query();
	link_answering(&wifi, false, 7000);
	next(67000);
	link_lost(&wifi, 67500);
	check(shows(LINK_WAITING, -1, "") && next(67500) == LINK_DO_SCAN, "the network lost during the query for a silent adapter: the host is forgotten, a scan at once");
	link_found(&wifi, MOVED, 68000);
	check(shows(LINK_SCANNING, -1, ""), "the report of the query for a silent adapter is ignored after the loss");
}

static void test_profile(void)
{
	link_init(&wifi, ALL, 4, false, 0);
	check(link_profile(&wifi) == -1, "no profile before the first scan");
	next(1000);
	check(link_profile(&wifi) == -1, "no profile during the scan");
	link_scanned(&wifi, TWO, 2, 3000);
	check(link_profile(&wifi) == 1, "the profile is known when the scan ended");
	next(3000);
	link_join_failed(&wifi, 4000);
	check(link_profile(&wifi) == 1, "the profile stays between two attempts");
	next(4000);
	link_joined(&wifi, GATEWAY, 5000);
	check(link_profile(&wifi) == 1 && link_up(&wifi), "the profile stays while the display is up in its network");
	link_lost(&wifi, 6000);
	check(link_profile(&wifi) == -1, "no profile after the network was lost");

	finding();
	check(link_profile(&wifi) == 0, "the profile stays while the adapter is looked for");
	link_profiles(&wifi, ALL, 4, 5500);
	check(link_profile(&wifi) == -1, "no profile from link_profiles on, although the display is still in the network");
}

// link_t.busy and link_t.finding as the header describes them
static void test_under_way(void)
{
	link_init(&wifi, HOME, 1, false, 0);
	check(!wifi.busy && !wifi.finding, "nothing is under way after the start");
	next(1000);
	check(wifi.busy && !wifi.finding, "a scan that was handed out is under way");
	link_scanned(&wifi, EVERYTHING, 5, 3000);
	check(!wifi.busy && !wifi.finding, "the scan ended: nothing is under way while the join is due");
	next(3000);
	check(wifi.busy && !wifi.finding, "a join that was handed out is under way");
	link_join_failed(&wifi, 4000);
	check(!wifi.busy && !wifi.finding, "between two attempts nothing is under way");
	next(4000);
	link_joined(&wifi, GATEWAY, 5000);
	check(!wifi.busy && !wifi.finding, "the join ended: nothing is under way while the query is due");
	next(5000);
	check(wifi.busy && wifi.finding, "a query that was handed out is under way");
	link_found(&wifi, ADAPTER, 6000);
	check(!wifi.busy && !wifi.finding && !wifi.changed, "nothing is under way while up");
	link_profiles(&wifi, GARAGE, 1, 7000);
	check(!wifi.busy && !wifi.finding && wifi.changed, "the list changed: a leave is due, nothing is under way yet");
	next(7000);
	check(wifi.busy && !wifi.finding, "a leave that was handed out is under way");
	link_left(&wifi, 8000);
	check(!wifi.busy && !wifi.finding && !wifi.changed, "after the leave nothing is under way and no leave is due");
}

/* ------------------------------------------------------------------------------------------------ */
/* The stored networks change                                                                         */
/* ------------------------------------------------------------------------------------------------ */

static void test_profiles_outside_a_network(void)
{
	// From none
	link_init(&wifi, HOME, 0, false, 0);
	next(0);
	link_profiles(&wifi, HOME, 1, 5000);
	check(shows(LINK_WAITING, -1, ""), "idle, a profile is stored: waiting for the scan");
	check(next(5000) == LINK_DO_SCAN, "idle, a profile is stored: the scan at once");
	check(link_ap_on(&wifi), "idle, a profile is stored: the access point stays on for now");

	link_init(&wifi, HOME, 0, false, 0);
	next(0);
	link_profiles(&wifi, ALL, 0, 5000);
	check(shows(LINK_IDLE, -1, "") && quiet(5000, 19000, 1000), "idle, the list changes to none again: idle");
	link_profiles(&wifi, ALL, 5, 20000);
	check(shows(LINK_IDLE, -1, "") && quiet(20000, 39000, 1000), "idle, a count of five is no list: idle");
	link_profiles(&wifi, ALL, -1, 40000);
	check(shows(LINK_IDLE, -1, "") && quiet(40000, 59000, 1000), "idle, a count of -1 is no list: idle");
	link_profiles(&wifi, ALL, INT_MAX, 60000);
	check(shows(LINK_IDLE, -1, "") && quiet(60000, 79000, 1000), "idle, the largest count is no list: idle");
	link_profiles(&wifi, ALL, 4, 80000);
	check(shows(LINK_WAITING, -1, "") && next(80000) == LINK_DO_SCAN, "idle, a count of four is a list: the scan at once");

	// Waiting, the next wait would be 30 s
	link_init(&wifi, HOME, 1, false, 0);
	next(0);
	scan_failed(1000);
	next(3000);
	scan_failed(3000);
	next(8000);
	scan_failed(8000);
	check(next(9000) == LINK_DO_NOTHING, "the scene: waiting 10000 ms for the fourth scan");
	link_profiles(&wifi, GARAGE, 1, 9000);
	check(shows(LINK_WAITING, -1, "") && next(9000) == LINK_DO_SCAN, "waiting, the list changes: the scan at once");
	scan_failed(10000);
	check(quiet(10000, 11999, 1) && next(12000) == LINK_DO_SCAN, "waiting, the list changes: the waits start with 2000 ms again");

	link_init(&wifi, HOME, 1, false, 0);
	next(0);
	scan_failed(1000);
	link_profiles(&wifi, HOME, 0, 1500);
	check(shows(LINK_IDLE, -1, ""), "waiting, the last profile is removed: idle");
	check(next(1500) == LINK_DO_AP_ON && quiet(1500, 100000, 500), "waiting, the last profile is removed: the access point is ordered on, no scan follows");

	// A scan is under way
	scanning(HOME, 1);
	link_profiles(&wifi, REVERSED, 3, 2000);
	check(shows(LINK_SCANNING, -1, "") && next(2000) == LINK_DO_NOTHING, "scanning, the list changes: the scan under way goes on, no second one");
	link_scanned(&wifi, ONLY_HOME, 2, 3000);
	check(shows(LINK_JOINING, 2, "") && next(3000) == LINK_DO_JOIN, "scanning, the list changes: what the scan saw is looked up in the new list");

	link_init(&wifi, HOME, 1, false, 0);
	next(0);
	scan_failed(1000);
	next(3000);
	scan_failed(3000);
	next(8000);
	link_profiles(&wifi, GARAGE, 1, 8500);
	scan_failed(9000);
	check(quiet(9000, 10999, 1) && next(11000) == LINK_DO_SCAN, "scanning, the list changes: the waits start with 2000 ms after this scan");

	scanning(HOME, 1);
	link_profiles(&wifi, HOME, 0, 2000);
	check(shows(LINK_SCANNING, -1, ""), "scanning, the last profile is removed: the scan under way ends first");
	check(next(2000) == LINK_DO_AP_ON && next(2000) == LINK_DO_NOTHING, "scanning, the last profile is removed: the access point is ordered on at once");
	link_scanned(&wifi, EVERYTHING, 5, 3000);
	check(shows(LINK_IDLE, -1, "") && quiet(3000, 100000, 500), "scanning, the last profile is removed: idle when the scan ends, no join and no scan");

	scanning(HOME, 1);
	link_profiles(&wifi, HOME, 0, 2000);
	link_scanned(&wifi, EVERYTHING, 5, 3000);
	check(shows(LINK_IDLE, -1, "") && next(3000) == LINK_DO_AP_ON && quiet(3000, 100000, 500),
	      "scanning, the last profile is removed, the scan ends before the access point was ordered: idle all the same");

	scanning(HOME, 1);
	link_profiles(&wifi, HOME, 0, 2000);
	link_profiles(&wifi, GARAGE, 1, 2500);
	link_scanned(&wifi, EVERYTHING, 5, 3000);
	check(shows(LINK_JOINING, 0, "") && next(3000) == LINK_DO_AP_ON && next(3000) == LINK_DO_JOIN, "scanning, the list is emptied and filled again: the scan serves the list that is stored when it ends");

	// A network was chosen, the join not handed out yet
	scanning(ALL, 4);
	link_scanned(&wifi, EVERYTHING, 5, 3000);
	link_profiles(&wifi, REVERSED, 3, 3500);
	check(shows(LINK_WAITING, -1, ""), "a join not handed out yet, the list changes: it is not handed out, waiting");
	check(next(3500) == LINK_DO_SCAN, "a join not handed out yet, the list changes: a new scan at once");

	scanning(ALL, 4);
	link_scanned(&wifi, EVERYTHING, 5, 3000);
	link_profiles(&wifi, ALL, 0, 3500);
	check(shows(LINK_IDLE, -1, "") && next(3500) == LINK_DO_AP_ON && quiet(3500, 100000, 500), "a join not handed out yet, the last profile is removed: idle, no join");

	// Between two attempts
	joining(ALL, 4);
	link_join_failed(&wifi, 4000);
	link_profiles(&wifi, REVERSED, 3, 4500);
	check(shows(LINK_WAITING, -1, "") && next(4500) == LINK_DO_SCAN, "between two attempts, the list changes: no second attempt, a new scan at once");
}

static void test_profiles_while_joining(void)
{
	joining(ALL, 4);
	link_profiles(&wifi, REVERSED, 3, 3500);
	check(shows(LINK_JOINING, -1, ""), "joining, the list changes: the attempt under way goes on, link_profile names none");
	check(next(3500) == LINK_DO_NOTHING && next(17999) == LINK_DO_NOTHING, "joining, the list changes: nothing is ordered while the attempt is under way");
	link_joined(&wifi, GATEWAY, 5000);
	check(shows(LINK_JOINED, -1, ""), "joining, the list changes, the join succeeds: in a network, nothing looked for in it");
	check(next(5000) == LINK_DO_LEAVE && shows(LINK_LEAVING, -1, ""), "joining, the list changes, the join succeeds: the network is left again");
	check(quiet(5000, 3600000, 1000) && shows(LINK_LEAVING, -1, ""), "a leave is handed out once and has no time limit");
	link_left(&wifi, 6000);
	check(shows(LINK_WAITING, -1, "") && next(6000) == LINK_DO_SCAN, "the network was left: a scan at once");
	link_scanned(&wifi, EVERYTHING, 5, 7000);
	check(shows(LINK_JOINING, 0, "") && next(7000) == LINK_DO_JOIN, "after the leave the new list is in use");
	link_joined(&wifi, GATEWAY, 8000);
	check(shows(LINK_UP, 0, "192.168.1.51"), "the address is the one of the new list");

	// A profile with an address is joined when the list changes: the address must not be taken
	joining(GARAGE, 1);
	link_profiles(&wifi, HOME, 1, 3500);
	link_joined(&wifi, GATEWAY, 5000);
	check(shows(LINK_JOINED, -1, ""), "joining a network with a stored address, the list changes: the join succeeds, but not up");

	joining(ALL, 4);
	link_profiles(&wifi, REVERSED, 3, 3500);
	link_join_failed(&wifi, 4000);
	check(shows(LINK_WAITING, -1, ""), "joining, the list changes, the attempt fails: no second attempt, nothing to leave");
	check(next(4000) == LINK_DO_SCAN, "joining, the list changes, the attempt fails: a new scan at once");

	joining(ALL, 4);
	link_profiles(&wifi, REVERSED, 3, 3500);
	check(next(17999) == LINK_DO_NOTHING && next(18000) == LINK_DO_SCAN, "joining, the list changes, the attempt runs out of time: a new scan at once");

	// The waits start anew although joins failed before
	link_init(&wifi, HOME, 1, false, 0);
	joins_fail(0);
	joins_fail(3000);
	next(9000);
	link_scanned(&wifi, ONLY_HOME, 2, 9000);
	next(9000);
	link_profiles(&wifi, HOME, 1, 9500);
	link_join_failed(&wifi, 10000);
	next(10000);
	scan_failed(11000);
	check(quiet(11000, 12999, 1) && next(13000) == LINK_DO_SCAN, "joining, the list changes: the waits start with 2000 ms again");

	joining(ALL, 4);
	link_profiles(&wifi, ALL, 0, 3500);
	check(shows(LINK_JOINING, -1, "") && next(3500) == LINK_DO_AP_ON && next(3500) == LINK_DO_NOTHING,
	      "joining, the last profile is removed: the access point is ordered on, the attempt goes on");
	link_join_failed(&wifi, 4000);
	check(shows(LINK_IDLE, -1, "") && quiet(4000, 100000, 500), "joining, the last profile is removed, the attempt fails: idle");

	joining(ALL, 4);
	link_profiles(&wifi, ALL, 0, 3500);
	next(3500);
	link_joined(&wifi, GATEWAY, 5000);
	check(next(5000) == LINK_DO_LEAVE, "joining, the last profile is removed, the join succeeds: the network is left");
	link_left(&wifi, 6000);
	check(shows(LINK_IDLE, -1, "") && quiet(6000, 100000, 500), "the network was left and no profile is stored: idle, no scan");

	joining(ALL, 4);
	link_profiles(&wifi, ALL, 0, 3500);
	link_profiles(&wifi, GARAGE, 1, 3600);
	link_join_failed(&wifi, 4000);
	check(shows(LINK_WAITING, -1, "") && next(4000) == LINK_DO_AP_ON && next(4000) == LINK_DO_SCAN, "joining, the list is emptied and filled again, the attempt fails: a scan for the list stored then");
}

static void test_profiles_in_a_network(void)
{
	up_by_query();
	link_profiles(&wifi, GARAGE, 1, 7000);
	check(shows(LINK_JOINED, -1, ""), "up, the list changes: the host is forgotten with the call, not up any more");
	check(next(7000) == LINK_DO_LEAVE && shows(LINK_LEAVING, -1, ""), "up, the list changes: the network is left first");
	check(next(7000) == LINK_DO_NOTHING, "the leave is handed out once");
	link_left(&wifi, 8000);
	check(shows(LINK_WAITING, -1, "") && next(8000) == LINK_DO_SCAN, "up, the list changes: after the leave it starts over with a scan");
	link_scanned(&wifi, EVERYTHING, 5, 9000);
	next(9000);
	link_joined(&wifi, GATEWAY, 10000);
	check(shows(LINK_UP, 0, "192.168.1.50"), "up, the list changes: the next join uses the new list");

	// Two scans without a stored network before the join: the next wait would be 10 s
	link_init(&wifi, HOME, 1, false, 0);
	next(0);
	scan_failed(1000);
	next(3000);
	scan_failed(3000);
	next(8000);
	link_scanned(&wifi, EVERYTHING, 5, 8000);
	next(8000);
	link_joined(&wifi, GATEWAY, 9000);
	link_profiles(&wifi, HOME, 1, 9500);
	next(9500);
	link_left(&wifi, 10000);
	next(10000);
	scan_failed(11000);
	check(quiet(11000, 12999, 1) && next(13000) == LINK_DO_SCAN, "in a network, the list changes: after the leave the waits start with 2000 ms again");

	up_by_query();
	link_profiles(&wifi, HOME, 0, 7000);
	check(shows(LINK_JOINED, -1, ""), "up, the last profile is removed: still in the network, not up");
	check(next(7000) == LINK_DO_AP_ON && next(7000) == LINK_DO_LEAVE, "up, the last profile is removed: the access point is ordered on, then the leave");
	link_left(&wifi, 8000);
	check(shows(LINK_IDLE, -1, "") && quiet(8000, 100000, 500), "up, the last profile is removed: idle after the leave");

	joined(GARAGE, 1, GATEWAY);
	link_profiles(&wifi, GARAGE, 1, 7000);
	check(shows(LINK_JOINED, -1, "") && next(7000) == LINK_DO_LEAVE, "up with a stored address, the same list is reported as changed: left as well");

	// The query is under way
	finding();
	link_profiles(&wifi, GARAGE, 1, 5500);
	check(shows(LINK_JOINED, -1, "") && next(5500) == LINK_DO_NOTHING && next(3600000) == LINK_DO_NOTHING, "looking for the adapter, the list changes: the query under way ends first");
	link_found(&wifi, ADAPTER, 3600000);
	check(shows(LINK_JOINED, -1, ""), "looking for the adapter, the list changes: what the query finds is not taken");
	check(next(3600000) == LINK_DO_LEAVE, "looking for the adapter, the list changes: the leave follows the report of the query");

	finding();
	link_profiles(&wifi, GARAGE, 1, 5500);
	link_not_found(&wifi, 6000);
	check(next(6000) == LINK_DO_LEAVE, "looking for the adapter, the list changes, the query finds nothing: the leave at once, no wait");

	// Waiting to ask again
	finding();
	link_not_found(&wifi, 6000);
	link_profiles(&wifi, GARAGE, 1, 7000);
	check(next(7000) == LINK_DO_LEAVE, "waiting to ask again, the list changes: the leave at once");
	check(next(16000) == LINK_DO_NOTHING && shows(LINK_LEAVING, -1, ""), "waiting to ask again, the list changes: the query that was planned is not ordered");

	// The query for a silent adapter is under way
	up_by_query();
	link_answering(&wifi, false, 7000);
	next(67000);
	link_profiles(&wifi, GARAGE, 1, 67500);
	check(shows(LINK_JOINED, -1, "") && next(67500) == LINK_DO_NOTHING, "querying for a silent adapter, the list changes: not up any more, the query ends first");
	link_found(&wifi, MOVED, 68000);
	check(shows(LINK_JOINED, -1, "") && next(68000) == LINK_DO_LEAVE, "querying for a silent adapter, the list changes: the address found is not taken, the leave follows");

	// Silence that would be due for a query
	up_by_query();
	link_answering(&wifi, false, 7000);
	link_profiles(&wifi, GARAGE, 1, 8000);
	check(next(67000) == LINK_DO_LEAVE, "the list changes while the adapter is silent: the leave, no query");

	// The network goes before the leave is handed out
	up_by_query();
	link_profiles(&wifi, GARAGE, 1, 7000);
	link_lost(&wifi, 7500);
	check(shows(LINK_WAITING, -1, "") && next(7500) == LINK_DO_SCAN, "the list changes and the network is lost before the leave: no leave, a scan at once");
	link_scanned(&wifi, EVERYTHING, 5, 8500);
	next(8500);
	link_joined(&wifi, GATEWAY, 9500);
	check(shows(LINK_UP, 0, "192.168.1.50") && quiet(9500, 100000, 500), "no leave follows later either: the change was served by the loss");

	up_by_query();
	link_profiles(&wifi, HOME, 0, 7000);
	link_lost(&wifi, 7500);
	check(shows(LINK_IDLE, -1, "") && next(7500) == LINK_DO_AP_ON && quiet(7500, 100000, 500), "the last profile is removed and the network is lost before the leave: idle");

	// The list changes again
	up_by_query();
	link_profiles(&wifi, GARAGE, 1, 7000);
	link_profiles(&wifi, ALL, 4, 7100);
	check(next(7100) == LINK_DO_LEAVE && next(7100) == LINK_DO_NOTHING, "the list changes twice before the leave: one leave");
	link_profiles(&wifi, REVERSED, 3, 7500);
	check(shows(LINK_LEAVING, -1, "") && next(7500) == LINK_DO_NOTHING, "the list changes while the leave is under way: the leave goes on");
	link_left(&wifi, 8000);
	next(8000);
	link_scanned(&wifi, TWO, 2, 9000);
	check(shows(LINK_JOINING, 0, ""), "the list changes while the leave is under way: the scan after it serves the latest list");

	up_by_query();
	link_profiles(&wifi, GARAGE, 1, 7000);
	next(7000);
	link_profiles(&wifi, GARAGE, 0, 7500);
	check(next(7500) == LINK_DO_AP_ON, "the last profile is removed while the leave is under way: the access point is ordered on");
	link_left(&wifi, 8000);
	check(shows(LINK_IDLE, -1, "") && quiet(8000, 100000, 500), "the last profile is removed while the leave is under way: idle after it");
}

/* ------------------------------------------------------------------------------------------------ */
/* The own access point                                                                               */
/* ------------------------------------------------------------------------------------------------ */

static void test_ap_request(void)
{
	link_init(&wifi, HOME, 1, false, 0);
	next(0);
	check(!link_ap_on(&wifi) && quiet(0, 700000, 1000) && !link_ap_on(&wifi), "a profile stored, no safe mode, nobody asks: the access point is never ordered on");

	scanning(HOME, 1);
	link_ap_request(&wifi, true, 2000);
	check(!link_ap_on(&wifi), "the user asks for the access point: not on before it is ordered");
	check(next(2000) == LINK_DO_AP_ON && link_ap_on(&wifi), "the user asks for the access point: ordered on, although a scan is under way");
	check(next(2000) == LINK_DO_NOTHING && shows(LINK_SCANNING, -1, ""), "the access point is ordered once and leaves the scan alone");
	link_ap_request(&wifi, true, 2500);
	check(next(2500) == LINK_DO_NOTHING, "asked for again while it is on: no second order");
	link_ap_request(&wifi, false, 3000);
	check(link_ap_on(&wifi), "the user switches it off: on until that is ordered");
	check(next(3000) == LINK_DO_AP_OFF && !link_ap_on(&wifi), "the user switches it off: ordered off");
	check(next(3000) == LINK_DO_NOTHING, "the access point is ordered off once");
	link_ap_request(&wifi, false, 3500);
	check(next(3500) == LINK_DO_NOTHING, "switched off again while it is off: no second order");

	scanning(HOME, 1);
	link_ap_request(&wifi, true, 2000);
	link_ap_request(&wifi, false, 2100);
	check(next(2100) == LINK_DO_NOTHING && !link_ap_on(&wifi), "asked on and off again before link_next: nothing is ordered");
	link_ap_request(&wifi, true, 2200);
	next(2200);
	link_ap_request(&wifi, false, 2300);
	link_ap_request(&wifi, true, 2400);
	check(next(2400) == LINK_DO_NOTHING && link_ap_on(&wifi), "switched off and on again before link_next: nothing is ordered, it stays on");

	// The access point does not block the rest
	scanning(HOME, 1);
	link_scanned(&wifi, EVERYTHING, 5, 3000);
	link_ap_request(&wifi, true, 3000);
	check(next(3000) == LINK_DO_AP_ON && next(3000) == LINK_DO_JOIN, "the access point goes before a join that is due, the join follows with the next call");
	link_ap_request(&wifi, false, 4000);
	check(next(4000) == LINK_DO_AP_OFF && shows(LINK_JOINING, 0, ""), "the access point is ordered off while a join is under way");
	link_joined(&wifi, GATEWAY, 5000);
	link_ap_request(&wifi, true, 5000);
	check(next(5000) == LINK_DO_AP_ON && next(5000) == LINK_DO_FIND, "the access point goes before a query that is due");
	link_ap_request(&wifi, false, 5500);
	check(next(5500) == LINK_DO_AP_OFF && shows(LINK_JOINED, 0, ""), "the access point is ordered off while a query is under way");
	link_found(&wifi, ADAPTER, 6000);
	link_profiles(&wifi, GARAGE, 1, 7000);
	link_ap_request(&wifi, true, 7000);
	check(next(7000) == LINK_DO_AP_ON && next(7000) == LINK_DO_LEAVE, "the access point goes before a leave that is due");
	link_ap_request(&wifi, false, 7500);
	check(next(7500) == LINK_DO_AP_OFF && shows(LINK_LEAVING, -1, ""), "the access point is ordered off while a leave is under way");
	link_left(&wifi, 8000);
	link_ap_request(&wifi, true, 8000);
	check(next(8000) == LINK_DO_AP_ON && next(8000) == LINK_DO_SCAN, "the access point goes before a scan that is due");

	// Without a profile and in safe mode it stays on whatever is asked
	link_init(&wifi, HOME, 0, false, 0);
	next(0);
	link_ap_request(&wifi, false, 1000);
	check(quiet(1000, 700000, 1000) && link_ap_on(&wifi), "no profile stored: the access point stays on when the user switches it off");
	link_init(&wifi, HOME, 0, false, 0);
	link_ap_request(&wifi, false, 0);
	check(next(0) == LINK_DO_AP_ON, "no profile stored, switched off before the first link_next: ordered on all the same");

	link_init(&wifi, HOME, 1, true, 0);
	next(0);
	link_ap_request(&wifi, false, 1000);
	check(next(1000) == LINK_DO_SCAN && quiet(1000, 700000, 1000) && link_ap_on(&wifi), "safe mode: the access point stays on when the user switches it off");
	link_init(&wifi, HOME, 1, true, 0);
	link_ap_request(&wifi, false, 0);
	check(next(0) == LINK_DO_AP_ON, "safe mode, switched off before the first link_next: ordered on all the same");
	link_init(&wifi, HOME, 0, true, 0);
	next(0);
	link_ap_request(&wifi, false, 1000);
	check(quiet(1000, 700000, 1000) && link_ap_on(&wifi), "safe mode without a profile: the access point stays on when the user switches it off");
}

static void test_ap_idle(void)
{
	// The access point is ordered on at 2000 in all these scenes
	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	check(quiet(2000, 601999, 1) && link_ap_on(&wifi), "no client for 599999 ms after the access point was ordered on: it stays on");
	check(next(602000) == LINK_DO_AP_OFF && !link_ap_on(&wifi), "no client for 600000 ms after the access point was ordered on: it closes by itself");
	check(quiet(602000, 1300000, 1000) && !link_ap_on(&wifi), "the access point that closed by itself stays closed");
	link_ap_request(&wifi, true, 1300000);
	check(next(1300000) == LINK_DO_AP_ON && quiet(1300000, 1899999, 1) && next(1900000) == LINK_DO_AP_OFF, "asked for again later: on for another 600000 ms");

	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	link_ap_clients(&wifi, 1, 3000);
	check(quiet(3000, 3000000, 1000) && link_ap_on(&wifi), "a client is there: the access point does not close");
	link_ap_clients(&wifi, 0, 3000000);
	check(quiet(3000000, 3599999, 1) && link_ap_on(&wifi), "the last client left 599999 ms ago: it stays on");
	check(next(3600000) == LINK_DO_AP_OFF, "the last client left 600000 ms ago: it closes by itself");

	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	link_ap_clients(&wifi, 2, 3000);
	check(wifi.ap_clients == 2, "the number of clients is kept as it was reported");
	link_ap_clients(&wifi, 1, 100000);
	check(quiet(100000, 3000000, 1000) && wifi.ap_clients == 1, "one of two clients left: the access point does not close");
	link_ap_clients(&wifi, 0, 3000000);
	link_ap_clients(&wifi, 0, 3300000);
	check(next(3599999) == LINK_DO_NOTHING && next(3600000) == LINK_DO_AP_OFF, "a second report of no client does not start the 600000 ms anew");

	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	link_ap_clients(&wifi, 2, 3000);
	link_ap_clients(&wifi, 0, 100000);
	check(next(699999) == LINK_DO_NOTHING && next(700000) == LINK_DO_AP_OFF, "two clients gone at once: the 600000 ms start when they left");

	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	link_ap_clients(&wifi, 0, 500000);
	check(next(601999) == LINK_DO_NOTHING && next(602000) == LINK_DO_AP_OFF, "a report of no client while there was none does not start the 600000 ms anew");

	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	link_ap_clients(&wifi, 1, 3000);
	link_ap_clients(&wifi, -3, 4000);
	check(next(603999) == LINK_DO_NOTHING && next(604000) == LINK_DO_AP_OFF, "a negative number of clients counts as none: the 600000 ms start with it");
	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	link_ap_clients(&wifi, -1, 3000);
	link_ap_clients(&wifi, 0, 4000);
	check(next(601999) == LINK_DO_NOTHING && next(602000) == LINK_DO_AP_OFF, "a negative number of clients is no client that could leave");

	// The 600000 ms run out with a client there, which leaves later
	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	link_ap_clients(&wifi, 1, 601999);
	check(next(602000) == LINK_DO_NOTHING && link_ap_on(&wifi), "a client that came 1 ms before the access point would close keeps it open");

	// Asking for it while it is on is no client
	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	link_ap_request(&wifi, true, 500000);
	check(next(601999) == LINK_DO_NOTHING && next(602000) == LINK_DO_AP_OFF, "asking for the access point while it is on does not start the 600000 ms anew");

	// The time counts from the order, not from the request
	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	check(next(100000) == LINK_DO_AP_ON && next(699999) == LINK_DO_NOTHING && next(700000) == LINK_DO_AP_OFF, "the 600000 ms count from the order, not from the request");

	// Clients of an access point that was closed are gone, whatever was reported
	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	link_ap_clients(&wifi, 2, 3000);
	link_ap_request(&wifi, false, 4000);
	next(4000);
	link_ap_request(&wifi, true, 5000);
	check(next(5000) == LINK_DO_AP_ON && next(604999) == LINK_DO_NOTHING && next(605000) == LINK_DO_AP_OFF,
	      "clients from before the access point was closed do not count when it opens again");
	scanning(HOME, 1);
	link_ap_clients(&wifi, 1, 1500);
	link_ap_request(&wifi, true, 1800);
	check(next(2000) == LINK_DO_AP_ON && next(601999) == LINK_DO_NOTHING && next(602000) == LINK_DO_AP_OFF,
	      "a client reported while the access point was closed does not count when it opens");
	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	link_ap_clients(&wifi, 1, 3000);
	link_ap_request(&wifi, false, 4000);
	link_ap_request(&wifi, true, 4500);
	check(quiet(4500, 3000000, 1000) && link_ap_on(&wifi), "switched off and on again without an order in between: it never closed, its client still counts");

	// It closes by itself whatever the link is doing
	up_by_query();
	link_ap_request(&wifi, true, 7000);
	check(next(7000) == LINK_DO_AP_ON && quiet(7000, 606999, 1) && next(607000) == LINK_DO_AP_OFF && shows(LINK_UP, 0, ADAPTER), "the access point closes by itself while the link is up");
	finding();
	link_not_found(&wifi, 6000);
	link_ap_request(&wifi, true, 6000);
	next(6000);
	check(next(606000) == LINK_DO_AP_OFF && next(606000) == LINK_DO_FIND, "the access point closes by itself while the adapter is looked for, the query follows");
	scanning(HOME, 1);
	scan_failed(3000);
	link_ap_request(&wifi, true, 3000);
	next(3000);
	check(next(603000) == LINK_DO_AP_OFF && next(603000) == LINK_DO_SCAN, "the access point closes by itself while waiting for a scan, the scan follows");

	// It closes once
	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	next(602000);
	check(next(602000) == LINK_DO_NOTHING && next(1202000) == LINK_DO_NOTHING, "the access point is closed once");
}

static void test_ap_kept_open(void)
{
	// No profile: open for good, until one is stored
	link_init(&wifi, HOME, 0, false, 0);
	next(0);
	check(quiet(0, 3600000, 1000) && link_ap_on(&wifi), "no profile stored: the access point does not close without a client");
	link_ap_clients(&wifi, 1, 3600000);
	link_profiles(&wifi, HOME, 1, 3700000);
	check(next(3700000) == LINK_DO_SCAN && quiet(3700000, 5000000, 1000) && link_ap_on(&wifi), "a profile is stored while a client is there: the access point stays on");
	link_ap_clients(&wifi, 0, 5000000);
	check(quiet(5000000, 5599999, 1) && next(5600000) == LINK_DO_AP_OFF, "the client that stored the first profile left 600000 ms ago: the access point closes");

	link_init(&wifi, HOME, 0, false, 0);
	next(1000);
	link_profiles(&wifi, HOME, 1, 2000);
	check(next(2000) == LINK_DO_SCAN && quiet(2000, 600999, 1), "a profile is stored 1000 ms after the access point opened, no client: it stays on for 599999 ms after the order");
	check(next(601000) == LINK_DO_AP_OFF, "it closes 600000 ms after the order");

	link_init(&wifi, HOME, 0, false, 0);
	next(1000);
	link_profiles(&wifi, HOME, 1, 3600000);
	check(next(3600000) == LINK_DO_AP_OFF && next(3600000) == LINK_DO_SCAN, "a profile is stored when the access point had no client for longer than 600000 ms: it closes with the next link_next");

	// The last profile is removed
	scanning(HOME, 1);
	link_profiles(&wifi, HOME, 0, 2000);
	check(!link_ap_on(&wifi) && next(2000) == LINK_DO_AP_ON && link_ap_on(&wifi), "the last profile is removed: the access point is wanted and ordered on");
	link_scanned(&wifi, NULL, 0, 3000);
	check(quiet(3000, 3600000, 1000) && link_ap_on(&wifi), "the last profile is removed: the access point stays on without a client");
	link_ap_request(&wifi, false, 3600000);
	check(quiet(3600000, 3700000, 1000) && link_ap_on(&wifi), "the last profile is removed: the user cannot switch the access point off");

	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	link_ap_request(&wifi, false, 2500);
	link_profiles(&wifi, HOME, 0, 2600);
	check(next(2600) == LINK_DO_NOTHING && link_ap_on(&wifi), "switched off, then the last profile is removed before that was ordered: the access point stays on");
	link_ap_request(&wifi, false, 2700);
	check(next(2700) == LINK_DO_NOTHING && link_ap_on(&wifi) && wifi.phase == LINK_SCANNING, "no profile stored and a scan still under way: the user cannot switch the access point off");

	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	link_profiles(&wifi, HOME, 0, 300000);
	link_scanned(&wifi, NULL, 0, 300000);
	check(quiet(300000, 3600000, 1000) && link_ap_on(&wifi), "the last profile is removed while the access point is on: it does not close after 600000 ms");

	// Safe mode
	link_init(&wifi, HOME, 1, true, 0);
	next(0);
	check(next(0) == LINK_DO_SCAN && quiet(0, 3600000, 1000) && link_ap_on(&wifi), "safe mode: the access point does not close without a client");
	link_init(&wifi, HOME, 1, true, 0);
	next(0);
	link_ap_clients(&wifi, 1, 1000);
	link_ap_clients(&wifi, 0, 2000);
	check(next(2000) == LINK_DO_SCAN && quiet(2000, 3600000, 1000) && link_ap_on(&wifi), "safe mode: the access point does not close after its last client left");
	link_init(&wifi, HOME, 0, true, 0);
	next(0);
	link_profiles(&wifi, HOME, 1, 1000);
	check(next(1000) == LINK_DO_SCAN && quiet(1000, 3600000, 1000) && link_ap_on(&wifi), "safe mode: a profile that is stored does not let the access point close");
	link_profiles(&wifi, HOME, 0, 3600000);
	link_scanned(&wifi, NULL, 0, 3600000);
	link_ap_request(&wifi, false, 3600000);
	check(quiet(3600000, 4300000, 1000) && link_ap_on(&wifi) && shows(LINK_IDLE, -1, ""), "safe mode: it stays on when the profile is removed again and the user switches it off");
}

/* ------------------------------------------------------------------------------------------------ */
/* Reports that do not match the action under way                                                     */
/* ------------------------------------------------------------------------------------------------ */

static void scene_idle(void)
{
	link_init(&wifi, HOME, 0, false, 0);
	next(1000);
}

static void scene_waiting(void)
{
	scanning(HOME, 1);
	scan_failed(3000);
}

static void scene_scanning(void)
{
	scanning(HOME, 1);
}

static void scene_join_due(void)
{
	scanning(HOME, 1);
	link_scanned(&wifi, EVERYTHING, 5, 3000);
}

static void scene_joining(void)
{
	joining(HOME, 1);
}

static void scene_joining_changed(void)
{
	joining(HOME, 1);
	link_profiles(&wifi, GARAGE, 1, 3500);
}

static void scene_finding(void)
{
	finding();
}

static void scene_asking_again(void)
{
	finding();
	link_not_found(&wifi, 6000);
}

static void scene_up(void)
{
	up_by_query();
}

static void scene_up_given(void)
{
	joined(GARAGE, 1, GATEWAY);
}

static void scene_up_finding(void)
{
	up_by_query();
	link_answering(&wifi, false, 7000);
	next(67000);
}

static void scene_leave_due(void)
{
	up_by_query();
	link_profiles(&wifi, GARAGE, 1, 7000);
}

static void scene_leaving(void)
{
	up_by_query();
	link_profiles(&wifi, GARAGE, 1, 7000);
	next(7000);
}

static void report_scanned(uint64_t now_ms)
{
	link_scanned(&wifi, EVERYTHING, 5, now_ms);
}

static void report_joined(uint64_t now_ms)
{
	link_joined(&wifi, GATEWAY, now_ms);
}

static void report_join_failed(uint64_t now_ms)
{
	link_join_failed(&wifi, now_ms);
}

static void report_lost(uint64_t now_ms)
{
	link_lost(&wifi, now_ms);
}

static void report_left(uint64_t now_ms)
{
	link_left(&wifi, now_ms);
}

static void report_found(uint64_t now_ms)
{
	link_found(&wifi, MOVED, now_ms);
}

static void report_not_found(uint64_t now_ms)
{
	link_not_found(&wifi, now_ms);
}

#define REPORTS     7

static void test_reports_that_do_not_match(void)
{
	static const struct
	{
		const char *name;
		void (*report)(uint64_t now_ms);
	} reports[REPORTS] = {
		{"link_scanned", report_scanned}, {"link_joined", report_joined}, {"link_join_failed", report_join_failed}, {"link_lost", report_lost},
		{"link_left", report_left}, {"link_found", report_found}, {"link_not_found", report_not_found},
	};
	// By the header: which report ends or changes something in which state
	static const struct
	{
		const char *name;
		void (*scene)(void);
		bool taken[REPORTS];
	} scenes[] = {
		{"idle", scene_idle,                                                {false, false, false, false, false, false, false}},
		{"waiting for the next scan", scene_waiting,                        {false, false, false, false, false, false, false}},
		{"a scan under way", scene_scanning,                                {true, false, false, false, false, false, false}},
		{"a join due but not handed out", scene_join_due,                   {false, false, false, false, false, false, false}},
		{"a join under way", scene_joining,                                 {false, true, true, false, false, false, false}},
		{"a join under way and the list changed", scene_joining_changed,    {false, true, true, false, false, false, false}},
		{"a query under way", scene_finding,                                {false, false, false, true, false, true, true}},
		{"waiting to ask again", scene_asking_again,                        {false, false, false, true, false, false, false}},
		{"up by a query", scene_up,                                         {false, false, false, true, false, false, false}},
		{"up by a stored address", scene_up_given,                          {false, false, false, true, false, false, false}},
		{"up and a query under way", scene_up_finding,                      {false, false, false, true, false, true, true}},
		{"a leave due but not handed out", scene_leave_due,                 {false, false, false, true, false, false, false}},
		{"a leave under way", scene_leaving,                                {false, false, false, false, true, false, false}},
	};
	size_t s;
	int r;

	for(s = 0; s < sizeof(scenes) / sizeof(scenes[0]); s++)
	{
		for(r = 0; r < REPORTS; r++)
		{
			char what[160];
			link_t before;

			scenes[s].scene();
			before = wifi;
			// At the time of the last call of the scene, so that no time limit is involved
			reports[r].report(wifi.clock_ms);
			if(scenes[s].taken[r])
			{
				snprintf(what, sizeof(what), "%s: %s is taken", scenes[s].name, reports[r].name);
				check(!same_link(&wifi, &before), what);
			}
			else
			{
				snprintf(what, sizeof(what), "%s: %s is ignored, nothing changes", scenes[s].name, reports[r].name);
				check(same_link(&wifi, &before) && wifi.clock_ms == before.clock_ms, what);
			}
		}
	}

	// What follows an ignored report is what would have followed without it
	scene_asking_again();
	link_not_found(&wifi, 9000);
	link_found(&wifi, MOVED, 9500);
	check(shows(LINK_JOINED, 0, "") && next(15999) == LINK_DO_NOTHING && next(16000) == LINK_DO_FIND, "reports of a query while none is under way do not move the next query");

	scene_up();
	link_found(&wifi, MOVED, 7000);
	check(shows(LINK_UP, 0, ADAPTER), "an address reported while no query is under way does not replace the one in use");

	scene_waiting();
	link_left(&wifi, 4000);
	link_join_failed(&wifi, 4000);
	check(next(4999) == LINK_DO_NOTHING && next(5000) == LINK_DO_SCAN, "reports while waiting do not shorten or restart the wait");

	scene_joining();
	link_scanned(&wifi, STRANGERS, 2, 4000);
	link_lost(&wifi, 4000);
	link_left(&wifi, 4000);
	link_not_found(&wifi, 4000);
	check(shows(LINK_JOINING, 0, "") && next(17999) == LINK_DO_NOTHING && next(18000) == LINK_DO_JOIN, "reports of other actions do not end a join nor restart its time limit");

	scene_scanning();
	link_joined(&wifi, GATEWAY, 2000);
	check(shows(LINK_SCANNING, -1, "") && next(2000) == LINK_DO_NOTHING, "a join reported during a scan: not in a network, the scan goes on");
}

/* ------------------------------------------------------------------------------------------------ */
/* The time                                                                                           */
/* ------------------------------------------------------------------------------------------------ */

static void test_clock(void)
{
	// A wait
	scanning(HOME, 1);
	scan_failed(10000);
	check(next(4000) == LINK_DO_NOTHING && next(0) == LINK_DO_NOTHING && next(11999) == LINK_DO_NOTHING, "the time steps back during a wait: no time passed, no scan");
	check(next(12000) == LINK_DO_SCAN, "the wait ends 2000 ms after the latest time seen before the step back");

	scanning(HOME, 1);
	next(10000);
	scan_failed(3000);
	check(next(11999) == LINK_DO_NOTHING && next(12000) == LINK_DO_SCAN, "a scan reported with a time before the latest one seen: the wait counts from the latest");

	// A report that is ignored brings its time along like every call
	scanning(HOME, 1);
	scan_failed(3000);
	link_left(&wifi, 5000);
	check(next(3000) == LINK_DO_SCAN, "the time of a report that is ignored counts: a wait is over with it");

	// The time limit of a join
	scanning(HOME, 1);
	link_scanned(&wifi, EVERYTHING, 5, 3000);
	next(10000);
	check(next(3000) == LINK_DO_NOTHING && next(24999) == LINK_DO_NOTHING && next(25000) == LINK_DO_JOIN, "a join handed out at 10000, then the time steps back: overdue at 25000");
	scanning(HOME, 1);
	link_scanned(&wifi, EVERYTHING, 5, 10000);
	check(next(3000) == LINK_DO_JOIN && next(24999) == LINK_DO_NOTHING && next(25000) == LINK_DO_JOIN, "a join handed out with a time before the latest one seen: its time limit counts from the latest");
	joining(HOME, 1);
	link_join_failed(&wifi, 2000);
	next(2500);
	link_join_failed(&wifi, 2600);
	check(next(4999) == LINK_DO_NOTHING && next(5000) == LINK_DO_SCAN, "failures reported with times before the latest one seen: the wait counts from the latest");

	// Queries
	finding();
	link_not_found(&wifi, 9000);
	check(next(6000) == LINK_DO_NOTHING && next(18999) == LINK_DO_NOTHING && next(19000) == LINK_DO_FIND, "the time steps back after a query that found nothing: asked again 10000 ms after the latest time seen");
	finding();
	next(9000);
	link_not_found(&wifi, 6000);
	check(next(18999) == LINK_DO_NOTHING && next(19000) == LINK_DO_FIND, "a query reported with a time before the latest one seen: the 10000 ms count from the latest");

	up_by_query();
	next(20000);
	link_answering(&wifi, false, 7000);
	check(next(79999) == LINK_DO_NOTHING && next(80000) == LINK_DO_FIND, "silence reported with a time before the latest one seen: the 60000 ms count from the latest");
	up_by_query();
	link_answering(&wifi, false, 7000);
	next(50000);
	check(next(8000) == LINK_DO_NOTHING && next(66999) == LINK_DO_NOTHING && next(67000) == LINK_DO_FIND, "the time steps back while the adapter is silent: queried when 60000 ms were counted");

	// The access point
	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	next(400000);
	check(next(3000) == LINK_DO_NOTHING && next(601999) == LINK_DO_NOTHING && next(602000) == LINK_DO_AP_OFF, "the time steps back while the access point waits for a client: it closes when 600000 ms were counted");
	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	link_ap_clients(&wifi, 1, 3000);
	next(100000);
	link_ap_clients(&wifi, 0, 50000);
	check(next(699999) == LINK_DO_NOTHING && next(700000) == LINK_DO_AP_OFF, "the last client reported gone with a time before the latest one seen: the 600000 ms count from the latest");
	scanning(HOME, 1);
	link_ap_request(&wifi, true, 500000);
	check(next(2000) == LINK_DO_AP_ON && next(1099999) == LINK_DO_NOTHING && next(1100000) == LINK_DO_AP_OFF, "the time of a request counts like the time of every call");
	scanning(HOME, 1);
	link_ap_request(&wifi, true, 1500);
	next(2000);
	check(next(1000) == LINK_DO_NOTHING && next(0) == LINK_DO_NOTHING && link_ap_on(&wifi), "a time before the one the access point was ordered on with does not close it");

	// Every call brings the time along
	link_init(&wifi, HOME, 1, false, 50000);
	next(0);
	scan_failed(1000);
	check(next(51999) == LINK_DO_NOTHING && next(52000) == LINK_DO_SCAN, "the time of link_init counts");
	scanning(HOME, 1);
	scan_failed(3000);
	link_answering(&wifi, true, 5000);
	check(next(0) == LINK_DO_SCAN, "the time of link_answering counts");
	scanning(HOME, 1);
	scan_failed(3000);
	link_ap_clients(&wifi, 0, 5000);
	check(next(0) == LINK_DO_SCAN, "the time of link_ap_clients counts");
	scanning(HOME, 1);
	scan_failed(3000);
	link_profiles(&wifi, HOME, 1, 50000);
	next(0);
	scan_failed(0);
	check(next(51999) == LINK_DO_NOTHING && next(52000) == LINK_DO_SCAN, "the time of link_profiles counts");
	finding();
	link_lost(&wifi, 50000);
	next(0);
	scan_failed(0);
	check(next(51999) == LINK_DO_NOTHING && next(52000) == LINK_DO_SCAN, "the time of link_lost counts");
	scene_leaving();
	link_left(&wifi, 50000);
	next(0);
	scan_failed(0);
	check(next(51999) == LINK_DO_NOTHING && next(52000) == LINK_DO_SCAN, "the time of link_left counts");
	joining(HOME, 1);
	link_joined(&wifi, GATEWAY, 50000);
	next(0);
	link_not_found(&wifi, 0);
	check(next(59999) == LINK_DO_NOTHING && next(60000) == LINK_DO_FIND, "the time of link_joined counts");
	finding();
	link_found(&wifi, ADAPTER, 50000);
	link_answering(&wifi, false, 0);
	check(next(109999) == LINK_DO_NOTHING && next(110000) == LINK_DO_FIND, "the time of link_found counts");
}

static void test_clock_limits(void)
{
	const uint64_t last = UINT64_MAX;

	link_init(&wifi, HOME, 1, false, last - 100000);
	next(last - 100000);
	scan_failed(last - 90000);
	check(next(last - 88001) == LINK_DO_NOTHING && next(last - 88000) == LINK_DO_SCAN, "a wait near the largest time ends when it is over");
	scan_failed(last - 1000);
	check(next(last - 1000) == LINK_DO_NOTHING && next(last - 1) == LINK_DO_NOTHING, "a wait that would end behind the largest time does not end at once");
	check(next(last) == LINK_DO_SCAN, "a wait that would end behind the largest time ends with the largest time");
	check(next(0) == LINK_DO_NOTHING && next(last) == LINK_DO_NOTHING && wifi.phase == LINK_SCANNING, "a time of 0 behind the largest time is a step back");

	link_init(&wifi, HOME, 1, false, last - 100000);
	next(last - 100000);
	link_scanned(&wifi, EVERYTHING, 5, last - 99000);
	next(last - 99000);
	link_joined(&wifi, GATEWAY, last - 98000);
	next(last - 98000);
	link_not_found(&wifi, last - 5000);
	check(next(last - 5000) == LINK_DO_NOTHING && next(last - 1) == LINK_DO_NOTHING && next(last) == LINK_DO_FIND, "a query that would be repeated behind the largest time is repeated with the largest time");

	link_init(&wifi, HOME, 1, false, last - 100000);
	next(last - 100000);
	link_scanned(&wifi, EVERYTHING, 5, last - 99000);
	next(last - 15000);
	check(next(last - 1) == LINK_DO_NOTHING && next(last) == LINK_DO_JOIN, "the time limit of a join runs out at the largest time");
}

/* ------------------------------------------------------------------------------------------------ */
/* Model: the rules of link.h once more, written from the header text and in another shape. The       */
/* module keeps a phase and points in time; the model keeps what the driver is doing, whether the      */
/* station is in a network, and durations that are counted up or down with every call. The phase is    */
/* derived from that.                                                                                 */
/* ------------------------------------------------------------------------------------------------ */

typedef enum { ACT_NONE, ACT_SCAN, ACT_JOIN, ACT_LEAVE, ACT_FIND } act_t;

typedef struct
{
	const net_profile_t *list;
	int stored;                 // profiles the list holds
	uint64_t latest;            // latest time seen

	act_t running;              // action that was handed out and has not ended
	uint64_t join_for;          // how long the join under way runs

	bool in_network;
	bool outdated;              // the list changed since the join began or the network was joined
	int target;                 // profile chosen by the last scan, -1 if none or outdated
	int attempts;               // joins handed out since that scan

	int pauses;                 // pauses since the last start from the beginning
	uint64_t pause_left;        // until the next scan

	char adapter[LINK_HOST_SIZE];
	bool queried;               // adapter came from a query
	uint64_t query_left;        // until the next query
	bool silent;
	uint64_t silent_for;

	bool safe;
	bool ap_wish;
	bool ap_ordered;
	int guests;
	uint64_t empty_for;         // how long the access point has no guest
} model_t;

static void model_time(model_t *model, uint64_t now_ms)
{
	uint64_t passed = now_ms > model->latest ? now_ms - model->latest : 0;

	model->latest += passed;
	model->join_for += passed;
	model->silent_for += passed;
	model->empty_for += passed;
	model->pause_left = model->pause_left > passed ? model->pause_left - passed : 0;
	model->query_left = model->query_left > passed ? model->query_left - passed : 0;
}

static int model_count(int count)
{
	return count == 1 || count == 2 || count == 3 || count == 4 ? count : 0;
}

// A text that can be an address: 1 to 39 bytes
static bool model_address(const char *text)
{
	int i;

	if(text == NULL || text[0] == '\0') return false;
	for(i = 1; i <= 39; i++)
	{
		if(text[i] == '\0') return true;
	}
	return false;
}

static void model_from_the_beginning(model_t *model)
{
	model->running = ACT_NONE;
	model->in_network = false;
	model->outdated = false;
	model->target = -1;
	model->adapter[0] = '\0';
	model->pauses = 0;
	model->pause_left = 0;
}

static void model_pause(model_t *model)
{
	static const uint64_t seconds[] = {2, 5, 10, 30};

	model->target = -1;
	model->pause_left = seconds[model->pauses < 3 ? model->pauses : 3] * 1000;
	model->pauses++;
}

static void model_init(model_t *model, const net_profile_t *list, int count, bool safe, uint64_t now_ms)
{
	memset(model, 0, sizeof(*model));
	model->list = list;
	model->stored = model_count(count);
	model->latest = now_ms;
	model->safe = safe;
	model->ap_wish = safe || model->stored == 0;
	model_from_the_beginning(model);
}

static link_phase_t model_phase(const model_t *model)
{
	if(model->running == ACT_SCAN) return LINK_SCANNING;
	if(model->running == ACT_JOIN) return LINK_JOINING;
	if(model->running == ACT_LEAVE) return LINK_LEAVING;
	if(model->in_network) return model->adapter[0] != '\0' ? LINK_UP : LINK_JOINED;
	if(model->target >= 0) return LINK_JOINING;
	return model->stored > 0 ? LINK_WAITING : LINK_IDLE;
}

// The join under way ended without the network
static void model_join_over(model_t *model)
{
	model->running = ACT_NONE;
	if(model->outdated) model_from_the_beginning(model);
	else if(model->attempts == 2) model_pause(model);
}

static link_do_t model_next(model_t *model, uint64_t now_ms)
{
	model_time(model, now_ms);
	if(model->running == ACT_JOIN && model->join_for >= 15000) model_join_over(model);

	if(model->ap_ordered && !model->safe && model->stored > 0 && model->guests == 0 && model->empty_for >= 600000) model->ap_wish = false;
	if(model->ap_wish && !model->ap_ordered)
	{
		model->ap_ordered = true;
		model->guests = 0;
		model->empty_for = 0;
		return LINK_DO_AP_ON;
	}
	if(!model->ap_wish && model->ap_ordered)
	{
		model->ap_ordered = false;
		return LINK_DO_AP_OFF;
	}

	if(model->running != ACT_NONE) return LINK_DO_NOTHING;

	if(model->in_network)
	{
		bool wanted = model->adapter[0] == '\0' || (model->queried && model->silent && model->silent_for >= 60000);

		if(model->outdated)
		{
			model->running = ACT_LEAVE;
			return LINK_DO_LEAVE;
		}
		if(!wanted || model->query_left > 0) return LINK_DO_NOTHING;
		model->running = ACT_FIND;
		return LINK_DO_FIND;
	}
	if(model->target >= 0)
	{
		model->running = ACT_JOIN;
		model->attempts++;
		model->join_for = 0;
		return LINK_DO_JOIN;
	}
	if(model->stored > 0 && model->pause_left == 0)
	{
		model->running = ACT_SCAN;
		return LINK_DO_SCAN;
	}
	return LINK_DO_NOTHING;
}

static void model_scanned(model_t *model, const char (*seen)[NET_SSID_SIZE], int seen_count, uint64_t now_ms)
{
	int p, s;

	model_time(model, now_ms);
	if(model->running != ACT_SCAN) return;

	model->running = ACT_NONE;
	model->attempts = 0;
	for(p = model->stored - 1; p >= 0; p--)
	{
		for(s = 0; s < seen_count && s < 20; s++)
		{
			if(model->list[p].ssid[0] != '\0' && strcmp(model->list[p].ssid, seen[s]) == 0) model->target = p;
		}
	}
	if(model->target < 0 && model->stored > 0) model_pause(model);
}

static void model_joined(model_t *model, const char *gateway, uint64_t now_ms)
{
	const net_profile_t *profile;
	const char *address = NULL;
	char stored[LINK_HOST_SIZE + 1];

	model_time(model, now_ms);
	if(model->running != ACT_JOIN) return;

	model->running = ACT_NONE;
	model->in_network = true;
	if(model->outdated) return;

	profile = &model->list[model->target];
	// A host that fills its array has no end
	memcpy(stored, profile->host, LINK_HOST_SIZE);
	stored[LINK_HOST_SIZE] = '\0';
	if(stored[0] != '\0') address = stored;
	else if(strlen(profile->ssid) > 6 && memcmp(profile->ssid, "WiCAN_", 6) == 0) address = gateway;

	if(model_address(address))
	{
		strcpy(model->adapter, address);
		model->queried = false;
		model->silent = false;
	}
	else
	{
		model->query_left = 0;
	}
}

static void model_join_failed(model_t *model, uint64_t now_ms)
{
	model_time(model, now_ms);
	if(model->running == ACT_JOIN) model_join_over(model);
}

static void model_lost(model_t *model, uint64_t now_ms)
{
	model_time(model, now_ms);
	if(model->in_network && model->running != ACT_LEAVE) model_from_the_beginning(model);
}

static void model_left(model_t *model, uint64_t now_ms)
{
	model_time(model, now_ms);
	if(model->running == ACT_LEAVE) model_from_the_beginning(model);
}

// address NULL: not found
static void model_found(model_t *model, const char *address, uint64_t now_ms)
{
	model_time(model, now_ms);
	if(model->running != ACT_FIND) return;

	model->running = ACT_NONE;
	if(model->outdated) return;

	if(model_address(address))
	{
		strcpy(model->adapter, address);
		model->queried = true;
		model->silent = false;
	}
	else
	{
		model->query_left = 10000;
	}
}

static void model_answering(model_t *model, bool answering, uint64_t now_ms)
{
	model_time(model, now_ms);
	if(!answering && !model->silent) model->silent_for = 0;
	model->silent = !answering;
}

static void model_profiles(model_t *model, const net_profile_t *list, int count, uint64_t now_ms)
{
	model_time(model, now_ms);
	model->list = list;
	model->stored = model_count(count);
	if(model->stored == 0) model->ap_wish = true;

	if(model->running == ACT_LEAVE) return;
	if(model->running == ACT_SCAN)
	{
		model->pauses = 0;
		return;
	}
	if(model->in_network || model->running == ACT_JOIN)
	{
		model->outdated = true;
		model->target = -1;
		model->adapter[0] = '\0';
		return;
	}
	model_from_the_beginning(model);
}

static void model_ap_request(model_t *model, bool on, uint64_t now_ms)
{
	model_time(model, now_ms);
	model->ap_wish = on;
	if(model->safe || model->stored == 0) model->ap_wish = true;
}

static void model_ap_clients(model_t *model, int clients, uint64_t now_ms)
{
	model_time(model, now_ms);
	if(clients < 1)
	{
		if(model->guests >= 1) model->empty_for = 0;
		clients = 0;
	}
	model->guests = clients;
}

/* ------------------------------------------------------------------------------------------------ */
/* What the header promises about the actions, watched without the model: only from the calls that    */
/* were made and from what the module handed out and shows.                                           */
/* ------------------------------------------------------------------------------------------------ */

typedef struct
{
	long calls, walks;
	long different;             // module and model disagree
	long two_actions;           // an action that needs a report while another one waits for its report
	long join_unseen;           // a join of a profile that the last scan did not see
	long in_network;            // a scan or a join while in a network
	long no_profile;            // a scan or a join without a stored profile
	long ap_off;                // the access point ordered off in safe mode or without a profile
	long host_not_up;           // a host although not up, or up without one
	long scan_early;            // a scan sooner after a failure than the wait of the header
	long stuck;                 // an honest driver did not get it up, or idle, in time
	long handed[LINK_DO_AP_OFF + 1];
	long phases[LINK_UP + 1];
	long timeouts, queries_up, steps_back, alive_checks, ignored, waits_checked;
} walk_result_t;

typedef struct
{
	const net_profile_t *list;
	int stored;
	bool safe;
	uint64_t latest;
	act_t open;                 // action that waits for its report
	uint64_t join_at;
	bool in_network;
	bool list_changed;          // since the join was handed out
	const char (*seen)[NET_SSID_SIZE];
	int seen_count;
	bool scan_valid;            // a scan ended since the list changed
	bool after_failure;         // the last scan or join series failed and nothing started the waits anew
	uint64_t failure_at;
	int failures;               // in a row
} watch_t;

static void watch_init(watch_t *watch, const net_profile_t *list, int count, bool safe, uint64_t now_ms)
{
	memset(watch, 0, sizeof(*watch));
	watch->list = list;
	watch->stored = count >= 0 && count <= NET_PROFILES_MAX ? count : 0;
	watch->safe = safe;
	watch->latest = now_ms;
}

static void watch_fresh(watch_t *watch)
{
	watch->after_failure = false;
	watch->failures = 0;
}

static void watch_failure(watch_t *watch)
{
	watch->after_failure = true;
	watch->failure_at = watch->latest;
	watch->failures++;
}

static bool watch_seen(const watch_t *watch, const char *ssid)
{
	int i;

	if(ssid[0] == '\0') return false;
	for(i = 0; i < watch->seen_count && i < LINK_SEEN_MAX; i++)
	{
		if(strcmp(watch->seen[i], ssid) == 0) return true;
	}
	return false;
}

// An action was handed out
static void watch_action(watch_t *watch, const link_t *walked, link_do_t action, walk_result_t *result)
{
	static const uint64_t waits[] = {2000, 5000, 10000, 30000};
	int profile = link_profile(walked);

	result->handed[action]++;
	if(action == LINK_DO_AP_OFF && (watch->safe || watch->stored == 0)) result->ap_off++;
	if(action == LINK_DO_NOTHING || action == LINK_DO_AP_ON || action == LINK_DO_AP_OFF) return;

	if(watch->open != ACT_NONE) result->two_actions++;
	if(action == LINK_DO_SCAN || action == LINK_DO_JOIN)
	{
		if(watch->in_network) result->in_network++;
		if(watch->stored == 0) result->no_profile++;
	}
	switch(action)
	{
		case LINK_DO_SCAN:
			watch->open = ACT_SCAN;
			if(watch->after_failure)
			{
				result->waits_checked++;
				if(watch->latest - watch->failure_at < waits[watch->failures < 4 ? watch->failures - 1 : 3]) result->scan_early++;
			}
			break;

		case LINK_DO_JOIN:
			watch->open = ACT_JOIN;
			watch->join_at = watch->latest;
			watch->list_changed = false;
			if(!watch->scan_valid || profile < 0 || profile >= watch->stored || !watch_seen(watch, watch->list[profile].ssid)) result->join_unseen++;
			break;

		case LINK_DO_LEAVE:
			watch->open = ACT_LEAVE;
			if(!watch->in_network) result->in_network++;
			break;

		default:
			watch->open = ACT_FIND;
			if(!watch->in_network) result->in_network++;
			if(link_up(walked)) result->queries_up++;
			break;
	}
}

/* ------------------------------------------------------------------------------------------------ */
/* Walk                                                                                               */
/* ------------------------------------------------------------------------------------------------ */

static uint32_t walk_random_state;

static uint32_t walk_random(uint32_t below)
{
	walk_random_state = walk_random_state * 1664525u + 1013904223u;
	return (walk_random_state >> 8) % below;
}

// Lists with a profile without an SSID and a profile whose host has no end. In `odd` the damaged one is the
// last of its array, so that a read behind its host is a read behind the array.
static net_profile_t odd[4];
static net_profile_t nameless[1];

static void walk_lists(void)
{
	memset(odd, 0, sizeof(odd));
	strcpy(odd[1].ssid, "WiCAN_x");
	strcpy(odd[2].ssid, "Home");
	strcpy(odd[2].password, "homepassword");
	strcpy(odd[3].ssid, "Broken");
	memset(odd[3].host, '9', sizeof(odd[3].host));
	memset(nameless, 0, sizeof(nameless));
}

// An honest driver in a world where every stored network is in range and the adapter is found: whatever
// state the link is in, it has to come up - or come to rest without a scan if no profile is stored, or keep
// scanning if no stored network has a name. Works on a copy.
static bool comes_up(link_t copy, act_t open, const net_profile_t *list, int stored, uint64_t now_ms)
{
	char seen[NET_PROFILES_MAX][NET_SSID_SIZE];
	bool nameable = false;
	int scans = 0, joins = 0, second, i;

	memset(seen, 0, sizeof(seen));
	for(i = 0; i < stored; i++)
	{
		memcpy(seen[i], list[i].ssid, NET_SSID_SIZE);
		if(list[i].ssid[0] != '\0') nameable = true;
	}

	// The action under way gets its report
	if(open == ACT_SCAN) link_scanned(&copy, seen, stored, now_ms);
	if(open == ACT_JOIN) link_joined(&copy, GATEWAY, now_ms);
	if(open == ACT_LEAVE) link_left(&copy, now_ms);
	if(open == ACT_FIND) link_found(&copy, ADAPTER, now_ms);

	for(second = 0; second < 80; second++)
	{
		uint64_t now = now_ms + (uint64_t)second * 1000;
		link_do_t action = link_next(&copy, now);

		if(action == LINK_DO_SCAN)
		{
			scans++;
			link_scanned(&copy, seen, stored, now);
		}
		if(action == LINK_DO_JOIN)
		{
			joins++;
			link_joined(&copy, GATEWAY, now);
		}
		if(action == LINK_DO_LEAVE) link_left(&copy, now);
		if(action == LINK_DO_FIND) link_found(&copy, ADAPTER, now);
		if(stored > 0 && nameable && link_up(&copy)) return true;
	}
	if(stored == 0) return copy.phase == LINK_IDLE && scans == 0 && joins == 0;
	return !nameable && scans >= 2 && joins == 0;
}

#define WALKS           400
#define WALK_STEPS      2500

typedef enum
{
	OP_NEXT, OP_SCANNED, OP_JOINED, OP_JOIN_FAILED, OP_LOST, OP_LEFT, OP_FOUND, OP_NOT_FOUND, OP_ANSWERING, OP_PROFILES, OP_AP_REQUEST,
	OP_AP_CLIENTS, OP_INIT,
} op_t;

static void walk(uint32_t seed, walk_result_t *result)
{
	static const char *const op_names[] = {"next", "scanned", "joined", "join_failed", "lost", "left", "found", "not_found", "answering",
	                                       "profiles", "ap_request", "ap_clients", "init"};
	static const char seen_odd[4][NET_SSID_SIZE] = {"", "Broken", "Net", "WiCAN_x"};
	static const char seen_broken[2][NET_SSID_SIZE] = {"Broken", ""};
	static const struct
	{
		const net_profile_t *list;
		int count;
	} lists[] = {
		{ALL, 0}, {HOME, 1}, {WICAN, 1}, {GARAGE, 1}, {ALL, 4}, {ALL, 4}, {ALL, 2}, {REVERSED, 3}, {odd, 4}, {odd, 4}, {odd, 1}, {nameless, 1},
		{ALL, 5}, {ALL, -1}, {HOME, INT_MAX}, {HOME, INT_MIN},
	};
	static const struct
	{
		const char (*seen)[NET_SSID_SIZE];
		int count;
	} scans[] = {
		{NULL, 0}, {STRANGERS, 2}, {EVERYTHING, 5}, {EVERYTHING, 5}, {EVERYTHING, 1}, {EVERYTHING, -1}, {ONLY_HOME, 2}, {ONLY_PHONE, 2}, {TWO, 2},
		{TWO_SWAPPED, 2}, {TWO, 1}, {CROWD, 25}, {CROWD, 20}, {CROWD, 19}, {CROWD, 1000}, {CROWD, INT_MAX}, {seen_odd, 4}, {seen_broken, 2},
		{seen_broken, 1}, {EVERYTHING, INT_MIN},
	};
	static char long_39[40], long_40[41], long_90[91];
	const char *gateways[] = {NULL, "", GATEWAY, GATEWAY, "10.0.0.1", "g\303\244te.local", long_39, long_40, long_90};
	const char *addresses[] = {"", ADAPTER, ADAPTER, MOVED, "wican.local", "x", "caf\303\251.local", long_39, long_40, long_90};
	static const uint64_t starts[] = {0, 0, 1000, 4294967296ull - 20000, 1099511627776ull};
	static const uint64_t small_steps[] = {0, 0, 1, 7, 100, 500, 999, 1000, 1001};
	static const uint64_t limit_steps[] = {1999, 2000, 2001, 4999, 5000, 5001, 9999, 10000, 10001, 14999, 15000, 15001, 29999, 30000, 30001,
	                                       59999, 60000, 60001};
	static const uint64_t huge_steps[] = {599999, 600000, 600001, 3600000};
	static const int client_counts[] = {-2, 0, 0, 0, 1, 1, 2, 5};
	link_t walked;
	model_t model;
	watch_t watch;
	uint64_t now;
	uint32_t which;
	bool safe;
	int step, shown = 0;

	memset(long_39, 'a', 39);
	memset(long_40, 'b', 40);
	memset(long_90, 'c', 90);
	walk_random_state = seed;
	which = walk_random(sizeof(lists) / sizeof(lists[0]));
	safe = walk_random(4) == 0;
	now = starts[walk_random(sizeof(starts) / sizeof(starts[0]))];

	// Whatever stood in the memory before
	memset(&walked, (int)(seed & 0xFF), sizeof(walked));
	link_init(&walked, lists[which].list, lists[which].count, safe, now);
	model_init(&model, lists[which].list, lists[which].count, safe, now);
	watch_init(&watch, lists[which].list, lists[which].count, safe, now);
	result->walks++;

	for(step = 0; step < WALK_STEPS; step++)
	{
		link_do_t action = LINK_DO_NOTHING, expected = LINK_DO_NOTHING;
		link_phase_t phase_before = walked.phase;
		act_t open_before = watch.open;
		uint32_t pace = walk_random(100);
		uint32_t dice = walk_random(100);
		uint64_t given;
		bool overdue = false, same;
		op_t op;

		// Mostly small steps, often exactly to one of the limits that run at the moment (1 ms before it, at it,
		// 1 ms behind it), sometimes long ones, sometimes a time before the latest one
		if(pace < 35)
		{
			now += small_steps[walk_random(sizeof(small_steps) / sizeof(small_steps[0]))];
		}
		else if(pace < 45)
		{
			now += limit_steps[walk_random(sizeof(limit_steps) / sizeof(limit_steps[0]))];
		}
		else if(pace < 47)
		{
			now += huge_steps[walk_random(sizeof(huge_steps) / sizeof(huge_steps[0]))];
		}
		else if(pace < 85)
		{
			uint64_t left[5];
			int count = 0;

			// Only limits that matter in the state of the moment: one that is left over from another state has
			// to stay in the way for a while
			if(model.pause_left > 0 && model_phase(&model) == LINK_WAITING) left[count++] = model.pause_left;
			if(model.query_left > 0 && model.in_network && model.running == ACT_NONE) left[count++] = model.query_left;
			if(model.running == ACT_JOIN && model.join_for < 15000) left[count++] = 15000 - model.join_for;
			if(model.silent && model.silent_for < 60000 && model_phase(&model) == LINK_UP && model.queried) left[count++] = 60000 - model.silent_for;
			if(model.ap_ordered && model.guests == 0 && model.empty_for < 600000 && walk_random(4) == 0) left[count++] = 600000 - model.empty_for;
			if(count > 0) now = model.latest + left[walk_random((uint32_t)count)] + walk_random(3) - 1;
		}
		given = now;
		if(pace >= 85 && pace < 91)
		{
			// For this call only
			uint64_t back = walk_random(3) == 0 ? 700000 : walk_random(5000);

			given = walk_random(8) == 0 || now < back ? 0 : now - back;
			result->steps_back++;
		}
		else if(pace >= 91 && pace < 93)
		{
			// For good: the time stands until the calls have caught up
			uint64_t back = walk_random(3000);

			now = now < back ? 0 : now - back;
			given = now;
			result->steps_back++;
		}

		// Half of the calls ask what to do, a quarter is the report the action under way waits for, the rest is
		// any call at any time
		if(dice < 50) op = OP_NEXT;
		else if(dice < 75 && watch.open == ACT_SCAN) op = OP_SCANNED;
		else if(dice < 75 && watch.open == ACT_JOIN) op = walk_random(3) == 0 ? OP_JOIN_FAILED : OP_JOINED;
		else if(dice < 75 && watch.open == ACT_LEAVE) op = OP_LEFT;
		else if(dice < 75 && watch.open == ACT_FIND) op = walk_random(3) == 0 ? OP_NOT_FOUND : OP_FOUND;
		else if(dice < 75) op = OP_NEXT;
		else if(dice < 79) op = OP_ANSWERING;
		else if(dice < 82) op = OP_AP_CLIENTS;
		else if(dice < 84) op = OP_AP_REQUEST;
		else if(dice < 86) op = OP_PROFILES;
		else if(dice < 87) op = OP_LOST;
		else if(dice < 99) op = (op_t)(OP_SCANNED + walk_random(OP_AP_CLIENTS - OP_SCANNED + 1));
		else op = walk_random(8) == 0 ? OP_INIT : OP_NEXT;

		if(given > watch.latest) watch.latest = given;
		switch(op)
		{
			case OP_NEXT:
				if(watch.open == ACT_JOIN && watch.latest - watch.join_at >= 15000)
				{
					overdue = true;
					watch.open = ACT_NONE;
					result->timeouts++;
				}
				action = link_next(&walked, given);
				expected = model_next(&model, given);
				if(overdue)
				{
					if(watch.list_changed) watch_fresh(&watch);
					else if(walked.phase == LINK_WAITING) watch_failure(&watch);
				}
				watch_action(&watch, &walked, action, result);
				break;

			case OP_SCANNED:
			{
				uint32_t scan = walk_random(sizeof(scans) / sizeof(scans[0]));

				link_scanned(&walked, scans[scan].seen, scans[scan].count, given);
				model_scanned(&model, scans[scan].seen, scans[scan].count, given);
				if(watch.open == ACT_SCAN)
				{
					watch.open = ACT_NONE;
					watch.seen = scans[scan].seen;
					watch.seen_count = scans[scan].count;
					watch.scan_valid = true;
					if(walked.phase == LINK_WAITING) watch_failure(&watch);
				}
				break;
			}

			case OP_JOINED:
			{
				const char *gateway = gateways[walk_random(sizeof(gateways) / sizeof(gateways[0]))];

				link_joined(&walked, gateway, given);
				model_joined(&model, gateway, given);
				if(watch.open == ACT_JOIN)
				{
					watch.open = ACT_NONE;
					watch.in_network = true;
					watch_fresh(&watch);
				}
				break;
			}

			case OP_JOIN_FAILED:
				link_join_failed(&walked, given);
				model_join_failed(&model, given);
				if(watch.open == ACT_JOIN)
				{
					watch.open = ACT_NONE;
					if(watch.list_changed) watch_fresh(&watch);
					else if(walked.phase == LINK_WAITING) watch_failure(&watch);
				}
				break;

			case OP_LOST:
				link_lost(&walked, given);
				model_lost(&model, given);
				if(watch.in_network && watch.open != ACT_LEAVE)
				{
					watch.open = ACT_NONE;
					watch.in_network = false;
					watch_fresh(&watch);
				}
				break;

			case OP_LEFT:
				link_left(&walked, given);
				model_left(&model, given);
				if(watch.open == ACT_LEAVE)
				{
					watch.open = ACT_NONE;
					watch.in_network = false;
					watch_fresh(&watch);
				}
				break;

			case OP_FOUND:
			{
				const char *address = addresses[walk_random(sizeof(addresses) / sizeof(addresses[0]))];

				link_found(&walked, address, given);
				model_found(&model, address, given);
				if(watch.open == ACT_FIND) watch.open = ACT_NONE;
				break;
			}

			case OP_NOT_FOUND:
				link_not_found(&walked, given);
				model_found(&model, NULL, given);
				if(watch.open == ACT_FIND) watch.open = ACT_NONE;
				break;

			case OP_ANSWERING:
			{
				bool answering = walk_random(3) == 0;

				link_answering(&walked, answering, given);
				model_answering(&model, answering, given);
				break;
			}

			case OP_PROFILES:
				which = walk_random(sizeof(lists) / sizeof(lists[0]));
				link_profiles(&walked, lists[which].list, lists[which].count, given);
				model_profiles(&model, lists[which].list, lists[which].count, given);
				watch.list = lists[which].list;
				watch.stored = lists[which].count >= 0 && lists[which].count <= NET_PROFILES_MAX ? lists[which].count : 0;
				watch.list_changed = true;
				watch.scan_valid = false;
				watch_fresh(&watch);
				break;

			case OP_AP_REQUEST:
			{
				bool on = walk_random(2) == 0;

				link_ap_request(&walked, on, given);
				model_ap_request(&model, on, given);
				break;
			}

			case OP_AP_CLIENTS:
			{
				int clients = client_counts[walk_random(sizeof(client_counts) / sizeof(client_counts[0]))];

				link_ap_clients(&walked, clients, given);
				model_ap_clients(&model, clients, given);
				break;
			}

			default:
				which = walk_random(sizeof(lists) / sizeof(lists[0]));
				safe = walk_random(4) == 0;
				link_init(&walked, lists[which].list, lists[which].count, safe, given);
				model_init(&model, lists[which].list, lists[which].count, safe, given);
				watch_init(&watch, lists[which].list, lists[which].count, safe, given);
				now = given;
				break;
		}

		result->calls++;
		result->phases[model_phase(&model)]++;
		if(op >= OP_SCANNED && op <= OP_NOT_FOUND && op != OP_LOST && open_before == watch.open && phase_before == walked.phase) result->ignored++;
		if((link_host(&walked)[0] != '\0') != link_up(&walked)) result->host_not_up++;

		same = action == expected && walked.phase == model_phase(&model) && link_profile(&walked) == model.target &&
		       strcmp(link_host(&walked), model.adapter) == 0 && link_up(&walked) == (model_phase(&model) == LINK_UP) &&
		       link_ap_on(&walked) == model.ap_ordered;
		if(!same)
		{
			result->different++;
			if(shown++ < 3)
			{
				printf("  walk %lu, step %d, after %s at %llu ms: module does %d, phase %d, profile %d, host '%s', ap %d - model does %d, phase %d, profile %d, host '%s', ap %d\n",
				       (unsigned long)seed, step, op_names[op], (unsigned long long)given, (int)action, (int)walked.phase, link_profile(&walked),
				       link_host(&walked), (int)link_ap_on(&walked), (int)expected, (int)model_phase(&model), model.target, model.adapter,
				       (int)model.ap_ordered);
			}
			// From here on they would differ in everything: this walk ends
			return;
		}

		if(step % 40 == 39)
		{
			result->alive_checks++;
			if(!comes_up(walked, watch.open, watch.list, watch.stored, watch.latest))
			{
				result->stuck++;
				if(shown++ < 3) printf("  walk %lu, step %d: an honest driver does not get the link up from phase %d\n", (unsigned long)seed, step, (int)walked.phase);
			}
		}
	}
}

static void test_walk(void)
{
	walk_result_t result;
	int ends[2];
	int status = 0;
	bool complete = false;
	pid_t child;
	char what[160];

	memset(&result, 0, sizeof(result));
	walk_lists();
	fflush(stdout);
	if(pipe(ends) != 0)
	{
		check(false, "a pipe for the result of the walk");
		return;
	}
	// In a child process: a crash or a hang of the module is then a failed check here, not the end of the test
	child = fork();
	if(child == 0)
	{
		uint32_t seed;

		close(ends[0]);
		alarm(120);
		for(seed = 1; seed <= WALKS; seed++) walk(seed, &result);
		fflush(stdout);
		_exit(write(ends[1], &result, sizeof(result)) == (ssize_t)sizeof(result) ? 0 : 3);
	}
	close(ends[1]);
	if(child > 0)
	{
		complete = read(ends[0], &result, sizeof(result)) == (ssize_t)sizeof(result);
		if(waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) complete = false;
	}
	close(ends[0]);

	printf("  walk: %ld calls in %ld walks; nothing %ld, scan %ld, join %ld, leave %ld, find %ld (%ld while up), ap on %ld, ap off %ld; "
	       "idle %ld, waiting %ld, scanning %ld, joining %ld, leaving %ld, joined %ld, up %ld; %ld joins overdue, %ld waits checked, "
	       "%ld reports ignored, %ld steps back, %ld checks with an honest driver\n",
	       result.calls, result.walks, result.handed[LINK_DO_NOTHING], result.handed[LINK_DO_SCAN], result.handed[LINK_DO_JOIN],
	       result.handed[LINK_DO_LEAVE], result.handed[LINK_DO_FIND], result.queries_up, result.handed[LINK_DO_AP_ON], result.handed[LINK_DO_AP_OFF],
	       result.phases[LINK_IDLE], result.phases[LINK_WAITING], result.phases[LINK_SCANNING], result.phases[LINK_JOINING],
	       result.phases[LINK_LEAVING], result.phases[LINK_JOINED], result.phases[LINK_UP], result.timeouts, result.waits_checked,
	       result.ignored, result.steps_back, result.alive_checks);

	snprintf(what, sizeof(what), "%d random walks of %d calls each: the module neither crashes nor hangs", WALKS, WALK_STEPS);
	check(complete, what);
	check(complete && result.walks == WALKS && result.different == 0, "in the walks module and model agree after every call: action, phase, profile, host, access point");
	check(complete && result.handed[LINK_DO_SCAN] > 20000 && result.handed[LINK_DO_JOIN] > 10000 && result.handed[LINK_DO_LEAVE] > 1000 &&
	      result.handed[LINK_DO_FIND] > 5000 && result.queries_up > 300 && result.handed[LINK_DO_AP_ON] > 1000 && result.handed[LINK_DO_AP_OFF] > 500 &&
	      result.phases[LINK_IDLE] > 10000 && result.phases[LINK_WAITING] > 10000 && result.phases[LINK_SCANNING] > 10000 &&
	      result.phases[LINK_JOINING] > 10000 && result.phases[LINK_LEAVING] > 1000 && result.phases[LINK_JOINED] > 10000 &&
	      result.phases[LINK_UP] > 10000 && result.timeouts > 500 && result.waits_checked > 5000 && result.ignored > 10000 &&
	      result.steps_back > 10000 && result.alive_checks > 10000,
	      "the walks reach every action and every phase, overdue joins, queries for a silent adapter, ignored reports and steps back of the time in numbers");
	check(complete && result.two_actions == 0, "in the walks no action that needs a report is handed out while another one waits for its report");
	check(complete && result.join_unseen == 0, "in the walks a join is only ordered for a profile of the list in use that the last scan saw");
	check(complete && result.in_network == 0, "in the walks no scan and no join while in a network, no leave and no query outside one");
	check(complete && result.no_profile == 0, "in the walks never a scan or a join without a stored profile");
	check(complete && result.ap_off == 0, "in the walks the access point is never ordered off in safe mode or without a stored profile");
	check(complete && result.host_not_up == 0, "in the walks link_host is empty exactly when link_up is false");
	check(complete && result.scan_early == 0, "in the walks a scan never follows a scan without success or a failed join sooner than the wait of the header");
	check(complete && result.stuck == 0, "in the walks an honest driver gets the link up from every state while a nameable profile is stored, and to rest without one");
}

int main(void)
{
	test_walk();
	test_constants();
	test_start();
	test_scan();
	test_waits();
	test_waits_start_anew();
	test_join();
	test_join_timeout();
	test_host_given();
	test_host_gateway();
	test_host_query();
	test_host_lengths();
	test_silent_adapter();
	test_lost();
	test_profile();
	test_under_way();
	test_profiles_outside_a_network();
	test_profiles_while_joining();
	test_profiles_in_a_network();
	test_ap_request();
	test_ap_idle();
	test_ap_kept_open();
	test_reports_that_do_not_match();
	test_clock();
	test_clock_limits();
	return test_end();
}
