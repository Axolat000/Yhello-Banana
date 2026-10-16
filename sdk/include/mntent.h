#ifndef _MNTENT_H
#define _MNTENT_H
#ifdef __cplusplus
extern "C" {
#endif
/* the mounted file systems (/etc/mtab): "/" and /mnt/usb when a stick is in */
#include <stdio.h>
#define MOUNTED "/etc/mtab"
#define _PATH_MOUNTED MOUNTED
struct mntent {
    char* mnt_fsname;
    char* mnt_dir;
    char* mnt_type;
    char* mnt_opts;
    int   mnt_freq, mnt_passno;
};
FILE*          setmntent(const char* file, const char* mode);
struct mntent* getmntent(FILE* f);
struct mntent* getmntent_r(FILE* f, struct mntent* out, char* buf, int len);
int            endmntent(FILE* f);
char*          hasmntopt(const struct mntent* m, const char* opt);
#ifdef __cplusplus
}
#endif
#endif
