# Experimental RTL8811CU / RTL8821CU native station

The native implementation is `net/rtl8811cu.c`. Hardware register definitions,
programming tables and selected MAC/firmware/channel routines are adapted from
Linux v6.12 `drivers/net/wireless/realtek/rtw88` under its BSD-3-Clause option:
https://github.com/torvalds/linux/tree/v6.12/drivers/net/wireless/realtek/rtw88
There is no Linux kernel, mac80211, workqueue or USB ABI dependency.
Original-file hashes are in `UPSTREAM-SHA256SUMS`.

The unmodified 139472-byte firmware is pinned to linux-firmware tag `20250211`:
https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/refs/tags/20250211/rtw88/rtw8821c_fw.bin
SHA-256: `2ef409bc418549fcf294061dd0cae1fc22fd9da79b60524950b25de18732f3f0`.
It is embedded in both kernel builds. BSD and separate binary-firmware licence
terms are retained in `NOTICES` and installed at
`/usr/share/licenses/rtl8811cu/NOTICES`. The firmware is not disassembled.

## Exact initial profile and limitations

USB `0bda:c811` or `0bda:8811`; production cut C/D, one RF path, RFE 0/6,
internal 2.4 GHz PA/LNA and no shared Bluetooth antenna/coexistence requirement.
The factory EFUSE ID, unicast MAC, crystal setting and calibration bases must
be valid. Other chips, test silicon, RFE variants, external PA/LNA and unknown
board data decline before interface registration.

The initial transport supports high-speed USB with exactly one bulk-IN and
two or three bulk-OUT endpoints on vendor interface 0, alternate setting 0.
Every bulk endpoint must have a 512-byte packet size. Full/super-speed and
composite interface profiles are deliberately outside this initial scope.

The shared station provides passive scan on channels 1-11 and open or
WPA2-PSK/CCMP networks. Hardware crypto and sequence assignment are disabled.
**This initial profile uses the lowest AP-supported legacy rate and the lowest
hardware TX gain index** (normally 1 Mbit/s, or 6 Mbit/s for an OFDM-only AP). It has no throughput claim or assumed gain-index-to-dBm conversion.
There is no 5 GHz, HT/VHT, aggregation, WPA3, automatic rate/power tracking or
suspend/resume support. A functioning model is not physical RF certification;
no physical RTL8811CU was available.

## Native initialization and ownership

The driver runs the chip power sequence, downloads DMEM/IMEM/EMEM through the
reserved TX page and internal DDMA engine, checks hardware checksums and waits
for the complete firmware-ready value. Firmware shape, section lengths and
destination ranges are validated with wide size arithmetic before transfer.
USB-boundary padding copies into owned memory instead of reading beyond the
last firmware byte.

Physical EFUSE is read only, with bounded polls. Sparse one/two-byte headers
are decoded transactionally within physical/protected and logical bounds.
Fixed programming tables select cut/package/interface/RFE branches. Channel
setup and firmware IQ calibration propagate failures and time out. PHY tables
cannot increase the initial lowest TX gain.

Packet descriptors preserve native sequence numbers and CCMP bytes. Exact USB
packet boundaries use separately owned zero-length transfers. Eight TX slots
return backpressure while owned by the controller; a two-second stall clears
the station keys/carrier. The entire RX aggregate is checked before delivering
any frame, including lengths, status, padding and C2H-versus-frame identity.

Initialization failures publish no network interface. Submitted USB objects
and DMA buffers remain resident because usbcore has no cancellation/free API.
Late completions after timeout or detach cannot target freed contexts.
At most 32 queued contexts are retained over the boot lifetime; exhaustion or
a fatal radio error requires reboot. Existing RTL8188EU and RTL8152 paths are
separate.

## Validation

Run `python3 tools/test_rtl8811cu.py --sanitize` and `--32`.
The actual HAL, USB frontend and WPA2 station execute against register, DDMA,
EFUSE and USB-controller models with independent AP/crypto vectors. Tests cover:

- All three firmware sections and byte-for-byte destination contents, DDMA
  checksum/stall/missing-ready failures, and every firmware-stage control error.
- Every TX frame length through `WIFI_FRAME_MAX`, exact USB packet boundaries,
  queued-buffer ownership, full queues and stalls.
- Scan/authentication/association, M1-M4, CCMP unicast/group packets and replay
  rejection.
- Submission, allocation and endpoint failures, sampled controls throughout
  initialization, malformed silicon/board profiles and late completions.
- 20000 EFUSE/header mutations and 20000 RX aggregate mutations.

These models validate software control/data paths and bounds. Host/VM tests
cannot establish that a physical adapter associates, its RF output is legal,
or its sensitivity/throughput is adequate. Hardware validation remains pending.
