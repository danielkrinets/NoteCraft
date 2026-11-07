#include "book.h"

#include <stdlib.h>
#include <string.h>

#include "layout.h"
#include "utf.h"

Book *book_new(void)
{
    Book *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->npages = 1;
    default_author(b->author, sizeof b->author);
    return b;
}

void book_free(Book *b) { free(b); }

void book_clear(Book *b)
{
    char author[64];
    memcpy(author, b->author, sizeof author);
    memset(b, 0, sizeof *b);
    memcpy(b->author, author, sizeof author);
    b->npages = 1;
}

bool book_add_page(Book *b)
{
    if (b->npages >= BOOK_MAX_PAGES) return false;
    b->pages[b->npages].len = 0;
    b->npages++;
    return true;
}

int book_trim_trailing_empty(Book *b)
{
    int removed = 0;
    while (b->npages > 1 && b->pages[b->npages - 1].len == 0) { b->npages--; removed++; }
    return removed;
}

int book_total_chars(const Book *b)
{
    int t = 0;
    for (int i = 0; i < b->npages; i++) t += b->pages[i].len;
    return t;
}

bool book_is_blank(const Book *b) { return b->npages == 1 && b->pages[0].len == 0; }

bool page_valid(const uint32_t *s, int n)
{
    if (n < 0 || n >= BOOK_PAGE_CAP) return false;
    if (cps_units(s, n) >= BOOK_PAGE_UNITS_LT) return false;
    Line lines[BOOK_MAX_LINES + 1];
    return layout_split(s, n, BOOK_TEXT_W, lines, BOOK_MAX_LINES + 1) <= BOOK_MAX_LINES;
}

bool title_valid(const uint32_t *s, int n)
{
    if (n < 0) return false;
    for (int i = 0; i < n; i++)
        if (s[i] < 0x20 || s[i] == 0x7F || s[i] == 0xA7) return false;
    return cps_units(s, n) < BOOK_TITLE_UNITS_LT;
}

void default_author(char *out, int cap)
{
    const char *u = getenv("NOTECRAFT_AUTHOR");
    if (!u || !*u) u = getenv("USER");
    if (!u || !*u) u = getenv("USERNAME");
    if (!u || !*u) u = "Player";
    strncpy(out, u, (size_t)cap - 1);
    out[cap - 1] = 0;
}
