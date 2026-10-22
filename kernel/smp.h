#ifndef SMP_H
#define SMP_H

#include "types.h"

/*
 * Processor cores. The boot core runs the kernel; the others (once
 * started by smp_init(), 64-bit kernel) run app code - see kernel/task.h.
 */

#define SMP_MAX_CPUS 64
#define SMP_VEC_KICK 0xF0          /* IPI: look at your work (kernel/task.c, task_ipi) */
#define SMP_VEC_TIMER 0xF1         /* the boot core's local APIC timer: the 1 kHz tick */
#define SMP_VEC_SPURIOUS 0xFF

void smp_init(void);
/* cores in use (1 until the others are started) */
int  cpu_count(void);
/* the core this runs on: 0 = the boot core */
static inline int cpu_id(void) {
    uint16_t tr;
    __asm__ volatile("str %0" : "=r"(tr));
    return tr <= 0x10 ? 0 : (int)(tr - 0x10) / 16;   /* each core's TSS (kernel/idt.c) */
}
/* an interrupt to another core (vector SMP_VEC_KICK) */
void smp_send_ipi(int cpu, int vector);
/* Best-effort, bounded stop notification; peers halt in the interrupt path. */
void smp_panic_stop_others(void);
/* end of a local-APIC interrupt (IPIs) */
void smp_eoi(void);
/* TLB shootdown (kernel/zswap.c): the page tables each core runs on (set
 * before it loads them), and every other core on cr3 told to reload them -
 * 0 once they all did, -1 if one did not within timeout_ms */
extern volatile uintptr_t smp_cur_cr3[SMP_MAX_CPUS];
int  smp_tlb_shootdown(uintptr_t cr3, uint32_t timeout_ms);
void smp_tlb_service(void);              /* (the KICK interrupt: kernel/idt.c) */
/* the cores found in the ACPI tables (including ones not started) */
int  smp_cores_found(void);

/* Where each logical processor sits: package, physical core, and hardware
 * thread on that core (Hyper-Threading / SMT: two or more logical
 * processors share one core's execution units), from CPUID leaves 0x1F /
 * 0xB (or 1 and 4 on older processors) run on that processor itself. */
#define CPU_TYPE_PLAIN 0
#define CPU_TYPE_PERF  1            /* hybrid processors: a performance core (Intel P-core) */
#define CPU_TYPE_EFF   2            /* ... an efficient core (E-core) */
typedef struct {
    uint32_t apic;                  /* its (x2)APIC id */
    uint16_t pkg, core;             /* package, core within it */
    uint8_t  smt;                   /* hardware thread within the core */
    uint8_t  type;                  /* CPU_TYPE_* */
    int      phys;                  /* the physical core it is on: 0, 1, ... (shared by siblings) */
} cpu_topo_t;
const cpu_topo_t* cpu_topo(int cpu);
int  smp_phys_cores(void);          /* physical cores among the running processors */
int  smp_threads_per_core(void);    /* the most hardware threads one core runs (2 with Hyper-Threading) */
int  smp_x2apic(void);              /* the local APICs run in x2APIC mode */

/* TSC cross-core synchronization and qualification state */
typedef struct {
    int     qualified;              /* 1: synchronized and within tolerance; 0: unverified or failed */
    int64_t offset;                 /* cycles to add to this core's TSC to match BSP */
    uint64_t rtt_min;               /* lowest round-trip time recorded during qualification */
    int     error_code;             /* 0: OK, 1: timeout, 2: RTT jitter / threshold exceeded */
} smp_tsc_sync_t;

int64_t smp_tsc_core_offset(int cpu);
const smp_tsc_sync_t* smp_tsc_status(int cpu);

/* an ACPI table by its 4-letter signature ("FACP", "APIC"...), NULL if none;
 * one at a physical address, if it has that signature (kernel/acpi.c) */
const void* acpi_find_table(const char* sig);
const void* acpi_find_table_n(const char* sig, int n);   /* the n-th one (SSDTs) */
const void* acpi_table_at(uint64_t addr, const char* sig);

#endif
