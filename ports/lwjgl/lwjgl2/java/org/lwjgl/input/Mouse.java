package org.lwjgl.input;

import java.util.ArrayDeque;

import org.lwjgl.LWJGLException;
import org.lwjgl.glfw.GLFW;

/**
 * LWJGL 2's Mouse over GLFW. Its coordinates start at the window's bottom
 * left (y goes up) - GLFW's at the top left, so every y is turned over.
 * getDX / getDY / getDWheel add up what moved since they were last asked;
 * events (buttons, motion, the wheel) queue up for next(). The wheel says
 * 120 a notch, as LWJGL 2 did on Windows (Minecraft only looks at the sign).
 */
public final class Mouse {
    private Mouse() {}

    public static final int EVENT_SIZE = 22;

    private static final class Ev {
        final int button, x, y, dx, dy, dwheel;
        final boolean state;
        final long nanos = System.nanoTime();
        Ev(int button, boolean state, int x, int y, int dx, int dy, int dwheel) {
            this.button = button; this.state = state; this.x = x; this.y = y; this.dx = dx; this.dy = dy; this.dwheel = dwheel;
        }
    }

    private static final ArrayDeque<Ev> queue = new ArrayDeque<>();
    private static final boolean[] buttons = new boolean[8];
    private static Ev current;
    private static boolean created, grabbed, inside = true, clip = true;
    private static long window;
    private static int x, y, dx, dy, dwheel;           /* (LWJGL 2's coordinates) */
    private static double lastX = Double.NaN, lastY = Double.NaN;

    /* ── from Display ── */
    static void attach(long w) {
        window = w;
        double[] cx = new double[1], cy = new double[1];
        GLFW.glfwGetCursorPos(w, cx, cy);
        lastX = cx[0];
        lastY = cy[0];
        x = (int) cx[0];
        y = height() - 1 - (int) cy[0];
    }

    static void detach() { window = 0; queue.clear(); current = null; }

    private static int height() {
        if (window == 0) return 0;
        int[] w = new int[1], h = new int[1];
        GLFW.glfwGetFramebufferSize(window, w, h);
        return h[0];
    }

    static void glfwCursor(double cx, double cy) {
        int mdx = 0, mdy = 0;
        if (!Double.isNaN(lastX)) {
            mdx = (int) Math.round(cx - lastX);
            mdy = (int) Math.round(lastY - cy);            /* (up is positive) */
        }
        lastX = cx;
        lastY = cy;
        if (mdx == 0 && mdy == 0) return;
        dx += mdx;
        dy += mdy;
        if (grabbed) {
            x += mdx;
            y += mdy;
        } else {
            x = (int) cx;
            y = height() - 1 - (int) cy;
        }
        if (clip && !grabbed) {
            x = Math.max(0, x);
            y = Math.max(0, y);
        }
        queue.add(new Ev(-1, false, x, y, mdx, mdy, 0));
    }

    static void glfwButton(int button, int action) {
        if (button < 0 || button >= buttons.length) return;
        buttons[button] = action == GLFW.GLFW_PRESS;
        queue.add(new Ev(button, buttons[button], x, y, 0, 0, 0));
    }

    static void glfwScroll(double yoffset) {
        int d = (int) Math.round(yoffset * 120);
        if (d == 0) return;
        dwheel += d;
        queue.add(new Ev(-1, false, x, y, 0, 0, d));
    }

    static void glfwEnter(boolean in) { inside = in; }

    /* ── the API ── */
    public static void create() throws LWJGLException { created = true; }
    public static boolean isCreated() { return created; }
    public static void destroy() { created = false; queue.clear(); current = null; }
    public static void poll() {}
    public static boolean isButtonDown(int b) { return b >= 0 && b < buttons.length && buttons[b]; }
    public static String getButtonName(int b) { return b >= 0 && b < 16 ? "BUTTON" + b : null; }
    public static int getButtonIndex(String name) {
        if (name != null && name.startsWith("BUTTON")) try { return Integer.parseInt(name.substring(6)); } catch (NumberFormatException e) { }
        return -1;
    }
    public static boolean next() { current = queue.poll(); return current != null; }
    public static int getEventButton() { return current != null ? current.button : -1; }
    public static boolean getEventButtonState() { return current != null && current.state; }
    public static int getEventDX() { return current != null ? current.dx : 0; }
    public static int getEventDY() { return current != null ? current.dy : 0; }
    public static int getEventX() { return current != null ? current.x : x; }
    public static int getEventY() { return current != null ? current.y : y; }
    public static int getEventDWheel() { return current != null ? current.dwheel : 0; }
    public static long getEventNanoseconds() { return current != null ? current.nanos : 0L; }
    public static int getX() { return x; }
    public static int getY() { return y; }
    public static int getDX() { int r = dx; dx = 0; return r; }
    public static int getDY() { int r = dy; dy = 0; return r; }
    public static int getDWheel() { int r = dwheel; dwheel = 0; return r; }
    public static int getButtonCount() { return 8; }
    public static boolean hasWheel() { return true; }
    public static boolean isGrabbed() { return grabbed; }
    public static boolean isInsideWindow() { return inside; }
    public static boolean isClipMouseCoordinatesToWindow() { return clip; }
    public static void setClipMouseCoordinatesToWindow(boolean on) { clip = on; }
    public static void updateCursor() {}

    public static void setGrabbed(boolean grab) {
        if (grabbed == grab) return;
        grabbed = grab;
        if (window != 0) {
            GLFW.glfwSetInputMode(window, GLFW.GLFW_CURSOR, grab ? GLFW.GLFW_CURSOR_DISABLED : GLFW.GLFW_CURSOR_NORMAL);
            double[] cx = new double[1], cy = new double[1];
            GLFW.glfwGetCursorPos(window, cx, cy);
            lastX = cx[0];
            lastY = cy[0];
        }
        dx = dy = 0;
    }

    public static void setCursorPosition(int nx, int ny) {
        x = nx;
        y = ny;
        if (window != 0 && !grabbed) {
            double gy = height() - 1 - ny;
            GLFW.glfwSetCursorPos(window, nx, gy);
            lastX = nx;
            lastY = gy;
        }
    }
}
