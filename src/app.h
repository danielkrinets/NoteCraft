/* The whole application, independent of any windowing library: feed it
 * events, call app_render() to get pixels. platform_sdl.c drives it in a
 * window, demo.c drives it headless for screenshots and the demo GIF. */
#ifndef NC_APP_H
#define NC_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "book.h"
#include "gfx.h"

enum {
    KEY_NONE = 0,
    KEY_BACKSPACE = 256, KEY_DELETE, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
    KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_ENTER, KEY_ESC, KEY_TAB,
    KEY_F2, KEY_F11,
    /* letters use their lowercase ASCII code: 'a'..'z' */
};

enum { MOD_SHIFT = 1, MOD_CTRL = 2, MOD_ALT = 4 };

typedef enum {
    EV_NONE, EV_TEXT, EV_KEY, EV_MOUSE_DOWN, EV_MOUSE_UP, EV_MOUSE_MOVE, EV_WHEEL, EV_DROP
} EvType;

typedef struct {
    EvType type;
    uint32_t cp;           /* EV_TEXT */
    int key, mods;         /* EV_KEY */
    int x, y, button;      /* mouse, logical coordinates; button 1 = left */
    int clicks;            /* click count for EV_MOUSE_DOWN */
    int wheel;             /* EV_WHEEL: +1 up / -1 down */
    const char *path;      /* EV_DROP */
} AppEvent;

typedef struct {
    char *(*get_clipboard)(void *ud);          /* malloc'd UTF-8 or NULL */
    void  (*set_clipboard)(void *ud, const char *utf8);
    void *ud;
} AppHost;

typedef struct App App;

App  *app_new(const AppHost *host, const char *pack_dir);
void  app_free(App *a);
void  app_set_size(App *a, int logical_w, int logical_h);
void  app_event(App *a, const AppEvent *e);
void  app_update(App *a, uint32_t now_ms);
void  app_render(App *a, Fb *fb);
bool  app_needs_redraw(const App *a);

bool  app_open_file(App *a, const char *path);       /* load txt/epub; false on error */
void  app_toast(App *a, const char *msg);
void  app_set_start_dir(App *a, const char *dir);

/* Requests for the platform layer. */
bool  app_wants_quit(const App *a);
bool  app_take_fullscreen_toggle(App *a);
bool  app_take_screenshot_request(App *a);
void  app_window_title(const App *a, char *out, int cap);

/* Introspection for tests / demo driver. */
Book *app_book(App *a);
int   app_page(const App *a);
const char *app_screen_name(const App *a);
bool  app_dirty(const App *a);

#endif
