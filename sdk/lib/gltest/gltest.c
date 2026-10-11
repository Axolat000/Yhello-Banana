/* Host test for sdk/lib/gl.c: renders scenes to PPM files.
 *   gcc -O2 -DBGL_HOST -I<dir with GL/> gltest.c gl.c -lm -lpthread
 *   BGL_THREADS=1 ./gltest out1   vs   ./gltest outN   (must be identical) */
#include "GL/gl.h"
#include "GL/glu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

typedef struct bgl_ctx bgl_ctx_t;
bgl_ctx_t* bgl_create_offscreen(int w, int h);
const unsigned int* bgl_pixels(bgl_ctx_t* ctx, int* w, int* h);
void bgl_destroy(bgl_ctx_t* ctx);
int bgl_threads(void);
void bgl_swap(bgl_ctx_t* ctx);

static const char* g_dir;

static void save(bgl_ctx_t* c, const char* name) {
    int w, h;
    const unsigned int* px = bgl_pixels(c, &w, &h);
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.ppm", g_dir, name);
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) { unsigned char p[3] = { px[i] >> 16, px[i] >> 8, px[i] }; fwrite(p, 1, 3, f); }
    fclose(f);
}

/* a gear, after the classic demo's shape (written from scratch) */
static void gear(float inner, float outer, float width, int teeth, float depth) {
    float r0 = inner, r1 = outer - depth / 2, r2 = outer + depth / 2;
    float da = 2 * 3.14159265f / teeth / 4;
    glShadeModel(GL_FLAT);
    glNormal3f(0, 0, 1);
    /* front face */
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= teeth; i++) {
        float a = i * 2 * 3.14159265f / teeth;
        glVertex3f(r0 * cosf(a), r0 * sinf(a), width / 2);
        glVertex3f(r1 * cosf(a), r1 * sinf(a), width / 2);
        if (i < teeth) {
            glVertex3f(r0 * cosf(a), r0 * sinf(a), width / 2);
            glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), width / 2);
        }
    }
    glEnd();
    /* front teeth */
    glBegin(GL_QUADS);
    for (int i = 0; i < teeth; i++) {
        float a = i * 2 * 3.14159265f / teeth;
        glVertex3f(r1 * cosf(a), r1 * sinf(a), width / 2);
        glVertex3f(r2 * cosf(a + da), r2 * sinf(a + da), width / 2);
        glVertex3f(r2 * cosf(a + 2 * da), r2 * sinf(a + 2 * da), width / 2);
        glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), width / 2);
    }
    glEnd();
    glNormal3f(0, 0, -1);
    /* back face */
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= teeth; i++) {
        float a = i * 2 * 3.14159265f / teeth;
        glVertex3f(r1 * cosf(a), r1 * sinf(a), -width / 2);
        glVertex3f(r0 * cosf(a), r0 * sinf(a), -width / 2);
        if (i < teeth) {
            glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), -width / 2);
            glVertex3f(r0 * cosf(a), r0 * sinf(a), -width / 2);
        }
    }
    glEnd();
    glBegin(GL_QUADS);
    for (int i = 0; i < teeth; i++) {
        float a = i * 2 * 3.14159265f / teeth;
        glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), -width / 2);
        glVertex3f(r2 * cosf(a + 2 * da), r2 * sinf(a + 2 * da), -width / 2);
        glVertex3f(r2 * cosf(a + da), r2 * sinf(a + da), -width / 2);
        glVertex3f(r1 * cosf(a), r1 * sinf(a), -width / 2);
    }
    glEnd();
    /* outward faces of the teeth */
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i < teeth; i++) {
        float a = i * 2 * 3.14159265f / teeth;
        float pts[4][2] = { { r1 * cosf(a), r1 * sinf(a) }, { r2 * cosf(a + da), r2 * sinf(a + da) },
                            { r2 * cosf(a + 2 * da), r2 * sinf(a + 2 * da) }, { r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da) } };
        for (int k = 0; k < 4; k++) {
            float nx, ny;
            if (k < 3) { nx = pts[k + 1][1] - pts[k][1]; ny = -(pts[k + 1][0] - pts[k][0]); }
            else { float b = a + 4 * da; nx = cosf(b - da * 0.5f); ny = sinf(b - da * 0.5f); }
            float l = sqrtf(nx * nx + ny * ny);
            glNormal3f(nx / l, ny / l, 0);
            glVertex3f(pts[k][0], pts[k][1], width / 2);
            glVertex3f(pts[k][0], pts[k][1], -width / 2);
        }
    }
    glVertex3f(r1, 0, width / 2);
    glVertex3f(r1, 0, -width / 2);
    glEnd();
    /* the hole */
    glShadeModel(GL_SMOOTH);
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= teeth; i++) {
        float a = i * 2 * 3.14159265f / teeth;
        glNormal3f(-cosf(a), -sinf(a), 0);
        glVertex3f(r0 * cosf(a), r0 * sinf(a), -width / 2);
        glVertex3f(r0 * cosf(a), r0 * sinf(a), width / 2);
    }
    glEnd();
}

static void scene_gears(bgl_ctx_t* c, float angle) {
    static GLuint g1, g2, g3;
    if (!g1) {
        float pos[4] = { 5, 5, 10, 0 }, red[4] = { 0.8f, 0.1f, 0, 1 }, green[4] = { 0, 0.8f, 0.2f, 1 }, blue[4] = { 0.2f, 0.2f, 1, 1 };
        glLightfv(GL_LIGHT0, GL_POSITION, pos);
        glEnable(GL_CULL_FACE);
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glEnable(GL_DEPTH_TEST);
        g1 = glGenLists(3);
        g2 = g1 + 1;
        g3 = g1 + 2;
        glNewList(g1, GL_COMPILE); glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red); gear(1, 4, 1, 20, 0.7f); glEndList();
        glNewList(g2, GL_COMPILE); glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green); gear(0.5f, 2, 2, 10, 0.7f); glEndList();
        glNewList(g3, GL_COMPILE); glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue); gear(1.3f, 2, 0.5f, 10, 0.7f); glEndList();
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-1.333, 1.333, -1, 1, 5, 60);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0, 0, -40);
    }
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPushMatrix();
    glRotatef(20, 1, 0, 0);
    glRotatef(30, 0, 1, 0);
    glPushMatrix(); glTranslatef(-3, -2, 0); glRotatef(angle, 0, 0, 1); glCallList(g1); glPopMatrix();
    glPushMatrix(); glTranslatef(3.1f, -2, 0); glRotatef(-2 * angle - 9, 0, 0, 1); glCallList(g2); glPopMatrix();
    glPushMatrix(); glTranslatef(-3.1f, 4.2f, 0); glRotatef(-2 * angle - 25, 0, 0, 1); glCallList(g3); glPopMatrix();
    glPopMatrix();
    (void)c;
}

static void scene_basic(void) {
    glClearColor(0.1f, 0.1f, 0.15f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-1, 1, -1, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    /* a smooth triangle */
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0); glVertex3f(-0.9f, -0.9f, 0);
    glColor3f(0, 1, 0); glVertex3f(0.1f, -0.9f, 0);
    glColor3f(0, 0, 1); glVertex3f(-0.4f, 0.0f, 0);
    glEnd();
    /* two quads crossing in depth */
    glColor3f(1, 1, 0);
    glBegin(GL_QUADS); glVertex3f(0.2f, 0.2f, -0.5f); glVertex3f(0.9f, 0.2f, -0.5f); glVertex3f(0.9f, 0.9f, 0.5f); glVertex3f(0.2f, 0.9f, 0.5f); glEnd();
    glColor3f(0, 1, 1);
    glBegin(GL_QUADS); glVertex3f(0.2f, 0.55f, 0); glVertex3f(0.9f, 0.55f, 0); glVertex3f(0.9f, 0.65f, 0); glVertex3f(0.2f, 0.65f, 0); glEnd();
    /* blending */
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 0.5f); glRectf(-0.95f, 0.1f, -0.25f, 0.9f);
    glDisable(GL_BLEND);
    /* lines and points */
    glLineWidth(2);
    glBegin(GL_LINE_LOOP); glColor3f(1, 0.5f, 0); glVertex2f(0.2f, -0.2f); glVertex2f(0.9f, -0.2f); glVertex2f(0.55f, -0.9f); glEnd();
    glPointSize(6);
    glBegin(GL_POINTS); glColor3f(1, 1, 1); for (int i = 0; i < 5; i++) glVertex2f(0.25f + i * 0.15f, -0.95f); glEnd();
}

static void scene_textured(float angle) {
    static GLuint tex;
    if (!tex) {
        unsigned char img[64 * 64 * 3];
        for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
            int on = ((x / 8) + (y / 8)) & 1;
            unsigned char* p = img + (y * 64 + x) * 3;
            p[0] = on ? 240 : 40; p[1] = on ? 200 : 60; p[2] = on ? 40 : 160;
        }
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 64, 64, 0, GL_RGB, GL_UNSIGNED_BYTE, img);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glClearColor(0.5f, 0.6f, 0.7f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluPerspective(60, 4.0 / 3, 0.5, 50);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity(); gluLookAt(0, 1.5, 4, 0, 0, 0, 0, 1, 0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_FOG); glFogi(GL_FOG_MODE, GL_LINEAR); glFogf(GL_FOG_START, 3); glFogf(GL_FOG_END, 25);
    float fogc[4] = { 0.5f, 0.6f, 0.7f, 1 }; glFogfv(GL_FOG_COLOR, fogc);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    /* a floor going far away (perspective-correct texture, fog) */
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(-10, -1, 10);
    glTexCoord2f(10, 0); glVertex3f(10, -1, 10);
    glTexCoord2f(10, 10); glVertex3f(10, -1, -30);
    glTexCoord2f(0, 10); glVertex3f(-10, -1, -30);
    glEnd();
    /* a cube with vertex arrays */
    static const float v[] = { -1,-1,1, 1,-1,1, 1,1,1, -1,1,1,  1,-1,-1, -1,-1,-1, -1,1,-1, 1,1,-1,
                                -1,-1,-1, -1,-1,1, -1,1,1, -1,1,-1,  1,-1,1, 1,-1,-1, 1,1,-1, 1,1,1,
                                -1,1,1, 1,1,1, 1,1,-1, -1,1,-1,  -1,-1,-1, 1,-1,-1, 1,-1,1, -1,-1,1 };
    static float t[48];
    for (int f = 0; f < 6; f++) { float uv[8] = { 0,0, 1,0, 1,1, 0,1 }; memcpy(t + f * 8, uv, sizeof(uv)); }
    glPushMatrix();
    glRotatef(angle, 0, 1, 0);
    glRotatef(angle * 0.5f, 1, 0, 0);
    glScalef(0.7f, 0.7f, 0.7f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, v);
    glTexCoordPointer(2, GL_FLOAT, 0, t);
    glDrawArrays(GL_QUADS, 0, 24);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glPopMatrix();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_FOG);
    /* a wireframe sphere-ish shape */
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glColor3f(1, 1, 1);
    glPushMatrix(); glTranslatef(1.8f, 0.2f, 0);
    for (int i = 0; i < 8; i++) {
        float a0 = i * 3.14159f / 8 - 1.5708f, a1 = (i + 1) * 3.14159f / 8 - 1.5708f;
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= 12; j++) {
            float b = j * 6.28318f / 12;
            glVertex3f(0.5f * cosf(a0) * cosf(b), 0.5f * sinf(a0), 0.5f * cosf(a0) * sinf(b));
            glVertex3f(0.5f * cosf(a1) * cosf(b), 0.5f * sinf(a1), 0.5f * cosf(a1) * sinf(b));
        }
        glEnd();
    }
    glPopMatrix();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_CULL_FACE);
}

int main(int argc, char** argv) {
    g_dir = argc > 1 ? argv[1] : ".";
    bgl_ctx_t* c = bgl_create_offscreen(640, 480);
    printf("%s | %s | %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    scene_basic();
    save(c, "basic");
    printf("basic err=%x\n", glGetError());
    bgl_destroy(c);

    c = bgl_create_offscreen(640, 480);
    scene_textured(35);
    save(c, "textured");
    printf("textured err=%x\n", glGetError());
    bgl_destroy(c);

    c = bgl_create_offscreen(640, 480);
    struct timespec t0, t1;
    scene_gears(c, 0);
    save(c, "gears");
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int frames = 60;
    for (int i = 0; i < frames; i++) { scene_gears(c, (float)i * 2); bgl_swap(c); }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double s = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("gears: %d frames in %.2f s = %.1f fps (%d threads), err=%x\n", frames, s, frames / s, bgl_threads(), glGetError());
    bgl_destroy(c);
    return 0;
}
