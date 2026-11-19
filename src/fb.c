/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "litho_fb.h"
#include "litho_font.h"
#include "litho_png.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/fb.h>
#ifdef __has_include
#  if __has_include(<drm/drm.h>)
#    define LITHO_HAS_DRM 1
#  endif
#endif
#ifndef LITHO_HAS_DRM
#  if defined(__linux__)
#    define LITHO_HAS_DRM 1
#  endif
#endif
#ifdef LITHO_HAS_DRM
#include <drm/drm.h>
#include <drm/drm_mode.h>
#endif
#endif

static uint32_t *pix_at(struct litho_fb *fb, int x, int y)
{
    return (uint32_t *)((uint8_t *)fb->pix + (size_t)y * (size_t)fb->stride_bytes) + x;
}

int litho_fb_open(struct litho_fb *fb, const char *backend)
{
    const char *kind = backend ? backend : "auto";

    if (fb == NULL) {
        return -EINVAL;
    }
    memset(fb, 0, sizeof(*fb));
    fb->w = LITHO_FB_W;
    fb->h = LITHO_FB_H;
    fb->stride_bytes = LITHO_FB_W * 4;
    fb->fd = -1;

    if (strcmp(kind, "auto") == 0) {
#ifdef __linux__
        if (litho_fb_open(fb, "drm") == 0) {
            return 0;
        }
        litho_fb_close(fb);
        if (litho_fb_open(fb, "fbdev") == 0) {
            return 0;
        }
        litho_fb_close(fb);
#endif
        kind = "headless";
    }

    if (strcmp(kind, "headless") == 0) {
        fb->pix = (uint32_t *)calloc((size_t)fb->w * (size_t)fb->h, 4);
        if (fb->pix == NULL) {
            return -ENOMEM;
        }
        fb->own_pix = 1;
        fb->kind = LITHO_FB_HEADLESS;
        return 0;
    }

#ifdef __linux__
    if (strcmp(kind, "fbdev") == 0) {
        struct fb_var_screeninfo vinfo;
        struct fb_fix_screeninfo finfo;
        void *map;
        size_t len;
        int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);

        if (fd < 0) {
            return -errno;
        }
        if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) != 0
            || ioctl(fd, FBIOGET_FSCREENINFO, &finfo) != 0) {
            int err = -errno;
            close(fd);
            return err;
        }
        fb->w = (int)vinfo.xres;
        fb->h = (int)vinfo.yres;
        fb->stride_bytes = (int)finfo.line_length;
        len = (size_t)finfo.smem_len;
        map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            int err = -errno;
            close(fd);
            return err;
        }
        fb->fd = fd;
        fb->pix = (uint32_t *)map;
        fb->kind = LITHO_FB_FBDEV;
        fb->handle = (uint32_t)len; /* stash length for munmap */
        return 0;
    }

#ifdef LITHO_HAS_DRM
    if (strcmp(kind, "drm") == 0) {
        const char *paths[] = { "/dev/dri/card0", "/dev/dri/card1", NULL };
        int i, fd = -1;
        struct drm_mode_card_res res;
        uint32_t conn_ids[16], crtc_ids[16];
        struct drm_mode_create_dumb creq;
        struct drm_mode_map_dumb mreq;
        struct drm_mode_fb_cmd fbcmd;
        void *map;

        for (i = 0; paths[i] != NULL; i++) {
            fd = open(paths[i], O_RDWR | O_CLOEXEC);
            if (fd >= 0) {
                break;
            }
        }
        if (fd < 0) {
            return -errno;
        }

        memset(&res, 0, sizeof(res));
        res.count_connectors = 16;
        res.count_crtcs = 16;
        res.connector_id_ptr = (uintptr_t)conn_ids;
        res.crtc_id_ptr = (uintptr_t)crtc_ids;
        if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) {
            int err = -errno;
            close(fd);
            return err;
        }
        if (res.count_connectors == 0 || res.count_crtcs == 0) {
            close(fd);
            return -ENODEV;
        }

        memset(&creq, 0, sizeof(creq));
        creq.width = LITHO_FB_W;
        creq.height = LITHO_FB_H;
        creq.bpp = 32;
        if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) != 0) {
            int err = -errno;
            close(fd);
            return err;
        }

        memset(&fbcmd, 0, sizeof(fbcmd));
        fbcmd.width = creq.width;
        fbcmd.height = creq.height;
        fbcmd.pitch = creq.pitch;
        fbcmd.bpp = 32;
        fbcmd.depth = 24;
        fbcmd.handle = creq.handle;
        if (ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fbcmd) != 0) {
            int err = -errno;
            close(fd);
            return err;
        }

        memset(&mreq, 0, sizeof(mreq));
        mreq.handle = creq.handle;
        if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) != 0) {
            int err = -errno;
            close(fd);
            return err;
        }
        map = mmap(NULL, (size_t)creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)mreq.offset);
        if (map == MAP_FAILED) {
            int err = -errno;
            close(fd);
            return err;
        }

        /* Best-effort SETCRTC is left for a later pass once the connector
           mode list is walked; the mapping is enough to draw. */
        fb->fd = fd;
        fb->pix = (uint32_t *)map;
        fb->stride_bytes = (int)creq.pitch;
        fb->w = LITHO_FB_W;
        fb->h = LITHO_FB_H;
        fb->kind = LITHO_FB_DRM;
        fb->fb_id = fbcmd.fb_id;
        fb->handle = creq.handle;
        fb->crtc_id = crtc_ids[0];
        fb->conn_id = conn_ids[0];
        return 0;
    }
#endif
#endif
    return -ENOTSUP;
}

void litho_fb_close(struct litho_fb *fb)
{
    if (fb == NULL) {
        return;
    }
    if (fb->own_pix && fb->pix != NULL) {
        free(fb->pix);
        fb->pix = NULL;
    } else if (fb->pix != NULL && fb->fd >= 0) {
        size_t len = (fb->kind == LITHO_FB_FBDEV) ? (size_t)fb->handle
                                                  : (size_t)fb->stride_bytes * (size_t)fb->h;
        munmap(fb->pix, len);
        fb->pix = NULL;
    }
    if (fb->fd >= 0) {
        close(fb->fd);
        fb->fd = -1;
    }
    fb->own_pix = 0;
}

void litho_fb_flip(struct litho_fb *fb)
{
    if (fb == NULL) {
        return;
    }
    /* Dumb buffers and fbdev are immediately visible. A later pass can
       issue a page flip once the panel is up. */
    (void)fb;
}

void litho_fb_clear(struct litho_fb *fb, uint32_t color)
{
    litho_fb_fill(fb, 0, 0, fb->w, fb->h, color);
}

void litho_fb_fill(struct litho_fb *fb, int x, int y, int w, int h, uint32_t color)
{
    int xx, yy, x0, y0, x1, y1;

    if (fb == NULL || fb->pix == NULL || w <= 0 || h <= 0) {
        return;
    }
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + w;
    y1 = y + h;
    if (x1 > fb->w) {
        x1 = fb->w;
    }
    if (y1 > fb->h) {
        y1 = fb->h;
    }
    for (yy = y0; yy < y1; yy++) {
        uint32_t *row = pix_at(fb, 0, yy);
        for (xx = x0; xx < x1; xx++) {
            row[xx] = color;
        }
    }
}

void litho_fb_rect(struct litho_fb *fb, int x, int y, int w, int h, int thickness, uint32_t color)
{
    int t;

    if (thickness < 1) {
        thickness = 1;
    }
    for (t = 0; t < thickness; t++) {
        litho_fb_fill(fb, x + t, y + t, w - 2 * t, 1, color);
        litho_fb_fill(fb, x + t, y + h - 1 - t, w - 2 * t, 1, color);
        litho_fb_fill(fb, x + t, y + t, 1, h - 2 * t, color);
        litho_fb_fill(fb, x + w - 1 - t, y + t, 1, h - 2 * t, color);
    }
}

void litho_fb_fill_circle(struct litho_fb *fb, int cx, int cy, int radius, uint32_t color)
{
    int x, y, r2;

    if (radius < 1) {
        return;
    }
    r2 = radius * radius;
    for (y = -radius; y <= radius; y++) {
        for (x = -radius; x <= radius; x++) {
            if (x * x + y * y <= r2) {
                int px = cx + x;
                int py = cy + y;
                if (px >= 0 && py >= 0 && px < fb->w && py < fb->h) {
                    pix_at(fb, px, py)[0] = color;
                }
            }
        }
    }
}

void litho_fb_fill_round(struct litho_fb *fb, int x, int y, int w, int h, int radius, uint32_t color)
{
    if (radius <= 0) {
        litho_fb_fill(fb, x, y, w, h, color);
        return;
    }
    if (radius * 2 > w) {
        radius = w / 2;
    }
    if (radius * 2 > h) {
        radius = h / 2;
    }
    litho_fb_fill(fb, x + radius, y, w - 2 * radius, h, color);
    litho_fb_fill(fb, x, y + radius, radius, h - 2 * radius, color);
    litho_fb_fill(fb, x + w - radius, y + radius, radius, h - 2 * radius, color);
    litho_fb_fill_circle(fb, x + radius, y + radius, radius, color);
    litho_fb_fill_circle(fb, x + w - 1 - radius, y + radius, radius, color);
    litho_fb_fill_circle(fb, x + radius, y + h - 1 - radius, radius, color);
    litho_fb_fill_circle(fb, x + w - 1 - radius, y + h - 1 - radius, radius, color);
}

static void draw_glyph(struct litho_fb *fb, int x, int y, int scale, uint32_t color, int ch)
{
    const uint8_t *g = litho_font_glyph(ch);
    int row, col, sy, sx;

    if (scale < 1) {
        scale = 1;
    }
    for (row = 0; row < LITHO_FONT_H; row++) {
        uint8_t bits = g[row];
        for (col = 0; col < LITHO_FONT_W; col++) {
            if ((bits & (uint8_t)(0x80u >> col)) == 0) {
                continue;
            }
            if (scale == 1) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && py >= 0 && px < fb->w && py < fb->h) {
                    pix_at(fb, px, py)[0] = color;
                }
            } else {
                for (sy = 0; sy < scale; sy++) {
                    for (sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        int py = y + row * scale + sy;
                        if (px >= 0 && py >= 0 && px < fb->w && py < fb->h) {
                            pix_at(fb, px, py)[0] = color;
                        }
                    }
                }
            }
        }
    }
}

int litho_fb_text(struct litho_fb *fb, int x, int y, int scale, uint32_t color, const char *s)
{
    int cx = x;

    if (fb == NULL || s == NULL) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    while (*s) {
        draw_glyph(fb, cx, y, scale, color, (unsigned char)*s);
        cx += LITHO_FONT_W * scale;
        s++;
    }
    return cx - x;
}

int litho_fb_printf(struct litho_fb *fb, int x, int y, int scale, uint32_t color, const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return litho_fb_text(fb, x, y, scale, color, buf);
}

int litho_fb_snapshot_ppm(const struct litho_fb *fb, const char *path)
{
    FILE *fp;
    int x, y;

    if (fb == NULL || fb->pix == NULL || path == NULL) {
        return -EINVAL;
    }
    fp = fopen(path, "wb");
    if (fp == NULL) {
        return -errno;
    }
    fprintf(fp, "P6\n%d %d\n255\n", fb->w, fb->h);
    for (y = 0; y < fb->h; y++) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)fb->pix
                                                 + (size_t)y * (size_t)fb->stride_bytes);
        for (x = 0; x < fb->w; x++) {
            uint32_t p = row[x];
            uint8_t rgb[3] = {
                (uint8_t)((p >> 16) & 0xFFu),
                (uint8_t)((p >> 8) & 0xFFu),
                (uint8_t)(p & 0xFFu)
            };
            if (fwrite(rgb, 1, 3, fp) != 3) {
                fclose(fp);
                return -EIO;
            }
        }
    }
    if (fclose(fp) != 0) {
        return -EIO;
    }
    return 0;
}

int litho_fb_snapshot_png(const struct litho_fb *fb, const char *path)
{
    if (fb == NULL || fb->pix == NULL) {
        return -EINVAL;
    }
    return litho_write_png(path, fb->pix, fb->w, fb->h, fb->stride_bytes);
}
