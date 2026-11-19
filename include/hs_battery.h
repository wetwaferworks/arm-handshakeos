/* SPDX-License-Identifier: MIT */
#ifndef HS_BATTERY_H
#define HS_BATTERY_H

struct hs_battery {
    int present;        /* 1 if a power-supply class device was found */
    int percent;        /* 0-100, or -1 if unknown */
    int charging;       /* 1 charging, 0 not, -1 unknown */
    int voltage_uv;     /* microvolts, or -1 */
    char name[64];
    char status[32];
};

void hs_battery_clear(struct hs_battery *bat);
int  hs_battery_read(struct hs_battery *bat);

#endif /* HS_BATTERY_H */
