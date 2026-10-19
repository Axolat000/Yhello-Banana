/* SPDX-License-Identifier: BSD-2-Clause */
package banana.lwjgl2.r.org.lwjgl.openal;

import java.nio.FloatBuffer;
import java.nio.IntBuffer;

/** LWJGL 2's AL10 methods that LWJGL 3 names otherwise */
public final class AL10 {
    private AL10() {}

    public static void alListener(int pname, FloatBuffer value) { org.lwjgl.openal.AL10.alListenerfv(pname, value); }
    public static void alSource(int source, int pname, FloatBuffer value) { org.lwjgl.openal.AL10.alSourcefv(source, pname, value); }
    public static void alSourceStop(IntBuffer sources) { org.lwjgl.openal.AL10.alSourceStopv(sources); }
}
