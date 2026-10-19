/*
 * SDL2 implementation for Banana OS.
 * Maps SDL2 video, events, timer, mouse, audio to Banana OS SDK (bwin, banana_api).
 */
#include "SDL2/SDL.h"
#include <banana.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SDL_Window {
    bwin_t bwin;
    Uint32 id;
    Uint32 flags;
    char   title[128];
    SDL_Surface* surface;
};

static char g_last_error[256] = "";
static Uint32 g_initialized_subsystems = 0;
static SDL_Window* g_main_window = NULL;
static SDL_bool g_relative_mouse = SDL_FALSE;
static int g_last_mouse_x = 0;
static int g_last_mouse_y = 0;
static Uint32 g_last_mouse_buttons = 0;
static int g_rel_dx = 0;
static int g_rel_dy = 0;

/* Error handling */
const char* SDL_GetError(void) {
    return g_last_error;
}

void SDL_SetError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, ap);
    va_end(ap);
}

void SDL_ClearError(void) {
    g_last_error[0] = '\0';
}

/* Subsystem Init / Quit */
int SDL_Init(Uint32 flags) {
    return SDL_InitSubSystem(flags);
}

int SDL_InitSubSystem(Uint32 flags) {
    g_initialized_subsystems |= flags;
    return 0;
}

void SDL_QuitSubSystem(Uint32 flags) {
    g_initialized_subsystems &= ~flags;
}

Uint32 SDL_WasInit(Uint32 flags) {
    if (flags == 0) return g_initialized_subsystems;
    return g_initialized_subsystems & flags;
}

void SDL_Quit(void) {
    if (g_main_window) {
        SDL_DestroyWindow(g_main_window);
        g_main_window = NULL;
    }
    SDL_CloseAudio();
    g_initialized_subsystems = 0;
    SDL_ClearError();
}

/* Timer */
Uint32 SDL_GetTicks(void) {
    return (Uint32)banana_ticks();
}

Uint64 SDL_GetTicks64(void) {
    return (Uint64)banana_ticks();
}

void SDL_Delay(Uint32 ms) {
    banana_sleep(ms);
}

/* Pixel formats & Surfaces */
static SDL_PixelFormat g_format_argb8888 = {
    .format = SDL_PIXELFORMAT_ARGB8888,
    .palette = NULL,
    .BitsPerPixel = 32,
    .BytesPerPixel = 4,
    .Rmask = 0x00FF0000u,
    .Gmask = 0x0000FF00u,
    .Bmask = 0x000000FFu,
    .Amask = 0xFF000000u,
    .refcount = 1
};

SDL_Surface* SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth,
                                  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask) {
    (void)flags; (void)depth; (void)Rmask; (void)Gmask; (void)Bmask; (void)Amask;
    if (w <= 0 || h <= 0) {
        SDL_SetError("Invalid dimensions for surface");
        return NULL;
    }
    SDL_Surface* s = (SDL_Surface*)calloc(1, sizeof(SDL_Surface));
    if (!s) {
        SDL_SetError("Out of memory");
        return NULL;
    }
    s->w = w;
    s->h = h;
    s->pitch = w * 4;
    s->pixels = malloc((size_t)w * (size_t)h * 4);
    if (!s->pixels) {
        free(s);
        SDL_SetError("Out of memory allocating pixels");
        return NULL;
    }
    memset(s->pixels, 0, (size_t)w * (size_t)h * 4);
    s->format = &g_format_argb8888;
    s->clip_rect.x = 0;
    s->clip_rect.y = 0;
    s->clip_rect.w = w;
    s->clip_rect.h = h;
    s->refcount = 1;
    return s;
}

void SDL_FreeSurface(SDL_Surface* surface) {
    if (!surface) return;
    if (--surface->refcount <= 0) {
        if (surface->pixels && !(surface->flags & 0x00000001u /* don't free window px */)) {
            free(surface->pixels);
        }
        free(surface);
    }
}

int SDL_FillRect(SDL_Surface* dst, const SDL_Rect* rect, Uint32 color) {
    if (!dst || !dst->pixels) return -1;
    int rx = 0, ry = 0, rw = dst->w, rh = dst->h;
    if (rect) {
        rx = rect->x;
        ry = rect->y;
        rw = rect->w;
        rh = rect->h;
    }
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > dst->w) rw = dst->w - rx;
    if (ry + rh > dst->h) rh = dst->h - ry;
    if (rw <= 0 || rh <= 0) return 0;

    Uint32* px = (Uint32*)dst->pixels;
    int stride = dst->pitch / 4;
    for (int y = ry; y < ry + rh; y++) {
        Uint32* row = px + (y * stride) + rx;
        for (int x = 0; x < rw; x++) {
            row[x] = color;
        }
    }
    return 0;
}

int SDL_BlitSurface(SDL_Surface* src, const SDL_Rect* srcrect, SDL_Surface* dst, SDL_Rect* dstrect) {
    if (!src || !dst || !src->pixels || !dst->pixels) return -1;
    int sx = 0, sy = 0, sw = src->w, sh = src->h;
    if (srcrect) {
        sx = srcrect->x; sy = srcrect->y; sw = srcrect->w; sh = srcrect->h;
    }
    int dx = dstrect ? dstrect->x : 0;
    int dy = dstrect ? dstrect->y : 0;

    if (sx < 0) { sw += sx; dx -= sx; sx = 0; }
    if (sy < 0) { sh += sy; dy -= sy; sy = 0; }
    if (dx < 0) { sw += dx; sx -= dx; dx = 0; }
    if (dy < 0) { sh += dy; sy -= dy; dy = 0; }
    if (sx + sw > src->w) sw = src->w - sx;
    if (sy + sh > src->h) sh = src->h - sy;
    if (dx + sw > dst->w) sw = dst->w - dx;
    if (dy + sh > dst->h) sh = dst->h - dy;
    if (sw <= 0 || sh <= 0) return 0;

    const Uint32* spx = (const Uint32*)src->pixels;
    Uint32* dpx = (Uint32*)dst->pixels;
    int sstride = src->pitch / 4;
    int dstride = dst->pitch / 4;

    for (int y = 0; y < sh; y++) {
        const Uint32* srow = spx + ((sy + y) * sstride) + sx;
        Uint32* drow = dpx + ((dy + y) * dstride) + dx;
        memcpy(drow, srow, (size_t)sw * sizeof(Uint32));
    }
    return 0;
}

/* Windows */
SDL_Window* SDL_CreateWindow(const char* title, int x, int y, int w, int h, Uint32 flags) {
    (void)x; (void)y;
    SDL_Window* win = (SDL_Window*)calloc(1, sizeof(SDL_Window));
    if (!win) {
        SDL_SetError("Out of memory allocating window");
        return NULL;
    }
    if (title) snprintf(win->title, sizeof(win->title), "%s", title);
    win->flags = flags | SDL_WINDOW_SHOWN;
    if (bwin_open(&win->bwin, win->title, w, h) < 0) {
        free(win);
        SDL_SetError("Failed to open Banana OS window (bwin_open)");
        return NULL;
    }
    win->id = (Uint32)win->bwin.id;

    if (flags & SDL_WINDOW_RESIZABLE) {
        bwin_resizable(&win->bwin, 100, 100);
    }

    /* Wrap client pixels in surface */
    win->surface = (SDL_Surface*)calloc(1, sizeof(SDL_Surface));
    if (win->surface) {
        win->surface->flags = 0x00000001u; /* do not free win->bwin.px */
        win->surface->w = win->bwin.w;
        win->surface->h = win->bwin.h;
        win->surface->pitch = win->bwin.w * 4;
        win->surface->pixels = win->bwin.px;
        win->surface->format = &g_format_argb8888;
        win->surface->clip_rect.x = 0;
        win->surface->clip_rect.y = 0;
        win->surface->clip_rect.w = win->bwin.w;
        win->surface->clip_rect.h = win->bwin.h;
        win->surface->refcount = 1;
    }

    g_main_window = win;
    return win;
}

void SDL_DestroyWindow(SDL_Window* window) {
    if (!window) return;
    if (g_relative_mouse) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
    }
    bwin_close(&window->bwin);
    if (window->surface) {
        free(window->surface);
        window->surface = NULL;
    }
    if (g_main_window == window) g_main_window = NULL;
    free(window);
}

Uint32 SDL_GetWindowID(SDL_Window* window) {
    return window ? window->id : 0;
}

SDL_Window* SDL_GetWindowFromID(Uint32 id) {
    if (g_main_window && g_main_window->id == id) return g_main_window;
    return NULL;
}

Uint32 SDL_GetWindowFlags(SDL_Window* window) {
    return window ? window->flags : 0;
}

void SDL_SetWindowTitle(SDL_Window* window, const char* title) {
    if (!window || !title) return;
    snprintf(window->title, sizeof(window->title), "%s", title);
    bwin_title(&window->bwin, window->title);
}

const char* SDL_GetWindowTitle(SDL_Window* window) {
    return window ? window->title : "";
}

void SDL_SetWindowSize(SDL_Window* window, int w, int h) {
    (void)window; (void)w; (void)h;
}

void SDL_GetWindowSize(SDL_Window* window, int* w, int* h) {
    if (!window) return;
    if (w) *w = window->bwin.w;
    if (h) *h = window->bwin.h;
}

void SDL_ShowWindow(SDL_Window* window) {
    if (window) window->flags |= SDL_WINDOW_SHOWN;
}

void SDL_HideWindow(SDL_Window* window) {
    if (window) window->flags &= ~SDL_WINDOW_SHOWN;
}

SDL_Surface* SDL_GetWindowSurface(SDL_Window* window) {
    if (!window) return NULL;
    if (window->surface) {
        window->surface->w = window->bwin.w;
        window->surface->h = window->bwin.h;
        window->surface->pitch = window->bwin.w * 4;
        window->surface->pixels = window->bwin.px;
    }
    return window->surface;
}

int SDL_UpdateWindowSurface(SDL_Window* window) {
    if (!window) return -1;
    bwin_update(&window->bwin);
    return 0;
}

int SDL_UpdateWindowSurfaceRects(SDL_Window* window, const SDL_Rect* rects, int numrects) {
    (void)rects; (void)numrects;
    return SDL_UpdateWindowSurface(window);
}

/* Mouse and Relative capture */
int SDL_SetRelativeMouseMode(SDL_bool enabled) {
    if (!g_main_window) return -1;
    g_relative_mouse = enabled;
    bwin_capture_mouse(&g_main_window->bwin, enabled ? 1 : 0);
    g_rel_dx = 0;
    g_rel_dy = 0;
    return 0;
}

SDL_bool SDL_GetRelativeMouseMode(void) {
    return g_relative_mouse;
}

Uint32 SDL_GetMouseState(int* x, int* y) {
    if (x) *x = g_last_mouse_x;
    if (y) *y = g_last_mouse_y;
    return g_last_mouse_buttons;
}

Uint32 SDL_GetRelativeMouseState(int* x, int* y) {
    if (x) *x = g_rel_dx;
    if (y) *y = g_rel_dy;
    g_rel_dx = 0;
    g_rel_dy = 0;
    return g_last_mouse_buttons;
}

/* Scancode & Keycode translation */
static SDL_Scancode translate_key_scancode(int key) {
    if (key >= 'a' && key <= 'z') return (SDL_Scancode)(SDL_SCANCODE_A + (key - 'a'));
    if (key >= 'A' && key <= 'Z') return (SDL_Scancode)(SDL_SCANCODE_A + (key - 'A'));
    if (key >= '1' && key <= '9') return (SDL_Scancode)(SDL_SCANCODE_1 + (key - '1'));
    if (key == '0') return SDL_SCANCODE_0;
    switch (key) {
    case 27: return SDL_SCANCODE_ESCAPE;
    case '\r': case '\n': return SDL_SCANCODE_RETURN;
    case '\b': return SDL_SCANCODE_BACKSPACE;
    case '\t': return SDL_SCANCODE_TAB;
    case ' ': return SDL_SCANCODE_SPACE;
    case BANANA_KEY_UP: return SDL_SCANCODE_UP;
    case BANANA_KEY_DOWN: return SDL_SCANCODE_DOWN;
    case BANANA_KEY_LEFT: return SDL_SCANCODE_LEFT;
    case BANANA_KEY_RIGHT: return SDL_SCANCODE_RIGHT;
    case BANANA_KEY_SHIFT: return SDL_SCANCODE_LSHIFT;
    case BANANA_KEY_CTRL: return SDL_SCANCODE_LCTRL;
    case BANANA_KEY_ALT: return SDL_SCANCODE_LALT;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}

static SDL_Keycode translate_key_keycode(int key) {
    if (key >= 'A' && key <= 'Z') return (SDL_Keycode)(key - 'A' + 'a');
    switch (key) {
    case BANANA_KEY_UP: return SDLK_UP;
    case BANANA_KEY_DOWN: return SDLK_DOWN;
    case BANANA_KEY_LEFT: return SDLK_LEFT;
    case BANANA_KEY_RIGHT: return SDLK_RIGHT;
    default: return (SDL_Keycode)key;
    }
}

/* Event processing */
static int translate_banana_event(SDL_Window* win, const banana_event_t* bev, SDL_Event* ev) {
    memset(ev, 0, sizeof(SDL_Event));
    ev->common.timestamp = SDL_GetTicks();

    switch (bev->type) {
    case BANANA_EV_CLOSE:
        ev->type = SDL_QUIT;
        return 1;

    case BANANA_EV_RESIZE:
        ev->type = SDL_WINDOWEVENT;
        ev->window.windowID = win->id;
        ev->window.event = SDL_WINDOWEVENT_RESIZED;
        ev->window.data1 = win->bwin.w;
        ev->window.data2 = win->bwin.h;
        if (win->surface) {
            win->surface->w = win->bwin.w;
            win->surface->h = win->bwin.h;
            win->surface->pitch = win->bwin.w * 4;
            win->surface->pixels = win->bwin.px;
        }
        return 1;

    case BANANA_EV_FOCUS:
        ev->type = SDL_WINDOWEVENT;
        ev->window.windowID = win->id;
        ev->window.event = bev->x ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
        return 1;

    case BANANA_EV_KEY:
        ev->type = SDL_KEYDOWN;
        ev->key.windowID = win->id;
        ev->key.state = SDL_PRESSED;
        ev->key.keysym.scancode = translate_key_scancode(bev->key);
        ev->key.keysym.sym = translate_key_keycode(bev->key);
        return 1;

    case BANANA_EV_MOUSE_DOWN:
        ev->type = SDL_MOUSEBUTTONDOWN;
        ev->button.windowID = win->id;
        ev->button.button = (Uint8)bev->button;
        ev->button.state = SDL_PRESSED;
        ev->button.clicks = 1;
        ev->button.x = bev->x;
        ev->button.y = bev->y;
        g_last_mouse_x = bev->x;
        g_last_mouse_y = bev->y;
        if (bev->button == 1) g_last_mouse_buttons |= SDL_BUTTON_LMASK;
        if (bev->button == 2) g_last_mouse_buttons |= SDL_BUTTON_RMASK;
        if (bev->button == 3) g_last_mouse_buttons |= SDL_BUTTON_MMASK;
        return 1;

    case BANANA_EV_MOUSE_UP:
        ev->type = SDL_MOUSEBUTTONUP;
        ev->button.windowID = win->id;
        ev->button.button = (Uint8)bev->button;
        ev->button.state = SDL_RELEASED;
        ev->button.clicks = 1;
        ev->button.x = bev->x;
        ev->button.y = bev->y;
        g_last_mouse_x = bev->x;
        g_last_mouse_y = bev->y;
        if (bev->button == 1) g_last_mouse_buttons &= ~SDL_BUTTON_LMASK;
        if (bev->button == 2) g_last_mouse_buttons &= ~SDL_BUTTON_RMASK;
        if (bev->button == 3) g_last_mouse_buttons &= ~SDL_BUTTON_MMASK;
        return 1;

    case BANANA_EV_MOUSE_MOVE:
        ev->type = SDL_MOUSEMOTION;
        ev->motion.windowID = win->id;
        ev->motion.state = g_last_mouse_buttons;
        ev->motion.x = bev->x;
        ev->motion.y = bev->y;
        ev->motion.xrel = bev->x - g_last_mouse_x;
        ev->motion.yrel = bev->y - g_last_mouse_y;
        g_last_mouse_x = bev->x;
        g_last_mouse_y = bev->y;
        return 1;

    case BANANA_EV_MOUSE_REL:
        ev->type = SDL_MOUSEMOTION;
        ev->motion.windowID = win->id;
        ev->motion.state = g_last_mouse_buttons;
        ev->motion.x = g_last_mouse_x;
        ev->motion.y = g_last_mouse_y;
        ev->motion.xrel = bev->x;
        ev->motion.yrel = bev->y;
        g_rel_dx += bev->x;
        g_rel_dy += bev->y;
        return 1;

    case BANANA_EV_WHEEL:
        ev->type = SDL_MOUSEWHEEL;
        ev->wheel.windowID = win->id;
        ev->wheel.y = bev->y;
        return 1;

    default:
        return 0;
    }
}

void SDL_PumpEvents(void) {
    /* Handled on demand in PollEvent */
}

int SDL_PollEvent(SDL_Event* event) {
    if (!g_main_window) return 0;
    banana_event_t bev;
    while (bwin_event(&g_main_window->bwin, &bev)) {
        if (translate_banana_event(g_main_window, &bev, event)) {
            return 1;
        }
    }
    return 0;
}

int SDL_WaitEvent(SDL_Event* event) {
    return SDL_WaitEventTimeout(event, -1);
}

int SDL_WaitEventTimeout(SDL_Event* event, int timeout) {
    if (!g_main_window) return 0;
    banana_event_t bev;
    while (bwin_wait_event(&g_main_window->bwin, &bev, timeout)) {
        if (translate_banana_event(g_main_window, &bev, event)) {
            return 1;
        }
    }
    return 0;
}

/* Audio Subsystem */
static SDL_AudioSpec g_audio_spec;
static int g_audio_paused = 1;
static int g_audio_open = 0;

int SDL_OpenAudio(SDL_AudioSpec* desired, SDL_AudioSpec* obtained) {
    if (!desired) {
        SDL_SetError("NULL audio spec");
        return -1;
    }
    memcpy(&g_audio_spec, desired, sizeof(SDL_AudioSpec));
    if (obtained) {
        memcpy(obtained, desired, sizeof(SDL_AudioSpec));
    }
    g_audio_paused = 1;
    g_audio_open = 1;
    return 0;
}

void SDL_CloseAudio(void) {
    if (g_audio_open) {
        const banana_api_t* api = banana_api();
        if (api && BANANA_HAS(api, audio_stop)) {
            api->audio_stop();
        }
        g_audio_open = 0;
        g_audio_paused = 1;
    }
}

void SDL_PauseAudio(int pause_on) {
    g_audio_paused = pause_on;
    if (pause_on) {
        const banana_api_t* api = banana_api();
        if (api && BANANA_HAS(api, audio_stop)) {
            api->audio_stop();
        }
    }
}

void SDL_LockAudio(void) {}
void SDL_UnlockAudio(void) {}
