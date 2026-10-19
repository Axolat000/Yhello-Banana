#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A fake USB game controller for QEMU, over usbredir (the "host" side).

    fakepad.py MODE PORT        MODE: hid | x360 | xone
    qemu ... -chardev socket,id=ur,host=127.0.0.1,port=PORT \
             -device qemu-xhci,id=xh -device usb-redir,chardev=ur,bus=xh.0

The controller holds: A + Start, d-pad up, left stick full right, right
stick full up, right trigger full. Every packet it gets is logged."""
import socket, struct, sys, threading, time

MODE, PORT = sys.argv[1], int(sys.argv[2])

# usbredir packet types
HELLO, CONNECT, RESET, IFACE_INFO, EP_INFO = 0, 1, 3, 4, 5
SET_CONFIG, GET_CONFIG, CONFIG_STATUS = 6, 7, 8
SET_ALT, GET_ALT, ALT_STATUS = 9, 10, 11
START_INT, STOP_INT, INT_STATUS = 15, 16, 17
CONTROL, BULK, INTERRUPT = 100, 101, 103
CAPS = (1 << 1) | (1 << 4) | (1 << 5) | (1 << 6)   # connect_device_version, ep_info_max_packet_size, 64-bit ids, 32-bit bulk length

def strdesc(s):
    b = s.encode("utf-16-le")
    return bytes([2 + len(b), 3]) + b

if MODE == "hid":
    REPORT = bytes([
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,                 # Gamepad application
        0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x04,
        0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x81, 0x02,   # X Y Z Rz
        0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3B, 0x01,
        0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,     # hat, null state
        0x65, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0C, 0x15, 0x00, 0x25, 0x01,
        0x75, 0x01, 0x95, 0x0C, 0x81, 0x02,                 # 12 buttons
        0x05, 0x01, 0x09, 0x33, 0x09, 0x34, 0x15, 0x00, 0x26, 0xFF, 0x00,
        0x75, 0x08, 0x95, 0x02, 0x81, 0x02,                 # Rx Ry (the triggers)
        0xC0])
    DEV = struct.pack("<BBHBBBBHHHBBBB", 18, 1, 0x0200, 0, 0, 0, 64, 0x1234, 0x5678, 0x0100, 1, 2, 0, 1)
    CFG_BODY = (struct.pack("<BBBBBBBBB", 9, 4, 0, 0, 1, 3, 0, 0, 0) +
                struct.pack("<BBHBBBH", 9, 0x21, 0x0111, 0, 1, 0x22, len(REPORT)) +
                struct.pack("<BBBBHB", 7, 5, 0x81, 3, 64, 10))
    IFACE = (3, 0, 0)
    EPS = [(0x81, 3, 10, 64)]
    VIDPID = (0x1234, 0x5678)
    DEVCLASS = (0, 0, 0)
    PRODUCT = "Fake HID Gamepad"
    # X=255 Y=128 Z=128 Rz=0 | hat 0 (up), button 2 (A) | button 10 (Start) | Rx 0, Ry 255
    INPUT = bytes([255, 128, 128, 0, 0x00 | (1 << 5), 1 << 5, 0, 255])
else:
    REPORT = b""
    if MODE == "x360":
        DEV = struct.pack("<BBHBBBBHHHBBBB", 18, 1, 0x0200, 0xFF, 0xFF, 0xFF, 8, 0x045E, 0x028E, 0x0114, 1, 2, 3, 1)
        vendor = bytes([0x11, 0x21, 0x00, 0x01, 0x01, 0x25, 0x81, 0x14, 0x00, 0x00, 0x00, 0x00, 0x13, 0x01, 0x08, 0x00, 0x00])
        CFG_BODY = (struct.pack("<BBBBBBBBB", 9, 4, 0, 0, 2, 0xFF, 0x5D, 0x01, 0) + vendor +
                    struct.pack("<BBBBHB", 7, 5, 0x81, 3, 32, 4) + struct.pack("<BBBBHB", 7, 5, 0x01, 3, 32, 8))
        IFACE = (0xFF, 0x5D, 0x01)
        EPS = [(0x81, 3, 4, 32), (0x01, 3, 8, 32)]
        VIDPID = (0x045E, 0x028E)
        PRODUCT = "Fake Xbox 360 Controller"
        bt = (1 << 12) | (1 << 4) | (1 << 0)               # A, Start, up
        INPUT = struct.pack("<BBHBBhhhh6x", 0x00, 0x14, bt, 0, 255, 32767, 0, 0, 32767)
    else:
        DEV = struct.pack("<BBHBBBBHHHBBBB", 18, 1, 0x0200, 0xFF, 0x47, 0xD0, 64, 0x045E, 0x02EA, 0x0301, 1, 2, 3, 1)
        CFG_BODY = (struct.pack("<BBBBBBBBB", 9, 4, 0, 0, 2, 0xFF, 0x47, 0xD0, 0) +
                    struct.pack("<BBBBHB", 7, 5, 0x81, 3, 64, 4) + struct.pack("<BBBBHB", 7, 5, 0x01, 3, 64, 4))
        IFACE = (0xFF, 0x47, 0xD0)
        EPS = [(0x81, 3, 4, 64), (0x01, 3, 4, 64)]
        VIDPID = (0x045E, 0x02EA)
        PRODUCT = "Fake Xbox One Controller"
        b4 = (1 << 2) | (1 << 4)                           # Menu (Start), A
        b5 = 1 << 0                                        # up
        INPUT = struct.pack("<BBBBBBHHhhhh", 0x20, 0x00, 1, 0x0E, b4, b5, 0, 1023, 32767, 0, 0, 32767)
    DEVCLASS = (DEV[4], DEV[5], DEV[6])

CFG = struct.pack("<BBHBBBBB", 9, 2, 9 + len(CFG_BODY), 1, 1, 0, 0x80, 50) + CFG_BODY
STRINGS = {0: bytes([4, 3, 0x09, 0x04]), 1: strdesc("Banana Test"), 2: strdesc(PRODUCT), 3: strdesc("0001")}

srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", PORT))
srv.listen(1)
srv.settimeout(60)
conn, _ = srv.accept()
lock = threading.Lock()
started = threading.Event()
seen_out = []

def send(t, body, pid=0):
    with lock:
        conn.sendall((struct.pack("<IIQ", t, len(body), pid) if wide else struct.pack("<III", t, len(body), pid)) + body)

def log(*a):
    print(*a, flush=True)

wide = False
send(HELLO, b"fakepad".ljust(64, b"\0") + struct.pack("<I", CAPS))
def recvn(n):
    b = b""
    while len(b) < n:
        c = conn.recv(n - len(b))
        if not c:
            raise EOFError
        b += c
    return b
t, ln, _ = struct.unpack("<III", recvn(12))     # the guest's hello (before the capabilities are known)
log("qemu hello", recvn(ln)[:64].split(bytes(1))[0].decode(errors="replace"))
wide = True
ii = bytearray(4 + 32 * 4)
struct.pack_into("<I", ii, 0, 1)
ii[4 + 0] = 0
ii[4 + 32], ii[4 + 64], ii[4 + 96] = IFACE
send(IFACE_INFO, bytes(ii))
types, intervals, ifaces, mps = [255] * 32, [0] * 32, [0] * 32, [0] * 32
types[0] = types[16] = 0
mps[0] = mps[16] = DEV[7]
for ep, ty, iv, mp in EPS:
    i = ((ep & 0x80) >> 3) | (ep & 0x0F)
    types[i], intervals[i], mps[i] = ty, iv, mp
send(EP_INFO, bytes(types) + bytes(intervals) + bytes(ifaces) + struct.pack("<32H", *mps))
send(CONNECT, struct.pack("<BBBBHHH", 1, DEVCLASS[0], DEVCLASS[1], DEVCLASS[2], VIDPID[0], VIDPID[1], 0x0100))

def pump():
    started.wait()
    while True:
        time.sleep(0.02)
        try:
            send(INTERRUPT, struct.pack("<BBH", 0x81, 0, len(INPUT)) + INPUT)
        except OSError:
            return
threading.Thread(target=pump, daemon=True).start()

def recvn(n):
    b = b""
    while len(b) < n:
        c = conn.recv(n - len(b))
        if not c:
            raise EOFError
        b += c
    return b

config = 0
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
                    data = {1: DEV, 2: CFG, 3: STRINGS.get(i), 0x22: REPORT or None}.get(kind)
                    if data is None:
                        status, data = 4, b""
                elif req == 0:
                    data = b"\0\0"
                elif req == 8:
                    data = bytes([config])
                else:
                    status = 4
                data = data[:length]
            else:
                log("control out", hex(rtype), hex(req), hex(val), hex(idx), body[10:].hex())
            send(CONTROL, struct.pack("<BBBBHHH", ep, req, rtype, status, val, idx, len(data)) + data, pid)
        elif t == SET_CONFIG:
            config = body[0]
            send(CONFIG_STATUS, struct.pack("<BB", 0, config), pid)
        elif t == GET_CONFIG:
            send(CONFIG_STATUS, struct.pack("<BB", 0, config), pid)
        elif t in (SET_ALT, GET_ALT):
            send(ALT_STATUS, struct.pack("<BBB", 0, body[0], body[1] if t == SET_ALT else 0), pid)
        elif t == START_INT:
            send(INT_STATUS, struct.pack("<BB", 0, body[0]), pid)
            started.set()
        elif t == STOP_INT:
            send(INT_STATUS, struct.pack("<BB", 0, body[0]), pid)
        elif t == INTERRUPT:
            ep, st, length = struct.unpack("<BBH", body[:4])
            if not ep & 0x80:
                log("interrupt out", hex(ep), body[4:].hex())
                send(INTERRUPT, struct.pack("<BBH", ep, 0, length), pid)
        elif t == HELLO:
            log("qemu hello", body[:64].rstrip(b"\0").decode(errors="replace"))
except (EOFError, OSError):
    pass
