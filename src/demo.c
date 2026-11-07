/* Headless driver: renders the real app to PNG files without any window.
 * Used for the README screenshots, banner and demo GIF.
 *
 *   notecraft-demo shot   <out.png> <scene> [scale]
 *   notecraft-demo frames <out_dir> [scale]        (frame_0000.png @ 20 fps)
 *   notecraft-demo banner <icon-32.png> <out.png> [scale]
 *   notecraft-demo specimen <out.png> [scale]       (font test sheet)
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "epub.h"
#include "font.h"
#include "png.h"
#include "theme.h"
#include "txt.h"
#include "utf.h"

#define LW 427
#define LH 240
#define FRAME_MS 50

static char *g_clip = NULL;
static char *clip_get(void *ud) { (void)ud; return g_clip ? strdup(g_clip) : NULL; }
static void clip_set(void *ud, const char *t) { (void)ud; free(g_clip); g_clip = strdup(t); }

typedef struct {
    App *app; Fb *fb;
    uint32_t now; int scale;
    const char *dir; int frame, acc;
    int mx, my, show_mouse;
} Drv;

/* A tiny arrow cursor, drawn only into demo frames. */
static const char *CURSOR[] = {
    "X          ", "XX         ", "X.X        ", "X..X       ", "X...X      ", "X....X     ", "X.....X    ", "X......X   ",
    "X.......X  ", "X....XXXXX ", "X..X..X    ", "X.X X..X   ", "XX  X..X   ", "X    X..X  ", "     X..X  ", "      XX   ",
};

static void draw_mouse(Drv *d)
{
    if (!d->show_mouse) return;
    for (int y = 0; y < 16; y++)
        for (int x = 0; CURSOR[y][x]; x++) {
            char c = CURSOR[y][x];
            if (c == 'X') fb_plot(d->fb, d->mx + x, d->my + y, 0xFF000000u);
            else if (c == '.') fb_plot(d->fb, d->mx + x, d->my + y, 0xFFFFFFFFu);
        }
}

static void snap(Drv *d)
{
    app_render(d->app, d->fb);
    draw_mouse(d);
    if (d->dir) {
        char p[1200];
        snprintf(p, sizeof p, "%s/frame_%04d.png", d->dir, d->frame++);
        png_write_fb(p, d->fb, d->scale);
    }
}

/* Advance time; when recording, emit exactly one frame per FRAME_MS. */
static void tick(Drv *d, int ms)
{
    while (ms > 0) {
        int step = ms < FRAME_MS ? ms : FRAME_MS;
        d->now += (uint32_t)step;
        app_update(d->app, d->now);
        d->acc += step;
        ms -= step;
        if (d->acc >= FRAME_MS) { d->acc -= FRAME_MS; if (d->dir) snap(d); }
    }
}

static void key(Drv *d, int k, int mods)
{
    AppEvent e = {0}; e.type = EV_KEY; e.key = k; e.mods = mods;
    app_event(d->app, &e);
}

static void text(Drv *d, uint32_t cp)
{
    AppEvent e = {0}; e.type = EV_TEXT; e.cp = cp;
    app_event(d->app, &e);
}

static void type_str(Drv *d, const char *s, int ms_per_char)
{
    size_t n = strlen(s), i = 0;
    while (i < n) {
        uint32_t cp;
        i += (size_t)u8_decode(s + i, n - i, &cp);
        if (cp == '\n') key(d, KEY_ENTER, 0); else text(d, cp);
        tick(d, ms_per_char);
    }
}

static void move_mouse(Drv *d, int x, int y)
{
    AppEvent e = {0}; e.type = EV_MOUSE_MOVE; e.x = x; e.y = y;
    app_event(d->app, &e);
    d->mx = x; d->my = y; d->show_mouse = 1;
}

/* Glide the pointer to (x,y) over `ms` milliseconds. */
static void glide(Drv *d, int x, int y, int ms)
{
    int x0 = d->mx, y0 = d->my, steps = ms / FRAME_MS;
    if (steps < 1) steps = 1;
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / (float)steps;
        t = t * t * (3.f - 2.f * t);
        move_mouse(d, x0 + (int)((float)(x - x0) * t), y0 + (int)((float)(y - y0) * t));
        tick(d, FRAME_MS);
    }
}

static void click(Drv *d, int x, int y)
{
    move_mouse(d, x, y);
    AppEvent e = {0}; e.type = EV_MOUSE_DOWN; e.x = x; e.y = y; e.button = 1; e.clicks = 1;
    app_event(d->app, &e);
    e.type = EV_MOUSE_UP;
    app_event(d->app, &e);
}

/* ---------------------------------------------------------------- samples */

static void mkdir_p(const char *p)
{
    char t[600]; snprintf(t, sizeof t, "%s", p);
    for (char *c = t + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(t, 0755); *c = '/'; }
    mkdir(t, 0755);
}

static void write_text(const char *path, const char *s)
{
    FILE *f = fopen(path, "wb");
    if (f) { fputs(s, f); fclose(f); }
}

static const char *SAMPLE_DIR = "/tmp/steve/Books";

static void make_samples(void)
{
    char p[700];
    mkdir_p(SAMPLE_DIR);
    snprintf(p, sizeof p, "%s/Redstone", SAMPLE_DIR); mkdir_p(p);
    snprintf(p, sizeof p, "%s/Screenshots", SAMPLE_DIR); mkdir_p(p);
    snprintf(p, sizeof p, "%s/Diary.txt", SAMPLE_DIR);
    write_text(p, "Day 1\n\nI found a village at dusk.\n\f\xD0\x94\xD0\xB5\xD0\xBD\xD1\x8C 2\n");
    snprintf(p, sizeof p, "%s/Recipes.txt", SAMPLE_DIR);
    write_text(p, "Cake: 3 milk, 2 sugar, 1 egg, 3 wheat.\n");
    snprintf(p, sizeof p, "%s/Shopping list.txt", SAMPLE_DIR);
    write_text(p, "diamonds\nobsidian\n");
    Book *b = book_new();
    ImportInfo info;
    const char *src = "It was a quiet night in the library.\n\f\"Silence,\" said the librarian.\n";
    txt_import(b, src, strlen(src), &info);
    snprintf(b->title, sizeof b->title, "The Last Library");
    snprintf(p, sizeof p, "%s/The Last Library.epub", SAMPLE_DIR);
    epub_export(b, NULL, p, NULL, 0);
    book_free(b);
}

static const char *PAGE1 =
    "Day 1\n\nI found a village at dusk. The librarian traded me a book and a quill for six emeralds.\n\n"
    "Notes: iron farm, bed, torches.";
static const char *PAGE2_RU =
    "\xD0\x94\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB3\xD0\xBE\xD0\xB9 \xD0\xB4\xD0\xBD\xD0\xB5\xD0\xB2\xD0\xBD\xD0\xB8\xD0\xBA,\n"
    "\xD1\x81\xD0\xB5\xD0\xB3\xD0\xBE\xD0\xB4\xD0\xBD\xD1\x8F \xD1\x8F \xD0\xBF\xD0\xBE\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xB8\xD0\xBB "
    "\xD0\xB4\xD0\xBE\xD0\xBC \xD1\x83 \xD1\x80\xD0\xB5\xD0\xBA\xD0\xB8.\n\n"
    "\xD0\x92\xD1\x81\xD1\x91 \xD1\x80\xD0\xB0\xD0\xB1\xD0\xBE\xD1\x82\xD0\xB0\xD0\xB5\xD1\x82 \xD0\xBA\xD0\xB0\xD0\xBA \xD0\xBD\xD0\xB0\xD0\xB4\xD0\xBE.";
static const char *CYRILLIC_TEST =
    "\xD0\xA2\xD0\xB5\xD0\xBF\xD0\xB5\xD1\x80\xD1\x8C \xD1\x81 \xD0\xBA\xD0\xB8\xD1\x80\xD0\xB8\xD0\xBB\xD0\xBB\xD0\xB8\xD1\x86\xD0\xB5\xD0\xB9:\n"
    "\xD0\xA1\xD1\x8A\xD0\xB5\xD1\x88\xD1\x8C \xD0\xB5\xD1\x89\xD1\x91 \xD1\x8D\xD1\x82\xD0\xB8\xD1\x85 "
    "\xD0\xBC\xD1\x8F\xD0\xB3\xD0\xBA\xD0\xB8\xD1\x85 \xD1\x84\xD1\x80\xD0\xB0\xD0\xBD\xD1\x86\xD1\x83\xD0\xB7\xD1\x81\xD0\xBA\xD0\xB8\xD1\x85 "
    "\xD0\xB1\xD1\x83\xD0\xBB\xD0\xBE\xD0\xBA.";

/* Static scenes for the README. */
static int scene(Drv *d, const char *name)
{
    if (!strcmp(name, "book")) { type_str(d, PAGE1, 0); return 0; }
    if (!strcmp(name, "cyrillic")) { type_str(d, PAGE1, 0); key(d, KEY_PGDN, 0); type_str(d, PAGE2_RU, 0); return 0; }
    if (!strcmp(name, "selection")) {
        type_str(d, PAGE1, 0);
        key(d, KEY_HOME, MOD_CTRL);
        for (int i = 0; i < 3; i++) key(d, KEY_RIGHT, MOD_CTRL | MOD_SHIFT);
        key(d, KEY_DOWN, MOD_SHIFT); key(d, KEY_DOWN, MOD_SHIFT);
        return 0;
    }
    if (!strcmp(name, "menu")) { type_str(d, PAGE1, 0); key(d, KEY_ESC, 0); move_mouse(d, LW / 2, 114); return 0; }
    if (!strcmp(name, "sign")) {
        type_str(d, PAGE1, 0);
        click(d, LW / 2 - 60, 206);
        type_str(d, "Village Diary", 0);
        return 0;
    }
    if (!strcmp(name, "read")) {
        type_str(d, PAGE1, 0);
        click(d, LW / 2 - 60, 206);
        type_str(d, "Village Diary", 0);
        key(d, KEY_ENTER, 0);
        move_mouse(d, 4, 4);
        d->show_mouse = 0;
        return 0;
    }
    if (!strcmp(name, "files")) {
        key(d, 'o', MOD_CTRL);
        click(d, LW / 2, 44 + 12 * 3 + 6);
        return 0;
    }
    return -1;
}

/* The demo GIF: typing, wrapping, the page limit, page turn, Cyrillic, files, sign. */
static void demo_frames(Drv *d)
{
    int bx = (LW - 192) / 2;
    tick(d, 700);
    type_str(d, "Day 1\n\n", 55);
    type_str(d, "I found a village at dusk. The librarian traded me a book and a quill for six emeralds.", 26);
    tick(d, 400);
    type_str(d, "\n\nNotes: iron farm, bed, torches.", 26);
    tick(d, 700);
    /* select words with the keyboard */
    key(d, KEY_HOME, MOD_CTRL);
    tick(d, 250);
    for (int i = 0; i < 3; i++) { key(d, KEY_RIGHT, MOD_CTRL | MOD_SHIFT); tick(d, 260); }
    tick(d, 700);
    key(d, KEY_END, MOD_CTRL);
    tick(d, 300);
    /* turn the page with the arrow */
    glide(d, bx + 128, 166, 600);
    tick(d, 300);
    click(d, bx + 128, 166);
    tick(d, 500);
    type_str(d, CYRILLIC_TEST, 30);
    tick(d, 900);
    /* run into the 14 line limit of the page */
    key(d, KEY_ENTER, 0);
    for (int i = 0; i < 12; i++) { text(d, 'x'); key(d, KEY_ENTER, 0); tick(d, 70); }
    tick(d, 300);
    for (int i = 0; i < 5; i++) { text(d, 'x'); tick(d, 110); }
    tick(d, 1100);
    /* back one page with the arrow */
    glide(d, bx + 54, 166, 500);
    click(d, bx + 54, 166);
    tick(d, 800);
    /* sign the book */
    glide(d, LW / 2 - 50, 206, 500);
    click(d, LW / 2 - 50, 206);
    tick(d, 500);
    type_str(d, "Village Diary", 75);
    tick(d, 500);
    key(d, KEY_ENTER, 0);
    d->show_mouse = 0;
    tick(d, 1100);
    /* Esc menu */
    key(d, KEY_ESC, 0);
    tick(d, 500);
    glide(d, LW / 2, 140, 500);
    tick(d, 500);
    glide(d, LW / 2 + 50, 94, 400);
    tick(d, 300);
    click(d, LW / 2 + 50, 94);                         /* Open... -> asks about unsaved changes */
    tick(d, 1200);
    glide(d, LW / 2, 138, 400);
    click(d, LW / 2, 138);                             /* Don't Save */
    tick(d, 900);
    glide(d, LW / 2, 44 + 12 * 3 + 6, 500);
    click(d, LW / 2, 44 + 12 * 3 + 6);
    tick(d, 900);
    key(d, KEY_ENTER, 0);                              /* open Diary.txt */
    d->show_mouse = 0;
    tick(d, 1400);
    key(d, KEY_PGDN, 0);
    tick(d, 1600);
}

/* ---------------------------------------------------------------- banner */

static void text_scaled(Fb *dst, int x, int y, const char *s, int k)
{
    Fb *t = fb_new(200, 14);
    fb_clear(t, 0);
    font_draw_u8(t, 0, 0, s, 0xFFFFFFFFu, 1);
    for (int yy = 0; yy < t->h; yy++)
        for (int xx = 0; xx < t->w; xx++) {
            uint32_t c = t->px[yy * t->w + xx];
            if (c >> 24) fb_rect(dst, x + xx * k, y + yy * k, k, k, c);
        }
    fb_free(t);
}

static int banner(const char *icon_path, const char *out, int scale)
{
    Theme *th = theme_new(NULL);
    Fb *icon = png_read_fb(icon_path);
    if (!th || !icon) { fprintf(stderr, "cannot load theme/icon\n"); return 1; }
    Fb *f = fb_new(232, 60);
    fb_tile_dim(f, th->dirt, 2, 70);
    fb_gradient_v(f, 0, 0, 232, 60, 0x20000000u, 0x60000000u);
    fb_blit(f, icon, 0, 0, 32, 32, 12, 14);
    text_scaled(f, 54, 10, "NoteCraft", 3);
    font_draw_u8(f, 56, 40, "A Book and Quill notepad for Linux", 0xFFC8C8C8u, 1);
    int rc = png_write_fb(out, f, scale);
    fb_free(f); fb_free(icon); theme_free(th);
    return rc;
}

/* ---------------------------------------------------------------- specimen */

static int specimen(const char *out, int scale)
{
    Fb *f = fb_new(400, 110);
    fb_clear(f, 0xFFE9DBB2u);
    const char *rows[] = {
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        "abcdefghijklmnopqrstuvwxyz",
        "0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
        "\xD0\x90\xD0\x91\xD0\x92\xD0\x93\xD0\x94\xD0\x95\xD0\x81\xD0\x96\xD0\x97\xD0\x98\xD0\x99\xD0\x9A\xD0\x9B\xD0\x9C\xD0\x9D\xD0\x9E\xD0\x9F\xD0\xA0\xD0\xA1\xD0\xA2\xD0\xA3\xD0\xA4\xD0\xA5\xD0\xA6\xD0\xA7\xD0\xA8\xD0\xA9\xD0\xAA\xD0\xAB\xD0\xAC\xD0\xAD\xD0\xAE\xD0\xAF",
        "\xD0\xB0\xD0\xB1\xD0\xB2\xD0\xB3\xD0\xB4\xD0\xB5\xD1\x91\xD0\xB6\xD0\xB7\xD0\xB8\xD0\xB9\xD0\xBA\xD0\xBB\xD0\xBC\xD0\xBD\xD0\xBE\xD0\xBF\xD1\x80\xD1\x81\xD1\x82\xD1\x83\xD1\x84\xD1\x85\xD1\x86\xD1\x87\xD1\x88\xD1\x89\xD1\x8A\xD1\x8B\xD1\x8C\xD1\x8D\xD1\x8E\xD1\x8F",
        "\xC2\xAB\xD0\x9A\xD0\xB0\xD0\xB2\xD1\x8B\xD1\x87\xD0\xBA\xD0\xB8\xC2\xBB \xE2\x80\x94 \xE2\x84\x96 5 \xE2\x80\xA6 \xE2\x82\xAC \xC2\xA3 \xC2\xB0 \xC3\x97",
        "The quick brown fox jumps over the lazy dog. 0123456789",
        "\xD0\xA1\xD1\x8A\xD0\xB5\xD1\x88\xD1\x8C \xD0\xB5\xD1\x89\xD1\x91 \xD1\x8D\xD1\x82\xD0\xB8\xD1\x85 \xD0\xBC\xD1\x8F\xD0\xB3\xD0\xBA\xD0\xB8\xD1\x85 \xD0\xB1\xD1\x83\xD0\xBB\xD0\xBE\xD0\xBA, \xD0\xB4\xD0\xB0 \xD0\xB2\xD1\x8B\xD0\xBF\xD0\xB5\xD0\xB9 \xD1\x87\xD0\xB0\xD1\x8E.",
    };
    for (int i = 0; i < 8; i++) font_draw_u8(f, 4, 4 + i * 12, rows[i], 0xFF000000u, 0);
    int rc = png_write_fb(out, f, scale);
    fb_free(f);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s shot <out.png> <scene> [scale] | frames <dir> [scale] | banner <icon> <out> [scale] | specimen <out> [scale]\n", argv[0]);
        return 2;
    }
    setenv("NOTECRAFT_AUTHOR", "Steve", 1);
    if (!strcmp(argv[1], "specimen")) return specimen(argv[2], argc > 3 ? atoi(argv[3]) : 3);
    if (!strcmp(argv[1], "banner") && argc >= 4) return banner(argv[2], argv[3], argc > 4 ? atoi(argv[4]) : 4);

    make_samples();
    AppHost host = {clip_get, clip_set, NULL};
    Drv d = {0};
    d.app = app_new(&host, NULL);
    d.fb = fb_new(LW, LH);
    if (!d.app || !d.fb) return 1;
    app_set_size(d.app, LW, LH);
    app_set_start_dir(d.app, SAMPLE_DIR);
    d.now = 1000;
    app_update(d.app, d.now);

    if (!strcmp(argv[1], "shot") && argc >= 4) {
        d.scale = argc > 4 ? atoi(argv[4]) : 3;
        if (scene(&d, argv[3]) != 0) { fprintf(stderr, "unknown scene %s\n", argv[3]); return 2; }
        app_update(d.app, d.now);                 /* keep the caret in its visible phase */
        snap(&d);
        d.dir = NULL;
        app_render(d.app, d.fb);
        draw_mouse(&d);
        if (png_write_fb(argv[2], d.fb, d.scale) != 0) { fprintf(stderr, "write failed\n"); return 1; }
        return 0;
    }
    if (!strcmp(argv[1], "frames")) {
        d.dir = argv[2];
        d.scale = argc > 3 ? atoi(argv[3]) : 2;
        mkdir_p(argv[2]);
        demo_frames(&d);
        printf("%d frames\n", d.frame);
        return 0;
    }
    return 2;
}
