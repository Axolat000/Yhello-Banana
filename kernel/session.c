#include "session.h"
#include "kstring.h"
#include "kheap.h"
#include "config.h"
#include "fs.h"
#include "pkg.h"
#include "app.h"
#include "gui.h"
#include "notepad.h"
#include "explorer.h"
#include "browser.h"
#include "notification.h"
#include "settings.h"

#define SESSION_CONF_HEADER "# Banana OS Desktop Session State\n"

int session_restore_is_enabled(void) {
    char val[16];
    if (cfg_get(CFG_SETTINGS, "session_restore", val, sizeof(val))) {
        return (strcmp(val, "yes") == 0 || strcmp(val, "1") == 0);
    }
    /* default: enabled */
    return 1;
}

void session_restore_set_enabled(int enabled) {
    cfg_set(CFG_SETTINGS, "session_restore", enabled ? "yes" : "no", "# Banana OS settings (the Settings app writes this)\n");
    cfg_persist();
}

void session_capture(session_state_t* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));

    /* Capture terminals */
    out->terminals_open = gui_terminal_open_count();

    /* Capture Files */
    if (explorer_is_open()) {
        out->files_open = 1;
        explorer_get_path(out->files_path, sizeof(out->files_path));
    }

    /* Capture Browser */
    if (browser_is_open()) {
        out->browser_open = 1;
        browser_get_url(out->browser_url, sizeof(out->browser_url));
    }

    /* Capture Notepad */
    if (notepad_is_open()) {
        out->notepad_open = 1;
        notepad_get_path(out->notepad_path, sizeof(out->notepad_path));
        out->notepad_dirty = notepad_is_dirty();
        if (out->notepad_dirty && out->unsaved_count < SESSION_DOCS_MAX) {
            const char* doc = out->notepad_path[0] ? out->notepad_path : "Untitled document";
            kstrlcpy(out->unsaved_docs[out->unsaved_count], doc, sizeof(out->unsaved_docs[0]));
            out->unsaved_count++;
        }
    }

    /* Capture Settings and Task Manager */
    if (gui_settings_is_open()) out->settings_open = 1;
    if (gui_taskmgr_is_open()) out->taskmgr_open = 1;

    /* Capture installed desktop application processes */
    app_info_t procs[APP_MAX];
    int nprocs = app_snapshot(procs, APP_MAX);
    for (int i = 0; i < nprocs && out->app_count < SESSION_APPS_MAX; i++) {
        if (!procs[i].desktop) continue;
        const char* name = procs[i].name;
        if (!name || !name[0]) continue;
        /* Ignore standard desktop tools captured above */
        if (strcmp(name, "notepad") == 0 || strcmp(name, "browser") == 0 ||
            strcmp(name, "files") == 0 || strcmp(name, "settings") == 0 ||
            strcmp(name, "taskmgr") == 0) continue;

        /* Deduplicate */
        int exists = 0;
        for (int j = 0; j < out->app_count; j++) {
            if (strcmp(out->apps[j], name) == 0) {
                exists = 1;
                break;
            }
        }
        if (!exists) {
            kstrlcpy(out->apps[out->app_count], name, sizeof(out->apps[0]));
            out->app_count++;
        }
    }
}

int session_save(void) {
    session_state_t st;
    session_capture(&st);

    char num[32];
    ksnprintf(num, sizeof(num), "%d", st.terminals_open);
    cfg_set(SESSION_FILE, "terminals", num, SESSION_CONF_HEADER);

    cfg_set(SESSION_FILE, "files_open", st.files_open ? "1" : "0", SESSION_CONF_HEADER);
    cfg_set(SESSION_FILE, "files_path", st.files_open && st.files_path[0] ? st.files_path : NULL, SESSION_CONF_HEADER);

    cfg_set(SESSION_FILE, "browser_open", st.browser_open ? "1" : "0", SESSION_CONF_HEADER);
    cfg_set(SESSION_FILE, "browser_url", st.browser_open && st.browser_url[0] ? st.browser_url : NULL, SESSION_CONF_HEADER);

    cfg_set(SESSION_FILE, "notepad_open", st.notepad_open ? "1" : "0", SESSION_CONF_HEADER);
    cfg_set(SESSION_FILE, "notepad_path", st.notepad_open && st.notepad_path[0] ? st.notepad_path : NULL, SESSION_CONF_HEADER);
    cfg_set(SESSION_FILE, "notepad_dirty", st.notepad_dirty ? "1" : "0", SESSION_CONF_HEADER);

    cfg_set(SESSION_FILE, "settings_open", st.settings_open ? "1" : "0", SESSION_CONF_HEADER);
    cfg_set(SESSION_FILE, "taskmgr_open", st.taskmgr_open ? "1" : "0", SESSION_CONF_HEADER);

    /* Serialize installed applications list as comma-separated list */
    if (st.app_count > 0) {
        char app_buf[SESSION_APPS_MAX * (SESSION_APP_NAME_MAX + 1)];
        app_buf[0] = 0;
        int off = 0;
        for (int i = 0; i < st.app_count; i++) {
            off += ksnprintf(app_buf + off, sizeof(app_buf) - (size_t)off, "%s%s",
                             i > 0 ? "," : "", st.apps[i]);
        }
        cfg_set(SESSION_FILE, "apps", app_buf, SESSION_CONF_HEADER);
    } else {
        cfg_set(SESSION_FILE, "apps", NULL, SESSION_CONF_HEADER);
    }

    /* Serialize unsaved documents list */
    if (st.unsaved_count > 0) {
        char doc_buf[SESSION_DOCS_MAX * (FS_PATH_LEN + 1)];
        doc_buf[0] = 0;
        int off = 0;
        for (int i = 0; i < st.unsaved_count; i++) {
            off += ksnprintf(doc_buf + off, sizeof(doc_buf) - (size_t)off, "%s%s",
                             i > 0 ? ";" : "", st.unsaved_docs[i]);
        }
        cfg_set(SESSION_FILE, "unsaved_docs", doc_buf, SESSION_CONF_HEADER);
    } else {
        cfg_set(SESSION_FILE, "unsaved_docs", NULL, SESSION_CONF_HEADER);
    }

    return cfg_persist() ? 0 : 0;
}

int session_load(session_state_t* out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    char val[1024];
    uint32_t num = 0;

    if (cfg_get(SESSION_FILE, "terminals", val, sizeof(val)) && k_parse_u32(val, &num)) {
        out->terminals_open = (int)num;
    }

    if (cfg_get(SESSION_FILE, "files_open", val, sizeof(val))) {
        out->files_open = (strcmp(val, "1") == 0);
        if (out->files_open && cfg_get(SESSION_FILE, "files_path", val, sizeof(val))) {
            kstrlcpy(out->files_path, val, sizeof(out->files_path));
        }
    }

    if (cfg_get(SESSION_FILE, "browser_open", val, sizeof(val))) {
        out->browser_open = (strcmp(val, "1") == 0);
        if (out->browser_open && cfg_get(SESSION_FILE, "browser_url", val, sizeof(val))) {
            kstrlcpy(out->browser_url, val, sizeof(out->browser_url));
        }
    }

    if (cfg_get(SESSION_FILE, "notepad_open", val, sizeof(val))) {
        out->notepad_open = (strcmp(val, "1") == 0);
        if (out->notepad_open && cfg_get(SESSION_FILE, "notepad_path", val, sizeof(val))) {
            kstrlcpy(out->notepad_path, val, sizeof(out->notepad_path));
        }
    }

    if (cfg_get(SESSION_FILE, "notepad_dirty", val, sizeof(val))) {
        out->notepad_dirty = (strcmp(val, "1") == 0);
    }

    if (cfg_get(SESSION_FILE, "settings_open", val, sizeof(val))) {
        out->settings_open = (strcmp(val, "1") == 0);
    }

    if (cfg_get(SESSION_FILE, "taskmgr_open", val, sizeof(val))) {
        out->taskmgr_open = (strcmp(val, "1") == 0);
    }

    /* Parse comma-separated installed applications */
    if (cfg_get(SESSION_FILE, "apps", val, sizeof(val)) && val[0]) {
        char* cur = val;
        while (*cur && out->app_count < SESSION_APPS_MAX) {
            char* comma = strchr(cur, ',');
            if (comma) *comma = 0;
            if (*cur) {
                kstrlcpy(out->apps[out->app_count], cur, sizeof(out->apps[0]));
                out->app_count++;
            }
            if (!comma) break;
            cur = comma + 1;
        }
    }

    /* Parse unsaved documents list */
    if (cfg_get(SESSION_FILE, "unsaved_docs", val, sizeof(val)) && val[0]) {
        char* cur = val;
        while (*cur && out->unsaved_count < SESSION_DOCS_MAX) {
            char* semi = strchr(cur, ';');
            if (semi) *semi = 0;
            if (*cur) {
                kstrlcpy(out->unsaved_docs[out->unsaved_count], cur, sizeof(out->unsaved_docs[0]));
                out->unsaved_count++;
            }
            if (!semi) break;
            cur = semi + 1;
        }
    }

    return 0;
}

int session_restore(const session_state_t* s) {
    if (!s) return 0;
    int restored = 0;

    /* 1. Restore terminals */
    int term_target = s->terminals_open;
    if (term_target > 4) term_target = 4;
    for (int i = 0; i < term_target; i++) {
        gui_open_terminal();
        restored++;
    }

    /* 2. Restore Files explorer */
    if (s->files_open) {
        gui_open_files(s->files_path[0] ? s->files_path : NULL);
        restored++;
    }

    /* 3. Restore Web browser */
    if (s->browser_open) {
        gui_open_browser(s->browser_url[0] ? s->browser_url : NULL);
        restored++;
    }

    /* 4. Restore Notepad (documents reopened) */
    if (s->notepad_open) {
        gui_open_notepad(s->notepad_path[0] ? s->notepad_path : NULL);
        restored++;
    }

    /* 5. Restore Settings and Task Manager */
    if (s->settings_open) {
        gui_open_settings();
        restored++;
    }
    if (s->taskmgr_open) {
        gui_open_taskmgr();
        restored++;
    }

    /* 6. Restore installed third-party applications (verified via pkg_get) */
    for (int i = 0; i < s->app_count; i++) {
        pkg_info_t info;
        if (pkg_get(s->apps[i], &info) == 0) {
            char err[96];
            if (pkg_run(s->apps[i], 0, NULL, 1, err, sizeof(err)) >= 0) {
                restored++;
            }
        }
        /* Note: deleted/uninstalled applications are intentionally not launched */
    }

    /* 7. Warn about unsaved data rather than pretending full volatile memory was restored */
    if (s->unsaved_count > 0 || s->notepad_dirty) {
        char body[NOTIFY_BODY_MAX];
        if (s->unsaved_count > 0) {
            ksnprintf(body, sizeof(body), "Unsaved changes in %s could not be fully restored from memory",
                      s->unsaved_docs[0]);
        } else {
            kstrlcpy(body, "Unsaved changes were present in session before restart", sizeof(body));
        }
        notify_post("Session", "Unsaved Data Warning", body, 0);
    }

    return restored;
}

int session_restore_startup(void) {
    if (!session_restore_is_enabled()) return 0;
    session_state_t st;
    if (session_load(&st) != 0) return 0;
    return session_restore(&st);
}

int session_clear(void) {
    if (fs_find_file(SESSION_FILE) >= 0) fs_delete(SESSION_FILE, 0);
    return 0;
}
