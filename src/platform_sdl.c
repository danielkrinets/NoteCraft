/* SDL2 front end: window, input, clipboard, integer GUI scaling. */
#include <SDL.h>
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

/* Vanilla "auto" GUI scale: the largest integer scale that still leaves a
 * logical screen of at least 240x240 (book + buttons + a little air). */
static int auto_scale(int pw, int ph)
{
    int s = 1;
    while (pw / (s + 1) >= BOOK_SPREAD_W && ph / (s + 1) >= BOOK_SPREAD_H) s++;
    return s;
}

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

static void usage(const char *argv0)
{
    printf("NoteCraft %s - a Minecraft-style Book and Quill for your desktop\n\n"
           "usage: %s [options] [file.txt|file.epub]\n"
           "  --scale N     window size as a multiple of the book (default: fits your screen); Ctrl +/- while running\n"
           "  --version     print the version\n", NOTECRAFT_VERSION, argv0);
}

int main(int argc, char **argv)
{
    const char *file = NULL, *pack = getenv("NOTECRAFT_PACK");
    int forced_scale = 0;
    const char *selftest_shot = NULL;   /* hidden: render one frame via SDL, dump the window pixels, exit */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) forced_scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--selftest-shot") && i + 1 < argc) selftest_shot = argv[++i];
        else if (!strcmp(argv[i], "--version")) { printf("NoteCraft %s\n", NOTECRAFT_VERSION); return 0; }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(argv[0]); return 0; }
        else if (argv[i][0] != '-') file = argv[i];
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }

    /* The window is exactly the open book, scaled by a whole number (Ctrl +/- to change). */
    int win_k = 3;
    {
        SDL_Rect ub;
        if (SDL_GetDisplayUsableBounds(0, &ub) == 0) {
            win_k = (int)(0.85f * (float)ub.h / BOOK_SPREAD_H);
            int kw = (int)(0.92f * (float)ub.w / BOOK_SPREAD_W);
            if (kw < win_k) win_k = kw;
        }
        if (win_k > 4) win_k = 4;
        if (win_k < 1) win_k = 1;
        if (forced_scale > 0) win_k = forced_scale;
    }
    SDL_Window *win = SDL_CreateWindow("NoteCraft", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       BOOK_SPREAD_W * win_k, BOOK_SPREAD_H * win_k, SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
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

    Fb *fb = fb_new(BOOK_SPREAD_W, BOOK_SPREAD_H);
    SDL_Texture *tex = NULL;
    int tex_w = 0, tex_h = 0, scale = 1, pw = 0, ph = 0;
    char last_title[300] = "";
    SDL_StartTextInput();

    bool running = true;
    while (running) {
        SDL_Event e;
        int timeout = app_needs_redraw(app) ? 0 : 40;
        int have = SDL_WaitEventTimeout(&e, timeout);
        while (have) {
            AppEvent ev; memset(&ev, 0, sizeof ev);
            int winw, winh;
            SDL_GetWindowSize(win, &winw, &winh);
            SDL_GetRendererOutputSize(ren, &pw, &ph);
            scale = auto_scale(pw, ph);
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
                if ((e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) &&
                    (sym == SDLK_EQUALS || sym == SDLK_PLUS || sym == SDLK_KP_PLUS || sym == SDLK_MINUS || sym == SDLK_KP_MINUS)) {
                    win_k += (sym == SDLK_MINUS || sym == SDLK_KP_MINUS) ? -1 : 1;
                    if (win_k < 1) win_k = 1;
                    if (win_k > 8) win_k = 8;
                    SDL_SetWindowSize(win, BOOK_SPREAD_W * win_k, BOOK_SPREAD_H * win_k);
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
                ev.x = (int)((float)e.button.x * fx) / scale; ev.y = (int)((float)e.button.y * fy) / scale;
                ev.button = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button;
                ev.clicks = e.button.clicks; ev.mods = map_mods(SDL_GetModState());
                app_event(app, &ev);
                break;
            case SDL_MOUSEMOTION:
                ev.type = EV_MOUSE_MOVE;
                ev.x = (int)((float)e.motion.x * fx) / scale; ev.y = (int)((float)e.motion.y * fy) / scale;
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

        if (app_needs_redraw(app)) {
            SDL_GetRendererOutputSize(ren, &pw, &ph);
            scale = auto_scale(pw, ph);
            int lw = (pw + scale - 1) / scale, lh = (ph + scale - 1) / scale;
            if (lw < 1) lw = 1;
            if (lh < 1) lh = 1;
            if (fb_resize(fb, lw, lh) != 0) break;
            app_set_size(app, lw, lh);
            if (!tex || tex_w != lw || tex_h != lh) {
                if (tex) SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, lw, lh);
                tex_w = lw; tex_h = lh;
            }
            app_render(app, fb);
            if (app_take_screenshot_request(app)) {
                save_screenshot(app, fb, scale);
                app_render(app, fb);
            }
            SDL_UpdateTexture(tex, NULL, fb->px, lw * 4);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            SDL_Rect dst = {0, 0, lw * scale, lh * scale};
            SDL_RenderCopy(ren, tex, NULL, &dst);
            if (selftest_shot) {
                uint32_t *pix = malloc((size_t)pw * (size_t)ph * 4);
                if (pix && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, pix, pw * 4) == 0) {
                    for (size_t k = 0; k < (size_t)pw * (size_t)ph; k++) pix[k] |= 0xFF000000u;
                    uint8_t *png; size_t pn;
                    if (png_encode(pix, pw, ph, &png, &pn) == 0) {
                        FILE *fp = fopen(selftest_shot, "wb");
                        if (fp) { fwrite(png, 1, pn, fp); fclose(fp); }
                        free(png);
                    }
                    printf("window %dx%d px, gui scale %d, logical %dx%d\n", pw, ph, scale, lw, lh);
                } else fprintf(stderr, "SDL_RenderReadPixels: %s\n", SDL_GetError());
                free(pix);
                break;
            }
            SDL_RenderPresent(ren);

            char title[300];
            app_window_title(app, title, sizeof title);
            if (strcmp(title, last_title) != 0) { SDL_SetWindowTitle(win, title); strcpy(last_title, title); }
        }
    }

    app_free(app);
    fb_free(fb);
    if (tex) SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
