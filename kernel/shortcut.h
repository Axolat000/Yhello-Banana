/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SHORTCUT_H
#define SHORTCUT_H

#include "types.h"
#include "fs.h"

/*
 * Shortcuts (kernel/shortcut.c): small ".link" files - on the desktop, or
 * anywhere - that open something else: a program, a web address, a folder
 * or a file. The file is text:
 *
 *     [Shortcut]
 *     Target=https://example.com          (or app:Notepad, or /home/banana/Music)
 *
 * The desktop draws one with its target's icon and an arrow; opening it -
 * from the desktop or from Files - opens the target.
 */

#define SHORTCUT_EXT ".link"

enum { SC_NONE, SC_APP, SC_WEB, SC_DIR, SC_FILE };

int  shortcut_is(const char* name);                       /* ends in .link */
/* the target of the shortcut file at path: its kind (SC_*), or SC_NONE */
int  shortcut_read(const char* path, char* target, int cap);
int  shortcut_kind(const char* target);
/* what someone typed ("notepad", "example.com", "/home/banana/Music") as a
 * target, and a name for it; SC_NONE if it is none of them */
int  shortcut_resolve(const char* typed, char* target, int tcap, char* name, int ncap);
/* writes dir/name.link (name (2).link... if taken); 0 = ok, its path in out */
int  shortcut_create(const char* dir, const char* name, const char* target, char* out, int ocap);

#endif
