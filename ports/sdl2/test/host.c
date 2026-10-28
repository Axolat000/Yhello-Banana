/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <SDL2/SDL.h>
#include <banana.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static Uint64 now;
static int opened, closed, fail_resize;
static banana_event_t queued;
static int queued_count;
int sdl_test_clock_gettime(clockid_t id, struct timespec *t) {
    assert(id == CLOCK_MONOTONIC);
    t->tv_sec = (time_t)(now / 1000); t->tv_nsec = (long)(now % 1000) * 1000000;
    return 0;
}
void banana_sleep(unsigned int ms) { now += ms; }
int bwin_open(bwin_t *win, const char *title, int w, int h) {
    (void)title; opened++; win->id = 0; win->w = w; win->h = h;
    win->px = calloc((size_t)w * h, 4); return win->px ? 0 : -1;
}
void bwin_close(bwin_t *win) { closed++; free(win->px); }
void bwin_title(bwin_t *win, const char *title) { (void)win; (void)title; }
void bwin_update(bwin_t *win) { assert(win->px); }
int bwin_resizable(bwin_t *win, int w, int h) { (void)win; (void)w; (void)h; return fail_resize ? -1 : 0; }
int bwin_event(bwin_t *win, banana_event_t *out) {
    if (!queued_count) return 0;
    *out = queued; queued_count = 0;
    if (out->type == BANANA_EV_RESIZE) { win->w = 2; win->h = 2; }
    return 1;
}
int main(void) {
    assert(SDL_Init(SDL_INIT_AUDIO) == -1 && SDL_WasInit(0) == 0);
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0);
    assert(SDL_GetTicks64() == 0);
    now = UINT32_MAX - 10u;
    assert(SDL_GetTicks64() == now);
    now = (1ULL << 32) + 123;
    assert(SDL_GetTicks64() == now && SDL_GetTicks() == 123);
    assert(!SDL_CreateRGBSurface(0, INT_MAX, INT_MAX, 32, 0, 0, 0, 0));
    assert(!SDL_CreateRGBSurface(0, 4096, 4097, 32, 0, 0, 0, 0));
    assert(!SDL_CreateRGBSurface(0, 4, 4, 16, 0, 0, 0, 0));
    assert(!SDL_CreateRGBSurface(0, 4, 4, 32, 0, 0, 0, 0xff000000));
    SDL_Surface *s = SDL_CreateRGBSurface(0, 4, 4, 32, 0, 0, 0, 0);
    assert(s);
    SDL_Rect rect = {-1, -1, 2, 2};
    assert(SDL_FillRect(s, &rect, 0x123456) == 0);
    Uint32 *px = s->pixels;
    assert(px[0] == 0x123456 && px[1] == 0 && px[4] == 0);
    rect = (SDL_Rect){INT_MAX, INT_MAX, INT_MAX, INT_MAX};
    assert(SDL_FillRect(s, &rect, 1) == 0);
    for (int i = 0; i < 16; i++) px[i] = (Uint32)i;
    SDL_Rect from = {0, 0, 4, 3}, to = {0, 1, 0, 0};
    assert(SDL_BlitSurface(s, &from, s, &to) == 0);
    assert(px[4] == 0 && px[8] == 4 && px[12] == 8 && to.w == 4 && to.h == 3);
    from = (SDL_Rect){INT_MIN, INT_MIN, INT_MAX, INT_MAX};
    to = (SDL_Rect){INT_MAX, INT_MAX, 0, 0};
    assert(SDL_BlitSurface(s, &from, s, &to) == 0 && to.w == 0 && to.h == 0);
    SDL_FreeSurface(s);
    assert(!SDL_CreateWindow("invalid", 0, 0, 4, 4, 0));
    for (int i = 0; i < 100; i++) {
        SDL_Window *w = SDL_CreateWindow("test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 4, 4, SDL_WINDOW_RESIZABLE);
        assert(w && SDL_GetWindowID(w));
        assert(!SDL_CreateWindow("second", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 4, 4, 0));
        SDL_Surface *borrowed = SDL_GetWindowSurface(w);
        SDL_FreeSurface(borrowed); /* must not release desktop-owned pixels */
        assert(SDL_UpdateWindowSurface(w) == 0);
        queued = (banana_event_t){.type = BANANA_EV_MOUSE_DOWN, .button = 2, .x = 3, .y = 1}; queued_count = 1;
        assert(SDL_PollEvent(NULL) == 1);
        SDL_Event e;
        assert(SDL_PollEvent(&e) == 1 && e.button.button == SDL_BUTTON_RIGHT);
        queued = (banana_event_t){.type = BANANA_EV_RESIZE}; queued_count = 1;
        assert(SDL_PollEvent(&e) == 1 && e.window.data1 == 2 && SDL_GetWindowSurface(w)->w == 2);
        Uint64 start = now;
        assert(SDL_WaitEventTimeout(&e, 5) == 0 && now - start == 5);
        SDL_DestroyWindow(w);
    }
    fail_resize = 1;
    assert(!SDL_CreateWindow("unsupported", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 4, 4, SDL_WINDOW_RESIZABLE));
    assert(opened == closed);
    SDL_Quit(); assert(SDL_WasInit(0) == 0);
    puts("sdl2-compat host: PASS (bounds, clipping, overlap, ownership, events, 64-bit timer, 100 window cycles)");
    return 0;
}
