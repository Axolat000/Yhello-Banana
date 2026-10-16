#ifndef _SYS_PRCTL_H
#define _SYS_PRCTL_H
#ifdef __cplusplus
extern "C" {
#endif
#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define PR_SET_TIMERSLACK 29
#define PR_GET_TIMERSLACK 30
#define PR_SET_DUMPABLE 4
int prctl(int option, ...);
#ifdef __cplusplus
}
#endif
#endif
