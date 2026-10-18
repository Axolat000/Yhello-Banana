package banana.lwjgl2.r.org.lwjgl.opengl;

import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.charset.StandardCharsets;

/** LWJGL 2's GL20 methods that LWJGL 3 names otherwise */
public final class GL20 {
    private GL20() {}

    /** the source's bytes (from the buffer's position), as LWJGL 2 took them */
    static String text(ByteBuffer b) {
        ByteBuffer d = b.duplicate();
        byte[] a = new byte[d.remaining()];
        d.get(a);
        return new String(a, StandardCharsets.UTF_8);
    }

    public static void glShaderSource(int shader, ByteBuffer string) { org.lwjgl.opengl.GL20.glShaderSource(shader, text(string)); }

    public static void glUniform1(int location, FloatBuffer v) { org.lwjgl.opengl.GL20.glUniform1fv(location, v); }
    public static void glUniform2(int location, FloatBuffer v) { org.lwjgl.opengl.GL20.glUniform2fv(location, v); }
    public static void glUniform3(int location, FloatBuffer v) { org.lwjgl.opengl.GL20.glUniform3fv(location, v); }
    public static void glUniform4(int location, FloatBuffer v) { org.lwjgl.opengl.GL20.glUniform4fv(location, v); }
    public static void glUniform1(int location, IntBuffer v) { org.lwjgl.opengl.GL20.glUniform1iv(location, v); }
    public static void glUniform2(int location, IntBuffer v) { org.lwjgl.opengl.GL20.glUniform2iv(location, v); }
    public static void glUniform3(int location, IntBuffer v) { org.lwjgl.opengl.GL20.glUniform3iv(location, v); }
    public static void glUniform4(int location, IntBuffer v) { org.lwjgl.opengl.GL20.glUniform4iv(location, v); }
    public static void glUniformMatrix2(int location, boolean transpose, FloatBuffer m) { org.lwjgl.opengl.GL20.glUniformMatrix2fv(location, transpose, m); }
    public static void glUniformMatrix3(int location, boolean transpose, FloatBuffer m) { org.lwjgl.opengl.GL20.glUniformMatrix3fv(location, transpose, m); }
    public static void glUniformMatrix4(int location, boolean transpose, FloatBuffer m) { org.lwjgl.opengl.GL20.glUniformMatrix4fv(location, transpose, m); }
}
