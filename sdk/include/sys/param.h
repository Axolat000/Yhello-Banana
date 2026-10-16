#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H
#ifdef __cplusplus
extern "C" {
#endif
#include <limits.h>
#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif
#define MAXHOSTNAMELEN 64
#define NBBY 8
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifdef __cplusplus
}
#endif
#endif
