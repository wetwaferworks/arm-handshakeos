/* SPDX-License-Identifier: MIT */
#ifndef LITHO_FONT_INCLUDED
#define LITHO_FONT_INCLUDED

#include <stdint.h>

#define LITHO_FONT_W 8
#define LITHO_FONT_H 8

/* Returns the 8 row bytes for ASCII c, or the glyph for '?' if out of range. */
const uint8_t *litho_font_glyph(int c);

int litho_font_text_width(const char *s, int scale);

#endif /* LITHO_FONT_INCLUDED */
