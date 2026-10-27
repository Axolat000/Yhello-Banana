# Aquantia Atlantic AQC107 hardware code

Derived from OpenBSD sys/dev/pci/if_aq_pci.c revision 1.40, commit
d2481591d62e40a44f0e3c4e342a98912d4ede97.
https://github.com/openbsd/src/blob/d2481591d62e40a44f0e3c4e342a98912d4ede97/sys/dev/pci/if_aq_pci.c

Original SHA-256: b119cb39c9238ebb0ceb65a64b7b32a499fa73fba0618602f6fde4eebfdc996a

ISC, Aquantia BSD-3-Clause and Ryo Shimizu BSD-2-Clause notices are retained in
the hardware files and NOTICES. The OS installs them under
/usr/share/licenses/aqc107/NOTICES. There is no BSD kernel/netstack dependency.

The native frontend is net/aqc107.c. It supports PCI 1d6a:07b1, Atlantic B0/B1
with card-resident flash firmware API 2.x/3.x. It never flashes or downloads
replacement firmware. Firmware 1.x, unknown revisions, host-boot-only cards,
AQC107S and AQC108/109/113 are not claimed. Failed reset/mailbox/MAC/ring setup
does not publish an interface and disables PCI bus mastering. DMA allocations
remain resident after failure so an unresponsive controller cannot write into
freed memory.

One polled queue, 128 RX/64 TX descriptors, MTU 1500, software checksums, no
VLAN stripping, EEE or RSS. Autonegotiation advertises 100M/1G/2.5G/5G/10G;
negotiated rates do not establish achieved throughput. No interrupt handlers
are used: MSI/MSI-X and INTx are disabled, and cached link status is sampled
every 250 ms. D0 entry preserves PME status; config accesses are 16 bits.
Eight independent lifetime contexts are bounded at boot, including failed
devices. Firmware mailbox pointers/counts and descriptor/packet-chain bounds
are checked. Models test two independent cards, wrap/full queues, malformed
RX, link loss, firmware/reset/semaphore failures and power capability handling.

Run tools/test_aqc107.py --sanitize and --32. They execute the real frontend
and shared hardware functions with modeled registers, flash firmware mailbox
and DMA completions. Physical hardware, PHY/link interoperability and
performance remain unvalidated.
