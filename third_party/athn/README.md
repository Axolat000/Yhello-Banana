# AR9280 hardware routines and native frontend

The hardware-only routines/structures are adapted from OpenBSD sys/dev/ic at
commit `d2dd70ac0a5fe22ca102dd594dce32a1fe581c76`. Original ISC notices are
retained in headers, the HAL, the native compatibility structures and NOTICES,
installed at `/usr/share/licenses/ar9280/NOTICES`. There is no BSD networking,
mbuf, kernel, interrupt or DMA API dependency. The native frontend is
`net/ar9280.c`; no downloadable radio firmware is required.

Pinned original files and SHA-256:

- athn.c: f300bba6a00f83f7343db71ef71a0ae405a475597b9c41dddbf3112c8153fbd9
- athnreg.h: 67e4cdb6959dbc8337ba379f9fe6b103ae5e42473a81576f87bbc097723f25a9
- athnvar.h: a92c0a85037a68d024676f4e5466d9652306754207ea4f8d609741556a92fce3
- ar5008.c: bc30996acc149743a935d85b3b36120c47bd260976fcb863bdff2d8cebda9b4a
- ar5008reg.h: 33d3e176f8e74d7ebeafdbe29ccffb8109abe4ec0663268b55ba965e88dae77d
- ar5416.c: 6ee803608bc88ace95f69d3322a582813d15fc615c535f1d249d57de2a689fbe
- ar5416reg.h: 85e52b819b4268ef72eab75432694dd3cfa2b1f7ad20773120cb14440acb3e84
- ar9280.c: 3ecaf8e117bca4c1134214d966720b16f85fb64a1517baa1b2eb1859c2bb953a
- ar9280reg.h: 3e98cacbeb5631f23677e58ac4b462eb7bb4d1b338ee3e201f8d42dad82136fd

## Exact initial scope

PCI `168c:002a`, PCIe MAC revision AR9280 2.0 or 2.1, validated native EEPROM
layout/checksum, unicast factory MAC, calibrated contiguous chain masks (1/3) and usable
2.4 GHz tables. Unknown MAC revisions, other AR928x/AR922x IDs, malformed
calibration, unsupported radio switches or reset failures publish no interface.
AR9380 is a separate implementation and is **not** supported by this driver.

The shared native station scans passively on channels 1-11 and implements open
or WPA2-PSK/CCMP networks using software crypto. This profile uses legacy b/g
rates, a 20 MHz channel and no HT/5 GHz. Calibration still uses board EEPROM
targets; an additional initial power ceiling of 10 dBm before antenna/chain
reduction is applied before the signed calibration offset. This is a software
setting, not a measured RF output guarantee.

RX uses 64 linked descriptors and fixed private buffers; fragmented DMA frames,
invalid lengths/status and inconsistent 802.11 header padding are discarded.
The MAC consumes control frames; software receives FCS-stripped frames.
TX keeps a bounded 16-packet software queue and permits one outstanding hardware packet
to avoid linked-tail ownership races. It applies legacy rate fallback and
bounded retries. Full queues return backpressure; this initial path does not
claim high throughput. A stalled descriptor/queue stops PCI bus mastering and
clears the station's keys/carrier. DMA allocations remain resident on failure.

AGC, noise-floor, periodic IQ and open-loop temperature compensation routines
are retained. Interrupts, hardware encryption and MAC sequence generation are
disabled. Initial hardware radio-switch checks and periodic checks can stop the
radio; recovery from a fatal device/switch error currently requires reboot.
No suspend/resume, Bluetooth coexistence or HT aggregation is implemented.

## Adaptation fixes and tests

The native adaptation validates EEPROM table ordering, gain/chain counts and
calibration ranges before radio setup. It corrects signed CCK/power offsets,
uses the high calibration pier for high-pier interpolation, clamps both ends
of PDADC shifts using an unmodified temporary table, clamps gain compensation
and uses unsigned descriptor/register shifts. TSF restore follows the saved
pre-reset decision. PCI D0/control writes preserve adjacent W1C status bits;
MSI/MSI-X/INTx and ASPM are disabled for this polling profile.

Run `python3 tools/test_ar9280.py --sanitize` and `--32`. The actual radio/HAL,
native descriptor frontend and station execute against a register/PHY model
and independent WPA2 vectors. Tests cover passive scan, association, M1-M4,
CCMP unicast/group traffic and replay rejection, signed power offsets, open-
loop compensation, pending TX ownership, reset/EEPROM/calibration failures,
1,000 checksum-correct EEPROM mutations, 20,000 RX descriptor mutations,
periodic calibration and two independent cards. Host tests cannot establish
physical RF, regulatory interoperability, sensitivity or throughput; no
physical AR9280 was available.
