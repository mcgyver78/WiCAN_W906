"""Mutations of display/components/core/web_route.c, see ../redproof.py."""

F = "components/core/web_route.c"
H = "components/core/web_route.h"
T = "test_web_route"


def row(method, path, route, knob="false", rests="false"):
    """One line of the list in the source, as it is aligned there."""
    return "\t{%-9s %-20s %-23s %-6s %s},\n" % (method + ",", '"%s",' % path, route + ",", knob + ",", rests)


PAGE = row("WEB_GET", "/", "WEB_ROUTE_PAGE")
INFO = row("WEB_GET", "/api/info", "WEB_ROUTE_INFO")
CATALOG = row("WEB_GET", "/api/catalog", "WEB_ROUTE_CATALOG")
VALUES = row("WEB_GET", "/api/values", "WEB_ROUTE_VALUES")
LAYOUT = row("WEB_GET", "/api/layout", "WEB_ROUTE_LAYOUT")
LAYOUT_PUT = row("WEB_PUT", "/api/layout", "WEB_ROUTE_LAYOUT_SAVE")
LAYOUT_RESET = row("WEB_POST", "/api/layout/reset", "WEB_ROUTE_LAYOUT_RESET")
DTC_LAST = row("WEB_GET", "/api/dtc/last", "WEB_ROUTE_DTC_LAST")
WIFI = row("WEB_GET", "/api/wifi", "WEB_ROUTE_WIFI")
WIFI_STORE = row("WEB_POST", "/api/wifi", "WEB_ROUTE_WIFI_STORE", "true")
WIFI_FORGET = row("WEB_POST", "/api/wifi/forget", "WEB_ROUTE_WIFI_FORGET")
SETTINGS = row("WEB_POST", "/api/settings", "WEB_ROUTE_SETTINGS")
REBOOT = row("WEB_POST", "/api/reboot", "WEB_ROUTE_REBOOT", "false", "true")
RESET = row("WEB_POST", "/api/reset", "WEB_ROUTE_RESET", "true", "true")
OTA = row("WEB_POST", "/api/ota", "WEB_ROUTE_OTA", "true", "true")
TICKET = row("WEB_GET", "/api/ticket", "WEB_ROUTE_TICKET")

REFUSAL = "\tweb_decision_t decision = {WEB_ROUTE_NONE, status, error, false, false, 0};"
MODE_CHECK = "\tif(strcmp(query, \"mode=check\") == 0) return WEB_ROUTE_LAYOUT_CHECK;\n"
MODE_APPLY = "\tif(strcmp(query, \"mode=apply\") == 0) return WEB_ROUTE_LAYOUT_APPLY;\n"
MODE_SAVE = "\tif(strcmp(query, \"mode=save\") == 0) return WEB_ROUTE_LAYOUT_SAVE;\n"
NO_MODE = MODE_SAVE + "\treturn WEB_ROUTE_NONE;\n"
ID_NAME = "\tif(strncmp(query, \"id=\", 3) != 0) return 0;\n"
ID_DIGITS = "\t\tif(++digits > 10) return 0;\n"
ID_END = "\tif(*query != '\\0' || number > UINT32_MAX) return 0;\n"
ZERO_IN_FRONT = "\t\tif(text > start && number == 0) return NULL;\n"
TOO_BIG = "\t\tif(number > 255) return NULL;\n"
ANY_DIGIT = "\treturn text > start ? text : NULL;\n"
FOUR = "\tfor(int i = 0; i < 4; i++)\n"
DOT = "\t\tif(i > 0 && *text++ != '.') return NULL;\n"
NAME = "\tstatic const char name[] = WEB_HOST_NAME \".local\";\n"
FOLD = "\t\tif(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');\n"
PORT_COLON = "\tif(*text != ':') return false;\n"
PORT_DIGITS = "\t\tif(++digits > 5) return false;\n"
PORT_END = "\treturn *text == '\\0' && digits > 0;\n"

ID_LOOP = "\tfor(query += 3; *query >= '0' && *query <= '9'; query++)\n"
ID_ADD = "\t\tnumber = number * 10 + (uint64_t)(*query - '0');\n"
ID_RETURN = "\treturn (uint32_t)number;\n"
NUMBER_LOOP = "\twhile(*text >= '0' && *text <= '9')\n"
NUMBER_ADD = "\t\tnumber = number * 10 + (*text++ - '0');\n"
PORT_LOOP = "\tfor(text++; *text >= '0' && *text <= '9'; text++)\n"
NAME_COMPARED = "\t\tif(c != name[i]) return NULL;\n"
NAME_END = "\treturn text + sizeof(name) - 1;\n"

QUERY = "\tconst char *query = request->query != NULL ? request->query : \"\";\n"
QUERY_FITS = "\tbool query_fits = query[0] == '\\0';\n"
PATH = "\t\tif(request->path == NULL || strcmp(request->path, ROUTES[i].path) != 0) continue;\n"
METHOD = "\t\tif(ROUTES[i].method == request->method) entry = &ROUTES[i];\n"
NOT_FOUND = "\tif(!path_known) return refused(404, \"not_found\");\n"
NOT_ALLOWED = "\tif(entry == NULL) return refused(405, \"method\");\n"
HOST = "\tif(!web_host_allowed(request->host)) return refused(403, \"host\");\n"
HEADER = "\tif(entry->method != WEB_GET && (request->header == NULL || strcmp(request->header, \"1\") != 0)) return refused(403, \"header\");\n"
CHANGES = "\tdecision.changes = entry->method != WEB_GET;\n"
KNOB = "\tdecision.knob = entry->knob;\n"
LAYOUT_ROUTE = "\t\tdecision.route = layout_route(query);\n"
LAYOUT_CHANGES = "\t\tdecision.changes = decision.route != WEB_ROUTE_LAYOUT_CHECK;\n"
LAYOUT_QUERY = "\t\tquery_fits = decision.route != WEB_ROUTE_NONE;\n"
LAYOUT_LIMIT = "\t\tlimit = WEB_BODY_LAYOUT_MAX;\n"
TICKET_NUMBER = "\t\tdecision.ticket = ticket_number(query);\n"
TICKET_QUERY = "\t\tquery_fits = decision.ticket != 0;\n"
OTA_LIMIT = "\tif(entry->route == WEB_ROUTE_OTA) limit = request->slot_size;\n"
LOCKED = "\tif(decision.changes && !request->release_open) return refused(403, \"locked\");\n"
BAD_QUERY = "\tif(!query_fits) return refused(400, \"query\");\n"
BODY_OPEN = "\tif(entry->method != WEB_GET)\n\t{\n"
NO_LENGTH = "\t\tif(!request->has_length) return refused(411, \"length\");\n"
TOO_LARGE = "\t\tif(request->length > limit || (entry->route == WEB_ROUTE_OTA && request->length == 0)) return refused(413, \"too_large\");\n"
BODY = BODY_OPEN + NO_LENGTH + TOO_LARGE + "\t}\n"
BUSY = "\tif(entry->rests && request->busy) return refused(409, \"busy\");\n"
# What stands between the check of the header and the check of the release
BETWEEN = ("\n\tdecision.route = entry->route;\n" + CHANGES + KNOB +
           "\tif(entry->route == WEB_ROUTE_LAYOUT_SAVE)\n\t{\n"
           "\t\t// Only the check changes nothing. A query that names no mode has to pass the release like a change:\n"
           "\t\t// \"locked\" goes before \"query\".\n" +
           LAYOUT_ROUTE + LAYOUT_CHANGES + LAYOUT_QUERY + LAYOUT_LIMIT + "\t}\n"
           "\tif(entry->route == WEB_ROUTE_TICKET)\n\t{\n" + TICKET_NUMBER + TICKET_QUERY + "\t}\n" + OTA_LIMIT +
           "\n\t// No query that fits is longer than 13 bytes, so WEB_QUERY_MAX needs no check of its own\n")

GOES_ON = "\tweb_decision_t decision = {WEB_ROUTE_NONE, 0, NULL, false, false, 0};\n\tuint32_t limit"
LAYOUT_NOTE = "\t// Stands for check, apply and save: the mode in the query names the route\n"

HOST_NULL = "\tif(host == NULL) return false;\n"
HOST_NAME = "\trest = behind_name(host);\n"
HOST_ADDRESS = "\tif(rest == NULL) rest = behind_address(host);\n"
HOST_END = "\treturn rest != NULL && is_port_or_end(rest);\n"

WORD_NULL = "\tif(error != NULL)\n"
HINT = "strcmp(error, \"locked\") == 0 ? "
FITS = "\tif(length < 0 || (size_t)length >= size)\n"
EMPTY = "\t\tif(size > 0) out[0] = '\\0';\n"

MUTATIONS = [
    # the list: a line missing
    ("web_route_page_missing", T, F, PAGE, ""),
    ("web_route_info_missing", T, F, INFO, ""),
    ("web_route_catalog_missing", T, F, CATALOG, ""),
    ("web_route_values_missing", T, F, VALUES, ""),
    ("web_route_layout_missing", T, F, LAYOUT, ""),
    ("web_route_layout_put_missing", T, F, LAYOUT_PUT, ""),
    ("web_route_layout_reset_missing", T, F, LAYOUT_RESET, ""),
    ("web_route_dtc_last_missing", T, F, DTC_LAST, ""),
    ("web_route_wifi_missing", T, F, WIFI, ""),
    ("web_route_wifi_store_missing", T, F, WIFI_STORE, ""),
    ("web_route_wifi_forget_missing", T, F, WIFI_FORGET, ""),
    ("web_route_settings_missing", T, F, SETTINGS, ""),
    ("web_route_reboot_missing", T, F, REBOOT, ""),
    ("web_route_reset_missing", T, F, RESET, ""),
    ("web_route_ota_missing", T, F, OTA, ""),
    ("web_route_ticket_missing", T, F, TICKET, ""),

    # the list: another method, another route, another path
    ("web_route_reboot_by_get", T, F, REBOOT, row("WEB_GET", "/api/reboot", "WEB_ROUTE_REBOOT", "false", "true")),
    ("web_route_reset_by_get", T, F, RESET, row("WEB_GET", "/api/reset", "WEB_ROUTE_RESET", "true", "true")),
    ("web_route_ota_by_put", T, F, OTA, row("WEB_PUT", "/api/ota", "WEB_ROUTE_OTA", "true", "true")),
    ("web_route_settings_by_put", T, F, SETTINGS, row("WEB_PUT", "/api/settings", "WEB_ROUTE_SETTINGS")),
    ("web_route_layout_by_post", T, F, LAYOUT_PUT, row("WEB_POST", "/api/layout", "WEB_ROUTE_LAYOUT_SAVE")),
    ("web_route_ticket_by_post", T, F, TICKET, row("WEB_POST", "/api/ticket", "WEB_ROUTE_TICKET")),
    ("web_route_info_is_the_catalog", T, F, INFO, row("WEB_GET", "/api/info", "WEB_ROUTE_CATALOG")),
    ("web_route_values_are_the_layout", T, F, VALUES, row("WEB_GET", "/api/values", "WEB_ROUTE_LAYOUT")),
    ("web_route_page_is_the_info", T, F, PAGE, row("WEB_GET", "/", "WEB_ROUTE_INFO")),
    ("web_route_dtc_last_is_the_wifi", T, F, DTC_LAST, row("WEB_GET", "/api/dtc/last", "WEB_ROUTE_WIFI")),
    ("web_route_forget_is_the_store", T, F, WIFI_FORGET, row("WEB_POST", "/api/wifi/forget", "WEB_ROUTE_WIFI_STORE")),
    ("web_route_reset_is_the_reboot", T, F, RESET, row("WEB_POST", "/api/reset", "WEB_ROUTE_REBOOT", "true", "true")),
    ("web_route_layout_reset_is_the_settings", T, F, LAYOUT_RESET, row("WEB_POST", "/api/layout/reset", "WEB_ROUTE_SETTINGS")),
    ("web_route_page_at_index_html", T, F, PAGE, row("WEB_GET", "/index.html", "WEB_ROUTE_PAGE")),
    ("web_route_dtc_last_at_dtc", T, F, DTC_LAST, row("WEB_GET", "/api/dtc", "WEB_ROUTE_DTC_LAST")),
    ("web_route_ota_at_firmware", T, F, OTA, row("WEB_POST", "/api/firmware", "WEB_ROUTE_OTA", "true", "true")),
    ("web_route_ticket_at_tickets", T, F, TICKET, row("WEB_GET", "/api/tickets", "WEB_ROUTE_TICKET")),
    ("web_route_reboot_and_reset_swapped", T, F, REBOOT + RESET,
     row("WEB_POST", "/api/reboot", "WEB_ROUTE_RESET", "false", "true") + row("WEB_POST", "/api/reset", "WEB_ROUTE_REBOOT", "true", "true")),
    ("web_route_knob_of_store_and_forget_swapped", T, F, WIFI_STORE + WIFI_FORGET,
     row("WEB_POST", "/api/wifi", "WEB_ROUTE_WIFI_STORE") + row("WEB_POST", "/api/wifi/forget", "WEB_ROUTE_WIFI_FORGET", "true")),
    ("web_route_methods_of_the_layout_swapped", T, F, LAYOUT + LAYOUT_NOTE + LAYOUT_PUT,
     row("WEB_PUT", "/api/layout", "WEB_ROUTE_LAYOUT") + row("WEB_GET", "/api/layout", "WEB_ROUTE_LAYOUT_SAVE")),
    ("web_route_info_also_by_post", T, F, INFO, INFO + row("WEB_POST", "/api/info", "WEB_ROUTE_INFO")),
    ("web_route_settings_also_by_get", T, F, SETTINGS, SETTINGS + row("WEB_GET", "/api/settings", "WEB_ROUTE_SETTINGS")),
    ("web_route_reboot_also_by_get", T, F, REBOOT, REBOOT + row("WEB_GET", "/api/reboot", "WEB_ROUTE_REBOOT", "false", "true")),
    ("web_route_page_also_by_any_other_method", T, F, PAGE, PAGE + row("WEB_OTHER", "/", "WEB_ROUTE_PAGE")),
    ("web_route_wifi_store_also_by_put", T, F, WIFI_STORE, WIFI_STORE + row("WEB_PUT", "/api/wifi", "WEB_ROUTE_WIFI_STORE", "true")),
    ("web_route_fault_memory_read_through_the_web", T, F, DTC_LAST, DTC_LAST + row("WEB_GET", "/api/dtc/read", "WEB_ROUTE_DTC_LAST")),
    ("web_route_fault_memory_cleared_through_the_web", T, F, DTC_LAST, DTC_LAST + row("WEB_POST", "/api/dtc/clear", "WEB_ROUTE_DTC_LAST")),

    # the list: the knob and the busy display
    ("web_route_wifi_store_without_knob", T, F, WIFI_STORE, row("WEB_POST", "/api/wifi", "WEB_ROUTE_WIFI_STORE", "false")),
    ("web_route_reset_without_knob", T, F, RESET, row("WEB_POST", "/api/reset", "WEB_ROUTE_RESET", "false", "true")),
    ("web_route_ota_without_knob", T, F, OTA, row("WEB_POST", "/api/ota", "WEB_ROUTE_OTA", "false", "true")),
    ("web_route_settings_with_knob", T, F, SETTINGS, row("WEB_POST", "/api/settings", "WEB_ROUTE_SETTINGS", "true")),
    ("web_route_reboot_with_knob", T, F, REBOOT, row("WEB_POST", "/api/reboot", "WEB_ROUTE_REBOOT", "true", "true")),
    ("web_route_forget_with_knob", T, F, WIFI_FORGET, row("WEB_POST", "/api/wifi/forget", "WEB_ROUTE_WIFI_FORGET", "true")),
    ("web_route_layout_put_with_knob", T, F, LAYOUT_PUT, row("WEB_PUT", "/api/layout", "WEB_ROUTE_LAYOUT_SAVE", "true")),
    ("web_route_knob_not_reported", T, F, KNOB, ""),
    ("web_route_reboot_while_busy", T, F, REBOOT, row("WEB_POST", "/api/reboot", "WEB_ROUTE_REBOOT", "false", "false")),
    ("web_route_reset_while_busy", T, F, RESET, row("WEB_POST", "/api/reset", "WEB_ROUTE_RESET", "true", "false")),
    ("web_route_ota_while_busy", T, F, OTA, row("WEB_POST", "/api/ota", "WEB_ROUTE_OTA", "true", "false")),
    ("web_route_settings_refused_while_busy", T, F, SETTINGS, row("WEB_POST", "/api/settings", "WEB_ROUTE_SETTINGS", "false", "true")),
    ("web_route_wifi_store_refused_while_busy", T, F, WIFI_STORE, row("WEB_POST", "/api/wifi", "WEB_ROUTE_WIFI_STORE", "true", "true")),
    ("web_route_info_refused_while_busy", T, F, INFO, row("WEB_GET", "/api/info", "WEB_ROUTE_INFO", "false", "true")),
    ("web_route_busy_not_refused", T, F, BUSY, ""),
    ("web_route_busy_refuses_every_route", T, F, BUSY, BUSY.replace("entry->rests && ", "")),
    ("web_route_busy_status_changed", T, F, BUSY, BUSY.replace("409", "503")),
    ("web_route_busy_word_changed", T, F, BUSY, BUSY.replace("\"busy\"", "\"Busy\"")),
    ("web_route_busy_refuses_what_needs_the_knob", T, F, BUSY, BUSY.replace("entry->rests", "entry->knob")),
    ("web_route_busy_only_refused_with_a_body", T, F, BUSY, BUSY.replace("request->busy", "request->busy && request->length > 0")),

    # path and method
    ("web_route_null_path_read", T, F, PATH, PATH.replace("request->path == NULL || ", "")),
    ("web_route_path_may_go_on", T, F, PATH, PATH.replace("strcmp(request->path, ROUTES[i].path)", "strncmp(request->path, ROUTES[i].path, strlen(ROUTES[i].path))")),
    ("web_route_path_may_be_cut_short", T, F, PATH, PATH.replace("strcmp(request->path, ROUTES[i].path)", "strncmp(request->path, ROUTES[i].path, strlen(request->path))")),
    ("web_route_empty_path_is_the_page", T, F, PATH,
     "\t\tif(request->path == NULL || (strcmp(request->path, ROUTES[i].path) != 0 && !(request->path[0] == '\\0' && i == 0))) continue;\n"),
    ("web_route_path_with_a_slash_behind", T, F, PATH,
     "\t\tif(request->path == NULL || strncmp(request->path, ROUTES[i].path, strlen(ROUTES[i].path)) != 0) continue;\n"
     "\t\tif(request->path[strlen(ROUTES[i].path)] != '\\0' && strcmp(request->path + strlen(ROUTES[i].path), \"/\") != 0) continue;\n"),
    ("web_route_unknown_path_is_405", T, F, NOT_FOUND, NOT_FOUND.replace("404, \"not_found\"", "405, \"method\"")),
    ("web_route_not_found_status_changed", T, F, NOT_FOUND, NOT_FOUND.replace("404", "400")),
    ("web_route_not_found_word_changed", T, F, NOT_FOUND, NOT_FOUND.replace("not_found", "notfound")),
    ("web_route_method_not_compared", T, F, METHOD, "\t\tentry = &ROUTES[i];\n"),
    ("web_route_unknown_method_taken_for_get", T, F, METHOD,
     "\t\tif(ROUTES[i].method == request->method || (ROUTES[i].method == WEB_GET && request->method > WEB_OTHER)) entry = &ROUTES[i];\n"),
    ("web_route_only_the_first_line_of_a_path_looked_at", T, F, METHOD, METHOD + "\t\tbreak;\n"),
    ("web_route_wrong_method_takes_the_first_line_of_the_path", T, F, METHOD,
     "\t\tif(ROUTES[i].method == request->method || entry == NULL) entry = &ROUTES[i];\n"),
    ("web_route_wrong_method_is_404", T, F, NOT_ALLOWED, NOT_ALLOWED.replace("405, \"method\"", "404, \"not_found\"")),
    ("web_route_method_status_changed", T, F, NOT_ALLOWED, NOT_ALLOWED.replace("405", "400")),
    ("web_route_method_word_changed", T, F, NOT_ALLOWED, NOT_ALLOWED.replace("\"method\"", "\"Method\"")),
    ("web_route_host_checked_before_the_method", T, F, NOT_ALLOWED + HOST, HOST + NOT_ALLOWED),
    ("web_route_host_checked_before_the_path", T, F, NOT_FOUND + NOT_ALLOWED + HOST, HOST + NOT_FOUND + NOT_ALLOWED),
    ("web_route_method_checked_before_the_path", T, F, NOT_FOUND + NOT_ALLOWED, NOT_ALLOWED + NOT_FOUND),

    # host and header
    ("web_route_host_not_checked", T, F, HOST, ""),
    ("web_route_host_not_checked_for_get", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->method != WEB_GET && !web_host_allowed")),
    ("web_route_host_only_checked_for_get", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->method == WEB_GET && !web_host_allowed")),
    ("web_route_host_status_changed", T, F, HOST, HOST.replace("403, \"host\"", "400, \"host\"")),
    ("web_route_host_refused_as_header", T, F, HOST, HOST.replace("\"host\"", "\"header\"")),
    ("web_route_host_word_changed", T, F, HOST, HOST.replace("\"host\"", "\"Host\"")),
    ("web_route_host_not_checked_while_the_release_is_open", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(!request->release_open && !web_host_allowed")),
    ("web_route_host_not_checked_while_busy", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(!request->busy && !web_host_allowed")),
    ("web_route_host_not_checked_for_the_ticket", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_TICKET && !web_host_allowed")),
    ("web_route_host_not_checked_for_the_page", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_PAGE && !web_host_allowed")),
    ("web_route_host_not_checked_for_the_firmware", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_OTA && !web_host_allowed")),
    ("web_route_header_not_checked", T, F, HEADER, ""),
    ("web_route_header_needed_for_get", T, F, HEADER, HEADER.replace("entry->method != WEB_GET && ", "")),
    ("web_route_header_not_needed_for_put", T, F, HEADER, HEADER.replace("entry->method != WEB_GET", "entry->method == WEB_POST")),
    ("web_route_header_not_needed_for_post", T, F, HEADER, HEADER.replace("entry->method != WEB_GET", "entry->method == WEB_PUT")),
    ("web_route_missing_header_passes", T, F, HEADER, HEADER.replace("request->header == NULL || ", "request->header != NULL && ")),
    ("web_route_null_header_read", T, F, HEADER, HEADER.replace("request->header == NULL || ", "")),
    ("web_route_header_may_go_on", T, F, HEADER, HEADER.replace("strcmp(request->header, \"1\") != 0", "request->header[0] != '1'")),
    ("web_route_header_any_text", T, F, HEADER, HEADER.replace("strcmp(request->header, \"1\") != 0", "request->header[0] == '\\0'")),
    ("web_route_header_value_changed", T, F, HEADER, HEADER.replace("\"1\")", "\"0\")")),
    ("web_route_header_status_changed", T, F, HEADER, HEADER.replace("403, \"header\"", "400, \"header\"")),
    ("web_route_header_word_changed", T, F, HEADER, HEADER.replace("\"header\")", "\"head\")")),
    ("web_route_header_compared_without_bit_5", T, F, HEADER,
     HEADER.replace("strcmp(request->header, \"1\") != 0", "(request->header[0] | 0x20) != '1' || request->header[1] != '\\0'")),
    ("web_route_header_true_passes", T, F, HEADER,
     HEADER.replace("strcmp(request->header, \"1\") != 0", "(strcmp(request->header, \"1\") != 0 && strcmp(request->header, \"true\") != 0)")),
    ("web_route_header_not_needed_while_the_release_is_open", T, F, HEADER,
     HEADER.replace("entry->method != WEB_GET &&", "entry->method != WEB_GET && !request->release_open &&")),
    ("web_route_header_not_needed_for_the_knob", T, F, HEADER, HEADER.replace("entry->method != WEB_GET &&", "entry->method != WEB_GET && !entry->knob &&")),
    ("web_route_header_not_needed_without_length", T, F, HEADER,
     HEADER.replace("entry->method != WEB_GET &&", "entry->method != WEB_GET && request->has_length &&")),
    ("web_route_header_checked_before_the_host", T, F, HOST + HEADER, HEADER + HOST),
    ("web_route_header_checked_behind_the_release", T, F, HEADER + BETWEEN + LOCKED, BETWEEN + LOCKED + HEADER),
    ("web_route_host_checked_behind_the_release", T, F, HOST + HEADER + BETWEEN + LOCKED, HEADER + BETWEEN + LOCKED + HOST),

    # the release
    ("web_route_release_not_checked", T, F, LOCKED, ""),
    ("web_route_release_only_for_the_knob", T, F, LOCKED, LOCKED.replace("decision.changes", "decision.knob")),
    ("web_route_release_not_for_the_knob", T, F, LOCKED, LOCKED.replace("decision.changes", "decision.changes && !decision.knob")),
    ("web_route_locked_status_changed", T, F, LOCKED, LOCKED.replace("403", "401")),
    ("web_route_locked_word_changed", T, F, LOCKED, LOCKED.replace("\"locked\"", "\"lock\"")),
    ("web_route_release_not_for_the_reboot", T, F, LOCKED, LOCKED.replace("decision.changes &&", "decision.changes && entry->route != WEB_ROUTE_REBOOT &&")),
    ("web_route_release_not_for_forgetting_a_network", T, F, LOCKED,
     LOCKED.replace("decision.changes &&", "decision.changes && entry->route != WEB_ROUTE_WIFI_FORGET &&")),
    ("web_route_release_not_for_the_layout_reset", T, F, LOCKED,
     LOCKED.replace("decision.changes &&", "decision.changes && entry->route != WEB_ROUTE_LAYOUT_RESET &&")),
    ("web_route_release_not_for_the_settings", T, F, LOCKED, LOCKED.replace("decision.changes &&", "decision.changes && entry->route != WEB_ROUTE_SETTINGS &&")),
    ("web_route_release_not_checked_while_busy", T, F, LOCKED, LOCKED.replace("decision.changes &&", "decision.changes && !request->busy &&")),
    ("web_route_release_not_checked_without_a_body", T, F, LOCKED, LOCKED.replace("decision.changes &&", "decision.changes && request->length > 0 &&")),
    ("web_route_release_needed_for_the_ticket", T, F, LOCKED, LOCKED.replace("decision.changes &&", "(decision.changes || entry->route == WEB_ROUTE_TICKET) &&")),
    ("web_route_release_needed_for_the_wifi_list", T, F, LOCKED, LOCKED.replace("decision.changes &&", "(decision.changes || entry->route == WEB_ROUTE_WIFI) &&")),
    ("web_route_get_changes", T, F, CHANGES, "\tdecision.changes = true;\n"),
    ("web_route_nothing_changes", T, F, CHANGES, "\tdecision.changes = false;\n"),
    ("web_route_layout_check_needs_the_release", T, F, LAYOUT_CHANGES, ""),
    ("web_route_layout_apply_needs_no_release", T, F, LAYOUT_CHANGES, LAYOUT_CHANGES.replace("!= WEB_ROUTE_LAYOUT_CHECK", "== WEB_ROUTE_LAYOUT_SAVE")),
    ("web_route_layout_without_mode_needs_no_release", T, F, LAYOUT_CHANGES,
     LAYOUT_CHANGES.replace("!= WEB_ROUTE_LAYOUT_CHECK", "!= WEB_ROUTE_LAYOUT_CHECK && decision.route != WEB_ROUTE_NONE")),
    ("web_route_layout_save_needs_no_release", T, F, LAYOUT_CHANGES,
     LAYOUT_CHANGES.replace("!= WEB_ROUTE_LAYOUT_CHECK", "!= WEB_ROUTE_LAYOUT_CHECK && decision.route != WEB_ROUTE_LAYOUT_SAVE")),
    ("web_route_layout_check_asks_for_the_knob", T, F, LAYOUT_LIMIT, LAYOUT_LIMIT + "\t\tdecision.knob = decision.route == WEB_ROUTE_LAYOUT_CHECK;\n"),
    ("web_route_query_checked_before_the_release", T, F, LOCKED + BAD_QUERY, BAD_QUERY + LOCKED),
    ("web_route_release_checked_behind_the_body", T, F, LOCKED + BAD_QUERY + BODY, BAD_QUERY + BODY + LOCKED),
    ("web_route_busy_checked_before_the_release", T, F, LOCKED + BAD_QUERY + BODY + BUSY, BUSY + LOCKED + BAD_QUERY + BODY),

    # the query
    ("web_route_null_query_read", T, F, QUERY, "\tconst char *query = request->query;\n"),
    ("web_route_query_only_refused_if_missing", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && query[0] == '\\0'")),
    ("web_route_query_only_checked_for_get", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->method == WEB_GET")),
    ("web_route_query_not_checked_for_get", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->method != WEB_GET")),
    ("web_route_query_taken_where_none_belongs", T, F, QUERY_FITS, "\tbool query_fits = true;\n"),
    ("web_route_query_status_changed", T, F, BAD_QUERY, BAD_QUERY.replace("400", "404")),
    ("web_route_query_word_changed", T, F, BAD_QUERY, BAD_QUERY.replace("\"query\"", "\"Query\"")),
    ("web_route_query_not_checked_while_busy", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && !request->busy")),
    ("web_route_query_not_checked_without_length", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && request->has_length")),
    ("web_route_query_checked_behind_the_length", T, F, BAD_QUERY + BODY, BODY + BAD_QUERY),
    ("web_route_busy_checked_before_the_query", T, F, BAD_QUERY + BODY + BUSY, BUSY + BAD_QUERY + BODY),
    ("web_route_layout_mode_not_read", T, F, LAYOUT_ROUTE, LAYOUT_ROUTE.replace("layout_route(query)", "layout_route(\"mode=save\")")),
    ("web_route_layout_without_mode_goes_on", T, F, LAYOUT_QUERY, "\t\tquery_fits = true;\n"),
    ("web_route_layout_without_mode_is_a_check", T, F, NO_MODE, MODE_SAVE + "\treturn WEB_ROUTE_LAYOUT_CHECK;\n"),
    ("web_route_layout_without_mode_is_a_save", T, F, NO_MODE, "\treturn WEB_ROUTE_LAYOUT_SAVE;\n"),
    ("web_route_mode_check_is_apply", T, F, MODE_CHECK, MODE_CHECK.replace("WEB_ROUTE_LAYOUT_CHECK", "WEB_ROUTE_LAYOUT_APPLY")),
    ("web_route_mode_apply_is_check", T, F, MODE_APPLY, MODE_APPLY.replace("WEB_ROUTE_LAYOUT_APPLY", "WEB_ROUTE_LAYOUT_CHECK")),
    ("web_route_mode_apply_is_save", T, F, MODE_APPLY, MODE_APPLY.replace("WEB_ROUTE_LAYOUT_APPLY", "WEB_ROUTE_LAYOUT_SAVE")),
    ("web_route_mode_save_is_check", T, F, MODE_SAVE, MODE_SAVE.replace("WEB_ROUTE_LAYOUT_SAVE", "WEB_ROUTE_LAYOUT_CHECK")),
    ("web_route_mode_check_may_go_on", T, F, MODE_CHECK, MODE_CHECK.replace("strcmp(query, \"mode=check\")", "strncmp(query, \"mode=check\", 10)")),
    ("web_route_mode_apply_may_go_on", T, F, MODE_APPLY, MODE_APPLY.replace("strcmp(query, \"mode=apply\")", "strncmp(query, \"mode=apply\", 10)")),
    ("web_route_mode_save_may_go_on", T, F, MODE_SAVE, MODE_SAVE.replace("strcmp(query, \"mode=save\")", "strncmp(query, \"mode=save\", 9)")),
    ("web_route_mode_found_anywhere", T, F, MODE_SAVE, MODE_SAVE.replace("strcmp(query, \"mode=save\") == 0", "strstr(query, \"mode=save\") != NULL")),
    ("web_route_mode_save_unknown", T, F, MODE_SAVE, ""),
    ("web_route_mode_beginning_of_check_is_enough", T, F, MODE_CHECK,
     MODE_CHECK.replace("strcmp(query, \"mode=check\")", "strncmp(query, \"mode=check\", strlen(query))")),
    ("web_route_mode_beginning_of_apply_is_enough", T, F, MODE_APPLY,
     MODE_APPLY.replace("strcmp(query, \"mode=apply\")", "strncmp(query, \"mode=apply\", strlen(query) > 5 ? strlen(query) : 10)")),
    ("web_route_mode_beginning_of_save_is_enough", T, F, MODE_SAVE,
     MODE_SAVE.replace("strcmp(query, \"mode=save\")", "strncmp(query, \"mode=save\", strlen(query) > 5 ? strlen(query) : 9)")),
    ("web_route_mode_value_alone_is_enough", T, F, MODE_SAVE,
     MODE_SAVE.replace("strcmp(query, \"mode=save\") == 0", "(strcmp(query, \"mode=save\") == 0 || strcmp(query, \"save\") == 0)")),
    ("web_route_layout_without_mode_is_the_reading_route", T, F, NO_MODE, MODE_SAVE + "\treturn WEB_ROUTE_LAYOUT;\n"),
    ("web_route_id_name_not_compared", T, F, ID_NAME, "\tif(strlen(query) < 3) return 0;\n"),
    ("web_route_id_equals_sign_not_compared", T, F, ID_NAME, "\tif(strncmp(query, \"id\", 2) != 0) return 0;\n"),
    ("web_route_id_of_11_digits", T, F, ID_DIGITS, ID_DIGITS.replace("> 10", "> 11")),
    ("web_route_id_of_9_digits_at_most", T, F, ID_DIGITS, ID_DIGITS.replace("> 10", "> 9")),
    ("web_route_id_of_20_digits", T, F, ID_DIGITS, ID_DIGITS.replace("> 10", "> 20")),
    ("web_route_id_digits_behind_the_tenth_ignored", T, F, ID_DIGITS, ID_DIGITS.replace("return 0;", "continue;")),
    ("web_route_id_above_32_bit_cut", T, F, ID_END, ID_END.replace(" || number > UINT32_MAX", "")),
    ("web_route_id_largest_refused", T, F, ID_END, ID_END.replace("number > UINT32_MAX", "number >= UINT32_MAX")),
    ("web_route_id_above_31_bit_refused", T, F, ID_END, ID_END.replace("number > UINT32_MAX", "number > INT32_MAX")),
    ("web_route_id_may_go_on", T, F, ID_END, ID_END.replace("*query != '\\0' || ", "")),
    ("web_route_id_zeros_in_front_refused", T, F, ID_END, ID_END.replace("*query != '\\0'", "*query != '\\0' || (digits > 1 && number < 1000000000)")),
    ("web_route_id_hexadecimal", T, F, "\t\tnumber = number * 10 + (uint64_t)(*query - '0');\n", "\t\tnumber = number * 16 + (uint64_t)(*query - '0');\n"),
    ("web_route_id_colon_is_a_digit", T, F, ID_LOOP, ID_LOOP.replace("<= '9'", "<= ':'")),
    ("web_route_id_slash_is_a_digit", T, F, ID_LOOP, ID_LOOP.replace(">= '0'", ">= '/'")),
    ("web_route_id_0_is_no_digit", T, F, ID_LOOP, ID_LOOP.replace(">= '0'", ">= '1'")),
    ("web_route_id_9_is_no_digit", T, F, ID_LOOP, ID_LOOP.replace("<= '9'", "<= '8'")),
    ("web_route_id_digits_worth_one_less", T, F, ID_ADD, ID_ADD.replace("- '0'", "- '1'")),
    ("web_route_id_cut_to_16_bit", T, F, ID_RETURN, "\treturn (uint32_t)number & 0xFFFFu;\n"),
    ("web_route_id_one_more", T, F, ID_RETURN, "\treturn (uint32_t)number + 1;\n"),
    ("web_route_id_name_in_upper_case", T, F, ID_NAME, "\tif(strncmp(query, \"id=\", 3) != 0 && strncmp(query, \"ID=\", 3) != 0) return 0;\n"),
    ("web_route_id_0_goes_on", T, F, TICKET_QUERY, "\t\tquery_fits = decision.ticket != 0 || strcmp(query, \"id=0\") == 0;\n"),
    ("web_route_id_read_on_every_get", T, F, "\tif(entry->route == WEB_ROUTE_TICKET)\n", "\tif(entry->method == WEB_GET)\n"),
    ("web_route_ticket_number_not_read", T, F, TICKET_NUMBER, TICKET_NUMBER.replace("ticket_number(query)", "ticket_number(\"id=1\")")),
    ("web_route_ticket_number_not_reported", T, F, TICKET_QUERY, TICKET_QUERY + "\t\tdecision.ticket = decision.ticket != 0 ? 1 : 0;\n"),
    ("web_route_ticket_without_id_goes_on", T, F, TICKET_QUERY, "\t\tquery_fits = true;\n"),
    ("web_route_ticket_changes", T, F, TICKET_QUERY, TICKET_QUERY + "\t\tdecision.changes = true;\n"),
    ("web_route_ticket_asks_for_the_knob", T, F, TICKET_QUERY, TICKET_QUERY + "\t\tdecision.knob = true;\n"),
    ("web_route_ticket_only_while_the_release_is_open", T, F, TICKET_QUERY, "\t\tquery_fits = decision.ticket != 0 && request->release_open;\n"),
    ("web_route_id_of_ten_digits_with_zeros_in_front_refused", T, F, ID_END,
     ID_END.replace("number > UINT32_MAX", "number > UINT32_MAX || (digits == 10 && number < 1000000000)")),

    # Content-Length
    ("web_route_length_not_needed", T, F, NO_LENGTH, ""),
    ("web_route_length_needed_for_get", T, F, BODY_OPEN + NO_LENGTH, "\tif(!request->has_length) return refused(411, \"length\");\n" + BODY_OPEN),
    ("web_route_length_status_changed", T, F, NO_LENGTH, NO_LENGTH.replace("411", "400")),
    ("web_route_length_word_changed", T, F, NO_LENGTH, NO_LENGTH.replace("\"length\"", "\"lenght\"")),
    ("web_route_length_not_needed_for_put", T, F, BODY_OPEN, "\tif(entry->method == WEB_POST)\n\t{\n"),
    ("web_route_length_not_needed_for_the_reboot", T, F, NO_LENGTH,
     NO_LENGTH.replace("!request->has_length", "!request->has_length && entry->route != WEB_ROUTE_REBOOT")),
    ("web_route_length_limit_for_get", T, F, BODY, NO_LENGTH.replace("\t\tif(!request", "\tif(entry->method != WEB_GET && !request") + TOO_LARGE[1:]),
    ("web_route_length_only_limited_for_the_firmware", T, F, TOO_LARGE,
     TOO_LARGE.replace("request->length > limit", "(entry->route == WEB_ROUTE_OTA && request->length > limit)")),
    ("web_route_length_not_limited_for_the_firmware", T, F, TOO_LARGE,
     TOO_LARGE.replace("request->length > limit", "(entry->route != WEB_ROUTE_OTA && request->length > limit)")),
    ("web_route_length_not_limited_for_the_layout", T, F, TOO_LARGE,
     TOO_LARGE.replace("request->length > limit", "(entry->method != WEB_PUT && request->length > limit)")),
    ("web_route_length_only_limited_for_the_layout", T, F, TOO_LARGE,
     TOO_LARGE.replace("request->length > limit", "(entry->method == WEB_PUT && request->length > limit)")),
    ("web_route_length_at_the_limit_refused", T, F, TOO_LARGE, TOO_LARGE.replace("request->length > limit", "request->length >= limit")),
    ("web_route_length_checked_before_it_is_known", T, F, NO_LENGTH + TOO_LARGE, TOO_LARGE + NO_LENGTH),
    ("web_route_empty_firmware_goes_on", T, F, TOO_LARGE, TOO_LARGE.replace(" || (entry->route == WEB_ROUTE_OTA && request->length == 0)", "")),
    ("web_route_empty_body_refused", T, F, TOO_LARGE, TOO_LARGE.replace("(entry->route == WEB_ROUTE_OTA && request->length == 0)", "request->length == 0")),
    ("web_route_too_large_status_changed", T, F, TOO_LARGE, TOO_LARGE.replace("413", "400")),
    ("web_route_too_large_word_changed", T, F, TOO_LARGE, TOO_LARGE.replace("\"too_large\"", "\"too large\"")),
    ("web_route_firmware_of_1_byte_refused", T, F, TOO_LARGE, TOO_LARGE.replace("request->length == 0", "request->length <= 1")),
    ("web_route_empty_firmware_goes_on_while_busy", T, F, TOO_LARGE, TOO_LARGE.replace("request->length == 0", "request->length == 0 && !request->busy")),
    ("web_route_empty_factory_reset_refused", T, F, TOO_LARGE,
     TOO_LARGE.replace("entry->route == WEB_ROUTE_OTA && request->length == 0", "entry->knob && entry->rests && request->length == 0")),
    ("web_route_length_compared_with_sign", T, F, TOO_LARGE, TOO_LARGE.replace("request->length > limit", "(int32_t)request->length > (int32_t)limit")),
    ("web_route_layout_limit_is_the_small_one", T, F, LAYOUT_LIMIT, ""),
    ("web_route_layout_limit_for_every_route", T, F, "\tuint32_t limit = WEB_BODY_SMALL_MAX;\n", "\tuint32_t limit = WEB_BODY_LAYOUT_MAX;\n"),
    ("web_route_firmware_limit_is_the_small_one", T, F, OTA_LIMIT, ""),
    ("web_route_firmware_limit_for_every_route", T, F, OTA_LIMIT, "\tif(entry->method == WEB_POST) limit = request->slot_size;\n"),
    ("web_route_firmware_limit_1_less", T, F, OTA_LIMIT, OTA_LIMIT.replace("request->slot_size", "request->slot_size - 1")),
    ("web_route_firmware_limit_never_above_the_small_one", T, F, OTA_LIMIT,
     "\tif(entry->route == WEB_ROUTE_OTA && request->slot_size < limit) limit = request->slot_size;\n"),
    ("web_route_firmware_limit_never_below_the_small_one", T, F, OTA_LIMIT,
     "\tif(entry->route == WEB_ROUTE_OTA && request->slot_size > limit) limit = request->slot_size;\n"),
    ("web_route_layout_limit_not_for_the_check", T, F, LAYOUT_LIMIT, "\t\tif(decision.route != WEB_ROUTE_LAYOUT_CHECK) limit = WEB_BODY_LAYOUT_MAX;\n"),
    ("web_route_layout_limit_not_for_the_apply", T, F, LAYOUT_LIMIT, "\t\tif(decision.route != WEB_ROUTE_LAYOUT_APPLY) limit = WEB_BODY_LAYOUT_MAX;\n"),
    ("web_route_layout_limit_for_the_layout_reset", T, F, OTA_LIMIT, OTA_LIMIT + "\tif(entry->route == WEB_ROUTE_LAYOUT_RESET) limit = WEB_BODY_LAYOUT_MAX;\n"),
    ("web_route_wifi_limit_doubled", T, F, OTA_LIMIT, OTA_LIMIT + "\tif(entry->route == WEB_ROUTE_WIFI_STORE) limit = WEB_BODY_SMALL_MAX * 2;\n"),
    ("web_route_small_limit_1_more", T, H, "#define WEB_BODY_SMALL_MAX  512u", "#define WEB_BODY_SMALL_MAX  513u"),
    ("web_route_small_limit_1_less", T, H, "#define WEB_BODY_SMALL_MAX  512u", "#define WEB_BODY_SMALL_MAX  511u"),
    ("web_route_layout_limit_1_more", T, H, "#define WEB_BODY_LAYOUT_MAX 16384u", "#define WEB_BODY_LAYOUT_MAX 16385u"),
    ("web_route_layout_limit_1_less", T, H, "#define WEB_BODY_LAYOUT_MAX 16384u", "#define WEB_BODY_LAYOUT_MAX 16383u"),
    ("web_route_busy_checked_before_the_length", T, F, BODY + BUSY, BUSY + BODY),

    # the refusal itself
    ("web_route_refusal_names_a_route", T, F, REFUSAL, REFUSAL.replace("WEB_ROUTE_NONE", "WEB_ROUTE_PAGE")),
    ("web_route_refusal_without_status", T, F, REFUSAL, REFUSAL.replace("status, error", "status != 0 ? 0 : status, error")),
    ("web_route_refusal_without_word", T, F, REFUSAL, REFUSAL.replace("status, error", "status, error == NULL ? error : NULL")),
    ("web_route_refusal_says_it_changes", T, F, REFUSAL, REFUSAL.replace("false, false, 0", "true, false, 0")),
    ("web_route_refusal_asks_for_the_knob", T, F, REFUSAL, REFUSAL.replace("false, false, 0", "false, true, 0")),
    ("web_route_refusal_carries_a_ticket", T, F, REFUSAL, REFUSAL.replace("false, false, 0", "false, false, 1")),
    ("web_route_request_that_goes_on_carries_a_ticket", T, F, GOES_ON, GOES_ON.replace("false, false, 0}", "false, false, 1}")),
    ("web_route_request_that_goes_on_carries_a_status", T, F, GOES_ON, GOES_ON.replace("WEB_ROUTE_NONE, 0, NULL", "WEB_ROUTE_NONE, 200, NULL")),
    ("web_route_request_that_goes_on_carries_a_word", T, F, GOES_ON, GOES_ON.replace("0, NULL", "0, \"\"")),

    # web_host_allowed: the address
    ("web_host_null_read", T, F, HOST_NULL, ""),
    ("web_host_user_in_front_skipped", T, F, HOST_NULL, HOST_NULL + "\tif(strchr(host, '@') != NULL) host = strchr(host, '@') + 1;\n"),
    ("web_host_blanks_in_front_skipped", T, F, HOST_NULL, HOST_NULL + "\twhile(*host == ' ') host++;\n"),
    ("web_host_ipv6_loopback_allowed", T, F, HOST_NULL, HOST_NULL + "\tif(strcmp(host, \"[::1]\") == 0) return true;\n"),
    ("web_host_256_allowed", T, F, TOO_BIG, TOO_BIG.replace("> 255", "> 256")),
    ("web_host_255_refused", T, F, TOO_BIG, TOO_BIG.replace("> 255", "> 254")),
    ("web_host_number_of_any_size", T, F, TOO_BIG, "\t\tif(text - start > 3) return NULL;\n"),
    ("web_host_zeros_in_front_allowed", T, F, ZERO_IN_FRONT, ""),
    ("web_host_single_zero_refused", T, F, ANY_DIGIT, "\treturn text > start && number > 0 ? text : NULL;\n"),
    ("web_host_empty_number_allowed", T, F, ANY_DIGIT, "\treturn text;\n"),
    ("web_host_three_numbers", T, F, FOUR, FOUR.replace("i < 4", "i < 3")),
    ("web_host_five_numbers", T, F, FOUR, FOUR.replace("i < 4", "i < 5")),
    ("web_host_any_byte_between_the_numbers", T, F, DOT, "\t\tif(i > 0 && *text++ == '\\0') return NULL;\n"),
    ("web_host_comma_between_the_numbers", T, F, DOT, "\t\tif(i > 0 && *text != '.' && *text != ',') return NULL;\n\t\tif(i > 0) text++;\n"),
    ("web_host_colon_is_a_digit", T, F, NUMBER_LOOP, NUMBER_LOOP.replace("<= '9'", "<= ':'")),
    ("web_host_slash_is_a_digit", T, F, NUMBER_LOOP, NUMBER_LOOP.replace(">= '0'", ">= '/'")),
    ("web_host_0_is_no_digit", T, F, NUMBER_LOOP, NUMBER_LOOP.replace(">= '0'", ">= '1'")),
    ("web_host_9_is_no_digit", T, F, NUMBER_LOOP, NUMBER_LOOP.replace("<= '9'", "<= '8'")),
    ("web_host_numbers_hexadecimal", T, F, NUMBER_ADD, NUMBER_ADD.replace("* 10", "* 16")),
    ("web_host_numbers_octal", T, F, NUMBER_ADD, NUMBER_ADD.replace("* 10", "* 8")),
    ("web_host_two_zeros_allowed", T, F, ZERO_IN_FRONT, "\t\tif(text > start + 1 && number == 0) return NULL;\n"),
    ("web_host_zero_in_front_of_the_last_digit_allowed", T, F, ZERO_IN_FRONT, "\t\tif(text > start && number == 0 && text[1] != '\\0') return NULL;\n"),
    ("web_host_255_refused_as_the_last_number", T, F, TOO_BIG, "\t\tif(number > 255 || (number > 254 && *text == '\\0')) return NULL;\n"),
    ("web_host_first_number_may_not_begin_with_0", T, F, DOT, DOT + "\t\tif(i == 0 && *text == '0') return NULL;\n"),
    ("web_host_dot_in_front_allowed", T, F, DOT, DOT + "\t\tif(i == 0 && *text == '.') text++;\n"),
    ("web_host_dot_behind_the_address_allowed", T, F, HOST_END, "\tif(rest != NULL && *rest == '.' && rest[1] == '\\0') return true;\n" + HOST_END),
    ("web_host_second_address_behind_a_dot_allowed", T, F, HOST_END,
     "\tif(rest != NULL && *rest == '.' && behind_address(rest + 1) != NULL) return true;\n" + HOST_END),
    ("web_host_numbers_counted_in_8_bit", T, F, NUMBER_ADD, "\t\tnumber = (number * 10 + (*text++ - '0')) & 0xFF;\n"),
    ("web_host_no_dot_needed_behind_the_first_number", T, F, DOT, "\t\tif(i > 1 && *text++ != '.') return NULL;\n"),
    ("web_host_dot_not_passed", T, F, DOT, "\t\tif(i > 0 && *text != '.') return NULL;\n"),
    ("web_host_address_not_allowed", T, F, HOST_ADDRESS, HOST_ADDRESS.replace("rest == NULL", "rest == NULL && host[0] == '\\0'")),

    # web_host_allowed: the name
    ("web_host_name_not_allowed", T, F, HOST_NAME, "\trest = host[0] == '\\0' ? behind_name(host) : NULL;\n"),
    ("web_host_name_without_local", T, F, NAME, "\tstatic const char name[] = WEB_HOST_NAME;\n"),
    ("web_host_name_in_another_domain", T, F, NAME, "\tstatic const char name[] = WEB_HOST_NAME \".lan\";\n"),
    ("web_host_name_written_with_upper_case", T, F, NAME, "\tstatic const char name[] = WEB_HOST_NAME \".Local\";\n"),
    ("web_host_name_changed", T, H, "#define WEB_HOST_NAME       \"wican-display\"", "#define WEB_HOST_NAME       \"wican_display\""),
    ("web_host_name_only_in_lower_case", T, F, FOLD, ""),
    ("web_host_name_case_folded_by_a_bit", T, F, FOLD, "\t\tc = (char)(c | 0x20);\n"),
    ("web_host_name_highest_bit_ignored", T, F, FOLD, "\t\tc = (char)(c & 0x7F);\n" + FOLD),
    ("web_host_name_only_first_letter_folded", T, F, FOLD, FOLD.replace("if(c >= 'A'", "if(i == 0 && c >= 'A'")),
    ("web_host_name_last_byte_not_compared", T, F,
     "\tfor(size_t i = 0; i < sizeof(name) - 1; i++)\n\t{\n\t\tchar c = text[i];", "\tfor(size_t i = 0; i < sizeof(name) - 2; i++)\n\t{\n\t\tchar c = text[i];"),
    ("web_host_name_first_byte_not_compared", T, F, NAME_COMPARED, "\t\tif(c != name[i] && i > 0) return NULL;\n"),
    ("web_host_name_last_byte_may_be_any", T, F, NAME_COMPARED, "\t\tif(c != name[i] && i < sizeof(name) - 2) return NULL;\n"),
    ("web_host_name_with_underscore", T, F, NAME_COMPARED, "\t\tif(c != name[i] && !(c == '_' && name[i] == '-')) return NULL;\n"),
    ("web_host_name_upper_a_not_folded", T, F, FOLD, FOLD.replace("c >= 'A'", "c > 'A'")),
    ("web_host_name_upper_y_not_folded", T, F, FOLD, FOLD.replace("c <= 'Z'", "c < 'Y'")),
    ("web_host_name_end_one_byte_early", T, F, NAME_END, "\treturn text + sizeof(name) - 2;\n"),
    ("web_host_name_with_dot_behind", T, F, NAME_END, "\treturn text[sizeof(name) - 1] == '.' ? text + sizeof(name) : text + sizeof(name) - 1;\n"),
    ("web_host_name_without_local_allowed_too", T, F, HOST_NAME, HOST_NAME + "\tif(rest == NULL && strcmp(host, WEB_HOST_NAME) == 0) return true;\n"),
    ("web_host_localhost_allowed", T, F, HOST_NAME, HOST_NAME + "\tif(rest == NULL && strcmp(host, \"localhost\") == 0) return true;\n"),

    # web_host_allowed: what follows
    ("web_host_anything_may_follow", T, F, HOST_END, "\treturn rest != NULL && (is_port_or_end(rest) || rest[0] != '\\0');\n"),
    ("web_host_any_byte_for_the_colon", T, F, PORT_COLON, ""),
    ("web_host_port_needed", T, F, "\tif(*text == '\\0') return true;\n\tif(*text != ':') return false;\n", "\tif(*text != ':') return false;\n"),
    ("web_host_port_not_allowed", T, F, PORT_COLON, "\treturn false;\n"),
    ("web_host_port_of_6_digits", T, F, PORT_DIGITS, PORT_DIGITS.replace("> 5", "> 6")),
    ("web_host_port_of_4_digits_at_most", T, F, PORT_DIGITS, PORT_DIGITS.replace("> 5", "> 4")),
    ("web_host_port_digits_not_counted", T, F, PORT_DIGITS, "\t\tdigits++;\n"),
    ("web_host_port_may_be_empty", T, F, PORT_END, "\treturn *text == '\\0';\n"),
    ("web_host_port_may_go_on", T, F, PORT_END, "\treturn digits > 0;\n"),
    ("web_host_port_colon_is_a_digit", T, F, PORT_LOOP, PORT_LOOP.replace("<= '9'", "<= ':'")),
    ("web_host_port_slash_is_a_digit", T, F, PORT_LOOP, PORT_LOOP.replace(">= '0'", ">= '/'")),
    ("web_host_port_0_is_no_digit", T, F, PORT_LOOP, PORT_LOOP.replace(">= '0'", ">= '1'")),
    ("web_host_port_9_is_no_digit", T, F, PORT_LOOP, PORT_LOOP.replace("<= '9'", "<= '8'")),
    ("web_host_port_of_2_digits_at_least", T, F, PORT_END, "\treturn *text == '\\0' && digits > 1;\n"),
    ("web_host_port_only_behind_an_address", T, F, HOST_END, "\treturn rest != NULL && is_port_or_end(rest) && (*rest == '\\0' || host[0] <= '9');\n"),
    ("web_host_port_only_behind_the_name", T, F, HOST_END, "\treturn rest != NULL && is_port_or_end(rest) && (*rest == '\\0' || host[0] > '9');\n"),

    # web_error_body
    ("web_error_null_word_read", T, F, WORD_NULL, "\tif(size != SIZE_MAX)\n"),
    ("web_error_null_word_has_length_0", T, F, "\tint length = -1;\n", "\tint length = 0;\n"),
    ("web_error_no_hint_for_locked", T, F, HINT, "strcmp(error, \"locked\") == 0 && size == 0 ? "),
    ("web_error_hint_for_every_word", T, F, HINT, "error != NULL ? "),
    ("web_error_hint_for_longer_words", T, F, HINT, "strncmp(error, \"locked\", 6) == 0 ? "),
    ("web_error_hint_for_every_word_with_l", T, F, HINT, "error[0] == 'l' ? "),
    ("web_error_hint_for_words_that_contain_locked", T, F, HINT, "strstr(error, \"locked\") != NULL ? "),
    ("web_error_hint_for_locked_in_upper_case", T, F, HINT, "(strcmp(error, \"locked\") == 0 || strcmp(error, \"Locked\") == 0) ? "),
    ("web_error_hint_without_umlaut", T, F, "Am Display: Menü > Web-Zugriff freigeben", "Am Display: Menue > Web-Zugriff freigeben"),
    ("web_error_hint_in_latin_1", T, F, "Am Display: Menü > Web-Zugriff freigeben", "Am Display: Men\\374 > Web-Zugriff freigeben"),
    ("web_error_member_renamed", T, F, "\"{\\\"error\\\":\\\"%s\\\"%s}\"", "\"{\\\"err\\\":\\\"%s\\\"%s}\""),
    ("web_error_word_is_the_format", T, F, "snprintf(out, size, \"{\\\"error\\\":\\\"%s\\\"%s}\", error,", "snprintf(out, size, error, error,"),
    ("web_error_full_buffer_not_zero_terminated_text", T, F, FITS, FITS.replace("(size_t)length >= size", "(size_t)length > size")),
    ("web_error_one_byte_wasted", T, F, FITS, FITS.replace("(size_t)length >= size", "(size_t)length + 1 >= size")),
    ("web_error_written_one_byte_behind_the_size", T, F, "snprintf(out, size, ", "snprintf(out, size + 1, "),
    ("web_error_written_without_the_size", T, F, "snprintf(out, size, ", "snprintf(out, size - 1, "),
    ("web_error_cut_text_left", T, F, EMPTY, ""),
    ("web_error_written_with_size_0", T, F, EMPTY, "\t\tout[0] = '\\0';\n"),
    ("web_error_null_word_leaves_a_buffer_of_1_byte", T, F, EMPTY, "\t\tif(size > 1) out[0] = '\\0';\n"),
    ("web_error_95_bytes_never_enough", T, F, FITS, FITS.replace("(size_t)length >= size", "(size_t)length >= size || size == WEB_ERROR_SIZE - 1")),
    ("web_error_too_small_not_reported", T, F, EMPTY + "\t\treturn -1;\n", EMPTY + "\t\treturn length;\n"),
    ("web_error_length_with_the_zero", T, F, "\t}\n\treturn length;\n}", "\t}\n\treturn length + 1;\n}"),
    ("web_error_line_feed_behind_the_body", T, F, "\"{\\\"error\\\":\\\"%s\\\"%s}\"", "\"{\\\"error\\\":\\\"%s\\\"%s}\\n\""),

    # the numbers of the header
    ("web_query_max_64", T, H, "#define WEB_QUERY_MAX       63 ", "#define WEB_QUERY_MAX       64 "),
    ("web_error_size_95", T, H, "#define WEB_ERROR_SIZE      96", "#define WEB_ERROR_SIZE      95"),
    ("web_error_size_97", T, H, "#define WEB_ERROR_SIZE      96", "#define WEB_ERROR_SIZE      97"),
    ("web_header_name_changed", T, H, "#define WEB_HEADER_NAME     \"X-Display\"", "#define WEB_HEADER_NAME     \"X-display\""),
    ("web_query_max_62", T, H, "#define WEB_QUERY_MAX       63 ", "#define WEB_QUERY_MAX       62 "),

    # A text read behind its zero, a pointer that is NULL, a loop that never ends. The test makes every call
    # of the module in a child process, so each of these fails a check instead of ending the test.
    ("web_route_id_zero_is_a_digit", T, F, ID_LOOP, ID_LOOP.replace("*query >= '0' && *query <= '9'", "*query <= '9'")),
    ("web_host_port_zero_is_a_digit", T, F, PORT_LOOP, PORT_LOOP.replace("*text >= '0' && *text <= '9'", "*text <= '9'")),
    ("web_host_number_never_ends", T, F, NUMBER_ADD, ""),
    ("web_host_missing_number_not_noticed", T, F, "\t\ttext = behind_number(text);\n\t\tif(text == NULL) return NULL;\n", "\t\ttext = behind_number(text);\n"),
    ("web_host_refused_host_read_on", T, F, HOST_END, "\treturn rest != NULL || is_port_or_end(rest);\n"),
    ("web_host_name_end_one_byte_late", T, F, NAME_END, "\treturn text + sizeof(name);\n"),
    ("web_host_name_end_two_bytes_late", T, F, NAME_END, "\treturn text + sizeof(name) + 1;\n"),
    ("web_host_name_zero_compared_too", T, F,
     "\tfor(size_t i = 0; i < sizeof(name) - 1; i++)\n\t{\n\t\tchar c = text[i];", "\tfor(size_t i = 0; i < sizeof(name); i++)\n\t{\n\t\tchar c = text[i];"),
    ("web_route_method_without_a_line_goes_on", T, F, NOT_ALLOWED, ""),

    # found by the hunt for survivors: what holds for every route and must not be dropped for one
    ("web_route_host_not_checked_with_the_header", T, F, HOST,
     HOST.replace("if(!web_host_allowed(request->host))", "if(!web_host_allowed(request->host) && !(request->header != NULL && strcmp(request->header, \"1\") == 0))")),
    ("web_route_host_not_checked_for_put", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->method != WEB_PUT && !web_host_allowed")),
    ("web_route_host_not_checked_without_length", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(request->has_length && !web_host_allowed")),
    ("web_route_host_not_checked_for_the_info", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_INFO && !web_host_allowed")),
    ("web_route_host_not_checked_for_the_catalog", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_CATALOG && !web_host_allowed")),
    ("web_route_host_not_checked_for_the_values", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_VALUES && !web_host_allowed")),
    ("web_route_host_not_checked_for_the_layout", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_LAYOUT && !web_host_allowed")),
    ("web_route_host_not_checked_for_the_fault_memory", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_DTC_LAST && !web_host_allowed")),
    ("web_route_host_not_checked_for_the_wifi_list", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_WIFI && !web_host_allowed")),
    ("web_route_host_not_checked_for_the_factory_reset", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_RESET && !web_host_allowed")),
    ("web_route_host_not_checked_for_the_wifi_store", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_WIFI_STORE && !web_host_allowed")),
    ("web_route_host_not_checked_for_the_reboot", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_REBOOT && !web_host_allowed")),
    ("web_route_empty_host_passes", T, F, HOST,
     HOST.replace("if(!web_host_allowed(request->host))", "if(request->host != NULL && request->host[0] != '\\0' && !web_host_allowed(request->host))")),
    ("web_route_header_not_needed_with_the_name_of_the_display", T, F, HEADER,
     HEADER.replace("entry->method != WEB_GET &&", "entry->method != WEB_GET && request->host[0] <= '9' &&")),
    ("web_route_header_not_needed_for_an_empty_body", T, F, HEADER, HEADER.replace("entry->method != WEB_GET &&", "entry->method != WEB_GET && request->length > 0 &&")),
    ("web_route_header_not_needed_for_the_layout_check", T, F, HEADER,
     HEADER.replace("entry->method != WEB_GET &&", "entry->method != WEB_GET && strcmp(query, \"mode=check\") != 0 &&")),
    ("web_route_header_not_needed_while_busy", T, F, HEADER, HEADER.replace("entry->method != WEB_GET &&", "entry->method != WEB_GET && !request->busy &&")),
    ("web_route_header_not_needed_for_the_reboot", T, F, HEADER,
     HEADER.replace("entry->method != WEB_GET &&", "entry->method != WEB_GET && entry->route != WEB_ROUTE_REBOOT &&")),
    ("web_route_header_not_needed_for_the_firmware", T, F, HEADER,
     HEADER.replace("entry->method != WEB_GET &&", "entry->method != WEB_GET && entry->route != WEB_ROUTE_OTA &&")),
    ("web_route_header_not_needed_for_forgetting_a_network", T, F, HEADER,
     HEADER.replace("entry->method != WEB_GET &&", "entry->method != WEB_GET && entry->route != WEB_ROUTE_WIFI_FORGET &&")),
    ("web_route_header_with_a_blank_behind_passes", T, F, HEADER,
     HEADER.replace("strcmp(request->header, \"1\") != 0", "(strcmp(request->header, \"1\") != 0 && strcmp(request->header, \"1 \") != 0)")),
    ("web_route_release_not_for_the_firmware", T, F, LOCKED, LOCKED.replace("decision.changes &&", "decision.changes && entry->route != WEB_ROUTE_OTA &&")),
    ("web_route_release_not_for_the_factory_reset", T, F, LOCKED, LOCKED.replace("decision.changes &&", "decision.changes && entry->route != WEB_ROUTE_RESET &&")),
    ("web_route_release_not_for_the_wifi_store", T, F, LOCKED, LOCKED.replace("decision.changes &&", "decision.changes && entry->route != WEB_ROUTE_WIFI_STORE &&")),
    ("web_route_release_not_for_the_layout_save", T, F, LOCKED,
     LOCKED.replace("decision.changes &&", "decision.changes && decision.route != WEB_ROUTE_LAYOUT_SAVE &&")),
    ("web_route_release_not_for_the_layout_apply", T, F, LOCKED,
     LOCKED.replace("decision.changes &&", "decision.changes && decision.route != WEB_ROUTE_LAYOUT_APPLY &&")),
    ("web_route_release_not_checked_with_the_name_of_the_display", T, F, LOCKED,
     LOCKED.replace("decision.changes &&", "decision.changes && request->host[0] <= '9' &&")),
    ("web_route_release_not_checked_without_length", T, F, LOCKED, LOCKED.replace("decision.changes &&", "decision.changes && request->has_length &&")),
    ("web_route_release_needed_for_the_layout_check", T, F, LOCKED,
     LOCKED.replace("decision.changes &&", "(decision.changes || decision.route == WEB_ROUTE_LAYOUT_CHECK) &&")),
    ("web_route_release_needed_for_the_info", T, F, LOCKED, LOCKED.replace("decision.changes &&", "(decision.changes || decision.route == WEB_ROUTE_INFO) &&")),
    ("web_route_release_needed_for_the_page", T, F, LOCKED, LOCKED.replace("decision.changes &&", "(decision.changes || decision.route == WEB_ROUTE_PAGE) &&")),
    ("web_route_reboot_changes_nothing", T, F, CHANGES, "\tdecision.changes = entry->method != WEB_GET && entry->route != WEB_ROUTE_REBOOT;\n"),
    ("web_route_query_not_checked_for_the_firmware", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->route != WEB_ROUTE_OTA")),
    ("web_route_query_not_checked_for_the_page", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->route != WEB_ROUTE_PAGE")),
    ("web_route_query_not_checked_for_the_factory_reset", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->route != WEB_ROUTE_RESET")),
    ("web_route_query_not_checked_for_the_layout_reset", T, F, BAD_QUERY,
     BAD_QUERY.replace("!query_fits", "!query_fits && entry->route != WEB_ROUTE_LAYOUT_RESET")),
    ("web_route_query_of_one_byte_passes", T, F, QUERY_FITS, "\tbool query_fits = query[0] == '\\0' || query[1] == '\\0';\n"),
    ("web_route_query_longer_than_the_limit_passes", T, F, QUERY_FITS, "\tbool query_fits = query[0] == '\\0' || strlen(query) > WEB_QUERY_MAX;\n"),
    ("web_route_query_of_63_bytes_passes", T, F, QUERY_FITS, "\tbool query_fits = query[0] == '\\0' || strlen(query) == WEB_QUERY_MAX;\n"),
    ("web_route_mode_check_is_save", T, F, MODE_CHECK, MODE_CHECK.replace("WEB_ROUTE_LAYOUT_CHECK", "WEB_ROUTE_LAYOUT_SAVE")),
    ("web_route_mode_save_is_apply", T, F, MODE_SAVE, MODE_SAVE.replace("WEB_ROUTE_LAYOUT_SAVE", "WEB_ROUTE_LAYOUT_APPLY")),
    ("web_route_mode_check_unknown", T, F, MODE_CHECK, ""),
    ("web_route_mode_apply_unknown", T, F, MODE_APPLY, ""),
    ("web_route_mode_check_with_an_ampersand_behind", T, F, MODE_CHECK,
     MODE_CHECK.replace("strcmp(query, \"mode=check\") == 0", "(strcmp(query, \"mode=check\") == 0 || strcmp(query, \"mode=check&\") == 0)")),
    ("web_route_id_with_a_plus_sign", T, F, ID_LOOP, "\tif(query[3] == '+') query++;\n" + ID_LOOP),
    ("web_route_id_with_blanks_in_front", T, F, ID_LOOP, "\twhile(query[3] == ' ') query++;\n" + ID_LOOP),
    ("web_route_id_with_an_ampersand_behind", T, F, ID_END, ID_END.replace("*query != '\\0'", "(*query != '\\0' && strcmp(query, \"&\") != 0)")),
    ("web_route_id_above_32_bit_is_the_largest", T, F, ID_END, "\tif(*query != '\\0') return 0;\n\tif(number > UINT32_MAX) number = UINT32_MAX;\n"),
    ("web_route_id_zeros_in_front_not_counted", T, F, ID_DIGITS, "\t\tif((number != 0 || *query != '0') && ++digits > 10) return 0;\n"),
    ("web_route_id_name_with_upper_i", T, F, ID_NAME,
     "\tif((query[0] != 'i' && query[0] != 'I') || strncmp(query + 1, \"d=\", 2) != 0) return 0;\n"),
    ("web_route_id_1_refused", T, F, TICKET_QUERY, "\t\tquery_fits = decision.ticket > 1;\n"),
    ("web_route_id_largest_reported_one_less", T, F, ID_RETURN, "\treturn number == UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)number;\n"),
    ("web_route_layout_carries_a_ticket", T, F, LAYOUT_LIMIT, LAYOUT_LIMIT + "\t\tdecision.ticket = 1;\n"),
    ("web_route_length_not_needed_for_the_firmware", T, F, NO_LENGTH,
     NO_LENGTH.replace("!request->has_length", "!request->has_length && entry->route != WEB_ROUTE_OTA")),
    ("web_route_length_not_needed_for_the_wifi_store", T, F, NO_LENGTH,
     NO_LENGTH.replace("!request->has_length", "!request->has_length && entry->route != WEB_ROUTE_WIFI_STORE")),
    ("web_route_length_taken_from_the_field_without_the_header", T, F, NO_LENGTH,
     NO_LENGTH.replace("!request->has_length", "!request->has_length && request->length == 0")),
    ("web_route_length_limit_wraps_at_2_32", T, F, TOO_LARGE, TOO_LARGE.replace("request->length > limit", "request->length >= limit + 1")),
    ("web_route_firmware_as_large_as_the_slot_refused", T, F, TOO_LARGE,
     TOO_LARGE.replace("request->length > limit", "(request->length > limit || (entry->route == WEB_ROUTE_OTA && request->length == limit))")),
    ("web_route_firmware_limit_for_the_factory_reset", T, F, OTA_LIMIT,
     "\tif(entry->route == WEB_ROUTE_OTA || entry->route == WEB_ROUTE_RESET) limit = request->slot_size;\n"),
    ("web_route_length_not_limited_for_the_reboot", T, F, TOO_LARGE,
     TOO_LARGE.replace("request->length > limit", "(entry->route != WEB_ROUTE_REBOOT && request->length > limit)")),
    ("web_route_length_not_limited_for_forgetting_a_network", T, F, TOO_LARGE,
     TOO_LARGE.replace("request->length > limit", "(entry->route != WEB_ROUTE_WIFI_FORGET && request->length > limit)")),
    ("web_route_layout_limit_only_for_the_save", T, F, LAYOUT_LIMIT, "\t\tif(decision.route == WEB_ROUTE_LAYOUT_SAVE) limit = WEB_BODY_LAYOUT_MAX;\n"),
    ("web_route_empty_layout_refused", T, F, TOO_LARGE,
     TOO_LARGE.replace("entry->route == WEB_ROUTE_OTA && request->length == 0", "(entry->route == WEB_ROUTE_OTA || entry->method == WEB_PUT) && request->length == 0")),
    ("web_route_busy_refuses_only_the_firmware", T, F, BUSY, BUSY.replace("entry->rests", "entry->route == WEB_ROUTE_OTA")),
    ("web_route_busy_does_not_refuse_the_firmware", T, F, BUSY, BUSY.replace("entry->rests", "entry->rests && entry->route != WEB_ROUTE_OTA")),
    ("web_route_layout_put_refused_while_busy", T, F, LAYOUT_PUT, row("WEB_PUT", "/api/layout", "WEB_ROUTE_LAYOUT_SAVE", "false", "true")),
    ("web_route_layout_reset_refused_while_busy", T, F, LAYOUT_RESET, row("WEB_POST", "/api/layout/reset", "WEB_ROUTE_LAYOUT_RESET", "false", "true")),
    ("web_route_forget_refused_while_busy", T, F, WIFI_FORGET, row("WEB_POST", "/api/wifi/forget", "WEB_ROUTE_WIFI_FORGET", "false", "true")),
    ("web_route_page_refused_while_busy", T, F, PAGE, row("WEB_GET", "/", "WEB_ROUTE_PAGE", "false", "true")),
    ("web_route_ticket_refused_while_busy", T, F, TICKET, row("WEB_GET", "/api/ticket", "WEB_ROUTE_TICKET", "false", "true")),
    ("web_route_layout_reset_with_knob", T, F, LAYOUT_RESET, row("WEB_POST", "/api/layout/reset", "WEB_ROUTE_LAYOUT_RESET", "true")),
    ("web_route_wifi_list_with_knob", T, F, WIFI, row("WEB_GET", "/api/wifi", "WEB_ROUTE_WIFI", "true")),
    ("web_route_catalog_and_values_swapped", T, F, CATALOG + VALUES,
     row("WEB_GET", "/api/catalog", "WEB_ROUTE_VALUES") + row("WEB_GET", "/api/values", "WEB_ROUTE_CATALOG")),
    ("web_route_ticket_also_by_put", T, F, TICKET, TICKET + row("WEB_PUT", "/api/ticket", "WEB_ROUTE_TICKET")),
    ("web_route_ota_also_in_upper_case", T, F, OTA, OTA + row("WEB_POST", "/api/OTA", "WEB_ROUTE_OTA", "true", "true")),
    ("web_route_layout_reset_also_by_post_to_the_layout", T, F, LAYOUT_RESET, LAYOUT_RESET + row("WEB_POST", "/api/layout", "WEB_ROUTE_LAYOUT_RESET")),
    ("web_route_info_also_with_a_slash_behind", T, F, INFO, INFO + row("WEB_GET", "/api/info/", "WEB_ROUTE_INFO")),
    ("web_route_state_of_the_adapter_answered", T, F, INFO, INFO + row("WEB_GET", "/api/state", "WEB_ROUTE_INFO")),
    ("web_host_255_allowed_only_behind_three_digits", T, F, TOO_BIG, "\t\tif(number > 255 && text - start > 3) return NULL;\n"),
    ("web_host_259_allowed", T, F, TOO_BIG, TOO_BIG.replace("> 255", "> 259")),
    ("web_host_name_folded_from_the_hyphen_on", T, F, FOLD, FOLD.replace("c >= 'A'", "c >= '-'")),
    ("web_host_name_upper_w_not_folded", T, F, FOLD, FOLD.replace("c <= 'Z'", "c <= 'V'")),
    ("web_host_port_may_not_begin_with_0", T, F, PORT_END, "\treturn *text == '\\0' && digits > 0 && text[-digits] != '0';\n"),
    ("web_host_port_above_69999_refused", T, F, PORT_END, "\treturn *text == '\\0' && digits > 0 && (digits < 5 || text[-5] < '7');\n"),
    ("web_host_address_of_zeros_refused", T, F, HOST_END, "\treturn rest != NULL && is_port_or_end(rest) && strncmp(host, \"0.0.0.0\", 7) != 0;\n"),
    ("web_host_address_of_255_refused", T, F, HOST_END,
     "\treturn rest != NULL && is_port_or_end(rest) && strncmp(host, \"255.255.255.255\", 15) != 0;\n"),
    ("web_host_www_in_front_skipped", T, F, HOST_NULL, HOST_NULL + "\tif(strncmp(host, \"www.\", 4) == 0) host += 4;\n"),
    ("web_host_dot_behind_the_name_or_address_allowed", T, F, HOST_END,
     "\tif(rest != NULL && rest[0] == '.' && is_port_or_end(rest + 1)) return true;\n" + HOST_END),
    ("web_error_empty_word_has_no_body", T, F, WORD_NULL, "\tif(error != NULL && error[0] != '\\0')\n"),
    ("web_error_hint_only_into_a_large_buffer", T, F, HINT, "strcmp(error, \"locked\") == 0 && size >= WEB_ERROR_SIZE ? "),
    ("web_error_cut_text_reported_with_its_length", T, F, FITS, "\tif(length < 0)\n"),
    ("web_error_hint_with_another_arrow", T, F, "Am Display: Menü > Web-Zugriff freigeben", "Am Display: Menü -> Web-Zugriff freigeben"),

    # the same for the routes the mutations above leave out: every line of the list has its own checks
    ("web_route_busy_checked_before_the_size_of_the_body", T, F, NO_LENGTH + TOO_LARGE, NO_LENGTH + "\t" + BUSY + TOO_LARGE),
    ("web_route_layout_check_changes_while_the_release_is_open", T, F, LAYOUT_CHANGES,
     "\t\tdecision.changes = decision.route != WEB_ROUTE_LAYOUT_CHECK || request->release_open;\n"),
    ("web_route_host_not_checked_for_the_settings", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_SETTINGS && !web_host_allowed")),
    ("web_route_host_not_checked_for_forgetting_a_network", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_WIFI_FORGET && !web_host_allowed")),
    ("web_route_host_not_checked_for_the_layout_reset", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_LAYOUT_RESET && !web_host_allowed")),
    ("web_route_host_not_checked_for_the_layout_put", T, F, HOST, HOST.replace("if(!web_host_allowed", "if(entry->route != WEB_ROUTE_LAYOUT_SAVE && !web_host_allowed")),
    ("web_route_header_not_needed_for_the_settings", T, F, HEADER, HEADER.replace("entry->method != WEB_GET &&", "entry->method != WEB_GET && entry->route != WEB_ROUTE_SETTINGS &&")),
    ("web_route_header_not_needed_for_the_factory_reset", T, F, HEADER, HEADER.replace("entry->method != WEB_GET &&", "entry->method != WEB_GET && entry->route != WEB_ROUTE_RESET &&")),
    ("web_route_header_not_needed_for_the_wifi_store", T, F, HEADER, HEADER.replace("entry->method != WEB_GET &&", "entry->method != WEB_GET && entry->route != WEB_ROUTE_WIFI_STORE &&")),
    ("web_route_header_not_needed_for_the_layout_reset", T, F, HEADER, HEADER.replace("entry->method != WEB_GET &&", "entry->method != WEB_GET && entry->route != WEB_ROUTE_LAYOUT_RESET &&")),
    ("web_route_query_not_checked_for_the_settings", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->route != WEB_ROUTE_SETTINGS")),
    ("web_route_query_not_checked_for_the_reboot", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->route != WEB_ROUTE_REBOOT")),
    ("web_route_query_not_checked_for_forgetting_a_network", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->route != WEB_ROUTE_WIFI_FORGET")),
    ("web_route_query_not_checked_for_the_wifi_store", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->route != WEB_ROUTE_WIFI_STORE")),
    ("web_route_query_not_checked_for_the_info", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->route != WEB_ROUTE_INFO")),
    ("web_route_query_not_checked_for_the_catalog", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->route != WEB_ROUTE_CATALOG")),
    ("web_route_query_not_checked_for_the_values", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->route != WEB_ROUTE_VALUES")),
    ("web_route_query_not_checked_for_the_layout", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->route != WEB_ROUTE_LAYOUT")),
    ("web_route_query_not_checked_for_the_fault_memory", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->route != WEB_ROUTE_DTC_LAST")),
    ("web_route_query_not_checked_for_the_wifi_list", T, F, BAD_QUERY, BAD_QUERY.replace("!query_fits", "!query_fits && entry->route != WEB_ROUTE_WIFI")),
    ("web_route_length_not_needed_for_the_settings", T, F, NO_LENGTH, NO_LENGTH.replace("!request->has_length", "!request->has_length && entry->route != WEB_ROUTE_SETTINGS")),
    ("web_route_length_not_needed_for_the_factory_reset", T, F, NO_LENGTH, NO_LENGTH.replace("!request->has_length", "!request->has_length && entry->route != WEB_ROUTE_RESET")),
    ("web_route_length_not_needed_for_forgetting_a_network", T, F, NO_LENGTH, NO_LENGTH.replace("!request->has_length", "!request->has_length && entry->route != WEB_ROUTE_WIFI_FORGET")),
    ("web_route_length_not_needed_for_the_layout_reset", T, F, NO_LENGTH, NO_LENGTH.replace("!request->has_length", "!request->has_length && entry->route != WEB_ROUTE_LAYOUT_RESET")),
    ("web_route_length_not_needed_for_the_layout_check", T, F, NO_LENGTH,
     NO_LENGTH.replace("!request->has_length", "!request->has_length && decision.route != WEB_ROUTE_LAYOUT_CHECK")),
    ("web_route_length_not_needed_for_the_layout_apply", T, F, NO_LENGTH,
     NO_LENGTH.replace("!request->has_length", "!request->has_length && decision.route != WEB_ROUTE_LAYOUT_APPLY")),
    ("web_route_settings_of_513_bytes", T, F, OTA_LIMIT, OTA_LIMIT + "\tif(entry->route == WEB_ROUTE_SETTINGS) limit = WEB_BODY_SMALL_MAX + 1;\n"),
    ("web_route_factory_reset_of_513_bytes", T, F, OTA_LIMIT, OTA_LIMIT + "\tif(entry->route == WEB_ROUTE_RESET) limit = WEB_BODY_SMALL_MAX + 1;\n"),
    ("web_route_layout_reset_of_513_bytes", T, F, OTA_LIMIT, OTA_LIMIT + "\tif(entry->route == WEB_ROUTE_LAYOUT_RESET) limit = WEB_BODY_SMALL_MAX + 1;\n"),
    ("web_route_layout_of_16384_bytes_refused", T, F, TOO_LARGE,
     TOO_LARGE.replace("request->length > limit", "(request->length > limit || (entry->method == WEB_PUT && request->length == limit))")),
    ("web_route_busy_does_not_refuse_the_reboot", T, F, BUSY, BUSY.replace("entry->rests &&", "entry->rests && entry->route != WEB_ROUTE_REBOOT &&")),
    ("web_route_busy_does_not_refuse_the_factory_reset", T, F, BUSY, BUSY.replace("entry->rests &&", "entry->rests && entry->route != WEB_ROUTE_RESET &&")),
]
