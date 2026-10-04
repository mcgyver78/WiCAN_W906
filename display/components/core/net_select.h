/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __NET_SELECT_H__
#define __NET_SELECT_H__

#include <stddef.h>
#include <stdbool.h>

/*
 * Which WiFi the display joins and where it looks for the WiCAN there. The network may be provided by a
 * router, by the WiCAN itself (its access point "WiCAN_<id>") or by anything else, so the display stores
 * several networks and takes the one that is there.
 */

#define NET_PROFILES_MAX    4
#define NET_SSID_SIZE       33
#define NET_PASSWORD_SIZE   65
#define NET_HOST_SIZE       40

typedef struct
{
	char ssid[NET_SSID_SIZE];
	char password[NET_PASSWORD_SIZE];
	char host[NET_HOST_SIZE];       // address or name of the WiCAN in this network, empty = find it
} net_profile_t;

typedef enum
{
	NET_HOST_GIVEN,         // the host stored with the profile
	NET_HOST_GATEWAY,       // the network is the access point of a WiCAN: it is the gateway
	NET_HOST_MDNS,          // look for the service _wican._tcp
} net_host_rule_t;

// Index of the profile to connect to: the first in the order of the list whose SSID is among the networks
// seen in a scan. The order of the list is the priority. SSIDs are compared byte by byte; profiles with an
// empty SSID never match. Returns -1 if none is in range. Here and below, an SSID in a list that has no
// terminating zero within its NET_SSID_SIZE bytes equals none, and a profile_count that is not 0 to
// NET_PROFILES_MAX is no list: nothing of it is read or written, net_choose returns -1.
int net_choose(const net_profile_t *profiles, int profile_count, const char (*seen)[NET_SSID_SIZE], int seen_count);

// How the WiCAN is found once the display is in the network of this profile
net_host_rule_t net_host_rule(const net_profile_t *profile);

// true if the SSID is the access point of a WiCAN: "WiCAN_" followed by at least one character
bool net_is_wican_ap(const char *ssid);

// true if the password is the one printed in the documentation of the WiCAN ("@meatpi#"). The display
// warns: everybody knows it, and the adapter accepts commands from everybody in its network.
bool net_is_factory_password(const char *password);

// Adds or replaces a profile. A profile with the same SSID is replaced in place; a new one is put first
// (highest priority) and the last one falls out if the list is full. An empty SSID is refused, as are an
// SSID, password or host that do not fit, a password of 1 to 7 bytes (WPA2 needs 8, empty = open), and a
// profile_count that is not 0 to NET_PROFILES_MAX. The texts may be those of a profile of the list. The
// entry is written whole, with zero bytes behind its texts, so that nothing of an older password stays.
// Returns the new number of profiles, -1 if refused (nothing changes).
int net_store(net_profile_t *profiles, int profile_count, const char *ssid, const char *password, const char *host);

// Removes the profile with this SSID, which may be the text of that profile itself. The entry that becomes
// free at the end of the list is zeroed. Returns the new number of profiles (unchanged if there was none or
// profile_count is no list).
int net_forget(net_profile_t *profiles, int profile_count, const char *ssid);

#endif
