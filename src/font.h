/* Proportional pixel font in the style of Minecraft's default font.
 * Glyph advance = glyph width + 1, space = 4, line height = 9 (like vanilla).
 * The bitmaps in font.c are drawn from scratch (no Mojang assets). */
#ifndef NC_FONT_H
#define NC_FONT_H

#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

#define FONT_LINE_H 9

int  font_cp_width(uint32_t cp);                       /* advance in px */
int  font_width(const uint32_t *s, int n);
int  font_width_u8(const char *s);
int  font_has_glyph(uint32_t cp);

/* Draw without shadow; x,y = top-left of the 9px line box. Returns advance. */
int  font_draw_cp(Fb *f, int x, int y, uint32_t cp, uint32_t color);
void font_draw(Fb *f, int x, int y, const uint32_t *s, int n, uint32_t color);
/* UTF-8 convenience; shadow like vanilla: ((c & 0xFCFCFC) >> 2). */
void font_draw_u8(Fb *f, int x, int y, const char *s, uint32_t color, int shadow);
void font_draw_u8_centered(Fb *f, int cx, int y, const char *s, uint32_t color, int shadow);

/* Raw glyph access (used by tests and the specimen renderer). */
typedef struct {
    uint32_t cp;
    uint8_t w, top, nrows;
    uint16_t rows[9];           /* bit 15 = leftmost column */
} Glyph;
const Glyph *font_glyph(uint32_t cp);
int  font_glyph_count(void);
const Glyph *font_glyph_at(int i);
/* Returns 0 if every glyph row in the source table is well-formed. */
int  font_selftest(void);

#endif
