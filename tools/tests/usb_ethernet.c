/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Driver-level USB host/device model: EP0 negotiation, URBs, packet
 * aggregates and link notifications, including malformed external input.
 * The actual USB Ethernet drivers and usbnet data path run unmodified.
 */
#include "../../net/usbnet.h"
#include "../../kernel/kstring.h"
extern int printf(const char*,...);
extern void* calloc(size_t,size_t);
extern void free(void*);
extern void abort(void);
static void* allocations[256];
static unsigned nalloc, checks, received, registered, submits, irqs;
static uint32_t now=5000, txlen;
static uint8_t txdata[32768], last_rx[1514], macregs[256], phyregs[64];
static int short_read, bad_write, fail_request=-1, ep_fail, formats=3;
static int proto, link_at_registration;
static usbnet_t* device;
static usb_device_t* active;
static int format_calls, input_calls, filter_calls, interface_calls;
static uint32_t input_limit;
#define CHECK(c) do { checks++; if(!(c)) { printf("FAIL line %d: %s\n",__LINE__,#c); abort(); } } while(0)
void* kmalloc(size_t n) {
    void* p=calloc(1,n);
    if(p) { CHECK(nalloc<256); allocations[nalloc++]=p; }
    return p;
}
void* kzalloc(size_t n) { return kmalloc(n); }
void kfree(void* p) {
    for(unsigned i=0;i<nalloc;i++) if(allocations[i]==p) {
        free(p); allocations[i]=allocations[--nalloc]; return;
    }
}
void* usb_dma_alloc(uint32_t n) { return kmalloc(n); }
uint32_t timer_ms(void) { return now; }
void timer_sleep_ms(uint32_t ms) { now+=ms; }
void usb_delay_ms(uint32_t ms) { now+=ms; }
void klog(const char* fmt,...) { (void)fmt; }
void net_irq_notify(void) { irqs++; }
void net_rx(netdev_t* nd,const uint8_t* frame,uint32_t n) {
    CHECK(nd==&device->nd); CHECK(n<=sizeof(last_rx));
    received++; memcpy(last_rx,frame,n);
}
void net_register_device(netdev_t* nd) {
    registered++; device=(usbnet_t*)nd; nd->present=1;
    link_at_registration=nd->link_up(nd);
}
void net_unregister_device(netdev_t* nd) { nd->present=0; }
int usb_get_string(usb_device_t* d,uint8_t index,char* out,uint32_t cap) {
    (void)d;
    if(index!=7 || cap<13) return -1;
    memcpy(out,"02aabbccddee",13); return 0;
}
int usb_open_endpoint(usb_device_t* d,const usb_ep_desc_t* ep) {
    (void)d; CHECK((ep->bEndpointAddress&15)!=0); return ep_fail?-1:0;
}
int usb_submit(usb_xfer_t* x) {
    CHECK(x->dev && x->dev->present); submits++;
    x->status=USB_XFER_PENDING; x->actual=0;
    if(!(x->ep&0x80)) {
        if(x->len) { CHECK(x->len<=sizeof(txdata)); memcpy(txdata,x->buf,x->len); txlen=x->len; }
        x->actual=x->len; x->status=USB_XFER_OK;
    }
    return 0;
}
void usb_poll(void) {
    if(active && active->driver && active->driver->poll) active->driver->poll(active);
}
int usb_walk_config(usb_device_t* d,int c,int (*cb)(const uint8_t*,void*),void* ctx) {
    const uint8_t* b=d->configs[c];
    if(!b || b[0]<9) return -1;
    uint32_t n=b[2]|(uint32_t)b[3]<<8;
    for(uint32_t off=0;off<n;) {
        if(n-off<2 || b[off]<2 || b[off]>n-off) return -1;
        if(cb(b+off,ctx)) return 0;
        off+=b[off];
    }
    return 0;
}
static void put16(uint8_t* p,uint32_t v) { p[0]=v; p[1]=v>>8; }
static void put32(uint8_t* p,uint32_t v) { put16(p,v); put16(p+2,v>>16); }
int usb_control(usb_device_t* d,uint8_t type,uint8_t req,uint16_t value,uint16_t index,void* data,uint16_t n) {
    CHECK(d->present);
    if(req==fail_request) return -1;
    if(type==USB_RECIP_IFACE && req==USB_REQ_SET_INTERFACE) { interface_calls++; return 0; }
    if(proto==1 && (type&0x60)==USB_TYPE_VENDOR) {
        uint8_t* regs=req==1?macregs:phyregs;
        uint32_t off=req==1?value:index*2;
        CHECK(req==1 || (req==2 && value==3));
        CHECK(off+n<=(req==1?sizeof(macregs):sizeof(phyregs)));
        if(type&0x80) { memcpy(data,regs+off,n); return short_read?n-1:n; }
        if(bad_write) return -1;
        memcpy(regs+off,data,n); return n;
    }
    if(proto==2) {
        CHECK(index==0); /* NCM control interface */
        if(type==0xa1 && req==0x80) {
            CHECK(n==28); memset(data,0,28);
            uint8_t* p=data; put16(p,28); put16(p+2,formats);
            put32(p+4,32768); put16(p+8,4); put16(p+12,4);
            put32(p+16,32768); put16(p+20,8); put16(p+22,2); put16(p+24,8);
            return short_read?27:28;
        }
        CHECK(type==0x21);
        if(req==0x84) { CHECK(value==0 && n==0); format_calls++; }
        else if(req==0x8a) CHECK(value==0 && n==0);
        else if(req==0x86) { CHECK(n==4); input_limit=((uint8_t*)data)[0]|(uint32_t)((uint8_t*)data)[1]<<8; input_calls++; }
        else if(req==0x43) { CHECK(value==0x0e && n==0); filter_calls++; }
        else CHECK(0);
        return n;
    }
    CHECK(0); return -1;
}
#include "../../net/usbnet.c"
#include "../../net/ax88179.c"
#include "../../net/cdc_ncm.c"
#include "../../net/cdc_ecm.c"
#include "../../net/r8152.c"

static uint8_t vendor_config[]={
    9,2,32,0,1,1,0,0x80,50,
    9,4,0,0,2,0xff,0xff,0,0,
    7,5,0x81,2,0,2,0, 7,5,2,2,0,2,0
};
static uint8_t ncm_config[]={
    9,2,81,0,2,1,0,0x80,50,
    9,4,0,0,1,2,0x0d,0,0,
    5,0x24,0x06,0,1,
    13,0x24,0x0f,7,0,0,0,0,0xea,5,0,0,0,
    6,0x24,0x1a,0,1,0x10,
    7,5,0x83,3,16,0,9,
    9,4,1,0,0,0x0a,0,0,0,
    9,4,1,1,2,0x0a,0,0,0,
    7,5,0x81,2,0,2,0, 7,5,2,2,0,2,0
};
static void reset(usb_device_t* d,int protocol) {
    for(unsigned i=0;i<nalloc;i++) free(allocations[i]);
    nalloc=0;
    memset(d,0,sizeof(*d)); memset(macregs,0,sizeof(macregs)); memset(phyregs,0,sizeof(phyregs));
    d->present=1; d->speed=USB_SPEED_SUPER;
    d->desc.idVendor=0x0b95; d->desc.idProduct=0x1790; d->active_config=0;
    d->configs[0]=protocol==1?vendor_config:ncm_config;
    /* Keep declared lengths tied to actual descriptor sizes. */
    put16(d->configs[0]+2,protocol==1?sizeof(vendor_config):sizeof(ncm_config));
    proto=protocol; received=registered=submits=irqs=0; device=0; active=d;
    short_read=bad_write=ep_fail=0; fail_request=-1; formats=3;
    format_calls=input_calls=filter_calls=interface_calls=0; input_limit=0;
    uint8_t mac[6]={2,0xaa,0xbb,0xcc,0xdd,0xee}; memcpy(macregs+0x10,mac,6);
}
static void receive(usbnet_t* u,const uint8_t* b,uint32_t n) {
    CHECK(n<=u->rx_size); memcpy(u->rx[0].buf,b,n);
    u->rx[0].actual=n; u->rx[0].status=USB_XFER_OK;
    u->nd.poll(&u->nd);
}
static void fuzz_packets(usbnet_t* u,const uint8_t* seed,uint32_t size) {
    uint32_t rng=0x517ac03;
    for(unsigned trial=0;trial<10000;trial++) {
        rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
        uint32_t n=trial%3?size:rng%(size+1);
        uint8_t* b=calloc(1,n?n:1); CHECK(b);
        memcpy(b,seed,n);
        for(unsigned j=0;j<1+(trial%5) && n;j++) {
            rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
            b[rng%n]^=(uint8_t)(rng>>16);
        }
        u->decap(u,b,n); free(b);
    }
}
static void test_ax(void) {
    usb_device_t d; reset(&d,1);
    CHECK(ax88179_driver.probe(&d)==0); CHECK(cdc_ncm_driver.probe(&d)<0); CHECK(r8152_driver.probe(&d)<0);
    CHECK(ax88179_driver.attach(&d)==0); d.driver=&ax88179_driver;
    CHECK(registered==1 && !link_at_registration);
    CHECK(macregs[AX_RXCOE]==0 && macregs[AX_TXCOE]==0);
    put16(phyregs+0x22,0xa400); now+=1100;
    CHECK(device->nd.link_up(&device->nd)); CHECK((ax_le16(macregs+AX_MEDIUM)&0x10b)==0x10b);
    uint8_t frame[1514]; memset(frame,0x5a,sizeof(frame));
    CHECK(device->nd.send(&device->nd,frame,504)==0);
    CHECK(txlen==512 && ax_le32(txdata)==504 && ax_le32(txdata+4)==0x80008000u);
    CHECK(!memcmp(txdata+8,frame,504));
    CHECK(device->nd.send(&device->nd,frame,1514)==0 && txlen==1522);
    CHECK(device->nd.send(&device->nd,frame,1515)<0);
    uint8_t rx[256]={0};
    memcpy(rx+2,frame,60); memcpy(rx+66,frame,60);
    put32(rx+128,62u<<16); put32(rx+132,0x80000000); put32(rx+136,62u<<16);
    put32(rx+140,3u|(128u<<16));
    receive(device,rx,144); CHECK(received==2 && !memcmp(last_rx,frame,60));
    put32(rx+128,(62u<<16)|0x20000000u); receive(device,rx,144); CHECK(received==3);
    put32(rx+140,0xffffu|(128u<<16)); receive(device,rx,144); CHECK(received==3);
    put32(rx+140,3u|(120u<<16)); receive(device,rx,144); CHECK(received==3);
    fuzz_packets(device,rx,144);
    put16(phyregs+0x22,0); now+=1100; CHECK(!device->nd.link_up(&device->nd));
    ax88179_driver.detach(&d); CHECK(!device->nd.present);
    reset(&d,1); short_read=1; CHECK(ax88179_driver.attach(&d)<0 && !registered);
    reset(&d,1); bad_write=1; CHECK(ax88179_driver.attach(&d)<0 && !registered);
    reset(&d,1); d.desc.idProduct=0x178a; CHECK(ax88179_driver.probe(&d)<0);
    reset(&d,1); d.desc.idVendor=0x0bda; d.desc.idProduct=0x8152;
    CHECK(r8152_driver.probe(&d)==0 && ax88179_driver.probe(&d)<0);
}
static void test_ncm(void) {
    usb_device_t d; reset(&d,2);
    CHECK(cdc_ncm_driver.probe(&d)==0 && ax88179_driver.probe(&d)<0 && cdc_ecm_driver.probe(&d)<0);
    CHECK(cdc_ncm_driver.attach(&d)==0); d.driver=&cdc_ncm_driver;
    CHECK(!link_at_registration && format_calls==1 && input_limit==32768 && filter_calls==1);
    ncm_priv_t* p=device->priv;
    memset(p->notify.buf,0,16); p->notify.buf[0]=0xa1;
    put16(p->notify.buf+2,1); p->notify.actual=8; p->notify.status=USB_XFER_OK;
    device->nd.poll(&device->nd); CHECK(device->nd.link_up(&device->nd) && irqs==1);
    uint8_t frame[1514]; memset(frame,0xb7,sizeof(frame));
    CHECK(device->nd.send(&device->nd,frame,sizeof(frame))==0);
    CHECK(nc32(txdata)==0x484d434e && nc16(txdata+8)==txlen);
    uint32_t table=nc16(txdata+10),off=nc16(txdata+table+8);
    CHECK(!(table&7) && (off+14)%8==2 && !memcmp(txdata+off,frame,1514));
    receive(device,txdata,txlen); CHECK(received==1 && !memcmp(last_rx,frame,1514));
    uint8_t rx[512]={0};
    np32(rx,0x484d434e); np16(rx+4,12); np16(rx+8,184); np16(rx+10,16);
    np32(rx+16,0x304d434e); np16(rx+20,20);
    np16(rx+24,40); np16(rx+26,60); np16(rx+28,112); np16(rx+30,60);
    memcpy(rx+40,frame,60); memcpy(rx+112,frame,60);
    receive(device,rx,184); CHECK(received==3);
    np16(rx+22,16); receive(device,rx,184); CHECK(received==3); /* NDP cycle */
    np16(rx+22,0); np16(rx+28,40); receive(device,rx,184); CHECK(received==3); /* duplicate */
    np16(rx+28,20); receive(device,rx,184); CHECK(received==3); /* metadata overlap */
    np16(rx+28,172); receive(device,rx,184); CHECK(received==3); /* beyond block */
    np16(rx+28,112); np32(rx+16,0x314d434e); receive(device,rx,184); CHECK(received==3);
    np32(rx+16,0x304d434e); fuzz_packets(device,rx,184);
    p->notify.buf[2]=0; p->notify.actual=8; p->notify.status=USB_XFER_OK;
    device->nd.poll(&device->nd); CHECK(!device->nd.link_up(&device->nd));
    cdc_ncm_driver.detach(&d); CHECK(!device->nd.present);
    reset(&d,2); short_read=1; CHECK(cdc_ncm_driver.attach(&d)<0 && !registered);
    reset(&d,2); fail_request=0x86; CHECK(cdc_ncm_driver.attach(&d)<0 && !registered);
    reset(&d,2); formats=1; fail_request=0x84;
    CHECK(cdc_ncm_driver.attach(&d)==0 && format_calls==0); /* NTB16-only devices need no SET */
}
int main(void) {
    test_ax(); test_ncm();
    usb_device_t d; reset(&d,1);
    printf("USB Ethernet models: %u checks passed\n",checks);
    return 0;
}
