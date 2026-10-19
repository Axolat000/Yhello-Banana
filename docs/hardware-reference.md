# Reference machines: inventory and qualification

Scope: `base-20`, step 1 only. Updated 2026-10-19 against source commit
`fe73f98e2bf9aef877b4fdea5f8f73a724d6fd7c`. The VM below is a proposed
reference profile with executed emulator inventory. The desktop and laptop
records are blocked: their models, availability and inventories have not been
provided. The Debian build server is not an identified physical test target.
Step 1 remains incomplete until both physical records contain real inventories.
Driver candidates and daily workflows belong to steps 2 and 3.

## Evidence rules

Each row has a stable ID, date, and Banana OS result. Results mean:

- **not tested**: no relevant Banana execution evidence;
- **detected**: Banana enumerated the exact device, with a saved log;
- **functional**: a specified operation passed on that device;
- **load**: a stated workload and duration passed;
- **resume**: a stated sleep mode and resume operation passed.

Keep dated observations when adding results; identify source commit, ISO hash,
profile/firmware, command or manual action, expected/observed outcome and log.
An emulator inventory, Linux enumeration or source inspection does not establish
Banana detection or functionality. Unknown IDs are not evidence of absent devices.
No physical hardware compatibility is claimed here.

## VM-01: proposed reference VM

Availability: QEMU was available locally and instantiated this profile on
2026-10-19. No guest OS or firmware instructions were executed (`-S`, no ISO).
The [inventory transcript](evidence/base-20/vm-inventory-2026-10-19.txt) records
the exact command, version, firmware SHA-256 and QMP results; QEMU exited 0.

| Row ID | Field | Exact configuration / observation | Banana result (2026-10-19) |
| --- | --- | --- | --- |
| VM-01-CPU | CPU | `qemu64-v1`, TCG, 1 socket / 4 cores / 1 thread per core; QMP listed 4 CPUs | not tested |
| VM-01-RAM | Memory | 512 MiB; qtree reported 536870912 bytes below 4 GiB | not tested |
| VM-01-BOARD | Machine | `pc-i440fx-9.2`, QEMU 10.0.13, Debian package `qemu-system-x86` `1:10.0.13+ds-0+deb13u1` | not tested |
| VM-01-FW | Firmware | Legacy BIOS, `/usr/share/seabios/bios-256k.bin`, Debian `seabios` `1.16.3-2`; hash below | not tested |
| VM-01-SLEEP | Sleep | ACPI PIIX4 present in emulator; supported sleep modes and Banana resume not measured | not tested |
| VM-01-PCI-00 | Host bridge | `00:00.0` `8086:1237` | not tested |
| VM-01-PCI-01 | ISA bridge | `00:01.0` `8086:7000` | not tested |
| VM-01-PCI-02 | IDE controller | `00:01.1` `8086:7010` | not tested |
| VM-01-PCI-03 | ACPI bridge | `00:01.3` `8086:7113` | not tested |
| VM-01-PCI-04 | VGA | `00:02.0` `1234:1111`, explicit `VGA` | not tested |
| VM-01-PCI-05 | Ethernet | `00:03.0` `8086:100e`, explicit `e1000`, user NAT | not tested |
| VM-01-PCI-06 | USB UHCI controller | `00:04.0` `8086:7020`, explicit `piix3-usb-uhci` | not tested |
| VM-01-USB-01 | USB keyboard | `usb.0`, port 1, `usb-kbd`; expected VID:PID `0627:0001` from source; QMP confirmed product and attachment only | not tested |
| VM-01-USB-02 | USB tablet | `usb.0`, port 2, `usb-tablet`; expected VID:PID `0627:0001` from source; QMP confirmed product and attachment only | not tested |
| VM-01-MEDIA | Storage | Empty IDE CD-ROM on `ide.1`, unit 0; no writable disk or USB storage attached | not tested |
| VM-01-INPUT | Other input | Chipset PS/2 keyboard/mouse instantiated in qtree | not tested |
| VM-01-OTHER | Other peripherals | No additional PCI/USB devices in captured inventory; audio, camera and physical passthrough not configured | not tested |

All seven PCI functions reported subsystem `1af4:1100`. USB IDs above are
**source-derived expectations**, not descriptor reads from the running Debian
binary or Banana. Both HID devices share an ID in
[QEMU v10.0.13 HID descriptor definitions](https://github.com/qemu/qemu/blob/v10.0.13/hw/usb/dev-hid.c#L379);
retain device type and port to distinguish them. Verify descriptors in a guest
before promoting them to observed IDs.

SeaBIOS SHA-256:
`fc6d2ea888862e6b26d53fb877ab712324726eb8f44eb6baf076c64a31f1c3fb`.
Firmware package identification was collected with
`dpkg-query -W -f='${Package} ${Version}\n' seabios qemu-system-x86`.

Reproduce the paused inventory, without a disk image or host USB passthrough:

```sh
qemu-system-x86_64 \
  -machine pc-i440fx-9.2 -accel tcg -cpu qemu64-v1 \
  -m 512M -smp 4,sockets=1,cores=4,threads=1 \
  -bios /usr/share/seabios/bios-256k.bin -nodefaults \
  -device VGA,addr=2 \
  -netdev user,id=net0 -device e1000,netdev=net0,addr=3 \
  -device piix3-usb-uhci,id=usb,addr=4 \
  -device usb-kbd,bus=usb.0,port=1 -device usb-tablet,bus=usb.0,port=2 \
  -device ide-cd,bus=ide.1,unit=0 \
  -display none -serial none -S -qmp stdio
```

Send these JSON lines to QMP after its greeting, one at a time:

```json
{"execute":"qmp_capabilities"}
{"execute":"human-monitor-command","arguments":{"command-line":"info pci"}}
{"execute":"human-monitor-command","arguments":{"command-line":"info usb"}}
{"execute":"human-monitor-command","arguments":{"command-line":"info cpus"}}
{"execute":"human-monitor-command","arguments":{"command-line":"info qtree"}}
{"execute":"quit"}
```

Compare PCI addresses/IDs, USB ports/products and CPU count with the transcript;
host thread IDs are incidental. For future Banana qualification, attach a known
ISO to this CD-ROM, record its SHA-256 and build commit, enable serial capture,
and remove `-S`. A headless inventory does not test display or input behavior.

`tools/dev.sh` currently leaves machine/CPU/accelerator at QEMU defaults and has
no explicit USB devices. Its results must record the actual arguments; they
cannot silently be attributed to VM-01. BIOS, UEFI and 32-bit test runs are
separate configurations. VM-01 does not establish UEFI, KVM, VirtIO disk,
installation, networking or sleep/resume qualification.

## DESKTOP-01: available desktop, identification blocked

| Row ID | Required field | Evidence / missing input | Banana result (2026-10-19) |
| --- | --- | --- | --- |
| DESKTOP-01-MODEL | Manufacturer, exact model/revision, availability | Unknown; owner must identify the machine and confirm test access | not tested |
| DESKTOP-01-CPU | CPU model, architecture, topology, RAM | Unknown; collect on the identified desktop | not tested |
| DESKTOP-01-FW | Firmware vendor/version/date, BIOS or UEFI boot, Secure Boot state if UEFI | Unknown; collect on the identified desktop | not tested |
| DESKTOP-01-PCI | Every PCI BDF, vendor:device, subsystem IDs and revision | Unknown; `lspci -Dnn` and `lspci -Dnnv` required | not tested |
| DESKTOP-01-USB | Every USB VID:PID, product and attachment path | Unknown; `lsusb` and `lsusb -t` required, including internal devices | not tested |
| DESKTOP-01-SLEEP | Advertised sleep modes and selected mode | Unknown; collect `/sys/power/mem_sleep`; Banana resume untested | not tested |
| DESKTOP-01-PERIPHERALS | Actually available monitor/input/storage/network/audio/other peripherals | Unknown; owner must list connected and available test peripherals | not tested |

## LAPTOP-01: available laptop, identification blocked

| Row ID | Required field | Evidence / missing input | Banana result (2026-10-19) |
| --- | --- | --- | --- |
| LAPTOP-01-MODEL | Manufacturer, exact model/revision, availability | Unknown; owner must identify the machine and confirm test access | not tested |
| LAPTOP-01-CPU | CPU model, architecture, topology, RAM | Unknown; collect on the identified laptop | not tested |
| LAPTOP-01-FW | Firmware vendor/version/date, BIOS or UEFI boot, Secure Boot state if UEFI | Unknown; collect on the identified laptop | not tested |
| LAPTOP-01-PCI | Every PCI BDF, vendor:device, subsystem IDs and revision | Unknown; `lspci -Dnn` and `lspci -Dnnv` required | not tested |
| LAPTOP-01-USB | Every USB VID:PID, product and attachment path | Unknown; `lsusb` and `lsusb -t` required, including internal devices | not tested |
| LAPTOP-01-SLEEP | Advertised sleep modes and selected mode | Unknown; collect `/sys/power/mem_sleep`; Banana resume untested | not tested |
| LAPTOP-01-PERIPHERALS | Internal panel/keyboard/touchpad, Wi-Fi, audio, camera, battery and external peripherals | Unknown; owner must list built-in devices and available external test peripherals | not tested |

## Completing the physical records

Run read-only inventory commands on each actual target under Linux, separately.
Do not substitute the build host. Add the UTC capture date and collection OS
version (`uname -sr`), plus an owner statement that the machine is available.
Record missing commands/files as unavailable; no system installation is needed.

```sh
date -u +%FT%TZ
uname -sr
lscpu
free -m
lspci -Dnn
lspci -Dnnv
lsusb
lsusb -t
cat /sys/power/mem_sleep
if [ -d /sys/firmware/efi ]; then echo UEFI; else echo 'Legacy BIOS (Linux boot)'; fi
```

For firmware/model information, read only these individual sysfs fields if
readable: `/sys/class/dmi/id/sys_vendor`, `product_name`, `product_version`,
`board_name`, `board_version`, `bios_vendor`, `bios_version`, `bios_date`.
For UEFI Secure Boot, use `mokutil --sb-state` if already available or record
its state from firmware setup. Record enabled storage mode (AHCI/RAID), graphics
selection and firmware sleep setting if exposed; do not change settings.
The brackets in Linux `mem_sleep` show the selected Linux mode, not a successful
Banana sleep test. `s2idle` and `deep` observations do not prove S0ix residency.

Do not collect whole DMI tables, serial-number/UUID fields, verbose USB serial
strings, authentication data or account information. Review inventory text for
private identifiers before adding it to the repository. Include docking state,
which ports/peripherals were connected, and which are available for later tests.
Expand each physical PCI/USB placeholder into stable device rows when the
inventory arrives; retain the dated missing-input observation. A genuine empty
USB list requires a dated capture, not an assumption.

To unblock step 1, both physical models and their availability must be confirmed,
CPU/firmware and all PCI/USB device rows must be populated with dated evidence,
and unavailable data must be explicitly justified. No Banana functionality
claim is required just to establish the inventory. Guest USB descriptor capture
for VM-01 remains pending. Independent review and controller validation are
required before checklist planning edits affect scheduling.
