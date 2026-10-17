// A small CPU benchmark: the JIT (C1 / C2) compiles these loops after a few
// rounds, so later rounds should get much faster than the first one.
public class Bench {
    static int primes(int n) {
        int count = 0;
        for (int i = 2; i < n; i++) {
            boolean p = true;
            for (int d = 2; d * d <= i; d++)
                if (i % d == 0) { p = false; break; }
            if (p) count++;
        }
        return count;
    }

    static long mix(int n) {
        long h = 1125899906842597L;
        int[] a = new int[1024];
        for (int i = 0; i < n; i++) {
            a[i & 1023] += i;
            h = 31 * h + a[(i * 7) & 1023];
        }
        return h;
    }

    public static void main(String[] args) {
        int rounds = args.length > 0 ? Integer.parseInt(args[0]) : 6;
        System.out.println("vm: " + System.getProperty("java.vm.name") + " (" + System.getProperty("java.vm.info") + ")");
        long sum = 0;
        for (int r = 1; r <= rounds; r++) {
            long t = System.nanoTime();
            sum += primes(200000) + mix(5000000);
            System.out.println("round " + r + ": " + (System.nanoTime() - t) / 1000000 + " ms");
        }
        System.out.println("bench done " + sum);
    }
}
