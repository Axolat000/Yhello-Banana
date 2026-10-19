# DHCP state and failure matrix

This is the base-17 step 1 inventory of the current implementation, not a
claim of reliable lease recovery. The protocol behavior in `net/dhcp.c` is
unchanged. DNS, IPv6 sockets and network time belong to later substeps.

## Where to observe state

Settings → Network displays the selected card, the driver's link report,
whether an address is configured, and `Mode: DHCP (<state>)` (or manual mode).
It refreshes through the existing once-per-second Settings signature update.
An address being configured does not prove Internet access or lease validity.
A retained address can coexist with `discovering`, `renewing` or a down link.
Link status is only as accurate as the driver: virtio-net and PCnet currently
return up unconditionally; USB link checks may be cached for one second.

`ifconfig` already displays the same `dhcp_state_str()` value, lease length,
and seconds since the last accepted ACK when configured. It also displays
RUNNING/NO-CARRIER separately. The lease age uses unsigned 32-bit milliseconds
and wraps after about 49.7 days; it is not an expiry countdown. The desktop
taskbar's address indicator is not a lease or connectivity test.

The Settings button labelled “Renew address (DHCP)” calls `dhcp_start()`:
it clears `configured` and starts discovery, rather than sending an in-place
renewal. `dhcp` in the terminal does the same. `ifconfig` and `dhcp` save
`/etc/network.conf`; `net/netconf.c` reapplies the saved mode/interface at boot
or when a matching interface appears. A static configuration calls
`dhcp_stop()`, so a later DHCP timer cannot overwrite it.

## States and timers

All times below are relative to entry into a state, assuming timely calls to
`dhcp_timer()` and no replies. They are scheduled deadlines, not hard real-time
guarantees. `net_poll()` runs protocol timers at most once per 10 ms;
netd normally sleeps 20 ms between polls and NIC interrupts can wake it earlier.
Task scheduling and driver work can delay execution. There is no catch-up
loop: a late retry schedules its next interval from the actual execution time.
Deadlines use `timer_ms()`, with signed subtraction of 32-bit milliseconds.
Retry and renewal deadlines spanning one wrap are covered by the host test;
this does not prove behavior after a stall of half the counter range.

| Internal state / displayed value | Entry and action | Deadline and next state |
| --- | --- | --- |
| `D_IDLE` / `idle` | `dhcp_stop()` removes UDP port 68 listener. It does not itself clear address fields or `configured`. | Timer does nothing. |
| `D_SELECTING` / `discovering` | Start/restart chooses a new transaction ID and sends DISCOVER immediately. | Retransmit at 1, 3, 7, 15, 31, 47… seconds; interval caps at 16 s, indefinitely. OFFER moves to requesting. |
| `D_REQUESTING` / `requesting` | OFFER supplies requested address/server; REQUEST sent immediately with the same transaction ID. | Retry REQUEST at 1, 3, 7 s; at 15 s restart discovery with a new ID. ACK binds; NAK clears `configured` and restarts discovery. |
| `D_BOUND` / `bound` | ACK installs address, mask (default /24), router, DNS if supplied, server, lease and `bound_ms`. Announces ARP and warms gateway ARP. | Renewal after `clamp(floor(lease_s / 2), 30, 86400)` seconds. Missing/zero lease defaults to 3600 s. Options 58/59 (server T1/T2) are not parsed. |
| `D_RENEWING` / `renewing` | New transaction ID; REQUEST immediately, `ciaddr` set to configured address. Address remains configured. | Retry at 2, 6, 14, 30 s; at 62 s restart discovery **without clearing configured**. ACK resets bound time and renewal timer; NAK clears configured and restarts discovery. |

All messages, including renewal REQUEST, go to IPv4 broadcast/UDP 67 and
request broadcast replies; there is no unicast renewal or separate T2
rebinding state. Renewal omits requested-IP and server-ID options.
The client ignores UDP bind/send failures. Response filtering checks BOOTREPLY,
transaction ID, MAC and magic cookie, but does not validate server source/port
or enforce a selected server identity. This inventory is not a DHCP security
or malformed-packet audit.

## Failure matrix and call bounds

| Trigger / call | Current result | Bound or limitation |
| --- | --- | --- |
| `dhcp_start()` | Rebind UDP 68, clear configured, start discovery. No device: return without starting. | No wait for server; no result code for bind/send failure. Acquisition can continue forever. |
| `dhcp_timer()` | Advance one due state/retry, then return. | No wait for DHCP replies; nominal intervals above depend on polling. |
| No OFFER | Remain discovering and retry. | No acquisition deadline. |
| OFFER but no ACK | Restart discovery after REQUEST retry sequence. | 15 s per requesting attempt, no overall deadline. |
| Renewal unanswered | Restart discovery while retaining the configured address. | 62 s after entering renewal; **no lease-expiry invalidation**. |
| Lease expires | No dedicated transition or configured-address removal. | A four-second lease is still bound at expiry because T1 is floored at 30 s. Long/infinite lease values are not handled as a separate infinite state. |
| NAK in requesting/renewing | Clear configured and restart discovery. | On accepted NAK; old IP/router/DNS fields remain but configured is false. |
| Cable/carrier down or up | DHCP does not consult `link_up`; current timers continue. | No link-triggered DHCP recovery or guaranteed socket error deadline. |
| Active adapter removed | `net_unregister_device()` selects first remaining card or none; selection clears configured/IP and ARP cache. DHCP starts on replacement, or stops if none. | Event-driven, distinct from cable unplug. Saved interface policy is in netconf; selection is not a carrier monitor. |
| Manual card selection | `net_select_device()` clears configured/IP and ARP, starts DHCP on selected card. | No guarantee of preserved connections; sockets are not explicitly closed by this path. |
| `dhcp_stop()` | Idle and remove listener. | No reply wait. Caller manages configuration (e.g. static mode). |
| `net_wait_configured(timeout_ms)` | Poll/sleep until configured, no device, timeout, or Ctrl+C. | Caller-supplied timeout, checked between waits of 20 ms plus scheduler/poll latency. A retained expired address can return success immediately. |
| Terminal `dhcp` / Settings renew | Restart acquisition. Terminal waits up to 15 s; Settings returns to event loop. | Terminal timeout does not stop background acquisition. No finite server-success guarantee. |

There is currently no reliable maximum recovery time after lease loss or a
30-second carrier outage, and no guarantee that an interrupted download gets
an immediate explicit link/lease error. These are implementation gaps, proposed
as `base-17-dhcp-lifecycle` in the living checklist. The existing base-17 steps
and acceptance obligations remain intact; the proposal requires independent
review and controller validation before scheduling.

## Verification

Run from the repository root:

```
python3 tools/test_dhcp_state.py --sanitize
python3 tools/test_dhcp_state.py --32
make -j4
```

The host harness compiles the real `net/dhcp.c` with controlled time, UDP,
ARP, interface and random shims. It verifies packet sends, state strings,
ACK/NAK, retry times, renewal reset, timer wrap, stop, and the known short-lease,
renewal-exhaustion and carrier gaps. `KNOWN GAP` output deliberately records
current shortcomings; it is not a passing recovery qualification. Fixes must
replace those assertions with the new required behavior.

The runner also extracts the actual `draw_network()` function from Settings
and captures its labels/colors with host drawing shims: discovery, requesting,
bound, renewing, retained-address/link-down, static and no-device displays.
This checks displayed state mapping, not framebuffer layout, refresh scheduling,
NIC drivers or packet interoperability. Native and 32-bit runs are separate;
a compiled harness whose execution fails is not a passing test.

For controlled QEMU qualification after the follow-up, use an isolated DHCP
server with a short lease and observable T1/T2; capture packets and serial logs.
Exercise successful renewal, server silence beyond expiry, NAK and link down
for 30 seconds then up. Check Settings/ifconfig, lease validity, reacquisition
without reboot and errors on an active download. QEMU user networking alone
is not a controlled short-lease/expiry test. Repeat on the user's actual NIC
before claiming physical hardware validation. No such hardware claim is made
here. See `docs/DHCP-STATE-VALIDATION.md` for this change's recorded execution.
