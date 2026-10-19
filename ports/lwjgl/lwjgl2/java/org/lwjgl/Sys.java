/* SPDX-License-Identifier: BSD-2-Clause */
package org.lwjgl;

/** LWJGL 2's Sys: the clock (milliseconds), the version, URLs */
public final class Sys {
    private Sys() {}
    private static final long T0 = System.nanoTime();

    public static String getVersion() { return "2.9.4-banana (LWJGL 3 underneath)"; }
    public static void initialize() {}
    public static long getTimerResolution() { return 1000L; }
    public static long getTime() { return (System.nanoTime() - T0) / 1000000L; }
    public static boolean is64Bit() { return true; }
    public static void alert(String title, String message) { System.err.println(title + ": " + message); }
    public static boolean openURL(String url) { return false; }       /* (no browser to hand it to) */
    public static String getClipboard() { return org.lwjgl.opengl.Display.clipboard(); }
}
