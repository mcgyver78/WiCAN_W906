# Draws the scenes of the core with LVGL. Portable C without ESP-IDF: the same sources are rendered and checked
# on a PC (display/host). LVGL is a managed component, pinned in main/idf_component.yml with the others.
idf_component_register(SRCS "ui.c" "ui_layout.c" "ui_font.c"
                       INCLUDE_DIRS "."
                       REQUIRES core lvgl__lvgl)

# The TrueType file the fonts are made from comes with LVGL: the package of the registry holds
# scripts/built_in_font/DejaVuSans.ttf, the same 756072 bytes as the git tag. It is linked in as it is, and
# ui_font.c finds it between _binary_DejaVuSans_ttf_start and _binary_DejaVuSans_ttf_end. This stands
# behind idf_component_register(): the directory of another component is only known once all are found.
idf_component_get_property(lvgl_dir lvgl__lvgl COMPONENT_DIR)
message(STATUS "LVGL for the fonts: ${lvgl_dir}")
target_add_binary_data(${COMPONENT_LIB} "${lvgl_dir}/scripts/built_in_font/DejaVuSans.ttf" BINARY)
