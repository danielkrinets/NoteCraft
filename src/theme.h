/* All GUI art is generated in code (no Mojang assets are shipped).
 * The book atlas uses the same layout as vanilla's book.png, so a resource
 * pack texture from a real Minecraft install can be dropped in unchanged:
 *   (0,0)   192x192  the page
 *   (0,192) 23x13    next arrow   (23,192) hovered
 *   (0,205) 23x13    back arrow   (23,205) hovered                       */
#ifndef NC_THEME_H
#define NC_THEME_H

#include "gfx.h"

typedef struct {
    Fb *book;       /* 256x256 atlas */
    Fb *dirt;       /* screen background tile */
    int custom_book, custom_dirt;
} Theme;

/* Builds the theme; if `pack_dir` (a resource pack root) has the matching
 * PNGs they replace the generated textures. */
Theme *theme_new(const char *pack_dir);
void   theme_free(Theme *t);

enum { BTN_NORMAL = 0, BTN_HOVER = 1, BTN_DISABLED = 2 };
void theme_button(Fb *f, int x, int y, int w, int h, int state);

#endif
