# One graphical Terminal

The desktop and Start menu retain the original Terminal entry. It opens the
same tabbed Terminal emulator; Bash and Zsh are shell choices, not additional
desktop apps. The terminal package is titled Terminal.

Only type=gui packages appear in the Apps launcher and Start menu, including
All Programs and program search. The Start menu omits the terminal package
entry because it already has the built-in Terminal entry.

Console packages remain installed, callable from /bin, and visible in App
Store and Settings > Apps. The package inventory itself is not filtered.
Desktop refresh removes app shortcuts to installed console packages and
duplicate Terminal app shortcuts, retaining the original Terminal.link.
It does not remove packages or ordinary files. New GUI shortcut creation
also rejects console-package app targets.

Choose a shell with:
    chsh zsh
    chsh bash
    chsh sh

The choice is saved in /etc/settings.conf and read when each Terminal tab
starts. New windows and tabs use it; existing tabs keep their shell.
This setting controls the graphical Terminal, not already-running consoles.
Explicit terminal -e commands still run as requested.

Host regression tests:
    sh tools/tests/desktop_catalog.sh
These exercise the actual Start menu builder (normal entries, All Programs
and search) and Apps launcher scanner against a mixed package inventory.

Validation (2026-10-23):
- Full i386/x86_64 hybrid image builds.
- Host tests pass for the Apps launcher and all Start menu views.
- QEMU cleanup of existing Bash, Zsh, GNU and duplicate Terminal shortcuts
  preserves the original Terminal.link.
- A graphical Terminal starts Zsh after chsh zsh. Selecting Bash starts it
  in a new tab while the existing Zsh tab stays Zsh.
- GNU commands still execute; coreutils and shell package details remain
  available through package management.
- Installation and reboot preserve the shell choice and clean desktop;
  a newly opened Terminal after reboot runs Zsh.
- The Apps launcher was visually checked: seven graphical apps, one Terminal,
  no separate shells or GNU command packages.
