#ifndef SESSION_H
#define SESSION_H

#include "types.h"
#include "fs.h"

/*
 * Session save and restore subsystem (base-30 step 3).
 *
 * Restores desktop state across reboots or GUI restarts:
 *  - Windows / core apps (Terminals, Files, Browser, Notepad, Settings, Task Manager)
 *  - Registered user documents (reopened in appropriate viewers/editors)
 *  - Installed third-party packages (only launched if still installed;
 *    uninstalled packages are cleaned/omitted rather than crashing)
 *  - Explicitly warns and reports unsaved changes instead of claiming to
 *    restore raw volatile memory state.
 */

#define SESSION_FILE           "/etc/desktop_session.conf"
#define SESSION_APPS_MAX       16
#define SESSION_DOCS_MAX       16
#define SESSION_APP_NAME_MAX   32

typedef struct {
    int  terminals_open;       /* count of terminal windows */
    int  files_open;           /* Files explorer */
    char files_path[FS_PATH_LEN];
    int  browser_open;         /* Browser */
    char browser_url[FS_PATH_LEN];
    int  notepad_open;         /* Notepad */
    char notepad_path[FS_PATH_LEN];
    int  notepad_dirty;        /* 1 if Notepad has unsaved buffer */
    int  settings_open;        /* Settings app */
    int  taskmgr_open;         /* Task Manager */

    /* Installed desktop apps */
    int  app_count;
    char apps[SESSION_APPS_MAX][SESSION_APP_NAME_MAX];

    /* Unsaved documents or warning records */
    int  unsaved_count;
    char unsaved_docs[SESSION_DOCS_MAX][FS_PATH_LEN];
} session_state_t;

/* Capture current desktop session into state structure */
void session_capture(session_state_t* out);

/* Save current desktop session to disk (/etc/desktop_session.conf) */
int session_save(void);

/* Load session definition from disk into state structure */
int session_load(session_state_t* out);

/*
 * Restore a captured session state.
 * Reopens terminal windows, document viewers, file explorer, browser, and
 * installed packages that remain present.
 * If unsaved edits were flagged, posts desktop notification toasts to alert the user.
 * Returns number of restored windows/apps.
 */
int session_restore(const session_state_t* s);

/* Apply session restore from persistent storage on boot/startx if enabled */
int session_restore_startup(void);

/* Configuration option: whether session restore is enabled on startx */
int  session_restore_is_enabled(void);
void session_restore_set_enabled(int enabled);

/* Manually clear saved session file */
int session_clear(void);

#endif
