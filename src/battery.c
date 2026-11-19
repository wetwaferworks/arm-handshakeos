/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "litho_battery.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void litho_battery_clear(struct litho_battery *bat)
{
    if (bat == NULL) {
        return;
    }
    memset(bat, 0, sizeof(*bat));
    bat->percent = -1;
    bat->charging = -1;
    bat->voltage_uv = -1;
}

static int read_file(const char *path, char *buf, size_t buflen)
{
    FILE *fp;
    size_t n;

    fp = fopen(path, "r");
    if (fp == NULL) {
        return -errno;
    }
    if (fgets(buf, (int)buflen, fp) == NULL) {
        fclose(fp);
        return -EIO;
    }
    fclose(fp);
    n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) {
        buf[--n] = '\0';
    }
    return 0;
}

int litho_battery_read(struct litho_battery *bat)
{
    DIR *dir;
    struct dirent *ent;
    char path[384];
    char type[64];
    char cap[32];
    char status[32];
    char volt[32];
    int found = 0;

    if (bat == NULL) {
        return -EINVAL;
    }
    litho_battery_clear(bat);

    dir = opendir("/sys/class/power_supply");
    if (dir == NULL) {
        return 0;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", ent->d_name);
        if (read_file(path, type, sizeof(type)) != 0) {
            continue;
        }
        if (strcmp(type, "Battery") != 0 && strcmp(type, "battery") != 0) {
            continue;
        }

        found = 1;
        {
            size_t n = strlen(ent->d_name);
            if (n > sizeof(bat->name) - 1) {
                n = sizeof(bat->name) - 1;
            }
            memcpy(bat->name, ent->d_name, n);
            bat->name[n] = '\0';
        }
        bat->present = 1;

        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", ent->d_name);
        if (read_file(path, cap, sizeof(cap)) == 0) {
            bat->percent = atoi(cap);
            if (bat->percent < 0) {
                bat->percent = 0;
            }
            if (bat->percent > 100) {
                bat->percent = 100;
            }
        }

        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/status", ent->d_name);
        if (read_file(path, status, sizeof(status)) == 0) {
            snprintf(bat->status, sizeof(bat->status), "%s", status);
            if (strcmp(status, "Charging") == 0) {
                bat->charging = 1;
            } else if (strcmp(status, "Full") == 0) {
                bat->charging = 0;
            } else if (strcmp(status, "Discharging") == 0 || strcmp(status, "Not charging") == 0) {
                bat->charging = 0;
            }
        }

        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/voltage_now", ent->d_name);
        if (read_file(path, volt, sizeof(volt)) == 0) {
            bat->voltage_uv = atoi(volt);
        }
        break;
    }
    closedir(dir);
    (void)found;
    return 0;
}
