#include "gfx.h"

#include <stdlib.h>
#include <string.h>

Fb *fb_new(int w, int h)
{
    Fb *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    f->px = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if (!f->px) { free(f); return NULL; }
    f->w = w; f->h = h;
    fb_reset_clip(f);
    return f;
}

void fb_free(Fb *f)
{
    if (!f) return;
    free(f->px);
    free(f);
}

int fb_resize(Fb *f, int w, int h)
{
    if (w == f->w && h == f->h) return 0;
    uint32_t *p = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if (!p) return -1;
    free(f->px);
    f->px = p; f->w = w; f->h = h;
    fb_reset_clip(f);
    return 0;
}

void fb_set_clip(Fb *f, int x, int y, int w, int h)
{
    f->cx0 = x < 0 ? 0 : x;
    f->cy0 = y < 0 ? 0 : y;
    f->cx1 = x + w > f->w ? f->w : x + w;
    f->cy1 = y + h > f->h ? f->h : y + h;
}

void fb_reset_clip(Fb *f) { f->cx0 = 0; f->cy0 = 0; f->cx1 = f->w; f->cy1 = f->h; }

uint32_t gfx_blend(uint32_t dst, uint32_t src)
{
    uint32_t a = src >> 24;
    if (a == 255) return src | 0xFF000000u;
    if (a == 0) return dst;
    uint32_t ia = 255 - a;
    uint32_t r = (((src >> 16) & 255) * a + ((dst >> 16) & 255) * ia + 127) / 255;
    uint32_t g = (((src >> 8) & 255) * a + ((dst >> 8) & 255) * ia + 127) / 255;
    uint32_t b = ((src & 255) * a + (dst & 255) * ia + 127) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void fb_clear(Fb *f, uint32_t argb)
{
    for (size_t i = 0, n = (size_t)f->w * (size_t)f->h; i < n; i++) f->px[i] = argb;
}

void fb_plot(Fb *f, int x, int y, uint32_t argb)
{
    if (x < f->cx0 || y < f->cy0 || x >= f->cx1 || y >= f->cy1) return;
    uint32_t *p = &f->px[(size_t)y * (size_t)f->w + (size_t)x];
    *p = gfx_blend(*p, argb);
}

uint32_t fb_get(const Fb *f, int x, int y)
{
    if (x < 0 || y < 0 || x >= f->w || y >= f->h) return 0;
    return f->px[(size_t)y * (size_t)f->w + (size_t)x];
}

void fb_rect(Fb *f, int x, int y, int w, int h, uint32_t argb)
{
    int x0 = x < f->cx0 ? f->cx0 : x, y0 = y < f->cy0 ? f->cy0 : y;
    int x1 = x + w > f->cx1 ? f->cx1 : x + w, y1 = y + h > f->cy1 ? f->cy1 : y + h;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = &f->px[(size_t)j * (size_t)f->w];
        if ((argb >> 24) == 255) for (int i = x0; i < x1; i++) row[i] = argb;
        else for (int i = x0; i < x1; i++) row[i] = gfx_blend(row[i], argb);
    }
}

void fb_frame(Fb *f, int x, int y, int w, int h, uint32_t argb)
{
    fb_rect(f, x, y, w, 1, argb);
    fb_rect(f, x, y + h - 1, w, 1, argb);
    fb_rect(f, x, y + 1, 1, h - 2, argb);
    fb_rect(f, x + w - 1, y + 1, 1, h - 2, argb);
}

void fb_gradient_v(Fb *f, int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    for (int j = 0; j < h; j++) {
        int t = h > 1 ? j * 255 / (h - 1) : 0;
        uint32_t c = 0;
        for (int s = 0; s < 32; s += 8) {
            uint32_t a = (top >> s) & 255, b = (bottom >> s) & 255;
            c |= (uint32_t)((a * (255 - t) + b * t) / 255) << s;
        }
        fb_rect(f, x, y + j, w, 1, c);
    }
}

void fb_or_reverse(Fb *f, int x, int y, int w, int h, uint32_t rgb)
{
    int x0 = x < f->cx0 ? f->cx0 : x, y0 = y < f->cy0 ? f->cy0 : y;
    int x1 = x + w > f->cx1 ? f->cx1 : x + w, y1 = y + h > f->cy1 ? f->cy1 : y + h;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            uint32_t *p = &f->px[(size_t)j * (size_t)f->w + (size_t)i];
            *p = 0xFF000000u | ((rgb & 0xFFFFFFu) | (~*p & 0xFFFFFFu));
        }
}

void fb_blit(Fb *dst, const Fb *src, int sx, int sy, int w, int h, int dx, int dy)
{
    for (int j = 0; j < h; j++) {
        int y = dy + j;
        if (y < dst->cy0 || y >= dst->cy1 || sy + j < 0 || sy + j >= src->h) continue;
        for (int i = 0; i < w; i++) {
            int x = dx + i;
            if (x < dst->cx0 || x >= dst->cx1 || sx + i < 0 || sx + i >= src->w) continue;
            uint32_t c = src->px[(size_t)(sy + j) * (size_t)src->w + (size_t)(sx + i)];
            if ((c >> 24) == 0) continue;
            uint32_t *p = &dst->px[(size_t)y * (size_t)dst->w + (size_t)x];
            *p = gfx_blend(*p, c);
        }
    }
}

void fb_tile_dim(Fb *dst, const Fb *tile, int scale, int bright)
{
    for (int y = 0; y < dst->h; y++)
        for (int x = 0; x < dst->w; x++) {
            uint32_t c = tile->px[(size_t)((y / scale) % tile->h) * (size_t)tile->w + (size_t)((x / scale) % tile->w)];
            uint32_t r = ((c >> 16) & 255) * (uint32_t)bright / 255;
            uint32_t g = ((c >> 8) & 255) * (uint32_t)bright / 255;
            uint32_t b = (c & 255) * (uint32_t)bright / 255;
            dst->px[(size_t)y * (size_t)dst->w + (size_t)x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
}
