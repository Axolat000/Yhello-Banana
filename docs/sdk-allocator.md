# Process-local SDK allocation

This adapts the goal of pinned upstream commit
`46446439da13f6664de67d9314b212544559a0b3`, rather than importing its allocator.
The rejected candidate used 1 GiB regions in 2 GiB reservations, size classes,
thread caches, executable mapping requests, and unchecked `MAP_FIXED`
reclamation. The replacement uses first-fit blocks in small arenas, checked
arithmetic, split/coalesce, and one statically initialized heap mutex. Capability
detection and all allocator metadata are protected by that mutex. There are no
TLS hooks, thread caches, or fixed-address remappings.

## Compatibility and VM contract

On x86_64 with API version 16 or later, a sufficiently large API table and
non-null `vm_map`/`vm_unmap` callbacks enable the local heap. Other systems,
including i686, retain kernel allocation and the existing aligned-pointer table.
Kernel-owned pointers (including HTTP bodies and decoded images) still go to the
kernel's free/realloc callbacks. Aligned allocation retains the SDK's existing
`posix_memalign`, `aligned_alloc`, and `memalign` semantics.

The SDK now depends on the existing address partition: kernel-owned allocations
are below `BANANA_VM_BASE`; non-fixed anonymous mappings are page-aligned and at
or above it. `kernel/app.c` asserts that the SDK boundary equals `UVM_BASE`.
The API table layout and version are unchanged. Previously built applications
continue to use their linked allocator; rebuild applications and bundled SDK
archives to use this implementation. There is no disk-format, durability, or
license change and no new external dependency (the repository's BSD-2-Clause
license applies).

Mappings request READ | WRITE, never EXEC. This is not an NX guarantee:
`kernel/uspace.c:prot_bits` does not currently enforce NX. `vm_map` returning NULL
must leave existing mappings intact; the allocator never requests MAP_FIXED.
Only a whole, empty arena can be unmapped. A failed `vm_unmap` leaves its arena
linked, counted, and reusable. This depends on the current `uvm_unmap` contract:
`vma_cut` checks allocation failure before mutation, and page release occurs
only on success. Neither syscall precommits physical pages: demand-fault OOM is
not a recoverable malloc failure guarantee.

Like the previous allocator, these functions are not async-signal-safe. Thread
return and `pthread_exit` need no allocator-specific cleanup. Cancellation is
currently unsupported (`pthread_cancel` returns ENOSYS); this change does not
implement it.

## Accounting

`__malloc_stats` snapshots both counters under the heap mutex. `mapped` counts
virtual bytes reserved by the allocator, including metadata, alignment padding,
and retained empty arenas after unmap failure. It is not physical RAM usage.
`inuse` counts usable payload capacity of live local blocks (including rounding),
not requested bytes or kernel-owned buffers. Fallback counters are zero.

`app_snapshot().mem` and `pkg ps` count kernel-owned allocation bytes plus
resident anonymous mapping pages, saturating at UINT32_MAX because the existing
ABI field is 32-bit. Resident pages are sampled under the VM lock. These include
heap and other mappings, including thread stacks; they exclude untouched virtual
reservations, compressed/swapped-out pages, page tables and other kernel process
overhead. The sum is an observational sample, not an atomic whole-process RSS.

## Verification and remaining qualification

Run `python3 tools/test_malloc.py` for deterministic random data preservation,
zero/overflow/large-alignment cases, simultaneous first use, cross-thread frees,
map/unmap failure injection, accounting, kernel ownership, and old/truncated/
missing-callback API fallback. The harness builds the real SDK malloc and posix
sources but uses Linux mmap and threads. It does not prove Banana paging or
thread-lifecycle correctness.

Build `sdk/examples/systest` and run `systest heap` in Banana for actual pthread
startup, destructor allocation/free, normal return and pthread_exit, the existing
ENOSYS cancellation contract, aligned realloc preservation, kernel-owned buffers,
and decoded-image ownership. `selftest swap` exercises the existing VM swap
path; it does not by itself prove allocator performance under pressure.

`systest mem N high` deliberately uses `banana_api()->malloc/free` for its
payloads. Those kernel umem allocations are identity mapped, so their addresses
can test physical RAM above 4 GiB. Ordinary `systest mem N` and `systest heap`
exercise the SDK heap separately: its virtual addresses do not identify physical
backing. The low-memory regression must explicitly reject `systest mem 4 high`
on a 512 MiB VM, while `selftest memory` skips unavailable high RAM and checks
ordinary allocation preservation.

The linked planning proposal `base-18-sdk-heap-evaluation` preserves the existing
base-18 history. It requires independent review and controller validation before
scheduling. Full memory-pressure/swap/teardown qualification, HTTP ownership,
before/after syscall/throughput/resident-memory measurements and desktop latency
remain explicit qualification work; host tests cannot establish those results.
