/* SPDX-License-Identifier: BSD-2-Clause */
/* (assert is defined again at every include, by NDEBUG then: the standard's
 * rule - gnulib includes this after undefining it) */
#ifndef _ASSERT_H
#define _ASSERT_H
#ifdef __cplusplus
extern "C" {
#endif
void __assert_fail(const char* expr, const char* file, int line);
#ifndef __cplusplus
#define static_assert _Static_assert
#endif
#ifdef __cplusplus
}
#endif
#endif

#undef assert
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) ((e) ? (void)0 : __assert_fail(#e, __FILE__, __LINE__))
#endif
