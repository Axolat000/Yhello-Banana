/* SPDX-License-Identifier: GPL-3.0-or-later */
/* USB CDC-NCM 1.0, NTB16 / Ethernet datagrams without CRC.
 * References: USB CDC NCM transfer layouts; Linux v6.12 usb/cdc.h and
 * drivers/net/usb/cdc_ncm.c (GPL-2.0-or-later). This is a native driver.
 * Class binding also handles AX88179A when it exposes CDC-NCM descriptors.
 */
#include "usbnet.h"
#include "kstring.h"
#include "kheap.h"
#include "timer.h"
#include "serial.h"

#define NCM_MAX_NTB 32768
#define NCM_FRAMES 512
#define NCM_TABLES 32
typedef struct {
    uint8_t ctrl, data, alt, mac, caps;
    int found, in_ctrl, in_data, got_mac, got_ncm, got_union, done;
    int have_in, have_out, have_notify;
    usb_ep_desc_t in, out, notify;
} ncm_eps_t;
typedef struct {
    uint8_t ctrl;
    uint16_t divisor, remainder, alignment, sequence;
    int link;
    usb_xfer_t notify;
    uint32_t retry_ms;
} ncm_priv_t;
static uint16_t nc16(const uint8_t* p) { return p[0]|(uint16_t)p[1]<<8; }
static uint32_t nc32(const uint8_t* p) { return nc16(p)|(uint32_t)nc16(p+2)<<16; }
static void np16(uint8_t* p,uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void np32(uint8_t* p,uint32_t v) { np16(p,(uint16_t)v); np16(p+2,(uint16_t)(v>>16)); }
static int power2(uint32_t n) { return n && !(n&(n-1)); }
static uint32_t aligned(uint32_t n,uint32_t a) { return (n+a-1)&~(a-1); }
static int ncm_walk(const uint8_t* b,void* ctx) {
    ncm_eps_t* e=ctx;
    if(e->done) return 1;
    if(b[1]==USB_DT_INTERFACE && b[0]>=sizeof(usb_iface_desc_t)) {
        const usb_iface_desc_t* i=(const usb_iface_desc_t*)b;
        e->in_ctrl=i->bInterfaceClass==2 && i->bInterfaceSubClass==0x0d &&
                   (!e->found || e->ctrl==i->bInterfaceNumber);
        if(e->in_ctrl && !e->found) { e->found=1; e->ctrl=i->bInterfaceNumber; e->data=0xff; }
        e->in_data=e->found && e->got_union && i->bInterfaceClass==0x0a && i->bInterfaceNumber==e->data;
        e->have_in=e->have_out=0;
        if(e->in_data) e->alt=i->bAlternateSetting;
    } else if(b[1]==USB_DT_CS_INTERFACE && e->in_ctrl && b[0]>=3) {
        if(b[2]==6 && b[0]>=5 && b[3]==e->ctrl) { e->data=b[4]; e->got_union=1; }
        if(b[2]==0x0f && b[0]>=13) { e->mac=b[3]; e->got_mac=1; }
        if(b[2]==0x1a && b[0]>=6) { e->caps=b[5]; e->got_ncm=1; }
    } else if(b[1]==USB_DT_ENDPOINT && b[0]>=sizeof(usb_ep_desc_t)) {
        const usb_ep_desc_t* ep=(const usb_ep_desc_t*)b;
        if(!(ep->bEndpointAddress&15)) return 0;
        if(e->in_ctrl && (ep->bmAttributes&3)==USB_EP_INTERRUPT && (ep->bEndpointAddress&0x80)) {
            e->notify=*ep; e->have_notify=1;
        }
        if(e->in_data && (ep->bmAttributes&3)==USB_EP_BULK) {
            if(ep->bEndpointAddress&0x80) { e->in=*ep; e->have_in=1; }
            else { e->out=*ep; e->have_out=1; }
            if(e->have_in && e->have_out) { e->done=1; return 1; }
        }
    }
    return 0;
}
static int ncm_parse(usb_device_t* d,int c,ncm_eps_t* e) {
    memset(e,0,sizeof(*e));
    if(usb_walk_config(d,c,ncm_walk,e)<0) return -1;
    return e->done && e->got_mac && e->got_ncm && e->have_notify ? 0 : -1;
}
static int ncm_probe(usb_device_t* d) {
    for(int c=0;c<USB_MAX_CONFIGS;c++) {
        ncm_eps_t e;
        if(d->configs[c] && !ncm_parse(d,c,&e)) return c;
    }
    return -1;
}
static int overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {
    return a<b+bn && b<a+an;
}
static void ncm_decap(usbnet_t* u,const uint8_t* b,uint32_t n) {
    uint16_t frames[NCM_FRAMES][2], tables[NCM_TABLES][2];
    unsigned nf=0,nt=0;
    if(n<12 || nc32(b)!=0x484d434e || nc16(b+4)!=12) goto bad;
    uint32_t block=nc16(b+8), next=nc16(b+10);
    if(block<12 || block>n || block>u->rx_size || !next) goto bad;
    while(next) {
        if(nt==NCM_TABLES || (next&3) || next<12 || next>block-8) goto bad;
        for(unsigned j=0;j<nt;j++) if(tables[j][0]==next) goto bad;
        if(nc32(b+next)!=0x304d434e) goto bad; /* negotiated no-CRC */
        uint32_t len=nc16(b+next+4);
        if(len<12 || (len&3) || len>block-next) goto bad;
        for(unsigned j=0;j<nt;j++) if(overlap(next,len,tables[j][0],tables[j][1])) goto bad;
        tables[nt][0]=(uint16_t)next; tables[nt++][1]=(uint16_t)len;
        int terminated=0;
        for(uint32_t at=next+8;at<next+len;at+=4) {
            uint32_t off=nc16(b+at), size=nc16(b+at+2);
            if(!off && !size) { terminated=1; break; }
            if(!off || nf==NCM_FRAMES || size<14 || size>ETH_FRAME_MAX ||
               off<12 || off>block || size>block-off) goto bad;
            for(unsigned j=0;j<nf;j++) if(overlap(off,size,frames[j][0],frames[j][1])) goto bad;
            frames[nf][0]=(uint16_t)off; frames[nf++][1]=(uint16_t)size;
        }
        if(!terminated) goto bad;
        next=nc16(b+next+6);
    }
    for(unsigned i=0;i<nf;i++) for(unsigned j=0;j<nt;j++)
        if(overlap(frames[i][0],frames[i][1],tables[j][0],tables[j][1])) goto bad;
    for(unsigned i=0;i<nf;i++) {
        u->nd.rx_packets++; u->nd.rx_bytes+=frames[i][1];
        net_rx(&u->nd,b+frames[i][0],frames[i][1]);
    }
    return;
bad: u->nd.rx_errors++;
}
static int ncm_encap(usbnet_t* u,uint8_t* b,const uint8_t* frame,uint32_t size) {
    ncm_priv_t* p=u->priv;
    if(size<14 || size>ETH_FRAME_MAX) return -1;
    uint32_t table=aligned(12,p->alignment), data=table+16;
    /* NCM's divisor/remainder aligns the payload after Ethernet's header. */
    uint32_t rem=(p->remainder+p->divisor-14%p->divisor)%p->divisor;
    data+=(rem+p->divisor-data%p->divisor)%p->divisor;
    if(data>u->tx_size || size>u->tx_size-data || data+size>65535) return -1;
    uint32_t total=data+size;
    memset(b,0,total);
    np32(b,0x484d434e); np16(b+4,12); np16(b+6,p->sequence++);
    np16(b+8,(uint16_t)total); np16(b+10,(uint16_t)table);
    np32(b+table,0x304d434e); np16(b+table+4,16);
    np16(b+table+8,(uint16_t)data); np16(b+table+10,(uint16_t)size);
    memcpy(b+data,frame,size);
    return (int)total;
}
static int ncm_link(usbnet_t* u) { return ((ncm_priv_t*)u->priv)->link; }
static void ncm_poll(usb_device_t* d) {
    usbnet_t* u=d->drvdata;
    if(!u || !d->present) return;
    ncm_priv_t* p=u->priv; usb_xfer_t* x=&p->notify;
    if(x->status==USB_XFER_PENDING || x->status==USB_XFER_GONE) return;
    if(x->status==USB_XFER_OK && x->actual>=8 && x->buf[0]==0xa1 &&
       nc16(x->buf+4)==p->ctrl && x->buf[1]==0 && nc16(x->buf+6)==0) {
        p->link=nc16(x->buf+2)!=0;
        u->link=p->link; u->link_checked_ms=timer_ms();
        net_irq_notify();
    }
    if(x->status<0 && timer_ms()-p->retry_ms<1000) return;
    x->len=16; p->retry_ms=timer_ms();
    if(usb_submit(x)<0) x->status=USB_XFER_ERROR;
}
static int ncm_hex(char c) {
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static int ncm_attach(usb_device_t* d) {
    ncm_eps_t e;
    if(ncm_parse(d,d->active_config,&e)) return -1;
    uint8_t params[28]={0};
    if(usb_control(d,0xa1,0x80,0,e.ctrl,params,28)!=28 ||
       nc16(params)<28 || !(nc16(params+2)&1)) return -1;
    uint32_t in=nc32(params+4), out=nc32(params+16);
    uint32_t div=nc16(params+20), rem=nc16(params+22), align=nc16(params+24);
    if(in<2048 || out<2048 || !power2(div) || div>4096 || rem>=div ||
       !power2(align) || align<4 || align>4096) return -1;
    if(in>NCM_MAX_NTB) in=NCM_MAX_NTB;
    if(out>NCM_MAX_NTB) out=NCM_MAX_NTB;
    usbnet_t* u=kzalloc(sizeof(*u)); ncm_priv_t* p=kzalloc(sizeof(*p));
    if(!u || !p) { kfree(u); kfree(p); return -1; }
    u->dev=d; u->priv=p; p->ctrl=e.ctrl;
    p->divisor=(uint16_t)div; p->remainder=(uint16_t)rem; p->alignment=(uint16_t)align;
    char mac[16]={0}; uint8_t any=0;
    if(!e.mac || usb_get_string(d,e.mac,mac,sizeof(mac))<0 || strlen(mac)!=12) goto fail;
    for(int i=0;i<6;i++) {
        int hi=ncm_hex(mac[2*i]),lo=ncm_hex(mac[2*i+1]);
        if(hi<0 || lo<0) goto fail;
        u->nd.mac[i]=(uint8_t)(hi*16+lo); any|=u->nd.mac[i];
    }
    if(!any || (u->nd.mac[0]&1)) goto fail;
    if((nc16(params+2)&2) && usb_control(d,0x21,0x84,0,e.ctrl,0,0)<0) goto fail; /* NTB16 */
    if((e.caps&0x10) && usb_control(d,0x21,0x8a,0,e.ctrl,0,0)<0) goto fail;
    uint8_t limit[4]; np32(limit,in);
    if(usb_control(d,0x21,0x86,0,e.ctrl,limit,4)!=4) goto fail;
    if(usb_control(d,USB_RECIP_IFACE,USB_REQ_SET_INTERFACE,e.alt,e.data,0,0)<0) goto fail;
    if(usb_open_endpoint(d,&e.in) || usb_open_endpoint(d,&e.out) || usb_open_endpoint(d,&e.notify)) goto fail;
    if(usb_control(d,0x21,0x43,0x0e,e.ctrl,0,0)<0) goto fail;
    u->ep_in=e.in.bEndpointAddress; u->ep_out=e.out.bEndpointAddress;
    u->mps_out=e.out.wMaxPacketSize&0x7ff;
    if(!u->mps_out) goto fail;
    u->rx_size=in; u->tx_size=out; u->zlp=1;
    u->decap=ncm_decap; u->encap=ncm_encap; u->get_link=ncm_link;
    u->nd.name="cdc_ncm"; u->nd.model="USB CDC Ethernet (NCM/NTB16)";
    p->notify.dev=d; p->notify.ep=e.notify.bEndpointAddress;
    p->notify.buf=usb_dma_alloc(16); p->notify.len=16; p->notify.status=USB_XFER_OK;
    if(!p->notify.buf) goto fail;
    /* Check that at least one full-size Ethernet frame fits negotiated TX. */
    uint32_t table=aligned(12,align), data=table+16;
    uint32_t r=(rem+div-14%div)%div;
    data+=(r+div-data%div)%div;
    if(data>out || ETH_FRAME_MAX>out-data) goto fail;
    if(usbnet_start(u)<0) goto fail;
    d->drvdata=u;
    if(usb_submit(&p->notify)<0) p->notify.status=USB_XFER_ERROR;
    klog("cdc_ncm: NTB16 initialized, waiting for a link notification\n");
    return 0;
fail:
    kfree(p); kfree(u); return -1;
}
static void ncm_detach(usb_device_t* d) {
    if(d->drvdata) usbnet_stop(d->drvdata);
}
const usb_driver_t cdc_ncm_driver={"cdc_ncm",ncm_probe,ncm_attach,ncm_poll,ncm_detach};
