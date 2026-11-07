#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static int chunk(uint8_t **buf, size_t *len, size_t *cap, const char *type, const uint8_t *d, size_t n)
{
    if (*len + n + 12 > *cap) {
        size_t nc = (*len + n + 12) * 2;
        uint8_t *p = realloc(*buf, nc);
        if (!p) return -1;
        *buf = p; *cap = nc;
    }
    uint8_t *o = *buf + *len;
    be32(o, (uint32_t)n);
    memcpy(o + 4, type, 4);
    if (n) memcpy(o + 8, d, n);
    uint32_t c = (uint32_t)crc32(0, o + 4, (uInt)(n + 4));
    be32(o + 8 + n, c);
    *len += n + 12;
    return 0;
}

int png_encode(const uint32_t *argb, int w, int h, uint8_t **out, size_t *outn)
{
    size_t stride = (size_t)w * 4 + 1;
    uint8_t *raw = malloc(stride * (size_t)h);
    if (!raw) return -1;
    for (int y = 0; y < h; y++) {
        uint8_t *r = raw + stride * (size_t)y;
        *r++ = 0;
        for (int x = 0; x < w; x++) {
            uint32_t c = argb[(size_t)y * (size_t)w + (size_t)x];
            *r++ = (uint8_t)(c >> 16); *r++ = (uint8_t)(c >> 8); *r++ = (uint8_t)c; *r++ = (uint8_t)(c >> 24);
        }
    }
    uLongf zl = compressBound((uLong)(stride * (size_t)h));
    uint8_t *z = malloc(zl);
    if (!z) { free(raw); return -1; }
    if (compress2(z, &zl, raw, (uLong)(stride * (size_t)h), 6) != Z_OK) { free(raw); free(z); return -1; }
    free(raw);

    uint8_t *buf = NULL; size_t len = 0, cap = 0;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    buf = malloc(zl + 128); cap = zl + 128;
    if (!buf) { free(z); return -1; }
    memcpy(buf, sig, 8); len = 8;
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w); be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    int rc = chunk(&buf, &len, &cap, "IHDR", ihdr, 13);
    if (!rc) rc = chunk(&buf, &len, &cap, "IDAT", z, zl);
    if (!rc) rc = chunk(&buf, &len, &cap, "IEND", NULL, 0);
    free(z);
    if (rc) { free(buf); return -1; }
    *out = buf; *outn = len;
    return 0;
}

int png_write_fb(const char *path, const Fb *f, int scale)
{
    if (scale < 1) scale = 1;
    int w = f->w * scale, h = f->h * scale;
    uint32_t *px = malloc((size_t)w * (size_t)h * 4);
    if (!px) return -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            px[(size_t)y * (size_t)w + (size_t)x] = f->px[(size_t)(y / scale) * (size_t)f->w + (size_t)(x / scale)] | 0xFF000000u;
    uint8_t *png; size_t n;
    int rc = png_encode(px, w, h, &png, &n);
    free(px);
    if (rc) return -1;
    FILE *fp = fopen(path, "wb");
    if (!fp) { free(png); return -1; }
    rc = fwrite(png, 1, n, fp) == n ? 0 : -1;
    if (fclose(fp)) rc = -1;
    free(png);
    return rc;
}

static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

Fb *png_read_fb(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 33 || sz > 64L * 1024 * 1024) { fclose(fp); return NULL; }
    uint8_t *d = malloc((size_t)sz);
    if (!d || fread(d, 1, (size_t)sz, fp) != (size_t)sz) { free(d); fclose(fp); return NULL; }
    fclose(fp);
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (memcmp(d, sig, 8)) { free(d); return NULL; }

    uint32_t w = 0, h = 0; int depth = 0, ctype = 0, interlace = 0;
    uint8_t *idat = NULL; size_t idat_n = 0;
    uint8_t pal[256][4]; int npal = 0;
    size_t p = 8; int ok = 1;
    while (p + 12 <= (size_t)sz) {
        uint32_t len = rd32(d + p);
        const uint8_t *type = d + p + 4, *body = d + p + 8;
        if (p + 12 + len > (size_t)sz) { ok = 0; break; }
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            w = rd32(body); h = rd32(body + 4); depth = body[8]; ctype = body[9]; interlace = body[12];
        } else if (!memcmp(type, "PLTE", 4)) {
            npal = (int)(len / 3) > 256 ? 256 : (int)(len / 3);
            for (int i = 0; i < npal; i++) { pal[i][0] = body[i * 3]; pal[i][1] = body[i * 3 + 1]; pal[i][2] = body[i * 3 + 2]; pal[i][3] = 255; }
        } else if (!memcmp(type, "tRNS", 4) && ctype == 3) {
            for (uint32_t i = 0; i < len && i < 256; i++) pal[i][3] = body[i];
        } else if (!memcmp(type, "IDAT", 4)) {
            uint8_t *t = realloc(idat, idat_n + len);
            if (!t) { ok = 0; break; }
            idat = t; memcpy(idat + idat_n, body, len); idat_n += len;
        } else if (!memcmp(type, "IEND", 4)) break;
        p += 12 + len;
    }
    Fb *fb = NULL;
    int ch = ctype == 6 ? 4 : ctype == 2 ? 3 : ctype == 0 ? 1 : ctype == 4 ? 2 : ctype == 3 ? 1 : 0;
    if (ok && w && h && w <= 4096 && h <= 4096 && depth == 8 && !interlace && ch && idat) {
        size_t stride = (size_t)w * (size_t)ch;
        size_t rawn = (stride + 1) * h;
        uint8_t *raw = malloc(rawn);
        uLongf rl = (uLongf)rawn;
        if (raw && uncompress(raw, &rl, idat, (uLong)idat_n) == Z_OK && rl == rawn) {
            fb = fb_new((int)w, (int)h);
            if (fb) {
                for (uint32_t y = 0; y < h; y++) {
                    uint8_t *row = raw + (stride + 1) * y;
                    uint8_t *prev = y ? raw + (stride + 1) * (y - 1) + 1 : NULL;
                    int ft = row[0];
                    uint8_t *cur = row + 1;
                    for (size_t i = 0; i < stride; i++) {
                        int a = i >= (size_t)ch ? cur[i - (size_t)ch] : 0;
                        int b = prev ? prev[i] : 0;
                        int c = (prev && i >= (size_t)ch) ? prev[i - (size_t)ch] : 0;
                        int v = cur[i];
                        switch (ft) {
                        case 1: v += a; break;
                        case 2: v += b; break;
                        case 3: v += (a + b) / 2; break;
                        case 4: v += paeth(a, b, c); break;
                        default: break;
                        }
                        cur[i] = (uint8_t)v;
                    }
                    for (uint32_t x = 0; x < w; x++) {
                        const uint8_t *s = cur + (size_t)x * (size_t)ch;
                        uint32_t r, g, bl, al = 255;
                        if (ctype == 6) { r = s[0]; g = s[1]; bl = s[2]; al = s[3]; }
                        else if (ctype == 2) { r = s[0]; g = s[1]; bl = s[2]; }
                        else if (ctype == 0) { r = g = bl = s[0]; }
                        else if (ctype == 4) { r = g = bl = s[0]; al = s[1]; }
                        else { int i = s[0] < npal ? s[0] : 0; r = pal[i][0]; g = pal[i][1]; bl = pal[i][2]; al = pal[i][3]; }
                        fb->px[(size_t)y * w + x] = (al << 24) | (r << 16) | (g << 8) | bl;
                    }
                }
            }
        }
        free(raw);
    }
    free(idat); free(d);
    return fb;
}
