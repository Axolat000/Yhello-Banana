/* Gears - OpenGL on Banana OS (USE_GL = 1).
 *
 * Three gears turning, lit, from display lists - the frame rate in the
 * title. Arrows turn the view, W wireframe, L lighting, Space pauses,
 * Esc quits. The window can be resized. */
#include <GL/bgl.h>
#include <math.h>
#include <stdio.h>

static void gear(float inner, float outer, float width, int teeth, float depth) {
    float r0 = inner, r1 = outer - depth / 2, r2 = outer + depth / 2;
    float da = 2 * 3.14159265f / teeth / 4;
    glShadeModel(GL_FLAT);
    glNormal3f(0, 0, 1);
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
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i < teeth; i++) {
        float a = i * 2 * 3.14159265f / teeth;
        float p[4][2] = { { r1 * cosf(a), r1 * sinf(a) }, { r2 * cosf(a + da), r2 * sinf(a + da) },
                          { r2 * cosf(a + 2 * da), r2 * sinf(a + 2 * da) }, { r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da) } };
        for (int k = 0; k < 4; k++) {
            float nx, ny;
            if (k < 3) { nx = p[k + 1][1] - p[k][1]; ny = -(p[k + 1][0] - p[k][0]); }
            else { nx = cosf(a + 3.5f * da); ny = sinf(a + 3.5f * da); }
            float l = sqrtf(nx * nx + ny * ny);
            glNormal3f(nx / l, ny / l, 0);
            glVertex3f(p[k][0], p[k][1], width / 2);
            glVertex3f(p[k][0], p[k][1], -width / 2);
        }
    }
    glVertex3f(r1, 0, width / 2);
    glVertex3f(r1, 0, -width / 2);
    glEnd();
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

static void reshape(int w, int h) {
    if (h < 1) h = 1;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float a = (float)w / h;
    glFrustum(-a, a, -1, 1, 5, 60);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -40);
}

int main(void) {
    bwin_t win;
    if (bwin_open(&win, "Gears", 640, 480) != 0) { printf("gears: start the desktop first (startx)\n"); return 1; }
    bwin_resizable(&win, 200, 150);
    bgl_ctx_t* gl = bgl_create(&win);
    if (!gl) { printf("gears: out of memory\n"); return 1; }

    float pos[4] = { 5, 5, 10, 0 }, red[4] = { 0.8f, 0.1f, 0, 1 }, green[4] = { 0, 0.8f, 0.2f, 1 }, blue[4] = { 0.2f, 0.2f, 1, 1 };
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    GLuint g = glGenLists(3);
    glNewList(g, GL_COMPILE); glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red); gear(1, 4, 1, 20, 0.7f); glEndList();
    glNewList(g + 1, GL_COMPILE); glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green); gear(0.5f, 2, 2, 10, 0.7f); glEndList();
    glNewList(g + 2, GL_COMPILE); glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue); gear(1.3f, 2, 0.5f, 10, 0.7f); glEndList();
    reshape(win.w, win.h);

    float view_x = 20, view_y = 30, angle = 0;
    int paused = 0, wire = 0, frames = 0;
    unsigned int t_last = banana_ticks(), t_fps = t_last;
    char title[96];
    for (;;) {
        banana_event_t ev;
        while (bwin_event(&win, &ev)) {
            if (ev.type == BANANA_EV_CLOSE) { bgl_destroy(gl); bwin_close(&win); return 0; }
            if (ev.type == BANANA_EV_RESIZE) { bgl_resize(gl); reshape(win.w, win.h); }
            if (ev.type == BANANA_EV_KEY) {
                switch (ev.key) {
                case 27: bgl_destroy(gl); bwin_close(&win); return 0;
                case BANANA_KEY_UP: view_x += 5; break;
                case BANANA_KEY_DOWN: view_x -= 5; break;
                case BANANA_KEY_LEFT: view_y += 5; break;
                case BANANA_KEY_RIGHT: view_y -= 5; break;
                case ' ': paused = !paused; break;
                case 'w': case 'W': wire = !wire; glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL); break;
                case 'l': case 'L': if (glIsEnabled(GL_LIGHTING)) glDisable(GL_LIGHTING); else glEnable(GL_LIGHTING); break;
                }
            }
        }
        unsigned int now = banana_ticks();
        if (!paused) angle += (now - t_last) * 0.07f;      /* ~70 degrees a second, whatever the frame rate */
        t_last = now;

        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPushMatrix();
        glRotatef(view_x, 1, 0, 0);
        glRotatef(view_y, 0, 1, 0);
        glPushMatrix(); glTranslatef(-3, -2, 0); glRotatef(angle, 0, 0, 1); glCallList(g); glPopMatrix();
        glPushMatrix(); glTranslatef(3.1f, -2, 0); glRotatef(-2 * angle - 9, 0, 0, 1); glCallList(g + 1); glPopMatrix();
        glPushMatrix(); glTranslatef(-3.1f, 4.2f, 0); glRotatef(-2 * angle - 25, 0, 0, 1); glCallList(g + 2); glPopMatrix();
        glPopMatrix();
        bgl_swap(gl);

        frames++;
        if (now - t_fps >= 1000) {
            snprintf(title, sizeof(title), "Gears - %d fps (%d cores)", frames * 1000 / (int)(now - t_fps), bgl_threads());
            bwin_title(&win, title);
            frames = 0;
            t_fps = now;
        }
        if (paused) banana_sleep(30);
    }
}
