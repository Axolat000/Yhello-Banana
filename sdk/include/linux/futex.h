#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H
#ifdef __cplusplus
extern "C" {
#endif
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_WAIT_PRIVATE (FUTEX_WAIT | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_PRIVATE (FUTEX_WAKE | FUTEX_PRIVATE_FLAG)
#ifdef __cplusplus
}
#endif
#endif
