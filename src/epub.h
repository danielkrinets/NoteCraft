/* ePub 3 export and import.
 *
 * Export writes one XHTML file per book page (so pages survive a round trip),
 * plus nav.xhtml, toc.ncx and a stylesheet. Import reads any reasonable ePub:
 * books written by NoteCraft keep their exact pages, everything else is
 * converted to plain text and flowed into book pages. */
#ifndef NC_EPUB_H
#define NC_EPUB_H

#include <stddef.h>
#include <stdint.h>

#include "book.h"
#include "txt.h"

#define NOTECRAFT_VERSION "1.0.0"

/* `fallback_title` is used when the book has no title (may be NULL). */
int epub_export_mem(const Book *b, const char *fallback_title, uint8_t **out, size_t *outn);
int epub_export(const Book *b, const char *fallback_title, const char *path, char *err, size_t errn);

int epub_import_mem(Book *b, const uint8_t *data, size_t n, ImportInfo *info, char *err, size_t errn);

#endif
