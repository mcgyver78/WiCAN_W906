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

# A failed assertion of LVGL ends in abort(), which restarts the device, not in the loop without end LVGL
# has for it: lv_conf_internal.h makes LV_ASSERT_HANDLER "while(1);" unless it is defined, and the Kconfig of
# 9.5.0 has no symbol for it (sdkconfig.defaults names the header, <stdlib.h>). The definition has to reach
# the sources of LVGL, which are another component. Its target exists here: ESP-IDF adds what a component
# requires before the component itself (tools/cmake/build.cmake, __build_expand_requirements).
#
# lv_assert.h puts the macro in front of a "}" and adds no semicolon, so it has to be a whole statement. One
# without a semicolon of its own: CMake keeps compile definitions as a list, whose separator is the
# semicolon, and one inside a definition would cut it in two. So abort() is called where the condition of
# an if is worked out.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PRIVATE "LV_ASSERT_HANDLER=if((abort(),0)){}")
