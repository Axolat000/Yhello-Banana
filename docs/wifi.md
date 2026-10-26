# Wi-Fi implementation and hardware status

The initial experimental native radio driver is **MediaTek MT7601U**
(reference USB ID `148f:7601`). It uses the native Banana OS USB stack and
station core. It has not yet been validated with a physical dongle.

The driver's model boots the actual embedded firmware through the vendor/USB
protocol, exercises EEPROM, RF/BBP calibration and channel setup, then performs
association, WPA2 handshake, encrypted TX/RX and group rekey through the real
driver and station implementation. This checks software/protocol behavior;
it cannot establish RF calibration, antenna/power behavior or interoperability
on a physical access point.

## Connect

Open **Settings > Wi-Fi**, choose the radio, click Scan, select a network,
type its password and click Connect. The password field is masked and wiped
on submission, page change and close. Open networks are explicitly labelled.
The initial implementation keeps credentials in memory for the current
connection; it does not save passwords or reconnect automatically after reboot.

The integrated Terminal has the same controls without a separate desktop app:

```sh
wifi
wifi scan wlan0
wifi connect wlan0 "My network" --password-file /home/banana/wifi.secret
wifi disconnect wlan0
wifi connect wlan0 "An open network" --open
ifconfig priority wifi
```

A password file contains an 8–63 character WPA2 passphrase or 64 hexadecimal
PSK characters, optionally followed by a newline. Keep that file private
(`chmod 600`). The secret is not placed on the command line or echoed by the
command. Temporary password buffers are wiped after use.

A radio can associate while Ethernet remains the preferred default route.
Settings > Network or `ifconfig priority auto|ethernet|wifi` controls selection.

## Initial scope

- Station mode on 2.4 GHz channels 1–11, passive scanning, 20 MHz legacy b/g
  rates, EEPROM power settings and adaptive legacy TX rates.
- WPA2-PSK with CCMP, or an explicitly selected open network.
- Software CCMP encryption/authentication and replay counters; no dependency
  on a hardware crypto key table.
- Four-way and group key handshakes with retransmission handling. Repeated
  key messages do not reset installed key packet-number state.
- Strict frame/IE bounds and a closed data port before authentication.
- No HT/802.11n throughput, 5/6 GHz, hidden-SSID discovery, WEP/TKIP, WPA1,
  WPA3/SAE, mandatory PMF, fragmentation, A-MSDU or background roaming yet.
- USB transfer contexts are retained after a DMA timeout and on detach because
  usbcore does not yet provide a cancellation/DMA-free interface. This avoids
  use-after-free, but repeated unplug/replug can consume additional memory.

The requested Intel AX200/AX210/7260/BE200, Atheros AR9280/AR9380 and Realtek
RTL8188EUS/RTL8811CU radio drivers are still pending. Their device IDs are not
added as false working network interfaces.

## Firmware, provenance and tests

The unmodified MT7601U firmware is 45,412 bytes, from linux-firmware:
[firmware source](https://gitlab.com/kernel-firmware/linux-firmware/-/blob/main/mediatek/mt7601u.bin).
SHA-256:
`4511b1d840e22aea2bf5fdca419c91c0d94cbfb291b9ac4f8be6d9100d1a7046`.
Its separate redistribution terms are in `firmware/mediatek/`. The full firmware
and OpenBSD hardware-source notices are available inside the running OS at
`/usr/share/licenses/mt7601u/NOTICES`, including after an installed-system update.

Hardware register/calibration code is adapted from OpenBSD
[if_mtw.c revision 1.13](https://github.com/openbsd/src/blob/master/sys/dev/usb/if_mtw.c)
and [mtwreg.h revision 1.2](https://github.com/openbsd/src/blob/master/sys/dev/ic/mtwreg.h),
under their retained ISC notices. This adds no Linux/BSD kernel dependency.

Run `tools/test_mt7601.py`, `tools/test_wifi_station.py` and
`tools/test_wifi_crypto.py` with `--sanitize` or `--32`.
The Python tests need `python3-cryptography`. Independent hashlib/HMAC,
AES key-wrap and AESCCM implementations produce the expected access-point
messages and ciphertext. Sanitizer errors fail the tests rather than merely
printing diagnostics. These checks run in GitHub Actions before ISO publication.


## Taskbar Wi-Fi menu

Click the network icon in the taskbar, then **Wi-Fi networks...**. The flyout
shows a compatible radio's state, nearby SSIDs, security and signal strength.
It starts a passive scan when the selected radio is disconnected; **Scan**
refreshes it, **Next radio** selects another adapter, and **Disconnect** ends
the selected radio's connection. Connected radios must disconnect before
scanning again.

Select a network, enter its WPA2 password and press Enter or **Connect (WPA2)**.
Open networks require an explicit **Connect (open)** click. Passwords are masked
and erased on submission, closing, scanning, changing selection/radio or
unplugging the radio; they are not saved by this menu. Unsupported security and
hidden SSIDs cannot connect through the flyout. **Wi-Fi settings...** opens the
existing full page. Tab/arrows/Enter and Escape support keyboard use.

The flyout uses the same station core and driver limits as Settings; it does not
add hardware support. It does not override the saved Ethernet/Wi-Fi priority
when authenticating a radio. The adapter overview is paged so all 16 registered
adapters remain accessible without overflowing a 640x480 desktop.

Native flyout tests (`tools/test_nettray.py --sanitize` and `--32`) cover actions
with simulated radios, key input, stale scan results, hot unplug, password
erasure, paging and layout. They complement the station/driver tests; physical
Wi-Fi validation is still pending.
