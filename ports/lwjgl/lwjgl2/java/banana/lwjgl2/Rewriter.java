/* SPDX-License-Identifier: BSD-2-Clause */
package banana.lwjgl2;

import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

import jdk.internal.org.objectweb.asm.ClassReader;
import jdk.internal.org.objectweb.asm.ClassVisitor;
import jdk.internal.org.objectweb.asm.ClassWriter;
import jdk.internal.org.objectweb.asm.MethodVisitor;
import jdk.internal.org.objectweb.asm.Opcodes;

/**
 * Calls to LWJGL 2 methods that LWJGL 3 has under another name or signature
 * (GL11.glGetFloat(int, FloatBuffer), AL.create()...) go to the redirect
 * class banana/lwjgl2/r/<owner> instead, which has a static method of the
 * same name and descriptor. Only those: a call LWJGL 3 has as it is stays.
 * Nothing moves in the class (same descriptors), so the constant pool is
 * copied and no frames are computed again.
 */
final class Rewriter {
    private static final String R = "banana/lwjgl2/r/";
    private static final byte[] MARK = "org/lwjgl/".getBytes();

    private final Loader loader;
    /* owner -> "name desc" of its redirect class's methods (empty: none) */
    private final ConcurrentHashMap<String, Set<String>> redirects = new ConcurrentHashMap<>();

    Rewriter(Loader loader) { this.loader = loader; }

    byte[] rewrite(byte[] b) {
        if (!mentions(b)) return b;
        ClassReader cr = new ClassReader(b);
        boolean[] changed = { false };
        ClassWriter cw = new ClassWriter(cr, 0);
        cr.accept(new ClassVisitor(Opcodes.ASM7, cw) {
            @Override
            public MethodVisitor visitMethod(int acc, String name, String desc, String sig, String[] ex) {
                MethodVisitor mv = super.visitMethod(acc, name, desc, sig, ex);
                return new MethodVisitor(Opcodes.ASM7, mv) {
                    @Override
                    public void visitMethodInsn(int op, String owner, String mname, String mdesc, boolean itf) {
                        if (op == Opcodes.INVOKESTATIC && owner.startsWith("org/lwjgl/")
                                && redirectsOf(owner).contains(mname + " " + mdesc)) {
                            owner = R + owner;
                            changed[0] = true;
                        }
                        super.visitMethodInsn(op, owner, mname, mdesc, itf);
                    }
                };
            }
        }, 0);
        return changed[0] ? cw.toByteArray() : b;
    }

    private static boolean mentions(byte[] b) {
        outer:
        for (int i = 0, n = b.length - MARK.length; i <= n; i++) {
            for (int k = 0; k < MARK.length; k++) if (b[i + k] != MARK[k]) continue outer;
            return true;
        }
        return false;
    }

    private Set<String> redirectsOf(String owner) {
        return redirects.computeIfAbsent(owner, o -> {
            byte[] b = loader.classBytes(R + o);
            if (b == null) return Collections.emptySet();
            Set<String> s = new HashSet<>();
            new ClassReader(b).accept(new ClassVisitor(Opcodes.ASM7) {
                @Override
                public MethodVisitor visitMethod(int acc, String name, String desc, String sig, String[] ex) {
                    if ((acc & Opcodes.ACC_STATIC) != 0 && (acc & Opcodes.ACC_PUBLIC) != 0) s.add(name + " " + desc);
                    return null;
                }
            }, ClassReader.SKIP_CODE);
            return s;
        });
    }
}
