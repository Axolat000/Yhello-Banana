/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BANANA_LIBJS_PROBE_H
#define BANANA_LIBJS_PROBE_H
#ifdef __cplusplus
extern "C" {
#endif
unsigned banana_libjs_test_count(void);
int banana_libjs_selftest(void (*report)(const char*,int));
int banana_libjs_eval(const char* code,char* output,unsigned capacity);
#ifdef __cplusplus
}
#endif
#endif
