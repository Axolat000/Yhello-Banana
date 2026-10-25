#!/usr/bin/env python3
"""Boots Banana OS in QEMU without a window and types commands at its
serial console (COM1 is a console of the shell), one after the other,
each once the shell's prompt is back. Everything the system writes is
kept in a log; a command whose output says the kernel panicked or the app
crashed counts as failed.

usage: serialtest.py [--qemu BIN] [--log FILE] [--timeout S] -- <qemu args...>
       (the commands: --cmd "..." repeated, --cmds-file FILE, or the default
       app tests). In a commands file: one command a line; "cmd ## text"
       also fails unless text is in its output; "#wait N" waits N seconds;
       other lines starting with # are comments.
"""
import argparse, re, socket, subprocess, sys, time

DEFAULT_CMDS = [
    "selftest ## 0 failed",                 # (shell/selftest.c: what the OS checks itself)
    "pkg install ~/Examples/threads.bpk",
    "threads 4 200000",
    "pkg install ~/Examples/cltest.bpk",
    "cltest",
    "free",
]

BAD = re.compile(r"PANIC|crashed|was stopped|segmentation fault|stack overflow|general protection", re.I)
STACK_ERR = re.compile(r"stack overflow", re.I)
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

def command_failure(output):
    # Successful signal tests name the fault they intentionally recovered
    # from. Actual crash messages and failing checks must still fail the run.
    diagnostics = "\n".join(l for l in output.splitlines() if not re.match(r"\s*ok\s", l))
    bad = BAD.search(diagnostics)
    if bad:
        return bad.group(0)
    failed = re.search(r"^\s*FAIL\s+.*|^selftest: \d+ passed, [1-9]\d* failed.*", diagnostics, re.M)
    return failed.group(0).strip() if failed else ""

def expected_output_present(output, expected):
    # A numeric expectation must not match the end of a different number:
    # "0% packet loss" is not satisfied by "100% packet loss".
    if expected[:1].isdigit():
        return re.search(r"(?<![0-9])" + re.escape(expected), output) is not None
    return expected in output

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qemu", default="qemu-system-x86_64")
    ap.add_argument("--log", default="serial-apps.log")
    ap.add_argument("--timeout", type=float, default=180, help="seconds per command")
    ap.add_argument("--boot-timeout", type=float, default=180)
    ap.add_argument("--cmd", action="append", help="a command to type (repeatable)")
    ap.add_argument("--cmds-file", help="commands, one a line (see above)")
    ap.add_argument("--window", action="store_true", help="show QEMU's window (to watch or debug); the commands still go over serial")
    ap.add_argument("--keep", action="store_true", help="leave QEMU running after the commands, until its window is closed")
    ap.add_argument("qemu_args", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    qargs = [x for x in a.qemu_args if x != "--"]
    cmds = a.cmd or DEFAULT_CMDS
    if a.cmds_file:
        cmds = [l.rstrip("\n") for l in open(a.cmds_file, encoding="utf-8")
                if l.strip() and (not l.startswith("#") or l.startswith("#wait"))]

    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    display = [] if a.window else ["-display", "none"]
    proc = subprocess.Popen([a.qemu, *qargs, *display, "-no-reboot",
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

    PROMPT_RE = re.compile(r'(\$\s*$|\[y/N\]\s*$|\[1-\d+, Enter cancels\]\s*$)')

    def wait_prompt(since, timeout):
        """the shell's prompt or question prompt, with nothing more coming: True"""
        end = time.time() + timeout
        quiet = None
        last_enter = 0.0
        while time.time() < end:
            got = pump()
            if got:
                quiet = None
                continue
            cur = text[since:]
            if PROMPT_RE.search(cur):
                quiet = quiet or time.time()
                if time.time() - quiet > 0.5:
                    return True
            elif "$ " in cur:
                # Prompt appeared but later asynchronous logs arrived; pulse Enter periodically
                now = time.time()
                if now - last_enter > 2.0:
                    sock.sendall(b"\r")
                    last_enter = now
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
        for line in cmds:
            if line.startswith("#wait"):
                end = time.time() + float(line.split()[1])
                while time.time() < end:
                    pump()
                continue
            c, _, expect = line.partition(" ## ")
            mark = len(text)
            print(f"$ {c}", flush=True)
            for b in c.encode("utf-8") + b"\r":
                sock.sendall(bytes([b]))
                time.sleep(0.01)
            ok = wait_prompt(mark + len(c), a.timeout)
            out = text[mark:]
            failure = command_failure(out)
            for l in out.splitlines()[1:-1]:
                print("    " + l)
            why = failure
            if ok and not failure and expect and not expected_output_present(out.encode("latin-1", "replace").decode("utf-8", "replace"), expect):
                why = f"expected {expect!r}"
            status = "FAIL" if why or not ok else "ok"
            if not ok:
                print("    (no prompt within %d s)" % a.timeout)
            results.append((c, status, why))
            if "PANIC" in failure.upper():
                break
    except EOFError:
        results.append(("(QEMU)", "FAIL", "the serial port closed - the VM stopped or reset"))
    finally:
        if a.keep and proc.poll() is None:
            print("\nQEMU stays open - close its window to go on", flush=True)
            try:
                while proc.poll() is None:
                    try:
                        pump()
                    except EOFError:
                        break
            except KeyboardInterrupt:
                pass
        proc.kill()
        log.close()

    print("\nresults:")
    for c, st, why in results:
        print(f"  {st:4}  {c}" + (f"   <- {why}" if why else ""))
    return 0 if results and all(st == "ok" for _, st, _ in results) else 1

if __name__ == "__main__":
    sys.exit(main())
