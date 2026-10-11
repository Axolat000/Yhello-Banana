/*
 * OpenGL for Banana OS: the fixed-function pipeline in software.
 *
 * Vertices go through the vertex stage as they come (modelview, lighting,
 * texture matrix, fog, projection), primitives are assembled, clipped
 * against the six planes, culled and turned into window coordinates, then
 * queued with a snapshot of the state they are drawn with. The queue is
 * rendered when the frame is shown (bgl_swap), on glFinish / glFlush /
 * glClear / glReadPixels, or when a texture in use changes: every processor
 * core takes every n-th row and walks the whole queue in order, so pixels
 * are drawn in submission order exactly as with one core.
 *
 * Built into apps that set USE_GL = 1 (sdk/banana.mk). BGL_HOST builds it on
 * Linux with pthreads, for testing (sdk/lib/gltest/).
 */
#ifdef BGL_HOST
#include "GL/gl.h"
#include "GL/glu.h"
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
typedef struct bwin bwin_t;
typedef struct bgl_ctx bgl_ctx_t;
bgl_ctx_t* bgl_create_offscreen(int w, int h);
void bgl_make_current(bgl_ctx_t* ctx);
bgl_ctx_t* bgl_current(void);
const unsigned int* bgl_pixels(bgl_ctx_t* ctx, int* w, int* h);
void bgl_destroy(bgl_ctx_t* ctx);
int  bgl_threads(void);
void bgl_swap(bgl_ctx_t* ctx);
#else
#include "GL/bgl.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#define NLIGHTS   8
#define STACK     32
#define MAXTHR    16
#define MAXCLIP   16
#define QUEUE_MAX (1 << 18)        /* primitives queued before an early render */

typedef struct { float m[16]; } M4;   /* column-major, as GL */

typedef struct {                /* a vertex after the vertex stage */
    float x, y, z, w;           /* clip coordinates */
    float c[4];
    float s, t;
    float fog;                  /* 1: no fog */
} V;

typedef struct tex {
    int w, h;
    unsigned int* px;           /* 0xAARRGGBB, top row = t 0 */
    GLenum minf, magf, ws, wt;
    int alive;
} TEX;

typedef struct {                /* what a primitive is drawn with */
    unsigned char depth_test, depth_write, blend, alpha_test, smooth, fog;
    unsigned char cmask[4];
    GLenum depth_func, sfac, dfac, alpha_func, env;
    float alpha_ref;
    float fog_color[3];
    float env_color[4];
    TEX* tex;
    int x0, y0, x1, y1;         /* drawable rectangle (scissor), buffer rows */
} RS;

typedef struct {                /* a primitive in buffer coordinates */
    float x[3], y[3], z[3], iw[3];
    float c[3][4];
    float st[3][2];
    float fog[3];
    float size;
    int rs;
    int kind;                   /* 1 point, 2 line, 3 triangle */
} P;

typedef struct { int op; unsigned a, b; float f[16]; } LOP;
typedef struct { LOP* ops; int n, cap, defined; } LIST;

typedef struct {
    float amb[4], dif[4], spe[4], pos[4];   /* pos: eye coordinates */
    float dir[3], spot_exp, spot_cut, att[3];
    int on;
} LIGHT;

typedef struct {
    int on, size, stride;
    GLenum type;
    const void* ptr;
} ARR;

struct bgl_ctx {
    int W, H;
    unsigned int* color;
    float* depth;
    bwin_t* win;
    GLenum err;
    /* state */
    float clear_c[4], clear_d;
    int depth_test, depth_write, blend, alpha_test, cull, lighting, color_material, fog_on, normalize;
    int tex2d, scissor_on, smooth;
    unsigned char cmask[4];
    GLenum depth_func, sfac, dfac, alpha_func, cull_face, front_face, cm_mode, poly_front, poly_back;
    GLenum fog_mode, env;
    float alpha_ref, fog_start, fog_end, fog_density, fog_color[4], env_color[4];
    float point_size, line_width, poly_factor, poly_units;
    int vx, vy, vw, vh;
    int sx, sy, sw, sh;
    float dnear, dfar;
    int unpack_align, pack_align, unpack_row;
    /* matrices */
    M4 mv[STACK], pr[STACK], tm[STACK];
    int nmv, npr, ntm;
    GLenum mmode;
    float nm[9];
    int nm_dirty, tm_ident;
    /* current attributes */
    float cur_c[4], cur_n[3], cur_st[2];
    /* lighting */
    LIGHT lt[NLIGHTS];
    float model_amb[4];
    float m_amb[4], m_dif[4], m_spe[4], m_emi[4], m_shin;
    /* primitive assembly */
    int in_begin;
    GLenum prim;
    V a, b, first, quad[4];
    int count, nq4;
    V* poly;                    /* GL_POLYGON / LINE_LOOP outline in line mode */
    int npoly, polycap;
    /* the queue */
    P* q;
    int nq, qcap;
    RS* rs;
    int nrs, rscap, rs_dirty;
    /* textures */
    TEX** texs;
    int ntex;
    GLuint bound;
    /* arrays */
    ARR va, ca, na, ta;
    /* lists */
    LIST* lists;
    int nlists;
    int rec;                    /* recording into list rec_list */
    GLuint rec_list;
    int rec_exec;               /* COMPILE_AND_EXECUTE */
    GLuint list_base;
};

static bgl_ctx_t* C;           /* the current context */

static void seterr(GLenum e) { if (C && !C->err) C->err = e; }

/* ══ threads: a pool rendering bands of rows ═════════════════════════ */

static int g_nthr = 0;
static bgl_ctx_t* g_job;
#ifdef BGL_HOST
static sem_t g_start[MAXTHR], g_done;
static int host_cpus(void) { long n = sysconf(_SC_NPROCESSORS_ONLN); return n < 1 ? 1 : (int)n; }
#define SEM_POST(s) sem_post(&(s))
#define SEM_WAIT(s) sem_wait(&(s))
#else
static banana_sem_t g_start[MAXTHR], g_done;
#define SEM_POST(s) banana_sem_post(&(s))
#define SEM_WAIT(s) banana_sem_wait(&(s))
#endif

static void render_band(bgl_ctx_t* c, int k, int n);

#ifdef BGL_HOST
static void* worker(void* arg)
#else
static int worker(void* arg)
#endif
{
    int k = (int)(long)arg;
    for (;;) {
        SEM_WAIT(g_start[k]);
        render_band(g_job, k, g_nthr);
        SEM_POST(g_done);
    }
    return 0;
}

static void pool_start(void) {
    if (g_nthr) return;
#ifdef BGL_HOST
    int n = getenv("BGL_THREADS") ? atoi(getenv("BGL_THREADS")) : host_cpus();
#else
    int n = banana_cpus();
#endif
    if (n < 1) n = 1;
    if (n > MAXTHR) n = MAXTHR;
    g_nthr = 1;
#ifdef BGL_HOST
    sem_init(&g_done, 0, 0);
#else
    banana_sem_init(&g_done, 0);
#endif
    for (int k = 1; k < n; k++) {
#ifdef BGL_HOST
        sem_init(&g_start[k], 0, 0);
        pthread_t t;
        if (pthread_create(&t, NULL, worker, (void*)(long)k) != 0) break;
        pthread_detach(t);
#else
        banana_sem_init(&g_start[k], 0);
        if (banana_thread(worker, (void*)(long)k) < 0) break;
#endif
        g_nthr++;
    }
}

int bgl_threads(void) { pool_start(); return g_nthr; }

/* renders the queue (every core its rows), empties it */
static void flush(bgl_ctx_t* c) {
    if (!c || !c->nq) return;
    pool_start();
    if (g_nthr <= 1 || c->nq * 8 < g_nthr) {
        int n = g_nthr <= 1 ? 1 : 1;
        render_band(c, 0, n);
    } else {
        g_job = c;
        for (int k = 1; k < g_nthr; k++) SEM_POST(g_start[k]);
        render_band(c, 0, g_nthr);
        for (int k = 1; k < g_nthr; k++) SEM_WAIT(g_done);
    }
    c->nq = 0;
    c->nrs = 0;
    c->rs_dirty = 1;
}

/* ══ matrices ════════════════════════════════════════════════════════ */

static void m_ident(M4* a) { memset(a, 0, sizeof(*a)); a->m[0] = a->m[5] = a->m[10] = a->m[15] = 1; }

static void m_mul(M4* r, const M4* a, const M4* b) {
    M4 t;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++)
            t.m[c * 4 + rr] = a->m[0 * 4 + rr] * b->m[c * 4 + 0] + a->m[1 * 4 + rr] * b->m[c * 4 + 1] +
                              a->m[2 * 4 + rr] * b->m[c * 4 + 2] + a->m[3 * 4 + rr] * b->m[c * 4 + 3];
    *r = t;
}

static void m_xform(const M4* a, const float v[4], float o[4]) {
    for (int r = 0; r < 4; r++) o[r] = a->m[r] * v[0] + a->m[4 + r] * v[1] + a->m[8 + r] * v[2] + a->m[12 + r] * v[3];
}

static M4* top(void) {
    if (C->mmode == GL_PROJECTION) return &C->pr[C->npr];
    if (C->mmode == GL_TEXTURE) return &C->tm[C->ntm];
    return &C->mv[C->nmv];
}

static void m_changed(void) {
    if (C->mmode == GL_MODELVIEW) C->nm_dirty = 1;
    if (C->mmode == GL_TEXTURE) {
        M4 id;
        m_ident(&id);
        C->tm_ident = !memcmp(&id, &C->tm[C->ntm], sizeof(M4));
    }
}

/* the normal matrix: inverse transpose of the modelview's 3x3 */
static void update_nm(void) {
    if (!C->nm_dirty) return;
    const float* m = C->mv[C->nmv].m;
    float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    float A = e * i - f * h, B = -(d * i - f * g), Cc = d * h - e * g;
    float D = -(b * i - c * h), E = a * i - c * g, F = -(a * h - b * g);
    float G = b * f - c * e, H = -(a * f - c * d), I = a * e - b * d;
    float det = a * A + b * B + c * Cc;
    float id = det != 0 ? 1.0f / det : 0;
    /* (inverse)^T = cofactor matrix / det; rows of nm multiply the normal */
    C->nm[0] = A * id; C->nm[1] = B * id; C->nm[2] = Cc * id;
    C->nm[3] = D * id; C->nm[4] = E * id; C->nm[5] = F * id;
    C->nm[6] = G * id; C->nm[7] = H * id; C->nm[8] = I * id;
    C->nm_dirty = 0;
}

/* ══ display lists: recording ═══════════════════════════════════════ */

enum { L_VERTEX = 1, L_COLOR, L_NORMAL, L_TEXCOORD, L_BEGIN, L_END, L_MATERIAL, L_TRANSLATE, L_ROTATE, L_SCALE,
       L_PUSH, L_POP, L_MULT, L_LOADID, L_SHADE, L_ENABLE, L_DISABLE, L_BIND, L_CALL, L_LIGHT, L_MMODE,
       L_LINEW, L_POINTSIZE, L_LOADM, L_BLEND, L_DEPTHF, L_CMODE };

static void rec_op(int op, unsigned a, unsigned b, const float* f, int nf) {
    LIST* l = &C->lists[C->rec_list];
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 64;
        LOP* n = (LOP*)realloc(l->ops, sizeof(LOP) * (size_t)cap);
        if (!n) { seterr(GL_OUT_OF_MEMORY); return; }
        l->ops = n;
        l->cap = cap;
    }
    LOP* o = &l->ops[l->n++];
    o->op = op;
    o->a = a;
    o->b = b;
    for (int i = 0; i < nf && i < 16; i++) o->f[i] = f[i];
}

/* in a function that a list records: record it, and stop unless it also runs */
#define REC(op, a, b, f, nf) do { if (C->rec) { rec_op(op, a, b, f, nf); if (!C->rec_exec) return; } } while (0)
#define NEED_CTX(...) do { if (!C) return __VA_ARGS__; } while (0)

/* ══ state ═══════════════════════════════════════════════════════════ */

static void state_changed(void) { if (C) C->rs_dirty = 1; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static float clampf(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    NEED_CTX();
    C->clear_c[0] = clampf(r); C->clear_c[1] = clampf(g); C->clear_c[2] = clampf(b); C->clear_c[3] = clampf(a);
}
void glClearDepth(GLclampd d) { NEED_CTX(); C->clear_d = clampf((float)d); }
void glClearDepthf(GLclampf d) { glClearDepth(d); }

static unsigned int pack(const float c[4]) {
    return (unsigned int)(c[3] * 255.0f + 0.5f) << 24 | (unsigned int)(c[0] * 255.0f + 0.5f) << 16 |
           (unsigned int)(c[1] * 255.0f + 0.5f) << 8 | (unsigned int)(c[2] * 255.0f + 0.5f);
}

void glClear(GLbitfield mask) {
    NEED_CTX();
    if (C->in_begin) { seterr(GL_INVALID_OPERATION); return; }
    flush(C);
    int x0 = 0, y0 = 0, x1 = C->W, y1 = C->H;
    if (C->scissor_on) {
        x0 = clampi(C->sx, 0, C->W);
        x1 = clampi(C->sx + C->sw, 0, C->W);
        y0 = clampi(C->H - (C->sy + C->sh), 0, C->H);
        y1 = clampi(C->H - C->sy, 0, C->H);
    }
    if (mask & GL_COLOR_BUFFER_BIT) {
        unsigned int v = pack(C->clear_c);
        unsigned int keep = 0;
        if (!C->cmask[0]) keep |= 0x00FF0000u;
        if (!C->cmask[1]) keep |= 0x0000FF00u;
        if (!C->cmask[2]) keep |= 0x000000FFu;
        if (!C->cmask[3]) keep |= 0xFF000000u;
        for (int y = y0; y < y1; y++) {
            unsigned int* row = C->color + (long)y * C->W;
            if (!keep) for (int x = x0; x < x1; x++) row[x] = v;
            else for (int x = x0; x < x1; x++) row[x] = (row[x] & keep) | (v & ~keep);
        }
    }
    if ((mask & GL_DEPTH_BUFFER_BIT) && C->depth_write) {
        for (int y = y0; y < y1; y++) {
            float* row = C->depth + (long)y * C->W;
            for (int x = x0; x < x1; x++) row[x] = C->clear_d;
        }
    }
}

static int* cap_flag(GLenum cap) {
    switch (cap) {
    case GL_DEPTH_TEST: return &C->depth_test;
    case GL_BLEND: return &C->blend;
    case GL_ALPHA_TEST: return &C->alpha_test;
    case GL_CULL_FACE: return &C->cull;
    case GL_LIGHTING: return &C->lighting;
    case GL_COLOR_MATERIAL: return &C->color_material;
    case GL_FOG: return &C->fog_on;
    case GL_NORMALIZE: case GL_RESCALE_NORMAL: return &C->normalize;
    case GL_TEXTURE_2D: return &C->tex2d;
    case GL_SCISSOR_TEST: return &C->scissor_on;
    }
    if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + NLIGHTS) return &C->lt[cap - GL_LIGHT0].on;
    return NULL;
}

static void enable(GLenum cap, int on) {
    int* f = cap_flag(cap);
    if (f) { *f = on; state_changed(); return; }
    switch (cap) {   /* known, nothing to do */
    case GL_DITHER: case GL_POINT_SMOOTH: case GL_LINE_SMOOTH: case GL_POLYGON_SMOOTH: case GL_LINE_STIPPLE:
    case GL_TEXTURE_1D: case GL_POLYGON_OFFSET_FILL: case GL_STENCIL_TEST: case GL_TEXTURE_GEN_S: case GL_TEXTURE_GEN_T:
        return;
    }
    seterr(GL_INVALID_ENUM);
}

void glEnable(GLenum cap) { NEED_CTX(); float f = 0; REC(L_ENABLE, cap, 0, &f, 0); enable(cap, 1); }
void glDisable(GLenum cap) { NEED_CTX(); float f = 0; REC(L_DISABLE, cap, 0, &f, 0); enable(cap, 0); }
GLboolean glIsEnabled(GLenum cap) { NEED_CTX(0); int* f = cap_flag(cap); return f && *f; }

void glDepthFunc(GLenum func) { NEED_CTX(); float f = 0; REC(L_DEPTHF, func, 0, &f, 0); C->depth_func = func; state_changed(); }
void glDepthMask(GLboolean flag) { NEED_CTX(); C->depth_write = flag != 0; state_changed(); }
void glDepthRange(GLclampd n, GLclampd f) { NEED_CTX(); C->dnear = clampf((float)n); C->dfar = clampf((float)f); }
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    NEED_CTX(); C->cmask[0] = r != 0; C->cmask[1] = g != 0; C->cmask[2] = b != 0; C->cmask[3] = a != 0; state_changed();
}
void glCullFace(GLenum mode) { NEED_CTX(); C->cull_face = mode; }
void glFrontFace(GLenum mode) { NEED_CTX(); C->front_face = mode; }
void glBlendFunc(GLenum s, GLenum d) { NEED_CTX(); float f = 0; REC(L_BLEND, s, d, &f, 0); C->sfac = s; C->dfac = d; state_changed(); }
void glAlphaFunc(GLenum func, GLclampf ref) { NEED_CTX(); C->alpha_func = func; C->alpha_ref = clampf(ref); state_changed(); }
void glShadeModel(GLenum mode) { NEED_CTX(); float f = 0; REC(L_SHADE, mode, 0, &f, 0); C->smooth = mode != GL_FLAT; state_changed(); }
void glPolygonMode(GLenum face, GLenum mode) {
    NEED_CTX();
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) C->poly_front = mode;
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) C->poly_back = mode;
}
void glPolygonOffset(GLfloat factor, GLfloat units) { NEED_CTX(); C->poly_factor = factor; C->poly_units = units; }
void glPointSize(GLfloat size) { NEED_CTX(); REC(L_POINTSIZE, 0, 0, &size, 1); C->point_size = size < 1 ? 1 : size; }
void glLineWidth(GLfloat width) { NEED_CTX(); REC(L_LINEW, 0, 0, &width, 1); C->line_width = width < 1 ? 1 : width; }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    NEED_CTX();
    if (w < 0 || h < 0) { seterr(GL_INVALID_VALUE); return; }
    C->vx = x; C->vy = y; C->vw = w; C->vh = h;
}
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { NEED_CTX(); C->sx = x; C->sy = y; C->sw = w; C->sh = h; state_changed(); }
void glHint(GLenum target, GLenum mode) { (void)target; (void)mode; }

void glFogf(GLenum pname, GLfloat p) {
    NEED_CTX();
    switch (pname) {
    case GL_FOG_MODE: C->fog_mode = (GLenum)p; break;
    case GL_FOG_DENSITY: C->fog_density = p; break;
    case GL_FOG_START: C->fog_start = p; break;
    case GL_FOG_END: C->fog_end = p; break;
    default: seterr(GL_INVALID_ENUM);
    }
}
void glFogi(GLenum pname, GLint p) { if (pname == GL_FOG_MODE) { NEED_CTX(); C->fog_mode = (GLenum)p; } else glFogf(pname, (float)p); }
void glFogfv(GLenum pname, const GLfloat* p) {
    NEED_CTX();
    if (pname == GL_FOG_COLOR) { for (int i = 0; i < 4; i++) C->fog_color[i] = clampf(p[i]); state_changed(); }
    else if (pname == GL_FOG_MODE) C->fog_mode = (GLenum)p[0];
    else glFogf(pname, p[0]);
}

GLenum glGetError(void) { NEED_CTX(GL_INVALID_OPERATION); GLenum e = C->err; C->err = GL_NO_ERROR; return e; }

const GLubyte* glGetString(GLenum name) {
    static char renderer[64];
    switch (name) {
    case GL_VENDOR: return (const GLubyte*)"Banana OS";
    case GL_RENDERER:
        snprintf(renderer, sizeof(renderer), "Banana OS softpipe (%d core%s)", bgl_threads(), bgl_threads() == 1 ? "" : "s");
        return (const GLubyte*)renderer;
    case GL_VERSION: return (const GLubyte*)"1.3 Banana OS";
    case GL_EXTENSIONS: return (const GLubyte*)"GL_EXT_bgra GL_SGIS_texture_edge_clamp GL_ARB_texture_mirrored_repeat";
    }
    seterr(GL_INVALID_ENUM);
    return (const GLubyte*)"";
}

static int get_values(GLenum pname, float* v) {
    switch (pname) {
    case GL_VIEWPORT: v[0] = (float)C->vx; v[1] = (float)C->vy; v[2] = (float)C->vw; v[3] = (float)C->vh; return 4;
    case GL_MODELVIEW_MATRIX: memcpy(v, C->mv[C->nmv].m, 64); return 16;
    case GL_PROJECTION_MATRIX: memcpy(v, C->pr[C->npr].m, 64); return 16;
    case GL_TEXTURE_MATRIX: memcpy(v, C->tm[C->ntm].m, 64); return 16;
    case GL_MAX_TEXTURE_SIZE: v[0] = 4096; return 1;
    case GL_MAX_LIGHTS: v[0] = NLIGHTS; return 1;
    case GL_MAX_MODELVIEW_STACK_DEPTH: case GL_MAX_PROJECTION_STACK_DEPTH: v[0] = STACK; return 1;
    case GL_MAX_VIEWPORT_DIMS: v[0] = v[1] = 8192; return 2;
    case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: v[0] = 8; return 1;
    case GL_DEPTH_BITS: v[0] = 24; return 1;
    case GL_STENCIL_BITS: v[0] = 0; return 1;
    case GL_DOUBLEBUFFER: v[0] = 1; return 1;
    case GL_MATRIX_MODE: v[0] = (float)C->mmode; return 1;
    case GL_SHADE_MODEL: v[0] = C->smooth ? GL_SMOOTH : GL_FLAT; return 1;
    case GL_DEPTH_FUNC: v[0] = (float)C->depth_func; return 1;
    case GL_TEXTURE_BINDING_2D: v[0] = (float)C->bound; return 1;
    case GL_POINT_SIZE: v[0] = C->point_size; return 1;
    case GL_LINE_WIDTH: v[0] = C->line_width; return 1;
    case GL_CURRENT_COLOR: memcpy(v, C->cur_c, 16); return 4;
    case GL_COLOR_CLEAR_VALUE: memcpy(v, C->clear_c, 16); return 4;
    case GL_UNPACK_ALIGNMENT: v[0] = (float)C->unpack_align; return 1;
    case GL_PACK_ALIGNMENT: v[0] = (float)C->pack_align; return 1;
    }
    int* f = cap_flag(pname);
    if (f) { v[0] = (float)*f; return 1; }
    seterr(GL_INVALID_ENUM);
    return 0;
}
void glGetFloatv(GLenum p, GLfloat* out) { NEED_CTX(); float v[16]; int n = get_values(p, v); for (int i = 0; i < n; i++) out[i] = v[i]; }
void glGetDoublev(GLenum p, GLdouble* out) { NEED_CTX(); float v[16]; int n = get_values(p, v); for (int i = 0; i < n; i++) out[i] = v[i]; }
void glGetIntegerv(GLenum p, GLint* out) { NEED_CTX(); float v[16]; int n = get_values(p, v); for (int i = 0; i < n; i++) out[i] = (GLint)v[i]; }
void glGetBooleanv(GLenum p, GLboolean* out) { NEED_CTX(); float v[16]; int n = get_values(p, v); for (int i = 0; i < n; i++) out[i] = v[i] != 0; }

void glFlush(void) { NEED_CTX(); flush(C); }
void glFinish(void) { NEED_CTX(); flush(C); }

/* ══ matrix calls ════════════════════════════════════════════════════ */

void glMatrixMode(GLenum mode) {
    NEED_CTX(); float f = 0; REC(L_MMODE, mode, 0, &f, 0);
    if (mode != GL_MODELVIEW && mode != GL_PROJECTION && mode != GL_TEXTURE) { seterr(GL_INVALID_ENUM); return; }
    C->mmode = mode;
}
void glLoadIdentity(void) { NEED_CTX(); float f = 0; REC(L_LOADID, 0, 0, &f, 0); m_ident(top()); m_changed(); }
void glLoadMatrixf(const GLfloat* m) { NEED_CTX(); REC(L_LOADM, 0, 0, m, 16); memcpy(top()->m, m, 64); m_changed(); }
void glLoadMatrixd(const GLdouble* m) { float f[16]; for (int i = 0; i < 16; i++) f[i] = (float)m[i]; glLoadMatrixf(f); }
void glMultMatrixf(const GLfloat* m) {
    NEED_CTX(); REC(L_MULT, 0, 0, m, 16);
    M4 b;
    memcpy(b.m, m, 64);
    m_mul(top(), top(), &b);
    m_changed();
}
void glMultMatrixd(const GLdouble* m) { float f[16]; for (int i = 0; i < 16; i++) f[i] = (float)m[i]; glMultMatrixf(f); }
void glPushMatrix(void) {
    NEED_CTX(); float f = 0; REC(L_PUSH, 0, 0, &f, 0);
    int* n = C->mmode == GL_PROJECTION ? &C->npr : C->mmode == GL_TEXTURE ? &C->ntm : &C->nmv;
    M4* s = C->mmode == GL_PROJECTION ? C->pr : C->mmode == GL_TEXTURE ? C->tm : C->mv;
    if (*n + 1 >= STACK) { seterr(GL_STACK_OVERFLOW); return; }
    s[*n + 1] = s[*n];
    (*n)++;
}
void glPopMatrix(void) {
    NEED_CTX(); float f = 0; REC(L_POP, 0, 0, &f, 0);
    int* n = C->mmode == GL_PROJECTION ? &C->npr : C->mmode == GL_TEXTURE ? &C->ntm : &C->nmv;
    if (*n == 0) { seterr(GL_STACK_UNDERFLOW); return; }
    (*n)--;
    m_changed();
}
static void mult_with(const M4* b) { m_mul(top(), top(), b); m_changed(); }
void glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    NEED_CTX(); float f[3] = { x, y, z }; REC(L_TRANSLATE, 0, 0, f, 3);
    M4 t; m_ident(&t); t.m[12] = x; t.m[13] = y; t.m[14] = z; mult_with(&t);
}
void glTranslated(GLdouble x, GLdouble y, GLdouble z) { glTranslatef((float)x, (float)y, (float)z); }
void glScalef(GLfloat x, GLfloat y, GLfloat z) {
    NEED_CTX(); float f[3] = { x, y, z }; REC(L_SCALE, 0, 0, f, 3);
    M4 t; m_ident(&t); t.m[0] = x; t.m[5] = y; t.m[10] = z; mult_with(&t);
}
void glScaled(GLdouble x, GLdouble y, GLdouble z) { glScalef((float)x, (float)y, (float)z); }
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    NEED_CTX(); float f[4] = { angle, x, y, z }; REC(L_ROTATE, 0, 0, f, 4);
    float len = sqrtf(x * x + y * y + z * z);
    if (len == 0) return;
    x /= len; y /= len; z /= len;
    float a = angle * 3.14159265358979f / 180.0f, c = cosf(a), s = sinf(a), t = 1 - c;
    M4 r;
    m_ident(&r);
    r.m[0] = t * x * x + c;     r.m[4] = t * x * y - s * z; r.m[8] = t * x * z + s * y;
    r.m[1] = t * x * y + s * z; r.m[5] = t * y * y + c;     r.m[9] = t * y * z - s * x;
    r.m[2] = t * x * z - s * y; r.m[6] = t * y * z + s * x; r.m[10] = t * z * z + c;
    mult_with(&r);
}
void glRotated(GLdouble a, GLdouble x, GLdouble y, GLdouble z) { glRotatef((float)a, (float)x, (float)y, (float)z); }
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
    NEED_CTX();
    if (l == r || b == t || n == f) { seterr(GL_INVALID_VALUE); return; }
    float m[16] = { 0 };
    m[0] = (float)(2 / (r - l)); m[5] = (float)(2 / (t - b)); m[10] = (float)(-2 / (f - n));
    m[12] = (float)(-(r + l) / (r - l)); m[13] = (float)(-(t + b) / (t - b)); m[14] = (float)(-(f + n) / (f - n)); m[15] = 1;
    glMultMatrixf(m);
}
void glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
    NEED_CTX();
    if (n <= 0 || f <= 0 || l == r || b == t || n == f) { seterr(GL_INVALID_VALUE); return; }
    float m[16] = { 0 };
    m[0] = (float)(2 * n / (r - l)); m[5] = (float)(2 * n / (t - b));
    m[8] = (float)((r + l) / (r - l)); m[9] = (float)((t + b) / (t - b)); m[10] = (float)(-(f + n) / (f - n)); m[11] = -1;
    m[14] = (float)(-2 * f * n / (f - n));
    glMultMatrixf(m);
}

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zn, GLdouble zf) {
    double ymax = zn * tan(fovy * 3.14159265358979 / 360.0);
    glFrustum(-ymax * aspect, ymax * aspect, -ymax, ymax, zn, zf);
}
void gluOrtho2D(GLdouble l, GLdouble r, GLdouble b, GLdouble t) { glOrtho(l, r, b, t, -1, 1); }
void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez, GLdouble cx, GLdouble cy, GLdouble cz, GLdouble ux, GLdouble uy, GLdouble uz) {
    float f[3] = { (float)(cx - ex), (float)(cy - ey), (float)(cz - ez) };
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl == 0) return;
    for (int i = 0; i < 3; i++) f[i] /= fl;
    float u[3] = { (float)ux, (float)uy, (float)uz };
    float s[3] = { f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2], f[0] * u[1] - f[1] * u[0] };
    float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (sl == 0) return;
    for (int i = 0; i < 3; i++) s[i] /= sl;
    float uu[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
    float m[16] = { s[0], uu[0], -f[0], 0, s[1], uu[1], -f[1], 0, s[2], uu[2], -f[2], 0, 0, 0, 0, 1 };
    glMultMatrixf(m);
    glTranslatef((float)-ex, (float)-ey, (float)-ez);
}
const GLubyte* gluErrorString(GLenum e) {
    switch (e) {
    case GL_NO_ERROR: return (const GLubyte*)"no error";
    case GL_INVALID_ENUM: return (const GLubyte*)"invalid enumerant";
    case GL_INVALID_VALUE: return (const GLubyte*)"invalid value";
    case GL_INVALID_OPERATION: return (const GLubyte*)"invalid operation";
    case GL_STACK_OVERFLOW: return (const GLubyte*)"stack overflow";
    case GL_STACK_UNDERFLOW: return (const GLubyte*)"stack underflow";
    case GL_OUT_OF_MEMORY: return (const GLubyte*)"out of memory";
    }
    return (const GLubyte*)"unknown error";
}

/* ══ lighting ════════════════════════════════════════════════════════ */

static LIGHT* light_of(GLenum l) {
    if (l < GL_LIGHT0 || l >= GL_LIGHT0 + NLIGHTS) { seterr(GL_INVALID_ENUM); return NULL; }
    return &C->lt[l - GL_LIGHT0];
}

void glLightfv(GLenum light, GLenum pname, const GLfloat* p) {
    NEED_CTX(); REC(L_LIGHT, light, pname, p, pname == GL_POSITION || pname <= GL_SPECULAR ? 4 : pname == GL_SPOT_DIRECTION ? 3 : 1);
    LIGHT* L = light_of(light);
    if (!L) return;
    float v[4];
    switch (pname) {
    case GL_AMBIENT: memcpy(L->amb, p, 16); break;
    case GL_DIFFUSE: memcpy(L->dif, p, 16); break;
    case GL_SPECULAR: memcpy(L->spe, p, 16); break;
    case GL_POSITION: m_xform(&C->mv[C->nmv], p, v); memcpy(L->pos, v, 16); break;   /* kept in eye coordinates */
    case GL_SPOT_DIRECTION: {
        update_nm();
        float in[4] = { p[0], p[1], p[2], 0 };
        m_xform(&C->mv[C->nmv], in, v);
        memcpy(L->dir, v, 12);
        break;
    }
    case GL_SPOT_EXPONENT: L->spot_exp = p[0]; break;
    case GL_SPOT_CUTOFF: L->spot_cut = p[0]; break;
    case GL_CONSTANT_ATTENUATION: L->att[0] = p[0]; break;
    case GL_LINEAR_ATTENUATION: L->att[1] = p[0]; break;
    case GL_QUADRATIC_ATTENUATION: L->att[2] = p[0]; break;
    default: seterr(GL_INVALID_ENUM);
    }
}
void glLightf(GLenum light, GLenum pname, GLfloat p) { glLightfv(light, pname, &p); }
void glLighti(GLenum light, GLenum pname, GLint p) { float f = (float)p; glLightfv(light, pname, &f); }
void glLightModelfv(GLenum pname, const GLfloat* p) {
    NEED_CTX();
    if (pname == GL_LIGHT_MODEL_AMBIENT) memcpy(C->model_amb, p, 16);
    else if (pname != GL_LIGHT_MODEL_TWO_SIDE && pname != GL_LIGHT_MODEL_LOCAL_VIEWER) seterr(GL_INVALID_ENUM);
}
void glLightModelf(GLenum pname, GLfloat p) { float v[4] = { p, p, p, p }; glLightModelfv(pname, v); }
void glLightModeli(GLenum pname, GLint p) { glLightModelf(pname, (float)p); }

void glMaterialfv(GLenum face, GLenum pname, const GLfloat* p) {
    NEED_CTX(); REC(L_MATERIAL, face, pname, p, pname == GL_SHININESS ? 1 : 4);
    if (face == GL_BACK) return;                  /* one-sided lighting: the front material */
    switch (pname) {
    case GL_AMBIENT: memcpy(C->m_amb, p, 16); break;
    case GL_DIFFUSE: memcpy(C->m_dif, p, 16); break;
    case GL_SPECULAR: memcpy(C->m_spe, p, 16); break;
    case GL_EMISSION: memcpy(C->m_emi, p, 16); break;
    case GL_SHININESS: C->m_shin = p[0] < 0 ? 0 : p[0] > 128 ? 128 : p[0]; break;
    case GL_AMBIENT_AND_DIFFUSE: memcpy(C->m_amb, p, 16); memcpy(C->m_dif, p, 16); break;
    default: seterr(GL_INVALID_ENUM);
    }
}
void glMaterialf(GLenum face, GLenum pname, GLfloat p) { float v[4] = { p, p, p, p }; glMaterialfv(face, pname, v); }
void glMateriali(GLenum face, GLenum pname, GLint p) { glMaterialf(face, pname, (float)p); }
void glColorMaterial(GLenum face, GLenum mode) { NEED_CTX(); float f = 0; (void)face; REC(L_CMODE, mode, 0, &f, 0); C->cm_mode = mode; }

/* the color of a lit vertex (eye position e, eye normal n) */
static void shade(const float e[4], const float n[3], float out[4]) {
    const float *amb = C->m_amb, *dif = C->m_dif, *spe = C->m_spe, *emi = C->m_emi;
    if (C->color_material) {
        switch (C->cm_mode) {
        case GL_AMBIENT: amb = C->cur_c; break;
        case GL_DIFFUSE: dif = C->cur_c; break;
        case GL_SPECULAR: spe = C->cur_c; break;
        case GL_EMISSION: emi = C->cur_c; break;
        default: amb = dif = C->cur_c; break;
        }
    }
    float r = emi[0] + C->model_amb[0] * amb[0];
    float g = emi[1] + C->model_amb[1] * amb[1];
    float b = emi[2] + C->model_amb[2] * amb[2];
    float ew = e[3] != 0 ? e[3] : 1;
    float ex = e[0] / ew, ey = e[1] / ew, ez = e[2] / ew;
    for (int i = 0; i < NLIGHTS; i++) {
        LIGHT* L = &C->lt[i];
        if (!L->on) continue;
        float lx, ly, lz, att = 1;
        if (L->pos[3] == 0) { lx = L->pos[0]; ly = L->pos[1]; lz = L->pos[2]; }
        else {
            lx = L->pos[0] / L->pos[3] - ex; ly = L->pos[1] / L->pos[3] - ey; lz = L->pos[2] / L->pos[3] - ez;
            float d = sqrtf(lx * lx + ly * ly + lz * lz);
            float den = L->att[0] + L->att[1] * d + L->att[2] * d * d;
            att = den > 0 ? 1.0f / den : 1;
        }
        float ll = sqrtf(lx * lx + ly * ly + lz * lz);
        if (ll > 0) { lx /= ll; ly /= ll; lz /= ll; }
        if (L->spot_cut != 180 && L->pos[3] != 0) {
            float dl = sqrtf(L->dir[0] * L->dir[0] + L->dir[1] * L->dir[1] + L->dir[2] * L->dir[2]);
            float cs = dl > 0 ? -(lx * L->dir[0] + ly * L->dir[1] + lz * L->dir[2]) / dl : 1;
            if (cs < cosf(L->spot_cut * 3.14159265f / 180.0f)) continue;
            att *= powf(cs > 0 ? cs : 0, L->spot_exp);
        }
        float nl = n[0] * lx + n[1] * ly + n[2] * lz;
        float cr = L->amb[0] * amb[0], cg = L->amb[1] * amb[1], cb = L->amb[2] * amb[2];
        if (nl > 0) {
            cr += nl * L->dif[0] * dif[0]; cg += nl * L->dif[1] * dif[1]; cb += nl * L->dif[2] * dif[2];
            float hx = lx, hy = ly, hz = lz + 1;            /* the half vector (viewer far away) */
            float hl = sqrtf(hx * hx + hy * hy + hz * hz);
            float nh = hl > 0 ? (n[0] * hx + n[1] * hy + n[2] * hz) / hl : 0;
            if (nh > 0 && (spe[0] > 0 || spe[1] > 0 || spe[2] > 0)) {
                float sp = C->m_shin > 0 ? powf(nh, C->m_shin) : 1;
                cr += sp * L->spe[0] * spe[0]; cg += sp * L->spe[1] * spe[1]; cb += sp * L->spe[2] * spe[2];
            }
        }
        r += att * cr; g += att * cg; b += att * cb;
    }
    out[0] = clampf(r); out[1] = clampf(g); out[2] = clampf(b); out[3] = clampf(dif[3]);
}

/* ══ textures ════════════════════════════════════════════════════════ */

static TEX* tex_get(GLuint id, int create) {
    if (id == 0) return NULL;
    if ((int)id >= C->ntex) {
        if (!create) return NULL;
        int n = (int)id + 16;
        TEX** t = (TEX**)realloc(C->texs, sizeof(TEX*) * (size_t)n);
        if (!t) return NULL;
        for (int i = C->ntex; i < n; i++) t[i] = NULL;
        C->texs = t;
        C->ntex = n;
    }
    if (!C->texs[id] && create) {
        TEX* t = (TEX*)calloc(1, sizeof(TEX));
        if (!t) return NULL;
        t->minf = GL_NEAREST_MIPMAP_LINEAR;
        t->magf = GL_LINEAR;
        t->ws = t->wt = GL_REPEAT;
        t->alive = 1;
        C->texs[id] = t;
    }
    return C->texs[id];
}

void glGenTextures(GLsizei n, GLuint* out) {
    NEED_CTX();
    GLuint id = 1;
    for (int i = 0; i < n; i++) {
        while ((int)id < C->ntex && C->texs[id]) id++;
        tex_get(id, 1);
        out[i] = id++;
    }
}
void glDeleteTextures(GLsizei n, const GLuint* ids) {
    NEED_CTX();
    flush(C);
    for (int i = 0; i < n; i++) {
        TEX* t = tex_get(ids[i], 0);
        if (!t) continue;
        free(t->px);
        free(t);
        C->texs[ids[i]] = NULL;
        if (C->bound == ids[i]) C->bound = 0;
    }
    state_changed();
}
void glBindTexture(GLenum target, GLuint id) {
    NEED_CTX(); float f = 0; REC(L_BIND, target, id, &f, 0);
    if (target != GL_TEXTURE_2D) { if (target != GL_TEXTURE_1D) seterr(GL_INVALID_ENUM); return; }
    tex_get(id, 1);
    C->bound = id;
    state_changed();
}
GLboolean glIsTexture(GLuint id) { NEED_CTX(0); return tex_get(id, 0) != NULL; }

static int comps_of(GLenum format) {
    switch (format) {
    case GL_RGB: case GL_BGR: return 3;
    case GL_RGBA: case GL_BGRA: return 4;
    case GL_LUMINANCE: case GL_ALPHA: case GL_RED: return 1;
    case GL_LUMINANCE_ALPHA: return 2;
    }
    return 0;
}

/* source pixels (format/type) into 0xAARRGGBB */
static void unpack_row(const unsigned char* s, GLenum format, GLenum type, int n, unsigned int* out) {
    int k = comps_of(format);
    for (int i = 0; i < n; i++) {
        float v[4] = { 0, 0, 0, 1 };
        float c[4];
        for (int j = 0; j < k; j++) {
            if (type == GL_FLOAT) c[j] = clampf(((const float*)s)[i * k + j]);
            else c[j] = s[i * k + j] / 255.0f;
        }
        switch (format) {
        case GL_RGB: v[0] = c[0]; v[1] = c[1]; v[2] = c[2]; break;
        case GL_BGR: v[0] = c[2]; v[1] = c[1]; v[2] = c[0]; break;
        case GL_RGBA: v[0] = c[0]; v[1] = c[1]; v[2] = c[2]; v[3] = c[3]; break;
        case GL_BGRA: v[0] = c[2]; v[1] = c[1]; v[2] = c[0]; v[3] = c[3]; break;
        case GL_LUMINANCE: case GL_RED: v[0] = v[1] = v[2] = c[0]; break;
        case GL_ALPHA: v[0] = v[1] = v[2] = 1; v[3] = c[0]; break;
        case GL_LUMINANCE_ALPHA: v[0] = v[1] = v[2] = c[0]; v[3] = c[1]; break;
        }
        out[i] = pack(v);
    }
}

static long row_bytes(int w, GLenum format, GLenum type, int align) {
    long b = (long)w * comps_of(format) * (type == GL_FLOAT ? 4 : 1);
    if (align > 1) b = (b + align - 1) / align * align;
    return b;
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border,
                  GLenum format, GLenum type, const GLvoid* pixels) {
    NEED_CTX();
    (void)internalformat; (void)border;
    if (target != GL_TEXTURE_2D) { seterr(GL_INVALID_ENUM); return; }
    if (level != 0) return;                    /* mipmaps: level 0 is used */
    if (w < 0 || h < 0 || w > 4096 || h > 4096) { seterr(GL_INVALID_VALUE); return; }
    if (!comps_of(format) || (type != GL_UNSIGNED_BYTE && type != GL_FLOAT)) { seterr(GL_INVALID_ENUM); return; }
    TEX* t = tex_get(C->bound, 1);
    if (!t) { seterr(GL_INVALID_OPERATION); return; }
    flush(C);
    unsigned int* px = (unsigned int*)malloc((size_t)(w ? w : 1) * (h ? h : 1) * 4);
    if (!px) { seterr(GL_OUT_OF_MEMORY); return; }
    long rb = row_bytes(C->unpack_row > 0 ? C->unpack_row : w, format, type, C->unpack_align);
    for (int y = 0; y < h; y++) {
        if (pixels) unpack_row((const unsigned char*)pixels + rb * y, format, type, w, px + (long)y * w);
        else memset(px + (long)y * w, 0, (size_t)w * 4);
    }
    free(t->px);
    t->px = px;
    t->w = w;
    t->h = h;
    state_changed();
}

void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                     GLenum format, GLenum type, const GLvoid* pixels) {
    NEED_CTX();
    if (target != GL_TEXTURE_2D || level != 0) return;
    TEX* t = tex_get(C->bound, 0);
    if (!t || !t->px || x < 0 || y < 0 || x + w > t->w || y + h > t->h) { seterr(GL_INVALID_VALUE); return; }
    if (!comps_of(format)) { seterr(GL_INVALID_ENUM); return; }
    flush(C);
    long rb = row_bytes(C->unpack_row > 0 ? C->unpack_row : w, format, type, C->unpack_align);
    for (int r = 0; r < h; r++) unpack_row((const unsigned char*)pixels + rb * r, format, type, w, t->px + (long)(y + r) * t->w + x);
}

void glTexParameteri(GLenum target, GLenum pname, GLint p) {
    NEED_CTX();
    if (target != GL_TEXTURE_2D) return;
    TEX* t = tex_get(C->bound, 1);
    if (!t) return;
    flush(C);
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER: t->minf = (GLenum)p; break;
    case GL_TEXTURE_MAG_FILTER: t->magf = (GLenum)p; break;
    case GL_TEXTURE_WRAP_S: t->ws = (GLenum)p; break;
    case GL_TEXTURE_WRAP_T: t->wt = (GLenum)p; break;
    }
}
void glTexParameterf(GLenum target, GLenum pname, GLfloat p) { glTexParameteri(target, pname, (GLint)p); }
void glTexEnvi(GLenum target, GLenum pname, GLint p) {
    NEED_CTX();
    if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_MODE) { C->env = (GLenum)p; state_changed(); }
}
void glTexEnvf(GLenum target, GLenum pname, GLfloat p) { glTexEnvi(target, pname, (GLint)p); }
void glTexEnvfv(GLenum target, GLenum pname, const GLfloat* p) {
    NEED_CTX();
    if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_COLOR) { memcpy(C->env_color, p, 16); state_changed(); }
    else if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_MODE) glTexEnvi(target, pname, (GLint)p[0]);
}
void glPixelStorei(GLenum pname, GLint p) {
    NEED_CTX();
    if (pname == GL_UNPACK_ALIGNMENT) C->unpack_align = p;
    else if (pname == GL_PACK_ALIGNMENT) C->pack_align = p;
    else if (pname == GL_UNPACK_ROW_LENGTH) C->unpack_row = p;
}

static float wrapc(float v, GLenum mode) {
    if (mode == GL_REPEAT) return v - floorf(v);
    if (mode == GL_MIRRORED_REPEAT) {
        float f = v - 2 * floorf(v / 2);
        return f > 1 ? 2 - f : f;
    }
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

static unsigned int texel(const TEX* t, int x, int y) {
    if (x < 0) x = 0; else if (x >= t->w) x = t->w - 1;
    if (y < 0) y = 0; else if (y >= t->h) y = t->h - 1;
    return t->px[(long)y * t->w + x];
}

static void sample(const TEX* t, float s, float tt, int linear, float out[4]) {
    s = wrapc(s, t->ws);
    tt = wrapc(tt, t->wt);
    float fx = s * t->w - 0.5f, fy = tt * t->h - 0.5f;
    if (!linear) {
        int x = (int)floorf(s * t->w), y = (int)floorf(tt * t->h);
        if (t->ws == GL_REPEAT) x = ((x % t->w) + t->w) % t->w;
        if (t->wt == GL_REPEAT) y = ((y % t->h) + t->h) % t->h;
        unsigned int p = texel(t, x, y);
        out[0] = ((p >> 16) & 255) / 255.0f; out[1] = ((p >> 8) & 255) / 255.0f; out[2] = (p & 255) / 255.0f; out[3] = (p >> 24) / 255.0f;
        return;
    }
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float ax = fx - x0, ay = fy - y0;
    int x1 = x0 + 1, y1 = y0 + 1;
    if (t->ws == GL_REPEAT) { x0 = ((x0 % t->w) + t->w) % t->w; x1 = ((x1 % t->w) + t->w) % t->w; }
    if (t->wt == GL_REPEAT) { y0 = ((y0 % t->h) + t->h) % t->h; y1 = ((y1 % t->h) + t->h) % t->h; }
    unsigned int p00 = texel(t, x0, y0), p10 = texel(t, x1, y0), p01 = texel(t, x0, y1), p11 = texel(t, x1, y1);
    for (int c = 0; c < 4; c++) {
        int sh = c == 0 ? 16 : c == 1 ? 8 : c == 2 ? 0 : 24;
        float a = (float)((p00 >> sh) & 255), b = (float)((p10 >> sh) & 255);
        float cc = (float)((p01 >> sh) & 255), d = (float)((p11 >> sh) & 255);
        float top = a + (b - a) * ax, bot = cc + (d - cc) * ax;
        out[c] = (top + (bot - top) * ay) / 255.0f;
    }
}

/* ══ the vertex stage, assembly, clipping ═══════════════════════════ */

static int rs_index(void) {
    if (!C->rs_dirty && C->nrs) return C->nrs - 1;
    if (C->nrs == C->rscap) {
        int cap = C->rscap ? C->rscap * 2 : 16;
        RS* n = (RS*)realloc(C->rs, sizeof(RS) * (size_t)cap);
        if (!n) return C->nrs - 1;
        C->rs = n;
        C->rscap = cap;
    }
    RS* r = &C->rs[C->nrs];
    r->depth_test = (unsigned char)C->depth_test;
    r->depth_write = (unsigned char)C->depth_write;
    r->depth_func = C->depth_func;
    r->blend = (unsigned char)C->blend;
    r->sfac = C->sfac;
    r->dfac = C->dfac;
    r->alpha_test = (unsigned char)C->alpha_test;
    r->alpha_func = C->alpha_func;
    r->alpha_ref = C->alpha_ref;
    r->smooth = (unsigned char)C->smooth;
    r->fog = (unsigned char)C->fog_on;
    memcpy(r->fog_color, C->fog_color, 12);
    memcpy(r->env_color, C->env_color, 16);
    memcpy(r->cmask, C->cmask, 4);
    r->env = C->env;
    TEX* t = C->tex2d ? tex_get(C->bound, 0) : NULL;
    r->tex = t && t->px && t->w > 0 && t->h > 0 ? t : NULL;
    r->x0 = 0; r->y0 = 0; r->x1 = C->W; r->y1 = C->H;
    if (C->scissor_on) {
        r->x0 = clampi(C->sx, 0, C->W);
        r->x1 = clampi(C->sx + C->sw, 0, C->W);
        r->y0 = clampi(C->H - (C->sy + C->sh), 0, C->H);
        r->y1 = clampi(C->H - C->sy, 0, C->H);
    }
    C->rs_dirty = 0;
    return C->nrs++;
}

static P* queue_slot(void) {
    if (C->nq >= QUEUE_MAX) flush(C);
    if (C->nq == C->qcap) {
        int cap = C->qcap ? C->qcap * 2 : 1024;
        P* n = (P*)realloc(C->q, sizeof(P) * (size_t)cap);
        if (!n) { seterr(GL_OUT_OF_MEMORY); return NULL; }
        C->q = n;
        C->qcap = cap;
    }
    P* p = &C->q[C->nq];
    p->rs = rs_index();
    if (p->rs < 0) return NULL;
    C->nq++;
    return p;
}

/* clip space -> buffer coordinates */
static void to_window(const V* v, float* x, float* y, float* z, float* iw) {
    float w = v->w != 0 ? v->w : 1e-6f;
    float i = 1.0f / w;
    *x = C->vx + (v->x * i + 1) * 0.5f * C->vw;
    *y = (float)C->H - (C->vy + (v->y * i + 1) * 0.5f * C->vh);
    *z = C->dnear + (v->z * i + 1) * 0.5f * (C->dfar - C->dnear);
    *iw = i;
}

static void put_vertex(P* p, int k, const V* v, const float* flat) {
    to_window(v, &p->x[k], &p->y[k], &p->z[k], &p->iw[k]);
    memcpy(p->c[k], flat ? flat : v->c, 16);
    p->st[k][0] = v->s;
    p->st[k][1] = v->t;
    p->fog[k] = v->fog;
}

static void lerp_v(V* o, const V* a, const V* b, float t) {
    const float* pa = &a->x;
    const float* pb = &b->x;
    float* po = &o->x;
    for (int i = 0; i < (int)(sizeof(V) / sizeof(float)); i++) po[i] = pa[i] + (pb[i] - pa[i]) * t;
}

static float plane_d(const V* v, int pl) {
    switch (pl) {
    case 0: return v->w + v->x;
    case 1: return v->w - v->x;
    case 2: return v->w + v->y;
    case 3: return v->w - v->y;
    case 4: return v->w + v->z;
    default: return v->w - v->z;
    }
}

static void emit_point(const V* v) {
    if (plane_d(v, 0) < 0 || plane_d(v, 1) < 0 || plane_d(v, 2) < 0 || plane_d(v, 3) < 0 || plane_d(v, 4) < 0 || plane_d(v, 5) < 0) return;
    P* p = queue_slot();
    if (!p) return;
    p->kind = 1;
    p->size = C->point_size;
    put_vertex(p, 0, v, NULL);
}

static void emit_line(const V* a0, const V* b0, const float* flat) {
    float t0 = 0, t1 = 1;
    for (int pl = 0; pl < 6; pl++) {
        float da = plane_d(a0, pl), db = plane_d(b0, pl);
        if (da < 0 && db < 0) return;
        if (da < 0) { float t = da / (da - db); if (t > t0) t0 = t; }
        else if (db < 0) { float t = da / (da - db); if (t < t1) t1 = t; }
    }
    if (t0 > t1) return;
    V a, b;
    lerp_v(&a, a0, b0, t0);
    lerp_v(&b, a0, b0, t1);
    P* p = queue_slot();
    if (!p) return;
    p->kind = 2;
    p->size = C->line_width;
    put_vertex(p, 0, &a, flat);
    put_vertex(p, 1, &b, flat);
}

static void emit_tri(const V* v0, const V* v1, const V* v2, const V* prov) {
    V buf[2][MAXCLIP];
    int n = 3;
    buf[0][0] = *v0; buf[0][1] = *v1; buf[0][2] = *v2;
    int cur = 0;
    /* all inside: no clipping */
    int out = 0;
    for (int pl = 0; pl < 6; pl++)
        if (plane_d(v0, pl) < 0 || plane_d(v1, pl) < 0 || plane_d(v2, pl) < 0) { out = 1; break; }
    if (out) {
        for (int pl = 0; pl < 6 && n >= 3; pl++) {
            V* in = buf[cur];
            V* o = buf[cur ^ 1];
            int m = 0;
            for (int i = 0; i < n; i++) {
                const V* a = &in[i];
                const V* b = &in[(i + 1) % n];
                float da = plane_d(a, pl), db = plane_d(b, pl);
                if (da >= 0 && m < MAXCLIP) o[m++] = *a;
                if ((da >= 0) != (db >= 0) && m < MAXCLIP) lerp_v(&o[m++], a, b, da / (da - db));
            }
            n = m;
            cur ^= 1;
        }
        if (n < 3) return;
    }
    V* poly = buf[cur];
    /* facing, from the window-space area */
    float sx[MAXCLIP], sy[MAXCLIP], tz, tw;
    for (int i = 0; i < n; i++) to_window(&poly[i], &sx[i], &sy[i], &tz, &tw);
    float area = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        area += sx[i] * sy[j] - sx[j] * sy[i];
    }
    /* buffer rows grow downwards: counter-clockwise on screen is a negative area here */
    int ccw = area < 0;
    int front = C->front_face == GL_CW ? !ccw : ccw;
    if (C->cull) {
        if (C->cull_face == GL_FRONT_AND_BACK) return;
        if (C->cull_face == GL_FRONT && front) return;
        if (C->cull_face != GL_FRONT && !front) return;
    }
    GLenum pm = front ? C->poly_front : C->poly_back;
    const float* flat = C->smooth ? NULL : prov->c;
    if (pm == GL_LINE) {
        for (int i = 0; i < n; i++) emit_line(&poly[i], &poly[(i + 1) % n], flat);
        return;
    }
    if (pm == GL_POINT) {
        for (int i = 0; i < n; i++) emit_point(&poly[i]);
        return;
    }
    float zoff = 0;
    if (C->poly_factor != 0 || C->poly_units != 0) zoff = C->poly_units * (1.0f / 16777216.0f) * 4;
    for (int i = 1; i + 1 < n; i++) {
        P* p = queue_slot();
        if (!p) return;
        p->kind = 3;
        put_vertex(p, 0, &poly[0], flat);
        put_vertex(p, 1, &poly[i], flat);
        put_vertex(p, 2, &poly[i + 1], flat);
        if (zoff != 0) for (int k = 0; k < 3; k++) p->z[k] += zoff;
    }
}

/* the vertex stage: object coordinates in, a primitive maybe out */
static void vertex(float x, float y, float z, float w) {
    V v;
    float obj[4] = { x, y, z, w }, eye[4];
    m_xform(&C->mv[C->nmv], obj, eye);
    float clip[4];
    m_xform(&C->pr[C->npr], eye, clip);
    v.x = clip[0]; v.y = clip[1]; v.z = clip[2]; v.w = clip[3];
    if (C->lighting) {
        update_nm();
        float n[3];
        for (int r = 0; r < 3; r++) n[r] = C->nm[r * 3] * C->cur_n[0] + C->nm[r * 3 + 1] * C->cur_n[1] + C->nm[r * 3 + 2] * C->cur_n[2];
        float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l > 0) { n[0] /= l; n[1] /= l; n[2] /= l; }
        shade(eye, n, v.c);
    } else memcpy(v.c, C->cur_c, 16);
    v.s = C->cur_st[0];
    v.t = C->cur_st[1];
    if (!C->tm_ident) {
        float st[4] = { v.s, v.t, 0, 1 }, o[4];
        m_xform(&C->tm[C->ntm], st, o);
        float q = o[3] != 0 ? o[3] : 1;
        v.s = o[0] / q;
        v.t = o[1] / q;
    }
    v.fog = 1;
    if (C->fog_on) {
        float d = fabsf(eye[3] != 0 ? eye[2] / eye[3] : eye[2]);
        float f;
        if (C->fog_mode == GL_LINEAR) f = C->fog_end != C->fog_start ? (C->fog_end - d) / (C->fog_end - C->fog_start) : 1;
        else if (C->fog_mode == GL_EXP2) f = expf(-(C->fog_density * d) * (C->fog_density * d));
        else f = expf(-C->fog_density * d);
        v.fog = clampf(f);
    }
    /* assembly */
    int k = C->count++;
    switch (C->prim) {
    case GL_POINTS: emit_point(&v); break;
    case GL_LINES:
        if (k & 1) emit_line(&C->a, &v, C->smooth ? NULL : v.c);
        else C->a = v;
        break;
    case GL_LINE_STRIP: case GL_LINE_LOOP:
        if (k == 0) C->first = v;
        else emit_line(&C->a, &v, C->smooth ? NULL : v.c);
        C->a = v;
        break;
    case GL_TRIANGLES:
        C->quad[k % 3] = v;
        if (k % 3 == 2) emit_tri(&C->quad[0], &C->quad[1], &C->quad[2], &v);
        break;
    case GL_TRIANGLE_STRIP:
        if (k >= 2) {
            if (k & 1) emit_tri(&C->b, &C->a, &v, &v);
            else emit_tri(&C->a, &C->b, &v, &v);
        }
        C->a = C->b;
        C->b = v;
        break;
    case GL_TRIANGLE_FAN:
        if (k == 0) C->first = v;
        else if (k >= 2) emit_tri(&C->first, &C->a, &v, &v);
        C->a = v;
        break;
    case GL_POLYGON:
        if (k == 0) C->first = v;
        else if (k >= 2) emit_tri(&C->first, &C->a, &v, &C->first);
        C->a = v;
        break;
    case GL_QUADS:
        C->quad[k & 3] = v;
        if ((k & 3) == 3) {
            /* line mode: the quad's outline, not its diagonal */
            int front_line = C->poly_front == GL_LINE && C->poly_back == GL_LINE;
            if (front_line) {
                const float* fl = C->smooth ? NULL : v.c;
                for (int i = 0; i < 4; i++) emit_line(&C->quad[i], &C->quad[(i + 1) & 3], fl);
            } else {
                emit_tri(&C->quad[0], &C->quad[1], &C->quad[2], &v);
                emit_tri(&C->quad[0], &C->quad[2], &C->quad[3], &v);
            }
        }
        break;
    case GL_QUAD_STRIP:
        C->quad[k & 3] = v;
        if (k >= 3 && (k & 1)) {
            /* v[k-3] v[k-2] v[k] v[k-1] */
            V* q0 = &C->quad[(k - 3) & 3];
            V* q1 = &C->quad[(k - 2) & 3];
            V* q2 = &C->quad[(k - 1) & 3];
            emit_tri(q0, q1, &v, &v);
            emit_tri(q0, &v, q2, &v);
        }
        break;
    }
}

void glBegin(GLenum mode) {
    NEED_CTX(); float f = 0; REC(L_BEGIN, mode, 0, &f, 0);
    if (C->in_begin) { seterr(GL_INVALID_OPERATION); return; }
    if (mode > GL_POLYGON) { seterr(GL_INVALID_ENUM); return; }
    C->in_begin = 1;
    C->prim = mode;
    C->count = 0;
}

void glEnd(void) {
    NEED_CTX(); float f = 0; REC(L_END, 0, 0, &f, 0);
    if (!C->in_begin) { seterr(GL_INVALID_OPERATION); return; }
    if (C->prim == GL_LINE_LOOP && C->count >= 2) emit_line(&C->a, &C->first, C->smooth ? NULL : C->a.c);
    C->in_begin = 0;
}

void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    NEED_CTX(); float f[4] = { x, y, z, w }; REC(L_VERTEX, 0, 0, f, 4);
    if (!C->in_begin) return;
    vertex(x, y, z, w);
}
void glVertex2f(GLfloat x, GLfloat y) { glVertex4f(x, y, 0, 1); }
void glVertex2i(GLint x, GLint y) { glVertex4f((float)x, (float)y, 0, 1); }
void glVertex2d(GLdouble x, GLdouble y) { glVertex4f((float)x, (float)y, 0, 1); }
void glVertex3f(GLfloat x, GLfloat y, GLfloat z) { glVertex4f(x, y, z, 1); }
void glVertex3d(GLdouble x, GLdouble y, GLdouble z) { glVertex4f((float)x, (float)y, (float)z, 1); }
void glVertex3i(GLint x, GLint y, GLint z) { glVertex4f((float)x, (float)y, (float)z, 1); }
void glVertex2fv(const GLfloat* v) { glVertex4f(v[0], v[1], 0, 1); }
void glVertex3fv(const GLfloat* v) { glVertex4f(v[0], v[1], v[2], 1); }
void glVertex3dv(const GLdouble* v) { glVertex4f((float)v[0], (float)v[1], (float)v[2], 1); }
void glVertex4fv(const GLfloat* v) { glVertex4f(v[0], v[1], v[2], v[3]); }

void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    NEED_CTX(); float f[4] = { r, g, b, a }; REC(L_COLOR, 0, 0, f, 4);
    C->cur_c[0] = clampf(r); C->cur_c[1] = clampf(g); C->cur_c[2] = clampf(b); C->cur_c[3] = clampf(a);
}
void glColor3f(GLfloat r, GLfloat g, GLfloat b) { glColor4f(r, g, b, 1); }
void glColor3d(GLdouble r, GLdouble g, GLdouble b) { glColor4f((float)r, (float)g, (float)b, 1); }
void glColor4d(GLdouble r, GLdouble g, GLdouble b, GLdouble a) { glColor4f((float)r, (float)g, (float)b, (float)a); }
void glColor3ub(GLubyte r, GLubyte g, GLubyte b) { glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, 1); }
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f); }
void glColor3fv(const GLfloat* v) { glColor4f(v[0], v[1], v[2], 1); }
void glColor4fv(const GLfloat* v) { glColor4f(v[0], v[1], v[2], v[3]); }
void glColor3ubv(const GLubyte* v) { glColor3ub(v[0], v[1], v[2]); }
void glColor4ubv(const GLubyte* v) { glColor4ub(v[0], v[1], v[2], v[3]); }
void glNormal3f(GLfloat x, GLfloat y, GLfloat z) {
    NEED_CTX(); float f[3] = { x, y, z }; REC(L_NORMAL, 0, 0, f, 3);
    C->cur_n[0] = x; C->cur_n[1] = y; C->cur_n[2] = z;
}
void glNormal3d(GLdouble x, GLdouble y, GLdouble z) { glNormal3f((float)x, (float)y, (float)z); }
void glNormal3fv(const GLfloat* v) { glNormal3f(v[0], v[1], v[2]); }
void glTexCoord2f(GLfloat s, GLfloat t) {
    NEED_CTX(); float f[2] = { s, t }; REC(L_TEXCOORD, 0, 0, f, 2);
    C->cur_st[0] = s; C->cur_st[1] = t;
}
void glTexCoord1f(GLfloat s) { glTexCoord2f(s, 0); }
void glTexCoord2d(GLdouble s, GLdouble t) { glTexCoord2f((float)s, (float)t); }
void glTexCoord2i(GLint s, GLint t) { glTexCoord2f((float)s, (float)t); }
void glTexCoord2fv(const GLfloat* v) { glTexCoord2f(v[0], v[1]); }
void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2) {
    glBegin(GL_POLYGON);
    glVertex2f(x1, y1); glVertex2f(x2, y1); glVertex2f(x2, y2); glVertex2f(x1, y2);
    glEnd();
}
void glRecti(GLint x1, GLint y1, GLint x2, GLint y2) { glRectf((float)x1, (float)y1, (float)x2, (float)y2); }

/* ══ vertex arrays ═══════════════════════════════════════════════════ */

static ARR* arr_of(GLenum a) {
    switch (a) {
    case GL_VERTEX_ARRAY: return &C->va;
    case GL_COLOR_ARRAY: return &C->ca;
    case GL_NORMAL_ARRAY: return &C->na;
    case GL_TEXTURE_COORD_ARRAY: return &C->ta;
    }
    seterr(GL_INVALID_ENUM);
    return NULL;
}
void glEnableClientState(GLenum a) { NEED_CTX(); ARR* r = arr_of(a); if (r) r->on = 1; }
void glDisableClientState(GLenum a) { NEED_CTX(); ARR* r = arr_of(a); if (r) r->on = 0; }
static int type_size(GLenum t) {
    switch (t) {
    case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
    case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
    case GL_DOUBLE: return 8;
    }
    return 4;
}
static void set_arr(ARR* r, int size, GLenum type, int stride, const void* p) {
    r->size = size; r->type = type; r->ptr = p;
    r->stride = stride ? stride : size * type_size(type);
}
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* p) { NEED_CTX(); set_arr(&C->va, size, type, stride, p); }
void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* p) { NEED_CTX(); set_arr(&C->ca, size, type, stride, p); }
void glNormalPointer(GLenum type, GLsizei stride, const GLvoid* p) { NEED_CTX(); set_arr(&C->na, 3, type, stride, p); }
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* p) { NEED_CTX(); set_arr(&C->ta, size, type, stride, p); }

static void fetch(const ARR* r, int i, float* out, int norm) {
    const unsigned char* p = (const unsigned char*)r->ptr + (long)r->stride * i;
    for (int k = 0; k < r->size && k < 4; k++) {
        switch (r->type) {
        case GL_FLOAT: out[k] = ((const float*)p)[k]; break;
        case GL_DOUBLE: out[k] = (float)((const double*)p)[k]; break;
        case GL_INT: out[k] = (float)((const int*)p)[k]; break;
        case GL_UNSIGNED_INT: out[k] = (float)((const unsigned int*)p)[k]; break;
        case GL_SHORT: out[k] = (float)((const short*)p)[k]; break;
        case GL_UNSIGNED_SHORT: out[k] = (float)((const unsigned short*)p)[k]; break;
        case GL_BYTE: out[k] = (float)((const signed char*)p)[k]; break;
        case GL_UNSIGNED_BYTE: out[k] = norm ? ((const unsigned char*)p)[k] / 255.0f : (float)((const unsigned char*)p)[k]; break;
        }
    }
}

void glArrayElement(GLint i) {
    NEED_CTX();
    float v[4];
    if (C->ca.on) { v[3] = 1; fetch(&C->ca, i, v, 1); glColor4f(v[0], v[1], v[2], C->ca.size == 4 ? v[3] : 1); }
    if (C->na.on) { fetch(&C->na, i, v, 0); glNormal3f(v[0], v[1], v[2]); }
    if (C->ta.on) { v[1] = 0; fetch(&C->ta, i, v, 0); glTexCoord2f(v[0], C->ta.size > 1 ? v[1] : 0); }
    if (C->va.on) {
        v[2] = 0; v[3] = 1;
        fetch(&C->va, i, v, 0);
        glVertex4f(v[0], v[1], C->va.size > 2 ? v[2] : 0, C->va.size > 3 ? v[3] : 1);
    }
}
void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    NEED_CTX();
    if (count < 0) { seterr(GL_INVALID_VALUE); return; }
    glBegin(mode);
    for (int i = 0; i < count; i++) glArrayElement(first + i);
    glEnd();
}
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* idx) {
    NEED_CTX();
    if (count < 0) { seterr(GL_INVALID_VALUE); return; }
    glBegin(mode);
    for (int i = 0; i < count; i++) {
        int k = type == GL_UNSIGNED_BYTE ? ((const unsigned char*)idx)[i] :
                type == GL_UNSIGNED_SHORT ? ((const unsigned short*)idx)[i] : (int)((const unsigned int*)idx)[i];
        glArrayElement(k);
    }
    glEnd();
}

/* ══ display lists ═══════════════════════════════════════════════════ */

GLuint glGenLists(GLsizei range) {
    NEED_CTX(0);
    if (range <= 0) { seterr(GL_INVALID_VALUE); return 0; }
    int first = C->nlists ? C->nlists : 1;
    LIST* n = (LIST*)realloc(C->lists, sizeof(LIST) * (size_t)(first + range));
    if (!n) { seterr(GL_OUT_OF_MEMORY); return 0; }
    for (int i = C->nlists; i < first + range; i++) memset(&n[i], 0, sizeof(LIST));
    C->lists = n;
    C->nlists = first + range;
    for (int i = first; i < first + range; i++) C->lists[i].defined = 1;
    return (GLuint)first;
}
GLboolean glIsList(GLuint l) { NEED_CTX(0); return l > 0 && (int)l < C->nlists && C->lists[l].defined; }
void glNewList(GLuint l, GLenum mode) {
    NEED_CTX();
    if (C->rec) { seterr(GL_INVALID_OPERATION); return; }
    if (l == 0 || (int)l >= C->nlists) { seterr(GL_INVALID_VALUE); return; }
    C->lists[l].n = 0;
    C->lists[l].defined = 1;
    C->rec = 1;
    C->rec_list = l;
    C->rec_exec = mode == GL_COMPILE_AND_EXECUTE;
}
void glEndList(void) { NEED_CTX(); if (!C->rec) { seterr(GL_INVALID_OPERATION); return; } C->rec = 0; }
void glDeleteLists(GLuint l, GLsizei range) {
    NEED_CTX();
    for (int i = 0; i < range; i++) {
        GLuint k = l + (GLuint)i;
        if (k == 0 || (int)k >= C->nlists) continue;
        free(C->lists[k].ops);
        memset(&C->lists[k], 0, sizeof(LIST));
    }
}
void glListBase(GLuint base) { NEED_CTX(); C->list_base = base; }

static void run_list(GLuint l, int depth) {
    if (l == 0 || (int)l >= C->nlists || depth > 64) return;
    LIST* L = &C->lists[l];
    for (int i = 0; i < L->n; i++) {
        LOP* o = &C->lists[l].ops[i];          /* (re-read: a call may grow the list array) */
        float* f = o->f;
        switch (o->op) {
        case L_VERTEX: glVertex4f(f[0], f[1], f[2], f[3]); break;
        case L_COLOR: glColor4f(f[0], f[1], f[2], f[3]); break;
        case L_NORMAL: glNormal3f(f[0], f[1], f[2]); break;
        case L_TEXCOORD: glTexCoord2f(f[0], f[1]); break;
        case L_BEGIN: glBegin(o->a); break;
        case L_END: glEnd(); break;
        case L_MATERIAL: glMaterialfv(o->a, o->b, f); break;
        case L_TRANSLATE: glTranslatef(f[0], f[1], f[2]); break;
        case L_ROTATE: glRotatef(f[0], f[1], f[2], f[3]); break;
        case L_SCALE: glScalef(f[0], f[1], f[2]); break;
        case L_PUSH: glPushMatrix(); break;
        case L_POP: glPopMatrix(); break;
        case L_MULT: glMultMatrixf(f); break;
        case L_LOADM: glLoadMatrixf(f); break;
        case L_LOADID: glLoadIdentity(); break;
        case L_SHADE: glShadeModel(o->a); break;
        case L_ENABLE: glEnable(o->a); break;
        case L_DISABLE: glDisable(o->a); break;
        case L_BIND: glBindTexture(o->a, o->b); break;
        case L_CALL: run_list(o->a, depth + 1); break;
        case L_LIGHT: glLightfv(o->a, o->b, f); break;
        case L_MMODE: glMatrixMode(o->a); break;
        case L_LINEW: glLineWidth(f[0]); break;
        case L_POINTSIZE: glPointSize(f[0]); break;
        case L_BLEND: glBlendFunc(o->a, o->b); break;
        case L_DEPTHF: glDepthFunc(o->a); break;
        case L_CMODE: glColorMaterial(GL_FRONT_AND_BACK, o->a); break;
        }
    }
}

void glCallList(GLuint l) {
    NEED_CTX();
    float f = 0;
    REC(L_CALL, l, 0, &f, 0);
    int rec = C->rec;
    C->rec = 0;                                   /* (its calls must not be recorded again) */
    run_list(l, 0);
    C->rec = rec;
}
void glCallLists(GLsizei n, GLenum type, const GLvoid* lists) {
    for (int i = 0; i < n; i++) {
        GLuint l = type == GL_UNSIGNED_BYTE ? ((const unsigned char*)lists)[i] :
                   type == GL_UNSIGNED_SHORT ? ((const unsigned short*)lists)[i] :
                   type == GL_INT || type == GL_UNSIGNED_INT ? ((const unsigned int*)lists)[i] : 0;
        glCallList(C ? C->list_base + l : l);
    }
}

/* ══ rasterization ═══════════════════════════════════════════════════ */

static int depth_pass(GLenum f, float z, float d) {
    switch (f) {
    case GL_LESS: return z < d;
    case GL_LEQUAL: return z <= d;
    case GL_GREATER: return z > d;
    case GL_GEQUAL: return z >= d;
    case GL_EQUAL: return z == d;
    case GL_NOTEQUAL: return z != d;
    case GL_ALWAYS: return 1;
    }
    return 0;
}

static void factor(GLenum f, const float* s, const float* d, float* o) {
    switch (f) {
    case GL_ZERO: o[0] = o[1] = o[2] = o[3] = 0; break;
    case GL_ONE: o[0] = o[1] = o[2] = o[3] = 1; break;
    case GL_SRC_COLOR: memcpy(o, s, 16); break;
    case GL_ONE_MINUS_SRC_COLOR: for (int i = 0; i < 4; i++) o[i] = 1 - s[i]; break;
    case GL_DST_COLOR: memcpy(o, d, 16); break;
    case GL_ONE_MINUS_DST_COLOR: for (int i = 0; i < 4; i++) o[i] = 1 - d[i]; break;
    case GL_SRC_ALPHA: o[0] = o[1] = o[2] = o[3] = s[3]; break;
    case GL_ONE_MINUS_SRC_ALPHA: o[0] = o[1] = o[2] = o[3] = 1 - s[3]; break;
    case GL_DST_ALPHA: o[0] = o[1] = o[2] = o[3] = d[3]; break;
    case GL_ONE_MINUS_DST_ALPHA: o[0] = o[1] = o[2] = o[3] = 1 - d[3]; break;
    case GL_SRC_ALPHA_SATURATE: {
        float v = s[3] < 1 - d[3] ? s[3] : 1 - d[3];
        o[0] = o[1] = o[2] = v; o[3] = 1;
        break;
    }
    default: o[0] = o[1] = o[2] = o[3] = 1;
    }
}

/* one fragment: texture, fog, alpha test, depth, blending, write */
static void fragment(bgl_ctx_t* c, const RS* r, int x, int y, float z, float* col, float s, float t, float fog) {
    long i = (long)y * c->W + x;
    if (r->depth_test && !depth_pass(r->depth_func, z, c->depth[i])) return;
    if (r->tex) {
        float tx[4];
        GLenum f = r->tex->magf == GL_NEAREST && (r->tex->minf == GL_NEAREST || r->tex->minf == GL_NEAREST_MIPMAP_NEAREST ||
                                                   r->tex->minf == GL_NEAREST_MIPMAP_LINEAR) ? GL_NEAREST : GL_LINEAR;
        sample(r->tex, s, t, f == GL_LINEAR, tx);
        switch (r->env) {
        case GL_REPLACE: memcpy(col, tx, 16); break;
        case GL_DECAL:
            for (int k = 0; k < 3; k++) col[k] = col[k] * (1 - tx[3]) + tx[k] * tx[3];
            break;
        case GL_ADD:
            for (int k = 0; k < 3; k++) col[k] = clampf(col[k] + tx[k]);
            col[3] *= tx[3];
            break;
        default:
            for (int k = 0; k < 4; k++) col[k] *= tx[k];
        }
    }
    if (r->fog) for (int k = 0; k < 3; k++) col[k] = fog * col[k] + (1 - fog) * r->fog_color[k];
    if (r->alpha_test && !depth_pass(r->alpha_func, col[3], r->alpha_ref)) return;
    unsigned int* px = &c->color[i];
    if (r->blend) {
        unsigned int p = *px;
        float d[4] = { ((p >> 16) & 255) / 255.0f, ((p >> 8) & 255) / 255.0f, (p & 255) / 255.0f, (p >> 24) / 255.0f };
        float sf[4], df[4];
        factor(r->sfac, col, d, sf);
        factor(r->dfac, col, d, df);
        for (int k = 0; k < 4; k++) col[k] = clampf(col[k] * sf[k] + d[k] * df[k]);
    } else for (int k = 0; k < 4; k++) col[k] = clampf(col[k]);
    unsigned int v = pack(col);
    if (r->cmask[0] && r->cmask[1] && r->cmask[2] && r->cmask[3]) *px = v;
    else {
        unsigned int m = (r->cmask[0] ? 0x00FF0000u : 0) | (r->cmask[1] ? 0x0000FF00u : 0) |
                         (r->cmask[2] ? 0x000000FFu : 0) | (r->cmask[3] ? 0xFF000000u : 0);
        *px = (*px & ~m) | (v & m);
    }
    if (r->depth_write && r->depth_test) c->depth[i] = z;
}

static void rast_tri(bgl_ctx_t* c, const P* p, const RS* r, int k, int n) {
    float x0 = p->x[0], y0 = p->y[0], x1 = p->x[1], y1 = p->y[1], x2 = p->x[2], y2 = p->y[2];
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area == 0 || area != area) return;
    float ia = 1.0f / area;
    float minx = fminf(x0, fminf(x1, x2)), maxx = fmaxf(x0, fmaxf(x1, x2));
    float miny = fminf(y0, fminf(y1, y2)), maxy = fmaxf(y0, fmaxf(y1, y2));
    int bx0 = (int)floorf(minx), bx1 = (int)ceilf(maxx), by0 = (int)floorf(miny), by1 = (int)ceilf(maxy);
    if (bx0 < r->x0) bx0 = r->x0;
    if (by0 < r->y0) by0 = r->y0;
    if (bx1 > r->x1 - 1) bx1 = r->x1 - 1;
    if (by1 > r->y1 - 1) by1 = r->y1 - 1;
    if (bx0 > bx1 || by0 > by1) return;
    /* barycentric weight i = (A_i x + B_i y + C_i) / area */
    float A[3], B[3], Cc[3];
    const float* xs = p->x;
    const float* ys = p->y;
    for (int e = 0; e < 3; e++) {
        int a = (e + 1) % 3, b = (e + 2) % 3;          /* weight of vertex e: the edge facing it */
        A[e] = -(ys[b] - ys[a]) * ia;
        B[e] = (xs[b] - xs[a]) * ia;
        Cc[e] = ((ys[b] - ys[a]) * xs[a] - (xs[b] - xs[a]) * ys[a]) * ia;
    }
    /* perspective: attributes divided by w, and 1/w, interpolate linearly */
    float pc[3][4], pst[3][2], pf[3];
    for (int v = 0; v < 3; v++) {
        float iw = p->iw[v];
        for (int q = 0; q < 4; q++) pc[v][q] = p->c[v][q] * iw;
        pst[v][0] = p->st[v][0] * iw;
        pst[v][1] = p->st[v][1] * iw;
        pf[v] = p->fog[v] * iw;
    }
    int y = by0 + ((k - by0 % n) % n + n) % n;
    for (; y <= by1; y += n) {
        float py = y + 0.5f;
        /* the row's span: every weight >= 0 */
        float xl = (float)bx0, xr = (float)bx1;
        int empty = 0;
        for (int e = 0; e < 3 && !empty; e++) {
            float cst = B[e] * py + Cc[e];
            if (A[e] > 1e-12f || A[e] < -1e-12f) {
                float xe = -cst / A[e] - 0.5f;           /* weight 0 at pixel x = xe */
                if (A[e] > 0) { float v = ceilf(xe - 1e-4f); if (v > xl) xl = v; }
                else { float v = floorf(xe + 1e-4f); if (v < xr) xr = v; }
            } else if (cst < -1e-7f) empty = 1;
        }
        if (empty || xl > xr) continue;
        int xa = (int)xl, xb = (int)xr;
        float px = xa + 0.5f;
        float l0 = A[0] * px + B[0] * py + Cc[0];
        float l1 = A[1] * px + B[1] * py + Cc[1];
        float l2 = A[2] * px + B[2] * py + Cc[2];
        for (int x = xa; x <= xb; x++, l0 += A[0], l1 += A[1], l2 += A[2]) {
            if (l0 < -1e-5f || l1 < -1e-5f || l2 < -1e-5f) continue;
            float z = l0 * p->z[0] + l1 * p->z[1] + l2 * p->z[2];
            if (r->depth_test && !depth_pass(r->depth_func, z, c->depth[(long)y * c->W + x])) continue;
            float iw = l0 * p->iw[0] + l1 * p->iw[1] + l2 * p->iw[2];
            float w = iw != 0 ? 1.0f / iw : 0;
            float col[4];
            for (int q = 0; q < 4; q++) col[q] = (l0 * pc[0][q] + l1 * pc[1][q] + l2 * pc[2][q]) * w;
            float s = (l0 * pst[0][0] + l1 * pst[1][0] + l2 * pst[2][0]) * w;
            float t = (l0 * pst[0][1] + l1 * pst[1][1] + l2 * pst[2][1]) * w;
            float f = r->fog ? (l0 * pf[0] + l1 * pf[1] + l2 * pf[2]) * w : 1;
            fragment(c, r, x, y, z, col, s, t, f);
        }
    }
}

static void rast_line(bgl_ctx_t* c, const P* p, const RS* r, int k, int n) {
    float x0 = p->x[0], y0 = p->y[0], x1 = p->x[1], y1 = p->y[1];
    float dx = x1 - x0, dy = y1 - y0;
    float len = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
    int steps = (int)ceilf(len);
    if (steps < 1) steps = 1;
    int wdt = (int)(p->size + 0.5f);
    if (wdt < 1) wdt = 1;
    int xmajor = fabsf(dx) >= fabsf(dy);
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        float fx = x0 + dx * t, fy = y0 + dy * t;
        for (int o = 0; o < wdt; o++) {
            int off = o - wdt / 2;
            int x = (int)floorf(fx) + (xmajor ? 0 : off), y = (int)floorf(fy) + (xmajor ? off : 0);
            if (y < r->y0 || y >= r->y1 || x < r->x0 || x >= r->x1) continue;
            if (((y % n) + n) % n != k) continue;
            float col[4];
            for (int q = 0; q < 4; q++) col[q] = p->c[0][q] + (p->c[1][q] - p->c[0][q]) * t;
            float z = p->z[0] + (p->z[1] - p->z[0]) * t;
            float s = p->st[0][0] + (p->st[1][0] - p->st[0][0]) * t;
            float tt = p->st[0][1] + (p->st[1][1] - p->st[0][1]) * t;
            float f = p->fog[0] + (p->fog[1] - p->fog[0]) * t;
            fragment(c, r, x, y, z, col, s, tt, f);
        }
    }
}

static void rast_point(bgl_ctx_t* c, const P* p, const RS* r, int k, int n) {
    int sz = (int)(p->size + 0.5f);
    if (sz < 1) sz = 1;
    int x0 = (int)floorf(p->x[0] - sz / 2.0f + 0.5f), y0 = (int)floorf(p->y[0] - sz / 2.0f + 0.5f);
    for (int y = y0; y < y0 + sz; y++) {
        if (y < r->y0 || y >= r->y1 || ((y % n) + n) % n != k) continue;
        for (int x = x0; x < x0 + sz; x++) {
            if (x < r->x0 || x >= r->x1) continue;
            float col[4];
            memcpy(col, p->c[0], 16);
            fragment(c, r, x, y, p->z[0], col, p->st[0][0], p->st[0][1], p->fog[0]);
        }
    }
}

static void render_band(bgl_ctx_t* c, int k, int n) {
    for (int i = 0; i < c->nq; i++) {
        const P* p = &c->q[i];
        const RS* r = &c->rs[p->rs];
        if (p->kind == 3) rast_tri(c, p, r, k, n);
        else if (p->kind == 2) rast_line(c, p, r, k, n);
        else rast_point(c, p, r, k, n);
    }
}

/* ══ reading pixels ══════════════════════════════════════════════════ */

void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid* pixels) {
    NEED_CTX();
    if (type != GL_UNSIGNED_BYTE || !comps_of(format)) { seterr(GL_INVALID_ENUM); return; }
    flush(C);
    int k = comps_of(format);
    long rb = (long)w * k;
    if (C->pack_align > 1) rb = (rb + C->pack_align - 1) / C->pack_align * C->pack_align;
    for (int r = 0; r < h; r++) {
        int by = C->H - 1 - (y + r);
        unsigned char* o = (unsigned char*)pixels + rb * r;
        for (int i = 0; i < w; i++) {
            int bx = x + i;
            unsigned int p = (by >= 0 && by < C->H && bx >= 0 && bx < C->W) ? C->color[(long)by * C->W + bx] : 0;
            unsigned char R = (unsigned char)(p >> 16), G = (unsigned char)(p >> 8), Bb = (unsigned char)p, A = (unsigned char)(p >> 24);
            unsigned char* d = o + (long)i * k;
            switch (format) {
            case GL_RGBA: d[0] = R; d[1] = G; d[2] = Bb; d[3] = A; break;
            case GL_BGRA: d[0] = Bb; d[1] = G; d[2] = R; d[3] = A; break;
            case GL_RGB: d[0] = R; d[1] = G; d[2] = Bb; break;
            case GL_BGR: d[0] = Bb; d[1] = G; d[2] = R; break;
            case GL_ALPHA: d[0] = A; break;
            default: d[0] = (unsigned char)((R * 77 + G * 150 + Bb * 29) >> 8); if (k == 2) d[1] = A; break;
            }
        }
    }
}

/* ══ contexts ════════════════════════════════════════════════════════ */

static int alloc_buffers(bgl_ctx_t* c, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    unsigned int* col = (unsigned int*)malloc((size_t)w * h * 4);
    float* dep = (float*)malloc((size_t)w * h * 4);
    if (!col || !dep) { free(col); free(dep); return -1; }
    free(c->color);
    free(c->depth);
    c->color = col;
    c->depth = dep;
    c->W = w;
    c->H = h;
    for (long i = 0; i < (long)w * h; i++) { col[i] = 0xFF000000u; dep[i] = 1; }
    return 0;
}

static bgl_ctx_t* ctx_new(int w, int h) {
    bgl_ctx_t* c = (bgl_ctx_t*)calloc(1, sizeof(bgl_ctx_t));
    if (!c) return NULL;
    if (alloc_buffers(c, w, h) != 0) { free(c); return NULL; }
    c->clear_d = 1;
    c->clear_c[3] = 0;
    c->depth_write = 1;
    c->depth_func = GL_LESS;
    c->sfac = GL_ONE;
    c->dfac = GL_ZERO;
    c->alpha_func = GL_ALWAYS;
    c->cull_face = GL_BACK;
    c->front_face = GL_CCW;
    c->cm_mode = GL_AMBIENT_AND_DIFFUSE;
    c->poly_front = c->poly_back = GL_FILL;
    c->fog_mode = GL_EXP;
    c->fog_density = 1;
    c->fog_end = 1;
    c->env = GL_MODULATE;
    c->smooth = 1;
    c->cmask[0] = c->cmask[1] = c->cmask[2] = c->cmask[3] = 1;
    c->point_size = c->line_width = 1;
    c->vw = c->W;
    c->vh = c->H;
    c->sw = c->W;
    c->sh = c->H;
    c->dfar = 1;
    c->unpack_align = c->pack_align = 4;
    c->mmode = GL_MODELVIEW;
    m_ident(&c->mv[0]);
    m_ident(&c->pr[0]);
    m_ident(&c->tm[0]);
    c->tm_ident = 1;
    c->nm_dirty = 1;
    c->cur_c[0] = c->cur_c[1] = c->cur_c[2] = c->cur_c[3] = 1;
    c->cur_n[2] = 1;
    float amb[4] = { 0.2f, 0.2f, 0.2f, 1 }, dif[4] = { 0.8f, 0.8f, 0.8f, 1 }, blk[4] = { 0, 0, 0, 1 };
    memcpy(c->model_amb, amb, 16);
    memcpy(c->m_amb, amb, 16);
    memcpy(c->m_dif, dif, 16);
    memcpy(c->m_spe, blk, 16);
    memcpy(c->m_emi, blk, 16);
    for (int i = 0; i < NLIGHTS; i++) {
        LIGHT* L = &c->lt[i];
        memcpy(L->amb, blk, 16);
        float one[4] = { 1, 1, 1, 1 };
        memcpy(L->dif, i == 0 ? one : blk, 16);
        memcpy(L->spe, i == 0 ? one : blk, 16);
        L->pos[2] = 1;
        L->dir[2] = -1;
        L->spot_cut = 180;
        L->att[0] = 1;
    }
    c->rs_dirty = 1;
    return c;
}

bgl_ctx_t* bgl_create_offscreen(int w, int h) {
    bgl_ctx_t* c = ctx_new(w, h);
    if (c) bgl_make_current(c);
    return c;
}

void bgl_make_current(bgl_ctx_t* c) {
    if (C && C != c) flush(C);
    C = c;
}
bgl_ctx_t* bgl_current(void) { return C; }

const unsigned int* bgl_pixels(bgl_ctx_t* c, int* w, int* h) {
    if (!c) return NULL;
    flush(c);
    if (w) *w = c->W;
    if (h) *h = c->H;
    return c->color;
}

void bgl_destroy(bgl_ctx_t* c) {
    if (!c) return;
    flush(c);
    if (C == c) C = NULL;
    for (int i = 0; i < c->ntex; i++) if (c->texs[i]) { free(c->texs[i]->px); free(c->texs[i]); }
    for (int i = 0; i < c->nlists; i++) free(c->lists[i].ops);
    free(c->texs);
    free(c->lists);
    free(c->q);
    free(c->rs);
    free(c->color);
    free(c->depth);
    free(c);
}

#ifndef BGL_HOST
bgl_ctx_t* bgl_create(bwin_t* win) {
    if (!win || win->id < 0) return NULL;
    bgl_ctx_t* c = ctx_new(win->w, win->h);
    if (!c) return NULL;
    c->win = win;
    bgl_make_current(c);
    pool_start();
    return c;
}

void bgl_resize(bgl_ctx_t* c) {
    if (!c || !c->win) return;
    flush(c);
    if (c->win->w == c->W && c->win->h == c->H) return;
    int vfull = c->vx == 0 && c->vy == 0 && c->vw == c->W && c->vh == c->H;
    if (alloc_buffers(c, c->win->w, c->win->h) == 0 && vfull) { c->vw = c->W; c->vh = c->H; }
    c->rs_dirty = 1;
}

void bgl_swap(bgl_ctx_t* c) {
    if (!c) return;
    flush(c);
    if (!c->win || !c->win->px) return;
    int w = c->W < c->win->w ? c->W : c->win->w, h = c->H < c->win->h ? c->H : c->win->h;
    for (int y = 0; y < h; y++) {
        const unsigned int* s = c->color + (long)y * c->W;
        unsigned int* d = c->win->px + (long)y * c->win->w;
        for (int x = 0; x < w; x++) d[x] = s[x] & 0x00FFFFFFu;
    }
    bwin_update(c->win);
}
#else
void bgl_swap(bgl_ctx_t* c) { flush(c); }
#endif
