# Graphics work

Implementation order: diagnostics, damage tracking, asynchronous commands,
frame presentation, VirGL, LLVMpipe. Hardware 3D and LLVMpipe are separate
ports; the existing GL renderers run on the CPU.

## 1. Diagnostics

Run `selftest gpu` for offscreen pixel, clipping, stride, overflow and live
framebuffer checks. Run `gpubench` for CPU memory-copy bandwidth and pixel
verification. It does not measure GPU rendering speed or display latency.
`lsgpu` also reports presentation bytes, copy time, driver updates, command
counts and timeouts. Counters are cumulative since boot.

Hosted verification: `cc -O2 tools/tests/test_gpu_surface.c kernel/gpu_surface.c -o /tmp/gpu-surface && /tmp/gpu-surface`.
Both kernel architectures build. The seven in-OS checks passed with QEMU
standard VGA, virtio-vga and VMware SVGA. `gpubench` checked pixels and the
shell help registry reported no missing pages or duplicate commands.

## 2. Damage tracking

The desktop compares 32x32 tiles against its previous frame in RAM, joins
adjacent changed tiles and copies only changed runs. Comparisons are exact;
there is no hash-collision risk and no video-memory readback. The shadow
is invalidated when the framebuffer or backbuffer changes. Allocation
failure falls back to the full copy. Software cursor erasure is explicit.
Virtual GPUs coalesce up to 32 rectangles and flush them on demand; direct
console drawing and the splash mark their writes. The old unconditional
full-screen refresh every 100 ms is gone.

`selftest gpu` adds unchanged-frame, edge-tile and separated-change checks.
The hosted checks pass with address/undefined-behavior sanitizers. Both
kernels link. A virtio desktop passes all checks, changes resolution,
returns to the console and reports no GPU timeouts. In one run 5 MiB
changed out of 74 MiB considered; actual savings depend on the workload.

## Remaining stages

- Asynchronous commands: pending.
- Frame presentation: pending.
- VirGL: pending.
- LLVMpipe: pending.
