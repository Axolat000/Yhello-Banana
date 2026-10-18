package org.lwjgl.opengl;

/** LWJGL 2's unchecked OpenGL error */
public class OpenGLException extends RuntimeException {
    private static final long serialVersionUID = 1L;
    public OpenGLException(int code) { super(Util.translateGLErrorString(code) + " (" + code + ")"); }
    public OpenGLException(String msg) { super(msg); }
    public OpenGLException() { super(); }
}
