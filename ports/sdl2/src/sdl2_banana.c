/*
 * SDL2 implementation for Banana OS.
 * Maps SDL2 video, events, timer, mouse, keyboard, audio, threads, and game controllers
 * to Banana OS SDK (bwin, banana_api, pthread).
 */
#include "SDL2/SDL.h"
#include <banana.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Window Struct ─────────────────────────────────────────────────── */
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

/* Key state tracking for keyup synthesis and polling */
static Uint8 g_key_state[SDL_NUM_SCANCODES];
static Uint32 g_last_key_press_time = 0;
static int g_last_pressed_scancode = -1;

/* Internal event queue (for synthesized KEYUP or multiple events per poll) */
#define EVENT_QUEUE_SIZE 64
static SDL_Event g_event_queue[EVENT_QUEUE_SIZE];
static int g_event_queue_head = 0;
static int g_event_queue_tail = 0;

static void queue_push_event(const SDL_Event* ev) {
    int next = (g_event_queue_head + 1) % EVENT_QUEUE_SIZE;
    if (next != g_event_queue_tail) {
        g_event_queue[g_event_queue_head] = *ev;
        g_event_queue_head = next;
    }
}

static int queue_pop_event(SDL_Event* ev) {
    if (g_event_queue_head == g_event_queue_tail) return 0;
    *ev = g_event_queue[g_event_queue_tail];
    g_event_queue_tail = (g_event_queue_tail + 1) % EVENT_QUEUE_SIZE;
    return 1;
}

/* ── Error handling ────────────────────────────────────────────────── */
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

/* ── Subsystem Init / Quit ─────────────────────────────────────────── */
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

/* ── Timer & Performance Counter ───────────────────────────────────── */
Uint32 SDL_GetTicks(void) {
    return (Uint32)banana_ticks();
}

Uint64 SDL_GetTicks64(void) {
    return (Uint64)banana_ticks();
}

Uint64 SDL_GetPerformanceCounter(void) {
    /* Nanosecond or microsecond scale monotonic counter mapped to ticks */
    return (Uint64)banana_ticks() * 1000ULL;
}

Uint64 SDL_GetPerformanceFrequency(void) {
    return 1000000ULL; /* 1 MHz */
}

void SDL_Delay(Uint32 ms) {
    banana_sleep(ms);
}

/* ── Pixel formats & Surfaces ──────────────────────────────────────── */
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

/* ── Windows ───────────────────────────────────────────────────────── */
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

/* ── Mouse and Relative capture ────────────────────────────────────── */
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

/* ── Scancode & Keycode translation ────────────────────────────────── */
static SDL_Scancode translate_key_scancode(int key) {
    if (key >= 'a' && key <= 'z') return (SDL_Scancode)(SDL_SCANCODE_A + (key - 'a'));
    if (key >= 'A' && key <= 'Z') return (SDL_Scancode)(SDL_SCANCODE_A + (key - 'A'));
    if (key >= '1' && key <= '9') return (SDL_Scancode)(SDL_SCANCODE_1 + (key - '1'));
    if (key == '0') return SDL_SCANCODE_0;
    if (key >= BANANA_KEY_F1 && key <= BANANA_KEY_F12) {
        return (SDL_Scancode)(SDL_SCANCODE_F1 + (key - BANANA_KEY_F1));
    }
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
    case BANANA_KEY_HOME: return SDL_SCANCODE_HOME;
    case BANANA_KEY_END: return SDL_SCANCODE_END;
    case BANANA_KEY_PGUP: return SDL_SCANCODE_PAGEUP;
    case BANANA_KEY_PGDN: return SDL_SCANCODE_PAGEDOWN;
    case BANANA_KEY_DELETE: return SDL_SCANCODE_DELETE;
    case BANANA_KEY_SHIFT: return SDL_SCANCODE_LSHIFT;
    case BANANA_KEY_CTRL: return SDL_SCANCODE_LCTRL;
    case BANANA_KEY_ALT: return SDL_SCANCODE_LALT;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}

static SDL_Keycode translate_key_keycode(int key) {
    if (key >= 'A' && key <= 'Z') return (SDL_Keycode)(key - 'A' + 'a');
    if (key >= BANANA_KEY_F1 && key <= BANANA_KEY_F12) {
        return (SDL_Keycode)(SDLK_F1 + (key - BANANA_KEY_F1));
    }
    switch (key) {
    case BANANA_KEY_UP: return SDLK_UP;
    case BANANA_KEY_DOWN: return SDLK_DOWN;
    case BANANA_KEY_LEFT: return SDLK_LEFT;
    case BANANA_KEY_RIGHT: return SDLK_RIGHT;
    case BANANA_KEY_HOME: return SDLK_HOME;
    case BANANA_KEY_END: return SDLK_END;
    case BANANA_KEY_PGUP: return SDLK_PAGEUP;
    case BANANA_KEY_PGDN: return SDLK_PAGEDOWN;
    case BANANA_KEY_DELETE: return SDLK_DELETE;
    default: return (SDL_Keycode)key;
    }
}

/* ── Event processing ──────────────────────────────────────────────── */
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

    case BANANA_EV_KEY: {
        SDL_Scancode sc = translate_key_scancode(bev->key);
        SDL_Keycode sym = translate_key_keycode(bev->key);
        ev->type = SDL_KEYDOWN;
        ev->key.windowID = win->id;
        ev->key.state = SDL_PRESSED;
        ev->key.keysym.scancode = sc;
        ev->key.keysym.sym = sym;

        if (sc < SDL_NUM_SCANCODES) {
            g_key_state[sc] = 1;
        }
        g_last_key_press_time = ev->common.timestamp;
        g_last_pressed_scancode = (int)sc;

        /* If printable, also push a text input event */
        if (bev->key >= 32 && bev->key <= 126) {
            SDL_Event text_ev;
            memset(&text_ev, 0, sizeof(text_ev));
            text_ev.type = SDL_TEXTINPUT;
            text_ev.text.timestamp = ev->common.timestamp;
            text_ev.text.windowID = win->id;
            text_ev.text.text[0] = (char)bev->key;
            text_ev.text.text[1] = '\0';
            queue_push_event(&text_ev);
        }
        return 1;
    }

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

/* Check key release timeout (synthesize KEYUP if key is released in Banana OS) */
static void check_key_releases(void) {
    if (g_last_pressed_scancode >= 0 && g_last_pressed_scancode < SDL_NUM_SCANCODES) {
        if (g_key_state[g_last_pressed_scancode]) {
            Uint32 now = SDL_GetTicks();
            /* If no repeat after 150ms and banana_key_down reports 0, generate KEYUP */
            if (now - g_last_key_press_time > 150) {
                g_key_state[g_last_pressed_scancode] = 0;
                SDL_Event up_ev;
                memset(&up_ev, 0, sizeof(up_ev));
                up_ev.type = SDL_KEYUP;
                up_ev.key.timestamp = now;
                up_ev.key.windowID = g_main_window ? g_main_window->id : 0;
                up_ev.key.state = SDL_RELEASED;
                up_ev.key.keysym.scancode = (SDL_Scancode)g_last_pressed_scancode;
                queue_push_event(&up_ev);
                g_last_pressed_scancode = -1;
            }
        }
    }
}

void SDL_PumpEvents(void) {
    check_key_releases();
}

int SDL_PollEvent(SDL_Event* event) {
    if (queue_pop_event(event)) return 1;
    if (!g_main_window) return 0;

    check_key_releases();
    if (queue_pop_event(event)) return 1;

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
    if (queue_pop_event(event)) return 1;
    if (!g_main_window) return 0;

    banana_event_t bev;
    while (bwin_wait_event(&g_main_window->bwin, &bev, timeout)) {
        if (translate_banana_event(g_main_window, &bev, event)) {
            return 1;
        }
    }
    return 0;
}

/* ── Audio Subsystem with Mixer Thread ─────────────────────────────── */
#define SDL_AUDIO_BUFFER_SIZE 4096
#define SDL_AUDIO_QUEUE_MAX_MS 150

static SDL_AudioSpec g_audio_spec;
static int g_audio_open = 0;
static int g_audio_paused = 1;
static pthread_t g_audio_thread;
static volatile int g_audio_running = 0;
static pthread_mutex_t g_audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static Uint8 g_audio_work_buf[SDL_AUDIO_BUFFER_SIZE];
static Uint8 g_queued_audio_buf[65536];
static Uint32 g_queued_audio_len = 0;

static void* audio_mixer_thread_func(void* arg) {
    (void)arg;
    while (g_audio_running) {
        if (g_audio_paused) {
            banana_sleep(10);
            continue;
        }

        /* Check queued audio in sound card driver */
        const banana_api_t* api = banana_api();
        if (api && BANANA_HAS(api, audio_queued_ms)) {
            unsigned int queued_ms = api->audio_queued_ms();
            if (queued_ms > SDL_AUDIO_QUEUE_MAX_MS) {
                banana_sleep(5);
                continue;
            }
        }

        pthread_mutex_lock(&g_audio_mutex);
        if (g_audio_spec.callback) {
            int cb_len = (int)g_audio_spec.size;
            if (cb_len <= 0 || cb_len > SDL_AUDIO_BUFFER_SIZE) cb_len = 1024;
            memset(g_audio_work_buf, g_audio_spec.silence, (size_t)cb_len);
            g_audio_spec.callback(g_audio_spec.userdata, g_audio_work_buf, cb_len);

            int bits = (g_audio_spec.format == AUDIO_U8) ? 8 : 16;
            int channels = g_audio_spec.channels;
            int freq = g_audio_spec.freq;
            if (channels < 1) channels = 1;
            if (freq < 1000) freq = 22050;

            banana_play(g_audio_work_buf, (unsigned long)cb_len, freq, channels, bits);
        } else if (g_queued_audio_len > 0) {
            /* QueueAudio path */
            int chunk = g_queued_audio_len > 2048 ? 2048 : (int)g_queued_audio_len;
            int bits = (g_audio_spec.format == AUDIO_U8) ? 8 : 16;
            int channels = g_audio_spec.channels;
            int freq = g_audio_spec.freq;
            banana_play(g_queued_audio_buf, (unsigned long)chunk, freq, channels, bits);
            if (g_queued_audio_len > (Uint32)chunk) {
                memmove(g_queued_audio_buf, g_queued_audio_buf + chunk, g_queued_audio_len - chunk);
            }
            g_queued_audio_len -= chunk;
        }
        pthread_mutex_unlock(&g_audio_mutex);

        banana_sleep(5);
    }
    return NULL;
}

int SDL_OpenAudio(SDL_AudioSpec* desired, SDL_AudioSpec* obtained) {
    return (SDL_OpenAudioDevice(NULL, 0, desired, obtained, 0) != 0) ? 0 : -1;
}

SDL_AudioDeviceID SDL_OpenAudioDevice(const char* device, int iscapture,
                                      const SDL_AudioSpec* desired,
                                      SDL_AudioSpec* obtained,
                                      int allowed_changes) {
    (void)device; (void)iscapture; (void)allowed_changes;
    if (!desired) {
        SDL_SetError("NULL audio spec");
        return 0;
    }
    SDL_CloseAudio();

    memcpy(&g_audio_spec, desired, sizeof(SDL_AudioSpec));
    if (g_audio_spec.freq <= 0) g_audio_spec.freq = 22050;
    if (g_audio_spec.channels <= 0) g_audio_spec.channels = 2;
    if (g_audio_spec.samples <= 0) g_audio_spec.samples = 1024;
    int bytes_per_sample = (g_audio_spec.format == AUDIO_U8) ? 1 : 2;
    g_audio_spec.size = (Uint32)g_audio_spec.samples * g_audio_spec.channels * bytes_per_sample;

    if (obtained) {
        memcpy(obtained, &g_audio_spec, sizeof(SDL_AudioSpec));
    }

    g_audio_paused = 1;
    g_audio_open = 1;
    g_audio_running = 1;
    g_queued_audio_len = 0;

    if (pthread_create(&g_audio_thread, NULL, audio_mixer_thread_func, NULL) != 0) {
        g_audio_running = 0;
        g_audio_open = 0;
        SDL_SetError("Failed to create audio thread");
        return 0;
    }

    return 1;
}

void SDL_CloseAudio(void) {
    SDL_CloseAudioDevice(1);
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) {
    (void)dev;
    if (g_audio_open) {
        g_audio_running = 0;
        pthread_join(g_audio_thread, NULL);
        const banana_api_t* api = banana_api();
        if (api && BANANA_HAS(api, audio_stop)) {
            api->audio_stop();
        }
        g_audio_open = 0;
        g_audio_paused = 1;
        g_queued_audio_len = 0;
    }
}

void SDL_PauseAudio(int pause_on) {
    SDL_PauseAudioDevice(1, pause_on);
}

void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on) {
    (void)dev;
    g_audio_paused = pause_on;
    if (pause_on) {
        const banana_api_t* api = banana_api();
        if (api && BANANA_HAS(api, audio_stop)) {
            api->audio_stop();
        }
    }
}

void SDL_LockAudio(void) {
    SDL_LockAudioDevice(1);
}

void SDL_LockAudioDevice(SDL_AudioDeviceID dev) {
    (void)dev;
    pthread_mutex_lock(&g_audio_mutex);
}

void SDL_UnlockAudio(void) {
    SDL_UnlockAudioDevice(1);
}

void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) {
    (void)dev;
    pthread_mutex_unlock(&g_audio_mutex);
}

int SDL_QueueAudio(SDL_AudioDeviceID dev, const void* data, Uint32 len) {
    (void)dev;
    if (!data || len == 0) return 0;
    pthread_mutex_lock(&g_audio_mutex);
    if (g_queued_audio_len + len <= sizeof(g_queued_audio_buf)) {
        memcpy(g_queued_audio_buf + g_queued_audio_len, data, len);
        g_queued_audio_len += len;
    }
    pthread_mutex_unlock(&g_audio_mutex);
    return 0;
}

Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev) {
    (void)dev;
    pthread_mutex_lock(&g_audio_mutex);
    Uint32 sz = g_queued_audio_len;
    pthread_mutex_unlock(&g_audio_mutex);
    return sz;
}

void SDL_ClearQueuedAudio(SDL_AudioDeviceID dev) {
    (void)dev;
    pthread_mutex_lock(&g_audio_mutex);
    g_queued_audio_len = 0;
    pthread_mutex_unlock(&g_audio_mutex);
}

/* ── Threads & Synchronization Subsystem ───────────────────────────── */
struct SDL_Thread {
    pthread_t thread;
    SDL_ThreadFunction fn;
    void* data;
    int status;
    char name[32];
    SDL_threadID tid;
};

static void* sdl_thread_wrapper(void* arg) {
    SDL_Thread* t = (SDL_Thread*)arg;
    t->status = t->fn(t->data);
    return NULL;
}

SDL_Thread* SDL_CreateThread(SDL_ThreadFunction fn, const char* name, void* data) {
    if (!fn) {
        SDL_SetError("NULL thread function");
        return NULL;
    }
    SDL_Thread* t = (SDL_Thread*)calloc(1, sizeof(SDL_Thread));
    if (!t) {
        SDL_SetError("Out of memory for SDL_Thread");
        return NULL;
    }
    t->fn = fn;
    t->data = data;
    if (name) snprintf(t->name, sizeof(t->name), "%s", name);

    if (pthread_create(&t->thread, NULL, sdl_thread_wrapper, t) != 0) {
        SDL_SetError("pthread_create failed");
        free(t);
        return NULL;
    }
    t->tid = (SDL_threadID)(uintptr_t)t->thread;
    return t;
}

SDL_threadID SDL_GetThreadID(SDL_Thread* thread) {
    return thread ? thread->tid : 0;
}

SDL_threadID SDL_ThreadID(void) {
    return (SDL_threadID)(uintptr_t)pthread_self();
}

int SDL_SetThreadPriority(SDL_ThreadPriority priority) {
    (void)priority;
    return 0;
}

void SDL_WaitThread(SDL_Thread* thread, int* status) {
    if (!thread) return;
    pthread_join(thread->thread, NULL);
    if (status) *status = thread->status;
    free(thread);
}

void SDL_DetachThread(SDL_Thread* thread) {
    if (!thread) return;
    pthread_detach(thread->thread);
    free(thread);
}

SDL_TLSID SDL_TLSCreate(void) {
    pthread_key_t key;
    if (pthread_key_create(&key, NULL) != 0) return 0;
    return (SDL_TLSID)(key + 1);
}

void* SDL_TLSGet(SDL_TLSID id) {
    if (id == 0) return NULL;
    return pthread_getspecific((pthread_key_t)(id - 1));
}

int SDL_TLSSet(SDL_TLSID id, const void* value, void (*destructor)(void*)) {
    (void)destructor;
    if (id == 0) return -1;
    return pthread_setspecific((pthread_key_t)(id - 1), value) == 0 ? 0 : -1;
}

/* Mutexes */
struct SDL_mutex {
    pthread_mutex_t m;
};

SDL_mutex* SDL_CreateMutex(void) {
    SDL_mutex* mutex = (SDL_mutex*)calloc(1, sizeof(SDL_mutex));
    if (!mutex) {
        SDL_SetError("Out of memory for SDL_mutex");
        return NULL;
    }
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mutex->m, &attr);
    pthread_mutexattr_destroy(&attr);
    return mutex;
}

void SDL_DestroyMutex(SDL_mutex* mutex) {
    if (!mutex) return;
    pthread_mutex_destroy(&mutex->m);
    free(mutex);
}

int SDL_LockMutex(SDL_mutex* mutex) {
    if (!mutex) return -1;
    return pthread_mutex_lock(&mutex->m) == 0 ? 0 : -1;
}

int SDL_TryLockMutex(SDL_mutex* mutex) {
    if (!mutex) return -1;
    return pthread_mutex_trylock(&mutex->m) == 0 ? 0 : -1;
}

int SDL_UnlockMutex(SDL_mutex* mutex) {
    if (!mutex) return -1;
    return pthread_mutex_unlock(&mutex->m) == 0 ? 0 : -1;
}

/* Semaphores */
struct SDL_sem {
    sem_t s;
};

SDL_sem* SDL_CreateSemaphore(Uint32 initial_value) {
    SDL_sem* sem = (SDL_sem*)calloc(1, sizeof(SDL_sem));
    if (!sem) {
        SDL_SetError("Out of memory for SDL_sem");
        return NULL;
    }
    if (sem_init(&sem->s, 0, (unsigned int)initial_value) != 0) {
        SDL_SetError("sem_init failed");
        free(sem);
        return NULL;
    }
    return sem;
}

void SDL_DestroySemaphore(SDL_sem* sem) {
    if (!sem) return;
    sem_destroy(&sem->s);
    free(sem);
}

int SDL_SemWait(SDL_sem* sem) {
    if (!sem) return -1;
    return sem_wait(&sem->s) == 0 ? 0 : -1;
}

int SDL_SemTryWait(SDL_sem* sem) {
    if (!sem) return -1;
    return sem_trywait(&sem->s) == 0 ? 0 : -1;
}

int SDL_SemWaitTimeout(SDL_sem* sem, Uint32 ms) {
    if (!sem) return -1;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return sem_timedwait(&sem->s, &ts) == 0 ? 0 : -1;
}

int SDL_SemPost(SDL_sem* sem) {
    if (!sem) return -1;
    return sem_post(&sem->s) == 0 ? 0 : -1;
}

Uint32 SDL_SemValue(SDL_sem* sem) {
    if (!sem) return 0;
    int val = 0;
    sem_getvalue(&sem->s, &val);
    return val > 0 ? (Uint32)val : 0;
}

/* Condition Variables */
struct SDL_cond {
    pthread_cond_t c;
};

SDL_cond* SDL_CreateCond(void) {
    SDL_cond* cond = (SDL_cond*)calloc(1, sizeof(SDL_cond));
    if (!cond) {
        SDL_SetError("Out of memory for SDL_cond");
        return NULL;
    }
    pthread_cond_init(&cond->c, NULL);
    return cond;
}

void SDL_DestroyCond(SDL_cond* cond) {
    if (!cond) return;
    pthread_cond_destroy(&cond->c);
    free(cond);
}

int SDL_CondSignal(SDL_cond* cond) {
    if (!cond) return -1;
    return pthread_cond_signal(&cond->c) == 0 ? 0 : -1;
}

int SDL_CondBroadcast(SDL_cond* cond) {
    if (!cond) return -1;
    return pthread_cond_broadcast(&cond->c) == 0 ? 0 : -1;
}

int SDL_CondWait(SDL_cond* cond, SDL_mutex* mutex) {
    if (!cond || !mutex) return -1;
    return pthread_cond_wait(&cond->c, &mutex->m) == 0 ? 0 : -1;
}

int SDL_CondWaitTimeout(SDL_cond* cond, SDL_mutex* mutex, Uint32 ms) {
    if (!cond || !mutex) return -1;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(&cond->c, &mutex->m, &ts) == 0 ? 0 : -1;
}

/* ── Joystick and Game Controller Subsystems ───────────────────────── */
struct _SDL_Joystick {
    int index;
    banana_gamepad_t pad;
    char name[64];
};

struct _SDL_GameController {
    SDL_Joystick joystick;
};

static SDL_Joystick g_joysticks[4];
static SDL_GameController g_controllers[4];

int SDL_NumJoysticks(void) {
    int count = 0;
    for (int i = 0; i < 4; i++) {
        banana_gamepad_t pad;
        if (banana_gamepad(i, &pad) && pad.connected) {
            count++;
        }
    }
    return count;
}

SDL_Joystick* SDL_JoystickOpen(int device_index) {
    if (device_index < 0 || device_index >= 4) return NULL;
    banana_gamepad_t pad;
    if (!banana_gamepad(device_index, &pad) || !pad.connected) {
        /* Allow opening even if mock disconnected to permit initialization */
        memset(&pad, 0, sizeof(pad));
    }
    g_joysticks[device_index].index = device_index;
    g_joysticks[device_index].pad = pad;
    snprintf(g_joysticks[device_index].name, sizeof(g_joysticks[device_index].name),
             "%s", pad.name[0] ? pad.name : "Banana Gamepad");
    return &g_joysticks[device_index];
}

void SDL_JoystickClose(SDL_Joystick* joystick) {
    (void)joystick;
}

const char* SDL_JoystickName(SDL_Joystick* joystick) {
    return joystick ? joystick->name : "Unknown Joystick";
}

SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick* joystick) {
    return joystick ? (SDL_JoystickID)joystick->index : -1;
}

int SDL_JoystickNumAxes(SDL_Joystick* joystick) {
    (void)joystick;
    return 6; /* LX, LY, RX, RY, LT, RT */
}

int SDL_JoystickNumButtons(SDL_Joystick* joystick) {
    (void)joystick;
    return 14; /* A, B, X, Y, LB, RB, BACK, START, GUIDE, LS, RS, etc */
}

int SDL_JoystickNumHats(SDL_Joystick* joystick) {
    (void)joystick;
    return 1; /* D-pad */
}

void SDL_JoystickUpdate(void) {
    for (int i = 0; i < 4; i++) {
        banana_gamepad_t pad;
        if (banana_gamepad(i, &pad)) {
            g_joysticks[i].pad = pad;
        }
    }
}

Sint16 SDL_JoystickGetAxis(SDL_Joystick* joystick, int axis) {
    if (!joystick) return 0;
    SDL_JoystickUpdate();
    switch (axis) {
    case 0: return (Sint16)joystick->pad.lx;
    case 1: return (Sint16)joystick->pad.ly;
    case 2: return (Sint16)joystick->pad.rx;
    case 3: return (Sint16)joystick->pad.ry;
    case 4: return (Sint16)joystick->pad.lt;
    case 5: return (Sint16)joystick->pad.rt;
    default: return 0;
    }
}

Uint8 SDL_JoystickGetButton(SDL_Joystick* joystick, int button) {
    if (!joystick) return 0;
    SDL_JoystickUpdate();
    unsigned int b = joystick->pad.buttons;
    switch (button) {
    case 0: return (b & BANANA_PAD_A) ? 1 : 0;
    case 1: return (b & BANANA_PAD_B) ? 1 : 0;
    case 2: return (b & BANANA_PAD_X) ? 1 : 0;
    case 3: return (b & BANANA_PAD_Y) ? 1 : 0;
    case 4: return (b & BANANA_PAD_LB) ? 1 : 0;
    case 5: return (b & BANANA_PAD_RB) ? 1 : 0;
    case 6: return (b & BANANA_PAD_BACK) ? 1 : 0;
    case 7: return (b & BANANA_PAD_START) ? 1 : 0;
    case 8: return (b & BANANA_PAD_GUIDE) ? 1 : 0;
    case 9: return (b & BANANA_PAD_LS) ? 1 : 0;
    case 10: return (b & BANANA_PAD_RS) ? 1 : 0;
    default: return 0;
    }
}

Uint8 SDL_JoystickGetHat(SDL_Joystick* joystick, int hat) {
    if (!joystick || hat != 0) return SDL_HAT_CENTERED;
    SDL_JoystickUpdate();
    unsigned int b = joystick->pad.buttons;
    Uint8 val = SDL_HAT_CENTERED;
    if (b & BANANA_PAD_UP) val |= SDL_HAT_UP;
    if (b & BANANA_PAD_DOWN) val |= SDL_HAT_DOWN;
    if (b & BANANA_PAD_LEFT) val |= SDL_HAT_LEFT;
    if (b & BANANA_PAD_RIGHT) val |= SDL_HAT_RIGHT;
    return val;
}

SDL_bool SDL_IsGameController(int joystick_index) {
    (void)joystick_index;
    return SDL_TRUE; /* All Banana OS gamepads follow standard Xbox layout */
}

SDL_GameController* SDL_GameControllerOpen(int joystick_index) {
    if (joystick_index < 0 || joystick_index >= 4) return NULL;
    SDL_Joystick* js = SDL_JoystickOpen(joystick_index);
    if (!js) return NULL;
    g_controllers[joystick_index].joystick = *js;
    return &g_controllers[joystick_index];
}

void SDL_GameControllerClose(SDL_GameController* gamecontroller) {
    (void)gamecontroller;
}

const char* SDL_GameControllerName(SDL_GameController* gamecontroller) {
    return gamecontroller ? SDL_JoystickName(&gamecontroller->joystick) : "Unknown Controller";
}

SDL_Joystick* SDL_GameControllerGetJoystick(SDL_GameController* gamecontroller) {
    return gamecontroller ? &gamecontroller->joystick : NULL;
}

Sint16 SDL_GameControllerGetAxis(SDL_GameController* gamecontroller, SDL_GameControllerAxis axis) {
    if (!gamecontroller) return 0;
    return SDL_JoystickGetAxis(&gamecontroller->joystick, (int)axis);
}

Uint8 SDL_GameControllerGetButton(SDL_GameController* gamecontroller, SDL_GameControllerButton button) {
    if (!gamecontroller) return 0;
    unsigned int b = gamecontroller->joystick.pad.buttons;
    SDL_JoystickUpdate();
    b = gamecontroller->joystick.pad.buttons;

    switch (button) {
    case SDL_CONTROLLER_BUTTON_A: return (b & BANANA_PAD_A) ? 1 : 0;
    case SDL_CONTROLLER_BUTTON_B: return (b & BANANA_PAD_B) ? 1 : 0;
    case SDL_CONTROLLER_BUTTON_X: return (b & BANANA_PAD_X) ? 1 : 0;
    case SDL_CONTROLLER_BUTTON_Y: return (b & BANANA_PAD_Y) ? 1 : 0;
    case SDL_CONTROLLER_BUTTON_BACK: return (b & BANANA_PAD_BACK) ? 1 : 0;
    case SDL_CONTROLLER_BUTTON_GUIDE: return (b & BANANA_PAD_GUIDE) ? 1 : 0;
    case SDL_CONTROLLER_BUTTON_START: return (b & BANANA_PAD_START) ? 1 : 0;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return (b & BANANA_PAD_LS) ? 1 : 0;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return (b & BANANA_PAD_RS) ? 1 : 0;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return (b & BANANA_PAD_LB) ? 1 : 0;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return (b & BANANA_PAD_RB) ? 1 : 0;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return (b & BANANA_PAD_UP) ? 1 : 0;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return (b & BANANA_PAD_DOWN) ? 1 : 0;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return (b & BANANA_PAD_LEFT) ? 1 : 0;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return (b & BANANA_PAD_RIGHT) ? 1 : 0;
    default: return 0;
    }
}

void SDL_GameControllerUpdate(void) {
    SDL_JoystickUpdate();
}
