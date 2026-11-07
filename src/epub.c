#include "epub.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "layout.h"
#include "utf.h"
#include "zipio.h"

/* ---------------------------------------------------------------- strings */

typedef struct { char *s; size_t n, cap; int oom; } Sb;

static void sb_put(Sb *b, const char *p, size_t n)
{
    if (b->oom) return;
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        while (nc < b->n + n + 1) nc *= 2;
        char *t = realloc(b->s, nc);
        if (!t) { b->oom = 1; return; }
        b->s = t; b->cap = nc;
    }
    memcpy(b->s + b->n, p, n);
    b->n += n;
    b->s[b->n] = 0;
}
static void sb_puts(Sb *b, const char *s) { sb_put(b, s, strlen(s)); }
static void sb_putc(Sb *b, char c) { sb_put(b, &c, 1); }
static void sb_cp(Sb *b, uint32_t cp) { char t[4]; sb_put(b, t, (size_t)u8_encode(cp, t)); }
static void sb_printf(Sb *b, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n > 0) sb_put(b, tmp, (size_t)(n < (int)sizeof tmp ? n : (int)sizeof tmp - 1));
}
static void sb_esc(Sb *b, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
        case '&': sb_puts(b, "&amp;"); break;
        case '<': sb_puts(b, "&lt;"); break;
        case '>': sb_puts(b, "&gt;"); break;
        case '"': sb_puts(b, "&quot;"); break;
        default: sb_putc(b, s[i]);
        }
    }
}
static void sb_esc_s(Sb *b, const char *s) { sb_esc(b, s, strlen(s)); }

static void seterr(char *err, size_t n, const char *msg)
{
    if (err && n) { strncpy(err, msg, n - 1); err[n - 1] = 0; }
}

/* ----------------------------------------------------------------- export */

static void make_uuid(char out[48])
{
    uint8_t r[16];
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got = f ? fread(r, 1, 16, f) : 0;
    if (f) fclose(f);
    if (got != 16) {
        srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)out);
        for (int i = 0; i < 16; i++) r[i] = (uint8_t)(rand() >> 3);
    }
    r[6] = (uint8_t)((r[6] & 0x0F) | 0x40);
    r[8] = (uint8_t)((r[8] & 0x3F) | 0x80);
    snprintf(out, 48, "urn:uuid:%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14], r[15]);
}

static const char *detect_lang(const Book *b, int np)
{
    long cyr = 0, lat = 0;
    for (int i = 0; i < np; i++)
        for (int j = 0; j < b->pages[i].len; j++) {
            uint32_t c = b->pages[i].cp[j];
            if (c >= 0x400 && c <= 0x4FF) cyr++;
            else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) lat++;
        }
    return cyr > lat ? "ru" : "en";
}

static void page_xhtml(Sb *o, const Page *p, int idx, const char *lang)
{
    sb_puts(o, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE html>\n"
               "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\" ");
    sb_printf(o, "xml:lang=\"%s\" lang=\"%s\">\n<head>\n<meta charset=\"utf-8\"/>\n", lang, lang);
    sb_printf(o, "<title>Page %d</title>\n<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/>\n</head>\n", idx + 1);
    sb_printf(o, "<body>\n<section class=\"page\" epub:type=\"chapter\" id=\"p%d\">\n", idx + 1);

    char *u8 = malloc((size_t)p->len * 4 + 1);
    if (u8) {
        size_t w = 0;
        for (int i = 0; i < p->len; i++) w += (size_t)u8_encode(p->cp[i], u8 + w);
        u8[w] = 0;
        size_t start = 0;
        for (size_t i = 0; i <= w; i++) {
            if (i < w && u8[i] != '\n') continue;
            if (i == start) sb_puts(o, "<p class=\"l\"><br/></p>\n");
            else { sb_puts(o, "<p class=\"l\">"); sb_esc(o, u8 + start, i - start); sb_puts(o, "</p>\n"); }
            start = i + 1;
        }
        free(u8);
    }
    sb_puts(o, "</section>\n</body>\n</html>\n");
}

int epub_export_mem(const Book *b, const char *fallback_title, uint8_t **out, size_t *outn)
{
    int np = b->npages;
    while (np > 1 && b->pages[np - 1].len == 0) np--;
    const char *title = b->title[0] ? b->title : (fallback_title && *fallback_title ? fallback_title : "Untitled Book");
    const char *author = b->author[0] ? b->author : "Player";
    const char *lang = detect_lang(b, np);
    char uuid[48];
    make_uuid(uuid);
    char stamp[32];
    time_t t = time(NULL);
    struct tm *g = gmtime(&t);
    if (!g || !strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", g)) strcpy(stamp, "2024-01-01T00:00:00Z");

    ZipW z; zipw_init(&z);
    Sb s = {0};
    zipw_add(&z, "mimetype", "application/epub+zip", 20);

    sb_puts(&s, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
                "  <rootfiles>\n    <rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>\n"
                "  </rootfiles>\n</container>\n");
    zipw_add(&z, "META-INF/container.xml", s.s, s.n);

    /* package document */
    s.n = 0;
    sb_puts(&s, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"bookid\" ");
    sb_printf(&s, "xml:lang=\"%s\">\n<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n", lang);
    sb_printf(&s, "  <dc:identifier id=\"bookid\">%s</dc:identifier>\n  <dc:title>", uuid);
    sb_esc_s(&s, title);
    sb_printf(&s, "</dc:title>\n  <dc:language>%s</dc:language>\n  <dc:creator>", lang);
    sb_esc_s(&s, author);
    sb_printf(&s, "</dc:creator>\n  <meta property=\"dcterms:modified\">%s</meta>\n"
                  "  <meta name=\"generator\" content=\"NoteCraft %s\"/>\n</metadata>\n<manifest>\n", stamp, NOTECRAFT_VERSION);
    sb_puts(&s, "  <item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n"
                "  <item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>\n"
                "  <item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>\n");
    for (int i = 0; i < np; i++)
        sb_printf(&s, "  <item id=\"p%d\" href=\"page%03d.xhtml\" media-type=\"application/xhtml+xml\"/>\n", i + 1, i + 1);
    sb_puts(&s, "</manifest>\n<spine toc=\"ncx\">\n");
    for (int i = 0; i < np; i++) sb_printf(&s, "  <itemref idref=\"p%d\"/>\n", i + 1);
    sb_puts(&s, "</spine>\n</package>\n");
    zipw_add(&z, "OEBPS/content.opf", s.s, s.n);

    /* EPUB 3 navigation document */
    s.n = 0;
    sb_puts(&s, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE html>\n"
                "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\" ");
    sb_printf(&s, "xml:lang=\"%s\" lang=\"%s\">\n<head>\n<meta charset=\"utf-8\"/>\n<title>", lang, lang);
    sb_esc_s(&s, title);
    sb_puts(&s, "</title>\n</head>\n<body>\n<nav epub:type=\"toc\" id=\"toc\">\n<h1>");
    sb_esc_s(&s, title);
    sb_puts(&s, "</h1>\n<ol>\n");
    for (int i = 0; i < np; i++)
        sb_printf(&s, "<li><a href=\"page%03d.xhtml\">Page %d</a></li>\n", i + 1, i + 1);
    sb_puts(&s, "</ol>\n</nav>\n</body>\n</html>\n");
    zipw_add(&z, "OEBPS/nav.xhtml", s.s, s.n);

    /* EPUB 2 NCX for older readers */
    s.n = 0;
    sb_puts(&s, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n<head>\n");
    sb_printf(&s, "  <meta name=\"dtb:uid\" content=\"%s\"/>\n  <meta name=\"dtb:depth\" content=\"1\"/>\n"
                  "  <meta name=\"dtb:totalPageCount\" content=\"0\"/>\n  <meta name=\"dtb:maxPageNumber\" content=\"0\"/>\n"
                  "</head>\n<docTitle><text>", uuid);
    sb_esc_s(&s, title);
    sb_puts(&s, "</text></docTitle>\n<navMap>\n");
    for (int i = 0; i < np; i++)
        sb_printf(&s, "  <navPoint id=\"n%d\" playOrder=\"%d\"><navLabel><text>Page %d</text></navLabel>"
                      "<content src=\"page%03d.xhtml\"/></navPoint>\n", i + 1, i + 1, i + 1, i + 1);
    sb_puts(&s, "</navMap>\n</ncx>\n");
    zipw_add(&z, "OEBPS/toc.ncx", s.s, s.n);

    sb_puts(&s, "");
    s.n = 0;
    sb_puts(&s, "body { margin: 5%; font-family: serif; line-height: 1.45; }\n"
                "p.l { margin: 0; white-space: pre-wrap; }\n"
                "h1 { font-size: 1.4em; }\n");
    zipw_add(&z, "OEBPS/style.css", s.s, s.n);

    for (int i = 0; i < np; i++) {
        char name[64];
        s.n = 0;
        page_xhtml(&s, &b->pages[i], i, lang);
        snprintf(name, sizeof name, "OEBPS/page%03d.xhtml", i + 1);
        zipw_add(&z, name, s.s, s.n);
    }
    int oom = s.oom;
    free(s.s);
    if (oom || zipw_finish(&z, out, outn) != 0) { zipw_free(&z); return -1; }
    zipw_free(&z);
    return 0;
}

int epub_export(const Book *b, const char *fallback_title, const char *path, char *err, size_t errn)
{
    uint8_t *data; size_t n;
    if (epub_export_mem(b, fallback_title, &data, &n) != 0) { seterr(err, errn, "Out of memory"); return -1; }
    FILE *f = fopen(path, "wb");
    if (!f) { free(data); seterr(err, errn, "Cannot write file"); return -1; }
    int rc = fwrite(data, 1, n, f) == n ? 0 : -1;
    if (fclose(f)) rc = -1;
    free(data);
    if (rc) seterr(err, errn, "Write error");
    return rc;
}

/* ----------------------------------------------------------------- import */

typedef struct { const char *name; uint32_t cp; } Ent;
static const Ent ENTS[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", ' '},
    {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026}, {"laquo", 0xAB}, {"raquo", 0xBB},
    {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bdquo", 0x201E},
    {"bull", 0x2022}, {"middot", 0xB7}, {"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122},
    {"deg", 0xB0}, {"euro", 0x20AC}, {"pound", 0xA3}, {"yen", 0xA5}, {"times", 0xD7},
    {"shy", 0}, {"zwnj", 0}, {"zwj", 0},
};

/* Decode "&...;" at p. Returns length consumed (0 if not an entity). */
static int decode_entity(const char *p, uint32_t *cp)
{
    if (*p != '&') return 0;
    const char *e = p + 1;
    while (*e && *e != ';' && *e != '&' && *e != '<' && (e - p) < 12) e++;
    if (*e != ';') return 0;
    if (p[1] == '#') {
        unsigned long v = 0;
        if (p[2] == 'x' || p[2] == 'X') v = strtoul(p + 3, NULL, 16);
        else v = strtoul(p + 2, NULL, 10);
        if (v == 0 || v > 0x10FFFF) v = 0xFFFD;
        *cp = (uint32_t)v;
        return (int)(e - p) + 1;
    }
    size_t nl = (size_t)(e - p - 1);
    for (size_t i = 0; i < sizeof ENTS / sizeof ENTS[0]; i++)
        if (strlen(ENTS[i].name) == nl && strncmp(ENTS[i].name, p + 1, nl) == 0) { *cp = ENTS[i].cp; return (int)(e - p) + 1; }
    return 0;
}

/* End of the tag starting at p ('<'), honouring quoted attributes. */
static const char *tag_close(const char *p)
{
    char q = 0;
    for (p++; *p; p++) {
        if (q) { if (*p == q) q = 0; }
        else if (*p == '"' || *p == '\'') q = *p;
        else if (*p == '>') return p;
    }
    return NULL;
}

static int tag_name(const char *p, char *out, size_t cap, int *closing)
{
    const char *q = p + 1;
    *closing = 0;
    if (*q == '/') { *closing = 1; q++; }
    size_t i = 0;
    while (*q && !isspace((unsigned char)*q) && *q != '>' && *q != '/' && i + 1 < cap) out[i++] = (char)tolower((unsigned char)*q++);
    out[i] = 0;
    return (int)i;
}

/* Fetch attribute `name` (case-insensitive) of the tag [p, end). */
static int attr_get(const char *p, const char *end, const char *name, char *out, size_t cap)
{
    size_t nl = strlen(name);
    for (const char *q = p; q + nl < end; q++) {
        if (q != p && !isspace((unsigned char)q[-1])) continue;
        if (strncasecmp(q, name, nl) != 0) continue;
        const char *v = q + nl;
        while (v < end && isspace((unsigned char)*v)) v++;
        if (v >= end || *v != '=') continue;
        v++;
        while (v < end && isspace((unsigned char)*v)) v++;
        char quote = (*v == '"' || *v == '\'') ? *v++ : 0;
        size_t i = 0;
        while (v < end && i + 1 < cap && (quote ? *v != quote : !isspace((unsigned char)*v))) out[i++] = *v++;
        out[i] = 0;
        return 1;
    }
    return 0;
}

static void text_decode(Sb *o, const char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        int l;
        if (s[i] == '&' && (l = decode_entity(s + i, &cp)) > 0) { if (cp) sb_cp(o, cp); i += (size_t)l; }
        else sb_putc(o, s[i++]);
    }
}

/* Text of the first <name ...>...</name> element, entities decoded, trimmed. */
static int find_text(const char *xml, const char *name, char *out, size_t cap)
{
    size_t nl = strlen(name);
    for (const char *p = xml; (p = strchr(p, '<')); p++) {
        if (strncasecmp(p + 1, name, nl) != 0) continue;
        char c = p[1 + nl];
        if (!(c == '>' || isspace((unsigned char)c))) continue;
        const char *e = tag_close(p);
        if (!e || e[-1] == '/') continue;
        const char *close = strstr(e, "</");
        if (!close) return 0;
        Sb t = {0};
        text_decode(&t, e + 1, (size_t)(close - e - 1));
        if (!t.s) { out[0] = 0; return 1; }
        size_t a = 0, b = t.n;
        while (a < b && isspace((unsigned char)t.s[a])) a++;
        while (b > a && isspace((unsigned char)t.s[b - 1])) b--;
        size_t k = b - a < cap - 1 ? b - a : cap - 1;
        while (k > 0 && ((unsigned char)t.s[a + k] & 0xC0) == 0x80 && a + k < b) k--;
        if (t.s) memcpy(out, t.s + a, k);
        out[k] = 0;
        free(t.s);
        return 1;
    }
    return 0;
}

static void percent_decode(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            char h[3] = {r[1], r[2], 0};
            *w++ = (char)strtol(h, NULL, 16);
            r += 2;
        } else *w++ = *r;
    }
    *w = 0;
}

/* dir + href with ./ and ../ resolved, fragment dropped. */
static void resolve_path(const char *dir, const char *href, char *out, size_t cap)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s%s", dir, href);
    char *hash = strchr(tmp, '#');
    if (hash) *hash = 0;
    percent_decode(tmp);
    char *parts[64]; int np = 0;
    for (char *tok = tmp, *next; tok; tok = next) {
        next = strchr(tok, '/');
        if (next) *next++ = 0;
        if (!*tok || strcmp(tok, ".") == 0) continue;
        if (strcmp(tok, "..") == 0) { if (np) np--; continue; }
        if (np < 64) parts[np++] = tok;
    }
    size_t w = 0;
    out[0] = 0;
    for (int i = 0; i < np; i++) w += (size_t)snprintf(out + w, cap - w < 1 ? 1 : cap - w, "%s%s", i ? "/" : "", parts[i]);
}

static int is_block(const char *n)
{
    static const char *B[] = {"p", "div", "li", "tr", "blockquote", "section", "article", "aside", "header", "footer",
                              "nav", "ul", "ol", "table", "pre", "dl", "dt", "dd", "figure", "figcaption", "hr", "body"};
    for (size_t i = 0; i < sizeof B / sizeof B[0]; i++) if (strcmp(B[i], n) == 0) return 1;
    return 0;
}

static void ensure_nl(Sb *o, int count)
{
    if (o->n == 0) return;
    int have = 0;
    for (size_t i = o->n; i > 0 && o->s[i - 1] == '\n' && have < count; i--) have++;
    while (have++ < count) sb_putc(o, '\n');
}

/* Generic XHTML -> plain text with paragraph breaks. */
static void html_to_text(const char *h, Sb *o)
{
    const char *p = h, *body = NULL;
    for (const char *q = h; (q = strchr(q, '<')); q++) {
        char nm[32]; int cl;
        tag_name(q, nm, sizeof nm, &cl);
        if (!cl && strcmp(nm, "body") == 0) { const char *e = tag_close(q); if (e) body = e + 1; break; }
    }
    if (body) p = body;
    int pending_space = 0;
    while (*p) {
        if (*p == '<') {
            if (strncmp(p, "<!--", 4) == 0) { const char *e = strstr(p + 4, "-->"); p = e ? e + 3 : p + strlen(p); continue; }
            if (strncmp(p, "<![CDATA[", 9) == 0) {
                const char *e = strstr(p + 9, "]]>");
                size_t n = e ? (size_t)(e - p - 9) : strlen(p + 9);
                sb_put(o, p + 9, n);
                p = e ? e + 3 : p + 9 + n;
                continue;
            }
            const char *e = tag_close(p);
            if (!e) break;
            char nm[32]; int cl;
            tag_name(p, nm, sizeof nm, &cl);
            if (!cl && (strcmp(nm, "script") == 0 || strcmp(nm, "style") == 0)) {
                char endtag[40]; snprintf(endtag, sizeof endtag, "</%s", nm);
                const char *x = strstr(e, endtag);
                const char *xe = x ? tag_close(x) : NULL;
                p = xe ? xe + 1 : e + strlen(e);
                continue;
            }
            if (strcmp(nm, "br") == 0) { sb_putc(o, '\n'); pending_space = 0; }
            else if (nm[0] == 'h' && nm[1] >= '1' && nm[1] <= '6' && !nm[2]) { ensure_nl(o, 2); pending_space = 0; }
            else if (is_block(nm)) { ensure_nl(o, 1); pending_space = 0; }
            else if (strcmp(nm, "td") == 0 || strcmp(nm, "th") == 0) pending_space = 1;
            if (strcmp(nm, "body") == 0 && cl) break;
            p = e + 1;
            continue;
        }
        if (isspace((unsigned char)*p)) { pending_space = 1; p++; continue; }
        uint32_t cp = 0;
        int l = decode_entity(p, &cp);
        if (l > 0 && cp == 0) { p += l; continue; }          /* soft hyphen etc. */
        int line_start = o->n == 0 || o->s[o->n - 1] == '\n';
        if (pending_space && !line_start) sb_putc(o, ' ');
        pending_space = 0;
        if (l > 0) { if (cp == ' ') pending_space = 1; else sb_cp(o, cp); p += l; }
        else {
            int cl = 1;
            while (cl < 4 && ((unsigned char)p[cl] & 0xC0) == 0x80) cl++;
            sb_put(o, p, (size_t)cl);
            p += cl;
        }
    }
}

/* NoteCraft pages: every <p> is one hard line of the page. */
static void nc_page_text(const char *h, Sb *o)
{
    int inside = 0, count = 0;
    for (const char *p = h; *p;) {
        if (*p == '<') {
            if (strncmp(p, "<!--", 4) == 0) { const char *e = strstr(p + 4, "-->"); p = e ? e + 3 : p + strlen(p); continue; }
            const char *e = tag_close(p);
            if (!e) break;
            char nm[32]; int cl;
            tag_name(p, nm, sizeof nm, &cl);
            if (strcmp(nm, "p") == 0) {
                if (!cl) { if (count++) sb_putc(o, '\n'); inside = !(e[-1] == '/'); }
                else inside = 0;
            }
            p = e + 1;
            continue;
        }
        const char *e = strchr(p, '<');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (inside) {
            Sb t = {0};
            text_decode(&t, p, n);
            for (size_t i = 0; i < t.n; i++) sb_putc(o, t.s[i] == '\n' || t.s[i] == '\r' ? ' ' : t.s[i]);
            free(t.s);
        }
        p += n;
    }
}

typedef struct { char id[96], href[400]; } Item;

int epub_import_mem(Book *b, const uint8_t *data, size_t n, ImportInfo *info, char *err, size_t errn)
{
    ImportInfo local = {0};
    if (!info) info = &local;
    memset(info, 0, sizeof *info);
    ZipR z;
    if (zipr_open(&z, data, n) != 0) { seterr(err, errn, "Not a valid ePub (cannot read ZIP)"); return -1; }

    int rc = -1;
    char *container = NULL, *opf = NULL;
    Item *items = NULL; char (*spine)[96] = NULL; int nitems = 0, nspine = 0;
    char opf_path[512] = "", opf_dir[512] = "";

    int ci = zipr_find(&z, "META-INF/container.xml");
    if (ci >= 0) container = (char *)zipr_read(&z, ci, NULL);
    if (container) {
        for (const char *p = container; (p = strchr(p, '<')); p++) {
            char nm[32]; int cl;
            tag_name(p, nm, sizeof nm, &cl);
            if (cl || strcmp(nm, "rootfile") != 0) continue;
            const char *e = tag_close(p);
            if (e && attr_get(p, e, "full-path", opf_path, sizeof opf_path)) break;
        }
    }
    if (!opf_path[0]) {
        for (int i = 0; i < z.count; i++) {
            size_t l = strlen(z.ents[i].name);
            if (l > 4 && strcasecmp(z.ents[i].name + l - 4, ".opf") == 0) { snprintf(opf_path, sizeof opf_path, "%s", z.ents[i].name); break; }
        }
    }
    int oi = opf_path[0] ? zipr_find(&z, opf_path) : -1;
    if (oi < 0) { seterr(err, errn, "ePub has no package (.opf) file"); goto done; }
    opf = (char *)zipr_read(&z, oi, NULL);
    if (!opf) { seterr(err, errn, "Cannot read the ePub package file"); goto done; }
    {
        char *sl = strrchr(opf_path, '/');
        if (sl) { size_t l = (size_t)(sl - opf_path) + 1; memcpy(opf_dir, opf_path, l); opf_dir[l] = 0; }
    }

    items = calloc(2048, sizeof *items);
    spine = calloc(4096, sizeof *spine);
    if (!items || !spine) { seterr(err, errn, "Out of memory"); goto done; }
    for (const char *p = opf; (p = strchr(p, '<')); p++) {
        if (strncmp(p, "<!--", 4) == 0) { const char *e = strstr(p + 4, "-->"); if (!e) break; p = e + 2; continue; }
        char nm[32]; int cl;
        tag_name(p, nm, sizeof nm, &cl);
        if (cl) continue;
        const char *e = tag_close(p);
        if (!e) break;
        if (strcmp(nm, "item") == 0 && nitems < 2048) {
            Item *it = &items[nitems];
            if (attr_get(p, e, "id", it->id, sizeof it->id) && attr_get(p, e, "href", it->href, sizeof it->href)) nitems++;
        } else if (strcmp(nm, "itemref") == 0 && nspine < 4096) {
            if (attr_get(p, e, "idref", spine[nspine], sizeof spine[0])) nspine++;
        }
    }
    if (!nspine) { seterr(err, errn, "ePub has an empty reading order"); goto done; }

    int is_nc = strstr(opf, "name=\"generator\" content=\"NoteCraft") != NULL;
    char title[256] = "", author[256] = "";
    if (!find_text(opf, "dc:title", title, sizeof title)) find_text(opf, "title", title, sizeof title);
    if (!find_text(opf, "dc:creator", author, sizeof author)) find_text(opf, "creator", author, sizeof author);

    Sb all = {0};
    int first = 1;
    book_begin_import(b);
    int have_text = 0;
    for (int si = 0; si < nspine; si++) {
        const Item *it = NULL;
        for (int k = 0; k < nitems; k++) if (strcmp(items[k].id, spine[si]) == 0) { it = &items[k]; break; }
        if (!it) continue;
        char path[1024];
        resolve_path(opf_dir, it->href, path, sizeof path);
        int xi = zipr_find(&z, path);
        if (xi < 0) continue;
        char *html = (char *)zipr_read(&z, xi, NULL);
        if (!html) continue;
        Sb page = {0};
        if (is_nc) nc_page_text(html, &page); else html_to_text(html, &page);
        free(html);
        if (is_nc) {
            have_text = 1;
            uint32_t *raw = malloc((page.n + 1) * sizeof(uint32_t));
            uint32_t *cl = malloc((page.n * 4 + 1) * sizeof(uint32_t));
            if (raw && cl) {
                int rn = (int)u8_to_cps(page.s ? page.s : "", page.n, raw, page.n);
                int cn = txt_sanitize(raw, rn, cl, &info->filtered_chars);
                book_append_page_text(b, cl, cn, &first, info);
            }
            free(raw); free(cl);
        } else if (page.n) {
            have_text = 1;
            ensure_nl(&all, 2);
            sb_put(&all, page.s, page.n);
        }
        free(page.s);
        if (all.n > 16u * 1024 * 1024) break;
    }
    if (!is_nc && all.n) {
        /* trim */
        size_t a = 0, e = all.n;
        while (a < e && (all.s[a] == '\n' || all.s[a] == ' ')) a++;
        while (e > a && (all.s[e - 1] == '\n' || all.s[e - 1] == ' ')) e--;
        uint32_t *cps = malloc((e - a + 1) * sizeof(uint32_t));
        if (cps) {
            int cn = (int)u8_to_cps(all.s + a, e - a, cps, e - a);
            book_flow_text(b, cps, cn, info);
            free(cps);
        }
    }
    free(all.s);
    if (!have_text) { seterr(err, errn, "No readable text found in this ePub"); goto done; }

    if (title[0]) { strncpy(b->title, title, sizeof b->title - 1); b->title[sizeof b->title - 1] = 0; }
    if (author[0]) { strncpy(b->author, author, sizeof b->author - 1); b->author[sizeof b->author - 1] = 0; }
    b->is_signed = false;
    info->pages = b->npages;
    rc = 0;
done:
    free(container); free(opf); free(items); free(spine);
    zipr_close(&z);
    return rc;
}
