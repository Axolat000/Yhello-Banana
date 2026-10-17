/*
 * OpenGL 3.3 for Banana OS apps (USE_GL3 = 1): a GL 3.3 core context from
 * Mesa's OSMesa (libOSMesa.so, softpipe: drawing on the processor), loaded
 * with the SDK's dlopen the first time a context is made. Mesa draws into a
 * buffer of ours (rows top first, 0xAARRGGBB); bgl3_swap copies it into
 * the window. The GL functions are pointers (<GL/gl3procs.h>) filled from
 * OSMesaGetProcAddress.
 */
#include "GL/gl3.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define X(type, name) type bgl3_##name;
BGL3_PROCS(X)
#undef X

/* OSMesa (GL/osmesa.h) */
typedef struct osmesa_context* OSMesaContext;
#define OSMESA_BGRA                  0x1
#define OSMESA_Y_UP                  0x11
#define OSMESA_FORMAT                0x22
#define OSMESA_DEPTH_BITS            0x30
#define OSMESA_STENCIL_BITS          0x31
#define OSMESA_PROFILE               0x33
#define OSMESA_CORE_PROFILE          0x34
#define OSMESA_CONTEXT_MAJOR_VERSION 0x36
#define OSMESA_CONTEXT_MINOR_VERSION 0x37
static OSMesaContext (*os_create)(const int* attribs, OSMesaContext share);
static void (*os_destroy)(OSMesaContext c);
static GLboolean (*os_make_current)(OSMesaContext c, void* buf, GLenum type, GLsizei w, GLsizei h);
static void (*os_pixel_store)(GLint pname, GLint value);
static void* (*os_proc)(const char* name);

struct bgl3_ctx {
    OSMesaContext os;
    bwin_t* win;
    int w, h;
    unsigned int* px;               /* Mesa's color buffer: w x h, top row first */
};

static void* g_lib;
static char g_err[256];
static bgl3_ctx_t* g_cur;

const char* bgl3_error(void) { return g_err; }

static int load(void) {
    if (g_lib) return 0;
    const char* env = getenv("BGL3_LIBRARY");
    static const char* const where[] = {
        "libOSMesa.so", "/lib/libOSMesa.so", "/usr/lib/libOSMesa.so", "/usr/java/lib/libOSMesa.so",
        "/mnt/usb/lib/libOSMesa.so", "/mnt/virtio/lib/libOSMesa.so", "/mnt/sata/lib/libOSMesa.so", NULL,
    };
    void* lib = env ? dlopen(env, RTLD_NOW | RTLD_GLOBAL) : NULL;
    /* (a copy: dlerror's text is overwritten by the next failure) */
    char why[200] = "";
    if (env && !lib) snprintf(why, sizeof(why), "%s", dlerror());
    for (int i = 0; !lib && where[i]; i++) {
        lib = dlopen(where[i], RTLD_NOW | RTLD_GLOBAL);
        const char* e = lib ? NULL : dlerror();
        if (e && !strstr(e, "No such file")) snprintf(why, sizeof(why), "%s", e);   /* (found, but it did not load) */
    }
    if (!lib) {
        snprintf(g_err, sizeof(g_err), "OpenGL 3.3: %s", why[0] ? why : "libOSMesa.so not found (/lib, /usr/lib, /usr/java/lib, /mnt/*/lib)");
        return -1;
    }
    *(void**)&os_create = dlsym(lib, "OSMesaCreateContextAttribs");
    *(void**)&os_destroy = dlsym(lib, "OSMesaDestroyContext");
    *(void**)&os_make_current = dlsym(lib, "OSMesaMakeCurrent");
    *(void**)&os_pixel_store = dlsym(lib, "OSMesaPixelStore");
    *(void**)&os_proc = dlsym(lib, "OSMesaGetProcAddress");
    if (!os_create || !os_destroy || !os_make_current || !os_pixel_store || !os_proc) {
        snprintf(g_err, sizeof(g_err), "OpenGL 3.3: libOSMesa.so lacks the OSMesa functions");
        dlclose(lib);
        return -1;
    }
    g_lib = lib;
    return 0;
}

/* the GL function pointers, from the current context's Mesa */
static int load_procs(void) {
    int missing = 0;
#define X(type, name) if (!(bgl3_##name = (type)os_proc(#name))) missing++;
    BGL3_PROCS(X)
#undef X
    if (missing) snprintf(g_err, sizeof(g_err), "OpenGL 3.3: %d functions missing in libOSMesa.so", missing);
    return missing ? -1 : 0;
}

void* bgl3_proc(const char* name) { return os_proc && name ? os_proc(name) : NULL; }

static int alloc_px(bgl3_ctx_t* c, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    unsigned int* p = calloc((size_t)w * h, 4);
    if (!p) return -1;
    free(c->px);
    c->px = p;
    c->w = w;
    c->h = h;
    return 0;
}

static bgl3_ctx_t* create(bwin_t* win, int w, int h) {
    g_err[0] = 0;
    if (load()) return NULL;
    const int at[] = {
        OSMESA_FORMAT, OSMESA_BGRA,
        OSMESA_DEPTH_BITS, 24,
        OSMESA_STENCIL_BITS, 8,
        OSMESA_PROFILE, OSMESA_CORE_PROFILE,
        OSMESA_CONTEXT_MAJOR_VERSION, 3,
        OSMESA_CONTEXT_MINOR_VERSION, 3,
        0,
    };
    bgl3_ctx_t* c = calloc(1, sizeof(*c));
    if (!c || alloc_px(c, w, h)) { free(c); snprintf(g_err, sizeof(g_err), "OpenGL 3.3: out of memory"); return NULL; }
    c->win = win;
    c->os = os_create(at, NULL);
    if (!c->os) {
        snprintf(g_err, sizeof(g_err), "OpenGL 3.3: Mesa could not make a 3.3 core context");
        free(c->px);
        free(c);
        return NULL;
    }
    if (bgl3_make_current(c) || load_procs()) {
        bgl3_destroy(c);
        if (!g_err[0]) snprintf(g_err, sizeof(g_err), "OpenGL 3.3: the context could not be made current");
        return NULL;
    }
    return c;
}

bgl3_ctx_t* bgl3_create(bwin_t* win) {
    if (!win || win->id < 0) { snprintf(g_err, sizeof(g_err), "OpenGL 3.3: no window"); return NULL; }
    return create(win, win->w, win->h);
}

bgl3_ctx_t* bgl3_create_offscreen(int w, int h) { return create(NULL, w, h); }

int bgl3_make_current(bgl3_ctx_t* c) {
    if (!c) { if (os_make_current) os_make_current(NULL, NULL, 0, 0, 0); g_cur = NULL; return 0; }
    if (!os_make_current(c->os, c->px, GL_UNSIGNED_BYTE, c->w, c->h)) return -1;
    os_pixel_store(OSMESA_Y_UP, 0);                    /* row 0 at the top, as windows */
    g_cur = c;
    return 0;
}

bgl3_ctx_t* bgl3_current(void) { return g_cur; }

void bgl3_resize(bgl3_ctx_t* c) {
    if (!c || !c->win || (c->win->w == c->w && c->win->h == c->h)) return;
    if (bgl3_glFinish) bgl3_glFinish();
    if (alloc_px(c, c->win->w, c->win->h) == 0 && g_cur == c) bgl3_make_current(c);
}

void bgl3_swap(bgl3_ctx_t* c) {
    if (!c) return;
    bgl3_glFinish();
    if (!c->win || !c->win->px) return;
    int w = c->w < c->win->w ? c->w : c->win->w, h = c->h < c->win->h ? c->h : c->win->h;
    for (int y = 0; y < h; y++) {
        const unsigned int* s = c->px + (long)y * c->w;
        unsigned int* d = c->win->px + (long)y * c->win->w;
        for (int x = 0; x < w; x++) d[x] = s[x] & 0x00FFFFFFu;
    }
    bwin_update(c->win);
}

const unsigned int* bgl3_pixels(bgl3_ctx_t* c, int* w, int* h) {
    if (!c) return NULL;
    bgl3_glFinish();
    if (w) *w = c->w;
    if (h) *h = c->h;
    return c->px;
}

void bgl3_destroy(bgl3_ctx_t* c) {
    if (!c) return;
    if (g_cur == c) bgl3_make_current(NULL);
    if (c->os) os_destroy(c->os);
    free(c->px);
    free(c);
}
