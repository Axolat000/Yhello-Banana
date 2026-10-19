/* SPDX-License-Identifier: BSD-2-Clause */
/* Mesa 24's C11 emulation predates glibc's C23 call_once declarations.
 * Keep the reference host's unused declarations in a separate namespace. */
#ifndef BANANA_MESA_HOST_COMPAT_H
#define BANANA_MESA_HOST_COMPAT_H
#if !defined(__BANANA_OS__) && !defined(__ASSEMBLER__)
#define once_flag mesa_host_once_flag
#define call_once mesa_host_call_once
#include <stdlib.h>
#undef once_flag
#undef call_once
#undef ONCE_FLAG_INIT
#endif
#endif
