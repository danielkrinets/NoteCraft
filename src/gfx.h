/* Tiny software framebuffer. Colours are 0xAARRGGBB. Everything the app draws
 * goes through here, so the same code renders in the SDL window and headless
 * (screenshots, demo GIF, tests). */
#ifndef NC_GFX_H
#define NC_GFX_H

#include <stdint.h>

typedef struct Fb {
    int w, h;
    uint32_t *px;
    int cx0, cy0, cx1, cy1;   /* clip rectangle, [x0,x1) x [y0,y1) */
} Fb;

Fb  *fb_new(int w, int h);
void fb_free(Fb *f);
int  fb_resize(Fb *f, int w, int h);

void fb_set_clip(Fb *f, int x, int y, int w, int h);
void fb_reset_clip(Fb *f);

void     fb_clear(Fb *f, uint32_t argb);
void     fb_plot(Fb *f, int x, int y, uint32_t argb);        /* alpha-blended */
uint32_t fb_get(const Fb *f, int x, int y);
void     fb_rect(Fb *f, int x, int y, int w, int h, uint32_t argb);
void     fb_frame(Fb *f, int x, int y, int w, int h, uint32_t argb); /* 1px outline */
void     fb_gradient_v(Fb *f, int x, int y, int w, int h, uint32_t top, uint32_t bottom);

/* GL_OR_REVERSE with colour `rgb`: out = rgb | ~dst. This is exactly what
 * Minecraft's book screen uses for the blue text selection. */
void fb_or_reverse(Fb *f, int x, int y, int w, int h, uint32_t rgb);

void fb_blit(Fb *dst, const Fb *src, int sx, int sy, int w, int h, int dx, int dy);

/* Tile `tile` over the whole framebuffer, each texel `scale` pixels wide,
 * multiplied by `bright` (0..255). Used for the dirt screen background. */
void fb_tile_dim(Fb *dst, const Fb *tile, int scale, int bright);

uint32_t gfx_blend(uint32_t dst, uint32_t src);

#endif
