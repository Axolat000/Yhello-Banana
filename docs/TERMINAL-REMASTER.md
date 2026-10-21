# Terminal remaster: Unix processes, ttys, bash and zsh

Goal: replace the built-in kernel shell and the 4-window GUI terminal with
the real thing - Unix processes (fork / exec / wait, process groups, job
control), kernel pipes and pseudo-terminals with termios, a proper
xterm-compatible terminal emulator with as many windows / tabs as memory
allows, GNU bash (default) and zsh, and the GNU tools (coreutils, grep, sed,
...). The old shell (`shell/`) goes away; everything it did becomes programs
in `/bin` that bash runs.

Decisions (2026-10-21): real fork with copy-on-write; remove the kernel shell
entirely and move every feature to the new shell; GNU coreutils + grep / sed
/ etc; bash is the default shell, zsh installed too. 64-bit kernel only (the
32-bit kernel runs apps in ring 0, without address spaces: it keeps what it
has).

## Where things stand (build 236)

- Apps (`kernel/app.c`): PIE ELF files, each in its own ring-3 address space
  (`kernel/uspace.c`). Code, data and the main stack are in `umem` blocks -
  2 MiB pieces of the identity map, the same address in every space; the
  malloc heap is in the app's mmap region (`kernel/uvm.c`, `UVM_BASE`,
  4 KiB pages, demand-paged, compressed / swapped).
- File descriptors: per app (`afd_t fds[APP_FD_MAX]`), no shared open file
  descriptions, 32-bit positions; fd 0 is a line read from the kernel
  shell's terminal, 1 / 2 its screen. Pipes, socketpairs, eventfd and epoll
  are made inside the SDK's C library (`sdk/lib/ufd.c`) - one process only.
- `fork`, `execve`, `waitpid` are ENOSYS stubs (`sdk/lib/unix.c`);
  `posix_spawn` too. termios is a stub.
- `kernel/tty.c`: only SSH sessions (2), feeding the kernel shell.
- GUI terminal: `kernel/gui.c` `term_win_t`, `TERM_WIN_MAX 4`, each a kernel
  task running `shell/shell.c` on a vt (`kernel/terminal.c`).
- `shell/`: ~10 700 lines - the REPL, its parser, and every command
  (apt, net, disk, wp, theme, selftest, ...).

## Design

### Processes (kernel/proc.c, new)

A process: pid, parent, process group, session, controlling tty, exit status
(a zombie until waited for), its app (`app_proc_t`), its file descriptor
table, cwd, umask, environment handed to exec. `APP_MAX` is replaced by
dynamic slots.

Memory, for fork: everything a process owns that its program can point to
must be private to it, so all of it moves into the mmap region (4 KiB pages,
per-process tables): the ELF image (loaded at a fixed address there), the
main and thread stacks, argv / envp / auxv. `umem` keeps only what the
kernel hands out (window pictures, the call table, http bodies) and is
shared, reference-counted, between a process and its forked children.

fork: a new address space whose mmap region copies the parent's ranges;
every present page becomes read-only in both and gets a reference count
(`kernel/upage.c`); a write fault on a shared page copies it (or takes it
back if it is the last user). Compressed / swapped pages are brought in
first (later: shared zswap entries). Only the calling thread is copied
(POSIX). The child returns 0 from the same system call, on a copy of the
caller's kernel-side registers.

exec: the address space is emptied and the new program loaded; fds without
close-on-exec, cwd, pid, process group and ignored signals are kept;
handlers reset.

wait: `waitpid` / `wait4` with WNOHANG / WUNTRACED / WCONTINUED, SIGCHLD,
orphans go to pid 1 (`init`, a kernel task that reaps them).

### File descriptors (kernel/kfile.c, new)

Open file descriptions, reference-counted, shared by dup and fork:
regular files (64-bit positions), sockets, pipes / FIFOs (kernel, 64 KiB),
socketpairs, pty masters and slaves, the console, eventfd. A per-process
table of up to 1024 descriptors with close-on-exec flags. The SDK's
read / write / close / poll / dup / fcntl go straight to it; `ufd.c` keeps
only epoll (over the kernel's poll).

### Terminals (kernel/pty.c, new)

`/dev/ptmx` + `/dev/pts/N` (unlimited), with a real line discipline:
termios (canonical / raw, ECHO, ISIG, ICRNL, OPOST/ONLCR, VMIN/VTIME, the
control characters), window size (TIOCGWINSZ / TIOCSWINSZ + SIGWINCH),
foreground process group (tcsetpgrp) and the signals it sends (SIGINT,
SIGQUIT, SIGTSTP), SIGHUP on hang-up, background-read SIGTTIN. The text
consoles (Ctrl+Alt+F1..) and the serial port become ttys on the same line
discipline; SSH sessions get a pty each.

### Terminal emulator (apps/terminal, new)

A desktop app: a pty per tab, any number of windows and tabs; xterm-256color
/ VT220 escapes (colours incl. 24-bit, cursor movement, scrolling regions,
alternate screen, bracketed paste, mouse reporting, OSC titles), UTF-8 with
double-width CJK / emoji, scrollback, selection, copy / paste, font size,
themes. Ships a terminfo entry.

### Shells and tools (ports/)

GNU bash 5.2 (default, `/bin/sh` too), zsh 5.9, GNU coreutils, grep, sed,
gawk, findutils, diffutils, less, ncurses (+ terminfo), readline. `/etc/passwd`
gets the user's shell; `chsh` and Settings pick it.

### The old shell's commands

Each becomes a program in `/bin` (apt, net, ping, disk, theme, wp, ...).
Commands that need kernel internals call them through a system call that
runs the command's kernel code with the process's stdout / stderr - so the
code moves out of `shell/` into the kernel pieces it belongs to, without
the REPL, parser, history, line editor or scripting. The serial console
and the recovery boot entry run bash; `selftest` / `posixtest` stay as
programs (the test harness types into the serial console's bash).

## Phases (a build each, at least; pushed when tested)

1. Kernel open file descriptions and kernel pipes; SDK switched to them.
2. Processes: pids / groups / sessions, the ELF image and stacks in the mmap
   region, copy-on-write fork, execve, waitpid, SIGCHLD, `posix_spawn`.
3. ptys + termios + job control signals; ttys for the consoles and serial.
4. The terminal emulator app (unlimited windows and tabs), replacing
   `term_win_t`.
5. Ports: ncurses, readline, bash, coreutils, grep, sed, gawk, findutils,
   diffutils, less, zsh.
6. The old shell's commands as `/bin` programs; bash as the console, serial
   and SSH shell; `shell/` removed; test harness moved to bash.

## Status (2026-10-21) and how to continue

Done (builds 237-239 on `main`, 240-241 on its branch):

| Build | What | Where |
|---|---|---|
| 237 | Kernel open file descriptions, `dup`/`dup2`/`fcntl`, close-on-exec, kernel pipes and socket pairs (API 25) | `kernel/kfile.c`, `kernel/app.c` (`fdt[]`, `HOLD`), `sdk/lib/ufd.c`, `sdk/lib/posix.c` |
| 238 | Programs wholly in their mmap region; copy-on-write `fork`, `execve` (+ `#!`), `waitpid`, groups, sessions, `kill`, `posix_spawn`, `system`/`popen`, `environ`; per-task cwd; CR0.WP (API 26) | `kernel/uspace.c` (`uvm_fork`, `P_COW`, `uvm_write`), `kernel/upage.c` (refcounts), `kernel/app.c` ("Unix processes" section, `load_program`, `run()` loop), `kernel/appcall64.asm` (`app_resume_user`), `sdk/lib/proc.c` |
| 239 | Ptys + line discipline, termios, controlling terminal, job control (stop/continue, SIGTTIN/TTOU, SIGHUP), `/dev/*` (API 27) | `kernel/pty.c`, `kernel/app.c` ("terminals" section: `dev_open`, `pts_read`, `a_tty_ioctl`, `proc_stop`), `sdk/lib/tty.c` |
| 240 | The cross toolchain (`banana-cc`, `banana-configure`, `banana-config.site`); GNU bash 5.2.37 as `/bin/bash` and `/bin/sh` (built in, 64-bit); `bin=` in package manifests; `poll` gives `EINTR`; `main` gets `envp`; SDK: `<wctype.h>`, `wcwidth`, `pselect`, `mktemp`, `set*id`, `getgrent`, `getcwd(NULL, 0)`, `_POSIX_VERSION` | `sdk/tools/banana-cc`, `ports/bash/build.sh`, `kernel/pkg.c` (`each_bin`), `kernel/builtin_apps.c`, `kernel/app.c` (`a_poll`), `sdk/lib/utf8.c` |
| 241 | The Terminal app: windows with tabs, a pty + `/bin/bash` each, xterm-256color (colours up to 24-bit, scrolling regions, alternate screen, mouse reporting, bracketed paste, OSC titles, DEC line drawing), UTF-8 with double-width characters, 5000-line scrollback, selection / copy / paste, resizing (`TIOCSWINSZ`), text size; built in (64-bit). Desktop: PgUp / PgDn and a lone Esc reach app windows, Ctrl+Shift+T goes to them | `apps/terminal/vt.c` (the screen), `apps/terminal/main.c` (windows, drawing, input), `tools/tests/terminal_vt` (vt.c on Linux), `kernel/appwin.c`, `kernel/gui.c` |

Tests: `selftest pipes` (kernel pipes + pty line discipline), `posixtest proc`,
`posixtest tty` and `posixtest bash` (scripts, pipelines, `#!`, and `bash -i`
on a pty: editing, history, ^C, job control - all run by `selftest posix`). Known limits: `execve` only
from a process's main thread; job control's "orphaned process group" rule is
not applied; `fchdir` / directory descriptors do not exist yet; the SDK's
stdio reads files a byte per system call (slow, correct).

Terminal, as of build 241 (`apps/terminal`, "Terminal (bash)" in Apps; the
desktop's own Terminal - the kernel shell's `term_win_t` - stays until phase
6, as bash cannot run the kernel shell's commands yet): Ctrl+Shift+T a tab,
Ctrl+Shift+N a window, Ctrl+Shift+W closes a tab, Ctrl+PgUp / PgDn between
tabs, Shift+PgUp / PgDn the scrollback, Ctrl+Shift+C / V (or select, then
right-click) copy and paste, Ctrl+= / - / 0 the text size; `terminal -e cmd`.
`TERM=xterm-256color` with the termcap entry in `$TERMCAP` (readline uses it);
no terminfo database yet (comes with ncurses, step 4). Not yet: CJK glyphs
(the system has no CJK font: double-width boxes), combining marks (dropped),
the middle mouse button (the desktop does not pass it to apps), reflowing
wrapped lines on resize. Tested in QEMU: `tools/tests/terminal_vt/run.sh`
(28 checks of vt.c), and screenshots of bash in it: colours, tabs, the
alternate screen, scrollback, resize + SIGWINCH, selection and paste.

Bash, as of build 240: runs from `/bin` (the kernel shell finds it there:
`bash -c '...'`, `bash script.sh`, `sh -c '...'`); interactive on a pty. Not yet: bash on
the text / serial consoles (they are not ttys - phase 6: `bash -i` there
reads empty lines), a termcap / terminfo entry (readline works without one,
as on a dumb terminal plus the arrow keys: no clear-screen, no
`tput`), the GNU tools a script expects (`ls`, `cat`, `tr`... - step 4).
Testing it from the kernel shell: it expands `$name` itself, even between
single quotes - write `${name}`, or put the script in a file.

### Next: phase 4 + 5 (in this order)

Steps 1 and 2 are done (build 240), step 3 (build 241); the cross toolchain's use is in
`sdk/README.md` ("Porting Unix software"), `ports/bash/build.sh` is the
example port (configure's answers for what it would run, `bin=` packaging).

1. ~~**A cross toolchain for autoconf ports** - `sdk/tools/banana-cc`: a gcc
   wrapper (the flags of `ports/ffmpeg/build.sh`: `-ffreestanding -fpie
   -nostdinc -I sdk/include -D__BANANA_OS__`, linking `crt0` + the SDK libc
   + libgcc into a PIE the kernel's loader takes - see `sdk/banana.mk`'s
   link rule). Configure with `--host=x86_64-pc-linux-gnu
   --build=x86_64-build-linux-gnu` (forces cross mode) and pre-set
   `ac_cv_*` for anything configure would run.~~ (done)
2. ~~**Port GNU bash 5.2** (`ports/bash/build.sh`, download + sha256 like the
   other ports): `--without-bash-malloc --disable-nls --enable-job-control`,
   its bundled readline. Expect gaps in the SDK libc to fill (getpwnam,
   sigsetjmp, locale, `fnmatch`, `strsignal`...). Package it so it lands as
   `/bin/bash` and `/bin/sh`: `kernel/pkg.c` installs into `/apps/<name>/`
   only - add a manifest key (e.g. `bin=bash,sh`) that also puts the program
   at `/bin/<name>`.~~ (done)
3. ~~**The terminal emulator app** (`apps/terminal`, a desktop app built with
   the SDK): `forkpty()` + `execve("/bin/bash")`, any number of windows and
   tabs, xterm-256color escapes, UTF-8 / wide characters, scrollback,
   selection, `TIOCSWINSZ` on resize.~~ (done, build 241). Left for later:
   replacing `term_win_t` / `TERM_WIN_MAX` in `kernel/gui.c` (with phase 6,
   once bash can do what the kernel shell does); a terminfo entry (with
   ncurses).
4. **GNU coreutils, grep, sed, gawk, findutils, diffutils, less, ncurses,
   zsh** as ports into `/bin`.
5. **Phase 6**: the kernel shell's commands (`shell/*.c`) become `/bin`
   programs (a system call runs a command's kernel code with the process's
   stdout), bash on the consoles, serial line and SSH (a pty each), then
   `shell/` goes. The test harness (`~/bos-tests/*.txt` on the dev VM, and
   CI) types into the serial console's shell - move it to bash last.

Testing: CI (`.github/workflows/build-iso.yml`) builds the ISO; the QEMU
checks run on the dev VM (`tools/serialtest.py`-style: `selftest`,
`selftest posix`). A session without the dev VM can build and push to this
branch; run the tests where QEMU is available before merging to `main`.
