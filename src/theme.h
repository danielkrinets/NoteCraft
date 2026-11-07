/* All GUI art is generated in code (no Mojang assets are shipped). It is
 * drawn the way Minecraft textures are: hard-edged tones, 1px outlines and
 * sparse pixel noise instead of smooth gradients. */
#ifndef NC_THEME_H
#define NC_THEME_H

#include "gfx.h"

#define BOOK_SPREAD_W 300
#define BOOK_SPREAD_H 216

typedef struct {
    Fb *book;       /* open book, two pages, BOOK_SPREAD_W x BOOK_SPREAD_H, fully opaque */
    Fb *arrows;     /* 46x26: next (0,0) hover (23,0); back (0,13) hover (23,13) */
    Fb *leather;    /* 32x32 cover tile, used behind dialogs */
} Theme;

Theme *theme_new(const char *unused_pack_dir);
void   theme_free(Theme *t);

enum { BTN_NORMAL = 0, BTN_HOVER = 1, BTN_DISABLED = 2 };
void theme_button(Fb *f, int x, int y, int w, int h, int state);

#endif
