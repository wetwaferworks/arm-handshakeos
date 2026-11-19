/* SPDX-License-Identifier: MIT */
#ifndef LITHO_THEME_H
#define LITHO_THEME_H

#include <stdint.h>

typedef enum {
    LITHO_THEME_PURPLE = 0,
    LITHO_THEME_GREEN  = 1,
    LITHO_THEME_COUNT
} litho_theme_id_t;

struct litho_palette {
    const char *name;
    const char *short_name;
    uint32_t bg;
    uint32_t bar;
    uint32_t surface;
    uint32_t tile;
    uint32_t tile_sel;
    uint32_t border;
    uint32_t border_sel;
    uint32_t accent;
    uint32_t accent2;
    uint32_t text;
    uint32_t dim;
    uint32_t value;
    uint32_t warn;
    uint32_t shadow;
    uint32_t good;
};

static inline uint32_t litho_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

const struct litho_palette *litho_theme_get(litho_theme_id_t id);
litho_theme_id_t litho_theme_next(litho_theme_id_t id);
litho_theme_id_t litho_theme_prev(litho_theme_id_t id);

#endif /* LITHO_THEME_H */
