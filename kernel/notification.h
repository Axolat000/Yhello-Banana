/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include "types.h"

/*
 * Desktop notification service.
 * Supports title, text/body, source application name, and action identifier.
 * Provides rate-limiting per source application, toast auto-dismiss,
 * and a bounded ring-buffer history.
 */

#define NOTIFY_TITLE_MAX 48
#define NOTIFY_BODY_MAX  128
#define NOTIFY_APP_MAX   24
#define NOTIFY_HIST_MAX  16
#define NOTIFY_RATE_MAX_BURST 3     /* max 3 notifications */
#define NOTIFY_RATE_WINDOW_MS 2000  /* per 2 seconds per app */
#define NOTIFY_TOAST_DURATION_MS 5000 /* 5s visible toast */

typedef void (*notify_action_fn)(int action_id, void* user_data);

typedef struct {
    uint32_t id;
    char     title[NOTIFY_TITLE_MAX];
    char     body[NOTIFY_BODY_MAX];
    char     app[NOTIFY_APP_MAX];
    int      action_id;
    uint32_t timestamp_ms;
    int      read;
} notify_item_t;

void notify_init(void);

/*
 * Post a notification. Returns notification ID (> 0) on success,
 * 0 if rate-limited (dropped), or -1 on invalid parameter.
 */
int notify_post(const char* app, const char* title, const char* body, int action_id);

/* Register custom callback for notification actions */
void notify_set_action_callback(notify_action_fn cb, void* user_data);

/* Invoke action corresponding to notification id */
int notify_trigger_action(uint32_t id);

/* History querying */
int notify_count(void);
int notify_peek(int index, notify_item_t* out);
int notify_clear_history(void);

/* Active toast inspection & management */
int notify_has_active_toast(void);
int notify_get_active_toast(notify_item_t* out, uint32_t* remaining_ms);
void notify_dismiss_toast(void);

/* Periodic poll for toast expiry */
void notify_poll(void);

#include "fb.h"
void notify_draw_toast(const fb_info_t* fi);
int notify_click(const fb_info_t* fi, int mx, int my);

#endif
