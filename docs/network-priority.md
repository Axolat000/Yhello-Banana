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

The requested Wi-Fi chipsets (Intel AX200/AX210/BE200/7260, Atheros AR9280/AR9380,
Realtek RTL8188EUS/RTL8811CU and MediaTek MT7601U) do **not** yet have working
radio/association/security drivers in Banana OS. Wi-Fi priority is infrastructure
for those drivers, not a claim that Wi-Fi already works. A future radio must
report carrier only after association/authentication; an unconnected radio or
one without a carrier callback cannot win automatic selection.

Intel I219/I211 and Realtek RTL8111/8168 use existing drivers. The requested
I225/I226, RTL8125, BCM5720, AQC107 and USB RTL8153/RTL8156 are still pending.
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
