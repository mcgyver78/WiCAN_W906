/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __TEXTS_H__
#define __TEXTS_H__

#include <stdint.h>
#include <stdbool.h>
#include "conn.h"
#include "dtc_flow.h"
#include "guard.h"

/*
 * What the display says about the state of the adapter and of a fault memory request, and the ring at the
 * edge of the screen that tells the same without reading. German texts, UTF-8, at most TEXT_MAX bytes
 * each, so that they fit two lines of the round screen.
 */

#define TEXT_MAX    79

// The line shown for the connection. Empty for CONN_VIEW_LIVE. A value that is no member of the enum gets
// the text of CONN_VIEW_NO_ANSWER.
//   NO_WIFI      "WiCAN nicht gefunden – schläft, stromlos oder außer Reichweite"
//   CONNECTING   "Verbinde mit WiCAN …"
//   NO_ANSWER    "WiCAN antwortet nicht"
//   FOREIGN      "Fremdes WiCAN – nicht gekoppelt"
//   NO_API       "WiCAN-Firmware ohne Display-API – nur Live-Werte"
//   AUTOPID_OFF  "AutoPID nicht aktiv"
//   STARTING     "WiCAN startet …"
//   SCAN         "Live-Werte angehalten (Fehlerspeicher-Scan)"
//   ECU_OFFLINE  "Zündung aus – Motorsteuergerät offline"
const char *text_view(conn_view_t view);

// The same as a word for GET /api/info: "no_wifi", "connecting", "no_answer", "foreign", "no_api",
// "autopid_off", "starting", "scan", "ecu_offline", "live"; "no_answer" for anything else
const char *text_view_word(conn_view_t view);

// Why a fault memory action is not offered. Empty for DTC_FLOW_ALLOWED; a value that is no member of the
// enum gets the text of DTC_FLOW_NO_ADAPTER.
//   NO_ADAPTER      "WiCAN nicht erreichbar"
//   FOREIGN         "Fremdes WiCAN – nicht gekoppelt"
//   NO_API          "WiCAN-Firmware ohne Display-API"
//   AUTOPID_OFF     "AutoPID nicht aktiv"
//   STARTING        "WiCAN startet noch"
//   NOT_SUPPORTED   "Profil ohne Fehlerspeicher"
//   BUSY            "Scan läuft bereits"
//   ECU_OFFLINE     "Zündung aus – Motorsteuergerät offline"
//   ENGINE_RUNNING  "Motor läuft – nur bei Motor aus"
//   RPM_UNKNOWN     "Drehzahl nicht lesbar"
//   NO_LIST         "Erst lesen, dann löschen"
//   LIST_OLD        "Liste veraltet – erneut lesen"
//   NO_CODES        "Keine Fehler zu löschen"
//   BUTTON_STUCK    "Knopf klemmt – Löschen gesperrt"
const char *text_block(dtc_flow_block_t block);

// The text for the reason of a failed request: a reason word of the adapter (tools/w906/API.md and W906.md)
// or of dtc_flow.h. Returns NULL for a word that is not in this list (also NULL and the empty text): the
// caller then shows the word itself.
//   busy                     "Scan läuft bereits (anderes Gerät)"
//   ecu_offline              "Motorsteuergerät offline – Zündung an?"
//   engine_running           "Motor läuft – nur bei Motor aus"
//   engine_state_unknown     "Drehzahl nicht lesbar – nichts gelöscht"
//   not_supported            "Profil ohne Fehlerspeicher"
//   out_of_memory            "WiCAN: Speicher knapp – erneut lesen"
//   result_serialize_failed  "WiCAN: Speicher knapp – erneut lesen"
//   stale_seq                "Liste veraltet – erneut lesen"
//   read_required            "Liste veraltet – erneut lesen"
//   nothing_to_clear         "Keine Fehler zu löschen"
//   expired                  "Auftrag verfallen – nichts gesendet"
//   not_ready                "WiCAN startet noch"
//   forbidden                "WiCAN lehnt die Anfrage ab"
//   bad_request              "WiCAN versteht die Anfrage nicht"
//   internal                 "WiCAN: interner Fehler – erneut lesen"
//   no_answer                "Keine Antwort vom WiCAN"
//   restarted                "WiCAN neu gestartet – Ergebnis verloren"
//   superseded               "Von einem anderen Scan überholt"
const char *text_reason(const char *reason);

// The word for the heat level in GET /api/info: "normal", "dim", "off"; "off" for anything else
const char *text_heat_word(guard_heat_t heat);

/*
 * The ring: a band of a few pixels at the edge of the round screen.
 */
typedef enum
{
	RING_NONE,      // live, all well: nothing to see
	RING_YELLOW,    // connecting, the adapter starts, or the values on the page are old
	RING_GREY,      // ignition off, AutoPID off, firmware without API
	RING_RED,       // adapter not found, no answer, foreign adapter, or a value on the page beyond a limit
	RING_PROGRESS,  // a fault memory scan runs: an arc that grows, see permille
} ring_kind_t;

typedef struct
{
	ring_kind_t kind;
	int permille;   // RING_PROGRESS: 0 to 1000 = step of total; 0 while queued or if total is 0. 0 for every
	                // other kind.
} ring_t;

// view: conn_view(); state: conn_state(), may be NULL; level: the worst layout_item_level() of the page
// shown (0, 1 or 2); old: at least one value of the page shown is VALUE_AGE_OLD.
//   NO_WIFI, NO_ANSWER, FOREIGN -> RED;  CONNECTING, STARTING -> YELLOW
//   NO_API, AUTOPID_OFF, ECU_OFFLINE -> GREY
//   SCAN -> PROGRESS with dtc.step * 1000 / dtc.total of the state (at most 1000; 0 without a state)
//   LIVE -> RED if level is 2 or more, else YELLOW if level is 1 or old, else NONE
// A view that is no member of the enum counts as NO_ANSWER.
ring_t ring_state(conn_view_t view, const wican_state_t *state, int level, bool old);

#endif
