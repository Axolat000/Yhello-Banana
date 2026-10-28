# SDL2 porting scope and acceptance checks

This document proposes an incremental qualification process for SDL2 on Banana
OS. It does not claim that a complete SDL port or a third-party game already
works. SDL2 and SDL3 have different APIs: a target application's actual major
version must determine which library is ported.

## Keep the first contribution optional

Use a dedicated directory under `ports/`. Build libraries and test packages
only on explicit request. Do not add them to the default kernel, ISO, boot
sequence or SDK link command. An application that does not opt in must retain
the same source inputs, ABI and linked libraries.

Prefer the maintained SDL sources with a Banana backend for a full port. Pin
the source revision or release, verify its digest, preserve its license and
keep the platform patch separate. A locally reimplemented subset must instead
identify itself as experimental, enumerate supported entry points, and avoid
claiming source or binary compatibility with the complete SDL distribution.

## Map capabilities before implementing them

- **Software video:** the SDK's `bwin_open`, pixel buffer, update and resize
  event provide a first target. Reacquire pixels after user-driven resize.
  A resize-grip API does not establish programmatic resize, hide/show or
  fullscreen support; do not advertise those capabilities without a backend.
- **Events:** distinguish key presses, physical key state and text input.
  Missing key releases must not be replaced by an arbitrary timer that releases
  a key while it is held. Translate mouse button numbering explicitly.
- **Timing:** elapsed-time counters use a monotonic source, independently of
  date/time adjustments. A cast of a wrapping 32-bit counter is not a working
  64-bit timer. Test wrap boundaries and explain SDK-clock limitations.
- **Audio:** negotiate formats/channels honestly, bound queue arithmetic and
  callback buffers, define capture support, and synchronize pause/close with
  the mixer thread. Unsupported requests must fail explicitly.
- **Threads:** demonstrate join/detach lifetime and destructor behavior before
  exposing those APIs. Never free a detached thread record still in use.
- **OpenGL:** the existing SDK `bgl3`/Mesa path is an optional 64-bit consumer.
  Verify context ownership, current-context changes and resize using the real
  context type. Software Mesa rendering is not proof of GPU acceleration.

No kernel or SDK ABI extension should be imported as an incidental dependency.
Propose a narrowly justified change separately when the SDK lacks an essential
operation, keeping older callers compatible.

## Acceptance gates

1. Build every supported architecture against the current SDK. Link a complete
   `.bpk`, not only an archive with unresolved symbols. State which architectures
   actually ran in a guest.
2. Test invalid/oversized dimensions, allocation failures, clipping, overlapping
   blits, borrowed framebuffer ownership and balanced window cleanup. Use host
   ASan/UBSan where practical, with deterministic backend stubs.
3. Boot an unmodified official ISO. Install only the optional test package on a
   disposable virtual disk. Record ISO digest, source revision, firmware and
   serial command results. Verify that the shell remains usable after the app exits.
4. Exercise user resize, focus changes, held/released keys and mouse capture
   interactively when those features are claimed. A headless create/destroy loop
   alone does not qualify physical input or prove the absence of all leaks.
5. Before claiming game compatibility, choose and pin one real SDL application,
   build its sources and dependencies, then verify graphics, input, sound and
   shutdown in a recorded session. A synthetic demo is a foundation, not a substitute.

Keep existing boot, application and filesystem checks enabled. If all changes
remain under an opt-in port directory, make that isolation visible in the PR;
do not silently modify default image contents to make a demonstration work.

## Contract references

- [SDL2 surface creation and pixel masks](https://wiki.libsdl.org/SDL2/SDL_CreateRGBSurface)
- [SDL2 clipping and destination rectangles](https://wiki.libsdl.org/SDL2/SDL_BlitSurface)
- [SDL2 64-bit timer semantics](https://wiki.libsdl.org/SDL2/SDL_GetTicks64)
- Banana SDK: `sdk/include/banana.h`, `sdk/include/banana_api.h`,
  `sdk/include/GL/gl3.h` and `sdk/banana.mk`.
