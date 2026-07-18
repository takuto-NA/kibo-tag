
# adapted from https://github.com/pantuza/c-project-template

BINARY=atagjs_example

BINDIR := bin
SRCDIR := src
LOGDIR := log
APRILTAG := apriltag
TESTDIR := test
WASMDIR := html

CC := gcc
STD := -std=gnu99
STACK := -fstack-protector-all -Wstack-protector
WARNS := -Wall -Wextra -pedantic
CFLAGS := -O3 $(STD) $(STACK) $(WARNS)
DEBUG := -g3 -DDEBUG=1
LIBS := -lm -lpthread -I../$(APRILTAG)
APRILTAGS := -lm -lpthread -I$(APRILTAG) -I$(APRILTAG)/aruco
TEST_LIBS := $(shell pkg-config --libs cmocka 2>/dev/null || echo -lcmocka)
TEST_BINARY := $(BINARY)_test_runner
VALGRIND_TEST_ARGS := test/tag-imgs/*

SRCS := $(filter-out $(SRCDIR)/$(BINARY).c,$(wildcard $(SRCDIR)/*.c))
OBJS := $(SRCS:%.c=%.o)

APRILTAG_ARUCO_SRCS := $(wildcard $(APRILTAG)/aruco/tagAruco4x4_100.c)

APRILTAG_SRCS := $(filter-out \
	$(APRILTAG)/apriltag_pywrap.c \
	$(APRILTAG)/tagCircle49h12.c \
	$(APRILTAG)/tagCustom48h12.c \
	$(APRILTAG)/tagStandard52h13.c, \
	$(wildcard $(APRILTAG)/*.c $(APRILTAG)/common/*.c)) \
	$(APRILTAG_ARUCO_SRCS)
APRILTAG_OBJS := $(APRILTAG_SRCS:%.c=%.o)

TEST_SRCS := $(filter-out $(TESTDIR)/main.c,$(wildcard $(TESTDIR)/*.c))

WASM_RUNTIME_METHODS := '["cwrap", "getValue", "setValue", "UTF8ToString"]'

# Absolute path: bare "node" fails under make in the emsdk Docker image on some hosts.
NODE := $(shell command -v node)

.PHONY: default all help clean tests valgrind apriltag_wasm.js wasm-smoke js-logic-tests docs

default: $(BINARY)

all: $(BINARY) apriltag_wasm.js

help:
	@echo "Target rules:"
	@echo "    all      - Builds the example binary (atagjs_example) and the WASM files (apriltag_wasm.js)"
	@echo "    tests    - Compiles with cmocka and run tests binary file"
	@echo "    js-logic-tests - Runs pure JS detector-settings unit tests (requires node)"
	@echo "    wasm-smoke - Builds WASM then runs Node smoke detect (requires node)"
	@echo "    valgrind - Runs binary file using valgrind tool"
	@echo "    clean    - Clean the project by removing binaries"
	@echo "    help     - Prints a help message with target rules"

$(BINARY): $(APRILTAG_OBJS) $(OBJS) $(SRCDIR)/$(BINARY).o
	$(warning building binary...)
	@mkdir -p $(BINDIR)
	$(CC) -o $(BINDIR)/$(BINARY) $^ $(DEBUG) $(CFLAGS) $(LIBS)
	@echo -en "\n--\nBinary file placed at" \
			  "$(BINDIR)/$(BINARY)\n";

$(APRILTAG)/%.o: $(APRILTAG)/%.c
	$(warning building apriltag...)
	$(CC) -c $^ -o $@ $(DEBUG) $(CFLAGS) $(APRILTAGS) -lpthread

$(APRILTAG)/aruco/%.o: $(APRILTAG)/aruco/%.c
	$(warning building apriltag aruco...)
	$(CC) -c $^ -o $@ $(DEBUG) $(CFLAGS) $(APRILTAGS) -lpthread

%.o: %.c
	$(CC) -c $^ -o $@ $(DEBUG) $(CFLAGS) $(APRILTAGS) -lpthread

valgrind:
	@mkdir -p $(LOGDIR)
	valgrind \
		--track-origins=yes \
		--leak-check=full \
		--leak-resolution=high \
		--log-file=$(LOGDIR)/$@.log \
		$(BINDIR)/$(BINARY) $(VALGRIND_TEST_ARGS)
	@echo -en "\nCheck the log file: $(LOGDIR)/$@.log\n"

tests: $(APRILTAG_OBJS) $(OBJS) $(TEST_SRCS)
	@mkdir -p $(BINDIR)
	@echo -en "CC ";
	$(CC) $(TESTDIR)/main.c -o $(BINDIR)/$(TEST_BINARY) $^ $(DEBUG) $(CFLAGS) $(LIBS) $(TEST_LIBS) -I$(SRCDIR) -I$(APRILTAG) -I$(APRILTAG)/aruco
	@echo -en " Running tests: ";
	./$(BINDIR)/$(TEST_BINARY)
	@echo -en " Running JS logic tests: ";
	$(NODE) $(TESTDIR)/detector_settings_logic.test.mjs

js-logic-tests:
	$(NODE) $(TESTDIR)/detector_settings_logic.test.mjs

apriltag_wasm.js: $(APRILTAG_SRCS) $(SRCS)
	@mkdir -p $(WASMDIR)
	emcc -Os -s MODULARIZE=1 -s 'EXPORT_NAME="AprilTagWasm"' -s WASM=1 -Iapriltag -Iapriltag/aruco -s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_FUNCTIONS="['_free']" -s EXPORTED_RUNTIME_METHODS=$(WASM_RUNTIME_METHODS) -o $(WASMDIR)/$@ $^

wasm-smoke: apriltag_wasm.js
	$(NODE) $(TESTDIR)/wasm_detect_smoke.mjs

docs:
	doxygen

clean:
	@rm -rvf $(BINDIR)/* $(LOGDIR)/* \
		$(APRILTAG)/*.o $(APRILTAG)/common/*.o $(APRILTAG)/aruco/*.o \
		$(SRCDIR)/*.o \
		$(WASMDIR)/apriltag_wasm.js $(WASMDIR)/apriltag_wasm.wasm;
