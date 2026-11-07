#include "utf.h"

int u8_decode(const char *s, size_t n, uint32_t *cp)
{
    const unsigned char *p = (const unsigned char *)s;
    if (n == 0) { *cp = 0xFFFD; return 1; }
    unsigned c = p[0];
    if (c < 0x80) { *cp = c; return 1; }
    int len; uint32_t v, min;
    if ((c & 0xE0) == 0xC0)      { len = 2; v = c & 0x1F; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { len = 3; v = c & 0x0F; min = 0x800; }
    else if ((c & 0xF8) == 0xF0) { len = 4; v = c & 0x07; min = 0x10000; }
    else { *cp = 0xFFFD; return 1; }
    if ((size_t)len > n) { *cp = 0xFFFD; return 1; }
    for (int i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        v = (v << 6) | (p[i] & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) { *cp = 0xFFFD; return 1; }
    *cp = v;
    return len;
}

int u8_encode(uint32_t cp, char out[4])
{
    if (cp < 0x80)    { out[0] = (char)cp; return 1; }
    if (cp < 0x800)   { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F)); return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

size_t u8_to_cps(const char *s, size_t n, uint32_t *out, size_t cap)
{
    size_t i = 0, k = 0;
    while (i < n && k < cap) {
        uint32_t cp;
        i += (size_t)u8_decode(s + i, n - i, &cp);
        out[k++] = cp;
    }
    return k;
}

size_t cps_to_u8(const uint32_t *cp, int n, char *out, size_t cap)
{
    size_t w = 0;
    if (cap == 0) return 0;
    for (int i = 0; i < n; i++) {
        char b[4];
        int l = u8_encode(cp[i], b);
        if (w + (size_t)l + 1 > cap) break;
        for (int j = 0; j < l; j++) out[w++] = b[j];
    }
    out[w] = 0;
    return w;
}

int cps_units(const uint32_t *cp, int n)
{
    int u = 0;
    for (int i = 0; i < n; i++) u += cp_units(cp[i]);
    return u;
}
