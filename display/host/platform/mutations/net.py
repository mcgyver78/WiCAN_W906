"""Mutations of display/main/net.c, see ../redproof.py. The simulation is ../net_sim.c."""

F = "main/net.c"
S = "net_sim"

# What the info page shows
INFO_IP = "has_ip ? sta_ip : ap_on ? ap_ip : \"\""
REPORT_END = "\tapp_net(platform_app, now);\n\tplatform_events();\n\tinfo_fill();\n\tplatform_unlock();\n}\n\nstatic void signal_read"
SIGNAL_SHOWN = "\tsignal_read();\n\tplatform_lock();\n\tinfo_fill();\n\tplatform_unlock();\n}"

# The conversation with the adapter
COUNT_RECONNECT = "\t\t\tif(kept)\n\t\t\t{\n\t\t\t\treconnects++;\n\t\t\t}"
SEQ_HEADER = "strcasecmp(event->header_key, POLL_SEQ_HEADER) == 0"
STEP_TIMEOUT = "\tesp_http_client_set_timeout_ms(event->client, time_left(reply.until_ms));"
SET_URL = "esp_http_client_set_url(client, request->path) != ESP_OK"
SET_HEADER = "request->post ? POLL_HEADER_VALUE : NULL"
ATTEMPT_TIMEOUT = "\tesp_http_client_set_timeout_ms(client, time_left(until_ms));"
TOO_LARGE = "\t\tif(reply.too_large)\n\t\t{\n\t\t\treply.length = 0;\n\t\t}"
FAILED_CLOSES = "\t\tesp_http_client_close(client);\n\t\t// A body that did not arrive whole is none"
FAILED_BODY = "\t\t// A body that did not arrive whole is none\n\t\treply.length = 0;"
POST_ALONE = "\tif(request->post)\n\t{\n\t\tclient_drop();\n\t}"
GET_AGAIN = "if(status == 0 && !request->post && !reply.connected && platform_now_ms() < until_ms)"
PREPARED = "\t\tapp_net(platform_app, now);\n\t\tplatform_events();\n\t\t// The text lies in the app"
NO_HOST = "\tif(!due || host[0] == '\\0')"
STORED = "\tplatform_stored();\n\tstatus = ask(host, &request);\n"
APPLY = "\tpoll_apply(&platform_app->poll, &request, status, room->body, reply.length,"
APPLIED = "\tapp_net(platform_app, now);\n\tplatform_events();\n\tplatform_unlock();\n\treturn status != 0;"

# The station
STOP_WAITS = "\t\twhile(sta_busy && now < until_ms)"
STOP_CLEARS = "\txEventGroupClearBits(events, NET_EVENT_DISCONNECTED | NET_EVENT_GOT_IP);\n"
ENDED_FAILED = "\tbool failed = joining;"
ENDED_LOST = "\t\tlink_lost(&platform_app->link, now);"
GOT_IP_BUSY = "\tsta_busy = true;\n\thas_ip = true;\n\tsignal_read();"
JOINED = "\t\tlink_joined(&platform_app->link, gateway, now);"
JOIN_STOPS = "\twifi_config_t config;\n\tuint64_t now;\n\n\tstation_stop();"
JOIN_REFUSED = "\tnow = report_begin();\n\tlink_join_failed(&platform_app->link, now);\n\treport_end(now);\n}\n\nstatic void leave"
LEAVE_WAITS = "\tvTaskDelay(pdMS_TO_TICKS(NET_LEAVE_WAIT_MS));"
LEFT = "\tlink_left(&platform_app->link, now);"

# Scan and query
SEEN_SECURE = "\t\tseen->secure = record->authmode != WIFI_AUTH_OPEN;\n"
SEEN_WEB = "\tapp_web_seen(platform_app, room->seen_web, count);"
SCAN_STOPS = "static void scan_start(void)\n{\n\tstation_stop();"
SCAN_LIMIT = "\tif(scanning && platform_now_ms() - scan_since_ms >= NET_SCAN_LIMIT_MS)"
FOUND = "\t\tlink_found(&platform_app->link, host, now);"

# The own access point
AP_CONFIG = "\t\terr = esp_wifi_set_config(WIFI_IF_AP, &ap_config);\n\t\tif(err != ESP_OK)"
AP_AGAIN = "\tif(ap_owed && !(sta_busy && !has_ip) && platform_now_ms() - ap_tried_ms >= NET_AP_RETRY_MS)\n"
AP_CLIENTS = "\tlink_ap_clients(&platform_app->link, count, now);"

# The task and its start
REASON = "\t\t\t\t\tatomic_store(&sta_reason, ((const wifi_event_sta_disconnected_t *)data)->reason);\n"
BOTH_BITS = "\t\tbits &= ~NET_EVENT_GOT_IP;\n"
TURN = "\t\tatomic_fetch_add(&task_turns, 1);\n"
TASK_WAITS = "answered ? 0 : pdMS_TO_TICKS(NET_STEP_MS)"
PASSWORD_LENGTH = "password_length >= 8 && password_length < sizeof(ap_config.ap.password)"
ROOM = "\troom = heap_caps_malloc(sizeof(*room), MALLOC_CAP_SPIRAM);\n"
NO_NVS = "\tinit_config.nvs_enable = 0;\n"
AP_PASSWORD = "\tmemcpy(ap_config.ap.password, ap_password, password_length);\n"
AP_WPA2 = "\tap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;\n"
AP_GIVEN = "\tESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, \"access point\");\n"
START_STA = "\tESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, \"WiFi mode\");\n"
MDNS_NAME = "\t\terr = mdns_hostname_set(WEB_HOST_NAME);\n"

MUTATIONS = [
    # what the info page shows
    ("net_access_point_address_not_shown", S, F, INFO_IP, "has_ip ? sta_ip : \"\""),
    ("net_no_app_net_after_a_report", S, F, REPORT_END,
     "\tplatform_events();\n\tinfo_fill();\n\tplatform_unlock();\n}\n\nstatic void signal_read"),
    ("net_signal_and_count_not_shown", S, F, SIGNAL_SHOWN, "\tsignal_read();\n}"),

    # the conversation with the adapter
    ("net_every_connection_is_a_reconnect", S, F, COUNT_RECONNECT, "\t\t\treconnects++;"),
    ("net_seq_header_compared_with_its_case", S, F, SEQ_HEADER, "strcmp(event->header_key, POLL_SEQ_HEADER) == 0"),
    ("net_timeout_not_shortened_step_by_step", S, F, STEP_TIMEOUT, "\t(void)0;"),
    ("net_host_goes_into_the_url", S, F, SET_URL, "esp_http_client_set_url(client, host) != ESP_OK"),
    ("net_header_stays_on_for_a_get", S, F, SET_HEADER, "POLL_HEADER_VALUE"),
    ("net_second_attempt_with_the_whole_time", S, F, ATTEMPT_TIMEOUT,
     "\tesp_http_client_set_timeout_ms(client, (int)POLL_TIMEOUT_MS);"),
    ("net_body_without_room_passed_on", S, F, TOO_LARGE, "\t\t(void)0;"),
    ("net_failed_attempt_leaves_the_connection_open", S, F, FAILED_CLOSES,
     "\t\t// A body that did not arrive whole is none"),
    ("net_failed_attempt_keeps_half_a_body", S, F, FAILED_BODY, "\t\t(void)0;"),
    ("net_post_into_the_kept_connection", S, F, POST_ALONE, "\t(void)0;"),
    ("net_get_on_a_closed_connection_not_sent_again", S, F, GET_AGAIN, "if(false)"),
    ("net_no_events_taken_after_a_request_was_handed_out", S, F, PREPARED,
     "\t\tapp_net(platform_app, now);\n\t\t// The text lies in the app"),
    ("net_request_sent_without_an_address", S, F, NO_HOST, "\tif(!due)"),
    ("net_request_before_what_was_raised_is_stored", S, F, STORED, "\tstatus = ask(host, &request);\n"),
    ("net_no_answer_applied", S, F, APPLY, "\tpoll_apply(&platform_app->poll, &request, 0, room->body, 0,"),
    ("net_no_events_taken_after_an_answer", S, F, APPLIED,
     "\tapp_net(platform_app, now);\n\tplatform_unlock();\n\treturn status != 0;"),

    # the station
    ("net_no_wait_for_the_driver_when_the_station_is_stopped", S, F, STOP_WAITS, "\t\twhile(false)"),
    ("net_old_reports_kept_when_the_station_is_stopped", S, F, STOP_CLEARS, "\t(void)0;\n"),
    ("net_failed_join_reported_as_lost", S, F, ENDED_FAILED, "\tbool failed = false;"),
    ("net_lost_network_not_reported", S, F, ENDED_LOST, "\t\t(void)0;"),
    ("net_address_nobody_waited_for_leaves_the_station_idle", S, F, GOT_IP_BUSY, "\thas_ip = true;\n\tsignal_read();"),
    ("net_link_not_told_of_a_join", S, F, JOINED, "\t\t(void)gateway;"),
    ("net_station_not_stopped_before_a_join", S, F, JOIN_STOPS, "\twifi_config_t config;\n\tuint64_t now;\n"),
    ("net_refused_join_not_reported", S, F, JOIN_REFUSED, "\t(void)now;\n}\n\nstatic void leave"),
    ("net_leave_at_once", S, F, LEAVE_WAITS, "\t(void)0;"),
    ("net_leave_without_telling_the_link", S, F, LEFT, "\t(void)0;"),

    # scan and query
    ("net_open_network_shown_as_secure", S, F, SEEN_SECURE, "\t\tseen->secure = true;\n"),
    ("net_scan_does_not_reach_the_web_interface", S, F, SEEN_WEB, "\t(void)0;"),
    ("net_station_not_stopped_before_a_scan", S, F, SCAN_STOPS, "static void scan_start(void)\n{"),
    ("net_no_limit_for_a_scan", S, F, SCAN_LIMIT, "\tif(false)"),
    ("net_query_result_reported_as_not_found", S, F, FOUND, "\t\tlink_not_found(&platform_app->link, now);"),

    # the own access point
    ("net_access_point_without_its_configuration", S, F, AP_CONFIG, "\t\tif(err != ESP_OK)"),
    ("net_refused_access_point_order_lost", S, F, AP_AGAIN, "\tif(false)\n"),
    ("net_access_point_order_given_again_in_an_attempt", S, F, AP_AGAIN,
     "\tif(ap_owed && platform_now_ms() - ap_tried_ms >= NET_AP_RETRY_MS)\n"),
    ("net_access_point_clients_not_reported", S, F, AP_CLIENTS, "\t(void)count;"),

    # the task and its start
    ("net_reason_of_the_driver_not_kept", S, F, REASON, ""),
    ("net_address_counts_although_the_connection_ended", S, F, BOTH_BITS, "\t\t(void)0;\n"),
    ("net_turns_not_counted", S, F, TURN, ""),
    ("net_no_wait_in_the_task", S, F, TASK_WAITS, "answered ? 0 : 0"),
    ("net_short_access_point_password_taken", S, F, PASSWORD_LENGTH,
     "password_length >= 1 && password_length < sizeof(ap_config.ap.password)"),
    ("net_room_in_the_internal_ram", S, F, ROOM, "\troom = heap_caps_malloc(sizeof(*room), MALLOC_CAP_INTERNAL);\n"),
    ("net_driver_with_its_own_nvs", S, F, NO_NVS, ""),
    ("net_access_point_without_its_password", S, F, AP_PASSWORD, ""),
    ("net_access_point_open", S, F, AP_WPA2, ""),
    ("net_access_point_not_configured_before_the_start", S, F, AP_GIVEN, ""),
    ("net_starts_as_an_access_point", S, F, START_STA, ""),
    ("net_no_name_for_mdns", S, F, MDNS_NAME, "\t\terr = ESP_OK;\n"),
]
