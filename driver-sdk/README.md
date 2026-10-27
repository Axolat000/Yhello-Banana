# Banana OS Driver SDK

A standalone BSD-2-Clause kit for persistent, trusted kernel drivers. This is
separate from the **application SDK** in `sdk/`: application headers, libc,
window APIs and `banana.mk` are not dependencies of the exported Driver SDK.

Build/export: `make driver-sdk` produces `banana-driver-sdk.tar.gz`.
Build all source examples: `make driver-examples`.
Requirements on Linux: GCC with multilib, binutils, make and Python 3.

```make
DRIVER = my-radio
TITLE = My hardware driver
VERSION = 1.0
SRCS = radio.c
DATA = firmware.bin NOTICES
include /path/to/banana-driver-sdk/driver.mk
```

`make` builds i686 and x86_64 ELFs and packs one `type=driver` `.bpk`.
`pkg install my-radio.bpk` installs/loads it; installed drivers load at boot.
`drivers` lists modules. Data files are installed under `/apps/my-radio/`.
Only ship firmware you have permission to redistribute; include its notices.

## ABI and optional services

`banana_driver.h` retains the original version-1 API prefix and every old offset.
Old driver packages continue to work. New CRT code also checks the magic,
version and minimum base-table size. Optional services have their own version
and size; missing extensions return NULL on old kernels:

```c
const bdrv_wifi_api_t* wifi = bdrv_extension(api, BDRV_EXT_WIFI, 1, sizeof(*wifi));
if (!wifi) return -1;
```

See [banana_driver_ext.h](banana_driver_ext.h) for full declarations.

| Extension | Services |
|---|---|
| `BDRV_EXT_NET` | Register/withdraw Ethernet interfaces and deliver RX frames |
| `BDRV_EXT_WIFI` | Register/withdraw raw 802.11 radios using the native station core |
| `BDRV_EXT_USB` | USB driver binding, descriptors, control, endpoints, async bulk/interrupt transfers |
| `BDRV_EXT_POWER` | Battery/AC/lid/brightness snapshots, vendor fallback providers, UCSI status/role requests |
| `BDRV_EXT_FIRMWARE` | Bounded reads of installed firmware/package files |

Descriptors are copied; callback code, contexts, name strings and PCI/USB ID
arrays must remain valid for the registration's lifetime. Driver modules remain
loaded until reboot, including when their entry returns an error. There is no
hot-unload or ring-0 crash isolation. No floating point/SIMD, and no sleeping,
allocation, filesystem, USB or network-extension calls from an IRQ handler.
Use the base API's task/IRQ facilities to defer work to task context.

PCI writes preserve their requested width (including byte/word accesses beside
W1C status fields). Invalid/misaligned configuration accesses and overflowing
DMA/MMIO requests are rejected. Non-display PCI modules skip already-claimed
devices. Display modules retain their historical ability to replace a display.

## Wi-Fi driver contract

Initialize the actual hardware, validated permanent MAC, firmware and calibrated
radio before registering a `bdrv_wifi_radio_t`. Supply poll, raw send,
set-channel and station-context callbacks; `set_station(NULL, 0)` clears state.
Do not register a fake usable interface just because a PCI/USB ID matches.

The kernel handles association, WPA2-PSK/CCMP, replay protection, network
selection and the Settings/taskbar UI. Registering a radio does not make it
connected: carrier stays down until authentication completes. The present
station core supports passive 2.4 GHz channels 1–11, legacy b/g rates and
WPA2-CCMP/open networks; this is not 5/6 GHz, WPA3, HT/VHT/HE or roaming support.

Deliver FCS-stripped 802.11 RX through `receive()` only during your radio poll
callback. Configure hardware so it preserves protected frames, CCMP headers and
ciphertext for software CCMP. TX copies/queues the complete supplied frame
before returning. The module owns the chip-specific MAC/PHY, firmware, RF,
descriptor, filtering and rate setup. USB drivers can leave the USB poll hook
NULL and inspect transfer completion from the radio poll callback.

Ethernet's corresponding `receive()` also runs only during poll, taking complete
14–1514-byte Ethernet frames without FCS. Both paths join the existing interface
registry and saved Auto/Ethernet/Wi-Fi policy.

## USB driver contract

Match exact VID/PID pairs, then return a configuration **index** from probe.
The core sets that configuration before attach. Attach can create a device
context returned to subsequent poll/detach calls. Registration also schedules
late binding in `usbd`, so already-enumerated unclaimed devices are supported.
Built-in drivers keep priority; modules cannot steal an active device.
Overlapping module VID/PID lists are rejected.

USB device and transfer handles are opaque generation cookies. Old handles do
not become valid when a USB slot is reused. Never keep internal USB pointers.
Configuration reads are bounded to 4096 bytes; bulk/interrupt transfers to 64 KiB.
Use `dma_alloc()` for transfer buffers, retain a transfer while pending, and
inspect completion/actual length before reusing or releasing it.

**Cancellation and DMA-quiesce are not available yet.** A detach/timeout does
not authorize freeing a pending transfer or its buffer. `transfer_release()`
rejects pending transfers; keep that memory until completion or reboot.
DMA allocations are retained, and retired network/radio registrations remain
until reboot (at most 16 of each). USB has 16 registration/device slots and 128
reusable completed-transfer slots. Native host-controller limits still apply.

## Laptop providers

`snapshot()` and `usbc_status()` return cached telemetry. Brightness and UCSI
power-role requests are asynchronous; inspect the next snapshot for the result.
Firmware remains responsible for native USB-PD negotiation. This does not add
a TCPC policy engine, arbitrary voltage requests, USB4/TBT or display mux control.

A vendor EC/platform driver can register a `bdrv_power_provider_t`. Its sample
callback runs on a dedicated background worker, never on the UI task. Set flags
only for valid fields, include units, respect unknown values (`percent/minutes`
-1), and terminate model/serial/chemistry strings. Samples expire after 10s;
failed or malformed samples are withdrawn. Up to four provider lifetimes are
available until reboot.

Providers fill information **absent from native ACPI**. They do not replace
working ACPI batteries, AC adapter, lid or backlight; native GPU backlights also
keep priority. The optional backlight callback runs on the provider worker,
and a failed callback does not block the UI. Drivers still implement their own
vendor register/EC protocol with the base PCI, MMIO and port services.

## Examples and validation

- `edu`: original PCI/IRQ/task example for QEMU `-device edu`.
- `bochsfb`: original replaceable display example.
- `sdk-probe`: read-only extension, package-data and laptop/PD diagnostics.
- `usb-serial-demo`: **QEMU FT232 emulation only**. It demonstrates late binding,
  control requests and real asynchronous bulk data in both directions. It
  rejects non-QEMU devices and is not a physical FTDI or Wi-Fi driver.

`tools/test_driver_sdk.py --sanitize` / `--32` exercise the actual kernel
adapters, USB core late-binding, stale handles, pending DMA retention, native
interface callbacks, provider precedence/expiry, firmware bounds and old ABI.
`tools/test_driver_sdk_export.py` builds examples from an extracted independent
archive. `tools/test_driver_sdk_vm.py` boots the OS, loads external packages
and a frozen v1 binary, and exchanges USB bytes with QEMU's emulated device.

These API/model/VM tests do not establish physical Wi-Fi RF calibration,
antenna behaviour or chipset interoperability. For actual radio/ethernet driver
status see [network status](https://github.com/qcom-toolbox/Banana-OS/blob/main/docs/network-priority.md).
