/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TERMINAL_PREFS_H
#define TERMINAL_PREFS_H
#include <stdint.h>
#include <stdio.h>
typedef struct { int font,theme,cursor,shell; uint32_t fg,bg,cursor_color; } term_prefs_t;
extern const char* const term_themes[4];
extern const char* const term_cursors[3];
extern const char* const term_shells[3];
void term_prefs_defaults(term_prefs_t*);
int term_prefs_set(term_prefs_t*,const char*,const char*);
void term_prefs_load(term_prefs_t*);
int term_prefs_save(const term_prefs_t*);
void term_prefs_print(const term_prefs_t*,FILE*);
#endif
