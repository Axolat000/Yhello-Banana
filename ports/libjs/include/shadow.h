/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Types only for unused LibCore account APIs; no shadow database is exposed. */
#ifndef BANANA_LIBJS_SHADOW_H
#define BANANA_LIBJS_SHADOW_H
struct spwd { char* sp_namp; char* sp_pwdp; long sp_lstchg,sp_min,sp_max,sp_warn,sp_inact,sp_expire; unsigned long sp_flag; };
#endif
