# The platform of the display (display/main) on a PC: each simulation includes one of its files unchanged
# and runs it against the real core, with stand-ins for ESP-IDF (idf/, and what the simulation itself says
# of the driver, the server, the flash, the boot loader and FreeRTOS).
#
#   make                    build and run the four simulations; the output of each is build/<name>.log
#   make build/net_sim      build one
#   make clean
#
# The CI runs "make" in this directory (.github/workflows/display.yml, job host-tests).
# For people, not in the CI: "python3 redproof.py" breaks the platform files in one place at a time
# (mutations/) and expects a check of the simulation to fail; see the head of that script.
#
# Needs a C library with strlcpy(), which the platform files use (glibc from 2.38 on, macOS), and python3:
# the page is compressed as the build of the firmware compresses it (display/tools/page_gz.py).

CC      ?= cc
PYTHON  ?= python3
# The dialect of C the firmware is built in (ESP-IDF v5.5.2, tools/cmake/build.cmake)
STD     := -std=gnu17
# As the host tests of the logic are built (display/test/Makefile)
CFLAGS  ?= $(STD) -Wall -Wextra -Wimplicit-fallthrough -Werror -fsanitize=address,undefined -fno-sanitize-recover=all
# The three warnings the build of ESP-IDF switches off for every file of the firmware (the same build.cmake):
# the platform files were never asked to be free of them, and each simulation is one unit with its file
IDF_OFF := -Wno-unused-parameter -Wno-sign-compare -Wno-enum-conversion
# EXTRA=-funsigned-char builds as the ESP32 does (char is unsigned there, signed on PC and Mac)
EXTRA   ?=

MAIN    := ../../main
CORE    := ../../components/core
BUILD   := build
SIMS    := main_sim net_sim web_sim screen_sim

# idf/ first: its headers stand for those of ESP-IDF and of LVGL, and its sys/socket.h for that of lwIP.
# board.h, store.h and ui.h are the real ones: main.c and screen.c are run against what they promise.
INCLUDES := -Iidf -I$(MAIN) -I$(CORE) -I../../components/board -I../../components/store -I../../components/ui

# What the build of the firmware embeds ($(MAIN)/CMakeLists.txt): the page, the page compressed and the
# built-in layout, each between the two symbols web.c and main.c ask for, as it is and without a zero behind
# it. The compressed page is no file of the repository: it is made here as the build of the firmware makes
# it, by the same script, into the build directory.
PAGE    := $(MAIN)/web/index.html
PAGE_GZ := $(BUILD)/index.html.gz
PACK    := ../../tools/page_gz.py
LAYOUT  := ../../layouts/w906_default.json

SOURCES := $(wildcard $(CORE)/*.c)
HEADERS := sim.h $(wildcard idf/*.h idf/*/*.h $(CORE)/*.h $(MAIN)/*.h ../../components/board/*.h ../../components/store/*.h) \
           ../../components/ui/ui.h

.PHONY: all clean

# No zlib is linked into a simulation. web_sim compares the compressed answer byte for byte with $(PAGE_GZ),
# and that this file unpacks to exactly the page is checked here, with Python's gzip, before every run.
all: $(addprefix $(BUILD)/,$(SIMS))
	@$(PYTHON) $(PACK) --check $(PAGE) $(PAGE_GZ)
	@set -e; for sim in $(SIMS); do echo "== $$sim"; $(BUILD)/$$sim > $(BUILD)/$$sim.log || { cat $(BUILD)/$$sim.log; exit 1; }; tail -1 $(BUILD)/$$sim.log; done

# A simulation is named after the file it includes: main_sim.c runs $(MAIN)/main.c
$(BUILD)/%_sim: %_sim.c $(MAIN)/%.c $(HEADERS) $(SOURCES) $(BUILD)/embed.o
	$(CC) $(CFLAGS) $(IDF_OFF) $(EXTRA) $(INCLUDES) $< $(SOURCES) $(BUILD)/embed.o -lm -o $@

$(PAGE_GZ): $(PAGE) $(PACK)
	@mkdir -p $(BUILD)
	$(PYTHON) $(PACK) $(PAGE) $@

# The same way display/host/Makefile embeds the font
$(BUILD)/embed.o: $(PAGE) $(PAGE_GZ) $(LAYOUT)
	@mkdir -p $(BUILD)
	printf '#ifdef __ELF__\n\t.section .note.GNU-stack,"",%%progbits\n\t.section .rodata\n#else\n\t.const\n#endif\n\t.globl _binary_index_html_start\n_binary_index_html_start:\n\t.incbin "%s"\n\t.globl _binary_index_html_end\n_binary_index_html_end:\n\t.globl _binary_index_html_gz_start\n_binary_index_html_gz_start:\n\t.incbin "%s"\n\t.globl _binary_index_html_gz_end\n_binary_index_html_gz_end:\n\t.globl _binary_w906_default_json_start\n_binary_w906_default_json_start:\n\t.incbin "%s"\n\t.globl _binary_w906_default_json_end\n_binary_w906_default_json_end:\n' '$(PAGE)' '$(PAGE_GZ)' '$(LAYOUT)' \
		| $(CC) -c -x assembler-with-cpp -o $@ -

clean:
	rm -rf $(BUILD)
