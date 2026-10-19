#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include "types.h"

/*
 * The system-wide clipboard supporting multiple formats:
 * - Text (UTF-8, up to 64 KiB)
 * - Image (PNG or raw image buffer, up to 2 MiB)
 * - Files (List of file paths, newline separated, up to 64 paths / 64 KiB)
 */

#define CLIPBOARD_MAX (64u * 1024u)
#define CLIPBOARD_IMAGE_MAX (2u * 1024u * 1024u)

typedef enum {
    CLIP_FORMAT_TEXT = 0,
    CLIP_FORMAT_IMAGE,
    CLIP_FORMAT_FILES
} clip_format_t;

/* Set text clipboard (convenience wrapper for backward compatibility) */
void        clipboard_set(const char* text, uint32_t len);
const char* clipboard_get(uint32_t* len);     /* "" when empty, never NULL */
uint32_t    clipboard_generation(void);       /* bumped on every change */

/* Extended multi-format clipboard API */
void          clipboard_set_data(clip_format_t format, const void* data, uint32_t len);
const void*   clipboard_get_data(clip_format_t* format, uint32_t* len);
clip_format_t clipboard_get_format(void);

/* Helpers specifically for image and file list */
int         clipboard_set_image(const void* png_data, uint32_t len);
const void* clipboard_get_image(uint32_t* len);
int         clipboard_set_files(const char* const* paths, int count);
int         clipboard_get_files(char* out_paths, uint32_t cap, uint32_t* len);

/* The clipboard history (Win+V): the last CLIP_HIST_MAX items copied, the
 * newest first - copying one that is there already moves it to the top.
 * Pinned ones are never pushed out or cleared, and are kept across
 * restarts (/etc/clipboard-pinned). Safe from any core (apps copy through
 * system calls); the pinned file is read and written by the desktop only. */
#define CLIP_HIST_MAX 25

int      clipboard_hist_count(void);
/* entry i (0: the newest): up to cap-1 bytes of it into out (NUL-ended),
 * its whole length in *len, pinned or not; 0, or -1 if there is no entry i */
int      clipboard_hist_peek(int i, char* out, uint32_t cap, uint32_t* len, int* pinned);
/* Extended peek including entry format */
int      clipboard_hist_peek_ex(int i, char* out, uint32_t cap, uint32_t* len, int* pinned, clip_format_t* fmt);

/* entry i becomes the clipboard (and the newest entry): 0 / -1 */
int      clipboard_hist_use(int i);
int      clipboard_hist_delete(int i);
int      clipboard_hist_pin(int i, int on);
/* removes every entry not pinned: how many */
int      clipboard_hist_clear(void);
uint32_t clipboard_hist_generation(void);     /* bumped on every change */
/* the pinned entries saved last time (once, when the desktop starts) */
void     clipboard_hist_load(void);

#endif
