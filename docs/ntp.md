# Native SNTP client

`ntp [server]` queries UTC without adjusting the clock. `ntp --set [server]`
explicitly steps UTC using rtc_set_utc; it does not change the monotonic clock.
Default server: pool.ntp.org, UDP 123, IPv4. The six-second total deadline
includes network configuration, DNS and up to three attempts.

Replies must match server address, UDP source port and the current request's
eight-byte originate cookie. Server mode, version, leap, stratum, timestamps
and processing interval are validated. Outdated/malformed replies are ignored.
The 2036 NTP era wrap is unfolded into the RTC's supported 2000..2099 interval.

The local RTC API has whole-second resolution, so the offset is an estimate;
the random fractional request timestamp is only a correlation cookie. This
is unauthenticated network time, not NTS, a precision clock discipline or an
automatic background service. No gradual slewing or trusted-time claim is made.
Host tests exercise the real UDP callback and query flow with fake DNS, UTC
and monotonic clocks; Internet-server and hardware RTC qualification remains.
