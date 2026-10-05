# The screens of the display, drawn on a PC without a display and checked (render.c).
#
# LVGL comes from a checkout of its repository at the version the firmware is built with
# (display/main/idf_component.yml):
#
#   git clone --depth 1 --branch v9.5.0 https://github.com/lvgl/lvgl.git
#   make LVGL=<that directory>      builds LVGL and the renderer, runs it, leaves the pictures in build/out/
#   make clean
#
# The CI runs "make -j" in this directory (.github/workflows/display.yml, job ui-render).

CC      ?= cc
# The dialect of C the firmware is built in (ESP-IDF v5.5.2, tools/cmake/build.cmake): named here, so that a
# runner whose compiler has another one of its own does not build something nobody has looked at
STD     := -std=gnu17
# Our own files, as the host tests of the logic are built (display/test/Makefile)
CFLAGS  ?= $(STD) -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1
# EXTRA=-funsigned-char builds them as the ESP32 does (char is unsigned there, signed on PC and Mac)
EXTRA   ?=
# LVGL: not our code, so its warnings are not our errors
LVGL_CFLAGS ?= $(STD) -O1 -g -w

LVGL    ?=
UI      := ../components/ui
CORE    := ../components/core
BUILD   := build
OUT     := $(BUILD)/out

# The scenes the core makes, as its host tests keep them (scene_dump(), display/test): every one is drawn
# and checked. Not scene_dump_*: those are scenes scene_build() never makes, for the test of the dump itself.
FIXTURES := $(sort $(filter-out ../test/fixtures/scene_dump_%,$(wildcard ../test/fixtures/scene_*.txt ../test/fixtures/app_*.txt)))

# The TrueType file the fonts are made from is part of LVGL (ui_font.h)
FONT    := $(LVGL)/scripts/built_in_font/DejaVuSans.ttf

# LV_CONF_INCLUDE_SIMPLE: lv_conf.h is the one in this directory. LV_KCONFIG_IGNORE: nothing comes from a
# Kconfig here. This is how LVGL builds itself for a PC (env_support/cmake/os_desktop.cmake).
DEFINES  := -DLV_CONF_INCLUDE_SIMPLE -DLV_KCONFIG_IGNORE
INCLUDES := -I. -I$(LVGL) -I$(UI) -I$(CORE)

SOURCES := render.c $(UI)/ui.c $(UI)/ui_layout.c $(UI)/ui_font.c
HEADERS := lv_conf.h $(wildcard $(UI)/*.h $(CORE)/*.h)

.PHONY: all clean

ifeq ($(MAKECMDGOALS),clean)
clean:
	rm -rf $(BUILD)
else

ifeq ($(wildcard $(LVGL)/lvgl.h),)
$(error LVGL=<directory> has to name a checkout of LVGL, see the head of this Makefile)
endif
# Without them the run would look at the screens made by hand only, and nobody would notice
ifeq ($(FIXTURES),)
$(error no scenes of the core in ../test/fixtures)
endif

# Every source of LVGL, as its own build takes them; what lv_conf.h switches off compiles to nothing
LVGL_SOURCES := $(shell find $(LVGL)/src -name '*.c')
LVGL_OBJECTS := $(patsubst $(LVGL)/src/%.c,$(BUILD)/lvgl/%.o,$(LVGL_SOURCES))

all: $(BUILD)/render
	@mkdir -p $(OUT)
	$(BUILD)/render $(OUT) $(FIXTURES)

$(BUILD)/render: $(SOURCES) $(HEADERS) $(LVGL_OBJECTS) $(BUILD)/font.o
	$(CC) $(CFLAGS) $(EXTRA) $(DEFINES) $(INCLUDES) $(SOURCES) $(LVGL_OBJECTS) $(BUILD)/font.o -lm -o $@

$(BUILD)/lvgl/%.o: $(LVGL)/src/%.c lv_conf.h
	@mkdir -p $(dir $@)
	$(CC) $(LVGL_CFLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@

# The file as it is, between the two symbols ui_font.c asks for; the firmware gets the same from
# target_add_binary_data() of ESP-IDF (components/ui/CMakeLists.txt)
$(BUILD)/font.o: $(FONT)
	@mkdir -p $(BUILD)
	printf '#ifdef __ELF__\n\t.section .note.GNU-stack,"",%%progbits\n\t.section .rodata\n#else\n\t.const\n#endif\n\t.globl _binary_DejaVuSans_ttf_start\n_binary_DejaVuSans_ttf_start:\n\t.incbin "%s"\n\t.globl _binary_DejaVuSans_ttf_end\n_binary_DejaVuSans_ttf_end:\n' '$(FONT)' \
		| $(CC) -c -x assembler-with-cpp -o $@ -

endif
