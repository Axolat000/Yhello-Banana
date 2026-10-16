#ifndef _ASSERT_H
#define _ASSERT_H
#ifdef __cplusplus
extern "C" {
#endif
void __assert_fail(const char* expr, const char* file, int line);
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) ((e) ? (void)0 : __assert_fail(#e, __FILE__, __LINE__))
#endif
#ifndef __cplusplus
#define static_assert _Static_assert
#endif
#ifdef __cplusplus
}
#endif
#endif
