/* Plain-text import/export and pagination.
 *
 * File format: UTF-8 text. Pages are separated by a form feed (U+000C), the
 * classic "page break" of plain text. Each page is written followed by "\n".
 * Text without form feeds is flowed into pages automatically, exactly as the
 * book would wrap it (114px lines, 14 lines per page). */
#ifndef NC_TXT_H
#define NC_TXT_H

#include <stddef.h>
#include <stdint.h>

#include "book.h"

typedef struct {
    int pages;            /* pages in the resulting book */
    int truncated;        /* 1 if text beyond 100 pages was dropped */
    int dropped_chars;    /* code points dropped because of the page limit */
    int filtered_chars;   /* control characters etc. removed on import */
} ImportInfo;

/* Replace the book's pages with the given text. Title/signature are untouched. */
void txt_import(Book *b, const char *utf8, size_t n, ImportInfo *info);

/* Serialise to UTF-8 (malloc'd, NUL-terminated). Trailing empty pages are ignored. */
char *txt_export(const Book *b, size_t *out_len);

/* ---- building blocks shared with the ePub importer ---- */

/* CRLF/CR -> LF, tab -> 4 spaces, drops control chars, BOM and the section
 * sign. `out` must hold n * 4 code points. Returns the new length. */
int  txt_sanitize(const uint32_t *in, int n, uint32_t *out, int *filtered);

/* Reset the book to a single empty page before appending imported pages. */
void book_begin_import(Book *b);

/* Append `s` as one page if it is valid, otherwise flow it over as many pages
 * as needed. *first = 1 means "fill the (empty) first page". Returns 0 once the
 * 100 page limit was hit (info->truncated is set). */
int  book_append_page_text(Book *b, const uint32_t *s, int n, int *first, ImportInfo *info);

/* Flow an arbitrary text into fresh pages (replaces the book's pages). */
void book_flow_text(Book *b, const uint32_t *cps, int n, ImportInfo *info);

#endif
