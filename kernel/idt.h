/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IDT_H
#define IDT_H

#include "types.h"

/* Installs handlers for the 32 CPU exception vectors so a fault produces
 * a readable panic screen instead of an unhandled triple-fault reset,
 * and remaps the 8259 PICs to vectors 32-47 with every IRQ masked. */
void idt_init(void);
/* the panic screen for a kernel bug found by a check (not a CPU exception) */
void kernel_panic(const char* what) __attribute__((noreturn));

/* Registers `handler` for hardware IRQ line `irq` (0-15) and unmasks it.
 * Handlers run with interrupts off and must be short: the EOI is sent by
 * the common stub after they return. Interrupts stay globally disabled
 * until the kernel executes `sti` (kernel_main, after timer_init). */
void irq_install(int irq, void (*handler)(void));

/* another processor core, starting (kernel/smp.c, 64-bit): the kernel's
 * GDT with that core's own TSS, and the IDT */
void idt_ap_init(int cpu);
void idt_s3_resume(void); /* retain gates/handlers/TSS, restore CPU and PIC state */
/* 64-bit: the timer (IRQ 0's handlers) runs from the local APIC timer (1) or the PIT (0) */
void irq_timer_from_lapic(int on);
/* before a restart (in a VM): the local APIC as a BIOS expects it */
void lapic_before_reset(void);
/* the firmware handed over with the local APIC in x2APIC mode / locked in it */
int  lapic_boot_x2apic(void);
int  lapic_x2apic_locked(void);

/* 64-bit: apps run in ring 3 (kernel/app.c). An interrupt or a system call
 * from there switches to the stack in core cpu's TSS - the running task's
 * kernel stack for it (kernel/task.c sets it on every switch) */
void tss_set_rsp0(int cpu, uintptr_t rsp0);
/* the system call vector: int 0x80 from ring 3 (kernel/isr64.asm) */
#define SYSCALL_VECTOR 0x80
/* 64-bit: the processor has the syscall instruction (set up on every core:
 * the apps' call stubs use it - faster than int 0x80, which stays too) */
int idt_has_syscall(void);

#endif
