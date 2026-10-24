# Ladybird LibJS port prototype

This experiment evaluates replacing BananaScript's JavaScript implementation
inside the restricted web worker. It does not change the browser's active
engine or add Ladybird to the normal ISO.

## Reference milestone

The standalone reference runs 17 semantic probes: arithmetic, Map/Set,
WeakMap identity, closures/default arguments, classes/super, Proxy/Reflect,
BigInt, Unicode regular expressions, JSON, optional chaining, descriptors,
Intl number formatting, Promise jobs, async/await and allocation/collection.
These are focused port regressions, not an ECMAScript conformance score or
proof that Google/CAPTCHA works.

The source baseline is Ladybird commit
[e6669482e693bda4f75cd8a90b2abd292c0f3774](https://github.com/LadybirdBrowser/ladybird/tree/e6669482e693bda4f75cd8a90b2abd292c0f3774)
(20 February 2026), before the Rust migration. Current Ladybird master uses
a Rust runtime; this C++ baseline is an experimental bootstrap, not a claim
to ship current Ladybird or its latest fixes.

The sources.json manifest pins the source archives and public suffix data by
SHA-256. Preparation verifies downloads before extracting and publishes complete
trees atomically. Existing unverified trees are rejected. Upstream copyright
and license files remain in the source trees.

The reference uses ICU 76.1 as supplied by Debian 13 and tests the APIs used
by this snapshot against it. Upstream specifies ICU 78. The probe includes
Intl formatting; broader locale and Temporal compatibility remains to test.

## Build the reference on Debian 13

Install the host prerequisites:

    sudo apt-get install g++ cmake ninja-build python3 git ca-certificates \
        pkg-config libicu-dev libssl-dev libtommath-dev libfast-float-dev

Then:

    JOBS=2 bash ports/libjs/build.sh

The script builds the pinned simdutf and simdjson dependencies, a minimal
selection of Ladybird libraries, and executes:

    ports/libjs/build/native/libjs-probe
    ports/libjs/build/native/libjs-probe --eval 'new Set([2]).has(2)'

LADYBIRD_SOURCE, LIBJS_PREFIX and LIBJS_NATIVE_BUILD can reuse a reference
source/prefix/build directory. LIBJS_DOWNLOAD_CACHE reuses verified archives.
Build outputs and downloaded sources are ignored by Git.

The prototype uses GCC 14 with C++23, RTTI and no C++ exceptions. Two upstream
enum-switch diagnostics are warnings for this compiler. The SDK stack-bounds
adaptation preserves the actual bounds returned by pthread_getattr_np instead
of applying Linux/musl's stack-growth adjustment.

## Remaining integration

The SDK build, optional isolated probe package and VM checks are under
development. Before connecting a page, the port must execute inside Banana OS
with the existing syscall restrictions, shared memory quota and CPU budgets.
The C facade is deliberately small; DOM bindings, persistent realms, host
objects and resource access through the existing IPC broker are separate
integration work. Full browser compatibility requires those APIs as well as
the JavaScript engine.
