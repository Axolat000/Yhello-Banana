/* SPDX-License-Identifier: BSD-2-Clause */
package org.lwjgl.input;

/** (Banana OS) how org.lwjgl.opengl.Display hands GLFW's input to Keyboard and Mouse */
public final class BananaInput {
    private BananaInput() {}

    public static void attach(long window) { Mouse.attach(window); }
    public static void detach() { Mouse.detach(); Keyboard.reset(); }
    public static void key(int glfwKey, int action) { Keyboard.glfwKey(glfwKey, action); }
    public static void character(int codepoint) { Keyboard.glfwChar(codepoint); }
    public static void cursor(double x, double y) { Mouse.glfwCursor(x, y); }
    public static void button(int button, int action) { Mouse.glfwButton(button, action); }
    public static void scroll(double yoffset) { Mouse.glfwScroll(yoffset); }
    public static void enter(boolean inside) { Mouse.glfwEnter(inside); }
}
