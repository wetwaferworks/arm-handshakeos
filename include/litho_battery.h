/* SPDX-License-Identifier: MIT */
#ifndef LITHO_BATTERY_H
#define LITHO_BATTERY_H

struct litho_battery {
    int present;        /* 1 if a power-supply class device was found */
    int percent;        /* 0-100, or -1 if unknown */
    int charging;       /* 1 charging, 0 not, -1 unknown */
    int voltage_uv;     /* microvolts, or -1 */
    char name[64];
    char status[32];
};

void litho_battery_clear(struct litho_battery *bat);
int  litho_battery_read(struct litho_battery *bat);

#endif /* LITHO_BATTERY_H */
