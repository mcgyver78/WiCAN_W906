# The platform of the display: tasks, WiFi, HTTP, flash and screen around the logic in components/core.
# Managed components (mDNS, the LVGL port) are named in idf_component.yml next to this file.
idf_component_register(SRCS "main.c" "screen.c" "net.c" "web.c"
                       INCLUDE_DIRS "."
                       REQUIRES core board store ui app_update esp_timer esp_wifi esp_netif esp_event
                                esp_http_client esp_http_server nvs_flash esp_lcd esp_partition
                       EMBED_FILES "web/index.html" "../layouts/w906_default.json")
