/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __WEB_JSON_H__
#define __WEB_JSON_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "json.h"
#include "values.h"
#include "catalog.h"
#include "layout.h"
#include "net_select.h"
#include "access.h"

/*
 * The bodies of the web interface of the display (display/API.md): what it answers and what it reads.
 * Examples, compared byte for byte by the test: display/test/fixtures/web_*.json.
 *
 * For every writer: the text has no whitespace and the members come in the order given here. In texts,
 * " and \ are written with a backslash, bytes below 0x20 and the byte 0x7F as \u00xx (hexadecimal digits
 * in small letters); everything else is passed on, also bytes that are no UTF-8. A text pointer that is
 * NULL counts as an empty text. The return value is the length without the terminating zero, or -1 if the
 * text does not fit: out is then an empty string, with size 0 nothing is written (out may then be NULL),
 * and no byte from out[size] on is ever touched.
 *
 * No answer ever contains a WiFi password: of a stored password web_wifi_json() tells nothing but whether
 * there is one and whether it is the one of the documentation. (Where an SSID or a host holds the same text
 * as a password, that text is in the answer - as the SSID or the host.)
 */

// GET /api/info
typedef struct
{
	const char *version;        // of this firmware, e.g. "0.1.0"
	const char *git;
	const char *slot;           // running app slot, "ota_0" or "ota_1"
	const char *reset;          // reason of the last reset as a word
	uint32_t up_s;
	bool safe_mode;
	bool rolled_back;           // the last update was not confirmed, the version before it runs
	bool update_pending;        // this version still waits for "Update in Ordnung?"
	uint32_t heap, heap_min;    // internal memory, bytes
	uint32_t psram, psram_min;
	int temp_c;                 // of the chip
	const char *heat;           // "normal", "dim" or "off"
	bool release_open;
	uint32_t release_left_s;
	const char *ssid;           // network the display is in, empty if in none
	const char *ip;
	int rssi;
	bool ap_on;                 // own access point
	const char *ap_ssid;
	const char *wican_host;
	const char *wican_id;       // adapter the display is bound to
	const char *wican_fw;
	const char *view;           // what the display shows about the adapter: "live", "no_wifi", ...
	const char *layout_name;
	const char *layout_source;  // "stored", "builtin", "generated" or "preview"
	uint32_t http_ok, http_failed, reconnects;
	const char *settings;       // the settings as their JSON text (settings_to_json), embedded as it is;
	                            // NULL or empty becomes null
} web_info_t;

// {"project":"wican-display","version":"..","git":"..","slot":"..","reset":"..","up":N,"safe_mode":B,
//  "rolled_back":B,"update_pending":B,"heap":N,"heap_min":N,"psram":N,"psram_min":N,"temp_c":N,"heat":"..",
//  "release":{"open":B,"left_s":N},"wifi":{"ssid":"..","ip":"..","rssi":N,"ap":B,"ap_ssid":".."},
//  "wican":{"host":"..","id":"..","fw":"..","view":".."},"layout":{"name":"..","source":".."},
//  "http":{"ok":N,"failed":N,"reconnects":N},"settings":{...}}
int web_info_json(const web_info_t *info, char *out, size_t size);

// GET /api/values: {"view":"live","values":{"ENGINE_RPM":{"v":812.5,"age":"fresh"},
//  "DPF_REGEN_STATUS":{"v":"on","age":"old"}}} with the values in their order. Values that are gone
// (VALUE_AGE_GONE), numbers that are not finite and values whose kind is none of value_kind_t are left out.
// A number is written as printf("%.9g") writes it in the C locale ("812.5", "0", "-40", "0.001", "1e+09");
// minus zero is written as "0".
// WEB_VALUES_SIZE is enough for VALUES_MAX values with the longest names and a view of up to 32 bytes, also
// when every byte of the names and the view has to be written as \u00xx (15063 bytes with the zero). With
// names and a view that need no escape the text has at most 4663 bytes with the zero.
#define WEB_VALUES_SIZE     15360
int web_values_json(const values_t *values, const char *view, uint64_t now_ms, char *out, size_t size);

// One network seen in a scan
typedef struct
{
	char ssid[NET_SSID_SIZE];
	int rssi;
	bool secure;
} web_seen_t;

// GET /api/wifi: {"current":"..","profiles":[{"ssid":"..","host":"..","password":B,"factory":B,
//  "wican_ap":B}],"seen":[{"ssid":"..","rssi":N,"secure":B}]}
// "password" tells whether one is stored (not empty), "factory" whether it is the one printed in the
// documentation of the WiCAN (net_is_factory_password), "wican_ap" whether the network is the access point
// of a WiCAN (net_is_wican_ap). Profiles in the order of the list (priority), seen networks in their order;
// seen networks with an empty SSID (hidden ones) are left out. A profile_count that is not 0 to
// NET_PROFILES_MAX counts as 0, a negative seen_count as 0. An SSID or host that has no terminating zero
// within its field (a list read from the flash may be damaged, see net_select.h) ends one byte before the
// end of the field: nothing behind a field is ever read as its text.
int web_wifi_json(const net_profile_t *profiles, int profile_count, const char *current,
                  const web_seen_t *seen, int seen_count, char *out, size_t size);

// POST /api/wifi: {"ssid":"..","password":"..","host":".."}
typedef struct
{
	char ssid[NET_SSID_SIZE];
	char password[NET_PASSWORD_SIZE];
	bool has_password;      // false: the member is missing - keep the password stored for this SSID
	char host[NET_HOST_SIZE];
} web_wifi_request_t;

#define WEB_WIFI_TOKENS     16

// Returns false and leaves *request untouched if the text is not a JSON object, "ssid" is missing, a known
// member is not a text, a text does not fit its field, contains a byte below 0x20 or an escape json_text()
// refuses, the SSID is empty, or the password has 1 to 7 bytes (WPA2 needs 8; empty means an open network).
// "host" is optional (empty = find the adapter). Members the format does not know are ignored. Of two
// members with the same name the last counts, and both have to be valid. A text that needs more than
// work_count tokens is refused. If the text is taken, *request is written whole, with zero bytes behind its
// texts.
bool web_wifi_parse(const char *json, size_t length, web_wifi_request_t *request,
                    json_token_t *work, int work_count);

// POST /api/wifi/forget: {"ssid":".."} - the same rules for the SSID, every other member is unknown here;
// `ssid` has NET_SSID_SIZE bytes, which are all written if the text is taken and left untouched if not
bool web_forget_parse(const char *json, size_t length, char *ssid, json_token_t *work, int work_count);

// The answer to PUT /api/layout.
// Refused (ok false): {"ok":false,"path":"pages[2].items[0].min","problem":"min is not below max"}
// Taken (ok true): {"ok":true,"name":"..","pages":N,"items":N,"warnings":N,"warning_path":"..",
//  "warning":"..","unknown":["KEY",...]} - "items" counts the items of all pages, "unknown" lists the keys
// of the layout that are not in the catalogue, each once, in the order of their first use; it is empty
// while the catalogue is not loaded (it holds nothing but CATALOG_BATTERY). layout and catalog are only
// read if ok.
// WEB_REPORT_SIZE is enough for every report and every layout, also when every byte of their texts has to
// be written as \u00xx (15002 bytes with the zero). A layout that layout_parse() accepted holds no control
// characters, so that only " and \ need an escape: at most 5108 bytes. With texts that need no escape 2772.
#define WEB_REPORT_SIZE     15360
int web_layout_report_json(bool ok, const layout_report_t *report, const layout_t *layout,
                           const catalog_t *catalog, char *out, size_t size);

// GET /api/ticket: {"ticket":N,"state":"waiting","left_s":N} with the state "unknown", "waiting",
// "confirmed", "refused" or "expired" ("unknown" also for a state that is none of the enum); left_s is
// written as 0 unless it waits
int web_ticket_json(uint32_t ticket, access_ticket_t state, uint32_t left_s, char *out, size_t size);

// The answer to a request that needs the knob: {"ticket":N,"hint":"Am Display bestätigen: Knopf drücken"}
int web_asked_json(uint32_t ticket, char *out, size_t size);

// GET /api/dtc/last: {"read":<result>,"read_age_s":N,"before_clear":<result>}
// `read` and `before_clear` are result texts of the adapter that dtc_result_parse() accepted; they are
// embedded as they are. NULL or an empty text becomes null. read_age_s: seconds since the display fetched
// `read`; it is written as 0 if there is none.
int web_dtc_last_json(const char *read, uint32_t read_age_s, const char *before_clear, char *out, size_t size);

#endif
