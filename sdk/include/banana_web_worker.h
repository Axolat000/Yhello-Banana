/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BANANA_WEB_WORKER_H
#define BANANA_WEB_WORKER_H
/* Versioned wire data only: no pointers or native size_t values. */
#define WW_VERSION 1u
#define WW_CHUNK 60000u
#define WW_DOCUMENT (8u << 20)
#define WW_PIXELS 8000000u
#define WW_POST (1u << 20)
#define WW_REQUEST_BODY (48u << 10)
enum { WW_WAIT=1, WW_INPUT, WW_OUTPUT, WW_FINISH, WW_RESOURCE, WW_RESPONSE };
enum { WW_INIT=1, WW_LOAD, WW_UPDATE, WW_CLICK, WW_KEY, WW_LIVE, WW_TICK,
       WW_LINK, WW_PICK, WW_RENDER, WW_TEXTPOS, WW_TEXTRANGE, WW_EVAL, WW_POSTMSG,
       WW_DECODE, WW_TEST };
enum { WW_RESULT=0, WW_NAVBODY=1 };
enum { WW_FETCH=1, WW_REQUEST, WW_COOKIE_GET, WW_COOKIE_SET, WW_FILE, WW_MESSAGE, WW_FONT };
typedef struct {
    unsigned int version, op, length;
    int a[12];
    char url[8192];
} ww_command_t;
typedef struct { unsigned int offset, kind; } ww_io_t;
typedef struct {
    unsigned int version;
    int result, height, dirty, focus, textarea, js, live_keys, oom;
    int alert_pending, nav_pending, nav_newtab, file_pick_pending, scroll_req;
    unsigned int nav_post_len;
    char title[128], status[200], alert[256], nav[8192], nav_post_type[96];
} ww_state_t;
typedef struct {
    unsigned int kind, arg, length;
    char url[8192], method[8], type[96];
    unsigned char body[WW_REQUEST_BODY];
} ww_resource_t;
typedef struct {
    int result;
    unsigned int length;
    char type[96], url[8192], error[160];
} ww_response_t;
#endif
