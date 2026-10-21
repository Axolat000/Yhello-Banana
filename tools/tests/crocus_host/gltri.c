/* SPDX-License-Identifier: GPL-3.0-or-later */
/* An OpenGL 3.3 core program through OSMesa, as Banana OS apps draw: a VBO,
 * GLSL shaders, a texture, a framebuffer object, many frames, glReadPixels.
 * On the GPU model (tools/tests/crocus_host) the pixels mean nothing - it
 * checks that crocus gets this far, frame after frame, and asks for no
 * more than the kernel gives. */
#define GL_GLEXT_PROTOTYPES 1
#include <GL/osmesa.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* VS = "#version 330 core\nlayout(location=0) in vec2 p; layout(location=1) in vec3 c; out vec3 v;\n"
                        "uniform float t; void main(){ v=c; gl_Position=vec4(p.x*cos(t)-p.y*sin(t), p.x*sin(t)+p.y*cos(t), 0, 1); }\n";
static const char* FS = "#version 330 core\nin vec3 v; out vec4 o; uniform sampler2D s;\n"
                        "void main(){ o=vec4(v,1)*texture(s, v.xy); }\n";

static GLuint shader(GLenum kind, const char* src) {
    GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, NULL, log); printf("shader: %s\n", log); exit(1); }
    return s;
}

int main(int argc, char** argv) {
    int frames = argc > 1 ? atoi(argv[1]) : 60;
    const int attrs[] = { OSMESA_FORMAT, OSMESA_RGBA, OSMESA_DEPTH_BITS, 24, OSMESA_STENCIL_BITS, 8,
                          OSMESA_PROFILE, OSMESA_CORE_PROFILE, OSMESA_CONTEXT_MAJOR_VERSION, 3,
                          OSMESA_CONTEXT_MINOR_VERSION, 3, 0 };
    OSMesaContext ctx = OSMesaCreateContextAttribs(attrs, NULL);
    static unsigned char buf[320 * 240 * 4];
    if (!ctx) {
        /* (Ironlake: OpenGL 2.1 - the fixed-function way, with a texture) */
        ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, NULL);
        if (!ctx || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, 320, 240)) { printf("gltri: no OpenGL context\n"); return 1; }
        printf("gltri: %s / %s / %s (no 3.3 core: the legacy path)\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
        static unsigned char texels[64 * 64 * 4];
        for (int i = 0; i < 64 * 64 * 4; i++) texels[i] = (unsigned char)(i * 7);
        GLuint tex;
        glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glEnable(GL_TEXTURE_2D); glEnable(GL_DEPTH_TEST);
        for (int f = 0; f < frames; f++) {
            glClearColor(0.1f, 0.2f, 0.3f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glLoadIdentity(); glRotatef(f * 3.0f, 0, 0, 1);
            glBegin(GL_TRIANGLES);
            glColor3f(1, 0, 0); glTexCoord2f(0, 0); glVertex2f(-0.8f, -0.8f);
            glColor3f(0, 1, 0); glTexCoord2f(1, 0); glVertex2f(0.8f, -0.8f);
            glColor3f(0, 0, 1); glTexCoord2f(0.5f, 1); glVertex2f(0, 0.8f);
            glEnd();
            glFinish();
        }
        unsigned char px[4];
        glReadPixels(160, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        GLenum err = glGetError();
        printf("gltri: %d frames, glGetError %#x, a pixel %02x%02x%02x%02x\n", frames, err, px[0], px[1], px[2], px[3]);
        OSMesaDestroyContext(ctx);
        return err != GL_NO_ERROR;
    }
    if (!OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, 320, 240)) { printf("gltri: MakeCurrent failed\n"); return 1; }
    printf("gltri: %s / %s / %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    GLuint prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, VS));
    glAttachShader(prog, shader(GL_FRAGMENT_SHADER, FS));
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { printf("gltri: link failed\n"); return 1; }
    static const float v[] = { -0.8f, -0.8f, 1, 0, 0,  0.8f, -0.8f, 0, 1, 0,  0, 0.8f, 0, 0, 1 };
    GLuint vao, vbo, tex, fbo, rb;
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 20, (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 20, (void*)8); glEnableVertexAttribArray(1);
    static unsigned char texels[64 * 64 * 4];
    for (int i = 0; i < 64 * 64 * 4; i++) texels[i] = (unsigned char)(i * 7);
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    /* an offscreen target too (a game's render-to-texture) */
    GLuint ftex;
    glGenTextures(1, &ftex); glBindTexture(GL_TEXTURE_2D, ftex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ftex, 0);
    glGenRenderbuffers(1, &rb); glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 128, 128);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rb);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("gltri: FBO incomplete\n"); return 1; }
    glUseProgram(prog);
    GLint ut = glGetUniformLocation(prog, "t");
    for (int f = 0; f < frames; f++) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, 128, 128);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.1f, 0.2f, 0.3f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1f(ut, f * 0.05f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, 320, 240);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindTexture(GL_TEXTURE_2D, ftex);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
    }
    unsigned char px[4];
    glReadPixels(160, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    GLenum err = glGetError();
    printf("gltri: %d frames, glGetError %#x, a pixel %02x%02x%02x%02x\n", frames, err, px[0], px[1], px[2], px[3]);
    OSMesaDestroyContext(ctx);
    return err != GL_NO_ERROR;
}
