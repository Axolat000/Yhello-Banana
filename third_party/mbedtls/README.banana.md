# Mbed TLS source provenance

Version: Mbed TLS 3.6.7 (maintained 3.6 LTS series).
Release: https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7
Archive: https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
SHA-256: `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6`

Imported verbatim: `include/`, `library/`, `LICENSE`, `ChangeLog`, `SECURITY.md` from
that release archive. No upstream patches. Banana selects Apache-2.0
from the upstream Apache-2.0 OR GPL-2.0-or-later dual license; the complete
license text and source notices are retained. This does not change the
license of other Banana sources.

Configuration and freestanding shims live in `ports/mbedtls/`; they do not
change the upstream default configuration. The source contains optional
features that Banana does not enable.

For an update: select a maintained release, read its security advisories
and configuration/API migration notes, download its official source archive,
record and verify its SHA-256, replace these imported paths, and review the
source diff and license notices. Run both kernel builds and the host port
regressions, then the authenticated TLS handshake matrix from base-01 step 2
before releasing an HTTPS consumer. Do not update only headers or mix releases.
