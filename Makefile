# LithoOS — pure C11, -Wall -Wextra -Werror, no C++
CC       ?= gcc
CROSS    ?= aarch64-linux-gnu-
CSTD      = -std=c11
WARN      = -Wall -Wextra -Werror
OPT      ?= -O2
DEFS      = -D_GNU_SOURCE
INC       = -Iinclude
CFLAGS   ?= $(CSTD) $(WARN) $(OPT) $(DEFS) $(INC)
LDFLAGS  ?=
PTHREAD   = -pthread

SRC = src/handshake.c src/fb.c src/ui.c src/input.c src/theme.c \
      src/font.c src/pngenc.c src/battery.c src/scoreboard.c

HOST_DIR  = build/host
CROSS_DIR = build/aarch64

HOST_OBJ  = $(patsubst src/%.c,$(HOST_DIR)/%.o,$(SRC))
CROSS_OBJ = $(patsubst src/%.c,$(CROSS_DIR)/%.o,$(SRC))

.PHONY: all test bench preview cross verify-cross clean

all: $(HOST_DIR)/litho-init $(HOST_DIR)/litho-test $(HOST_DIR)/litho-bench

$(HOST_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(HOST_DIR)/litho-init: $(HOST_OBJ) src/init.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ src/init.c $(HOST_OBJ) $(PTHREAD) $(LDFLAGS)

$(HOST_DIR)/litho-test: $(HOST_DIR)/handshake.o tests/test_handshake.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/test_handshake.c $(HOST_DIR)/handshake.o $(PTHREAD) $(LDFLAGS)

$(HOST_DIR)/litho-bench: $(HOST_DIR)/handshake.o $(HOST_DIR)/scoreboard.o tests/bench_jitter.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/bench_jitter.c $(HOST_DIR)/handshake.o $(HOST_DIR)/scoreboard.o $(PTHREAD) $(LDFLAGS)

test: $(HOST_DIR)/litho-test
	$(HOST_DIR)/litho-test

bench: $(HOST_DIR)/litho-bench
	$(HOST_DIR)/litho-bench

preview: $(HOST_DIR)/litho-init
	$(HOST_DIR)/litho-init --backend=headless --snapshot-both=.

$(CROSS_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@

cross: $(CROSS_DIR)/litho-init $(CROSS_DIR)/litho-test $(CROSS_DIR)/litho-bench

$(CROSS_DIR)/litho-init: $(CROSS_OBJ) src/init.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(CFLAGS) -static -o $@ src/init.c $(CROSS_OBJ) $(PTHREAD)

$(CROSS_DIR)/litho-test: $(CROSS_DIR)/handshake.o tests/test_handshake.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(CFLAGS) -static -o $@ tests/test_handshake.c $(CROSS_DIR)/handshake.o $(PTHREAD)

$(CROSS_DIR)/litho-bench: $(CROSS_DIR)/handshake.o $(CROSS_DIR)/scoreboard.o tests/bench_jitter.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(CFLAGS) -static -o $@ tests/bench_jitter.c $(CROSS_DIR)/handshake.o $(CROSS_DIR)/scoreboard.o $(PTHREAD)

verify-cross: cross
	file $(CROSS_DIR)/litho-init $(CROSS_DIR)/litho-test $(CROSS_DIR)/litho-bench
	ls -l $(CROSS_DIR)/litho-init $(CROSS_DIR)/litho-test $(CROSS_DIR)/litho-bench
	@if command -v qemu-aarch64-static >/dev/null 2>&1; then \
		echo "qemu-aarch64-static litho-test:"; \
		qemu-aarch64-static $(CROSS_DIR)/litho-test; \
	elif command -v qemu-aarch64 >/dev/null 2>&1; then \
		echo "qemu-aarch64 litho-test:"; \
		qemu-aarch64 $(CROSS_DIR)/litho-test; \
	else \
		echo "qemu-aarch64 not installed; link verified only"; \
	fi

clean:
	rm -rf build
