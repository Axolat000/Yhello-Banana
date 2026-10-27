# ASIX AX88179A/B vendor-mode reference

`net/ax88179a.c` adapts register initialization and packet framing from the
official ASIX driver:
https://github.com/ASIXElectronics/asix-usb-nic-linux-driver/tree/42feb1252fe0669845a8d6714bcc77bfc19eea1d

Pinned source SHA-256 values:

- `ax88179a_772d.c`:
  `cb6c48401d1a96cb2024f6c95ab59d8c66baefec5295c72ab35366457d44911a`
- `ax88179a_772d.h`:
  `dfa70fb68875ed4519c3a288280f6138750995eb75900114b713271c7919b62b`
- `ax_main.h`:
  `689a30e4c1cb62c8c7a14057b6e10874596baae9cc4ff46dbd723061bfbf3ea2`

Those files carry an ASIX copyright notice explicitly permitting GPL version 2
or any later version. The adaptation uses that later-version permission under
GPLv3. The complete permission notice is retained in NOTICES and installed in
the OS at `/usr/share/licenses/ax88179a/NOTICES`. Linux implementations and APIs
are not linked into Banana OS.

Exact matching: USB `0b95:1790`, `bcdDevice=0200`, MAC chip code 6 and a vendor
interface exposing bulk IN EP2 / OUT EP3. This covers the shared AX88179A/B
profile; it does not claim AX88772D/E, AX88279/A or unknown silicon. Firmware
mode selects one TX queue without PTP; existing flash/eFuse is reloaded but never
programmed. SuperSpeed, high-speed and full-speed descriptor sizes are checked.
An AX88179A offering a standard NCM/ECM configuration can still use the existing
class driver. The older AX88179 driver now explicitly matches revision 0100 so
the two same-VID/PID chips cannot be initialized using each other's protocol.

The native frontend powers the PHY, disables USB LPM/automatic detach, reads the
factory MAC, advertises 10/100/1000, polls negotiated link and programs link-
specific aggregation settings. The data path uses 64-bit vendor RX/TX headers,
8-byte payload alignment and TX header checksums. Ethernet checksums stay in
software; VLAN stripping, TSO, PTP, EEE tuning, suspend/resume and extra TX queues
are not implemented. No physical adapter was available.

`tools/test_network_drivers.py --sanitize` and `--32` cover actual driver
initialization and framing, USB speed variants, link negotiation, every TX
length, multi-packet RX, truncated/corrupt aggregates, unsupported silicon,
control/endpoint/submission errors and retained URB storage on failed startup.
Existing RTL8152/8153, AX88179 and CDC-NCM models remain in the same regression.
The USB startup helper publishes a device only after all RX submissions succeed;
a submitted context is retained on failure because controllers may still hold
its embedded transfer objects.
