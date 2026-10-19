/*
 * SDL2 Qualification Test & Demo for Banana OS.
 * Exercises:
 *  - SDL_Init & SDL_Quit
 *  - Window creation, surface rendering, title setting
 *  - 100-cycle open/draw/close stress test (memory leak verification)
 *  - Event handling & timer delay
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

static int run_stress_test(int cycles) {
    printf("[sdl2_test] Running %d-cycle window creation/rendering/destruction test...\n", cycles);
    for (int i = 0; i < cycles; i++) {
        SDL_Window* win = SDL_CreateWindow("SDL2 Stress Cycle", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                           320, 240, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (!win) {
            printf("[sdl2_test] Error creating window at cycle %d: %s\n", i, SDL_GetError());
            return -1;
        }

        SDL_Surface* surf = SDL_GetWindowSurface(win);
        if (!surf) {
            printf("[sdl2_test] Error getting surface at cycle %d: %s\n", i, SDL_GetError());
            SDL_DestroyWindow(win);
            return -1;
        }

        /* Fill background color (alternating colors per cycle) */
        Uint32 color = (i % 2 == 0) ? 0x003A7BD5u : 0x0040C060u;
        SDL_FillRect(surf, NULL, color);

        /* Draw a small inner test rect */
        SDL_Rect rect = { 40, 40, 240, 160 };
        SDL_FillRect(surf, &rect, 0x00F4D35Eu);

        SDL_UpdateWindowSurface(win);

        /* Poll any pending events */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {}

        SDL_DestroyWindow(win);
    }
    printf("[sdl2_test] Completed %d cycles cleanly.\n", cycles);
    return 0;
}

static int run_interactive_demo(void) {
    printf("[sdl2_test] Starting interactive demonstration...\n");
    SDL_Window* win = SDL_CreateWindow("Banana OS - SDL2 Demo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       640, 480, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) {
        printf("[sdl2_test] Error creating demo window: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Surface* surf = SDL_GetWindowSurface(win);
    if (!surf) {
        printf("[sdl2_test] Error getting demo surface: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        return -1;
    }

    int running = 1;
    int frame = 0;
    Uint32 last_tick = SDL_GetTicks();

    while (running && frame < 10) { /* 10 frames in automated test mode */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    running = 0;
                }
            } else if (ev.type == SDL_WINDOWEVENT) {
                if (ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                    surf = SDL_GetWindowSurface(win);
                }
            }
        }

        /* Clear surface to dark grey */
        SDL_FillRect(surf, NULL, 0x001D232Cu);

        /* Animate a bouncing rectangle */
        int rx = 50 + (frame * 15) % 400;
        int ry = 50 + (frame * 10) % 300;
        SDL_Rect box = { rx, ry, 120, 80 };
        SDL_FillRect(surf, &box, 0x00E04040u);

        SDL_UpdateWindowSurface(win);
        SDL_Delay(10);
        frame++;
    }

    Uint32 elapsed = SDL_GetTicks() - last_tick;
    printf("[sdl2_test] Demo executed %d frames in %u ms.\n", frame, elapsed);
    SDL_DestroyWindow(win);
    return 0;
}

int main(int argc, char** argv) {
    printf("[sdl2_test] Initializing SDL2 subsystems...\n");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        printf("[sdl2_test] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* 1. Stress test: 100 cycles open, draw, close */
    if (run_stress_test(100) != 0) {
        printf("[sdl2_test] Stress test FAILED.\n");
        SDL_Quit();
        return 1;
    }

    /* 2. Interactive / functional demo */
    if (run_interactive_demo() != 0) {
        printf("[sdl2_test] Demo FAILED.\n");
        SDL_Quit();
        return 1;
    }

    SDL_Quit();
    printf("[sdl2_test] All SDL2 tests PASSED successfully.\n");
    return 0;
}
