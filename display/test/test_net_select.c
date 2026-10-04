/*
 * Host test for display/components/core/net_select.c. Run "make test_net_select && ./test_net_select" in
 * display/test. redproof.py removes or weakens every rule once (mutations/net_select.py) and expects this
 * test to fail.
 */
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include "test.h"
#include "net_select.h"

#define GUARD   0x5A

// The list under test with one guard entry in front of it and three behind it
#define BEHIND  3
#define ROOM    (1 + NET_PROFILES_MAX + BEHIND)

static net_profile_t room[ROOM];
static net_profile_t *const list = &room[1];

static uint32_t random_state = 20261003;

static uint32_t random_next(void)
{
	random_state = random_state * 1664525u + 1013904223u;
	return random_state >> 8;
}

static void clear_room(void)
{
	memset(room, GUARD, sizeof(room));
}

static bool guards_intact(void)
{
	const unsigned char *front = (const unsigned char *)&room[0];
	const unsigned char *back = (const unsigned char *)&room[NET_PROFILES_MAX + 1];
	size_t i;

	for(i = 0; i < sizeof(net_profile_t); i++)
	{
		if(front[i] != GUARD) return false;
	}
	for(i = 0; i < BEHIND * sizeof(net_profile_t); i++)
	{
		if(back[i] != GUARD) return false;
	}
	return true;
}

// An entry as the header describes it: the three texts, every other byte zero
static void expected_profile(net_profile_t *profile, const char *ssid, const char *password, const char *host)
{
	memset(profile, 0, sizeof(*profile));
	strcpy(profile->ssid, ssid);
	strcpy(profile->password, password);
	strcpy(profile->host, host);
}

static bool entry_is(int index, const char *ssid, const char *password, const char *host)
{
	net_profile_t expected;

	expected_profile(&expected, ssid, password, host);
	return memcmp(&list[index], &expected, sizeof(expected)) == 0;
}

static bool entry_is_zero(int index)
{
	net_profile_t zero;

	memset(&zero, 0, sizeof(zero));
	return memcmp(&list[index], &zero, sizeof(zero)) == 0;
}

// A text of `length` bytes, the same byte throughout
static const char *text_of(char byte, size_t length)
{
	static char texts[4][80];
	static int next = 0;
	char *text = texts[next++ % 4];

	memset(text, byte, length);
	text[length] = '\0';
	return text;
}

static void test_choose(void)
{
	static const net_profile_t profiles[5] = {
		{"Home", "homepassword", ""}, {"WiCAN_a1b2c3d4e5f6", "@meatpi#", ""}, {"Garage", "", "192.168.1.50"}, {"Phone", "phonepass", ""},
		{"Fifth", "", ""},
	};
	static const char both[2][NET_SSID_SIZE] = {"Garage", "WiCAN_a1b2c3d4e5f6"};
	static const char swapped[2][NET_SSID_SIZE] = {"WiCAN_a1b2c3d4e5f6", "Garage"};
	static const char all[5][NET_SSID_SIZE] = {"Other", "Phone", "Garage", "WiCAN_a1b2c3d4e5f6", "Home"};
	static const char strangers[3][NET_SSID_SIZE] = {"Other", "Neighbour", "FRITZ!Box 7590"};
	static const char last[3][NET_SSID_SIZE] = {"Other", "Neighbour", "Phone"};
	static const char fifth[3][NET_SSID_SIZE] = {"Other", "Fifth", ""};

	check(net_choose(profiles, 4, last, 2) == -1, "a network behind seen_count is not looked at");
	check(net_choose(profiles, 4, fifth, 2) == -1, "a profile behind profile_count is not looked at");
	check(net_choose(profiles, 3, last, 3) == -1, "the last of four profiles is behind a profile_count of three");
	check(net_choose(profiles, 4, both, 2) == 1, "of two profiles in range the one earlier in the list is chosen");
	check(net_choose(profiles, 4, swapped, 2) == 1, "the order of the scan does not matter");
	check(net_choose(profiles, 4, all, 5) == 0, "all profiles in range: the first of the list");
	check(net_choose(profiles, 4, &both[0], 1) == 2, "only the third profile in range");
	check(net_choose(profiles, 4, last, 3) == 3, "only the last of four profiles in range, as the last network of the scan");
	check(net_choose(profiles, 4, strangers, 3) == -1, "no profile in range: -1");
	check(net_choose(profiles, 4, all, 0) == -1, "an empty scan: -1");
	check(net_choose(profiles, 0, all, 5) == -1, "an empty list: -1");
	check(net_choose(profiles, -1, all, 5) == -1 && net_choose(profiles, 4, all, -1) == -1, "negative counts: -1");
}

static void test_choose_bytes(void)
{
	static const net_profile_t home[1] = {{"Home", "homepassword", ""}};
	static const net_profile_t cafe[1] = {{"Caf\303\251 Nord", "", ""}};
	static const net_profile_t high[1] = {{"Net\351", "", ""}};
	static const net_profile_t hidden[2] = {{"", "", ""}, {"Home", "homepassword", ""}};
	static const char same[1][NET_SSID_SIZE] = {"Home"};
	static const char similar[6][NET_SSID_SIZE] = {"home", "HOME", "Hom", "Home2", "Home ", " Home"};
	static const char umlaut[1][NET_SSID_SIZE] = {"Caf\303\251 Nord"};
	static const char plain[3][NET_SSID_SIZE] = {"Cafe Nord", "Caf\303\250 Nord", "Caf\303"};
	static const char low[2][NET_SSID_SIZE] = {"Neti", "Net"};
	static const char with_hidden[2][NET_SSID_SIZE] = {"", "Home"};
	static const char only_hidden[1][NET_SSID_SIZE] = {""};
	static const net_profile_t shortest[1] = {{"x", "", ""}};
	static const char one_byte[2][NET_SSID_SIZE] = {"y", "x"};
	net_profile_t longest[1];
	char seen[2][NET_SSID_SIZE];

	check(net_choose(home, 1, same, 1) == 0, "the same SSID matches");
	check(net_choose(home, 1, similar, 6) == -1, "another case, a shorter, a longer SSID do not match");
	check(net_choose(shortest, 1, one_byte, 2) == 0 && net_choose(shortest, 1, one_byte, 1) == -1, "an SSID of one byte matches itself and no other");
	check(net_choose(cafe, 1, umlaut, 1) == 0, "an SSID with a UTF-8 character matches itself");
	check(net_choose(cafe, 1, plain, 3) == -1, "an SSID with another or half a UTF-8 character does not match");
	check(net_choose(high, 1, low, 2) == -1, "a byte above 127 is not the byte without its highest bit");

	check(net_choose(hidden, 2, with_hidden, 2) == 1, "a profile with an empty SSID is skipped, also when a hidden network was seen");
	check(net_choose(hidden, 1, only_hidden, 1) == -1, "a profile with an empty SSID never matches");

	memset(longest, 0, sizeof(longest));
	memset(longest[0].ssid, 'A', NET_SSID_SIZE - 1);
	memset(seen, 0, sizeof(seen));
	memset(seen[0], 'A', NET_SSID_SIZE - 1);
	check(net_choose(longest, 1, seen, 1) == 0, "an SSID of 32 bytes matches");
	seen[0][NET_SSID_SIZE - 2] = 'B';
	check(net_choose(longest, 1, seen, 1) == -1, "an SSID of 32 bytes that differs in the last byte does not match");
	seen[0][NET_SSID_SIZE - 2] = '\0';
	check(net_choose(longest, 1, seen, 1) == -1, "an SSID of 31 bytes does not match one of 32 that begins with it");

	// Both without a terminating zero, and what follows them in the memory is equal as well
	memset(longest, 0, sizeof(longest));
	memset(longest[0].ssid, 'A', NET_SSID_SIZE);
	strcpy(longest[0].password, "x");
	memset(seen, 0, sizeof(seen));
	memset(seen[0], 'A', NET_SSID_SIZE);
	strcpy(seen[1], "x");
	check(net_choose(longest, 1, seen, 1) == -1, "an SSID without a terminating zero in its 33 bytes equals none");
	longest[0].password[0] = '\0';
	seen[1][0] = '\0';
	check(net_choose(longest, 1, seen, 1) == -1, "an SSID without a terminating zero equals none, also when a zero follows directly behind it");

	// 32 bytes and a zero in the list, the same 32 bytes and one more in the scan
	memset(longest, 0, sizeof(longest));
	memset(longest[0].ssid, 'A', NET_SSID_SIZE - 1);
	memset(seen, 0, sizeof(seen));
	memset(seen[0], 'A', NET_SSID_SIZE);
	check(net_choose(longest, 1, seen, 1) == -1, "an SSID of 32 bytes does not match 33 bytes without a terminating zero that begin with it");
}

// Every byte of an SSID counts, wherever it stands and however long the SSID is
static void test_ssid_positions(void)
{
	static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEF";
	char seen[1][NET_SSID_SIZE];
	int wrong_same = 0, wrong_differs = 0, wrong_forgotten = 0, wrong_longer = 0, wrong_shorter = 0, wrong_pairs = 0;
	int stored, other;
	size_t length, at;

	for(length = 1; length <= NET_SSID_SIZE - 1; length++)
	{
		// In the list with its guard entries: a function that looks behind profile_count finds no SSID there
		clear_room();
		memset(&list[0], 0, sizeof(list[0]));
		memcpy(list[0].ssid, letters, length);
		memset(seen, 0, sizeof(seen));
		memcpy(seen[0], letters, length);
		if(net_choose(list, 1, seen, 1) != 0) wrong_same++;

		for(at = 0; at < length; at++)
		{
			// a space, as a form may leave one in a field, and the letter in the other case
			seen[0][at] = ' ';
			if(net_choose(list, 1, seen, 1) != -1) wrong_differs++;
			if(net_forget(list, 1, seen[0]) != 1 || list[0].ssid[0] != 'a') wrong_forgotten++;
			seen[0][at] = (char)(letters[at] ^ 0x20);
			if(net_choose(list, 1, seen, 1) != -1) wrong_differs++;
			seen[0][at] = letters[at];
		}

		if(length == NET_SSID_SIZE - 1) continue;
		// The same SSID with one byte more behind it, in the scan and in the list
		seen[0][length] = ' ';
		if(net_choose(list, 1, seen, 1) != -1) wrong_longer++;
		seen[0][length] = letters[length];
		if(net_choose(list, 1, seen, 1) != -1) wrong_longer++;
		seen[0][length] = '\0';
		list[0].ssid[length] = ' ';
		if(net_choose(list, 1, seen, 1) != -1) wrong_shorter++;
		if(net_forget(list, 1, seen[0]) != 1 || list[0].ssid[0] != 'a') wrong_forgotten++;
		list[0].ssid[length] = letters[length];
		if(net_choose(list, 1, seen, 1) != -1) wrong_shorter++;
	}

	// No byte equals another one: "Net", one byte and "work" in the list, the same with every other byte in the scan
	clear_room();
	memset(&list[0], 0, sizeof(list[0]));
	memset(seen, 0, sizeof(seen));
	strcpy(list[0].ssid, "Net?work");
	strcpy(seen[0], "Net?work");
	for(stored = 1; stored <= 255; stored++)
	{
		for(other = 1; other <= 255; other++)
		{
			list[0].ssid[3] = (char)stored;
			seen[0][3] = (char)other;
			if((net_choose(list, 1, seen, 1) == 0) != (stored == other)) wrong_pairs++;
		}
	}
	check(wrong_same == 0, "an SSID of every length from 1 to 32 bytes matches itself");
	check(wrong_pairs == 0, "of all 65025 pairs of bytes in the same place of two SSIDs only the 255 equal ones match");
	check(wrong_differs == 0, "SSIDs of every length that differ in one byte, at every position, do not match");
	check(wrong_longer == 0, "an SSID of every length does not match a network with one byte more, a space or a letter, behind the same bytes");
	check(wrong_shorter == 0, "an SSID with one byte more, a space or a letter, does not match a network that is its beginning");
	check(wrong_forgotten == 0, "a profile is not forgotten by an SSID that differs in one byte or lacks its last one");
}

// A scan may list any number of networks
static void test_choose_long_scan(void)
{
	static const net_profile_t profiles[4] = {
		{"Home", "homepassword", ""}, {"WiCAN_a1b2c3d4e5f6", "@meatpi#", ""}, {"Garage", "", "192.168.1.50"}, {"Phone", "phonepass", ""},
	};
	static char seen[64][NET_SSID_SIZE];
	int at, i, wrong = 0;

	for(at = 0; at < 64; at++)
	{
		for(i = 0; i < 64; i++) snprintf(seen[i], NET_SSID_SIZE, "Network %d", i);
		strcpy(seen[at], "Garage");
		if(net_choose(profiles, 4, seen, 64) != 2) wrong++;
		if(net_choose(profiles, 4, seen, at) != -1) wrong++;
	}
	check(wrong == 0, "a scan of 64 networks: the profile is chosen wherever its network stands in it, and not if it stands behind seen_count");
	strcpy(seen[63], "Phone");
	strcpy(seen[40], "WiCAN_a1b2c3d4e5f6");
	check(net_choose(profiles, 4, seen, 64) == 1, "a scan of 64 networks with two profiles far behind in it: the one earlier in the list");
}

// Every list of up to 4 profiles from 5 names and the empty SSID, against every set of the names in a scan.
// Expected from the numbers alone: the first position whose name is in the set.
static void test_choose_all(void)
{
	static const char *const names[6] = {"", "Home", "WiCAN_1", "Garage", "home", "Home2"};
	// What else stands in a profile does not matter
	static const char *const passwords[3] = {"", "@meatpi#", "another password"};
	static const char *const hosts[2] = {"", "192.168.1.50"};
	net_profile_t profiles[4];
	char seen[6][NET_SSID_SIZE];
	int length, code, codes, set, i, wrong = 0, count = 0;

	for(length = 0; length <= 4; length++)
	{
		codes = 1;
		for(i = 0; i < length; i++) codes *= 6;
		for(code = 0; code < codes; code++)
		{
			int picks[4] = {0, 0, 0, 0}, rest = code;

			memset(profiles, 0, sizeof(profiles));
			for(i = 0; i < length; i++)
			{
				picks[i] = rest % 6;
				rest /= 6;
				strcpy(profiles[i].ssid, names[picks[i]]);
				strcpy(profiles[i].password, passwords[(code + i) % 3]);
				strcpy(profiles[i].host, hosts[(code / 3 + i) % 2]);
			}
			for(set = 0; set < 32; set++)
			{
				int expected = -1, seen_count = 0;

				// A hidden network is always there
				memset(seen, 0, sizeof(seen));
				seen_count++;
				for(i = 1; i <= 5; i++)
				{
					if(set & (1 << (i - 1))) strcpy(seen[seen_count++], names[i]);
				}
				for(i = length - 1; i >= 0; i--)
				{
					if(picks[i] != 0 && (set & (1 << (picks[i] - 1)))) expected = i;
				}
				if(net_choose(profiles, length, seen, seen_count) != expected) wrong++;
				count++;
			}
		}
	}
	check(count == 49760, "all 1555 lists against all 32 scans are tried");
	check(wrong == 0, "in every list and scan the first profile in range is chosen");
}

static void test_host_rule(void)
{
	static const net_profile_t router = {"Home", "homepassword", "192.168.1.50"};
	static const net_profile_t router_name = {"Home", "homepassword", "wican_a1b2c3d4e5f6.local"};
	static const net_profile_t router_search = {"Home", "homepassword", ""};
	static const net_profile_t ap = {"WiCAN_a1b2c3d4e5f6", "@meatpi#", ""};
	static const net_profile_t ap_given = {"WiCAN_a1b2c3d4e5f6", "@meatpi#", "192.168.80.1"};
	static const net_profile_t ap_without_id = {"WiCAN_", "", ""};
	static const net_profile_t open = {"Guest", "", ""};
	static const net_profile_t ap_open = {"WiCAN_a1b2c3d4e5f6", "", ""};
	static const net_profile_t short_host = {"Home", "homepassword", "w"};
	static const net_profile_t plain_name = {"Home", "homepassword", "wican"};
	static const net_profile_t ap_own_password = {"WiCAN_a1b2c3d4e5f6", "my own password", ""};
	static const net_profile_t ap_own_password_given = {"WiCAN_a1b2c3d4e5f6", "my own password", "wican"};
	static const net_profile_t ap_longest = {"WiCAN_abcdefghijklmnopqrstuvwxyz", "", ""};
	static const net_profile_t mdns_name = {"Home", "homepassword", "wican.local"};
	static const net_profile_t longest_host = {"Home", "", "a-host-name-of-39-bytes.fritz.box.local"};
	bool every_byte = true;
	int byte;

	check(net_host_rule(&router) == NET_HOST_GIVEN && net_host_rule(&router_name) == NET_HOST_GIVEN, "a host stored with the profile is used");
	check(net_host_rule(&ap_given) == NET_HOST_GIVEN, "a host stored with the profile goes before the gateway of a WiCAN access point");
	check(net_host_rule(&short_host) == NET_HOST_GIVEN, "a host of one byte is a stored host");
	check(net_host_rule(&plain_name) == NET_HOST_GIVEN && net_host_rule(&ap_own_password_given) == NET_HOST_GIVEN, "a name without a dot is a stored host");
	check(net_host_rule(&mdns_name) == NET_HOST_GIVEN && net_host_rule(&longest_host) == NET_HOST_GIVEN, "wican.local and a host of 39 bytes are stored hosts");
	for(byte = 1; byte <= 255; byte++)
	{
		net_profile_t profile = {"Home", "homepassword", ""};

		profile.host[0] = (char)byte;
		if(net_host_rule(&profile) != NET_HOST_GIVEN) every_byte = false;
		strcpy(profile.ssid, "WiCAN_a1b2");
		if(net_host_rule(&profile) != NET_HOST_GIVEN) every_byte = false;
	}
	check(every_byte, "every byte from 1 to 255 as a host of one byte is a stored host, in every network");
	check(net_host_rule(&ap_own_password) == NET_HOST_GATEWAY, "in an access point of a WiCAN with another password than the factory one it is the gateway");
	check(net_host_rule(&ap_longest) == NET_HOST_GATEWAY, "in an access point of a WiCAN with an SSID of 32 bytes it is the gateway");
	check(net_host_rule(&ap) == NET_HOST_GATEWAY, "in the access point of a WiCAN without a stored host it is the gateway");
	check(net_host_rule(&ap_open) == NET_HOST_GATEWAY, "in an access point of a WiCAN without a password it is the gateway as well");
	check(net_host_rule(&router_search) == NET_HOST_MDNS && net_host_rule(&open) == NET_HOST_MDNS, "in another network without a stored host it is searched by mDNS");
	check(net_host_rule(&ap_without_id) == NET_HOST_MDNS, "WiCAN_ without a character behind it is no access point of a WiCAN");
}

// Every combination of SSID, password and host. Expected from the places in the tables alone.
static void test_host_rule_all(void)
{
	// The first two are access points of a WiCAN
	static const char *const ssids[11] = {
		"WiCAN_a1b2c3d4e5f6", "WiCAN_x", "WiCAN_", "WiCAN", "wican_a1b2", "WICAN_a1b2", "WiCAN-a1b2", " WiCAN_a1b2", "Home", "x", "",
	};
	static const char *const passwords[3] = {"", "@meatpi#", "another password"};
	// The first is no host
	static const char *const hosts[4] = {"", "x", "192.168.80.1", "wican.local"};
	int wrong = 0, s, p, h;

	for(s = 0; s < 11; s++)
	{
		for(p = 0; p < 3; p++)
		{
			for(h = 0; h < 4; h++)
			{
				net_host_rule_t expected = h > 0 ? NET_HOST_GIVEN : s < 2 ? NET_HOST_GATEWAY : NET_HOST_MDNS;
				net_profile_t profile;

				expected_profile(&profile, ssids[s], passwords[p], hosts[h]);
				if(net_host_rule(&profile) != expected)
				{
					if(wrong++ < 5) printf("  \"%s\" with password \"%s\" and host \"%s\"\n", ssids[s], passwords[p], hosts[h]);
				}
			}
		}
	}
	check(wrong == 0, "11 SSIDs with 3 passwords and 4 hosts: the stored host if there is one, else the gateway in an access point of a WiCAN, else mDNS");
}

static void test_wican_ap(void)
{
	static const char *const others[] = {
		"W", "WiCAN", "wican_a1b2", "WICAN_a1b2", "wiCAN_a", "Wican_a", "WiCaN_a", "WiCAn_a",
		"xWiCAN_a1b2", " WiCAN_a1b2", "_WiCAN_a", "Home", "WiCA_N_a", "ViCAN_a",
	};
	char ssid[80] = "WiCAN_";
	bool none = true, every_byte = true, every_length = true;
	size_t i, length;
	int byte;

	check(net_is_wican_ap("WiCAN_a1b2c3d4e5f6"), "WiCAN_ and a device id is the access point of a WiCAN");
	check(net_is_wican_ap("WiCAN_x") && net_is_wican_ap("WiCAN_ ") && net_is_wican_ap("WiCAN__"), "one character behind WiCAN_ is enough");
	check(!net_is_wican_ap("WiCAN_"), "WiCAN_ alone is no access point of a WiCAN");
	check(!net_is_wican_ap(""), "an empty SSID is no access point of a WiCAN");
	check(!net_is_wican_ap("WiCAN-a1b2") && !net_is_wican_ap("WiCANa1b2"), "WiCAN without the underscore behind it is no access point of a WiCAN");
	for(i = 0; i < sizeof(others) / sizeof(others[0]); i++)
	{
		if(net_is_wican_ap(others[i]))
		{
			printf("  \"%s\" counts as the access point of a WiCAN\n", others[i]);
			none = false;
		}
	}
	check(none, "another case, another sign, a shorter beginning or something in front of it is no access point of a WiCAN");

	for(byte = 1; byte <= 255; byte++)
	{
		ssid[6] = (char)byte;
		ssid[7] = '\0';
		if(!net_is_wican_ap(ssid)) every_byte = false;
	}
	check(every_byte, "every byte from 1 to 255 behind WiCAN_ makes an access point of a WiCAN");

	// From one character to more than an SSID can have
	for(length = 1; length <= 70; length++)
	{
		memset(&ssid[6], 'f', length);
		ssid[6 + length] = '\0';
		if(!net_is_wican_ap(ssid)) every_length = false;
	}
	check(every_length, "1 to 70 characters behind WiCAN_ make an access point of a WiCAN");
}

static void test_factory_password(void)
{
	bool none = true;
	int at, byte, count = 0;

	check(net_is_factory_password("@meatpi#"), "@meatpi# is the factory password");
	check(!net_is_factory_password(""), "no password is not the factory password");
	check(!net_is_factory_password("@meatpi") && !net_is_factory_password("@") && !net_is_factory_password("meatpi#"), "a part of the factory password is not it");
	check(!net_is_factory_password("@meatpi##") && !net_is_factory_password("@meatpi#1") && !net_is_factory_password("x@meatpi#"), "the factory password with more around it is not it");
	check(!net_is_factory_password("@MEATPI#") && !net_is_factory_password("@Meatpi#") && !net_is_factory_password("@meatpi$"), "another case or sign is not the factory password");

	// One byte of it changed to every other value, and every byte behind it
	for(at = 0; at <= 8; at++)
	{
		for(byte = 1; byte <= 255; byte++)
		{
			char password[10] = "@meatpi#";

			if(at < 8 && (char)byte == password[at]) continue;
			password[at] = (char)byte;
			if(net_is_factory_password(password)) none = false;
			count++;
		}
	}
	check(count == 8 * 254 + 255, "every other value of each byte of the factory password and every byte behind it are tried");
	check(none, "the factory password with one byte changed or one byte more is not it");
}

static void test_store(void)
{
	check(NET_PROFILES_MAX == 4, "the list has room for 4 profiles");
	check(NET_SSID_SIZE == 33 && NET_PASSWORD_SIZE == 65 && NET_HOST_SIZE == 40, "an SSID has room for 32 bytes, a password for 64, a host for 39");

	clear_room();
	check(net_store(list, 0, "Home", "homepassword", "192.168.1.50") == 1, "a profile stored into an empty list: one profile");
	check(entry_is(0, "Home", "homepassword", "192.168.1.50"), "the stored profile has the SSID, the password and the host, zero behind them");
	check(net_store(list, 1, "WiCAN_a1b2", "@meatpi#", "") == 2 && entry_is(0, "WiCAN_a1b2", "@meatpi#", "") && entry_is(1, "Home", "homepassword", "192.168.1.50"),
	      "a new profile is put first, the older one moves back");
	check(net_store(list, 2, "Garage", "", "wican.local") == 3 && entry_is(0, "Garage", "", "wican.local") && entry_is(1, "WiCAN_a1b2", "@meatpi#", "") &&
	      entry_is(2, "Home", "homepassword", "192.168.1.50"), "a third profile is put first, both others move back in their order");
	check(net_store(list, 3, "Phone", "phonepass", "") == 4 && entry_is(0, "Phone", "phonepass", "") && entry_is(1, "Garage", "", "wican.local") &&
	      entry_is(2, "WiCAN_a1b2", "@meatpi#", "") && entry_is(3, "Home", "homepassword", "192.168.1.50"), "a fourth profile fills the list");
	check(guards_intact(), "nothing is written outside a list that was filled");

	check(net_store(list, 4, "Office", "officepass", "10.0.0.7") == 4, "a new profile for a full list: still four profiles");
	check(entry_is(0, "Office", "officepass", "10.0.0.7") && entry_is(1, "Phone", "phonepass", "") && entry_is(2, "Garage", "", "wican.local") &&
	      entry_is(3, "WiCAN_a1b2", "@meatpi#", ""), "the new profile is first and the last one fell out");
	check(guards_intact(), "nothing is written behind a full list");
}

static void test_store_replace(void)
{
	clear_room();
	net_store(list, 0, "Home", "a long password of the home network", "wican_a1b2c3d4e5f6.local");
	net_store(list, 1, "WiCAN_a1b2", "@meatpi#", "");
	net_store(list, 2, "Garage", "garagepassword", "192.168.1.50");
	net_store(list, 3, "Phone", "phonepass", "");

	check(net_store(list, 4, "Garage", "newgarage", "10.0.0.7") == 4, "a profile with a known SSID in a full list: still four profiles");
	check(entry_is(0, "Phone", "phonepass", "") && entry_is(1, "Garage", "newgarage", "10.0.0.7") && entry_is(2, "WiCAN_a1b2", "@meatpi#", "") &&
	      entry_is(3, "Home", "a long password of the home network", "wican_a1b2c3d4e5f6.local"), "it is replaced in place, the others stay where they are");
	check(net_store(list, 4, "Phone", "", "") == 4 && entry_is(0, "Phone", "", "") && entry_is(1, "Garage", "newgarage", "10.0.0.7"),
	      "the first profile is replaced in place");
	check(net_store(list, 4, "Home", "short pw", "") == 4 && entry_is(3, "Home", "short pw", "") && entry_is(2, "WiCAN_a1b2", "@meatpi#", ""),
	      "the last profile is replaced in place, nothing of its longer password and host stays behind");
	check(net_store(list, 2, "Home", "homepassword", "") == 3 && entry_is(0, "Home", "homepassword", "") && entry_is(1, "Phone", "", "") &&
	      entry_is(2, "Garage", "newgarage", "10.0.0.7") && entry_is(3, "Home", "short pw", ""), "an SSID behind profile_count is not known: the profile is new");
	check(net_store(list, 3, "garage", "lowercase", "") == 4 && entry_is(0, "garage", "lowercase", "") && entry_is(3, "Garage", "newgarage", "10.0.0.7"),
	      "an SSID in another case is a new profile");
	check(net_store(list, 4, "Garag", "shorter1", "") == 4 && entry_is(0, "Garag", "shorter1", "") && entry_is(1, "garage", "lowercase", ""),
	      "an SSID that is the beginning of a known one is a new profile");
	check(guards_intact(), "nothing is written outside the list when profiles are replaced");

	// A list as it may come from the flash
	clear_room();
	memset(list, 0, 2 * sizeof(list[0]));
	memset(list[0].ssid, 'A', NET_SSID_SIZE);
	strcpy(list[0].password, "x");
	strcpy(list[1].ssid, "Home");
	check(net_store(list, 2, text_of('A', NET_SSID_SIZE - 1), "", "") == 3 && entry_is(0, text_of('A', NET_SSID_SIZE - 1), "", "") &&
	      list[1].ssid[NET_SSID_SIZE - 1] == 'A' && entry_is(2, "Home", "", ""), "an SSID in the list without a terminating zero equals none: the profile is new");
}

static void test_store_own_texts(void)
{
	clear_room();
	net_store(list, 0, "Home", "homepassword", "192.168.1.50");
	net_store(list, 1, "WiCAN_a1b2", "@meatpi#", "");
	net_store(list, 2, "Garage", "garagepassword", "wican.local");

	check(net_store(list, 3, "Phone", list[1].password, list[2].host) == 4 && entry_is(0, "Phone", "@meatpi#", "192.168.1.50") &&
	      entry_is(1, "Garage", "garagepassword", "wican.local") && entry_is(2, "WiCAN_a1b2", "@meatpi#", "") && entry_is(3, "Home", "homepassword", "192.168.1.50"),
	      "a new profile with the password of the second and the host of the third profile, which both move");
	check(net_store(list, 2, list[2].ssid, list[2].password, list[2].host) == 3 && entry_is(0, "WiCAN_a1b2", "@meatpi#", "") &&
	      entry_is(1, "Phone", "@meatpi#", "192.168.1.50") && entry_is(2, "Garage", "garagepassword", "wican.local"),
	      "a new profile with the three texts of an entry behind profile_count, which is overwritten");
	check(net_store(list, 4, list[3].ssid, "new home password", list[3].host) == 4 && entry_is(3, "Home", "new home password", "192.168.1.50"),
	      "a profile replaced with its own SSID and host keeps them");
	check(net_store(list, 4, list[1].ssid, list[1].password, list[2].host) == 4 && entry_is(1, "Phone", "@meatpi#", "wican.local"),
	      "a profile replaced with its own SSID and password and the host of another one");
	check(net_store(list, 4, "Office", list[3].password, list[3].host) == 4 && entry_is(0, "Office", "new home password", "192.168.1.50") &&
	      entry_is(1, "WiCAN_a1b2", "@meatpi#", "") && entry_is(3, "Garage", "garagepassword", "wican.local"),
	      "a new profile with the password and host of the one that falls out");
	check(guards_intact(), "nothing is written outside the list when texts of the list are stored");
}

// Refused whether the list is empty, full or in between, and whether the SSID is in it or not
static bool refused(const char *ssid, const char *password, const char *host)
{
	int count;

	for(count = 0; count <= NET_PROFILES_MAX; count++)
	{
		if(net_store(list, count, ssid, password, host) != -1)
		{
			printf("  not refused in a list of %d\n", count);
			return false;
		}
	}
	return true;
}

static void test_store_refused(void)
{
	net_profile_t before[ROOM];

	clear_room();
	net_store(list, 0, "Home", "homepassword", "192.168.1.50");
	net_store(list, 1, "Garage", "garagepassword", "wican.local");
	net_store(list, 2, "WiCAN_a1b2", "@meatpi#", "");
	net_store(list, 3, "Phone", "phonepass", "");
	memcpy(before, room, sizeof(room));

	check(refused("", "password", ""), "an empty SSID is refused");
	check(refused(text_of('s', NET_SSID_SIZE), "password", ""), "an SSID of 33 bytes is refused");
	check(refused(text_of('s', 70), "password", ""), "an SSID of 70 bytes is refused");
	check(refused("New", "p", ""), "a password of 1 byte is refused");
	check(refused("New", "1234567", ""), "a password of 7 bytes is refused");
	check(refused("New", text_of('p', NET_PASSWORD_SIZE), ""), "a password of 65 bytes is refused");
	check(refused("New", "", text_of('h', NET_HOST_SIZE)), "a host of 40 bytes is refused");
	check(refused("Home", "1234567", "") && refused("Phone", "password", text_of('h', NET_HOST_SIZE)) && refused("Garage", text_of('p', NET_PASSWORD_SIZE), ""),
	      "a replacement with a password or host that is refused is refused");
	check(net_store(list, -1, "New", "password", "") == -1, "a profile_count of -1 is refused");
	check(net_store(list, NET_PROFILES_MAX + 1, "New", "password", "") == -1, "a profile_count of 5 is refused");
	check(memcmp(before, room, sizeof(room)) == 0, "nothing changes when a profile is refused");

	clear_room();
	check(net_store(list, 0, text_of('s', NET_SSID_SIZE - 1), "", "") == 1 && entry_is(0, text_of('s', NET_SSID_SIZE - 1), "", ""), "an SSID of 32 bytes is stored");
	check(net_store(list, 1, "x", "12345678", "") == 2 && entry_is(0, "x", "12345678", ""), "an SSID of 1 byte and a password of 8 bytes are stored");
	check(net_store(list, 2, "Open", "", "") == 3 && entry_is(0, "Open", "", ""), "no password is stored: an open network");
	check(net_store(list, 3, "Long", text_of('p', NET_PASSWORD_SIZE - 1), text_of('h', NET_HOST_SIZE - 1)) == 4 &&
	      entry_is(0, "Long", text_of('p', NET_PASSWORD_SIZE - 1), text_of('h', NET_HOST_SIZE - 1)), "a password of 64 bytes and a host of 39 bytes are stored");
	check(net_store(list, 4, text_of('t', NET_SSID_SIZE - 1), text_of('q', NET_PASSWORD_SIZE - 1), text_of('i', NET_HOST_SIZE - 1)) == 4 &&
	      entry_is(0, text_of('t', NET_SSID_SIZE - 1), text_of('q', NET_PASSWORD_SIZE - 1), text_of('i', NET_HOST_SIZE - 1)) && entry_is(3, "x", "12345678", ""),
	      "a profile with all three texts at their limits is new in a full list");
	check(guards_intact(), "nothing is written outside the list by texts at their limits");
}

// Every length of SSID, password and host up to beyond their limits, for an empty list, a full list and a
// list that has the SSID already. Which of them are refused is what the header says.
static void test_store_lengths(void)
{
	static char ssid[48], password[80], host[56];
	net_profile_t before[ROOM];
	int wrong_refused = 0, wrong_changed = 0, wrong_stored = 0, count = 0, kind;
	size_t ssid_length, password_length, host_length;

	for(kind = 0; kind <= 2; kind++)
	{
		for(ssid_length = 0; ssid_length <= 40; ssid_length++)
		{
			bool known = kind == 2 && ssid_length >= 1 && ssid_length <= 32;
			int profile_count = 0;

			memset(ssid, 's', ssid_length);
			ssid[ssid_length] = '\0';

			// The lists are put together here, not by net_store
			clear_room();
			if(kind >= 1) expected_profile(&list[profile_count++], "Home", "homepassword", "192.168.1.50");
			if(known) expected_profile(&list[profile_count++], ssid, "the old password", "old.host");
			if(kind >= 1) expected_profile(&list[profile_count++], "Phone", "phonepass", "");
			if(kind == 1)
			{
				expected_profile(&list[profile_count++], "Garage", "garagepassword", "wican.local");
				expected_profile(&list[profile_count++], "WiCAN_a1b2", "@meatpi#", "");
			}
			memcpy(before, room, sizeof(room));

			for(password_length = 0; password_length <= 70; password_length++)
			{
				memset(password, 'p', password_length);
				password[password_length] = '\0';
				for(host_length = 0; host_length <= 45; host_length++)
				{
					bool valid = ssid_length >= 1 && ssid_length <= 32 && (password_length == 0 || (password_length >= 8 && password_length <= 64)) &&
					             host_length <= 39;
					bool stored;
					int result;

					memset(host, 'h', host_length);
					host[host_length] = '\0';
					result = net_store(list, profile_count, ssid, password, host);
					count++;

					if((result == -1) != !valid) wrong_refused++;
					if(!valid)
					{
						if(memcmp(before, room, sizeof(room)) != 0) wrong_changed++;
					}
					else
					{
						if(kind == 0) stored = result == 1 && entry_is(0, ssid, password, host);
						else if(kind == 1) stored = result == 4 && entry_is(0, ssid, password, host) && entry_is(1, "Home", "homepassword", "192.168.1.50") &&
						                            entry_is(2, "Phone", "phonepass", "") && entry_is(3, "Garage", "garagepassword", "wican.local");
						else stored = result == 3 && entry_is(0, "Home", "homepassword", "192.168.1.50") && entry_is(1, ssid, password, host) &&
						              entry_is(2, "Phone", "phonepass", "");
						if(!stored || !guards_intact()) wrong_stored++;
					}
					memcpy(room, before, sizeof(room));
				}
			}
		}
	}
	check(count == 3 * 41 * 71 * 46, "every SSID of 0 to 40 bytes with every password of 0 to 70 and every host of 0 to 45 bytes is tried with three kinds of list");
	check(wrong_refused == 0, "of these exactly those are refused that have no SSID or one above 32 bytes, a password of 1 to 7 or above 64 bytes or a host above 39 bytes");
	check(wrong_changed == 0, "nothing changes with any of the refused ones");
	check(wrong_stored == 0, "every other one is stored with its three texts: first in an empty and in a full list, in place where the SSID was known");
}

// `length` bytes that are anything but zero: in one of four cases one of the bytes that mean something to
// a form, a shell or a URL
static void random_text(char *text, size_t length)
{
	static const char special[] = {' ', '.', ':', '_', '\t', '"', '\200', '\377'};
	size_t i;

	for(i = 0; i < length; i++)
	{
		text[i] = random_next() % 4 == 0 ? special[random_next() % sizeof(special)] : (char)(1 + random_next() % 255);
	}
	text[length] = '\0';
}

// A length up to `limit`, more often one of those next to `limit`, the largest that is valid
static size_t random_length(size_t limit)
{
	if(random_next() % 3 == 0) return limit - 1 + random_next() % 3;
	return random_next() % (limit + 6);
}

// SSID, password and host are bytes. Nothing but their lengths decides whether a profile is stored, and
// what was stored is found again byte for byte. The list that is expected is kept next to the real one.
static void test_any_bytes(void)
{
	static const char *const beginnings[6] = {"", "", "", "WiCAN_", "wican_", "WiCAN"};
	static net_profile_t model[NET_PROFILES_MAX];
	net_profile_t before[ROOM];
	char ssid[48], password[80], host[56];
	char seen[2][NET_SSID_SIZE];
	int wrong_refused = 0, wrong_changed = 0, wrong_count = 0, wrong_list = 0, wrong_chosen = 0, wrong_rule = 0, wrong_zeroed = 0;
	int added = 0, dropped = 0, replaced = 0, refused = 0, forgotten = 0, missed = 0, gateways = 0;
	int count = 0, i, k;

	clear_room();
	for(i = 0; i < 60000; i++)
	{
		const char *beginning = beginnings[random_next() % 6];
		size_t ssid_length = random_length(NET_SSID_SIZE - 1), password_length = random_length(NET_PASSWORD_SIZE - 1);
		size_t host_length = random_next() % 3 == 0 ? 0 : random_length(NET_HOST_SIZE - 1);
		uint32_t what = random_next() % 8;
		int known = -1, result;

		if(random_next() % 4 == 0) password_length = random_next() % 10;
		random_text(ssid, ssid_length);
		if(strlen(beginning) <= ssid_length) memcpy(ssid, beginning, strlen(beginning));
		random_text(password, password_length);
		random_text(host, host_length);
		// Every fourth time the SSID of a profile that is in the list
		if(count > 0 && random_next() % 4 == 0) strcpy(ssid, model[random_next() % (uint32_t)count].ssid);
		ssid_length = strlen(ssid);
		for(k = 0; k < count; k++) if(strcmp(model[k].ssid, ssid) == 0) known = k;

		memcpy(before, room, sizeof(room));
		if(what < 5)
		{
			bool valid = ssid_length >= 1 && ssid_length <= 32 && (password_length == 0 || (password_length >= 8 && password_length <= 64)) && host_length <= 39;

			result = net_store(list, count, ssid, password, host);
			if(!valid)
			{
				refused++;
				if(result != -1) wrong_refused++;
				if(memcmp(before, room, sizeof(room)) != 0) wrong_changed++;
				continue;
			}
			if(known >= 0)
			{
				replaced++;
			}
			else
			{
				if(count == NET_PROFILES_MAX) dropped++;
				else count++;
				for(k = count - 1; k > 0; k--) model[k] = model[k - 1];
				known = 0;
				added++;
			}
			expected_profile(&model[known], ssid, password, host);
		}
		else
		{
			result = net_forget(list, count, ssid);
			if(known >= 0)
			{
				for(k = known; k < count - 1; k++) model[k] = model[k + 1];
				count--;
				forgotten++;
				if(!entry_is_zero(count)) wrong_zeroed++;
			}
			else
			{
				missed++;
				if(memcmp(before, room, sizeof(room)) != 0) wrong_changed++;
			}
		}
		if(result != count) wrong_count++;
		if(memcmp(list, model, (size_t)count * sizeof(model[0])) != 0 || !guards_intact()) wrong_list++;
		if(count == 0) continue;

		// One of the profiles in range behind a hidden network, and how the WiCAN is found there
		k = (int)(random_next() % (uint32_t)count);
		memset(seen, 0, sizeof(seen));
		strcpy(seen[1], model[k].ssid);
		if(net_choose(list, count, seen, 2) != k) wrong_chosen++;
		if(model[k].host[0] != '\0')
		{
			if(net_host_rule(&list[k]) != NET_HOST_GIVEN) wrong_rule++;
		}
		else if(strlen(model[k].ssid) > 6 && memcmp(model[k].ssid, "WiCAN_", 6) == 0)
		{
			gateways++;
			if(net_host_rule(&list[k]) != NET_HOST_GATEWAY) wrong_rule++;
		}
		else if(net_host_rule(&list[k]) != NET_HOST_MDNS)
		{
			wrong_rule++;
		}
	}
	printf("  random bytes: %d added, %d of them into a full list, %d replaced, %d refused, %d forgotten, %d not found, %d access points of a WiCAN\n",
	       added, dropped, replaced, refused, forgotten, missed, gateways);
	check(added > 5000 && dropped > 1000 && replaced > 1000 && refused > 5000 && forgotten > 1000 && missed > 1000 && gateways > 300,
	      "the walk with random bytes adds, fills the list, replaces, is refused, forgets and misses in numbers");
	check(wrong_refused == 0, "60000 profiles of random bytes: exactly those with a length the header refuses are refused, whatever their bytes");
	check(wrong_changed == 0, "nothing changes when one of them is refused or an SSID that is not in the list is forgotten");
	check(wrong_count == 0, "storing and forgetting them returns the number of profiles of the list kept next to it");
	check(wrong_list == 0, "after each of them the list is, byte for byte, the list kept next to it: new first, replaced in place, the last out of a full list");
	check(wrong_zeroed == 0, "the entry that becomes free is zeroed whatever stood in it");
	check(wrong_chosen == 0, "each profile of random bytes is chosen when its SSID is in range");
	check(wrong_rule == 0, "for each of them the WiCAN is found by the stored host if there is one, else as the gateway in an access point of a WiCAN, else by mDNS");
}

static void test_forget(void)
{
	net_profile_t before[ROOM];
	char run_on[NET_SSID_SIZE + 2];

	clear_room();
	net_store(list, 0, "Home", "homepassword", "192.168.1.50");
	net_store(list, 1, "WiCAN_a1b2", "@meatpi#", "");
	net_store(list, 2, "Garage", "garagepassword", "wican.local");
	net_store(list, 3, "Phone", "phonepass", "");
	memcpy(before, room, sizeof(room));

	check(net_forget(list, 4, "Office") == 4 && net_forget(list, 4, "") == 4 && net_forget(list, 4, "phone") == 4 && net_forget(list, 4, "Phone1") == 4 &&
	      net_forget(list, 4, "Phon") == 4, "an SSID that is not in the list: the number of profiles stays");
	check(net_forget(list, 3, "Home") == 3, "an SSID behind profile_count is not in the list");
	check(net_forget(list, 0, "Phone") == 0 && net_forget(list, -1, "Phone") == -1, "nothing to forget in an empty list");
	check(memcmp(before, room, sizeof(room)) == 0, "nothing changes when no profile is forgotten");

	check(net_forget(list, 4, "Garage") == 3, "a profile in the middle is forgotten: one profile less");
	check(entry_is(0, "Phone", "phonepass", "") && entry_is(1, "WiCAN_a1b2", "@meatpi#", "") && entry_is(2, "Home", "homepassword", "192.168.1.50"),
	      "the profiles behind the forgotten one move up, the order stays");
	check(entry_is_zero(3), "the entry that became free is zeroed");
	check(net_forget(list, 3, "Phone") == 2 && entry_is(0, "WiCAN_a1b2", "@meatpi#", "") && entry_is(1, "Home", "homepassword", "192.168.1.50") && entry_is_zero(2),
	      "the first profile is forgotten");
	check(net_forget(list, 2, "Home") == 1 && entry_is(0, "WiCAN_a1b2", "@meatpi#", "") && entry_is_zero(1), "the last profile is forgotten");
	check(net_forget(list, 1, "WiCAN_a1b2") == 0 && entry_is_zero(0), "the only profile is forgotten");
	check(guards_intact(), "nothing is written outside the list when profiles are forgotten");

	clear_room();
	net_store(list, 0, "Home", "homepassword", "192.168.1.50");
	net_store(list, 1, "WiCAN_a1b2", "@meatpi#", "");
	net_store(list, 2, "Garage", "garagepassword", "wican.local");
	net_store(list, 3, "Phone", "phonepass", "");
	check(net_forget(list, 2, "Phone") == 1 && entry_is(0, "Garage", "garagepassword", "wican.local") && entry_is_zero(1) &&
	      entry_is(2, "WiCAN_a1b2", "@meatpi#", "") && entry_is(3, "Home", "homepassword", "192.168.1.50"),
	      "entries behind profile_count are not touched when a profile is forgotten");

	clear_room();
	net_store(list, 0, "Home", "homepassword", "192.168.1.50");
	net_store(list, 1, "WiCAN_a1b2", "@meatpi#", "");
	net_store(list, 2, "Garage", "garagepassword", "wican.local");
	net_store(list, 3, "Phone", "phonepass", "");
	check(net_forget(list, 4, list[1].ssid) == 3 && entry_is(0, "Phone", "phonepass", "") && entry_is(1, "WiCAN_a1b2", "@meatpi#", "") &&
	      entry_is(2, "Home", "homepassword", "192.168.1.50") && entry_is_zero(3), "a profile is forgotten by its own SSID text");
	check(net_forget(list, 3, list[2].ssid) == 2 && entry_is(1, "WiCAN_a1b2", "@meatpi#", "") && entry_is_zero(2), "the last profile is forgotten by its own SSID text");
	check(guards_intact(), "nothing is written outside the list when a profile is forgotten by its own text");

	// A list as it may come from the flash
	clear_room();
	memset(list, 0, 2 * sizeof(list[0]));
	memset(list[0].ssid, 'A', NET_SSID_SIZE);
	strcpy(list[0].password, "x");
	strcpy(list[1].ssid, "Home");
	memcpy(before, room, sizeof(room));
	// The text a reader finds who does not stop at the end of the SSID
	memset(run_on, 'A', NET_SSID_SIZE);
	strcpy(&run_on[NET_SSID_SIZE], "x");
	check(net_forget(list, 2, text_of('A', NET_SSID_SIZE - 1)) == 2 && net_forget(list, 2, text_of('A', NET_SSID_SIZE)) == 2 &&
	      net_forget(list, 2, run_on) == 2 && memcmp(before, room, sizeof(room)) == 0,
	      "an SSID in the list without a terminating zero equals none: it is not forgotten");
	list[0].password[0] = '\0';
	check(net_forget(list, 2, text_of('A', NET_SSID_SIZE)) == 2 && list[0].ssid[0] == 'A',
	      "an SSID without a terminating zero is not forgotten, also when a zero follows directly behind it");

	// 32 bytes and a zero in the list, the same 32 bytes and more asked for
	clear_room();
	net_store(list, 0, text_of('A', NET_SSID_SIZE - 1), "", "");
	memcpy(before, room, sizeof(room));
	check(net_forget(list, 1, text_of('A', NET_SSID_SIZE)) == 1 && net_forget(list, 1, text_of('A', 70)) == 1 && memcmp(before, room, sizeof(room)) == 0,
	      "an SSID of 32 bytes is not forgotten by a text of 33 or 70 bytes that begins with it");
	check(net_forget(list, 1, text_of('A', NET_SSID_SIZE - 1)) == 0 && entry_is_zero(0), "an SSID of 32 bytes is forgotten by its 32 bytes");

	// A list filled by hand: behind the texts of its entries stands whatever was in the memory
	clear_room();
	strcpy(list[0].ssid, "Home");
	strcpy(list[0].password, "homepassword");
	strcpy(list[0].host, "");
	strcpy(list[1].ssid, "Garage");
	strcpy(list[1].password, "");
	strcpy(list[1].host, "wican.local");
	check(net_forget(list, 2, "Home") == 1 && strcmp(list[0].ssid, "Garage") == 0 && strcmp(list[0].host, "wican.local") == 0 && entry_is_zero(1),
	      "the entry that became free is zeroed to its last byte, whatever stood behind its texts");

	clear_room();
	memset(list, 0, 3 * sizeof(list[0]));
	strcpy(list[0].ssid, "Home");
	strcpy(list[1].password, "password of no network");
	strcpy(list[2].ssid, "Garage");
	check(net_forget(list, 3, "") == 2 && entry_is(0, "Home", "", "") && entry_is(1, "Garage", "", "") && entry_is_zero(2),
	      "a profile with an empty SSID is the one an empty SSID forgets");
}

// A profile_count above the maximum, as a damaged store may give it. Two of the entries behind the list are
// profiles here, so a function that looks at them finds something.
static void test_no_list(void)
{
	static const char seen[3][NET_SSID_SIZE] = {"Fifth", "Sixth", "Garage"};
	net_profile_t before[ROOM];

	clear_room();
	net_store(list, 0, "Home", "homepassword", "192.168.1.50");
	net_store(list, 1, "WiCAN_a1b2", "@meatpi#", "");
	net_store(list, 2, "Garage", "garagepassword", "wican.local");
	net_store(list, 3, "Phone", "phonepass", "");
	expected_profile(&list[4], "Fifth", "fifthpassword", "");
	expected_profile(&list[5], "Sixth", "sixthpassword", "");
	memcpy(before, room, sizeof(room));

	check(net_choose(list, 4, seen, 3) == 1, "the scene: of four profiles the second is in range");
	check(net_choose(list, 5, seen, 1) == -1, "a profile_count of 5 is no list: the entry behind the four is not chosen");
	check(net_choose(list, 5, seen, 3) == -1, "a profile_count of 5 is no list: none of the first four is chosen either");
	check(net_choose(list, 6, seen, 3) == -1, "a profile_count of 6 is no list for net_choose");

	check(net_forget(list, 5, "Fifth") == 5, "a profile_count of 5 is no list: the entry behind the four is not forgotten, the count comes back");
	check(net_forget(list, 5, "Garage") == 5 && net_forget(list, 5, "Phone") == 5 && net_forget(list, 5, "Nowhere") == 5,
	      "a profile_count of 5 is no list: none of the first four is forgotten either");
	check(net_forget(list, 6, "Sixth") == 6 && net_forget(list, 6, "Home") == 6, "a profile_count of 6 is no list for net_forget");

	check(net_store(list, 5, "Fifth", "another password", "") == -1 && net_store(list, 5, "Garage", "", "") == -1 && net_store(list, 6, "New", "", "") == -1,
	      "a profile_count of 5 or 6 is no list for net_store: nothing is replaced, nothing is added");
	check(memcmp(before, room, sizeof(room)) == 0, "nothing changes when profile_count is no list");

	// Last, because a function that takes these for a list leaves the memory of the test
	check(net_choose(list, INT_MAX, seen, 3) == -1 && net_forget(list, INT_MAX, "Garage") == INT_MAX && net_store(list, INT_MAX, "New", "", "") == -1,
	      "the largest profile_count is no list");
	check(net_choose(list, INT_MIN, seen, 3) == -1 && net_forget(list, INT_MIN, "Garage") == INT_MIN && net_store(list, INT_MIN, "New", "", "") == -1,
	      "the smallest profile_count is no list");
	check(memcmp(before, room, sizeof(room)) == 0, "nothing changes with the largest and the smallest profile_count");
}

// A list in a block of exactly four entries: the address sanitizer stops every access outside it
static void test_exact_room(void)
{
	net_profile_t *exact = malloc(NET_PROFILES_MAX * sizeof(net_profile_t));
	static const char seen[1][NET_SSID_SIZE] = {"Home"};
	int count = 0;

	if(exact == NULL)
	{
		check(false, "memory for a list of four entries");
		return;
	}
	memset(exact, GUARD, NET_PROFILES_MAX * sizeof(net_profile_t));
	count = net_store(exact, count, "Home", "homepassword", "");
	count = net_store(exact, count, "WiCAN_a1b2", "@meatpi#", "");
	count = net_store(exact, count, "Garage", "garagepassword", "wican.local");
	count = net_store(exact, count, "Phone", "phonepass", "");
	check(count == 4 && net_choose(exact, count, seen, 1) == 3, "four profiles in a list of exactly four entries");
	check(net_store(exact, count, "Office", "officepass", "") == 4 && net_store(exact, count, "Garage", "", "") == 4 && net_choose(exact, count, seen, 1) == -1,
	      "a fifth profile and a replaced one in a list of exactly four entries");
	check(net_choose(exact, 5, seen, 1) == -1 && net_forget(exact, 5, "Home") == 5 && net_forget(exact, 5, "Office") == 5 && net_store(exact, 5, "Home", "", "") == -1,
	      "a profile_count of 5 with a list of exactly four entries: nothing behind them is read");
	check(net_forget(exact, 4, "WiCAN_a1b2") == 3 && net_forget(exact, 3, "Office") == 2 && net_forget(exact, 2, "Garage") == 1 && net_forget(exact, 1, "Phone") == 0,
	      "the last, the first, the middle and the only profile forgotten from a list of exactly four entries");
	free(exact);
}

// The same list kept as numbers: which network, which of its passwords and hosts
static void test_walk(void)
{
	static const char *const ssids[6] = {"Home", "WiCAN_a1b2", "Garage", "Phone", "home", "Home2"};
	static const char *const passwords[3] = {"", "12345678", "a much longer password than the one before it"};
	static const char *const hosts[3] = {"", "192.168.1.50", "wican_a1b2c3d4e5f6.local"};
	int ids[NET_PROFILES_MAX] = {0, 0, 0, 0}, variants[NET_PROFILES_MAX] = {0, 0, 0, 0};
	int count = 0, wrong = 0, wrong_count = 0, steps, i;
	int full = 0, replaced = 0, forgotten = 0, missing = 0;

	clear_room();
	for(steps = 0; steps < 100000; steps++)
	{
		int id = (int)(random_next() % 6);
		int variant = (int)(random_next() % 9);
		int at = -1, result;

		for(i = 0; i < count; i++) if(ids[i] == id) at = i;

		if(random_next() % 3 != 0)
		{
			result = net_store(list, count, ssids[id], passwords[variant % 3], hosts[variant / 3]);
			if(at >= 0)
			{
				variants[at] = variant;
				replaced++;
			}
			else
			{
				if(count == NET_PROFILES_MAX) full++;
				else count++;
				for(i = count - 1; i > 0; i--)
				{
					ids[i] = ids[i - 1];
					variants[i] = variants[i - 1];
				}
				ids[0] = id;
				variants[0] = variant;
			}
		}
		else
		{
			result = net_forget(list, count, ssids[id]);
			if(at >= 0)
			{
				for(i = at; i < count - 1; i++)
				{
					ids[i] = ids[i + 1];
					variants[i] = variants[i + 1];
				}
				count--;
				forgotten++;
				if(!entry_is_zero(count)) wrong++;
			}
			else
			{
				missing++;
			}
		}

		if(result != count) wrong_count++;
		for(i = 0; i < count; i++)
		{
			if(!entry_is(i, ssids[ids[i]], passwords[variants[i] % 3], hosts[variants[i] / 3])) wrong++;
		}
		if(!guards_intact()) wrong++;
	}
	check(full > 1000 && replaced > 1000 && forgotten > 1000 && missing > 1000,
	      "the walk stores into a full list, replaces, forgets and misses more than 1000 times each");
	check(wrong_count == 0, "100000 random stores and forgets return the number of profiles of the list kept as numbers");
	check(wrong == 0, "after each of them the list holds the profiles of the list kept as numbers, in its order");
}

int main(void)
{
	test_choose();
	test_choose_bytes();
	test_ssid_positions();
	test_choose_long_scan();
	test_choose_all();
	test_host_rule();
	test_host_rule_all();
	test_wican_ap();
	test_factory_password();
	test_store();
	test_store_replace();
	test_store_own_texts();
	test_store_refused();
	test_store_lengths();
	test_any_bytes();
	test_forget();
	test_no_list();
	test_exact_room();
	test_walk();
	return test_end();
}
