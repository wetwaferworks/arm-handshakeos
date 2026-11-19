/* SPDX-License-Identifier: MIT */
#ifndef LITHO_FB_INCLUDED
#define LITHO_FB_INCLUDED

#include <stddef.h>
#include <stdint.h>

#define LITHO_FB_W 640
#define LITHO_FB_H 480

typedef enum {
    LITHO_FB_HEADLESS = 0,
    LITHO_FB_FBDEV    = 1,
    LITHO_FB_DRM      = 2
} litho_fb_kind_t;

struct litho_fb {
    int            w;
    int            h;
    int            stride_bytes;
    uint32_t      *pix;
    int            fd;
    litho_fb_kind_t kind;
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
    LITHO_FB_BPP_16 = 16,
    LITHO_FB_BPP_32 = 32
};

/* backend: "auto", "drm", "fbdev", or "headless". */
int  litho_fb_open(struct litho_fb *fb, const char *backend);
void litho_fb_close(struct litho_fb *fb);
void litho_fb_flip(struct litho_fb *fb);

void litho_fb_clear(struct litho_fb *fb, uint32_t color);
void litho_fb_fill(struct litho_fb *fb, int x, int y, int w, int h, uint32_t color);
void litho_fb_rect(struct litho_fb *fb, int x, int y, int w, int h, int thickness, uint32_t color);
void litho_fb_fill_round(struct litho_fb *fb, int x, int y, int w, int h, int radius, uint32_t color);
void litho_fb_fill_circle(struct litho_fb *fb, int cx, int cy, int radius, uint32_t color);

int  litho_fb_text(struct litho_fb *fb, int x, int y, int scale, uint32_t color, const char *s);
int  litho_fb_printf(struct litho_fb *fb, int x, int y, int scale, uint32_t color, const char *fmt, ...)
    __attribute__((format(printf, 6, 7)));

int  litho_fb_snapshot_ppm(const struct litho_fb *fb, const char *path);
int  litho_fb_snapshot_png(const struct litho_fb *fb, const char *path);

#endif /* LITHO_FB_INCLUDED */
