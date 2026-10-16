/* gl3test - checks OpenGL 3.3 (Mesa) on Banana OS.
 *
 *   gl3test           draws offscreen and checks the pixels (prints PASS / FAIL)
 *   gl3test window    a spinning shaded triangle in a window (Q or Escape quits)
 *
 * Needs libOSMesa.so (ports/mesa/build.sh) in /lib, /usr/lib or /mnt/<disk>/lib. */
#include <GL/gl3.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail;

static void check(int ok, const char* what) {
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}

static const char* VS =
    "#version 330 core\n"
    "layout(location = 0) in vec2 pos;\n"
    "layout(location = 1) in vec3 col;\n"
    "uniform float angle;\n"
    "out vec3 vcol;\n"
    "void main() {\n"
    "    float c = cos(angle), s = sin(angle);\n"
    "    gl_Position = vec4(c * pos.x - s * pos.y, s * pos.x + c * pos.y, 0.0, 1.0);\n"
    "    vcol = col;\n"
    "}\n";
static const char* FS =
    "#version 330 core\n"
    "in vec3 vcol;\n"
    "uniform vec3 tint;\n"
    "out vec4 frag;\n"
    "void main() { frag = vec4(vcol * tint, 1.0); }\n";

static GLuint shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        printf("shader: %s\n", log);
    }
    return s;
}

static GLuint program(void) {
    GLuint p = glCreateProgram();
    glAttachShader(p, shader(GL_VERTEX_SHADER, VS));
    glAttachShader(p, shader(GL_FRAGMENT_SHADER, FS));
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    check(ok, "GLSL 3.30 program links");
    return p;
}

static GLuint triangle(void) {
    static const float v[] = {     /* x, y, r, g, b */
        -0.8f, -0.8f, 1, 0, 0,
         0.8f, -0.8f, 0, 1, 0,
         0.0f,  0.8f, 0, 0, 1,
    };
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    return vao;
}

static int offscreen(void) {
    const int W = 64, H = 64;
    bgl3_ctx_t* gl = bgl3_create_offscreen(W, H);
    if (!gl) { printf("gl3test: %s\nFAIL\n", bgl3_error()); return 1; }
    printf("GL_VERSION  %s\nGL_RENDERER %s\nGLSL        %s\n", (const char*)glGetString(GL_VERSION),
           (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));
    GLint major = 0, minor = 0, prof = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &prof);
    check(major > 3 || (major == 3 && minor >= 3), "OpenGL 3.3 or later");
    check(prof & GL_CONTEXT_CORE_PROFILE_BIT, "core profile");

    GLuint p = program();
    GLuint vao = triangle();
    glViewport(0, 0, W, H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(p);
    glUniform1f(glGetUniformLocation(p, "angle"), 0.0f);
    glUniform3f(glGetUniformLocation(p, "tint"), 1.0f, 1.0f, 1.0f);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    int w, h;
    const unsigned int* px = bgl3_pixels(gl, &w, &h);
    unsigned int mid = px[(H / 2) * W + W / 2] & 0xFFFFFF, corner = px[0] & 0xFFFFFF;
    unsigned int low = px[(H - 8) * W + 10] & 0xFFFFFF;                /* near the red corner, bottom left */
    printf("  center %06x, bottom-left %06x, top-left %06x\n", mid, low, corner);
    check(corner == 0, "clear color outside the triangle");
    check(mid != 0 && (mid & 0xFF) > 0x20 && ((mid >> 8) & 0xFF) > 0x20 && ((mid >> 16) & 0xFF) > 0x20,
          "interpolated colors inside it");
    check(((low >> 16) & 0xFF) > 0xA0 && (low & 0xFF) < 0x60, "red at the red vertex (rows top first)");

    /* a framebuffer object with a texture, then the texture drawn back */
    GLuint tex, fbo;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "framebuffer object complete");
    glViewport(0, 0, 16, 16);
    glClearColor(0.0f, 0.5f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    unsigned char rgba[4] = { 0 };
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    check(rgba[0] == 0 && rgba[1] >= 0x7e && rgba[1] <= 0x81 && rgba[2] == 0xff, "glReadPixels from the FBO");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    check(glGetError() == GL_NO_ERROR, "no GL error");
    bgl3_destroy(gl);
    printf("gl3test: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}

static int window(void) {
    bwin_t win;
    if (bwin_open(&win, "OpenGL 3.3", 480, 360) < 0) { printf("gl3test: no desktop\n"); return 1; }
    bwin_resizable(&win, 160, 120);
    bgl3_ctx_t* gl = bgl3_create(&win);
    if (!gl) { printf("gl3test: %s\n", bgl3_error()); bwin_close(&win); return 1; }
    GLuint p = program();
    GLuint vao = triangle();
    GLint ua = glGetUniformLocation(p, "angle"), ut = glGetUniformLocation(p, "tint");
    float a = 0;
    unsigned frames = 0, t0 = banana_ticks();
    char title[64];
    for (;;) {
        banana_event_t ev;
        while (bwin_event(&win, &ev)) {
            if (ev.type == BANANA_EV_CLOSE) goto out;
            if (ev.type == BANANA_EV_KEY && (ev.key == 'q' || ev.key == 27)) goto out;
            if (ev.type == BANANA_EV_RESIZE) bgl3_resize(gl);
        }
        glViewport(0, 0, win.w, win.h);
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(p);
        glUniform1f(ua, a);
        glUniform3f(ut, 1.0f, 1.0f, 1.0f);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        bgl3_swap(gl);
        a += 0.03f;
        frames++;
        unsigned t = banana_ticks();
        if (t - t0 >= 1000) {
            snprintf(title, sizeof(title), "OpenGL 3.3 - %u fps", frames * 1000 / (t - t0));
            bwin_title(&win, title);
            frames = 0;
            t0 = t;
        }
    }
out:
    bgl3_destroy(gl);
    bwin_close(&win);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "window")) return window();
    return offscreen();
}
