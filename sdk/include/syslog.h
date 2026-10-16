#ifndef _SYSLOG_H
#define _SYSLOG_H
#ifdef __cplusplus
extern "C" {
#endif
/* the system log: messages go to the app's standard error */
#include <stdarg.h>
#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7
#define LOG_PID     0x01
#define LOG_CONS    0x02
#define LOG_NDELAY  0x08
#define LOG_USER    (1 << 3)
#define LOG_DAEMON  (3 << 3)
#define LOG_MASK(p) (1 << (p))
#define LOG_UPTO(p) ((1 << ((p) + 1)) - 1)
void openlog(const char* ident, int option, int facility);
void syslog(int priority, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void vsyslog(int priority, const char* fmt, va_list ap);
void closelog(void);
int  setlogmask(int mask);
#ifdef __cplusplus
}
#endif
#endif
