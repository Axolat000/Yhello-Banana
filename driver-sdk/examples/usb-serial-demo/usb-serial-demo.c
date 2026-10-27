/* SPDX-License-Identifier: BSD-2-Clause */
/* Independent USB module example; QEMU's emulated FT232 only, not a general
 * physical FTDI driver. Demonstrates late bind, descriptors, EP0, async TX/RX.
 * Host backend replies "BDK USB reply\n" after receiving "BDK USB ready\n". */
#include "banana_driver.h"
static const bdrv_usb_api_t* usb;
static const char ready[]="BDK USB ready\n",reply[]="BDK USB reply\n";
typedef struct {
    bdrv_handle_t device,tx,rx;
    uint8_t* txbuf;uint8_t* rxbuf;
    int tx_done,rx_done,failed;
    uint32_t started;
} serial_t;
static int same(const char* a,const char* b) {
    return bdrv_strlen(a)==bdrv_strlen(b) && !bdrv_memcmp(a,b,bdrv_strlen(a));
}
static int probe(void* ctx,bdrv_handle_t h) {
    (void)ctx;bdrv_usb_info_t info={.size=sizeof(info)};
    if(usb->info(h,&info)<0 || !same(info.manufacturer,"QEMU") ||
       !same(info.product_name,"QEMU USB SERIAL")) return -1;
    return 0;
}
static int attach(void* ctx,bdrv_handle_t h,void** out) {
    (void)ctx;
    int n=usb->configuration(h,0,0,0);if(n<9 || n>4096) return -1;
    uint8_t* desc=bdrv->alloc((unsigned long)n);if(!desc) return -1;
    if(usb->configuration(h,0,desc,(unsigned)n)!=n) { bdrv->free(desc);return -1; }
    bdrv_usb_endpoint_t in={0},tx={0};
    for(int off=0;off+2<=n;) {
        int length=desc[off];if(length<2 || off+length>n) break;
        if(desc[off+1]==5 && length>=7) {
            bdrv_usb_endpoint_t ep;bdrv_memcpy(&ep,desc+off,sizeof(ep));
            if(ep.address==0x81) in=ep;
            if(ep.address==2) tx=ep;
        }
        off+=length;
    }
    bdrv->free(desc);
    if(!in.length || !tx.length || usb->open_endpoint(h,&in)<0 || usb->open_endpoint(h,&tx)<0) return -1;
    uint8_t modem[2];
    if(usb->control(h,0xc0,5,0,0,modem,2)!=2) return -1;
    serial_t* s=bdrv->alloc(sizeof(*s));if(!s) return -1;
    *out=s;s->device=h;s->started=bdrv->ticks_ms();
    s->txbuf=usb->dma_alloc(64);s->rxbuf=usb->dma_alloc(64);
    if(!s->txbuf || !s->rxbuf) return -1;
    bdrv_memcpy(s->txbuf,ready,sizeof(ready)-1);
    s->tx=usb->transfer_create(h,2,s->txbuf,sizeof(ready)-1);
    s->rx=usb->transfer_create(h,0x81,s->rxbuf,64);
    if(!s->tx || !s->rx || usb->transfer_submit(s->rx)<0 || usb->transfer_submit(s->tx)<0) return -1;
    bdrv->log("usb-serial-demo: control passed; bulk transfers queued");return 0;
}
static void poll(void* ctx) {
    serial_t* s=ctx;if(!s || s->failed) return;
    uint32_t n=0;
    if(!s->tx_done) {
        int result=usb->transfer_status(s->tx,&n);
        if(result==BDRV_USB_OK && n==sizeof(ready)-1) {
            usb->transfer_release(s->tx);s->tx_done=1;
            bdrv->log("usb-serial-demo: TX bytes verified");
        } else if(result!=BDRV_USB_PENDING) s->failed=1;
    }
    if(!s->rx_done) {
        int result=usb->transfer_status(s->rx,&n);
        if(result==BDRV_USB_OK) {
            if(n>=sizeof(reply)+1 && !bdrv_memcmp(s->rxbuf+2,reply,sizeof(reply)-1)) {
                usb->transfer_release(s->rx);s->rx_done=1;
                bdrv->log("usb-serial-demo: RX bytes verified");
            } else s->failed=1;
        } else if(result!=BDRV_USB_PENDING) s->failed=1;
    }
    if(bdrv->ticks_ms()-s->started>5000 && (!s->tx_done || !s->rx_done)) s->failed=1;
    if(s->failed) bdrv->log("usb-serial-demo: transfer failed or timed out; pending DMA retained");
}
static void detach(void* ctx) {
    serial_t* s=ctx;if(!s) return;
    s->failed=1;
    if(s->tx) usb->transfer_release(s->tx);
    if(s->rx) usb->transfer_release(s->rx);
    /* The kit currently has no cancellation/quiesce primitive. Retain context
     * and DMA buffers after detach; never free memory still owned by the HC. */
    bdrv->log("usb-serial-demo: detached");
}
static const bdrv_usb_id_t ids[]={{0x0403,0x6001}};
static const bdrv_usb_driver_t driver={sizeof(driver),"usb-serial-demo",ids,1,0,probe,attach,poll,detach};
int banana_driver_main(const banana_driver_api_t* api) {
    usb=bdrv_extension(api,BDRV_EXT_USB,1,sizeof(*usb));
    return usb?usb->register_driver(&driver):-1;
}
