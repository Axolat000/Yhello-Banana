# Integrated graphical Terminal

There is one graphical terminal implementation: the newer tabbed user-space
terminal in apps/terminal, using Bash by default. Desktop and Start retain
one Terminal entry with the original terminal icon. Its taskbar windows use
that same icon. Opening Terminal, Win+R, and opening a console command from
Files all use this implementation. The old kernel GUI terminal factory and
its fallback have been removed; a startup failure is reported instead.

Terminal, Bash and Zsh are integrated OS components. They have internal
packages for delivery and dependencies, but do not appear as applications in
Apps, App Store or Settings > Apps. They cannot be uninstalled as ordinary
apps and are restored at boot if missing. Other CLI tools remain in App Store
and Settings > Apps, while desktop, Start and Apps show graphical apps only.

Desktop refresh removes obsolete Bash/Zsh/ZShell app shortcuts, console
package shortcuts and duplicate Terminal shortcuts. It preserves ordinary
files and the canonical Terminal shortcut. This cleanup also runs on upgrades.

Choose the shell for new windows and tabs with chsh bash, chsh zsh or chsh sh.
The saved choice is retained; existing tabs keep their shell. Explicit
terminal -e commands still run as requested. Zsh has no separate app launcher.

The graphical Bash terminal currently requires 64-bit Banana OS. On 32-bit
systems, opening Terminal reports this requirement; the old GUI terminal is
not substituted. The serial and text consoles remain available.

Run sh tools/tests/desktop_catalog.sh for the real Start/All Programs/search
and Apps visibility policy tests.

Validation on QEMU:
- Win+R opens the tabbed terminal with Bash; after chsh zsh a new window
  uses Zsh while the existing Bash window remains active.
- The Terminal windows use the original terminal icon in the taskbar.
- Desktop cleanup leaves one Terminal shortcut and removes obsolete
  Bash, Zsh, ZShell and duplicate terminal shortcuts.
- Apps shows six GUI applications and no terminal/shell entries; Settings
  retains other command packages while excluding the integrated components.
- The boot selftests pass (37 checks).
