/* SPDX-License-Identifier: BSD-2-Clause */
package org.lwjgl.opengl;

/** LWJGL 2's GLContext: the current context's capabilities */
public final class GLContext {
    private GLContext() {}
    private static ContextCapabilities caps;
    private static GLCapabilities from;

    public static ContextCapabilities getCapabilities() {
        GLCapabilities c = GL.getCapabilities();
        if (caps == null || from != c) { caps = new ContextCapabilities(c); from = c; }
        return caps;
    }
}
