# Multiple network adapters and connection priority

Settings > Network has Auto, Ethernet and Wi-Fi priority buttons. The same
persistent setting is available through the integrated Terminal:

```sh
ifconfig                         # list cards, carrier and active interface
ifconfig priority                # show preference and active card
ifconfig priority auto           # prefer connected Ethernet, fall back to Wi-Fi
ifconfig priority ethernet       # prefer connected Ethernet, fall back to Wi-Fi
ifconfig priority wifi           # prefer authenticated Wi-Fi, fall back to Ethernet
ifconfig eth1 up                 # explicitly prefer one named interface
ifconfig reset                  # remove saved network settings
```

Renewing DHCP preserves the selected media priority and any explicit interface pin.
Changing media priority returns to DHCP and removes an explicit interface pin
and static address. Manual addresses remain specific to the selected interface;
they are not copied onto an unrelated fallback network. Settings are kept in
`/etc/network.conf` and survive reboot on an installed disk. An explicit
interface selection takes precedence over media priority while its link is up;
another connected card can still take over when it goes down.

The stack accepts up to 16 registered interfaces. There is one active IP
interface/default route, rather than simultaneous per-interface IP addresses,
load balancing, bridging or bonding. PCI drivers which expose one device per
driver family still have that limitation; mixed supported PCI families and USB
adapters can coexist. Interface names are unique among present devices after
USB unplug/replug; they are not permanent MAC-based names across boot order
changes.

A better connected candidate must remain preferred for three seconds before
switching. Unplugging the active USB device withdraws it immediately. An equal
priority link does not displace the current one. USB transport by itself gives
no extra priority. CDC-ECM currently assumes carrier is up because its link
notifications are not consumed; physical link failover cannot be guaranteed for
that class driver. Auto currently uses the same wired-first rule as Ethernet.

Switching interface starts DHCP, clears ARP, IPv6 neighbor/router state and the
DNS cache, and ends external TCP sessions with a network-not-ready error so
applications can reconnect. Local loopback TCP sessions and listening sockets
remain available. This is carrier-based failover: detecting a failed Internet
uplink behind a live Ethernet/Wi-Fi link is not implemented.

## Driver status

New native ASIX AX88179 (`0b95:1790`) and CDC-NCM/NTB16 drivers are experimental,
tested with host/device protocol models, not physical adapters. AX88179A can
use the class driver when its selected USB configuration exposes CDC-NCM or
CDC-ECM; its vendor-specific protocol is not claimed. The old RTL8152 and
CDC-ECM bindings remain separate.

RTL8153/RTL8153B (5c00/5c10/5c20/5c30/6000/6010) and RTL8156/RTL8156B
(7020/7030/7410) now have a separate experimental native vendor driver.
It handles boot/PHY readiness, family-specific reset and FIFO setup, MAC filters,
10/100/1000/2500 autonegotiation, aggregated RX and link renegotiation.
Both existing RTL8152 revisions and all nine new revisions run through the
register/USB models in 32-bit and sanitizer builds. RTL8152 is already reported
working on physical hardware by the project owner; the new revisions still need
physical validation. Unknown versions (including 7420/8153D) are declined.

The requested Wi-Fi chipsets (Intel AX200/AX210/BE200/7260, Atheros AR9280/AR9380,
Realtek RTL8188EUS/RTL8811CU) do **not** yet have working
radio drivers in Banana OS. A native station core now implements passive 2.4 GHz
discovery, open-system association, WPA2-PSK/CCMP handshake, group rekey and
software CCMP. It is tested against an independent simulated access point,
including retransmissions, key-reinstallation/replay attempts, invalid MICs,
timeouts and malformed frames. It is now connected to an experimental MT7601U USB radio driver, with the
firmware included in the OS and Settings/CLI controls. Physical RF validation
is still pending; see [Wi-Fi setup and limits](wifi.md). Wi-Fi priority is infrastructure
for other drivers. MT7601U is the initial experimental radio implementation. A future radio must
report carrier only after association/authentication; an unconnected radio or
one without a carrier callback cannot win automatic selection.

Intel I219/I211 and Realtek RTL8111/8168 use existing drivers. The requested
I225-V (8086:15f3) and I226-V (8086:125c) now use a separate experimental
`igc` driver. It probes multiple cards independently, initializes PHY/NVM
through the Intel/OpenBSD shared HAL, negotiates 10/100/1000/2500 and uses
bounded polling with advanced DMA descriptors. MTU is 1500, one queue,
software checksums; no MSI/MSI-X, RSS, jumbo frames or performance claims.
Cards without the expected PHY, valid loaded flash NVM/MAC, or completed reset
are declined. Physical validation is pending.

RTL8125/A/B/D now has a separate experimental native `r8125` driver:
PCI 10ec:8125, hardware revisions 60900000 / 64100000 / 68800000 / 68900000.
It initializes MAC, EPHY and PHY, loads Realtek microcode and verifies the PHY
RAM version, negotiates 10/100/1000/2500, and uses independent 32-byte DMA rings
per card. Polling, MTU 1500, software checksums and memory BAR2 are required.
Unknown revisions, IO-only cards and RTL8126/8127 are not bound. There is no
jumbo/VLAN/offload/RSS or hardware-performance claim; physical testing is pending.
The old RTL8111/8168 and RTL8152 drivers are unchanged. BCM5720 and AQC107 remain
pending.
A matching device ID alone is not sufficient to declare these supported.

## Validation

`tools/test_network_drivers.py --sanitize` and `--32` run the actual interface
registry, persisted configuration, debounce policy, TCP switch handling and
USB drivers against deterministic models. They include 20,000 mutated/truncated
USB receive aggregates per run. DHCP lifecycle has a separate regression suite.

`tools/test_network_vm.py --model MODEL` boots the built ISO, acquires DHCP,
pings its gateway and downloads 30 MiB from a local HTTP fixture, checking the
complete file's SHA-256. Models: e1000, e1000e, igb, pcnet, rtl8139,
virtio-net-pci, usb-ecm and multi. The multi model removes/restores a preferred
e1000 link through QMP, checks fallback to RTL8139 and return to e1000, and
verifies downloads across both links. These tests gate ISO publication in CI.
QEMU cannot establish that unmodeled physical controllers work.

The Wi-Fi station and crypto tests require Python's `cryptography` package:
`python3 tools/test_wifi_crypto.py --sanitize` and
`python3 tools/test_wifi_station.py --sanitize` (also `--32`).
Fixtures use independent Python HMAC/PBKDF2, AES key wrap and AESCCM rather than
round trips through the implementation being tested.

`python3 tools/test_igc.py --sanitize` and `--32` execute the actual Intel HAL
and native DMA frontend against a register model. They cover two simultaneous
cards, PCI matching, NVM checksum/MAC/PHY initialization, ring wrap and full
TX queues, RX length/error/fragment bounds, link loss/recovery, failed MDIC
transactions, missing firmware autoload, reset stalls and stuck semaphores.
They verify no NVM programming occurs. Hardware remains untested.


`python3 tools/test_r8125.py --sanitize` and `--32` run the actual native
frontend and extracted OpenBSD/Realtek hardware code with four independent
A/B/D controller models, cold/warm microcode initialization and expected PHY
RAM versions. They cover traffic/ring wrap/full queues, RX/FCS/error bounds,
link changes, stuck MAC/PHY/EPHY/CSI/reset/patch handshakes, dropped version
writes, invalid MAC and unknown revision. Failed initialization stops PCI bus
mastering and registers no interface. The pinned firmware data hash is checked.
Firmware writes are modeled, not executed by a physical PHY/MCU.
