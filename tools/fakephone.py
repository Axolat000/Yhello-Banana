#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A fake Android phone for QEMU over usbredir: adb (with RSA auth) and fastboot.

    fakephone.py PORT [adb|fastboot]
    qemu ... -chardev socket,id=ur,host=127.0.0.1,port=PORT \
             -device qemu-xhci,id=xh -device usb-redir,chardev=ur,bus=xh.0

Everything it does is logged to stdout (keys learned, files pushed, partitions flashed)."""
import base64, os, socket, struct, sys, threading, time

PORT = int(sys.argv[1])
MODE = sys.argv[2] if len(sys.argv) > 2 else "adb"

HELLO, CONNECT, DISCONNECT, RESET, IFACE_INFO, EP_INFO = 0, 1, 2, 3, 4, 5
SET_CONFIG, GET_CONFIG, CONFIG_STATUS = 6, 7, 8
SET_ALT, GET_ALT, ALT_STATUS = 9, 10, 11
CANCEL = 21
CONTROL, BULK = 100, 101
CAPS = (1 << 1) | (1 << 4) | (1 << 5) | (1 << 6)

def log(*a): print(*a, flush=True)

def strdesc(s):
    b = s.encode("utf-16-le")
    return bytes([2 + len(b), 3]) + b

def descriptors(mode):
    proto = 1 if mode == "adb" else 3
    pid = 0x4EE7 if mode == "adb" else 0x4EE0
    dev = struct.pack("<BBHBBBBHHHBBBB", 18, 1, 0x0200, 0, 0, 0, 64, 0x18D1, pid, 0x0100, 1, 2, 3, 1)
    body = (struct.pack("<BBBBBBBBB", 9, 4, 0, 0, 2, 0xFF, 0x42, proto, 4) +
            struct.pack("<BBBBHB", 7, 5, 0x81, 2, 512, 0) + struct.pack("<BBBBHB", 7, 5, 0x01, 2, 512, 0))
    cfg = struct.pack("<BBHBBBBB", 9, 2, 9 + len(body), 1, 1, 0, 0x80, 250) + body
    strings = {0: bytes([4, 3, 0x09, 0x04]), 1: strdesc("Google"), 2: strdesc("Fake Pixel"),
               3: strdesc("FAKE0001"), 4: strdesc("ADB Interface" if mode == "adb" else "fastboot")}
    return dev, cfg, strings, pid, proto

srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", PORT))
srv.listen(1)
srv.settimeout(90)
conn, _ = srv.accept()
lock = threading.Lock()
wide = False

def send(t, body, pid=0):
    with lock:
        conn.sendall((struct.pack("<IIQ", t, len(body), pid) if wide else struct.pack("<III", t, len(body), pid)) + body)

def recvn(n):
    b = b""
    while len(b) < n:
        c = conn.recv(n - len(b))
        if not c:
            raise EOFError
        b += c
    return b

send(HELLO, b"fakephone".ljust(64, b"\0") + struct.pack("<I", CAPS))
t, ln, _ = struct.unpack("<III", recvn(12))
recvn(ln)
wide = True

state = {"mode": MODE, "config": 0}
def attach(mode):
    dev, cfg, strings, pid, proto = descriptors(mode)
    state.update(mode=mode, dev=dev, cfg=cfg, strings=strings)
    ii = bytearray(4 + 128)
    struct.pack_into("<I", ii, 0, 1)
    ii[4 + 32], ii[4 + 64], ii[4 + 96] = 0xFF, 0x42, proto
    send(IFACE_INFO, bytes(ii))
    types, iv, ifs, mps = [255] * 32, [0] * 32, [0] * 32, [0] * 32
    types[0] = types[16] = 0
    mps[0] = mps[16] = 64
    types[1] = types[17] = 2
    mps[1] = mps[17] = 512
    send(EP_INFO, bytes(types) + bytes(iv) + bytes(ifs) + struct.pack("<32H", *mps))
    send(CONNECT, struct.pack("<BBBBHHH", 2, 0, 0, 0, 0x18D1, pid, 0x0100))
    log("attached as", mode)

# ── bulk IN: data waiting for the host's requests ──
inq = []            # byte strings, one per USB transfer we want to make
pending = []        # (id, length) IN requests from QEMU
qlock = threading.Lock()

def push_in(data):
    with qlock:
        inq.append(data)
        serve()

def serve():
    while inq and pending:
        pid, length = pending.pop(0)
        d = inq[0]
        part, rest = d[:length], d[length:]
        if rest: inq[0] = rest
        else: inq.pop(0)
        send(BULK, struct.pack("<BBHIH", 0x81, 0, len(part) & 0xFFFF, 0, len(part) >> 16) + part, pid)

# ── adb ──
A = {k: struct.unpack("<I", k.encode())[0] for k in ("CNXN", "AUTH", "OPEN", "OKAY", "CLSE", "WRTE")}
known_keys = []
token = os.urandom(20)
outbuf = b""
streams = {}
next_id = [100]
files = {"/sdcard/hello.txt": b"hello from the phone\n"}
reboot_to = [None]

def adb_send(cmd, a0, a1, data=b""):
    c = A[cmd]
    push_in(struct.pack("<6I", c, a0, a1, len(data), sum(data) & 0xFFFFFFFF, c ^ 0xFFFFFFFF))
    if data: push_in(data)

def verify(sig):
    info = bytes.fromhex("3021300906052b0e03021a05000414")
    want = b"\x00\x01" + b"\xff" * (256 - 3 - 35) + b"\x00" + info + token
    s = int.from_bytes(sig, "big")
    return any(pow(s, 65537, n).to_bytes(256, "big") == want for n in known_keys)

BANNER = b"device::ro.product.name=fakephone;ro.product.model=Pixel_Fake;ro.product.device=fake;features=shell_v1"

class Stream:
    def __init__(s, local, svc):
        s.local, s.remote, s.svc = local, next_id[0], svc
        next_id[0] += 1
        s.canwrite, s.q, s.data, s.line = True, [], b"", b""
    def write(s, data):
        while data:
            s.q.append(data[:4096]); data = data[4096:]
        s.flush()
    def flush(s):
        if s.canwrite and s.q and s.q[0] is not None:
            s.canwrite = False
            adb_send("WRTE", s.remote, s.local, s.q.pop(0))
    def close(s):
        s.q.append(None)
        s.flush2()
    def flush2(s):
        if s.q and s.q[0] is None and s.canwrite:
            adb_send("CLSE", s.remote, s.local)
            streams.pop(s.remote, None)

def run(cmd):
    if cmd.startswith("echo "): return cmd[5:].strip("'") + "\n"
    if cmd == "getprop ro.product.model": return "Pixel_Fake\n"
    if cmd.startswith("pm install"): log("installed", cmd); return "Success\n"
    if cmd.startswith("pm uninstall"): log("uninstalled", cmd); return "Success\n"
    if cmd.startswith("rm "): return ""
    if cmd.startswith("ls"): return "".join(p + "\n" for p in sorted(files))
    if cmd.startswith("logcat"): return "I/fake: line one\nI/fake: line two\n"
    return "/system/bin/sh: %s: inaccessible or not found\n" % cmd.split()[0]

def sync_feed(st):
    while True:
        d = st.data
        if len(d) < 8: return
        cmd, n = d[:4], struct.unpack("<I", d[4:8])[0]
        if cmd == b"SEND":
            if len(d) < 8 + n: return
            spec = d[8:8 + n].decode(); st.data = d[8 + n:]
            st.path = spec.rsplit(",", 1)[0]; st.content = b""
        elif cmd == b"DATA":
            if len(d) < 8 + n: return
            st.content += d[8:8 + n]; st.data = d[8 + n:]
        elif cmd == b"DONE":
            st.data = d[8:]
            files[st.path] = st.content
            log("pushed", st.path, len(st.content), "bytes")
            st.write(b"OKAY" + struct.pack("<I", 0))
        elif cmd == b"RECV":
            if len(d) < 8 + n: return
            path = d[8:8 + n].decode(); st.data = d[8 + n:]
            if path in files:
                c = files[path]
                out = b""
                for i in range(0, len(c), 65536):
                    out += b"DATA" + struct.pack("<I", len(c[i:i + 65536])) + c[i:i + 65536]
                st.write(out + b"DONE" + struct.pack("<I", 0))
            else:
                m = b"No such file or directory"
                st.write(b"FAIL" + struct.pack("<I", len(m)) + m)
        elif cmd == b"QUIT":
            st.data = d[8:]; st.close(); return
        else:
            log("sync: unknown", cmd); return

def adb_in(data):
    global outbuf, token
    outbuf += data
    while len(outbuf) >= 24:
        cmd, a0, a1, n, chk, magic = struct.unpack("<6I", outbuf[:24])
        if len(outbuf) < 24 + n: return
        body = outbuf[24:24 + n]; outbuf = outbuf[24 + n:]
        name = next((k for k, v in A.items() if v == cmd), "?")
        if name == "CNXN":
            token = os.urandom(20)
            log("CNXN from", body.rstrip(b"\0"))
            adb_send("AUTH", 1, 0, token)
        elif name == "AUTH" and a0 == 2:
            if verify(body):
                log("signature OK - known key"); adb_send("CNXN", 0x01000001, 256 * 1024, BANNER)
            else:
                log("signature from an unknown key"); token = os.urandom(20); adb_send("AUTH", 1, 0, token)
        elif name == "AUTH" and a0 == 3:
            text = body.rstrip(b"\0").decode()
            b64, _, who = text.partition(" ")
            raw = base64.b64decode(b64)
            nwords = struct.unpack("<I", raw[:4])[0]
            n_ = int.from_bytes(raw[8:8 + 4 * nwords], "little")
            known_keys.append(n_)
            log("public key accepted:", who, "modulus bits", n_.bit_length())
            adb_send("CNXN", 0x01000001, 256 * 1024, BANNER)
        elif name == "OPEN":
            svc = body.rstrip(b"\0").decode()
            log("OPEN", svc)
            st = Stream(a0, svc)
            streams[st.remote] = st
            adb_send("OKAY", st.remote, st.local)
            if svc.startswith("shell:") and len(svc) > 6:
                st.write(run(svc[6:]).encode()); st.close()
            elif svc == "shell:":
                st.write(b"fake:/ $ ")
            elif svc.startswith("reboot:"):
                reboot_to[0] = svc[7:] or "system"
                st.close()
        elif name == "OKAY":
            st = streams.get(a1)
            if st: st.canwrite = True; st.flush(); st.flush2()
        elif name == "WRTE":
            st = streams.get(a1)
            if not st: continue
            adb_send("OKAY", st.remote, st.local)
            if st.svc == "sync:":
                st.data += body; sync_feed(st)
            elif st.svc == "shell:":
                for ch in body:
                    if ch == 13:
                        line = st.line.decode(); st.line = b""
                        st.write(b"\r\n")
                        if line == "exit": st.close(); break
                        st.write(run(line).replace("\n", "\r\n").encode() + b"fake:/ $ ")
                    elif ch == 0x7F:
                        st.line = st.line[:-1]; st.write(b"\b \b")
                    else:
                        st.line += bytes([ch]); st.write(bytes([ch]))
        elif name == "CLSE":
            streams.pop(a1, None)

# ── fastboot ──
fb = {"expect": 0, "buf": b"", "img": b""}
def fb_in(data):
    if fb["expect"]:
        fb["buf"] += data
        if len(fb["buf"]) >= fb["expect"]:
            fb["img"], fb["expect"], fb["buf"] = fb["buf"], 0, b""
            log("downloaded", len(fb["img"]), "bytes")
            push_in(b"OKAY")
        return
    c = data.decode(errors="replace")
    log("fastboot:", c)
    if c == "getvar:max-download-size": push_in(b"OKAY0x01000000")
    elif c == "getvar:product": push_in(b"OKAYfakephone")
    elif c == "getvar:all":
        push_in(b"INFOproduct:fakephone"); push_in(b"INFOunlocked:yes"); push_in(b"OKAY")
    elif c.startswith("getvar:"): push_in(b"FAILVariable not found")
    elif c.startswith("download:"):
        fb["expect"] = int(c[9:], 16); push_in(b"DATA%08x" % fb["expect"])
    elif c.startswith("flash:"):
        log("flashed", c[6:], len(fb["img"]), "bytes, first", fb["img"][:16])
        push_in(b"INFOwriting"); push_in(b"OKAY")
    elif c.startswith("erase:"): push_in(b"OKAY")
    elif c.startswith("oem ") or c.startswith("flashing "): push_in(b"INFO" + c.encode()); push_in(b"OKAY")
    elif c == "reboot":
        push_in(b"OKAY"); reboot_to[0] = "system"
    elif c == "reboot-bootloader": push_in(b"OKAY")
    else: push_in(b"FAILunknown command")

def rebooter():
    while True:
        time.sleep(0.2)
        to = reboot_to[0]
        if not to: continue
        reboot_to[0] = None
        time.sleep(0.5)
        send(DISCONNECT, b"")
        log("rebooting to", to)
        with qlock:
            inq.clear(); pending.clear()
        time.sleep(1.5)
        attach("fastboot" if to == "bootloader" else "adb")
threading.Thread(target=rebooter, daemon=True).start()

attach(MODE)
try:
    while True:
        t, ln, pid = struct.unpack("<IIQ", recvn(16))
        body = recvn(ln)
        if t == CONTROL:
            ep, req, rtype, st, val, idx, length = struct.unpack("<BBBBHHH", body[:10])
            data, status = b"", 0
            if rtype & 0x80:
                if req == 6:
                    kind, i = val >> 8, val & 0xFF
                    data = {1: state["dev"], 2: state["cfg"], 3: state["strings"].get(i)}.get(kind)
                    if data is None: status, data = 4, b""
                elif req == 0: data = b"\0\0"
                elif req == 8: data = bytes([state["config"]])
                else: status = 4
                data = data[:length]
            send(CONTROL, struct.pack("<BBBBHHH", ep, req, rtype, status, val, idx, len(data)) + data, pid)
        elif t == SET_CONFIG:
            state["config"] = body[0]; send(CONFIG_STATUS, struct.pack("<BB", 0, body[0]), pid)
        elif t == GET_CONFIG:
            send(CONFIG_STATUS, struct.pack("<BB", 0, state["config"]), pid)
        elif t in (SET_ALT, GET_ALT):
            send(ALT_STATUS, struct.pack("<BBB", 0, body[0], body[1] if t == SET_ALT else 0), pid)
        elif t == BULK:
            ep, st, l16, sid, lhi = struct.unpack("<BBHIH", body[:10])
            length = l16 | lhi << 16
            if ep & 0x80:
                with qlock:
                    pending.append((pid, length)); serve()
            else:
                data = body[10:]
                send(BULK, struct.pack("<BBHIH", ep, 0, len(data) & 0xFFFF, 0, len(data) >> 16), pid)
                (adb_in if state["mode"] == "adb" else fb_in)(data)
        elif t == CANCEL:
            with qlock:
                for p in list(pending):
                    if p[0] == pid:
                        pending.remove(p)
                        send(BULK, struct.pack("<BBHIH", 0x81, 1, 0, 0, 0), pid)
except (EOFError, OSError):
    pass
