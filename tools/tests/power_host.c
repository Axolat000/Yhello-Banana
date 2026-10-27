/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual native uACPI host callbacks and EC driver with deterministic I/O. */
#include "../../kernel/types.h"
#include "../../kernel/kstring.h"
extern int printf(const char*,...);
extern void abort(void);
extern void* calloc(size_t,size_t);
extern void free(void*);
static unsigned checks;
#define CHECK(c) do{checks++;if(!(c)){printf("FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
#define IO_H
static uint32_t flags=0x200;
static uint32_t irq_save(void) { uint32_t n=flags;flags=0;return n; }
static void irq_restore(uint32_t f) { flags=f; }
static uint32_t config=0xabcd0007,config_addr,clock_ms;
static uint8_t ec_memory[256],ec_address,ec_readvalue,ec_stage,ec_obf,ec_query_code;
static int ec_stuck,ec_missing,glk_denied,no_hardware_clock;
static unsigned io_delays;
static unsigned glk_acquired,glk_released,gpe_finished,method_called;
static uint16_t last_out_port;static unsigned last_out_width;
static void outb(uint16_t port,uint8_t v) {
    last_out_port=port;last_out_width=1;
    if(port>=0xcfc && port<=0xcff) {
        unsigned shift=(port-0xcfc)*8;config=(config&~(255u<<shift))|((uint32_t)v<<shift);return;
    }
    if(port==0x66) {
        if(v==0x80) ec_stage=1;
        else if(v==0x81) ec_stage=2;
        else if(v==0x84) { ec_readvalue=ec_query_code;ec_query_code=0;ec_obf=1; }
        else CHECK(0);
    } else if(port==0x62) {
        if(ec_stage==1) { ec_readvalue=ec_memory[v];ec_obf=1;ec_stage=0; }
        else if(ec_stage==2) { ec_address=v;ec_stage=3; }
        else if(ec_stage==3) { ec_memory[ec_address]=v;ec_stage=0; }
        else CHECK(0);
    } else CHECK(0);
}
static void outw(uint16_t port,uint16_t v) {
    last_out_port=port;last_out_width=2;CHECK(port==0xcfc || port==0xcfe);
    unsigned shift=(port-0xcfc)*8;config=(config&~(65535u<<shift))|((uint32_t)v<<shift);
}
static void outl(uint16_t port,uint32_t v) {
    last_out_port=port;last_out_width=4;
    if(port==0xcf8) config_addr=v;else CHECK(0);
}
static uint8_t inb(uint16_t port) {
    if(port==0x80) { io_delays++;return 0; }
    if(port==0x66) return ec_missing?255:ec_stuck?2:(ec_obf?1:0)|(ec_query_code?0x20:0);
    CHECK(port==0x62 && ec_obf);ec_obf=0;return ec_readvalue;
}
static uint16_t inw(uint16_t p) { (void)p;return 0x1234; }
static uint32_t inl(uint16_t p) { (void)p;return 0x12345678; }
#include "../../kernel/aml_host.c"
#include "../../kernel/laptop.c"
void* kmalloc(size_t n) { return calloc(1,n); }
void kfree(void* p) { free(p); }
void klog(const char* fmt,...) { (void)fmt; }
int mmio_map(uint64_t address,uint64_t size) { return address>=4096 && size && size<=1048576; }
uintptr_t acpi_rsdp_address(void) { return 0x1234000; }
uint32_t pci_read32(uint8_t b,uint8_t d,uint8_t f,uint8_t off) { (void)b;(void)d;(void)f;(void)off;return config; }
uint16_t pci_read16(uint8_t b,uint8_t d,uint8_t f,uint8_t off) { return (uint16_t)(pci_read32(b,d,f,off)>>((off&2)*8)); }
void pci_write32(uint8_t b,uint8_t d,uint8_t f,uint8_t off,uint32_t v) { (void)b;(void)d;(void)f;(void)off;config=v; }
int timer_has_hardware_clock(void) { return !no_hardware_clock; }
uint32_t timer_ms(void) { return clock_ms++; }
uint64_t timer_counter(void) { return (uint64_t)clock_ms++*1000000; }
void task_maybe_yield(void) {}
void task_sleep_ms(uint32_t n) { clock_ms+=n; }
int task_current_pid(void) { return 7; }
static void (*irq_fn)(void);
void irq_install(int irq,void(*fn)(void)) { CHECK(irq==9);irq_fn=fn; }
uacpi_status uacpi_acquire_global_lock(uacpi_u16 timeout,uacpi_u32* seq) {
    CHECK(timeout==100);if(glk_denied)return UACPI_STATUS_TIMEOUT;glk_acquired++;*seq=123;return UACPI_STATUS_OK;
}
uacpi_status uacpi_release_global_lock(uacpi_u32 seq) { CHECK(seq==123);glk_released++;return UACPI_STATUS_OK; }
uacpi_status uacpi_eval_simple(uacpi_namespace_node* n,const uacpi_char* method,uacpi_object** out) {
    CHECK(n==ec_node && !strcmp(method,"_Q42") && !out);method_called++;return UACPI_STATUS_OK;
}
uacpi_status uacpi_finish_handling_gpe(uacpi_namespace_node* n,uacpi_u16 idx) {
    CHECK(!n && idx==42);gpe_finished++;return UACPI_STATUS_OK;
}
/* ASan retains constant transport tables from the included service. These
 * AML object entry points are outside this EC/callback model and must never
 * execute here; the complete interpreter is exercised by the QEMU fixture. */
uacpi_object* uacpi_object_create_buffer(uacpi_data_view v) { (void)v;CHECK(0);return NULL; }
uacpi_object* uacpi_object_create_integer(uacpi_u64 v) { (void)v;CHECK(0);return NULL; }
uacpi_object* uacpi_object_create_package(uacpi_object_array v) { (void)v;CHECK(0);return NULL; }
uacpi_status uacpi_eval(uacpi_namespace_node* n,const uacpi_char* p,const uacpi_object_array* a,uacpi_object** out) {
    (void)n;(void)p;(void)a;(void)out;CHECK(0);return UACPI_STATUS_UNIMPLEMENTED;
}
uacpi_status uacpi_object_get_buffer(uacpi_object* o,uacpi_data_view* out) { (void)o;(void)out;CHECK(0);return UACPI_STATUS_UNIMPLEMENTED; }
void uacpi_object_unref(uacpi_object* o) { (void)o;CHECK(0); }
static unsigned called;
static void once(void* ctx) { CHECK(ctx==(void*)123);called++; }
static void self_free(void* ctx) { called++;uacpi_kernel_free_work_item(ctx); }
static uacpi_interrupt_ret interrupt(void* ctx) { CHECK(ctx==(void*)77);called++;return UACPI_INTERRUPT_HANDLED; }
int main(void) {
    CHECK(aml_host_bytes()==0);
    void* mem=uacpi_kernel_alloc(1024);CHECK(mem && aml_host_bytes()==1024);
    CHECK(!uacpi_kernel_alloc(AML_HEAP_MAX));
    uacpi_kernel_free(mem);CHECK(!aml_host_bytes());
    uacpi_handle sem=uacpi_kernel_create_semaphore(1);CHECK(sem);
    CHECK(!uacpi_kernel_wait_for_semaphore(sem,0));
    CHECK(uacpi_kernel_wait_for_semaphore(sem,2)==UACPI_STATUS_TIMEOUT);
    uacpi_kernel_signal_semaphore(sem);CHECK(!uacpi_kernel_wait_for_semaphore(sem,0));
    uacpi_kernel_free_semaphore(sem);
    uacpi_handle work=uacpi_kernel_create_work_item();CHECK(work);
    uacpi_kernel_schedule_work(UACPI_WORK_NOTIFICATION,work,once,(void*)123);
    uacpi_kernel_schedule_work(UACPI_WORK_NOTIFICATION,work,once,(void*)123);
    aml_host_drain();CHECK(called==1);
    uacpi_kernel_schedule_work(UACPI_WORK_NOTIFICATION,work,self_free,work);
    aml_host_drain();CHECK(called==2 && !aml_host_bytes());
    uacpi_handle pc;
    CHECK(!uacpi_kernel_pci_device_open((uacpi_pci_address){.bus=2,.device=4,.function=1},&pc));
    CHECK(!uacpi_kernel_pci_write8(pc,4,0x12) && config==0xabcd0012 && last_out_width==1 && last_out_port==0xcfc);
    CHECK(config_addr==(0x80000000u|(2u<<16)|(4u<<11)|(1u<<8)|4));
    CHECK(!uacpi_kernel_pci_write16(pc,4,7) && config==0xabcd0007 && last_out_width==2);
    CHECK(uacpi_kernel_pci_write16(pc,5,7)==UACPI_STATUS_INVALID_ARGUMENT);
    CHECK(uacpi_kernel_pci_write8(pc,256,7)==UACPI_STATUS_INVALID_ARGUMENT);
    uacpi_kernel_pci_device_close(pc);
    uacpi_handle port;CHECK(!uacpi_kernel_io_map(65535,1,&port));
    uint16_t value;CHECK(uacpi_kernel_io_read16(port,0,&value)==UACPI_STATUS_INVALID_ARGUMENT);
    uacpi_kernel_io_unmap(port);
    CHECK(uacpi_kernel_map(0xffffffffffffffffull,1)==UACPI_MAP_FAILED);
    CHECK(uacpi_kernel_map(0,16)==UACPI_MAP_FAILED);
    uacpi_handle irq;CHECK(!uacpi_kernel_install_interrupt_handler(9,interrupt,(void*)77,&irq));
    irq_fn();CHECK(called==3);
    CHECK(!uacpi_kernel_uninstall_interrupt_handler(interrupt,irq));irq_fn();CHECK(called==3);
    ec_data=0x62;ec_command=0x66;ec_mutex=uacpi_kernel_create_semaphore(1);
    ec_glk=1;ec_memory[0x10]=0x34;ec_memory[0x11]=0x12;
    uacpi_region_rw_data rw={.address=0x10,.byte_width=2};
    CHECK(!ec_region(UACPI_REGION_OP_READ,&rw) && rw.value==0x1234);
    CHECK(glk_acquired==1 && glk_released==1);
    rw.address=0x20;rw.byte_width=4;rw.value=0x87654321;
    CHECK(!ec_region(UACPI_REGION_OP_WRITE,&rw));
    CHECK(ec_memory[0x20]==0x21 && ec_memory[0x23]==0x87);
    rw.address=255;rw.byte_width=2;CHECK(ec_region(UACPI_REGION_OP_READ,&rw)==UACPI_STATUS_INVALID_ARGUMENT);
    rw.address=0x10;rw.byte_width=1;ec_stuck=1;
    CHECK(ec_region(UACPI_REGION_OP_READ,&rw)==UACPI_STATUS_TIMEOUT);
    ec_stuck=0;CHECK(!ec_region(UACPI_REGION_OP_READ,&rw)); /* mutex released after timeout */
    glk_denied=1;CHECK(ec_region(UACPI_REGION_OP_READ,&rw)==UACPI_STATUS_TIMEOUT);
    glk_denied=0;CHECK(!ec_region(UACPI_REGION_OP_READ,&rw));
    ec_missing=1;CHECK(ec_region(UACPI_REGION_OP_READ,&rw)==UACPI_STATUS_TIMEOUT);ec_missing=0;
    ec_node=(void*)1;ec_gpe=42;ec_query_code=0x42;
    CHECK(ec_interrupt(NULL,NULL,42)==UACPI_INTERRUPT_HANDLED && ec_gpe_pending);
    ec_query();CHECK(method_called==1 && !ec_query_code);
    ec_query();CHECK(gpe_finished==1 && !ec_gpe_pending);
    uacpi_kernel_free_semaphore(ec_mutex);CHECK(!aml_host_bytes());
    panel=(void*)1;panel_nlevels=4;
    panel_levels[0]=0;panel_levels[1]=30;panel_levels[2]=70;panel_levels[3]=100;
    info.brightness=30;
    CHECK(laptop_backlight_step(10)==70 && brightness_request==70);
    info.brightness=70;
    CHECK(laptop_backlight_step(-10)==30 && brightness_request==30);
    info.brightness=100;CHECK(laptop_backlight_step(10)==100);
    no_hardware_clock=1;uacpi_kernel_stall(17);CHECK(io_delays==17);no_hardware_clock=0;
    uacpi_resource res={.type=UACPI_RESOURCE_TYPE_ADDRESS64};
    res.address64.common.type=UACPI_RANGE_MEMORY;res.address64.common.direction=UACPI_CONSUMER;
    res.address64.common.fixed_min_address=res.address64.common.fixed_max_address=UACPI_ADDRESS_FIXED;
    res.address64.common.attribute.memory.write_status=UACPI_WRITABLE;
    res.address64.minimum=0x800000000ull;res.address64.maximum=0x800000fffull;res.address64.address_length=4096;
    ucsi_mailbox=NULL;
    int accepted=ucsi_resource(NULL,&res)==UACPI_ITERATION_DECISION_BREAK;
    CHECK(accepted==(sizeof(uintptr_t)==8));
    res.address64.common.attribute.memory.write_status=UACPI_NON_WRITABLE;
    CHECK(ucsi_resource(NULL,&res)==UACPI_ITERATION_DECISION_CONTINUE);
    res.address64.common.attribute.memory.write_status=UACPI_WRITABLE;
    res.address64.maximum=res.address64.minimum;
    CHECK(ucsi_resource(NULL,&res)==UACPI_ITERATION_DECISION_CONTINUE);
    printf("ACPI host/EC: %u checks passed\n",checks);
    return 0;
}

/* Module providers are tested independently by driver_sdk.c. */
void driver_power_merge(laptop_info_t* out) { (void)out; }
int driver_power_backlight(int percent) { (void)percent;return -1; }
