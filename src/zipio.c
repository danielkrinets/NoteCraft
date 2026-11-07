#include "zipio.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define MAX_ENTRY_BYTES (32u * 1024u * 1024u)
#define MAX_ENTRIES 5000

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return get16(p) | (get16(p + 2) << 16); }

/* ---- writer ---- */

void zipw_init(ZipW *z) { memset(z, 0, sizeof *z); }

static int zw_grow(ZipW *z, size_t extra)
{
    if (z->len + extra <= z->cap) return 0;
    size_t nc = z->cap ? z->cap : 4096;
    while (nc < z->len + extra) nc *= 2;
    uint8_t *p = realloc(z->buf, nc);
    if (!p) { z->failed = 1; return -1; }
    z->buf = p; z->cap = nc;
    return 0;
}

#define DOS_TIME 0u
#define DOS_DATE ((2024u - 1980u) << 9 | 1u << 5 | 1u)

int zipw_add(ZipW *z, const char *name, const void *data, size_t n)
{
    size_t nl = strlen(name);
    if (z->failed || nl >= sizeof z->ents[0].name || n > 0xFFFFFFF0u) return -1;
    if (z->n == z->ecap) {
        int nc = z->ecap ? z->ecap * 2 : 32;
        ZipWEntry *e = realloc(z->ents, (size_t)nc * sizeof *e);
        if (!e) { z->failed = 1; return -1; }
        z->ents = e; z->ecap = nc;
    }
    uint32_t crc = (uint32_t)crc32(0, (const Bytef *)data, (uInt)n);
    if (zw_grow(z, 30 + nl + n) < 0) return -1;
    uint8_t *p = z->buf + z->len;
    put32(p, 0x04034b50); put16(p + 4, 20); put16(p + 6, 0x0800); put16(p + 8, 0);
    put16(p + 10, DOS_TIME); put16(p + 12, DOS_DATE);
    put32(p + 14, crc); put32(p + 18, (uint32_t)n); put32(p + 22, (uint32_t)n);
    put16(p + 26, (uint32_t)nl); put16(p + 28, 0);
    memcpy(p + 30, name, nl);
    if (n) memcpy(p + 30 + nl, data, n);
    ZipWEntry *e = &z->ents[z->n++];
    memcpy(e->name, name, nl + 1);
    e->crc = crc; e->size = (uint32_t)n; e->offset = (uint32_t)z->len;
    z->len += 30 + nl + n;
    return 0;
}

int zipw_finish(ZipW *z, uint8_t **out, size_t *outn)
{
    if (z->failed) return -1;
    size_t cd_off = z->len;
    for (int i = 0; i < z->n; i++) {
        ZipWEntry *e = &z->ents[i];
        size_t nl = strlen(e->name);
        if (zw_grow(z, 46 + nl) < 0) return -1;
        uint8_t *p = z->buf + z->len;
        put32(p, 0x02014b50); put16(p + 4, 20); put16(p + 6, 20); put16(p + 8, 0x0800);
        put16(p + 10, 0); put16(p + 12, DOS_TIME); put16(p + 14, DOS_DATE);
        put32(p + 16, e->crc); put32(p + 20, e->size); put32(p + 24, e->size);
        put16(p + 28, (uint32_t)nl); put16(p + 30, 0); put16(p + 32, 0);
        put16(p + 34, 0); put16(p + 36, 0); put32(p + 38, 0); put32(p + 42, e->offset);
        memcpy(p + 46, e->name, nl);
        z->len += 46 + nl;
    }
    size_t cd_size = z->len - cd_off;
    if (zw_grow(z, 22) < 0) return -1;
    uint8_t *p = z->buf + z->len;
    put32(p, 0x06054b50); put16(p + 4, 0); put16(p + 6, 0);
    put16(p + 8, (uint32_t)z->n); put16(p + 10, (uint32_t)z->n);
    put32(p + 12, (uint32_t)cd_size); put32(p + 16, (uint32_t)cd_off); put16(p + 20, 0);
    z->len += 22;
    *out = z->buf; *outn = z->len;
    z->buf = NULL; z->len = z->cap = 0;
    return 0;
}

void zipw_free(ZipW *z)
{
    free(z->buf);
    free(z->ents);
    memset(z, 0, sizeof *z);
}

/* ---- reader ---- */

int zipr_open(ZipR *z, const uint8_t *d, size_t n)
{
    memset(z, 0, sizeof *z);
    if (n < 22) return -1;
    size_t lo = n > 22 + 65535 ? n - 22 - 65535 : 0;
    size_t eocd = (size_t)-1;
    for (size_t i = n - 22 + 1; i-- > lo;)
        if (get32(d + i) == 0x06054b50) { eocd = i; break; }
    if (eocd == (size_t)-1) return -1;
    uint32_t count = get16(d + eocd + 10), cd_size = get32(d + eocd + 12), cd_off = get32(d + eocd + 16);
    if (count > MAX_ENTRIES || (size_t)cd_off + cd_size > n) return -1;
    z->ents = calloc(count ? count : 1, sizeof *z->ents);
    if (!z->ents) return -1;
    size_t p = cd_off;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 46 > n || get32(d + p) != 0x02014b50) { free(z->ents); z->ents = NULL; return -1; }
        uint32_t nl = get16(d + p + 28), xl = get16(d + p + 30), cl = get16(d + p + 32);
        if (p + 46 + nl + xl + cl > n) { free(z->ents); z->ents = NULL; return -1; }
        ZipREntry *e = &z->ents[i];
        e->method = (uint16_t)get16(d + p + 10);
        e->csize = get32(d + p + 20); e->usize = get32(d + p + 24); e->offset = get32(d + p + 42);
        size_t cp = nl < sizeof e->name - 1 ? nl : sizeof e->name - 1;
        memcpy(e->name, d + p + 46, cp);
        e->name[cp] = 0;
        p += 46 + nl + xl + cl;
    }
    z->d = d; z->n = n; z->count = (int)count;
    return 0;
}

void zipr_close(ZipR *z) { free(z->ents); memset(z, 0, sizeof *z); }

int zipr_find(const ZipR *z, const char *name)
{
    for (int i = 0; i < z->count; i++) if (strcmp(z->ents[i].name, name) == 0) return i;
    return -1;
}

uint8_t *zipr_read(const ZipR *z, int idx, size_t *outn)
{
    if (idx < 0 || idx >= z->count) return NULL;
    const ZipREntry *e = &z->ents[idx];
    if (e->usize > MAX_ENTRY_BYTES) return NULL;
    size_t lh = e->offset;
    if (lh + 30 > z->n || get32(z->d + lh) != 0x04034b50) return NULL;
    size_t data = lh + 30 + get16(z->d + lh + 26) + get16(z->d + lh + 28);
    if (data + e->csize > z->n) return NULL;
    uint8_t *out = malloc((size_t)e->usize + 1);
    if (!out) return NULL;
    if (e->method == 0) {
        if (e->csize != e->usize) { free(out); return NULL; }
        memcpy(out, z->d + data, e->usize);
    } else if (e->method == 8) {
        z_stream s; memset(&s, 0, sizeof s);
        if (inflateInit2(&s, -15) != Z_OK) { free(out); return NULL; }
        s.next_in = (Bytef *)(uintptr_t)(z->d + data); s.avail_in = e->csize;
        s.next_out = out; s.avail_out = e->usize;
        int r = inflate(&s, Z_FINISH);
        inflateEnd(&s);
        if (r != Z_STREAM_END || s.total_out != e->usize) { free(out); return NULL; }
    } else { free(out); return NULL; }
    out[e->usize] = 0;
    if (outn) *outn = e->usize;
    return out;
}
