/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ASIX AX88179 vendor protocol. Register/framing reference:
 * Linux v6.12 drivers/net/usb/ax88179_178a.c (GPL-2.0-or-later),
 * Copyright (C) 2011-2013 ASIX. No Linux kernel dependency.
 * AX88179A in CDC-NCM/ECM mode belongs to the corresponding class driver.
 */
#include "usbnet.h"
#include "kstring.h"
#include "kheap.h"
#include "timer.h"
#include "serial.h"

#define AX_MAC 1
#define AX_PHY 2
#define AX_RX_CTL 0x0b
#define AX_NODE_ID 0x10
#define AX_MEDIUM 0x22
#define AX_PHY_POWER 0x26
#define AX_QCTRL 0x2e
#define AX_CLOCK 0x33
#define AX_RXCOE 0x34
#define AX_TXCOE 0x35
#define AX_RX_FLAGS 0x03aa /* CRC drop, IP alignment, start, physical/all multicast/broadcast */
#define AX_RX_SIZE 32768

typedef struct {
    usb_ep_desc_t in, out;
    uint8_t iface, alt;
    int vendor, have_in, have_out, done;
} ax_eps_t;
typedef struct { int ok; uint16_t phy; } ax_priv_t;
static uint16_t ax_le16(const uint8_t* p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t ax_le32(const uint8_t* p) { return ax_le16(p) | (uint32_t)ax_le16(p+2) << 16; }
static void ax_put32(uint8_t* p, uint32_t v) {
    for (int i=0;i<4;i++) p[i]=(uint8_t)(v>>(i*8));
}
static int ax_eps(const uint8_t* d, void* ctx) {
    ax_eps_t* e=ctx;
    if (e->done) return 1;
    if (d[1]==USB_DT_INTERFACE && d[0]>=sizeof(usb_iface_desc_t)) {
        const usb_iface_desc_t* id=(const usb_iface_desc_t*)d;
        e->vendor=id->bInterfaceClass==0xff;
        e->iface=id->bInterfaceNumber; e->alt=id->bAlternateSetting;
        e->have_in=e->have_out=0;
    } else if (d[1]==USB_DT_ENDPOINT && d[0]>=sizeof(usb_ep_desc_t) && e->vendor) {
        const usb_ep_desc_t* ep=(const usb_ep_desc_t*)d;
        if ((ep->bmAttributes&3)!=USB_EP_BULK || !(ep->bEndpointAddress&15)) return 0;
        if (ep->bEndpointAddress&0x80) { e->in=*ep; e->have_in=1; }
        else { e->out=*ep; e->have_out=1; }
        if (e->have_in && e->have_out) { e->done=1; return 1; }
    }
    return 0;
}
static int ax_config(usb_device_t* d, int c, ax_eps_t* e) {
    memset(e,0,sizeof(*e));
    return usb_walk_config(d,c,ax_eps,e)>=0 && e->done ? 0 : -1;
}
static int ax_probe(usb_device_t* d) {
    if (d->desc.idVendor!=0x0b95 || d->desc.idProduct!=0x1790 || d->desc.bcdDevice!=0x0100) return -1;
    for(int c=0;c<USB_MAX_CONFIGS;c++) {
        ax_eps_t e;
        if(d->configs[c] && !ax_config(d,c,&e)) return c;
    }
    return -1;
}
static int ax_ctl(usbnet_t* u,int read,uint8_t req,uint16_t value,uint16_t index,void* data,uint16_t n) {
    ax_priv_t* a=u->priv;
    int rc=usb_control(u->dev,(read?USB_DIR_IN:0)|USB_TYPE_VENDOR|USB_RECIP_DEVICE,
                       req,value,index,data,n);
    if(rc!=(int)n) { a->ok=0; return -1; }
    return 0;
}
static void ax_wr(usbnet_t* u,uint16_t reg,uint32_t value,uint16_t n) {
    uint8_t b[4]; ax_put32(b,value);
    ax_ctl(u,0,AX_MAC,reg,n,b,n);
}
static int ax_link(usbnet_t* u) {
    ax_priv_t* a=u->priv;
    uint8_t b[2]={0};
    a->ok=1;
    if(ax_ctl(u,1,AX_PHY,3,0x11,b,2)<0) return 0;
    uint16_t phy=ax_le16(b);
    if (!(phy&0x0400)) { a->phy=0; return 0; }
    if(phy!=a->phy) {
        static const uint8_t queues[4][5]={
            {7,0x4f,0,0x12,0xff},{7,0x20,3,0x16,0xff},
            {7,0xae,7,0x18,0xff},{7,0xcc,0x4c,0x18,8}};
        uint16_t mode=0x0130;
        int q=3;
        if((phy&0xc000)==0x8000) {
            mode|=0x0009;
            q=u->dev->speed==USB_SPEED_SUPER?0:u->dev->speed==USB_SPEED_HIGH?1:3;
        } else if((phy&0xc000)==0x4000) {
            mode|=0x0200; q=u->dev->speed==USB_SPEED_FULL?3:2;
        }
        if(phy&0x2000) mode|=2;
        /* Stop/restart the receive engine on a negotiated link change. */
        ax_wr(u,AX_RX_CTL,0,2); ax_wr(u,AX_RX_CTL,AX_RX_FLAGS,2);
        ax_ctl(u,0,AX_MAC,AX_QCTRL,5,(void*)queues[q],5);
        ax_wr(u,AX_MEDIUM,mode,2);
        if(!a->ok) return 0;
        a->phy=phy;
    }
    return 1;
}
static void ax_decap(usbnet_t* u,const uint8_t* b,uint32_t n) {
    if(n<4) { u->nd.rx_errors++; return; }
    uint32_t tail=ax_le32(b+n-4), count=tail&0xffff, table=tail>>16;
    if(!count || table>n-4 || count>(n-4-table)/4) { u->nd.rx_errors++; return; }
    uint32_t at=0;
    /* Validate all spans before delivering anything from the aggregate. */
    for(uint32_t i=0;i<count;i++) {
        uint32_t size=(ax_le32(b+table+i*4)>>16)&0x1fff;
        uint32_t padded=(size+7)&~7u;
        if(at>table || padded>table-at) { u->nd.rx_errors++; return; }
        at+=padded;
    }
    at=0;
    for(uint32_t i=0;i<count;i++) {
        uint32_t h=ax_le32(b+table+i*4), size=(h>>16)&0x1fff;
        if(size) {
            if((h&0xa0000000u) || size<16 || size>ETH_FRAME_MAX+2) u->nd.rx_errors++;
            else {
                u->nd.rx_packets++; u->nd.rx_bytes+=size-2;
                net_rx(&u->nd,b+at+2,size-2);
            }
        }
        at+=(size+7)&~7u;
    }
}
static int ax_encap(usbnet_t* u,uint8_t* out,const uint8_t* frame,uint32_t n) {
    if(n<14 || n>ETH_FRAME_MAX || n+8>u->tx_size) return -1;
    ax_put32(out,n);
    ax_put32(out+4,u->mps_out && (n+8)%u->mps_out==0 ? 0x80008000u : 0);
    memcpy(out+8,frame,n);
    return (int)n+8;
}
static int ax_attach(usb_device_t* d) {
    if(d->desc.idVendor!=0x0b95 || d->desc.idProduct!=0x1790 || d->desc.bcdDevice!=0x0100) return -1;
    ax_eps_t e;
    if(ax_config(d,d->active_config,&e)) return -1;
    usbnet_t* u=kzalloc(sizeof(*u)); ax_priv_t* a=kzalloc(sizeof(*a));
    if(!u || !a) { kfree(u); kfree(a); return -1; }
    u->dev=d; u->priv=a; a->ok=1;
    if(usb_control(d,USB_TYPE_STANDARD|USB_RECIP_IFACE,USB_REQ_SET_INTERFACE,e.alt,e.iface,0,0)<0) goto fail;
    ax_wr(u,AX_PHY_POWER,0,2); ax_wr(u,AX_PHY_POWER,0x20,2); timer_sleep_ms(500);
    ax_wr(u,AX_CLOCK,3,1); timer_sleep_ms(200);
    ax_wr(u,AX_RX_CTL,0,2);
    if(ax_ctl(u,1,AX_MAC,AX_NODE_ID,6,u->nd.mac,6)<0) goto fail;
    uint8_t any=0, all=0xff;
    for(int i=0;i<6;i++) { any|=u->nd.mac[i]; all&=u->nd.mac[i]; }
    if(!any || all==0xff || (u->nd.mac[0]&1)) goto fail;
    /* Software checksums only, matching the existing network stack. */
    ax_wr(u,AX_RXCOE,0,1); ax_wr(u,AX_TXCOE,0,1);
    ax_wr(u,0x55,0x34,1); ax_wr(u,0x54,0x52,1);
    uint8_t q[5]={7,0x4f,0,0x12,0xff};
    ax_ctl(u,0,AX_MAC,AX_QCTRL,5,q,5);
    ax_wr(u,0x24,0x64,1);
    ax_wr(u,AX_MEDIUM,0x0133,2);
    ax_wr(u,AX_RX_CTL,AX_RX_FLAGS,2);
    /* Disable the PHY EEE mode (ASIX page 3, register 25), then autoneg. */
    uint8_t page[2]={3,0}, eee[2]={0x46,0x32}, zero[2]={0,0};
    ax_ctl(u,0,AX_PHY,3,0x1f,page,2);
    ax_ctl(u,0,AX_PHY,3,0x19,eee,2);
    ax_ctl(u,0,AX_PHY,3,0x1f,zero,2);
    uint8_t phy[2]={0x00,0x12};
    ax_ctl(u,0,AX_PHY,3,0,phy,2);
    if(!a->ok) goto fail;
    if(usb_open_endpoint(d,&e.in) || usb_open_endpoint(d,&e.out)) goto fail;
    u->ep_in=e.in.bEndpointAddress; u->ep_out=e.out.bEndpointAddress;
    u->mps_out=e.out.wMaxPacketSize&0x7ff;
    if(!u->mps_out) goto fail;
    u->rx_size=AX_RX_SIZE; u->tx_size=ETH_FRAME_MAX+8;
    u->decap=ax_decap; u->encap=ax_encap; u->get_link=ax_link;
    u->nd.name="ax88179"; u->nd.model="ASIX AX88179 USB Gigabit Ethernet";
    if(usbnet_start(u)<0) goto fail;
    d->drvdata=u;
    klog("ax88179: initialized; negotiated link checked through PHY status\n");
    return 0;
fail:
    ax_wr(u,AX_RX_CTL,0,2);
    if(!u->submitted) { kfree(a); kfree(u); }
    return -1;
}
static void ax_detach(usb_device_t* d) {
    if(d->drvdata) usbnet_stop(d->drvdata);
}
const usb_driver_t ax88179_driver={"ax88179",ax_probe,ax_attach,0,ax_detach};
