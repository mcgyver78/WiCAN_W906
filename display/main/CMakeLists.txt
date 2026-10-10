# The platform of the display: tasks, WiFi, HTTP, flash and screen around the logic in components/core.
# Managed components (LVGL, mDNS) are named in idf_component.yml next to this file.
idf_component_register(SRCS "main.c" "screen.c" "net.c" "web.c"
                       INCLUDE_DIRS "."
                       REQUIRES core board store ui app_update esp_timer esp_wifi esp_netif esp_event
                                esp_http_client esp_http_server nvs_flash esp_lcd esp_partition
                       EMBED_FILES "web/index.html" "../layouts/w906_default.json")

# The page a second time, compressed: web.c sends it to every client that says it reads gzip, a third of the
# bytes over a radio link that is slow. It is no file of the repository: the build makes it of web/index.html
# in its own directory, with the Python of ESP-IDF and the zlib that comes with every Python - no gzip
# program is asked for. tools/page_gz.py says what the bytes are (one gzip member, level 9, no name and no
# time in it) and writes nothing that does not unpack to the page; display/host/platform/Makefile makes the
# same file with the same script for the simulation of web.c.
#
# Embedded the way ESP-IDF embeds a file it generates itself (v5.5.2: components/bootloader_support, and
# "Embedding Binary Data" in the build system guide): a command that makes the file, and
# target_add_binary_data(), which is what EMBED_FILES calls for each of its files. The symbols are named
# after the file name alone, as for the page: _binary_index_html_gz_start and _binary_index_html_gz_end.
# Below idf_component_register(): the first pass over this file, which only collects the requirements, ends
# there, and ${COMPONENT_LIB} exists from there on.
set(page "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html")
get_filename_component(page_pack "${CMAKE_CURRENT_SOURCE_DIR}/../tools/page_gz.py" ABSOLUTE)
set(page_gz "${CMAKE_CURRENT_BINARY_DIR}/index.html.gz")
idf_build_get_property(python PYTHON)
add_custom_command(OUTPUT "${page_gz}"
                   COMMAND ${python} "${page_pack}" "${page}" "${page_gz}"
                   DEPENDS "${page}" "${page_pack}"
                   COMMENT "Compressing the page of the display"
                   VERBATIM)
target_add_binary_data(${COMPONENT_LIB} "${page_gz}" BINARY)
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${page_gz}")
