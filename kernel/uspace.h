#ifndef USPACE_H
#define USPACE_H

#include "types.h"

/*
 * Address spaces of apps (64-bit kernel). An app runs in ring 3 on page
 * tables of its own: a copy of the kernel's identity map (physical address
 * == virtual address, so a pointer means the same thing to the app and to
 * the kernel) in which only the app's memory is open to user mode - its
 * 2 MiB blocks (kernel/umem.c), anywhere in the first 512 GiB, mapped with
 * 4 KiB pages so pages can be left out (guard pages under stacks). It has
 * page directories of its own only for the gigabytes it has memory in; the
 * rest are the kernel's, shared. Everything else - the kernel, other
 * apps, devices - is there for the kernel only: the app faults if it
 * touches it.
 *
 * The 32-bit kernel runs without paging: these do nothing there and apps
 * stay in ring 0.
 */

typedef struct uspace uspace_t;

uspace_t* uspace_create(void);                  /* NULL if out of memory */
/* frees its tables; nothing may run on it any more (CR3 elsewhere) */
void      uspace_destroy(uspace_t* us);
/* what CR3 is loaded with to run on it, and what the TLBs keep of it (kernel/tlb.c) */
uintptr_t uspace_cr3(uspace_t* us);
struct tlb_tag* uspace_tag(uspace_t* us);
/* opens [addr, addr + size) to the app: 2 MiB-aligned, whole 2 MiB blocks; 0 or -1 */
int       uspace_map(uspace_t* us, uintptr_t addr, uint32_t size);
/* makes 4 KiB pages of its memory fault (guard = 1) or usable again (0) */
void      uspace_guard(uspace_t* us, uintptr_t addr, uint32_t size, int guard);

/* kernel/paging.c, after it changed the kernel's tables: every address
 * space takes the change */
void      uspace_kernel_changed(void);

/* a page fault from user mode at addr that the tables in use allow (a
 * stale translation another processor kept from before the block was the
 * app's): 1 if the access can simply be tried again */
int       uspace_fault_is_stale(uintptr_t addr, uint32_t err);
/* how many there were since boot (selftest tlb: the kernel's global pages
 * are such translations too, kept across CR3 loads - kernel/tlb.c) */
uint32_t  uspace_stale_faults(void);

#endif
