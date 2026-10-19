# DNS and SDK Socket Mapping Characterization Matrix

This document presents the characterization, test results, failure modes, call bounds, and known architectural gaps for DNS resolution (`net/dns.c`) and SDK socket address mapping (`sdk/lib/socket.c`), delivering **base-17 step 2** of the living checklist.

---

## 1. Overview and Architecture

Banana OS implements networking in two tiers:
1. **Kernel / System Resolver (`net/dns.c`)**:
   - Implements synchronous UDP DNS queries for IPv4 (`dns_resolve`, type A, 1) and IPv6 (`dns_resolve6`, type AAAA, 28) per RFC 1035.
   - Provides a 16-entry TTL-respecting cache (`g_cache`).
   - Supports IPv4 literal parsing (`str_to_ip4`) and IPv6 literal parsing (`ip6_parse`) without issuing network queries.
   - Handles `localhost` statically (`127.0.0.1` for IPv4; `::1` for IPv6).
   - Bound by caller-specified timeouts (`timeout_ms`).
2. **SDK POSIX Layer (`sdk/lib/socket.c`)**:
   - Implements standard lookup functions: `getaddrinfo()`, `freeaddrinfo()`, `getnameinfo()`, `gethostbyname()`, `gethostbyaddr()`.
   - Bridges to the kernel via `__banana->resolve(name, ip)` (sys call `a_resolve` in `kernel/app.c`), which returns only IPv4 addresses (`unsigned char ip[4]`).
   - Sockets created via `socket(AF_INET, ...)` map to kernel `usock.c`, while `socket(AF_INET6, ...)` intentionally returns `-1` with `errno = EAFNOSUPPORT`.

---

## 2. DNS Resolution Matrix (`net/dns.c`)

| Query Type / Scenario | Transport / Method | Behavior | Return Code | Max Latency / Bound |
|---|---|---|---|---|
| **IPv4 literal** (`"10.0.2.15"`) | Direct parse (`str_to_ip4`) | Immediate conversion | `NET_OK` | 0 ms (no network query) |
| **IPv4 localhost** (`"localhost"`) | Hardcoded lookup | Returns `127.0.0.1` | `NET_OK` | 0 ms (no network query) |
| **IPv6 literal** (`"::1"`) | Direct parse (`ip6_parse`) | Checks `ipv6_enabled()`; returns binary `ip6_t` | `NET_OK` (`NET_ERR_NOTREADY` if IPv6 off) | 0 ms |
| **IPv6 localhost** (`"localhost"`) | Hardcoded lookup | Checks `ipv6_enabled()`; returns `::1` | `NET_OK` (`NET_ERR_NOTREADY` if IPv6 off) | 0 ms |
| **Cached Entry (A or AAAA)** | In-memory lookup | Matches name, type and `expires_ms > timer_ms()` | `NET_OK` | 0 ms (cache hit) |
| **A record query** | UDP IPv4 to `nif->dns:53` | 3 attempts, 2-second interval, retransmits with same transaction ID | `NET_OK` on answer | `min(timeout_ms, 6000)` ms |
| **AAAA record query (IPv6 DNS available)** | UDP IPv6 to `server6:53` | Transmitted over `udp6_send()` using ephemeral port | `NET_OK` on answer | `min(timeout_ms, 6000)` ms |
| **AAAA record query (no IPv6 DNS)** | UDP IPv4 to `nif->dns:53` | Transmitted over IPv4 transport querying type 28 | `NET_OK` on answer | `min(timeout_ms, 6000)` ms |
| **NXDOMAIN (rcode = 3)** | UDP response | `parse_reply()` records `q->rcode = 3`; `!q->found` triggers return | `NET_ERR_DNS` | Immediate upon packet receipt |
| **Mismatched Transaction ID** | UDP response | `rd16(p) != q->id` causes early return from `parse_reply()`; packet ignored | `NET_ERR_TIMEOUT` (if no valid packet follows) | Bounded by `timeout_ms` |
| **Mismatched Query Section** | UDP response | `memcmp(p + 12, q->question, q->qlen) != 0` or type mismatch causes packet drop | `NET_ERR_TIMEOUT` (if no valid packet follows) | Bounded by `timeout_ms` |
| **Malformed RDLENGTH / Truncated Record** | UDP response | Offset boundary check `off + rdlen > len` aborts loop cleanly | `NET_ERR_DNS` (or timeout if discarded) | Bounded by `timeout_ms` |
| **Truncated Reply (`TC` bit = 1, `0x0200`)** | UDP response | `parse_reply()` sets `q->tc = 1`; `resolve()` falls back to `resolve_tcp()` (RFC 1035 4.2.2) | `NET_OK` on answer | Bounded by remaining `timeout_ms` |
| **Server Silence / Packet Loss** | Unanswered UDP | 3 attempts with 2000 ms per-attempt wait, polling `net_wait(20)` | `NET_ERR_TIMEOUT` | Exactly `min(timeout_ms, 6000)` ms |
| **Unconfigured Interface** | Pre-flight check | `net_wait_configured(timeout_ms)` fails if DHCP not bound | `NET_ERR_NOTREADY` | Caller timeout or DHCP completion |
| **Missing DNS Server** | Pre-flight check | `nif->dns == 0` | `NET_ERR_DNS` | Immediate |
| **Missing Network Device** | Pre-flight check | `nif->dev == NULL` | `NET_ERR_NODEV` | Immediate |

---

## 3. SDK POSIX Socket Address Mapping (`sdk/lib/socket.c`)

| Function | Input Family / Arguments | Handled Mapping / Behavior | Return Value / Error Code |
|---|---|---|---|
| `getaddrinfo()` | `AF_INET`, `node="localhost"` | Synthesizes `sockaddr_in` with `127.0.0.1` | `0` (`EAI_SUCCESS`) |
| `getaddrinfo()` | `AF_INET`, `node="example.com"` | Calls `lookup()` -> `__banana->resolve()` -> `dns_resolve()` | `0` on success, `EAI_NONAME` on resolution failure |
| `getaddrinfo()` | `AF_INET`, numeric IP | Calls `inet_aton()` directly | `0` (`EAI_SUCCESS`) |
| `getaddrinfo()` | `AF_INET6`, any node | Checked at function entry: `family != AF_UNSPEC && family != AF_INET` | `EAI_FAMILY` |
| `getaddrinfo()` | `AF_UNSPEC`, any node | Only resolves and returns `AF_INET` (`sockaddr_in`) records | `0` (`EAI_SUCCESS`) |
| `getnameinfo()` | `AF_INET` (`sockaddr_in`) | Formats numeric IPv4 address and port | `0` (`EAI_SUCCESS`) |
| `getnameinfo()` | `AF_INET6` (`sockaddr_in6`) | Checked at entry: `sa->sa_family != AF_INET` | `EAI_FAMILY` |
| `gethostbyname()` | Node name | Calls `lookup()`; returns static `struct hostent` | Pointer to `hostent`, or `NULL` (`h_errno = HOST_NOT_FOUND`) |
| `gethostbyaddr()` | IPv4 address | Length 4, family `AF_INET`; formats string and resolves | Pointer to `hostent`, or `NULL` (`h_errno = HOST_NOT_FOUND`) |
| `gethostbyaddr()` | IPv6 address | Length 16, family `AF_INET6`; rejected | `NULL` (`h_errno = HOST_NOT_FOUND`) |
| `socket()` | `AF_INET, SOCK_STREAM/DGRAM` | Maps to kernel `usock_new(AF_INET, ...)` | Valid socket file descriptor |
| `socket()` | `AF_INET6, SOCK_STREAM/DGRAM`| Rejected in `kernel/usock.c:136` (`family != AF_INET`) | `-1`, `errno = EAFNOSUPPORT` |

---

## 4. Architectural Boundaries and Living Checklist Proposal

### Documented Current Boundaries
1. **DNS TCP Fallback (Implemented)**:
   - RFC 1035 section 4.2.2 defines TCP fallback when responses have the truncation flag (`TC`) set.
   - In `net/dns.c`, incoming packets with the TC flag set now set `q->tc = 1`, and `resolve()` initiates a TCP connection to the DNS server port 53 (via `tcp_connect` or `tcp_connect6`), transmitting the 2-byte length prefixed question and parsing the TCP answer.
2. **SDK IPv6 Socket Separation**:
   - The Banana SDK `banana_api_t` table currently exports `resolve(name, ip4)`.
   - `kernel/usock.c` explicitly enforces `family == AF_INET`.
   - Applications ported via the SDK (such as Java or web clients) rely on `socket(AF_INET6, ...)` returning `EAFNOSUPPORT` to cleanly fall back to IPv4 networking.
3. **Checklist Evolution**:
   - A dedicated task `base-17-dns-sockets` is proposed in `docs/daily-use-checklist.json` to track future enhancements (DNS TCP fallback, AAAA resolution in SDK `banana_api`, and IPv6 `usock` support) without expanding the scope of the current base-17 step 2 deliverables.

---

## 5. Verification Commands

Run the host characterization test suite with AddressSanitizer and UndefinedBehaviorSanitizer:
```bash
python3 tools/test_dns_socket_state.py --sanitize
```
In Banana OS (when running QEMU or hardware):
```bash
selftest ipv6
selftest posix sockets
```
