# RTL8125 native hardware layer

OpenBSD source snapshots:
- if_rge.c revision 1.47, 2026-10-13 (Kevin Lo, ISC).
- if_rgereg.h revision 1.19, 2026-10-13 (Kevin Lo, ISC).
- dev/microcode/rge/rgemcode.h revision 1.1, 2026-10-13 (Realtek firmware).
Source: https://github.com/openbsd/src/tree/master/sys/dev

The Realtek firmware header is unmodified:
SHA256 8ef508a048f687f6924feddcb839d6af7638c8a8840aa20103a71e2a48d7c1a5
Its redistribution notice is retained both here and inside the installed OS.
Firmware is not released under the ISC licence of the register code.

rge_hw.c extracts the hardware-only functions, removes BSD network/bus-space
dependencies and uses rge_native.h to access native MMIO/delays. A native
single-queue setup adapts rge_init with MTU 1500, polling, software checksums
and no VLAN stripping. PCI extended configuration uses the controller's CSI
access path. Timeout failures are sticky; MAC packing/alignment are corrected.

The upstream hardware layer also contains RTL8126/8127 routines/data, but the
native frontend does not bind or claim these controllers. Only RTL8125 PCI
10ec:8125 with HW revisions 60900000, 64100000, 68800000 and 68900000 is enabled.
The existing RTL8111/8168 and RTL8152 paths are separate and unchanged.

Native glue, DMA rings and per-device enumeration are in net/r8125.c.
No Linux/BSD kernel network stack or mbuf structures are linked.
