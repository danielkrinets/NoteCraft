#include "theme.h"

#include <stdlib.h>

#define C(r, g, b) (0xFF000000u | ((r) << 16) | ((g) << 8) | (b))

static uint32_t hash2(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void px(Fb *f, int x, int y, uint32_t c)
{
    if (x >= 0 && y >= 0 && x < f->w && y < f->h) f->px[(size_t)y * (size_t)f->w + (size_t)x] = c;
}

/* ---- leather -------------------------------------------------------------- */
static uint32_t leather_px(int x, int y)
{
    static const uint32_t base = C(0x5e, 0x3a, 0x1e), dark = C(0x50, 0x31, 0x19), darker = C(0x45, 0x29, 0x14),
                          light = C(0x6c, 0x45, 0x25), spot = C(0x7a, 0x50, 0x2c);
    uint32_t h = hash2(x, y) % 64;
    uint32_t g = hash2(x / 4, y) % 9;                    /* short horizontal grain streaks */
    if (h < 3) return spot;
    if (h < 11 || g == 0) return light;
    if (h < 22) return dark;
    if (h < 26) return darker;
    return base;
}

/* ---- the open book ---------------------------------------------------------- */
#define W BOOK_SPREAD_W
#define H BOOK_SPREAD_H
#define SPINE 150
#define PX0 9
#define PX1 290
#define PY0 9
#define PY1 189

static uint32_t book_px(int x, int y)
{
    int d = x;
    if (W - 1 - x < d) d = W - 1 - x;
    if (y < d) d = y;
    if (H - 1 - y < d) d = H - 1 - y;

    int in_pages = x >= PX0 && x <= PX1 && y >= PY0 && y <= PY1;
    if (!in_pages) {
        /* leather cover with an outline, a lit top-left and a shaded bottom-right edge */
        if (d == 0) return C(0x1c, 0x10, 0x09);
        if (d == 1) return C(0x3b, 0x24, 0x12);
        if (d == 2) return (x < W / 2 && y < H / 2) || y == 2 || x == 2 ? C(0x86, 0x58, 0x30) : C(0x4a, 0x2d, 0x16);
        /* stitching, 5px in from the edge */
        if (d == 5 && ((x + y) & 3) < 2) return C(0xa8, 0x78, 0x44);
        if (d == 6 && ((x + y) & 3) < 2) return C(0x3b, 0x24, 0x12);
        /* seam of the spine on the cover, above and below the pages */
        if (x == SPINE - 1 || x == SPINE) return x == SPINE - 1 ? C(0x3b, 0x24, 0x12) : C(0x4a, 0x2d, 0x16);
        return leather_px(x, y);
    }
    /* pages: outer page-stack edge */
    int ex = x - PX0 < PX1 - x ? x - PX0 : PX1 - x;       /* 0.. from the outer side */
    static const uint32_t stack[4] = {C(0x8a, 0x74, 0x49), C(0xef, 0xe3, 0xbc), C(0xd2, 0xbe, 0x8c), C(0xe8, 0xda, 0xb0)};
    if (ex < 4) return stack[ex];
    if (y == PY0) return stack[0];
    if (y == PY0 + 1) return stack[1];
    if (y >= PY1 - 3) return stack[PY1 - y];
    /* gutter: hard-edged darker bands towards the spine */
    int g = x < SPINE ? SPINE - x : x - (SPINE - 1);
    if (g <= 1) return C(0x86, 0x71, 0x47);
    if (g <= 2) return C(0xa6, 0x91, 0x64);
    if (g <= 4) return C(0xc2, 0xad, 0x7c);
    if (g <= 7) return C(0xd4, 0xc1, 0x90);
    uint32_t h = hash2(x, y);
    if (h % 31 == 0) return C(0xdc, 0xca, 0x98);
    if (h % 47 == 0) return C(0xee, 0xe0, 0xb6);
    return C(0xe5, 0xd5, 0xa5);
}

static Fb *make_book(void)
{
    Fb *f = fb_new(W, H);
    if (!f) return NULL;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) px(f, x, y, book_px(x, y));
    return f;
}

static void arrow(Fb *f, int ox, int oy, int forward, int hover)
{
    const int AW = 23, AH = 13;
    const uint32_t outline = C(0x1c, 0x10, 0x09);
    const uint32_t fill = hover ? C(0xf2, 0xe8, 0xc2) : C(0x4a, 0x36, 0x20);
    const uint32_t shade = hover ? C(0xc6, 0xb4, 0x84) : C(0x2e, 0x1f, 0x10);
    for (int y = 0; y < AH; y++)
        for (int x = 0; x < AW; x++) {
            int fx = forward ? x : AW - 1 - x, inside = 0;
            if (fx >= 1 && fx <= 13 && y >= 4 && y <= 8) inside = 1;
            if (fx >= 11 && fx <= 21) { int half = (21 - fx) * 6 / 10; if (y >= 6 - half && y <= 6 + half) inside = 1; }
            if (inside) px(f, ox + x, oy + y, fill);
        }
    for (int y = 0; y < AH; y++)
        for (int x = 0; x < AW; x++) {
            if (f->px[(size_t)(oy + y) * (size_t)f->w + (size_t)(ox + x)] != fill) continue;
            static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; k++) {
                int nx = x + dx[k], ny = y + dy[k];
                int in = nx >= 0 && ny >= 0 && nx < AW && ny < AH && f->px[(size_t)(oy + ny) * (size_t)f->w + (size_t)(ox + nx)] != 0;
                if (!in) { px(f, ox + x, oy + y, outline); break; }
            }
        }
    for (int y = 1; y < AH - 1; y++)
        for (int x = 1; x < AW - 1; x++) {
            uint32_t *p = &f->px[(size_t)(oy + y) * (size_t)f->w + (size_t)(ox + x)];
            if (*p == fill && f->px[(size_t)(oy + y + 1) * (size_t)f->w + (size_t)(ox + x)] == outline) *p = shade;
        }
}

Theme *theme_new(const char *unused_pack_dir)
{
    (void)unused_pack_dir;
    Theme *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->book = make_book();
    t->arrows = fb_new(46, 26);
    t->leather = fb_new(32, 32);
    if (!t->book || !t->arrows || !t->leather) { theme_free(t); return NULL; }
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) px(t->leather, x, y, leather_px(x, y));
    fb_clear(t->arrows, 0);
    arrow(t->arrows, 0, 0, 1, 0);
    arrow(t->arrows, 23, 0, 1, 1);
    arrow(t->arrows, 0, 13, 0, 0);
    arrow(t->arrows, 23, 13, 0, 1);
    return t;
}

void theme_free(Theme *t)
{
    if (!t) return;
    fb_free(t->book);
    fb_free(t->arrows);
    fb_free(t->leather);
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
