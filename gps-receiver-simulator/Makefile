# GPS Receiver Simulator - build system
#   make                build release binary  -> build/gps_receiver
#   make debug          build debug binary    -> build/debug/gps_receiver
#   make test           run every test suite
#   make test-c | test-python | test-integration | sanitize | lint | clean

CC      ?= gcc
PYTHON  ?= python3
WERROR  ?= -Werror
WARN    := -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
           -Wformat=2 -Wconversion -Wno-sign-conversion $(WERROR)
INCLUDE := -Iinclude

BUILD_DIR ?= build
OPT       ?= -O2
EXTRA_CFLAGS  ?=
EXTRA_LDFLAGS ?=
CFLAGS  := $(WARN) $(OPT) $(INCLUDE) $(EXTRA_CFLAGS) -MMD -MP
LDFLAGS := $(EXTRA_LDFLAGS)

LIB_NAMES := gps_receiver nmea_parser nmea_checksum coordinate_converter
LIB_OBJS  := $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(LIB_NAMES)))
APP       := $(BUILD_DIR)/gps_receiver
TEST_NAMES := test_checksum test_parser test_coordinates
TEST_BINS  := $(addprefix $(BUILD_DIR)/tests/,$(TEST_NAMES))

SAN_FLAGS := -fsanitize=address,undefined -fno-sanitize-recover=all \
             -fno-omit-frame-pointer

.PHONY: all debug test test-c test-python test-integration sanitize lint clean help

all: $(APP)

help:
	@sed -n '2,5p' Makefile

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(APP): $(BUILD_DIR)/main.o $(LIB_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/tests/%: tests/c/%.c tests/c/test_harness.h $(LIB_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Itests/c $< $(LIB_OBJS) $(LDFLAGS) -o $@

debug:
	$(MAKE) BUILD_DIR=build/debug OPT="-O0 -g" all

test-c: $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do echo "== $$t"; $$t; done

test-python:
	$(PYTHON) -m unittest discover -s tests/python -v

test-integration: $(APP)
	GPS_RECEIVER_BIN=$(abspath $(APP)) $(PYTHON) -m unittest discover -s tests/integration -v

test: test-c test-python test-integration

# Rebuild everything under ASan+UBSan in build/asan and run C + integration tests.
sanitize:
	$(MAKE) BUILD_DIR=build/asan OPT="-O1 -g" EXTRA_CFLAGS="$(SAN_FLAGS)" \
	    EXTRA_LDFLAGS="$(SAN_FLAGS)" test-c build/asan/gps_receiver
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 \
	GPS_RECEIVER_BIN=$(abspath build/asan/gps_receiver) \
	    $(PYTHON) -m unittest discover -s tests/integration -v

lint:
	cppcheck --enable=warning,style,performance,portability --error-exitcode=1 \
	    --inline-suppr --suppress=missingIncludeSystem -I include src tests/c

clean:
	rm -rf build

-include $(wildcard $(BUILD_DIR)/*.d) $(wildcard $(BUILD_DIR)/tests/*.d)
