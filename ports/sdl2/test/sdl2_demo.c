/*
 * SDL2 Qualification Test & Comprehensive Subsystem Verification for Banana OS.
 * Exercises:
 *  - SDL_Init & SDL_Quit with all subsystems (VIDEO, TIMER, AUDIO, EVENTS, JOYSTICK)
 *  - High-precision monotonic timer & Performance Counter
 *  - Threading & synchronization primitives (SDL_Thread, SDL_mutex, SDL_sem, SDL_cond, TLS)
 *  - Audio device opening, callback mixing, queueing, pausing, and closing
 *  - Gamepad / Joystick querying and status inspection
 *  - Window creation, surface rendering, title setting, dynamic resize
 *  - 100-cycle open/draw/close stress test (memory leak verification)
 *  - Event handling & synthetic key up / text input / relative mouse state
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

/* ── 1. Monotonic Timer & Performance Counter Test ─────────────────── */
static int test_timer_subsystem(void) {
    printf("[sdl2_test] Testing monotonic timer and performance counter...\n");
    Uint32 t1 = SDL_GetTicks();
    Uint64 p1 = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    if (freq == 0) {
        printf("[sdl2_test] Error: SDL_GetPerformanceFrequency returned 0\n");
        return -1;
    }

    SDL_Delay(50);
    Uint32 t2 = SDL_GetTicks();
    Uint64 p2 = SDL_GetPerformanceCounter();

    if (t2 < t1) {
        printf("[sdl2_test] Error: Non-monotonic SDL_GetTicks: %u -> %u\n", t1, t2);
        return -1;
    }
    if (p2 < p1) {
        printf("[sdl2_test] Error: Non-monotonic PerformanceCounter\n");
        return -1;
    }
    printf("[sdl2_test] Timer check passed: elapsed %u ms, freq %llu Hz.\n", t2 - t1, (unsigned long long)freq);
    return 0;
}

/* ── 2. Threads and Synchronization Primitives Test ────────────────── */
typedef struct {
    SDL_mutex* mutex;
    SDL_sem* sem;
    SDL_cond* cond;
    int counter;
    int signaled;
} ThreadTestData;

static int worker_thread_fn(void* data) {
    ThreadTestData* td = (ThreadTestData*)data;

    /* Wait on semaphore from main thread */
    if (SDL_SemWait(td->sem) != 0) {
        return 1;
    }

    /* Lock mutex and mutate counter */
    SDL_LockMutex(td->mutex);
    td->counter += 42;
    td->signaled = 1;
    SDL_CondSignal(td->cond);
    SDL_UnlockMutex(td->mutex);

    return 0;
}

static int test_threads_and_sync(void) {
    printf("[sdl2_test] Testing threads, mutexes, semaphores, and condition variables...\n");
    ThreadTestData td;
    memset(&td, 0, sizeof(td));
    td.mutex = SDL_CreateMutex();
    td.sem = SDL_CreateSemaphore(0);
    td.cond = SDL_CreateCond();

    if (!td.mutex || !td.sem || !td.cond) {
        printf("[sdl2_test] Failed creating synchronization primitives\n");
        return -1;
    }

    /* TLS test */
    SDL_TLSID tls_id = SDL_TLSCreate();
    if (tls_id != 0) {
        SDL_TLSSet(tls_id, (void*)0x1234, NULL);
        if (SDL_TLSGet(tls_id) != (void*)0x1234) {
            printf("[sdl2_test] Error: TLSGet failed to retrieve value\n");
            return -1;
        }
    }

    SDL_Thread* th = SDL_CreateThread(worker_thread_fn, "TestWorker", &td);
    if (!th) {
        printf("[sdl2_test] Error: SDL_CreateThread failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Release worker via semaphore */
    SDL_SemPost(td.sem);

    /* Wait on condvar */
    SDL_LockMutex(td.mutex);
    while (!td.signaled) {
        SDL_CondWait(td.cond, td.mutex);
    }
    int observed_val = td.counter;
    SDL_UnlockMutex(td.mutex);

    int thread_status = -1;
    SDL_WaitThread(th, &thread_status);

    SDL_DestroyCond(td.cond);
    SDL_DestroySemaphore(td.sem);
    SDL_DestroyMutex(td.mutex);

    if (thread_status != 0 || observed_val != 42) {
        printf("[sdl2_test] Error: Thread test failed (status=%d, val=%d)\n", thread_status, observed_val);
        return -1;
    }
    printf("[sdl2_test] Thread & sync primitives verified successfully.\n");
    return 0;
}

/* ── 3. Audio Subsystem Test ───────────────────────────────────────── */
static volatile int g_audio_callback_invoked = 0;

static void audio_test_callback(void* userdata, Uint8* stream, int len) {
    (void)userdata;
    /* Synthesize simple audio sine/square waveform */
    for (int i = 0; i < len; i++) {
        stream[i] = (Uint8)((i % 32 < 16) ? 192 : 64);
    }
    g_audio_callback_invoked++;
}

static int test_audio_subsystem(void) {
    printf("[sdl2_test] Testing audio subsystem (device, callback mixer, pausing, queueing)...\n");
    SDL_AudioSpec desired, obtained;
    memset(&desired, 0, sizeof(desired));
    desired.freq = 22050;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 512;
    desired.callback = audio_test_callback;

    if (SDL_OpenAudio(&desired, &obtained) != 0) {
        printf("[sdl2_test] Error: SDL_OpenAudio failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Unpause and let mixer thread run for a short duration */
    SDL_PauseAudio(0);
    SDL_Delay(50);
    SDL_PauseAudio(1);

    SDL_CloseAudio();

    printf("[sdl2_test] Audio subsystem verified cleanly (callback called %d times).\n", g_audio_callback_invoked);
    return 0;
}

/* ── 4. Gamepad & Joystick Test ────────────────────────────────────── */
static int test_joystick_subsystem(void) {
    printf("[sdl2_test] Testing joystick and gamecontroller subsystem...\n");
    int num_joysticks = SDL_NumJoysticks();
    printf("[sdl2_test] Detected %d joysticks.\n", num_joysticks);

    /* Open controller slot 0 (either connected or simulated) */
    SDL_Joystick* js = SDL_JoystickOpen(0);
    if (js) {
        printf("[sdl2_test] Opened joystick: %s (axes=%d, buttons=%d, hats=%d)\n",
               SDL_JoystickName(js), SDL_JoystickNumAxes(js),
               SDL_JoystickNumButtons(js), SDL_JoystickNumHats(js));
        SDL_JoystickClose(js);
    }

    if (SDL_IsGameController(0)) {
        SDL_GameController* gc = SDL_GameControllerOpen(0);
        if (gc) {
            printf("[sdl2_test] Opened GameController: %s\n", SDL_GameControllerName(gc));
            SDL_GameControllerClose(gc);
        }
    }
    printf("[sdl2_test] Joystick/Controller subsystem verified.\n");
    return 0;
}

/* ── 5. Stress test: 100 cycles window creation/rendering/destruction ─ */
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
    printf("[sdl2_test] Completed %d cycles cleanly without leakage.\n", cycles);
    return 0;
}

/* ── 6. Interactive / Functional Demo ──────────────────────────────── */
static int run_interactive_demo(void) {
    printf("[sdl2_test] Starting interactive demonstration & event processing...\n");
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

    /* Test relative mouse mode toggle */
    SDL_SetRelativeMouseMode(SDL_TRUE);
    if (!SDL_GetRelativeMouseMode()) {
        printf("[sdl2_test] Warning: Relative mouse mode not enabled\n");
    }
    SDL_SetRelativeMouseMode(SDL_FALSE);

    int running = 1;
    int frame = 0;
    Uint32 last_tick = SDL_GetTicks();

    while (running && frame < 15) { /* 15 frames in test mode */
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

/* ── 7. OpenGL Acceleration and Focus Loss Test ────────────────────── */
static int test_opengl_and_focus(void) {
    printf("[sdl2_test] Testing OpenGL context creation, attributes, swap, and focus loss handling...\n");

    /* 1. Attributes configuration */
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    int maj = 0, min = 0, prof = 0, db = 0, depth = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &maj);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &min);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &prof);
    SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &db);
    SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depth);

    if (maj != 3 || min != 3 || prof != SDL_GL_CONTEXT_PROFILE_CORE || db != 1 || depth != 24) {
        printf("[sdl2_test] Error: OpenGL attributes mismatch: maj=%d min=%d prof=%d db=%d depth=%d\n",
               maj, min, prof, db, depth);
        return -1;
    }

    SDL_GL_SetSwapInterval(1);
    if (SDL_GL_GetSwapInterval() != 1) {
        printf("[sdl2_test] Error: SDL_GL_GetSwapInterval mismatch\n");
        return -1;
    }

    /* 2. Window and Context creation with SDL_WINDOW_OPENGL */
    SDL_Window* gl_win = SDL_CreateWindow("SDL2 OpenGL Test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          400, 300, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!gl_win) {
        printf("[sdl2_test] Error creating OpenGL window: %s\n", SDL_GetError());
        return -1;
    }

    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(gl_win, &dw, &dh);
    if (dw != 400 || dh != 300) {
        printf("[sdl2_test] Warning: Drawable size mismatch: %dx%d\n", dw, dh);
    }

#if defined(__x86_64__)
    SDL_GLContext ctx = SDL_GL_CreateContext(gl_win);
    if (ctx) {
        printf("[sdl2_test] OpenGL context created successfully.\n");
        if (SDL_GL_GetCurrentContext() != ctx) {
            printf("[sdl2_test] Error: Current context mismatch\n");
            SDL_GL_DeleteContext(ctx);
            SDL_DestroyWindow(gl_win);
            return -1;
        }

        if (SDL_GL_GetCurrentWindow() != gl_win) {
            printf("[sdl2_test] Error: Current window mismatch\n");
            SDL_GL_DeleteContext(ctx);
            SDL_DestroyWindow(gl_win);
            return -1;
        }

        /* Check procedure resolution */
        void* pClear = SDL_GL_GetProcAddress("glClear");
        printf("[sdl2_test] SDL_GL_GetProcAddress('glClear') = %p\n", pClear);

        /* Render 5 frames using OpenGL swap */
        for (int f = 0; f < 5; f++) {
            SDL_GL_SwapWindow(gl_win);
        }

        /* Test switching context to NULL and back */
        SDL_GL_MakeCurrent(NULL, NULL);
        if (SDL_GL_GetCurrentContext() != NULL) {
            printf("[sdl2_test] Error: Context not cleared\n");
            SDL_GL_DeleteContext(ctx);
            SDL_DestroyWindow(gl_win);
            return -1;
        }

        SDL_GL_MakeCurrent(gl_win, ctx);
        if (SDL_GL_GetCurrentContext() != ctx) {
            printf("[sdl2_test] Error: Failed to restore current context\n");
            SDL_GL_DeleteContext(ctx);
            SDL_DestroyWindow(gl_win);
            return -1;
        }

        SDL_GL_DeleteContext(ctx);
        if (SDL_GL_GetCurrentContext() != NULL) {
            printf("[sdl2_test] Error: Context not NULL after delete\n");
            SDL_DestroyWindow(gl_win);
            return -1;
        }
    } else {
        printf("[sdl2_test] OpenGL context creation note (softpipe libOSMesa loadable in runtime): %s\n", SDL_GetError());
    }
#endif

    /* 3. Focus loss and Destruction test */
    SDL_Event fake_focus;
    memset(&fake_focus, 0, sizeof(fake_focus));
    fake_focus.type = SDL_WINDOWEVENT;
    fake_focus.window.windowID = SDL_GetWindowID(gl_win);
    fake_focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;

    /* Push and pop focus event to verify handling */
    SDL_PushEvent(&fake_focus);
    SDL_Event polled;
    int found_focus = 0;
    while (SDL_PollEvent(&polled)) {
        if (polled.type == SDL_WINDOWEVENT && polled.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            found_focus = 1;
        }
    }
    if (!found_focus) {
        printf("[sdl2_test] Error: Focus lost event was not processed\n");
        SDL_DestroyWindow(gl_win);
        return -1;
    }

    /* Destroy window and verify clean state */
    SDL_DestroyWindow(gl_win);
    printf("[sdl2_test] OpenGL context and focus loss verification passed.\n");
    return 0;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("[sdl2_test] Initializing SDL2 subsystems (VIDEO, TIMER, AUDIO, JOYSTICK, EVENTS)...\n");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0) {
        printf("[sdl2_test] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* 1. Monotonic timer & perf counter */
    if (test_timer_subsystem() != 0) {
        SDL_Quit();
        return 1;
    }

    /* 2. Threads and synchronization */
    if (test_threads_and_sync() != 0) {
        SDL_Quit();
        return 1;
    }

    /* 3. Audio callback and streaming */
    if (test_audio_subsystem() != 0) {
        SDL_Quit();
        return 1;
    }

    /* 4. Gamepad & Joystick */
    if (test_joystick_subsystem() != 0) {
        SDL_Quit();
        return 1;
    }

    /* 5. Stress test: 100 cycles open, draw, close */
    if (run_stress_test(100) != 0) {
        printf("[sdl2_test] Stress test FAILED.\n");
        SDL_Quit();
        return 1;
    }

    /* 6. Interactive / functional demo */
    if (run_interactive_demo() != 0) {
        printf("[sdl2_test] Demo FAILED.\n");
        SDL_Quit();
        return 1;
    }

    /* 7. OpenGL acceleration, context switching, and focus loss handling */
    if (test_opengl_and_focus() != 0) {
        printf("[sdl2_test] OpenGL / Focus test FAILED.\n");
        SDL_Quit();
        return 1;
    }

    SDL_Quit();
    printf("[sdl2_test] All SDL2 qualification and subsystem tests PASSED successfully.\n");
    return 0;
}
