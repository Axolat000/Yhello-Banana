package banana.lwjgl2;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.JarURLConnection;
import java.net.URL;
import java.net.URLClassLoader;
import java.net.URLConnection;
import java.security.CodeSource;
import java.security.ProtectionDomain;
import java.security.cert.Certificate;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;

/**
 * LWJGL 2 games (Minecraft 1.7.10 to 1.12.2) on LWJGL 3: the system class
 * loader (-Djava.system.class.loader=banana.lwjgl2.Loader). It loads the
 * whole class path itself, child first, and on the way rewrites the calls
 * LWJGL 3 has under another name (GL11.glGetFloat -> glGetFloatv...) into
 * calls to the redirect classes, banana.lwjgl2.r.<the LWJGL 2 class> - see
 * Rewriter. What LWJGL 3 has no class for at all (Display, Keyboard, Mouse,
 * Sys...) is in this jar under its LWJGL 2 name.
 * Needs: --add-exports java.base/jdk.internal.org.objectweb.asm=ALL-UNNAMED
 */
public final class Loader extends URLClassLoader {
    static { registerAsParallelCapable(); }

    private final ConcurrentHashMap<String, ProtectionDomain> domains = new ConcurrentHashMap<>();
    private final Rewriter rewriter = new Rewriter(this);

    public Loader(ClassLoader parent) {
        super(classPath(), parent);
    }

    private static URL[] classPath() {
        List<URL> urls = new ArrayList<>();
        for (String p : System.getProperty("java.class.path", "").split(File.pathSeparator)) {
            if (p.isEmpty()) continue;
            try { urls.add(new File(p).toURI().toURL()); } catch (IOException e) { /* (a bad entry: left out) */ }
        }
        return urls.toArray(new URL[0]);
    }

    @Override
    protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
        synchronized (getClassLoadingLock(name)) {
            Class<?> c = findLoadedClass(name);
            if (c == null) {
                /* the JDK's, and this loader's own classes, from the parent; the rest from here first */
                if (name.startsWith("java.") || name.equals("banana.lwjgl2.Loader") || name.equals("banana.lwjgl2.Rewriter")
                        || name.startsWith("banana.lwjgl2.Rewriter$")) {
                    c = getParent().loadClass(name);
                } else {
                    try {
                        c = findClass(name);
                    } catch (ClassNotFoundException e) {
                        c = getParent().loadClass(name);
                    }
                }
            }
            if (resolve) resolveClass(c);
            return c;
        }
    }

    @Override
    protected Class<?> findClass(String name) throws ClassNotFoundException {
        String path = name.replace('.', '/') + ".class";
        URL url = findResource(path);
        if (url == null) throw new ClassNotFoundException(name);
        byte[] b;
        URL base;
        try {
            URLConnection uc = url.openConnection();
            uc.setUseCaches(false);
            try (InputStream in = uc.getInputStream()) { b = in.readAllBytes(); }
            base = uc instanceof JarURLConnection ? ((JarURLConnection) uc).getJarFileURL() : url;
        } catch (IOException e) {
            throw new ClassNotFoundException(name, e);
        }
        /* LWJGL 3 and this jar call LWJGL 3 as it is; the game and lwjgl_util (org.lwjgl.util) are rewritten */
        if (!name.startsWith("banana.lwjgl2.") && (!name.startsWith("org.lwjgl.") || name.startsWith("org.lwjgl.util.")))
            b = rewriter.rewrite(b);
        int dot = name.lastIndexOf('.');
        if (dot > 0) {
            String pkg = name.substring(0, dot);
            if (getDefinedPackage(pkg) == null) {
                try { definePackage(pkg, null, null, null, null, null, null, null); } catch (IllegalArgumentException e) { /* (defined meanwhile) */ }
            }
        }
        ProtectionDomain pd = domains.computeIfAbsent(base.toString(),
                k -> new ProtectionDomain(new CodeSource(base, (Certificate[]) null), null, this, null));
        return defineClass(name, b, 0, b.length, pd);
    }

    /** a class file of the class path, for the rewriter (null if none) */
    byte[] classBytes(String internalName) {
        URL url = findResource(internalName + ".class");
        if (url == null) return null;
        try (InputStream in = url.openStream()) { return in.readAllBytes(); } catch (IOException e) { return null; }
    }

    /* (the JDK calls this when an agent adds to the class path; kept for completeness) */
    void appendToClassPathForInstrumentation(String path) throws IOException {
        addURL(new File(path).toURI().toURL());
    }
}
