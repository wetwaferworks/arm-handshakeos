/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include "litho_font.h"
#include "litho_font_data.h"

const uint8_t *litho_font_glyph(int c)
{
    if (c < LITHO_FONT_FIRST || c > LITHO_FONT_LAST) {
        c = '?';
    }
    return litho_font8[c - LITHO_FONT_FIRST];
}

int litho_font_text_width(const char *s, int scale)
{
    int n = 0;

    if (s == NULL || scale < 1) {
        return 0;
    }
    while (*s != '\0') {
        n++;
        s++;
    }
    return n * LITHO_FONT_W * scale;
}
