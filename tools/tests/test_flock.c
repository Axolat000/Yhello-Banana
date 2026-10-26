/* Exercise the real kernel flock entry and file locking table on the host.
 * Unused kernel code is discarded by the linker; no privileged code runs. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/app.c"

static int test_pid;
static fs_file_t test_file1;
static fs_file_t test_file2;
static int unpins, socket_closes;
static int mock_fsdisk_installed;
static int mock_fsdisk_sync_result;

int task_current_pid(void) { return test_pid; }
fs_file_t* fs_file_info(int idx) {
    if (idx == 7) return &test_file1;
    if (idx == 8) return &test_file2;
    return NULL;
}
void fs_unpin(int idx) {
    if (idx == 7 || idx == 8) unpins++;
}
void usock_close(usock_t* s) {
    if (s) socket_closes++;
}
int fsdisk_is_installed(void) { return mock_fsdisk_installed; }
int fsdisk_sync_wait(void) { return mock_fsdisk_sync_result; }

/* v21 -> v22 -> v23 layout stability */
_Static_assert(__builtin_offsetof(banana_api_t, fd_sync) ==
               __builtin_offsetof(banana_api_t, utc_offset) + sizeof(void*),
               "fd_sync must follow the v21 tail");
_Static_assert(__builtin_offsetof(banana_api_t, flock) ==
               __builtin_offsetof(banana_api_t, fd_sync) + sizeof(void*),
               "flock must follow the v22 tail");
_Static_assert(sizeof(banana_api_t) ==
               __builtin_offsetof(banana_api_t, flock) + sizeof(void*),
               "flock must be the ABI tail");

int main(void) {
    /* 1. ABI and Versioning */
    assert(BANANA_API_VERSION == 23);
    banana_api_t old = { .size = __builtin_offsetof(banana_api_t, flock) };
    assert(!BANANA_HAS(&old, flock));
    old.size = sizeof(old);
    assert(BANANA_HAS(&old, flock));

#ifdef __x86_64__
    spec_init();
    unsigned slot = __builtin_offsetof(banana_api_t, flock) / sizeof(void*);
    assert(g_spec[slot].args && g_spec[slot].args[0] == 'i' && g_spec[slot].args[1] == 'i' && !g_spec[slot].args[2]);
    assert(g_local[slot] == LOCAL_NO);
#endif

    /* Setup mock file entries */
    test_file1.used = 1;
    test_file1.mnt = 0;
    test_file1.node = 1001;
    test_file2.used = 1;
    test_file2.mnt = 0;
    test_file2.node = 1002;

    /* Setup processes: Proc A (0, pid 10) and Proc B (1, pid 20) */
    memset(g_procs, 0, sizeof(g_procs));
    memset(g_flocks, 0, sizeof(g_flocks));

    g_procs[0].used = 1;
    g_procs[0].id = 0;
    g_procs[0].pid = 10;
    pid_set(10, 0, -1);

    g_procs[1].used = 1;
    g_procs[1].id = 1;
    g_procs[1].pid = 20;
    pid_set(20, 1, -1);

    /* Switch to Proc A */
    test_pid = 10;
    assert(cur() == &g_procs[0]);

    /* Test invalid descriptor numbers */
    assert(a_flock(-1, BANANA_LOCK_EX) == -9);
    assert(a_flock(3, BANANA_LOCK_EX) == -9); /* unused */
    assert(a_flock(0, BANANA_LOCK_EX) == -22); /* stdin */
    assert(a_flock(1, BANANA_LOCK_EX) == -22); /* stdout */
    assert(a_flock(2, BANANA_LOCK_EX) == -22); /* stderr */

    /* Open file1 on fd 3, file2 on fd 4, socket on fd 5 in Proc A */
    g_procs[0].fds[0] = (afd_t){ .used = 1, .fidx = 7, .flags = BANANA_O_READ };
    g_procs[0].fds[1] = (afd_t){ .used = 1, .fidx = 8, .flags = BANANA_O_WRITE };
    g_procs[0].fds[2] = (afd_t){ .used = 1, .sock = (usock_t*)&test_file1 };

    /* Socket cannot be locked */
    assert(a_flock(5, BANANA_LOCK_EX) == -22);

    /* Invalid operations */
    assert(a_flock(3, 0) == -22);
    assert(a_flock(3, 100) == -22);
    assert(a_flock(3, BANANA_LOCK_NB) == -22);

    /* 2. Lock sharing: Proc A acquires LOCK_SH */
    assert(a_flock(3, BANANA_LOCK_SH) == 0);
    /* Re-acquiring same lock on same descriptor is idempotent */
    assert(a_flock(3, BANANA_LOCK_SH) == 0);

    /* Switch to Proc B */
    test_pid = 20;
    assert(cur() == &g_procs[1]);
    /* Open file1 on fd 3 in Proc B */
    g_procs[1].fds[0] = (afd_t){ .used = 1, .fidx = 7, .flags = BANANA_O_READ };

    /* Proc B acquires LOCK_SH concurrently on same file -> SUCCESS */
    assert(a_flock(3, BANANA_LOCK_SH | BANANA_LOCK_NB) == 0);

    /* Proc B attempts LOCK_EX with LOCK_NB on file1 -> CONFLICT (Proc A holds LOCK_SH) */
    assert(a_flock(3, BANANA_LOCK_EX | BANANA_LOCK_NB) == -11);

    /* Switch back to Proc A */
    test_pid = 10;
    /* Proc A unlocks fd 3 */
    assert(a_flock(3, BANANA_LOCK_UN) == 0);

    /* Switch to Proc B: now B is the sole holder of LOCK_SH and upgrades to LOCK_EX */
    test_pid = 20;
    assert(a_flock(3, BANANA_LOCK_EX | BANANA_LOCK_NB) == 0);

    /* Switch to Proc A */
    test_pid = 10;
    /* Proc A cannot acquire LOCK_SH or LOCK_EX while Proc B holds LOCK_EX */
    assert(a_flock(3, BANANA_LOCK_SH | BANANA_LOCK_NB) == -11);
    assert(a_flock(3, BANANA_LOCK_EX | BANANA_LOCK_NB) == -11);

    /* File 2 is unlocked, Proc A takes LOCK_EX on file2 (fd 4) */
    assert(a_flock(4, BANANA_LOCK_EX | BANANA_LOCK_NB) == 0);

    /* 3. Automatic release on close: Proc B closes fd 3 */
    test_pid = 20;
    assert(a_close(3) == 0);

    /* Switch to Proc A: file1 is now available for LOCK_EX */
    test_pid = 10;
    assert(a_flock(3, BANANA_LOCK_EX | BANANA_LOCK_NB) == 0);
    assert(a_flock(3, BANANA_LOCK_UN) == 0);

    /* 4. Automatic release on process death / cleanup */
    /* Proc A still holds LOCK_EX on file2 (fd 4). Switch to B */
    test_pid = 20;
    g_procs[1].fds[1] = (afd_t){ .used = 1, .fidx = 8, .flags = BANANA_O_WRITE };
    assert(a_flock(4, BANANA_LOCK_EX | BANANA_LOCK_NB) == -11);

    /* Proc A exits and its locks are cleared via flock_release_owner */
    flock_release_owner(0);

    /* Proc B can now take LOCK_EX on file2 */
    assert(a_flock(4, BANANA_LOCK_EX | BANANA_LOCK_NB) == 0);
    assert(a_flock(4, BANANA_LOCK_UN) == 0);
    assert(a_close(4) == 0);

    /* 5. RAM disk file identity: node == 0, mnt == 0, matched via fidx */
    test_file1.node = 0;
    test_file1.mnt = 0;
    test_pid = 20;
    g_procs[1].fds[0] = (afd_t){ .used = 1, .fidx = 7, .flags = BANANA_O_READ };
    g_procs[1].fds[2] = (afd_t){ .used = 1, .fidx = 7, .flags = BANANA_O_READ }; /* separate descriptor, same file */
    assert(a_flock(3, BANANA_LOCK_EX | BANANA_LOCK_NB) == 0);
    /* fd 5 pointing to same file in same proc conflicts */
    assert(a_flock(5, BANANA_LOCK_EX | BANANA_LOCK_NB) == -11);
    assert(a_flock(5, BANANA_LOCK_SH | BANANA_LOCK_NB) == -11);
    /* Unlock fd 3 */
    assert(a_flock(3, BANANA_LOCK_UN) == 0);
    assert(a_flock(5, BANANA_LOCK_EX | BANANA_LOCK_NB) == 0);
    assert(a_close(5) == 0);

    puts("flock: ABI presence, syscall spec, descriptor validation, mutual exclusion, LOCK_NB, close/exit release, and RAM identity tests passed");
    return 0;
}

int gui_focused_vt(void) { return 0; }
void app_leave(uintptr_t sp, int code) { (void)sp; (void)code; __builtin_unreachable(); }
uint32_t timer_ms(void) { return 0; }
int appwin_kill_requested(int id) { (void)id; return 0; }
char keyboard_try_getchar(void) { return 0; }
void gui_poll(void) {}
void terminal_vt_set_active(int vt) { (void)vt; }
int ksnprintf(char* buf, size_t n, const char* fmt, ...) { (void)buf; (void)n; (void)fmt; return 0; }
void terminal_writeln(const char* s) { (void)s; }
