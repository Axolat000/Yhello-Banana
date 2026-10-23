# PC name

Open Settings > About (or run settings about on the desktop), edit PC name
and click Apply or press Enter. Tab moves from the field to Apply;
Escape cancels an edit.

In a terminal:
    hostname
    hostname banana-laptop
    uname -n

Use 1-63 ASCII letters, digits or hyphens. The first and last characters must
be letters or digits. Uppercase letters are saved as lowercase. Invalid names
are rejected without changing the saved name; the CLI exits with status 1.

The shared setting is /etc/hostname, a single name followed by a newline.
It is applied immediately and saved with the filesystem. Installed systems
keep it across reboots. A live session keeps the name until reboot (or until
that session is installed).

New terminal shells use the new name; already-running shells may retain
their original prompt. The built-in shell prompt updates immediately.
DHCP sends the current name on its next request or lease renewal; renaming
does not interrupt the network connection.

SDK gethostname() and uname() read the same file through the existing file
API, including when used by bundled Bash and coreutils. No API version
change is required. Missing or invalid saved names fall back to banana-os.

Run the host tests with: sh tools/tests/hostname.sh
They exercise validation, persistence, failed writes, boot fallback, a
63-character DHCP option, and the SDK's hostname/utsname buffer handling
under AddressSanitizer and UBSan.

Validation (2026-10-23):
- Full hybrid image builds on the build machine with GCC 14.
- Kernel/DHCP and SDK host regression tests pass under ASan and UBSan.
- QEMU: CLI normalization, invalid/empty/quoted names, exit statuses and
  63/64-character boundaries pass; /bin/hostname belongs to kcmd.
- Settings > About was tested with keyboard navigation, editing and Apply;
  the resulting name matches hostname, /etc/hostname, uname -n and new Bash.
- A fresh installation preserves the GUI-set name across reboot; a later
  CLI rename persists across a second reboot. Banana OS fsck reports clean.
- The 32-bit fallback also changes the name and rejects invalid input.

The SDK rebuild exposed cold-build issues in Bash, Zsh and Neovim's libuv
dependency on GCC 14. Their port scripts include reproducible compatibility
fixes; no compiler diagnostics were disabled.
