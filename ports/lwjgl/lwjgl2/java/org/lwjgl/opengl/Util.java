/* SPDX-License-Identifier: BSD-2-Clause */
package org.lwjgl.opengl;

/** LWJGL 2's org.lwjgl.opengl.Util (GLU and Minecraft ask it for error names) */
public final class Util {
    private Util() {}

    public static void checkGLError() throws OpenGLException {
        int err = org.lwjgl.opengl.GL11.glGetError();
        if (err != org.lwjgl.opengl.GL11.GL_NO_ERROR) throw new OpenGLException(err);
    }

    public static String translateGLErrorString(int code) {
        switch (code) {
            case 0x0000: return "No error";
            case 0x0500: return "Invalid enum";
            case 0x0501: return "Invalid value";
            case 0x0502: return "Invalid operation";
            case 0x0503: return "Stack overflow";
            case 0x0504: return "Stack underflow";
            case 0x0505: return "Out of memory";
            case 0x0506: return "Invalid framebuffer operation";
            case 0x8031: return "Table too large";
            default: return null;
        }
    }
}
