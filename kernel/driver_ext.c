/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Versioned bridges for external hardware modules. Kernel structs never form
 * part of the SDK ABI. Modules remain trusted/persistent ring-0 code. */
#define BANANA_KERNEL
#include "../driver-sdk/banana_driver.h"
#include "driver_ext.h"
#include "kheap.h"
#include "kstring.h"
#include "fs.h"
#include "gpu.h"
#include "task.h"
#include "timer.h"
#include "../net/netdev.h"
#include "../net/wifi.h"
#include "../usb/usbcore.h"
#define MODULE_NICS 16
typedef struct {
    bdrv_handle_t handle;int live,in_poll;
    bdrv_net_device_t d;netdev_t native;
} module_net_t;
typedef struct {
    bdrv_handle_t handle;int live,in_poll;
    bdrv_wifi_radio_t d;wifi_radio_t native;
} module_wifi_t;
static module_net_t* networks[MODULE_NICS];
static module_wifi_t* radios[MODULE_NICS];
static unsigned nnet,nradio;
static bdrv_handle_t next_cookie;
static bdrv_handle_t cookie(void) { if(!++next_cookie) ++next_cookie;return next_cookie; }
static int mac_valid(const uint8_t* m) {
    unsigned any=0;for(unsigned i=0;i<6;i++) any|=m[i];
    return any && !(m[0]&1);
}
static module_net_t* network(bdrv_handle_t h) {
    for(unsigned i=0;i<nnet;i++) if(networks[i]->live && networks[i]->handle==h) return networks[i];
    return NULL;
}
static module_wifi_t* radio(bdrv_handle_t h) {
    for(unsigned i=0;i<nradio;i++) if(radios[i]->live && radios[i]->handle==h) return radios[i];
    return NULL;
}
static int mn_send(netdev_t* n,const void* f,uint32_t size) {
    module_net_t* m=n->priv;
    return m->live && size>=14 && size<=ETH_FRAME_MAX?m->d.send(m->d.ctx,f,size):-1;
}
static int mn_poll(netdev_t* n) {
    module_net_t* m=n->priv;if(!m->live || m->in_poll) return 0;
    m->in_poll=1;int result=m->d.poll(m->d.ctx);m->in_poll=0;return result;
}
static int mn_link(netdev_t* n) { module_net_t* m=n->priv;return m->live && m->d.link_up(m->d.ctx)>0; }
static void mn_multicast(netdev_t* n,int all) {
    module_net_t* m=n->priv;if(m->live && m->d.set_multicast) m->d.set_multicast(m->d.ctx,all);
}
static bdrv_handle_t net_add(const bdrv_net_device_t* d) {
    if(!d || d->size<sizeof(*d) || !d->name || !d->send || !d->poll ||
       !d->link_up || !mac_valid(d->mac) || nnet==MODULE_NICS) return 0;
    module_net_t* m=kmalloc(sizeof(*m));if(!m) return 0;
    memset(m,0,sizeof(*m));m->d=*d;m->handle=cookie();m->live=1;
    m->native.name=d->name;m->native.model=d->model;m->native.is_usb=d->is_usb;
    memcpy(m->native.mac,d->mac,6);m->native.irq=0xff;m->native.priv=m;
    m->native.send=mn_send;m->native.poll=mn_poll;m->native.link_up=mn_link;
    m->native.set_multicast=mn_multicast;
    networks[nnet++]=m;net_register_device(&m->native);
    if(!m->native.present) { m->live=0;return 0; }return m->handle;
}
static void net_remove(bdrv_handle_t h) {
    module_net_t* m=network(h);if(m) { m->live=0;net_unregister_device(&m->native); }
}
static int net_receive(bdrv_handle_t h,const void* f,uint32_t n) {
    module_net_t* m=network(h);
    if(!m || !m->in_poll || !f || n<14 || n>ETH_FRAME_MAX) return -1;
    net_rx(&m->native,f,n);return 0;
}
static int mw_poll(wifi_radio_t* r) {
    module_wifi_t* m=r->hw;if(!m->live || m->in_poll) return 0;
    m->in_poll=1;int result=m->d.poll(m->d.ctx);m->in_poll=0;return result;
}
static int mw_send(wifi_radio_t* r,const uint8_t* f,uint32_t n) {
    module_wifi_t* m=r->hw;return m->live?m->d.send(m->d.ctx,f,n):-1;
}
static int mw_channel(wifi_radio_t* r,unsigned channel) {
    module_wifi_t* m=r->hw;return m->live?m->d.set_channel(m->d.ctx,channel):-1;
}
static int mw_station(wifi_radio_t* r,const uint8_t* bssid,unsigned aid) {
    module_wifi_t* m=r->hw;return m->live?m->d.set_station(m->d.ctx,bssid,aid):-1;
}
static const wifi_ops_t wifi_operations={mw_poll,mw_send,mw_channel,mw_station};
static bdrv_handle_t wifi_add(const bdrv_wifi_radio_t* d) {
    if(!d || d->size<sizeof(*d) || !d->name || !d->poll || !d->send ||
       !d->set_channel || !d->set_station || !mac_valid(d->mac) || nradio==MODULE_NICS) return 0;
    module_wifi_t* m=kmalloc(sizeof(*m));if(!m) return 0;
    memset(m,0,sizeof(*m));m->d=*d;m->handle=cookie();m->live=1;
    m->native.nd.name=d->name;m->native.nd.model=d->model;m->native.nd.is_usb=d->is_usb;
    memcpy(m->native.nd.mac,d->mac,6);m->native.nd.irq=0xff;
    m->native.ops=&wifi_operations;m->native.hw=m;radios[nradio++]=m;
    if(wifi_register(&m->native)<0) { m->live=0;return 0; }return m->handle;
}
static void wifi_remove(bdrv_handle_t h) {
    module_wifi_t* m=radio(h);
    if(m) { wifi_unregister(&m->native);m->live=0; }
}
static int wifi_receive(bdrv_handle_t h,const uint8_t* f,uint32_t n,int rssi) {
    module_wifi_t* m=radio(h);
    if(!m || !m->in_poll || !f || n<10 || n>WIFI_FRAME_MAX || rssi<-127 || rssi>0) return -1;
    wifi_rx(&m->native,f,n,rssi);return 0;
}

/* USB identifiers are attachment-generation cookies, not native slot pointers. */
#define MODULE_USB 16
#define MODULE_XFERS 128
static bdrv_usb_driver_t usb_drivers[MODULE_USB];
static unsigned nusbdrivers;
typedef struct {
    bdrv_handle_t handle;usb_device_t* d;bdrv_usb_driver_t* driver;void* ctx;
    int live,attached;uint32_t generation;
} module_usb_t;
static module_usb_t usb_devices[USB_MAX_DEVICES];
typedef struct {
    bdrv_handle_t handle,device;int used;usb_xfer_t native;
} module_xfer_t;
static module_xfer_t transfers[MODULE_XFERS];
static module_usb_t* usb_device(bdrv_handle_t h) {
    for(unsigned i=0;i<USB_MAX_DEVICES;i++) {
        module_usb_t* u=&usb_devices[i];
        if(u->live && u->handle==h && u->d->used && u->d->present && u->generation==u->d->generation) return u;
    }return NULL;
}
static module_xfer_t* usb_transfer(bdrv_handle_t h) {
    for(unsigned i=0;i<MODULE_XFERS;i++) if(transfers[i].used && transfers[i].handle==h) return &transfers[i];
    return NULL;
}
static module_usb_t* usb_native(usb_device_t* d) {
    for(unsigned i=0;i<USB_MAX_DEVICES;i++) if(usb_devices[i].live && usb_devices[i].d==d && usb_devices[i].generation==d->generation) return &usb_devices[i];
    return NULL;
}
static int usb_probe(usb_device_t* d) {
    for(unsigned i=0;i<USB_MAX_DEVICES;i++) {
        module_usb_t* old=&usb_devices[i];
        if(old->live && (!old->d->used || !old->d->present || old->generation!=old->d->generation)) old->live=0;
    }
    module_usb_t* u=usb_native(d);
    if(!u) {
        for(unsigned i=0;i<USB_MAX_DEVICES;i++) if(!usb_devices[i].live) {
            u=&usb_devices[i];memset(u,0,sizeof(*u));u->handle=cookie();u->live=1;u->d=d;u->generation=d->generation;break;
        }
    }
    if(!u) return -1;
    for(unsigned i=0;i<nusbdrivers;i++) {
        bdrv_usb_driver_t* drv=&usb_drivers[i];int match=0;
        for(unsigned j=0;j<drv->nids;j++) if(drv->ids[j].vendor==d->desc.idVendor && drv->ids[j].product==d->desc.idProduct) match=1;
        if(!match) continue;
        u->driver=drv;int cfg=drv->probe(drv->ctx,u->handle);
        if(cfg>=0 && cfg<USB_MAX_CONFIGS && d->configs[cfg]) return cfg;
    }
    u->live=0;return -1;
}
static int usb_attach(usb_device_t* d) {
    module_usb_t* u=usb_native(d);if(!u || !u->driver) return -1;
    void* ctx=NULL;int result=u->driver->attach(u->driver->ctx,u->handle,&ctx);
    if(result<0 || !d->present) {
        if(ctx && u->driver->detach) u->driver->detach(ctx);
        u->live=0;return -1;
    }
    u->ctx=ctx;u->attached=1;return 0;
}
static void usb_module_poll(usb_device_t* d) {
    module_usb_t* u=usb_native(d);if(u && u->attached && u->driver->poll) u->driver->poll(u->ctx);
}
static void usb_module_detach(usb_device_t* d) {
    module_usb_t* u=usb_native(d);if(!u) return;
    u->live=0;
    if(u->attached && u->driver->detach) u->driver->detach(u->ctx);
    u->attached=0;
}
static const usb_driver_t usb_module_driver={"driver-module",usb_probe,usb_attach,usb_module_poll,usb_module_detach};
static int usb_add(const bdrv_usb_driver_t* d) {
    if(!d || d->size<sizeof(*d) || !d->name || !d->ids || !d->nids || d->nids>64 ||
       !d->probe || !d->attach || nusbdrivers==MODULE_USB) return -1;
    for(unsigned i=0;i<d->nids;i++) {
        if(!d->ids[i].vendor || d->ids[i].vendor==0xffff || !d->ids[i].product || d->ids[i].product==0xffff) return -1;
        for(unsigned j=0;j<nusbdrivers;j++) for(unsigned k=0;k<usb_drivers[j].nids;k++)
            if(d->ids[i].vendor==usb_drivers[j].ids[k].vendor && d->ids[i].product==usb_drivers[j].ids[k].product) return -1;
    }
    if(usb_register_driver(&usb_module_driver)<0) return -1;
    usb_drivers[nusbdrivers++]=*d;return 0;
}
static int usb_information(bdrv_handle_t h,bdrv_usb_info_t* out) {
    module_usb_t* u=usb_device(h);
    if(!u || !out || out->size<sizeof(*out)) return -1;
    usb_device_t* d=u->d;memset(out,0,sizeof(*out));out->size=sizeof(*out);
    out->vendor=d->desc.idVendor;out->product=d->desc.idProduct;out->bcd_usb=d->desc.bcdUSB;
    out->bcd_device=d->desc.bcdDevice;out->device_class=d->desc.bDeviceClass;
    out->subclass=d->desc.bDeviceSubClass;out->protocol=d->desc.bDeviceProtocol;
    out->speed=(uint8_t)d->speed;out->address=d->address;out->configurations=d->desc.bNumConfigurations;
    kstrlcpy(out->manufacturer,d->manufacturer,sizeof(out->manufacturer));
    kstrlcpy(out->product_name,d->product,sizeof(out->product_name));return 0;
}
static int usb_configuration(bdrv_handle_t h,unsigned cfg,void* out,unsigned capacity) {
    module_usb_t* u=usb_device(h);if(!u || cfg>=USB_MAX_CONFIGS || !u->d->configs[cfg]) return -1;
    const uint8_t* p=u->d->configs[cfg];unsigned n=p[2]|(unsigned)p[3]<<8;
    if(n<9 || n>4096) return -1;
    if(!out && !capacity) return (int)n;
    if(!out || capacity<n) return -1;
    memcpy(out,p,n);return (int)n;
}
static int usb_request(bdrv_handle_t h,uint8_t rt,uint8_t request,uint16_t value,uint16_t index,void* data,uint16_t n) {
    module_usb_t* u=usb_device(h);
    if(!u || n>4096 || (n && !data)) return -1;
    return usb_control(u->d,rt,request,value,index,data,n);
}
static int usb_endpoint(bdrv_handle_t h,const bdrv_usb_endpoint_t* ep) {
    module_usb_t* u=usb_device(h);
    if(!u || !ep || ep->length<7 || ep->type!=5 || !(ep->address&15) || (ep->address&0x70) ||
       ((ep->attributes&3)!=USB_EP_BULK && (ep->attributes&3)!=USB_EP_INTERRUPT) ||
       !ep->max_packet || ep->max_packet>1024) return -1;
    usb_ep_desc_t e;memcpy(&e,ep,sizeof(e));return usb_open_endpoint(u->d,&e);
}
static void* usb_memory(uint32_t size) { return size && size<=65536?usb_dma_alloc(size):NULL; }
static bdrv_handle_t transfer_create(bdrv_handle_t dev,uint8_t ep,void* buf,uint32_t size) {
    module_usb_t* u=usb_device(dev);
    if(!u || !(ep&15) || (ep&0x70) || size>65536 || (size && !buf)) return 0;
    for(unsigned i=0;i<MODULE_XFERS;i++) if(!transfers[i].used) {
        module_xfer_t* t=&transfers[i];memset(t,0,sizeof(*t));t->used=1;t->handle=cookie();t->device=dev;
        t->native.dev=u->d;t->native.ep=ep;t->native.buf=buf;t->native.len=size;t->native.status=USB_XFER_ERROR;return t->handle;
    }return 0;
}
static int transfer_submit(bdrv_handle_t h) {
    module_xfer_t* t=usb_transfer(h);
    if(!t || t->native.status==USB_XFER_PENDING || !usb_device(t->device)) return -1;
    return usb_submit(&t->native);
}
static int transfer_status(bdrv_handle_t h,uint32_t* actual) {
    module_xfer_t* t=usb_transfer(h);if(!t) return BDRV_USB_GONE;
    if(actual) *actual=t->native.actual;
    return t->native.status;
}
static int transfer_release(bdrv_handle_t h) {
    module_xfer_t* t=usb_transfer(h);
    if(!t || t->native.status==USB_XFER_PENDING) return -1;
    t->used=0;return 0;
}

/* Cached vendor laptop providers fill only fields absent from native ACPI. */
#define POWER_PROVIDERS 4
typedef struct { bdrv_handle_t handle;int live; bdrv_power_provider_t d;bdrv_power_sample_t sample; } module_power_t;
static module_power_t powers[POWER_PROVIDERS];
static unsigned npower;
static int power_worker_started,power_request=-1;
static bdrv_handle_t power_request_provider;
static int terminated(const char* text,unsigned size) {
    for(unsigned i=0;i<size;i++) if(!text[i]) return 1;
    return 0;
}
static int sample_valid(const bdrv_power_sample_t* s) {
    if(s->size<sizeof(*s) || (s->flags&~15u) || s->battery_count>BDRV_POWER_BATTERIES) return 0;
    if((s->flags&BDRV_POWER_LIGHT) && (s->brightness<0 || s->brightness>100)) return 0;
    if((s->flags&BDRV_POWER_AC) && (s->ac_online<0 || s->ac_online>1)) return 0;
    if((s->flags&BDRV_POWER_LID) && (s->lid_open<0 || s->lid_open>1)) return 0;
    for(unsigned i=0;i<s->battery_count;i++) {
        const bdrv_battery_t* b=&s->batteries[i];
        if(b->percent < -1 || b->percent>100 || b->minutes < -1 || b->unit<0 || b->unit>1 ||
           (b->state&~7u) || !terminated(b->model,sizeof(b->model)) ||
           !terminated(b->serial,sizeof(b->serial)) || !terminated(b->chemistry,sizeof(b->chemistry))) return 0;
    }return 1;
}
static void power_poll_once(void) {
        for(unsigned i=0;i<npower;i++) {
            module_power_t* p=&powers[i];if(!p->live) continue;
            if(power_request>=0 && p->handle==power_request_provider) {
                int value=power_request;power_request=-1;
                int result=p->d.backlight?p->d.backlight(p->d.ctx,value):-1;
                if(result<0 || result>100) p->sample.flags&=~BDRV_POWER_LIGHT;
            }
            bdrv_power_sample_t next;memset(&next,0,sizeof(next));next.size=sizeof(next);
            next.brightness=-1;
            int result=p->d.sample(p->d.ctx,&next);
            if(p->live && !result && sample_valid(&next)) { next.updated_ms=timer_ms();p->sample=next; }
            else memset(&p->sample,0,sizeof(p->sample));
        }
}
static void power_worker(void) {
    task_set_background();
    for(;;) {
        power_poll_once();
        for(unsigned i=0;i<20 && power_request<0;i++) task_sleep_ms(100);
    }
}
static bdrv_handle_t power_add(const bdrv_power_provider_t* d) {
    if(!d || d->size<sizeof(*d) || !d->name || !d->sample || npower==POWER_PROVIDERS) return 0;
    if(!power_worker_started) {
        if(task_create_stack("driver-power",power_worker,64u<<10)<0) return 0;
        power_worker_started=1;
    }
    module_power_t* p=&powers[npower++];memset(p,0,sizeof(*p));
    p->d=*d;p->handle=cookie();p->live=1;return p->handle;
}
static void power_remove(bdrv_handle_t h) {
    for(unsigned i=0;i<npower;i++) if(powers[i].handle==h) {
        powers[i].live=0;powers[i].sample.flags=0;
        if(power_request_provider==h) power_request=-1;
    }
}
void driver_power_merge(laptop_info_t* out) {
    for(unsigned i=0;i<npower;i++) {
        module_power_t* p=&powers[i];bdrv_power_sample_t* s=&p->sample;
        if(!p->live || timer_ms()-s->updated_ms>10000) continue;
        if(!out->updated_ms && s->flags) out->updated_ms=s->updated_ms;
        if(!out->battery_count && (s->flags&BDRV_POWER_BATTERY)) {
            out->battery_count=(int)s->battery_count;
            for(unsigned j=0;j<s->battery_count;j++) {
                const bdrv_battery_t* a=&s->batteries[j];laptop_battery_t* b=&out->batteries[j];
                memcpy(b->model,a->model,sizeof(b->model));memcpy(b->serial,a->serial,sizeof(b->serial));memcpy(b->chemistry,a->chemistry,sizeof(b->chemistry));
                b->present=a->present;b->valid=a->valid;b->percent=a->percent;b->minutes=a->minutes;b->unit=a->unit;
                b->state=a->state;b->design=a->design;b->full=a->full;b->remaining=a->remaining;b->rate=a->rate;b->millivolts=a->millivolts;
            }
        }
        if(!out->ac_present && (s->flags&BDRV_POWER_AC)) { out->ac_present=1;out->ac_online=s->ac_online; }
        if(out->lid<0 && (s->flags&BDRV_POWER_LID)) out->lid=s->lid_open;
        if(out->brightness<0 && (s->flags&BDRV_POWER_LIGHT)) out->brightness=s->brightness;
    }
}
int driver_power_backlight(int percent) {
    for(unsigned i=0;i<npower;i++) {
        module_power_t* p=&powers[i];
        if(!p->live || !(p->sample.flags&BDRV_POWER_LIGHT) || timer_ms()-p->sample.updated_ms>10000) continue;
        if(percent<0) return p->sample.brightness;
        if(percent>100 || !p->d.backlight || power_request>=0) return -1;
        power_request_provider=p->handle;power_request=percent;return percent;
    }return -1;
}
static int usbc_snapshot(unsigned port,bdrv_usbc_status_t* out) {
    if(!out || out->size<sizeof(*out)) return -1;
    laptop_info_t p;laptop_get_info(&p);ucsi_t* u=&p.usbc;
    if(!port || port>u->num_ports) return -1;
    ucsi_port_t* c=&u->ports[port-1];
    memset(out,0,sizeof(*out));out->size=sizeof(*out);out->ucsi_version=u->version;
    out->pd_version=u->pd_version;out->port_count=u->num_ports;out->ready=u->ready && !u->failed;
    out->valid=c->valid && out->ready;out->connected=c->connected;out->source=c->source;
    out->mode=c->mode;out->charging=c->charging;out->contract_valid=c->contract_valid && out->valid;
    out->millivolts=c->millivolts;out->milliamps=c->milliamps;out->milliwatts=c->milliwatts;out->rdo=c->rdo;
    out->last_error=(uint32_t)u->last_error;out->ppm_error=u->ppm_error;return 0;
}
static int power_snapshot(bdrv_power_sample_t* out) {
    if(!out || out->size<sizeof(*out)) return -1;
    laptop_info_t p;laptop_get_info(&p);
    memset(out,0,sizeof(*out));out->size=sizeof(*out);out->updated_ms=p.updated_ms;
    if(p.ac_present) { out->flags|=BDRV_POWER_AC;out->ac_online=p.ac_online; }
    if(p.lid>=0) { out->flags|=BDRV_POWER_LID;out->lid_open=p.lid; }
    out->brightness=gpu_backlight(-1);
    if(out->brightness>=0) out->flags|=BDRV_POWER_LIGHT;
    if(p.battery_count>0) {
        out->flags|=BDRV_POWER_BATTERY;out->battery_count=(uint32_t)p.battery_count;
        for(int i=0;i<p.battery_count && i<BDRV_POWER_BATTERIES;i++) {
            laptop_battery_t* a=&p.batteries[i];bdrv_battery_t* b=&out->batteries[i];
            memcpy(b->model,a->model,sizeof(b->model));memcpy(b->serial,a->serial,sizeof(b->serial));memcpy(b->chemistry,a->chemistry,sizeof(b->chemistry));
            b->present=a->present;b->valid=a->valid;b->percent=a->percent;b->minutes=a->minutes;b->unit=a->unit;
            b->state=a->state;b->design=a->design;b->full=a->full;b->remaining=a->remaining;b->rate=a->rate;b->millivolts=a->millivolts;
        }
    }return 0;
}
static int firmware_read(const char* path,uint32_t offset,void* out,uint32_t n) {
    if(!path || path[0]!='/' || strlen(path)>=FS_PATH_LEN || (n && !out)) return -1;
    int f=fs_find_file(path);if(f<0) return -1;
    fs_pin(f);fs_file_t* file=fs_get_file(f);int result=-1;
    if(file && file->size<=16u*1024*1024) {
        if(!out && !n) result=(int)file->size;
        else if(offset<=file->size) {
            if(n>file->size-offset) n=file->size-offset;
            if(!n) result=0;
            else if(file->content) { memcpy(out,(const uint8_t*)file->content+offset,n);result=(int)n; }
        }
    }
    fs_unpin(f);return result;
}
static const bdrv_net_api_t net_api={1,sizeof(net_api),net_add,net_remove,net_receive};
static const bdrv_wifi_api_t wifi_api={1,sizeof(wifi_api),wifi_add,wifi_remove,wifi_receive};
static const bdrv_usb_api_t usb_api={1,sizeof(usb_api),usb_add,usb_information,usb_configuration,usb_request,usb_endpoint,usb_memory,transfer_create,transfer_submit,transfer_status,transfer_release};
static const bdrv_power_api_t power_api={1,sizeof(power_api),power_snapshot,power_add,power_remove,gpu_backlight,laptop_usbc_role,usbc_snapshot};
static const bdrv_firmware_api_t firmware_api={1,sizeof(firmware_api),firmware_read};
const void* driver_extension(uint32_t id,uint32_t version,uint32_t size) {
    const bdrv_ext_header_t* out=NULL;
    switch(id) {
    case BDRV_EXT_NET:out=(const void*)&net_api;break;
    case BDRV_EXT_WIFI:out=(const void*)&wifi_api;break;
    case BDRV_EXT_USB:out=(const void*)&usb_api;break;
    case BDRV_EXT_POWER:out=(const void*)&power_api;break;
    case BDRV_EXT_FIRMWARE:out=(const void*)&firmware_api;break;
    }
    return out && version<=out->version && size<=out->size?out:NULL;
}
