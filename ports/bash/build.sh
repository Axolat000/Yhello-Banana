#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU bash 5.2 for Banana OS (64-bit: it needs fork), with its own readline:
#     ports/bash/build.sh <output.bpk>
# makes the package bash, whose program is also /bin/bash and /bin/sh
# (bin= in its manifest, kernel/pkg.c), with /apps/bash/bashrc (a prompt,
# colours). Built with ports/port.sh.
[ -n "$1" ] || { echo "usage: $0 <output.bpk>"; exit 1; }
OUT=$(realpath -m "$1")
. "$(dirname "$0")/../port.sh"

port_fetch bash 5.2.37 9599b22ecd1d5787ad7d3b7bf0c59f312b3396d1e281175dd1f8a4014da621ff \
    https://ftp.gnu.org/gnu/bash/bash-5.2.37.tar.gz

# GCC 14 rejects Bash's undeclared legacy bcopy calls in its glob matcher.
# memmove has the same overlap-safe behavior and is declared by the SDK.
python3 - "$SRC/lib/glob/sm_loop.c" "$SRC/lib/glob/smatch.c" "$SRC/lib/termcap/tparam.c" <<'PY'
from pathlib import Path
import sys
for name in sys.argv[1:]:
    p = Path(name)
    s = p.read_text()
    s = s.replace("bcopy (p + 1, ccname, (close - p - 1) * sizeof (CHAR))",
                  "memmove (ccname, p + 1, (close - p - 1) * sizeof (CHAR))")
    s = s.replace("bcopy (p, cc, p1 - p)", "memmove (cc, p, p1 - p)")
    if p.name == "tparam.c" and "#include <unistd.h>" not in s:
        s = "#include <unistd.h> /* write, for GCC 14 */\n" + s
    p.write_text(s)
PY

# what configure cannot find out by running a program (cross-compiling),
# as it is on Banana OS
bash_cv_getcwd_malloc=yes          # getcwd(NULL, 0) allocates (no lib/sh/getcwd.c)
bash_cv_dev_fd=standard            # /dev/fd/N: process substitution <(...)
bash_cv_dev_stdin=present
bash_cv_sys_named_pipes=missing
bash_cv_job_control_missing=present
bash_cv_wcontinued_broken=no
bash_cv_func_sigsetjmp=present
bash_cv_must_reinstall_sighandlers=no
bash_cv_sys_siglist=no
bash_cv_under_sys_siglist=no
bash_cv_opendir_not_robust=no
bash_cv_printf_a_format=yes
bash_cv_func_snprintf=yes
bash_cv_func_vsnprintf=yes
bash_cv_ulimit_maxfds=no
bash_cv_unusable_rtsigs=no
bash_cv_pgrp_pipe=no
bash_cv_wexitstatus_offset=8
bash_cv_dup2_broken=no
bash_cv_fnmatch_equiv_fallback=no
PORT_ENV=$(set | sed -n 's/^\(bash_cv_[a-z_0-9]*\)=.*/\1/p')

# every interactive bash reads /apps/bash/bashrc first (this package's)
grep -q '^#define SYS_BASHRC "/apps/bash/bashrc"' "$SRC/config-top.h" ||
    printf '\n/* Banana OS (ports/bash/build.sh) */\n#define SYS_BASHRC "/apps/bash/bashrc"\n' >> "$SRC/config-top.h"

# notify_of_job_status: SIGCHLD blocked while it prints the jobs, always -
# with SIGTTOU at its default when bash started (bash -i run by a program),
# it only counts a SIGCHLD that comes meanwhile and never takes it later:
# a job that stopped or ended right then stayed "Running" (signals come at
# a system call's return here: the write printing "[1] pid")
sed -i 's/^  if (old_ttou != 0)$/  if (1)  \/\* Banana OS: SIGCHLD always blocked here \*\//' "$SRC/jobs.c"

port_configure --without-bash-malloc --disable-nls --enable-job-control --with-installed-readline=no \
    --without-curses --prefix=/usr --bindir=/bin
# (bash's Makefile makes pathnames.h twice at once under -j: once first)
make pathnames.h >/dev/null
port_make bash

port_package "$OUT" name=bash "title=GNU Bash" version=$PORT_VER type=console \
    "description=The GNU Bourne Again shell - /bin/bash and /bin/sh" "author=Free Software Foundation" \
    "category=Developer Tools" bin=bash,sh -- bash "bashrc=$PORT_DIR/bashrc"
