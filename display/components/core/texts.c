/*
 * Standalone display for the WiCAN W906 fork.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "texts.h"

// The reason words of the adapter (tools/w906/API.md) and of dtc_flow.h
static const struct
{
	const char *word;
	const char *text;
} REASONS[] = {
	{"busy", "Scan läuft bereits (anderes Gerät)"},
	{"ecu_offline", "Motorsteuergerät offline – Zündung an?"},
	{"engine_running", "Motor läuft – nur bei Motor aus"},
	{"engine_state_unknown", "Drehzahl nicht lesbar – nichts gelöscht"},
	{"not_supported", "Profil ohne Fehlerspeicher"},
	{"out_of_memory", "WiCAN: Speicher knapp – erneut lesen"},
	{"result_serialize_failed", "WiCAN: Speicher knapp – erneut lesen"},
	{"stale_seq", "Liste veraltet – erneut lesen"},
	{"read_required", "Liste veraltet – erneut lesen"},
	{"nothing_to_clear", "Keine Fehler zu löschen"},
	{"expired", "Auftrag verfallen – nichts gesendet"},
	{"not_ready", "WiCAN startet noch"},
	{"forbidden", "WiCAN lehnt die Anfrage ab"},
	{"bad_request", "WiCAN versteht die Anfrage nicht"},
	{"internal", "WiCAN: interner Fehler – erneut lesen"},
	{"no_answer", "Keine Antwort vom WiCAN"},
	{"no_result", "Ergebnis nicht abrufbar – erneut lesen"},
	{"restarted", "WiCAN neu gestartet – Ergebnis verloren"},
	{"superseded", "Von einem anderen Scan überholt"},
};

const char *text_view(conn_view_t view)
{
	switch(view)
	{
		case CONN_VIEW_NO_WIFI:     return "WiCAN nicht gefunden – schläft, stromlos oder außer Reichweite";
		case CONN_VIEW_CONNECTING:  return "Verbinde mit WiCAN …";
		case CONN_VIEW_FOREIGN:     return "Fremdes WiCAN – nicht gekoppelt";
		case CONN_VIEW_NO_API:      return "WiCAN-Firmware ohne Display-API – nur Live-Werte";
		case CONN_VIEW_AUTOPID_OFF: return "AutoPID nicht aktiv";
		case CONN_VIEW_STARTING:    return "WiCAN startet …";
		case CONN_VIEW_SCAN:        return "Live-Werte angehalten (Fehlerspeicher-Scan)";
		case CONN_VIEW_ECU_OFFLINE: return "Zündung aus – Motorsteuergerät offline";
		case CONN_VIEW_LIVE:        return "";
		// CONN_VIEW_NO_ANSWER, and what is no view: silence is the one thing known then
		default:                    return "WiCAN antwortet nicht";
	}
}

const char *text_view_word(conn_view_t view)
{
	switch(view)
	{
		case CONN_VIEW_NO_WIFI:     return "no_wifi";
		case CONN_VIEW_CONNECTING:  return "connecting";
		case CONN_VIEW_FOREIGN:     return "foreign";
		case CONN_VIEW_NO_API:      return "no_api";
		case CONN_VIEW_AUTOPID_OFF: return "autopid_off";
		case CONN_VIEW_STARTING:    return "starting";
		case CONN_VIEW_SCAN:        return "scan";
		case CONN_VIEW_ECU_OFFLINE: return "ecu_offline";
		case CONN_VIEW_LIVE:        return "live";
		default:                    return "no_answer";
	}
}

const char *text_block(dtc_flow_block_t block)
{
	switch(block)
	{
		case DTC_FLOW_ALLOWED:        return "";
		case DTC_FLOW_FOREIGN:        return "Fremdes WiCAN – nicht gekoppelt";
		case DTC_FLOW_NO_API:         return "WiCAN-Firmware ohne Display-API";
		case DTC_FLOW_AUTOPID_OFF:    return "AutoPID nicht aktiv";
		case DTC_FLOW_STARTING:       return "WiCAN startet noch";
		case DTC_FLOW_NOT_SUPPORTED:  return "Profil ohne Fehlerspeicher";
		case DTC_FLOW_BUSY:           return "Scan läuft bereits";
		case DTC_FLOW_ECU_OFFLINE:    return "Zündung aus – Motorsteuergerät offline";
		case DTC_FLOW_ENGINE_RUNNING: return "Motor läuft – nur bei Motor aus";
		case DTC_FLOW_RPM_UNKNOWN:    return "Drehzahl nicht lesbar";
		case DTC_FLOW_NO_LIST:        return "Erst lesen, dann löschen";
		case DTC_FLOW_LIST_OLD:       return "Liste veraltet – erneut lesen";
		case DTC_FLOW_NO_CODES:       return "Keine Fehler zu löschen";
		case DTC_FLOW_BUTTON_STUCK:   return "Knopf klemmt – Löschen gesperrt";
		// DTC_FLOW_NO_ADAPTER, and what is no block: never the empty text, which would offer the action
		default:                      return "WiCAN nicht erreichbar";
	}
}

const char *text_reason(const char *reason)
{
	if(reason == NULL) return NULL;

	for(size_t i = 0; i < sizeof(REASONS) / sizeof(REASONS[0]); i++)
	{
		if(strcmp(reason, REASONS[i].word) == 0) return REASONS[i].text;
	}
	return NULL;
}

const char *text_heat_word(guard_heat_t heat)
{
	switch(heat)
	{
		case GUARD_HEAT_NORMAL: return "normal";
		case GUARD_HEAT_DIM:    return "dim";
		// GUARD_HEAT_OFF, and what is no level, which guard_heat() treats as off too
		default:                return "off";
	}
}

// How far the scan is, 0 to 1000
static int progress(const wican_state_t *state)
{
	// A queued scan has not begun, whatever the counters still say, and a total of 0 cannot be divided by
	if(state == NULL || state->dtc.phase == WICAN_DTC_QUEUED || state->dtc.total == 0) return 0;
	if(state->dtc.step >= state->dtc.total) return 1000;

	// 64 bit: the step times 1000 does not fit into 32
	return (int)((uint64_t)state->dtc.step * 1000u / state->dtc.total);
}

ring_t ring_state(conn_view_t view, const wican_state_t *state, int level, bool old)
{
	ring_t ring = {RING_RED, 0};

	switch(view)
	{
		case CONN_VIEW_CONNECTING:
		case CONN_VIEW_STARTING:
			ring.kind = RING_YELLOW;
			break;

		case CONN_VIEW_NO_API:
		case CONN_VIEW_AUTOPID_OFF:
		case CONN_VIEW_ECU_OFFLINE:
			ring.kind = RING_GREY;
			break;

		case CONN_VIEW_SCAN:
			ring.kind = RING_PROGRESS;
			ring.permille = progress(state);
			break;

		case CONN_VIEW_LIVE:
			// A level beyond the known ones is no reason to show less than for a crit limit
			if(level >= 2) ring.kind = RING_RED;
			else if(level == 1 || old) ring.kind = RING_YELLOW;
			else ring.kind = RING_NONE;
			break;

		// CONN_VIEW_NO_WIFI, CONN_VIEW_NO_ANSWER, CONN_VIEW_FOREIGN, and what is no view
		default:
			break;
	}
	return ring;
}
