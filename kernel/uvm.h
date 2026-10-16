#ifndef UVM_H
#define UVM_H

#include "types.h"
#include "uspace.h"
#include "umem.h"

/*
 * Memory an app maps itself (mmap, 64-bit kernel): a region of virtual
 * addresses of its own, UVM_BASE .. UVM_BASE + UVM_SIZE, outside the
 * identity map of kernel/uspace.c. A mapping only reserves addresses; a
 * page gets memory (zeroed, from the app's own blocks: kernel/umem.c) the
 * first time it is touched, by the app or by a system call working for it.
 * So a program can reserve a big range (a Java heap) and use a little of
 * it. Each range has its protection - none, read, read + write (execute
 * goes with read: the kernel does not turn on NX) - and touching a page
 * against it faults like any bad access.
 *
 * The 32-bit kernel has no paging: kernel/app.c hands out ordinary
 * (zeroed) app memory there, without reservations or protection.
 */

#define UVM_BASE  0x0000640000000000ull     /* PML4 slots 200..203: 100 TiB */
#define UVM_SLOTS 4
#define UVM_SIZE  ((uint64_t)UVM_SLOTS << 39)  /* 2 TiB */

#define UVM_READ   1
#define UVM_WRITE  2
#define UVM_EXEC   4
#define UVM_FIXED  1                         /* at addr exactly, replacing what is there */

/* maps len bytes (rounded up to pages) at addr (a hint, or exactly with
 * UVM_FIXED): the address, or 0 */
uintptr_t uvm_map(uspace_t* us, umem_t* m, uintptr_t addr, uint64_t len, int prot, int flags);
/* unmaps [addr, addr + len): its pages go back to the app's memory; 0, -1 */
int       uvm_unmap(uspace_t* us, umem_t* m, uintptr_t addr, uint64_t len);
/* new protection for [addr, addr + len), which must be mapped: 0, -1 */
int       uvm_protect(uspace_t* us, uintptr_t addr, uint64_t len, int prot);
/* 1 if [p, p + n) is mapped and readable (a system call may use it) */
int       uvm_owns(uspace_t* us, const void* p, uint64_t n);
int       uvm_owns_str(uspace_t* us, const char* s);
/* a page fault at addr (err: the CPU's error code): 1 if a page was put
 * there (or it was a stale translation) and the access can go on */
int       uvm_fault(uspace_t* us, umem_t* m, uintptr_t addr, uint32_t err);
/* bytes of memory its mapped pages use now */
uint64_t  uvm_resident(uspace_t* us);

#endif
