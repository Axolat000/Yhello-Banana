/* Host unit test for session restoration and unsaved document signaling */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../../web/test/shim/types.h"

/* Freestanding kheap shims using host malloc/free */
void* kmalloc(size_t sz) { return malloc(sz); }
void* kmalloc_aligned(size_t sz, size_t align) { (void)align; return malloc(sz); }
void* krealloc(void* p, size_t sz) { return realloc(p, sz); }
void* kzalloc(size_t sz) { void* p = malloc(sz); if (p) memset(p, 0, sz); return p; }
void  kfree(void* p) { free(p); }

/* Mock spinlock */
#define SPINLOCK_H
typedef struct { volatile int v; } spinlock_t;
#define SPINLOCK_INIT { 0 }
static inline void spin_lock(spinlock_t* l) { (void)l; }
static inline void spin_unlock(spinlock_t* l) { (void)l; }
static inline uintptr_t spin_lock_irq(spinlock_t* l) { (void)l; return 0; }
static inline void spin_unlock_irq(spinlock_t* l, uintptr_t f) { (void)l; (void)f; }

/* Mock timer and rtc */
#define TIMER_H
uint32_t timer_ms(void) { return 10000; }
uint32_t timer_ticks(void) { return 1000; }

/* In-memory mock filesystem and config store */
#define FS_H
#define FS_PATH_LEN 256

typedef struct {
    char path[FS_PATH_LEN];
    char content[4096];
    uint32_t size;
    int used;
} mock_fs_file_t;

static mock_fs_file_t g_mock_files[32];

static mock_fs_file_t* mock_find_file(const char* path) {
    for (int i = 0; i < 32; i++) {
        if (g_mock_files[i].used && strcmp(g_mock_files[i].path, path) == 0) {
            return &g_mock_files[i];
        }
    }
    return NULL;
}

int fs_find_file(const char* path) {
    mock_fs_file_t* mf = mock_find_file(path);
    return mf ? (int)(mf - g_mock_files) : -1;
}

int fs_write_path(const char* path, const char* data, uint32_t len) {
    mock_fs_file_t* mf = mock_find_file(path);
    if (!mf) {
        for (int i = 0; i < 32; i++) {
            if (!g_mock_files[i].used) {
                mf = &g_mock_files[i];
                mf->used = 1;
                strncpy(mf->path, path, sizeof(mf->path) - 1);
                break;
            }
        }
    }
    if (!mf) return -1;
    if (len > sizeof(mf->content) - 1) len = sizeof(mf->content) - 1;
    memcpy(mf->content, data, len);
    mf->content[len] = 0;
    mf->size = len;
    return (int)len;
}

void fs_delete(const char* path, int rec) {
    (void)rec;
    mock_fs_file_t* mf = mock_find_file(path);
    if (mf) {
        mf->used = 0;
        mf->path[0] = 0;
        mf->size = 0;
    }
}


typedef struct { uint32_t size; const char* content; } fs_file_t;
static fs_file_t g_query_file;
fs_file_t* fs_get_file(int fi) {
    if (fi < 0 || fi >= 32 || !g_mock_files[fi].used) return NULL;
    g_query_file.size = g_mock_files[fi].size;
    g_query_file.content = g_mock_files[fi].content;
    return &g_query_file;
}

/* Shims for fsdisk and gfx */
int fsdisk_is_installed(void) { return 0; }
int fsdisk_sync(void) { return 0; }

void gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    (void)x; (void)y; (void)w; (void)h; (void)rgb;
}
void gfx_draw_text(int x, int y, const char* s, uint32_t fg, uint32_t bg) {
    (void)x; (void)y; (void)s; (void)fg; (void)bg;
}

/* Include config.c */
#include "../../kernel/config.c"
#include "../../kernel/notification.c"


/* Mock application and package subsystem */
#define PKG_H
#define PKG_NAME_MAX 32
typedef struct {
    char name[PKG_NAME_MAX];
    char version[16];
} pkg_info_t;

static const char* g_installed_pkgs[] = { "calc", "photos", "code", NULL };
static char g_last_pkg_run[64] = "";

int pkg_get(const char* name, pkg_info_t* out) {
    for (int i = 0; g_installed_pkgs[i]; i++) {
        if (strcmp(g_installed_pkgs[i], name) == 0) {
            if (out) {
                strncpy(out->name, name, sizeof(out->name) - 1);
                strncpy(out->version, "1.0", sizeof(out->version) - 1);
            }
            return 0;
        }
    }
    return -1;
}

int pkg_run(const char* name, int argc, char** argv, int from_desktop, char* err, int ecap) {
    (void)argc; (void)argv; (void)from_desktop; (void)err; (void)ecap;
    strncpy(g_last_pkg_run, name, sizeof(g_last_pkg_run) - 1);
    return 0;
}

#define APP_H
#define APP_MAX 32
typedef struct {
    int      id;
    char     name[32];
    int      pid;
    int      desktop;
    uint32_t mem;
    int      threads;
} app_info_t;

static app_info_t g_mock_procs[4];
static int        g_mock_nprocs = 0;

int app_snapshot(app_info_t* out, int max) {
    int n = 0;
    for (int i = 0; i < g_mock_nprocs && n < max; i++) {
        out[n] = g_mock_procs[i];
        n++;
    }
    return n;
}

/* Mock GUI, Notepad, Explorer, Browser */
static int  g_mock_terminals = 0;
static int  g_mock_files_open = 0;
static char g_mock_files_path[FS_PATH_LEN] = "";
static int  g_mock_browser_open = 0;
static char g_mock_browser_url[FS_PATH_LEN] = "";
static int  g_mock_notepad_open = 0;
static char g_mock_notepad_path[FS_PATH_LEN] = "";
static int  g_mock_notepad_dirty = 0;
static int  g_mock_settings_open = 0;
static int  g_mock_taskmgr_open = 0;

static int g_restored_terms = 0;
static int g_restored_files = 0;
static int g_restored_browser = 0;
static int g_restored_notepad = 0;
static char g_restored_notepad_doc[FS_PATH_LEN] = "";

int gui_terminal_open_count(void) { return g_mock_terminals; }
int gui_open_terminal(void) { g_restored_terms++; return g_restored_terms; }

int explorer_is_open(void) { return g_mock_files_open; }
const char* explorer_get_path(char* out, int cap) {
    if (out && cap > 0) strncpy(out, g_mock_files_path, cap - 1);
    return g_mock_files_path;
}
int gui_open_files(const char* path) {
    g_restored_files++;
    if (path) strncpy(g_mock_files_path, path, sizeof(g_mock_files_path) - 1);
    return 1;
}

int browser_is_open(void) { return g_mock_browser_open; }
const char* browser_get_url(char* out, int cap) {
    if (out && cap > 0) strncpy(out, g_mock_browser_url, cap - 1);
    return g_mock_browser_url;
}
int gui_open_browser(const char* url) {
    g_restored_browser++;
    if (url) strncpy(g_mock_browser_url, url, sizeof(g_mock_browser_url) - 1);
    return 1;
}

int notepad_is_open(void) { return g_mock_notepad_open; }
const char* notepad_get_path(char* out, int cap) {
    if (out && cap > 0) strncpy(out, g_mock_notepad_path, cap - 1);
    return g_mock_notepad_path;
}
int notepad_is_dirty(void) { return g_mock_notepad_dirty; }
int gui_open_notepad(const char* path) {
    g_restored_notepad++;
    if (path) strncpy(g_restored_notepad_doc, path, sizeof(g_restored_notepad_doc) - 1);
    return 1;
}

int gui_settings_is_open(void) { return g_mock_settings_open; }
int gui_open_settings(void) { return 1; }

int gui_taskmgr_is_open(void) { return g_mock_taskmgr_open; }
int gui_open_taskmgr(void) { return 1; }

#include "../../kernel/session.c"

int main(void) {
    notify_init();

    /* ── 1. Test Session Capture ── */
    g_mock_terminals = 2;
    g_mock_files_open = 1;
    strcpy(g_mock_files_path, "/home/banana/Documents");
    g_mock_browser_open = 1;
    strcpy(g_mock_browser_url, "https://example.org");
    g_mock_notepad_open = 1;
    strcpy(g_mock_notepad_path, "/home/banana/notes.txt");
    g_mock_notepad_dirty = 1; /* unsaved buffer */

    /* Add installed app in app_snapshot */
    g_mock_nprocs = 2;
    g_mock_procs[0].desktop = 1;
    strcpy(g_mock_procs[0].name, "calc");
    g_mock_procs[1].desktop = 1;
    strcpy(g_mock_procs[1].name, "deleted_game"); /* not installed in pkg_get */

    session_state_t st;
    session_capture(&st);

    assert(st.terminals_open == 2);
    assert(st.files_open == 1);
    assert(strcmp(st.files_path, "/home/banana/Documents") == 0);
    assert(st.browser_open == 1);
    assert(strcmp(st.browser_url, "https://example.org") == 0);
    assert(st.notepad_open == 1);
    assert(strcmp(st.notepad_path, "/home/banana/notes.txt") == 0);
    assert(st.notepad_dirty == 1);
    assert(st.unsaved_count == 1);
    assert(strcmp(st.unsaved_docs[0], "/home/banana/notes.txt") == 0);
    assert(st.app_count == 2);
    assert(strcmp(st.apps[0], "calc") == 0);
    assert(strcmp(st.apps[1], "deleted_game") == 0);

    /* ── 2. Test Session Save and Load Roundtrip ── */
    assert(session_save() == 0);
    assert(fs_find_file(SESSION_FILE) >= 0);

    session_state_t loaded;
    assert(session_load(&loaded) == 0);
    assert(loaded.terminals_open == 2);
    assert(loaded.files_open == 1);
    assert(strcmp(loaded.files_path, "/home/banana/Documents") == 0);
    assert(loaded.browser_open == 1);
    assert(strcmp(loaded.browser_url, "https://example.org") == 0);
    assert(loaded.notepad_open == 1);
    assert(strcmp(loaded.notepad_path, "/home/banana/notes.txt") == 0);
    assert(loaded.notepad_dirty == 1);
    assert(loaded.unsaved_count == 1);
    assert(strcmp(loaded.unsaved_docs[0], "/home/banana/notes.txt") == 0);
    assert(loaded.app_count == 2);

    /* ── 3. Test Session Restoration & Unsaved Changes Alert ── */
    g_restored_terms = 0;
    g_restored_files = 0;
    g_restored_browser = 0;
    g_restored_notepad = 0;
    g_last_pkg_run[0] = 0;

    int n_restored = session_restore(&loaded);
    assert(n_restored >= 5); /* 2 terms + files + browser + notepad + calc */
    assert(g_restored_terms == 2);
    assert(g_restored_files == 1);
    assert(g_restored_browser == 1);
    assert(g_restored_notepad == 1);
    assert(strcmp(g_restored_notepad_doc, "/home/banana/notes.txt") == 0);

    /* Verify installed calc was launched, but deleted_game was omitted */
    assert(strcmp(g_last_pkg_run, "calc") == 0);

    /* Verify notification was triggered to signal unsaved edits */
    assert(notify_count() == 1);
    notify_item_t nitem;
    assert(notify_peek(0, &nitem) == 0);
    assert(strcmp(nitem.app, "Session") == 0);
    assert(strcmp(nitem.title, "Unsaved Data Warning") == 0);
    assert(strstr(nitem.body, "notes.txt") != NULL);

    /* ── 4. Test Configuration Enable/Disable and Clear ── */
    assert(session_restore_is_enabled() == 1);
    session_restore_set_enabled(0);
    assert(session_restore_is_enabled() == 0);
    session_restore_set_enabled(1);
    assert(session_restore_is_enabled() == 1);

    assert(session_clear() == 0);
    assert(fs_find_file(SESSION_FILE) < 0);

    puts("session_restore: all host unit tests passed successfully");
    return 0;
}
