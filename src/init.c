/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "litho_battery.h"
#include "litho_fb.h"
#include "litho_handshake.h"
#include "litho_input.h"
#include "litho_scoreboard.h"
#include "litho_ui.h"
#include "litho_gb.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
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

static litho_theme_id_t parse_theme(const char *s)
{
    if (s && (strcmp(s, "green") == 0 || strcmp(s, "matrix") == 0)) {
        return LITHO_THEME_GREEN;
    }
    return LITHO_THEME_PURPLE;
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

static void do_power(litho_action_t act, int as_init)
{
    if (!as_init) {
        fprintf(stderr, "host: %s requested, exiting\n",
                act == LITHO_ACTION_REBOOT ? "reboot" : "poweroff");
        exit(0);
    }
    sync();
    if (act == LITHO_ACTION_REBOOT) {
        reboot(RB_AUTOBOOT);
    } else {
        reboot(RB_POWER_OFF);
    }
}

static int render_snapshot(struct litho_fb *fb, struct litho_ui *ui,
                           struct litho_hs *hs, const struct litho_jitter_stats *jit,
                           const struct litho_battery *bat, const char *path)
{
    int rc;

    litho_ui_draw(fb, ui, hs, jit, bat);
    litho_fb_flip(fb);
    rc = litho_fb_snapshot_png(fb, path);
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
    litho_theme_id_t theme = LITHO_THEME_PURPLE;
    int i;
    struct litho_fb fb;
    struct litho_ui ui;
    struct litho_hs hs;
    struct litho_input in;
    struct litho_battery bat;
    struct litho_jitter_stats jit;
    uint64_t hash;
    int running = 1;
    struct litho_gb *gb = NULL;
    const uint32_t gbp[4] = {
        0x00f4e8ffu,
        0x00c084e0u,
        0x00703aa8u,
        0x001a0828u
    };

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
        if (cmdline_has("litho.serial") || cmdline_has("litho.once")) {
            serial_sb = 1;
        }
        if (cmdline_has("litho.once")) {
            once = 1;
        }
        if (rom == NULL) {
            /* Prefer a packed legal demo ROM when we are PID 1. */
            if (access("/roms/litho-demo.gb", R_OK) == 0) {
                rom = "/roms/litho-demo.gb";
            } else if (access("roms/litho-demo.gb", R_OK) == 0) {
                rom = "roms/litho-demo.gb";
            }
        }
        serial_sb = 1;
    }

    /* Host snapshots always go through the headless backend so we get a
       crisp 640x480 frame, even on a machine with a different display. */
    if (snapshot || snap_both) {
        backend = "headless";
    }

    if (litho_fb_open(&fb, backend) != 0) {
        fprintf(stderr, "fb open (%s) failed, trying headless\n", backend);
        if (litho_fb_open(&fb, "headless") != 0) {
            fprintf(stderr, "headless fb open failed\n");
            return 1;
        }
    }

    if (litho_scoreboard_setup(&hs, workers) != 0) {
        fprintf(stderr, "handshake setup failed\n");
        litho_fb_close(&fb);
        return 1;
    }
    hash = litho_scoreboard_run(&hs);
    litho_hs_jitter(&hs, &jit);
    fprintf(stderr, "ledger %016llx  epoch %llu  workers %d  p50 %lld ns  overruns %llu\n",
            (unsigned long long)hash,
            (unsigned long long)litho_hs_commit_epoch(&hs),
            litho_hs_worker_count(&hs),
            (long long)jit.p50_ns,
            (unsigned long long)jit.overruns);

    if (serial_sb) {
        char line[512];
        struct litho_gb_stats gs;

        snprintf(line, sizeof(line),
                 "\n======== LithoOS PID %d ========\n"
                 "Timing Handshake 3+1\n"
                 "ledger %016llx\n"
                 "epoch  %llu\n"
                 "workers %d\n"
                 "overruns %llu\n",
                 (int)getpid(),
                 (unsigned long long)hash,
                 (unsigned long long)litho_hs_commit_epoch(&hs),
                 litho_hs_worker_count(&hs),
                 (unsigned long long)jit.overruns);
        say(line);

        if (rom != NULL) {
            memset(&gs, 0, sizeof(gs));
            if (litho_gb_scoreboard(rom, workers, 90, &gs) == 0) {
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

    litho_ui_init(&ui, theme);
    litho_battery_clear(&bat);
    litho_battery_read(&bat);
    litho_input_open(&in);

    if (snap_both) {
        char path[512];
        int rc = 0;
        ui.theme = LITHO_THEME_PURPLE;
        snprintf(path, sizeof(path), "%s/preview-purple.png", snap_dir[0] ? snap_dir : ".");
        rc |= render_snapshot(&fb, &ui, &hs, &jit, &bat, path);
        ui.theme = LITHO_THEME_GREEN;
        snprintf(path, sizeof(path), "%s/preview-green.png", snap_dir[0] ? snap_dir : ".");
        rc |= render_snapshot(&fb, &ui, &hs, &jit, &bat, path);
        litho_input_close(&in);
        litho_hs_fini(&hs);
        litho_fb_close(&fb);
        return rc == 0 ? 0 : 1;
    }

    if (snapshot) {
        int rc = render_snapshot(&fb, &ui, &hs, &jit, &bat, snapshot);
        litho_input_close(&in);
        litho_hs_fini(&hs);
        litho_fb_close(&fb);
        return rc == 0 ? 0 : 1;
    }

    /* Live glass: keep the Deep Purple shell up and blit the legal demo
       ROM unless litho.once / --once asked us to print-and-exit. */
    if (rom != NULL && !once) {
        gb = litho_gb_create();
        if (gb == NULL || litho_gb_load_rom(gb, rom) != 0 ||
            litho_gb_start(gb, workers) != 0) {
            if (gb != NULL) {
                litho_gb_destroy(gb);
                gb = NULL;
            }
        } else {
            ui.reserve_gb = 1;
        }
    }

    while (running) {
        litho_action_t act = LITHO_ACTION_NONE;

        litho_input_poll(&in, 16);
        if (litho_input_pressed(&in, LITHO_BTN_L1) || litho_input_pressed(&in, LITHO_BTN_LEFT)) {
            if (litho_input_pressed(&in, LITHO_BTN_L1)) {
                litho_ui_cycle_theme(&ui, -1);
            } else {
                act = litho_ui_move(&ui, -1, 0);
            }
        }
        if (litho_input_pressed(&in, LITHO_BTN_R1) || litho_input_pressed(&in, LITHO_BTN_RIGHT)) {
            if (litho_input_pressed(&in, LITHO_BTN_R1)) {
                litho_ui_cycle_theme(&ui, 1);
            } else {
                act = litho_ui_move(&ui, 1, 0);
            }
        }
        if (litho_input_pressed(&in, LITHO_BTN_UP)) {
            act = litho_ui_move(&ui, 0, -1);
        }
        if (litho_input_pressed(&in, LITHO_BTN_DOWN)) {
            act = litho_ui_move(&ui, 0, 1);
        }
        if (litho_input_pressed(&in, LITHO_BTN_A)) {
            act = litho_ui_activate(&ui);
        }
        if (litho_input_pressed(&in, LITHO_BTN_B) || litho_input_pressed(&in, LITHO_BTN_MENU)) {
            act = litho_ui_cancel(&ui);
        }
        if (litho_input_pressed(&in, LITHO_BTN_POWER)) {
            ui.selected = LITHO_TILE_POWEROFF;
            act = litho_ui_activate(&ui);
        }
        if (act == LITHO_ACTION_POWEROFF || act == LITHO_ACTION_REBOOT) {
            do_power(act, as_init);
            running = 0;
        }

        litho_battery_read(&bat);
        litho_ui_draw(&fb, &ui, &hs, &jit, &bat);
        if (gb != NULL) {
            int gx = fb.w - 12 - 176 + 8;
            int gy = 40 + 12 + 16;
            (void)litho_gb_run_frames(gb, 1);
            litho_gb_blit(&fb, gx, gy, 1, litho_gb_framebuffer(gb), gbp);
            litho_fb_printf(&fb, gx, gy + LITHO_GB_H + 4, 1,
                            litho_theme_get(ui.theme)->dim, "GB DEMO");
        }
        litho_fb_flip(&fb);

        if (once) {
            running = 0;
        }
    }

    if (gb != NULL) {
        litho_gb_stop(gb);
        litho_gb_destroy(gb);
    }
    litho_input_close(&in);
    litho_hs_fini(&hs);
    litho_fb_close(&fb);
    return 0;
}
