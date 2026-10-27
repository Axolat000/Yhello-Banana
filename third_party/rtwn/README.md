# RTL8188EU hardware source

Adapted from OpenBSD src commit de9d3701611719a42bd2afa65d59f74dda4b04ba:
https://github.com/openbsd/src/tree/de9d3701611719a42bd2afa65d59f74dda4b04ba/sys/dev

- ic/rtwn.c (revision 1.61)
- usb/if_urtwn.c (revision 1.114)
- ic/r92creg.h (unmodified register/ROM/table header)

ISC notices are retained in the files and firmware/realtek/NOTICES. There is
no BSD networking, mbuf, bus or USB dependency. Only the RTL8188EU USB path is
enabled. Native frontend: net/rtl8188eu.c. Header/source SHA-256 before adaptation:

0ff6d69b2687fe3dfa54c417ddf74082e7629c632d4a2e1d00e55b7e6e477dae  rtwn.c
99a378ba8f7783c7d58f8214a9a27994df64116544a15fe5da8e0d811fb9c8f4  if_urtwn.c
5021270f869a0bbef91af1b805fc2a0e50dcbbd051f69b050fb73bfb8536a180  r92creg.h

The adaptation fixes physical/logical EFUSE bounds, fails on control errors,
validates ROM/MAC/calibration, and clamps signed power offsets rather than
wrapping negative values to maximum power. Hardware encryption and sequence
generation are off; native wifi.c owns CCMP and replay protection. RX aggregate
spans are validated before delivery; pending USB buffers/contexts are retained
because usbcore has no transfer cancellation/free API.

Firmware is the unmodified 13,904-byte linux-firmware image from commit
ffc47f18780aeda6c15108d1765e36a543ee4cc4, retrieved from the kernel mirror:
https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/ffc47f18780aeda6c15108d1765e36a543ee4cc4/rtlwifi/rtl8188eufw.bin
Its separate Realtek licence is in firmware/realtek/. It is not disassembled.

Run tools/test_rtl8188eu.py --sanitize and --32. They execute the native
frontend, HAL, embedded firmware and actual WPA2 core against a deterministic
USB/register model and independent AP crypto vectors. They do not simulate RF
propagation or establish that a physical dongle works. Initial IQ/LC calibration
is implemented; periodic temperature tracking is not yet enabled.
