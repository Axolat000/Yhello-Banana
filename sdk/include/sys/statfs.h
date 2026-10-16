#ifndef _SYS_STATFS_H
#define _SYS_STATFS_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
typedef struct { int __val[2]; } fsid_t;
struct statfs {
    long f_type, f_bsize;
    unsigned long f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    fsid_t f_fsid;
    long f_namelen, f_frsize, f_flags, f_spare[4];
};
int statfs(const char* path, struct statfs* s);
int fstatfs(int fd, struct statfs* s);
#ifdef __cplusplus
}
#endif
#endif
