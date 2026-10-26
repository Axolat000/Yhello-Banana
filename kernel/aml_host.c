/* SPDX-License-Identifier: GPL-3.0-or-later */
/* uACPI kernel bridge. Kernel tasks execute only on the BSP, so IRQ masking
 * provides short spinlock critical sections; blocking locks yield/sleep. */
#include "aml_host.h"
#include "smp.h"
#include "pci.h"
#include "paging.h"
#include "task.h"
#include "timer.h"
#include "io.h"
#include "idt.h"
#include "kheap.h"
#include "kstring.h"
#include "serial.h"
#include <uacpi/uacpi.h>
__asm__(".pushsection .rodata\n"
        ".global uacpi_notice_start\nuacpi_notice_start:\n"
        ".incbin \"third_party/uacpi/LICENSE\"\n"
        ".global uacpi_notice_end\nuacpi_notice_end:\n.popsection\n");
#define AML_HEAP_MAX (32u<<20)
static uint32_t aml_bytes;
typedef struct { uint32_t bytes,pad[3]; } allocation_t;
typedef struct { uint16_t base;uint32_t length; } port_t;
typedef struct { uint8_t bus,dev,fn; } pci_handle_t;
typedef struct { uint32_t count; } semaphore_t;
typedef struct work {
    struct work* next;
    uacpi_work_handler fn;
    uacpi_handle ctx;
    int pending;
} work_t;
static work_t *work_head,*work_tail;
static unsigned active_work;
typedef struct { uacpi_interrupt_handler fn;void* ctx;int installed; } aml_irq_t;
static aml_irq_t sci;
static void sci_trampoline(void) { if(sci.fn) sci.fn(sci.ctx); }
uint32_t aml_host_bytes(void) { return aml_bytes; }
uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out) {
    *out=acpi_rsdp_address();
    return *out?UACPI_STATUS_OK:UACPI_STATUS_NOT_FOUND;
}
void* uacpi_kernel_map(uacpi_phys_addr address,uacpi_size len) {
    if(address<4096 || !len || (uint64_t)len>0xffffffffffffffffull-address)
        return UACPI_MAP_FAILED;
    if(!mmio_map(address,len)) return UACPI_MAP_FAILED;
    return (void*)(uintptr_t)address;
}
void uacpi_kernel_unmap(void* addr,uacpi_size len) { (void)addr;(void)len; /* permanent identity maps */ }
void uacpi_kernel_log(uacpi_log_level lvl,const uacpi_char* msg) {
    if(lvl<=UACPI_LOG_WARN) klog("aml: %s",msg);
}
void* uacpi_kernel_alloc(uacpi_size size) {
    if(!size || size>AML_HEAP_MAX || size>AML_HEAP_MAX-aml_bytes) return NULL;
    allocation_t* h=kmalloc(size+sizeof(*h));
    if(!h) return NULL;
    h->bytes=(uint32_t)size;aml_bytes+=(uint32_t)size;
    return h+1;
}
void uacpi_kernel_free(void* mem) {
    if(!mem) return;
    allocation_t* h=(allocation_t*)mem-1;
    aml_bytes-=h->bytes;kfree(h);
}
uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address a,uacpi_handle* out) {
    if(a.segment || a.device>=32 || a.function>=8) return UACPI_STATUS_NOT_FOUND;
    pci_handle_t* p=uacpi_kernel_alloc(sizeof(*p));if(!p) return UACPI_STATUS_OUT_OF_MEMORY;
    p->bus=a.bus;p->dev=a.device;p->fn=a.function;*out=p;return UACPI_STATUS_OK;
}
void uacpi_kernel_pci_device_close(uacpi_handle h) { uacpi_kernel_free(h); }
#define PCI_CHECK(n) pci_handle_t* p=h;if(!p || off>256-(n) || off%((n))) return UACPI_STATUS_INVALID_ARGUMENT
uacpi_status uacpi_kernel_pci_read8(uacpi_handle h,uacpi_size off,uacpi_u8* out) {
    PCI_CHECK(1);*out=(uint8_t)(pci_read32(p->bus,p->dev,p->fn,(uint8_t)(off&~3u))>>((off&3)*8));return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_pci_read16(uacpi_handle h,uacpi_size off,uacpi_u16* out) {
    PCI_CHECK(2);*out=pci_read16(p->bus,p->dev,p->fn,(uint8_t)off);return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_pci_read32(uacpi_handle h,uacpi_size off,uacpi_u32* out) {
    PCI_CHECK(4);*out=pci_read32(p->bus,p->dev,p->fn,(uint8_t)off);return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_pci_write8(uacpi_handle h,uacpi_size off,uacpi_u8 value) {
    PCI_CHECK(1);
    uint32_t flags=irq_save();
    uint32_t addr=0x80000000u|((uint32_t)p->bus<<16)|((uint32_t)p->dev<<11)|
                  ((uint32_t)p->fn<<8)|((uint32_t)off&~3u);
    outl(0xcf8,addr);outb((uint16_t)(0xcfc+(off&3)),value);
    irq_restore(flags);return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_pci_write16(uacpi_handle h,uacpi_size off,uacpi_u16 value) {
    PCI_CHECK(2);uint32_t flags=irq_save();
    uint32_t addr=0x80000000u|((uint32_t)p->bus<<16)|((uint32_t)p->dev<<11)|
                  ((uint32_t)p->fn<<8)|((uint32_t)off&~3u);
    outl(0xcf8,addr);outw((uint16_t)(0xcfc+(off&3)),value);
    irq_restore(flags);return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle h,uacpi_size off,uacpi_u32 value) {
    PCI_CHECK(4);pci_write32(p->bus,p->dev,p->fn,(uint8_t)off,value);return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_io_map(uacpi_io_addr base,uacpi_size len,uacpi_handle* out) {
    if(base>65535 || !len || len>65536-base) return UACPI_STATUS_INVALID_ARGUMENT;
    port_t* p=uacpi_kernel_alloc(sizeof(*p));if(!p) return UACPI_STATUS_OUT_OF_MEMORY;
    p->base=(uint16_t)base;p->length=(uint32_t)len;*out=p;return UACPI_STATUS_OK;
}
void uacpi_kernel_io_unmap(uacpi_handle h) { uacpi_kernel_free(h); }
#define PORT_CHECK(n) port_t* p=h;if(!p || off>p->length || (n)>p->length-off) return UACPI_STATUS_INVALID_ARGUMENT;uint16_t port=(uint16_t)(p->base+off)
uacpi_status uacpi_kernel_io_read8(uacpi_handle h,uacpi_size off,uacpi_u8* out) { PORT_CHECK(1);*out=inb(port);return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_io_read16(uacpi_handle h,uacpi_size off,uacpi_u16* out) { PORT_CHECK(2);*out=inw(port);return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_io_read32(uacpi_handle h,uacpi_size off,uacpi_u32* out) { PORT_CHECK(4);*out=inl(port);return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_io_write8(uacpi_handle h,uacpi_size off,uacpi_u8 v) { PORT_CHECK(1);outb(port,v);return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_io_write16(uacpi_handle h,uacpi_size off,uacpi_u16 v) { PORT_CHECK(2);outw(port,v);return UACPI_STATUS_OK; }
uacpi_status uacpi_kernel_io_write32(uacpi_handle h,uacpi_size off,uacpi_u32 v) { PORT_CHECK(4);outl(port,v);return UACPI_STATUS_OK; }
uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    uintptr_t flags;__asm__ volatile("pushf; pop %0":"=r"(flags));
    if(flags&0x200) task_maybe_yield();
    return timer_counter();
}
void uacpi_kernel_stall(uacpi_u8 us) {
    /* The PIT interrupt counter cannot advance in a spinlock with IRQs off.
     * Keep even an early/failing host call bounded on that fallback. */
    if(!timer_has_hardware_clock()) { while(us--) (void)inb(0x80);return; }
    uint64_t start=timer_counter();
    while(timer_counter()-start<(uint64_t)us*1000) __asm__ volatile("pause");
}
void uacpi_kernel_sleep(uacpi_u64 ms) {
    while(ms) { uint32_t n=ms>1000?1000:(uint32_t)ms;task_sleep_ms(n);ms-=n; }
}
uacpi_handle uacpi_kernel_create_semaphore(uacpi_u32 units) {
    semaphore_t* s=uacpi_kernel_alloc(sizeof(*s));if(s) s->count=units;return s;
}
void uacpi_kernel_free_semaphore(uacpi_handle h) { uacpi_kernel_free(h); }
uacpi_status uacpi_kernel_wait_for_semaphore(uacpi_handle h,uacpi_u16 timeout) {
    semaphore_t* s=h;if(!s) return UACPI_STATUS_INVALID_ARGUMENT;
    uint32_t start=timer_ms();
    do {
        uint32_t flags=irq_save();
        if(s->count) { s->count--;irq_restore(flags);return UACPI_STATUS_OK; }
        irq_restore(flags);
        if(!timeout || (timeout!=0xffff && (uint32_t)(timer_ms()-start)>=timeout)) break;
        task_sleep_ms(1);
    } while(1);
    return UACPI_STATUS_TIMEOUT;
}
void uacpi_kernel_signal_semaphore(uacpi_handle h) {
    semaphore_t* s=h;if(!s) return;
    uint32_t flags=irq_save();if(s->count!=0xffffffffu) s->count++;irq_restore(flags);
}
uacpi_thread_id uacpi_kernel_get_thread_id(void) { return (void*)(uintptr_t)((unsigned)task_current_pid()+1); }
uacpi_interrupt_state uacpi_kernel_disable_interrupts(void) { return irq_save(); }
void uacpi_kernel_restore_interrupts(uacpi_interrupt_state f) { irq_restore((uint32_t)f); }
uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request* req) {
    (void)req;klog("aml: firmware breakpoint/fatal request declined\n");return UACPI_STATUS_UNIMPLEMENTED;
}
uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq,uacpi_interrupt_handler fn,uacpi_handle ctx,uacpi_handle* out) {
    if(!irq || irq>=16 || sci.fn) return UACPI_STATUS_UNIMPLEMENTED;
    sci.fn=fn;sci.ctx=ctx;
    if(!sci.installed) { irq_install((int)irq,sci_trampoline);sci.installed=1; }
    *out=&sci;return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler fn,uacpi_handle h) {
    if(h!=&sci || fn!=sci.fn) return UACPI_STATUS_INVALID_ARGUMENT;
    uint32_t flags=irq_save();sci.fn=NULL;sci.ctx=NULL;irq_restore(flags);return UACPI_STATUS_OK;
}
uacpi_handle uacpi_kernel_create_spinlock(void) { return uacpi_kernel_alloc(1); }
void uacpi_kernel_free_spinlock(uacpi_handle h) { uacpi_kernel_free(h); }
uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle h) { (void)h;return irq_save(); }
void uacpi_kernel_unlock_spinlock(uacpi_handle h,uacpi_cpu_flags f) { (void)h;irq_restore((uint32_t)f); }
uacpi_handle uacpi_kernel_create_work_item(void) {
    work_t* w=uacpi_kernel_alloc(sizeof(*w));if(w) memset(w,0,sizeof(*w));return w;
}
void uacpi_kernel_free_work_item(uacpi_handle h) { uacpi_kernel_free(h); }
void uacpi_kernel_schedule_work(uacpi_work_type type,uacpi_handle h,uacpi_work_handler fn,uacpi_handle ctx) {
    (void)type;work_t* w=h;uint32_t flags=irq_save();
    if(!w->pending) {
        w->fn=fn;w->ctx=ctx;w->next=NULL;w->pending=1;
        if(work_tail) work_tail->next=w;else work_head=w;
        work_tail=w;
    }
    irq_restore(flags);
}
void aml_host_drain(void) {
    /* A finite batch: a noisy firmware cannot monopolize the background task. */
    for(unsigned i=0;i<64;i++) {
        uint32_t flags=irq_save();work_t* w=work_head;
        if(!w) { irq_restore(flags);return; }
        work_head=w->next;if(!work_head) work_tail=NULL;
        uacpi_work_handler fn=w->fn;void* ctx=w->ctx;
        w->pending=0;w->next=NULL;active_work++;
        irq_restore(flags);
        fn(ctx); /* may free/requeue w: never touch it again */
        active_work--;
        task_maybe_yield();
    }
}
static void aml_worker(void) {
    task_set_background();
    for(;;) { aml_host_drain();task_sleep_ms(10); }
}
int aml_host_start(void) { return task_create_stack("acpi-events",aml_worker,128u<<10); }

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    uint32_t start=timer_ms();
    for(;;) {
        uint32_t flags=irq_save();
        int done=!work_head && !active_work;
        irq_restore(flags);
        if(done) return UACPI_STATUS_OK;
        if((uint32_t)(timer_ms()-start)>30000) return UACPI_STATUS_TIMEOUT;
        task_sleep_ms(1);
    }
}
