/* Minimal ZIP reader/writer for ePub (stored entries on write, stored or
 * deflate on read; no ZIP64). */
#ifndef NC_ZIPIO_H
#define NC_ZIPIO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char name[200];
    uint32_t crc, size, offset;
} ZipWEntry;

typedef struct {
    uint8_t *buf;
    size_t len, cap;
    ZipWEntry *ents;
    int n, ecap;
    int failed;
} ZipW;

void zipw_init(ZipW *z);
int  zipw_add(ZipW *z, const char *name, const void *data, size_t n);
/* Appends the central directory. On success the caller owns the archive (*out, *outn). */
int  zipw_finish(ZipW *z, uint8_t **out, size_t *outn);
void zipw_free(ZipW *z);

typedef struct {
    char name[256];
    uint16_t method;
    uint32_t csize, usize, offset;
} ZipREntry;

typedef struct {
    const uint8_t *d;
    size_t n;
    ZipREntry *ents;
    int count;
} ZipR;

int  zipr_open(ZipR *z, const uint8_t *data, size_t n);   /* 0 on success */
void zipr_close(ZipR *z);
int  zipr_find(const ZipR *z, const char *name);          /* index or -1 */
/* Decompresses entry idx into a malloc'd, NUL-terminated buffer (max 32MB). */
uint8_t *zipr_read(const ZipR *z, int idx, size_t *outn);

#endif
