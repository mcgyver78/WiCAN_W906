/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "net_select.h"

#define WICAN_AP_PREFIX     "WiCAN_"
#define FACTORY_PASSWORD    "@meatpi#"
#define WPA2_PASSWORD_MIN   8

// `stored` is an array of NET_SSID_SIZE bytes, from a list that may have been read from the flash: without
// a terminating zero it is no SSID. `ssid` is not read behind its end.
static bool ssid_equals(const char *stored, const char *ssid)
{
	for(int i = 0; i < NET_SSID_SIZE; i++)
	{
		if(stored[i] != ssid[i]) return false;
		if(stored[i] == '\0') return true;
	}
	return false;
}

// Whether profile_count can be the length of a list. A count that was stored may be damaged like the list
// itself; behind NET_PROFILES_MAX entries there is no memory that belongs to it.
static bool is_list(int profile_count)
{
	return profile_count >= 0 && profile_count <= NET_PROFILES_MAX;
}

static int find(const net_profile_t *profiles, int profile_count, const char *ssid)
{
	for(int i = 0; i < profile_count; i++)
	{
		if(ssid_equals(profiles[i].ssid, ssid)) return i;
	}
	return -1;
}

int net_choose(const net_profile_t *profiles, int profile_count, const char (*seen)[NET_SSID_SIZE], int seen_count)
{
	if(!is_list(profile_count)) return -1;

	for(int i = 0; i < profile_count; i++)
	{
		// A scan lists hidden networks with an empty SSID
		if(profiles[i].ssid[0] == '\0') continue;

		for(int j = 0; j < seen_count; j++)
		{
			if(ssid_equals(profiles[i].ssid, seen[j])) return i;
		}
	}
	return -1;
}

net_host_rule_t net_host_rule(const net_profile_t *profile)
{
	if(profile->host[0] != '\0') return NET_HOST_GIVEN;
	if(net_is_wican_ap(profile->ssid)) return NET_HOST_GATEWAY;
	return NET_HOST_MDNS;
}

bool net_is_wican_ap(const char *ssid)
{
	size_t length = strlen(WICAN_AP_PREFIX);

	return strncmp(ssid, WICAN_AP_PREFIX, length) == 0 && ssid[length] != '\0';
}

bool net_is_factory_password(const char *password)
{
	return strcmp(password, FACTORY_PASSWORD) == 0;
}

int net_store(net_profile_t *profiles, int profile_count, const char *ssid, const char *password, const char *host)
{
	size_t password_length = strlen(password);
	net_profile_t copy;
	int index;

	if(!is_list(profile_count)) return -1;
	if(ssid[0] == '\0' || strlen(ssid) >= NET_SSID_SIZE) return -1;
	if(password_length >= NET_PASSWORD_SIZE) return -1;
	if(password_length > 0 && password_length < WPA2_PASSWORD_MIN) return -1;
	if(strlen(host) >= NET_HOST_SIZE) return -1;

	// The texts may be those of a profile in the list, which is moved and overwritten below
	strcpy(copy.ssid, ssid);
	strcpy(copy.password, password);
	strcpy(copy.host, host);

	index = find(profiles, profile_count, ssid);
	if(index < 0)
	{
		if(profile_count == NET_PROFILES_MAX) profile_count--;
		memmove(&profiles[1], &profiles[0], (size_t)profile_count * sizeof(profiles[0]));
		profile_count++;
		index = 0;
	}

	// Nothing of the password that was here before may stay behind a shorter one
	memset(&profiles[index], 0, sizeof(profiles[index]));
	strcpy(profiles[index].ssid, copy.ssid);
	strcpy(profiles[index].password, copy.password);
	strcpy(profiles[index].host, copy.host);
	return profile_count;
}

int net_forget(net_profile_t *profiles, int profile_count, const char *ssid)
{
	int index;

	if(!is_list(profile_count)) return profile_count;

	index = find(profiles, profile_count, ssid);
	if(index < 0) return profile_count;

	// `ssid` may be the text of the profile itself. It is not read any more.
	profile_count--;
	memmove(&profiles[index], &profiles[index + 1], (size_t)(profile_count - index) * sizeof(profiles[0]));
	// Its password must not stay in the entry that became free
	memset(&profiles[profile_count], 0, sizeof(profiles[0]));
	return profile_count;
}
