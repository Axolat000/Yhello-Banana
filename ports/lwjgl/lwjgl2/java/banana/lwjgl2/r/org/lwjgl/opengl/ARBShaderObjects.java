package banana.lwjgl2.r.org.lwjgl.opengl;

import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;

/** LWJGL 2's ARBShaderObjects methods that LWJGL 3 names otherwise */
public final class ARBShaderObjects {
    private ARBShaderObjects() {}

    public static void glShaderSourceARB(int shader, ByteBuffer string) { org.lwjgl.opengl.ARBShaderObjects.glShaderSourceARB(shader, GL20.text(string)); }

    public static void glUniform1ARB(int location, FloatBuffer v) { org.lwjgl.opengl.ARBShaderObjects.glUniform1fvARB(location, v); }
    public static void glUniform2ARB(int location, FloatBuffer v) { org.lwjgl.opengl.ARBShaderObjects.glUniform2fvARB(location, v); }
    public static void glUniform3ARB(int location, FloatBuffer v) { org.lwjgl.opengl.ARBShaderObjects.glUniform3fvARB(location, v); }
    public static void glUniform4ARB(int location, FloatBuffer v) { org.lwjgl.opengl.ARBShaderObjects.glUniform4fvARB(location, v); }
    public static void glUniform1ARB(int location, IntBuffer v) { org.lwjgl.opengl.ARBShaderObjects.glUniform1ivARB(location, v); }
    public static void glUniform2ARB(int location, IntBuffer v) { org.lwjgl.opengl.ARBShaderObjects.glUniform2ivARB(location, v); }
    public static void glUniform3ARB(int location, IntBuffer v) { org.lwjgl.opengl.ARBShaderObjects.glUniform3ivARB(location, v); }
    public static void glUniform4ARB(int location, IntBuffer v) { org.lwjgl.opengl.ARBShaderObjects.glUniform4ivARB(location, v); }
    public static void glUniformMatrix2ARB(int location, boolean transpose, FloatBuffer m) { org.lwjgl.opengl.ARBShaderObjects.glUniformMatrix2fvARB(location, transpose, m); }
    public static void glUniformMatrix3ARB(int location, boolean transpose, FloatBuffer m) { org.lwjgl.opengl.ARBShaderObjects.glUniformMatrix3fvARB(location, transpose, m); }
    public static void glUniformMatrix4ARB(int location, boolean transpose, FloatBuffer m) { org.lwjgl.opengl.ARBShaderObjects.glUniformMatrix4fvARB(location, transpose, m); }
}
