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

## Remaining stages

- Damage tracking: pending.
- Asynchronous commands: pending.
- Frame presentation: pending.
- VirGL: pending.
- LLVMpipe: pending.
