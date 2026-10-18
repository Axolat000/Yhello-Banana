package org.lwjgl.opengl;

/** LWJGL 2's PixelFormat: what a window's framebuffer is asked to have (immutable: each with... is a copy) */
public final class PixelFormat {
    final int bpp, alpha, depth, stencil, samples;

    public PixelFormat() { this(0, 0, 8, 0, 0); }
    public PixelFormat(int alpha, int depth, int stencil) { this(0, alpha, depth, stencil, 0); }
    public PixelFormat(int alpha, int depth, int stencil, int samples) { this(0, alpha, depth, stencil, samples); }
    public PixelFormat(int bpp, int alpha, int depth, int stencil, int samples) {
        this.bpp = bpp; this.alpha = alpha; this.depth = depth; this.stencil = stencil; this.samples = samples;
    }

    public int getBitsPerPixel() { return bpp; }
    public int getAlphaBits() { return alpha; }
    public int getDepthBits() { return depth; }
    public int getStencilBits() { return stencil; }
    public int getSamples() { return samples; }
    public PixelFormat withBitsPerPixel(int v) { return new PixelFormat(v, alpha, depth, stencil, samples); }
    public PixelFormat withAlphaBits(int v) { return new PixelFormat(bpp, v, depth, stencil, samples); }
    public PixelFormat withDepthBits(int v) { return new PixelFormat(bpp, alpha, v, stencil, samples); }
    public PixelFormat withStencilBits(int v) { return new PixelFormat(bpp, alpha, depth, v, samples); }
    public PixelFormat withSamples(int v) { return new PixelFormat(bpp, alpha, depth, stencil, v); }
}
