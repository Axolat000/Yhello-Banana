/* SPDX-License-Identifier: BSD-2-Clause */
/* Banana OS app startup: the system calls _banana_start() with its call
 * table; it sets up the C library and runs main(). */
#include "banana_api.h"

const banana_api_t* __banana;
extern unsigned long __banana_stack_top;   /* (pthread.c: the main thread's stack) */

int  main(int argc, char** argv);
extern char** environ;
void __libc_init(void);
void exit(int code) __attribute__((noreturn));

__attribute__((used, visibility("default")))
int _banana_start(const banana_api_t* api, int argc, char** argv) {
    if (!api || api->magic != BANANA_API_MAGIC || api->version < 1) return 127;
    __banana = api;
    /* version 26: the environment follows argv's NULL (execve's envp, or the
     * system's default) */
    if (BANANA_HAS(api, proc_kill) && argv && !argv[argc] && argv[argc + 1]) environ = argv + argc + 1;
    __banana_stack_top = (unsigned long)__builtin_frame_address(0);
    __libc_init();
    exit(main(argc, argv));
}
