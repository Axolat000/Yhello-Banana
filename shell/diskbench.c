/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "diskbench.h"
#include "../kernel/terminal.h"
#include "../kernel/kstring.h"
#include "../kernel/kheap.h"
#include "../kernel/timer.h"
#include "../kernel/task.h"
#include "../kernel/fs.h"
#include "../kernel/fsdisk.h"
#include "../kernel/ext4.h"

/* `diskbench [MB]`: the installed disk's speed through its filesystem - a
 * file written in 1 MiB pieces (and synced), read back (nothing of it in
 * RAM first), then 4 KiB reads at random places in it */

static void say(const char* s) { terminal_writeln(s); }

static void drop_all(void) {
    fs_evict(0xFFFFFFFFu, 0);
    if (ext4_mounted()) { ext4_flush(); ext4_cache_trim(ext4_cache_bytes()); }
}

/* "123.4 MB/s" for bytes in ms */
static void rate(char* out, int cap, uint64_t bytes, uint32_t ms) {
    if (!ms) ms = 1;
    uint32_t kbs = (uint32_t)(bytes * 1000u / 1024u / ms);
    ksnprintf(out, (size_t)cap, "%u.%u MB/s", kbs / 1024u, (kbs % 1024u) * 10u / 1024u);
}

int diskbench_cmd(int argc, char** argv) {
    uint32_t mb = 64;
    if (argc > 1) k_parse_u32(argv[1], &mb);
    if (mb < 4) mb = 4;
    if (mb > 2048) mb = 2048;
    if (!fsdisk_is_installed()) { say("diskbench: no installed disk (the files live in RAM on the live CD)"); return 1; }
    const char* path = "/tmp/.diskbench";
    uint8_t* buf = (uint8_t*)kmalloc(1u << 20);
    if (!buf) { say("diskbench: no memory"); return 1; }
    for (uint32_t i = 0; i < (1u << 20); i++) buf[i] = (uint8_t)(i * 2654435761u >> 24);
    fs_mkdir_p("/tmp");
    fs_delete(path, 0);
    int idx = fs_create(path);
    char line[160], r[32];
    if (idx < 0) { say("diskbench: cannot make /tmp/.diskbench"); kfree(buf); return 1; }
    ksnprintf(line, sizeof(line), "diskbench: %u MB on the installed disk (%s)", mb, fsdisk_fs_type());
    say(line);

    /* 1. written, 1 MiB at a time, and on the disk */
    uint32_t t0 = timer_ms();
    int ok = 1;
    for (uint32_t i = 0; i < mb && ok; i++) {
        buf[0] = (uint8_t)i;
        ok = fs_write_at(idx, i << 20, buf, 1u << 20) == 0;
        if ((i & 7) == 7) task_yield();
    }
    if (ok) ok = fsdisk_sync() == 0;
    uint32_t tw = timer_ms() - t0;
    rate(r, sizeof(r), (uint64_t)mb << 20, tw);
    ksnprintf(line, sizeof(line), "  write  (1 MiB pieces, then sync): %s  (%u ms)%s", r, tw, ok ? "" : "  FAILED");
    say(line);

    /* 2. read back, nothing of it cached */
    drop_all();
    t0 = timer_ms();
    uint32_t bad = 0;
    for (uint32_t i = 0; i < mb && ok; i++) {
        if (fs_read(idx, i << 20, buf, 1u << 20) != (int)(1u << 20)) { ok = 0; break; }
        if (buf[0] != (uint8_t)i || buf[12345] != (uint8_t)(12345u * 2654435761u >> 24)) bad++;
        if ((i & 7) == 7) task_yield();
    }
    uint32_t tr = timer_ms() - t0;
    rate(r, sizeof(r), (uint64_t)mb << 20, tr);
    ksnprintf(line, sizeof(line), "  read   (1 MiB pieces, not cached): %s  (%u ms)%s%s", r, tr,
              bad ? "  WRONG DATA" : "", ok ? "" : "  FAILED");
    say(line);

    /* 3. 4 KiB reads at random places */
    drop_all();
    uint32_t n = 2000, x = 12345;
    t0 = timer_ms();
    for (uint32_t i = 0; i < n && ok; i++) {
        x = x * 1103515245u + 12345u;
        uint32_t off = (x >> 8) % (mb << 8) * 4096u;
        if (fs_read(idx, off, buf, 4096) != 4096) ok = 0;
    }
    uint32_t tq = timer_ms() - t0;
    ksnprintf(line, sizeof(line), "  random 4 KiB reads: %u per second  (%u in %u ms)%s", n * 1000u / (tq ? tq : 1), n, tq,
              ok ? "" : "  FAILED");
    say(line);
    fs_delete(path, 0);
    kfree(buf);
    return 1;
}
