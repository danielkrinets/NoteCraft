#include "txt.h"

#include <stdlib.h>
#include <string.h>

#include "layout.h"
#include "utf.h"

int txt_sanitize(const uint32_t *in, int n, uint32_t *out, int *filtered)
{
    int k = 0;
    for (int i = 0; i < n; i++) {
        uint32_t c = in[i];
        if (c == '\r') { if (i + 1 < n && in[i + 1] == '\n') continue; c = '\n'; }
        if (c == 0x2028 || c == 0x2029) c = '\n';
        if (c == 0xA0) c = ' ';
        if (c == '\t') { for (int t = 0; t < 4; t++) out[k++] = ' '; continue; }
        if (c == '\n' || c == '\f') { out[k++] = c; continue; }
        if (c < 0x20 || c == 0x7F || c == 0xA7 || c == 0xFEFF || (c >= 0x80 && c < 0xA0)
            || (c >= 0xD800 && c < 0xE000) || c > 0x10FFFF) { if (filtered) (*filtered)++; continue; }
        out[k++] = c;
    }
    return k;
}

/* Take the next page worth of text starting at pos. Returns the number of code
 * points consumed; *take receives the length of the page text (trailing
 * whitespace removed). */
static int next_chunk(const uint32_t *s, int n, int pos, int *take)
{
    /* Window limited by UTF-16 units so a page can never exceed 1023. */
    int units = 0, w = 0;
    while (pos + w < n && units + cp_units(s[pos + w]) < BOOK_PAGE_UNITS_LT) {
        units += cp_units(s[pos + w]);
        w++;
    }
    Line lines[BOOK_MAX_LINES + 2];
    int nl = layout_split(s + pos, w, BOOK_TEXT_W, lines, BOOK_MAX_LINES + 1);
    int use = nl > BOOK_MAX_LINES ? BOOK_MAX_LINES : nl;
    if (nl <= BOOK_MAX_LINES && pos + w < n && nl > 1) use = nl - 1;   /* last line may be cut */
    int end = use > 0 ? lines[use - 1].end : w;
    if (end <= 0) end = w > 0 ? w : 1;
    int t = end;
    while (t > 0 && (s[pos + t - 1] == '\n' || s[pos + t - 1] == ' ')) t--;
    *take = t;
    return end;
}

static int add_page(Book *b, const uint32_t *s, int n, int *first)
{
    if (*first) *first = 0;
    else if (!book_add_page(b)) return 0;
    Page *p = &b->pages[b->npages - 1];
    if (n > 0) memcpy(p->cp, s, (size_t)n * sizeof(uint32_t));
    p->len = n;
    return 1;
}

/* Flow `s` over pages appended to b. */
static int flow(Book *b, const uint32_t *s, int n, int *first, ImportInfo *info)
{
    int pos = 0, leading = 0;
    while (pos < n) {
        if (leading) while (pos < n && s[pos] == '\n') pos++;
        if (pos >= n) break;
        int take;
        int used = next_chunk(s, n, pos, &take);
        if (!add_page(b, s + pos, take, first)) {
            info->truncated = 1;
            info->dropped_chars += n - pos;
            return 0;
        }
        pos += used;
        leading = 1;
    }
    return 1;
}

void book_begin_import(Book *b)
{
    b->npages = 1;
    b->pages[0].len = 0;
}

int book_append_page_text(Book *b, const uint32_t *s, int n, int *first, ImportInfo *info)
{
    ImportInfo local = {0};
    if (!info) info = &local;
    if (info->truncated) { info->dropped_chars += n; return 0; }
    if (page_valid(s, n)) {
        if (add_page(b, s, n, first)) return 1;
        info->truncated = 1;
        info->dropped_chars += n;
        return 0;
    }
    return flow(b, s, n, first, info);
}

void book_flow_text(Book *b, const uint32_t *cps, int n, ImportInfo *info)
{
    ImportInfo local = {0};
    if (!info) info = &local;
    int first = 1;
    book_begin_import(b);
    uint32_t *tmp = malloc(((size_t)n * 4 + 1) * sizeof(uint32_t));
    if (!tmp) return;
    int k = txt_sanitize(cps, n, tmp, &info->filtered_chars);
    for (int i = 0; i < k; i++) if (tmp[i] == '\f') tmp[i] = '\n';
    flow(b, tmp, k, &first, info);
    free(tmp);
    info->pages = b->npages;
}

void txt_import(Book *b, const char *utf8, size_t n, ImportInfo *info)
{
    ImportInfo local = {0};
    if (!info) info = &local;
    memset(info, 0, sizeof *info);

    uint32_t *raw = malloc((n + 1) * sizeof(uint32_t));
    uint32_t *txt = malloc((n * 4 + 1) * sizeof(uint32_t));
    if (!raw || !txt) { free(raw); free(txt); return; }
    int rn = (int)u8_to_cps(utf8, n, raw, n);
    int tn = txt_sanitize(raw, rn, txt, &info->filtered_chars);
    free(raw);

    book_begin_import(b);

    int has_ff = 0;
    for (int i = 0; i < tn; i++) if (txt[i] == '\f') { has_ff = 1; break; }

    int first = 1;
    if (!has_ff) {
        flow(b, txt, tn, &first, info);
    } else {
        int start = 0;
        for (int i = 0; i <= tn; i++) {
            if (i < tn && txt[i] != '\f') continue;
            int end = i;
            if (end > start && txt[end - 1] == '\n') end--;       /* the "\n" we add on export */
            if (i == tn && end == start && start > 0) break;       /* trailing form feed */
            book_append_page_text(b, txt + start, end - start, &first, info);
            start = i + 1;
        }
    }
    free(txt);
    info->pages = b->npages;
}

char *txt_export(const Book *b, size_t *out_len)
{
    int np = b->npages;
    while (np > 1 && b->pages[np - 1].len == 0) np--;
    size_t cap = 16;
    for (int i = 0; i < np; i++) cap += (size_t)b->pages[i].len * 4 + 2;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t w = 0;
    for (int i = 0; i < np; i++) {
        const Page *p = &b->pages[i];
        for (int j = 0; j < p->len; j++) w += (size_t)u8_encode(p->cp[j], out + w);
        out[w++] = '\n';
        if (i + 1 < np) out[w++] = '\f';
    }
    out[w] = 0;
    if (out_len) *out_len = w;
    return out;
}
