# Laptop power and USB-C status

This is an experimental native ACPI/USB-C firmware integration, not a claim that
every laptop, USB-C port, charger or dock has been validated.

## User controls

Open **Settings > Power & USB-C** (`settings power`) for battery percentage,
charging/discharging state, capacity and an estimate of remaining time, AC
adapter/lid status, brightness controls and the selected USB-C port.
The page reads cached telemetry, so AML evaluations do not run on the UI task.
Multiple batteries and ports have selection buttons.

Commands:

```sh
laptop                  # ACPI runtime, EC, battery, adapter, lid and backlight
brightness              # query the active panel
brightness 80           # native GPU backlight, falling back to ACPI
usbc                    # firmware UCSI port status and active PD contract
usbc role 1 sink        # request reception of power on connector 1
usbc role 1 source      # request supplying power, if firmware permits it
```

A role request is asynchronous. Re-run `usbc` to inspect the actual role.
The PPM and connected partner may reject a swap. The last request result and
last command failure remain visible in `usbc`. GET_ERROR_STATUS retrieves firmware
causes such as partner refusal, policy conflict, CC communication failure and
overcurrent; the complete error mask is retained. Settings shows the last error.
An accepted command is not a guarantee that the requested role is already active.
Capabilities and connection
state are checked before sending SET_PDR. Automatic power-role changes,
raw voltage/current requests and connector hard resets are not sent at boot.
PPM_RESET resets the UCSI command interface; PD negotiation stays in firmware.

## Implemented paths

- Pinned uACPI interpreter, with native allocation, PCI/port IO, mapping, clocks,
  sleeping semaphores, BSP event worker and legacy SCI interrupt integration.
- One ACPI Embedded Controller, from ECDT or PNP0C09 resources. Byte transactions,
  bounds and timeouts, _GLK global locking when requested, _Qxx queries and a
  deferred level-triggered integer _GPE handler. Query polling is retained.
- Up to four PNP0C0A batteries through _BIX (or _BIF), _BST and _STA. Unknown
  capacities/rates are not interpreted as valid values. mAh/mWh units are retained.
- Up to four ACPI0003 AC adapters via _PSR and one PNP0C0D lid via _LID.
- A panel exposing _BCL/_BCM/_BQC. Requested brightness is rounded to an advertised
  firmware level; native GPU backlight support keeps priority.
- One USBC000 ACPI UCSI interface using _CRS memory and the standard _DSM GUID
  6f8398c2-7ca4-11e4-ad36-631042b5008f. Writable fixed/assigned 32/64-bit memory
  resources are checked before access; the 32-bit kernel declines high addresses.
- UCSI **1.1/1.2** mailbox commands: reset, notifications, capabilities, connector
  capabilities/status, PDO retrieval, completion acknowledgements, cancel and
  supported power-role requests. Failed/oversized replies are rejected.
- PD contract decoding uses the selected Source PDO and RDO. Fixed SPR PDOs show
  voltage/current/power; battery PDOs show power without inventing a voltage;
  PPS APDOs show their requested voltage/current. Capabilities alone are never
  presented as measured charger output or an active contract.

The existing shutdown/S3 implementation remains separate. This does not add a
complete laptop suspend/wake policy, automatic lid-close suspend, charge limits
or a CPU thermal/power policy.

## Limits

Physical laptop/charger/EC/PD validation is still pending. Only legacy SCI IRQs
1..15 and an HPET/ACPI PM clock are required by this host bridge. Multiple ECs/UCSI PPMs, package-valued
EC _GPE descriptions, vendor EC transports and some platform quirks need work.
The runtime has a 32 MiB allocation budget and bounded AML loops/call depth.

UCSI 2.x/3.x extended layouts and EPR/AVS are declined or left undecoded.
USB-C data uses the existing USB host-controller stack where supported.
USB4/Thunderbolt tunnelling, DisplayPort Alt Mode/mux/retimer management,
USB device/gadget mode and proprietary/non-UCSI PD controllers are not supplied
by this milestone. A USB-C-shaped connector alone does not identify those drivers.

## Validation

`python3 tools/test_ucsi.py --sanitize` and `--32` test the actual command engine,
fixed/battery/PPS contract decoding, connection changes, role checks, malformed
lengths, timeouts/cancel, command/ack failures and mutated PDO/RDO values.

`python3 tools/test_power_host.py --sanitize` and `--32` test the actual kernel
callbacks and EC code: allocation bounds, locks/events, IRQ/deferred work, exact
PCI access widths (preserving neighbouring W1C fields), IO bounds, EC reads/writes,
timeout recovery, global locking, _Q42 dispatch/GPE completion and ACPI 64-bit
resource bounds. Those tests mock hardware IO; they do not link the interpreter.

`python3 tools/test_laptop_vm.py --firmware bios|uefi` (or `--32` on BIOS) boots
the actual kernel, uACPI interpreter and native UCSI transport. A synthetic SSDT
and shared-memory PPM expose a 50% battery, adapter/lid, adjustable brightness,
a 20 V / 3 A contract and both power-role changes. It verifies the licence and
boot selftests too. QEMU/OVMF uses 32-bit AML on this machine model, so the fixture
keeps its dynamically addressed BAR below 4 GiB; high memory resource handling
has separate host-model coverage. ISO SHA-256 and VM arguments are recorded.
A `--reject-role` fixture also rejects both swaps and reports a partner refusal
through GET_ERROR_STATUS; it verifies the actual role stays unchanged and that
normal polling continues. Malformed/error replies to GET_ERROR_STATUS and failed
connector acknowledgements have separate host-model coverage.
These host and VM checks gate prerelease publication.

References:
- [uACPI](https://github.com/uACPI/uACPI) (MIT, pinned in third_party/uacpi/BANANA.md)
- [Intel UCSI 1.2 specification](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/usb-type-c-ucsi-spec.pdf)
- [Linux UCSI protocol definitions](https://github.com/torvalds/linux/blob/v6.12/drivers/usb/typec/ucsi/ucsi.h)
- [Linux ACPI transport reference](https://github.com/torvalds/linux/blob/v6.12/drivers/usb/typec/ucsi/ucsi_acpi.c)
