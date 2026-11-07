/* The document model: a Minecraft "Book and Quill" (Java Edition limits). */
#ifndef NC_BOOK_H
#define NC_BOOK_H

#include <stdbool.h>
#include <stdint.h>

#define BOOK_MAX_PAGES       100
#define BOOK_PAGE_UNITS_LT   1024   /* page length must be < 1024 UTF-16 units -> 1023 chars */
#define BOOK_TITLE_UNITS_LT  16     /* title must be < 16 units -> 15 chars */
#define BOOK_TEXT_W          114    /* text width in px */
#define BOOK_MAX_LINES       14     /* 14 * 9px = 126 <= 128 */
#define BOOK_PAGE_CAP        1024   /* storage capacity per page */

typedef struct {
    uint32_t cp[BOOK_PAGE_CAP];
    int len;
} Page;

typedef struct {
    Page pages[BOOK_MAX_PAGES];
    int npages;                 /* always >= 1 */
    char title[64];             /* UTF-8, set when signed */
    char author[64];            /* UTF-8 */
    bool is_signed;
} Book;

Book *book_new(void);
void  book_free(Book *b);
void  book_clear(Book *b);                 /* one empty page, no title */
bool  book_add_page(Book *b);              /* false at the 100 page limit */
int   book_trim_trailing_empty(Book *b);   /* drops empty last pages (keeps >= 1); returns removed */
int   book_total_chars(const Book *b);
bool  book_is_blank(const Book *b);        /* single empty page */

/* Vanilla validator for a page: length < 1024 units and <= 14 wrapped lines. */
bool  page_valid(const uint32_t *s, int n);
/* Title validator: length < 16 units. */
bool  title_valid(const uint32_t *s, int n);

/* Rendering helper: bytes of a page as UTF-8 (NUL-terminated, truncated). */
void  default_author(char *out, int cap);

#endif
