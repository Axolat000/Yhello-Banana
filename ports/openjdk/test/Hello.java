/* SPDX-License-Identifier: BSD-2-Clause */
/* The OpenJDK port's first test: java -cp /mnt/virtio Hello */
public class Hello {
    public static void main(String[] args) {
        System.out.println("Hello from Java " + System.getProperty("java.version") + " on " + System.getProperty("os.name"));
        long s = 0;
        for (int i = 0; i < 1000000; i++) s += i % 7;
        System.out.println("sum=" + s);
    }
}
