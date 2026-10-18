package org.lwjgl.opengl;

/** LWJGL 2's DisplayMode: a size, bits per pixel and refresh rate */
public final class DisplayMode {
    private final int width, height, bpp, freq;
    private final boolean fullscreen;

    public DisplayMode(int width, int height) { this(width, height, 0, 0, false); }
    DisplayMode(int width, int height, int bpp, int freq, boolean fullscreen) {
        this.width = width; this.height = height; this.bpp = bpp; this.freq = freq; this.fullscreen = fullscreen;
    }

    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public int getBitsPerPixel() { return bpp; }
    public int getFrequency() { return freq; }
    public boolean isFullscreenCapable() { return fullscreen; }

    @Override public boolean equals(Object o) {
        if (!(o instanceof DisplayMode)) return false;
        DisplayMode m = (DisplayMode) o;
        return m.width == width && m.height == height && m.bpp == bpp && m.freq == freq;
    }
    @Override public int hashCode() { return width ^ height ^ freq ^ bpp; }
    @Override public String toString() { return width + " x " + height + " x " + bpp + " @" + freq + "Hz"; }
}
