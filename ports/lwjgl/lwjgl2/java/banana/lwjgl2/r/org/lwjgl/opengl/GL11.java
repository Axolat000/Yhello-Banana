/* SPDX-License-Identifier: BSD-2-Clause */
package banana.lwjgl2.r.org.lwjgl.opengl;

import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.ShortBuffer;

/** LWJGL 2's GL11 methods that LWJGL 3 names otherwise (calls rewritten here by banana.lwjgl2.Rewriter) */
public final class GL11 {
    private GL11() {}

    public static void glGetFloat(int pname, FloatBuffer params) { org.lwjgl.opengl.GL11.glGetFloatv(pname, params); }
    public static void glGetInteger(int pname, IntBuffer params) { org.lwjgl.opengl.GL11.glGetIntegerv(pname, params); }
    public static void glFog(int pname, FloatBuffer params) { org.lwjgl.opengl.GL11.glFogfv(pname, params); }
    public static void glLight(int light, int pname, FloatBuffer params) { org.lwjgl.opengl.GL11.glLightfv(light, pname, params); }
    public static void glLightModel(int pname, FloatBuffer params) { org.lwjgl.opengl.GL11.glLightModelfv(pname, params); }
    public static void glMultMatrix(FloatBuffer m) { org.lwjgl.opengl.GL11.glMultMatrixf(m); }
    public static void glTexEnv(int target, int pname, FloatBuffer params) { org.lwjgl.opengl.GL11.glTexEnvfv(target, pname, params); }
    public static void glTexGen(int coord, int pname, FloatBuffer params) { org.lwjgl.opengl.GL11.glTexGenfv(coord, pname, params); }

    /* the pointer calls: LWJGL 2 takes the type from the buffer */
    public static void glColorPointer(int size, boolean unsigned, int stride, ByteBuffer pointer) {
        org.lwjgl.opengl.GL11.glColorPointer(size, unsigned ? org.lwjgl.opengl.GL11.GL_UNSIGNED_BYTE : org.lwjgl.opengl.GL11.GL_BYTE, stride, pointer);
    }
    public static void glNormalPointer(int stride, ByteBuffer pointer) {
        org.lwjgl.opengl.GL11.glNormalPointer(org.lwjgl.opengl.GL11.GL_BYTE, stride, pointer);
    }
    public static void glTexCoordPointer(int size, int stride, FloatBuffer pointer) {
        org.lwjgl.opengl.GL11.glTexCoordPointer(size, org.lwjgl.opengl.GL11.GL_FLOAT, stride, pointer);
    }
    public static void glTexCoordPointer(int size, int stride, ShortBuffer pointer) {
        org.lwjgl.opengl.GL11.glTexCoordPointer(size, org.lwjgl.opengl.GL11.GL_SHORT, stride, pointer);
    }
    public static void glVertexPointer(int size, int stride, FloatBuffer pointer) {
        org.lwjgl.opengl.GL11.glVertexPointer(size, org.lwjgl.opengl.GL11.GL_FLOAT, stride, pointer);
    }
}
