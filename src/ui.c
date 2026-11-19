/* SPDX-License-Identifier: MIT */
#include "hs_ui.h"
#include "hs_fb.h"
#include "hs_font.h"

#include <stdio.h>
#include <string.h>

void hs_ui_init(struct hs_ui *ui, hs_theme_id_t theme)
{
    if (ui == NULL) {
        return;
    }
    memset(ui, 0, sizeof(*ui));
    ui->theme = theme;
    ui->selected = HS_TILE_THEME;
    ui->confirm = HS_CONFIRM_NONE;
    ui->status_line = NULL;
}

static void format_ns(char *buf, size_t n, int64_t ns)
{
    if (ns < 0) {
        ns = -ns;
    }
    if (ns < 1000) {
        snprintf(buf, n, "%lld ns", (long long)ns);
    } else if (ns < 1000000) {
        snprintf(buf, n, "%lld us", (long long)((ns + 500) / 1000));
    } else if (ns < 1000000000) {
        snprintf(buf, n, "%lld.%02d ms",
                 (long long)(ns / 1000000),
                 (int)(((ns % 1000000) + 5000) / 10000));
    } else {
        snprintf(buf, n, "%lld ms", (long long)((ns + 500000) / 1000000));
    }
}

/* Diamond: top=publish, right=pull, bottom=barrier, left=commit. */
static void draw_gemstone(struct hs_fb *fb, int cx, int cy, int r,
                          const struct hs_palette *pal, hs_beat_t beat, int frozen)
{
    const int verts[4][2] = {
        { 0, -r },          /* publish  (top)    */
        { r,  0 },          /* pull     (right)  */
        { 0,  r },          /* barrier  (bottom) */
        { -r, 0 }           /* commit   (left)   */
    };
    const char *labs[4] = { "PUB", "PULL", "BAR", "CMIT" };
    int i, t, x0, y0, x1, y1;
    hs_beat_t lit = beat;

    if (lit == HS_BEAT_IDLE) {
        lit = HS_BEAT_COMMIT; /* settled after a successful phase */
    }

    /* Draw the four edges a few pixels thick. */
    for (i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        int dx, dy, steps, s;
        uint32_t col = pal->border;

        if ((hs_beat_t)(i + 1) == lit || (hs_beat_t)(j + 1) == lit) {
            col = frozen ? pal->warn : pal->accent;
        }
        x0 = cx + verts[i][0];
        y0 = cy + verts[i][1];
        x1 = cx + verts[j][0];
        y1 = cy + verts[j][1];
        dx = x1 - x0;
        dy = y1 - y0;
        steps = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (steps < 1) {
            steps = 1;
        }
        for (s = 0; s <= steps; s++) {
            int x = x0 + dx * s / steps;
            int y = y0 + dy * s / steps;
            hs_fb_fill(fb, x - 1, y - 1, 3, 3, col);
        }
    }

    for (i = 0; i < 4; i++) {
        int vx = cx + verts[i][0];
        int vy = cy + verts[i][1];
        int on = ((hs_beat_t)(i + 1) == lit);
        uint32_t col = on ? (frozen ? pal->warn : pal->accent2) : pal->dim;
        int rad = on ? 7 : 5;
        hs_fb_fill_circle(fb, vx, vy, rad, col);
        /* Labels just outside each vertex. */
        {
            int tx = vx, ty = vy;
            int tw = hs_font_text_width(labs[i], 1);
            if (i == 0) { tx -= tw / 2; ty -= 20; }
            if (i == 1) { tx += 10;     ty -= 4;  }
            if (i == 2) { tx -= tw / 2; ty += 12; }
            if (i == 3) { tx -= tw + 10; ty -= 4; }
            /* Never let a label (e.g. PULL) run off the right edge. */
            if (tx + tw > fb->w - 4) {
                tx = fb->w - 4 - tw;
            }
            if (tx < 0) {
                tx = 0;
            }
            hs_fb_text(fb, tx, ty, 1, on ? pal->text : pal->dim, labs[i]);
        }
        (void)t;
    }
}

void hs_ui_draw_gemstone(struct hs_fb *fb, int cx, int cy, int radius,
                            const struct hs_palette *pal,
                            hs_beat_t beat, int frozen)
{
    draw_gemstone(fb, cx, cy, radius, pal, beat, frozen);
}

static void draw_battery_icon(struct hs_fb *fb, int x, int y, int w, int h,
                              int percent, const struct hs_palette *pal)
{
    int inner, fillw;
    uint32_t col = pal->value;

    hs_fb_rect(fb, x, y, w - 3, h, 1, pal->text);
    hs_fb_fill(fb, x + w - 3, y + 3, 3, h - 6, pal->text);
    if (percent < 0) {
        return;
    }
    if (percent <= 20) {
        col = pal->warn;
    } else if (percent >= 80) {
        col = pal->good;
    }
    inner = w - 7;
    fillw = inner * percent / 100;
    if (fillw < 0) {
        fillw = 0;
    }
    hs_fb_fill(fb, x + 2, y + 2, fillw, h - 4, col);
}

void hs_ui_draw(struct hs_fb *fb,
                      const struct hs_ui *ui,
                      const struct hs_hs *hs,
                      const struct hs_jitter_stats *stats,
                      const struct hs_battery *bat)
{
    const struct hs_palette *pal;
    const int bar_h = 40;
    const int foot_h = 32;
    const int margin = 12;
    int hero_x, hero_y, hero_w, hero_h;
    int tile_y, tile_w, tile_h, tile_x, gap = 10;
    int i;
    char p50[32], p95[32], p99[32], pmax[32];
    hs_beat_t beat;
    int frozen;
    uint64_t epoch, hash;
    const char *wdog;
    const char *titles[HS_TILE_COUNT];
    hs_tile_t t;

    if (fb == NULL || ui == NULL) {
        return;
    }
    pal = hs_theme_get(ui->theme);
    hs_fb_clear(fb, pal->bg);

    /* Top bar */
    hs_fb_fill(fb, 0, 0, fb->w, bar_h, pal->bar);
    hs_fb_fill(fb, 0, bar_h - 2, fb->w, 2, pal->accent);
    hs_fb_text(fb, 12, 12, 2, pal->accent2, "HandshakeOS");
    hs_fb_printf(fb, 200, 16, 1, pal->dim, "Timing Handshake");
    hs_fb_printf(fb, 400, 16, 1, pal->text, "%s", pal->short_name);
    {
        int bp = (bat && bat->present && bat->percent >= 0) ? bat->percent : -1;
        draw_battery_icon(fb, 560, 12, 36, 16, bp, pal);
        if (bp >= 0) {
            hs_fb_printf(fb, 600, 14, 1, pal->text, "%d%%", bp);
        } else {
            hs_fb_text(fb, 600, 14, 1, pal->dim, "--");
        }
    }

    hero_x = margin;
    hero_y = bar_h + margin;
    /* When reserve_gb is set, leave a 176px column on the right for a 160x144 bezel. */
    hero_w = fb->w - 2 * margin - (ui->reserve_gb ? 184 : 0);
    hero_h = 196;
    hs_fb_fill_round(fb, hero_x + 2, hero_y + 2, hero_w, hero_h, 8, pal->shadow);
    hs_fb_fill_round(fb, hero_x, hero_y, hero_w, hero_h, 8, pal->tile);
    hs_fb_rect(fb, hero_x, hero_y, hero_w, hero_h, 1, pal->border);

    beat = hs ? hs_hs_beat(hs) : HS_BEAT_IDLE;
    if (ui->live_beat != HS_BEAT_IDLE) {
        beat = ui->live_beat;
    }
    frozen = hs ? (hs_hs_frozen(hs) ? 1 : 0) : 0;
    epoch = hs ? hs_hs_commit_epoch(hs) : 0;
    hash = hs ? hs_hs_ledger_hash(hs) : 0;

    hs_fb_text(fb, hero_x + 14, hero_y + 10, 2, pal->text, "HANDSHAKE");
    hs_fb_printf(fb, hero_x + 14, hero_y + 32, 1, pal->dim, "%s",
                    ui->reserve_gb ? "3 pillars  +  3+1 seating"
                                   : "3 pillars  +  3+1 seating  +  Gemstone beats");

    hs_fb_printf(fb, hero_x + 14, hero_y + 54, 1, pal->dim, "EPOCH");
    hs_fb_printf(fb, hero_x + 14, hero_y + 66, 2, pal->value, "%llu",
                    (unsigned long long)epoch);

    hs_fb_printf(fb, hero_x + 14, hero_y + 90, 1, pal->dim, "LEDGER");
    hs_fb_printf(fb, hero_x + 14, hero_y + 102, 2, pal->accent2, "%016llx",
                    (unsigned long long)hash);

    if (stats) {
        format_ns(p50, sizeof(p50), stats->p50_ns);
        format_ns(p95, sizeof(p95), stats->p95_ns);
        format_ns(p99, sizeof(p99), stats->p99_ns);
        format_ns(pmax, sizeof(pmax), stats->max_ns);
        if (ui->reserve_gb) {
            /* Compact two-line form so the stats stay clear of the dial. */
            hs_fb_printf(fb, hero_x + 14, hero_y + 132, 1, pal->text,
                            "p50 %s  p95 %s  p99 %s", p50, p95, p99);
            hs_fb_printf(fb, hero_x + 14, hero_y + 148, 1, pal->dim,
                            "max %s  phases %llu  ovr %llu", pmax,
                            (unsigned long long)stats->ticks_done,
                            (unsigned long long)stats->overruns);
        } else {
            hs_fb_printf(fb, hero_x + 14, hero_y + 132, 1, pal->text,
                            "p50 %s   p95 %s   p99 %s   max %s", p50, p95, p99, pmax);
            hs_fb_printf(fb, hero_x + 14, hero_y + 148, 1, pal->dim,
                            "phases %llu   overruns %llu   monatomic %llu",
                            (unsigned long long)stats->ticks_done,
                            (unsigned long long)stats->overruns,
                            (unsigned long long)(hs ? hs_hs_monatomic(hs) : 0));
        }
    } else {
        hs_fb_text(fb, hero_x + 14, hero_y + 132, 1, pal->dim, "no samples yet");
    }

    if (frozen) {
        struct hs_freeze fr;
        hs_hs_last_freeze(hs, &fr);
        wdog = "FREEZE";
        hs_fb_printf(fb, hero_x + 14, hero_y + 168, 1, pal->warn,
                        "WATCHDOG  FREEZE  %s  %llu ms",
                        fr.component_name,
                        (unsigned long long)(fr.elapsed_ns / 1000000ull));
    } else {
        wdog = "OK";
        hs_fb_printf(fb, hero_x + 14, hero_y + 168, 1, pal->good,
                        "WATCHDOG  OK  ceiling 250 ms   beat %s",
                        hs_beat_name(beat == HS_BEAT_IDLE && epoch > 0
                                        ? HS_BEAT_COMMIT : beat));
    }
    (void)wdog;

    if (ui->reserve_gb) {
        /* Narrow hero: smaller dial, PULL label kept inside the card. */
        draw_gemstone(fb, hero_x + hero_w - 80, hero_y + 76, 32, pal, beat, frozen);
    } else {
        draw_gemstone(fb, hero_x + hero_w - 110, hero_y + 102, 56, pal, beat, frozen);
    }

    /* Four tiles */
    tile_y = hero_y + hero_h + margin;
    tile_h = fb->h - foot_h - margin - tile_y;
    tile_w = (fb->w - 2 * margin - 3 * gap) / 4;

    titles[HS_TILE_THEME] = "THEME";
    titles[HS_TILE_BATTERY] = "BATTERY";
    titles[HS_TILE_POWEROFF] = "POWER OFF";
    titles[HS_TILE_REBOOT] = "REBOOT";

    for (i = 0; i < HS_TILE_COUNT; i++) {
        int sel = (ui->selected == (hs_tile_t)i);
        uint32_t fill = sel ? pal->tile_sel : pal->tile;
        uint32_t bord = sel ? pal->border_sel : pal->border;
        tile_x = margin + i * (tile_w + gap);

        hs_fb_fill_round(fb, tile_x + 2, tile_y + 2, tile_w, tile_h, 6, pal->shadow);
        hs_fb_fill_round(fb, tile_x, tile_y, tile_w, tile_h, 6, fill);
        hs_fb_rect(fb, tile_x, tile_y, tile_w, tile_h, sel ? 2 : 1, bord);
        if (sel) {
            hs_fb_fill(fb, tile_x, tile_y + 8, 4, tile_h - 16, pal->accent);
        }
        hs_fb_text(fb, tile_x + 12, tile_y + 12, 1, pal->dim, titles[i]);

        t = (hs_tile_t)i;
        if (t == HS_TILE_THEME) {
            hs_fb_text(fb, tile_x + 12, tile_y + 36, 1, pal->text, pal->name);
            hs_fb_text(fb, tile_x + 12, tile_y + 56, 1, pal->dim, "L1 / R1 switch");
        } else if (t == HS_TILE_BATTERY) {
            if (bat && bat->present && bat->percent >= 0) {
                hs_fb_printf(fb, tile_x + 12, tile_y + 36, 2, pal->value, "%d%%", bat->percent);
                hs_fb_text(fb, tile_x + 12, tile_y + 60, 1, pal->dim,
                              bat->charging == 1 ? "charging" : bat->status[0] ? bat->status : "AXP717");
            } else {
                hs_fb_text(fb, tile_x + 12, tile_y + 36, 1, pal->text, "AXP717");
                hs_fb_text(fb, tile_x + 12, tile_y + 52, 1, pal->dim, "sysfs pending");
            }
        } else if (t == HS_TILE_POWEROFF) {
            hs_fb_text(fb, tile_x + 12, tile_y + 36, 1, pal->text, "Hold A");
            if (ui->confirm == HS_CONFIRM_POWEROFF) {
                hs_fb_text(fb, tile_x + 12, tile_y + 56, 1, pal->warn, "A yes  B no");
            }
        } else if (t == HS_TILE_REBOOT) {
            hs_fb_text(fb, tile_x + 12, tile_y + 36, 1, pal->text, "Hold A");
            if (ui->confirm == HS_CONFIRM_REBOOT) {
                hs_fb_text(fb, tile_x + 12, tile_y + 56, 1, pal->warn, "A yes  B no");
            }
        }
    }

    /* Footer */
    hs_fb_fill(fb, 0, fb->h - foot_h, fb->w, foot_h, pal->bar);
    hs_fb_fill(fb, 0, fb->h - foot_h, fb->w, 1, pal->border);
    if (ui->confirm != HS_CONFIRM_NONE) {
        hs_fb_text(fb, 12, fb->h - 22, 1, pal->warn, "A confirm    B back");
    } else if (ui->status_line) {
        hs_fb_text(fb, 12, fb->h - 22, 1, pal->text, ui->status_line);
    } else {
        hs_fb_text(fb, 12, fb->h - 22, 1, pal->dim,
                      "L1/R1 Theme   D-pad Move   A Select   SELECT+START Reboot");
    }
}

hs_action_t hs_ui_move(struct hs_ui *ui, int dx, int dy)
{
    int s;

    if (ui == NULL) {
        return HS_ACTION_NONE;
    }
    if (ui->confirm != HS_CONFIRM_NONE) {
        return HS_ACTION_NONE;
    }
    s = (int)ui->selected + dx + dy; /* one row of four */
    if (s < 0) {
        s = 0;
    }
    if (s >= HS_TILE_COUNT) {
        s = HS_TILE_COUNT - 1;
    }
    ui->selected = (hs_tile_t)s;
    return HS_ACTION_NONE;
}

hs_action_t hs_ui_activate(struct hs_ui *ui)
{
    if (ui == NULL) {
        return HS_ACTION_NONE;
    }
    if (ui->confirm == HS_CONFIRM_POWEROFF) {
        ui->confirm = HS_CONFIRM_NONE;
        return HS_ACTION_POWEROFF;
    }
    if (ui->confirm == HS_CONFIRM_REBOOT) {
        ui->confirm = HS_CONFIRM_NONE;
        return HS_ACTION_REBOOT;
    }
    if (ui->selected == HS_TILE_THEME) {
        hs_ui_cycle_theme(ui, 1);
        return HS_ACTION_NONE;
    }
    if (ui->selected == HS_TILE_POWEROFF) {
        ui->confirm = HS_CONFIRM_POWEROFF;
        return HS_ACTION_NONE;
    }
    if (ui->selected == HS_TILE_REBOOT) {
        ui->confirm = HS_CONFIRM_REBOOT;
        return HS_ACTION_NONE;
    }
    return HS_ACTION_NONE;
}

hs_action_t hs_ui_cancel(struct hs_ui *ui)
{
    if (ui == NULL) {
        return HS_ACTION_NONE;
    }
    ui->confirm = HS_CONFIRM_NONE;
    return HS_ACTION_NONE;
}

void hs_ui_cycle_theme(struct hs_ui *ui, int dir)
{
    if (ui == NULL) {
        return;
    }
    if (dir < 0) {
        ui->theme = hs_theme_prev(ui->theme);
    } else {
        ui->theme = hs_theme_next(ui->theme);
    }
}
