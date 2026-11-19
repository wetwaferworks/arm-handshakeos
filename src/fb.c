/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_fb.h"
#include "hs_font.h"
#include "hs_png.h"

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
#    define HS_HAS_DRM 1
#  endif
#endif
#ifndef HS_HAS_DRM
#  if defined(__linux__)
#    define HS_HAS_DRM 1
#  endif
#endif
#ifdef HS_HAS_DRM
#include <drm/drm.h>
#include <drm/drm_mode.h>
#endif
#endif

static uint32_t *pix_at(struct hs_fb *fb, int x, int y)
{
    return (uint32_t *)((uint8_t *)fb->pix + (size_t)y * (size_t)fb->stride_bytes) + x;
}


static uint32_t pack_hw(const struct hs_fb *fb, uint32_t xrgb)
{
    unsigned r = (xrgb >> 16) & 0xFFu;
    unsigned g = (xrgb >> 8) & 0xFFu;
    unsigned b = xrgb & 0xFFu;
    unsigned rl = fb->red_len ? fb->red_len : 8;
    unsigned gl = fb->green_len ? fb->green_len : 8;
    unsigned bl = fb->blue_len ? fb->blue_len : 8;

    if (rl < 8) {
        r >>= (8 - rl);
    }
    if (gl < 8) {
        g >>= (8 - gl);
    }
    if (bl < 8) {
        b >>= (8 - bl);
    }
    return (r << fb->red_off) | (g << fb->green_off) | (b << fb->blue_off);
}

static int alloc_shadow(struct hs_fb *fb)
{
    fb->w = HS_FB_W;
    fb->h = HS_FB_H;
    fb->stride_bytes = HS_FB_W * 4;
    fb->pix = (uint32_t *)calloc((size_t)fb->w * (size_t)fb->h, 4);
    if (fb->pix == NULL) {
        return -ENOMEM;
    }
    fb->own_pix = 1;
    return 0;
}

static void default_fmt(struct hs_fb *fb, int bpp)
{
    fb->hw_bpp = bpp;
    if (bpp == 16) {
        fb->red_off = 11;
        fb->red_len = 5;
        fb->green_off = 5;
        fb->green_len = 6;
        fb->blue_off = 0;
        fb->blue_len = 5;
    } else {
        fb->hw_bpp = 32;
        fb->red_off = 16;
        fb->red_len = 8;
        fb->green_off = 8;
        fb->green_len = 8;
        fb->blue_off = 0;
        fb->blue_len = 8;
    }
}

static void blit_to_hw(struct hs_fb *fb)
{
    int x, y, dw, dh, ox, oy;
    const uint8_t *src;
    uint8_t *dst;

    if (fb == NULL || fb->hw_map == NULL || fb->pix == NULL) {
        return;
    }
    dw = fb->w < fb->hw_w ? fb->w : fb->hw_w;
    dh = fb->h < fb->hw_h ? fb->h : fb->hw_h;
    ox = (fb->hw_w - dw) / 2;
    oy = (fb->hw_h - dh) / 2;
    if (ox < 0) {
        ox = 0;
    }
    if (oy < 0) {
        oy = 0;
    }

    /* Fast path: packed XRGB8888, same layout, same size. */
    if (fb->hw_bpp == 32 && fb->red_off == 16 && fb->green_off == 8
        && fb->blue_off == 0 && fb->hw_w == fb->w && fb->hw_h == fb->h
        && fb->hw_stride == fb->stride_bytes) {
        memcpy(fb->hw_map, fb->pix, (size_t)fb->h * (size_t)fb->stride_bytes);
        return;
    }

    for (y = 0; y < dh; y++) {
        src = (const uint8_t *)fb->pix + (size_t)y * (size_t)fb->stride_bytes;
        dst = (uint8_t *)fb->hw_map + (size_t)(y + oy) * (size_t)fb->hw_stride
            + (size_t)ox * (size_t)(fb->hw_bpp / 8);
        if (fb->hw_bpp == 16) {
            uint16_t *d16 = (uint16_t *)dst;
            const uint32_t *s32 = (const uint32_t *)src;
            for (x = 0; x < dw; x++) {
                d16[x] = (uint16_t)pack_hw(fb, s32[x]);
            }
        } else {
            uint32_t *d32 = (uint32_t *)dst;
            const uint32_t *s32 = (const uint32_t *)src;
            if (fb->red_off == 16 && fb->green_off == 8 && fb->blue_off == 0) {
                memcpy(d32, s32, (size_t)dw * 4);
            } else {
                for (x = 0; x < dw; x++) {
                    d32[x] = pack_hw(fb, s32[x]);
                }
            }
        }
    }
}


int hs_fb_open(struct hs_fb *fb, const char *backend)
{
    const char *kind = backend ? backend : "auto";
    int tries, rc;

    if (fb == NULL) {
        return -EINVAL;
    }
    memset(fb, 0, sizeof(*fb));
    fb->w = HS_FB_W;
    fb->h = HS_FB_H;
    fb->stride_bytes = HS_FB_W * 4;
    fb->fd = -1;
    default_fmt(fb, 32);

    if (strcmp(kind, "auto") == 0) {
#ifdef __linux__
        /* Real hardware may need a moment for the panel to bind. */
        for (tries = 0; tries < 25; tries++) {
            rc = hs_fb_open(fb, "drm");
            if (rc == 0) {
                return 0;
            }
            hs_fb_close(fb);
            rc = hs_fb_open(fb, "fbdev");
            if (rc == 0) {
                return 0;
            }
            hs_fb_close(fb);
            if (tries < 24) {
                usleep(100000);
            }
        }
#endif
        kind = "headless";
    }

    if (strcmp(kind, "headless") == 0) {
        return alloc_shadow(fb) == 0 ? 0 : -ENOMEM;
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
        /* Prefer 32-bit XRGB8888 when the driver will take it. */
        if (vinfo.bits_per_pixel != 32) {
            vinfo.bits_per_pixel = 32;
            vinfo.red.offset = 16;
            vinfo.red.length = 8;
            vinfo.green.offset = 8;
            vinfo.green.length = 8;
            vinfo.blue.offset = 0;
            vinfo.blue.length = 8;
            vinfo.transp.offset = 24;
            vinfo.transp.length = 0;
            (void)ioctl(fd, FBIOPUT_VSCREENINFO, &vinfo);
            (void)ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
            (void)ioctl(fd, FBIOGET_FSCREENINFO, &finfo);
        }
        if (vinfo.bits_per_pixel != 16 && vinfo.bits_per_pixel != 32) {
            close(fd);
            return -ENOTSUP;
        }
        len = (size_t)finfo.smem_len;
        map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            int err = -errno;
            close(fd);
            return err;
        }
        fb->fd = fd;
        fb->hw_map = map;
        fb->hw_map_len = len;
        fb->hw_w = (int)vinfo.xres;
        fb->hw_h = (int)vinfo.yres;
        fb->hw_stride = (int)finfo.line_length;
        fb->hw_bpp = (int)vinfo.bits_per_pixel;
        fb->red_off = vinfo.red.offset;
        fb->red_len = vinfo.red.length;
        fb->green_off = vinfo.green.offset;
        fb->green_len = vinfo.green.length;
        fb->blue_off = vinfo.blue.offset;
        fb->blue_len = vinfo.blue.length;
        if (fb->red_len == 0) {
            default_fmt(fb, fb->hw_bpp);
        }
        fb->kind = HS_FB_FBDEV;
        if (alloc_shadow(fb) != 0) {
            hs_fb_close(fb);
            return -ENOMEM;
        }
        return 0;
    }

#ifdef HS_HAS_DRM
    if (strcmp(kind, "drm") == 0) {
        const char *paths[] = { "/dev/dri/card0", "/dev/dri/card1", NULL };
        int i, fd = -1;
        struct drm_mode_card_res res;
        uint32_t conn_ids[32], crtc_ids[16], enc_ids[16];
        struct drm_mode_create_dumb creq;
        struct drm_mode_map_dumb mreq;
        struct drm_mode_fb_cmd fbcmd;
        struct drm_mode_crtc crtc;
        struct drm_mode_modeinfo modes[32];
        struct drm_mode_modeinfo chosen;
        struct drm_mode_get_connector conn;
        uint32_t conn_id = 0, crtc_id = 0, encoder_id = 0;
        int found = 0, n, m;
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
        (void)ioctl(fd, DRM_IOCTL_SET_MASTER, 0);

        memset(&res, 0, sizeof(res));
        res.count_connectors = 32;
        res.count_crtcs = 16;
        res.connector_id_ptr = (uintptr_t)conn_ids;
        res.crtc_id_ptr = (uintptr_t)crtc_ids;
        if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0
            || res.count_connectors == 0 || res.count_crtcs == 0) {
            int err = (res.count_connectors == 0 || res.count_crtcs == 0) ? -ENODEV : -errno;
            close(fd);
            return err;
        }

        memset(&chosen, 0, sizeof(chosen));
        for (n = 0; n < (int)res.count_connectors && n < 32; n++) {
            memset(&conn, 0, sizeof(conn));
            conn.connector_id = conn_ids[n];
            conn.count_modes = 32;
            conn.count_encoders = 16;
            conn.modes_ptr = (uintptr_t)modes;
            conn.encoders_ptr = (uintptr_t)enc_ids;
            if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0) {
                continue;
            }
            if (conn.connection != 1 || conn.count_modes == 0) {
                continue;
            }
            conn_id = conn.connector_id;
            encoder_id = conn.encoder_id;
            chosen = modes[0];
            for (m = 0; m < (int)conn.count_modes && m < 32; m++) {
                if (modes[m].hdisplay == HS_FB_W
                    && modes[m].vdisplay == HS_FB_H) {
                    chosen = modes[m];
                    break;
                }
            }
            if (encoder_id == 0 && conn.count_encoders > 0) {
                encoder_id = enc_ids[0];
            }
            found = 1;
            break;
        }
        if (!found) {
            close(fd);
            return -ENODEV;
        }
        crtc_id = crtc_ids[0];
        if (encoder_id != 0) {
            struct drm_mode_get_encoder enc;
            memset(&enc, 0, sizeof(enc));
            enc.encoder_id = encoder_id;
            if (ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0) {
                if (enc.crtc_id != 0) {
                    crtc_id = enc.crtc_id;
                }
            }
        }

        memset(&creq, 0, sizeof(creq));
        creq.width = chosen.hdisplay ? chosen.hdisplay : HS_FB_W;
        creq.height = chosen.vdisplay ? chosen.vdisplay : HS_FB_H;
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
        map = mmap(NULL, (size_t)creq.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fd, (off_t)mreq.offset);
        if (map == MAP_FAILED) {
            int err = -errno;
            close(fd);
            return err;
        }

        memset(&crtc, 0, sizeof(crtc));
        crtc.crtc_id = crtc_id;
        crtc.fb_id = fbcmd.fb_id;
        crtc.set_connectors_ptr = (uintptr_t)&conn_id;
        crtc.count_connectors = 1;
        crtc.mode = chosen;
        crtc.mode_valid = 1;
        if (ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &crtc) != 0) {
            /* Invisible buffer is not a success — fall back to fbdev. */
            munmap(map, (size_t)creq.size);
            close(fd);
            return -EIO;
        }

        fb->fd = fd;
        fb->hw_map = map;
        fb->hw_map_len = (size_t)creq.size;
        fb->hw_w = (int)creq.width;
        fb->hw_h = (int)creq.height;
        fb->hw_stride = (int)creq.pitch;
        default_fmt(fb, 32);
        fb->kind = HS_FB_DRM;
        fb->fb_id = fbcmd.fb_id;
        fb->handle = creq.handle;
        fb->crtc_id = crtc_id;
        fb->conn_id = conn_id;
        if (alloc_shadow(fb) != 0) {
            hs_fb_close(fb);
            return -ENOMEM;
        }
        return 0;
    }
#endif
#endif
    return -ENOTSUP;
}

void hs_fb_close(struct hs_fb *fb)
{
    if (fb == NULL) {
        return;
    }
    if (fb->own_pix && fb->pix != NULL) {
        free(fb->pix);
        fb->pix = NULL;
    }
    if (fb->hw_map != NULL && fb->hw_map_len > 0) {
        munmap(fb->hw_map, fb->hw_map_len);
        fb->hw_map = NULL;
        fb->hw_map_len = 0;
    }
    if (fb->fd >= 0) {
        close(fb->fd);
        fb->fd = -1;
    }
    fb->own_pix = 0;
}

void hs_fb_flip(struct hs_fb *fb)
{
    if (fb == NULL) {
        return;
    }
    blit_to_hw(fb);
}

void hs_fb_clear(struct hs_fb *fb, uint32_t color)
{
    hs_fb_fill(fb, 0, 0, fb->w, fb->h, color);
}

void hs_fb_fill(struct hs_fb *fb, int x, int y, int w, int h, uint32_t color)
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

void hs_fb_rect(struct hs_fb *fb, int x, int y, int w, int h, int thickness, uint32_t color)
{
    int t;

    if (thickness < 1) {
        thickness = 1;
    }
    for (t = 0; t < thickness; t++) {
        hs_fb_fill(fb, x + t, y + t, w - 2 * t, 1, color);
        hs_fb_fill(fb, x + t, y + h - 1 - t, w - 2 * t, 1, color);
        hs_fb_fill(fb, x + t, y + t, 1, h - 2 * t, color);
        hs_fb_fill(fb, x + w - 1 - t, y + t, 1, h - 2 * t, color);
    }
}

void hs_fb_fill_circle(struct hs_fb *fb, int cx, int cy, int radius, uint32_t color)
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

void hs_fb_fill_round(struct hs_fb *fb, int x, int y, int w, int h, int radius, uint32_t color)
{
    if (radius <= 0) {
        hs_fb_fill(fb, x, y, w, h, color);
        return;
    }
    if (radius * 2 > w) {
        radius = w / 2;
    }
    if (radius * 2 > h) {
        radius = h / 2;
    }
    hs_fb_fill(fb, x + radius, y, w - 2 * radius, h, color);
    hs_fb_fill(fb, x, y + radius, radius, h - 2 * radius, color);
    hs_fb_fill(fb, x + w - radius, y + radius, radius, h - 2 * radius, color);
    hs_fb_fill_circle(fb, x + radius, y + radius, radius, color);
    hs_fb_fill_circle(fb, x + w - 1 - radius, y + radius, radius, color);
    hs_fb_fill_circle(fb, x + radius, y + h - 1 - radius, radius, color);
    hs_fb_fill_circle(fb, x + w - 1 - radius, y + h - 1 - radius, radius, color);
}

static void draw_glyph(struct hs_fb *fb, int x, int y, int scale, uint32_t color, int ch)
{
    const uint8_t *g = hs_font_glyph(ch);
    int row, col, sy, sx;

    if (scale < 1) {
        scale = 1;
    }
    for (row = 0; row < HS_FONT_H; row++) {
        uint8_t bits = g[row];
        for (col = 0; col < HS_FONT_W; col++) {
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

int hs_fb_text(struct hs_fb *fb, int x, int y, int scale, uint32_t color, const char *s)
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
        cx += HS_FONT_W * scale;
        s++;
    }
    return cx - x;
}

int hs_fb_printf(struct hs_fb *fb, int x, int y, int scale, uint32_t color, const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return hs_fb_text(fb, x, y, scale, color, buf);
}

int hs_fb_snapshot_ppm(const struct hs_fb *fb, const char *path)
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

int hs_fb_snapshot_png(const struct hs_fb *fb, const char *path)
{
    if (fb == NULL || fb->pix == NULL) {
        return -EINVAL;
    }
    return hs_write_png(path, fb->pix, fb->w, fb->h, fb->stride_bytes);
}
