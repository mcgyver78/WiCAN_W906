"""Mutations of display/main/web.c, see ../redproof.py. The simulation is ../web_sim.c."""

F = "main/web.c"
S = "web_sim"

# read_request()
SATURATE = "\t\tif(value > UINT32_MAX)\n\t\t{\n\t\t\treturn UINT32_MAX;\n\t\t}"
CUT_QUERY = "\t\t*query++ = '\\0';"
READ_HEADER = "\trequest->header = header_value(req, WEB_HEADER_NAME, header, sizeof(header));\n"
LENGTH_FULL = "length != NULL && strlen(length) >= sizeof(length_text) - 1 ? UINT32_MAX : length_of(length);"
CLOSE_AFTER = "\tclose_after = request->method == WEB_OTHER || (request->method != WEB_GET && !request->has_length) ||\n" \
              "\t              (request->has_length && request->length != req->content_len);\n"

# receive(), answer()
RECEIVE_ENDS = "\t\telse if(more != HTTPD_SOCK_ERR_TIMEOUT || !patient)\n"
RECEIVE_LOOKS = "\t\t\tif(more <= 0 || got == length || now - looked_ms >= WEB_LOOK_MS)\n"
BODY_TIME = "\t\telse if(got < length && now - begun_ms >= WEB_BODY_MS)\n"
ANSWER_TYPE = "\thttpd_resp_set_type(req, type);\n"
DROP = "\t\tdropped = httpd_req_recv(req, room->body, sizeof(room->body));\n"
DROP_TIME = "\t\telse if(platform_now_ms() >= until_ms)\n"

# upload()
BEGUN = "\tbegun = platform_upload_begun() && flashed(esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota), \"begin\");\n"
PROGRESS = "\t\tok = progress(written, size);\n"
IMAGE_CHECK = "\t\tok = flashed(esp_ota_end(ota), \"check of the image\");"
ABORT = "\t\tesp_ota_abort(ota);"
COMPLETE = "\tif(ok)\n\t{\n\t\tplatform_upload_complete();\n\t}"
UPLOAD_END = "\tstatus = app_web_upload_end(platform_app, ok, room->out, &length, now);"

# handle()
STORED_FIRST = "\tplatform_stored();\n\tread_request(req, &request);\n\n" \
               "\t// The time is read after the request has arrived and before the lock is waited for\n" \
               "\tnow = platform_now_ms();\n\tplatform_lock();\n\tapp_web_request(platform_app, &request, now);\n" \
               "\tdecision = web_route(&request);\n\tplatform_unlock();\n"
REFUSAL = "web_error_body(decision.error, room->out, APP_WEB_OUT_SIZE)"
FRAME = "\t\thttpd_resp_set_hdr(req, \"X-Frame-Options\", \"DENY\");\n"
FRAME_POLICY = "\t\thttpd_resp_set_hdr(req, \"Content-Security-Policy\", \"frame-ancestors 'none'\");\n"
PAGE = "\t\treturn answer(req, 200, WEB_TYPE_PAGE, page_start, (size_t)(page_end - page_start));\n"
WHOLE_BODY = "!receive(req, room->body, request.length, false)))"
CALL_TIME = "\tnow = platform_now_ms();\n\tplatform_lock();\n\tswitch(decision.route)"
EVENTS = "\tplatform_events();\n\tplatform_unlock();\n\treturn answer(req, status, WEB_TYPE_JSON, room->out, length);\n}\n\n" \
         "// What the server cannot read"

# on_error(), on_open(), web_start()
ERROR_CLOSES = "\treturn ESP_FAIL;\n}\n\n// A new connection."
NODELAY = "\tint on = 1;\n"
BLOCK_ROOM = "\tblock = heap_caps_malloc(WEB_BLOCK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);\n"
EVERY_METHOD = "\t\t.method = HTTP_ANY,\n"
WILDCARD = "\tconfig.uri_match_fn = httpd_uri_match_wildcard;\n"
EVERY_ERROR = "\tfor(int error = 0; error < HTTPD_ERR_CODE_MAX; error++)\n"

MUTATIONS = [
    # the request as it was received
    ("web_length_not_saturated", S, F, SATURATE, "\t\tvalue &= 0xffffffffu;"),
    ("web_query_not_cut_off_the_path", S, F, CUT_QUERY, "\t\tquery++;"),
    ("web_host_room_cut_to_an_allowed_address", S, F, "static char host[64];", "static char host[12];"),
    ("web_header_room_too_small", S, F, "static char header[4];", "static char header[2];"),
    ("web_change_without_the_header", S, F, READ_HEADER,
     "\t(void)header_value(req, WEB_HEADER_NAME, header, sizeof(header));\n\trequest->header = \"1\";\n"),
    ("web_length_that_fills_its_room_read_as_a_number", S, F, LENGTH_FULL, "length_of(length);"),
    ("web_never_closed_after_the_answer", S, F, CLOSE_AFTER, "\tclose_after = false;\n"),
    ("web_unserved_method_keeps_the_connection", S, F, CLOSE_AFTER,
     "\tclose_after = (request->method != WEB_GET && !request->has_length) ||\n"
     "\t              (request->has_length && request->length != req->content_len);\n"),
    ("web_change_without_a_length_keeps_the_connection", S, F, CLOSE_AFTER,
     "\tclose_after = request->method == WEB_OTHER ||\n"
     "\t              (request->has_length && request->length != req->content_len);\n"),
    ("web_other_length_of_the_server_keeps_the_connection", S, F, CLOSE_AFTER,
     "\tclose_after = request->method == WEB_OTHER || (request->method != WEB_GET && !request->has_length);\n"),

    # waiting for a client
    ("web_timeout_ends_a_block_of_the_firmware", S, F, RECEIVE_ENDS, "\t\telse\n"),
    ("web_app_not_asked_while_a_block_comes_in", S, F, RECEIVE_LOOKS, "\t\t\tif(now < looked_ms)\n"),
    ("web_body_takes_as_long_as_its_sender_likes", S, F, BODY_TIME, "\t\telse if(false)\n"),
    ("web_nothing_dropped_behind_the_answer", S, F, DROP, "\t\tdropped = 0;\n"),
    ("web_dropping_has_no_end", S, F, DROP_TIME, "\t\telse if(false)\n"),
    ("web_answer_lets_another_origin_in", S, F, ANSWER_TYPE,
     "\thttpd_resp_set_type(req, type);\n\thttpd_resp_set_hdr(req, \"Access-Control-Allow-Origin\", \"*\");\n"),

    # the firmware
    ("web_slot_erased_before_its_record", S, F, BEGUN,
     "\tbegun = flashed(esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota), \"begin\") && platform_upload_begun();\n"),
    ("web_upload_begins_without_its_record", S, F, BEGUN,
     "\tbegun = (platform_upload_begun(), true) && flashed(esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota), \"begin\");\n"),
    ("web_slot_erased_in_one_piece", S, F, BEGUN,
     "\tbegun = platform_upload_begun() && flashed(esp_ota_begin(slot, OTA_SIZE_UNKNOWN, &ota), \"begin\");\n"),
    ("web_progress_not_told", S, F, PROGRESS, "\t\t(void)progress;\n\t\tok = true;\n"),
    ("web_image_not_checked", S, F, IMAGE_CHECK, "\t\tesp_ota_end(ota);"),
    ("web_no_abort", S, F, ABORT, "\t\t(void)ota;"),
    ("web_complete_also_when_the_image_is_bad", S, F, COMPLETE, "\tplatform_upload_complete();"),
    ("web_no_end_after_a_failed_upload", S, F, UPLOAD_END,
     "\tstatus = ok ? app_web_upload_end(platform_app, ok, room->out, &length, now) : 500;"),

    # a request
    ("web_request_served_before_what_was_raised_is_stored", S, F, STORED_FIRST,
     STORED_FIRST.replace("\tplatform_stored();\n", "")),
    ("web_stored_asked_after_the_app", S, F, STORED_FIRST,
     STORED_FIRST.replace("\tplatform_stored();\n", "") + "\tplatform_stored();\n"),
    ("web_refusal_with_another_word", S, F, REFUSAL, "web_error_body(\"not_found\", room->out, APP_WEB_OUT_SIZE)"),
    ("web_page_may_be_framed", S, F, FRAME, ""),
    ("web_page_without_its_frame_policy", S, F, FRAME_POLICY, ""),
    ("web_page_sent_as_json", S, F, PAGE,
     "\t\treturn answer(req, 200, WEB_TYPE_JSON, page_start, (size_t)(page_end - page_start));\n"),
    ("web_page_cut_by_a_byte", S, F, PAGE,
     "\t\treturn answer(req, 200, WEB_TYPE_PAGE, page_start, (size_t)(page_end - page_start) - 1);\n"),
    ("web_function_called_without_the_whole_body", S, F, WHOLE_BODY,
     "(receive(req, room->body, request.length, false), false)))"),
    ("web_time_ahead", S, F, CALL_TIME,
     "\tnow = platform_now_ms() + 600000;\n\tplatform_lock();\n\tswitch(decision.route)"),
    ("web_time_of_before_the_body", S, F, CALL_TIME, "\tplatform_lock();\n\tswitch(decision.route)"),
    ("web_events_not_taken_after_a_request", S, F, EVENTS,
     "\tplatform_unlock();\n\treturn answer(req, status, WEB_TYPE_JSON, room->out, length);\n}\n\n"
     "// What the server cannot read"),

    # the server
    ("web_error_answered_by_the_server", S, F, ERROR_CLOSES, "\treturn ESP_OK;\n}\n\n// A new connection."),
    ("web_small_answers_held_back", S, F, NODELAY, "\tint on = 0;\n"),
    ("web_block_in_the_external_ram", S, F, BLOCK_ROOM,
     "\tblock = heap_caps_malloc(WEB_BLOCK_SIZE, MALLOC_CAP_SPIRAM);\n"),
    ("web_handler_for_get_only", S, F, EVERY_METHOD, "\t\t.method = HTTP_GET,\n"),
    ("web_no_wildcard", S, F, WILDCARD, ""),
    ("web_one_error_handled", S, F, EVERY_ERROR, "\tfor(int error = 0; error < 1; error++)\n"),
]
