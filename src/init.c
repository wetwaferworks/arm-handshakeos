/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_battery.h"
#include "hs_fb.h"
#include "hs_handshake.h"
#include "hs_input.h"
#include "hs_scoreboard.h"
#include "hs_ui.h"
#include "hs_gb.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  --backend=auto|drm|fbdev|headless\n"
            "  --theme=purple|green\n"
            "  --snapshot=PATH.png\n"
            "  --snapshot-both[=DIR]   write preview-purple.png and preview-green.png\n"
            "  --workers=N             default 3 (the 3+1 seating)\n"
            "  --as-init               mount proc/sys/dev (when pid 1)\n"
            "  --once                  run the scoreboard, draw one frame, exit\n"
            "  --serial-scoreboard     print handshake (and GB) ledger to console\n"
            "  --rom=PATH.gb           optional ROM for a PID-1 / serial GB run\n",
            argv0);
}

static hs_theme_id_t parse_theme(const char *s)
{
    if (s && (strcmp(s, "green") == 0 || strcmp(s, "matrix") == 0)) {
        return HS_THEME_GREEN;
    }
    return HS_THEME_PURPLE;
}

static int mount_essentials(void)
{
    mkdir("/proc", 0755);
    mkdir("/sys", 0755);
    mkdir("/dev", 0755);
    (void)mount("proc", "/proc", "proc", 0, NULL);
    (void)mount("sysfs", "/sys", "sysfs", 0, NULL);
    (void)mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    return 0;
}


static void say(const char *msg)
{
    FILE *c = fopen("/dev/console", "w");

    /* One stream only: on virt, stdout, stderr, and /dev/console are the
       same UART, and writing all three triples the banner. */
    if (c != NULL) {
        fputs(msg, c);
        fflush(c);
        fclose(c);
        return;
    }
    fputs(msg, stdout);
    fflush(stdout);
}


/* Live on-glass frame timing (H700 or host): a 128-frame ring of loop
   periods, summarised every 30 frames. Single-threaded, no locks. */
#define LIVE_RING 128
struct live_timing {
    uint64_t frames;
    int64_t  last_ns;
    int64_t  ring[LIVE_RING];
    int      n;
    int      pos;
    int64_t  p50_ns;
    int64_t  p99_ns;
    int64_t  max_ns;
};

static int64_t mono_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000ll + (int64_t)ts.tv_nsec;
}

static int cmp_i64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a;
    int64_t y = *(const int64_t *)b;

    return (x > y) - (x < y);
}

static void live_tick(struct live_timing *lt)
{
    int64_t now = mono_ns();

    if (lt->last_ns != 0) {
        lt->ring[lt->pos] = now - lt->last_ns;
        lt->pos = (lt->pos + 1) % LIVE_RING;
        if (lt->n < LIVE_RING) {
            lt->n++;
        }
    }
    lt->last_ns = now;
    lt->frames++;
    if (lt->n > 0 && (lt->frames % 30u) == 0) {
        int64_t tmp[LIVE_RING];
        int k = lt->n;

        memcpy(tmp, lt->ring, sizeof(int64_t) * (size_t)k);
        qsort(tmp, (size_t)k, sizeof(int64_t), cmp_i64);
        lt->p50_ns = tmp[k / 2];
        lt->p99_ns = tmp[(k * 99) / 100 < k ? (k * 99) / 100 : k - 1];
        lt->max_ns = tmp[k - 1];
    }
}

/* Right-hand column: 160x144 GB demo bezel plus live timing. */
static void draw_gb_panel(struct hs_fb *fb, const struct hs_ui *ui,
                          struct hs_gb *gb, const struct live_timing *lt)
{
    static const uint32_t gbp[4] = {
        0x00f4e8ffu, 0x00c084e0u, 0x00703aa8u, 0x001a0828u
    };
    const struct hs_palette *pal = hs_theme_get(ui->theme);
    struct hs_gb_stats gs;
    int px = fb->w - 12 - 172;
    int py = 40 + 12;
    int gx = px + 6;
    int gy = py + 20;

    hs_gb_stats(gb, &gs);
    hs_fb_fill_round(fb, px + 2, py + 2, 172, 196, 8, pal->shadow);
    hs_fb_fill_round(fb, px, py, 172, 196, 8, pal->tile);
    hs_fb_rect(fb, px, py, 172, 196, 1, pal->border);
    hs_fb_text(fb, px + 8, py + 6, 1, pal->text, "GB DEMO  LIVE");
    hs_fb_fill(fb, gx - 2, gy - 2, HS_GB_W + 4, HS_GB_H + 4, pal->border);
    hs_gb_blit(fb, gx, gy, 1, hs_gb_framebuffer(gb), gbp);
    if (lt != NULL && lt->n > 0) {
        hs_fb_printf(fb, px + 8, gy + HS_GB_H + 6, 1, pal->value,
                        "f%llu p50 %lld.%02lldms",
                        (unsigned long long)gs.frames,
                        (long long)(lt->p50_ns / 1000000),
                        (long long)((lt->p50_ns / 10000) % 100));
        hs_fb_printf(fb, px + 8, gy + HS_GB_H + 18, 1, pal->dim,
                        "p99 %lld.%02lldms xr%llu ov%llu",
                        (long long)(lt->p99_ns / 1000000),
                        (long long)((lt->p99_ns / 10000) % 100),
                        (unsigned long long)gs.xruns,
                        (unsigned long long)gs.overruns);
    } else {
        hs_fb_printf(fb, px + 8, gy + HS_GB_H + 6, 1, pal->value,
                        "frames %llu", (unsigned long long)gs.frames);
        hs_fb_printf(fb, px + 8, gy + HS_GB_H + 18, 1, pal->dim,
                        "xruns %llu  ovr %llu",
                        (unsigned long long)gs.xruns,
                        (unsigned long long)gs.overruns);
    }
}

static int cmdline_has(const char *key)
{
    char buf[4096];
    FILE *fp = fopen("/proc/cmdline", "r");
    size_t n;

    if (fp == NULL) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    if (n == 0) {
        return 0;
    }
    buf[n] = '\0';
    return strstr(buf, key) != NULL;
}

static void do_power(hs_action_t act, int as_init)
{
    if (!as_init) {
        fprintf(stderr, "host: %s requested, exiting\n",
                act == HS_ACTION_REBOOT ? "reboot" : "poweroff");
        exit(0);
    }
    sync();
    if (act == HS_ACTION_REBOOT) {
        reboot(RB_AUTOBOOT);
    } else {
        reboot(RB_POWER_OFF);
    }
}

static int render_snapshot(struct hs_fb *fb, struct hs_ui *ui,
                           struct hs_hs *hs, const struct hs_jitter_stats *jit,
                           const struct hs_battery *bat, struct hs_gb *gb,
                           const char *path)
{
    int rc;

    hs_ui_draw(fb, ui, hs, jit, bat);
    if (gb != NULL) {
        draw_gb_panel(fb, ui, gb, NULL);
    }
    hs_fb_flip(fb);
    rc = hs_fb_snapshot_png(fb, path);
    if (rc != 0) {
        fprintf(stderr, "snapshot %s failed: %d\n", path, rc);
        return rc;
    }
    fprintf(stderr, "wrote %s\n", path);
    return 0;
}

int main(int argc, char **argv)
{
    const char *backend = "auto";
    const char *snapshot = NULL;
    const char *snap_dir = NULL;
    int snap_both = 0;
    int as_init = 0;
    int once = 0;
    int serial_sb = 0;
    int workers = 3;
    const char *rom = NULL;
    hs_theme_id_t theme = HS_THEME_PURPLE;
    int i;
    struct hs_fb fb;
    struct hs_ui ui;
    struct hs_hs hs;
    struct hs_input in;
    struct hs_battery bat;
    struct hs_jitter_stats jit;
    uint64_t hash;
    int running = 1;
    struct hs_gb *gb = NULL;
    struct live_timing lt;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--backend=", 10) == 0) {
            backend = argv[i] + 10;
        } else if (strncmp(argv[i], "--theme=", 8) == 0) {
            theme = parse_theme(argv[i] + 8);
        } else if (strncmp(argv[i], "--snapshot=", 11) == 0) {
            snapshot = argv[i] + 11;
            once = 1;
        } else if (strcmp(argv[i], "--snapshot-both") == 0) {
            snap_both = 1;
            snap_dir = ".";
            once = 1;
        } else if (strncmp(argv[i], "--snapshot-both=", 16) == 0) {
            snap_both = 1;
            snap_dir = argv[i] + 16;
            once = 1;
        } else if (strncmp(argv[i], "--workers=", 10) == 0) {
            workers = atoi(argv[i] + 10);
        } else if (strcmp(argv[i], "--as-init") == 0) {
            as_init = 1;
        } else if (strcmp(argv[i], "--once") == 0) {
            once = 1;
        } else if (strcmp(argv[i], "--serial-scoreboard") == 0) {
            serial_sb = 1;
        } else if (strncmp(argv[i], "--rom=", 6) == 0) {
            rom = argv[i] + 6;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (as_init || getpid() == 1) {
        as_init = 1;
        mount_essentials();
        if (cmdline_has("handshakeos.serial") || cmdline_has("hs.serial") ||
            cmdline_has("handshakeos.once") || cmdline_has("hs.once")) {
            serial_sb = 1;
        }
        if (cmdline_has("handshakeos.once") || cmdline_has("hs.once")) {
            once = 1;
        }
        if (rom == NULL) {
            /* Prefer a packed legal demo ROM when we are PID 1. */
            if (access("/roms/handshake-demo.gb", R_OK) == 0) {
                rom = "/roms/handshake-demo.gb";
            } else if (access("roms/handshake-demo.gb", R_OK) == 0) {
                rom = "roms/handshake-demo.gb";
            }
        }
        serial_sb = 1;
    }

    /* Host snapshots always go through the headless backend so we get a
       crisp 640x480 frame, even on a machine with a different display. */
    if (snapshot || snap_both) {
        backend = "headless";
    }

    if (hs_fb_open(&fb, backend) != 0) {
        fprintf(stderr, "fb open (%s) failed, trying headless\n", backend);
        if (hs_fb_open(&fb, "headless") != 0) {
            fprintf(stderr, "headless fb open failed\n");
            return 1;
        }
    }

    if (hs_scoreboard_setup(&hs, workers) != 0) {
        fprintf(stderr, "handshake setup failed\n");
        hs_fb_close(&fb);
        return 1;
    }
    hash = hs_scoreboard_run(&hs);
    hs_hs_jitter(&hs, &jit);
    fprintf(stderr, "ledger %016llx  epoch %llu  workers %d  p50 %lld ns  overruns %llu\n",
            (unsigned long long)hash,
            (unsigned long long)hs_hs_commit_epoch(&hs),
            hs_hs_worker_count(&hs),
            (long long)jit.p50_ns,
            (unsigned long long)jit.overruns);

    if (serial_sb) {
        char line[512];
        struct hs_gb_stats gs;

        snprintf(line, sizeof(line),
                 "\n======== HandshakeOS PID %d ========\n"
                 "Timing Handshake 3+1\n"
                 "ledger %016llx\n"
                 "epoch  %llu\n"
                 "workers %d\n"
                 "overruns %llu\n",
                 (int)getpid(),
                 (unsigned long long)hash,
                 (unsigned long long)hs_hs_commit_epoch(&hs),
                 hs_hs_worker_count(&hs),
                 (unsigned long long)jit.overruns);
        say(line);

        if (rom != NULL) {
            memset(&gs, 0, sizeof(gs));
            if (hs_gb_scoreboard(rom, workers, 90, &gs) == 0) {
                snprintf(line, sizeof(line),
                         "GB rom %s\n"
                         "GB ledger %016llx\n"
                         "GB frame  %016llx\n"
                         "GB audio  %016llx\n"
                         "GB xruns  %llu\n"
                         "GB overruns %llu\n",
                         rom,
                         (unsigned long long)gs.ledger_hash,
                         (unsigned long long)gs.frame_crc,
                         (unsigned long long)gs.audio_crc,
                         (unsigned long long)gs.xruns,
                         (unsigned long long)gs.overruns);
                say(line);
            } else {
                say("GB scoreboard failed\n");
            }
        }
        say("================================\n");
        if (as_init && once) {
            /* QEMU proof: print, then reboot so -no-reboot exits cleanly. */
            sync();
            reboot(RB_AUTOBOOT);
        }
    }

    hs_ui_init(&ui, theme);
    hs_battery_clear(&bat);
    hs_battery_read(&bat);
    hs_input_open(&in);

    if (snap_both) {
        char path[512];
        int rc = 0;
        ui.theme = HS_THEME_PURPLE;
        snprintf(path, sizeof(path), "%s/preview-purple.png", snap_dir[0] ? snap_dir : ".");
        rc |= render_snapshot(&fb, &ui, &hs, &jit, &bat, NULL, path);
        ui.theme = HS_THEME_GREEN;
        snprintf(path, sizeof(path), "%s/preview-green.png", snap_dir[0] ? snap_dir : ".");
        rc |= render_snapshot(&fb, &ui, &hs, &jit, &bat, NULL, path);
        hs_input_close(&in);
        hs_hs_fini(&hs);
        hs_fb_close(&fb);
        return rc == 0 ? 0 : 1;
    }

    /* Live glass: keep the Deep Purple shell up and run the legal demo
       ROM unless handshakeos.once / --once asked us to print-and-exit. A host
       --snapshot with --rom renders the GB column after 90 frames. */
    memset(&lt, 0, sizeof(lt));
    if (rom != NULL && (!once || snapshot != NULL)) {
        gb = hs_gb_create();
        if (gb == NULL || hs_gb_load_rom(gb, rom) != 0 ||
            hs_gb_start(gb, workers) != 0) {
            if (gb != NULL) {
                hs_gb_destroy(gb);
                gb = NULL;
            }
            fprintf(stderr, "GB demo unavailable (%s)\n", rom);
        } else {
            ui.reserve_gb = 1;
            if (snapshot != NULL) {
                (void)hs_gb_run_frames(gb, 90);
            }
        }
    }

    if (snapshot) {
        int rc = render_snapshot(&fb, &ui, &hs, &jit, &bat, gb, snapshot);
        if (gb != NULL) {
            hs_gb_stop(gb);
            hs_gb_destroy(gb);
        }
        hs_input_close(&in);
        hs_hs_fini(&hs);
        hs_fb_close(&fb);
        return rc == 0 ? 0 : 1;
    }

    while (running) {
        hs_action_t act = HS_ACTION_NONE;

        {
            /* ~60 Hz pacing: wait for input only until the next frame slot. */
            int64_t slot = 16742706; /* 1/59.73 s, the DMG refresh */
            int64_t wait = lt.last_ns ? slot - (mono_ns() - lt.last_ns) : slot;
            hs_input_poll(&in, wait > 0 ? (int)(wait / 1000000) : 0);
        }
        if (hs_input_pressed(&in, HS_BTN_L1) || hs_input_pressed(&in, HS_BTN_LEFT)) {
            if (hs_input_pressed(&in, HS_BTN_L1)) {
                hs_ui_cycle_theme(&ui, -1);
            } else {
                act = hs_ui_move(&ui, -1, 0);
            }
        }
        if (hs_input_pressed(&in, HS_BTN_R1) || hs_input_pressed(&in, HS_BTN_RIGHT)) {
            if (hs_input_pressed(&in, HS_BTN_R1)) {
                hs_ui_cycle_theme(&ui, 1);
            } else {
                act = hs_ui_move(&ui, 1, 0);
            }
        }
        if (hs_input_pressed(&in, HS_BTN_UP)) {
            act = hs_ui_move(&ui, 0, -1);
        }
        if (hs_input_pressed(&in, HS_BTN_DOWN)) {
            act = hs_ui_move(&ui, 0, 1);
        }
        if (hs_input_pressed(&in, HS_BTN_A)) {
            act = hs_ui_activate(&ui);
        }
        if (hs_input_pressed(&in, HS_BTN_B) || hs_input_pressed(&in, HS_BTN_MENU)) {
            act = hs_ui_cancel(&ui);
        }
        if (hs_input_pressed(&in, HS_BTN_POWER)) {
            /* First tap asks, second tap (or A) powers off. */
            ui.selected = HS_TILE_POWEROFF;
            act = hs_ui_activate(&ui);
        }
        /* SELECT+START: clean reboot straight away (no confirm). */
        if (hs_input_down(&in, HS_BTN_SELECT) &&
            hs_input_down(&in, HS_BTN_START)) {
            act = HS_ACTION_REBOOT;
        }
        if (act == HS_ACTION_POWEROFF || act == HS_ACTION_REBOOT) {
            if (gb != NULL) {
                hs_gb_stop(gb);
                hs_gb_destroy(gb);
                gb = NULL;
            }
            do_power(act, as_init);
            running = 0;
        }

        if (!running) {
            break;
        }
        hs_battery_read(&bat);
        if (gb != NULL) {
            /* Dial walks PUB->PULL->BAR->CMIT once per 4 live frames. */
            ui.live_beat = (hs_beat_t)(1 + (int)(lt.frames % 4u));
        }
        hs_ui_draw(&fb, &ui, &hs, &jit, &bat);
        if (gb != NULL) {
            (void)hs_gb_run_frames(gb, 1);
            draw_gb_panel(&fb, &ui, gb, &lt);
        }
        live_tick(&lt);
        hs_fb_flip(&fb);

        if (once) {
            running = 0;
        }
    }

    if (gb != NULL) {
        hs_gb_stop(gb);
        hs_gb_destroy(gb);
    }
    hs_input_close(&in);
    hs_hs_fini(&hs);
    hs_fb_close(&fb);
    return 0;
}
