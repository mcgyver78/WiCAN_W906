# Host tests of the display logic. Every test_<module>.c is built together with all sources of
# display/components/core, so a module may use the others.
#
#   make            build and run all tests
#   make test_json  build one test
#
# The CI runs "make" and "python redproof.py" in this directory.

CC      ?= cc
CFLAGS  ?= -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all
CORE    := ../components/core
SOURCES := $(wildcard $(CORE)/*.c)
TESTS   := $(patsubst %.c,%,$(wildcard test_*.c))

.PHONY: all clean

all: $(TESTS)
	@set -e; for test in $(TESTS); do echo "== $$test"; ./$$test > $$test.log || { cat $$test.log; exit 1; }; tail -1 $$test.log; done

test_%: test_%.c test.h $(SOURCES) $(wildcard $(CORE)/*.h)
	$(CC) $(CFLAGS) -I$(CORE) $(SOURCES) $< -lm -o $@

clean:
	rm -f $(TESTS) *.log
