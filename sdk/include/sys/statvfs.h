#ifndef _SYS_STATVFS_H
#define _SYS_STATVFS_H
#ifdef __cplusplus
extern "C" {
#endif
/* a file system's size and free space (the disk a path is on) */
#include <sys/types.h>
struct statvfs {
    unsigned long f_bsize, f_frsize;
    fsblkcnt_t    f_blocks, f_bfree, f_bavail;
    fsfilcnt_t    f_files, f_ffree, f_favail;
    unsigned long f_fsid, f_flag, f_namemax;
    int           __spare[6];
};
#define ST_RDONLY 1
#define ST_NOSUID 2
int statvfs(const char* path, struct statvfs* out);
int fstatvfs(int fd, struct statvfs* out);
#define statvfs64 statvfs
#define fstatvfs64 fstatvfs
#ifdef __cplusplus
}
#endif
#endif
