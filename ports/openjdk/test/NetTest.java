/* SPDX-License-Identifier: BSD-2-Clause */
import java.io.*;
import java.net.*;
import java.nio.ByteBuffer;
import java.nio.channels.*;
import java.nio.charset.StandardCharsets;
import java.util.Iterator;

/* The network on Banana OS: a small HTTP server on a Selector (epoll and
 * eventfd underneath), asked by HttpURLConnection, a SocketChannel and a
 * plain Socket over the loopback; Selector.wakeup, a Pipe, a ServerSocket
 * echo, and an HTTP request out to the Internet if there is a way. */
public class NetTest {
    static int failed;

    static void check(String what, boolean ok, String note) {
        System.out.println("  " + (ok ? "ok  " : "FAIL") + "  " + what + (note.isEmpty() ? "" : " - " + note));
        if (!ok) failed++;
    }

    /* answers every request with a page naming its path, then closes */
    static void serve(Selector sel, ServerSocketChannel ss, int requests) throws IOException {
        int done = 0;
        while (done < requests) {
            sel.select(10000);
            Iterator<SelectionKey> it = sel.selectedKeys().iterator();
            while (it.hasNext()) {
                SelectionKey k = it.next();
                it.remove();
                if (k.isAcceptable()) {
                    SocketChannel c = ss.accept();
                    if (c == null) continue;
                    c.configureBlocking(false);
                    c.register(sel, SelectionKey.OP_READ, new StringBuilder());
                } else if (k.isReadable()) {
                    SocketChannel c = (SocketChannel) k.channel();
                    StringBuilder req = (StringBuilder) k.attachment();
                    ByteBuffer b = ByteBuffer.allocate(4096);
                    int n = c.read(b);
                    if (n > 0) req.append(new String(b.array(), 0, n, StandardCharsets.ISO_8859_1));
                    if (n < 0 || req.indexOf("\r\n\r\n") >= 0) {
                        String first = req.length() > 0 ? req.substring(0, Math.max(0, req.indexOf("\r\n"))) : "";
                        String path = first.split(" ").length > 1 ? first.split(" ")[1] : "?";
                        String body = "Banana OS says hello to " + path + "\n";
                        String resp = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + body.length() +
                                      "\r\nConnection: close\r\n\r\n" + body;
                        k.attach(ByteBuffer.wrap(resp.getBytes(StandardCharsets.ISO_8859_1)));
                        k.interestOps(SelectionKey.OP_WRITE);
                    }
                } else if (k.isWritable()) {
                    SocketChannel c = (SocketChannel) k.channel();
                    ByteBuffer out = (ByteBuffer) k.attachment();
                    c.write(out);
                    if (!out.hasRemaining()) {
                        k.cancel();
                        c.close();
                        done++;
                    }
                }
            }
        }
        sel.selectNow();            /* (a registered channel is closed for good at the next select) */
    }

    static String readAll(InputStream in) throws IOException {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] b = new byte[4096];
        int n;
        while ((n = in.read(b)) > 0) o.write(b, 0, n);
        return o.toString(StandardCharsets.ISO_8859_1);
    }

    public static void main(String[] args) throws Exception {
        System.out.println("NetTest: java.nio and java.net on " + System.getProperty("os.name") + " " + System.getProperty("os.arch"));

        /* Selector.wakeup from another thread */
        Selector ws = Selector.open();
        check("Selector.open (" + ws.getClass().getSimpleName() + ")", true, "");
        long t0 = System.nanoTime();
        int got0 = ws.selectNow();
        Thread waker = new Thread(() -> { try { Thread.sleep(200); } catch (InterruptedException e) {} ws.wakeup(); });
        waker.start();
        int got1 = ws.select(10000);
        long ms = (System.nanoTime() - t0) / 1000000;
        waker.join();
        check("Selector.wakeup ends a select from another thread", got0 == 0 && got1 == 0 && ms >= 150 && ms < 5000, ms + " ms (200)");
        t0 = System.nanoTime();
        ws.select(150);
        ms = (System.nanoTime() - t0) / 1000000;
        check("select(150) times out", ms >= 140 && ms < 3000, ms + " ms");
        ws.close();

        /* the HTTP server on a Selector */
        Selector sel = Selector.open();
        ServerSocketChannel ss = ServerSocketChannel.open();
        ss.bind(new InetSocketAddress(InetAddress.getLoopbackAddress(), 0));
        ss.configureBlocking(false);
        ss.register(sel, SelectionKey.OP_ACCEPT);
        int port = ((InetSocketAddress) ss.getLocalAddress()).getPort();
        check("ServerSocketChannel bound on the loopback", port > 0, "port " + port);
        final Throwable[] serr = new Throwable[1];
        Thread server = new Thread(() -> { try { serve(sel, ss, 3); } catch (Throwable e) { serr[0] = e; } });
        server.start();

        /* 1: HttpURLConnection */
        URL url = new URL("http://127.0.0.1:" + port + "/hello");
        HttpURLConnection hc = (HttpURLConnection) url.openConnection();
        hc.setConnectTimeout(10000);
        hc.setReadTimeout(10000);
        int code = hc.getResponseCode();
        String body = readAll(hc.getInputStream());
        hc.disconnect();
        check("HttpURLConnection GET " + url, code == 200 && body.equals("Banana OS says hello to /hello\n"), code + " " + body.trim());

        /* 2: a blocking SocketChannel */
        SocketChannel sc = SocketChannel.open(new InetSocketAddress("127.0.0.1", port));
        sc.write(ByteBuffer.wrap("GET /channel HTTP/1.0\r\nHost: x\r\n\r\n".getBytes(StandardCharsets.ISO_8859_1)));
        String resp = readAll(Channels.newInputStream(sc));
        sc.close();
        check("SocketChannel: a request by hand", resp.startsWith("HTTP/1.0 200 OK") && resp.endsWith("hello to /channel\n"), resp.length() + " bytes");

        /* 3: a plain Socket, with a timeout */
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress("localhost", port), 5000);
            s.setSoTimeout(10000);
            s.getOutputStream().write("GET /socket HTTP/1.0\r\n\r\n".getBytes(StandardCharsets.ISO_8859_1));
            resp = readAll(s.getInputStream());
        }
        check("Socket to localhost", resp.endsWith("hello to /socket\n"), "");
        server.join(20000);
        check("the server answered all three and stopped", !server.isAlive() && serr[0] == null, serr[0] == null ? "" : serr[0].toString());
        sel.close();
        ss.close();

        /* a ServerSocket echo, 1 MB through the loopback */
        try (ServerSocket srv = new ServerSocket(0, 4, InetAddress.getLoopbackAddress())) {
            Thread echo = new Thread(() -> {
                try (Socket c = srv.accept()) {
                    InputStream in = c.getInputStream();
                    OutputStream out = c.getOutputStream();
                    byte[] b = new byte[8192];
                    int n;
                    while ((n = in.read(b)) > 0) out.write(b, 0, n);
                } catch (IOException e) { serr[0] = e; }
            });
            echo.start();
            byte[] data = new byte[1 << 20];
            for (int i = 0; i < data.length; i++) data[i] = (byte) (i * 31 + (i >> 11));
            byte[] back = new byte[data.length];
            try (Socket c = new Socket(InetAddress.getLoopbackAddress(), srv.getLocalPort())) {
                Thread writer = new Thread(() -> {
                    try { c.getOutputStream().write(data); c.shutdownOutput(); } catch (IOException e) { serr[0] = e; }
                });
                t0 = System.nanoTime();
                writer.start();
                InputStream in = c.getInputStream();
                int at = 0, n;
                while (at < back.length && (n = in.read(back, at, back.length - at)) > 0) at += n;
                writer.join();
                ms = (System.nanoTime() - t0) / 1000000;
                check("ServerSocket echo of 1 MB", at == data.length && java.util.Arrays.equals(data, back), at + " bytes in " + ms + " ms");
            }
            echo.join(10000);
        }

        /* a Pipe (pipe() underneath), with a Selector */
        Pipe p = Pipe.open();
        Selector ps = Selector.open();
        p.source().configureBlocking(false);
        p.source().register(ps, SelectionKey.OP_READ);
        p.sink().write(ByteBuffer.wrap("through the pipe".getBytes(StandardCharsets.ISO_8859_1)));
        int ready = ps.select(5000);
        ByteBuffer pb = ByteBuffer.allocate(64);
        p.source().read(pb);
        check("Pipe: written, selected, read", ready == 1 && new String(pb.array(), 0, pb.position(), StandardCharsets.ISO_8859_1).equals("through the pipe"), "");
        p.sink().close();
        p.source().close();
        ps.close();

        /* out to the Internet, if there is a way */
        try {
            InetAddress ex = InetAddress.getByName("example.com");
            HttpURLConnection oc = (HttpURLConnection) new URL("http://example.com/").openConnection();
            oc.setConnectTimeout(10000);
            oc.setReadTimeout(10000);
            int oc_code = oc.getResponseCode();
            String page = readAll(oc.getInputStream());
            check("HTTP to the Internet: http://example.com/", oc_code == 200 && page.contains("Example Domain"), ex.getHostAddress() + ", " + page.length() + " bytes");
        } catch (UnknownHostException e) {
            System.out.println("  skip  the Internet - example.com could not be looked up (no network?)");
        }

        System.out.println(failed == 0 ? "NETTEST PASS" : "NETTEST FAIL: " + failed + " failed");
    }
}
