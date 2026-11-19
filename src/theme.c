/* SPDX-License-Identifier: MIT */
#include "litho_theme.h"

static const struct litho_palette k_themes[LITHO_THEME_COUNT] = {
    [LITHO_THEME_PURPLE] = {
        .name = "Deep Purple",
        .short_name = "Purple",
        .bg         = 0x12081Fu,
        .bar        = 0x1B0E2Eu,
        .surface    = 0x1E1033u,
        .tile       = 0x2A1848u,
        .tile_sel   = 0x3D2266u,
        .border     = 0x5E35B1u,
        .border_sel = 0xC77DFFu,
        .accent     = 0x8E2DE2u,
        .accent2    = 0xE0B0FFu,
        .text       = 0xF3E8FFu,
        .dim        = 0x9A86B5u,
        .value      = 0xE0B0FFu,
        .warn       = 0xFFB74Du,
        .shadow     = 0x0A0414u,
        .good       = 0x66FFAAu,
    },
    [LITHO_THEME_GREEN] = {
        .name = "Matrix Green",
        .short_name = "Green",
        .bg         = 0x020802u,
        .bar        = 0x041004u,
        .surface    = 0x051405u,
        .tile       = 0x0A1F0Au,
        .tile_sel   = 0x0F3310u,
        .border     = 0x1B5E20u,
        .border_sel = 0x00FF41u,
        .accent     = 0x00FF41u,
        .accent2    = 0x39FF14u,
        .text       = 0xC8FFCEu,
        .dim        = 0x3D8B44u,
        .value      = 0x00FF41u,
        .warn       = 0xC8FF00u,
        .shadow     = 0x000000u,
        .good       = 0x00FF41u,
    },
};

const struct litho_palette *litho_theme_get(litho_theme_id_t id)
{
    if ((unsigned)id >= (unsigned)LITHO_THEME_COUNT) {
        id = LITHO_THEME_PURPLE;
    }
    return &k_themes[id];
}

litho_theme_id_t litho_theme_next(litho_theme_id_t id)
{
    return (litho_theme_id_t)(((unsigned)id + 1u) % (unsigned)LITHO_THEME_COUNT);
}

litho_theme_id_t litho_theme_prev(litho_theme_id_t id)
{
    return (litho_theme_id_t)(((unsigned)id + (unsigned)LITHO_THEME_COUNT - 1u)
                              % (unsigned)LITHO_THEME_COUNT);
}
