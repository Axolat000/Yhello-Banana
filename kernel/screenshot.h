#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include "types.h"

/*
 * Screenshot capture subsystem.
 * Encodes full framebuffer or specified dimensions into standard PNG format.
 * Supports custom destination folder (defaults to /home/banana/Pictures),
 * automatic collision handling (Screenshot-YYYY-MM-DD-NN.png / Screenshot-NN.png),
 * and triggers visible feedback notification on the desktop.
 */

#define SCREENSHOT_DIR_DEFAULT "/home/banana/Pictures"

/* Set the default directory for saved screenshots. */
int screenshot_set_directory(const char* dir);

/* Get the current screenshot directory into out (at most cap bytes). */
const char* screenshot_get_directory(char* out, int cap);

/*
 * Captures the current screen and saves it as a PNG file in target_dir
 * (or the configured screenshot directory if target_dir is NULL/empty).
 * If notify is non-zero, posts a desktop notification with visible feedback.
 * If out_path is provided, the written path is stored in out_path (cap bytes).
 * Returns 0 on success, or -1 on error.
 */
int screenshot_take(const char* target_dir, int notify, char* out_path, int cap);

/*
 * Low-level PNG encoder: encodes 24-bit RGB or 32-bit XRGB/RGBA pixels
 * into standard PNG file bytes.
 * rgb_pixels: row-major pixels
 * is_bgrx: 1 if src is 32-bit XRGB/BGRX (like fb_target), 0 if src is 24-bit RGB
 * out_len: pointer where output size is written
 * Returns kmalloc'd PNG buffer (caller must kfree), or NULL on failure.
 */
uint8_t* screenshot_encode_png(const uint8_t* pixels, int w, int h, int stride_bytes, int is_bgrx, uint32_t* out_len);

#endif
