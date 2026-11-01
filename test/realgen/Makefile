# Host statistical tests for src/genetics.c (REALGEN_SPEC.md).
#   make -C test/realgen                     build + run against this worktree's src/genetics.c
#   make -C test/realgen core                build + run against branch lane/core's src/genetics.c
#   make -C test/realgen GENETICS_SRC=path   build + run against any copy of genetics.c
# Uses the system cc only; never builds the ROM.

ROOT        := $(abspath ../..)
GENETICS_SRC ?= $(ROOT)/src/genetics.c
BUILD       := build
CC          ?= cc
CFLAGS      := -std=gnu11 -O2 -g -Wall -Wno-unused-parameter -Wno-unused-function
INCLUDES    := -Ishim -I$(ROOT)/include
LIBS        := -lm

.PHONY: all test run core clean

all: test

test: $(BUILD)/test_genetics
	./$(BUILD)/test_genetics

$(BUILD)/test_genetics: test_genetics.c shim/rng.c $(BUILD)/genetics_under_test.c shim/global.h shim/random.h shim/pokemon.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_genetics.c shim/rng.c $(BUILD)/genetics_under_test.c $(LIBS)

# copy into build/ so quoted includes resolve through shim/ first, whatever GENETICS_SRC points at
$(BUILD)/genetics_under_test.c: FORCE | $(BUILD)
	@cmp -s "$(GENETICS_SRC)" $@ || cp "$(GENETICS_SRC)" $@
	@echo "genetics.c under test: $(GENETICS_SRC)"

core: | $(BUILD)
	git -C $(ROOT) show lane/core:src/genetics.c > $(BUILD)/genetics-core.c
	$(MAKE) test GENETICS_SRC=$(abspath $(BUILD)/genetics-core.c)

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

FORCE:
