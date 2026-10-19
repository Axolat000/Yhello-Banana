/* SPDX-License-Identifier: BSD-2-Clause */
package org.lwjgl.opengl;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;

import org.lwjgl.LWJGLException;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.glfw.GLFWErrorCallback;
import org.lwjgl.glfw.GLFWImage;
import org.lwjgl.glfw.GLFWVidMode;
import org.lwjgl.input.BananaInput;
import org.lwjgl.input.Keyboard;
import org.lwjgl.input.Mouse;

/**
 * LWJGL 2's Display over a GLFW window (Banana OS: LWJGL 2 on LWJGL 3).
 * The context asks for no version and no profile: Banana OS's GLFW then
 * gives a compatibility context (glBegin, display lists, the matrix stack -
 * what Minecraft 1.7.10 to 1.12.2 draw with). update() swaps and takes the
 * window's events, which go to Keyboard and Mouse.
 */
public final class Display {
    private Display() {}

    private static long window;
    private static String title = "Game";
    private static DisplayMode mode = new DisplayMode(640, 480);
    private static boolean fullscreen, resizable, vsync, created, active = true;
    private static boolean resizedPending, resized;
    private static int width, height, windowedX = 50, windowedY = 50;
    private static ByteBuffer[] icons;
    private static boolean glfwUp;
    private static long syncNext;

    private static void glfwUp() throws LWJGLException {
        if (glfwUp) return;
        GLFWErrorCallback.createPrint(System.err).set();
        if (!GLFW.glfwInit()) throw new LWJGLException("GLFW could not start (no window system?)");
        glfwUp = true;
    }

    private static DisplayMode modeOf(GLFWVidMode v) {
        return new DisplayMode(v.width(), v.height(), v.redBits() + v.greenBits() + v.blueBits(), v.refreshRate(), true);
    }

    /* ── modes ── */
    public static DisplayMode getDesktopDisplayMode() {
        try { glfwUp(); } catch (LWJGLException e) { return new DisplayMode(1024, 768); }
        GLFWVidMode v = GLFW.glfwGetVideoMode(GLFW.glfwGetPrimaryMonitor());
        return v != null ? modeOf(v) : new DisplayMode(1024, 768);
    }

    public static DisplayMode[] getAvailableDisplayModes() throws LWJGLException {
        glfwUp();
        LinkedHashSet<DisplayMode> all = new LinkedHashSet<>();
        GLFWVidMode.Buffer b = GLFW.glfwGetVideoModes(GLFW.glfwGetPrimaryMonitor());
        if (b != null) for (int i = 0; i < b.limit(); i++) all.add(modeOf(b.get(i)));
        if (all.isEmpty()) all.add(getDesktopDisplayMode());
        return all.toArray(new DisplayMode[0]);
    }

    public static DisplayMode getDisplayMode() { return mode; }

    public static void setDisplayMode(DisplayMode m) throws LWJGLException {
        if (m == null) throw new NullPointerException("mode");
        mode = m;
        if (created) applyMode();
    }

    public static void setDisplayModeAndFullscreen(DisplayMode m) throws LWJGLException {
        mode = m;
        fullscreen = true;
        if (created) applyMode();
    }

    public static void setFullscreen(boolean on) throws LWJGLException {
        if (fullscreen == on) return;
        fullscreen = on;
        if (created) applyMode();
    }

    public static boolean isFullscreen() { return fullscreen; }

    private static void applyMode() {
        long monitor = GLFW.glfwGetPrimaryMonitor();
        if (fullscreen && monitor != 0) {
            if (GLFW.glfwGetWindowMonitor(window) == 0) {
                int[] x = new int[1], y = new int[1];
                GLFW.glfwGetWindowPos(window, x, y);
                windowedX = x[0];
                windowedY = y[0];
            }
            GLFWVidMode v = GLFW.glfwGetVideoMode(monitor);
            int w = v != null ? v.width() : mode.getWidth(), h = v != null ? v.height() : mode.getHeight();
            GLFW.glfwSetWindowMonitor(window, monitor, 0, 0, w, h, v != null ? v.refreshRate() : GLFW.GLFW_DONT_CARE);
        } else if (GLFW.glfwGetWindowMonitor(window) != 0) {
            GLFW.glfwSetWindowMonitor(window, 0, windowedX, windowedY, mode.getWidth(), mode.getHeight(), GLFW.GLFW_DONT_CARE);
        } else {
            GLFW.glfwSetWindowSize(window, mode.getWidth(), mode.getHeight());
        }
        GLFW.glfwSwapInterval(vsync ? 1 : 0);
    }

    /* ── the window ── */
    public static void create() throws LWJGLException { create(new PixelFormat()); }

    public static void create(PixelFormat pf) throws LWJGLException {
        if (created) throw new IllegalStateException("Only one LWJGL context may be instantiated at any one time.");
        glfwUp();
        GLFW.glfwDefaultWindowHints();
        GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE, GLFW.GLFW_FALSE);
        GLFW.glfwWindowHint(GLFW.GLFW_RESIZABLE, resizable ? GLFW.GLFW_TRUE : GLFW.GLFW_FALSE);
        if (pf != null) {
            if (pf.depth > 0) GLFW.glfwWindowHint(GLFW.GLFW_DEPTH_BITS, pf.depth);
            if (pf.stencil > 0) GLFW.glfwWindowHint(GLFW.GLFW_STENCIL_BITS, pf.stencil);
            if (pf.samples > 0) GLFW.glfwWindowHint(GLFW.GLFW_SAMPLES, pf.samples);
            if (pf.alpha > 0) GLFW.glfwWindowHint(GLFW.GLFW_ALPHA_BITS, pf.alpha);
        }
        long monitor = fullscreen ? GLFW.glfwGetPrimaryMonitor() : 0;
        int w = mode.getWidth(), h = mode.getHeight();
        if (monitor != 0) {
            GLFWVidMode v = GLFW.glfwGetVideoMode(monitor);
            if (v != null) { w = v.width(); h = v.height(); }
        }
        window = GLFW.glfwCreateWindow(w, h, title, monitor, 0);
        if (window == 0) throw new LWJGLException("Could not create the window (OpenGL context)");
        GLFW.glfwMakeContextCurrent(window);
        GL.createCapabilities();
        GLFW.glfwSwapInterval(vsync ? 1 : 0);

        GLFW.glfwSetKeyCallback(window, (win, key, scancode, action, mods) -> BananaInput.key(key, action));
        GLFW.glfwSetCharCallback(window, (win, cp) -> BananaInput.character(cp));
        GLFW.glfwSetMouseButtonCallback(window, (win, button, action, mods) -> BananaInput.button(button, action));
        GLFW.glfwSetCursorPosCallback(window, (win, x, y) -> BananaInput.cursor(x, y));
        GLFW.glfwSetScrollCallback(window, (win, xo, yo) -> BananaInput.scroll(yo));
        GLFW.glfwSetCursorEnterCallback(window, (win, in) -> BananaInput.enter(in));
        GLFW.glfwSetWindowFocusCallback(window, (win, focused) -> active = focused);
        GLFW.glfwSetFramebufferSizeCallback(window, (win, fw, fh) -> {
            if (fw != width || fh != height) { width = fw; height = fh; resizedPending = true; }
        });
        int[] fw = new int[1], fh = new int[1];
        GLFW.glfwGetFramebufferSize(window, fw, fh);
        width = fw[0];
        height = fh[0];
        if (icons != null) applyIcons();
        GLFW.glfwShowWindow(window);
        created = true;
        active = true;
        BananaInput.attach(window);
        Keyboard.create();
        Mouse.create();
    }

    public static boolean isCreated() { return created; }

    public static void destroy() {
        if (!created) return;
        Mouse.destroy();
        Keyboard.destroy();
        BananaInput.detach();
        org.lwjgl.glfw.Callbacks.glfwFreeCallbacks(window);
        GLFW.glfwDestroyWindow(window);
        window = 0;
        created = false;
    }

    public static void update() { update(true); }

    public static void update(boolean processMessages) {
        if (!created) return;
        GLFW.glfwSwapBuffers(window);
        if (processMessages) processMessages();
        resized = resizedPending;
        resizedPending = false;
    }

    public static void processMessages() { if (created) GLFW.glfwPollEvents(); }

    public static void swapBuffers() throws LWJGLException { if (created) GLFW.glfwSwapBuffers(window); }

    /** at most fps frames a second: sleeps what is left of this frame's time */
    public static void sync(int fps) {
        if (fps <= 0) return;
        long period = 1_000_000_000L / fps, now = System.nanoTime();
        if (syncNext == 0 || now - syncNext > period * 2) syncNext = now;
        long wait = syncNext - now;
        if (wait > 0) {
            try { Thread.sleep(wait / 1_000_000L, (int) (wait % 1_000_000L)); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
        }
        syncNext += period;
    }

    public static boolean isCloseRequested() { return created && GLFW.glfwWindowShouldClose(window); }
    public static boolean isActive() { return active; }
    public static boolean isVisible() { return true; }
    public static boolean isDirty() { return true; }
    public static boolean wasResized() { return resized; }
    public static boolean isCurrent() throws LWJGLException { return created && GLFW.glfwGetCurrentContext() == window; }
    public static void makeCurrent() throws LWJGLException { if (created) GLFW.glfwMakeContextCurrent(window); }
    public static void releaseContext() throws LWJGLException { GLFW.glfwMakeContextCurrent(0); }

    public static int getWidth() { return created ? width : mode.getWidth(); }
    public static int getHeight() { return created ? height : mode.getHeight(); }
    public static float getPixelScaleFactor() { return 1f; }

    public static int getX() {
        if (!created) return windowedX;
        int[] x = new int[1], y = new int[1];
        GLFW.glfwGetWindowPos(window, x, y);
        return x[0];
    }

    public static int getY() {
        if (!created) return windowedY;
        int[] x = new int[1], y = new int[1];
        GLFW.glfwGetWindowPos(window, x, y);
        return y[0];
    }

    public static void setLocation(int x, int y) {
        windowedX = x;
        windowedY = y;
        if (created && x >= 0 && y >= 0) GLFW.glfwSetWindowPos(window, x, y);
    }

    public static String getTitle() { return title; }

    public static void setTitle(String t) {
        title = t == null ? "" : t;
        if (created) GLFW.glfwSetWindowTitle(window, title);
    }

    public static void setResizable(boolean on) {
        resizable = on;
        if (created) GLFW.glfwSetWindowAttrib(window, GLFW.GLFW_RESIZABLE, on ? GLFW.GLFW_TRUE : GLFW.GLFW_FALSE);
    }

    public static boolean isResizable() { return resizable; }

    public static void setVSyncEnabled(boolean on) {
        vsync = on;
        if (created) GLFW.glfwSwapInterval(on ? 1 : 0);
    }

    public static void setSwapInterval(int n) { if (created) GLFW.glfwSwapInterval(n); }

    /** square RGBA images (16x16, 32x32...); how many were taken */
    public static int setIcon(ByteBuffer[] list) {
        icons = list;
        if (created) applyIcons();
        return list == null ? 0 : list.length;
    }

    private static void applyIcons() {
        List<ByteBuffer> ok = new ArrayList<>();
        for (ByteBuffer b : icons) if (b != null && b.isDirect() && b.remaining() >= 4) ok.add(b);
        if (ok.isEmpty()) return;
        try (GLFWImage.Buffer imgs = GLFWImage.malloc(ok.size())) {
            for (int i = 0; i < ok.size(); i++) {
                ByteBuffer b = ok.get(i);
                int side = (int) Math.sqrt(b.remaining() / 4.0);
                imgs.position(i);
                imgs.width(side);
                imgs.height(side);
                imgs.pixels(b);
            }
            imgs.position(0);
            GLFW.glfwSetWindowIcon(window, imgs);
        } catch (RuntimeException e) {
            /* (no icon: the window works all the same) */
        }
    }

    public static void setInitialBackground(float r, float g, float b) {}
    public static String getAdapter() { return null; }
    public static String getVersion() { return null; }

    /** (Sys.getClipboard) */
    public static String clipboard() {
        if (!created) return null;
        return GLFW.glfwGetClipboardString(window);
    }
}
