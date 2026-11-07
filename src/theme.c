#include "theme.h"

#include <stdio.h>
#include <stdlib.h>

#include "png.h"

#define C(r, g, b) (0xFF000000u | ((r) << 16) | ((g) << 8) | (b))

static void px(Fb *f, int x, int y, uint32_t c)
{
    if (x >= 0 && y >= 0 && x < f->w && y < f->h) f->px[(size_t)y * (size_t)f->w + (size_t)x] = c;
}

static uint32_t lerp(uint32_t a, uint32_t b, int t, int max)
{
    uint32_t o = 0xFF000000u;
    for (int s = 0; s < 24; s += 8) {
        int x = (int)((a >> s) & 255), y = (int)((b >> s) & 255);
        o |= (uint32_t)((x * (max - t) + y * t) / max) << s;
    }
    return o;
}

/* Colour of the page at (x,y), 0 = transparent. */
static uint32_t page_pixel(int x, int y)
{
    const int W = 192, H = 192;
    /* chamfered corners */
    {
        int dx = x < W - 1 - x ? x : W - 1 - x, dy = y < H - 1 - y ? y : H - 1 - y;
        if (dx + dy < 2) return 0;
    }

    int d = x;
    if (W - 1 - x < d) d = W - 1 - x;
    if (y < d) d = y;
    if (H - 1 - y < d) d = H - 1 - y;

    static const uint32_t frame[] = {
        C(0x1d, 0x12, 0x0a), C(0x4a, 0x2d, 0x15), C(0x74, 0x4a, 0x24), C(0x8c, 0x5e, 0x31),
        C(0x5a, 0x37, 0x1b), C(0x2e, 0x1b, 0x0e),
    };
    if (d < 6) {
        /* a touch of light from the top-left on the leather frame */
        uint32_t c = frame[d];
        if (d >= 1 && d <= 3 && (y < 3 || x < 3) && d > 0) c = lerp(c, C(0xb0, 0x7c, 0x45), 1, 3);
        return c;
    }
    const uint32_t page = C(0xe9, 0xdb, 0xb2);
    if (d == 6) return C(0xc9, 0xb5, 0x84);
    if (d == 7) return C(0xdc, 0xca, 0x9b);
    uint32_t c = page;
    /* bound-edge shadow along the spine (left) */
    if (x < 22) {
        int t = 22 - x;                 /* 0..14 */
        c = lerp(page, C(0xb4, 0x9d, 0x6a), t, 16);
        if (x == 8 || x == 11) c = lerp(c, C(0x9a, 0x82, 0x52), 1, 2);
    }
    /* soft paper edge on the other sides */
    if (W - 1 - x < 10) c = lerp(c, C(0xd7, 0xc6, 0x96), 10 - (W - 1 - x), 11);
    if (y < 10)         c = lerp(c, C(0xd7, 0xc6, 0x96), 10 - y, 11);
    if (H - 1 - y < 10) c = lerp(c, C(0xd7, 0xc6, 0x96), 10 - (H - 1 - y), 11);
    return c;
}

static void arrow(Fb *f, int ox, int oy, int forward, int hover)
{
    const int W = 23, H = 13;
    const uint32_t outline = C(0x1d, 0x12, 0x0a);
    const uint32_t fill = hover ? C(0xf4, 0xea, 0xc6) : C(0x4a, 0x36, 0x20);
    const uint32_t shade = hover ? C(0xc8, 0xb6, 0x86) : C(0x2e, 0x1f, 0x10);
    /* right-pointing arrow: shaft + head */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int fx = forward ? x : W - 1 - x;
            int inside = 0;
            if (fx >= 1 && fx <= 13 && y >= 4 && y <= 8) inside = 1;
            if (fx >= 11 && fx <= 21) {
                int half = (21 - fx) * 6 / 10;
                if (y >= 6 - half && y <= 6 + half) inside = 1;
            }
            if (!inside) continue;
            px(f, ox + x, oy + y, fill);
        }
    /* outline & bottom shade */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (f->px[(size_t)(oy + y) * (size_t)f->w + (size_t)(ox + x)] != fill) continue;
            int edge = 0, below = 0;
            static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; k++) {
                int nx = x + dx[k], ny = y + dy[k];
                int in = nx >= 0 && ny >= 0 && nx < W && ny < H &&
                         f->px[(size_t)(oy + ny) * (size_t)f->w + (size_t)(ox + nx)] != 0;
                if (!in) edge = 1;
            }
            (void)below;
            if (edge) px(f, ox + x, oy + y, outline);
        }
    /* second pass: pixels that sit on the lower edge of the interior get a shade */
    for (int y = 1; y < H - 1; y++)
        for (int x = 1; x < W - 1; x++) {
            uint32_t *p = &f->px[(size_t)(oy + y) * (size_t)f->w + (size_t)(ox + x)];
            uint32_t *dn = &f->px[(size_t)(oy + y + 1) * (size_t)f->w + (size_t)(ox + x)];
            if (*p == fill && *dn == outline) *p = shade;
        }
}

static Fb *make_book(void)
{
    Fb *f = fb_new(256, 256);
    if (!f) return NULL;
    fb_clear(f, 0);
    for (int y = 0; y < 192; y++)
        for (int x = 0; x < 192; x++) px(f, x, y, page_pixel(x, y));
    arrow(f, 0, 192, 1, 0);
    arrow(f, 23, 192, 1, 1);
    arrow(f, 0, 205, 0, 0);
    arrow(f, 23, 205, 0, 1);
    return f;
}

static Fb *make_dirt(void)
{
    Fb *f = fb_new(16, 16);
    if (!f) return NULL;
    static const uint32_t pal[] = {
        C(134, 96, 67), C(121, 85, 58), C(150, 108, 76), C(104, 73, 50), C(116, 82, 56), C(142, 101, 70),
    };
    uint32_t s = 0x1234567u;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            s = s * 1664525u + 1013904223u;
            uint32_t r = (s >> 16) & 0xFF;
            uint32_t c = pal[r % 6];
            if (r > 236) c = C(108, 108, 108);        /* pebbles */
            else if (r < 10) c = C(88, 60, 40);
            px(f, x, y, c);
        }
    return f;
}

Theme *theme_new(const char *pack_dir)
{
    Theme *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->book = make_book();
    t->dirt = make_dirt();
    if (pack_dir && *pack_dir) {
        char path[1100];
        snprintf(path, sizeof path, "%s/assets/minecraft/textures/gui/book.png", pack_dir);
        Fb *b = png_read_fb(path);
        if (b && b->w >= 192 && b->h >= 192) {
            /* Normalise to a 256x256 atlas (vanilla size) so coordinates match. */
            Fb *atlas = fb_new(256, 256);
            if (atlas) {
                fb_clear(atlas, 0);
                int sc = b->w / 256 > 0 ? b->w / 256 : 1;
                if (sc == 1) fb_blit(atlas, b, 0, 0, b->w < 256 ? b->w : 256, b->h < 256 ? b->h : 256, 0, 0);
                if (sc == 1) { fb_free(t->book); t->book = atlas; t->custom_book = 1; }
                else fb_free(atlas);
            }
        }
        fb_free(b);
        snprintf(path, sizeof path, "%s/assets/minecraft/textures/gui/options_background.png", pack_dir);
        Fb *d = png_read_fb(path);
        if (d && d->w >= 8 && d->h >= 8) { fb_free(t->dirt); t->dirt = d; t->custom_dirt = 1; }
        else fb_free(d);
    }
    if (!t->book || !t->dirt) { theme_free(t); return NULL; }
    return t;
}

void theme_free(Theme *t)
{
    if (!t) return;
    fb_free(t->book);
    fb_free(t->dirt);
    free(t);
}

void theme_button(Fb *f, int x, int y, int w, int h, int state)
{
    uint32_t face = state == BTN_HOVER ? C(0x6f, 0x7f, 0xae) : state == BTN_DISABLED ? C(0x4a, 0x4a, 0x4a) : C(0x70, 0x70, 0x70);
    uint32_t hi = state == BTN_DISABLED ? C(0x6a, 0x6a, 0x6a) : state == BTN_HOVER ? C(0xb8, 0xc4, 0xef) : C(0xc8, 0xc8, 0xc8);
    uint32_t lo = state == BTN_DISABLED ? C(0x2e, 0x2e, 0x2e) : state == BTN_HOVER ? C(0x38, 0x42, 0x68) : C(0x3a, 0x3a, 0x3a);
    fb_rect(f, x, y, w, h, C(0, 0, 0));
    fb_rect(f, x + 1, y + 1, w - 2, h - 2, face);
    fb_rect(f, x + 1, y + 1, w - 2, 1, hi);
    fb_rect(f, x + 1, y + 1, 1, h - 2, hi);
    fb_rect(f, x + 1, y + h - 3, w - 2, 2, lo);
    fb_rect(f, x + w - 2, y + 2, 1, h - 4, lo);
}
