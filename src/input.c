/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif
#include "litho_input.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *const k_btn_names[LITHO_BTN_COUNT] = {
    "Up", "Down", "Left", "Right",
    "A", "B", "X", "Y",
    "L1", "R1", "L2", "R2",
    "Start", "Select", "Menu",
    "Vol+", "Vol-", "Power"
};

const char *litho_btn_name(litho_btn_t btn)
{
    if ((unsigned)btn >= (unsigned)LITHO_BTN_COUNT) {
        return "?";
    }
    return k_btn_names[btn];
}

static int map_code(uint16_t type, uint16_t code, litho_btn_t *out)
{
    if (type != EV_KEY) {
        return 0;
    }

    switch (code) {
    case BTN_DPAD_UP:
    case KEY_UP:
        *out = LITHO_BTN_UP;
        return 1;
    case BTN_DPAD_DOWN:
    case KEY_DOWN:
        *out = LITHO_BTN_DOWN;
        return 1;
    case BTN_DPAD_LEFT:
    case KEY_LEFT:
        *out = LITHO_BTN_LEFT;
        return 1;
    case BTN_DPAD_RIGHT:
    case KEY_RIGHT:
        *out = LITHO_BTN_RIGHT;
        return 1;
    case BTN_EAST:      /* physical A */
    case KEY_ENTER:
    case KEY_Z:
        *out = LITHO_BTN_A;
        return 1;
    case BTN_SOUTH:     /* physical B */
    case KEY_ESC:
    case KEY_X:
        *out = LITHO_BTN_B;
        return 1;
    case BTN_NORTH:     /* physical X */
    case KEY_A:
        *out = LITHO_BTN_X;
        return 1;
    case BTN_WEST:      /* physical Y */
    case KEY_S:
        *out = LITHO_BTN_Y;
        return 1;
    case BTN_TL:
    case KEY_Q:
    case KEY_TAB:
        *out = LITHO_BTN_L1;
        return 1;
    case BTN_TR:
    case KEY_E:
        *out = LITHO_BTN_R1;
        return 1;
    case BTN_TL2:
        *out = LITHO_BTN_L2;
        return 1;
    case BTN_TR2:
        *out = LITHO_BTN_R2;
        return 1;
    case BTN_START:
    case KEY_RIGHTCTRL:
        *out = LITHO_BTN_START;
        return 1;
    case BTN_SELECT:
    case KEY_RIGHTSHIFT:
        *out = LITHO_BTN_SELECT;
        return 1;
    case BTN_MODE:
    case KEY_F1:
        *out = LITHO_BTN_MENU;
        return 1;
    case KEY_VOLUMEUP:
        *out = LITHO_BTN_VOLUP;
        return 1;
    case KEY_VOLUMEDOWN:
        *out = LITHO_BTN_VOLDOWN;
        return 1;
    case KEY_POWER:
    case KEY_P:
        *out = LITHO_BTN_POWER;
        return 1;
    default:
        return 0;
    }
}

int litho_input_open(struct litho_input *in)
{
    DIR *dir;
    struct dirent *ent;
    char path[320];
    int fd;

    if (in == NULL) {
        return -EINVAL;
    }
    memset(in, 0, sizeof(*in));

    dir = opendir("/dev/input");
    if (dir == NULL) {
        return 0; /* headless is fine */
    }

    while ((ent = readdir(dir)) != NULL && in->n_fds < LITHO_INPUT_MAX_FDS) {
        if (strncmp(ent->d_name, "event", 5) != 0) {
            continue;
        }
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        in->fds[in->n_fds++] = fd;
    }
    closedir(dir);
    return 0;
}

void litho_input_close(struct litho_input *in)
{
    int i;

    if (in == NULL) {
        return;
    }
    for (i = 0; i < in->n_fds; i++) {
        if (in->fds[i] >= 0) {
            close(in->fds[i]);
            in->fds[i] = -1;
        }
    }
    in->n_fds = 0;
}

static void apply_event(struct litho_input *in, const struct input_event *ev)
{
    litho_btn_t btn;
    uint32_t bit;

    if (!map_code(ev->type, ev->code, &btn)) {
        return;
    }
    bit = 1u << (unsigned)btn;

    if (ev->value > 0) {
        if ((in->down & bit) == 0) {
            in->pressed |= bit;
        }
        in->down |= bit;
    } else if (ev->value == 0) {
        if ((in->down & bit) != 0) {
            in->released |= bit;
        }
        in->down &= ~bit;
    }
    /* ev->value == 2 is key repeat; ignore for edge triggers. */
}

int litho_input_poll(struct litho_input *in, int timeout_ms)
{
    struct pollfd pfds[LITHO_INPUT_MAX_FDS];
    struct input_event ev;
    int i, n, r;
    ssize_t got;

    if (in == NULL) {
        return -EINVAL;
    }
    in->pressed = 0;
    in->released = 0;

    if (in->n_fds <= 0) {
        return 0;
    }

    for (i = 0; i < in->n_fds; i++) {
        pfds[i].fd = in->fds[i];
        pfds[i].events = POLLIN;
        pfds[i].revents = 0;
    }

    r = poll(pfds, (nfds_t)in->n_fds, timeout_ms);
    if (r < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -errno;
    }
    if (r == 0) {
        return 0;
    }

    n = 0;
    for (i = 0; i < in->n_fds; i++) {
        if ((pfds[i].revents & POLLIN) == 0) {
            continue;
        }
        for (;;) {
            got = read(in->fds[i], &ev, sizeof(ev));
            if (got < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                break;
            }
            if (got != (ssize_t)sizeof(ev)) {
                break;
            }
            apply_event(in, &ev);
            n++;
        }
    }
    return n;
}

int litho_input_down(const struct litho_input *in, litho_btn_t btn)
{
    if (in == NULL || (unsigned)btn >= (unsigned)LITHO_BTN_COUNT) {
        return 0;
    }
    return (in->down & (1u << (unsigned)btn)) != 0;
}

int litho_input_pressed(const struct litho_input *in, litho_btn_t btn)
{
    if (in == NULL || (unsigned)btn >= (unsigned)LITHO_BTN_COUNT) {
        return 0;
    }
    return (in->pressed & (1u << (unsigned)btn)) != 0;
}
