/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Initial S3 target: QEMU's PC/PIIX4, std VGA, e1000 and optional virtio-blk.
 * Other platforms need AML method execution and their own driver PM hooks. */
#include "acpi_sleep.h"
#include "fpu.h"
#include "tlb.h"
#include "smp.h"
#include "idt.h"
#include "task.h"
#include "timer.h"
#include "rtc.h"
#include "io.h"
#include "kstring.h"
#include "kheap.h"
#include "serial.h"
#include "pci.h"
#include "fb.h"
#include "gpu.h"
#include "keyboard.h"
#include "usb.h"
#include "appwin.h"
#include "audio.h"
#include "fsdisk.h"
#include "virtio_blk.h"
#include "vmtools.h"
#include "sysinfo.h"
#include "../net/net.h"
#include "../net/ipv6.h"

uintptr_t s3_context; /* the assembly uses a page selected from usable conventional RAM */
typedef struct { uint16_t cnta,cntb,evta,evtb,ena,enb,a,b; volatile uint8_t* facs; } platform;
typedef struct { pci_dev_t dev; uint32_t cfg[64]; } saved_pci;
static saved_pci devices[32];
static int ndevices,busy;
static platform pm;
static unsigned resumes;
static uint32_t pm_rd32(const volatile uint8_t* p) { uint32_t n=0;for(int i=0;i<4;i++)n|=(uint32_t)p[i]<<(8*i);return n; }
static uint64_t pm_rd64(const volatile uint8_t* p) { return pm_rd32(p)|(uint64_t)pm_rd32(p+4)<<32; }
static void pm_wr32(volatile uint8_t* p,uint32_t n) { for(int i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i)); }
static void pm_wr64(volatile uint8_t* p,uint64_t n) { pm_wr32(p,(uint32_t)n);pm_wr32(p+4,(uint32_t)(n>>32)); }
static int fail(char* reason,int cap,const char* text) { kstrlcpy(reason,text,cap);return -1; }
static int sdt_ok(const uint8_t* t,uint32_t max) {
    if(!t || (uint64_t)(uintptr_t)t>(1ull<<32)-36)return 0;
    uint32_t n=pm_rd32(t+4);
    if(n<36 || n>max || n>(1ull<<32)-(uintptr_t)t)return 0;
    uint8_t sum=0;for(uint32_t i=0;i<n;i++)sum+=t[i];return sum==0;
}
static uint16_t port(const uint8_t* f,uint32_t len,unsigned legacy,unsigned gas) {
    uint64_t a=pm_rd32(f+legacy);
    if(!a && len>=gas+12 && f[gas]==1 && f[gas+2]==0 && f[gas+1]>=16)a=pm_rd64(f+gas+4);
    return a>0 && a<=65532?(uint16_t)a:0;
}
static int sleep_decl(const uint8_t* t,uint16_t* a,uint16_t* b) {
    if(!sdt_ok(t,1u<<20))return 0;
    uint32_t len=pm_rd32(t+4);
    if(len<36 || len>(1u<<20) || (uint64_t)(uintptr_t)t+len>(1ull<<32))return 0;
    return acpi_sleep_types(t+36,len-36,"_S3_",a,b);
}
static int pci_allowed(const pci_dev_t* d,void* ctx) {
    char* reason=ctx;
    int ok=(d->vendor==0x8086 && (d->device==0x1237 || d->device==0x7000 || d->device==0x7010 || d->device==0x7113 || d->device==0x100e)) ||
        (d->vendor==0x1234 && d->device==0x1111) ||
        (d->vendor==0x1af4 && (d->device==0x1001 || d->device==0x1042));
    if(!ok) { ksnprintf(reason,128,"S3 resume is not implemented for PCI %04x:%04x (%u:%u.%u)",d->vendor,d->device,d->bus,d->dev,d->fn);return -1; }
    return 0;
}
int acpi_s3_status(char* reason,int cap) {
    uint32_t a,b,c,d;
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(1),"c"(0));
    if(!(c&(1u<<31)) || !(d&(1u<<24)) || !(d&(1u<<16)))return fail(reason,cap,"S3 currently targets QEMU PC guests with FXSR/PAT");
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0x40000000),"c"(0));
    char hv[13];memcpy(hv,&b,4);memcpy(hv+4,&c,4);memcpy(hv+8,&d,4);hv[12]=0;
    if(strcmp(hv,"TCGTCGTCGTCG") && strcmp(hv,"KVMKVMKVM"))return fail(reason,cap,"S3 is enabled for QEMU/KVM guests first");
    pci_dev_t chipset;
    if(!pci_find(0x8086,0x7113,&chipset))return fail(reason,cap,"S3 currently needs QEMU's PC/PIIX4 machine");
    const uint8_t* f=acpi_find_table("FACP");
    if(!f)return fail(reason,cap,"firmware supplied no ACPI FADT");
    uint32_t len=pm_rd32(f+4);
    if(!sdt_ok(f,4096) || len<116 || (pm_rd32(f+112)&(1u<<20)))return fail(reason,cap,"unsupported or corrupt ACPI fixed-hardware table");
    uint64_t dsdt=pm_rd32(f+40),facs=pm_rd32(f+36);
    if(len>=148 && pm_rd64(f+140))dsdt=pm_rd64(f+140);
    if(len>=140 && pm_rd64(f+132))facs=pm_rd64(f+132);
    uint16_t sa=0,sb=0;
    int found=sleep_decl(acpi_table_at(dsdt,"DSDT"),&sa,&sb);
    for(int i=0;i<8 && !found;i++)found=sleep_decl(acpi_find_table_n("SSDT",i),&sa,&sb);
    if(!found)return fail(reason,cap,"firmware does not declare a static ACPI S3 state");
    if(!timer_has_hardware_clock())return fail(reason,cap,"S3 needs an HPET or ACPI PM clock");
    if(!sysinfo_get()->s3_wake_page)return fail(reason,cap,"no available conventional RAM page for S3 wake code");
    if(facs<4096 || facs>(1ull<<32)-64)return fail(reason,cap,"no reachable ACPI waking-vector structure");
    volatile uint8_t* fc=(volatile uint8_t*)(uintptr_t)facs;
    if(fc[0]!='F' || fc[1]!='A' || fc[2]!='C' || fc[3]!='S' || pm_rd32(fc+4)<64)return fail(reason,cap,"invalid ACPI FACS");
    platform p={.cnta=port(f,len,64,172),.cntb=port(f,len,68,184),.evta=port(f,len,56,148),
        .evtb=port(f,len,60,160),.a=sa,.b=sb,.facs=fc};
    if(!p.cnta || !p.evta || f[88]<4 || f[88]>16 || (f[88]&1) || f[89]<2 ||
       (uint32_t)p.evta+f[88]>65536 || (p.evtb && (uint32_t)p.evtb+f[88]>65536))
        return fail(reason,cap,"no usable ACPI PM1 control/event ports");
    if(!(inw(p.cnta)&1))return fail(reason,cap,"ACPI mode is not enabled");
    p.ena=p.evta+f[88]/2;p.enb=p.evtb?p.evtb+f[88]/2:0;
    gpu_t* g=gpu_active();
    if(fb_available() && (!g || !g->driver || strcmp(g->driver,"bga")))return fail(reason,cap,"S3 display resume currently needs QEMU standard VGA");
    if(audio_available())return fail(reason,cap,"S3 audio-controller resume is not implemented yet");
    char why[128]="";
    if(pci_scan(pci_allowed,why))return fail(reason,cap,why);
    pm=p;kstrlcpy(reason,"ACPI S3 available (QEMU PC, standard VGA/e1000/virtio-blk)",cap);return 0;
}
unsigned acpi_s3_resumes(void) { return resumes; }

static int pci_save(const pci_dev_t* d,void* unused) {
    (void)unused;if(ndevices>=32)return -1;
    saved_pci* s=&devices[ndevices++];s->dev=*d;
    for(int i=0;i<64;i++)s->cfg[i]=pci_read32(d->bus,d->dev,d->fn,i*4);
    return 0;
}
static void pci_restore(void) {
    for(int i=0;i<ndevices;i++) {
        saved_pci* s=&devices[i];pci_dev_t* d=&s->dev;
        for(int r=0x10;r<=0x24;r+=4)pci_write32(d->bus,d->dev,d->fn,r,s->cfg[r/4]);
        if(d->vendor==0x8086 && d->device==0x7000)pci_write32(d->bus,d->dev,d->fn,0x60,s->cfg[0x60/4]);
        if(d->vendor==0x8086 && d->device==0x7113) {
            pci_write32(d->bus,d->dev,d->fn,0x40,s->cfg[0x40/4]);
            pci_write32(d->bus,d->dev,d->fn,0x80,s->cfg[0x80/4]);
        }
        pci_write16(d->bus,d->dev,d->fn,0x3c,(uint16_t)s->cfg[0x3c/4]);
        pci_write16(d->bus,d->dev,d->fn,4,(uint16_t)s->cfg[1]&~4u);
    }
}
static uint8_t cmos(int r) { outb(0x70,(uint8_t)(0x80|r));uint8_t v=inb(0x71);outb(0x70,0);return v; }
static void cmos_write(int r,uint8_t v) { outb(0x70,(uint8_t)(0x80|r));outb(0x71,v);outb(0x70,0); }
static uint8_t encode(unsigned n,int binary) { return binary?(uint8_t)n:(uint8_t)((n/10)*16+n%10); }
static uint8_t alarm_saved[4];
static int arm_alarm(unsigned seconds) {
    rtc_datetime_t now;if(rtc_read_hardware(&now))return -1;
    alarm_saved[0]=cmos(1);alarm_saved[1]=cmos(3);alarm_saved[2]=cmos(5);alarm_saved[3]=cmos(0xb);
    unsigned future=(now.hour*3600u+now.minute*60u+now.second+seconds)%86400u;
    int binary=alarm_saved[3]&4;unsigned hour=future/3600u;
    uint8_t h=encode(hour,binary);
    if(!(alarm_saved[3]&2))h=encode(hour%12?hour%12:12,binary)|(hour>=12?0x80:0);
    cmos_write(0xb,alarm_saved[3]&~0x20u);
    cmos_write(1,encode(future%60,binary));cmos_write(3,encode(future/60%60,binary));cmos_write(5,h);
    (void)cmos(0xc);cmos_write(0xb,alarm_saved[3]|0x20);return 0;
}
static void restore_alarm(void) {
    cmos_write(0xb,alarm_saved[3]&~0x20u);(void)cmos(0xc);
    cmos_write(1,alarm_saved[0]);cmos_write(3,alarm_saved[1]);cmos_write(5,alarm_saved[2]);cmos_write(0xb,alarm_saved[3]);
}
/* Called by the assembly after saving the return stack and CPU context. */
void acpi_s3_enter_hw(void) {
    uint16_t a=inw(pm.cnta)&~(uint16_t)((7u<<10)|(1u<<13));
    uint16_t b=pm.cntb?inw(pm.cntb)&~(uint16_t)((7u<<10)|(1u<<13)):0;
    if(pm.cntb)outw(pm.cntb,b|(pm.b<<10));
    outw(pm.cnta,a|(pm.a<<10));
    if(pm.cntb)outw(pm.cntb,b|(pm.b<<10)|(1u<<13));
    outw(pm.cnta,a|(pm.a<<10)|(1u<<13));
    for(unsigned i=0;i<500000;i++)inb(0x80); /* refusal returns; wake uses the trampoline */
}
extern uint8_t s3_wake_start[],s3_wake_end[],s3_wake_segment[];
extern const uint32_t s3_wake_fixups[];
extern int s3_arch_suspend(void);
extern netdev_t* e1000_probe(void);
int acpi_s3_suspend(unsigned seconds,char* reason,int cap) {
    uintptr_t initial_flags;__asm__ volatile("pushf; pop %0":"=r"(initial_flags));
    if(cpu_id()!=0 || !(initial_flags&0x200))return fail(reason,cap,"S3 must run on the boot core with interrupts enabled");
    if(seconds>3600)return fail(reason,cap,"wake delay must be 1..3600 seconds, or zero for manual wake");
    if(acpi_s3_status(reason,cap))return -1;
    if(__sync_lock_test_and_set(&busy,1))return fail(reason,cap,"another sleep request is in progress");
    int rc=-1,frozen=0,paused=0,armed=0;
    uint8_t* image=0;uint32_t image_size=0;
    if(fsdisk_frozen()){fail(reason,cap,"the filesystem is already frozen");goto done;}
    if(fsdisk_is_installed()) {
        int sync_rc=fsdisk_freeze(1);frozen=1;
        if(sync_rc){fail(reason,cap,"filesystem sync failed");goto done;}
    }
    if(task_smp_pause()){fail(reason,cap,"processor cores did not quiesce");goto done;}paused=1;
    const fb_info_t* fb=fb_info();
    if(fb_available()) {
        uint64_t size=(uint64_t)fb->pitch*fb->height;
        if(size>(64u<<20) || !(image=kmalloc((uint32_t)size))){fail(reason,cap,"not enough memory to retain the display");goto done;}
        image_size=(uint32_t)size;fb_present();memcpy(image,(const void*)fb->addr,image_size);
    }
    ndevices=0;if(pci_scan(pci_save,0)){fail(reason,cap,"too many PCI devices");goto done;}
    if(seconds){if(arm_alarm(seconds)){fail(reason,cap,"RTC wake alarm could not be set");goto done;}armed=1;}
    appwin_release_mouse();audio_stop();
    uint32_t old_vector=pm_rd32(pm.facs+12),old_flags=pm_rd32(pm.facs+36);
    uint64_t old_xvector=pm_rd64(pm.facs+24);
    uint16_t old_a=inw(pm.ena),old_b=pm.enb?inw(pm.enb):0;
    uint16_t old_cnta=inw(pm.cnta)&~(uint16_t)((7u<<10)|(1u<<13));
    uint16_t old_cntb=pm.cntb?inw(pm.cntb)&~(uint16_t)((7u<<10)|(1u<<13)):0;
    uintptr_t flags;__asm__ volatile("pushf; pop %0; cli":"=r"(flags)::"memory");
    smp_s3_save();
    uint64_t before=timer_counter();
    uint32_t wake=sysinfo_get()->s3_wake_page;s3_context=wake+0x800u;
    uint8_t* code=(uint8_t*)(uintptr_t)wake;
    memcpy(code,s3_wake_start,(size_t)(s3_wake_end-s3_wake_start));
    for(unsigned i=0;s3_wake_fixups[i]!=0xffffffffu;i++) {
        uint32_t off=s3_wake_fixups[i];pm_wr32(code+off,pm_rd32(code+off)+wake);
    }
    uint16_t segment=(uint16_t)(wake>>4);
    memcpy(code+(s3_wake_segment-s3_wake_start),&segment,2);
    pm_wr32(pm.facs+12,wake);pm_wr64(pm.facs+24,0);pm_wr32(pm.facs+36,old_flags&~1u);
    outw(pm.evta,0xffff);if(pm.evtb)outw(pm.evtb,0xffff);
    outw(pm.ena,(1u<<8)|(armed?(1u<<10):0));if(pm.enb)outw(pm.enb,(1u<<8)|(armed?(1u<<10):0));
    for(int i=0;i<ndevices;i++) { pci_dev_t* d=&devices[i].dev;pci_write16(d->bus,d->dev,d->fn,4,(uint16_t)devices[i].cfg[1]&~4u); }
    klog("acpi: entering S3; wake vector %x%s\n",wake,armed?", RTC alarm armed":"");serial_kick();
    rc=s3_arch_suspend();
    fpu_cpu_init();                         /* (the wake reset XCR0: AVX state cannot be restored without it) */
    serial_init();pci_restore();idt_s3_resume();timer_s3_resume(before);
    tlb_cpu_init();                         /* (CR4 came back without global pages, PCIDs: kernel/tlb.c) */
    int ap_rc=smp_s3_resume();
    outw(pm.cnta,old_cnta);if(pm.cntb)outw(pm.cntb,old_cntb);
    outw(pm.evta,0xffff);if(pm.evtb)outw(pm.evtb,0xffff);
    outw(pm.ena,old_a);if(pm.enb)outw(pm.enb,old_b);
    pm_wr32(pm.facs+12,old_vector);pm_wr64(pm.facs+24,old_xvector);pm_wr32(pm.facs+36,old_flags);
    if(armed){restore_alarm();armed=0;}
    if(ap_rc)kernel_panic("S3: a processor failed to resume");
    __asm__ volatile("sti" ::: "memory");
    if(gpu_bga_s3_resume())kernel_panic("S3: the display failed to resume");
    if(image)memcpy((void*)fb->addr,image,image_size);
    keyboard_init();mouse_init();vmtools_s3_resume();rtc_init();
    if(net_if()->dev) {
        netdev_t old=*net_if()->dev;
        netdev_t* nd=e1000_probe();if(!nd)kernel_panic("S3: the network device failed to resume");
        /* probe resets the driver's record; registration belongs to net.c
         * and must survive, or net_poll silently skips this interface. */
        memcpy(nd->ifname,old.ifname,sizeof(nd->ifname));nd->present=old.present;nd->is_usb=old.is_usb;
        nd->rx_packets=old.rx_packets;nd->tx_packets=old.tx_packets;nd->rx_bytes=old.rx_bytes;nd->tx_bytes=old.tx_bytes;
        nd->rx_errors=old.rx_errors;nd->tx_errors=old.tx_errors;nd->rx_dropped=old.rx_dropped;
        net_if()->dev=nd;
        if(nd->set_multicast)nd->set_multicast(nd,ipv6_enabled());
    }
    if(virtio_blk_s3_resume())kernel_panic("S3: a disk failed to resume");
    if(!rc){resumes++;klog("acpi: resumed from S3 (%u), %d cores\n",resumes,cpu_count());kstrlcpy(reason,"resumed from ACPI S3",cap);}
    else fail(reason,cap,"firmware did not enter S3; state restored");
    if(!(flags&0x200))__asm__ volatile("cli" ::: "memory");
done:
    if(armed)restore_alarm();
    if(image)kfree(image);
    if(paused)task_smp_unpause();
    if(frozen)fsdisk_freeze(0);
    __sync_lock_release(&busy);return rc;
}
