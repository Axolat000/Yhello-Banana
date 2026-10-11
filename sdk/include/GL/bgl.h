#ifndef BGL_H
#define BGL_H

/*
 * OpenGL contexts on Banana OS windows (USE_GL = 1 in the Makefile).
 *
 *     bwin_t win;
 *     bwin_open(&win, "Spin", 640, 480);
 *     bgl_ctx_t* gl = bgl_create(&win);       (it is now current)
 *     for (;;) {
 *         ... glClear, draw ...
 *         bgl_swap(gl);                       (the frame shows up in the window)
 *         events: on BANANA_EV_RESIZE call bgl_resize(gl) and glViewport
 *     }
 *
 * Drawing is queued and rendered at bgl_swap / glFinish by every processor
 * core of the computer, each taking its share of the rows.
 */

#include "gl.h"
#include "glu.h"
#include <banana.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bgl_ctx bgl_ctx_t;

bgl_ctx_t* bgl_create(bwin_t* win);            /* a context drawing into win; NULL if out of memory */
bgl_ctx_t* bgl_create_offscreen(int w, int h); /* no window: read the result with bgl_pixels() */
void bgl_make_current(bgl_ctx_t* ctx);
bgl_ctx_t* bgl_current(void);
void bgl_resize(bgl_ctx_t* ctx);               /* the window changed size (after BANANA_EV_RESIZE) */
void bgl_swap(bgl_ctx_t* ctx);                 /* finish the frame, show it */
const unsigned int* bgl_pixels(bgl_ctx_t* ctx, int* w, int* h);   /* the frame, 0x00RRGGBB, top row first */
void bgl_destroy(bgl_ctx_t* ctx);
int  bgl_threads(void);                        /* processor cores the renderer uses */

#ifdef __cplusplus
}
#endif

#endif
