/* SPDX-License-Identifier: MIT */
#ifndef LITHO_UI_H
#define LITHO_UI_H

#include "litho_theme.h"
#include "litho_handshake.h"

#include "litho_battery.h"

struct litho_fb;

typedef enum {
    LITHO_TILE_THEME = 0,
    LITHO_TILE_BATTERY,
    LITHO_TILE_POWEROFF,
    LITHO_TILE_REBOOT,
    LITHO_TILE_COUNT
} litho_tile_t;

typedef enum {
    LITHO_CONFIRM_NONE = 0,
    LITHO_CONFIRM_POWEROFF,
    LITHO_CONFIRM_REBOOT
} litho_confirm_t;

typedef enum {
    LITHO_ACTION_NONE = 0,
    LITHO_ACTION_POWEROFF,
    LITHO_ACTION_REBOOT,
    LITHO_ACTION_QUIT
} litho_action_t;

struct litho_ui {
    litho_theme_id_t theme;
    litho_tile_t     selected;
    litho_confirm_t  confirm;
    const char      *status_line;
};

void litho_ui_init(struct litho_ui *ui, litho_theme_id_t theme);

void litho_ui_draw(struct litho_fb *fb,
                   const struct litho_ui *ui,
                   const struct litho_hs *hs,
                   const struct litho_jitter_stats *stats,
                   const struct litho_battery *bat);

/* Move the selection and handle A/B/L1/R1. Returns an action if any. */
litho_action_t litho_ui_move(struct litho_ui *ui, int dx, int dy);
litho_action_t litho_ui_activate(struct litho_ui *ui);
litho_action_t litho_ui_cancel(struct litho_ui *ui);
void           litho_ui_cycle_theme(struct litho_ui *ui, int dir);

#endif /* LITHO_UI_H */
