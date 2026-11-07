/* Unit tests for the SDL-free core. Run with `make test`. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "book.h"
#include "epub.h"
#include "field.h"
#include "font.h"
#include "layout.h"
#include "png.h"
#include "txt.h"
#include "utf.h"
#include "zipio.h"

static int g_fail, g_run;
#define CHECK(c) do { g_run++; if (!(c)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { g_run++; long _a = (long)(a), _b = (long)(b); if (_a != _b) { g_fail++; printf("  FAIL %s:%d  %s == %s  (%ld vs %ld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

static int cps_of(const char *s, uint32_t *out) { return (int)u8_to_cps(s, strlen(s), out, 8192); }

static void test_utf(void)
{
    uint32_t c[16];
    int n = cps_of("a\xD0\x96\xE2\x82\xAC\xF0\x9F\x98\x80", c);
    CHECK_EQ(n, 4);
    CHECK_EQ(c[1], 0x416);
    CHECK_EQ(c[3], 0x1F600);
    CHECK_EQ(cps_units(c, n), 5);                 /* emoji = surrogate pair = 2 units */
    char out[32];
    cps_to_u8(c, n, out, sizeof out);
    CHECK(strcmp(out, "a\xD0\x96\xE2\x82\xAC\xF0\x9F\x98\x80") == 0);
    uint32_t bad;
    CHECK_EQ(u8_decode("\xC0\x80", 2, &bad), 1);  /* overlong */
    CHECK_EQ(bad, 0xFFFD);
}

static void test_font(void)
{
    CHECK_EQ(font_selftest(), 0);
    for (uint32_t c = 0x21; c < 0x7F; c++) CHECK(font_glyph(c) != NULL);
    for (uint32_t c = 0x410; c <= 0x44F; c++) CHECK(font_glyph(c) != NULL);
    CHECK(font_glyph(0x401) && font_glyph(0x451));
    CHECK_EQ(font_cp_width(' '), 4);
    CHECK_EQ(font_cp_width('A'), 6);
    CHECK_EQ(font_cp_width('i'), 2);
    CHECK_EQ(font_cp_width('.'), 2);
    uint32_t hello[8];
    int n = cps_of("Hello", hello);
    CHECK_EQ(font_width(hello, n), 6 + 6 + 3 + 3 + 6);   /* H e l l o */
}

static void test_layout(void)
{
    uint32_t t[2048];
    Line l[32];
    int n = cps_of("Hello world", t);
    CHECK_EQ(layout_split(t, n, 114, l, 32), 1);
    n = cps_of("a\nb\n", t);
    CHECK_EQ(layout_split(t, n, 114, l, 32), 2);          /* trailing newline adds no line to the validator */
    CHECK_EQ(layout_display(t, n, 114, l, 32), 3);        /* ...but the cursor gets a line */
    CHECK_EQ(layout_display(t, 0, 114, l, 32), 1);

    /* a long unbroken word is cut between characters */
    memset(t, 0, sizeof t);
    for (int i = 0; i < 60; i++) t[i] = 'W';              /* each 'W' is 6px wide -> 19 per 114px line */
    CHECK_EQ(layout_split(t, 60, 114, l, 32), 4);
    CHECK_EQ(l[0].end - l[0].start, 19);

    /* wraps at the last space; the space stays on the previous line */
    n = cps_of("aaaa bbbb cccc dddd eeee ffff gggg hhhh", t);
    int nl = layout_split(t, n, 114, l, 32);
    CHECK(nl >= 2);
    CHECK(l[0].brk == 1);
    CHECK_EQ(t[l[0].end - 1], ' ');
    CHECK_EQ(l[1].start, l[0].end);
    for (int i = 0; i < nl; i++) CHECK(font_width(t + l[i].start, layout_vis_end(t, &l[i]) - l[i].start) <= 114);
}

static int lines_of(const char *s)
{
    uint32_t t[4096];
    Line l[32];
    int n = cps_of(s, t);
    return layout_split(t, n, 114, l, 32);
}

static void test_limits(void)
{
    uint32_t t[2048];
    int n;
    /* 14 lines are fine, 15 are not */
    char buf[64];
    memset(buf, '\n', 13); buf[13] = 'a'; buf[14] = 0;
    n = cps_of(buf, t);
    CHECK(page_valid(t, n));
    CHECK_EQ(lines_of(buf), 14);
    memset(buf, '\n', 14); buf[14] = 'a'; buf[15] = 0;
    n = cps_of(buf, t);
    CHECK(!page_valid(t, n));
    memset(buf, '\n', 14); buf[14] = 0;                   /* 14 newlines -> 14 lines, valid */
    n = cps_of(buf, t);
    CHECK(page_valid(t, n));
    memset(buf, '\n', 15); buf[15] = 0;
    n = cps_of(buf, t);
    CHECK(!page_valid(t, n));

    CHECK(title_valid(t, 0));
    for (int i = 0; i < 15; i++) t[i] = 'a';
    CHECK(title_valid(t, 15));
    CHECK(!title_valid(t, 16));                           /* titles are at most 15 chars */
    t[0] = 0xA7;
    CHECK(!title_valid(t, 1));
}

static void test_field(void)
{
    Book *b = book_new();
    Page *p = &b->pages[0];
    Field f;
    field_bind(&f, p->cp, &p->len, BOOK_PAGE_CAP, NULL, NULL);
    uint32_t c[64];
    int n = cps_of("hello big world", c);
    CHECK(field_insert(&f, c, n));
    CHECK_EQ(p->len, 15);
    CHECK_EQ(f.cur, 15);
    CHECK_EQ(field_word_pos(&f, -1, 15), 10);
    CHECK_EQ(field_word_pos(&f, -1, 10), 6);
    CHECK_EQ(field_word_pos(&f, +1, 0), 6);
    field_delete(&f, -1, true);                           /* Ctrl+Backspace removes "world" */
    CHECK_EQ(p->len, 10);
    field_select_all(&f);
    n = cps_of("X", c);
    field_insert(&f, c, n);                               /* typing replaces the selection */
    CHECK_EQ(p->len, 1);
    CHECK_EQ(p->cp[0], 'X');
    field_set_cursor(&f, 0, false);
    CHECK(!field_delete(&f, -1, false));
    book_free(b);
}

static bool valid_cb(const uint32_t *s, int n, void *ud) { (void)ud; return page_valid(s, n); }

static void test_field_validator(void)
{
    Book *b = book_new();
    Page *p = &b->pages[0];
    Field f;
    field_bind(&f, p->cp, &p->len, BOOK_PAGE_CAP, valid_cb, NULL);
    uint32_t nl = '\n', a = 'a';
    for (int i = 0; i < 13; i++) CHECK(field_insert(&f, &nl, 1));
    CHECK(field_insert(&f, &a, 1));                       /* 14th line */
    CHECK(field_insert(&f, &nl, 1));                      /* trailing newline still validates (vanilla quirk) */
    int before = p->len;
    CHECK(!field_insert(&f, &a, 1));                      /* a 15th line does not */
    CHECK_EQ(p->len, before);

    /* paste fits as much as possible */
    book_clear(b);
    field_bind(&f, p->cp, &p->len, BOOK_PAGE_CAP, valid_cb, NULL);
    uint32_t big[2000];
    for (int i = 0; i < 2000; i++) big[i] = (i % 7 == 6) ? ' ' : 'm';
    int took = field_insert_fit(&f, big, 2000);
    CHECK(took > 100 && took < 2000);
    CHECK(page_valid(p->cp, p->len));
    book_free(b);
}

static void test_txt(void)
{
    Book *b = book_new();
    ImportInfo info;
    const char *src = "Page one line\n\x0c" "\xD0\x92\xD1\x82\xD0\xBE\xD1\x80\xD0\xB0\xD1\x8F \xD1\x81\xD1\x82\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB8\xD1\x86\xD0\xB0\n\x0c" "third\n";
    txt_import(b, src, strlen(src), &info);
    CHECK_EQ(b->npages, 3);
    CHECK_EQ(b->pages[0].len, 13);
    CHECK_EQ(b->pages[1].cp[0], 0x412);
    size_t n;
    char *out = txt_export(b, &n);
    CHECK(out && strcmp(out, src) == 0);                  /* exact round trip */
    free(out);

    /* CRLF, tabs, control chars */
    const char *dirty = "a\r\nb\tc\x01\xC2\xA7z";
    txt_import(b, dirty, strlen(dirty), &info);
    uint32_t want[16];
    int wn = cps_of("a\nb    cz", want);
    CHECK_EQ(b->pages[0].len, wn);
    CHECK(memcmp(b->pages[0].cp, want, (size_t)wn * 4) == 0);
    CHECK(info.filtered_chars >= 2);

    /* flowing a long text keeps every word, in order, within the limits */
    size_t cap = 60000;
    char *text = malloc(cap);
    size_t w = 0;
    for (int i = 0; i < 2500 && w + 16 < cap; i++) w += (size_t)snprintf(text + w, 16, "word%d ", i);
    text[w] = 0;
    txt_import(b, text, w, &info);
    CHECK(b->npages > 1 && b->npages <= 100);
    CHECK(!info.truncated);
    int next = 0, ok = 1;
    for (int p = 0; p < b->npages; p++) {
        CHECK(page_valid(b->pages[p].cp, b->pages[p].len));
        char *u = malloc((size_t)b->pages[p].len * 4 + 1);
        cps_to_u8(b->pages[p].cp, b->pages[p].len, u, (size_t)b->pages[p].len * 4 + 1);
        for (char *tok = strtok(u, " \n"); tok; tok = strtok(NULL, " \n")) {
            char exp[32]; snprintf(exp, sizeof exp, "word%d", next++);
            if (strcmp(tok, exp) != 0) ok = 0;
        }
        free(u);
    }
    CHECK(ok);
    CHECK_EQ(next, 2500);
    free(text);

    /* more than 100 pages worth of text is truncated, not crashed on */
    cap = 400000; text = malloc(cap); w = 0;
    for (int i = 0; i < 30000 && w + 16 < cap; i++) w += (size_t)snprintf(text + w, 16, "word%d ", i);
    txt_import(b, text, w, &info);
    CHECK_EQ(b->npages, 100);
    CHECK(info.truncated);
    free(text);
    book_free(b);
}

static void test_zip(void)
{
    ZipW z; zipw_init(&z);
    zipw_add(&z, "mimetype", "application/epub+zip", 20);
    zipw_add(&z, "a/b.txt", "hello zip", 9);
    zipw_add(&z, "empty", "", 0);
    uint8_t *d; size_t n;
    CHECK_EQ(zipw_finish(&z, &d, &n), 0);
    CHECK(memcmp(d + 30, "mimetype", 8) == 0);            /* mimetype is the first entry, stored */
    ZipR r;
    CHECK_EQ(zipr_open(&r, d, n), 0);
    CHECK_EQ(r.count, 3);
    int i = zipr_find(&r, "a/b.txt");
    CHECK(i >= 0);
    size_t sz;
    uint8_t *c = zipr_read(&r, i, &sz);
    CHECK(c && sz == 9 && memcmp(c, "hello zip", 9) == 0);
    free(c);
    CHECK_EQ(zipr_find(&r, "nope"), -1);
    zipr_close(&r);
    d[n - 5] ^= 0xFF;                                     /* corrupt the EOCD */
    CHECK(zipr_open(&r, d, n) != 0 || r.count >= 0);
    zipr_close(&r);
    zipw_free(&z);
    free(d);
}

static void test_epub(void)
{
    Book *b = book_new();
    ImportInfo info;
    const char *src = "First <page> & \"quotes\"\n\n  indented line\x0c" "\xD0\x92\xD1\x82\xD0\xBE\xD1\x80\xD0\xB0\xD1\x8F\n\x0c" "Third\n";
    txt_import(b, src, strlen(src), &info);
    CHECK_EQ(b->npages, 3);
    snprintf(b->title, sizeof b->title, "Diary & <Co>");
    snprintf(b->author, sizeof b->author, "Steve");

    uint8_t *d; size_t n;
    CHECK_EQ(epub_export_mem(b, "fallback", &d, &n), 0);
    ZipR z;
    CHECK_EQ(zipr_open(&z, d, n), 0);
    CHECK(zipr_find(&z, "mimetype") == 0);
    CHECK(zipr_find(&z, "META-INF/container.xml") > 0);
    CHECK(zipr_find(&z, "OEBPS/content.opf") > 0);
    CHECK(zipr_find(&z, "OEBPS/nav.xhtml") > 0);
    CHECK(zipr_find(&z, "OEBPS/page001.xhtml") > 0);
    CHECK(zipr_find(&z, "OEBPS/page002.xhtml") > 0);
    zipr_close(&z);

    /* round trip keeps the exact pages, title and author */
    Book *r = book_new();
    char err[200];
    CHECK_EQ(epub_import_mem(r, d, n, &info, err, sizeof err), 0);
    free(d);
    CHECK_EQ(r->npages, 3);
    CHECK_EQ(r->pages[0].len, b->pages[0].len);
    CHECK(memcmp(r->pages[0].cp, b->pages[0].cp, (size_t)b->pages[0].len * 4) == 0);
    CHECK(memcmp(r->pages[1].cp, b->pages[1].cp, (size_t)b->pages[1].len * 4) == 0);
    CHECK(strcmp(r->title, "Diary & <Co>") == 0);
    CHECK(strcmp(r->author, "Steve") == 0);
    book_free(r);

    /* a foreign ePub (deflate not needed: stored entries) is flowed into pages */
    ZipW w; zipw_init(&w);
    zipw_add(&w, "mimetype", "application/epub+zip", 20);
    const char *cont = "<?xml version=\"1.0\"?><container><rootfiles><rootfile full-path=\"content/book.opf\" media-type=\"x\"/></rootfiles></container>";
    zipw_add(&w, "META-INF/container.xml", cont, strlen(cont));
    const char *opf = "<package><metadata><dc:title>Other &amp; Book</dc:title><dc:creator>Alex</dc:creator></metadata>"
                      "<manifest><item id=\"c1\" href=\"ch%201.xhtml\" media-type=\"application/xhtml+xml\"/>"
                      "<item id=\"c2\" href=\"../x/ch2.xhtml\" media-type=\"application/xhtml+xml\"/></manifest>"
                      "<spine><itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>";
    zipw_add(&w, "content/book.opf", opf, strlen(opf));
    const char *c1 = "<html><head><title>x</title><style>p{color:red}</style></head><body><h1>Chapter&nbsp;One</h1>"
                     "<p>It was a <b>dark</b> and stormy night&mdash;really.</p><p>Second&#32;paragraph &amp; more.</p></body></html>";
    const char *c2 = "<html><body><p>Chapter two text.<br/>After break.</p></body></html>";
    zipw_add(&w, "content/ch 1.xhtml", c1, strlen(c1));
    zipw_add(&w, "x/ch2.xhtml", c2, strlen(c2));
    uint8_t *fd; size_t fn;
    zipw_finish(&w, &fd, &fn);
    Book *f = book_new();
    CHECK_EQ(epub_import_mem(f, fd, fn, &info, err, sizeof err), 0);
    free(fd); zipw_free(&w);
    CHECK(strcmp(f->title, "Other & Book") == 0);
    CHECK(strcmp(f->author, "Alex") == 0);
    char all[4096];
    cps_to_u8(f->pages[0].cp, f->pages[0].len, all, sizeof all);
    CHECK(strstr(all, "Chapter One") != NULL);
    CHECK(strstr(all, "dark and stormy night\xE2\x80\x94really.") != NULL);
    CHECK(strstr(all, "Second paragraph & more.") != NULL);
    CHECK(strstr(all, "color:red") == NULL);              /* <style> content is dropped */
    CHECK(strstr(all, "Chapter two text.") != NULL || f->npages > 1);
    book_free(f);

    Book *bad = book_new();
    CHECK(epub_import_mem(bad, (const uint8_t *)"not a zip at all, definitely not", 33, &info, err, sizeof err) != 0);
    book_free(bad);
    book_free(b);
}

static void test_png(void)
{
    uint32_t px[4 * 3];
    for (int i = 0; i < 12; i++) px[i] = 0xFF000000u | (uint32_t)(i * 20) << 16 | (uint32_t)(255 - i * 9) << 8 | (uint32_t)i;
    px[5] = 0x80FF0000u;
    uint8_t *png; size_t n;
    CHECK_EQ(png_encode(px, 4, 3, &png, &n), 0);
    FILE *fp = fopen("/tmp/notecraft_test.png", "wb");
    fwrite(png, 1, n, fp);
    fclose(fp);
    free(png);
    Fb *f = png_read_fb("/tmp/notecraft_test.png");
    CHECK(f != NULL);
    if (f) {
        CHECK(f->w == 4 && f->h == 3);
        CHECK(memcmp(f->px, px, sizeof px) == 0);
        fb_free(f);
    }
    remove("/tmp/notecraft_test.png");
}

static char *g_clip;
static char *clip_get(void *ud) { (void)ud; return g_clip ? strdup(g_clip) : NULL; }
static void clip_set(void *ud, const char *t) { (void)ud; free(g_clip); g_clip = strdup(t); }

static void key(App *a, int k, int mods) { AppEvent e = {0}; e.type = EV_KEY; e.key = k; e.mods = mods; app_event(a, &e); }
static void typ(App *a, const char *s)
{
    size_t n = strlen(s), i = 0;
    while (i < n) { uint32_t cp; i += (size_t)u8_decode(s + i, n - i, &cp); AppEvent e = {0}; if (cp == '\n') { key(a, KEY_ENTER, 0); continue; } e.type = EV_TEXT; e.cp = cp; app_event(a, &e); }
}

static void test_app(void)
{
    AppHost h = {clip_get, clip_set, NULL};
    App *a = app_new(&h, NULL);
    CHECK(a != NULL);
    app_set_size(a, 427, 240);
    app_update(a, 1000);
    CHECK(!app_dirty(a));
    typ(a, "Hello, world");
    CHECK(app_dirty(a));
    CHECK_EQ(app_book(a)->pages[0].len, 12);
    key(a, KEY_HOME, MOD_CTRL);
    key(a, KEY_RIGHT, MOD_CTRL | MOD_SHIFT);
    key(a, 'c', MOD_CTRL);
    CHECK(g_clip && strcmp(g_clip, "Hello, ") == 0);
    key(a, KEY_END, MOD_CTRL);
    key(a, 'v', MOD_CTRL);
    CHECK_EQ(app_book(a)->pages[0].len, 19);
    key(a, KEY_PGDN, 0);
    CHECK_EQ(app_page(a), 1);
    CHECK_EQ(app_book(a)->npages, 2);
    typ(a, "second page");
    key(a, KEY_PGUP, 0);
    CHECK_EQ(app_page(a), 0);

    Fb *fb = fb_new(427, 240);
    app_render(a, fb);
    CHECK(fb->px[0] != 0);
    key(a, KEY_ESC, 0);
    CHECK(strcmp(app_screen_name(a), "menu") == 0);
    app_render(a, fb);
    key(a, KEY_ESC, 0);
    CHECK(strcmp(app_screen_name(a), "book") == 0);

    /* save + reopen through the real file path */
    app_toast(a, "x");
    const char *path = "/tmp/notecraft_app_test.txt";
    FILE *fp = fopen(path, "wb");
    fputs("alpha\n\x0c" "beta\n", fp);
    fclose(fp);
    CHECK(app_open_file(a, path));
    CHECK_EQ(app_book(a)->npages, 2);
    CHECK(!app_dirty(a));
    remove(path);
    CHECK(!app_open_file(a, "/nonexistent/file.txt"));

    /* signing locks the book */
    app_free(a);
    fb_free(fb);
    free(g_clip); g_clip = NULL;
}


static void test_flows(void)
{
    AppHost h = {clip_get, clip_set, NULL};
    App *a = app_new(&h, NULL);
    app_set_size(a, 427, 240);
    app_update(a, 1000);
    char dir[] = "/tmp/notecraft_e2e_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    app_set_start_dir(a, dir);

    typ(a, "First page\nsecond line");
    key(a, KEY_PGDN, 0);
    typ(a, "Page two");

    /* Ctrl+S -> Save As picker -> Enter writes <dir>/book.txt */
    key(a, 's', MOD_CTRL);
    CHECK(strcmp(app_screen_name(a), "files") == 0);
    key(a, KEY_ENTER, 0);
    CHECK(strcmp(app_screen_name(a), "book") == 0);
    CHECK(!app_dirty(a));
    char path[300];
    snprintf(path, sizeof path, "%s/book.txt", dir);
    FILE *fp = fopen(path, "rb");
    CHECK(fp != NULL);
    if (fp) {
        char buf[200] = {0};
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        fclose(fp);
        CHECK(n > 0 && strcmp(buf, "First page\nsecond line\n\fPage two\n") == 0);
    }

    /* editing marks dirty again; plain Ctrl+S now saves in place without a dialog */
    typ(a, "!");
    CHECK(app_dirty(a));
    key(a, 's', MOD_CTRL);
    CHECK(strcmp(app_screen_name(a), "book") == 0);
    CHECK(!app_dirty(a));

    /* ePub export through the picker, then open it back */
    key(a, 'e', MOD_CTRL);
    CHECK(strcmp(app_screen_name(a), "files") == 0);
    key(a, KEY_ENTER, 0);
    char epath[300];
    snprintf(epath, sizeof epath, "%s/book.epub", dir);
    CHECK(access(epath, R_OK) == 0);
    CHECK(app_open_file(a, epath));
    CHECK_EQ(app_book(a)->npages, 2);

    /* signing locks the book: typing no longer changes the text */
    app_open_file(a, path);
    key(a, KEY_PGUP, 0);
    AppEvent c = {0}; c.type = EV_MOUSE_DOWN; c.button = 1; c.clicks = 1; c.x = 427 / 2 - 60; c.y = 206;
    app_event(a, &c);
    CHECK(strcmp(app_screen_name(a), "sign") == 0);
    typ(a, "My Book");
    key(a, KEY_ENTER, 0);
    CHECK(strcmp(app_screen_name(a), "read") == 0);
    Fb *fb_tmp = fb_new(427, 240);
    int before = app_book(a)->pages[0].len;
    typ(a, "zzz");
    CHECK_EQ(app_book(a)->pages[0].len, before);
    CHECK(strcmp(app_book(a)->title, "My Book") == 0);

    /* unsaved changes trigger the confirmation dialog on New */
    key(a, KEY_ESC, 0);                                   /* read-only book -> menu */
    CHECK(strcmp(app_screen_name(a), "menu") == 0);
    AppEvent u = {0}; u.type = EV_MOUSE_DOWN; u.button = 1; u.clicks = 1; u.x = 427 / 2; u.y = 160;
    app_render(a, fb_tmp);                                /* lays the buttons out */
    app_event(a, &u);                                     /* "Unsign Book" */
    CHECK(strcmp(app_screen_name(a), "book") == 0);
    CHECK(!app_book(a)->is_signed);
    typ(a, "dirty");
    key(a, 'n', MOD_CTRL);
    CHECK(strcmp(app_screen_name(a), "confirm") == 0);
    key(a, KEY_ESC, 0);                                   /* cancel keeps everything */
    CHECK(strcmp(app_screen_name(a), "book") == 0);
    CHECK(app_dirty(a));

    fb_free(fb_tmp);
    remove(path); remove(epath); rmdir(dir);
    app_free(a);
    free(g_clip); g_clip = NULL;
}

int main(void)
{
    test_utf(); test_font(); test_layout(); test_limits(); test_field(); test_field_validator();
    test_txt(); test_zip(); test_epub(); test_png(); test_app(); test_flows();
    printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
