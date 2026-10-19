/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FSCMDS_H
#define FSCMDS_H

/* The filesystem's Unix side (shell/fscmds.c): `chmod`, `chown`, `chgrp`,
 * `ln -s`, `readlink`, `stat`, `fsck`, and `zip` / `unzip`. Returns 1 if `line` was one of
 * them (and ran it). */
int fscmd_dispatch(const char* line);

#endif
