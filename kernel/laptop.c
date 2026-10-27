/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Full AML-backed laptop services. All hardware evaluation runs on background
 * tasks; UI clients read cached snapshots. One legacy ACPI EC and one UCSI PPM.
 * UCSI 1.1/1.2 only until newer layouts have dedicated tests. */
#include "laptop.h"
#include "driver_ext.h"
#include "aml_host.h"
#include "smp.h"
#include "task.h"
#include "timer.h"
#include "io.h"
#include "paging.h"
#include "kstring.h"
#include "serial.h"
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include <uacpi/resources.h>
#include <uacpi/opregion.h>
#include <uacpi/event.h>

static laptop_info_t info={.lid=-1,.brightness=-1};
static uacpi_namespace_node* batteries[LAPTOP_BATTERIES];
static uacpi_namespace_node *adapter[4],*lid,*panel,*ec_node,*ucsi_node;
static unsigned nadapter;
static uint16_t ec_data,ec_command;
static int ec_glk,ec_gpe=-1,ec_gpe_pending;
static uacpi_handle ec_mutex;
static volatile uint8_t* ucsi_mailbox;
static unsigned ucsi_mailbox_len;
static uint32_t panel_levels[64];
static unsigned panel_nlevels;
static int brightness_request=-1,role_request=-1,role_source;
static uint64_t int_value(uacpi_object* o) {
    uacpi_u64 n=0xffffffffffffffffull;
    if(uacpi_object_get_integer(o,&n)) return 0xffffffffffffffffull;
    return n;
}
static void status(const char* s) { ksnprintf(info.status,sizeof(info.status),"%s",s);info.generation++; }
static int ec_wait(unsigned mask,unsigned value) {
    uint32_t start=timer_ms();
    for(unsigned i=0;i<100000;i++) {
        uint8_t s=inb(ec_command);
        if(s==255) return -1;
        if((s&mask)==value) return 0;
        if((uint32_t)(timer_ms()-start)>=50) return -1;
        task_maybe_yield();
        __asm__ volatile("pause");
    }
    return -1;
}
static int ec_send(uint16_t port,uint8_t value) {
    if(ec_wait(2,0)<0) return -1;
    outb(port,value);return 0;
}
static int ec_byte(unsigned address,uint8_t* value,int write) {
    if(ec_send(ec_command,write?0x81:0x80)<0 || ec_send(ec_data,(uint8_t)address)<0) return -1;
    if(write) return ec_send(ec_data,*value)<0 || ec_wait(2,0)<0?-1:0;
    if(ec_wait(1,1)<0) return -1;
    *value=inb(ec_data);return 0;
}
static uacpi_status ec_region(uacpi_region_op op,void* arg) {
    if(op==UACPI_REGION_OP_ATTACH) {
        uacpi_region_attach_data* d=arg;d->out_region_context=NULL;return UACPI_STATUS_OK;
    }
    if(op==UACPI_REGION_OP_DETACH) return UACPI_STATUS_OK;
    if(op!=UACPI_REGION_OP_READ && op!=UACPI_REGION_OP_WRITE) return UACPI_STATUS_UNIMPLEMENTED;
    uacpi_region_rw_data* d=arg;
    if(!ec_data || !ec_command || !d->byte_width || d->byte_width>8 ||
       d->address>255 || d->byte_width>256-d->address) return UACPI_STATUS_INVALID_ARGUMENT;
    uacpi_status ret=uacpi_kernel_wait_for_semaphore(ec_mutex,500);
    if(ret) return ret;
    uacpi_u32 seq=0;
    if(ec_glk) {
        ret=uacpi_acquire_global_lock(100,&seq);
        if(ret) { uacpi_kernel_signal_semaphore(ec_mutex);return ret; }
    }
    uint64_t value=0;
    for(unsigned i=0;i<d->byte_width;i++) {
        uint8_t b=(uint8_t)(d->value>>(8*i));
        if(ec_byte((unsigned)d->address+i,&b,op==UACPI_REGION_OP_WRITE)<0) { ret=UACPI_STATUS_TIMEOUT;break; }
        value|=(uint64_t)b<<(8*i);
    }
    if(ec_glk) uacpi_release_global_lock(seq);
    uacpi_kernel_signal_semaphore(ec_mutex);
    if(!ret && op==UACPI_REGION_OP_READ) d->value=value;
    return ret;
}
static uacpi_interrupt_ret ec_interrupt(void* ctx,uacpi_namespace_node* dev,uacpi_u16 idx) {
    (void)ctx;(void)dev;(void)idx;ec_gpe_pending=1;
    return UACPI_INTERRUPT_HANDLED; /* defer query, then explicitly re-enable */
}
static void ec_query(void) {
    if(!ec_node || !ec_command) return;
    if(!(inb(ec_command)&0x20)) {
        if(ec_gpe_pending && ec_gpe>=0) {
            ec_gpe_pending=0;uacpi_finish_handling_gpe(NULL,(uacpi_u16)ec_gpe);
        }
        return;
    }
    if(uacpi_kernel_wait_for_semaphore(ec_mutex,0)) return;
    uacpi_u32 seq=0;
    if(ec_glk && uacpi_acquire_global_lock(100,&seq)) {
        uacpi_kernel_signal_semaphore(ec_mutex);return;
    }
    uint8_t q=0;
    if(ec_send(ec_command,0x84)==0 && ec_wait(1,1)==0) q=inb(ec_data);
    if(ec_glk) uacpi_release_global_lock(seq);
    uacpi_kernel_signal_semaphore(ec_mutex);
    if(q) {
        const char* hex="0123456789ABCDEF";
        char method[5]={'_','Q',hex[q>>4],hex[q&15],0};
        uacpi_eval_simple(ec_node,method,NULL);
    }
}
static uacpi_iteration_decision ec_resource(void* user,uacpi_resource* r) {
    unsigned* n=user;uint16_t port=0;
    if(r->type==UACPI_RESOURCE_TYPE_IO && r->io.length && r->io.minimum==r->io.maximum) port=r->io.minimum;
    if(r->type==UACPI_RESOURCE_TYPE_FIXED_IO && r->fixed_io.length) port=r->fixed_io.address;
    if(port) { if(!(*n)++) ec_data=port;else if(*n==2) ec_command=port; }
    return UACPI_ITERATION_DECISION_CONTINUE;
}
static uacpi_iteration_decision find_ec(void* user,uacpi_namespace_node* node,uacpi_u32 depth) {
    (void)user;(void)depth;
    if(ec_node) return UACPI_ITERATION_DECISION_BREAK;
    unsigned n=0;
    if(!ec_data || !ec_command) {
        if(uacpi_for_each_device_resource(node,"_CRS",ec_resource,&n) || n!=2 || ec_data==ec_command) {
            ec_data=ec_command=0;return UACPI_ITERATION_DECISION_CONTINUE;
        }
    }
    ec_node=node;uacpi_u64 lock=0;
    if(!uacpi_eval_simple_integer(node,"_GLK",&lock)) ec_glk=lock!=0;
    uacpi_u64 gpe;
    if(!uacpi_eval_simple_integer(node,"_GPE",&gpe) && gpe<=65535) ec_gpe=(int)gpe;
    return UACPI_ITERATION_DECISION_BREAK;
}
static uacpi_iteration_decision find_battery(void* user,uacpi_namespace_node* node,uacpi_u32 depth) {
    (void)user;(void)depth;
    if(info.battery_count<LAPTOP_BATTERIES) batteries[info.battery_count++]=node;
    return UACPI_ITERATION_DECISION_CONTINUE;
}
static uacpi_iteration_decision find_ac(void* user,uacpi_namespace_node* node,uacpi_u32 depth) {
    (void)user;(void)depth;if(nadapter<4) adapter[nadapter++]=node;
    return UACPI_ITERATION_DECISION_CONTINUE;
}
static uacpi_iteration_decision find_lid(void* user,uacpi_namespace_node* node,uacpi_u32 depth) {
    (void)user;(void)depth;lid=node;return UACPI_ITERATION_DECISION_BREAK;
}
static void object_string(uacpi_object* o,char* out,unsigned size) {
    uacpi_data_view v;
    if(!size) return;
    out[0]=0;
    if(uacpi_object_get_string(o,&v)) return;
    unsigned n=v.length<size-1?(unsigned)v.length:size-1;
    for(unsigned i=0;i<n;i++) { if(!((const uint8_t*)v.bytes)[i]) { n=i;break; } out[i]=((const uint8_t*)v.bytes)[i]>=32 && ((const uint8_t*)v.bytes)[i]<127?((const char*)v.bytes)[i]:'?'; }
    out[n]=0;
}
static void battery_update(unsigned n) {
    laptop_battery_t b;memset(&b,0,sizeof(b));b.percent=b.minutes=-1;
    uacpi_u64 sta=0;
    if(uacpi_eval_simple_integer(batteries[n],"_STA",&sta) || !(sta&16)) { info.batteries[n]=b;return; }
    b.present=1;
    uacpi_object *o=NULL;uacpi_object_array a;
    int bix=!uacpi_eval_simple_package(batteries[n],"_BIX",&o);
    if(!bix && uacpi_eval_simple_package(batteries[n],"_BIF",&o)) { info.batteries[n]=b;return; }
    if(!uacpi_object_get_package(o,&a) && a.count>=(bix?20u:13u)) {
        unsigned off=bix?1:0;
        uint64_t unit=int_value(a.objects[off]),design=int_value(a.objects[off+1]),full=int_value(a.objects[off+2]);
        if(unit<=1 && design<0xffffffffull && full>0 && full<0xffffffffull) {
            b.unit=(int)unit;b.design=(uint32_t)design;b.full=(uint32_t)full;
        }
        unsigned str=bix?16:9;
        object_string(a.objects[str],b.model,sizeof(b.model));
        object_string(a.objects[str+1],b.serial,sizeof(b.serial));
        object_string(a.objects[str+2],b.chemistry,sizeof(b.chemistry));
    }
    uacpi_object_unref(o);o=NULL;
    if(!uacpi_eval_simple_package(batteries[n],"_BST",&o)) {
        if(!uacpi_object_get_package(o,&a) && a.count>=4) {
            uint64_t st=int_value(a.objects[0]),rate=int_value(a.objects[1]),rem=int_value(a.objects[2]),mv=int_value(a.objects[3]);
            if(st<=7 && rem<0xffffffffull && b.full) {
                b.state=(uint32_t)st;b.remaining=(uint32_t)rem;b.valid=1;
                uint64_t percent=rem*100/b.full;b.percent=percent>100?100:(int)percent;
                b.rate=rate<0xffffffffull?(uint32_t)rate:0;
                b.millivolts=mv<0xffffffffull?(uint32_t)mv:0;
                if(b.rate && (b.state&1)) {
                    uint64_t minutes=rem*60/b.rate;b.minutes=minutes>100000?-1:(int)minutes;
                }
            }
        }
        uacpi_object_unref(o);
    }
    info.batteries[n]=b;
}
static uacpi_iteration_decision find_panel(void* user,uacpi_namespace_node* node,uacpi_u32 depth) {
    (void)user;(void)depth;
    uacpi_namespace_node* method;
    if(uacpi_namespace_node_find(node,"_BCM",&method)) return UACPI_ITERATION_DECISION_CONTINUE;
    uacpi_object* o=NULL;uacpi_object_array a;
    if(uacpi_eval_simple_package(node,"_BCL",&o)) return UACPI_ITERATION_DECISION_CONTINUE;
    unsigned count=0;
    if(!uacpi_object_get_package(o,&a) && a.count>=3 && a.count<=66) {
        for(unsigned i=2;i<a.count;i++) {
            uint64_t level=int_value(a.objects[i]);
            if(level<=100) panel_levels[count++]=(uint32_t)level;
        }
    }
    uacpi_object_unref(o);
    if(!count) return UACPI_ITERATION_DECISION_CONTINUE;
    panel=node;panel_nlevels=count;
    return UACPI_ITERATION_DECISION_BREAK;
}
static void panel_update(void) {
    if(!panel) return;
    int request=brightness_request;brightness_request=-1;
    if(request>=0) {
        uint32_t best=panel_levels[0],distance=101;
        for(unsigned i=0;i<panel_nlevels;i++) {
            uint32_t level=panel_levels[i],d=level>(unsigned)request?level-(unsigned)request:(unsigned)request-level;
            if(d<distance) { best=level;distance=d; }
        }
        uacpi_object* arg=uacpi_object_create_integer(best);
        if(arg) {
            uacpi_object* args[]={arg};uacpi_object_array a={args,1};
            if(uacpi_eval(panel,"_BCM",&a,NULL)) status("ACPI brightness change failed");
            else info.brightness=(int)best;
            uacpi_object_unref(arg);
        }
    }
    uacpi_u64 value;
    if(!uacpi_eval_simple_integer(panel,"_BQC",&value) && value<=100) info.brightness=(int)value;
    info.brightness_pending=brightness_request>=0;
}
static int ucsi_dsm(unsigned function) {
    static const uint8_t guid[16]={0xc2,0x98,0x83,0x6f,0xa4,0x7c,0xe4,0x11,0xad,0x36,0x63,0x10,0x42,0xb5,0x00,0x8f};
    uacpi_data_view v={.bytes=(void*)guid,.length=16};
    uacpi_object* args[4]={uacpi_object_create_buffer(v),uacpi_object_create_integer(1),
                          uacpi_object_create_integer(function),uacpi_object_create_package((uacpi_object_array){NULL,0})};
    int good=args[0] && args[1] && args[2] && args[3];
    uacpi_object* result=NULL;
    if(good) good=!uacpi_eval(ucsi_node,"_DSM",&(uacpi_object_array){args,4},&result);
    if(good && function==0) {
        uacpi_data_view b;
        good=result && !uacpi_object_get_buffer(result,&b) && b.length && (((uint8_t*)b.bytes)[0]&7)==7;
    }
    if(result) uacpi_object_unref(result);
    for(unsigned i=0;i<4;i++) if(args[i]) uacpi_object_unref(args[i]);
    return good?0:-1;
}
static int ucsi_read(void* ctx,unsigned off,void* data,unsigned n) {
    (void)ctx;
    if(off>ucsi_mailbox_len || n>ucsi_mailbox_len-off || ucsi_dsm(2)<0) return -1;
    for(unsigned i=0;i<n;i++) ((uint8_t*)data)[i]=ucsi_mailbox[off+i];
    return 0;
}
static int ucsi_write(void* ctx,uint64_t command) {
    (void)ctx;
    if(ucsi_mailbox_len<16) return -1;
    for(unsigned i=0;i<8;i++) ucsi_mailbox[8+i]=(uint8_t)(command>>(8*i));
    __asm__ volatile("" ::: "memory");
    return ucsi_dsm(1);
}
static uint32_t ucsi_now(void* ctx) { (void)ctx;return timer_ms(); }
static void ucsi_wait(void* ctx,unsigned ms) { (void)ctx;task_sleep_ms(ms); }
static const ucsi_transport_t ucsi_ops={ucsi_read,ucsi_write,ucsi_now,ucsi_wait};
static uacpi_iteration_decision ucsi_resource(void* user,uacpi_resource* r) {
    (void)user;
    uint64_t base=0,length=0,maximum=0,translation=0;
    uacpi_resource_address_common* common=NULL;
    if(r->type==UACPI_RESOURCE_TYPE_FIXED_MEMORY32 && r->fixed_memory32.write_status==UACPI_WRITABLE) {
        base=r->fixed_memory32.address;length=r->fixed_memory32.length;
    } else if(r->type==UACPI_RESOURCE_TYPE_MEMORY32 && r->memory32.write_status==UACPI_WRITABLE &&
              r->memory32.minimum==r->memory32.maximum) {
        base=r->memory32.minimum;length=r->memory32.length;
    } else if(r->type==UACPI_RESOURCE_TYPE_ADDRESS32) {
        common=&r->address32.common;base=r->address32.minimum;maximum=r->address32.maximum;
        translation=r->address32.translation_offset;length=r->address32.address_length;
    } else if(r->type==UACPI_RESOURCE_TYPE_ADDRESS64) {
        common=&r->address64.common;base=r->address64.minimum;maximum=r->address64.maximum;
        translation=r->address64.translation_offset;length=r->address64.address_length;
    } else if(r->type==UACPI_RESOURCE_TYPE_ADDRESS64_EXTENDED) {
        common=&r->address64_extended.common;base=r->address64_extended.minimum;maximum=r->address64_extended.maximum;
        translation=r->address64_extended.translation_offset;length=r->address64_extended.address_length;
    }
    if(common) {
        if(common->type!=UACPI_RANGE_MEMORY || common->direction!=UACPI_CONSUMER ||
           common->fixed_min_address!=UACPI_ADDRESS_FIXED || common->fixed_max_address!=UACPI_ADDRESS_FIXED ||
           common->attribute.memory.write_status!=UACPI_WRITABLE || !length || maximum<base ||
           length-1>maximum-base || translation>0xffffffffffffffffull-base)
            return UACPI_ITERATION_DECISION_CONTINUE;
        base+=translation;
    }
    if(base>=4096 && length>=48 && length<=(1u<<20) && length<=0xffffffffffffffffull-base &&
       base<=(uint64_t)(uintptr_t)-1 && mmio_map(base,length)) {
        ucsi_mailbox=(void*)(uintptr_t)base;ucsi_mailbox_len=(unsigned)length;
        return UACPI_ITERATION_DECISION_BREAK;
    }
    return UACPI_ITERATION_DECISION_CONTINUE;
}
static uacpi_iteration_decision find_ucsi(void* user,uacpi_namespace_node* node,uacpi_u32 depth) {
    (void)user;(void)depth;
    if(ucsi_node) return UACPI_ITERATION_DECISION_BREAK;
    ucsi_node=node;
    if(uacpi_for_each_device_resource(node,"_CRS",ucsi_resource,NULL) || !ucsi_mailbox || ucsi_dsm(0)<0) {
        ucsi_node=NULL;ucsi_mailbox=NULL;ucsi_mailbox_len=0;return UACPI_ITERATION_DECISION_CONTINUE;
    }
    if(ucsi_init(&info.usbc,&ucsi_ops,NULL)<0) klog("ucsi: firmware interface did not initialize (version %x)\n",info.usbc.version);
    else klog("ucsi: %u USB-C connector(s), PPM negotiates USB Power Delivery\n",info.usbc.num_ports);
    return UACPI_ITERATION_DECISION_BREAK;
}
static void laptop_service(void) {
    task_set_background();status("Initializing ACPI AML");
    if(!timer_has_hardware_clock()) { status("ACPI requires an HPET or ACPI PM clock");return; }
    if(aml_host_start()<0) { status("ACPI event task unavailable");return; }
    uacpi_status ret=uacpi_initialize(UACPI_FLAG_BAD_CSUM_FATAL|UACPI_FLAG_BAD_TBL_SIGNATURE_FATAL);
    if(ret) { status(uacpi_status_to_string(ret));return; }
    ec_mutex=uacpi_kernel_create_semaphore(1);
    const uint8_t* e=acpi_find_table("ECDT");
    if(e) {
        uint32_t len;memcpy(&len,e+4,4);
        uint64_t cmd=0,data=0;
        if(len>=66 && e[36]==1 && e[48]==1) {
            memcpy(&cmd,e+40,8);memcpy(&data,e+52,8);
            if(cmd && data && cmd<=65535 && data<=65535 && cmd!=data) { ec_command=(uint16_t)cmd;ec_data=(uint16_t)data;ec_gpe=e[64]; }
        }
    }
    if(ec_mutex) uacpi_install_address_space_handler(uacpi_namespace_root(),UACPI_ADDRESS_SPACE_EMBEDDED_CONTROLLER,ec_region,NULL);
    ret=uacpi_namespace_load();
    if(ret) { status(uacpi_status_to_string(ret));return; }
    uacpi_find_devices("PNP0C09",find_ec,NULL);info.ec=ec_node!=NULL;
    ret=uacpi_set_interrupt_model(UACPI_INTERRUPT_MODEL_PIC);
    if(ret && ret!=UACPI_STATUS_NOT_FOUND) klog("aml: _PIC: %s\n",uacpi_status_to_string(ret));
    ret=uacpi_namespace_initialize();
    if(ret) { status(uacpi_status_to_string(ret));return; }
    if(!ec_node) { uacpi_find_devices("PNP0C09",find_ec,NULL);info.ec=ec_node!=NULL; }
    if(ec_node && ec_gpe>=0 && !uacpi_install_gpe_handler(NULL,(uacpi_u16)ec_gpe,UACPI_GPE_TRIGGERING_LEVEL,ec_interrupt,NULL))
        uacpi_enable_gpe(NULL,(uacpi_u16)ec_gpe);
    info.runtime=1;status("ACPI AML ready");
    uacpi_find_devices("PNP0C0A",find_battery,NULL);
    uacpi_find_devices("ACPI0003",find_ac,NULL);
    uacpi_find_devices("PNP0C0D",find_lid,NULL);
    uacpi_namespace_for_each_child(uacpi_namespace_root(),find_panel,NULL,UACPI_OBJECT_DEVICE_BIT,UACPI_MAX_DEPTH_ANY,NULL);
    uacpi_find_devices("USBC000",find_ucsi,NULL);
    for(;;) {
        for(unsigned i=0;i<(unsigned)info.battery_count;i++) battery_update(i);
        info.ac_present=nadapter!=0;info.ac_online=0;
        for(unsigned i=0;i<nadapter;i++) { uacpi_u64 v=0;if(!uacpi_eval_simple_integer(adapter[i],"_PSR",&v) && v) info.ac_online=1; }
        if(lid) { uacpi_u64 v;if(!uacpi_eval_simple_integer(lid,"_LID",&v)) info.lid=v!=0; }
        panel_update();
        if(role_request>=0) {
            int port=role_request,src=role_source;role_request=-1;
            if(ucsi_power_role(&info.usbc,(unsigned)port,src)<0) status("USB-C power-role request rejected or timed out");
        }
        if(info.usbc.ready && !info.usbc.failed) ucsi_refresh(&info.usbc);
        info.aml_bytes=aml_host_bytes();info.updated_ms=timer_ms();info.generation++;
        for(unsigned i=0;i<20;i++) {
            ec_query();
            if(brightness_request>=0 || role_request>=0) break;
            task_sleep_ms(100);
        }
    }
}
void laptop_init(void) {
    if(task_create_stack("laptop-power",laptop_service,128u<<10)<0) status("Laptop power task unavailable");
}
void laptop_get_info(laptop_info_t* out) {
    if(!out) return;
    /* BSP cooperative tasks: this copy never yields, so readers see one state. */
    memcpy(out,&info,sizeof(*out));
    out->usbc.ops=NULL;out->usbc.ctx=NULL;
    driver_power_merge(out);
}
int laptop_backlight(int percent) {
    if(!panel) return driver_power_backlight(percent);
    if(percent<0) return info.brightness;
    if(percent>100) return -1;
    brightness_request=percent;info.brightness_pending=1;info.generation++;
    return percent;
}
int laptop_backlight_step(int delta) {
    if(!delta) return -1;
    if(!panel || info.brightness<0) {
        int value=driver_power_backlight(-1);if(value<0) return -1;
        value+=delta;return driver_power_backlight(value<0?0:value>100?100:value);
    }
    int current=info.brightness,best=current;
    for(unsigned i=0;i<panel_nlevels;i++) {
        int level=(int)panel_levels[i];
        if(delta>0 && level>current && (best==current || level<best)) best=level;
        if(delta<0 && level<current && (best==current || level>best)) best=level;
    }
    return best==current?current:laptop_backlight(best);
}
int laptop_usbc_role(unsigned port,int source) {
    if(!info.usbc.ready || info.usbc.failed || !port || port>info.usbc.num_ports || role_request>=0) return -1;
    if(!info.usbc.ports[port-1].valid || !info.usbc.ports[port-1].connected ||
       !(info.usbc.role_capabilities[port-1]&(source?(1u<<4):(1u<<5)))) return -1;
    role_source=source!=0;role_request=(int)port;return 0;
}
