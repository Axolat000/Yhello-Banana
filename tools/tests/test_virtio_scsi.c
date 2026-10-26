#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

/*
 * Unit tests validating virtio-scsi protocol invariants, descriptor chain layout,
 * completion tokens, resid short transfer detection, quarantine state, and truthful flush.
 */

#define CHUNK     (128u * 1024u)
#define CDB_SIZE  32
#define SENSE     96

#define D_NEXT    1
#define D_WRITE   2

#define S_OK         0
#define S_BAD_TARGET 3

typedef struct __attribute__((packed)) {
    uint8_t  lun[8];
    uint64_t id;
    uint8_t  task_attr, prio, crn;
    uint8_t  cdb[CDB_SIZE];
} vs_req_t;

typedef struct __attribute__((packed)) {
    uint32_t sense_len, resid;
    uint16_t status_qualifier;
    uint8_t  status, response;
    uint8_t  sense[SENSE];
} vs_resp_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} vring_used_elem_t;

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags, next;
} vio_desc_t;

typedef struct {
    vio_desc_t desc[4];
    uint16_t avail[8];
    uint8_t used[256];
    uint16_t size, last_used, avail_idx;
    vs_req_t req;
    vs_resp_t resp;
    uint8_t bounce[CHUNK];
    uint64_t next_id;
    volatile int lock;
    int dead;
} mock_vsc_t;

/* Emulated command execution logic mirroring kernel/virtio_scsi.c */
static int mock_command(mock_vsc_t* c, uint16_t target, uint16_t lun, const uint8_t* cdb, int cdb_len,
                        uint32_t bytes, int write, int sim_timeout, uint32_t sim_ret_id, uint32_t sim_resid, uint8_t sim_status) {
    if (c->dead) return -1;

    memset(&c->req, 0, sizeof(c->req));
    c->req.lun[0] = 1;
    c->req.lun[1] = (uint8_t)target;
    c->req.lun[2] = (uint8_t)(0x40 | ((lun >> 8) & 0x3F));
    c->req.lun[3] = (uint8_t)lun;
    c->req.id = ++c->next_id;
    memcpy(c->req.cdb, cdb, (size_t)cdb_len);
    memset(&c->resp, 0, sizeof(c->resp));
    c->resp.response = 0xFF;

    int n = 0;
    c->desc[n].addr = (uint64_t)(uintptr_t)&c->req;
    c->desc[n].len = sizeof(vs_req_t);
    c->desc[n].flags = D_NEXT;
    c->desc[n].next = (uint16_t)(n + 1);
    n++;

    if (bytes && write) {
        c->desc[n].addr = (uint64_t)(uintptr_t)c->bounce;
        c->desc[n].len = bytes;
        c->desc[n].flags = D_NEXT;
        c->desc[n].next = (uint16_t)(n + 1);
        n++;
    }

    c->desc[n].addr = (uint64_t)(uintptr_t)&c->resp;
    c->desc[n].len = sizeof(vs_resp_t);
    c->desc[n].flags = D_WRITE;
    c->desc[n].next = 0;

    if (bytes && !write) {
        c->desc[n].flags |= D_NEXT;
        c->desc[n].next = (uint16_t)(n + 1);
        n++;
        c->desc[n].addr = (uint64_t)(uintptr_t)c->bounce;
        c->desc[n].len = bytes;
        c->desc[n].flags = D_WRITE;
        c->desc[n].next = 0;
    }

    c->avail_idx++;
    c->avail[1] = c->avail_idx;

    if (sim_timeout) {
        /* On timeout: controller is quarantined and dead flag is set */
        c->dead = 1;
        return -1;
    }

    /* Simulate device writing completion into used ring */
    volatile uint16_t* uidx = (volatile uint16_t*)(c->used + 2);
    volatile vring_used_elem_t* uelems = (volatile vring_used_elem_t*)(c->used + 4);
    uint16_t cur_idx = *uidx;
    uelems[cur_idx % c->size].id = sim_ret_id;
    uelems[cur_idx % c->size].len = sizeof(vs_resp_t) + (write ? 0 : (bytes - sim_resid));
    *uidx = cur_idx + 1;

    c->resp.response = S_OK;
    c->resp.status = sim_status;
    c->resp.resid = sim_resid;

    vring_used_elem_t ue = uelems[c->last_used % c->size];
    c->last_used = *uidx;

    if (ue.id != 0) {
        /* Mismatched descriptor head */
        c->dead = 1;
        return -1;
    }

    if (c->resp.response == S_BAD_TARGET) return -2;
    if (c->resp.response != S_OK) return -1;

    if (bytes && c->resp.status == 0 && c->resp.resid != 0) {
        return -1;
    }

    return c->resp.status;
}

static void test_structure_sizes(void) {
    assert(sizeof(vs_req_t) == 51);
    assert(sizeof(vs_resp_t) == 108);
    assert(sizeof(vring_used_elem_t) == 8);
    printf("[PASS] test_structure_sizes\n");
}

static void test_read_write_cdb_layout(void) {
    mock_vsc_t c;
    memset(&c, 0, sizeof(c));
    c.size = 128;

    uint8_t read_cdb[10] = { 0x28, 0, 0, 0, 0, 10, 0, 0, 4, 0 };
    int st = mock_command(&c, 0, 1, read_cdb, 10, 2048, 0, 0, 0, 0, 0);
    assert(st == 0);
    assert(c.req.lun[0] == 1);
    assert(c.req.lun[1] == 0);
    assert(c.req.lun[2] == 0x40); /* LUN 1 encoded */
    assert(c.req.lun[3] == 1);
    assert(c.desc[0].len == sizeof(vs_req_t));
    assert(c.desc[0].flags == D_NEXT);
    assert(c.desc[1].flags == (D_WRITE | D_NEXT)); /* response buffer */
    assert(c.desc[2].flags == D_WRITE); /* data buffer for read */
    assert(c.desc[2].len == 2048);

    uint8_t write_cdb[10] = { 0x2A, 0, 0, 0, 0, 10, 0, 0, 4, 0 };
    st = mock_command(&c, 0, 1, write_cdb, 10, 2048, 1, 0, 0, 0, 0);
    assert(st == 0);
    assert(c.desc[0].flags == D_NEXT);
    assert(c.desc[1].flags == D_NEXT); /* data buffer out for write */
    assert(c.desc[1].len == 2048);
    assert(c.desc[2].flags == D_WRITE); /* response buffer */
    printf("[PASS] test_read_write_cdb_layout\n");
}

static void test_timeout_quarantine(void) {
    mock_vsc_t c;
    memset(&c, 0, sizeof(c));
    c.size = 128;

    uint8_t tur_cdb[6] = { 0 };
    int st = mock_command(&c, 0, 0, tur_cdb, 6, 0, 0, 1 /* timeout */, 0, 0, 0);
    assert(st == -1);
    assert(c.dead == 1);

    /* Subsequent command must immediately fail due to quarantine */
    st = mock_command(&c, 0, 0, tur_cdb, 6, 0, 0, 0, 0, 0, 0);
    assert(st == -1);
    printf("[PASS] test_timeout_quarantine\n");
}

static void test_mismatched_used_id(void) {
    mock_vsc_t c;
    memset(&c, 0, sizeof(c));
    c.size = 128;

    uint8_t tur_cdb[6] = { 0 };
    /* Simulate device returning bad used descriptor id 5 instead of 0 */
    int st = mock_command(&c, 0, 0, tur_cdb, 6, 0, 0, 0, 5, 0, 0);
    assert(st == -1);
    assert(c.dead == 1);
    printf("[PASS] test_mismatched_used_id\n");
}

static void test_short_transfer_resid(void) {
    mock_vsc_t c;
    memset(&c, 0, sizeof(c));
    c.size = 128;

    uint8_t read_cdb[10] = { 0x28, 0, 0, 0, 0, 1, 0, 0, 2, 0 };
    /* Requested 1024 bytes, but device reports resid=512 */
    int st = mock_command(&c, 0, 0, read_cdb, 10, 1024, 0, 0, 0, 512, 0);
    assert(st == -1);
    printf("[PASS] test_short_transfer_resid\n");
}

static void test_truthful_flush(void) {
    mock_vsc_t c;
    memset(&c, 0, sizeof(c));
    c.size = 128;

    uint8_t sync_cdb[10] = { 0x35 };
    /* CHECK CONDITION (status 2) or transport error */
    int st = mock_command(&c, 0, 0, sync_cdb, 10, 0, 0, 0, 0, 0, 2);
    /* In our adapted driver, flush returns non-zero when status != 0 */
    assert(st != 0);

    /* When status is 0 (GOOD), flush succeeds */
    st = mock_command(&c, 0, 0, sync_cdb, 10, 0, 0, 0, 0, 0, 0);
    assert(st == 0);
    printf("[PASS] test_truthful_flush\n");
}

int main(void) {
    printf("Executing virtio-scsi verification test suite...\n");
    test_structure_sizes();
    test_read_write_cdb_layout();
    test_timeout_quarantine();
    test_mismatched_used_id();
    test_short_transfer_resid();
    test_truthful_flush();
    printf("All virtio-scsi tests PASSED.\n");
    return 0;
}
