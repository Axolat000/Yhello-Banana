# Requested network controller status

This tracks implementation, not just PCI/USB IDs. "Experimental" means a native
data path with host/protocol models; it does not mean physical interoperability
or performance has been established. The owner has physically validated the
existing RTL8152. No new Wi-Fi/Ethernet adapters were available for physical tests.

| Controller | Native implementation | Remaining validation / work |
| --- | --- | --- |
| Intel AX200 | Pending | Firmware transport, calibration and radio data path |
| Intel AC 7260 | Pending | Firmware transport, calibration and radio data path |
| Intel AX210 | Pending | Firmware transport, calibration and radio data path |
| Intel BE200 | Pending | Firmware transport, calibration and radio data path |
| Atheros AR9280 | Experimental, 168c:002a, MAC 2.0/2.1, native station | Physical RF; initial legacy 2.4 GHz channels 1-11 only |
| Atheros AR9380 | Pending | PCI DMA, radio/calibration and native station integration |
| Realtek RTL8188EUS | Experimental RTL8188EU, 0bda:8179 | Physical RF; initial legacy 2.4 GHz b/g only |
| Realtek RTL8811CU | Pending | Different chipset/firmware; not covered by RTL8188EU |
| MediaTek MT7601U | Experimental, 148f:7601 | Physical RF; initial legacy 2.4 GHz b/g only |
| Intel I219-V | Existing e1000e path | Physical testing of the particular device/revision |
| Intel I211-AT | Existing igb path | Physical testing of the particular device/revision |
| Intel I225-V | Experimental igc | Physical hardware, link negotiation and traffic |
| Intel I226-V | Experimental igc | Physical hardware, link negotiation and traffic |
| Realtek RTL8111 | Existing r8169 path | Physical testing of the particular device/revision |
| Realtek RTL8168 | Existing r8169 path | Physical testing of the particular device/revision |
| Realtek RTL8125 | Experimental r8125 A/B/D | Physical hardware, link negotiation and traffic |
| Broadcom BCM5720 | Experimental 14e4:165f, A0 copper, boot firmware and inactive APE | Active BMC/APE excluded; physical PHY negotiation and traffic pending |
| Marvell Aquantia AQC107 | Experimental, 1d6a:07b1 B0/B1, firmware 2.x/3.x | Physical hardware, PHY negotiation and traffic |
| ASIX AX88179 | Experimental, 0b95:1790 revision 0100 | Physical hardware, link negotiation and traffic |
| ASIX AX88179A | Experimental vendor mode, 0b95:1790 revision 0200 / chip 6; or CDC-NCM/ECM | Physical hardware, PHY negotiation and traffic pending |
| Realtek RTL8153 | Experimental r8153, documented revisions | Physical hardware, link negotiation and traffic |
| Realtek RTL8156 | Experimental r8153, documented revisions | Physical hardware, link negotiation and traffic |
| Realtek RTL8152 | Existing separate driver; owner-tested | Preserve the working path |

The shared station core implements WPA2-PSK/CCMP or open networks, passive scan,
and channels 1–11; Wi-Fi 5/6/6E/7 marketing names do not establish those radio
capabilities in this OS. See [Wi-Fi limits and controls](wifi.md).

Network selection is [Auto/Ethernet/Wi-Fi](network-priority.md), exposed in
Settings/CLI, with Wi-Fi scan/connect also in the taskbar. There is one active
IP interface/default route and carrier-based fallback, not bonding or multiple
simultaneous independent IP stacks.

Laptop ACPI battery/AC/lid/backlight and firmware UCSI 1.1/1.2 Power Delivery
have native implementations and host/VM tests. Physical laptop/charger tests,
USB4/Thunderbolt, DP Alt Mode/mux/retimers and newer UCSI/EPR paths remain
outstanding; see [power and USB-C](power-usbc.md). Those are separate from the
missing network radio drivers.

The [standalone Driver SDK](../driver-sdk/README.md) exposes versioned Ethernet,
Wi-Fi, USB, firmware and cached laptop/power services. The application SDK stays
separate. SDK services and device identification do not substitute for hardware
initialization, firmware, a functioning data path or physical validation.
