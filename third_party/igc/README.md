# Intel I225/I226 HAL provenance

The igc_*.c/h files are the Intel/Netgate BSD-3-Clause shared hardware code
from OpenBSD src/sys/dev/pci, downloaded 2026-10-26. Each file retains its
OpenBSD revision and original copyright. Sources:
https://github.com/openbsd/src/tree/master/sys/dev/pci

Banana OS changes:
- Replace BSD includes and bus-space/delay/debug macros with igc_osdep.h.
- Limit device selection to the two native frontend IDs: I225-V 8086:15f3,
  I226-V 8086:125c. The existing e1000/igb driver is separate.
- Mark intentionally unused arguments and cast PHY ID before shifting.
- Bound semaphore release, latch unrecoverable failures in native_fault.
- Fail missing NVM reads instead of returning uninitialized data as success.
- Release GPY PHY locks on MDIC errors; preserve link-check errors.

net/igc.c supplies native PCI enumeration, per-card DMA queues and polling.
Queue setup follows OpenBSD if_igc.c revision 1.32 (BSD-2-Clause).
NOTICES contains redistribution notices for both sources and is installed
inside the OS at /usr/share/licenses/igc/NOTICES.

The HAL contains upstream NVM programming routines, but the native frontend
never calls NVM write/update routines. Host models assert that no EEWR/SRWR
register write occurs during initialization, traffic, or failure handling.
No Linux/BSD kernel or host network stack is linked into Banana OS.
