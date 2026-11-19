/* SPDX-License-Identifier: MIT */
#ifndef HS_UI_H
#define HS_UI_H

#include "hs_theme.h"
#include "hs_handshake.h"

#include "hs_battery.h"

struct hs_fb;

typedef enum {
    HS_TILE_THEME = 0,
    HS_TILE_BATTERY,
    HS_TILE_POWEROFF,
    HS_TILE_REBOOT,
    HS_TILE_COUNT
} hs_tile_t;

typedef enum {
    HS_CONFIRM_NONE = 0,
    HS_CONFIRM_POWEROFF,
    HS_CONFIRM_REBOOT
} hs_confirm_t;

typedef enum {
    HS_ACTION_NONE = 0,
    HS_ACTION_POWEROFF,
    HS_ACTION_REBOOT,
    HS_ACTION_QUIT
} hs_action_t;

struct hs_ui {
    hs_theme_id_t theme;
    hs_tile_t     selected;
    hs_confirm_t  confirm;
    const char      *status_line;
    int              reserve_gb; /* leave a right-hand column for a 160x144 GB bezel */
    hs_beat_t     live_beat;  /* nonzero: animate the dial with this beat */
};

void hs_ui_init(struct hs_ui *ui, hs_theme_id_t theme);

void hs_ui_draw(struct hs_fb *fb,
                   const struct hs_ui *ui,
                   const struct hs_hs *hs,
                   const struct hs_jitter_stats *stats,
                   const struct hs_battery *bat);

/* Move the selection and handle A/B/L1/R1. Returns an action if any. */
hs_action_t hs_ui_move(struct hs_ui *ui, int dx, int dy);
hs_action_t hs_ui_activate(struct hs_ui *ui);
hs_action_t hs_ui_cancel(struct hs_ui *ui);
void           hs_ui_cycle_theme(struct hs_ui *ui, int dir);

void hs_ui_draw_gemstone(struct hs_fb *fb, int cx, int cy, int radius,
                            const struct hs_palette *pal,
                            hs_beat_t beat, int frozen);

#endif /* HS_UI_H */
