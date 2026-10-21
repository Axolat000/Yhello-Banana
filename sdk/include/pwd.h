/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _PWD_H
#define _PWD_H
#ifdef __cplusplus
extern "C" {
#endif
/* the one user: banana (uid 1000, home /home/banana) */
#include <sys/types.h>
struct passwd { char* pw_name; char* pw_passwd; uid_t pw_uid; gid_t pw_gid; char* pw_gecos; char* pw_dir; char* pw_shell; };
struct passwd* getpwuid(uid_t uid);
struct passwd* getpwnam(const char* name);
int getpwuid_r(uid_t uid, struct passwd* pw, char* buf, size_t len, struct passwd** out);
int getpwnam_r(const char* name, struct passwd* pw, char* buf, size_t len, struct passwd** out);
void setpwent(void);
struct passwd* getpwent(void);
void endpwent(void);
#ifdef __cplusplus
}
#endif
#endif
