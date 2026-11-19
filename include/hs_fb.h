/* SPDX-License-Identifier: MIT */
#ifndef HS_FB_INCLUDED
#define HS_FB_INCLUDED

#include <stddef.h>
#include <stdint.h>

#define HS_FB_W 640
#define HS_FB_H 480

typedef enum {
    HS_FB_HEADLESS = 0,
    HS_FB_FBDEV    = 1,
    HS_FB_DRM      = 2
} hs_fb_kind_t;

struct hs_fb {
    int            w;
    int            h;
    int            stride_bytes;
    uint32_t      *pix;
    int            fd;
    hs_fb_kind_t kind;
    int            own_pix;
    /* DRM bookkeeping */
    uint32_t       crtc_id;
    uint32_t       conn_id;
    uint32_t       fb_id;
    uint32_t       handle;
    uint32_t       saved_crtc;
    /* Hardware scanout (may be 16-bit RGB565 or 32-bit XRGB8888).
       Drawing always happens in a 640x480 XRGB8888 shadow; flip() converts. */
    void          *hw_map;
    size_t         hw_map_len;
    int            hw_w;
    int            hw_h;
    int            hw_stride;
    int            hw_bpp;
    unsigned       red_off, green_off, blue_off;
    unsigned       red_len, green_len, blue_len;
};

enum {
    HS_FB_BPP_16 = 16,
    HS_FB_BPP_32 = 32
};

/* backend: "auto", "drm", "fbdev", or "headless". */
int  hs_fb_open(struct hs_fb *fb, const char *backend);
void hs_fb_close(struct hs_fb *fb);
void hs_fb_flip(struct hs_fb *fb);

void hs_fb_clear(struct hs_fb *fb, uint32_t color);
void hs_fb_fill(struct hs_fb *fb, int x, int y, int w, int h, uint32_t color);
void hs_fb_rect(struct hs_fb *fb, int x, int y, int w, int h, int thickness, uint32_t color);
void hs_fb_fill_round(struct hs_fb *fb, int x, int y, int w, int h, int radius, uint32_t color);
void hs_fb_fill_circle(struct hs_fb *fb, int cx, int cy, int radius, uint32_t color);

int  hs_fb_text(struct hs_fb *fb, int x, int y, int scale, uint32_t color, const char *s);
int  hs_fb_printf(struct hs_fb *fb, int x, int y, int scale, uint32_t color, const char *fmt, ...)
    __attribute__((format(printf, 6, 7)));

int  hs_fb_snapshot_ppm(const struct hs_fb *fb, const char *path);
int  hs_fb_snapshot_png(const struct hs_fb *fb, const char *path);

#endif /* HS_FB_INCLUDED */
