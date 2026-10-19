/* SPDX-License-Identifier: BSD-2-Clause */
package org.lwjgl;

/** LWJGL 2's checked exception (Banana OS: LWJGL 2 over LWJGL 3) */
public class LWJGLException extends Exception {
    private static final long serialVersionUID = 1L;
    public LWJGLException() { super(); }
    public LWJGLException(String msg) { super(msg); }
    public LWJGLException(String msg, Throwable t) { super(msg, t); }
    public LWJGLException(Throwable t) { super(t); }
}
