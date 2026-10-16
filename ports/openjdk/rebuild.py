#!/usr/bin/env python3
"""rebuild.py REFOBJS OUT [names...] - compile a native part of OpenJDK again
for Banana OS.

REFOBJS: a folder of the reference Linux build holding *.o.cmdline files
(hotspot/variant-zero/libjvm/objs, support/native/java.base/libjava...).
The command lines are replayed with the Banana OS SDK's headers in place
of the system's (C++: the compiler's libstdc++ headers, no library); objects
go to OUT/objs, errors to OUT/logs/<name>.log and a summary is printed.
EXTRA (environment): more compiler flags, after the original ones."""
import collections, glob, os, re, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
REF, OUT = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
NAMES = sys.argv[3:]
SDK = os.path.normpath(HERE + "/../../sdk")
GCC = subprocess.run(["gcc", "-dumpversion"], capture_output=True, text=True).stdout.strip().split(".")[0]
GCCINC = subprocess.run(["gcc", "-print-file-name=include"], capture_output=True, text=True).stdout.strip()
PRE = ["-nostdinc", "-I" + SDK + "/include", "-I" + HERE + "/include",
       "-isystem", GCCINC, "-D__BANANA_OS__", "-mno-red-zone", "-fno-stack-protector"]
PRE_CXX = ["-nostdinc++", "-isystem", "/usr/include/c++/" + GCC, "-isystem", "/usr/include/x86_64-linux-gnu/c++/" + GCC]
EXTRA = os.environ.get("EXTRA", "").split()
JOBS = int(os.environ.get("JOBS", os.cpu_count() or 2))
os.makedirs(OUT + "/objs", exist_ok=True)
os.makedirs(OUT + "/logs", exist_ok=True)


def rewrite(cmd, name):
    a = shlex.split(cmd)
    cxx = a[0].endswith("++")
    pre = PRE[:3] + (PRE_CXX if cxx else []) + PRE[3:]
    out, i = [a[0]] + pre, 1
    while i < len(a):
        x = a[i]
        if x in ("-MMD", "-pipe") or x.startswith("-fstack-protector"):
            i += 1
            continue
        if x == "-MF":
            i += 2
            continue
        if x.startswith("-I") and x.endswith("/objs/precompiled"):
            i += 1
            continue
        if x == "-o":
            out += ["-o", OUT + "/objs/" + name + ".o"]
            i += 2
            continue
        out.append(x)
        i += 1
    return out + EXTRA


def build(path):
    name = os.path.basename(path)[:-len(".o.cmdline")]
    r = subprocess.run(rewrite(open(path).read().strip(), name), capture_output=True, text=True)
    log = OUT + "/logs/" + name + ".log"
    if r.returncode == 0:
        if os.path.exists(log):
            os.remove(log)
        return name, True, ""
    open(log, "w").write(r.stderr)
    return name, False, r.stderr


paths = sorted(glob.glob(REF + "/*.o.cmdline"))
if NAMES:
    paths = [p for p in paths if os.path.basename(p)[:-len(".o.cmdline")] in NAMES]
with ThreadPoolExecutor(JOBS) as ex:
    res = list(ex.map(build, paths))
bad = [r for r in res if not r[1]]
print("ok %d, failed %d" % (len(res) - len(bad), len(bad)))
if bad:
    missing, errs = collections.Counter(), collections.Counter()
    for _, _, e in bad:
        for m in re.findall(r"fatal error: ([^:]+): No such file", e):
            missing[m] += 1
        for m in re.findall(r"error: (.*)", e):
            errs[re.sub(r"[‘’'\"][^‘’'\"]*[‘’'\"]", "X", m)[:90]] += 1
    print("missing headers: " + ", ".join("%s(%d)" % kv for kv in missing.most_common(60)))
    print("errors:")
    for m, n in errs.most_common(40):
        print("  %4d %s" % (n, m))
    print("failed: " + " ".join(r[0] for r in bad[:80]))
    sys.exit(1)
