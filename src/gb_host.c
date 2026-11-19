/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "litho_fb.h"
#include "litho_gb.h"
#include "litho_theme.h"
#include "litho_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  --rom=PATH.gb           default roms/litho-demo.gb\n"
            "  --frames=N              default 90\n"
            "  --workers=N             default 3\n"
            "  --snapshot=PATH.png     Deep Purple shell + live GB frame\n"
            "  --scoreboard            100-run + 1/2/3/4 worker fairness\n"
            "  --runs=N                scoreboard runs, default 100\n",
            argv0);
}

static int render_gb_snapshot(const char *path, const struct litho_gb *g,
                              const struct litho_gb_stats *st)
{
    struct litho_fb fb;
    const struct litho_palette *pal;
    const uint32_t gbp[4] = {
        0x00f4e8ffu, /* light lilac */
        0x00c084e0u,
        0x00703aa8u,
        0x001a0828u  /* deep purple */
    };
    const int scale = 2;
    const int gw = LITHO_GB_W * scale;
    const int gh = LITHO_GB_H * scale;
    int gx, gy;
    litho_beat_t beat;
    int frozen;

    if (litho_fb_open(&fb, "headless") != 0) {
        fprintf(stderr, "headless fb open failed\n");
        return 1;
    }
    pal = litho_theme_get(LITHO_THEME_PURPLE);
    litho_fb_clear(&fb, pal->bg);

    litho_fb_fill(&fb, 0, 0, fb.w, 40, pal->bar);
    litho_fb_fill(&fb, 0, 38, fb.w, 2, pal->accent);
    litho_fb_text(&fb, 12, 12, 2, pal->accent2, "LITHOOS");
    litho_fb_printf(&fb, 180, 16, 1, pal->dim, "Deep Purple  +  Peanut-GB");
    litho_fb_printf(&fb, 500, 16, 1, pal->text, "DMG");

    /* Left status rail */
    litho_fb_fill_round(&fb, 12, 56, 200, 392, 8, pal->tile);
    litho_fb_rect(&fb, 12, 56, 200, 392, 1, pal->border);
    litho_fb_text(&fb, 24, 68, 1, pal->dim, "HANDSHAKE");
    litho_fb_text(&fb, 24, 84, 1, pal->text, "3+1 seating");
    litho_fb_text(&fb, 24, 112, 1, pal->dim, "EPOCH");
    litho_fb_printf(&fb, 24, 126, 2, pal->value, "%llu",
                    (unsigned long long)st->epoch);
    litho_fb_text(&fb, 24, 160, 1, pal->dim, "LEDGER");
    litho_fb_printf(&fb, 24, 174, 1, pal->accent2, "%016llx",
                    (unsigned long long)st->ledger_hash);
    litho_fb_text(&fb, 24, 200, 1, pal->dim, "FRAME CRC");
    litho_fb_printf(&fb, 24, 214, 1, pal->text, "%016llx",
                    (unsigned long long)st->frame_crc);
    litho_fb_text(&fb, 24, 240, 1, pal->dim, "AUDIO CRC");
    litho_fb_printf(&fb, 24, 254, 1, pal->text, "%016llx",
                    (unsigned long long)st->audio_crc);
    litho_fb_text(&fb, 24, 280, 1, pal->dim, "AUDIO XRUNS");
    litho_fb_printf(&fb, 24, 294, 2, st->xruns ? pal->warn : pal->good, "%llu",
                    (unsigned long long)st->xruns);
    litho_fb_text(&fb, 24, 330, 1, pal->dim, "OVERRUNS");
    litho_fb_printf(&fb, 24, 344, 2, st->overruns ? pal->warn : pal->good, "%llu",
                    (unsigned long long)st->overruns);
    litho_fb_text(&fb, 24, 380, 1, pal->dim, "WATCHDOG");
    litho_fb_text(&fb, 24, 394, 1, pal->good, "OK");
    litho_fb_printf(&fb, 24, 416, 1, pal->dim, "frames %llu",
                    (unsigned long long)st->frames);

    /* GB bezel, scale 2 = 320x288, sits to the right of the rail. */
    gx = 236;
    gy = 72;
    litho_fb_fill_round(&fb, gx - 16, gy - 24, gw + 32, gh + 56, 10, pal->surface);
    litho_fb_rect(&fb, gx - 16, gy - 24, gw + 32, gh + 56, 2, pal->accent);
    litho_fb_text(&fb, gx, gy - 18, 1, pal->dim, "LITHO DEMO  (MIT, original)");
    litho_gb_blit(&fb, gx, gy, scale, litho_gb_framebuffer(g), gbp);
    litho_fb_printf(&fb, gx, gy + gh + 8, 1, pal->text,
                    "xruns %llu   epoch %llu",
                    (unsigned long long)st->xruns,
                    (unsigned long long)st->epoch);

    /* Live Gemstone dial, lower-left of the bezel's right margin. */
    beat = LITHO_BEAT_COMMIT;
    frozen = 0;
    litho_ui_draw_gemstone(&fb, 560, 400, 48, pal, beat, frozen);

    litho_fb_fill(&fb, 0, fb.h - 28, fb.w, 28, pal->bar);
    litho_fb_printf(&fb, 12, fb.h - 20, 1, pal->dim,
                    "phase-boundary PCM  +  hash-chained ledger  +  zero mutexes");

    litho_fb_flip(&fb);
    if (litho_fb_snapshot_png(&fb, path) != 0) {
        fprintf(stderr, "snapshot %s failed\n", path);
        litho_fb_close(&fb);
        return 1;
    }
    fprintf(stderr, "wrote %s\n", path);
    litho_fb_close(&fb);
    return 0;
}

static int run_once(const char *rom, int workers, uint64_t frames,
                    struct litho_gb **keep, struct litho_gb_stats *st)
{
    struct litho_gb *g = litho_gb_create();
    uint64_t t;
    int rc;

    if (g == NULL) {
        return -1;
    }
    rc = litho_gb_load_rom(g, rom);
    if (rc != 0) {
        fprintf(stderr, "load %s failed: %d\n", rom, rc);
        litho_gb_destroy(g);
        return rc;
    }
    (void)litho_gb_script(g, 0, 0);
    for (t = 20; t < 40 && t < frames; t++) {
        (void)litho_gb_script(g, t, 0x10);
    }
    for (t = 40; t < 60 && t < frames; t++) {
        (void)litho_gb_script(g, t, 0x11);
    }
    for (t = 60; t < frames; t++) {
        (void)litho_gb_script(g, t, 0x80);
    }
    rc = litho_gb_start(g, workers);
    if (rc != 0) {
        fprintf(stderr, "start failed: %d\n", rc);
        litho_gb_destroy(g);
        return rc;
    }
    rc = litho_gb_run_frames(g, frames);
    litho_gb_stats(g, st);
    if (keep != NULL) {
        *keep = g;
        return rc;
    }
    litho_gb_stop(g);
    litho_gb_destroy(g);
    return rc;
}

static int run_scoreboard(const char *rom, uint64_t frames, int runs)
{
    struct litho_gb_stats first, st;
    int workers, i, rc;
    int fails = 0;

    memset(&first, 0, sizeof(first));
    printf("GB scoreboard  rom=%s  frames=%llu  runs=%d\n",
           rom, (unsigned long long)frames, runs);

    for (i = 0; i < runs; i++) {
        rc = run_once(rom, 3, frames, NULL, &st);
        if (rc != 0) {
            fprintf(stderr, "run %d failed rc=%d\n", i, rc);
            return 1;
        }
        if (i == 0) {
            first = st;
            printf("  golden  ledger=%016llx  frame=%016llx  audio=%016llx"
                   "  epoch=%llu  xruns=%llu  overruns=%llu\n",
                   (unsigned long long)st.ledger_hash,
                   (unsigned long long)st.frame_crc,
                   (unsigned long long)st.audio_crc,
                   (unsigned long long)st.epoch,
                   (unsigned long long)st.xruns,
                   (unsigned long long)st.overruns);
        } else if (st.ledger_hash != first.ledger_hash
                   || st.frame_crc != first.frame_crc
                   || st.audio_crc != first.audio_crc
                   || st.xruns != 0 || st.overruns != 0) {
            fprintf(stderr, "MISMATCH run %d ledger=%016llx frame=%016llx "
                            "audio=%016llx xruns=%llu overruns=%llu\n",
                    i,
                    (unsigned long long)st.ledger_hash,
                    (unsigned long long)st.frame_crc,
                    (unsigned long long)st.audio_crc,
                    (unsigned long long)st.xruns,
                    (unsigned long long)st.overruns);
            fails++;
        }
    }
    printf("  %d/%d identical ledger+frame+audio  xruns=%llu  overruns=%llu\n",
           runs - fails, runs,
           (unsigned long long)first.xruns,
           (unsigned long long)first.overruns);

    printf("  fairness:");
    for (workers = 1; workers <= 4; workers++) {
        rc = run_once(rom, workers, frames, NULL, &st);
        if (rc != 0 || st.ledger_hash != first.ledger_hash
            || st.frame_crc != first.frame_crc
            || st.audio_crc != first.audio_crc
            || st.xruns != 0 || st.overruns != 0) {
            printf("  %d=FAIL(%016llx xrun=%llu ov=%llu)", workers,
                   (unsigned long long)st.ledger_hash,
                   (unsigned long long)st.xruns,
                   (unsigned long long)st.overruns);
            fails++;
        } else {
            printf("  %d=ok", workers);
        }
    }
    printf("\n");
    return fails == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *rom = "roms/litho-demo.gb";
    const char *snapshot = NULL;
    int workers = 3;
    int scoreboard = 0;
    int runs = 100;
    uint64_t frames = 90;
    int i, rc;
    struct litho_gb *kept = NULL;
    struct litho_gb_stats st;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--rom=", 6) == 0) {
            rom = argv[i] + 6;
        } else if (strncmp(argv[i], "--frames=", 9) == 0) {
            frames = (uint64_t)strtoull(argv[i] + 9, NULL, 10);
        } else if (strncmp(argv[i], "--workers=", 10) == 0) {
            workers = atoi(argv[i] + 10);
        } else if (strncmp(argv[i], "--snapshot=", 11) == 0) {
            snapshot = argv[i] + 11;
        } else if (strcmp(argv[i], "--scoreboard") == 0) {
            scoreboard = 1;
        } else if (strncmp(argv[i], "--runs=", 7) == 0) {
            runs = atoi(argv[i] + 7);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (scoreboard) {
        return run_scoreboard(rom, frames, runs);
    }

    memset(&st, 0, sizeof(st));
    rc = run_once(rom, workers, frames, snapshot ? &kept : NULL, &st);
    if (rc != 0) {
        return 1;
    }
    printf("ledger %016llx  frame %016llx  audio %016llx  epoch %llu  "
           "xruns %llu  overruns %llu  frames %llu\n",
           (unsigned long long)st.ledger_hash,
           (unsigned long long)st.frame_crc,
           (unsigned long long)st.audio_crc,
           (unsigned long long)st.epoch,
           (unsigned long long)st.xruns,
           (unsigned long long)st.overruns,
           (unsigned long long)st.frames);
    if (snapshot) {
        rc = render_gb_snapshot(snapshot, kept, &st);
        litho_gb_stop(kept);
        litho_gb_destroy(kept);
        return rc;
    }
    return 0;
}
