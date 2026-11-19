/* SPDX-License-Identifier: MIT */
#ifndef LITHO_INPUT_H
#define LITHO_INPUT_H

#include <stdint.h>

/*
 * RG35XX 2024 evdev map, from the mainline device tree
 * sun50i-h700-anbernic-rg35xx-2024.dts (Ryan Walklin, 2024):
 *
 *   A BTN_EAST    B BTN_SOUTH    X BTN_NORTH    Y BTN_WEST
 *   L1 BTN_TL     R1 BTN_TR      L2 BTN_TL2     R2 BTN_TR2
 *   Start BTN_START  Select BTN_SELECT  Menu BTN_MODE
 *   D-pad BTN_DPAD_*    Vol± KEY_VOLUMEUP/DOWN
 *
 * Power is not in that gpio-keys block (it typically comes from the
 * AXP717 PMIC as KEY_POWER). We listen for KEY_POWER as well.
 *
 * On a host keyboard we also accept arrows, Enter/Esc, Q/E for L1/R1,
 * Z/X/A/S for A/B/X/Y, P for Power, and Tab for Menu.
 */

typedef enum {
    LITHO_BTN_UP = 0,
    LITHO_BTN_DOWN,
    LITHO_BTN_LEFT,
    LITHO_BTN_RIGHT,
    LITHO_BTN_A,
    LITHO_BTN_B,
    LITHO_BTN_X,
    LITHO_BTN_Y,
    LITHO_BTN_L1,
    LITHO_BTN_R1,
    LITHO_BTN_L2,
    LITHO_BTN_R2,
    LITHO_BTN_START,
    LITHO_BTN_SELECT,
    LITHO_BTN_MENU,
    LITHO_BTN_VOLUP,
    LITHO_BTN_VOLDOWN,
    LITHO_BTN_POWER,
    LITHO_BTN_COUNT
} litho_btn_t;

#define LITHO_INPUT_MAX_FDS 16

struct litho_input {
    int      n_fds;
    int      fds[LITHO_INPUT_MAX_FDS];
    uint32_t down;
    uint32_t pressed;
    uint32_t released;
};

int  litho_input_open(struct litho_input *in);
void litho_input_close(struct litho_input *in);
int  litho_input_poll(struct litho_input *in, int timeout_ms);
int  litho_input_down(const struct litho_input *in, litho_btn_t btn);
int  litho_input_pressed(const struct litho_input *in, litho_btn_t btn);
const char *litho_btn_name(litho_btn_t btn);

#endif /* LITHO_INPUT_H */
