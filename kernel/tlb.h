/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TLB_H
#define TLB_H

#include "types.h"

/*
 * What the processor remembers of the page tables: its TLB (kernel/tlb.c).
 *
 * Loading CR3 used to forget everything - the kernel's translations too,
 * which are the same in every address space - and the boot core loads CR3
 * whenever it goes from an app to a kernel task and back, the other cores
 * before every task. Now:
 *
 *  - the kernel's pages are global (the G bit, CR4.PGE): loading CR3 keeps
 *    them. When the kernel's tables change, every core forgets everything
 *    (this one at once, the others before their next task - as before);
 *  - where the processor has PCIDs, each core keeps the translations of the
 *    last address spaces it ran, each under a PCID of its own, and loads
 *    CR3 without forgetting them - unless the space changed since. Every
 *    change that takes rights away counts up the space's generation
 *    (tlb_space_changed); a core that ran it at an older generation forgets
 *    its translations of it when it loads it again.
 *
 * The 32-bit kernel runs without paging: these do nothing there.
 */

typedef struct tlb_tag {
    uint64_t          id;               /* never used twice */
    volatile uint32_t gen;              /* changes that took rights away */
} tlb_tag_t;

/* on every core (and after a wake from sleep): global pages, PCIDs */
void tlb_cpu_init(void);
/* a new address space's tag */
void tlb_tag_init(tlb_tag_t* t);
/* its tables lost rights: a core that kept translations of it forgets them
 * before it runs it again (one running it now: smp_tlb_shootdown) */
void tlb_space_changed(tlb_tag_t* t);
/* the kernel's tables changed: this core forgets everything now, the
 * others before their next task. others_only: this core already did */
void tlb_kernel_changed(int others_only);
/* this core forgets everything, global pages too */
void tlb_flush_all(void);
/* loads cr3 on this core: tag NULL for the kernel's own tables */
void tlb_load(uintptr_t cr3, const tlb_tag_t* tag);

int  tlb_has_global(void);
int  tlb_has_pcid(void);

typedef struct {
    uint64_t loads;                     /* address spaces loaded (task switches) */
    uint64_t kept;                      /* ... keeping what the TLB held of it */
    uint64_t forgot;                    /* ... forgetting it: new to the core, or changed since */
    uint64_t full;                      /* every translation forgotten (kernel changes) */
} tlb_stats_t;
void tlb_stats(tlb_stats_t* s);         /* every core's, added up */

/* selftest tlb: a space of its own with a page whose frame is changed
 * under it; 0 if every check passed, else -1 and what failed in msg.
 * *old_ns / *new_ns: one switch there and back, touching 64 pages on each
 * side, forgetting everything at each switch (as before) / now (0: no
 * clock). *kept_after: 1 if a change not told (tlb_space_changed) was
 * still unseen after switching away and back - the PCID kept it */
int  tlb_selftest(char* msg, int cap, uint32_t* old_ns, uint32_t* new_ns, int* kept_after);

#endif
