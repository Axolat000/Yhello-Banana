/* Exercise the real kernel descriptor lookup and sync entry on the host.
 * Unused kernel code is discarded by the linker; no privileged code runs. */
#include <assert.h>
#include <stdio.h>
#include "../../kernel/app.c"

static int test_pid;
static fs_file_t test_file;
static int unpins, socket_closes;
static int mock_fsdisk_installed;
static int mock_fsdisk_sync_result;
static int mock_sync_wait_calls;

int task_current_pid(void) { return test_pid; }
fs_file_t* fs_file_info(int idx) { return idx == 7 ? &test_file : NULL; }
void fs_unpin(int idx) { assert(idx == 7); unpins++; }
void usock_close(usock_t* s) { assert(s); socket_closes++; }

int fsdisk_is_installed(void) { return mock_fsdisk_installed; }
int fsdisk_sync_wait(void) {
    mock_sync_wait_calls++;
    return mock_fsdisk_sync_result;
}

/* v21's final field must keep its offset, and v22 must only append. */
_Static_assert(__builtin_offsetof(banana_api_t, fd_sync) ==
               __builtin_offsetof(banana_api_t, utc_offset) + sizeof(void*),
               "fd_sync must follow the v21 tail");
_Static_assert(__builtin_offsetof(banana_api_t, flock) ==
               __builtin_offsetof(banana_api_t, fd_sync) + sizeof(void*),
               "flock must follow the v22 tail");

int main(void) {
    assert(BANANA_API_VERSION == 23);
    banana_api_t old = { .size = __builtin_offsetof(banana_api_t, fd_sync) };
    assert(!BANANA_HAS(&old, fd_sync));
    assert(!BANANA_HAS(&old, flock));
    old.size = sizeof(old);
    assert(BANANA_HAS(&old, fd_sync));
    assert(BANANA_HAS(&old, flock));
#ifdef __x86_64__
    spec_init();
    unsigned slot = __builtin_offsetof(banana_api_t, fd_sync) / sizeof(void*);
    assert(g_spec[slot].args && g_spec[slot].args[0] == 'i' && !g_spec[slot].args[1]);
    assert(g_local[slot] == LOCAL_NO); /* storage calls stay on the boot core */
#endif
    assert(a_fd_sync(3) == -9); /* no current app */
    assert(a_fd_sync(0) == -9);
    g_procs[0].used = 1;
    pid_set(0, 0, -1);
    app_proc_t* p = &g_procs[0];
    int invalid[] = {-2147483647 - 1, -1, 3, 255, 256, 2147483647};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        assert(a_fd_sync(invalid[i]) == -9);
    for (int fd = 0; fd <= 2; fd++) assert(a_fd_sync(fd) == -22);
    p->fds[0] = (afd_t){ .used = 1, .fidx = 7, .flags = BANANA_O_READ, .pos = 42 };
    assert(a_fd_sync(3) == -9); /* deleted file */
    test_file.used = 1;
    /* RAM disk session without installed disk: must return -38 (ENOSYS) */
    mock_fsdisk_installed = 0;
    assert(a_fd_sync(3) == -38);
    assert(mock_sync_wait_calls == 0);
    assert(p->fds[0].pos == 42 && p->fds[0].used);

    /* Installed disk with successful sync: must return 0 */
    mock_fsdisk_installed = 1;
    mock_fsdisk_sync_result = FSDISK_OK;
    assert(a_fd_sync(3) == 0);
    assert(mock_sync_wait_calls == 1);
    assert(p->fds[0].pos == 42);

    /* Installed disk with I/O or freeze-timeout error: must return -5 (EIO) */
    mock_fsdisk_sync_result = FSDISK_ERR_IO;
    assert(a_fd_sync(3) == -5);
    assert(mock_sync_wait_calls == 2);

    /* File on another mounted volume (e.g. FAT32/exFAT USB stick mnt=1): ENOSYS */
    test_file.mnt = 1;
    assert(a_fd_sync(3) == -38);
    assert(mock_sync_wait_calls == 2); /* no call to fsdisk_sync_wait for external mount */
    test_file.mnt = 0;

    p->fds[0].fidx = 8;
    assert(a_fd_sync(3) == -9); /* missing file entry */
    p->fds[0].fidx = 7;
    assert(a_close(3) == 0 && unpins == 1);
    assert(a_fd_sync(3) == -9); /* actually closed through kernel close */
    p->fds[APP_FD_MAX - 1] = (afd_t){ .used = 1, .fidx = 7, .flags = BANANA_O_WRITE };
    mock_fsdisk_sync_result = FSDISK_OK;
    assert(a_fd_sync(255) == 0);
    assert(mock_sync_wait_calls == 3);
    p->fds[0] = (afd_t){ .used = 1, .sock = (usock_t*)&test_file };
    assert(a_fd_sync(3) == -22);
    assert(a_close(3) == 0 && socket_closes == 1);
    assert(a_fd_sync(3) == -9);
    test_pid = TASK_MAX;
    assert(a_fd_sync(255) == -9);
    puts("fd_sync: ABI presence, syscall spec, descriptor validation, volume-aware sync and error propagation tests passed");
    return 0;
}
