package banana.lwjgl2.r.org.lwjgl.opengl;

import java.nio.IntBuffer;

/** LWJGL 2's ARBOcclusionQuery methods that LWJGL 3 names otherwise */
public final class ARBOcclusionQuery {
    private ARBOcclusionQuery() {}

    public static void glGetQueryObjectuARB(int id, int pname, IntBuffer params) { org.lwjgl.opengl.ARBOcclusionQuery.glGetQueryObjectuivARB(id, pname, params); }
}
