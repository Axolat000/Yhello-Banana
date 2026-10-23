# Completed descriptor synchronization (ABI 31)

`fd_sync` is appended after the ABI 30 tail; older offsets remain unchanged.
The SDK checks its presence and converts negative errno results into errno/-1.
`fdatasync` uses the same full-volume synchronization as `fsync`.

An open regular file or directory on the installed system volume flushes the
volume's journal and backing device. The call holds the open file description
while it may sleep, preserving its position and dup/fork sharing. Invalid
descriptors return EBADF; pipes, sockets and devices return EINVAL. Other
mounted filesystems return ENOSYS until they have a sync backend.
In a live session the root filesystem is memory-backed: file/directory sync
succeeds without disk I/O because its writes are already current in RAM.
This does not make live-session files survive shutdown. Treating this case
as ENOSYS made Nano, Vim and vi report save failures on the live CD.
Backup freeze returns EBUSY, including when the freeze starts during a wait
for another flush. A controller/filesystem failure returns EIO. The historical
deferred autosave behavior of fsdisk_sync is kept for its existing callers.

Host tests execute the actual entry, backend and SDK wrapper bodies with
descriptor/storage shims, including a freeze during contention and a close
during flush, plus RAM-root success without calling the disk backend.
Live editor save/quit tests cover the application regression. They do not
establish physical-controller power-loss behavior;
that depends on the disk driver honoring its flush and needs QEMU/hardware
power-cut qualification. No durability is promised for unsupported mounts.

The exact-ISO BIOS/UEFI checks in `tools/ci-boot.sh` also install the
`shelltest` example and optional Neovim, then run `shelltest -e`. These
exercise typing, saving and quitting Nano, Vim, vi and Neovim on the live
filesystem, including the unsupported-sync regression.
