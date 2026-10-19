/* SPDX-License-Identifier: BSD-2-Clause */
// Checks what HotSpot's JIT needs from the system: in compiled code, a null
// pointer and a division by zero are caught as signals (SIGSEGV, SIGFPE) and
// become Java exceptions; a deep recursion hits the stack's guard pages
// (StackOverflowError); threads stop at safepoints (the polling page) for the
// garbage collector; code is deoptimized when a class it assumed is loaded.
import java.util.ArrayList;
import java.util.List;

public class JitTest {
    static int fails = 0;

    static void check(String what, boolean ok) {
        System.out.println((ok ? "ok   " : "FAIL ") + what);
        if (!ok) fails++;
    }

    static class Box { int v; Box(int v) { this.v = v; } }

    static int get(Box b) { return b.v; }                       // implicit null check

    static int div(int a, int b) { return a / b; }

    static int depth = 0;
    static void recurse() { depth++; recurse(); }

    interface Shape { int area(); }
    static class Sq implements Shape { public int area() { return 4; } }
    static class Tri implements Shape { public int area() { return 3; } }

    static int areas(Shape[] s) {
        int t = 0;
        for (Shape x : s) t += x.area();
        return t;
    }

    public static void main(String[] args) throws Exception {
        // warm up so C1 / C2 compile get() and div()
        Box b = new Box(1);
        long s = 0;
        for (int i = 0; i < 200000; i++) s += get(b) + div(i, 3);
        int npe = 0, ae = 0;
        for (int i = 0; i < 2000; i++) {
            try { get(i % 2 == 0 ? null : b); } catch (NullPointerException e) { npe++; }
            try { div(i, i % 2); } catch (ArithmeticException e) { ae++; }
        }
        check("NullPointerException from compiled code (" + npe + ")", npe == 1000);
        check("ArithmeticException from compiled code (" + ae + ")", ae == 1000);

        try { recurse(); check("StackOverflowError", false); }
        catch (StackOverflowError e) { check("StackOverflowError at depth " + depth, depth > 1000); }
        depth = 0;
        try { recurse(); check("StackOverflowError again", false); }
        catch (StackOverflowError e) { check("StackOverflowError again (guard pages re-armed)", depth > 1000); }

        // deoptimization: areas() is compiled seeing only Sq, then a Tri comes
        Shape[] sq = new Shape[100];
        for (int i = 0; i < sq.length; i++) sq[i] = new Sq();
        int a = 0;
        for (int i = 0; i < 20000; i++) a = areas(sq);
        sq[50] = new Tri();
        check("deoptimization (" + areas(sq) + ")", a == 400 && areas(sq) == 399);

        // threads allocating and computing while the GC stops them at safepoints
        final int N = 4;
        final long[] out = new long[N];
        List<Thread> ts = new ArrayList<>();
        for (int t = 0; t < N; t++) {
            final int id = t;
            Thread th = new Thread(() -> {
                long acc = 0;
                for (int r = 0; r < 300; r++) {
                    List<int[]> junk = new ArrayList<>();
                    for (int k = 0; k < 200; k++) junk.add(new int[256]);
                    for (int[] j : junk) acc += j.length;
                }
                out[id] = acc;
            });
            ts.add(th);
            th.start();
        }
        for (int k = 0; k < 5; k++) System.gc();
        for (Thread th : ts) th.join();
        boolean all = true;
        for (long v : out) all &= v == 300L * 200 * 256;
        check("4 threads + GC safepoints", all);

        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 100000; i++) sb.append((char) ('a' + i % 26));
        check("strings (" + sb.length() + ")", sb.toString().hashCode() == sb.toString().hashCode() && s > 0);

        System.out.println(fails == 0 ? "JITTEST PASS" : "JITTEST FAIL " + fails);
    }
}
