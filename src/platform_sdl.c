/* SDL2 front end: window, input, clipboard.
 *
 * The window is a free-form, aspect-locked view of the book: drag any edge or
 * corner and the whole book scales proportionally (no bars, no extra area). */
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "app.h"
#include "epub.h"
#include "png.h"
#include "theme.h"
#include "utf.h"

#ifdef NC_HAVE_ICON
#include "icon_data.h"
#endif

#ifdef __APPLE__
#include <SDL_syswm.h>
#include <objc/message.h>
#include <objc/runtime.h>
typedef struct { double w, h; } NCSize;

/* Let Cocoa keep the aspect ratio natively, so live resizing is exact. */
static void lock_aspect_native(SDL_Window *win)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(win, &info) || info.subsystem != SDL_SYSWM_COCOA) return;
    void *nswin = (void *)info.info.cocoa.window;
    if (!nswin) return;
    NCSize sz = {BOOK_SPREAD_W, BOOK_SPREAD_H};
    ((void (*)(void *, SEL, NCSize))objc_msgSend)(nswin, sel_registerName("setContentAspectRatio:"), sz);
}
#endif

static char *host_get_clipboard(void *ud)
{
    (void)ud;
    char *t = SDL_GetClipboardText();
    if (!t) return NULL;
    char *c = *t ? strdup(t) : NULL;
    SDL_free(t);
    return c;
}

static void host_set_clipboard(void *ud, const char *t)
{
    (void)ud;
    SDL_SetClipboardText(t);
}

static int map_key(const SDL_KeyboardEvent *k)
{
    switch (k->keysym.sym) {
    case SDLK_BACKSPACE: return KEY_BACKSPACE;
    case SDLK_DELETE: return KEY_DELETE;
    case SDLK_LEFT: return KEY_LEFT;
    case SDLK_RIGHT: return KEY_RIGHT;
    case SDLK_UP: return KEY_UP;
    case SDLK_DOWN: return KEY_DOWN;
    case SDLK_HOME: return KEY_HOME;
    case SDLK_END: return KEY_END;
    case SDLK_PAGEUP: return KEY_PGUP;
    case SDLK_PAGEDOWN: return KEY_PGDN;
    case SDLK_RETURN: case SDLK_KP_ENTER: return KEY_ENTER;
    case SDLK_ESCAPE: return KEY_ESC;
    case SDLK_TAB: return KEY_TAB;
    case SDLK_F2: return KEY_F2;
    case SDLK_F11: return KEY_F11;
    default: break;
    }
    /* Shortcuts follow the physical key so Ctrl+C works on any layout (e.g. Russian). */
    SDL_Scancode sc = k->keysym.scancode;
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'a' + (int)(sc - SDL_SCANCODE_A);
    return KEY_NONE;
}

static int map_mods(Uint16 m)
{
    int r = 0;
    if (m & KMOD_SHIFT) r |= MOD_SHIFT;
    if (m & (KMOD_CTRL | KMOD_GUI)) r |= MOD_CTRL;   /* Cmd acts as Ctrl on macOS */
    if (m & KMOD_ALT) r |= MOD_ALT;
    return r;
}

/* The book always keeps its 300:216 shape. The window is kept in that shape
 * too; only maximised/fullscreen windows can differ, and then the book is
 * centred on a dark backdrop. */
static void fit_rect(int pw, int ph, SDL_Rect *r)
{
    long wl = (long)pw * BOOK_SPREAD_H, hl = (long)ph * BOOK_SPREAD_W;
    int dw, dh;
    if (labs(wl - hl) <= 2L * BOOK_SPREAD_W) { dw = pw; dh = ph; }   /* off by a pixel or two: just fill */
    else if (wl < hl) { dw = pw; dh = (int)(((long)pw * BOOK_SPREAD_H + BOOK_SPREAD_W / 2) / BOOK_SPREAD_W); }
    else { dh = ph; dw = (int)(((long)ph * BOOK_SPREAD_W + BOOK_SPREAD_H / 2) / BOOK_SPREAD_H); }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    r->w = dw; r->h = dh; r->x = (pw - dw) / 2; r->y = (ph - dh) / 2;
}

typedef struct {
    App *app;
    Fb *fb;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;      /* the book, 1:1 (BOOK_SPREAD_W x BOOK_SPREAD_H) */
    SDL_Texture *tgt;      /* integer-upscaled copy, filtered down to odd window sizes */
    int tgt_m;
    int max_tex;
    char last_title[300];
    const char *selftest_shot;
    bool shot_done;
} Ctx;

static void mkdirs(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

static void save_screenshot(App *app, const Fb *fb, int scale)
{
    const char *home = getenv("HOME");
    char dir[800], path[1000], msg[1100];
    snprintf(dir, sizeof dir, "%s/Pictures/NoteCraft", home ? home : ".");
    mkdirs(dir);
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    char stamp[64];
    strftime(stamp, sizeof stamp, "%Y-%m-%d_%H.%M.%S", lt);
    snprintf(path, sizeof path, "%s/%s.png", dir, stamp);
    if (png_write_fb(path, fb, scale) == 0) snprintf(msg, sizeof msg, "Saved screenshot as %s.png", stamp);
    else snprintf(msg, sizeof msg, "Could not save screenshot");
    app_toast(app, msg);
}

static void draw_frame(Ctx *c)
{
    int pw, ph;
    SDL_GetRendererOutputSize(c->ren, &pw, &ph);
    if (pw < 1 || ph < 1) return;
    SDL_Rect r;
    fit_rect(pw, ph, &r);
    int scale = r.w / BOOK_SPREAD_W;
    if (scale < 1) scale = 1;

    app_render(c->app, c->fb);
    if (app_take_screenshot_request(c->app)) {
        save_screenshot(c->app, c->fb, scale);
        app_render(c->app, c->fb);
    }
    SDL_UpdateTexture(c->tex, NULL, c->fb->px, BOOK_SPREAD_W * 4);
    SDL_SetRenderDrawColor(c->ren, 22, 15, 10, 255);
    SDL_RenderClear(c->ren);

    if (r.w % BOOK_SPREAD_W == 0 && r.h == r.w / BOOK_SPREAD_W * BOOK_SPREAD_H) {
        SDL_RenderCopy(c->ren, c->tex, NULL, &r);          /* whole-number scale: crisp pixels */
    } else {
        /* Any other size: blow the book up by a whole number first (crisp), then
         * filter that down to the window so every pixel keeps even proportions. */
        int m = (r.w + BOOK_SPREAD_W - 1) / BOOK_SPREAD_W;
        int mmax = c->max_tex / BOOK_SPREAD_W;
        if (c->max_tex / BOOK_SPREAD_H < mmax) mmax = c->max_tex / BOOK_SPREAD_H;
        if (m > mmax) m = mmax;
        if (m < 1) m = 1;
        if (!c->tgt || c->tgt_m != m) {
            if (c->tgt) SDL_DestroyTexture(c->tgt);
            c->tgt = SDL_CreateTexture(c->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                       BOOK_SPREAD_W * m, BOOK_SPREAD_H * m);
            c->tgt_m = m;
#if SDL_VERSION_ATLEAST(2, 0, 12)
            if (c->tgt) SDL_SetTextureScaleMode(c->tgt, SDL_ScaleModeLinear);
#endif
        }
        if (c->tgt && SDL_SetRenderTarget(c->ren, c->tgt) == 0) {
            SDL_RenderCopy(c->ren, c->tex, NULL, NULL);
            SDL_SetRenderTarget(c->ren, NULL);
            SDL_RenderCopy(c->ren, c->tgt, NULL, &r);
        } else {
            SDL_SetRenderTarget(c->ren, NULL);
            SDL_RenderCopy(c->ren, c->tex, NULL, &r);
        }
    }

    if (c->selftest_shot) {
        uint32_t *pix = malloc((size_t)pw * (size_t)ph * 4);
        if (pix && SDL_RenderReadPixels(c->ren, NULL, SDL_PIXELFORMAT_ARGB8888, pix, pw * 4) == 0) {
            for (size_t k = 0; k < (size_t)pw * (size_t)ph; k++) pix[k] |= 0xFF000000u;
            uint8_t *png; size_t pn;
            if (png_encode(pix, pw, ph, &png, &pn) == 0) {
                FILE *fp = fopen(c->selftest_shot, "wb");
                if (fp) { fwrite(png, 1, pn, fp); fclose(fp); }
                free(png);
            }
            printf("window %dx%d px, book %dx%d at (%d,%d), %s\n", pw, ph, r.w, r.h, r.x, r.y,
                   r.w % BOOK_SPREAD_W == 0 ? "whole-number scale" : "filtered scale");
        } else fprintf(stderr, "SDL_RenderReadPixels: %s\n", SDL_GetError());
        free(pix);
        c->shot_done = true;
        return;
    }
    SDL_RenderPresent(c->ren);

    char title[300];
    app_window_title(c->app, title, sizeof title);
    if (strcmp(title, c->last_title) != 0) { SDL_SetWindowTitle(c->win, title); strcpy(c->last_title, title); }
}

#ifdef __APPLE__
/* macOS runs a modal loop while a window edge is being dragged, so the main
 * loop is starved; repaint from here to keep the book scaling live. */
static int live_resize_watch(void *ud, SDL_Event *e)
{
    if (e->type == SDL_WINDOWEVENT && e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        Ctx *c = ud;
        if (!c->selftest_shot) draw_frame(c);
    }
    return 0;
}
#endif

/* Keep the window book-shaped when the window manager lets the user drag it
 * to any size (macOS does this natively; X11/Wayland need the nudge). */
static void snap_aspect(SDL_Window *win, int w, int h, int *prev_w, int *prev_h)
{
    Uint32 fl = SDL_GetWindowFlags(win);
    if (!(fl & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_MAXIMIZED)) &&
        labs((long)w * BOOK_SPREAD_H - (long)h * BOOK_SPREAD_W) > 2L * BOOK_SPREAD_W) {
        double rw = *prev_w > 0 ? fabs((double)(w - *prev_w)) / *prev_w : 1.0;
        double rh = *prev_h > 0 ? fabs((double)(h - *prev_h)) / *prev_h : 1.0;
        int nw, nh;
        if (rw >= rh) { nw = w; nh = (w * BOOK_SPREAD_H + BOOK_SPREAD_W / 2) / BOOK_SPREAD_W; }
        else          { nh = h; nw = (h * BOOK_SPREAD_W + BOOK_SPREAD_H / 2) / BOOK_SPREAD_H; }
        if (nw < BOOK_SPREAD_W) { nw = BOOK_SPREAD_W; nh = BOOK_SPREAD_H; }
        SDL_SetWindowSize(win, nw, nh);
        w = nw; h = nh;
    }
    *prev_w = w; *prev_h = h;
}

static void usage(const char *argv0)
{
    printf("NoteCraft %s - a Minecraft-style Book and Quill for your desktop\n\n"
           "usage: %s [options] [file.txt|file.epub]\n"
           "  --scale N     initial window size as a multiple of the book (default: fits your screen)\n"
           "                drag the window edges/corners, or Ctrl +/- / Ctrl 0, to resize while running\n"
           "  --version     print the version\n", NOTECRAFT_VERSION, argv0);
}

int main(int argc, char **argv)
{
    const char *file = NULL, *pack = getenv("NOTECRAFT_PACK");
    int forced_scale = 0, test_w = 0, test_h = 0;
    const char *selftest_shot = NULL;   /* hidden: render one frame via SDL, dump the window pixels, exit */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) forced_scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--selftest-shot") && i + 1 < argc) selftest_shot = argv[++i];
        else if (!strcmp(argv[i], "--selftest-size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &test_w, &test_h);
        else if (!strcmp(argv[i], "--version")) { printf("NoteCraft %s\n", NOTECRAFT_VERSION); return 0; }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(argv[0]); return 0; }
        else if (argv[i][0] != '-') file = argv[i];
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }

    /* Start modestly: a whole-number multiple of the book that takes about 60% of the screen height. */
    int init_w = BOOK_SPREAD_W * 2, init_h = BOOK_SPREAD_H * 2, default_k = 2;
    {
        SDL_Rect ub;
        int k = 2;
        if (SDL_GetDisplayUsableBounds(0, &ub) == 0) {
            k = (int)(0.6f * (float)ub.h / BOOK_SPREAD_H);
            int kw = (int)(0.7f * (float)ub.w / BOOK_SPREAD_W);
            if (kw < k) k = kw;
        }
        if (k > 4) k = 4;
        if (k < 1) k = 1;
        default_k = k;
        if (forced_scale > 0) k = forced_scale;
        init_w = BOOK_SPREAD_W * k; init_h = BOOK_SPREAD_H * k;
        if (test_w > 0 && test_h > 0) { init_w = test_w; init_h = test_h; }
    }
    SDL_Window *win = SDL_CreateWindow("NoteCraft", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       init_w, init_h, SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_SetWindowMinimumSize(win, BOOK_SPREAD_W, BOOK_SPREAD_H);
#ifdef __APPLE__
    lock_aspect_native(win);
#endif
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }

#ifdef NC_HAVE_ICON
    {
        SDL_Surface *ic = SDL_CreateRGBSurfaceWithFormatFrom((void *)icon_pixels, ICON_W, ICON_H, 32, ICON_W * 4, SDL_PIXELFORMAT_RGBA32);
        if (ic) { SDL_SetWindowIcon(win, ic); SDL_FreeSurface(ic); }
    }
#endif

    char packbuf[1024] = "";
    if (!pack || !*pack) {
        const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
        if (xdg && *xdg) snprintf(packbuf, sizeof packbuf, "%s/notecraft/pack", xdg);
        else if (home) snprintf(packbuf, sizeof packbuf, "%s/.config/notecraft/pack", home);
        pack = packbuf;
    }

    AppHost host = {host_get_clipboard, host_set_clipboard, NULL};
    App *app = app_new(&host, pack);
    if (!app) { fprintf(stderr, "out of memory\n"); return 1; }
    if (file) app_open_file(app, file);
    else app_toast(app, "Esc: menu   Ctrl+O: open   Ctrl+S: save");
    app_set_size(app, BOOK_SPREAD_W, BOOK_SPREAD_H);

    Ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.app = app; ctx.win = win; ctx.ren = ren; ctx.selftest_shot = selftest_shot;
    ctx.fb = fb_new(BOOK_SPREAD_W, BOOK_SPREAD_H);
    ctx.tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, BOOK_SPREAD_W, BOOK_SPREAD_H);
    {
        SDL_RendererInfo ri;
        ctx.max_tex = 4096;
        if (SDL_GetRendererInfo(ren, &ri) == 0 && ri.max_texture_width > 0 && ri.max_texture_height > 0)
            ctx.max_tex = ri.max_texture_width < ri.max_texture_height ? ri.max_texture_width : ri.max_texture_height;
    }
    if (!ctx.fb || !ctx.tex) { fprintf(stderr, "out of memory\n"); return 1; }
#ifdef __APPLE__
    SDL_AddEventWatch(live_resize_watch, &ctx);
#endif

    int prev_w = 0, prev_h = 0;
    SDL_GetWindowSize(win, &prev_w, &prev_h);
    SDL_StartTextInput();

    bool running = true;
    while (running && !ctx.shot_done) {
        SDL_Event e;
        int timeout = app_needs_redraw(app) ? 0 : 40;
        int have = SDL_WaitEventTimeout(&e, timeout);
        while (have) {
            AppEvent ev; memset(&ev, 0, sizeof ev);
            int winw, winh, pw = 0, ph = 0;
            SDL_GetWindowSize(win, &winw, &winh);
            SDL_GetRendererOutputSize(ren, &pw, &ph);
            SDL_Rect fr;
            fit_rect(pw > 0 ? pw : 1, ph > 0 ? ph : 1, &fr);
            float fx = winw ? (float)pw / (float)winw : 1.f, fy = winh ? (float)ph / (float)winh : 1.f;
            switch (e.type) {
            case SDL_QUIT:
                ev.type = EV_KEY; ev.key = 'q'; ev.mods = MOD_CTRL; app_event(app, &ev);
                break;
            case SDL_TEXTINPUT: {
                const char *s = e.text.text;
                size_t n = strlen(s), i = 0;
                while (i < n) {
                    uint32_t cp;
                    i += (size_t)u8_decode(s + i, n - i, &cp);
                    ev.type = EV_TEXT; ev.cp = cp;
                    app_event(app, &ev);
                }
                break;
            }
            case SDL_KEYDOWN: {
                SDL_Keycode sym = e.key.keysym.sym;
                bool plus = sym == SDLK_EQUALS || sym == SDLK_PLUS || sym == SDLK_KP_PLUS;
                bool minus = sym == SDLK_MINUS || sym == SDLK_KP_MINUS;
                bool zero = sym == SDLK_0 || sym == SDLK_KP_0;
                if ((e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) && (plus || minus || zero) &&
                    !(SDL_GetWindowFlags(win) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP))) {
                    /* Step the window size by half a book-width, or go back to the default. */
                    double cur = (double)winw / BOOK_SPREAD_W, ns;
                    if (zero) ns = default_k;
                    else if (plus) ns = floor(cur * 2 + 0.001) / 2 + 0.5;
                    else ns = ceil(cur * 2 - 0.001) / 2 - 0.5;
                    SDL_Rect ub;
                    if (SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(win), &ub) == 0) {
                        double mx = (double)ub.w / BOOK_SPREAD_W, my = (double)ub.h / BOOK_SPREAD_H;
                        if (my < mx) mx = my;
                        if (ns > mx) ns = floor(mx * 2) / 2;
                    }
                    if (ns < 1) ns = 1;
                    SDL_SetWindowSize(win, (int)lround(BOOK_SPREAD_W * ns), (int)lround(BOOK_SPREAD_H * ns));
                    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                    ev.type = EV_NONE; app_event(app, &ev);
                    break;
                }
                int k = map_key(&e.key);
                if (k) { ev.type = EV_KEY; ev.key = k; ev.mods = map_mods(SDL_GetModState()); app_event(app, &ev); }
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                ev.type = e.type == SDL_MOUSEBUTTONDOWN ? EV_MOUSE_DOWN : EV_MOUSE_UP;
                ev.x = (int)floorf(((float)e.button.x * fx - (float)fr.x) * BOOK_SPREAD_W / (float)fr.w);
                ev.y = (int)floorf(((float)e.button.y * fy - (float)fr.y) * BOOK_SPREAD_H / (float)fr.h);
                ev.button = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button;
                ev.clicks = e.button.clicks; ev.mods = map_mods(SDL_GetModState());
                app_event(app, &ev);
                break;
            case SDL_MOUSEMOTION:
                ev.type = EV_MOUSE_MOVE;
                ev.x = (int)floorf(((float)e.motion.x * fx - (float)fr.x) * BOOK_SPREAD_W / (float)fr.w);
                ev.y = (int)floorf(((float)e.motion.y * fy - (float)fr.y) * BOOK_SPREAD_H / (float)fr.h);
                app_event(app, &ev);
                break;
            case SDL_MOUSEWHEEL:
                ev.type = EV_WHEEL; ev.wheel = e.wheel.y > 0 ? 1 : e.wheel.y < 0 ? -1 : 0;
                if (ev.wheel) app_event(app, &ev);
                break;
            case SDL_DROPFILE:
                ev.type = EV_DROP; ev.path = e.drop.file;
                app_event(app, &ev);
                SDL_free(e.drop.file);
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    snap_aspect(win, e.window.data1, e.window.data2, &prev_w, &prev_h);
                if (e.window.event == SDL_WINDOWEVENT_EXPOSED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED
                    || e.window.event == SDL_WINDOWEVENT_RESTORED) { ev.type = EV_NONE; app_event(app, &ev); }
                break;
            default: break;
            }
            have = SDL_PollEvent(&e);
        }

        app_update(app, SDL_GetTicks());
        if (app_wants_quit(app)) break;
        if (app_take_fullscreen_toggle(app)) {
            bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
            SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        }

        if (app_needs_redraw(app)) draw_frame(&ctx);
    }

#ifdef __APPLE__
    SDL_DelEventWatch(live_resize_watch, &ctx);
#endif
    app_free(app);
    fb_free(ctx.fb);
    if (ctx.tex) SDL_DestroyTexture(ctx.tex);
    if (ctx.tgt) SDL_DestroyTexture(ctx.tgt);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
