// LWJGL on Banana OS: a GLFW window with an OpenGL 3.3 core context, a
// GLSL 3.30 shader triangle turning on a blue background. Checks the
// pixels of the first frame (glReadPixels) and that GLFW's callbacks reach
// Java (libffi closures), then draws for a while (or until Esc / close).
//     java -Dos.name=Linux -Dorg.lwjgl.librarypath=/mnt/virtio/lwjgl -jar lwjgltest.jar [frames]
// (os.name: the JDK says "Banana OS", which LWJGL does not know; on "Linux"
// it takes the path our libraries implement: libglfw.so, libGL.so.1)
import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import org.lwjgl.Version;
import org.lwjgl.glfw.GLFWErrorCallback;
import org.lwjgl.opengl.GL;
import org.lwjgl.system.MemoryStack;
import static org.lwjgl.glfw.GLFW.*;
import static org.lwjgl.opengl.GL33C.*;

public class LwjglTriangle {
    static final String VS = "#version 330 core\n"
        + "layout(location = 0) in vec2 pos;\n"
        + "layout(location = 1) in vec3 col;\n"
        + "uniform float angle;\n"
        + "out vec3 c;\n"
        + "void main() {\n"
        + "    float s = sin(angle), k = cos(angle);\n"
        + "    gl_Position = vec4(k * pos.x - s * pos.y, s * pos.x + k * pos.y, 0.0, 1.0);\n"
        + "    c = col;\n"
        + "}\n";
    static final String FS = "#version 330 core\n"
        + "in vec3 c;\n"
        + "out vec4 frag;\n"
        + "void main() { frag = vec4(c, 1.0); }\n";

    static int fails;

    static void check(boolean ok, String what) {
        System.out.println("  " + (ok ? "ok   " : "FAIL ") + what);
        if (!ok) fails++;
    }

    static int shader(int type, String src) {
        int s = glCreateShader(type);
        glShaderSource(s, src);
        glCompileShader(s);
        if (glGetShaderi(s, GL_COMPILE_STATUS) == GL_FALSE) System.out.println(glGetShaderInfoLog(s));
        return s;
    }

    public static void main(String[] args) {
        int frames = args.length > 0 ? Integer.parseInt(args[0]) : 300;
        System.out.println("LWJGL " + Version.getVersion() + " on " + System.getProperty("os.name") + " " + System.getProperty("os.arch"));

        // a callback from C into Java: GLFW reports an error through it
        int[] err = {0};
        GLFWErrorCallback probe = GLFWErrorCallback.create((code, desc) -> err[0] = code);
        glfwSetErrorCallback(probe);
        check(glfwInit(), "glfwInit (" + glfwGetVersionString() + ")");
        glfwSetGamma(glfwGetPrimaryMonitor(), 1.0f);
        check(err[0] == GLFW_FEATURE_UNAVAILABLE, "error callback called from C (code 0x" + Integer.toHexString(err[0]) + ")");
        glfwSetErrorCallback(GLFWErrorCallback.createPrint(System.err)).free();

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        long win = glfwCreateWindow(480, 360, "LWJGL triangle", 0, 0);
        check(win != 0, "glfwCreateWindow");
        if (win == 0) { System.out.println("LWJGLTEST FAIL"); glfwTerminate(); return; }
        glfwSetKeyCallback(win, (w, key, sc, action, mods) -> {
            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(w, true);
        });
        glfwMakeContextCurrent(win);
        GL.createCapabilities();
        System.out.println("  GL " + glGetString(GL_VERSION) + " - " + glGetString(GL_RENDERER));
        check(GL.getCapabilities().OpenGL33, "OpenGL 3.3 capabilities");

        int prog = glCreateProgram();
        glAttachShader(prog, shader(GL_VERTEX_SHADER, VS));
        glAttachShader(prog, shader(GL_FRAGMENT_SHADER, FS));
        glLinkProgram(prog);
        check(glGetProgrami(prog, GL_LINK_STATUS) == GL_TRUE, "GLSL 3.30 program");
        int vao = glGenVertexArrays();
        glBindVertexArray(vao);
        int vbo = glGenBuffers();
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, new float[] {
            //  x      y     r  g  b
             0.0f,  0.8f,   1, 0, 0,
            -0.7f, -0.6f,   0, 1, 0,
             0.7f, -0.6f,   1, 1, 0,
        }, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, false, 20, 0);
        glVertexAttribPointer(1, 3, GL_FLOAT, false, 20, 8);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        int angle = glGetUniformLocation(prog, "angle");

        double t0 = glfwGetTime();
        int n = 0;
        while (!glfwWindowShouldClose(win) && n < frames) {
            int[] w = new int[1], h = new int[1];
            glfwGetFramebufferSize(win, w, h);
            glViewport(0, 0, w[0], h[0]);
            glClearColor(0.1f, 0.2f, 0.5f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(prog);
            glUniform1f(angle, n == 0 ? 0 : (float)(glfwGetTime() - t0));
            glDrawArrays(GL_TRIANGLES, 0, 3);
            if (n == 0) {
                try (MemoryStack st = MemoryStack.stackPush()) {
                    ByteBuffer px = st.malloc(4);
                    glReadPixels(w[0] / 2, h[0] / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
                    int r = px.get(0) & 255, g = px.get(1) & 255, b = px.get(2) & 255;
                    check(r > 100 && g > 100 && b < 60, "triangle pixel at the center (" + r + "," + g + "," + b + ")");
                    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
                    r = px.get(0) & 255; g = px.get(1) & 255; b = px.get(2) & 255;
                    check(r < 40 && g > 35 && g < 70 && b > 110 && b < 145, "background pixel in a corner (" + r + "," + g + "," + b + ")");
                }
                check(glGetError() == GL_NO_ERROR, "no GL error");
                System.out.println(fails == 0 ? "LWJGLTEST PASS" : "LWJGLTEST FAIL (" + fails + ")");
            }
            glfwSwapBuffers(win);
            glfwPollEvents();
            n++;
        }
        double dt = glfwGetTime() - t0;
        System.out.printf("  %d frames in %.1f s (%.1f fps)%n", n, dt, n / Math.max(dt, 0.001));
        glDeleteBuffers(vbo);
        glDeleteVertexArrays(vao);
        glDeleteProgram(prog);
        GL.setCapabilities(null);
        glfwDestroyWindow(win);
        glfwTerminate();
        System.out.println("LWJGLTEST DONE");
    }
}
