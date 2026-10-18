# LLVM JIT runtime for Banana OS

This is the optional LLVM 18.1.8 X86 runtime used by Mesa 24.3.4 LLVMpipe.
It is a 64-bit SDK library, not an installed compiler or a Linux binary.

Build everything through Mesa:

```sh
JOBS=6 LLVMPIPE=1 ports/mesa/build.sh
DISK=ports/mesa/build-llvmpipe/gl3disk.img MEM=2G KVM=1 tools/dev.sh run
```

Inside Banana OS:

```text
selftest llvmpipe
```

Source preparation verifies LLVM and its companion CMake archive separately,
extracts into a temporary directory, and publishes only a complete tree.
Interrupted extractions recover on the next run; the previous tree is kept
in a `.previous-*` directory. Incomplete downloads never replace the cache.
`ports/llvm/build.sh configure` prepares sources and checks CMake configuration
without compiling LLVM. An explicit `LLVM_SRC` must point to a complete tree
with the matching `../cmake/Modules` directory.

The first build downloads verified LLVM and CMake source archives, builds
native LLVM/tablegen/llvm-config and a native Mesa reference, then rebuilds
the required LLVM/Mesa objects against the SDK. It takes considerably longer
and needs more disk space than the default softpipe/VirGL build. Dependencies
include CMake, Ninja, GCC/G++, static libstdc++, and Mesa's dependencies listed
in `ports/mesa/build.sh`. `JOBS` controls parallelism; reduce it on low RAM hosts.

`LLVM_BUILD`, `LLVM_SRC`, and `LLVM_REF` can reuse an existing output, source,
or native reference build of this exact version. Native and target objects
are separate. `KEEP=0` forces a target rebuild; normal builds check source
mtime, compiler arguments, compiler version and header contents.

The native Mesa imports select a static LLVM object closure (currently 190
entry points and 1156 objects). The target library uses SDK allocation,
threads, TLS, executable mappings, file I/O and clocks. GNU C++ headers and
static libstdc++ supply container/stream implementation code. A link-only
symbol provider prevents pulling in a second C allocator/runtime; its single
DT_NEEDED entry is validated and removed. The provider is never deployed.
Both output DSOs undergo a strong-import audit against SDK providers.

Private compatibility code supplies the C/UTF-8 locale ABI required by GNU
stream facets. It does not add locale support to the OS. Message catalogs
return the original text, unsupported process spawning and iconv report
errors, and unwinding through JIT code is unsupported. LLVM and target C++
code are built without exceptions or RTTI. This is a renderer runtime, not a
general C++ exception runtime or a complete LLVM distribution.

`selftest llvmpipe` requires the actual renderer and checks narrow/wide C++
streams, restartable UTF-8 conversion, GLSL shaders, triangle colors, an FBO
and readback. The SDK's integer constants now match the compiler's integer
types, and `mbstate_t` has GNU's eight-byte stream ABI. Conversion state uses
only its first word, retaining compatibility with older four-byte C clients.
The host test `tools/tests/test_sdk_utf8.c` checks all Unicode scalar values,
each byte split, invalid encodings and resetting state against the SDK module.

Upstream design references: [Mesa LLVMpipe](https://docs.mesa3d.org/drivers/llvmpipe.html),
[LLVM MCJIT](https://llvm.org/docs/MCJITDesignAndImplementation.html).
LLVM source is Apache-2.0 with LLVM exceptions; static GCC runtime code is
covered by its accompanying license and GCC Runtime Library Exception.
