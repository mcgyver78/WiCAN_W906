# Host tests of the display logic. Every test_<module>.c is built with the sources named for it below.
#
#   make            build and run all tests
#   make test_json  build one test
#
# The CI runs "make" and "python redproof.py" in this directory.

CC      ?= cc
CFLAGS  ?= -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all
# EXTRA=-funsigned-char builds the tests as the ESP32 does (char is unsigned there, signed on PC and Mac)
EXTRA   ?=
CORE    := ../components/core
TESTS   := $(patsubst %.c,%,$(wildcard test_*.c))

# Sources each test needs. A module may only use the modules listed with it.
SOURCES_json        := json.c
SOURCES_fmt         := fmt.c
SOURCES_net_select  := net_select.c
SOURCES_ota_check   := ota_check.c
SOURCES_hold        := hold.c
SOURCES_values      := values.c json.c
SOURCES_catalog     := catalog.c values.c json.c
SOURCES_wican_state := wican_state.c json.c
SOURCES_dtc_model   := dtc_model.c json.c
SOURCES_layout      := layout.c catalog.c values.c fmt.c json.c
SOURCES_conn        := conn.c
SOURCES_dtc_flow    := dtc_flow.c conn.c values.c catalog.c json.c
SOURCES_access      := access.c
SOURCES_web_route   := web_route.c
SOURCES_settings    := settings.c json.c
SOURCES_web_json    := web_json.c layout.c catalog.c values.c fmt.c net_select.c json.c
SOURCES_knob        := knob.c
SOURCES_guard       := guard.c
SOURCES_dtc_view    := dtc_view.c dtc_model.c json.c
SOURCES_texts       := texts.c
SOURCES_nav         := nav.c layout.c catalog.c values.c fmt.c json.c
SOURCES_link        := link.c net_select.c
SOURCES_touch       := touch.c
SOURCES_poll        := poll.c conn.c dtc_flow.c values.c catalog.c wican_state.c dtc_model.c guard.c json.c
SOURCES_scene       := scene.c nav.c layout.c catalog.c values.c fmt.c json.c conn.c dtc_flow.c dtc_model.c \
                       dtc_view.c hold.c access.c guard.c texts.c
APP_SOURCES         := app.c poll.c link.c scene.c nav.c knob.c hold.c access.c settings.c layout.c catalog.c \
                       values.c fmt.c json.c conn.c dtc_flow.c dtc_model.c dtc_view.c guard.c texts.c \
                       wican_state.c net_select.c
SOURCES_app         := $(APP_SOURCES)
SOURCES_app_web     := app_web.c web_route.c web_json.c ota_check.c $(APP_SOURCES)

.PHONY: all clean

all: $(TESTS)
	@set -e; for test in $(TESTS); do echo "== $$test"; ./$$test > $$test.log || { cat $$test.log; exit 1; }; tail -1 $$test.log; done

.SECONDEXPANSION:
test_%: test_%.c test.h $$(addprefix $(CORE)/,$$(SOURCES_$$*)) $(wildcard $(CORE)/*.h)
	$(CC) $(CFLAGS) $(EXTRA) -I$(CORE) $(addprefix $(CORE)/,$(SOURCES_$*)) $< -lm -o $@

clean:
	rm -f $(TESTS) *.log
