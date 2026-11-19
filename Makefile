# LithoOS — pure C11, -Wall -Wextra -Werror, no C++
CC       ?= gcc
CROSS    ?= aarch64-linux-gnu-
CSTD      = -std=c11
WARN      = -Wall -Wextra -Werror
OPT      ?= -O2
DEFS      = -D_GNU_SOURCE
INC       = -Iinclude
GB_INC    = -Ithird_party/peanut-gb -Ithird_party/minigb_apu
CFLAGS   ?= $(CSTD) $(WARN) $(OPT) $(DEFS) $(INC)
TP_CFLAGS = $(CSTD) $(OPT) $(DEFS) $(INC) $(GB_INC) -DMINIGB_APU_AUDIO_FORMAT_S16SYS
LDFLAGS  ?=
PTHREAD   = -pthread

SRC = src/handshake.c src/fb.c src/ui.c src/input.c src/theme.c \
      src/font.c src/pngenc.c src/battery.c src/scoreboard.c src/gb_emu.c

HOST_DIR  = build/host
CROSS_DIR = build/aarch64

HOST_OBJ  = $(patsubst src/%.c,$(HOST_DIR)/%.o,$(SRC))
CROSS_OBJ = $(patsubst src/%.c,$(CROSS_DIR)/%.o,$(SRC))
HOST_APU  = $(HOST_DIR)/minigb_apu.o
CROSS_APU = $(CROSS_DIR)/minigb_apu.o

.PHONY: all test bench preview preview-gb gb-test cross verify-cross clean

all: $(HOST_DIR)/litho-init $(HOST_DIR)/litho-test $(HOST_DIR)/litho-bench \
     $(HOST_DIR)/litho-gb $(HOST_DIR)/litho-gb-test

$(HOST_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(HOST_DIR)/gb_emu.o: src/gb_emu.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GB_INC) -c $< -o $@

$(HOST_APU): third_party/minigb_apu/minigb_apu.c
	@mkdir -p $(dir $@)
	$(CC) $(TP_CFLAGS) -c $< -o $@

$(HOST_DIR)/litho-init: $(HOST_OBJ) $(HOST_APU) src/init.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GB_INC) -o $@ src/init.c $(HOST_OBJ) $(HOST_APU) $(PTHREAD) $(LDFLAGS)

$(HOST_DIR)/litho-gb: $(HOST_OBJ) $(HOST_APU) src/gb_host.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GB_INC) -o $@ src/gb_host.c $(HOST_OBJ) $(HOST_APU) $(PTHREAD) $(LDFLAGS)

$(HOST_DIR)/litho-gb-test: $(HOST_DIR)/handshake.o $(HOST_DIR)/gb_emu.o $(HOST_DIR)/fb.o $(HOST_DIR)/font.o $(HOST_DIR)/pngenc.o $(HOST_APU) tests/test_gb_handshake.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GB_INC) -o $@ tests/test_gb_handshake.c $(HOST_DIR)/handshake.o $(HOST_DIR)/gb_emu.o $(HOST_DIR)/fb.o $(HOST_DIR)/font.o $(HOST_DIR)/pngenc.o $(HOST_APU) $(PTHREAD) $(LDFLAGS)

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

preview-gb: $(HOST_DIR)/litho-gb
	$(HOST_DIR)/litho-gb --rom=roms/litho-demo.gb --frames=90 --snapshot=preview-gb-emu.png

gb-test: $(HOST_DIR)/litho-gb-test
	$(HOST_DIR)/litho-gb-test

$(CROSS_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@

$(CROSS_DIR)/gb_emu.o: src/gb_emu.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(CFLAGS) $(GB_INC) -c $< -o $@

$(CROSS_APU): third_party/minigb_apu/minigb_apu.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(TP_CFLAGS) -c $< -o $@

cross: $(CROSS_DIR)/litho-init $(CROSS_DIR)/litho-test $(CROSS_DIR)/litho-bench        $(CROSS_DIR)/litho-gb $(CROSS_DIR)/litho-gb-test

$(CROSS_DIR)/litho-init: $(CROSS_OBJ) $(CROSS_APU) src/init.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(CFLAGS) $(GB_INC) -static -o $@ src/init.c $(CROSS_OBJ) $(CROSS_APU) $(PTHREAD)

$(CROSS_DIR)/litho-test: $(CROSS_DIR)/handshake.o tests/test_handshake.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(CFLAGS) -static -o $@ tests/test_handshake.c $(CROSS_DIR)/handshake.o $(PTHREAD)

$(CROSS_DIR)/litho-bench: $(CROSS_DIR)/handshake.o $(CROSS_DIR)/scoreboard.o tests/bench_jitter.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(CFLAGS) -static -o $@ tests/bench_jitter.c $(CROSS_DIR)/handshake.o $(CROSS_DIR)/scoreboard.o $(PTHREAD)

$(CROSS_DIR)/litho-gb: $(CROSS_OBJ) $(CROSS_APU) src/gb_host.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(CFLAGS) $(GB_INC) -static -o $@ src/gb_host.c $(CROSS_OBJ) $(CROSS_APU) $(PTHREAD)

$(CROSS_DIR)/litho-gb-test: $(CROSS_DIR)/handshake.o $(CROSS_DIR)/gb_emu.o $(CROSS_DIR)/fb.o $(CROSS_DIR)/font.o $(CROSS_DIR)/pngenc.o $(CROSS_APU) tests/test_gb_handshake.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(CFLAGS) $(GB_INC) -static -o $@ tests/test_gb_handshake.c $(CROSS_DIR)/handshake.o $(CROSS_DIR)/gb_emu.o $(CROSS_DIR)/fb.o $(CROSS_DIR)/font.o $(CROSS_DIR)/pngenc.o $(CROSS_APU) $(PTHREAD)

verify-cross: cross
	file $(CROSS_DIR)/litho-init $(CROSS_DIR)/litho-test $(CROSS_DIR)/litho-bench $(CROSS_DIR)/litho-gb-test
	ls -l $(CROSS_DIR)/litho-init $(CROSS_DIR)/litho-test $(CROSS_DIR)/litho-bench $(CROSS_DIR)/litho-gb $(CROSS_DIR)/litho-gb-test
	@if command -v qemu-aarch64-static >/dev/null 2>&1; then \
		echo "qemu-aarch64-static litho-test:"; \
		qemu-aarch64-static $(CROSS_DIR)/litho-test; \
		echo "qemu-aarch64-static litho-gb-test:"; \
		qemu-aarch64-static $(CROSS_DIR)/litho-gb-test; \
	elif command -v qemu-aarch64 >/dev/null 2>&1; then \
		echo "qemu-aarch64 litho-test:"; \
		qemu-aarch64 $(CROSS_DIR)/litho-test; \
		echo "qemu-aarch64 litho-gb-test:"; \
		qemu-aarch64 $(CROSS_DIR)/litho-gb-test; \
	else \
		echo "qemu-aarch64 not installed; link verified only"; \
	fi

clean:
	rm -rf build
