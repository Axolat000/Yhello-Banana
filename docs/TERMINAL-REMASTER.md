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
