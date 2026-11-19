/* SPDX-License-Identifier: MIT */
#ifndef LITHO_GB_INCLUDED
#define LITHO_GB_INCLUDED

#include "litho_handshake.h"

struct litho_fb;

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define LITHO_GB_W 160
#define LITHO_GB_H 144
#define LITHO_GB_SCRIPT_MAX 256

struct litho_gb_stats {
    uint64_t frames;
    uint64_t ledger_hash;
    uint64_t frame_crc;
    uint64_t audio_crc;
    uint64_t epoch;
    uint64_t monatomic;
    uint64_t xruns;
    uint64_t overruns;
    uint64_t phases;
};

struct litho_gb;

struct litho_gb *litho_gb_create(void);
void             litho_gb_destroy(struct litho_gb *g);

int  litho_gb_load_rom(struct litho_gb *g, const char *path);
int  litho_gb_load_rom_mem(struct litho_gb *g, const uint8_t *data, size_t len);

/* Scripted button event: absolute JOYPAD_* mask (Peanut-GB defines) applied at tick. */
int  litho_gb_script(struct litho_gb *g, uint64_t tick, uint8_t joy_mask);

int  litho_gb_start(struct litho_gb *g, int workers);
void litho_gb_stop(struct litho_gb *g);

/* Run `frames` handshake phases, one GB frame each. */
int  litho_gb_run_frames(struct litho_gb *g, uint64_t frames);

void litho_gb_stats(const struct litho_gb *g, struct litho_gb_stats *out);
const uint8_t *litho_gb_framebuffer(const struct litho_gb *g); /* 160*144, values 0-3 */
uint64_t       litho_gb_ledger_hash(const struct litho_gb *g);
uint64_t       litho_gb_xruns(const struct litho_gb *g);

/* Scoreboard helpers: default script on litho-demo.gb, `frames` default 90. */
int litho_gb_scoreboard(const char *rom_path, int workers, uint64_t frames,
                        struct litho_gb_stats *out);

void litho_gb_blit(struct litho_fb *fb, int x, int y, int scale,
                   const uint8_t *fb160, const uint32_t pal[4]);

#endif
