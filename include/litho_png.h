/* SPDX-License-Identifier: MIT */
#ifndef LITHO_PNG_H
#define LITHO_PNG_H

#include <stdint.h>

/* Write an 8-bit RGB PNG. pix is XRGB8888, stride is in bytes. */
int litho_write_png(const char *path, const uint32_t *pix, int width, int height, int stride_bytes);

#endif /* LITHO_PNG_H */
