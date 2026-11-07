#include "app.h"

#include <dirent.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "epub.h"
#include "field.h"
#include "font.h"
#include "layout.h"
#include "theme.h"
#include "txt.h"
#include "utf.h"

#define PATH_CAP 1024
#define BLINK_MS 300
#define TOAST_MS 3500

typedef enum { SCR_BOOK, SCR_SIGN, SCR_READ, SCR_MENU, SCR_FILES, SCR_CONFIRM } Screen;
typedef enum { P_NONE, P_NEW, P_OPEN_DIALOG, P_OPEN_PATH, P_QUIT } Pending;
typedef enum { FP_OPEN, FP_SAVE_TXT, FP_SAVE_EPUB } FpMode;
typedef enum { CF_UNSAVED, CF_OVERWRITE } ConfirmKind;

typedef enum {
    B_NONE, B_SIGN, B_DONE, B_SIGN_OK, B_SIGN_CANCEL, B_READ_DONE, B_NEXT, B_PREV,
    B_MENU_BACK, B_NEW, B_OPEN, B_SAVE, B_SAVEAS, B_EXPORT, B_UNSIGN, B_QUIT,
    B_FP_OK, B_FP_CANCEL, B_CF1, B_CF2, B_CF3
} BtnId;

typedef struct { int x, y, w, h, id, enabled; char label[40]; } Btn;
typedef struct { char name[256]; int dir; } FEnt;

typedef struct {
    FpMode mode;
    char dir[PATH_CAP];
    FEnt *ents; int n;
    int sel, scroll;
    uint32_t pbuf[PATH_CAP + 4]; int plen;
    Field pf;
    uint32_t last_click_ms; int last_click_row;
    char saved_name[256];
} FilePicker;

struct App {
    Book *book;
    char path[PATH_CAP];            /* current .txt path, "" if none */
    bool dirty;
    int page;
    Field pf;                       /* editing state of the active page */
    int anim; uint32_t anim_t0; int anim_old;   /* page-turn animation: direction, start time, old left page */

    Screen screen;
    int W, H;

    uint32_t tbuf[64]; int tlen; Field tf;      /* signing title */

    FilePicker fp;
    char last_dir[PATH_CAP];

    ConfirmKind cf_kind;
    Pending pend; char pend_path[PATH_CAP];
    Pending after_save;
    char overwrite_path[PATH_CAP];
    FpMode overwrite_mode;

    int mx, my; bool mdown;
    uint32_t last_click_ms; int last_click_idx;

    uint32_t now, blink_base; int last_blink;
    char toast[240]; uint32_t toast_until;

    Btn btn[12]; int nbtn;

    AppHost host;
    Theme *theme;
    bool want_quit, want_fullscreen, want_screenshot, redraw;
};

/* ------------------------------------------------------------------ utils */

static const char *base_name(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static bool is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool is_file(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static bool has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n >= e && strcasecmp(name + n - e, ext) == 0;
}

static void dir_of(const char *path, char *out, size_t cap)
{
    copy_str(out, cap, path);
    char *s = strrchr(out, '/');
    if (!s) copy_str(out, cap, ".");
    else if (s == out) out[1] = 0;
    else *s = 0;
}

void app_toast(App *a, const char *msg)
{
    copy_str(a->toast, sizeof a->toast, msg);
    a->toast_until = a->now + TOAST_MS;
    a->redraw = true;
}

static void blink_reset(App *a) { a->blink_base = a->now; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------------- page state */

static bool page_valid_cb(const uint32_t *s, int n, void *ud) { (void)ud; return page_valid(s, n); }
static bool title_valid_cb(const uint32_t *s, int n, void *ud) { (void)ud; return title_valid(s, n); }

static void bind_page(App *a)
{
    a->page = clampi(a->page, 0, a->book->npages - 1);
    Page *p = &a->book->pages[a->page];
    field_bind(&a->pf, p->cp, &p->len, BOOK_PAGE_CAP, page_valid_cb, NULL);
}

static void goto_page(App *a, int p)
{
    a->anim = 0;
    a->page = clampi(p, 0, a->book->npages - 1);
    bind_page(a);
    blink_reset(a);
}

/* A spread shows two pages; keep the page count even so the right page exists. */
static void ensure_even(App *a)
{
    if ((a->book->npages & 1) && a->book->npages < BOOK_MAX_PAGES) book_add_page(a->book);
}

static bool editable(const App *a) { return !a->book->is_signed; }

static void turn(App *a, int dir, bool allow_add)
{
    int sp = a->page & ~1, nsp = sp + dir * 2;
    if (nsp < 0) return;
    if (nsp >= a->book->npages) {
        if (!allow_add || !editable(a)) return;
        if (nsp + 2 > BOOK_MAX_PAGES) { app_toast(a, "Books are limited to 100 pages"); return; }
        while (a->book->npages < nsp + 2) book_add_page(a->book);
    }
    a->anim = dir; a->anim_t0 = a->now; a->anim_old = sp;
    a->page = nsp;
    bind_page(a);
    blink_reset(a);
}

static void page_forward(App *a, bool allow_add) { turn(a, +1, allow_add); }

static void page_back(App *a) { turn(a, -1, false); }

/* ------------------------------------------------------- layout & cursor */

#define DISP_CAP 24

static int disp_lines(const App *a, Line *out)
{
    const Page *p = &a->book->pages[a->page];
    return layout_display(p->cp, p->len, BOOK_TEXT_W, out, DISP_CAP);
}

#define BK_W BOOK_SPREAD_W
#define BK_H BOOK_SPREAD_H
#define SPINE_X 150
#define TX_L 24            /* text origin of the left / right page, 114px wide */
#define TX_R 162
#define TX_Y 30
#define ARROW_Y 168
#define STRIP_Y 193        /* the two buttons sit on the cover below the pages */
#define PG_X0 9
#define PG_X1 290
#define PG_Y0 9
#define PG_Y1 189
#define ANIM_MS 380

static int book_ox(const App *a) { return (a->W - BK_W) / 2; }
static int book_oy(const App *a) { return (a->H - BK_H) / 2; }
static int side_tx(int side) { return side ? TX_R : TX_L; }

/* Nearest cursor index for a point local to the text area. */
static int index_at(const App *a, int lx, int ly)
{
    const Page *p = &a->book->pages[a->page];
    Line lines[DISP_CAP];
    int nl = disp_lines(a, lines);
    if (ly < 0) return 0;
    int li = ly / FONT_LINE_H;
    if (li >= nl) return p->len;
    const Line *l = &lines[li];
    int end = layout_vis_end(p->cp, l);
    int w = 0;
    for (int i = l->start; i < end; i++) {
        int cw = font_cp_width(p->cp[i]);
        if (lx < w + cw / 2 + (cw & 1)) return i;
        w += cw;
    }
    return end;
}

static void cursor_xy(const App *a, int pos, int *x, int *y)
{
    const Page *p = &a->book->pages[a->page];
    Line lines[DISP_CAP];
    int nl = disp_lines(a, lines);
    int li = layout_line_of(lines, nl, pos);
    *x = font_width(p->cp + lines[li].start, pos - lines[li].start);
    *y = li * FONT_LINE_H;
}

static void move_line(App *a, int dir, bool extend)
{
    const Page *p = &a->book->pages[a->page];
    Line lines[DISP_CAP];
    int nl = disp_lines(a, lines);
    int li = layout_line_of(lines, nl, a->pf.cur);
    int k = li + dir;
    if (k < 0 || k >= nl) return;
    int col = a->pf.cur - lines[li].start;
    int vis = layout_vis_end(p->cp, &lines[k]) - lines[k].start;
    field_set_cursor(&a->pf, lines[k].start + (col < vis ? col : vis), extend);
}

static void home_end(App *a, bool end, bool ctrl, bool extend)
{
    const Page *p = &a->book->pages[a->page];
    if (ctrl) { field_set_cursor(&a->pf, end ? p->len : 0, extend); return; }
    Line lines[DISP_CAP];
    int nl = disp_lines(a, lines);
    int li = layout_line_of(lines, nl, a->pf.cur);
    field_set_cursor(&a->pf, end ? layout_vis_end(p->cp, &lines[li]) : lines[li].start, extend);
}

/* ------------------------------------------------------------ edit helpers */

static bool is_typable(uint32_t cp)
{
    return cp >= 0x20 && cp != 0x7F && cp != 0xA7 && !(cp >= 0x80 && cp < 0xA0) && cp <= 0x10FFFF
           && !(cp >= 0xD800 && cp < 0xE000);
}

static void mark_dirty(App *a) { a->dirty = true; }

static void insert_cp(App *a, uint32_t cp)
{
    if (!editable(a)) return;
    if (cp == '\n' || is_typable(cp))
        if (field_insert(&a->pf, &cp, 1)) mark_dirty(a);
    blink_reset(a);
}

static void paste_text(App *a, Field *f, bool page, const char *u8)
{
    size_t n = strlen(u8);
    uint32_t *raw = malloc((n + 1) * sizeof(uint32_t)), *clean = malloc((n * 4 + 1) * sizeof(uint32_t));
    if (!raw || !clean) { free(raw); free(clean); return; }
    int rn = (int)u8_to_cps(u8, n, raw, n);
    int cn = txt_sanitize(raw, rn, clean, NULL);
    int k = 0;
    for (int i = 0; i < cn; i++) {
        if (clean[i] == '\f') clean[i] = '\n';
        if (clean[i] == '\n' && !page) continue;           /* single-line fields */
        clean[k++] = clean[i];
    }
    int took = field_insert_fit(f, clean, k);
    if (page && took > 0) mark_dirty(a);
    if (took < k) app_toast(a, page ? "Paste truncated: page is full" : "Text too long");
    free(raw); free(clean);
}

static void do_copy(App *a, Field *f, bool cut, bool page)
{
    if (!field_has_selection(f) || !a->host.set_clipboard) return;
    char *buf = malloc((size_t)(*f->len) * 4 + 1);
    if (!buf) return;
    field_selection_u8(f, buf, *f->len * 4 + 1);
    a->host.set_clipboard(a->host.ud, buf);
    free(buf);
    if (cut && (!page || editable(a))) { if (field_delete_selection(f) && page) mark_dirty(a); }
}

static void do_paste(App *a, Field *f, bool page)
{
    if (!a->host.get_clipboard) return;
    if (page && !editable(a)) return;
    char *t = a->host.get_clipboard(a->host.ud);
    if (!t) return;
    paste_text(a, f, page, t);
    free(t);
}

/* ------------------------------------------------------------- file i/o */

static uint8_t *read_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 64L * 1024 * 1024) { fclose(f); return NULL; }
    uint8_t *d = malloc((size_t)sz + 1);
    if (!d || fread(d, 1, (size_t)sz, f) != (size_t)sz) { free(d); fclose(f); return NULL; }
    fclose(f);
    d[sz] = 0;
    *n = (size_t)sz;
    return d;
}

static void reset_view(App *a)
{
    ensure_even(a);
    a->page = 0;
    bind_page(a);
    field_set_cursor(&a->pf, 0, false);
    a->screen = a->book->is_signed ? SCR_READ : SCR_BOOK;
    blink_reset(a);
}

static void new_book(App *a)
{
    book_clear(a->book);
    a->path[0] = 0;
    a->dirty = false;
    reset_view(a);
}

bool app_open_file(App *a, const char *path)
{
    size_t n;
    uint8_t *d = read_file(path, &n);
    if (!d) { app_toast(a, "Cannot read file"); return false; }
    Book *nb = book_new();
    if (!nb) { free(d); return false; }
    copy_str(nb->author, sizeof nb->author, a->book->author);
    ImportInfo info;
    bool epub = has_ext(path, ".epub") || (n > 4 && d[0] == 'P' && d[1] == 'K');
    char err[160] = "";
    bool ok = true;
    if (epub) ok = epub_import_mem(nb, d, n, &info, err, sizeof err) == 0;
    else txt_import(nb, (const char *)d, n, &info);
    free(d);
    if (!ok) { book_free(nb); app_toast(a, err[0] ? err : "Cannot open file"); return false; }
    book_free(a->book);
    a->book = nb;
    if (epub) a->path[0] = 0; else copy_str(a->path, sizeof a->path, path);
    a->dirty = false;
    dir_of(path, a->last_dir, sizeof a->last_dir);
    reset_view(a);
    char msg[300];
    if (info.truncated) snprintf(msg, sizeof msg, "Opened %s - only the first %d pages fit", base_name(path), BOOK_MAX_PAGES);
    else snprintf(msg, sizeof msg, "Opened %s (%d page%s)", base_name(path), a->book->npages, a->book->npages == 1 ? "" : "s");
    app_toast(a, msg);
    return true;
}

static bool write_bytes(const char *path, const void *d, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(d, 1, n, f) == n;
    if (fclose(f)) ok = false;
    return ok;
}

static void run_pending(App *a, Pending p);

static void after_save(App *a)
{
    Pending p = a->after_save;
    a->after_save = P_NONE;
    if (p != P_NONE) run_pending(a, p);
}

static bool save_txt(App *a, const char *path)
{
    size_t n;
    char *d = txt_export(a->book, &n);
    if (!d) { app_toast(a, "Out of memory"); return false; }
    bool ok = write_bytes(path, d, n);
    free(d);
    if (!ok) { app_toast(a, "Could not save file"); return false; }
    copy_str(a->path, sizeof a->path, path);
    dir_of(path, a->last_dir, sizeof a->last_dir);
    a->dirty = false;
    char msg[300];
    snprintf(msg, sizeof msg, "Saved %s", base_name(path));
    app_toast(a, msg);
    return true;
}

static bool export_epub(App *a, const char *path)
{
    char fallback[256], err[160] = "";
    copy_str(fallback, sizeof fallback, base_name(path));
    char *dot = strrchr(fallback, '.');
    if (dot) *dot = 0;
    if (epub_export(a->book, fallback, path, err, sizeof err) != 0) {
        app_toast(a, err[0] ? err : "Could not write ePub");
        return false;
    }
    dir_of(path, a->last_dir, sizeof a->last_dir);
    char msg[300];
    snprintf(msg, sizeof msg, "Exported %s", base_name(path));
    app_toast(a, msg);
    return true;
}

/* ---------------------------------------------------------- file picker */

static int ent_cmp(const void *x, const void *y)
{
    const FEnt *a = x, *b = y;
    if (strcmp(a->name, "..") == 0) return -1;
    if (strcmp(b->name, "..") == 0) return 1;
    if (a->dir != b->dir) return b->dir - a->dir;
    return strcasecmp(a->name, b->name);
}


/* Absolute, lexically normalised path ("." and ".." resolved, symlinks kept
 * as typed - like a file manager's breadcrumb). */
static void normalize_path(const char *in, char *out, size_t cap)
{
    char full[PATH_CAP * 2];
    if (in[0] == '/') copy_str(full, sizeof full, in);
    else {
        char cwd[PATH_CAP];
        if (!getcwd(cwd, sizeof cwd)) copy_str(cwd, sizeof cwd, "/");
        snprintf(full, sizeof full, "%s/%s", cwd, in);
    }
    char *parts[256]; int np = 0;
    for (char *tok = full, *next; tok; tok = next) {
        next = strchr(tok, '/');
        if (next) *next++ = 0;
        if (!*tok || strcmp(tok, ".") == 0) continue;
        if (strcmp(tok, "..") == 0) { if (np) np--; continue; }
        if (np < 256) parts[np++] = tok;
    }
    size_t w = 0;
    out[0] = 0;
    if (np == 0) { copy_str(out, cap, "/"); return; }
    for (int i = 0; i < np && w + 2 < cap; i++) w += (size_t)snprintf(out + w, cap - w, "/%s", parts[i]);
}

static void fp_set_path(App *a, const char *p)
{
    FilePicker *fp = &a->fp;
    fp->plen = (int)u8_to_cps(p, strlen(p), fp->pbuf, PATH_CAP);
    field_bind(&fp->pf, fp->pbuf, &fp->plen, PATH_CAP, NULL, NULL);
}

static void fp_default_path(App *a)
{
    FilePicker *fp = &a->fp;
    char p[PATH_CAP + 300];
    const char *suffix = "";
    char name[300];
    if (fp->mode == FP_SAVE_TXT) {
        suffix = a->path[0] ? base_name(a->path) : "book.txt";
    } else if (fp->mode == FP_SAVE_EPUB) {
        if (a->book->title[0]) snprintf(name, sizeof name, "%s.epub", a->book->title);
        else if (a->path[0]) {
            snprintf(name, sizeof name, "%s", base_name(a->path));
            char *dot = strrchr(name, '.');
            if (dot) *dot = 0;
            strncat(name, ".epub", sizeof name - strlen(name) - 1);
        } else snprintf(name, sizeof name, "book.epub");
        suffix = name;
    }
    snprintf(p, sizeof p, "%s%s%s", fp->dir, strcmp(fp->dir, "/") ? "/" : "", suffix);
    fp_set_path(a, p);
}

static void fp_read_dir(App *a, const char *dir)
{
    FilePicker *fp = &a->fp;
    free(fp->ents); fp->ents = NULL; fp->n = 0;
    char real[PATH_CAP];
    normalize_path(dir, real, sizeof real);
    if (!is_dir(real)) copy_str(real, sizeof real, "/");
    copy_str(fp->dir, sizeof fp->dir, real);
    int cap = 64;
    fp->ents = malloc((size_t)cap * sizeof *fp->ents);
    if (!fp->ents) return;
    if (strcmp(real, "/") != 0) { copy_str(fp->ents[0].name, 256, ".."); fp->ents[0].dir = 1; fp->n = 1; }
    DIR *d = opendir(real);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) && fp->n < 5000) {
            if (de->d_name[0] == '.') continue;
            char full[PATH_CAP + 300];
            snprintf(full, sizeof full, "%s/%s", real, de->d_name);
            struct stat st;
            if (stat(full, &st) != 0) continue;
            int dir_flag = S_ISDIR(st.st_mode);
            if (!dir_flag) {
                if (!S_ISREG(st.st_mode)) continue;
                if (fp->mode == FP_SAVE_TXT && !has_ext(de->d_name, ".txt")) continue;
                if (fp->mode == FP_SAVE_EPUB && !has_ext(de->d_name, ".epub")) continue;
            }
            if (fp->n == cap) {
                cap *= 2;
                FEnt *t = realloc(fp->ents, (size_t)cap * sizeof *t);
                if (!t) break;
                fp->ents = t;
            }
            copy_str(fp->ents[fp->n].name, 256, de->d_name);
            fp->ents[fp->n].dir = dir_flag;
            fp->n++;
        }
        closedir(d);
    }
    qsort(fp->ents, (size_t)fp->n, sizeof *fp->ents, ent_cmp);
    fp->sel = -1; fp->scroll = 0;
}

static void open_picker(App *a, FpMode mode)
{
    FilePicker *fp = &a->fp;
    fp->mode = mode;
    fp->last_click_row = -1;
    const char *start = a->last_dir[0] ? a->last_dir : getenv("HOME");
    if (!start || !is_dir(start)) start = ".";
    fp_read_dir(a, start);
    fp_default_path(a);
    field_to_end(&fp->pf);
    a->screen = SCR_FILES;
    blink_reset(a);
}

static void fp_path_u8(const App *a, char *out, size_t cap)
{
    cps_to_u8(a->fp.pbuf, a->fp.plen, out, cap);
}

static void do_save_as_path(App *a, FpMode mode, const char *path)
{
    bool ok = mode == FP_SAVE_EPUB ? export_epub(a, path) : save_txt(a, path);
    if (ok) {
        a->screen = a->book->is_signed ? SCR_READ : SCR_BOOK;
        if (mode == FP_SAVE_TXT) after_save(a);
    }
}

static void fp_accept(App *a)
{
    FilePicker *fp = &a->fp;
    char typed[PATH_CAP + 8], path[PATH_CAP + 300];
    fp_path_u8(a, typed, sizeof typed);
    if (!typed[0]) return;
    if (typed[0] == '~' && (typed[1] == '/' || !typed[1]) && getenv("HOME")) snprintf(path, sizeof path, "%s%s", getenv("HOME"), typed + 1);
    else if (typed[0] != '/') snprintf(path, sizeof path, "%s/%s", fp->dir, typed);
    else copy_str(path, sizeof path, typed);

    if (is_dir(path)) {
        fp_read_dir(a, path);
        fp_default_path(a);
        field_to_end(&fp->pf);
        return;
    }
    if (fp->mode == FP_OPEN) {
        if (!is_file(path)) { app_toast(a, "File not found"); return; }
        a->pend = P_OPEN_PATH;
        copy_str(a->pend_path, sizeof a->pend_path, path);
        if (a->dirty) {
            /* unsaved changes: ask first */
            a->cf_kind = CF_UNSAVED;
            a->screen = SCR_CONFIRM;
        } else run_pending(a, P_OPEN_PATH);
        return;
    }
    const char *ext = fp->mode == FP_SAVE_EPUB ? ".epub" : ".txt";
    if (!has_ext(path, ext) && !(fp->mode == FP_SAVE_TXT && has_ext(path, ".text")))
        strncat(path, ext, sizeof path - strlen(path) - 1);
    if (is_file(path)) {
        a->cf_kind = CF_OVERWRITE;
        a->overwrite_mode = fp->mode;
        copy_str(a->overwrite_path, sizeof a->overwrite_path, path);
        a->screen = SCR_CONFIRM;
        return;
    }
    do_save_as_path(a, fp->mode, path);
}

static void fp_select(App *a, int row)
{
    FilePicker *fp = &a->fp;
    if (row < 0 || row >= fp->n) return;
    fp->sel = row;
    const FEnt *e = &fp->ents[row];
    char p[PATH_CAP + 300];
    if (e->dir) {
        if (strcmp(e->name, "..") == 0) { char up[PATH_CAP]; dir_of(fp->dir, up, sizeof up); snprintf(p, sizeof p, "%s/", up); }
        else snprintf(p, sizeof p, "%s%s%s/", fp->dir, strcmp(fp->dir, "/") ? "/" : "", e->name);
        if (strcmp(p, "//") == 0) copy_str(p, sizeof p, "/");
    } else snprintf(p, sizeof p, "%s%s%s", fp->dir, strcmp(fp->dir, "/") ? "/" : "", e->name);
    fp_set_path(a, p);
    field_to_end(&fp->pf);
}

static int fp_rows(const App *a) { return (a->H - 44 - 58) / 12; }

/* ------------------------------------------------------ high level actions */

static void goto_home_screen(App *a) { a->screen = a->book->is_signed ? SCR_READ : SCR_BOOK; blink_reset(a); }

static void leave_picker(App *a)
{
    a->after_save = P_NONE;
    a->pend = P_NONE;
    goto_home_screen(a);
}

static void ask_unsaved(App *a, Pending p)
{
    a->pend = p;
    a->cf_kind = CF_UNSAVED;
    a->screen = SCR_CONFIRM;
}

static void run_pending(App *a, Pending p)
{
    switch (p) {
    case P_NEW: new_book(a); break;
    case P_OPEN_DIALOG: open_picker(a, FP_OPEN); break;
    case P_OPEN_PATH: app_open_file(a, a->pend_path); break;
    case P_QUIT: a->want_quit = true; break;
    default: break;
    }
}

static void request(App *a, Pending p)
{
    if (a->dirty && p != P_NONE) ask_unsaved(a, p);
    else run_pending(a, p);
}

static void action_save(App *a, bool as, Pending then)
{
    book_trim_trailing_empty(a->book);
    a->page = clampi(a->page, 0, a->book->npages - 1);
    bind_page(a);
    a->after_save = then;
    if (!as && a->path[0]) {
        if (save_txt(a, a->path)) after_save(a);
        return;
    }
    open_picker(a, FP_SAVE_TXT);
}

static void action_export(App *a)
{
    book_trim_trailing_empty(a->book);
    a->page = clampi(a->page, 0, a->book->npages - 1);
    bind_page(a);
    open_picker(a, FP_SAVE_EPUB);
}

static void start_signing(App *a)
{
    const char *t = a->book->title;
    a->tlen = (int)u8_to_cps(t, strlen(t), a->tbuf, 63);
    while (a->tlen > 0 && !title_valid(a->tbuf, a->tlen)) a->tlen--;
    field_bind(&a->tf, a->tbuf, &a->tlen, 64, title_valid_cb, NULL);
    field_to_end(&a->tf);
    a->screen = SCR_SIGN;
    blink_reset(a);
}

static void finish_signing(App *a)
{
    if (a->tlen <= 0) return;
    cps_to_u8(a->tbuf, a->tlen, a->book->title, sizeof a->book->title);
    a->book->is_signed = true;
    a->screen = SCR_READ;
    app_toast(a, "Book signed");
}

/* -------------------------------------------------------------- UI layout */

static void add_btn(App *a, int id, int x, int y, int w, int h, const char *label, int enabled)
{
    if (a->nbtn >= (int)(sizeof a->btn / sizeof a->btn[0])) return;
    Btn *b = &a->btn[a->nbtn++];
    b->id = id; b->x = x; b->y = y; b->w = w; b->h = h; b->enabled = enabled;
    copy_str(b->label, sizeof b->label, label);
}

static void build_ui(App *a)
{
    int cx = a->W / 2, ox = book_ox(a), oy = book_oy(a);
    a->nbtn = 0;
    switch (a->screen) {
    case SCR_BOOK:
    case SCR_READ: {
        int sp = a->page & ~1;
        if (sp > 0) add_btn(a, B_PREV, ox + TX_L + 4, oy + ARROW_Y, 23, 13, "", 1);
        if (sp + 2 < a->book->npages || (a->screen == SCR_BOOK && a->book->npages <= BOOK_MAX_PAGES - 2))
            add_btn(a, B_NEXT, ox + TX_R + BOOK_TEXT_W - 23 - 4, oy + ARROW_Y, 23, 13, "", 1);
        if (a->screen == SCR_BOOK) {
            add_btn(a, B_SIGN, ox + 48, oy + STRIP_Y, 98, 20, "Sign", 1);
            add_btn(a, B_DONE, ox + 154, oy + STRIP_Y, 98, 20, "Done", 1);
        } else add_btn(a, B_READ_DONE, ox + 50, oy + STRIP_Y, 200, 20, "Done", 1);
        break;
    }
    case SCR_SIGN:
        add_btn(a, B_SIGN_OK, ox + 48, oy + STRIP_Y, 98, 20, "Sign and Close", a->tlen > 0);
        add_btn(a, B_SIGN_CANCEL, ox + 154, oy + STRIP_Y, 98, 20, "Cancel", 1);
        break;
    case SCR_MENU: {
        int rows = a->book->is_signed ? 6 : 5;
        int y0 = (a->H - (rows * 24 - 4)) / 2 + 10;
        if (y0 < 30) y0 = 30;
        int y = y0;
        add_btn(a, B_MENU_BACK, cx - 100, y, 200, 20, "Back to Book", 1); y += 24;
        add_btn(a, B_NEW, cx - 100, y, 98, 20, "New Book", 1);
        add_btn(a, B_OPEN, cx + 2, y, 98, 20, "Open...", 1); y += 24;
        add_btn(a, B_SAVE, cx - 100, y, 98, 20, "Save", 1);
        add_btn(a, B_SAVEAS, cx + 2, y, 98, 20, "Save As...", 1); y += 24;
        add_btn(a, B_EXPORT, cx - 100, y, 200, 20, "Export as ePub...", 1); y += 24;
        if (a->book->is_signed) { add_btn(a, B_UNSIGN, cx - 100, y, 200, 20, "Unsign Book (make editable)", 1); y += 24; }
        add_btn(a, B_QUIT, cx - 100, y, 200, 20, "Quit NoteCraft", 1);
        break;
    }
    case SCR_FILES: {
        int lx = cx - 110, by = a->H - 28;
        add_btn(a, B_FP_OK, lx, by, 108, 20, a->fp.mode == FP_OPEN ? "Open" : (a->fp.mode == FP_SAVE_EPUB ? "Export" : "Save"), 1);
        add_btn(a, B_FP_CANCEL, lx + 112, by, 108, 20, "Cancel", 1);
        break;
    }
    case SCR_CONFIRM: {
        int y = a->H / 2 + 8;
        if (a->cf_kind == CF_UNSAVED) {
            add_btn(a, B_CF1, cx - 112, y, 72, 20, "Save", 1);
            add_btn(a, B_CF2, cx - 36, y, 72, 20, "Don't Save", 1);
            add_btn(a, B_CF3, cx + 40, y, 72, 20, "Cancel", 1);
        } else {
            add_btn(a, B_CF1, cx - 100, y, 98, 20, "Overwrite", 1);
            add_btn(a, B_CF3, cx + 2, y, 98, 20, "Cancel", 1);
        }
        break;
    }
    }
}

static const Btn *hit_btn(const App *a, int x, int y)
{
    for (int i = 0; i < a->nbtn; i++) {
        const Btn *b = &a->btn[i];
        if (b->enabled && x >= b->x && y >= b->y && x < b->x + b->w && y < b->y + b->h) return b;
    }
    return NULL;
}

static void press(App *a, int id)
{
    switch (id) {
    case B_SIGN: start_signing(a); break;
    case B_DONE: action_save(a, false, P_NONE); break;
    case B_SIGN_OK: finish_signing(a); break;
    case B_SIGN_CANCEL: a->screen = SCR_BOOK; break;
    case B_READ_DONE: a->screen = SCR_MENU; break;
    case B_NEXT: page_forward(a, true); break;
    case B_PREV: page_back(a); break;
    case B_MENU_BACK: goto_home_screen(a); break;
    case B_NEW: request(a, P_NEW); break;
    case B_OPEN: request(a, P_OPEN_DIALOG); break;
    case B_SAVE: action_save(a, false, P_NONE); if (a->screen == SCR_MENU) goto_home_screen(a); break;
    case B_SAVEAS: action_save(a, true, P_NONE); break;
    case B_EXPORT: action_export(a); break;
    case B_UNSIGN: a->book->is_signed = false; goto_home_screen(a); app_toast(a, "Book unsigned"); break;
    case B_QUIT: request(a, P_QUIT); break;
    case B_FP_OK: fp_accept(a); break;
    case B_FP_CANCEL: leave_picker(a); break;
    case B_CF1:
        if (a->cf_kind == CF_OVERWRITE) {
            do_save_as_path(a, a->overwrite_mode, a->overwrite_path);
            if (a->screen == SCR_CONFIRM) goto_home_screen(a);
        } else {
            /* save, then continue with the pending action */
            Pending p = a->pend;
            a->pend = P_NONE;
            goto_home_screen(a);
            a->after_save = p;      /* P_OPEN_PATH keeps its target in pend_path */
            book_trim_trailing_empty(a->book);
            bind_page(a);
            if (a->path[0]) { if (save_txt(a, a->path)) after_save(a); }
            else open_picker(a, FP_SAVE_TXT);
        }
        break;
    case B_CF2: {
        Pending p = a->pend;
        a->pend = P_NONE;
        a->dirty = false;
        goto_home_screen(a);
        run_pending(a, p);
        break;
    }
    case B_CF3:
        a->pend = P_NONE;
        a->after_save = P_NONE;
        a->screen = a->fp.n && a->cf_kind == CF_OVERWRITE ? SCR_FILES : (a->book->is_signed ? SCR_READ : SCR_BOOK);
        break;
    default: break;
    }
    blink_reset(a);
}

/* ------------------------------------------------------------ input events */

static void select_word(App *a, int idx)
{
    int s = field_word_pos(&a->pf, -1, idx);
    int e = idx;
    const Page *p = &a->book->pages[a->page];
    while (e < p->len && p->cp[e] != ' ' && p->cp[e] != '\n') e++;
    if (s > idx) s = idx;
    field_select_range(&a->pf, s, e);
}

static void global_shortcuts(App *a, int key, int mods, bool *handled)
{
    *handled = true;
    bool ctrl = mods & MOD_CTRL, shift = mods & MOD_SHIFT;
    if (key == KEY_F2) { a->want_screenshot = true; return; }
    if (key == KEY_F11) { a->want_fullscreen = true; return; }
    if (ctrl && key == 's') { action_save(a, shift, P_NONE); return; }
    if (ctrl && key == 'o') { request(a, P_OPEN_DIALOG); return; }
    if (ctrl && key == 'n') { request(a, P_NEW); return; }
    if (ctrl && key == 'e') { action_export(a); return; }
    if (ctrl && key == 'q') { request(a, P_QUIT); return; }
    *handled = false;
}

static void key_book(App *a, int key, int mods)
{
    bool ctrl = mods & MOD_CTRL, shift = mods & MOD_SHIFT;
    Field *f = &a->pf;
    blink_reset(a);
    if (ctrl && key == 'a') { field_select_all(f); return; }
    if (ctrl && key == 'c') { do_copy(a, f, false, true); return; }
    if (ctrl && key == 'x') { do_copy(a, f, true, true); return; }
    if (ctrl && key == 'v') { do_paste(a, f, true); return; }
    switch (key) {
    case KEY_ESC: a->screen = SCR_MENU; break;
    case KEY_ENTER: insert_cp(a, '\n'); break;
    case KEY_BACKSPACE: if (editable(a) && field_delete(f, -1, ctrl)) mark_dirty(a); break;
    case KEY_DELETE: if (editable(a) && field_delete(f, +1, ctrl)) mark_dirty(a); break;
    case KEY_LEFT:
        if (f->cur == 0 && !shift && !ctrl && (a->page & 1)) { a->page--; bind_page(a); field_to_end(&a->pf); }
        else field_move(f, -1, ctrl, shift);
        break;
    case KEY_RIGHT:
        if (f->cur == *f->len && !shift && !ctrl && !(a->page & 1) && a->page + 1 < a->book->npages) { a->page++; bind_page(a); field_set_cursor(&a->pf, 0, false); }
        else field_move(f, +1, ctrl, shift);
        break;
    case KEY_TAB:
        if ((a->page ^ 1) < a->book->npages) { a->page ^= 1; bind_page(a); field_to_end(&a->pf); }
        break;
    case KEY_UP: move_line(a, -1, shift); break;
    case KEY_DOWN: move_line(a, +1, shift); break;
    case KEY_HOME: home_end(a, false, ctrl, shift); break;
    case KEY_END: home_end(a, true, ctrl, shift); break;
    case KEY_PGUP: page_back(a); break;
    case KEY_PGDN: page_forward(a, true); break;
    default: break;
    }
}

static void key_read(App *a, int key, int mods)
{
    (void)mods;
    switch (key) {
    case KEY_ESC: a->screen = SCR_MENU; break;
    case KEY_LEFT: case KEY_PGUP: case KEY_UP: page_back(a); break;
    case KEY_RIGHT: case KEY_PGDN: case KEY_DOWN: case KEY_ENTER: page_forward(a, false); break;
    case KEY_HOME: goto_page(a, 0); break;
    case KEY_END: goto_page(a, a->book->npages - 1); break;
    default: break;
    }
}

static void key_sign(App *a, int key, int mods)
{
    bool ctrl = mods & MOD_CTRL;
    Field *f = &a->tf;
    blink_reset(a);
    if (ctrl && key == 'v') { do_paste(a, f, false); return; }
    if (ctrl && key == 'c') { field_select_all(f); do_copy(a, f, false, false); field_to_end(f); return; }
    switch (key) {
    case KEY_ESC: a->screen = SCR_BOOK; break;
    case KEY_ENTER: finish_signing(a); break;
    case KEY_BACKSPACE: field_delete(f, -1, ctrl); break;
    default: break;
    }
}

static void key_files(App *a, int key, int mods)
{
    FilePicker *fp = &a->fp;
    bool ctrl = mods & MOD_CTRL, shift = mods & MOD_SHIFT;
    Field *f = &fp->pf;
    blink_reset(a);
    if (ctrl && key == 'a') { field_select_all(f); return; }
    if (ctrl && key == 'c') { do_copy(a, f, false, false); return; }
    if (ctrl && key == 'x') { do_copy(a, f, true, false); return; }
    if (ctrl && key == 'v') { do_paste(a, f, false); return; }
    switch (key) {
    case KEY_ESC: leave_picker(a); break;
    case KEY_ENTER: fp_accept(a); break;
    case KEY_BACKSPACE: field_delete(f, -1, ctrl); fp->sel = -1; break;
    case KEY_DELETE: field_delete(f, +1, ctrl); fp->sel = -1; break;
    case KEY_LEFT: field_move(f, -1, ctrl, shift); break;
    case KEY_RIGHT: field_move(f, +1, ctrl, shift); break;
    case KEY_HOME: field_set_cursor(f, 0, shift); break;
    case KEY_END: field_set_cursor(f, *f->len, shift); break;
    case KEY_UP: case KEY_DOWN: {
        int dir = key == KEY_UP ? -1 : 1;
        int row = fp->sel < 0 ? (dir > 0 ? 0 : fp->n - 1) : fp->sel + dir;
        row = clampi(row, 0, fp->n - 1);
        fp_select(a, row);
        int rows = fp_rows(a);
        if (row < fp->scroll) fp->scroll = row;
        if (row >= fp->scroll + rows) fp->scroll = row - rows + 1;
        break;
    }
    case KEY_PGUP: fp->scroll = clampi(fp->scroll - fp_rows(a), 0, fp->n); break;
    case KEY_PGDN: fp->scroll = clampi(fp->scroll + fp_rows(a), 0, fp->n); break;
    default: break;
    }
}

static void on_key(App *a, int key, int mods)
{
    bool handled;
    if (a->screen != SCR_FILES && a->screen != SCR_SIGN && a->screen != SCR_CONFIRM) {
        global_shortcuts(a, key, mods, &handled);
        if (handled) return;
    } else if (key == KEY_F2 || key == KEY_F11) {
        global_shortcuts(a, key, mods, &handled);
        return;
    } else if ((mods & MOD_CTRL) && a->screen == SCR_FILES && key == 'q') {
        request(a, P_QUIT);
        return;
    }
    switch (a->screen) {
    case SCR_BOOK: key_book(a, key, mods); break;
    case SCR_READ: key_read(a, key, mods); break;
    case SCR_SIGN: key_sign(a, key, mods); break;
    case SCR_FILES: key_files(a, key, mods); break;
    case SCR_MENU: if (key == KEY_ESC) goto_home_screen(a); break;
    case SCR_CONFIRM:
        if (key == KEY_ESC) press(a, B_CF3);
        else if (key == KEY_ENTER) press(a, B_CF1);
        break;
    }
}

static void on_text(App *a, uint32_t cp)
{
    if (!is_typable(cp)) return;
    if (a->screen == SCR_BOOK) insert_cp(a, cp);
    else if (a->screen == SCR_SIGN) { if (field_insert(&a->tf, &cp, 1)) blink_reset(a); }
    else if (a->screen == SCR_FILES) { if (field_insert(&a->fp.pf, &cp, 1)) { a->fp.sel = -1; blink_reset(a); } }
}

static void on_mouse_down(App *a, const AppEvent *e)
{
    a->mx = e->x; a->my = e->y;
    if (e->button != 1) return;
    build_ui(a);
    const Btn *b = hit_btn(a, e->x, e->y);
    if (b) { press(a, b->id); return; }
    if (a->screen == SCR_BOOK) {
        int ox = book_ox(a), oy = book_oy(a), rx = e->x - ox, ry = e->y - oy;
        if (rx >= PG_X0 && rx <= PG_X1 && ry >= PG_Y0 && ry <= PG_Y1) {
            int side = rx >= SPINE_X ? 1 : 0;
            int pi = (a->page & ~1) + side;
            if (pi >= a->book->npages && editable(a)) ensure_even(a);
            if (pi < a->book->npages) {
                if (pi != a->page) { a->page = pi; bind_page(a); }
                int idx = index_at(a, rx - side_tx(side), ry - TX_Y);
                if (e->clicks >= 2 && idx == a->last_click_idx) {
                    select_word(a, idx);
                } else {
                    field_set_cursor(&a->pf, idx, (e->mods & MOD_SHIFT) != 0);
                    a->mdown = true;
                }
                a->last_click_idx = idx;
                a->last_click_ms = a->now;
                blink_reset(a);
            }
        }
    } else if (a->screen == SCR_FILES) {
        FilePicker *fp = &a->fp;
        int lx = a->W / 2 - 110, top = 44, rows = fp_rows(a);
        if (e->x >= lx && e->x < lx + 220 && e->y >= top && e->y < top + rows * 12) {
            int row = fp->scroll + (e->y - top) / 12;
            if (row < fp->n) {
                bool dbl = e->clicks >= 2 && row == fp->last_click_row;
                fp_select(a, row);
                fp->last_click_row = row;
                if (dbl) fp_accept(a);
            }
        }
    }
}

static void on_mouse_move(App *a, const AppEvent *e)
{
    a->mx = e->x; a->my = e->y;
    if (a->mdown && a->screen == SCR_BOOK) {
        int idx = index_at(a, e->x - (book_ox(a) + side_tx(a->page & 1)), e->y - (book_oy(a) + TX_Y));
        field_set_cursor(&a->pf, idx, true);
        blink_reset(a);
    }
}

static void on_wheel(App *a, int w)
{
    if (a->screen == SCR_BOOK || a->screen == SCR_READ) {
        if (w > 0) page_back(a); else if (w < 0) page_forward(a, false);
    } else if (a->screen == SCR_FILES) {
        a->fp.scroll = clampi(a->fp.scroll - w * 3, 0, a->fp.n > 0 ? a->fp.n - 1 : 0);
    }
}

void app_event(App *a, const AppEvent *e)
{
    a->redraw = true;
    if (a->anim && (e->type == EV_KEY || e->type == EV_TEXT || e->type == EV_MOUSE_DOWN)) a->anim = 0;   /* skip the animation */
    switch (e->type) {
    case EV_TEXT: on_text(a, e->cp); break;
    case EV_KEY: on_key(a, e->key, e->mods); break;
    case EV_MOUSE_DOWN: on_mouse_down(a, e); break;
    case EV_MOUSE_UP: a->mdown = false; break;
    case EV_MOUSE_MOVE: on_mouse_move(a, e); break;
    case EV_WHEEL: on_wheel(a, e->wheel); break;
    case EV_DROP:
        if (e->path && is_file(e->path)) {
            a->pend = P_OPEN_PATH;
            copy_str(a->pend_path, sizeof a->pend_path, e->path);
            if (a->dirty) ask_unsaved(a, P_OPEN_PATH); else run_pending(a, P_OPEN_PATH);
        }
        break;
    default: break;
    }
}

/* ---------------------------------------------------------------- render */

#define COL_BLACK 0xFF000000u
#define COL_WHITE 0xFFFFFFFFu

static void draw_cps_shadow(Fb *f, int x, int y, const uint32_t *s, int n, uint32_t c)
{
    font_draw(f, x + 1, y + 1, s, n, (c & 0xFF000000u) | ((c & 0xFCFCFCu) >> 2));
    font_draw(f, x, y, s, n, c);
}

static void draw_wrapped(Fb *f, int x, int y, int w, const char *u8, uint32_t color)
{
    uint32_t cps[256];
    int n = (int)u8_to_cps(u8, strlen(u8), cps, 256);
    Line lines[16];
    int nl = layout_split(cps, n, w, lines, 16);
    for (int i = 0; i < nl; i++)
        font_draw(f, x, y + i * FONT_LINE_H, cps + lines[i].start, layout_vis_end(cps, &lines[i]) - lines[i].start, color);
}

static void draw_buttons(const App *a, Fb *f)
{
    for (int i = 0; i < a->nbtn; i++) {
        const Btn *b = &a->btn[i];
        bool hover = b->enabled && a->mx >= b->x && a->my >= b->y && a->mx < b->x + b->w && a->my < b->y + b->h;
        if (b->id == B_NEXT || b->id == B_PREV) {
            int sx = hover ? 23 : 0, sy = b->id == B_NEXT ? 0 : 13;
            fb_blit(f, a->theme->arrows, sx, sy, 23, 13, b->x, b->y);
            continue;
        }
        theme_button(f, b->x, b->y, b->w, b->h, !b->enabled ? BTN_DISABLED : hover ? BTN_HOVER : BTN_NORMAL);
        uint32_t col = !b->enabled ? 0xFFA0A0A0u : hover ? 0xFFFFFFA0u : 0xFFE0E0E0u;
        font_draw_u8_centered(f, b->x + b->w / 2, b->y + (b->h - 8) / 2, b->label, col, 1);
    }
}

static int blink_phase(const App *a) { return (int)(((a->now - a->blink_base) / BLINK_MS) & 1); }

static void draw_page_face(App *a, Fb *f, int ox, int oy, int pi, int side, bool edit)
{
    if (pi < 0 || pi >= a->book->npages) return;
    const Page *p = &a->book->pages[pi];
    int tx = ox + side_tx(side);
    char ind[48];
    snprintf(ind, sizeof ind, "Page %d of %d", pi + 1, a->book->npages);
    font_draw_u8(f, side ? tx + BOOK_TEXT_W - font_width_u8(ind) : tx, oy + 15, ind, COL_BLACK, 0);

    Line lines[DISP_CAP];
    int nl = layout_display(p->cp, p->len, BOOK_TEXT_W, lines, DISP_CAP);
    int ty = oy + TX_Y;
    for (int i = 0; i < nl; i++) {
        int ve = layout_vis_end(p->cp, &lines[i]);
        font_draw(f, tx, ty + i * FONT_LINE_H, p->cp + lines[i].start, ve - lines[i].start, COL_BLACK);
    }
    if (!edit || pi != a->page) return;

    int sa, sb;
    field_sel_range(&a->pf, &sa, &sb);
    if (sa != sb) {
        for (int i = 0; i < nl; i++) {
            int ls = lines[i].start, ve = layout_vis_end(p->cp, &lines[i]);
            int s0 = sa > ls ? sa : ls, e0 = sb < ve ? sb : ve;
            if (e0 <= s0) continue;
            int x1 = font_width(p->cp + ls, s0 - ls), x2 = font_width(p->cp + ls, e0 - ls);
            fb_or_reverse(f, tx + x1, ty + i * FONT_LINE_H, x2 - x1, FONT_LINE_H, 0x0000FF);
        }
    }
    if (blink_phase(a) == 0) {
        int cx, cy;
        cursor_xy(a, a->pf.cur, &cx, &cy);
        if (a->pf.cur == p->len) font_draw_cp(f, tx + cx, ty + cy, '_', COL_BLACK);
        else fb_rect(f, tx + cx, ty + cy - 1, 1, 10, COL_BLACK);
    }
}

static void draw_spread(App *a, Fb *f, int ox, int oy, int left, int right, bool edit)
{
    fb_blit(f, a->theme->book, 0, 0, BK_W, BK_H, ox, oy);
    draw_page_face(a, f, ox, oy, left, 0, edit);
    draw_page_face(a, f, ox, oy, right, 1, edit);
}

/* The page turn: a leaf swings around the spine. It is the old page while it
 * is on the side it came from and the new page once it has crossed over;
 * its width follows cos(angle), its free edge lifts and it darkens mid-turn. */
static void draw_anim(App *a, Fb *f, int ox, int oy)
{
    float t = (float)(a->now - a->anim_t0) / (float)ANIM_MS;
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    float e = t * t * (3.f - 2.f * t);
    int fwd = a->anim > 0, S = a->anim_old;
    int base_l = fwd ? S : S - 2, base_r = fwd ? S + 3 : S + 1;
    int lface = fwd ? S + 2 : S, rface = fwd ? S + 1 : S - 1;
    draw_spread(a, f, ox, oy, base_l, base_r, false);

    float th = (fwd ? e : 1.f - e) * 3.14159265f;
    float c = cosf(th), sn = sinf(th);
    int on_right = c > 0.f;
    int pi = on_right ? rface : lface;
    Fb *tmp = fb_new(BK_W, BK_H);
    if (!tmp) return;
    fb_clear(tmp, 0xFF000000u);
    if (on_right) draw_spread(a, tmp, 0, 0, pi - 1, pi, false);
    else draw_spread(a, tmp, 0, 0, pi, pi + 1, false);

    const int HW = SPINE_X - PG_X0 - 0;             /* columns from the spine to the outer page edge */
    int wproj = (int)((float)HW * fabsf(c) + 0.5f);
    if (wproj < 1) wproj = 1;
    for (int d = 0; d < wproj; d++) {
        float u = wproj > 1 ? (float)d / (float)(wproj - 1) : 0.f;
        int sx = on_right ? SPINE_X + (d * HW) / wproj : (SPINE_X - 1) - (d * HW) / wproj;
        int dx = on_right ? ox + SPINE_X + d : ox + SPINE_X - 1 - d;
        int lift = (int)(8.f * sn * u);
        float sh = 1.f - 0.16f * sn - 0.08f * sn * u;
        for (int y = PG_Y0; y <= PG_Y1; y++) {
            uint32_t px = tmp->px[(size_t)y * BK_W + (size_t)sx];
            uint32_t r = (uint32_t)((float)((px >> 16) & 255) * sh), g = (uint32_t)((float)((px >> 8) & 255) * sh), b = (uint32_t)((float)(px & 255) * sh);
            if (d == wproj - 1) { r = 0x7a; g = 0x66; b = 0x40; }      /* the free edge of the leaf */
            fb_plot(f, dx, oy + y - lift, 0xFF000000u | (r << 16) | (g << 8) | b);
        }
    }
    /* soft cast shadow on the page underneath */
    for (int k = 1; k <= 10; k++) {
        int x = on_right ? ox + SPINE_X + wproj - 1 + k : ox + SPINE_X - wproj - k;
        if (x < ox + PG_X0 || x > ox + PG_X1) continue;
        int alpha = (int)(70.f * sn * (float)(11 - k) / 10.f);
        if (alpha > 0) fb_rect(f, x, oy + PG_Y0, 1, PG_Y1 - PG_Y0 + 1, (uint32_t)alpha << 24);
    }
    fb_free(tmp);
}

static void draw_book_page(App *a, Fb *f, bool editing)
{
    int ox = book_ox(a), oy = book_oy(a), sp = a->page & ~1;
    if (a->anim) draw_anim(a, f, ox, oy);
    else draw_spread(a, f, ox, oy, sp, sp + 1, editing);
}

static void draw_sign(App *a, Fb *f)
{
    int ox = book_ox(a), oy = book_oy(a), tx = ox + TX_R, ty = oy + TX_Y;
    draw_spread(a, f, ox, oy, -1, -1, false);
    const char *label = "Enter Book Title:";
    font_draw_u8(f, tx + (BOOK_TEXT_W - font_width_u8(label)) / 2, ty + 2, label, COL_BLACK, 0);

    int tw = font_width(a->tbuf, a->tlen) + font_cp_width('_');
    int x0 = tx + (BOOK_TEXT_W - tw) / 2;
    font_draw(f, x0, ty + 18, a->tbuf, a->tlen, COL_BLACK);
    font_draw_cp(f, x0 + font_width(a->tbuf, a->tlen), ty + 18, '_', blink_phase(a) == 0 ? COL_BLACK : 0xFFAAAAAAu);

    char by[100];
    snprintf(by, sizeof by, "by %s", a->book->author);
    font_draw_u8(f, tx + (BOOK_TEXT_W - font_width_u8(by)) / 2, ty + 28, by, 0xFF555555u, 0);
    draw_wrapped(f, tx, ty + 50, BOOK_TEXT_W, "Note! When you sign the book, it will no longer be editable.", COL_BLACK);
}

static void draw_editbox(const App *a, Fb *f, int x, int y, int w, int h, const Field *fl, bool focused)
{
    fb_rect(f, x, y, w, h, focused ? 0xFFFFFFFFu : 0xFFA0A0A0u);
    fb_rect(f, x + 1, y + 1, w - 2, h - 2, COL_BLACK);
    int avail = w - 10;
    int cw = font_width(fl->buf, fl->cur);
    int scroll = cw > avail ? cw - avail : 0;
    int ty = y + (h - 8) / 2;
    Fb *fbw = f;
    fb_set_clip(fbw, x + 1, y + 1, w - 2, h - 2);
    draw_cps_shadow(f, x + 4 - scroll, ty, fl->buf, *fl->len, 0xFFE0E0E0u);
    int sa, sb; field_sel_range(fl, &sa, &sb);
    if (sa != sb) {
        int x1 = font_width(fl->buf, sa), x2 = font_width(fl->buf, sb);
        fb_or_reverse(f, x + 4 - scroll + x1 - 1, ty - 1, x2 - x1 + 1, 10, 0x0000FF);
    }
    if (focused && blink_phase(a) == 0) {
        if (fl->cur == *fl->len) font_draw_cp(f, x + 4 - scroll + cw, ty, '_', 0xFFE0E0E0u);
        else fb_rect(f, x + 4 - scroll + cw - 1, ty - 1, 1, 10, 0xFFD0D0D0u);
    }
    fb_reset_clip(f);
}

static void draw_menu(App *a, Fb *f)
{
    int cx = a->W / 2;
    int top = a->btn[0].y;
    font_draw_u8_centered(f, cx, top - 34, "NoteCraft", COL_WHITE, 1);
    char info[120];
    snprintf(info, sizeof info, "%d page%s, %d characters", a->book->npages, a->book->npages == 1 ? "" : "s", book_total_chars(a->book));
    font_draw_u8_centered(f, cx, top - 20, info, 0xFFAAAAAAu, 1);
}

static void draw_files(App *a, Fb *f)
{
    FilePicker *fp = &a->fp;
    int cx = a->W / 2, lx = cx - 110, top = 44, rows = fp_rows(a), lh = rows * 12;
    const char *title = fp->mode == FP_OPEN ? "Open Book" : fp->mode == FP_SAVE_EPUB ? "Export as ePub" : "Save Book As";
    font_draw_u8_centered(f, cx, 14, title, COL_WHITE, 1);
    /* shorten the directory from the left so it fits */
    char dir[PATH_CAP];
    copy_str(dir, sizeof dir, fp->dir);
    const char *shown = dir;
    char buf[PATH_CAP + 4];
    while (font_width_u8(shown) > 216 && strlen(shown) > 4) {
        shown++;
        snprintf(buf, sizeof buf, "...%s", shown);
        if (font_width_u8(buf) <= 216) { shown = buf; break; }
    }
    font_draw_u8_centered(f, cx, 28, shown, 0xFFAAAAAAu, 1);

    /* list panel: darker dirt with shadows like the vanilla world list */
    fb_rect(f, lx, top, 220, lh, 0xA0000000u);
    fb_set_clip(f, lx, top, 220, lh);
    for (int i = 0; i < rows && fp->scroll + i < fp->n; i++) {
        int row = fp->scroll + i, y = top + i * 12;
        const FEnt *e = &fp->ents[row];
        if (row == fp->sel) { fb_rect(f, lx, y, 220, 12, 0xFF808080u); fb_rect(f, lx + 1, y + 1, 218, 10, COL_BLACK); }
        char name[300];
        snprintf(name, sizeof name, "%s%s", e->name, e->dir && strcmp(e->name, "..") ? "/" : "");
        font_draw_u8(f, lx + 4, y + 2, name, e->dir ? COL_WHITE : 0xFFB0B0B0u, 1);
    }
    if (fp->n == 0) font_draw_u8_centered(f, cx, top + 8, "(empty)", 0xFF808080u, 1);
    fb_reset_clip(f);
    fb_gradient_v(f, lx, top, 220, 4, 0xFF000000u, 0x00000000u);
    fb_gradient_v(f, lx, top + lh - 4, 220, 4, 0x00000000u, 0xFF000000u);
    if (fp->n > rows) {
        int bh = lh * rows / fp->n; if (bh < 8) bh = 8;
        int by = top + (lh - bh) * fp->scroll / (fp->n - rows > 0 ? fp->n - rows : 1);
        fb_rect(f, lx + 214, top, 6, lh, COL_BLACK);
        fb_rect(f, lx + 214, by, 6, bh, 0xFF808080u);
        fb_rect(f, lx + 214, by, 5, bh - 1, 0xFFC0C0C0u);
    }
    draw_editbox(a, f, lx, a->H - 52, 220, 18, &fp->pf, true);
}

static void draw_confirm(App *a, Fb *f)
{
    int cx = a->W / 2, y = a->H / 2 - 30;
    if (a->cf_kind == CF_UNSAVED) {
        font_draw_u8_centered(f, cx, y, "You have unsaved changes.", COL_WHITE, 1);
        font_draw_u8_centered(f, cx, y + 14, "Save them first?", 0xFFAAAAAAu, 1);
    } else {
        font_draw_u8_centered(f, cx, y, "This file already exists.", COL_WHITE, 1);
        font_draw_u8_centered(f, cx, y + 14, base_name(a->overwrite_path), 0xFFAAAAAAu, 1);
    }
}

static void draw_toast(App *a, Fb *f)
{
    if (a->now >= a->toast_until || !a->toast[0]) return;
    int w = font_width_u8(a->toast) + 10;
    int x = (a->W - w) / 2, y = 4;
    fb_rect(f, x, y - 3, w, 14, 0xA0000000u);
    font_draw_u8(f, x + 5, y, a->toast, COL_WHITE, 1);
}

void app_render(App *a, Fb *f)
{
    int on_book = a->screen == SCR_BOOK || a->screen == SCR_READ || a->screen == SCR_SIGN;
    fb_tile_dim(f, a->theme->leather, 2, on_book ? 100 : 120);
    build_ui(a);
    switch (a->screen) {
    case SCR_BOOK: draw_book_page(a, f, true); break;
    case SCR_READ: draw_book_page(a, f, false); break;
    case SCR_SIGN: draw_sign(a, f); break;
    case SCR_MENU: draw_menu(a, f); break;
    case SCR_FILES: draw_files(a, f); break;
    case SCR_CONFIRM: draw_confirm(a, f); break;
    }
    draw_buttons(a, f);
    draw_toast(a, f);
    a->redraw = false;
}

/* ------------------------------------------------------------- lifecycle */

App *app_new(const AppHost *host, const char *pack_dir)
{
    App *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    if (host) a->host = *host;
    a->book = book_new();
    a->theme = theme_new(pack_dir);
    if (!a->book || !a->theme) { app_free(a); return NULL; }
    a->W = BK_W; a->H = BK_H;
    a->last_click_idx = -1;
    ensure_even(a);
    bind_page(a);
    a->redraw = true;
    return a;
}

void app_free(App *a)
{
    if (!a) return;
    book_free(a->book);
    theme_free(a->theme);
    free(a->fp.ents);
    free(a);
}

void app_set_size(App *a, int w, int h) { a->W = w; a->H = h; a->redraw = true; }
void app_set_start_dir(App *a, const char *dir) { if (is_dir(dir)) copy_str(a->last_dir, sizeof a->last_dir, dir); }

void app_update(App *a, uint32_t now)
{
    a->now = now;
    int ph = blink_phase(a);
    if (ph != a->last_blink) { a->last_blink = ph; a->redraw = true; }
    if (a->toast_until && now >= a->toast_until) { a->toast_until = 0; a->redraw = true; }
    if (a->anim) { a->redraw = true; if (now - a->anim_t0 >= ANIM_MS) a->anim = 0; }
}

bool app_needs_redraw(const App *a) { return a->redraw; }
bool app_wants_quit(const App *a) { return a->want_quit; }
bool app_take_fullscreen_toggle(App *a) { bool r = a->want_fullscreen; a->want_fullscreen = false; return r; }
bool app_take_screenshot_request(App *a) { bool r = a->want_screenshot; a->want_screenshot = false; return r; }

void app_window_title(const App *a, char *out, int cap)
{
    const char *name = a->path[0] ? base_name(a->path) : (a->book->title[0] ? a->book->title : "Untitled");
    snprintf(out, (size_t)cap, "NoteCraft - %s%s", name, a->dirty ? "*" : "");
}

Book *app_book(App *a) { return a->book; }
int app_page(const App *a) { return a->page; }
bool app_dirty(const App *a) { return a->dirty; }

const char *app_screen_name(const App *a)
{
    static const char *n[] = {"book", "sign", "read", "menu", "files", "confirm"};
    return n[a->screen];
}
