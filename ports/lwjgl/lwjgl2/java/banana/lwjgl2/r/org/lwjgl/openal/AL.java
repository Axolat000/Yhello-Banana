package banana.lwjgl2.r.org.lwjgl.openal;

import java.nio.ByteBuffer;
import java.nio.IntBuffer;

import org.lwjgl.LWJGLException;
import org.lwjgl.openal.ALC;
import org.lwjgl.openal.ALC10;
import org.lwjgl.openal.ALCCapabilities;

/** LWJGL 2's AL.create() / isCreated() / destroy(): the default device and a context on it, made current */
public final class AL {
    private AL() {}

    private static long device, context;

    public static void create() throws LWJGLException {
        if (context != 0) return;
        long dev = ALC10.alcOpenDevice((ByteBuffer) null);
        if (dev == 0) throw new LWJGLException("Could not open the OpenAL device");
        ALCCapabilities caps = ALC.createCapabilities(dev);
        long ctx = ALC10.alcCreateContext(dev, (IntBuffer) null);
        if (ctx == 0 || !ALC10.alcMakeContextCurrent(ctx)) {
            if (ctx != 0) ALC10.alcDestroyContext(ctx);
            ALC10.alcCloseDevice(dev);
            throw new LWJGLException("Could not make an OpenAL context");
        }
        org.lwjgl.openal.AL.createCapabilities(caps);
        device = dev;
        context = ctx;
    }

    public static boolean isCreated() { return context != 0; }

    public static void destroy() {
        if (context == 0) return;
        ALC10.alcMakeContextCurrent(0);
        ALC10.alcDestroyContext(context);
        ALC10.alcCloseDevice(device);
        context = device = 0;
    }
}
