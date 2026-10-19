/* Host device-response tests execute the production driver, with no PCI/DMA. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sched.h>
#include <pthread.h>
#include "../../web/test/shim/types.h"
#define IOWAIT_H
static void io_wait_step(void) { sched_yield(); }
static void io_lock(volatile int* p) {
    while (__sync_lock_test_and_set(p, 1)) sched_yield();
}
static void io_unlock(volatile int* p) { __sync_lock_release(p); }
static void task_sleep_ms(uint32_t ms) { (void)ms; }
#include "../../kernel/virtio_scsi.c"

enum { NORMAL, TIMEOUT, RESIDUAL, WRONG_ID, BAD_DELTA, SHORT_FRAME,
       LONG_FRAME, BAD_SENSE, TRUNCATED_SENSE, FLUSH_REJECT, TOO_MUCH_RESID, BAD_RESPONSE, WRITE_USED_TOTAL };
static int mode, modern = 1, notify_count, reg_fail, alloc_fail, queue_fail;
static uint32_t cfg[9], now, block_size = 512;
static uint64_t last_block = 4095;
static int fake_topology;
static blockdev_t devices[VSCSI_MAX];
static int registered;
static void* allocations[1024];
static int allocated;
static uint8_t media[1024 * 512];

void* kmalloc(size_t n) {
    if (alloc_fail && --alloc_fail == 0) return NULL;
    assert(allocated < 1024);
    return allocations[allocated++] = calloc(1, n);
}
void klog(const char* fmt, ...) { (void)fmt; }
uint32_t timer_ms(void) { now += 10001; return now; }
size_t kstrlcpy(char* d, const char* s, size_t n) {
    size_t len = strlen(s);
    if (n) { size_t take = len < n - 1 ? len : n - 1; memcpy(d, s, take); d[take] = 0; }
    return len;
}
int ksnprintf(char* d, size_t n, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = vsnprintf(d, n, fmt, ap); va_end(ap); return r;
}
int vio_open(vio_t* v, const pci_dev_t* d) {
    (void)d; v->modern = modern; v->device = (volatile uint8_t*)cfg; return 0;
}
uint32_t vio_features(vio_t* v) { (void)v; return 0; }
int vio_set_features(vio_t* v, uint32_t f) { (void)v; (void)f; return 0; }
uint32_t vio_cfg32(vio_t* v, int off) { (void)v; return cfg[off / 4]; }
void vio_driver_ok(vio_t* v) { (void)v; }
void vio_failed(vio_t* v) { (void)v; }
uint8_t vio_isr(vio_t* v) { (void)v; return 0; }
uint16_t vio_queue(vio_t* v, int q, uint8_t* mem, uint16_t max,
                  vio_desc_t** d, volatile uint16_t** a, volatile uint8_t** u) {
    (void)v; assert(q == 2); (void)max;
    *d = (vio_desc_t*)mem; *a = (volatile uint16_t*)(mem + 128);
    *u = mem + 4096; return queue_fail ? 2 : 8;
}
blockdev_t* blockdev_register_kind(const char* k, const char* m, uint32_t n, uint32_t s,
                                 int (*r)(blockdev_t*,uint32_t,uint32_t,void*),
                                 int (*w)(blockdev_t*,uint32_t,uint32_t,const void*), void* p) {
    assert(!strcmp(k, "scsi")); assert(s == 512); (void)m;
    if (reg_fail) return NULL;
    assert(registered < VSCSI_MAX);
    blockdev_t* b = &devices[registered++]; b->sectors=n; b->read=r; b->write=w; b->priv=p; return b;
}
int pci_scan(int (*cb)(const pci_dev_t*,void*), void* ctx) { (void)cb; (void)ctx; return 0; }

void vio_notify(vio_t* v, int q) {
    assert(q == 2);
    vsc_t* c = (vsc_t*)v;
    notify_count++;
    if (mode == TIMEOUT) return;
    uint32_t bytes = 0; int write = 0;
    for (int i = 1; i < 3; i++) {
        if (c->desc[i].addr == (uintptr_t)c->bounce) {
            bytes = c->desc[i].len; write = !(c->desc[i].flags & D_WRITE); break;
        }
        if (!(c->desc[i].flags & D_NEXT)) break;
    }
    memset(c->resp, 0, sizeof(*c->resp));
    unsigned target = c->req->lun[1], lun = c->req->lun[3];
    int present = !fake_topology || (target == 0 && lun == 1) || (target == 1 && lun == 0);
    uint8_t op = c->req->cdb[0];
    if (!present) { c->resp->response = S_BAD_TARGET; c->resp->resid = bytes; }
    else if (op == 0x12) {
        memset(c->bounce, 0, bytes); memcpy(c->bounce + 8, "MOCK    DISK            ", 24);
    } else if (op == 0x25) {
        be32(c->bounce, last_block >= 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)last_block);
        be32(c->bounce + 4, block_size);
    } else if (op == 0x9E) {
        be32(c->bounce, last_block >> 32); be32(c->bounce + 4, last_block); be32(c->bounce + 8, block_size);
    } else if (op == 0x28 || op == 0x2A) {
        uint32_t lba = rd_be32(c->req->cdb + 2);
        assert(lba * 512u + bytes <= sizeof(media));
        if (write) memcpy(media + lba * 512u, c->bounce, bytes);
        else memcpy(c->bounce, media + lba * 512u, bytes);
    }
    if (mode == RESIDUAL) c->resp->resid = bytes ? 1 : 0;
    if (mode == TOO_MUCH_RESID) c->resp->resid = bytes + 1;
    if (mode == BAD_SENSE) c->resp->sense_len = SENSE + 1;
    if (mode == BAD_RESPONSE) c->resp->response = 0xFF;
    if (mode == TRUNCATED_SENSE || (mode == FLUSH_REJECT && op == 0x35)) {
        c->resp->status = 2; c->resp->sense_len = mode == TRUNCATED_SENSE ? 1 : 18;
        c->resp->sense[0] = 0x70; c->resp->sense[2] = 5; c->resp->sense[7] = 10;
    }
    volatile uint32_t* e = (volatile uint32_t*)(c->used + 4 + 8u*(c->last_used % c->size));
    e[0] = mode == WRONG_ID ? 1 : 0;
    e[1] = sizeof(vs_resp_t) + (write ? 0 : bytes - c->resp->resid);
    if (mode == WRITE_USED_TOTAL && write) e[1] += bytes;
    if (mode == SHORT_FRAME) e[1] = 11;
    if (mode == LONG_FRAME) e[1] = sizeof(vs_resp_t) + bytes + 1;
    *(volatile uint16_t*)(c->used + 2) = c->last_used + (mode == BAD_DELTA ? 2 : 1);
}

static void reset(void) {
    for (int i=0;i<allocated;i++) free(allocations[i]);
    allocated=0; memset(g_ctl,0,sizeof(g_ctl)); memset(g_disk,0,sizeof(g_disk));
    g_count=g_nctl=registered=notify_count=now=0;
    mode=NORMAL; reg_fail=alloc_fail=queue_fail=fake_topology=0; modern=1;
    block_size=512; last_block=4095;
    uint32_t defaults[9]={1,1,256,1,16,96,32,0,0}; memcpy(cfg,defaults,sizeof(cfg));
}
static vsc_t* controller(void) {
    pci_dev_t pd = {0}; assert(start(&g_ctl[0], &pd)==0); return &g_ctl[0];
}
static void test_completion(void) {
    int faults[]={WRONG_ID,BAD_DELTA,SHORT_FRAME,LONG_FRAME,BAD_SENSE,TOO_MUCH_RESID,BAD_RESPONSE};
    for (unsigned i=0;i<sizeof(faults)/sizeof(faults[0]);i++) {
        reset(); vsc_t* c=controller(); mode=faults[i]; uint8_t cmd[6]={0};
        assert(command(c,0,0,cmd,6,0,0)==-1); assert(c->failed);
    }
    reset(); controller(); uint8_t out[512]; memset(out,0xA5,sizeof(out)); mode=RESIDUAL;
    assert(virtio_scsi_read(0,0,1,out)==-1);
    for (unsigned i=0;i<sizeof(out);i++) assert(out[i]==0xA5);
    mode=TRUNCATED_SENSE; uint8_t cmd[6]={0};
    assert(command_retry(&g_ctl[0],0,0,cmd,6,0,0)==-1);
    assert(sense_key(&g_ctl[0])==0xFF);
    mode=FLUSH_REJECT; assert(virtio_scsi_flush(0)==-1);
    mode=WRITE_USED_TOTAL; assert(!virtio_scsi_write(0,0,1,out));
    mode=LONG_FRAME; assert(virtio_scsi_write(0,0,1,out)==-1 && g_ctl[0].failed);
    /* Legacy framing ignores used lengths, but still rejects residuals. */
    reset(); modern=0; controller(); mode=SHORT_FRAME; assert(virtio_scsi_flush(0)==0);
    mode=RESIDUAL; assert(virtio_scsi_read(0,0,1,out)==-1);
}
static void test_timeout(void) {
    reset(); vsc_t* c=controller(); mode=TIMEOUT; uint8_t data[512]={0};
    assert(virtio_scsi_write(0,0,1,data)==-1); assert(c->failed);
    vs_req_t req=*c->req; vio_desc_t desc[3]; memcpy(desc,c->desc,sizeof(desc));
    uint16_t avail=c->avail_idx; int sent=notify_count;
    memset(c->bounce,0xA7,CHUNK); /* Device completes after the timeout. */
    *(volatile uint16_t*)(c->used+2)=c->last_used+1;
    assert(virtio_scsi_write(0,0,1,data)==-1);
    assert(virtio_scsi_read(0,0,1,data)==-1); assert(virtio_scsi_flush(0)==-1);
    assert(notify_count==sent && c->avail_idx==avail);
    assert(!memcmp(&req,c->req,sizeof(req)) && !memcmp(desc,c->desc,sizeof(desc)));
    for (unsigned i=0;i<CHUNK;i++) assert(c->bounce[i]==0xA7);
    /* A failed discovery consumes its slot rather than overwriting it. */
    reset(); mode=TIMEOUT; pci_dev_t pd={.vendor=0x1AF4,.device=0x1048};
    scan_cb(&pd,NULL); assert(g_nctl==1 && g_ctl[0].failed);
    void* retained=g_ctl[0].req; mode=NORMAL; scan_cb(&pd,NULL);
    assert(g_nctl==2 && g_ctl[0].req==retained && g_disk[0].c==&g_ctl[1]);
}
static void test_config_capacity(void) {
    unsigned fields[]={0,1,2,3,5,6};
    for (unsigned i=0;i<6;i++) { reset(); cfg[fields[i]]=0; pci_dev_t pd={0};
        assert(start(&g_ctl[0],&pd)==-1 && !allocated); }
    reset(); cfg[7]=1; pci_dev_t pd={0}; assert(start(&g_ctl[0],&pd)==-1);
    for (int i=1;i<=3;i++) { reset(); alloc_fail=i; assert(start(&g_ctl[0],&pd)==-1); assert(!g_count); }
    reset(); queue_fail=1; assert(start(&g_ctl[0],&pd)==-1 && !g_count);
    reset(); reg_fail=1; controller(); assert(!g_count);
    reset(); block_size=4096; controller(); assert(!g_count);
    reset(); last_block=0; controller(); assert(g_disk[0].sectors==1);
    reset(); last_block=0xFFFFFFFEull; controller(); assert(g_disk[0].sectors==0xFFFFFFFFu);
    reset(); last_block=0xFFFFFFFFull; controller(); assert(!g_count);
    reset(); last_block=0x100000000ull; controller(); assert(!g_count);
    reset(); fake_topology=1; cfg[7]=1u<<16; cfg[8]=1; controller();
    assert(g_count==2 && g_disk[0].lun==1 && g_disk[1].target==1);
}
static void* reader(void* arg) {
    uint8_t out[512]; unsigned lba=(uintptr_t)arg;
    for (int i=0;i<1000;i++) { assert(!virtio_scsi_read(0,lba,1,out)); assert(!memcmp(out,media+lba*512,512)); }
    return NULL;
}
static void test_io(void) {
    reset(); cfg[2]=3; vsc_t* c=controller();
    uint8_t input[512*263], output[sizeof(input)];
    for (unsigned i=0;i<sizeof(input);i++) input[i]=(i*17u+i/512u)%251u;
    assert(!virtio_scsi_write(0,0,263,input)); assert(!virtio_scsi_read(0,0,263,output));
    assert(!memcmp(input,output,sizeof(input)));
    assert(virtio_scsi_read(0,4095,2,output)==-1);
    assert(virtio_scsi_read(0,0xFFFFFFFFu,1,output)==-1);
    c->last_used=c->avail_idx=65535; *(volatile uint16_t*)(c->used+2)=65535;
    assert(!virtio_scsi_read(0,0,1,output)); assert(!c->last_used && !c->avail_idx);
    /* Also cross the default 128 KiB chunk boundary. */
    reset(); c=controller();
    assert(!virtio_scsi_write(0,0,263,input));
    assert(!virtio_scsi_read(0,0,263,output));
    assert(!memcmp(input,output,sizeof(input)));
    pthread_t threads[4];
    for (uintptr_t i=0;i<4;i++) assert(!pthread_create(&threads[i],NULL,reader,(void*)i));
    for (int i=0;i<4;i++) pthread_join(threads[i],NULL);
}
int main(void) {
    test_completion(); test_timeout(); test_config_capacity(); test_io(); reset();
    puts("virtio-scsi host: completion, timeout/late DMA, config, capacity, discovery, chunking, wrap and concurrent callers PASS");
}
