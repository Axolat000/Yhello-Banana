#!/usr/bin/env python3
"""Boots Banana OS in QEMU without a window and types commands at its
serial console (COM1 is a console of the shell), one after the other,
each once the shell's prompt is back. Everything the system writes is
kept in a log; a command whose output says the kernel panicked or the app
crashed counts as failed.

usage: serialtest.py [--qemu BIN] [--log FILE] [--timeout S] -- <qemu args...>
       (the commands: --cmd "..." repeated, or the default app tests)
"""
import argparse, re, socket, subprocess, sys, time

DEFAULT_CMDS = [
    "pkg install ~/Examples/hello.bpk",
    "hello",
    "pkg install ~/Examples/threads.bpk",
    "threads 4 200000",
    "pkg install ~/Examples/cltest.bpk",
    "cltest",
    "pkg ps",
    "free",
]

BAD = re.compile(r"PANIC|crashed|was stopped|segmentation fault|stack overflow|general protection", re.I)
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qemu", default="qemu-system-x86_64")
    ap.add_argument("--log", default="serial-apps.log")
    ap.add_argument("--timeout", type=float, default=180, help="seconds per command")
    ap.add_argument("--boot-timeout", type=float, default=180)
    ap.add_argument("--cmd", action="append", help="a command to type (repeatable)")
    ap.add_argument("qemu_args", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    qargs = [x for x in a.qemu_args if x != "--"]
    cmds = a.cmd or DEFAULT_CMDS

    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    proc = subprocess.Popen([a.qemu, *qargs, "-display", "none", "-no-reboot",
                             "-chardev", f"socket,id=s0,host=127.0.0.1,port={port},server=on,wait=on",
                             "-serial", "chardev:s0"])
    sock = None
    for _ in range(100):
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=1)
            break
        except OSError:
            if proc.poll() is not None:
                print("QEMU did not start")
                return 2
            time.sleep(0.1)
    if not sock:
        print("could not connect to QEMU's serial port")
        proc.kill()
        return 2
    sock.settimeout(0.1)
    log = open(a.log, "w", encoding="utf-8", errors="replace")
    text = ""                                   # everything, ANSI colours taken out

    def pump():
        nonlocal text
        try:
            data = sock.recv(65536)
        except socket.timeout:
            return False
        if not data:
            raise EOFError
        t = data.decode("latin-1")
        log.write(t)
        log.flush()
        text += ANSI.sub("", t).replace("\r", "")
        return True

    def wait_prompt(since, timeout):
        """the shell's prompt ("...$ " at the end, nothing more coming): True"""
        end = time.time() + timeout
        quiet = None
        while time.time() < end:
            got = pump()
            if got:
                quiet = None
                continue
            if text[since:].rstrip(" ").endswith("$") and text.endswith("$ "):
                quiet = quiet or time.time()
                if time.time() - quiet > 0.7:
                    return True
            if proc.poll() is not None:
                return False
        return False

    results = []
    try:
        print("booting...", flush=True)
        if not wait_prompt(0, a.boot_timeout):
            print("no shell prompt on the serial console (log: %s)" % a.log)
            print(text[-1500:])
            return 1
        for c in cmds:
            mark = len(text)
            print(f"$ {c}", flush=True)
            for ch in c + "\r":
                sock.sendall(ch.encode())
                time.sleep(0.01)
            ok = wait_prompt(mark + len(c), a.timeout)
            out = text[mark:]
            bad = BAD.search(out)
            for line in out.splitlines()[1:-1]:
                print("    " + line)
            status = "FAIL" if bad or not ok else "ok"
            if not ok:
                print("    (no prompt within %d s)" % a.timeout)
            results.append((c, status, bad.group(0) if bad else ""))
            if bad and "PANIC" in bad.group(0).upper():
                break
    except EOFError:
        results.append(("(QEMU)", "FAIL", "the serial port closed - the VM stopped or reset"))
    finally:
        proc.kill()
        log.close()

    print("\nresults:")
    for c, st, why in results:
        print(f"  {st:4}  {c}" + (f"   <- {why}" if why else ""))
    return 0 if results and all(st == "ok" for _, st, _ in results) else 1

if __name__ == "__main__":
    sys.exit(main())
